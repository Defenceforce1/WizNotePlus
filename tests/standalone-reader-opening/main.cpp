// Public-interface-only timing diagnostic. Never opens an account or note.
#include "share/WizDocumentPageReady.h"
#include "share/WizReaderOpeningPresentation.h"
#include <QApplication>
#include <QWebEngineView>
#include <QWindow>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>
#include <QWebChannel>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <functional>
#include <memory>
#include <atomic>

class Bridge final:public QObject {
    Q_OBJECT
    Q_PROPERTY(QString userGuid READ guid CONSTANT)
    Q_PROPERTY(QString userAlias READ alias CONSTANT)
    Q_PROPERTY(QString userAvatarFilePath READ avatar CONSTANT)
    Q_PROPERTY(QString currentNoteHtml READ html CONSTANT)
    Q_PROPERTY(bool isPersonalDocument READ allowed CONSTANT)
    Q_PROPERTY(bool canEditNote READ allowed CONSTANT)
    Q_PROPERTY(bool hasEditPermissionOnCurrentNote READ allowed CONSTANT)
public:
    QString original;
    QString guid()const{return "synthetic-opening-user";}QString alias()const{return "Synthetic";}QString avatar()const{return {};}
    QString html()const{return original;}bool allowed()const{return true;}
    Q_INVOKABLE bool canRenderMarkdown()const{return true;}Q_INVOKABLE bool shouldAddCustomCSS()const{return true;}
    Q_INVOKABLE QString getLocalLanguage()const{return "zh-cn";}
    Q_INVOKABLE void onSelectionChange(const QString&){}Q_INVOKABLE void onMarkerUndoStatusChanged(const QString&){}
    Q_INVOKABLE void onMarkerInitiated(const QString&){}Q_INVOKABLE void onNoteLoadFinished(){}Q_INVOKABLE void setModified(bool){}
signals:void clickingTodoCallBack(bool,bool);
};
class LocalOnly final:public QWebEngineUrlRequestInterceptor {
public:std::atomic<int> blocked{0};void interceptRequest(QWebEngineUrlRequestInfo& r)override{if(!QStringList{"file","qrc","data","about","blob"}.contains(r.requestUrl().scheme())){++blocked;r.block(true);}}
};
bool wait(const std::function<bool()>& pred,int ms=35000){if(pred())return true;QEventLoop loop;QTimer poll,timeout;bool ok=false;QObject::connect(&poll,&QTimer::timeout,[&]{if(pred()){ok=true;loop.quit();}});QObject::connect(&timeout,&QTimer::timeout,&loop,&QEventLoop::quit);poll.start(15);timeout.setSingleShot(true);timeout.start(ms);loop.exec();return ok;}
QVariant js(QWebEnginePage* page,const QString& code){struct State{bool done=false;QVariant value;};auto s=std::make_shared<State>();page->runJavaScript(code,[s](const QVariant& v){s->value=v;s->done=true;});wait([s]{return s->done;},5000);return s->value;}
QString literal(const QString& s){return QString::fromUtf8(QJsonDocument(QJsonArray{s}).toJson(QJsonDocument::Compact))+"[0]";}
int main(int argc,char**argv){
    if(argc<3||argc>4)return 4;
    QCoreApplication::setAttribute(Qt::AA_PluginApplication);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QTemporaryDir root;if(!root.isValid())return 2;
    const QString resources=QDir(QString::fromLocal8Bit(argv[1])).absolutePath()+"/",kind=QString::fromLocal8Bit(argv[2]);
    const bool gated=argc==4&&QString::fromLocal8Bit(argv[3])=="gated";
    if(!QStringList{"plain","math","bare","controls"}.contains(kind)||QGuiApplication::platformName()!="cocoa")return 4;
    QWebEngineProfile profile;LocalOnly local;profile.setUrlRequestInterceptor(&local);
    QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(900,600);
    auto* page=new QWebEnginePage(&profile,&view);view.setPage(page);QWebChannel channel;Bridge editor,explorer;
    channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);page->setWebChannel(&channel);
    if(kind=="controls") {
        int checks=0,failures=0,index=0;const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;};
        QString cached="<head><script id='user-script'>window.synthetic=1;</script>"+WizReaderOpeningPresentation::bootstrapHtml()+WizReaderOpeningPresentation::bootstrapHtml()+"</head><body>Source</body>";
        WizReaderOpeningPresentation::removeBootstrap(cached);
        check(!cached.contains("wiz_reader_opening_bootstrap")&&cached.contains("id='user-script'")&&cached.endsWith("<body>Source</body>"),"cached reopen removes only owned bootstraps, preserving source and user scripts");
        const QString clean=cached;WizReaderOpeningPresentation::removeBootstrap(cached);check(cached==clean,"bootstrap cleanup is idempotent for edit-mode reopen");
        const auto visible=[&]{return js(page,"getComputedStyle(document.documentElement).opacity!=='0'").toBool();};
        const auto load=[&](const QString& before,bool gate=true){
            const QString file=root.filePath(QString("control-%1.html").arg(++index));QFile f(file);if(!f.open(QFile::WriteOnly))return false;
            f.write(("<!doctype html><html><head><script>"+before+"</script>"+(gate?WizReaderOpeningPresentation::bootstrapHtml():QString())+"</head><body><p id='source'>Synthetic preserved text</p></body></html>").toUtf8());f.close();
            bool done=false,ok=false;const auto connection=QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool success){done=true;ok=success;});view.show();view.load(QUrl::fromLocalFile(file));const bool completed=wait([&]{return done;});QObject::disconnect(connection);return completed&&ok;
        };
        check(load("window.WizNativeEditorSetup={state:'pending'};"),"pending control loads");check(!visible(),"pending rendering stays hidden without altering DOM");
        check(js(page,"document.getElementById('source').textContent==='Synthetic preserved text'&&document.getElementById('wiz_tmp_style_reader_opening').getAttribute('wiz_style')==='unsave'").toBool(),"source and unsaved-style marker preserved");
        js(page,"WizNativeEditorSetup.state='failed'");check(wait(visible,1500),"failed initialization restores presentation");
        check(load("window.WizNativeEditorSetup={state:'pending'};"),"editing cancellation control loads");js(page,"document.body.contentEditable='true'");check(wait(visible,1500)&&js(page,"document.body.isContentEditable").toBool(),"switch to editing reveals intact editable DOM");
        check(load("const realTimeout=setTimeout;window.setTimeout=(f,ms)=>realTimeout(f,ms===30000?80:ms);"),"timeout-only control loads");check(wait(visible,1500),"missing initialization has a bounded visible presentation fallback");
        check(load("window.WizNativeEditorSetup={state:'pending'};window.framesToRun=[];window.requestAnimationFrame=f=>{framesToRun.push(f);return framesToRun.length;};window.cancelAnimationFrame=()=>{};"),"late-token control loads");
        js(page,"WizNativeEditorSetup.state='ready'");check(wait([&]{return js(page,"framesToRun.length===1").toBool();},1500),"ready state waits for frame boundary");
        js(page,"window.WizNativeEditorSetup={state:'pending'};framesToRun.shift()();framesToRun.shift()();");check(!visible(),"superseded ready state cannot reveal the replacement");
        js(page,"WizNativeEditorSetup.state='ready'");check(wait([&]{return js(page,"framesToRun.length===1").toBool();},1500),"replacement completion is observed");js(page,"framesToRun.shift()();framesToRun.shift()();");check(visible(),"current ready state reveals after both frame boundaries");
        check(load("window.WizNativeEditorSetup={state:'pending'};"),"navigation cancellation control loads");js(page,"window.dispatchEvent(new Event('pagehide'))");check(visible(),"navigation teardown retires presentation timers");
        check(load("",false)&&visible(),"ordinary or editor HTML without gate stays visible");check(local.blocked.load()==0,"all presentation controls use local resources only");
        profile.setUrlRequestInterceptor(nullptr);qInfo()<<"Reader presentation controls checks"<<checks<<"failures"<<failures;return failures?1:0;
    }
    QString source="# Synthetic opening\n\nA paragraph without math.\n";
    if(kind=="math")source+="\n$$a+b=c$$\n\n$x^2\\ge0$.\n";
    if(kind=="bare")source+="\n\\begin{equation}\\label{synthetic:one}\na+b=c\n\\end{equation}\n\nSee \\eqref{synthetic:one}.\n";
    editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+source.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
    QString html=editor.original;
    if(gated)html.insert(html.indexOf("</head>"),WizReaderOpeningPresentation::bootstrapHtml());
    QString tags;
    for(const auto& f:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})
        tags+="<script src=\""+QUrl::fromLocalFile(resources+f).toString().toHtmlEscaped()+"\" name='wiz_inner_script' wiz_style='unsave'></script>";
    html.insert(html.lastIndexOf("</head>"),tags);
    QWebEngineScript observer;observer.setInjectionPoint(QWebEngineScript::DocumentCreation);observer.setWorldId(QWebEngineScript::MainWorld);
    observer.setSourceCode(R"JS(
window.openingEvents=[];window.openingFrames=[];
window.openingMark=function(name){if(openingEvents.length<100)openingEvents.push({name:name,ms:Math.round(performance.now()),bodyClass:document.body?document.body.className:null,editing:document.body?document.body.isContentEditable:null,text:document.body?document.body.textContent.slice(0,120):null});};
document.addEventListener('DOMContentLoaded',function(){openingMark('dom-ready');let previous='';const sample=()=>{const b=document.body;if(b){const visible=getComputedStyle(b).visibility!=='hidden'&&getComputedStyle(document.documentElement).opacity!=='0';const s=JSON.stringify([b.className,b.textContent.slice(0,120),b.scrollHeight,document.querySelectorAll('.MathJax_SVG,.MathJax_Display').length,visible]);if(s!==previous&&openingFrames.length<100){openingFrames.push({ms:Math.round(performance.now()),visible:visible,setupState:window.WizNativeEditorSetup?WizNativeEditorSetup.state:null,signature:s});previous=s;}}if(performance.now()<15000)requestAnimationFrame(sample);};requestAnimationFrame(sample);});
)JS");page->scripts().insert(observer);
    const QString file=root.filePath("synthetic.html");QFile output(file);if(!output.open(QFile::WriteOnly))return 2;output.write(html.toUtf8());output.close();
    int completedLoads=0;bool loaded=false,ok=false;QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool v){++completedLoads;loaded=true;ok=v;});view.show();view.load(QUrl::fromLocalFile(file));
    if(!wait([&]{return loaded;})||!ok)return 3;
    js(page,R"JS(
openingMark('native-load-finished');
const originalInit=WizEditor.init;WizEditor.init=function(options){openingMark('init-begin');for(const n of ['markdown','mathJax']){const original=options.reader.callback[n];options.reader.callback[n]=function(){openingMark(n+'-callback');if(original)return original.apply(this,arguments);};}const result=originalInit.apply(this,arguments);openingMark('init-return');return result;};
const originalStyle=WizApplyNoteStyle;WizApplyNoteStyle=function(){openingMark('apply-style');return originalStyle.apply(this,arguments);};
)JS");
    QString body="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';";
    body+=WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-opening-user","Synthetic",false,"markdown");
    body+="openingMark('native-init-resolved');WizPrepareNoteModeChange();openingMark('native-off-begin');await new Promise(resolve=>WizEditor.off(null,()=>{openingMark('native-off-complete');resolve();}));WizApplyNoteStyle();WizApplySpellCheck();";
    page->runJavaScript(WizDocumentPageReady::setupScript(literal("opening"),body));
    if(!wait([&]{return js(page,"WizNativeEditorSetup.state").toString()=="ready";}))return 5;
    if(!wait([&]{return js(page,"!window.MathJax||!MathJax.Hub||(!MathJax.Hub.queue.running&&!MathJax.Hub.queue.pending)").toBool();}))return 6;
    QEventLoop settle;QTimer::singleShot(500,&settle,&QEventLoop::quit);settle.exec();
    const bool sourceIntact=js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source;
    const auto report=QJsonObject{{"kind",kind},{"gated",gated},{"completedLoads",completedLoads},{"events",QJsonValue::fromVariant(js(page,"openingEvents"))},{"frames",QJsonValue::fromVariant(js(page,"openingFrames"))},{"sourceIntact",sourceIntact},{"readOnly",js(page,"document.body.classList.contains('wiz-readonly')&&!document.body.isContentEditable").toBool()},{"blockedExternalRequests",local.blocked.load()},{"hiddenCocoa",view.testAttribute(Qt::WA_DontShowOnScreen)&&(!view.windowHandle()||!view.windowHandle()->isExposed())},{"domSnapshot",js(page,"document.body.textContent.slice(0,300)").toString()}};
    qInfo().noquote()<<"OPENING_REPORT"<<QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact));
    profile.setUrlRequestInterceptor(nullptr);return sourceIntact&&local.blocked==0?0:7;
}
#include "main.moc"
