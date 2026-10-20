#include "WizMarkdownExportRecovery.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLockFile>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>

#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
#include <QElapsedTimer>
#include <QThread>
#endif

#include <utility>
#include <atomic>

#ifdef Q_OS_UNIX
#include <cerrno>
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {

constexpr qint64 MaximumManifestBytes = 64 * 1024;

const QString noRecoverySummary = QStringLiteral("未发现待恢复的 Markdown 导出。");
const QString awaitingSummary = QStringLiteral(
    "检测到上次 Markdown 导出未完整结束。默认将保留现场，不会覆盖现有文件。");
const QString busySummary = QStringLiteral(
    "此 Markdown 导出目标正在使用或保留了写入锁。默认将保留现场。");
const QString refusedSummary = QStringLiteral(
    "无法安全识别 Markdown 导出恢复现场。已保留全部文件。");

bool safeBaseName(const QString& name)
{
    return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..")
        && QFileInfo(name).fileName() == name && !name.contains('/') && !name.contains('\\');
}

QString resourceNameForDestination(const QString& destination)
{
    QString name = QFileInfo(destination).fileName();
    const int dot = name.lastIndexOf('.');
    if (dot >= 0)
        name.truncate(dot);
    return name.toHtmlEscaped() + QStringLiteral("_files");
}

bool validDigest(const QString& digest)
{
    static const QRegularExpression pattern(QStringLiteral("^[0-9a-f]{64}$"));
    return pattern.match(digest).hasMatch();
}

bool exactKeys(const QJsonObject& object, const QSet<QString>& expected)
{
    const auto keys = object.keys();
    return keys.size() == expected.size()
        && QSet<QString>(keys.cbegin(), keys.cend()) == expected;
}

bool validateManifest(const QJsonObject& object, const QString& destination,
                      QString& reason)
{
    static const QSet<QString> fields{
        QStringLiteral("schema"),
        QStringLiteral("destinationName"),
        QStringLiteral("resourcesName"),
        QStringLiteral("oldMarkdownExisted"),
        QStringLiteral("oldMarkdownSha256"),
        QStringLiteral("newMarkdownSha256"),
        QStringLiteral("oldResourcesExisted"),
        QStringLiteral("oldResourcesSha256"),
        QStringLiteral("newResourcesExisted"),
        QStringLiteral("newResourcesSha256")
    };
    if (!exactKeys(object, fields)) {
        reason = QStringLiteral("清单字段集合不受支持");
        return false;
    }
    if (!object.value(QStringLiteral("schema")).isDouble()
        || object.value(QStringLiteral("schema")).toDouble() != 1.0) {
        reason = QStringLiteral("清单版本不受支持");
        return false;
    }

    const auto destinationValue = object.value(QStringLiteral("destinationName"));
    const auto resourcesValue = object.value(QStringLiteral("resourcesName"));
    if (!destinationValue.isString() || !resourcesValue.isString()) {
        reason = QStringLiteral("清单文件名类型无效");
        return false;
    }
    const QString destinationName = destinationValue.toString();
    const QString resourcesName = resourcesValue.toString();
    if (!safeBaseName(destinationName) || !safeBaseName(resourcesName)
        || destinationName != QFileInfo(destination).fileName()
        || resourcesName != resourceNameForDestination(destination)) {
        reason = QStringLiteral("清单目标与请求目标不匹配");
        return false;
    }

    const QStringList existedFields{
        QStringLiteral("oldMarkdownExisted"),
        QStringLiteral("oldResourcesExisted"),
        QStringLiteral("newResourcesExisted")
    };
    for (const QString& field : existedFields) {
        if (!object.value(field).isBool()) {
            reason = QStringLiteral("清单存在性字段类型无效");
            return false;
        }
    }

    const QStringList digestFields{
        QStringLiteral("oldMarkdownSha256"),
        QStringLiteral("newMarkdownSha256"),
        QStringLiteral("oldResourcesSha256"),
        QStringLiteral("newResourcesSha256")
    };
    for (const QString& field : digestFields) {
        if (!object.value(field).isString()) {
            reason = QStringLiteral("清单摘要字段类型无效");
            return false;
        }
    }

    const auto digestMatchesExistence = [&](const QString& existsField,
                                            const QString& digestField) {
        const bool exists = object.value(existsField).toBool();
        const QString digest = object.value(digestField).toString();
        return exists ? validDigest(digest) : digest.isEmpty();
    };
    if (!digestMatchesExistence(QStringLiteral("oldMarkdownExisted"),
            QStringLiteral("oldMarkdownSha256"))
        || !validDigest(object.value(QStringLiteral("newMarkdownSha256")).toString())
        || !digestMatchesExistence(QStringLiteral("oldResourcesExisted"),
            QStringLiteral("oldResourcesSha256"))
        || !digestMatchesExistence(QStringLiteral("newResourcesExisted"),
            QStringLiteral("newResourcesSha256"))) {
        reason = QStringLiteral("清单摘要与存在性声明不一致");
        return false;
    }
    return true;
}

bool validateContentLayout(const QString& content, const QString& resourcesName,
                           bool resourcesExpected, bool requireExpected,
                           QString& reason)
{
    const QFileInfo contentInfo(content);
    if (contentInfo.isSymLink() || !contentInfo.isDir()) {
        reason = QStringLiteral("恢复内容目录缺失或类型无效：") + content;
        return false;
    }
    const auto contentEntries = QDir(content).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    const qsizetype expectedCount = resourcesExpected ? 1 : 0;
    if ((requireExpected && contentEntries.size() != expectedCount)
        || (!requireExpected && contentEntries.size() > expectedCount)) {
        reason = QStringLiteral("恢复内容目录条目数与清单不一致：") + content;
        return false;
    }
    for (const QFileInfo& entry : contentEntries) {
        if (entry.fileName() != resourcesName || entry.isSymLink() || !entry.isDir()) {
            reason = QStringLiteral("恢复内容目录包含未知条目：") + entry.filePath();
            return false;
        }
    }
    return true;
}

bool validateRecoveryLayout(const QString& recovery, const QString& resourcesName,
                            bool resourcesExpected, QString& reason)
{
    const QSet<QString> allowedRoot{
        QStringLiteral("transaction.json"),
        QStringLiteral("new-markdown"),
        QStringLiteral("content"),
        QStringLiteral("previous-resources"),
        QStringLiteral("returned-resources")
    };
    const QSet<QString> requiredRoot{
        QStringLiteral("transaction.json"),
        QStringLiteral("new-markdown"),
        QStringLiteral("content")
    };
    QSet<QString> seenRoot;
    const auto rootEntries = QDir(recovery).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : rootEntries) {
        if (!allowedRoot.contains(entry.fileName()) || entry.isSymLink()) {
            reason = QStringLiteral("恢复目录包含未知或链接条目：") + entry.filePath();
            return false;
        }
        const bool mustBeFile = entry.fileName() == QStringLiteral("transaction.json")
            || entry.fileName() == QStringLiteral("new-markdown");
        if ((mustBeFile && !entry.isFile()) || (!mustBeFile && !entry.isDir())) {
            reason = QStringLiteral("恢复目录条目类型无效：") + entry.filePath();
            return false;
        }
        seenRoot.insert(entry.fileName());
    }
    QSet<QString> missingRoot = requiredRoot;
    missingRoot.subtract(seenRoot);
    if (!missingRoot.isEmpty()) {
        reason = QStringLiteral("恢复目录缺少必需条目：") + recovery;
        return false;
    }
    return validateContentLayout(QDir(recovery).filePath(QStringLiteral("content")),
        resourcesName, resourcesExpected, false, reason);
}

bool readStrictManifest(const QString& destination, QJsonObject& object,
                        QString& reason)
{
    const QString manifestPath = QDir(
        WizMarkdownExportRecovery::recoveryDirectory(destination))
        .filePath(QStringLiteral("transaction.json"));
    const QFileInfo manifestInfo(manifestPath);
    if (manifestInfo.isSymLink() || !manifestInfo.isFile()
        || manifestInfo.size() < 2 || manifestInfo.size() > MaximumManifestBytes) {
        reason = QStringLiteral("恢复清单形状或大小无效：") + manifestPath;
        return false;
    }
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::ReadOnly)) {
        reason = QStringLiteral("无法只读打开恢复清单：") + manifestPath;
        return false;
    }
    const QByteArray bytes = manifestFile.read(MaximumManifestBytes + 1);
    if (bytes.size() != manifestInfo.size()) {
        reason = QStringLiteral("恢复清单读取期间发生变化：") + manifestPath;
        return false;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        reason = QStringLiteral("恢复清单 JSON 无效：") + manifestPath;
        return false;
    }
    QString validationReason;
    if (!validateManifest(document.object(), destination, validationReason)) {
        reason = validationReason + QStringLiteral("：") + manifestPath;
        return false;
    }
    if (!validateRecoveryLayout(WizMarkdownExportRecovery::recoveryDirectory(destination),
            document.object().value(QStringLiteral("resourcesName")).toString(),
            document.object().value(QStringLiteral("newResourcesExisted")).toBool(),
            validationReason)) {
        reason = validationReason;
        return false;
    }
    object = document.object();
    return true;
}

QByteArray fileDigest(const QString& path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile())
        return {};
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file) || file.error() != QFileDevice::NoError)
        return {};
    return hash.result().toHex();
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
        const QByteArray name = relative.toUtf8();
        const QString absolute = directory.filePath(relative);
        const QFileInfo info(absolute);
        QFile file(absolute);
        if (info.isSymLink() || !info.isFile() || !file.open(QIODevice::ReadOnly))
            return {};
        hash.addData(QByteArray::number(name.size()));
        hash.addData(QByteArrayView(":", 1));
        hash.addData(name);
        hash.addData(QByteArray::number(info.size()));
        hash.addData(QByteArrayView(":", 1));
        while (!file.atEnd()) {
            const QByteArray bytes = file.read(64 * 1024);
            if (bytes.isEmpty() && file.error() != QFileDevice::NoError)
                return {};
            hash.addData(bytes);
        }
        if (file.error() != QFileDevice::NoError)
            return {};
    }
    return hash.result().toHex();
}

bool missingPath(const QString& path)
{
    const QFileInfo info(path);
    return !info.exists() && !info.isSymLink();
}

bool fileMatches(const QString& path, bool expected, const QString& digest)
{
    return expected ? fileDigest(path) == digest.toLatin1() : missingPath(path);
}

bool directoryMatches(const QString& path, bool expected, const QString& digest)
{
    return expected ? directoryDigest(path) == digest.toLatin1() : missingPath(path);
}

bool baseShapesSupported(const QString& destination, QString& reason)
{
    const QFileInfo destinationInfo(destination);
    if (destinationInfo.fileName().isEmpty() || destinationInfo.isSymLink()
        || (destinationInfo.exists() && !destinationInfo.isFile())) {
        reason = QStringLiteral("目标路径形状不受支持：") + destination;
        return false;
    }
    const QString resources = QDir(destinationInfo.absolutePath()).filePath(
        resourceNameForDestination(destination));
    const QFileInfo resourcesInfo(resources);
    if (resourcesInfo.isSymLink()
        || (resourcesInfo.exists() && !resourcesInfo.isDir())) {
        reason = QStringLiteral("资源路径形状不受支持：") + resources;
        return false;
    }
    const QString recovery = WizMarkdownExportRecovery::recoveryDirectory(destination);
    const QFileInfo recoveryInfo(recovery);
    if (recoveryInfo.isSymLink() || (recoveryInfo.exists() && !recoveryInfo.isDir())) {
        reason = QStringLiteral("恢复路径不是私有普通目录：") + recovery;
        return false;
    }
    return true;
}

bool syncDirectoryPath(const QString& path)
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

void recoveryTestCheckpoint(const char* name)
{
#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
    const QByteArray requested = qgetenv("WIZ_RECOVERY_TEST_CHECKPOINT");
    const QString readyPath = qEnvironmentVariable("WIZ_RECOVERY_TEST_READY");
    if (requested != name || readyPath.isEmpty())
        return;
    QFile ready(readyPath);
    const QByteArray payload(name);
    if (!ready.open(QIODevice::WriteOnly | QIODevice::NewOnly)
        || ready.write(payload) != payload.size()) {
        return;
    }
    ready.close();
    if (ready.error() != QFileDevice::NoError || !syncRegularFile(readyPath)
        || !syncDirectoryPath(QFileInfo(readyPath).absolutePath())) {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000)
        QThread::msleep(10);
#else
    Q_UNUSED(name);
#endif
}

bool recoveryStepAllowed(const char* name)
{
    recoveryTestCheckpoint(name);
#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
    return !qgetenv("WIZ_RECOVERY_TEST_FAILURE_AT").split(',').contains(QByteArray(name));
#else
    return true;
#endif
}

bool syncTree(const QString& path)
{
    const QFileInfo root(path);
    if (root.isSymLink() || !root.isDir())
        return false;
    const auto entries = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (entry.isSymLink())
            return false;
        if (entry.isDir()) {
            if (!syncTree(entry.filePath()))
                return false;
        } else if (!entry.isFile() || !syncRegularFile(entry.filePath())) {
            return false;
        }
    }
    return syncDirectoryPath(path);
}

bool removeRegularFileAndSync(const QString& path, const char* removedCheckpoint = nullptr)
{
#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(path);
    struct stat state {};
    if (::lstat(encoded.constData(), &state) != 0 || !S_ISREG(state.st_mode)
        || ::unlink(encoded.constData()) != 0) {
        return false;
    }
#else
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isFile() || !QFile::remove(path))
        return false;
#endif
    return (!removedCheckpoint || recoveryStepAllowed(removedCheckpoint))
        && syncDirectoryPath(QFileInfo(path).absolutePath());
}

bool durableRenameDirectory(const QString& source, const QString& destination,
                            const char* checkpoint, const char* sourceSyncStep,
                            const char* destinationSyncStep)
{
    const QFileInfo sourceInfo(source);
    const QFileInfo destinationInfo(destination);
    if (sourceInfo.isSymLink() || !sourceInfo.isDir()
        || destinationInfo.exists() || destinationInfo.isSymLink()) {
        return false;
    }
    const QString sourceParent = sourceInfo.absolutePath();
    const QString destinationParent = destinationInfo.absolutePath();
    const QFileInfo sourceParentInfo(sourceParent);
    const QFileInfo destinationParentInfo(destinationParent);
    if (sourceParentInfo.isSymLink() || !sourceParentInfo.isDir()
        || destinationParentInfo.isSymLink() || !destinationParentInfo.isDir()
        || !QDir().rename(source, destination)) {
        return false;
    }
    recoveryTestCheckpoint(checkpoint);
    if (!recoveryStepAllowed(sourceSyncStep) || !syncDirectoryPath(sourceParent))
        return false;
    return sourceParent == destinationParent
        || (recoveryStepAllowed(destinationSyncStep) && syncDirectoryPath(destinationParent));
}

bool syncExpectedLivePair(const QString& destination, const QJsonObject& manifest,
                          bool useNewState, QString& reason)
{
    const bool markdownExpected = useNewState
        || manifest.value(QStringLiteral("oldMarkdownExisted")).toBool();
    const QString markdownDigest = useNewState
        ? manifest.value(QStringLiteral("newMarkdownSha256")).toString()
        : manifest.value(QStringLiteral("oldMarkdownSha256")).toString();
    const bool resourcesExpected = manifest.value(useNewState
        ? QStringLiteral("newResourcesExisted")
        : QStringLiteral("oldResourcesExisted")).toBool();
    const QString resourcesDigest = manifest.value(useNewState
        ? QStringLiteral("newResourcesSha256")
        : QStringLiteral("oldResourcesSha256")).toString();
    const QString resources = QDir(QFileInfo(destination).absolutePath()).filePath(
        manifest.value(QStringLiteral("resourcesName")).toString());

    if (!fileMatches(destination, markdownExpected, markdownDigest)
        || !directoryMatches(resources, resourcesExpected, resourcesDigest)) {
        reason = QStringLiteral("同步前实时导出状态与清单不一致");
        return false;
    }
    if ((markdownExpected && !syncRegularFile(destination))
        || (resourcesExpected && !syncTree(resources))
        || !syncDirectoryPath(QFileInfo(destination).absolutePath())) {
        reason = QStringLiteral("无法持久化恢复后的实时导出状态");
        return false;
    }
    if (!fileMatches(destination, markdownExpected, markdownDigest)
        || !directoryMatches(resources, resourcesExpected, resourcesDigest)) {
        reason = QStringLiteral("同步期间实时导出状态发生变化");
        return false;
    }
    return true;
}

bool removeTreeNoFollow(const QString& path, const char* cleanupCheckpoint = nullptr)
{
    const QFileInfo root(path);
    if (root.isSymLink() || root.isFile()) {
        return QFile::remove(path)
            && (!cleanupCheckpoint || recoveryStepAllowed(cleanupCheckpoint));
    }
    if (!root.isDir())
        return !root.exists();
    const auto entries = QDir(path).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        if (!removeTreeNoFollow(entry.filePath(), cleanupCheckpoint))
            return false;
    }
    return QDir().rmdir(path)
        && (!cleanupCheckpoint || recoveryStepAllowed(cleanupCheckpoint));
}

struct PathIdentity {
#ifdef Q_OS_UNIX
    dev_t device = 0;
    ino_t inode = 0;
#endif
    bool valid = false;
};

PathIdentity pathIdentity(const QString& path)
{
    PathIdentity identity;
#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(path);
    struct stat state {};
    if (::lstat(encoded.constData(), &state) == 0 && S_ISDIR(state.st_mode)) {
        identity.device = state.st_dev;
        identity.inode = state.st_ino;
        identity.valid = true;
    }
#else
    const QFileInfo info(path);
    identity.valid = info.isDir() && !info.isSymLink();
#endif
    return identity;
}

bool samePathIdentity(const QString& path, const PathIdentity& identity)
{
    if (!identity.valid)
        return false;
#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(path);
    struct stat state {};
    return ::lstat(encoded.constData(), &state) == 0 && S_ISDIR(state.st_mode)
        && state.st_dev == identity.device && state.st_ino == identity.inode;
#else
    const QFileInfo info(path);
    return info.isDir() && !info.isSymLink();
#endif
}

bool createPrivateDirectory(const QString& path, bool* created = nullptr,
                            const char* createdCheckpoint = nullptr)
{
    if (created)
        *created = false;
#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(path);
    if (::mkdir(encoded.constData(), 0700) != 0)
        return false;
#else
    if (!QDir().mkdir(path))
        return false;
#endif
    if (created)
        *created = true;
    return QFile::setPermissions(path,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner)
        && (!createdCheckpoint || recoveryStepAllowed(createdCheckpoint))
        && syncDirectoryPath(QFileInfo(path).absolutePath());
}

struct AtomicWriteSteps {
    const char* committed;
    const char* fileSync;
    const char* directorySync;
};

bool writePrivateAtomicFile(const QString& path, const QByteArray& bytes,
                            const AtomicWriteSteps& steps)
{
    if (QFileInfo::exists(path) || QFileInfo(path).isSymLink())
        return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || file.write(bytes) != bytes.size() || !file.commit()) {
        file.cancelWriting();
        return false;
    }
    return recoveryStepAllowed(steps.committed)
        && recoveryStepAllowed(steps.fileSync) && syncRegularFile(path)
        && recoveryStepAllowed(steps.directorySync)
        && syncDirectoryPath(QFileInfo(path).absolutePath());
}

QJsonObject recoveryManifest(const QString& destination,
                             bool oldMarkdownExisted,
                             const QByteArray& oldMarkdownDigest,
                             const QByteArray& newMarkdownDigest,
                             bool oldResourcesExisted,
                             const QByteArray& oldResourcesDigest,
                             bool newResourcesExisted,
                             const QByteArray& newResourcesDigest)
{
    return {
        {QStringLiteral("schema"), 1},
        {QStringLiteral("destinationName"), QFileInfo(destination).fileName()},
        {QStringLiteral("resourcesName"), resourceNameForDestination(destination)},
        {QStringLiteral("oldMarkdownExisted"), oldMarkdownExisted},
        {QStringLiteral("oldMarkdownSha256"), QString::fromLatin1(oldMarkdownDigest)},
        {QStringLiteral("newMarkdownSha256"), QString::fromLatin1(newMarkdownDigest)},
        {QStringLiteral("oldResourcesExisted"), oldResourcesExisted},
        {QStringLiteral("oldResourcesSha256"), QString::fromLatin1(oldResourcesDigest)},
        {QStringLiteral("newResourcesExisted"), newResourcesExisted},
        {QStringLiteral("newResourcesSha256"), QString::fromLatin1(newResourcesDigest)}
    };
}

class ConservativeDestinationLock
{
public:
    explicit ConservativeDestinationLock(QString path)
        : m_path(std::move(path))
    {
    }

    ~ConservativeDestinationLock()
    {
        release();
    }

    bool acquire()
    {
        if (m_acquired)
            return true;
#ifdef Q_OS_UNIX
        const QByteArray encoded = QFile::encodeName(m_path);
        m_descriptor = ::open(encoded.constData(),
            O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (m_descriptor < 0)
            return false;
        if (::fstat(m_descriptor, &m_state) != 0) {
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
            || !syncParentDirectory()) {
            release();
            return false;
        }
        m_acquired = true;
        return true;
#else
        m_file.setFileName(m_path);
        if (!m_file.open(QIODevice::WriteOnly | QIODevice::NewOnly))
            return false;
        const QByteArray payload = QByteArray::number(QCoreApplication::applicationPid()) + '\n'
            + QCoreApplication::applicationName().toUtf8() + '\n'
            + QSysInfo::machineHostName().toUtf8() + '\n';
        if (m_file.write(payload) != payload.size()
            || !m_file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)) {
            m_file.close();
            QFile::remove(m_path);
            return false;
        }
        m_file.close();
        m_acquired = true;
        return true;
#endif
    }

    bool release()
    {
#ifdef Q_OS_UNIX
        if (m_descriptor < 0)
            return true;
        const QByteArray encoded = QFile::encodeName(m_path);
        struct stat current {};
        const bool sameFile = ::lstat(encoded.constData(), &current) == 0
            && current.st_dev == m_state.st_dev && current.st_ino == m_state.st_ino;
        const bool removed = sameFile && ::unlink(encoded.constData()) == 0
            && syncParentDirectory();
        const bool closed = ::close(m_descriptor) == 0;
        m_descriptor = -1;
#else
        const bool removed = !m_acquired || QFile::remove(m_path);
        const bool closed = true;
#endif
        m_acquired = false;
#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
        if (qEnvironmentVariableIsSet("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE"))
            return false;
#endif
        return removed && closed;
    }

private:
#ifdef Q_OS_UNIX
    bool syncParentDirectory() const
    {
        const QByteArray parent = QFile::encodeName(QFileInfo(m_path).absolutePath());
        const int descriptor = ::open(parent.constData(),
            O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY);
        if (descriptor < 0)
            return false;
        const bool ok = ::fsync(descriptor) == 0;
        return ::close(descriptor) == 0 && ok;
    }

    int m_descriptor = -1;
    struct stat m_state {};
#else
    QFile m_file;
#endif
    QString m_path;
    bool m_acquired = false;
};

WizMarkdownExportRecovery::PreflightPlan classifyLiveState(
    const QString& destination, const QJsonObject& manifest,
    QString& evidenceState, QString& reason, bool preferOld = false)
{
    using Plan = WizMarkdownExportRecovery::PreflightPlan;
    const QFileInfo destinationInfo(destination);
    const QString parent = destinationInfo.absolutePath();
    const QString recovery = WizMarkdownExportRecovery::recoveryDirectory(destination);
    const QString resources = QDir(parent).filePath(
        manifest.value(QStringLiteral("resourcesName")).toString());
    const QString backup = QDir(recovery).filePath(QStringLiteral("previous-resources"));
    const QString returned = QDir(recovery).filePath(QStringLiteral("returned-resources"));
    const QString stagedResources = QDir(recovery).filePath(
        QStringLiteral("content/")
        + manifest.value(QStringLiteral("resourcesName")).toString());
    const QString stagedMarkdown = QDir(recovery).filePath(QStringLiteral("new-markdown"));

    const bool oldMarkdownExisted = manifest.value(
        QStringLiteral("oldMarkdownExisted")).toBool();
    const bool oldResourcesExisted = manifest.value(
        QStringLiteral("oldResourcesExisted")).toBool();
    const bool newResourcesExisted = manifest.value(
        QStringLiteral("newResourcesExisted")).toBool();
    const QString oldMarkdownDigest = manifest.value(
        QStringLiteral("oldMarkdownSha256")).toString();
    const QString newMarkdownDigest = manifest.value(
        QStringLiteral("newMarkdownSha256")).toString();
    const QString oldResourcesDigest = manifest.value(
        QStringLiteral("oldResourcesSha256")).toString();
    const QString newResourcesDigest = manifest.value(
        QStringLiteral("newResourcesSha256")).toString();

    if (!fileMatches(stagedMarkdown, true, newMarkdownDigest)) {
        reason = QStringLiteral("暂存新正文与清单不匹配");
        return Plan::Refused;
    }
    const bool markdownOld = fileMatches(
        destination, oldMarkdownExisted, oldMarkdownDigest);
    const bool markdownNew = fileMatches(destination, true, newMarkdownDigest);
    if (!markdownOld && !markdownNew) {
        reason = QStringLiteral("实时正文不匹配清单中的旧状态或新状态");
        return Plan::Refused;
    }

    const bool liveOld = directoryMatches(
        resources, oldResourcesExisted, oldResourcesDigest);
    const bool liveNew = directoryMatches(
        resources, newResourcesExisted, newResourcesDigest);
    const bool liveMissing = missingPath(resources);
    const bool backupOld = directoryMatches(
        backup, oldResourcesExisted, oldResourcesDigest);
    const bool backupMissing = missingPath(backup);
    const bool returnedNew = directoryMatches(
        returned, newResourcesExisted, newResourcesDigest);
    const bool returnedMissing = missingPath(returned);
    const bool stagedNew = directoryMatches(
        stagedResources, newResourcesExisted, newResourcesDigest);
    const bool stagedMissing = missingPath(stagedResources);

    if (markdownNew && !preferOld) {
        if (liveNew && backupOld && returnedMissing && stagedMissing) {
            evidenceState = QStringLiteral("markdown-committed");
            return Plan::Finalize;
        }
        if (!markdownOld) {
            reason = QStringLiteral("新正文存在，但资源或中间目录组合未知");
            return Plan::Refused;
        }
        // Identical old/new Markdown alone cannot prove a commit. A complete
        // new pair above may finalize; otherwise recognize the strict old-pair
        // states below. Only a current publication proof may prefer rollback.
    }

    if (!markdownOld) {
        reason = QStringLiteral("正文不再匹配本次回退的旧状态");
        return Plan::Refused;
    }

    if (liveOld && backupMissing && returnedMissing && stagedNew) {
        evidenceState = QStringLiteral("prepared");
        return Plan::RollBack;
    }
    if (newResourcesExisted && oldResourcesExisted && liveMissing
        && backupOld && returnedMissing && stagedNew) {
        evidenceState = QStringLiteral("old-resources-reserved");
        return Plan::RollBack;
    }
    if (newResourcesExisted && liveNew && backupOld
        && returnedMissing && stagedMissing) {
        evidenceState = QStringLiteral("new-resources-installed");
        return Plan::RollBack;
    }
    if (newResourcesExisted && liveMissing && backupOld
        && returnedNew && stagedMissing) {
        evidenceState = QStringLiteral("rollback-new-resources-returned");
        return Plan::RollBack;
    }
    if (newResourcesExisted && liveOld && backupMissing
        && returnedNew && stagedMissing) {
        evidenceState = QStringLiteral("rollback-old-resources-restored");
        return Plan::RollBack;
    }

    reason = QStringLiteral("旧正文存在，但资源或中间目录组合未知");
    return Plan::Refused;
}

WizMarkdownExportRecovery::Inspection result(
    WizMarkdownExportRecovery::Availability availability,
    const QString& destination, const QString& summary, const QString& detail)
{
    return {availability,
        WizMarkdownExportRecovery::destinationIdentity(destination), summary, detail};
}

}

namespace WizMarkdownExportRecovery {

struct PreparedExport {
    PreparedExport(QString target, PathIdentity identity, QJsonObject object)
        : destination(std::move(target)), recoveryIdentity(identity), manifest(std::move(object)) {}
    const QString destination;
    const PathIdentity recoveryIdentity;
    const QJsonObject manifest;
    mutable std::atomic_bool consumed {false};
};

QString destinationIdentity(const QString& destination)
{
    const QFileInfo info(destination);
    if (info.fileName().isEmpty())
        return QString();
    QString parent = QDir(info.absolutePath()).canonicalPath();
    if (parent.isEmpty())
        parent = QDir(info.absolutePath()).absolutePath();
    const QByteArray identity = QFile::encodeName(QDir(parent).filePath(info.fileName()));
    return QString::fromLatin1(QCryptographicHash::hash(
        identity, QCryptographicHash::Sha256).toHex().left(24));
}

QString recoveryDirectory(const QString& destination)
{
    const QFileInfo info(destination);
    const QString identity = destinationIdentity(destination);
    if (identity.isEmpty())
        return QString();
    return QDir(info.absolutePath()).filePath(
        QStringLiteral(".wiz-export-recovery-") + identity);
}

QString destinationLockPath(const QString& destination)
{
    const QFileInfo info(destination);
    const QString identity = destinationIdentity(destination);
    if (identity.isEmpty())
        return QString();
    return QDir(info.absolutePath()).filePath(
        QStringLiteral(".wiz-export-lock-") + identity + QStringLiteral(".lock"));
}

Inspection inspect(const QString& destination)
{
    QString reason;
    if (!baseShapesSupported(destination, reason)) {
        return result(Availability::Refused, destination, refusedSummary,
            reason);
    }

    const QString recovery = recoveryDirectory(destination);
    const QFileInfo recoveryInfo(recovery);
    if (!recoveryInfo.exists() && !recoveryInfo.isSymLink()) {
        return result(Availability::None, destination, noRecoverySummary,
            QStringLiteral("未找到恢复目录：") + recovery);
    }
    if (recoveryInfo.isSymLink() || !recoveryInfo.isDir()) {
        return result(Availability::Refused, destination, refusedSummary,
            QStringLiteral("恢复路径不是私有普通目录：") + recovery);
    }

    const LockInspection lock = inspectDestinationLock(destination);
    if (lock.state == LockState::Refused) {
        return result(Availability::Refused, destination, refusedSummary,
            lock.diagnosticDetail);
    }
    if (lock.state != LockState::Available) {
        return result(Availability::Busy, destination, busySummary,
            lock.diagnosticDetail);
    }

    QJsonObject manifest;
    if (!readStrictManifest(destination, manifest, reason)) {
        return result(Availability::Refused, destination, refusedSummary,
            reason);
    }

    return result(Availability::AwaitingDecision, destination, awaitingSummary,
        QStringLiteral("已识别只读恢复现场：") + recovery);
}

LockInspection inspectDestinationLock(const QString& destination)
{
    const QString path = destinationLockPath(destination);
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return {LockState::Available, QStringLiteral("目标锁可用：") + path};
    if (info.isSymLink() || !info.isFile())
        return {LockState::Refused, QStringLiteral("目标锁路径形状不受支持：") + path};

    QLockFile lock(path);
    lock.setStaleLockTime(0);
    qint64 processId = 0;
    QString hostName;
    QString applicationName;
    if (!lock.getLockInfo(&processId, &hostName, &applicationName)
        || processId <= 0 || hostName != QSysInfo::machineHostName()
        || applicationName != QCoreApplication::applicationName()) {
        return {LockState::Refused, QStringLiteral("目标锁所有者无法安全确认：") + path};
    }
#ifdef Q_OS_UNIX
    errno = 0;
    if (::kill(static_cast<pid_t>(processId), 0) == 0 || errno == EPERM) {
        return {LockState::Active, QStringLiteral("目标锁由活动进程持有：") + path};
    }
    if (errno == ESRCH) {
        return {LockState::ConfirmedDead,
            QStringLiteral("目标锁属于本机同程序的已结束进程：") + path};
    }
#endif
    return {LockState::Refused, QStringLiteral("目标锁进程状态无法安全确认：") + path};
}

LockTakeover takeOverConfirmedDeadDestinationLock(const QString& destination)
{
    QString reason;
    if (!baseShapesSupported(destination, reason))
        return {LockTakeoverOutcome::Refused, reason};

    const QString path = destinationLockPath(destination);
    const LockInspection initial = inspectDestinationLock(destination);
    if (initial.state == LockState::Available)
        return {LockTakeoverOutcome::None, initial.diagnosticDetail};
    if (initial.state == LockState::Active)
        return {LockTakeoverOutcome::Active, initial.diagnosticDetail};
    if (initial.state != LockState::ConfirmedDead)
        return {LockTakeoverOutcome::Refused, initial.diagnosticDetail};

#ifdef Q_OS_UNIX
    const QByteArray encoded = QFile::encodeName(path);
    const int descriptor = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        return {LockTakeoverOutcome::Failed,
            QStringLiteral("无法打开已确认死亡的目标锁：") + path};
    }
    const auto finishTakeover = [&](LockTakeoverOutcome outcome, const QString& detail,
                                    bool synced = true) {
        bool released = ::close(descriptor) == 0 && synced;
#ifdef WIZ_MARKDOWN_EXPORT_RECOVERY_TESTING
        if (qEnvironmentVariableIsSet("WIZ_RECOVERY_TEST_DESTINATION_RELEASE_FAILURE"))
            released = false;
#endif
        return LockTakeover{released ? outcome : LockTakeoverOutcome::Failed,
            detail + (released ? QString() : QStringLiteral("；目标锁释放或持久化未确认")),
            released ? LockReleaseOutcome::Released : LockReleaseOutcome::Failed};
    };
    struct stat opened {};
    if (::fstat(descriptor, &opened) != 0 || !S_ISREG(opened.st_mode)) {
        return finishTakeover(LockTakeoverOutcome::Refused,
            QStringLiteral("死亡锁文件类型发生变化：") + path);
    }

    // Cooperating explicit removers must not race a new writer after one has
    // deleted the old path. Keep this inode locked until unlink/close finishes.
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        const bool busy = errno == EWOULDBLOCK || errno == EAGAIN;
        return finishTakeover(busy ? LockTakeoverOutcome::Active : LockTakeoverOutcome::Failed,
            QStringLiteral("另一个目标锁接管正在进行，或无法确认独占访问：") + path);
    }
    recoveryTestCheckpoint("destination-takeover-opened");

    const LockInspection verified = inspectDestinationLock(destination);
    struct stat current {};
    const bool sameFile = verified.state == LockState::ConfirmedDead
        && ::lstat(encoded.constData(), &current) == 0
        && S_ISREG(current.st_mode)
        && current.st_dev == opened.st_dev && current.st_ino == opened.st_ino;
    if (!sameFile) {
        if (verified.state == LockState::Active)
            return finishTakeover(LockTakeoverOutcome::Active, verified.diagnosticDetail);
        return finishTakeover(LockTakeoverOutcome::Refused,
            QStringLiteral("死亡锁在接管前发生变化：") + path);
    }
    if (::unlink(encoded.constData()) != 0) {
        return finishTakeover(LockTakeoverOutcome::Failed,
            QStringLiteral("无法删除已确认死亡的目标锁：") + path);
    }
    const bool synced = syncDirectoryPath(QFileInfo(path).absolutePath());
    return finishTakeover(synced ? LockTakeoverOutcome::Removed : LockTakeoverOutcome::Failed,
        synced ? QStringLiteral("已显式删除同机同程序死亡目标锁：") + path
               : QStringLiteral("无法持久化死亡目标锁删除：") + path, synced);
#else
    const LockInspection verified = inspectDestinationLock(destination);
    QLockFile stale(path);
    stale.setStaleLockTime(0);
    if (verified.state != LockState::ConfirmedDead || !stale.removeStaleLockFile()
        || !syncDirectoryPath(QFileInfo(path).absolutePath())) {
        return {verified.state == LockState::Active
                ? LockTakeoverOutcome::Active : LockTakeoverOutcome::Failed,
            QStringLiteral("无法持久化死亡目标锁删除：") + path};
    }
#endif
    return {LockTakeoverOutcome::Removed,
        QStringLiteral("已显式删除同机同程序死亡目标锁：") + path};
}

Preflight preflight(const QString& destination)
{
    const Inspection initial = inspect(destination);
    if (initial.availability == Availability::None) {
        return {PreflightPlan::None, LockState::Available, QString(),
            initial.userVisibleSummary, initial.diagnosticDetail};
    }
    if (initial.availability == Availability::Refused) {
        return {PreflightPlan::Refused, LockState::Refused, QString(),
            initial.userVisibleSummary, initial.diagnosticDetail};
    }
    if (initial.availability == Availability::Busy) {
        const LockInspection lock = inspectDestinationLock(destination);
        return {lock.state == LockState::Refused
                ? PreflightPlan::Refused : PreflightPlan::Busy,
            lock.state, QString(),
            lock.state == LockState::Refused ? refusedSummary : busySummary,
            lock.diagnosticDetail};
    }

    const QString lockPath = destinationLockPath(destination);
    // inspect() just established that no lock path exists. Refuse any race
    // instead of asking QLockFile to clean a pre-existing owner automatically.
    if (QFileInfo::exists(lockPath) || QFileInfo(lockPath).isSymLink()) {
        return {PreflightPlan::Busy, LockState::Active, QString(), busySummary,
            QStringLiteral("取得目标锁前检测到并发写者：") + lockPath};
    }
    ConservativeDestinationLock lock(lockPath);
    if (!lock.acquire()) {
        return {PreflightPlan::Busy, LockState::Active, QString(), busySummary,
            QStringLiteral("无法取得目标锁：") + lockPath};
    }

    const auto finish = [&](PreflightPlan plan, const QString& evidence,
                            const QString& summary, const QString& detail) {
        const bool released = lock.release();
        return Preflight{released ? plan : PreflightPlan::Refused,
            released ? LockState::Available : LockState::Refused,
            released ? evidence : QString(), released ? summary : refusedSummary,
            detail + (released ? QString()
                : QStringLiteral("；目标锁释放或持久化未确认：") + lockPath),
            released ? LockReleaseOutcome::Released : LockReleaseOutcome::Failed};
    };

    QString reason;
    if (!baseShapesSupported(destination, reason)) {
        return finish(PreflightPlan::Refused, QString(), refusedSummary, reason);
    }
    QJsonObject manifest;
    if (!readStrictManifest(destination, manifest, reason)) {
        return finish(PreflightPlan::Refused, QString(), refusedSummary, reason);
    }
    QString evidenceState;
    const PreflightPlan plan = classifyLiveState(
        destination, manifest, evidenceState, reason);
    if (plan == PreflightPlan::Refused) {
        return finish(plan, QString(), refusedSummary,
            reason + QStringLiteral("：") + recoveryDirectory(destination));
    }
    return finish(plan, evidenceState, awaitingSummary,
        QStringLiteral("预检只读识别状态：") + evidenceState);
}

static Result executePlanImpl(const QString& destination, PreflightPlan expectedPlan,
    const PreparedExport* publication, bool preferOld)
{
    const QString rolledBackSummary = QStringLiteral(
        "Markdown 导出已恢复到上次完整状态。");
    const QString finalizedSummary = QStringLiteral(
        "Markdown 导出的已提交状态已完成收尾。");
    const QString refusedSummaryForExecution = QStringLiteral(
        "无法安全执行 Markdown 导出恢复；已保留现场。");
    auto makeResult = [](RecoveryOutcome outcome, const QString& summary,
                          const QString& detail) {
        return Result{outcome, AuditOutcome::NotRequested, summary, detail};
    };

    if (expectedPlan != PreflightPlan::RollBack
        && expectedPlan != PreflightPlan::Finalize) {
        return makeResult(RecoveryOutcome::Refused, refusedSummaryForExecution,
            QStringLiteral("执行器只接受明确的回退或完成计划"));
    }

    QString reason;
    if (!baseShapesSupported(destination, reason)) {
        return makeResult(RecoveryOutcome::Refused,
            refusedSummaryForExecution, reason);
    }
    const QString recovery = recoveryDirectory(destination);
    const QFileInfo initialRecovery(recovery);
    if (!initialRecovery.exists() && !initialRecovery.isSymLink()) {
        return makeResult(RecoveryOutcome::None, noRecoverySummary,
            QStringLiteral("未找到恢复目录：") + recovery);
    }

    const LockInspection existingLock = inspectDestinationLock(destination);
    if (existingLock.state != LockState::Available) {
        return makeResult(existingLock.state == LockState::Refused
                ? RecoveryOutcome::Refused : RecoveryOutcome::Busy,
            existingLock.state == LockState::Refused
                ? refusedSummaryForExecution : busySummary,
            existingLock.diagnosticDetail);
    }
    ConservativeDestinationLock lock(destinationLockPath(destination));
    if (!lock.acquire()) {
        return makeResult(RecoveryOutcome::Busy, busySummary,
            QStringLiteral("无法取得目标锁：") + destinationLockPath(destination));
    }
    auto finish = [&](RecoveryOutcome outcome, const QString& summary,
                      const QString& detail) {
        Result result = makeResult(outcome, summary, detail);
        const bool released = lock.release();
        result.lockRelease = released ? LockReleaseOutcome::Released : LockReleaseOutcome::Failed;
        if (!released) {
            result.userVisibleSummary += QStringLiteral("目标锁释放或持久化尚未确认。");
            result.diagnosticDetail += QStringLiteral("；目标锁释放或持久化未确认：")
                + destinationLockPath(destination);
        }
        return result;
    };

    if (!baseShapesSupported(destination, reason)) {
        return finish(RecoveryOutcome::Refused,
            refusedSummaryForExecution, reason);
    }
    const PathIdentity ownedRecovery = pathIdentity(recovery);
    if (!ownedRecovery.valid || (publication
            && !samePathIdentity(recovery, publication->recoveryIdentity))) {
        return finish(RecoveryOutcome::Refused, refusedSummaryForExecution,
            QStringLiteral("无法确认恢复目录身份：") + recovery);
    }

    const QString liveResources = QDir(QFileInfo(destination).absolutePath()).filePath(
        resourceNameForDestination(destination));
    const QString backup = QDir(recovery).filePath(QStringLiteral("previous-resources"));
    const QString returned = QDir(recovery).filePath(QStringLiteral("returned-resources"));

    for (int step = 0; step < 6; ++step) {
        if (!samePathIdentity(recovery, ownedRecovery)) {
            return finish(RecoveryOutcome::Refused, refusedSummaryForExecution,
                QStringLiteral("恢复目录身份在执行期间发生变化：") + recovery);
        }
        QJsonObject manifest;
        if (!readStrictManifest(destination, manifest, reason)) {
            return finish(RecoveryOutcome::Refused,
                refusedSummaryForExecution, reason);
        }
        if (publication && (manifest != publication->manifest
                || !samePathIdentity(recovery, publication->recoveryIdentity))) {
            return finish(RecoveryOutcome::Refused, refusedSummaryForExecution,
                QStringLiteral("现场不再属于本次导出凭证，未执行自动收尾"));
        }
        QString evidenceState;
        const PreflightPlan actualPlan = classifyLiveState(
            destination, manifest, evidenceState, reason, preferOld);
        if (actualPlan != expectedPlan) {
            return finish(RecoveryOutcome::Refused, refusedSummaryForExecution,
                actualPlan == PreflightPlan::Refused
                    ? reason + QStringLiteral("：") + recovery
                    : QStringLiteral("实时证据计划与调用方期望不一致：")
                        + evidenceState);
        }

        const bool oldResourcesExisted = manifest.value(
            QStringLiteral("oldResourcesExisted")).toBool();
        const bool rollbackTerminal = expectedPlan == PreflightPlan::RollBack
            && (evidenceState == QStringLiteral("prepared")
                || evidenceState == QStringLiteral("rollback-old-resources-restored")
                || (evidenceState == QStringLiteral("rollback-new-resources-returned")
                    && !oldResourcesExisted));
        const bool finalizeTerminal = expectedPlan == PreflightPlan::Finalize
            && evidenceState == QStringLiteral("markdown-committed");
        if (rollbackTerminal || finalizeTerminal) {
            if (!recoveryStepAllowed("terminal-live-sync")
                || !syncExpectedLivePair(destination, manifest,
                    finalizeTerminal, reason)) {
                return finish(RecoveryOutcome::Refused,
                    refusedSummaryForExecution,
                    (reason.isEmpty() ? QStringLiteral("实时导出状态同步未确认") : reason)
                        + QStringLiteral("：") + recovery);
            }

            QJsonObject verifiedManifest;
            QString verifiedState;
            QString verifiedReason;
            if (!samePathIdentity(recovery, ownedRecovery)
                || !readStrictManifest(destination, verifiedManifest, verifiedReason)
                || (publication && verifiedManifest != publication->manifest)
                || classifyLiveState(destination, verifiedManifest,
                       verifiedState, verifiedReason, preferOld) != expectedPlan
                || verifiedState != evidenceState) {
                return finish(RecoveryOutcome::Refused, refusedSummaryForExecution,
                    verifiedReason.isEmpty()
                        ? QStringLiteral("清理前恢复证据发生变化：") + recovery
                        : verifiedReason + QStringLiteral("：") + recovery);
            }
            const auto cleanupFailed = [&](const QString& detail) {
                const QFileInfo remaining(recovery);
                const bool exists = remaining.exists() || remaining.isSymLink();
                return finish(RecoveryOutcome::Refused,
                    QStringLiteral("正文及资源此前已核对同步，但恢复目录清理尚未确认。"),
                    detail + (exists
                        ? QStringLiteral("；恢复目录当前仍存在，可能仅剩部分内容")
                        : QStringLiteral("；恢复目录当前不存在，不表示此前清理同步已确认")));
            };
            recoveryTestCheckpoint("before-recovery-cleanup");
            if (!recoveryStepAllowed("terminal-before-cleanup")
                || !samePathIdentity(recovery, ownedRecovery)
                || !removeTreeNoFollow(recovery, "cleanup-entry-removed")) {
                return cleanupFailed(QStringLiteral("恢复后无法持久化清理恢复目录：") + recovery);
            }
            recoveryTestCheckpoint("after-recovery-cleanup-before-parent-sync");
            if (!recoveryStepAllowed("cleanup-parent-sync")
                || !syncDirectoryPath(QFileInfo(recovery).absolutePath())) {
                return cleanupFailed(QStringLiteral("恢复后无法持久化恢复目录清理：") + recovery);
            }
            return finish(finalizeTerminal
                    ? RecoveryOutcome::Finalized : RecoveryOutcome::RolledBack,
                finalizeTerminal ? finalizedSummary : rolledBackSummary,
                QStringLiteral("已完成并清理已识别恢复现场：") + recovery);
        }

        if (evidenceState == QStringLiteral("new-resources-installed")) {
            if (!durableRenameDirectory(liveResources, returned,
                    "after-new-resources-returned-before-sync",
                    "return-new-source-sync", "return-new-destination-sync")) {
                return finish(RecoveryOutcome::Refused, refusedSummaryForExecution,
                    QStringLiteral("无法持久化返回新资源目录：") + returned);
            }
            continue;
        }
        if (evidenceState == QStringLiteral("old-resources-reserved")
            || evidenceState == QStringLiteral("rollback-new-resources-returned")) {
            if (!oldResourcesExisted || !durableRenameDirectory(backup, liveResources,
                    "after-old-resources-restored-before-sync",
                    "restore-old-source-sync", "restore-old-destination-sync")) {
                return finish(RecoveryOutcome::Refused, refusedSummaryForExecution,
                    QStringLiteral("无法持久化恢复旧资源目录：") + liveResources);
            }
            continue;
        }

        return finish(RecoveryOutcome::Refused, refusedSummaryForExecution,
            QStringLiteral("识别状态没有受支持的执行步骤：") + evidenceState);
    }

    return finish(RecoveryOutcome::Refused, refusedSummaryForExecution,
        QStringLiteral("恢复步骤超过上限：") + recovery);
}

Result executePlan(const QString& destination, PreflightPlan expectedPlan)
{
    return executePlanImpl(destination, expectedPlan, nullptr, false);
}

Publication publishPrepared(const Preparation& preparation)
{
    const auto proof = preparation.publication;
    if (preparation.outcome != PreparationOutcome::Prepared
        || preparation.lockRelease != LockReleaseOutcome::Released || !proof
        || proof->consumed.exchange(true, std::memory_order_acq_rel)) {
        return {PublicationOutcome::Refused, false, LockReleaseOutcome::NotRequested,
            QStringLiteral("缺少已释放且尚未使用的本次导出凭证")};
    }
    const QString destination = proof->destination;
    const QString recovery = recoveryDirectory(destination);
    recoveryTestCheckpoint("publish-before-acquire");
    const auto existing = inspectDestinationLock(destination);
    if (existing.state != LockState::Available) {
        return {existing.state == LockState::Refused ? PublicationOutcome::Refused : PublicationOutcome::Busy,
            false, LockReleaseOutcome::NotRequested, existing.diagnosticDetail};
    }
    ConservativeDestinationLock lock(destinationLockPath(destination));
    if (!lock.acquire()) {
        return {PublicationOutcome::Busy, false, LockReleaseOutcome::NotRequested,
            QStringLiteral("无法取得本次导出发布锁")};
    }
    const auto finish = [&](PublicationOutcome outcome, bool committed, QString detail) {
        const bool released = lock.release();
        if (!released)
            detail += QStringLiteral("；目标锁释放未确认，不取得后续锁，也不声称文件已回滚");
        return Publication{released ? outcome : PublicationOutcome::Retained, committed,
            released ? LockReleaseOutcome::Released : LockReleaseOutcome::Failed, detail};
    };
    QString reason, state;
    QJsonObject manifest;
    if (!baseShapesSupported(destination, reason)
        || !samePathIdentity(recovery, proof->recoveryIdentity)
        || !readStrictManifest(destination, manifest, reason) || manifest != proof->manifest
        || classifyLiveState(destination, manifest, state, reason, true) != PreflightPlan::RollBack
        || state != QStringLiteral("prepared")) {
        return finish(PublicationOutcome::Refused, false,
            QStringLiteral("本次准备证据已变化；未安装或覆盖导出：") + reason);
    }
    const bool oldResources = manifest.value(QStringLiteral("oldResourcesExisted")).toBool();
    const bool newResources = manifest.value(QStringLiteral("newResourcesExisted")).toBool();
    if (oldResources && !newResources) {
        return finish(PublicationOutcome::Refused, false,
            QStringLiteral("v1 发布要求保留既有资源；此准备回调未生成合并资源树"));
    }
    QFile staged(QDir(recovery).filePath(QStringLiteral("new-markdown")));
    if (!staged.open(QIODevice::ReadOnly))
        return finish(PublicationOutcome::Refused, false, QStringLiteral("无法读取本次暂存正文"));
    const QByteArray bytes = staged.readAll();
    if (staged.error() != QFileDevice::NoError
        || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()
            != manifest.value(QStringLiteral("newMarkdownSha256")).toString().toLatin1()) {
        return finish(PublicationOutcome::Refused, false, QStringLiteral("本次暂存正文读取不一致"));
    }
    staged.close();
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    const auto failBeforeCommit = [&](const QString& detail) {
        output.cancelWriting();
        if (!lock.release()) {
            return Publication{PublicationOutcome::Retained, false, LockReleaseOutcome::Failed,
                detail + QStringLiteral("；发布锁释放未确认，未自动回退，须重新检查现场")};
        }
        // Only this one-attempt proof can authorize ordinary-failure rollback,
        // including identical old/new Markdown. Reacquire and revalidate; never
        // recover another operation's site, retry, take over a lock or audit.
        const auto undone = executePlanImpl(destination, PreflightPlan::RollBack, proof.get(), true);
        const bool restored = undone.recovery == RecoveryOutcome::RolledBack
            && undone.lockRelease == LockReleaseOutcome::Released;
        return Publication{restored ? PublicationOutcome::RolledBack : PublicationOutcome::Retained,
            false, undone.lockRelease, detail + (restored
                ? QStringLiteral("；本次导出已回退到旧正文／资源")
                : QStringLiteral("；本次回退或清理未确认，未猜测修改现场：") + undone.diagnosticDetail)};
    };
    if (!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size())
        return failBeforeCommit(QStringLiteral("无法预留或写入本次最终正文"));
    if (!recoveryStepAllowed("publish-before-reserve"))
        return failBeforeCommit(QStringLiteral("本次资源安装前检查未完成"));
    const QString live = QDir(QFileInfo(destination).absolutePath()).filePath(
        manifest.value(QStringLiteral("resourcesName")).toString());
    const QString backup = QDir(recovery).filePath(QStringLiteral("previous-resources"));
    const QString newTree = QDir(QDir(recovery).filePath(QStringLiteral("content"))).filePath(
        manifest.value(QStringLiteral("resourcesName")).toString());
    if (newResources && oldResources && !durableRenameDirectory(live, backup,
            "publish-old-renamed-before-sync", "publish-old-source-sync", "publish-old-destination-sync"))
        return failBeforeCommit(QStringLiteral("旧资源预留或同步未完成"));
    if (newResources && !durableRenameDirectory(newTree, live,
            "publish-new-installed-before-sync", "publish-new-source-sync", "publish-new-destination-sync"))
        return failBeforeCommit(QStringLiteral("新资源安装或同步未完成"));

    QJsonObject verified;
    if (!samePathIdentity(recovery, proof->recoveryIdentity)
        || !readStrictManifest(destination, verified, reason) || verified != manifest
        || classifyLiveState(destination, verified, state, reason, true) != PreflightPlan::RollBack
        || state != (newResources ? QStringLiteral("new-resources-installed") : QStringLiteral("prepared"))
        || !recoveryStepAllowed("publish-before-markdown-commit"))
        return failBeforeCommit(QStringLiteral("最终正文提交前现场检查未通过：") + reason);
    if (!output.commit()) {
        if (fileMatches(destination, manifest.value(QStringLiteral("oldMarkdownExisted")).toBool(),
                manifest.value(QStringLiteral("oldMarkdownSha256")).toString()))
            return failBeforeCommit(QStringLiteral("最终正文提交失败"));
        return finish(PublicationOutcome::Retained, false,
            QStringLiteral("正文提交结果不确定，未自动回滚或清理，须重新检查现场"));
    }
    if (!recoveryStepAllowed("publish-markdown-committed")
        || !recoveryStepAllowed("publish-markdown-file-sync") || !syncRegularFile(destination)
        || !recoveryStepAllowed("publish-markdown-parent-sync")
        || !syncDirectoryPath(QFileInfo(destination).absolutePath())) {
        return finish(PublicationOutcome::Retained, true,
            QStringLiteral("正文已提交，但持久化未完整确认；未撤销新导出，须重新检查现场"));
    }
    recoveryTestCheckpoint("publish-markdown-synced");
    if (!lock.release()) {
        return {PublicationOutcome::Retained, true, LockReleaseOutcome::Failed,
            QStringLiteral("正文已提交，但目标锁释放未确认；未取得后续清理锁")};
    }
    const auto finalized = executePlanImpl(destination, PreflightPlan::Finalize, proof.get(), false);
    const bool complete = finalized.recovery == RecoveryOutcome::Finalized
        && finalized.lockRelease == LockReleaseOutcome::Released;
    return {complete ? PublicationOutcome::Published : PublicationOutcome::Retained,
        true, finalized.lockRelease, complete ? QString()
            : QStringLiteral("正文已提交，但本次清理尚未确认；未撤销新导出：") + finalized.diagnosticDetail};
}

bool withFreshDestination(const QString& destination,
    const std::function<bool()>& writer, QString& error)
{
    error.clear();
    QString reason;
    if (!writer || !baseShapesSupported(destination, reason)) {
        error = writer ? reason : QStringLiteral("缺少普通导出写入回调");
        return false;
    }
    const auto existing = inspectDestinationLock(destination);
    if (existing.state != LockState::Available) {
        error = existing.diagnosticDetail;
        return false;
    }
    ConservativeDestinationLock lock(destinationLockPath(destination));
    if (!lock.acquire()) {
        error = QStringLiteral("无法取得普通导出的目标锁");
        return false;
    }
    bool written = false;
    const QFileInfo recovery(recoveryDirectory(destination));
    if (!baseShapesSupported(destination, reason))
        error = reason;
    else if (recovery.exists() || recovery.isSymLink())
        error = QStringLiteral("导出前发现恢复现场；未覆盖目标，请重新检查并明确处理");
    else
        written = writer();
    const bool released = lock.release();
    if (!released)
        error += QStringLiteral("；目标锁释放或持久化未确认，写入结果不能视作已回退");
    return written && released;
}

Preparation prepare(const QString& destination, const QByteArray& markdownUtf8,
                    const std::function<bool(const QString&)>& exportHtml)
{
    const QString preparedSummary = QStringLiteral(
        "Markdown 导出已在私有恢复目录中准备完成，尚未替换现有文件。");
    const QString failedSummary = QStringLiteral(
        "无法准备 Markdown 导出；现有文件未被替换。");
    QString reason;
    if (!exportHtml || !baseShapesSupported(destination, reason)) {
        return {PreparationOutcome::Refused, recoveryDirectory(destination),
            failedSummary, exportHtml ? reason : QStringLiteral("缺少暂存导出回调")};
    }

    const QString recovery = recoveryDirectory(destination);
    const LockInspection existingLock = inspectDestinationLock(destination);
    if (existingLock.state != LockState::Available) {
        return {existingLock.state == LockState::Refused
                ? PreparationOutcome::Refused : PreparationOutcome::Busy,
            recovery,
            existingLock.state == LockState::Refused ? failedSummary : busySummary,
            existingLock.diagnosticDetail};
    }
    const QFileInfo recoveryInfo(recovery);
    if (recoveryInfo.exists() || recoveryInfo.isSymLink()) {
        return {PreparationOutcome::Refused, recovery, failedSummary,
            QStringLiteral("既有恢复现场必须保留：") + recovery};
    }

    ConservativeDestinationLock lock(destinationLockPath(destination));
    if (!lock.acquire()) {
        return {PreparationOutcome::Busy, recovery, busySummary,
            QStringLiteral("无法取得目标锁：") + destinationLockPath(destination)};
    }

    std::shared_ptr<const PreparedExport> publication;
    const auto finish = [&](PreparationOutcome outcome, const QString& summary,
                            const QString& detail) {
        const bool released = lock.release();
        return Preparation{!released && outcome == PreparationOutcome::Prepared
                ? PreparationOutcome::FailedRetained : outcome,
            recovery, released ? summary
                : QStringLiteral("目标锁释放或持久化尚未确认，请勿开始后续导出。"),
            detail + (released ? QString()
                : QStringLiteral("；目标锁释放或持久化未确认：") + destinationLockPath(destination)),
            released ? LockReleaseOutcome::Released : LockReleaseOutcome::Failed,
            released && outcome == PreparationOutcome::Prepared ? publication : nullptr};
    };

    const auto cleanupOutcome = [&](bool cleaned, const QString& detail, bool created = true) {
        const QFileInfo current(recovery);
        const bool exists = current.exists() || current.isSymLink();
        return finish(cleaned ? PreparationOutcome::FailedCleaned : PreparationOutcome::FailedRetained,
            cleaned ? failedSummary
                    : QStringLiteral("本次准备未替换现有文件，但清理尚未确认。"),
            detail + (cleaned ? (created ? QStringLiteral("；自有准备现场已清理并同步")
                                       : QStringLiteral("；本次未创建恢复目录，也未清理任何现有目录"))
                : exists ? QStringLiteral("；清理未确认；恢复目录当前仍存在，可能不完整")
                         : QStringLiteral("；清理未确认；恢复目录当前不存在，不表示此前清理同步已确认")));
    };

    if (!baseShapesSupported(destination, reason)
        || QFileInfo::exists(recovery) || QFileInfo(recovery).isSymLink()) {
        return finish(PreparationOutcome::Refused, failedSummary,
            reason.isEmpty() ? QStringLiteral("取得锁后发现既有恢复现场：") + recovery
                             : reason);
    }

    const QFileInfo destinationInfo(destination);
    const QString resourcesName = resourceNameForDestination(destination);
    const QString liveResources = QDir(destinationInfo.absolutePath()).filePath(resourcesName);
    const bool oldMarkdownExisted = destinationInfo.exists();
    const QByteArray oldMarkdownDigest = oldMarkdownExisted
        ? fileDigest(destination) : QByteArray();
    const bool oldResourcesExisted = QFileInfo(liveResources).exists();
    const QByteArray oldResourcesDigest = oldResourcesExisted
        ? directoryDigest(liveResources) : QByteArray();
    if ((oldMarkdownExisted
            && (oldMarkdownDigest.isEmpty() || !syncRegularFile(destination)))
        || (oldResourcesExisted
            && (oldResourcesDigest.isEmpty() || !syncTree(liveResources)))) {
        return finish(PreparationOutcome::Refused, failedSummary,
            QStringLiteral("无法读取或持久化现有导出状态"));
    }

    bool recoveryCreated = false;
    if (!createPrivateDirectory(recovery, &recoveryCreated, "prepare-recovery-created")) {
        const PathIdentity failedIdentity = recoveryCreated
            ? pathIdentity(recovery) : PathIdentity{};
        const bool cleaned = !recoveryCreated
            || (samePathIdentity(recovery, failedIdentity)
                && recoveryStepAllowed("prepare-before-failure-cleanup")
                && removeTreeNoFollow(recovery, "prepare-cleanup-entry-removed")
                && recoveryStepAllowed("prepare-cleanup-parent-sync")
                && syncDirectoryPath(QFileInfo(recovery).absolutePath()));
        return cleanupOutcome(cleaned, QStringLiteral("无法创建并持久化私有恢复目录：") + recovery,
            recoveryCreated);
    }
    const PathIdentity ownedRecovery = pathIdentity(recovery);
    auto failPreparedSite = [&](const QString& detail) {
        const bool cleaned = samePathIdentity(recovery, ownedRecovery)
            && recoveryStepAllowed("prepare-before-failure-cleanup")
            && removeTreeNoFollow(recovery, "prepare-cleanup-entry-removed")
            && recoveryStepAllowed("prepare-cleanup-parent-sync")
            && syncDirectoryPath(QFileInfo(recovery).absolutePath());
        return cleanupOutcome(cleaned, detail);
    };
    if (!ownedRecovery.valid) {
        return finish(PreparationOutcome::FailedRetained, failedSummary,
            QStringLiteral("无法确认新恢复目录所有权：") + recovery);
    }

    const QString content = QDir(recovery).filePath(QStringLiteral("content"));
    bool contentCreated = false;
    if (!createPrivateDirectory(content, &contentCreated, "prepare-content-created") || !contentCreated)
        return failPreparedSite(QStringLiteral("无法创建私有内容目录：") + content);

    const QString stagedMarkdown = QDir(recovery).filePath(QStringLiteral("new-markdown"));
    if (!writePrivateAtomicFile(stagedMarkdown, markdownUtf8,
            {"prepare-markdown-committed", "prepare-markdown-file-sync", "prepare-markdown-directory-sync"}))
        return failPreparedSite(QStringLiteral("无法持久化暂存 Markdown：") + stagedMarkdown);

    const QString stagedHtml = QDir(content).filePath(QFileInfo(destination).fileName());
    if (!exportHtml(stagedHtml))
        return failPreparedSite(QStringLiteral("暂存 HTML／资源导出失败"));
    if (!samePathIdentity(recovery, ownedRecovery)) {
        return finish(PreparationOutcome::FailedRetained, failedSummary,
            QStringLiteral("暂存期间恢复目录身份发生变化：") + recovery);
    }
    const QFileInfo stagedHtmlInfo(stagedHtml);
    if (stagedHtmlInfo.isSymLink() || !stagedHtmlInfo.isFile())
        return failPreparedSite(QStringLiteral("暂存 HTML 路径形状无效：") + stagedHtml);

    const QString stagedResources = QDir(content).filePath(resourcesName);
    const QFileInfo stagedResourcesInfo(stagedResources);
    if (stagedResourcesInfo.isSymLink()
        || (stagedResourcesInfo.exists() && !stagedResourcesInfo.isDir())) {
        return failPreparedSite(QStringLiteral("暂存资源路径形状无效：") + stagedResources);
    }
    const bool newResourcesExisted = stagedResourcesInfo.exists();
    if (!removeRegularFileAndSync(stagedHtml, "prepare-html-removed"))
        return failPreparedSite(QStringLiteral("无法删除不再需要的暂存 HTML：") + stagedHtml);
    if (!validateContentLayout(content, resourcesName, newResourcesExisted, true, reason))
        return failPreparedSite(reason);
    if (!recoveryStepAllowed("prepare-resource-sync") || !syncTree(content))
        return failPreparedSite(QStringLiteral("无法持久化暂存资源树：") + content);

    if (!fileMatches(destination, oldMarkdownExisted,
            QString::fromLatin1(oldMarkdownDigest))
        || !directoryMatches(liveResources, oldResourcesExisted,
            QString::fromLatin1(oldResourcesDigest))) {
        return failPreparedSite(QStringLiteral("暂存期间现有导出状态发生变化"));
    }

    const QByteArray newMarkdownDigest = fileDigest(stagedMarkdown);
    const QByteArray newResourcesDigest = newResourcesExisted
        ? directoryDigest(stagedResources) : QByteArray();
    if (newMarkdownDigest.isEmpty()
        || (newResourcesExisted && newResourcesDigest.isEmpty())) {
        return failPreparedSite(QStringLiteral("无法计算暂存内容摘要：") + recovery);
    }

    const QJsonObject manifest = recoveryManifest(destination,
        oldMarkdownExisted, oldMarkdownDigest, newMarkdownDigest,
        oldResourcesExisted, oldResourcesDigest,
        newResourcesExisted, newResourcesDigest);
    QString manifestReason;
    if (!validateManifest(manifest, destination, manifestReason))
        return failPreparedSite(manifestReason);
    const QString manifestPath = QDir(recovery).filePath(QStringLiteral("transaction.json"));
    if (!samePathIdentity(recovery, ownedRecovery)
        || !writePrivateAtomicFile(manifestPath,
            QJsonDocument(manifest).toJson(QJsonDocument::Compact),
            {"prepare-manifest-committed", "prepare-manifest-file-sync", "prepare-manifest-directory-sync"})
        || !recoveryStepAllowed("prepare-root-sync")
        || !syncDirectoryPath(recovery)
        || !recoveryStepAllowed("prepare-parent-sync")
        || !syncDirectoryPath(QFileInfo(recovery).absolutePath())) {
        return failPreparedSite(QStringLiteral("无法原子提交恢复清单：") + manifestPath);
    }

    publication = std::make_shared<const PreparedExport>(destination, ownedRecovery, manifest);
    return finish(PreparationOutcome::Prepared, preparedSummary,
        QStringLiteral("已准备持久化恢复现场：") + recovery);
}

}

