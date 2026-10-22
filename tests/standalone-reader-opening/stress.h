// Bounded synthetic public-API workload. No account, persistent note, forced GC,
// cache clearing, graphics override, or accumulating per-frame telemetry.
#include <QElapsedTimer>
#include <QProcess>
#include <QSet>
#include <QPointer>
#include <QQuickWidget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#ifdef Q_OS_MAC
#include <mach/mach.h>
#include <malloc/malloc.h>
#endif

static void stressSettle(int ms) {
    QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();
}
static QJsonObject stressMemory() {
    QProcess ps;ps.start("/bin/ps",{"-axo","pid=,ppid=,rss="});const auto observer=ps.processId();
    if(!ps.waitForFinished(5000)||ps.exitCode()!=0)return {{"available",false}};
    struct Row{qint64 pid,parent,rss;};QList<Row> rows;
    for(const auto& line:ps.readAllStandardOutput().split('\n')){
        auto parts=line.simplified().split(' ');
        if(parts.size()==3)rows.append({parts[0].toLongLong(),parts[1].toLongLong(),parts[2].toLongLong()});
    }
    QSet<qint64> members{QCoreApplication::applicationPid()};bool changed=true;
    while(changed){changed=false;for(const auto& row:rows)if(members.contains(row.parent)&&!members.contains(row.pid)){members.insert(row.pid);changed=true;}}
    qint64 rss=0,main=0;int count=0;
    for(const auto& row:rows)if(members.contains(row.pid)&&row.pid!=observer){rss+=row.rss;++count;if(row.pid==QCoreApplication::applicationPid())main=row.rss;}
    QJsonObject result{{"available",true},{"rssKiBSum",rss},{"mainRssKiB",main},{"processCount",count},
        {"widgetCount",QApplication::allWidgets().size()},{"windowCount",QGuiApplication::allWindows().size()}};
#ifdef Q_OS_MAC
    task_vm_info_data_t info{};mach_msg_type_number_t size=TASK_VM_INFO_COUNT;
    if(task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&info),&size)==KERN_SUCCESS)
        result.insert("mainFootprintKiB",qint64(info.phys_footprint/1024));
    malloc_statistics_t heap{};malloc_zone_statistics(nullptr,&heap);
    result.insert("mallocInUseKiB",qint64(heap.size_in_use/1024));
    result.insert("mallocAllocatedKiB",qint64(heap.size_allocated/1024));
#endif
    return result;
}
static int runReaderStress(QTemporaryDir& root,const QString& resources,const QString& kind) {
    // Functional throughput harness only: these closed samples share one outer
    // callback/autorelease lifetime. They cannot establish profile-held growth.
    // Use retention-yield for per-turn native memory acceptance instead.
    if(QGuiApplication::platformName()!="cocoa")return 4;
    const bool math=kind=="stress-math";const int formulaCount=math?512:0,rounds=8;
    const bool noGrab=kind=="stress-static-no-grab";
    const bool staticControl=kind=="stress-static"||noGrab;
    int checks=0,failures=0;const auto check=[&](bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;};
    QWebEngineProfile profile;profile.setCachePath(root.filePath("cache"));profile.setPersistentStoragePath(root.filePath("profile"));
    LocalOnly local;profile.setUrlRequestInterceptor(&local);QJsonArray samples;
    samples.append(QJsonObject{{"phase","baseline"},{"memory",stressMemory()}});
    QString source="# Synthetic large document\n\n";
    for(int i=0;i<6000;++i){
        source+=QString("Paragraph %1: Synthetic academic prose with **emphasis**, a local list of concepts, and punctuation. 合成正文，仅用于离线大文档测试；不来自任何真实笔记。\n\n").arg(i);
        if(math&&i<formulaCount)source+=QString("\\begin{equation}\\label{stress:%1}\n\\sum_{k=1}^{%2}\\frac{k^2+1}{k+1}=S_{%1}\n\\end{equation}\n\n").arg(i).arg(i+1);
    }
    if(math)source+="See \\eqref{stress:0} and \\eqref{stress:511}.\n";
    QString tags;
    for(const auto& f:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})
        tags+="<script src=\""+QUrl::fromLocalFile(resources+f).toString().toHtmlEscaped()+"\" name='wiz_inner_script' wiz_style='unsave'></script>";
    for(int round=0;round<rounds;++round){
        QElapsedTimer elapsed;elapsed.start();const QString current=source+QString("\nSynthetic document instance %1.\n").arg(round);
        auto view=std::make_unique<QWebEngineView>();view->setAttribute(Qt::WA_DontShowOnScreen);view->resize(900,600);
        auto* page=new QWebEnginePage(&profile,view.get());view->setPage(page);QWebChannel channel;Bridge editor,explorer;
        editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+current.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
        if(!staticControl){channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);page->setWebChannel(&channel);}
        QString html=editor.original;if(!staticControl)html.insert(html.indexOf("</head>"),WizReaderOpeningPresentation::bootstrapHtml()+tags);
        QFile file(root.filePath("stress.html"));if(!file.open(QFile::WriteOnly))return 2;file.write(html.toUtf8());file.close();
        bool loaded=false,loadOk=false;QObject::connect(view.get(),&QWebEngineView::loadFinished,[&](bool ok){loaded=true;loadOk=ok;});
        view->show();view->load(QUrl::fromLocalFile(file.fileName()));
        check(wait([&]{return loaded;},45000)&&loadOk,"large synthetic HTML loads");if(!loadOk)return 3;
        qint64 initialMs=0;if(!staticControl){
        js(page,R"JS(window.stressTicks=0;window.stressMaxGap=0;window.stressLast=performance.now();window.stressTimer=setInterval(()=>{const now=performance.now();stressMaxGap=Math.max(stressMaxGap,now-stressLast);stressLast=now;++stressTicks;},20);)JS");
        js(page,R"JS(window.stressOwnedTiming={};for(const name of ['WizApplyNoteStyle','WizApplyMathEnvironments','WizMathProtectedPositions','WizRenderLatexTheorems','WizMarkdownEnvironmentCodePositions','WizProtectMarkdownEquationReferences','WizMarkdownEnvironmentBlocks']){const original=window[name];if(typeof original!=='function')continue;const entry=stressOwnedTiming[name]={calls:0,totalMs:0,maxMs:0};window[name]=function(){const begin=performance.now();try{return original.apply(this,arguments);}finally{const ms=performance.now()-begin;++entry.calls;entry.totalMs+=ms;entry.maxMs=Math.max(entry.maxMs,ms);}};})JS");
        QString body="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';";
        body+=WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-opening-user","Synthetic",false,"markdown");
        body+="WizPrepareNoteModeChange();await new Promise(resolve=>WizEditor.off(null,resolve));WizApplyNoteStyle();WizApplySpellCheck();";
        page->runJavaScript(WizDocumentPageReady::setupScript(literal("stress"),body));
        const bool ready=wait([&]{return js(page,"window.WizNativeEditorSetup&&WizNativeEditorSetup.state==='ready'").toBool();},45000);
        check(ready,"large document finishes public native setup");if(!ready)return 5;
        stressSettle(100);initialMs=elapsed.elapsed();
        check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==current,"large document exact source retained after initial render");
        check(js(page,"document.querySelectorAll('.MathJax_Error,.merror').length===0").toBool(),"large formula set has no rendering errors");
        check(js(page,"window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().length:0").toInt()==formulaCount+(math?2:0),"all formulas and references rendered");
        if(math)check(js(page,"(()=>{const a=[...document.querySelectorAll('a')].map(x=>x.getAttribute('href')||x.getAttributeNS('http://www.w3.org/1999/xlink','href')).filter(x=>x&&x.startsWith('#mjx-eqn'));return a.length===2&&a.every(x=>!!document.getElementById(decodeURIComponent(x.slice(1))));})()").toBool(),"both large document equation links have real targets");
        // Exercise the prior blank-page regression on this genuinely large DOM.
        js(page,"window.stressModeDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>stressModeDone=true);");
        check(wait([&]{return js(page,"stressModeDone===true&&document.body.isContentEditable").toBool();},45000),"large document enters editor");
        const QString serialized=js(page,"WizEditor.getContentHtml()").toString();
        check(!serialized.isEmpty()&&!serialized.contains("opacity:0!important")&&!serialized.contains("wiz_markdown_math_scheduling"),"large editor snapshot excludes transient hide and math configuration");
        js(page,"window.stressModeDone=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>{WizApplyNoteStyle();WizApplySpellCheck();stressModeDone=true;});");
        check(wait([&]{return js(page,"stressModeDone===true&&!document.body.isContentEditable").toBool();},45000),"large document returns to reader");
        check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==current,"large document exact source retained after mode round trip");
        check(js(page,"getComputedStyle(document.documentElement).opacity!=='0'&&document.body.textContent.includes('Synthetic document instance')").toBool(),"large reader is visible and nonempty after mode round trip");
        check(js(page,"window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().length:0").toInt()==formulaCount+(math?2:0),"all formulas survive mode round trip");
        }else check(js(page,"document.body.textContent").toString().contains("Synthetic document instance")&&js(page,"typeof WizEditor==='undefined'&&document.scripts.length===0").toBool(),"static control has complete synthetic prose and no editor scripts");
        QJsonArray backends;QList<QPointer<QQuickWindow>> quickWindows;
        for(auto* widget:view->findChildren<QQuickWidget*>()){
            quickWindows.append(widget->quickWindow());backends.append(QJsonObject{{"windowClass",widget->quickWindow()->metaObject()->className()},{"graphicsApi",int(widget->quickWindow()->rendererInterface()->graphicsApi())}});
        }
        check(!backends.isEmpty()&&view->testAttribute(Qt::WA_DontShowOnScreen)&&(!view->windowHandle()||!view->windowHandle()->isExposed())&&(noGrab||!view->grab().isNull()),"hidden Cocoa page has real graphics backing without exposing a window");
        const auto telemetry=QJsonObject::fromVariantMap(js(page,staticControl?"({domNodes:document.querySelectorAll('*').length})":"({ticks:stressTicks,maxGapMs:Math.round(stressMaxGap),domNodes:document.querySelectorAll('*').length,configNodes:document.querySelectorAll('#wiz_markdown_math_scheduling').length,loaderSettled:WizMarkdownEnvironmentLoader===null,ownedInclusiveTiming:stressOwnedTiming})").toMap());
        samples.append(QJsonObject{{"round",round},{"phase","open"},{"initialReadyMs",initialMs},{"readyAndRoundTripMs",elapsed.elapsed()},{"sourceBytes",current.toUtf8().size()},{"telemetry",telemetry},{"backends",backends},{"memory",stressMemory()}});
        QPointer<QWebEngineView> weakView=view.get();QPointer<QWebEnginePage> weakPage=page;
        view.reset();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);stressSettle(300);
        bool destroyed=weakView.isNull()&&weakPage.isNull();for(const auto& w:quickWindows)destroyed&=w.isNull();
        check(destroyed,"closed page, view and Quick windows are destroyed");
        auto closed=QJsonObject{{"round",round},{"phase","closed"},{"memory",stressMemory()},{"profileObjects",profile.findChildren<QObject*>().size()}};samples.append(closed);
        qInfo().noquote()<<"STRESS_SAMPLE"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"round",round},{"open",samples.at(samples.size()-2)},{"closed",closed}}).toJson(QJsonDocument::Compact));
    }
    stressSettle(10000);samples.append(QJsonObject{{"phase","closed-10s"},{"memory",stressMemory()},{"profileObjects",profile.findChildren<QObject*>().size()}});
    check(local.blocked.load()==0,"stress workload makes no external requests");profile.setUrlRequestInterceptor(nullptr);
    qInfo().noquote()<<"STRESS_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"kind",kind},{"nativeMemorySequence","nested-single-callback-not-ui-representative"},{"pixelGrab",!noGrab},{"rounds",rounds},{"formulaCount",formulaCount},{"paragraphs",6000},{"checks",checks},{"failures",failures},{"blockedExternalRequests",local.blocked.load()},{"samples",samples}}).toJson(QJsonDocument::Compact));
    return failures?10:0;
}
