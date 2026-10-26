// Public-interface-only phase attribution. Does not read private editor code.
#ifdef Q_OS_MAC
class InitializationAttribution final:public QObject {
    QTemporaryDir& root;QString resources,kind,source;
    std::unique_ptr<QWebEngineProfile> profile;LocalOnly local;
    std::unique_ptr<QWebEngineView> view;QPointer<QWebEnginePage> page;
    QWebChannel channel;Bridge editor,explorer;QSet<qint64> renderers;
    QJsonArray phases;QJsonObject getterObservation;QTimer monitor;QElapsedTimer elapsed;
    quint64 rendererPeak=0,mainPeak=0;int checks=0,failures=0;
    bool stopped=false;std::function<void(int)> finished;
    void later(int ms,std::function<void()> next){QTimer::singleShot(ms,this,[this,next=std::move(next)]{if(!stopped)next();});}
    bool check(bool value,const char* text){++checks;failures+=!value;qInfo().noquote()<<(value?"PASS":"FAIL")<<text;return value;}
    void stop(const char* text){if(stopped)return;check(false,text);stopped=true;monitor.stop();view.reset();profile->setUrlRequestInterceptor(nullptr);profile.reset();finished(13);}
    void read(const QString& code,std::function<void(const QVariant&)> next){if(!page){stop("phase read has live synthetic page");return;}page->runJavaScript(code,[this,next=std::move(next)](const QVariant& value){if(!stopped&&page)next(value);});}
    void poll(const QString& code,QDeadlineTimer deadline,std::function<void()> next){read(code,[this,code,deadline,next=std::move(next)](const QVariant& v){if(v.toBool()){later(0,next);return;}if(deadline.hasExpired()){stop("public initialization phase finishes before deadline");return;}later(25,[this,code,deadline,next]{poll(code,deadline,next);});});}
    void nativePeak(){
        if(page&&page->renderProcessPid()>0){rusage_info_v2 r{};if(proc_pid_rusage(page->renderProcessPid(),RUSAGE_INFO_V2,reinterpret_cast<rusage_info_t*>(&r))==0)rendererPeak=std::max(rendererPeak,r.ri_phys_footprint);}
        task_vm_info_data_t vm{};mach_msg_type_number_t count=TASK_VM_INFO_COUNT;if(task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&vm),&count)==KERN_SUCCESS)mainPeak=std::max(mainPeak,quint64(vm.phys_footprint));
    }
    void checkpoint(const QString& phase,std::function<void()> next){
        nativePeak();renderers.insert(page->renderProcessPid());
        read("WizPhase246.snapshot()",[this,phase,next=std::move(next)](const QVariant& value){
            const auto metrics=value.toMap();if(!check(metrics.contains("domElements")&&metrics.value("domElements").toInt()>0&&metrics.contains("sourceGetterCalls"),"phase metrics are present rather than an empty callback value")){stop("missing phase snapshot");return;}
            QJsonObject s{{"phase",phase},{"ms",elapsed.elapsed()},{"public",QJsonObject::fromVariantMap(metrics)},{"native",attributionMemory(renderers)},{"sampledRendererPeakBytes",qint64(rendererPeak)},{"sampledMainPeakBytes",qint64(mainPeak)}};
            phases.append(s);qInfo().noquote()<<"INITIALIZATION_SAMPLE"<<QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact));later(0,next);
        });
    }
    void initialize(){
        if(kind=="loaded-only"){checkpoint("loaded-no-initialization",[this]{close();});return;}
        QString init="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';";
        init+=WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-opening-user","Synthetic",false,"markdown");
        init+="window.WizPhase246InitDone=true;";
        page->runJavaScript(WizDocumentPageReady::setupScript(literal("phase246"),init));
        poll("window.WizPhase246InitDone===true",QDeadlineTimer(45000),[this]{checkpoint("public-init-resolved",[this]{reader();});});
    }
    void reader(){
        page->runJavaScript("window.WizPhase246ReaderDone=false;WizPrepareNoteModeChange();WizEditor.off(null,()=>WizPhase246ReaderDone=true);");
        poll("WizPhase246ReaderDone===true",QDeadlineTimer(45000),[this]{
            checkpoint("reader-before-explicit-style-and-source",[this]{
                page->runJavaScript("WizApplyNoteStyle();window.WizPhase246StyleDone=true;");
                poll("window.WizPhase246StyleDone===true",QDeadlineTimer(5000),[this]{checkpoint("reader-after-explicit-style-before-source",[this]{readSource();});});
            });
        });
    }
    void readSource(){
        if(kind.startsWith("getter-")){probePublicGetter();return;}
        read("WizEditor.getMarkdownSrc({unEscapeHtml:true})",[this](const QVariant& value){
            if(!check(value.toString()==source,"exact synthetic source after reader initialization")){stop("reader source mismatch");return;}
            checkpoint("reader-after-source-getter",[this]{
                const int refs=kind=="math-theorem-ref"?6:kind=="math-ref"?3:2;
                const QString valid=QString("MathJax.Hub.getAllJax().length===%1&&document.querySelectorAll('.MathJax_Error,merror').length===0&&(()=>{const a=[...document.querySelectorAll('a')].map(a=>a.getAttribute('href')||a.getAttributeNS('http://www.w3.org/1999/xlink','href')).filter(h=>h&&h.startsWith('#mjx-eqn'));return a.length===%2&&a.every(h=>!!document.getElementById(decodeURIComponent(h.slice(1))));})()").arg(128+refs).arg(refs);
                read(kind.startsWith("math")?valid:"document.querySelectorAll('.MathJax_Error,merror').length===0",[this](const QVariant& value){
                    if(!check(value.toBool(),"reader workload matches expected formula/reference count")){stop("reader workload changed");return;}later(0,[this]{edit();});
                });
            });
        });
    }
    void probePublicGetter(){
        const QString call=kind=="getter-raw"?"WizEditor.getMarkdownSrc({unEscapeHtml:true})":kind=="getter-encoded"?"WizEditor.getMarkdownSrc({unEscapeHtml:false})":"WizEditor.getMarkdownSrc()";
        read("(()=>{const nodes=document.querySelectorAll('*').length,cls=document.body.className,modified=WizEditor.isModified(),start=performance.now();const text="+call+";const ms=performance.now()-start;let hash=2166136261;for(let i=0;i<text.length;i++)hash=Math.imul((hash^text.charCodeAt(i))>>>0,16777619)>>>0;window.__WizSyntheticSource249=text;return {hash,length:text.length,getterMs:ms,nodeCountUnchanged:document.querySelectorAll('*').length===nodes,bodyClassUnchanged:document.body.className===cls,modifiedUnchanged:WizEditor.isModified()===modified};})()",[this](const QVariant& value){
            getterObservation=QJsonObject::fromVariantMap(value.toMap());
            if(!check(getterObservation.contains("hash")&&getterObservation.value("length").toInt()>0,"public getter returns nonempty measured metadata")){stop("missing getter observation");return;}
            const bool rawMatches=getterObservation.value("length").toInt()==source.size()&&quint32(getterObservation.value("hash").toDouble())==attributionDigest(source);
            getterObservation.insert("variant",kind);getterObservation.insert("rawMatchesExactSource",rawMatches);
            if(kind=="getter-raw"&&!check(rawMatches,"authoritative unescape getter retains exact synthetic source")){stop("unescape source contract");return;}
            check(getterObservation.value("nodeCountUnchanged").toBool()&&getterObservation.value("bodyClassUnchanged").toBool()&&getterObservation.value("modifiedUnchanged").toBool(),"public getter preserves measured DOM shape and modified flag");
            checkpoint("reader-after-source-getter",[this]{
                read("(()=>{const start=performance.now(),node=document.createElement('textarea');node.innerHTML=window.__WizSyntheticSource249;const text=node.value,ms=performance.now()-start;let hash=2166136261;for(let i=0;i<text.length;i++)hash=Math.imul((hash^text.charCodeAt(i))>>>0,16777619)>>>0;delete window.__WizSyntheticSource249;return {hash,length:text.length,decodeMs:ms};})()",[this](const QVariant& value){
                    const auto decoded=value.toMap();const bool matches=decoded.value("length").toInt()==source.size()&&quint32(decoded.value("hash").toULongLong())==attributionDigest(source);
                    getterObservation.insert("decodedMatchesExactSource",matches);getterObservation.insert("decodeMs",decoded.value("decodeMs").toDouble());getterObservation.insert("decodedLength",decoded.value("length").toInt());getterObservation.insert("decodedHash",double(decoded.value("hash").toULongLong()));
                    checkpoint("reader-after-separate-decode",[this]{close();});
                });
            });
        });
    }
    void edit(){
        page->runJavaScript("window.WizPhase246EditDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>WizPhase246EditDone=true);");
        poll("WizPhase246EditDone&&document.body.isContentEditable",QDeadlineTimer(35000),[this]{checkpoint("edit-before-source-getter",[this]{
            read("WizEditor.getMarkdownSrc({unEscapeHtml:true})",[this](const QVariant& value){if(!check(value.toString()==source,"exact synthetic source after edit transition")){stop("edit source mismatch");return;}checkpoint("edit-after-source-getter",[this]{close();});});
        });});
    }
    void close(){
        read("WizPhase246.finish()",[this](const QVariant& value){
            const auto telemetry=QJsonObject::fromVariantMap(value.toMap());
            bool metal=true;QList<QPointer<QQuickWindow>> windows;for(auto* w:view->findChildren<QQuickWidget*>()){windows.append(w->quickWindow());metal&=int(w->quickWindow()->rendererInterface()->graphicsApi())==6;}
            check(metal&&!windows.isEmpty()&&(!view->windowHandle()||!view->windowHandle()->isExposed()),"hidden Cocoa Metal without pixel grabs");
            QPointer<QWebEngineView> weakView=view.get();QPointer<QWebEnginePage> weakPage=page;monitor.stop();view.reset();
            later(500,[this,weakView,weakPage,windows,telemetry]{bool gone=weakView.isNull()&&weakPage.isNull();for(auto w:windows)gone&=w.isNull();check(gone&&profile->findChildren<QObject*>().isEmpty(),"synthetic page view and Quick objects destroyed");
                const auto closed=attributionMemory(renderers);check(closed.value("children").toArray().isEmpty(),"own renderer exits after page close");check(local.blocked.load()==0,"no external request in phase attribution");
                qInfo().noquote()<<"INITIALIZATION_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"kind",kind},{"checks",checks},{"failures",failures},{"paragraphs",6000},{"formulaCount",kind.startsWith("math")?128:0},{"sourceBytes",source.toUtf8().size()},{"externalRequests",local.blocked.load()},{"samplePeriodMs",50},{"phases",phases},{"getterObservation",getterObservation},{"trace",telemetry},{"closed",closed},{"productCodeChanged",false},{"privateEditorInternalsInspected",false}}).toJson(QJsonDocument::Compact));
                stopped=true;profile->setUrlRequestInterceptor(nullptr);profile.reset();finished(failures?13:0);
            });
        });
    }
    void loaded(){
        page->runJavaScript(R"JS((()=>{
const marks=[],totals=Object.create(null),wrapped=new WeakSet();let sequence=0;
const heap=()=>performance.memory?{used:performance.memory.usedJSHeapSize,total:performance.memory.totalJSHeapSize}:null;
function mark(name){if(marks.length<256)marks.push({sequence:++sequence,name,ms:performance.now(),heap:heap()});}
function wrap(object,name,label){const original=object&&object[name];if(typeof original!=='function'||wrapped.has(original))return;const stat=totals[label]||(totals[label]={calls:0,totalMs:0,maxMs:0,inputCharacters:0,maxInputCharacters:0,outputCharacters:0,sourceGetterCalls:0});object[name]=function(){const start=performance.now(),reads=totals['public-source-getter']?totals['public-source-getter'].calls:0;++stat.calls;if(typeof arguments[0]==='string'){stat.inputCharacters+=arguments[0].length;stat.maxInputCharacters=Math.max(stat.maxInputCharacters,arguments[0].length);}mark(label+':begin');let value;try{value=original.apply(this,arguments);return value;}finally{const ms=performance.now()-start;stat.totalMs+=ms;stat.maxMs=Math.max(stat.maxMs,ms);if(typeof value==='string')stat.outputCharacters+=value.length;stat.sourceGetterCalls+=(totals['public-source-getter']?totals['public-source-getter'].calls:0)-reads;mark(label+':return');}};wrapped.add(object[name]);}
for(const name of ['WizApplyNoteStyle','WizApplyMathEnvironments','WizConfigureMarkdownEnvironments','WizMathProtectedPositions','WizMarkdownEnvironmentCodePositions','WizMarkdownEnvironmentBlocks','WizRenderLatexTheorems','WizProtectMarkdownEquationReferences'])wrap(window,name,name);
wrap(WizEditor,'getMarkdownSrc','public-source-getter');
const init=WizEditor.init;WizEditor.init=function(options){for(const name of ['markdown','mathJax']){const old=options.reader.callback[name];options.reader.callback[name]=function(){mark(name+':callback-begin');try{if(old)return old.apply(this,arguments);}finally{mark(name+':callback-return');}};}wrap(options.reader.markdownPlugIn,'customBlockGamut','owned-markdown-hook');mark('public-init:begin');try{return init.apply(this,arguments);}finally{wrap(WizEditor,'getMarkdownSrc','public-source-getter');mark('public-init:return');}};
window.WizPhase246={snapshot:()=>({heap:heap(),domElements:document.querySelectorAll('*').length,bodyClass:document.body.className,editable:document.body.isContentEditable,sourceGetterCalls:totals['public-source-getter']?totals['public-source-getter'].calls:0,ownedTotals:totals}),finish:()=>({marks,totals,marksCapped:marks.length===256})};mark('scripts-loaded');
})())JS");
        poll("!!window.WizPhase246",QDeadlineTimer(5000),[this]{checkpoint("scripts-loaded-before-init",[this]{initialize();});});
    }
public:
    InitializationAttribution(QTemporaryDir& root,const QString& resources,const QString& kind,std::function<void(int)> done):root(root),resources(resources),kind(kind),finished(std::move(done)){
        source="# Synthetic large document\n\n";for(int i=0;i<6000;++i)source+=QString("Paragraph %1: Synthetic academic prose with **emphasis**, a local list of concepts, and punctuation. 合成正文，仅用于离线大文档测试；不来自任何真实笔记。\n\n").arg(i);
        if(kind.startsWith("math")){for(int i=0;i<128;++i)source+=QString("\\begin{equation}\\label{retention:%1}\na_{%1}+b=c\n\\end{equation}\n\n").arg(i);source+="See \\eqref{retention:0} and \\eqref{retention:127}.\n";}if(kind=="math-ref"||kind=="math-theorem-ref")source+="Plain reference \\ref{retention:0}.\n";
        if(kind=="math-theorem-ref")source+="\n\\begin{theorem}\\label{theorem:outer}\nOuter reference \\ref{retention:0}.\n\\begin{proof}\nNested reference \\ref{retention:127}.\n\\end{proof}\n\\end{theorem}\n\n\\begin{corollary}\nSibling reference \\ref{retention:0}.\n\\end{corollary}\n";
        source+="\nSynthetic document instance 0.\n";
        if(kind.startsWith("getter-"))source+="\nLiteral <tag> & \"quotes\" 'single' &amp; &lt; &#169; 中文😀.\n\n`code \\alpha & \\beta`\n";
        QWebEngineProfileBuilder builder;builder.setPersistentStoragePath(root.filePath("profile")).setCachePath(root.filePath("cache"));profile.reset(builder.createProfile(QStringLiteral("SyntheticInitialization246")));if(!profile)qFatal("isolated profile");profile->setUrlRequestInterceptor(&local);
        monitor.setInterval(50);QObject::connect(&monitor,&QTimer::timeout,this,[this]{nativePeak();});
    }
    void start(){
        elapsed.start();editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+source.toHtmlEscaped().replace("\n","<br>")+"</body></html>";QString html=editor.original,tags;
        for(const auto& f:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})tags+="<script src=\""+QUrl::fromLocalFile(resources+f).toString().toHtmlEscaped()+"\" name='wiz_inner_script' wiz_style='unsave'></script>";
        html.insert(html.indexOf("</head>"),WizReaderOpeningPresentation::bootstrapHtml()+tags);channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);
        QFile file(root.filePath("initialization.html"));if(!file.open(QFile::WriteOnly)){stop("synthetic file opens");return;}file.write(html.toUtf8());file.close();
        view=std::make_unique<QWebEngineView>();view->setAttribute(Qt::WA_DontShowOnScreen);view->resize(900,600);page=new QWebEnginePage(profile.get(),view.get());page->setWebChannel(&channel);view->setPage(page);
        QObject::connect(view.get(),&QWebEngineView::loadFinished,this,[this](bool ok){if(!check(ok,"synthetic initialization page loads")){stop("local load");return;}later(0,[this]{loaded();});});view->show();monitor.start();view->load(QUrl::fromLocalFile(file.fileName()));
    }
};
static int runInitializationAttribution(QTemporaryDir& root,const QString& resources,const QString& kind){
    if(QGuiApplication::platformName()!="cocoa"||!QStringList{"math","math-ref","math-theorem-ref","prose","loaded-only","getter-raw","getter-encoded","getter-default"}.contains(kind))return 4;int status=0;
    InitializationAttribution probe(root,resources,kind,[&](int value){status=value;QCoreApplication::quit();});QTimer::singleShot(0,&probe,[&]{probe.start();});QCoreApplication::exec();return status;
}
#endif
