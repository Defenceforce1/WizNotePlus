#include "share/WizMarkdownExportRecoveryRequest.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QPointer>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <functional>

using namespace WizMarkdownExportRecovery;
using Request = WizMarkdownExportRecoveryRequest;
using Stage = Request::Stage;
static int checks = 0;
static int failures = 0;

static void check(bool condition, const QString& label)
{
    ++checks;
    if (!condition) {
        ++failures;
        QTextStream(stderr) << "FAIL: " << label << Qt::endl;
    }
}

static bool pumpUntil(const std::function<bool()>& predicate)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < 2500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::msleep(1);
    }
    return predicate();
}

static void join(QThread& thread)
{
    const bool stopped = thread.wait(3000);
    check(stopped, "bounded synthetic worker joined");
    if (!stopped)
        qFatal("synthetic worker did not stop; do not destroy a live QThread");
}

static Result outcome()
{
    Result result;
    result.recovery = RecoveryOutcome::RolledBack;
    result.audit = AuditOutcome::Failed;
    result.lockRelease = LockReleaseOutcome::Failed;
    result.userVisibleSummary = QStringLiteral("synthetic completed file result; audit/unlock unconfirmed");
    result.diagnosticDetail = QStringLiteral("/synthetic/only/export.md\nretained detail");
    return result;
}

static bool exact(const std::optional<Result>& result)
{
    const Result expected = outcome();
    return result && result->recovery == expected.recovery && result->audit == expected.audit
        && result->lockRelease == expected.lockRelease && result->userVisibleSummary == expected.userVisibleSummary
        && result->diagnosticDetail == expected.diagnosticDetail;
}

static void choices()
{
    Request::Work invalid;
    check(!invalid && invalid.destination().isEmpty() && invalid.stage() == Stage::Retained, "default work invalid");
    check(!invalid.tryStart() && !invalid.complete(outcome()) && !invalid.completedResult(), "invalid work cannot start or complete");
    QObject owner;
    WizDocumentCallbackEpoch document;
    for (Decision decision : {Decision::Cancel, Decision::Retain, static_cast<Decision>(99)}) {
        Request request(&owner, document.ticket(), QStringLiteral("/synthetic/choice.md"));
        check(request.isCurrent() && request.stage() == Stage::AwaitingDecision, "choice starts current and pending");
        check(!request.decide(Decision::Inspect) && request.stage() == Stage::AwaitingDecision, "inspect leaves decision pending");
        check(!request.decide(decision) && request.stage() == Stage::Retained, "nonconfirm choice retains");
        check(!request.decide(Decision::Confirm) && !request.takeForUi(), "retained choice cannot be reconfirmed");
    }
    {
        QString destination = QStringLiteral("/synthetic/frozen.md");
        Request request(&owner, document.ticket(), destination);
        auto work = request.decide(Decision::Confirm);
        destination = QStringLiteral("/synthetic/other.md");
        check(bool(work) && work.stage() == Stage::Queued, "explicit confirmation produces queued work");
        check(work.destination() == QStringLiteral("/synthetic/frozen.md"), "destination frozen by value");
        check(!request.decide(Decision::Confirm) && work.stage() == Stage::Queued, "duplicate choice neither duplicates nor cancels work");
        check(!work.complete(outcome()) && !work.completedResult() && !request.takeForUi(), "queued work cannot fabricate completion");
        check(work.tryStart() && !work.tryStart() && request.stage() == Stage::Running, "worker start claim once");
        check(!request.takeForUi(), "early UI receipt not terminal");
        check(work.complete(outcome()) && request.stage() == Stage::Completed, "running work publishes completion");
        check(!work.complete(Result()) && !work.tryStart(), "duplicate completion and restart refused");
        check(exact(work.completedResult()) && exact(work.completedResult()), "independent completion stable and exact");
        check(exact(request.takeForUi()) && !request.takeForUi(), "current UI receipt delivered once");
        check(exact(work.completedResult()), "taking UI receipt does not consume actual result");
    }
    {
        Request request(nullptr, document.ticket(), QStringLiteral("/synthetic/none.md"));
        check(!request.isCurrent() && request.stage() == Stage::Retained
            && !request.decide(Decision::Confirm), "null owner refused");
    }
    {
        Request request(&owner, {}, QStringLiteral("/synthetic/none.md"));
        check(!request.isCurrent() && !request.decide(Decision::Confirm), "empty document ticket refused");
    }
    {
        Request request(&owner, document.ticket(), QString());
        check(request.stage() == Stage::Retained && !request.decide(Decision::Confirm), "empty destination refused");
    }
    {
        Request request(&owner, document.ticket(), QStringLiteral("/synthetic/stale.md"));
        document.invalidate();
        check(!request.isCurrent() && !request.decide(Decision::Confirm)
            && request.stage() == Stage::Retained, "changed document before confirmation refused");
    }
    {
        Request request(&owner, document.ticket(), QStringLiteral("/synthetic/queue-discard.md"));
        auto work = request.decide(Decision::Confirm);
        work.abandonBeforeStart();
        check(work.stage() == Stage::Retained && !work.tryStart(), "discarded queue action releases unstarted claim");
        work.abandonBeforeStart();
        check(work.stage() == Stage::Retained, "queue abandonment is idempotent");
    }
    {
        Request request(&owner, document.ticket(), QStringLiteral("/synthetic/already-started.md"));
        auto work = request.decide(Decision::Confirm);
        check(work.tryStart(), "abandon-after-start fixture starts");
        work.abandonBeforeStart();
        check(work.stage() == Stage::Running && work.complete(outcome()), "queue abandonment never cancels started work");
        work.abandonBeforeStart();
        check(exact(work.completedResult()), "queue abandonment preserves completed result");
    }
}

static void pendingInvalidation()
{
    for (bool quit : {false, true}) {
        for (bool queued : {false, true}) {
            QObject owner;
            WizDocumentCallbackEpoch document;
            Request request(&owner, document.ticket(), QStringLiteral("/synthetic/pending.md"));
            auto work = queued ? request.decide(Decision::Confirm) : Request::Work();
            check(quit ? request.beginQuit() : request.invalidate(), "GUI invalidation accepted");
            check(request.stage() == Stage::Retained && !request.isCurrent(), "pending/queued request retains on invalidate or exit");
            check(!work.tryStart() && !request.decide(Decision::Confirm) && !request.takeForUi(), "invalidated work cannot start or deliver");
            check(request.beginQuit() && request.invalidate() && request.stage() == Stage::Retained, "repeated cancellation idempotent");
        }
    }
    {
        auto* owner = new QObject;
        WizDocumentCallbackEpoch document;
        Request request(owner, document.ticket(), QStringLiteral("/synthetic/deleted-owner.md"));
        auto work = request.decide(Decision::Confirm);
        delete owner;
        check(!request.isCurrent() && request.stage() == Stage::Retained && !work.tryStart(), "destroyed owner automatically cancels queued work");
    }
    {
        QObject owner;
        WizDocumentCallbackEpoch document;
        Request::Work work;
        {
            Request request(&owner, document.ticket(), QStringLiteral("/synthetic/deleted-request.md"));
            work = request.decide(Decision::Confirm);
        }
        check(work.stage() == Stage::Retained && !work.tryStart(), "request destruction cancels unstarted packet");
    }
    {
        class Owner final : public QObject {
        public:
            Request* request = nullptr;
            Request::Work work;
            bool* guardClosedBeforeBase = nullptr;
            ~Owner() override
            {
                request->invalidate(); // Required BEFORE base QObject teardown.
                *guardClosedBeforeBase = !work.tryStart() && !request->isCurrent();
            }
        };
        auto* owner = new Owner;
        WizDocumentCallbackEpoch document;
        Request request(owner, document.ticket(), QStringLiteral("/synthetic/early-teardown.md"));
        owner->request = &request;
        owner->work = request.decide(Decision::Confirm);
        bool early = false;
        owner->guardClosedBeforeBase = &early;
        delete owner;
        check(early && request.stage() == Stage::Retained, "derived teardown invalidates before QPointer base clearing");
    }
}

static void staleReceipts()
{
    for (int invalidation = 0; invalidation != 4; ++invalidation) {
        auto* owner = new QObject;
        WizDocumentCallbackEpoch document;
        Request request(owner, document.ticket(), QStringLiteral("/synthetic/receipt.md"));
        const auto work = request.decide(Decision::Confirm);
        check(work.tryStart() && work.complete(outcome()), "receipt fixture completed");
        if (invalidation == 0)
            request.beginQuit();
        else if (invalidation == 1)
            request.invalidate();
        else if (invalidation == 2) {
            document.invalidate();
            document.invalidate(); // Leave and return to the same note != same generation.
        } else {
            delete owner;
            owner = nullptr;
        }
        check(!request.takeForUi() && !request.takeForUi(), "stale/quit/deleted-owner UI receipt discarded");
        check(exact(work.completedResult()), "discarded UI does not rewrite file/audit/unlock result");
        check(work.stage() == Stage::Completed, "completed state remains completed after invalidation");
        delete owner;
    }
    {
        QObject owner;
        WizDocumentCallbackEpoch document;
        Request oldRequest(&owner, document.ticket(), QStringLiteral("/synthetic/same-note.md"));
        const auto oldWork = oldRequest.decide(Decision::Confirm);
        check(oldWork.tryStart(), "old request starts");
        oldRequest.invalidate(); // Host invalidates before changing its document epoch.
        document.invalidate();
        Request fresh(&owner, document.ticket(), QStringLiteral("/synthetic/same-note.md"));
        const auto newWork = fresh.decide(Decision::Confirm);
        check(newWork.tryStart() && newWork.complete(outcome()) && oldWork.complete(outcome()), "old/new requests independently complete");
        check(!oldRequest.takeForUi() && exact(fresh.takeForUi()), "old same-note callback cannot deliver into new request");
        check(exact(oldWork.completedResult()) && exact(newWork.completedResult()), "both independent outcomes retained");
    }
    {
        QObject owner;
        WizDocumentCallbackEpoch document;
        Request::Work work;
        {
            Request request(&owner, document.ticket(), QStringLiteral("/synthetic/request-gone-running.md"));
            work = request.decide(Decision::Confirm);
            check(work.tryStart(), "packet starts before request destruction");
        }
        check(work.stage() == Stage::Running && work.complete(outcome()) && exact(work.completedResult()),
            "request destruction does not cancel running completion");
    }
    {
        auto* owner = new QObject;
        WizDocumentCallbackEpoch document;
        Request request(owner, document.ticket(), QStringLiteral("/synthetic/queued-ui.md"));
        auto work = request.decide(Decision::Confirm);
        check(work.tryStart() && work.complete(outcome()), "queued UI fixture complete");
        bool queuedRan = false;
        bool delivered = false;
        QTimer::singleShot(0, &request, [&] { queuedRan = true; delivered = bool(request.takeForUi()); });
        delete owner;
        check(pumpUntil([&] { return queuedRan; }) && !delivered && exact(work.completedResult()), "owner removed before actual queued receipt");
    }
    {
        QObject owner;
        WizDocumentCallbackEpoch document;
        auto* request = new Request(&owner, document.ticket(), QStringLiteral("/synthetic/queued-request-gone.md"));
        bool ran = false;
        QTimer::singleShot(0, request, [&] { ran = true; });
        QPointer<Request> guarded(request);
        delete request;
        QCoreApplication::processEvents();
        check(!guarded && !ran, "context-bound receipt discarded when request gone");
    }
}

static void actualThreads()
{
    for (bool exitBeforeStart : {true, false}) {
        auto* owner = new QObject;
        WizDocumentCallbackEpoch document;
        Request request(owner, document.ticket(), QStringLiteral("/synthetic/worker.md"));
        const auto work = request.decide(Decision::Confirm);
        QSemaphore atGate, releaseGate, started, finishGate;
        bool claimed = false;
        bool recorded = false;
        bool waitsSucceeded = true;
        std::unique_ptr<QThread> worker(QThread::create([&] {
            atGate.release();
            waitsSucceeded = releaseGate.tryAcquire(1, 2500);
            claimed = work.tryStart();
            started.release();
            if (claimed) {
                waitsSucceeded = finishGate.tryAcquire(1, 2500) && waitsSucceeded;
                recorded = work.complete(outcome());
            }
        }));
        worker->start();
        check(atGate.tryAcquire(1, 2500), "real worker waiting before start claim");
        if (exitBeforeStart)
            request.beginQuit();
        releaseGate.release();
        check(started.tryAcquire(1, 2500), "real worker start decision observed");
        if (!exitBeforeStart) {
            check(request.stage() == Stage::Running, "actual worker running before quit");
            request.beginQuit();
            delete owner;
            owner = nullptr;
            bool guiReceipt = false;
            QTimer::singleShot(0, qApp, [&] { guiReceipt = true; });
            check(pumpUntil([&] { return guiReceipt; }) && !worker->isFinished(), "quit gate returns and GUI queue runs while worker held");
        }
        finishGate.release();
        join(*worker);
        check(waitsSucceeded && claimed == !exitBeforeStart && recorded == !exitBeforeStart, "start/cancel ordering determines whether work runs");
        check(exitBeforeStart ? (work.stage() == Stage::Retained && !work.completedResult())
                              : (work.stage() == Stage::Completed && exact(work.completedResult())), "actual worker outcome not mislabeled by quit");
        check(!request.takeForUi(), "quit never delivers stale UI");
        delete owner;
    }
    {
        QObject owner;
        WizDocumentCallbackEpoch document;
        Request request(&owner, document.ticket(), QStringLiteral("/synthetic/wrong-thread.md"));
        bool rejected = false;
        std::unique_ptr<QThread> worker(QThread::create([&] {
            rejected = !request.decide(Decision::Confirm) && !request.invalidate()
                && !request.beginQuit() && !request.takeForUi() && !request.isCurrent();
        }));
        worker->start();
        join(*worker);
        check(rejected && request.stage() == Stage::AwaitingDecision && request.isCurrent(), "wrong-thread GUI operations rejected without state mutation");
        check(bool(request.decide(Decision::Confirm)), "GUI confirmation still valid after rejected worker misuse");
    }
    {
        QObject owner;
        WizDocumentCallbackEpoch document;
        Request request(&owner, document.ticket(), QStringLiteral("/synthetic/competing-workers.md"));
        const auto work = request.decide(Decision::Confirm);
        QSemaphore ready, go;
        std::atomic_int starts {0}, completions {0};
        auto compete = [&] {
            ready.release();
            if (go.tryAcquire(1, 2500) && work.tryStart()) {
                ++starts;
                if (work.complete(outcome()))
                    ++completions;
            }
        };
        std::unique_ptr<QThread> first(QThread::create(compete)), second(QThread::create(compete));
        first->start();
        second->start();
        check(ready.tryAcquire(2, 2500), "two actual workers ready at shared start gate");
        go.release(2);
        join(*first);
        join(*second);
        check(starts.load() == 1 && completions.load() == 1 && exact(work.completedResult()), "atomic start admits one worker and completion");
        check(exact(request.takeForUi()) && !request.takeForUi(), "competing workers still deliver once");
    }
}

int main(int argc, char** argv)
{
    QCoreApplication::setOrganizationName(QStringLiteral("WizRecoveryRequestSynthetic"));
    QCoreApplication::setApplicationName(QStringLiteral("WizRecoveryRequestCoreOnly"));
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication application(argc, argv);
    QTemporaryDir sandbox;
    check(sandbox.isValid(), "synthetic sandbox valid");
    const QString path = sandbox.filePath(QStringLiteral("untouched.md"));
    QFile sentinel(path);
    const QByteArray bytes("synthetic lifetime sentinel\n");
    check(sentinel.open(QIODevice::WriteOnly) && sentinel.write(bytes) == bytes.size(), "synthetic sentinel created");
    sentinel.close();
    choices();
    pendingInvalidation();
    staleReceipts();
    actualThreads();
    check(sentinel.open(QIODevice::ReadOnly) && sentinel.readAll() == bytes, "synthetic sentinel unchanged");
    check(!QFile::exists(sandbox.filePath(QStringLiteral("audit"))), "no synthetic audit created");
    QTextStream(stdout) << "RECOVERY_REQUEST_CHECKS=" << checks << '\n'
        << "RECOVERY_REQUEST_FAILURES=" << failures << Qt::endl;
    return failures ? 1 : 0;
}
