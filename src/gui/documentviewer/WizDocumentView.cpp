#include "WizDocumentView.h"

#include <QLineEdit>
#include <QLabel>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QApplication>
#include <QScreen>
#include <QGuiApplication>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QAction>
#include <QElapsedTimer>
#include <memory>

#include "share/WizGlobal.h"
#include "share/WizDocumentUiQuery.h"
#include "share/WizSessionAccess.h"
#include "share/WizObjectDataDownloader.h"
#include "database/WizDatabaseManager.h"
#include "share/WizSettings.h"
#include "share/WizUIHelper.h"
#include "sync/WizToken.h"
#include "sync/WizApiEntry.h"
#include "sync/WizKMServer.h"
#include "widgets/WizScrollBar.h"
#include "widgets/WizLocalProgressWebView.h"
#include "widgets/WizSegmentedButton.h"
#include "widgets/WizTipsWidget.h"
#include "widgets/WizEmailShareDialog.h"
#include "WizNoteStyle.h"
#include "WizDocumentTransitionView.h"
#include "gui/documentviewer/WizDocumentWebView.h"
#include "WizButton.h"
#include "WizUserCipherForm.h"
#include "WizNotifyBar.h"
#include "WizMainWindow.h"
#include "gui/documentviewer/WizEditorToolBar.h"
#include "gui/documentviewer/WizDocumentWebView.h"
#include "gui/documentviewer/WizDocumentEditStatus.h"
#include "gui/documentviewer/WizTitleBar.h"
#include "share/WizThreads.h"
#include "share/WizWebEngineView.h"
#include "share/WizEnc.h"
#include "share/WizMisc.h"
#include "jsplugin/JSPluginWidgets.h"
#include "jsplugin/JSPluginManager.h"
#include "jsplugin/JSPlugin.h"
#include "utils/WizPathResolve.h"
#include "WizInfoBar.h"
#include "WizTitleEdit.h"


#define DOCUMENT_STATUS_NOSTATUS            0x0000
#define DOCUMENT_STATUS_GROUP                 0x0001
#define DOCUMENT_STATUS_OFFLINE               0x0002
#define DOCUMENT_STATUS_FIRSTTIMEVIEW     0x0004
#define DOCUMENT_STATUS_EDITBYOTHERS   0x0008
#define DOCUMENT_STATUS_NEWVERSIONFOUNDED      0x0010
#define DOCUMENT_STATUS_PERSONAL           0x0020
#define DOCUMENT_STATUS_ON_EDITREQUEST       0x0040
#define DOCUMENT_STATUS_ON_CHECKLIST       0x0080

WizDocumentView::WizDocumentView(WizExplorerApp& app, QWidget* parent)
    : AbstractDocumentView(parent)
    , m_app(app)
    , m_dbMgr(app.databaseManager())
    , m_userSettings(app.userSettings())
    , m_commentWidget(new WizLocalProgressWebView(app.mainWindow()))
    , m_title(new WizTitleBar(app, this))
    , m_passwordView(new WizUserCipherForm(app, this))
    , m_defaultViewMode(app.userSettings().noteViewMode())
    , m_transitionView(new WizDocumentTransitionView(this))
    , m_bLocked(false)
    , m_editorMode(modeReader)
    , m_noteLoaded(false)
    , m_editStatusSyncThread(new WizDocumentEditStatusSyncThread(this))
    , m_editStatus(0)
    , m_sizeHint(QSize(200, 1))
    , m_comments(nullptr)
    , m_pluginSidebar(nullptr)
    , m_editorBar(new WizEditorToolBar(app, this))
    , m_infoBar(new WizInfoBar(app, this))
{
    // docView is the widget to view/edit document actually.
    QVBoxLayout* layoutDoc = new QVBoxLayout();
    layoutDoc->setContentsMargins(0, 0, 0, 0);
    layoutDoc->setSpacing(0);
    m_docView = new QWidget(this);
    m_docView->setLayout(layoutDoc);

    // Control different page via stack widget
    m_stack = new QStackedWidget(this);

    // This page will ask password to view encrypted note
    connect(m_passwordView, SIGNAL(cipherCheckRequest()), SLOT(onCipherCheckRequest()));
    connect(m_passwordView, &WizUserCipherForm::biometricCredentialReady,
            this, &WizDocumentView::onBiometricCredentialReady);
    connect(WizSessionAccess::Monitor::applicationMonitor(),&WizSessionAccess::Monitor::accessRetired,
            this,&WizDocumentView::onSessionAccessRetired);
    connect(WizBiometricNoteAccess::PolicyEvents::applicationEvents(),&WizBiometricNoteAccess::PolicyEvents::accessRetired,
            this,&WizDocumentView::onBiometricPolicyRetired);

    m_blankView = new QWidget(this);

    m_stack->addWidget(m_docView);
    m_stack->addWidget(m_passwordView);
    m_stack->addWidget(m_transitionView);
    m_stack->addWidget(m_blankView);
    m_stack->setCurrentWidget(m_blankView);
    m_stack->setBackgroundRole(QPalette::HighlightedText);

    // Setup comment widget
    m_comments = m_commentWidget->web();
    WizWebEngineInjectObjectCollection objects = {{"WizExplorerApp", WizGlobal::mainWindow()->publicAPIsObject()}};
    auto profile = createWebEngineProfile(objects, this);
    auto webPage = new WizWebEnginePage(objects, profile, m_comments);
    m_comments->setPage(webPage);
    m_comments->settings()->setAttribute(QWebEngineSettings::LocalStorageEnabled, true);
    m_comments->setAcceptDrops(false);
    connect(m_comments, SIGNAL(loadFinishedEx(bool)), m_title, SLOT(onCommentPageLoaded(bool)));
    connect(m_commentWidget, SIGNAL(widgetStatusChanged()), SLOT(on_commentWidget_statusChanged()));
    m_commentWidget->hide();

    // Editor Components
    QWidget* wgtEditor = new QWidget(m_docView);
    wgtEditor->setObjectName("editor-container");
    m_web = new WizDocumentWebView(app, wgtEditor);
    m_title->setEditor(m_web);
    m_editorBar->layout()->setAlignment(Qt::AlignVCenter);
    m_editorBar->setDelegate(m_web);
    connect(m_web, &WizDocumentWebView::focusIn, this, &WizDocumentView::onEditorFocusIn);
    connect(m_web, &WizDocumentWebView::focusOut, this, &WizDocumentView::onEditorFocusOut);
    connect(m_comments->getPage(), &WizWebEnginePage::linkClicked,
            m_web, &WizDocumentWebView::onEditorLinkClicked);

    QVBoxLayout* layoutEditor = new QVBoxLayout(wgtEditor);
    layoutEditor->setSpacing(0);
    layoutEditor->setContentsMargins(0, 5, 0, 0);

    QVBoxLayout* layoutTitle = new QVBoxLayout;
    layoutTitle->setContentsMargins(14, 0, 14, 6);
    layoutTitle->setSpacing(0);
    layoutTitle->addWidget(m_title);
    layoutTitle->addWidget(m_infoBar);
    layoutTitle->addWidget(m_editorBar);
    layoutTitle->addStretch();

    layoutEditor->addLayout(layoutTitle);
    layoutEditor->addWidget(m_web);
    layoutEditor->setStretchFactor(layoutTitle, 0);
    layoutEditor->setStretchFactor(m_web, 1);
    m_editorBar->hide();

    m_splitter = new WizSplitter(this);
    m_splitter->addWidget(wgtEditor);
    m_splitter->addWidget(m_commentWidget);
    m_splitter->setOrientation(Qt::Horizontal);

    layoutDoc->addWidget(m_splitter);

    QVBoxLayout* layoutMain = new QVBoxLayout(this);
    layoutMain->setContentsMargins(0, 0, 0, 0);
    setLayout(layoutMain);
    layoutMain->addWidget(m_stack);

    // 设置下载器
    WizMainWindow* mainWindow = qobject_cast<WizMainWindow *>(m_app.mainWindow());
    m_downloaderHost = mainWindow->downloaderHost();
    connect(m_downloaderHost, SIGNAL(downloadDone(const WIZOBJECTDATA&, bool)),
            SLOT(on_download_finished(const WIZOBJECTDATA&, bool)));

    connect(&m_dbMgr, SIGNAL(documentModified(const WIZDOCUMENTDATA&, const WIZDOCUMENTDATA&)),
            SLOT(on_document_modified(const WIZDOCUMENTDATA&, const WIZDOCUMENTDATA&)));

    connect(&m_dbMgr, SIGNAL(documentDataModified(const WIZDOCUMENTDATA&)),
            SLOT(on_document_data_modified(const WIZDOCUMENTDATA&)));

    connect(&m_dbMgr, SIGNAL(attachmentCreated(const WIZDOCUMENTATTACHMENTDATA&)),
            SLOT(on_attachment_created(const WIZDOCUMENTATTACHMENTDATA&)));

    connect(&m_dbMgr, SIGNAL(attachmentDeleted(const WIZDOCUMENTATTACHMENTDATA&)),
            SLOT(on_attachment_deleted(const WIZDOCUMENTATTACHMENTDATA&)));

    connect(&m_dbMgr, SIGNAL(documentUploaded(QString,QString)),
            m_editStatusSyncThread, SLOT(documentUploaded(QString,QString)));
    
    connect(&m_dbMgr, &WizDatabaseManager::documentDeleted, this, &WizDocumentView::on_document_deleted);

    // 连接浏览笔记请求信号
    connect(WizGlobal::instance(), SIGNAL(viewNoteRequested(WizDocumentView*,const WIZDOCUMENTDATAEX&,bool)),
            SLOT(onViewNoteRequested(WizDocumentView*,const WIZDOCUMENTDATAEX&,bool)));

    connect(WizGlobal::instance(), SIGNAL(viewNoteLoaded(WizDocumentView*,WIZDOCUMENTDATAEX,bool)),
            SLOT(onViewNoteLoaded(WizDocumentView*,const WIZDOCUMENTDATAEX&,bool)));

    connect(WizGlobal::instance(), SIGNAL(closeNoteRequested(WizDocumentView*)),
            SLOT(onCloseNoteRequested(WizDocumentView*)));

    connect(m_web, SIGNAL(focusIn()), SLOT(on_webView_focus_changed()));
    connect(m_web->page(), &QWebEnginePage::windowCloseRequested, this, &WizDocumentView::handleWindowCloseRequest);

    connect(m_title, SIGNAL(notifyBar_link_clicked(QString)), SLOT(on_notifyBar_link_clicked(QString)));
    connect(m_title, SIGNAL(loadComment_request(QString)), SLOT(on_loadComment_request(QString)), Qt::QueuedConnection);
    connect(m_title, SIGNAL(viewNoteInExternalEditor_request(QString&,QString&,QString&,int,int)), SLOT(on_viewNoteInExternalEditor_request(QString&,QString&,QString&,int,int)));
    connect(m_title, &WizTitleBar::discardChangesRequest,
            this, &WizDocumentView::handleDiscardChangesRequest);
    connect(m_title, &WizTitleBar::pluginSidebarRequest,
            this, &WizDocumentView::handlePluginSidebarRequest);
    connect(m_title, &WizTitleBar::shareNoteByEmailRequest,
            this, &WizDocumentView::shareNoteByEmail);
    connect(m_title, &WizTitleBar::shareNoteByLinkRequest,
            this, &WizDocumentView::shareNoteByLink);
    connect(m_title->getTitleEdit(), &WizTitleEdit::returnPressed,
            this, &WizDocumentView::onTitleReturnPressed);
    connect(m_title->getTitleEdit(), &WizTitleEdit::newTitleRequest,
            this, &WizDocumentView::onTitleEditingFinished);
    connect(m_title, &WizTitleBar::showPageZoomWidgetRequested,
            m_web, &WizWebEngineView::handleShowZoomWidgetRequest);
    connect(m_web, &WizDocumentWebView::zoomWidgetFinished,
            m_title, &WizTitleBar::onPageZoomWidgetClosed);

    // 编辑状态同步线程
    m_editStatusSyncThread->start(QThread::IdlePriority);

    m_editStatusChecker = new WizDocumentStatusChecker();
    connect(this, SIGNAL(checkDocumentEditStatusRequest(QString,QString)), m_editStatusChecker,
            SLOT(checkEditStatus(QString,QString)));
    connect(this, SIGNAL(stopCheckDocumentEditStatusRequest(QString,QString)),
            m_editStatusChecker, SLOT(stopCheckStatus(QString,QString)));
    connect(m_editStatusChecker, SIGNAL(checkEditStatusFinished(QString,bool)), \
            SLOT(on_checkEditStatus_finished(QString,bool)));
    connect(m_editStatusChecker, SIGNAL(checkTimeOut(QString)), \
            SLOT(on_checkEditStatus_timeout(QString)));
    connect(m_editStatusChecker, SIGNAL(documentEditingByOthers(QString,QStringList)), \
            SLOT(on_documentEditingByOthers(QString,QStringList)));
    connect(m_editStatusChecker, SIGNAL(checkDocumentChangedFinished(QString,bool)), \
            SLOT(on_checkDocumentChanged_finished(QString,bool)));

    QThread* checkThread = m_editStatusCheckerThread = new QThread(this);
    connect(checkThread, SIGNAL(started()), m_editStatusChecker, SLOT(initialise()));
    connect(checkThread, SIGNAL(finished()), m_editStatusChecker, SLOT(clearTimers()));
    connect(checkThread, &QThread::finished, m_editStatusChecker.data(), &QObject::deleteLater);
    m_editStatusChecker->moveToThread(checkThread);
    checkThread->start();

}

WizDocumentView::~WizDocumentView()
{
    m_passwordView->blockSignals(true);
    revokeBiometricAccess(true);
    m_web->cancelLinkInsert();
    m_web->cancelPdfExport();
    waitForThread();
}

QSize WizDocumentView::sizeHint() const
{
    return m_sizeHint;
}

void WizDocumentView::setSizeHint(QSize size)
{
    m_sizeHint = size;
}

void WizDocumentView::waitForDone()
{
    QPointer<WizDocumentView> self(this);
    revokeBiometricAccess(true);
    if (!self) return;
    m_web->cancelLinkInsert();
    m_web->cancelPdfExport();
    // Reuse the bounded, heap-owned result. A late WebEngine callback must not
    // write a returned stack variable or keep the GUI spinning indefinitely.
    if (!waitForSave()) qWarning() << "Document snapshot did not complete during cleanup";
    waitForThread();
}

bool WizDocumentView::waitForSave(bool force)
{
    // Unloaded/encrypted pages have no editable content to save.
    if (!noteLoaded()) return true;
    struct Result { bool done = false; bool ok = false; };
    const auto result = std::make_shared<Result>();
    m_web->trySaveDocument(m_note, force, [result](const QVariant& ret){
        result->ok = ret.toBool(); result->done = true;
    });
    QElapsedTimer deadline; deadline.start();
    while (!result->done && deadline.elapsed() < 30000)
    {
        QApplication::processEvents();
        QThread::msleep(5);
    }
    return result->done && result->ok;
}

void WizDocumentView::waitForThread()
{
    if (m_editStatusCheckerThread) {
        m_editStatusCheckerThread->requestInterruption();
        m_editStatusCheckerThread->quit();
    }
    m_editStatusSyncThread->waitForDone();
    if (m_editStatusCheckerThread) m_editStatusCheckerThread->wait();
}

void WizDocumentView::RequestClose(bool force /*= false*/)
{
    QPointer<WizDocumentView> self(this);
    revokeBiometricAccess(true);
    if (!self) return;
    m_web->cancelLinkInsert();
    m_web->cancelPdfExport();
    if (force) {
        // Force to close when Chromium process no response.
        handleWindowCloseRequest();
    } else {
        // We can not runJavaScript after RequestClose.
        if (!waitForSave()) {
            if (self) {
                m_accessClosing = false;
                if (m_biometricEnabled) prepareEncryptedAccess(m_dbMgr.db(m_note.strKbGUID));
            }
            return;
        }
        if (!self) return;
        m_web->triggerPageAction(QWebEnginePage::RequestClose);
    }
}

void WizDocumentView::handleWindowCloseRequest()
{
    QPointer<WizDocumentView> self(this);
    revokeBiometricAccess(true);
    if (!self) return;
    m_web->cancelLinkInsert();
    m_web->cancelPdfExport();
    // We Should exit all thread after user confirmed to close page.
    waitForThread();
    emit pageCloseRequested();
}

void WizDocumentView::handlePluginSidebarRequest(QAction *ac, bool checked)
{
    QString moduleGuid = ac->data().toString();
    if (moduleGuid.isEmpty())
        return;

    JSPluginManager &jsPluginMgr = JSPluginManager::instance();
    JSPluginModule *moduleData = jsPluginMgr.moduleByGUID(moduleGuid);
    if (!moduleData)
        return;

    static QPointer<QAction> lastAc = nullptr;
    if (m_pluginSidebar) {
        auto old = m_pluginSidebar->module()->spec()->guid();
        if (moduleData->spec()->guid() != old) {
            m_pluginSidebar->deleteLater();
            m_pluginSidebar = nullptr;
            if (lastAc) lastAc->setChecked(false);
        }
    }

    if (!m_pluginSidebar) {
        m_pluginSidebar = new JSPluginDocSidebar(m_app, moduleData, this);
        if (moduleData->spec()->sidebarLocation() == "Left")
            m_splitter->insertWidget(0, m_pluginSidebar);
        else
            m_splitter->addWidget(m_pluginSidebar);
        connect(m_web, &WizDocumentWebView::currentHtmlChanged,
                m_pluginSidebar, &JSPluginDocSidebar::documentHtmlChanged);
        connect(m_web, &WizDocumentWebView::loadFinishedEx,
                m_pluginSidebar, &JSPluginDocSidebar::editorLoadFinished);
    }

    if (checked) {
        moduleData->emitShowEvent();
        m_pluginSidebar->setVisible(true);
    } else {
        m_pluginSidebar->setVisible(false);
    }

    lastAc = ac;
}

WizWebEngineView*WizDocumentView::commentView() const
{
    return m_commentWidget->web();
}

void WizDocumentView::showEvent(QShowEvent *event)
{
    const QPointer<WizDocumentView> self(this);
    QWidget::showEvent(event);
    if (!self) return;
    if (m_biometricEnabled && !m_note.strGUID.isEmpty() && !m_accessClosing)
        if (!prepareEncryptedAccess(m_dbMgr.db(m_note.strKbGUID))) return;
    if (self && !m_accessClosing) {
        m_web->resumePendingDocumentLoad();
        if (self) m_web->resumeEditorSetup();
    }
}

void WizDocumentView::hideEvent(QHideEvent* event)
{
    QPointer<WizDocumentView> self(this);
    // Hiding cancels authentication, not a pending save/navigation completion.
    m_biometricGate.revoke();
    m_biometricPrepared = false;
    m_web->cancelLinkInsert();
    if (!self) return;
    m_web->cancelFindReplace();
    if (!self) return;
    if (m_biometricEnabled) m_web->closeDevTools();
    if (!self) return;
    m_passwordView->cancelBiometric();
    if (!self) return;
    // Conceal the existing view, but keep its DOM and save cache intact.
    if (m_biometricEnabled) {
        // Only cancel the output reservation, never release the print/save barrier.
        m_web->cancelPdfExport();
        m_stack->setCurrentWidget(m_passwordView);
    }
    if (self) QWidget::hideEvent(event);
}

void WizDocumentView::resizeEvent(QResizeEvent* ev)
{
    QWidget::resizeEvent(ev);
    qDebug() << "oldSize: " << ev->oldSize() << ", newSize: " << ev->size();

    m_editorBar->adjustButtonPosition();
}

/**
 * @brief 处理“浏览笔记请求”信号，
 * @param view 文档视图
 * @param doc 文档数据
 * @param forceEditing 是否强制编辑
 */
void WizDocumentView::onViewNoteRequested(WizDocumentView* view, const WIZDOCUMENTDATAEX& doc, bool forceEditing)
{
    // 只处理指定为本视图的信号
    if (view != this)
        return;

    if (doc.tCreated.secsTo(QDateTime::currentDateTime()) <= 1) {
        // 新建笔记
        viewNote(doc, forceEditing);
        m_title->clearAndSetPlaceHolderText(doc.strTitle);
    } else {
        // 已有笔记
        m_title->clearPlaceHolderText();
        viewNote(doc, forceEditing);
    }
}

void WizDocumentView::onViewNoteLoaded(WizDocumentView* view, const WIZDOCUMENTDATAEX& doc, bool bOk)
{
}

bool WizDocumentView::reload()
{
    bool ret = m_dbMgr.db(m_note.strKbGUID).documentFromGuid(m_note.strGUID, m_note);
    m_title->updateInfo(note());
    m_infoBar->setDocument(note());
    emit titleChanged(m_note.strTitle);

    return ret;
}

void WizDocumentView::reloadNote()
{
    reload();
    WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
    if (m_biometricEnabled || ((m_note.nProtected || db.isEncryptAllData())
        && WizBiometricNoteAccess::optedIn(m_userSettings, biometricContext().scope))) {
        m_biometricReloadPending = true;
        QPointer<WizDocumentView> self(this);
        if (!prepareEncryptedAccess(db) || !self) return;
        m_biometricReloadPending = false;
    }
    m_web->reloadNoteData(note());
}


void WizDocumentView::initStat(const WIZDOCUMENTDATA& data, bool forceEdit)
{
    m_bLocked = false;
    int nLockReason = -1;

    if (m_dbMgr.db(data.strKbGUID).isGroup()) {
        if (!forceEdit) {
            nLockReason = WizNotifyBar::LockForGruop;
            m_bLocked = true;
        }
    } else if (!m_dbMgr.db(data.strKbGUID).canEditDocument(data)) {
        nLockReason = WizNotifyBar::PermissionLack;
        m_bLocked = true;
    } else if (WizDatabase::isInDeletedItems(data.strLocation)) {
        nLockReason = WizNotifyBar::Deleted;
        m_bLocked = true;
    }

    m_editorMode = modeReader;
    if (!m_bLocked) {
        // Decide once in the host, before title/toolbar and page initialization.
        // A page-only new-note override leaves the host believing it is reading.
        const auto seconds = data.tCreated.secsTo(QDateTime::currentDateTime());
        const bool newlyCreated = seconds >= 0 && seconds <= 1;
        if (forceEdit || m_defaultViewMode == viewmodeAlwaysEditing || newlyCreated) {
            m_editorMode = modeEditor;
        }
    }

    emit m_web->canEditNoteChanged();

    bool bGroup = m_dbMgr.db(data.strKbGUID).isGroup();
    m_editStatus = m_editStatus & DOCUMENT_STATUS_PERSONAL;
    if (bGroup)
    {
        m_editStatus = m_editStatus | DOCUMENT_STATUS_GROUP | DOCUMENT_STATUS_FIRSTTIMEVIEW;
    }

    m_title->setLocked(m_bLocked, nLockReason, bGroup);
    //
    //   个人笔记检查笔记是否已经更新了
    //暂时禁止，等待服务器优化速度
    if (WizNotifyBar::LockForGruop == nLockReason)
    {
        startCheckDocumentEditStatus();
    }

    if (m_editorMode == modeEditor)
    {
        showCoachingTips();
    }
}

/**
 * @brief View note in current document view.
 * @param wizDoc Note meta data.
 * @param forceEdit Force to edit or not.
 */
void WizDocumentView::viewNote(const WIZDOCUMENTDATAEX& wizDoc, bool forceEdit)
{
    QPointer<WizDocumentView> self(this);
    const auto pendingLoad = m_web->pendingLoadSequence();
    const auto requestSequence = ++m_openSequence;
    m_accessClosing = false;
    revokeBiometricAccess();
    if (!self || m_openSequence != requestSequence || m_accessClosing) return;
    const auto opening = m_openEpoch.ticket();
    const bool hadLoadedNote = m_noteLoaded;
    m_web->cancelLinkInsert();
    m_web->cancelPdfExport();
    WIZDOCUMENTDATAEX dataTemp = wizDoc;

    m_web->trySaveDocument(m_note, false, [=](const QVariant& ret){
        // The save always completes; only its stale UI continuation is dropped.
        // An unloaded password/blank page has no snapshot to save (false is normal).
        if (!self || !WizDocumentCallbackEpoch::current(opening) || m_accessClosing) return;
        if (hadLoadedNote && (ret.userType() != QMetaType::Bool || !ret.toBool())) {
            m_web->continuePendingLoadAfterFailedOpen(pendingLoad);
            if (!self || !WizDocumentCallbackEpoch::current(opening) || m_accessClosing) return;
            if (m_biometricEnabled) prepareEncryptedAccess(m_dbMgr.db(m_note.strKbGUID));
            return;
        }

        WIZDOCUMENTDATAEX data = dataTemp;

        if (m_dbMgr.db(m_note.strKbGUID).isGroup())
        {
            stopDocumentEditingStatus();
        }

        m_noteLoaded = false;
        m_biometricReloadPending = false;
        m_note = data;
        m_biometricEnabled = false;
        initStat(data, forceEdit);

        // download document if not exist
        WizDatabase& db = m_dbMgr.db(data.strKbGUID);
        QString strDocumentFileName = db.getDocumentFileName(data.strGUID);
        if (!db.isObjectDataDownloaded(data.strGUID, "document") || \
                !WizPathFileExists(strDocumentFileName))
        {
            m_web->clear();

            m_stack->setCurrentWidget(m_transitionView);
            downloadNoteFromServer(data);

            return;
        }

        // ask user cipher if needed
        data.nProtected = WizZiwReader::isEncryptedFile(strDocumentFileName) ? 1 : 0;
        m_note.nProtected = data.nProtected;
        if (data.nProtected || db.isEncryptAllData()) {
            //
            db.initCert(false);
            //
            if(!db.loadUserCert()) {
                return;
            }
            //
            if (!prepareEncryptedAccess(db) || !self) return;
        }
        //
        m_stack->setCurrentWidget(m_docView);
        if (!self || !WizDocumentCallbackEpoch::current(opening) || m_accessClosing) return;

        // Emit notification of change regarding ownership of this note
        emit m_web->isPersonalDocumentChanged();
        if (!self || !WizDocumentCallbackEpoch::current(opening) || m_accessClosing) return;
        emit m_web->hasEditPermissionOnCurrentNoteChanged();
        if (!self || !WizDocumentCallbackEpoch::current(opening) || m_accessClosing) return;

        loadNote(data);
        if (!self || !WizDocumentCallbackEpoch::current(opening) || m_accessClosing) return;
        WIZDOCUMENTDATA docData = data;
        docData.nReadCount ++;
        db.modifyDocumentReadCount(docData);
        docData.tAccessed = WizGetCurrentTime();
        db.modifyDocumentDateAccessed(docData);

        emit titleChanged(docData.strTitle);
    });
}

void WizDocumentView::reviewCurrentNote()
{
    Q_ASSERT(!m_note.strGUID.isEmpty());

    initStat(m_note, m_editorMode == modeEditor);

    // download document if not exist
    WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
    QString strDocumentFileName = db.getDocumentFileName(m_note.strGUID);
    if (!db.isObjectDataDownloaded(m_note.strGUID, "document") || \
            !WizPathFileExists(strDocumentFileName))
    {
        downloadNoteFromServer(m_note);

        return;
    }

    // ask user cipher if needed
    if (m_note.nProtected || db.isEncryptAllData()) {
        if(!db.loadUserCert()) {
            return;
        }

        QPointer<WizDocumentView> self(this);
        if (!prepareEncryptedAccess(db) || !self) return;
    }

    if (m_stack->currentWidget() != m_docView) {
        m_stack->setCurrentWidget(m_docView);
    }
}

/*!
    Set the \a mode of editor and try to save document before switching mode.
    Editor will check and send group notes editing status.
*/
void WizDocumentView::setEditorMode(WizEditorMode mode)
{
    if (m_bLocked)
        return;

    if (m_editorMode == mode)
        return;

    bool edit = mode == modeEditor;
    bool read = !edit;

    // 检查文档的团队编辑状态
    bool isGroupNote = m_dbMgr.db(m_note.strKbGUID).isGroup();
    if (edit && isGroupNote)
    {
        m_title->startEditButtonAnimation();
        if (!checkDocumentEditable(false))
        {
            return;
        }
        // stop check document edit status while enter editing mode
        stopCheckDocumentEditStatus();
    }

    // 设置文档视图编辑器状态
    m_editorMode = mode;

    if (read)
    {
        m_editStatus = DOCUMENT_STATUS_NOSTATUS;

        // 保存标题，防止因多线程保存引起覆盖
        m_title->onTitleEditFinished();
        m_title->hideMessageTips(false);
    }

    if (mode == modeReader)
    {
        showInfoBar();
        m_editorBar->switchToNormalMode();
    }
    else
    {
        m_editorBar->switchToNormalMode();
        showEditorBar();
    }

    m_title->setEditorMode(mode);

    m_web->setEditorMode(mode);

    // 发送团队文档编辑状态
    if (isGroupNote)
    {
        if (m_editorMode == modeEditor)
        {
            sendDocumentEditingStatus();
        }
        else
        {
            stopDocumentEditingStatus();
            startCheckDocumentEditStatus();
        }
    }

    emit m_web->canEditNoteChanged();
}

void WizDocumentView::setDefaultViewMode(WizDocumentViewMode mode)
{
    m_defaultViewMode = mode;

    switch (m_defaultViewMode)
    {
    case viewmodeAlwaysEditing:
        setEditorMode(modeEditor);
        break;
    case viewmodeAlwaysReading:
        setEditorMode(modeReader);
        break;
    default:
        //Q_ASSERT(0);           //when set to the auto mode,do nonthing
        break;
    }
}

void WizDocumentView::settingsChanged()
{
    setDefaultViewMode(m_userSettings.noteViewMode());
}

void WizDocumentView::sendDocumentSavedSignal(const QString& strGUID, const QString& strKbGUID)
{
    if (m_note.strGUID != strGUID || m_note.strKbGUID != strKbGUID) return;
    const QPointer<WizDocumentView> self(this);
    const QString savedGuid=strGUID,savedKb=strKbGUID;
    WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
    if (db.isGroup())
    {
        QString strUserAlias = db.getUserAlias();
        m_editStatusSyncThread->documentSaved(strUserAlias, savedKb, savedGuid);
        if (!self) return;
    }

    emit documentSaved(savedGuid, this);
    if (!self || m_note.strGUID != savedGuid || m_note.strKbGUID != savedKb) return;
    //FIXME: Is it really necessary?
    const QString title=m_note.strTitle;
    emit titleChanged(title);
}

void WizDocumentView::resetTitle(const QString& strTitle)
{
    m_title->resetTitle(strTitle);
}

bool WizDocumentView::checkListClickable()
{
    WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
    if (db.canEditDocument(m_note))
    {
        //m_title->showMessageTips(Qt::PlainText, tr("Checking whether checklist is clickable..."));
        setCursor(Qt::WaitCursor);
        return checkDocumentEditable(true);
    }
    return false;
}

void WizDocumentView::setStatusToEditingByCheckList()
{
    stopCheckDocumentEditStatus();
    sendDocumentEditingStatus();
    m_title->showMessageTips(Qt::PlainText, tr("You have occupied this note by clicking checklist !  " \
                                               "Switch to other notes to free this note."));
}

void WizDocumentView::showCoachingTips()
{
    if (WizTipsWidget* tipWidget = m_editorBar->showCoachingTips())
    {
        connect(tipWidget, SIGNAL(finished()), m_title, SLOT(showCoachingTips()));
    }
    else
    {
        m_title->showCoachingTips();
    }
}

void WizDocumentView::wordCount(std::function<void(const QString&)> callback)
{
    const auto read = m_web->noteReadPermit();
    if (!read.current() || !callback) return;
    WizDocumentUiQuery::string(this, m_web->page(), m_web->documentCallbackTicket(),
        QStringLiteral("WizReader.getWordCount()"), [read,callback](const QString& count) {
            if (read.current()) callback(count);
        });
}

void WizDocumentView::setEditorFocus()
{
    const QPointer<WizDocumentView> self(this);
    const QPointer<WizDocumentWebView> web(m_web);
    const auto read = web->noteReadPermit();
    if (!read.current()) {
        if (m_stack->currentWidget() == m_passwordView) m_passwordView->setCipherEditorFocus();
        return;
    }
    if (m_stack->currentWidget() != m_docView) return;
    web->setFocus(Qt::MouseFocusReason);
    if (self && web && self->m_web == web && read.current()) web->editorFocus();
}


void WizDocumentView::on_attachment_created(const WIZDOCUMENTATTACHMENTDATA& attachment)
{
    if (attachment.strDocumentGUID != note().strGUID)
        return;

    reload();
}

void WizDocumentView::on_attachment_deleted(const WIZDOCUMENTATTACHMENTDATA& attachment)
{
    if (attachment.strDocumentGUID != note().strGUID)
        return;

    reload();
}

void WizDocumentView::on_checkEditStatus_finished(const QString& strGUID, bool editable)
{
//    qDebug() << "check eidt status finished , editable  : " << editable;
    if (strGUID == m_note.strGUID)
    {
        if (editable)
        {
            WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
            WIZDOCUMENTDATA doc;
            db.documentFromGuid(strGUID, doc);
            if (db.canEditDocument(doc) && !WizDatabase::isInDeletedItems(doc.strLocation))
            {
                //            qDebug() << "document editable , hide message tips.";
                if (0 == (m_editStatus & DOCUMENT_STATUS_NEWVERSIONFOUNDED))
                {
                    m_title->hideMessageTips(true);
                }
                m_title->setEditButtonEnabled(true);
                m_bLocked = false;
            }
        }
        stopCheckDocumentAnimations();
    }
}

void WizDocumentView::loadNote(const WIZDOCUMENTDATAEX& doc)
{
    const QPointer<WizDocumentView> self(this);
    const auto opening = m_openEpoch.ticket();
    const auto sequence = m_openSequence;
    const WIZDOCUMENTDATAEX document = doc;
    const auto current = [self,opening,sequence] {
        return self && WizDocumentCallbackEpoch::current(opening)
            && self->m_openSequence == sequence && !self->m_accessClosing;
    };
    m_web->viewDocument(document, m_editorMode);
    if (!current()) return;
    m_title->setNote(document, m_editorMode, m_bLocked);
    if (!current()) return;
    m_infoBar->setDocument(document);
    if (!current()) return;

    // save last
    m_note = document;

    m_noteLoaded = true;

    if (m_editorMode == modeEditor && m_web->hasFocus())
    {
        sendDocumentEditingStatus();
    }
}

void WizDocumentView::downloadNoteFromServer(const WIZDOCUMENTDATA& note)
{
    connect(m_downloaderHost, SIGNAL(downloadProgress(QString,int,int)),
            m_transitionView, SLOT(onDownloadProgressChanged(QString,int,int)), Qt::UniqueConnection);
    m_downloaderHost->downloadDocument(note);
    m_transitionView->showAsMode(note.strGUID, WizDocumentTransitionView::Downloading);
}

void WizDocumentView::sendDocumentEditingStatus()
{
    WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
    if (db.isGroup())
    {
        QString strUserAlias = db.getUserAlias();
        m_editStatusSyncThread->startEditingDocument(strUserAlias, m_note.strKbGUID, m_note.strGUID);
    }
}

void WizDocumentView::stopDocumentEditingStatus()
{
    WIZDOCUMENTDATA doc = m_note;
    if (m_dbMgr.db(doc.strKbGUID).documentFromGuid(doc.strGUID, doc))
    {
        bool bModified = doc.nVersion == -1;
        m_editStatusSyncThread->stopEditingDocument(doc.strKbGUID, doc.strGUID, bModified);
    }
}

void WizDocumentView::startCheckDocumentEditStatus()
{
    //首先检查笔记是否是待下载状态，如果一篇笔记一直打开，自动同步会把笔记状态设置为待下载，
    //但是其版本号是服务器的版本号，检查新版本无效果

    WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
    if (!db.isDocumentDownloaded(m_note.strGUID))
    {
        on_checkDocumentChanged_finished(m_note.strGUID, true);
        m_title->stopEditButtonAnimation();
        return;
    }

    m_editStatus = DOCUMENT_STATUS_NOSTATUS;
    emit checkDocumentEditStatusRequest(m_note.strKbGUID, m_note.strGUID);
}

void WizDocumentView::stopCheckDocumentEditStatus()
{
    emit stopCheckDocumentEditStatusRequest(m_note.strKbGUID, m_note.strGUID);
}

bool WizDocumentView::checkDocumentEditable(bool checklist)
{
    QEventLoop loop;
    connect(m_editStatusChecker, SIGNAL(checkEditStatusFinished(QString,bool)), &loop, SLOT(quit()));
    connect(m_editStatusChecker, SIGNAL(checkTimeOut(QString)), &loop, SLOT(quit()));
    startCheckDocumentEditStatus();
    m_editStatus = m_editStatus | DOCUMENT_STATUS_ON_EDITREQUEST;
    if (checklist)
    {
        m_editStatus |= DOCUMENT_STATUS_ON_CHECKLIST;
    }
    loop.exec();
    //

    bool editByOther = m_editStatus & DOCUMENT_STATUS_EDITBYOTHERS;
    bool newVersion = m_editStatus & DOCUMENT_STATUS_NEWVERSIONFOUNDED;
    bool isOffLine = m_editStatus & DOCUMENT_STATUS_OFFLINE;
    return !(editByOther || newVersion) || isOffLine;
}

void WizDocumentView::stopCheckDocumentAnimations()
{
    if (cursor().shape() != Qt::ArrowCursor)
    {
        setCursor(Qt::ArrowCursor);
    }
    m_title->stopEditButtonAnimation();
}

void WizDocumentView::on_document_modified(const WIZDOCUMENTDATA& documentOld, const WIZDOCUMENTDATA& documentNew)
{
    Q_UNUSED(documentOld);

    if (note().strGUID != documentNew.strGUID)
        return;

    reload();
}

void WizDocumentView::onCloseNoteRequested(WizDocumentView *view)
{
    if (view != this) return;
    QPointer<WizDocumentView> self(this);
    revokeBiometricAccess(true);
    if (!self) return;
    m_stack->setCurrentWidget(m_blankView);
}

// Begin extracted biometric routing methods (isolated synthetic shell tests).
bool WizDocumentView::canReadCurrentNote() const
{
    if (!m_noteLoaded || m_accessClosing) return false;
    const auto context = biometricContext();
    WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
    const bool required = m_biometricEnabled || ((m_note.nProtected || db.isEncryptAllData())
        && WizBiometricNoteAccess::optedIn(m_userSettings,context.scope));
    if (!required) return true; // No change to legacy/non-encrypted visibility policy.
    return m_biometricEnabled && isVisible() && m_stack->currentWidget() == m_docView
        && m_biometricGate.current(context)
        && !m_biometricGate.requiresAuthorization(!db.getCertPassword().isEmpty());
}

bool WizDocumentView::acceptsNoteRead(const WizDocumentCallbackEpoch::Ticket& ticket) const
{
    const auto own = m_biometricGate.ticket();
    return canReadCurrentNote() && WizDocumentCallbackEpoch::current(ticket)
        && !own.owner_before(ticket) && !ticket.owner_before(own);
}

bool WizDocumentView::canReadLegacyHiddenNote() const
{
    return !m_biometricEnabled && canReadCurrentNote();
}

WizBiometricNoteAccess::Context WizDocumentView::biometricContext() const
{
    return {WizBiometricNoteAccess::snapshot(m_dbMgr.db(),m_dbMgr.db(m_note.strKbGUID),WizCommonApiEntry::server()),
            m_note.strGUID,m_openSequence};
}

void WizDocumentView::revokeBiometricAccess(bool closing,bool preserveCapture)
{
    const QPointer<WizDocumentView> self(this);
    m_openEpoch.invalidate();
    m_biometricGate.revoke();
    m_biometricPrepared = false;
    if (closing) m_accessClosing = true;
    m_web->cancelLinkInsert();
    if (!self) return;
    m_web->cancelFindReplace();
    if (!self) return;
    if(!preserveCapture)m_web->cancelScreenShot(!closing);
    if (!self) return;
    m_web->closeDevTools();
    if (!self) return;
    m_passwordView->cancelBiometric();
}

void WizDocumentView::onSessionAccessRetired(bool hard)
{
    // Legacy/non-opted-in behavior is unchanged. Retire access, not the saved
    // password, DOM, snapshot producer or queued local saves.
    if(!m_biometricEnabled||m_accessClosing)return;
    // The native Touch ID panel itself may temporarily deactivate the app.
    // This exception preserves only an explicit pending native request while
    // the note is ALREADY locked. It preserves no body grant; the widget holds
    // success until active. Hard boundaries and hide/navigation still revoke.
    if(!hard && m_stack->currentWidget()==m_passwordView
        && m_biometricGate.requiresAuthorization(true)
        && m_passwordView->preserveNativeAuthenticationOnInactive()) return;
    const QPointer<WizDocumentView> self(this);
    revokeBiometricAccess(false,!hard);
    if(!self)return;
    m_web->cancelPdfExport();
    if(!self)return;
    m_stack->setCurrentWidget(m_passwordView);
    // No focus/reauthentication or navigation while the session is inactive.
}

void WizDocumentView::onBiometricPolicyRetired(const QString& policy)
{
    if(m_accessClosing||m_note.strGUID.isEmpty())return;
    auto& db=m_dbMgr.db(m_note.strKbGUID);
    if(!m_note.nProtected&&!db.isEncryptAllData())return;
    const auto context=biometricContext();
    if(policy.isEmpty()||policy!=WizBiometricNoteAccess::policyIdentity(context.scope))return;
    if(!m_biometricEnabled&&!WizBiometricNoteAccess::optedIn(m_userSettings,context.scope))return;
    // New opt-in must upgrade a previously legacy gate before showing password;
    // certificate revisions retain the policy but never retain the old grant.
    m_biometricGate.begin(context,true);m_biometricEnabled=true;
    const QPointer<WizDocumentView> self(this);
    onSessionAccessRetired();
    if(!self||m_accessClosing||!m_biometricGate.current(context))return;
    // Policy changes may lock an already-open legacy note without another
    // review/navigation. Refresh that password page now, without focus, read,
    // authentication or body reload (including inactive/background windows).
    m_biometricPrepared=true;
    m_passwordView->prepareBiometric(context,true);
    if(!self||m_accessClosing||!m_biometricGate.current(context))return;
    m_passwordView->setPasswordText(db.isGroup()?tr("Encrypted group password"):tr("Personal notes certificate password"));
    if(!self||m_accessClosing||!m_biometricGate.current(context))return;
    m_passwordView->setHint(db.getCertPasswordHint());
}

void WizDocumentView::awaitScreenShotAuthorization()
{
    if(m_accessClosing||!m_biometricEnabled)return;
    const QPointer<WizDocumentView> self(this);
    revokeBiometricAccess(false,true);if(!self)return;
    if(!prepareEncryptedAccess(m_dbMgr.db(m_note.strKbGUID))&&self)
        m_passwordView->setPasswordText(tr("重新输入证书密码以插入刚才的截图；切换笔记将丢弃截图"));
}

bool WizDocumentView::prepareEncryptedAccess(WizDatabase& db)
{
    if (m_accessClosing) return false;
    if (!m_openSequence) ++m_openSequence;
    const auto context = biometricContext();
    if(!m_note.nProtected&&!db.isEncryptAllData()){
        if(m_biometricEnabled){m_biometricGate.begin(context,false);m_biometricEnabled=false;m_biometricPrepared=false;}
        return true;
    }
    const bool enabled = WizBiometricNoteAccess::optedIn(m_userSettings,context.scope)
        || (m_biometricEnabled && WizBiometricNoteAccess::policyIdentity(context.scope).isEmpty());
    if (enabled != m_biometricEnabled || !m_biometricGate.current(context)) {
        m_biometricGate.begin(context,enabled);
        m_biometricEnabled = enabled;
        m_biometricPrepared = false;
    }
    if (!m_biometricGate.requiresAuthorization(!db.getCertPassword().isEmpty())) return true;
    QPointer<WizDocumentView> self(this);
    const auto ticket = m_biometricGate.ticket();
    if (!m_biometricPrepared) {
        m_biometricPrepared = true;
        m_passwordView->prepareBiometric(context,enabled);
        if (!self || !WizDocumentCallbackEpoch::current(ticket)) return false;
    }
    m_passwordView->setPasswordText(db.isGroup() ? tr("Encrypted group password") : tr("Personal notes certificate password"));
    m_passwordView->setHint(db.getCertPasswordHint());
    m_stack->setCurrentWidget(m_passwordView);
    if (self && WizDocumentCallbackEpoch::current(ticket)) m_passwordView->setCipherEditorFocus();
    return false;
}

bool WizDocumentView::biometricRequestCurrent(const WizBiometricNoteAccess::Context& context,
                                             const WizBiometricNoteAccess::Gate::Ticket& ticket) const
{
    const auto current = biometricContext();
    return !m_accessClosing && isVisible() && m_biometricEnabled
        && WizDocumentCallbackEpoch::current(ticket) && m_biometricGate.current(context)
        && current.documentGuid == context.documentGuid && current.openSequence == context.openSequence
        && WizBiometricKeychain::itemIdentity(current.scope) == WizBiometricKeychain::itemIdentity(context.scope)
        && WizBiometricNoteAccess::optedIn(m_userSettings,current.scope);
}

bool WizDocumentView::completeBiometricUnlock(const QString& password, const WizBiometricNoteAccess::Context& context,
                                             const WizBiometricNoteAccess::Gate::Ticket& ticket)
{
    if (!biometricRequestCurrent(context,ticket)) return false;
    WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
    const auto& cert = context.scope.certificate;
    if (!db.cacheCertPasswordLocally(password,cert.n,cert.e,cert.encryptedD)
        || !biometricRequestCurrent(context,ticket) || !m_biometricGate.grant(context,ticket)) return false;
    const auto data = m_note;
    QPointer<WizDocumentView> self(this);
    m_passwordView->cipherCorrect();
    if (!self || !biometricRequestCurrent(context,ticket)) return false;
    m_stack->setCurrentWidget(m_docView);
    if (!self || !biometricRequestCurrent(context,ticket)) return false;
    // Reauth after hide must not reload/discard a still-dirty editor.
    if (!m_noteLoaded) loadNote(data);
    else {
        const bool resumed = m_web->resumePendingDocumentLoad();
        if (!self || !biometricRequestCurrent(context,ticket)) return false;
        if (resumed) m_biometricReloadPending = false;
        else if (m_biometricReloadPending) {
            m_biometricReloadPending = false;
            m_web->reloadNoteData(data);
        }
        if (!self || !biometricRequestCurrent(context,ticket)) return false;
        m_web->resumeEditorSetup();
    }
    if(self&&biometricRequestCurrent(context,ticket))m_web->resumePendingScreenShot();
    return true;
}

void WizDocumentView::onBiometricCredentialReady()
{
    const auto context = biometricContext();
    const auto ticket = m_biometricGate.ticket();
    if (m_stack->currentWidget() != m_passwordView || !biometricRequestCurrent(context,ticket)) return;
    QPointer<WizDocumentView> self(this);
    const auto credential = m_passwordView->takeBiometricCredential(context);
    if (!self || !credential || !biometricRequestCurrent(context,ticket)) return;
    completeBiometricUnlock(credential.password,context,ticket);
}
// End extracted biometric routing methods.

void WizDocumentView::onCipherCheckRequest()
{
    const WIZDOCUMENTDATAEX noteData = note();
    WizDatabase& db = m_dbMgr.db(noteData.strKbGUID);
    //
    QString password = m_passwordView->userCipher();
    const auto context = biometricContext();
    if (m_biometricEnabled || WizBiometricNoteAccess::optedIn(m_userSettings,context.scope)) {
        const auto ticket = m_biometricGate.ticket();
        if (!biometricRequestCurrent(context,ticket)) { prepareEncryptedAccess(db); return; }
        QPointer<WizDocumentView> self(this);
        if (completeBiometricUnlock(password,context,ticket) || !self) return;
        if (!biometricRequestCurrent(context,ticket)) return;
        m_passwordView->cipherError();
        WizMessageBox::information(qApp->activeWindow(), tr("WizNote"), tr("Invalid password."));
        return;
    }
    if (m_accessClosing) return;
    QPointer<WizDocumentView> self(this);
    const auto opening = m_openEpoch.ticket();
    const bool verified = db.verifyCertPassword(password);
    if (!self || !WizDocumentCallbackEpoch::current(opening) || m_accessClosing) return;
    if (!verified)
    {
        m_passwordView->cipherError();
        WizMessageBox::information(qApp->activeWindow(), tr("WizNote"), tr("Invalid password."));

        return;
    }
    //
    m_passwordView->cipherCorrect();
    if (!self || !WizDocumentCallbackEpoch::current(opening) || m_accessClosing) return;

    m_stack->setCurrentWidget(m_docView);
    if (!self || !WizDocumentCallbackEpoch::current(opening) || m_accessClosing) return;
    loadNote(noteData);
}

void WizDocumentView::on_download_finished(const WIZOBJECTDATA &data, bool bSucceed)
{
    if (m_note.strKbGUID != data.strKbGUID
            || m_note.strGUID != data.strObjectGUID)
        return;

    if (!bSucceed)
    {
        m_transitionView->showAsMode(data.strObjectGUID, WizDocumentTransitionView::ErrorOccured);
        return;
    }

    if (m_editorMode == modeEditor)
        return;


    bool onEditRequest = m_editStatus & DOCUMENT_STATUS_ON_EDITREQUEST;
    if (onEditRequest)
    {
        if (m_editStatus & DOCUMENT_STATUS_ON_CHECKLIST)
        {
            onEditRequest = false;
        }
    }
    //
    bool forceEdit = onEditRequest ? true : false;

    viewNote(m_note, forceEdit);
}

void WizDocumentView::on_document_data_modified(const WIZDOCUMENTDATA& data)
{
    //verify m_noteLoaded before reload
    if (note().strGUID != data.strGUID || !m_noteLoaded)
        return;

    reloadNote();

    WizMainWindow::instance()->quickSyncKb(data.strKbGUID);
}



void WizDocumentView::on_document_data_changed(const QString& strGUID,
                                              WizDocumentView* viewer)
{
    if (viewer == this || strGUID != note().strGUID || m_editorMode != modeReader || m_accessClosing) return;
    const QPointer<WizDocumentView> self(this);
    const QPointer<WizDocumentWebView> web(m_web);
    const auto read=web->noteReadPermit();
    if (!read.current()) return;
    const auto opening=m_openSequence;
    // A reader can still contain an unsaved checklist change. Never save or
    // replace it just because another window published a newer disk version.
    web->isModified([self,web,read,opening](bool modified) {
        if (!self || !web || modified || !read.current() || self->m_accessClosing
            || self->m_web != web || self->m_openSequence != opening || self->m_editorMode != modeReader) return;
        self->reloadNote();
    });
}

void WizDocumentView::on_document_deleted(const WIZDOCUMENTDATA& data)
{
    if (note().strGUID != data.strGUID || !m_noteLoaded)
        return;

    RequestClose();
}


void WizDocumentView::on_documentEditingByOthers(QString strGUID, QStringList editors)
{
    //
    //QString strCurrentUser = m_dbMgr.db(m_note.strKbGUID).GetUserAlias();
    //editors.removeAll(strCurrentUser);

    if (strGUID == m_note.strGUID)
    {
        if (!editors.isEmpty())
        {
            QString strEditor = editors.join(" , ");
            if (!strEditor.isEmpty())
            {
                WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
                QString strUserAlias = db.getUserAlias();
                if (editors.count() == 1 && editors.first() == strUserAlias)
                {
                    qDebug() << "[EditStatus]:editing by myself.";
                    m_title->hideMessageTips(true);
                    m_title->setEditButtonEnabled(true);
                    m_editStatus = m_editStatus & ~DOCUMENT_STATUS_EDITBYOTHERS;
                }
                else
                {
                    m_title->showMessageTips(Qt::PlainText, QString(tr("%1 is currently editing this note. Note has been locked.")).arg(strEditor));
                    m_editStatus = m_editStatus | DOCUMENT_STATUS_EDITBYOTHERS;
                    if (m_note.nVersion == -1)
                    {
                        m_title->showMessageTips(Qt::PlainText, QString(tr("%1 is currently editing this note.")).arg(strEditor));
                        m_title->setEditButtonEnabled(true);
                    }
                    else
                    {
                        m_title->setEditButtonEnabled(false);
                    }
                }
            }
        }
        else
        {
            m_title->setEditButtonEnabled(!m_bLocked);
            m_editStatus = m_editStatus & ~DOCUMENT_STATUS_EDITBYOTHERS;
        }
    }
}

void WizDocumentView::on_checkEditStatus_timeout(const QString& strGUID)
{
    if (strGUID == m_note.strGUID)
    {
        if (m_editStatus & DOCUMENT_STATUS_ON_EDITREQUEST)
        {
            m_title->showMessageTips(Qt::RichText, tr("The current network in poor condition, you are <b> offline editing mode </b>."));
        }
        m_title->setEditButtonEnabled(true);
        m_editStatus = m_editStatus | DOCUMENT_STATUS_OFFLINE;
        m_bLocked = false;
        stopCheckDocumentAnimations();
    }
}

void WizDocumentView::on_checkDocumentChanged_finished(const QString& strGUID, bool changed)
{
    if (strGUID == m_note.strGUID)
    {       
        if (changed)
        {
            m_title->showMessageTips(Qt::RichText, QString(tr("New version on server avalible. <a href='%1'>Click to download new version.<a>")).arg(NOTIFYBAR_LABELLINK_DOWNLOAD));
            m_editStatus |= DOCUMENT_STATUS_NEWVERSIONFOUNDED;
        }
        else
        {
            m_bLocked = false;
            int nLockReason = -1;

            const WIZDOCUMENTDATA& doc = note();
            if (!m_dbMgr.db(doc.strKbGUID).canEditDocument(doc)) {
                nLockReason = WizNotifyBar::PermissionLack;
                m_bLocked = true;
            } else if (WizDatabase::isInDeletedItems(doc.strLocation)) {
                nLockReason = WizNotifyBar::Deleted;
                m_bLocked = true;
            }

            if (m_bLocked)
            {
                bool bGroup = m_dbMgr.db(doc.strKbGUID).isGroup();
                m_title->setLocked(m_bLocked, nLockReason, bGroup);
            }
            //
            setEditorMode(modeReader);

            m_editStatus &= ~DOCUMENT_STATUS_NEWVERSIONFOUNDED;
        }
        m_editStatus = m_editStatus & ~DOCUMENT_STATUS_FIRSTTIMEVIEW;
    }
}

void WizDocumentView::on_syncDatabase_request(const QString& strKbGUID)
{
    WizMainWindow* mainWindow = qobject_cast<WizMainWindow *>(m_app.mainWindow());
    mainWindow->quickSyncKb(strKbGUID);
}

void WizDocumentView::on_webView_focus_changed()
{
    if (m_web->hasFocus())
    {
        sendDocumentEditingStatus();
    }
}

void WizDocumentView::on_notifyBar_link_clicked(const QString& link)
{
    if (link == NOTIFYBAR_LABELLINK_DOWNLOAD)
    {
        WizDatabase& db = m_dbMgr.db(m_note.strKbGUID);
        QString strDocumentFileName = db.getDocumentFileName(m_note.strGUID);
        WizDeleteFile(strDocumentFileName);

        downloadNoteFromServer(m_note);
    }
}

void WizDocumentView::on_viewNoteInExternalEditor_request(QString& Name, QString& ProgramFile,
                                                          QString& Arguments, int TextEditor, int UTF8Encoding)
{
    WizExternalEditorData editorData = {
        Name, ProgramFile, Arguments, TextEditor, UTF8Encoding
    };

    emit viewNoteInExternalEditorRequest(editorData, m_note);
}

void WizDocumentView::handleDiscardChangesRequest()
{
    // Change editor state. WizDocumentView::setEditorMode() involves 
    // the process of saving, so we change m_editorMode separately.
    m_editorMode = modeReader;

    m_editStatus = DOCUMENT_STATUS_NOSTATUS;
    m_title->setEditorMode(modeReader);

    if (m_editorMode == modeReader)
    {
        showInfoBar();
        m_editorBar->switchToNormalMode();
    }
    else
    {
        m_editorBar->switchToNormalMode();
        showEditorBar();
    }

    // Discard changes
    m_web->discardChanges();

    // Notify group status
    bool isGroupNote = m_dbMgr.db(m_note.strKbGUID).isGroup();
    if (isGroupNote) {
        stopDocumentEditingStatus();
        startCheckDocumentEditStatus();
    }

    emit m_web->canEditNoteChanged();
}

void WizDocumentView::on_loadComment_request(const QString& url)
{
    m_comments->load(url);
}

void WizDocumentView::on_commentWidget_statusChanged()
{    
    if (m_web->isInSeperateWindow())
    {
        int commentWidth = 271;
        int maxWidth = maximumWidth();
        if (!WizIsHighPixel())
        {
            if (QGuiApplication::primaryScreen()->availableGeometry().width() < 1440)
            {
                maxWidth = 916;
            }
            maxWidth = m_commentWidget->isVisible() ? (maxWidth + commentWidth) : (maxWidth - commentWidth);
        }
        if (width() > 1000)
        {
            m_commentWidget->setFixedWidth(271);
        }
        else
        {
            m_commentWidget->setMinimumWidth(0);
            m_commentWidget->setMaximumWidth(500);
        }
        setMaximumWidth(maxWidth);
        setSizeHint(QSize(maxWidth, 1));

    }

    m_editorBar->adjustButtonPosition();
}

void WizDocumentView::shareNoteByEmail()
{
    const auto read = m_web->noteReadPermit();
    if (!read.current()) return;
    const QPointer<WizDocumentView> self(this);
    const WIZDOCUMENTDATA doc = note();
    getMailSender([self,read,doc](QString sendTo){
        if (!self || !read.current()) return;
        const QPointer<WizDocumentWebView> web(self->m_web);
        WizEmailShareDialog dlg(self->m_app);
        dlg.setNote(doc, sendTo, read);
        connect(&dlg, &WizEmailShareDialog::insertCommentToNoteRequest,
                web.data(), [web,read,doc](const QString& guid,const QString& comment) {
            if (web && read.current() && guid == doc.strGUID) web->on_insertCommentToNote_request(guid,comment);
        });
        if (read.current()) dlg.exec();
    });
}

void WizDocumentView::getMailSender(std::function<void(QString)> callback)
{
    const auto read = m_web->noteReadPermit();
    if (!read.current() || !callback) return;
    QString scriptFileName = Utils::WizPathResolve::resourcesPath() + "files/scripts/GetMailSender.js";
    QString code;
    ::WizLoadUnicodeTextFromFile(scriptFileName, code);

    const QString title = note().strTitle;
    WizDocumentUiQuery::string(this, m_web->page(), m_web->documentCallbackTicket(), code,
        [read, title, callback](const QString& sender){
        if (!read.current()) return;
        QString mailSender = sender;

        if (mailSender.isEmpty())
        {
            QRegExp rxlen("\\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\\.[A-Z]{2,4}\\b");
            rxlen.setCaseSensitivity(Qt::CaseInsensitive);
            rxlen.setPatternSyntax(QRegExp::RegExp);
            int pos = rxlen.indexIn(title);
            if (pos > -1) {
                mailSender = rxlen.cap(0);
            }
        }

        callback(mailSender);
    }, true);
}

void WizDocumentView::shareNoteByLink()
{
    auto &doc = note();
    emit shareDocumentByLinkRequest(doc.strKbGUID, doc.strGUID);
}


void WizDocumentView::onEditorFocusIn()
{
    showEditorBar();
}

void WizDocumentView::onEditorFocusOut()
{
    showEditorBar();
    if (!m_editorBar->hasFocus())
        showInfoBar();
}

void WizDocumentView::onTitleReturnPressed()
{
    setEditorFocus();
}

void WizDocumentView::onTitleEditingFinished(const QString &newTitle)
{
    WIZDOCUMENTDATA data;
    WizDatabase& db = WizDatabaseManager::instance()->db(note().strKbGUID);
    if (db.documentFromGuid(note().strGUID, data)) {
        if (!db.canEditDocument(data))
            return;

        if (newTitle != data.strTitle) {
            data.strTitle = newTitle;
            data.tDataModified = WizGetCurrentTime();
            db.modifyDocumentInfo(data);

            m_web->onTitleEdited(newTitle);
        }
    }
}

void WizDocumentView::showInfoBar()
{
    m_editorBar->hide();
    m_infoBar->show();
}

void WizDocumentView::showEditorBar()
{
    m_infoBar->hide();
    m_editorBar->show();
    m_editorBar->adjustButtonPosition();
}
