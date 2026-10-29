// Test-only structural attribution through the supported editor API and our
// own helpers. No editor-source reading, DOM prototype interception or stacks.
#ifdef Q_OS_MAC
class SourceCostProbe final : public QObject {
    QTemporaryDir& root; QString resources, source; const int paragraphs; const QString contentKind;
    std::unique_ptr<QWebEngineProfile> profile; LocalOnly local;
    std::unique_ptr<QWebEngineView> view; QPointer<QWebEnginePage> page;
    QWebChannel channel; Bridge editor, explorer; QSet<qint64> renderers;
    QTimer monitor; quint64 rendererPeak=0; QJsonArray samples;
    int checks=0,failures=0; bool stopped=false; std::function<void(int)> done;
    void later(int ms,std::function<void()> next) { QTimer::singleShot(ms,this,[this,next=std::move(next)]{if(!stopped)next();}); }
    bool check(bool ok,const char* text) { ++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;return ok; }
    void stop(const char* text) { if(stopped)return;check(false,text);stopped=true;monitor.stop();view.reset();profile->setUrlRequestInterceptor(nullptr);profile.reset();done(14); }
    void read(const QString& code,std::function<void(const QVariant&)> next) { if(!page){stop("live source-cost page");return;}page->runJavaScript(code,[this,next=std::move(next)](const QVariant& v){if(!stopped&&page)next(v);}); }
    void poll(const QString& code,QDeadlineTimer deadline,std::function<void()> next) { read(code,[this,code,deadline,next=std::move(next)](const QVariant& v){if(v.toBool()){later(0,next);return;}if(deadline.hasExpired()){stop("source-cost initialization deadline");return;}later(25,[this,code,deadline,next]{poll(code,deadline,next);});}); }
    void peak() { if(!page||page->renderProcessPid()<=0)return;rusage_info_v2 r{};if(proc_pid_rusage(page->renderProcessPid(),RUSAGE_INFO_V2,reinterpret_cast<rusage_info_t*>(&r))==0)rendererPeak=std::max(rendererPeak,quint64(r.ri_phys_footprint)); }
    void sample(const QString& phase) { peak();if(page&&page->renderProcessPid()>0)renderers.insert(page->renderProcessPid());QJsonObject s{{"phase",phase},{"memory",attributionMemory(renderers)},{"sampledRendererPeakBytes",qint64(rendererPeak)}};samples.append(s);qInfo().noquote()<<"SOURCE_COST_SAMPLE"<<QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact)); }
    void probe() {
        sample("ready-before-first-getter");
        QElapsedTimer request;request.start();
        read(R"JS((()=>{
const modifiedStart=performance.now(),modifiedBefore=WizEditor.isModified(),modifiedBeforeMs=performance.now()-modifiedStart;
const before={nodes:document.querySelectorAll('*').length,bodyClass:document.body.className,modified:modifiedBefore};
const own=window.WizSourceCost257;own.reset();
const start=performance.now(),text=WizEditor.getMarkdownSrc({unEscapeHtml:true}),getterMs=performance.now()-start;
const digestStart=performance.now();let hash=2166136261;for(let i=0;i<text.length;++i)hash=Math.imul((hash^text.charCodeAt(i))>>>0,16777619)>>>0;
const digestMs=performance.now()-digestStart;window.WizSourceCost257Text=text;
return {getterMs,digestMs,modifiedBeforeMs,length:text.length,hash,own:own.snapshot(),bodyClassUnchanged:document.body.className===before.bodyClass,nodeCountUnchanged:document.querySelectorAll('*').length===before.nodes,modifiedUnchanged:WizEditor.isModified()===before.modified,domNodes:before.nodes,renderedCharacters:document.body.textContent.length,editable:document.body.isContentEditable,mathVersion:window.MathJax&&MathJax.version||null,initialProjection:window.WizSourceProjection258};
})())JS",[this,request](const QVariant& value){
            QJsonObject result=QJsonObject::fromVariantMap(value.toMap());result.insert("getterAndDigestRoundTripMs",request.nsecsElapsed()/1e6);
            if(!check(result.value("length").toInt()==source.size()&&quint32(result.value("hash").toDouble())==attributionDigest(source),"first necessary public getter exact length and UTF16 digest")){stop("source-cost getter source mismatch");return;}
            check(result.value("bodyClassUnchanged").toBool()&&result.value("nodeCountUnchanged").toBool()&&result.value("modifiedUnchanged").toBool()&&!result.value("editable").toBool(),"getter preserves reader DOM shape and modification state");
            check(result.value("getterMs").toDouble()>0&&result.value("own").isObject(),"separate public getter and own-helper timings present");
            if(contentKind=="literal"||contentKind=="theorem-literal")check(result.value("initialProjection").toObject().contains("sourceReads"),"literal-code projection source-read observation present");
            sample("after-getter-digest-only-transfer");
            QElapsedTimer transfer;transfer.start();
            read("window.WizSourceCost257Text",[this,result,transfer](const QVariant& v)mutable{
                result.insert("cachedStringRoundTripMs",transfer.nsecsElapsed()/1e6);
                check(v.toString()==source,"cached string bridge transfer is byte-equivalent without a second getter");sample("after-cached-string-transfer");
                read("delete window.WizSourceCost257Text;true",[this,result](const QVariant& v){check(v.toBool(),"diagnostic source snapshot released explicitly after measurement");later(1000,[this,result]{sample("idle-1s-after-release");finish(result);});});
            });
        });
    }
    void finish(const QJsonObject& observation) {
        bool metal=true;QList<QPointer<QQuickWindow>> windows;for(auto* w:view->findChildren<QQuickWidget*>()){windows.append(w->quickWindow());metal&=int(w->quickWindow()->rendererInterface()->graphicsApi())==6;}
        check(metal&&!windows.isEmpty()&&(!view->windowHandle()||!view->windowHandle()->isExposed()),"normal top-level loop hidden Cocoa Metal without pixel grabs");
        QPointer<QWebEngineView> weakView=view.get();QPointer<QWebEnginePage> weakPage=page;monitor.stop();view.reset();
        later(500,[this,weakView,weakPage,windows,observation]{bool gone=weakView.isNull()&&weakPage.isNull();for(auto w:windows)gone&=w.isNull();check(gone&&profile->findChildren<QObject*>().isEmpty(),"synthetic page view Quick windows destroyed");sample("closed-profile-still-live");check(local.blocked.load()==0,"source-cost no external request");
            const auto memory=attributionMemory(renderers);bool ownRendererGone=true;for(const auto& c:memory.value("children").toArray())if(c.toObject().value("rendererObservedByPage").toBool())ownRendererGone=false;check(ownRendererGone,"observed own renderer exits on close");
            const QJsonObject report{{"schema",2},{"kind","source-cost"},{"contentKind",contentKind},{"paragraphs",paragraphs},{"literalEntityOccurrences",source.count("&amp;")+source.count("&lt;")+source.count("&#169;")},{"sourceCharacters",source.size()},{"sourceBytes",source.toUtf8().size()},{"checks",checks},{"failures",failures},{"observation",observation},{"samples",samples},{"rendererPeakBytes",qint64(rendererPeak)},{"externalRequests",local.blocked.load()},{"privateEditorInternalsInspected",false},{"productCodeChanged",false},{"secondGetterUsedForTransfer",false},{"samplePeriodMs",50}};
            qInfo().noquote()<<"SOURCE_COST_REPORT"<<QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Compact));stopped=true;profile->setUrlRequestInterceptor(nullptr);profile.reset();done(failures?14:0);
        });
    }
    void loaded() {
        read(R"JS((()=>{
const stats=Object.create(null);let active=false;
for(const name of ['WizRestoreSpellCheckAttributes','WizApplySpellCheck','WizClearMathEnvironments','WizApplyMathEnvironments']){
const original=window[name];if(typeof original!=='function')throw Error('Missing owned helper '+name);
window[name]=function(){if(!active)return original.apply(this,arguments);const start=performance.now();try{return original.apply(this,arguments);}finally{const s=stats[name]||(stats[name]={calls:0,totalMs:0,maxMs:0});const ms=performance.now()-start;++s.calls;s.totalMs+=ms;s.maxMs=Math.max(s.maxMs,ms);}};
}
window.WizSourceCost257={reset(){for(const k of Object.keys(stats))delete stats[k];active=true;},snapshot(){active=false;return JSON.parse(JSON.stringify(stats));}};return true;
})())JS",[this](const QVariant& v){
            if(!check(v.toBool(),"only owned helpers instrumented, public engine API unchanged")){stop("owned-helper instrumentation");return;}
            page->runJavaScript(R"JS((()=>{
const configure=WizConfigureMarkdownEnvironments;
window.WizSourceProjection258={callbacks:0,sourceReads:0,totalMs:0};
window.WizConfigureMarkdownEnvironments=function(options){
const result=configure.apply(this,arguments),callback=result.reader.callback.beforeMarkdownRender;
result.reader.callback.beforeMarkdownRender=function(){
const source=WizEditor.getMarkdownSrc,stat=window.WizSourceProjection258;stat.callbacks++;
WizEditor.getMarkdownSrc=function(){stat.sourceReads++;return source.apply(this,arguments);};
const start=performance.now();try{return callback.apply(this,arguments);}finally{stat.totalMs+=performance.now()-start;WizEditor.getMarkdownSrc=source;}
};return result;
};return true;
})())JS");
            QString setup="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';";
            setup+=WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-opening-user","Synthetic",false,"markdown");
            setup+="WizPrepareNoteModeChange();await new Promise(resolve=>WizEditor.off(null,resolve));WizApplyNoteStyle();WizApplySpellCheck();";
            page->runJavaScript(WizDocumentPageReady::setupScript(literal("source-cost257"),setup));
            poll("window.WizNativeEditorSetup&&WizNativeEditorSetup.state==='ready'&&document.body.classList.contains('wiz-readonly')",QDeadlineTimer(45000),[this]{later(0,[this]{probe();});});
        });
    }
public:
    SourceCostProbe(QTemporaryDir& root,const QString& resources,int paragraphs,const QString& contentKind,std::function<void(int)> done):root(root),resources(resources),paragraphs(paragraphs),contentKind(contentKind),done(std::move(done)) {
        // Same UTF16 length and byte length in every variant. Only the number
        // of newline-separated Markdown paragraphs and resulting DOM differs.
        QString token="Synthetic academic prose with literals &amp; &lt; and punctuation plus text. ";
        if(contentKind=="plain")token.replace("&amp;","ampxx").replace("&lt;","ltxx");
        const int lineLength=720000/paragraphs-2;QString line;while(line.size()<lineLength)line+=token;line.truncate(lineLength);
        for(int i=0;i<paragraphs;++i)source+=line+"\n\n";
        source+="Literal entities: &amp; &lt; &#169; <tag> 中文。\n";
        check(source.size()==720046,"equal-size structural source fixture");
        if(contentKind=="literal")source+="\n```tex\n\\begin{equation}\nx=y\n\\end{equation}\n```\n";
        if(contentKind=="theorem-literal")source+="\n```tex\n\\begin{theorem}\nExample body.\n\\end{theorem}\n```\n";
        QWebEngineProfileBuilder builder;builder.setPersistentStoragePath(root.filePath("profile")).setCachePath(root.filePath("cache"));profile.reset(builder.createProfile(QStringLiteral("SyntheticSourceCost257")));if(!profile)qFatal("source-cost isolated profile");profile->setUrlRequestInterceptor(&local);
        monitor.setInterval(50);connect(&monitor,&QTimer::timeout,this,[this]{peak();});
    }
    void start() {
        editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+source.toHtmlEscaped().replace("\n","<br>")+"</body></html>";QString html=editor.original,tags;
        for(const auto& f:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})tags+="<script src=\""+QUrl::fromLocalFile(resources+f).toString().toHtmlEscaped()+"\" name='wiz_inner_script' wiz_style='unsave'></script>";
        html.insert(html.indexOf("</head>"),WizReaderOpeningPresentation::bootstrapHtml()+tags);channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);
        QFile file(root.filePath("source-cost.html"));if(!file.open(QFile::WriteOnly)){stop("source-cost synthetic file");return;}file.write(html.toUtf8());file.close();
        view=std::make_unique<QWebEngineView>();view->setAttribute(Qt::WA_DontShowOnScreen);view->resize(900,600);page=new QWebEnginePage(profile.get(),view.get());view->setPage(page);page->setWebChannel(&channel);
        connect(view.get(),&QWebEngineView::loadFinished,this,[this](bool ok){if(!check(ok,"source-cost synthetic page loads")){stop("source-cost load");return;}later(0,[this]{loaded();});});view->show();monitor.start();view->load(QUrl::fromLocalFile(file.fileName()));
    }
};
static int runSourceCost(QTemporaryDir& root,const QString& resources,int paragraphs,const QString& contentKind="entity") {
    if(QGuiApplication::platformName()!="cocoa"||!QList<int>{60,600,6000}.contains(paragraphs)||!QStringList{"entity","plain","literal","theorem-literal"}.contains(contentKind))return 4;int result=0;
    SourceCostProbe probe(root,resources,paragraphs,contentKind,[&](int status){result=status;QCoreApplication::quit();});QTimer::singleShot(0,&probe,[&]{probe.start();});QCoreApplication::exec();return result;
}
#endif
