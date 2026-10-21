#include "WizDocumentWebView.h"
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
#include "widgets/WizMarkdownExportRecoveryApp.h"
#include "widgets/WizMarkdownExportRecoverySession.h"
#endif

#include <QRunnable>
#include <QList>
#include <QMimeData>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QRegExp>
#include <QAction>
#include <QMenu>
#include <QContextMenuEvent>
#include <QPrinter>
#include <QFileDialog>
#include <QTextEdit>
#include <QMultiMap>
#include <QPrintDialog>
#include <QPrinterInfo>
#include <QMessageBox>
#include <QProcess>
#include <QFileSystemWatcher>
#include <QCommandLineParser>
#include <QTimer>
#include <QTextDocument>
#include <QPointer>
#include <QTemporaryDir>
#include "WizNotePresentation.h"

#include <QApplication>
#include <QUndoStack>
#include <QDesktopServices>
#include <QNetworkDiskCache>
#include <QWebEngineProfile>

#include "share/WizGlobal.h"
#include "share/WizMisc.h"
#include "share/WizAnalyzer.h"
#include "share/WizMessageBox.h"
#include "share/WizObjectDataDownloader.h"
#include "share/WizThreads.h"
#include "share/WizPdfExport.h"
#include "share/WizPdfSvgLinks.h"
#include "share/WizDocumentPageReady.h"
#include "share/WizMarkdownExportTransaction.h"
#include "share/WizDocumentReadOutput.h"
#include "share/WizDocumentReadSave.h"

#include "WizDef.h"

#include "utils/WizPathResolve.h"
#include "utils/WizLogger.h"
#include "utils/WizMisc.h"
#include "utils/WizStyleHelper.h"

#include "database/WizDatabaseManager.h"

#include "sync/WizAvatarHost.h"
#include "sync/WizToken.h"
#include "sync/WizApiEntry.h"
#include "core/WizAccountManager.h"
#include "core/WizNoteManager.h"

#include "widgets/WizCodeEditorDialog.h"
#include "widgets/WizScreenShotWidget.h"
#include "widgets/WizEmailShareDialog.h"
#include "widgets/WizShareLinkDialog.h"
#include "widgets/WizScrollBar.h"
#include "widgets/WizExecutingActionDialog.h"

#include "mac/WizMacHelper.h"

#include "WizMainWindow.h"
#include "WizDocumentTransitionView.h"
#include "WizFileImporter.h"

#include "html/WizHtmlReader.h"
#include "api/ApiWizHtmlEditorApp.h"
#include "jsplugin/JSPluginManager.h"

#include "WizTitleBar.h"
#include "WizDocumentView.h"
#include "WizSearchReplaceWidget.h"
#include "WizEditorInsertLinkForm.h"
#include "WizEditorInsertTableForm.h"
#include "WizSvgEditorDialog.h"

enum WizLinkType {
    WizLink_Doucment,
    WizLink_Attachment
};

WizDocumentWebViewPage::WizDocumentWebViewPage(WizDocumentWebView* parent)
    : WizWebEnginePage(parent)
    , m_engineView(parent)
{
    Q_ASSERT(m_engineView);

    action(QWebEnginePage::Undo)->setShortcut(QKeySequence());
    action(QWebEnginePage::Redo)->setShortcut(QKeySequence());
    action(QWebEnginePage::Copy)->setShortcut(QKeySequence());
    action(QWebEnginePage::Cut)->setShortcut(QKeySequence());
    action(QWebEnginePage::Paste)->setShortcut(QKeySequence());
    action(QWebEnginePage::SelectAll)->setShortcut(QKeySequence());
}

bool WizDocumentWebViewPage::acceptNavigationRequest(const QUrl &url, QWebEnginePage::NavigationType type, bool isMainFrame)
{
    if (NavigationTypeBackForward == type || NavigationTypeReload == type)
        return false;
    //
    return WizWebEnginePage::acceptNavigationRequest(url, type, isMainFrame);
}

void WizDocumentWebViewPage::triggerAction(QWebEnginePage::WebAction typeAction, bool checked)
{
    switch (typeAction) {
    case QWebEnginePage::Copy:
    case QWebEnginePage::Cut:
    case QWebEnginePage::CopyLinkToClipboard:
    case QWebEnginePage::CopyImageToClipboard:
    case QWebEnginePage::CopyImageUrlToClipboard:
    case QWebEnginePage::CopyMediaUrlToClipboard:
    case QWebEnginePage::DownloadLinkToDisk:
    case QWebEnginePage::DownloadImageToDisk:
    case QWebEnginePage::DownloadMediaToDisk:
    case QWebEnginePage::SavePage:
    case QWebEnginePage::ViewSource:
    case QWebEnginePage::InspectElement:
    case QWebEnginePage::Paste:
    case QWebEnginePage::Undo:
    case QWebEnginePage::Redo:
        if (!m_engineView->noteReadPermit().current()) return;
        break;
    default: break;
    }
    if (typeAction == QWebEnginePage::Back || typeAction == QWebEnginePage::Forward) {
        return;
    }

    if (typeAction == QWebEnginePage::Paste) {
        if (m_engineView->onPasteCommand())
            return;
    } else if (typeAction == QWebEnginePage::Undo || typeAction == QWebEnginePage::Redo) {
        //FIXME: cannot forbid webpage short cutter key after QT5.4.2, webpage short cutter will override menubar short cutter
        Q_EMIT actionTriggered(typeAction);
        return;
    }

    if (typeAction == QWebEnginePage::Copy) {
        const auto read = m_engineView->noteReadPermit();
        const QPointer<WizDocumentWebViewPage> self(this);
        if (!read.current()) return;
        // The generic wrapper schedules an unscoped fix. Use Qt's copy once,
        // then schedule the same encoding fix with this note's current permit.
        QWebEnginePage::triggerAction(typeAction, checked);
        if (!self || !read.current()) return;
#ifdef Q_OS_MAC
        WizWebEnginePage::processCopiedData([read]{return read.current();});
#endif
    } else {
        WizWebEnginePage::triggerAction(typeAction, checked);
    }

    Q_EMIT actionTriggered(typeAction);
}

void WizDocumentWebViewPage::javaScriptConsoleMessage(QWebEnginePage::JavaScriptConsoleMessageLevel level, const QString& message, int lineNumber, const QString& sourceID)
{
    Q_UNUSED(sourceID);

    qDebug() << "[Console]line: " << lineNumber << ", " << message;
}


static int nWindowIDCounter = 0;

WizDocumentWebView::WizDocumentWebView(WizExplorerApp& app, QWidget* parent)
    : AbstractDocumentEditor(parent)
    , m_app(app)
    , m_dbMgr(app.databaseManager())
    , m_currentEditorMode(modeReader)
    , m_bNewNote(false)
    , m_bNewNoteTitleInited(false)
    , m_bContentsChanged(false)
    , m_bInSeperateWindow(false)
    , m_nWindowID(nWindowIDCounter ++)
    , m_searchReplaceWidget(nullptr)
    , m_ignoreActiveWindowEvent(false)
{
    WizDocumentWebViewPage* page = new WizDocumentWebViewPage(this);
    setupPage(page);
    WizPdfSvgLinks::install(page);
    connect(this, &QWebEngineView::loadStarted, this, &WizDocumentWebView::onEditorPageLoadStarted);
    connect(page, &WizDocumentWebViewPage::linkClicked,
            this, &WizDocumentWebView::onEditorLinkClicked);

    page->profile()->setSpellCheckEnabled(m_app.userSettings().isEnableSpellCheck());
    page->profile()->setSpellCheckLanguages({"en-US"});

    // minimum page size hint
    //setMinimumSize(400, 250);

    // only accept focus by mouse click as the best way to trigger toolbar reset
    setFocusPolicy(Qt::ClickFocus);
    setAttribute(Qt::WA_AcceptTouchEvents, false);

    // FIXME: should accept drop picture, attachment, link etc.
    setAcceptDrops(true);

    m_htmlEditorApp = new ApiWizHtmlEditorApp(this, this);
    connect(this, &WizDocumentWebView::isPersonalDocumentChanged,
        m_htmlEditorApp, &ApiWizHtmlEditorApp::isPersonalDocumentChanged);
    connect(this, &WizDocumentWebView::hasEditPermissionOnCurrentNoteChanged,
        m_htmlEditorApp, &ApiWizHtmlEditorApp::hasEditPermissionOnCurrentNoteChanged);
    connect(this, &WizDocumentWebView::canEditNoteChanged,
        m_htmlEditorApp, &ApiWizHtmlEditorApp::canEditNoteChanged);
    connect(this, &WizDocumentWebView::currentHtmlChanged,
        m_htmlEditorApp, &ApiWizHtmlEditorApp::currentHtmlChanged);
    connect(this, &WizDocumentWebView::clickingTodoCallBack,
        m_htmlEditorApp, &ApiWizHtmlEditorApp::clickingTodoCallBack);


    // refers
    qRegisterMetaType<WizEditorMode>("WizEditorMode");

    // loading and saving thread
    m_timerAutoSave.setInterval(1*60*1000); // 1 minutes
    connect(&m_timerAutoSave, SIGNAL(timeout()), SLOT(onTimerAutoSaveTimout()));

    WizMainWindow* mainWindow = qobject_cast<WizMainWindow*>(m_app.mainWindow());
    addObjectToJavaScriptClient("WizExplorerApp", mainWindow->publicAPIsObject());
    addObjectToJavaScriptClient("WizQtEditor", m_htmlEditorApp);

    // A Bool is only a wakeup hint; prove the current renderer document first.
    connect(this, &WizDocumentWebView::loadFinishedEx, this, &WizDocumentWebView::onEditorPageLoadHint);
    connect(view()->titleBar(), SIGNAL(onViewMindMap(bool)), SLOT(onViewMindMap(bool)));

    // Qt may recreate the native ancestor when it first binds the Quick widget.
    // Do that with an empty page, before this new view is shown or can hold a
    // protected-note grant. Otherwise the first real load can hide its host and
    // correctly revoke the very authorization that dispatched that load. This
    // constructor-only initialization never navigates/replaces an existing DOM.
    this->page()->runJavaScript(QStringLiteral("void 0"));
}

WizDocumentWebView::~WizDocumentWebView()
{
    cancelMarkdownRecovery();
    cancelLinkInsert();
    cancelFindReplace();
    cancelScreenShot(false);
    cancelPdfExport();
    m_documentCallbacks.cancel();
}

void WizDocumentWebView::setupWebActions()
{
    WizWebEngineView::setupWebActions();

    // Add "Save Note" action.
    QAction* action = new QAction(tr("Save Note"), this);
    action->setShortcut(QKeySequence::Save);
    connect(action, &QAction::triggered, this, &WizDocumentWebView::onActionSaveTriggered);
    addAction(action);

    // reload
    connect(pageAction(QWebEnginePage::Reload), &QAction::triggered, 
            this, &WizDocumentWebView::handleReloadTriggered, Qt::UniqueConnection);

    // Open temp file location
    connect(viewAction(OpenTempFileLocation), &QAction::triggered, this, [this]() {
        const QPointer<WizDocumentWebView> self(this);
        const auto read = noteReadPermit();
        const QPointer<QWebEnginePage> source(page());
        if (!read.current() || !source) return;
        const QUrl url = source->url();
        if (!self || !read.current() || self->page() != source || !url.isLocalFile()) return;
        QDesktopServices::openUrl(url.adjusted(QUrl::RemoveFilename));
    });
}

#if QT_VERSION >= QT_VERSION_CHECK(5, 11, 0)
void WizDocumentWebView::openDevTools()
{
    const auto read = noteReadPermit();
    openDevToolsWithPermit([read] { return read.current(); });
}
#endif

WizDocumentWebViewPage* WizDocumentWebView::getPage() {
    return qobject_cast<WizDocumentWebViewPage*>(page());
}

bool WizDocumentWebView::onPasteCommand()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    // A revoked paste is consumed, not passed to Chromium's default paste.
    const auto aborted = [self, read] { return !self || !read.current(); };
    if (aborted()) return true;
    QClipboard* clip = QApplication::clipboard();
    if (aborted()) return true;
    if (!clip) return false;

    const bool editing = isEditing();
    if (aborted()) return true;
    if (editing)
    {
        const QPointer<const QMimeData> mimeData(clip->mimeData());
        if (aborted()) return true;
        if (!mimeData) return false;
        const auto formats = mimeData->formats();
        if (aborted()) return true;
        if (!mimeData) return false;
        qDebug() << "Clipboard MIME Data has formats: " << formats;
        if (aborted()) return true;
        if (!mimeData) return false;
        // On MacOS: copying files will result in "text/uri-list", "text/plain", "application/x-qt-image" formats.
        // So, we should test `hasUrls()` before `hasImage()`.
        const bool hasUrls = mimeData->hasUrls();
        if (aborted()) return true;
        if (!mimeData) return false;
        if (hasUrls) {
            int nAccepted = 0;

            QList<QUrl> urls = mimeData->urls();
            if (aborted()) return true;
            QList<QUrl>::const_iterator it;
            for (it = urls.constBegin(); it != urls.constEnd(); it++) {
                if (aborted()) return true;
                QUrl url = *it;
                if (acceptUrlAsAttachment(url))
                    nAccepted++;
                if (aborted()) return true;
            }

            return nAccepted > 0;
        }
        const bool hasImage = mimeData->hasImage();
        if (aborted()) return true;
        if (hasImage) {
            // save clipboard image to
            QString strImagePath = noteResourcesPath();
            if (aborted()) return true;
            CString strFileName = strImagePath + WizIntToStr(WizGetTickCount()) + ".png";
            const QImage image = clip->image();
            if (aborted()) return true;
            const bool saved = image.save(strFileName);
            if (aborted()) return true;
            if (!saved) {
                TOLOG("ERROR: Can't save clipboard image to file");
                return false;
            }

            QString strHtml;
            const bool converted = WizImage2Html(strFileName, strHtml, strImagePath);
            if (aborted()) return true;
            if (!converted)
                return false;

            editorCommandExecuteInsertHtml(strHtml, true);

            return true;
        }

    }

    return false;
}

void WizDocumentWebView::inputMethodEvent(QInputMethodEvent* event)
{
    // On X windows, fcitx flick while preediting, only update while webview end process.
    // maybe it's a QT-BUG?
#ifdef Q_OS_LINUX
    setUpdatesEnabled(false);
    //QWebView::inputMethodEvent(event);
    setUpdatesEnabled(true);
#else
    QWebEngineView::inputMethodEvent(event);
#endif

#ifdef Q_OS_MAC
    /*
    /// comment below code, for move cursor will cause extrem high cpu, made input stop
    //int nLength = 0;
    int nOffset = 0;
    for (int i = 0; i < event->attributes().size(); i++) {
        const QInputMethodEvent::Attribute& a = event->attributes().at(i);
        if (a.type == QInputMethodEvent::Cursor) {
            //nLength = a.length;
            nOffset = a.start;
            break;
        }
    }

    // Move cursor
    // because every time input method event triggered, old preedit text will be
    // deleted and the new one inserted, this action made cursor restore to the
    // beginning of the input context, move it as far as offset indicated after
    // default implementation should correct this issue!!!
    for (int i = 0; i < nOffset; i++) {
        page()->triggerAction(QWebPage::MoveToNextChar);
    }
    */
#endif // Q_OS_MAC
}

/**
 * @brief Save current note when action_save triggered.
 * 
 */
void WizDocumentWebView::onActionSaveTriggered()
{
    trySaveDocument(view()->note(), false, [=](const QVariant&){});
}

void WizDocumentWebView::keyPressEvent(QKeyEvent* event)
{
    // WARNING!!
    // Because of unknown bugs, QKeyEvent cannot be passed to the QWebEngineView 
    // which is child of QTabWidget. This problem also appeared in official example:
    // Qt5.11.1/Examples/Qt-5.11.1/webenginewidgets/simplebrowser

    // special cases process
    if (event->key() == Qt::Key_Escape)
    {
        // FIXME: press esc will insert space at cursor if not clear focus
        clearFocus();
        return;
    }
    else if (event->key() == Qt::Key_S
             && event->modifiers() == Qt::ControlModifier)
    {
        trySaveDocument(view()->note(), false, [=](const QVariant&){});
        return;
    }
#if QT_VERSION >= 0x050402
//    else if (event->modifiers() == Qt::ControlModifier)
//    {
//        return;
//    }
    //FIXME: cannot use global save button after QT5.4.2 
    else if (event->key() == Qt::Key_V && event->modifiers() == Qt::ControlModifier)
    {
        WizGetAnalyzer().logAction("paste");

        triggerPageAction(QWebEnginePage::Paste);
        return;
    }
#endif
#if QT_VERSION < 0x050000
    #ifdef Q_OS_MAC
    else if (event->key() == Qt::Key_Z)
    {
        //Ctrl+Shift+Z,  shortcut for redo can't catch by actions in QT4
        Qt::KeyboardModifiers keyMod = QApplication::keyboardModifiers();
        bool isSHIFT = keyMod.testFlag(Qt::ShiftModifier);
        bool isCTRL = keyMod.testFlag(Qt::ControlModifier);
        if (isCTRL && isSHIFT) {
            redo();
            return;
        } else if (isCTRL && !isSHIFT) {
            undo();
            return;
        }
    }
    #endif

#endif


#ifdef Q_OS_LINUX
    setUpdatesEnabled(false);
    //QWebView::keyPressEvent(event);
    setUpdatesEnabled(true);
#else

    QWebEngineView::keyPressEvent(event);
#endif

    if (event->key() == Qt::Key_Enter || event->key() == Qt::Key_Return) {
        tryResetTitle();
    }

    emit updateEditorToolBarRequest();
}

void WizDocumentWebView::mousePressEvent(QMouseEvent* event)
{
    QWebEngineView::mousePressEvent(event);
    emit updateEditorToolBarRequest();
}

void WizDocumentWebView::focusInEvent(QFocusEvent *event)
{
    if (m_currentEditorMode == modeEditor) {
        Q_EMIT focusIn();
    }

    QWebEngineView::focusInEvent(event);
}

void WizDocumentWebView::focusOutEvent(QFocusEvent *event)
{
    // because qt will clear focus when context menu popup, we need keep focus there.
    if (event->reason() == Qt::PopupFocusReason)
    {
        return;
    }
    else if (m_ignoreActiveWindowEvent && event->reason() == Qt::ActiveWindowFocusReason)
    {
        //NOTE:when display CWizTipsWidget will let editor lose focus, then tips on toolbar disappear
        // use ignore produced ActiveWindowFocusReason when display tips to display tips
        return;
    }

    Q_EMIT focusOut();
    QWebEngineView::focusOutEvent(event);
}

void WizDocumentWebView::hideEvent(QHideEvent *event)
{
    const QPointer<WizDocumentWebView> self(this);
    cancelFindReplace();
    if (self) WizWebEngineView::hideEvent(event);
}

/**
 * @brief handle right menu event
 * 
 * @param event 
 */
void WizDocumentWebView::contextMenuEvent(QContextMenuEvent *event)
{
    if (isEditing()) {
        // Edit mode
        showContextMenu(mapToGlobal(event->pos()));
    } else {
        // Read mode
        createReadModeContextMenu(event);
    }
}

/**
 * @brief generate right menu under read pattern
 * 
 */
void WizDocumentWebView::createReadModeContextMenu(QContextMenuEvent *event)
{
    QMenu *menu = createStandardContextMenu();
    const QList<QAction *> actions = menu->actions();

    // remove back & forward actions
    auto backAction = std::find_if(actions.cbegin(), actions.cend(), [=](QAction * ac){
        return (ac->text() == "&Back" || ac->iconText() == "Back");
    });
    if (backAction != actions.cend()) {
        menu->removeAction(*backAction);
    }
    auto forwardAction = std::find_if(actions.cbegin(), actions.cend(), [=](QAction * ac){
        return (ac->text() == "&Forward" || ac->iconText() == "Forward");
    });
    if (forwardAction != actions.cend()) {
        menu->removeAction(*forwardAction);
    }

    // handle open location of document
    if(page()->url().isLocalFile()) {
        auto inspectElement = std::find(actions.cbegin(), actions.cend(), page()->action(QWebEnginePage::InspectElement));
        QAction *before(inspectElement == actions.cend() ? viewAction(OpenDevTools) : *inspectElement);
        menu->insertAction(before, viewAction(OpenTempFileLocation));
    }

    menu->popup(event->globalPos());
}

void WizDocumentWebView::handleReloadTriggered()
{
    reloadNoteData(view()->note());
}

void WizDocumentWebView::discardChanges()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto ticket = m_documentCallbacks.ticket();
    const WIZDOCUMENTDATA doc = view()->note();
    isModified([self, ticket, doc](bool modified){
        if (!self || !self->acceptsDocumentCallback(ticket, doc)) return;
        // Close rich text editor
        self->enableEditor(false);
        self->m_currentEditorMode = modeReader;
        // Stop auto save
        self->m_timerAutoSave.stop();
        // Reload document if necessary
        if (modified)
            self->reloadNoteData(doc);
    });
}

void WizDocumentWebView::dragEnterEvent(QDragEnterEvent *event)
{
    if (!isEditing())
        return;

    int nAccepted = 0;
    if (event->mimeData()->hasUrls()) {
        QList<QUrl> li = event->mimeData()->urls();
        QList<QUrl>::const_iterator it;
        for (it = li.begin(); it != li.end(); it++) {
            QUrl url = *it;
            if (url.toString().startsWith("file:///")) {
                nAccepted++;
            }
        }

        if (nAccepted == li.size()) {
            event->acceptProposedAction();
        }
    } else if (event->mimeData()->hasFormat(WIZNOTE_MIMEFORMAT_DOCUMENTS)) {
        if (!event->mimeData()->data(WIZNOTE_MIMEFORMAT_DOCUMENTS).isEmpty()) {
            setFocus();
            event->acceptProposedAction();
        }
    }
}

void WizDocumentWebView::dragMoveEvent(QDragMoveEvent* event)
{
    if (event->mimeData()->hasFormat(WIZNOTE_MIMEFORMAT_DOCUMENTS)) {
            if (!event->mimeData()->data(WIZNOTE_MIMEFORMAT_DOCUMENTS).isEmpty()) {
                setFocus();
                event->acceptProposedAction();
            }
    }
}

void WizDocumentWebView::onActionTriggered(QWebEnginePage::WebAction act)
{
    if (!noteReadPermit().current()) return;
    // webpage will override menubar short cutter after qt5.4.2, and cannot avoid this action, some operation (undo, redo et.) need operat by editer
    // need webpage feedback operation to webview to operate
    if (act == QWebEnginePage::Paste)
    {
        tryResetTitle();
    }
    else if (QWebEnginePage::Undo == act)
    {
        //WizGetAnalyzer().LogAction("Undo");
        undo();
    }
    else if (QWebEnginePage::Redo == act)
    {
        //WizGetAnalyzer().LogAction("Redo");
        redo();
    }
}

void WizDocumentWebView::tryResetTitle()
{
    if (m_bNewNoteTitleInited)
        return;

    const QPointer<WizDocumentWebView> self(this);
    const QPointer<WizDocumentView> host(view());
    const auto read = noteReadPermit();
    if (!host || !read.current()) return;
    const auto note = host->note();
    // if note already modified, maybe title changed by use manuallly
    if (note.tCreated.secsTo(note.tModified) != 0)
        return;

    page()->toPlainText([self,host,read,note](const QString& text){
        if (!self || !host || !read.current() || self->m_bNewNoteTitleInited) return;
        const auto current = host->note();
        if (current.strGUID != note.strGUID || current.strKbGUID != note.strKbGUID
            || current.tCreated != note.tCreated || current.tModified != note.tModified
            || current.strTitle != note.strTitle) return;
        QString strTitle = WizStr2Title(text.left(128));
        if (strTitle.isEmpty())
            return;

        // Claim once before signals can reenter, switch or destroy the view.
        // Never mark a replacement document initialized after resetTitle.
        self->m_bNewNoteTitleInited = true;
        host->resetTitle(strTitle);
    });
}

bool WizDocumentWebView::acceptUrlAsAttachment(const QUrl &url)
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return false;
    //paste image file as images, add attachment for other file
    QString strFileName = url.toLocalFile();

#ifdef Q_OS_MAC
    if (wizIsYosemiteFilePath(strFileName))
    {
        strFileName = wizConvertYosemiteFilePathToNormalPath(strFileName);
    }
#endif

    if (!self || !read.current()) return false;
    QFileInfo info(strFileName);

    //FIXME: //TODO: should merged with add attachment in attachment list
    if (info.size() == 0) {
        WizMessageBox::warning(nullptr, tr("Info"),
            tr("Can not add a 0 bit size file as attachment! File name : ' %1 '").arg(strFileName));
        return false;
    } else if (info.isBundle()) {
        WizMessageBox::warning(nullptr, tr("Info"),
            tr("Can not add a bundle file as attachment! File name : ' %1 '").arg(strFileName));
        return false;
    }

    QList<QByteArray> imageFormats = QImageReader::supportedImageFormats();
    if (!self || !read.current()) return false;
    if (imageFormats.contains(info.suffix().toLower().toUtf8())) {
        QString strHtml;
        const QString resources = noteResourcesPath();
        if (!self || !read.current()) return false;
        const bool converted = WizImage2Html(strFileName, strHtml, resources);
        if (!self || !read.current()) return false;
        if (converted) {
            editorCommandExecuteInsertHtml(strHtml, true);
            return true;
        }
    }
    else {
        WIZDOCUMENTDATA doc = view()->note();
        WizDatabase& db = m_dbMgr.db(doc.strKbGUID);
        if (!self || !read.current()) return false;
        WIZDOCUMENTATTACHMENTDATA data;
        data.strKbGUID = doc.strKbGUID; // needed by under layer
        const bool added = db.addAttachment(doc, strFileName, data);
        // An already completed attachment remains in its original note.
        if (!self || !read.current()) return false;
        if (!added)
        {
            TOLOG1("[drop] add attachment failed %1", strFileName);
            return false;
        }
        addAttachmentThumbnail(strFileName, data.strGUID);
        return true;
    }
    return false;
}

void WizDocumentWebView::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();

    int nAccepted = 0;
    if (mimeData->hasUrls())
    {
        QList<QUrl> li = mimeData->urls();
        QList<QUrl>::const_iterator it;
        for (it = li.constBegin(); it != li.constEnd(); it++) {
            QUrl url = *it;
            if (acceptUrlAsAttachment(url))
                nAccepted++;
        }
    }
    else if (mimeData->hasFormat(WIZNOTE_MIMEFORMAT_DOCUMENTS))
    {
        QString strData(mimeData->data(WIZNOTE_MIMEFORMAT_DOCUMENTS));
        if (!strData.isEmpty())
        {
            QString strLinkHtml;
            QStringList doclist = strData.split(';');
            foreach (QString doc, doclist) {
                QStringList docIds = doc.split(':');
                if (docIds.count() == 2)
                {
                    WIZDOCUMENTDATA document;
                    WizDatabase& db = m_dbMgr.db(docIds.first());
                    if (db.documentFromGuid(docIds.last(), document))
                    {
                        QString strHtml, strLink;
                        WizNoteToHtmlLink(document, strHtml, strLink);
                        strLinkHtml += "<span>&nbsp;" + strHtml + "&nbsp;</span>";
                    }
                }
            }

            editorCommandExecuteInsertHtml(strLinkHtml, false);

            nAccepted ++;
        }
    }

    if (nAccepted > 0) {
        event->accept();
        trySaveDocument(view()->note(), false, [=](const QVariant&){});
    }
}

WizDocumentView* WizDocumentWebView::view() const
{
    QWidget* pParent = parentWidget();
    while(pParent) {
        if (WizDocumentView* view = dynamic_cast<WizDocumentView*>(pParent)) {
            return view;
        }

        pParent = pParent->parentWidget();
    }

    return 0;
}

// Begin extracted plaintext read-permit adapter.
WizDocumentReadPermit WizDocumentWebView::noteReadPermit() const
{
    const QPointer<WizDocumentWebView> self(const_cast<WizDocumentWebView*>(this));
    const QPointer<QObject> sourcePage(page());
    const QPointer<WizDocumentView> host(view());
    if (!host || !sourcePage) return {};
    const auto access = host->noteReadTicket();
    return {self,sourcePage,m_documentCallbacks.ticket(),[self,sourcePage,host,access] {
        return self && sourcePage && host && self->page() == sourcePage
            && self->view() == host && host->acceptsNoteRead(access);
    }};
}
// End extracted plaintext read-permit adapter.

void WizDocumentWebView::clear()
{
    const QPointer<WizDocumentWebView> self(this);
    m_editorPageCallbacks.invalidate();
    m_editorPageResume = {};
    m_editorPageRestart = {};
    m_loadPending = false;
    m_loadRequestCurrent = {};
    m_loadOriginCurrent = {};
    m_loadReadPermit = {};
    m_loadSequence.next();
    const auto opening = invalidateDocumentCallbacks();
    if (!self || !WizDocumentCallbackEpoch::current(opening)) return;
    load(QUrl("about:blank"));
}

void WizDocumentWebView::onTimerAutoSaveTimout()
{
    trySaveDocument(view()->note(), false, [=](const QVariant&){});
}

void WizDocumentWebView::onTitleEdited(QString strTitle)
{
    emit titleEdited(view(), strTitle);
    WizMainWindow* mainWindow = qobject_cast<WizMainWindow*>(m_app.mainWindow());
    mainWindow->titleChanged();
    //
    WIZDOCUMENTDATA document = view()->note();
    document.strTitle = strTitle;
    // Only sync when contents unchanged. If contents changed would sync after document saved.
    const QPointer<WizDocumentWebView> self(this);
    const auto ticket = m_documentCallbacks.ticket();
    isModified([self, ticket, document](bool modified){
        if (!self || !self->acceptsDocumentCallback(ticket, document)) return;

        if (modified)
        {
            WizMainWindow* mainWindow = qobject_cast<WizMainWindow*>(self->m_app.mainWindow());
            mainWindow->quickSyncKb(document.strKbGUID);
        }
    });
}

bool WizDocumentWebView::acceptsLoadResult(const QString& kbGUID, const QString& guid, QPointer<QObject> requester, quint64 sequence) const
{
    return m_loadPending && m_loadSequence.accepts(this, requester, sequence)
        && m_loadDocument.strKbGUID == kbGUID && m_loadDocument.strGUID == guid
        && m_loadRequestCurrent && m_loadRequestCurrent() && m_loadReadPermit.current();
}

bool WizDocumentWebView::takeLoadResult(const QString& kbGUID, const QString& guid, QPointer<QObject> requester, quint64 sequence)
{
    if (!acceptsLoadResult(kbGUID, guid, requester, sequence)) return false;
    m_loadPending = false;
    return true;
}

void WizDocumentWebView::onDocumentReady(const QString kbGUID, const QString strGUID, const QString strFileName, WizEditorMode editorMode,
                                       QPointer<QObject> requester, quint64 sequence)
{
    if (!takeLoadResult(kbGUID, strGUID, requester, sequence)) return;
    const QPointer<WizDocumentWebView> self(this);
    const auto read = m_loadReadPermit;
    const auto current = m_loadRequestCurrent;
    const QPointer<WizDocumentView> host(view());
    const QPointer<QObject> source(page());
    const auto access = host->noteReadTicket();
    const auto opening = host->noteOpenSequence();
    const auto document = m_loadDocument;
    WIZDOCUMENTDATA doc;
    if (!m_dbMgr.db(kbGUID).documentFromGuid(strGUID, doc)) return;
    if (!self || !current || !current() || !read.current()
        || !m_loadSequence.accepts(this, requester, sequence)) return;
    m_mapFile.insert(strGUID, strFileName);
    WizDocumentCallbackEpoch::Ticket navigation;
    const bool dispatched = loadDocumentInWeb(editorMode, &navigation);
    // After navigation dispatch the DOM belongs to the renderer, even when a
    // synchronous loadStarted callback hides/relocks it. Never reload that DOM.
    if (dispatched || !self || !host || !source || view() != host || page() != source
        || !m_loadSequence.accepts(this, requester, sequence)
        || host->noteOpenSequence() != opening || host->note().strGUID != document.strGUID
        || host->note().strKbGUID != document.strKbGUID
        || !WizDocumentCallbackEpoch::current(navigation)
        || WizDocumentCallbackEpoch::current(access)) return;
    // Only an authorization interruption before dispatch is retryable here.
    // Keep the surviving navigation context; unrelated navigation cannot adopt it.
    if (!bindDocumentLoadOrigin(document, navigation)) return;
    m_loadReadPermit = {};
    m_loadPending = true;
    m_loadSequence.next();
    if (host->isVisible()) resumePendingDocumentLoad();
}

bool WizDocumentWebView::bindDocumentLoadOrigin(const WIZDOCUMENTDATA& document, const WizDocumentCallbackEpoch::Ticket& navigation)
{
    const QPointer<WizDocumentWebView> self(this);
    const QPointer<WizDocumentView> host(view());
    const QPointer<QObject> source(page());
    if (!host || !source || !WizDocumentCallbackEpoch::current(navigation)) return false;
    m_loadOpening = host->noteOpenSequence();
    m_loadOriginCurrent = [self,host,source,document,navigation] {
        return self && host && source && self->view() == host && self->page() == source
            && WizDocumentCallbackEpoch::current(navigation)
            && host->note().strGUID == document.strGUID && host->note().strKbGUID == document.strKbGUID;
    };
    m_loadRequestCurrent = [self,host,origin=m_loadOriginCurrent] {
        return origin() && self->m_loadOpening == host->noteOpenSequence();
    };
    return true;
}

void WizDocumentWebView::dispatchDocumentLoad(const WIZDOCUMENTDATA& document, WizEditorMode mode)
{
    const QPointer<WizDocumentWebView> self(this);
    const QPointer<WizDocumentView> host(view());
    const QPointer<QObject> source(page());
    if (!bindDocumentLoadOrigin(document, m_documentCallbacks.ticket())) return;
    // loadNote marks the host loaded after dispatch. Bind original access now,
    // and evaluate readability when its result arrives, not during dispatch.
    const auto access = host->noteReadTicket();
    const auto current = m_loadRequestCurrent;
    m_loadReadPermit = {self,source,access,[current,host,access] {
        return current() && host->acceptsNoteRead(access);
    }};
    m_loadDocument = document;
    m_loadEditorMode = mode;
    m_loadPending = true;
    const auto sequence = m_loadSequence.next();
    Q_EMIT loadDocumentRequested(document, mode, sequence);
}

bool WizDocumentWebView::resumePendingDocumentLoad()
{
    if (!m_loadPending || !m_loadRequestCurrent || !m_loadRequestCurrent()
        || m_loadReadPermit.current() || !view()->canReadCurrentNote()) return false;
    const auto document = m_loadDocument;
    const auto mode = m_loadEditorMode;
    // Do not clear/reinitialize dirty state, invalidate saves, or reuse the
    // obsolete worker result. Give the fresh extraction a different sequence.
    dispatchDocumentLoad(document, mode);
    return true;
}

bool WizDocumentWebView::continuePendingLoadAfterFailedOpen(quint64 sequence)
{
    // Only the current failed-save continuation may keep this original note.
    // A new request, different page/note or unrelated navigation is not adopted.
    if (!sequence || !m_loadPending || m_loadSequence.current() != sequence
        || !m_loadOriginCurrent || !m_loadOriginCurrent()) return false;
    m_loadReadPermit = {};
    m_loadSequence.next(); // Retire the old worker result even before reauthorization.
    m_loadOpening = view()->noteOpenSequence();
    resumePendingDocumentLoad();
    return true;
}

void WizDocumentWebView::onDocumentSaved(const QString kbGUID, const QString strGUID, bool ok, QPointer<QObject> requester)
{
    const QPointer<WizDocumentWebView> self(this);
    const QPointer<WizDocumentView> host(view());
    if (!host) return;
    const auto &note = host->note();
    if (requester != this || note.strKbGUID != kbGUID || note.strGUID != strGUID)
        return;

    if (!ok)
    {
        TOLOG("Save document failed");
        setModified(true);
        return;
    }

    host->sendDocumentSavedSignal(strGUID, kbGUID);
    if (!self) return;

    const QPointer<WizMainWindow> mainWindow(qobject_cast<WizMainWindow*>(m_app.mainWindow()));
    if (self && mainWindow) mainWindow->quickSyncKb(kbGUID);
}

void WizDocumentWebView::viewDocument(const WIZDOCUMENTDATA& doc, WizEditorMode editorMode)
{
    const QPointer<WizDocumentWebView> self(this);
    const WIZDOCUMENTDATA document = doc;
    const auto opening = invalidateDocumentCallbacks();
    if (!self || !WizDocumentCallbackEpoch::current(opening)) return;
    // set data
    // FIXME: not very well to decide a file is new through create time
    qint64 seconds = document.tCreated.secsTo(QDateTime::currentDateTime());
    m_bNewNote = (seconds >= 0 && seconds <= 1) ? true : false;
    m_bNewNoteTitleInited = m_bNewNote ? false : true;
    //
    // Do not queue a baseline reset into the previous document's JS page.
    m_bContentsChanged = false;

    // The host already chose the mode with its permission/lock policy. Keep
    // new-note title bookkeeping above without overriding that shared decision.
    // ask extract and load
    dispatchDocumentLoad(document, editorMode);
}

void WizDocumentWebView::reloadNoteData(const WIZDOCUMENTDATA& data)
{
    Q_ASSERT(!data.strGUID.isEmpty());
    const QPointer<WizDocumentWebView> self(this);
    const WIZDOCUMENTDATA document = data;

    // reset only if user not in editing mode
    if (isEditing() && hasFocus())
        return;
    if (!self) return;

    // reload may triggered when update from server or locally reflected by modify
    const auto opening = invalidateDocumentCallbacks();
    if (!self || !WizDocumentCallbackEpoch::current(opening)) return;
    dispatchDocumentLoad(document, m_currentEditorMode);
}


void WizDocumentWebView::setNoteTitleInited(bool inited)
{
    m_bNewNoteTitleInited = inited;
}

void WizDocumentWebView::setInSeperateWindow(bool inSeperateWindow)
{
    m_bInSeperateWindow = inSeperateWindow;
}

bool WizDocumentWebView::isInSeperateWindow() const
{
    return m_bInSeperateWindow;
}

QString WizDocumentWebView::documentTitle()
{
    return view()->note().strTitle;
}

void WizDocumentWebView::replaceDefaultCss(QString& strHtml)
{
    QString strFileName = Utils::WizPathResolve::resourcesPath() + "files/wizeditor/default.css";
    QFile f(strFileName);
    if (!f.open(QIODevice::ReadOnly)) {
        qDebug() << "[Editor]Failed to get default css code";
        return;
    }

    QString strCss;
    if (!WizLoadUnicodeTextFromFile(strFileName, strCss))
        return;

    QString strFont = m_app.userSettings().defaultFontFamily();
    int nSize = m_app.userSettings().defaultFontSize();
    QString lineHeight = m_app.userSettings().editorLineHeight();
    QString paraSpacing = m_app.userSettings().editorParaSpacing();
    QString pagePadding = m_app.userSettings().editorPagePadding();
    QString backgroundColor = m_app.userSettings().editorBackgroundColor();

    strCss.replace("/*default-font-family*/", QString("font-family:%1;").arg(strFont));
    strCss.replace("/*default-font-size*/", QString("font-size:%1px;").arg(nSize));

    if (backgroundColor.isEmpty())
    {
        backgroundColor = m_bInSeperateWindow ? "#F5F5F5" : "#FFFFFF";
    }

    strCss.replace("/*default-line-height*/", QString("line-height:%1;").arg(lineHeight));
    strCss.replace("/*default-para-spacing*/", QString("margin-top:%1px; margin-bottom:%1px").arg(paraSpacing));
    strCss.replace("/*default-page-padding*/", QString("padding-left:%1px; padding-right:%1px;").arg(pagePadding));
    strCss.replace("/*default-background-color*/", QString("background-color:%1;").arg(backgroundColor));

    const QString customCssId("wiz_custom_css");

    WizHtmlRemoveStyle(strHtml, customCssId);
    WizHtmlInsertStyle(strHtml, customCssId, strCss);
}

void WizDocumentWebView::editorResetFont()
{
    WIZDOCUMENTDATA data = view()->note();
    const QPointer<WizDocumentWebView> self(this);
    const auto ticket = m_documentCallbacks.ticket();
    trySaveDocument(data, false, [self, ticket, data](const QVariant& vRet){
        if (self && self->acceptsDocumentCallback(ticket, data) && vRet.toBool())
            self->reloadNoteData(data);
    });
}

void WizDocumentWebView::editorResetSpellCheck()
{
    QWebEngineProfile *profile = page()->profile();
    const bool enabled = m_app.userSettings().isEnableSpellCheck();
    profile->setSpellCheckEnabled(enabled);
    profile->setSpellCheckLanguages({"en-US"});
    page()->runJavaScript(QStringLiteral("window.WizSpellCheckEnabled=%1;"
        "if(typeof WizSetSpellCheck==='function') WizSetSpellCheck(window.WizSpellCheckEnabled);")
        .arg(enabled ? "true" : "false"));
}


void WizDocumentWebView::editorFocus()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return;
    // Focus may arrive while the blank/loading page has no editor yet.
    page()->runJavaScript("if(window.WizNativeEditorInitialized && window.WizEditor) WizEditor.focus();");
    if (self && read.current()) emit self->focusIn();
}

/**
 * @brief open native rich text edit
 * @param enalbe
 */
void WizDocumentWebView::enableEditor(bool enalbe)
{
    if (enalbe)
    {
        page()->runJavaScript("if(window.WizNativeEditorInitialized && window.WizEditor) "
            "{WizPrepareNoteModeChange();WizEditor.on(null, function(){WizApplyNoteStyle();WizApplySpellCheck();});}");
        setFocus();
    }
    else
    {
        QString code = QString("if(window.WizNativeEditorInitialized && window.WizEditor) "
            "{WizPrepareNoteModeChange();WizEditor.off({reader: {type:'%1'}}, function(){WizApplyNoteStyle();WizApplySpellCheck();});}").arg(getNoteType());
        //
        page()->runJavaScript(code);
        //
        clearFocus();
    }
}

void WizDocumentWebView::setIgnoreActiveWindowEvent(bool igoreEvent)
{
    m_ignoreActiveWindowEvent = igoreEvent;
}

bool WizDocumentWebView::evaluateJavaScript(const QString& js)
{
    page()->runJavaScript(js);
    return true;
}


void WizDocumentWebView::on_insertCommentToNote_request(const QString& docGUID, const QString& comment)
{
    if (docGUID != view()->note().strGUID)
        return;

    if (!isEditing() && WizIsMarkdownNote(view()->note()))
    {
        WizMessageBox::information(this, tr("Info"), tr("Do not support insert comment into markdown note."));
        return;
    }
    //
    // beblow should optimize, for template style note, insert position should not be the front
    if (isEditing())
    {
        QString htmlBody = "<div>" + comment + "</div><hr>";
        htmlBody.replace("\n", "<br>");
        htmlBody.replace("'", "&#39;");
        //
        QString code = QString("var objDiv = document.createElement('div');objDiv.innerHTML= unescape('%1');"
                               "var first=document.body.firstChild;document.body.insertBefore(objDiv,first);").arg(htmlBody);
        //
        page()->runJavaScript(code);
        //
        saveEditingViewDocument(view()->note(), true, [=](const QVariant &){});

    }
    else
    {
        QString commentsText = QUrl::fromPercentEncoding(comment.toUtf8());
        QString commentsHtml = ::WizText2Html(commentsText);
        //
        QString documentGuid = docGUID;
        QString kbGuid = view()->note().strKbGUID;
        //
        ::WizExecutingActionDialog::executeAction(QObject::tr("Saving comments..."), WIZ_THREAD_DEFAULT, [=]{
            //
            WizDatabase& db = m_dbMgr.db(kbGuid);
            //
            WIZDOCUMENTDATA doc;
            if (!db.documentFromGuid(documentGuid, doc))
                return;
            //
            QString strTempPath = ::Utils::WizPathResolve::tempPath() + WizGenGUIDLowerCaseLetterOnly() + "/";
            ::WizEnsurePathExists(strTempPath);
            //
            if (!db.documentToHtmlFile(doc, strTempPath))
                return;
            //
            QString htmlFileName = strTempPath + "index.html";
            if (!QFileInfo::exists(htmlFileName))
                return;
            //
            QString html;
            if (!WizLoadUnicodeTextFromFile(htmlFileName, html))
                return;
            //
            WizHtmlInsertHtmlBeforeAllBodyChildren(html, commentsHtml);
            //
            if (!::WizSaveUnicodeTextToUtf8File(htmlFileName, html, true))
                return;
            //
            db.updateDocumentDataWithFolder(doc, strTempPath, true);
        });
    }
}

void WizDocumentWebView::setWindowVisibleOnScreenShot(bool bVisible)
{
    if (bVisible) {
        const QPointer<QWidget> window(m_screenShotWindow);
        m_screenShotWindow = nullptr;
        if (window) window->show();
    } else {
        const QPointer<WizMainWindow> window(qobject_cast<WizMainWindow*>(m_app.mainWindow()));
        if (window && window->isVisible()) {
            m_screenShotWindow = window;
            window->hide();
        }
    }
}

QString WizDocumentWebView::noteResourcesPath()
{
    Q_ASSERT(!m_strNoteHtmlFileName.isEmpty());
    if (m_strNoteHtmlFileName.isEmpty())
        return QString();
    //
    QString path = Utils::WizMisc::extractFilePath(m_strNoteHtmlFileName);
    path += "index_files/";
    Utils::WizMisc::ensurePathExists(path);
    //
    return path;
}

void WizDocumentWebView::insertImage(const QString& strFileName)
{
    QString strHtml;
    if (WizImage2Html(strFileName, strHtml, noteResourcesPath())) {
        editorCommandExecuteInsertHtml(strHtml, true);
    }
}


void WizDocumentWebView::addAttachmentThumbnail(const QString strFile, const QString& strGuid)
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return;
    QImage img;
    qreal scale = 2;
    ::WizCreateThumbnailForAttachment(img, strFile, QSize(32, 32), scale);
    if (!self || !read.current()) return;
    QString strDestFile = Utils::WizPathResolve::tempPath() + WizGenGUIDLowerCaseLetterOnly() + ".png";
    if (!self || !read.current()) return;
    img.save(strDestFile, "PNG");
    if (!self || !read.current()) return;
    QString strLink = QString("wiz://open_attachment?guid=%1").arg(strGuid);
    QSize szImg = img.size();
    QString strHtml = WizGetImageHtmlLabelWithLink(strDestFile, szImg / scale, strLink);
    if (!self || !read.current()) return;
    editorCommandExecuteInsertHtml(strHtml, true);
}

QString WizDocumentWebView::getNoteType()
{
    return WizGetNoteType(view()->note());
}

bool WizDocumentWebView::isOutline() const
{
    const WIZDOCUMENTDATA& doc = view()->note();
    return doc.strType == "outline";
}

QString WizDocumentWebView::getHighlightKeywords()
{
    const WIZDOCUMENTDATAEX& doc = view()->note();
    CWizStdStringArray texts;
    texts.push_back(doc.strHighlightText);
    texts.push_back(doc.strHighlightTitle);
    //
    std::set<QString> lowerCaseKeywords;
    std::set<QString> keywords;
    for (auto it : texts)
    {
        QString text = it;
        int begin = 0;
        while (true) {
            int start = text.indexOf("<em>",  begin);
            if (start == -1)
                break;
            start += 4;
            int end = text.indexOf("</em>", start);
            if (end == -1)
                break;
            //
            QString keyword = text.mid(start, end - start);
            QString lower = keyword.toLower();
            if (lowerCaseKeywords.find(lower) == lowerCaseKeywords.end())
            {
                lowerCaseKeywords.insert(lower);
                keywords.insert("'" + keyword + "'");
            }
            //
            begin = end + 5;
        }
    }
    //
    CWizStdStringArray arr;
    arr.assign(keywords.begin(), keywords.end());
    //
    CString ret;
    ::WizStringArrayToText(arr, ret, ",");
    return ret;
}


bool WizDocumentWebView::onEditorLoadFinished(bool ok, WizDocumentCallbackEpoch::Ticket* pendingAttempt)
{
    if (pendingAttempt) *pendingAttempt = {};
    if (!ok)
        return false;
    const QPointer<WizDocumentWebView> self(this);
    const QPointer<QWebEnginePage> source(page());
    const auto read = noteReadPermit();
    if (!read.current() || !source) return false;
    m_editorSetupCallbacks.invalidate();
    m_editorSetupResume = {};
    const auto request = m_editorSetupCallbacks.ticket();
    if (pendingAttempt) *pendingAttempt = request;
    const auto mode = m_currentEditorMode;
    const auto current = [self,source,read,request,mode] {
        if (!self || !source || !WizDocumentCallbackEpoch::current(request)) return false;
        const bool allowed = read.current();
        return allowed && self && source && self->page() == source
            && self->m_currentEditorMode == mode && WizDocumentCallbackEpoch::current(request);
    };
    //
    QString resPath = Utils::WizPathResolve::resourcesPath() + "files/";
    QString editorPath = resPath + "wizeditor/";
    QString lang = "zh-cn";
    QString userGUID = m_dbMgr.db().getUserGuid();
    QString userAlias = m_dbMgr.db().getUserAlias();
    //
    const WIZDOCUMENTDATAEX doc = view()->note();
    bool ignoreTable = doc.strURL.startsWith("http");
    //
    QString noteType = getNoteType();
    if (!current()) return false;
    QString keywords = getHighlightKeywords();
    if (!current()) return false;
    // Initialize rich text editor
    const QString setupToken = WizNotePresentation::jsonString(WizGenGUIDLowerCaseLetterOnly());
    const bool spellCheck = m_app.userSettings().isEnableSpellCheck();
    if (!current()) return false;
    const QString css = WizNotePresentation::styleSheet(WizNotePresentation::loadStyle(WizGlobal::settings()));
    if (!current()) return false;
    QString strCode;
    strCode += QStringLiteral("window.WizSpellCheckEnabled=%1;\n")
        .arg(spellCheck ? "true" : "false");
    strCode += "window.WizNoteCss=" + WizNotePresentation::jsonString(css) + ";\n";
    strCode += WizDocumentPageReady::initializeEditorScript(editorPath, lang, userGUID, userAlias, ignoreTable, noteType);
    strCode += "if(window.WizNativeEditorSetup.token!==" + setupToken + ")return;\n";
    strCode += "WizPrepareNoteModeChange();\n";
    if (mode == modeEditor) {
        // Open rich text editor when doc is in edit mode
        strCode += "if(!document.body.isContentEditable||document.body.classList.contains('wiz-readonly'))"
            "await new Promise(resolve=>WizEditor.on(null,resolve));";
    } else {
        // Close rich text editor when doc is in read mode
        if (keywords.isEmpty()) {
            strCode += "await new Promise(resolve=>WizEditor.off(null,resolve));";
        } else {
            // When user open document in search results, highlit key words
            QString strCodeMarkdown = QString("await new Promise(resolve=>WizEditor.off(null,function(){WizReader.highlight.on([%1]);resolve();}));").arg(keywords);
            QString strCodeCommon = "await new Promise( (resolve, reject) => {"
                "WizEditor.off(null, () => {"
                    "resolve();"
                "});"
            "});";
            // It's hard to determine when markdown document has rendered.
            // So, keywords highlight in scroll bar is only available for 
            // common document. 
            if (noteType == "common") {
                strCode += strCodeCommon;
            } else {
                strCode += strCodeMarkdown;
            }
        }
    }
    strCode += "\nif(window.WizNativeEditorSetup.token!==" + setupToken + ")return;\nWizApplyNoteStyle();WizApplySpellCheck();";
    if (!current()) return false;
    const auto submitted = armEditorSetupReadiness(setupToken, keywords, noteType);
    if (!current() || !submitted) return false;
    source->runJavaScript(WizDocumentPageReady::setupScript(setupToken,strCode));
    submitted();
    return true;
}

std::function<void()> WizDocumentWebView::armEditorSetupReadiness(const QString& token, const QString& keywords, const QString& noteType)
{
    const QPointer<WizDocumentWebView> self(this);
    const QPointer<QWebEnginePage> source(page());
    const QPointer<WizDocumentView> host(view());
    if (!host || !source) return {};
    const auto request = m_editorSetupCallbacks.ticket();
    const auto navigation = m_documentCallbacks.ticket();
    const auto opening = host->noteOpenSequence();
    const auto sequence = m_loadSequence.current();
    const auto mode = m_currentEditorMode;
    const WIZDOCUMENTDATA document = host->note();
    const auto origin = [self,source,host,request,navigation,opening,sequence,mode,document] {
        return self && source && host && self->page() == source && self->view() == host
            && WizDocumentCallbackEpoch::current(request) && WizDocumentCallbackEpoch::current(navigation)
            && host->noteOpenSequence() == opening && self->m_loadSequence.current() == sequence
            && self->m_currentEditorMode == mode && host->note().strGUID == document.strGUID
            && host->note().strKbGUID == document.strKbGUID;
    };
    struct State {
        bool submitted=false,done=false,failed=false,findStarted=false;
        WizDocumentCallbackEpoch::Ticket access;
    };
    const auto state = std::make_shared<State>();
    m_editorSetupResume = [self,source,host,origin,state,token,keywords,noteType] {
        if (!origin() || !state->submitted || state->done || state->failed
            || WizDocumentCallbackEpoch::current(state->access)) return false;
        const auto read = self->noteReadPermit();
        if (!read.current() || !origin()) return false;
        state->access = host->noteReadTicket();
        const auto current = [origin,read] { return origin() && read.current() && origin(); };
        // Only observe the existing JS token. Never reinitialize/reload the DOM.
        WizDocumentPageReady::watch(self, source, WizDocumentPageReady::stateExpression(token), current,
            [self,current,state,keywords,noteType](bool ready) {
            if (!current() || state->done || state->failed) return;
            if (!ready) {
                state->failed = true;
                self->m_editorSetupResume = {};
                qWarning()<<"Editor setup failed or timed out";
                return;
            }
            if (!state->findStarted && !keywords.isEmpty() && noteType == "common") {
                // Mark before the reentrant find; fresh authorization must not repeat it.
                state->findStarted = true;
                const QString word = keywords.split(",").first().section("'", 0, 0, QString::SectionSkipEmpty);
                self->findText(word);
                if (!current()) return;
            }
            // Retire before plugin delivery, which can reenter or destroy the view.
            state->done = true;
            self->m_editorSetupResume = {};
            JSPluginManager::instance().handleDocumentChanged();
        });
        return true;
    };
    return [self,origin,state] {
        if (!origin()) return;
        state->submitted = true;
        // Finish the submission stack before starting a fresh permission-bound
        // observer, including synchronous hide/reauthorization during dispatch.
        QTimer::singleShot(0, self, [self,origin] {
            if (origin()) self->resumeEditorSetup();
        });
    };
}

bool WizDocumentWebView::resumeEditorSetup()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto pageResume = m_editorPageResume;
    if (pageResume && pageResume()) return true;
    if (!self) return false;
    const auto resume = m_editorSetupResume;
    return resume && resume();
}

QUrl WizDocumentWebView::armEditorPageLoad(const QUrl& url, bool retainIdentity)
{
    const QUrl expected = retainIdentity ? url : WizDocumentPageReady::navigationUrl(url);
    const QPointer<WizDocumentWebView> self(this);
    const QPointer<QWebEnginePage> source(page());
    const QPointer<WizDocumentView> host(view());
    m_editorPageCallbacks.invalidate();
    m_editorPageResume = {};
    if (!expected.isValid() || expected.isEmpty() || !source || !host) return {};
    const auto attempt = m_editorPageCallbacks.ticket();
    const auto sequence = m_loadSequence.current();
    const auto opening = host->noteOpenSequence();
    const auto mode = m_currentEditorMode;
    const WIZDOCUMENTDATA document = host->note();
    const auto owner = [self,source,host,sequence,opening,mode,document] {
        return self && source && host && self->page() == source && self->view() == host
            && self->m_loadSequence.current() == sequence
            && host->noteOpenSequence() == opening && self->m_currentEditorMode == mode
            && host->note().strKbGUID == document.strKbGUID && host->note().strGUID == document.strGUID;
    };
    const auto origin = [owner,attempt] { return owner() && WizDocumentCallbackEpoch::current(attempt); };
    // A native reload may keep its URL. A start event must still retire the
    // previous renderer proof; do not adopt another note's requested URL.
    m_editorPageRestart = [self,source,owner,expected] {
        if (!owner()) return;
        const auto requested = source->requestedUrl().adjusted(QUrl::RemoveFragment);
        if (!owner() || requested != expected) return;
        self->armEditorPageLoad(expected, true);
    };
    struct State { bool done=false,watching=false; WizDocumentCallbackEpoch::Ticket access; };
    const auto state = std::make_shared<State>();
    m_editorPageResume = [self,source,host,origin,expected,state] {
        if (!origin() || state->done) return false;
        const auto read = self->noteReadPermit();
        if (!read.current() || !origin()) return false;
        const auto url = source->url().adjusted(QUrl::RemoveFragment);
        if (!origin() || !read.current() || url != expected) return false;
        if (state->watching && WizDocumentCallbackEpoch::current(state->access)) return false;
        state->watching = true;
        state->access = host->noteReadTicket();
        const auto access = state->access;
        const auto current = [origin,read] { return origin() && read.current() && origin(); };
        WizDocumentPageReady::watch(self, source, WizDocumentPageReady::documentExpression(expected), current,
            [self,source,host,origin,current,expected,state,access](bool ready) {
            if (!current() || state->done || state->access.owner_before(access) || access.owner_before(state->access)) return;
            state->watching = false;
            if (!ready) return;
            const auto url = source->url().adjusted(QUrl::RemoveFragment);
            if (!current() || url != expected) return;
            const auto navigation = self->m_documentCallbacks.ticket();
            const auto retry = self->m_editorPageResume;
            state->done = true;
            self->m_editorPageResume = {};
            WizDocumentCallbackEpoch::Ticket preparing;
            const bool submitted = self->onEditorLoadFinished(true, &preparing);
            // Never replay submitted JS. Only a current preparation interrupted
            // by revoked access, in this unchanged renderer, can request proof again.
            if (submitted || !origin() || !WizDocumentCallbackEpoch::current(preparing)
                || !WizDocumentCallbackEpoch::current(navigation)
                || WizDocumentCallbackEpoch::current(access)) return;
            state->done = false;
            state->watching = false;
            state->access = {};
            self->m_editorPageResume = retry;
            // show/unlock may already have run in a metadata callback. Finish
            // that obsolete preparation stack before taking a fresh read permit.
            if (host->isVisible()) QTimer::singleShot(0, self, [self,origin] {
                if (origin()) self->resumeEditorSetup();
            });
        });
        return true;
    };
    return expected;
}

void WizDocumentWebView::onEditorPageLoadHint(bool ok)
{
    if (!ok) return;
    const auto resume = m_editorPageResume;
    if (resume) resume();
}

void WizDocumentWebView::onEditorPageLoadStarted()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto ticket = invalidateDocumentCallbacks();
    if (!self || !WizDocumentCallbackEpoch::current(ticket)) return;
    m_editorPageCallbacks.invalidate();
    m_editorPageResume = {};
    const auto restart = m_editorPageRestart;
    if (restart) restart();
}

/**
 * @brief handle click event on note page hyperlink
 * @param url
 * @param navigationType
 * @param isMainFrame
 * @param page
 */
void WizDocumentWebView::onEditorLinkClicked(QUrl url, QWebEnginePage::NavigationType navigationType, bool isMainFrame, WizWebEnginePage* page)
{
    const QPointer<WizDocumentWebView> self(this);
    const QPointer<WizWebEnginePage> source(page);
    const auto read = noteReadPermit();
    const auto current = [self, source, read] {
        return self && source && self->page() == source && read.current();
    };
    if (!isMainFrame || !current()) return;
    //
    //page->stopCurrentNavigation(); // this will make document page become blank, so it was commented
    //
    if (isInternalUrl(url))
    {
        QString strUrl = url.toString();
        switch (GetWizUrlType(strUrl)) {
        case WizUrl_Document:
            if (current()) viewDocumentByUrl(strUrl);
            return;
        case WizUrl_Attachment:
            if (!isEditing() && current())
            {
                const auto* host = view();
                if (!host) return;
                const auto kbGUID = host->note().strKbGUID;
                if (current()) viewAttachmentByUrl(kbGUID, strUrl);
            }
            return;
        default:
            qDebug() << QString("%1 is a wiz internal url , but we can not identify it");
            return;
        }
    }
    else
    {
        QString strUrl = url.toString();
        if (strUrl.left(12) == "http://file/")
        {
            strUrl.replace(0, 12, "file:/");
        }

        qDebug() << "Open url " << strUrl;
        if (current()) QDesktopServices::openUrl(strUrl);
        return;
    }
}

bool WizDocumentWebView::isInternalUrl(const QUrl& url)
{
    return url.scheme().toLower() == "wiz";
}

bool WizStringList2Map(const QStringList& list, QMap<QString, QString>& map)
{
    for (int i = 0; i < list.size(); i++) {
        int indx = list[i].indexOf("=");
        if (indx == -1) {
            return false;
        }

        qDebug() << "key: " << list[i].left(indx).toLower();
        qDebug() << "value: " << list[i].mid(indx + 1);

        map.insert(list[i].left(indx).toLower(), list[i].mid(indx + 1));
    }

    return true;
}

void WizDocumentWebView::viewDocumentByUrl(const QString& strUrl)
{
    if (strUrl.isEmpty())
        return;

    WizMainWindow* mainWindow = qobject_cast<WizMainWindow *>(m_app.mainWindow());
    mainWindow->viewDocumentByWizKMURL(strUrl);
}

/**
 * @brief open attachment through wiz protocol on note page
 * @param strKbGUID
 * @param strUrl
 */
void WizDocumentWebView::viewAttachmentByUrl(const QString& strKbGUID, const QString& strUrl)
{
    if (strUrl.isEmpty())
        return;

    WizMainWindow* mainWindow = qobject_cast<WizMainWindow *>(m_app.mainWindow());
    mainWindow->viewAttachmentByWizKMURL(strKbGUID, strUrl);
}

void getHtmlBodyStyle(const QString& strHtml, QString& strBodyStyle)
{
    QRegExp regh("<body ([^>]*)>", Qt::CaseInsensitive);
    if (regh.indexIn(strHtml) != -1)
    {
        strBodyStyle = regh.cap(1);
    }
}

/**
 * @brief save editting note
 * @param data note data
 * @param force weather edit force
 * @param callback callback function after save, 
 */
void WizDocumentWebView::saveEditingViewDocument(const WIZDOCUMENTDATA &data, bool force, std::function<void(const QVariant &)> callback)
{
    Q_ASSERT(!data.strGUID.isEmpty());
    if (!m_dbMgr.db(data.strKbGUID).canEditDocument(data)) {
        callback(QVariant(false));
        return;
    }
    const auto ticket = m_documentCallbacks.ticket();
    const QPointer<WizDocumentWebView> self(this);
    const WIZDOCUMENTDATA doc = data;
    const QString extractedFile = m_strNoteHtmlFileName;
    const QString sourceFile = m_mapFile.value(data.strGUID);
    const quint64 changeSequence = m_contentChangeSequence;
    if (sourceFile.isEmpty() || extractedFile.isEmpty()) { callback(false); return; }
    m_pdfPageOperations.snapshot(page(),[self,ticket,doc]{return self && self->acceptsDocumentCallback(ticket,doc);},
        WizDocumentSaveSnapshot::editorScript(force, m_bContentsChanged),
        [self, ticket, doc, extractedFile, sourceFile, changeSequence, callback](const QVariant& value) {
        if (!self || !self->acceptsDocumentCallback(ticket, doc)) { callback(false); return; }
        const auto result = WizDocumentSaveSnapshot::parse(value);
        if (result.state == WizDocumentSaveSnapshot::State::Unchanged) { callback(true); return; }
        if (result.state == WizDocumentSaveSnapshot::State::Failed
            || !WizDocumentSaveSnapshot::writeHtmlFile(extractedFile, result.html)) {
            self->reportSnapshotSaveFailure(); callback(false); return;
        }
        self->m_currentNoteHtml = result.html;
        self->m_currentNoteHtmlDocument = doc;
        if (self->m_contentChangeSequence == changeSequence) self->m_bContentsChanged = false;
        // This is the existing handoff to the FIFO saver, not a disk-save ack.
        // Its separate failure signal and the quit-time drain remain required.
        Q_EMIT self->saveDocumentRequested(doc, result.html, sourceFile, 0);
        if (self && self->acceptsDocumentCallback(ticket, doc)) Q_EMIT self->currentHtmlChanged();
        callback(true);
    });
}

/**
 * @brief save note under read pattern
 * @param data note data
 * @param force save force
 * @param callback callback after failure or sucess 

 */
void WizDocumentWebView::saveReadingViewDocument(const WIZDOCUMENTDATA &data, bool force, std::function<void(const QVariant &)> callback)
{
    Q_UNUSED(force);
    const WIZDOCUMENTDATA doc = data;

    const QPointer<WizDocumentWebView> self(this);
    const auto ticket = m_documentCallbacks.ticket();
    const QString sourceFile = m_mapFile.value(doc.strGUID);
    const quint64 changeSequence = m_contentChangeSequence;
    m_pdfPageOperations.snapshot(page(),[self,ticket,doc]{return self && self->acceptsDocumentCallback(ticket,doc);},
        WizDocumentSaveSnapshot::readerScript(m_bContentsChanged),
        [self, ticket, doc, sourceFile, changeSequence, callback](const QVariant& value) {
        if (!self || !self->acceptsDocumentCallback(ticket, doc)) { callback(false); return; }
        const auto result = WizDocumentSaveSnapshot::parse(value);
        if (result.state == WizDocumentSaveSnapshot::State::Failed
            || (result.state == WizDocumentSaveSnapshot::State::Changed && sourceFile.isEmpty())) {
            self->reportSnapshotSaveFailure(); callback(false); return;
        }
        if (result.state == WizDocumentSaveSnapshot::State::Changed) {
            if (self->m_contentChangeSequence == changeSequence) self->m_bContentsChanged = false;
            Q_EMIT self->saveDocumentRequested(doc, result.html, sourceFile, 0);
        }
        callback(true);
    });
}

bool WizDocumentWebView::acceptsDocumentCallback(const WizDocumentCallbackEpoch::Ticket& ticket, const WIZDOCUMENTDATA& data) const
{
    if (!WizDocumentCallbackEpoch::current(ticket)) return false;
    const auto* owner = view();
    return owner && owner->note().strGUID == data.strGUID && owner->note().strKbGUID == data.strKbGUID;
}

void WizDocumentWebView::reportSnapshotSaveFailure()
{
    setModified(true);
    TOLOG("save note failed: invalid page snapshot or extracted HTML write failure");
    const auto ticket = m_documentCallbacks.ticket();
    QTimer::singleShot(0, this, [this, ticket] {
        if (WizDocumentCallbackEpoch::current(ticket))
            WizMessageBox::warning(this, tr("Error"), tr("Failed to save note."));
    });
}

void WizDocumentWebView::on_insertCodeHtml_requset(QString strOldHtml)
{
    QString strHtml = strOldHtml;
    if (WizGetBodyContentFromHtml(strHtml, false))
    {
        QString name("wiz_code_highlight.css");
        QString strSrcCssFileName = Utils::WizPathResolve::resourcesPath() + "files/code/" + name;
        QString strDestCssFileName = noteResourcesPath() + name;

        if (!strDestCssFileName.isEmpty() && QFile::exists(strDestCssFileName))
        {
            QFile::remove(strDestCssFileName);
        }
        if (!QFile::copy(strSrcCssFileName, strDestCssFileName))
        {
            QMessageBox::critical(this, tr("Error"), tr("Can't copy style files"));
            return;
        }
        //
        QString link = "index_files/" + name;
        //
        page()->runJavaScript(QString("WizAddCssForCode('%1');").arg(link));
        //
        editorCommandExecuteInsertHtml(strHtml, true);
    }
}


//#define DEBUG_EDITOR

/**
 * @brief Get all scripts and style files needed for rich text editor
 * 
 * @param files 
 */
void WizDocumentWebView::getAllEditorScriptAndStyleFileName(std::map<QString, QString>& files)
{
    QString strResourcePath = Utils::WizPathResolve::resourcesPath();
    QString strHtmlEditorPath = strResourcePath + "files/wizeditor/";
    QString strWebEnginePath = strResourcePath + "files/webengine/";
    //
#ifdef DEBUG_EDITOR
    QString strEditorJS = "http://192.168.1.73:8080/libs/wizEditor/wizEditorForMac.js";
    QString strInit = QUrl::fromLocalFile(strHtmlEditorPath + "editorHelper.js").toString();
#else
    QString strEditorJS = QUrl::fromLocalFile(strHtmlEditorPath + "wizEditorForMac.js").toString();
    QString strWebChannelJS = QUrl::fromLocalFile(strWebEnginePath + "wizwebchannel.js").toString();
    QString strInit = QUrl::fromLocalFile(strHtmlEditorPath + "editorHelper.js").toString();
#endif
    //
    files.clear();
    files[strEditorJS] = "";
    files[strWebChannelJS] = "";
    files[strInit] = "";
    files[QUrl::fromLocalFile(strHtmlEditorPath + "markdownEnvironments.js").toString()] = "";
    files[QUrl::fromLocalFile(strHtmlEditorPath + "latexTheorems.js").toString()] = "";
    files[QUrl::fromLocalFile(strHtmlEditorPath + "dependency/marked/marked.umd.js").toString()] = "";
    //
    /*
     *
     * load note gradually, not need now
    QString tempCss = QUrl::fromLocalFile(strHtmlEditorPath + "tempeditorstyle.css").toString();
    QString tempCssLoadOnly = QUrl::fromLocalFile(strHtmlEditorPath + "tempeditorstyle_loadonly.css").toString();
    //
    files[tempCss] = "wiz_unsave_style";
    files[tempCssLoadOnly] = "wiz_style_for_load";
    */

}



void WizDocumentWebView::insertScriptAndStyleCore(QString& strHtml, const std::map<QString, QString>& files)
{
    Q_ASSERT(!files.empty());
    for (std::map<QString, QString>::const_iterator it = files.begin(); it != files.end(); it++)
    {
        QString url = it->first;
        QString strFileName = QUrl(url).toLocalFile();
        QString name = it->second;
        //
#ifndef DEBUG_EDITOR
        if (!strFileName.isEmpty())
        {
            Q_ASSERT(WizPathFileExists(strFileName));
        }
#endif
        //
        QString strExt = Utils::WizMisc::extractFileExt(strFileName);
        if (strExt.isEmpty())
        {
            strExt = Utils::WizMisc::extractFileExt(url);
        }
        //
        if (0 == strExt.compare(".css", Qt::CaseInsensitive))
        {
            if (name.isEmpty()) {
                name = "wiz_inner_style";
            }
            //
            QString strTag = WizFormatString2(
                        "<link rel=\"stylesheet\" type=\"text/css\" href=\"%1\" name=\"%2\" wiz_style=\"unsave\" charset=\"utf-8\">",
                        url, name);
            //
            if (strHtml.indexOf(strTag) == -1)
            {
                WizHTMLAppendTextInHead(strTag, strHtml);
            }
        }
        else if (0 == strExt.compare(".js", Qt::CaseInsensitive))
        {
            if (name.isEmpty()) {
                name = "wiz_inner_script";
            }
            QString	strTag = WizFormatString2(
                    "<script type=\"text/javascript\" src=\"%1\" name=\"%2\" wiz_style=\"unsave\" charset=\"utf-8\"></script>",
                    url, name);
            //
            if (strHtml.indexOf(strTag) == -1)
            {
                WizHTMLAppendTextInHead(strTag, strHtml);
            }
        }
        else
        {
            Q_ASSERT(false);
        }
    }
    //
    QString strTemplateJsFileName = ::Utils::WizPathResolve::wizTemplateJsFilePath();
    if (QFileInfo(strTemplateJsFileName).exists())
    {
        strTemplateJsFileName = QUrl::fromLocalFile(strTemplateJsFileName).toString();
        QString strTag = QString(
            "<script type=\"text/javascript\" src=\"%1\" wiz_style=\"unsave\" charset=\"utf-8\"></script>")
            .arg(strTemplateJsFileName);
        
        WizHTMLAppendTextInHead(strTag, strHtml);
    }
}


bool WizDocumentWebView::addDefaultScriptsToDocumentHtml(QString htmlFileName)
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return false;
    const WIZDOCUMENTDATA document = view()->note();
    QString strHtml;
    bool ret = WizLoadUnicodeTextFromFile(htmlFileName, strHtml);
    if (!self || !read.current() || !ret) return false;

    m_currentNoteHtml = strHtml;
    m_currentNoteHtmlDocument = document;
    emit currentHtmlChanged();
    if (!self || !read.current()) return false;

    std::map<QString, QString> files;
    getAllEditorScriptAndStyleFileName(files);
    if (!self || !read.current()) return false;
    insertScriptAndStyleCore(strHtml, files);
    if (!self || !read.current()) return false;

    replaceDefaultCss(strHtml);
    if (!self || !read.current()) return false;

    const bool written = WizDocumentReadOutput::writeUtf8(read, htmlFileName, strHtml, true);
    return self && read.current() && written;
}


/**
 * @brief read text and insert style table and insert rich editter script from note file, then load changed html to page
 * @param editorMode
 */
bool WizDocumentWebView::loadDocumentInWeb(WizEditorMode editorMode, WizDocumentCallbackEpoch::Ticket* navigation)
{
    if (navigation) *navigation = m_documentCallbacks.ticket();
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return false;
    const QPointer<WizDocumentView> host(view());
    const QPointer<QObject> source(page());
    const WIZDOCUMENTDATA document = host->note();
    const auto access = host->noteReadTicket();
    const auto sequence = m_loadSequence.current();
    const auto current = [self,host,source,document,access,sequence] {
        return self && host && source && self->view() == host && self->page() == source
            && self->m_loadSequence.current() == sequence && host->acceptsNoteRead(access)
            && host->note().strGUID == document.strGUID && host->note().strKbGUID == document.strKbGUID;
    };
    const auto preparing = [current,read] { return current() && read.current(); };
    QString strGUID = document.strGUID;
    QString strFileName = m_mapFile.value(strGUID);
    if (strFileName.isEmpty()) {
        return false;
    }
    //
    QString strHtml;
    bool ret = WizLoadUnicodeTextFromFile(strFileName, strHtml);
    if (!preparing() || !ret) return false;

    if (!addDefaultScriptsToDocumentHtml(strFileName) || !preparing()) return false;

    WizEditorMode oldMode = m_currentEditorMode;
    m_currentEditorMode = editorMode;
    if (oldMode != m_currentEditorMode)
    {
        if (m_currentEditorMode == modeEditor) {
            Q_EMIT focusIn();
        } else {
            Q_EMIT focusOut();
        }
        if (!preparing()) return false;
        view()->titleBar()->setEditorMode(m_currentEditorMode);
        if (!preparing()) return false;
    }

    m_strNoteHtmlFileName = strFileName;
    // Own invalidation retires transient reads. Keep the originating host
    // access ticket and load sequence, not a newly issued authorization.
    const auto opening = invalidateDocumentCallbacks();
    if (navigation) *navigation = opening;
    if (!current() || !WizDocumentCallbackEpoch::current(opening)) return false;
    const auto url = armEditorPageLoad(QUrl::fromLocalFile(strFileName));
    if (!current() || url.isEmpty()) return false;
    load(url);

    // loadStarted may legitimately retire opening; it must not change the
    // original host/access/loader context before the legacy notification.
    if (current()) onNoteLoadFinished();
    return true;
}

void WizDocumentWebView::onNoteLoadFinished()
{
    WizGlobal::instance()->emitViewNoteLoaded(view(), view()->note(), true);
}

/*!
    Set the \a mode of editor and try to save document before switching mode.
    Editor will try to save document.
*/
void WizDocumentWebView::setEditorMode(WizEditorMode mode)
{
    const quint64 requestSequence = ++m_editorModeRequestSequence;
    if (m_currentEditorMode == mode)
        return;

    cancelLinkInsert();

    bool editing = mode == modeEditor;
    cancelPdfExport();

    // show editor toolbar properly
    if (!editing && hasFocus()) {
        Q_EMIT focusOut();
    }

    if (editing && hasFocus()) {
        Q_EMIT focusIn();
    }

    WIZDOCUMENTDATA docData;
    WizDatabase& db = m_dbMgr.db(view()->note().strKbGUID);
    if (!db.documentFromGuid(view()->note().strGUID, docData))
        return;

    const QPointer<WizDocumentWebView> self(this);
    const auto ticket = m_documentCallbacks.ticket();
    const auto previousMode = m_currentEditorMode;
    trySaveDocument(docData, false, [self, ticket, docData, editing, mode, previousMode, requestSequence](const QVariant& saved){
        if (!self || !self->acceptsDocumentCallback(ticket, docData)
            || self->m_editorModeRequestSequence != requestSequence) return;
        if (!saved.toBool()) {
            // Do not switch out of an unsaved editor on a failed snapshot.
            self->view()->setEditorMode(previousMode);
            return;
        }
        self->m_editorSetupCallbacks.invalidate();
        self->m_editorSetupResume = {};
        self->m_currentEditorMode = mode;
        if (mode == modeEditor) self->m_timerAutoSave.start();
        else self->m_timerAutoSave.stop();
        self->enableEditor(editing);
        if (editing) {
            self->setFocus(Qt::MouseFocusReason);
            self->editorFocus();
        }
    });

}

/**
 * @brief save note
 * @param data note data
 * @param force save force
 * @param callback callback if fail
 */
void WizDocumentWebView::trySaveDocument(const WIZDOCUMENTDATA& data, bool force, std::function<void(const QVariant &)> callback)
{
    if (!view()->noteLoaded())  //encrypting note & has been loaded
    {
        // callback after fail
        callback(QVariant(false));
        return;
    }

    if (m_currentEditorMode == modeEditor)
    {
        // save note under editable pattern
        saveEditingViewDocument(data, force, callback);
    }
    else
    {
        // save note under read pattern
        saveReadingViewDocument(data, force, callback);
    }
}

WizDocumentSaverThread* WizDocumentWebView::readDocumentSaver() const
{
    auto* window=qobject_cast<WizMainWindow*>(m_app.mainWindow());
    return window ? window->docSaver() : nullptr;
}

// Begin extracted disk-save read adapter.
void WizDocumentWebView::trySaveDocumentForRead(const WIZDOCUMENTDATA& data, bool force,
                                             std::function<void(const QVariant&)> callback)
{
    const QPointer<WizDocumentWebView> self(this);
    const auto* host=view();
    if (!host || host->note().strGUID!=data.strGUID || host->note().strKbGUID!=data.strKbGUID) {
        if (callback) callback(false);
        return;
    }
    const auto read=noteReadPermit();
    WizDocumentReadSave::start(this,readDocumentSaver(),data,read,
        [self,data,force](std::function<void(const QVariant&)> saved) {
        if (self) self->trySaveDocument(data,force,std::move(saved));
        else saved(false);
    },[read,callback](bool ok) {
        if (callback) callback(QVariant(ok && read.current()));
    });
}
// End extracted disk-save read adapter.

void WizDocumentWebView::editorCommandQueryCommandValue(const QString& strCommand, std::function<void(const QString& value)> callback)
{
    QString script = "document.queryCommandValue('" + strCommand +"');";
    page()->runJavaScript(script, [=](const QVariant& ret){
        //
        callback(ret.toString());
        //
    });
}

void WizDocumentWebView::editorCommandQueryCommandState(const QString& strCommand, std::function<void(int state)> callback)
{
    QString script = "document.queryCommandState('" + strCommand +"');";
    page()->runJavaScript(script, [=](const QVariant& ret){
        //
        callback(ret.toInt());
        //
    });
}

/*
 * Execute command and also save status to undostack.
 * All commands execute from client which may modify document MUST invoke this
 * instead of use frame's evaluateJavascript.
 */
void WizDocumentWebView::editorCommandExecuteCommand(const QString& strCommand,
                                                      const QString& arg1 /* = QString() */,
                                                      const QString& arg2 /* = QString() */,
                                                      const QString& arg3 /* = QString() */)
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return;
    QString strExec = QString("WizEditor.execCommand('%1'").arg(strCommand);
    if (!arg1.isEmpty()) {
        strExec += ", " + arg1;
    }

    if (!arg2.isEmpty()) {
        strExec += ", " + arg2;
    }

    if (!arg3.isEmpty()) {
        strExec += ", " + arg3;
    }

    strExec += ");";

    qDebug() << strExec;

    page()->runJavaScript(strExec);
    //
    if (self && read.current()) self->setModified(true);
}


bool WizDocumentWebView::editorCommandQueryMobileFileReceiverState()
{
    return m_app.userSettings().receiveMobileFile();
}

void WizDocumentWebView::editorCommandExecuteParagraph(const QString& strType)
{
    WizGetAnalyzer().logAction("editorParagraph");
    editorCommandExecuteCommand("formatBlock", "false", "'" + strType + "'");
}

void WizDocumentWebView::editorCommandExecuteInsertHtml(const QString& strHtml, bool bNotSerialize)
{
    QString s = bNotSerialize ? "true" : "false";
    //
    QString base64Html = WizStringToBase64(strHtml);
    base64Html.replace("\r", "");
    base64Html.replace("\n", "");
    QString code = QString("WizEditor.insertB64Html('%1')").arg(base64Html);
    //
    editorExecJs(code);
    //editorCommandExecuteCommand("insertHtml", s, "'" + strHtml + "'");
}


void WizDocumentWebView::editorCommandExecutePastePlainText()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return;
    QClipboard* clip = QApplication::clipboard();
    if (!self || !read.current() || !clip)
        return;
    const QMimeData* data = clip->mimeData();
    if (!self || !read.current() || !data)
        return;
    QString text = data->text();
    if (!self || !read.current()) return;

    QString base64Text = WizStringToBase64(text);
    QString js = QString("WizEditor.pasteB64('', '%1')").arg(base64Text);
    self->editorExecJs(js);
}

void WizDocumentWebView::editorCommandExecuteIndent()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("indent");
    editorCommandExecuteCommand("indent");
}

void WizDocumentWebView::editorCommandExecuteOutdent()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("outdent");
    editorCommandExecuteCommand("outdent");
}

void WizDocumentWebView::editorCommandExecuteLinkInsert()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return;
    if (!isEnabled()) return;
    const bool editing = isEditing();
    if (!self || !read.current() || !editing || !view() || !view()->noteLoaded()) return;
    if (!self || !read.current()) return;
    if (m_editorInsertLinkForm && m_editorInsertLinkForm->isVisible() && m_linkReadPermit.current()) {
        const QPointer<WizEditorInsertLinkForm> form(m_editorInsertLinkForm);
        const auto request = m_linkUiCallbacks.ticket();
        form->raise();
        if (self && form && read.current() && WizDocumentCallbackEpoch::current(request)) form->activateWindow();
        return;
    }
    cancelLinkInsert();
    if (!self || !read.current()) return;
    const auto request = m_linkUiCallbacks.ticket();
    m_linkReadPermit = read;
    if (!m_editorInsertLinkForm) {
        m_editorInsertLinkForm = new WizEditorInsertLinkForm(this);
        connect(m_editorInsertLinkForm, &QDialog::accepted, this, &WizDocumentWebView::on_editorCommandExecuteLinkInsert_accepted);
        connect(m_editorInsertLinkForm, &QDialog::rejected, this, [this] {
            m_linkUiCallbacks.invalidate();
            m_linkReadPermit = {};
            m_linkRequest.cancel();
        });
    }
    if (!self || !read.current() || !WizDocumentCallbackEpoch::current(request)) return;
    const auto ticket = m_documentCallbacks.ticket();
    const auto doc = view()->note();
    m_linkRequest.query(this, page(), ticket, [self, ticket, doc, read, request](const QString& url) {
        if (!self || !read.current() || !WizDocumentCallbackEpoch::current(request)) return;
        if (!self->acceptsDocumentCallback(ticket, doc) || !self->isEnabled()) {
            self->m_linkRequest.cancel();
            return;
        }
        const bool editing = self->isEditing();
        if (!self || !read.current() || !WizDocumentCallbackEpoch::current(request)) return;
        if (!editing || !self->m_editorInsertLinkForm) { self->m_linkRequest.cancel(); return; }
        const QPointer<WizEditorInsertLinkForm> form(self->m_editorInsertLinkForm);
        const auto current = [self, form, read, request] {
            return self && form && read.current() && WizDocumentCallbackEpoch::current(request)
                && self->m_editorInsertLinkForm == form;
        };
        if (!current()) return;
        form->setUrl(url.isEmpty() ? "http://" : url);
        if (!current()) return;
        // Preserve the existing application-modal UI, without a nested event
        // loop inside a WebEngine callback that can outlive its document.
        form->show();
        if (!current()) return;
        form->raise();
        if (!current()) return;
        form->activateWindow();
        if (!current()) return;
        WizAnalyzer::getAnalyzer().logAction("linkInsert");
    });
}

void WizDocumentWebView::cancelLinkInsert()
{
    m_linkUiCallbacks.invalidate();
    m_linkReadPermit = {};
    m_linkRequest.cancel();
    const QPointer<WizEditorInsertLinkForm> form(m_editorInsertLinkForm);
    if (form) form->reject();
}

WizDocumentCallbackEpoch::Ticket WizDocumentWebView::invalidateDocumentCallbacks()
{
    const QPointer<WizDocumentWebView> self(this);
    m_documentCallbacks.invalidate();
    m_editorSetupResume = {};
    const auto ticket = m_documentCallbacks.ticket();
    const auto current = [self,ticket] { return self && WizDocumentCallbackEpoch::current(ticket); };
    cancelLinkInsert();
    if (!current()) return ticket;
    cancelFindReplace();
    if (!current()) return ticket;
    cancelScreenShot();
    if (!current()) return ticket;
    cancelPdfExport();
    if (!current()) return ticket;
    cancelMarkdownRecovery();
    return ticket;
}

void WizDocumentWebView::cancelMarkdownRecovery()
{
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
    const auto session = m_markdownRecoverySession;
    m_markdownRecoverySession = nullptr;
    if (session)
        session->invalidate();
#endif
}

void WizDocumentWebView::cancelPdfExport()
{
    m_pdfExportRequest.cancel();
}

void WizDocumentWebView::on_editorCommandExecuteLinkInsert_accepted()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = m_linkReadPermit;
    const auto request = m_linkUiCallbacks.ticket();
    const QPointer<WizEditorInsertLinkForm> form(m_editorInsertLinkForm);
    if (!read.current() || !form || !isEnabled()) {
        m_linkRequest.cancel();
        return;
    }
    const bool editing = isEditing();
    if (!self || !form || !read.current() || !WizDocumentCallbackEpoch::current(request)) return;
    if (!editing) { self->m_linkRequest.cancel(); return; }
    const QString url = form->getUrl();
    if (!self || !form || !read.current() || !WizDocumentCallbackEpoch::current(request)) return;
    const QString code = m_linkRequest.takeCommand(page(), url);
    m_linkUiCallbacks.invalidate();
    m_linkReadPermit = {};
    if (!code.isEmpty() && self && read.current()) self->editorExecJs(code);
}

void WizDocumentWebView::editorCommandExecuteLinkRemove()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("unlink");
    editorCommandExecuteCommand("unlink");
}

void WizDocumentWebView::editorCommandExecuteFindReplace()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return;
    m_findUiCallbacks.invalidate();
    const auto request = m_findUiCallbacks.ticket();
    if (!m_searchReplaceWidget)
        m_searchReplaceWidget = new WizSearchReplaceWidget(this);
    if (!self || !read.current() || !WizDocumentCallbackEpoch::current(request)) return;
    const QPointer<WizSearchReplaceWidget> form(m_searchReplaceWidget);
    const auto current = [self, form, read, request] {
        return self && form && read.current() && WizDocumentCallbackEpoch::current(request)
            && self->m_searchReplaceWidget == form;
    };
    const auto stopped = [self, form, current, request] {
        if (current()) return false;
        if (self && self->m_searchReplaceWidget == form && WizDocumentCallbackEpoch::current(request)) self->cancelFindReplace();
        return true;
    };
    // Posted signals keep their original permit/epoch even after disconnect.
    QObject::disconnect(form.data(), nullptr, this, nullptr);
    connect(form.data(), &WizSearchReplaceWidget::findPre, this, [self, form, current](QString text, bool flag) {
        if (current() && form->isVisible()) self->findPre(text, flag);
    });
    connect(form.data(), &WizSearchReplaceWidget::findNext, this, [self, form, current](QString text, bool flag) {
        if (current() && form->isVisible()) self->findNext(text, flag);
    });
    connect(form.data(), &WizSearchReplaceWidget::replaceCurrent, this, [self, form, current](QString from, QString to) {
        if (current() && form->isVisible()) self->replaceCurrent(from, to);
    });
    connect(form.data(), &WizSearchReplaceWidget::replaceAndFindNext, this, [self, form, current](QString from, QString to, bool flag) {
        if (current() && form->isVisible()) self->replaceAndFindNext(from, to, flag);
    });
    connect(form.data(), &WizSearchReplaceWidget::replaceAll, this, [self, form, current](QString from, QString to, bool flag) {
        if (current() && form->isVisible()) self->replaceAll(from, to, flag);
    });

    QRect rect = geometry();
    QPoint leftTop = pos();
    if (parentWidget())
        leftTop = parentWidget()->mapToGlobal(leftTop);
    rect.moveTo(leftTop);
    if (stopped()) return;
    form->showInEditor(rect, current);
    if (stopped() || !form->isVisible()) return;
    const QString source = selectedText();
    if (stopped() || !form->isVisible()) return;
    form->setSourceText(source);
    if (stopped() || !form->isVisible()) return;

    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("findReplace");

}

void WizDocumentWebView::cancelFindReplace()
{
    m_findUiCallbacks.invalidate();
    const QPointer<WizSearchReplaceWidget> form(m_searchReplaceWidget);
    if (!form) return;
    QObject::disconnect(form.data(), nullptr, this, nullptr);
    form->hide();
}

void WizDocumentWebView::innerFindText(QString text, bool next, bool matchCase)
{
    bool back = !next;
    QString code = QString("find('%1', %2, %3, true)").arg(text)
            .arg(back ? "true" : "false")
            .arg(matchCase ? "true" : "false");
    editorExecJs(code);

}

static QString strOldSearchText = "";
static bool strOldCase = false;
void WizDocumentWebView::findPre(QString strTxt, bool bCasesensitive)
{
    if (!noteReadPermit().current()) return;
    //FIXME:  there is a problem here, HighlightAllOccurrences can not be used togethor with find one.
    if (strOldSearchText != strTxt || strOldCase != bCasesensitive)
    {
        // clear highlight
        //findText("");
        strOldSearchText = strTxt;
        strOldCase = bCasesensitive;
    }
    QWebEnginePage::FindFlags options;
    options |= QWebEnginePage::FindBackward;
    if (bCasesensitive)
    {
        options |= QWebEnginePage::FindCaseSensitively;
    }
    //
    findText(strTxt, options);
    //innerFindText(strTxt, false, bCasesensitive);
}

void WizDocumentWebView::findNext(QString strTxt, bool bCasesensitive)
{
    if (!noteReadPermit().current()) return;
    if (strOldSearchText != strTxt || strOldCase != bCasesensitive)
    {
        //findText("", 0);
        strOldSearchText = strTxt;
        strOldCase = bCasesensitive;
    }
    QWebEnginePage::FindFlags options;
    if (bCasesensitive)
    {
        options |= QWebEnginePage::FindCaseSensitively;
    }
    //
    findText(strTxt, options);
    //innerFindText(strTxt, true, bCasesensitive);
}

void WizDocumentWebView::replaceCurrent(QString strSource, QString strTarget)
{
    QString strExec = QString("WizEditor.replace('%1', '%2', true)").arg(strSource).arg(strTarget);
    editorExecJs(strExec);
}

void WizDocumentWebView::replaceAndFindNext(QString strSource, QString strTarget, bool bCasesensitive)
{
    QString strExec = QString("WizEditor.replace('%1', '%2', %3)").arg(strSource).arg(strTarget).arg(bCasesensitive ? "true" : "false");
    editorExecJs(strExec);
}

void WizDocumentWebView::replaceAll(QString strSource, QString strTarget, bool bCasesensitive)
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return;
    QString strExec = QString("WizEditor.replaceAll('%1', '%2', %3)").arg(strSource).arg(strTarget).arg(bCasesensitive ? "true" : "false");
    page()->runJavaScript(strExec);
    if (self && read.current()) self->setModified(true);
}

void WizDocumentWebView::editorCommandExecuteFontFamily(const QString& strFamily)
{
    WizGetAnalyzer().logAction(QString("editorSetFontFamily : %1").arg(strFamily));
    editorCommandExecuteCommand("fontName", "false", "'" + strFamily + "'");
}

void WizDocumentWebView::editorCommandExecuteFontSize(const QString& strSize)
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return;
    WizGetAnalyzer().logAction(QString("editorSetFontSize : %1pt").arg(strSize));
    //
    CString strStyle = WizFormatString1("{\\\"font-size\\\" : \\\"%1pt\\\"}", strSize);
    CString strScript = WizFormatString1("WizEditor.modifySelectionDom(JSON.parse(\"%1\"))", strStyle);

    if (!self || !read.current()) return;
    page()->runJavaScript(strScript);
    if (self && read.current()) self->setModified(true);
}

void WizDocumentWebView::editorCommandExecuteBackColor(const QColor& color)
{
    if (color == QColor(Qt::transparent)) {
       //editorCommandExecuteCommand("backColor", "false", "'default'");
    }
    else {
        editorCommandExecuteCommand("backColor", "false", "'" + color.name() + "'");
    }
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("backColor");
}

void WizDocumentWebView::editorCommandExecuteForeColor(const QColor& color)
{
    if (color == QColor(Qt::transparent)) {
        //editorCommandExecuteCommand("foreColor", "false", "'default'");
    }
    else {
        editorCommandExecuteCommand("foreColor", "false", "'" + color.name() + "'");
    }
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("foreColor");
}

void WizDocumentWebView::editorCommandExecuteBold()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("bold");
    editorCommandExecuteCommand("bold");
}

void WizDocumentWebView::editorCommandExecuteItalic()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("italic");
    editorCommandExecuteCommand("italic");
}

void WizDocumentWebView::editorCommandExecuteUnderLine()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("underline");
    editorCommandExecuteCommand("underline");
}

void WizDocumentWebView::editorCommandExecuteStrikeThrough()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("strikethrough");
    editorCommandExecuteCommand("strikethrough");
}

void WizDocumentWebView::editorCommandExecuteSubScript()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("subscript");
    editorCommandExecuteCommand("subscript");
}

void WizDocumentWebView::editorCommandExecuteSuperScript()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("superscript");
    editorCommandExecuteCommand("superscript");
}

void WizDocumentWebView::editorCommandExecuteJustifyLeft()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("justifyLeft");
    editorCommandExecuteCommand("JustifyLeft");
}

void WizDocumentWebView::editorCommandExecuteJustifyRight()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("justifyRight");
    editorCommandExecuteCommand("JustifyRight");
}

void WizDocumentWebView::editorCommandExecuteJustifyCenter()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("justifyCenter");
    editorCommandExecuteCommand("JustifyCenter");
}

void WizDocumentWebView::editorCommandExecuteJustifyJustify()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("justifyJustify");
    editorCommandExecuteCommand("justify", "'justify'");
}

void WizDocumentWebView::editorCommandExecuteInsertOrderedList()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("insertOrderedList");
    editorCommandExecuteCommand("insertOrderedList");
}

void WizDocumentWebView::editorCommandExecuteInsertUnorderedList()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("insertUnorderedList");
    editorCommandExecuteCommand("insertUnorderedList");
}

void WizDocumentWebView::editorCommandExecuteTableInsert(int row, int col)
{
    QString code = QString("WizEditor.table.insertTable(%1, %2);").arg(col).arg(row);
    //
    editorExecJs(code);
}


void WizDocumentWebView::on_editorCommandExecuteScreenShot_imageAccepted(QPixmap pix)
{
    if (!m_screenShotHelper || sender() != m_screenShotHelper) return;
    const QPointer<WizDocumentWebView> self(this);
    const auto read = m_screenShotReadPermit;
    const bool reauth=m_screenShotRequiresReauth;
    const auto origin=m_screenShotOriginCurrent;
    // Retire before restoring the window: show() can start a newer request.
    const auto retired = cancelScreenShot();
    if(reauth){
        if(!self||pix.isNull()||!origin||!origin()||!WizDocumentCallbackEpoch::current(retired))return;
        m_pendingScreenShot=pix;m_screenShotOriginCurrent=origin;
        view()->awaitScreenShotAuthorization();
        return;
    }
    if(self)insertScreenShot(pix,read,retired);
}

void WizDocumentWebView::resumePendingScreenShot()
{
    if(m_pendingScreenShot.isNull())return;
    if(!m_screenShotOriginCurrent||!m_screenShotOriginCurrent()){cancelScreenShot();return;}
    const auto read=noteReadPermit();if(!read.current())return;
    const QPixmap pix=m_pendingScreenShot;const auto ticket=m_screenShotCallbacks.ticket();
    m_pendingScreenShot={};m_screenShotOriginCurrent={};
    insertScreenShot(pix,read,ticket);
}

void WizDocumentWebView::insertScreenShot(const QPixmap& pix,const WizDocumentReadPermit& read,const WizDocumentCallbackEpoch::Ticket& retired)
{
    const QPointer<WizDocumentWebView> self(this);
    const auto allowed = [self,read,retired] {
        return self && read.current() && WizDocumentCallbackEpoch::current(retired);
    };
    if (!allowed() || pix.isNull()) return;
    const QString resources = noteResourcesPath();
    if (!allowed() || resources.isEmpty()) return;
    const QString strFileName = resources + WizIntToStr(WizGetTickCount()) + ".png";
    if (!pix.save(strFileName)) {
        TOLOG("ERROR: Can't save clipboard image to file");
        return;
    }
    if (!allowed()) return;
    QString html;
    if (!WizImage2Html(strFileName, html, resources)) return;
    if (allowed()) self->editorCommandExecuteInsertHtml(html, true);
}

void WizDocumentWebView::on_editorCommandExecuteScreenShot_finished()
{
    if (m_screenShotHelper && sender() == m_screenShotHelper) cancelScreenShot();
}

WizDocumentCallbackEpoch::Ticket WizDocumentWebView::cancelScreenShot(bool restoreWindow)
{
    m_screenShotCallbacks.invalidate();
    const auto retired = m_screenShotCallbacks.ticket();
    m_screenShotReadPermit = {};
    m_screenShotRequiresReauth=false;m_pendingScreenShot={};m_screenShotOriginCurrent={};
    const QPointer<WizScreenShotHelper> helper(m_screenShotHelper);
    m_screenShotHelper = nullptr;
    if (helper) {
        QObject::disconnect(helper, nullptr, this, nullptr);
        // Signals originate inside the widget/helper stack. Never delete it there.
        helper->setParent(nullptr);
        helper->deleteLater();
    }
    if (restoreWindow) setWindowVisibleOnScreenShot(true);
    else m_screenShotWindow = nullptr;
    return retired;
}

void WizDocumentWebView::editorCommandExecuteInsertHorizontal()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("insertHorizontal");
    editorCommandExecuteCommand("InsertHorizontalRule");
}

void WizDocumentWebView::editorCommandExecuteInsertCheckList()
{
    QString strExec = "WizEditor.todo.setTodo();";
    editorExecJs(strExec);

    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("insertCheckList");
}

void WizDocumentWebView::editorCommandExecuteInsertImage()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto read = noteReadPermit();
    if (!read.current()) return;
    static QString initPath = QDir::homePath();
    QStringList strImgFileList = QFileDialog::getOpenFileNames(0, tr("Image File"), initPath, tr("Images (*.png *.bmp *.gif *.jpg)"));
    // The chooser's modal loop may lock, switch or close the source note.
    if (!self || !read.current() || strImgFileList.isEmpty())
        return;
    //
    QString strImagePath = noteResourcesPath();
    if (!self || !read.current()) return;

    CWizStdStringArray files;
    foreach (QString strImgFile, strImgFileList)
    {
        if (!self || !read.current()) return;
        QString destImageFileName = strImagePath + ::WizGenGUIDLowerCaseLetterOnly()
                + Utils::WizMisc::extractFileExt(strImgFile);
        //
        if (QFile::copy(strImgFile, destImageFileName))
        {
            files.push_back(QUrl::fromLocalFile(destImageFileName).toString());
        }
        if (!self || !read.current()) return;
        //
        initPath = Utils::WizMisc::extractFilePath(strImgFile);
    }
    //
    CString param;
    WizStringArrayToText(files, param, "*");
    //WizEditor.img.insertByPath('./img01.jpg');
    QString script = QString("WizEditor.img.insertByPath('%1');").arg(param);
    if (!self || !read.current()) return;
    self->editorExecJs(script);
    if (!self || !read.current()) return;

    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("insertImage");
}


void WizDocumentWebView::editorCommandExecuteStartMarkup()
{
    QString js = QString("WizEditor.marker.start();");
    editorExecJs(js);
}


void WizDocumentWebView::editorCommandExecuteStopMarkup()
{
    QString js = QString("WizEditor.marker.quit();");
    editorExecJs(js);
}


void WizDocumentWebView::editorExecJs(QString js)
{
    if (!noteReadPermit().current()) return;
    page()->runJavaScript(js);
}

void WizDocumentWebView::onViewMindMap(bool on)
{
    if (!noteReadPermit().current()) return;
    if (on) {
        QString title = view()->note().strTitle;
        QString js = QString("WizEditor.outline.showMinder(`%1`);").arg(title.replace("`", "\\`"));
        editorExecJs(js);
    } else {
        QString js = "WizEditor.outline.hideMinder();";
        editorExecJs(js);
    }
}

void WizDocumentWebView::editorCommandExecuteInsertPainter()
{
    if (!noteReadPermit().current()) return;
    view()->changeType("svgpainter");
    QString js = QString("WizEditor.createSvg()");
    editorExecJs(js);
}


void WizDocumentWebView::editorCommandExecuteInsertDate()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("insertDate");
    //
    QString date = QLocale().toString(QDate::currentDate(), QLocale::LongFormat);
    editorCommandExecuteInsertHtml(date, false);
}

void WizDocumentWebView::editorCommandExecuteInsertTime()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("insertTime");
    //
    QString time = QLocale().toString(QTime::currentTime(), QLocale::LongFormat);
    editorCommandExecuteInsertHtml(time, false);
}

void WizDocumentWebView::editorCommandExecuteRemoveFormat()
{
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("removeFormat");
    editorCommandExecuteCommand("removeFormat");
}

void WizDocumentWebView::editorCommandExecuteFormatPainterOn(bool multi)
{
    const auto read = noteReadPermit();
    if (!read.current()) return;
    QString script = QString("WizEditor.formatPainter.on(%1);").arg(multi ? "true" : "false");
    //
    page()->runJavaScript(script, [read] (const QVariant& vRet) {
        if (!read.current()) return;
        if (vRet.type() == QVariant::Bool) {
            if (!vRet.toBool()) {
                //
                TOLOG("Can't stsart format painter");

            }
        }

    });
    //
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("formatPainter");
}

void WizDocumentWebView::editorCommandExecuteFormatPainterOff()
{
    const auto read = noteReadPermit();
    if (!read.current()) return;
    QString script = QString("WizEditor.formatPainter.off();");
    //
    page()->runJavaScript(script, [read] (const QVariant& vRet) {
        if (!read.current()) return;
        if (vRet.type() == QVariant::Bool) {
            if (!vRet.toBool()) {
                //
                TOLOG("Can't stsart format painter");

            }
        }

    });
    //
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("formatPainter");
}

void WizDocumentWebView::editorCommandExecuteInsertCode()
{
    editorExecJs("WizEditor.code.insertCode();");
    //
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("insertCode");
}

void WizDocumentWebView::editorCommandExecuteMobileImage(bool bReceiveImage)
{
    WizMainWindow* mainWindow = qobject_cast<WizMainWindow *>(m_app.mainWindow());
    if (bReceiveImage && m_app.userSettings().needShowMobileFileReceiverUserGuide())
    {
        mainWindow->showMobileFileReceiverUserGuide();
    }

    m_app.userSettings().setReceiveMobileFile(bReceiveImage);
    mainWindow->setMobileFileReceiverEnable(bReceiveImage);

    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("mobileImage");
}

void WizDocumentWebView::editorCommandExecuteScreenShot()
{
    const QPointer<WizDocumentWebView> self(this);
    const auto initialRead = noteReadPermit();
    const auto document = m_documentCallbacks.ticket();
    if (!initialRead.current()) return;
    const bool legacyHide = view()->canReadLegacyHiddenNote();
    // One request per editor. Do not restore/hide the window for a duplicate click.
    if (m_screenShotHelper || !m_pendingScreenShot.isNull()) return;
    const QPointer<WizScreenShotHelper> helper(new WizScreenShotHelper());
    helper->setParent(this);
    m_screenShotHelper = helper;
    m_screenShotRequiresReauth=!legacyHide;
    const QPointer<WizDocumentView> host(view());const QPointer<QObject> source(page());
    const WIZDOCUMENTDATA note=host->note();const auto opening=host->noteOpenSequence();
    const auto identity=WizBiometricKeychain::itemIdentity(WizBiometricNoteAccess::snapshot(m_dbMgr.db(),m_dbMgr.db(note.strKbGUID),WizCommonApiEntry::server()));
    m_screenShotOriginCurrent=[self,host,source,document,note,opening,identity,legacyHide]{
        return self&&host&&source&&self->page()==source&&WizDocumentCallbackEpoch::current(document)
            &&host->noteOpenSequence()==opening&&host->note().strGUID==note.strGUID&&host->note().strKbGUID==note.strKbGUID
            &&(legacyHide||identity==WizBiometricKeychain::itemIdentity(WizBiometricNoteAccess::snapshot(self->m_dbMgr.db(),self->m_dbMgr.db(note.strKbGUID),WizCommonApiEntry::server())));
    };
    m_screenShotCallbacks.invalidate();
    const auto request = m_screenShotCallbacks.ticket();
    const auto alive = [self,helper,request] {
        return self && helper && self->m_screenShotHelper == helper
            && WizDocumentCallbackEpoch::current(request);
    };
    const auto current = [self,alive,document] {
        return alive() && WizDocumentCallbackEpoch::current(document)
            && self->m_screenShotOriginCurrent && self->m_screenShotOriginCurrent();
    };
    connect(helper, &WizScreenShotHelper::screenShotCaptured, this, [self,current,alive](QPixmap pix) {
        if (current()) self->on_editorCommandExecuteScreenShot_imageAccepted(pix);
        else if(alive())self->cancelScreenShot();
    });
    connect(helper, &WizScreenShotHelper::shotScreenQuit, this, [self,alive] {
        if (alive()) self->on_editorCommandExecuteScreenShot_finished();
    });
    connect(helper, &QObject::destroyed, this, [self,request] {
        if (self && WizDocumentCallbackEpoch::current(request) && !self->m_screenShotHelper)
            self->cancelScreenShot();
    });
    setWindowVisibleOnScreenShot(false);
    if (!current()) { if(alive())cancelScreenShot();return; }
    // The intentional hide revokes the old ticket. Legacy notes bind their
    // hidden permit now. Protected captures retain pixels only in memory;
    // returning to the original note requires fresh authorization to insert.
    const auto read = legacyHide?noteReadPermit():WizDocumentReadPermit();
    if (legacyHide&&!read.current()) { cancelScreenShot(); return; }
    m_screenShotReadPermit = read;
    QTimer::singleShot(200, helper, [self,helper,current,alive,read,legacyHide] {
        if (!current()) {if(alive())self->cancelScreenShot();return;}
        if (legacyHide&&!read.current()) { self->cancelScreenShot(); return; }
        helper->startScreenShot();
    });
    WizAnalyzer& analyzer = WizAnalyzer::getAnalyzer();
    analyzer.logAction("screenShot");
}


void WizDocumentWebView::saveAsPDF(WizPdfExport::Mode selectedMode)
{
    const auto read = noteReadPermit();
    if (!read.current()) return;
    const QPointer<WizDocumentWebView> guard(this);
    const auto request=m_pdfExportRequest.begin(this,page(),m_documentCallbacks.ticket());
    if (!WizPdfExportRequest::current(request)) return;
    CString strTitle = view()->note().strTitle;
    WizMakeValidFileNameNoPath(strTitle);
    static QString strInitPath = QDir::homePath();
    QString strInitFileName = Utils::WizMisc::addBackslash2(strInitPath) + Utils::WizMisc::extractFileTitle(strTitle);
    //
    QString strFileName = QFileDialog::getSaveFileName(this, QString(),
                                                       strInitFileName,
                                                       tr("PDF Files (*.pdf)"));
    //
    // A native modal dialog can dispatch close/navigation events. Check weak
    // context before accessing this, settings, or the replacement document.
    if (!guard || !read.current() || !WizPdfExportRequest::current(request)) {
        WizPdfExportRequest::cancel(request);
        return;
    }
    if (strFileName.isEmpty()) {
        WizPdfExportRequest::cancel(request);
        return;
    }
    //
    strInitPath = Utils::WizMisc::extractFilePath(strFileName);
    //
    const int marginUnit = m_app.userSettings().printMarginUnit();
    double marginTop = m_app.userSettings().printMarginValue(wizPositionTop);
    double marginBottom = m_app.userSettings().printMarginValue(wizPositionBottom);
    double marginLeft = m_app.userSettings().printMarginValue(wizPositionLeft);
    double marginRight = m_app.userSettings().printMarginValue(wizPositionRight);
    QMarginsF margins(marginLeft, marginTop, marginRight, marginBottom);
    //
    const QPageLayout layout = WizPdfExport::pageLayout(marginUnit, margins);
    if (!layout.isValid()) {
        WizPdfExportRequest::cancel(request);
        QMessageBox::critical(this, tr("Error"), QStringLiteral("PDF 页边距设置无效，请在设置中检查单位和数值。"));
        return;
    }
    // Keep the same renderer and CSS. Commit only complete output, preserving an
    // existing destination on cancellation, page destruction or write failure.
    const auto document=m_documentCallbacks.ticket();
    m_pdfPageOperations.printWithMode(page(),[request,read]{return read.current() && WizPdfExportRequest::current(request);},
        marginUnit,margins,selectedMode,
        [guard, request, read, strFileName](const QByteArray& bytes) {
        if (!read.current()) { WizPdfExportRequest::cancel(request); return; }
        QString error;
        const auto result=WizPdfExportRequest::complete(request,strFileName,bytes,error);
        if (result!=WizPdfExportRequest::Result::Failed) return;
        qWarning() << "PDF export failed:" << error;
        // A print callback can run while the page/view is being destroyed. Do
        // not touch either here; a queued receiver is cancelled on destruction.
        if (guard) QTimer::singleShot(0, guard.data(), [guard, request, read, error] {
            if (guard && read.current() && WizPdfExportRequest::ownsContext(request)) QMessageBox::critical(guard.data(), QObject::tr("Error"), QStringLiteral("无法导出 PDF：\n") + error);
        });
    },[guard,document]{return guard && WizDocumentCallbackEpoch::current(document);},
        QUrl::fromLocalFile(Utils::WizPathResolve::resourcesPath()+"files/wizeditor/dependency/mathjax/MathJax.js"));
}

void WizDocumentWebView::applyNoteStyle()
{
    const QString css = WizNotePresentation::styleSheet(WizNotePresentation::loadStyle(WizGlobal::settings()));
    page()->runJavaScript("if (typeof WizApplyNoteStyle==='function') WizApplyNoteStyle(" + WizNotePresentation::jsonString(css) + ");");
}
void WizDocumentWebView::editNoteStyle()
{
    const QPointer<WizDocumentWebView> guard(this);
    WizNotePresentation::editStyle(this, WizGlobal::settings(), [guard](const WizNotePresentation::Style& style) {
        if (guard) guard->page()->runJavaScript("if (typeof WizApplyNoteStyle==='function') WizApplyNoteStyle("
            + WizNotePresentation::jsonString(WizNotePresentation::styleSheet(style)) + ");");
    });
}

void WizDocumentWebView::saveAsMarkdown()
{
    const auto read = noteReadPermit();
    if (!read.current()) return;
    const QPointer<WizDocumentWebView> self(this);
    // prepare file and dir
    CString strTitle = view()->note().strTitle;
    WizMakeValidFileNameNoPath(strTitle);
    static QString strInitPath = QDir::homePath();
    QString strInitFileName = Utils::WizMisc::addBackslash2(strInitPath) + strTitle;
    // destination file address
    QString strIndexFileName = QFileDialog::getSaveFileName(this, QString(),
                                                       strInitFileName,
                                                       tr("Markdown Files (*.md)"));
    if (!self || !read.current()) return;
    saveAsMarkdown(strIndexFileName);
}

void WizDocumentWebView::saveAsMarkdown(QString& strIndexFileName, bool bSaveResource)
{
    Q_UNUSED(bSaveResource);
    const auto read = noteReadPermit();
    if (strIndexFileName.isEmpty() || !read.current()) return;
    const WIZDOCUMENTDATA doc = view()->note();
    const auto ticket = m_documentCallbacks.ticket();
    const QPointer<WizDocumentWebView> self(this);
    const QPointer<QWebEnginePage> sourcePage(page());
    if (!sourcePage) return;
    const auto query = [self, sourcePage, ticket, doc, read, strIndexFileName] {
        if (!self || !sourcePage || !WizDocumentCallbackEpoch::current(ticket)
            || self->page() != sourcePage || !self->acceptsDocumentCallback(ticket, doc) || !read.current()) return;
        sourcePage->runJavaScript(QString("WizEditor.getMarkdownSrc({unEscapeHtml: true});"),
        [self, sourcePage, ticket, doc, read, strIndexFileName](const QVariant& result) {
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
        if (WizMarkdownExportRecoveryApp::isQuitting()) return;
#endif
        // Reject stale/failed queries before touching the user's export files.
        if (!WizDocumentCallbackEpoch::current(ticket) || !self || !sourcePage
            || self->page() != sourcePage || !self->acceptsDocumentCallback(ticket, doc)
            || !read.current() || result.userType() != QMetaType::QString) return;
        const QString source = result.toString(); // A valid empty string is allowed.
        WizDatabase& db = self->m_dbMgr.db(doc.strKbGUID);
        QString error;
        if (!WizMarkdownExportTransaction::write(strIndexFileName, source,
                [&db, doc, read](const QString& stagedFile) {
                    return read.current() && db.exportToHtmlFile(doc, stagedFile) && read.current();
                }, error)) {
            qWarning() << "Markdown export failed:" << error;
        }
        // No owner access after a collaborator that could run a nested loop.
    });
    };
#ifdef WIZNOTE_ENABLE_MARKDOWN_RECOVERY
    if (WizMarkdownExportRecoveryApp::isQuitting()) return;
    cancelMarkdownRecovery();
    m_markdownRecoverySession = WizMarkdownExportRecoveryApp::open(this, ticket, strIndexFileName, query,
        [self](const WizMarkdownExportRecovery::Result& result) {
            if (self)
                WizMessageBox::information(self, QStringLiteral("Markdown 导出恢复"), result.userVisibleSummary);
            // A result is not permission to retry the original export.
        });
    if (m_markdownRecoverySession)
        m_markdownRecoverySession->show();
#else
    query();
#endif
}

void WizDocumentWebView::saveAsPlainMarkdown(QString& destFileName, std::function<void(QString fileName)> callback)
{
    const QString destination=destFileName;
    const auto read=noteReadPermit();
    if (destination.isEmpty() || !read.current()) return;
    page()->runJavaScript(QString("WizEditor.getMarkdownSrc({unEscapeHtml: true});"),
        [read,destination,callback](const QVariant& result){
        if (!read.current() || result.userType()!=QMetaType::QString) return;
        if (WizDocumentReadOutput::writeUtf8(read,destination,result.toString()) && callback)
            callback(destination);
    });
}

void WizDocumentWebView::saveAsPlainText(QString& destFileName, std::function<void(QString fileName)> callback)
{
    const QString destination=destFileName;
    const auto read=noteReadPermit();
    if (destination.isEmpty() || !read.current()) return;
    WIZDOCUMENTDATA docData;
    WizDatabase& db = m_dbMgr.db(view()->note().strKbGUID);
    if (!db.documentFromGuid(view()->note().strGUID, docData))
        return;
    const QPointer<WizDocumentWebView> self(this);
    trySaveDocumentForRead(docData, false, [self,read,docData,destination,callback](const QVariant& saved){
        if (!self || !read.current() || saved.userType()!=QMetaType::Bool || !saved.toBool()) return;
        // Extract the completed archive into our own disposable directory, not
        // the live editor's extracted HTML or resources.
        QTemporaryDir extracted;
        if (!extracted.isValid() || !self->m_dbMgr.db(docData.strKbGUID).documentToHtmlFile(docData,extracted.path()+"/")
            || !self || !read.current()) return;
        QString strHtml;
        QString strFileName = extracted.filePath("index.html");
        bool ret = WizLoadUnicodeTextFromFile(strFileName, strHtml);
        if (!ret)
        {
            return;
        }
        // get pure text
        QTextDocument doc;
        doc.setHtml(strHtml);
        QString strText = doc.toPlainText(); //auto deHtmlEscaped
        strText.replace("&nbsp", " "); // older version exsits unnomarlly &ngsp, so cannot be jump by QTextDocument
        // write file 
        if(WizDocumentReadOutput::writeUtf8(read,destination,strText) && callback)
        {
            callback(destination);
        }
    });
}

/**
 * @brief save note be html, include resource file
 */
void WizDocumentWebView::saveAsHtml()
{
    const auto read=noteReadPermit();
    if (!read.current()) return;
    const QPointer<WizDocumentWebView> self(this);
    const WIZDOCUMENTDATA doc=view()->note();
    CString strTitle = view()->note().strTitle;
    WizMakeValidFileNameNoPath(strTitle);
    strTitle = Utils::WizMisc::extractFileTitle(strTitle);
    //
    static QString strInitPath = QDir::homePath();
    QString strInitFileName = Utils::WizMisc::addBackslash2(strInitPath) + Utils::WizMisc::extractFileTitle(strTitle);
    //
    QString strIndexFileName = QFileDialog::getSaveFileName(this, QString(),
                                                       strInitFileName,
                                                       tr("Html Files (*.html)"));
    //
    if (!self || !read.current() || strIndexFileName.isEmpty())
        return;
    //
    strInitPath = Utils::WizMisc::extractFilePath(strIndexFileName);
    //
    // No destination/resource access before both the captured save frontier
    // and (for Markdown) a valid rendered reply. Stage body/resources beside
    // the target and commit the body last; do not enable persistent recovery.
    const auto publish=[self,read,doc,strIndexFileName](const QString& rendered) {
        if (!self || !read.current()) return;
        QString error;
        if (!WizMarkdownExportTransaction::writeHtml(strIndexFileName,rendered,
                [self,read,doc](const QString& staged) {
                    return self && read.current()
                        && self->m_dbMgr.db(doc.strKbGUID).exportToHtmlFile(doc,staged) && read.current();
                },[read]{return read.current();},error))
            qWarning() << "HTML export failed:" << error;
    };
    self->trySaveDocumentForRead(doc,false,[self,read,doc,publish](const QVariant& saved) {
        if (!self || !read.current() || saved.userType()!=QMetaType::Bool || !saved.toBool()) return;
        if (!WizIsMarkdownNote(doc)) {
            publish(QString());return;
        }
        self->page()->runJavaScript(QString("WizReader.getRenderDocument();"),[read,publish](const QVariant& result) {
            if (read.current() && result.userType()==QMetaType::QString) publish(result.toString());
        });
    });
}

/**
 * @brief save note be rendered html, exclude resource file
 * @param destFileName
 * @param callback
 */
void WizDocumentWebView::saveAsRenderedHtml(QString& destFileName, std::function<void(QString fileName)> callback)
{
    const QString destination=destFileName;
    const auto read=noteReadPermit();
    if (destination.isEmpty() || !read.current()) return;
    //FIXME: should trimmed Wiz's Script and Style.
    //Script: name="wiz_inner_script", wiz_style="unsave", src="contains: odemirror"
    //Style: id="wiz_custom_css", id="wiz_tmp_style_reader_common", name="wiz_tmp_editor_style"
    //          id="wiz_tmp_style_reader_block_scroll", id="wiz_tmp_style_code_common", name="wiz_tmp_editor_style"
    //          id="wiz_tmp_style_code_reader"
    //Tags: wiz_tmp_tag
    // not need remove those staff, for need rendered html page
    page()->toHtml([read,destination,callback](const QString& strHtml){
        if(WizDocumentReadOutput::writeUtf8(read,destination,strHtml) && callback)
            callback(destination);
    });
}

/**
 * @brief is note content modified
 * @param callback callback to judge wether saved succefully
 */
void WizDocumentWebView::isModified(std::function<void(bool modified)> callback)
{
    if (m_bContentsChanged)
    {
        callback(true);
        return;
    }
    const QPointer<WizDocumentWebView> self(this);
    const auto ticket = m_documentCallbacks.ticket();
    // Failure/unknown is conservative, never a false assertion of clean state.
    page()->runJavaScript(QString("(function(){try{return window.WizNativeEditorInitialized&&window.WizEditor?!!WizEditor.isModified():null;}catch(e){return null;}})()"),
        [self, ticket, callback](const QVariant& vModified){
        callback(!self || !WizDocumentCallbackEpoch::current(ticket) || self->m_bContentsChanged
            || vModified.userType() != QMetaType::Bool || vModified.toBool());
    });
}

/**
 * @brief set note content changed
 * @param b changed?
 */
void WizDocumentWebView::setModified(bool b)
{
    if (b) ++m_contentChangeSequence;
    m_bContentsChanged = b;
    // viewDocument clears the C++ flag before loading a page; there is no JS
    // editor then. Marking content dirty must not reset the editor's baseline.
    if (!b) page()->runJavaScript("if(window.WizNativeEditorInitialized && window.WizEditor) WizEditor.setUnModified();");
}

void WizDocumentWebView::undo()
{
    if (!noteReadPermit().current()) return;
    page()->runJavaScript("if(window.WizNativeEditorInitialized && window.WizEditor) WizEditor.undo();");
}

void WizDocumentWebView::redo()
{
    if (!noteReadPermit().current()) return;
    page()->runJavaScript("if(window.WizNativeEditorInitialized && window.WizEditor) WizEditor.redo();");
}

QString WizDocumentWebView::getUserGuid()
{
    return m_dbMgr.db().getUserGuid();
}


QString WizDocumentWebView::getUserAvatarFilePath()
{
    int size = 24;
    QString strFileName;
    QString strUserID = m_dbMgr.db().getUserId();
    if (WizAvatarHost::customSizeAvatar(strUserID, size, size, strFileName))
        return strFileName;


    return QString();
}

QString WizDocumentWebView::getUserAlias()
{
    QString strKbGUID = view()->note().strKbGUID;
    return m_dbMgr.db(strKbGUID).getUserAlias();
}


bool WizDocumentWebView::isPersonalDocument()
{
    QString strKbGUID = view()->note().strKbGUID;
    QString dbKbGUID = m_dbMgr.db().kbGUID();
    return strKbGUID.isEmpty() || (strKbGUID == dbKbGUID);
}

QString WizDocumentWebView::getCurrentNoteHtml()
{
    if (!noteReadPermit().current()) return QString();
    const auto& document = view()->note();
    if (m_currentNoteHtmlDocument.strGUID != document.strGUID
        || m_currentNoteHtmlDocument.strKbGUID != document.strKbGUID) return QString();
    return m_currentNoteHtml;
}


void copyFileToFolder(const QString& strFileFolder, const QString& strIndexFile, \
                         const QStringList& strResourceList)
{
    //copy index file
    QString strFolderIndex = strFileFolder + "index.html";
    if (strIndexFile != strFolderIndex)
    {
        QFile::remove(strFolderIndex);
        QFile::copy(strIndexFile, strFolderIndex);
    }

    //copy resources to temp folder
    QString strResourcePath = strFileFolder + "index_files/";
    for (int i = 0; i < strResourceList.count(); i++)
    {
        if (QFile::exists(strResourceList.at(i)))
        {
            QFile::copy(strResourceList.at(i), strResourcePath + Utils::WizMisc::extractFileName(strResourceList.at(i)));
        }
    }
}

bool WizDocumentWebView::hasEditPermissionOnCurrentNote()
{
    WIZDOCUMENTDATA docData = view()->note();
    WizDatabase& db = m_dbMgr.db(docData.strKbGUID);
    return db.canEditDocument(docData) && !WizDatabase::isInDeletedItems(docData.strLocation);
}

void WizDocumentWebView::changeCurrentDocumentType(const QString &strType)
{
    WIZDOCUMENTDATA docData = view()->note();
    WizDatabase& db = m_dbMgr.db(docData.strKbGUID);
    docData.strType = strType;
    db.modifyDocumentInfoEx(docData);
}

bool WizDocumentWebView::checkListClickable()
{
    WizDocumentView* v = view();
    if (!m_dbMgr.db(v->note().strKbGUID).isGroup())
    {
        emit clickingTodoCallBack(false, false);
        return true;
    }

    if (v->checkListClickable())
    {
        emit clickingTodoCallBack(false, false);
        v->setStatusToEditingByCheckList();
        return true;
    }
    emit clickingTodoCallBack(true, true);
    return false;
}

bool WizDocumentWebView::shouldAddCustomCSS()
{
    const WIZDOCUMENTDATA& data = view()->note();
    // note from web page canot add self define style 
    bool styledNote = data.strURL.startsWith("http");
    if (styledNote)
        return false;

    bool isMarkdown = WizIsMarkdownNote(data) && !view()->isEditing();

    return !isMarkdown;
}

bool WizDocumentWebView::canRenderMarkdown()
{
    const WIZDOCUMENTDATA& doc = view()->note();

    if (view()->isEditing())
        return false;

    return WizIsMarkdownNote(doc);
}

bool WizDocumentWebView::canEditNote()
{
    return view()->isEditing();
}

// used in editor initialize
QString WizDocumentWebView::getLocalLanguage()
{
    QLocale locale;
    if (locale.language() == QLocale::Chinese)
    {
        if (locale.country() == QLocale::China)
            return "zh-cn";
        else if (locale.country() == QLocale::Taiwan)
            return "zh-tw";
    }

    return "en";
}

void WizDocumentWebView::onSelectionChange(const QString& currentStyle)
{
    Q_EMIT statusChanged(currentStyle);
}


void WizDocumentWebView::onClickedSvg(const QString& data)
{
    const WIZDOCUMENTDATA note = view()->note();
    const auto ticket = m_documentCallbacks.ticket();
    trySaveDocument(note, true, WizDocumentOwnerAction::afterSuccessfulSave(this, ticket,
        [](auto action) { WizExecuteOnThread(WIZ_THREAD_MAIN, std::move(action)); },
        [note, data, ticket](WizDocumentWebView& owner) {
            const QString htmlFile = owner.m_strNoteHtmlFileName;
            // The original editor is modal. Its event loop may close/navigate
            // the source view; recheck before reloading after it returns.
            auto reload = WizDocumentOwnerAction::forDocument(&owner, ticket,
                [htmlFile](WizDocumentWebView& current) {
                    if (!current.addDefaultScriptsToDocumentHtml(htmlFile)) return;
                    const auto url = current.armEditorPageLoad(QUrl::fromLocalFile(htmlFile));
                    if (!url.isEmpty()) current.load(url);
                });
            editHandwritingNote(owner.m_dbMgr, note, htmlFile, data, WizMainWindow::instance());
            reload();
        }));

}

void WizDocumentWebView::updateSvg(QString data)
{
    //
    data = data.replace("\\", "\\\\");
    QString js = QString("WizEditor.replaceSvg(`%1`)").arg(data);
    page()->runJavaScript(js);
    //
}


void WizDocumentWebView::saveCurrentNote()
{
    WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::forDocument(this, m_documentCallbacks.ticket(), [](WizDocumentWebView& owner) {
        owner.onTimerAutoSaveTimout();
    }));
}

void WizDocumentWebView::onReturn()
{
    const auto read = noteReadPermit();
    if (!read.current()) return;
    WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::forDocument(this, m_documentCallbacks.ticket(), [read](WizDocumentWebView& owner) {
        if (read.current()) owner.tryResetTitle();
    }));
}


void WizDocumentWebView::doCopy()
{
    if (!noteReadPermit().current()) return;
    page()->triggerAction(QWebEnginePage::Copy, false);
}


void WizDocumentWebView::doPaste()
{
    const auto read = noteReadPermit();
    if (!read.current()) return;
    WizExecuteOnThread(WIZ_THREAD_MAIN, WizDocumentOwnerAction::forDocument(this, m_documentCallbacks.ticket(), [read](WizDocumentWebView& owner) {
        if (read.current()) owner.onPasteCommand();
    }));
}


void WizDocumentWebView::afterCopied()
{
    const auto read = noteReadPermit();
    if (!read.current()) return;
    WizWebEnginePage::processCopiedData([read]{return read.current();});
}

void WizDocumentWebView::onMarkerUndoStatusChanged(QString data)
{
    emit markerUndoStatusChanged(data);

}
void WizDocumentWebView::onMarkerInitiated(QString data)
{
    emit markerInitiated(data);
}

static std::map<QString, QWebEnginePage::WebAction> g_webActions;
void initWebActions(QWebEnginePage* page)
{
    if (!g_webActions.empty())
        return;
    //
    for (int action = QWebEnginePage::NoWebAction + 1;
         action < QWebEnginePage::WebActionCount;
         action++)
    {
        QWebEnginePage::WebAction a = (QWebEnginePage::WebAction)action;
        QAction* actionObj = page->action(a);
        //
        QString text = actionObj->text();
        g_webActions[text] = a;
        //
        text = text.replace("&", "");
        g_webActions[text] = a;
    }
}

QWebEnginePage::WebAction menuText2WebAction(QWebEnginePage* page, QString text)
{
    initWebActions(page);

    text = text.replace("&", "");
    auto it = g_webActions.find(text);
    if (it == g_webActions.end())
        return QWebEnginePage::NoWebAction;

    return it->second;
}

#define WIZEDITOR_ACTION_GOOGLE         QObject::tr("Use \"Google\" search")
#define WIZEDITOR_ACTION_BAIDU           QObject::tr("Use \"Baidu\" search")
#define WIZEDITOR_ACTION_CUT            QObject::tr("Cut")
#define WIZEDITOR_ACTION_COPY           QObject::tr("Copy")
#define WIZEDITOR_ACTION_PASTE          QObject::tr("Paste")
#define WIZEDITOR_ACTION_PASTE_PLAIN    QObject::tr("Paste plain text")
#define WIZEDITOR_ACTION_REMOVE_LINK    QObject::tr("Remove link")

void WizDocumentWebView::showContextMenu(const QPoint &pos)
{
    QWebEnginePage* page = this->page();
    if (!page)
        return;

    QMenu *menu = createStandardContextMenu();
    if (!menu)
        return;

    bool editing = isEditing();

    bool hasPasteMenu = false;
    bool hasLinkMenu = false;

    // get all actions of menu
    // FIXME: 不要使用这种遍历方法，速度有点慢，参考Qt浏览器实现示例
    QList<QAction*> actions = menu->actions();
    for (QAction* action : actions)
    {
        QWebEnginePage::WebAction a = menuText2WebAction(page, action->iconText());
        switch (a)
        {
        case QWebEnginePage::Copy:
            action->setText(QObject::tr("Copy"));
            break;
        case QWebEnginePage::Unselect:
            action->setText(QObject::tr("Unselect"));
            break;
        case QWebEnginePage::Back:
        case QWebEnginePage::Forward:
        case QWebEnginePage::Stop:
        case QWebEnginePage::Reload:
#if QT_VERSION >= 0x050600
        case QWebEnginePage::DownloadImageToDisk:
#endif
#if QT_VERSION >= 0x050800
        case QWebEnginePage::ViewSource:
#endif
            menu->removeAction(action);
            break;

        case QWebEnginePage::Paste:
            hasPasteMenu = true;
            break;
        case QWebEnginePage::OpenLinkInThisWindow:
        case QWebEnginePage::OpenLinkInNewWindow:
        case QWebEnginePage::OpenLinkInNewTab:
        case QWebEnginePage::DownloadLinkToDisk:
            menu->removeAction(action);
            hasLinkMenu = true;
            break;
        case QWebEnginePage::CopyLinkToClipboard:
            hasLinkMenu = true;
            break;
        default:
            break;
        }
    }

    if (!selectedText().isEmpty())
    {
        if (!menu->actions().isEmpty())
        {
            menu->addSeparator();
        }
        menu->addAction(WIZEDITOR_ACTION_GOOGLE, this, SLOT(on_editor_google_triggered()));
        menu->addAction(WIZEDITOR_ACTION_BAIDU, this, SLOT(on_editor_baidu_triggered()));
    }
    //
    if (editing)
    {
        if (!hasPasteMenu)
        {
            if (!menu->actions().isEmpty())
            {
                menu->addSeparator();
            }

            menu->addAction(WIZEDITOR_ACTION_PASTE, this, SLOT(on_editor_paste_triggered()));
            menu->addAction(WIZEDITOR_ACTION_PASTE_PLAIN, this, SLOT(on_editor_pastePlain_triggered()));
        }

        if (hasLinkMenu)
        {
            menu->addAction(WIZEDITOR_ACTION_REMOVE_LINK, this, SLOT(on_editor_removeLink_triggered()));
        }
    }

    if (menu->actions().isEmpty())
        return;

    menu->popup(pos);

    WizGetAnalyzer().logAction("editorContextMenu");
}
