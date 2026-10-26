// Actual load/preparation bodies with local UTF-8 files, not a client session.
#include "share/WizDocumentReadPermit.h"
#define writeUtf8 originalWriteUtf8
#include "share/WizDocumentReadOutput.h"
#undef writeUtf8
#include "share/WizReaderOpeningPresentation.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QMap>
#include <QUrl>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <map>

struct WIZDOCUMENTDATA {QString strGUID="synthetic-note",strKbGUID="synthetic-kb";};
enum WizEditorMode {modeReader,modeEditor};
struct Io {
    static inline int reads=0,writes=0;static inline qint64 bytesRead=0;
    static inline bool failWrite=false;static inline std::function<void()> duringRead;
    static void reset(){reads=writes=0;bytesRead=0;failWrite=false;duringRead={};}
};
bool WizLoadUnicodeTextFromFile(const QString& path,QString& text){
    ++Io::reads;QFile f(path);const bool ok=f.open(QIODevice::ReadOnly);
    if(ok){auto bytes=f.readAll();Io::bytesRead+=bytes.size();if(bytes.startsWith(QByteArray::fromHex("efbbbf")))bytes.remove(0,3);text=QString::fromUtf8(bytes);}
    const auto action=Io::duringRead;if(action)action();return ok;
}
namespace WizDocumentReadOutput {
bool writeUtf8(const WizDocumentReadPermit& permit,const QString& path,const QString& html,bool bom){++Io::writes;return !Io::failWrite&&originalWriteUtf8(permit,path,html,bom);}
}
void WizHTMLAppendTextInHead(const QString& added,QString& html){html.insert(html.indexOf("</head>"),added);}
struct Title {std::function<void()> changed;void setEditorMode(WizEditorMode){const auto action=changed;if(action)action();}};
class WizDocumentView:public QObject {
public:
    WIZDOCUMENTDATA document;quint64 access=1;bool allowed=true;Title title;
    const WIZDOCUMENTDATA& note()const{return document;}
    quint64 noteReadTicket()const{return access;}
    bool acceptsNoteRead(quint64 ticket)const{return allowed&&access==ticket;}
    Title* titleBar(){return &title;}
};
class WizDocumentWebView:public QObject {
public:
    WizDocumentView* owner;QObject source;WizDocumentCallbackEpoch m_documentCallbacks,saveEpoch;WizDocumentLoadSequence m_loadSequence;
    QString m_currentNoteHtml,m_strNoteHtmlFileName,type="common",unsavedDom="keep unsaved synthetic DOM";
    WIZDOCUMENTDATA m_currentNoteHtmlDocument;QMap<QString,QString> m_mapFile;WizEditorMode m_currentEditorMode=modeReader;
    int publications=0,loads=0,notifications=0;quint64 dirty=7;QUrl lastUrl;QStringList stages;
    std::function<void(const QString&)> duringStage;
    explicit WizDocumentWebView(WizDocumentView* host):QObject(host),owner(host){m_loadSequence.next();}
    WizDocumentView* view()const{return owner;}QObject* page(){return &source;}
    WizDocumentReadPermit noteReadPermit(){
        const QPointer<WizDocumentWebView> self(this);const QPointer<WizDocumentView> host(owner);const auto doc=host->note();const auto access=host->noteReadTicket();
        return {this,page(),m_documentCallbacks.ticket(),[self,host,doc,access]{return self&&host&&host->acceptsNoteRead(access)&&host->note().strGUID==doc.strGUID&&host->note().strKbGUID==doc.strKbGUID;}};
    }
    void mark(const QString& stage){stages.push_back(stage);const auto action=duringStage;if(action)action(stage);}
    void currentHtmlChanged(){++publications;mark("publish");}
    void getAllEditorScriptAndStyleFileName(std::map<QString,QString>& files){files["synthetic-local-script"]="";mark("files");}
    void insertScriptAndStyleCore(QString& html,const std::map<QString,QString>&){WizHTMLAppendTextInHead("<!-- core -->",html);mark("core");}
    void replaceDefaultCss(QString& html){WizHTMLAppendTextInHead("<!-- css -->",html);mark("css");}
    QString getNoteType(){return type;}
    void focusIn(){mark("focus");}void focusOut(){mark("focus");}
    WizDocumentCallbackEpoch::Ticket invalidateDocumentCallbacks(){m_documentCallbacks.invalidate();mark("retire");return m_documentCallbacks.ticket();}
    QUrl armEditorPageLoad(const QUrl& url){return url;}
    void load(const QUrl& url){++loads;lastUrl=url;m_documentCallbacks.invalidate();mark("load");}
    void onNoteLoadFinished(){++notifications;mark("notify");}
    bool addDefaultScriptsToDocumentHtml(QString,bool=false);
    bool loadDocumentInWeb(WizEditorMode,WizDocumentCallbackEpoch::Ticket* =nullptr);
    QString getCurrentNoteHtml();
};
#include WIZ_INITIAL_NOTE_LOAD_IMPL

static QByteArray bytes(const QString& path){QFile f(path);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray();}
static bool seed(const QString& path,const QString& html){QFile f(path);return f.open(QIODevice::WriteOnly)&&f.write(QByteArray::fromHex("efbbbf")+html.toUtf8())==html.toUtf8().size()+3;}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);QTemporaryDir dir;if(!dir.isValid())return 2;const auto file=dir.filePath("initial.html");
    int checks=0,failures=0;QJsonArray cases;
    const auto check=[&](bool v,const char* message){++checks;failures+=!v;qInfo().noquote()<<(v?"PASS":"FAIL")<<message;};
    QString original="<!doctype html><html><head><meta charset='utf-8'><style data-user='keep'>p{color:#246}</style></head><body>";
    for(int i=0;i<6000;++i)original+=QString("<p>Row %1: 离线合成正文 &amp; literal entity. <b>Keep</b> \\begin{equation} A+B \\end{equation}</p>\n").arg(i);
    original+="</body></html>";
    for(int scenario=0;scenario<4;++scenario){
        QString input=original;if(scenario==3)WizHTMLAppendTextInHead(WizReaderOpeningPresentation::bootstrapHtml(),input);
        check(seed(file,input),"seed only temporary synthetic UTF8 file");Io::reset();WizDocumentView host;auto* web=new WizDocumentWebView(&host);web->m_mapFile[host.note().strGUID]=file;web->type=scenario==0?"common":"markdown";
        const auto save=web->saveEpoch.ticket();const auto dirty=web->dirty;const auto mode=scenario==2?modeEditor:modeReader;
        check(web->loadDocumentInWeb(mode),"actual checked preparation and load succeeds");
        check(Io::reads==WIZ_INITIAL_NOTE_LOAD_EXPECTED_READS&&Io::writes==1,"expected complete-input read count and one atomic output");
        check(web->m_currentNoteHtml==original&&web->getCurrentNoteHtml()==original&&web->publications==1,"owned raw source cache and existing property publication remain exact");
        QString expected=original;WizHTMLAppendTextInHead("<!-- core -->",expected);WizHTMLAppendTextInHead("<!-- css -->",expected);if(mode==modeReader)WizHTMLAppendTextInHead(WizReaderOpeningPresentation::bootstrapHtml(),expected);
        check(bytes(file)==QByteArray::fromHex("efbbbf")+expected.toUtf8(),"temporary output keeps exact UTF8 BOM source CSS and reader/editor bootstrap behavior");
        check(web->loads==1&&web->notifications==1&&web->lastUrl==QUrl::fromLocalFile(file),"one navigation and legacy completion notification");
        check(web->unsavedDom=="keep unsaved synthetic DOM"&&web->dirty==dirty&&WizDocumentCallbackEpoch::current(save),"initial loading preserves retained DOM dirty marker and independent save ticket");
        cases.append(QJsonObject{{"case",scenario},{"reads",Io::reads},{"inputBytes",qint64(input.toUtf8().size()+3)},{"bytesRead",Io::bytesRead},{"outputBytes",qint64(bytes(file).size())}});
    }
    const auto fixture=[&](WizDocumentView& host){seed(file,original);Io::reset();auto* web=new WizDocumentWebView(&host);web->m_mapFile[host.note().strGUID]=file;return web;};
    {WizDocumentView host;auto* web=fixture(host);QFile::remove(file);check(!web->loadDocumentInWeb(modeReader)&&!web->loads&&!web->publications&&!Io::writes,"missing input fails before publication output or navigation");}
    {WizDocumentView host;auto* web=fixture(host);Io::failWrite=true;check(!web->loadDocumentInWeb(modeReader)&&!web->loads&&!web->notifications&&bytes(file)==QByteArray::fromHex("efbbbf")+original.toUtf8(),"atomic output failure keeps original file and prevents navigation");}
    {WizDocumentView host;auto* web=fixture(host);host.allowed=false;check(!web->loadDocumentInWeb(modeReader)&&!Io::reads&&!Io::writes&&!web->publications,"unavailable view does not start input or output");}
    {WizDocumentView host;auto* web=fixture(host);Io::duringRead=[&]{++host.access;};check(!web->loadDocumentInWeb(modeReader)&&Io::reads==1&&!Io::writes&&!web->publications&&!web->loads,"view generation changed during the only required read discards its result");Io::duringRead={};}
    for(const QString stage:{"publish","core","css","focus","retire","load"}){
        WizDocumentView host;auto* web=fixture(host);web->duringStage=[&host,stage](const QString& at){if(at==stage)++host.access;};const bool ok=web->loadDocumentInWeb(stage=="focus"?modeEditor:modeReader);
        check((stage=="load"?ok:!ok)&&web->notifications==0&&(stage=="load"?web->loads==1:web->loads==0),"reentrant view changes stop remaining initial-load continuation without retracting an already dispatched load");
    }
    {auto* host=new WizDocumentView;auto* web=fixture(*host);QPointer<WizDocumentWebView> guard(web);Io::duringRead=[host]{delete host;};const bool ok=web->loadDocumentInWeb(modeReader);Io::duringRead={};check(!ok&&!guard,"owner destroyed during actual file read stops without later member access");}
    qInfo().noquote()<<"INITIAL_NOTE_LOAD_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"sourceSha256",WIZ_INITIAL_NOTE_LOAD_SOURCE_SHA},{"expectedReads",WIZ_INITIAL_NOTE_LOAD_EXPECTED_READS},{"cases",cases},{"noAccountDatabaseRendererOrNetwork",true},{"notFullClientOrAllEncodingAcceptance",true}}).toJson(QJsonDocument::Compact));
    return failures?1:0;
}
