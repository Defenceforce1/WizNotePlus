#include "WizBiometricManageWidget.h"
#include "share/WizBiometricNoteAccess.h"
#include <QCheckBox>
#include <QCoreApplication>
#include <QHideEvent>
#include <QLabel>
#include <QFrame>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QRunnable>
#include <QSignalBlocker>
#include <QThreadPool>
#include <QTimer>
#include <QVBoxLayout>
#include <atomic>
namespace {
using namespace WizBiometricKeychain;
class NativeManage final : public WizBiometricManageWidget::Operation {
public:
    Error enroll(const Scope& s,const QString& p) override{return WizBiometricKeychain::enroll(s,p,true);}
    Error forget(const Scope& s) override{return WizBiometricKeychain::forget(s,true);}
};
}
struct WizBiometricManageWidget::Task {
    Scope scope;
    QString password;
    bool forget=false;
    WizDocumentCallbackEpoch::Ticket ticket;
    std::shared_ptr<Operation> operation;
    bool dispatched=false; // GUI-only; distinguishes pre-dispatch reentry.
    Error result=Error::Failed;
    std::atomic<bool> retired{false};
    std::atomic<int> state{0}; // queued, running, complete, pool-dropped
};
class WizBiometricManageWidget::Job final : public QRunnable {
    std::shared_ptr<Task> task;
public:
    explicit Job(std::shared_ptr<Task> value):task(std::move(value)){}
    ~Job()override{int expected=0;task->state.compare_exchange_strong(expected,3);}
    void run()override{
        int expected=0;if(!task->state.compare_exchange_strong(expected,1))return;
        try {
            if(task->retired.load()) task->result=Error::Canceled;
            else if(task->forget) task->result=task->operation->forget(task->scope);
            else {
                task->result=validateEnrollment(task->scope,task->password,true);
                if(task->result==Error::None&&!task->retired.load())
                    task->result=task->operation->enroll(task->scope,task->password);
            }
        }catch(...){task->result=Error::Failed;}
        task->password.clear();
        task->state.store(2,std::memory_order_release);
    }
};
WizBiometricManageWidget::WizBiometricManageWidget(QWidget* parent):WizBiometricManageWidget(Services{},parent){}
WizBiometricManageWidget::WizBiometricManageWidget(Services services,QWidget* parent)
    :QWidget(parent),m_services(std::move(services))
{
    if(!m_services.makeOperation)m_services.makeOperation=[]{return std::make_shared<NativeManage>();};
    m_pool=m_services.pool?m_services.pool:QThreadPool::globalInstance();
    auto* outer=new QVBoxLayout(this);outer->setContentsMargins(0,0,0,0);
    auto* card=new QFrame(this);card->setObjectName("biometricSettingsCard");card->setMaximumWidth(560);
    card->setStyleSheet("QFrame#biometricSettingsCard {background:palette(base);border:1px solid palette(mid);border-radius:8px;}");
    outer->addWidget(card,0,Qt::AlignHCenter);outer->addStretch();
    auto* layout=new QVBoxLayout(card);layout->setContentsMargins(24,24,24,24);layout->setSpacing(12);
    auto* title=new QLabel(tr("Touch ID 解锁"),card);auto font=title->font();font.setPointSize(16);font.setBold(true);title->setFont(font);layout->addWidget(title);
    m_password=new QLineEdit(this);m_password->setEchoMode(QLineEdit::Password);m_password->setObjectName("biometricManagePassword");layout->addWidget(m_password);
    m_password->setMinimumHeight(32);m_password->setPlaceholderText(tr("请输入证书密码"));
    layout->removeWidget(m_password);auto* fields=new QFormLayout;fields->addRow(tr("证书密码"),m_password);layout->addLayout(fields);
    m_consent=new QCheckBox(tr("允许管理本机解锁记录"),this);m_consent->setObjectName("biometricManageConsent");layout->addWidget(m_consent);
    auto* buttons=new QHBoxLayout;
    m_enroll=new QPushButton(tr("启用 Touch ID"),this);m_enroll->setObjectName("biometricManageEnroll");buttons->addWidget(m_enroll);
    m_forget=new QPushButton(tr("移除本机记录"),this);m_forget->setObjectName("biometricManageForget");buttons->addWidget(m_forget);layout->addLayout(buttons);
    for(auto* button:{m_enroll,m_forget}){button->setAutoDefault(false);button->setMinimumHeight(32);}
    m_status=new QLabel(this);m_status->setObjectName("biometricManageStatus");m_status->setTextFormat(Qt::PlainText);m_status->setWordWrap(true);layout->addWidget(m_status);
    m_timer=new QTimer(this);m_timer->setInterval(20);
    connect(m_timer,&QTimer::timeout,this,&WizBiometricManageWidget::poll);
    connect(m_enroll,&QPushButton::clicked,this,[this]{start(false);});
    connect(m_forget,&QPushButton::clicked,this,[this]{start(true);});
    connect(m_password,&QLineEdit::textChanged,this,[this]{update();});
    connect(m_consent,&QCheckBox::toggled,this,[this]{update();});
    connect(QCoreApplication::instance(),&QCoreApplication::aboutToQuit,this,&WizBiometricManageWidget::beginShutdown);
    update();
}
WizBiometricManageWidget::~WizBiometricManageWidget(){if(m_task)m_task->retired=true;}
void WizBiometricManageWidget::retire()
{
    const QPointer<WizBiometricManageWidget> self(this);
    m_epoch.invalidate();if(m_task){m_task->retired=true;if(!m_task->dispatched)m_task.reset();}
    // Keep observing an outstanding native mutation without adopting its
    // result, so this widget cannot overlap it with another manage operation.
    if(!m_task)m_timer->stop();
    // Internal secret removal is not a user edit. In particular do not invoke
    // application textChanged slots inside QLineEdit's unfinished clear stack.
    {const QSignalBlocker blocker(m_password);m_password->clear();}if(!self)return;
    m_consent->setChecked(false);if(!self)return;m_error=Error::None;
    m_stage=m_closed?Stage::Closed:(m_available?Stage::Ready:Stage::Disabled);update();
}
void WizBiometricManageWidget::beginShutdown(){m_closed=true;retire();}
void WizBiometricManageWidget::hideEvent(QHideEvent* event){const QPointer<WizBiometricManageWidget> self(this);retire();if(self)QWidget::hideEvent(event);}
void WizBiometricManageWidget::setContext(const Scope& scope,bool enabled)
{
    if(m_closed)return;const QPointer<WizBiometricManageWidget> self(this);retire();if(!self)return;m_scope=scope;
    m_available=enabled&&!itemIdentity(scope).isEmpty()&&bool(m_services.contextCurrent)&&bool(m_services.enablePolicy);
    m_stage=m_available?Stage::Ready:Stage::Disabled;update();
}
void WizBiometricManageWidget::start(bool forget)
{
    if(m_closed||!m_available||m_task||!isVisible()||!m_consent->isChecked()||!m_pool)return;
    const QPointer<WizBiometricManageWidget> self(this);const auto ticket=m_epoch.ticket();const auto scope=m_scope;
    const auto isCurrent=m_services.contextCurrent;
    if(!isCurrent(scope)||!self||!WizDocumentCallbackEpoch::current(ticket))return;
    auto task=std::make_shared<Task>();task->scope=scope;task->forget=forget;task->ticket=ticket;task->password=forget?QString():m_password->text();
    {const QSignalBlocker blocker(m_password);m_password->clear();}if(!self||!WizDocumentCallbackEpoch::current(ticket))return;
    m_consent->setChecked(false);if(!self||!WizDocumentCallbackEpoch::current(ticket))return;
    m_task=task;m_stage=Stage::Working;m_error=Error::None;update();
    if(!self||m_closed||m_task!=task||!WizDocumentCallbackEpoch::current(ticket))return;
    emit scopeAccessRetired(WizBiometricNoteAccess::policyIdentity(scope));
    if(!self||m_closed||m_task!=task||!WizDocumentCallbackEpoch::current(ticket))return;
    const auto factory=m_services.makeOperation;
    try{task->operation=factory();}catch(...){}
    if(!self)return;
    if(m_closed||m_task!=task||!WizDocumentCallbackEpoch::current(ticket))return;
    if(!task->operation||!m_pool){m_task.reset();retire();if(!self)return;m_error=Error::Failed;m_stage=Stage::Failed;update();return;}
    task->dispatched=true;m_timer->start();m_pool->start(new Job(task));
}
void WizBiometricManageWidget::poll()
{
    const auto task=m_task;if(!task)return;
    const int state=task->state.load(std::memory_order_acquire);if(state<2)return;
    m_timer->stop();m_task.reset();
    if(task->retired.load()){update();return;}
    const QPointer<WizBiometricManageWidget> self(this);const auto current=m_services.contextCurrent;
    if(m_closed||!WizDocumentCallbackEpoch::current(task->ticket)||!current)return;
    const bool matches=current(task->scope);
    if(!self||!WizDocumentCallbackEpoch::current(task->ticket))return;
    if(!matches){m_available=false;m_stage=Stage::Disabled;update();return;}
    m_error=state==3?Error::Failed:task->result;
    const bool forgotten=task->forget&&(m_error==Error::None||m_error==Error::Missing);
    if(!task->forget&&m_error==Error::None){
        const auto enable=m_services.enablePolicy;
        const bool enabled=enable(task->scope);
        if(!self||!WizDocumentCallbackEpoch::current(task->ticket))return;
        if(!enabled)m_error=Error::Failed;
    }
    m_stage=forgotten||m_error==Error::None?Stage::Succeeded:(m_error==Error::AlreadyExists?Stage::Conflict:Stage::Failed);update();
    // Forgetting intentionally retains required fresh authorization policy.
    if(!self||!WizDocumentCallbackEpoch::current(task->ticket))return;
    if(forgotten||m_error==Error::None){
        emit scopeAccessRetired(WizBiometricNoteAccess::policyIdentity(task->scope));
        if(self&&WizDocumentCallbackEpoch::current(task->ticket))emit accessRetired();
    }
}
void WizBiometricManageWidget::update()
{
    const bool idle=m_available&&!m_closed&&!m_task&&m_stage!=Stage::Working;
    const QPointer<WizBiometricManageWidget> self(this);
    m_enroll->setEnabled(idle&&m_consent->isChecked()&&!m_password->text().isEmpty());if(!self)return;m_forget->setEnabled(idle&&m_consent->isChecked());if(!self)return;
    m_password->setEnabled(idle);if(!self)return;m_consent->setEnabled(idle);if(!self)return;
    QString text=tr("暂不可用");
    if(m_stage==Stage::Ready||m_stage==Stage::Closed)text.clear();
    if(m_stage==Stage::Working)text=tr("正在处理…");
    if(m_stage==Stage::Conflict)text=tr("已有记录，请先移除再启用。");
    if(m_stage==Stage::Succeeded)text=tr("已完成");
    if(m_stage==Stage::Failed)text=tr("操作未完成，请重试");
    if(m_stage==Stage::Failed&&m_error==Error::PasswordInvalid)text=tr("证书密码不正确");
    if(m_stage==Stage::Failed&&m_error==Error::BiometryUnavailable)text=tr("Touch ID 暂不可用");
    if(m_stage==Stage::Failed&&m_error==Error::SigningRequired)text=tr("Touch ID 签名未配置");
    m_status->setText(text);
    if(self)m_status->setVisible(!text.isEmpty());
}
