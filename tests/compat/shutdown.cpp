#include "share/WizEventLoop.h"
#include "share/WizNetworkSecurity.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "gui/documentviewer/DocumentLoaderSaver.h"
#include "gui/documentviewer/WizDocumentEditStatus.h"
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
#include <QSemaphore>
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
    {
        WizWaitEvent event;
        QSemaphore checkedEmpty, allowWait, firstReturned, allowSecond;
        std::unique_ptr<QThread> worker(QThread::create([&] {
            checkedEmpty.release();allowWait.acquire();event.wait();
            firstReturned.release();allowSecond.acquire();event.wait();
        }));
        worker->start();
        const bool started=checkedEmpty.tryAcquire(1,2000);
        event.wakeAll(); // Deterministically before this worker registers wait.
        allowWait.release();
        const bool received=firstReturned.tryAcquire(1,2000);
        check(started&&received,"loader/saver event remembers a notification before wait registration");
        allowSecond.release();
        check(received&&!worker->wait(50),"one notification is consumed once, without busy-waiting");
        event.wakeAll();
        QElapsedTimer rescue;rescue.start();
        while(!worker->wait(10)&&rescue.elapsed()<2000)event.wakeAll();
        check(worker->isFinished(),"subsequent notification completes and joins the isolated worker");
        if(!worker->isFinished()){worker.release();return 1;}
    }
    {
        bool joined=true,cleared=true;
        const auto correctOwner=std::make_shared<std::atomic_bool>(true);
        for(int i=0;i<32;++i){
            auto thread=std::make_unique<QThread>();
            QPointer<WizDocumentStatusChecker> checker=new WizDocumentStatusChecker;
            QObject::connect(thread.get(),&QThread::started,checker.data(),&WizDocumentStatusChecker::initialise);
            QObject::connect(thread.get(),&QThread::finished,checker.data(),&WizDocumentStatusChecker::clearTimers);
            QObject::connect(thread.get(),&QThread::finished,checker.data(),&QObject::deleteLater);
            QObject::connect(checker.data(),&QObject::destroyed,[owner=thread.get(),correctOwner]{
                if(QThread::currentThread()!=owner)*correctOwner=false;
            });
            checker->moveToThread(thread.get());thread->start();thread->requestInterruption();thread->quit();
            joined&=thread->wait(2000);cleared&=checker.isNull();
            if(!joined){thread.release();return 1;}
        }
        check(joined&&cleared&&correctOwner->load(),"32 rapid checker starts/closes join and destroy timers on the owning worker");
        bool syncStopped=true;
        for(int i=0;i<32;++i){WizDocumentEditStatusSyncThread status;
            if(i%2)status.on_timerOut();status.start();status.waitForDone();syncStopped&=!status.isRunning();}
        check(syncStopped,"32 edit-status starts/stops retain early notification and complete without GUI reentry");
        QObject owner;WizDocumentStatusChecker child(&owner);
        check(child.parent()==&owner,"status checker respects an explicit QObject parent outside the worker-owned path");
    }
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
    {
        class PendingLoader final : public WizDocumentLoaderThread {
        public:
            using WizDocumentLoaderThread::WizDocumentLoaderThread;
            void publish(const WIZDOCUMENTDATA& doc,WizEditorMode mode,QObject* owner) {
                setCurrentDoc(doc.strKbGUID,doc.strGUID,mode,owner);
            }
        } loader(manager,nullptr);
        QObject firstPage,secondPage;
        auto* closedPage=new QObject;
        QStringList loaded;QMap<QString,QString> files;QMap<QString,WizEditorMode> modes;
        bool mainThread=true;
        QObject::connect(&loader,&WizDocumentLoaderThread::loaded,&app,
            [&](const QString& kb,const QString& guid,const QString& file,WizEditorMode mode){
                mainThread&=QThread::currentThread()==app.thread()&&kb==notes[0].strKbGUID;
                loaded.append(guid);files.insert(guid,file);modes.insert(guid,mode);
            },Qt::QueuedConnection);
        // Publish before starting the consumer: deterministic single-slot overwrite counterexample.
        loader.publish(notes[0],modeReader,&firstPage);
        loader.publish(notes[1],modeEditor,&secondPage);
        loader.publish(notes[2],modeEditor,&firstPage);
        loader.publish(notes[3],modeReader,nullptr);
        loader.publish(notes[4],modeEditor,nullptr);
        loader.publish(notes[5],modeReader,closedPage);delete closedPage;
        loader.start();
        QElapsedTimer pending;pending.start();
        while(loaded.size()<4&&pending.elapsed()<5000){app.processEvents();QThread::msleep(2);}
        loader.waitForDone();app.processEvents();
        check(loaded==QStringList{notes[2].strGUID,notes[1].strGUID,notes[3].strGUID,notes[4].strGUID},
            "independent pages and direct loads retain FIFO requests; same page keeps its latest pending note");
        check(!loaded.contains(notes[0].strGUID)&&!loaded.contains(notes[5].strGUID),
            "superseded and destroyed-page requests do not load stale content");
        check(modes.value(notes[2].strGUID)==modeEditor&&modes.value(notes[3].strGUID)==modeReader&&mainThread,
            "loaded results retain note mode and reach the application thread");
        bool extracted=loaded.size()==4;
        for(int index:{1,2,3,4}){QFile file(files.value(notes[index].strGUID));
            extracted&=file.open(QIODevice::ReadOnly)&&file.readAll().contains(QString("saved-before-exit-%1").arg(index).toUtf8());}
        check(extracted,"actual queued loader extracts the correct synthetic saved note for each request");
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
