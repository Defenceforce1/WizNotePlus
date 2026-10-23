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
#include "stress.h"
#include "retention.h"
#include "mathjax-upgrade.h"
#include "math-references.h"
#include "mathjax-benchmark.h"
#include "mathjax-font-preview.h"
#include "frontend-components.h"
#include "ordinary-workflow.h"
#include "diagram-components.h"
int main(int argc,char**argv){
    if(argc<3||argc>5)return 4;
    QCoreApplication::setAttribute(Qt::AA_PluginApplication);
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QTemporaryDir root;if(!root.isValid())return 2;
    const QString resources=QDir(QString::fromLocal8Bit(argv[1])).absolutePath()+"/",kind=QString::fromLocal8Bit(argv[2]);
    const bool gated=argc>=4&&QString::fromLocal8Bit(argv[3])=="gated";
    if(kind=="diagram-components"&&argc==5)return runDiagramComponents(root,resources,QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
    if(kind=="math-references"&&argc==4)return runMathReferences(root,resources,QString::fromLocal8Bit(argv[3]));
    if(kind=="frontend-components"&&argc==4)return runFrontendComponents(root,resources,QString::fromLocal8Bit(argv[3]));
    if(kind=="ordinary-workflow"&&argc==4)return runOrdinaryWorkflow(root,resources,QString::fromLocal8Bit(argv[3]));
    if(kind=="mathjax-benchmark"&&argc==5)return runMathjaxBenchmark(root,resources,QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
    if(kind=="mathjax-font-preview"&&argc==5)return runMathjaxFontPreview(root,resources,QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
    if(kind=="mathjax-upgrade"&&argc==5)return runMathjaxUpgrade(root,resources,QString::fromLocal8Bit(argv[3]),QString::fromLocal8Bit(argv[4]));
    if(kind=="retention-nested"||kind=="retention-yield"||kind=="retention-nested-pool"||kind=="retention-yield-persistent"){
        if(QGuiApplication::platformName()!="cocoa")return 4;int status=0;
        NativeRetention probe(root,kind,[&](int result){status=result;app.quit();});
        QTimer::singleShot(0,&probe,[&]{probe.start();});app.exec();return status;
    }
    if(kind=="stress-prose"||kind=="stress-math"||kind=="stress-static"||kind=="stress-static-no-grab"){
        int status=0;
        QTimer::singleShot(0,&app,[&]{status=runReaderStress(root,resources,kind);QTimer::singleShot(10000,&app,[&]{
            qInfo().noquote()<<"STRESS_AFTER_SCOPE"<<QString::fromUtf8(QJsonDocument(stressMemory()).toJson(QJsonDocument::Compact));app.quit();});});
        app.exec();return status;
    }
    if(!QStringList{"plain","math","bare","many","bare-many","controls","cycle","rapid"}.contains(kind)||QGuiApplication::platformName()!="cocoa")return 4;
    QWebEngineProfile profile;profile.setCachePath(root.filePath("cache"));profile.setPersistentStoragePath(root.filePath("profile"));LocalOnly local;profile.setUrlRequestInterceptor(&local);
    QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(900,600);
    auto* page=new QWebEnginePage(&profile,&view);view.setPage(page);QWebChannel channel;Bridge editor,explorer;
    channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);page->setWebChannel(&channel);
    if(kind=="controls") {
        int checks=0,failures=0,index=0;const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;};
        QString cached="<head><script id='user-script'>window.synthetic=1;</script>"+WizReaderOpeningPresentation::bootstrapHtml()+WizReaderOpeningPresentation::bootstrapHtml()+"<style id=\"wiz_tmp_style_reader_opening\" name=\"wiz_inner_style\" wiz_style=\"unsave\">@media screen{html{opacity:0!important}}</style></head><body>Source</body>";
        WizReaderOpeningPresentation::removeBootstrap(cached);
        check(!cached.contains("wiz_reader_opening_bootstrap")&&!cached.contains("wiz_tmp_style_reader_opening")&&cached.contains("id='user-script'")&&cached.endsWith("<body>Source</body>"),"cached reopen removes only owned bootstrap and legacy hidden style, preserving source and user scripts");
        const QString clean=cached;WizReaderOpeningPresentation::removeBootstrap(cached);check(cached==clean,"bootstrap cleanup is idempotent for edit-mode reopen");
        QString userStyle="<style id='wiz_tmp_style_reader_opening'>User CSS</style>";WizReaderOpeningPresentation::removeBootstrap(userStyle);check(userStyle.contains("User CSS"),"similar user style without owned unsaved markers is preserved");
        const auto visible=[&]{return js(page,"getComputedStyle(document.documentElement).opacity!=='0'").toBool();};
        const auto load=[&](const QString& before,bool gate=true){
            const QString file=root.filePath(QString("control-%1.html").arg(++index));QFile f(file);if(!f.open(QFile::WriteOnly))return false;
            f.write(("<!doctype html><html><head><script>"+before+"</script>"+(gate?WizReaderOpeningPresentation::bootstrapHtml():QString())+"</head><body><p id='source'>Synthetic preserved text</p></body></html>").toUtf8());f.close();
            bool done=false,ok=false;const auto connection=QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool success){done=true;ok=success;});view.show();view.load(QUrl::fromLocalFile(file));const bool completed=wait([&]{return done;});QObject::disconnect(connection);return completed&&ok;
        };
        check(load("window.WizNativeEditorSetup={state:'pending'};"),"pending control loads");check(!visible(),"pending rendering stays hidden without altering DOM");
        check(js(page,"document.getElementById('source').textContent==='Synthetic preserved text'&&document.getElementById('wiz_tmp_style_reader_opening').getAttribute('wiz_style')==='unsave'").toBool(),"source and unsaved-style marker preserved");
        check(js(page,"document.getElementById('wiz_tmp_style_reader_opening').textContent===''&&document.getElementById('wiz_tmp_style_reader_opening').sheet.cssRules.length===1").toBool(),"live hide rule is absent from serialized style text");
        js(page,"WizNativeEditorSetup.state='failed'");check(wait(visible,1500),"failed initialization restores presentation");
        check(load("window.WizNativeEditorSetup={state:'pending'};"),"editing cancellation control loads");js(page,"document.body.contentEditable='true'");check(wait(visible,1500)&&js(page,"document.body.isContentEditable").toBool(),"switch to editing reveals intact editable DOM");
        check(load("const realTimeout=setTimeout;window.setTimeout=(f,ms)=>realTimeout(f,ms===30000?80:ms);"),"timeout-only control loads");check(wait(visible,1500),"missing initialization has a bounded visible presentation fallback");
        check(load("window.WizNativeEditorSetup={state:'pending'};window.framesToRun=[];window.requestAnimationFrame=f=>{framesToRun.push(f);return framesToRun.length;};window.cancelAnimationFrame=()=>{};"),"late-token control loads");
        js(page,"WizNativeEditorSetup.state='ready'");check(wait([&]{return js(page,"framesToRun.length===1").toBool();},1500),"ready state waits for frame boundary");
        js(page,"window.WizNativeEditorSetup={state:'pending'};framesToRun.shift()();");check(!visible(),"superseded ready state cannot reveal the replacement");
        js(page,"WizNativeEditorSetup.state='ready'");check(wait([&]{return js(page,"framesToRun.length===1").toBool();},1500),"replacement completion is observed");js(page,"framesToRun.shift()();");check(visible(),"current ready state reveals at the next frame boundary");
        check(load("window.WizNativeEditorSetup={state:'pending'};window.framesToRun=[];window.requestAnimationFrame=f=>{framesToRun.push(f);return framesToRun.length;};window.cancelAnimationFrame=()=>{};const originalInterval=setInterval;window.setInterval=(f,ms)=>originalInterval(f,60000);"),"notification control loads without timely polling");
        check(js(page,"WizNativeEditorSetup.state='ready';window.dispatchEvent(new Event('wiz-native-editor-setup-state'));framesToRun.length===1").toBool(),"ready notification schedules presentation synchronously without polling");
        js(page,"framesToRun.shift()();");check(visible(),"notification reveals only after current frame check");
        check(load("window.WizNativeEditorSetup={state:'pending'};const originalInterval=setInterval;window.setInterval=(f,ms)=>originalInterval(f,60000);"),"failed notification control loads");
        js(page,"WizNativeEditorSetup.state='failed';window.dispatchEvent(new Event('wiz-native-editor-setup-state'));");check(visible(),"failed notification restores visibility immediately");
        check(load("window.addEventListener('wiz-native-editor-setup-state',()=>{window.setupNotifications=(window.setupNotifications||0)+1;});",false),"native setup notification control loads");
        js(page,WizDocumentPageReady::setupScript("'notify-ready'","window.setupBodyComplete=true;"));check(wait([&]{return js(page,"WizNativeEditorSetup.state==='ready'&&setupNotifications===1&&setupBodyComplete===true").toBool();},1500),"native setup preserves ready result and emits one completion notification");
        js(page,WizDocumentPageReady::setupScript("'notify-failed'","throw Error('synthetic notified failure');"));check(wait([&]{return js(page,"WizNativeEditorSetup.state==='failed'&&setupNotifications===2").toBool();},1500),"native setup preserves failed result and emits failure notification");
        js(page,WizDocumentPageReady::setupScript("'notify-obsolete'","await new Promise(resolve=>window.finishObsoleteSetup=resolve);"));
        check(wait([&]{return js(page,"typeof finishObsoleteSetup==='function'").toBool();},1500),"delayed native setup exposes synthetic continuation");
        js(page,"window.WizNativeEditorSetup={token:'notify-replacement',state:'pending'};finishObsoleteSetup();");
        check(wait([&]{return js(page,"WizNativeEditorSetup.token==='notify-replacement'&&WizNativeEditorSetup.state==='pending'&&setupNotifications===2").toBool();},1500),"obsolete setup neither completes nor notifies the replacement");
        js(page,"window.dispatchEvent=()=>{throw Error('synthetic notification failure');};");js(page,WizDocumentPageReady::setupScript("'notify-unavailable'","window.setupBodyComplete=true;"));check(wait([&]{return js(page,"WizNativeEditorSetup.state==='ready'").toBool();},1500),"notification failure does not change authoritative readiness");
        check(load("window.WizNativeEditorSetup={state:'pending'};"),"navigation cancellation control loads");js(page,"window.dispatchEvent(new Event('pagehide'))");check(visible(),"navigation teardown retires presentation timers");
        check(load("",false)&&visible(),"ordinary or editor HTML without gate stays visible");
        QFile extension(resources+"wizeditor/markdownEnvironments.js");check(extension.open(QFile::ReadOnly),"owned Markdown extension loads for scheduling controls");js(page,QString::fromUtf8(extension.readAll()));
        check(js(page,"WizPrepareMarkdownMathJaxScheduling();WizPrepareMarkdownMathJaxScheduling();document.querySelectorAll('#wiz_markdown_math_scheduling').length===1").toBool(),"repeated scheduling preparation uses one config node");
        check(js(page,"const config=document.getElementById('wiz_markdown_math_scheduling');config.type+=';executed=true';WizPrepareMarkdownMathJaxScheduling();config.type==='text/x-mathjax-config'&&config.getAttribute('wiz_style')==='unsave'").toBool(),"next fresh engine re-arms owned config with unsaved marker");
        check(js(page,"window.MathJax={Hub:{processSectionDelay:50,processUpdateTime:250,processUpdateDelay:10}};new Function(config.textContent)();MathJax.Hub.processSectionDelay===8&&MathJax.Hub.processUpdateTime===250&&MathJax.Hub.processUpdateDelay===10").toBool(),"stock section delay shortens without changing processing budgets");
        check(js(page,"MathJax.Hub.processSectionDelay=17;new Function(config.textContent)();MathJax.Hub.processSectionDelay===17").toBool(),"explicit non-default scheduling remains unchanged");
        check(js(page,"config.textContent='user custom configuration';config.type='text/plain';WizPrepareMarkdownMathJaxScheduling();config.textContent==='user custom configuration'&&config.type==='text/plain'").toBool(),"unowned or changed configuration is not overwritten");
        check(local.blocked.load()==0,"all presentation controls use local resources only");
        profile.setUrlRequestInterceptor(nullptr);qInfo()<<"Reader presentation controls checks"<<checks<<"failures"<<failures;return failures?1:0;
    }
    QString source="# Synthetic opening\n\nA paragraph without math.\n";
    if(kind=="math")source+="\n$$a+b=c$$\n\n$x^2\\ge0$.\n";
    if(kind=="bare"||kind=="cycle"||kind=="rapid")source+="\n\\begin{equation}\\label{synthetic:one}\na+b=c\n\\end{equation}\n\nSee \\eqref{synthetic:one}.\n";
    if(kind=="many")for(int i=0;i<96;++i)source+=QString("\n$$\\sum_{k=1}^{%1}\\frac{k^2+1}{k+1}=S_{%1}$$\n").arg(i+1);
    if(kind=="bare-many"){
        for(int i=0;i<64;++i)source+=QString("\n\\begin{equation}\\label{large:%1}\nx_{%1}+y_{%1}=z_{%1}\n\\end{equation}\n").arg(i);
        source+="\nSee \\eqref{large:0} and \\eqref{large:63}.\n";
    }
    editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+source.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
    QString html=editor.original;
    if(gated)html.insert(html.indexOf("</head>"),WizReaderOpeningPresentation::bootstrapHtml());
    QString tags;
    for(const auto& f:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"}){
        const QString resource=f=="wizeditor/markdownEnvironments.js"&&argc==5?QString::fromLocal8Bit(argv[4]):resources+f;
        tags+="<script src=\""+QUrl::fromLocalFile(resource).toString().toHtmlEscaped()+"\" name='wiz_inner_script' wiz_style='unsave'></script>";
    }
    html.insert(html.lastIndexOf("</head>"),tags);
    QWebEngineScript observer;observer.setInjectionPoint(QWebEngineScript::DocumentCreation);observer.setWorldId(QWebEngineScript::MainWorld);
    observer.setSourceCode(R"JS(
window.openingEvents=[];window.openingFrames=[];
window.mathStages=[];window.mathEngines=new WeakSet();
window.mathTicks=[];setInterval(()=>mathTicks.push(Math.round(performance.now())),10);
window.mathStage=function(name){const h=window.MathJax&&MathJax.Hub;mathStages.push({name:name,ms:Math.round(performance.now()),hub:!!h,sectionDelay:h?h.processSectionDelay:null,updateDelay:h?h.processUpdateDelay:null});};
window.observeMathHub=function(){const h=window.MathJax&&MathJax.Hub;if(!h||!h.Register||mathEngines.has(h))return;mathEngines.add(h);mathStage('hub-observed');for(const name of ['Begin PreProcess','End PreProcess','Begin Process','Begin Math Input','End Math Input','Begin Math Output','End Math Output','End Process','Begin Typeset','End Typeset'])h.Register.MessageHook(name,()=>mathStage(name));};
const mathObserver=new MutationObserver(()=>{observeMathHub();});mathObserver.observe(document,{subtree:true,childList:true});
const mathPoll=setInterval(observeMathHub,1);window.addEventListener('pagehide',()=>{clearInterval(mathPoll);mathObserver.disconnect();},{once:true});
window.openingMark=function(name){if(openingEvents.length<100)openingEvents.push({name:name,ms:Math.round(performance.now()),bodyClass:document.body?document.body.className:null,editing:document.body?document.body.isContentEditable:null,text:document.body?document.body.textContent.slice(0,120):null});};
document.addEventListener('DOMContentLoaded',function(){openingMark('dom-ready');let previous='';const sample=()=>{const b=document.body;if(b){const visible=getComputedStyle(b).visibility!=='hidden'&&getComputedStyle(document.documentElement).opacity!=='0';const s=JSON.stringify([b.className,b.textContent.slice(0,120),b.scrollHeight,document.querySelectorAll('.MathJax_SVG,.MathJax_Display').length,visible]);if(s!==previous&&openingFrames.length<100){openingFrames.push({ms:Math.round(performance.now()),visible:visible,setupState:window.WizNativeEditorSetup?WizNativeEditorSetup.state:null,signature:s});previous=s;}}if(performance.now()<15000)requestAnimationFrame(sample);};requestAnimationFrame(sample);});
)JS");page->scripts().insert(observer);
    const QString file=root.filePath("synthetic.html");QFile output(file);if(!output.open(QFile::WriteOnly))return 2;output.write(html.toUtf8());output.close();
    int completedLoads=0;bool loaded=false,ok=false;QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool v){++completedLoads;loaded=true;ok=v;});view.show();view.load(QUrl::fromLocalFile(file));
    if(!wait([&]{return loaded;})||!ok)return 3;
    js(page,R"JS(
openingMark('native-load-finished');
const originalInit=WizEditor.init;WizEditor.init=function(options){openingMark('init-begin');for(const n of ['markdown','mathJax']){const original=options.reader.callback[n];options.reader.callback[n]=function(){openingMark(n+'-callback');mathStage(n+'-callback');observeMathHub();if(original)return original.apply(this,arguments);};}const result=originalInit.apply(this,arguments);openingMark('init-return');return result;};
const originalStyle=WizApplyNoteStyle;WizApplyNoteStyle=function(){openingMark('apply-style');return originalStyle.apply(this,arguments);};
)JS");
    QString body="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';";
    body+=WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-opening-user","Synthetic",false,"markdown");
    body+="openingMark('native-init-resolved');WizPrepareNoteModeChange();openingMark('native-off-begin');await new Promise(resolve=>WizEditor.off(null,()=>{openingMark('native-off-complete');resolve();}));WizApplyNoteStyle();WizApplySpellCheck();";
    page->runJavaScript(WizDocumentPageReady::setupScript(literal("opening"),body));
    if(!wait([&]{return js(page,"WizNativeEditorSetup.state").toString()=="ready";}))return 5;
    js(page,"window.mathIdle=false;if(window.MathJax&&MathJax.Hub)MathJax.Hub.Queue(()=>window.mathIdle=true);else window.mathIdle=true;");
    if(!wait([&]{return js(page,"window.mathIdle").toBool();}))return 6;
    QEventLoop settle;QTimer::singleShot(500,&settle,&QEventLoop::quit);settle.exec();
    if(kind=="rapid"){
        int checks=0,failures=0;const auto check=[&](bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;};
        js(page,"window.realMathLoader=WizLoadMarkdownEnvironmentMathJax;window.mathReleases=[];WizLoadMarkdownEnvironmentMathJax=function(dep){return new Promise((resolve,reject)=>mathReleases.push(()=>realMathLoader(dep).then(resolve,reject)));};");
        const QList<QStringList> sequences={{"on","off"},{"on","on"},{"off","on","off"}};
        for(const auto& operations:sequences){
            js(page,"WizPrepareNoteModeChange();WizEditor.on(null,()=>{WizApplyNoteStyle();WizApplySpellCheck();});");check(wait([&]{return js(page,"document.body.isContentEditable").toBool();},4000),"rapid case begins from intact editor");
            js(page,"window.rapidCallbacks=[];window.rapidDone=false;WizPrepareNoteModeChange();WizEditor.off(null,()=>rapidCallbacks.push('old'));");check(wait([&]{return js(page,"mathReleases.length>0").toBool();},4000),"actual bare rendering reaches controlled dependency wait");
            for(int i=0;i<operations.size();++i)js(page,"WizPrepareNoteModeChange();WizEditor."+operations[i]+"(null,()=>{WizApplyNoteStyle();WizApplySpellCheck();rapidCallbacks.push('"+QString::number(i)+"');"+(i==operations.size()-1?QString("window.rapidDone=true;"):QString())+"});");
            check(wait([&]{js(page,"mathReleases.splice(0).forEach(f=>f());");return js(page,"window.rapidDone===true").toBool();},10000),"latest real mode request finishes after controlled release");
            check(js(page,"document.body.isContentEditable").toBool()==(operations.last()=="on"),"latest real mode remains selected");
            check(js(page,"rapidCallbacks.length===1&&rapidCallbacks[0]==='"+QString::number(operations.size()-1)+"'").toBool(),"real superseded callbacks do not run");
            check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"rapid mode switches preserve exact synthetic source");
            check(js(page,"getComputedStyle(document.documentElement).opacity!=='0'&&document.querySelectorAll('.MathJax_Error,.merror').length===0").toBool(),"rapid mode result is visible without math errors");
        }
        check(local.blocked.load()==0,"rapid native test uses local resources only");
        profile.setUrlRequestInterceptor(nullptr);qInfo()<<"Rapid mode checks"<<checks<<"failures"<<failures;return failures?9:0;
    }
    if(kind=="cycle"){
        QJsonArray states;int failures=0;QString expectedSource=source;bool editedSourceRoundtrip=false,serializedHideAbsent=true;
        const auto snapshot=[&](const QString& stage){
            auto object=QJsonObject::fromVariantMap(js(page,R"JS((()=>({opacity:getComputedStyle(document.documentElement).opacity,bodyOpacity:getComputedStyle(document.body).opacity,display:getComputedStyle(document.body).display,editing:document.body.isContentEditable,text:document.body.textContent.slice(0,300),styles:[...document.querySelectorAll('#wiz_tmp_style_reader_opening')].map(s=>s.textContent),bootstrapCount:document.querySelectorAll('#wiz_reader_opening_bootstrap').length,sectionDelay:window.MathJax&&MathJax.Hub?MathJax.Hub.processSectionDelay:null,configCount:document.querySelectorAll('#wiz_markdown_math_scheduling').length,head:document.head.innerHTML.slice(-2000)}))())JS").toMap());
            object.insert("stage",stage);const bool visible=object["opacity"].toString()!="0"&&object["bodyOpacity"].toString()!="0"&&object["display"].toString()!="none"&&!object["text"].toString().isEmpty();
            object.insert("visible",visible);if(!visible)++failures;states.append(object);
        };
        snapshot("initial-reader");
        for(int i=0;i<3;++i){
            const QString token=QString("cycle-%1").arg(i);
            js(page,"window.cycleDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>{WizApplyNoteStyle();WizApplySpellCheck();window.cycleDone=true;});");
            if(!wait([&]{return js(page,"window.cycleDone===true").toBool();},5000))++failures;
            snapshot(token+"-editor");if(!js(page,"document.body.isContentEditable").toBool())++failures;
            if(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()!=expectedSource)++failures;
            if(i==1){
                js(page,"const added=document.createElement('div');added.textContent='Synthetic edited marker';document.body.appendChild(added);");
                expectedSource=js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString();
                if(!expectedSource.contains("Synthetic edited marker"))++failures;
            }
            const QString serialized=js(page,"WizEditor.getContentHtml()").toString();
            if(serialized.isEmpty()||serialized.contains("opacity:0!important")||serialized.contains("wiz_markdown_math_scheduling")){serializedHideAbsent=false;++failures;}
            js(page,"window.cycleDone=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>{WizApplyNoteStyle();WizApplySpellCheck();window.cycleDone=true;});");
            if(!wait([&]{return js(page,"window.cycleDone===true").toBool();},5000))++failures;
            snapshot(token+"-reader");if(js(page,"document.body.isContentEditable").toBool())++failures;
            if(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()!=expectedSource)++failures;
            if(i==1)editedSourceRoundtrip=js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==expectedSource&&expectedSource.contains("Synthetic edited marker");
        }
        qInfo().noquote()<<"CYCLE_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"gated",gated},{"states",states},{"failures",failures},{"editedSourceRoundtrip",editedSourceRoundtrip},{"serializedHideAbsent",serializedHideAbsent},{"blockedExternalRequests",local.blocked.load()},{"completedLoads",completedLoads}}).toJson(QJsonDocument::Compact));
        profile.setUrlRequestInterceptor(nullptr);return failures||local.blocked?8:0;
    }
    const bool sourceIntact=js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source;
    qInfo().noquote()<<"MATH_TIMING"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"stages",QJsonValue::fromVariant(js(page,"mathStages"))},{"ticks",QJsonValue::fromVariant(js(page,"mathTicks"))},{"svg",QJsonValue::fromVariant(js(page,"Array.from(document.querySelectorAll('.MathJax_SVG svg')).map(s=>s.outerHTML)"))},{"mathErrors",js(page,"document.querySelectorAll('.MathJax_Error,.merror').length").toInt()},{"equationLinksValid",js(page,QString::fromLatin1(R"JS((()=>{const links=[...document.querySelectorAll('a')].map(a=>a.getAttribute('href')||a.getAttributeNS('http://www.w3.org/1999/xlink','href')).filter(h=>h&&h.startsWith('#mjx-eqn'));return links.every(h=>!!document.getElementById(decodeURIComponent(h.slice(1))))&&(%1||links.length>0);})())JS").arg(source.contains("\\eqref{")?"false":"true")).toBool()},{"jaxCount",js(page,"window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().length:0").toInt()},{"sectionDelay",js(page,"window.MathJax&&MathJax.Hub?MathJax.Hub.processSectionDelay:0").toInt()},{"updateTime",js(page,"window.MathJax&&MathJax.Hub?MathJax.Hub.processUpdateTime:0").toInt()},{"chunkDelay",js(page,"window.MathJax&&MathJax.OutputJax.SVG?MathJax.OutputJax.SVG.config.EqnChunkDelay:0").toInt()}}).toJson(QJsonDocument::Compact));
    const auto report=QJsonObject{{"kind",kind},{"gated",gated},{"completedLoads",completedLoads},{"events",QJsonValue::fromVariant(js(page,"openingEvents"))},{"frames",QJsonValue::fromVariant(js(page,"openingFrames"))},{"sourceIntact",sourceIntact},{"readOnly",js(page,"document.body.classList.contains('wiz-readonly')&&!document.body.isContentEditable").toBool()},{"blockedExternalRequests",local.blocked.load()},{"hiddenCocoa",view.testAttribute(Qt::WA_DontShowOnScreen)&&(!view.windowHandle()||!view.windowHandle()->isExposed())},{"domSnapshot",js(page,"document.body.textContent.slice(0,300)").toString()}};
    qInfo().noquote()<<"OPENING_REPORT"<<QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact));
    profile.setUrlRequestInterceptor(nullptr);return sourceIntact&&local.blocked==0?0:7;
}
#include "main.moc"
