#include "WizNativeNoteTransfer.h"
#include "share/WizNativeNoteArchive.h"
#include "database/WizDatabase.h"
#include "database/WizDatabaseManager.h"
#include "share/WizEnc.h"
#include "share/WizZiwReader.h"
#include "share/WizLocalFileUrl.h"
#include "share/WizMarkdownExportTransaction.h"
#include "utils/WizMisc.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QBuffer>
#include <QImage>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <quazip.h>
#include <quazipfile.h>
#include <cryptopp/osrng.h>
#include <cryptopp/rsa.h>
#include <sstream>

namespace {
bool put(const QString& path, const QByteArray& bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray get(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool zipFiles(const QString& path, const QList<QPair<QString, QByteArray>>& files,
              bool link = false, bool stored = false)
{
    QuaZip zip(path); zip.setFileNameCodec("UTF-8");
    if (!zip.open(QuaZip::mdCreate)) return false;
    for (const auto& pair : files) {
        QuaZipFile file(&zip);
        QuaZipNewInfo info(pair.first);
        if (link) { info.externalAttr = 0120777U << 16; }
        if (!file.open(QIODevice::WriteOnly, info, nullptr, 0, stored ? 0 : Z_DEFLATED)
            || file.write(pair.second) != pair.second.size()) return false;
        file.close(); if (file.getZipError() != UNZ_OK) return false;
    }
    zip.close(); return zip.getZipError() == UNZ_OK;
}
int count(WizDatabase& db)
{
    CWizDocumentDataArray all;
    return db.getDocumentsByLocation("/", all, true) ? int(all.size()) : -1;
}
}

int wizNativeNoteTransferSelfTest(int argc, char** argv)
{
    // Construct the isolation boundary before QApplication or any database/path resolver.
    // No login, sockets, cloud synchronisation, or real user account is opened.
    QTemporaryDir root;
    if (!root.isValid()) return 1;
    QDir().mkpath(root.filePath("tmp"));
    qputenv("WIZNOTE_DATA_STORE", root.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", root.filePath("cache").toUtf8());
    qputenv("TMPDIR", root.filePath("tmp").toUtf8());
    QApplication app(argc, argv);
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        failures += !ok; return ok;
    };
    using namespace WizNativeNotes;
    QString error;
    const QString localPath = root.filePath(QStringLiteral("导出 目录 #50%/"));
    const QUrl localUrl = WizLocalFileUrl(localPath);
    check(localUrl.isLocalFile() && localUrl.scheme() == "file"
        && localUrl.toLocalFile() == QDir(localPath).absolutePath()
        && localUrl.fragment().isEmpty(), "export directory URL preserves Unicode, spaces, hash and percent");
    check(safePath(QStringLiteral("笔记/图像.png")), "Unicode relative path");
    for (const QString& path : {"../escape", "/absolute", "a/../b", "a\\b", "a:b", "a//b", "a/./b", "a./b", "a /b"})
        check(!safePath(path), "reject unsafe path");
    const QByteArray html = QStringLiteral("<html><body><h1>中文原始笔记</h1><img src=\"index_files/image.png\"><p>\\(x^2\\)</p></body></html>").toUtf8();
    QByteArray image;
    QBuffer imageBuffer(&image); imageBuffer.open(QIODevice::WriteOnly);
    QImage pixel(1, 1, QImage::Format_RGB32); pixel.fill(Qt::red);
    if (!check(pixel.save(&imageBuffer, "PNG"), "valid synthetic PNG fixture")) return 1;
    const QString ziw = root.filePath("synthetic.ziw");
    if (!check(zipFiles(ziw, {{"index.html", html}, {"index_files/image.png", image}, {"empty", {}}})
        && validateZiw(ziw, error), "valid synthetic native container")) return 1;
    const QByteArray original = get(ziw);
    const QString corruptZiw = root.filePath("bad-crc.ziw");
    bool corrupted = zipFiles(corruptZiw, {{"index.html", html}}, false, true);
    QByteArray corruptBytes = get(corruptZiw);
    const int offset = corruptBytes.indexOf(html);
    if (offset >= 0) corruptBytes[offset] = 'X';
    check(corrupted && offset >= 0 && put(corruptZiw, corruptBytes)
        && !validateZiw(corruptZiw, error), "reject native container with deliberately corrupted CRC");
    const QString simple = root.filePath("simple.wiznotes");
    check(writeArchive(simple, {{"documents", QJsonArray{}}}, {{"notes/0/note.ziw", ziw}}, error), "write native envelope");
    {
        QTemporaryDir staging(root.filePath("stage-XXXXXX")); QJsonObject metadata;
        check(readArchive(simple, staging.path(), metadata, error)
            && get(staging.filePath("notes/0/note.ziw")) == original, "envelope round trip preserves exact bytes");
    }
    check(!writeArchive(simple, {}, {{"notes/0/note.ziw", ziw}}, error), "existing export never replaced");
    for (const auto& attack : QList<QPair<QString, QList<QPair<QString, QByteArray>>>>{
        {"traversal", {{"../escape", "bad"}}},
        {"case-duplicate", {{"A", "one"}, {"a", "two"}}},
        {"normalized-duplicate", {{QString::fromUtf8("é"), "one"}, {QString::fromUtf8("e\xcc\x81"), "two"}}},
        {"missing-manifest", {{"notes/a", "bad"}}},
        {"symlink", {{"notes/link", "../escape"}}}
    }) {
        const QString file = root.filePath(attack.first + ".zip");
        QTemporaryDir staging(root.filePath("stage-XXXXXX")); QJsonObject metadata;
        check(zipFiles(file, attack.second, attack.first == "symlink")
            && !readArchive(file, staging.path(), metadata, error), "reject malicious or unsupported ZIP before database writes");
    }
    QJsonObject badManifest{{"format", "WizNotePlus-native-notes"}, {"version", 1}, {"notes", QJsonObject{}},
        {"files", QJsonArray{QJsonObject{{"path", "notes/a"}, {"size", 3}, {"sha256", QString(64, '0')}}}}};
    const QString badHash = root.filePath("bad-hash.wiznotes");
    check(zipFiles(badHash, {{"notes/a", "bad"}, {"manifest.json", QJsonDocument(badManifest).toJson()}}), "build deliberate hash-mismatch fixture");
    {
        QTemporaryDir staging(root.filePath("stage-XXXXXX")); QJsonObject metadata;
        check(!readArchive(badHash, staging.path(), metadata, error), "reject hash mismatch");
    }
    const QString extra = root.filePath("unlisted.zip");
    badManifest["files"] = QJsonArray{};
    check(zipFiles(extra, {{"extra", "bad"}, {"manifest.json", QJsonDocument(badManifest).toJson()}}), "build unlisted-file fixture");
    {
        QTemporaryDir staging(root.filePath("stage-XXXXXX")); QJsonObject metadata;
        check(!readArchive(extra, staging.path(), metadata, error), "reject unlisted payload");
    }
    WizDatabaseManager manager("synthetic-offline-account");
    if (!check(manager.open(), "open fresh isolated personal database")) return 1;
    WizDatabase& db = manager.db(); db.setKbGUID("synthetic-kb");
    WIZDOCUMENTDATA first, second;
    if (!check(db.createDocument(QStringLiteral("同名笔记"), "source.ziw", QStringLiteral("/中文文件夹/子目录/"), "https://example.invalid/original", 0, first)
        && QFile::copy(ziw, db.getDocumentFileName(first.strGUID))
        && db.setObjectDataDownloaded(first.strGUID, "document", true)
        && db.updateDocumentDataMD5(first, db.getDocumentFileName(first.strGUID)), "create offline synthetic note with image")) return 1;
    {
        // Actual database + actual stored ZIW extraction, not the independent
        // source-flow adapter. All paths remain under the pre-app isolation root.
        // No transaction prototype is linked into this main-client self-test.
        const QString destinationRoot = root.filePath("legacy-single-export/");
        if (!QDir().mkpath(destinationRoot)) return 1;
        const QString destination = destinationRoot + QStringLiteral("单笔记 A&B #50%.md");
        const QString resourceName = Utils::WizMisc::extractFileTitle(destination).toHtmlEscaped() + "_files/";
        QByteArray expected = QByteArray::fromHex("efbbbf") + html;
        expected.replace("index_files/", resourceName.toUtf8());
        const QByteArray noteBefore = get(db.getDocumentFileName(first.strGUID));
        const int noteCountBefore = count(db);
        check(db.exportToHtmlFile(first, destination), "actual isolated DB single-export extracts the original synthetic ZIW");
        check(get(destination) == expected, "actual DB single-export preserves BOM and legacy escaped HTML resource paths");
        check(get(destinationRoot + resourceName + "image.png") == image,
              "actual DB single-export preserves stored image bytes");
        WIZDOCUMENTDATA missing = first;
        missing.strGUID = "missing-synthetic-export-note";
        const QString early = destinationRoot + "early.md";
        if (!put(early, "OLD EARLY EXPORT")) return 1;
        check(!db.exportToHtmlFile(missing, early) && get(early) == "OLD EARLY EXPORT",
              "actual missing-ZIW extraction failure preserves existing single-export target");
        const QString late = destinationRoot + "late.md";
        const QString blockedResources = destinationRoot + "late_files";
        if (!put(late, "OLD LATE EXPORT") || !put(blockedResources, "SYNTHETIC RESOURCE DIRECTORY BLOCKER")) return 1;
        check(!db.exportToHtmlFile(first, late), "actual DB reports real resource-directory creation failure after ZIW extraction");
        QByteArray lateExpected = QByteArray::fromHex("efbbbf") + html;
        lateExpected.replace("index_files/", "late_files/");
        check(get(late) == lateExpected && get(blockedResources) == "SYNTHETIC RESOURCE DIRECTORY BLOCKER",
              "known legacy late failure leaves HTML intermediate but does not overwrite resource-directory blocker");
        check(get(db.getDocumentFileName(first.strGUID)) == noteBefore && count(db) == noteCountBefore,
              "actual single-export success and failures never change stored original ZIW or note count");
    }
    {
        // Product Markdown transaction with the actual temporary database and
        // stored synthetic ZIW. JavaScript reply/lifetime boundaries remain in
        // the separate browser-flow test.
        const QString destinationRoot = root.filePath("transaction-single-export/");
        if (!QDir().mkpath(destinationRoot)) return 1;
        const QString destination = destinationRoot + QStringLiteral("事务 A&B.md");
        const QString resources = destinationRoot + QStringLiteral("事务 A&amp;B_files/");
        const QByteArray previousMarkdown("OLD TRANSACTION MARKDOWN");
        const QByteArray previousImage("OLD TRANSACTION IMAGE");
        if (!put(destination, previousMarkdown) || !put(resources + "image.png", previousImage)
            || !put(resources + ".hidden-resource", "KEEP HIDDEN RESOURCE")) return 1;
        const QByteArray noteBefore = get(db.getDocumentFileName(first.strGUID));
        const int noteCountBefore = count(db);
        QString transactionError;
        const QString markdown = QString::fromUtf8("# 新正文\n![图](index_files/image.png)\n");
        QString expectedMarkdown = markdown;
        expectedMarkdown.replace("index_files/", QStringLiteral("事务 A&amp;B_files/"));
        check(WizMarkdownExportTransaction::write(destination, markdown,
            [&](const QString& staged) { return db.exportToHtmlFile(first, staged); }, transactionError)
            && transactionError.isEmpty(), "product Markdown transaction exports actual stored synthetic ZIW");
        check(get(destination) == expectedMarkdown.toUtf8(),
              "product transaction commits exact Unicode Markdown without BOM");
        check(get(resources + "image.png") == image && get(resources + ".hidden-resource") == "KEEP HIDDEN RESOURCE",
              "product transaction installs actual image and preserves unrelated hidden resource");

        WIZDOCUMENTDATA missing = first;
        missing.strGUID = "missing-transaction-note";
        if (!put(destination, previousMarkdown) || !put(resources + "image.png", previousImage)) return 1;
        check(!WizMarkdownExportTransaction::write(destination, "NEW EARLY",
            [&](const QString& staged) { return db.exportToHtmlFile(missing, staged); }, transactionError)
            && get(destination) == previousMarkdown && get(resources + "image.png") == previousImage,
              "actual missing-ZIW transaction failure preserves previous Markdown and resource");

        if (!put(destination, previousMarkdown) || !put(resources + "image.png", previousImage)) return 1;
        check(!WizMarkdownExportTransaction::write(destination, "NEW LATE",
            [&](const QString& staged) {
                const QString stagedResources = Utils::WizMisc::extractFilePath(staged)
                    + Utils::WizMisc::extractFileTitle(staged).toHtmlEscaped() + "_files";
                return put(stagedResources, "SYNTHETIC STAGED RESOURCE BLOCKER")
                    && db.exportToHtmlFile(first, staged);
            }, transactionError)
            && get(destination) == previousMarkdown && get(resources + "image.png") == previousImage,
              "actual late resource failure stays in staging and preserves previous export");
        check(QDir(destinationRoot).entryList(QStringList{".wiz-export-*"},
            QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
              "product transaction success and ordinary failures clean private staging directories");
        check(get(db.getDocumentFileName(first.strGUID)) == noteBefore && count(db) == noteCountBefore,
              "product Markdown transaction never changes stored original ZIW or note count");
    }
    const QString attachSource = root.filePath(QStringLiteral("附件.txt"));
    const QByteArray attachment("original attachment\n");
    WIZDOCUMENTATTACHMENTDATA a;
    check(put(attachSource, attachment) && db.addAttachment(first, attachSource, a), "add original attachment");
    a.tInfoModified = QDateTime::fromSecsSinceEpoch(1690000000);
    a.tDataModified = QDateTime::fromSecsSinceEpoch(1690000010);
    check(db.restoreImportedAttachmentDates(a), "set attachment source timestamps");
    WIZTAGDATA top, child;
    check(db.createTag("", QStringLiteral("标签"), "parent description", top)
        && db.createTag(top.strGUID, QStringLiteral("子标签"), "child description", child)
        && db.insertDocumentTag(first, child.strGUID), "hierarchical tags");
    WIZSTYLEDATA style;
    check(db.createStyle("Synthetic style", "description", RGB(12, 34, 56), RGB(210, 220, 230), true, 2, style), "synthetic style");
    check(db.setDocumentParam(first.strGUID, "CUSTOM", QStringLiteral("中文参数 ' quoted"))
        && db.setDocumentParam(first.strGUID, "DOCUMENT_FLAGS", "2")
        && db.setDocumentParam(first.strGUID, "RATE", "3"), "parameters including quoted Unicode text");
    db.documentFromGuid(first.strGUID, first);
    first.strStyleGUID = style.strGUID; first.strAuthor = "Synthetic author"; first.strKeywords = "one,two";
    first.tCreated = QDateTime::fromSecsSinceEpoch(1700000000);
    first.tModified = QDateTime::fromSecsSinceEpoch(1700000010);
    first.tAccessed = QDateTime::fromSecsSinceEpoch(1700000020);
    first.tDataModified = QDateTime::fromSecsSinceEpoch(1700000030);
    check(db.modifyDocumentInfo(first, false), "original note metadata dates");
    check(db.createDocument(QStringLiteral("同名笔记"), "second.ziw", "/another/", "", 0, second)
        && QFile::copy(ziw, db.getDocumentFileName(second.strGUID))
        && db.setObjectDataDownloaded(second.strGUID, "document", true)
        && db.updateDocumentDataMD5(second, db.getDocumentFileName(second.strGUID)), "second note in different folder");
    const QString exported = root.filePath("notes.wiznotes");
    {
        const QString portable = root.filePath(QStringLiteral("通用 HTML #50%"));
        const QString relative = QStringLiteral("Notes/中文文件夹/子目录/000001-同名笔记");
        check(exportPortableHtml(db, {first, second}, portable, error), "portable HTML export publishes a new folder");
        check(get(portable + "/" + relative + "/index.html") == html
            && get(portable + "/" + relative + "/index_files/image.png") == image,
            "portable body and relative image resources preserve original bytes");
        check(get(portable + "/" + relative + QStringLiteral("/_WizNote-attachments/0001-附件.txt")) == attachment
            && get(portable + "/" + relative + "/_WizNote-attachments/index.html").contains("%E9%99%84%E4%BB%B6.txt"),
            "portable named attachments and URI-escaped links");
        const QByteArray catalog = get(portable + QStringLiteral("/浏览笔记.html"));
        check(catalog.contains("index.html") && catalog.contains("_WizNote-attachments")
            && get(portable + "/note-metadata.json").contains("sourceGuid"), "portable catalog and note-only metadata");
        check(!exportPortableHtml(db, {first}, portable, error)
            && get(portable + QStringLiteral("/浏览笔记.html")) == catalog, "portable destination is never replaced");
        WIZDOCUMENTDATA originalSecond = second;
        second.strLocation = first.strLocation; second.strTitle = first.strTitle;
        check(db.modifyDocumentInfo(second, false), "same-folder duplicate-title fixture");
        const QString duplicate = root.filePath("duplicate-titles");
        check(exportPortableHtml(db, {first, second}, duplicate, error)
            && get(duplicate + "/" + relative + "/index.html") == html
            && get(duplicate + QStringLiteral("/Notes/中文文件夹/子目录/000002-同名笔记/index.html")) == html,
            "same-folder duplicate titles produce independent note/resource folders");
        second = originalSecond;
        check(db.modifyDocumentInfo(second, false), "restore original second-note fixture");
        const QString emptySelection = root.filePath("empty-export");
        check(!exportPortableHtml(db, {}, emptySelection, error) && !QFileInfo::exists(emptySelection),
            "portable failure does not publish a partial destination");
        WIZDOCUMENTDATA unusual = first;
        unusual.strTitle = QStringLiteral("<标题 & #50% / 文件>");
        check(db.modifyDocumentInfo(unusual, false), "HTML-special and filename-special title fixture");
        const QString special = root.filePath("special-title");
        check(exportPortableHtml(db, {unusual}, special, error)
            && get(special + QStringLiteral("/浏览笔记.html")).contains("&lt;")
            && get(special + QStringLiteral("/浏览笔记.html")).contains("%2350%25")
            && get(special + QStringLiteral("/浏览笔记.html")).contains("&amp;"),
            "catalog escapes title markup and percent-encodes filename links");
        check(db.modifyDocumentInfo(first, false), "restore original first-note fixture");
    }
    if (!check(exportNotes(db, {first, second}, exported, error), "export two notes and folders")) {
        qWarning().noquote() << error; return 1;
    }
    {
        QTemporaryDir staging(root.filePath("stage-XXXXXX")); QJsonObject metadata;
        check(readArchive(exported, staging.path(), metadata, error)
            && get(staging.filePath("notes/0/html/index.html")) == html
            && get(staging.filePath("notes/0/html/index_files/image.png")) == image,
            "portable HTML view and image preserve original bytes");
        QJsonArray notes = metadata.value("documents").toArray();
        QJsonObject invalid = notes.last().toObject(); invalid["location"] = "/../escape/";
        notes[notes.size() - 1] = invalid; metadata["documents"] = notes;
        const QString invalidPath = root.filePath("bad-metadata.wiznotes");
        // Native-note validation is all-or-nothing even when the first note is valid.
        check(writeArchive(invalidPath, metadata, {{"notes/0/note.ziw", ziw}, {"notes/1/note.ziw", ziw},
            {"notes/0/attachments/0.bin", attachSource}}, error), "build invalid second-note fixture");
        const int before = count(db);
        const ImportResult rejected = importNotes(db, invalidPath);
        check(!rejected.error.isEmpty() && rejected.imported == 0 && count(db) == before,
            "all note metadata preflight before any database write");
    }
    const ImportResult imported = importNotes(db, exported);
    check(imported.error.isEmpty() && imported.imported == 2, "import note/folder package as new copies");
    if (!imported.error.isEmpty()) { qWarning().noquote() << imported.error; return 1; }
    CWizDocumentDataArray copies;
    db.getDocumentsByLocation(imported.folder, copies, true);
    check(copies.size() == 2 && count(db) == 4, "new import folder contains both notes");
    for (const auto& copy : copies) {
        check(copy.strGUID != first.strGUID && copy.strGUID != second.strGUID
            && get(db.getDocumentFileName(copy.strGUID)) == original
            && db.isObjectDataDownloaded(copy.strGUID, "document") && copy.nVersion < 0,
            "new identity; original ZIW bytes; downloaded and sync-eligible");
        if (copy.strLocation.endsWith(QStringLiteral("中文文件夹/子目录/"))) {
            check(copy.strTitle == first.strTitle && copy.strURL == first.strURL
                && copy.strAuthor == first.strAuthor && copy.strKeywords == first.strKeywords
                && copy.tCreated == first.tCreated && copy.tModified == first.tModified
                && copy.tAccessed == first.tAccessed && copy.tDataModified == first.tDataModified
                && copy.nFlags == 2 && copy.nRate == 3, "metadata, original dates, flags and rating round trip");
            WIZDOCUMENTPARAMDATA param;
            check(db.getDocumentParam(copy.strGUID, "CUSTOM", param)
                && param.strValue == QStringLiteral("中文参数 ' quoted"), "custom parameter round trip");
            WIZSTYLEDATA restored;
            check(db.styleFromGuid(copy.strStyleGUID, restored) && restored.crTextColor == style.crTextColor
                && restored.crBackColor == style.crBackColor && restored.bTextBold == style.bTextBold,
                "style colours and bold round trip");
            CWizDocumentAttachmentDataArray attachments;
            check(db.getDocumentAttachments(copy.strGUID, attachments) && attachments.size() == 1
                && get(db.getAttachmentFileName(attachments[0].strGUID)) == attachment
                && attachments[0].strName == a.strName && attachments[0].tInfoModified == a.tInfoModified
                && attachments[0].tDataModified == a.tDataModified, "attachment bytes, name and dates round trip");
            CWizTagDataArray tags;
            WIZTAGDATA parent;
            check(db.getDocumentTags(copy.strGUID, tags) && tags.size() == 1 && tags[0].strName == child.strName
                && db.tagFromGuid(tags[0].strParentGUID, parent) && parent.strName == top.strName,
                "tag hierarchy round trip under independent import namespace");
        }
    }
    const ImportResult repeat = importNotes(db, exported);
    check(repeat.error.isEmpty() && repeat.imported == 2 && repeat.folder != imported.folder && count(db) == 6,
        "repeated import adds independent copies, never overwrites");
    const int beforeCorrupt = count(db);
    check(!importNotes(db, badHash).error.isEmpty() && count(db) == beforeCorrupt,
        "corrupt archive leaves database untouched");
    check(db.execSQL("CREATE TEMP TRIGGER reject_import_attachment BEFORE INSERT ON WIZ_DOCUMENT_ATTACHMENT "
        "BEGIN SELECT RAISE(ABORT, 'synthetic import failure'); END"), "inject isolated attachment write failure");
    const ImportResult failed = importNotes(db, exported);
    check(!failed.error.isEmpty() && failed.imported == 0 && count(db) == beforeCorrupt,
        "database failure cleans current new note; original notes remain");
    db.execSQL("DROP TRIGGER reject_import_attachment");
    check(get(db.getDocumentFileName(first.strGUID)) == original
        && get(db.getAttachmentFileName(a.strGUID)) == attachment, "existing original payloads remain unchanged");
    check(db.setObjectDataDownloaded(a.strGUID, "attachment", false)
        && !exportNotes(db, {first}, root.filePath("missing.wiznotes"), error)
        && !QFileInfo::exists(root.filePath("missing.wiznotes")), "missing attachment prevents incomplete export");
    db.setObjectDataDownloaded(a.strGUID, "attachment", true);
    const QString partialPackage = root.filePath("partial.wiznotes");
    check(exportNotes(db, {second, first}, partialPackage, error), "export partial-failure fixture");
    db.execSQL("CREATE TEMP TRIGGER reject_import_attachment BEFORE INSERT ON WIZ_DOCUMENT_ATTACHMENT "
        "BEGIN SELECT RAISE(ABORT, 'synthetic import failure'); END");
    const ImportResult partial = importNotes(db, partialPackage);
    check(!partial.error.isEmpty() && partial.imported == 1 && count(db) == beforeCorrupt + 1,
        "partial failure reports completed count and retains only preceding completed note");
    db.execSQL("DROP TRIGGER reject_import_attachment");

    // Ephemeral test keys only. Legacy ZIWR has a fixed 128-byte RSA field; this
    // exercises that unchanged format, not a recommendation of 1024-bit RSA.
    CryptoPP::AutoSeededRandomPool rng;
    CryptoPP::RSA::PrivateKey key; key.GenerateRandomWithKeySize(rng, 1024);
    auto decimal = [](const CryptoPP::Integer& integer) {
        std::ostringstream output; output << integer;
        return QString::fromStdString(output.str());
    };
    const QString n = decimal(key.GetModulus()), e = decimal(key.GetPublicExponent());
    const QString privatePart = decimal(key.GetPrivateExponent());
    const QString password = "synthetic-test-password-only";
    QString encryptedPart;
    if (!check(WizAESEncryptStringToBase64String(password, privatePart, encryptedPart)
        && db.setUserCert(n, e, encryptedPart, "synthetic") && db.verifyCertPassword(password),
        "ephemeral isolated encryption certificate")) return 1;
    WizZiwReader reader; reader.setDatabase(&db);
    reader.setRSAKeys(n.toUtf8(), e.toUtf8(), encryptedPart.toUtf8(), "synthetic");
    const QString encryptedZiw = root.filePath("encrypted.ziw");
    WIZDOCUMENTDATA secret;
    if (!check(reader.encryptFileToFile(ziw, encryptedZiw)
        && db.createDocument("synthetic encrypted note", "encrypted.ziw", "/encrypted/", "", 1, secret)
        && QFile::copy(encryptedZiw, db.getDocumentFileName(secret.strGUID))
        && db.setObjectDataDownloaded(secret.strGUID, "document", true)
        && db.WizIndex::updateDocumentDataMD5(secret, db.getDocumentFileName(secret.strGUID)),
        "synthetic encrypted native note")) return 1;
    const QString encryptedPackage = root.filePath("encrypted.wiznotes");
    const QString encryptedPortable = root.filePath("encrypted-html");
    check(!exportPortableHtml(db, {first, secret}, encryptedPortable, error)
        && !QFileInfo::exists(encryptedPortable), "mixed encrypted selection refuses plaintext portable export without partial output");
    check(exportNotes(db, {secret}, encryptedPackage, error), "encrypted note exported without decryption");
    {
        QTemporaryDir staging(root.filePath("stage-XXXXXX")); QJsonObject metadata;
        check(readArchive(encryptedPackage, staging.path(), metadata, error)
            && !metadata.value("documents").toArray().first().toObject().contains("html")
            && !QJsonDocument(metadata).toJson().contains(encryptedPart.toUtf8())
            && !QJsonDocument(metadata).toJson().contains(password.toUtf8()),
            "encrypted export contains no plaintext HTML, certificate private key or password");
    }
    const ImportResult secretResult = importNotes(db, encryptedPackage);
    CWizDocumentDataArray secretCopies;
    db.getDocumentsByLocation(secretResult.folder, secretCopies, true);
    QByteArray decrypted;
    check(secretResult.error.isEmpty() && secretResult.imported == 1 && secretCopies.size() == 1
        && get(db.getDocumentFileName(secretCopies[0].strGUID)) == get(encryptedZiw)
        && reader.decryptFileToData(db.getDocumentFileName(secretCopies[0].strGUID), decrypted)
        && decrypted == original, "same-certificate encrypted import preserves bytes and decrypts to original ZIW");
    const int beforeMismatch = count(db);
    db.setUserCert("different-synthetic-certificate", e, encryptedPart, "synthetic");
    const ImportResult mismatch = importNotes(db, encryptedPackage);
    check(!mismatch.error.isEmpty() && mismatch.imported == 0 && count(db) == beforeMismatch,
        "different certificate rejects encrypted import before writes");
    WizDatabase::clearCertPassword();
    qInfo() << "Synthetic native-note transfer failures:" << failures;
    return failures ? 1 : 0;
}
