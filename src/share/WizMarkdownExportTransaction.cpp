#include "WizMarkdownExportTransaction.h"
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
#include "WizMarkdownExportRecovery.h"
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>

#ifdef Q_OS_MACOS
#include <copyfile.h>
#endif

#include "utils/WizMisc.h"

namespace {

bool preserveSnapshotMetadata(const QString& source, const QString& destination)
{
#ifdef Q_OS_MACOS
    const QByteArray sourcePath = QFile::encodeName(source);
    const QByteArray destinationPath = QFile::encodeName(destination);
    return ::copyfile(sourcePath.constData(), destinationPath.constData(), nullptr,
        COPYFILE_METADATA | COPYFILE_NOFOLLOW) == 0;
#else
    Q_UNUSED(source);
    Q_UNUSED(destination);
    return true;
#endif
}

bool copySnapshotFile(const QString& source, const QString& destination)
{
#ifdef Q_OS_MACOS
    const QByteArray sourcePath = QFile::encodeName(source);
    const QByteArray destinationPath = QFile::encodeName(destination);
    return ::copyfile(sourcePath.constData(), destinationPath.constData(), nullptr,
        COPYFILE_ALL | COPYFILE_NOFOLLOW | COPYFILE_EXCL) == 0;
#else
    return QFile::copy(source, destination);
#endif
}

bool snapshotResources(const QString& source, const QString& destination)
{
    const QFileInfo root(source);
    if (root.isSymLink() || !root.isDir() || !QDir().mkpath(destination))
        return false;

    const auto entries = QDir(source).entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo& entry : entries) {
        // Do not follow an unexpected link or open a device/FIFO while taking
        // the private rollback snapshot.
        if (entry.isSymLink())
            return false;
        const QString target = QDir(destination).filePath(entry.fileName());
        if (entry.isDir()) {
            if (!snapshotResources(entry.filePath(), target))
                return false;
        } else if (!entry.isFile() || !copySnapshotFile(entry.filePath(), target)) {
            return false;
        }
    }
    return preserveSnapshotMetadata(source, destination);
}

}

namespace WizMarkdownExportTransaction {

#ifdef WIZNOTE_HTML_EXPORT_TRANSACTION_TESTING
// Defined only by the isolated filesystem corpus, never the normal App.
void beforeHtmlCommitForTest(QSaveFile& output);
#endif

static bool writeUnreserved(const QString& destination, const QString& markdown,
           const std::function<bool(const QString&)>& exportHtml,
           QString& error,
           const std::function<bool(const QString&, QByteArray&)>& htmlBody = {},
           const std::function<bool()>& current = {})
{
    error.clear();
    if (current && !current()) {
        error = QStringLiteral("output permission expired");
        return false;
    }
    const QFileInfo destinationInfo(destination);
    const QString parent = destinationInfo.absolutePath();
    const QString resourceName = Utils::WizMisc::extractFileTitle(destination).toHtmlEscaped() + "_files";
    const QString resources = QDir(parent).filePath(resourceName);

    if (destinationInfo.isSymLink() || destinationInfo.isDir()
        || QFileInfo(resources).isSymLink()
        || (QFileInfo::exists(resources) && !QFileInfo(resources).isDir())) {
        error = QStringLiteral("unsupported destination shape");
        return false;
    }

    QTemporaryDir staging(QDir(parent).filePath(QStringLiteral(".wiz-export-XXXXXX")));
    if (!staging.isValid()) {
        error = QStringLiteral("staging creation failed");
        return false;
    }

    // Keep user-controlled basenames separate from private rollback names.
    const QString content = staging.filePath(QStringLiteral("content"));
    if (!QDir().mkpath(content)) {
        error = QStringLiteral("content staging creation failed");
        return false;
    }
    const QString stagedResources = QDir(content).filePath(resourceName);
    const QString backup = staging.filePath(QStringLiteral("previous-resources"));
    const QString returned = staging.filePath(QStringLiteral("returned-resources"));
    const bool hadResources = QFileInfo::exists(resources);

    // Preserve the established merge behavior: unrelated existing resource
    // files remain after a successful export.
    if (hadResources && !snapshotResources(resources, stagedResources)) {
        error = QStringLiteral("existing resource snapshot failed");
        return false;
    }
    const QString stagedHtml = QDir(content).filePath(destinationInfo.fileName());
    if (!exportHtml(stagedHtml)) {
        error = QStringLiteral("staged export failed");
        return false;
    }

    QByteArray bytes;
    if (htmlBody) {
        if (!htmlBody(stagedHtml, bytes)) {
            error = QStringLiteral("staged HTML read failed");
            return false;
        }
    } else {
        QString source = markdown;
        source.replace(QStringLiteral("index_files/"), resourceName + '/');
        bytes = source.toUtf8();
    }
    if (current && !current()) {
        error = QStringLiteral("output permission expired");
        return false;
    }
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) {
        error = QStringLiteral("output reservation failed");
        return false;
    }
    if (output.write(bytes) != bytes.size()) {
        output.cancelWriting();
        error = QStringLiteral("output write failed");
        return false;
    }

    const bool hasStagedResources = QFileInfo::exists(stagedResources);
    bool movedOld = false;
    bool installedNew = false;
    auto rollback = [&] {
        bool ok = true;
        if (installedNew)
            ok = QDir().rename(resources, returned);
        if (ok && movedOld)
            ok = QDir().rename(backup, resources);
        if (!ok) {
            // Never let QTemporaryDir remove the only complete old copy.
            staging.setAutoRemove(false);
            error = QStringLiteral("rollback failed; retained recovery directory: ") + staging.path();
        }
        return ok;
    };

    if (hasStagedResources) {
        if (hadResources) {
            if (!QDir().rename(resources, backup)) {
                error = QStringLiteral("old resource reservation failed");
                return false;
            }
            movedOld = true;
        }
        if (!QDir().rename(stagedResources, resources)) {
            error = QStringLiteral("new resource installation failed");
            rollback();
            return false;
        }
        installedNew = true;
    }

    if (current && !current()) {
        output.cancelWriting();
        error = QStringLiteral("output permission expired");
        rollback();
        return false;
    }
#ifdef WIZNOTE_HTML_EXPORT_TRANSACTION_TESTING
    if (htmlBody) beforeHtmlCommitForTest(output);
#endif
    if (!output.commit()) {
        error = htmlBody ? QStringLiteral("final HTML commit failed") : QStringLiteral("final Markdown commit failed");
        rollback();
        return false;
    }
    return true;
}

bool writeHtml(const QString& destination, const QString& rendered,
               const std::function<bool(const QString&)>& exportHtml,
               const std::function<bool()>& current, QString& error)
{
    if (!exportHtml || !current) {
        error = QStringLiteral("missing HTML export callback or permission");
        return false;
    }
    const auto body = [&](const QString& stagedHtml, QByteArray& bytes) {
        if (rendered.isEmpty()) {
            QFile file(stagedHtml);
            if (!file.open(QIODevice::ReadOnly)) return false;
            bytes = file.readAll();
            return file.error() == QFileDevice::NoError;
        }
        QString source = rendered;
        // Retain the rendered HTML branch's existing basename policy. The DB
        // exporter still owns saved-HTML/resource naming (including escaping).
        source.replace(QStringLiteral("index_files/"), Utils::WizMisc::extractFileTitle(destination) + "_files/");
        bytes = QByteArray::fromHex("efbbbf") + source.toUtf8();
        return true;
    };
    return writeUnreserved(destination, QString(), exportHtml, error, body, current);
}

bool write(const QString& destination, const QString& markdown,
           const std::function<bool(const QString&)>& exportHtml,
           QString& error)
{
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
    error.clear();
    if (!exportHtml) {
        error = QStringLiteral("missing export callback");
        return false;
    }
    const QString resourcesName = Utils::WizMisc::extractFileTitle(destination).toHtmlEscaped() + "_files";
    const QString resources = QDir(QFileInfo(destination).absolutePath()).filePath(resourcesName);
    QString source = markdown;
    source.replace(QStringLiteral("index_files/"), resourcesName + '/');
    const auto prepared = WizMarkdownExportRecovery::prepare(destination, source.toUtf8(),
        [&](const QString& html) {
            // Preserve the same old/unrelated resource bytes and metadata as
            // the legacy transaction, while prepare owns the destination lock.
            const QString stagedResources = QDir(QFileInfo(html).absolutePath()).filePath(resourcesName);
            if (QFileInfo::exists(resources) && !snapshotResources(resources, stagedResources))
                return false;
            return exportHtml(html);
        });
    if (prepared.outcome != WizMarkdownExportRecovery::PreparationOutcome::Prepared) {
        error = prepared.diagnosticDetail;
        return false;
    }
    const auto published = WizMarkdownExportRecovery::publishPrepared(prepared);
    const bool completed = published.outcome == WizMarkdownExportRecovery::PublicationOutcome::Published;
    if (!completed)
        error = published.diagnosticDetail;
    return completed;
#else
    return writeUnreserved(destination, markdown, exportHtml, error);
#endif
}

}
