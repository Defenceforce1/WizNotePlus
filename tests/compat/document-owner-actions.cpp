#include "share/WizDocumentAsync.h"
#include "share/WizThreads.h"
#include <QCoreApplication>
#include <QDebug>
#include <QVector>
#include <memory>

// The actual main-action queue and actual owner wrapper, without a browser,
// account, note store, or physical input. No intentionally invalid dereference.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* description) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << description;
    };
    WizQueuedThreadsInit();
    int calls = 0;
    auto owner = std::make_unique<QObject>();
    WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::guarded(owner.get(), [&](QObject&) { ++calls; }));
    check(calls == 0, "guard preserves deferred main-queue scheduling rather than running inline");
    app.processEvents();
    check(calls == 1, "actual main queue delivers an action to its living document owner");

    const QPointer<QObject> oldOwner(owner.get());
    bool unguardedDeliveredAfterClose = false;
    WizExecuteOnThread(WIZ_THREAD_MAIN, [oldOwner, &unguardedDeliveredAfterClose] {
        unguardedDeliveredAfterClose = oldOwner.isNull();
    });
    WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::guarded(owner.get(), [&](QObject&) { ++calls; }));
    owner.reset(); app.processEvents();
    check(unguardedDeliveredAfterClose,
          "negative control confirms the unguarded queue still delivers after an unrelated view closes");
    check(calls == 1, "guarded action in that same actual batch cannot enter the closed view");

    auto deletingOwner = std::make_unique<QObject>();
    WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::guarded(deletingOwner.get(), [&](QObject&) {
        ++calls; deletingOwner.reset();
    }));
    WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::guarded(deletingOwner.get(), [&](QObject&) { ++calls; }));
    app.processEvents();
    check(calls == 2 && !deletingOwner, "a callback may close its view while later actions in the batch are safely skipped");

    QObject liveOwner;
    QVector<int> sequence;
    for (int index = 0; index < 3; ++index)
        WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::guarded(&liveOwner, [&, index](QObject&) {
            sequence.append(index);
        }));
    app.processEvents();
    check(sequence == QVector<int>({0, 1, 2}), "guard retains FIFO ordering for actions on a live view");

    auto payload = std::make_shared<int>(42);
    const std::weak_ptr<int> weakPayload(payload);
    WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::guarded(&liveOwner, [payload = std::move(payload), &calls](QObject&) {
        ++calls;
    }));
    WizQueuedThreadsShutdown();
    app.processEvents();
    check(calls == 2 && weakPayload.expired(), "queue shutdown still discards guarded work and releases its captured resources");
    WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::guarded(&liveOwner, [&](QObject&) { ++calls; }));
    app.processEvents();
    check(calls == 2, "guard does not bypass the main queue's rejection of work after shutdown");
    qInfo() << "Document owner action checks:" << checks << "failures:" << failures;
    return failures ? 1 : 0;
}
