#include "WizAsyncApi.h"
#include "share/WizThreads.h"
#include "share/WizQueuedThreadsSession.h"
#include "async-api-transport-stub.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <QProcess>
#include <QPointer>
#include <atomic>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc==1) {
        int checks=0,failures=0;
        for(const auto* route : {"--initial-state","--deleted-before-start","--deleted-inflight","--live-owner-thread",
                                "--message-status","--empty-token","--reentrant-delete","--queued-reply","--many-replies","--early-return-session"}) {
            QProcess child;child.start(QCoreApplication::applicationFilePath(),{route});
            const bool finished=child.waitForFinished(6000);
            if(!finished){child.kill();child.waitForFinished(2000);} // This exact synthetic child only.
            qInfo().noquote()<<child.readAllStandardError();
            const bool ok=finished&&child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0;
            ++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<route;
        }
        qInfo()<<"Async API lifetime checks:"<<checks<<"failures:"<<failures;
        return failures?1:0;
    }
    if(argc!=2)return 2;
    const QByteArray route(argv[1]);
    auto wait=[&](const std::function<bool()>& predicate) {
        QElapsedTimer timer;timer.start();
        while(!predicate()&&timer.elapsed()<3000){QCoreApplication::processEvents();QThread::msleep(1);}
        return predicate();
    };
    WizQueuedThreadsInit();
    if(route=="--early-return-session") {
        QSemaphore entered;
        std::atomic_bool canceled{false},transportExited{false};
        SyntheticAsyncApiTransport::handler=[&](const QString&) {
            entered.release();
            while(!canceled.load())QThread::msleep(1);
            transportExited=true;return false;
        };
        const auto earlyReturn=[&] {
            WizQueuedThreadsSession queues([&]{canceled=true;});
            WizAsyncApi api;
            api.registerAccount("synthetic-only","not-a-real-password","");
            if(!entered.tryAcquire(1,3000))qFatal("Scoped request did not begin");
            return true; // api dies, cancellation fires, workers join, app remains.
        };
        const bool returned=earlyReturn();
        QCoreApplication::processEvents();
        const bool ok=returned&&canceled&&transportExited&&QCoreApplication::instance()==&app;
        qInfo()<<"Early startup return cancels and joins workers before application destruction"<<ok;
        return ok?0:1;
    }
    if(route=="--initial-state") {
        WizAsyncApi api;
        const bool ok=api.lastErrorCode()==0&&api.lastErrorMessage().isEmpty()&&api.children().isEmpty();
        WizQueuedThreadsShutdown();
        qInfo()<<"Defined initial error state without an unused network manager"<<ok;
        return ok?0:1;
    }
    if(route=="--deleted-before-start") {
        QSemaphore entered,releaseFirst,releaseSecond,drained;
        WizExecuteOnThread(WIZ_THREAD_NETWORK,[&]{entered.release();releaseFirst.acquire();});
        WizExecuteOnThread(WIZ_THREAD_NETWORK,[&]{entered.release();releaseSecond.acquire();});
        if(!entered.tryAcquire(2,3000))qFatal("Synthetic blockers did not start");
        auto* api=new WizAsyncApi;
        api->registerAccount("synthetic-only","not-a-real-password","");
        delete api;
        WizExecuteOnThread(WIZ_THREAD_NETWORK,[&]{drained.release();});
        releaseFirst.release();
        const bool finished=drained.tryAcquire(1,3000);
        releaseSecond.release();
        WizQueuedThreadsShutdown();
        const bool ok=finished&&SyntheticAsyncApiTransport::calls==0;
        qInfo()<<"Deleted-before-start request safely skipped"<<ok;
        return ok?0:1;
    }
    if(route=="--deleted-inflight") {
        QSemaphore entered,release;
        SyntheticAsyncApiTransport::handler=[&](const QString&){entered.release();release.acquire();return false;};
        auto* api=new WizAsyncApi;
        api->registerAccount("synthetic-only","not-a-real-password","");
        if(!entered.tryAcquire(1,3000))qFatal("Synthetic transport did not start");
        delete api;
        release.release();
        WizQueuedThreadsShutdown();
        QCoreApplication::processEvents();
        const bool ok=SyntheticAsyncApiTransport::calls==1;
        qInfo()<<"Owner destroyed during request without late access"<<ok;
        return ok?0:1;
    }
    if(route=="--live-owner-thread") {
        WizAsyncApi api;
        std::atomic_bool finished{false},correctThread{false},correctError{false};
        QObject::connect(&api,&WizAsyncApi::registerAccountFinished,&app,[&](bool succeeded) {
            correctThread=QThread::currentThread()==app.thread();
            correctError=!succeeded&&api.lastErrorCode()==703&&api.lastErrorMessage()=="synthetic transport failure";
            finished=true;
        },Qt::DirectConnection);
        api.registerAccount("synthetic-only","not-a-real-password","");
        const bool delivered=wait([&]{return finished.load();});
        WizQueuedThreadsShutdown();
        const bool ok=delivered&&correctThread&&correctError;
        qInfo()<<"Live reply updates error state and emits on owner thread"<<ok;
        return ok?0:1;
    }
    if(route=="--message-status"||route=="--many-replies") {
        WizAsyncApi api;
        SyntheticAsyncApiTransport::handler=[](const QString& operation){return operation!="register";};
        int replies=0;bool correct=true;
        const QString ids=QStringLiteral("synthetic-1,中文-2");
        const auto verify=[&](bool ok) {correct=correct&&ok&&QThread::currentThread()==app.thread();++replies;};
        QObject::connect(&api,&WizAsyncApi::registerAccountFinished,&app,[&](bool success) {
            verify(!success&&api.lastErrorCode()==703&&api.lastErrorMessage()=="synthetic transport failure");
        },Qt::DirectConnection);
        const auto message=[&](const QString& actual){verify(actual==ids&&api.lastErrorCode()==0&&api.lastErrorMessage().isEmpty());};
        QObject::connect(&api,&WizAsyncApi::uploadMessageReadStatusFinished,&app,message,Qt::DirectConnection);
        QObject::connect(&api,&WizAsyncApi::uploadMessageDeleteStatusFinished,&app,message,Qt::DirectConnection);
        const int rounds=route=="--many-replies"?16:1;
        for(int i=0;i<rounds;++i){api.registerAccount("synthetic-only","not-a-real-password","");api.setMessageReadStatus(ids,true);api.setMessageDeleteStatus(ids,true);}
        const bool finished=wait([&]{return replies==rounds*3;});
        WizQueuedThreadsShutdown();
        const bool ok=finished&&correct&&SyntheticAsyncApiTransport::calls==rounds*3;
        qInfo()<<"Register/read/delete replies preserve IDs, per-reply error state and owner affinity"<<ok;
        return ok?0:1;
    }
    if(route=="--empty-token") {
        SyntheticAsyncApiTransport::emptyToken=true;
        QSemaphore entered,release,drained;
        WizExecuteOnThread(WIZ_THREAD_NETWORK,[&]{entered.release();release.acquire();});
        if(!entered.tryAcquire(1,3000))qFatal("Empty-token blocker did not start");
        WizAsyncApi api;int notifications=0;
        QObject::connect(&api,&WizAsyncApi::uploadMessageReadStatusFinished,&app,[&]{++notifications;});
        QObject::connect(&api,&WizAsyncApi::uploadMessageDeleteStatusFinished,&app,[&]{++notifications;});
        api.setMessageReadStatus("synthetic",true);api.setMessageDeleteStatus("synthetic",true);
        WizExecuteOnThread(WIZ_THREAD_NETWORK,[&]{drained.release();});
        const bool finished=drained.tryAcquire(1,3000);
        release.release();WizQueuedThreadsShutdown();QCoreApplication::processEvents();
        const bool ok=finished&&SyntheticAsyncApiTransport::calls==0&&notifications==0&&api.lastErrorCode()==0;
        qInfo()<<"Missing token remains quiet and never reaches the transport"<<ok;
        return ok?0:1;
    }
    if(route=="--reentrant-delete") {
        auto* api=new WizAsyncApi;QPointer<WizAsyncApi> guard(api);bool correctThread=false;
        QObject::connect(api,&WizAsyncApi::registerAccountFinished,&app,[&] {
            correctThread=QThread::currentThread()==app.thread();delete api;api=nullptr;
        },Qt::DirectConnection);
        api->registerAccount("synthetic-only","not-a-real-password","");
        const bool finished=wait([&]{return guard.isNull();});
        WizQueuedThreadsShutdown();
        qInfo()<<"Completion handler may delete its owner synchronously"<<(finished&&correctThread);
        return finished&&correctThread?0:1;
    }
    if(route=="--queued-reply") {
        struct Filter : QObject {
            QObject* target=nullptr;
            std::function<void()> action;
            bool eventFilter(QObject* watched,QEvent* event) override {
                if(watched==target&&event->type()==QEvent::MetaCall&&action){auto run=std::move(action);run();}
                return false;
            }
        } filter;
        filter.target=&app;
        auto* api=new WizAsyncApi;QPointer<WizAsyncApi> guard(api);int notifications=0;
        QObject::connect(api,&WizAsyncApi::registerAccountFinished,&app,[&]{++notifications;});
        filter.action=[&]{delete api;api=nullptr;};
        app.installEventFilter(&filter);
        api->registerAccount("synthetic-only","not-a-real-password","");
        const bool finished=wait([&]{return guard.isNull();});
        WizQueuedThreadsShutdown();QCoreApplication::processEvents();
        const bool ok=finished&&notifications==0&&SyntheticAsyncApiTransport::calls==1;
        qInfo()<<"Already-posted reply is invalidated before its delivery event executes"<<ok;
        return ok?0:1;
    }
    WizQueuedThreadsShutdown();
    return 2;
}
