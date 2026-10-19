#include <QCoreApplication>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUuid>
#include <QDebug>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
#include "WizQtCompat.h"

using CString = QString; // Only QString operations occur in extracted path code.
struct WIZDOCUMENTDATA {};
bool WizCopyFolder(const QString&, const QString&, bool);
bool WizLoadUnicodeTextFromFile(const QString&, QString&, const char* = "UTF-8");
bool WizSaveUnicodeTextToUtf8File(const QString&, const QString&);
namespace Utils {
struct WizMisc {
    static QString extractFileName(const QString&);
    static QString extractFileTitle(const QString&);
    static CString extractFilePath(const CString&);
};
struct WizPathResolve {
    static QString root;
    static QString tempPath() { return root; }
};
QString WizPathResolve::root;
}
QString WizGenGUIDLowerCaseLetterOnly()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).remove('-');
}
struct WizDatabase {
    QString fixture;
    bool failBeforeExtraction = false;
    bool makeLastExtractedResourceUnreadable = false;
    QString unreadableResource;
    bool extractZiwFileToFolder(const WIZDOCUMENTDATA&, const QString& path)
    {
        if (failBeforeExtraction) return false;
        // This is the ONLY database/archive adapter: copy known synthetic bytes
        // with the actual helper, then optionally alter a temporary permission.
        if (!QDir().mkpath(path) || !WizCopyFolder(fixture, path, true)) return false;
        if (makeLastExtractedResourceUnreadable) {
            unreadableResource = path + "index_files/z-fails.png";
            return QFile::setPermissions(unreadableResource, QFileDevice::Permissions());
        }
        return true;
    }
    bool exportToHtmlFile(const WIZDOCUMENTDATA&, const QString&);
};
#include "actual-resources.inc"
#include "transaction-prototype.h"

static QByteArray read(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
static bool write(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid()) return 2;
    const bool requireSafe = app.arguments().contains("--require-safe");
    const bool transaction = app.arguments().contains("--transaction-prototype");
    const bool compatibility = app.arguments().contains("--transaction-compatibility");
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* text) {
        ++checks; failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << text;
    };
    auto path = [&](const QString& relative) { return temporary.filePath(relative); };
    Utils::WizPathResolve::root = path("extracted/");
    WizDatabase database;
    database.fixture = path("fixture/");
    if (!QDir().mkpath(database.fixture + "index_files/")
        || !QDir().mkpath(path("out")) || !QDir().mkpath(path("extracted"))) return 2;
    const QByteArray html("<html><img src='index_files/a-first.png'><img src='index_files/z-fails.png'></html>");
    if (!write(database.fixture + "index.html", html)
        || !write(database.fixture + "index_files/a-first.png", "NEW A")
        || !write(database.fixture + "index_files/z-fails.png", "NEW Z")) return 2;
    const WIZDOCUMENTDATA document;
    if (compatibility) {
        const QStringList names{QStringLiteral("previous-resources"), QStringLiteral("returned-resources"),
            QStringLiteral(".hidden.md"), QString::fromUtf8("中文 & <x>.md")};
        for (const QString& name : names) {
            const QString target = path("out/") + name;
            const QString resources = path("out/") + Utils::WizMisc::extractFileTitle(target).toHtmlEscaped() + "_files/";
            if (!QDir().mkpath(resources + ".hidden-directory/") || !write(target, "OLD MARKDOWN")
                || !write(resources + "a-first.png", "OLD A") || !write(resources + "z-fails.png", "OLD Z")
                || !write(resources + ".hidden-image", "KEEP HIDDEN IMAGE")
                || !write(resources + ".hidden-directory/nested-image", "KEEP HIDDEN DIRECTORY")) return 2;
            QString error;
            const bool ok = ExportTransactionPrototype::write(target, "![image](index_files/a-first.png)",
                [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error);
            const QByteArray expected = ("![image](" + Utils::WizMisc::extractFileTitle(target).toHtmlEscaped() + "_files/a-first.png)").toUtf8();
            check(ok && error.isEmpty(), qPrintable("compatibility success for basename " + name));
            check(read(target) == expected && read(resources + "a-first.png") == "NEW A",
                  qPrintable("compatibility exact output for basename " + name));
            check(read(resources + ".hidden-image") == "KEEP HIDDEN IMAGE"
                && read(resources + ".hidden-directory/nested-image") == "KEEP HIDDEN DIRECTORY",
                  qPrintable("compatibility preserves unrelated hidden files for basename " + name));
        }
        const QString target = path("out/returned-resources");
        const QString resources = path("out/returned-resources_files/");
        if (!write(target, "OLD MARKDOWN") || !write(resources + "a-first.png", "OLD A")
            || !write(resources + "z-fails.png", "OLD Z")) return 2;
        QString error;
        const bool ok = ExportTransactionPrototype::write(target, "NEW SOURCE",
            [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error, true);
        check(!ok && read(target) == "OLD MARKDOWN", "reserved-looking basename failed commit preserves original Markdown");
        check(read(resources + "a-first.png") == "OLD A" && read(resources + "z-fails.png") == "OLD Z",
              "reserved-looking basename failed commit restores original resource directory");
        const QString linked = path("out/linked.md");
        const QString linkedResources = path("out/linked_files/");
        const QString external = path("outside-export-image");
        if (!QDir().mkpath(linkedResources) || !write(linked, "OLD LINKED MARKDOWN") || !write(external, "UNCHANGED OUTSIDE EXPORT")) return 2;
        check(QFile::link(external, linkedResources + "unknown-link"), "synthetic old-resource symlink created");
        check(!ExportTransactionPrototype::write(linked, "NEW SOURCE",
            [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error)
            && read(linked) == "OLD LINKED MARKDOWN", "unsupported old-resource symlink refuses without changing Markdown");
        check(QFileInfo(linkedResources + "unknown-link").isSymLink() && read(external) == "UNCHANGED OUTSIDE EXPORT",
              "refused resource symlink remains intact and its target is untouched");
#ifdef Q_OS_UNIX
        const QString special = path("out/special.md");
        const QString specialResources = path("out/special_files/");
        if (!QDir().mkpath(specialResources) || !write(special, "OLD SPECIAL MARKDOWN")) return 2;
        check(::mkfifo(QFile::encodeName(specialResources + "unknown-fifo").constData(), 0600) == 0,
              "synthetic nonregular resource fixture created");
        check(!ExportTransactionPrototype::write(special, "NEW SOURCE",
            [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error)
            && read(special) == "OLD SPECIAL MARKDOWN", "nonregular old resource refuses without opening it or changing Markdown");
#endif
        check(QDir(path("out")).entryList(QStringList{".wiz-export-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty(),
              "all compatibility success/refusal paths clean only their own staging directories");
        qInfo().noquote() << QJsonDocument(QJsonObject{{"diagnosticOnly", true}, {"productModified", false},
            {"transactionCompatibility", true}, {"archiveExtractionIsAdapter", true},
            {"checks", checks}, {"failures", failures}, {"transactionPrototypeSHA256", TRANSACTION_SOURCE_SHA}
        }).toJson(QJsonDocument::Compact);
        return failures ? 1 : 0;
    }
    if (transaction) {
        const QString target = path(QString::fromUtf8("out/定理 A&B.md"));
        const QString resources = path("out/") + Utils::WizMisc::extractFileTitle(target).toHtmlEscaped() + "_files/";
        const QByteArray sentinel("ORIGINAL MARKDOWN\n");
        auto reset = [&] {
            database.failBeforeExtraction = false;
            database.makeLastExtractedResourceUnreadable = false;
            return QDir().mkpath(resources) && write(target, sentinel)
                && write(resources + "a-first.png", "OLD A")
                && write(resources + "z-fails.png", "OLD Z")
                && write(resources + "unrelated.png", "KEEP UNRELATED");
        };
        auto unchanged = [&] {
            return read(target) == sentinel && read(resources + "a-first.png") == "OLD A"
                && read(resources + "z-fails.png") == "OLD Z"
                && read(resources + "unrelated.png") == "KEEP UNRELATED";
        };
        auto invoke = [&](const QString& source, QString& error, bool cancel = false) {
            return ExportTransactionPrototype::write(target, source,
                [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error, cancel);
        };
        auto noStageLeft = [&] {
            return QDir(path("out")).entryList(QStringList{".wiz-export-*"}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty();
        };
        QString error;
        if (!reset()) return 2;
        database.failBeforeExtraction = true;
        check(!invoke("NEW SOURCE", error) && !error.isEmpty(), "prototype reports actual export-method early failure");
        check(unchanged(), "early failure preserves entire original synthetic export");
        check(noStageLeft(), "early failure cleans only its temporary staging directory");
        if (!reset()) return 2;
        database.makeLastExtractedResourceUnreadable = true;
        const bool result = invoke("NEW SOURCE", error);
        const bool restored = QFile::setPermissions(database.unreadableResource, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        check(restored, "prototype late-failure synthetic permissions restored");
        check(!result && !error.isEmpty(), "prototype reports actual recursive resource-copy failure");
        check(unchanged(), "late resource failure preserves original Markdown AND every old resource");
        check(noStageLeft(), "late resource failure removes only its incomplete staging directory");
        if (!reset()) return 2;
        check(!invoke("NEW SOURCE", error, true) && !error.isEmpty(), "actual QSaveFile canceled commit reports failure");
        check(unchanged(), "failed final Markdown commit rolls installed resource directory back");
        check(noStageLeft(), "successful rollback leaves no temporary directory");
        if (!reset()) return 2;
        const QString source = QString::fromUtf8("# 定理 α\n![图](index_files/a-first.png)\n");
        QString expected = source;
        expected.replace("index_files/", Utils::WizMisc::extractFileTitle(target).toHtmlEscaped() + "_files/");
        check(invoke(source, error) && error.isEmpty(), "prototype completes ordinary synthetic export");
        check(read(target) == expected.toUtf8(), "final Unicode Markdown/resource path bytes match legacy without BOM");
        check(read(resources + "a-first.png") == "NEW A" && read(resources + "z-fails.png") == "NEW Z",
              "success installs both complete new resources");
        check(read(resources + "unrelated.png") == "KEEP UNRELATED", "success preserves legacy unrelated-resource merge behavior");
        check(noStageLeft(), "success cleans temporary HTML and backup directories");
        check(invoke(QString(), error) && read(target).isEmpty(), "valid empty Markdown remains supported");
        check(noStageLeft(), "empty successful export cleans staging");
        const QString first = path("out/first.md");
        auto firstExport = [&](bool cancel) {
            return ExportTransactionPrototype::write(first, "![image](index_files/a-first.png)",
                [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error, cancel);
        };
        check(!firstExport(true) && !QFileInfo::exists(first) && !QFileInfo::exists(path("out/first_files")),
              "failed first export removes new resources and creates no Markdown target");
        check(noStageLeft(), "failed first export leaves no staging directory");
        check(firstExport(false) && read(first) == "![image](first_files/a-first.png)"
            && read(path("out/first_files/a-first.png")) == "NEW A", "first successful export creates exact Markdown and new resource folder");
        const QString missing = path("nonexistent-parent/note.md");
        check(!ExportTransactionPrototype::write(missing, "TEXT", [&](const QString&) { return true; }, error)
            && !QFileInfo::exists(missing), "missing parent fails without creating a destination");
        if (!reset()) return 2;
        const auto directoryPermissions = QFileInfo(path("out")).permissions();
        if (!QFile::setPermissions(path("out"), QFileDevice::ReadOwner | QFileDevice::ExeOwner)) return 2;
        const bool lockedResult = invoke("NEW SOURCE", error);
        const bool lockedUnchanged = unchanged();
        const bool directoryRestored = QFile::setPermissions(path("out"), directoryPermissions);
        check(directoryRestored, "temporary output directory permissions restored");
        check(!lockedResult && lockedUnchanged, "unwritable output parent preserves all original bytes");
        check(noStageLeft(), "unwritable parent leaves no incomplete staging");
        const QString unsupported = path("out/directory.md");
        QDir().mkpath(unsupported);
        check(!ExportTransactionPrototype::write(unsupported, "TEXT", [&](const QString&) { return true; }, error)
            && QFileInfo(unsupported).isDir(), "prototype refuses directory destination without mutation");
        const QString symbolic = path("out/symbolic.md");
        check(QFile::link(target, symbolic), "synthetic destination symlink created");
        check(!ExportTransactionPrototype::write(symbolic, "UNEXPECTED", [&](const QString&) { return true; }, error)
            && QFileInfo(symbolic).isSymLink() && read(target) == sentinel,
              "prototype refuses symlink destination and leaves its target unchanged");
        if (!reset()) return 2;
        QString recovery;
        QFileDevice::Permissions stagingPermissions;
        bool permissionBlocked = false;
        const bool retained = ExportTransactionPrototype::write(target, "NEW SOURCE",
            [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error, true,
            [&](const QString& stage) {
                recovery = stage;
                stagingPermissions = QFileInfo(stage).permissions();
                if (!QFile::setPermissions(stage, QFileDevice::ReadOwner | QFileDevice::ExeOwner)) return;
                permissionBlocked = !write(stage + "/permission-control", "NOT WRITABLE");
            });
        const bool stagePermissionsRestored = !recovery.isEmpty() && QFile::setPermissions(recovery, stagingPermissions);
        check(stagePermissionsRestored && permissionBlocked, "rollback-failure fixture genuinely blocked staging writes and restored permission");
        check(!retained && error.startsWith("rollback failed; retained recovery directory: ") && error.endsWith(recovery),
              "failed new-resource rollback reports explicit retained recovery location");
        check(read(target) == sentinel, "failed rollback still leaves previous Markdown bytes untouched");
        check(read(recovery + "/previous-resources/a-first.png") == "OLD A"
            && read(recovery + "/previous-resources/z-fails.png") == "OLD Z"
            && read(recovery + "/previous-resources/unrelated.png") == "KEEP UNRELATED",
              "failed rollback retains the only complete old resource backup after helper return");
        check(read(resources + "a-first.png") == "NEW A" && read(resources + "z-fails.png") == "NEW Z",
              "fixture records partial rollback honestly rather than claiming resources restored");
        if (!reset()) return 2;
        QString secondRecovery;
        bool blockerCreated = false;
        const bool secondResult = ExportTransactionPrototype::write(target, "NEW SOURCE",
            [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error, true,
            [&](const QString& stage) { secondRecovery = stage; },
            [&] { blockerCreated = write(resources.chopped(1), "SYNTHETIC EXTERNAL BLOCKER"); });
        check(blockerCreated && !secondResult && error.endsWith(secondRecovery), "failed old-resource restoration reports its separate recovery location");
        check(read(target) == sentinel && read(resources.chopped(1)) == "SYNTHETIC EXTERNAL BLOCKER",
              "failed restore does not overwrite an unexpected destination or old Markdown");
        check(read(secondRecovery + "/previous-resources/a-first.png") == "OLD A"
            && read(secondRecovery + "/previous-resources/z-fails.png") == "OLD Z"
            && read(secondRecovery + "/returned-resources/a-first.png") == "NEW A"
            && read(secondRecovery + "/returned-resources/z-fails.png") == "NEW Z",
              "failed restore retains both old backup and returned new resources after helper destruction");
        qInfo().noquote() << QJsonDocument(QJsonObject{{"diagnosticOnly", true}, {"productModified", false},
            {"transactionPrototype", true}, {"archiveExtractionIsAdapter", true},
            {"actualDatabaseEngineEndToEnd", false}, {"crashOrConcurrentWriterRecovery", false},
            {"checks", checks}, {"failures", failures}, {"databaseSourceSHA256", DATABASE_SOURCE_SHA},
            {"transactionPrototypeSHA256", TRANSACTION_SOURCE_SHA}, {"controlledRollbackFailureRetention", true}
        }).toJson(QJsonDocument::Compact);
        return failures ? 1 : 0;
    }
    const QString good = path(QString::fromUtf8("out/导出 A&B.md"));
    check(database.exportToHtmlFile(document, good), "actual export method returns success for synthetic extracted note");
    const QString resourceName = Utils::WizMisc::extractFileTitle(good).toHtmlEscaped() + "_files/";
    QByteArray expected = QByteArray::fromHex("efbbbf") + html;
    expected.replace("index_files/", resourceName.toUtf8());
    check(read(good) == expected, "actual HTML intermediate preserves UTF-8 BOM and legacy escaped resource paths");
    check(read(path("out/") + resourceName + "a-first.png") == "NEW A"
        && read(path("out/") + resourceName + "z-fails.png") == "NEW Z", "actual recursive helper copies both resource bytes");
    const QString early = path("out/early.md");
    if (!write(early, "EXISTING EARLY")) return 2;
    database.failBeforeExtraction = true;
    check(!database.exportToHtmlFile(document, early) && read(early) == "EXISTING EARLY",
          "actual database method preserves destination on early extraction failure");
    database.failBeforeExtraction = false;

    // Test actual copy failure AFTER removing an existing destination file.
    const QString directSource = path("unreadable-source/");
    const QString directTarget = path("direct-target/");
    if (!QDir().mkpath(directSource) || !QDir().mkpath(directTarget)
        || !write(directSource + "file.png", "UNREADABLE NEW")
        || !write(directTarget + "file.png", "OLD RESOURCE")) return 2;
    const auto originalPermissions = QFileInfo(directSource + "file.png").permissions();
    if (!QFile::setPermissions(directSource + "file.png", QFileDevice::Permissions())) return 2;
    QFile unreadable(directSource + "file.png");
    const bool readRefused = !unreadable.open(QIODevice::ReadOnly);
    unreadable.close();
    check(readRefused, "synthetic permission control genuinely refuses reading");
    const bool copied = WizCopyFolder(directSource, directTarget, true);
    QFile::setPermissions(directSource + "file.png", originalPermissions);
    check(!copied, "actual recursive helper reports unreadable-source failure");
    check(!QFile::exists(directTarget + "file.png"), "legacy helper removes old resource before failing copy (risk reproduced)");
    if (requireSafe) check(read(directTarget + "file.png") == "OLD RESOURCE", "SAFETY: direct failed copy preserves old resource");
    if (!readRefused) return 2; // Privileged hosts cannot accept this fixture.

    const QString late = path("out/late.md");
    const QString lateResources = path("out/late_files/");
    if (!QDir().mkpath(lateResources) || !write(late, "EXISTING MARKDOWN")
        || !write(lateResources + "a-first.png", "OLD A")
        || !write(lateResources + "z-fails.png", "OLD Z")) return 2;
    database.makeLastExtractedResourceUnreadable = true;
    const bool exported = database.exportToHtmlFile(document, late);
    const bool permissionRestored = !database.unreadableResource.isEmpty()
        && QFile::setPermissions(database.unreadableResource, originalPermissions);
    check(permissionRestored, "temporary unreadable fixture permissions restored");
    check(!exported, "actual database export method reports late recursive-copy failure");
    check(read(late).startsWith(QByteArray::fromHex("efbbbf") + "<html>"), "actual failed export has already replaced destination with HTML");
    check(read(lateResources + "a-first.png") == "NEW A", "earlier resource was already replaced before actual failure");
    check(!QFile::exists(lateResources + "z-fails.png"), "failing resource lost its previous destination bytes");
    if (requireSafe) {
        check(read(late) == "EXISTING MARKDOWN", "SAFETY: late failure preserves previous Markdown");
        check(read(lateResources + "a-first.png") == "OLD A", "SAFETY: late failure restores earlier resource");
        check(read(lateResources + "z-fails.png") == "OLD Z", "SAFETY: late failure restores failing resource");
    }
    qInfo().noquote() << QJsonDocument(QJsonObject{{"diagnosticOnly", true}, {"productModified", false},
        {"archiveExtractionIsAdapter", true}, {"actualDatabaseEngineEndToEnd", false},
        {"databaseSourceSHA256", DATABASE_SOURCE_SHA}, {"writerSourceSHA256", WRITER_SOURCE_SHA},
        {"namesSourceSHA256", NAMES_SOURCE_SHA}, {"checks", checks}, {"failures", failures},
        {"requireSafe", requireSafe}}).toJson(QJsonDocument::Compact);
    return failures ? 1 : 0;
}
