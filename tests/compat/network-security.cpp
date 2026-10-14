#include "share/WizNetworkSecurity.h"
#include "share/WizRequest.h"
#include "share/jsoncpp/json/json.h"
#include "WizBuildIdentity.h"
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
            received += socket->readAll();
            if (!socket->property("responded").toBool() && received.contains("\r\n\r\n")) {
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
    QSslConfiguration::setDefaultConfiguration(systemConfiguration);
    check(QSslConfiguration::defaultConfiguration().caCertificates() == systemConfiguration.caCertificates(),
          "test trust removed; no persistent system CA installation");
    qInfo() << "Network security checks:" << checks << "failures:" << failures;
    return failures ? 1 : 0;
}
