#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QUuid>
#ifdef Q_OS_UNIX
#include <sys/stat.h>
#endif
#ifdef Q_OS_MACOS
#include <sys/xattr.h>
#endif

#include "WizQtCompat.h"
#include "share/WizMarkdownExportTransaction.h"

using CString = QString;
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
        if (failBeforeExtraction)
            return false;
        if (!QDir().mkpath(path) || !WizCopyFolder(fixture, path, true))
            return false;
        if (makeLastExtractedResourceUnreadable) {
            unreadableResource = path + "index_files/z-fails.png";
            return QFile::setPermissions(unreadableResource, QFileDevice::Permissions());
        }
        return true;
    }
    bool exportToHtmlFile(const WIZDOCUMENTDATA&, const QString&);
};
#include "actual-resources.inc"

static QByteArray readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

static bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

#include "html-transaction-cases.h"

#ifdef WIZ_VERIFY_PUBLICATION
#include "recovery-publication-cases.h"
#endif

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
#ifdef WIZ_VERIFY_PUBLICATION
    if (app.arguments().size() == 6 && app.arguments().at(1) == QStringLiteral("--publication-child"))
        return PublicationCases::child(app.arguments());
#endif
    const bool stressMode = QCoreApplication::arguments().contains(QStringLiteral("--stress"));
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 2;

    int checks = 0;
    int failures = 0;
    auto check = [&](bool ok, const char* text) {
        ++checks;
        failures += !ok;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << text;
        return ok;
    };
    auto path = [&](const QString& relative) { return temporary.filePath(relative); };
    auto noStageLeft = [&] {
        return QDir(path("out")).entryList(QStringList{".wiz-export-*"},
            QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot).isEmpty();
    };

    Utils::WizPathResolve::root = path("extracted/");
    WizDatabase database;
    database.fixture = path("fixture/");
    if (!QDir().mkpath(database.fixture + "index_files/")
        || !QDir().mkpath(path("out")) || !QDir().mkpath(path("extracted")))
        return 2;
    const QByteArray html("<html><img src='index_files/a-first.png'><img src='index_files/z-fails.png'></html>");
    if (!writeFile(database.fixture + "index.html", html)
        || !writeFile(database.fixture + "index_files/a-first.png", "NEW A")
        || !writeFile(database.fixture + "index_files/z-fails.png", "NEW Z"))
        return 2;

    const WIZDOCUMENTDATA document;
    const QString target = path(QString::fromUtf8("out/定理 A&B.md"));
    const QString resources = path("out/定理 A&amp;B_files/");
    const QByteArray sentinel("ORIGINAL MARKDOWN\n");
    auto reset = [&] {
        database.failBeforeExtraction = false;
        database.makeLastExtractedResourceUnreadable = false;
        return QDir().mkpath(resources + ".hidden-directory/")
            && writeFile(target, sentinel)
            && writeFile(resources + "a-first.png", "OLD A")
            && writeFile(resources + "z-fails.png", "OLD Z")
            && writeFile(resources + "unrelated.png", "KEEP UNRELATED")
            && writeFile(resources + ".hidden-image", "KEEP HIDDEN")
            && writeFile(resources + ".hidden-directory/nested", "KEEP NESTED");
    };
    auto unchanged = [&] {
        return readFile(target) == sentinel
            && readFile(resources + "a-first.png") == "OLD A"
            && readFile(resources + "z-fails.png") == "OLD Z"
            && readFile(resources + "unrelated.png") == "KEEP UNRELATED";
    };
    auto invoke = [&](const QString& source, QString& error) {
        return WizMarkdownExportTransaction::write(target, source,
            [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error);
    };

    QString error;
    if (!reset())
        return 2;
    database.failBeforeExtraction = true;
    check(!invoke("NEW SOURCE", error) && !error.isEmpty(), "product transaction reports early export failure");
    check(unchanged(), "early failure preserves previous Markdown and resources");
    check(noStageLeft(), "early failure cleans only its staging directory");

    if (!reset())
        return 2;
    database.makeLastExtractedResourceUnreadable = true;
    const bool lateResult = invoke("NEW SOURCE", error);
    const bool permissionRestored = QFile::setPermissions(database.unreadableResource,
        QFileDevice::ReadOwner | QFileDevice::WriteOwner);
    check(permissionRestored, "late-failure fixture permissions restored");
    check(!lateResult && !error.isEmpty(), "product transaction reports late resource failure");
    check(unchanged(), "late failure preserves previous Markdown and every old resource");
    check(noStageLeft(), "late failure cleans incomplete staged content");

    if (!reset())
        return 2;
    const QString source = QString::fromUtf8("# 定理 α\n![图](index_files/a-first.png)\n");
    QString expected = source;
    expected.replace("index_files/", "定理 A&amp;B_files/");
    check(invoke(source, error) && error.isEmpty(), "product transaction completes ordinary export");
    check(readFile(target) == expected.toUtf8(), "successful Markdown bytes and escaped resource path match legacy");
    check(readFile(resources + "a-first.png") == "NEW A" && readFile(resources + "z-fails.png") == "NEW Z",
        "successful export installs complete new resources");
    check(readFile(resources + "unrelated.png") == "KEEP UNRELATED"
        && readFile(resources + ".hidden-image") == "KEEP HIDDEN"
        && readFile(resources + ".hidden-directory/nested") == "KEEP NESTED",
        "successful export preserves unrelated and hidden existing resources");
    check(noStageLeft(), "successful export removes private staging content");
    check(invoke(QString(), error) && readFile(target).isEmpty(), "valid empty Markdown remains supported");
    check(noStageLeft(), "empty export cleans staging");

    const QString first = path("out/first.md");
    check(WizMarkdownExportTransaction::write(first, "![image](index_files/a-first.png)",
        [&](const QString& staged) { return database.exportToHtmlFile(document, staged); }, error)
        && readFile(first) == "![image](first_files/a-first.png)"
        && readFile(path("out/first_files/a-first.png")) == "NEW A",
        "first export creates exact Markdown and resources");
    check(noStageLeft(), "first export cleans staging");

    const QString missing = path("missing/note.md");
    check(!WizMarkdownExportTransaction::write(missing, "TEXT", [&](const QString&) { return true; }, error)
        && !QFileInfo::exists(missing), "missing parent fails without creating output");
    const QString directoryTarget = path("out/directory.md");
    QDir().mkpath(directoryTarget);
    check(!WizMarkdownExportTransaction::write(directoryTarget, "TEXT", [&](const QString&) { return true; }, error)
        && QFileInfo(directoryTarget).isDir(), "directory destination is refused without mutation");
    const QString symbolic = path("out/symbolic.md");
    check(QFile::link(target, symbolic), "synthetic destination symlink created");
    check(!WizMarkdownExportTransaction::write(symbolic, "UNEXPECTED", [&](const QString&) { return true; }, error)
        && QFileInfo(symbolic).isSymLink() && readFile(target).isEmpty(),
        "destination symlink is refused without following it");

    const QString linked = path("out/linked.md");
    const QString linkedResources = path("out/linked_files/");
    const QString external = path("outside-resource");
    if (!QDir().mkpath(linkedResources) || !writeFile(linked, "OLD LINKED") || !writeFile(external, "OUTSIDE"))
        return 2;
    check(QFile::link(external, linkedResources + "unknown-link"), "synthetic resource symlink created");
    check(!WizMarkdownExportTransaction::write(linked, "NEW", [&](const QString&) { return true; }, error)
        && readFile(linked) == "OLD LINKED", "resource symlink refuses export without changing Markdown");
    check(readFile(external) == "OUTSIDE", "refused resource symlink target remains unchanged");

#ifdef Q_OS_UNIX
    const QString special = path("out/special.md");
    const QString specialResources = path("out/special_files/");
    if (!QDir().mkpath(specialResources) || !writeFile(special, "OLD SPECIAL"))
        return 2;
    check(::mkfifo(QFile::encodeName(specialResources + "unknown-fifo").constData(), 0600) == 0,
        "synthetic nonregular resource created");
    check(!WizMarkdownExportTransaction::write(special, "NEW", [&](const QString&) { return true; }, error)
        && readFile(special) == "OLD SPECIAL", "nonregular resource refuses without opening or changing it");
#endif

    const QString blocked = path("out/blocked.md");
    const QString blockedResources = path("out/blocked_files");
    if (!writeFile(blocked, "OLD BLOCKED") || !writeFile(blockedResources, "BLOCKER"))
        return 2;
    check(!WizMarkdownExportTransaction::write(blocked, "NEW", [&](const QString&) { return true; }, error)
        && readFile(blocked) == "OLD BLOCKED" && readFile(blockedResources) == "BLOCKER",
        "non-directory resource destination is refused without mutation");
    check(noStageLeft(), "all refusal paths leave no private staging directory");

    qint64 stressElapsedMs = -1;
    bool stressXattrPreserved = false;
    bool stressDirectoryXattrPreserved = false;
    bool stressPermissionsPreserved = false;
    int stressModeBefore = -1;
    int stressModeAfter = -1;
    constexpr int stressFileCount = 2048;
    constexpr int stressBytesPerFile = 4096;
    if (stressMode) {
        const QString stressTarget = path("out/stress.md");
        const QString stressResources = path("out/stress_files/");
        const QByteArray payload(stressBytesPerFile, 'R');
        bool seeded = writeFile(stressTarget, "OLD STRESS\n");
        for (int index = 0; seeded && index < stressFileCount; ++index) {
            const QString directory = stressResources + QStringLiteral("bucket-%1/").arg(index % 32, 2, 10, QLatin1Char('0'));
            seeded = QDir().mkpath(directory)
                && writeFile(directory + QStringLiteral("resource-%1.bin").arg(index, 4, 10, QLatin1Char('0')), payload);
        }
        const QString metadataFile = stressResources + "bucket-00/resource-0000.bin";
        seeded = seeded && QFile::setPermissions(metadataFile, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#ifdef Q_OS_UNIX
        struct stat modeBefore {};
        if (::stat(QFile::encodeName(metadataFile).constData(), &modeBefore) == 0)
            stressModeBefore = int(modeBefore.st_mode & 07777);
#endif
#ifdef Q_OS_MACOS
        const QByteArray metadataPath = QFile::encodeName(metadataFile);
        const QByteArray metadataDirectoryPath = QFile::encodeName(stressResources + "bucket-00");
        const char metadataValue[] = "synthetic-metadata";
        const bool xattrSeeded = ::setxattr(metadataPath.constData(), "user.wiznote.synthetic",
            metadataValue, sizeof(metadataValue) - 1, 0, 0) == 0;
        const bool directoryXattrSeeded = ::setxattr(metadataDirectoryPath.constData(),
            "user.wiznote.synthetic.directory", metadataValue, sizeof(metadataValue) - 1, 0, 0) == 0;
#else
        const bool xattrSeeded = false;
        const bool directoryXattrSeeded = false;
#endif
        check(seeded, "large synthetic resource snapshot fixture created");
        QElapsedTimer timer;
        timer.start();
        const bool stressResult = WizMarkdownExportTransaction::write(stressTarget, "NEW STRESS\n",
            [&](const QString& staged) {
                const QString stagedResources = QFileInfo(staged).absolutePath() + "/stress_files/";
                return writeFile(staged, "<html>stress</html>")
                    && QDir().mkpath(stagedResources)
                    && writeFile(stagedResources + "new-resource.bin", "NEW");
            }, error);
        stressElapsedMs = timer.elapsed();
        int preservedFiles = 0;
        QDirIterator iterator(stressResources, QDir::Files, QDirIterator::Subdirectories);
        while (iterator.hasNext()) {
            iterator.next();
            if (iterator.fileInfo().fileName().startsWith(QStringLiteral("resource-")))
                ++preservedFiles;
        }
#ifdef Q_OS_UNIX
        struct stat modeAfter {};
        if (::stat(QFile::encodeName(metadataFile).constData(), &modeAfter) == 0)
            stressModeAfter = int(modeAfter.st_mode & 07777);
        stressPermissionsPreserved = stressModeBefore >= 0 && stressModeAfter == stressModeBefore;
#else
        stressPermissionsPreserved = QFileInfo(metadataFile).permissions()
            == (QFileDevice::ReadOwner | QFileDevice::WriteOwner);
#endif
#ifdef Q_OS_MACOS
        char xattrBuffer[64] = {};
        const QByteArray resultPath = QFile::encodeName(metadataFile);
        const ssize_t xattrSize = ::getxattr(resultPath.constData(), "user.wiznote.synthetic",
            xattrBuffer, sizeof(xattrBuffer), 0, 0);
        stressXattrPreserved = xattrSeeded && xattrSize == ssize_t(sizeof(metadataValue) - 1)
            && QByteArray(xattrBuffer, int(xattrSize)) == QByteArray(metadataValue, sizeof(metadataValue) - 1);
        char directoryXattrBuffer[64] = {};
        const QByteArray resultDirectoryPath = QFile::encodeName(stressResources + "bucket-00");
        const ssize_t directoryXattrSize = ::getxattr(resultDirectoryPath.constData(),
            "user.wiznote.synthetic.directory", directoryXattrBuffer, sizeof(directoryXattrBuffer), 0, 0);
        stressDirectoryXattrPreserved = directoryXattrSeeded
            && directoryXattrSize == ssize_t(sizeof(metadataValue) - 1)
            && QByteArray(directoryXattrBuffer, int(directoryXattrSize))
                == QByteArray(metadataValue, sizeof(metadataValue) - 1);
#else
        Q_UNUSED(xattrSeeded);
        Q_UNUSED(directoryXattrSeeded);
#endif
        check(stressResult && error.isEmpty(), "large synthetic resource transaction completes");
        check(readFile(stressTarget) == "NEW STRESS\n"
            && readFile(stressResources + "new-resource.bin") == "NEW",
            "large synthetic resource transaction commits exact new output");
        check(preservedFiles == stressFileCount,
            "large synthetic resource transaction preserves every unrelated old resource");
        check(stressPermissionsPreserved,
            "large synthetic resource transaction preserves restrictive file permissions");
        check(!xattrSeeded || stressXattrPreserved,
            "large synthetic resource transaction preserves existing file extended attributes");
        check(!directoryXattrSeeded || stressDirectoryXattrPreserved,
            "large synthetic resource transaction preserves existing directory extended attributes");
        check(noStageLeft(), "large synthetic resource transaction cleans staging");
        qInfo() << "OBSERVATION large resource transaction" << stressFileCount << "files"
            << qint64(stressFileCount) * stressBytesPerFile << "bytes" << stressElapsedMs << "ms"
            << "permissionsPreserved" << stressPermissionsPreserved
            << "modeBefore" << stressModeBefore << "modeAfter" << stressModeAfter
            << "xattrSeeded" << xattrSeeded << "xattrPreserved" << stressXattrPreserved
            << "directoryXattrSeeded" << directoryXattrSeeded
            << "directoryXattrPreserved" << stressDirectoryXattrPreserved;
    }

    HtmlTransactionCases::run(temporary.path(), database, document, check);
#ifdef WIZ_VERIFY_PUBLICATION
    PublicationCases::run(temporary.path(), check);
#endif
    qInfo().noquote() << QJsonDocument(QJsonObject{
        {"productTransaction", true},
        {"archiveExtractionIsAdapter", true},
        {"actualDatabaseEngineEndToEnd", false},
        {"powerLossOrConcurrentWriterAtomicity", false},
        {"stressMode", stressMode},
        {"stressFileCount", stressMode ? stressFileCount : 0},
        {"stressPayloadBytes", stressMode ? qint64(stressFileCount) * stressBytesPerFile : 0},
        {"stressElapsedMs", stressElapsedMs},
        {"stressPermissionsPreserved", stressPermissionsPreserved},
        {"stressModeBefore", stressModeBefore},
        {"stressModeAfter", stressModeAfter},
        {"stressXattrPreserved", stressXattrPreserved},
        {"stressDirectoryXattrPreserved", stressDirectoryXattrPreserved},
        {"checks", checks},
        {"failures", failures},
        {"databaseSourceSHA256", DATABASE_SOURCE_SHA},
        {"transactionSourceSHA256", PRODUCT_TRANSACTION_SOURCE_SHA}
    }).toJson(QJsonDocument::Compact);
    return failures ? 1 : 0;
}
