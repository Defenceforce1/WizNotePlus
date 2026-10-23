// A genuinely asynchronous UI sequence. Unlike one(), no load/render/edit/close
// sequence is contained in an outer callback with nested wait()/js() loops.
// Same source, profile and checks; no forced collection, cache purge or pool.
class StagedNativeRetention final:public QObject {
    struct EditorContext {QWebChannel channel;Bridge editor,explorer;};
    NativeRetention data;
    std::unique_ptr<EditorContext> context;
    std::unique_ptr<QWebEngineView> view;
    QPointer<QWebEnginePage> page;
    QString current;
    QMetaObject::Connection loadedConnection;
    bool stopped=false;
    void later(int ms,std::function<void()> next){
        QTimer::singleShot(ms,this,[this,next=std::move(next)]{if(!stopped)next();});
    }
    void fail(const char* message){
        if(stopped)return;
        data.check(false,message);stopped=true;
        QObject::disconnect(loadedConnection);view.reset();context.reset();
        data.profile->setUrlRequestInterceptor(nullptr);data.profile.reset();
        data.finished(11);
    }
    void read(const QString& script,std::function<void(const QVariant&)> next){
        if(!page){fail("staged page exists for asynchronous read");return;}
        page->runJavaScript(script,[this,next=std::move(next)](const QVariant& value){
            if(stopped)return;
            if(!page){fail("staged callback belongs to a live page");return;}
            next(value);
        });
    }
    void poll(const QString& script,QDeadlineTimer deadline,std::function<void()> ready){
        read(script,[this,script,deadline,ready=std::move(ready)](const QVariant& value){
            if(value.toBool()){later(0,ready);return;}
            if(deadline.hasExpired()){fail("asynchronous stage becomes ready before deadline");return;}
            later(50,[this,script,deadline,ready]{poll(script,deadline,ready);});
        });
    }
    void initialize(){
        QString setup="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';";
        setup+=WizDocumentPageReady::initializeEditorScript(data.resources+"wizeditor/","zh-cn","synthetic-opening-user","Synthetic",false,"markdown");
        setup+="WizPrepareNoteModeChange();await new Promise(resolve=>WizEditor.off(null,resolve));WizApplyNoteStyle();";
        page->runJavaScript(WizDocumentPageReady::setupScript(literal("retention-editor"),setup));
        poll("window.WizNativeEditorSetup&&WizNativeEditorSetup.state==='ready'",QDeadlineTimer(45000),[this]{
            data.check(true,"persistent-profile editor and actual formula engine become ready");
            data.sample("reader-ready");
            read("WizEditor.getMarkdownSrc({unEscapeHtml:true})",[this](const QVariant& value){
                data.check(value.toString()==current,"editor source remains exact before close");
                later(0,[this]{read("MathJax.Hub.getAllJax().length===130&&document.querySelectorAll('.MathJax_Error,merror').length===0&&WizMarkdownEnvironmentLoader===null",[this](const QVariant& value){
                    data.check(value.toBool(),"128 formulas and two references settle without outstanding owned loader");
                    later(0,[this]{edit();});
                });});
            });
        });
    }
    void edit(){
        page->runJavaScript("window.retentionEditDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>retentionEditDone=true);");
        poll("retentionEditDone&&document.body.isContentEditable",QDeadlineTimer(35000),[this]{
            read("WizEditor.getMarkdownSrc({unEscapeHtml:true})",[this](const QVariant& value){
                data.check(value.toString()==current,"persistent-profile edit restores exact source before page teardown");
                data.sample("editor-ready");later(0,[this]{close();});
            });
        });
    }
    void close(){
        bool metal=true;QList<QPointer<QQuickWindow>> quickWindows;
        for(auto* widget:view->findChildren<QQuickWidget*>()){
            quickWindows.append(widget->quickWindow());metal&=int(widget->quickWindow()->rendererInterface()->graphicsApi())==6;
        }
        data.check(metal&&!quickWindows.isEmpty()&&(!view->windowHandle()||!view->windowHandle()->isExposed()),"hidden native Metal window remains unexposed without pixel grabs");
        QPointer<QWebEnginePage> weakPage=page;QPointer<QWebEngineView> weakView=view.get();
        QObject::disconnect(loadedConnection);view.reset();context.reset();current.clear();
        later(300,[this,weakPage,weakView,quickWindows]{
            bool destroyed=weakPage.isNull()&&weakView.isNull();for(auto window:quickWindows)destroyed&=window.isNull();
            data.check(destroyed,"page, view and Quick windows destroyed");
            data.sample("closed");++data.round;
            if(data.round==data.rounds)data.atBoundary();else later(50,[this]{beginRound();});
        });
    }
    void beginRound(){
        data.sample("before-round");current=data.source+QString("\nSynthetic document instance %1.\n").arg(data.round);
        context=std::make_unique<EditorContext>();
        QString html="<!doctype html><html><head><meta charset='utf-8'></head><body>"+current.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
        context->editor.original=html;QString tags;
        for(const auto& f:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})
            tags+="<script src=\""+QUrl::fromLocalFile(data.resources+f).toString().toHtmlEscaped()+"\" name='wiz_inner_script' wiz_style='unsave'></script>";
        html.insert(html.indexOf("</head>"),WizReaderOpeningPresentation::bootstrapHtml()+tags);
        context->channel.registerObject("WizQtEditor",&context->editor);context->channel.registerObject("WizExplorerApp",&context->explorer);
        QFile file(data.root.filePath("native-retention.html"));if(!file.open(QFile::WriteOnly)){fail("synthetic staged file opens");return;}file.write(html.toUtf8());file.close();
        view=std::make_unique<QWebEngineView>();view->setAttribute(Qt::WA_DontShowOnScreen);view->resize(900,600);
        page=new QWebEnginePage(data.profile.get(),view.get());view->setPage(page);page->setWebChannel(&context->channel);
        loadedConnection=QObject::connect(view.get(),&QWebEngineView::loadFinished,this,[this](bool ok){
            QObject::disconnect(loadedConnection);if(!ok){fail("native-only synthetic staged document loads");return;}
            data.check(true,"native-only synthetic document loads");later(0,[this]{initialize();});
        });
        const int generation=data.round;
        QTimer::singleShot(35000,this,[this,generation]{if(!stopped&&view&&data.round==generation&&!page->url().isLocalFile())fail("staged local page load timeout");});
        view->show();view->load(QUrl::fromLocalFile(file.fileName()));
    }
public:
    StagedNativeRetention(QTemporaryDir& root,std::function<void(int)> finished,const QString& resources)
        :data(root,"retention-yield-editor-staged",std::move(finished),resources){}
    void start(){data.sample("baseline");later(0,[this]{beginRound();});}
};
