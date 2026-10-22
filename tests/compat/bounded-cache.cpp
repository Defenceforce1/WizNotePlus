#include "share/WizBoundedCache.h"
#include <QCoreApplication>
#include <QImage>
#include <QMap>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDebug>
#include <atomic>
#include <thread>
#include <vector>
#ifdef Q_OS_MAC
#include <mach/mach.h>
#endif
struct Preview { QImage image;QString text; };
int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    using Cache=WizBoundedCache<Preview>;
    if(argc==2&&(QByteArray(argv[1])=="--bench-legacy"||QByteArray(argv[1])=="--bench-bounded")){
        const bool legacy=QByteArray(argv[1])=="--bench-legacy";QMap<QString,Preview> old;Cache cache;
        QElapsedTimer time;time.start();
        for(int i=0;i<2000;++i){const QString k=QString::number(i);Preview p{QImage(50,50,QImage::Format_ARGB32),QString(120,'x')};p.image.fill(qRgb(i%256,i/256,64));
            if(legacy){if(old.size()>=10000)old.clear();old.insert(k,p);}else cache.complete(k,cache.beginLoad(k),p,p.image.sizeInBytes()+p.text.size()*2+512);
        }
        qint64 imageBytes=0;for(int i=0;i<2000;++i){Preview p;const QString k=QString::number(i);if(legacy){if(old.contains(k))p=old.value(k);}else cache.find(k,p);imageBytes+=p.image.sizeInBytes();}
        QJsonObject result{{"mode",legacy?"legacy-10000-entry-map":"bounded-cost-lru"},{"entries",legacy?int(old.size()):cache.stats().entries},{"retainedImageBytes",imageBytes},{"elapsedMs",time.elapsed()},{"workloadImages",2000},{"width",50},{"height",50},{"syntheticOnly",true},{"fullAppMemoryClaimed",false}};
#ifdef Q_OS_MAC
        task_vm_info_data_t info{};mach_msg_type_number_t size=TASK_VM_INFO_COUNT;
        if(task_info(mach_task_self(),TASK_VM_INFO,reinterpret_cast<task_info_t>(&info),&size)==KERN_SUCCESS)result.insert("ownFootprintBytes",qint64(info.phys_footprint));
#endif
        qInfo().noquote()<<"CACHE_BENCH"<<QJsonDocument(result).toJson(QJsonDocument::Compact);return 0;
    }
    int n=0,fail=0;const auto check=[&](bool ok,const char* text){++n;fail+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;};
    Preview value{QImage(32,32,QImage::Format_ARGB32),"initial"},read;
    Cache c(128,32);const auto a=c.beginLoad("kb-a::note");
    int admitted=bool(a);for(int i=0;i<499;++i)admitted+=bool(c.beginLoad("kb-a::note"));
    check(admitted==1&&c.stats().pending==1&&c.stats().coalesced==499,"500 simultaneous misses coalesce to one pending load");
    check(c.complete("kb-a::note",a,value,4096)==Cache::Completion::Stored&&c.find("kb-a::note",read)&&read.text==value.text,"current result publishes an exact copy");
    check(c.complete("kb-a::note",a,value,4096)==Cache::Completion::Dropped,"a ticket cannot publish twice");
    const auto other=c.beginLoad("kb-b::note");check(other&&other!=a,"same note identifier in another knowledge base is independent");c.cancel("kb-b::note",other);
    for(const auto& k:{"b","c","d"})c.complete(k,c.beginLoad(k),value,4096);
    c.find("kb-a::note",read);c.complete("e",c.beginLoad("e"),value,4096);
    check(c.find("kb-a::note",read)&&!c.find("b",read)&&c.find("e",read)&&c.stats().entries==4,"LRU evicts only the least recently used entry,not the whole cache");
    c.invalidate("kb-a::note");const auto old=c.beginLoad("kb-a::note");c.invalidate("kb-a::note");c.invalidate("kb-a::note");
    check(!c.beginLoad("kb-a::note")&&c.complete("kb-a::note",old,value,4096)==Cache::Completion::Reload&&!c.find("kb-a::note",read),"updates during a load coalesce and discard stale content before one reload");
    value.text="latest";const auto newer=c.beginLoad("kb-a::note");
    check(c.complete("kb-a::note",old,value,4096)==Cache::Completion::Dropped&&c.complete("kb-a::note",newer,value,4096)==Cache::Completion::Stored&&c.find("kb-a::note",read)&&read.text=="latest","old completion cannot consume or replace a newer generation");
    const auto canceled=c.beginLoad("cancel");c.cancel("cancel",a);check(!c.beginLoad("cancel"),"wrong cancellation cannot release another request");c.cancel("cancel",canceled);check(bool(c.beginLoad("cancel")),"actual cancellation permits retry");
    Cache huge(64,8);huge.complete("small",huge.beginLoad("small"),value,1);huge.complete("huge",huge.beginLoad("huge"),value,1024*1024);
    check(huge.stats().entries==1&&huge.stats().costKiB==64&&huge.find("huge",read),"one oversized preview stays correct without unbounded companion entries or reload loop");
    const auto closing=c.beginLoad("closing");c.close();check(!c.find("e",read)&&!c.beginLoad("new")&&c.complete("closing",closing,value,32)==Cache::Completion::Dropped&&c.stats().entries==0,"closed cache rejects work and clears retained payloads");
    Cache concurrent(512,8);std::vector<std::thread> workers;
    for(int t=0;t<4;++t)workers.emplace_back([&,t]{for(int i=0;i<500;++i){const auto k=QString::number(t)+":"+QString::number(i);const auto token=concurrent.beginLoad(k);concurrent.complete(k,token,value,8192);Preview p;concurrent.find(k,p);}});
    for(auto& worker:workers)worker.join();check(concurrent.stats().pending==0&&concurrent.stats().costKiB<=512&&concurrent.stats().entries<=64,"concurrent read/write eviction remains within the accounting budget");
    qInfo()<<"Bounded cache checks"<<n<<"failures"<<fail;return fail?1:0;
}
