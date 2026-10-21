#import <Cocoa/Cocoa.h>
#include "WizMainWindow.h"
#include "WizThumbCache.h"
#include "WizUserCipherForm.h"
#include "WizDef.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "share/WizGlobal.h"
#include "share/WizSettings.h"
#include "share/WizThreads.h"
#include "share/WizMisc.h"
#include "share/WizWebEngineView.h"
#include "share/WizCertificateCreation.h"
#include "share/WizBiometricNoteAccess.h"
#include "share/WizSessionAccess.h"
#include "share/WizLegacyLightAppearance.h"
#include "share/WizZiwReader.h"
#include "sync/WizApiEntry.h"
#include "sync/WizToken.h"
#include "sync/WizAvatarHost.h"
#include "gui/documentviewer/WizDocumentView.h"
#include "gui/documentviewer/WizDocumentWebView.h"
#include "gui/documentviewer/WizNoteInfoForm.h"
#include "gui/documentviewer/WizSingleDocumentView.h"
#include "widgets/WizScreenShotWidget.h"
#include "widgets/WizBiometricManageHost.h"
#include "widgets/WizBiometricUnlockWidget.h"
#include <QApplication>
#include <QAction>
#include <QCheckBox>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTimer>
#include <QThreadPool>
#include <QUuid>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <atomic>
#include <functional>
#include <stdexcept>

namespace {
// Deliberately not registered with CTest: foreground use needs explicit consent.
// All profiles are local; normal sync/listeners are disabled by the existing
// isolated-window fixture property, never by changing system/network policy.
class LocalOnly final : public QWebEngineUrlRequestInterceptor {
public:
    std::atomic<int> blocked{0};
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto s=info.requestUrl().scheme();
        if(s!="file"&&s!="qrc"&&s!="data"&&s!="about"&&s!="blob"){++blocked;info.block(true);}
    }
};
bool waitFor(const std::function<bool()>& predicate,int timeout=15000){
    if(predicate())return true;QEventLoop loop;QTimer poll,deadline;bool done=false;
    QObject::connect(&poll,&QTimer::timeout,&loop,[&]{if(predicate()){done=true;loop.quit();}});
    QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
    poll.start(20);deadline.setSingleShot(true);deadline.start(timeout);loop.exec();return done;
}
QVariant js(QWebEnginePage* page,const QString& script,quint32 world=0){
    struct State {bool ready=false;QVariant value;};auto s=std::make_shared<State>();
    page->runJavaScript(script,world,[s](const QVariant& v){s->ready=true;s->value=v;});waitFor([s]{return s->ready;},10000);return s->value;
}
QByteArray bytes(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
bool writeNew(const QString& path,const QByteArray& data){QFile f(path);return f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write(data)==data.size();}
// Observe ownership for exact finally-cleanup. Calls the real native backend,
// never supplies authentication results or replaces the default read provider.
class ObservedNativeManage final : public WizBiometricManageWidget::Operation {
    std::shared_ptr<std::atomic<bool>> owned;
public:
    explicit ObservedNativeManage(std::shared_ptr<std::atomic<bool>> value):owned(std::move(value)){}
    WizBiometricKeychain::Error enroll(const WizBiometricKeychain::Scope& s,const QString& p)override{
        const auto r=WizBiometricKeychain::enroll(s,p,true);
        if(r==WizBiometricKeychain::Error::None)owned->store(true);
        return r;
    }
    WizBiometricKeychain::Error forget(const WizBiometricKeychain::Scope& s)override{
        const auto r=WizBiometricKeychain::forget(s,true);
        if(r==WizBiometricKeychain::Error::None||r==WizBiometricKeychain::Error::Missing)owned->store(false);
        return r;
    }
};
}
int wizNativeFeatureAcceptance(int argc,char** argv){
    const bool biometricMain=qEnvironmentVariable("WIZNOTE_TEST_FOCUS")=="biometric-main-window";
    const bool coordinationOnly=argc==2&&QByteArray(argv[1])=="--biometric-window-coordination-probe";
    const bool liveSession=argc==2&&QByteArray(argv[1])=="--biometric-live-session-probe";
    const bool multiWindow=coordinationOnly||(argc==2&&QByteArray(argv[1])=="--biometric-multi-window-probe");
    const bool manualOnly=liveSession||multiWindow||(argc==2&&QByteArray(argv[1])=="--biometric-save-quit-probe");
    if(biometricMain&&(argc!=2||(!manualOnly&&QByteArray(argv[1])!="--biometric-main-window-probe")
        ||qgetenv("WIZNOTE_SYNTHETIC_TOUCH_ID_CONSENT")!="create-read-delete"))return 2;
    const QString artifacts=qEnvironmentVariable("WIZNOTE_TEST_ARTIFACT_DIR");
    if(!QFileInfo(artifacts).isAbsolute()||!QDir(artifacts).exists()||QFileInfo::exists(artifacts+"/frontend-result.json"))return 2;
    QTemporaryDir root;if(!root.isValid())return 2;QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE",root.filePath("store").toUtf8());qputenv("XDG_CACHE_HOME",root.filePath("cache").toUtf8());qputenv("XDG_CONFIG_HOME",root.filePath("config").toUtf8());qputenv("TMPDIR",root.filePath("tmp").toUtf8());
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    // Exercise actual Qt chooser widgets on Cocoa; do not pretend these are
    // NS(Save/Open)Panel or physical-pointer acceptance.
    QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("WizNotePlus Isolated Feature Acceptance");
    if(QGuiApplication::platformName()!="cocoa")return 2;
    app.setProperty("wiznoteIsolatedWindowControlsTest",true);
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy,"127.0.0.1",1));
    LocalOnly interceptor;WizWebEngineProfile()->setUrlRequestInterceptor(&interceptor);
    QSettings global(root.filePath("global.ini"),QSettings::IniFormat),ui(root.filePath("ui.ini"),QSettings::IniFormat);
    WizGlobal::setGlobalSettings(&global);WizGlobal::setSettings(&ui);WizQueuedThreadsInit();
    WizToken token;WizDatabaseManager manager("synthetic-foreground-only");if(!manager.open())return 2;
    auto& db=manager.db();db.setKbGUID("synthetic-foreground-kb");
    WizUserSettings settings(db);settings.setAutoCheckUpdate(false);settings.setSyncInterval(0);settings.setReceiveMobileFile(false);settings.setNewFeatureGuideVersion(WIZ_NEW_FEATURE_GUIDE_VERSION);
    app.setStyle(QStyleFactory::create("fusion"));WizApplyLegacyLightAppearance();app.setStyleSheet(WizLoadSkinStyleSheet(settings.skin()));app.setFont(QFont(settings.UIFontFamily(),settings.UIFontSize()));
    WizAvatarHost avatar;WizThumbCache cache;
    const QString source=QStringLiteral("# 合成数学验收\n\n"
        "\\begin{equation}\\label{eq:front}\nA+B=C\n\\end{equation}\n\n"
        "\\begin{align}\\label{eq:align}\nx+y&=z\\\\\na+b&=c\n\\end{align}\n\n"
        "\\begin{theorem}\\label{thm:front}\n对任意实数 $x$，有 $x^2\\ge0$。\n\\end{theorem}\n\n"
        "\\begin{multline}\\label{eq:wide}\n")+QString("a+").repeated(65)+"b=c\n\\end{multline}\n\n"
        +QStringLiteral("合成正文 ENDWIDTH，用于检查宽公式不裁切及正常文本字号。\n\n").repeated(16)
        +"See \\eqref{eq:front}, \\eqref{eq:align}, \\ref{thm:front}, \\eqref{eq:wide}.\n";
    QString body=source.toHtmlEscaped();body.replace('\n',"<br>");WIZDOCUMENTDATA math,protectedNote;
    if(!db.createDocumentAndInit("<html><body>"+body+"</body></html>",{},0,"Synthetic acceptance.md","native.html","/Synthetic/",{},math)
       ||!db.createDocumentAndInit("<html><body><h1>Synthetic protected note</h1><p>KEEP-ORIGINAL-BODY</p></body></html>",{},0,"Synthetic protected note","native.html","/Synthetic/",{},protectedNote))return 2;
    // Model existing academic notes, not the legacy <=1-second new-note title/
    // auto-edit special case. Keep the normal View/Web mode transition intact.
    math.tCreated=math.tCreated.addSecs(-60);protectedNote.tCreated=protectedNote.tCreated.addSecs(-60);
    if(!db.modifyDocumentInfo(math)||!db.modifyDocumentInfo(protectedNote))return 2;
    const bool newNoteModes=qEnvironmentVariable("WIZNOTE_TEST_FOCUS")=="new-note-modes";
    const QString mathArchive=db.getDocumentFileName(math.strGUID);const auto mathBefore=bytes(mathArchive);
    WizMainWindow window(manager);window.setWindowTitle(QStringLiteral("WizNotePlus — 仅合成资料前台验收"));window.resize(1200,820);
    for(auto* p:app.findChildren<QWebEngineProfile*>())p->setUrlRequestInterceptor(&interceptor);
    window.show();[NSApp activateIgnoringOtherApps:YES];window.raise();window.activateWindow();window.init();
    QJsonArray results;int failures=0;bool cleanup=false,exitRequested=false;QString password;QByteArray encryptedBefore;
    WizBiometricKeychain::Scope ownedScope;auto ownedItem=std::make_shared<std::atomic<bool>>(false);
    bool nativeAuthenticated=false,syntheticItemAbsent=!biometricMain,nativeReadCanceled=false;
    int workspaceNotificationsPosted=0;
    int liveWorkspaceRetireNotifications=0,liveAppInactiveEvents=0;
    bool userConfirmedPhysicalLockAndUnlock=false;
    const auto capture=[&](QWidget* widget,const QString& name){return widget&&widget->grab().save(artifacts+"/"+name+".png");};
    const auto check=[&](bool ok,const QString& label){results.append(QJsonObject{{"name",label},{"passed",ok}});qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;if(!ok){++failures;capture(&window,"failure");throw std::runtime_error(label.toStdString());}};
    const auto ready=[&]{return window.docView()&&window.docView()->noteLoaded()&&window.docView()->canReadCurrentNote()&&js(window.docView()->web()->page(),"!!(window.WizNativeEditorSetup&&WizNativeEditorSetup.state==='ready')").toBool();};
    const auto openNote=[&](const WIZDOCUMENTDATA& doc){WIZDOCUMENTDATAEX ex;ex=doc;window.viewDocument(ex);};
    QString chooserDestination,promptPassword;bool promptConsent=true;int chooserCount=0,passwordPrompts=0,confirmations=0,successReports=0;QSet<QObject*> handled;
    QTimer dialogs;dialogs.setInterval(25);
    QObject::connect(&dialogs,&QTimer::timeout,&app,[&]{
        auto* modal=QApplication::activeModalWidget();if(!modal||handled.contains(modal))return;
        if(auto* file=qobject_cast<QFileDialog*>(modal)){
            if(chooserDestination.isEmpty())return;handled.insert(file);QObject::connect(file,&QObject::destroyed,&app,[&,file]{handled.remove(file);});++chooserCount;
            const QPointer<QFileDialog> guard(file);const auto destination=chooserDestination;
            QTimer::singleShot(100,file,[guard,destination,&artifacts]{if(!guard)return;guard->setDirectory(artifacts);guard->selectFile(destination);if(auto* edit=guard->findChild<QLineEdit*>("fileNameEdit"))edit->setText(destination);qInfo()<<"Synthetic chooser selection"<<guard->selectedFiles();QMetaObject::invokeMethod(guard,"accept",Qt::QueuedConnection);});
        }else if(auto* input=qobject_cast<QInputDialog*>(modal)){
            handled.insert(input);QObject::connect(input,&QObject::destroyed,&app,[&,input]{handled.remove(input);});++passwordPrompts;
            input->setTextValue(promptPassword);capture(input,"decryption-password-dialog");input->accept();
        }else if(auto* message=qobject_cast<QMessageBox*>(modal)){
            handled.insert(message);QObject::connect(message,&QObject::destroyed,&app,[&,message]{handled.remove(message);});
            qInfo()<<"Synthetic modal title"<<message->windowTitle();
            if(message->standardButtons().testFlag(QMessageBox::Yes)){++confirmations;capture(message,"decryption-confirmation");message->button(promptConsent?QMessageBox::Yes:QMessageBox::No)->click();}
            else{if(message->icon()==QMessageBox::Information&&message->text().contains(QStringLiteral("原加密归档保留在")))++successReports;message->accept();}
        }
    });dialogs.start();
    QObject::connect(&app,&QApplication::aboutToQuit,&window,[&]{auto* p=window.findChild<QProgressDialog*>();cleanup=p&&p->minimum()==0&&p->maximum()==11;});
    QTimer::singleShot(0,&app,[&]{try{
        if(biometricMain){
            using namespace WizBiometricKeychain;
            QObject::connect(WizSessionAccess::Monitor::applicationMonitor(),&WizSessionAccess::Monitor::accessRetired,&app,[](bool hard){qInfo()<<"Synthetic main session retired hard"<<hard<<"application state"<<int(QGuiApplication::applicationState())<<"native app active"<<bool(NSApp.isActive)<<"frontmost is test process"<<bool(NSWorkspace.sharedWorkspace.frontmostApplication.processIdentifier==NSProcessInfo.processInfo.processIdentifier);});
            check(hasSigningPrerequisites(),"actual main-window executable has development signing and keychain authorization");
            {WizBiometricManageHost noCertificate(window,&window);
                check(noCertificate.management()->stage()==WizBiometricManageWidget::Stage::Disabled,"signed normal host remains unavailable without an existing certificate");}
            db.setMeta("Account","GUID",QUuid::createUuid().toString(QUuid::WithoutBraces));
            password="Synthetic-main-"+QUuid::createUuid().toString(QUuid::WithoutBraces);
            const auto generated=WizCertificateCreation::generate(password,password,{});const auto c=generated.certificate;
            check(bool(generated)&&db.setUserCert(c.n,c.e,c.encryptedD,{})&&db.cacheCertPasswordLocally(password,c.n,c.e,c.encryptedD)
                  &&db.encryptDocument(protectedNote),"temporary actual database encrypts the synthetic note and retains save cache");
            encryptedBefore=bytes(db.getDocumentFileName(protectedNote.strGUID));
            WizCommonApiEntry::setEnterpriseServerIP("https://synthetic-main-window.invalid/");
            ownedScope=WizBiometricNoteAccess::snapshot(db,db,WizCommonApiEntry::server());
            check(!itemIdentity(ownedScope).isEmpty()&&!WizBiometricNoteAccess::optedIn(window.userSettings(),ownedScope),"new random account scope has no remembered-access policy");
            {WizBiometricManageHost normalHost(window,&window);
                check(normalHost.management()->stage()==WizBiometricManageWidget::Stage::Ready
                      &&!WizBiometricNoteAccess::optedIn(window.userSettings(),ownedScope),"normal settings host offers management only for signed existing certificate without automatic opt-in");}
            openNote(protectedNote);
            check(waitFor(ready,20000),"actual main window initially opens synthetic encrypted note through legacy warm cache");
            WizBiometricManageHost::Services services;services.explicitlyAvailable=true;
            services.makeOperation=[ownedItem]{return std::make_shared<ObservedNativeManage>(ownedItem);};
            WizBiometricManageHost host(window,std::move(services),&window);host.setWindowFlags(Qt::Dialog);
            host.setWindowTitle(QStringLiteral("仅合成账号 — Touch ID 管理验收"));host.show();
            auto* management=host.management();
            auto* edit=management->findChild<QLineEdit*>("biometricManagePassword");
            auto* consent=management->findChild<QCheckBox*>("biometricManageConsent");
            auto* enrollButton=management->findChild<QPushButton*>("biometricManageEnroll");
            auto* forgetButton=management->findChild<QPushButton*>("biometricManageForget");
            check(edit&&consent&&enrollButton&&forgetButton&&management->stage()==WizBiometricManageWidget::Stage::Ready,"actual host binds personal certificate and management controls");
            edit->setText(password);consent->setChecked(true);enrollButton->click();
            check(waitFor([&]{return management->stage()==WizBiometricManageWidget::Stage::Succeeded;},15000)&&ownedItem->load()
                  &&WizBiometricNoteAccess::optedIn(window.userSettings(),ownedScope),"management UI completes native enrollment then persists synthetic opt-in");
            check(!window.docView()->canReadCurrentNote()&&!db.getCertPassword().isEmpty(),"new policy retires already-open main-window access without clearing save cache");
            if(liveSession){
                host.hide();window.raise();window.activateWindow();[NSApp activateIgnoringOtherApps:YES];
                check(waitFor([&]{return app.applicationState()==Qt::ApplicationActive&&window.isActiveWindow();},5000),"isolated live-session window is active before synthetic password authorization");
                auto* view=window.docView();auto* web=view->web();
                auto* form=view->findChild<WizUserCipherForm*>();auto* passwordEdit=form?form->findChild<QLineEdit*>("editUserCipher"):nullptr;
                check(passwordEdit,"live-session fixture has the production manual password control");
                passwordEdit->setText(password);QMetaObject::invokeMethod(form,"onButtonOK_clicked",Qt::DirectConnection);
                check(waitFor(ready,20000),"fresh synthetic password opens the isolated renderer before user-operated lock");
                view->setEditorMode(modeEditor);
                check(waitFor([&]{return js(web->page(),"!!document.querySelector('.wiz-editor-body[contenteditable=true]')").toBool();}),"live-session note enters the actual editor");
                check(js(web->page(),"(function(){const b=document.querySelector('.wiz-editor-body');b.insertAdjacentHTML('beforeend','<p>QUIT-SAVED-SYNTHETIC</p>');return b.textContent.includes('QUIT-SAVED-SYNTHETIC');})()").toBool(),"live-session note contains distinct latest synthetic unsaved content");
                web->setModified(true);const auto permit=web->noteReadPermit();
                QObject observation;
                QObject::connect(&app,&QGuiApplication::applicationStateChanged,&observation,[&](Qt::ApplicationState state){if(state!=Qt::ApplicationActive)++liveAppInactiveEvents;});
                const auto stopNative=WizSessionAccess::subscribeWorkspace(&observation,[&]{++liveWorkspaceRetireNotifications;});
                QDialog instruction(&window);instruction.setWindowTitle(QStringLiteral("仅合成资料：手动锁屏验收"));
                auto* layout=new QVBoxLayout(&instruction);
                auto* label=new QLabel(QStringLiteral("请手动按 Control + Command + Q 锁屏，再按正常方式解锁。\n回来后点击下方按钮。无需输入笔记密码或按测试指纹。\n本测试不会自动锁屏／休眠，3分钟未确认将退出并清理。"),&instruction);
                layout->addWidget(label);auto* confirm=new QPushButton(QStringLiteral("我已手动锁屏并解锁，检查结果"),&instruction);layout->addWidget(confirm);
                QObject::connect(confirm,&QPushButton::clicked,&observation,[&]{
                    userConfirmedPhysicalLockAndUnlock=true;instruction.hide();
                });
                instruction.show();instruction.raise();instruction.activateWindow();
                writeNew(artifacts+"/ready-for-user-lock.json",QJsonDocument(QJsonObject{{"syntheticOnly",true},{"automaticLock",false},{"automaticSleep",false},{"requiresUserConfirmation",true},{"timeoutSeconds",180}}).toJson());
                qInfo()<<"READY_FOR_USER_LOCK: manually Control+Command+Q,unlock normally,then click the confirmation button; no automatic lock or fingerprint.";
                const bool confirmed=waitFor([&]{return userConfirmedPhysicalLockAndUnlock;},180000);
                if(stopNative)stopNative();
                writeNew(artifacts+"/live-session-observation.json",QJsonDocument(QJsonObject{{"userConfirmedPhysicalLockAndUnlock",confirmed},{"appInactiveEvents",liveAppInactiveEvents},{"nativeWorkspaceRetireNotifications",liveWorkspaceRetireNotifications},{"notificationsPostedByFixture",0},{"automaticDeviceOperation",false}}).toJson());
                check(confirmed,"user explicitly confirms their own physical lock/unlock round trip before acceptance");
                check(liveAppInactiveEvents>0&&app.applicationState()==Qt::ApplicationActive,"actual application leaves active state and returns active during the user-confirmed round trip");
                check(!view->canReadCurrentNote()&&form->isVisible()&&!permit.current(),"lock/unlock does not restore protected body access or the original read permit");
                check(js(web->page(),"document.body.textContent.includes('QUIT-SAVED-SYNTHETIC')&&document.body.textContent.includes('KEEP-ORIGINAL-BODY')").toBool()
                    &&!db.getCertPassword().isEmpty()&&db.hasCert(),"user-confirmed round trip retains latest dirty DOM plus certificate and save cache");
                // A manual round trip can exceed the normal auto-save interval.
                // Do not misclassify a legitimate encrypted save as mutation.
                check(WizZiwReader::isEncryptedFile(db.getDocumentFileName(protectedNote.strGUID)),"user-lock round trip preserves encrypted archive format even if normal autosave runs");
                host.show();consent->setChecked(true);forgetButton->click();
                check(waitFor([&]{return management->stage()==WizBiometricManageWidget::Stage::Succeeded;})&&!ownedItem->load(),"live-session cleanup deletes only the native-add-owned synthetic item");
                syntheticItemAbsent=forget(ownedScope,true)==Error::Missing;
                check(syntheticItemAbsent,"live-session synthetic item is exactly absent before normal save-first quit");
                host.hide();exitRequested=true;window.on_actionExit_triggered();return;
            }
            if(multiWindow){
                host.hide();window.raise();window.activateWindow();[NSApp activateIgnoringOtherApps:YES];
                auto* mainView=window.docView();
                const auto lockedView=[](WizDocumentView* v){auto* f=v?v->findChild<WizUserCipherForm*>():nullptr;return f&&f->isVisible()&&!v->canReadCurrentNote();};
                const auto readyView=[&](WizDocumentView* v,const QString& marker){return v&&v->noteLoaded()&&v->canReadCurrentNote()&&v->web()->page()->url().scheme()=="file"
                    &&js(v->web()->page(),"!!(window.WizNativeEditorSetup&&WizNativeEditorSetup.state==='ready')&&document.body.textContent.includes('"+marker+"')").toBool();};
                const auto manualView=[&](WizDocumentView* v){auto* f=v?v->findChild<WizUserCipherForm*>():nullptr;auto* e=f?f->findChild<QLineEdit*>("editUserCipher"):nullptr;if(!e)return false;e->setText(password);return QMetaObject::invokeMethod(f,"onButtonOK_clicked",Qt::DirectConnection);};
                WIZDOCUMENTDATA other;
                check(db.createDocumentAndInit("<html><body><p>OTHER-MULTI-BODY</p></body></html>",{},0,"Synthetic independent encrypted note","native.html","/Synthetic/",{},other)
                      &&db.encryptDocument(other),"second synthetic encrypted note created in the same temporary certificate scope");
                other.tCreated=other.tCreated.addSecs(-60);check(db.modifyDocumentInfo(other),"independent synthetic note uses existing-note timestamp");
                auto* singleManager=window.findChild<WizSingleDocumentViewManager*>();check(singleManager,"actual main window owns production independent-window manager");
                window.viewNoteInSeparateWindow(other);
                auto* separate=singleManager->getDocumentViewer(other.strGUID);auto* secondView=separate?separate->docView():nullptr;
                check(separate&&waitFor([&]{return lockedView(secondView);}),"production independent encrypted window starts locked despite shared warm save cache");
                window.viewNoteInSeparateWindow(math);
                auto* plain=singleManager->getDocumentViewer(math.strGUID);auto* plainView=plain?plain->docView():nullptr;
                check(plain&&waitFor([&]{return readyView(plainView,"ENDWIDTH");},20000),"production unencrypted independent window reads normally without biometric authorization");
                check(lockedView(mainView)&&lockedView(secondView),"opening a plain window grants neither protected window access");
                window.raise();window.activateWindow();[NSApp activateIgnoringOtherApps:YES];
                auto* f=mainView->findChild<WizUserCipherForm*>();auto* bio=f?f->findChild<WizBiometricUnlockWidget*>("cipherBiometricUnlock"):nullptr;
                auto* button=bio?bio->findChild<QPushButton*>("biometricUnlockStart"):nullptr;
                check(button&&button->isEnabled(),"main password page can explicitly start native read for cancellation test");
                if(!coordinationOnly){
                qInfo()<<"NATIVE_CANCEL_ONLY: no fingerprint needed; test cancels its own request.";
                button->click();check(bio->stage()==WizBiometricUnlockWidget::Stage::Working,"default native read is dispatched from actual main password page");
                QTimer::singleShot(500,bio,&WizBiometricUnlockWidget::cancel);
                check(waitFor([&]{return bio->stage()==WizBiometricUnlockWidget::Stage::Ready;},5000)
                      &&waitFor([]{return QThreadPool::globalInstance()->activeThreadCount()==0;},5000),"explicit cancellation drains the actual native read worker without a credential delivery");
                nativeReadCanceled=true;
                check(lockedView(mainView)&&lockedView(secondView),"canceled native request and late worker completion authorize neither protected window");
                }
                const auto activate=[&](QWidget* target){target->raise();target->activateWindow();[NSApp activateIgnoringOtherApps:YES];QElapsedTimer settled;settled.start();return waitFor([&]{return settled.elapsed()>=750&&app.applicationState()==Qt::ApplicationActive&&target->isActiveWindow();},5000);};
                check(activate(&window),"main test window settles active after system cancel before fresh password gesture");
                check(manualView(mainView)&&waitFor([&]{return readyView(mainView,"KEEP-ORIGINAL-BODY");},20000),"fresh synthetic password opens main view after canceled system read");
                check(lockedView(secondView),"main-view authorization is not inherited by another encrypted window");
                check(activate(separate),"independent test window is foreground before its own password gesture");
                const bool secondReady=manualView(secondView)&&waitFor([&]{return readyView(secondView,"OTHER-MULTI-BODY");},20000);
                if(!secondReady){
                    const auto diagnose=[&](WizDocumentView* v){return QJsonObject{{"loaded",v->noteLoaded()},{"readable",v->canReadCurrentNote()},
                        {"renderer",QJsonValue::fromVariant(js(v->web()->page(),"({url:location.href,setup:window.WizNativeEditorSetup&&WizNativeEditorSetup.state,body:document.body&&document.body.textContent,editor:!!document.querySelector('.wiz-editor-body')})"))}};};
                    writeNew(artifacts+"/second-view-diagnostic.json",QJsonDocument(QJsonObject{{"applicationActive",app.applicationState()==Qt::ApplicationActive},{"main",diagnose(mainView)},{"second",diagnose(secondView)},{"plain",diagnose(plainView)}}).toJson());
                    capture(separate,"failure-independent");
                }
                check(secondReady,"independent encrypted view obtains its own fresh manual authorization");
                check(mainView->canReadCurrentNote()&&secondView->canReadCurrentNote()&&plainView->canReadCurrentNote(),"independent authorizations coexist inside the actual three-window client");
                const auto mainTicket=mainView->web()->noteReadPermit(),secondTicket=secondView->web()->noteReadPermit();
                const auto dirty=[&](WizDocumentView* v,const QString& marker){v->setEditorMode(modeEditor);if(!waitFor([&]{return js(v->web()->page(),"!!document.querySelector('.wiz-editor-body[contenteditable=true]')").toBool();}))return false;
                    const bool inserted=js(v->web()->page(),"(function(){const b=document.querySelector('.wiz-editor-body');b.insertAdjacentHTML('beforeend','<p>"+marker+"</p>');return b.textContent.includes('"+marker+"');})()").toBool();v->web()->setModified(true);return inserted;};
                check(dirty(mainView,"QUIT-SAVED-SYNTHETIC")&&dirty(secondView,"SINGLE-MULTI-UNSAVED"),"both encrypted renderers hold distinct latest unsaved synthetic DOM");
                host.show();consent->setChecked(true);forgetButton->click();
                check(waitFor([&]{return management->stage()==WizBiometricManageWidget::Stage::Succeeded;})&&!ownedItem->load(),"actual management deletion completes for the unique synthetic scope");
                check(lockedView(mainView)&&lockedView(secondView)&&!mainTicket.current()&&!secondTicket.current(),"one scoped deletion retires both production windows and their original read permits");
                check(plainView->canReadCurrentNote(),"same-account plaintext independent window stays readable after scoped retirement");
                check(js(mainView->web()->page(),"document.body.textContent.includes('QUIT-SAVED-SYNTHETIC')").toBool()
                      &&js(secondView->web()->page(),"document.body.textContent.includes('SINGLE-MULTI-UNSAVED')").toBool()
                      &&!db.getCertPassword().isEmpty()&&db.hasCert(),"cross-window retirement preserves both dirty bodies and independent save credential/certificate");
                syntheticItemAbsent=forget(ownedScope,true)==Error::Missing;check(syntheticItemAbsent,"deleted synthetic keychain item is exactly absent");
                host.hide();
                QPointer<WizSingleDocumentViewer> separateGuard(separate);
                const bool independentClosed=separate->close();
                // This entire fixture is inside a timer callback. Its nested
                // waits do not unwind to the outer event loop to deliver the
                // WA_DeleteOnClose event; flush only the accepted close's own
                // posted deletion, never delete a window whose close refused.
                if(independentClosed)QCoreApplication::sendPostedEvents(separate,QEvent::DeferredDelete);
                check(waitFor([&]{return separateGuard.isNull();},20000)&&!singleManager->getDocumentViewer(other.strGUID),"locked dirty independent window closes only after production save and manager removal");
                const auto extracted=root.filePath("verify-single/");QDir().mkpath(extracted);
                check(db.documentToHtmlFile(other,extracted)&&bytes(extracted+"index.html").contains("SINGLE-MULTI-UNSAVED")
                      &&bytes(extracted+"index.html").contains("OTHER-MULTI-BODY")&&WizZiwReader::isEncryptedFile(db.getDocumentFileName(other.strGUID)),"independent close actually preserves latest body inside encrypted native archive");
                check(js(mainView->web()->page(),"document.body.textContent.includes('QUIT-SAVED-SYNTHETIC')").toBool()&&!mainView->canReadCurrentNote(),"other-note save completion does not reload or reauthorize locked main dirty view");
                QPointer<WizSingleDocumentViewer> plainGuard(plain);
                if(plain->close())QCoreApplication::sendPostedEvents(plain,QEvent::DeferredDelete);
                check(waitFor([&]{return plainGuard.isNull();},20000),"unmodified independent plain window closes through original path");
                exitRequested=true;window.on_actionExit_triggered();return;
            }
            edit->setText(password);consent->setChecked(true);enrollButton->click();
            check(waitFor([&]{return management->stage()==WizBiometricManageWidget::Stage::Conflict;})&&ownedItem->load(),"actual management duplicate reports conflict without overwriting or deleting");
            host.hide();window.raise();window.activateWindow();[NSApp activateIgnoringOtherApps:YES];
            WIZDOCUMENTDATAEX ex;ex=protectedNote;window.viewDocument(ex,false);
            const auto locked=[&]{auto* v=window.docView();auto* f=v?v->findChild<WizUserCipherForm*>():nullptr;return f&&f->isVisible()&&!v->canReadCurrentNote();};
            check(waitFor(locked),"reopened protected note requires fresh authorization despite cached password");
            auto* form=window.docView()->findChild<WizUserCipherForm*>();
            auto* biometric=form->findChild<WizBiometricUnlockWidget*>("cipherBiometricUnlock");
            auto* start=biometric?biometric->findChild<QPushButton*>("biometricUnlockStart"):nullptr;
            check(start&&start->isVisible()&&start->isEnabled(),"actual password page offers the native Touch ID action");
            biometric->cancel();check(locked()&&!window.docView()->canReadCurrentNote(),"cancel before read leaves actual note locked");
            QElapsedTimer focusSettled;focusSettled.start();
            check(waitFor([&]{return focusSettled.elapsed()>=350&&app.applicationState()==Qt::ApplicationActive&&window.isActiveWindow();},5000),"main window settles in active Cocoa state before explicit biometric gesture");
            if(!manualOnly)qInfo()<<"READY_FOR_TOUCH_ID: main-window synthetic note; fingerprint requested once.";
            QObject::connect(biometric,&WizBiometricUnlockWidget::stageChanged,&app,[biometric]{qInfo()<<"Main synthetic biometric stage"<<int(biometric->stage());});
            const auto manual=[&]{auto* f=window.docView()->findChild<WizUserCipherForm*>();auto* e=f?f->findChild<QLineEdit*>("editUserCipher"):nullptr;if(!e)return false;e->setText(password);return QMetaObject::invokeMethod(f,"onButtonOK_clicked",Qt::DirectConnection);};
            if(manualOnly){qInfo()<<"Focused save/quit continuation: no native authentication request.";manual();}
            else start->click();
            const bool opened=waitFor([&]{return ready()&&js(window.docView()->web()->page(),"document.body.textContent.includes('KEEP-ORIGINAL-BODY')").toBool();},90000);
            writeNew(artifacts+"/biometric-open-diagnostic.json",QJsonDocument(QJsonObject{{"widgetStage",int(biometric->stage())},{"canRead",window.docView()->canReadCurrentNote()},{"noteLoaded",window.docView()->noteLoaded()},{"passwordPageVisible",form->isVisible()},{"applicationActive",app.applicationState()==Qt::ApplicationActive},{"renderer",QJsonValue::fromVariant(js(window.docView()->web()->page(),"({ready:!!(window.WizNativeEditorSetup&&WizNativeEditorSetup.state==='ready'),bodyMarker:document.body.textContent.includes('KEEP-ORIGINAL-BODY')})"))}}).toJson());
            nativeAuthenticated=opened&&!manualOnly;
            check(opened,manualOnly?"focused continuation opens protected renderer with fresh synthetic password":"real system Touch ID opens actual protected main-window renderer through default password-page widget");
            check(bytes(db.getDocumentFileName(protectedNote.strGUID))==encryptedBefore,"opening does not rewrite original encrypted note archive");
            // Post only to THIS process's workspace notification center. This
            // verifies native subscription glue, not an actual device/session lock.
            [[NSWorkspace sharedWorkspace].notificationCenter postNotificationName:NSWorkspaceSessionDidResignActiveNotification object:nil];
            ++workspaceNotificationsPosted;
            check(waitFor(locked)&&!db.getCertPassword().isEmpty(),"workspace session notification retires main-window access and preserves save cache");
            check(manual()&&waitFor(ready,20000),"fresh manual certificate password reopens main window after re-lock");
            host.show();consent->setChecked(true);forgetButton->click();
            check(waitFor([&]{return management->stage()==WizBiometricManageWidget::Stage::Succeeded;})&&!ownedItem->load(),"actual management UI deletes only this synthetic remembered item");
            check(!window.docView()->canReadCurrentNote()&&WizBiometricNoteAccess::optedIn(window.userSettings(),ownedScope)
                  &&!db.getCertPassword().isEmpty(),"forgetting retires open grant but retains fresh-authorization policy and save cache");
            syntheticItemAbsent=forget(ownedScope,true)==Error::Missing;
            check(syntheticItemAbsent&&db.hasCert()&&bytes(db.getDocumentFileName(protectedNote.strGUID))==encryptedBefore,"exact item absence confirmed; certificate and encrypted archive remain intact");
            host.hide();window.raise();window.activateWindow();[NSApp activateIgnoringOtherApps:YES];
            window.viewDocument(ex,false);check(waitFor(locked),"after forgetting the main password page remains required");
            form=window.docView()->findChild<WizUserCipherForm*>();biometric=form->findChild<WizBiometricUnlockWidget*>("cipherBiometricUnlock");
            start=biometric?biometric->findChild<QPushButton*>("biometricUnlockStart"):nullptr;
            check(start&&start->isEnabled(),"explicit retry action remains available after deletion");start->click();
            check(waitFor([&]{auto* label=biometric->findChild<QLabel*>("biometricUnlockStatus");return label&&label->text().contains(QStringLiteral("没有此证书"));})&&locked(),"actual missing-item result returns to manual password without exposing note");
            check(manual()&&waitFor(ready,20000),"manual password still opens encrypted note after local memory removal");
            auto* web=window.docView()->web();window.docView()->setEditorMode(modeEditor);
            check(waitFor([&]{return js(web->page(),"!!document.querySelector('.wiz-editor-body[contenteditable=true]')").toBool();}),"actual protected renderer enters editor after manual fallback");
            check(js(web->page(),"(function(){const b=document.querySelector('.wiz-editor-body');b.insertAdjacentHTML('beforeend','<p>QUIT-SAVED-SYNTHETIC</p>');return b.textContent.includes('QUIT-SAVED-SYNTHETIC');})()").toBool(),"latest unsaved synthetic DOM added before lock and quit");web->setModified(true);
            // The first posted session notification deliberately did not change
            // the real session. Model its intervening active/resumed event so
            // the second independent notification is not correctly coalesced.
            WizSessionAccess::Monitor::applicationMonitor()->observeApplicationState(Qt::ApplicationActive);
            [[NSWorkspace sharedWorkspace].notificationCenter postNotificationName:NSWorkspaceWillSleepNotification object:nil];
            ++workspaceNotificationsPosted;
            auto dirty=std::make_shared<std::pair<bool,bool>>(false,false);
            web->isModified([dirty](bool modified){dirty->first=true;dirty->second=modified;});
            check(waitFor(locked)&&waitFor([dirty]{return dirty->first;})&&dirty->second&&js(web->page(),"document.body.textContent.includes('QUIT-SAVED-SYNTHETIC')").toBool(),"native sleep-notification route locks access without canceling dirty DOM or pending save");
            exitRequested=true;window.on_actionExit_triggered();return;
        }
        if(newNoteModes){
            window.userSettings().setNoteViewMode(viewmodeAlwaysReading);
            math.tCreated=QDateTime::currentDateTime();check(db.modifyDocumentInfo(math),"synthetic note has immediate creation timestamp");
            openNote(math);check(waitFor(ready,20000),"new note loads through actual MainWindow/View/Web");
            auto* view=window.docView();auto* web=view->web();
            const auto reopen=[&](const WIZDOCUMENTDATA& doc){
                WIZDOCUMENTDATAEX ex;ex=doc;window.viewDocument(ex,false);
                const QString marker=doc.strGUID==math.strGUID?"ENDWIDTH":"KEEP-ORIGINAL-BODY";
                // Host metadata changes before navigation completes. Old-page
                // setup readiness alone must not authorize the next command.
                return waitFor([&]{return view->note().strGUID==doc.strGUID&&ready()
                    &&js(web->page(),"document.body.textContent.includes('"+marker+"')").toBool();},20000);
            };
            const auto editable=[&]{return js(web->page(),"document.body.isContentEditable || !!document.querySelector('.wiz-editor-body[contenteditable=true]')").toBool();};
            writeNew(artifacts+"/new-note-modes.json",QJsonDocument(QJsonObject{{"hostEditing",view->isEditing()},{"pageEditable",editable()}}).toJson());
            check(view->isEditing()&&editable(),"immediate new-note host mode and renderer both start editing");
            view->setEditorMode(modeReader);
            check(waitFor([&]{return !view->isEditing()&&!editable()&&js(web->page(),"!!(window.MathJax&&MathJax.Hub&&MathJax.Hub.getAllJax().length>=4)").toBool();},20000),"single host transition enters reader and renders bare math");
            check(js(web->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"reader transition retains exact Markdown source");
            view->setEditorMode(modeEditor);check(waitFor([&]{return view->isEditing()&&editable();}),"single host transition returns to actual editor");
            check(js(web->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"return to editor retains exact Markdown source");
            bool saved=false;window.trySaveCurrentNote([&](const QVariant& ok){saved=ok.toBool();});
            check(waitFor([&]{return saved&&window.docSaver()->isIdleForTransfer();}),"new-note edit/read/edit finishes production save");
            protectedNote.tCreated=QDateTime::currentDateTime().addSecs(60);check(db.modifyDocumentInfo(protectedNote),"synthetic future creation timestamp stored");
            check(reopen(protectedNote)&&!view->isEditing()&&!editable(),"future timestamp does not force editor");
            math.tCreated=QDateTime::currentDateTime().addSecs(-60);check(db.modifyDocumentInfo(math),"synthetic existing creation timestamp stored");
            check(reopen(math)&&!view->isEditing()&&!editable(),"existing note respects reader preference");
            check(reopen(protectedNote),"quit fixture loads original synthetic HTML");
            view->setEditorMode(modeEditor);check(waitFor([&]{return editable();}),"quit fixture enters actual editable DOM");
            check(js(web->page(),"(function(){const b=document.querySelector('.wiz-editor-body');b.insertAdjacentHTML('beforeend','<p>QUIT-SAVED-SYNTHETIC</p>');return b.textContent.includes('QUIT-SAVED-SYNTHETIC');})()").toBool(),"quit fixture inserts latest synthetic DOM content");web->setModified(true);
            exitRequested=true;window.on_actionExit_triggered();return;
        }
        openNote(math);check(waitFor(ready,20000),"actual main window loads synthetic Markdown through production page initialization");
        writeNew(artifacts+"/initial-editor-diagnostic.json",QJsonDocument::fromVariant(js(window.docView()->web()->page(),"(function(){var r={body:document.body.isContentEditable,classes:document.body.className,setup:window.WizNativeEditorSetup&&WizNativeEditorSetup.state,initialized:!!window.WizNativeEditorInitialized};try{var h=WizEditor.getContentHtml();r.htmlType=typeof h;r.htmlLength=h&&h.length;r.modified=WizEditor.isModified();}catch(e){r.error=String(e);}return r;})()")).toJson());
        window.docView()->setEditorMode(modeReader);
        const bool rendered=waitFor([&]{return js(window.docView()->web()->page(),"!!(window.MathJax&&MathJax.Hub&&MathJax.Hub.getAllJax().length>=4&&document.getElementById('mjx-eqn-eq:front')&&document.getElementById('mjx-eqn-eq:align')&&document.querySelector('.wiz-math-reference'))").toBool();},20000);
        writeNew(artifacts+"/reader-dom.json",QJsonDocument::fromVariant(js(window.docView()->web()->page(),"({jax:window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().length:0,refs:Array.from(document.querySelectorAll('a[href]')).map(a=>({text:a.textContent,href:a.getAttribute('href')})),ids:Array.from(document.querySelectorAll('[id]')).map(a=>a.id).filter(s=>/eq:|thm:/.test(s)),source:WizEditor.getMarkdownSrc({unEscapeHtml:true})})")).toJson());
        check(rendered,"real reader renders bare environments and cross-references without added display delimiters");
        js(window.docView()->web()->page(),"window.scrollTo(0,0)");
        check(capture(&window,"math-reader"),"capture only the synthetic main-window widget for visual comparison");
        for(const auto& mode:QStringList{"wizPdfReadingWidth","wizPdfA4Width"}){
            auto* action=window.findChild<QAction*>(mode);check(action&&action->isEnabled(),"actual PDF width submenu is available: "+mode);
            chooserDestination=artifacts+"/"+(mode=="wizPdfA4Width"?"a4.pdf":"reading.pdf");const int before=chooserCount;action->trigger();
            check(waitFor([&]{return QFileInfo(chooserDestination).size()>1000;},20000)&&chooserCount==before+1,"actual submenu/chooser/renderer publishes PDF: "+mode);
            writeNew(artifacts+"/"+mode+"-print-status.json",QJsonDocument::fromVariant(js(window.docView()->web()->page(),"({status:globalThis.__wizPdfSvgLinks?.status(),frames:Array.from(document.querySelectorAll('.MathJax_SVG_Display .MathJax_SVG')).map(frame=>{const input=document.getElementById(frame.id.replace(/-Frame$/,''));const shape=(n,depth)=>n&&({tag:n.tagName,id:n.id,transform:n.getAttribute('transform'),children:depth?Array.from(n.children).map(c=>shape(c,depth-1)):n.children.length});return {input:input&&{type:input.type,text:input.textContent},tree:shape(frame.querySelector('svg'),5)};})})",QWebEngineScript::ApplicationWorld)).toJson());
            check(waitFor([&]{return js(window.docView()->web()->page(),"!document.querySelector('[data-wiz-pdf-snapshot]') && !document.body.isContentEditable").toBool();}),"reader remains restored after PDF export: "+mode);
        }
        chooserDestination=artifacts+"/source.md";window.on_actionSaveAsMarkdown_triggered();
        check(waitFor([&]{return QFileInfo(chooserDestination).exists();})&&QString::fromUtf8(bytes(chooserDestination)).contains("\\label{eq:front}"),"actual main Markdown export preserves original equation source");
        check(bytes(mathArchive)==mathBefore,"reader exports leave stored original math archive byte-identical");
        // Set only the synthetic account GUID after the avatar/no-account bootstrap.
        db.setMeta("Account","GUID","synthetic-foreground-account");password="Synthetic-foreground-certificate-2026";
        const auto generated=WizCertificateCreation::generate(password,password,{});const auto& c=generated.certificate;
        check(bool(generated)&&db.setUserCert(c.n,c.e,c.encryptedD,{})&&db.cacheCertPasswordLocally(password,c.n,c.e,c.encryptedD)&&db.encryptDocument(protectedNote),"actual database encrypts the new synthetic HTML note with an ephemeral test certificate");
        encryptedBefore=bytes(db.getDocumentFileName(protectedNote.strGUID));
        WizCommonApiEntry::setEnterpriseServerIP("https://synthetic-foreground.invalid/");
        const auto scope=WizBiometricNoteAccess::snapshot(db,db,WizCommonApiEntry::server());
        window.userSettings().set("BiometricUnlock",WizBiometricNoteAccess::policyIdentity(scope),"1");
        check(WizBiometricNoteAccess::policyIdentity(scope).size()==64&&WizBiometricNoteAccess::optedIn(window.userSettings(),scope),"synthetic opt-in has an explicit nonempty local server/account/library identity");
        openNote(protectedNote);
        check(waitFor([&]{auto* v=window.docView();auto* p=v?v->findChild<WizUserCipherForm*>():nullptr;return p&&p->isVisible()&&!v->canReadCurrentNote();}),"actual protected note opens at fresh password page despite warm save cache");
        const auto unlock=[&]{auto* form=window.docView()->findChild<WizUserCipherForm*>();auto* edit=form?form->findChild<QLineEdit*>("editUserCipher"):nullptr;if(!edit)return false;edit->setText(password);return QMetaObject::invokeMethod(form,"onButtonOK_clicked",Qt::DirectConnection);};
        check(unlock()&&waitFor(ready,20000),"manual test password unlocks actual protected editor without invoking Touch ID or Keychain");
        auto* view=window.docView();auto* web=view->web();view->setEditorMode(modeEditor);
        check(waitFor([&]{return js(web->page(),"!!document.querySelector('.wiz-editor-body')&&!!document.querySelector('.wiz-editor-body').isContentEditable").toBool();}),"actual protected HTML editor is editable before synthetic capture");
        const auto insert=[&]{QPixmap pix(160,90);pix.fill(QColor(30,110,190));web->editorCommandExecuteScreenShot();auto* helper=web->findChild<WizScreenShotHelper*>();if(!helper)return false;
            // Synchronous delivery retires helper/timer before the 200ms screen
            // grab can run. Never grab the desktop or create a screen recording.
            emit helper->screenShotCaptured(pix);return true;};
        const auto beforeImageCount=js(web->page(),"document.querySelectorAll('.wiz-editor-body img').length").toInt();
        check(insert()&&waitFor([&]{return window.isVisible()&&!view->canReadCurrentNote();}),"actual hide/restore path retains synthetic pixels and presents password again");
        check(js(web->page(),"document.querySelectorAll('.wiz-editor-body img').length").toInt()==beforeImageCount,"pending protected capture has not inserted image before fresh authorization");
        check(capture(&window,"capture-reauthorization"),"synthetic password page is visible after protected capture");
        check(unlock()&&waitFor([&]{return view->canReadCurrentNote()&&js(web->page(),"document.querySelectorAll('.wiz-editor-body img').length").toInt()==beforeImageCount+1;}),"actual fresh-password completion inserts one synthetic image through production renderer");
        check(capture(&window,"capture-inserted"),"actual protected editor paints the inserted synthetic image");
        writeNew(artifacts+"/save-dom-diagnostic.json",QJsonDocument::fromVariant(js(web->page(),"(function(){var r={body:document.body.isContentEditable,classes:document.body.className,setup:window.WizNativeEditorSetup&&WizNativeEditorSetup.state,initialized:!!window.WizNativeEditorInitialized,frames:document.querySelectorAll('iframe').length};try{var h=WizEditor.getContentHtml();r.htmlType=typeof h;r.htmlLength=h&&h.length;r.modified=WizEditor.isModified();}catch(e){r.error=String(e);}return r;})()")).toJson());
        bool saved=false;window.trySaveCurrentNote([&](const QVariant& ok){saved=ok.toBool();});
        check(waitFor([&]{return saved&&window.docSaver()->isIdleForTransfer();}),"actual main save callback and disk saver complete after screenshot insertion");
        const auto encryptedAfterCapture=bytes(db.getDocumentFileName(protectedNote.strGUID));
        check(WizZiwReader::isEncryptedFile(db.getDocumentFileName(protectedNote.strGUID)),"inserted image saves inside an encrypted note before permanent conversion");
        WizNoteInfoForm info(&window);info.setDocument(view->note());info.show();auto* toggle=info.findChild<QCheckBox*>("checkEncrypted");
        check(toggle&&toggle->isChecked(),"actual information popup reflects protected-note state");
        promptPassword="wrong synthetic";promptConsent=true;toggle->click();
        check(waitFor([&]{return passwordPrompts>=1&&confirmations>=1&&toggle->isChecked();})&&bytes(db.getDocumentFileName(protectedNote.strGUID))==encryptedAfterCapture,"actual information checkbox flow rejects wrong fresh password and restores protected state");
        promptPassword=password;promptConsent=false;toggle->click();
        check(waitFor([&]{return passwordPrompts>=2&&confirmations>=2&&toggle->isChecked();})&&bytes(db.getDocumentFileName(protectedNote.strGUID))==encryptedAfterCapture,"actual default-no confirmation refusal preserves encrypted note and popup state");
        promptConsent=true;toggle->click();
        const bool converted=waitFor([&]{WIZDOCUMENTDATA d;return db.documentFromGuid(protectedNote.strGUID,d)&&!d.nProtected&&successReports==1&&!toggle->isChecked();},20000);
        WIZDOCUMENTDATA actual;db.documentFromGuid(protectedNote.strGUID,actual);
        writeNew(artifacts+"/decryption-result-diagnostic.json",QJsonDocument(QJsonObject{{"protected",actual.nProtected!=0},{"checked",toggle->isChecked()},{"passwordPrompts",passwordPrompts},{"confirmations",confirmations},{"successReports",successReports},{"certificatePresent",db.hasCert()}}).toJson());
        check(converted,"actual information popup completes confirmed permanent decryption and reports success once");
        info.hide();check(waitFor(ready,20000),"actual current note reloads as ordinary plaintext without another password");
        const auto folder=QFileInfo(db.getDocumentFileName(protectedNote.strGUID)).absolutePath()+"/encrypted-backups";bool backup=false;
        for(const auto& f:QDir(folder).entryList(QDir::Files))backup|=bytes(folder+"/"+f)==encryptedAfterCapture;
        check(backup&&!WizZiwReader::isEncryptedFile(db.getDocumentFileName(protectedNote.strGUID)),"actual conversion leaves byte-identical encrypted backup and a readable native ZIP");
        check(capture(&window,"decrypted-note"),"actual reader paints original body and captured image after permanent conversion");
        // Leave a real dirty editor change for the normal save-first quit handler.
        view=window.docView();web=view->web();view->setEditorMode(modeEditor);
        check(waitFor([&]{return js(web->page(),"!!document.querySelector('.wiz-editor-body')&&document.querySelector('.wiz-editor-body').isContentEditable").toBool();}),"decrypted note enters actual editor for save-priority exit");
        js(web->page(),"document.querySelector('.wiz-editor-body').insertAdjacentHTML('beforeend','<p>QUIT-SAVED-SYNTHETIC</p>');");web->setModified(true);
        exitRequested=true;window.on_actionExit_triggered();
    }catch(const std::exception& e){
        qWarning()<<"Stopped focused native workflow:"<<e.what();
        // Failure is not a normal save-first quit acceptance. Exit this isolated
        // event loop directly so refused window closes cannot strand its owned
        // synthetic credential. The cleanup below still deletes only our item.
        QCoreApplication::exit(1);
    }});
    app.exec();dialogs.stop();window.hide();
    if(biometricMain){
        QThreadPool::globalInstance()->waitForDone();
        if(ownedItem->load()){
            const auto removed=WizBiometricKeychain::forget(ownedScope,true);
            syntheticItemAbsent=removed==WizBiometricKeychain::Error::None
                &&WizBiometricKeychain::forget(ownedScope,true)==WizBiometricKeychain::Error::Missing;
            results.append(QJsonObject{{"name","failure cleanup deletes only native-add-owned synthetic item"},{"passed",syntheticItemAbsent}});
            if(!syntheticItemAbsent)++failures;
        }
    }
    if(exitRequested){WIZDOCUMENTDATA d;const bool loaded=db.documentFromGuid(protectedNote.strGUID,d);const QString extracted=root.filePath("verify-exit/");QDir().mkpath(extracted);const bool exported=loaded&&db.documentToHtmlFile(d,extracted);
        const bool preserved=exported&&bytes(extracted+"index.html").contains("QUIT-SAVED-SYNTHETIC")&&bytes(extracted+"index.html").contains("KEEP-ORIGINAL-BODY");
        results.append(QJsonObject{{"name","normal quit saved latest dirty content before eleven-stage cleanup"},{"passed",preserved&&cleanup}});if(!preserved||!cleanup)++failures;
    }
    const QJsonObject result{{"focus",liveSession?"biometric-live-session":(coordinationOnly?"biometric-window-coordination":(multiWindow?"biometric-multi-window":(biometricMain?(manualOnly?"biometric-save-quit":"biometric-main-window"):(newNoteModes?"new-note-modes":"foreground-features"))))},{"syntheticOnly",true},{"platform",QGuiApplication::platformName()},{"actualMainWindow",true},{"actualRenderer",true},{"nativeSavePanel",false},{"physicalPointer",false},{"actualScreenCapture",false},{"nativeAuthentication",nativeAuthenticated},{"nativeReadCanceled",nativeReadCanceled},{"actualIndependentWindows",multiWindow},{"syntheticItemAbsent",syntheticItemAbsent},{"workspaceNotificationsPostedLocally",workspaceNotificationsPosted},{"actualDeviceSessionChanged",liveSession&&userConfirmedPhysicalLockAndUnlock},{"userConfirmedPhysicalLockAndUnlock",userConfirmedPhysicalLockAndUnlock},{"liveWorkspaceRetireNotifications",liveWorkspaceRetireNotifications},{"liveAppInactiveEvents",liveAppInactiveEvents},{"automaticDeviceOperation",false},{"realAccount",false},{"blockedWebRequests",interceptor.blocked.load()},{"exitRequested",exitRequested},{"originalCleanup",cleanup},{"checks",results},{"failures",failures}};
    const bool output=writeNew(artifacts+"/frontend-result.json",QJsonDocument(result).toJson());
    qInfo()<<"native-feature-acceptance checks"<<results.size()<<"failures"<<failures;
    for(auto* p:app.findChildren<QWebEngineProfile*>())p->setUrlRequestInterceptor(nullptr);
    WizDatabase::clearCertPassword();return output&&!failures?0:1;
}
