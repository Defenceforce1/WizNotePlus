#ifndef CWIZCOMMENTDOWNLOADER_H
#define CWIZCOMMENTDOWNLOADER_H

#include <QObject>
#include <QList>
#include <QTimer>
#include <QMutex>
#include <atomic>
#include <functional>
#include <memory>

class WizCommentQuerier;

/*
 *  为防止快速切换笔记时创建大量评论数目查询带来的问题，获取评论数目的请求会延迟执行
 */


class WizCommentManager : public QObject
{
    Q_OBJECT
public:
    WizCommentManager(QObject* parent = 0);
    ~WizCommentManager() override;

    QString getCommentUrlTemplate();
    void queryCommentCount(const QString& kbGUID, const QString& GUID, bool removeOtherQueryRequest);

signals:
    void tokenAcquired(QString token);
    void commentCountAcquired(QString documentGUID, int count);

private slots:
    void on_timer_timeOut();

private:
    struct CountQueryData
    {
        QString strGUID;
        QString strKBGUID;
    };

    QList<CountQueryData> m_queryList;
    QTimer m_timer;
    QMutex m_mutext;
    bool m_busy;
    QString m_commentUrlTemplate;
    // Workers retain only this cancellation flag and immutable request values,
    // never the manager or the title bar that owns it.
    std::shared_ptr<std::atomic_bool> m_canceled = std::make_shared<std::atomic_bool>(false);

private:
    void pickData(CountQueryData& data);
    void setCommentUrlTemplate(QString url);
    void enqueueQuery(const CountQueryData& data, std::function<void(WizCommentQuerier&)> run);
    friend int wizCommentLifetimeSelfTest(int, char**);
    //
    friend class WizCommentQuerier;
};

class WizCommentQuerier : public QObject
{
    Q_OBJECT
public:
    WizCommentQuerier(QString commentUrl, const QString& kbGUID, const QString& GUID,
        std::shared_ptr<std::atomic_bool> canceled, QObject* parent = nullptr);

    void run();
    QString commentUrlTemplate() const { return m_commentUrl; }

signals:
    void tokenAcquired(QString token);
    void commentCountAcquired(QString documentGUID, int count);

private:
    void setCommentsCount(int count);

private:
    QString m_commentUrl;
    std::shared_ptr<std::atomic_bool> m_canceled;
    QString m_kbGUID;
    QString m_GUID;
};

#endif // CWIZCOMMENTDOWNLOADER_H
