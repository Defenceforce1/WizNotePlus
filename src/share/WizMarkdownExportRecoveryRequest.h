#pragma once

#include "WizDocumentAsync.h"
#include "WizMarkdownExportRecovery.h"
#include <QObject>
#include <QPointer>
#include <memory>
#include <optional>
#include <utility>

// Dormant request-lifetime gate, not a worker, recovery executor or quit policy.
// Construct/call/destroy on the owning GUI thread, parented to an application-
// scoped host rather than the originating view. No filesystem/audit calls.
class WizMarkdownExportRecoveryRequest final : public QObject
{
    struct Shared;
public:
    enum class Stage { AwaitingDecision, Queued, Running, Completing, Completed, Retained };

    // Only this value may cross threads. No QObject, QPointer or document-token
    // access occurs through it. It keeps the frozen destination and completion
    // alive even after a view/request is destroyed. It does not schedule work.
    class Work final {
    public:
        Work() = default;
        explicit operator bool() const { return bool(m_shared); }
        QString destination() const;
        Stage stage() const;
        // Start and cancellation compete atomically. Call immediately before
        // the already-authorized backend; only one worker can win.
        bool tryStart() const;
        // Queue-discard hook. Thread-safe and never cancels a started job.
        // A runnable destroyed without execution must release its pending claim.
        void abandonBeforeStart() const;
        bool complete(WizMarkdownExportRecovery::Result result) const;
        std::optional<WizMarkdownExportRecovery::Result> completedResult() const;
    private:
        friend class WizMarkdownExportRecoveryRequest;
        explicit Work(std::shared_ptr<Shared> shared) : m_shared(std::move(shared)) {}
        std::shared_ptr<Shared> m_shared;
    };

    WizMarkdownExportRecoveryRequest(QObject* owner,
        WizDocumentCallbackEpoch::Ticket document, QString destination,
        QObject* applicationHost = nullptr);
    ~WizMarkdownExportRecoveryRequest() override;

    // Inspect leaves the choice pending. Cancel/Retain/unknown decisions retain.
    // Only a current owner's explicit Confirm can produce one Work packet.
    Work decide(WizMarkdownExportRecovery::Decision decision);

    // The host MUST call before navigation, owner-derived teardown or leaving
    // the originating document context (not just rely on QPointer at teardown).
    // A document ticket is also rechecked at decision and UI receipt. Invalidate
    // does not stop/undo already-started work or clear its real completion.
    bool invalidate();
    // One-way and immediate. No wait, event pumping, I/O, timer, sync or retry.
    // A canceled quit requires a fresh request and new explicit confirmation.
    bool beginQuit();
    Stage stage() const;
    bool isCurrent() const;
    // UI-only, once, and only for a current context. Dropping this receipt never
    // drops/relabels Work::completedResult(). No callback is executed here.
    // Never use this receipt as a local-note-save persistence acknowledgment.
    std::optional<WizMarkdownExportRecovery::Result> takeForUi();

private:
    bool onOwnerThread() const;
    QPointer<QObject> m_owner;
    WizDocumentCallbackEpoch::Ticket m_document;
    const std::shared_ptr<Shared> m_shared;
    bool m_uiEligible = true;
    bool m_decided = false;
    bool m_delivered = false;
};
