#include "ExternalEditorLauncher.h"

#include <QFileInfo>
#include <QDir>
#include <QMessageBox>
#include <QTextDocument>
#include <QFileSystemWatcher>
#include <QStringList>

#include "gui/documentviewer/WizDocumentView.h"
#include "gui/documentviewer/WizDocumentWebView.h"
#include "utils/WizPathResolve.h"
#include "utils/WizMisc.h"
#include "share/WizMisc.h"
#include "share/WizDocumentReadOutput.h"
#include "share/WizExternalEditorSnapshot.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "WizFileImporter.h"
#include "WizMainWindow.h"

ExternalEditorLauncher::ExternalEditorLauncher(WizExplorerApp &app, QObject *parent)
    : QObject(parent)
    , m_app(app)
    , m_window(qobject_cast<WizMainWindow*>(m_app.mainWindow()))
    , m_dbMgr(app.databaseManager())
    , m_externalEditorFileWatcher(new QFileSystemWatcher(this))
{
    // initialize document saver thread
    m_watchedDocSaver = new WizDocumentSaverThread(m_dbMgr, this);

    connect(m_externalEditorFileWatcher, &QFileSystemWatcher::fileChanged, 
            this, &ExternalEditorLauncher::onWatchedDocumentChanged);
}

void ExternalEditorLauncher::waitForDone()
{
    // Stop watcher admission before pumping the GUI during the save drain.
    // Queued notifications may still arrive; they must not restart saving.
    auto saver = m_watchedDocSaver;
    if (!saver) return;
    m_watchedDocSaver = nullptr;
    if (m_externalEditorFileWatcher) {
        disconnect(m_externalEditorFileWatcher, nullptr, this, nullptr);
        const auto files = m_externalEditorFileWatcher->files();
        if (!files.isEmpty()) m_externalEditorFileWatcher->removePaths(files);
    }
    saver->waitForDone();
}

/*!
    Ensure that all characters in the \a docTitle are legal and automatically
    add the file suffix number if the file exists in the folder \a tempDir .
 */
QString ExternalEditorLauncher::makeValidCacheFileName(const QString &docTitle, const QDir &tempDir)
{
    CString title = docTitle;
#ifdef Q_OS_WIN
    // MS Office Word can not handle space correctly.
    title.replace(" ", "_");
#endif
    WizMakeValidFileNameNoPath(title);
    QString cacheFileName = title;

    QFileInfo cacheFileInfo(cacheFileName);
    QString fileSuffix = cacheFileInfo.suffix();
    QString fileBaseName = cacheFileInfo.completeBaseName();
    int num = 1;

    while(tempDir.exists(cacheFileName)) {
        cacheFileName = fileBaseName + "_" + QString::number(num++)
            + (fileSuffix.isEmpty() ? "" : "." + fileSuffix);
    }

    return tempDir.absoluteFilePath(cacheFileName);
}

void ExternalEditorLauncher::handleViewNoteInExternalEditorRequest(
    const WizExternalEditorData &editorData,
    const WIZDOCUMENTDATAEX &noteData)
{
    viewNoteInExternalEditor(qobject_cast<WizDocumentView*>(sender()), editorData, noteData);
}

void ExternalEditorLauncher::viewNoteInExternalEditor(WizDocumentView *docView,
    const WizExternalEditorData &editorData, const WIZDOCUMENTDATAEX &noteData)
{
    if (!docView || docView->note().strGUID != noteData.strGUID
        || docView->note().strKbGUID != noteData.strKbGUID
        || (editorData.TextEditor != 0 && editorData.TextEditor != 2)) return;
    const QPointer<WizDocumentWebView> webView(docView->web());
    const QPointer<WizDocumentSaverThread> saver(m_watchedDocSaver);
    if (!webView || !saver) return;
    const auto read = webView->noteReadPermit();
    if (!read.current()) return;
    const QPointer<ExternalEditorLauncher> self(this);
    const auto current = [self, saver, read] {
        return self && saver && self->m_watchedDocSaver == saver && read.current();
    };
    const QDir noteTempDir(Utils::WizPathResolve::tempPath() + noteData.strGUID + "/");
    // Only the read continuation waits for the saver. Relocking must not cancel
    // the original snapshot, queued disk save or an existing watched-file save.
    webView->trySaveDocumentForRead(noteData, false,
        [self, webView, read, current, noteData, editorData, noteTempDir](const QVariant& saved) {
        if (!webView || !current()
            || saved.userType() != QMetaType::Bool || !saved.toBool()) return;
        QString strHtmlFile, strHtml;
        if (!self->m_dbMgr.db(noteData.strKbGUID).documentToTempHtmlFile(noteData, strHtmlFile)
            || !current() || !WizLoadUnicodeTextFromFile(strHtmlFile, strHtml)) return;
        if (editorData.TextEditor == 2) {
            const QString cacheFileName = self->makeValidCacheFileName(noteData.strTitle, noteTempDir);
            QTextDocument doc;
            doc.setHtml(strHtml);
            QString strText = doc.toPlainText();
            strText.replace("&nbsp", " ");
            if (current() && WizDocumentReadOutput::writeUtf8(read, cacheFileName, strText)
                && current())
                self->startExternalEditor(cacheFileName, editorData, noteData);
        } else {
            const QString cacheFileName = self->makeValidCacheFileName(noteData.strTitle + ".html", noteTempDir);
            if (!current()) return;
            webView->page()->toHtml([self, read, current, cacheFileName, editorData, noteData](const QString& html) {
                if (!current()) return;
                if (WizDocumentReadOutput::writeUtf8(read, cacheFileName, html)
                    && current())
                    self->startExternalEditor(cacheFileName, editorData, noteData);
            });
        }
    });
}

/*!
    Start external editor process and watch the document \a cacheFileName.
 */
void ExternalEditorLauncher::startExternalEditor(
    QString cacheFileName,
    const WizExternalEditorData &editorData,
    const WIZDOCUMENTDATAEX &noteData)
{
    // Bind launch admission to this still-open saver lifetime. New read
    // requests use unique cache paths and must not take over an existing task.
    const QPointer<ExternalEditorLauncher> self(this);
    const QPointer<WizDocumentSaverThread> saver(m_watchedDocSaver);
    const auto current = [self, saver] {
        return self && saver && self->m_watchedDocSaver == saver;
    };
    if (!current() || !m_externalEditorFileWatcher || noteData.strGUID.isEmpty()
        || (editorData.TextEditor != 0 && editorData.TextEditor != 2)
        || !QFileInfo(cacheFileName).isFile()
        || m_externalEditorTasks.contains(cacheFileName)
        || m_externalEditorFileWatcher->files().contains(cacheFileName)) return;
    const auto initial = WizExternalEditorSnapshot::read(cacheFileName, editorData.TextEditor != 0);
    if (!initial.ok) return;

    // Register before dispatch so an immediate editor write cannot arrive
    // before its origin is known. A failed launch rolls back only this session.
    if (!m_externalEditorFileWatcher->addPath(cacheFileName)) {
        if (current()) QMessageBox::critical(m_app.mainWindow(),
            tr("Failed to launch external editor"),
            tr("There may be a system dependent limit to the number "
               "of files that can be monitored simultaneously."));
        return;
    }
    if (!current()) return;
    WizExternalEditTask newTask = { editorData, noteData, cacheFileName, initial.digest };
    m_externalEditorTasks.insert(cacheFileName, newTask);

    // Launch external editor process
    // FIXME: split too many items
    QString programFile = editorData.ProgramFile;
    QString args = editorData.Arguments.arg("\"" + cacheFileName + "\"");

    // Do not use attached QProcess to detect the state of external editor GUI programs,
    // for reasons described in https://github.com/altairwei/WizNotePlus/issues/199

    QProcess editor;
    editor.setProgram(programFile);
    editor.setArguments(splitCommand(args));
    editor.setWorkingDirectory(Utils::WizPathResolve::tempPath() + noteData.strGUID + "/");

    qDebug() << editor.program() << " " << editor.arguments();

    if (!current() || editor.startDetached()) return;
    if (!current()) return;
    const auto found = m_externalEditorTasks.constFind(cacheFileName);
    if (found == m_externalEditorTasks.cend()
        || found->docData.strGUID != noteData.strGUID
        || found->docData.strKbGUID != noteData.strKbGUID
        || found->cacheFileName != cacheFileName) return;
    m_externalEditorTasks.remove(cacheFileName);
    m_externalEditorFileWatcher->removePath(cacheFileName);
    if (m_window) m_window->showBubbleNotification(
            tr("Failed to launch external editor"),
            tr("%1 %2").arg(editor.program()).arg(editor.arguments().join(" "))
        );

}

void ExternalEditorLauncher::onWatchedDocumentChanged(const QString& fileName)
{
    if (!m_watchedDocSaver) return;
    const auto task = m_externalEditorTasks.constFind(fileName);
    if (task == m_externalEditorTasks.cend() || task->cacheFileName != fileName
        || task->docData.strGUID.isEmpty()) return;
    saveWatchedFile(fileName);

    QFileInfo watchedFile(fileName);

    // Many applications save an open file by writing a new file and then deleting the old one.
    // So we watch that file again.
    if(!m_externalEditorFileWatcher->files().contains(fileName)){
        if (watchedFile.exists()) {
            if (!m_externalEditorFileWatcher->addPath(fileName)) {
                m_window->showBubbleNotification(
                    tr("Unable to continue monitoring files"),
                    tr("Failed to monitor the file required "
                        "by external editor: %1").arg(watchedFile.fileName())
                );
            }
        } else {
            m_window->showBubbleNotification(
                tr("Unable to continue monitoring files"),
                tr("File does not exist: %1").arg(watchedFile.fileName())
            );
        }
    }
}

/*!
    Save the watched document \a fileName which is changed by external editor.
 */
void ExternalEditorLauncher::saveWatchedFile(const QString &fileName)
{
    auto* saver = m_watchedDocSaver;
    if (!saver) return;
    const auto found = m_externalEditorTasks.constFind(fileName);
    if (found == m_externalEditorTasks.cend() || found->cacheFileName != fileName
        || found->docData.strGUID.isEmpty()) return;
    // Copy the registered origin; never infer a target from the active note or
    // a directory name, and never default-insert a task for an unknown path.
    const WizExternalEditTask task = found.value();
    QFileInfo changedFileInfo(fileName);

    if (!changedFileInfo.isFile())
        return;

    const auto snapshot = WizExternalEditorSnapshot::read(fileName, task.editorData.TextEditor != 0);
    if (!snapshot.ok) return;
    // Get document's file name
    //FIXME: Windows client has encoding problem.
    QString indexFileName = Utils::WizMisc::extractFilePath(fileName) + "index.html";
    // Start saver thread
    saver->save(task.docData, snapshot.html, indexFileName, 0, true);
    // Admission is not a disk acknowledgement. Quit also checks saver errors.
    m_externalEditorTasks[fileName].admittedHtmlSha256 = snapshot.digest;
}

QStringList ExternalEditorLauncher::pendingExternalChanges() const
{
    QStringList pending;
    for (auto it=m_externalEditorTasks.cbegin(); it!=m_externalEditorTasks.cend(); ++it) {
        const auto& task=it.value();
        if (task.cacheFileName!=it.key() || task.docData.strGUID.isEmpty()) {
            pending.append(it.key());continue;
        }
        const auto snapshot=WizExternalEditorSnapshot::read(it.key(),task.editorData.TextEditor!=0);
        if (!snapshot.ok || task.admittedHtmlSha256.isEmpty()
            || snapshot.digest!=task.admittedHtmlSha256) pending.append(it.key());
    }
    return pending;
}

bool ExternalEditorLauncher::verifyExternalChangesForQuit() const
{
    if (pendingExternalChanges().isEmpty() && (!m_watchedDocSaver
        || (m_watchedDocSaver->isIdleForTransfer() && !m_watchedDocSaver->hasTransferError()))) return true;
    // This is a refusal, never a prompt to overwrite either version.
    QMessageBox::critical(m_app.mainWindow(), QStringLiteral("未退出"),
        QStringLiteral("外部编辑文件的修改尚未确认读取或保存成功，已取消退出。"
                       "请保留当前窗口和外部文件，重新保存外部文件并确认笔记回写后再退出；"
                       "若笔记也已修改，请先分别保留两份内容。"));
    return false;
}

/*!
    Copy from Qt 5.15.2

    Splits the string \a command into a list of tokens, and returns
    the list.

    Tokens with spaces can be surrounded by double quotes; three
    consecutive double quotes represent the quote character itself.
*/
QStringList ExternalEditorLauncher::splitCommand(const QString &command)
{
    QStringList args;
    QString tmp;
    int quoteCount = 0;
    bool inQuote = false;

    // handle quoting. tokens can be surrounded by double quotes
    // "hello world". three consecutive double quotes represent
    // the quote character itself.
    for (int i = 0; i < command.size(); ++i) {
        if (command.at(i) == QLatin1Char('"')) {
            ++quoteCount;
            if (quoteCount == 3) {
                // third consecutive quote
                quoteCount = 0;
                tmp += command.at(i);
            }
            continue;
        }
        if (quoteCount) {
            if (quoteCount == 1)
                inQuote = !inQuote;
            quoteCount = 0;
        }
        if (!inQuote && command.at(i).isSpace()) {
            if (!tmp.isEmpty()) {
                args += tmp;
                tmp.clear();
            }
        } else {
            tmp += command.at(i);
        }
    }
    if (!tmp.isEmpty())
        args += tmp;

    return args;
}
