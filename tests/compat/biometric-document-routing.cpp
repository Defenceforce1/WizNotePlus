#include "share/WizBiometricNoteAccess.h"
#include "share/WizDocumentReadPermit.h"
#include "share/WizDocumentReadOutput.h"
#include "share/WizDocumentReadSave.h"
#include "share/WizThreads.h"
#include "share/WizQtCompat.h"
#include "share/WizMarkdownExportTransaction.h"
#include "share/WizPdfExport.h"
#include "share/WizSettings.h"
#include "share/WizGlobal.h"
#include "share/WizMisc.h"
#include "share/WizLocalFileUrl.h"
#include "share/WizZiwReader.h"
#include "database/WizDatabase.h"
#include "database/WizDatabaseManager.h"
#include "sync/WizApiEntry.h"
#include "WizUserCipherForm.h"
#include "WizFileExporter.h"
#include "widgets/FileExportWizard.h"
#include "utils/WizMisc.h"
#include "utils/ExternalEditorLauncher.h"
#include "utils/WizPathResolve.h"
#include "gui/documentviewer/DocumentLoaderSaver.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QHideEvent>
#include <QLineEdit>
#include <QLabel>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QTextEdit>
#include <QScopeGuard>
#include <QMimeData>
#include <QPushButton>
#include <QProgressBar>
#include <QWizard>
#include <QWizardPage>
#include <QMessageBox>
#include <QInputDialog>
#include <QAbstractButton>
#include <QAction>
#include <QColor>
#include <QLocale>
#include <QTreeWidget>
#include <QShowEvent>
#include <QSemaphore>
#include <QStackedWidget>
#include <QTemporaryDir>
#include <QTextDocument>
#include <QThread>
#include <QTimer>
#include <QWebEnginePage>
#include <QScreen>
#include <QStyle>
#include <quazip.h>
#include <quazipfile.h>
#include <quazip/JlCompress.h>
#include <atomic>
#include <stdexcept>
#include "share/jsoncpp/json/json.h"

namespace {
using namespace WizBiometricNoteAccess;
enum { modeReader, modeEditor };
struct MemoryClipboard {
    std::unique_ptr<QMimeData> data=std::make_unique<QMimeData>();int writes=0;
    const QMimeData* mimeData() const {return data.get();}
    void setMimeData(QMimeData* value){++writes;data.reset(value);}
    void seed(const QString& html){data=std::make_unique<QMimeData>();data->setHtml(html);data->setText("合成 clipboard");data->setData("application/x-synthetic","KEEP");writes=0;}
};
struct ClipboardApp {
    static MemoryClipboard* clipboard(){static MemoryClipboard value;return &value;}
};
struct ClipboardTimer {
    static inline std::vector<std::function<void()>> pending;
    static inline bool hold=true;static inline int fired=0,lastDelay=0;
    static void singleShot(int ms,std::function<void()> callback){
        lastDelay=ms;
        if(hold)pending.push_back(std::move(callback));
        else QTimer::singleShot(ms,[callback=std::move(callback)]{++fired;callback();});
    }
    static void fire(){auto callbacks=std::move(pending);pending.clear();for(auto& done:callbacks)done();}
};
struct ClipboardProcessor {
    static void processCopiedData();
    static void processCopiedData(const std::function<bool()>&);
};
#include WIZNOTE_CLIPBOARD_METHODS
class App final : public WizExplorerApp {
public:
    App(WizDatabaseManager& db,WizUserSettings& settings):db(db),settings(settings){}
    QWidget window; WizDatabaseManager& db; WizUserSettings& settings;
    QWidget* mainWindow() override {return &window;}
    QObject* object() override {return &window;}
    WizDatabaseManager& databaseManager() override {return db;}
    WizUserSettings& userSettings() override {return settings;}
    WizCategoryBaseView& category() override {throw std::logic_error("no category");}
};
// Only the shell and asynchronous save/loader collaborators are substitutes.
// Actual temporary DB, settings, password form, gate and extracted methods run.
class RoutingView;
struct ReadPage : QObject {
    QUrl address;std::function<void()> duringUrl;
    QUrl url(){const auto value=address;const auto hook=duringUrl;if(hook)hook();return value;}
    int copyActions=0,inspectActions=0;std::function<void()> duringInspect;
    std::vector<std::function<void(const QVariant&)>> scripts;
    std::vector<std::function<void(const QString&)>> html;
    std::vector<std::function<void(const QString&)>> plainText;
    QStringList commands;std::function<void()> duringScript;
    QStringList queryCommands;
    void runJavaScript(const QString& code,std::function<void(const QVariant&)> done){queryCommands.push_back(code);scripts.push_back(std::move(done));}
    void toHtml(std::function<void(const QString&)> done){html.push_back(std::move(done));}
    void toPlainText(std::function<void(const QString&)> done){plainText.push_back(std::move(done));}
    void runJavaScript(const QString& code){commands.push_back(code);const auto hook=duringScript;if(hook)hook();}
    void triggerAction(QWebEnginePage::WebAction type,bool=false){if(type==QWebEnginePage::Copy)++copyActions;if(type==QWebEnginePage::InspectElement){++inspectActions;const auto hook=duringInspect;if(hook)hook();}}
};
// Compile the actual typed query helper; substitute only its page class with
// the recording QObject above. document-async separately uses real WebEngine.
#define QWebEnginePage ReadPage
#include "share/WizDocumentUiQuery.h"
#undef QWebEnginePage
// Only the inspector collaborator records operations. The exact current base
// lifecycle methods below run with real QWidget ownership/deferred destruction.
struct InspectorPage {
    ReadPage* inspected=nullptr;
    static inline std::function<void(ReadPage*)> onBind;
    void setInspectedPage(ReadPage* value){inspected=value;const auto hook=onBind;if(hook)hook(value);}
};
struct InspectorWeb {InspectorPage source;InspectorPage* page(){return &source;}};
struct InspectorDialog : QWidget {
    InspectorWeb content;int shows=0,raises=0;
    static inline int constructed=0,destroyed=0;
    static inline std::function<void()> onConstruct,onTitle,onShow,onRaise;
    InspectorDialog(){++constructed;const auto hook=onConstruct;if(hook)hook();}
    ~InspectorDialog() override{++destroyed;}
    InspectorWeb* web(){return &content;}
    void setWindowTitle(const QString& title){QWidget::setWindowTitle(title);const auto hook=onTitle;if(hook)hook();}
    void show(){++shows;QWidget::show();const auto hook=onShow;if(hook)hook();}
    void raise(){++raises;const auto hook=onRaise;if(hook)hook();}
};
struct InspectorShell : QWidget {
    QPointer<InspectorDialog> m_devToolsWindow;
    quint64 m_devToolsGeneration=0;bool m_openingDevTools=false;
    ReadPage source;ReadPage* target=&source;std::function<void()> duringTitle;
    virtual ReadPage* page()const{return target;}
    virtual QString documentTitle(){const auto hook=duringTitle;if(hook)hook();return "Synthetic inspector";}
    virtual void openDevTools();void closeDevTools();
    void openDevToolsWithPermit(const std::function<bool()>&);
};
#include WIZNOTE_INSPECTOR_METHODS
struct Web : InspectorShell {
    explicit Web(WizDatabaseManager& manager):m_dbMgr(manager){}
    using Completion=std::function<void(const QVariant&)>;
    std::vector<Completion> pending;
    bool defer=false; QVariant reply=true; int saves=0,loads=0,reloads=0,closes=0;
    std::function<void(const WIZDOCUMENTDATA&)> handoff;
    QString body;
    QString m_currentNoteHtml;
    bool editing=false;std::function<void()> duringEditing;
    int documentsOpened=0,attachmentsOpened=0,saveActions=0,reloadActions=0;
    QString dispatchedUrl,dispatchedKb;
    QAction folderAction{this},reloadAction{this};
    enum {OpenTempFileLocation=1};
    QAction* viewAction(int){return &folderAction;}
    QAction* pageAction(QWebEnginePage::WebAction){return &reloadAction;}
    void setupWebActions();
    void onActionSaveTriggered(){++saveActions;}
    void handleReloadTriggered(){++reloadActions;}
    bool isEditing()const{const bool value=editing;const auto hook=duringEditing;if(hook)hook();return value;}
    bool isInternalUrl(const QUrl&);
    void onEditorLinkClicked(QUrl,QWebEnginePage::NavigationType,bool,ReadPage*);
    void viewDocumentByUrl(const QString& url){++documentsOpened;dispatchedUrl=url;}
    void viewAttachmentByUrl(const QString& kb,const QString& url){++attachmentsOpened;dispatchedUrl=url;dispatchedKb=kb;}
    RoutingView* owner=nullptr;
    WizDatabaseManager& m_dbMgr;
    QMap<QString,QString> m_mapFile;
    ReadPage sourcePage;
    ReadPage* activePage=&sourcePage;
    WizDocumentCallbackEpoch m_documentCallbacks;
    RoutingView* view() const {return owner;}
    ReadPage* page() const {return activePage;}
    WizDocumentCallbackEpoch::Ticket documentCallbackTicket() const {return m_documentCallbacks.ticket();}
    WizDocumentReadPermit noteReadPermit() const;
    QString getCurrentNoteHtml();
    void doCopy();void afterCopied();
    void openDevTools() override;
    bool m_bNewNoteTitleInited=false;
    int pasteRequests=0,focusRequests=0,focusSignals=0;std::function<void()> duringFocus;
    void onActionTriggered(QWebEnginePage::WebAction);void tryResetTitle();
    void doPaste();void onReturn();void undo();void redo();void editorFocus();
    void focusIn(){++focusSignals;}
    void setFocus(Qt::FocusReason reason){++focusRequests;QWidget::setFocus(reason);const auto hook=duringFocus;if(hook)hook();}
    quint64 m_contentChangeSequence=0;bool m_bContentsChanged=false;
    void setModified(bool);
    void editorCommandExecuteCommand(const QString&,const QString& =QString(),const QString& =QString(),const QString& =QString());
    void editorCommandExecuteParagraph(const QString&);void editorCommandExecuteInsertHtml(const QString&,bool);
    void editorCommandExecuteIndent();void editorCommandExecuteOutdent();void editorCommandExecuteLinkRemove();
    void editorCommandExecuteFontFamily(const QString&);void editorCommandExecuteFontSize(const QString&);
    void editorCommandExecuteBackColor(const QColor&);void editorCommandExecuteForeColor(const QColor&);
    void editorCommandExecuteBold();void editorCommandExecuteItalic();void editorCommandExecuteUnderLine();void editorCommandExecuteStrikeThrough();
    void editorCommandExecuteSubScript();void editorCommandExecuteSuperScript();
    void editorCommandExecuteJustifyLeft();void editorCommandExecuteJustifyRight();void editorCommandExecuteJustifyCenter();void editorCommandExecuteJustifyJustify();
    void editorCommandExecuteInsertOrderedList();void editorCommandExecuteInsertUnorderedList();void editorCommandExecuteTableInsert(int,int);
    void editorCommandExecuteInsertHorizontal();void editorCommandExecuteInsertCheckList();
    void editorCommandExecuteStartMarkup();void editorCommandExecuteStopMarkup();void editorExecJs(QString);void onViewMindMap(bool);
    void editorCommandExecuteInsertPainter();void editorCommandExecuteInsertDate();void editorCommandExecuteInsertTime();void editorCommandExecuteRemoveFormat();
    void editorCommandExecuteFormatPainterOn(bool);void editorCommandExecuteFormatPainterOff();void editorCommandExecuteInsertCode();
    void replaceCurrent(QString,QString);void replaceAndFindNext(QString,QString,bool);void replaceAll(QString,QString,bool);
    void saveAsHtml();
    void saveAsPlainMarkdown(QString&,std::function<void(QString)>);
    void saveAsPlainText(QString&,std::function<void(QString)>);
    void saveAsRenderedHtml(QString&,std::function<void(QString)>);
    bool onPasteCommand(){++pasteRequests;return false;}
    void trySaveDocument(const WIZDOCUMENTDATA& doc,bool,Completion done) {
        ++saves;if(defer)pending.push_back(std::move(done));else {
            if(reply.userType()==QMetaType::Bool&&reply.toBool()&&handoff)handoff(doc);
            done(reply);
        }
    }
    // Older routing cases isolate consumers by substituting their save result.
    // DiskWeb below instead compiles the actual adapter and actual FIFO helper.
    virtual void trySaveDocumentForRead(const WIZDOCUMENTDATA& doc,bool force,Completion done){trySaveDocument(doc,force,std::move(done));}
    void cancelLinkInsert(){} void cancelPdfExport(){} void clear(){body.clear();}
    void viewDocument(const WIZDOCUMENTDATA& doc,int){++loads;body="loaded:"+doc.strGUID;}
    void reloadNoteData(const WIZDOCUMENTDATA&){++reloads;body="reloaded";}
    void isPersonalDocumentChanged(){} void hasEditPermissionOnCurrentNoteChanged(){}
    void triggerPageAction(QWebEnginePage::WebAction){++closes;}
};
struct Title {void setNote(const WIZDOCUMENTDATA&,int,bool){}};
struct Info {void setDocument(const WIZDOCUMENTDATA&){}};
namespace WizMessageBox {
int messages=0;
void information(QWidget*,const QString&,const QString&){++messages;}
}
class RoutingView : public QWidget {
public:
    WizDatabaseManager& m_dbMgr; WizUserSettings& m_userSettings;
    Web* m_web; WizUserCipherForm* m_passwordView; QStackedWidget* m_stack;
    QWidget *m_docView,*m_blankView,*m_transitionView;
    Title title;Info info;Title* m_title=&title;Info* m_infoBar=&info;
    WIZDOCUMENTDATAEX m_note;
    bool m_noteLoaded=false,m_bLocked=false;
    int m_editorMode=modeReader,downloads=0,threadWaits=0,closedSignals=0;
    Gate m_biometricGate;WizDocumentCallbackEpoch m_openEpoch;
    quint64 m_openSequence=0;
    bool m_biometricEnabled=false,m_biometricPrepared=false,m_accessClosing=false,m_biometricReloadPending=false;
    RoutingView(App& app,WizBiometricUnlockWidget::Services services={})
        :m_dbMgr(app.db),m_userSettings(app.settings),m_web(new Web(app.db)),
         m_passwordView(new WizUserCipherForm(app,this,std::move(services))),m_stack(new QStackedWidget(this)),
         m_docView(new QWidget),m_blankView(new QWidget),m_transitionView(new QWidget) {
        m_web->setParent(m_docView);
        m_web->owner=this;
        for(auto* w:{m_docView,static_cast<QWidget*>(m_passwordView),m_blankView,m_transitionView})m_stack->addWidget(w);
        m_stack->setCurrentWidget(m_blankView);m_stack->resize(760,400);resize(760,400);
        QObject::connect(m_passwordView,&WizUserCipherForm::biometricCredentialReady,this,&RoutingView::onBiometricCredentialReady);
        QObject::connect(m_passwordView,&WizUserCipherForm::cipherCheckRequest,this,&RoutingView::onCipherCheckRequest);
    }
    ~RoutingView() override {m_passwordView->blockSignals(true);revokeBiometricAccess(true);}
    const WIZDOCUMENTDATAEX& note() const {return m_note;}
    Web* web() const {return m_web;}
    bool noteLoaded() const {return m_noteLoaded;}
    void initStat(const WIZDOCUMENTDATA&,bool edit){m_editorMode=edit?modeEditor:modeReader;}
    void stopDocumentEditingStatus(){} void sendDocumentEditingStatus(){}
    void downloadNoteFromServer(const WIZDOCUMENTDATA&){++downloads;}
    void titleChanged(const QString&){} void pageCloseRequested(){++closedSignals;}
    void waitForThread(){++threadWaits;}
    bool reload(){return m_dbMgr.db(m_note.strKbGUID).documentFromGuid(m_note.strGUID,m_note);}
    Context biometricContext() const;
    bool canReadCurrentNote() const;
    WizDocumentCallbackEpoch::Ticket noteReadTicket() const {return m_biometricGate.ticket();}
    bool acceptsNoteRead(const WizDocumentCallbackEpoch::Ticket&) const;
    void revokeBiometricAccess(bool closing=false);
    bool prepareEncryptedAccess(WizDatabase&);
    bool biometricRequestCurrent(const Context&,const Gate::Ticket&) const;
    bool completeBiometricUnlock(const QString&,const Context&,const Gate::Ticket&);
    void onBiometricCredentialReady();void onCipherCheckRequest();
    void viewNote(const WIZDOCUMENTDATAEX&,bool);void reviewCurrentNote();
    void loadNote(const WIZDOCUMENTDATAEX&);void reloadNote();
    void onCloseNoteRequested(RoutingView*);
    void waitForDone();bool waitForSave(bool force=false);
    void RequestClose(bool force=false);void handleWindowCloseRequest();
    void wordCount(std::function<void(const QString&)>);
    int titleResets=0;QString resetValue;std::function<void()> duringResetTitle;
    void resetTitle(const QString& value){++titleResets;resetValue=value;const auto hook=duringResetTitle;if(hook)hook();}
    void setEditorFocus();void onTitleReturnPressed();
    int typeChanges=0;void changeType(QString type){++typeChanges;m_note.strType=type;}
    void getMailSender(std::function<void(QString)>);
protected:
    void showEvent(QShowEvent*) override;void hideEvent(QHideEvent*) override;
};
#include WIZNOTE_ROUTING_METHODS
struct RawActionPageBase : QObject {
    int actions=0;
    void triggerAction(QWebEnginePage::WebAction,bool){++actions;}
};
struct ActionPageBase : RawActionPageBase {
    void triggerAction(QWebEnginePage::WebAction type,bool checked){RawActionPageBase::triggerAction(type,checked);if(type==QWebEnginePage::Copy)processCopiedData();}
    static void processCopiedData(){ClipboardProcessor::processCopiedData();}
    static void processCopiedData(const std::function<bool()>& read){ClipboardProcessor::processCopiedData(read);}
};
struct ActionPage : ActionPageBase {
    Web* m_engineView;int notifications=0;
    explicit ActionPage(Web* web):m_engineView(web){}
    void triggerAction(QWebEnginePage::WebAction,bool=false);
    void actionTriggered(QWebEnginePage::WebAction){++notifications;}
};
namespace QFileDialog {
QString destination;std::function<void()> beforeReturn;int requests=0;
QString getSaveFileName(QWidget*,const QString&,const QString&,const QString&){
    ++requests;auto action=std::move(beforeReturn);beforeReturn={};const auto result=destination;
    if(action)action();return result;
}
}
#include WIZNOTE_READ_METHODS
struct DiskWeb : Web {
    using Web::Web;
    QPointer<WizDocumentSaverThread> diskSaver;
    WizDocumentSaverThread* readDocumentSaver() const {return diskSaver.data();}
    void trySaveDocumentForRead(const WIZDOCUMENTDATA&,bool,std::function<void(const QVariant&)>) override;
};
#include WIZNOTE_DISK_READ_METHOD
// No process or real file watcher is launched. The exact current request body
// uses the real temporary DB, converter and atomic output, but a launch recorder.
class Launcher : public QObject {
public:
    explicit Launcher(WizDatabaseManager& manager):m_dbMgr(manager){}
    WizDatabaseManager& m_dbMgr;
    QString destination,launchedFile,requestedTitle;WIZDOCUMENTDATAEX launchedNote;
    int launches=0;
    QString makeValidCacheFileName(const QString& title,const QDir&){requestedTitle=title;return destination;}
    void startExternalEditor(QString file,const WizExternalEditorData&,const WIZDOCUMENTDATAEX& note){
        ++launches;launchedFile=file;launchedNote=note;
    }
    void viewNoteInExternalEditor(RoutingView*,const WizExternalEditorData&,const WIZDOCUMENTDATAEX&);
};
#include WIZNOTE_EXTERNAL_METHOD
// Only delay the worker's start; queue, disk saves, failure tracking, fences and
// waitForDone are the actual product implementation, not copied test methods.
class HeldSaver final : public WizDocumentSaverThread {
public:
    using WizDocumentSaverThread::WizDocumentSaverThread;
    QSemaphore entered,release;
    std::atomic_bool rescued{false};
protected:
    void run() override {
        entered.release();if(!release.tryAcquire(1,3000))rescued=true;
        WizDocumentSaverThread::run();
    }
};
struct Read final : WizBiometricUnlockWidget::Operation {
    QString password;std::atomic<int>* reads;
    Read(QString p,std::atomic<int>* n):password(std::move(p)),reads(n){}
    WizBiometricKeychain::Result read(const WizBiometricKeychain::Scope&) override {++*reads;return {WizBiometricKeychain::Error::None,password};}
    void cancel() noexcept override {}
};
bool until(const std::function<bool()>& test,int ms=3000){
    QElapsedTimer clock;clock.start();
    while(!test()&&clock.elapsed()<ms){QEventLoop loop;QTimer::singleShot(5,&loop,&QEventLoop::quit);loop.exec();}
    return test();
}

struct ShareRequest {QString url;explicit ShareRequest(QString value):url(std::move(value)){};};
struct ShareReply : QObject {
    using QObject::QObject;enum {NoError=0};int reads=0;int failure=0;QByteArray body="{\"return_code\":200}";
    int error() const{return failure;}QString errorString() const{return "synthetic transport error";}
    QByteArray readAll(){++reads;return body;}
};
struct ShareNetwork : QObject {
    using QObject::QObject;int requests=0;ShareReply* last=nullptr;
    ShareReply* get(const ShareRequest&){++requests;last=new ShareReply(this);return last;}
};
struct ShareToken {
    static inline int requests=0;static inline std::function<void()> during;
    static QString token(){++requests;if(during)during();return "synthetic-share-token";}
};
struct ShareEndpoint {
    static inline int requests=0;static inline std::function<void()> during;
    static QString mailShareUrl(const QString&,const QString&){++requests;if(during)during();return "https://synthetic.invalid/never-requested";}
};
struct ShareUi {
    QLineEdit *lineEdit_subject,*lineEdit_to;QComboBox* comboBox_replyTo;QTextEdit* textEdit_notes;
    QCheckBox *checkBox_saveNotes,*checkBox_sendMeCopy;QLabel* labelInfo;
    explicit ShareUi(QWidget* p):lineEdit_subject(new QLineEdit(p)),lineEdit_to(new QLineEdit(p)),comboBox_replyTo(new QComboBox(p)),
        textEdit_notes(new QTextEdit(p)),checkBox_saveNotes(new QCheckBox(p)),checkBox_sendMeCopy(new QCheckBox(p)),labelInfo(new QLabel(p)){}
};
class ShareShell : public QDialog {
public:
    ShareUi fields;ShareUi* ui=&fields;WIZDOCUMENTDATA m_note,m_sentNote;App& m_app;
    ShareNetwork* m_net=nullptr;WizDocumentReadPermit m_read;QPointer<ShareReply> m_request;
    WizDocumentCallbackEpoch m_shareCallbacks;QString m_sentComment,m_sentTo;bool m_sentInsert=false,m_sending=false;
    int comments=0;QString commentNote,commentText;std::function<void()> onComment;
    explicit ShareShell(App& app):fields(this),m_app(app){}
    void bind(const WIZDOCUMENTDATA& note,const WizDocumentReadPermit& read){m_note=note;m_read=read;}
    void connect(QObject*,const char*,const char*){} // no transport signals; tests deliver each reply explicitly
    void setNote(const WIZDOCUMENTDATA&,const QString&);
    void setNote(const WIZDOCUMENTDATA&,const QString&,const WizDocumentReadPermit&);
    void done(int) override;
    bool isInsertCommentToNote() const;QString getCommentsText() const;
    void on_toolButton_send_clicked();QString getExInfo();
    void on_mailShare_finished(int,const QString&);void processReturnMessage(const QString&,int&,QString&);
    void saveContacts();void sendEmails();void on_networkFinished(ShareReply*);void on_networkError(const QString&);
    void insertCommentToNoteRequest(const QString& guid,const QString& text){++comments;commentNote=guid;commentText=text;if(onComment)onComment();}
};
enum {codeOK=200,codeErrorParam=322,codeErrorFile=3330,codeErrorSize=3370,codeErrorEncrypt=3371,codeErrorEmail=3372,codeErrorFrequent=429,codeErrorServer=500};
#define EMAIL_CONTACTS "EMAILCONTACTS"
#include WIZNOTE_SHARE_METHODS
#undef EMAIL_CONTACTS

struct BatchWait {
    static inline int documents=0,attachments=0;
    static inline std::function<void()> duringDocument,duringAttachment;
    static bool document(WizDatabase& db,const WIZDOCUMENTDATA& doc){
        ++documents;if(duringDocument)duringDocument();
        return db.isObjectDataDownloaded(doc.strGUID,"document")&&QFile::exists(db.getDocumentFileName(doc.strGUID));
    }
    static bool attachment(WizDatabase& db,const WIZDOCUMENTATTACHMENTDATAEX& att){
        ++attachments;if(duringAttachment)duringAttachment();
        return QFile::exists(db.getAttachmentFileName(att.strGUID));
    }
};
// Same exporter declarations, without moc: only blocking waits are replaced.
class BatchExporter : public QObject {
public:
    explicit BatchExporter(WizDatabaseManager&,QObject* =nullptr);
    using ExportFormat=WizFileExporter::ExportFormat;
    static constexpr auto HTML=WizFileExporter::HTML,Markdown=WizFileExporter::Markdown;
    bool exportNote(const WIZDOCUMENTDATA&,const QString&,ExportFormat,bool=false,bool=true,bool=false,QString* =nullptr);
    bool exportNote(const WIZDOCUMENTDATA&,const QString&,ExportFormat,bool,bool,bool,QString*,std::function<bool()>);
    bool exportAttachment(const WIZDOCUMENTATTACHMENTDATAEX&,const QString&);
    bool exportAttachment(const WIZDOCUMENTATTACHMENTDATAEX&,const QString&,std::function<bool()>);
private:
    bool extractMarkdownToFile(const QString&,const QString&);
    bool writeDocumentInfoToJsonFile(const WIZDOCUMENTDATA&,const QString&);
    bool compressDocumentFolder(const QString&,const std::function<bool()>&,bool=true);
    WizDatabaseManager& m_dbMgr;
};
#define GUIDLEN 38
#include WIZNOTE_BATCH_METHODS
#undef GUIDLEN

struct BatchPrompt {
    static inline int calls=0;static inline bool accepted=true;static inline QString password;
    static inline std::function<void()> during;
    static QString getText(QWidget*,const QString&,const QString&,QLineEdit::EchoMode,const QString&,bool* ok){
        ++calls;if(during)during();*ok=accepted;return password;
    }
};
struct BatchMessage {
    using StandardButton=QMessageBox::StandardButton;
    static constexpr auto Ok=QMessageBox::Ok,Abort=QMessageBox::Abort;
    static inline int errors=0,infos=0;static inline StandardButton answer=Ok;
    static inline std::function<void()> during;
    static StandardButton critical(QWidget*,const QString&,const QString&,QMessageBox::StandardButtons,StandardButton){
        ++errors;if(during)during();return answer;
    }
    static void information(QWidget*,const QString&,const QString&){++infos;}
};
struct BatchDesktop {
    static inline int opens=0;static inline bool result=true;static inline std::function<void()> during;
    static bool openUrl(const QUrl&){++opens;if(during)during();return result;}
};
struct BatchEvents {
    static inline std::function<void()> during;
    static void processEvents(){if(during)during();}
};
class BatchPage : public QWizardPage {
public:
    App& m_app;WizDatabaseManager& m_dbMgr;QProgressBar* m_progress;QTextEdit* m_details;
    bool m_cancel=false;BatchExporter* m_exporter;
    bool m_exporting=false,m_finished=false,m_restartPending=false;WizDocumentCallbackEpoch m_exportCallbacks;
    QMap<QString,QVariant> fields;int queuedStarts=0;
    explicit BatchPage(App& app):m_app(app),m_dbMgr(app.databaseManager()),m_progress(new QProgressBar(this)),
        m_details(new QTextEdit(this)),m_exporter(new BatchExporter(m_dbMgr,this)){}
    QVariant field(const QString& name)const{return fields.value(name);}
    void initialized(){++queuedStarts;}
    void initializePage() override;void cleanupPage() override;bool isComplete()const override;
    void handleCancelBtnClicked();void insertLog(const QString&);void handleExportFile();
};
#include WIZNOTE_BATCH_PAGE_METHODS

struct ReadActionsBase {static void setupWebActions(){}};
struct ReadDesktop {
    static inline QList<QUrl> opened;
    static bool openUrl(const QUrl& url){opened.push_back(url);return true;}
};
#include WIZNOTE_HIDDEN_READ_METHODS
#include WIZNOTE_EDITOR_COMMAND_METHODS
struct ToolbarAnalyzer {
    static ToolbarAnalyzer& getAnalyzer(){static ToolbarAnalyzer value;return value;}
    static inline QStringList events;
    void logAction(const QString& text){events.push_back(text);}
};
struct ToolbarDiagnostics {static inline QStringList messages;static void log(const QString& value){messages.push_back(value);}};
struct ToolbarClock {static QDate date(){return QDate(2020,2,3);}static QTime time(){return QTime(4,5,6);}};
#include WIZNOTE_TOOLBAR_METHODS
}

int wizBiometricDocumentRoutingSelfTest(int argc,char** argv)
{
    QTemporaryDir root;if(!root.isValid())return 2;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE",root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME",root.filePath("cache").toUtf8());
    qputenv("XDG_CONFIG_HOME",root.filePath("config").toUtf8());
    qputenv("TMPDIR",root.filePath("tmp").toUtf8());
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    QSettings ui(root.filePath("ui.ini"),QSettings::IniFormat),global(root.filePath("global.ini"),QSettings::IniFormat);
    ui.setValue("Theme/Name","default");WizGlobal::setSettings(&ui);WizGlobal::setGlobalSettings(&global);
    qInfo()<<"Extracted DocumentView SHA"<<WIZNOTE_ROUTING_SOURCE_SHA;
    qInfo()<<"Extracted read adapter SHA"<<WIZNOTE_READ_SOURCE_SHA;
    qInfo()<<"Extracted external editor SHA"<<WIZNOTE_EXTERNAL_SOURCE_SHA;
    qInfo()<<"Extracted clipboard helper SHA"<<WIZNOTE_CLIPBOARD_SOURCE_SHA;
    qInfo()<<"Extracted share dialog SHA"<<WIZNOTE_SHARE_SOURCE_SHA;
    qInfo()<<"Extracted batch exporter SHA"<<WIZNOTE_BATCH_SOURCE_SHA;
    qInfo()<<"Extracted batch page SHA"<<WIZNOTE_BATCH_PAGE_SOURCE_SHA;
    int checks=0,failures=0;
    const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;return ok;};
    WizDatabaseManager manager("synthetic-routing-only");
    if(!check(manager.open(),"routing test opens only an isolated actual database"))return 1;
    auto& db=manager.db();WizUserSettings settings(db);App explorer(manager,settings);
    db.setMeta("Account","GUID","synthetic-routing-account");
    WizCommonApiEntry::setEnterpriseServerIP("https://synthetic-routing.invalid/");
    const QString password="Synthetic-routing-password-2026";
    const auto generated=WizCertificateCreation::generate(password,password,{});
    const auto& cert=generated.certificate;
    if(!check(generated && db.setUserCert(cert.n,cert.e,cert.encryptedD,{})
        && db.cacheCertPasswordLocally(password,cert.n,cert.e,cert.encryptedD),"synthetic certificate and warm save cache are prepared locally"))return 1;
    const QString plain=root.filePath("plain.ziw");
    QuaZip zip(plain);bool zipped=zip.open(QuaZip::mdCreate);
    if(zipped){QuaZipFile file(&zip);zipped=file.open(QIODevice::WriteOnly,QuaZipNewInfo("index.html"));
        if(zipped){zipped=file.write("<html><body><a href='index_files/fixture.txt'>synthetic only</a></body></html>")>0;file.close();}
        if(zipped){zipped=file.open(QIODevice::WriteOnly,QuaZipNewInfo("index_files/fixture.txt"));
            if(zipped){zipped=file.write("synthetic resource")>0;file.close();}}
        zip.close();}
    WizZiwReader reader;reader.setDatabase(&db);reader.setRSAKeys(cert.n.toUtf8(),cert.e.toUtf8(),cert.encryptedD.toUtf8(),{});
    const QString encrypted=root.filePath("encrypted.ziw");
    if(!check(zipped&&reader.encryptFileToFile(plain,encrypted),"actual native reader encrypts a synthetic archive without a real note"))return 1;
    WIZDOCUMENTDATAEX a,b,c;
    const auto create=[&](const QString& title,int protectedFlag,WIZDOCUMENTDATAEX& doc){
        return db.createDocument(title,"synthetic.ziw","/synthetic/",{},protectedFlag,doc)
            && QFile::copy(protectedFlag?encrypted:plain,db.getDocumentFileName(doc.strGUID))
            && db.setObjectDataDownloaded(doc.strGUID,"document",true);
    };
    if(!check(create("a",1,a)&&create("b",1,b)&&create("c",0,c),"three downloaded synthetic notes are stored in the temporary database"))return 1;
    const auto scope=snapshot(db,db,WizCommonApiEntry::server());const auto policy=policyIdentity(scope);
    std::atomic<int> reads{0};
    RoutingView view(explorer,{[&]{return std::make_shared<Read>(password,&reads);},nullptr});
    view.show();app.processEvents();
    view.m_web->reply=false;view.viewNote(a,false);
    check(view.m_web->loads==1&&view.m_stack->currentWidget()==view.m_docView&&!view.m_biometricEnabled,
          "default-off actual opening body keeps legacy warm-cache behavior and unloaded false save result");
    settings.set("BiometricUnlock",policy,"1");view.m_web->reply=true;view.viewNote(a,false);
    check(view.m_web->loads==1&&view.m_biometricEnabled&&view.m_stack->currentWidget()==view.m_passwordView&&reads==0,
          "opted-in actual opening requires fresh proof despite warm cache and never automatically reads");
    auto submit=[&](RoutingView& target,const QString& value=QString()){
        target.m_passwordView->findChild<QLineEdit*>("editUserCipher")->setText(value.isEmpty()?password:value);
        target.m_passwordView->onButtonOK_clicked();
    };
    submit(view,"wrong");
    check(view.m_web->loads==1&&WizMessageBox::messages==1&&db.getCertPassword()==password,
          "opted-in manual failure stays local preserves saves and does not open a note");
    submit(view);
    check(view.m_web->loads==2&&view.m_stack->currentWidget()==view.m_docView&&!view.m_biometricGate.requiresAuthorization(true),
          "actual manual slot grants a freshly validated current opening and loads it once");
    view.reviewCurrentNote();
    check(view.m_stack->currentWidget()==view.m_docView&&view.m_web->loads==2,"reviewing the same authorized opening does not discard or reauthenticate it");
    view.viewNote(a,false);
    check(view.m_web->loads==2&&view.m_stack->currentWidget()==view.m_passwordView,"explicit reopening of the same note cannot reuse its previous opening permission");
    auto* bio=view.m_passwordView->findChild<WizBiometricUnlockWidget*>();
    bio->findChild<QPushButton*>("biometricUnlockStart")->click();
    check(until([&]{return view.m_web->loads==3;}),"actual biometric completion slot accepts one simulated proof and opens only the current note");
    check(reads==1&&view.m_passwordView->findChild<QLineEdit*>("editUserCipher")->text().isEmpty(),"biometric path never fills the manual password editor");
    view.m_web->body="unsaved synthetic editor";view.hide();
    check(view.m_biometricGate.requiresAuthorization(true)&&view.m_stack->currentWidget()==view.m_passwordView
        &&db.getCertPassword()==password&&view.m_noteLoaded,"hiding revokes access but retains the dirty editor and save cache");
    view.show();app.processEvents();submit(view);
    check(view.m_web->loads==3&&view.m_web->body=="unsaved synthetic editor"&&view.m_stack->currentWidget()==view.m_docView,
          "fresh proof after show restores the existing dirty DOM without reloading it");
    const auto oldContext=view.biometricContext();const auto oldTicket=view.m_biometricGate.ticket();
    view.viewNote(b,false);
    check(!view.completeBiometricUnlock(password,oldContext,oldTicket)&&view.m_web->loads==3&&view.note().strGUID==b.strGUID,
          "a stale completion from another note cannot open or reload the new note");
    const auto current=view.biometricContext();const auto ticket=view.m_biometricGate.ticket();
    db.setMeta("Account","GUID","other-synthetic-account");
    check(!view.completeBiometricUnlock(password,current,ticket)&&view.m_web->loads==3,"changed live account rejects an old successful proof");
    db.setMeta("Account","GUID","synthetic-routing-account");
    const auto replacement=WizCertificateCreation::generate(password,password,{});
    db.setUserCert(replacement.certificate.n,replacement.certificate.e,replacement.certificate.encryptedD,{});
    check(!view.completeBiometricUnlock(password,current,ticket)&&view.m_web->loads==3&&optedIn(settings,view.biometricContext().scope),
          "changed certificate rejects old proof without disabling the explicit policy");
    db.setUserCert(cert.n,cert.e,cert.encryptedD,{});view.revokeBiometricAccess();view.prepareEncryptedAccess(db);submit(view);
    check(view.m_web->loads==4&&view.note().strGUID==b.strGUID,"restored current local certificate permits a new explicit manual proof");
    view.revokeBiometricAccess();view.reloadNote();
    check(view.m_web->reloads==0&&view.m_biometricReloadPending&&view.m_stack->currentWidget()==view.m_passwordView,
          "actual reload path defers reading behind a revoked biometric gate");
    submit(view);
    check(view.m_web->reloads==1&&!view.m_biometricReloadPending,"a requested reload resumes once only after fresh proof");
    const int previousLoads=view.m_web->loads;view.m_web->reply=false;view.viewNote(a,false);
    check(view.note().strGUID==b.strGUID&&view.m_web->loads==previousLoads&&view.m_stack->currentWidget()==view.m_passwordView,
          "save failure preserves the old note and conceals it without loading the requested note");
    view.m_web->reply=true;submit(view);view.m_web->defer=true;
    view.viewNote(a,false);view.viewNote(c,false);
    auto first=view.m_web->pending[0],second=view.m_web->pending[1];view.m_web->pending.clear();
    first(true);
    check(view.note().strGUID==b.strGUID&&view.m_web->loads==previousLoads,"older successful save completion cannot navigate after a newer request");
    second(true);
    check(view.note().strGUID==c.strGUID&&view.m_web->loads==previousLoads+1&&!view.m_biometricEnabled,
          "latest save completion opens the plain note without carrying over the old biometric policy");
    view.m_web->defer=false;const int plainReloads=view.m_web->reloads;view.reloadNote();
    check(view.m_web->reloads==plainReloads+1&&view.m_stack->currentWidget()==view.m_docView,
          "plain-note reload is not gated merely because its knowledge base has an encryption opt-in");
    view.viewNote(a,false);submit(view);
    const auto serverContext=view.biometricContext();const auto serverTicket=view.m_biometricGate.ticket();
    WizCommonApiEntry::setEnterpriseServerIP("https://other-synthetic.invalid/");
    check(!view.completeBiometricUnlock(password,serverContext,serverTicket),"changing the live server invalidates an otherwise valid proof");
    WizCommonApiEntry::setEnterpriseServerIP("https://synthetic-routing.invalid/");
    WizDatabase::clearCertPassword();view.reviewCurrentNote();
    check(view.m_stack->currentWidget()==view.m_passwordView&&view.m_biometricGate.requiresAuthorization(false),
          "loss of the shared password cache requires fresh proof even for a previously authorized opening");
    const auto loadsBeforeCacheRestore=view.m_web->loads;submit(view);
    check(db.getCertPassword()==password&&view.m_web->loads==loadsBeforeCacheRestore,
          "manual cache restoration does not reload an already open editor");
    RoutingView unrelated(explorer);const auto closeTicket=view.m_biometricGate.ticket();view.onCloseNoteRequested(&unrelated);
    check(WizDocumentCallbackEpoch::current(closeTicket)&&!view.m_accessClosing,"another document window close cannot revoke this opening");
    view.m_web->defer=true;view.viewNote(b,false);auto afterClose=view.m_web->pending.back();view.m_web->pending.clear();
    const int beforeClose=view.m_web->loads;view.onCloseNoteRequested(&view);afterClose(true);
    check(view.m_accessClosing&&view.m_stack->currentWidget()==view.m_blankView&&view.m_web->loads==beforeClose,
          "closing cancels pending UI continuation while the save completion itself remains callable");
    view.m_web->defer=false;view.viewNote(a,false);submit(view);view.m_web->body="dirty close protection";
    view.m_web->reply=false;view.RequestClose(false);
    check(!view.m_accessClosing&&view.m_web->closes==0&&view.m_web->body=="dirty close protection"&&db.getCertPassword()==password,
          "failed close snapshot keeps the window and original dirty content available for retry");
    view.m_web->reply=true;submit(view);view.RequestClose(false);
    check(view.m_accessClosing&&view.m_web->closes==1&&view.m_biometricGate.requiresAuthorization(true),
          "successful close snapshot precedes Chromium close and leaves authentication revoked");
    {
        auto* owner=new RoutingView(explorer);owner->m_web->defer=true;owner->viewNote(a,false);
        auto done=owner->m_web->pending.back();QPointer<RoutingView> guard(owner);delete owner;done(false);
        check(!guard,"save callback retained outside its destroyed shell safely drops only the UI continuation");
    }
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);bool routed=false;
        auto* widget=owner.m_passwordView->findChild<WizBiometricUnlockWidget*>();
        QObject::connect(widget,&WizBiometricUnlockWidget::stageChanged,&owner,[&]{if(!routed){routed=true;owner.viewNote(c,false);}});
        owner.viewNote(b,false);
        check(routed&&owner.note().strGUID==c.strGUID&&owner.m_web->loads==1,
              "navigation reentered during cancellation wins over the obsolete outer opening");
    }
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);bool routed=false;int manualSignals=0;
        auto* widget=owner.m_passwordView->findChild<WizBiometricUnlockWidget*>();
        QObject::connect(owner.m_passwordView,&WizUserCipherForm::cipherCheckRequest,&owner,[&]{++manualSignals;});
        QObject::connect(widget,&WizBiometricUnlockWidget::stageChanged,&owner,[&]{if(!routed){routed=true;owner.viewNote(c,false);}});
        submit(owner);
        check(routed&&manualSignals==0&&owner.note().strGUID==c.strGUID&&owner.m_stack->currentWidget()==owner.m_docView,
              "manual submission cannot migrate into a new context reentered during cancellation");
    }
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        const int saves=owner.m_web->saves;owner.waitForDone();
        check(owner.m_accessClosing&&owner.m_web->saves==saves+1&&owner.threadWaits==1
            &&owner.m_biometricGate.requiresAuthorization(true)&&db.getCertPassword()==password,
              "actual cleanup method revokes authentication but still completes the existing save and thread wait");
    }
    {
        check(!WizDocumentReadPermit{}.current(),"empty plaintext read permit is denied without an owner or page");
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        const auto read=owner.m_web->noteReadPermit();
        check(owner.canReadCurrentNote()&&read.current(),"actual read adapter permits a freshly authorized current note");
        check(!owner.acceptsNoteRead(unrelated.noteReadTicket()),"read tickets from another window cannot authorize this view");
        const QList<QWebEnginePage::WebAction> outputs{QWebEnginePage::Copy,QWebEnginePage::Cut,
            QWebEnginePage::CopyLinkToClipboard,QWebEnginePage::CopyImageToClipboard,
            QWebEnginePage::CopyImageUrlToClipboard,QWebEnginePage::CopyMediaUrlToClipboard,
            QWebEnginePage::DownloadLinkToDisk,QWebEnginePage::DownloadImageToDisk,
            QWebEnginePage::DownloadMediaToDisk,QWebEnginePage::SavePage,QWebEnginePage::ViewSource};
        ActionPage actions(owner.m_web);
        for(auto action:outputs)actions.triggerAction(action);
        check(actions.actions==outputs.size()&&actions.notifications==outputs.size(),
              "actual action-dispatch body preserves eleven authorized clipboard and output actions");
        const auto saveBarrier=owner.m_web->m_documentCallbacks.ticket();
        owner.m_web->body="unsaved protected content";owner.hide();
        check(owner.m_noteLoaded&&!owner.canReadCurrentNote()&&!read.current()&&db.getCertPassword()==password,
              "a warm save cache and loaded hidden DOM do not grant a plaintext read");
        for(auto action:outputs)actions.triggerAction(action);
        check(actions.actions==outputs.size()&&actions.notifications==outputs.size(),
              "actual dispatch rejects all eleven protected actions before base dispatch or notification");
        actions.triggerAction(QWebEnginePage::RequestClose);
        check(actions.actions==outputs.size()+1,"revoked clipboard permission does not block Chromium close action");
        check(WizDocumentCallbackEpoch::current(saveBarrier)&&owner.m_web->body=="unsaved protected content",
              "revoking read permission preserves the independent save ticket and unsaved DOM");
        owner.show();submit(owner);
        check(!read.current()&&owner.m_web->noteReadPermit().current(),
              "reauthentication cannot revive an already revoked read permit");
        const QString output=root.filePath("guarded-output.pdf");
        const QByteArray before="existing synthetic output";
        {QFile file(output);check(file.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&file.write(before)==before.size(),
              "old-output sentinel is created only inside the synthetic root");}
        const auto readBytes=[&]{QFile file(output);return file.open(QIODevice::ReadOnly)?file.readAll():QByteArray();};
        // Only the final byte writer, NOT a PDF renderer/geometry acceptance.
        const QByteArray pdf="%PDF-1.4\nsynthetic writer fixture\n%%EOF\n";
        const auto permitted=owner.m_web->noteReadPermit();
        const auto delayedWrite=[permitted,output,pdf]{QString error;return permitted.current()&&WizPdfExport::writeFile(output,pdf,error);};
        owner.hide();
        check(!delayedWrite()&&readBytes()==before,"late output callback after relock cannot replace an old output");
        owner.show();submit(owner);
        check(!delayedWrite()&&readBytes()==before,"unlocking the same note again cannot revive an older output callback");
        const auto fresh=owner.m_web->noteReadPermit();QString error;
        check(fresh.current()&&WizPdfExport::writeFile(output,pdf,error)&&readBytes()==pdf,
              "a fresh current permit can reach the original atomic byte writer");
        ReadPage replacementPage;owner.m_web->activePage=&replacementPage;
        check(!fresh.current(),"replacement of the source page rejects a pending plaintext read");
        owner.m_web->activePage=&owner.m_web->sourcePage;
        const auto documentRead=owner.m_web->noteReadPermit();owner.m_web->m_documentCallbacks.invalidate();
        check(!documentRead.current(),"a new document epoch rejects an otherwise live authorized read");
        owner.revokeBiometricAccess();owner.prepareEncryptedAccess(db);submit(owner);
        const auto accountRead=owner.m_web->noteReadPermit();db.setMeta("Account","GUID","other-synthetic-account");
        check(!accountRead.current(),"live account change rejects pending read permission despite the same cached password");
        db.setMeta("Account","GUID","synthetic-routing-account");
        owner.revokeBiometricAccess();owner.prepareEncryptedAccess(db);submit(owner);
        const auto certificateRead=owner.m_web->noteReadPermit();
        db.setUserCert(replacement.certificate.n,replacement.certificate.e,replacement.certificate.encryptedD,{});
        check(!certificateRead.current(),"live certificate change rejects an authorized read snapshot");
        db.setUserCert(cert.n,cert.e,cert.encryptedD,{});
        owner.revokeBiometricAccess();owner.prepareEncryptedAccess(db);submit(owner);
        const auto cacheRead=owner.m_web->noteReadPermit();WizDatabase::clearCertPassword();
        check(!cacheRead.current(),"loss of the password cache also revokes pending read permission");
        owner.prepareEncryptedAccess(db);submit(owner);
        owner.viewNote(c,false);
        check(owner.canReadCurrentNote()&&owner.m_web->noteReadPermit().current(),"ordinary plain note remains readable under a knowledge-base opt-in");
        owner.onCloseNoteRequested(&owner);
        check(!owner.canReadCurrentNote()&&!owner.m_web->noteReadPermit().current(),"a closed view cannot expose its retained plaintext buffer");
    }
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        const auto setup=[&](ShareShell& share){share.bind(owner.note(),owner.m_web->noteReadPermit());
            share.ui->lineEdit_to->setText("original@example.test");share.ui->textEdit_notes->setPlainText("original comment");
            share.ui->checkBox_saveNotes->setChecked(true);settings.set("EMAILCONTACTS","KEEP CONTACTS");};
        ShareShell current(explorer);setup(current);current.sendEmails();
        if(!check(current.m_net&&current.m_net->last,"share fixture records its synthetic request without a real network manager"))return 1;
        current.on_networkFinished(current.m_net->last);
        check(current.comments==1&&current.commentNote==a.strGUID&&current.commentText=="original%20comment"
            &&current.result()==QDialog::Accepted,"current successful share reply keeps its original local comment and close behavior");
        ShareShell locked(explorer);setup(locked);owner.hide();const int tokenBefore=ShareToken::requests,endpointBefore=ShareEndpoint::requests;
        locked.sendEmails();
        check(!locked.m_net&&ShareToken::requests==tokenBefore&&ShareEndpoint::requests==endpointBefore,
              "locked share dialog refuses before token acquisition, endpoint lookup or request creation");
        owner.show();submit(owner);
        ShareShell late(explorer);setup(late);late.sendEmails();
        if(!check(late.m_net&&late.m_net->last,"late share fixture holds a successful synthetic response"))return 1;
        auto* response=late.m_net->last;owner.hide();late.on_networkFinished(response);
        check(response->reads==0&&late.comments==0&&late.result()!=QDialog::Accepted&&settings.get("EMAILCONTACTS")=="KEEP CONTACTS",
              "share success after relock neither reads response body nor saves contacts, inserts comments or accepts the dialog");
        owner.show();submit(owner);
        ShareShell reauthenticated(explorer);setup(reauthenticated);reauthenticated.sendEmails();
        if(!check(reauthenticated.m_net&&reauthenticated.m_net->last,"reauthentication share fixture records its original request"))return 1;
        response=reauthenticated.m_net->last;owner.hide();owner.show();submit(owner);reauthenticated.on_networkFinished(response);
        check(response->reads==0&&reauthenticated.comments==0&&settings.get("EMAILCONTACTS")=="KEEP CONTACTS",
              "fresh authorization cannot revive an old open share dialog request");
        ShareShell duplicate(explorer);setup(duplicate);duplicate.sendEmails();duplicate.sendEmails();
        check(duplicate.m_net&&duplicate.m_net->requests==1,"one pending share request suppresses a duplicate send click");
        ShareShell unrelated(explorer);setup(unrelated);unrelated.sendEmails();
        auto* other=new ShareReply(&unrelated);unrelated.on_networkFinished(other);
        check(other->reads==0&&unrelated.comments==0,"a response not owned by the pending share request cannot update note or contacts");
        ShareShell edited(explorer);setup(edited);edited.sendEmails();
        if(!check(edited.m_net&&edited.m_net->last,"edited-fields fixture records the sent share request"))return 1;
        edited.ui->lineEdit_to->setText("later@example.test");edited.ui->textEdit_notes->setPlainText("later comment");
        edited.on_networkFinished(edited.m_net->last);
        check(edited.commentText=="original%20comment"&&settings.get("EMAILCONTACTS").contains("original@example.test")
            &&!settings.get("EMAILCONTACTS").contains("later@example.test"),
              "success bookkeeping uses the sent recipient and comment rather than fields edited while awaiting reply");
        edited.on_networkFinished(edited.m_net->last);
        check(edited.comments==1,"one completed share reply cannot be consumed twice before deferred deletion");
        ShareShell duringToken(explorer);setup(duringToken);const int endpoints=ShareEndpoint::requests;
        ShareToken::during=[&]{owner.hide();};duringToken.sendEmails();ShareToken::during={};
        check(!duringToken.m_net&&!duringToken.m_sending&&ShareEndpoint::requests==endpoints,
              "revocation during token preparation stops before endpoint lookup and releases the preparing state");
        owner.show();submit(owner);
        ShareShell duringEndpoint(explorer);setup(duringEndpoint);ShareEndpoint::during=[&]{owner.hide();};
        duringEndpoint.sendEmails();ShareEndpoint::during={};
        check(!duringEndpoint.m_net&&!duringEndpoint.m_sending,"revocation during endpoint preparation stops before creating a mail request");
        owner.show();submit(owner);
        ShareShell nestedSend(explorer);setup(nestedSend);const int tokens=ShareToken::requests;
        ShareToken::during=[&]{nestedSend.sendEmails();};nestedSend.sendEmails();ShareToken::during={};
        check(nestedSend.m_net&&nestedSend.m_net->requests==1&&ShareToken::requests==tokens+1&&!nestedSend.m_sending,
              "nested send during preparation cannot issue duplicate token or transport work");
        nestedSend.onComment=[&]{nestedSend.sendEmails();};nestedSend.on_networkFinished(nestedSend.m_net->last);
        check(nestedSend.comments==1&&nestedSend.m_net->requests==1&&!nestedSend.m_sending,
              "nested send while delivering a successful reply is suppressed until processing finishes");
        for(const auto& body:std::vector<QByteArray>{"not JSON","{}","{\"return_code\":\"200\"}","{\"return_code\":3371,\"return_message\":\"encrypted notes unsupported\"}"}) {
            ShareShell malformed(explorer);setup(malformed);malformed.sendEmails();
            if(!check(malformed.m_net&&malformed.m_net->last,"invalid-response fixture records one synthetic request"))return 1;
            malformed.m_net->last->body=body;malformed.on_networkFinished(malformed.m_net->last);
            check(malformed.comments==0&&malformed.result()!=QDialog::Accepted&&settings.get("EMAILCONTACTS")=="KEEP CONTACTS",
                  "malformed or service-refused response never becomes success bookkeeping or note insertion");
        }
        ShareShell networkError(explorer);setup(networkError);networkError.sendEmails();
        if(!check(networkError.m_net&&networkError.m_net->last,"transport-error fixture records one synthetic request"))return 1;
        auto* failed=networkError.m_net->last;failed->failure=1;networkError.on_networkFinished(failed);
        check(failed->reads==0&&networkError.comments==0&&!networkError.m_request&&!networkError.m_sending,
              "transport error finishes local pending state without reading a success body or inserting comments");
        networkError.sendEmails();check(networkError.m_net->requests==2,"an explicit retry after failed transport is a new request rather than an automatic retry");
        ShareShell signalLock(explorer);setup(signalLock);signalLock.sendEmails();signalLock.onComment=[&]{owner.hide();};
        if(!check(signalLock.m_net&&signalLock.m_net->last,"comment-signal fixture records one synthetic request"))return 1;
        signalLock.on_networkFinished(signalLock.m_net->last);
        check(signalLock.comments==1&&signalLock.result()!=QDialog::Accepted&&!signalLock.m_sending,
              "revocation inside an already-authorized local comment notification prevents later dialog acceptance");
        owner.show();submit(owner);
        ShareShell reused(explorer);setup(reused);reused.setNote(owner.note(),"first@example.test",owner.m_web->noteReadPermit());reused.sendEmails();
        if(!check(reused.m_net&&reused.m_net->last,"rebound-note fixture records its first synthetic request"))return 1;
        auto* old=reused.m_net->last;owner.viewNote(b,false);submit(owner);
        reused.setNote(owner.note(),"second@example.test",owner.m_web->noteReadPermit());reused.sendEmails();
        auto* fresh=reused.m_net->last;reused.on_networkFinished(old);
        check(old!=fresh&&old->reads==0&&reused.comments==0&&reused.m_request==fresh,
              "rebinding a dialog rejects its old reply without clearing the new note's pending request");
        reused.on_networkFinished(fresh);
        check(reused.comments==1&&reused.commentNote==b.strGUID&&reused.result()==QDialog::Accepted,
              "the newly bound note alone receives its own successful request continuation");
        ShareShell absent(explorer);absent.setNote(b,"legacy@example.test");const int noToken=ShareToken::requests;absent.sendEmails();
        check(!absent.m_net&&ShareToken::requests==noToken,"missing explicit share permission fails before even synthetic token acquisition");
        ShareShell cancelled(explorer);setup(cancelled);cancelled.sendEmails();
        if(!check(cancelled.m_net&&cancelled.m_net->last,"cancelled share fixture has a pending synthetic response"))return 1;
        auto* cancelledReply=cancelled.m_net->last;cancelled.reject();cancelled.on_networkFinished(cancelledReply);
        check(cancelledReply->reads==0&&cancelled.comments==0&&settings.get("EMAILCONTACTS")=="KEEP CONTACTS"
            &&!cancelled.m_read.current(),"actual QDialog rejection revokes the share continuation before a late successful reply");
        check(db.getCertPassword()==password,"share request revocation and failures never clear the original password-for-save cache");
    }
    {
        ClipboardTimer::pending.clear();auto* clipboard=ClipboardApp::clipboard();
        const QString html="<p>合成 clipboard HTML</p>";
        const QString prefix="<meta content=\"text/html; charset=utf-8\" http-equiv=\"Content-Type\">";
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        auto* web=owner.m_web;web->body="unsaved copy DOM";
        const auto saveTicket=web->m_documentCallbacks.ticket();ActionPage actions(web);
        clipboard->seed(html);actions.triggerAction(QWebEnginePage::Copy);
        check(actions.actions==1&&actions.notifications==1&&ClipboardTimer::pending.size()==1&&ClipboardTimer::lastDelay==500,
              "current note copy dispatches once and schedules exactly one memory-only encoding fix");
        ClipboardTimer::fire();
        check(clipboard->writes==1&&clipboard->mimeData()->html()==prefix+html
            &&clipboard->mimeData()->text()=="合成 clipboard"&&clipboard->mimeData()->data("application/x-synthetic")=="KEEP",
              "current copied-data helper retains the original UTF8 HTML prefix and every other MIME format");
        clipboard->seed(html);actions.triggerAction(QWebEnginePage::Copy);owner.hide();ClipboardTimer::fire();
        check(clipboard->writes==0&&clipboard->mimeData()->html()==html,
              "a note copy timer cannot rewrite even the memory clipboard after relock");
        owner.show();submit(owner);clipboard->seed(html);actions.triggerAction(QWebEnginePage::Copy);
        owner.hide();owner.show();submit(owner);ClipboardTimer::fire();
        check(clipboard->writes==0,"reauthentication does not revive a pending old copied-data timer");
        clipboard->seed(html);actions.triggerAction(QWebEnginePage::Copy);ReadPage anotherPage;
        web->activePage=&anotherPage;ClipboardTimer::fire();web->activePage=&web->sourcePage;
        check(clipboard->writes==0,"page replacement revokes the old note copy timer while its owner survives");
        clipboard->seed(html);web->afterCopied();owner.hide();ClipboardTimer::fire();
        check(clipboard->writes==0,"legacy afterCopied bridge timer also captures permission from its current invocation");
        const int dispatched=web->sourcePage.copyActions;web->doCopy();web->afterCopied();
        check(web->sourcePage.copyActions==dispatched&&ClipboardTimer::pending.empty()&&clipboard->writes==0,
              "locked direct-copy and afterCopied entries dispatch no copy or delayed clipboard processing");
        ClipboardTimer::pending.clear();
        check(web->body=="unsaved copy DOM"&&WizDocumentCallbackEpoch::current(saveTicket)&&db.getCertPassword()==password,
              "copy revocation never clears dirty DOM, save ticket, saved password or existing clipboard contents");
        owner.show();submit(owner);clipboard->seed(html);actions.triggerAction(QWebEnginePage::Copy);
        web->m_documentCallbacks.invalidate();ClipboardTimer::fire();
        check(clipboard->writes==0,"document generation change rejects delayed copied-data processing");
        auto* gone=new RoutingView(explorer);gone->show();gone->viewNote(a,false);submit(*gone);
        clipboard->seed(html);gone->m_web->afterCopied();delete gone;ClipboardTimer::fire();
        check(clipboard->writes==0,"destroyed note owner cannot normalize clipboard through an outstanding bridge timer");
        const int direct=web->sourcePage.copyActions;web->doCopy();
        check(web->sourcePage.copyActions==direct+1,"current direct doCopy entry retains its normal single page-action dispatch");
        clipboard->seed(html);web->afterCopied();
        check(ClipboardTimer::pending.size()==1,"current afterCopied entry schedules one guarded normalization");
        ClipboardTimer::fire();check(clipboard->writes==1&&clipboard->mimeData()->html()==prefix+html,
              "current bridge normalization preserves the original encoding fix without reading a system clipboard");
        clipboard->seed(html);int validations=0;
        ClipboardProcessor::processCopiedData([&]{return ++validations==1;});ClipboardTimer::fire();
        check(validations==2&&clipboard->writes==0&&clipboard->mimeData()->html()==html,
              "permission revoked during MIME snapshot cancels the final clipboard commit");
        clipboard->seed(html);web->afterCopied();db.setMeta("Account","GUID","other-copy-account");ClipboardTimer::fire();
        check(clipboard->writes==0,"account change invalidates delayed bridge normalization despite the same cached password");
        db.setMeta("Account","GUID","synthetic-routing-account");owner.revokeBiometricAccess();owner.prepareEncryptedAccess(db);submit(owner);
        clipboard->seed(html);web->afterCopied();
        db.setUserCert(replacement.certificate.n,replacement.certificate.e,replacement.certificate.encryptedD,{});ClipboardTimer::fire();
        check(clipboard->writes==0,"certificate replacement rejects the old delayed copied-data permit");
        db.setUserCert(cert.n,cert.e,cert.encryptedD,{});owner.revokeBiometricAccess();owner.prepareEncryptedAccess(db);submit(owner);
        ClipboardTimer::hold=false;clipboard->seed(html);int fired=ClipboardTimer::fired;
        QElapsedTimer elapsed;elapsed.start();web->afterCopied();const bool completed=until([&]{return ClipboardTimer::fired>fired;});
        check(completed&&elapsed.elapsed()>=400&&ClipboardTimer::lastDelay==500&&clipboard->writes==1
            &&clipboard->mimeData()->html()==prefix+html,"actual Qt timer performs the permitted 500 ms encoding fix using only memory MIME data");
        clipboard->seed(html);fired=ClipboardTimer::fired;web->afterCopied();owner.hide();
        const bool revoked=until([&]{return ClipboardTimer::fired>fired;});
        check(revoked&&clipboard->writes==0&&clipboard->mimeData()->html()==html,
              "actual Qt timer after relock leaves memory clipboard contents untouched");
        ClipboardTimer::hold=true;owner.show();submit(owner);
        clipboard->seed(html);ClipboardProcessor::processCopiedData();ClipboardTimer::fire();
        check(clipboard->writes==1&&clipboard->mimeData()->html()==prefix+html,"ordinary browser no-argument normalization retains its established compatibility behavior");
    }
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        auto* web=owner.m_web;web->body="retained dirty DOM";web->m_currentNoteHtml="<html>retained source cache</html>";
        const auto saveTicket=web->m_documentCallbacks.ticket();int delivered=0;QString value;
        const auto reply=[&](const QString& text){++delivered;value=text;};
        const auto valid=[](QString text){return QVariantMap{{"ok",true},{"value",text}};};
        const auto take=[&] {
            if(web->sourcePage.scripts.empty())return std::function<void(const QVariant&)>();
            auto result=web->sourcePage.scripts.back();web->sourcePage.scripts.clear();return result;
        };
        check(web->getCurrentNoteHtml()==web->m_currentNoteHtml,"authorized HTML getter retains the existing source-cache bytes");
        owner.wordCount(reply);auto words=take();
        if(!check(bool(words),"word-count fixture captures the actual typed query callback"))return 1;
        words(valid("words: 12"));
        check(delivered==1&&value=="words: 12","current word count delivers its valid string without saving or clearing content");
        owner.getMailSender(reply);auto sender=take();
        if(!check(bool(sender),"sender fixture captures the actual typed query callback"))return 1;
        sender(valid("sender@example.test"));
        check(delivered==2&&value=="sender@example.test","current sender query preserves its valid string result");
        owner.wordCount(reply);words=take();owner.getMailSender(reply);sender=take();
        if(!check(bool(words)&&bool(sender),"pending UI reads are captured before relocking"))return 1;
        owner.hide();
        check(web->getCurrentNoteHtml().isEmpty()&&web->m_currentNoteHtml=="<html>retained source cache</html>",
              "locked public HTML getter returns no body but never clears the independent save cache");
        owner.wordCount(reply);owner.getMailSender(reply);
        check(web->sourcePage.scripts.empty(),"locked word-count and sender entries refuse before page query dispatch");
        web->sourcePage.scripts.clear();words(valid("late words"));sender(valid("late@example.test"));
        check(delivered==2,"UI query replies arriving after relock are discarded rather than published");
        owner.show();submit(owner);words(valid("revived words"));sender(valid("revived@example.test"));
        check(delivered==2,"fresh authorization cannot revive old word-count or sender requests");
        owner.wordCount(reply);words=take();owner.getMailSender(reply);sender=take();
        if(!check(bool(words)&&bool(sender),"page-replacement fixtures have current pending UI reads"))return 1;
        ReadPage replacementPage;web->activePage=&replacementPage;words(valid("other-page words"));sender(valid("other@example.test"));
        check(delivered==2,"UI replies from a replaced page do not expose the previous note");web->activePage=&web->sourcePage;
        owner.getMailSender(reply);sender=take();
        if(!check(bool(sender),"live-account-change fixture has a pending sender query"))return 1;
        db.setMeta("Account","GUID","other-ui-account");sender(valid("changed@example.test"));
        check(delivered==2,"account context change rejects pending sender result despite a warm password cache");
        db.setMeta("Account","GUID","synthetic-routing-account");owner.revokeBiometricAccess();owner.prepareEncryptedAccess(db);submit(owner);
        check(web->getCurrentNoteHtml()==web->m_currentNoteHtml&&web->body=="retained dirty DOM"
            &&WizDocumentCallbackEpoch::current(saveTicket)&&db.getCertPassword()==password,
              "UI read revocation leaves dirty DOM, source cache, original save ticket and password-for-save intact");
        owner.wordCount({});owner.getMailSender({});
        check(web->sourcePage.scripts.empty(),"empty UI callbacks are refused without dispatch or exceptions");
        for(const auto& invalid:std::vector<QVariant>{QVariant(),true,7,QString("words"),
            QVariantMap{{"ok","true"},{"value","forged"}},QVariantMap{{"ok",true},{"value",7}},
            QVariantMap{{"ok",false},{"value","failed"}}}) {
            owner.wordCount(reply);words=take();owner.getMailSender(reply);sender=take();
            if(!check(bool(words)&&bool(sender),"typed UI failure fixtures each hold their own pending result"))return 1;
            words(invalid);sender(invalid);
            check(delivered==2,"typed query failure or non-string result never publishes word count or title fallback");
        }
        owner.wordCount(reply);words=take();
        if(!check(bool(words),"valid empty word-count fixture has a pending result"))return 1;
        words(valid(QString()));check(delivered==3&&value.isEmpty(),"valid empty word-count string remains a successful UI response");
        const auto originalTitle=owner.m_note.strTitle;owner.m_note.strTitle="From first@example.test";
        owner.getMailSender(reply);sender=take();
        if(!check(bool(sender),"empty sender fallback fixture has a pending result"))return 1;
        owner.m_note.strTitle="Renamed second@example.test";sender(valid(QString()));
        check(delivered==4&&value=="first@example.test","valid empty sender uses the captured title fallback rather than a later rename");
        owner.m_note.strTitle=originalTitle;
        owner.wordCount(reply);words=take();
        if(!check(bool(words),"absent-page fixture has a pending result before detaching its page"))return 1;
        web->activePage=nullptr;words(valid("detached words"));owner.wordCount(reply);owner.getMailSender(reply);
        check(delivered==4&&web->getCurrentNoteHtml().isEmpty()&&web->sourcePage.scripts.empty(),
              "absent page blocks the body getter, new UI queries and a late current-looking result");
        web->activePage=&web->sourcePage;
        owner.m_noteLoaded=false;owner.wordCount(reply);owner.getMailSender(reply);
        check(web->getCurrentNoteHtml().isEmpty()&&web->sourcePage.scripts.empty(),"unloaded note does not expose its retained source or dispatch UI reads");
        owner.m_noteLoaded=true;
        check(web->getCurrentNoteHtml()==web->m_currentNoteHtml,"completed current load keeps source getter available for the existing editor contract");
        owner.wordCount(reply);words=take();
        if(!check(bool(words),"document-generation fixture has a current pending UI result"))return 1;
        web->m_documentCallbacks.invalidate();words(valid("obsolete-generation words"));
        check(delivered==4,"document generation change drops a UI query even if the same page and password survive");
        owner.getMailSender(reply);sender=take();
        if(!check(bool(sender),"certificate-change fixture has a current pending sender result"))return 1;
        db.setUserCert(replacement.certificate.n,replacement.certificate.e,replacement.certificate.encryptedD,{});
        sender(valid("changed-cert@example.test"));
        check(delivered==4&&web->getCurrentNoteHtml().isEmpty(),"certificate replacement revokes pending sender and body getter without clearing cached source");
        db.setUserCert(cert.n,cert.e,cert.encryptedD,{});owner.revokeBiometricAccess();owner.prepareEncryptedAccess(db);submit(owner);
        owner.m_accessClosing=true;owner.wordCount(reply);owner.getMailSender(reply);
        check(web->sourcePage.scripts.empty()&&web->getCurrentNoteHtml().isEmpty(),"closing view refuses new transient reads without blocking its independent saves");
        owner.m_accessClosing=false;
        check(web->m_currentNoteHtml=="<html>retained source cache</html>"&&web->body=="retained dirty DOM"&&db.getCertPassword()==password,
              "all transient UI failure paths preserve source cache, dirty DOM and original password-for-save");
    }
    {
        auto* owner=new RoutingView(explorer);owner->show();owner->viewNote(a,false);submit(*owner);
        int delivered=0;owner->wordCount([&](const QString&){++delivered;});
        if(!check(!owner->m_web->sourcePage.scripts.empty(),"destroyed UI owner fixture captures its pending typed result"))return 1;
        auto result=owner->m_web->sourcePage.scripts.back();delete owner;
        result(QVariantMap{{"ok",true},{"value","destroyed words"}});
        check(delivered==0,"destroyed UI owner and page discard their late query without retaining a window");
    }
    {
        auto* owner=new RoutingView(explorer);owner->show();owner->viewNote(a,false);submit(*owner);
        const auto read=owner->m_web->noteReadPermit();check(read.current(),"owner-destruction fixture begins with an authorized read");
        delete owner;check(!read.current(),"destroyed owner safely rejects pending plaintext read without retaining the window");
    }
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        int callbacks=0;QString reported;
        const auto done=[&](QString path){++callbacks;reported=path;};
        const auto bytes=[](const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();};
        const auto seed=[&](const QString& name){QString path=root.filePath(name);QFile f(path);
            check(f.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&f.write("previous-output")==15,"synthetic old output is created without touching a user file");return path;};
        QString md=seed("plain.md"),html=seed("rendered.html"),text=seed("plain.txt");
        owner.hide();const int saves=owner.m_web->saves;
        owner.m_web->saveAsPlainMarkdown(md,done);owner.m_web->saveAsRenderedHtml(html,done);owner.m_web->saveAsPlainText(text,done);
        check(owner.m_web->sourcePage.scripts.empty()&&owner.m_web->sourcePage.html.empty()&&owner.m_web->saves==saves&&callbacks==0,
              "three actual export entry bodies refuse before reads or saves when locked");
        check(bytes(md)=="previous-output"&&bytes(html)=="previous-output"&&bytes(text)=="previous-output",
              "locked export entry leaves every existing output unchanged");
        owner.show();submit(owner);
        owner.m_web->saveAsPlainMarkdown(md,done);
        check(bytes(md)=="previous-output"&&owner.m_web->sourcePage.scripts.size()==1,"plain Markdown does not delete old output while waiting for its source");
        auto markdown=owner.m_web->sourcePage.scripts.back();owner.m_web->sourcePage.scripts.clear();
        const QString originalMd=md;md=root.filePath("changed-after-call.md");
        markdown(QStringLiteral("合成 $A+B$\n"));
        check(bytes(originalMd)==QStringLiteral("合成 $A+B$\n").toUtf8()&&!QFile::exists(md)&&reported==originalMd&&callbacks==1,
              "plain Markdown writes exact UTF8 without BOM to the captured original destination");
        md=originalMd;const auto markdownBytes=bytes(md);
        owner.m_web->saveAsPlainMarkdown(md,done);markdown=owner.m_web->sourcePage.scripts.back();owner.m_web->sourcePage.scripts.clear();markdown(QVariant());
        check(bytes(md)==markdownBytes&&callbacks==1,"failed JavaScript result cannot erase a valid plain Markdown export");
        owner.m_web->saveAsPlainMarkdown(md,done);markdown=owner.m_web->sourcePage.scripts.back();owner.m_web->sourcePage.scripts.clear();
        owner.hide();markdown(QString("stale"));owner.show();submit(owner);markdown(QString("stale after reauth"));
        check(bytes(md)==markdownBytes&&callbacks==1,"late Markdown result remains rejected after relock and new authentication");
        owner.m_web->saveAsPlainMarkdown(md,done);markdown=owner.m_web->sourcePage.scripts.back();owner.m_web->sourcePage.scripts.clear();markdown(QString(""));
        check(QFileInfo(md).size()==0&&callbacks==2,"valid empty Markdown remains an accepted zero-byte export");
        owner.m_web->saveAsRenderedHtml(html,done);
        auto rendered=owner.m_web->sourcePage.html.back();owner.m_web->sourcePage.html.clear();
        const QString renderedBody="<!DOCTYPE html><html><body>合成 &amp; exact</body></html>";rendered(renderedBody);
        check(bytes(html)==renderedBody.toUtf8()&&reported==html&&callbacks==3,"actual rendered HTML body writes exact source bytes without BOM");
        const auto htmlBytes=bytes(html);owner.m_web->saveAsRenderedHtml(html,done);
        rendered=owner.m_web->sourcePage.html.back();owner.m_web->sourcePage.html.clear();
        ReadPage replacementPage;owner.m_web->activePage=&replacementPage;rendered("wrong page");owner.m_web->activePage=&owner.m_web->sourcePage;
        check(bytes(html)==htmlBytes&&callbacks==3,"rendered HTML from a replaced page cannot overwrite the previous output");
        owner.m_web->saveAsRenderedHtml(html,done);rendered=owner.m_web->sourcePage.html.back();owner.m_web->sourcePage.html.clear();
        owner.hide();rendered("locked HTML");owner.show();submit(owner);
        check(bytes(html)==htmlBytes&&callbacks==3,"rendered HTML callback after locking performs no output or success notification");
        const QString input=root.filePath("synthetic-extracted.html");
        const QByteArray inputBytes=QStringLiteral("<html><body><p>合成 A &amp; B</p><p>第二行</p></body></html>").toUtf8();
        {QFile file(input);check(file.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&file.write(inputBytes)==inputBytes.size(),"plain text fixture provides only an isolated extracted HTML file");}
        owner.m_web->m_mapFile[a.strGUID]=input;owner.m_web->defer=true;
        owner.m_web->saveAsPlainText(text,done);auto saved=owner.m_web->pending.back();owner.m_web->pending.clear();
        owner.hide();saved(true);owner.show();submit(owner);saved(true);
        check(bytes(text)=="previous-output"&&callbacks==3,"successful old save callback is not a plaintext output permission after relock");
        owner.m_web->defer=false;owner.m_web->reply=false;owner.m_web->saveAsPlainText(text,done);
        check(bytes(text)=="previous-output"&&callbacks==3,"failed save preserves old plain text and reports no export success");
        owner.m_web->reply=1;owner.m_web->saveAsPlainText(text,done);
        check(bytes(text)=="previous-output"&&callbacks==3,"truthy nonboolean save result cannot authorize plain text output");
        owner.m_web->reply=true;owner.m_web->saveAsPlainText(text,done);
        check(bytes(text)=="synthetic only"&&reported==text&&callbacks==4,
              "actual plain text method converts the saved archive rather than stale extracted editor HTML without BOM");
        const auto textBytes=bytes(text);owner.m_web->m_mapFile[a.strGUID]=root.filePath("absent-extracted.html");
        owner.m_web->saveAsPlainText(text,done);
        check(bytes(text)==textBytes&&callbacks==5,"plain text no longer depends on a stale or missing editor extraction mapping");
        check(bytes(input)==inputBytes&&db.getCertPassword()==password,"export failures never rewrite extracted note data or clear the save cache");
        QString directory=root.filePath("output-directory");QDir().mkdir(directory);
        owner.m_web->saveAsRenderedHtml(directory,done);rendered=owner.m_web->sourcePage.html.back();owner.m_web->sourcePage.html.clear();rendered("cannot replace directory");
        check(QFileInfo(directory).isDir()&&callbacks==5,"failed atomic output cannot replace a directory or report success");
        QObject testOwner,testPage;WizDocumentCallbackEpoch epoch;int validations=0;
        WizDocumentReadPermit revokesBeforeCommit(&testOwner,&testPage,epoch.ticket(),[&]{return ++validations==1;});
        check(!WizDocumentReadOutput::writeUtf8(revokesBeforeCommit,text,"discard staged bytes")&&validations==2&&bytes(text)==textBytes,
              "permission revoked after staging cancels commit and preserves old target bytes");
        QString bom=root.filePath("explicit-bom.html");
        check(WizDocumentReadOutput::writeUtf8(owner.m_web->noteReadPermit(),bom,"exact",true)&&bytes(bom)==QByteArray::fromHex("efbbbf")+"exact",
              "shared output helper supports an explicitly requested UTF8 BOM without changing default exports");
    }
    {
        auto* owner=new RoutingView(explorer);owner->show();owner->viewNote(a,false);submit(*owner);
        QString path=root.filePath("destroyed-html.html");int callbacks=0;
        owner->m_web->saveAsRenderedHtml(path,[&](QString){++callbacks;});
        auto rendered=owner->m_web->sourcePage.html.back();delete owner;rendered("late after destruction");
        check(!QFile::exists(path)&&callbacks==0,"actual rendered output callback survives owner destruction without writing a file");
    }
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        const auto bytes=[](const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();};
        const QString output=root.filePath("full-note.html");
        const auto archiveBefore=bytes(db.getDocumentFileName(a.strGUID));
        {QFile file(output);check(file.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&file.write("old HTML")==8,
             "ordinary HTML old-output sentinel is confined to the synthetic root");}
        QFileDialog::destination=output;const int requests=QFileDialog::requests;
        owner.hide();owner.m_web->saveAsHtml();
        check(QFileDialog::requests==requests&&bytes(output)=="old HTML","actual ordinary HTML entry refuses before displaying a dialog when locked");
        owner.show();submit(owner);QFileDialog::beforeReturn=[&]{owner.hide();};owner.m_web->saveAsHtml();
        check(bytes(output)=="old HTML","ordinary HTML dialog continuation after relock preserves old output");
        check(!QFileDialog::beforeReturn,"synthetic modal interruption hook is explicitly consumed once");
        owner.show();submit(owner);
        owner.m_web->saveAsHtml();
        check(bytes(output).contains("full-note_files/fixture.txt")&&bytes(root.filePath("full-note_files/fixture.txt"))=="synthetic resource",
              "actual ordinary HTML entry exports through the real temporary database with resources");
        owner.m_note.strType="lite/markdown";
        const QString resource=root.filePath("full-note_files/fixture.txt");
        if(!check(WizSaveUnicodeTextToUtf8File(output,"previous rendered HTML",true)
            &&WizSaveUnicodeTextToUtf8File(resource,"previous exported resource",false),
              "HTML reply-first fixture distinguishes its previous successful body and resource from archive input"))return 1;
        const auto previousBody=bytes(output),previousResource=bytes(resource);
        owner.m_web->saveAsHtml();
        check(bytes(output)==previousBody&&bytes(resource)==previousResource,
              "ordinary Markdown HTML keeps both previous body and resource while waiting for a valid rendered reply");
        if(!check(!owner.m_web->sourcePage.scripts.empty(),"ordinary Markdown HTML fixture has a render callback before consuming it")) return 1;
        auto rendered=owner.m_web->sourcePage.scripts.back();owner.m_web->sourcePage.scripts.clear();
        const auto beforeLock=bytes(output);owner.hide();rendered(QString("late HTML"));
        check(bytes(output)==beforeLock&&bytes(resource)==previousResource,
              "relocked Markdown HTML reply cannot replace the previous body or resource before publication");
        owner.show();submit(owner);owner.m_web->saveAsHtml();
        if(!check(!owner.m_web->sourcePage.scripts.empty(),"fresh ordinary Markdown HTML fixture has a render callback")) return 1;
        rendered=owner.m_web->sourcePage.scripts.back();owner.m_web->sourcePage.scripts.clear();
        rendered(QString("<html><a href='index_files/fixture.txt'>合成</a></html>"));
        const auto expected=QByteArray::fromHex("efbbbf")+QString("<html><a href='full-note_files/fixture.txt'>合成</a></html>").toUtf8();
        check(bytes(output)==expected,"ordinary rendered Markdown HTML keeps explicit BOM and the existing resource rewrite");
        const auto validHtml=bytes(output),validResource=bytes(resource);
        owner.m_web->saveAsHtml();
        check(bytes(output)==validHtml&&bytes(resource)==validResource,"a new HTML request cannot eagerly overwrite a previous successful rendered export");
        if(!check(!owner.m_web->sourcePage.scripts.empty(),"failed-result fixture has a render callback")) return 1;
        rendered=owner.m_web->sourcePage.scripts.back();owner.m_web->sourcePage.scripts.clear();
        rendered(QVariant());
        check(bytes(output)==validHtml&&bytes(resource)==validResource,
              "failed ordinary HTML render preserves the exact prior successful body and resources from before the request");
        for(const auto& response:std::vector<QVariant>{false,1,QVariant(),QString("true")}){
            owner.m_web->reply=response;owner.m_web->saveAsHtml();
            check(owner.m_web->sourcePage.scripts.empty()&&bytes(output)==validHtml&&bytes(resource)==validResource,
                  "HTML save failure or non-Bool handoff refuses before querying or publishing body/resources");
        }
        owner.m_web->reply=true;owner.m_web->defer=true;owner.m_web->saveAsHtml();
        if(!check(!owner.m_web->pending.empty(),"ordinary HTML fixture captures its delayed save completion"))return 1;
        auto delayed=owner.m_web->pending.back();owner.m_web->pending.clear();owner.hide();owner.show();submit(owner);delayed(true);
        check(owner.m_web->sourcePage.scripts.empty()&&bytes(output)==validHtml&&bytes(resource)==validResource,
              "new authentication cannot revive an HTML request that was awaiting its save before relock");
        owner.m_web->defer=false;owner.m_web->saveAsHtml();
        if(!check(!owner.m_web->sourcePage.scripts.empty(),"empty-render fallback fixture has a valid query callback"))return 1;
        rendered=owner.m_web->sourcePage.scripts.back();owner.m_web->sourcePage.scripts.clear();rendered(QString());
        check(bytes(output).startsWith(QByteArray::fromHex("efbbbf"))&&bytes(output).contains("synthetic only")
            &&bytes(resource)=="synthetic resource",
              "valid empty Markdown render preserves the existing saved-HTML fallback, BOM and archive resources");
        if(!check(WizSaveUnicodeTextToUtf8File(output,"previous complete HTML",true)
            &&QFile::remove(resource)&&QDir().mkpath(resource)
            &&WizSaveUnicodeTextToUtf8File(resource+"/prior.txt","previous resource directory",false),
              "late HTML copy failure uses only a synthetic conflicting resource directory"))return 1;
        const auto completeHtml=bytes(output);
        owner.m_web->saveAsHtml();
        if(!check(!owner.m_web->sourcePage.scripts.empty(),"late copy fixture reaches its current valid render reply"))return 1;
        rendered=owner.m_web->sourcePage.scripts.back();owner.m_web->sourcePage.scripts.clear();rendered(QString("<html>new rendered HTML</html>"));
        check(bytes(output)==completeHtml&&bytes(resource+"/prior.txt")=="previous resource directory",
              "late HTML resource copy failure preserves the entire previous body and resource directory");
        check(QDir(root.path()).entryList({".wiz-export-*"},QDir::Dirs|QDir::Hidden|QDir::NoDotAndDotDot).isEmpty(),
              "ordinary HTML copy failure leaves no incomplete private publication directory");
        check(bytes(db.getDocumentFileName(a.strGUID))==archiveBefore&&WizZiwReader::isEncryptedFile(db.getDocumentFileName(a.strGUID)),
              "HTML export leaves the original synthetic encrypted archive byte-identical and encrypted");
    }
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        auto* web=owner.m_web;web->body="dirty external-editor source";
        const auto bytes=[](const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();};
        const auto archiveBefore=bytes(db.getDocumentFileName(a.strGUID));
        Launcher launcher(manager);const QString output=root.filePath("external.txt");launcher.destination=output;
        const WizExternalEditorData textEditor={"synthetic","never-run",{},2,1};
        const WizExternalEditorData htmlEditor={"synthetic","never-run",{},0,1};
        const auto invoke=[&](const WizExternalEditorData& editor){launcher.viewNoteInExternalEditor(&owner,editor,a);};
        const auto reset=[&]{check(WizSaveUnicodeTextToUtf8File(output,"old external output",false),"external fixture prepares its old target");};
        reset();const auto old=bytes(output);const int saves=web->saves;
        launcher.viewNoteInExternalEditor(nullptr,textEditor,a);
        launcher.viewNoteInExternalEditor(&owner,textEditor,b);
        auto wrongKb=a;wrongKb.strKbGUID="not-the-current-library";
        launcher.viewNoteInExternalEditor(&owner,textEditor,wrongKb);
        auto unsupported=textEditor;unsupported.TextEditor=1;invoke(unsupported);
        check(web->saves==saves&&launcher.launches==0&&bytes(output)==old,
              "external request rejects missing sender, mismatched note/library and unsupported mode before save or DB access");
        owner.hide();invoke(textEditor);
        check(web->saves==saves&&bytes(output)==old,"locked external request does not save, extract or launch");
        owner.show();submit(owner);
        for(const auto& result:std::vector<QVariant>{false,QVariant(),1,QString("true")}){
            web->reply=result;invoke(textEditor);
            check(launcher.launches==0&&bytes(output)==old,"external request requires an explicit successful Bool save handoff");
        }
        web->reply=true;web->defer=true;invoke(textEditor);
        if(!check(!web->pending.empty(),"external request retains its original save completion"))return 1;
        auto saved=web->pending.back();web->pending.clear();owner.hide();owner.show();submit(owner);saved(true);
        check(launcher.launches==0&&bytes(output)==old&&web->body=="dirty external-editor source",
              "relocked then reauthenticated external save cannot revive old output or discard dirty DOM");
        web->defer=false;invoke(textEditor);
        check(launcher.launches==1&&launcher.launchedNote.strGUID==a.strGUID&&launcher.launchedFile==output
            &&bytes(output)=="synthetic only"&&launcher.requestedTitle==a.strTitle,
              "current external text path converts real synthetic DB HTML and launches only after no-BOM atomic output");
        reset();const QString noteArchive=db.getDocumentFileName(a.strGUID),heldArchive=root.filePath("held-synthetic.ziw");
        if(!check(QFile::rename(noteArchive,heldArchive),"external missing-archive fixture temporarily retains the exact synthetic input"))return 1;
        invoke(textEditor);invoke(htmlEditor);
        check(launcher.launches==1&&bytes(output)==old&&web->sourcePage.html.empty(),
              "failed DB extraction never reads stale existing index HTML or starts a render query");
        if(!check(QFile::rename(heldArchive,noteArchive)&&bytes(noteArchive)==archiveBefore,
                  "external missing-archive fixture restores its exact synthetic input"))return 1;
        invoke(htmlEditor);
        if(!check(!web->sourcePage.html.empty(),"external HTML path queues one captured-page result"))return 1;
        auto html=web->sourcePage.html.back();web->sourcePage.html.clear();owner.hide();html("late HTML");
        check(launcher.launches==1&&bytes(output)==old,"relocked external HTML result preserves old target and never starts a process");
        owner.show();submit(owner);invoke(htmlEditor);
        if(!check(!web->sourcePage.html.empty(),"fresh external HTML path has a result callback"))return 1;
        html=web->sourcePage.html.back();web->sourcePage.html.clear();ReadPage replacement;web->activePage=&replacement;
        html("wrong page HTML");
        check(launcher.launches==1&&bytes(output)==old,"replaced external source page rejects an otherwise live result");
        web->activePage=&web->sourcePage;invoke(htmlEditor);
        if(!check(!web->sourcePage.html.empty(),"current external HTML path has a result callback"))return 1;
        html=web->sourcePage.html.back();web->sourcePage.html.clear();launcher.destination=root.filePath("not-captured.html");
        html("<html>合成 external</html>");
        check(launcher.launches==2&&launcher.launchedFile==output&&bytes(output)==QString("<html>合成 external</html>").toUtf8()
            &&!QFile::exists(launcher.destination)&&launcher.requestedTitle==a.strTitle+".html",
              "external HTML captures destination by value and keeps UTF-8 without BOM");
        launcher.destination=root.path();invoke(textEditor);
        check(launcher.launches==2&&QFileInfo(root.path()).isDir(),"failed external output never launches and leaves directory intact");
        web->defer=true;launcher.destination=output;invoke(textEditor);
        if(!check(!web->pending.empty(),"external navigation fixture has a pending save"))return 1;
        saved=web->pending.back();web->pending.clear();owner.m_note=b;saved(true);owner.m_note=a;
        check(launcher.launches==2,"external save callback cannot cross into another note");
        auto* goneLauncher=new Launcher(manager);goneLauncher->destination=root.filePath("gone-launcher.txt");
        goneLauncher->viewNoteInExternalEditor(&owner,textEditor,a);
        if(!check(!web->pending.empty(),"external launcher-destruction fixture has a pending save"))return 1;
        saved=web->pending.back();web->pending.clear();delete goneLauncher;saved(true);
        check(!QFile::exists(root.filePath("gone-launcher.txt")),"destroyed external launcher drops only its output continuation");
        web->defer=false;goneLauncher=new Launcher(manager);goneLauncher->destination=root.filePath("gone-html-launcher.html");
        goneLauncher->viewNoteInExternalEditor(&owner,htmlEditor,a);
        if(!check(!web->sourcePage.html.empty(),"external HTML launcher-destruction fixture has a pending result"))return 1;
        html=web->sourcePage.html.back();web->sourcePage.html.clear();delete goneLauncher;html("discarded HTML");
        check(!QFile::exists(root.filePath("gone-html-launcher.html")),"destroyed launcher cannot publish its late HTML result");
        auto* goneOwner=new RoutingView(explorer);goneOwner->show();goneOwner->viewNote(a,false);submit(*goneOwner);
        goneOwner->m_web->defer=true;launcher.viewNoteInExternalEditor(goneOwner,htmlEditor,a);
        if(!check(!goneOwner->m_web->pending.empty(),"external owner-destruction fixture has a pending save"))return 1;
        saved=goneOwner->m_web->pending.back();delete goneOwner;saved(true);
        check(launcher.launches==2,"destroyed external owner safely receives its old save completion without launching");
        check(bytes(db.getDocumentFileName(a.strGUID))==archiveBefore&&db.getCertPassword()==password
            &&web->body=="dirty external-editor source",
              "external read refusals preserve synthetic encrypted archive, save cache and dirty DOM");
    }
    {
        WIZDOCUMENTDATAEX diskNote;
        if(!check(create("disk-read-synthetic",1,diskNote),"disk-read chain prepares only an isolated encrypted note"))return 1;
        RoutingView owner(explorer);delete owner.m_web;
        auto* web=new DiskWeb(manager);owner.m_web=web;web->owner=&owner;web->setParent(owner.m_docView);
        owner.show();owner.viewNote(diskNote,false);submit(owner);web->body="dirty disk-read DOM";
        const auto bytes=[](const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();};
        QString output=root.filePath("disk-read.txt");
        if(!check(WizSaveUnicodeTextToUtf8File(output,"old disk output",false),"disk-read chain seeds its private previous output"))return 1;
        const QString live=root.filePath("live-editor-extraction.html");
        if(!check(WizSaveUnicodeTextToUtf8File(live,"<html>stale live editor file</html>",false),"disk-read fixture keeps a separate stale editor extraction sentinel"))return 1;
        web->m_mapFile[diskNote.strGUID]=live;int exported=0;
        const auto done=[&](QString){++exported;};
        const auto archiveHtml=[&]{QTemporaryDir temp;
            return temp.isValid()&&db.documentToHtmlFile(diskNote,temp.path()+"/")?bytes(temp.filePath("index.html")):QByteArray();};
        {
            HeldSaver saver(manager,nullptr);web->diskSaver=&saver;
            web->handoff=[&](const WIZDOCUMENTDATA& doc){saver.save(doc,"<html><body>latest-one</body></html>","",0);};
            web->saveAsPlainText(output,done);
            check(saver.entered.tryAcquire(1,3000)&&exported==0&&bytes(output)=="old disk output",
                  "actual plain-text adapter cannot read or publish while its real disk saver is held");
            saver.release.release();const bool completed=until([&]{return exported==1;});saver.waitForDone();
            check(completed&&bytes(output)=="latest-one"&&bytes(live)=="<html>stale live editor file</html>",
                  "plain text publishes completed archive content without overwriting or trusting live editor extraction");
        }
        {
            HeldSaver saver(manager,nullptr);web->diskSaver=&saver;web->handoff={};
            saver.save(diskNote,"<html><body>latest-unchanged-snapshot</body></html>","",0);
            web->saveAsPlainText(output,done);
            check(saver.entered.tryAcquire(1,3000)&&exported==1&&bytes(output)=="latest-one",
                  "an unchanged snapshot still waits for the already queued save rather than using old archive bytes");
            saver.release.release();const bool completed=until([&]{return exported==2;});saver.waitForDone();
            check(completed&&bytes(output)=="latest-unchanged-snapshot","unchanged-snapshot read receives the preceding actual saved version");
        }
        Launcher launcher(manager);launcher.destination=root.filePath("disk-external.txt");
        const WizExternalEditorData textEditor={"synthetic","never-run",{},2,1},htmlEditor={"synthetic","never-run",{},0,1};
        {
            HeldSaver saver(manager,nullptr);web->diskSaver=&saver;
            web->handoff=[&](const WIZDOCUMENTDATA& doc){saver.save(doc,"<html><body>latest-external</body></html>","",0);};
            launcher.viewNoteInExternalEditor(&owner,textEditor,diskNote);
            check(saver.entered.tryAcquire(1,3000)&&launcher.launches==0&&!QFile::exists(launcher.destination),
                  "actual external-editor entry cannot publish or launch from a mere snapshot handoff");
            saver.release.release();const bool completed=until([&]{return launcher.launches==1;});saver.waitForDone();
            check(completed&&bytes(launcher.destination)=="latest-external","external text entry sees actual completed archive bytes before recording a launch");
        }
        {
            HeldSaver saver(manager,nullptr);web->diskSaver=&saver;
            web->handoff=[&](const WIZDOCUMENTDATA& doc){saver.save(doc,"<html><body>html-save-completed</body></html>","",0);};
            launcher.viewNoteInExternalEditor(&owner,htmlEditor,diskNote);
            check(saver.entered.tryAcquire(1,3000)&&web->sourcePage.html.empty(),"external HTML does not query its page until the actual archive save completes");
            saver.release.release();const bool queried=until([&]{return !web->sourcePage.html.empty();});saver.waitForDone();
            if(!check(queried,"external HTML queries its still-current page after its specific save fence"))return 1;
            auto result=web->sourcePage.html.back();web->sourcePage.html.clear();result("<html>current rendered fixture</html>");
            check(launcher.launches==2&&bytes(launcher.destination)=="<html>current rendered fixture</html>","current external HTML retains its normal rendered-result path after disk coordination");
        }
        {
            HeldSaver wanted(manager,nullptr),other(manager,nullptr);web->diskSaver=&wanted;
            web->handoff=[&](const WIZDOCUMENTDATA& doc){wanted.save(doc,"<html><body>specific-saver-only</body></html>","",0);};
            const auto before=bytes(output);web->saveAsPlainText(output,done);
            bool otherDelivered=false;
            QObject::connect(&other,&WizDocumentSaverThread::saveFenceReached,&owner,[&](quint64,bool){otherDelivered=true;},Qt::QueuedConnection);
            const auto otherId=other.saveFence(diskNote);
            const bool entered=wanted.entered.tryAcquire(1,3000)&&other.entered.tryAcquire(1,3000);other.release.release();
            const bool delivered=until([&]{return otherDelivered;});other.waitForDone();
            check(entered&&otherId==1&&delivered&&exported==2&&bytes(output)==before,
                  "an identical fence ID emitted by another actual saver cannot satisfy the waiting read");
            wanted.release.release();const bool completed=until([&]{return exported==3;});wanted.waitForDone();
            check(completed&&bytes(output)=="specific-saver-only","only the captured saver instance can complete its own read request");
        }
        {
            HeldSaver saver(manager,nullptr);web->diskSaver=&saver;
            web->handoff=[&](const WIZDOCUMENTDATA& doc){saver.save(doc,"<html><body>save-survives-read-relock</body></html>","",0);};
            const auto before=bytes(output);web->saveAsPlainText(output,done);
            const bool entered=saver.entered.tryAcquire(1,3000);owner.hide();owner.show();submit(owner);
            saver.release.release();saver.waitForDone();app.processEvents();
            check(entered&&exported==3&&bytes(output)==before&&archiveHtml().contains("save-survives-read-relock"),
                  "relock and fresh authentication reject old output while the original actual save still completes");
        }
        {
            HeldSaver saver(manager,nullptr);web->diskSaver=&saver;web->handoff={};web->defer=true;
            int completions=0;bool accepted=false;
            web->trySaveDocumentForRead(diskNote,false,[&](const QVariant& value){++completions;accepted=value.toBool();});
            if(!check(!web->pending.empty(),"actual disk-read wrapper retains a delayed snapshot completion"))return 1;
            auto snapshot=web->pending.back();web->pending.clear();saver.save(diskNote,"<html>single-use-snapshot</html>","",0);
            snapshot(true);snapshot(false);snapshot(true);
            check(saver.entered.tryAcquire(1,3000)&&completions==0,"duplicate snapshot replies cannot report early failure or success after the first accepted handoff");
            saver.release.release();const bool completed=until([&]{return completions==1;});saver.waitForDone();
            check(completed&&accepted&&completions==1,"disk-read wrapper consumes its snapshot and actual frontier exactly once");
            web->defer=false;
        }
        {
            auto* saver=new WizDocumentSaverThread(manager,nullptr);web->diskSaver=saver;web->defer=true;web->handoff={};
            int completions=0;bool accepted=true;
            web->trySaveDocumentForRead(diskNote,false,[&](const QVariant& v){++completions;accepted=v.toBool();});
            if(!check(!web->pending.empty(),"saver-destruction fixture retains an uncompleted snapshot"))return 1;
            auto snapshot=web->pending.back();web->pending.clear();delete saver;snapshot(true);
            check(completions==1&&!accepted,"destroyed saver refuses once and cannot be revived by a late snapshot result");
            web->defer=false;
        }
        {
            WizDocumentSaverThread stopped(manager,nullptr);stopped.waitForDone();web->diskSaver=&stopped;web->handoff={};
            int completions=0;bool accepted=true;
            web->trySaveDocumentForRead(diskNote,false,[&](const QVariant& v){++completions;accepted=v.toBool();});
            check(completions==1&&!accepted&&!stopped.isRunning(),"stopped saver refusal cannot turn an unchanged snapshot into successful disk completion");
            const int saves=web->saves;web->diskSaver=nullptr;
            web->trySaveDocumentForRead(diskNote,false,[&](const QVariant& v){++completions;accepted=v.toBool();});
            check(completions==2&&!accepted&&web->saves==saves,"missing saver fails before scheduling another read snapshot");
            web->trySaveDocumentForRead(a,false,[&](const QVariant& v){++completions;accepted=v.toBool();});
            check(completions==3&&!accepted&&web->saves==saves,"disk-read wrapper refuses a mismatched note before snapshot handoff");
        }
        {
            const QString archive=db.getDocumentFileName(diskNote.strGUID),retained=root.filePath("disk-read-retained.ziw");
            const auto before=bytes(output),original=bytes(archive);
            if(!check(QFile::rename(archive,retained)&&QDir().mkdir(archive),"read failure fixture retains only its synthetic archive behind an empty write blocker"))return 1;
            HeldSaver saver(manager,nullptr);web->diskSaver=&saver;
            web->handoff=[&](const WIZDOCUMENTDATA& doc){saver.save(doc,"<html>failed-output-save</html>","",0);};
            web->saveAsPlainText(output,done);const bool entered=saver.entered.tryAcquire(1,3000);
            saver.release.release();saver.waitForDone();app.processEvents();
            const bool restored=QDir().rmdir(archive)&&QFile::rename(retained,archive);
            check(entered&&restored&&bytes(archive)==original&&exported==3&&bytes(output)==before&&saver.hasTransferError(),
                  "actual failed disk frontier preserves old plaintext output, reports no success and keeps original save error");
        }
        {
            HeldSaver saver(manager,nullptr);auto* gone=new RoutingView(explorer);delete gone->m_web;
            auto* goneWeb=new DiskWeb(manager);gone->m_web=goneWeb;goneWeb->owner=gone;goneWeb->setParent(gone->m_docView);
            gone->show();gone->viewNote(diskNote,false);submit(*gone);goneWeb->diskSaver=&saver;
            goneWeb->handoff=[&](const WIZDOCUMENTDATA& doc){saver.save(doc,"<html>save-survives-read-owner</html>","",0);};
            QString absent=root.filePath("destroyed-disk-owner.txt");goneWeb->saveAsPlainText(absent,done);
            const bool entered=saver.entered.tryAcquire(1,3000);delete gone;saver.release.release();saver.waitForDone();app.processEvents();
            check(entered&&exported==3&&!QFile::exists(absent)&&archiveHtml().contains("save-survives-read-owner"),
                  "destroyed actual read context discards output only and lets its already queued synthetic save finish");
        }
        {
            const QString htmlOutput=root.filePath("disk-ordinary.html"),htmlResource=root.filePath("disk-ordinary_files/prior.txt");
            QFileDialog::destination=htmlOutput;owner.m_note.strType="common";
            if(!check(QDir().mkpath(QFileInfo(htmlResource).absolutePath())
                &&WizSaveUnicodeTextToUtf8File(htmlOutput,"previous disk HTML",false)
                &&WizSaveUnicodeTextToUtf8File(htmlResource,"previous disk resource",false),
                  "ordinary HTML disk fixture seeds only private previous body and resource files"))return 1;
            {
                HeldSaver saver(manager,nullptr);web->diskSaver=&saver;
                web->handoff=[&](const WIZDOCUMENTDATA& doc){saver.save(doc,"<html><body>ordinary-html-latest</body></html>","",0);};
                web->saveAsHtml();
                check(saver.entered.tryAcquire(1,3000)&&bytes(htmlOutput)=="previous disk HTML"
                    &&bytes(htmlResource)=="previous disk resource",
                      "ordinary HTML cannot access either destination before its actual save frontier completes");
                saver.release.release();const bool completed=until([&]{return bytes(htmlOutput).contains("ordinary-html-latest");});saver.waitForDone();
                check(completed&&bytes(htmlOutput).startsWith(QByteArray::fromHex("efbbbf")),
                      "ordinary HTML publishes the actual completed synthetic archive with its existing BOM");
            }
            owner.m_note.strType="lite/markdown";
            const auto before=bytes(htmlOutput),beforeResource=bytes(htmlResource);
            {
                HeldSaver saver(manager,nullptr);web->diskSaver=&saver;
                web->handoff=[&](const WIZDOCUMENTDATA& doc){saver.save(doc,"<html><body>markdown-html-saved</body></html>","",0);};
                web->saveAsHtml();
                check(saver.entered.tryAcquire(1,3000)&&web->sourcePage.scripts.empty()&&bytes(htmlOutput)==before,
                      "Markdown HTML does not query or publish while its actual archive save is pending");
                saver.release.release();const bool queried=until([&]{return !web->sourcePage.scripts.empty();});saver.waitForDone();
                if(!check(queried&&bytes(htmlOutput)==before&&bytes(htmlResource)==beforeResource,
                          "completed Markdown save alone still cannot replace HTML body/resources before a valid page reply"))return 1;
                auto rendered=web->sourcePage.scripts.back();web->sourcePage.scripts.clear();rendered(QVariant());
                check(bytes(htmlOutput)==before&&bytes(htmlResource)==beforeResource,
                      "failed Markdown page reply after a real save leaves the entire previous export untouched");
            }
            {
                HeldSaver saver(manager,nullptr);web->diskSaver=&saver;web->handoff={};
                web->saveAsHtml();const bool entered=saver.entered.tryAcquire(1,3000);
                saver.release.release();const bool queried=until([&]{return !web->sourcePage.scripts.empty();});saver.waitForDone();
                if(!check(entered&&queried,"unchanged Markdown snapshot still waits for its own actual saver before querying HTML"))return 1;
                auto rendered=web->sourcePage.scripts.back();web->sourcePage.scripts.clear();rendered(QString("<html>rendered after real save</html>"));
                check(bytes(htmlOutput)==QByteArray::fromHex("efbbbf")+"<html>rendered after real save</html>",
                      "valid current Markdown reply publishes its exact rendered body after the real disk frontier");
            }
            {
                owner.m_note.strType="common";HeldSaver saver(manager,nullptr);web->diskSaver=&saver;
                web->handoff=[&](const WIZDOCUMENTDATA& doc){saver.save(doc,"<html><body>html-save-survives-relock</body></html>","",0);};
                const auto current=bytes(htmlOutput);web->saveAsHtml();const bool entered=saver.entered.tryAcquire(1,3000);
                owner.hide();owner.show();submit(owner);saver.release.release();saver.waitForDone();app.processEvents();
                check(entered&&bytes(htmlOutput)==current&&bytes(htmlResource)==beforeResource
                    &&archiveHtml().contains("html-save-survives-relock"),
                      "relocked ordinary HTML request cannot publish, while its original actual archive save still finishes");
            }
        }
        web->diskSaver=nullptr;web->handoff={};
        check(web->body=="dirty disk-read DOM"&&bytes(live)=="<html>stale live editor file</html>"
            &&db.getCertPassword()==password&&WizZiwReader::isEncryptedFile(db.getDocumentFileName(diskNote.strGUID)),
              "disk-read coordination preserves dirty shell content, live extraction, cached save credential and encrypted archive format");
    }
    {
        WIZDOCUMENTDATAEX savedNote;
        if(!check(create("fence-synthetic",1,savedNote),"save-fence fixture creates only a new synthetic encrypted note"))return 1;
        WIZDOCUMENTDATAEX missing=savedNote;missing.strGUID="missing-fence-synthetic";
        HeldSaver saver(manager,nullptr);
        std::vector<std::pair<quint64,bool>> workerReceipts,guiReceipts;
        std::atomic<int> legacySaved{0},legacyFailed{0},deadDeliveries{0},workerFences{0};
        bool allGuiThread=true;std::atomic<quint64> lastFence{0};
        QSemaphore posted,finish;
        auto* deadContext=new QObject;auto* lateContext=new QObject;
        // Queue GUI delivery BEFORE the last direct observer. Reaching its gate
        // therefore proves these deliveries have already been posted.
        QObject::connect(&saver,&WizDocumentSaverThread::saveFenceReached,&app,[&](quint64 id,bool ok){
            allGuiThread&=QThread::currentThread()==app.thread();guiReceipts.push_back({id,ok});
        },Qt::QueuedConnection);
        QObject::connect(&saver,&WizDocumentSaverThread::saveFenceReached,deadContext,
                         [&](quint64,bool){++deadDeliveries;},Qt::QueuedConnection);
        QObject::connect(&saver,&WizDocumentSaverThread::saveFenceReached,lateContext,
                         [&](quint64,bool){++deadDeliveries;},Qt::QueuedConnection);
        QObject::connect(&saver,&WizDocumentSaverThread::saved,&app,
                         [&](const QString&,const QString&,bool ok,QPointer<QObject>){++legacySaved;if(!ok)++legacyFailed;},Qt::DirectConnection);
        QObject::connect(&saver,&WizDocumentSaverThread::saveFenceReached,&app,[&](quint64 id,bool ok){
            workerReceipts.push_back({id,ok});++workerFences;
            if(id==lastFence){posted.release();if(!finish.tryAcquire(1,3000))saver.rescued=true;}
        },Qt::DirectConnection);
        check(saver.saveFence(WIZDOCUMENTDATA())==0&&!saver.isRunning(),"invalid save fence refuses without starting a worker");
        const auto unchanged=saver.saveFence(savedNote);
        const bool entered=saver.entered.tryAcquire(1,3000);
        saver.save(savedNote,"<html><body>fence-one</body></html>","",0);
        const auto firstSave=saver.saveFence(savedNote);
        const auto beforeFailure=saver.saveFence(missing);
        saver.save(missing,"<html><body>cannot-persist</body></html>","",0);
        const auto failed=saver.saveFence(missing);
        auto otherLibrary=missing;otherLibrary.strKbGUID="synthetic-fence-other-library";
        const auto distinctLibrary=saver.saveFence(otherLibrary);
        const auto independent=saver.saveFence(savedNote);
        saver.save(savedNote,"<html><body>fence-two</body></html>","",0);
        lastFence=saver.saveFence(savedNote);
        check(entered&&unchanged&&firstSave>unchanged&&beforeFailure>firstSave&&failed>beforeFailure
            &&distinctLibrary>failed&&independent>distinctLibrary&&lastFence>independent,
              "save fences have distinct FIFO identities even between same-note saves");
        check(!saver.isIdleForTransfer()&&workerFences==0&&guiReceipts.empty()&&legacySaved==0,
              "queued snapshot work cannot masquerade as completed disk saves while worker is held");
        delete deadContext;saver.release.release();
        const bool reached=posted.tryAcquire(1,5000);
        // Join before reading the worker's vector. The late receiver is deleted
        // before GUI pumping, after its queued events have really been posted.
        delete lateContext;finish.release();saver.waitForDone();app.processEvents();
        check(reached&&!saver.rescued&&saver.isIdleForTransfer()&&!saver.isRunning(),
              "actual save-first drain finishes all queued saves and checkpoints without a save deadline");
        const std::vector<std::pair<quint64,bool>> expected={{unchanged,true},{firstSave,true},{beforeFailure,true},
                                                    {failed,false},{distinctLibrary,true},{independent,true},{lastFence,true}};
        check(workerReceipts==expected,"worker receipts freeze per-note/library status at each exact frontier, not at later GUI delivery");
        check(guiReceipts==expected&&allGuiThread,"queued fence consumers receive ordered results on their own GUI thread");
        check(legacySaved==3&&legacyFailed==1,"fences never emit fake saved receipts or suppress original missing-note failure");
        check(deadDeliveries==0,"destroyed receivers discard both unposted and already-posted fence UI results without canceling saves");
        check(saver.hasTransferError(),"successful other-note fences cannot clear a prior save failure or the global error status");
        check(saver.saveFence(savedNote)==0&&!saver.isRunning(),"a stopped saver refuses new fences instead of restarting after quit drain");
        QString savedPath;bool extracted=db.documentToTempHtmlFile(savedNote,savedPath);
        QFile savedFile(savedPath);const auto savedHtml=extracted&&savedFile.open(QIODevice::ReadOnly)?savedFile.readAll():QByteArray();
        check(savedHtml.contains("fence-two")&&!savedHtml.contains("fence-one")
            &&WizZiwReader::isEncryptedFile(db.getDocumentFileName(savedNote.strGUID)),
              "actual FIFO saver persists the final synthetic content and preserves the existing encrypted format");
        check(db.getCertPassword()==password,"disk-save checkpoints never clear the synthetic password cache");
        savedFile.close();
        // Ordinary local write failure, not a corruption/security test: retain
        // the exact synthetic archive, put an empty directory at its old path,
        // then restore it while the worker is paused at the failed checkpoint.
        const auto bytes=[](const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();};
        const QString archivePath=db.getDocumentFileName(savedNote.strGUID),retained=root.filePath("fence-retry-original.ziw");
        const auto original=bytes(archivePath);
        if(!check(QFile::rename(archivePath,retained)&&QDir().mkdir(archivePath),
                  "disk-failure fixture retains exact synthetic archive and installs only an empty output blocker"))return 1;
        HeldSaver retry(manager,nullptr);QSemaphore failedReached,resume;
        std::atomic<quint64> blockedFence{0};std::atomic<int> failedSaves{0},goodSaves{0};
        std::vector<std::pair<quint64,bool>> retryReceipts;
        QObject::connect(&retry,&WizDocumentSaverThread::saved,&app,
                         [&](const QString&,const QString&,bool ok,QPointer<QObject>){if(ok)++goodSaves;else++failedSaves;},Qt::DirectConnection);
        QObject::connect(&retry,&WizDocumentSaverThread::saveFenceReached,&app,[&](quint64 id,bool ok){
            retryReceipts.push_back({id,ok});
            if(id==blockedFence){failedReached.release();if(!resume.tryAcquire(1,3000))retry.rescued=true;}
        },Qt::DirectConnection);
        retry.save(savedNote,"<html><body>blocked-write</body></html>","",0);
        blockedFence=retry.saveFence(savedNote);
        const bool retryEntered=retry.entered.tryAcquire(1,3000);retry.release.release();
        const bool failedPosted=failedReached.tryAcquire(1,5000);
        const bool restored=QDir().rmdir(archivePath)&&QFile::rename(retained,archivePath);
        check(failedPosted&&QFileInfo(archivePath).isFile()&&restored&&bytes(archivePath)==original,
              "disk failure reaches its checkpoint and fixture restores the exact retained input before retry");
        retry.save(savedNote,"<html><body>explicit-retry-persisted</body></html>","",0);
        const auto retried=retry.saveFence(savedNote);resume.release();retry.waitForDone();
        check(retryEntered&&!retry.rescued&&failedSaves==1&&goodSaves==1,
              "real disk-output failure and explicit same-note retry each deliver their original save receipt");
        const std::vector<std::pair<quint64,bool>> retryExpected={{blockedFence,false},{retried,true}};
        check(retryReceipts==retryExpected,"a same-note failed frontier stays false while a later successful retry gets its own true frontier");
        check(retry.hasTransferError(),"successful retry fence does not silently clear sticky global transfer errors");
        QString retryPath;const bool retryExtracted=db.documentToTempHtmlFile(savedNote,retryPath);
        check(retryExtracted&&bytes(retryPath).contains("explicit-retry-persisted")
            &&WizZiwReader::isEncryptedFile(archivePath)&&db.getCertPassword()==password,
              "explicit retry persists current synthetic content with encrypted format and save cache intact");
    }
    {
        const auto bytes=[](const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();};
        const auto put=[](const QString& path,const QByteArray& value){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(value)==value.size();};
        WIZDOCUMENTDATAEX batch;
        if(!check(create("batch.md",1,batch),"batch fixture creates a separate downloaded encrypted synthetic note"))return 1;
        const QString attachmentSource=root.filePath("batch-attachment.txt");WIZDOCUMENTATTACHMENTDATA attachment;
        if(!check(put(attachmentSource,"batch attachment")&&db.addAttachment(batch,attachmentSource,attachment),
                  "batch fixture uses an actual temporary attachment without a downloader"))return 1;
        CWizDocumentAttachmentDataArray attachments;db.getDocumentAttachments(batch.strGUID,attachments);
        if(!check(attachments.size()==1,"batch fixture has one locally stored attachment"))return 1;
        const auto att=attachments.front();const auto originalArchive=bytes(db.getDocumentFileName(batch.strGUID));
        const auto originalAttachment=bytes(db.getAttachmentFileName(att.strGUID));
        BatchExporter exporter(manager);WizFileExporter actual(manager);QString error;
        QObject authority;WizDocumentCallbackEpoch epoch;auto ticket=epoch.ticket();
        const auto permit=[&]{const auto captured=ticket;QPointer<QObject> owner(&authority);
            return std::function<bool()>([owner,captured]{return owner&&WizDocumentCallbackEpoch::current(captured);});};
        const QString denied=root.filePath("batch-denied");
        check(!exporter.exportNote(batch,denied,BatchExporter::HTML,false,true,false,&error,{})
            &&!QFile::exists(denied)&&BatchWait::documents==0&&error.contains("permission expired"),
              "explicit empty batch permission refuses before download or directory creation");
        const auto stale=permit();epoch.invalidate();ticket=epoch.ticket();
        check(!exporter.exportNote(batch,denied,BatchExporter::HTML,false,true,false,&error,stale)
            &&!QFile::exists(denied)&&BatchWait::documents==0,
              "a new batch ticket cannot revive an earlier cancelled export");
        const QString held=root.filePath("batch-held"),heldBody=held+"/batch.md/index.html";
        check(QDir().mkpath(held+"/batch.md")&&put(heldBody,"OLD BODY"),"batch old-output sentinel is isolated locally");
        BatchWait::duringDocument=[&]{epoch.invalidate();};
        check(!exporter.exportNote(batch,held,BatchExporter::HTML,false,true,false,&error,permit())
            &&bytes(heldBody)=="OLD BODY"&&!QFile::exists(held+"/batch.md/metainfo.json")
            &&!QFile::exists(held+"/batch.md/index_files")&&error.contains("permission expired"),
              "revocation during document wait stops before output creation or replacement");
        BatchWait::duringDocument={};ticket=epoch.ticket();
        const QString missing=root.filePath("batch-missing");auto absent=batch;absent.strGUID="missing-synthetic-batch-document";
        check(!exporter.exportNote(absent,missing,BatchExporter::HTML,false,true,false,&error,permit())
            &&!QFile::exists(missing)&&error.contains("Can't download document"),
              "document preparation failure does not publish a partial body");
        const QString attFolder=held+"/batch.md/attachments";
        const QString attName=QFileInfo(db.getAttachmentFileName(att.strGUID)).fileName().remove(0,38);
        check(QDir().mkpath(attFolder)&&put(attFolder+"/"+attName,"OLD ATTACHMENT")
            &&db.setObjectDataDownloaded(att.strGUID,"attachment",false),"attachment wait fixture retains an existing output");
        BatchWait::duringAttachment=[&]{epoch.invalidate();};
        check(!exporter.exportNote(batch,held,BatchExporter::HTML,false,true,false,&error,permit())
            &&BatchWait::attachments==1&&bytes(attFolder+"/"+attName)=="OLD ATTACHMENT"&&bytes(heldBody)=="OLD BODY"
            &&error.contains("permission expired"),
              "revocation during attachment wait preserves that old attachment and prevents later body export");
        check(QFile::exists(held+"/batch.md/metainfo.json"),
              "batch output is not transactional: metadata already written before cancellation remains explicit");
        BatchWait::duringAttachment={};ticket=epoch.ticket();db.setObjectDataDownloaded(att.strGUID,"attachment",true);
        const QString good=root.filePath("batch-current");
        check(exporter.exportNote(batch,good,BatchExporter::HTML,false,true,false,&error,permit())
            &&bytes(good+"/batch.md/index.html").contains("synthetic only")
            &&bytes(good+"/batch.md/index_files/fixture.txt")=="synthetic resource"
            &&bytes(good+"/batch.md/attachments/"+attName)==originalAttachment
            &&bytes(good+"/batch.md/metainfo.json").contains("batch.md"),
              "current explicit permission preserves actual HTML, resources, attachment and metadata output");
        const QString md=root.filePath("batch-markdown");
        check(actual.exportNote(batch,md,WizFileExporter::Markdown,false,false,false,&error,permit())
            &&bytes(md+"/batch.md/index.md").contains("synthetic only")&&!QFile::exists(md+"/batch.md/index.html"),
              "actual linked exporter preserves Markdown conversion with explicit current permission");
        const QString compressed=root.filePath("batch-compressed");
        check(actual.exportNote(batch,compressed,WizFileExporter::HTML,true,false,false,&error,permit())
            &&QFile::exists(compressed+"/batch.md.zip")&&!QFile::exists(compressed+"/batch.md"),
              "actual linked exporter preserves successful compression and source-folder cleanup");
        const QString beforeZip=root.filePath("batch-cancel-before-zip");
        check(!actual.exportNote(batch,beforeZip,WizFileExporter::HTML,true,false,false,&error,
            [&,allowed=permit()]{return allowed()&&!QFile::exists(beforeZip+"/batch.md/index.html");})
            &&QFile::exists(beforeZip+"/batch.md/index.html")&&!QFile::exists(beforeZip+"/batch.md.zip"),
              "permission loss at the post-body checkpoint prevents starting compression");
        const QString afterZip=root.filePath("batch-cancel-after-zip");
        check(!actual.exportNote(batch,afterZip,WizFileExporter::HTML,true,false,false,&error,
            [&,allowed=permit()]{return allowed()&&!QFile::exists(afterZip+"/batch.md.zip");})
            &&QFile::exists(afterZip+"/batch.md/index.html")&&QFile::exists(afterZip+"/batch.md.zip"),
              "permission loss after compression does not delete its uncompressed source or retract the existing zip");
        const QString blocked=root.filePath("batch-compression-failure");
        check(QDir().mkpath(blocked+"/batch.md.zip"),"compression failure fixture is an empty synthetic directory");
        check(!actual.exportNote(batch,blocked,WizFileExporter::HTML,true,false,false,&error,permit())
            &&bytes(blocked+"/batch.md/index.html").contains("synthetic only"),
              "compression failure reports false and retains the uncompressed exported note");
        const QString legacy=root.filePath("batch-legacy");
        check(actual.exportNote(batch,legacy,WizFileExporter::HTML,false,false,false,&error)
            &&bytes(legacy+"/batch.md/index.html").contains("synthetic only"),
              "legacy seven-argument exporter behavior remains callable and is not mistaken for new batch authority");
        auto* gone=new BatchExporter(manager);QPointer<BatchExporter> weak(gone);
        BatchWait::duringDocument=[&]{delete gone;};const QString destroyed=root.filePath("batch-destroyed");
        check(!gone->exportNote(batch,destroyed,BatchExporter::HTML,false,false,false,&error,permit())
            &&!weak&&!QFile::exists(destroyed),"exporter destruction during document wait drops its remaining output continuation");
        BatchWait::duringDocument={};
        auto* beforeWait=new BatchExporter(manager);QPointer<BatchExporter> weakBefore(beforeWait);
        const int waitsBefore=BatchWait::documents;
        check(!beforeWait->exportNote(batch,destroyed,BatchExporter::HTML,false,false,false,&error,
            [&]{delete beforeWait;return true;})&&!weakBefore&&BatchWait::documents==waitsBefore,
              "a reentrant authority predicate cannot continue after destroying the exporter itself");
        auto* duringAtt=new BatchExporter(manager);QPointer<BatchExporter> weakAtt(duringAtt);
        db.setObjectDataDownloaded(att.strGUID,"attachment",false);
        BatchWait::duringAttachment=[&]{delete duringAtt;};
        check(!duringAtt->exportNote(batch,held,BatchExporter::HTML,false,false,false,&error,permit())
            &&!weakAtt&&bytes(attFolder+"/"+attName)=="OLD ATTACHMENT"&&bytes(heldBody)=="OLD BODY",
              "exporter destruction during attachment wait preserves that attachment and drops the enclosing note output");
        BatchWait::duringAttachment={};db.setObjectDataDownloaded(att.strGUID,"attachment",true);
        const QString attDenied=root.filePath("batch-attachment-denied");QDir().mkpath(attDenied);
        check(put(attDenied+"/"+attName,"KEEP")
            &&!actual.exportAttachment(att,attDenied,{})&&bytes(attDenied+"/"+attName)=="KEEP",
              "standalone attachment overload rejects missing authority without deleting old output");
        check(actual.exportAttachment(att,attDenied,permit())&&bytes(attDenied+"/"+attName)==originalAttachment,
              "standalone current attachment permission preserves the original replacement behavior");
        const QString flatArchive=root.filePath("flat.ziw");QuaZip flatZip(flatArchive);bool flatReady=flatZip.open(QuaZip::mdCreate);
        if(flatReady){QuaZipFile f(&flatZip);flatReady=f.open(QIODevice::WriteOnly,QuaZipNewInfo("index.html"));
            if(flatReady){flatReady=f.write("<html><body>flat only</body></html>")>0;f.close();}flatZip.close();}
        WIZDOCUMENTDATAEX flat;
        if(!check(flatReady&&db.createDocument("flat","synthetic.ziw","/synthetic/",{},0,flat)
            &&QFile::copy(flatArchive,db.getDocumentFileName(flat.strGUID))&&db.setObjectDataDownloaded(flat.strGUID,"document",true),
                  "flattening fixture has one local HTML file and no attachment or resource"))return 1;
        const QString flatOutput=root.filePath("batch-flat");
        check(actual.exportNote(flat,flatOutput,WizFileExporter::HTML,false,false,true,&error,permit())
            &&bytes(flatOutput+"/flat.html").contains("flat only")&&!QFile::exists(flatOutput+"/flat"),
              "explicit current authority preserves no-title-folder naming and cleanup");
        check(bytes(db.getDocumentFileName(batch.strGUID))==originalArchive
            &&bytes(db.getAttachmentFileName(att.strGUID))==originalAttachment&&db.getCertPassword()==password,
              "batch cancellation never changes stored encrypted archive, attachment or independent save-password cache");
    }
    {
        int serial=0;
        const auto reset=[] {
            BatchPrompt::calls=0;BatchPrompt::accepted=true;BatchPrompt::during={};
            BatchMessage::errors=BatchMessage::infos=0;BatchMessage::answer=BatchMessage::Ok;BatchMessage::during={};
            BatchDesktop::opens=0;BatchDesktop::result=true;BatchDesktop::during={};BatchEvents::during={};
            BatchWait::duringDocument={};BatchWait::duringAttachment={};
        };
        const auto setup=[&](QWizard& wizard,const QStringList& notes){
            reset();BatchPrompt::password=password;
            auto* page=new BatchPage(explorer);wizard.addPage(page);
            page->fields["documents"]=notes;page->fields["outputFolder"]=root.filePath("wizard-"+QString::number(++serial));
            page->fields["keepFolder"]=false;page->fields["compress"]=false;
            page->fields["metainformation"]=false;page->fields["noTitleFolderIfPossible"]=false;
            page->initializePage();return page;
        };
        const auto output=[](BatchPage* page){return page->field("outputFolder").toString();};
        const auto restoreCert=[&]{db.setUserCert(cert.n,cert.e,cert.encryptedD,{});db.loadUserCert();};
        const auto cleanup=qScopeGuard([&]{reset();BatchPrompt::password.clear();});
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID,b.strGUID,c.strGUID});bool lastStillIncomplete=false;
            BatchEvents::during=[&]{if(page->m_progress->value()==page->m_progress->maximum())lastStillIncomplete=!page->isComplete();};
            check(!page->isComplete()&&page->queuedStarts==1,"batch initialization is incomplete until its export loop finishes");
            page->handleExportFile();
            check(BatchPrompt::calls==1&&page->isComplete()&&BatchDesktop::opens==1
                &&QFile::exists(output(page)+"/a/index.html")&&QFile::exists(output(page)+"/b/index.html")&&QFile::exists(output(page)+"/c/index.html"),
                  "fresh batch password authorizes two encrypted notes and a plain note without borrowing a visible-note permit");
            check(lastStillIncomplete,"reaching the final progress step cannot enable Finish before that note is exported");
            page->handleExportFile();
            check(BatchPrompt::calls==1&&BatchDesktop::opens==1,"duplicate queued starts do not repeat a completed batch");
            page->initializePage();page->handleExportFile();
            check(BatchPrompt::calls==2&&page->isComplete()&&BatchDesktop::opens==2,
                  "a new batch requires a new password proof even while the independent save cache stays warm");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{c.strGUID});page->handleExportFile();
            check(page->isComplete()&&BatchPrompt::calls==0&&QFile::exists(output(page)+"/c/index.html"),
                  "plain-only batch keeps its original no-password workflow");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});const auto ticket=page->m_exportCallbacks.ticket();
            wizard.reject();page->handleExportFile();
            check(page->m_cancel&&!WizDocumentCallbackEpoch::current(ticket)&&!page->isComplete()
                &&BatchPrompt::calls==0&&BatchDesktop::opens==0&&!QFile::exists(output(page)),
                  "actual QWizard rejection revokes a queued export before any prompt or output");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});page->cleanupPage();page->handleExportFile();
            check(page->m_cancel&&!page->isComplete()&&BatchPrompt::calls==0&&!QFile::exists(output(page)),
                  "leaving the export page cancels its old batch without clearing saved note credentials");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});BatchPrompt::accepted=false;page->handleExportFile();
            check(!page->isComplete()&&BatchPrompt::calls==1&&BatchMessage::errors==0&&BatchDesktop::opens==0&&!QFile::exists(output(page)),
                  "cancelling the password prompt ends the batch without a second error dialog or false completion");
            page->handleExportFile();check(BatchPrompt::calls==1,"cancelled prompt cannot be revived by a duplicate queued start");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});BatchPrompt::password="wrong synthetic password";
            BatchMessage::answer=BatchMessage::Abort;page->handleExportFile();
            check(!page->isComplete()&&BatchPrompt::calls==1&&BatchMessage::errors==1&&!QFile::exists(output(page))
                &&db.getCertPassword()==password,"wrong local password does not fall back to the warm cache or clear save credentials");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID,c.strGUID});BatchPrompt::password="wrong synthetic password";
            page->handleExportFile();
            check(page->isComplete()&&BatchPrompt::calls==1&&BatchMessage::errors==1
                &&!QFile::exists(output(page)+"/a")&&QFile::exists(output(page)+"/c/index.html"),
                  "explicit continue after a password error skips that encrypted note and preserves later plain export");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});BatchPrompt::during=[&]{page->handleCancelBtnClicked();};
            page->handleExportFile();
            check(page->m_cancel&&BatchPrompt::calls==1&&!QFile::exists(output(page))&&BatchDesktop::opens==0,
                  "wizard cancellation while the password dialog waits prevents cache proof and output continuation");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});BatchEvents::during=[&]{page->handleCancelBtnClicked();};
            page->handleExportFile();
            check(page->m_cancel&&BatchPrompt::calls==0&&!QFile::exists(output(page)),
                  "cancellation at the existing event-pump boundary stops before an encrypted-note prompt");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});BatchWait::duringDocument=[&]{page->handleCancelBtnClicked();};
            page->handleExportFile();
            check(page->m_cancel&&BatchPrompt::calls==1&&!QFile::exists(output(page))&&BatchDesktop::opens==0,
                  "actual wizard passes a revocable batch predicate through the document-wait exporter overload");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});BatchWait::duringDocument=[&]{db.setMeta("Account","GUID","other-batch-account");};
            page->handleExportFile();db.setMeta("Account","GUID","synthetic-routing-account");page->handleExportFile();
            check(page->m_cancel&&BatchPrompt::calls==1&&!QFile::exists(output(page))&&BatchDesktop::opens==0,
                  "account change in a document wait permanently revokes that batch even after the old account is restored");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{c.strGUID});BatchEvents::during=[&]{WizCommonApiEntry::setEnterpriseServerIP("https://other-batch.invalid/");};
            page->handleExportFile();WizCommonApiEntry::setEnterpriseServerIP("https://synthetic-routing.invalid/");
            check(page->m_cancel&&!QFile::exists(output(page))&&BatchPrompt::calls==0,
                  "server identity change cancels even a plain-note batch before output");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});BatchPrompt::during=[&]{db.setUserCert(replacement.certificate.n,replacement.certificate.e,replacement.certificate.encryptedD,{});};
            page->handleExportFile();restoreCert();
            check(page->m_cancel&&!QFile::exists(output(page))&&BatchMessage::errors==0,
                  "certificate change while asking for a password cannot transfer the entered proof to another certificate");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});BatchWait::duringDocument=[&]{db.setUserCert(replacement.certificate.n,replacement.certificate.e,replacement.certificate.encryptedD,{});};
            page->handleExportFile();restoreCert();
            check(page->m_cancel&&!QFile::exists(output(page))&&BatchDesktop::opens==0,
                  "certificate replacement during a document wait revokes the encrypted output predicate");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{c.strGUID});BatchWait::duringDocument=[&]{auto changed=c;changed.nProtected=1;db.modifyDocumentInfo(changed);};
            page->handleExportFile();db.modifyDocumentInfo(c);
            check(page->m_cancel&&!QFile::exists(output(page)),"protection change during a wait cannot reuse a plain-note output grant");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});bool entered=false;
            BatchEvents::during=[&]{if(!entered){entered=true;page->handleExportFile();}};
            page->handleExportFile();
            check(page->isComplete()&&BatchPrompt::calls==1&&BatchDesktop::opens==1,
                  "reentrant start during an active batch cannot duplicate prompts, exports or completion");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{a.strGUID});bool restarted=false;
            BatchEvents::during=[&]{if(!restarted){restarted=true;page->initializePage();page->handleExportFile();}};
            page->handleExportFile();
            check(!page->isComplete()&&!page->m_exporting&&!QFile::exists(output(page))&&BatchPrompt::calls==0&&page->queuedStarts==3,
                  "reinitialization during an active batch retires the old run and requests a fresh queued start");
            page->handleExportFile();
            check(page->isComplete()&&BatchPrompt::calls==1&&BatchDesktop::opens==1,
                  "the fresh reinitialized batch can run after the obsolete stack has unwound");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{"missing-synthetic-wizard-document",c.strGUID});
            BatchMessage::during=[&]{wizard.reject();};page->handleExportFile();
            check(page->m_cancel&&BatchMessage::errors==1&&!QFile::exists(output(page))&&BatchDesktop::opens==0,
                  "cancellation in the error continuation dialog cannot advance to the next note");
        }
        {
            auto wizard=std::make_unique<QWizard>();auto* page=setup(*wizard,{a.strGUID});const QString destination=output(page);QPointer<BatchPage> weak(page);
            BatchPrompt::during=[&]{wizard.reset();};page->handleExportFile();
            check(!weak&&!QFile::exists(destination)&&BatchDesktop::opens==0,
                  "owner destruction in the password prompt drops the batch continuation without touching saves");
        }
        {
            auto wizard=std::make_unique<QWizard>();auto* page=setup(*wizard,{c.strGUID});QPointer<BatchPage> weak(page);
            BatchDesktop::result=false;BatchDesktop::during=[&]{wizard.reset();};page->handleExportFile();
            check(!weak&&BatchDesktop::opens==1&&BatchMessage::infos==0,
                  "owner destruction while opening the output folder suppresses the later failure popup");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{c.strGUID});BatchDesktop::result=false;page->handleExportFile();
            check(page->isComplete()&&BatchDesktop::opens==1&&BatchMessage::infos==1&&QFile::exists(output(page)+"/c/index.html"),
                  "live folder-open failure reports the retained export without changing its completion");
        }
        {
            QWizard wizard;auto* page=setup(wizard,{c.strGUID});
            QObject::connect(page,&QWizardPage::completeChanged,&wizard,[&]{wizard.reject();});page->handleExportFile();
            check(page->m_cancel&&BatchDesktop::opens==0&&QFile::exists(output(page)+"/c/index.html"),
                  "closing from actual completion notification suppresses later folder opening but retains completed output");
        }
        {
            auto wizard=std::make_unique<QWizard>();auto* page=setup(*wizard,{a.strGUID});const QString destination=output(page);QPointer<BatchPage> weak(page);
            BatchWait::duringDocument=[&]{wizard.reset();};page->handleExportFile();
            check(!weak&&!QFile::exists(destination)&&BatchDesktop::opens==0,
                  "destroying the wizard and its child exporter during document wait drops both remaining continuations");
        }
        {
            WIZDOCUMENTDATAEX attached;WIZDOCUMENTATTACHMENTDATA attachment;
            const QString source=root.filePath("wizard-attachment.txt");QFile input(source);
            bool ready=input.open(QIODevice::WriteOnly)&&input.write("wizard attachment")>0;input.close();
            if(!check(ready&&create("wizard-attached",1,attached)&&db.addAttachment(attached,source,attachment),
                      "wizard attachment fixture uses a separate actual encrypted local note"))return 1;
            QWizard wizard;auto* page=setup(wizard,{attached.strGUID});
            const QString folder=output(page)+"/wizard-attached/attachments";
            const QString name=QFileInfo(db.getAttachmentFileName(attachment.strGUID)).fileName().remove(0,38);
            QFile sentinel(folder+"/"+name);bool kept=QDir().mkpath(folder)&&sentinel.open(QIODevice::WriteOnly)&&sentinel.write("KEEP ATTACHMENT")>0;sentinel.close();
            if(!check(kept&&db.setObjectDataDownloaded(attachment.strGUID,"attachment",false),"wizard attachment wait retains an old local output sentinel"))return 1;
            BatchWait::duringAttachment=[&]{wizard.reject();};page->handleExportFile();
            sentinel.open(QIODevice::ReadOnly);const auto retained=sentinel.readAll();sentinel.close();
            db.setObjectDataDownloaded(attachment.strGUID,"attachment",true);
            check(page->m_cancel&&retained=="KEEP ATTACHMENT"&&!QFile::exists(output(page)+"/wizard-attached/index.html")&&BatchDesktop::opens==0,
                  "wizard rejection during attachment wait reaches the guarded overload without replacing attachment or exporting the body");
        }
        check(db.getCertPassword()==password&&WizZiwReader::isEncryptedFile(db.getDocumentFileName(a.strGUID)),
              "wizard cancellation and local verification preserve the separate warm save cache and encrypted original");
    }
    {
        WIZDOCUMENTDATAEX one,two;
        const auto local=[&](const QString& name,int protectedFlag,WIZDOCUMENTDATAEX& doc){
            return db.createDocument(name,"synthetic.ziw","/phase141/",{},protectedFlag,doc)
                &&QFile::copy(protectedFlag?encrypted:plain,db.getDocumentFileName(doc.strGUID))
                &&db.setObjectDataDownloaded(doc.strGUID,"document",true);
        };
        if(!check(local("actual-wizard-one",1,one)&&local("actual-wizard-two",1,two),
                  "actual wizard fixture contains two downloaded encrypted notes in its own temporary folder"))return 1;
        int serial=0;
        const auto spin=[&](const std::function<bool()>& done){QElapsedTimer elapsed;elapsed.start();
            while(!done()&&elapsed.elapsed()<4000){app.processEvents();QThread::msleep(1);}return done();};
        const auto prepare=[&](FileExportWizard& wizard,const QString& destination){
            wizard.restart();
            auto* list=qobject_cast<FileExportPageDocList*>(wizard.currentPage());
            auto* tree=list?list->findChild<QTreeWidget*>():nullptr;
            if(!list||!tree||!spin([&]{return tree->topLevelItemCount()==1&&list->isComplete();}))return false;
            tree->topLevelItem(0)->setCheckState(0,Qt::Checked);
            wizard.next();
            if(!qobject_cast<FileExportPageOptions*>(wizard.currentPage()))return false;
            wizard.setField("outputFolder",destination);wizard.setField("keepFolder",false);
            wizard.setField("compress",false);wizard.setField("metainformation",false);
            wizard.setField("noTitleFolderIfPossible",false);
            const auto docs=wizard.field("documents").toStringList();
            return docs.size()==2&&docs.contains(one.strGUID)&&docs.contains(two.strGUID)
                &&wizard.field("outputFolder").toString()==destination;
        };
        enum class Flow { Accept, CancelPrompt, WrongPassword, CancelQueued };
        for(auto flow:{Flow::Accept,Flow::CancelPrompt,Flow::WrongPassword,Flow::CancelQueued}){
            FileExportWizard wizard("/phase141/",explorer);const QString destination=root.filePath("actual-wizard-"+QString::number(++serial));
            if(!check(prepare(wizard,destination),"actual wizard document-tree selection and option fields retain both selected GUIDs"))return 1;
            auto* page=qobject_cast<FileExportPageExport*>(wizard.page(wizard.pageIds().last()));
            if(!check(page!=nullptr,"actual wizard constructs its normal export page and exporter"))return 1;
            int prompts=0,errors=0,completionSignals=0,finishedSignals=0;bool done=false,timedOut=false,completedBeforeAccept=false;
            QObject::connect(&wizard,&QWizard::finished,&wizard,[&](int){++finishedSignals;done=true;});
            QObject::connect(page,&QWizardPage::completeChanged,&wizard,[&]{
                ++completionSignals;completedBeforeAccept=page->isComplete();
                // Finish through the actual wizard before the optional folder
                // opener; never start Finder or a browser in this test.
                wizard.accept();
            });
            QTimer responder;responder.setInterval(1);
            QObject::connect(&responder,&QTimer::timeout,&wizard,[&]{
                if(auto* input=qobject_cast<QInputDialog*>(QApplication::activeModalWidget())){
                    ++prompts;
                    if(flow==Flow::CancelPrompt){wizard.reject();input->reject();}
                    else {input->setTextValue(flow==Flow::WrongPassword?"wrong synthetic wizard password":password);input->accept();}
                }else if(auto* box=qobject_cast<QMessageBox*>(QApplication::activeModalWidget())){
                    ++errors;if(auto* abort=box->button(QMessageBox::Abort))abort->click();else box->reject();
                    wizard.reject();
                }
            });
            QTimer watchdog;watchdog.setSingleShot(true);watchdog.setInterval(3000);
            QObject::connect(&watchdog,&QTimer::timeout,&wizard,[&]{
                timedOut=true;if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();wizard.reject();
            });
            responder.start();watchdog.start();wizard.next();
            if(flow==Flow::CancelQueued)wizard.reject();
            const bool delivered=spin([&]{return done;});
            // Drain the original queued start too, including cancellation
            // before dispatch; both timers remain alive until it has returned.
            app.processEvents();responder.stop();watchdog.stop();
            check(delivered&&!timedOut&&finishedSignals==1,"actual queued wizard run settles once without watchdog intervention");
            if(flow==Flow::Accept){
                check(prompts==1&&errors==0&&completionSignals==1&&completedBeforeAccept&&wizard.result()==QDialog::Accepted
                    &&QFile::exists(destination+"/actual-wizard-one/index.html")&&QFile::exists(destination+"/actual-wizard-two/index.html"),
                      "actual queued two-note export verifies one real Qt password prompt and accepts only after both files exist");
            }else{
                check(completionSignals==0&&wizard.result()==QDialog::Rejected&&!QFile::exists(destination),
                      "actual queued cancellation or invalid-password abort produces no output or false completion");
                check(flow==Flow::CancelQueued?prompts==0&&errors==0:
                      flow==Flow::CancelPrompt?prompts==1&&errors==0:prompts==1&&errors==1,
                      "actual password and error dialogs follow the selected cancel or local-failure path");
            }
        }
        check(db.getCertPassword()==password&&WizZiwReader::isEncryptedFile(db.getDocumentFileName(one.strGUID))
            &&WizZiwReader::isEncryptedFile(db.getDocumentFileName(two.strGUID)),
              "actual wizard modal runs preserve the independent save-password cache and encrypted synthetic originals");
    }
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        auto* web=owner.m_web;web->sourcePage.address=QUrl::fromLocalFile(root.filePath("note with spaces/index.html"));
        web->setupWebActions();ReadDesktop::opened.clear();
        web->folderAction.trigger();
        check(ReadDesktop::opened.size()==1&&ReadDesktop::opened.last()==web->sourcePage.address.adjusted(QUrl::RemoveFilename),
              "current authorized temporary-folder action preserves its local URL and filename adjustment");
        const auto saveTicket=web->m_documentCallbacks.ticket();web->body="dirty hidden output fixture";owner.hide();
        web->folderAction.trigger();
        check(ReadDesktop::opened.size()==1,"locked temporary-folder action cannot hand the extracted directory to an external application");
        check(WizDocumentCallbackEpoch::current(saveTicket)&&web->body=="dirty hidden output fixture"&&db.getCertPassword()==password,
              "rejecting hidden folder output preserves the independent save ticket, dirty DOM and password cache");
        owner.show();submit(owner);web->sourcePage.duringUrl=[&]{owner.hide();};web->folderAction.trigger();
        check(ReadDesktop::opened.size()==1,"revocation while obtaining the page URL stops before opening its directory");
        web->sourcePage.duringUrl={};owner.show();submit(owner);
        ReadPage replacementPage;web->sourcePage.duringUrl=[&]{web->activePage=&replacementPage;};web->folderAction.trigger();
        check(ReadDesktop::opened.size()==1,"source page replacement while obtaining the URL cannot open the old extracted folder");
        web->activePage=&web->sourcePage;web->sourcePage.duringUrl={};
        const QUrl external("https://synthetic.invalid/path?q=math%20notes#part");
        web->onEditorLinkClicked(external,QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);
        check(ReadDesktop::opened.size()==2&&ReadDesktop::opened.last()==external,
              "authorized source-page link dispatch preserves the original external URL");
        web->onEditorLinkClicked(external,QWebEnginePage::NavigationTypeLinkClicked,false,&web->sourcePage);
        check(ReadDesktop::opened.size()==2,"subframe links remain ignored by note link dispatch");
        web->onEditorLinkClicked(external,QWebEnginePage::NavigationTypeLinkClicked,true,&replacementPage);
        web->onEditorLinkClicked(external,QWebEnginePage::NavigationTypeLinkClicked,true,nullptr);
        check(ReadDesktop::opened.size()==2,"wrong or absent source page cannot dispatch an external note link");
        const QUrl document("wiz://open_document?guid=synthetic-destination"),attachment("wiz://open_attachment?guid=synthetic-attachment");
        web->onEditorLinkClicked(document,QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);
        check(web->documentsOpened==1&&web->dispatchedUrl==document.toString(),"authorized internal document links retain their existing dispatcher");
        web->onEditorLinkClicked(attachment,QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);
        check(web->attachmentsOpened==1&&web->dispatchedKb==a.strKbGUID&&web->dispatchedUrl==attachment.toString(),
              "authorized reader attachment links keep the originating knowledge base and URL");
        web->editing=true;web->onEditorLinkClicked(attachment,QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);
        check(web->attachmentsOpened==1,"attachment clicks in edit mode remain suppressed");web->editing=false;
        owner.hide();
        for(const auto& url:{external,document,attachment})web->onEditorLinkClicked(url,QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);
        check(ReadDesktop::opened.size()==2&&web->documentsOpened==1&&web->attachmentsOpened==1,
              "locked note link events cannot dispatch external URLs, documents or attachments using a warm save cache");
        owner.show();submit(owner);web->duringEditing=[&]{owner.hide();};
        web->onEditorLinkClicked(attachment,QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);
        check(web->attachmentsOpened==1,"revocation during attachment-mode inspection prevents the later attachment dispatcher");
        web->duringEditing={};owner.show();submit(owner);
        web->onEditorLinkClicked(QUrl("http://file/synthetic/path"),QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);
        check(ReadDesktop::opened.size()==3&&ReadDesktop::opened.last()==QUrl("file:/synthetic/path"),
              "legacy file URL conversion is preserved for current authorized link events");
        check(web->body=="dirty hidden output fixture"&&WizDocumentCallbackEpoch::current(saveTicket)&&db.getCertPassword()==password,
              "navigation read checks never discard hidden unsaved text or cancel the existing save barrier");
        web->sourcePage.address=QUrl("https://synthetic.invalid/not-an-extracted-file/index.html");web->folderAction.trigger();
        check(ReadDesktop::opened.size()==3,"temporary-file-location action does not turn a nonlocal page URL into an external folder request");
        web->sourcePage.address=QUrl::fromLocalFile(root.filePath("note with spaces/index.html"));
        const bool queued=QMetaObject::invokeMethod(&web->folderAction,"trigger",Qt::QueuedConnection);owner.hide();app.processEvents();
        check(queued&&ReadDesktop::opened.size()==3,"actual queued QAction activation after revocation does not open the extracted directory");
        owner.show();submit(owner);ReadPage newerPage;web->activePage=&newerPage;
        QTimer::singleShot(0,web,[web,external]{web->onEditorLinkClicked(external,QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);});
        app.processEvents();
        check(ReadDesktop::opened.size()==3,"queued old-source link dispatch is rejected after the view switches to a newer page");
        web->activePage=&web->sourcePage;
        web->duringEditing=[&]{web->activePage=&newerPage;};
        web->onEditorLinkClicked(attachment,QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);
        check(web->attachmentsOpened==1,"source replacement during mode inspection stops before using the note knowledge base");
        web->activePage=&web->sourcePage;web->duringEditing={};owner.viewNote(c,false);
        web->onEditorLinkClicked(external,QWebEnginePage::NavigationTypeLinkClicked,true,&web->sourcePage);web->folderAction.trigger();
        check(ReadDesktop::opened.size()==5,"ordinary readable plain notes retain their external-link and local-folder actions");
        auto* gone=new RoutingView(explorer);gone->show();gone->viewNote(a,false);submit(*gone);
        Web* goneWeb=gone->m_web;QPointer<Web> weak(goneWeb);goneWeb->duringEditing=[&]{delete gone;};
        goneWeb->onEditorLinkClicked(attachment,QWebEnginePage::NavigationTypeLinkClicked,true,&goneWeb->sourcePage);
        check(!weak&&ReadDesktop::opened.size()==5,"owner destruction during mode inspection drops the remaining link continuation safely");
        ReadDesktop::opened.clear();
    }
    // Phase143: exact inspector lifecycle with recording dialog/page; no live
    // DevTools frontend or operating-system F12/key/clipboard interaction.
    {
        const auto resetInspector=[] {
            InspectorDialog::onConstruct={};InspectorDialog::onTitle={};InspectorDialog::onShow={};InspectorDialog::onRaise={};InspectorPage::onBind={};
        };
        const auto deferredDeletes=[] {QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);};
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        auto* web=owner.m_web;web->body="dirty inspector fixture";const auto saveTicket=web->documentCallbackTicket();
        QAction f12;QObject::connect(&f12,&QAction::triggered,web,&InspectorShell::openDevTools);
        f12.trigger();QPointer<InspectorDialog> first=web->m_devToolsWindow;
        check(first&&first->isVisible()&&first->content.source.inspected==&web->sourcePage&&first->shows==1&&first->raises==1,
              "typed base-member QAction dispatch reaches the note override and opens only the current authorized source");
        const int initialConstructed=InspectorDialog::constructed;f12.trigger();
        check(web->m_devToolsWindow==first&&InspectorDialog::constructed==initialConstructed&&first->shows==2&&web->sourcePage.inspectActions==2,
              "authorized repeated inspector actions reuse the same window without duplicate construction");
        owner.hide();
        check(!web->m_devToolsWindow&&first&&!first->isVisible()&&!first->content.source.inspected,
              "actual extracted hide routing detaches and conceals an already open note inspector immediately");
        check(web->body=="dirty inspector fixture"&&WizDocumentCallbackEpoch::current(saveTicket)&&db.getCertPassword()==password,
              "inspector revocation preserves dirty body save callback ticket and synthetic save password cache");
        deferredDeletes();check(!first,"revoked inspector is actually destroyed by Qt deferred deletion");
        ActionPage actions(web);actions.triggerAction(QWebEnginePage::InspectElement);f12.trigger();
        check(actions.actions==0&&actions.notifications==0&&!web->m_devToolsWindow&&InspectorDialog::constructed==initialConstructed,
              "locked InspectElement page action and F12-equivalent QAction both refuse before inspection or construction");
        owner.show();submit(owner);actions.triggerAction(QWebEnginePage::InspectElement);
        check(actions.actions==1&&actions.notifications==1,"authorized InspectElement retains its ordinary page action dispatch");
        f12.trigger();auto next=web->m_devToolsWindow;
        check(next&&next!=first&&next->content.source.inspected==&web->sourcePage,"fresh manual authorization can create a new note inspector");
        owner.revokeBiometricAccess();
        check(!web->m_devToolsWindow&&next&&!next->content.source.inspected&&!next->isVisible(),
              "explicit note revocation disconnects an existing inspector without waiting for another action");
        owner.prepareEncryptedAccess(db);submit(owner);const int queuedBefore=InspectorDialog::constructed;
        const bool queued=QMetaObject::invokeMethod(&f12,"trigger",Qt::QueuedConnection);owner.hide();app.processEvents();
        check(queued&&InspectorDialog::constructed==queuedBefore&&!web->m_devToolsWindow,"real queued inspector QAction after lock cannot open using a warm save cache");
        owner.show();submit(owner);f12.trigger();auto switched=web->m_devToolsWindow;owner.viewNote(c,false);
        check(!web->m_devToolsWindow&&switched&&!switched->content.source.inspected,"note switch detaches the preceding note inspector before the next document");
        f12.trigger();check(web->m_devToolsWindow&&web->m_devToolsWindow->content.source.inspected==&web->sourcePage,
              "ordinary readable plain notes retain inspector access");web->closeDevTools();
        owner.viewNote(a,false);submit(owner);
        web->body="dirty inspector fixture";const auto inspectorSaveTicket=web->documentCallbackTicket();
        const auto authorize=[&] {resetInspector();web->duringTitle={};web->sourcePage.duringInspect={};web->activePage=&web->sourcePage;owner.show();owner.prepareEncryptedAccess(db);submit(owner);};
        InspectorDialog::onConstruct=[&]{owner.hide();};f12.trigger();resetInspector();
        check(!web->m_devToolsWindow&&!web->m_openingDevTools,"revocation inside inspector construction prevents adoption and clears the opening guard");
        authorize();web->duringTitle=[&]{owner.hide();};f12.trigger();web->duringTitle={};
        check(!web->m_devToolsWindow,"revocation while obtaining the document title disconnects the inspector before binding");
        authorize();InspectorDialog::onTitle=[&]{owner.hide();};f12.trigger();resetInspector();
        check(!web->m_devToolsWindow,"revocation during inspector title update stops before source inspection");
        authorize();QPointer<InspectorDialog> bound;
        InspectorPage::onBind=[&](ReadPage* page){if(page){bound=web->m_devToolsWindow;owner.hide();}};f12.trigger();resetInspector();
        check(!web->m_devToolsWindow&&bound&&!bound->content.source.inspected&&!bound->isVisible(),
              "revocation during inspected-page binding detaches immediately and never shows the window");
        authorize();const int actionsBefore=web->sourcePage.inspectActions;web->sourcePage.duringInspect=[&]{owner.hide();};f12.trigger();web->sourcePage.duringInspect={};
        check(!web->m_devToolsWindow&&web->sourcePage.inspectActions==actionsBefore+1,"revocation during InspectElement dispatch suppresses the later inspector show");
        authorize();QPointer<InspectorDialog> shown;
        InspectorDialog::onShow=[&]{shown=web->m_devToolsWindow;owner.hide();};f12.trigger();resetInspector();
        check(shown&&!shown->content.source.inspected&&!shown->isVisible()&&shown->raises==0&&!web->m_devToolsWindow,
              "revocation from show callback closes the inspector and suppresses raise");
        authorize();QPointer<InspectorDialog> raised;
        InspectorDialog::onRaise=[&]{raised=web->m_devToolsWindow;owner.hide();};f12.trigger();resetInspector();
        check(raised&&!raised->content.source.inspected&&!raised->isVisible()&&!web->m_devToolsWindow,
              "revocation from raise callback still detaches and closes the inspector");
        authorize();InspectorDialog::onConstruct=[&]{web->closeDevTools();};f12.trigger();resetInspector();
        check(!web->m_devToolsWindow&&!web->m_openingDevTools,"explicit close during unadopted construction invalidates the in-flight generation");
        const int nestedBefore=InspectorDialog::constructed;InspectorDialog::onConstruct=[&]{web->openDevTools();};f12.trigger();resetInspector();
        check(web->m_devToolsWindow&&InspectorDialog::constructed==nestedBefore+1&&!web->m_openingDevTools,
              "reentrant inspector open during construction is suppressed without blocking later opens");
        web->closeDevTools();ReadPage replacement;
        InspectorPage::onBind=[&](ReadPage* page){if(page)web->activePage=&replacement;};f12.trigger();resetInspector();
        check(!web->m_devToolsWindow,"source page replacement during binding cancels and detaches the old inspector");web->activePage=&web->sourcePage;
        InspectorDialog::onTitle=[&]{delete web->m_devToolsWindow.data();};f12.trigger();resetInspector();
        check(!web->m_devToolsWindow&&!web->m_openingDevTools,"inspector destruction during title update leaves no raw cached window or stuck guard");
        f12.trigger();QPointer<InspectorDialog> externallyDeleted=web->m_devToolsWindow;delete externallyDeleted.data();
        check(!externallyDeleted&&!web->m_devToolsWindow,"external inspector destruction automatically clears the cached QPointer");
        f12.trigger();check(web->m_devToolsWindow,"inspector reopens normally after independent window destruction");web->closeDevTools();
        check(web->body=="dirty inspector fixture"&&WizDocumentCallbackEpoch::current(inspectorSaveTicket)&&db.getCertPassword()==password,
              "all inspector cancellation and reentrancy cases leave save state and dirty body intact");
        auto* gone=new RoutingView(explorer);gone->show();gone->viewNote(a,false);submit(*gone);
        QPointer<Web> goneWeb=gone->m_web;goneWeb->duringTitle=[&]{delete gone;};goneWeb->openDevTools();
        check(!goneWeb,"owner destruction during document-title callback drops the remaining inspector continuation safely");
        resetInspector();deferredDeletes();
        InspectorShell browser;int browserTitleReads=0;browser.duringTitle=[&]{++browserTitleReads;};
        browser.openDevTools();QPointer<InspectorDialog> browserTools=browser.m_devToolsWindow;
        check(browserTools&&browserTools->isVisible()&&browserTools->content.source.inspected==browser.page(),
              "generic non-note browser keeps ordinary unscoped inspector behavior");
        browserTools->setGeometry(34,45,620,380);const auto userGeometry=browserTools->geometry();browser.openDevTools();
        check(browser.m_devToolsWindow==browserTools&&browserTools->geometry()==userGeometry&&browserTitleReads==1,
              "generic browser inspector reuse preserves the user's window geometry and original title initialization policy");
        ReadPage browserReplacement;browser.target=&browserReplacement;browser.openDevTools();
        check(browser.m_devToolsWindow==browserTools&&browserTools->content.source.inspected==&browserReplacement,
              "generic browser inspector reuse follows its new current page rather than retaining a stale target");
        browser.closeDevTools();browser.closeDevTools();
        check(!browser.m_devToolsWindow&&browserTools&&!browserTools->content.source.inspected&&!browserTools->isVisible(),
              "repeated inspector close is harmless and disconnects only the inspector page");
        deferredDeletes();check(!browserTools,"generic explicit close completes Qt deferred cleanup");
        const int emptyBefore=InspectorDialog::constructed;browser.openDevToolsWithPermit({});
        check(!browser.m_devToolsWindow&&InspectorDialog::constructed==emptyBefore,"empty explicit inspector permit never authorizes construction");
        browser.openDevToolsWithPermit([&]{browser.closeDevTools();return true;});
        check(!browser.m_devToolsWindow&&!browser.m_openingDevTools,"revocation from the permit predicate cannot revive a canceled inspector generation");
        auto* goneBrowser=new InspectorShell;QPointer<InspectorShell> weakBrowser(goneBrowser);
        goneBrowser->openDevToolsWithPermit([&]{delete goneBrowser;return true;});
        check(!weakBrowser,"owner destruction from the permit predicate is checked before further inspector access");
        deferredDeletes();resetInspector();
    }
    // Phase144: original current methods, real GUI queue and synthetic title/
    // clipboard-operation collaborators; no system clipboard is read.
    {
        WizQueuedThreadsInit();
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        auto* web=owner.m_web;owner.m_note.tModified=owner.m_note.tCreated;
        web->body="dirty command fixture";const auto saveTicket=web->documentCallbackTicket();
        web->tryResetTitle();check(web->sourcePage.plainText.size()==1,"authorized new-note title detection requests its source plain text");
        auto title=std::move(web->sourcePage.plainText.back());web->sourcePage.plainText.clear();
        owner.hide();title("Late locked title");
        check(owner.titleResets==0&&!web->m_bNewNoteTitleInited,"plain-text title result arriving after lock cannot rename or mark the hidden note initialized");
        web->m_bNewNoteTitleInited=false;web->tryResetTitle();
        check(web->sourcePage.plainText.empty(),"locked new-note title detection refuses before querying plaintext");web->sourcePage.plainText.clear();
        const int pastes=web->pasteRequests;web->doPaste();app.processEvents();
        check(web->pasteRequests==pastes,"direct paste request while locked never reaches the clipboard-operation collaborator");
        web->onReturn();app.processEvents();check(web->sourcePage.plainText.empty(),"locked return request cannot start delayed title detection");web->sourcePage.plainText.clear();
        ActionPage actions(web);for(auto action:{QWebEnginePage::Paste,QWebEnginePage::Undo,QWebEnginePage::Redo})actions.triggerAction(action);
        check(actions.actions==0&&actions.notifications==0&&web->pasteRequests==pastes,"locked paste undo redo page actions do not dispatch to the hidden editor");
        for(auto action:{QWebEnginePage::Undo,QWebEnginePage::Redo,QWebEnginePage::Paste})web->onActionTriggered(action);
        check(web->sourcePage.commands.empty()&&web->sourcePage.plainText.empty(),"locked editor action feedback cannot mutate history or derive a title");
        web->sourcePage.commands.clear();web->sourcePage.plainText.clear();
        owner.show();submit(owner);web->m_bNewNoteTitleInited=false;
        web->doPaste();check(web->pasteRequests==pastes,"authorized paste preserves deferred original main-queue delivery");owner.hide();app.processEvents();
        check(web->pasteRequests==pastes,"paste queued before revocation is dropped despite the preserved save ticket");
        owner.show();submit(owner);web->onReturn();owner.hide();owner.show();submit(owner);app.processEvents();
        check(web->sourcePage.plainText.empty(),"return queued before lock cannot borrow a later manual authorization");web->sourcePage.plainText.clear();
        owner.hide();const int focus=web->focusRequests,focusSignals=web->focusSignals;owner.setEditorFocus();
        check(web->focusRequests==focus&&web->focusSignals==focusSignals,"locked focus request does not target the hidden editor or emit editor focus");
        owner.onTitleReturnPressed();
        check(web->focusRequests==focus&&web->focusSignals==focusSignals,"title return uses the guarded focus route without an unguarded duplicate");
        owner.show();submit(owner);web->sourcePage.commands.clear();owner.setEditorFocus();
        check(web->focusRequests==focus+1&&web->focusSignals==focusSignals+1,"authorized focus still reaches the original editor once");
        check(web->body=="dirty command fixture"&&WizDocumentCallbackEpoch::current(saveTicket)&&db.getCertPassword()==password,
              "command revocation preserves synthetic dirty body active save ticket and save cache");
        web->sourcePage.plainText.clear();
        const auto takeText=[](Web* source){
            std::function<void(const QString&)> reply=[](const QString&){};
            if(!source->sourcePage.plainText.empty()){reply=std::move(source->sourcePage.plainText.back());source->sourcePage.plainText.clear();}return reply;
        };
        const auto authorize=[&]{web->duringFocus={};web->sourcePage.duringScript={};web->activePage=&web->sourcePage;
            owner.show();owner.prepareEncryptedAccess(db);submit(owner);web->m_bNewNoteTitleInited=false;owner.m_note.tModified=owner.m_note.tCreated;};
        authorize();const int startingResets=owner.titleResets;
        web->tryResetTitle();auto emptyTitle=takeText(web);emptyTitle("");
        check(owner.titleResets==startingResets&&!web->m_bNewNoteTitleInited,"empty title reply preserves the new-note initialization opportunity");
        web->tryResetTitle();auto acceptedTitle=takeText(web);const QString unicodeTitle="合成标题 α：A+B=C";acceptedTitle(unicodeTitle);
        check(owner.titleResets==startingResets+1&&owner.resetValue==WizStr2Title(unicodeTitle.left(128))&&web->m_bNewNoteTitleInited,
              "authorized new-note title retains the original Unicode normalizer and marks initialization exactly once");
        acceptedTitle("Second title");check(owner.titleResets==startingResets+1,"duplicate plain-text delivery cannot apply a second automatic title");
        web->tryResetTitle();check(web->sourcePage.plainText.empty(),"already initialized title is not queried again");
        authorize();owner.m_note.tModified=owner.m_note.tCreated.addSecs(1);web->tryResetTitle();
        check(web->sourcePage.plainText.empty(),"previously modified note preserves the original no-auto-title policy");
        authorize();web->tryResetTitle();auto changedTime=takeText(web);owner.m_note.tModified=owner.m_note.tCreated.addSecs(1);changedTime("Obsolete timestamp");
        check(owner.titleResets==startingResets+1&&!web->m_bNewNoteTitleInited,"modification after title query prevents the late automatic rename");
        authorize();web->tryResetTitle();auto changedTitle=takeText(web);const auto savedTitle=owner.m_note.strTitle;owner.m_note.strTitle="Manually edited title";changedTitle("Stale auto title");
        check(owner.titleResets==startingResets+1&&!web->m_bNewNoteTitleInited,"manual title edit with unchanged timestamps still wins over the delayed automatic title");owner.m_note.strTitle=savedTitle;
        authorize();web->tryResetTitle();auto changedPage=takeText(web);ReadPage replacement;web->activePage=&replacement;changedPage("Wrong page title");
        check(owner.titleResets==startingResets+1&&!web->m_bNewNoteTitleInited,"title reply bound to the replaced page cannot rename the current note");
        authorize();web->tryResetTitle();auto changedEpoch=takeText(web);web->m_documentCallbacks.invalidate();changedEpoch("Expired document");
        check(owner.titleResets==startingResets+1&&!web->m_bNewNoteTitleInited,"old document-generation title reply is ignored even with the same page and note IDs");
        authorize();web->tryResetTitle();auto reauthorizedTitle=takeText(web);owner.hide();owner.show();submit(owner);reauthorizedTitle("Old authorization title");
        check(owner.titleResets==startingResets+1&&!web->m_bNewNoteTitleInited,"late title reply cannot borrow a new authorization on the same note and page");
        authorize();const int authorizedPastes=web->pasteRequests;web->doPaste();
        check(web->pasteRequests==authorizedPastes,"authorized paste is still queued rather than executed inline");app.processEvents();
        check(web->pasteRequests==authorizedPastes+1,"actual original main queue delivers one current authorized paste");
        web->doPaste();owner.hide();owner.show();submit(owner);app.processEvents();
        check(web->pasteRequests==authorizedPastes+1,"queued paste cannot borrow a new authorization on the same document");
        web->doPaste();web->activePage=&replacement;app.processEvents();
        check(web->pasteRequests==authorizedPastes+1,"page replacement before actual paste queue delivery cancels that pending operation");web->activePage=&web->sourcePage;
        authorize();const int titleQueueResets=owner.titleResets;web->onReturn();
        check(web->sourcePage.plainText.empty(),"authorized return preserves deferred title-query scheduling");app.processEvents();
        check(web->sourcePage.plainText.size()==1,"current authorized return starts one title query through the actual main queue");takeText(web)("Queued current title");
        check(owner.titleResets==titleQueueResets+1&&web->m_bNewNoteTitleInited,"authorized queued return can still initialize a new note title");
        web->sourcePage.commands.clear();web->undo();web->redo();
        check(web->sourcePage.commands==QStringList({"if(window.WizNativeEditorInitialized && window.WizEditor) WizEditor.undo();","if(window.WizNativeEditorInitialized && window.WizEditor) WizEditor.redo();"}),
              "authorized undo and redo retain their original JavaScript commands and ordering");
        owner.hide();web->sourcePage.commands.clear();const int hiddenFocusSignals=web->focusSignals;
        web->undo();web->redo();web->editorFocus();
        check(web->sourcePage.commands.empty()&&web->focusSignals==hiddenFocusSignals,"direct locked undo redo and editorFocus refuse before JavaScript or focus notification");
        owner.show();owner.setEditorFocus();auto* passwordEdit=owner.m_passwordView->findChild<QLineEdit*>("editUserCipher");
        check(owner.m_stack->currentWidget()==owner.m_passwordView&&owner.m_passwordView->focusWidget()==passwordEdit,
              "focus request while the password page is visible selects its actual password editor");
        authorize();const int focusBeforeReturn=web->focusRequests,signalsBeforeReturn=web->focusSignals;owner.onTitleReturnPressed();
        check(web->focusRequests==focusBeforeReturn+1&&web->focusSignals==signalsBeforeReturn+1,"authorized title return focuses the editor once without duplicate script dispatch");
        const int beforeFocusRevocation=web->focusSignals;web->duringFocus=[&]{owner.hide();};owner.setEditorFocus();web->duringFocus={};
        check(web->focusSignals==beforeFocusRevocation,"revocation from QWidget focus continuation stops before editor focus script");
        authorize();const int beforeScriptRevocation=web->focusSignals;web->sourcePage.duringScript=[&]{owner.hide();};web->editorFocus();web->sourcePage.duringScript={};
        check(web->focusSignals==beforeScriptRevocation,"revocation during editor focus dispatch suppresses the later focus-in notification");
        authorize();const int resetsBeforeReentry=owner.titleResets;web->tryResetTitle();auto reentrantTitle=takeText(web);
        owner.duringResetTitle=[&]{reentrantTitle("Nested title");};reentrantTitle("First title");owner.duringResetTitle={};
        check(owner.titleResets==resetsBeforeReentry+1,"automatic-title initialization is claimed before a reentrant resetTitle signal");
        web->m_bNewNoteTitleInited=false;web->tryResetTitle();auto switchedTitle=takeText(web);
        owner.duringResetTitle=[&]{owner.viewNote(c,false);web->m_bNewNoteTitleInited=false;};switchedTitle("Origin title");owner.duringResetTitle={};
        check(owner.m_note.strGUID==c.strGUID&&!web->m_bNewNoteTitleInited,"note switch inside resetTitle never marks the replacement document initialized on return");
        web->sourcePage.commands.clear();const int plainPastes=web->pasteRequests;web->undo();web->redo();web->doPaste();app.processEvents();
        check(web->sourcePage.commands.size()==2&&web->pasteRequests==plainPastes+1,"ordinary readable plain notes keep undo redo and queued paste behavior");
        const int blankFocus=web->focusRequests;owner.m_stack->setCurrentWidget(owner.m_blankView);owner.setEditorFocus();
        check(web->focusRequests==blankFocus,"blank or transition stack pages are not replaced by an editor focus request");
        auto* gone=new RoutingView(explorer);gone->show();gone->viewNote(a,false);submit(*gone);gone->m_note.tModified=gone->m_note.tCreated;
        auto* oldWeb=gone->m_web;oldWeb->tryResetTitle();auto afterDestroy=takeText(oldWeb);QPointer<Web> goneWeb(oldWeb);delete gone;afterDestroy("Destroyed owner");
        check(!goneWeb,"title callback after owner destruction is dropped without reading a closed view");
        gone=new RoutingView(explorer);gone->show();gone->viewNote(a,false);submit(*gone);gone->m_note.tModified=gone->m_note.tCreated;
        oldWeb=gone->m_web;oldWeb->tryResetTitle();auto destroyedInReset=takeText(oldWeb);goneWeb=oldWeb;gone->duringResetTitle=[&]{delete gone;};destroyedInReset("Destroy inside reset");
        check(!goneWeb,"owner destruction inside resetTitle leaves no post-signal access or initialization write");
        gone=new RoutingView(explorer);gone->show();gone->viewNote(a,false);submit(*gone);goneWeb=gone->m_web;
        goneWeb->doPaste();delete gone;app.processEvents();check(!goneWeb,"pending paste in the actual main queue tolerates owner destruction");
        gone=new RoutingView(explorer);gone->show();gone->viewNote(a,false);submit(*gone);goneWeb=gone->m_web;
        goneWeb->duringFocus=[&]{delete gone;};gone->setEditorFocus();check(!goneWeb,"owner destruction during focus continuation suppresses further editor access");
        app.processEvents();WizQueuedThreadsShutdown();
    }
    // Phase145: actual synchronous toolbar method bodies; recording page and
    // diagnostics only, never actual clipboard, picker or main-window tools.
    {
        RoutingView owner(explorer);owner.show();owner.viewNote(a,false);submit(owner);
        auto* web=owner.m_web;web->body="dirty toolbar fixture";const auto saveTicket=web->documentCallbackTicket();
        owner.hide();
        const auto reset=[&]{web->sourcePage.commands.clear();web->sourcePage.queryCommands.clear();web->sourcePage.scripts.clear();web->m_bContentsChanged=false;web->m_contentChangeSequence=0;};
        reset();web->editorCommandExecuteCommand("bold");
        check(web->sourcePage.commands.empty()&&!web->m_bContentsChanged&&web->m_contentChangeSequence==0,"locked generic format command neither dispatches JavaScript nor marks a note dirty");
        reset();web->editorExecJs("WizEditor.marker.undo();");check(web->sourcePage.commands.empty(),"locked toolbar raw-script gateway refuses the command");
        reset();web->editorCommandExecuteInsertHtml("<b>synthetic</b>",true);check(web->sourcePage.commands.empty(),"locked HTML insertion refuses before dispatch");
        reset();web->editorCommandExecuteFontSize("13");check(web->sourcePage.commands.empty()&&!web->m_bContentsChanged,"locked font-size direct path does not bypass format permission");
        reset();web->editorCommandExecuteTableInsert(3,4);web->editorCommandExecuteInsertCheckList();web->editorCommandExecuteInsertCode();
        check(web->sourcePage.commands.empty(),"locked table checklist and code insertions do not reach the editor");
        reset();web->editorCommandExecuteFormatPainterOn(false);web->editorCommandExecuteFormatPainterOff();
        check(web->sourcePage.queryCommands.empty()&&web->sourcePage.scripts.empty(),"locked format-painter operations do not start asynchronous script queries");
        reset();web->replaceAll("source","target",true);check(web->sourcePage.commands.empty()&&!web->m_bContentsChanged,"locked replace-all neither dispatches nor dirties a hidden note");
        const int types=owner.typeChanges;web->editorCommandExecuteInsertPainter();
        check(owner.typeChanges==types&&web->sourcePage.commands.empty(),"locked painter insertion cannot change note type or dispatch its creation script");
        reset();owner.show();submit(owner);web->editorCommandExecuteCommand("bold");
        check(web->sourcePage.commands==QStringList({"WizEditor.execCommand('bold');"})&&web->m_bContentsChanged&&web->m_contentChangeSequence==1,"authorized generic format command preserves its exact script and one dirty notification");
        reset();web->sourcePage.duringScript=[&]{owner.viewNote(c,false);web->m_bContentsChanged=false;web->m_contentChangeSequence=73;};
        web->editorCommandExecuteCommand("italic");web->sourcePage.duringScript={};
        check(!web->m_bContentsChanged&&web->m_contentChangeSequence==73,"note switch during script dispatch cannot mark the replacement note dirty afterward");
        check(WizDocumentCallbackEpoch::current(saveTicket)&&db.getCertPassword()==password,"toolbar revocation does not cancel independent save tickets or clear the save cache");
        // Phase145 extended caller matrix; original clock calls are the only
        // additional collaborators fixed for deterministic date/time checks.
        owner.viewNote(a,false);submit(owner);owner.m_note.strTitle="合成 `标题`";
        const QString html="<b>合成</b>",htmlScript=QString("WizEditor.insertB64Html('%1')").arg(QString::fromLatin1(html.toUtf8().toBase64()));
        struct CommandCase {QString name;std::function<void(Web&)> run;QStringList commands,queries;bool dirty=false;};
        const std::vector<CommandCase> commands={
            {"bold",[](Web& w){w.editorCommandExecuteBold();},{"WizEditor.execCommand('bold');"},{},true},
            {"italic",[](Web& w){w.editorCommandExecuteItalic();},{"WizEditor.execCommand('italic');"},{},true},
            {"underline",[](Web& w){w.editorCommandExecuteUnderLine();},{"WizEditor.execCommand('underline');"},{},true},
            {"strike",[](Web& w){w.editorCommandExecuteStrikeThrough();},{"WizEditor.execCommand('strikethrough');"},{},true},
            {"subscript",[](Web& w){w.editorCommandExecuteSubScript();},{"WizEditor.execCommand('subscript');"},{},true},
            {"superscript",[](Web& w){w.editorCommandExecuteSuperScript();},{"WizEditor.execCommand('superscript');"},{},true},
            {"paragraph",[](Web& w){w.editorCommandExecuteParagraph("h2");},{"WizEditor.execCommand('formatBlock', false, 'h2');"},{},true},
            {"font family",[](Web& w){w.editorCommandExecuteFontFamily("Test Font");},{"WizEditor.execCommand('fontName', false, 'Test Font');"},{},true},
            {"font size",[](Web& w){w.editorCommandExecuteFontSize("13");},{"WizEditor.modifySelectionDom(JSON.parse(\"{\\\"font-size\\\" : \\\"13pt\\\"}\"))"},{},true},
            {"background color",[](Web& w){w.editorCommandExecuteBackColor(QColor("#123456"));},{"WizEditor.execCommand('backColor', false, '#123456');"},{},true},
            {"foreground color",[](Web& w){w.editorCommandExecuteForeColor(QColor("#abcdef"));},{"WizEditor.execCommand('foreColor', false, '#abcdef');"},{},true},
            {"transparent background",[](Web& w){w.editorCommandExecuteBackColor(Qt::transparent);},{},{},false},
            {"transparent foreground",[](Web& w){w.editorCommandExecuteForeColor(Qt::transparent);},{},{},false},
            {"indent",[](Web& w){w.editorCommandExecuteIndent();},{"WizEditor.execCommand('indent');"},{},true},
            {"outdent",[](Web& w){w.editorCommandExecuteOutdent();},{"WizEditor.execCommand('outdent');"},{},true},
            {"unlink",[](Web& w){w.editorCommandExecuteLinkRemove();},{"WizEditor.execCommand('unlink');"},{},true},
            {"justify left",[](Web& w){w.editorCommandExecuteJustifyLeft();},{"WizEditor.execCommand('JustifyLeft');"},{},true},
            {"justify right",[](Web& w){w.editorCommandExecuteJustifyRight();},{"WizEditor.execCommand('JustifyRight');"},{},true},
            {"justify center",[](Web& w){w.editorCommandExecuteJustifyCenter();},{"WizEditor.execCommand('JustifyCenter');"},{},true},
            {"justify full",[](Web& w){w.editorCommandExecuteJustifyJustify();},{"WizEditor.execCommand('justify', 'justify');"},{},true},
            {"ordered list",[](Web& w){w.editorCommandExecuteInsertOrderedList();},{"WizEditor.execCommand('insertOrderedList');"},{},true},
            {"unordered list",[](Web& w){w.editorCommandExecuteInsertUnorderedList();},{"WizEditor.execCommand('insertUnorderedList');"},{},true},
            {"horizontal rule",[](Web& w){w.editorCommandExecuteInsertHorizontal();},{"WizEditor.execCommand('InsertHorizontalRule');"},{},true},
            {"remove format",[](Web& w){w.editorCommandExecuteRemoveFormat();},{"WizEditor.execCommand('removeFormat');"},{},true},
            {"HTML serialize true",[&](Web& w){w.editorCommandExecuteInsertHtml(html,true);},{htmlScript},{},false},
            {"HTML serialize false",[&](Web& w){w.editorCommandExecuteInsertHtml(html,false);},{htmlScript},{},false},
            {"table row-column order",[](Web& w){w.editorCommandExecuteTableInsert(2,5);},{"WizEditor.table.insertTable(5, 2);"},{},false},
            {"checklist",[](Web& w){w.editorCommandExecuteInsertCheckList();},{"WizEditor.todo.setTodo();"},{},false},
            {"insert code",[](Web& w){w.editorCommandExecuteInsertCode();},{"WizEditor.code.insertCode();"},{},false},
            {"markup start",[](Web& w){w.editorCommandExecuteStartMarkup();},{"WizEditor.marker.start();"},{},false},
            {"markup stop",[](Web& w){w.editorCommandExecuteStopMarkup();},{"WizEditor.marker.quit();"},{},false},
            {"raw toolbar",[](Web& w){w.editorExecJs("WizEditor.marker.undo();");},{"WizEditor.marker.undo();"},{},false},
            {"mind map show",[](Web& w){w.onViewMindMap(true);},{"WizEditor.outline.showMinder(`合成 \\`标题\\``);"},{},false},
            {"mind map hide",[](Web& w){w.onViewMindMap(false);},{"WizEditor.outline.hideMinder();"},{},false},
            {"painter insert",[](Web& w){w.editorCommandExecuteInsertPainter();},{"WizEditor.createSvg()"},{},false},
            {"date insert",[](Web& w){w.editorCommandExecuteInsertDate();},{QString("WizEditor.insertB64Html('%1')").arg(QString::fromLatin1(QLocale().toString(ToolbarClock::date(),QLocale::LongFormat).toUtf8().toBase64()))},{},false},
            {"time insert",[](Web& w){w.editorCommandExecuteInsertTime();},{QString("WizEditor.insertB64Html('%1')").arg(QString::fromLatin1(QLocale().toString(ToolbarClock::time(),QLocale::LongFormat).toUtf8().toBase64()))},{},false},
            {"format painter single",[](Web& w){w.editorCommandExecuteFormatPainterOn(false);},{},{"WizEditor.formatPainter.on(false);"},false},
            {"format painter multi",[](Web& w){w.editorCommandExecuteFormatPainterOn(true);},{},{"WizEditor.formatPainter.on(true);"},false},
            {"format painter off",[](Web& w){w.editorCommandExecuteFormatPainterOff();},{},{"WizEditor.formatPainter.off();"},false},
            {"replace current",[](Web& w){w.replaceCurrent("A","B");},{"WizEditor.replace('A', 'B', true)"},{},false},
            {"replace next",[](Web& w){w.replaceAndFindNext("A","B",false);},{"WizEditor.replace('A', 'B', false)"},{},false},
            {"replace all",[](Web& w){w.replaceAll("A","B",true);},{"WizEditor.replaceAll('A', 'B', true)"},{},true},
            {"three command arguments",[](Web& w){w.editorCommandExecuteCommand("synthetic","false","'A'","7");},{"WizEditor.execCommand('synthetic', false, 'A', 7);"},{},true}
        };
        const auto verifyCase=[&](const CommandCase& item,const QString& prefix,bool allowed){
            reset();const int changes=owner.typeChanges;item.run(*web);
            const bool expectedType=allowed&&item.name=="painter insert";
            const bool ok=allowed?(web->sourcePage.commands==item.commands&&web->sourcePage.queryCommands==item.queries
                &&web->m_bContentsChanged==item.dirty&&web->m_contentChangeSequence==quint64(item.dirty)
                &&owner.typeChanges==changes+int(expectedType))
                :(web->sourcePage.commands.empty()&&web->sourcePage.queryCommands.empty()&&!web->m_bContentsChanged&&web->m_contentChangeSequence==0&&owner.typeChanges==changes);
            const auto label=(prefix+item.name).toUtf8();check(ok,label.constData());
        };
        for(const auto& item:commands)verifyCase(item,"authorized toolbar exact script and dirty policy: ",true);
        owner.hide();for(const auto& item:commands)verifyCase(item,"locked toolbar refuses dispatch and mutation: ",false);
        owner.show();submit(owner);web->body="preserved toolbar body";const auto matrixSaveTicket=web->documentCallbackTicket();
        reset();web->editorCommandExecuteFormatPainterOn(false);const int logs=ToolbarDiagnostics::messages.size();
        auto painterReply=std::move(web->sourcePage.scripts.back());web->sourcePage.scripts.clear();owner.hide();painterReply(QVariant(false));
        check(ToolbarDiagnostics::messages.size()==logs,"format-painter reply after lock does not continue obsolete diagnostics");
        owner.show();submit(owner);reset();web->editorCommandExecuteFormatPainterOff();auto renewedReply=std::move(web->sourcePage.scripts.back());web->sourcePage.scripts.clear();
        owner.hide();owner.show();submit(owner);renewedReply(QVariant(false));check(ToolbarDiagnostics::messages.size()==logs,"old format-painter reply cannot borrow a renewed authorization");
        reset();web->editorCommandExecuteFormatPainterOn(true);auto failedReply=std::move(web->sourcePage.scripts.back());web->sourcePage.scripts.clear();failedReply(QVariant(false));
        check(ToolbarDiagnostics::messages.size()==logs+1,"current failed format-painter reply retains its original diagnostic");
        ToolbarDiagnostics::messages.clear();
        QAction bold;QObject::connect(&bold,&QAction::triggered,web,&Web::editorCommandExecuteBold);reset();
        const bool queued=QMetaObject::invokeMethod(&bold,"trigger",Qt::QueuedConnection);owner.hide();app.processEvents();
        check(queued&&web->sourcePage.commands.empty()&&!web->m_bContentsChanged,"real queued format QAction after lock cannot dispatch or dirty the hidden note");
        owner.show();submit(owner);ReadPage replaced;reset();
        web->sourcePage.duringScript=[&]{web->activePage=&replaced;web->m_contentChangeSequence=41;};web->editorCommandExecuteFontSize("14");web->sourcePage.duringScript={};
        check(web->m_contentChangeSequence==41&&!web->m_bContentsChanged,"font-size dispatch cannot mark a replacement page dirty after returning");web->activePage=&web->sourcePage;
        reset();web->sourcePage.duringScript=[&]{owner.hide();};web->replaceAll("A","B",false);web->sourcePage.duringScript={};
        check(!web->m_bContentsChanged&&web->m_contentChangeSequence==0,"replace-all dispatch drops its later dirty marker after revocation");
        check(web->body=="preserved toolbar body"&&WizDocumentCallbackEpoch::current(matrixSaveTicket)&&db.getCertPassword()==password,
              "full toolbar matrix leaves unsaved body independent save ticket and cache unchanged");
        owner.show();submit(owner);owner.viewNote(c,false);reset();web->editorCommandExecuteBold();web->editorCommandExecuteInsertHtml(html,true);web->editorCommandExecuteTableInsert(1,2);
        check(web->sourcePage.commands==QStringList({"WizEditor.execCommand('bold');",htmlScript,"WizEditor.table.insertTable(2, 1);"})&&web->m_contentChangeSequence==1,
              "ordinary plain notes retain authorized format HTML and table insertion behavior");
        for(const auto& operation:std::vector<std::function<void(Web&)>>{
                [](Web& w){w.editorCommandExecuteCommand("bold");},[](Web& w){w.editorCommandExecuteFontSize("12");},[](Web& w){w.replaceAll("A","B",true);}}){
            auto* gone=new RoutingView(explorer);gone->show();gone->viewNote(a,false);submit(*gone);QPointer<Web> weak(gone->m_web);
            weak->sourcePage.duringScript=[&]{delete gone;};operation(*weak);check(!weak,"toolbar dispatch tolerates owner destruction without a later dirty write");
        }
        auto* gone=new RoutingView(explorer);gone->show();gone->viewNote(a,false);submit(*gone);auto* goneWeb=gone->m_web;
        goneWeb->editorCommandExecuteFormatPainterOff();auto destroyedReply=std::move(goneWeb->sourcePage.scripts.back());goneWeb->sourcePage.scripts.clear();delete gone;
        destroyedReply(QVariant(false));check(ToolbarDiagnostics::messages.empty(),"format-painter reply after owner destruction never touches the closed note");
        ToolbarAnalyzer::events.clear();
    }
    check(view.downloads==0&&unrelated.downloads==0,"all extracted flows stay in downloaded synthetic notes with no downloader work");
    check(db.getCertPassword()==password,"routing revocations and failures preserve the original synthetic save cache");
    WizDatabase::clearCertPassword();
    qInfo().noquote()<<"biometric-document-routing checks"<<checks<<"failures"<<failures;
    return failures?1:0;
}
