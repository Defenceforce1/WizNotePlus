// Native-only controls. Deliberately compare an artificial nested batch with
// real top-level event turns, keeping the SAME profile alive at the boundary.
#include <QWebEngineProfileBuilder>
extern int wizNativeMemoryBatchPool(const std::function<int()>&);
class NativeRetention final:public QObject {
public:
    QTemporaryDir& root;QString kind,source;std::unique_ptr<QWebEngineProfile> profile;
    LocalOnly local;QJsonArray samples;int round=0,checks=0,failures=0;const int rounds;
    std::function<void(int)> finished;
    NativeRetention(QTemporaryDir& root,const QString& kind,std::function<void(int)> done):root(root),kind(kind),rounds(kind=="retention-yield-persistent"?16:8),finished(std::move(done)){
        if(kind=="retention-yield-persistent"){
            QWebEngineProfileBuilder builder;builder.setPersistentStoragePath(root.filePath("profile")).setCachePath(root.filePath("cache"));
            profile.reset(builder.createProfile(QStringLiteral("SyntheticNativeRetention")));
            if(!profile)qFatal("Cannot construct synthetic isolated profile");
        }else profile=std::make_unique<QWebEngineProfile>();
        profile->setCachePath(root.filePath("cache"));profile->setPersistentStoragePath(root.filePath("profile"));profile->setUrlRequestInterceptor(&local);
        source="# Synthetic large document\n\n";
        for(int i=0;i<6000;++i)source+=QString("Paragraph %1: Synthetic academic prose with **emphasis**, a local list of concepts, and punctuation. 合成正文，仅用于离线大文档测试；不来自任何真实笔记。\n\n").arg(i);
    }
    void check(bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;}
    void sample(const QString& phase){
        QJsonObject s{{"phase",phase},{"round",round},{"profileLive",bool(profile)},{"profileGeneration",1},{"offTheRecord",profile?QJsonValue(profile->isOffTheRecord()):QJsonValue(QJsonValue::Null)},{"storageIsTemporary",profile?QJsonValue(profile->persistentStoragePath()==root.filePath("profile")&&profile->cachePath()==root.filePath("cache")):QJsonValue(QJsonValue::Null)},{"profileObjects",profile?profile->findChildren<QObject*>().size():0},{"memory",stressMemory()}};
        samples.append(s);qInfo().noquote()<<"RETENTION_SAMPLE"<<QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact));
    }
    void one(){
        const QString current=source+QString("\nSynthetic document instance %1.\n").arg(round);
        const QString html="<!doctype html><html><head><meta charset='utf-8'></head><body>"+current.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
        QFile file(root.filePath("native-retention.html"));if(!file.open(QFile::WriteOnly)){check(false,"synthetic file opens");return;}file.write(html.toUtf8());file.close();
        auto view=std::make_unique<QWebEngineView>();view->setAttribute(Qt::WA_DontShowOnScreen);view->resize(900,600);auto* page=new QWebEnginePage(profile.get(),view.get());view->setPage(page);
        bool loaded=false,loadOk=false;QObject::connect(view.get(),&QWebEngineView::loadFinished,[&](bool ok){loaded=true;loadOk=ok;});view->show();view->load(QUrl::fromLocalFile(file.fileName()));
        check(wait([&]{return loaded;},35000)&&loadOk,"native-only synthetic document loads");
        check(js(page,"document.body.textContent").toString()==QString(current).remove('\n'),"complete static source retained");
        check(js(page,"document.scripts.length===0&&typeof WizEditor==='undefined'&&typeof MathJax==='undefined'").toBool()&&page->webChannel()==nullptr,"no editor, math engine, scripts or channel");
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
            profile->setUrlRequestInterceptor(nullptr);profile.reset();sample("profile-destroyed");
            QTimer::singleShot(10000,this,[this]{
                sample("profile-destroyed-idle-10s");check(local.blocked.load()==0,"all native controls stay local");
                qInfo().noquote()<<"RETENTION_REPORT"<<QString::fromUtf8(QJsonDocument(QJsonObject{{"schema",1},{"kind",kind},{"rounds",rounds},{"sourceBytes",source.toUtf8().size()},{"checks",checks},{"failures",failures},{"externalRequests",local.blocked.load()},{"formulaTestsRun",false},{"explicitPoolIsDiagnosticOnly",kind=="retention-nested-pool"},{"samples",samples}}).toJson(QJsonDocument::Compact));finished(failures?11:0);
            });
        });
    }
};
