#pragma once

#include "share/WizMarkdownExportRecoveryRequest.h"
#include <QMetaType>

class QWidget;
class WizMarkdownExportRecoveryPrompt;

Q_DECLARE_METATYPE(WizMarkdownExportRecovery::Result)
Q_DECLARE_METATYPE(WizMarkdownExportRecovery::Inspection)
Q_DECLARE_METATYPE(WizMarkdownExportRecovery::LockInspection)

// Dormant one-shot adapter. Own this at application scope, NOT under the view;
// use the existing WizQueuedThreadsSession lifetime before any account windows.
// Confirmation uses the frozen Work packet and strict coordinator; discovery
// uses a separate read-only snapshot. Workers never read QObject/document state.
// It does not initialize/shut down queues, save notes, sync, scan or extend quit.
class WizMarkdownExportRecoverySession final : public QObject
{
    Q_OBJECT
public:
    // Only for a user-selected same destination (export or explicit details),
    // never startup/history scanning. First show() queues read-only discovery.
    WizMarkdownExportRecoverySession(QWidget* owner,
        WizDocumentCallbackEpoch::Ticket document, const QString& destination);
    // Already-inspected entry retained for isolated hosts/tests.
    WizMarkdownExportRecoverySession(QWidget* owner,
        WizDocumentCallbackEpoch::Ticket document, const QString& destination,
        const WizMarkdownExportRecovery::Inspection& inspection,
        WizMarkdownExportRecovery::LockState lock);
    ~WizMarkdownExportRecoverySession() override;

    // GUI thread only. No synchronous inspection or recovery on the GUI thread.
    void show();
    // Required before navigation/derived owner teardown, even if note ID repeats.
    void invalidate();
    // Immediate UI/request retain; no join, I/O, save, sync, retry or timer.
    // Does not start exit logging. Cancels optional ordinary-retain logging
    // before start; already-running I/O drains in the existing pool (no bound).
    void beginQuit();
    WizMarkdownExportRecoveryPrompt* prompt() const;
    // Application observers may keep this independent result handle after the
    // session is destroyed. A queued handle is NOT permission to skip tryStart.
    WizMarkdownExportRecoveryRequest::Work work() const { return m_work; }

signals:
    // Advisory current-context snapshot only, not a reservation or permission
    // to overwrite/recover. A receiver may invalidate or destroy this session.
    void discovered(const WizMarkdownExportRecovery::Inspection& inspection,
        const WizMarkdownExportRecovery::LockInspection& lock);
    // GUI-only receipt; stale/closed/quitting contexts emit nothing. The
    // independent Work result and durable coordinator audit remain separate.
    void resultReady(const WizMarkdownExportRecovery::Result& result);
    // Application cleanup notification, NOT permission to update a stale view.
    // For retained choices, abandoned read-only delivery or completed recovery,
    // even if UI context expired. An abandoned read may still drain in the pool.
    // Retain logging is optional, may be dropped on cleanup, and is never awaited.
    // Queue shutdown can discard this delivery; the queue session still joins
    // workers and a saved Work handle preserves the outcome independently.
    void settled();

private:
    struct Discovery;
    class DiscoveryAction;
    struct RetainAudit;
    class RetainAuditAction;
    void startRetainAudit();
    void startDiscovery();
    void discoveryFinished();
    void attachPrompt(const WizMarkdownExportRecovery::Inspection& inspection,
        WizMarkdownExportRecovery::LockState lock);
    void promptFinished();
    void deliver();
    void notifySettled();
    WizDocumentCallbackEpoch m_lifetime;
    QPointer<QWidget> m_owner;
    std::shared_ptr<Discovery> m_discovery;
    std::shared_ptr<RetainAudit> m_retainAudit;
    QString m_retainIdentity;
    WizMarkdownExportRecoveryRequest* m_request = nullptr;
    QPointer<WizMarkdownExportRecoveryPrompt> m_prompt;
    WizMarkdownExportRecoveryRequest::Work m_work;
    bool m_settled = false;
};
