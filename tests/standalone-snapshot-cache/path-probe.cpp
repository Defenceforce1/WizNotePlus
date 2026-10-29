#include "share/WizDocumentReadPermit.h"
#include <QCoreApplication>
#include <QTimer>
#include <QMap>
#include <QUrl>
#include <QMutex>
#include <QWaitCondition>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <functional>
#include <vector>
using WizEditorMode=int;
struct WIZDOCUMENTDATA{QString strGUID,strKbGUID;};
struct WizDocumentView:QObject{
    WIZDOCUMENTDATA current;WizDocumentCallbackEpoch access;quint64 opening=1;bool visible=false;
    auto noteReadTicket()const{return access.ticket();}quint64 noteOpenSequence()const{return opening;}
    const WIZDOCUMENTDATA& note()const{return current;}bool isVisible()const{return visible;}
};
struct Database{bool found=true;std::function<void()> duringLookup;bool documentFromGuid(const QString& guid,WIZDOCUMENTDATA& doc){doc={guid,"synthetic-kb"};const auto hook=duringLookup;if(hook)hook();return found;}};
struct Manager{Database database;Database& db(const QString&){return database;}};
struct ViewState:QObject{
    Manager& m_dbMgr;WizDocumentView host;QObject sourcePage;
    WizDocumentCallbackEpoch document;WizDocumentLoadSequence m_loadSequence;WIZDOCUMENTDATA m_loadDocument;
    WizDocumentReadPermit m_loadReadPermit;std::function<bool()> m_loadRequestCurrent;
    bool m_loadPending=false,readable=true,currentRequest=true,dispatchAllowed=true,revokeAtDispatch=false;
    QMap<QString,QString> m_mapFile;int loads=0,resumes=0;QString usedPath;
    explicit ViewState(Manager& m):m_dbMgr(m){}
    WizDocumentView* view(){return &host;}QObject* page(){return &sourcePage;}
    quint64 prepare(const QString& guid,const QString& kb="synthetic-kb"){
        document.invalidate();host.access.invalidate();++host.opening;host.current={guid,kb};m_loadDocument=host.current;m_loadPending=true;
        m_loadRequestCurrent=[this]{return currentRequest;};m_loadReadPermit={this,&sourcePage,document.ticket(),[this]{return readable;}};return m_loadSequence.next();
    }
    bool loadDocumentInWeb(int,WizDocumentCallbackEpoch::Ticket* navigation){++loads;usedPath=m_mapFile.value(host.current.strGUID);*navigation=document.ticket();if(revokeAtDispatch)host.access.invalidate();return dispatchAllowed;}
    bool bindDocumentLoadOrigin(const WIZDOCUMENTDATA&,const WizDocumentCallbackEpoch::Ticket&){return true;}
    bool resumePendingDocumentLoad(){++resumes;return true;}
};
struct BeforeView:ViewState{using ViewState::ViewState;bool acceptsLoadResult(const QString&,const QString&,QPointer<QObject>,quint64)const;bool takeLoadResult(const QString&,const QString&,QPointer<QObject>,quint64);void onDocumentReady(const QString,const QString,const QString,WizEditorMode,QPointer<QObject>,quint64);};
struct CurrentView:ViewState{using ViewState::ViewState;bool acceptsLoadResult(const QString&,const QString&,QPointer<QObject>,quint64)const;bool takeLoadResult(const QString&,const QString&,QPointer<QObject>,quint64);void onDocumentReady(const QString,const QString,const QString,WizEditorMode,QPointer<QObject>,quint64);};
#include "before-load.inc"
#include "current-load.inc"
struct SnapshotSaver:QObject{
    struct SAVEDATA{WIZDOCUMENTDATA doc;QString html,htmlFile;int flags=0;bool notify=false;QPointer<QObject> requester;};
    std::vector<SAVEDATA> m_arrayData;QMutex m_mutex;QWaitCondition m_waitEvent;bool m_savingForTransfer=false;
    bool isRunning()const{return true;}void start(){qFatal("worker/database must not start");}
    void save(const WIZDOCUMENTDATA&,const QString&,const QString&,int,bool);
    SAVEDATA peekFirst();
};
#include "saver-handoff.inc"
struct Memory{quint64 active=0,allocated=0,footprint=0,rss=0;bool complete=false;};
static Memory memory(){Memory r;vm_address_t* addresses=nullptr;unsigned count=0;r.complete=malloc_get_all_zones(mach_task_self(),nullptr,&addresses,&count)==KERN_SUCCESS&&count<=32;if(r.complete)for(unsigned i=0;i<count;++i){malloc_statistics_t s{};malloc_zone_statistics(reinterpret_cast<malloc_zone_t*>(addresses[i]),&s);r.active+=s.size_in_use;r.allocated+=s.size_allocated;}task_vm_info_data_t v{};mach_msg_type_number_t size=TASK_VM_INFO_COUNT;if(task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&v),&size)==KERN_SUCCESS){r.footprint=v.phys_footprint;r.rss=v.resident_size;}return r;}
static QJsonObject json(const Memory& r){return {{"mallocActiveBytes",qint64(r.active)},{"mallocAllocatedBytes",qint64(r.allocated)},{"footprintBytes",qint64(r.footprint)},{"rssBytes",qint64(r.rss)},{"zonesComplete",r.complete}};}
int main(int argc,char** argv){QCoreApplication app(argc,argv);int checks=0,failures=0,status=0;QJsonArray observations;bool delivered=false;const auto check=[&](bool v,const char* why){++checks;failures+=!v;qInfo().noquote()<<(v?"PASS":"FAIL")<<why;};
 QTimer::singleShot(0,&app,[&]{
    const auto many=[&](auto* pointer,bool fixed){auto& view=*pointer;const auto before=memory();qsizetype maxEntries=0;bool correct=true;for(int i=0;i<4096;++i){const QString guid=QString("synthetic-note-%1-abcdefghijklmnop").arg(i,8,16,QChar('0'));const QString file="/synthetic-only/"+guid+QString(180,QChar('p'))+"/index.html";const auto sequence=view.prepare(guid);view.onDocumentReady("synthetic-kb",guid,file,0,&view,sequence);maxEntries=qMax(maxEntries,view.m_mapFile.size());correct&=view.usedPath==file&&view.m_mapFile.value(guid)==file;}
      const auto after=memory();check(correct&&view.loads==4096,"every accepted synthetic note resolves its current path");check(view.m_mapFile.size()==(fixed?1:4096),"current-only index bound differs from historical accumulation");check(maxEntries==(fixed?1:4096),"index never exceeds the expected live-entry bound");check(before.complete&&after.complete,"self allocation counters complete");check(fixed?(after.active<before.active+128*1024):(after.active>before.active+1024*1024),"synthetic index active allocation stays bounded only in current method");observations.append(QJsonObject{{"variant",fixed?"current-only":"historical-accumulation"},{"notes",4096},{"maxEntries",maxEntries},{"entries",view.m_mapFile.size()},{"before",json(before)},{"after",json(after)},{"activeDeltaBytes",qint64(after.active)-qint64(before.active)}});
    };
    {Manager m;BeforeView v(m);many(&v,false);} {Manager m;CurrentView v(m);many(&v,true);}
    for(int kind=0;kind<9;++kind){Manager m;CurrentView v(m);const auto first=v.prepare("old");v.onDocumentReady("synthetic-kb","old","/synthetic-only/old.html",0,&v,first);const auto snapshot=v.m_mapFile;const auto current=v.prepare("new");QObject stranger;QPointer<QObject> requester=&v;quint64 sequence=current;QString guid="new",kb="synthetic-kb";
        if(kind==0)requester=&stranger;if(kind==1)requester=nullptr;if(kind==2)sequence=0;if(kind==3)sequence=first;if(kind==4)guid="wrong";if(kind==5)kb="wrong-kb";if(kind==6)v.m_loadPending=false;if(kind==7)v.currentRequest=false;if(kind==8)v.readable=false;
        const int loads=v.loads;v.onDocumentReady(kb,guid,"/synthetic-only/unaccepted.html",0,requester,sequence);check(v.m_mapFile==snapshot&&v.loads==loads,"stale/wrong/unreadable result cannot replace current path index");}
    {Manager m;CurrentView v(m);auto n=v.prepare("old");v.onDocumentReady("synthetic-kb","old","/synthetic-only/old.html",0,&v,n);const auto previous=v.m_mapFile;n=v.prepare("new");m.database.found=false;v.onDocumentReady("synthetic-kb","new","/synthetic-only/missing.html",0,&v,n);check(v.m_mapFile==previous&&v.loads==1,"database lookup failure leaves previous index untouched");}
    {Manager m;CurrentView v(m);auto n=v.prepare("old");v.onDocumentReady("synthetic-kb","old","/synthetic-only/old.html",0,&v,n);n=v.prepare("new");m.database.duringLookup=[&]{v.prepare("newer");v.m_mapFile={{"newer","/synthetic-only/newer.html"}};};v.onDocumentReady("synthetic-kb","new","/synthetic-only/new.html",0,&v,n);check(v.m_mapFile==QMap<QString,QString>{{"newer","/synthetic-only/newer.html"}}&&v.loads==1,"reentrant newer request keeps its map and rejects obsolete database result");}
    {Manager m;auto* v=new CurrentView(m);QPointer<CurrentView> weak=v;const auto n=v->prepare("destroyed");m.database.duringLookup=[v]{delete v;};v->onDocumentReady("synthetic-kb","destroyed","/synthetic-only/unused.html",0,v,n);check(!weak,"destroyed owner during lookup cannot be touched by path replacement");}
    {Manager m;CurrentView v(m);auto n=v.prepare("same");v.onDocumentReady("synthetic-kb","same","/synthetic-only/first.html",0,&v,n);n=v.prepare("same");v.onDocumentReady("synthetic-kb","same","/synthetic-only/reextracted.html",1,&v,n);check(v.m_mapFile.size()==1&&v.usedPath=="/synthetic-only/reextracted.html","same-note reload uses its newest extracted source path");v.onDocumentReady("synthetic-kb","same","/synthetic-only/duplicate.html",0,&v,n);check(v.m_mapFile.size()==1&&v.usedPath=="/synthetic-only/reextracted.html"&&v.loads==2,"duplicate consumed result cannot overwrite newest path");}
    {Manager m;CurrentView v(m);auto n=v.prepare("interrupted");v.dispatchAllowed=false;v.revokeAtDispatch=true;const auto before=v.m_loadSequence.current();v.onDocumentReady("synthetic-kb","interrupted","/synthetic-only/interrupted.html",0,&v,n);check(v.m_loadPending&&v.m_loadSequence.current()==before+1&&v.m_mapFile.value("interrupted")=="/synthetic-only/interrupted.html","authorization interruption preserves existing retry admission and current path");}
    {Manager m;CurrentView v(m);auto n=v.prepare("save-old");v.onDocumentReady("synthetic-kb","save-old","/synthetic-only/saved-old.html",0,&v,n);const auto doc=v.host.current;const QString path=v.m_mapFile.value(doc.strGUID);SnapshotSaver saver;saver.save(doc,"<p>complete pending synthetic body</p>",path,9,true);n=v.prepare("next");v.onDocumentReady("synthetic-kb","next","/synthetic-only/next.html",0,&v,n);check(path=="/synthetic-only/saved-old.html"&&!v.m_mapFile.contains("save-old"),"copied original path survives retirement of historical map entry");check(saver.m_arrayData.size()==1&&saver.m_arrayData[0].htmlFile==path&&saver.m_arrayData[0].doc.strGUID=="save-old"&&saver.m_arrayData[0].html=="<p>complete pending synthetic body</p>"&&saver.m_arrayData[0].flags==9&&saver.m_arrayData[0].notify,"actual production saver queue retains full prior task despite current-map replacement");
      saver.save(v.host.current,"<p>new note body</p>",v.m_mapFile.value("next"),3,false);
      const auto first=saver.peekFirst();check(first.doc.strGUID=="save-old"&&first.htmlFile==path&&saver.m_arrayData.size()==1,"actual saver dequeue keeps FIFO order and the previous task path");
      const auto second=saver.peekFirst();check(second.doc.strGUID=="next"&&second.htmlFile=="/synthetic-only/next.html"&&second.html=="<p>new note body</p>","actual saver dequeue keeps the newer task independent");
      check(saver.m_arrayData.empty()&&saver.m_savingForTransfer,"consumed queue has no queued payload while in-flight ownership remains explicit");
      QMetaObject::invokeMethod(&app,[&,path]{check(path=="/synthetic-only/saved-old.html","real queued callback keeps independently captured old path after view scope ends");delivered=true;},Qt::QueuedConnection);
    }
    QTimer::singleShot(0,&app,[&]{check(delivered,"queued old-path consumer completed once");const QJsonObject report{{"checks",checks},{"failures",failures},{"observations",observations},{"qtVersion",qVersion()},{"usesActualLoadAdmissionAndReadyMethods",true},{"usesActualSaverHandoff",true},{"pathValuesSyntheticOnly",true},{"normalAccountOrNotesAccessed",false},{"allocatorIntervention",false},{"fullClientAccepted",false}};qInfo().noquote()<<"PATH_CACHE_REPORT"<<QJsonDocument(report).toJson(QJsonDocument::Compact);status=failures?1:0;app.quit();});
 });QTimer::singleShot(15000,&app,[&]{status=2;qCritical()<<"path probe deadline";app.quit();});app.exec();return status;}
