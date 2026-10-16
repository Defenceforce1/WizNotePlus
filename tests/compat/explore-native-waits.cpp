// Standalone Qt Core protocol probe. No DB, account, note, GUI, network or login.
// Tests the EXACT extracted WizWaitEvent class plus the source ordering around it;
// it does not pretend to instantiate the full document saver/loader.
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QMutex>
#include <QtCore/QSemaphore>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QWaitCondition>
#include <QtCore/QDebug>
#include <atomic>
#include <memory>
#ifdef WIZNOTE_STICKY_WAIT_CANDIDATE
#include "WizWaitEvent-candidate-extracted.h"
#else
#include "WizWaitEvent-extracted.h"
#endif

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("WizNoteIsolatedWaitProbe");
    int failures = 0;
    const auto check = [&](bool ok, const char* message) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << message;
        failures += !ok;
    };
    qInfo() << "Actual Qt runtime" << qVersion();
    for (bool stopInsteadOfPublish : {false, true}) {
        QMutex queueMutex;
        WizWaitEvent event;
        QSemaphore checkedEmpty, allowWait;
        int queued = 0;
        bool stop = false;
        std::atomic_bool completed{false};
        std::unique_ptr<QThread> consumer(QThread::create([&] {
            bool empty;
            { QMutexLocker locker(&queueMutex); empty = queued == 0; }
            if (empty) {
                checkedEmpty.release();
                allowWait.acquire(); // force publication into the source's check/wait gap
                event.wait();
            }
            { QMutexLocker locker(&queueMutex); completed = queued > 0 || stop; }
        }));
        consumer->start();
        if (!checkedEmpty.tryAcquire(1, 2000)) return 2;
        { QMutexLocker locker(&queueMutex); if (stopInsteadOfPublish) stop = true; else ++queued; }
        event.wakeAll(); // nobody is waiting yet; exactly the source protocol gap
        allowWait.release();
#ifdef WIZNOTE_STICKY_WAIT_CANDIDATE
        const bool wokeWithoutRescue = consumer->wait(2000);
        check(wokeWithoutRescue && completed.load(), stopInsteadOfPublish
            ? "candidate remembers stop notification before wait registration"
            : "candidate remembers queued-task notification before wait registration");
#else
        const bool wokeWithoutRescue = consumer->wait(100);
        check(!wokeWithoutRescue && !completed.load(), stopInsteadOfPublish
            ? "source stop notification is lost before wait registration"
            : "source queued-task notification is lost before wait registration");
#endif
        QElapsedTimer rescue; rescue.start();
        while (!consumer->wait(10) && rescue.elapsed() < 2000) event.wakeAll();
        check(consumer->isFinished() && completed.load(), "isolated probe rescues and joins its own worker cleanly");
        if (!consumer->isFinished()) { consumer.release(); return 3; }
    }
    {
        QMutex mutex;
        QWaitCondition condition;
        QSemaphore checkedEmpty, allowWait;
        int queued = 0;
        std::atomic_bool completed{false};
        std::unique_ptr<QThread> consumer(QThread::create([&] {
            QMutexLocker locker(&mutex);
            checkedEmpty.release(); allowWait.acquire();
            while (queued == 0) condition.wait(&mutex);
            completed = true;
        }));
        consumer->start();
        if (!checkedEmpty.tryAcquire(1, 2000)) return 4;
        allowWait.release();
        { QMutexLocker locker(&mutex); ++queued; condition.wakeAll(); }
        check(consumer->wait(2000) && completed.load(), "same-lock predicate wait receives notification without rescue");
        if (!consumer->isFinished()) { consumer.release(); return 5; }
    }
    {
        QThread sender;
        std::atomic_bool contextlessOnWorker{false}, contextualOnMain{false};
        QObject::connect(&sender, &QThread::started, [&] {
            contextlessOnWorker = QThread::currentThread() != app.thread();
            sender.quit();
        });
        QObject::connect(&sender, &QThread::started, &app, [&] {
            contextualOnMain = QThread::currentThread() == app.thread();
        }, Qt::QueuedConnection);
        QObject::connect(&sender, &QThread::finished, &app, &QCoreApplication::quit, Qt::QueuedConnection);
        QTimer::singleShot(2000, &app, &QCoreApplication::quit);
        sender.start(); app.exec();
        check(sender.wait(2000), "signal-delivery probe joins its own worker");
        check(contextlessOnWorker.load(), "receiverless functor executes on emitting worker, not the GUI owner thread");
        check(contextualOnMain.load(), "explicit queued receiver context delivers to the application thread");
    }
    qInfo() << "Native protocol probe failures:" << failures;
    return failures ? 1 : 0;
}
