#include "ApiWizExplorerWindow.h"
#include "WizMainWindow.h"
#include "gui/documentviewer/WizDocumentView.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "share/WizTodo.h"

namespace {
bool saveTodoDate(WizDatabase& db, const QString& guid, const QString& date)
{
    WIZDOCUMENTDATA note;
    if (!db.isOpened() || db.isGroup() || !db.documentFromGuid(guid, note)
        || WizDatabase::isInDeletedItems(note.strLocation) || !db.canEditDocument(note))
        return false;
    if (!WizTodo::writeDate(guid, date, [&](const QString& sql) { return db.execSQL(sql); }))
        return false;
    for (const auto& field : WizTodo::parameters(date)) {
        WIZDOCUMENTPARAMDATA param;
        param.strKbGUID = db.kbGUID();
        param.strDocumentGuid = guid;
        param.strName = field.first;
        param.strValue = field.second;
        param.nVersion = -1;
        emit db.documentParamModified(param);
    }
    return true;
}
}

ApiWizExplorerWindow::ApiWizExplorerWindow(WizMainWindow* mw, QObject* parent)
    : QObject(parent)
    , m_mainWindow(mw)
{

}

QObject* ApiWizExplorerWindow::CurrentDocumentBrowserObject()
{
    return m_mainWindow->CurrentDocumentBrowserObject();
}

QObject *ApiWizExplorerWindow::CurrentDocument()
{
    WizDocumentView *docView = m_mainWindow->documentView();
    if (!docView) 
        return nullptr;

    WizDatabase &db = WizDatabaseManager::instance()->db(
        docView->note().strKbGUID
    );
    WIZDOCUMENTDATA data;
    if (db.documentFromGuid(docView->note().strGUID, data)) {
        return new WizDocument(db, data, this);
    } else {
        return nullptr;
    }
}

void ApiWizExplorerWindow::ViewDocument(QObject *pWizDocument, bool vbOpenInNewTab /* = true */)
{
    WizDocument *doc = qobject_cast<WizDocument *>(pWizDocument);
    if (doc) {
        WIZDOCUMENTDATAEX docData(doc->data());
        m_mainWindow->viewDocument(docData);
    }
}

void ApiWizExplorerWindow::ViewAttachment(QObject *pWizDocumentAttachment)
{
    WizDocumentAttachment *att = qobject_cast<WizDocumentAttachment *>(pWizDocumentAttachment);
    if (att) {
        m_mainWindow->viewAttachment(att->data());
    }
}

void ApiWizExplorerWindow::OpenMessageConsole()
{
    m_mainWindow->on_actionConsole_triggered();
}

QVariantMap ApiWizExplorerWindow::CreateTodo(const QString& title, const QString& date)
{
    if (!WizTodo::validTitle(title) || !WizTodo::validDate(date))
        return {{"ok", false}, {"error", "invalid-input"}};
    auto& db = m_mainWindow->DatabaseManagerEx()->db();
    if (!db.isOpened() || db.isGroup())
        return {{"ok", false}, {"error", "unavailable"}};
    WIZDOCUMENTDATA note;
    // Use the normal note initializer, including the user's encrypt-all policy.
    // Do not invent a Todo folder or touch the currently edited page/unsaved DOM.
    if (!db.createDocumentAndInit("<!DOCTYPE html><html><head></head><body><p><br/></p></body></html>",
            "", 0, title.trimmed(), "newnote", db.getDefaultNoteLocation(), "", note))
        return {{"ok", false}, {"error", "note-create-failed"}};
    const bool ok = saveTodoDate(db, note.strGUID, date);
    // On an ordinary metadata failure retain the created note, and return its
    // identity so the form can retry without creating duplicate notes.
    return {{"ok", ok}, {"guid", note.strGUID}, {"error", ok ? "" : "date-save-failed"}};
}

bool ApiWizExplorerWindow::SetTodoDate(const QString& guid, const QString& date)
{
    auto& db = m_mainWindow->DatabaseManagerEx()->db();
    return saveTodoDate(db, guid, date);
}
