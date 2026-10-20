#include <QCoreApplication>
#include <QCryptographicHash>
#include <QByteArrayView>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <functional>
#include <signal.h>
#include <sys/stat.h>
#ifdef Q_OS_MACOS
#include <sys/xattr.h>
#endif
#include <unistd.h>

namespace {

enum class RecoveryResult {
    RolledBack,
    Finalized,
    Refused,
    Busy
};

QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool syncRegularFile(const QString& path, bool* usedFullSync = nullptr)
{
    if (usedFullSync)
        *usedFullSync = false;
    const QByteArray encoded = QFile::encodeName(path);
    const int descriptor = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0)
        return false;
#ifdef F_FULLFSYNC
    const bool ok = ::fcntl(descriptor, F_FULLFSYNC) == 0;
    if (usedFullSync)
        *usedFullSync = ok;
    return ::close(descriptor) == 0 && ok;
#else
    const bool ok = ::fsync(descriptor) == 0;
    return ::close(descriptor) == 0 && ok;
#endif
}

bool syncDirectory(const QString& path)
{
    const QFileInfo info(path);
    if (info.isSymLink() || !info.isDir())
        return false;
    const QByteArray encoded = QFile::encodeName(path);
    const int descriptor = ::open(encoded.constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_DIRECTORY);
    if (descriptor < 0)
        return false;
    const bool ok = ::fsync(descriptor) == 0;
    return ::close(descriptor) == 0 && ok;
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
    return syncDirectory(path);
}

bool durableWriteFile(const QString& path, const QByteArray& bytes)
{
    return writeFile(path, bytes) && syncRegularFile(path)
        && syncDirectory(QFileInfo(path).absolutePath());
}

bool durableRename(const QString& source, const QString& destination)
{
    const QString sourceParent = QFileInfo(source).absolutePath();
    const QString destinationParent = QFileInfo(destination).absolutePath();
    if (!QDir().rename(source, destination) || !syncDirectory(sourceParent))
        return false;
    return sourceParent == destinationParent || syncDirectory(destinationParent);
}

bool renameWithoutDirectorySync(const QString& source, const QString& destination)
{
    const QByteArray encodedSource = QFile::encodeName(source);
    const QByteArray encodedDestination = QFile::encodeName(destination);
    return ::rename(encodedSource.constData(), encodedDestination.constData()) == 0;
}

bool replaceMarkdownWithoutDirectorySync(const QString& destination)
{
    const QString temporary = destination + QStringLiteral(".phase73-next");
    QFile::remove(temporary);
    return writeFile(temporary, "NEW MARKDOWN\n")
        && syncRegularFile(temporary)
        && renameWithoutDirectorySync(temporary, destination);
}

bool durableRemoveTree(const QString& path)
{
    const QString parent = QFileInfo(path).absolutePath();
    return QDir(path).removeRecursively() && syncDirectory(parent);
}

QByteArray fileDigest(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return hash.result().toHex();
}

QByteArray directoryDigest(const QString& path)
{
    const QFileInfo root(path);
    if (root.isSymLink() || !root.isDir())
        return {};

    QStringList relativeFiles;
    QDirIterator iterator(path, QDir::Files | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    const QDir directory(path);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.fileInfo().isSymLink() || !iterator.fileInfo().isFile())
            return {};
        relativeFiles.append(directory.relativeFilePath(iterator.filePath()));
    }
    relativeFiles.sort(Qt::CaseSensitive);

    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& relative : relativeFiles) {
        const QByteArray name = relative.toUtf8();
        const QByteArray bytes = readFile(directory.filePath(relative));
        hash.addData(QByteArray::number(name.size()));
        hash.addData(QByteArrayView(":", 1));
        hash.addData(name);
        hash.addData(QByteArray::number(bytes.size()));
        hash.addData(QByteArrayView(":", 1));
        hash.addData(bytes);
    }
    return hash.result().toHex();
}

void addDigestField(QCryptographicHash& hash, const QByteArray& value)
{
    hash.addData(QByteArray::number(value.size()));
    hash.addData(QByteArrayView(":", 1));
    hash.addData(value);
}

bool addMetadata(const QString& path, QCryptographicHash& hash)
{
    const QByteArray encoded = QFile::encodeName(path);
    struct stat state {};
    if (::lstat(encoded.constData(), &state) != 0 || S_ISLNK(state.st_mode))
        return false;
    addDigestField(hash, QByteArray::number(state.st_mode & 07777));
    addDigestField(hash, QByteArray::number(state.st_uid));
    addDigestField(hash, QByteArray::number(state.st_gid));
#ifdef Q_OS_MACOS
    addDigestField(hash, QByteArray::number(state.st_flags));
    const ssize_t namesSize = ::listxattr(encoded.constData(), nullptr, 0, XATTR_NOFOLLOW);
    if (namesSize < 0)
        return false;
    QByteArray names(int(namesSize), Qt::Uninitialized);
    if (namesSize && ::listxattr(encoded.constData(), names.data(), size_t(names.size()),
            XATTR_NOFOLLOW) != namesSize) {
        return false;
    }
    QList<QByteArray> attributes;
    for (qsizetype offset = 0; offset < names.size();) {
        const qsizetype end = names.indexOf('\0', offset);
        if (end < 0)
            return false;
        attributes.append(names.mid(offset, end - offset));
        offset = end + 1;
    }
    std::sort(attributes.begin(), attributes.end());
    for (const QByteArray& name : attributes) {
        const ssize_t valueSize = ::getxattr(encoded.constData(), name.constData(), nullptr, 0,
            0, XATTR_NOFOLLOW);
        if (valueSize < 0)
            return false;
        QByteArray value(int(valueSize), Qt::Uninitialized);
        if (valueSize && ::getxattr(encoded.constData(), name.constData(), value.data(),
                size_t(value.size()), 0, XATTR_NOFOLLOW) != valueSize) {
            return false;
        }
        addDigestField(hash, name);
        addDigestField(hash, value);
    }
#endif
    return true;
}

QByteArray metadataDirectoryDigest(const QString& path)
{
    const QFileInfo root(path);
    if (root.isSymLink() || !root.isDir())
        return {};
    QStringList entries{QStringLiteral(".")};
    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::System
        | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir directory(path);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.fileInfo().isSymLink()
            || (!iterator.fileInfo().isFile() && !iterator.fileInfo().isDir())) {
            return {};
        }
        entries.append(directory.relativeFilePath(iterator.filePath()));
    }
    entries.sort(Qt::CaseSensitive);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& relative : entries) {
        const QString absolute = relative == QStringLiteral(".")
            ? path : directory.filePath(relative);
        const QFileInfo info(absolute);
        addDigestField(hash, relative.toUtf8());
        addDigestField(hash, info.isDir() ? QByteArray("D") : QByteArray("F"));
        if (!addMetadata(absolute, hash))
            return {};
        if (info.isFile()) {
            QFile file(absolute);
            if (!file.open(QIODevice::ReadOnly))
                return {};
            addDigestField(hash, file.readAll());
        }
    }
    return hash.result().toHex();
}

QByteArray cacheFingerprint(const QString& path)
{
    const QByteArray encoded = QFile::encodeName(path);
    struct stat state {};
    if (::lstat(encoded.constData(), &state) != 0 || !S_ISREG(state.st_mode))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!addMetadata(path, hash))
        return {};
    addDigestField(hash, QByteArray::number(state.st_dev));
    addDigestField(hash, QByteArray::number(state.st_ino));
    addDigestField(hash, QByteArray::number(state.st_size));
#ifdef Q_OS_MACOS
    addDigestField(hash, QByteArray::number(state.st_mtimespec.tv_sec));
    addDigestField(hash, QByteArray::number(state.st_mtimespec.tv_nsec));
    addDigestField(hash, QByteArray::number(state.st_ctimespec.tv_sec));
    addDigestField(hash, QByteArray::number(state.st_ctimespec.tv_nsec));
#else
    addDigestField(hash, QByteArray::number(state.st_mtim.tv_sec));
    addDigestField(hash, QByteArray::number(state.st_mtim.tv_nsec));
    addDigestField(hash, QByteArray::number(state.st_ctim.tv_sec));
    addDigestField(hash, QByteArray::number(state.st_ctim.tv_nsec));
#endif
    return hash.result().toHex();
}

struct CachedFileDigest {
    QByteArray fingerprint;
    QByteArray contentDigest;
    bool reusableInProcess = true;
};

struct IncrementalDigestResult {
    QByteArray digest;
    int hashedFiles = 0;
    int reusedFiles = 0;
    int validationRetries = 0;
};

IncrementalDigestResult incrementalMetadataDirectoryDigest(
    const QString& path, QHash<QString, CachedFileDigest>& cache,
    const std::function<void(const QString&)>& afterFingerprint = {},
    QHash<QString, QByteArray>* observedFingerprints = nullptr)
{
    IncrementalDigestResult result;
    const QFileInfo root(path);
    if (root.isSymLink() || !root.isDir())
        return result;
    QStringList entries{QStringLiteral(".")};
    QDirIterator iterator(path, QDir::AllEntries | QDir::Hidden | QDir::System
        | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    const QDir directory(path);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.fileInfo().isSymLink()
            || (!iterator.fileInfo().isFile() && !iterator.fileInfo().isDir())) {
            return {};
        }
        entries.append(directory.relativeFilePath(iterator.filePath()));
    }
    entries.sort(Qt::CaseSensitive);
    QSet<QString> activeFiles;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for (const QString& relative : entries) {
        const QString absolute = relative == QStringLiteral(".")
            ? path : directory.filePath(relative);
        const QFileInfo info(absolute);
        addDigestField(hash, relative.toUtf8());
        addDigestField(hash, info.isDir() ? QByteArray("D") : QByteArray("F"));
        if (!addMetadata(absolute, hash))
            return {};
        if (!info.isFile())
            continue;
        activeFiles.insert(relative);
        const QByteArray fingerprint = cacheFingerprint(absolute);
        if (fingerprint.isEmpty())
            return {};
        if (observedFingerprints)
            observedFingerprints->insert(relative, fingerprint);
        if (afterFingerprint)
            afterFingerprint(relative);
        const auto cached = cache.constFind(relative);
        QByteArray contentDigest;
        if (cached != cache.cend() && cached->fingerprint == fingerprint
            && cached->reusableInProcess) {
            contentDigest = cached->contentDigest;
            ++result.reusedFiles;
        } else {
            contentDigest = fileDigest(absolute);
            if (contentDigest.isEmpty())
                return {};
            cache.insert(relative, {fingerprint, contentDigest});
            ++result.hashedFiles;
        }
        addDigestField(hash, contentDigest);
    }
    for (auto cached = cache.begin(); cached != cache.end();) {
        if (!activeFiles.contains(cached.key()))
            cached = cache.erase(cached);
        else
            ++cached;
    }
    result.digest = hash.result().toHex();
    return result;
}

bool fingerprintsStillMatch(const QString& path,
                            const QHash<QString, QByteArray>& observedFingerprints)
{
    QHash<QString, QByteArray> current;
    QDirIterator iterator(path, QDir::Files | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories);
    const QDir directory(path);
    while (iterator.hasNext()) {
        iterator.next();
        if (iterator.fileInfo().isSymLink() || !iterator.fileInfo().isFile())
            return false;
        const QString relative = directory.relativeFilePath(iterator.filePath());
        const QByteArray fingerprint = cacheFingerprint(iterator.filePath());
        if (fingerprint.isEmpty() || current.contains(relative))
            return false;
        current.insert(relative, fingerprint);
    }
    return current == observedFingerprints;
}

IncrementalDigestResult validatedIncrementalMetadataDirectoryDigest(
    const QString& path, QHash<QString, CachedFileDigest>& cache,
    const std::function<void(const QString&)>& afterFingerprint = {})
{
    QHash<QString, QByteArray> observed;
    IncrementalDigestResult result = incrementalMetadataDirectoryDigest(
        path, cache, afterFingerprint, &observed);
    if (result.digest.isEmpty() || fingerprintsStillMatch(path, observed))
        return result;
    QHash<QString, QByteArray> retriedObserved;
    result = incrementalMetadataDirectoryDigest(path, cache, {}, &retriedObserved);
    result.validationRetries = 1;
    if (result.digest.isEmpty() || !fingerprintsStillMatch(path, retriedObserved))
        return {};
    return result;
}

QString cacheLockPathFor(const QString& cachePath)
{
    return cachePath + QStringLiteral(".lock");
}

bool exactJsonKeys(const QJsonObject& object, QStringList expected)
{
    QStringList actual = object.keys();
    actual.sort(Qt::CaseSensitive);
    expected.sort(Qt::CaseSensitive);
    return actual == expected;
}

bool safeRelativeCachePath(const QString& path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path)
        || QDir::cleanPath(path) != path) {
        return false;
    }
    const QStringList parts = path.split('/');
    return !parts.contains(QString()) && !parts.contains(QStringLiteral("."))
        && !parts.contains(QStringLiteral(".."));
}

bool sameDigestCache(const QHash<QString, CachedFileDigest>& first,
                     const QHash<QString, CachedFileDigest>& second)
{
    if (first.size() != second.size())
        return false;
    for (auto entry = first.cbegin(); entry != first.cend(); ++entry) {
        const auto other = second.constFind(entry.key());
        if (other == second.cend()
            || other->fingerprint != entry->fingerprint
            || other->contentDigest != entry->contentDigest) {
            return false;
        }
    }
    return true;
}

QByteArray serializePersistentCache(const QByteArray& rootIdentity,
                                    const QHash<QString, CachedFileDigest>& cache)
{
    QStringList paths = cache.keys();
    paths.sort(Qt::CaseSensitive);
    QJsonArray entries;
    for (const QString& path : paths) {
        const CachedFileDigest& entry = cache[path];
        entries.append(QJsonObject{
            {QStringLiteral("path"), path},
            {QStringLiteral("fingerprint"), QString::fromLatin1(entry.fingerprint)},
            {QStringLiteral("contentSha256"), QString::fromLatin1(entry.contentDigest)}
        });
    }
    return QJsonDocument(QJsonObject{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("rootIdentity"), QString::fromLatin1(rootIdentity)},
        {QStringLiteral("entries"), entries}
    }).toJson(QJsonDocument::Compact);
}

bool writePersistentCache(const QString& cachePath, const QByteArray& rootIdentity,
                          const QHash<QString, CachedFileDigest>& cache)
{
    const QFileInfo pathInfo(cachePath);
    if (pathInfo.isSymLink() || QFileInfo(pathInfo.absolutePath()).isSymLink())
        return false;
    QLockFile lock(cacheLockPathFor(cachePath));
    lock.setStaleLockTime(0);
    if (QFileInfo::exists(lock.fileName()) || !lock.tryLock(0))
        return false;
    QSaveFile output(cachePath);
    const QByteArray bytes = serializePersistentCache(rootIdentity, cache);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(bytes) != bytes.size() || !output.commit()) {
        return false;
    }
    return syncRegularFile(cachePath) && syncDirectory(pathInfo.absolutePath());
}

bool loadPersistentCache(const QString& cachePath, const QByteArray& expectedRootIdentity,
                         QHash<QString, CachedFileDigest>& cache)
{
    cache.clear();
    const QFileInfo pathInfo(cachePath);
    if (pathInfo.isSymLink() || !pathInfo.isFile()
        || QFileInfo(pathInfo.absolutePath()).isSymLink()) {
        return false;
    }
    QLockFile lock(cacheLockPathFor(cachePath));
    lock.setStaleLockTime(0);
    if (QFileInfo::exists(lock.fileName()) || !lock.tryLock(0))
        return false;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(readFile(cachePath), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject())
        return false;
    const QJsonObject object = document.object();
    if (!exactJsonKeys(object, {QStringLiteral("schema"),
            QStringLiteral("rootIdentity"), QStringLiteral("entries")})
        || !object.value(QStringLiteral("schema")).isDouble()
        || object.value(QStringLiteral("schema")).toInt(-1) != 1
        || !object.value(QStringLiteral("rootIdentity")).isString()
        || object.value(QStringLiteral("rootIdentity")).toString().toLatin1()
            != expectedRootIdentity
        || !object.value(QStringLiteral("entries")).isArray()) {
        return false;
    }
    const QRegularExpression digestPattern(QStringLiteral("^[0-9a-f]{64}$"));
    QHash<QString, CachedFileDigest> parsed;
    for (const QJsonValue& value : object.value(QStringLiteral("entries")).toArray()) {
        if (!value.isObject())
            return false;
        const QJsonObject entry = value.toObject();
        if (!exactJsonKeys(entry, {QStringLiteral("path"),
                QStringLiteral("fingerprint"), QStringLiteral("contentSha256")})
            || !entry.value(QStringLiteral("path")).isString()
            || !entry.value(QStringLiteral("fingerprint")).isString()
            || !entry.value(QStringLiteral("contentSha256")).isString()) {
            return false;
        }
        const QString path = entry.value(QStringLiteral("path")).toString();
        const QString fingerprint = entry.value(QStringLiteral("fingerprint")).toString();
        const QString contentDigest = entry.value(QStringLiteral("contentSha256")).toString();
        if (!safeRelativeCachePath(path) || parsed.contains(path)
            || !digestPattern.match(fingerprint).hasMatch()
            || !digestPattern.match(contentDigest).hasMatch()) {
            return false;
        }
        parsed.insert(path, {fingerprint.toLatin1(), contentDigest.toLatin1(), false});
    }
    cache = parsed;
    return true;
}

bool syncOpenDescriptor(int descriptor)
{
#ifdef F_FULLFSYNC
    return ::fcntl(descriptor, F_FULLFSYNC) == 0;
#else
    return ::fsync(descriptor) == 0;
#endif
}

bool stagePersistentCacheAndHold(const QString& cachePath, const QString& readyPath,
                                 const QByteArray& replacement)
{
    QLockFile lock(cacheLockPathFor(cachePath));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return false;
    QSaveFile output(cachePath);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(replacement) != replacement.size()
        || !output.flush() || !syncOpenDescriptor(output.handle())
        || !writeFile(readyPath, "READY")) {
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000)
        QThread::msleep(10);
    return false;
}

bool safeBaseName(const QString& name)
{
    return !name.isEmpty() && name != QStringLiteral(".") && name != QStringLiteral("..")
        && QFileInfo(name).fileName() == name && !name.contains('/') && !name.contains('\\');
}

QByteArray identityDigest(const QString& destination)
{
    const QFileInfo info(destination);
    QString parent = QDir(info.absolutePath()).canonicalPath();
    if (parent.isEmpty())
        parent = info.absolutePath();
    const QByteArray identity = QFile::encodeName(QDir(parent).filePath(info.fileName()));
    return QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(24);
}

QString lockPathFor(const QString& destination)
{
    const QFileInfo info(destination);
    return QDir(info.absolutePath()).filePath(QStringLiteral(".wiz-export-lock-")
        + QString::fromLatin1(identityDigest(destination)) + QStringLiteral(".lock"));
}

QString recoveryPathFor(const QString& destination)
{
    const QFileInfo info(destination);
    return QDir(info.absolutePath()).filePath(QStringLiteral(".wiz-export-recovery-")
        + QString::fromLatin1(identityDigest(destination)));
}

bool writeManifest(const QString& stage, const QString& destinationName,
                   const QString& resourcesName, bool oldMarkdownExisted,
                   const QByteArray& oldMarkdownDigest, const QByteArray& newMarkdownDigest,
                   bool oldResourcesExisted, const QByteArray& oldResourcesDigest,
                   bool newResourcesExisted, const QByteArray& newResourcesDigest)
{
    const QJsonObject object{
        {QStringLiteral("schema"), 1},
        {QStringLiteral("destinationName"), destinationName},
        {QStringLiteral("resourcesName"), resourcesName},
        {QStringLiteral("oldMarkdownExisted"), oldMarkdownExisted},
        {QStringLiteral("oldMarkdownSha256"), QString::fromLatin1(oldMarkdownDigest)},
        {QStringLiteral("newMarkdownSha256"), QString::fromLatin1(newMarkdownDigest)},
        {QStringLiteral("oldResourcesExisted"), oldResourcesExisted},
        {QStringLiteral("oldResourcesSha256"), QString::fromLatin1(oldResourcesDigest)},
        {QStringLiteral("newResourcesExisted"), newResourcesExisted},
        {QStringLiteral("newResourcesSha256"), QString::fromLatin1(newResourcesDigest)}
    };
    QSaveFile output(QDir(stage).filePath(QStringLiteral("transaction.json")));
    if (!output.open(QIODevice::WriteOnly))
        return false;
    const QByteArray bytes = QJsonDocument(object).toJson(QJsonDocument::Compact);
    if (output.write(bytes) != bytes.size() || !output.commit())
        return false;
    const QString manifest = QDir(stage).filePath(QStringLiteral("transaction.json"));
    return syncRegularFile(manifest) && syncDirectory(stage)
        && syncDirectory(QFileInfo(stage).absolutePath());
}

bool sameHostDeadOwner(QLockFile& lock)
{
    qint64 processId = 0;
    QString hostName;
    QString applicationName;
    if (!lock.getLockInfo(&processId, &hostName, &applicationName)
        || processId <= 0 || hostName != QSysInfo::machineHostName()
        || applicationName != QCoreApplication::applicationName()) {
        return false;
    }
    errno = 0;
    return ::kill(static_cast<pid_t>(processId), 0) != 0 && errno == ESRCH;
}

RecoveryResult recover(const QString& stage, bool allowConfirmedDeadOwner = false)
{
    const QFileInfo stageInfo(stage);
    if (stageInfo.isSymLink() || !stageInfo.isDir())
        return RecoveryResult::Refused;

    const QJsonDocument document = QJsonDocument::fromJson(
        readFile(QDir(stage).filePath(QStringLiteral("transaction.json"))));
    if (!document.isObject())
        return RecoveryResult::Refused;
    const QJsonObject manifest = document.object();
    if (manifest.value(QStringLiteral("schema")).toInt() != 1)
        return RecoveryResult::Refused;

    const QString destinationName = manifest.value(QStringLiteral("destinationName")).toString();
    const QString resourcesName = manifest.value(QStringLiteral("resourcesName")).toString();
    if (!safeBaseName(destinationName) || !safeBaseName(resourcesName))
        return RecoveryResult::Refused;

    const QString parent = stageInfo.absoluteDir().absolutePath();
    const QString destination = QDir(parent).filePath(destinationName);
    const QString resources = QDir(parent).filePath(resourcesName);
    const QString backup = QDir(stage).filePath(QStringLiteral("previous-resources"));
    const QString returned = QDir(stage).filePath(QStringLiteral("returned-resources"));
    if (QFileInfo(destination).isSymLink() || QFileInfo(resources).isSymLink()
        || QFileInfo(backup).isSymLink() || QFileInfo(returned).isSymLink()) {
        return RecoveryResult::Refused;
    }

    QLockFile lock(lockPathFor(destination));
    lock.setStaleLockTime(0);
    if (QFileInfo::exists(lock.fileName())) {
        if (!allowConfirmedDeadOwner || !sameHostDeadOwner(lock)
            || !lock.removeStaleLockFile()) {
            return RecoveryResult::Busy;
        }
    }
    if (!lock.tryLock(0))
        return RecoveryResult::Busy;

    const bool oldMarkdownExisted = manifest.value(QStringLiteral("oldMarkdownExisted")).toBool();
    const QByteArray oldMarkdownDigest = manifest.value(QStringLiteral("oldMarkdownSha256"))
        .toString().toLatin1();
    const QByteArray newMarkdownDigest = manifest.value(QStringLiteral("newMarkdownSha256"))
        .toString().toLatin1();
    const QRegularExpression digestPattern(QStringLiteral("^[0-9a-f]{64}$"));
    auto validDigest = [&](const QByteArray& digest) {
        return digestPattern.match(QString::fromLatin1(digest)).hasMatch();
    };
    if (!manifest.value(QStringLiteral("oldMarkdownExisted")).isBool()
        || !manifest.value(QStringLiteral("oldResourcesExisted")).isBool()
        || !manifest.value(QStringLiteral("newResourcesExisted")).isBool()
        || !validDigest(newMarkdownDigest)
        || (oldMarkdownExisted ? !validDigest(oldMarkdownDigest) : !oldMarkdownDigest.isEmpty())) {
        return RecoveryResult::Refused;
    }
    const bool destinationExists = QFileInfo::exists(destination);
    const QByteArray currentMarkdownDigest = destinationExists ? fileDigest(destination) : QByteArray();
    const bool matchesOldMarkdown = oldMarkdownExisted
        ? destinationExists && !oldMarkdownDigest.isEmpty() && currentMarkdownDigest == oldMarkdownDigest
        : !destinationExists;
    const bool matchesNewMarkdown = destinationExists && !newMarkdownDigest.isEmpty()
        && currentMarkdownDigest == newMarkdownDigest;
    if (!matchesOldMarkdown && !matchesNewMarkdown)
        return RecoveryResult::Refused;

    const bool oldResourcesExisted = manifest.value(QStringLiteral("oldResourcesExisted")).toBool();
    const QByteArray oldResourcesDigest = manifest.value(QStringLiteral("oldResourcesSha256"))
        .toString().toLatin1();
    const bool newResourcesExisted = manifest.value(QStringLiteral("newResourcesExisted")).toBool();
    const QByteArray newResourcesDigest = manifest.value(QStringLiteral("newResourcesSha256"))
        .toString().toLatin1();
    if ((oldResourcesExisted ? !validDigest(oldResourcesDigest) : !oldResourcesDigest.isEmpty())
        || (newResourcesExisted ? !validDigest(newResourcesDigest) : !newResourcesDigest.isEmpty())) {
        return RecoveryResult::Refused;
    }

    auto matchesDirectory = [](const QString& path, const QByteArray& digest) {
        return QFileInfo(path).isDir() && !digest.isEmpty() && directoryDigest(path) == digest;
    };

    if (matchesNewMarkdown) {
        const bool resourcesMatch = newResourcesExisted
            ? matchesDirectory(resources, newResourcesDigest)
            : !QFileInfo::exists(resources);
        if (!resourcesMatch)
            return RecoveryResult::Refused;
        return durableRemoveTree(stage)
            ? RecoveryResult::Finalized : RecoveryResult::Refused;
    }

    const bool backupExists = QFileInfo::exists(backup);
    const bool resourcesExist = QFileInfo::exists(resources);
    const bool returnedExists = QFileInfo::exists(returned);
    if (backupExists && (!oldResourcesExisted
        || !matchesDirectory(backup, oldResourcesDigest))) {
        return RecoveryResult::Refused;
    }
    if (returnedExists && (!newResourcesExisted
        || !matchesDirectory(returned, newResourcesDigest))) {
        return RecoveryResult::Refused;
    }
    if (returnedExists && backupExists && resourcesExist)
        return RecoveryResult::Refused;

    if (resourcesExist) {
        if (backupExists || !oldResourcesExisted) {
            if (!newResourcesExisted || !matchesDirectory(resources, newResourcesDigest)
                || returnedExists || !durableRename(resources, returned)) {
                return RecoveryResult::Refused;
            }
        } else if (!matchesDirectory(resources, oldResourcesDigest)) {
            return RecoveryResult::Refused;
        }
    } else if (!backupExists && oldResourcesExisted) {
        return RecoveryResult::Refused;
    }

    if (backupExists && !durableRename(backup, resources))
        return RecoveryResult::Refused;
    if (!durableRemoveTree(stage))
        return RecoveryResult::Refused;
    return RecoveryResult::RolledBack;
}

enum class RecoveryDecision {
    Inspect,
    Cancel,
    Retain,
    Confirm
};

enum class PolicyResult {
    AwaitingConfirmation,
    Cancelled,
    Retained,
    RolledBack,
    Finalized,
    Refused,
    Busy,
    AuditFailure
};

QString decisionName(RecoveryDecision decision)
{
    switch (decision) {
    case RecoveryDecision::Inspect:
        return QStringLiteral("inspect");
    case RecoveryDecision::Cancel:
        return QStringLiteral("cancel");
    case RecoveryDecision::Retain:
        return QStringLiteral("retain");
    case RecoveryDecision::Confirm:
        return QStringLiteral("confirm");
    }
    return QStringLiteral("unknown");
}

QString policyResultName(PolicyResult result)
{
    switch (result) {
    case PolicyResult::AwaitingConfirmation:
        return QStringLiteral("awaiting-confirmation");
    case PolicyResult::Cancelled:
        return QStringLiteral("cancelled");
    case PolicyResult::Retained:
        return QStringLiteral("retained");
    case PolicyResult::RolledBack:
        return QStringLiteral("rolled-back");
    case PolicyResult::Finalized:
        return QStringLiteral("finalized");
    case PolicyResult::Refused:
        return QStringLiteral("refused");
    case PolicyResult::Busy:
        return QStringLiteral("busy");
    case PolicyResult::AuditFailure:
        return QStringLiteral("audit-failure");
    }
    return QStringLiteral("unknown");
}

QByteArray auditIdentity(const QString& stage)
{
    const QJsonDocument document = QJsonDocument::fromJson(
        readFile(QDir(stage).filePath(QStringLiteral("transaction.json"))));
    if (!document.isObject())
        return QByteArrayLiteral("invalid");
    const QString destinationName = document.object().value(
        QStringLiteral("destinationName")).toString();
    if (!safeBaseName(destinationName))
        return QByteArrayLiteral("invalid");
    return identityDigest(QDir(QFileInfo(stage).absolutePath()).filePath(destinationName));
}

bool validAuditFields(const QString& identity, const QString& decision,
                      const QString& event)
{
    static const QRegularExpression identityPattern(QStringLiteral("^[0-9a-f]{24}$"));
    static const QSet<QString> allowedDecisions{
        QStringLiteral("inspect"), QStringLiteral("cancel"),
        QStringLiteral("retain"), QStringLiteral("confirm")};
    static const QSet<QString> allowedEvents{
        QStringLiteral("awaiting-confirmation"), QStringLiteral("cancelled"),
        QStringLiteral("retained"), QStringLiteral("requested"),
        QStringLiteral("rolled-back"), QStringLiteral("finalized"),
        QStringLiteral("refused"), QStringLiteral("busy")};
    return (identity == QStringLiteral("invalid")
        || identityPattern.match(identity).hasMatch())
        && allowedDecisions.contains(decision) && allowedEvents.contains(event);
}

bool validAuditBytes(const QByteArray& bytes)
{
    if (bytes.isEmpty())
        return true;
    if (!bytes.endsWith('\n'))
        return false;
    QList<QByteArray> lines = bytes.split('\n');
    lines.removeLast();
    for (const QByteArray& line : lines) {
        const QJsonDocument document = QJsonDocument::fromJson(line);
        if (!document.isObject())
            return false;
        const QJsonObject object = document.object();
        if (!exactJsonKeys(object, {QStringLiteral("destinationIdentity"),
                QStringLiteral("decision"), QStringLiteral("event")})
            || !object.value(QStringLiteral("destinationIdentity")).isString()
            || !object.value(QStringLiteral("decision")).isString()
            || !object.value(QStringLiteral("event")).isString()
            || !validAuditFields(object.value(QStringLiteral("destinationIdentity")).toString(),
                object.value(QStringLiteral("decision")).toString(),
                object.value(QStringLiteral("event")).toString())) {
            return false;
        }
    }
    return true;
}

bool auditCheckpointAndHold(const QString& checkpoint,
                            const QString& requestedCheckpoint,
                            const QString& readyPath)
{
    if (checkpoint != requestedCheckpoint)
        return true;
    if (readyPath.isEmpty() || !writeFile(readyPath, checkpoint.toUtf8()))
        return false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000)
        QThread::msleep(10);
    return false;
}

QString auditLockPathFor(const QString& auditPath)
{
    return auditPath + QStringLiteral(".lock");
}

bool appendAuditEvent(const QString& auditPath, const QString& identity,
                      RecoveryDecision decision, const QString& event,
                      qsizetype maximumBytes = 65536, int maximumEntries = 256,
                      int maximumArchives = 2,
                      const QString& holdCheckpoint = {},
                      const QString& readyPath = {},
                      bool allowConfirmedDeadOwner = false)
{
    const QString auditDirectory = QFileInfo(auditPath).absolutePath();
    const QFileDevice::Permissions privateDirectoryPermissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    const QFileDevice::Permissions privateFilePermissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    if (maximumBytes <= 0 || maximumEntries <= 0 || maximumArchives < 1
        || QFileInfo(auditPath).isSymLink() || QFileInfo(auditDirectory).isSymLink()
        || (!QFileInfo(auditDirectory).isDir() && !QDir().mkpath(auditDirectory))
        || !QFile::setPermissions(auditDirectory, privateDirectoryPermissions)) {
        return false;
    }
    const QString auditLockPath = auditLockPathFor(auditPath);
    if (QFileInfo(auditLockPath).isSymLink())
        return false;
    QLockFile lock(auditLockPath);
    lock.setStaleLockTime(0);
    if (QFileInfo::exists(auditLockPath)) {
        if (!allowConfirmedDeadOwner || !sameHostDeadOwner(lock)
            || !lock.removeStaleLockFile() || !syncDirectory(auditDirectory)) {
            return false;
        }
    }
    if (!lock.tryLock(0)
        || !QFile::setPermissions(auditLockPath, privateFilePermissions)
        || !syncRegularFile(auditLockPath) || !syncDirectory(auditDirectory)
        || !auditCheckpointAndHold(QStringLiteral("lock-acquired"),
            holdCheckpoint, readyPath)) {
        return false;
    }
    const QString decisionText = decisionName(decision);
    if (!validAuditFields(identity, decisionText, event)) {
        return false;
    }
    QByteArray bytes = QFileInfo::exists(auditPath) ? readFile(auditPath) : QByteArray();
    if (!validAuditBytes(bytes))
        return false;
    QVector<bool> archiveExists(maximumArchives + 1, false);
    for (int index = 1; index <= maximumArchives; ++index) {
        const QString archive = auditPath + QStringLiteral(".%1").arg(index);
        const QFileInfo archiveInfo(archive);
        if (archiveInfo.isSymLink()
            || (archiveInfo.exists() && (!archiveInfo.isFile()
                || !validAuditBytes(readFile(archive))))) {
            return false;
        }
        archiveExists[index] = archiveInfo.exists();
    }
    int firstMissingArchive = 1;
    while (firstMissingArchive <= maximumArchives
        && archiveExists.at(firstMissingArchive)) {
        ++firstMissingArchive;
    }
    bool archiveAfterGap = false;
    int missingArchives = 0;
    for (int index = 1; index <= maximumArchives; ++index) {
        missingArchives += !archiveExists.at(index);
        archiveAfterGap = archiveAfterGap
            || (index > firstMissingArchive && archiveExists.at(index));
    }
    // A sparse archive layout can only be resumed safely when it represents one
    // interrupted descending shift. Arbitrary gaps are retained and refused.
    if ((archiveAfterGap && missingArchives != 1)
        || (bytes.isEmpty() && archiveAfterGap)) {
        return false;
    }
    QByteArray line = QJsonDocument(QJsonObject{
        {QStringLiteral("destinationIdentity"), identity},
        {QStringLiteral("decision"), decisionText},
        {QStringLiteral("event"), event}
    }).toJson(QJsonDocument::Compact);
    line += '\n';
    if (line.size() > maximumBytes)
        return false;
    const int currentEntries = bytes.isEmpty() ? 0 : bytes.count('\n');
    if (!bytes.isEmpty() && (bytes.size() + line.size() > maximumBytes
        || currentEntries + 1 > maximumEntries)) {
        int freeArchive = firstMissingArchive;
        if (freeArchive > maximumArchives) {
            const QString oldest = auditPath
                + QStringLiteral(".%1").arg(maximumArchives);
            if (!QFile::remove(oldest) || !syncDirectory(auditDirectory)
                || !auditCheckpointAndHold(QStringLiteral("oldest-removed"),
                    holdCheckpoint, readyPath)) {
                return false;
            }
            freeArchive = maximumArchives;
        }
        for (int index = freeArchive - 1; index >= 1; --index) {
            const QString source = auditPath + QStringLiteral(".%1").arg(index);
            const QString destination = auditPath + QStringLiteral(".%1").arg(index + 1);
            const QString checkpoint = QStringLiteral("archive-%1-to-%2")
                .arg(index).arg(index + 1);
            if (QFileInfo::exists(source)
                && (!durableRename(source, destination)
                    || !auditCheckpointAndHold(checkpoint, holdCheckpoint, readyPath))) {
                return false;
            }
        }
        if (!durableRename(auditPath, auditPath + QStringLiteral(".1"))
            || !auditCheckpointAndHold(QStringLiteral("live-archived"),
                holdCheckpoint, readyPath)) {
            return false;
        }
        bytes.clear();
    }
    bytes += line;
    QSaveFile output(auditPath);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(bytes) != bytes.size()
        || !output.setPermissions(privateFilePermissions)
        || !output.commit()
        || !auditCheckpointAndHold(QStringLiteral("live-committed"),
            holdCheckpoint, readyPath)) {
        return false;
    }
    if (!syncRegularFile(auditPath)
        || !auditCheckpointAndHold(QStringLiteral("live-file-synced"),
            holdCheckpoint, readyPath)) {
        return false;
    }
    if (!syncDirectory(QFileInfo(auditPath).absolutePath())
        || !auditCheckpointAndHold(QStringLiteral("live-directory-synced"),
            holdCheckpoint, readyPath)) {
        return false;
    }
    lock.unlock();
    return !QFileInfo::exists(auditLockPath) && syncDirectory(auditDirectory);
}

bool holdAuditLock(const QString& auditPath, const QString& readyPath,
                   const QString& releasePath)
{
    const QString directory = QFileInfo(auditPath).absolutePath();
    const QFileDevice::Permissions privateDirectoryPermissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    const QFileDevice::Permissions privateFilePermissions =
        QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    const QString lockPath = auditLockPathFor(auditPath);
    if (QFileInfo(directory).isSymLink()
        || (!QFileInfo(directory).isDir() && !QDir().mkpath(directory))
        || !QFile::setPermissions(directory, privateDirectoryPermissions)
        || QFileInfo(lockPath).isSymLink() || QFileInfo::exists(lockPath)) {
        return false;
    }
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0)
        || !QFile::setPermissions(lockPath, privateFilePermissions)
        || !syncRegularFile(lockPath) || !syncDirectory(directory)
        || !writeFile(readyPath, "READY")) {
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    while (!QFileInfo::exists(releasePath) && timer.elapsed() < 5000)
        QThread::msleep(10);
    return QFileInfo::exists(releasePath);
}

QString quarantineCorruptAuditLog(const QString& auditPath,
                                  bool allowConfirmedDeadOwner = false)
{
    const QFileInfo info(auditPath);
    if (info.isSymLink() || !info.isFile()
        || QFileInfo(info.absolutePath()).isSymLink()) {
        return {};
    }
    const QString lockPath = auditLockPathFor(auditPath);
    if (QFileInfo(lockPath).isSymLink())
        return {};
    QLockFile lock(lockPath);
    lock.setStaleLockTime(0);
    if (QFileInfo::exists(lockPath)) {
        if (!allowConfirmedDeadOwner || !sameHostDeadOwner(lock)
            || !lock.removeStaleLockFile() || !syncDirectory(info.absolutePath())) {
            return {};
        }
    }
    if (!lock.tryLock(0)
        || !QFile::setPermissions(lockPath,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || !syncRegularFile(lockPath) || !syncDirectory(info.absolutePath())) {
        return {};
    }
    const QByteArray bytes = readFile(auditPath);
    if (bytes.isEmpty() || validAuditBytes(bytes))
        return {};
    const QString quarantine = auditPath + QStringLiteral(".corrupt-")
        + QString::fromLatin1(QCryptographicHash::hash(bytes,
            QCryptographicHash::Sha256).toHex().left(16));
    if (QFileInfo::exists(quarantine) || QFileInfo(quarantine).isSymLink()
        || !durableRename(auditPath, quarantine)
        || !QFile::setPermissions(quarantine,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || !syncRegularFile(quarantine)) {
        return {};
    }
    lock.unlock();
    return !QFileInfo::exists(lockPath) && syncDirectory(info.absolutePath())
        ? quarantine : QString();
}

PolicyResult applyRecoveryDecision(const QString& stage, RecoveryDecision decision,
                                   const QString& auditPath)
{
    const QString identity = QString::fromLatin1(auditIdentity(stage));
    if (decision == RecoveryDecision::Inspect) {
        return appendAuditEvent(auditPath, identity, decision,
            policyResultName(PolicyResult::AwaitingConfirmation))
            ? PolicyResult::AwaitingConfirmation : PolicyResult::AuditFailure;
    }
    if (decision == RecoveryDecision::Cancel) {
        return appendAuditEvent(auditPath, identity, decision,
            policyResultName(PolicyResult::Cancelled))
            ? PolicyResult::Cancelled : PolicyResult::AuditFailure;
    }
    if (decision == RecoveryDecision::Retain) {
        return appendAuditEvent(auditPath, identity, decision,
            policyResultName(PolicyResult::Retained))
            ? PolicyResult::Retained : PolicyResult::AuditFailure;
    }
    if (!appendAuditEvent(auditPath, identity, decision, QStringLiteral("requested")))
        return PolicyResult::AuditFailure;
    const RecoveryResult recovery = recover(stage, true);
    PolicyResult result = PolicyResult::Refused;
    if (recovery == RecoveryResult::RolledBack)
        result = PolicyResult::RolledBack;
    else if (recovery == RecoveryResult::Finalized)
        result = PolicyResult::Finalized;
    else if (recovery == RecoveryResult::Busy)
        result = PolicyResult::Busy;
    if (!appendAuditEvent(auditPath, identity, decision, policyResultName(result)))
        return PolicyResult::AuditFailure;
    return result;
}

struct Fixture {
    QString parent;
    QString destination;
    QString resources;
    QString stage;
    QString stagedResources;
    QByteArray oldMarkdownDigest;
    QByteArray newMarkdownDigest;
    QByteArray oldResourcesDigest;
    QByteArray newResourcesDigest;
};

bool prepareFixture(const QString& root, const QString& name, bool existing, Fixture& fixture)
{
    fixture.parent = QDir(root).filePath(name);
    if (!QDir().mkpath(fixture.parent))
        return false;
    fixture.destination = QDir(fixture.parent).filePath(QStringLiteral("note.md"));
    fixture.resources = QDir(fixture.parent).filePath(QStringLiteral("note_files"));
    fixture.stage = recoveryPathFor(fixture.destination);
    fixture.stagedResources = QDir(fixture.stage).filePath(QStringLiteral("content/note_files"));
    if (!QDir().mkpath(fixture.stagedResources))
        return false;
    if (existing && (!writeFile(fixture.destination, "OLD MARKDOWN\n")
        || !QDir().mkpath(fixture.resources)
        || !writeFile(QDir(fixture.resources).filePath(QStringLiteral("old.bin")), "OLD RESOURCE"))) {
        return false;
    }
    if (!writeFile(QDir(fixture.stage).filePath(QStringLiteral("new-markdown")), "NEW MARKDOWN\n")
        || !writeFile(QDir(fixture.stagedResources).filePath(QStringLiteral("new.bin")), "NEW RESOURCE")) {
        return false;
    }
    if (!syncRegularFile(QDir(fixture.stage).filePath(QStringLiteral("new-markdown")))
        || !syncTree(QDir(fixture.stage).filePath(QStringLiteral("content")))
        || (existing && (!syncRegularFile(fixture.destination)
            || !syncTree(fixture.resources)))) {
        return false;
    }
    fixture.oldMarkdownDigest = existing ? fileDigest(fixture.destination) : QByteArray();
    fixture.newMarkdownDigest = fileDigest(QDir(fixture.stage).filePath(QStringLiteral("new-markdown")));
    fixture.oldResourcesDigest = existing ? directoryDigest(fixture.resources) : QByteArray();
    fixture.newResourcesDigest = directoryDigest(fixture.stagedResources);
    return writeManifest(fixture.stage, QStringLiteral("note.md"), QStringLiteral("note_files"),
        existing, fixture.oldMarkdownDigest, fixture.newMarkdownDigest,
        existing, fixture.oldResourcesDigest, true, fixture.newResourcesDigest);
}

bool oldPair(const Fixture& fixture)
{
    return readFile(fixture.destination) == "OLD MARKDOWN\n"
        && readFile(QDir(fixture.resources).filePath(QStringLiteral("old.bin"))) == "OLD RESOURCE";
}

bool newPair(const Fixture& fixture)
{
    return readFile(fixture.destination) == "NEW MARKDOWN\n"
        && readFile(QDir(fixture.resources).filePath(QStringLiteral("new.bin"))) == "NEW RESOURCE";
}

bool advanceAndHold(const QString& stage, const QString& checkpoint,
                    const QString& readyPath)
{
    const QJsonDocument document = QJsonDocument::fromJson(
        readFile(QDir(stage).filePath(QStringLiteral("transaction.json"))));
    if (!document.isObject())
        return false;
    const QString destinationName = document.object().value(
        QStringLiteral("destinationName")).toString();
    const QString resourcesName = document.object().value(
        QStringLiteral("resourcesName")).toString();
    if (!safeBaseName(destinationName) || !safeBaseName(resourcesName))
        return false;
    const QString parent = QFileInfo(stage).absolutePath();
    const QString destination = QDir(parent).filePath(destinationName);
    const QString resources = QDir(parent).filePath(resourcesName);
    if (stage != recoveryPathFor(destination))
        return false;
    QLockFile lock(lockPathFor(destination));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return false;
    const QString backup = QDir(stage).filePath(QStringLiteral("previous-resources"));
    const QString staged = QDir(stage).filePath(QStringLiteral("content/") + resourcesName);
    if (!durableRename(resources, backup))
        return false;
    if (checkpoint != QStringLiteral("old-moved")) {
        if (!durableRename(staged, resources))
            return false;
        if (checkpoint == QStringLiteral("markdown-committed")
            && !durableWriteFile(destination, "NEW MARKDOWN\n")) {
            return false;
        }
    }
    if (!durableWriteFile(readyPath, checkpoint.toUtf8()))
        return false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000)
        QThread::msleep(10);
    return false;
}

bool advanceUnsyncedAndHold(const QString& stage, const QString& checkpoint,
                            const QString& readyPath)
{
    const QJsonDocument document = QJsonDocument::fromJson(
        readFile(QDir(stage).filePath(QStringLiteral("transaction.json"))));
    if (!document.isObject())
        return false;
    const QString destinationName = document.object().value(
        QStringLiteral("destinationName")).toString();
    const QString resourcesName = document.object().value(
        QStringLiteral("resourcesName")).toString();
    if (!safeBaseName(destinationName) || !safeBaseName(resourcesName))
        return false;
    const QString parent = QFileInfo(stage).absolutePath();
    const QString destination = QDir(parent).filePath(destinationName);
    const QString resources = QDir(parent).filePath(resourcesName);
    if (stage != recoveryPathFor(destination))
        return false;
    QLockFile lock(lockPathFor(destination));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(0))
        return false;
    const QString backup = QDir(stage).filePath(QStringLiteral("previous-resources"));
    const QString staged = QDir(stage).filePath(QStringLiteral("content/") + resourcesName);

    if (checkpoint == QStringLiteral("old-renamed-before-sync")) {
        if (!renameWithoutDirectorySync(resources, backup))
            return false;
    } else if (checkpoint == QStringLiteral("new-renamed-before-sync")) {
        if (!durableRename(resources, backup)
            || !renameWithoutDirectorySync(staged, resources)) {
            return false;
        }
    } else if (checkpoint == QStringLiteral("markdown-replaced-before-sync")) {
        if (!durableRename(resources, backup)
            || !durableRename(staged, resources)
            || !replaceMarkdownWithoutDirectorySync(destination)) {
            return false;
        }
    } else {
        return false;
    }

    // The readiness file is deliberately outside the export's parent directory so
    // observing the checkpoint cannot accidentally persist the rename under test.
    if (!writeFile(readyPath, checkpoint.toUtf8()))
        return false;
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000)
        QThread::msleep(10);
    return false;
}

}

int main(int argc, char** argv)
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments();
    if (arguments.size() == 5 && arguments.at(1) == QStringLiteral("--hold-lock")) {
        QLockFile lock(lockPathFor(arguments.at(2)));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0) || !writeFile(arguments.at(3), "READY"))
            return 3;
        QElapsedTimer timer;
        timer.start();
        while (!QFileInfo::exists(arguments.at(4)) && timer.elapsed() < 5000)
            QThread::msleep(10);
        return QFileInfo::exists(arguments.at(4)) ? 0 : 4;
    }
    if (arguments.size() == 5 && arguments.at(1) == QStringLiteral("--hold-cache-lock")) {
        QLockFile lock(cacheLockPathFor(arguments.at(2)));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0) || !writeFile(arguments.at(3), "READY"))
            return 7;
        QElapsedTimer timer;
        timer.start();
        while (!QFileInfo::exists(arguments.at(4)) && timer.elapsed() < 5000)
            QThread::msleep(10);
        return QFileInfo::exists(arguments.at(4)) ? 0 : 8;
    }
    if (arguments.size() == 5
        && arguments.at(1) == QStringLiteral("--stage-cache-and-hold")) {
        return stagePersistentCacheAndHold(arguments.at(2), arguments.at(3),
            arguments.at(4).toUtf8()) ? 0 : 9;
    }
    if (arguments.size() == 5 && arguments.at(1) == QStringLiteral("--advance-and-hold"))
        return advanceAndHold(arguments.at(2), arguments.at(3), arguments.at(4)) ? 0 : 5;
    if (arguments.size() == 5
        && arguments.at(1) == QStringLiteral("--advance-unsynced-and-hold")) {
        return advanceUnsyncedAndHold(arguments.at(2), arguments.at(3), arguments.at(4)) ? 0 : 6;
    }
    if (arguments.size() == 6
        && arguments.at(1) == QStringLiteral("--append-audit-and-hold")) {
        return appendAuditEvent(arguments.at(2), arguments.at(3),
            RecoveryDecision::Retain, QStringLiteral("retained"), 4096, 3, 2,
            arguments.at(4), arguments.at(5)) ? 0 : 10;
    }
    if (arguments.size() == 5
        && arguments.at(1) == QStringLiteral("--hold-audit-lock")) {
        return holdAuditLock(arguments.at(2), arguments.at(3), arguments.at(4)) ? 0 : 11;
    }
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 2;

    int checks = 0;
    int failures = 0;
    auto check = [&](bool condition, const char* text) {
        ++checks;
        failures += !condition;
        qInfo().noquote() << (condition ? "PASS" : "FAIL") << text;
    };

    const QString durabilityDirectory = QDir(temporary.path()).filePath(QStringLiteral("durability"));
    const QString durabilityFile = QDir(durabilityDirectory).filePath(QStringLiteral("file.bin"));
    const QString durabilityLink = QDir(durabilityDirectory).filePath(QStringLiteral("file-link"));
    bool usedFullSync = false;
    check(QDir().mkpath(durabilityDirectory) && writeFile(durabilityFile, "DURABLE")
        && syncRegularFile(durabilityFile, &usedFullSync) && syncDirectory(durabilityDirectory),
        "regular file and containing directory support explicit persistence barriers");
#ifdef F_FULLFSYNC
    check(usedFullSync, "macOS regular-file persistence uses F_FULLFSYNC rather than only fsync");
#else
    check(!usedFullSync, "non-macOS persistence records the fsync fallback explicitly");
#endif
    check(QFile::link(durabilityFile, durabilityLink) && !syncRegularFile(durabilityLink),
        "persistence helper refuses to follow a replacement symbolic link");

    constexpr int metadataDirectoryCount = 32;
    constexpr int metadataFilesPerDirectory = 64;
    constexpr int metadataFileCount = metadataDirectoryCount * metadataFilesPerDirectory;
    constexpr int metadataFileBytes = 4096;
    constexpr qint64 metadataPayloadBytes = qint64(metadataFileCount) * metadataFileBytes;
    const QString metadataRoot = QDir(temporary.path()).filePath(QStringLiteral("metadata-tree"));
    QString metadataProbe;
    bool metadataSeeded = QDir().mkpath(metadataRoot);
    const QByteArray metadataPayload(metadataFileBytes, 'M');
    for (int directoryIndex = 0; metadataSeeded
        && directoryIndex < metadataDirectoryCount; ++directoryIndex) {
        const QString directory = QDir(metadataRoot).filePath(
            QStringLiteral("bucket-%1").arg(directoryIndex, 2, 10, QLatin1Char('0')));
        metadataSeeded = QDir().mkpath(directory);
        for (int fileIndex = 0; metadataSeeded
            && fileIndex < metadataFilesPerDirectory; ++fileIndex) {
            const QString file = QDir(directory).filePath(
                QStringLiteral("entry-%1.bin").arg(fileIndex, 2, 10, QLatin1Char('0')));
            metadataSeeded = writeFile(file, metadataPayload);
            if (directoryIndex == 0 && fileIndex == 0)
                metadataProbe = file;
        }
    }
    metadataSeeded = metadataSeeded && QFile::setPermissions(metadataProbe,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    bool metadataXattrsSeeded = true;
#ifdef Q_OS_MACOS
    const QByteArray encodedMetadataProbe = QFile::encodeName(metadataProbe);
    const QByteArray encodedMetadataRoot = QFile::encodeName(metadataRoot);
    constexpr char metadataXattrName[] = "user.wiznote.phase73";
    constexpr char metadataXattrValue[] = "metadata-v1";
    constexpr char directoryXattrName[] = "user.wiznote.phase73.directory";
    constexpr char directoryXattrValue[] = "directory-v1";
    metadataXattrsSeeded = ::setxattr(encodedMetadataProbe.constData(), metadataXattrName,
        metadataXattrValue, sizeof(metadataXattrValue) - 1, 0, XATTR_NOFOLLOW) == 0
        && ::setxattr(encodedMetadataRoot.constData(), directoryXattrName,
            directoryXattrValue, sizeof(directoryXattrValue) - 1, 0, XATTR_NOFOLLOW) == 0;
#endif
    metadataSeeded = metadataSeeded && metadataXattrsSeeded;
    check(metadataSeeded,
        "2048-file 8 MiB synthetic resource tree includes controlled metadata");

    qint64 contentDigestMedianMs = -1;
    qint64 metadataDigestMedianMs = -1;
    auto benchmarkDigest = [](auto digestFunction, QByteArray& digest, qint64& medianMs) {
        std::array<qint64, 3> timings{};
        bool consistent = true;
        for (size_t index = 0; index < timings.size(); ++index) {
            QElapsedTimer timer;
            timer.start();
            const QByteArray current = digestFunction();
            timings[index] = timer.elapsed();
            if (current.isEmpty() || (!digest.isEmpty() && current != digest))
                consistent = false;
            if (digest.isEmpty())
                digest = current;
        }
        std::sort(timings.begin(), timings.end());
        medianMs = timings[1];
        return consistent && !digest.isEmpty();
    };
    QByteArray contentDigestBefore;
    QByteArray metadataDigestBefore;
    const bool contentDigestMeasured = metadataSeeded && benchmarkDigest(
        [&] { return directoryDigest(metadataRoot); },
        contentDigestBefore, contentDigestMedianMs);
    const bool metadataDigestMeasured = metadataSeeded && benchmarkDigest(
        [&] { return metadataDirectoryDigest(metadataRoot); },
        metadataDigestBefore, metadataDigestMedianMs);
    check(contentDigestMeasured && metadataDigestMeasured
        && contentDigestMedianMs < 15000 && metadataDigestMedianMs < 15000,
        "content and metadata-aware tree digests complete consistently within the diagnostic bound");

    bool metadataMutated = QFile::setPermissions(metadataProbe,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ReadGroup);
#ifdef Q_OS_MACOS
    constexpr char metadataXattrValue2[] = "metadata-v2";
    metadataMutated = metadataMutated
        && ::setxattr(encodedMetadataProbe.constData(), metadataXattrName,
            metadataXattrValue2, sizeof(metadataXattrValue2) - 1, 0, XATTR_NOFOLLOW) == 0;
#endif
    const QByteArray contentDigestAfter = metadataMutated
        ? directoryDigest(metadataRoot) : QByteArray();
    const QByteArray metadataDigestAfter = metadataMutated
        ? metadataDirectoryDigest(metadataRoot) : QByteArray();
    check(metadataMutated && contentDigestAfter == contentDigestBefore,
        "metadata-only mutation leaves the content-only resource digest unchanged");
    check(metadataMutated && !metadataDigestAfter.isEmpty()
        && metadataDigestAfter != metadataDigestBefore,
        "metadata-aware resource digest detects permissions and extended-attribute changes");
    qInfo().noquote() << "OBSERVATION metadata-digest"
        << "files=" << metadataFileCount
        << "payloadBytes=" << metadataPayloadBytes
        << "contentMedianMs=" << contentDigestMedianMs
        << "metadataMedianMs=" << metadataDigestMedianMs
        << "xattrsSeeded=" << metadataXattrsSeeded;

    QHash<QString, CachedFileDigest> digestCache;
    QElapsedTimer incrementalTimer;
    incrementalTimer.start();
    const IncrementalDigestResult coldIncremental =
        incrementalMetadataDirectoryDigest(metadataRoot, digestCache);
    const qint64 coldIncrementalMs = incrementalTimer.elapsed();
    incrementalTimer.restart();
    const IncrementalDigestResult warmIncremental =
        incrementalMetadataDirectoryDigest(metadataRoot, digestCache);
    const qint64 warmIncrementalMs = incrementalTimer.elapsed();
    check(!coldIncremental.digest.isEmpty()
        && coldIncremental.hashedFiles == metadataFileCount
        && coldIncremental.reusedFiles == 0
        && digestCache.size() == metadataFileCount,
        "cold incremental digest hashes every synthetic file and records exact cache membership");
    check(warmIncremental.digest == coldIncremental.digest
        && warmIncremental.hashedFiles == 0
        && warmIncremental.reusedFiles == metadataFileCount,
        "unchanged incremental digest reuses every cached content hash");

    const QString contentMutationPath = QDir(metadataRoot).filePath(
        QStringLiteral("bucket-00/entry-01.bin"));
    const QByteArray encodedContentMutation = QFile::encodeName(contentMutationPath);
    struct stat contentMutationState {};
    bool sameSizeContentMutated = ::lstat(encodedContentMutation.constData(),
        &contentMutationState) == 0;
    const QByteArray changedPayload(metadataFileBytes, 'N');
    sameSizeContentMutated = sameSizeContentMutated
        && writeFile(contentMutationPath, changedPayload);
    struct timespec originalTimes[2] {};
#ifdef Q_OS_MACOS
    originalTimes[0] = contentMutationState.st_atimespec;
    originalTimes[1] = contentMutationState.st_mtimespec;
#else
    originalTimes[0] = contentMutationState.st_atim;
    originalTimes[1] = contentMutationState.st_mtim;
#endif
    sameSizeContentMutated = sameSizeContentMutated
        && ::utimensat(AT_FDCWD, encodedContentMutation.constData(), originalTimes,
            AT_SYMLINK_NOFOLLOW) == 0;
    const IncrementalDigestResult sameSizeContentResult = sameSizeContentMutated
        ? incrementalMetadataDirectoryDigest(metadataRoot, digestCache)
        : IncrementalDigestResult{};
    check(sameSizeContentMutated
        && sameSizeContentResult.digest != warmIncremental.digest
        && sameSizeContentResult.hashedFiles == 1
        && sameSizeContentResult.reusedFiles == metadataFileCount - 1,
        "same-size content rewrite with restored mtime invalidates one cached file through ctime");

    const bool permissionCacheMutation = QFile::setPermissions(metadataProbe,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    const IncrementalDigestResult permissionCacheResult = permissionCacheMutation
        ? incrementalMetadataDirectoryDigest(metadataRoot, digestCache)
        : IncrementalDigestResult{};
    check(permissionCacheMutation
        && permissionCacheResult.digest != sameSizeContentResult.digest
        && permissionCacheResult.hashedFiles == 1
        && permissionCacheResult.reusedFiles == metadataFileCount - 1,
        "permission mutation invalidates exactly one cached file and changes the tree digest");

    IncrementalDigestResult xattrCacheResult = permissionCacheResult;
    bool xattrCacheMutation = true;
#ifdef Q_OS_MACOS
    constexpr char metadataXattrValue3[] = "metadata-v3";
    xattrCacheMutation = ::setxattr(encodedMetadataProbe.constData(), metadataXattrName,
        metadataXattrValue3, sizeof(metadataXattrValue3) - 1, 0, XATTR_NOFOLLOW) == 0;
    xattrCacheResult = xattrCacheMutation
        ? incrementalMetadataDirectoryDigest(metadataRoot, digestCache)
        : IncrementalDigestResult{};
    check(xattrCacheMutation
        && xattrCacheResult.digest != permissionCacheResult.digest
        && xattrCacheResult.hashedFiles == 1
        && xattrCacheResult.reusedFiles == metadataFileCount - 1,
        "extended-attribute mutation invalidates exactly one cached file");
#else
    check(xattrCacheMutation,
        "extended-attribute cache invalidation is not applicable on this platform");
#endif

    const QString renameSource = QDir(metadataRoot).filePath(
        QStringLiteral("bucket-00/entry-02.bin"));
    const QString renameDestination = QDir(metadataRoot).filePath(
        QStringLiteral("bucket-00/renamed-entry.bin"));
    const bool cacheRename = QDir().rename(renameSource, renameDestination);
    const IncrementalDigestResult renameCacheResult = cacheRename
        ? incrementalMetadataDirectoryDigest(metadataRoot, digestCache)
        : IncrementalDigestResult{};
    check(cacheRename && renameCacheResult.digest != xattrCacheResult.digest
        && renameCacheResult.hashedFiles == 1
        && renameCacheResult.reusedFiles == metadataFileCount - 1
        && digestCache.size() == metadataFileCount,
        "path rename drops the stale key and conservatively rehashes the renamed file");

    const QString addedCacheFile = QDir(metadataRoot).filePath(QStringLiteral("added.bin"));
    const bool cacheFileAdded = writeFile(addedCacheFile, metadataPayload);
    const IncrementalDigestResult addCacheResult = cacheFileAdded
        ? incrementalMetadataDirectoryDigest(metadataRoot, digestCache)
        : IncrementalDigestResult{};
    check(cacheFileAdded && addCacheResult.digest != renameCacheResult.digest
        && addCacheResult.hashedFiles == 1
        && addCacheResult.reusedFiles == metadataFileCount
        && digestCache.size() == metadataFileCount + 1,
        "new file adds one cache entry without rehashing unchanged files");
    const bool cacheFileRemoved = QFile::remove(addedCacheFile);
    const IncrementalDigestResult removeCacheResult = cacheFileRemoved
        ? incrementalMetadataDirectoryDigest(metadataRoot, digestCache)
        : IncrementalDigestResult{};
    check(cacheFileRemoved && removeCacheResult.digest == renameCacheResult.digest
        && removeCacheResult.hashedFiles == 0
        && removeCacheResult.reusedFiles == metadataFileCount
        && digestCache.size() == metadataFileCount,
        "removed file prunes its cache entry and restores the prior tree digest");
    qInfo().noquote() << "OBSERVATION incremental-digest"
        << "coldMs=" << coldIncrementalMs
        << "warmMs=" << warmIncrementalMs
        << "coldHashed=" << coldIncremental.hashedFiles
        << "warmReused=" << warmIncremental.reusedFiles;

    const QString persistentCacheDirectory = QDir(temporary.path()).filePath(
        QStringLiteral("persistent-cache"));
    const QString cacheSignalDirectory = QDir(temporary.path()).filePath(
        QStringLiteral("cache-signals"));
    const QString persistentCachePath = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("resource-digests.json"));
    const QByteArray persistentRootIdentity = QCryptographicHash::hash(
        QFile::encodeName(QDir(metadataRoot).canonicalPath()),
        QCryptographicHash::Sha256).toHex();
    check(QDir().mkpath(persistentCacheDirectory)
        && QDir().mkpath(cacheSignalDirectory)
        && writePersistentCache(persistentCachePath, persistentRootIdentity, digestCache),
        "versioned persistent cache commits through an atomic same-directory replacement");
    const QByteArray validPersistentCacheBytes = readFile(persistentCachePath);
    QHash<QString, CachedFileDigest> loadedPersistentCache;
    check(loadPersistentCache(persistentCachePath, persistentRootIdentity,
        loadedPersistentCache) && sameDigestCache(digestCache, loadedPersistentCache),
        "strict persistent cache reload reproduces all 2048 entries exactly");
    const IncrementalDigestResult firstLoadedCacheScan =
        validatedIncrementalMetadataDirectoryDigest(metadataRoot, loadedPersistentCache);
    check(firstLoadedCacheScan.digest == removeCacheResult.digest
        && firstLoadedCacheScan.hashedFiles == metadataFileCount
        && firstLoadedCacheScan.reusedFiles == 0,
        "persistent entries are hints only and must rehash every file before in-process reuse");
    const IncrementalDigestResult secondLoadedCacheScan =
        validatedIncrementalMetadataDirectoryDigest(metadataRoot, loadedPersistentCache);
    check(secondLoadedCacheScan.digest == firstLoadedCacheScan.digest
        && secondLoadedCacheScan.hashedFiles == 0
        && secondLoadedCacheScan.reusedFiles == metadataFileCount,
        "reverified persistent entries become reusable only within the current process");

    const QString racedRelativePath = QStringLiteral("bucket-00/entry-03.bin");
    const QString racedAbsolutePath = QDir(metadataRoot).filePath(racedRelativePath);
    const QByteArray encodedRacedPath = QFile::encodeName(racedAbsolutePath);
    struct stat racedState {};
    bool racedMutation = ::lstat(encodedRacedPath.constData(), &racedState) == 0;
    struct timespec racedTimes[2] {};
#ifdef Q_OS_MACOS
    racedTimes[0] = racedState.st_atimespec;
    racedTimes[1] = racedState.st_mtimespec;
#else
    racedTimes[0] = racedState.st_atim;
    racedTimes[1] = racedState.st_mtim;
#endif
    bool raceInjected = false;
    const IncrementalDigestResult racedDigest =
        validatedIncrementalMetadataDirectoryDigest(metadataRoot, digestCache,
            [&](const QString& relative) {
                if (raceInjected || relative != racedRelativePath)
                    return;
                raceInjected = true;
                racedMutation = racedMutation
                    && writeFile(racedAbsolutePath, QByteArray(metadataFileBytes, 'R'))
                    && ::utimensat(AT_FDCWD, encodedRacedPath.constData(), racedTimes,
                        AT_SYMLINK_NOFOLLOW) == 0;
            });
    check(raceInjected && racedMutation && racedDigest.validationRetries == 1
        && racedDigest.hashedFiles == 1
        && racedDigest.reusedFiles == metadataFileCount - 1
        && racedDigest.digest != removeCacheResult.digest,
        "post-fingerprint same-size rewrite is detected by validation and triggers one-file rehash");
    const IncrementalDigestResult stableAfterRace =
        validatedIncrementalMetadataDirectoryDigest(metadataRoot, digestCache);
    check(stableAfterRace.validationRetries == 0
        && stableAfterRace.hashedFiles == 0
        && stableAfterRace.reusedFiles == metadataFileCount
        && stableAfterRace.digest == racedDigest.digest,
        "stable follow-up scan reuses the race-corrected in-process cache");

    QHash<QString, CachedFileDigest> poisonedPersistentCache = digestCache;
    poisonedPersistentCache[racedRelativePath].contentDigest = QByteArray(64, '0');
    const QString poisonedCachePath = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("poisoned-but-well-formed.json"));
    QHash<QString, CachedFileDigest> loadedPoisonedCache;
    const bool poisonedCacheLoaded = writePersistentCache(poisonedCachePath,
        persistentRootIdentity, poisonedPersistentCache)
        && loadPersistentCache(poisonedCachePath, persistentRootIdentity,
            loadedPoisonedCache);
    const IncrementalDigestResult poisonedCacheResult = poisonedCacheLoaded
        ? validatedIncrementalMetadataDirectoryDigest(metadataRoot, loadedPoisonedCache)
        : IncrementalDigestResult{};
    check(poisonedCacheLoaded && poisonedCacheResult.digest == racedDigest.digest
        && poisonedCacheResult.hashedFiles == metadataFileCount
        && poisonedCacheResult.reusedFiles == 0,
        "well-formed persisted wrong digest cannot bypass first-use content revalidation");
    check(writePersistentCache(persistentCachePath, persistentRootIdentity, digestCache),
        "race-corrected in-process cache can be atomically persisted for later hint loading");

    const QJsonObject validPersistentObject = QJsonDocument::fromJson(
        validPersistentCacheBytes).object();
    QJsonObject wrongVersionObject = validPersistentObject;
    wrongVersionObject.insert(QStringLiteral("schema"), 2);
    const QString wrongVersionPath = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("wrong-version.json"));
    loadedPersistentCache = digestCache;
    check(writeFile(wrongVersionPath,
        QJsonDocument(wrongVersionObject).toJson(QJsonDocument::Compact))
        && !loadPersistentCache(wrongVersionPath, persistentRootIdentity,
            loadedPersistentCache) && loadedPersistentCache.isEmpty(),
        "unknown persistent-cache schema is refused without retaining stale entries");

    QJsonObject unknownTopLevelObject = validPersistentObject;
    unknownTopLevelObject.insert(QStringLiteral("unexpected"), true);
    const QString unknownTopLevelPath = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("unknown-top-level.json"));
    check(writeFile(unknownTopLevelPath,
        QJsonDocument(unknownTopLevelObject).toJson(QJsonDocument::Compact))
        && !loadPersistentCache(unknownTopLevelPath, persistentRootIdentity,
            loadedPersistentCache) && loadedPersistentCache.isEmpty(),
        "unknown top-level persistent-cache field is refused");

    QJsonObject unknownEntryObject = validPersistentObject;
    QJsonArray unknownEntryArray = unknownEntryObject.value(
        QStringLiteral("entries")).toArray();
    QJsonObject unknownEntry = unknownEntryArray.at(0).toObject();
    unknownEntry.insert(QStringLiteral("unexpected"), true);
    unknownEntryArray.replace(0, unknownEntry);
    unknownEntryObject.insert(QStringLiteral("entries"), unknownEntryArray);
    const QString unknownEntryPath = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("unknown-entry.json"));
    check(writeFile(unknownEntryPath,
        QJsonDocument(unknownEntryObject).toJson(QJsonDocument::Compact))
        && !loadPersistentCache(unknownEntryPath, persistentRootIdentity,
            loadedPersistentCache) && loadedPersistentCache.isEmpty(),
        "unknown per-entry persistent-cache field is refused");

    const QString truncatedCachePath = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("truncated.json"));
    check(writeFile(truncatedCachePath,
        validPersistentCacheBytes.left(validPersistentCacheBytes.size() / 2))
        && !loadPersistentCache(truncatedCachePath, persistentRootIdentity,
            loadedPersistentCache) && loadedPersistentCache.isEmpty(),
        "truncated persistent cache is refused without partial entry reuse");

    QJsonObject malformedDigestObject = validPersistentObject;
    QJsonArray malformedDigestArray = malformedDigestObject.value(
        QStringLiteral("entries")).toArray();
    QJsonObject malformedDigestEntry = malformedDigestArray.at(0).toObject();
    malformedDigestEntry.insert(QStringLiteral("contentSha256"), QStringLiteral("not-a-digest"));
    malformedDigestArray.replace(0, malformedDigestEntry);
    malformedDigestObject.insert(QStringLiteral("entries"), malformedDigestArray);
    const QString malformedDigestPath = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("malformed-digest.json"));
    check(writeFile(malformedDigestPath,
        QJsonDocument(malformedDigestObject).toJson(QJsonDocument::Compact))
        && !loadPersistentCache(malformedDigestPath, persistentRootIdentity,
            loadedPersistentCache) && loadedPersistentCache.isEmpty(),
        "malformed persistent content digest is refused");

    QJsonObject duplicatePathObject = validPersistentObject;
    QJsonArray duplicatePathArray = duplicatePathObject.value(
        QStringLiteral("entries")).toArray();
    duplicatePathArray.append(duplicatePathArray.at(0));
    duplicatePathObject.insert(QStringLiteral("entries"), duplicatePathArray);
    const QString duplicatePathCache = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("duplicate-path.json"));
    check(writeFile(duplicatePathCache,
        QJsonDocument(duplicatePathObject).toJson(QJsonDocument::Compact))
        && !loadPersistentCache(duplicatePathCache, persistentRootIdentity,
            loadedPersistentCache) && loadedPersistentCache.isEmpty(),
        "duplicate persistent-cache path is refused");

    QJsonObject traversalCacheObject = validPersistentObject;
    QJsonArray traversalCacheArray = traversalCacheObject.value(
        QStringLiteral("entries")).toArray();
    QJsonObject traversalCacheEntry = traversalCacheArray.at(0).toObject();
    traversalCacheEntry.insert(QStringLiteral("path"), QStringLiteral("../outside.bin"));
    traversalCacheArray.replace(0, traversalCacheEntry);
    traversalCacheObject.insert(QStringLiteral("entries"), traversalCacheArray);
    const QString traversalCachePath = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("traversal-path.json"));
    check(writeFile(traversalCachePath,
        QJsonDocument(traversalCacheObject).toJson(QJsonDocument::Compact))
        && !loadPersistentCache(traversalCachePath, persistentRootIdentity,
            loadedPersistentCache) && loadedPersistentCache.isEmpty(),
        "out-of-root persistent-cache path is refused");

    loadedPersistentCache = digestCache;
    check(!loadPersistentCache(persistentCachePath, QByteArray(64, 'f'),
        loadedPersistentCache) && loadedPersistentCache.isEmpty(),
        "cache for a different root identity is never reused");

    const QString cacheLinkTarget = QDir(cacheSignalDirectory).filePath(
        QStringLiteral("cache-link-target"));
    const QString cacheLink = QDir(persistentCacheDirectory).filePath(
        QStringLiteral("cache-link.json"));
    check(writeFile(cacheLinkTarget, "CACHE LINK TARGET\n")
        && QFile::link(cacheLinkTarget, cacheLink)
        && !loadPersistentCache(cacheLink, persistentRootIdentity, loadedPersistentCache)
        && !writePersistentCache(cacheLink, persistentRootIdentity, digestCache)
        && readFile(cacheLinkTarget) == "CACHE LINK TARGET\n",
        "persistent cache read and write both refuse a replacement symbolic link");

    const QString activeCacheReady = QDir(cacheSignalDirectory).filePath(
        QStringLiteral("active-cache-ready"));
    const QString activeCacheRelease = QDir(cacheSignalDirectory).filePath(
        QStringLiteral("active-cache-release"));
    QProcess activeCacheWriter;
    activeCacheWriter.start(application.applicationFilePath(),
        {QStringLiteral("--hold-cache-lock"), persistentCachePath,
            activeCacheReady, activeCacheRelease});
    bool activeCacheReached = activeCacheWriter.waitForStarted(3000);
    QElapsedTimer activeCacheTimer;
    activeCacheTimer.start();
    while (activeCacheReached && !QFileInfo::exists(activeCacheReady)
        && activeCacheTimer.elapsed() < 3000) {
        activeCacheWriter.waitForReadyRead(10);
    }
    activeCacheReached = activeCacheReached && QFileInfo::exists(activeCacheReady);
    check(activeCacheReached,
        "separate cache writer acquires the deterministic persistent-cache lock");
    const QByteArray cacheBeforeContention = readFile(persistentCachePath);
    check(activeCacheReached
        && !loadPersistentCache(persistentCachePath, persistentRootIdentity,
            loadedPersistentCache)
        && !writePersistentCache(persistentCachePath, persistentRootIdentity, digestCache)
        && readFile(persistentCachePath) == cacheBeforeContention,
        "reader and second writer both refuse an actively locked persistent cache");
    check(writeFile(activeCacheRelease, "RELEASE")
        && activeCacheWriter.waitForFinished(3000)
        && activeCacheWriter.exitStatus() == QProcess::NormalExit
        && activeCacheWriter.exitCode() == 0
        && loadPersistentCache(persistentCachePath, persistentRootIdentity,
            loadedPersistentCache),
        "persistent cache becomes readable after the active writer releases its lock");

    const QString stagedCacheReady = QDir(cacheSignalDirectory).filePath(
        QStringLiteral("staged-cache-ready"));
    const QByteArray cacheBeforeInterruptedCommit = readFile(persistentCachePath);
    QProcess interruptedCacheWriter;
    interruptedCacheWriter.start(application.applicationFilePath(),
        {QStringLiteral("--stage-cache-and-hold"), persistentCachePath,
            stagedCacheReady, QStringLiteral("UNCOMMITTED REPLACEMENT")});
    bool stagedCacheReached = interruptedCacheWriter.waitForStarted(3000);
    QElapsedTimer stagedCacheTimer;
    stagedCacheTimer.start();
    while (stagedCacheReached && !QFileInfo::exists(stagedCacheReady)
        && stagedCacheTimer.elapsed() < 3000) {
        interruptedCacheWriter.waitForReadyRead(10);
    }
    stagedCacheReached = stagedCacheReached && QFileInfo::exists(stagedCacheReady);
    if (stagedCacheReached)
        interruptedCacheWriter.kill();
    const bool stagedCacheKilled = stagedCacheReached
        && interruptedCacheWriter.waitForFinished(3000)
        && interruptedCacheWriter.exitStatus() == QProcess::CrashExit;
    check(stagedCacheKilled
        && readFile(persistentCachePath) == cacheBeforeInterruptedCommit
        && QFileInfo::exists(cacheLockPathFor(persistentCachePath)),
        "SIGKILL before atomic cache commit preserves the previous target and leaves an auditable lock");
    loadedPersistentCache.clear();
    check(stagedCacheKilled
        && !loadPersistentCache(persistentCachePath, persistentRootIdentity,
            loadedPersistentCache)
        && loadedPersistentCache.isEmpty()
        && readFile(persistentCachePath) == cacheBeforeInterruptedCommit,
        "default cache load refuses a dead-writer lock without discarding the valid old target");
    QLockFile deadCacheLock(cacheLockPathFor(persistentCachePath));
    deadCacheLock.setStaleLockTime(0);
    check(stagedCacheKilled && sameHostDeadOwner(deadCacheLock)
        && deadCacheLock.removeStaleLockFile()
        && loadPersistentCache(persistentCachePath, persistentRootIdentity,
            loadedPersistentCache)
        && sameDigestCache(digestCache, loadedPersistentCache),
        "explicit same-host dead-owner confirmation restores access to the intact old cache");

    QHash<QString, CachedFileDigest> updatedPersistentCache = digestCache;
    const QString updatedPath = updatedPersistentCache.keys().constFirst();
    updatedPersistentCache[updatedPath].fingerprint = QCryptographicHash::hash(
        QByteArrayLiteral("updated-fingerprint"), QCryptographicHash::Sha256).toHex();
    check(writePersistentCache(persistentCachePath, persistentRootIdentity,
        updatedPersistentCache)
        && loadPersistentCache(persistentCachePath, persistentRootIdentity,
            loadedPersistentCache)
        && sameDigestCache(updatedPersistentCache, loadedPersistentCache)
        && readFile(persistentCachePath) != cacheBeforeInterruptedCommit,
        "successful atomic cache update replaces the complete versioned document");

    Fixture prepared;
    check(prepareFixture(temporary.path(), QStringLiteral("prepared"), true, prepared),
        "prepared-state fixture created");
    check(recover(prepared.stage) == RecoveryResult::RolledBack && oldPair(prepared)
        && !QFileInfo::exists(prepared.stage),
        "crash before live renames keeps the old pair and removes private staging");

    Fixture oldMoved;
    check(prepareFixture(temporary.path(), QStringLiteral("old-moved"), true, oldMoved)
        && QDir().rename(oldMoved.resources,
            QDir(oldMoved.stage).filePath(QStringLiteral("previous-resources"))),
        "old-resources-moved fixture created");
    check(recover(oldMoved.stage) == RecoveryResult::RolledBack && oldPair(oldMoved)
        && !QFileInfo::exists(oldMoved.stage),
        "restart restores the old resource directory when the live path is empty");

    Fixture newInstalled;
    check(prepareFixture(temporary.path(), QStringLiteral("new-installed"), true, newInstalled)
        && QDir().rename(newInstalled.resources,
            QDir(newInstalled.stage).filePath(QStringLiteral("previous-resources")))
        && QDir().rename(newInstalled.stagedResources, newInstalled.resources),
        "new-resources-installed fixture created");
    check(recover(newInstalled.stage) == RecoveryResult::RolledBack && oldPair(newInstalled)
        && !QFileInfo::exists(newInstalled.stage),
        "old Markdown plus known new resources rolls back to the complete old pair");

    Fixture rollbackMovedNew;
    check(prepareFixture(temporary.path(), QStringLiteral("rollback-moved-new"), true, rollbackMovedNew)
        && QDir().rename(rollbackMovedNew.resources,
            QDir(rollbackMovedNew.stage).filePath(QStringLiteral("previous-resources")))
        && QDir().rename(rollbackMovedNew.stagedResources, rollbackMovedNew.resources)
        && QDir().rename(rollbackMovedNew.resources,
            QDir(rollbackMovedNew.stage).filePath(QStringLiteral("returned-resources"))),
        "interrupted rollback after moving new resources fixture created");
    check(recover(rollbackMovedNew.stage) == RecoveryResult::RolledBack && oldPair(rollbackMovedNew)
        && !QFileInfo::exists(rollbackMovedNew.stage),
        "recovery resumes rollback after new resources were already moved aside");

    Fixture rollbackRestoredOld;
    check(prepareFixture(temporary.path(), QStringLiteral("rollback-restored-old"), true, rollbackRestoredOld)
        && QDir().rename(rollbackRestoredOld.resources,
            QDir(rollbackRestoredOld.stage).filePath(QStringLiteral("previous-resources")))
        && QDir().rename(rollbackRestoredOld.stagedResources, rollbackRestoredOld.resources)
        && QDir().rename(rollbackRestoredOld.resources,
            QDir(rollbackRestoredOld.stage).filePath(QStringLiteral("returned-resources")))
        && QDir().rename(QDir(rollbackRestoredOld.stage).filePath(
            QStringLiteral("previous-resources")), rollbackRestoredOld.resources),
        "interrupted rollback after restoring old resources fixture created");
    check(recover(rollbackRestoredOld.stage) == RecoveryResult::RolledBack
        && oldPair(rollbackRestoredOld) && !QFileInfo::exists(rollbackRestoredOld.stage),
        "recovery recognizes an already restored old pair and completes cleanup");

    Fixture tamperedReturned;
    check(prepareFixture(temporary.path(), QStringLiteral("tampered-returned"), true, tamperedReturned)
        && QDir().rename(tamperedReturned.resources,
            QDir(tamperedReturned.stage).filePath(QStringLiteral("previous-resources")))
        && QDir().rename(tamperedReturned.stagedResources, tamperedReturned.resources)
        && QDir().rename(tamperedReturned.resources,
            QDir(tamperedReturned.stage).filePath(QStringLiteral("returned-resources")))
        && writeFile(QDir(tamperedReturned.stage).filePath(
            QStringLiteral("returned-resources/new.bin")), "EXTERNAL RETURNED RESOURCE"),
        "tampered returned-resource fixture created");
    check(recover(tamperedReturned.stage) == RecoveryResult::Refused
        && readFile(tamperedReturned.destination) == "OLD MARKDOWN\n"
        && readFile(QDir(tamperedReturned.stage).filePath(
            QStringLiteral("previous-resources/old.bin"))) == "OLD RESOURCE",
        "recovery refuses an unknown interrupted-rollback directory and retains the old backup");

    Fixture committed;
    check(prepareFixture(temporary.path(), QStringLiteral("committed"), true, committed)
        && QDir().rename(committed.resources,
            QDir(committed.stage).filePath(QStringLiteral("previous-resources")))
        && QDir().rename(committed.stagedResources, committed.resources)
        && writeFile(committed.destination, "NEW MARKDOWN\n"),
        "committed-state fixture created");
    check(recover(committed.stage) == RecoveryResult::Finalized && newPair(committed)
        && !QFileInfo::exists(committed.stage),
        "known new Markdown plus known new resources finalizes without reverting");

    Fixture externalMarkdown;
    check(prepareFixture(temporary.path(), QStringLiteral("external-markdown"), true, externalMarkdown)
        && QDir().rename(externalMarkdown.resources,
            QDir(externalMarkdown.stage).filePath(QStringLiteral("previous-resources")))
        && QDir().rename(externalMarkdown.stagedResources, externalMarkdown.resources)
        && writeFile(externalMarkdown.destination, "EXTERNAL MARKDOWN\n"),
        "unexpected-Markdown fixture created");
    check(recover(externalMarkdown.stage) == RecoveryResult::Refused
        && readFile(externalMarkdown.destination) == "EXTERNAL MARKDOWN\n"
        && readFile(QDir(externalMarkdown.resources).filePath(QStringLiteral("new.bin"))) == "NEW RESOURCE"
        && readFile(QDir(externalMarkdown.stage).filePath(
            QStringLiteral("previous-resources/old.bin"))) == "OLD RESOURCE",
        "recovery refuses unknown Markdown and retains both live and backup evidence");

    Fixture externalResources;
    check(prepareFixture(temporary.path(), QStringLiteral("external-resources"), true, externalResources)
        && QDir().rename(externalResources.resources,
            QDir(externalResources.stage).filePath(QStringLiteral("previous-resources")))
        && QDir().rename(externalResources.stagedResources, externalResources.resources)
        && writeFile(QDir(externalResources.resources).filePath(QStringLiteral("new.bin")), "EXTERNAL RESOURCE"),
        "unexpected-resource fixture created");
    check(recover(externalResources.stage) == RecoveryResult::Refused
        && readFile(externalResources.destination) == "OLD MARKDOWN\n"
        && readFile(QDir(externalResources.resources).filePath(QStringLiteral("new.bin"))) == "EXTERNAL RESOURCE"
        && QFileInfo::exists(QDir(externalResources.stage).filePath(QStringLiteral("previous-resources"))),
        "recovery refuses unknown live resources without overwriting either side");

    Fixture cachedAuthorization;
    QHash<QString, CachedFileDigest> authorizationHints;
    QHash<QString, CachedFileDigest> loadedAuthorizationHints;
    const bool authorizationPrepared = prepareFixture(temporary.path(),
        QStringLiteral("cached-authorization"), true, cachedAuthorization);
    const QString authorizationResource = QDir(cachedAuthorization.resources).filePath(
        QStringLiteral("old.bin"));
    const QString authorizationCache = QDir(cachedAuthorization.parent).filePath(
        QStringLiteral("resource-digests.json"));
    const QByteArray authorizationRootIdentity = authorizationPrepared
        ? QCryptographicHash::hash(QFile::encodeName(
            QDir(cachedAuthorization.resources).canonicalPath()),
            QCryptographicHash::Sha256).toHex()
        : QByteArray();
    if (authorizationPrepared) {
        authorizationHints.insert(QStringLiteral("old.bin"),
            {cacheFingerprint(authorizationResource), fileDigest(authorizationResource)});
    }
    check(authorizationPrepared
        && writePersistentCache(authorizationCache, authorizationRootIdentity,
            authorizationHints)
        && loadPersistentCache(authorizationCache, authorizationRootIdentity,
            loadedAuthorizationHints)
        && sameDigestCache(authorizationHints, loadedAuthorizationHints),
        "recovery authorization fixture has a structurally valid matching cache hint");
    check(writeFile(authorizationResource, "CHANGED RESOURCE")
        && recover(cachedAuthorization.stage) == RecoveryResult::Refused
        && readFile(authorizationResource) == "CHANGED RESOURCE"
        && QFileInfo::exists(cachedAuthorization.stage),
        "valid stale cache hint cannot authorize recovery over changed live resources");

    Fixture firstPending;
    check(prepareFixture(temporary.path(), QStringLiteral("first-pending"), false, firstPending)
        && QDir().rename(firstPending.stagedResources, firstPending.resources),
        "first-export pending fixture created");
    check(recover(firstPending.stage) == RecoveryResult::RolledBack
        && !QFileInfo::exists(firstPending.destination) && !QFileInfo::exists(firstPending.resources)
        && !QFileInfo::exists(firstPending.stage),
        "first export before Markdown commit rolls new resources back to no output");

    Fixture firstCommitted;
    check(prepareFixture(temporary.path(), QStringLiteral("first-committed"), false, firstCommitted)
        && QDir().rename(firstCommitted.stagedResources, firstCommitted.resources)
        && writeFile(firstCommitted.destination, "NEW MARKDOWN\n"),
        "first-export committed fixture created");
    check(recover(firstCommitted.stage) == RecoveryResult::Finalized && newPair(firstCommitted)
        && !QFileInfo::exists(firstCommitted.stage),
        "first export with committed Markdown finalizes the new pair");

    Fixture noResourcesPending;
    check(prepareFixture(temporary.path(), QStringLiteral("no-resources-pending"), false,
        noResourcesPending)
        && QDir(noResourcesPending.stagedResources).removeRecursively()
        && writeManifest(noResourcesPending.stage, QStringLiteral("note.md"),
            QStringLiteral("note_files"), false, {}, noResourcesPending.newMarkdownDigest,
            false, {}, false, {}),
        "first export without a resource directory fixture created");
    check(recover(noResourcesPending.stage) == RecoveryResult::RolledBack
        && !QFileInfo::exists(noResourcesPending.destination)
        && !QFileInfo::exists(noResourcesPending.resources)
        && !QFileInfo::exists(noResourcesPending.stage),
        "resource-free first export before Markdown commit rolls back to no output");

    Fixture noResourcesCommitted;
    check(prepareFixture(temporary.path(), QStringLiteral("no-resources-committed"), false,
        noResourcesCommitted)
        && QDir(noResourcesCommitted.stagedResources).removeRecursively()
        && writeManifest(noResourcesCommitted.stage, QStringLiteral("note.md"),
            QStringLiteral("note_files"), false, {}, noResourcesCommitted.newMarkdownDigest,
            false, {}, false, {})
        && writeFile(noResourcesCommitted.destination, "NEW MARKDOWN\n"),
        "committed resource-free first export fixture created");
    check(recover(noResourcesCommitted.stage) == RecoveryResult::Finalized
        && readFile(noResourcesCommitted.destination) == "NEW MARKDOWN\n"
        && !QFileInfo::exists(noResourcesCommitted.resources)
        && !QFileInfo::exists(noResourcesCommitted.stage),
        "resource-free first export with committed Markdown finalizes correctly");

    Fixture malformed;
    check(prepareFixture(temporary.path(), QStringLiteral("malformed"), true, malformed)
        && writeFile(QDir(malformed.stage).filePath(QStringLiteral("transaction.json")), "not-json"),
        "malformed-manifest fixture created");
    check(recover(malformed.stage) == RecoveryResult::Refused && oldPair(malformed)
        && QFileInfo::exists(malformed.stage),
        "malformed manifest is retained and never changes the live export");

    Fixture traversal;
    check(prepareFixture(temporary.path(), QStringLiteral("traversal"), true, traversal)
        && writeManifest(traversal.stage, QStringLiteral("../outside.md"), QStringLiteral("note_files"),
            true, traversal.oldMarkdownDigest, traversal.newMarkdownDigest,
            true, traversal.oldResourcesDigest, true, traversal.newResourcesDigest),
        "path-traversal manifest fixture created");
    check(recover(traversal.stage) == RecoveryResult::Refused && oldPair(traversal)
        && QFileInfo::exists(traversal.stage),
        "manifest path traversal is refused without touching the parent");

    Fixture symbolicDestination;
    const QString externalTarget = QDir(temporary.path()).filePath(QStringLiteral("external-target.md"));
    check(prepareFixture(temporary.path(), QStringLiteral("symbolic-destination"), true,
        symbolicDestination)
        && QFile::remove(symbolicDestination.destination)
        && writeFile(externalTarget, "EXTERNAL TARGET\n")
        && QFile::link(externalTarget, symbolicDestination.destination),
        "symbolic destination recovery fixture created");
    check(recover(symbolicDestination.stage) == RecoveryResult::Refused
        && QFileInfo(symbolicDestination.destination).isSymLink()
        && readFile(externalTarget) == "EXTERNAL TARGET\n"
        && QFileInfo::exists(symbolicDestination.stage),
        "recovery refuses a replaced destination symlink without following it");

    Fixture busy;
    check(prepareFixture(temporary.path(), QStringLiteral("busy"), true, busy),
        "active-writer fixture created");
    const QString readyPath = QDir(busy.parent).filePath(QStringLiteral("writer-ready"));
    const QString releasePath = QDir(busy.parent).filePath(QStringLiteral("writer-release"));
    QProcess activeWriter;
    activeWriter.start(application.applicationFilePath(),
        {QStringLiteral("--hold-lock"), busy.destination, readyPath, releasePath});
    bool writerReady = activeWriter.waitForStarted(3000);
    QElapsedTimer readyTimer;
    readyTimer.start();
    while (writerReady && !QFileInfo::exists(readyPath) && readyTimer.elapsed() < 3000) {
        activeWriter.waitForReadyRead(10);
    }
    writerReady = writerReady && QFileInfo::exists(readyPath);
    check(writerReady, "separate writer process acquires the deterministic destination lock");
    QLockFile secondLock(lockPathFor(busy.destination));
    secondLock.setStaleLockTime(0);
    check(!secondLock.tryLock(0), "second writer cannot acquire the same destination lock");
    check(recover(busy.stage) == RecoveryResult::Busy && oldPair(busy)
        && QFileInfo::exists(busy.stage),
        "restart recovery also refuses while an active writer holds the destination lock");
    check(recover(busy.stage, true) == RecoveryResult::Busy && oldPair(busy)
        && QFileInfo::exists(busy.stage),
        "confirmed recovery cannot take over a still-running same-host writer");
    check(writeFile(releasePath, "RELEASE") && activeWriter.waitForFinished(3000)
        && activeWriter.exitStatus() == QProcess::NormalExit && activeWriter.exitCode() == 0,
        "separate writer process releases the destination lock cleanly");
    check(recover(busy.stage) == RecoveryResult::RolledBack && oldPair(busy)
        && !QFileInfo::exists(busy.stage),
        "recovery proceeds after the active destination lock is released");

    const QString otherDestination = QDir(temporary.path()).filePath(QStringLiteral("other/note.md"));
    check(QDir().mkpath(QFileInfo(otherDestination).absolutePath()),
        "independent-destination lock fixture created");
    QLockFile otherLock(lockPathFor(otherDestination));
    otherLock.setStaleLockTime(0);
    check(otherLock.tryLock(0), "different destinations use independent locks");

    Fixture deterministic;
    check(prepareFixture(temporary.path(), QStringLiteral("deterministic"), true, deterministic)
        && deterministic.stage == recoveryPathFor(deterministic.destination)
        && recoveryPathFor(QDir(deterministic.parent).filePath(QStringLiteral("other.md")))
            != deterministic.stage,
        "on-demand recovery resolves one deterministic sibling directory per destination");
    check(recover(deterministic.stage) == RecoveryResult::RolledBack && oldPair(deterministic),
        "deterministic on-demand recovery does not require a broad startup directory scan");

    Fixture linkedStage;
    const QString linkedStageTarget = QDir(temporary.path()).filePath(QStringLiteral("linked-stage-target"));
    check(prepareFixture(temporary.path(), QStringLiteral("linked-stage"), true, linkedStage)
        && QDir(linkedStage.stage).removeRecursively() && QDir().mkpath(linkedStageTarget)
        && QFile::link(linkedStageTarget, linkedStage.stage),
        "replacement recovery-directory symlink fixture created");
    check(recover(linkedStage.stage, true) == RecoveryResult::Refused
        && QFileInfo(linkedStage.stage).isSymLink() && oldPair(linkedStage),
        "on-demand recovery refuses a replaced recovery-directory symlink");

    const QString controlDirectory = QDir(temporary.path()).filePath(QStringLiteral("control"));
    const bool controlReady = QDir().mkpath(controlDirectory);
    auto crashAt = [&](const QString& name, const QString& mode, const QString& checkpoint,
                       RecoveryResult expected, bool expectNew) {
        Fixture fixture;
        const bool created = controlReady
            && prepareFixture(temporary.path(), name, true, fixture);
        const QString ready = QDir(controlDirectory).filePath(name + QStringLiteral("-ready"));
        QProcess writer;
        if (created) {
            writer.start(application.applicationFilePath(),
                {mode, fixture.stage, checkpoint, ready});
        }
        bool reached = created && writer.waitForStarted(3000);
        QElapsedTimer timer;
        timer.start();
        while (reached && !QFileInfo::exists(ready) && timer.elapsed() < 3000)
            writer.waitForReadyRead(10);
        reached = reached && QFileInfo::exists(ready);
        check(reached, qPrintable(QStringLiteral("forced-termination checkpoint reached: ") + checkpoint));
        if (reached)
            writer.kill();
        const bool killed = reached && writer.waitForFinished(3000)
            && writer.exitStatus() == QProcess::CrashExit
            && QFileInfo::exists(lockPathFor(fixture.destination));
        check(killed, qPrintable(QStringLiteral("SIGKILL leaves an auditable destination lock: ") + checkpoint));
        check(killed && recover(fixture.stage) == RecoveryResult::Busy
            && QFileInfo::exists(fixture.stage),
            qPrintable(QStringLiteral("default recovery never silently takes over a dead lock: ") + checkpoint));
        const RecoveryResult result = killed ? recover(fixture.stage, true) : RecoveryResult::Refused;
        check(result == expected && (expectNew ? newPair(fixture) : oldPair(fixture))
            && !QFileInfo::exists(fixture.stage) && !QFileInfo::exists(lockPathFor(fixture.destination)),
            qPrintable(QStringLiteral("confirmed same-host dead owner recovers exact state: ") + checkpoint));
    };
    crashAt(QStringLiteral("crash-old-moved"), QStringLiteral("--advance-and-hold"),
        QStringLiteral("old-moved"),
        RecoveryResult::RolledBack, false);
    crashAt(QStringLiteral("crash-new-installed"), QStringLiteral("--advance-and-hold"),
        QStringLiteral("new-installed"),
        RecoveryResult::RolledBack, false);
    crashAt(QStringLiteral("crash-markdown-committed"), QStringLiteral("--advance-and-hold"),
        QStringLiteral("markdown-committed"),
        RecoveryResult::Finalized, true);
    crashAt(QStringLiteral("crash-old-before-sync"),
        QStringLiteral("--advance-unsynced-and-hold"),
        QStringLiteral("old-renamed-before-sync"), RecoveryResult::RolledBack, false);
    crashAt(QStringLiteral("crash-new-before-sync"),
        QStringLiteral("--advance-unsynced-and-hold"),
        QStringLiteral("new-renamed-before-sync"), RecoveryResult::RolledBack, false);
    crashAt(QStringLiteral("crash-markdown-before-sync"),
        QStringLiteral("--advance-unsynced-and-hold"),
        QStringLiteral("markdown-replaced-before-sync"),
        RecoveryResult::Finalized, true);

    Fixture policyFixture;
    const bool policyCreated = prepareFixture(temporary.path(),
        QStringLiteral("recovery-policy"), true, policyFixture);
    const QString policyReady = QDir(controlDirectory).filePath(
        QStringLiteral("recovery-policy-ready"));
    QProcess policyWriter;
    if (policyCreated) {
        policyWriter.start(application.applicationFilePath(),
            {QStringLiteral("--advance-and-hold"), policyFixture.stage,
                QStringLiteral("old-moved"), policyReady});
    }
    bool policyReached = policyCreated && policyWriter.waitForStarted(3000);
    QElapsedTimer policyReadyTimer;
    policyReadyTimer.start();
    while (policyReached && !QFileInfo::exists(policyReady)
        && policyReadyTimer.elapsed() < 3000) {
        policyWriter.waitForReadyRead(10);
    }
    policyReached = policyReached && QFileInfo::exists(policyReady);
    if (policyReached)
        policyWriter.kill();
    const bool policyKilled = policyReached && policyWriter.waitForFinished(3000)
        && policyWriter.exitStatus() == QProcess::CrashExit
        && QFileInfo::exists(lockPathFor(policyFixture.destination));
    check(policyKilled,
        "recovery decision fixture retains a real dead-writer lock and staged evidence");

    const QString policyAudit = QDir(controlDirectory).filePath(
        QStringLiteral("recovery-policy.jsonl"));
    const QByteArray policyBefore = policyKilled
        ? metadataDirectoryDigest(policyFixture.parent) : QByteArray();
    const PolicyResult inspectResult = policyKilled
        ? applyRecoveryDecision(policyFixture.stage, RecoveryDecision::Inspect, policyAudit)
        : PolicyResult::AuditFailure;
    check(inspectResult == PolicyResult::AwaitingConfirmation
        && metadataDirectoryDigest(policyFixture.parent) == policyBefore,
        "inspect records an external audit event without changing recovery evidence");
    const PolicyResult cancelResult = policyKilled
        ? applyRecoveryDecision(policyFixture.stage, RecoveryDecision::Cancel, policyAudit)
        : PolicyResult::AuditFailure;
    check(cancelResult == PolicyResult::Cancelled
        && metadataDirectoryDigest(policyFixture.parent) == policyBefore,
        "cancel leaves the dead lock, manifest, old Markdown and backup untouched");
    const PolicyResult retainResult = policyKilled
        ? applyRecoveryDecision(policyFixture.stage, RecoveryDecision::Retain, policyAudit)
        : PolicyResult::AuditFailure;
    check(retainResult == PolicyResult::Retained
        && metadataDirectoryDigest(policyFixture.parent) == policyBefore,
        "explicit retain leaves the entire recovery site unchanged");
    const PolicyResult confirmResult = policyKilled
        ? applyRecoveryDecision(policyFixture.stage, RecoveryDecision::Confirm, policyAudit)
        : PolicyResult::AuditFailure;
    check(confirmResult == PolicyResult::RolledBack && oldPair(policyFixture)
        && !QFileInfo::exists(policyFixture.stage)
        && !QFileInfo::exists(lockPathFor(policyFixture.destination)),
        "confirm writes intent first, then recovers the exact old pair and records completion");

    QList<QByteArray> policyAuditLines = readFile(policyAudit).split('\n');
    if (!policyAuditLines.isEmpty() && policyAuditLines.constLast().isEmpty())
        policyAuditLines.removeLast();
    const QByteArray policyAuditBytes = readFile(policyAudit);
    check(policyAuditLines.size() == 5
        && policyAuditBytes.contains("\"decision\":\"inspect\"")
        && policyAuditBytes.contains("\"decision\":\"cancel\"")
        && policyAuditBytes.contains("\"decision\":\"retain\"")
        && policyAuditBytes.contains("\"event\":\"requested\"")
        && policyAuditBytes.contains("\"event\":\"rolled-back\""),
        "audit log distinguishes inspection, cancellation, retention, request and result");
    check(policyAuditBytes.contains(identityDigest(policyFixture.destination))
        && !policyAuditBytes.contains(policyFixture.parent.toUtf8())
        && !policyAuditBytes.contains("OLD MARKDOWN")
        && !policyAuditBytes.contains("OLD RESOURCE"),
        "audit records only a destination identity and decision outcome, not paths or note content");

    Fixture blockedAuditFixture;
    const QString blockedAuditTarget = QDir(controlDirectory).filePath(
        QStringLiteral("blocked-audit-target"));
    const QString blockedAuditLink = QDir(controlDirectory).filePath(
        QStringLiteral("blocked-audit-link"));
    check(prepareFixture(temporary.path(), QStringLiteral("blocked-audit"), true,
        blockedAuditFixture)
        && writeFile(blockedAuditTarget, "AUDIT TARGET\n")
        && QFile::link(blockedAuditTarget, blockedAuditLink),
        "replacement audit-log symlink fixture created");
    check(applyRecoveryDecision(blockedAuditFixture.stage, RecoveryDecision::Confirm,
        blockedAuditLink) == PolicyResult::AuditFailure
        && oldPair(blockedAuditFixture)
        && QFileInfo::exists(blockedAuditFixture.stage)
        && readFile(blockedAuditTarget) == "AUDIT TARGET\n",
        "failed pre-recovery audit refuses confirmation and never follows the replacement symlink");

    const QFileDevice::Permissions sharedPermissions =
        QFileDevice::ReadGroup | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    auto privateAuditFile = [&](const QString& path) {
        const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        return QFileInfo(path).isFile()
            && permissions.testFlag(QFileDevice::ReadOwner)
            && permissions.testFlag(QFileDevice::WriteOwner)
            && !(permissions & sharedPermissions);
    };
    auto privateAuditDirectory = [&](const QString& path) {
        const QFileDevice::Permissions permissions = QFileInfo(path).permissions();
        return QFileInfo(path).isDir()
            && permissions.testFlag(QFileDevice::ReadOwner)
            && permissions.testFlag(QFileDevice::WriteOwner)
            && permissions.testFlag(QFileDevice::ExeOwner)
            && !(permissions & sharedPermissions);
    };
    check(privateAuditDirectory(QFileInfo(policyAudit).absolutePath())
        && privateAuditFile(policyAudit),
        "recovery audit uses a private owner-only directory and file");

    const QString boundedAuditDirectory = QDir(temporary.path()).filePath(
        QStringLiteral("bounded-audit"));
    const QString boundedAudit = QDir(boundedAuditDirectory).filePath(
        QStringLiteral("recovery.jsonl"));
    const QString boundedIdentity = QString::fromLatin1(identityDigest(
        QDir(temporary.path()).filePath(QStringLiteral("bounded-note.md"))));
    bool boundedAuditWritten = true;
    for (int eventIndex = 0; eventIndex < 10; ++eventIndex) {
        boundedAuditWritten = boundedAuditWritten
            && appendAuditEvent(boundedAudit, boundedIdentity,
                RecoveryDecision::Retain, QStringLiteral("retained"), 4096, 3, 2);
    }
    const QString boundedArchive1 = boundedAudit + QStringLiteral(".1");
    const QString boundedArchive2 = boundedAudit + QStringLiteral(".2");
    auto auditLineCount = [](const QString& path) {
        return readFile(path).count('\n');
    };
    check(boundedAuditWritten && validAuditBytes(readFile(boundedAudit))
        && validAuditBytes(readFile(boundedArchive1))
        && validAuditBytes(readFile(boundedArchive2))
        && auditLineCount(boundedAudit) == 1
        && auditLineCount(boundedArchive1) == 3
        && auditLineCount(boundedArchive2) == 3,
        "bounded audit rotates at three entries and retains exactly two complete archives");
    check(privateAuditDirectory(boundedAuditDirectory)
        && privateAuditFile(boundedAudit)
        && privateAuditFile(boundedArchive1)
        && privateAuditFile(boundedArchive2),
        "current and rotated audit segments retain owner-only permissions");
    const QByteArray boundedAuditBytes = readFile(boundedAudit)
        + readFile(boundedArchive1) + readFile(boundedArchive2);
    check(!boundedAuditBytes.contains(temporary.path().toUtf8())
        && !boundedAuditBytes.contains("MARKDOWN")
        && !boundedAuditBytes.contains("RESOURCE")
        && boundedAuditBytes.count(boundedIdentity.toLatin1()) == 7,
        "rotation preserves data minimization and stores only the derived identity");

    auto auditTestIdentity = [](int sequence) {
        return QStringLiteral("%1").arg(sequence, 24, 16, QLatin1Char('0'));
    };
    auto auditIdentitySequence = [](const QString& livePath) {
        QStringList identities;
        const QStringList paths{livePath + QStringLiteral(".2"),
            livePath + QStringLiteral(".1"), livePath};
        for (const QString& path : paths) {
            if (!QFileInfo::exists(path))
                continue;
            QList<QByteArray> lines = readFile(path).split('\n');
            if (!lines.isEmpty() && lines.constLast().isEmpty())
                lines.removeLast();
            for (const QByteArray& line : lines) {
                identities.append(QJsonDocument::fromJson(line).object().value(
                    QStringLiteral("destinationIdentity")).toString());
            }
        }
        return identities;
    };
    auto expectedAuditIdentitySequence = [&](int first, int last,
                                             bool includeEleven) {
        QStringList identities;
        for (int sequence = first; sequence <= last; ++sequence)
            identities.append(auditTestIdentity(sequence));
        if (includeEleven)
            identities.append(auditTestIdentity(11));
        return identities;
    };
    auto validPrivateAuditSet = [&](const QString& livePath) {
        const QString directory = QFileInfo(livePath).absolutePath();
        const QSet<QString> allowedNames{QFileInfo(livePath).fileName(),
            QFileInfo(livePath).fileName() + QStringLiteral(".1"),
            QFileInfo(livePath).fileName() + QStringLiteral(".2"),
            QFileInfo(auditLockPathFor(livePath)).fileName()};
        const QStringList entries = QDir(directory).entryList(
            QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        if (!privateAuditDirectory(directory))
            return false;
        for (const QString& entry : entries) {
            if (!allowedNames.contains(entry))
                return false;
            const QString path = QDir(directory).filePath(entry);
            if (!privateAuditFile(path)
                || (path != auditLockPathFor(livePath)
                    && !validAuditBytes(readFile(path)))) {
                return false;
            }
        }
        return true;
    };
    auto auditCrashAt = [&](const QString& checkpoint, bool eventCommitted,
                            int interruptedFirst = 4) {
        const QString directory = QDir(temporary.path()).filePath(
            QStringLiteral("audit-crash-") + checkpoint);
        const QString livePath = QDir(directory).filePath(
            QStringLiteral("recovery.jsonl"));
        bool seeded = true;
        for (int sequence = 1; sequence <= 9; ++sequence) {
            seeded = seeded && appendAuditEvent(livePath,
                auditTestIdentity(sequence), RecoveryDecision::Retain,
                QStringLiteral("retained"), 4096, 3, 2);
        }
        check(seeded && auditIdentitySequence(livePath)
            == expectedAuditIdentitySequence(1, 9, false),
            qPrintable(QStringLiteral("audit kill fixture seeded: ") + checkpoint));

        const QString ready = QDir(controlDirectory).filePath(
            QStringLiteral("audit-") + checkpoint + QStringLiteral("-ready"));
        QProcess writer;
        if (seeded) {
            writer.start(application.applicationFilePath(),
                {QStringLiteral("--append-audit-and-hold"), livePath,
                    auditTestIdentity(10), checkpoint, ready});
        }
        bool reached = seeded && writer.waitForStarted(3000);
        QElapsedTimer timer;
        timer.start();
        while (reached && !QFileInfo::exists(ready) && timer.elapsed() < 3000)
            writer.waitForReadyRead(10);
        reached = reached && QFileInfo::exists(ready);
        if (reached)
            writer.kill();
        const bool killed = reached && writer.waitForFinished(3000)
            && writer.exitStatus() == QProcess::CrashExit;
        check(killed,
            qPrintable(QStringLiteral("SIGKILL reached audit rotation checkpoint: ")
                + checkpoint));

        const QStringList interruptedExpected = eventCommitted
            ? expectedAuditIdentitySequence(4, 10, false)
            : expectedAuditIdentitySequence(interruptedFirst, 9, false);
        check(killed && validPrivateAuditSet(livePath)
            && QFileInfo::exists(auditLockPathFor(livePath))
            && auditIdentitySequence(livePath) == interruptedExpected,
            qPrintable(QStringLiteral("interrupted audit retains a valid exact prefix: ")
                + checkpoint));

        const QByteArray deadLockBytes = readFile(auditLockPathFor(livePath));
        check(killed && !appendAuditEvent(livePath,
            auditTestIdentity(11), RecoveryDecision::Retain,
            QStringLiteral("retained"), 4096, 3, 2)
            && auditIdentitySequence(livePath) == interruptedExpected
            && readFile(auditLockPathFor(livePath)) == deadLockBytes,
            qPrintable(QStringLiteral("default append preserves the dead audit lock: ")
                + checkpoint));
        const bool resumed = killed && appendAuditEvent(livePath,
            auditTestIdentity(11), RecoveryDecision::Retain,
            QStringLiteral("retained"), 4096, 3, 2, {}, {}, true);
        const QStringList resumedExpected = eventCommitted
            ? expectedAuditIdentitySequence(4, 10, true)
            : expectedAuditIdentitySequence(4, 9, true);
        check(resumed && validPrivateAuditSet(livePath)
            && auditIdentitySequence(livePath) == resumedExpected,
            qPrintable(QStringLiteral("next append resumes exact bounded audit state: ")
                + checkpoint));
    };
    auditCrashAt(QStringLiteral("lock-acquired"), false, 1);
    auditCrashAt(QStringLiteral("oldest-removed"), false);
    auditCrashAt(QStringLiteral("archive-1-to-2"), false);
    auditCrashAt(QStringLiteral("live-archived"), false);
    auditCrashAt(QStringLiteral("live-committed"), true);
    auditCrashAt(QStringLiteral("live-file-synced"), true);
    auditCrashAt(QStringLiteral("live-directory-synced"), true);

    const QString busyAudit = QDir(temporary.path()).filePath(
        QStringLiteral("busy-audit/recovery.jsonl"));
    const QString busyAuditReady = QDir(controlDirectory).filePath(
        QStringLiteral("busy-audit-ready"));
    const QString busyAuditRelease = QDir(controlDirectory).filePath(
        QStringLiteral("busy-audit-release"));
    const bool busyAuditSeeded = appendAuditEvent(busyAudit,
        auditTestIdentity(1), RecoveryDecision::Retain, QStringLiteral("retained"),
        4096, 3, 2);
    QProcess activeAuditWriter;
    if (busyAuditSeeded) {
        activeAuditWriter.start(application.applicationFilePath(),
            {QStringLiteral("--hold-audit-lock"), busyAudit,
                busyAuditReady, busyAuditRelease});
    }
    bool activeAuditReached = busyAuditSeeded
        && activeAuditWriter.waitForStarted(3000);
    QElapsedTimer activeAuditTimer;
    activeAuditTimer.start();
    while (activeAuditReached && !QFileInfo::exists(busyAuditReady)
        && activeAuditTimer.elapsed() < 3000) {
        activeAuditWriter.waitForReadyRead(10);
    }
    activeAuditReached = activeAuditReached && QFileInfo::exists(busyAuditReady);
    check(activeAuditReached && privateAuditFile(auditLockPathFor(busyAudit)),
        "separate audit writer holds a private deterministic audit lock");
    const QByteArray busyAuditBeforeContention = readFile(busyAudit);
    check(activeAuditReached && !appendAuditEvent(busyAudit,
        auditTestIdentity(2), RecoveryDecision::Retain, QStringLiteral("retained"),
        4096, 3, 2) && readFile(busyAudit) == busyAuditBeforeContention,
        "second writer refuses an actively locked audit without changing the log");

    const QString independentAudit = QDir(temporary.path()).filePath(
        QStringLiteral("independent-audit/recovery.jsonl"));
    check(activeAuditReached && appendAuditEvent(independentAudit,
        auditTestIdentity(2), RecoveryDecision::Retain, QStringLiteral("retained"),
        4096, 3, 2),
        "different audit logs use independent writer locks");
    const QString separatedRecoveryDestination = QDir(temporary.path()).filePath(
        QStringLiteral("separated-recovery/note.md"));
    const bool separatedRecoveryParent = QDir().mkpath(
        QFileInfo(separatedRecoveryDestination).absolutePath());
    QLockFile separatedRecoveryLock(lockPathFor(separatedRecoveryDestination));
    separatedRecoveryLock.setStaleLockTime(0);
    check(activeAuditReached && separatedRecoveryParent
        && auditLockPathFor(busyAudit) != lockPathFor(separatedRecoveryDestination)
        && separatedRecoveryLock.tryLock(0),
        "audit writer lock and destination recovery lock have separate namespaces");
    check(writeFile(busyAuditRelease, "RELEASE")
        && activeAuditWriter.waitForFinished(3000)
        && activeAuditWriter.exitStatus() == QProcess::NormalExit
        && activeAuditWriter.exitCode() == 0
        && appendAuditEvent(busyAudit, auditTestIdentity(2),
            RecoveryDecision::Retain, QStringLiteral("retained"), 4096, 3, 2)
        && auditIdentitySequence(busyAudit)
            == expectedAuditIdentitySequence(1, 2, false),
        "audit append resumes after the active writer releases its lock");

    const QString busyQuarantineAudit = QDir(temporary.path()).filePath(
        QStringLiteral("busy-quarantine/recovery.jsonl"));
    const QByteArray busyQuarantineBytes("{corrupt while locked\n");
    const QString busyQuarantineReady = QDir(controlDirectory).filePath(
        QStringLiteral("busy-quarantine-ready"));
    const QString busyQuarantineRelease = QDir(controlDirectory).filePath(
        QStringLiteral("busy-quarantine-release"));
    const bool busyQuarantineSeeded = QDir().mkpath(
        QFileInfo(busyQuarantineAudit).absolutePath())
        && writeFile(busyQuarantineAudit, busyQuarantineBytes);
    QProcess activeQuarantineWriter;
    if (busyQuarantineSeeded) {
        activeQuarantineWriter.start(application.applicationFilePath(),
            {QStringLiteral("--hold-audit-lock"), busyQuarantineAudit,
                busyQuarantineReady, busyQuarantineRelease});
    }
    bool activeQuarantineReached = busyQuarantineSeeded
        && activeQuarantineWriter.waitForStarted(3000);
    QElapsedTimer activeQuarantineTimer;
    activeQuarantineTimer.start();
    while (activeQuarantineReached && !QFileInfo::exists(busyQuarantineReady)
        && activeQuarantineTimer.elapsed() < 3000) {
        activeQuarantineWriter.waitForReadyRead(10);
    }
    activeQuarantineReached = activeQuarantineReached
        && QFileInfo::exists(busyQuarantineReady);
    check(activeQuarantineReached
        && quarantineCorruptAuditLog(busyQuarantineAudit).isEmpty()
        && readFile(busyQuarantineAudit) == busyQuarantineBytes,
        "corrupt-log quarantine refuses an active audit writer and preserves bytes");
    const bool activeQuarantineReleased = writeFile(busyQuarantineRelease, "RELEASE")
        && activeQuarantineWriter.waitForFinished(3000)
        && activeQuarantineWriter.exitStatus() == QProcess::NormalExit
        && activeQuarantineWriter.exitCode() == 0;
    const QString busyQuarantined = activeQuarantineReleased
        ? quarantineCorruptAuditLog(busyQuarantineAudit) : QString();
    check(activeQuarantineReleased && !busyQuarantined.isEmpty()
        && !QFileInfo::exists(busyQuarantineAudit)
        && readFile(busyQuarantined) == busyQuarantineBytes,
        "corrupt-log quarantine proceeds after the audit writer releases its lock");

    const QString linkedAuditLockLog = QDir(temporary.path()).filePath(
        QStringLiteral("linked-audit-lock/recovery.jsonl"));
    const QString linkedAuditLockTarget = QDir(controlDirectory).filePath(
        QStringLiteral("linked-audit-lock-target"));
    check(QDir().mkpath(QFileInfo(linkedAuditLockLog).absolutePath())
        && writeFile(linkedAuditLockTarget, "LOCK TARGET\n")
        && QFile::link(linkedAuditLockTarget, auditLockPathFor(linkedAuditLockLog))
        && !appendAuditEvent(linkedAuditLockLog, auditTestIdentity(1),
            RecoveryDecision::Retain, QStringLiteral("retained"), 4096, 3, 2,
            {}, {}, true)
        && readFile(linkedAuditLockTarget) == "LOCK TARGET\n"
        && !QFileInfo::exists(linkedAuditLockLog),
        "audit append refuses a replacement lock symlink without following it");

    const QString malformedAuditLockLog = QDir(temporary.path()).filePath(
        QStringLiteral("malformed-audit-lock/recovery.jsonl"));
    const QByteArray malformedAuditLockBytes("not-a-lock\n");
    const bool malformedAuditSeeded = appendAuditEvent(malformedAuditLockLog,
        auditTestIdentity(1), RecoveryDecision::Retain, QStringLiteral("retained"));
    const QByteArray malformedAuditBefore = readFile(malformedAuditLockLog);
    check(malformedAuditSeeded
        && writeFile(auditLockPathFor(malformedAuditLockLog), malformedAuditLockBytes)
        && !appendAuditEvent(malformedAuditLockLog, auditTestIdentity(2),
            RecoveryDecision::Retain, QStringLiteral("retained"), 4096, 3, 2,
            {}, {}, true)
        && readFile(malformedAuditLockLog) == malformedAuditBefore
        && readFile(auditLockPathFor(malformedAuditLockLog)) == malformedAuditLockBytes,
        "confirmed append refuses and preserves a malformed audit lock");

    const QString foreignAuditLockLog = QDir(temporary.path()).filePath(
        QStringLiteral("foreign-audit-lock/recovery.jsonl"));
    const QByteArray foreignAuditLockBytes = QByteArray::number(99999999)
        + "\nmarkdown-recovery-prototype\nforeign-host.invalid\n";
    const bool foreignAuditSeeded = appendAuditEvent(foreignAuditLockLog,
        auditTestIdentity(1), RecoveryDecision::Retain, QStringLiteral("retained"));
    const QByteArray foreignAuditBefore = readFile(foreignAuditLockLog);
    check(foreignAuditSeeded
        && writeFile(auditLockPathFor(foreignAuditLockLog), foreignAuditLockBytes)
        && !appendAuditEvent(foreignAuditLockLog, auditTestIdentity(2),
            RecoveryDecision::Retain, QStringLiteral("retained"), 4096, 3, 2,
            {}, {}, true)
        && readFile(foreignAuditLockLog) == foreignAuditBefore
        && readFile(auditLockPathFor(foreignAuditLockLog)) == foreignAuditLockBytes,
        "confirmed append refuses and preserves a foreign-host audit lock");

    const QString linkedArchiveDirectory = QDir(temporary.path()).filePath(
        QStringLiteral("linked-audit-archive"));
    const QString linkedArchiveLog = QDir(linkedArchiveDirectory).filePath(
        QStringLiteral("recovery.jsonl"));
    bool linkedArchiveSeeded = true;
    for (int eventIndex = 0; eventIndex < 3; ++eventIndex) {
        linkedArchiveSeeded = linkedArchiveSeeded
            && appendAuditEvent(linkedArchiveLog, boundedIdentity,
                RecoveryDecision::Retain, QStringLiteral("retained"), 4096, 3, 2);
    }
    const QByteArray linkedArchiveLiveBefore = readFile(linkedArchiveLog);
    const QString linkedArchiveTarget = QDir(temporary.path()).filePath(
        QStringLiteral("linked-audit-target"));
    const QString linkedArchivePath = linkedArchiveLog + QStringLiteral(".1");
    check(linkedArchiveSeeded && writeFile(linkedArchiveTarget, "ARCHIVE TARGET\n")
        && QFile::link(linkedArchiveTarget, linkedArchivePath)
        && !appendAuditEvent(linkedArchiveLog, boundedIdentity,
            RecoveryDecision::Retain, QStringLiteral("retained"), 4096, 3, 2)
        && readFile(linkedArchiveLog) == linkedArchiveLiveBefore
        && readFile(linkedArchiveTarget) == "ARCHIVE TARGET\n",
        "rotation refuses a replacement archive symlink without changing either file");

    const QString corruptAuditDirectory = QDir(temporary.path()).filePath(
        QStringLiteral("corrupt-audit"));
    const QString corruptAudit = QDir(corruptAuditDirectory).filePath(
        QStringLiteral("recovery.jsonl"));
    const QByteArray corruptAuditBytes("{truncated audit\n");
    check(QDir().mkpath(corruptAuditDirectory)
        && writeFile(corruptAudit, corruptAuditBytes)
        && !appendAuditEvent(corruptAudit, boundedIdentity,
            RecoveryDecision::Retain, QStringLiteral("retained"))
        && readFile(corruptAudit) == corruptAuditBytes,
        "corrupt live audit is refused and retained byte-for-byte");
    const QString quarantinedAudit = quarantineCorruptAuditLog(corruptAudit);
    check(!quarantinedAudit.isEmpty() && !QFileInfo::exists(corruptAudit)
        && privateAuditFile(quarantinedAudit)
        && readFile(quarantinedAudit) == corruptAuditBytes
        && appendAuditEvent(corruptAudit, boundedIdentity,
            RecoveryDecision::Retain, QStringLiteral("retained"))
        && validAuditBytes(readFile(corruptAudit)),
        "explicit quarantine preserves corrupt evidence before a new valid log is created");
    const QByteArray validAuditBeforeQuarantineAttempt = readFile(corruptAudit);
    check(quarantineCorruptAuditLog(corruptAudit).isEmpty()
        && readFile(corruptAudit) == validAuditBeforeQuarantineAttempt,
        "explicit quarantine refuses a valid current audit log");

    const QString oversizedAudit = QDir(temporary.path()).filePath(
        QStringLiteral("oversized-audit/recovery.jsonl"));
    check(!appendAuditEvent(oversizedAudit, boundedIdentity,
        RecoveryDecision::Retain, QStringLiteral("retained"), 10, 3, 2)
        && !QFileInfo::exists(oversizedAudit),
        "single audit event larger than the configured bound is refused");

    Fixture foreignLock;
    const QByteArray foreignBytes = QByteArray::number(99999999)
        + "\nmarkdown-recovery-prototype\nforeign-host.invalid\n";
    check(prepareFixture(temporary.path(), QStringLiteral("foreign-lock"), true, foreignLock)
        && durableWriteFile(lockPathFor(foreignLock.destination), foreignBytes),
        "unverifiable foreign-host lock fixture created");
    check(recover(foreignLock.stage, true) == RecoveryResult::Busy
        && readFile(lockPathFor(foreignLock.destination)) == foreignBytes
        && oldPair(foreignLock) && QFileInfo::exists(foreignLock.stage),
        "confirmed recovery refuses and retains an unverifiable foreign-host lock");

    qInfo().noquote() << QJsonDocument(QJsonObject{
        {QStringLiteral("diagnosticOnly"), true},
        {QStringLiteral("productModified"), false},
        {QStringLiteral("realUserData"), false},
        {QStringLiteral("recoveryProtocolAcceptedForProduct"), false},
        {QStringLiteral("stateDigestsRequired"), true},
        {QStringLiteral("deterministicDestinationLockRequired"), true},
        {QStringLiteral("deterministicOnDemandRecoveryPath"), true},
        {QStringLiteral("explicitFileAndDirectorySync"), true},
        {QStringLiteral("forcedTerminationBoundaries"), 6},
        {QStringLiteral("metadataDigestFields"),
            QStringLiteral("mode,uid,gid,flags,xattrs,content")},
        {QStringLiteral("metadataTreeFiles"), metadataFileCount},
        {QStringLiteral("metadataTreePayloadBytes"), metadataPayloadBytes},
        {QStringLiteral("contentDigestMedianMs"), contentDigestMedianMs},
        {QStringLiteral("metadataDigestMedianMs"), metadataDigestMedianMs},
        {QStringLiteral("incrementalColdMs"), coldIncrementalMs},
        {QStringLiteral("incrementalWarmMs"), warmIncrementalMs},
        {QStringLiteral("incrementalWarmReusedFiles"), warmIncremental.reusedFiles},
        {QStringLiteral("persistentCacheSchema"), 1},
        {QStringLiteral("persistentCacheEntries"), digestCache.size()},
        {QStringLiteral("persistentCacheStrictFields"), true},
        {QStringLiteral("persistentCacheAtomicCommit"), true},
        {QStringLiteral("persistentCacheSilentStaleLockTakeover"), false},
        {QStringLiteral("persistentHintsRequireFirstUseRehash"), true},
        {QStringLiteral("postFingerprintMutationValidation"), true},
        {QStringLiteral("cacheCanAuthorizeRecovery"), false},
        {QStringLiteral("privateBoundedAuditLog"), true},
        {QStringLiteral("auditArchiveCount"), 2},
        {QStringLiteral("auditForcedTerminationBoundaries"), 7},
        {QStringLiteral("auditInterruptedRotationResumable"), true},
        {QStringLiteral("auditCrossProcessWriterLock"), true},
        {QStringLiteral("auditSilentDeadLockTakeover"), false},
        {QStringLiteral("auditLockSeparateFromRecoveryLock"), true},
        {QStringLiteral("corruptAuditRequiresExplicitQuarantine"), true},
        {QStringLiteral("recoveryDecisionAudit"), true},
        {QStringLiteral("nonMutatingInspectCancelRetain"), true},
        {QStringLiteral("silentStaleLockTakeover"), false},
        {QStringLiteral("checks"), checks},
        {QStringLiteral("failures"), failures}
    }).toJson(QJsonDocument::Compact);
    return failures ? 1 : 0;
}
