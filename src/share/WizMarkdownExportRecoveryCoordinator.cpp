#include "WizMarkdownExportRecoveryCoordinator.h"
#include "WizMarkdownExportRecoveryAudit.h"

#include <QFileInfo>

#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
#include <QElapsedTimer>
#include <QFile>
#include <QThread>
#endif

namespace {

void confirmationCheckpoint(const char* name)
{
#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
    if (qgetenv("WIZ_RECOVERY_TEST_CONFIRM_CHECKPOINT") != name)
        return;
    QFile ready(qEnvironmentVariable("WIZ_RECOVERY_TEST_CONFIRM_READY"));
    const QByteArray payload(name);
    if (!ready.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || ready.write(payload) != payload.size() || !ready.flush())
        return;
    ready.close();
    const QString resume = qEnvironmentVariable("WIZ_RECOVERY_TEST_CONFIRM_RESUME");
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        if (!resume.isEmpty() && QFileInfo::exists(resume))
            break;
        QThread::msleep(10);
    }
    if (payload == "preflight-complete"
        && qEnvironmentVariableIsSet("WIZ_RECOVERY_TEST_CONFIRM_EXECUTE_RELEASE_FAILURE")) {
        qputenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE", "1");
    }
#else
    Q_UNUSED(name);
#endif
}

}

namespace WizMarkdownExportRecoveryCoordinator {

WizMarkdownExportRecovery::Result decide(const QString& destination,
    WizMarkdownExportRecovery::Decision decision)
{
    using namespace WizMarkdownExportRecovery;
    namespace Audit = WizMarkdownExportRecoveryAudit;
    if (decision == Decision::Cancel || decision == Decision::Retain) {
        return {RecoveryOutcome::None, AuditOutcome::NotRequested,
            QStringLiteral("已保留 Markdown 导出恢复现场；未执行恢复。"), QString()};
    }
    if (decision == Decision::Inspect) {
        const auto view = inspect(destination);
        return {view.availability == Availability::Busy ? RecoveryOutcome::Busy
                : view.availability == Availability::Refused ? RecoveryOutcome::Refused
                : RecoveryOutcome::None,
            AuditOutcome::NotRequested, view.userVisibleSummary, view.diagnosticDetail};
    }
    if (decision != Decision::Confirm || destination.isEmpty()) {
        return {RecoveryOutcome::Refused, AuditOutcome::NotRequested,
            QStringLiteral("未执行恢复；需要有效目标及明确确认。"), QString()};
    }

    const QString target = QFileInfo(destination).absoluteFilePath();
    const QString identity = destinationIdentity(target);
    if (identity.isEmpty()) {
        return {RecoveryOutcome::Refused, AuditOutcome::NotRequested,
            QStringLiteral("未执行恢复；无法确定导出目标身份。"), QString()};
    }
    // This is the only audit takeover in a decision. Do not silently retry the
    // result append or erase a later writer's lock after file recovery.
    const auto auditTakeover = Audit::takeOverConfirmedDeadAuditLock();
    if (auditTakeover.outcome != Audit::TakeoverOutcome::None
        && auditTakeover.outcome != Audit::TakeoverOutcome::Removed) {
        return {RecoveryOutcome::None, AuditOutcome::Failed,
            QStringLiteral("确认意图日志不可用；未开始文件恢复。"), auditTakeover.diagnosticDetail};
    }
    const auto intent = Audit::appendConfirmIntent(identity);
    if (intent.outcome != Audit::AppendOutcome::Recorded) {
        return {RecoveryOutcome::None, AuditOutcome::Failed,
            QStringLiteral("确认意图日志未确认写入并解锁；未开始文件恢复。"), intent.diagnosticDetail};
    }
    confirmationCheckpoint("intent-recorded");

    const auto recordResult = [&](Result result) {
        // NotRequested is valid for an unlocked early result; every preceding
        // operation has already checked its own release. Failed or an unknown
        // release state must never enter another lock.
        if ((result.lockRelease != LockReleaseOutcome::NotRequested
                && result.lockRelease != LockReleaseOutcome::Released)
            || ((result.recovery == RecoveryOutcome::RolledBack
                    || result.recovery == RecoveryOutcome::Finalized)
                && result.lockRelease != LockReleaseOutcome::Released)) {
            result.audit = AuditOutcome::Failed;
            result.userVisibleSummary += QStringLiteral("最终日志未写入：目标锁释放尚未确认。");
            return result;
        }
        const auto appended = Audit::appendConfirmResult(identity, result);
        result.audit = appended.outcome == Audit::AppendOutcome::Recorded
            ? AuditOutcome::Recorded : AuditOutcome::Failed;
        if (result.audit == AuditOutcome::Failed) {
            result.userVisibleSummary += QStringLiteral("恢复结果日志未确认写入。");
            result.diagnosticDetail += QStringLiteral("；结果日志：") + appended.diagnosticDetail;
        }
        return result;
    };
    const auto refused = [&](const QString& detail) {
        return recordResult(Result{RecoveryOutcome::Refused, AuditOutcome::NotRequested,
            QStringLiteral("无法安全恢复 Markdown 导出；已保留现场。"), detail});
    };
    if (destinationIdentity(target) != identity)
        return refused(QStringLiteral("意图记录后目标身份发生变化"));

    const auto destinationTakeover = takeOverConfirmedDeadDestinationLock(target);
    if ((destinationTakeover.lockRelease != LockReleaseOutcome::NotRequested
            && destinationTakeover.lockRelease != LockReleaseOutcome::Released)
        || (destinationTakeover.outcome == LockTakeoverOutcome::Removed
            && destinationTakeover.lockRelease != LockReleaseOutcome::Released)) {
        return recordResult(Result{RecoveryOutcome::Refused, AuditOutcome::NotRequested,
            QStringLiteral("目标锁接管后的释放未确认；未开始文件恢复。"),
            destinationTakeover.diagnosticDetail,
            destinationTakeover.lockRelease == LockReleaseOutcome::NotRequested
                ? LockReleaseOutcome::Failed : destinationTakeover.lockRelease});
    }
    if (destinationTakeover.outcome != LockTakeoverOutcome::None
        && destinationTakeover.outcome != LockTakeoverOutcome::Removed) {
        return recordResult(Result{
            destinationTakeover.outcome == LockTakeoverOutcome::Active
                ? RecoveryOutcome::Busy : RecoveryOutcome::Refused,
            AuditOutcome::NotRequested, QStringLiteral("目标锁不可用；已保留恢复现场。"),
            destinationTakeover.diagnosticDetail, destinationTakeover.lockRelease});
    }
    if (destinationIdentity(target) != identity)
        return refused(QStringLiteral("接管后目标身份发生变化"));

    const auto plan = preflight(target);
    confirmationCheckpoint("preflight-complete");
    if ((plan.lockRelease != LockReleaseOutcome::NotRequested
            && plan.lockRelease != LockReleaseOutcome::Released)
        || ((plan.plan == PreflightPlan::RollBack || plan.plan == PreflightPlan::Finalize)
            && plan.lockRelease != LockReleaseOutcome::Released)) {
        return recordResult(Result{RecoveryOutcome::Refused, AuditOutcome::NotRequested,
            plan.userVisibleSummary, plan.diagnosticDetail,
            plan.lockRelease == LockReleaseOutcome::NotRequested
                ? LockReleaseOutcome::Failed : plan.lockRelease});
    }
    if (plan.plan != PreflightPlan::RollBack && plan.plan != PreflightPlan::Finalize) {
        return recordResult(Result{
            plan.plan == PreflightPlan::None ? RecoveryOutcome::None
                : plan.plan == PreflightPlan::Busy ? RecoveryOutcome::Busy : RecoveryOutcome::Refused,
            AuditOutcome::NotRequested, plan.userVisibleSummary,
            plan.diagnosticDetail, plan.lockRelease});
    }
    if (destinationIdentity(target) != identity)
        return refused(QStringLiteral("执行前目标身份发生变化"));

    // executePlan reacquires the target lock and rechecks live evidence; the
    // preflight result alone never authorizes a stale state or cached digest.
    auto recovered = executePlan(target, plan.plan);
    confirmationCheckpoint("recovery-complete");
    return recordResult(recovered);
}

}
