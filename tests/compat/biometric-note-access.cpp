#include "share/WizBiometricNoteAccess.h"
#include "share/WizSettings.h"
#include "share/WizGlobal.h"
#include "database/WizDatabase.h"
#include "database/WizDatabaseManager.h"
#include "WizUserCipherForm.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QLineEdit>
#include <QPushButton>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QTimer>
#include <atomic>
#include <stdexcept>
namespace {
using namespace WizBiometricNoteAccess;
using namespace WizBiometricKeychain;
bool until(const std::function<bool()>& test, int ms=3000)
{
    QElapsedTimer time; time.start();
    while(!test() && time.elapsed()<ms) { QEventLoop loop; QTimer::singleShot(5,&loop,&QEventLoop::quit); loop.exec(); }
    return test();
}
class App final : public WizExplorerApp {
public:
    App(WizDatabaseManager& db, WizUserSettings& settings):db(db),settings(settings){}
    QWidget window;
    WizDatabaseManager& db; WizUserSettings& settings;
    QWidget* mainWindow() override { return &window; }
    QObject* object() override { return &window; }
    WizDatabaseManager& databaseManager() override { return db; }
    WizCategoryBaseView& category() override { throw std::logic_error("no category"); }
    WizUserSettings& userSettings() override { return settings; }
};
struct ReadState { std::atomic<int> read{0},cancel{0}; QSemaphore resume; bool hold=false; QString password; };
class Read final : public WizBiometricUnlockWidget::Operation {
    std::shared_ptr<ReadState> state;
public:
    explicit Read(std::shared_ptr<ReadState> state):state(std::move(state)){}
    Result read(const Scope&) override {
        state->read.fetch_add(1);
        if(state->hold && !state->resume.tryAcquire(1,3000)) return {Error::Failed,{}};
        return {Error::None,state->password};
    }
    void cancel() noexcept override { state->cancel.fetch_add(1); }
};
}
int wizBiometricNoteAccessSelfTest(int argc,char** argv)
{
    QTemporaryDir root; if(!root.isValid())return 2;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE",root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME",root.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME",root.filePath("config").toUtf8());
    qputenv("TMPDIR",root.filePath("tmp").toUtf8());
    QApplication app(argc,argv); app.setQuitOnLastWindowClosed(false);
    QSettings uiSettings(root.filePath("ui.ini"),QSettings::IniFormat);
    QSettings globalSettings(root.filePath("global.ini"),QSettings::IniFormat);
    uiSettings.setValue("Theme/Name","default");
    WizGlobal::setSettings(&uiSettings);
    WizGlobal::setGlobalSettings(&globalSettings);
    int checks=0,failures=0;
    const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;return ok;};
    WizDatabaseManager manager("synthetic-biometric-note-only");
    if(!check(manager.open(),"actual temporary database opens without a normal account"))return 1;
    auto& db=manager.db(); WizUserSettings settings(db); App explorer(manager,settings);
    db.setMeta("Account","GUID","synthetic-account");
    const QString password="Synthetic-note-bridge-2026";
    const auto generated=WizCertificateCreation::generate(password,password,{});
    if(!check(bool(generated)&&db.setUserCert(generated.certificate.n,generated.certificate.e,generated.certificate.encryptedD,{}),
              "actual database stores only a synthetic encrypted certificate"))return 1;
    const auto scope=snapshot(db,db,"https://synthetic.invalid/");
    check(scope.accountGuid=="synthetic-account" && itemIdentity(scope).size()==64,"scope snapshot reads the current local account and certificate without service discovery");
    const auto policy=policyIdentity(scope);
    check(policy.size()==64 && !optedIn(settings,scope),"actual local settings default to biometric access disabled");
    settings.set("BiometricUnlock",policy,"1");
    check(optedIn(settings,scope),"explicit synthetic opt-in is read only for its exact account server and knowledge base");
    auto changed=scope; changed.certificate.encryptedD+="A";
    check(policyIdentity(changed)==policy && itemIdentity(changed)!=itemIdentity(scope) && optedIn(settings,changed),
          "certificate revision changes the native item but cannot silently disable the authorization policy");
    for(int field=0;field<3;++field){
        changed=scope;
        if(field==0)changed.serverIdentity+="other/";
        if(field==1)changed.accountGuid+="-other";
        if(field==2)changed.kbGuid="other-kb";
        check(policyIdentity(changed)!=policy && !optedIn(settings,changed),"server account and kb keep separate explicit opt-in settings");
    }
    changed=scope;changed.accountGuid+=QChar::Null;
    check(policyIdentity(changed).isEmpty(),"malformed identity cannot address a saved policy");
    for(const auto& value:{"true","2","0",""}){
        settings.set("BiometricUnlock",policy,value);
        check(!optedIn(settings,scope),"only the explicit stored value one enables remembered access");
    }
    settings.set("BiometricUnlock",policy,"1");
    const auto& cert=generated.certificate;
    WizDatabase::clearCertPassword();
    check(!db.cacheCertPasswordLocally("wrong",cert.n,cert.e,cert.encryptedD) && db.getCertPassword().isEmpty(),
          "wrong password refuses locally without populating cache or refreshing from a server");
    check(db.cacheCertPasswordLocally(password,cert.n,cert.e,cert.encryptedD) && db.getCertPassword()==password,
          "strong local certificate verification publishes the exact synthetic password to the existing cache");
    check(!db.cacheCertPasswordLocally("wrong",cert.n,cert.e,cert.encryptedD) && db.getCertPassword()==password,
          "failed local verification preserves an already warm password needed by in-flight saves");
    check(!db.cacheCertPasswordLocally(password,cert.n,cert.e,cert.encryptedD+"A") && db.getCertPassword()==password,
          "changed certificate snapshot cannot publish a password or clear another operation cache");
    Context context{scope,"synthetic-note",1}; Gate gate;
    gate.begin(context,false);
    check(!gate.requiresAuthorization(true) && gate.requiresAuthorization(false),"non-opted-in policy preserves original warm and cold cache behavior");
    gate.begin(context,true);const auto ticket=gate.ticket();
    check(gate.requiresAuthorization(true),"opted-in note requires fresh permission even with an actual warm database cache");
    Gate other;other.begin(context,true);
    check(!gate.grant(context,other.ticket()),"a live ticket from another window or gate cannot grant this note access");
    auto next=context;++next.openSequence;
    check(!gate.grant(next,ticket),"another open request cannot consume the current authorization ticket");
    check(gate.grant(context,ticket) && !gate.requiresAuthorization(true),"verified current opening grants only its own already-open note");
    check(gate.requiresAuthorization(false),"lost password cache still requires fresh authentication after a prior grant");
    gate.revoke();
    check(!gate.grant(context,ticket) && gate.requiresAuthorization(true) && db.getCertPassword()==password,
          "revocation rejects old permission without clearing the password used by existing saves");
    gate.begin(context,true);const auto before=gate.ticket();gate.begin(next,true);gate.begin(context,true);
    check(!gate.grant(context,before) && gate.requiresAuthorization(true),"leave and return to the same note cannot revive a stale open ticket");
    auto bad=context;bad.scope.certificate={};gate.begin(bad,true);
    check(gate.requiresAuthorization(true) && !gate.grant(bad,gate.ticket()),"required but invalid certificate context refuses instead of reverting to legacy cache access");
    {
        QThreadPool pool;auto state=std::make_shared<ReadState>();state->password=password;
        WizUserCipherForm form(explorer,nullptr,{[state]{return std::make_shared<Read>(state);},&pool});
        form.resize(760,400);form.show();app.processEvents();
        auto* bio=form.findChild<WizBiometricUnlockWidget*>("cipherBiometricUnlock");
        auto* edit=form.findChild<QLineEdit*>("editUserCipher");
        check(bio && bio->isHidden() && edit && edit->echoMode()==QLineEdit::Password,
              "actual old password form keeps its masked editor and hides biometrics by default");
        form.prepareBiometric(context,true);app.processEvents();
        check(bio->isVisible() && state->read==0,"preparing the actual password form adds controls without automatic authentication");
        int ready=0,manual=0;
        QObject::connect(&form,&WizUserCipherForm::biometricCredentialReady,&form,[&]{++ready;});
        QObject::connect(&form,&WizUserCipherForm::cipherCheckRequest,&form,[&]{++manual;});
        bio->findChild<QPushButton*>("biometricUnlockStart")->click();
        check(until([&]{return ready==1;}),"actual password form forwards a simulated verified biometric result once");
        const auto value=form.takeBiometricCredential(context);
        check(value && value.password==password && edit->text().isEmpty() && form.userCipher().isEmpty() && manual==0,
              "biometric credential is not injected into the visible editor or the legacy network-refresh validation signal");
        gate.begin(context,true);const auto current=gate.ticket();
        check(value && db.cacheCertPasswordLocally(value.password,cert.n,cert.e,cert.encryptedD) && gate.grant(context,current),
              "actual form result local database verification and note gate compose without a server or keychain");
        form.hide();
        check(!gate.requiresAuthorization(true),"successful password-form hiding does not revoke the independent already-open note lease");
        form.show();edit->setText(password);form.onButtonOK_clicked();
        check(manual==1 && form.userCipher()==password,"original manual-password request remains available and unchanged");
        form.cipherCorrect();check(edit->text().isEmpty(),"original password clearing behavior remains available after manual success");
        const auto path=qEnvironmentVariable("WIZNOTE_BIOMETRIC_FORM_EVIDENCE_DIR");
        if(!path.isEmpty()) {QDir().mkpath(path);form.prepareBiometric(context,true);app.processEvents();check(form.grab().save(QDir(path).filePath("cipher-biometric.png")),"synthetic integrated password-form screenshot saves without a real note");}
    }
    {
        QThreadPool pool;auto state=std::make_shared<ReadState>();state->password=password;state->hold=true;
        WizUserCipherForm form(explorer,nullptr,{[state]{return std::make_shared<Read>(state);},&pool});
        form.show();form.prepareBiometric(context,true);auto* bio=form.findChild<WizBiometricUnlockWidget*>();
        int ready=0;QObject::connect(&form,&WizUserCipherForm::biometricCredentialReady,&form,[&]{++ready;});
        bio->findChild<QPushButton*>("biometricUnlockStart")->click();
        check(until([&]{return state->read==1;}),"typing-cancellation fixture starts its isolated background read");
        form.findChild<QLineEdit*>("editUserCipher")->setText("manual-synthetic");state->resume.release();
        check(pool.waitForDone(3000),"typing-canceled worker completes without owning the password form");
        until([&]{return false;},60);
        check(state->cancel==1 && ready==0 && !form.takeBiometricCredential(context),"typing a manual password cancels and drops the late biometric result");
    }
    {
        auto* form=new WizUserCipherForm(explorer,nullptr);auto* bio=form->findChild<WizBiometricUnlockWidget*>();
        QPointer<WizUserCipherForm> guard(form);
        QObject::connect(bio,&WizBiometricUnlockWidget::stageChanged,&app,[form]{delete form;});
        form->prepareBiometric(context,false);
        check(!guard,"actual form survives owner deletion during context-change notification without a later widget access");
    }
    check(db.getCertPassword()==password,"UI cancellation and note permission revocation never clear the existing save cache");
    WizDatabase::clearCertPassword();
    qInfo().noquote()<<"biometric-note-access checks"<<checks<<"failures"<<failures;
    return failures?1:0;
}
