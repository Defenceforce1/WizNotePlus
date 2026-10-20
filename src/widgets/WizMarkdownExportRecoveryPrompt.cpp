#include "WizMarkdownExportRecoveryPrompt.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QVBoxLayout>

WizMarkdownExportRecoveryPrompt::WizMarkdownExportRecoveryPrompt(
    const WizMarkdownExportRecovery::Inspection& inspection,
    WizMarkdownExportRecovery::LockState lock, QWidget* parent)
    : QDialog(parent), m_detailText(inspection.diagnosticDetail)
{
    using namespace WizMarkdownExportRecovery;
    setObjectName(QStringLiteral("recoveryPrompt"));
    setWindowTitle(tr("Markdown 导出恢复"));
    setAccessibleName(windowTitle());
    setAccessibleDescription(tr("默认保留现场。关闭、取消或按 Escape 不会恢复文件。"));
    setWindowModality(Qt::WindowModal);

    const bool known = inspection.availability == Availability::None
        || inspection.availability == Availability::AwaitingDecision
        || inspection.availability == Availability::Busy;
    m_confirmAllowed = known && ((inspection.availability == Availability::AwaitingDecision
            && lock == LockState::Available) || lock == LockState::ConfirmedDead);
    QString status;
    if (inspection.availability == Availability::Refused || !known)
        status = tr("无法安全识别此现场。将保留文件，不自动恢复。亦可查看只读详情。");
    else if (lock == LockState::ConfirmedDead)
        status = tr("目标留有已结束进程的写锁。明确确认后仍须重新核对文件，未知状态将保留。");
    else if (lock == LockState::Active)
        status = tr("目标正由活动进程使用。当前不能请求恢复，将保留现场。");
    else if (lock != LockState::Available)
        status = tr("无法确认目标锁状态。当前不能请求恢复，将保留现场。");
    else if (inspection.availability == Availability::AwaitingDecision)
        status = tr("可请求检查；仅在明确确认后尝试恢复，不保证恢复成功。");
    else if (inspection.availability == Availability::Busy)
        status = tr("目标状态已发生变化，须重新检查；当前保留现场。");
    else
        status = tr("未发现可恢复的现场。当前不执行恢复。");

    auto* layout = new QVBoxLayout(this);
    auto* summary = new QLabel(tr("检测到上次 Markdown 导出未完整结束。默认将保留现场，不会覆盖现有文件。"), this);
    summary->setObjectName(QStringLiteral("recoveryPromptSummary"));
    summary->setTextFormat(Qt::PlainText);
    summary->setWordWrap(true);
    summary->setAccessibleName(summary->text());
    layout->addWidget(summary);
    auto* state = new QLabel(status, this);
    state->setObjectName(QStringLiteral("recoveryPromptStatus"));
    state->setTextFormat(Qt::PlainText);
    state->setWordWrap(true);
    state->setAccessibleName(status);
    layout->addWidget(state);

    m_details = new QPlainTextEdit(this);
    m_details->setObjectName(QStringLiteral("recoveryPromptDetails"));
    m_details->setReadOnly(true);
    m_details->setAccessibleName(tr("恢复详情"));
    m_details->setAccessibleDescription(tr("仅在主动查看详情后显示完整路径和原因，不修改文件。"));
    m_details->setMinimumHeight(150);
    m_details->setMaximumHeight(300);
    m_details->hide(); // Also empty: hidden accessibility text contains no paths.
    layout->addWidget(m_details);

    auto* buttons = new QHBoxLayout;
    m_detailButton = new QPushButton(tr("查看详情"), this);
    m_detailButton->setObjectName(QStringLiteral("recoveryPromptShowDetails"));
    m_detailButton->setAccessibleName(m_detailButton->text());
    m_detailButton->setAccessibleDescription(tr("显示只读路径和原因，不执行恢复。"));
    m_detailButton->setEnabled(!m_detailText.isEmpty());
    m_detailButton->setAutoDefault(false);
    buttons->addWidget(m_detailButton);
    buttons->addStretch();
    m_retain = new QPushButton(tr("保留现场"), this);
    m_retain->setObjectName(QStringLiteral("recoveryPromptRetain"));
    m_retain->setAccessibleName(m_retain->text());
    m_retain->setAccessibleDescription(tr("默认操作。Enter、Escape 或关闭均保留现场，不修改文件。"));
    m_retain->setAutoDefault(false);
    m_retain->setDefault(true);
    buttons->addWidget(m_retain);
    m_confirm = new QPushButton(tr("检查并恢复"), this);
    m_confirm->setObjectName(QStringLiteral("recoveryPromptConfirm"));
    m_confirm->setAccessibleName(m_confirm->text());
    m_confirm->setAccessibleDescription(tr("提交明确确认；之后仍须重新核对现场，不保证恢复成功。"));
    m_confirm->setAutoDefault(false);
    m_confirm->setEnabled(m_confirmAllowed);
    buttons->addWidget(m_confirm);
    layout->addLayout(buttons);
    setTabOrder(m_retain, m_detailButton);
    setTabOrder(m_detailButton, m_confirm);
    setTabOrder(m_confirm, m_details);
    resize(560, sizeHint().height());

    connect(m_detailButton, &QPushButton::clicked, this, &WizMarkdownExportRecoveryPrompt::toggleDetails);
    connect(m_retain, &QPushButton::clicked, this, [this] { settle(Decision::Retain); });
    connect(m_confirm, &QPushButton::clicked, this, [this] { settle(Decision::Confirm); });
}

void WizMarkdownExportRecoveryPrompt::showEvent(QShowEvent* event)
{
    QPointer<WizMarkdownExportRecoveryPrompt> self(this);
    QDialog::showEvent(event);
    if (self && !m_settled)
        m_retain->setFocus(Qt::OtherFocusReason);
}

void WizMarkdownExportRecoveryPrompt::settle(WizMarkdownExportRecovery::Decision decision)
{
    using namespace WizMarkdownExportRecovery;
    if (m_settled)
        return;
    if (decision == Decision::Confirm
        && (!isVisible() || !m_confirmAllowed || !m_confirm->isEnabled() || m_exitRequested))
        return;
    m_settled = true;
    m_decision = decision;
    QPointer<WizMarkdownExportRecoveryPrompt> self(this);
    for (QPushButton* button : {m_retain, m_detailButton, m_confirm}) {
        button->setEnabled(false);
        if (!self)
            return;
    }
    // Hide/focus observers may request exit before the decision is published.
    hide();
    if (!self)
        return;
    if (m_exitRequested)
        m_decision = Decision::Retain;
    m_published = true;
    // finished handlers may destroy the owner. Do not access members afterwards.
    QDialog::done(m_decision == Decision::Confirm ? QDialog::Accepted : QDialog::Rejected);
}

void WizMarkdownExportRecoveryPrompt::retainForExit()
{
    m_exitRequested = true;
    if (m_settled) {
        if (!m_published)
            m_decision = WizMarkdownExportRecovery::Decision::Retain;
        return;
    }
    settle(WizMarkdownExportRecovery::Decision::Retain);
}

void WizMarkdownExportRecoveryPrompt::accept() { settle(WizMarkdownExportRecovery::Decision::Retain); }
void WizMarkdownExportRecoveryPrompt::reject() { settle(WizMarkdownExportRecovery::Decision::Retain); }
void WizMarkdownExportRecoveryPrompt::done(int result)
{
    Q_UNUSED(result);
    settle(WizMarkdownExportRecovery::Decision::Retain);
}

void WizMarkdownExportRecoveryPrompt::toggleDetails()
{
    if (m_settled)
        return;
    const bool reveal = !m_details->isVisible();
    const QString text = reveal ? m_detailText : QString();
    QPointer<WizMarkdownExportRecoveryPrompt> self(this);
    // This is a private read-only view, not an editable value. Do not expose
    // textChanged while Qt is still updating its text control: an observer
    // deleting the owner inside that signal would invalidate Qt's own stack.
    QSignalBlocker textSignals(m_details);
    m_details->setPlainText(text);
    if (!self) {
        textSignals.dismiss();
        return;
    }
    textSignals.unblock();
    m_details->setVisible(reveal);
    if (!self)
        return;
    m_detailButton->setText(reveal ? tr("隐藏详情") : tr("查看详情"));
    if (!self)
        return;
    m_detailButton->setAccessibleName(m_detailButton->text());
    if (!self)
        return;
    adjustSize();
}
