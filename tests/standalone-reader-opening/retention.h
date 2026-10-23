// Native-only controls. Deliberately compare an artificial nested batch with
// real top-level event turns, keeping the SAME profile alive at the boundary.
#include <QWebEngineProfileBuilder>
extern int wizNativeMemoryBatchPool(const std::function<int()>&);
class NativeRetention final:public QObject {
public:
    QTemporaryDir& root;QString kind,source,resources;std::unique_ptr<QWebEngineProfile> profile;
    LocalOnly local;QJsonArray samples;int round=0,checks=0,failures=0;const int rounds;
    std::function<void(int)> finished;
    NativeRetention(QTemporaryDir& root,const QString& kind,std::function<void(int)> done,const QString& assets=QString()):root(root),kind(kind),resources(assets),rounds(kind=="retention-yield-persistent"?16:8),finished(std::move(done)){
        if(kind=="retention-yield-persistent"||kind.startsWith("retention-yield-editor")){
            QWebEngineProfileBuilder builder;builder.setPersistentStoragePath(root.filePath("profile")).setCachePath(root.filePath("cache"));
            profile.reset(builder.createProfile(QStringLiteral("SyntheticNativeRetention")));
            if(!profile)qFatal("Cannot construct synthetic isolated profile");
        }else profile=std::make_unique<QWebEngineProfile>();
        profile->setCachePath(root.filePath("cache"));profile->setPersistentStoragePath(root.filePath("profile"));profile->setUrlRequestInterceptor(&local);
        source="# Synthetic large document\n\n";
        for(int i=0;i<6000;++i)source+=QString("Paragraph %1: Synthetic academic prose with **emphasis**, a local list of concepts, and punctuation. 合成正文，仅用于离线大文档测试；不来自任何真实笔记。\n\n").arg(i);
        if(kind.startsWith("retention-yield-editor")){
            for(int i=0;i<128;++i)source+=QString("\\begin{equation}\\label{retention:%1}\na_{%1}+b=c\n\\end{equation}\n\n").arg(i);
            source+="See \\eqref{retention:0} and \\eqref{retention:127}.\n";
        }
    }
    void check(bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;}
    void sample(const QString& phase){
        QJsonObject s{{"phase",phase},{"round",round},{"profileLive",bool(profile)},{"profileGeneration",1},{"offTheRecord",profile?QJsonValue(profile->isOffTheRecord()):QJsonValue(QJsonValue::Null)},{"storageIsTemporary",profile?QJsonValue(profile->persistentStoragePath()==root.filePath("profile")&&profile->cachePath()==root.filePath("cache")):QJsonValue(QJsonValue::Null)},{"profileObjects",profile?profile->findChildren<QObject*>().size():0},{"memory",stressMemory()}};
        samples.append(s);qInfo().noquote()<<"RETENTION_SAMPLE"<<QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact));
    }
    void one(){
        const QString current=source+QString("\nSynthetic document instance %1.\n").arg(round);
        const bool editorCase=kind.startsWith("retention-yield-editor");
        QString html="<!doctype html><html><head><meta charset='utf-8'></head><body>"+current.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
        struct EditorContext {QWebChannel channel;Bridge editor,explorer;};
        std::unique_ptr<EditorContext> context;
        if(editorCase){
            context=std::make_unique<EditorContext>();context->editor.original=html;
            QString tags;
            for(const auto& f:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})
                tags+="<script src=\""+QUrl::fromLocalFile(resources+f).toString().toHtmlEscaped()+"\" name='wiz_inner_script' wiz_style='unsave'></script>";
            html.insert(html.indexOf("</head>"),WizReaderOpeningPresentation::bootstrapHtml()+tags);
            context->channel.registerObject("WizQtEditor",&context->editor);context->channel.registerObject("WizExplorerApp",&context->explorer);
        }
        QFile file(root.filePath("native-retention.html"));if(!file.open(QFile::WriteOnly)){check(false,"synthetic file opens");return;}file.write(html.toUtf8());file.close();
        auto view=std::make_unique<QWebEngineView>();view->setAttribute(Qt::WA_DontShowOnScreen);view->resize(900,600);auto* page=new QWebEnginePage(profile.get(),view.get());view->setPage(page);
        if(editorCase)page->setWebChannel(&context->channel);
        bool loaded=false,loadOk=false;QObject::connect(view.get(),&QWebEngineView::loadFinished,[&](bool ok){loaded=true;loadOk=ok;});view->show();view->load(QUrl::fromLocalFile(file.fileName()));
        check(wait([&]{return loaded;},35000)&&loadOk,"native-only synthetic document loads");
        if(editorCase){
            QString setup="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';";
            setup+=WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-opening-user","Synthetic",false,"markdown");
            setup+="WizPrepareNoteModeChange();await new Promise(resolve=>WizEditor.off(null,resolve));WizApplyNoteStyle();";
            page->runJavaScript(WizDocumentPageReady::setupScript(literal("retention-editor"),setup));
            check(wait([&]{return js(page,"window.WizNativeEditorSetup&&WizNativeEditorSetup.state==='ready'").toBool();},45000),"persistent-profile editor and actual formula engine become ready");
            check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==current,"editor source remains exact before close");
            check(js(page,"MathJax.Hub.getAllJax().length===130&&document.querySelectorAll('.MathJax_Error,merror').length===0&&WizMarkdownEnvironmentLoader===null").toBool(),"128 formulas and two references settle without outstanding owned loader");
            js(page,"window.retentionEditDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>retentionEditDone=true);");
            check(wait([&]{return js(page,"retentionEditDone&&document.body.isContentEditable").toBool();},35000)&&js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==current,"persistent-profile edit restores exact source before page teardown");
        }else{
            check(js(page,"document.body.textContent").toString()==QString(current).remove('\n'),"complete static source retained");
            check(js(page,"document.scripts.length===0&&typeof WizEditor==='undefined'&&typeof MathJax==='undefined'").toBool()&&page->webChannel()==nullptr,"no editor, math engine, scripts or channel");
        }
        bool metal=true;QList<QPointer<QQuickWindow>> quickWindows;
        for(auto* w:view->findChildren<QQuickWidget*>()){quickWindows.append(w->quickWindow());metal&=int(w->quickWindow()->rendererInterface()->graphicsApi())==6;}
        check(metal&&!quickWindows.isEmpty()&&(!view->windowHandle()||!view->windowHandle()->isExposed()),"hidden native Metal window remains unexposed without pixel grabs");
        QPointer<QWebEnginePage> weakPage=page;QPointer<QWebEngineView> weakView=view.get();view.reset();QCoreApplication::sendPostedEvents(nullptr,QEvent::DeferredDelete);stressSettle(300);
        bool destroyed=weakPage.isNull()&&weakView.isNull();for(auto w:quickWindows)destroyed&=w.isNull();check(destroyed,"page, view and Quick windows destroyed");
    }
    void step(){
        sample("before-round");
        if(kind=="retention-nested-pool")wizNativeMemoryBatchPool([this]{one();return 0;});else one();
        sample("closed");++round;
        if(round==rounds)atBoundary();else if(kind.startsWith("retention-yield"))QTimer::singleShot(50,this,[this]{step();});
    }
    void start(){
        sample("baseline");
        if(kind.startsWith("retention-yield"))step();else {while(round<rounds)step();}
    }
    void atBoundary(){
        // For the nested batch this marker is still inside its single outer
        // timer callback. Do not confuse a nested QEventLoop with returning it.
        sample("batch-callback-end");
        QTimer::singleShot(10000,this,[this]{
            sample("profile-live-idle-10s");check(bool(profile)&&profile->findChildren<QObject*>().isEmpty(),"same profile remains alive after top-level event-loop idle");
            if(kind=="retention-yield-editor-vm") {
                QTimer::singleShot(20000,this,[this]{
                    sample("profile-live-idle-30s");
                    QTimer::singleShot(30000,this,[this]{
                        sample("profile-live-idle-60s");
                        check(profile&&profile->findChildren<QObject*>().isEmpty(),"same profile remains alive through sixty-second observation");
                        destroyProfile();
                    });
                });
            } else destroyProfile();
        });
    }
    void destroyProfile(){
        profile->setUrlRequestInterceptor(nullptr);profile.reset();sample("profile-destroyed");
        QTimer::singleShot(10000,this,[this]{
            sample("profile-destroyed-idle-10s");check(local.blocked.load()==0,"all native controls stay local");
            qInfo().noquote()<<"RETENTION_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"kind",kind},{"rounds",rounds},{"sourceBytes",source.toUtf8().size()},{"checks",checks},{"failures",failures},{"externalRequests",local.blocked.load()},{"formulaTestsRun",kind.startsWith("retention-yield-editor")},{"explicitPoolIsDiagnosticOnly",kind=="retention-nested-pool"},{"samples",samples}}).toJson(QJsonDocument::Compact));finished(failures?11:0);
        });
    }
};
