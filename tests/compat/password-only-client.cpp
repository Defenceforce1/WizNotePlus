#include "WizUserCipherForm.h"
#include "mac/WizBiometricKeychain.h"
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
#include <QTabWidget>
#include "WizPreferenceDialog.h"
#include "database/WizDatabaseManager.h"
#include "share/WizSettings.h"
#include <QTemporaryDir>
#include <stdexcept>

namespace {
class PasswordOnlyApp final : public WizExplorerApp {
public:
    WizDatabaseManager& manager;WizUserSettings& settings;QWidget window;
    PasswordOnlyApp(WizDatabaseManager& manager,WizUserSettings& settings):manager(manager),settings(settings){}
    QWidget* mainWindow() override{return &window;}
    QObject* object() override{return &window;}
    WizDatabaseManager& databaseManager() override{return manager;}
    WizUserSettings& userSettings() override{return settings;}
    WizCategoryBaseView& category() override{throw std::logic_error("no category");}
};
}
void wizPasswordOnlyClientSelfTest(const std::function<void(bool,const char*)>& check)
{
    using namespace WizBiometricKeychain;
    // The caller has already installed a temporary data root and app settings.
    WizDatabaseManager manager("synthetic-password-only-ui");
    const bool opened=manager.open();check(opened,"password-only fixture opens only its synthetic database");if(!opened)return;
    WizUserSettings settings(manager.db());PasswordOnlyApp app(manager,settings);
    settings.set("BiometricUnlock","synthetic-old-record","1");
    WizUserCipherForm form(app,nullptr);form.show();QApplication::processEvents();
    auto* input=form.findChild<QLineEdit*>("editUserCipher");auto* unlock=form.findChild<QPushButton*>("buttonOk");
    check(input&&unlock&&input->echoMode()==QLineEdit::Password,"production password-only form retains masked input and Unlock button");
    check(!form.findChild<WizBiometricUnlockWidget*>()&&!form.findChild<QPushButton*>("biometricUnlockStart"),"production form does not construct a Touch ID widget or button");
    form.setHint(QStringLiteral("合成证书提示"));auto* hint=form.findChild<QLabel*>("labelHint");
    check(hint&&!hint->isHidden()&&hint->text().contains(QStringLiteral("合成证书提示")),"password-only form still shows the actual existing certificate hint");
    form.setHint({});check(hint->isHidden()&&hint->text().isEmpty(),"absent password hint clears old text without leaving a placeholder");
    check(!unlock->isEnabled(),"empty password keeps Unlock disabled");
    int submitted=0;QObject::connect(&form,&WizUserCipherForm::cipherCheckRequest,&form,[&]{++submitted;});
    input->setText(QStringLiteral("Synthetic-ui-only"));unlock->click();
    check(submitted==1&&form.userCipher()==QStringLiteral("Synthetic-ui-only"),"manual password still submits the current value once without biometric dispatch");
    form.cipherCorrect();check(input->text().isEmpty(),"successful password UI clears the editor without changing the note");
    WizBiometricUnlockWidget::Context old;form.prepareBiometric(old,true);form.cancelBiometric();
    check(!form.findChild<WizBiometricUnlockWidget*>()&&!form.preserveNativeAuthenticationOnInactive()
        &&form.takeBiometricCredential(old).error==Error::UnsupportedPlatform,"old enabled-policy preparation cannot create biometric controls or yield credentials");
    check(!hasSigningPrerequisites()&&!isBiometryAvailable(),"password-only backend never requests signing or biometry capability");
    const auto generated=WizCertificateCreation::generate("Synthetic-inert-backend","Synthetic-inert-backend",{});
    Scope scope{"https://synthetic.invalid/","synthetic-password-only",{},generated.certificate};
    check(bool(generated)&&enroll(scope,"Synthetic-inert-backend",true)==Error::UnsupportedPlatform,
        "removed native enrollment returns unsupported even for a valid synthetic certificate");
    check(forget(scope,true)==Error::UnsupportedPlatform,"removed native deletion never touches existing remembered records");
    ReadOperation operation;check(operation.read(scope).error==Error::UnsupportedPlatform,"removed native read cannot release any credential");
    operation.cancel();check(operation.read(scope).error==Error::Canceled,"inert one-shot operation still rejects canceled reuse");
    WizPreferenceWindow preferences(app,nullptr);auto* tabs=preferences.findChild<QTabWidget*>("tabWidget");bool noBiometricTab=tabs;
    if(tabs)for(int i=0;i<tabs->count();++i)noBiometricTab=noBiometricTab&&!tabs->tabText(i).contains("Touch ID",Qt::CaseInsensitive);
    check(noBiometricTab,"production Settings contains no Touch ID tab");preferences.showPrintMarginPage();
    check(tabs&&tabs->currentWidget()->objectName()=="tabPrint","removing biometric Settings preserves print-margin navigation");
    check(settings.get("BiometricUnlock","synthetic-old-record")=="1","opening password-only UI leaves old local policy values untouched");
}
