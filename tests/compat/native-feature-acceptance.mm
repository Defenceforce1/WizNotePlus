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
#include "share/WizLegacyLightAppearance.h"
#include "share/WizZiwReader.h"
#include "sync/WizApiEntry.h"
#include "sync/WizToken.h"
#include "sync/WizAvatarHost.h"
#include "gui/documentviewer/WizDocumentView.h"
#include "gui/documentviewer/WizDocumentWebView.h"
#include "gui/documentviewer/WizNoteInfoForm.h"
#include "widgets/WizScreenShotWidget.h"
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
#include <QMenu>
#include <QMessageBox>
#include <QNetworkProxy>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QStyleFactory>
#include <QTemporaryDir>
#include <QTimer>
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
}
int wizNativeFeatureAcceptance(int argc,char** argv){
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
    }catch(const std::exception& e){qWarning()<<"Stopped focused native workflow:"<<e.what();app.quit();}});
    app.exec();dialogs.stop();window.hide();
    if(exitRequested){WIZDOCUMENTDATA d;const bool loaded=db.documentFromGuid(protectedNote.strGUID,d);const QString extracted=root.filePath("verify-exit/");QDir().mkpath(extracted);const bool exported=loaded&&db.documentToHtmlFile(d,extracted);
        const bool preserved=exported&&bytes(extracted+"index.html").contains("QUIT-SAVED-SYNTHETIC")&&bytes(extracted+"index.html").contains("KEEP-ORIGINAL-BODY");
        results.append(QJsonObject{{"name","normal quit saved latest dirty content before eleven-stage cleanup"},{"passed",preserved&&cleanup}});if(!preserved||!cleanup)++failures;
    }
    const QJsonObject result{{"focus",newNoteModes?"new-note-modes":"foreground-features"},{"syntheticOnly",true},{"platform",QGuiApplication::platformName()},{"actualMainWindow",true},{"actualRenderer",true},{"nativeSavePanel",false},{"physicalPointer",false},{"actualScreenCapture",false},{"nativeAuthentication",false},{"realAccount",false},{"blockedWebRequests",interceptor.blocked.load()},{"exitRequested",exitRequested},{"originalCleanup",cleanup},{"checks",results},{"failures",failures}};
    const bool output=writeNew(artifacts+"/frontend-result.json",QJsonDocument(result).toJson());
    qInfo()<<"native-feature-acceptance checks"<<results.size()<<"failures"<<failures;
    for(auto* p:app.findChildren<QWebEngineProfile*>())p->setUrlRequestInterceptor(nullptr);
    WizDatabase::clearCertPassword();return output&&!failures?0:1;
}
