#include "share/WizMarkdownExportRecovery.h"
#include "share/WizMarkdownExportRecoveryAudit.h"
#include "share/WizMarkdownExportRecoveryCoordinator.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QSet>
#include <QSysInfo>

#include <cstdio>

namespace {

int checks = 0;
int failures = 0;

void check(bool condition, const char* description)
{
    ++checks;
    if (!condition) {
        ++failures;
        std::fprintf(stderr, "FAIL: %s\n", description);
    }
}

QByteArray digest(char fill)
{
    return QByteArray(64, fill);
}

QByteArray fileDigest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file) ? hash.result().toHex() : QByteArray();
}

QByteArray directoryDigest(const QString& path)
{
    const QFileInfo root(path);
    if (root.isSymLink() || !root.isDir())
        return {};
    QStringList relativeFiles;
    const QDir directory(path);
    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::System
        | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        iterator.next();
        const QFileInfo entry = iterator.fileInfo();
        if (entry.isSymLink() || (!entry.isFile() && !entry.isDir()))
            return {};
        if (entry.isFile())
            relativeFiles.append(directory.relativeFilePath(entry.filePath()));
    }
    relativeFiles.sort(Qt::CaseSensitive);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& relative : relativeFiles) {
        QFile file(directory.filePath(relative));
        if (!file.open(QIODevice::ReadOnly))
            return {};
        const QByteArray name = relative.toUtf8();
        const QByteArray bytes = file.readAll();
        hash.addData(QByteArray::number(name.size()));
        hash.addData(QByteArrayView(":", 1));
        hash.addData(name);
        hash.addData(QByteArray::number(bytes.size()));
        hash.addData(QByteArrayView(":", 1));
        hash.addData(bytes);
    }
    return hash.result().toHex();
}

QJsonObject validManifest(const QString& destination)
{
    QString title = QFileInfo(destination).fileName();
    const int dot = title.lastIndexOf('.');
    if (dot >= 0)
        title.truncate(dot);
    return {
        {QStringLiteral("schema"), 1},
        {QStringLiteral("destinationName"), QFileInfo(destination).fileName()},
        {QStringLiteral("resourcesName"), title.toHtmlEscaped() + QStringLiteral("_files")},
        {QStringLiteral("oldMarkdownExisted"), true},
        {QStringLiteral("oldMarkdownSha256"), QString::fromLatin1(digest('a'))},
        {QStringLiteral("newMarkdownSha256"), QString::fromLatin1(digest('b'))},
        {QStringLiteral("oldResourcesExisted"), true},
        {QStringLiteral("oldResourcesSha256"), QString::fromLatin1(digest('c'))},
        {QStringLiteral("newResourcesExisted"), true},
        {QStringLiteral("newResourcesSha256"), QString::fromLatin1(digest('d'))}
    };
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || file.write(bytes) != bytes.size()) {
        return false;
    }
    file.close();
    return file.error() == QFileDevice::NoError;
}

bool writeManifest(const QString& destination, const QJsonObject& object)
{
    const QString recovery = WizMarkdownExportRecovery::recoveryDirectory(destination);
    const QString content = QDir(recovery).filePath(QStringLiteral("content"));
    const QString stagedMarkdown = QDir(recovery).filePath(
        QStringLiteral("new-markdown"));
    if (!QDir().mkpath(content)
        || (!QFileInfo::exists(stagedMarkdown)
            && !writeBytes(stagedMarkdown, QByteArrayLiteral("PLACEHOLDER\n")))) {
        return false;
    }
    if (object.value(QStringLiteral("newResourcesExisted")).isBool()
        && object.value(QStringLiteral("newResourcesExisted")).toBool()) {
        QString title = QFileInfo(destination).fileName();
        const int dot = title.lastIndexOf('.');
        if (dot >= 0)
            title.truncate(dot);
        if (!QDir().mkpath(QDir(content).filePath(
                title.toHtmlEscaped() + QStringLiteral("_files")))) {
            return false;
        }
    }
    return writeBytes(QDir(recovery).filePath(QStringLiteral("transaction.json")),
        QJsonDocument(object).toJson(QJsonDocument::Compact));
}

void clearFixture(const QString& destination)
{
    const QString recovery = WizMarkdownExportRecovery::recoveryDirectory(destination);
    const QFileInfo recoveryInfo(recovery);
    if (recoveryInfo.isSymLink() || recoveryInfo.isFile())
        QFile::remove(recovery);
    else if (recoveryInfo.isDir())
        QDir(recovery).removeRecursively();

    const QString lock = WizMarkdownExportRecovery::destinationLockPath(destination);
    const QFileInfo lockInfo(lock);
    if (lockInfo.isSymLink() || lockInfo.isFile())
        QFile::remove(lock);
}

void expectManifestRefused(const QString& destination, const QJsonObject& object,
                           const char* description)
{
    clearFixture(destination);
    check(writeManifest(destination, object), "invalid manifest fixture created");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        description);
}

struct PreflightFixture {
    QString destination;
    QString resources;
    QString recovery;
    QString stagedResources;
    QString backup;
    QString returned;
};

bool preparePreflightFixture(const QString& root, const QString& name,
                             PreflightFixture& fixture)
{
    const QString parent = QDir(root).filePath(name);
    if (!QDir().mkpath(parent))
        return false;
    fixture.destination = QDir(parent).filePath(QStringLiteral("note.md"));
    fixture.resources = QDir(parent).filePath(QStringLiteral("note_files"));
    fixture.recovery = WizMarkdownExportRecovery::recoveryDirectory(fixture.destination);
    fixture.stagedResources = QDir(fixture.recovery).filePath(
        QStringLiteral("content/note_files"));
    fixture.backup = QDir(fixture.recovery).filePath(QStringLiteral("previous-resources"));
    fixture.returned = QDir(fixture.recovery).filePath(QStringLiteral("returned-resources"));
    if (!QDir().mkpath(fixture.resources)
        || !QDir().mkpath(fixture.stagedResources)
        || !writeBytes(fixture.destination, QByteArrayLiteral("OLD MARKDOWN\n"))
        || !writeBytes(QDir(fixture.resources).filePath(QStringLiteral("old.bin")),
            QByteArrayLiteral("OLD RESOURCE"))
        || !writeBytes(QDir(fixture.recovery).filePath(QStringLiteral("new-markdown")),
            QByteArrayLiteral("NEW MARKDOWN\n"))
        || !writeBytes(QDir(fixture.stagedResources).filePath(QStringLiteral("new.bin")),
            QByteArrayLiteral("NEW RESOURCE"))) {
        return false;
    }
    QJsonObject manifest = validManifest(fixture.destination);
    manifest.insert(QStringLiteral("oldMarkdownSha256"),
        QString::fromLatin1(fileDigest(fixture.destination)));
    manifest.insert(QStringLiteral("newMarkdownSha256"),
        QString::fromLatin1(fileDigest(QDir(fixture.recovery).filePath(
            QStringLiteral("new-markdown")))));
    manifest.insert(QStringLiteral("oldResourcesSha256"),
        QString::fromLatin1(directoryDigest(fixture.resources)));
    manifest.insert(QStringLiteral("newResourcesSha256"),
        QString::fromLatin1(directoryDigest(fixture.stagedResources)));
    return writeManifest(fixture.destination, manifest);
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool oldLivePair(const PreflightFixture& fixture)
{
    return readBytes(fixture.destination) == QByteArrayLiteral("OLD MARKDOWN\n")
        && readBytes(QDir(fixture.resources).filePath(QStringLiteral("old.bin")))
            == QByteArrayLiteral("OLD RESOURCE")
        && !QFileInfo::exists(QDir(fixture.resources).filePath(
            QStringLiteral("new.bin")));
}

bool newLivePair(const PreflightFixture& fixture)
{
    return readBytes(fixture.destination) == QByteArrayLiteral("NEW MARKDOWN\n")
        && readBytes(QDir(fixture.resources).filePath(QStringLiteral("new.bin")))
            == QByteArrayLiteral("NEW RESOURCE")
        && !QFileInfo::exists(QDir(fixture.resources).filePath(
            QStringLiteral("old.bin")));
}

}

void testAudit(const QString& root, const QString& executable)
{
    using namespace WizMarkdownExportRecoveryAudit;
    const QString base = QDir(root).filePath(QStringLiteral("audit-app-data"));
    qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", base.toUtf8());
    const QString identity = QStringLiteral("0123456789abcdef01234567");
    check(auditDirectory() == QDir(base).filePath(QStringLiteral("recovery-audit/v1")),
        "audit uses only the test app-local data location");
    check(auditLockPath() != WizMarkdownExportRecovery::destinationLockPath(
            QDir(root).filePath(QStringLiteral("audit-note.md"))),
        "audit and destination locks have independent paths");
    check(appendConfirmIntent(QStringLiteral("invalid/path")).outcome == AppendOutcome::Refused
            && !QFileInfo::exists(base),
        "invalid identity is refused before creating app data");
    using WizMarkdownExportRecovery::Result;
    using WizMarkdownExportRecovery::RecoveryOutcome;
    using WizMarkdownExportRecovery::LockReleaseOutcome;
    Result unconfirmed;
    unconfirmed.recovery = RecoveryOutcome::RolledBack;
    check(appendConfirmResult(identity, unconfirmed).outcome == AppendOutcome::Refused
            && !QFileInfo::exists(base),
        "completed recovery without confirmed destination unlock refuses result audit before I/O");
    unconfirmed.lockRelease = LockReleaseOutcome::Failed;
    check(appendConfirmResult(identity, unconfirmed).outcome == AppendOutcome::Refused
            && !QFileInfo::exists(base),
        "failed destination unlock cannot acquire audit lock or create app data");
    check(appendConfirmIntent(identity).outcome == AppendOutcome::Recorded,
        "audit intent is atomically persisted and its lock released");
    check(!QFileInfo::exists(auditLockPath()), "successful audit append leaves no audit lock");
    const auto publicPermissions = QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther
        | QFileDevice::ExeOther;
    check(!(QFileInfo(auditDirectory()).permissions() & publicPermissions),
        "audit directory has no group or other permissions");
    check(!(QFileInfo(auditLogPath()).permissions()
            & (publicPermissions | QFileDevice::ExeOwner)),
        "audit log has private non-executable permissions");
    const QByteArray first = readBytes(auditLogPath());
    const QJsonObject event = QJsonDocument::fromJson(first.trimmed()).object();
    check(first.endsWith('\n') && first.count('\n') == 1
            && event.keys() == QStringList({QStringLiteral("decision"),
                QStringLiteral("destinationIdentity"), QStringLiteral("event")})
            && event.value(QStringLiteral("destinationIdentity")).toString() == identity
            && event.value(QStringLiteral("decision")).toString() == QStringLiteral("confirm")
            && event.value(QStringLiteral("event")).toString() == QStringLiteral("requested")
            && !first.contains(root.toUtf8()),
        "audit contains exactly identity decision event and no path or content");
    check(appendConfirmIntent(identity).outcome == AppendOutcome::Recorded
            && readBytes(auditLogPath()) == first + first,
        "second audit intent preserves the complete first event");

    const QList<QByteArray> invalidLogs{
        first.left(first.size() - 1),
        QByteArrayLiteral("not json\n"),
        QByteArrayLiteral("{\"decision\":\"confirm\",\"destinationIdentity\":\"0123456789abcdef01234567\",\"event\":\"requested\",\"path\":\"secret\"}\n"),
        QByteArrayLiteral("{\"decision\":\"retain\",\"destinationIdentity\":\"0123456789abcdef01234567\",\"event\":\"requested\"}\n"),
        QByteArrayLiteral("{\"decision\":\"confirm\",\"decision\":\"confirm\",\"destinationIdentity\":\"0123456789abcdef01234567\",\"event\":\"requested\"}\n"),
        QByteArrayLiteral("{\"decision\":\"confirm\",\"destinationIdentity\":\"0123456789abcdef01234567\",\"event\":\"unknown\"}\n")
    };
    for (const QByteArray& invalid : invalidLogs) {
        check(writeBytes(auditLogPath(), invalid), "invalid audit fixture written");
        check(appendConfirmIntent(identity).outcome == AppendOutcome::Refused
                && readBytes(auditLogPath()) == invalid && !QFileInfo::exists(auditLockPath()),
            "invalid audit bytes are retained and cannot authorize confirmation");
    }
    QByteArray full;
    for (int index = 0; index < 256; ++index)
        full += first;
    check(writeBytes(auditLogPath(), full)
            && appendConfirmIntent(identity).outcome == AppendOutcome::Recorded
            && readBytes(auditLogPath()) == first && readBytes(auditArchivePath(1)) == full,
        "entry limit archives the complete live segment before committing the next event");
    const QByteArray oversized(65537, 'x');
    check(writeBytes(auditLogPath(), oversized)
            && appendConfirmIntent(identity).outcome == AppendOutcome::Refused
            && readBytes(auditLogPath()) == oversized,
        "byte limit refuses an oversized existing audit unchanged");
    check(QFile::remove(auditLogPath()), "temporary oversized audit removed");
    const QString external = QDir(root).filePath(QStringLiteral("audit-link-target"));
    check(writeBytes(external, QByteArrayLiteral("UNCHANGED"))
            && QFile::link(external, auditLogPath()), "audit log link fixture created");
    check(appendConfirmIntent(identity).outcome == AppendOutcome::Refused
            && readBytes(external) == QByteArrayLiteral("UNCHANGED")
            && QFileInfo(auditLogPath()).isSymLink(),
        "audit refuses a linked log and preserves its target");
    check(QFile::remove(auditLogPath()) && writeBytes(auditLogPath(), first),
        "private audit fixture restored");
    check(writeBytes(auditLockPath(), QByteArrayLiteral("malformed")),
        "malformed audit lock fixture created");
    check(appendConfirmIntent(identity).outcome == AppendOutcome::Refused
            && readBytes(auditLockPath()) == QByteArrayLiteral("malformed")
            && readBytes(auditLogPath()) == first,
        "malformed audit lock and log are preserved");
    check(QFile::remove(auditLockPath()), "temporary malformed audit lock removed");

    QProcess writer;
    writer.start(executable, {QStringLiteral("--hold-destination-lock"), auditLockPath()});
    const bool ready = writer.waitForStarted(3000) && writer.waitForReadyRead(3000)
        && writer.readAllStandardOutput().contains("LOCK_READY");
    check(ready, "separate process owns the audit lock");
    check(ready && appendConfirmIntent(identity).outcome == AppendOutcome::Busy
            && readBytes(auditLogPath()) == first,
        "active audit writer blocks append unchanged");
    qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA",
        QDir(root).filePath(QStringLiteral("other-audit-data")).toUtf8());
    check(appendConfirmIntent(identity).outcome == AppendOutcome::Recorded,
        "independent app-local audit is not blocked by another root");
    qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", base.toUtf8());
    writer.kill();
    const bool killed = writer.waitForFinished(3000) && writer.exitStatus() == QProcess::CrashExit;
    check(killed && appendConfirmIntent(identity).outcome == AppendOutcome::Busy
            && QFileInfo::exists(auditLockPath()) && readBytes(auditLogPath()) == first,
        "dead audit owner is retained and never silently taken over");
    QLockFile stale(auditLockPath());
    stale.setStaleLockTime(0);
    check(killed && stale.removeStaleLockFile(), "test explicitly removes its own dead audit lock");
    check(appendConfirmIntent(identity).outcome == AppendOutcome::Recorded
            && readBytes(auditLogPath()) == first + first,
        "audit append resumes only after explicit test cleanup");
    qputenv("WIZ_RECOVERY_TEST_AUDIT_RELEASE_FAILURE", "1");
    check(appendConfirmIntent(identity).outcome == AppendOutcome::Failed
            && readBytes(auditLogPath()) == first + first + first,
        "committed intent with unconfirmed lock-release durability is not Recorded");
    qunsetenv("WIZ_RECOVERY_TEST_AUDIT_RELEASE_FAILURE");
    check(!QFileInfo::exists(auditLockPath()),
        "release failure is reported separately from already committed audit bytes");

    QByteArray expected = readBytes(auditLogPath());
    const QList<RecoveryOutcome> outcomes{RecoveryOutcome::None, RecoveryOutcome::RolledBack,
        RecoveryOutcome::Finalized, RecoveryOutcome::Busy, RecoveryOutcome::Refused};
    const QStringList names{QStringLiteral("none"), QStringLiteral("rolled-back"),
        QStringLiteral("finalized"), QStringLiteral("busy"), QStringLiteral("refused")};
    for (qsizetype i = 0; i < outcomes.size(); ++i) {
        Result result;
        result.recovery = outcomes[i];result.lockRelease = LockReleaseOutcome::Released;
        result.diagnosticDetail = root + QStringLiteral("/PRIVATE-NOTE-CONTENT");
        const auto appended = appendConfirmResult(identity, result);
        const QByteArray line = QJsonDocument(QJsonObject{
            {QStringLiteral("destinationIdentity"), identity},
            {QStringLiteral("decision"), QStringLiteral("confirm")},
            {QStringLiteral("event"), names[i]}}).toJson(QJsonDocument::Compact) + '\n';
        expected += line;
        check(appended.outcome == AppendOutcome::Recorded && readBytes(auditLogPath()) == expected
                && !QFileInfo::exists(auditLockPath()),
            "all finite result events append canonically without changing prior intents");
        check(!readBytes(auditLogPath()).contains(root.toUtf8())
                && !readBytes(auditLogPath()).contains("PRIVATE-NOTE-CONTENT")
                && result.recovery == outcomes[i]
                && result.audit == WizMarkdownExportRecovery::AuditOutcome::NotRequested,
            "result audit excludes diagnostics and never mutates the independent recovery result");
    }
    check(appendConfirmIntent(identity).outcome == AppendOutcome::Recorded
            && readBytes(auditLogPath()) == expected + first,
        "subsequent intent validates and preserves all accepted result events");
    expected += first;
    Result invalid;
    invalid.recovery = static_cast<RecoveryOutcome>(99);
    check(appendConfirmResult(identity, invalid).outcome == AppendOutcome::Refused
            && readBytes(auditLogPath()) == expected,
        "unknown recovery enum refuses without changing audit");
    invalid.recovery = RecoveryOutcome::Refused;
    invalid.lockRelease = static_cast<LockReleaseOutcome>(99);
    check(appendConfirmResult(identity, invalid).outcome == AppendOutcome::Refused
            && readBytes(auditLogPath()) == expected,
        "unknown unlock result cannot permit result audit");
    invalid.lockRelease = LockReleaseOutcome::NotRequested;
    check(appendConfirmResult(identity, invalid).outcome == AppendOutcome::Recorded,
        "refusal before acquiring a destination lock can be recorded without claiming recovery");

    PreflightFixture completed;
    check(preparePreflightFixture(root, QStringLiteral("audit-completed-recovery"), completed),
        "result audit uses a real temporary prepared recovery site");
    const auto recovered = WizMarkdownExportRecovery::executePlan(completed.destination,
        WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(recovered.recovery == RecoveryOutcome::RolledBack
            && recovered.lockRelease == LockReleaseOutcome::Released && oldLivePair(completed),
        "actual recovery completes and releases destination lock before any result append");
    const QByteArray beforeBusy = readBytes(auditLogPath());
    QLockFile held(auditLockPath());held.setStaleLockTime(0);
    const bool heldOk = held.tryLock(0);
    check(heldOk && appendConfirmResult(identity, recovered).outcome == AppendOutcome::Busy
            && readBytes(auditLogPath()) == beforeBusy && oldLivePair(completed)
            && !QFileInfo::exists(completed.recovery)
            && recovered.recovery == RecoveryOutcome::RolledBack,
        "post-recovery busy audit cannot undo completed files or misreport recovery");
    held.unlock();
    check(appendConfirmResult(identity, recovered).outcome == AppendOutcome::Recorded
            && oldLivePair(completed) && !QFileInfo::exists(completed.recovery),
        "result-only retry records completed recovery without executing recovery again");
    const QByteArray beforeFailedRelease = readBytes(auditLogPath());
    qputenv("WIZ_RECOVERY_TEST_AUDIT_RELEASE_FAILURE", "1");
    check(appendConfirmResult(identity, recovered).outcome == AppendOutcome::Failed
            && readBytes(auditLogPath()).size() > beforeFailedRelease.size()
            && oldLivePair(completed) && recovered.recovery == RecoveryOutcome::RolledBack,
        "committed result with audit unlock failure preserves the completed recovery outcome");
    qunsetenv("WIZ_RECOVERY_TEST_AUDIT_RELEASE_FAILURE");
    check(writeBytes(auditLogPath(), full)
            && appendConfirmResult(identity, recovered).outcome == AppendOutcome::Recorded
            && readBytes(auditLogPath()).count('\n') == 1
            && QJsonDocument::fromJson(readBytes(auditLogPath())).object()
                .value(QStringLiteral("event")).toString() == QStringLiteral("rolled-back")
            && readBytes(auditArchivePath(1)) == full && readBytes(auditArchivePath(2)) == full
            && oldLivePair(completed),
        "result audit rotates full segments without repeating or changing completed recovery");
}

QString auditTestIdentity(int index)
{
    return QStringLiteral("%1").arg(index, 24, 16, QLatin1Char('0'));
}

QByteArray auditTestLines(int first, int last)
{
    QByteArray bytes;
    for (int index = first; index <= last; ++index)
        bytes += QJsonDocument(QJsonObject{
            {QStringLiteral("destinationIdentity"), auditTestIdentity(index)},
            {QStringLiteral("decision"), QStringLiteral("confirm")},
            {QStringLiteral("event"), QStringLiteral("requested")}
        }).toJson(QJsonDocument::Compact) + '\n';
    return bytes;
}

QByteArray auditManagedBytes()
{
    using namespace WizMarkdownExportRecoveryAudit;
    QByteArray bytes;
    for (int index : {2, 1, 0}) {
        const QString path = index ? auditArchivePath(index) : auditLogPath();
        if (QFileInfo::exists(path))
            bytes += readBytes(path);
    }
    return bytes;
}

QByteArray auditManagedSnapshot()
{
    using namespace WizMarkdownExportRecoveryAudit;
    QByteArray snapshot;
    for (int index : {0, 1, 2}) {
        const QString path = index ? auditArchivePath(index) : auditLogPath();
        const QFileInfo info(path);
        snapshot += QByteArray::number(index) + ':' + QByteArray::number(info.exists()) + ':'
            + QByteArray::number(info.isSymLink()) + ':' + QByteArray::number(info.isDir()) + '\n';
        snapshot += readBytes(path) + '\n';
    }
    return snapshot;
}

bool seedFullAudit(const QString& base)
{
    using namespace WizMarkdownExportRecoveryAudit;
    qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", base.toUtf8());
    if (appendConfirmIntent(auditTestIdentity(513)).outcome != AppendOutcome::Recorded)
        return false;
    return writeBytes(auditArchivePath(2), auditTestLines(1, 256))
        && writeBytes(auditArchivePath(1), auditTestLines(257, 512))
        && writeBytes(auditLogPath(), auditTestLines(513, 768))
        && QFile::setPermissions(auditArchivePath(2), QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        && QFile::setPermissions(auditArchivePath(1), QFileDevice::ReadOwner | QFileDevice::WriteOwner);
}

bool privateAuditSegments()
{
    using namespace WizMarkdownExportRecoveryAudit;
    const auto forbidden = QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther
        | QFileDevice::ExeOwner | QFileDevice::ExeUser;
    for (int index : {0, 1, 2}) {
        const QString path = index ? auditArchivePath(index) : auditLogPath();
        if (!QFileInfo::exists(path))
            continue;
        const QFileInfo info(path);
        if (info.isSymLink() || !info.isFile() || (info.permissions() & forbidden)
            || !info.permissions().testFlag(QFileDevice::ReadOwner)
            || !info.permissions().testFlag(QFileDevice::WriteOwner))
            return false;
    }
    return true;
}

void testAuditRotation(const QString& root, const QString& executable)
{
    using namespace WizMarkdownExportRecoveryAudit;
    check(auditArchivePath(0).isEmpty() && auditArchivePath(3).isEmpty()
            && auditArchivePath(-1).isEmpty(),
        "audit exposes only its two owned archive slots");
    const QString base = QDir(root).filePath(QStringLiteral("bounded-product-audit"));
    qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", base.toUtf8());
    check(appendConfirmIntent(auditTestIdentity(1)).outcome == AppendOutcome::Recorded
            && writeBytes(auditLogPath(), auditTestLines(1, 256)),
        "canonical 256-entry live audit fixture prepared");
    check(appendConfirmIntent(auditTestIdentity(257)).outcome == AppendOutcome::Recorded
            && readBytes(auditArchivePath(1)) == auditTestLines(1, 256)
            && !QFileInfo::exists(auditArchivePath(2))
            && readBytes(auditLogPath()) == auditTestLines(257, 257),
        "first full segment is archived without losing any event");
    check(writeBytes(auditLogPath(), auditTestLines(257, 512))
            && appendConfirmIntent(auditTestIdentity(513)).outcome == AppendOutcome::Recorded
            && auditManagedBytes() == auditTestLines(1, 513),
        "second rotation preserves chronological order across both archives and live log");
    check(writeBytes(auditLogPath(), auditTestLines(513, 768))
            && appendConfirmIntent(auditTestIdentity(769)).outcome == AppendOutcome::Recorded
            && auditManagedBytes() == auditTestLines(257, 769),
        "third rotation discards exactly the oldest bounded segment and retains the newest 513 events");
    check(privateAuditSegments() && !QFileInfo::exists(auditLockPath())
            && !auditManagedBytes().contains(root.toUtf8()),
        "all managed segments remain private and exclude note paths after rotation");
    check(QFile::setPermissions(auditArchivePath(2), QFileDevice::ReadOwner | QFileDevice::WriteOwner
                | QFileDevice::ReadGroup)
            && appendConfirmIntent(auditTestIdentity(770)).outcome == AppendOutcome::Recorded
            && privateAuditSegments() && auditManagedBytes() == auditTestLines(257, 770),
        "validated retained archive permissions are restored to owner-only without changing history");

    for (const QString& kind : {QStringLiteral("corrupt"), QStringLiteral("oversized"),
             QStringLiteral("empty"), QStringLiteral("linked"), QStringLiteral("directory"),
             QStringLiteral("unknown"), QStringLiteral("over-entries")}) {
        const QString location = QDir(root).filePath(QStringLiteral("audit-refuse-") + kind);
        check(seedFullAudit(location), "independent full archive refusal fixture prepared");
        const QString slot = auditArchivePath(2);
        QString external;
        bool changed = false;
        if (kind == QStringLiteral("corrupt"))
            changed = writeBytes(slot, QByteArrayLiteral("{truncated\n"));
        else if (kind == QStringLiteral("oversized"))
            changed = writeBytes(slot, QByteArray(65537, 'x'));
        else if (kind == QStringLiteral("empty"))
            changed = writeBytes(slot, {});
        else if (kind == QStringLiteral("linked")) {
            external = QDir(root).filePath(QStringLiteral("audit-external-target"));
            changed = writeBytes(external, QByteArrayLiteral("UNCHANGED\n"))
                && QFile::remove(slot) && QFile::link(external, slot);
        } else if (kind == QStringLiteral("directory"))
            changed = QFile::remove(slot) && QDir().mkdir(slot);
        else if (kind == QStringLiteral("unknown"))
            changed = writeBytes(auditLogPath() + QStringLiteral(".3"), QByteArrayLiteral("UNKNOWN\n"));
        else
            changed = writeBytes(auditLogPath(), auditTestLines(1, 257));
        check(changed, "unsupported managed audit shape written in synthetic fixture");
        const QByteArray before = auditManagedSnapshot();
        check(appendConfirmIntent(auditTestIdentity(769)).outcome == AppendOutcome::Refused
                && auditManagedSnapshot() == before && !QFileInfo::exists(auditLockPath())
                && (external.isEmpty() || readBytes(external) == "UNCHANGED\n")
                && (kind != QStringLiteral("unknown")
                    || readBytes(auditLogPath() + QStringLiteral(".3")) == "UNKNOWN\n"),
            "all retained audit segments are validated before any history is removed or moved");
    }
    for (bool missingLive : {false, true}) {
        check(seedFullAudit(QDir(root).filePath(missingLive ? QStringLiteral("audit-gap-absent")
                : QStringLiteral("audit-gap-partial")))
                && QFile::remove(auditArchivePath(1))
                && (missingLive ? QFile::remove(auditLogPath())
                    : writeBytes(auditLogPath(), auditTestLines(513, 513))),
            "unexplained archive-gap fixture prepared");
        const QByteArray before = auditManagedSnapshot();
        check(appendConfirmIntent(auditTestIdentity(769)).outcome == AppendOutcome::Refused
                && auditManagedSnapshot() == before,
            "archive gap without a full live segment refuses and preserves unknown history");
    }
    check(seedFullAudit(QDir(root).filePath(QStringLiteral("audit-resume-gap")))
            && QFile::remove(auditArchivePath(2))
            && QFile::rename(auditArchivePath(1), auditArchivePath(2)),
        "single known interrupted descending shift fixture prepared");
    check(appendConfirmIntent(auditTestIdentity(769)).outcome == AppendOutcome::Recorded
            && auditManagedBytes() == auditTestLines(257, 769),
        "known shift gap resumes without dropping the already moved retained archive");

    const QStringList checkpoints{QStringLiteral("lock-acquired"), QStringLiteral("oldest-removed"),
        QStringLiteral("archive-1-to-2"), QStringLiteral("live-archived"),
        QStringLiteral("live-committed"), QStringLiteral("live-file-synced"),
        QStringLiteral("live-directory-synced")};
    const auto committed = [](const QString& checkpoint) {
        return checkpoint == QStringLiteral("live-committed")
            || checkpoint == QStringLiteral("live-file-synced")
            || checkpoint == QStringLiteral("live-directory-synced");
    };
    for (const QString& checkpoint : checkpoints + QStringList{QStringLiteral("before-live-commit")}) {
        check(seedFullAudit(QDir(root).filePath(QStringLiteral("audit-fault-") + checkpoint)),
            "checkpoint-failure audit history prepared");
        qputenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT", checkpoint.toUtf8());
        const auto failed = appendConfirmIntent(auditTestIdentity(769));
        qunsetenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT");
        const int start = checkpoint == QStringLiteral("lock-acquired") ? 1 : 257;
        const int last = committed(checkpoint) ? 769 : 768;
        check(failed.outcome == AppendOutcome::Failed && !QFileInfo::exists(auditLockPath())
                && auditManagedBytes() == auditTestLines(start, last),
            "failed checkpoint preserves the exact explainable bounded prefix and reports failure");
        check(appendConfirmIntent(auditTestIdentity(last + 1)).outcome == AppendOutcome::Recorded
                && auditManagedBytes() == auditTestLines(257, last + 1) && privateAuditSegments(),
            "ordinary retry completes interrupted rotation without losing a retained segment");
    }

    for (const QString& checkpoint : checkpoints) {
        const QString location = QDir(root).filePath(QStringLiteral("audit-kill-") + checkpoint);
        check(seedFullAudit(location), "real child termination audit history prepared");
        const QString ready = QDir(root).filePath(QStringLiteral("audit-ready-") + checkpoint);
        QProcess writer;
        writer.start(executable, {QStringLiteral("--append-audit-and-hold"), location,
            auditTestIdentity(769), checkpoint, ready});
        bool reached = writer.waitForStarted(3000);
        QElapsedTimer timer;
        timer.start();
        while (reached && !QFileInfo::exists(ready) && timer.elapsed() < 3000)
            writer.waitForReadyRead(10);
        reached = reached && readBytes(ready) == checkpoint.toUtf8();
        const QByteArray before = auditManagedSnapshot();
        check(reached && appendConfirmIntent(auditTestIdentity(999)).outcome == AppendOutcome::Busy
                && auditManagedSnapshot() == before,
            "active product audit writer excludes another writer at every rotation boundary");
        const qint64 childPid = writer.processId();
        if (reached)
            writer.kill();
        const bool killed = writer.waitForFinished(3000) && writer.exitStatus() == QProcess::CrashExit;
        check(reached && killed, "own child is terminated at the exact requested product audit boundary");
        const int start = checkpoint == QStringLiteral("lock-acquired") ? 1 : 257;
        const int last = committed(checkpoint) ? 769 : 768;
        check(killed && auditManagedBytes() == auditTestLines(start, last) && privateAuditSegments(),
            "process interruption retains the exact valid bounded event prefix");
        check(appendConfirmIntent(auditTestIdentity(last + 1)).outcome == AppendOutcome::Busy
                && auditManagedSnapshot() == before && QFileInfo::exists(auditLockPath()),
            "dead audit lock is retained and no default append resumes the rotation");
        QLockFile ownDead(auditLockPath());
        ownDead.setStaleLockTime(0);
        qint64 pid = 0;
        QString host, application;
        const bool knownDead = ownDead.getLockInfo(&pid, &host, &application)
            && pid > 0 && pid == childPid && host == QSysInfo::machineHostName()
            && application == QCoreApplication::applicationName();
        check(killed && knownDead
                && takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::Removed,
            "explicit product takeover removes only its own terminated child's identified audit lock");
        check(appendConfirmIntent(auditTestIdentity(last + 1)).outcome == AppendOutcome::Recorded
                && auditManagedBytes() == auditTestLines(257, last + 1)
                && !QFileInfo::exists(auditLockPath()) && privateAuditSegments(),
            "after explicit product lock takeover the known interrupted prefix resumes exactly");
    }
    qunsetenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA");
}

void testAuditTakeover(const QString& root, const QString& executable)
{
    using namespace WizMarkdownExportRecoveryAudit;
    const QString absent = QDir(root).filePath(QStringLiteral("audit-takeover-absent"));
    qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", absent.toUtf8());
    check(takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::None
            && !QFileInfo::exists(absent),
        "absent audit takeover does not create directories or log files");
    check(QDir().mkpath(absent)
            && takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::None
            && QDir(absent).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(),
        "missing audit parent remains absent without directory creation");
    check(QDir().mkpath(QFileInfo(auditDirectory()).absolutePath())
            && takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::None
            && !QFileInfo::exists(auditDirectory()),
        "missing audit version root is not created by explicit takeover");
    check(QDir().mkpath(auditDirectory())
            && takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::None
            && QDir(auditDirectory()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty(),
        "empty existing audit root remains empty when no lock is present");
    check(QDir().rmdir(auditDirectory()) && writeBytes(auditDirectory(), QByteArrayLiteral("KEEP"))
            && takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::Refused
            && readBytes(auditDirectory()) == "KEEP",
        "non-directory audit root refuses without modifying it");

    const auto deadFixture = [&](const QString& base) {
        qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", base.toUtf8());
        if (appendConfirmIntent(auditTestIdentity(1)).outcome != AppendOutcome::Recorded)
            return false;
        QProcess holder;
        holder.start(executable, {QStringLiteral("--hold-destination-lock"), auditLockPath()});
        const bool ready = holder.waitForStarted(3000) && holder.waitForReadyRead(3000)
            && holder.readAllStandardOutput().contains("LOCK_READY");
        const qint64 pid = holder.processId();
        const QByteArray lock = readBytes(auditLockPath());
        const bool blocked = ready
            && takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::Active
            && readBytes(auditLockPath()) == lock;
        holder.kill();
        const bool ended = holder.waitForFinished(3000);
        return blocked && ended && holder.exitStatus() == QProcess::CrashExit
            && lock.startsWith(QByteArray::number(pid) + '\n');
    };
    check(deadFixture(QDir(root).filePath(QStringLiteral("audit-takeover-dead"))),
        "real active audit owner refuses takeover before its synthetic child is killed");
    const QByteArray before = auditManagedSnapshot();
    check(appendConfirmIntent(auditTestIdentity(2)).outcome == AppendOutcome::Busy
            && takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::Removed
            && !QFileInfo::exists(auditLockPath()) && auditManagedSnapshot() == before,
        "explicit dead audit takeover changes only the lock and never writes an audit event");
    check(takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::None
            && appendConfirmIntent(auditTestIdentity(2)).outcome == AppendOutcome::Recorded
            && auditManagedBytes() == auditTestLines(1, 2),
        "takeover is idempotent and a fresh append independently revalidates the log");

    check(deadFixture(QDir(root).filePath(QStringLiteral("audit-takeover-corrupt-log")))
            && writeBytes(auditLogPath(), QByteArrayLiteral("INVALID HISTORY\n")),
        "dead lock with corrupt audit history fixture prepared");
    check(takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::Removed
            && readBytes(auditLogPath()) == "INVALID HISTORY\n"
            && appendConfirmIntent(auditTestIdentity(2)).outcome == AppendOutcome::Refused
            && readBytes(auditLogPath()) == "INVALID HISTORY\n"
            && !QFileInfo::exists(auditLockPath()),
        "lock takeover does not authorize a corrupt log or bypass fresh append validation");

    for (const QString& kind : {QStringLiteral("malformed"), QStringLiteral("foreign-host"),
             QStringLiteral("other-app"), QStringLiteral("link"), QStringLiteral("directory")}) {
        check(deadFixture(QDir(root).filePath(QStringLiteral("audit-takeover-refuse-") + kind)),
            "independent identified dead audit lock refusal fixture prepared");
        const QString lock = auditLockPath();
        const QByteArray bytes = readBytes(lock);
        QString external;
        bool changed = false;
        if (kind == QStringLiteral("malformed"))
            changed = writeBytes(lock, QByteArrayLiteral("INVALID\n"));
        else if (kind == QStringLiteral("foreign-host") || kind == QStringLiteral("other-app")) {
            QList<QByteArray> lines = bytes.split('\n');
            lines[kind == QStringLiteral("foreign-host") ? 2 : 1] = "not-this-owner.invalid";
            changed = writeBytes(lock, lines.join('\n'));
        } else if (kind == QStringLiteral("link")) {
            external = QDir(root).filePath(QStringLiteral("takeover-external-lock"));
            changed = writeBytes(external, bytes) && QFile::remove(lock) && QFile::link(external, lock);
        } else
            changed = QFile::remove(lock) && QDir().mkdir(lock);
        check(changed, "unsupported audit lock shape written only in isolated fixture");
        const QByteArray snapshot = auditManagedSnapshot();
        const QByteArray lockBefore = readBytes(lock);
        check(takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::Refused
                && auditManagedSnapshot() == snapshot && QFileInfo::exists(lock)
                && readBytes(lock) == lockBefore
                && (external.isEmpty() || readBytes(external) == bytes),
            "unknown owner or unsupported lock is preserved without changing audit history");
    }

    for (const QString& boundary : {QStringLiteral("takeover-opened"),
             QStringLiteral("takeover-before-unlink"), QStringLiteral("takeover-unlinked")}) {
        check(deadFixture(QDir(root).filePath(QStringLiteral("audit-takeover-fault-") + boundary)),
            "explicit takeover fault fixture has its own ended writer");
        const QByteArray snapshot = auditManagedSnapshot();
        qputenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT", boundary.toUtf8());
        const auto failed = takeOverConfirmedDeadAuditLock();
        qunsetenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT");
        const bool unlinked = boundary == QStringLiteral("takeover-unlinked");
        check(failed.outcome == TakeoverOutcome::Failed
                && QFileInfo::exists(auditLockPath()) != unlinked
                && auditManagedSnapshot() == snapshot,
            "takeover failure reports uncertain durability separately and never claims recovery success");
        check(takeOverConfirmedDeadAuditLock().outcome
                == (unlinked ? TakeoverOutcome::None : TakeoverOutcome::Removed)
                && appendConfirmIntent(auditTestIdentity(2)).outcome == AppendOutcome::Recorded,
            "after takeover failure fresh classification and append determine the actual state");
    }

    for (const QString& change : {QStringLiteral("inode"), QStringLiteral("content"),
             QStringLiteral("active"), QStringLiteral("root")}) {
        const QString base = QDir(root).filePath(QStringLiteral("audit-takeover-change-") + change);
        check(deadFixture(base), "takeover revalidation fixture has its own ended writer");
        const QString lock = auditLockPath();
        const QString directory = auditDirectory();
        const QString moved = directory + QStringLiteral("-retained");
        const QByteArray originalLock = readBytes(lock);
        const QByteArray snapshot = auditManagedSnapshot();
        const QString ready = QDir(root).filePath(QStringLiteral("takeover-ready-") + change);
        const QString resume = ready + QStringLiteral("-resume");
        QProcess takeover;
        takeover.start(executable, {QStringLiteral("--takeover-audit-and-hold"), base, ready, resume});
        bool reached = takeover.waitForStarted(3000);
        QElapsedTimer timer;
        timer.start();
        while (reached && !QFileInfo::exists(ready) && timer.elapsed() < 3000)
            takeover.waitForReadyRead(10);
        reached = reached && readBytes(ready) == "takeover-opened";
        check(reached && takeOverConfirmedDeadAuditLock().outcome == TakeoverOutcome::Active
                && readBytes(lock) == originalLock && auditManagedSnapshot() == snapshot,
            "another explicit takeover refuses nonblockingly while the old inode is held");
        bool changed = false;
        QByteArray replacement = originalLock;
        if (reached && change == QStringLiteral("inode"))
            changed = QFile::rename(lock, lock + QStringLiteral("-retained"))
                && writeBytes(lock, replacement);
        else if (reached && change == QStringLiteral("content")) {
            replacement += "CHANGED\n";
            changed = writeBytes(lock, replacement);
        } else if (reached && change == QStringLiteral("active")) {
            auto lines = replacement.split('\n');
            lines[0] = QByteArray::number(QCoreApplication::applicationPid());
            replacement = lines.join('\n');
            changed = writeBytes(lock, replacement);
        } else if (reached)
            changed = QDir().rename(directory, moved) && QDir().mkdir(directory);
        check(reached && changed && writeBytes(resume, QByteArrayLiteral("CONTINUE")),
            "controlled lock or directory change occurs after the actual takeover snapshot");
        const bool finished = takeover.waitForFinished(3000);
        if (!finished) {
            takeover.kill();
            takeover.waitForFinished(3000);
        }
        const auto expected = change == QStringLiteral("active")
            ? TakeoverOutcome::Active : TakeoverOutcome::Refused;
        const QByteArray output = takeover.readAllStandardOutput();
        check(finished && takeover.exitCode() == 0
                && output.contains("TAKEOVER_OUTCOME=" + QByteArray::number(int(expected)))
                && (change == QStringLiteral("root")
                    ? readBytes(QDir(moved).filePath(QStringLiteral("recovery.jsonl.lock"))) == originalLock
                        && readBytes(QDir(moved).filePath(QStringLiteral("recovery.jsonl"))) == auditTestLines(1, 1)
                        && QDir(directory).entryList(QDir::Files).isEmpty()
                    : readBytes(lock) == replacement && auditManagedSnapshot() == snapshot),
            "revalidated takeover preserves replaced inode, changed content, new active owner, or moved root");
    }
    qunsetenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA");
}

bool endSyntheticLockHolder(const QString& executable, const QString& path)
{
    QProcess holder;
    holder.start(executable, {QStringLiteral("--hold-destination-lock"), path});
    const bool ready = holder.waitForStarted(3000) && holder.waitForReadyRead(3000)
        && holder.readAllStandardOutput().contains("LOCK_READY");
    const qint64 pid = holder.processId();
    holder.kill();
    const bool ended = holder.waitForFinished(3000);
    return ready && ended && holder.exitStatus() == QProcess::CrashExit
        && readBytes(path).startsWith(QByteArray::number(pid) + '\n');
}

QStringList coordinatorAuditEvents(const QString& identity)
{
    QStringList events;
    for (const QByteArray& line : auditManagedBytes().split('\n')) {
        if (line.isEmpty())
            continue;
        const auto object = QJsonDocument::fromJson(line).object();
        if (object.value(QStringLiteral("destinationIdentity")).toString() != identity
            || object.size() != 3
            || object.value(QStringLiteral("decision")).toString() != QStringLiteral("confirm"))
            return {QStringLiteral("INVALID")};
        events.append(object.value(QStringLiteral("event")).toString());
    }
    return events;
}

void testRecoveryCoordinator(const QString& root, const QString& executable)
{
    using namespace WizMarkdownExportRecovery;
    namespace Audit = WizMarkdownExportRecoveryAudit;
    namespace Coordinator = WizMarkdownExportRecoveryCoordinator;
    const auto selectAudit = [&](const QString& name) {
        qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", QDir(root).filePath(name).toUtf8());
    };
    for (int state = 0; state < 6; ++state) {
        const QString name = QStringLiteral("coordinator-state-%1").arg(state);
        PreflightFixture fixture;
        bool prepared = preparePreflightFixture(root, name, fixture);
        if (state >= 1)
            prepared = prepared && QDir().rename(fixture.resources, fixture.backup);
        if (state >= 2)
            prepared = prepared && QDir().rename(fixture.stagedResources, fixture.resources);
        if (state == 3 || state == 4)
            prepared = prepared && QDir().rename(fixture.resources, fixture.returned);
        if (state == 4)
            prepared = prepared && QDir().rename(fixture.backup, fixture.resources);
        if (state == 5)
            prepared = prepared && writeBytes(fixture.destination, QByteArrayLiteral("NEW MARKDOWN\n"));
        check(prepared, "coordinator recognized-state fixture prepared");
        selectAudit(name + QStringLiteral("-audit"));
        const auto result = Coordinator::decide(fixture.destination, Decision::Confirm);
        const QString identity = destinationIdentity(fixture.destination);
        check(result.recovery == (state == 5 ? RecoveryOutcome::Finalized : RecoveryOutcome::RolledBack)
                && result.audit == AuditOutcome::Recorded && result.lockRelease == LockReleaseOutcome::Released
                && (state == 5 ? newLivePair(fixture) : oldLivePair(fixture))
                && !QFileInfo::exists(fixture.recovery)
                && !QFileInfo::exists(destinationLockPath(fixture.destination))
                && !QFileInfo::exists(Audit::auditLockPath())
                && coordinatorAuditEvents(identity) == QStringList{QStringLiteral("requested"),
                    state == 5 ? QStringLiteral("finalized") : QStringLiteral("rolled-back")}
                && !auditManagedBytes().contains(root.toUtf8()),
            "confirmed coordinator recovers exact recognized state and records intent then finite result");
        const auto repeated = Coordinator::decide(fixture.destination, Decision::Confirm);
        check(repeated.recovery == RecoveryOutcome::None && repeated.audit == AuditOutcome::Recorded
                && coordinatorAuditEvents(identity).mid(2)
                    == QStringList{QStringLiteral("requested"), QStringLiteral("none")}
                && (state == 5 ? newLivePair(fixture) : oldLivePair(fixture)),
            "repeated confirmation records none instead of repeating completed recovery");
    }

    PreflightFixture fixture;
    check(preparePreflightFixture(root, QStringLiteral("coordinator-retain"), fixture)
            && endSyntheticLockHolder(executable, destinationLockPath(fixture.destination)),
        "nonconfirm fixture retains an identified dead destination lock");
    selectAudit(QStringLiteral("coordinator-no-audit-io"));
    const QByteArray site = directoryDigest(QFileInfo(fixture.destination).absolutePath());
    for (Decision decision : {Decision::Inspect, Decision::Cancel, Decision::Retain, Decision(99)}) {
        const auto result = Coordinator::decide(fixture.destination, decision);
        check(result.audit == AuditOutcome::NotRequested
                && directoryDigest(QFileInfo(fixture.destination).absolutePath()) == site
                && !QFileInfo::exists(Audit::auditDirectory()),
            "inspect cancel retain and unknown decisions never clear locks or create audit storage");
    }
    check(Coordinator::decide(QString(), Decision::Confirm).recovery == RecoveryOutcome::Refused
            && !QFileInfo::exists(Audit::auditDirectory()),
        "empty confirm destination refuses before audit storage creation");

    for (const QString& kind : {QStringLiteral("corrupt-log"), QStringLiteral("intent-commit"),
             QStringLiteral("intent-release"), QStringLiteral("audit-active"),
             QStringLiteral("audit-takeover-failure")}) {
        const QString name = QStringLiteral("coordinator-intent-") + kind;
        check(preparePreflightFixture(root, name, fixture)
                && endSyntheticLockHolder(executable, destinationLockPath(fixture.destination)),
            "intent-failure fixture has untouched live data and a real dead destination lock");
        selectAudit(name + QStringLiteral("-audit"));
        check(Audit::appendConfirmIntent(destinationIdentity(fixture.destination)).outcome
                == Audit::AppendOutcome::Recorded,
            "independent existing audit prepared for intent failure");
        QProcess active;
        bool ready = true;
        if (kind == QStringLiteral("corrupt-log"))
            ready = writeBytes(Audit::auditLogPath(), QByteArrayLiteral("INVALID\n"));
        else if (kind == QStringLiteral("intent-commit"))
            qputenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT", "live-committed");
        else if (kind == QStringLiteral("intent-release"))
            qputenv("WIZ_RECOVERY_TEST_AUDIT_RELEASE_FAILURE", "1");
        else if (kind == QStringLiteral("audit-active")) {
            active.start(executable, {QStringLiteral("--hold-destination-lock"), Audit::auditLockPath()});
            ready = active.waitForStarted(3000) && active.waitForReadyRead(3000)
                && active.readAllStandardOutput().contains("LOCK_READY");
        } else {
            ready = endSyntheticLockHolder(executable, Audit::auditLockPath());
            qputenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT", "takeover-unlinked");
        }
        const QByteArray before = directoryDigest(QFileInfo(fixture.destination).absolutePath());
        const auto result = Coordinator::decide(fixture.destination, Decision::Confirm);
        qunsetenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT");
        qunsetenv("WIZ_RECOVERY_TEST_AUDIT_RELEASE_FAILURE");
        check(ready && result.recovery == RecoveryOutcome::None && result.audit == AuditOutcome::Failed
                && result.lockRelease == LockReleaseOutcome::NotRequested
                && oldLivePair(fixture)
                && directoryDigest(QFileInfo(fixture.destination).absolutePath()) == before,
            "unconfirmed intent or audit takeover never clears the target lock or starts file recovery");
        if (active.state() != QProcess::NotRunning) {
            active.kill();
            active.waitForFinished(3000);
        }
    }

    check(preparePreflightFixture(root, QStringLiteral("coordinator-dead-pair"), fixture)
            && endSyntheticLockHolder(executable, destinationLockPath(fixture.destination)),
        "confirmed dual dead-lock fixture prepared");
    selectAudit(QStringLiteral("coordinator-dead-pair-audit"));
    check(Audit::appendConfirmIntent(destinationIdentity(fixture.destination)).outcome
                == Audit::AppendOutcome::Recorded
            && endSyntheticLockHolder(executable, Audit::auditLockPath()),
        "independent audit lock belongs to another ended synthetic child");
    const auto recovered = Coordinator::decide(fixture.destination, Decision::Confirm);
    check(recovered.recovery == RecoveryOutcome::RolledBack && recovered.audit == AuditOutcome::Recorded
            && oldLivePair(fixture) && !QFileInfo::exists(fixture.recovery)
            && !QFileInfo::exists(destinationLockPath(fixture.destination))
            && !QFileInfo::exists(Audit::auditLockPath())
            && coordinatorAuditEvents(destinationIdentity(fixture.destination))
                == QStringList{QStringLiteral("requested"), QStringLiteral("requested"), QStringLiteral("rolled-back")},
        "explicit confirmation separately takes over dead audit and target locks before strict recovery");

    check(preparePreflightFixture(root, QStringLiteral("coordinator-preflight-release"), fixture),
        "coordinator preflight release failure fixture prepared");
    selectAudit(QStringLiteral("coordinator-preflight-release-audit"));
    const QByteArray before = directoryDigest(QFileInfo(fixture.destination).absolutePath());
    qputenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE", "1");
    const auto releaseFailure = Coordinator::decide(fixture.destination, Decision::Confirm);
    qunsetenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE");
    check(releaseFailure.recovery == RecoveryOutcome::Refused && releaseFailure.audit == AuditOutcome::Failed
            && releaseFailure.lockRelease == LockReleaseOutcome::Failed
            && directoryDigest(QFileInfo(fixture.destination).absolutePath()) == before
            && coordinatorAuditEvents(destinationIdentity(fixture.destination)) == QStringList{QStringLiteral("requested")},
        "failed preflight unlock blocks execution and final audit lock acquisition");

    check(preparePreflightFixture(root, QStringLiteral("coordinator-takeover-release"), fixture)
            && endSyntheticLockHolder(executable, destinationLockPath(fixture.destination)),
        "coordinator takeover release failure starts from a real dead target lock");
    selectAudit(QStringLiteral("coordinator-takeover-release-audit"));
    const QByteArray retainedSite = directoryDigest(fixture.recovery);
    qputenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE", "1");
    const auto takeoverRelease = Coordinator::decide(fixture.destination, Decision::Confirm);
    qunsetenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE");
    check(takeoverRelease.recovery == RecoveryOutcome::Refused && takeoverRelease.audit == AuditOutcome::Failed
            && takeoverRelease.lockRelease == LockReleaseOutcome::Failed
            && !QFileInfo::exists(destinationLockPath(fixture.destination))
            && oldLivePair(fixture) && directoryDigest(fixture.recovery) == retainedSite
            && coordinatorAuditEvents(destinationIdentity(fixture.destination)) == QStringList{QStringLiteral("requested")},
        "uncertain takeover unlock never enters preflight execution or another audit lock");
    const auto freshAfterTakeoverFailure = Coordinator::decide(fixture.destination, Decision::Confirm);
    check(freshAfterTakeoverFailure.recovery == RecoveryOutcome::RolledBack
            && freshAfterTakeoverFailure.audit == AuditOutcome::Recorded
            && oldLivePair(fixture) && !QFileInfo::exists(fixture.recovery),
        "fresh confirmed operation reclassifies actual state after uncertain takeover removal");

    const QStringList cases{QStringLiteral("intent-kill"), QStringLiteral("stale-plan"),
        QStringLiteral("result-busy"), QStringLiteral("execute-release"),
        QStringLiteral("execution-kill"), QStringLiteral("takeover-kill")};
    for (const QString& kind : cases) {
        const QString name = QStringLiteral("coordinator-child-") + kind;
        check(preparePreflightFixture(root, name, fixture), "coordinator child-window fixture prepared");
        if (kind == QStringLiteral("takeover-kill"))
            check(endSyntheticLockHolder(executable, destinationLockPath(fixture.destination)),
                "concurrent target takeover starts from its own ended child's lock");
        const QString base = QDir(root).filePath(name + QStringLiteral("-audit"));
        qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", base.toUtf8());
        const QString checkpoint = kind == QStringLiteral("intent-kill") ? QStringLiteral("intent-recorded")
            : kind == QStringLiteral("result-busy") ? QStringLiteral("recovery-complete")
            : kind == QStringLiteral("execution-kill") ? QStringLiteral("before-recovery-cleanup")
            : kind == QStringLiteral("takeover-kill") ? QStringLiteral("destination-takeover-opened")
            : QStringLiteral("preflight-complete");
        const QString ready = QDir(root).filePath(name + QStringLiteral("-ready"));
        const QString resume = ready + QStringLiteral("-resume");
        QProcess child;
        if (kind == QStringLiteral("execute-release"))
            qputenv("WIZ_RECOVERY_TEST_CONFIRM_EXECUTE_RELEASE_FAILURE", "1");
        child.start(executable, {QStringLiteral("--coordinate-and-hold"), fixture.destination,
            base, checkpoint, ready, resume});
        bool reached = child.waitForStarted(3000);
        qunsetenv("WIZ_RECOVERY_TEST_CONFIRM_EXECUTE_RELEASE_FAILURE");
        QElapsedTimer timer;
        timer.start();
        while (reached && !QFileInfo::exists(ready) && timer.elapsed() < 3000)
            child.waitForReadyRead(10);
        reached = reached && readBytes(ready) == checkpoint.toUtf8();
        check(reached && !QFileInfo::exists(Audit::auditLockPath())
                && coordinatorAuditEvents(destinationIdentity(fixture.destination)) == QStringList{QStringLiteral("requested")},
            "actual confirmation reaches requested window with durable intent and no audit lock held");
        QProcess resultBlocker;
        if (kind == QStringLiteral("execution-kill")) {
            check(QFileInfo::exists(destinationLockPath(fixture.destination))
                    && preflight(fixture.destination).plan == PreflightPlan::Busy
                    && Audit::appendConfirmIntent(destinationIdentity(fixture.destination)).outcome
                        == Audit::AppendOutcome::Recorded,
                "actual executor holds only target lock while independent audit append succeeds");
        } else if (kind == QStringLiteral("takeover-kill")) {
            const QByteArray held = directoryDigest(QFileInfo(fixture.destination).absolutePath());
            const auto competing = Coordinator::decide(fixture.destination, Decision::Confirm);
            check(competing.recovery == RecoveryOutcome::Busy && competing.audit == AuditOutcome::Recorded
                    && directoryDigest(QFileInfo(fixture.destination).absolutePath()) == held,
                "concurrent confirmed target takeover is nonblocking and cannot remove the held old inode");
        } else {
            check(!QFileInfo::exists(destinationLockPath(fixture.destination)),
                "between coordinator operations neither target nor audit lock is held");
        }
        if (kind == QStringLiteral("intent-kill") || kind == QStringLiteral("execution-kill")
            || kind == QStringLiteral("takeover-kill")) {
            child.kill();
            check(child.waitForFinished(3000) && child.exitStatus() == QProcess::CrashExit
                    && oldLivePair(fixture) && QFileInfo::exists(fixture.recovery),
                "actual child termination keeps the recognized live state and recovery site");
            const auto retry = Coordinator::decide(fixture.destination, Decision::Confirm);
            check(retry.recovery == RecoveryOutcome::RolledBack && retry.audit == AuditOutcome::Recorded
                    && !QFileInfo::exists(fixture.recovery) && oldLivePair(fixture),
                "fresh explicit confirmation after child termination revalidates and completes recovery");
            continue;
        }
        if (kind == QStringLiteral("stale-plan"))
            check(writeBytes(fixture.destination, QByteArrayLiteral("EXTERNAL EDIT\n")),
                "external edit occurs after coordinator preflight and before executor lock");
        if (kind == QStringLiteral("result-busy")) {
            resultBlocker.start(executable, {QStringLiteral("--hold-destination-lock"), Audit::auditLockPath()});
            check(resultBlocker.waitForStarted(3000) && resultBlocker.waitForReadyRead(3000)
                    && resultBlocker.readAllStandardOutput().contains("LOCK_READY")
                    && oldLivePair(fixture) && !QFileInfo::exists(fixture.recovery),
                "another actual audit owner blocks only the final result after recovery completed");
        }
        check(writeBytes(resume, QByteArrayLiteral("CONTINUE")), "test resumes only its own paused coordinator");
        const bool ended = child.waitForFinished(3000);
        if (!ended) {
            child.kill();
            child.waitForFinished(3000);
        }
        const auto result = QJsonDocument::fromJson(child.readAllStandardOutput()).object();
        check(ended && child.exitCode() == 0 && !result.isEmpty(), "actual coordinator child returns a typed result");
        if (kind == QStringLiteral("stale-plan")) {
            check(result.value(QStringLiteral("recovery")).toInt() == int(RecoveryOutcome::Refused)
                    && result.value(QStringLiteral("audit")).toInt() == int(AuditOutcome::Recorded)
                    && readBytes(fixture.destination) == "EXTERNAL EDIT\n" && QFileInfo::exists(fixture.recovery),
                "executor refuses stale preflight and final audit records refusal without overwriting external edit");
        } else {
            const bool unlockFailed = kind == QStringLiteral("execute-release");
            check(result.value(QStringLiteral("recovery")).toInt() == int(RecoveryOutcome::RolledBack)
                    && result.value(QStringLiteral("audit")).toInt() == int(AuditOutcome::Failed)
                    && result.value(QStringLiteral("lockRelease")).toInt()
                        == int(unlockFailed ? LockReleaseOutcome::Failed : LockReleaseOutcome::Released)
                    && oldLivePair(fixture) && !QFileInfo::exists(fixture.recovery)
                    && coordinatorAuditEvents(destinationIdentity(fixture.destination)) == QStringList{QStringLiteral("requested")},
                "completed file recovery stays completed when final audit or target unlock report fails");
        }
        if (resultBlocker.state() != QProcess::NotRunning) {
            resultBlocker.kill();
            resultBlocker.waitForFinished(3000);
        }
    }
    qunsetenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA");
}

QByteArray recoverySiteSnapshot(const QString& path)
{
    if (!QFileInfo(path).isDir() || QFileInfo(path).isSymLink())
        return QByteArrayLiteral("NO ORDINARY ROOT");
    QStringList entries;
    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::System
        | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir directory(path);
    while (iterator.hasNext()) {
        iterator.next();
        const auto info = iterator.fileInfo();
        entries.append(directory.relativeFilePath(info.filePath()) + QLatin1Char(':')
            + (info.isSymLink() ? QStringLiteral("LINK") : info.isDir() ? QStringLiteral("DIR")
                : QString::fromLatin1(fileDigest(info.filePath()))));
    }
    entries.sort(Qt::CaseSensitive);
    return entries.join(QLatin1Char('\n')).toUtf8();
}

void testRecoveryCleanupFailures(const QString& root, const QString& executable)
{
    using namespace WizMarkdownExportRecovery;
    namespace Coordinator = WizMarkdownExportRecoveryCoordinator;
    const auto setup = [&](const QString& name, bool finalized, PreflightFixture& fixture) {
        return preparePreflightFixture(root, name, fixture)
            && (!finalized || (QDir().rename(fixture.resources, fixture.backup)
                && QDir().rename(fixture.stagedResources, fixture.resources)
                && writeBytes(fixture.destination, QByteArrayLiteral("NEW MARKDOWN\n"))));
    };
    for (bool finalized : {false, true}) {
        const auto plan = finalized ? PreflightPlan::Finalize : PreflightPlan::RollBack;
        const auto expected = finalized ? RecoveryOutcome::Finalized : RecoveryOutcome::RolledBack;
        for (const QString& point : {QStringLiteral("terminal-live-sync"),
                 QStringLiteral("terminal-before-cleanup"), QStringLiteral("cleanup-entry-removed"),
                 QStringLiteral("cleanup-parent-sync")}) {
            const QString name = QStringLiteral("cleanup-fault-%1-").arg(finalized) + point;
            PreflightFixture fixture;
            check(setup(name, finalized, fixture), "independent terminal cleanup failure fixture prepared");
            const QByteArray site = recoverySiteSnapshot(fixture.recovery);
            const QByteArray markdown = readBytes(fixture.destination);
            const QByteArray resources = directoryDigest(fixture.resources);
            qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", point.toUtf8());
            const auto failed = executePlan(fixture.destination, plan);
            qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
            check(failed.recovery == RecoveryOutcome::Refused && failed.audit == AuditOutcome::NotRequested
                    && failed.lockRelease == LockReleaseOutcome::Released
                    && readBytes(fixture.destination) == markdown
                    && directoryDigest(fixture.resources) == resources
                    && !QFileInfo::exists(destinationLockPath(fixture.destination)),
                "failed cleanup or terminal sync never damages the exact terminal live pair or reports complete");
            const bool partial = point == QStringLiteral("cleanup-entry-removed");
            const bool absent = point == QStringLiteral("cleanup-parent-sync");
            check(absent ? !QFileInfo::exists(fixture.recovery)
                    : QFileInfo(fixture.recovery).isDir()
                        && (partial ? recoverySiteSnapshot(fixture.recovery) != site
                                    : recoverySiteSnapshot(fixture.recovery) == site),
                "fault boundary distinguishes intact partial and absent recovery sites precisely");
            if (point != QStringLiteral("terminal-live-sync")) {
                check(failed.userVisibleSummary.contains(QStringLiteral("清理"))
                        && failed.userVisibleSummary.contains(QStringLiteral("未确认"))
                        && !failed.userVisibleSummary.contains(QStringLiteral("保留现场"))
                        && failed.diagnosticDetail.contains(absent ? QStringLiteral("当前不存在")
                                                                  : QStringLiteral("当前仍存在")),
                    "cleanup failure summary must not claim the whole site was retained");
            }
            if (partial) {
                const QByteArray remaining = recoverySiteSnapshot(fixture.recovery);
                check(preflight(fixture.destination).plan == PreflightPlan::Refused
                        && executePlan(fixture.destination, plan).recovery == RecoveryOutcome::Refused
                        && recoverySiteSnapshot(fixture.recovery) == remaining,
                    "strict v1 refuses partial cleanup rather than inventing missing evidence or deleting more");
                qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", QDir(root).filePath(name + QStringLiteral("-audit")).toUtf8());
                const auto confirmed = Coordinator::decide(fixture.destination, Decision::Confirm);
                check(confirmed.recovery == RecoveryOutcome::Refused && confirmed.audit == AuditOutcome::Recorded
                        && recoverySiteSnapshot(fixture.recovery) == remaining
                        && readBytes(fixture.destination) == markdown
                        && directoryDigest(fixture.resources) == resources,
                    "even explicit confirmation preserves unknown partial-cleanup evidence and the live pair");
                qunsetenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA");
                const auto prepared = prepare(fixture.destination, QByteArrayLiteral("OTHER\n"),
                    [](const QString&) { return false; });
                check(prepared.outcome == PreparationOutcome::Refused
                        && recoverySiteSnapshot(fixture.recovery) == remaining,
                    "new preparation cannot overwrite an incompletely cleaned recovery site");
            } else if (absent) {
                check(preflight(fixture.destination).plan == PreflightPlan::None
                        && executePlan(fixture.destination, plan).recovery == RecoveryOutcome::None
                        && readBytes(fixture.destination) == markdown
                        && directoryDigest(fixture.resources) == resources,
                    "absent-site retry is explicitly a no-op and does not repeat recovery or assert prior sync");
            } else {
                check(preflight(fixture.destination).plan == plan
                        && executePlan(fixture.destination, plan).recovery == expected
                        && !QFileInfo::exists(fixture.recovery),
                    "intact terminal site revalidates and completes cleanup after a fresh attempt");
            }
        }
    }

    for (const QString& point : {QStringLiteral("terminal-live-sync"), QStringLiteral("cleanup-parent-sync")}) {
        PreflightFixture moved;
        check(setup(QStringLiteral("cleanup-after-resource-restore-") + point, false, moved)
                && QDir().rename(moved.resources, moved.backup)
                && QDir().rename(moved.stagedResources, moved.resources),
            "cleanup failure after actual resource rollback fixture prepared");
        qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", point.toUtf8());
        const auto failed = executePlan(moved.destination, PreflightPlan::RollBack);
        qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
        check(failed.recovery == RecoveryOutcome::Refused && failed.lockRelease == LockReleaseOutcome::Released
                && oldLivePair(moved) && !QFileInfo::exists(destinationLockPath(moved.destination)),
            "incomplete overall recovery can already have restored the exact old pair without undoing it");
        if (point == QStringLiteral("terminal-live-sync")) {
            check(preflight(moved.destination).evidenceState == QStringLiteral("rollback-old-resources-restored")
                    && executePlan(moved.destination, PreflightPlan::RollBack).recovery == RecoveryOutcome::RolledBack,
                "terminal sync failure after resource restore remains a recognized revalidatable state");
        } else {
            check(!QFileInfo::exists(moved.recovery)
                    && failed.diagnosticDetail.contains(QStringLiteral("当前不存在"))
                    && executePlan(moved.destination, PreflightPlan::RollBack).recovery == RecoveryOutcome::None,
                "after restored resources and cleanup uncertainty no stale success or retained-site claim is made");
        }
    }

    PreflightFixture permission;
    check(setup(QStringLiteral("cleanup-real-permission"), false, permission),
        "actual filesystem cleanup failure fixture prepared");
    const auto permissions = QFileInfo(permission.stagedResources).permissions();
    check(QFile::setPermissions(permission.stagedResources, QFileDevice::ReadOwner | QFileDevice::ExeOwner),
        "only the synthetic staged resource directory is made non-writable");
    const QByteArray site = recoverySiteSnapshot(permission.recovery);
    const auto denied = executePlan(permission.destination, PreflightPlan::RollBack);
    const bool restored = QFile::setPermissions(permission.stagedResources, permissions);
    check(restored && denied.recovery == RecoveryOutcome::Refused
            && denied.lockRelease == LockReleaseOutcome::Released
            && oldLivePair(permission) && recoverySiteSnapshot(permission.recovery) == site
            && denied.userVisibleSummary.contains(QStringLiteral("清理")),
        "actual unlink permission failure retains evidence and restores temporary test permissions");
    check(preflight(permission.destination).plan == PreflightPlan::RollBack
            && executePlan(permission.destination, PreflightPlan::RollBack).recovery == RecoveryOutcome::RolledBack,
        "fresh recovery succeeds once the synthetic permission obstruction is removed");

    for (bool finalized : {false, true}) {
        const QString name = QStringLiteral("cleanup-partial-kill-%1").arg(finalized);
        PreflightFixture fixture;
        check(setup(name, finalized, fixture), "partial cleanup real termination fixture prepared");
        const QByteArray site = recoverySiteSnapshot(fixture.recovery);
        const QString ready = QDir(root).filePath(name + QStringLiteral("-ready"));
        QProcess child;
        child.start(executable, {QStringLiteral("--execute-plan-and-hold"), fixture.destination,
            finalized ? QStringLiteral("finalize") : QStringLiteral("rollback"),
            QStringLiteral("cleanup-entry-removed"), ready});
        bool reached = child.waitForStarted(3000);
        QElapsedTimer timer;
        timer.start();
        while (reached && !QFileInfo::exists(ready) && timer.elapsed() < 3000)
            child.waitForReadyRead(10);
        reached = reached && readBytes(ready) == "cleanup-entry-removed";
        const bool blocked = reached && preflight(fixture.destination).plan == PreflightPlan::Busy;
        child.kill();
        const bool ended = child.waitForFinished(3000) && child.exitStatus() == QProcess::CrashExit;
        check(blocked && ended && QFileInfo(fixture.recovery).isDir()
                && recoverySiteSnapshot(fixture.recovery) != site
                && (finalized ? newLivePair(fixture) : oldLivePair(fixture)),
            "real child kill after one removed entry keeps the exact terminal pair and partial site");
        const QByteArray remaining = recoverySiteSnapshot(fixture.recovery);
        check(preflight(fixture.destination).plan == PreflightPlan::Busy
                && inspectDestinationLock(fixture.destination).state == LockState::ConfirmedDead
                && recoverySiteSnapshot(fixture.recovery) == remaining,
            "default retry retains the actual dead lock and incomplete cleanup bytes");
        const auto taken = takeOverConfirmedDeadDestinationLock(fixture.destination);
        check(taken.outcome == LockTakeoverOutcome::Removed && taken.lockRelease == LockReleaseOutcome::Released
                && preflight(fixture.destination).plan == PreflightPlan::Refused
                && recoverySiteSnapshot(fixture.recovery) == remaining
                && (finalized ? newLivePair(fixture) : oldLivePair(fixture)),
            "explicit dead-lock removal still cannot authorize cleanup of missing v1 evidence");
    }
}

bool stagePreparationResources(const QString& html)
{
    QString title = QFileInfo(html).fileName();
    const int dot = title.lastIndexOf('.');
    if (dot >= 0)
        title.truncate(dot);
    const QString resources = QDir(QFileInfo(html).absolutePath())
        .filePath(title.toHtmlEscaped() + QStringLiteral("_files"));
    return QDir().mkpath(resources) && writeBytes(html, QByteArrayLiteral("<html>new</html>"))
        && writeBytes(QDir(resources).filePath(QStringLiteral("new.bin")), QByteArrayLiteral("NEW RESOURCE"));
}

void testPreparationBarriers(const QString& root, const QString& executable)
{
    using namespace WizMarkdownExportRecovery;
    const auto setup = [&](const QString& name, bool existing, PreflightFixture& fixture) {
        const QString parent = QDir(root).filePath(name);
        if (!QDir().mkpath(parent))
            return false;
        fixture.destination = QDir(parent).filePath(QStringLiteral("note.md"));
        fixture.resources = QDir(parent).filePath(QStringLiteral("note_files"));
        fixture.recovery = recoveryDirectory(fixture.destination);
        fixture.stagedResources = QDir(fixture.recovery).filePath(QStringLiteral("content/note_files"));
        return !existing || (QDir().mkpath(fixture.resources)
            && writeBytes(fixture.destination, QByteArrayLiteral("OLD MARKDOWN\n"))
            && writeBytes(QDir(fixture.resources).filePath(QStringLiteral("old.bin")), QByteArrayLiteral("OLD RESOURCE")));
    };
    const auto unchanged = [&](const PreflightFixture& fixture, bool existing) {
        return existing ? oldLivePair(fixture)
            : !QFileInfo::exists(fixture.destination) && !QFileInfo::exists(fixture.resources);
    };
    const QStringList points{QStringLiteral("prepare-recovery-created"), QStringLiteral("prepare-content-created"),
        QStringLiteral("prepare-markdown-committed"), QStringLiteral("prepare-markdown-file-sync"),
        QStringLiteral("prepare-markdown-directory-sync"), QStringLiteral("prepare-html-removed"),
        QStringLiteral("prepare-resource-sync"), QStringLiteral("prepare-manifest-committed"),
        QStringLiteral("prepare-manifest-file-sync"), QStringLiteral("prepare-manifest-directory-sync"),
        QStringLiteral("prepare-root-sync"), QStringLiteral("prepare-parent-sync")};
    for (bool existing : {false, true}) {
        for (int index = 0; index < points.size(); ++index) {
            const QString point = points.at(index);
            PreflightFixture fixture;
            check(setup(QStringLiteral("prepare-fault-%1-%2").arg(existing).arg(point), existing, fixture),
                "independent preparation durability failure fixture created");
            int callbacks = 0;
            qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", point.toUtf8());
            const auto failed = prepare(fixture.destination, QByteArrayLiteral("NEW MARKDOWN\n"),
                [&](const QString& html) { ++callbacks; return stagePreparationResources(html); });
            qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
            check(failed.outcome == PreparationOutcome::FailedCleaned
                    && failed.lockRelease == LockReleaseOutcome::Released && unchanged(fixture, existing)
                    && !QFileInfo::exists(fixture.recovery)
                    && !QFileInfo::exists(destinationLockPath(fixture.destination)),
                "operation failure never reports Prepared and cleans only its own site while preserving live output");
            check(callbacks == (index < 5 ? 0 : 1),
                "failure before staging callback prevents export and later failures invoke it exactly once");
            const auto fresh = prepare(fixture.destination, QByteArrayLiteral("NEW MARKDOWN\n"), stagePreparationResources);
            check(fresh.outcome == PreparationOutcome::Prepared && fresh.lockRelease == LockReleaseOutcome::Released
                    && unchanged(fixture, existing)
                    && preflight(fixture.destination).plan == PreflightPlan::RollBack,
                "fresh preparation independently rebuilds strict evidence after cleaned failure");
            check(executePlan(fixture.destination, PreflightPlan::RollBack).recovery == RecoveryOutcome::RolledBack
                    && unchanged(fixture, existing) && !QFileInfo::exists(fixture.recovery),
                "fresh prepared site rolls back to the exact original present or absent pair");
        }
    }

    for (const QString& cleanup : {QStringLiteral("prepare-before-failure-cleanup"),
             QStringLiteral("prepare-cleanup-entry-removed"), QStringLiteral("prepare-cleanup-parent-sync")}) {
        PreflightFixture fixture;
        check(setup(QStringLiteral("prepare-double-fault-") + cleanup, true, fixture),
            "primary preparation failure plus cleanup failure fixture prepared");
        qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", QByteArrayLiteral("prepare-resource-sync,") + cleanup.toUtf8());
        const auto failed = prepare(fixture.destination, QByteArrayLiteral("NEW MARKDOWN\n"), stagePreparationResources);
        qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
        const bool absent = cleanup == QStringLiteral("prepare-cleanup-parent-sync");
        check(failed.outcome == PreparationOutcome::FailedRetained
                && failed.lockRelease == LockReleaseOutcome::Released && oldLivePair(fixture)
                && QFileInfo::exists(fixture.recovery) != absent
                && !QFileInfo::exists(destinationLockPath(fixture.destination)),
            "unconfirmed failed-site cleanup is reported independently and leaves exact old live output");
        check(failed.userVisibleSummary.contains(QStringLiteral("清理尚未确认"))
                && failed.diagnosticDetail.contains(absent ? QStringLiteral("当前不存在")
                                                          : QStringLiteral("当前仍存在")),
            "FailedRetained must describe cleanup uncertainty instead of guaranteeing a directory still exists");
        if (absent) {
            check(preflight(fixture.destination).plan == PreflightPlan::None
                    && prepare(fixture.destination, QByteArrayLiteral("NEW MARKDOWN\n"), stagePreparationResources).outcome
                        == PreparationOutcome::Prepared
                    && executePlan(fixture.destination, PreflightPlan::RollBack).recovery == RecoveryOutcome::RolledBack,
                "absent failed preparation can start a fresh operation without asserting the prior barrier succeeded");
        } else {
            const QByteArray retained = recoverySiteSnapshot(fixture.recovery);
            check(preflight(fixture.destination).plan == PreflightPlan::Refused
                    && prepare(fixture.destination, QByteArrayLiteral("NEW\n"), stagePreparationResources).outcome
                        == PreparationOutcome::Refused
                    && recoverySiteSnapshot(fixture.recovery) == retained,
                "incomplete retained preparation cannot authorize a new export or guessed cleanup");
        }
    }

    PreflightFixture release;
    check(setup(QStringLiteral("prepare-failure-release"), true, release),
        "preparation cleanup and target release independence fixture prepared");
    qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", "prepare-resource-sync");
    qputenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE", "1");
    const auto releaseFailed = prepare(release.destination, QByteArrayLiteral("NEW MARKDOWN\n"), stagePreparationResources);
    qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
    qunsetenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE");
    check(releaseFailed.outcome == PreparationOutcome::FailedCleaned
            && releaseFailed.lockRelease == LockReleaseOutcome::Failed && oldLivePair(release)
            && !QFileInfo::exists(release.recovery)
            && !releaseFailed.userVisibleSummary.contains(QStringLiteral("保留现场")),
        "unconfirmed target release cannot claim an already cleaned preparation site still exists");

    PreflightFixture permission;
    check(setup(QStringLiteral("prepare-real-cleanup-permission"), true, permission),
        "real failed-preparation cleanup permission fixture prepared");
    QFileDevice::Permissions original;
    bool blocked = false;
    const auto denied = prepare(permission.destination, QByteArrayLiteral("NEW MARKDOWN\n"), [&](const QString& html) {
        if (!stagePreparationResources(html))
            return false;
        original = QFileInfo(permission.stagedResources).permissions();
        blocked = QFile::setPermissions(permission.stagedResources, QFileDevice::ReadOwner | QFileDevice::ExeOwner);
        return false;
    });
    const bool restored = blocked && QFile::setPermissions(permission.stagedResources, original);
    check(restored && QFileInfo(permission.stagedResources).permissions() == original
            && denied.outcome == PreparationOutcome::FailedRetained
            && denied.lockRelease == LockReleaseOutcome::Released && oldLivePair(permission)
            && readBytes(QDir(permission.stagedResources).filePath(QStringLiteral("new.bin"))) == "NEW RESOURCE",
        "actual unlink denial preserves staged data and explicitly restores only synthetic directory permissions");
    const QByteArray remainder = recoverySiteSnapshot(permission.recovery);
    check(preflight(permission.destination).plan == PreflightPlan::Refused
            && prepare(permission.destination, QByteArrayLiteral("NEW\n"), stagePreparationResources).outcome
                == PreparationOutcome::Refused
            && recoverySiteSnapshot(permission.recovery) == remainder,
        "restored permissions do not authorize deleting an incomplete failed-preparation site");

    const QStringList killPoints{QStringLiteral("prepare-recovery-created"), QStringLiteral("prepare-markdown-committed"),
        QStringLiteral("prepare-html-removed"), QStringLiteral("prepare-manifest-committed"),
        QStringLiteral("prepare-manifest-directory-sync"), QStringLiteral("prepare-parent-sync")};
    for (int index = 0; index < killPoints.size(); ++index) {
        const QString point = killPoints.at(index);
        PreflightFixture fixture;
        check(setup(QStringLiteral("prepare-kill-") + point, true, fixture),
            "actual preparation termination live fixture prepared");
        const QString ready = QDir(root).filePath(point + QStringLiteral("-prepare-ready"));
        QProcess child;
        child.start(executable, {QStringLiteral("--prepare-at-checkpoint"), fixture.destination, point, ready});
        bool reached = child.waitForStarted(3000);
        QElapsedTimer timer;
        timer.start();
        while (reached && !QFileInfo::exists(ready) && timer.elapsed() < 3000)
            child.waitForReadyRead(10);
        reached = reached && readBytes(ready) == point.toUtf8();
        check(reached && oldLivePair(fixture) && preflight(fixture.destination).plan == PreflightPlan::Busy,
            "actual preparation holds the target lock and never changes old output at a durability boundary");
        child.kill();
        const bool ended = child.waitForFinished(3000) && child.exitStatus() == QProcess::CrashExit;
        const QByteArray retained = recoverySiteSnapshot(fixture.recovery);
        check(ended && oldLivePair(fixture) && inspectDestinationLock(fixture.destination).state == LockState::ConfirmedDead
                && preflight(fixture.destination).plan == PreflightPlan::Busy
                && recoverySiteSnapshot(fixture.recovery) == retained,
            "actual preparation interruption preserves its real dead lock and stage without default cleanup");
        const auto taken = takeOverConfirmedDeadDestinationLock(fixture.destination);
        check(taken.outcome == LockTakeoverOutcome::Removed && taken.lockRelease == LockReleaseOutcome::Released
                && oldLivePair(fixture) && recoverySiteSnapshot(fixture.recovery) == retained,
            "explicit takeover removes only its ended child's lock before full stage revalidation");
        if (index < 3) {
            check(preflight(fixture.destination).plan == PreflightPlan::Refused
                    && prepare(fixture.destination, QByteArrayLiteral("NEW\n"), stagePreparationResources).outcome
                        == PreparationOutcome::Refused
                    && recoverySiteSnapshot(fixture.recovery) == retained,
                "pre-manifest interruption remains unrecognized and cannot be silently reconstructed");
        } else {
            check(preflight(fixture.destination).plan == PreflightPlan::RollBack
                    && executePlan(fixture.destination, PreflightPlan::RollBack).recovery == RecoveryOutcome::RolledBack
                    && oldLivePair(fixture) && !QFileInfo::exists(fixture.recovery),
                "visible valid manifest after process interruption can revalidate without claiming storage power-loss safety");
        }
    }
}

void testResourceRenameSyncFailures(const QString& root)
{
    using namespace WizMarkdownExportRecovery;
    for (const QString& point : {QStringLiteral("return-new-source-sync"), QStringLiteral("return-new-destination-sync"),
             QStringLiteral("restore-old-source-sync"), QStringLiteral("restore-old-destination-sync")}) {
        PreflightFixture fixture;
        check(preparePreflightFixture(root, QStringLiteral("rename-sync-fault-") + point, fixture)
                && QDir().rename(fixture.resources, fixture.backup)
                && QDir().rename(fixture.stagedResources, fixture.resources),
            "resource rename ordinary sync failure fixture prepared");
        qputenv("WIZ_RECOVERY_TEST_FAILURE_AT", point.toUtf8());
        const auto failed = executePlan(fixture.destination, PreflightPlan::RollBack);
        qunsetenv("WIZ_RECOVERY_TEST_FAILURE_AT");
        check(failed.recovery == RecoveryOutcome::Refused && failed.lockRelease == LockReleaseOutcome::Released
                && readBytes(fixture.destination) == "OLD MARKDOWN\n"
                && readBytes(QDir(fixture.returned).filePath(QStringLiteral("new.bin"))) == "NEW RESOURCE"
                && !QFileInfo::exists(destinationLockPath(fixture.destination)),
            "failed source or destination parent sync keeps all exact old and returned-new bytes without reporting success");
        const bool returnedOnly = point.startsWith(QStringLiteral("return-new"));
        check(returnedOnly
                ? !QFileInfo::exists(fixture.resources)
                    && readBytes(QDir(fixture.backup).filePath(QStringLiteral("old.bin"))) == "OLD RESOURCE"
                : oldLivePair(fixture) && !QFileInfo::exists(fixture.backup),
            "ordinary sync failure exposes exactly the recognized resource-return or old-restored intermediate state");
        const auto livePlan = preflight(fixture.destination);
        check(livePlan.plan == PreflightPlan::RollBack
                && livePlan.evidenceState == (returnedOnly ? QStringLiteral("rollback-new-resources-returned")
                                                          : QStringLiteral("rollback-old-resources-restored"))
                && executePlan(fixture.destination, livePlan.plan).recovery == RecoveryOutcome::RolledBack
                && oldLivePair(fixture) && !QFileInfo::exists(fixture.recovery),
            "fresh strict revalidation completes a recognized rename-sync failure without losing either prior state");
    }
}

void testDestinationReleaseFailures(const QString& root)
{
    using namespace WizMarkdownExportRecovery;
    PreflightFixture fixture;
    check(preparePreflightFixture(root, QStringLiteral("release-failure-preflight"), fixture),
        "destination release preflight fixture created");
    check(preflight(fixture.destination).lockRelease == LockReleaseOutcome::Released,
        "ordinary preflight explicitly confirms durable destination-lock release");
    qputenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE", "1");
    const auto deniedPlan = preflight(fixture.destination);
    check(deniedPlan.plan == PreflightPlan::Refused
            && deniedPlan.lockRelease == LockReleaseOutcome::Failed
            && oldLivePair(fixture) && QFileInfo(fixture.recovery).isDir(),
        "unconfirmed release cannot return an actionable preflight plan");
    const auto rollback = executePlan(fixture.destination, PreflightPlan::RollBack);
    check(rollback.recovery == RecoveryOutcome::RolledBack
            && rollback.lockRelease == LockReleaseOutcome::Failed
            && oldLivePair(fixture) && !QFileInfo::exists(fixture.recovery)
            && rollback.userVisibleSummary.contains(QStringLiteral("尚未确认")),
        "completed rollback is preserved while release failure is reported independently");
    qunsetenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE");
    const auto repeated = executePlan(fixture.destination, PreflightPlan::RollBack);
    check(repeated.recovery == RecoveryOutcome::None
            && repeated.lockRelease == LockReleaseOutcome::NotRequested && oldLivePair(fixture),
        "completed rollback is not repeated because its prior unlock report failed");

    check(preparePreflightFixture(root, QStringLiteral("release-failure-finalize"), fixture)
            && QDir().rename(fixture.resources, fixture.backup)
            && QDir().rename(fixture.stagedResources, fixture.resources)
            && writeBytes(fixture.destination, QByteArrayLiteral("NEW MARKDOWN\n")),
        "destination release committed-state fixture created");
    qputenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE", "1");
    const auto finalized = executePlan(fixture.destination, PreflightPlan::Finalize);
    check(finalized.recovery == RecoveryOutcome::Finalized
            && finalized.lockRelease == LockReleaseOutcome::Failed
            && readBytes(fixture.destination) == QByteArrayLiteral("NEW MARKDOWN\n")
            && !QFileInfo::exists(fixture.recovery),
        "completed finalization is not misreported as reverted on unlock failure");
    qunsetenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE");

    const QString parent = QDir(root).filePath(QStringLiteral("release-failure-prepare"));
    const QString destination = QDir(parent).filePath(QStringLiteral("note.md"));
    check(QDir().mkpath(parent) && writeBytes(destination, QByteArrayLiteral("OLD MARKDOWN\n")),
        "destination release preparation fixture created");
    qputenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE", "1");
    const auto prepared = prepare(destination, QByteArrayLiteral("NEW MARKDOWN\n"),
        [](const QString& html) { return writeBytes(html, QByteArrayLiteral("<p>new</p>")); });
    check(prepared.outcome == PreparationOutcome::FailedRetained
            && prepared.lockRelease == LockReleaseOutcome::Failed
            && readBytes(destination) == QByteArrayLiteral("OLD MARKDOWN\n")
            && QFileInfo(prepared.recoveryDirectory).isDir(),
        "unconfirmed release retains the committed preparation without declaring Prepared");
    qunsetenv("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE");
    check(preflight(destination).plan == PreflightPlan::RollBack
            && executePlan(destination, PreflightPlan::RollBack).lockRelease
                == LockReleaseOutcome::Released,
        "retained preparation is revalidated and recovered after the unlock fault ends");
}

void testIdenticalMarkdownPreparation(const QString& root)
{
    using namespace WizMarkdownExportRecovery;
    const QString parent = QDir(root).filePath(QStringLiteral("identical-markdown-prepared"));
    const QString target = QDir(parent).filePath(QStringLiteral("note.md"));
    check(QDir().mkpath(QDir(parent).filePath(QStringLiteral("note_files")))
        && writeBytes(target, "SAME MARKDOWN\n")
        && writeBytes(QDir(parent).filePath(QStringLiteral("note_files/old.bin")), "OLD RESOURCE"),
        "identical old/new Markdown fixture created");
    const auto prepared = prepare(target, "SAME MARKDOWN\n", [](const QString& html) {
        const QString resources = QDir(QFileInfo(html).absolutePath()).filePath(QStringLiteral("note_files"));
        return QDir().mkpath(resources) && writeBytes(html, "<p>same</p>")
            && writeBytes(QDir(resources).filePath(QStringLiteral("new.bin")), "NEW RESOURCE");
    });
    check(prepared.outcome == PreparationOutcome::Prepared, "same-body preparation succeeds without replacing output");
    const auto inspected = preflight(target);
    check(inspected.plan == PreflightPlan::RollBack && inspected.evidenceState == QStringLiteral("prepared"),
        "same-body prepared resources must not be mistaken for an unknown committed state");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().size() == 5
        && app.arguments().at(1) == QStringLiteral("--prepare-at-checkpoint")) {
        qputenv("WIZ_RECOVERY_TEST_CHECKPOINT", app.arguments().at(3).toUtf8());
        qputenv("WIZ_RECOVERY_TEST_READY", app.arguments().at(4).toUtf8());
        const auto prepared = WizMarkdownExportRecovery::prepare(app.arguments().at(2),
            QByteArrayLiteral("NEW MARKDOWN\n"), stagePreparationResources);
        return prepared.outcome == WizMarkdownExportRecovery::PreparationOutcome::Prepared ? 0 : 5;
    }
    if (app.arguments().size() == 7
        && app.arguments().at(1) == QStringLiteral("--coordinate-and-hold")) {
        qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", app.arguments().at(3).toUtf8());
        const QByteArray checkpoint = app.arguments().at(4).toUtf8();
        if (checkpoint == "before-recovery-cleanup" || checkpoint == "destination-takeover-opened") {
            qputenv("WIZ_RECOVERY_TEST_CHECKPOINT", checkpoint);
            qputenv("WIZ_RECOVERY_TEST_READY", app.arguments().at(5).toUtf8());
        } else {
            qputenv("WIZ_RECOVERY_TEST_CONFIRM_CHECKPOINT", checkpoint);
            qputenv("WIZ_RECOVERY_TEST_CONFIRM_READY", app.arguments().at(5).toUtf8());
            qputenv("WIZ_RECOVERY_TEST_CONFIRM_RESUME", app.arguments().at(6).toUtf8());
        }
        const auto result = WizMarkdownExportRecoveryCoordinator::decide(app.arguments().at(2),
            WizMarkdownExportRecovery::Decision::Confirm);
        const auto bytes = QJsonDocument(QJsonObject{
            {QStringLiteral("recovery"), int(result.recovery)},
            {QStringLiteral("audit"), int(result.audit)},
            {QStringLiteral("lockRelease"), int(result.lockRelease)}
        }).toJson(QJsonDocument::Compact);
        std::puts(bytes.constData());
        return 0;
    }
    if (app.arguments().size() == 5
        && app.arguments().at(1) == QStringLiteral("--takeover-audit-and-hold")) {
        qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", app.arguments().at(2).toUtf8());
        qputenv("WIZ_RECOVERY_TEST_AUDIT_CHECKPOINT", "takeover-opened");
        qputenv("WIZ_RECOVERY_TEST_AUDIT_READY", app.arguments().at(3).toUtf8());
        qputenv("WIZ_RECOVERY_TEST_AUDIT_RESUME", app.arguments().at(4).toUtf8());
        const auto result = WizMarkdownExportRecoveryAudit::takeOverConfirmedDeadAuditLock();
        std::printf("TAKEOVER_OUTCOME=%d\n", int(result.outcome));
        return 0;
    }
    if (app.arguments().size() == 6
        && app.arguments().at(1) == QStringLiteral("--append-audit-and-hold")) {
        qputenv("WIZ_RECOVERY_TEST_APP_LOCAL_DATA", app.arguments().at(2).toUtf8());
        qputenv("WIZ_RECOVERY_TEST_AUDIT_CHECKPOINT", app.arguments().at(4).toUtf8());
        qputenv("WIZ_RECOVERY_TEST_AUDIT_READY", app.arguments().at(5).toUtf8());
        const auto result = WizMarkdownExportRecoveryAudit::appendConfirmIntent(app.arguments().at(3));
        return result.outcome == WizMarkdownExportRecoveryAudit::AppendOutcome::Recorded ? 0 : 5;
    }
    if (app.arguments().size() == 3
        && app.arguments().at(1) == QStringLiteral("--hold-destination-lock")) {
        QLockFile lock(app.arguments().at(2));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            return 2;
        std::puts("LOCK_READY");
        std::fflush(stdout);
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000)
            QThread::msleep(10);
        return 3;
    }
    if (app.arguments().size() == 4
        && app.arguments().at(1) == QStringLiteral("--prepare-and-hold")) {
        const QString destination = app.arguments().at(2);
        const QString ready = app.arguments().at(3);
        const auto prepared = WizMarkdownExportRecovery::prepare(destination,
            QByteArrayLiteral("NEW MARKDOWN\n"), [&](const QString& stagedHtml) {
                const QString resources = QDir(QFileInfo(stagedHtml).absolutePath())
                    .filePath(QStringLiteral("note_files"));
                if (!QDir().mkpath(resources)
                    || !writeBytes(stagedHtml, QByteArrayLiteral("<html>new</html>"))
                    || !writeBytes(QDir(resources).filePath(QStringLiteral("new.bin")),
                        QByteArrayLiteral("NEW RESOURCE"))
                    || !writeBytes(ready, QByteArrayLiteral("READY"))) {
                    return false;
                }
                QElapsedTimer timer;
                timer.start();
                while (timer.elapsed() < 10000)
                    QThread::msleep(10);
                return false;
            });
        return prepared.outcome == WizMarkdownExportRecovery::PreparationOutcome::Prepared
            ? 0 : 4;
    }
    if (app.arguments().size() == 4
        && app.arguments().at(1) == QStringLiteral("--prepare-complete-and-hold")) {
        const QString destination = app.arguments().at(2);
        const QString ready = app.arguments().at(3);
        const auto prepared = WizMarkdownExportRecovery::prepare(destination,
            QByteArrayLiteral("NEW MARKDOWN\n"), [&](const QString& stagedHtml) {
                QString title = QFileInfo(destination).fileName();
                const int dot = title.lastIndexOf('.');
                if (dot >= 0)
                    title.truncate(dot);
                const QString resources = QDir(QFileInfo(stagedHtml).absolutePath())
                    .filePath(title.toHtmlEscaped() + QStringLiteral("_files"));
                return QDir().mkpath(resources)
                    && writeBytes(stagedHtml, QByteArrayLiteral("<html>new</html>"))
                    && writeBytes(QDir(resources).filePath(QStringLiteral("new.bin")),
                        QByteArrayLiteral("NEW RESOURCE"));
            });
        if (prepared.outcome != WizMarkdownExportRecovery::PreparationOutcome::Prepared
            || !writeBytes(ready, QByteArrayLiteral("READY"))) {
            return 4;
        }
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000)
            QThread::msleep(10);
        return 3;
    }
    if (app.arguments().size() == 6
        && app.arguments().at(1) == QStringLiteral("--execute-plan-and-hold")) {
        const QString destination = app.arguments().at(2);
        const QString planName = app.arguments().at(3);
        qputenv("WIZ_RECOVERY_TEST_CHECKPOINT", app.arguments().at(4).toUtf8());
        qputenv("WIZ_RECOVERY_TEST_READY", app.arguments().at(5).toUtf8());
        const auto plan = planName == QStringLiteral("rollback")
            ? WizMarkdownExportRecovery::PreflightPlan::RollBack
            : WizMarkdownExportRecovery::PreflightPlan::Finalize;
        const auto executed = WizMarkdownExportRecovery::executePlan(destination, plan);
        const bool expected = (plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
                && executed.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack)
            || (plan == WizMarkdownExportRecovery::PreflightPlan::Finalize
                && executed.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Finalized);
        return expected ? 0 : 5;
    }
    QTemporaryDir temporary;
    check(temporary.isValid(), "temporary root created");
    testAudit(temporary.path(), app.applicationFilePath());
    testIdenticalMarkdownPreparation(temporary.path());
    testAuditRotation(temporary.path(), app.applicationFilePath());
    testAuditTakeover(temporary.path(), app.applicationFilePath());
    testRecoveryCoordinator(temporary.path(), app.applicationFilePath());
    testRecoveryCleanupFailures(temporary.path(), app.applicationFilePath());
    testPreparationBarriers(temporary.path(), app.applicationFilePath());
    testResourceRenameSyncFailures(temporary.path());
    testDestinationReleaseFailures(temporary.path());
    const QString destination = QDir(temporary.path()).filePath(QStringLiteral("paper & notes.md"));
    const QString other = QDir(temporary.path()).filePath(QStringLiteral("other.md"));

    const WizMarkdownExportRecovery::Result defaultResult;
    check(defaultResult.recovery == WizMarkdownExportRecovery::RecoveryOutcome::None,
        "compound result defaults to no recovery mutation");
    check(defaultResult.audit == WizMarkdownExportRecovery::AuditOutcome::NotRequested,
        "compound result defaults to no audit request");

    const QString identity = WizMarkdownExportRecovery::destinationIdentity(destination);
    check(identity.size() == 24, "destination identity is 24 characters");
    check(identity == identity.toLower(), "destination identity is lowercase");
    check(identity == WizMarkdownExportRecovery::destinationIdentity(destination),
        "destination identity is deterministic");
    check(identity != WizMarkdownExportRecovery::destinationIdentity(other),
        "different destination has independent identity");
    check(QFileInfo(WizMarkdownExportRecovery::recoveryDirectory(destination)).absolutePath()
            == temporary.path(),
        "recovery directory is a destination sibling");
    check(QFileInfo(WizMarkdownExportRecovery::destinationLockPath(destination)).absolutePath()
            == temporary.path(),
        "destination lock is a destination sibling");

    auto inspection = WizMarkdownExportRecovery::inspect(destination);
    check(inspection.availability == WizMarkdownExportRecovery::Availability::None,
        "missing recovery directory reports none");
    check(!inspection.userVisibleSummary.contains(temporary.path()),
        "ordinary summary omits full path");
    check(inspection.diagnosticDetail.contains(temporary.path()),
        "diagnostic detail retains explicit path");
    check(WizMarkdownExportRecovery::inspectDestinationLock(destination).state
            == WizMarkdownExportRecovery::LockState::Available,
        "missing destination lock reports available");
    check(WizMarkdownExportRecovery::preflight(destination).plan
            == WizMarkdownExportRecovery::PreflightPlan::None,
        "preflight without a recovery site performs no work");

    check(writeManifest(destination, validManifest(destination)), "valid manifest written");
    inspection = WizMarkdownExportRecovery::inspect(destination);
    check(inspection.availability == WizMarkdownExportRecovery::Availability::AwaitingDecision,
        "strict valid manifest awaits a decision");
    check(inspection.destinationIdentity == identity, "inspection reports derived identity");
    check(!inspection.userVisibleSummary.contains(temporary.path()),
        "recovery warning omits full path");

    clearFixture(destination);
    QJsonObject firstExport = validManifest(destination);
    firstExport.insert(QStringLiteral("oldMarkdownExisted"), false);
    firstExport.insert(QStringLiteral("oldMarkdownSha256"), QString());
    firstExport.insert(QStringLiteral("oldResourcesExisted"), false);
    firstExport.insert(QStringLiteral("oldResourcesSha256"), QString());
    firstExport.insert(QStringLiteral("newResourcesExisted"), false);
    firstExport.insert(QStringLiteral("newResourcesSha256"), QString());
    check(writeManifest(destination, firstExport), "first-export manifest written");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::AwaitingDecision,
        "first export without old or new resources is recognized");

    const QString manifestPath = QDir(WizMarkdownExportRecovery::recoveryDirectory(destination))
        .filePath(QStringLiteral("transaction.json"));
    QFile manifestBefore(manifestPath);
    check(manifestBefore.open(QIODevice::ReadOnly), "valid manifest opened before inspection");
    const QByteArray originalManifest = manifestBefore.readAll();
    manifestBefore.close();
    WizMarkdownExportRecovery::inspect(destination);
    QFile manifestAfter(manifestPath);
    check(manifestAfter.open(QIODevice::ReadOnly)
            && manifestAfter.readAll() == originalManifest,
        "inspection does not rewrite a valid manifest");

    PreflightFixture unknownRootLayout;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("layout-unknown-root"),
            unknownRootLayout),
        "unknown-root layout fixture created");
    const QString unknownRootEntry = QDir(unknownRootLayout.recovery).filePath(
        QStringLiteral("untracked.bin"));
    check(writeBytes(unknownRootEntry, QByteArrayLiteral("UNTRACKED")),
        "unknown recovery root entry injected");
    check(WizMarkdownExportRecovery::inspect(unknownRootLayout.destination).availability
            == WizMarkdownExportRecovery::Availability::Refused
            && WizMarkdownExportRecovery::preflight(unknownRootLayout.destination).plan
                == WizMarkdownExportRecovery::PreflightPlan::Refused,
        "inspection and preflight refuse an unknown recovery root entry");
    check(QFileInfo::exists(unknownRootEntry)
            && QFileInfo::exists(unknownRootLayout.destination),
        "unknown root refusal preserves recovery evidence and live Markdown");

    PreflightFixture stagedHtmlLayout;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("layout-staged-html"),
            stagedHtmlLayout),
        "staged-HTML layout fixture created");
    const QString untrackedHtml = QDir(stagedHtmlLayout.recovery).filePath(
        QStringLiteral("content/note.md"));
    check(writeBytes(untrackedHtml, QByteArrayLiteral("<html>untracked</html>")),
        "untracked staged HTML injected");
    check(WizMarkdownExportRecovery::inspect(stagedHtmlLayout.destination).availability
            == WizMarkdownExportRecovery::Availability::Refused
            && WizMarkdownExportRecovery::preflight(stagedHtmlLayout.destination).plan
                == WizMarkdownExportRecovery::PreflightPlan::Refused,
        "inspection and preflight refuse staged HTML outside the manifest contract");
    check(QFileInfo::exists(untrackedHtml)
            && QFileInfo::exists(stagedHtmlLayout.destination),
        "staged-HTML refusal preserves recovery evidence and live Markdown");

    QJsonObject changed = validManifest(destination);
    changed.insert(QStringLiteral("futureField"), true);
    expectManifestRefused(destination, changed, "unknown manifest field is refused");
    changed = validManifest(destination);
    changed.remove(QStringLiteral("newResourcesSha256"));
    expectManifestRefused(destination, changed, "missing manifest field is refused");
    changed = validManifest(destination);
    changed.insert(QStringLiteral("schema"), 2);
    expectManifestRefused(destination, changed, "unknown manifest schema is refused");
    changed = validManifest(destination);
    changed.insert(QStringLiteral("destinationName"), QStringLiteral("../paper.md"));
    expectManifestRefused(destination, changed, "parent traversal destination is refused");
    changed = validManifest(destination);
    changed.insert(QStringLiteral("destinationName"), QStringLiteral("other.md"));
    expectManifestRefused(destination, changed, "mismatched destination is refused");
    changed = validManifest(destination);
    changed.insert(QStringLiteral("resourcesName"), QStringLiteral("other_files"));
    expectManifestRefused(destination, changed, "mismatched resource name is refused");
    changed = validManifest(destination);
    changed.insert(QStringLiteral("oldMarkdownExisted"), QStringLiteral("true"));
    expectManifestRefused(destination, changed, "string existence flag is refused");
    changed = validManifest(destination);
    changed.insert(QStringLiteral("newMarkdownSha256"), QStringLiteral("ABC"));
    expectManifestRefused(destination, changed, "malformed new digest is refused");
    changed = validManifest(destination);
    changed.insert(QStringLiteral("oldMarkdownExisted"), false);
    expectManifestRefused(destination, changed, "digest present for absent old Markdown is refused");
    changed = validManifest(destination);
    changed.insert(QStringLiteral("newResourcesExisted"), false);
    expectManifestRefused(destination, changed, "digest present for absent new resources is refused");

    clearFixture(destination);
    check(QDir().mkpath(WizMarkdownExportRecovery::recoveryDirectory(destination)),
        "malformed JSON fixture directory created");
    check(writeBytes(manifestPath, QByteArrayLiteral("{")), "malformed JSON written");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "malformed JSON is refused");

    clearFixture(destination);
    check(QDir().mkpath(WizMarkdownExportRecovery::recoveryDirectory(destination)),
        "oversized fixture directory created");
    check(writeBytes(manifestPath, QByteArray(64 * 1024 + 1, 'x')),
        "oversized manifest written");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "oversized manifest is refused");

    clearFixture(destination);
    check(writeBytes(WizMarkdownExportRecovery::recoveryDirectory(destination),
            QByteArrayLiteral("not-a-directory")),
        "non-directory recovery fixture written");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "non-directory recovery path is refused");

    clearFixture(destination);
    check(QDir().mkpath(WizMarkdownExportRecovery::recoveryDirectory(destination)),
        "manifest directory fixture recovery created");
    check(QDir().mkpath(manifestPath), "manifest directory fixture created");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "manifest directory is refused");

    clearFixture(destination);
    check(writeManifest(destination, validManifest(destination)), "lock fixture manifest written");
    QLockFile activeLock(WizMarkdownExportRecovery::destinationLockPath(destination));
    activeLock.setStaleLockTime(0);
    check(activeLock.tryLock(0), "active destination lock acquired");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Busy,
        "active regular lock is preserved as busy");
    check(WizMarkdownExportRecovery::inspectDestinationLock(destination).state
            == WizMarkdownExportRecovery::LockState::Active,
        "same-host same-application live owner is classified active");
    check(WizMarkdownExportRecovery::preflight(destination).plan
            == WizMarkdownExportRecovery::PreflightPlan::Busy,
        "preflight refuses an active destination lock");
    auto takeover = WizMarkdownExportRecovery::takeOverConfirmedDeadDestinationLock(
        destination);
    check(takeover.outcome == WizMarkdownExportRecovery::LockTakeoverOutcome::Active
            && QFileInfo::exists(WizMarkdownExportRecovery::destinationLockPath(
                destination)),
        "explicit takeover refuses and preserves an active destination lock");
    check(QFileInfo::exists(WizMarkdownExportRecovery::destinationLockPath(destination)),
        "inspection never removes an existing lock");
    activeLock.unlock();
    takeover = WizMarkdownExportRecovery::takeOverConfirmedDeadDestinationLock(
        destination);
    check(takeover.outcome == WizMarkdownExportRecovery::LockTakeoverOutcome::None,
        "explicit takeover reports no work when no lock exists");

    check(writeBytes(WizMarkdownExportRecovery::destinationLockPath(destination),
            QByteArrayLiteral("owner")),
        "malformed lock fixture written");
    check(WizMarkdownExportRecovery::inspectDestinationLock(destination).state
            == WizMarkdownExportRecovery::LockState::Refused,
        "malformed regular lock is refused rather than treated as stale");
    check(WizMarkdownExportRecovery::preflight(destination).plan
            == WizMarkdownExportRecovery::PreflightPlan::Refused,
        "preflight refuses a malformed lock without removing it");
    check(QFileInfo::exists(WizMarkdownExportRecovery::destinationLockPath(destination)),
        "malformed lock is preserved");
    takeover = WizMarkdownExportRecovery::takeOverConfirmedDeadDestinationLock(
        destination);
    check(takeover.outcome == WizMarkdownExportRecovery::LockTakeoverOutcome::Refused
            && QFileInfo::exists(WizMarkdownExportRecovery::destinationLockPath(
                destination)),
        "explicit takeover refuses and preserves a malformed destination lock");
    const QByteArray foreignLock = QByteArray::number(
        QCoreApplication::applicationPid()) + '\n'
        + QCoreApplication::applicationName().toUtf8()
        + QByteArrayLiteral("\nforeign-host\n");
    check(writeBytes(WizMarkdownExportRecovery::destinationLockPath(destination),
            foreignLock),
        "foreign-host lock fixture written");
    takeover = WizMarkdownExportRecovery::takeOverConfirmedDeadDestinationLock(
        destination);
    check(takeover.outcome == WizMarkdownExportRecovery::LockTakeoverOutcome::Refused
            && QFileInfo::exists(WizMarkdownExportRecovery::destinationLockPath(
                destination)),
        "explicit takeover refuses and preserves a foreign-host lock");

    clearFixture(destination);
    check(writeManifest(destination, validManifest(destination)), "lock directory manifest written");
    check(QDir().mkpath(WizMarkdownExportRecovery::destinationLockPath(destination)),
        "lock directory fixture created");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "non-file lock path is refused");
    QDir(WizMarkdownExportRecovery::destinationLockPath(destination)).removeRecursively();

    clearFixture(destination);
    check(writeManifest(destination, validManifest(destination)), "lock link manifest written");
    const QString lockTarget = QDir(temporary.path()).filePath(QStringLiteral("lock-target"));
    check(writeBytes(lockTarget, QByteArrayLiteral("external")), "lock link target written");
    check(QFile::link(lockTarget, WizMarkdownExportRecovery::destinationLockPath(destination)),
        "lock symlink created");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "lock symlink is refused");
    takeover = WizMarkdownExportRecovery::takeOverConfirmedDeadDestinationLock(
        destination);
    check(takeover.outcome == WizMarkdownExportRecovery::LockTakeoverOutcome::Refused
            && QFileInfo::exists(lockTarget),
        "explicit takeover refuses the lock symlink and preserves its target");

    clearFixture(destination);
    const QString recoveryTarget = QDir(temporary.path()).filePath(QStringLiteral("external-recovery"));
    check(QDir().mkpath(recoveryTarget), "external recovery target created");
    check(QFile::link(recoveryTarget, WizMarkdownExportRecovery::recoveryDirectory(destination)),
        "recovery directory symlink created");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "recovery directory symlink is refused");
    check(QFileInfo(recoveryTarget).isDir(), "recovery symlink target is preserved");

    clearFixture(destination);
    check(writeManifest(destination, validManifest(destination)), "manifest link directory created");
    QFile::remove(manifestPath);
    const QString manifestTarget = QDir(temporary.path()).filePath(QStringLiteral("manifest-target"));
    check(writeBytes(manifestTarget, QJsonDocument(validManifest(destination)).toJson()),
        "manifest link target written");
    check(QFile::link(manifestTarget, manifestPath), "manifest symlink created");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "manifest symlink is refused");
    check(QFileInfo::exists(manifestTarget), "manifest symlink target is preserved");

    clearFixture(destination);
    const QString resources = QDir(temporary.path()).filePath(
        QStringLiteral("paper &amp; notes_files"));
    check(writeBytes(resources, QByteArrayLiteral("not-a-directory")),
        "non-directory resource fixture written");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "non-directory live resource path is refused");
    QFile::remove(resources);

    clearFixture(destination);
    const QString resourceTarget = QDir(temporary.path()).filePath(
        QStringLiteral("external-resources"));
    check(QDir().mkpath(resourceTarget), "external resource target created");
    check(QFile::link(resourceTarget, resources), "resource directory symlink created");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "live resource symlink is refused");
    check(QFileInfo(resourceTarget).isDir(), "resource symlink target is preserved");
    QFile::remove(resources);

    PreflightFixture prepared;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-prepared"),
            prepared),
        "prepared preflight fixture created");
    auto preflight = WizMarkdownExportRecovery::preflight(prepared.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState == QStringLiteral("prepared"),
        "preflight classifies prepared state for rollback");
    check(!QFileInfo::exists(WizMarkdownExportRecovery::destinationLockPath(
            prepared.destination)),
        "successful preflight releases its destination lock");
    check(fileDigest(prepared.destination)
            == QCryptographicHash::hash(QByteArrayLiteral("OLD MARKDOWN\n"),
                QCryptographicHash::Sha256).toHex(),
        "prepared preflight does not change live Markdown");

    PreflightFixture oldReserved;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-old-reserved"),
            oldReserved)
            && QDir().rename(oldReserved.resources, oldReserved.backup),
        "old-resources-reserved preflight fixture created");
    preflight = WizMarkdownExportRecovery::preflight(oldReserved.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState == QStringLiteral("old-resources-reserved"),
        "preflight classifies reserved old resources for rollback");
    check(QFileInfo(oldReserved.backup).isDir()
            && !QFileInfo::exists(oldReserved.resources),
        "old-resources-reserved preflight does not rename resources");

    PreflightFixture newInstalled;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-new-installed"),
            newInstalled)
            && QDir().rename(newInstalled.resources, newInstalled.backup)
            && QDir().rename(newInstalled.stagedResources, newInstalled.resources),
        "new-resources-installed preflight fixture created");
    preflight = WizMarkdownExportRecovery::preflight(newInstalled.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState == QStringLiteral("new-resources-installed"),
        "preflight classifies installed new resources for rollback");
    check(QFileInfo(newInstalled.backup).isDir()
            && QFileInfo(newInstalled.resources).isDir(),
        "new-resources-installed preflight preserves both resource states");

    PreflightFixture committed;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-committed"),
            committed)
            && QDir().rename(committed.resources, committed.backup)
            && QDir().rename(committed.stagedResources, committed.resources)
            && writeBytes(committed.destination, QByteArrayLiteral("NEW MARKDOWN\n")),
        "committed preflight fixture created");
    preflight = WizMarkdownExportRecovery::preflight(committed.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::Finalize
            && preflight.evidenceState == QStringLiteral("markdown-committed"),
        "preflight classifies an exact new pair for finalization");
    check(QFileInfo(committed.recovery).isDir()
            && QFileInfo(committed.backup).isDir(),
        "finalization preflight does not clean recovery evidence");

    PreflightFixture returned;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-returned"),
            returned)
            && QDir().rename(returned.resources, returned.backup)
            && QDir().rename(returned.stagedResources, returned.resources)
            && QDir().rename(returned.resources, returned.returned),
        "rollback-returned preflight fixture created");
    preflight = WizMarkdownExportRecovery::preflight(returned.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState
                == QStringLiteral("rollback-new-resources-returned"),
        "preflight classifies returned new resources for rollback");
    check(QFileInfo(returned.backup).isDir() && QFileInfo(returned.returned).isDir(),
        "returned-resource preflight preserves both private trees");

    PreflightFixture restored;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-restored"),
            restored)
            && QDir().rename(restored.resources, restored.backup)
            && QDir().rename(restored.stagedResources, restored.resources)
            && QDir().rename(restored.resources, restored.returned)
            && QDir().rename(restored.backup, restored.resources),
        "rollback-restored preflight fixture created");
    preflight = WizMarkdownExportRecovery::preflight(restored.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState
                == QStringLiteral("rollback-old-resources-restored"),
        "preflight classifies already restored old resources for rollback completion");
    check(QFileInfo(restored.resources).isDir() && QFileInfo(restored.returned).isDir(),
        "restored-resource preflight preserves live and returned trees");

    PreflightFixture unknownMarkdown;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-unknown-markdown"),
            unknownMarkdown)
            && writeBytes(unknownMarkdown.destination,
                QByteArrayLiteral("EXTERNAL MARKDOWN\n")),
        "unknown Markdown preflight fixture created");
    const QByteArray unknownMarkdownBefore = fileDigest(unknownMarkdown.destination);
    preflight = WizMarkdownExportRecovery::preflight(unknownMarkdown.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::Refused,
        "preflight refuses externally changed Markdown");
    check(fileDigest(unknownMarkdown.destination) == unknownMarkdownBefore
            && QFileInfo(unknownMarkdown.recovery).isDir(),
        "refused Markdown preflight preserves live and recovery evidence");

    PreflightFixture unknownResources;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-unknown-resources"),
            unknownResources)
            && QDir().rename(unknownResources.resources, unknownResources.backup)
            && QDir().rename(unknownResources.stagedResources, unknownResources.resources)
            && writeBytes(QDir(unknownResources.resources).filePath(
                QStringLiteral("new.bin")), QByteArrayLiteral("EXTERNAL RESOURCE")),
        "unknown resources preflight fixture created");
    preflight = WizMarkdownExportRecovery::preflight(unknownResources.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::Refused,
        "preflight refuses externally changed resources");
    check(QFileInfo(unknownResources.backup).isDir()
            && QFileInfo(unknownResources.resources).isDir(),
        "refused resource preflight preserves old and live trees");

    PreflightFixture stagedMarkdownChanged;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-staged-changed"),
            stagedMarkdownChanged)
            && writeBytes(QDir(stagedMarkdownChanged.recovery).filePath(
                QStringLiteral("new-markdown")), QByteArrayLiteral("TAMPERED\n")),
        "changed staged Markdown preflight fixture created");
    preflight = WizMarkdownExportRecovery::preflight(stagedMarkdownChanged.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::Refused,
        "preflight refuses a changed staged new Markdown copy");

    auto execution = WizMarkdownExportRecovery::executePlan(
        unknownRootLayout.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Refused
            && QFileInfo::exists(unknownRootEntry)
            && QFileInfo(unknownRootLayout.recovery).isDir(),
        "executor refuses and retains an unknown recovery root entry");

    execution = WizMarkdownExportRecovery::executePlan(
        prepared.destination, WizMarkdownExportRecovery::PreflightPlan::None);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Refused
            && QFileInfo(prepared.recovery).isDir(),
        "executor refuses a non-mutating or ambiguous expected plan");
    execution = WizMarkdownExportRecovery::executePlan(
        prepared.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && execution.audit == WizMarkdownExportRecovery::AuditOutcome::NotRequested,
        "executor rolls back a prepared state without inventing an audit result");
    check(oldLivePair(prepared) && !QFileInfo::exists(prepared.recovery)
            && !QFileInfo::exists(WizMarkdownExportRecovery::destinationLockPath(
                prepared.destination)),
        "prepared rollback preserves the old pair and durably removes its private site");
    execution = WizMarkdownExportRecovery::executePlan(
        prepared.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::None,
        "repeating a completed rollback is an idempotent no-op");

    execution = WizMarkdownExportRecovery::executePlan(
        oldReserved.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && oldLivePair(oldReserved) && !QFileInfo::exists(oldReserved.recovery),
        "executor restores reserved old resources and completes rollback");

    execution = WizMarkdownExportRecovery::executePlan(
        newInstalled.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && oldLivePair(newInstalled) && !QFileInfo::exists(newInstalled.recovery),
        "executor returns installed new resources, restores old resources and cleans up");

    execution = WizMarkdownExportRecovery::executePlan(
        returned.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && oldLivePair(returned) && !QFileInfo::exists(returned.recovery),
        "executor resumes rollback after new resources were already returned");

    execution = WizMarkdownExportRecovery::executePlan(
        restored.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && oldLivePair(restored) && !QFileInfo::exists(restored.recovery),
        "executor completes cleanup after old resources were already restored");

    execution = WizMarkdownExportRecovery::executePlan(
        committed.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Refused
            && newLivePair(committed) && QFileInfo(committed.recovery).isDir(),
        "executor refuses a stale rollback plan after Markdown commit");
    execution = WizMarkdownExportRecovery::executePlan(
        committed.destination, WizMarkdownExportRecovery::PreflightPlan::Finalize);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Finalized
            && execution.audit == WizMarkdownExportRecovery::AuditOutcome::NotRequested
            && newLivePair(committed) && !QFileInfo::exists(committed.recovery),
        "executor finalizes an exact committed new pair and cleans old evidence");
    execution = WizMarkdownExportRecovery::executePlan(
        committed.destination, WizMarkdownExportRecovery::PreflightPlan::Finalize);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::None,
        "repeating completed finalization is an idempotent no-op");

    execution = WizMarkdownExportRecovery::executePlan(
        unknownMarkdown.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Refused
            && fileDigest(unknownMarkdown.destination) == unknownMarkdownBefore
            && QFileInfo(unknownMarkdown.recovery).isDir(),
        "executor refuses external Markdown changes and retains all evidence");
    execution = WizMarkdownExportRecovery::executePlan(
        unknownResources.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Refused
            && QFileInfo(unknownResources.backup).isDir()
            && QFileInfo(unknownResources.resources).isDir()
            && QFileInfo(unknownResources.recovery).isDir(),
        "executor refuses external resource changes and retains old plus live trees");
    execution = WizMarkdownExportRecovery::executePlan(
        stagedMarkdownChanged.destination,
        WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Refused
            && QFileInfo(stagedMarkdownChanged.recovery).isDir(),
        "executor refuses a changed staged Markdown copy without cleanup");

    PreflightFixture deadOwner;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("preflight-dead-lock"),
            deadOwner),
        "dead-owner preflight fixture created");
    const QString deadLockPath = WizMarkdownExportRecovery::destinationLockPath(
        deadOwner.destination);
    QProcess deadWriter;
    deadWriter.start(app.applicationFilePath(),
        {QStringLiteral("--hold-destination-lock"), deadLockPath});
    bool deadWriterReady = deadWriter.waitForStarted(3000)
        && deadWriter.waitForReadyRead(3000)
        && deadWriter.readAllStandardOutput().contains("LOCK_READY");
    check(deadWriterReady, "separate process acquires destination lock");
    check(deadWriterReady
            && WizMarkdownExportRecovery::inspectDestinationLock(deadOwner.destination).state
                == WizMarkdownExportRecovery::LockState::Active,
        "live child destination lock is classified active");
    takeover = WizMarkdownExportRecovery::takeOverConfirmedDeadDestinationLock(
        deadOwner.destination);
    check(deadWriterReady
            && takeover.outcome
                == WizMarkdownExportRecovery::LockTakeoverOutcome::Active,
        "explicit takeover cannot remove a live child lock");
    execution = WizMarkdownExportRecovery::executePlan(
        deadOwner.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(deadWriterReady
            && execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Busy
            && QFileInfo(deadOwner.recovery).isDir(),
        "executor refuses an active destination writer without mutating evidence");
    if (deadWriterReady)
        deadWriter.kill();
    const bool deadWriterKilled = deadWriterReady && deadWriter.waitForFinished(3000)
        && deadWriter.exitStatus() == QProcess::CrashExit;
    check(deadWriterKilled, "destination lock owner is terminated for stale-lock fixture");
    check(deadWriterKilled
            && WizMarkdownExportRecovery::inspectDestinationLock(deadOwner.destination).state
                == WizMarkdownExportRecovery::LockState::ConfirmedDead,
        "same-host same-application dead PID is classified without takeover");
    preflight = WizMarkdownExportRecovery::preflight(deadOwner.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::Busy
            && preflight.lock == WizMarkdownExportRecovery::LockState::ConfirmedDead,
        "ordinary preflight preserves a confirmed dead-owner lock");
    check(QFileInfo::exists(deadLockPath), "dead-owner lock remains after preflight");
    execution = WizMarkdownExportRecovery::executePlan(
        deadOwner.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Busy
            && QFileInfo::exists(deadLockPath) && QFileInfo(deadOwner.recovery).isDir(),
        "executor preserves a confirmed-dead lock until explicit takeover exists");
    takeover = WizMarkdownExportRecovery::takeOverConfirmedDeadDestinationLock(
        deadOwner.destination);
    check(takeover.outcome == WizMarkdownExportRecovery::LockTakeoverOutcome::Removed
            && !QFileInfo::exists(deadLockPath)
            && QFileInfo(deadOwner.recovery).isDir(),
        "explicit takeover removes only the confirmed-dead lock and retains evidence");
    preflight = WizMarkdownExportRecovery::preflight(deadOwner.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState == QStringLiteral("prepared"),
        "preflight fully revalidates the recovery site after explicit lock takeover");
    execution = WizMarkdownExportRecovery::executePlan(
        deadOwner.destination, WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && oldLivePair(deadOwner) && !QFileInfo::exists(deadOwner.recovery),
        "executor completes only after takeover and fresh preflight evidence");

    const auto terminateExecutorAt = [&](const QString& target, const QString& plan,
                                         const QString& checkpoint,
                                         const QString& readyName) {
        const QString ready = QDir(temporary.path()).filePath(readyName);
        QFile::remove(ready);
        QProcess writer;
        writer.start(app.applicationFilePath(),
            {QStringLiteral("--execute-plan-and-hold"), target, plan,
                checkpoint, ready});
        bool readySeen = writer.waitForStarted(3000);
        QElapsedTimer timer;
        timer.start();
        while (readySeen && !QFileInfo::exists(ready) && timer.elapsed() < 3000)
            writer.waitForReadyRead(10);
        readySeen = readySeen && QFileInfo::exists(ready);
        if (readySeen)
            writer.kill();
        return readySeen && writer.waitForFinished(3000)
            && writer.exitStatus() == QProcess::CrashExit;
    };
    const auto removeExecutionDeadLock = [&](const QString& target) {
        QLockFile stale(WizMarkdownExportRecovery::destinationLockPath(target));
        stale.setStaleLockTime(0);
        return stale.removeStaleLockFile();
    };

    PreflightFixture killedAfterReturn;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("execute-kill-return"),
            killedAfterReturn)
            && QDir().rename(killedAfterReturn.resources, killedAfterReturn.backup)
            && QDir().rename(killedAfterReturn.stagedResources,
                killedAfterReturn.resources),
        "executor return-resource termination fixture created");
    check(terminateExecutorAt(killedAfterReturn.destination, QStringLiteral("rollback"),
            QStringLiteral("after-new-resources-returned-before-sync"),
            QStringLiteral("execute-kill-return-ready")),
        "executor child terminated after returning new resources before parent sync");
    check(WizMarkdownExportRecovery::inspectDestinationLock(
                killedAfterReturn.destination).state
            == WizMarkdownExportRecovery::LockState::ConfirmedDead
            && !QFileInfo::exists(killedAfterReturn.resources)
            && QFileInfo(killedAfterReturn.backup).isDir()
            && QFileInfo(killedAfterReturn.returned).isDir(),
        "return-resource termination exposes the recognized returned-new state");
    preflight = WizMarkdownExportRecovery::preflight(killedAfterReturn.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::Busy,
        "ordinary preflight retains the executor dead lock after resource return");
    check(removeExecutionDeadLock(killedAfterReturn.destination),
        "test removes its return-resource dead lock after assertions");
    preflight = WizMarkdownExportRecovery::preflight(killedAfterReturn.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState
                == QStringLiteral("rollback-new-resources-returned"),
        "returned-new checkpoint reenters as a known rollback state");
    execution = WizMarkdownExportRecovery::executePlan(killedAfterReturn.destination,
        WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && oldLivePair(killedAfterReturn)
            && !QFileInfo::exists(killedAfterReturn.recovery),
        "executor idempotently resumes after return-resource termination");

    PreflightFixture killedAfterRestore;
    check(preparePreflightFixture(temporary.path(), QStringLiteral("execute-kill-restore"),
            killedAfterRestore)
            && QDir().rename(killedAfterRestore.resources, killedAfterRestore.backup)
            && QDir().rename(killedAfterRestore.stagedResources,
                killedAfterRestore.resources)
            && QDir().rename(killedAfterRestore.resources, killedAfterRestore.returned),
        "executor restore-old termination fixture created");
    check(terminateExecutorAt(killedAfterRestore.destination, QStringLiteral("rollback"),
            QStringLiteral("after-old-resources-restored-before-sync"),
            QStringLiteral("execute-kill-restore-ready")),
        "executor child terminated after restoring old resources before parent sync");
    check(WizMarkdownExportRecovery::inspectDestinationLock(
                killedAfterRestore.destination).state
            == WizMarkdownExportRecovery::LockState::ConfirmedDead
            && oldLivePair(killedAfterRestore)
            && !QFileInfo::exists(killedAfterRestore.backup)
            && QFileInfo(killedAfterRestore.returned).isDir(),
        "restore-old termination exposes the recognized restored-old state");
    check(removeExecutionDeadLock(killedAfterRestore.destination),
        "test removes its restore-old dead lock after assertions");
    preflight = WizMarkdownExportRecovery::preflight(killedAfterRestore.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState
                == QStringLiteral("rollback-old-resources-restored"),
        "restored-old checkpoint reenters as a known rollback state");
    execution = WizMarkdownExportRecovery::executePlan(killedAfterRestore.destination,
        WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && oldLivePair(killedAfterRestore)
            && !QFileInfo::exists(killedAfterRestore.recovery),
        "executor idempotently resumes after restore-old termination");

    PreflightFixture killedBeforeFinalizeCleanup;
    check(preparePreflightFixture(temporary.path(),
            QStringLiteral("execute-kill-before-finalize-cleanup"),
            killedBeforeFinalizeCleanup)
            && QDir().rename(killedBeforeFinalizeCleanup.resources,
                killedBeforeFinalizeCleanup.backup)
            && QDir().rename(killedBeforeFinalizeCleanup.stagedResources,
                killedBeforeFinalizeCleanup.resources)
            && writeBytes(killedBeforeFinalizeCleanup.destination,
                QByteArrayLiteral("NEW MARKDOWN\n")),
        "executor pre-finalize-cleanup termination fixture created");
    check(terminateExecutorAt(killedBeforeFinalizeCleanup.destination,
            QStringLiteral("finalize"), QStringLiteral("before-recovery-cleanup"),
            QStringLiteral("execute-kill-before-finalize-cleanup-ready")),
        "executor child terminated after terminal revalidation before final cleanup");
    check(WizMarkdownExportRecovery::inspectDestinationLock(
                killedBeforeFinalizeCleanup.destination).state
            == WizMarkdownExportRecovery::LockState::ConfirmedDead
            && newLivePair(killedBeforeFinalizeCleanup)
            && QFileInfo(killedBeforeFinalizeCleanup.recovery).isDir(),
        "pre-cleanup termination retains the complete committed site and new pair");
    check(removeExecutionDeadLock(killedBeforeFinalizeCleanup.destination),
        "test removes its pre-cleanup dead lock after assertions");
    preflight = WizMarkdownExportRecovery::preflight(
        killedBeforeFinalizeCleanup.destination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::Finalize
            && preflight.evidenceState == QStringLiteral("markdown-committed"),
        "pre-cleanup checkpoint reenters as the committed finalization state");
    execution = WizMarkdownExportRecovery::executePlan(
        killedBeforeFinalizeCleanup.destination,
        WizMarkdownExportRecovery::PreflightPlan::Finalize);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Finalized
            && newLivePair(killedBeforeFinalizeCleanup)
            && !QFileInfo::exists(killedBeforeFinalizeCleanup.recovery),
        "executor idempotently resumes finalization after pre-cleanup termination");

    PreflightFixture killedAfterCleanup;
    check(preparePreflightFixture(temporary.path(),
            QStringLiteral("execute-kill-after-cleanup"), killedAfterCleanup),
        "executor post-cleanup termination fixture created");
    check(terminateExecutorAt(killedAfterCleanup.destination, QStringLiteral("rollback"),
            QStringLiteral("after-recovery-cleanup-before-parent-sync"),
            QStringLiteral("execute-kill-after-cleanup-ready")),
        "executor child terminated after cleanup before recovery-parent sync");
    check(WizMarkdownExportRecovery::inspectDestinationLock(
                killedAfterCleanup.destination).state
            == WizMarkdownExportRecovery::LockState::ConfirmedDead
            && oldLivePair(killedAfterCleanup)
            && !QFileInfo::exists(killedAfterCleanup.recovery),
        "post-cleanup termination leaves the old pair complete and no visible site");
    bool postCleanupCallbackCalled = false;
    auto postCleanupPreparation = WizMarkdownExportRecovery::prepare(
        killedAfterCleanup.destination, QByteArrayLiteral("RETRY"),
        [&](const QString&) {
            postCleanupCallbackCalled = true;
            return true;
        });
    check(postCleanupPreparation.outcome
            == WizMarkdownExportRecovery::PreparationOutcome::Busy
            && !postCleanupCallbackCalled,
        "orphaned post-cleanup dead lock conservatively blocks a new export");
    check(removeExecutionDeadLock(killedAfterCleanup.destination),
        "test removes its post-cleanup dead lock after assertions");
    execution = WizMarkdownExportRecovery::executePlan(killedAfterCleanup.destination,
        WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::None
            && oldLivePair(killedAfterCleanup),
        "post-cleanup retry is an idempotent no-op after explicit test lock cleanup");

    const QString orphanLockParent = QDir(temporary.path()).filePath(
        QStringLiteral("takeover-orphan-lock"));
    const QString orphanLockDestination = QDir(orphanLockParent).filePath(
        QStringLiteral("note.md"));
    check(QDir().mkpath(orphanLockParent), "orphan-lock takeover parent created");
    const QString orphanLockPath = WizMarkdownExportRecovery::destinationLockPath(
        orphanLockDestination);
    QProcess orphanLockWriter;
    orphanLockWriter.start(app.applicationFilePath(),
        {QStringLiteral("--hold-destination-lock"), orphanLockPath});
    bool orphanLockReady = orphanLockWriter.waitForStarted(3000)
        && orphanLockWriter.waitForReadyRead(3000)
        && orphanLockWriter.readAllStandardOutput().contains("LOCK_READY");
    if (orphanLockReady)
        orphanLockWriter.kill();
    const bool orphanLockKilled = orphanLockReady
        && orphanLockWriter.waitForFinished(3000)
        && orphanLockWriter.exitStatus() == QProcess::CrashExit;
    check(orphanLockKilled
            && WizMarkdownExportRecovery::inspectDestinationLock(
                orphanLockDestination).state
                == WizMarkdownExportRecovery::LockState::ConfirmedDead
            && !QFileInfo::exists(WizMarkdownExportRecovery::recoveryDirectory(
                orphanLockDestination)),
        "confirmed-dead orphan lock exists without a recovery site");
    takeover = WizMarkdownExportRecovery::takeOverConfirmedDeadDestinationLock(
        orphanLockDestination);
    check(takeover.outcome == WizMarkdownExportRecovery::LockTakeoverOutcome::Removed
            && !QFileInfo::exists(orphanLockPath),
        "explicit takeover removes a confirmed-dead orphan lock");
    auto orphanPreparation = WizMarkdownExportRecovery::prepare(orphanLockDestination,
        QByteArrayLiteral("NEW MARKDOWN\n"), [&](const QString& stagedHtml) {
            return writeBytes(stagedHtml, QByteArrayLiteral("<html>new</html>"));
        });
    check(orphanPreparation.outcome
            == WizMarkdownExportRecovery::PreparationOutcome::Prepared,
        "new export may prepare only after explicit orphan-lock takeover");
    execution = WizMarkdownExportRecovery::executePlan(orphanLockDestination,
        WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && !QFileInfo::exists(orphanLockDestination)
            && !QFileInfo::exists(orphanPreparation.recoveryDirectory),
        "orphan-lock takeover does not skip normal preparation and revalidation");

    const auto stageNoteWithResources = [&](const QString& stagedHtml) {
        const QString stagedResources = QDir(QFileInfo(stagedHtml).absolutePath())
            .filePath(QStringLiteral("note_files"));
        return QDir().mkpath(stagedResources)
            && writeBytes(stagedHtml, QByteArrayLiteral("<html>new</html>"))
            && writeBytes(QDir(stagedResources).filePath(QStringLiteral("new.bin")),
                QByteArrayLiteral("NEW RESOURCE"));
    };

    const QString firstRollbackParent = QDir(temporary.path()).filePath(
        QStringLiteral("execute-first-rollback"));
    const QString firstRollbackDestination = QDir(firstRollbackParent).filePath(
        QStringLiteral("note.md"));
    check(QDir().mkpath(firstRollbackParent), "first-export rollback parent created");
    auto firstPreparation = WizMarkdownExportRecovery::prepare(firstRollbackDestination,
        QByteArrayLiteral("NEW MARKDOWN\n"), stageNoteWithResources);
    check(firstPreparation.outcome
            == WizMarkdownExportRecovery::PreparationOutcome::Prepared,
        "first export with resources prepared for rollback execution");
    execution = WizMarkdownExportRecovery::executePlan(firstRollbackDestination,
        WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && !QFileInfo::exists(firstRollbackDestination)
            && !QFileInfo::exists(QDir(firstRollbackParent).filePath(
                QStringLiteral("note_files")))
            && !QFileInfo::exists(firstPreparation.recoveryDirectory),
        "first-export rollback returns to a completely absent old pair");

    const QString firstFinalizeParent = QDir(temporary.path()).filePath(
        QStringLiteral("execute-first-finalize"));
    const QString firstFinalizeDestination = QDir(firstFinalizeParent).filePath(
        QStringLiteral("note.md"));
    const QString firstFinalizeResources = QDir(firstFinalizeParent).filePath(
        QStringLiteral("note_files"));
    check(QDir().mkpath(firstFinalizeParent), "first-export finalization parent created");
    firstPreparation = WizMarkdownExportRecovery::prepare(firstFinalizeDestination,
        QByteArrayLiteral("NEW MARKDOWN\n"), stageNoteWithResources);
    const QString firstFinalizeStagedResources = QDir(
        firstPreparation.recoveryDirectory).filePath(QStringLiteral("content/note_files"));
    check(firstPreparation.outcome
            == WizMarkdownExportRecovery::PreparationOutcome::Prepared
            && QDir().rename(firstFinalizeStagedResources, firstFinalizeResources)
            && writeBytes(firstFinalizeDestination, QByteArrayLiteral("NEW MARKDOWN\n")),
        "first export committed new pair fixture created");
    execution = WizMarkdownExportRecovery::executePlan(firstFinalizeDestination,
        WizMarkdownExportRecovery::PreflightPlan::Finalize);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Finalized
            && readBytes(firstFinalizeDestination) == QByteArrayLiteral("NEW MARKDOWN\n")
            && readBytes(QDir(firstFinalizeResources).filePath(
                QStringLiteral("new.bin"))) == QByteArrayLiteral("NEW RESOURCE")
            && !QFileInfo::exists(firstPreparation.recoveryDirectory),
        "first-export finalization preserves the committed new pair");

    const auto stageNoteWithoutResources = [&](const QString& stagedHtml) {
        return writeBytes(stagedHtml, QByteArrayLiteral("<html>new</html>"));
    };
    const QString noResourceRollbackParent = QDir(temporary.path()).filePath(
        QStringLiteral("execute-no-resource-rollback"));
    const QString noResourceRollbackDestination = QDir(noResourceRollbackParent)
        .filePath(QStringLiteral("note.md"));
    check(QDir().mkpath(noResourceRollbackParent),
        "resource-free rollback parent created");
    firstPreparation = WizMarkdownExportRecovery::prepare(noResourceRollbackDestination,
        QByteArrayLiteral("NEW MARKDOWN\n"), stageNoteWithoutResources);
    execution = WizMarkdownExportRecovery::executePlan(noResourceRollbackDestination,
        WizMarkdownExportRecovery::PreflightPlan::RollBack);
    check(firstPreparation.outcome
            == WizMarkdownExportRecovery::PreparationOutcome::Prepared
            && execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::RolledBack
            && !QFileInfo::exists(noResourceRollbackDestination)
            && !QFileInfo::exists(firstPreparation.recoveryDirectory),
        "resource-free first export rolls back to absent output");

    const QString noResourceFinalizeParent = QDir(temporary.path()).filePath(
        QStringLiteral("execute-no-resource-finalize"));
    const QString noResourceFinalizeDestination = QDir(noResourceFinalizeParent)
        .filePath(QStringLiteral("note.md"));
    check(QDir().mkpath(noResourceFinalizeParent),
        "resource-free finalization parent created");
    firstPreparation = WizMarkdownExportRecovery::prepare(noResourceFinalizeDestination,
        QByteArrayLiteral("NEW MARKDOWN\n"), stageNoteWithoutResources);
    check(firstPreparation.outcome
            == WizMarkdownExportRecovery::PreparationOutcome::Prepared
            && writeBytes(noResourceFinalizeDestination,
                QByteArrayLiteral("NEW MARKDOWN\n")),
        "resource-free first export committed fixture created");
    execution = WizMarkdownExportRecovery::executePlan(noResourceFinalizeDestination,
        WizMarkdownExportRecovery::PreflightPlan::Finalize);
    check(execution.recovery == WizMarkdownExportRecovery::RecoveryOutcome::Finalized
            && readBytes(noResourceFinalizeDestination)
                == QByteArrayLiteral("NEW MARKDOWN\n")
            && !QFileInfo::exists(QDir(noResourceFinalizeParent).filePath(
                QStringLiteral("note_files")))
            && !QFileInfo::exists(firstPreparation.recoveryDirectory),
        "resource-free first export finalizes without inventing resources");

    const QString prepareParent = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-success"));
    const QString prepareDestination = QDir(prepareParent).filePath(QStringLiteral("note.md"));
    const QString prepareResources = QDir(prepareParent).filePath(
        QStringLiteral("note_files"));
    check(QDir().mkpath(prepareResources)
            && writeBytes(prepareDestination, QByteArrayLiteral("OLD MARKDOWN\n"))
            && writeBytes(QDir(prepareResources).filePath(QStringLiteral("old.bin")),
                QByteArrayLiteral("OLD RESOURCE")),
        "successful preparation live fixture created");
    const QByteArray prepareOldMarkdownDigest = fileDigest(prepareDestination);
    const QByteArray prepareOldResourcesDigest = directoryDigest(prepareResources);
    auto preparation = WizMarkdownExportRecovery::prepare(prepareDestination,
        QByteArrayLiteral("NEW MARKDOWN\n"), stageNoteWithResources);
    check(preparation.outcome == WizMarkdownExportRecovery::PreparationOutcome::Prepared,
        "durable recovery site preparation succeeds");
    check(QFileInfo(prepareDestination).isFile()
            && fileDigest(prepareDestination) == prepareOldMarkdownDigest
            && directoryDigest(prepareResources) == prepareOldResourcesDigest,
        "successful preparation does not change live Markdown or resources");
    const QString preparedRecovery = WizMarkdownExportRecovery::recoveryDirectory(
        prepareDestination);
    const QString preparedManifest = QDir(preparedRecovery).filePath(
        QStringLiteral("transaction.json"));
    const auto directoryPermissions = QFileInfo(preparedRecovery).permissions();
    const auto manifestPermissions = QFileInfo(preparedManifest).permissions();
    check((directoryPermissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
                | QFileDevice::ExeGroup | QFileDevice::ReadOther
                | QFileDevice::WriteOther | QFileDevice::ExeOther)) == 0,
        "prepared recovery directory is owner-only");
    check((manifestPermissions & (QFileDevice::ReadGroup | QFileDevice::WriteGroup
                | QFileDevice::ReadOther | QFileDevice::WriteOther)) == 0,
        "prepared manifest is owner-only");
    check(WizMarkdownExportRecovery::inspect(prepareDestination).availability
            == WizMarkdownExportRecovery::Availability::AwaitingDecision,
        "prepared site passes strict read-only inspection");
    check(!QFileInfo::exists(QDir(preparedRecovery).filePath(
                QStringLiteral("content/note.md")))
            && QDir(QDir(preparedRecovery).filePath(QStringLiteral("content")))
                    .entryList(QDir::AllEntries | QDir::NoDotAndDotDot)
                == QStringList{QStringLiteral("note_files")},
        "successful preparation removes staged HTML and retains only declared resources");
    preflight = WizMarkdownExportRecovery::preflight(prepareDestination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState == QStringLiteral("prepared"),
        "prepared site and live old pair agree with preflight protocol");
    bool duplicateCallbackCalled = false;
    preparation = WizMarkdownExportRecovery::prepare(prepareDestination,
        QByteArrayLiteral("SECOND"), [&](const QString&) {
            duplicateCallbackCalled = true;
            return true;
        });
    check(preparation.outcome == WizMarkdownExportRecovery::PreparationOutcome::Refused
            && !duplicateCallbackCalled,
        "existing recovery site is preserved and never overwritten");

    const QString emptyParent = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-empty"));
    check(QDir().mkpath(emptyParent), "empty first-export parent created");
    const QString emptyDestination = QDir(emptyParent).filePath(QStringLiteral("note.md"));
    preparation = WizMarkdownExportRecovery::prepare(emptyDestination, QByteArray(),
        [&](const QString& stagedHtml) {
            return writeBytes(stagedHtml, QByteArrayLiteral("<html></html>"));
        });
    check(preparation.outcome == WizMarkdownExportRecovery::PreparationOutcome::Prepared,
        "empty first export without resources is durably prepared");
    preflight = WizMarkdownExportRecovery::preflight(emptyDestination);
    check(preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState == QStringLiteral("prepared"),
        "empty first export is recognized as prepared without inventing files");

    const QString callbackFailureParent = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-callback-failure"));
    check(QDir().mkpath(callbackFailureParent), "callback-failure parent created");
    const QString callbackFailureDestination = QDir(callbackFailureParent)
        .filePath(QStringLiteral("note.md"));
    preparation = WizMarkdownExportRecovery::prepare(callbackFailureDestination,
        QByteArrayLiteral("NEW"), [](const QString&) { return false; });
    check(preparation.outcome
            == WizMarkdownExportRecovery::PreparationOutcome::FailedCleaned,
        "ordinary staging callback failure reports cleaned failure");
    check(!QFileInfo::exists(WizMarkdownExportRecovery::recoveryDirectory(
                callbackFailureDestination))
            && !QFileInfo::exists(WizMarkdownExportRecovery::destinationLockPath(
                callbackFailureDestination)),
        "ordinary callback failure removes only its private site and lock");

    const QString linkFailureParent = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-link-failure"));
    check(QDir().mkpath(linkFailureParent), "link-failure parent created");
    const QString linkFailureDestination = QDir(linkFailureParent)
        .filePath(QStringLiteral("note.md"));
    const QString linkFailureTarget = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-link-target"));
    check(writeBytes(linkFailureTarget, QByteArrayLiteral("EXTERNAL")),
        "staging link target created");
    preparation = WizMarkdownExportRecovery::prepare(linkFailureDestination,
        QByteArrayLiteral("NEW"), [&](const QString& stagedHtml) {
            return writeBytes(stagedHtml, QByteArrayLiteral("<html></html>"))
                && QFile::link(linkFailureTarget,
                    QDir(QFileInfo(stagedHtml).absolutePath()).filePath(
                        QStringLiteral("unexpected-link")));
        });
    check(preparation.outcome
            == WizMarkdownExportRecovery::PreparationOutcome::FailedCleaned,
        "link inside staged content is refused and cleaned");
    QFile linkFailureTargetFile(linkFailureTarget);
    check(linkFailureTargetFile.open(QIODevice::ReadOnly)
            && linkFailureTargetFile.readAll() == QByteArrayLiteral("EXTERNAL")
            && !QFileInfo::exists(WizMarkdownExportRecovery::recoveryDirectory(
                linkFailureDestination)),
        "staging cleanup never follows the refused link");

    const QString changedLiveParent = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-live-change"));
    check(QDir().mkpath(changedLiveParent), "live-change parent created");
    const QString changedLiveDestination = QDir(changedLiveParent)
        .filePath(QStringLiteral("note.md"));
    check(writeBytes(changedLiveDestination, QByteArrayLiteral("OLD\n")),
        "live-change old Markdown written");
    preparation = WizMarkdownExportRecovery::prepare(changedLiveDestination,
        QByteArrayLiteral("NEW\n"), [&](const QString& stagedHtml) {
            return writeBytes(stagedHtml, QByteArrayLiteral("<html></html>"))
                && writeBytes(changedLiveDestination,
                    QByteArrayLiteral("EXTERNAL CHANGE\n"));
        });
    check(preparation.outcome
            == WizMarkdownExportRecovery::PreparationOutcome::FailedCleaned,
        "live change during staging is detected before manifest commit");
    QFile changedLiveFile(changedLiveDestination);
    check(changedLiveFile.open(QIODevice::ReadOnly)
            && changedLiveFile.readAll() == QByteArrayLiteral("EXTERNAL CHANGE\n")
            && !QFileInfo::exists(WizMarkdownExportRecovery::recoveryDirectory(
                changedLiveDestination)),
        "live-change refusal preserves external bytes and cleans only private staging");

    const QString activePrepareParent = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-active-lock"));
    check(QDir().mkpath(activePrepareParent), "prepare active-lock parent created");
    const QString activePrepareDestination = QDir(activePrepareParent)
        .filePath(QStringLiteral("note.md"));
    QLockFile activePrepareLock(WizMarkdownExportRecovery::destinationLockPath(
        activePrepareDestination));
    activePrepareLock.setStaleLockTime(0);
    check(activePrepareLock.tryLock(0), "prepare active lock acquired");
    bool activePrepareCallbackCalled = false;
    preparation = WizMarkdownExportRecovery::prepare(activePrepareDestination,
        QByteArrayLiteral("NEW"), [&](const QString&) {
            activePrepareCallbackCalled = true;
            return true;
        });
    check(preparation.outcome == WizMarkdownExportRecovery::PreparationOutcome::Busy
            && !activePrepareCallbackCalled,
        "preparation refuses an active writer before invoking export");
    activePrepareLock.unlock();

    const QString committedStopParent = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-committed-stop"));
    const QString committedStopResources = QDir(committedStopParent).filePath(
        QStringLiteral("note_files"));
    const QString committedStopDestination = QDir(committedStopParent).filePath(
        QStringLiteral("note.md"));
    check(QDir().mkpath(committedStopResources)
            && writeBytes(committedStopDestination, QByteArrayLiteral("OLD MARKDOWN\n"))
            && writeBytes(QDir(committedStopResources).filePath(
                QStringLiteral("old.bin")), QByteArrayLiteral("OLD RESOURCE")),
        "post-commit termination live fixture created");
    const QByteArray committedStopOldMarkdown = fileDigest(committedStopDestination);
    const QByteArray committedStopOldResources = directoryDigest(committedStopResources);
    const QString committedStopReady = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-committed-stop-ready"));
    QProcess committedStopWriter;
    committedStopWriter.start(app.applicationFilePath(),
        {QStringLiteral("--prepare-complete-and-hold"), committedStopDestination,
            committedStopReady});
    bool committedStopReadySeen = committedStopWriter.waitForStarted(3000);
    QElapsedTimer committedStopTimer;
    committedStopTimer.start();
    while (committedStopReadySeen && !QFileInfo::exists(committedStopReady)
        && committedStopTimer.elapsed() < 3000) {
        committedStopWriter.waitForReadyRead(10);
    }
    committedStopReadySeen = committedStopReadySeen
        && QFileInfo::exists(committedStopReady);
    const QString committedStopRecovery = WizMarkdownExportRecovery::recoveryDirectory(
        committedStopDestination);
    check(committedStopReadySeen
            && QFileInfo::exists(QDir(committedStopRecovery).filePath(
                QStringLiteral("transaction.json")))
            && !QFileInfo::exists(WizMarkdownExportRecovery::destinationLockPath(
                committedStopDestination)),
        "child commits complete manifest and releases lock before termination");
    if (committedStopReadySeen)
        committedStopWriter.kill();
    const bool committedStopKilled = committedStopReadySeen
        && committedStopWriter.waitForFinished(3000)
        && committedStopWriter.exitStatus() == QProcess::CrashExit;
    check(committedStopKilled
            && WizMarkdownExportRecovery::inspect(committedStopDestination).availability
                == WizMarkdownExportRecovery::Availability::AwaitingDecision,
        "post-commit process termination leaves an inspectable recovery site");
    preflight = WizMarkdownExportRecovery::preflight(committedStopDestination);
    check(committedStopKilled
            && preflight.plan == WizMarkdownExportRecovery::PreflightPlan::RollBack
            && preflight.evidenceState == QStringLiteral("prepared"),
        "post-commit process termination remains deterministically recoverable");
    check(fileDigest(committedStopDestination) == committedStopOldMarkdown
            && directoryDigest(committedStopResources) == committedStopOldResources,
        "post-commit process termination preserves the complete old live export");

    const QString interruptedParent = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-interrupted"));
    const QString interruptedResources = QDir(interruptedParent).filePath(
        QStringLiteral("note_files"));
    const QString interruptedDestination = QDir(interruptedParent).filePath(
        QStringLiteral("note.md"));
    check(QDir().mkpath(interruptedResources)
            && writeBytes(interruptedDestination, QByteArrayLiteral("OLD MARKDOWN\n"))
            && writeBytes(QDir(interruptedResources).filePath(QStringLiteral("old.bin")),
                QByteArrayLiteral("OLD RESOURCE")),
        "interrupted preparation live fixture created");
    const QString interruptedReady = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-interrupted-ready"));
    QProcess interruptedWriter;
    interruptedWriter.start(app.applicationFilePath(),
        {QStringLiteral("--prepare-and-hold"), interruptedDestination, interruptedReady});
    bool interruptedReadySeen = interruptedWriter.waitForStarted(3000);
    QElapsedTimer interruptedTimer;
    interruptedTimer.start();
    while (interruptedReadySeen && !QFileInfo::exists(interruptedReady)
        && interruptedTimer.elapsed() < 3000) {
        interruptedWriter.waitForReadyRead(10);
    }
    interruptedReadySeen = interruptedReadySeen && QFileInfo::exists(interruptedReady);
    check(interruptedReadySeen, "child reaches preparation callback before manifest commit");
    bool concurrentCallbackCalled = false;
    preparation = WizMarkdownExportRecovery::prepare(interruptedDestination,
        QByteArrayLiteral("CONCURRENT"), [&](const QString&) {
            concurrentCallbackCalled = true;
            return true;
        });
    check(interruptedReadySeen
            && preparation.outcome == WizMarkdownExportRecovery::PreparationOutcome::Busy
            && !concurrentCallbackCalled,
        "same-target concurrent preparation is refused while child holds lock");
    const QString independentPrepareParent = QDir(temporary.path()).filePath(
        QStringLiteral("prepare-independent"));
    check(QDir().mkpath(independentPrepareParent),
        "independent preparation parent created");
    const QString independentPrepareDestination = QDir(independentPrepareParent)
        .filePath(QStringLiteral("note.md"));
    preparation = WizMarkdownExportRecovery::prepare(independentPrepareDestination,
        QByteArrayLiteral("INDEPENDENT"), stageNoteWithResources);
    check(interruptedReadySeen
            && preparation.outcome == WizMarkdownExportRecovery::PreparationOutcome::Prepared,
        "different destination prepares independently while first target is locked");
    if (interruptedReadySeen)
        interruptedWriter.kill();
    const bool interruptedKilled = interruptedReadySeen
        && interruptedWriter.waitForFinished(3000)
        && interruptedWriter.exitStatus() == QProcess::CrashExit;
    const QString interruptedRecovery = WizMarkdownExportRecovery::recoveryDirectory(
        interruptedDestination);
    check(interruptedKilled && QFileInfo(interruptedRecovery).isDir()
            && !QFileInfo::exists(QDir(interruptedRecovery).filePath(
                QStringLiteral("transaction.json"))),
        "termination during callback leaves partial site without committed manifest");
    check(interruptedKilled
            && WizMarkdownExportRecovery::inspectDestinationLock(
                interruptedDestination).state
                == WizMarkdownExportRecovery::LockState::ConfirmedDead,
        "terminated preparation leaves a classifiable dead-owner lock");
    preparation = WizMarkdownExportRecovery::prepare(interruptedDestination,
        QByteArrayLiteral("RETRY"), stageNoteWithResources);
    check(preparation.outcome == WizMarkdownExportRecovery::PreparationOutcome::Busy
            && QFileInfo(interruptedRecovery).isDir(),
        "ordinary retry preserves dead lock and partial recovery evidence");
    clearFixture(interruptedDestination);

    clearFixture(destination);
    const QString destinationTarget = QDir(temporary.path()).filePath(
        QStringLiteral("external-markdown"));
    check(writeBytes(destinationTarget, QByteArrayLiteral("external")),
        "external destination target written");
    check(QFile::link(destinationTarget, destination), "destination symlink created");
    check(WizMarkdownExportRecovery::inspect(destination).availability
            == WizMarkdownExportRecovery::Availability::Refused,
        "destination symlink is refused");
    QFile destinationTargetFile(destinationTarget);
    check(destinationTargetFile.open(QIODevice::ReadOnly)
            && destinationTargetFile.readAll() == QByteArrayLiteral("external"),
        "destination symlink target is preserved");

    std::printf("RECOVERY_CORE_CHECKS=%d\nRECOVERY_CORE_FAILURES=%d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}

