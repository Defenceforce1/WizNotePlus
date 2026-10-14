#include "api/WizNativeClipperBridge.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "share/WizNetworkSecurity.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonArray>
#include <QEventLoop>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QTcpSocket>
#include <QProcess>
#include <QUuid>

int wizNativeClipperSelfTest(int argc, char** argv)
{
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    QApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    int failures = 0, checks = 0;
    const auto check = [&](bool ok, const char* label) { ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label; return ok; };
    WizDatabaseManager database("synthetic-clipper-account");
    if (!check(database.open(), "isolated synthetic clip database opens")) return 1;
    database.db().setKbGUID("synthetic-clipper-kb");
    database.db().setMeta("Account", "USERID", "synthetic-clipper-account");
    database.db().loadDatabaseInfo();
    bool unavailable = false;
    const QString tokenFile = root.filePath("connection/token.json");
    WizNativeClipperBridge bridge(database, tokenFile, [&] { return unavailable; });
    if (!check(bridge.start(0), "clip bridge binds only a random loopback port")) return 1;
    struct Response { int status; QJsonObject body; };
    const auto request = [&](const QByteArray& path, const QJsonObject& body, const QString& token = QString(),
                             const QByteArray& extraHeader = QByteArray(), quint16 serverPort = 0) {
        QNetworkAccessManager net;
        QNetworkRequest req{QUrl(QString("http://127.0.0.1:%1").arg(serverPort ? serverPort : bridge.port()) + QString::fromLatin1(path))};
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        if (!token.isEmpty()) req.setRawHeader("Authorization", "Bearer " + token.toLatin1());
        if (!extraHeader.isEmpty()) req.setRawHeader("Origin", extraHeader);
        auto* reply = net.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
        QEventLoop loop; QTimer timeout; timeout.setSingleShot(true);
        QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
        QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { reply->abort(); loop.quit(); });
        timeout.start(5000); if (!reply->isFinished()) loop.exec();
        return Response{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), QJsonDocument::fromJson(reply->readAll()).object()};
    };
    const QJsonObject account{{"accountID", "synthetic-clipper-kb"}};
    check(request("/v1/status", account).status == 401, "unpaired callers cannot discover accounts or folders");
    const QString code = bridge.createPairingCode();
    check(code.size() == 8 && request("/v1/pair", {{"code", code}}, {}, "https://example.invalid").status == 403,
          "browser-origin pairing is rejected even with a correct code");
    auto paired = request("/v1/pair", {{"code", code}});
    const QString token = paired.body.value("token").toString();
    check(paired.status == 200 && token.size() == 64 && paired.body.value("accountName") == "synthetic-clipper-account",
          "one-time pairing returns only local connection credentials and explicit destination");
    check(request("/v1/pair", {{"code", code}}).status == 401, "successful pairing consumes its short code");
    const auto permissions = QFileInfo(tokenFile).permissions();
    check(QFileInfo::exists(tokenFile) && !(permissions & (QFile::ReadGroup | QFile::ReadOther)), "connection token file is owner-only");
    check(request("/v1/status", account, token).status == 200, "paired native status returns folders");
    check(request("/v1/status", account, QString(64, '0')).status == 401, "incorrect tokens rejected");
    check(request("/v1/status", {{"accountID", "different-account"}}, token).status == 409, "account mismatch never silently changes destination");
    QString clean, error; bool remote = false;
    const QString malicious = "<p style=\"color:red;background-image:url(file:///private)\" onclick=\"bad()\">hello</p>"
        "<script>WizExplorerApp.CreateWizObject('QProcess')</script><iframe src='file:///private'></iframe>"
        "<a href='javascript:bad()'>link</a><img src='https://images.example.invalid/p.png' onerror='bad()'>"
        "<img src='data:image/svg+xml;base64,PHN2Zz4='><math><mi>x</mi><msup><mi>y</mi><mn>2</mn></msup></math>";
    const bool sanitized = WizNativeClipperBridge::sanitizeHtml(malicious, "https://example.invalid/article", clean, remote, error);
    const bool safeMath = sanitized
        && clean.contains("color:red") && clean.contains("<math>") && clean.contains("<mi>x</mi>")
        && clean.contains("<msup>") && clean.contains("</msup>")
        && !clean.contains("script") && !clean.contains("onclick") && !clean.contains("onerror") && !clean.contains("javascript:")
        && !clean.contains("file:") && !clean.contains("svg+xml") && !clean.contains("<img src=\"https:")
        && remote;
    if (!safeMath) qInfo().noquote() << "Synthetic sanitizer diagnostic:" << clean << error;
    check(safeMath, "Gumbo sanitization preserves text/math and converts remote images to safe links without downloading");
    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonObject clip{{"accountID", "synthetic-clipper-kb"}, {"requestID", id}, {"title", QStringLiteral("中文网页剪藏")},
        {"folder", QStringLiteral("/网页剪藏/")}, {"url", "https://example.invalid/article"}, {"html", malicious}};
    const auto saved = request("/v1/clip", clip, token);
    check(saved.status == 200 && !saved.body.value("guid").toString().isEmpty(), "native request creates an actual local Wiz note");
    WIZDOCUMENTDATA note;
    check(database.db().documentFromGuid(saved.body.value("guid").toString(), note)
        && note.strTitle == QStringLiteral("中文网页剪藏") && note.strLocation == QStringLiteral("/网页剪藏/")
        && note.strURL == "https://example.invalid/article", "saved note has correct account, folder, title and original URL");
    QString path;
    bool content = database.db().documentToTempHtmlFile(note, path);
    QFile html(path);
    content = content && html.open(QIODevice::ReadOnly);
    const QByteArray bytes = content ? html.readAll() : QByteArray();
    check(bytes.contains("hello") && !bytes.contains("WizExplorerApp.CreateWizObject") && !bytes.contains("onclick="),
          "saved original Wiz container contains sanitized full body rather than only a link");
    const auto duplicate = request("/v1/clip", clip, token);
    check(duplicate.status == 200 && duplicate.body.value("duplicate").toBool()
        && duplicate.body.value("guid") == saved.body.value("guid"), "network retry is idempotent and creates no duplicate note");
    auto changed = clip; changed["html"] = "<p>different</p>";
    check(request("/v1/clip", changed, token).status == 409, "same request identity cannot replace a previously saved note");
    changed = clip; changed["requestID"] = QUuid::createUuid().toString(QUuid::WithoutBraces); changed["folder"] = "/../escape/";
    check(request("/v1/clip", changed, token).status == 400, "unsafe folders rejected without a new note");
    changed["folder"] = "/Nonexistent/";
    check(request("/v1/clip", changed, token).status == 409, "unadvertised folders rejected");
    changed = clip; changed["url"] = "file:///private";
    check(request("/v1/clip", changed, token).status == 400, "local-file source URL rejected");
    changed = clip; changed["requestID"] = "'; DROP TABLE WIZ_DOCUMENT;--";
    check(request("/v1/clip", changed, token).status == 400, "request ID rejects SQL/path injection before database access");
    unavailable = true;
    check(request("/v1/clip", clip, token).status == 503, "quit/save-busy state rejects clipping without writing notes");
    unavailable = false;
    WizNativeClipperBridge reopened(database, tokenFile, [] { return false; });
    check(reopened.start(0), "new bridge instance can restore the owner-only pairing");
    check(request("/v1/status", account, token, {}, reopened.port()).status == 200,
          "restored token authenticates to the reopened bridge");
    check(request("/arbitrary/database", account, token).status == 404, "paired client has no arbitrary database or process API");
    const QString limitedCode = bridge.createPairingCode();
    for (int i = 0; i < 5; ++i) request("/v1/pair", {{"code", "invalid"}});
    check(request("/v1/pair", {{"code", limitedCode}}).status == 401, "five incorrect pairing attempts invalidate the short code");
    CWizDocumentDataArray all;
    check(database.db().getDocumentsByLocation("/", all, true) && all.size() == 1, "rejected/retried requests leave exactly one original note");
    auto dropped = clip; dropped["requestID"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto disconnectOnSave = QObject::connect(&bridge, &WizNativeClipperBridge::noteSaved, &app, [&] {
        check(bridge.isSaving(), "bridge advertises local-save busy state during completion signals");
        for (auto* socket : bridge.findChildren<QTcpSocket*>()) socket->abort();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    const auto lostAcknowledgement = request("/v1/clip", dropped, token);
    QObject::disconnect(disconnectOnSave);
    check(lostAcknowledgement.status == 0, "client disconnect during save does not dereference a deleted socket");
    const auto retried = request("/v1/clip", dropped, token);
    check(retried.status == 200 && retried.body.value("duplicate").toBool(),
          "retry after lost acknowledgement recovers the saved note without duplication");
    all.clear();
    check(database.db().getDocumentsByLocation("/", all, true) && all.size() == 2,
          "disconnect regression creates exactly one additional note");
    qInfo() << "Native clipping checks:" << checks << "failures:" << failures;
    return failures ? 1 : 0;
}

int wizNativeClipperTransportSelfTest(int argc, char** argv)
{
    // Only this isolated route launches the Swift client, using a random port
    // and an ephemeral synthetic database. No real login or keychain is read.
    if (argc != 3 || !QFileInfo(QString::fromLocal8Bit(argv[2])).isAbsolute()
        || QFileInfo(QString::fromLocal8Bit(argv[2])).fileName() != "WizNotePlusClipper") return 2;
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    QApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    WizDatabaseManager database("synthetic-clipper-account");
    if (!database.open()) return 1;
    database.db().setKbGUID("synthetic-clipper-kb");
    database.db().setMeta("Account", "USERID", "synthetic-clipper-account");
    database.db().loadDatabaseInfo();
    WizNativeClipperBridge bridge(database, root.filePath("connection/token.json"), [] { return false; });
    if (!bridge.start(0) || bridge.port() == 8848 || bridge.port() == 8849) return 1;
    QProcess client;
    QEventLoop loop;
    QTimer timeout; timeout.setSingleShot(true);
    bool timedOut = false;
    QObject::connect(&timeout, &QTimer::timeout, &loop, [&] { timedOut = true; client.kill(); loop.quit(); });
    QObject::connect(&client, &QProcess::finished, &loop, &QEventLoop::quit);
    QObject::connect(&client, &QProcess::errorOccurred, &loop, [&] { loop.quit(); });
    client.start(QString::fromLocal8Bit(argv[2]), {"--bridge-self-test", QString::number(bridge.port()), bridge.createPairingCode()});
    timeout.start(30000); loop.exec();
    if (client.state() != QProcess::NotRunning) { client.kill(); client.waitForFinished(1000); }
    qInfo().noquote() << client.readAllStandardOutput() << client.readAllStandardError();
    CWizDocumentDataArray all;
    QString path;
    bool ok = !timedOut && client.exitStatus() == QProcess::NormalExit && client.exitCode() == 0
        && database.db().getDocumentsByLocation("/", all, true) && all.size() == 1
        && all.front().strTitle == QStringLiteral("Swift 原生剪藏测试")
        && all.front().strLocation == QStringLiteral("/网页剪藏/")
        && database.db().documentToTempHtmlFile(all.front(), path);
    QFile html(path);
    ok = ok && html.open(QIODevice::ReadOnly) && html.readAll().contains("synthetic-swift-clip");
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << "actual Swift Foundation transport saves one original Wiz container in a synthetic account";
    return ok ? 0 : 1;
}

int wizNativeClipperSafariProbe(int argc, char** argv)
{
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    WizDatabaseManager database("synthetic-clipper-account");
    if (!database.open()) return 1;
    database.db().setKbGUID("synthetic-clipper-kb");
    database.db().setMeta("Account", "USERID", "synthetic-clipper-account");
    database.db().loadDatabaseInfo();
    WizNativeClipperBridge bridge(database, root.filePath("connection/token.json"), [] { return false; });
    // Do not displace a real running bridge. No normal login or sync thread.
    if (!bridge.start(8849)) { qCritical() << "Synthetic Safari probe port is busy; no endpoint replaced."; return 1; }
    qInfo().noquote() << "SYNTHETIC SAFARI PROBE pairing code:" << bridge.createPairingCode();
    qInfo() << "No real account or cloud sync; temporary notes removed on normal test exit. Lifetime: 10 minutes.";
    int savedCount = 0;
    QObject::connect(&bridge, &WizNativeClipperBridge::noteSaved, &app, [&](const QString& guid) {
        WIZDOCUMENTDATA note;
        QString path;
        const bool saved = database.db().documentFromGuid(guid, note) && database.db().documentToTempHtmlFile(note, path);
        QFile html(path);
        const bool body = saved && html.open(QIODevice::ReadOnly) && html.size() > 0;
        const QByteArray content = body ? html.readAll() : QByteArray();
        qInfo().noquote() << (body ? "PASS" : "FAIL") << "Safari saved actual original Wiz container in synthetic account; title:" << note.strTitle;
        qInfo() << "Synthetic capture flags: full-page" << content.contains("synthetic-safari-full-page")
            << "selected-paragraph" << content.contains(QStringLiteral("选中测试段落").toUtf8())
            << "table" << content.contains("<table") << "math" << content.contains("<math")
            << "computed-color" << content.contains("rgb(38, 57, 80)")
            << "embedded-image" << (content.contains("index_files/") || content.contains("data:image/png;base64,"))
            << "math-superscript" << content.contains("<msup");
        qInfo().noquote() << "SYNTHETIC SAFARI PROBE next pairing code:" << bridge.createPairingCode();
        if (++savedCount == 2) QTimer::singleShot(5000, &app, &QApplication::quit);
    });
    QTimer::singleShot(10 * 60 * 1000, &app, &QApplication::quit);
    return app.exec();
}
