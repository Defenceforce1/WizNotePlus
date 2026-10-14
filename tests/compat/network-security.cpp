#include "share/WizNetworkSecurity.h"
#include "share/WizRequest.h"
#include "share/jsoncpp/json/json.h"
#include "WizBuildIdentity.h"
#include "sync/WizAvatarUploader.h"
#include <QImage>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QProcess>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslKey>
#include <QSslSocket>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QTimer>

// Only ephemeral loopback fixtures. No login, account store, external request,
// permanent CA installation, ignored certificate errors or private user data.
class FixtureServer final : public QTcpServer {
public:
    QSslCertificate certificate;
    QSslKey key;
    QByteArray response = "HTTP/1.1 200 OK\r\nContent-Length: 18\r\nConnection: close\r\n\r\n{\"returnCode\":200}";
    int accepted = 0, requests = 0;
    QByteArray received;
    explicit FixtureServer(QObject* parent = nullptr) : QTcpServer(parent) {}
protected:
    void incomingConnection(qintptr descriptor) override {
        ++accepted;
        auto* socket = new QSslSocket(this);
        socket->setSocketDescriptor(descriptor);
        socket->setLocalCertificate(certificate); socket->setPrivateKey(key);
        socket->setPeerVerifyMode(QSslSocket::QueryPeer); // server does not demand client certificates
        socket->setProtocol(QSsl::TlsV1_2OrLater);
        QObject::connect(socket, &QSslSocket::disconnected, socket, &QObject::deleteLater);
        QObject::connect(socket, &QSslSocket::readyRead, socket, [this, socket] {
            const QByteArray bytes = socket->readAll();
            received += bytes;
            const QByteArray request = socket->property("requestBytes").toByteArray() + bytes;
            socket->setProperty("requestBytes", request);
            const int end = request.indexOf("\r\n\r\n");
            if (end < 0) return;
            const auto length = QRegularExpression("Content-Length:\\s*(\\d+)", QRegularExpression::CaseInsensitiveOption)
                .match(QString::fromLatin1(request.left(end)));
            const int bodyLength = length.hasMatch() ? length.captured(1).toInt() : 0;
            if (!socket->property("responded").toBool() && request.size() >= end + 4 + bodyLength) {
                socket->setProperty("responded", true); ++requests;
                socket->write(response); socket->disconnectFromHost();
            }
        });
        socket->startServerEncryption();
    }
};

struct FetchResult { QNetworkReply::NetworkError error; bool sslErrors = false; QByteArray body; };
static FetchResult fetch(const QUrl& url, bool download = false) {
    QNetworkRequest request(url);
    if (!WizNetworkSecurity::prepareAccountRequest(request, download))
        return {QNetworkReply::ProtocolInvalidOperationError, false, {}};
    QNetworkAccessManager manager;
    manager.setProxy(QNetworkProxy::NoProxy);
    auto* reply = manager.get(request);
    QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
    FetchResult result{QNetworkReply::NoError, false, {}};
    QObject::connect(reply, &QNetworkReply::sslErrors, &loop, [&](const QList<QSslError>&) { result.sslErrors = true; });
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { reply->abort(); loop.quit(); });
    timeout.start(10000); if (!reply->isFinished()) loop.exec();
    result.error = reply->error();
    if (result.error == QNetworkReply::NoError) result.body = reply->readAll();
    return result;
}

int wizNetworkSecuritySelfTest(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    int failures = 0, checks = 0;
    const auto check = [&](bool ok, const char* description) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << description; return ok;
    };
    using namespace WizNetworkSecurity;
    installSecureDefaults();
    const auto systemConfiguration = QSslConfiguration::defaultConfiguration();
    check(systemConfiguration.peerVerifyMode() == QSslSocket::VerifyPeer, "production defaults verify server certificates");
    check(systemConfiguration.protocol() == QSsl::TlsV1_2OrLater, "production minimum TLS is 1.2");
    check(QSslSocket::supportsSsl() && !QSslSocket::activeBackend().isEmpty(), "packaged TLS backend available");
    check(!systemConfiguration.caCertificates().isEmpty(), "system CA store available");
    check(allowsAccountUrl(QUrl("https://api.wiz.cn/")), "official HTTPS account endpoint accepted");
    check(!WizBuildIdentity::appleSiliconTrial || !allowsAccountUrl(QUrl("http://api.wiz.cn/")), "Apple Silicon account traffic cannot use plaintext HTTP");
    check(!allowsAccountUrl(QUrl("https://user:secret@api.wiz.cn/")), "embedded URL credentials rejected");
    check(!allowsAccountUrl(QUrl("file:///tmp/private")) && !allowsAccountUrl(QUrl("https:///missing")), "invalid or non-network account destinations rejected");
    QNetworkRequest request(QUrl("https://api.wiz.cn/"));
    check(prepareAccountRequest(request) && request.sslConfiguration().peerVerifyMode() == QSslSocket::VerifyPeer
        && request.sslConfiguration().protocol() == QSsl::TlsV1_2OrLater, "actual account request enforces verified modern TLS");
    check(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::SameOriginRedirectPolicy,
          "authenticated requests cannot redirect credentials to another origin");
    prepareAccountRequest(request, true);
    check(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt() == QNetworkRequest::NoLessSafeRedirectPolicy,
          "resource downloads permit HTTPS CDN redirects but not downgrade");
    const QString secretUrl = "https://user:secret@api.wiz.cn/ks/test?token=fixture-token&a=token%3Dnested-secret#private";
    const QString redacted = redactLogUrls("failure \"" + secretUrl + "\" and https://as.wiz.cn/a?password=fixture-password");
    check(!redacted.contains("secret") && !redacted.contains("fixture-token") && !redacted.contains("fixture-password")
          && redacted.contains("https://api.wiz.cn/ks/test") && redacted.contains("https://as.wiz.cn/a"),
          "log URLs remove userinfo, all query values and fragments");
    check(redactLogUrls("ordinary diagnostic") == "ordinary diagnostic", "ordinary diagnostics unchanged");

    const QString openssl = argc == 3 ? QString::fromLocal8Bit(argv[2]) : QStringLiteral("openssl");
    QTemporaryDir temporary;
    if (!check(temporary.isValid(), "ephemeral certificate fixture created")) return 1;
    qputenv("WIZNOTE_DATA_STORE", temporary.filePath("isolated-store").toUtf8());
    qputenv("XDG_CACHE_HOME", temporary.filePath("isolated-cache").toUtf8());
    const auto run = [&](const QStringList& arguments) {
        QProcess process; process.setWorkingDirectory(temporary.path());
        process.start(openssl, arguments);
        const bool ok = process.waitForFinished(20000) && process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
        if (!ok) qWarning().noquote() << "Certificate fixture generator failed:" << arguments.first() << process.readAllStandardError();
        return ok;
    };
    QFile extensions(temporary.filePath("extensions.txt"));
    if (!extensions.open(QIODevice::WriteOnly)) return 1;
    extensions.write("basicConstraints=critical,CA:FALSE\nkeyUsage=digitalSignature,keyEncipherment\nextendedKeyUsage=serverAuth\nsubjectAltName=DNS:localhost\n");
    extensions.close();
    const auto writeFixture = [&](const QString& name, const QByteArray& contents) {
        QFile file(temporary.filePath(name));
        return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
    };
    if (!writeFixture("index.txt", {}) || !writeFixture("serial", "1000\n")
        || !writeFixture("ca.conf", "[ca]\ndefault_ca=test_ca\n[test_ca]\ndatabase=index.txt\nnew_certs_dir=.\ncertificate=ca.pem\nprivate_key=ca.key\nserial=serial\ndefault_md=sha256\ndefault_days=1\npolicy=test_policy\n[test_policy]\ncommonName=supplied\n")) return 1;
    const auto now = QDateTime::currentDateTimeUtc();
    const bool generated = run({"req", "-x509", "-newkey", "rsa:2048", "-nodes", "-keyout", "ca.key", "-out", "ca.pem", "-days", "2", "-subj", "/CN=WizNotePlus Ephemeral Test CA", "-addext", "basicConstraints=critical,CA:TRUE"})
        && run({"req", "-newkey", "rsa:2048", "-nodes", "-keyout", "server.key", "-out", "server.csr", "-subj", "/CN=localhost"})
        && run({"x509", "-req", "-in", "server.csr", "-CA", "ca.pem", "-CAkey", "ca.key", "-set_serial", "2", "-out", "server.pem", "-days", "1", "-extfile", "extensions.txt"})
        && run({"ca", "-batch", "-config", "ca.conf", "-in", "server.csr", "-out", "expired.pem", "-startdate", now.addDays(-2).toString("yyyyMMddhhmmss'Z'"), "-enddate", now.addDays(-1).toString("yyyyMMddhhmmss'Z'"), "-extfile", "extensions.txt"});
    if (!check(generated, "OpenSSL generated valid and expired loopback certificates")) return 1;
    const auto read = [&](const QString& name) { QFile file(temporary.filePath(name)); file.open(QIODevice::ReadOnly); return file.readAll(); };
    const QSslCertificate ca(read("ca.pem")), valid(read("server.pem")), expired(read("expired.pem"));
    FixtureServer server; server.certificate = valid; server.key = QSslKey(read("server.key"), QSsl::Rsa);
    if (!check(!valid.isNull() && !server.key.isNull() && server.listen(QHostAddress::LocalHost), "HTTPS loopback server ready")) return 1;
    const QUrl localhost(QString("https://localhost:%1/fixture").arg(server.serverPort()));
    auto result = fetch(localhost);
    check(result.error == QNetworkReply::SslHandshakeFailedError && result.sslErrors && server.requests == 0,
          "untrusted CA rejected before any account payload");
    auto trusted = systemConfiguration; trusted.addCaCertificate(ca);
    QSslConfiguration::setDefaultConfiguration(trusted);
    result = fetch(localhost);
    check(result.error == QNetworkReply::NoError && result.body == "{\"returnCode\":200}" && server.requests == 1,
          "valid trusted certificate and matching hostname work");
    Json::Value productionResponse;
    const auto production = WizRequest::execStandardJsonRequest(localhost.toString(), "POST", QByteArray("{\"fixture\":true}"), productionResponse);
    check(production.returnCode == 200 && server.requests == 2, "actual JSON account POST path succeeds with verified HTTPS");
    result = fetch(QUrl(QString("https://127.0.0.1:%1/fixture").arg(server.serverPort())));
    check(result.error == QNetworkReply::SslHandshakeFailedError && result.sslErrors && server.requests == 2,
          "trusted certificate with wrong hostname rejected before payload");
    server.certificate = expired;
    check(expired.expiryDate() < QDateTime::currentDateTimeUtc(), "expired fixture is genuinely expired");
    result = fetch(localhost);
    check(result.error == QNetworkReply::SslHandshakeFailedError && result.sslErrors && server.requests == 2,
          "expired certificate rejected before payload");
    server.certificate = valid;
    const int beforeHttp = server.accepted;
    auto plaintext = localhost; plaintext.setScheme("http");
    if (WizBuildIdentity::appleSiliconTrial) {
        Json::Value response;
        const auto denied = WizRequest::execStandardJsonRequest(plaintext.toString(), "POST", QByteArray("fixture-credential"), response);
        check(denied.returnCode < 0 && server.accepted == beforeHttp, "production JSON entry rejects HTTP before connecting");
    }
    QByteArray downloaded;
    check(downloadAccountData(localhost.toString(), downloaded) && downloaded == "{\"returnCode\":200}",
          "production note-resource download path works with verified HTTPS");
    server.certificate = expired;
    const int beforeFailedDownload = server.requests;
    QByteArray preserved("fixture existing note bytes");
    check(!downloadAccountData(localhost.toString(), preserved) && preserved == "fixture existing note bytes"
          && server.requests == beforeFailedDownload, "failed verified download preserves existing note bytes without sending payload");
    server.certificate = valid;
    const int beforeRedirect = server.requests;
    server.response = "HTTP/1.1 302 Found\r\nLocation: http://localhost:" + QByteArray::number(server.serverPort())
        + "/downgrade\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    result = fetch(localhost, true);
    check(result.error == QNetworkReply::InsecureRedirectError && server.requests == beforeRedirect + 1,
          "HTTPS resource redirect to HTTP blocked without retry");
    FixtureServer other; other.certificate = valid; other.key = server.key;
    other.listen(QHostAddress::LocalHost);
    server.response = "HTTP/1.1 302 Found\r\nLocation: https://localhost:" + QByteArray::number(other.serverPort())
        + "/other-origin\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
    result = fetch(QUrl(localhost.toString() + "?token=fixture-token"));
    check(result.error == QNetworkReply::InsecureRedirectError && other.accepted == 0,
          "account token never reaches another origin during redirect");
    WizAvatarUploader avatarUploader(nullptr);
    QImage image(240, 160, QImage::Format_ARGB32); image.fill(QColor("#539675"));
    const QString imagePath = temporary.filePath("synthetic-avatar.png");
    check(image.save(imagePath), "synthetic avatar image created without user pictures");
    const QByteArray avatar = avatarUploader.convert2Avatar(imagePath);
    check(QImage::fromData(avatar).size() == QSize(100, 100), "avatar is prepared as a bounded square PNG in memory");
    const auto initialFiles = QDir(temporary.path()).entryList(QDir::Files);
    avatarUploader.convert2Avatar(imagePath);
    check(initialFiles == QDir(temporary.path()).entryList(QDir::Files), "avatar preparation leaves no random temporary PNG files");
    bool avatarSucceeded = false; int avatarSignals = 0;
    QObject::connect(&avatarUploader, &WizAvatarUploader::uploaded, &app, [&](bool ok) {
        avatarSucceeded = ok; ++avatarSignals;
    });
    avatarUploader.upload(temporary.filePath("missing-synthetic-image.png"));
    check(avatarSignals == 1 && !avatarSucceeded && !avatarUploader.lastErrorMessage().isEmpty(),
          "unreadable avatar reports one clear completion before any login or request");
    const auto avatarResponse = [&](int status, const QByteArray& body, const QByteArray& extra = QByteArray()) {
        server.response = "HTTP/1.1 " + QByteArray::number(status) + " Fixture\r\nContent-Length: "
            + QByteArray::number(body.size()) + "\r\nConnection: close\r\n" + extra + "\r\n" + body;
        server.received.clear(); avatarSignals = 0; avatarSucceeded = false;
        avatarUploader.upload_impl(localhost.toString(), "synthetic-avatar-token", avatar);
        return avatarSignals == 1;
    };
    check(avatarResponse(200, "{\"returnCode\":200}") && avatarSucceeded,
          "actual avatar multipart upload succeeds with verified loopback HTTPS and structured result");
    check(server.received.startsWith("POST ") && server.received.contains("name=\"token\"")
          && server.received.contains("synthetic-avatar-token") && server.received.contains("name=\"avatar\"; filename=\"avatar.png\"")
          && server.received.contains("image/png") && !server.received.contains(temporary.path().toUtf8()),
          "avatar request preserves Wiz field names and does not transmit local file paths");
    check(avatarResponse(200, " 200 \n") && avatarSucceeded, "exact legacy avatar success code remains supported");
    check(avatarResponse(200, "{\"returnCode\":503,\"message\":\"not 200\"}") && !avatarSucceeded,
          "an unrelated 200 substring cannot report a rejected avatar as success");
    check(avatarResponse(200, "<html>200 avatars unavailable</html>") && !avatarSucceeded,
          "HTML or unexpected avatar responses are not success");
    check(avatarResponse(502, "private synthetic backend error") && !avatarSucceeded
          && avatarUploader.lastErrorMessage().contains("HTTP 502")
          && avatarUploader.lastErrorMessage().contains("499")
          && !avatarUploader.lastErrorMessage().contains("private synthetic"),
          "Qt server error 499 exposes actual HTTP 502 without leaking response bodies");
    check(avatarResponse(401, "denied") && !avatarSucceeded && avatarUploader.lastErrorMessage().contains("HTTP 401"),
          "avatar authentication denial reports actual HTTP status");
    const int beforeAvatarRedirect = other.accepted;
    check(avatarResponse(307, {}, "Location: https://localhost:" + QByteArray::number(other.serverPort()) + "/avatar\r\n")
          && !avatarSucceeded && other.accepted == beforeAvatarRedirect,
          "avatar redirects never replay credentials to another server");
    const int beforeUnsafeAvatar = server.accepted;
    avatarSignals = 0;
    avatarUploader.upload_impl(plaintext.toString(), "synthetic-avatar-token", avatar);
    check(avatarSignals == 1 && !avatarSucceeded && server.accepted == beforeUnsafeAvatar,
          "actual avatar upload rejects HTTP before sending credentials");
    // Use a fresh origin: an already verified live TLS connection is not
    // supposed to re-check a certificate merely changed on the fixture server.
    FixtureServer expiredAvatarServer;
    expiredAvatarServer.certificate = expired; expiredAvatarServer.key = server.key;
    check(expiredAvatarServer.listen(QHostAddress::LocalHost), "fresh expired-certificate avatar origin ready");
    avatarSignals = 0;
    avatarUploader.upload_impl(QString("https://localhost:%1/avatar").arg(expiredAvatarServer.serverPort()),
                               "synthetic-avatar-token", avatar);
    check(avatarSignals == 1 && !avatarSucceeded && expiredAvatarServer.requests == 0,
          "actual avatar upload rejects expired certificates before sending a picture or token");
    QSslConfiguration::setDefaultConfiguration(systemConfiguration);
    check(QSslConfiguration::defaultConfiguration().caCertificates() == systemConfiguration.caCertificates(),
          "test trust removed; no persistent system CA installation");
    qInfo() << "Network security checks:" << checks << "failures:" << failures;
    return failures ? 1 : 0;
}
