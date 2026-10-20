#include "WizMarkdownExportRecoverySession.h"
#include "WizMarkdownExportRecoveryPrompt.h"
#include "share/WizMarkdownExportRecoveryCoordinator.h"
#include "share/WizMarkdownExportRecoveryAudit.h"
#include "share/WizThreads.h"

#include <QCoreApplication>
#include <functional>
#include <atomic>

struct WizMarkdownExportRecoverySession::Discovery
{
    enum class Stage { Idle, Queued, Running, Complete, Abandoned };
    explicit Discovery(QString target) : destination(std::move(target)) {}
    const QString destination;
    std::atomic<Stage> stage {Stage::Idle};
    WizMarkdownExportRecovery::Inspection inspection;
    WizMarkdownExportRecovery::LockInspection lock;
    void cancelBeforeStart()
    {
        Stage expected = stage.load(std::memory_order_acquire);
        while (expected == Stage::Idle || expected == Stage::Queued) {
            if (stage.compare_exchange_weak(expected, Stage::Abandoned,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                return;
        }
    }
};

class WizMarkdownExportRecoverySession::DiscoveryAction final : public IWizRunable
{
public:
    DiscoveryAction(std::shared_ptr<Discovery> discovery, std::function<void()> receipt)
        : m_discovery(std::move(discovery)), m_receipt(std::move(receipt)) {}
    ~DiscoveryAction() override { m_discovery->cancelBeforeStart(); }
    void run(int, IWizThreadPool*, IWizRunableEvents*) override
    {
        auto expected = Discovery::Stage::Queued;
        if (m_discovery->stage.compare_exchange_strong(expected, Discovery::Stage::Running,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            // Both APIs are read-only. No QObject/document ticket is read here;
            // only the GUI receipt may do that. Never acquire locks, prepare,
            // audit or execute plans from this discovery action.
            m_discovery->inspection = WizMarkdownExportRecovery::inspect(m_discovery->destination);
            m_discovery->lock = WizMarkdownExportRecovery::inspectDestinationLock(m_discovery->destination);
            m_discovery->stage.store(Discovery::Stage::Complete, std::memory_order_release);
        }
        WizExecuteOnThread(WIZ_THREAD_MAIN, m_receipt);
    }
private:
    std::shared_ptr<Discovery> m_discovery;
    std::function<void()> m_receipt;
};

struct WizMarkdownExportRecoverySession::RetainAudit
{
    enum class Stage { Queued, Running, Complete, Abandoned };
    explicit RetainAudit(QString identity) : identity(std::move(identity)) {}
    const QString identity; // Never carry a note, path, QObject or document ticket.
    std::atomic<Stage> stage {Stage::Queued};
    void cancelBeforeStart()
    {
        auto expected = Stage::Queued;
        stage.compare_exchange_strong(expected, Stage::Abandoned,
            std::memory_order_acq_rel, std::memory_order_acquire);
    }
};

class WizMarkdownExportRecoverySession::RetainAuditAction final : public IWizRunable
{
public:
    explicit RetainAuditAction(std::shared_ptr<RetainAudit> audit) : m_audit(std::move(audit)) {}
    ~RetainAuditAction() override { m_audit->cancelBeforeStart(); }
    void run(int, IWizThreadPool*, IWizRunableEvents*) override
    {
        auto expected = RetainAudit::Stage::Queued;
        if (m_audit->stage.compare_exchange_strong(expected, RetainAudit::Stage::Running,
                std::memory_order_acq_rel, std::memory_order_acquire)) {
            // Optional and single-attempt: no UI receipt, recovery, takeover,
            // retry or quit barrier regardless of the append outcome.
            WizMarkdownExportRecoveryAudit::appendRetain(m_audit->identity);
            m_audit->stage.store(RetainAudit::Stage::Complete, std::memory_order_release);
        }
    }
private:
    std::shared_ptr<RetainAudit> m_audit;
};

namespace {
class ConfirmAction final : public IWizRunable
{
public:
    ConfirmAction(WizMarkdownExportRecoveryRequest::Work work, std::function<void()> receipt)
        : m_work(std::move(work)), m_receipt(std::move(receipt)) {}
    ~ConfirmAction() override { m_work.abandonBeforeStart(); }
    void run(int, IWizThreadPool*, IWizRunableEvents*) override
    {
        const QString destination = m_work.destination();
        if (m_work.tryStart()) {
            // Do not queue/wait again after claiming start. The coordinator
            // revalidates actual files and owns all lock/audit ordering.
            m_work.complete(WizMarkdownExportRecoveryCoordinator::decide(
                destination, WizMarkdownExportRecovery::Decision::Confirm));
        }
        // This closure was created on the GUI thread and is read only there.
        // Queue shutdown may discard it; Work still contains the real result.
        WizExecuteOnThread(WIZ_THREAD_MAIN, m_receipt);
    }
private:
    WizMarkdownExportRecoveryRequest::Work m_work;
    std::function<void()> m_receipt;
};
}

WizMarkdownExportRecoverySession::WizMarkdownExportRecoverySession(QWidget* owner,
    WizDocumentCallbackEpoch::Ticket document, const QString& destination)
    : QObject(QCoreApplication::instance()), m_owner(owner),
      m_discovery(std::make_shared<Discovery>(destination))
{
    m_request = new WizMarkdownExportRecoveryRequest(owner, std::move(document), destination, this);
}

WizMarkdownExportRecoverySession::WizMarkdownExportRecoverySession(QWidget* owner,
    WizDocumentCallbackEpoch::Ticket document, const QString& destination,
    const WizMarkdownExportRecovery::Inspection& inspection, WizMarkdownExportRecovery::LockState lock)
    : WizMarkdownExportRecoverySession(owner, std::move(document), destination)
{
    m_discovery.reset();
    attachPrompt(inspection, lock);
}

void WizMarkdownExportRecoverySession::attachPrompt(
    const WizMarkdownExportRecovery::Inspection& inspection, WizMarkdownExportRecovery::LockState lock)
{
    if (!m_owner || !m_request->isCurrent() || m_settled || m_prompt)
        return;
    m_retainIdentity = inspection.destinationIdentity;
    m_prompt = new WizMarkdownExportRecoveryPrompt(inspection, lock, m_owner);
    connect(m_prompt, &QDialog::finished, this, [this] { promptFinished(); });
}

WizMarkdownExportRecoverySession::~WizMarkdownExportRecoverySession()
{
    m_lifetime.cancel(); // Before QObject base teardown/weak-pointer clearing.
    if (m_discovery)
        m_discovery->cancelBeforeStart();
    if (m_retainAudit)
        m_retainAudit->cancelBeforeStart();
    m_request->invalidate();
    // Do not emit a new dialog decision from inside session destruction.
    delete m_prompt.data();
}

WizMarkdownExportRecoveryPrompt* WizMarkdownExportRecoverySession::prompt() const
{
    return m_prompt.data();
}

void WizMarkdownExportRecoverySession::show()
{
    if (m_settled)
        return;
    if (!m_request->isCurrent()) {
        invalidate();
        return;
    }
    if (m_prompt)
        m_prompt->show();
    else if (m_discovery)
        startDiscovery();
}

void WizMarkdownExportRecoverySession::invalidate()
{
    if (m_retainAudit)
        m_retainAudit->cancelBeforeStart();
    if (m_discovery)
        m_discovery->cancelBeforeStart();
    m_request->invalidate();
    // May synchronously destroy this session through an outside finished slot.
    // Do not access any members after this call.
    if (m_prompt)
        m_prompt->retainForExit();
    else if (!m_work)
        notifySettled(); // Abandoned read-only delivery need not keep a view alive.
}

void WizMarkdownExportRecoverySession::startDiscovery()
{
    auto expected = Discovery::Stage::Idle;
    if (!m_discovery->stage.compare_exchange_strong(expected, Discovery::Stage::Queued,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return;
    auto receipt = WizDocumentOwnerAction::forDocument(this, m_lifetime.ticket(),
        [](WizMarkdownExportRecoverySession& session) { session.discoveryFinished(); });
    WizQueuedThreadAddAction(WIZ_THREAD_DEFAULT, new DiscoveryAction(m_discovery, std::move(receipt)));
    if (m_discovery->stage.load(std::memory_order_acquire) == Discovery::Stage::Abandoned)
        notifySettled();
}

void WizMarkdownExportRecoverySession::discoveryFinished()
{
    if (m_settled)
        return;
    if (!m_request->isCurrent()
        || m_discovery->stage.load(std::memory_order_acquire) != Discovery::Stage::Complete) {
        invalidate();
        return;
    }
    const auto inspection = m_discovery->inspection;
    const auto lock = m_discovery->lock;
    QPointer<WizMarkdownExportRecoverySession> self(this);
    emit discovered(inspection, lock);
    if (!self)
        return;
    if (!m_request->isCurrent()) {
        invalidate();
        return;
    }
    if (inspection.availability == WizMarkdownExportRecovery::Availability::None
        && lock.state == WizMarkdownExportRecovery::LockState::Available) {
        notifySettled();
        return;
    }
    auto view = inspection;
    view.diagnosticDetail += QStringLiteral("\n") + lock.diagnosticDetail;
    attachPrompt(view, lock.state);
    if (m_prompt)
        m_prompt->show(); // May destroy this session. No member access afterwards.
}

void WizMarkdownExportRecoverySession::beginQuit()
{
    invalidate();
}

void WizMarkdownExportRecoverySession::promptFinished()
{
    const auto decision = m_prompt ? m_prompt->decision() : WizMarkdownExportRecovery::Decision::Retain;
    const bool auditRetain = decision == WizMarkdownExportRecovery::Decision::Retain
        && m_request->isCurrent() && !m_settled;
    if (m_prompt) {
        m_prompt->deleteLater();
        m_prompt = nullptr;
    }
    m_work = m_request->decide(decision);
    if (!m_work) {
        if (auditRetain)
            startRetainAudit();
        notifySettled();
        return;
    }
    auto receipt = WizDocumentOwnerAction::forDocument(this, m_lifetime.ticket(),
        [](WizMarkdownExportRecoverySession& session) { session.deliver(); });
    WizQueuedThreadAddAction(WIZ_THREAD_DEFAULT, new ConfirmAction(m_work, std::move(receipt)));
    // Rejected submissions are destroyed synchronously by the existing queue.
    if (m_work.stage() == WizMarkdownExportRecoveryRequest::Stage::Retained)
        notifySettled();
}

void WizMarkdownExportRecoverySession::startRetainAudit()
{
    if (m_retainAudit || m_retainIdentity.isEmpty())
        return;
    m_retainAudit = std::make_shared<RetainAudit>(m_retainIdentity);
    WizQueuedThreadAddAction(WIZ_THREAD_DEFAULT, new RetainAuditAction(m_retainAudit));
}

void WizMarkdownExportRecoverySession::deliver()
{
    const auto result = m_request->takeForUi();
    QPointer<WizMarkdownExportRecoverySession> self(this);
    if (result)
        emit resultReady(*result);
    if (self)
        notifySettled();
}

void WizMarkdownExportRecoverySession::notifySettled()
{
    if (m_settled)
        return;
    m_settled = true;
    emit settled(); // May delete this session. No member access afterwards.
}
