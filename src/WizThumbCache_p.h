#ifndef THUMBCACHE_P_H
#define THUMBCACHE_P_H

#include <QObject>
#include "share/WizBoundedCache.h"
#include "share/WizObject.h"


struct WIZABSTRACT;
struct WIZDOCUMENTDATA;

class WizThumbCache;


class WizThumbCachePrivate : public QObject
{
    Q_OBJECT

public:
    WizThumbCachePrivate(WizThumbCache* cache);
    ~WizThumbCachePrivate() override;
    bool find(const QString& strKbGUID, const QString& strGUID, WIZABSTRACT& abs);
    using Cache=WizBoundedCache<WIZABSTRACT>;
    Cache::Stats statistics() const { return m_cache->stats(); }

private:
    QString key(const QString& strKbGUID, const QString& strGUID);
    void load(const QString& strKbGUID, const QString& strGUID);

protected Q_SLOTS:
    void onNoteThumbChanged(const WIZDOCUMENTDATA& data);

Q_SIGNALS:
    void thumbLoaded(const QString& strKbGUID, const QString& strGUID);

private:
    std::shared_ptr<Cache> m_cache;
};



#endif // THUMBCACHE_P_H
