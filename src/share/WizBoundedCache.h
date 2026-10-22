#pragma once
#include <QCache>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QString>
#include <memory>

// Byte-cost LRU and single-flight loading; no files, database or QObject access.
template<class Value> class WizBoundedCache final {
public:
    using Ticket=std::shared_ptr<const char>;
    enum class Completion { Dropped, Stored, Reload };
    struct Stats { int entries,pending,costKiB,budgetKiB; quint64 loads,hits,coalesced; bool closed; };
    explicit WizBoundedCache(int budgetKiB=16*1024,int minimumCostKiB=32)
        :m_values(qMax(1,budgetKiB)),m_floor(qBound(1,minimumCostKiB,qMax(1,budgetKiB))){}
    bool find(const QString& key,Value& result) {
        QMutexLocker lock(&m_mutex);if(m_closed)return false;
        if(const auto* value=m_values.object(key)){result=*value;++m_hits;return true;}return false;
    }
    Ticket beginLoad(const QString& key) {
        QMutexLocker lock(&m_mutex);if(m_closed)return {};
        if(m_pending.contains(key)){++m_coalesced;return {};}
        const auto token=std::make_shared<const char>(0);m_pending.insert(key,{token,false});++m_loads;return token;
    }
    void invalidate(const QString& key) {
        QMutexLocker lock(&m_mutex);m_values.remove(key);
        auto p=m_pending.find(key);if(p!=m_pending.end())p->dirty=true;
    }
    Completion complete(const QString& key,const Ticket& token,const Value& value,qint64 bytes) {
        QMutexLocker lock(&m_mutex);auto p=m_pending.find(key);
        if(m_closed||!token||p==m_pending.end()||p->token!=token)return Completion::Dropped;
        const bool dirty=p->dirty;m_pending.erase(p);if(dirty)return Completion::Reload;
        // A single oversized preview occupies the whole accounting budget,
        // preserving quality without a discard/reload loop. Not a strict RSS cap.
        const qint64 kib=bytes<=0?1:bytes/1024+(bytes%1024!=0);
        const int cost=int(qMin<qint64>(m_values.maxCost(),qMax<qint64>(m_floor,kib)));
        m_values.insert(key,new Value(value),cost);return Completion::Stored;
    }
    void cancel(const QString& key,const Ticket& token) {
        QMutexLocker lock(&m_mutex);auto p=m_pending.find(key);
        if(p!=m_pending.end()&&p->token==token)m_pending.erase(p);
    }
    void close() {QMutexLocker lock(&m_mutex);m_closed=true;m_pending.clear();m_values.clear();}
    bool isClosed() const {QMutexLocker lock(&m_mutex);return m_closed;}
    Stats stats() const {QMutexLocker lock(&m_mutex);return {int(m_values.size()),int(m_pending.size()),int(m_values.totalCost()),int(m_values.maxCost()),m_loads,m_hits,m_coalesced,m_closed};}
private:
    struct Pending {Ticket token;bool dirty;};
    mutable QMutex m_mutex;
    QCache<QString,Value> m_values;
    QHash<QString,Pending> m_pending;
    int m_floor;
    quint64 m_loads=0,m_hits=0,m_coalesced=0;
    bool m_closed=false;
};
