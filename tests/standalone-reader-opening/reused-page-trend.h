// One live view/page/channel/profile, normal public reader navigation only.
#include <QWebEngineHistory>
#include <QWebEngineSettings>
#ifdef Q_OS_MAC
class ReusedPageTrend final:public QObject {
    QTemporaryDir& root;QString resources,source,current;std::array<qsizetype,3> sizes{};
    std::unique_ptr<QWebEngineProfile> profile;LocalOnly local;QWebChannel channel;Bridge editor,explorer;
    std::unique_ptr<QWebEngineView> view;QPointer<QWebEnginePage> page;QSet<qint64> rendererPids;
    QElapsedTimer elapsed;int note=0,checks=0,failures=0;bool stopped=false,retiring=false,idleAttribution=false,rendererMetadata=false;qint64 readingAt=0;QJsonObject publicDom;std::function<void(int)> finished;
    int paragraphs()const{return idleAttribution?(note%2==0?6000:60):(note%3==0?6000:note%3==1?600:60);}
    int notes()const{return idleAttribution?4:24;}
    void later(int ms,std::function<void()> f){QTimer::singleShot(ms,this,[this,f=std::move(f)]{if(!stopped)f();});}
    bool check(bool v,const char* text){++checks;failures+=!v;qInfo().noquote()<<(v?"PASS":"FAIL")<<text;return v;}
    void stop(const char* why){if(stopped)return;check(false,why);stopped=true;view.reset();profile->setUrlRequestInterceptor(nullptr);profile.reset();finished(15);}
    void read(const QString& code,std::function<void(const QVariant&)> next){const int requested=note;if(!page){stop("live synthetic reused page");return;}page->runJavaScript(code,[this,requested,next=std::move(next)](const QVariant& v){if(!stopped&&page&&note==requested)next(v);});}
    void poll(const QString& code,QDeadlineTimer deadline,std::function<void()> next){read(code,[this,code,deadline,next=std::move(next)](const QVariant& v){if(v.toBool()){later(0,next);return;}if(deadline.hasExpired()){stop("synthetic reused reader deadline");return;}later(25,[this,code,deadline,next]{poll(code,deadline,next);});});}
    void sample(const QString& phase){
        if(page&&page->renderProcessPid()>0)rendererPids.insert(page->renderProcessPid());
        const auto m=attributionMemory(rendererPids);const auto s=QJsonObject{{"phase",phase},{"note",note},{"elapsedMs",elapsed.elapsed()},{"profileLive",bool(profile)},{"samePage",bool(page&&view&&view->page()==page&&page->profile()==profile.get())},{"historyEntries",page?page->history()->count():0},{"backForwardCacheEnabled",page?page->settings()->testAttribute(QWebEngineSettings::BackForwardCacheEnabled):false},{"currentRendererPid",page?qint64(page->renderProcessPid()):0},{"paragraphs",paragraphs()},{"publicDom",publicDom},{"memory",m}};
        qInfo().noquote()<<"REUSE_SAMPLE"<<QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact));
    }
    void waitAck(const QString& ack,QDeadlineTimer deadline,std::function<void()> next){
        if(QFile::exists(ack)){later(0,std::move(next));return;}
        if(deadline.hasExpired()){stop("own renderer metadata acknowledgement deadline");return;}
        later(25,[this,ack,deadline,next=std::move(next)]{waitAck(ack,deadline,next);});
    }
    void publicSample(const QString& phase,std::function<void()> next){
        if(!idleAttribution){sample(phase);next();return;}
        // Standard DOM plus this project's own helper state only. No editor
        // implementation, heap snapshot, JS stack, or source getter is read.
        read("(()=>{let detachedSpell=0,detachedMath=0;const s=WizSpellCheckState,d=WizMathDecoration;s.records.forEach((v,n)=>{if(!n.isConnected)++detachedSpell;});['blocks','links','splits'].forEach(k=>d[k].forEach(r=>{if(!r.element.isConnected)++detachedMath;}));return {domNodes:document.querySelectorAll('*').length,paragraphs:document.querySelectorAll('p').length,bodyCharacters:document.body.textContent.length,scrollHeight:document.documentElement.scrollHeight,sourceCalls:window.__syntheticPublicSourceCalls,spellTracked:s.records.size,detachedSpellRecords:detachedSpell,mathTracked:d.blocks.length+d.links.length+d.splits.length,detachedMathRecords:detachedMath,mathReferenceRootsUseWeakCache:typeof WizMathReferenceRoots!=='undefined'&&WizMathReferenceRoots instanceof WeakSet,currentMathReferenceRootTracked:typeof WizMathReferenceRoots!=='undefined'&&WizMathReferenceRoots.has(document.body)};})()",[this,phase,next=std::move(next)](const QVariant& value){
            publicDom=QJsonObject::fromVariantMap(value.toMap());
            if(!check(publicDom.contains("domNodes")&&publicDom.value("paragraphs").toInt()>=paragraphs()&&publicDom.value("sourceCalls").toInt(-1)==0,"public current DOM counters without extra source getter")){stop("idle attribution DOM sample");return;}
            sample(phase);
            const bool capture=rendererMetadata&&((paragraphs()==6000&&phase=="reading-ready")||phase=="small-note-idle-10s"||phase=="small-note-idle-60s");
            if(!capture){next();return;}
            const auto ack=root.filePath(QString("renderer-%1-%2.ack").arg(note).arg(phase));
            qInfo().noquote()<<"REUSE_OBSERVER"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"rendererPid",page->renderProcessPid()},{"phase",phase},{"note",note},{"ack",ack}}).toJson(QJsonDocument::Compact));
            waitAck(ack,QDeadlineTimer(12000),std::move(next));
        });
    }
    void idleCheckpoint(int stage){
        const std::array<int,3> seconds{10,30,60};const int second=seconds[stage];
        later(int(qMax<qint64>(0,readingAt+second*1000-elapsed.elapsed())),[this,stage,second]{
            publicSample(QString("small-note-idle-%1s").arg(second),[this,stage]{if(stage==2)advance();else idleCheckpoint(stage+1);});
        });
    }
    void begin(){
        current=source.left(sizes[paragraphs()==6000?2:paragraphs()==600?1:0])+QString("\nSynthetic document instance %1.\n").arg(note);
        editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+current.toHtmlEscaped().replace("\n","<br>")+"</body></html>";Q_EMIT editor.currentHtmlChanged();
        QString html=editor.original,tags;for(const auto& f:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})tags+="<script src=\""+QUrl::fromLocalFile(resources+f).toString().toHtmlEscaped()+"\" name='wiz_inner_script' wiz_style='unsave'></script>";
        html.insert(html.indexOf("</head>"),WizReaderOpeningPresentation::bootstrapHtml()+tags);QFile file(root.filePath(QString("reuse-%1.html").arg(note)));if(!file.open(QFile::WriteOnly)){stop("synthetic reuse HTML file");return;}file.write(html.toUtf8());file.close();view->load(QUrl::fromLocalFile(file.fileName()));
    }
    void initialize(){
        QString setup="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';";
        setup+=WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-opening-user","Synthetic",false,"markdown");
        setup+="window.__syntheticPublicSourceCalls=0;{const get=WizEditor.getMarkdownSrc;WizEditor.getMarkdownSrc=function(){++window.__syntheticPublicSourceCalls;return get.apply(this,arguments);};}WizPrepareNoteModeChange();await new Promise(resolve=>WizEditor.off(null,resolve));WizApplyNoteStyle();";
        // The owned main viewer also reapplies spelling after a mode change.
        // Earlier251 was reader-only; do not call this a product patch or A/B gain.
        if(idleAttribution)setup+="WizApplySpellCheck();";
        page->runJavaScript(WizDocumentPageReady::setupScript(literal(QString("reuse-%1").arg(note)),setup));
        poll("window.WizNativeEditorSetup&&WizNativeEditorSetup.state==='ready'",QDeadlineTimer(45000),[this]{
            const int paragraphs=this->paragraphs();
            read("(()=>({ready:!document.body.isContentEditable&&document.body.textContent.includes("+literal(QString("Synthetic document instance %1.").arg(note))+")&&document.querySelectorAll('p').length>="+QString::number(paragraphs)+"&&document.querySelectorAll('strong,b').length>="+QString::number(paragraphs)+"&&document.querySelectorAll('.MathJax_Error,merror').length===0,sourceReads:window.__syntheticPublicSourceCalls}))()",[this](const QVariant& v){
                const auto m=v.toMap();if(!check(m.value("ready").toBool(),"correct synthetic note rendered on the same page")){stop("reused note mismatch");return;}
                if(!check(m.contains("sourceReads")&&m.value("sourceReads").toInt()==0,"no extra public source getter during reading measurement")){stop("unexpected getter in reading window");return;}
                later(350,[this]{
                    bool metal=true;const auto widgets=view->findChildren<QQuickWidget*>();for(auto* w:widgets)metal&=int(w->quickWindow()->rendererInterface()->graphicsApi())==6;
                    if(!check(metal&&!widgets.isEmpty()&&(!view->windowHandle()||!view->windowHandle()->isExposed()),"same hidden native Metal presentation without grabs")){stop("reused native backend");return;}
                    readingAt=elapsed.elapsed();
                    publicSample("reading-ready",[this]{if(idleAttribution){if(this->paragraphs()==60)idleCheckpoint(0);else advance();}else if((note+1)%6==0)later(10000,[this]{sample("small-note-idle-10s");advance();});else advance();});
                });
            });
        });
    }
    void advance(){if(note==notes()-1){verifyAfterMetrics();return;}++note;later(50,[this]{begin();});}
    void verifyAfterMetrics(){
        // This source validation is outside all reading-memory checkpoints.
        const auto code=attributionDigestScript("WizEditor.getMarkdownSrc({unEscapeHtml:true})");
        read(code,[this](const QVariant& v){const auto m=v.toMap();check(m.value("length").toInt()==current.size()&&quint32(m.value("hash").toULongLong())==attributionDigest(current),"final original source length and UTF16 digest after measurements");
            sample("after-final-source-check-outside-metrics");QPointer<QWebEngineView> weak=view.get();QPointer<QWebEnginePage> weakPage=page;retiring=true;view.reset();
            later(700,[this,weak,weakPage]{check(weak.isNull()&&weakPage.isNull()&&profile->findChildren<QObject*>().isEmpty(),"single reused page and view destroyed at end");
                later(10000,[this]{sample("view-destroyed-profile-live-idle-10s");check(attributionMemory(rendererPids).value("children").toArray().isEmpty(),"own renderer exits after final view teardown");profile->setUrlRequestInterceptor(nullptr);profile.reset();later(10000,[this]{sample("profile-destroyed-idle-10s");finish();});});
            });
        });
    }
    void finish(){
        check(local.blocked.load()==0,"reused-page synthetic workload stays local");
        qInfo().noquote()<<"REUSE_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"notes",notes()},{"paragraphBands",idleAttribution?QJsonArray{60,6000}:QJsonArray{60,600,6000}},{"checks",checks},{"failures",failures},{"externalRequests",local.blocked.load()},{"singleViewPageChannelProfile",true},{"uniqueRendererPids",rendererPids.size()},{"extraSourceReadsDuringMemoryWindow",0},{"finalSourceReadOutsideMetrics",true},{"targetRetainsSampleArrays",false},{"profileOrHistoryClearedDuringMeasurement",false},{"forcedCollection",false},{"productCodeChanged",false},{"idleAttribution",idleAttribution},{"rendererMetadata",rendererMetadata}}).toJson(QJsonDocument::Compact));stopped=true;finished(failures?15:0);
    }
public:
    ReusedPageTrend(QTemporaryDir& root,const QString& resources,std::function<void(int)> done,bool idle=false,bool metadata=false):root(root),resources(resources),idleAttribution(idle),rendererMetadata(metadata),finished(std::move(done)){
        source="# Synthetic reused-page document\n\n";for(int i=0;i<6000;++i){source+=QString("Paragraph %1: Synthetic academic prose with **emphasis**, a local list of concepts, and punctuation. 合成正文，仅用于离线大文档测试；不来自任何真实笔记。\n\n").arg(i);if(i==59)sizes[0]=source.size();if(i==599)sizes[1]=source.size();}sizes[2]=source.size();
        QWebEngineProfileBuilder b;b.setPersistentStoragePath(root.filePath("profile")).setCachePath(root.filePath("cache"));profile.reset(b.createProfile(QStringLiteral("SyntheticReusedPageTrend")));if(!profile)qFatal("isolated reused profile");profile->setUrlRequestInterceptor(&local);
        channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);view=std::make_unique<QWebEngineView>();view->setAttribute(Qt::WA_DontShowOnScreen);view->resize(900,600);page=new QWebEnginePage(profile.get(),view.get());view->setPage(page);page->setWebChannel(&channel);
        QObject::connect(view.get(),&QWebEngineView::loadFinished,this,[this](bool ok){if(stopped||retiring)return;if(!check(ok,"local synthetic reused-page note loads")){stop("reused local load");return;}later(0,[this]{initialize();});});
    }
    void start(){elapsed.start();sample("baseline");view->show();begin();}
};
static int runReusedPageTrend(QTemporaryDir& root,const QString& resources){if(QGuiApplication::platformName()!="cocoa")return 4;int status=0;ReusedPageTrend probe(root,resources,[&](int s){status=s;QCoreApplication::quit();});QTimer::singleShot(0,&probe,[&]{probe.start();});QCoreApplication::exec();return status;}
static int runReusedPageIdleAttribution(QTemporaryDir& root,const QString& resources,bool metadata){if(QGuiApplication::platformName()!="cocoa")return 4;int status=0;ReusedPageTrend probe(root,resources,[&](int s){status=s;QCoreApplication::quit();},true,metadata);QTimer::singleShot(0,&probe,[&]{probe.start();});QCoreApplication::exec();return status;}
#endif
