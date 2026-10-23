#include "WizBoundedCache.h"
#include <QCoreApplication>
#include <QImage>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
struct Preview {QImage image;QString text;};
int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);using Cache=WizBoundedCache<Preview>;
    int checks=0,failures=0;const auto check=[&](bool v,const char* m){++checks;failures+=!v;qInfo().noquote()<<(v?"PASS":"FAIL")<<m;};
    Cache cache(128,32);Preview value{QImage(50,50,QImage::Format_ARGB32),QStringLiteral("synthetic")},found;
    value.image.fill(Qt::blue);check(!cache.find("preview",found),"initial miss occurs before another worker completes");
    const auto first=cache.beginLoad("preview");check(bool(first),"one original load admitted");
    check(cache.complete("preview",first,value,12000)==Cache::Completion::Stored,"another worker fills the requested entry");
    int redundant=0;for(int i=0;i<500;++i){const auto ticket=cache.beginLoad("preview");if(ticket){++redundant;cache.complete("preview",ticket,value,12000);}}
    check(redundant==WIZ_PERF_EXPECTED_REDUNDANT,"immediate pre-change and current header have their recorded admission counts");
    check(cache.find("preview",found)&&found.image==value.image&&found.text==value.text,"exact thumbnail bytes and text remain available");
    cache.invalidate("preview");const auto changed=cache.beginLoad("preview");check(bool(changed),"actual invalidation still admits a new load");
    value.text="updated";check(cache.complete("preview",changed,value,12000)==Cache::Completion::Stored&&cache.find("preview",found)&&found.text=="updated","invalidated replacement remains observable");
    cache.close();check(cache.stats().entries==0&&cache.stats().pending==0&&!cache.beginLoad("later"),"close releases held previews and refuses late work");
    qInfo().noquote()<<"PERF_CACHE_REPORT "<<QJsonDocument(QJsonObject{{"accepted",!failures},{"checks",checks},{"failures",failures},{"redundantAdmissions",redundant},{"inputAdmissions",500},{"syntheticOnly",true},{"wholeAppMemoryClaimed",false}}).toJson(QJsonDocument::Compact);
    return failures?1:0;
}
