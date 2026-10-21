#include "DocumentLoaderSaver.h"

#include <QApplication>
#include <QDebug>
#include <QMessageBox>
#include <algorithm>
#include <limits>

#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "share/WizThreads.h"

WizDocumentLoaderThread::WizDocumentLoaderThread(WizDatabaseManager &dbMgr, QObject *parent)
    : QThread(parent)
    , m_dbMgr(dbMgr)
    , m_stop(false)
{
    qRegisterMetaType<QPointer<QObject>>("QPointer<QObject>");
}

void WizDocumentLoaderThread::load(const WIZDOCUMENTDATA &doc, WizEditorMode editorMode, quint64 sequence)
{
    setCurrentDoc(doc.strKbGUID, doc.strGUID, editorMode, sender(), sequence);

    if (!isRunning())
    {
        start();
    }
}

void WizDocumentLoaderThread::stop()
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker);

    m_stop = true;

    m_waitEvent.wakeAll();
}

void WizDocumentLoaderThread::waitForDone()
{
    stop();

    while (this->isRunning())
    {
        QApplication::processEvents();
        // Yield while I/O finishes without imposing a local-save deadline.
        this->wait(10);
    }

    this->disconnect();
}

void WizDocumentLoaderThread::run()
{
    //FIXME: Document needs to be loaded once at least.
    //  So, two conditional m_stop-return within the loop were commented.
    while (!m_stop)
    {
        //if (m_stop)
        //    return;
        //
        LoadRequest request;
        if (!peekCurrentDoc(request)) continue;
        const auto& kbGuid = request.kbGuid;
        const auto& docGuid = request.docGuid;
        //if (m_stop)
        //    return;
        //
        if (docGuid.isEmpty())
            continue;
        //
        WizDatabase& db = m_dbMgr.db(kbGuid);
        WIZDOCUMENTDATA data;
        if (!db.documentFromGuid(docGuid, data))
        {
            emit loadFailed(kbGuid, docGuid, {}, {}, request.requester, request.sequence);
            continue;
        }
        //
        QString strHtmlFile;
        if (db.documentToTempHtmlFile(data, strHtmlFile))
        {
            emit loaded(kbGuid, docGuid, strHtmlFile, request.editorMode, request.requester, request.sequence);
        }
        else
        {
            emit loadFailed(kbGuid, docGuid, strHtmlFile, data.strTitle, request.requester, request.sequence);
        }
    }
}

void WizDocumentLoaderThread::setCurrentDoc(QString kbGUID, QString docGUID, WizEditorMode editorMode, QObject* requester, quint64 sequence)
{
    {
        QMutexLocker locker(&m_mutex);
        Q_UNUSED(locker);

        // Keep independent pages' requests. Only supersede a pending request
        // from the same live view, preserving rapid navigation's latest choice.
        LoadRequest request{kbGUID, docGUID, editorMode, requester, requester != nullptr, sequence};
        auto pending = requester ? std::find_if(m_pendingLoads.begin(), m_pendingLoads.end(),
            [requester](const LoadRequest& item) { return item.hasRequester && item.requester == requester; }) : m_pendingLoads.end();
        if (pending != m_pendingLoads.end()) *pending = request;
        else m_pendingLoads.push_back(request);
    }

    m_waitEvent.wakeAll();
}

bool WizDocumentLoaderThread::isEmpty()
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker);

    return m_pendingLoads.empty();
}

bool WizDocumentLoaderThread::peekCurrentDoc(LoadRequest& result)
{
    while (true)
    {
        if (isEmpty()) m_waitEvent.wait();
        QMutexLocker locker(&m_mutex);
        Q_UNUSED(locker);
        while (!m_pendingLoads.empty()) {
            const auto request = m_pendingLoads.front(); m_pendingLoads.pop_front();
            if (request.hasRequester && request.requester.isNull()) continue;
            result = request;
            return true;
        }
        if (m_stop) return false;
    }
}




WizDocumentSaverThread::WizDocumentSaverThread(WizDatabaseManager &dbMgr, QObject *parent)
    : QThread(parent)
    , m_dbMgr(dbMgr)
    , m_stop(false)
{
    qRegisterMetaType<QPointer<QObject>>("QPointer<QObject>");
}

void WizDocumentSaverThread::save(const WIZDOCUMENTDATA& doc, const QString& strHtml,
                                          const QString& strHtmlFile, int nFlags, bool bNotify /*= fasle*/)
{
    SAVEDATA data;
    data.doc = doc;
    data.html = strHtml;
    data.htmlFile = strHtmlFile;
    data.flags = nFlags;
    data.notify = bNotify;
    data.requester = sender();

    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker);

    m_arrayData.push_back(data);

    m_waitEvent.wakeAll();

    if (!isRunning())
    {
        start();
    }
}

quint64 WizDocumentSaverThread::saveFence(const WIZDOCUMENTDATA& doc)
{
    QMutexLocker locker(&m_mutex);
    if (m_stop || doc.strGUID.isEmpty() || m_nextFence == std::numeric_limits<quint64>::max()) return 0;
    SAVEDATA data;
    data.doc = doc;
    data.fence = ++m_nextFence;
    data.flags = 0;
    data.notify = false;
    m_arrayData.push_back(data);
    m_waitEvent.wakeAll();
    if (!isRunning()) start();
    return data.fence;
}

void WizDocumentSaverThread::waitForDone()
{
    stop();

    while (this->isRunning())
    {
        QApplication::processEvents();
        // Keep completion/progress delivery responsive, but avoid busy polling.
        this->wait(10);
    }

    this->disconnect();
}

void WizDocumentSaverThread::stop()
{
    QMutexLocker locker(&m_mutex);
    m_stop = true;
    m_waitEvent.wakeAll();
}

bool WizDocumentSaverThread::isEmpty()
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker);

    return m_arrayData.empty();
}

WizDocumentSaverThread::SAVEDATA WizDocumentSaverThread::peekFirst()
{
    QMutexLocker locker(&m_mutex);
    Q_UNUSED(locker);

    SAVEDATA data = m_arrayData[0];
    m_arrayData.erase(m_arrayData.begin());
    m_savingForTransfer = true;
    return data;
}

void WizDocumentSaverThread::peekData(SAVEDATA& data)
{
    while (1)
    {
        if (m_stop && isEmpty())
            return;

        if (isEmpty())
        {
            m_waitEvent.wait();
        }

        if (isEmpty())
        {
            if (m_stop)
                return;
            continue;
        }

        data = peekFirst();

        break;
    }
}

void WizDocumentSaverThread::run()
{
    while (true)
    {
        SAVEDATA data;
        peekData(data);

        if (data.fence) {
            bool ok;
            {
                QMutexLocker locker(&m_mutex);
                ok = !m_failedSaveNotes.contains(data.doc.strKbGUID + "/" + data.doc.strGUID);
                m_savingForTransfer = false;
            }
            // Only an ID and the status at this frontier cross threads. No
            // callback/GUI owner or plaintext snapshot is held by a fence.
            emit saveFenceReached(data.fence, ok);
            continue;
        }

        if (data.doc.strGUID.isEmpty())
        {
            if (m_stop)
                return;
            continue;
        }

        WizDatabase& db = m_dbMgr.db(data.doc.strKbGUID);

        WIZDOCUMENTDATA doc;
        if (!db.documentFromGuid(data.doc.strGUID, doc))
        {
            {
                QMutexLocker locker(&m_mutex);
                m_savingForTransfer = false;
                m_transferError = true;
                m_failedSaveNotes.insert(data.doc.strKbGUID + "/" + data.doc.strGUID);
            }
            qDebug() << "fault error: can't find doc in database: " << data.doc.strGUID;
            // Every dequeued save must complete, including a deleted/missing
            // note. Emit outside the mutex so observers cannot deadlock it.
            emit saveFailed(data.doc.strKbGUID, data.doc.strGUID, data.doc.strTitle);
            emit saved(data.doc.strKbGUID, data.doc.strGUID, false, data.requester);
            continue;
        }

        qDebug() << "Saving note: " << doc.strTitle;

        bool ok = db.updateDocumentData(doc, data.html, data.htmlFile, data.flags, data.notify);
        {
            QMutexLocker locker(&m_mutex);
            m_savingForTransfer = false;
            m_transferError = m_transferError || !ok;
            const QString key = doc.strKbGUID + "/" + doc.strGUID;
            if (ok) m_failedSaveNotes.remove(key);
            else m_failedSaveNotes.insert(key);
        }

        if (ok)
        {
            qDebug() << "Save note done: " << doc.strTitle;
        }
        else
        {
            qDebug() << "Save note failed: " << doc.strTitle;
            emit saveFailed(db.kbGUID(), doc.strGUID, doc.strTitle);
        }

        emit saved(db.kbGUID(), doc.strGUID, ok, data.requester);
    }
}

bool WizDocumentSaverThread::isIdleForTransfer()
{
    QMutexLocker locker(&m_mutex);
    return m_arrayData.empty() && !m_savingForTransfer;
}

void WizDocumentSaverThread::clearTransferError()
{
    QMutexLocker locker(&m_mutex);
    m_transferError = false;
}

bool WizDocumentSaverThread::hasTransferError()
{
    QMutexLocker locker(&m_mutex);
    return m_transferError || !m_failedSaveNotes.isEmpty();
}

bool WizDocumentSaverThread::hasUnresolvedSaveFailure()
{
    QMutexLocker locker(&m_mutex);
    return !m_failedSaveNotes.isEmpty();
}

