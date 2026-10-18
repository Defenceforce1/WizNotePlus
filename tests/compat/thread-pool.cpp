#include "share/WizThreads.h"
#include <QCoreApplication>
#include <QDebug>
#include <QSemaphore>
#include <QProcess>
#include <atomic>
#include <chrono>
#include <memory>
#include <functional>
#include <thread>
#include <vector>

namespace {
QSemaphore beforeWait, allowWait;
std::atomic<bool> barrierArmed{false};
struct Task final : IWizRunable {
    std::atomic<int>& runs;
    std::atomic<int>& destructions;
    std::atomic<bool> destroyed{false};
    std::atomic<IWizThreadPool*> executingPool{nullptr};
    QString id;
    std::function<void()> runAction,destroyAction;
    Task(std::atomic<int>& r,std::atomic<int>& d,QString name={}) : runs(r),destructions(d),id(name) {}
    void run(int,IWizThreadPool* pool,IWizRunableEvents*) override { executingPool=pool;++runs;if(runAction)runAction(); }
    QString getTaskID() override { return id; }
    void destroy() override {
        // Keep the tiny synthetic object alive for inspection. The real
        // resource counter changes at destroy(); no test task can be reused.
        if(destroyed.exchange(true))qFatal("Synthetic task destroyed twice");
        ++destructions;
        if(destroyAction)destroyAction();
    }
};
}
void wizThreadPoolBeforeWaitForTest() {
    if(barrierArmed.exchange(false)) {
        beforeWait.release();
        if(!allowWait.tryAcquire(1,3000))qFatal("Scheduling barrier timed out");
    }
}
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    if(argc==2&&QByteArray(argv[1])=="--queued-shutdown-probe") {
        std::atomic<int> runs{0},destroyed{0};QSemaphore entered,release;
        Task active(runs,destroyed),pendingMain(runs,destroyed),lateMain(runs,destroyed);
        active.runAction=[&] {
            entered.release();if(!release.tryAcquire(1,3000))qFatal("Queued worker was not released");
            qInfo()<<"Synthetic worker submitting main action during queued shutdown";
            WizQueuedThreadAddAction(WIZ_THREAD_MAIN,&lateMain);
        };
        WizQueuedThreadsInit();
        WizQueuedThreadAddAction(WIZ_THREAD_MAIN,&pendingMain);
        WizQueuedThreadAddAction(WIZ_THREAD_NETWORK,&active);
        if(!entered.tryAcquire(1,3000))return 2;
        std::thread releasing([&]{std::this_thread::sleep_for(std::chrono::milliseconds(100));release.release();});
        qInfo()<<"Synthetic queued shutdown begins";
        WizQueuedThreadsShutdown();
        releasing.join();
        const bool correct=runs==1&&destroyed==3&&pendingMain.destroyed&&lateMain.destroyed;
        qInfo()<<"Synthetic queued shutdown completed"<<correct;
        return correct?0:1;
    }
    if(argc==2&&QByteArray(argv[1])=="--queued-submit-lease-probe") {
        std::atomic<int> runs{0},destroyed{0};
        QSemaphore entered,releaseWorker,disposing,releaseDisposal;
        Task active(runs,destroyed),old(runs,destroyed,"replace"),replacement(runs,destroyed,"replace"),late(runs,destroyed);
        active.runAction=[&]{entered.release();if(!releaseWorker.tryAcquire(1,3000))qFatal("Lease worker timed out");};
        old.destroyAction=[&]{
            disposing.release();
            if(!releaseDisposal.tryAcquire(1,3000))qFatal("Lease disposal timed out");
            WizQueuedThreadAddAction(WIZ_THREAD_MAIN,&late);
        };
        WizQueuedThreadsInit();
        WizQueuedThreadAddAction(WIZ_THREAD_SEARCH,&active);
        if(!entered.tryAcquire(1,3000))return 2;
        WizQueuedThreadAddAction(WIZ_THREAD_SEARCH,&old);
        std::thread producer([&]{WizQueuedThreadAddAction(WIZ_THREAD_SEARCH,&replacement);});
        if(!disposing.tryAcquire(1,3000))qFatal("Replacement did not enter cleanup");
        std::thread releasing([&]{
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            releaseDisposal.release();
            // Shutdown visits several pools in order. Hold the active worker
            // until THIS pool has stopped accepting work, not an assumed delay.
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(3);
            while(!active.executingPool.load()->isShuttingDown()) {
                if(std::chrono::steady_clock::now()>deadline)qFatal("Lease pool did not enter shutdown");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            releaseWorker.release();
        });
        WizQueuedThreadsShutdown();
        producer.join();releasing.join();
        const bool correct=runs==1&&destroyed==4;
        qInfo()<<"Submission lease and reentrant cleanup completed"<<correct;
        return correct?0:1;
    }
    if(argc==2&&QByteArray(argv[1])=="--queued-main-reinit-probe") {
        std::atomic<int> runs{0},destroyed{0};
        Task first(runs,destroyed),canceled(runs,destroyed),late(runs,destroyed),fresh(runs,destroyed);
        first.runAction=[] { WizQueuedThreadsShutdown(); };
        WizQueuedThreadsInit();
        WizQueuedThreadAddAction(WIZ_THREAD_MAIN,&first);
        WizQueuedThreadAddAction(WIZ_THREAD_MAIN,&canceled);
        QCoreApplication::processEvents();
        WizQueuedThreadAddAction(WIZ_THREAD_MAIN,&late);
        const bool canceledCorrectly=runs==1&&destroyed==3;
        WizQueuedThreadsInit();
        WizQueuedThreadAddAction(WIZ_THREAD_MAIN,&fresh);
        QCoreApplication::processEvents();
        WizQueuedThreadsShutdown();
        const bool correct=canceledCorrectly&&runs==2&&destroyed==4;
        qInfo()<<"Main batch cancellation and explicit reinitialization completed"<<correct;
        return correct?0:1;
    }
    int failures=0,checks=0;
    auto check=[&](bool ok,const char* label) { ++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label; };
    // No workers: inspect ownership independently from scheduling.
    {
        std::atomic<int> runs{0},destroyed{0};
        std::vector<std::unique_ptr<Task>> tasks;
        IWizThreadPool* pool=WizCreateThreadPool(0);
        for(int i=0;i<100;++i) {
            tasks.emplace_back(new Task(runs,destroyed,"same-synthetic-id"));
            pool->addTask(tasks.back().get());
        }
        int waiting=0;pool->getTaskCount(nullptr,&waiting);
        check(waiting==1&&runs==0,"same-ID replacement retains only the latest pending task without executing discarded work");
        check(destroyed==99,"superseded pending tasks release their owned resources");
        pool->clearTasks();
        check(destroyed==100,"clearTasks releases the final pending task");
        pool->shutdown(5);
        for(auto& task:tasks)if(!task->destroyed)task->destroy(); // Negative-control cleanup only.
        check(destroyed==100,"negative control leaves no synthetic resource behind");
    }
    {
        std::atomic<int> runs{0},destroyed{0};
        Task task(runs,destroyed);
        IWizThreadPool* pool=WizCreateThreadPool(0);
        pool->addTask(&task);
        pool->shutdown(5);
        check(destroyed==1,"shutdown releases unstarted tasks without running them");
        if(!task.destroyed)task.destroy();
    }
    // Force one real enqueue in the empty-check/wait gap. An old implementation
    // is rescued after observing the missed wake, so the test never hangs.
    {
        std::atomic<int> runs{0},destroyed{0};
        Task first(runs,destroyed),rescue(runs,destroyed);
        IWizThreadPool* pool=WizCreateThreadPool(0);
        QSemaphore completed;
        barrierArmed=true;
        std::thread consumer([&] {
            if(auto* task=pool->peekOne()){task->run(0,pool,nullptr);task->destroy();}
            completed.release();
        });
        const bool paused=beforeWait.tryAcquire(1,3000);
        check(paused,"consumer reaches the explicit empty-check/wait boundary");
        pool->addTask(&first);
        allowWait.release();
        const bool noRescue=completed.tryAcquire(1,300);
        check(noRescue,"one enqueue cannot lose its wake before the consumer starts waiting");
        if(!noRescue){pool->addTask(&rescue);if(!completed.tryAcquire(1,3000))qFatal("Old wait path did not accept rescue");}
        consumer.join();
        check(first.destroyed&&runs==1,"the original queued task executes exactly once");
        pool->clearTasks();pool->shutdown(5);
        if(!rescue.destroyed)rescue.destroy();
    }
    {
        std::atomic<int> runs{0},destroyed{0};
        Task first(runs,destroyed,"replace"),second(runs,destroyed,"replace"),reentrant(runs,destroyed);
        IWizThreadPool* pool=WizCreateThreadPool(0);
        first.destroyAction=[&]{pool->addTask(&reentrant);};
        pool->addTask(&first);pool->addTask(&second);
        int waiting=0;pool->getTaskCount(nullptr,&waiting);
        check(destroyed==1&&waiting==2,"discard cleanup may reenter enqueue without deadlocking or executing canceled work");
        pool->clearTasks();
        check(destroyed==3&&runs==0,"reentrant and replacement tasks are each released once");
        pool->shutdown(5);
    }
    {
        std::atomic<int> runs{0},destroyed{0};Task task(runs,destroyed,"same-object");
        IWizThreadPool* pool=WizCreateThreadPool(0);pool->addTask(&task);pool->addTask(&task);
        int waiting=0;pool->getTaskCount(nullptr,&waiting);
        check(waiting==1&&destroyed==0,"re-enqueueing an already pending pointer cannot destroy that same live task");
        pool->clearTasks();pool->shutdown(5);
        check(destroyed==1,"duplicate pending pointer is released exactly once");
    }
    const auto waitFor=[](const std::function<bool()>& predicate) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(4);
        while(!predicate()&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return predicate();
    };
    {
        std::atomic<int> runs{0},destroyed{0};
        struct Events final : IWizThreadPoolEvents {
            std::atomic<int> before{0},after{0};
            void beforeTask(IWizRunable*) override { ++before; }
            void afterTask(IWizRunable*) override { ++after; }
        } events;
        IWizThreadPool* pool=WizCreateThreadPool(4);pool->setEventsListener(&events);
        std::vector<std::unique_ptr<Task>> tasks;
        for(int i=0;i<2000;++i)tasks.emplace_back(new Task(runs,destroyed));
        std::vector<std::thread> producers;
        for(int producer=0;producer<4;++producer)producers.emplace_back([&,producer] {
            for(int i=producer;i<2000;i+=4)pool->addTask(tasks[i].get());
        });
        for(auto& producer:producers)producer.join();
        check(waitFor([&]{return destroyed==2000;}),"four producers and four workers complete 2000 independent tasks without another wake");
        pool->shutdown(5);
        check(runs==2000&&destroyed==2000,"parallel tasks execute and release exactly once");
        check(events.before==2000&&events.after==2000,"listener callbacks remain paired for all completed tasks");
    }
    {
        std::atomic<int> runs{0},destroyed{0};QSemaphore entered,finish,stopped;
        Task active(runs,destroyed),pending(runs,destroyed),late(runs,destroyed);
        active.runAction=[&]{entered.release();if(!finish.tryAcquire(1,4000))qFatal("Active synthetic task timed out");};
        IWizThreadPool* pool=WizCreateThreadPool(1);pool->addTask(&active);
        if(!entered.tryAcquire(1,3000))qFatal("Synthetic worker did not start");
        pool->addTask(&pending);
        std::thread stopping([&]{pool->shutdown(5);stopped.release();});
        if(!waitFor([&]{return pool->isShuttingDown();}))qFatal("Shutdown did not begin");
        pool->addTask(&late);
        check(late.destroyed&&runs==1,"work submitted while shutdown waits is rejected and released without running");
        check(!stopped.tryAcquire(1,30),"shutdown does not return before the running task finishes");
        finish.release();stopping.join();
        check(active.destroyed&&pending.destroyed&&destroyed==3&&runs==1,"shutdown waits for active work and releases unstarted work exactly once");
    }
    QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);
    {
        QProcess child;
        child.start(QCoreApplication::applicationFilePath(),{"--queued-shutdown-probe"});
        const bool finished=child.waitForFinished(4000);
        if(!finished){child.kill();child.waitForFinished(2000);} // This exact synthetic child only.
        qInfo().noquote()<<child.readAllStandardError();
        check(finished&&child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0,
            "global queued shutdown releases pending main work and cannot lock out a finishing worker submission");
    }
    for(const auto& probe : {"--queued-submit-lease-probe","--queued-main-reinit-probe"}) {
        QProcess child;child.start(QCoreApplication::applicationFilePath(),{probe});
        const bool finished=child.waitForFinished(4000);
        if(!finished){child.kill();child.waitForFinished(2000);} // This exact synthetic child only.
        qInfo().noquote()<<child.readAllStandardError();
        check(finished&&child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0,probe);
    }
    qInfo()<<"Thread pool checks:"<<checks<<"failures:"<<failures;
    return failures?1:0;
}
