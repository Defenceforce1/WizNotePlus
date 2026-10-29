#include "share/WizDocumentAsync.h"
#include "share/WizDocumentReadPermit.h"
#include <QCoreApplication>
#include <QTimer>
#include <QUrl>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QMutex>
#include <QWaitCondition>
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <array>
#include <functional>
#include <type_traits>
#include <vector>

struct Document {QString strGUID,strKbGUID;};
using WIZDOCUMENTDATA=Document;
struct SnapshotSaver:QObject {
    struct SAVEDATA {Document doc;QString html,htmlFile;int flags=0;bool notify=false;QPointer<QObject> requester;};
    std::vector<SAVEDATA> m_arrayData;QMutex m_mutex;QWaitCondition m_waitEvent;
    bool isRunning()const{return true;}void start(){qFatal("No worker or database may start in this ownership test");}
    void save(const Document&,const QString&,const QString&,int,bool);
};
#include "saver-handoff.inc"
struct OwnerState : QObject {
    WizDocumentCallbackEpoch m_editorPageCallbacks,m_documentCallbacks;
    std::function<void()> m_editorPageResume=[]{},m_editorPageRestart=[]{};
    bool m_loadPending=true;
    std::function<bool()> m_loadRequestCurrent=[]{return true;},m_loadOriginCurrent=[]{return true;};
    WizDocumentReadPermit m_loadReadPermit;
    WizDocumentLoadSequence m_loadSequence;
    QString m_currentNoteHtml;Document m_currentNoteHtmlDocument;
    bool m_bContentsChanged=true;QString unsavedDom="unsubmitted synthetic DOM";
    std::function<void()> duringInvalidate;
    int loads=0;QUrl lastUrl;bool emptyAtDispatch=false;
    WizDocumentCallbackEpoch::Ticket invalidateDocumentCallbacks(){
        m_documentCallbacks.invalidate();auto ticket=m_documentCallbacks.ticket();const auto callback=duringInvalidate;if(callback)callback();return ticket;
    }
    void load(const QUrl& url){++loads;lastUrl=url;emptyAtDispatch=m_currentNoteHtml.isEmpty()&&m_currentNoteHtmlDocument.strGUID.isEmpty()&&m_currentNoteHtmlDocument.strKbGUID.isEmpty();}
};
struct BeforeView:OwnerState{void clear();};
struct CurrentView:OwnerState{void clear();};
#include "before-clear.inc"
#include "current-clear.inc"
struct Memory {quint64 inUse=0,allocated=0,footprint=0,rss=0;bool complete=false;};
static Memory memory(){
    Memory r;vm_address_t* zones=nullptr;unsigned count=0;r.complete=malloc_get_all_zones(mach_task_self(),nullptr,&zones,&count)==KERN_SUCCESS&&count<=32;
    if(r.complete)for(unsigned i=0;i<count;++i){malloc_statistics_t s{};malloc_zone_statistics(reinterpret_cast<malloc_zone_t*>(zones[i]),&s);r.inUse+=s.size_in_use;r.allocated+=s.size_allocated;}
    task_vm_info_data_t vm{};mach_msg_type_number_t size=TASK_VM_INFO_COUNT;if(task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&vm),&size)==KERN_SUCCESS){r.footprint=vm.phys_footprint;r.rss=vm.resident_size;}return r;
}
static QJsonObject json(const Memory& r){return {{"mallocActiveBytes",qint64(r.inUse)},{"mallocAllocatedBytes",qint64(r.allocated)},{"footprintBytes",qint64(r.footprint)},{"rssBytes",qint64(r.rss)},{"zoneEnumerationComplete",r.complete}};}
static quint32 digest(const QString& s){quint32 h=2166136261u;for(const auto c:s)h=(h^c.unicode())*16777619u;return h;}
static void seed(OwnerState& owner,int characters=8*1024*1024){owner.m_currentNoteHtml=QString(characters,QChar('A'));owner.m_currentNoteHtml[1]=QChar(0x4e2d);owner.m_currentNoteHtmlDocument={"old-note","synthetic-kb"};}
int main(int argc,char** argv){
    QCoreApplication app(argc,argv);int checks=0,failures=0;QJsonArray observations;
    const auto check=[&](bool ok,const char* why){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<why;};
    int status=0;QTimer::singleShot(0,&app,[&]{
        // Both variants execute the exact extracted production method bodies.
        const auto retained=[&](auto* pointer,const char* name,bool fixed){
            auto& v=*pointer;seed(v);const auto doc=v.m_documentCallbacks.ticket(),page=v.m_editorPageCallbacks.ticket();const auto sequence=v.m_loadSequence.current();const auto before=memory();v.clear();const auto after=memory();
            check(before.complete&&after.complete,"self malloc-zone enumeration complete");
            check(v.loads==1&&v.lastUrl==QUrl("about:blank"),"explicit blank navigation dispatched once");
            check(fixed?v.emptyAtDispatch:!v.emptyAtDispatch,"cache lifetime matches expected before/current control");
            check(!v.m_loadPending&&!v.m_loadRequestCurrent&&!v.m_loadOriginCurrent&&!v.m_editorPageResume&&!v.m_editorPageRestart&&v.m_loadSequence.current()==sequence+1,"existing pending loader and resume retirement preserved");
            check(!WizDocumentCallbackEpoch::current(doc)&&!WizDocumentCallbackEpoch::current(page),"late document/page callbacks invalidated before blank navigation");
            check(v.m_bContentsChanged&&v.unsavedDom=="unsubmitted synthetic DOM","cache retirement does not cancel dirty state or mutate unsaved DOM in the caller");
            check(fixed?(before.inUse>=after.inUse+15*1024*1024):(v.m_currentNoteHtml.size()==8*1024*1024&&after.inUse+1024*1024>=before.inUse),"sole snapshot active allocation released only in current implementation");
            observations.append(QJsonObject{{"variant",name},{"snapshotUtf16Bytes",16*1024*1024},{"before",json(before)},{"after",json(after)},{"activeBytesReleased",qint64(before.inUse)-qint64(after.inUse)}});
        };
        {BeforeView v;retained(&v,"historical-clear",false);}
        {CurrentView v;retained(&v,"current-clear",true);v.clear();check(v.loads==2&&v.m_currentNoteHtml.isEmpty(),"repeated empty-page retirement is idempotent");}
        {
            CurrentView v;seed(v,128);const QString original=v.m_currentNoteHtml;const auto originalDoc=v.m_currentNoteHtmlDocument;
            const auto page=v.m_editorPageCallbacks.ticket();const auto old=v.m_documentCallbacks.ticket();
            v.duringInvalidate=[&]{v.m_documentCallbacks.invalidate();v.m_currentNoteHtml="newly opened authoritative snapshot";v.m_currentNoteHtmlDocument={"new-note","synthetic-kb"};};
            v.clear();check(v.loads==0&&v.m_currentNoteHtml=="newly opened authoritative snapshot"&&v.m_currentNoteHtmlDocument.strGUID=="new-note","reentrant newer document retains cache and suppresses obsolete blank dispatch");
            check(!WizDocumentCallbackEpoch::current(page)&&!WizDocumentCallbackEpoch::current(old),"obsolete callback tickets stay retired during reentrant navigation");
            (void)original;(void)originalDoc;
        }
        {
            auto* v=new CurrentView;QPointer<CurrentView> weak(v);v->duringInvalidate=[v]{delete v;};v->clear();check(!weak,"owner destroyed in cancellation cannot be touched by snapshot cleanup");
        }
        {
            CurrentView v;seed(v,1024);const auto before=v.m_currentNoteHtml;v.invalidateDocumentCallbacks();check(v.m_currentNoteHtml==before&&v.m_currentNoteHtmlDocument.strGUID=="old-note"&&v.loads==0,"ordinary callback invalidation alone leaves the source cache intact");
        }
        {
            CurrentView v;seed(v,4096);SnapshotSaver saver;const auto expected=digest(v.m_currentNoteHtml);
            saver.save(v.m_currentNoteHtmlDocument,v.m_currentNoteHtml,QStringLiteral("synthetic-index.html"),7,true);v.clear();
            check(saver.m_arrayData.size()==1&&saver.m_arrayData[0].html.size()==4096&&digest(saver.m_arrayData[0].html)==expected,"actual production saver handoff independently owns complete snapshot after cache retirement");
            check(saver.m_arrayData[0].doc.strGUID=="old-note"&&saver.m_arrayData[0].htmlFile=="synthetic-index.html"&&saver.m_arrayData[0].flags==7&&saver.m_arrayData[0].notify,"actual saver keeps note identity source path flags and notification policy");
            check(v.emptyAtDispatch&&v.m_bContentsChanged&&v.unsavedDom=="unsubmitted synthetic DOM","actual saver handoff remains intact while only retired source cache is cleared");
        }
        // Real Qt queued functor owns a shared QString snapshot independently.
        auto* queuedView=new CurrentView;seed(*queuedView);const qsizetype expectedSize=queuedView->m_currentNoteHtml.size();const quint32 expectedDigest=digest(queuedView->m_currentNoteHtml);const auto allocationBefore=memory();
        QObject* receiver=new QObject;auto delivered=std::make_shared<bool>(false);
        QMetaObject::invokeMethod(receiver,[&,receiver,delivered,text=queuedView->m_currentNoteHtml,expectedSize,expectedDigest,allocationBefore]{
            check(text.size()==expectedSize&&digest(text)==expectedDigest,"real queued handoff preserves the complete synthetic snapshot after view-cache release");*delivered=true;
            QTimer::singleShot(0,&app,[&,receiver,delivered,allocationBefore]{
                const auto afterQueuedRelease=memory();check(*delivered,"queued snapshot receiver ran once");check(allocationBefore.inUse>=afterQueuedRelease.inUse+15*1024*1024,"snapshot is freed after the independent queued owner completes");
                observations.append(QJsonObject{{"variant","queued-save-owned-snapshot"},{"before",json(allocationBefore)},{"afterQueuedOwnerCompletes",json(afterQueuedRelease)}});delete receiver;
                const QJsonObject report{{"checks",checks},{"failures",failures},{"qtVersion",qVersion()},{"observations",observations},{"usesExactExtractedClearMethods",true},{"usesActualSaverHandoff",true},{"queuedOwnershipUsesRealQt",true},{"fullClientAccepted",false},{"allocatorIntervention",false},{"normalAccountOrNotesAccessed",false}};
                qInfo().noquote()<<"CACHE_RETIREMENT_REPORT"<<QJsonDocument(report).toJson(QJsonDocument::Compact);status=failures?1:0;app.quit();
            });
        },Qt::QueuedConnection);
        queuedView->clear();const auto afterViewRelease=memory();check(queuedView->m_currentNoteHtml.isEmpty()&&queuedView->emptyAtDispatch,"view releases its reference before explicit blank dispatch");check(afterViewRelease.inUse+1024*1024>=allocationBefore.inUse,"queued handoff retains allocation until its own completion");delete queuedView;
    });
    QTimer::singleShot(10000,&app,[&]{qCritical()<<"cache retirement test deadline";status=2;app.quit();});app.exec();return status;
}
