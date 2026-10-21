#ifndef GUI_DOCUMENTVIEWER_WIZDOCUMENTVIEW_H
#define GUI_DOCUMENTVIEWER_WIZDOCUMENTVIEW_H

#include <QSharedPointer>
#include <QWidget>
#include <QPointer>
#include <QStackedWidget>

#include "share/WizObject.h"
#include "utils/ExternalEditorLauncher.h"
#include "gui/tabbrowser/AbstractTabPage.h"
#include "AbstractDocumentView.h"
#include "share/WizBiometricNoteAccess.h"

class QScrollArea;
class QThread;
class QLineEdit;
class QLabel;


struct WIZDOCUMENTDATA;
struct WIZDOCUMENTATTACHMENTDATA;
class WizExplorerApp;
class WizDatabaseManager;
class WizUserSettings;
class WizScrollBar;
class WizDocumentWebView;
class WizDatabase;
class WizSplitter;
class WizUserCipherForm;
class WizObjectDownloaderHost;
class QWebFrame;
class QWebEnginePage;
class WizWebEngineView;
class WizDocumentEditStatusSyncThread;
class WizDocumentStatusChecker;
class WizLocalProgressWebView;
class WizDocumentTransitionView;

class WizTitleBar;
class WizEditorToolBar;
class WizInfoBar;
class WizTagBar;
class JSPluginDocSidebar;

class WizDocumentView : public AbstractDocumentView
{
    Q_OBJECT

public:
    WizDocumentView(WizExplorerApp& app, QWidget* parent = nullptr);
    ~WizDocumentView();

    virtual QSize sizeHint() const override;
    void setSizeHint(QSize size);

    QWidget* client() const { return m_stack; }
    WizDocumentWebView* web() const { return m_web; }
    quint64 noteOpenSequence() const { return m_openSequence; }
    WizWebEngineView* commentView() const;
    WizLocalProgressWebView* commentWidget() const { return m_commentWidget; }
    WizDocumentTransitionView* transitionView() { return m_transitionView; }
    WizTitleBar* titleBar() { return m_title; }

    QString Title() override { return m_note.strTitle; }

    void waitForDone();
    bool waitForSave(bool force = false);
    void waitForThread();
    void RequestClose(bool force = false) override;

protected:
    WizExplorerApp& m_app;
    WizDatabaseManager& m_dbMgr;
    WizUserSettings& m_userSettings;
    WizObjectDownloaderHost* m_downloaderHost;
    WizDocumentTransitionView* m_transitionView;

    QStackedWidget* m_stack;
    QWidget* m_docView;
    WizDocumentWebView* m_web;
    WizWebEngineView* m_comments;
    JSPluginDocSidebar* m_pluginSidebar;
    WizLocalProgressWebView* m_commentWidget;
    WizSplitter* m_splitter;
    WizTitleBar* m_title;
    QWidget* m_blankView;

    WizUserCipherForm* m_passwordView;
    WizDocumentEditStatusSyncThread* m_editStatusSyncThread;
    QPointer<WizDocumentStatusChecker> m_editStatusChecker;
    QThread* m_editStatusCheckerThread = nullptr;

    virtual void showEvent(QShowEvent *event);
    void hideEvent(QHideEvent* event) override;
    virtual void resizeEvent(QResizeEvent* ev);

private:
    WIZDOCUMENTDATAEX m_note;
    bool m_bLocked; // note is force locked as readonly status
    WizEditorMode m_editorMode;
    WizDocumentViewMode m_defaultViewMode; // user defined editing mode
    bool m_noteLoaded;
    WizBiometricNoteAccess::Gate m_biometricGate;
    WizDocumentCallbackEpoch m_openEpoch;
    quint64 m_openSequence = 0;
    bool m_biometricEnabled = false, m_biometricPrepared = false;
    bool m_accessClosing = false, m_biometricReloadPending = false;
    //
    int m_editStatus;  // document edit or version status
    QSize m_sizeHint;
    WizEditorToolBar* m_editorBar;
    WizInfoBar* m_infoBar;

public:
    const WIZDOCUMENTDATAEX& note() const override { return m_note; }
    bool isLocked() const { return m_bLocked; }
    bool isEditing() const { return m_editorMode == modeEditor; }
    WizEditorMode editorMode() const override { return m_editorMode; }

    bool reload();
    void reloadNote();
    void setEditorFocus();
    bool noteLoaded() const { return m_noteLoaded; }
    bool canReadCurrentNote() const;
    bool canReadLegacyHiddenNote() const;
    WizDocumentCallbackEpoch::Ticket noteReadTicket() const { return m_biometricGate.ticket(); }
    bool acceptsNoteRead(const WizDocumentCallbackEpoch::Ticket& ticket) const;
    void changeType(QString type) { m_note.strType = type; }

    void initStat(const WIZDOCUMENTDATA& data, bool forceEdit);
    void viewNote(const WIZDOCUMENTDATAEX& data, bool forceEdit);
    void reviewCurrentNote();
    void setEditorMode(WizEditorMode mode);
    void setDefaultViewMode(WizDocumentViewMode mode);
    void setModified(bool modified);
    void settingsChanged();
    void sendDocumentSavedSignal(const QString& strGUID, const QString& strKbGUID);
    void resetTitle(const QString& strTitle);
    bool checkListClickable();
    void setStatusToEditingByCheckList();
    //
    void showCoachingTips();
    //
    void wordCount(std::function<void(const QString&)> callback);
    WizEditorToolBar *editorToolBar() { return m_editorBar; }

signals:
    void documentSaved(const QString& strGUID, WizDocumentView* viewer);
    void checkDocumentEditStatusRequest(const QString& strKbGUID, const QString& strGUID);
    void stopCheckDocumentEditStatusRequest(const QString& strKbGUID, const QString& strGUID);
    void viewNoteInExternalEditorRequest(const WizExternalEditorData &editorData, const WIZDOCUMENTDATAEX &noteData);
    void shareDocumentByLinkRequest(const QString& strKbGUID, const QString& strGUID);

public Q_SLOTS:
    void onViewNoteRequested(WizDocumentView* view, const WIZDOCUMENTDATAEX& doc, bool forceEditing);
    void onViewNoteLoaded(WizDocumentView*,const WIZDOCUMENTDATAEX&,bool);
    void onCloseNoteRequested(WizDocumentView* view);

    void onCipherCheckRequest();
    void onBiometricCredentialReady();

    void on_download_finished(const WIZOBJECTDATA& data, bool bSucceed);

    void on_document_modified(const WIZDOCUMENTDATA& documentOld,
                              const WIZDOCUMENTDATA& documentNew);
    void on_document_data_modified(const WIZDOCUMENTDATA& data);
    void on_document_data_changed(const QString& strGUID, WizDocumentView* viewer);
    void on_document_deleted(const WIZDOCUMENTDATA& data);

    void on_attachment_created(const WIZDOCUMENTATTACHMENTDATA& attachment);
    void on_attachment_deleted(const WIZDOCUMENTATTACHMENTDATA& attachment);

    //
    void on_checkEditStatus_finished(const QString& strGUID, bool editable);
    void on_checkEditStatus_timeout(const QString& strGUID);
    void on_documentEditingByOthers(QString strGUID, QStringList editors);
    void on_checkDocumentChanged_finished(const QString& strGUID, bool changed);
    void on_syncDatabase_request(const QString& strKbGUID);
    void on_webView_focus_changed();

    void on_notifyBar_link_clicked(const QString& link);

    void on_loadComment_request(const QString& url);

    void on_commentWidget_statusChanged();

    void on_viewNoteInExternalEditor_request(QString& Name, QString& ProgramFile,
                                                QString& Arguments, int TextEditor, int UTF8Encoding);
    void handleDiscardChangesRequest();
    void handleWindowCloseRequest();
    void handlePluginSidebarRequest(QAction *ac, bool checked);
    void shareNoteByEmail();
    void shareNoteByLink();
    void onEditorFocusIn();
    void onEditorFocusOut();
    void onTitleReturnPressed();
    void onTitleEditingFinished(const QString &newTitle);

private:
    WizBiometricNoteAccess::Context biometricContext() const;
    void revokeBiometricAccess(bool closing = false);
    bool prepareEncryptedAccess(WizDatabase& db);
    bool biometricRequestCurrent(const WizBiometricNoteAccess::Context& context,
                                 const WizBiometricNoteAccess::Gate::Ticket& ticket) const;
    bool completeBiometricUnlock(const QString& password, const WizBiometricNoteAccess::Context& context,
                                 const WizBiometricNoteAccess::Gate::Ticket& ticket);
    void loadNote(const WIZDOCUMENTDATAEX &doc);
    void downloadNoteFromServer(const WIZDOCUMENTDATA& note);
    void sendDocumentEditingStatus();
    void stopDocumentEditingStatus();
    void startCheckDocumentEditStatus();
    void stopCheckDocumentEditStatus();
    bool checkDocumentEditable(bool checklist);
    //
    void stopCheckDocumentAnimations();
    void getMailSender(std::function<void(QString)> callback);
    void showInfoBar();
    void showEditorBar();
};


#endif // GUI_DOCUMENTVIEWER_WIZDOCUMENTVIEW_H
