// Test-only macOS attribution. Public APIs and synthetic data only. No allocator
// intervention, stack logging, debugger, forced GC, cache purge or profile cycling.
#ifdef Q_OS_MAC
#include <libproc.h>
#include <sys/resource.h>
#include <unistd.h>
#include <array>

static quint32 attributionDigest(const QString& text) {
    quint32 value=2166136261u;
    for(const QChar c:text){value^=c.unicode();value*=16777619u;}
    return value;
}
static QString attributionDigestScript(const QString& expression) {
    return "(()=>{const text="+expression+";let hash=2166136261;for(let i=0;i<text.length;++i)hash=Math.imul((hash^text.charCodeAt(i))>>>0,16777619)>>>0;return {length:text.length,hash};})()";
}
static QJsonObject attributionMemory(const QSet<qint64>& renderers) {
    // Read native counters before creating the JSON/QString observation objects.
    task_vm_info_data_t vm{};mach_msg_type_number_t size=TASK_VM_INFO_COUNT;
    const bool vmOk=task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&vm),&size)==KERN_SUCCESS;
    struct Zone {const char* name=nullptr;malloc_statistics_t stats{};};std::array<Zone,32> raw{};
    vm_address_t* addresses=nullptr;unsigned count=0;
    const bool zonesOk=malloc_get_all_zones(mach_task_self(),nullptr,&addresses,&count)==KERN_SUCCESS&&count<=raw.size();
    if(zonesOk)for(unsigned i=0;i<count;++i){auto* z=reinterpret_cast<malloc_zone_t*>(addresses[i]);raw[i].name=malloc_get_zone_name(z);malloc_zone_statistics(z,&raw[i].stats);}
    malloc_statistics_t total{};malloc_zone_statistics(nullptr,&total);
    QJsonArray zones;quint64 summedUse=0,summedAllocated=0,summedBlocks=0;
    if(zonesOk)for(unsigned i=0;i<count;++i){const auto& z=raw[i];summedUse+=z.stats.size_in_use;summedAllocated+=z.stats.size_allocated;summedBlocks+=z.stats.blocks_in_use;
        zones.append(QJsonObject{{"name",QString::fromUtf8(z.name?z.name:"unnamed")},{"inUseBytes",qint64(z.stats.size_in_use)},{"allocatedBytes",qint64(z.stats.size_allocated)},{"blocksInUse",qint64(z.stats.blocks_in_use)}});}
    QSet<qint64> seen{getpid()};QList<pid_t> queue{getpid()};QJsonArray children;bool childListComplete=true;qint64 childRss=0,childFootprint=0;
    for(qsizetype q=0;q<queue.size()&&q<64;++q){std::array<pid_t,64> pids{};const int reported=proc_listchildpids(queue[q],pids.data(),sizeof(pids));
        // proc_listchildpids returns a PID count, unlike proc_listpids' byte count.
        // Scan only this zero-filled bounded buffer; reject truncation/error.
        if(reported<0||reported>=int(pids.size())){childListComplete=false;continue;}
        int populated=0;for(const auto pid:pids)if(pid>0)++populated;
        if(populated!=reported){childListComplete=false;continue;}
        for(int i=0;i<reported;++i){const auto pid=pids[i];if(pid<=0||seen.contains(pid))continue;seen.insert(pid);queue.append(pid);
            char executable[PROC_PIDPATHINFO_MAXSIZE]{};const int pathSize=proc_pidpath(pid,executable,sizeof(executable));
            proc_taskinfo task{};const bool taskOk=proc_pidinfo(pid,PROC_PIDTASKINFO,0,&task,sizeof(task))==sizeof(task);
            rusage_info_v2 usage{};const bool usageOk=proc_pid_rusage(pid,RUSAGE_INFO_V2,reinterpret_cast<rusage_info_t*>(&usage))==0;
            childRss+=taskOk?task.pti_resident_size:0;childFootprint+=usageOk?usage.ri_phys_footprint:0;
            children.append(QJsonObject{{"pid",pid},{"parent",queue[q]},{"path",pathSize>0?QString::fromUtf8(executable):QString()},{"rendererObservedByPage",renderers.contains(pid)},{"rssBytes",taskOk?QJsonValue(qint64(task.pti_resident_size)):QJsonValue()},{"footprintBytes",usageOk?QJsonValue(qint64(usage.ri_phys_footprint)):QJsonValue()},{"birthAbsoluteTime",usageOk?QJsonValue(qint64(usage.ri_proc_start_abstime)):QJsonValue()}});
        }}
    if(queue.size()>=64)childListComplete=false;
    return {{"vmAvailable",vmOk},{"footprintBytes",qint64(vm.phys_footprint)},{"rssBytes",qint64(vm.resident_size)},{"internalBytes",qint64(vm.internal)},{"reusableBytes",qint64(vm.reusable)},{"compressedBytes",qint64(vm.compressed)},{"regionCount",vm.region_count},
        {"mallocInUseBytes",qint64(total.size_in_use)},{"mallocAllocatedBytes",qint64(total.size_allocated)},{"mallocBlocksInUse",qint64(total.blocks_in_use)},{"zoneEnumerationComplete",zonesOk},{"zoneSumInUseBytes",qint64(summedUse)},{"zoneSumAllocatedBytes",qint64(summedAllocated)},{"zoneSumBlocks",qint64(summedBlocks)},{"zones",zones},
        {"childListComplete",childListComplete},{"children",children},{"childRssSumBytes",childRss},{"childFootprintSumBytes",childFootprint},{"widgetCount",QApplication::allWidgets().size()},{"windowCount",QGuiApplication::allWindows().size()}};
}
static int runAttributionSamplerControl(){
    QProcess ownChild;ownChild.start("/bin/sleep",{"10"});if(!ownChild.waitForStarted(2000))return 12;
    const qint64 pid=ownChild.processId();const auto sample=attributionMemory({});bool childSeen=false;
    for(const auto& value:sample.value("children").toArray()){const auto c=value.toObject();if(c.value("pid").toInteger()==pid)childSeen=c.value("path").toString()=="/bin/sleep"&&c.value("rssBytes").toInteger()>0&&c.value("footprintBytes").toInteger()>0;}
    ownChild.terminate();const bool finished=ownChild.waitForFinished(2000);if(!finished){ownChild.kill();ownChild.waitForFinished(2000);}
    const bool unicodeDigest=attributionDigest(QStringLiteral("A中文𝛼\n"))==quint32(3331261389u);
    const bool ok=childSeen&&finished&&sample.value("childListComplete").toBool()&&sample.value("zoneEnumerationComplete").toBool()&&unicodeDigest;
    qInfo().noquote()<<"ATTRIBUTION_CONTROL"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"accepted",ok},{"ownChildPid",pid},{"ownChildSeen",childSeen},{"unicodeDigest",unicodeDigest},{"sample",sample}}).toJson(QJsonDocument::Compact));return ok?0:12;
}
class NativeAttribution final:public QObject {
    struct EditorContext {QWebChannel channel;Bridge editor,explorer;};
    QTemporaryDir& root;QString resources,kind,source,current;const int rounds;
    std::unique_ptr<QWebEngineProfile> profile;LocalOnly local;
    std::unique_ptr<EditorContext> context;std::unique_ptr<QWebEngineView> view;
    QPointer<QWebEnginePage> page;QSet<qint64> rendererPids;QJsonArray samples;
    int round=0,checks=0,failures=0;bool stopped=false;std::function<void(int)> finished;
    QMetaObject::Connection loadedConnection;QElapsedTimer elapsed,roundElapsed;
    const bool editorCase,formulaCase,fullResult;
    void later(int ms,std::function<void()> next){QTimer::singleShot(ms,this,[this,next=std::move(next)]{if(!stopped)next();});}
    bool check(bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;return ok;}
    void sample(const QString& phase){
        QJsonObject s{{"phase",phase},{"round",round},{"elapsedMs",elapsed.elapsed()},{"roundMs",roundElapsed.isValid()?roundElapsed.elapsed():0},{"profileLive",bool(profile)},{"profileObjects",profile?profile->findChildren<QObject*>().size():0},{"memory",attributionMemory(rendererPids)}};
        samples.append(s);qInfo().noquote()<<"ATTRIBUTION_SAMPLE"<<QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact));
    }
    void stop(const char* message){if(stopped)return;check(false,message);stopped=true;QObject::disconnect(loadedConnection);view.reset();context.reset();if(profile)profile->setUrlRequestInterceptor(nullptr);profile.reset();finished(12);}
    void read(const QString& code,std::function<void(const QVariant&)> next){if(!page){stop("live page for attribution callback");return;}page->runJavaScript(code,[this,next=std::move(next)](const QVariant& value){if(stopped)return;if(!page){stop("callback remains bound to a live synthetic page");return;}next(value);});}
    void poll(const QString& code,QDeadlineTimer deadline,std::function<void()> next){read(code,[this,code,deadline,next=std::move(next)](const QVariant& value){if(value.toBool()){later(0,next);return;}if(deadline.hasExpired()){stop("attribution stage ready before deadline");return;}later(50,[this,code,deadline,next]{poll(code,deadline,next);});});}
    void verifySource(const QString& phase,std::function<void()> next){
        const QString expression=editorCase?"WizEditor.getMarkdownSrc({unEscapeHtml:true})":"document.body.textContent";
        const QString expected=editorCase?current:QString(current).remove('\n');
        const quint32 expectedHash=attributionDigest(expected);const int expectedLength=expected.size();
        read(fullResult?expression:attributionDigestScript(expression),[this,phase,next=std::move(next),expectedHash,expectedLength](const QVariant& value){
            bool ok;if(fullResult)ok=value.toString()==current;else {const auto m=value.toMap();ok=m.value("length").toInt()==expectedLength&&quint32(m.value("hash").toULongLong())==expectedHash;}
            if(!check(ok,fullResult?"exact full source returned across IPC":"source length and UTF16 digest match across IPC")){stop("synthetic source validation");return;}
            sample(phase);later(0,next);
        });
    }
    void ready(){
        rendererPids.insert(page->renderProcessPid());
        if(!editorCase){read("document.scripts.length===0&&typeof WizEditor==='undefined'&&typeof MathJax==='undefined'",[this](const QVariant& v){if(!check(v.toBool(),"static control has no scripts, editor or math")){stop("static isolation");return;}verifySource("static-ready",[this]{close();});});return;}
        QString setup="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';";
        setup+=WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-opening-user","Synthetic",false,"markdown");
        setup+="WizPrepareNoteModeChange();await new Promise(resolve=>WizEditor.off(null,resolve));WizApplyNoteStyle();";
        page->runJavaScript(WizDocumentPageReady::setupScript(literal("attribution"),setup));
        poll("window.WizNativeEditorSetup&&WizNativeEditorSetup.state==='ready'",QDeadlineTimer(45000),[this]{
            read(formulaCase?"MathJax.Hub.getAllJax().length===130&&document.querySelectorAll('.MathJax_Error,merror').length===0&&WizMarkdownEnvironmentLoader===null":"document.querySelectorAll('.MathJax_Error,merror').length===0",[this](const QVariant& v){
                if(!check(v.toBool(),"same ready workload without formula errors")){stop("ready workload");return;}verifySource("reader-ready",[this]{if(kind.endsWith("-observe"))observeRenderer();else edit();});
            });
        });
    }
    void observeRenderer(){
        read("(()=>{const m=performance.memory;return {domNodes:document.querySelectorAll('*').length,bodyCharacters:document.body.textContent.length,scrollHeight:document.documentElement.scrollHeight,viewportWidth:innerWidth,viewportHeight:innerHeight,coarseJsHeap:m?{used:m.usedJSHeapSize,total:m.totalJSHeapSize,limit:m.jsHeapSizeLimit}:null};})()",[this](const QVariant& v){
            qInfo().noquote()<<"ATTRIBUTION_RENDERER"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"kind",kind},{"pid",page->renderProcessPid()},{"publicMetrics",QJsonObject::fromVariantMap(v.toMap())},{"privateEditorInternalsInspected",false}}).toJson(QJsonDocument::Compact));
            waitForObserver("reader-live",[this]{edit();},page->renderProcessPid());
        });
    }
    void edit(){page->runJavaScript("window.attributionEdited=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>attributionEdited=true);");poll("attributionEdited&&document.body.isContentEditable",QDeadlineTimer(35000),[this]{verifySource("editor-ready",[this]{close();});});}
    void close(){
        bool metal=true;QList<QPointer<QQuickWindow>> windows;for(auto* w:view->findChildren<QQuickWidget*>()){windows.append(w->quickWindow());metal&=int(w->quickWindow()->rendererInterface()->graphicsApi())==6;}
        if(!check(metal&&!windows.isEmpty()&&(!view->windowHandle()||!view->windowHandle()->isExposed()),"hidden Cocoa Metal workload without pixel grabs")){stop("native backend");return;}
        QPointer<QWebEnginePage> weakPage=page;QPointer<QWebEngineView> weakView=view.get();QObject::disconnect(loadedConnection);view.reset();context.reset();current.clear();
        later(300,[this,weakPage,weakView,windows]{bool gone=weakPage.isNull()&&weakView.isNull();for(auto w:windows)gone&=w.isNull();if(!check(gone&&profile->findChildren<QObject*>().isEmpty(),"page, view, Quick windows and profile-owned children gone")){stop("object teardown");return;}sample("closed");++round;
            if(round%8==0||round==rounds){later(10000,[this]{sample(QString("profile-live-batch-%1-idle-10s").arg(round/8));if(round==rounds)observeLive();else later(0,[this]{beginRound();});});}
            else later(50,[this]{beginRound();});
        });
    }
    void beginRound(){
        roundElapsed.restart();current=source+QString("\nSynthetic document instance %1.\n").arg(round);
        QString html="<!doctype html><html><head><meta charset='utf-8'></head><body>"+current.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
        if(editorCase){context=std::make_unique<EditorContext>();context->editor.original=html;QString tags;
            for(const auto& f:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})tags+="<script src=\""+QUrl::fromLocalFile(resources+f).toString().toHtmlEscaped()+"\" name='wiz_inner_script' wiz_style='unsave'></script>";
            html.insert(html.indexOf("</head>"),WizReaderOpeningPresentation::bootstrapHtml()+tags);context->channel.registerObject("WizQtEditor",&context->editor);context->channel.registerObject("WizExplorerApp",&context->explorer);
        }
        QFile file(root.filePath("attribution.html"));if(!file.open(QFile::WriteOnly)){stop("synthetic local file opens");return;}file.write(html.toUtf8());file.close();
        view=std::make_unique<QWebEngineView>();view->setAttribute(Qt::WA_DontShowOnScreen);view->resize(900,600);page=new QWebEnginePage(profile.get(),view.get());view->setPage(page);if(editorCase)page->setWebChannel(&context->channel);
        loadedConnection=QObject::connect(view.get(),&QWebEngineView::loadFinished,this,[this](bool ok){QObject::disconnect(loadedConnection);if(!check(ok,"local synthetic page loads")){stop("page load");return;}later(0,[this]{ready();});});
        view->show();view->load(QUrl::fromLocalFile(file.fileName()));
    }
    void waitForObserver(const QString& phase,std::function<void()> next,qint64 rendererPid=0){
        const QString ack=root.filePath(phase+".ack");qInfo().noquote()<<"ATTRIBUTION_OBSERVER"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"phase",phase},{"pid",getpid()},{"rendererPid",rendererPid},{"ack",ack},{"profileLive",bool(profile)}}).toJson(QJsonDocument::Compact));
        auto* poller=new QTimer(this);poller->setInterval(50);const QDeadlineTimer deadline(15000);
        QObject::connect(poller,&QTimer::timeout,this,[this,ack,deadline,poller,next=std::move(next)]{if(QFile::exists(ack)){poller->stop();poller->deleteLater();later(0,next);}else if(deadline.hasExpired()){poller->stop();poller->deleteLater();stop("exact-phase observer acknowledged before timeout");}});poller->start();
    }
    void observeLive(){
        check(profile&&!profile->isOffTheRecord()&&profile->persistentStoragePath()==root.filePath("profile")&&profile->cachePath()==root.filePath("cache"),"same temporary persistent profile alive at observation boundary");
        waitForObserver("profile-live",[this]{sample("profile-live-after-observer");profile->setUrlRequestInterceptor(nullptr);profile.reset();sample("profile-destroyed");later(10000,[this]{sample("profile-destroyed-idle-10s");waitForObserver("profile-destroyed",[this]{finish();});});});
    }
    void finish(){
        check(local.blocked.load()==0,"synthetic attribution made no external requests");
        qInfo().noquote()<<"ATTRIBUTION_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"kind",kind},{"rounds",rounds},{"paragraphs",6000},{"formulaCount",formulaCase?128:0},{"fullSourceReturned",fullResult},{"sourceBytes",source.toUtf8().size()},{"checks",checks},{"failures",failures},{"externalRequests",local.blocked.load()},{"samples",samples},{"productionCodeChanged",false},{"forcedCollectionOrCachePurge",false}}).toJson(QJsonDocument::Compact));stopped=true;finished(failures?12:0);
    }
public:
    NativeAttribution(QTemporaryDir& root,const QString& resources,const QString& kind,std::function<void(int)> done):root(root),resources(resources),kind(kind),rounds(kind.endsWith("-observe")?1:24),finished(std::move(done)),editorCase(kind!="static-digest"),formulaCase(kind=="editor-full"||kind=="editor-digest"||kind=="editor-observe"),fullResult(kind=="editor-full") {
        QWebEngineProfileBuilder builder;builder.setPersistentStoragePath(root.filePath("profile")).setCachePath(root.filePath("cache"));profile.reset(builder.createProfile(QStringLiteral("SyntheticAttribution245")));if(!profile)qFatal("synthetic profile construction");profile->setUrlRequestInterceptor(&local);
        source="# Synthetic large document\n\n";for(int i=0;i<6000;++i)source+=QString("Paragraph %1: Synthetic academic prose with **emphasis**, a local list of concepts, and punctuation. 合成正文，仅用于离线大文档测试；不来自任何真实笔记。\n\n").arg(i);
        if(formulaCase){for(int i=0;i<128;++i)source+=QString("\\begin{equation}\\label{retention:%1}\na_{%1}+b=c\n\\end{equation}\n\n").arg(i);source+="See \\eqref{retention:0} and \\eqref{retention:127}.\n";}
    }
    void start(){elapsed.start();sample("baseline");later(0,[this]{beginRound();});}
};
static int runNativeAttribution(QTemporaryDir& root,const QString& resources,const QString& kind){
    if(QGuiApplication::platformName()!="cocoa"||!QStringList{"editor-full","editor-digest","static-digest","prose-digest","editor-observe","prose-observe"}.contains(kind))return 4;int status=0;
    NativeAttribution probe(root,resources,kind,[&](int result){status=result;QCoreApplication::quit();});QTimer::singleShot(0,&probe,[&]{probe.start();});QCoreApplication::exec();return status;
}
#endif
