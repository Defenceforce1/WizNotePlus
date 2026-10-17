#include "share/WizWebEngineView.h"
#include "share/WizDocumentAsync.h"
#include "share/WizDocumentPageReady.h"
#include "share/WizGlobal.h"
#include "WizNotePresentation.h"
#include "utils/WizPathResolve.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <atomic>
#include <memory>
#include <vector>

// Real QWebChannel + unmodified mac client engine; only the native account/
// persistence endpoints are synthetic. Never construct a real account window.
class SyntheticEditorBridge final : public QObject {
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
    QString guid() const { return "synthetic-runtime-user"; }
    QString alias() const { return "Synthetic local fixture"; }
    QString avatar() const { return {}; }
    QString html() const { return original; }
    bool allowed() const { return true; }
    Q_INVOKABLE bool canRenderMarkdown() const { return true; }
    Q_INVOKABLE bool shouldAddCustomCSS() const { return true; }
    Q_INVOKABLE QString getLocalLanguage() const { return "zh-cn"; }
    Q_INVOKABLE void onSelectionChange(const QString&) {}
    Q_INVOKABLE void onMarkerUndoStatusChanged(const QString&) {}
    Q_INVOKABLE void onMarkerInitiated(const QString&) {}
    Q_INVOKABLE void onNoteLoadFinished() {}
    Q_INVOKABLE void setModified(bool) {}
signals:
    void clickingTodoCallBack(bool cancel, bool needCallAgain);
};

namespace {
class LocalOnly final : public QWebEngineUrlRequestInterceptor {
public:
    std::atomic<int> blocked{0};
    void interceptRequest(QWebEngineUrlRequestInfo& request) override {
        const auto scheme=request.requestUrl().scheme();
        if(scheme!="file"&&scheme!="qrc"&&scheme!="data"&&scheme!="about"&&scheme!="blob") {
            ++blocked;request.block(true);
        }
    }
};
bool waitFor(const std::function<bool()>& predicate,int timeout=15000) {
    if(predicate())return true;
    QEventLoop loop;QTimer poll,deadline;bool ok=false;
    QObject::connect(&poll,&QTimer::timeout,[&]{if(predicate()){ok=true;loop.quit();}});
    QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
    poll.start(10);deadline.setSingleShot(true);deadline.start(timeout);loop.exec();return ok;
}
QVariant evaluate(QWebEnginePage* page,const QString& script) {
    struct State { bool ready=false;QVariant value; };
    const auto state=std::make_shared<State>();
    page->runJavaScript(script,[state](const QVariant& value){state->value=value;state->ready=true;});
    waitFor([state]{return state->ready;},10000);return state->value;
}
void settle(int ms) { QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec(); }
using WizNotePresentation::jsonString;
struct Session {
    SyntheticEditorBridge editor,explorer;
    std::unique_ptr<WizWebEngineView> view;
    WizDocumentCallbackEpoch epoch;
    QString source,path;
    bool load(const QString& folder,int index,int blocks=1,bool completeBridge=true,bool visibleOffscreen=false,bool emptyControl=false) {
        source="# Native bridge fixture\n\nReference \\ref{thm:local}.\n\n";
        for(int i=0;i<blocks;++i)source+=QString("> **定理** \\label{thm:%1}\n>\n> Synthetic $x^2\\ge0$.\n\n<!-- -->\n\n").arg(i?QString::number(i):"local");
        source+="```text\n\\ref{code:unchanged}\n```\n";
        if(emptyControl)source="Synthetic empty control";
        editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+QString(source.toHtmlEscaped()).replace("\n","<br>")+"</body></html>";
        QString html=editor.original;
        const QString resources=Utils::WizPathResolve::resourcesPath()+"files/";
        QString scripts;
        if(!emptyControl)for(const QString& file:{QString("webengine/wizwebchannel.js"),QString("wizeditor/wizEditorForMac.js"),QString("wizeditor/editorHelper.js")})
            scripts+="<script name='wiz_inner_script' wiz_style='unsave' src=\""+QUrl::fromLocalFile(resources+file).toString().toHtmlEscaped()+"\"></script>";
        html.insert(html.lastIndexOf("</body>"),scripts);
        path=folder+QString("/native-%1.html").arg(index);
        if(!WizDocumentSaveSnapshot::writeHtmlFile(path,html))return false;
        WizWebEngineInjectObjectCollection objects;
        if(completeBridge)objects.insert("WizQtEditor",&editor);
        objects.insert("WizExplorerApp",&explorer);
        view=std::make_unique<WizWebEngineView>(objects);
        if(visibleOffscreen){view->resize(900,600);view->show();}
        bool loaded=false,ok=false;
        const auto connection=QObject::connect(view.get(),&WizWebEngineView::loadFinishedEx,[&](bool success){loaded=true;ok=success;});
        view->load(QUrl::fromLocalFile(path));
        const bool ready=waitFor([&]{return loaded;});QObject::disconnect(connection);
        return ready&&ok&&evaluate(view->page(),emptyControl?"document.body.textContent==='Synthetic empty control'":"typeof WizEditorInit==='function'&&typeof qt==='object'").toBool();
    }
    QString initialize(const QString& token) const {
        const QString resources=Utils::WizPathResolve::resourcesPath()+"files/wizeditor/";
        const QString css="/* WizNotePlus scientific math environments v1 */\n.wiz-math-block {border-left:3px solid #4b69c6;}";
        return WizDocumentPageReady::setupScript(jsonString(token),
            "window.WizSpellCheckEnabled=true;window.WizNoteCss="+jsonString(css)+";"
            +WizDocumentPageReady::initializeEditorScript(resources,"zh-cn","synthetic-runtime-user","Synthetic local fixture",false,"markdown")+
            "if(WizNativeEditorSetup.token!=="+jsonString(token)+")return;WizPrepareNoteModeChange();"
            "await new Promise(resolve=>WizEditor.off(null,resolve));"
            "if(window.MathJax&&MathJax.Hub)await new Promise(resolve=>MathJax.Hub.Queue(resolve));"
            "WizApplyNoteStyle();WizApplySpellCheck();");
    }
    bool start(const QString& token) {
        struct State { bool done=false,ready=false; };
        const auto state=std::make_shared<State>();const auto ticket=epoch.ticket();
        view->page()->runJavaScript(initialize(token));
        WizDocumentPageReady::watch(view.get(),view->page(),WizDocumentPageReady::stateExpression(jsonString(token)),
            [ticket]{return WizDocumentCallbackEpoch::current(ticket);},
            [state](bool ready){state->ready=ready;state->done=true;});
        return waitFor([state]{return state->done;},35000)&&state->ready;
    }
};
QJsonObject processMemory() {
    // Sum of RSS is an observation, NOT unique physical memory or proof of a
    // leak. Report only this isolated probe's descendants, never other apps.
    QProcess ps;ps.start("/bin/ps",{"-axo","pid=,ppid=,rss="});
    const auto observerPid=ps.processId();
    if(!ps.waitForFinished(5000)||ps.exitCode()!=0)return {{"available",false}};
    struct Row { qint64 pid,parent,rss; };QList<Row> rows;
    for(const auto& line:ps.readAllStandardOutput().split('\n')) {
        const auto values=line.simplified().split(' ');
        if(values.size()==3)rows.append({values[0].toLongLong(),values[1].toLongLong(),values[2].toLongLong()});
    }
    QSet<qint64> members{QCoreApplication::applicationPid()};
    bool changed=true;while(changed){changed=false;for(const auto& row:rows)
        if(members.contains(row.parent)&&!members.contains(row.pid)){members.insert(row.pid);changed=true;}}
    qint64 rss=0;int count=0;for(const auto& row:rows)if(members.contains(row.pid)&&row.pid!=observerPid){rss+=row.rss;++count;}
    return {{"available",true},{"rssKiBSum",rss},{"processCount",count}};
}
}

int wizNativeEditorRuntimeSelfTest(int argc,char** argv) {
    QTemporaryDir root;if(!root.isValid())return 1;
    qputenv("WIZNOTE_DATA_STORE",root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME",root.filePath("cache").toUtf8());
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("WizNotePlus Synthetic Native Editor Runtime");
    QSettings settings(root.filePath("user.ini"),QSettings::IniFormat),global(root.filePath("global.ini"),QSettings::IniFormat);
    WizGlobal::setSettings(&settings);WizGlobal::setGlobalSettings(&global);
    LocalOnly requests;WizWebEngineProfile()->setUrlRequestInterceptor(&requests);
    int failures=0,count=0;
    const auto check=[&](bool ok,const char* label){++count;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;return ok;};
    if(argc==2&&QByteArray(argv[1])=="--editor-memory-probe") {
        const int batches=qEnvironmentVariableIsSet("WIZNOTE_MEMORY_BATCHES")?qEnvironmentVariableIntValue("WIZNOTE_MEMORY_BATCHES"):5;
        const bool visible=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_VISIBLE_OFFSCREEN")==1;
        const bool empty=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_EMPTY_CONTROL")==1;
        // Visible here means only the offscreen platform's virtual surface,
        // never opening/activating a window on the user's real desktop.
        if(batches<1||batches>10||(visible&&QGuiApplication::platformName()!="offscreen"))return 4;
        QJsonArray samples;
        for(int batch=0;batch<batches;++batch) {
            std::vector<std::unique_ptr<Session>> pages;
            QElapsedTimer elapsed;elapsed.start();
            for(int i=0;i<4;++i) {
                auto session=std::make_unique<Session>();
                if(!session->load(root.path(),batch*4+i,16,true,visible,empty)
                    ||(!empty&&!session->start(QString("memory-%1-%2").arg(batch).arg(i))))return 2;
                pages.push_back(std::move(session));
            }
            settle(250);auto opened=processMemory();opened.insert("batch",batch);opened.insert("phase","four-open");
            opened.insert("elapsedMs",elapsed.elapsed());samples.append(opened);
            bool content=true;QJsonArray visibility;
            for(const auto& page:pages) {
                content&=evaluate(page->view->page(),empty?"document.body.textContent":"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==page->source;
                if(!empty)content&=evaluate(page->view->page(),"MathJax.Hub.getAllJax().length").toInt()==16;
                visibility.append(evaluate(page->view->page(),"document.visibilityState").toString());
            }
            if(!check(content,empty?"all four native empty controls retain only synthetic placeholder text":"all four synthetic native pages retain source and render all sixteen formulas"))return 5;
            opened.insert("visibilityStates",visibility);opened.insert("visibleOffscreen",visible);
            opened.insert("emptyControl",empty);
            samples.replace(samples.size()-1,opened);
            qInfo().noquote()<<"MEMORY_SAMPLE"<<QString::fromUtf8(QJsonDocument(opened).toJson(QJsonDocument::Compact));
            QList<QPointer<WizWebEngineView>> views;
            for(const auto& page:pages)views.append(page->view.get());
            pages.clear();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);settle(1000);
            bool gone=true;for(const auto& view:views)gone&=view.isNull();
            if(!check(gone,"all four native views are released after closing the synthetic batch"))return 3;
            auto closed=processMemory();closed.insert("batch",batch);closed.insert("phase","all-closed");samples.append(closed);
            qInfo().noquote()<<"MEMORY_SAMPLE"<<QString::fromUtf8(QJsonDocument(closed).toJson(QJsonDocument::Compact));
        }
        qInfo().noquote()<<"MEMORY_PROBE"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"engine","native Qt mac bridge; synthetic views, not full main-window workload"},
            {"rssIsNotUniquePhysicalMemory",true},{"samples",samples}}).toJson(QJsonDocument::Compact));
    } else {
        {
            Session session;if(!check(session.load(root.path(),0),"native editor fixture loads original engine helper and actual Qt transport"))return 1;
            evaluate(session.view->page(),"window.RealQWebChannel=QWebChannel;window.initCalls=0;window.actualInit=WizEditor.init;WizEditor.init=function(){initCalls++;return actualInit.apply(this,arguments);};"
                "window.QWebChannel=function(transport,callback){return new RealQWebChannel(transport,channel=>{window.releaseOldBridge=()=>callback(channel);});};");
            session.view->page()->runJavaScript(session.initialize("obsolete-bridge"));
            check(waitFor([&]{return evaluate(session.view->page(),"typeof releaseOldBridge==='function'").toBool();}),
                "real Qt bridge handshake can be delayed before invoking the native initializer");
            evaluate(session.view->page(),"window.WizNativeEditorSetup={token:'replacement',state:'pending'};releaseOldBridge();");settle(100);
            check(evaluate(session.view->page(),"initCalls===0&&!window.WizNativeEditorInitialized&&WizNativeEditorSetup.token==='replacement'&&WizNativeEditorSetup.state==='pending'").toBool(),
                "obsolete bridge completion cannot initialize or mark the replacement page ready");
            evaluate(session.view->page(),"window.QWebChannel=RealQWebChannel;");
            check(session.start("recovered-bridge")&&evaluate(session.view->page(),"initCalls===1&&WizNativeEditorInitialized").toBool(),
                "current real bridge initializes normally after rejecting an obsolete handshake");
        }
        {
            Session session;if(!check(session.load(root.path(),1)&&session.start("current-bridge"),"production mac-client initialization awaits real QWebChannel reader and formula setup"))return 1;
            qInfo()<<"Native bridge formula status"<<evaluate(session.view->page(),
                "({guid:WizQtEditor.userGuid,initialized:WizNativeEditorInitialized,jax:window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().map(j=>j.originalText):[],rendered:document.querySelectorAll('.MathJax,.MathJax_SVG,.MathJax_Display').length})");
            check(evaluate(session.view->page(),"WizQtEditor.userGuid==='synthetic-runtime-user'&&!!WizExplorerApp&&WizNativeEditorInitialized&&window.MathJax&&MathJax.Hub.getAllJax().length===1&&document.querySelectorAll('.MathJax,.MathJax_SVG,.MathJax_Display').length>0").toBool(),
                "native bridge exposes synthetic account metadata and renders formulas without cloud access");
            check(evaluate(session.view->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==session.source,
                "actual mac-client initialization preserves exact Markdown source");
            evaluate(session.view->page(),"window.ModeReady=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>{WizApplyNoteStyle();WizApplySpellCheck();window.ModeReady=true;});");
            const bool editing=waitFor([&]{return evaluate(session.view->page(),"!!window.ModeReady&&document.body.isContentEditable").toBool();});
            const auto snapshot=WizDocumentSaveSnapshot::parse(evaluate(session.view->page(),WizDocumentSaveSnapshot::editorScript(true,false)));
            check(editing&&snapshot.state==WizDocumentSaveSnapshot::State::Changed&&snapshot.html.contains("Native bridge fixture"),
                "actual mac-client editor mode produces a valid original save snapshot");
            check(evaluate(session.view->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==session.source,
                "native bridge read-edit transition preserves labels formulas and code source");
            evaluate(session.view->page(),"var fixtureCode=document.createElement('pre');fixtureCode.id='synthetic-inserted-code';fixtureCode.textContent='synthetic code';document.body.appendChild(fixtureCode);");
            check(waitFor([&]{return evaluate(session.view->page(),"document.getElementById('synthetic-inserted-code').getAttribute('spellcheck')==='false'").toBool();},1500),
                "spellchecking still observes new code blocks after native read-edit body replacement");
        }
        {
            Session session;if(!check(session.load(root.path(),2,1,false),"missing-bridge fixture still uses an actual Qt channel"))return 1;
            QElapsedTimer elapsed;elapsed.start();const bool ready=session.start("missing-bridge");
            check(!ready&&elapsed.elapsed()<3000&&evaluate(session.view->page(),"WizNativeEditorSetup.state==='failed'&&!WizNativeEditorInitialized").toBool(),
                "missing native endpoint fails promptly without claiming successful editor initialization");
        }
        {
            Session session;if(!check(session.load(root.path(),3),"missing-engine fixture starts from an isolated native page"))return 1;
            evaluate(session.view->page(),"window.WizEditor=undefined;");
            QElapsedTimer elapsed;elapsed.start();const bool ready=session.start("missing-engine");
            check(!ready&&elapsed.elapsed()<3000&&evaluate(session.view->page(),"WizNativeEditorSetup.state==='failed'&&!WizNativeEditorInitialized").toBool(),
                "missing editor rejects explicitly instead of leaving a Promise or modal alert pending");
        }
    }
    check(requests.blocked.load()==0,"native runtime fixture performs no external page requests");
    WizWebEngineProfile()->setUrlRequestInterceptor(nullptr);
    qInfo()<<"Native editor runtime checks:"<<count<<"failures:"<<failures;return failures?1:0;
}

#include "native-editor-runtime.moc"
