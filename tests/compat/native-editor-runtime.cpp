#include "share/WizWebEngineView.h"
#include "share/WizDocumentAsync.h"
#include "share/WizDocumentPageReady.h"
#include "share/WizPdfPageOperations.h"
#include "share/WizPdfExport.h"
#include "share/WizGlobal.h"
#include "WizNotePresentation.h"
#include "utils/WizPathResolve.h"
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QGridLayout>
#include <QImage>
#include <QPainter>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QProcess>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSettings>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>
#ifdef Q_OS_MAC
#include <mach/mach.h>
int wizNativeMemoryBatchPool(const std::function<int()>& action);
#endif
#ifdef WIZNOTE_MEMORY_RHI_TESTS
bool wizNativeMemoryRhiControl(QQuickWindow* window, bool finish);
#endif

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
QVariant evaluate(QWebEnginePage* page,const QString& script,quint32 world=0) {
    struct State { bool ready=false;QVariant value; };
    const auto state=std::make_shared<State>();
    page->runJavaScript(script,world,[state](const QVariant& value){state->value=value;state->ready=true;});
    waitFor([state]{return state->ready;},10000);return state->value;
}
void settle(int ms) { QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec(); }
using WizNotePresentation::jsonString;
bool createSyntheticLargeImages(const QString& folder) {
    for(int index=0;index<2;++index) {
        QImage image(2048,1536,QImage::Format_ARGB32_Premultiplied);
        image.fill(QColor(225,235,245));
        QPainter painter(&image);
        for(int y=0;y<image.height();y+=64)for(int x=0;x<image.width();x+=64)
            painter.fillRect(x,y,64,64,QColor((x/8+index*47)%256,(y/6+index*31)%256,150));
        painter.end();
        if(!image.save(QDir(folder).filePath(QString("native-large-%1.png").arg(index))))return false;
    }
    return true;
}
struct Session {
    SyntheticEditorBridge editor,explorer;
    std::unique_ptr<QWebEngineView> view;
    WizDocumentCallbackEpoch epoch;
    QString source,path;
    QString initialNoteType = QStringLiteral("markdown");
    QString markdownOverride;
    bool imagePayload=false;
    bool withPdfHelper=false;
    bool load(const QString& folder,int index,int blocks=1,bool completeBridge=true,bool showView=false,bool emptyControl=false,QWidget* host=nullptr,bool stockView=false,bool withImages=false,bool withBareMath=false) {
        imagePayload=withImages;
        source="# Native bridge fixture\n\nReference \\ref{thm:local}.\n\n";
        for(int i=0;i<blocks;++i)source+=QString("> **定理** \\label{thm:%1}\n>\n> Synthetic $x^2\\ge0$.\n\n<!-- -->\n\n").arg(i?QString::number(i):"local");
        source+="```text\n\\ref{code:unchanged}\n```\n";
        if(withBareMath)source="# Native bare environments, without dollar triggers\n\n"
            "> \\begin{equation}\n> x=1\\label{native:quote}\n> \\end{equation}\n\n"
            "- Display:\n\n    \\begin{align}\n    a&=b\\\\\n    c&=d\\label{native:list}\n    \\end{align}\n\n"
            "\\begin{gather}\na=b\\\\\nc=d\\label{native:gather}\n\\end{gather}\n\n"
            "\\begin{multline}\na+b+c\\\\\n=d+e\\label{native:multline}\n\\end{multline}\n\n"
            "```latex\n\\begin{equation}code_only\\end{equation}\n```\n";
        if(imagePayload)for(int image=0;image<2;++image)
            source+=QString("\n![Synthetic large image %1](%2)\n").arg(image)
                .arg(QUrl::fromLocalFile(QDir(folder).filePath(QString("native-large-%1.png").arg(image))).toString(QUrl::FullyEncoded));
        if(emptyControl)source="Synthetic empty control";
        if(!markdownOverride.isNull())source=markdownOverride;
        editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+QString(source.toHtmlEscaped()).replace("\n","<br>")+"</body></html>";
        QString html=editor.original;
        const QString resources=Utils::WizPathResolve::resourcesPath()+"files/";
        QString scripts;
        if(!emptyControl)for(const QString& file:{QString("webengine/wizwebchannel.js"),QString("wizeditor/wizEditorForMac.js"),QString("wizeditor/dependency/marked/marked.umd.js"),QString("wizeditor/latexTheorems.js"),QString("wizeditor/markdownEnvironments.js"),QString("wizeditor/editorHelper.js")})
            scripts+="<script name='wiz_inner_script' wiz_style='unsave' src=\""+QUrl::fromLocalFile(resources+file).toString().toHtmlEscaped()+"\"></script>";
        html.insert(html.lastIndexOf("</body>"),scripts);
        path=folder+QString("/native-%1.html").arg(index);
        if(!WizDocumentSaveSnapshot::writeHtmlFile(path,html))return false;
        WizWebEngineInjectObjectCollection objects;
        if(completeBridge)objects.insert("WizQtEditor",&editor);
        objects.insert("WizExplorerApp",&explorer);
        if(stockView) {
            view=std::make_unique<QWebEngineView>(host);
            view->setPage(new QWebEnginePage(WizWebEngineProfile(),view.get()));
        } else view=std::make_unique<WizWebEngineView>(objects,host);
        if(auto* tabs=qobject_cast<QTabWidget*>(host)) {
            tabs->addTab(view.get(),QString("Synthetic %1").arg(index));tabs->setCurrentWidget(view.get());
        } else if(host)static_cast<QGridLayout*>(host->layout())->addWidget(view.get(),index%4/2,index%2);
        if(withPdfHelper)WizPdfSvgLinks::install(view->page());
        if(showView){view->resize(900,600);view->show();}
        bool loaded=false,ok=false;
        QMetaObject::Connection connection;
        const auto finished=[&](bool success){loaded=true;ok=success;};
        if(stockView)connection=QObject::connect(view.get(),&QWebEngineView::loadFinished,finished);
        else connection=QObject::connect(static_cast<WizWebEngineView*>(view.get()),&WizWebEngineView::loadFinishedEx,finished);
        view->load(QUrl::fromLocalFile(path));
        const bool ready=waitFor([&]{return loaded;});QObject::disconnect(connection);
        return ready&&ok&&evaluate(view->page(),emptyControl?"document.body.textContent==='Synthetic empty control'":"typeof WizEditorInit==='function'&&typeof qt==='object'").toBool();
    }
    QString initialize(const QString& token) const {
        const QString resources=Utils::WizPathResolve::resourcesPath()+"files/wizeditor/";
        const QString css="/* WizNotePlus scientific math environments v1 */\n.wiz-math-block {border-left:3px solid #4b69c6;}";
        return WizDocumentPageReady::setupScript(jsonString(token),
            "window.WizSpellCheckEnabled=true;window.WizNoteCss="+jsonString(css)+";"
            +WizDocumentPageReady::initializeEditorScript(resources,"zh-cn","synthetic-runtime-user","Synthetic local fixture",false,initialNoteType)+
            "if(WizNativeEditorSetup.token!=="+jsonString(token)+")return;WizPrepareNoteModeChange();"
            "await new Promise(resolve=>WizEditor.off({reader:{type:'markdown'}},resolve));"
            "if(window.MathJax&&MathJax.Hub)await new Promise(resolve=>MathJax.Hub.Queue(resolve));"
            "WizApplyNoteStyle();WizApplySpellCheck();"+
            (imagePayload?QString("const syntheticImages=Array.from(document.images).filter(i=>/\\/native-large-[01]\\.png$/.test(i.src));"
                "if(syntheticImages.length!==2)throw new Error('Expected two synthetic large images: '+JSON.stringify(Array.from(document.images).map(i=>i.outerHTML)));"
                "await Promise.all(syntheticImages.map(i=>i.decode()));"):QString()));
    }
    bool imagesIntact() const {
        return !imagePayload||evaluate(view->page(),
            "(()=>{const images=Array.from(document.images).filter(i=>/\\/native-large-[01]\\.png$/.test(i.src));"
            "return images.length===2&&images.every(i=>i.complete&&i.naturalWidth===2048&&i.naturalHeight===1536);})()").toBool();
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
    qint64 rss=0,mainRss=0;int count=0;for(const auto& row:rows)if(members.contains(row.pid)&&row.pid!=observerPid){
        rss+=row.rss;++count;if(row.pid==QCoreApplication::applicationPid())mainRss=row.rss;
    }
    QJsonObject result{{"available",true},{"rssKiBSum",rss},{"mainRssKiB",mainRss},{"processCount",count}};
#ifdef Q_OS_MAC
    // Own-task accounting needs no access to another application. Footprint
    // includes compressed/private memory and must not be equated with RSS.
    task_vm_info_data_t info{};mach_msg_type_number_t size=TASK_VM_INFO_COUNT;
    if(task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&info),&size)==KERN_SUCCESS) {
        result.insert("mainFootprintKiB",qint64(info.phys_footprint/1024));
        result.insert("mainInternalKiB",qint64(info.internal/1024));
        result.insert("mainCompressedKiB",qint64(info.compressed/1024));
        result.insert("mainExternalKiB",qint64(info.external/1024));
    }
#endif
    result.insert("profileDescendantObjects",WizWebEngineProfile()->findChildren<QObject*>().size());
    result.insert("widgetCount",QApplication::allWidgets().size());
    result.insert("windowCount",QGuiApplication::allWindows().size());
    return result;
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
        const bool nativeVisible=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_VISIBLE_COCOA")==1;
        const bool empty=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_EMPTY_CONTROL")==1;
        const bool nestedControl=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_NESTED_LOOP_CONTROL")==1;
        const bool sharedHost=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_SHARED_HOST")==1;
        const bool keepNote=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_KEEP_NOTE")==1;
        const bool releaseGraphics=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_RELEASE_GRAPHICS")==1;
        const bool batchPool=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_BATCH_POOL")==1;
        const QString rhiControl=qEnvironmentVariable("WIZNOTE_MEMORY_RHI_CONTROL");
        const bool stockView=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_STOCK_VIEW")==1;
        const bool imagePayload=qEnvironmentVariableIntValue("WIZNOTE_MEMORY_IMAGE_PAYLOAD")==1;
        // Offscreen visibility cannot accidentally open desktop windows.
        // Actual Cocoa windows need a separate, explicit test-only opt-in.
        if(batches<1||batches>30||(visible&&QGuiApplication::platformName()!="offscreen")
            ||(nativeVisible&&QGuiApplication::platformName()!="cocoa")||(visible&&nativeVisible)
            ||(sharedHost&&!nativeVisible)||(keepNote&&(!sharedHost||nestedControl))
            ||(releaseGraphics&&!keepNote)||(batchPool&&(!nativeVisible||nestedControl))
            ||(!rhiControl.isEmpty()&&(!keepNote||(rhiControl!="finish"&&rhiControl!="cache")))
            ||(stockView&&(!empty||!keepNote))||(imagePayload&&(empty||stockView)))return 4;
        if(imagePayload&&!createSyntheticLargeImages(root.path()))return 10;
#ifndef WIZNOTE_MEMORY_RHI_TESTS
        if(!rhiControl.isEmpty())return 4;
#endif
        std::unique_ptr<QWidget> host;
        QTabWidget* tabs=nullptr;
        if(sharedHost) {
            host=std::make_unique<QWidget>();host->setWindowTitle("WizNotePlus — synthetic memory test (no account)");
            auto* layout=new QGridLayout(host.get());layout->setContentsMargins(0,0,0,0);
            if(keepNote){tabs=new QTabWidget(host.get());layout->addWidget(tabs,0,0);}
            host->resize(900,600);host->show();
        }
        std::unique_ptr<Session> retained;
        if(keepNote) {
            retained=std::make_unique<Session>();
            if(!retained->load(root.path(),10000,16,true,true,empty,tabs,stockView,imagePayload)
                ||(!empty&&!retained->start("memory-retained")))return 2;
        }
        QJsonArray samples;
        const auto runBatch=[&](int batch)->int {
            std::vector<std::unique_ptr<Session>> pages;
            QElapsedTimer elapsed;elapsed.start();
            for(int i=0;i<4;++i) {
                auto session=std::make_unique<Session>();
                if(!session->load(root.path(),batch*4+i,16,true,visible||nativeVisible,empty,tabs?static_cast<QWidget*>(tabs):host.get(),stockView,imagePayload)
                    ||(!empty&&!session->start(QString("memory-%1-%2").arg(batch).arg(i))))return 2;
                pages.push_back(std::move(session));
            }
            settle(250);auto opened=processMemory();opened.insert("batch",batch);opened.insert("phase","four-open");
            opened.insert("elapsedMs",elapsed.elapsed());samples.append(opened);
            bool content=true;QJsonArray visibility;
            for(const auto& page:pages) {
                content&=evaluate(page->view->page(),empty?"document.body.textContent":"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==page->source;
                if(!empty)content&=evaluate(page->view->page(),"MathJax.Hub.getAllJax().length").toInt()==16;
                content&=page->imagesIntact();
                if(stockView)content&=QByteArray(page->view->metaObject()->className())=="QWebEngineView"
                    &&QByteArray(page->view->page()->metaObject()->className())=="QWebEnginePage"
                    &&page->view->page()->webChannel()==nullptr
                    &&evaluate(page->view->page(),"document.scripts.length===0&&typeof WizEditor==='undefined'&&typeof MathJax==='undefined'").toBool();
                visibility.append(evaluate(page->view->page(),"document.visibilityState").toString());
            }
            if(!check(content,imagePayload?"all four native pages preserve source, sixteen formulas and two decoded large images":
                stockView?"all four stock Qt controls preserve plain text without app page subclasses, channel or editor scripts":
                empty?"all four native empty controls retain only synthetic placeholder text":"all four synthetic native pages retain source and render all sixteen formulas"))return 5;
            opened.insert("visibilityStates",visibility);opened.insert("visibleOffscreen",visible);
            opened.insert("visibleCocoa",nativeVisible);
            opened.insert("emptyControl",empty);
            opened.insert("syntheticLargeImagesPerPage",imagePayload?2:0);
            samples.replace(samples.size()-1,opened);
            qInfo().noquote()<<"MEMORY_SAMPLE"<<QString::fromUtf8(QJsonDocument(opened).toJson(QJsonDocument::Compact));
            QList<QPointer<QWebEngineView>> views;
            QList<QPointer<QWebEnginePage>> webPages;
            QList<QPointer<QQuickWindow>> quickWindows;
            for(const auto& page:pages){
                views.append(page->view.get());webPages.append(page->view->page());
                for(auto* widget:page->view->findChildren<QQuickWidget*>())quickWindows.append(widget->quickWindow());
            }
            pages.clear();
            if(retained)tabs->setCurrentWidget(retained->view.get());
            QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);settle(1000);
            bool gone=true;for(const auto& view:views)gone&=view.isNull();
            for(const auto& page:webPages)gone&=page.isNull();
            for(const auto& window:quickWindows)gone&=window.isNull();
            if(!check(gone&&quickWindows.size()==4,"all four native views, pages and Quick windows are released after closing the synthetic batch"))return 3;
            if(retained) {
#ifdef WIZNOTE_MEMORY_RHI_TESTS
                if(!rhiControl.isEmpty()) {
                    int completed=0;
                    for(auto* widget:retained->view->findChildren<QQuickWidget*>())
                        completed+=wizNativeMemoryRhiControl(widget->quickWindow(),rhiControl=="finish");
                    if(!check(completed==1,"optional Metal diagnostic completes on exactly one retained native graphics context"))return 9;
                    settle(250);
                }
#endif
                if(releaseGraphics) {
                    int released=0;
                    for(auto* widget:retained->view->findChildren<QQuickWidget*>()) {
                        if(auto* window=widget->quickWindow()) {
                            // Diagnostic only: public scene-graph resource
                            // release request, never browser cache/DOM/data.
                            window->releaseResources();widget->update();++released;
                        }
                    }
                    if(!check(released==1,"diagnostic releases cached graphics through the retained page's single native Quick widget"))return 8;
                    settle(250);
                }
                const bool intact=tabs->count()==1&&tabs->currentWidget()==retained->view.get()
                    &&evaluate(retained->view->page(),empty?"document.body.textContent":"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==retained->source
                    &&(empty||evaluate(retained->view->page(),"MathJax.Hub.getAllJax().length").toInt()==16)
                    &&retained->imagesIntact()
                    &&evaluate(retained->view->page(),"document.visibilityState==='visible'").toBool();
                if(!check(intact,imagePayload?"retained tab preserves source, formulas and decoded large images after other pages close":
                    "retained current tab remains visible with exact source and formulas after other pages close"))return 6;
            }
            auto closed=processMemory();closed.insert("batch",batch);closed.insert("phase","all-closed");samples.append(closed);
            qInfo().noquote()<<"MEMORY_SAMPLE"<<QString::fromUtf8(QJsonDocument(closed).toJson(QJsonDocument::Compact));
            return 0;
        };
        if(nestedControl) {
            for(int batch=0;batch<batches;++batch)if(const int status=runBatch(batch))return status;
        } else {
            // Return to the real application event loop between batches, like
            // separate user actions. A single long synchronous test callback
            // can retain native autoreleased window/graphics resources and
            // misrepresent the application's steady-state memory behaviour.
            int batch=0;
            std::function<void()> next;
            next=[&] {
                if(batch==batches){app.exit(0);return;}
                const auto action=[&]{return runBatch(batch++);};
                int status=0;
#ifdef Q_OS_MAC
                status=batchPool?wizNativeMemoryBatchPool(action):action();
#else
                status=action();
#endif
                if(status){app.exit(status);return;}
                if(batchPool) {
                    settle(100);auto drained=processMemory();drained.insert("phase","after-native-batch-pool");
                    drained.insert("batch",batch-1);samples.append(drained);
                    qInfo().noquote()<<"MEMORY_SAMPLE"<<QString::fromUtf8(QJsonDocument(drained).toJson(QJsonDocument::Compact));
                }
                QTimer::singleShot(100,&app,next);
            };
            QTimer::singleShot(0,&app,next);
            if(const int status=app.exec())return status;
        }
        settle(10000);auto settled=processMemory();settled.insert("phase","all-closed-after-ten-seconds");samples.append(settled);
        qInfo().noquote()<<"MEMORY_SAMPLE"<<QString::fromUtf8(QJsonDocument(settled).toJson(QJsonDocument::Compact));
        if(retained) {
            const QPointer<QWebEngineView> view=retained->view.get();
            const QPointer<QWebEnginePage> page=retained->view->page();
            retained.reset();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);settle(1000);
            if(!check(view.isNull()&&page.isNull()&&tabs->count()==0,"retained tab is released only at the explicit end of the isolated test"))return 7;
            auto final=processMemory();final.insert("phase","retained-tab-finally-closed");samples.append(final);
            qInfo().noquote()<<"MEMORY_SAMPLE"<<QString::fromUtf8(QJsonDocument(final).toJson(QJsonDocument::Compact));
        }
        qInfo().noquote()<<"MEMORY_PROBE"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},
            {"engine",stockView?"stock Qt views/pages without editor or app bridge; shared isolated profile":"native Qt mac bridge; synthetic views, not full main-window workload"},
            {"rssIsNotUniquePhysicalMemory",true},{"nestedLoopControl",nestedControl},{"sharedHost",sharedHost},
            {"syntheticLargeImagesPerPage",imagePayload?2:0},{"largeImageWidth",imagePayload?2048:0},
            {"largeImageHeight",imagePayload?1536:0},{"imagesDecodedBeforeSample",imagePayload},
            {"retainedNote",keepNote},{"releaseGraphics",releaseGraphics},
            {"nativeBatchPool",batchPool},
            {"rhiControl",rhiControl},
            {"stockView",stockView},
            {"expectedClosedProcessCount",keepNote?2:1},{"samples",samples}}).toJson(QJsonDocument::Compact));
    } else {
        const bool printOnly=qEnvironmentVariableIsSet("WIZNOTE_NATIVE_PRINT_ONLY");
        for(const auto& initialType : {QStringLiteral("markdown"),QStringLiteral("common")}) {
            if(printOnly)break;
            Session session;
            session.initialNoteType=initialType;
            if(!check(session.load(root.path(),21,1,true,false,false,nullptr,false,false,true)
                &&session.start("native-bare-environments"),"actual mac bridge initializes bare math environments in Markdown containers"))return 1;
            const QString rendered="!!window.MathJax&&!!MathJax.Hub&&MathJax.Hub.getAllJax().length===4"
                "&&document.querySelectorAll('.MathJax_Error,merror,.noError').length===0"
                "&&!document.querySelector('pre .MathJax_SVG')"
                "&&['native:quote','native:list','native:gather','native:multline'].every(id=>!!document.getElementById('mjx-eqn-'+id))";
            qInfo()<<"Native bare math initial note type"<<initialType;
            check(evaluate(session.view->page(),rendered).toBool(),"actual mac-client reader renders quote list gather and multline with number targets after initial note type");
            evaluate(session.view->page(),"window.ModeReady=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>{window.ModeReady=true;});");
            check(waitFor([&]{return evaluate(session.view->page(),"!!ModeReady&&document.body.isContentEditable").toBool();})
                &&evaluate(session.view->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==session.source,
                "actual mac-client bare math edit transition preserves exact source");
            evaluate(session.view->page(),"window.ModeReady=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>{window.ModeReady=true;});");
            check(waitFor([&]{return evaluate(session.view->page(),"!!ModeReady").toBool();})
                &&evaluate(session.view->page(),rendered).toBool()
                &&evaluate(session.view->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==session.source,
                "native reader-type override retains bare environment hook numbering and exact source");
        }
        if(!printOnly) {
            Session session;session.initialNoteType=QStringLiteral("common");
            session.markdownOverride=QStringLiteral("\\begin{equation}\\label{eq1}\n"
                "A+C-D=\\begin{cases}A\\\\B,\n\\end{cases}\n\\end{equation}\n\n\\eqref{eq1}\n");
            if(!check(session.load(root.path(),23)&&session.start("screenshot-equation-cases"),
                "screenshot-shaped source uses actual common-to-Markdown mac bridge"))return 1;
            check(evaluate(session.view->page(),"!!window.MathJax&&MathJax.Hub.getAllJax().length===2"
                "&&!!document.getElementById('mjx-eqn-eq1')&&document.querySelectorAll('.MathJax_Error,merror,.noError').length===0"
                "&&Array.from(document.querySelectorAll('.MathJax_SVG a')).some(a=>(a.getAttribute('xlink:href')||a.getAttribute('href')||'').endsWith('#mjx-eqn-eq1'))").toBool(),
                "native screenshot equation nested cases and bare eqref render with resolved local destination");
            check(evaluate(session.view->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==session.source,
                "native screenshot TeX including exact cases row separator is unchanged");
        }
        {
            Session session;session.initialNoteType=QStringLiteral("common");session.withPdfHelper=true;
            session.markdownOverride=QStringLiteral("\\begin{equation}\\label{eq1}\na=b\n\\end{equation}\n\n"
                "\\begin{equation}\\label{eq2}\nc=d\n\\end{equation}\n\n"
                "\\begin{multline}\na+b+c\\\\\n=d+e\\label{eq:multline}\n\\end{multline}\n\n"
                "\\eqref{eq2}\n\n\\eqref{eq1}\n\n\\eqref{eq:multline}\n");
            const bool visiblePrint=!qEnvironmentVariableIsSet("WIZNOTE_NATIVE_PRINT_HIDDEN_CONTROL");
            if(!check(session.load(root.path(),24,1,true,visiblePrint)&&session.start("native-print-transitions"),
                "native mac reader initializes equations after installing the isolated print helper"))return 1;
            qInfo()<<"Synthetic native print viewport"<<evaluate(session.view->page(),
                "({width:innerWidth,displays:Array.from(document.querySelectorAll('.MathJax_SVG_Display')).map(n=>n.getBoundingClientRect().width)})");
            const auto getterControlBefore=evaluate(session.view->page(),"WizEditor.getContentHtml()");
            evaluate(session.view->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})");
            const auto getterControlAfter=evaluate(session.view->page(),"WizEditor.getContentHtml()");
            if(getterControlBefore!=getterControlAfter) {
                const auto a=getterControlBefore.toString(),b=getterControlAfter.toString();qsizetype offset=0;
                while(offset<qMin(a.size(),b.size())&&a[offset]==b[offset])++offset;
                qInfo()<<"Synthetic getter-only control without printing"<<offset
                    <<a.mid(qMax(qsizetype(0),offset-50),160)<<b.mid(qMax(qsizetype(0),offset-50),160);
            }
            WizPdfPageOperations operations;
            for(int cycle=0;cycle<3;++cycle) {
                evaluate(session.view->page(),"window.ModeReady=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>{window.ModeReady=true;});");
                const bool edit=waitFor([&]{return evaluate(session.view->page(),"!!ModeReady&&document.body.isContentEditable").toBool();});
                evaluate(session.view->page(),"window.ModeReady=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>{window.ModeReady=true;});");
                const bool read=waitFor([&]{return evaluate(session.view->page(),"!!ModeReady&&!document.body.isContentEditable").toBool();});
                const auto before=evaluate(session.view->page(),"WizEditor.getContentHtml()");
                const auto modified=evaluate(session.view->page(),"WizEditor.isModified()");
                const auto domBefore=evaluate(session.view->page(),"document.documentElement.outerHTML");
                auto pdf=std::make_shared<QByteArray>();auto done=std::make_shared<bool>(false);
                operations.print(session.view->page(),[]{return true;},WizPdfExport::pageLayout(0,QMarginsF(5,5,5,5)),
                    [pdf,done](const QByteArray& bytes){*pdf=bytes;*done=true;});
                const bool printed=waitFor([done]{return *done;},30000);
                const auto state=evaluate(session.view->page(),"globalThis.__wizPdfSvgLinks?.status()",QWebEngineScript::ApplicationWorld).toMap();
                qInfo()<<"Native multline print lifecycle"<<cycle<<state;
                if(state.value("lastFitted").toInt()!=1)qInfo().noquote()<<"Native multline shape"<<evaluate(session.view->page(),R"JS(JSON.stringify(Array.from(document.querySelectorAll('.MathJax_SVG_Display .MathJax_SVG')).map(frame=>{
                  const input=document.getElementById(frame.id.replace(/-Frame$/,''));
                  const shape=(n,depth)=>n&&({tag:n.tagName,id:n.id,transform:n.getAttribute('transform'),children:depth?Array.from(n.children).map(c=>shape(c,depth-1)):n.children.length});
                  return {id:frame.id,box:frame.closest('.MathJax_SVG_Display').getBoundingClientRect().toJSON(),input:input&&{tag:input.tagName,type:input.type,text:input.textContent},tree:shape(frame.querySelector('svg'),4)};
                })))JS").toString();
                check(edit&&read&&printed&&pdf->startsWith("%PDF-")&&state.value("prepares").toInt()==cycle+1
                    &&state.value("prepares")==state.value("restores")&&state.value("lastAdded").toInt()==3
                    &&state.value("lastFitted").toInt()==1
                    &&!state.value("errors").toInt()&&!state.value("pending").toInt(),
                    "actual mac bridge edit/read/print reattaches trusted equation preparation exactly once");
                const auto domAfter=evaluate(session.view->page(),"document.documentElement.outerHTML");
                const auto after=evaluate(session.view->page(),"WizEditor.getContentHtml()");
                const auto modifiedAfter=evaluate(session.view->page(),"WizEditor.isModified()");
                const bool sourceSame=evaluate(session.view->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==session.source;
                if(!sourceSame||after!=before||modifiedAfter!=modified) {
                    const QString oldHtml=before.toString(),newHtml=after.toString();qsizetype difference=0;
                    while(difference<qMin(oldHtml.size(),newHtml.size())&&oldHtml[difference]==newHtml[difference])++difference;
                    qInfo()<<"Synthetic native print preservation"<<cycle<<"source"<<sourceSame
                        <<"html"<<(after==before)<<"dirty before/after"<<modified<<modifiedAfter
                        <<"sizes"<<oldHtml.size()<<newHtml.size()<<"first diff"<<difference
                        <<oldHtml.mid(qMax(qsizetype(0),difference-80),200)<<newHtml.mid(qMax(qsizetype(0),difference-80),200);
                }
                check(sourceSame&&after==before&&domAfter==domBefore&&modifiedAfter==modified,
                    "actual mac bridge printing after reader rewrites preserves exact source HTML and dirty state");
            }
        }
        if(printOnly||qEnvironmentVariableIsSet("WIZNOTE_NATIVE_BARE_ONLY"))return failures?1:0;
        {
            Session session;session.initialNoteType=QStringLiteral("common");
            session.markdownOverride=QStringLiteral("# Native theorem syntax\n\nSee \\ref{thm:native}.\n\n"
                "\\begin{theorem}[Title]\\label{thm:native}\nFor $x$, the statement holds.\n\n"
                "\\begin{proof}\nBy definition.\n\\end{proof}\n\\end{theorem}\n\n"
                "\\begin{proposition}\n\\begin{equation}\nx=1\\label{eq:theorem}\n\\end{equation}\n\\end{proposition}\n\n"
                "\\begin{corollary}\nA consequence.\n\\end{corollary}\n");
            if(!check(session.load(root.path(),22)&&session.start("native-theorem-environments"),
                "actual mac bridge initializes common note then displays explicit LaTeX theorem syntax"))return 1;
            const QString rendered="document.querySelectorAll('.wiz-latex-theorem').length===4"
                "&&Array.from(document.querySelectorAll('.wiz-latex-theorem')).map(b=>b.getAttribute('data-wiz-math-number')).join(',')==='1,,2,3'"
                "&&document.querySelectorAll('.wiz-math-reference').length===1&&!!document.getElementById('wiz-math-thm:native')"
                "&&!!window.MathJax&&MathJax.Hub.getAllJax().length===2&&!!document.getElementById('mjx-eqn-eq:theorem')"
                "&&document.querySelectorAll('.MathJax_Error,merror,.noError').length===0";
            check(evaluate(session.view->page(),rendered).toBool(),"native theorem proposition corollary nested proof numbers and formula destinations resolve");
            evaluate(session.view->page(),"window.ModeReady=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>{window.ModeReady=true;});");
            check(waitFor([&]{return evaluate(session.view->page(),"!!ModeReady&&document.body.isContentEditable").toBool();})
                &&evaluate(session.view->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==session.source
                &&!evaluate(session.view->page(),"WizEditor.getContentHtml()").toString().contains("wiz-latex-theorem"),
                "native theorem edit and save HTML retain exact source without generated preview blocks");
            evaluate(session.view->page(),"window.ModeReady=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>{window.ModeReady=true;});");
            check(waitFor([&]{return evaluate(session.view->page(),"!!ModeReady").toBool();})
                &&evaluate(session.view->page(),rendered).toBool()
                &&evaluate(session.view->page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==session.source,
                "native theorem repeated read transition preserves formula and block numbering with exact source");
        }
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
