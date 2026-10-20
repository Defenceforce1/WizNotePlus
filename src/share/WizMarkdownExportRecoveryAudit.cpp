#include "WizMarkdownExportRecoveryAudit.h"
#include "WizMarkdownExportRecovery.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QSysInfo>
#include <array>
#include <utility>

#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
#include <QElapsedTimer>
#include <QThread>
#endif

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

constexpr qsizetype MaximumAuditBytes = 64 * 1024;
constexpr qsizetype MaximumAuditEntries = 256;
constexpr int MaximumAuditArchives = 2;

bool auditCheckpoint(const char* name)
{
#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
    const QByteArray checkpoint(name);
    if (qgetenv("WIZ_RECOVERY_TEST_AUDIT_FAIL_AT") == checkpoint)
        return false;
    if (qgetenv("WIZ_RECOVERY_TEST_AUDIT_CHECKPOINT") == checkpoint) {
        QFile ready(qEnvironmentVariable("WIZ_RECOVERY_TEST_AUDIT_READY"));
        if (!ready.open(QIODevice::WriteOnly | QIODevice::NewOnly)
            || ready.write(checkpoint) != checkpoint.size() || !ready.flush())
            return false;
        ready.close();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 10000) {
            const QString resume = qEnvironmentVariable("WIZ_RECOVERY_TEST_AUDIT_RESUME");
            if (!resume.isEmpty() && QFileInfo::exists(resume))
                return true;
            QThread::msleep(10);
        }
        return false;
    }
#else
    Q_UNUSED(name);
#endif
    return true;
}

bool validConfirmEvent(const QString& event)
{
    static const QSet<QString> allowed{
        QStringLiteral("requested"), QStringLiteral("none"),
        QStringLiteral("rolled-back"), QStringLiteral("finalized"),
        QStringLiteral("busy"), QStringLiteral("refused")
    };
    return allowed.contains(event);
}

bool validEvent(const QString& decision, const QString& event)
{
    return (decision == QStringLiteral("confirm") && validConfirmEvent(event))
        || (decision == QStringLiteral("retain") && event == QStringLiteral("retained"));
}

bool validIdentity(const QString& identity)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{24}$"));
    return pattern.match(identity).hasMatch();
}

bool syncDirectory(const QString& path)
{
#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(path);
    const int descriptor = ::open(encoded.constData(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY);
    if (descriptor < 0)
        return false;
    const bool ok = ::fsync(descriptor) == 0;
    return ::close(descriptor) == 0 && ok;
#else
    Q_UNUSED(path);
    return true;
#endif
}

bool syncRegularFile(const QString& path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile())
        return false;
#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(path);
    const int descriptor = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        return false;
#ifdef F_FULLFSYNC
    const bool ok = ::fcntl(descriptor, F_FULLFSYNC) == 0;
#else
    const bool ok = ::fsync(descriptor) == 0;
#endif
    return ::close(descriptor) == 0 && ok;
#else
    return true;
#endif
}

bool ownedDirectory(const QString& path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isDir())
        return false;
#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(path);
    struct stat state {};
    return ::lstat(encoded.constData(), &state) == 0 && S_ISDIR(state.st_mode)
        && state.st_uid == ::geteuid();
#else
    return true;
#endif
}

bool ownedRegularFile(const QString& path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile())
        return false;
#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(path);
    struct stat state {};
    return ::lstat(encoded.constData(), &state) == 0 && S_ISREG(state.st_mode)
        && state.st_uid == ::geteuid();
#else
    return true;
#endif
}

bool forcePrivateDirectory(const QString& path)
{
    return ownedDirectory(path)
        && QFile::setPermissions(path,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

bool ensureAuditDirectory(QString& reason)
{
    const QString root = WizMarkdownExportRecoveryAudit::auditDirectory();
    const QString auditParent = QFileInfo(root).absolutePath();
    const QString appData = QFileInfo(auditParent).absolutePath();
    if (!QDir().mkpath(appData) || !ownedDirectory(appData)) {
        reason = QStringLiteral("应用本地数据目录不可用：") + appData;
        return false;
    }
    if (!QFileInfo::exists(auditParent) && !QDir().mkdir(auditParent)) {
        reason = QStringLiteral("无法创建恢复审计父目录：") + auditParent;
        return false;
    }
    if (!forcePrivateDirectory(auditParent)) {
        reason = QStringLiteral("恢复审计父目录归属或形状无效：") + auditParent;
        return false;
    }
    if (!QFileInfo::exists(root) && !QDir().mkdir(root)) {
        reason = QStringLiteral("无法创建私有恢复审计目录：") + root;
        return false;
    }
    if (!forcePrivateDirectory(root) || !syncDirectory(auditParent)) {
        reason = QStringLiteral("恢复审计目录归属、权限或持久化无效：") + root;
        return false;
    }
    return true;
}

enum class AuditLockState { Available, Active, ConfirmedDead, Refused };

AuditLockState inspectAuditLock(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return AuditLockState::Available;
    if (info.isSymLink() || !ownedRegularFile(path))
        return AuditLockState::Refused;
    QLockFile lock(path);
    lock.setStaleLockTime(0);
    qint64 processId = 0;
    QString hostName;
    QString applicationName;
    if (!lock.getLockInfo(&processId, &hostName, &applicationName)
        || processId <= 0 || hostName != QSysInfo::machineHostName()
        || applicationName != QCoreApplication::applicationName()) {
        return AuditLockState::Refused;
    }
#ifdef Q_OS_UNIX
    errno = 0;
    if (::kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM)
        return AuditLockState::Active;
    if (errno == ESRCH)
        return AuditLockState::ConfirmedDead;
#endif
    return AuditLockState::Refused;
}

class ConservativeAuditLock
{
public:
    explicit ConservativeAuditLock(QString path)
        : m_path(std::move(path))
    {
    }

    ~ConservativeAuditLock() { release(); }

    bool acquire()
    {
#ifdef Q_OS_UNIX
        const QByteArray encoded = QFile::encodeName(m_path);
        m_descriptor = ::open(encoded.constData(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (m_descriptor < 0 || ::fstat(m_descriptor, &m_state) != 0) {
            if (m_descriptor >= 0)
                ::close(m_descriptor);
            m_descriptor = -1;
            return false;
        }
        const QByteArray payload = QByteArray::number(QCoreApplication::applicationPid()) + '\n'
            + QCoreApplication::applicationName().toUtf8() + '\n'
            + QSysInfo::machineHostName().toUtf8() + '\n';
        qsizetype offset = 0;
        while (offset < payload.size()) {
            const ssize_t written = ::write(m_descriptor,
                payload.constData() + offset, size_t(payload.size() - offset));
            if (written <= 0) {
                release();
                return false;
            }
            offset += written;
        }
        if (::fchmod(m_descriptor, 0600) != 0 || ::fsync(m_descriptor) != 0
            || !syncDirectory(QFileInfo(m_path).absolutePath())) {
            release();
            return false;
        }
        m_acquired = true;
        return true;
#else
        QFile file(m_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return false;
        const QByteArray payload = QByteArray::number(QCoreApplication::applicationPid()) + '\n'
            + QCoreApplication::applicationName().toUtf8() + '\n'
            + QSysInfo::machineHostName().toUtf8() + '\n';
        m_acquired = file.write(payload) == payload.size()
            && file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        file.close();
        if (!m_acquired)
            QFile::remove(m_path);
        return m_acquired;
#endif
    }

    bool release()
    {
#ifdef Q_OS_UNIX
        if (m_descriptor < 0)
            return true;
        const QByteArray encoded = QFile::encodeName(m_path);
        struct stat current {};
        bool removed = false;
        if (::lstat(encoded.constData(), &current) == 0
            && current.st_dev == m_state.st_dev && current.st_ino == m_state.st_ino
            && ::unlink(encoded.constData()) == 0) {
            removed = syncDirectory(QFileInfo(m_path).absolutePath());
        }
        const bool closed = ::close(m_descriptor) == 0;
        m_descriptor = -1;
#else
        const bool removed = !m_acquired || QFile::remove(m_path);
        const bool closed = true;
#endif
        m_acquired = false;
#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
        if (qEnvironmentVariableIsSet("WIZ_RECOVERY_TEST_AUDIT_RELEASE_FAILURE"))
            return false;
#endif
        return removed && closed;
    }

private:
    QString m_path;
    bool m_acquired = false;
#ifdef Q_OS_UNIX
    int m_descriptor = -1;
    struct stat m_state {};
#endif
};

bool validateAuditBytes(const QByteArray& bytes, qsizetype& entries, QString& reason)
{
    entries = 0;
    if (bytes.isEmpty())
        return true;
    if (bytes.size() > MaximumAuditBytes || !bytes.endsWith('\n')) {
        reason = QStringLiteral("恢复审计文件超限或缺少完整行结尾");
        return false;
    }
    const QList<QByteArray> lines = bytes.split('\n');
    entries = lines.size() - 1;
    if (entries > MaximumAuditEntries) {
        reason = QStringLiteral("恢复审计条目数超限");
        return false;
    }
    const QSet<QString> exactFields{
        QStringLiteral("destinationIdentity"),
        QStringLiteral("decision"),
        QStringLiteral("event")
    };
    for (qsizetype index = 0; index < entries; ++index) {
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(lines.at(index), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            reason = QStringLiteral("恢复审计 JSON 行无效");
            return false;
        }
        const QJsonObject object = document.object();
        const auto keys = object.keys();
        if (QJsonDocument(object).toJson(QJsonDocument::Compact) != lines.at(index)
            || keys.size() != exactFields.size()
            || QSet<QString>(keys.cbegin(), keys.cend()) != exactFields
            || !object.value(QStringLiteral("destinationIdentity")).isString()
            || !validIdentity(object.value(QStringLiteral("destinationIdentity")).toString())
            || !validEvent(object.value(QStringLiteral("decision")).toString(),
                object.value(QStringLiteral("event")).toString())) {
            reason = QStringLiteral("恢复审计字段、类型或值无效");
            return false;
        }
    }
    return true;
}

struct AuditSegment {
    QString path;
    bool exists = false;
    QByteArray bytes;
    qsizetype entries = 0;
#ifdef Q_OS_UNIX
    dev_t device = 0;
    ino_t inode = 0;
#endif
};

bool readSegment(const QString& path, AuditSegment& segment, QString& reason)
{
    segment = AuditSegment{};
    segment.path = path;
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return true;
    if (!ownedRegularFile(path) || info.size() > MaximumAuditBytes) {
        reason = QStringLiteral("恢复审计段形状、归属或大小无效：") + path;
        return false;
    }
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly)) {
        reason = QStringLiteral("无法读取恢复审计段：") + path;
        return false;
    }
#ifdef Q_OS_UNIX
    struct stat opened {}, current {};
    const QByteArray encoded = QFile::encodeName(path);
    if (::fstat(input.handle(), &opened) != 0 || !S_ISREG(opened.st_mode)
        || opened.st_uid != ::geteuid() || ::lstat(encoded.constData(), &current) != 0
        || !S_ISREG(current.st_mode) || current.st_dev != opened.st_dev
        || current.st_ino != opened.st_ino) {
        reason = QStringLiteral("恢复审计段读取身份发生变化：") + path;
        return false;
    }
    segment.device = opened.st_dev;
    segment.inode = opened.st_ino;
#endif
    segment.bytes = input.read(MaximumAuditBytes + 1);
    if (segment.bytes.size() != info.size()
        || !validateAuditBytes(segment.bytes, segment.entries, reason)) {
        reason += QStringLiteral("；恢复审计段读取或格式无效：") + path;
        return false;
    }
    segment.exists = true;
    return true;
}

bool sameSegments(const std::array<AuditSegment, MaximumAuditArchives + 1>& segments)
{
    for (const auto& expected : segments) {
        AuditSegment current;
        QString reason;
        if (!readSegment(expected.path, current, reason)
            || current.exists != expected.exists || current.bytes != expected.bytes)
            return false;
#ifdef Q_OS_UNIX
        if (current.exists && (current.device != expected.device || current.inode != expected.inode))
            return false;
#endif
    }
    return true;
}

bool makePrivate(const AuditSegment& segment)
{
    if (!segment.exists)
        return true;
    const auto forbidden = QFileDevice::ReadGroup | QFileDevice::WriteGroup
        | QFileDevice::ExeGroup | QFileDevice::ReadOther | QFileDevice::WriteOther
        | QFileDevice::ExeOther | QFileDevice::ExeOwner | QFileDevice::ExeUser;
    const auto permissions = QFileInfo(segment.path).permissions();
    if (!(permissions & forbidden) && permissions.testFlag(QFileDevice::ReadOwner)
        && permissions.testFlag(QFileDevice::WriteOwner))
        return true;
    return QFile::setPermissions(segment.path, QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        && syncRegularFile(segment.path);
}

}

namespace WizMarkdownExportRecoveryAudit {

QString auditDirectory()
{
#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
    const QString overrideBase = qEnvironmentVariable("WIZ_RECOVERY_TEST_APP_LOCAL_DATA");
    if (!overrideBase.isEmpty())
        return QDir(overrideBase).filePath(QStringLiteral("recovery-audit/v1"));
#endif
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation))
        .filePath(QStringLiteral("recovery-audit/v1"));
}

QString auditLogPath()
{
    return QDir(auditDirectory()).filePath(QStringLiteral("recovery.jsonl"));
}

QString auditLockPath()
{
    return QDir(auditDirectory()).filePath(QStringLiteral("recovery.jsonl.lock"));
}

QString auditArchivePath(int index)
{
    return index >= 1 && index <= MaximumAuditArchives
        ? auditLogPath() + QStringLiteral(".%1").arg(index) : QString();
}

TakeoverResult takeOverConfirmedDeadAuditLock()
{
    const QString root = auditDirectory();
    const QString parent = QFileInfo(root).absolutePath();
    const QString appData = QFileInfo(parent).absolutePath();
    for (const QString& directory : {appData, parent, root}) {
        const QFileInfo info(directory);
        if (!info.exists() && !info.isSymLink())
            return {TakeoverOutcome::None, QStringLiteral("审计目录尚不存在；未创建目录")};
        if (!ownedDirectory(directory))
            return {TakeoverOutcome::Refused, QStringLiteral("审计目录归属或形状无效")};
    }
    const QString path = auditLockPath();
    const AuditLockState initial = inspectAuditLock(path);
    if (initial == AuditLockState::Available)
        return {TakeoverOutcome::None, QStringLiteral("没有审计写锁需要接管")};
    if (initial == AuditLockState::Active)
        return {TakeoverOutcome::Active, QStringLiteral("审计写锁仍由活动进程持有")};
    if (initial != AuditLockState::ConfirmedDead)
        return {TakeoverOutcome::Refused, QStringLiteral("审计锁所有者无法安全确认")};

#ifdef Q_OS_UNIX
    const QByteArray rootName = QFile::encodeName(root);
    const int directory = ::open(rootName.constData(),
        O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY);
    if (directory < 0)
        return {TakeoverOutcome::Failed, QStringLiteral("无法打开审计目录")};
    const auto finish = [directory](TakeoverOutcome outcome, const QString& detail) {
        const bool closed = ::close(directory) == 0;
        return TakeoverResult{closed ? outcome : TakeoverOutcome::Failed, detail};
    };
    struct stat directoryState {};
    if (::fstat(directory, &directoryState) != 0 || !S_ISDIR(directoryState.st_mode)
        || directoryState.st_uid != ::geteuid()) {
        return finish(TakeoverOutcome::Refused, QStringLiteral("审计目录在接管前发生变化"));
    }
    // Resolve and unlink only inside this opened audit directory, not through a
    // potentially replaced final path component.
    constexpr const char* lockName = "recovery.jsonl.lock";
    const int descriptor = ::openat(directory, lockName, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        return finish(TakeoverOutcome::Failed, QStringLiteral("无法打开已确认死亡的审计锁"));
    struct stat opened {};
    char original[4097];
    const ssize_t originalSize = ::pread(descriptor, original, sizeof(original), 0);
    const bool validOpened = ::fstat(descriptor, &opened) == 0
        && S_ISREG(opened.st_mode) && opened.st_uid == ::geteuid()
        && originalSize > 0 && originalSize <= 4096;
    if (!validOpened) {
        ::close(descriptor);
        return finish(TakeoverOutcome::Refused,
            QStringLiteral("死亡审计锁快照未获确认；保留锁"));
    }
    // Serialize cooperating explicit takeovers on the old inode. Otherwise a
    // second remover could race a new writer after the first removes the lock.
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        const bool busy = errno == EWOULDBLOCK || errno == EAGAIN;
        ::close(descriptor);
        return finish(busy ? TakeoverOutcome::Active : TakeoverOutcome::Failed,
            QStringLiteral("另一个审计锁接管正在进行，或无法确认独占访问"));
    }
    if (!auditCheckpoint("takeover-opened")) {
        ::close(descriptor);
        return finish(TakeoverOutcome::Failed, QStringLiteral("死亡审计锁快照未获确认；保留锁"));
    }
    const AuditLockState verified = inspectAuditLock(path);
    struct stat current {}, currentDirectory {};
    char rechecked[4097];
    const ssize_t recheckedSize = ::pread(descriptor, rechecked, sizeof(rechecked), 0);
    const bool same = verified == AuditLockState::ConfirmedDead
        && ownedDirectory(appData) && ownedDirectory(parent)
        && ::lstat(rootName.constData(), &currentDirectory) == 0
        && S_ISDIR(currentDirectory.st_mode)
        && currentDirectory.st_dev == directoryState.st_dev
        && currentDirectory.st_ino == directoryState.st_ino
        && ::fstatat(directory, lockName, &current, AT_SYMLINK_NOFOLLOW) == 0
        && S_ISREG(current.st_mode) && current.st_uid == ::geteuid()
        && current.st_dev == opened.st_dev && current.st_ino == opened.st_ino
        && originalSize == recheckedSize
        && QByteArray(original, originalSize) == QByteArray(rechecked, recheckedSize);
    if (!same || !auditCheckpoint("takeover-before-unlink")) {
        ::close(descriptor);
        return finish(verified == AuditLockState::Active ? TakeoverOutcome::Active
                : same ? TakeoverOutcome::Failed : TakeoverOutcome::Refused,
            QStringLiteral("审计锁或目录在接管核对期间变化；保留现场"));
    }
    const bool removed = ::unlinkat(directory, lockName, 0) == 0;
    const bool closed = ::close(descriptor) == 0;
    if (!removed || !closed || !auditCheckpoint("takeover-unlinked")
        || ::fsync(directory) != 0) {
        return finish(TakeoverOutcome::Failed,
            QStringLiteral("审计死亡锁删除未确认持久化；必须重新检查现场"));
    }
    return finish(TakeoverOutcome::Removed,
        QStringLiteral("已显式删除同机同程序死亡审计锁；尚未写入日志或执行恢复"));
#else
    return {TakeoverOutcome::Refused, QStringLiteral("此平台不能确认死亡审计锁接管")};
#endif
}

static AppendResult appendEvent(const QString& destinationIdentity,
    const QString& decision, const QString& eventName)
{
    if (!validIdentity(destinationIdentity)) {
        return {AppendOutcome::Refused,
            QStringLiteral("审计目标身份必须是 24 位小写十六进制摘要")};
    }
    QString reason;
    if (!ensureAuditDirectory(reason))
        return {AppendOutcome::Refused, reason};

    const QString lockPath = auditLockPath();
    const AuditLockState existingLock = inspectAuditLock(lockPath);
    if (existingLock == AuditLockState::Active
        || existingLock == AuditLockState::ConfirmedDead) {
        return {AppendOutcome::Busy,
            QStringLiteral("恢复审计锁正在使用或已遗留：") + lockPath};
    }
    if (existingLock == AuditLockState::Refused) {
        return {AppendOutcome::Refused,
            QStringLiteral("恢复审计锁形状或所有者无法安全确认：") + lockPath};
    }

    ConservativeAuditLock lock(lockPath);
    if (!lock.acquire()) {
        return {AppendOutcome::Busy,
            QStringLiteral("无法取得恢复审计锁：") + lockPath};
    }
    if (!auditCheckpoint("lock-acquired"))
        return {AppendOutcome::Failed, QStringLiteral("审计锁取得后的检查点未完成")};
    const QString logPath = auditLogPath();
    const QJsonObject event{
        {QStringLiteral("destinationIdentity"), destinationIdentity},
        {QStringLiteral("decision"), decision},
        {QStringLiteral("event"), eventName}
    };
    const QByteArray line = QJsonDocument(event).toJson(QJsonDocument::Compact) + '\n';
    if (line.size() > MaximumAuditBytes)
        return {AppendOutcome::Refused, QStringLiteral("单条恢复审计事件超限")};

    // Validate all retained segments before deleting/moving any of them. A
    // single .1 gap plus a full live log is the known interrupted shift; do
    // not drop .2 again when resuming that state.
    const QStringList allowed{QFileInfo(auditArchivePath(1)).fileName(),
        QFileInfo(auditArchivePath(2)).fileName(), QFileInfo(lockPath).fileName()};
    const auto siblings = QDir(auditDirectory()).entryList(
        {QFileInfo(logPath).fileName() + QStringLiteral(".*")},
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QString& sibling : siblings) {
        if (!allowed.contains(sibling))
            return {AppendOutcome::Refused, QStringLiteral("存在未知审计段，保留并拒绝轮换：") + sibling};
    }
    std::array<AuditSegment, MaximumAuditArchives + 1> segments;
    for (int i = 0; i <= MaximumAuditArchives; ++i) {
        if (!readSegment(i ? auditArchivePath(i) : logPath, segments[i], reason)
            || (i && segments[i].exists && segments[i].bytes.isEmpty()))
            return {AppendOutcome::Refused, reason + QStringLiteral("；归档必须是完整非空审计段")};
    }
    const bool rotate = !segments[0].bytes.isEmpty()
        && (segments[0].entries >= MaximumAuditEntries
            || segments[0].bytes.size() > MaximumAuditBytes - line.size());
    if (!segments[1].exists && segments[2].exists && !rotate) {
        return {AppendOutcome::Refused,
            QStringLiteral("审计归档缺口不能解释为待续轮换，保留现场")};
    }
    if (!sameSegments(segments))
        return {AppendOutcome::Refused, QStringLiteral("审计段在轮换前发生变化")};
    for (const auto& segment : segments) {
        if (!makePrivate(segment))
            return {AppendOutcome::Failed, QStringLiteral("无法持久化审计段的私有权限")};
    }
    const auto moveSegment = [&](int from, int to, const char* checkpoint) {
        if (!sameSegments(segments) || segments[to].exists
            || !QFile::rename(segments[from].path, segments[to].path))
            return false;
        const QString sourcePath = segments[from].path;
        const QString destinationPath = segments[to].path;
        segments[to] = segments[from];
        segments[to].path = destinationPath;
        segments[from] = AuditSegment{};
        segments[from].path = sourcePath;
        return syncDirectory(auditDirectory()) && auditCheckpoint(checkpoint);
    };
    if (rotate) {
        if (segments[1].exists && segments[2].exists) {
            if (!sameSegments(segments) || !QFile::remove(segments[2].path))
                return {AppendOutcome::Failed, QStringLiteral("无法移除已验证的最旧审计段")};
            const QString oldest = segments[2].path;
            segments[2] = AuditSegment{};
            segments[2].path = oldest;
            if (!syncDirectory(auditDirectory()) || !auditCheckpoint("oldest-removed"))
                return {AppendOutcome::Failed, QStringLiteral("最旧审计段移除后的持久化未完成")};
        }
        if (segments[1].exists && !moveSegment(1, 2, "archive-1-to-2"))
            return {AppendOutcome::Failed, QStringLiteral("审计归档移动未完成，保留可检查状态")};
        if (!moveSegment(0, 1, "live-archived"))
            return {AppendOutcome::Failed, QStringLiteral("当前审计段归档未完成，保留可检查状态")};
    }
    if (!sameSegments(segments))
        return {AppendOutcome::Refused, QStringLiteral("审计段在提交前发生变化")};
    const QByteArray next = segments[0].bytes + line;
    if (!auditCheckpoint("before-live-commit"))
        return {AppendOutcome::Failed, QStringLiteral("审计提交前的检查点未完成")};

    QSaveFile output(logPath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)
        || !output.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || output.write(next) != next.size() || !output.commit()) {
        output.cancelWriting();
        return {AppendOutcome::Failed,
            QStringLiteral("无法原子提交恢复审计事件：") + logPath};
    }
    if (!auditCheckpoint("live-committed") || !ownedRegularFile(logPath)
        || !QFile::setPermissions(logPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || !syncRegularFile(logPath) || !auditCheckpoint("live-file-synced")
        || !syncDirectory(auditDirectory()) || !auditCheckpoint("live-directory-synced")) {
        return {AppendOutcome::Failed,
            QStringLiteral("无法持久化恢复审计事件：") + logPath};
    }
    if (!lock.release()) {
        return {AppendOutcome::Failed,
            QStringLiteral("审计事件已提交，但审计锁释放未完整持久化：") + lockPath};
    }
    return {AppendOutcome::Recorded,
        QStringLiteral("已记录隐私最小化的恢复决策事件：") + logPath};
}

AppendResult appendConfirmIntent(const QString& destinationIdentity)
{
    return appendEvent(destinationIdentity, QStringLiteral("confirm"), QStringLiteral("requested"));
}

AppendResult appendRetain(const QString& destinationIdentity)
{
    return appendEvent(destinationIdentity, QStringLiteral("retain"), QStringLiteral("retained"));
}

AppendResult appendConfirmResult(const QString& destinationIdentity,
    const WizMarkdownExportRecovery::Result& recoveryResult)
{
    using namespace WizMarkdownExportRecovery;
    if ((recoveryResult.lockRelease != LockReleaseOutcome::NotRequested
            && recoveryResult.lockRelease != LockReleaseOutcome::Released)
        || ((recoveryResult.recovery == RecoveryOutcome::RolledBack
                || recoveryResult.recovery == RecoveryOutcome::Finalized)
            && recoveryResult.lockRelease != LockReleaseOutcome::Released)) {
        return {AppendOutcome::Refused,
            QStringLiteral("目标锁尚未确认释放，不取得审计锁或追加结果")};
    }
    QString event;
    switch (recoveryResult.recovery) {
    case RecoveryOutcome::None: event = QStringLiteral("none"); break;
    case RecoveryOutcome::RolledBack: event = QStringLiteral("rolled-back"); break;
    case RecoveryOutcome::Finalized: event = QStringLiteral("finalized"); break;
    case RecoveryOutcome::Busy: event = QStringLiteral("busy"); break;
    case RecoveryOutcome::Refused: event = QStringLiteral("refused"); break;
    default:
        return {AppendOutcome::Refused, QStringLiteral("未知恢复结果，不追加审计事件")};
    }
    return appendEvent(destinationIdentity, QStringLiteral("confirm"), event);
}

}
