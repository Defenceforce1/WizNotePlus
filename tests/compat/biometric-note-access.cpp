#include "share/WizBiometricNoteAccess.h"
#include "widgets/WizBiometricManageWidget.h"
#include "widgets/WizBiometricManageHost.h"
#include "sync/WizApiEntry.h"
#include "WizPreferenceDialog.h"
#include <QTabWidget>
#include "share/WizSessionAccess.h"
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
#include <QCheckBox>
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
struct ManageState {std::atomic<int> enroll{0},forget{0};QSemaphore release;bool hold=false;Error result=Error::None;std::atomic<bool> offGui{false};QThread* gui=nullptr;};
class ManageOperation final : public WizBiometricManageWidget::Operation {
    std::shared_ptr<ManageState> state;
public:
    explicit ManageOperation(std::shared_ptr<ManageState> value):state(std::move(value)){}
    Error enroll(const Scope&,const QString&)override{++state->enroll;return run();}
    Error forget(const Scope&)override{++state->forget;return run();}
    Error run(){state->offGui=QThread::currentThread()!=state->gui;if(state->hold&&!state->release.tryAcquire(1,3000))return Error::Failed;return state->result;}
};
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
    check(!gate.requiresAuthorization(true) && gate.requiresAuthorization(false),"optional unprotected gate keeps its explicit cache semantics; encrypted hosts require it");
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
    // Phase190: shared product session with synthetic monitor/policy events.
    {
        WizSessionAccess::Monitor monitor(false,nullptr,35);PolicyEvents policies;Session session(&monitor,&policies);
        Gate first,second;first.setSession(&session);second.setSession(&session);first.begin(context,true);
        check(first.requiresAuthorization(true),"new app session cannot treat a warm save cache as a verified unlock");
        check(first.grant(context,first.ticket()),"verified opening can establish its exact certificate session");
        const auto old=first.ticket();auto another=context;another.documentGuid+="-other";++another.openSequence;
        first.begin(another,true);second.begin(context,true);
        check(!first.requiresAuthorization(true)&&!second.requiresAuthorization(true)&&!first.grant(context,old),"same-certificate note and window changes reuse proof without reviving the old page ticket");
        const auto hidden=first.ticket();first.revoke();
        check(!first.current(another)&&!WizDocumentCallbackEpoch::current(hidden),"hiding invalidates local page reads while retaining session proof");
        first.begin(another,true);check(!first.requiresAuthorization(true),"showing a note restores the existing app-session authorization");
        auto group=context;group.scope.kbGuid="synthetic-group";Gate otherKb;otherKb.setSession(&session);otherKb.begin(group,true);
        check(otherKb.requiresAuthorization(true)&&!first.requiresAuthorization(true),"another knowledge base needs its own proof without locking personal notes");
        check(otherKb.grant(group,otherKb.ticket()),"separate knowledge-base proof coexists in the app session");
        policies.retire(policyIdentity(context.scope));
        check(first.requiresAuthorization(true)&&!otherKb.requiresAuthorization(true),"local-record policy retirement clears only its matching certificate scope");
        first.begin(context,true);first.grant(context,first.ticket());const auto beforeBoundary=first.ticket();
        monitor.observeApplicationState(Qt::ApplicationInactive);monitor.observeApplicationState(Qt::ApplicationActive);
        check(!first.requiresAuthorization(true),"brief app switching keeps shared session proof");
        monitor.observeApplicationState(Qt::ApplicationInactive);
        check(until([&]{return first.requiresAuthorization(true)&&otherKb.requiresAuthorization(true);})&&db.getCertPassword()==password,"grace expiry retires all proofs without clearing queued-save credentials");
        monitor.observeApplicationState(Qt::ApplicationActive);second.begin(context,true);second.grant(context,second.ticket());
        check(!first.current(context)&&!first.grant(context,beforeBoundary)&&!second.requiresAuthorization(true),"a fresh unlock elsewhere cannot revive pre-expiry page authorization");
        first.begin(context,true);check(!first.requiresAuthorization(true),"a new page generation may reuse the new current session proof");
        for(int field=0;field<2;++field){
            auto foreign=context;if(field==0)foreign.scope.accountGuid+="-other";else foreign.scope.serverIdentity+="other/";
            second.begin(foreign,true);check(second.requiresAuthorization(true)&&first.requiresAuthorization(true),"account or server switch drops previous session proof");
            first.begin(context,true);check(first.requiresAuthorization(true),"returning to the previous account or server requires a new verified unlock");first.grant(context,first.ticket());
        }
        auto revised=context;revised.scope.certificate.encryptedD+="A";second.begin(revised,true);
        check(second.requiresAuthorization(true)&&first.requiresAuthorization(true),"certificate revision invalidates its old proof generation");
        first.begin(context,true);check(first.requiresAuthorization(true),"reverting certificate bytes cannot resurrect a retired grant");first.grant(context,first.ticket());
        monitor.systemBoundary(true);check(first.requiresAuthorization(true),"hard system boundary locks immediately without waiting for the timer");
        monitor.observeApplicationState(Qt::ApplicationActive);int notified=0;QObject::connect(&session,&Session::authorizationAvailable,&app,[&](const QString&){++notified;});
        first.begin(context,true);first.grant(context,first.ticket());session.retire();app.processEvents();
        check(notified==0&&first.requiresAuthorization(true),"retirement suppresses queued authorization notifications from an older session");
        first.begin(context,true);first.grant(context,first.ticket());monitor.beginShutdown();second.begin(context,true);
        check(first.requiresAuthorization(true)&&!second.grant(context,second.ticket())&&db.getCertPassword()==password,"shutdown forbids reuse or new grants and leaves save cache independent");
    }
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
    // Phase164 opt-in management, exact conflict and forget semantics.
    {
        using Manage=WizBiometricManageWidget;
        const auto click=[&](Manage& widget,bool forget,bool consent,const QString& value){widget.findChild<QLineEdit*>("biometricManagePassword")->setText(value);widget.findChild<QCheckBox*>("biometricManageConsent")->setChecked(consent);widget.findChild<QPushButton*>(forget?"biometricManageForget":"biometricManageEnroll")->click();};
        const auto fresh=[] {auto p=std::make_shared<ManageState>();p->gui=QThread::currentThread();return p;};
        const auto originalCert=scope.certificate.encryptedD;settings.set("BiometricUnlock",policy,"0");
        QThreadPool pool;const auto probe=fresh();bool current=true;int policyWrites=0,retired=0;
        Manage::Services services{[probe]{return std::make_shared<ManageOperation>(probe);},[&](const Scope& s){return current&&itemIdentity(s)==itemIdentity(scope);},[&](const Scope& s){++policyWrites;settings.set("BiometricUnlock",policyIdentity(s),"1");return optedIn(settings,s);},&pool};
        Manage widget(services);widget.show();widget.setContext(scope,false);click(widget,false,true,password);
        check(probe->enroll==0&&!optedIn(settings,scope)&&widget.stage()==Manage::Stage::Disabled,"management defaults unavailable and never enrolls or enables policy automatically");
        widget.setContext(scope,true);click(widget,false,false,password);check(probe->enroll==0&&policyWrites==0,"explicit button without explicit consent cannot start enrollment");
        click(widget,false,true,"wrong synthetic password");check(until([&]{return widget.stage()==Manage::Stage::Failed;})&&probe->enroll==0&&policyWrites==0&&widget.error()==Error::PasswordInvalid,"local wrong-password verification refuses before management provider and policy write");
        QObject::connect(&widget,&Manage::accessRetired,&widget,[&]{++retired;});click(widget,false,true,password);
        check(until([&]{return widget.stage()==Manage::Stage::Succeeded;})&&probe->enroll==1&&policyWrites==1&&optedIn(settings,scope)&&probe->offGui&&retired==1,"explicit enrollment verifies off GUI and enables actual isolated policy only after native-provider success");
        check(widget.findChild<QLineEdit*>("biometricManagePassword")->text().isEmpty()&&!widget.findChild<QCheckBox*>("biometricManageConsent")->isChecked(),"management clears masked password and one-shot consent after starting without publishing the secret");
        probe->result=Error::AlreadyExists;click(widget,false,true,password);
        check(until([&]{return widget.stage()==Manage::Stage::Conflict;})&&probe->enroll==2&&probe->forget==0&&policyWrites==1,"existing record becomes an explicit conflict, never automatic overwrite or delete");
        probe->result=Error::None;click(widget,true,true,{});
        check(until([&]{return widget.stage()==Manage::Stage::Succeeded;})&&probe->forget==1&&policyWrites==1&&optedIn(settings,scope)&&retired==2,"forget removes only local memory and retains fresh-authorization policy instead of reopening warm-cache legacy access");
        probe->result=Error::Missing;click(widget,true,true,{});
        check(until([&]{return widget.stage()==Manage::Stage::Succeeded;})&&policyWrites==1&&retired==3,"forget missing record is idempotent and never disables required access policy");
        for(const auto error:{Error::SigningRequired,Error::Canceled,Error::AccessDenied,Error::Failed}){
            probe->result=error;click(widget,true,true,{});check(until([&]{return widget.stage()==Manage::Stage::Failed;})&&retired==3&&policyWrites==1,"failed or canceled forget never grants access or mutates policy");
        }
        check(db.getCertPassword()==password&&scope.certificate.encryptedD==originalCert,"management never clears in-flight save cache or alters certificate material");
        widget.beginShutdown();click(widget,false,true,password);check(widget.stage()==Manage::Stage::Closed&&policyWrites==1,"management shutdown cannot start another enrollment");
        for(const bool forget:{false,true}){
            const auto pending=fresh();pending->hold=true;QThreadPool held;int writes=0,notifications=0;bool same=true;
            Manage late({[pending]{return std::make_shared<ManageOperation>(pending);},[&](const Scope&){return same;},[&](const Scope&){++writes;return true;},&held});late.show();late.setContext(scope,true);QObject::connect(&late,&Manage::accessRetired,&late,[&]{++notifications;});click(late,forget,true,password);
            const bool started=until([&]{return pending->enroll+pending->forget==1;});late.hide();late.show();late.setContext(scope,true);click(late,false,true,password);
            check(started&&pending->enroll+pending->forget==1,"hidden or replaced management context cannot overlap its still-running system mutation");
            pending->release.release();const bool joined=held.waitForDone(3000);until([&]{return late.findChild<QLineEdit*>("biometricManagePassword")->isEnabled();});
            check(joined&&writes==0&&notifications==0,"late completed enrollment or forget cannot write policy or revive retired context");
        }
        const auto mismatch=fresh();Manage failedPolicy({[mismatch]{return std::make_shared<ManageOperation>(mismatch);},[](const Scope&){return true;},[](const Scope&){return false;},&pool});failedPolicy.show();failedPolicy.setContext(scope,true);click(failedPolicy,false,true,password);
        check(until([&]{return failedPolicy.stage()==Manage::Stage::Failed;}),"native-provider success with failed policy persistence is not reported as completed enrollment");
        Manage* destroyed=new Manage(services);destroyed->show();destroyed->setContext(scope,true);auto* edit=destroyed->findChild<QLineEdit*>("biometricManagePassword");edit->setText(password);destroyed->findChild<QCheckBox*>("biometricManageConsent")->setChecked(true);QPointer<Manage> weak(destroyed);int clearNotifications=0;QObject::connect(edit,&QLineEdit::textChanged,&app,[&](const QString& v){if(v.isEmpty()){++clearNotifications;delete destroyed;}});destroyed->findChild<QPushButton*>("biometricManageEnroll")->click();
        check(weak&&edit->text().isEmpty()&&clearNotifications==0,"internal password clear is signal-blocked and cannot enter destructive textChanged slots inside Qt line-control update");
        delete destroyed;check(pool.waitForDone(3000)&&!weak,"management destruction after dispatch releases GUI safely while worker owns only its task");
        Manage* factoryGone=nullptr;Manage::Services factoryServices{[&]()->std::shared_ptr<Manage::Operation>{delete factoryGone;return std::make_shared<ManageOperation>(probe);},[](const Scope&){return true;},[](const Scope&){return true;},&pool};
        factoryGone=new Manage(factoryServices);factoryGone->show();factoryGone->setContext(scope,true);QPointer<Manage> factoryWeak(factoryGone);click(*factoryGone,false,true,password);
        check(!factoryWeak,"owner destruction from operation factory safely stops before worker dispatch");
        Manage* reentrant=nullptr;Manage::Services resetServices{[&](){reentrant->setContext(scope,true);return std::make_shared<ManageOperation>(probe);},[](const Scope&){return true;},[](const Scope&){return true;},&pool};reentrant=new Manage(resetServices);reentrant->show();reentrant->setContext(scope,true);const auto prior=probe->enroll.load();click(*reentrant,false,true,password);
        check(reentrant->stage()==Manage::Stage::Ready&&reentrant->findChild<QLineEdit*>("biometricManagePassword")->isEnabled()&&!reentrant->findChild<QPushButton*>("biometricManageEnroll")->isEnabled()&&probe->enroll==prior,"pre-dispatch context retirement releases input without enabling another unconsented enrollment");delete reentrant;
    }
    // Phase164 deterministic session router, never real OS notifications.
    {
        WizSessionAccess::Monitor monitor(false);int notifications=0;QObject::connect(&monitor,&WizSessionAccess::Monitor::accessRetired,&app,[&]{++notifications;});
        monitor.observeApplicationState(Qt::ApplicationInactive);check(notifications==0,"brief switching away keeps the editing session without immediate relock");monitor.systemBoundary(false);monitor.systemBoundary();monitor.observeApplicationState(Qt::ApplicationHidden);
        check(notifications==2,"hard sleep/session boundary escalates prior soft inactivity once without issuing credentials");
        monitor.observeApplicationState(Qt::ApplicationActive);check(notifications==2,"activation rearms notifications only and never restores access automatically");monitor.systemBoundary();check(notifications==3,"a later session boundary retires a freshly active session again");
        monitor.beginShutdown();monitor.observeApplicationState(Qt::ApplicationActive);monitor.systemBoundary();monitor.beginShutdown();check(notifications==4,"shutdown retires once and prevents further session reactivation");
        auto* gone=new WizSessionAccess::Monitor(false);QPointer<WizSessionAccess::Monitor> weak(gone);QObject::connect(gone,&WizSessionAccess::Monitor::accessRetired,&app,[&]{delete gone;});gone->systemBoundary();check(!weak,"session receiver may destroy the monitor during retirement without later member access");
        WizSessionAccess::Monitor grace(false,nullptr,60);int soft=0,hard=0;
        QObject::connect(&grace,&WizSessionAccess::Monitor::accessRetired,&app,[&](bool boundary){boundary?++hard:++soft;});
        grace.observeApplicationState(Qt::ApplicationInactive);grace.observeApplicationState(Qt::ApplicationActive);until([]{return false;},90);
        check(soft==0&&hard==0,"returning within the grace period cancels pending relock");
        grace.observeApplicationState(Qt::ApplicationInactive);until([]{return false;},35);grace.observeApplicationState(Qt::ApplicationHidden);
        check(until([&]{return soft==1;},50)&&hard==0,"repeated background states cannot extend the original inactivity deadline");
        grace.systemBoundary();check(hard==1&&soft==1,"actual sleep or session lock immediately escalates a grace-period lock");
        grace.observeApplicationState(Qt::ApplicationActive);grace.observeApplicationState(Qt::ApplicationInactive);grace.beginShutdown();until([]{return false;},90);
        check(soft==1&&hard==2,"shutdown cancels the inactivity timer without a late extra notification");
        WizSessionAccess::Monitor delayed(false,nullptr,20);int overdue=0;QObject::connect(&delayed,&WizSessionAccess::Monitor::accessRetired,&app,[&]{++overdue;});
        delayed.observeApplicationState(Qt::ApplicationInactive);QThread::msleep(30);delayed.observeApplicationState(Qt::ApplicationActive);
        check(overdue==1,"activation after the grace period still locks when GUI timer delivery was delayed");
    }
    // End Phase164 management/session tests.
    // Phase165 actual disabled host, scoped policy bridge and stale contexts.
    {
        using Manage=WizBiometricManageWidget;
        WizCommonApiEntry::setEnterpriseServerIP("https://synthetic-host.invalid/");
        const auto hostScope=snapshot(db,db,WizCommonApiEntry::server());const auto id=policyIdentity(hostScope);
        settings.set("BiometricUnlock",id,"0");QThreadPool pool;const auto probe=std::make_shared<ManageState>();probe->gui=QThread::currentThread();
        const auto click=[&](Manage* widget,bool forget){widget->findChild<QLineEdit*>("biometricManagePassword")->setText(password);widget->findChild<QCheckBox*>("biometricManageConsent")->setChecked(true);widget->findChild<QPushButton*>(forget?"biometricManageForget":"biometricManageEnroll")->click();};
        auto* events=PolicyEvents::applicationEvents();QStringList notified;
        const auto connection=QObject::connect(events,&PolicyEvents::accessRetired,&app,[&](const QString& key){notified.append(key);});
        events->retire({});events->retire(QString(64,'G'));check(notified.isEmpty(),"policy bus refuses malformed retirement identifiers without notifying any host");
        WizBiometricManageHost disabled(explorer);disabled.show();click(disabled.management(),false);
        check(disabled.management()->stage()==Manage::Stage::Disabled&&!optedIn(settings,hostScope)&&notified.isEmpty(),"normal host construction and presentation are unavailable and do not write policy or publish access changes");
        {
            WizPreferenceWindow settingsWindow(explorer,nullptr);auto* tabs=settingsWindow.findChild<QTabWidget*>("tabWidget");
            auto* manage=tabs?tabs->widget(tabs->count()-1)->findChild<Manage*>():nullptr;
            check(manage&&manage->stage()==Manage::Stage::Disabled&&tabs->currentIndex()==0,"actual Settings constructor adds the disabled management page without changing the initial tab");
            settingsWindow.showPrintMarginPage();check(tabs&&tabs->currentWidget()->objectName()=="tabPrint","actual print-margin settings navigation retains its original page after the management tab is appended");
            if(manage){tabs->setCurrentIndex(tabs->count()-1);click(manage,false);}
            check(manage&&!optedIn(settings,hostScope)&&notified.isEmpty(),"selecting the actual Settings management page and clicking its disabled button cannot write policy or launch an operation");
        }
        WizBiometricManageHost host(explorer,{true,[probe]{return std::make_shared<ManageOperation>(probe);},&pool});host.show();
        click(host.management(),false);
        check(until([&]{return host.management()->stage()==Manage::Stage::Succeeded;})&&probe->enroll==1&&optedIn(settings,hostScope)&&notified==QStringList{id,id},"actual host enables only its current personal scope after provider success and broadcasts scoped retirement at start and commit");
        notified.clear();probe->result=Error::AlreadyExists;click(host.management(),false);
        check(until([&]{return host.management()->stage()==Manage::Stage::Conflict;})&&probe->forget==0&&notified==QStringList{id},"actual host conflict retires existing access but never deletes or overwrites the existing local record");
        notified.clear();probe->result=Error::None;click(host.management(),true);
        check(until([&]{return host.management()->stage()==Manage::Stage::Succeeded;})&&probe->forget==1&&optedIn(settings,hostScope)&&notified==QStringList{id,id},"actual forget bridge retains required policy and publishes no credential or certificate data");
        check(db.getCertPassword()==password&&snapshot(db,db,WizCommonApiEntry::server()).certificate.encryptedD==hostScope.certificate.encryptedD,"actual management host leaves certificate and active save password unchanged");
        for(const int field:{0,1,2}){
            const auto pending=std::make_shared<ManageState>();pending->gui=QThread::currentThread();pending->hold=true;QThreadPool held;
            WizBiometricManageHost stale(explorer,{true,[pending]{return std::make_shared<ManageOperation>(pending);},&held});stale.show();settings.set("BiometricUnlock",id,"0");notified.clear();click(stale.management(),false);
            const bool started=until([&]{return pending->enroll==1;});
            if(field==0)db.setMeta("Account","GUID","other-synthetic-host");
            if(field==1)WizCommonApiEntry::setEnterpriseServerIP("https://other-synthetic-host.invalid/");
            if(field==2)db.setUserCert(hostScope.certificate.n,hostScope.certificate.e,hostScope.certificate.encryptedD+"A",hostScope.certificate.hint);
            pending->release.release();const bool joined=held.waitForDone(3000);
            check(started&&joined&&until([&]{return stale.management()->stage()==Manage::Stage::Disabled;})&&!optedIn(settings,hostScope)&&notified==QStringList{id},"account server or encrypted certificate revision change rejects a late management result without enabling either scope");
            db.setMeta("Account","GUID","synthetic-account");WizCommonApiEntry::setEnterpriseServerIP("https://synthetic-host.invalid/");db.setUserCert(hostScope.certificate.n,hostScope.certificate.e,hostScope.certificate.encryptedD,hostScope.certificate.hint);
        }
        settings.set("BiometricUnlock",id,"1");notified.clear();const int before=probe->enroll.load();
        auto* destroyed=new WizBiometricManageHost(explorer,{true,[probe]{return std::make_shared<ManageOperation>(probe);},&pool});destroyed->show();QPointer<WizBiometricManageHost> weak(destroyed);
        const auto death=QObject::connect(events,&PolicyEvents::accessRetired,&app,[&](const QString& key){if(key==id&&weak)delete destroyed;});click(destroyed->management(),false);
        check(!weak&&probe->enroll==before,"policy receiver may destroy the management host during start before worker dispatch");QObject::disconnect(death);
        {
            const auto heldProbe=std::make_shared<ManageState>();heldProbe->hold=true;heldProbe->gui=QThread::currentThread();QThreadPool held;
            WizBiometricManageHost first(explorer,{true,[heldProbe]{return std::make_shared<ManageOperation>(heldProbe);},&held});first.show();click(first.management(),false);
            const bool started=until([&]{return heldProbe->enroll==1;});
            const auto otherProbe=std::make_shared<ManageState>();otherProbe->gui=QThread::currentThread();WizBiometricManageHost second(explorer,{true,[otherProbe]{return std::make_shared<ManageOperation>(otherProbe);},&pool});second.show();click(second.management(),true);
            check(started&&until([&]{return second.management()->stage()==Manage::Stage::Failed;})&&otherProbe->forget==0,"separate management hosts cannot overlap enrollment and deletion or silently queue an obsolete second intent");
            first.hide();heldProbe->release.release();const bool joined=held.waitForDone(3000);click(second.management(),true);
            check(joined&&until([&]{return second.management()->stage()==Manage::Stage::Succeeded;})&&otherProbe->forget==1,"retired host completion releases process-wide mutation lease for a new explicit management action");
        }
        QObject::disconnect(connection);pool.waitForDone();
    }
    // End Phase165 host tests.
    if(const QString output=qEnvironmentVariable("WIZNOTE_BIOMETRIC_UI_SNAPSHOT_DIR");!output.isEmpty()){
        using Manage=WizBiometricManageWidget;Manage::Services fake;
        fake.makeOperation=[]()->std::shared_ptr<Manage::Operation>{return {};};fake.contextCurrent=[](const Scope&){return true;};fake.enablePolicy=[](const Scope&){return true;};
        Manage panel(fake);panel.resize(560,480);panel.setContext(scope,true);panel.show();app.processEvents();
        check(!panel.findChild<QPushButton*>("biometricManageEnroll")->isEnabled()&&!panel.findChild<QPushButton*>("biometricManageForget")->isEnabled(),"settings actions are disabled before explicit consent and enrollment password");
        auto* consent=panel.findChild<QCheckBox*>("biometricManageConsent");consent->setChecked(true);
        check(!panel.findChild<QPushButton*>("biometricManageEnroll")->isEnabled()&&panel.findChild<QPushButton*>("biometricManageForget")->isEnabled(),"minimal settings still requires explicit consent but no password for removing a local record");
        panel.findChild<QLineEdit*>("biometricManagePassword")->setText(password);
        check(panel.findChild<QPushButton*>("biometricManageEnroll")->isEnabled(),"minimal settings enables enrollment only after consent and password");
        panel.findChild<QLineEdit*>("biometricManagePassword")->clear();consent->setChecked(false);
        check(QGuiApplication::platformName()=="offscreen"&&panel.grab().save(output+"/touch-id-settings.png"),"synthetic settings snapshot renders without real account or desktop");
    }
    check(db.getCertPassword()==password,"UI cancellation and note permission revocation never clear the existing save cache");
    WizDatabase::clearCertPassword();
    qInfo().noquote()<<"biometric-note-access checks"<<checks<<"failures"<<failures;
    return failures?1:0;
}
