#ifndef GUI_DOCUMENTVIEWER_DOCUMENTLOADERSAVER_H
#define GUI_DOCUMENTVIEWER_DOCUMENTLOADERSAVER_H

#include <QThread>
#include <atomic>
#include <QSet>
#include <QMutex>
#include <QWaitCondition>
#include <QPointer>
#include <deque>

#include "share/WizObject.h"

class WizDatabaseManager;

class WizWaitEvent
{

private:
    QMutex m_mutex;
    QWaitCondition m_waitForData;
    // Each loader/saver owns one event and has exactly one consumer. Remember
    // an early notification until consumed, closing its empty-check/wait gap.
    bool m_signaled = false;

public:
    void wait()
    {
        QMutexLocker locker(&m_mutex);
        Q_UNUSED(locker);

        while (!m_signaled) m_waitForData.wait(&m_mutex);
        m_signaled = false;
    }

    void wakeAll()
    {
        QMutexLocker locker(&m_mutex);
        Q_UNUSED(locker);

        m_signaled = true;
        m_waitForData.wakeAll();
    }
};


class WizDocumentLoaderThread : public QThread
{
    Q_OBJECT

public:
    WizDocumentLoaderThread(WizDatabaseManager& dbMgr, QObject* parent);

    void stop();
    void waitForDone();

protected:
    virtual void run();
    void setCurrentDoc(QString kbGuid, QString docGuid, WizEditorMode editorMode, QObject* requester = nullptr, quint64 sequence = 0);

public Q_SLOTS:
    void load(const WIZDOCUMENTDATA& doc, WizEditorMode editorMode, quint64 sequence = 0);

Q_SIGNALS:
    void loaded(const QString kbGUID, const QString strGUID, const QString strFileName, WizEditorMode editorMode,
                QPointer<QObject> requester, quint64 sequence);
    void loadFailed(const QString kbGUID, const QString strGUID, const QString strFileName, const QString strTitle,
                    QPointer<QObject> requester, quint64 sequence);

private:
    bool isEmpty();

private:
    WizDatabaseManager& m_dbMgr;
    struct LoadRequest {
        QString kbGuid;
        QString docGuid;
        WizEditorMode editorMode;
        QPointer<QObject> requester;
        bool hasRequester;
        quint64 sequence;
    };
    bool peekCurrentDoc(LoadRequest& request);
    std::deque<LoadRequest> m_pendingLoads;
    QMutex m_mutex;
    WizWaitEvent m_waitEvent;
    std::atomic_bool m_stop;
};

class WizDocumentSaverThread : public QThread
{
    Q_OBJECT

public:
    WizDocumentSaverThread(WizDatabaseManager& dbMgr, QObject* parent);
    void waitForDone();
    bool isIdleForTransfer();
    void clearTransferError();
    bool hasTransferError();
    // Current per-note failures; unlike the historical transfer error this
    // retires only when the worker actually saves that same note successfully.
    bool hasUnresolvedSaveFailure();

    // Enqueue after the caller's save handoff. The worker reports this note's
    // save status at that exact FIFO frontier, even if no new save was needed.
    // Zero refuses a stopped saver/invalid note. GUI consumers must use a queued
    // connection with their own QObject context and recheck read permission.
    quint64 saveFence(const WIZDOCUMENTDATA& doc);

public Q_SLOTS:
    void save(const WIZDOCUMENTDATA& doc, const QString& strHtml,
              const QString& strHtmlFile, int nFlags, bool bNotify = false);

private:
    struct SAVEDATA
    {
        WIZDOCUMENTDATA doc;
        QString html;
        QString htmlFile;
        int flags;
        bool notify;
        QPointer<QObject> requester;
        quint64 fence = 0;
    };

    std::vector<SAVEDATA> m_arrayData;
    bool m_savingForTransfer = false;
    bool m_transferError = false;
    QSet<QString> m_failedSaveNotes;
    quint64 m_nextFence = 0; // protected by m_mutex; zero is never queued

    bool isEmpty();
    SAVEDATA peekFirst();

protected:
    virtual void run();
    void stop();
    void peekData(SAVEDATA& data);

Q_SIGNALS:
    void saveFenceReached(quint64 fence, bool ok);
    void saved(const QString kbGUID, const QString strGUID, bool ok, QPointer<QObject> requester);
    void saveFailed(const QString kbGUID, const QString strGUID, const QString strTitle);

private:
    WizDatabaseManager& m_dbMgr;
    QMutex m_mutex;
    WizWaitEvent m_waitEvent;
    std::atomic_bool m_stop;
};

#endif // GUI_DOCUMENTVIEWER_DOCUMENTLOADERSAVER_H
