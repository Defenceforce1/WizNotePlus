#pragma once

#include <QString>

namespace WizMarkdownExportRecovery { struct Result; }

namespace WizMarkdownExportRecoveryAudit {

enum class AppendOutcome {
    Recorded,
    Busy,
    Refused,
    Failed
};

struct AppendResult {
    AppendOutcome outcome = AppendOutcome::Refused;
    QString diagnosticDetail;
};

enum class TakeoverOutcome { None, Removed, Active, Refused, Failed };

struct TakeoverResult {
    TakeoverOutcome outcome = TakeoverOutcome::Refused;
    QString diagnosticDetail;
};

// Product location: QStandardPaths::AppLocalDataLocation/recovery-audit/v1.
// A standalone-test-only compile definition may redirect the app-data base.
QString auditDirectory();
QString auditLogPath();
QString auditLockPath();
// Only archive slots 1 and 2 are owned by this version; other indexes are empty.
QString auditArchivePath(int index);

// Explicit-confirmation building block only: removes only a same-host,
// same-application, demonstrably dead writer lock after revalidation. Never
// creates directories, writes audit events, or touches a destination/recovery
// site. Removed is not recovery success or a lock reservation: the caller must
// start a fresh, fully validated append. Ordinary append never calls this.
TakeoverResult takeOverConfirmedDeadAuditLock();

// Appends only the privacy-minimized confirm/requested intent. This operation
// owns only the audit lock; it never acquires a destination lock or changes a
// recovery site. Full segments rotate into at most two validated archives.
// Existing dead or unsafe audit locks are retained.
AppendResult appendConfirmIntent(const QString& destinationIdentity);

// Optional worker-side retain/retained record. No destination access, takeover,
// retry or recovery authority. This uses normal durable bounded audit I/O and
// therefore has NO filesystem time bound: never call it on the GUI/quit path or
// start it merely to log shutdown. A caller may cancel/drop it before start;
// failure must not gate saving, quitting or the decision to retain the site.
AppendResult appendRetain(const QString& destinationIdentity);

// Records only a finite recovery outcome, never diagnostic text or file paths.
// The caller must have released its destination lock first. A failed release
// or an unconfirmed release after a completed recovery refuses before audit I/O.
// This shares bounded audit rotation but never executes/retries file recovery
// or provides user confirmation.
AppendResult appendConfirmResult(const QString& destinationIdentity,
    const WizMarkdownExportRecovery::Result& recoveryResult);

}
