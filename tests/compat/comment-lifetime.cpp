#include "core/WizCommentManager.h"
#include "share/WizThreads.h"
#include "share/WizNetworkSecurity.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QThread>
#include <QPointer>
#include <QDebug>
#include <atomic>
#include <memory>
#include <vector>

// No account, database, token singleton or HTTP endpoint. The real production
// scheduler, cancellation state, QObject connections and delivery code are used;
// only the network body of running tasks is replaced by synthetic signal values.
int wizCommentLifetimeSelfTest(int argc, char** argv)
{
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    QCoreApplication app(argc, argv);
    WizQueuedThreadsInit();
    struct Gate { std::atomic<int> entered{0}; std::atomic_bool release{false}; };
    std::vector<std::shared_ptr<Gate>> gates;
    struct Cleanup {
        std::vector<std::shared_ptr<Gate>>& gates;
        ~Cleanup() { for (const auto& gate : gates) gate->release = true; WizQueuedThreadsShutdown(); }
    } cleanup{gates};
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok; qInfo().noquote() << (ok ? "PASS" : "FAIL") << label; return ok;
    };
    const auto waitFor = [&](const std::function<bool()>& predicate, bool events = true) {
        QElapsedTimer elapsed; elapsed.start();
        while (!predicate() && elapsed.elapsed() < 4000) {
            if (events) app.processEvents();
            QThread::msleep(2);
        }
        return predicate();
    };
    const auto blocked = std::make_shared<Gate>(); gates.push_back(blocked);
    for (int i = 0; i < 2; ++i) WizExecuteOnThread(WIZ_THREAD_NETWORK, [blocked] {
        ++blocked->entered;
        while (!blocked->release) QThread::msleep(1);
    });
    if (!check(waitFor([&] { return blocked->entered == 2; }), "both actual network workers are held before comment tasks start")) return 1;
    std::vector<std::shared_ptr<std::atomic_bool>> canceled;
    for (int i = 0; i < 100; ++i) {
        auto* page = new QObject;
        auto* manager = new WizCommentManager(page);
        manager->queryCommentCount("synthetic-kb", QString("closed-note-%1").arg(i), true);
        canceled.push_back(manager->m_canceled);
        QPointer<WizCommentManager> weak(manager);
        delete page;
        if (!weak.isNull()) return 1;
    }
    bool allCanceled = true;
    for (const auto& flag : canceled) allCanceled &= flag->load();
    check(allCanceled, "destroying one hundred note parents cancels every queued real comment request");

    WizCommentManager live;
    int tokenSignals = 0, countSignals = 0;
    bool guiDelivery = true;
    QObject::connect(&live, &WizCommentManager::tokenAcquired, &app, [&](const QString& token) {
        guiDelivery &= QThread::currentThread() == app.thread();
        if (token == "synthetic-token") ++tokenSignals;
    });
    QObject::connect(&live, &WizCommentManager::commentCountAcquired, &app, [&](const QString& guid, int count) {
        guiDelivery &= QThread::currentThread() == app.thread();
        if (guid == "synthetic-note" && count == 7) ++countSignals;
    });
    WizCommentManager::CountQueryData data;
    data.strGUID = "synthetic-note"; data.strKBGUID = "synthetic-kb";
    live.setCommentUrlTemplate("https://example.invalid/template-not-requested");
    const auto liveRan = std::make_shared<std::atomic_bool>(false);
    live.enqueueQuery(data, [liveRan](WizCommentQuerier& query) {
        Q_EMIT query.tokenAcquired("synthetic-token");
        Q_EMIT query.commentCountAcquired("synthetic-note", 7);
        *liveRan = true;
    });
    check(live.m_busy, "busy state is reserved on GUI thread before a worker can run");
    blocked->release = true;
    check(waitFor([&] { return liveRan->load() && tokenSignals == 1 && countSignals == 1 && !live.m_busy; }),
          "canceled real jobs drain without network access and the following live job completes");
    check(guiDelivery, "live comment signals are delivered only on the GUI thread");
    check(live.getCommentUrlTemplate() == "https://example.invalid/template-not-requested",
          "immutable URL snapshot returns safely to the live manager");

    auto running = std::make_shared<Gate>(); gates.push_back(running);
    auto* parent = new QObject;
    auto* dying = new WizCommentManager(parent);
    const auto dyingCanceled = dying->m_canceled;
    int staleSignals = 0;
    QObject::connect(dying, &WizCommentManager::tokenAcquired, &app, [&](const QString&) { ++staleSignals; });
    QObject::connect(dying, &WizCommentManager::commentCountAcquired, &app, [&](const QString&, int) { ++staleSignals; });
    const auto finished = std::make_shared<std::atomic_bool>(false);
    dying->enqueueQuery(data, [running, finished](WizCommentQuerier& query) {
        ++running->entered;
        while (!running->release) QThread::msleep(1);
        Q_EMIT query.tokenAcquired("late-synthetic-token");
        Q_EMIT query.commentCountAcquired("closed-synthetic-note", 99);
        *finished = true;
    });
    if (!check(waitFor([&] { return running->entered == 1; }), "real worker connection setup finishes before the note is destroyed")) {
        // Drain before references captured by the synthetic body leave scope,
        // including on a heavily loaded machine's fixture-timeout path.
        for (const auto& gate : gates) gate->release = true;
        WizQueuedThreadsShutdown();
        delete parent;
        return 1;
    }
    delete parent;
    check(dyingCanceled->load(), "destroying a note during a running task marks its independent cancellation state");
    running->release = true;
    check(waitFor([&] { return finished->load(); }), "a running task finishes safely after its note manager has gone");
    // An ordered fence on that worker queue makes sure late signal/completion
    // closures have been posted before testing their GUI-side delivery.
    const auto fence = std::make_shared<std::atomic_bool>(false);
    WizExecuteOnThread(WIZ_THREAD_NETWORK, [fence] { *fence = true; });
    check(waitFor([&] { return fence->load(); }), "worker pool remains usable after a running note task is canceled");
    for (int i = 0; i < 20; ++i) { app.processEvents(); QThread::msleep(2); }
    check(staleSignals == 0, "late token and count signals never reach a destroyed note");

    auto* late = new WizCommentManager;
    int lateSignals = 0;
    QObject::connect(late, &WizCommentManager::tokenAcquired, &app, [&](const QString&) { ++lateSignals; });
    const auto posted = std::make_shared<std::atomic_bool>(false);
    late->enqueueQuery(data, [posted](WizCommentQuerier& query) {
        Q_EMIT query.tokenAcquired("already-posted-token");
        *posted = true;
    });
    check(waitFor([&] { return posted->load(); }, false), "result is queued while GUI delivery is deliberately held");
    delete late;
    app.processEvents();
    check(lateSignals == 0, "queued result delivery after manager deletion is harmless and emits no stale signal");

    WizNetworkSecurity::beginShutdown();
    WizCommentManager stopped;
    stopped.queryCommentCount("synthetic-kb", "shutdown-note", true);
    check(!stopped.m_busy && !stopped.m_timer.isActive() && stopped.m_queryList.isEmpty(),
          "shutdown refuses fresh comment work before any token or endpoint lookup");
    qInfo() << "Comment lifetime checks:" << checks << "failures:" << failures;
    return failures ? 1 : 0;
}
