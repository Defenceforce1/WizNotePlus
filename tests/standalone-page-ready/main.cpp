#include "share/WizDocumentPageReady.h"
#include "reference.h"
#include "share/WizDocumentAsync.h"
#include <QApplication>
#include <QWebEngineProfile>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineView>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QEventLoop>
#include <QTimer>
#include <QDebug>
#include <memory>
#include <atomic>
QVariant evaluate(QWebEnginePage& page,const QString& script){
 struct Result{bool done=false;QVariant value;};auto r=std::make_shared<Result>();QEventLoop loop;QTimer deadline;
 QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);deadline.setSingleShot(true);deadline.start(10000);
 page.runJavaScript(script,[r,&loop](const QVariant& x){r->value=x;r->done=true;loop.quit();});if(!r->done)loop.exec();return r->value;
}
class LocalOnly:public QWebEngineUrlRequestInterceptor{public:std::atomic<int> blocked{0};void interceptRequest(QWebEngineUrlRequestInfo& x)override{if(!QStringList{"file","about","qrc","data","blob"}.contains(x.requestUrl().scheme())){blocked++;x.block(true);}}};
class Probe:public QObject{
public:
 QApplication& app;QString output;LocalOnly local;QWebEngineProfile profile;std::unique_ptr<QWebEnginePage> source;
 QMetaObject::Connection initialLoad;QJsonArray rows;int index=0,checks=0,failures=0;QVector<bool> variants;
 Probe(QApplication& a,const QString& folder):app(a),output(folder){profile.setCachePath(folder+"/cache");profile.setPersistentStoragePath(folder+"/profile");profile.setUrlRequestInterceptor(&local);source=std::make_unique<QWebEnginePage>(&profile);
 for(int i=0;i<5;i++)for(bool x:{false,true,true,false})variants.push_back(x);for(bool x:{false,true,true,false})variants.push_back(x);}
 void check(bool ok,const char* label){checks++;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;}
 void start(){initialLoad=QObject::connect(source.get(),&QWebEnginePage::loadFinished,this,[this](bool ok){QObject::disconnect(initialLoad);check(ok,"local scalar readiness page loads");qInfo().noquote()<<"READINESS328_ID "<<QJsonDocument(QJsonObject{{"pid",QCoreApplication::applicationPid()},{"rendererPid",source->renderProcessPid()}}).toJson(QJsonDocument::Compact);QTimer::singleShot(0,this,[this]{next();});});source->setHtml("<!doctype html><html><head><meta charset='utf-8'></head><body><p>Synthetic readiness</p></body></html>");}
 void next(){if(index>=variants.size()){boundary();return;}
 const int current=index++;const bool candidate=variants[current],pending=current>=20;
 source->runJavaScript("window.queryCount=0;window.readyNow=false",[this,current,candidate,pending](const QVariant&){
  struct Sample{QElapsedTimer timer;bool completed=false;};auto sample=std::make_shared<Sample>();sample->timer.start();
  if(pending)QTimer::singleShot(80,source.get(),[this]{source->runJavaScript("window.readyNow=true");});
  const QString expression=pending?"(()=>{++window.queryCount;return window.readyNow?'ready':'pending';})()":"(()=>{++window.queryCount;return 'ready';})()";
  const auto callback=[this,current,candidate,pending,sample](bool ok){
   const double ms=sample->timer.nsecsElapsed()/1000000.0;check(ok&&!sample->completed,"current readiness completes once");sample->completed=true;
   source->runJavaScript("window.queryCount",[this,current,candidate,pending,ms](const QVariant& value){rows.append(QJsonObject{{"sequence",current},{"candidate",candidate},{"pending",pending},{"completionMs",ms},{"queries",value.toInt()}});QTimer::singleShot(0,this,[this]{next();});});
  };
  auto watch=candidate?WizDocumentPageReady::watch:WizPageReadyReference::watch;watch(this,source.get(),expression,[]{return true;},callback,1000);
 });}
 void boundary(){auto& page=*source;auto check=[this](bool ok,const char* label){this->check(ok,label);};
    {
        const auto waitFor=[&](const std::function<bool()>& predicate,int timeout=1500){
            QEventLoop loop;QTimer poll,deadline;
            QObject::connect(&poll,&QTimer::timeout,[&]{if(predicate())loop.quit();});
            QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
            poll.start(5);deadline.setSingleShot(true);deadline.start(timeout);loop.exec();
            return predicate();
        };
        struct Completion { int count=0;bool ready=false; };
        auto completion=std::make_shared<Completion>();
        QObject owner;
        // Release a real JS Promise from a GUI timer, without relying on
        // background-page JavaScript timer scheduling in an invisible fixture.
        page.runJavaScript(WizDocumentPageReady::setupScript("'synthetic-token'",
            "await new Promise(resolve=>window.completeSetup=resolve);"));
        QTimer::singleShot(150,&page,[&page]{page.runJavaScript("window.completeSetup()");});
        WizDocumentPageReady::watch(&owner,&page,WizDocumentPageReady::stateExpression("'synthetic-token'"),[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},1000);
        check(completion->count==0,"asynchronous editor setup does not notify plugins before readiness");
        check(waitFor([completion]{return completion->count>0;})&&completion->ready&&completion->count==1,
            "actual delayed JS setup completes exactly once after readiness");
        page.runJavaScript(WizDocumentPageReady::setupScript("'failed-token'","throw Error('synthetic setup error');"));
        completion=std::make_shared<Completion>();
        WizDocumentPageReady::watch(&owner,&page,WizDocumentPageReady::stateExpression("'failed-token'"),[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},1000);
        check(waitFor([completion]{return completion->count>0;})&&!completion->ready&&completion->count==1,
            "explicit editor initialization failure reports failure once");
        completion=std::make_shared<Completion>();
        WizDocumentPageReady::watch(&owner,&page,"'pending'",[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},100);
        check(waitFor([completion]{return completion->count>0;})&&!completion->ready,
            "unresolved initialization has a bounded timeout");
        completion=std::make_shared<Completion>();
        WizDocumentCallbackEpoch navigation;const auto ticket=navigation.ticket();
        WizDocumentPageReady::watch(&owner,&page,"'pending'",[ticket]{return WizDocumentCallbackEpoch::current(ticket);},
            [completion](bool ready){++completion->count;completion->ready=ready;},100);
        navigation.invalidate();
        waitFor([]{return false;},200);
        check(completion->count==0,"navigation cancellation suppresses old-page readiness notifications");
        completion=std::make_shared<Completion>();
        auto closingOwner=std::make_unique<QObject>();
        WizDocumentPageReady::watch(closingOwner.get(),&page,"'pending'",[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},100);
        const QPointer<QTimer> pendingTimer=closingOwner->findChild<QTimer*>();closingOwner.reset();
        waitFor([]{return false;},150);
        check(pendingTimer.isNull()&&completion->count==0,"owner destruction releases the setup timer without late notification");
        completion=std::make_shared<Completion>();
        auto closingPage=std::make_unique<QWebEnginePage>(&profile);
        WizDocumentPageReady::watch(&owner,closingPage.get(),"'pending'",[]{return true;},
            [completion](bool ready){++completion->count;completion->ready=ready;},1000);
        closingPage.reset();
        check(waitFor([completion]{return completion->count>0;})&&!completion->ready&&completion->count==1,
            "destroyed renderer page ends the readiness wait without calling its methods");
        // Phase152: an owner can disappear during the final permission check.
        auto finalOwner=std::make_unique<QObject>();bool retired=false;int lateCalls=0;
        WizDocumentPageReady::watch(finalOwner.get(),&page,"'ready'",[&]{
            auto* timer=finalOwner?finalOwner->findChild<QTimer*>():nullptr;
            if(timer&&!timer->isActive()){retired=true;finalOwner.reset();}
            return true;
        },[&](bool){++lateCalls;},1000);
        waitFor([&]{return retired||lateCalls>0;});
        check(retired&&!finalOwner&&lateCalls==0,"readiness completion rechecks its weak owner after the final permission predicate destroys it");
        auto entryOwner=std::make_unique<QObject>();int entryCalls=0;
        WizDocumentPageReady::watch(entryOwner.get(),&page,"'ready'",[&]{entryOwner.reset();return true;},[&](bool){++entryCalls;});
        check(!entryOwner&&entryCalls==0,"native readiness entry survives owner destruction inside its initial predicate");
        QObject entryContext;auto entryPage=std::make_unique<QWebEnginePage>(&profile);
        WizDocumentPageReady::watch(&entryContext,entryPage.get(),"'ready'",[&]{entryPage.reset();return true;},[&](bool){++entryCalls;});
        check(!entryPage&&entryContext.findChildren<QTimer*>().empty()&&entryCalls==0,"native readiness entry refuses a page destroyed by its initial predicate");
        WizDocumentPageReady::watch(&entryContext,&page,"'ready'",{},[&](bool){++entryCalls;});
        WizDocumentPageReady::watch(&entryContext,&page,"'ready'",[]{return true;},{});
        check(entryContext.findChildren<QTimer*>().empty()&&entryCalls==0,"empty readiness callbacks do not allocate a timer or start native queries");
        auto deliveredOwner=std::make_unique<QObject>();int delivered=0;
        WizDocumentPageReady::watch(deliveredOwner.get(),&page,"'ready'",[]{return true;},[&](bool ready){if(ready){++delivered;deliveredOwner.reset();}},1000);
        check(waitFor([&]{return delivered>0;})&&delivered==1&&!deliveredOwner,"valid native readiness delivery may destroy its owner without another notification");
        // End Phase152 readiness tests.
        // Phase156: observe the same live setup token after authorization changes.
        {
            QObject resumedOwner;WizDocumentCallbackEpoch authorization;
            const auto first=authorization.ticket();int oldDelivery=0,newDelivery=0;bool readyAgain=false;
            const QString originalBody=evaluate(page,"document.body.innerHTML").toString();
            evaluate(page,"window.resumedSetupRuns=0;");
            evaluate(page,WizDocumentPageReady::setupScript("'resumed-once'","++window.resumedSetupRuns;await new Promise(resolve=>window.finishResumedSetup=resolve);"));
            WizDocumentPageReady::watch(&resumedOwner,&page,WizDocumentPageReady::stateExpression("'resumed-once'"),[first]{return WizDocumentCallbackEpoch::current(first);},[&](bool){++oldDelivery;},1000);
            authorization.invalidate();const auto second=authorization.ticket();
            evaluate(page,"document.body.innerHTML='<p>unsaved synthetic editor DOM</p>';window.finishResumedSetup();");
            WizDocumentPageReady::watch(&resumedOwner,&page,WizDocumentPageReady::stateExpression("'resumed-once'"),[second]{return WizDocumentCallbackEpoch::current(second);},[&](bool ready){++newDelivery;readyAgain=ready;},1000);
            check(waitFor([&]{return newDelivery>0;})&&readyAgain&&newDelivery==1&&oldDelivery==0,"native fresh authorization can observe original delayed setup readiness while the revoked observer stays silent");
            check(evaluate(page,"resumedSetupRuns").toInt()==1,"native readiness resumption does not replay the submitted asynchronous initializer");
            check(evaluate(page,"document.body.innerHTML").toString()=="<p>unsaved synthetic editor DOM</p>","native resumed observation preserves renderer-owned edited DOM");
            evaluate(page,"document.body.innerHTML="+QString::fromUtf8(QJsonDocument(QJsonArray{originalBody}).toJson(QJsonDocument::Compact))+"[0];");
        }
        // End Phase156 native readiness tests.
        // Phase157: native local-file proof and relative resources with navigation query.
        {
            QTemporaryDir local;const QString file=local.filePath("公式 page.html"),css=local.filePath("note.css");
            QFile style(css);style.open(QIODevice::WriteOnly);style.write("body { color: rgb(1, 2, 3); }");style.close();
            QFile html(file);html.open(QIODevice::WriteOnly);const QByteArray original="<html><head><link rel='stylesheet' href='note.css'></head><body><a href='#eq1'>reference</a><p id='eq1'>equation</p></body></html>";html.write(original);html.close();
            const QUrl first=WizDocumentPageReady::navigationUrl(QUrl::fromLocalFile(file)),second=WizDocumentPageReady::navigationUrl(QUrl::fromLocalFile(file));
            check(first!=second&&first.toLocalFile()==file&&second.toLocalFile()==file,"native repeated local-file loads have distinct query identities without changing the source path");
            bool loaded=false;const auto connection=QObject::connect(&page,&QWebEnginePage::loadFinished,&page,[&](bool ok){loaded=ok;});page.load(first);
            check(waitFor([&]{return loaded;})&&page.url()==first,"native local HTML with a query marker loads using the exact requested navigation identity");
            check(evaluate(page,WizDocumentPageReady::documentExpression(first)).toString()=="ready"&&evaluate(page,WizDocumentPageReady::documentExpression(second)).toString()=="failed","native renderer proof accepts current document and rejects the same filename with another nonce");
            check(evaluate(page,"getComputedStyle(document.body).color").toString()=="rgb(1, 2, 3)","native navigation marker preserves relative local CSS resolution");
            evaluate(page,"document.querySelector('a').click()");
            check(waitFor([&]{return page.url().fragment()=="eq1";})&&evaluate(page,WizDocumentPageReady::documentExpression(first)).toString()=="ready","native equation anchor navigation keeps the original document proof valid");
            int reloadStarts=0;QUrl requested;const auto started=QObject::connect(&page,&QWebEnginePage::loadStarted,&page,[&]{++reloadStarts;requested=page.requestedUrl();});loaded=false;page.triggerAction(QWebEnginePage::Reload);
            check(waitFor([&]{return loaded&&reloadStarts>0;})&&requested.adjusted(QUrl::RemoveFragment)==first&&evaluate(page,WizDocumentPageReady::documentExpression(first)).toString()=="ready","native same-URL reload start exposes the current requested identity and obtains a new complete renderer proof");QObject::disconnect(started);
            html.open(QIODevice::ReadOnly);const QByteArray unchanged=html.readAll();html.close();
            check(unchanged==original,"native navigation proof never rewrites original HTML bytes");
            QObject::disconnect(connection);bool blank=false;const auto restore=QObject::connect(&page,&QWebEnginePage::loadFinished,&page,[&](bool ok){blank=ok;});page.setHtml("<html><body>synthetic restored page</body></html>");waitFor([&]{return blank;});QObject::disconnect(restore);
        }
        // End Phase157 native page proof tests.
    }
 { QObject zeroOwner;int zeroCalls=0;bool value=true;
   WizDocumentPageReady::watch(&zeroOwner,&page,"'pending'",[]{return true;},[&](bool ready){zeroCalls++;value=ready;},0);
   check(zeroCalls==0,"zero-timeout completion remains asynchronous");QEventLoop spin;QTimer::singleShot(60,&spin,&QEventLoop::quit);spin.exec();check(zeroCalls==1&&!value,"zero timeout reports one bounded failure");
 }
 check(local.blocked.load()==0,"network remains blocked and unused");
 QFile f(output+"/result.json");if(!f.open(QIODevice::WriteOnly|QIODevice::NewOnly)){app.exit(4);return;}
 f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"rows",rows},{"externalRequests",local.blocked.load()},{"qtVersion",qVersion()},{"boundaryOnlyNotFirstVisiblePaint",true},{"topLevelLoopDuringMeasurement",true}}).toJson(QJsonDocument::Indented));f.close();profile.setUrlRequestInterceptor(nullptr);source.reset();app.exit(failures?1:0);
 }
};
int main(int argc,char**argv){if(argc!=2)return 2;QCoreApplication::setAttribute(Qt::AA_PluginApplication);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QCoreApplication::setApplicationName("WizNotePlus Synthetic Readiness328");
 QTemporaryDir root(QString::fromLocal8Bit(argv[1])+"/scratch-XXXXXX");if(!root.isValid())return 3;
 Probe probe(app,QString::fromLocal8Bit(argv[1]));QTimer::singleShot(0,&probe,[&]{probe.start();});QTimer::singleShot(30000,&app,[&]{qWarning()<<"OWN_READINESS_DEADLINE";app.exit(5);});return app.exec();}
