#include "WizBiometricManageHost.h"
#include "WizDef.h"
#include "share/WizGlobal.h"
#include "share/WizBiometricNoteAccess.h"
#include "share/WizSettings.h"
#include "database/WizDatabaseManager.h"
#include "sync/WizApiEntry.h"
#include <QVBoxLayout>
#include <mutex>

namespace {
using namespace WizBiometricKeychain;
// A second settings/management host must not delete a record while another
// host is enrolling it. Fail rather than queue an obsolete management intent.
std::mutex managementMutation;
class SerializedManage final : public WizBiometricManageWidget::Operation {
    std::shared_ptr<WizBiometricManageWidget::Operation> operation;
public:
    explicit SerializedManage(std::shared_ptr<WizBiometricManageWidget::Operation> value):operation(std::move(value)){}
    Error enroll(const Scope& scope,const QString& password)override{
        const std::unique_lock<std::mutex> lock(managementMutation,std::try_to_lock);
        if(!lock.owns_lock())return Error::Failed;
        return operation?operation->enroll(scope,password):WizBiometricKeychain::enroll(scope,password,true);
    }
    Error forget(const Scope& scope)override{
        const std::unique_lock<std::mutex> lock(managementMutation,std::try_to_lock);
        if(!lock.owns_lock())return Error::Failed;
        return operation?operation->forget(scope):WizBiometricKeychain::forget(scope,true);
    }
};
}

WizBiometricManageHost::WizBiometricManageHost(WizExplorerApp& app,QWidget* parent)
    :WizBiometricManageHost(app,Services{WizBiometricKeychain::hasSigningPrerequisites(),{},nullptr},parent){}
WizBiometricManageHost::WizBiometricManageHost(WizExplorerApp& app,Services services,QWidget* parent)
    :QWidget(parent)
{
    using namespace WizBiometricNoteAccess;
    using namespace WizBiometricKeychain;
    const QPointer<WizBiometricManageHost> self(this);
    const auto snapshotCurrent=[&app]{auto& db=app.databaseManager().db();return snapshot(db,db,WizCommonApiEntry::server());};
    const auto current=[self,snapshotCurrent](const Scope& scope){
        return self&&!itemIdentity(scope).isEmpty()&&itemIdentity(scope)==itemIdentity(snapshotCurrent());
    };
    WizBiometricManageWidget::Services widget;
    widget.makeOperation=[factory=std::move(services.makeOperation)]()->std::shared_ptr<WizBiometricManageWidget::Operation>{
        auto operation=factory?factory():nullptr;
        if(factory&&!operation)return {};
        return std::make_shared<SerializedManage>(std::move(operation));
    };widget.pool=services.pool;
    widget.contextCurrent=current;
    widget.enablePolicy=[self,&app,current](const Scope& scope){
        if(!self||!current(scope))return false;
        const auto id=policyIdentity(scope);if(id.isEmpty())return false;
        app.userSettings().set("BiometricUnlock",id,"1");
        return self&&current(scope)&&optedIn(app.userSettings(),scope);
    };
    m_management=new WizBiometricManageWidget(std::move(widget),this);
    auto* layout=new QVBoxLayout(this);layout->addWidget(m_management);
    connect(m_management,&WizBiometricManageWidget::scopeAccessRetired,this,[](const QString& id){
        if(auto* events=PolicyEvents::applicationEvents())events->retire(id);
    });
    // Presentation performs only the self-signature prerequisite check above.
    // No authentication, Keychain credential calls or policy writes here; an
    // existing valid certificate and explicit checkbox/button action are needed.
    m_management->setContext(snapshotCurrent(),services.explicitlyAvailable);
}
