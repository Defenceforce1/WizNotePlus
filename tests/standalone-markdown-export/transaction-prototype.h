#pragma once

#include <QSaveFile>
#include <QTemporaryDir>
#include <functional>

// TEST ONLY. No crash/power-loss or concurrent-writer recovery contract. An
// ordinary returned failure must roll back the directories or retain backups
// explicitly. Never linked into the main App or run against user destinations.
namespace ExportTransactionPrototype {
inline bool write(const QString& destination, const QString& markdown,
                  const std::function<bool(const QString&)>& exportHtml,
                  QString& error, bool cancelFinalCommitForTest = false)
{
    error.clear();
    const QFileInfo info(destination);
    const QString parent = info.absolutePath();
    const QString resourceName = Utils::WizMisc::extractFileTitle(destination).toHtmlEscaped() + "_files";
    const QString resources = QDir(parent).filePath(resourceName);
    if (info.isSymLink() || info.isDir() || QFileInfo(resources).isSymLink()
        || (QFileInfo::exists(resources) && !QFileInfo(resources).isDir())) {
        error = "unsupported destination shape";
        return false;
    }
    QTemporaryDir staging(QDir(parent).filePath(".wiz-export-XXXXXX"));
    if (!staging.isValid()) { error = "staging creation failed"; return false; }
    const QString stagedResources = staging.filePath(resourceName);
    const QString backup = staging.filePath("previous-resources");
    const QString returned = staging.filePath("returned-resources");
    const bool hadResources = QFileInfo::exists(resources);
    // Preserve the legacy merge behavior: unrelated/stale existing resources
    // survive a successful export, rather than silently deleting old files.
    if (hadResources && !WizCopyFolder(resources, stagedResources, true)) {
        error = "existing resource snapshot failed";
        return false;
    }
    if (!exportHtml(staging.filePath(info.fileName()))) {
        error = "staged export failed";
        return false;
    }
    QString source = markdown;
    source.replace("index_files/", resourceName + "/");
    QSaveFile output(destination);
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)) { error = "output reservation failed"; return false; }
    const QByteArray bytes = source.toUtf8();
    if (output.write(bytes) != bytes.size()) {
        output.cancelWriting(); error = "output write failed"; return false;
    }
    const bool hasStagedResources = QFileInfo::exists(stagedResources);
    bool movedOld = false, installedNew = false;
    auto rollback = [&] {
        bool ok = true;
        if (installedNew) ok = QDir().rename(resources, returned);
        if (ok && movedOld) ok = QDir().rename(backup, resources);
        if (!ok) {
            staging.setAutoRemove(false); // Never discard the only old copy.
            error = "rollback failed; retained recovery directory: " + staging.path();
        }
        return ok;
    };
    if (hasStagedResources) {
        if (hadResources) {
            if (!QDir().rename(resources, backup)) { error = "old resource reservation failed"; return false; }
            movedOld = true;
        }
        if (!QDir().rename(stagedResources, resources)) {
            error = "new resource installation failed";
            rollback();
            return false;
        }
        installedNew = true;
    }
    if (cancelFinalCommitForTest) output.cancelWriting();
    if (!output.commit()) {
        error = "final Markdown commit failed";
        rollback();
        return false;
    }
    return true;
}
}
