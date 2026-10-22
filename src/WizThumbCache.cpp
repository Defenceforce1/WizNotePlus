#include "WizThumbCache.h"
#include "WizThumbCache_p.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "share/WizThreads.h"
#include <QPointer>

namespace {
bool loadAbstract(const QString& kb,const QString& guid,WIZABSTRACT& abs)
{
    auto* manager=WizDatabaseManager::instance();
    if(!manager||!manager->isOpened(kb))return false;
    auto& db=manager->db(kb);
    if(!db.padAbstractFromGuid(guid,abs)){
        if(db.updateDocumentAbstract(guid))db.padAbstractFromGuid(guid,abs);
    }
    abs.strKbGUID=kb;abs.guid=guid;
    if(abs.text.isEmpty())abs.text=" ";
    return true;
}
qint64 costBytes(const QString& key,const WIZABSTRACT& value)
{
    return value.image.sizeInBytes()+2*(qint64(key.size())+value.strKbGUID.size()+value.guid.size()+value.text.size())
        +sizeof(WIZABSTRACT)+256;
}
WizThumbCache* currentCache=nullptr;
WizThumbCachePrivate* implementation=nullptr;
}

WizThumbCachePrivate::WizThumbCachePrivate(WizThumbCache* cache)
    :QObject(cache),m_cache(std::make_shared<Cache>())
{
    connect(WizDatabaseManager::instance(),SIGNAL(documentAbstractModified(const WIZDOCUMENTDATA&)),
        SLOT(onNoteThumbChanged(const WIZDOCUMENTDATA&)));
    connect(this,SIGNAL(thumbLoaded(const QString&,const QString&)),cache,SIGNAL(loaded(const QString&,const QString&)));
}
WizThumbCachePrivate::~WizThumbCachePrivate(){m_cache->close();}
QString WizThumbCachePrivate::key(const QString& kb,const QString& guid){return kb+"::"+guid;}
bool WizThumbCachePrivate::find(const QString& kb,const QString& guid,WIZABSTRACT& abs)
{
    if(m_cache->find(key(kb,guid),abs))return true;
    load(kb,guid);return false;
}
void WizThumbCachePrivate::load(const QString& kb,const QString& guid)
{
    const auto cache=m_cache;const QString id=key(kb,guid);const auto ticket=cache->beginLoad(id);
    if(!ticket)return;
    const QPointer<WizThumbCachePrivate> owner(this);
    // Worker owns state and values, not a raw cache QObject. QPointer is only
    // inspected on the GUI queue; teardown closes independent state.
    WizExecuteOnThread(WIZ_THREAD_DEFAULT,[cache,owner,kb,guid,id,ticket]{
        if(cache->isClosed())return;
        WIZABSTRACT abs;
        if(!loadAbstract(kb,guid,abs)){cache->cancel(id,ticket);return;}
        const auto result=cache->complete(id,ticket,abs,costBytes(id,abs));
        if(result==Cache::Completion::Dropped)return;
        WizExecuteOnThread(WIZ_THREAD_MAIN,[cache,owner,kb,guid,result]{
            if(cache->isClosed()||!owner)return;
            if(result==Cache::Completion::Reload)owner->load(kb,guid);
            else emit owner->thumbLoaded(kb,guid);
        });
    });
}
void WizThumbCachePrivate::onNoteThumbChanged(const WIZDOCUMENTDATA& doc)
{
    m_cache->invalidate(key(doc.strKbGUID,doc.strGUID));load(doc.strKbGUID,doc.strGUID);
}
WizThumbCache::WizThumbCache()
{
    Q_ASSERT(!currentCache);currentCache=this;implementation=new WizThumbCachePrivate(this);
}
WizThumbCache* WizThumbCache::instance(){return currentCache;}
WizThumbCache::~WizThumbCache()
{
    delete implementation;implementation=nullptr;currentCache=nullptr;
}
bool WizThumbCache::find(const QString& kb,const QString& guid,WIZABSTRACT& abs)
{
    return implementation&&implementation->find(kb,guid,abs);
}
QVariantMap WizThumbCache::cacheStatistics()
{
    if(!implementation)return {};
    const auto s=implementation->statistics();
    return {{"entries",s.entries},{"pending",s.pending},{"costKiB",s.costKiB},{"budgetKiB",s.budgetKiB},
        {"loads",qulonglong(s.loads)},{"hits",qulonglong(s.hits)},{"coalesced",qulonglong(s.coalesced)}};
}
