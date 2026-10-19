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

    {
        QObject reusedView;
        WizDocumentCallbackEpoch document;
        int legacyUi = 0, currentUi = 0, activeSaveCompletions = 0;
        const auto originalDocument = document.ticket();
        WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::guarded(&reusedView, [&](QObject&) { ++legacyUi; }));
        WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::forDocument(&reusedView, originalDocument,
            [&](QObject&) { ++currentUi; }));
        // An already handed-off persistence completion must remain independent
        // of the originating view's navigation; do not apply UI guards to it.
        WizExecuteOnThread(WIZ_THREAD_MAIN, [&] { ++activeSaveCompletions; });
        document.invalidate(); app.processEvents();
        check(legacyUi == 1, "negative control: owner-only guard delivers a stale UI command after same-view navigation");
        check(currentUi == 0 && activeSaveCompletions == 1,
              "document guard skips stale UI work but does not cancel an independent active-save completion");
        WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::forDocument(&reusedView, document.ticket(),
            [&](QObject&) { ++currentUi; }));
        app.processEvents();
        check(currentUi == 1, "current document command still runs through the existing main queue");
        const auto beforeRoundTrip = document.ticket();
        WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::forDocument(&reusedView, beforeRoundTrip,
            [&](QObject&) { ++currentUi; }));
        document.invalidate(); document.invalidate(); app.processEvents();
        check(currentUi == 1, "leaving and returning to a document cannot revive its old queued generation");
        WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::forDocument(&reusedView, {},
            [&](QObject&) { ++currentUi; }));
        app.processEvents();
        check(currentUi == 1, "missing document ticket cannot authorize a queued UI command");
        auto closingView = std::make_unique<QObject>();
        WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::forDocument(closingView.get(), document.ticket(),
            [&](QObject&) { ++currentUi; }));
        closingView.reset(); app.processEvents();
        check(currentUi == 1, "valid document ticket cannot keep a closed view alive or enter it");
    }

    {
        QObject documentOwner;
        WizDocumentCallbackEpoch document;
        int legacyUi=0, currentUi=0, posts=0;
        const auto post=[&](auto action) { ++posts;WizExecuteOnThread(WIZ_THREAD_MAIN,std::move(action)); };
        const auto legacy=[&](const QVariant&) { post([&] { ++legacyUi; }); };
        legacy(QVariant(false));app.processEvents();
        check(legacyUi==1,"negative control: ignoring a failed save still queues the following UI operation");
        posts=0;
        auto afterSave=WizDocumentOwnerAction::afterSuccessfulSave(&documentOwner,document.ticket(),post,
            [&](QObject&) { ++currentUi; });
        for (const auto& result : {QVariant(false),QVariant(),QVariant("true"),QVariant(1)}) afterSave(result);
        app.processEvents();
        check(posts==0&&currentUi==0,"failed or untyped save results cannot open the subsequent document editor");
        afterSave(QVariant(true));
        check(posts==1&&currentUi==0,"confirmed successful save retains deferred main-queue scheduling");
        app.processEvents();
        check(currentUi==1,"confirmed successful save opens the subsequent UI for its live original document");
        afterSave(QVariant(true));document.invalidate();app.processEvents();
        check(currentUi==1,"navigation after save completion but before main delivery rejects stale document editing");
        auto late=WizDocumentOwnerAction::afterSuccessfulSave(&documentOwner,document.ticket(),post,
            [&](QObject&) { ++currentUi; });
        document.invalidate();late(QVariant(true));app.processEvents();
        check(currentUi==1,"late successful save remains acknowledged without reviving the old document UI");
        auto closing=std::make_unique<QObject>();
        auto closed=WizDocumentOwnerAction::afterSuccessfulSave(closing.get(),document.ticket(),post,
            [&](QObject&) { ++currentUi; });
        closing.reset();closed(QVariant(true));app.processEvents();
        check(currentUi==1,"a completed save cannot reopen a destroyed document view");
        // A modal operation may invalidate the view before it returns.
        auto resume=WizDocumentOwnerAction::forDocument(&documentOwner,document.ticket(),
            [&](QObject&) { ++currentUi; });
        document.invalidate();resume();
        check(currentUi==1,"guarded continuation after a modal operation cannot reload a replacement document");
    }
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
