#include "WizNativeNoteTransfer.h"
#include "share/WizNativeNoteArchive.h"
#include "database/WizDatabase.h"
#include "share/WizMisc.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QSaveFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QMessageBox>
#include <QSet>
#include <QRegularExpression>
#include <QTemporaryDir>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QUuid>
#include <QUrl>
#include <QVBoxLayout>
#include <quazip/JlCompress.h>

namespace WizNativeNotes {
namespace {
QString certificateFingerprint(WizDatabase& db)
{
    QString n, e, privateKey, hint;
    if (!db.getUserCert(n, e, privateKey, hint) || n.isEmpty() || e.isEmpty()) return {};
    return QString::fromLatin1(QCryptographicHash::hash((n + ":" + e).toUtf8(), QCryptographicHash::Sha256).toHex());
}
bool failure(QString& error, const QString& message) { error = message; return false; }
bool copySnapshot(const QString& source, const QString& target, QString& error)
{
    const QFileInfo before(source);
    if (!before.isFile() || before.isSymLink() || before.size() > 256LL * 1024 * 1024)
        return failure(error, QStringLiteral("源文件未下载、为链接或超过 256 MiB。请先下载笔记和附件。"));
    if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(source, target))
        return failure(error, QStringLiteral("无法暂存原始数据。"));
    QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    const QFileInfo after(source);
    if (before.size() != after.size() || before.lastModified() != after.lastModified())
        return failure(error, QStringLiteral("数据正在变化；请停止编辑或同步后重试。"));
    return true;
}
QJsonObject encode(const WIZDOCUMENTDATA& d)
{
    return {{"title", d.strTitle}, {"location", d.strLocation}, {"sourceGuid", d.strGUID},
        {"url", d.strURL}, {"author", d.strAuthor}, {"keywords", d.strKeywords},
        {"type", d.strType}, {"fileType", d.strFileType}, {"name", d.strName},
        {"created", double(d.tCreated.toSecsSinceEpoch())}, {"modified", double(d.tModified.toSecsSinceEpoch())},
        {"accessed", double(d.tAccessed.toSecsSinceEpoch())}, {"dataModified", double(d.tDataModified.toSecsSinceEpoch())},
        {"protected", int(d.nProtected)}, {"flags", d.nFlags}, {"rate", d.nRate}};
}
bool validLocation(const QString& location)
{
    return location == "/" || (location.startsWith('/') && location.endsWith('/')
        && safePath(location.mid(1, location.size() - 2)));
}
bool tagPath(WizDatabase& db, WIZTAGDATA tag, QJsonArray& path)
{
    QSet<QString> seen;
    while (!tag.strGUID.isEmpty()) {
        if (seen.contains(tag.strGUID) || seen.size() >= 64) return false;
        seen.insert(tag.strGUID);
        path.prepend(QJsonObject{{"name", tag.strName}, {"description", tag.strDescription}});
        if (tag.strParentGUID.isEmpty()) break;
        if (!db.tagFromGuid(tag.strParentGUID, tag)) return false;
    }
    return !path.isEmpty();
}
}

bool exportNotes(WizDatabase& db, const CWizDocumentDataArray& documents,
                 const QString& destination, QString& error)
{
    error.clear();
    if (db.isGroup()) return failure(error, QStringLiteral("本版仅支持个人笔记。"));
    if (documents.empty() || documents.size() > 10000)
        return failure(error, QStringLiteral("请选择 1 至 10000 条个人笔记。"));
    QTemporaryDir staging;
    if (!staging.isValid()) return failure(error, QStringLiteral("无法创建导出暂存目录。"));
    QList<File> files;
    QJsonArray notes;
    int number = 0;
    QSet<QString> guids;
    for (const WIZDOCUMENTDATA& requested : documents) {
        WIZDOCUMENTDATA d;
        if (requested.strKbGUID != db.kbGUID() || guids.contains(requested.strGUID)
            || !db.documentFromGuid(requested.strGUID, d))
            return failure(error, QStringLiteral("笔记不存在、重复或不属于当前个人资料库。"));
        guids.insert(d.strGUID);
        if (!validLocation(d.strLocation) || d.strTitle.isEmpty() || d.strTitle.size() > 4096)
            return failure(error, QStringLiteral("现有笔记的目录或标题不受本版原始包支持，尚未生成导出包。"));
        const QString prefix = QStringLiteral("notes/%1/").arg(number++);
        const QString native = prefix + "note.ziw";
        const QString raw = staging.filePath(native);
        if (!db.isObjectDataDownloaded(d.strGUID, "document")
            || !copySnapshot(db.getDocumentFileName(d.strGUID), raw, error))
            return failure(error, error.isEmpty() ? QStringLiteral("请先下载所有选中笔记。") : error);
        files.append({native, raw});
        QJsonObject note = encode(d);
        note.insert("native", native);
        const bool encrypted = WizZiwReader::isEncryptedFile(raw);
        if (encrypted != bool(d.nProtected)) return failure(error, QStringLiteral("加密标记与原始容器不一致。"));
        if (!encrypted) {
            if (!validateZiw(raw, error)) return false;
            // A portable view of the original HTML/resources, alongside unchanged ZIW.
            const QString view = staging.filePath(prefix + "html");
            if (JlCompress::extractDir(raw, view).isEmpty()) return failure(error, QStringLiteral("无法生成 HTML 资源视图。"));
            QDirIterator it(view, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                const QString path = it.next();
                files.append({prefix + "html/" + QDir(view).relativeFilePath(path), path});
            }
            note.insert("html", prefix + "html/index.html");
        }
        QJsonArray tagPaths;
        CWizTagDataArray tags;
        if (!db.getDocumentTags(d.strGUID, tags)) return failure(error, QStringLiteral("标签读取失败。"));
        for (const WIZTAGDATA& tag : tags) {
            QJsonArray path;
            if (!tagPath(db, tag, path)) return failure(error, QStringLiteral("标签层级损坏。"));
            tagPaths.append(path);
        }
        note.insert("tags", tagPaths);
        QJsonObject params;
        CWizDocumentParamDataArray parameters;
        if (!db.getDocumentParamsForExport(d.strGUID, parameters)) return failure(error, QStringLiteral("笔记参数读取失败。"));
        const QRegularExpression paramName("^[A-Za-z0-9_.-]{1,128}$");
        for (const auto& param : parameters) {
            if (!paramName.match(param.strName).hasMatch() || param.strValue.size() > 65536)
                return failure(error, QStringLiteral("现有笔记参数不受本版支持，尚未生成导出包。"));
            params.insert(param.strName, param.strValue);
        }
        note.insert("params", params);
        if (!d.strStyleGUID.isEmpty()) {
            WIZSTYLEDATA style;
            if (!db.styleFromGuid(d.strStyleGUID, style)) return failure(error, QStringLiteral("笔记样式读取失败。"));
            note.insert("style", QJsonObject{{"name", style.strName}, {"description", style.strDescription},
                // createStyle assigns its integer directly to QColor (Qt QRgb, not Win32 BGR).
                {"textColor", double(style.crTextColor.rgb())},
                {"backColor", double(style.crBackColor.rgb())},
                {"bold", style.bTextBold}, {"flag", style.nFlagIndex}});
        }
        QJsonArray attachments;
        CWizDocumentAttachmentDataArray data;
        if (!db.getDocumentAttachments(d.strGUID, data)) return failure(error, QStringLiteral("附件清单读取失败。"));
        int index = 0;
        for (const auto& a : data) {
            if (!safePath(a.strName) || a.strName.contains('/') || a.strName.size() > 255)
                return failure(error, QStringLiteral("附件名称不受本版原始包支持，尚未生成导出包。"));
            const QString path = prefix + QStringLiteral("attachments/%1.bin").arg(index++);
            if (!db.isObjectDataDownloaded(a.strGUID, "attachment")
                || !copySnapshot(db.getAttachmentFileName(a.strGUID), staging.filePath(path), error))
                return failure(error, error.isEmpty() ? QStringLiteral("请先下载所有选中笔记的附件。") : error);
            files.append({path, staging.filePath(path)});
            attachments.append(QJsonObject{{"path", path}, {"name", a.strName}, {"url", a.strURL},
                {"description", a.strDescription}, {"modified", double(a.tInfoModified.toSecsSinceEpoch())},
                {"dataModified", double(a.tDataModified.toSecsSinceEpoch())}});
        }
        note.insert("attachments", attachments);
        notes.append(note);
    }
    return writeArchive(destination, QJsonObject{{"documents", notes},
        {"certificateFingerprint", certificateFingerprint(db)}}, files, error);
}

bool exportPortableHtml(WizDatabase& db, const CWizDocumentDataArray& documents,
                        const QString& destination, QString& error)
{
    error.clear();
    const QFileInfo target(destination);
    if (target.exists() || target.isSymLink() || !QFileInfo(target.absolutePath()).isDir())
        return failure(error, QStringLiteral("请选择尚不存在的新文件夹；不会覆盖已有文件夹。"));
    // Cross-app HTML is plaintext. Never implicitly decrypt an encrypted selection.
    for (const auto& requested : documents) {
        WIZDOCUMENTDATA d;
        if (!db.documentFromGuid(requested.strGUID, d) || d.nProtected)
            return failure(error, QStringLiteral("通用 HTML 导出只支持未加密笔记；请选择未加密笔记，或使用原始笔记包保留加密数据。"));
    }
    QTemporaryDir snapshot;
    QTemporaryDir unpacked;
    QTemporaryDir output(target.absolutePath() + "/.WizNote-export-XXXXXX");
    if (!snapshot.isValid() || !unpacked.isValid() || !output.isValid())
        return failure(error, QStringLiteral("无法创建导出暂存目录。"));
    // Reuse the native export's download checks, safe ZIP validation and snapshots.
    const QString archive = snapshot.filePath("snapshot.wiznotes");
    QJsonObject metadata;
    if (!exportNotes(db, documents, archive, error)
        || !readArchive(archive, unpacked.path(), metadata, error)) return false;
    auto write = [&](const QString& path, const QByteArray& bytes) {
        if (QFileInfo::exists(path)) return false;
        QSaveFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
    };
    auto link = [](const QString& path) {
        return QString::fromLatin1(QUrl::toPercentEncoding(path, "/")).toHtmlEscaped();
    };
    QString catalog = QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><title>导出的笔记</title></head><body><h1>导出的笔记</h1><p>请连同资源和附件导入完整的 Notes 文件夹。附件清单单独列出；原始正文未改写。</p><ul>");
    QJsonArray portableDocuments;
    int number = 0;
    for (const auto& value : metadata.value("documents").toArray()) {
        const QJsonObject note = value.toObject();
        QString title = note.value("title").toString();
        QString filename = title.normalized(QString::NormalizationForm_C);
        filename.replace(QRegularExpression(QStringLiteral("[\\x00-\\x1f\\x7f/\\\\:*?\"<>|]")), "_");
        filename = filename.trimmed().left(96);
        while (filename.endsWith('.') || filename.endsWith(' ')) filename.chop(1);
        if (filename.isEmpty()) filename = "Untitled";
        const QString relative = "Notes/" + note.value("location").toString().mid(1)
            + QStringLiteral("%1-%2").arg(++number, 6, 10, QChar('0')).arg(filename);
        const QString noteDir = output.filePath(relative);
        const QString html = note.value("html").toString();
        if (!safePath(relative) || html.isEmpty() || !QDir().mkpath(noteDir))
            return failure(error, QStringLiteral("无法生成通用 HTML 目录。"));
        const QString sourceDir = QFileInfo(unpacked.filePath(html)).absolutePath();
        QDirIterator files(sourceDir, QDir::Files | QDir::Hidden | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const QString source = files.next();
            const QString path = QDir(sourceDir).relativeFilePath(source);
            if (!safePath(path) || !copySnapshot(source, noteDir + "/" + path, error)) return false;
        }
        catalog += QStringLiteral("<li><a href=\"%1\">%2</a>").arg(link(relative + "/index.html"), title.toHtmlEscaped());
        const auto attachments = note.value("attachments").toArray();
        QJsonObject portableNote = note;
        portableNote.remove("native");
        portableNote["html"] = relative + "/index.html";
        QJsonArray portableAttachments;
        if (!attachments.isEmpty()) {
            QString attachmentDir = "_WizNote-attachments";
            while (QFileInfo::exists(noteDir + "/" + attachmentDir)) attachmentDir += "_";
            if (!QDir().mkpath(noteDir + "/" + attachmentDir))
                return failure(error, QStringLiteral("无法创建附件目录。"));
            QString list = QStringLiteral("<!doctype html><html><head><meta charset=\"utf-8\"><title>附件</title></head><body><h1>%1 — 附件</h1><p><a href=\"../index.html\">返回笔记正文</a></p><ul>").arg(title.toHtmlEscaped());
            int index = 0;
            for (const auto& aValue : attachments) {
                const auto a = aValue.toObject();
                const QString name = a.value("name").toString();
                const QString renamed = QStringLiteral("%1-%2").arg(++index, 4, 10, QChar('0')).arg(name);
                if (!copySnapshot(unpacked.filePath(a.value("path").toString()), noteDir + "/" + attachmentDir + "/" + renamed, error)) return false;
                list += QStringLiteral("<li><a href=\"%1\">%2</a></li>").arg(link(renamed), name.toHtmlEscaped());
                QJsonObject portableAttachment = a;
                portableAttachment["path"] = relative + "/" + attachmentDir + "/" + renamed;
                portableAttachments.append(portableAttachment);
            }
            list += "</ul></body></html>";
            // This directory is fresh, so the generated index cannot replace a payload.
            if (!write(noteDir + "/" + attachmentDir + "/index.html", list.toUtf8()))
                return failure(error, QStringLiteral("附件清单写入失败。"));
            catalog += QStringLiteral(" — <a href=\"%1\">附件</a>").arg(link(relative + "/" + attachmentDir + "/index.html"));
        }
        catalog += "</li>";
        portableNote["attachments"] = portableAttachments;
        portableDocuments.append(portableNote);
    }
    catalog += "</ul></body></html>";
    metadata["documents"] = portableDocuments;
    metadata.remove("certificateFingerprint");
    metadata["format"] = "WizNotePlus-portable-html";
    metadata["version"] = 1;
    if (!write(output.filePath(QStringLiteral("浏览笔记.html")), catalog.toUtf8())
        || !write(output.filePath("note-metadata.json"), QJsonDocument(metadata).toJson())
        || !write(output.filePath(QStringLiteral("导入说明.txt")), QStringLiteral(
            "Notes 内为按原始文件夹组织的 HTML 笔记，每条笔记有独立文件夹，正文为 index.html。\n"
            "请将完整 Notes 文件夹及其图片、资源和附件导入支持 HTML/文件夹导入的软件，不要只移动 HTML。\n"
            "附件位于各笔记的 _WizNote-attachments 目录，内容未转换。正文 HTML 未改写、未净化。\n"
            "note-metadata.json 保留标题、时间、标签等信息；目标软件未必读取这些元信息。\n"
            "加密笔记不支持本模式；为知内部 GUID 链接不保证在其他软件可用。\n"
            "导出含私人笔记内容，请妥善保存。本目录不包含可直接恢复的 .wiznotes 原始包。\n").toUtf8()))
        return failure(error, QStringLiteral("导出索引写入失败。"));
    if (QFileInfo::exists(destination) || QFileInfo(destination).isSymLink()
        || !QDir().rename(output.path(), target.absoluteFilePath()))
        return failure(error, QStringLiteral("无法发布导出文件夹，或目标已存在；未覆盖已有文件。"));
    output.setAutoRemove(false);
    return true;
}

ImportResult importNotes(WizDatabase& db, const QString& archive)
{
    ImportResult result;
    if (db.isGroup()) { result.error = QStringLiteral("本版仅支持个人笔记。"); return result; }
    QTemporaryDir staging;
    QJsonObject metadata;
    if (!staging.isValid() || !readArchive(archive, staging.path(), metadata, result.error)) return result;
    const QJsonArray notes = metadata.value("documents").toArray();
    if (notes.isEmpty() || notes.size() > 10000) {
        result.error = QStringLiteral("笔记清单为空或超过 10000 条。"); return result;
    }
    // Preflight all notes before creating even one database record.
    QSet<QString> paths;
    const QString fingerprint = certificateFingerprint(db);
    for (const QJsonValue& value : notes) {
        const QJsonObject n = value.toObject();
        const QString path = n.value("native").toString();
        const QString raw = staging.filePath(path);
        const QString title = n.value("title").toString();
        const bool encrypted = n.value("protected").toInt() == 1;
        if (!safePath(path) || !path.startsWith("notes/") || paths.contains(path)
            || title.isEmpty() || title.size() > 4096 || !validLocation(n.value("location").toString())
            || !QFileInfo(raw).isFile() || !n.value("attachments").isArray()
            || !n.value("tags").isArray() || !n.value("params").isObject()
            || (n.value("protected").toInt(-1) != 0 && n.value("protected").toInt(-1) != 1)
            || WizZiwReader::isEncryptedFile(raw) != encrypted) {
            result.error = QStringLiteral("无效的笔记元信息，尚未导入任何笔记。"); return result;
        }
        paths.insert(path);
        if (encrypted && (fingerprint.isEmpty() || metadata.value("certificateFingerprint").toString() != fingerprint)) {
            result.error = QStringLiteral("加密笔记只能导入具有相同加密证书的账号；本包未携带密码或私钥。"); return result;
        }
        if (!encrypted && db.isEncryptAllData()) {
            result.error = QStringLiteral("目标启用了全库加密。本版不能将明文包自动重新加密，未导入任何笔记。"); return result;
        }
        if (!encrypted && !validateZiw(raw, result.error)) return result;
        for (const QJsonValue& attachment : n.value("attachments").toArray()) {
            const QJsonObject a = attachment.toObject();
            const QString p = a.value("path").toString();
            const QString name = a.value("name").toString();
            if (!safePath(p) || !p.startsWith("notes/") || paths.contains(p)
                || !QFileInfo(staging.filePath(p)).isFile() || name.isEmpty()
                || !safePath(name) || name.contains('/') || name.size() > 255) {
                result.error = QStringLiteral("附件元信息无效，尚未导入任何笔记。"); return result;
            }
            paths.insert(p);
            for (const QString& key : {"modified", "dataModified"}) {
                const QJsonValue time = a.value(key);
                if (!time.isDouble() || time.toDouble() < 0 || time.toDouble() > 253402300799.0) {
                    result.error = QStringLiteral("附件时间无效，尚未导入任何笔记。"); return result;
                }
            }
        }
        for (const QJsonValue& tags : n.value("tags").toArray()) {
            if (!tags.isArray() || tags.toArray().isEmpty() || tags.toArray().size() > 64) {
                result.error = QStringLiteral("标签路径无效。"); return result;
            }
            for (const QJsonValue& tag : tags.toArray()) if (tag.toObject().value("name").toString().isEmpty()
                || tag.toObject().value("name").toString().size() > 255) {
                result.error = QStringLiteral("标签名称无效。"); return result;
            }
        }
        const QJsonObject params = n.value("params").toObject();
        const QRegularExpression paramName("^[A-Za-z0-9_.-]{1,128}$");
        for (auto it = params.begin(); it != params.end(); ++it) if (!paramName.match(it.key()).hasMatch()
            || !it.value().isString() || it.value().toString().size() > 65536) {
            result.error = QStringLiteral("笔记参数格式不受支持，尚未导入任何笔记。"); return result;
        }
        for (const QString& key : {"created", "modified", "accessed", "dataModified"}) {
            const QJsonValue time = n.value(key);
            if (!time.isDouble() || time.toDouble() < 0 || time.toDouble() > 253402300799.0) {
                result.error = QStringLiteral("笔记时间无效，尚未导入任何笔记。"); return result;
            }
        }
        if (n.contains("style") && (!n.value("style").isObject()
            || n.value("style").toObject().value("name").toString().isEmpty())) {
            result.error = QStringLiteral("笔记样式无效，尚未导入任何笔记。"); return result;
        }
    }
    result.folder = "/导入-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")
        + "-" + QUuid::createUuid().toString(QUuid::Id128).left(8) + "/";
    QMap<QString, QString> newTags;
    QMap<QString, QString> newStyles;
    QString importedTagRoot;
    for (const QJsonValue& value : notes) {
        const QJsonObject n = value.toObject();
        const QString location = result.folder + n.value("location").toString().mid(1);
        WIZDOCUMENTDATA d;
        d.strOwner = db.getUserId();
        bool ok = db.createDocument(n.value("title").toString(), "newnote", location,
            n.value("url").toString(), n.value("author").toString(), n.value("keywords").toString(),
            n.value("type").toString(), d.strOwner, n.value("fileType").toString(), "", 0, 0,
            n.value("protected").toInt(), d);
        if (ok) {
            ok = QFile::copy(staging.filePath(n.value("native").toString()), db.getDocumentFileName(d.strGUID))
                && db.setObjectDataDownloaded(d.strGUID, "document", true)
                && (d.nProtected
                    ? db.WizIndex::updateDocumentDataMD5(d, db.getDocumentFileName(d.strGUID))
                    : db.updateDocumentDataMD5(d, db.getDocumentFileName(d.strGUID)));
        }
        for (const QJsonValue& valueA : n.value("attachments").toArray()) {
            if (!ok) break;
            const QJsonObject a = valueA.toObject();
            const QString source = staging.filePath(a.value("path").toString());
            WIZDOCUMENTATTACHMENTDATA created;
            ok = db.createAttachment(d.strGUID, a.value("name").toString(), a.value("url").toString(),
                a.value("description").toString(), WizMd5FileString(source), created);
            if (ok) {
                ok = QFile::copy(source, db.getAttachmentFileName(created.strGUID))
                    && db.setObjectDataDownloaded(created.strGUID, "attachment", true);
                if (ok) {
                    created.tInfoModified = QDateTime::fromSecsSinceEpoch(qint64(a.value("modified").toDouble()));
                    created.tDataModified = QDateTime::fromSecsSinceEpoch(qint64(a.value("dataModified").toDouble()));
                    ok = db.restoreImportedAttachmentDates(created);
                }
            }
        }
        for (const QJsonValue& tags : n.value("tags").toArray()) {
            if (!ok) break;
            if (importedTagRoot.isEmpty()) {
                WIZTAGDATA root;
                ok = db.createTag("", result.folder.mid(1, result.folder.size() - 2), "", root);
                if (!ok) break;
                importedTagRoot = root.strGUID;
            }
            QString parent = importedTagRoot, path;
            for (const QJsonValue& tag : tags.toArray()) {
                const QJsonObject t = tag.toObject();
                // JSON encoding avoids collisions between names containing separators.
                path += QString::fromUtf8(QJsonDocument(QJsonArray{t.value("name")}).toJson(QJsonDocument::Compact));
                if (!newTags.contains(path)) {
                    WIZTAGDATA created;
                    ok = db.createTag(parent, t.value("name").toString(), t.value("description").toString(), created);
                    if (!ok) break;
                    newTags.insert(path, created.strGUID);
                }
                parent = newTags.value(path);
            }
            if (ok) ok = db.insertDocumentTag(d, parent);
        }
        if (ok && n.value("style").isObject()) {
            const QJsonObject s = n.value("style").toObject();
            const QString key = QString::fromUtf8(QJsonDocument(s).toJson(QJsonDocument::Compact));
            if (!newStyles.contains(key)) {
                WIZSTYLEDATA style;
                ok = db.createStyle(result.folder.mid(1, result.folder.size() - 2) + " · " + s.value("name").toString(), s.value("description").toString(),
                    COLORREF(quint32(s.value("textColor").toDouble())), COLORREF(quint32(s.value("backColor").toDouble())),
                    s.value("bold").toBool(), s.value("flag").toInt(), style);
                if (ok) newStyles.insert(key, style.strGUID);
            }
            d.strStyleGUID = newStyles.value(key);
        }
        const QJsonObject params = n.value("params").toObject();
        for (auto it = params.begin(); ok && it != params.end(); ++it)
            ok = db.setDocumentParam(d.strGUID, it.key(), it.value().toString());
        if (ok) {
            db.updateDocumentAttachmentCount(d.strGUID, false);
            WIZDOCUMENTDATA actual;
            ok = db.documentFromGuid(d.strGUID, actual);
            if (ok) {
                actual.tCreated = QDateTime::fromSecsSinceEpoch(qint64(n.value("created").toDouble()));
                actual.tModified = QDateTime::fromSecsSinceEpoch(qint64(n.value("modified").toDouble()));
                actual.tAccessed = QDateTime::fromSecsSinceEpoch(qint64(n.value("accessed").toDouble()));
                actual.tDataModified = QDateTime::fromSecsSinceEpoch(qint64(n.value("dataModified").toDouble()));
                actual.strStyleGUID = d.strStyleGUID;
                actual.strTitle = n.value("title").toString();
                actual.strName = actual.strGUID + ".ziw";
                ok = db.modifyDocumentInfo(actual, false);
            }
        }
        if (!ok) {
            bool cleaned = true;
            if (!d.strGUID.isEmpty()) {
                CWizDocumentAttachmentDataArray added;
                cleaned = db.getDocumentAttachments(d.strGUID, added);
                for (const auto& a : added) cleaned = db.deleteAttachment(a, false, false) && cleaned;
                CWizDocumentParamDataArray parameters;
                cleaned = db.getDocumentParamsForExport(d.strGUID, parameters) && cleaned;
                for (const auto& p : parameters)
                    cleaned = db.removeDocumentParam(d.strGUID, p.strName) && cleaned;
                const QString path = db.getDocumentFileName(d.strGUID);
                if (QFileInfo::exists(path)) cleaned = QFile::remove(path) && cleaned;
                cleaned = db.deleteDocument(d, false) && cleaned;
            }
            if (!cleaned)
                result.error = QStringLiteral("导入失败且清理失败；请保留本次导入目录以检查。已导入 %1 条。").arg(result.imported);
            else result.error = QStringLiteral("当前笔记导入失败，已清理当前新副本。先前 %1 条保留，剩余未导入。").arg(result.imported);
            return result;
        }
        ++result.imported;
    }
    return result;
}

CWizDocumentDataArray chooseNotes(QWidget* parent, WizDatabase& db, const CWizDocumentDataArray& selected)
{
    CWizDocumentDataArray all, chosen;
    if (!db.getDocumentsByLocation("/", all, true)) return chosen;
    QDialog dialog(parent);
    dialog.setWindowTitle(QStringLiteral("导出原始笔记与文件夹"));
    dialog.resize(650, 550);
    QVBoxLayout layout(&dialog);
    QLabel label(QStringLiteral("勾选笔记或整个文件夹（含子文件夹）。仅当前个人资料库；团队/协作笔记暂不支持。\n"
        "原始包可在本实验版重新导入；未加密笔记附原始 HTML 视图。不是所有软件通用的格式。"));
    label.setWordWrap(true); layout.addWidget(&label);
    QTreeWidget tree; tree.setHeaderLabels({QStringLiteral("笔记 / 文件夹")}); layout.addWidget(&tree);
    QMap<QString, QTreeWidgetItem*> folders;
    auto* root = new QTreeWidgetItem(&tree, {QStringLiteral("当前个人资料库")});
    root->setFlags(root->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
    root->setCheckState(0, Qt::Unchecked); folders.insert("/", root);
    QSet<QString> initial;
    for (const auto& d : selected) if (d.strKbGUID == db.kbGUID()) initial.insert(d.strGUID);
    QMap<QString, WIZDOCUMENTDATA> documents;
    for (const auto& d : all) {
        if (WizDatabase::isInDeletedItems(d.strLocation)) continue;
        QString path = "/";
        QTreeWidgetItem* folder = root;
        for (const QString& name : d.strLocation.split('/', Qt::SkipEmptyParts)) {
            path += name + "/";
            if (!folders.contains(path)) {
                auto* item = new QTreeWidgetItem(folder, {name});
                item->setFlags(item->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsAutoTristate);
                item->setCheckState(0, Qt::Unchecked); folders.insert(path, item);
            }
            folder = folders.value(path);
        }
        auto* item = new QTreeWidgetItem(folder, {d.strTitle});
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setData(0, Qt::UserRole, d.strGUID);
        item->setCheckState(0, initial.contains(d.strGUID) ? Qt::Checked : Qt::Unchecked);
        documents.insert(d.strGUID, d);
    }
    root->setExpanded(true);
    QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    layout.addWidget(&buttons);
    QObject::connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    QObject::connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec() != QDialog::Accepted) return chosen;
    QTreeWidgetItemIterator it(&tree);
    while (*it) {
        const QString guid = (*it)->data(0, Qt::UserRole).toString();
        if ((*it)->checkState(0) == Qt::Checked && documents.contains(guid)) chosen.push_back(documents.value(guid));
        ++it;
    }
    return chosen;
}

void showImport(QWidget* parent, WizDatabase& db)
{
    const QString file = QFileDialog::getOpenFileName(parent, QStringLiteral("导入原始笔记包"), {},
        QStringLiteral("WizNotePlus 原始笔记包 (*.wiznotes)"));
    if (file.isEmpty()) return;
    if (QMessageBox::question(parent, QStringLiteral("导入为新副本"), QStringLiteral(
        "将在当前个人账号中新建“导入-日期”文件夹，保留原目录层级并生成新的笔记编号。\n"
        "不会覆盖已有笔记；再次导入会新增副本。导入的笔记可能随后同步到当前账号云端。\n"
        "仅导入可信来源的包；原始 HTML 中的脚本与外部链接未做清理。\n"
        "本包格式仅保证本实验版使用，不能将任意 ZIP 或官方格式冒充本包。\n\n是否继续？"),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    const ImportResult result = importNotes(db, file);
    if (!result.error.isEmpty()) QMessageBox::warning(parent, QStringLiteral("导入未完成"), result.error);
    else QMessageBox::information(parent, QStringLiteral("导入完成"),
        QStringLiteral("已导入 %1 条笔记到 %2\n原笔记未覆盖。请检查内容后再同步。").arg(result.imported).arg(result.folder));
}
}
