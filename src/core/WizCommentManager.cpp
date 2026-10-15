#include "WizCommentManager.h"

#include <QMutexLocker>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDebug>
#include <QCoreApplication>
#include <QPointer>
#include "share/WizNetworkSecurity.h"

#include "share/jsoncpp/json/json.h"
#include "sync/WizApiEntry.h"
#include "sync/WizToken.h"
#include "share/WizEventLoop.h"
#include "share/WizThreads.h"
#include "sync/WizKMServer.h"
#include "database/WizDatabase.h"
#include "database/WizDatabaseManager.h"

#define QUERY_DELAY 3

WizCommentQuerier::WizCommentQuerier(QString commentUrl, const QString& kbGUID, const QString& GUID,
    std::shared_ptr<std::atomic_bool> canceled, QObject* parent)
    : QObject(parent)
    , m_commentUrl(std::move(commentUrl))
    , m_canceled(std::move(canceled))
    , m_kbGUID(kbGUID)
    , m_GUID(GUID)
{
}


void WizCommentQuerier::setCommentsCount(int count)
{
    emit commentCountAcquired(m_GUID, count);
}

void WizCommentQuerier::run()
{
    if (m_canceled->load() || WizNetworkSecurity::isNetworkCancellationRequested()) return;
    if (m_commentUrl.isEmpty())
    {
        m_commentUrl = WizCommonApiEntry::commentUrlTemplate();
    }
    if (m_canceled->load() || WizNetworkSecurity::isNetworkCancellationRequested()) return;
    //
    QString token = WizToken::token();
    if (m_canceled->load() || WizNetworkSecurity::isNetworkCancellationRequested()) return;
    emit tokenAcquired(token);
    //
    if (token.isEmpty())
        return;
    //
    WIZUSERINFO info = WizToken::userInfo();
    //
    info.strKbGUID = m_kbGUID;
    //
    WIZGROUPDATA group;
    auto* database = WizDatabaseManager::instance();
    if (!database) return;
    if (database->db().getGroupData(m_kbGUID, group))
    {
        info = WIZUSERINFO(info, group);
    }
    //
    WizKMDatabaseServer server(info);
    int count = 0;
    server.getCommentCount(m_GUID, count);
    if (m_canceled->load() || WizNetworkSecurity::isNetworkCancellationRequested()) return;
    //
    setCommentsCount(count);
}


WizCommentManager::WizCommentManager(QObject* parent)
    : QObject(parent)
    , m_busy(false)
{
    m_timer.setSingleShot(true);
    connect(&m_timer, SIGNAL(timeout()), SLOT(on_timer_timeOut()));
}

WizCommentManager::~WizCommentManager()
{
    m_canceled->store(true);
    m_timer.stop();
}

QString WizCommentManager::getCommentUrlTemplate()
{
    QMutexLocker locker(&m_mutext);
    Q_UNUSED(locker)

    return m_commentUrlTemplate;
}

void WizCommentManager::setCommentUrlTemplate(QString url)
{
    QMutexLocker locker(&m_mutext);
    Q_UNUSED(locker)

    m_commentUrlTemplate = url;
}

/*
 *  获取评论数目是通过网络请求获取的，需要延迟处理请求。防止在快速切换笔记时产生大量的网络请求
 */
void WizCommentManager::queryCommentCount(const QString& kbGUID, const QString& GUID, bool removeOtherQueryRequest)
{
    if (WizNetworkSecurity::isNetworkCancellationRequested()) return;
#ifdef WIZNOTE_NATIVE_TRANSFER_TESTS
    // The isolated main-window fixture has no account or remote comments.
    if (QCoreApplication::instance()->property("wiznoteIsolatedWindowControlsTest").toBool())
        return;
#endif
    {
        QMutexLocker locker(&m_mutext);
        Q_UNUSED(locker)

        if (removeOtherQueryRequest)
        {
            m_timer.stop();
            m_queryList.clear();
        }

        for (CountQueryData query : m_queryList)
        {
            if(query.strGUID == GUID)
                return;
        }
        //
        CountQueryData query;
        query.strKBGUID = kbGUID;
        query.strGUID = GUID;
        m_queryList.append(query);

        if (!m_timer.isActive())
        {
            m_timer.start(QUERY_DELAY * 1000);
        }
    }
    //
    if (m_queryList.size() == 1 && !m_busy)
    {
        on_timer_timeOut();
    }
}


void WizCommentManager::on_timer_timeOut()
{
    if (m_busy || WizNetworkSecurity::isNetworkCancellationRequested()) return;
    CountQueryData data;
    pickData(data);

    if (data.strGUID.isEmpty())
        return;

    enqueueQuery(data, [](WizCommentQuerier& query) { query.run(); });
}

void WizCommentManager::enqueueQuery(const CountQueryData& data, std::function<void(WizCommentQuerier&)> run)
{
    // Reserve on the UI thread, before the task starts, so a timer cannot enqueue
    // several concurrent queries while the shared network workers are occupied.
    m_busy = true;
    const QPointer<WizCommentManager> owner(this);
    const auto canceled = m_canceled;
    const QString url = getCommentUrlTemplate();
    QObject* const dispatcher = QCoreApplication::instance();
    // QApplication outlives the network pool (drained by cleanOnQuit). Worker
    // connections target that stable dispatcher, not a possibly closed note.
    WizExecuteOnThread(WIZ_THREAD_NETWORK, [owner, canceled, dispatcher, url, data, run] {
        if (canceled->load() || WizNetworkSecurity::isNetworkCancellationRequested()) return;
        WizCommentQuerier query(url, data.strKBGUID, data.strGUID, canceled);
        QObject::connect(&query, &WizCommentQuerier::tokenAcquired, dispatcher,
            [owner, canceled](const QString& token) {
                if (canceled->load() || !owner || WizNetworkSecurity::isShuttingDown()) return;
                Q_EMIT owner->tokenAcquired(token);
            }, Qt::QueuedConnection);
        QObject::connect(&query, &WizCommentQuerier::commentCountAcquired, dispatcher,
            [owner, canceled](const QString& guid, int count) {
                if (canceled->load() || !owner || WizNetworkSecurity::isShuttingDown()) return;
                Q_EMIT owner->commentCountAcquired(guid, count);
            }, Qt::QueuedConnection);
        run(query);
        const QString resolvedUrl = query.commentUrlTemplate();
        QMetaObject::invokeMethod(dispatcher, [owner, canceled, resolvedUrl] {
            if (canceled->load() || !owner || WizNetworkSecurity::isShuttingDown()) return;
            if (!resolvedUrl.isEmpty()) owner->setCommentUrlTemplate(resolvedUrl);
            owner->m_busy = false;
            if (!owner->m_queryList.isEmpty()) owner->m_timer.start(QUERY_DELAY * 1000);
        }, Qt::QueuedConnection);
    });
}

void WizCommentManager::pickData(CountQueryData& data)
{
    QMutexLocker locker(&m_mutext);
    Q_UNUSED(locker)

    if (m_queryList.count() > 0)
    {
        data = m_queryList.first();
        m_queryList.pop_front();
    }
}

