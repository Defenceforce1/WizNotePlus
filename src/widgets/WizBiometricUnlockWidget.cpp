#include "WizBiometricUnlockWidget.h"
#include <QCoreApplication>
#include <QHideEvent>
#include <QShowEvent>
#include <QGuiApplication>
#include <QDeadlineTimer>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRunnable>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>

namespace {
using namespace WizBiometricKeychain;
enum WorkState { Queued, Running, Complete, Dropped };
class NativeRead final : public WizBiometricUnlockWidget::Operation {
    ReadOperation operation;
public:
    Result read(const Scope& scope) override { return operation.read(scope); }
    void cancel() noexcept override { operation.cancel(); }
};
QString failureText(Error error)
{
    switch (error) {
    case Error::SigningRequired: return QObject::tr("Touch ID 签名未配置");
    case Error::BiometryUnavailable: return QObject::tr("Touch ID 暂不可用");
    case Error::Missing: return QObject::tr("尚未启用 Touch ID");
    case Error::Canceled: return {};
    case Error::CredentialMismatch: return QObject::tr("请使用证书密码");
    case Error::AccessDenied: return QObject::tr("验证未通过，请重试");
    default: return QObject::tr("验证未完成，请重试");
    }
}
}

struct WizBiometricUnlockWidget::Task {
    std::atomic<int> state{Queued};
    std::atomic<bool> canceled{false};
    Context context;
    WizDocumentCallbackEpoch::Ticket ticket;
    std::shared_ptr<Operation> operation;
    WizBiometricKeychain::Result result;
    QDeadlineTimer activeDeliveryDeadline;
};

class WizBiometricUnlockWidget::Job final : public QRunnable {
    std::shared_ptr<Task> task;
public:
    explicit Job(std::shared_ptr<Task> task) : task(std::move(task)) {}
    ~Job() override {
        int expected = Queued;
        task->state.compare_exchange_strong(expected, Dropped);
    }
    void run() override {
        int expected = Queued;
        if (!task->state.compare_exchange_strong(expected, Running)) return;
        Result result{Error::Canceled, {}};
        if (!task->canceled.load()) {
            try {
                result = task->operation->read(task->context.scope);
                // Verify even a supplied provider's success against THIS key.
                if (result && WizCertificateCreation::verify(task->context.scope.certificate, result.password)
                        != WizCertificateCreation::Error::None)
                    result = {Error::CredentialMismatch, {}};
            } catch (...) { result = {Error::Failed, {}}; }
        }
        if (!result || task->canceled.load()) result = {task->canceled.load() ? Error::Canceled : result.error, {}};
        task->result = std::move(result);
        task->activeDeliveryDeadline = QDeadlineTimer(15000);
        task->state.store(Complete, std::memory_order_release);
        // Worker owns only task values/operation, never a QWidget or database.
    }
};

WizBiometricUnlockWidget::WizBiometricUnlockWidget(QWidget* parent)
    : WizBiometricUnlockWidget(Services{}, parent) {}

WizBiometricUnlockWidget::WizBiometricUnlockWidget(Services services, QWidget* parent)
    : QWidget(parent), m_services(std::move(services))
{
    m_nativeProvider = !m_services.makeOperation;
    if(!m_services.biometricAvailable)m_services.biometricAvailable=m_nativeProvider
        ? std::function<bool()>([]{return hasSigningPrerequisites()&&isBiometryAvailable();})
        : std::function<bool()>([]{return true;});
    if (!m_services.makeOperation) m_services.makeOperation = [] { return std::make_shared<NativeRead>(); };
    m_pool = m_services.pool ? m_services.pool : QThreadPool::globalInstance();
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0,0,0,0);layout->setSpacing(8);
    layout->setSizeConstraint(QLayout::SetMinimumSize);
    m_status = new QLabel(this);
    m_status->setObjectName("biometricUnlockStatus");
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto* buttons = new QHBoxLayout;
    m_unlock = new QPushButton(tr("使用 Touch ID"), this);
    m_manual = new QPushButton(tr("使用证书密码"), this);
    m_cancel = new QPushButton(tr("取消"), this);
    m_unlock->setObjectName("biometricUnlockStart");
    m_manual->setObjectName("biometricUnlockManual");
    m_cancel->setObjectName("biometricUnlockCancel");
    for (auto* button : {m_unlock, m_manual, m_cancel}) {
        button->setAutoDefault(false);
        button->setMinimumHeight(30);
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    m_timer = new QTimer(this);
    m_timer->setInterval(20);
    connect(m_timer, &QTimer::timeout, this, &WizBiometricUnlockWidget::poll);
    connect(m_unlock, &QPushButton::clicked, this, &WizBiometricUnlockWidget::start);
    connect(m_cancel, &QPushButton::clicked, this, &WizBiometricUnlockWidget::cancel);
    connect(m_manual, &QPushButton::clicked, this, [this] {
        QPointer<WizBiometricUnlockWidget> guard(this);
        cancel();
        if (guard && !m_closed) emit manualPasswordRequested();
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit,
            this, &WizBiometricUnlockWidget::beginShutdown);
    if(auto* app=qobject_cast<QGuiApplication*>(QCoreApplication::instance()))
        connect(app,&QGuiApplication::applicationStateChanged,this,[this](Qt::ApplicationState state){
            if(state==Qt::ApplicationActive)scheduleAutomaticStart();
        });
    updateControls();
}

WizBiometricUnlockWidget::~WizBiometricUnlockWidget() { m_closed = true; cancelWork(); }

bool WizBiometricUnlockWidget::valid(const Context& context)
{
    return context.openSequence != 0 && !context.documentGuid.trimmed().isEmpty()
        && context.documentGuid.size() <= 256 && !context.documentGuid.contains(QChar::Null)
        && QString::fromUtf8(context.documentGuid.toUtf8()) == context.documentGuid
        && !WizBiometricKeychain::itemIdentity(context.scope).isEmpty();
}
bool WizBiometricUnlockWidget::matches(const Context& context) const
{
    return valid(context) && context.openSequence == m_context.openSequence
        && context.documentGuid == m_context.documentGuid
        && WizBiometricKeychain::itemIdentity(context.scope) == WizBiometricKeychain::itemIdentity(m_context.scope);
}

void WizBiometricUnlockWidget::cancelWork()
{
    m_automaticPending=false;
    m_epoch.invalidate();
    m_secret.clear();
    m_timer->stop();
    const auto task = std::move(m_task);
    if (task) {
        task->canceled.store(true);
        if (task->operation) task->operation->cancel();
    }
    // Clearing QString is not a secure-memory-erasure claim.
}
void WizBiometricUnlockWidget::setContext(const Context& context, bool enabled)
{
    if (m_closed) return;
    QPointer<WizBiometricUnlockWidget> guard(this);
    cancelWork(); if (!guard || m_closed) return;
    m_context = context;
    const bool remembered=enabled&&valid(context);
    m_eligible=remembered;
    const auto ticket=m_epoch.ticket();
    const bool available=!remembered||currentAvailability();
    if(!guard||!WizDocumentCallbackEpoch::current(ticket))return;
    m_enabled = remembered && available;
    m_error = remembered&&!available?Error::BiometryUnavailable:Error::None;
    m_stage = m_enabled ? Stage::Ready : Stage::Unavailable;
    m_automaticPending=m_automaticStart&&remembered;
    updateControls();
    if (!guard) return;
    emit stageChanged();
    if (guard) scheduleAutomaticStart();
}

bool WizBiometricUnlockWidget::currentAvailability() const
{
    const QPointer<const WizBiometricUnlockWidget> self(this);
    const auto ticket=m_epoch.ticket();const auto query=m_services.biometricAvailable;
    bool available=false;try{available=query&&query();}catch(...){}
    return self&&WizDocumentCallbackEpoch::current(ticket)&&available;
}
void WizBiometricUnlockWidget::usePasswordFallback()
{
    m_enabled=false;m_error=Error::BiometryUnavailable;m_stage=Stage::Unavailable;
    const QPointer<WizBiometricUnlockWidget> self(this);
    updateControls();if(!self)return;emit stageChanged();
    if(self&&(!m_nativeProvider||QGuiApplication::applicationState()==Qt::ApplicationActive))
        emit manualPasswordRequested();
}

void WizBiometricUnlockWidget::setAutomaticStart(bool enabled)
{
    m_automaticStart=enabled;
    if(!enabled)m_automaticPending=false;
}
void WizBiometricUnlockWidget::setPresentationVisible(bool visible)
{
    m_capabilityHidden=!visible&&m_eligible&&m_stage==Stage::Unavailable;
    const QPointer<WizBiometricUnlockWidget> self(this);
    m_presentationChange=true;
    setVisible(visible);
    if(self)m_presentationChange=false;
}
void WizBiometricUnlockWidget::scheduleAutomaticStart()
{
    // WindowActivate/show can arrive before Qt updates activeWindow(). Defer
    // that check until the queued turn, otherwise re-shown windows miss it.
    const bool hostVisible=isVisible()||(m_capabilityHidden&&parentWidget()&&parentWidget()->isVisible());
    if((!m_automaticPending&&!(m_eligible&&m_stage==Stage::Unavailable))||m_closed||!hostVisible||!isEnabled())return;
    if(m_nativeProvider&&QGuiApplication::applicationState()!=Qt::ApplicationActive)return;
    const auto ticket=m_epoch.ticket();
    QTimer::singleShot(0,this,[this,ticket]{
        const bool hostVisible=isVisible()||(m_capabilityHidden&&parentWidget()&&parentWidget()->isVisible());
        if(!WizDocumentCallbackEpoch::current(ticket)||m_closed
            ||!hostVisible||!isEnabled()||!window()->isActiveWindow())return;
        if(m_nativeProvider&&QGuiApplication::applicationState()!=Qt::ApplicationActive)return;
        if(m_eligible&&m_stage==Stage::Unavailable){
            const QPointer<WizBiometricUnlockWidget> self(this);
            const bool available=currentAvailability();
            if(!self||!WizDocumentCallbackEpoch::current(ticket)||!available)return;
            m_enabled=true;m_stage=Stage::Ready;m_error=Error::None;
            updateControls();if(!self||!WizDocumentCallbackEpoch::current(ticket))return;
            emit stageChanged();if(!self||!WizDocumentCallbackEpoch::current(ticket))return;
        }
        if(!m_automaticPending||!isVisible()||m_stage!=Stage::Ready)return;
        start();
    });
}
bool WizBiometricUnlockWidget::event(QEvent* event)
{
    const QPointer<WizBiometricUnlockWidget> self(this);
    const bool result=QWidget::event(event);
    if(self&&event->type()==QEvent::WindowActivate)scheduleAutomaticStart();
    return result;
}
void WizBiometricUnlockWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    scheduleAutomaticStart();
}

void WizBiometricUnlockWidget::start()
{
    if (m_closed || m_stage != Stage::Ready || !m_enabled || !isVisible() || !m_pool) return;
    const QPointer<WizBiometricUnlockWidget> availableOwner(this);const auto availableTicket=m_epoch.ticket();
    const bool available=currentAvailability();
    if(!availableOwner||!WizDocumentCallbackEpoch::current(availableTicket))return;
    if(!available){usePasswordFallback();return;}
    m_automaticPending=false;
    auto task = std::make_shared<Task>();
    task->context = m_context;
    task->ticket = m_epoch.ticket();
    const auto factory = m_services.makeOperation;
    QPointer<WizBiometricUnlockWidget> guard(this);
    m_task = task;
    m_error = Error::None;
    m_stage = Stage::Working;
    updateControls(); if (!guard || m_task != task || m_closed) return;
    emit stageChanged(); if (!guard || m_task != task || m_closed) return;
    try { task->operation = factory(); } catch (...) {}
    if (!guard) return;
    if (m_closed || m_task != task || !WizDocumentCallbackEpoch::current(task->ticket) || !matches(task->context)) {
        if (task->operation) task->operation->cancel();
        return;
    }
    if (!task->operation) {
        m_task.reset();
        m_error = Error::Failed;
        m_stage = Stage::Ready;
        updateControls();
        if (guard) emit stageChanged();
        return;
    }
    if (!m_pool) { cancel(); return; }
    m_timer->start();
    m_pool->start(new Job(task));
}

void WizBiometricUnlockWidget::poll()
{
    const auto task = m_task;
    if (!task) return;
    const int state = task->state.load(std::memory_order_acquire);
    if (state != Complete && state != Dropped) return;
    // A system authentication panel can temporarily deactivate Cocoa. Keep
    // successful native credentials inside the original task until the app is
    // active again; never deliver/grant body access while another app is active.
    if (state == Complete && m_nativeProvider && task->result && !task->canceled.load()
        && QGuiApplication::applicationState() != Qt::ApplicationActive
        && isVisible() && WizDocumentCallbackEpoch::current(task->ticket) && matches(task->context)) {
        if (!task->activeDeliveryDeadline.hasExpired()) return;
        task->result = {Error::Canceled, {}}; // Bounded wait, never a background grant.
    }
    m_task.reset();
    m_timer->stop();
    if (m_closed || task->canceled.load() || !isVisible()
        || !WizDocumentCallbackEpoch::current(task->ticket) || !matches(task->context)) return;
    auto result = state == Complete ? std::move(task->result) : Result{Error::Canceled, {}};
    m_error = result.error;
    if(result.error==Error::BiometryUnavailable){usePasswordFallback();return;}
    m_secret = result ? std::move(result.password) : QString();
    m_stage = result ? Stage::Verified : Stage::Ready;
    QPointer<WizBiometricUnlockWidget> guard(this);
    updateControls(); if (!guard) return;
    emit stageChanged();
    if (guard && m_stage == Stage::Verified && WizDocumentCallbackEpoch::current(task->ticket)
        && matches(task->context)) emit credentialReady();
}

bool WizBiometricUnlockWidget::preserveNativeAuthenticationOnInactive() const
{
    return m_nativeProvider && m_stage == Stage::Working && bool(m_task)
        && isVisible() && window()->isVisible() && !window()->isMinimized()
        && QGuiApplication::applicationState() == Qt::ApplicationInactive;
}

WizBiometricKeychain::Result WizBiometricUnlockWidget::takeCredential(const Context& context)
{
    if (m_closed || m_stage != Stage::Verified || !isVisible() || !matches(context))
        return {Error::Canceled, {}};
    const auto ticket = m_epoch.ticket();
    QString secret = std::move(m_secret);
    m_secret.clear();
    m_stage = Stage::Consumed;
    QPointer<WizBiometricUnlockWidget> guard(this);
    updateControls();
    if (guard) emit stageChanged();
    if (!guard || !WizDocumentCallbackEpoch::current(ticket)
        || m_stage != Stage::Consumed || !matches(context)) return {Error::Canceled, {}};
    return {Error::None, std::move(secret)};
}
bool WizBiometricUnlockWidget::authorizedFor(const Context& context) const
{
    return !m_closed && isVisible() && m_enabled && m_stage == Stage::Consumed && matches(context);
}

void WizBiometricUnlockWidget::cancel()
{
    if (m_closed) return;
    QPointer<WizBiometricUnlockWidget> guard(this);
    cancelWork(); if (!guard || m_closed) return;
    m_error = Error::Canceled;
    m_stage = m_enabled ? Stage::Ready : Stage::Unavailable;
    updateControls();
    if (guard) emit stageChanged();
}
void WizBiometricUnlockWidget::systemLocked() { cancel(); }
void WizBiometricUnlockWidget::beginShutdown()
{
    if (m_closed) return;
    m_closed = true;
    m_stage = Stage::Closed;
    m_enabled = false;
    m_eligible=false;
    m_context = {};
    QPointer<WizBiometricUnlockWidget> guard(this);
    cancelWork(); if (!guard) return;
    updateControls();
    if (guard) emit stageChanged();
}
void WizBiometricUnlockWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if(!m_presentationChange)cancel();
}

void WizBiometricUnlockWidget::updateControls()
{
    QPointer<WizBiometricUnlockWidget> guard(this);
    m_unlock->setEnabled(m_enabled && m_stage == Stage::Ready); if (!guard) return;
    m_manual->setEnabled(!m_closed); if (!guard) return;
    m_cancel->setEnabled(m_stage == Stage::Working || m_stage == Stage::Verified || m_stage == Stage::Consumed);
    if (!guard) return;
    m_cancel->setVisible(m_stage==Stage::Working);if(!guard)return;
    QString status;
    switch (m_stage) {
    case Stage::Unavailable: break;
    case Stage::Working: status = tr("正在验证…"); break;
    case Stage::Verified: break;
    case Stage::Consumed: break;
    case Stage::Closed: break;
    case Stage::Ready: status = m_error == Error::None
        ? QString() : failureText(m_error); break;
    }
    m_status->setText(status);
    if(guard)m_status->setVisible(!status.isEmpty());
}
