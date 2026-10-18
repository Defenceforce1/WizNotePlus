#include "share/WizEventLoop.h"
#include "share/WizNetworkSecurity.h"
#include "share/WizDocumentAsync.h"
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
#include <functional>
#include <ctime>

// Override only disk work, not the actual production waitForDone methods.
// A GUI timer releases a synthetic pending operation; bounded worker rescue
// prevents an incorrect event-pump change from hanging the test indefinitely.
template<class Base>
class PendingIoWaitProbe final : public Base {
public:
    using Base::Base;
    QSemaphore entered, release;
    std::atomic_bool completed{false};
protected:
    void run() override {
        entered.release();
        completed = release.tryAcquire(1, 2000);
    }
};

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
        using namespace WizQuitSyncPolicy;
        Wait neverStarted;
        check(neverStarted.observe(2999,false,0,false)==Decision::Wait
            &&neverStarted.observe(3000,false,0,false)==Decision::StartTimedOut,
            "quit allows three seconds to connect/start, not a three-second upload limit");
        Wait progressing;
        bool continued=progressing.observe(0,false,0,false)==Decision::Wait
            &&progressing.observe(2900,true,1,false)==Decision::Wait;
        for(int seconds=3;seconds<=120;++seconds)
            continued&=progressing.observe(seconds*1000,true,quint64(seconds),false)==Decision::Wait;
        check(continued&&progressing.observe(121000,true,120,true)==Decision::Finished,
            "a sync starting within three seconds may keep progressing for two minutes and finish normally");
        Wait stalled;
        check(stalled.observe(100,true,1,false)==Decision::Wait
            &&stalled.observe(3099,true,1,false)==Decision::Wait
            &&stalled.observe(3100,true,1,false)==Decision::Stalled,
            "started sync stops at three seconds without progress, not before");
        Wait refreshed;
        check(refreshed.observe(100,true,1,false)==Decision::Wait
            &&refreshed.observe(2900,true,2,false)==Decision::Wait
            &&refreshed.observe(5899,true,2,false)==Decision::Wait
            &&refreshed.observe(5900,true,2,false)==Decision::Stalled,
            "new transfer progress resets inactivity timeout without imposing a total deadline");
        check(noProgressTimeoutMs==3000,
            "quit no-progress limit is three seconds independently of the local-save confirmation");
        check(stalled.observe(3101,true,2,false)==Decision::Stalled
            &&stalled.observe(3200,true,3,true)==Decision::Stalled
            &&neverStarted.observe(3001,true,1,false)==Decision::StartTimedOut,
            "late network activity cannot revive a timed-out quit attempt or trigger a retry");
        Wait failure;
        check(failure.observe(200,true,1,true)==Decision::Finished,
            "completed failed attempts also end waiting without automatic retry");
        int outer=0,inner=0;
        {
            WizNetworkSecurity::ActivityScope scope([&]{++outer;});
            WizNetworkSecurity::noteActivity();
            {WizNetworkSecurity::ActivityScope nested([&]{++inner;});WizNetworkSecurity::noteActivity();}
            WizNetworkSecurity::noteActivity();
        }
        WizNetworkSecurity::noteActivity();
        check(outer==2&&inner==1,"network activity observation is scoped and restored, with no persistent callback");
        {
            WIZUSERINFOBASE user;user.strKbServer="https://example.invalid/no-network";
            WizKMDatabaseServer resources(user);
            int ownedActivity=0;
            const WizNetworkSecurity::ActivityScope scope([&]{++ownedActivity;});
            auto publisher=std::unique_ptr<QThread>(QThread::create([&]{
                WizNetworkSecurity::noteActivity(); // Unrelated worker has no observer.
                for(qint64 bytes:{qint64(0),qint64(64),qint64(64),qint64(128)})
                    QMetaObject::invokeMethod(&resources,"onDocumentObjectDownloadProgress",Qt::QueuedConnection,
                        Q_ARG(QUrl,QUrl("https://example.invalid/resource")),Q_ARG(qint64,bytes),Q_ARG(qint64,qint64(256)));
            }));
            publisher->start();const bool joined=publisher->wait(2000);
            if(!joined){publisher.release();return 1;}
            const bool isolated=ownedActivity==0;app.processEvents();
            check(isolated&&ownedActivity==2,
                "owned parallel resource progress extends its sync wait; other workers and duplicate/zero-byte heartbeats do not");
        }
    }
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
    const auto measureLocalWait = [&](auto& worker, const char* description) {
        bool releasedByGui = false;
        worker.start();
        const bool entered = worker.entered.tryAcquire(1, 1000);
        QTimer release;
        release.setSingleShot(true);
        QObject::connect(&release, &QTimer::timeout, &release, [&] {
            releasedByGui = true; worker.release.release();
        });
        release.start(250);
        QElapsedTimer elapsed; elapsed.start();
        double cpuMs = -1.0;
#if defined(CLOCK_THREAD_CPUTIME_ID)
        timespec before{}, after{};
        const bool cpuAvailable = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &before) == 0;
#endif
        worker.waitForDone();
#if defined(CLOCK_THREAD_CPUTIME_ID)
        const bool cpuFinished = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &after) == 0;
        cpuMs = cpuAvailable && cpuFinished
            ? (after.tv_sec - before.tv_sec) * 1000.0 + (after.tv_nsec - before.tv_nsec) / 1000000.0 : -1.0;
#endif
        release.stop();
        qInfo() << "Synthetic pending I/O wait" << description << "wall ms" << elapsed.elapsed()
                << "GUI CPU ms" << cpuMs;
        check(entered && releasedByGui && worker.completed && !worker.isRunning(), description);
    };
    PendingIoWaitProbe<WizDocumentLoaderThread> pendingLoader(manager, nullptr);
    measureLocalWait(pendingLoader, "actual loader wait keeps GUI events alive and joins pending I/O");
    PendingIoWaitProbe<WizDocumentSaverThread> pendingSaver(manager, nullptr);
    measureLocalWait(pendingSaver, "actual saver wait keeps GUI events alive and joins pending I/O");
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
    {
        class SequencedLoader final : public WizDocumentLoaderThread {
        public:
            using WizDocumentLoaderThread::WizDocumentLoaderThread;
            void publish(const WIZDOCUMENTDATA& doc,WizEditorMode mode,QObject* owner,quint64 sequence) {
                setCurrentDoc(doc.strKbGUID,doc.strGUID,mode,owner,sequence);
            }
        } loader(manager,nullptr);
        QObject firstPage,secondPage;
        WizDocumentLoadSequence firstSequence,secondSequence;
        QSemaphore produced;
        QSemaphore continuePosting;
        std::atomic_bool pausePosting{false}, postingGateTimedOut{false};
        int delivered=0,firstAccepted=0,secondAccepted=0,oldRejected=0,deadRejected=0;
        bool correctModes=true,mainThread=true;
        QObject::connect(&loader,&WizDocumentLoaderThread::loaded,&app,
            [&](const QString&,const QString&,const QString&,WizEditorMode){
                const bool paused=pausePosting.load();
                produced.release();
                // This DIRECT observer runs before the queued connection below.
                // Bound the fixture gate so a failed assertion cannot deadlock cleanup.
                if(paused&&!continuePosting.tryAcquire(1,5000)) postingGateTimedOut=true;
            },Qt::DirectConnection);
        const auto awaitDelivery=[&](const std::function<bool()>& complete){
            QElapsedTimer pending;pending.start();
            while(!complete()&&pending.elapsed()<5000){app.processEvents();QThread::msleep(1);}
            return complete();
        };
        QObject::connect(&loader,&WizDocumentLoaderThread::loaded,&app,
            [&](const QString&,const QString&,const QString&,WizEditorMode mode,QPointer<QObject> requester,quint64 sequence){
                ++delivered;mainThread&=QThread::currentThread()==app.thread();
                if(firstSequence.accepts(&firstPage,requester,sequence)){++firstAccepted;correctModes&=mode==modeEditor;}
                else if(requester==&firstPage)++oldRejected;
                if(secondSequence.accepts(&secondPage,requester,sequence)){++secondAccepted;correctModes&=mode==modeReader;}
                if(!requester)++deadRejected;
            },Qt::QueuedConnection);
        // Deliberately hold the GUI queue until an older extraction has
        // completed, then request the SAME note again and in another view.
        loader.publish(notes[0],modeReader,&firstPage,firstSequence.next());loader.start();
        const bool firstReady=produced.tryAcquire(1,5000);
        loader.publish(notes[0],modeEditor,&firstPage,firstSequence.next());
        loader.publish(notes[0],modeReader,&secondPage,secondSequence.next());
        const bool latestReady=produced.tryAcquire(2,5000);
        const bool deliveredAll=awaitDelivery([&]{return delivered==3;});
        check(firstReady&&latestReady&&deliveredAll&&mainThread,"real loader queues same-note results with GUI-thread delivery");
        check(firstAccepted==1&&secondAccepted==1&&correctModes,
            "two views of one note accept only their own current result and requested editor mode");
        check(oldRejected==1,"already completed but delayed old same-note result is rejected after a newer request");
        int failed=0;bool failureAddressed=false;
        QSemaphore failedProduced;
        QObject::connect(&loader,&WizDocumentLoaderThread::loadFailed,&app,[&]{failedProduced.release();},Qt::DirectConnection);
        WIZDOCUMENTDATA missing=notes[0];missing.strGUID="missing-synthetic-load";
        QObject::connect(&loader,&WizDocumentLoaderThread::loadFailed,&app,
            [&](const QString& kb,const QString& guid,const QString&,const QString&,QPointer<QObject> requester,quint64 sequence){
                ++failed;failureAddressed=kb==missing.strKbGUID&&guid==missing.strGUID
                    &&firstSequence.accepts(&firstPage,requester,sequence)&&!secondSequence.accepts(&secondPage,requester,sequence);
            },Qt::QueuedConnection);
        loader.publish(missing,modeReader,&firstPage,firstSequence.next());
        const bool missingReady=failedProduced.tryAcquire(1,5000);
        const bool failedDelivered=awaitDelivery([&]{return failed==1;});
        check(missingReady&&failedDelivered&&failureAddressed,"missing-note load reports one failure only to its requesting view and generation");
        auto closing=std::make_unique<QObject>();
        const QPointer<QObject> closed=closing.get();
        pausePosting=true;
        loader.publish(notes[0],modeReader,closing.get(),1);
        const bool closedReady=produced.tryAcquire(1,5000);closing.reset();app.processEvents();
        check(closedReady&&closed.isNull()&&deadRejected==0&&!postingGateTimedOut,
            "direct extraction notification is not proof that its later queued GUI delivery has been posted");
        continuePosting.release();
        const bool closedDelivered=awaitDelivery([&]{return deadRejected==1;});
        check(closedReady&&closed.isNull()&&closedDelivered&&!postingGateTimedOut,
            "destroying a view after extraction clears the queued requester instead of targeting a replacement view");
        loader.waitForDone();
    }
    WizDocumentSaverThread failingSaver(manager, nullptr);
    WIZDOCUMENTDATA missing = notes[0]; missing.strGUID = "missing-synthetic-note";
    int missingCompletions=0;bool missingResult=false;
    QObject::connect(&failingSaver,&WizDocumentSaverThread::saved,&app,
        [&](const QString& kb,const QString& guid,bool ok,QPointer<QObject> requester){
            ++missingCompletions;missingResult=kb==missing.strKbGUID&&guid==missing.strGUID&&!ok&&requester.isNull();
        },Qt::QueuedConnection);
    failingSaver.save(missing, "<html><body>cannot save</body></html>", "", 0);
    failingSaver.waitForDone();
    app.processEvents();
    check(missingCompletions==1&&missingResult,"missing-note save completes exactly once with failure and original note identity");
    check(failingSaver.hasTransferError(), "failed disk/database save is not treated as success");
    failingSaver.clearTransferError();
    check(failingSaver.hasTransferError(), "clearing a transfer status cannot conceal an unresolved failed note");
    {
        class ObservedSaver final : public WizDocumentSaverThread {
        public:
            using WizDocumentSaverThread::WizDocumentSaverThread;
            QObject* currentSender() const { return sender(); }
        } ownerSaver(manager,nullptr);
        auto requester=std::make_unique<QTimer>();
        bool senderObserved=false,receiptNull=false;int receipts=0;
        QObject::connect(requester.get(),&QTimer::timeout,&ownerSaver,[&]{
            senderObserved=ownerSaver.currentSender()==requester.get();
            ownerSaver.save(notes[0],"<html><body>save-survives-page-close</body></html>","",0);
        });
        QObject::connect(&ownerSaver,&WizDocumentSaverThread::saved,&app,
            [&](const QString&,const QString&,bool ok,QPointer<QObject> owner){
                ++receipts;receiptNull=ok&&owner.isNull();
            },Qt::QueuedConnection);
        QMetaObject::invokeMethod(requester.get(),"timeout",Qt::DirectConnection);
        requester.reset();ownerSaver.waitForDone();app.processEvents();
        check(senderObserved&&receipts==1&&receiptNull,"queued save completion cannot retain or address-reuse a destroyed requester");
        QString path;bool persisted=manager.db().documentToTempHtmlFile(notes[0],path);QFile file(path);
        check(persisted&&file.open(QIODevice::ReadOnly)&&file.readAll().contains("save-survives-page-close"),
            "closing a requester still lets its already queued local save finish on disk");
    }

    WizKMSyncThread idle(manager.db(), true);
    idle.stopSync(); idle.addQuickSyncKb("synthetic-shutdown-kb");
    idle.startSyncAll();
    check(idle.isIdle(), "stopped sync worker rejects new queued work before startup");
    idle.start();
    check(idle.wait(1000), "a stopped worker cannot restart synchronization");

    check(WizKMSyncThread::quitSyncTimeoutMs == 3000, "final sync start budget is three seconds, independent of local-save confirmation and active uploads");
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
        while ((attempts < target || !offline.isIdle()) && attempt.elapsed() < 2500) {
            app.processEvents(); QThread::msleep(5);
        }
        check(attempts == target && attempt.elapsed() < 2500,
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
                const auto request=socket->readAll(); ++requests;
                if(request.startsWith("GET /silent ")) return;
                if(request.startsWith("GET /stall-after-progress ")) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\n");
                    // Let Qt's progress-notification coalescing interval pass
                    // before a real body byte arrives, then deliberately stall.
                    QTimer::singleShot(250,socket,[socket]{socket->write("x");});
                    return;
                }
                if(request.startsWith("GET /slow ")) {
                    socket->write("HTTP/1.1 200 OK\r\nContent-Length: 8\r\nConnection: close\r\n\r\n");
                    auto* chunks=new QTimer(socket);
                    auto remaining=std::make_shared<int>(8);
                    QObject::connect(chunks,&QTimer::timeout,socket,[socket,chunks,remaining]{
                        socket->write("x");
                        if(!--*remaining){chunks->stop();socket->disconnectFromHost();}
                    });
                    chunks->start(500);return;
                }
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
    {
        QNetworkAccessManager net;
        QUrl slow=url;slow.setPath("/slow");
        WizAutoTimeOutEventLoop loop(net.get(QNetworkRequest(slow)));
        bool started=false;quint64 activity=0;
        const WizNetworkSecurity::ActivityScope observe([&]{started=true;++activity;});
        WizQuitSyncPolicy::Wait policy;QElapsedTimer elapsed;elapsed.start();
        QTimer monitor;
        QObject::connect(&monitor,&QTimer::timeout,&app,[&]{
            const auto decision=policy.observe(elapsed.elapsed(),started,activity,loop.isFinished());
            if(decision!=WizQuitSyncPolicy::Decision::Wait&&decision!=WizQuitSyncPolicy::Decision::Finished)
                loop.networkReply()->abort();
        });
        monitor.start(25);loop.exec();monitor.stop();
        check(loop.error()==QNetworkReply::NoError&&loop.result()=="xxxxxxxx"&&elapsed.elapsed()>3000&&activity>1,
            "actual slow loopback download keeps progressing past three seconds and saves the complete response");
    }
    for (const QString& path : {QStringLiteral("/silent"), QStringLiteral("/stall-after-progress")}) {
        QNetworkAccessManager net;
        QUrl pending=url;pending.setPath(path);
        const int beforeRequests=requests;
        WizAutoTimeOutEventLoop loop(net.get(QNetworkRequest(pending)));
        bool started=false;quint64 activity=0;
        const WizNetworkSecurity::ActivityScope observe([&]{started=true;++activity;});
        WizQuitSyncPolicy::Wait policy;QElapsedTimer elapsed;elapsed.start();
        WizQuitSyncPolicy::Decision cutoff=WizQuitSyncPolicy::Decision::Wait;
        QTimer monitor;
        QObject::connect(&monitor,&QTimer::timeout,&app,[&]{
            const auto decision=policy.observe(elapsed.elapsed(),started,activity,loop.isFinished());
            if(decision!=WizQuitSyncPolicy::Decision::Wait&&decision!=WizQuitSyncPolicy::Decision::Finished) {
                cutoff=decision;loop.networkReply()->abort();
            }
        });
        monitor.start(25);loop.exec();monitor.stop();
        const auto expected=path==QStringLiteral("/silent")?WizQuitSyncPolicy::Decision::StartTimedOut
            :WizQuitSyncPolicy::Decision::Stalled;
        qInfo()<<"Synthetic quit cutoff"<<path<<"decision"<<int(cutoff)<<"elapsed ms"<<elapsed.elapsed()
            <<"activity count"<<activity<<"requests"<<(requests-beforeRequests);
        check(cutoff==expected&&loop.error()==QNetworkReply::OperationCanceledError
            &&elapsed.elapsed()>=3000&&elapsed.elapsed()<4500,
            path==QStringLiteral("/silent")
                ?"actual unresponsive loopback request stops at the three-second start cutoff"
                :"actual loopback transfer that stops progressing is canceled after three seconds of inactivity");
        app.processEvents();
        check(requests-beforeRequests==1&&policy.observe(elapsed.elapsed(),true,activity+1,false)==expected,
            "quit cutoff sends no retry and late activity cannot restart the attempt");
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
        // Actual legacy-only-finished negative beside the cancellable loop:
        // both replies stall against this same loopback server, no cloud calls.
        QNetworkReply* legacyReply = net.get(QNetworkRequest(url));
        QEventLoop legacyLoop;
        QObject::connect(legacyReply, &QNetworkReply::finished, &legacyLoop, &QEventLoop::quit);
        bool rescuedLegacy = false;
        QTimer::singleShot(650, &legacyLoop, [&] {
            rescuedLegacy = true;
            legacyReply->abort();
            legacyLoop.quit();
        });
        WizAutoTimeOutEventLoop loop(net.get(QNetworkRequest(url)));
        QTimer::singleShot(150, &app, &WizNetworkSecurity::beginShutdown);
        elapsed.restart(); loop.exec();
        check(loop.error() == QNetworkReply::OperationCanceledError && elapsed.elapsed() < 3000,
              "process shutdown cancels stalled network waits without waiting for the 60-second timeout");
        check(!legacyReply->isFinished(), "negative control: finished-only legacy reply does not observe process cancellation");
        if (!legacyReply->isFinished()) legacyLoop.exec();
        check(rescuedLegacy, "negative control requires explicit rescue rather than being misreported as canceled");
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
