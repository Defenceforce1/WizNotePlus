#pragma once

#include "share/WizMarkdownExportRecovery.h"
#include "share/WizMarkdownExportRecoveryCoordinator.h"
#include "share/WizMarkdownExportRecoveryAudit.h"
#include <QProcess>
#include <QThread>
#include <QLockFile>

namespace PublicationCases {
using namespace WizMarkdownExportRecovery;

inline QByteArray snapshot(const QString& root)
{
    QStringList paths;
    QDirIterator it(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories);
    while (it.hasNext()) paths << QDir(root).relativeFilePath(it.next());
    paths.sort();
    QByteArray bytes;
    for (const auto& path : paths) {
        const QString full = QDir(root).filePath(path);
        const auto info = QFileInfo(full);
        bytes += path.toUtf8() + '\0' + QByteArray::number(info.permissions()) + '\0'
            + (info.isDir() ? QByteArray("DIRECTORY") : readFile(full)) + '\0';
    }
    return bytes;
}

struct Fixture {
    QString root, target, resources;
    bool ready;
    Fixture(const QString& parent, const QString& name, bool old = true)
    {
        root = QDir(parent).filePath(QStringLiteral("publish-") + name);
        target = QDir(root).filePath(QStringLiteral("note.md"));
        resources = QDir(root).filePath(QStringLiteral("note_files"));
        ready = QDir().mkpath(root) && (!old || (QDir().mkpath(resources)
            && writeFile(target, "OLD MARKDOWN\n")
            && writeFile(QDir(resources).filePath(QStringLiteral("old.bin")), "OLD RESOURCE")));
    }
    bool oldPair() const {
        return readFile(target) == "OLD MARKDOWN\n"
            && readFile(QDir(resources).filePath(QStringLiteral("old.bin"))) == "OLD RESOURCE"
            && !QFileInfo::exists(QDir(resources).filePath(QStringLiteral("new.bin")));
    }
    bool newPair(bool same = false) const {
        return readFile(target) == (same ? "OLD MARKDOWN\n" : "NEW MARKDOWN\n")
            && readFile(QDir(resources).filePath(QStringLiteral("old.bin"))) == "OLD RESOURCE"
            && readFile(QDir(resources).filePath(QStringLiteral("new.bin"))) == "NEW RESOURCE";
    }
    Preparation prepareOwn(bool same = false, bool newResource = true) const {
        return prepare(target, same ? "OLD MARKDOWN\n" : "NEW MARKDOWN\n", [&](const QString& html) {
            if (!writeFile(html, "<p>synthetic</p>")) return false;
            if (!newResource) return true;
            const QString tree = QDir(QFileInfo(html).absolutePath()).filePath(QStringLiteral("note_files"));
            return QDir().mkpath(tree)
                && writeFile(QDir(tree).filePath(QStringLiteral("old.bin")), "OLD RESOURCE")
                && writeFile(QDir(tree).filePath(QStringLiteral("new.bin")), "NEW RESOURCE");
        });
    }
};

inline int child(const QStringList& args)
{
    qputenv("WIZ_RECOVERY_TEST_CHECKPOINT", args.at(3).toUtf8());
    qputenv("WIZ_RECOVERY_TEST_READY", args.at(4).toUtf8());
    const QString target = args.at(2);
    QString error;
    const bool ok = WizMarkdownExportTransaction::write(target,
        args.at(5) == QStringLiteral("same") ? "OLD MARKDOWN\n" : "NEW MARKDOWN\n",
        [&](const QString& html) {
            const QString tree = QDir(QFileInfo(html).absolutePath()).filePath(QStringLiteral("note_files"));
            return QDir().mkpath(tree) && writeFile(html, "<p>child</p>")
                && writeFile(QDir(tree).filePath(QStringLiteral("new.bin")), "NEW RESOURCE");
        }, error);
    return ok ? 0 : 5;
}

template<class Check>
void run(const QString& parent, Check check)
{
    for (bool same : {false, true}) {
        Fixture f(parent, same ? QStringLiteral("same-success") : QStringLiteral("success"));
        check(f.ready, "publication fixture created");
        check(QFile::setPermissions(f.target, QFileDevice::ReadOwner | QFileDevice::WriteOwner),
            "synthetic original Markdown mode set to owner-only");
        const auto prepared = f.prepareOwn(same);
        check(prepared.outcome == PreparationOutcome::Prepared && bool(prepared.publication)
            && preflight(f.target).evidenceState == QStringLiteral("prepared"),
            "prepared publication proof recognizes differing or identical old/new Markdown");
        const auto result = publishPrepared(prepared);
        check(result.outcome == PublicationOutcome::Published && result.markdownCommitted
            && result.lockRelease == LockReleaseOutcome::Released && f.newPair(same)
            && !QFileInfo::exists(recoveryDirectory(f.target)), "publication installs resources and commits body last with complete cleanup");
        check(!(QFileInfo(f.target).permissions() & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
            | QFileDevice::ReadOther | QFileDevice::WriteOther)), "publication preserves original Markdown private mode");
        const auto before = snapshot(f.root);
        check(publishPrepared(prepared).outcome == PublicationOutcome::Refused && snapshot(f.root) == before,
            "one-attempt proof cannot publish twice or recreate a finished site");
        const auto next = f.prepareOwn();
        const auto pending = snapshot(f.root);
        check(next.outcome == PreparationOutcome::Prepared
            && publishPrepared(prepared).outcome == PublicationOutcome::Refused && snapshot(f.root) == pending,
            "consumed proof cannot adopt a later preparation for the same destination");
    }
    for (bool same : {false, true}) {
        Fixture f(parent, same ? QStringLiteral("same-no-resources") : QStringLiteral("first-no-resources"), false);
        check(f.ready && (!same || writeFile(f.target, "OLD MARKDOWN\n")), "no-resource publication fixture created");
        const auto prepared = f.prepareOwn(same, false);
        const auto result = publishPrepared(prepared);
        check(result.outcome == PublicationOutcome::Published && result.markdownCommitted
            && readFile(f.target) == (same ? "OLD MARKDOWN\n" : "NEW MARKDOWN\n")
            && !QFileInfo::exists(f.resources) && !QFileInfo::exists(recoveryDirectory(f.target)),
            "first or identical-body resource-free publication succeeds without inventing resources");
    }
    for (const auto& failure : {QString(), QStringLiteral("publish-new-source-sync"),
             QStringLiteral("publish-before-markdown-commit")}) {
        Fixture f(parent, QStringLiteral("first-resources-") + failure, false);
        const auto prepared = f.prepareOwn();
        if (!failure.isEmpty()) qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", failure.toUtf8());
        const auto result = publishPrepared(prepared);
        qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
        check(f.ready && prepared.outcome == PreparationOutcome::Prepared
            && (failure.isEmpty() ? result.outcome == PublicationOutcome::Published && f.newPair()
                : result.outcome == PublicationOutcome::RolledBack && !QFileInfo::exists(f.target)
                    && !QFileInfo::exists(f.resources))
            && !QFileInfo::exists(recoveryDirectory(f.target)),
            "first resource export either publishes complete files or rolls back to both paths absent");
    }
    {
        Fixture f(parent, QStringLiteral("missing-resource-merge"));
        const auto prepared = f.prepareOwn(false, false);
        const auto before = snapshot(f.root);
        check(prepared.outcome == PreparationOutcome::Prepared
            && publishPrepared(prepared).outcome == PublicationOutcome::Refused
            && snapshot(f.root) == before && f.oldPair(), "v1 publisher refuses unsupported deletion of old resources before mutations");
    }
    for (const QString& change : {QStringLiteral("body"), QStringLiteral("staging"), QStringLiteral("replace")}) {
        Fixture f(parent, QStringLiteral("changed-") + change);
        const auto prepared = f.prepareOwn();
        bool changed = false;
        if (change == QStringLiteral("body")) changed = writeFile(f.target, "EXTERNAL BODY");
        else if (change == QStringLiteral("staging"))
            changed = writeFile(QDir(recoveryDirectory(f.target)).filePath(QStringLiteral("new-markdown")), "EXTERNAL STAGING");
        else {
            changed = QDir().rename(recoveryDirectory(f.target), QDir(f.root).filePath(QStringLiteral("older-proof-site")))
                && f.prepareOwn(true).outcome == PreparationOutcome::Prepared;
        }
        const auto before = snapshot(f.root);
        check(changed && publishPrepared(prepared).outcome == PublicationOutcome::Refused
            && snapshot(f.root) == before, "publication refuses changed live/staged files or replacement directory without adopting another site");
    }
    {
        Fixture f(parent, QStringLiteral("invalid-token"));
        auto prepared = f.prepareOwn();
        auto invalid = prepared;
        invalid.lockRelease = LockReleaseOutcome::Failed;
        const auto before = snapshot(f.root);
        check(publishPrepared({}).outcome == PublicationOutcome::Refused
            && publishPrepared(invalid).outcome == PublicationOutcome::Refused && snapshot(f.root) == before,
            "missing or unreleased proof refuses before locks or destination I/O");
        QLockFile lock(destinationLockPath(f.target));
        lock.setStaleLockTime(0);
        check(lock.tryLock(0) && publishPrepared(prepared).outcome == PublicationOutcome::Busy
            && f.oldPair(), "publication never takes over an active writer lock");
        lock.unlock();
        check(publishPrepared(prepared).outcome == PublicationOutcome::Refused && f.oldPair(),
            "busy publication attempt is consumed; no automatic retry or implicit recovery");
    }
    const QStringList preCommit{
        QStringLiteral("publish-before-reserve"), QStringLiteral("publish-old-source-sync"),
        QStringLiteral("publish-old-destination-sync"), QStringLiteral("publish-new-source-sync"),
        QStringLiteral("publish-new-destination-sync"), QStringLiteral("publish-before-markdown-commit")};
    for (bool same : {false, true}) for (const auto& point : preCommit) {
        Fixture f(parent, (same ? QStringLiteral("same-fail-") : QStringLiteral("fail-")) + point);
        const auto prepared = f.prepareOwn(same);
        qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", point.toUtf8());
        const auto result = publishPrepared(prepared);
        qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
        check(prepared.outcome == PreparationOutcome::Prepared && result.outcome == PublicationOutcome::RolledBack
            && !result.markdownCommitted && result.lockRelease == LockReleaseOutcome::Released
            && f.oldPair() && !QFileInfo::exists(recoveryDirectory(f.target)),
            "all finite pre-commit failures restore old resources even when body digests are identical");
    }
    for (const auto& point : {QStringLiteral("publish-markdown-committed"), QStringLiteral("publish-markdown-file-sync"),
             QStringLiteral("publish-markdown-parent-sync"), QStringLiteral("terminal-before-cleanup"),
             QStringLiteral("cleanup-parent-sync")}) {
        Fixture f(parent, QStringLiteral("committed-") + point);
        const auto prepared = f.prepareOwn();
        qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", point.toUtf8());
        const auto result = publishPrepared(prepared);
        qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
        check(result.outcome == PublicationOutcome::Retained && result.markdownCommitted && f.newPair()
            && !result.diagnosticDetail.isEmpty(), "post-commit sync/cleanup failure never rolls back or misreports complete new files");
        const bool missing = point == QStringLiteral("cleanup-parent-sync");
        const auto inspected = preflight(f.target);
        check(inspected.plan == (missing ? PreflightPlan::None : PreflightPlan::Finalize)
            && (missing || executePlan(f.target, PreflightPlan::Finalize).recovery == RecoveryOutcome::Finalized)
            && f.newPair(), "committed valid evidence can finalize; already-removed site returns no operation");
    }
    for (bool committed : {false, true}) {
        Fixture f(parent, committed ? QStringLiteral("unlock-after-commit") : QStringLiteral("unlock-before-commit"));
        const auto prepared = f.prepareOwn();
        if (!committed) qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", "publish-before-reserve");
        qputenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE", "1");
        const auto result = publishPrepared(prepared);
        qunsetenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE");
        qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
        check(result.outcome == PublicationOutcome::Retained && result.markdownCommitted == committed
            && result.lockRelease == LockReleaseOutcome::Failed && (committed ? f.newPair() : f.oldPair())
            && QFileInfo::exists(recoveryDirectory(f.target)), "uncertain release stops before every subsequent recovery/cleanup lock");
    }
    {
        Fixture f(parent, QStringLiteral("rollback-sync-failure"));
        const auto prepared = f.prepareOwn();
        qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", "publish-new-source-sync,return-new-source-sync");
        const auto result = publishPrepared(prepared);
        qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
        check(result.outcome == PublicationOutcome::Retained && !result.markdownCommitted
            && preflight(f.target).plan == PreflightPlan::RollBack,
            "ordinary rollback sync failure retains strict recoverable intermediate evidence without retry");
        check(executePlan(f.target, PreflightPlan::RollBack).recovery == RecoveryOutcome::RolledBack && f.oldPair(),
            "fresh explicit recovery completes retained ordinary-failure evidence");
    }

    const QStringList boundaries{QStringLiteral("publish-before-acquire"), QStringLiteral("publish-old-renamed-before-sync"),
        QStringLiteral("publish-new-installed-before-sync"), QStringLiteral("publish-markdown-committed"),
        QStringLiteral("publish-markdown-synced"), QStringLiteral("before-recovery-cleanup"),
        QStringLiteral("after-recovery-cleanup-before-parent-sync")};
    int index = 0;
    for (bool same : {false, true}) for (const auto& point : boundaries) {
        // Two additional identical-body windows cover semantic completion
        // versus a still-uninstalled tree, not another unbounded kill matrix.
        if (same && point != QStringLiteral("publish-before-acquire")
            && point != QStringLiteral("publish-new-installed-before-sync")) continue;
        Fixture f(parent, QStringLiteral("kill-%1").arg(++index));
        const QString ready = QDir(parent).filePath(QStringLiteral("publish-ready-%1").arg(index));
        QProcess writer;
        writer.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--publication-child"), f.target,
            point, ready, same ? QStringLiteral("same") : QStringLiteral("different")});
        const bool started = writer.waitForStarted(3000);
        QElapsedTimer timer;
        timer.start();
        while (started && writer.state() != QProcess::NotRunning && !QFileInfo::exists(ready) && timer.elapsed() < 8000)
            QThread::msleep(5);
        const bool reached = QFileInfo::exists(ready);
        check(reached, "actual publication child reaches each finite interruption window");
        writer.kill();
        const bool killed = writer.waitForFinished(3000) && writer.exitStatus() == QProcess::CrashExit;
        check(killed, "only the owned synthetic publication child is terminated");
        const auto before = snapshot(f.root);
        QString error;
        check(!WizMarkdownExportTransaction::write(f.target, "DO NOT RETRY", [](const QString&) { return false; }, error)
            && snapshot(f.root) == before, "ordinary retry preserves actual crash site and any dead target lock");
        qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", QDir(parent).filePath(QStringLiteral("kill-audit-%1").arg(index)).toUtf8());
        const auto recovered = WizMarkdownExportRecoveryCoordinator::decide(f.target, Decision::Confirm);
        const bool early = point == boundaries.at(0) || (!same && boundaries.indexOf(point) <= 2);
        const bool cleaned = point == boundaries.constLast();
        check(reached && recovered.recovery == (cleaned ? RecoveryOutcome::None
                : early ? RecoveryOutcome::RolledBack : RecoveryOutcome::Finalized)
            && recovered.audit == AuditOutcome::Recorded && (early ? f.oldPair() : f.newPair(same))
            && !QFileInfo::exists(recoveryDirectory(f.target)) && !QFileInfo::exists(destinationLockPath(f.target)),
            "explicit same-target recovery restores old pair or finalizes the complete new pair after actual interruption");
        const auto remaining = QDir(f.root).entryList(QDir::AllEntries | QDir::Hidden
            | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
        qInfo().noquote() << "PUBLICATION_CHILD_REMAINING" << point << (same ? "same" : "different")
                         << remaining.join(',');
    }
}
}
