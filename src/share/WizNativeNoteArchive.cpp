#include "WizNativeNoteArchive.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QTemporaryFile>
#include <quazip.h>
#include <quazipfile.h>

namespace WizNativeNotes {
namespace {
constexpr qint64 maxFile = 256LL * 1024 * 1024;
constexpr qint64 maxTotal = 8LL * 1024 * 1024 * 1024;
constexpr qint64 maxManifest = 16LL * 1024 * 1024;
constexpr int maxEntries = 100000;
QString key(const QString& path) { return path.normalized(QString::NormalizationForm_C).toCaseFolded(); }
bool fail(QString& error, const QString& text) { error = text; return false; }
bool stream(QIODevice& input, QIODevice* output, qint64 expected, QString& digest)
{
    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 count = 0;
    while (!input.atEnd()) {
        const QByteArray chunk = input.read(65536);
        if (chunk.isEmpty()) return false;
        count += chunk.size();
        if (count > expected || (output && output->write(chunk) != chunk.size())) return false;
        hash.addData(chunk);
    }
    digest = QString::fromLatin1(hash.result().toHex());
    return count == expected;
}
bool inventory(QuaZip& zip, QMap<QString, qint64>& entries, bool directories, QString& error)
{
    QSet<QString> seen;
    qint64 total = 0;
    if (!zip.goToFirstFile()) return fail(error, QStringLiteral("空的 ZIP 文件。"));
    do {
        QuaZipFileInfo64 info;
        if (!zip.getCurrentFileInfo(&info)) return fail(error, QStringLiteral("无法读取 ZIP 目录。"));
        const bool directory = info.name.endsWith('/');
        const QString name = directory ? info.name.chopped(1) : info.name;
        const quint32 type = (info.externalAttr >> 16) & 0170000;
        if (!safePath(name) || (directory && !directories)
            || (type && type != 0100000 && !(directory && type == 0040000))
            || seen.contains(key(name)) || info.uncompressedSize > quint64(maxFile)
            || seen.size() >= maxEntries) {
            return fail(error, QStringLiteral("ZIP 含不安全路径、链接、重复名称或超限文件。"));
        }
        seen.insert(key(name));
        total += qint64(info.uncompressedSize);
        if (total > maxTotal) return fail(error, QStringLiteral("ZIP 解压总量超过 8 GiB。"));
        if (!directory) entries.insert(info.name, qint64(info.uncompressedSize));
    } while (zip.goToNextFile());
    return zip.getZipError() == UNZ_OK || fail(error, QStringLiteral("ZIP 目录损坏。"));
}
}

bool safePath(const QString& path)
{
    if (path.isEmpty() || path.size() > 1024 || path.contains('\\') || path.contains(':')
        || path.startsWith('/') || path.endsWith('/')) return false;
    for (QChar c : path) if (c.unicode() < 32 || c.unicode() == 127) return false;
    for (const QString& part : path.split('/'))
        if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ')) return false;
    return true;
}

bool writeArchive(const QString& destination, const QJsonObject& notes,
                  const QList<File>& files, QString& error)
{
    error.clear();
    if (files.isEmpty() || files.size() >= maxEntries || QFileInfo::exists(destination)
        || QFileInfo(destination).isSymLink())
        return fail(error, QStringLiteral("导出目标已存在或文件数量无效；请选择新的文件名。"));
    QTemporaryFile output(QFileInfo(destination).absolutePath() + "/.wiznotes-XXXXXX");
    if (!output.open()) return fail(error, QStringLiteral("无法创建导出文件。"));
    output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    QuaZip zip(&output);
    zip.setAutoClose(false);
    zip.setFileNameCodec("UTF-8");
    if (!zip.open(QuaZip::mdCreate)) return fail(error, QStringLiteral("无法建立 ZIP 包。"));
    QJsonArray manifestFiles;
    QSet<QString> names;
    qint64 total = 0;
    for (const File& file : files) {
        QFileInfo info(file.source);
        if (!safePath(file.path) || file.path == "manifest.json" || names.contains(key(file.path))
            || !info.isFile() || info.isSymLink() || info.size() > maxFile
            || info.size() < 0 || (total += info.size()) > maxTotal) {
            zip.close();
            return fail(error, QStringLiteral("导出源含无效路径、链接或超限文件。"));
        }
        names.insert(key(file.path));
        QFile input(file.source);
        QuaZipFile entry(&zip);
        QString sha;
        bool ok = input.open(QIODevice::ReadOnly)
            && entry.open(QIODevice::WriteOnly, QuaZipNewInfo(file.path))
            && stream(input, &entry, info.size(), sha);
        entry.close();
        ok = ok && entry.getZipError() == UNZ_OK;
        if (!ok || input.size() != info.size() || QFileInfo(file.source).lastModified() != info.lastModified()) {
            zip.close();
            return fail(error, QStringLiteral("源文件读取失败或在导出时发生变化，未生成最终文件。"));
        }
        manifestFiles.append(QJsonObject{{"path", file.path}, {"size", double(info.size())}, {"sha256", sha}});
    }
    QJsonObject manifest{{"format", "WizNotePlus-native-notes"}, {"version", 1},
                         {"notes", notes}, {"files", manifestFiles}};
    const QByteArray body = QJsonDocument(manifest).toJson();
    QuaZipFile entry(&zip);
    bool ok = body.size() <= maxManifest && entry.open(QIODevice::WriteOnly, QuaZipNewInfo("manifest.json"));
    if (ok) ok = entry.write(body) == body.size();
    entry.close();
    ok = ok && entry.getZipError() == UNZ_OK;
    zip.close();
    if (!ok || zip.getZipError() != UNZ_OK || !output.flush())
        return fail(error, QStringLiteral("导出包写入失败。"));
    // QFile::rename refuses to replace an existing destination, including a race.
    if (!output.rename(destination)) return fail(error, QStringLiteral("不能完成导出；目标可能已存在。"));
    output.setAutoRemove(false);
    return true;
}

bool readArchive(const QString& archive, const QString& staging, QJsonObject& notes, QString& error)
{
    notes = {}; error.clear();
    QFileInfo root(staging);
    if (!root.isDir() || root.isSymLink()
        || !QDir(staging).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System).isEmpty())
        return fail(error, QStringLiteral("导入暂存目录必须为空且不能是链接。"));
    QuaZip zip(archive);
    zip.setFileNameCodec("UTF-8");
    if (!zip.open(QuaZip::mdUnzip)) return fail(error, QStringLiteral("不是可读取的原始笔记包。"));
    QMap<QString, qint64> entries;
    if (!inventory(zip, entries, false, error)) return false;
    if (!entries.contains("manifest.json") || entries.value("manifest.json") > maxManifest
        || !zip.setCurrentFile("manifest.json", QuaZip::csSensitive))
        return fail(error, QStringLiteral("缺少或超限的原始笔记清单。"));
    QuaZipFile entry(&zip);
    if (!entry.open(QIODevice::ReadOnly)) return fail(error, QStringLiteral("清单读取失败。"));
    const QByteArray body = entry.read(maxManifest + 1);
    entry.close();
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parse);
    const QJsonObject manifest = document.object();
    if (entry.getZipError() != UNZ_OK || parse.error != QJsonParseError::NoError
        || manifest.value("format").toString() != "WizNotePlus-native-notes"
        || manifest.value("version").toInt() != 1 || !manifest.value("notes").isObject()
        || !manifest.value("files").isArray()) return fail(error, QStringLiteral("不支持或损坏的笔记包版本。"));
    const QJsonArray files = manifest.value("files").toArray();
    QSet<QString> listed;
    if (files.size() + 1 != entries.size()) return fail(error, QStringLiteral("清单与 ZIP 文件数量不一致。"));
    for (const QJsonValue& value : files) {
        const QJsonObject item = value.toObject();
        const QString path = item.value("path").toString();
        const qint64 size = entries.value(path, -1);
        const QString sha = item.value("sha256").toString();
        if (path == "manifest.json" || size < 0 || listed.contains(path)
            || item.value("size").toDouble(-1) != double(size) || sha.size() != 64
            || !zip.setCurrentFile(path, QuaZip::csSensitive)) return fail(error, QStringLiteral("文件清单无效。"));
        listed.insert(path);
        const QString target = QDir(staging).filePath(path);
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) return fail(error, QStringLiteral("暂存目录创建失败。"));
        QFile output(target);
        QuaZipFile payload(&zip);
        QString actual;
        bool ok = payload.open(QIODevice::ReadOnly) && output.open(QIODevice::WriteOnly | QIODevice::NewOnly);
        if (ok) {
            output.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
            ok = stream(payload, &output, size, actual) && output.flush();
        }
        payload.close(); output.close();
        if (!ok || payload.getZipError() != UNZ_OK || actual != sha)
            return fail(error, QStringLiteral("笔记包校验失败，尚未导入数据库。"));
    }
    notes = manifest.value("notes").toObject();
    return true;
}

bool validateZiw(const QString& path, QString& error)
{
    QuaZip zip(path);
    zip.setFileNameCodec("UTF-8");
    if (!zip.open(QuaZip::mdUnzip)) return fail(error, QStringLiteral("未加密的笔记容器不是有效 ZIP。"));
    QMap<QString, qint64> entries;
    if (!inventory(zip, entries, true, error)) return false;
    if (!entries.contains("index.html")) return fail(error, QStringLiteral("笔记容器缺少 index.html。"));
    for (auto it = entries.cbegin(); it != entries.cend(); ++it) {
        if (!zip.setCurrentFile(it.key(), QuaZip::csSensitive)) return false;
        QuaZipFile file(&zip);
        QString unused;
        bool ok = file.open(QIODevice::ReadOnly) && stream(file, nullptr, it.value(), unused);
        file.close();
        if (!ok || file.getZipError() != UNZ_OK) return fail(error, QStringLiteral("笔记容器 CRC 或大小校验失败。"));
    }
    return true;
}
}
