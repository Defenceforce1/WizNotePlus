#pragma once
#include "mac/WizBiometricKeychain.h"
#include "share/WizDocumentAsync.h"
#include <QWidget>
#include <QPointer>
#include <functional>
class QCheckBox;
class QLineEdit;
class QLabel;
class QPushButton;
class QTimer;
class QThreadPool;

// Default-off management component. No normal-client entry until lawful signing
// and host policy-change revocation integration are accepted. No automatic read.
class WizBiometricManageWidget final : public QWidget {
    Q_OBJECT
public:
    using Scope=WizBiometricKeychain::Scope;
    using Error=WizBiometricKeychain::Error;
    enum class Stage { Disabled, Ready, Working, Succeeded, Conflict, Failed, Closed };
    class Operation {
    public:
        virtual ~Operation()=default;
        virtual Error enroll(const Scope&,const QString&)=0;
        virtual Error forget(const Scope&)=0;
    };
    struct Services {
        std::function<std::shared_ptr<Operation>()> makeOperation;
        // Must recheck current account/certificate. Policy enablement follows
        // native success; forgetting never disables fresh-access authorization.
        std::function<bool(const Scope&)> contextCurrent;
        std::function<bool(const Scope&)> enablePolicy;
        QThreadPool* pool=nullptr;
    };
    explicit WizBiometricManageWidget(QWidget* parent=nullptr);
    explicit WizBiometricManageWidget(Services services,QWidget* parent=nullptr);
    ~WizBiometricManageWidget() override;
    void setContext(const Scope&,bool explicitlyAvailable);
    Stage stage()const{return m_stage;}
    Error error()const{return m_error;}
public Q_SLOTS:
    void retire();
    void beginShutdown();
Q_SIGNALS:
    void accessRetired(); // Host must revoke open-note grants, never save cache.
protected:
    void hideEvent(QHideEvent*) override;
private:
    struct Task;
    class Job;
    void start(bool forget);
    void poll();
    void update();
    Services m_services;
    QPointer<QThreadPool> m_pool;
    Scope m_scope;
    bool m_available=false,m_closed=false;
    Stage m_stage=Stage::Disabled;
    Error m_error=Error::None;
    WizDocumentCallbackEpoch m_epoch;
    std::shared_ptr<Task> m_task;
    QCheckBox* m_consent;
    QLineEdit* m_password;
    QLabel* m_status;
    QPushButton *m_enroll,*m_forget;
    QTimer* m_timer;
};
