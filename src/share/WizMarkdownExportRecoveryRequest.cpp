#include "WizMarkdownExportRecoveryRequest.h"

#include <QThread>
#include <atomic>

struct WizMarkdownExportRecoveryRequest::Shared
{
    explicit Shared(QString destination) : destination(std::move(destination)) {}
    const QString destination;
    std::atomic<Stage> stage {Stage::AwaitingDecision};
    // Written only by the winner of Running -> Completing, read only after an
    // acquire load of Completed. Never changed afterwards, including on exit.
    WizMarkdownExportRecovery::Result result;

    void cancelBeforeStart()
    {
        Stage expected = stage.load(std::memory_order_acquire);
        while (expected == Stage::AwaitingDecision || expected == Stage::Queued) {
            if (stage.compare_exchange_weak(expected, Stage::Retained,
                    std::memory_order_acq_rel, std::memory_order_acquire))
                return;
        }
    }
};

WizMarkdownExportRecoveryRequest::WizMarkdownExportRecoveryRequest(QObject* owner,
    WizDocumentCallbackEpoch::Ticket document, QString destination, QObject* applicationHost)
    : QObject(applicationHost), m_owner(owner), m_document(std::move(document)),
      m_shared(std::make_shared<Shared>(std::move(destination)))
{
    if (!isCurrent() || m_shared->destination.isEmpty()) {
        invalidate();
        return;
    }
    connect(owner, &QObject::destroyed, this, [this] { invalidate(); });
}

WizMarkdownExportRecoveryRequest::~WizMarkdownExportRecoveryRequest()
{
    m_shared->cancelBeforeStart();
}

bool WizMarkdownExportRecoveryRequest::onOwnerThread() const
{
    return QThread::currentThread() == thread();
}

bool WizMarkdownExportRecoveryRequest::isCurrent() const
{
    return onOwnerThread() && m_uiEligible && m_owner && m_owner->thread() == thread()
        && WizDocumentCallbackEpoch::current(m_document);
}

WizMarkdownExportRecoveryRequest::Stage WizMarkdownExportRecoveryRequest::stage() const
{
    return m_shared->stage.load(std::memory_order_acquire);
}

WizMarkdownExportRecoveryRequest::Work WizMarkdownExportRecoveryRequest::decide(
    WizMarkdownExportRecovery::Decision decision)
{
    using WizMarkdownExportRecovery::Decision;
    if (!onOwnerThread())
        return {};
    if (decision == Decision::Inspect) {
        if (!isCurrent())
            invalidate();
        return {};
    }
    if (m_decided)
        return {};
    m_decided = true;
    if (decision != Decision::Confirm || !isCurrent()) {
        invalidate();
        return {};
    }
    Stage expected = Stage::AwaitingDecision;
    if (!m_shared->stage.compare_exchange_strong(expected, Stage::Queued,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return {};
    return Work(m_shared);
}

bool WizMarkdownExportRecoveryRequest::invalidate()
{
    if (!onOwnerThread())
        return false;
    m_uiEligible = false;
    m_decided = true;
    m_shared->cancelBeforeStart();
    return true;
}

bool WizMarkdownExportRecoveryRequest::beginQuit()
{
    return invalidate();
}

std::optional<WizMarkdownExportRecovery::Result> WizMarkdownExportRecoveryRequest::takeForUi()
{
    if (!onOwnerThread() || m_delivered)
        return std::nullopt;
    if (!isCurrent()) {
        invalidate();
        return std::nullopt;
    }
    auto result = Work(m_shared).completedResult();
    if (result)
        m_delivered = true;
    return result;
}

QString WizMarkdownExportRecoveryRequest::Work::destination() const
{
    return m_shared ? m_shared->destination : QString();
}

WizMarkdownExportRecoveryRequest::Stage WizMarkdownExportRecoveryRequest::Work::stage() const
{
    return m_shared ? m_shared->stage.load(std::memory_order_acquire) : Stage::Retained;
}

bool WizMarkdownExportRecoveryRequest::Work::tryStart() const
{
    if (!m_shared)
        return false;
    Stage expected = Stage::Queued;
    return m_shared->stage.compare_exchange_strong(expected, Stage::Running,
        std::memory_order_acq_rel, std::memory_order_acquire);
}

bool WizMarkdownExportRecoveryRequest::Work::complete(WizMarkdownExportRecovery::Result result) const
{
    if (!m_shared)
        return false;
    Stage expected = Stage::Running;
    if (!m_shared->stage.compare_exchange_strong(expected, Stage::Completing,
            std::memory_order_acq_rel, std::memory_order_acquire))
        return false;
    m_shared->result = std::move(result);
    m_shared->stage.store(Stage::Completed, std::memory_order_release);
    return true;
}

void WizMarkdownExportRecoveryRequest::Work::abandonBeforeStart() const
{
    if (m_shared)
        m_shared->cancelBeforeStart();
}

std::optional<WizMarkdownExportRecovery::Result>
WizMarkdownExportRecoveryRequest::Work::completedResult() const
{
    if (!m_shared || m_shared->stage.load(std::memory_order_acquire) != Stage::Completed)
        return std::nullopt;
    return m_shared->result;
}
