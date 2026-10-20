#pragma once

#include <QByteArray>
#include <QString>

#include <functional>
#include <memory>

namespace WizMarkdownExportRecovery {

enum class Availability {
    None,
    AwaitingDecision,
    Busy,
    Refused
};

struct Inspection {
    Availability availability = Availability::None;
    QString destinationIdentity;
    QString userVisibleSummary;
    QString diagnosticDetail;
};

enum class Decision {
    Inspect,
    Cancel,
    Retain,
    Confirm
};

enum class RecoveryOutcome {
    None,
    RolledBack,
    Finalized,
    Busy,
    Refused
};

enum class AuditOutcome {
    NotRequested,
    Recorded,
    Failed
};

enum class LockReleaseOutcome {
    NotRequested,
    Released,
    Failed
};

// Recovery and audit are intentionally independent. A future confirmed recovery
// must not report an already completed file operation as reverted merely because
// the final audit append or lock release failed. Callers must check lockRelease
// before proceeding to another locked operation or reporting overall completion.
struct Result {
    RecoveryOutcome recovery = RecoveryOutcome::None;
    AuditOutcome audit = AuditOutcome::NotRequested;
    QString userVisibleSummary;
    QString diagnosticDetail;
    LockReleaseOutcome lockRelease = LockReleaseOutcome::NotRequested;
};

enum class LockState {
    Available,
    Active,
    ConfirmedDead,
    Refused
};

struct LockInspection {
    LockState state = LockState::Available;
    QString diagnosticDetail;
};

enum class LockTakeoverOutcome {
    None,
    Removed,
    Active,
    Refused,
    Failed
};

struct LockTakeover {
    LockTakeoverOutcome outcome = LockTakeoverOutcome::Refused;
    QString diagnosticDetail;
    LockReleaseOutcome lockRelease = LockReleaseOutcome::NotRequested;
};

enum class PreflightPlan {
    None,
    RollBack,
    Finalize,
    Busy,
    Refused
};

struct Preflight {
    PreflightPlan plan = PreflightPlan::None;
    LockState lock = LockState::Available;
    QString evidenceState;
    QString userVisibleSummary;
    QString diagnosticDetail;
    LockReleaseOutcome lockRelease = LockReleaseOutcome::NotRequested;
};

enum class PreparationOutcome {
    Prepared,
    Busy,
    Refused,
    FailedCleaned,
    // Staging, cleanup or release remains unconfirmed. This is not a guarantee
    // that the recovery directory still exists; inspect current state/details.
    FailedRetained
};

struct PreparedExport; // Opaque, process-local, one-attempt publication proof.

struct Preparation {
    PreparationOutcome outcome = PreparationOutcome::Refused;
    QString recoveryDirectory;
    QString userVisibleSummary;
    QString diagnosticDetail;
    LockReleaseOutcome lockRelease = LockReleaseOutcome::NotRequested;
    std::shared_ptr<const PreparedExport> publication;
};

enum class PublicationOutcome { Published, RolledBack, Retained, Busy, Refused };
struct Publication {
    PublicationOutcome outcome = PublicationOutcome::Refused;
    // True only when this attempt confirmed the final Markdown commit. False
    // is not a claim that the old contents are present after an uncertain error.
    bool markdownCommitted = false;
    LockReleaseOutcome lockRelease = LockReleaseOutcome::NotRequested;
    QString diagnosticDetail;
};

// These helpers only derive deterministic sibling paths. They do not create,
// remove or lock anything.
QString destinationIdentity(const QString& destination);
QString recoveryDirectory(const QString& destination);
QString destinationLockPath(const QString& destination);

// Read-only, same-destination inspection. It never removes a stale lock,
// repairs a manifest, changes live output or cleans a recovery directory.
Inspection inspect(const QString& destination);

// Classifies an existing lock without removing it. ConfirmedDead is evidence for
// a future explicit-confirmation path, not permission to take the lock here.
LockInspection inspectDestinationLock(const QString& destination);

// Explicit-confirmation building block. Ordinary inspection, preflight,
// preparation and execution never call this. It removes only a lock that is
// revalidated as same-host, same-application and owned by a non-running PID.
// The caller must reacquire the destination lock and revalidate the manifest
// and live state after a successful removal. Cooperating explicit takeovers
// serialize nonblockingly on the opened old inode on Unix.
LockTakeover takeOverConfirmedDeadDestinationLock(const QString& destination);

// Briefly acquires an otherwise free destination lock and recomputes live
// Markdown/resource digests. It returns a plan only; no export or recovery file
// is renamed, removed, replaced or otherwise repaired.
Preflight preflight(const QString& destination);

// Executes only a caller-confirmed plan after acquiring the destination lock,
// rereading the strict manifest and recomputing the live state. The expected
// plan must be RollBack or Finalize and must still match the evidence. This
// dormant primitive does not append audit events or provide user confirmation.
Result executePlan(const QString& destination, PreflightPlan expectedPlan);

// Creates a durable, deterministic recovery site without changing the live
// Markdown or resources. The callback exports HTML/resources beneath the given
// staged HTML path. Existing recovery sites and locks are never removed here.
Preparation prepare(const QString& destination, const QByteArray& markdownUtf8,
    const std::function<bool(const QString&)>& exportHtml);

// Publishes only a successfully prepared, released, same-process one-attempt
// proof. Revalidates directory identity/manifest/live digests under a fresh lock,
// installs resources, then atomically commits Markdown last. Pre-commit failures
// may roll back only this proof's site; committed/cleanup/release uncertainty is
// reported separately. Never adopts existing sites, takes over locks or audits.
// If old resources exist, the prepare callback must preserve/merge them into the
// new resource tree (as the product transaction does); v1 has no deletion mode.
Publication publishPrepared(const Preparation& preparation);

// Transitional experimental App gate for the existing ordinary transaction.
// Revalidates absence of a recovery site under the conservative destination lock
// before calling the writer exactly once. No takeover, audit or recovery. False
// may mean an already-completed write with uncertain lock release, NOT rollback.
// This does not yet give the legacy writer persistent restart recovery.
bool withFreshDestination(const QString& destination,
    const std::function<bool()>& writer, QString& error);

}

