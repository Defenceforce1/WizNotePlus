#import <Cocoa/Cocoa.h>
#include "WizMainWindow.h"
#include "WizThumbCache.h"
#include "WizDef.h"
#include "database/WizDatabase.h"
#include "database/WizDatabaseManager.h"
#include "share/WizGlobal.h"
#include "share/WizSettings.h"
#include "share/WizThreads.h"
#include "share/WizMisc.h"
#include "share/WizWebEngineView.h"
#include "sync/WizAvatarHost.h"
#include "sync/WizToken.h"
#include "gui/documentviewer/WizDocumentView.h"
#include "gui/documentviewer/WizDocumentWebView.h"
#include "gui/documentviewer/WizTitleEdit.h"
#include "gui/doclistviewer/WizDocumentListView.h"
#include "widgets/WizAdvancedSearchDialog.h"
#include "WizSearchWidget.h"
#include "share/WizLegacyLightAppearance.h"
#include "WizNotePresentation.h"
#include <QFileDialog>
#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QSettings>
#include <QStyleFactory>
#include <QDebug>
#include <QTimer>
#include <QPushButton>
#include <QVBoxLayout>
#include <QLabel>
#include <QAction>
#include <QMenu>
#include <QComboBox>
#include <QListWidget>
#include <QLineEdit>
#include <QToolButton>
#include <QMouseEvent>
#include <QCompleter>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QPlainTextEdit>
#include <QInputMethodEvent>
#include <QDir>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QNetworkProxy>
#include <functional>
#include <memory>
#include <vector>

// Exercise the actual main-window class, menus, toolbar, stylesheet, editor and
// close handler. A bare QMainWindow does not cover this application's chrome.
// No existing account is read. Sync and clipping are disabled by a test-only
// property; all remote browser requests are blocked and Qt networking is local.
class WindowTestRequestInterceptor final : public QWebEngineUrlRequestInterceptor
{
public:
    void interceptRequest(QWebEngineUrlRequestInfo& info) override
    {
        const QString scheme = info.requestUrl().scheme();
        if (scheme != "file" && scheme != "qrc" && scheme != "data" && scheme != "about" && scheme != "blob")
            info.block(true);
    }
};

int wizMacWindowControlsSelfTest(int argc, char** argv)
{
    const bool interactive = (argc == 2 && QByteArray(argv[1]) == "--mac-main-window-controls-probe")
        || QByteArray(argv[0]).endsWith("/WizWindowControlsTest");
    QTemporaryDir isolation;
    if (!isolation.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", isolation.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", isolation.filePath("cache").toUtf8());
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QApplication app(argc, argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("WizNotePlus Window Regression Test");
    app.setProperty("wiznoteIsolatedWindowControlsTest", true);
    if (QGuiApplication::platformName() != "cocoa") return 2;
    QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::HttpProxy, "127.0.0.1", 1));
    WindowTestRequestInterceptor interceptor;
    WizWebEngineProfile()->setUrlRequestInterceptor(&interceptor);
    WizWebEngineProfile()->settings()->setAttribute(QWebEngineSettings::FocusOnNavigationEnabled, false);
    WizWebEngineProfile()->settings()->setAttribute(QWebEngineSettings::FullScreenSupportEnabled, true);
    WizGlobal::setGlobalSettings(new QSettings(isolation.filePath("global.ini"), QSettings::IniFormat));
    WizGlobal::setSettings(new QSettings(isolation.filePath("user.ini"), QSettings::IniFormat));
    WizQueuedThreadsInit();
    WizToken token;
    WizDatabaseManager manager("synthetic-window-account");
    if (!manager.open()) return 1;
    manager.db().setKbGUID("synthetic-window-kb");
    WizUserSettings settings(manager.db());
    settings.setAutoCheckUpdate(false);
    settings.setSyncInterval(0);
    settings.setReceiveMobileFile(false);
    settings.setNewFeatureGuideVersion(WIZ_NEW_FEATURE_GUIDE_VERSION);
    app.setStyle(QStyleFactory::create("fusion"));
    // Simulate the reported dark-system/light-skin combination, in this process only.
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    app.styleHints()->setColorScheme(Qt::ColorScheme::Dark);
#endif
    QPalette dark = app.palette();
    dark.setColor(QPalette::Window, Qt::black);
    dark.setColor(QPalette::Base, Qt::black);
    dark.setColor(QPalette::Text, Qt::white);
    app.setPalette(dark);
    WizApplyLegacyLightAppearance();
    app.setStyleSheet(WizLoadSkinStyleSheet(settings.skin()));
    app.setFont(QFont(settings.UIFontFamily(), settings.UIFontSize()));
    WizAvatarHost avatar;
    WizThumbCache cache;
    WIZDOCUMENTDATA note;
    if (!manager.db().createDocumentAndInit(
            "<html><body><h1>隔离窗口测试</h1><p>这不是您的账号或笔记。仅验证主窗口按钮。</p></body></html>",
            "", 0, QStringLiteral("窗口按钮测试笔记"), "synthetic.html", "/Window test/", "", note)) return 1;
    WIZTAGDATA tag;
    if (!manager.db().createTag("", QStringLiteral("测试标签"), "", tag)
        || !manager.db().insertDocumentTag(note, tag.strGUID)) return 1;
    WIZDOCUMENTDATA unrelated;
    if (!manager.db().createDocumentAndInit("<html><body>无此标签的测试笔记</body></html>",
        "", 0, QStringLiteral("不应出现在标签结果中"), "other.html", "/Window test/", "", unrelated)) return 1;
    WIZTAGDATA secondTag;
    if (!manager.db().createTag("", QStringLiteral("待整理"), "", secondTag)
        || !manager.db().insertDocumentTag(note, secondTag.strGUID)
        || !manager.db().insertDocumentTag(unrelated, secondTag.strGUID)) return 1;
    WIZDOCUMENTDATA elsewhere;
    if (!manager.db().createDocumentAndInit("<html><body>另一个文件夹中的测试笔记</body></html>",
        "", 0, QStringLiteral("另一个文件夹的标签笔记"), "elsewhere.html", "/Elsewhere/", "", elsewhere)
        || !manager.db().insertDocumentTag(elsewhere, tag.strGUID)) return 1;
    WizFramelessWindow<QDialog> login(nullptr, false);
    login.titleBar()->layoutTitleBar();
    login.titleBar()->setFixedHeight(40);
    login.setZoomBtnEnabled(false);
    login.setWindowModality(Qt::ApplicationModal);
    login.resize(400, 300);
    login.show(); app.processEvents(); login.hide(); app.processEvents();
    WizMainWindow window(manager);
    window.setWindowTitle(QStringLiteral("WizNotePlus — 隔离主窗口测试"));
    window.resize(1000, 650);
    window.show();
    window.init();
    WIZDOCUMENTDATAEX extended;
    extended = note;
    // The one-argument overload creates a tab; the older two-argument overload
    // requires a pre-existing current document view and is not a startup path.
    window.viewDocument(extended);
    NSWindow* native = [(NSView*)window.winId() window];
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        if (!ok) qInfo() << "Window state at failure" << window.windowState()
            << "native fullscreen" << bool(native.styleMask & NSWindowStyleMaskFullScreen)
            << "native minimized" << native.miniaturized << "visible" << window.isVisible()
            << "modal" << QApplication::activeModalWidget()
            << "popup" << QApplication::activePopupWidget()
            << "native key window" << native.keyWindow << "app active" << NSApp.active;
        failures += !ok;
    };
    const auto fullscreenTransition = std::make_shared<bool>(false);
    const auto minimizeTransition = std::make_shared<bool>(false);
    const auto deminiaturized = std::make_shared<int>(0);
    const auto enteredFullscreen = std::make_shared<int>(0);
    const auto exitedFullscreen = std::make_shared<int>(0);
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    NSMutableArray* observers = [NSMutableArray array];
    for (NSString* name in @[NSWindowWillEnterFullScreenNotification, NSWindowWillExitFullScreenNotification])
        [observers addObject:[center addObserverForName:name object:native queue:nil usingBlock:^(NSNotification*) { *fullscreenTransition = true; }]];
    for (NSString* name in @[NSWindowDidEnterFullScreenNotification, NSWindowDidExitFullScreenNotification])
        [observers addObject:[center addObserverForName:name object:native queue:nil usingBlock:^(NSNotification*) { *fullscreenTransition = false; }]];
    [observers addObject:[center addObserverForName:NSWindowDidEnterFullScreenNotification object:native queue:nil usingBlock:^(NSNotification*) { ++*enteredFullscreen; }]];
    [observers addObject:[center addObserverForName:NSWindowDidExitFullScreenNotification object:native queue:nil usingBlock:^(NSNotification*) { ++*exitedFullscreen; }]];
    [observers addObject:[center addObserverForName:NSWindowWillMiniaturizeNotification object:native queue:nil usingBlock:^(NSNotification*) { *minimizeTransition = true; }]];
    [observers addObject:[center addObserverForName:NSWindowDidMiniaturizeNotification object:native queue:nil usingBlock:^(NSNotification*) { *minimizeTransition = false; }]];
    [observers addObject:[center addObserverForName:NSWindowDidDeminiaturizeNotification object:native queue:nil usingBlock:^(NSNotification*) { ++*deminiaturized; }]];
    auto fullscreenComplete = [&](bool full) {
        return !*fullscreenTransition && window.isFullScreen() == full
            && bool(native.styleMask & NSWindowStyleMaskFullScreen) == full;
    };
    auto minimizeComplete = [&](bool minimized) {
        return !*minimizeTransition && window.isMinimized() == minimized
            && bool(native.miniaturized) == minimized;
    };
    auto button = [&](NSWindowButton type) { return [native standardWindowButton:type]; };
    auto hit = [&](NSButton* control) {
        NSView* frame = native.contentView.superview;
        NSPoint p = [control convertPoint:NSMakePoint(NSMidX(control.bounds), NSMidY(control.bounds)) toView:frame];
        NSView* target = [frame hitTest:p];
        return target == control || [target isDescendantOf:control];
    };
    // Run every action from the application's real event loop. A tight manual
    // processEvents loop does not enter NSApplication.run and is not an adequate
    // model of the native fullscreen/Space lifecycle. Keep timers asynchronous.
    struct Step {
        const char* label;
        std::function<void()> begin;
        std::function<bool()> complete;
        int timeout;
    };
    std::vector<Step> steps;
    auto addCheck = [&](const char* label, std::function<bool()> complete,
                        std::function<void()> begin = {}, int timeout = 8000) {
        steps.push_back({label, std::move(begin), std::move(complete), timeout});
    };
    addCheck("actual main-window synthetic note loads in the actual editor",
        [&] { return window.docView() && window.docView()->noteLoaded(); }, {}, 15000);
    const auto editorReady = std::make_shared<bool>(false);
    const auto queryPending = std::make_shared<bool>(false);
    addCheck("actual editor JavaScript is initialized before window-action checks", [&] {
        if (!*editorReady && !*queryPending && window.docView()) {
            *queryPending = true;
            window.docView()->web()->page()->runJavaScript(
                "!!(window.WizEditor && typeof window.WizEditor.focus === 'function')",
                [editorReady, queryPending](const QVariant& result) {
                    *editorReady = result.toBool(); *queryPending = false;
                });
        }
        return *editorReady;
    }, {}, 15000);
    const QString artifactDir = qEnvironmentVariable("WIZNOTE_TEST_ARTIFACT_DIR");
    auto capture = [&](QWidget* widget, const QString& name) {
        if (!artifactDir.isEmpty() && QDir().mkpath(artifactDir))
            widget->grab().save(QDir(artifactDir).filePath(name));
    };
    addCheck("actual main-window search and title fields have readable light backgrounds and dark text", [&] {
        auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
        auto* title = window.findChild<WizTitleEdit*>();
        if (!search || !title || !search->isVisible() || !title->isVisible()) return false;
        search->ensurePolished(); title->ensurePolished();
        for (const QLineEdit* edit : {static_cast<QLineEdit*>(search), static_cast<QLineEdit*>(title)}) {
            if (edit->palette().color(QPalette::Base).lightnessF() < 0.8
                || edit->palette().color(QPalette::Text).lightnessF() > 0.4) return false;
        }
        capture(&window, "v11-main-light.png");
        return true;
    });
    addCheck("refined search field keeps readable height and reserved icon spacing", [&] {
        auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
        if (!search || search->height() != 30 || search->textMargins().left() < 32
            || search->textMargins().right() < 28) return false;
        search->setFocus();
        search->setText(QStringLiteral("长关键词与标签搜索 / mathematical notes and references"));
        capture(&window, "v11-search-filled.png");
        search->clear();
        if (window.docView()) window.docView()->setEditorFocus();
        return true;
    });
    const auto searchAligned = [&] {
        auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
        auto* newNote = window.findChild<QToolButton*>("btn-newnote");
        return search && newNote && qAbs(search->mapTo(&window, search->rect().center()).y()
            - newNote->mapTo(&window, newNote->rect().center()).y()) <= 1;
    };
    addCheck("search and New Note centers align without filters", searchAligned);
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    addCheck("native title bar uses Aqua after simulated dark appearance", [&] {
        return [[native.effectiveAppearance bestMatchFromAppearancesWithNames:
                 @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]] isEqualToString:NSAppearanceNameAqua];
    });
#endif
    if (!interactive) {
        auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
        auto* menu = search ? search->findChild<QMenu*>("wiz-note-search-menu") : nullptr;
        const auto sendClick = [](QWidget* target, const QPoint& local) {
            if (!target) return;
            const QPoint global = target->mapToGlobal(local);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(local), QPointF(global),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &press);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), QPointF(global),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &release);
        };
        const auto clickMagnifier = [search, sendClick] {
            if (search) sendClick(search, QPoint(12, search->height() / 2));
        };
        const auto chooseAdvanced = [menu, sendClick] {
            if (menu && menu->findChild<QAction*>("actionAdvancedSearch"))
                QTimer::singleShot(0, menu, [menu, sendClick] {
                    sendClick(menu, menu->actionGeometry(menu->findChild<QAction*>("actionAdvancedSearch")).center());
                });
        };
        addCheck("magnifier opens an inline personal filter panel without a modal dialog or extra toolbar button", [&, menu] {
            auto* panel = window.findChild<QWidget*>("wiz-note-filter-panel");
            auto* advanced = menu ? menu->findChild<QAction*>("actionAdvancedSearch") : nullptr;
            if (!menu || !menu->isVisible() || !panel || !panel->isVisible() || !advanced
                || QApplication::activeModalWidget()) return false;
            for (auto* tool : window.findChildren<QToolButton*>())
                if (tool->defaultAction() == advanced && tool->isVisible()) return false;
            capture(menu, "v9-filter-panel.png");
            return true;
        }, clickMagnifier);
        const auto selectPanelTag = [&, sendClick](const QString& guid) {
            auto* tags = window.findChild<QListWidget*>("wiz-note-panel-tags");
            if (!tags) return;
            for (int i = 0; i < tags->count(); ++i) if (tags->item(i)->data(Qt::UserRole).toString() == guid) {
                const QRect rect = tags->visualItemRect(tags->item(i));
                sendClick(tags->viewport(), QPoint(rect.left() + 8, rect.center().y())); return;
            }
        };
        addCheck("one panel tag click searches actual notes and displays a removable tag chip", [&] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            const bool ok = view && view->hasFilters() && view->queryParams().contains(tag.strGUID)
                && docs && docs->isSearchResult() && docs->documentCount() == 2
                && docs->documentIndexFromGUID(note.strGUID) >= 0 && docs->documentIndexFromGUID(elsewhere.strGUID) >= 0
                && docs->documentIndexFromGUID(unrelated.strGUID) < 0 && !QApplication::activeModalWidget();
            if (ok) capture(&window, "v9-tag-chip.png");
            return ok;
        }, [&, selectPanelTag] { selectPanelTag(tag.strGUID); });
        addCheck("filter tokens are inside the search field with keyword space and aligned toolbar", [&] {
            auto* row = window.findChild<QWidget*>("wiz-note-search-chips");
            auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
            if (!row || !search || !row->isVisible() || !searchAligned()
                || row->parentWidget() != search || row->geometry().bottom() >= search->height()
                || search->textMargins().left() <= row->geometry().right()
                || search->width() - search->textMargins().left() - search->textMargins().right() < 120
                || !search->hasFocus()) return false;
            const QRect caret = search->inputMethodQuery(Qt::ImCursorRectangle).toRect();
            if (caret.left() <= row->geometry().right()) return false;
            capture(&window, "v12-search-tokens.png");
            return true;
        });
        addCheck("IME keyword input follows tokens without mixing tag labels into the keyword", [&, search] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* row = window.findChild<QWidget*>("wiz-note-search-chips");
            if (!view || !search || !row || view->keyword() != QStringLiteral("窗口")
                || !view->queryParams().contains(tag.strGUID)
                || !view->queryParams().contains(QStringLiteral("keyword:窗口"))) return false;
            const QRect caret = search->inputMethodQuery(Qt::ImCursorRectangle).toRect();
            if (caret.left() <= row->geometry().right()) return false;
            capture(&window, "v12-search-token-keyword.png");
            return true;
        }, [search] {
            if (!search) return;
            QInputMethodEvent commit; commit.setCommitString(QStringLiteral("窗口"));
            QCoreApplication::sendEvent(search, &commit);
        });
        addCheck("keyword clear icon leaves selected tags and returns the caret after tokens", [&, search] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            return view && search && view->keyword().isEmpty() && view->queryParams().contains(tag.strGUID)
                && search->hasFocus() && docs && docs->documentCount() == 2
                && !QApplication::activeModalWidget();
        }, [search, sendClick] {
            if (search) sendClick(search, QPoint(search->width() - 14, search->height() / 2));
        });
        addCheck("inline folder selection intersects tag results in actual main list", [&] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            return view && view->queryParams().contains("folder-path-encoded:") && docs
                && docs->documentCount() == 1 && docs->documentIndexFromGUID(note.strGUID) >= 0
                && docs->documentIndexFromGUID(elsewhere.strGUID) < 0 && !QApplication::activeModalWidget();
        }, [&, clickMagnifier] {
            clickMagnifier();
            auto* folder = window.findChild<QComboBox*>("wiz-note-panel-folder");
            if (folder) {
                folder->setCurrentIndex(folder->findData("/Window test/"));
                QMetaObject::invokeMethod(folder, "activated", Q_ARG(int, folder->currentIndex()));
            }
        });
        addCheck("adding a second inline tag retains AND semantics and current folder", [&] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            return view && view->queryParams().contains(tag.strGUID) && view->queryParams().contains(secondTag.strGUID)
                && view->queryParams().contains("folder-path-encoded:") && docs && docs->documentCount() == 1
                && docs->documentIndexFromGUID(unrelated.strGUID) < 0 && !QApplication::activeModalWidget();
        }, [&, clickMagnifier, selectPanelTag] { clickMagnifier(); selectPanelTag(secondTag.strGUID); });
        addCheck("inline attachment checkbox filters notes with no attachments out", [&] {
            auto* docs = window.findChild<WizDocumentListView*>();
            auto* attachment = window.findChild<QCheckBox*>("wiz-note-panel-attachments");
            return attachment && attachment->isChecked() && docs && docs->documentCount() == 0
                && !QApplication::activeModalWidget();
        }, [&, clickMagnifier, sendClick] {
            clickMagnifier();
            auto* attachment = window.findChild<QCheckBox*>("wiz-note-panel-attachments");
            if (attachment) sendClick(attachment, QPoint(8, attachment->height() / 2));
        });
        addCheck("folded conditions reopen the panel without changing the combined search", [&, menu] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* attachment = window.findChild<QCheckBox*>("wiz-note-panel-attachments");
            return menu && menu->isVisible() && view && view->queryParams().contains(tag.strGUID)
                && view->queryParams().contains(secondTag.strGUID) && attachment && attachment->isChecked();
        }, [&, sendClick] {
            auto* more = window.findChild<QPushButton*>("wiz-note-more-filters");
            if (more && more->isVisible()) sendClick(more, more->rect().center());
        });
        const auto clickChip = [&, sendClick](const QString& kind, const QString& id = QString()) {
            for (auto* chip : window.findChildren<QPushButton*>("wiz-note-filter-chip"))
                if (chip->isVisible() && chip->property("filterKind").toString() == kind
                    && (id.isEmpty() || chip->property("filterId").toString() == id)) {
                    sendClick(chip, chip->rect().center()); return;
                }
        };
        addCheck("removing one visible tag reveals folded chips and retains other conditions", [&] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            auto* attachment = window.findChild<QCheckBox*>("wiz-note-panel-attachments");
            return view && view->queryParams().contains(tag.strGUID) && !view->queryParams().contains(secondTag.strGUID)
                && view->queryParams().contains("folder-path-encoded:") && attachment && attachment->isChecked()
                && docs && docs->documentCount() == 0 && !QApplication::activeModalWidget();
        }, [&, menu, clickChip] { if (menu) menu->hide(); clickChip("tag", secondTag.strGUID); });
        addCheck("removing a folded attachment condition restores matching notes without losing tags or folder", [&] {
            auto* attachment = window.findChild<QCheckBox*>("wiz-note-panel-attachments");
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            return attachment && !attachment->isChecked() && view && view->queryParams().contains(tag.strGUID)
                && !view->queryParams().contains(secondTag.strGUID) && view->queryParams().contains("folder-path-encoded:")
                && docs && docs->documentCount() == 1
                && !QApplication::activeModalWidget();
        }, [&, clickMagnifier, sendClick] {
            clickMagnifier();
            auto* attachment = window.findChild<QCheckBox*>("wiz-note-panel-attachments");
            if (attachment) sendClick(attachment, QPoint(8, attachment->height() / 2));
        });
        addCheck("clear filters in magnifier exits filter search and restores ordinary note list", [&] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            return view && !view->hasFilters() && view->keyword().isEmpty() && docs
                && !docs->acceptAllSearchItems() && docs->documentCount() == 3;
        }, [&, clickMagnifier, sendClick] {
            clickMagnifier();
            auto* clear = window.findChild<QPushButton*>("wiz-note-panel-clear");
            if (clear && clear->isVisible()) sendClick(clear, clear->rect().center());
        });
        addCheck("typing a tag offers local suggestions without submitting a note search", [&] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* completer = view ? view->findChild<QCompleter*>() : nullptr;
            auto* docs = window.findChild<WizDocumentListView*>();
            const bool ok = view && !view->hasFilters() && completer && completer->popup()->isVisible()
                && !completer->popup()->currentIndex().isValid() && docs && !docs->acceptAllSearchItems();
            if (ok) capture(completer->popup(), "v9-tag-suggestions.png");
            return ok;
        }, [search] {
            if (search) {
                search->setFocus();
                QInputMethodEvent commit; commit.setCommitString(QStringLiteral("测试标签"));
                QCoreApplication::sendEvent(search, &commit);
            }
        });
        addCheck("explicit suggestion click creates a tag condition and searches real temporary notes", [&] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            return view && view->hasFilters() && view->keyword().isEmpty() && view->queryParams().contains(tag.strGUID)
                && docs && docs->documentCount() == 2 && docs->documentIndexFromGUID(unrelated.strGUID) < 0
                && !QApplication::activeModalWidget();
        }, [&, sendClick] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* completer = view ? view->findChild<QCompleter*>() : nullptr;
            if (completer) {
                auto* popup = completer->popup();
                sendClick(popup->viewport(), popup->visualRect(completer->completionModel()->index(0, 0)).center());
            }
        });
        const auto savedQuery = std::make_shared<bool>(false);
        addCheck("save-current-search menu prefills and persists the actual inline tag query", [&, savedQuery] {
            auto* dialog = qobject_cast<WizAdvancedSearchDialog*>(QApplication::activeModalWidget());
            if (dialog && !*savedQuery) {
                auto* name = dialog->findChild<QLineEdit*>("lineEdit_name");
                auto* list = dialog->findChild<QListWidget*>("listWidget");
                auto* ok = dialog->findChild<QPushButton*>("pushButton_ok");
                if (!name || !name->isVisible() || !list || list->count() != 1
                    || !dialog->getParams().contains(tag.strGUID) || !ok) {
                    dialog->reject(); return false;
                }
                name->setText(QStringLiteral("隔离已保存搜索"));
                *savedQuery = true;
                QTimer::singleShot(0, dialog, [ok] { ok->click(); });
            }
            if (!*savedQuery || QApplication::activeModalWidget()) return false;
            CWizMetaDataArray values;
            manager.db().getMetasByName("CUSTOM_QUICK_SEARCH", values);
            for (const auto& value : values)
                if (value.strValue.contains(QStringLiteral("name:隔离已保存搜索"))
                    && value.strValue.contains(tag.strGUID)) return true;
            return false;
        }, [clickMagnifier, menu, sendClick] {
            clickMagnifier();
            if (menu) QTimer::singleShot(0, menu, [menu, sendClick] {
                auto* save = menu->findChild<QAction*>("actionSaveSearch");
                if (save) sendClick(menu, menu->actionGeometry(save).center());
            });
        });
        const auto submitted = std::make_shared<bool>(false);
        addCheck("clicking advanced-search in magnifier menu opens dialog and tag-only search filters actual main list", [&, submitted] {
            auto* dialog = qobject_cast<WizAdvancedSearchDialog*>(QApplication::activeModalWidget());
            if (dialog && !*submitted) {
                auto* first = dialog->findChild<QComboBox*>("comboBox_first");
                auto* second = dialog->findChild<QComboBox*>("comboBox_second");
                auto* add = dialog->findChild<QPushButton*>("toolButton_add");
                auto* ok = dialog->findChild<QPushButton*>("pushButton_ok");
                auto* list = dialog->findChild<QListWidget*>("listWidget");
                if (!first || !second || !add || !ok || !list) { dialog->reject(); return false; }
                const int index = first->findText(QObject::tr("Tag"));
                if (index < 0) { dialog->reject(); return false; }
                first->setCurrentIndex(index);
                QMetaObject::invokeMethod(first, "activated", Q_ARG(int, index));
                const int selected = second->findData(tag.strGUID);
                if (selected < 0) { dialog->reject(); return false; }
                second->setCurrentIndex(selected); add->click();
                if (list->count() != 1) { dialog->reject(); return false; }
                capture(dialog, "v9-advanced-tag.png");
                *submitted = true;
                // Queue acceptance so the step timer never blocks in a nested search modal.
                QTimer::singleShot(0, dialog, [ok] { ok->click(); });
            }
            auto* docs = window.findChild<WizDocumentListView*>();
            return *submitted && !QApplication::activeModalWidget() && docs && docs->isSearchResult()
                && docs->documentCount() == 2 && docs->documentIndexFromGUID(note.strGUID) >= 0
                && docs->documentIndexFromGUID(unrelated.strGUID) < 0;
        }, [clickMagnifier, chooseAdvanced] { clickMagnifier(); chooseAdvanced(); });
        const auto menuOpened = std::make_shared<bool>(false);
        addCheck("reopening magnifier menu and advanced search opens a cancellable built-in dialog", [menuOpened] {
            auto* dialog = qobject_cast<WizAdvancedSearchDialog*>(QApplication::activeModalWidget());
            if (dialog) { *menuOpened = true; dialog->reject(); }
            return *menuOpened && !QApplication::activeModalWidget();
        }, [clickMagnifier, chooseAdvanced] {
            clickMagnifier();
            chooseAdvanced();
        });
    }
    if (!interactive) {
        const auto originalHtml = std::make_shared<QString>();
        const auto sourceReady = std::make_shared<bool>(false);
        addCheck("original editor source is captured before presentation operations", [&, sourceReady, originalHtml] {
            if (!*sourceReady && !*queryPending) {
                *queryPending = true;
                window.docView()->web()->page()->runJavaScript("WizEditor.getContentHtml()", [=](const QVariant& value) {
                    *originalHtml = value.toString(); *sourceReady = !originalHtml->isEmpty(); *queryPending = false;
                });
            }
            return *sourceReady;
        });
        const auto styleVisited = std::make_shared<bool>(false);
        const auto styleMeasured = std::make_shared<bool>(false);
        const auto styleVisible = std::make_shared<bool>(false);
        addCheck("Tools menu opens CSS dialog, visibly styles paragraphs, and cancel restores rendering", [&, styleVisited, styleMeasured, styleVisible] {
            auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
            if (dialog && dialog->objectName() == "wiz-note-style-dialog" && !*styleVisited) {
                auto* css = dialog->findChild<QPlainTextEdit*>("wiz-note-screen-css");
                auto* preview = dialog->findChild<QPushButton*>("wiz-note-css-preview");
                if (!css || !preview) { dialog->reject(); return false; }
                window.docView()->web()->page()->runJavaScript(
                    "(function(){function read(){var p=document.querySelector('.wiz-editor-body p');var s=getComputedStyle(document.body);"
                    "return {bodyClass:document.body.className,bodyColor:s.color,bodyLine:s.lineHeight,paragraphColor:p?getComputedStyle(p).color:'',paragraphLine:p?getComputedStyle(p).lineHeight:''};}"
                    "var before=read();WizApplyNoteStyle('@media screen {body {line-height:1.7;color:#263950;}}');return {before:before,after:read()};})()",
                    [](const QVariant& value) { qInfo() << "CSS original-example computed comparison" << value; });
                css->setPlainText("body, .wiz-editor-body { background-color:#fff8dc !important; } .wiz-editor-body p, .wiz-editor-body li { color:#0066cc !important; font-size:22px !important; line-height:2 !important; }"); preview->click();
                capture(dialog, "v10-css-dialog.png"); *styleVisited = true;
                window.docView()->web()->page()->runJavaScript(
                    "(function(){var p=document.querySelector('.wiz-editor-body p');var s=p?getComputedStyle(p):null;"
                    "return {background:getComputedStyle(document.body).backgroundColor,color:s?s.color:'',fontSize:s?s.fontSize:'',lineHeight:s?s.lineHeight:''};})()",
                    [&, styleMeasured, styleVisible](const QVariant& value) {
                        qInfo() << "CSS visible diagnostic computed values" << value;
                        const auto style = value.toMap();
                        *styleVisible = style.value("background") == "rgb(255, 248, 220)" && style.value("color") == "rgb(0, 102, 204)"
                            && style.value("fontSize") == "22px" && style.value("lineHeight") == "44px";
                        *styleMeasured = true;
                        capture(&window, "css-visible-diagnostic.png");
                    });
            }
            if (dialog && *styleMeasured) QTimer::singleShot(0, dialog, &QDialog::reject);
            return *styleVisited && *styleVisible && !QApplication::activeModalWidget();
        }, [&] {
            if (auto* action = window.findChild<QAction*>("actionNoteStyle"))
                QTimer::singleShot(0, action, &QAction::trigger);
        });
        const auto pdfVisited = std::make_shared<bool>(false);
        const bool wasEditing = window.docView()->isEditing();
        addCheck("original Save as PDF opens its save dialog without print preview", [&, pdfVisited, wasEditing] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (dialog) {
                if (window.docView()->isEditing() != wasEditing) { dialog->reject(); return false; }
                *pdfVisited = true; QTimer::singleShot(0, dialog, &QDialog::reject);
            }
            return *pdfVisited && !QApplication::activeModalWidget() && !window.findChild<QAction*>("actionPrintNote");
        }, [&] {
            if (auto* action = window.findChild<QAction*>("actionSaveAsPDF"))
                QTimer::singleShot(0, action, &QAction::trigger);
        });
        const auto unchanged = std::make_shared<bool>(false);
        addCheck("actual editor HTML remains identical after CSS preview and canceling original PDF export", [&, unchanged, originalHtml] {
            if (!*unchanged && !*queryPending) {
                *queryPending = true;
                window.docView()->web()->page()->runJavaScript("WizEditor.getContentHtml()", [=](const QVariant& value) {
                    *unchanged = value.toString() == *originalHtml; *queryPending = false;
                });
            }
            return *unchanged;
        });
    }
    addCheck("Qt native delegate is retained, not replaced by the legacy frameless proxy", [&] {
        return [native.delegate respondsToSelector:@selector(windowShouldClose:)]
            && ![NSStringFromClass([native.delegate class]) isEqualToString:@"NSWindowProxyDelegate"];
    }, [&] { if (window.docView()) window.docView()->setEditorFocus(); });
    addCheck("system title bar is separate from the client toolbar",
        [&] { return !(native.styleMask & NSWindowStyleMaskFullSizeContentView); });
    auto controlsEnabled = [&] {
        return button(NSWindowCloseButton).enabled && button(NSWindowMiniaturizeButton).enabled
            && button(NSWindowZoomButton).enabled;
    };
    addCheck("all three actual main-window controls enabled", controlsEnabled);
    for (NSWindowButton type : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton})
        addCheck("system control receives native pointer hit testing", [&, type] { return hit(button(type)); });
    qInfo().noquote() << "Native delegate:" << QString::fromNSString(NSStringFromClass([native.delegate class]));
    QWidget tools;
    QTimer state;
    QString last;
    QDialog modal(&window);
    modal.setWindowModality(Qt::ApplicationModal);
    int beforeRestore = 0;
    int beforeFull = 0;
    auto restoreWindow = [&] {
        beforeRestore = *deminiaturized;
        window.showNormal(); window.activateWindow();
    };
    auto restoreComplete = [&] {
        return *deminiaturized > beforeRestore && minimizeComplete(false) && native.visible;
    };
    auto reopen = [&] { window.showNormal(); window.activateWindow(); };
    auto reopened = [&] { return window.isVisible() && native.visible && controlsEnabled(); };
    auto addFullscreenCheck = [&](bool full, const char* label) {
        const auto completionCount = full ? enteredFullscreen : exitedFullscreen;
        addCheck(label, [&, full, completionCount] {
            return *completionCount > beforeFull && fullscreenComplete(full);
        }, [&, completionCount] {
            beforeFull = *completionCount;
            [button(NSWindowZoomButton) performClick:nil];
        });
    };
    if (interactive) {
        tools.setWindowTitle(QStringLiteral("隔离测试控制台"));
        auto* layout = new QVBoxLayout(&tools);
        layout->addWidget(new QLabel(QStringLiteral("无账号、无联网、无真实笔记。\n请用鼠标测试主窗口红/黄/绿按钮。")));
        auto* restore = new QPushButton(QStringLiteral("恢复主窗口"));
        layout->addWidget(restore);
        QObject::connect(restore, &QPushButton::clicked, &window, [&] { window.showNormal(); window.activateWindow(); });
        auto* modal = new QPushButton(QStringLiteral("测试模态窗口"));
        layout->addWidget(modal);
        QObject::connect(modal, &QPushButton::clicked, &window, [&] {
            QDialog dialog(&window); dialog.setWindowTitle(QStringLiteral("隔离模态测试"));
            auto* box = new QVBoxLayout(&dialog);
            auto* done = new QPushButton(QStringLiteral("完成")); box->addWidget(done);
            QObject::connect(done, &QPushButton::clicked, &dialog, &QDialog::accept);
            dialog.exec();
        });
        auto* done = new QPushButton(QStringLiteral("结束测试")); layout->addWidget(done);
        QObject::connect(done, &QPushButton::clicked, &app, &QApplication::quit);
        tools.move(20, 120);
        QObject::connect(&state, &QTimer::timeout, &window, [&] {
            const QString now = QStringLiteral("STATE visible=%1 minimized(Qt/native)=%2/%3 fullscreen(Qt/native)=%4/%5 modal=%6")
                .arg(window.isVisible()).arg(window.isMinimized()).arg(native.miniaturized)
                .arg(window.isFullScreen()).arg(bool(native.styleMask & NSWindowStyleMaskFullScreen))
                .arg(QApplication::activeModalWidget() != nullptr);
            if (now != last) { qInfo().noquote() << now; last = now; }
        });
    } else {
        addCheck("actual main-window yellow action minimizes Qt and native window",
            [&] { return minimizeComplete(true); }, [&] { [button(NSWindowMiniaturizeButton) performClick:nil]; });
        addCheck("actual main window restores after minimize", restoreComplete, restoreWindow);
        addFullscreenCheck(true, "actual main-window green action enters fullscreen");
        addFullscreenCheck(false, "actual main-window green action exits fullscreen");
        addCheck("actual main window is blocked by modal dialog", [&] {
            return QApplication::activeModalWidget() == &modal
                && !button(NSWindowCloseButton).enabled && !button(NSWindowMiniaturizeButton).enabled
                && !button(NSWindowZoomButton).enabled;
        }, [&] { modal.show(); }, 2000);
        addCheck("all three controls restore after modal closes",
            [&] { return !QApplication::activeModalWidget() && !QApplication::activePopupWidget()
                && !NSApp.modalWindow && native.keyWindow && controlsEnabled(); },
            [&] { modal.close(); window.activateWindow(); });
        addCheck("actual main-window red action hides the window",
            [&] { return !window.isVisible() && !native.visible; }, [&] { [button(NSWindowCloseButton) performClick:nil]; });
        addCheck("actual main window reopens after red close", reopened, reopen);
        addCheck("yellow action still works after red close and reopen",
            [&] { return minimizeComplete(true); }, [&] { [button(NSWindowMiniaturizeButton) performClick:nil]; });
        addCheck("final native minimize restoration", restoreComplete, restoreWindow);
        addFullscreenCheck(true, "green action still works after red close and reopen");
        addCheck("red close exits fullscreen and hides the actual main window", [&] {
            return *exitedFullscreen > beforeFull && !window.isVisible() && !native.visible && fullscreenComplete(false);
        }, [&] { beforeFull = *exitedFullscreen; [button(NSWindowCloseButton) performClick:nil]; });
        addCheck("actual main window restores after fullscreen red close", reopened, reopen);
    }
    QTimer test;
    QElapsedTimer deadline;
    size_t step = 0;
    bool started = false;
    QObject::connect(&test, &QTimer::timeout, &app, [&] {
        if (step == steps.size()) {
            test.stop();
            if (interactive) {
                tools.show(); state.start(150);
                QTimer::singleShot(10 * 60 * 1000, &app, &QApplication::quit);
                qInfo() << "READY actual main-window pointer test";
            } else app.quit();
            return;
        }
        Step& current = steps[step];
        if (!started) {
            started = true; deadline.start();
            if (current.begin) current.begin();
        }
        const bool complete = current.complete();
        if (!complete && deadline.elapsed() < current.timeout) return;
        check(complete, current.label);
        if (!complete) {
            // Later actions depend on this transition. Stop instead of clicking
            // the green control in an unknown state and hiding the first cause.
            test.stop(); app.quit(); return;
        }
        ++step; started = false;
    });
    test.start(20);
    app.exec();
    window.hide();
    for (id observer in observers) [center removeObserver:observer];
    return failures ? 1 : 0;
}
