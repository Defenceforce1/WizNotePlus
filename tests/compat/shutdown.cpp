#include "share/WizEventLoop.h"
#include "share/WizNetworkSecurity.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "gui/documentviewer/DocumentLoaderSaver.h"
#include "sync/WizKMSync.h"
#include "sync/WizApiEntry.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QElapsedTimer>
#include <QTimer>
#include <QFile>
#include <atomic>
#include <memory>

// Separate process, synthetic database and loopback sockets only. No login,
// global network disconnection, real account, cloud requests or forced exit.
int wizShutdownSelfTest(int argc, char** argv)
{
    QTemporaryDir isolation;
    if (!isolation.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", isolation.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", isolation.filePath("cache").toUtf8());
    QApplication app(argc, argv);
    QNetworkProxy::setApplicationProxy(QNetworkProxy::NoProxy);
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    WizDatabaseManager manager("synthetic-shutdown-account");
    if (!check(manager.open(), "fresh isolated database opens")) return 1;
    manager.db().setKbGUID("synthetic-shutdown-kb");
    QList<WIZDOCUMENTDATA> notes;
    for (int i = 0; i < 6; ++i) {
        WIZDOCUMENTDATA note;
        if (!manager.db().createDocumentAndInit("<html><body>before</body></html>", "", 0,
                QString("Synthetic %1").arg(i), "synthetic.html", "/Shutdown/", "", note)) return 1;
        notes.append(note);
    }
    WizDocumentSaverThread saver(manager, nullptr);
    for (int i = 0; i < notes.size(); ++i) {
        const QString html = QString("<html><body>saved-before-exit-%1</body></html>").arg(i);
        saver.save(notes[i], html, "", 0);
    }
    saver.waitForDone(); // Immediately stop while several saves are still queued.
    check(saver.isIdleForTransfer() && !saver.hasTransferError(), "shutdown drains every queued local save");
    for (int i = 0; i < notes.size(); ++i) {
        QString path;
        bool ok = manager.db().documentToTempHtmlFile(notes[i], path);
        QFile file(path);
        ok = ok && file.open(QIODevice::ReadOnly)
            && file.readAll().contains(QString("saved-before-exit-%1").arg(i).toUtf8());
        check(ok, "each queued note retains its changed content on disk");
    }
    WizDocumentSaverThread failingSaver(manager, nullptr);
    WIZDOCUMENTDATA missing = notes[0]; missing.strGUID = "missing-synthetic-note";
    failingSaver.save(missing, "<html><body>cannot save</body></html>", "", 0);
    failingSaver.waitForDone();
    check(failingSaver.hasTransferError(), "failed disk/database save is not treated as success");
    failingSaver.clearTransferError();
    check(failingSaver.hasTransferError(), "clearing a transfer status cannot conceal an unresolved failed note");

    WizKMSyncThread idle(manager.db(), true);
    idle.stopSync(); idle.addQuickSyncKb("synthetic-shutdown-kb");
    idle.startSyncAll();
    check(idle.isIdle(), "stopped sync worker rejects new queued work before startup");
    idle.start();
    check(idle.wait(1000), "a stopped worker cannot restart synchronization");

    check(WizKMSyncThread::quitSyncTimeoutMs == 5000, "final sync network-stage budget is five seconds, not twenty");
    // Empty server means explicit offline, without DNS or any cloud request.
    check(WizCommonApiEntry::server().isEmpty(), "offline completion fixture has no server or credentials");
    WizKMSyncThread offline(manager.db(), false);
    offline.setFullSyncInterval(60);
    int attempts = 0, uiErrors = 0;
    QObject::connect(&offline, &WizKMSyncThread::fullSyncAttemptFinished, &app, [&] { ++attempts; });
    QObject::connect(&offline, &WizKMSyncThread::syncFinished, &app,
        [&](int, bool, const QString&, bool) { ++uiErrors; });
    offline.start();
    for (int target = 1; target <= 2; ++target) {
        offline.startSyncAll();
        QElapsedTimer attempt; attempt.start();
        while ((attempts < target || !offline.isIdle()) && attempt.elapsed() < 4500) {
            app.processEvents(); QThread::msleep(5);
        }
        check(attempts == target && attempt.elapsed() < 4500,
              "each offline final attempt reports completion without exhausting quit budget");
    }
    check(uiErrors == 1 && attempts == 2, "suppressed repeated offline UI errors cannot conceal attempt completion");
    offline.stopSync();
    check(offline.wait(1000), "offline fixture closes its worker normally");

    QTcpServer server;
    bool respond = true;
    int requests = 0;
    QObject::connect(&server, &QTcpServer::newConnection, &app, [&] {
        while (server.hasPendingConnections()) {
            auto* socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [&, socket] {
                socket->readAll(); ++requests;
                if (respond) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 2\r\nConnection: close\r\n\r\nOK");
                    socket->disconnectFromHost();
                }
            });
        }
    });
    if (!check(server.listen(QHostAddress::LocalHost), "bounded loopback network fixture ready")) return 1;
    const QUrl url(QString("http://127.0.0.1:%1/synthetic").arg(server.serverPort()));
    {
        QNetworkAccessManager net;
        WizAutoTimeOutEventLoop loop(net.get(QNetworkRequest(url)));
        loop.exec();
        check(loop.error() == QNetworkReply::NoError && loop.result() == "OK",
              "successful network operation still completes normally before shutdown");
    }
    respond = false;
    std::atomic_bool workerCanceled{false};
    auto worker = std::unique_ptr<QThread>(QThread::create([&] {
        QNetworkAccessManager net;
        WizAutoTimeOutEventLoop loop(net.get(QNetworkRequest(url)));
        loop.exec();
        workerCanceled = loop.error() == QNetworkReply::OperationCanceledError;
    }));
    worker->start();
    QTimer::singleShot(150, &app, [&] { worker->requestInterruption(); });
    QElapsedTimer elapsed; elapsed.start();
    while (worker->isRunning() && elapsed.elapsed() < 3000) {
        app.processEvents(); QThread::msleep(5);
    }
    check(!worker->isRunning() && workerCanceled && elapsed.elapsed() < 3000,
          "thread interruption cancels an in-flight stalled request promptly");
    if (worker->isRunning()) { worker.release(); return 1; }
    worker->wait();
    {
        QNetworkAccessManager net;
        WizAutoTimeOutEventLoop loop(net.get(QNetworkRequest(url)));
        QTimer::singleShot(150, &app, &WizNetworkSecurity::beginShutdown);
        elapsed.restart(); loop.exec();
        check(loop.error() == QNetworkReply::OperationCanceledError && elapsed.elapsed() < 3000,
              "process shutdown cancels stalled network waits without waiting for the 60-second timeout");
    }
    QNetworkRequest authenticated{QUrl("https://example.invalid/no-request")};
    check(!WizNetworkSecurity::prepareAccountRequest(authenticated), "shutdown rejects new authenticated requests and retries");
    QByteArray untouched("keep-existing-content");
    check(!WizNetworkSecurity::downloadAccountData(authenticated.url().toString(), untouched)
        && untouched == "keep-existing-content", "canceled downloads cannot replace existing local content");
    check(requests >= 3, "successful and stalled cases reached only the local fixture");
    qInfo() << "Shutdown checks:" << checks << "failures:" << failures;
    return failures ? 1 : 0;
}
