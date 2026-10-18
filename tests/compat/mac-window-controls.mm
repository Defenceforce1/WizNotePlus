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
#include "widgets/WizUserInfoWidget.h"
#include "gui/documentviewer/WizDocumentView.h"
#include "gui/documentviewer/WizDocumentWebView.h"
#include "gui/documentviewer/WizEditorInsertLinkForm.h"
#include "gui/documentviewer/WizEditorToolBar.h"
#include "gui/documentviewer/WizTitleEdit.h"
#include "gui/tabbrowser/WizMainTabBrowser.h"
#include "gui/tabbrowser/TabButton.h"
#include "gui/doclistviewer/WizDocumentListView.h"
#include "gui/categoryviewer/WizCategoryView.h"
#include "gui/categoryviewer/WizCategoryViewGroupItem.h"
#include "widgets/WizAdvancedSearchDialog.h"
#include "WizSearchWidget.h"
#include "share/WizLegacyLightAppearance.h"
#include "WizNotePresentation.h"
#include "WizBuildIdentity.h"
#include "utils/WizPathResolve.h"
#include <QTranslator>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProgressDialog>
#include <QImage>
#include <QApplication>
#include <QTabBar>
#include <QAbstractButton>
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
#include <QToolBar>
#include <QWindow>
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
class TabCloseFixturePage final : public AbstractTabPage {
public:
    QString name;
    explicit TabCloseFixturePage(const QString& title) : name(title) {}
    QString Title() override { return name; }
    void RequestClose(bool = false) override { Q_EMIT pageCloseRequested(); }
};

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
    const bool headerOnly = !interactive && qEnvironmentVariableIntValue("WIZNOTE_TEST_HEADER_ONLY") == 1;
    if (headerOnly) qInfo() << "HEADER-ONLY diagnostic: not the full main-window regression";
    QTemporaryDir isolation;
    if (!isolation.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", isolation.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", isolation.filePath("cache").toUtf8());
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    QApplication app(argc, argv);
    // Exercise the actual deployed Qt 6 base catalog, not its dependency-only
    // qt_zh_CN index. Only this isolated process changes language.
    QTranslator qtChinese;
    const bool qtChineseLoaded = qtChinese.load(Utils::WizPathResolve::qtLocaleFileName(QStringLiteral("zh_CN")));
    if (qtChineseLoaded) app.installTranslator(&qtChinese);
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
    if (window.findChild<QAction*>("actionConnectNativeClipper")) {
        qWarning() << "Obsolete native clipper pairing action is still present";
        return 1;
    }
    for (auto* object : window.findChildren<QObject*>()) {
        if (QByteArray(object->metaObject()->className()) == "WizNativeClipperBridge") {
            qWarning() << "Normal main window still instantiates the obsolete 8849 bridge";
            return 1;
        }
    }
    qInfo() << "PASS obsolete 8849 bridge and pairing menu are absent from actual main window";
    const QString productWindowTitle = window.windowTitle();
    window.setWindowTitle(QStringLiteral("WizNotePlus — 隔离主窗口测试"));
    window.resize(1000, 650);
    window.show();
    // This fixture checks real caret focus. A shell-launched Cocoa application
    // may remain inactive; activate this isolated test process, not another app.
    [NSApp activateIgnoringOtherApps:YES];
    window.raise();
    window.activateWindow();
    window.init();
    WIZDOCUMENTDATAEX extended;
    extended = note;
    // The one-argument overload creates a tab; the older two-argument overload
    // requires a pre-existing current document view and is not a startup path.
    window.viewDocument(extended);
    NSWindow* native = [(NSView*)window.winId() window];
    int failures = 0;
    int recordedChecks = 1; // The isolated bridge-absence check above also passed.
    auto check = [&](bool ok, const char* label) {
        ++recordedChecks;
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        if (!ok) qInfo() << "Window state at failure" << window.windowState()
            << "native fullscreen" << bool(native.styleMask & NSWindowStyleMaskFullScreen)
            << "native minimized" << native.miniaturized << "visible" << window.isVisible()
            << "modal" << QApplication::activeModalWidget()
            << "popup" << QApplication::activePopupWidget()
            << "native key window" << native.keyWindow << "app active" << NSApp.active;
        failures += !ok;
    };
    check(productWindowTitle == WizBuildIdentity::displayName(), "main-window product title matches trial branding");
    check(qtChineseLoaded && QCoreApplication::translate("MAC_APPLICATION_MENU", "Preferences...") == QStringLiteral("首选项..."),
          "packaged Qt base catalog translates the native macOS settings role into Chinese");
    bool nativeSettingsChinese = false;
    NSMenu* applicationMenu = NSApp.mainMenu.numberOfItems > 0 ? [[NSApp.mainMenu itemAtIndex:0] submenu] : nil;
    if (applicationMenu) {
        for (NSMenuItem* item in applicationMenu.itemArray)
            if ([item.title containsString:@"首选项"] || [item.title containsString:@"设置"])
                nativeSettingsChinese = true;
    }
    check(nativeSettingsChinese, "actual native application menu contains a Chinese settings entry");
    auto* styleAction = window.findChild<QAction*>("actionNoteStyle");
    auto* aboutAction = window.findChild<QAction*>("actionAbout");
    check(styleAction && styleAction->text() == QStringLiteral("笔记样式（CSS）…")
        && aboutAction && aboutAction->text() == QStringLiteral("关于 WizNotePlus…"),
        "style and About menu names are consistent with their dialogs");
    if (!WizBuildIdentity::groupAdministrationEnabled) {
        auto* category = dynamic_cast<WizCategoryView*>(&window.category());
        check(category != nullptr, "personal-use build retains the original category and group-note navigation");
        if (category) {
            bool noCreateLink = true;
            for (int i = 0; i < category->topLevelItemCount(); ++i)
                noCreateLink &= !dynamic_cast<WizCategoryViewCreateGroupLinkItem*>(category->topLevelItem(i));
            check(noCreateLink, "personal-use build removes the legacy create-team promotion");
            for (const char* name : {"wiz-group-information", "wiz-group-administration", "wiz-team-administration"}) {
                auto* action = category->findChild<QAction*>(QString::fromLatin1(name));
                check(action && !action->isVisible(), name);
            }
            auto* originalCategory = category->currentItem();
            WIZBIZDATA biz;
            biz.bizGUID = QStringLiteral("synthetic-team-admin-guard");
            biz.bizName = QStringLiteral("合成团队（非真实账号）");
            biz.bizUserRole = WIZ_BIZROLE_OWNER;
            auto* teamRoot = dynamic_cast<WizCategoryViewBizGroupRootItem*>(category->findBizGroupsRootItem(biz));
            auto* ownRoot = category->findOwnGroupsRootItem();
            WIZGROUPDATA group;
            group.strGroupGUID = QStringLiteral("synthetic-existing-group");
            group.strGroupName = QStringLiteral("既有群组导航（合成）");
            group.bOwn = true;
            auto* groupItem = new WizCategoryViewGroupRootItem(window, group);
            ownRoot->addChild(groupItem);
            category->setCurrentItem(teamRoot);
            check(teamRoot && !teamRoot->isExtraButtonUseable(), "legacy team billing button is absent without disabling note navigation");
            const int widgetsBefore = QApplication::allWidgets().size();
            category->createGroup();
            category->viewPersonalGroupInfo(group.strGroupGUID);
            category->viewBizGroupInfo(group.strGroupGUID, biz.bizGUID);
            category->managePersonalGroup(group.strGroupGUID);
            category->manageBizGroup(group.strGroupGUID, biz.bizGUID);
            category->viewBizInfo(biz.bizGUID);
            category->manageBiz(biz.bizGUID, true);
            const bool invoked = QMetaObject::invokeMethod(category, "on_action_manageBiz", Qt::DirectConnection);
            category->showAdminBizGroupRootContextMenu(QPoint(1, 1));
            category->showNormalBizGroupRootContextMenu(QPoint(1, 1));
            check(invoked && QApplication::allWidgets().size() == widgetsBefore && !QApplication::activePopupWidget(),
                  "direct legacy administration and reported menu slot do not create a token web dialog or popup");
            check(category->findBizGroupsRootItem(biz, false) == teamRoot
                    && category->findGroup(group.strGroupGUID) == groupItem
                    && groupItem->text(0) == group.strGroupName,
                  "administration removal preserves existing group branches and identifiers");
            // Initially no category may be selected while the first note loads.
            // Restore personal navigation before removing fixture-only branches;
            // Qt otherwise selects their neighbor and opens a synthetic group DB.
            category->setCurrentItem(originalCategory ? originalCategory : category->findAllFolderItem());
            delete category->takeTopLevelItem(category->indexOfTopLevelItem(teamRoot));
            delete category->takeTopLevelItem(category->indexOfTopLevelItem(ownRoot));
        }
    }
    auto* avatarAccount = window.findChild<WizUserInfoWidget*>("wiz-header-account");
    auto* avatarRefresh = avatarAccount && avatarAccount->menu()
        ? avatarAccount->menu()->findChild<QAction*>("wiz-refresh-avatar-action") : nullptr;
    check(avatarAccount && !avatarRefresh, "account menu no longer exposes manual avatar refresh");
    if (avatarAccount) avatarAccount->refreshAvatar();
    check(avatarAccount && avatarAccount->toolTip().contains(QStringLiteral("GUID")),
          "automatic avatar refresh without a GUID reports status without a popup or request");
    if (avatarAccount) {
        Q_EMIT avatar.loadFailed(manager.db().getUserId(), QStringLiteral("synthetic avatar failure"));
        const bool visibleError = avatarAccount->toolTip() == QStringLiteral("synthetic avatar failure");
        Q_EMIT avatar.loaded(manager.db().getUserId());
        check(visibleError && avatarAccount->toolTip().isEmpty(), "avatar failure status is displayed and cleared on a later load");
    }
    auto* tabs = window.mainTabView();
    auto* originalPage = tabs ? tabs->currentWidget() : nullptr;
    QList<QPointer<TabCloseFixturePage>> tabFixtures;
    if (tabs) for (int i = 0; i < 8; ++i) {
        auto* page = new TabCloseFixturePage(QStringLiteral("多笔记关闭测试 %1 长标题").arg(i));
        tabFixtures.append(page); tabs->createTab(page);
    }
    const auto tabActionsMatchPointer = [&] {
        if (!tabs) return false;
        const QPoint pos = tabs->tabBar()->mapFromGlobal(QCursor::pos());
        const int hovered = tabs->tabBar()->rect().contains(pos) ? tabs->tabBar()->tabAt(pos) : -1;
        for (int i = 0; i < tabs->count(); ++i) {
            auto* button = qobject_cast<TabButton*>(tabs->tabBar()->tabButton(i, QTabBar::RightSide));
            if (!button || button->isHidden() || button->size().width() < 16 || button->icon().isNull()) return false;
            if (button->isAutoHidden() != (i != hovered && !tabs->isTabLocked(i))) return false;
        }
        return true;
    };
    const auto hoverAt = [&](const QPoint& point) {
        QCursor::setPos(tabs->tabBar()->mapToGlobal(point));
        QMouseEvent move(QEvent::MouseMove, QPointF(point), QPointF(QCursor::pos()),
                         Qt::NoButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(tabs->tabBar(), &move);
        app.processEvents(); app.processEvents();
    };
    app.processEvents();
    if (tabs) hoverAt(QPoint(-10, -10));
    check(tabFixtures.size() == 8 && tabActionsMatchPointer(), "multiple long-title tabs auto-hide close glyphs without losing layout slots");
    if (tabs) hoverAt(tabs->tabBar()->tabRect(tabs->currentIndex()).center());
    check(tabActionsMatchPointer(), "hover reveals only the pointed tab's close action");
    if (tabs) {
        auto* button = tabs->tabBar()->tabButton(tabs->currentIndex(), QTabBar::RightSide);
        hoverAt(button->geometry().center());
        check(tabActionsMatchPointer(), "pointer over the close child keeps its action visible");
        hoverAt(QPoint(-10, -10));
    }
    check(tabActionsMatchPointer(), "leaving the bar auto-hides close actions again");
    if (tabs) {
        for (int i = tabs->count() - 1; i >= 0; --i) tabs->setCurrentIndex(i);
        tabs->tabBar()->moveTab(tabs->indexOf(tabFixtures[0]), tabs->count() - 1);
        app.processEvents();
    }
    check(tabActionsMatchPointer(), "switching and reordering recalculate hover state");
    if (tabs) {
        const int locked = tabs->indexOf(tabFixtures[0]);
        tabs->lockTab(locked); tabs->closeTab(locked); app.processEvents();
        check(tabs->indexOf(tabFixtures[0]) >= 0 && tabs->isTabLocked(tabs->indexOf(tabFixtures[0]))
              && tabActionsMatchPointer(), "visible lock action still prevents closing a locked tab");
        tabs->unlockTab(tabs->indexOf(tabFixtures[0]));
        const int index = tabs->indexOf(tabFixtures[2]);
        tabs->setCurrentIndex(index); app.processEvents();
        hoverAt(tabs->tabBar()->tabRect(index).center());
        auto* close = qobject_cast<QAbstractButton*>(tabs->tabBar()->tabButton(index, QTabBar::RightSide));
        if (close) close->click();
        check(tabs->indexOf(tabFixtures[2]) == -1 && tabs->indexOf(tabFixtures[1]) >= 0
              && tabActionsMatchPointer(), "a reordered close button closes only its own page and refreshes neighbor hover");
        for (const auto& page : tabFixtures) if (page) tabs->closeTab(tabs->indexOf(page));
        if (originalPage) tabs->setCurrentWidget(originalPage);
        hoverAt(QPoint(-10, -10));
        app.processEvents();
        // Restore settled main-window geometry after the long-title fixture
        // temporarily expanded the splitter's minimum width.
        window.resize(1000, 650);
        app.processEvents();
        QMetaObject::invokeMethod(&window, "adjustToolBarLayout", Qt::DirectConnection);
        app.processEvents();
    }
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
    bool collectHeaderSteps = true;
    auto addCheck = [&](const char* label, std::function<bool()> complete,
                        std::function<void()> begin = {}, int timeout = 8000) {
        if (headerOnly && !collectHeaderSteps) return;
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
                "!!(window.WizNativeEditorInitialized && window.WizEditor && typeof window.WizEditor.focus === 'function')",
                [editorReady, queryPending](const QVariant& result) {
                    *editorReady = result.toBool(); *queryPending = false;
                });
        }
        return *editorReady;
    }, {}, 15000);
    collectHeaderSteps = false;
    const QString artifactDir = qEnvironmentVariable("WIZNOTE_TEST_ARTIFACT_DIR");
    if (!interactive) {
        const auto spellingOn = std::make_shared<bool>(false);
        const auto spellingOff = std::make_shared<bool>(false);
        addCheck("main-client preference reaches browser profile and original prose editor", [&, spellingOn, queryPending] {
            if (!*spellingOn && !*queryPending && window.docView()) {
                *queryPending = true;
                auto* web = window.docView()->web();
                const bool profileEnabled = web->page()->profile()->isSpellCheckEnabled();
                web->page()->runJavaScript("!!window.WizSpellCheckEnabled && !!document.querySelector('.wiz-editor-body')"
                    "&& document.querySelector('.wiz-editor-body').spellcheck", [spellingOn, queryPending, profileEnabled](const QVariant& v) {
                        *spellingOn = profileEnabled && v.toBool(); *queryPending = false;
                    });
            }
            return *spellingOn;
        }, [&] {
            settings.setEnableSpellCheck(true);
            if (window.docView()) window.docView()->setEditorMode(modeEditor);
            QMetaObject::invokeMethod(&window, "on_options_settingsChanged", Qt::DirectConnection,
                Q_ARG(WizOptionsType, wizoptionsSpellCheck));
        });
        addCheck("main-client preference switches spelling off in an already open note", [&, spellingOff, queryPending] {
            if (!*spellingOff && !*queryPending && window.docView()) {
                *queryPending = true;
                auto* web = window.docView()->web();
                const bool profileDisabled = !web->page()->profile()->isSpellCheckEnabled();
                web->page()->runJavaScript("!window.WizSpellCheckEnabled && !document.querySelector('.wiz-editor-body').spellcheck",
                    [spellingOff, queryPending, profileDisabled](const QVariant& v) {
                        *spellingOff = profileDisabled && v.toBool(); *queryPending = false;
                    });
            }
            return *spellingOff;
        }, [&] {
            settings.setEnableSpellCheck(false);
            QMetaObject::invokeMethod(&window, "on_options_settingsChanged", Qt::DirectConnection,
                Q_ARG(WizOptionsType, wizoptionsSpellCheck));
        });
    }
    // Extra synthetic lifecycle checks are explicit opt-in so the already
    // delivered candidate's 93-check launcher contract remains unchanged.
    if (!interactive && qEnvironmentVariableIntValue("WIZNOTE_TEST_LINK_LIFETIME") == 1) {
        struct LinkState {
            QPointer<WizEditorInsertLinkForm> dialog;
            std::unique_ptr<WizDocumentView> closing;
            QString expected;
            bool ready=false, pending=false, acted=false;
        };
        const auto link=std::make_shared<LinkState>();
        const auto linkResult=[&,link](const QString& script) {
            if (!link->ready&&!link->pending) {
                link->pending=true;
                window.docView()->web()->page()->runJavaScript(script,[link](const QVariant& value) {
                    link->ready=value.toBool();link->pending=false;
                });
            }
            return link->ready;
        };
        addCheck("actual link dialog belongs to its editor and remains application modal without nested exec", [&,link] {
            auto* web=window.docView()->web();
            link->dialog=web->findChild<WizEditorInsertLinkForm*>();
            return link->dialog&&link->dialog->isVisible()&&link->dialog->parentWidget()==web
                &&link->dialog->windowModality()==Qt::ApplicationModal&&link->dialog->getUrl()=="http://";
        }, [&] {
            auto* web=window.docView()->web();
            web->page()->runJavaScript("window.NativeOriginalLink=WizEditor.link;window.NativeLinkCalls=[];window.NativeLinkReads=0;"
                "WizEditor.link={getCurrentLink:()=>{++NativeLinkReads;return '';},"
                "setCurrentLink:url=>NativeLinkCalls.push(url),removeSelectedLink:()=>NativeLinkCalls.push('removed')};",
                [web](const QVariant&) {web->editorCommandExecuteLinkInsert();web->editorCommandExecuteLinkInsert();});
        });
        addCheck("repeated insert-link action reuses its visible dialog without another lookup", [&,link,linkResult] {
            return link->dialog&&link->dialog->isVisible()
                &&window.docView()->web()->findChildren<WizEditorInsertLinkForm*>().size()==1
                &&linkResult("NativeLinkReads===2");
        }, [&,link] {link->ready=false;window.docView()->web()->editorCommandExecuteLinkInsert();});
        addCheck("actual link dialog transports quotes and Unicode and cannot submit twice", [&,link,linkResult] {
            return link->dialog&&!link->dialog->isVisible()
                &&linkResult("NativeLinkCalls.length===1&&NativeLinkCalls[0]==="+WizNotePresentation::jsonString(link->expected));
        }, [&,link] {
            link->ready=false;
            link->dialog->setUrl(QStringLiteral("https://example.test/a'b?q=中文"));
            link->expected=QUrl(link->dialog->getUrl()).toString();
            link->dialog->accept();
            window.docView()->web()->on_editorCommandExecuteLinkInsert_accepted();
        });
        addCheck("canceling the actual link dialog invalidates later accepted delivery", [&,link,linkResult] {
            if (!link->acted&&link->dialog&&link->dialog->isVisible()) {
                link->acted=true;link->dialog->setUrl("https://example.test/canceled");link->dialog->reject();
                window.docView()->web()->on_editorCommandExecuteLinkInsert_accepted();
            }
            return link->acted&&linkResult("NativeLinkCalls.length===1");
        }, [&,link] {link->ready=false;link->acted=false;window.docView()->web()->editorCommandExecuteLinkInsert();});
        addCheck("switching to reader cancels a pending actual link query", [&,link,linkResult] {
            return !window.docView()->web()->isEditing()&&link->dialog&&!link->dialog->isVisible()
                &&linkResult("NativeLinkCalls.length===1");
        }, [&,link] {
            link->ready=false;
            window.docView()->web()->editorCommandExecuteLinkInsert();
            window.docView()->setEditorMode(modeReader);
        });
        addCheck("actual link fixture returns to the original editable engine", [&,link,linkResult] {
            return window.docView()->web()->isEditing()&&linkResult(
                "document.body.isContentEditable&&!!window.NativeOriginalLink");
        }, [&,link] {link->ready=false;window.docView()->setEditorMode(modeEditor);});
        addCheck("separate real document view loads a synthetic note before close-during-link test", [&,link] {
            if (!link->closing||!link->closing->noteLoaded()||!link->closing->web()->isEditing()) return false;
            if (!link->ready&&!link->pending) {
                link->pending=true;
                link->closing->web()->page()->runJavaScript("!!window.WizNativeEditorInitialized&&!!WizEditor.link",[link](const QVariant& v) {
                    link->ready=v.toBool();link->pending=false;
                });
            }
            return link->ready;
        }, [&,link] {
            link->ready=false;
            link->closing=std::make_unique<WizDocumentView>(window,&window);
            // A bare document widget does not wire the main window's loader.
            // Use the same real loader and queued delivery as createDocumentView.
            if (auto* loader=window.findChild<WizDocumentLoaderThread*>()) {
                auto* web=link->closing->web();
                QObject::connect(web,&WizDocumentWebView::loadDocumentRequested,loader,&WizDocumentLoaderThread::load);
                QObject::connect(loader,&WizDocumentLoaderThread::loaded,web,&WizDocumentWebView::onDocumentReady,Qt::QueuedConnection);
            }
            link->closing->viewNote(extended,true);
        },15000);
        addCheck("closing actual document during pending link lookup destroys its editor and dialog safely", [&,link,linkResult] {
            return link->acted&&linkResult("true");
        }, [&,link] {
            link->ready=false;link->acted=false;
            const QPointer<WizDocumentWebView> closingWeb=link->closing->web();
            closingWeb->editorCommandExecuteLinkInsert();
            const QPointer<WizEditorInsertLinkForm> closingDialog=closingWeb->findChild<WizEditorInsertLinkForm*>();
            const bool owned=closingDialog&&closingDialog->parentWidget()==closingWeb;
            link->closing->RequestClose(); // Normal close still confirms the local snapshot.
            link->closing.reset();
            link->acted=owned&&closingWeb.isNull()&&closingDialog.isNull();
        });
        addCheck("link lifecycle fixture restores original link methods without changing note contents", [&,link,linkResult] {
            return linkResult("WizEditor.link===NativeOriginalLink&&typeof WizEditor.link.getCurrentLink==='function'");
        }, [&,link] {
            link->ready=false;
            window.docView()->web()->page()->runJavaScript("WizEditor.link=NativeOriginalLink;");
        });
    }
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
            if ((edit->palette().color(QPalette::Base).alpha() != 0
                 && edit->palette().color(QPalette::Base).lightnessF() < 0.8)
                || edit->palette().color(QPalette::Text).lightnessF() > 0.4) return false;
        }
        capture(&window, "v11-main-light.png");
        return true;
    });
    addCheck("refined search field keeps readable height and reserved icon spacing", [&] {
        auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
        if (!search || search->height() != 24 || search->textMargins().left() < 32
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
        if (!search || !newNote) return false;
        const int searchY = search->mapTo(&window, search->rect().center()).y();
        const int newNoteY = newNote->mapTo(&window, newNote->rect().center()).y();
        if (qAbs(searchY - newNoteY) > 1)
            qInfo() << "Header center mismatch" << searchY << newNoteY << search->geometry() << newNote->geometry();
        return qAbs(searchY - newNoteY) <= 1;
    };
    addCheck("search and New Note centers align without filters", searchAligned);
    addCheck("compact neutral header aligns account, sync, search and New Note", [&] {
        auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
        auto* toolbar = window.findChild<QToolBar*>("wiz-main-toolbar");
        if (!search || !toolbar || toolbar->height() != 34 || window.titleBar()->height() != 44) return false;
        const int center = search->mapTo(&window, search->rect().center()).y();
        for (const QString& name : {QStringLiteral("wiz-header-account"), QStringLiteral("wiz-header-sync"), QStringLiteral("btn-newnote")}) {
            auto* control = window.findChild<QWidget*>(name);
            if (!control || qAbs(control->mapTo(&window, control->rect().center()).y() - center) > 1) return false;
        }
        capture(window.titleBar(), "v144rc7-header.png");
        return true;
    });
    addCheck("compact search view uses 260 logical pixels", [&] {
        auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
        return view && view->width() == 260;
    });
    addCheck("search stays before New Note and near note-list left edge without covering sync", [&] {
        auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
        auto* sync = window.findChild<QWidget*>("wiz-header-sync");
        auto* list = window.findChild<WizDocumentListView*>();
        auto* note = window.findChild<QToolButton*>("btn-newnote");
        auto* spacer = window.findChild<QWidget*>("wiz-header-search-alignment");
        if (!view || !sync || !list || !note || !spacer) return false;
        const int left = view->mapTo(&window, QPoint()).x();
        const int target = list->mapTo(&window, QPoint()).x();
        return left > sync->mapTo(&window, sync->rect().topRight()).x()
            && note->mapTo(&window, QPoint()).x() > left + view->width()
            && (qAbs(left - target) <= 3 || (spacer->width() == 0 && left > target));
    });
    addCheck("focused search interior uses the same header background", [&] {
        auto* edit = window.findChild<WizSearchEdit*>("wiz-note-search-field");
        if (!edit) return false;
        edit->clear(); edit->setFocus();
        const QImage image = window.titleBar()->grab().toImage();
        const qreal scale = image.devicePixelRatio();
        const QPoint inside = edit->mapTo(window.titleBar(), QPoint(edit->width() - 45, edit->height() / 2));
        const QPoint outside(window.titleBar()->width() - 15, inside.y());
        const QColor a = image.pixelColor(qRound(inside.x() * scale), qRound(inside.y() * scale));
        const QColor b = image.pixelColor(qRound(outside.x() * scale), qRound(outside.y() * scale));
        return a == b && a.lightnessF() > 0.8;
    });
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    addCheck("native title bar uses Aqua after simulated dark appearance", [&] {
        return [[native.effectiveAppearance bestMatchFromAppearancesWithNames:
                 @[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]] isEqualToString:NSAppearanceNameAqua];
    });
#endif
    if (!interactive) {
        auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
        auto* menu = search ? search->findChild<QMenu*>("wiz-note-search-menu") : nullptr;
        auto* filterPopup = search ? search->findChild<QWidget*>("wiz-note-filter-popup") : nullptr;
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
        const auto chooseAdvanced = [filterPopup, sendClick] {
            if (filterPopup)
                QTimer::singleShot(0, filterPopup, [filterPopup, sendClick] {
                    auto* button = filterPopup->findChild<QPushButton*>("actionAdvancedSearchButton");
                    if (button) sendClick(button, button->rect().center());
                });
        };
        addCheck("magnifier opens an inline personal filter panel without a modal dialog or extra toolbar button", [&, menu, filterPopup] {
            auto* panel = window.findChild<QWidget*>("wiz-note-filter-panel");
            auto* advanced = menu ? menu->findChild<QAction*>("actionAdvancedSearch") : nullptr;
            if (!filterPopup || !filterPopup->isVisible() || !panel || !panel->isVisible() || !advanced
                || QApplication::activeModalWidget()) return false;
            for (auto* tool : window.findChildren<QToolButton*>())
                if (tool->defaultAction() == advanced && tool->isVisible()) return false;
            capture(filterPopup, "v9-filter-panel.png");
            return true;
        }, clickMagnifier);
        addCheck("filter is an IME-capable key tool window instead of a native menu", [&, filterPopup] {
            auto* field = window.findChild<QLineEdit*>("wiz-note-panel-tag-filter");
            NSWindow* tool = filterPopup ? [(NSView*)filterPopup->winId() window] : nil;
            return field && field->hasFocus() && field->testAttribute(Qt::WA_InputMethodEnabled)
                && filterPopup && filterPopup->windowType() == Qt::Tool && tool.canBecomeKeyWindow && tool.keyWindow;
        // Real foreground activation may require a native user click on newer
        // macOS. Allow that precondition without bypassing actual key/focus.
        }, {}, 30000);
        addCheck("tag-filter pinyin preedit stays uncommitted without changing the note query", [&] {
            auto* field = window.findChild<QLineEdit*>("wiz-note-panel-tag-filter");
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            return field && field->hasFocus() && field->text().isEmpty()
                && field->property("wiz-ime-composing").toBool() && view && !view->hasFilters();
        }, [&] {
            auto* field = window.findChild<QLineEdit*>("wiz-note-panel-tag-filter");
            if (field) {
                QInputMethodEvent input(QStringLiteral("ceshi"), {});
                QCoreApplication::sendEvent(field, &input);
            }
        });
        addCheck("Chinese IME commit filters tag names without changing content keywords or selecting tags", [&, filterPopup] {
            auto* field = window.findChild<QLineEdit*>("wiz-note-panel-tag-filter");
            auto* tags = window.findChild<QListWidget*>("wiz-note-panel-tags");
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            if (!field || field->text() != QStringLiteral("测试") || !field->hasFocus()
                || field->property("wiz-ime-composing").toBool() || !tags || !view || view->hasFilters() || !view->keyword().isEmpty()) return false;
            int visible = 0;
            for (int i = 0; i < tags->count(); ++i) if (!tags->item(i)->isHidden()) {
                ++visible;
                if (tags->item(i)->data(Qt::UserRole).toString() != tag.strGUID) return false;
            }
            capture(filterPopup, "v14416-Chinese-tag-filter.png");
            return visible == 1;
        }, [&] {
            if (auto* field = window.findChild<QLineEdit*>("wiz-note-panel-tag-filter")) {
                QInputMethodEvent input;
                input.setCommitString(QStringLiteral("测试"));
                QCoreApplication::sendEvent(field, &input);
            }
        });
        addCheck("canceling a second preedit preserves committed Chinese and clearing restores all tags", [&, filterPopup] {
            auto* field = window.findChild<QLineEdit*>("wiz-note-panel-tag-filter");
            auto* tags = window.findChild<QListWidget*>("wiz-note-panel-tags");
            if (!field || !tags || field->text() != QStringLiteral("测试")) return false;
            QInputMethodEvent preedit(QStringLiteral("x"), {}), canceled;
            QCoreApplication::sendEvent(field, &preedit);
            QCoreApplication::sendEvent(field, &canceled);
            const bool retained = field->text() == QStringLiteral("测试") && !field->property("wiz-ime-composing").toBool();
            field->clear();
            for (int i = 0; i < tags->count(); ++i) if (tags->item(i)->isHidden()) return false;
            return retained && filterPopup && filterPopup->isVisible();
        });
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
        }, [&, selectPanelTag] {
            // Direct synthetic clicks do not activate a shell-launched Cocoa
            // process as a real foreground mouse click would. Activate before
            // the click; keep the subsequent real caret-focus assertion intact.
            [[NSRunningApplication currentApplication] activateWithOptions:NSApplicationActivateIgnoringOtherApps];
            [native makeKeyAndOrderFront:nil];
            window.activateWindow();
            QTimer::singleShot(100, &window, [&, selectPanelTag] { selectPanelTag(tag.strGUID); });
        });
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
        addCheck("folded conditions reopen the panel without changing the combined search", [&, filterPopup] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* attachment = window.findChild<QCheckBox*>("wiz-note-panel-attachments");
            return filterPopup && filterPopup->isVisible() && view && view->queryParams().contains(tag.strGUID)
                && view->queryParams().contains(secondTag.strGUID) && attachment && attachment->isChecked();
        }, [&, sendClick] {
            auto* more = window.findChild<QPushButton*>("wiz-note-more-filters");
            if (more && more->isVisible()) sendClick(more, more->rect().center());
        });
        const auto clickChip = [&, sendClick](const QString& kind, const QString& id = QString()) {
            for (auto* chip : window.findChildren<QPushButton*>("wiz-note-filter-chip"))
                if (chip->isVisible() && chip->property("filterKind").toString() == kind
                    && (id.isEmpty() || chip->property("filterId").toString() == id)) {
                    sendClick(chip, chip->rect().center()); return true;
                }
            return false;
        };
        addCheck("removing a visible or folded tag retains all other search conditions", [&] {
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            auto* attachment = window.findChild<QCheckBox*>("wiz-note-panel-attachments");
            return view && !view->queryParams().contains(tag.strGUID) && view->queryParams().contains(secondTag.strGUID)
                && view->queryParams().contains("folder-path-encoded:") && attachment && attachment->isChecked()
                && docs && docs->documentCount() == 0 && !QApplication::activeModalWidget();
        }, [&, filterPopup, clickChip, clickMagnifier] {
            if (filterPopup) filterPopup->hide();
            if (clickChip("tag", tag.strGUID)) return;
            // At 260 pixels multiple filters can all fold. Remove via the same
            // user-facing panel; keep the exact query/result assertions above.
            clickMagnifier();
            if (auto* tags = window.findChild<QListWidget*>("wiz-note-panel-tags"))
                for (int i = 0; i < tags->count(); ++i)
                    if (tags->item(i)->data(Qt::UserRole).toString() == tag.strGUID)
                        tags->item(i)->setCheckState(Qt::Unchecked);
            if (filterPopup) filterPopup->hide();
        });
        addCheck("removing a folded attachment condition restores matching notes without losing tags or folder", [&] {
            auto* attachment = window.findChild<QCheckBox*>("wiz-note-panel-attachments");
            auto* view = window.findChild<WizSearchView*>("wiz-note-search-view");
            auto* docs = window.findChild<WizDocumentListView*>();
            return attachment && !attachment->isChecked() && view && !view->queryParams().contains(tag.strGUID)
                && view->queryParams().contains(secondTag.strGUID) && view->queryParams().contains("folder-path-encoded:")
                && docs && docs->documentCount() == 2
                && docs->documentIndexFromGUID(note.strGUID) >= 0
                && docs->documentIndexFromGUID(unrelated.strGUID) >= 0
                && docs->documentIndexFromGUID(elsewhere.strGUID) < 0
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
        }, [&, search] {
            // Synthetic menu clicks do not model Cocoa's native activation on
            // return from a popup. Restore the foreground before typing; retain
            // the real focus/suggestion assertions rather than weakening them.
            [[NSRunningApplication currentApplication] activateWithOptions:NSApplicationActivateIgnoringOtherApps];
            [native makeKeyAndOrderFront:nil];
            window.activateWindow();
            QTimer::singleShot(100, &window, [search] {
                if (!search) return;
                search->setFocus();
                QInputMethodEvent commit; commit.setCommitString(QStringLiteral("测试标签"));
                QCoreApplication::sendEvent(search, &commit);
            });
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
        }, [clickMagnifier, filterPopup, sendClick] {
            clickMagnifier();
            if (filterPopup) QTimer::singleShot(0, filterPopup, [filterPopup, sendClick] {
                auto* save = filterPopup->findChild<QPushButton*>("actionSaveSearchButton");
                if (save) sendClick(save, save->rect().center());
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
        const auto fontEditorReady = std::make_shared<bool>(false);
        addCheck("rich-text editing mode is ready before selecting text for font-size action", [&, fontEditorReady] {
            if (!*fontEditorReady && !*queryPending) {
                *queryPending = true;
                window.docView()->web()->page()->runJavaScript(
                    "!!(window.WizNativeEditorInitialized&&document.querySelector('.wiz-editor-body')"
                    "&&document.querySelector('.wiz-editor-body').isContentEditable)",
                    [=](const QVariant& value) { *fontEditorReady = value.toBool(); *queryPending = false; });
            }
            return *fontEditorReady;
        }, [&] {
            window.docView()->web()->setEditorMode(modeEditor);
            window.docView()->setEditorMode(modeEditor);
        });
        const auto fontApplied = std::make_shared<bool>(false);
        const auto fontDiagnostic = std::make_shared<QString>();
        addCheck("actual rich-text font-size signal changes selected text and retains the dirty baseline", [&, fontApplied, fontDiagnostic] {
            if (!*fontApplied && !*queryPending) {
                *queryPending = true;
                window.docView()->web()->page()->runJavaScript(
                    "(function(){var p=document.querySelector('.wiz-editor-body p');if(!p)return {};"
                    "var sizes=Array.from(p.querySelectorAll('*')).concat([p]).map(function(e){return getComputedStyle(e).fontSize;});"
                    "var html=WizEditor.getContentHtml(),dirty=WizEditor.isModified();"
                    // The original editor normalizes 24pt to 2rem at this
                    // fixture's 16px root. Verify equivalent persisted sizing,
                    // actual 32px rendering and a genuinely dirty source.
                    "return {ok:sizes.indexOf('32px')>=0&&/font-size:\\s*(?:24pt|32px|2rem)\\b/.test(html)&&dirty,"
                    "sizes:sizes,dirty:dirty,html:p.outerHTML};})()",
                    [=](const QVariant& value) {
                        const auto result = value.toMap(); *fontApplied = result.value("ok").toBool(); *queryPending = false;
                        const QString summary = result.value("html").toString();
                        if (!*fontApplied && *fontDiagnostic != summary) {
                            qInfo() << "Synthetic font-size diagnostic" << result; *fontDiagnostic = summary;
                        }
                    });
            }
            return *fontApplied;
        }, [&] {
            auto* web = window.docView()->web();
            web->setModified(false);
            web->page()->runJavaScript(
                "(function(){var p=document.querySelector('.wiz-editor-body p');if(!p)return false;"
                "document.querySelector('.wiz-editor-body').focus();"
                "var r=document.createRange();r.selectNodeContents(p);var s=getSelection();s.removeAllRanges();s.addRange(r);return true;})()",
                [&](const QVariant& selected) {
                    if (!selected.toBool()) return;
                    // Several open tabs own toolbars. Target this note's actual
                    // toolbar, not the first inactive toolbar under the window.
                    if (auto* combo = window.docView()->editorToolBar()->findChild<QComboBox*>("wiz-editor-font-size")) {
                        combo->setCurrentIndex(combo->findText("24"));
                        QMetaObject::invokeMethod(combo, "textActivated", Qt::DirectConnection, Q_ARG(QString, QStringLiteral("24")));
                    }
                });
        });
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
        const auto wasEditing = std::make_shared<bool>(false);
        addCheck("original Save as PDF opens its save dialog without print preview", [&, pdfVisited, wasEditing] {
            auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
            if (dialog) {
                if (window.docView()->isEditing() != *wasEditing) { dialog->reject(); return false; }
                *pdfVisited = true; QTimer::singleShot(0, dialog, &QDialog::reject);
            }
            return *pdfVisited && !QApplication::activeModalWidget() && !window.findChild<QAction*>("actionPrintNote");
        }, [&, wasEditing] {
            *wasEditing = window.docView()->isEditing();
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
    if (!interactive) {
        const auto ready = std::make_shared<bool>(false);
        const auto markdown = std::make_shared<QString>();
        const auto diagnostic = std::make_shared<QString>();
        const auto readerRequested = std::make_shared<bool>(false);
        addCheck("actual native Markdown note renders typed blocks and label reference", [&, ready, markdown, diagnostic, readerRequested] {
            if (!*ready && !*queryPending && window.docView() && window.docView()->noteLoaded()) {
                *queryPending = true;
                window.docView()->web()->page()->runJavaScript(
                    "(function(){if(!document.body||!window.WizEditor||typeof WizEditor.getMarkdownSrc!=='function')return {};"
                    "if(typeof WizApplyNoteStyle==='function')WizApplyNoteStyle();"
                    "return {ok:document.querySelectorAll('.wiz-math-block').length===2&&document.querySelectorAll('.wiz-math-reference').length===1,"
                    "bodyClass:document.body.className,css:!!window.WizNoteCss,blocks:Array.from(document.querySelectorAll('blockquote')).map(function(b){return b.textContent.slice(0,180);}),"
                    "source:window.WizEditor?WizEditor.getMarkdownSrc({unEscapeHtml:true}):''};})()",
                    [&, ready, markdown, diagnostic, readerRequested](const QVariant& value) {
                        const auto result = value.toMap(); *ready = result.value("ok").toBool();
                        const QString summary = result.value("bodyClass").toString() + QString::number(result.value("css").toBool());
                        if (*diagnostic != summary) { qInfo() << "Native math readiness diagnostic" << result; *diagnostic = summary; }
                        if (!*readerRequested && !result.value("source").toString().isEmpty()
                            && result.value("bodyClass").toString().contains("wiz-editing")) {
                            *readerRequested = true;
                            QTimer::singleShot(0, &window, [&] {
                                // A newly created Markdown note starts in source view.
                                // Initialize the production type-aware reader only
                                // after the asynchronous editor has actually started.
                                window.docView()->setEditorMode(modeReader);
                                window.docView()->web()->setEditorMode(modeReader);
                                window.docView()->web()->enableEditor(false);
                            });
                        }
                        if (*ready) *markdown = result.value("source").toString(); *queryPending = false;
                    });
            }
            return *ready && markdown->contains("\\label{thm:native}");
        }, [&] {
            QString error;
            WizNotePresentation::saveStyle(WizGlobal::settings(), {WizNotePresentation::scientificMathStyle(), true}, error);
            const QString source = QStringLiteral("# 原生数学样式测试\n\n见定理 \\ref{thm:native}。\n\n"
                "> **定理（测试）** \\label{thm:native}\n>\n> $x^2\\ge 0$。\n\n> **证明**\n>\n> 由实数性质可得。\n");
            QString body = source.toHtmlEscaped(); body.replace('\n', "<br>");
            WIZDOCUMENTDATA data;
            if (manager.db().createDocumentAndInit("<html><body>" + body + "</body></html>", "", 0,
                    QStringLiteral("原生数学样式.md"), "native-math.html", "/Window test/", "", data)) {
                WIZDOCUMENTDATAEX note; note = data; window.viewDocument(note);
            }
        }, 20000);
        const auto paintStarted = std::make_shared<QElapsedTimer>();
        const auto paintReady = std::make_shared<bool>(false);
        addCheck("native Markdown formula reader paints actual content after asynchronous readiness", [&, paintStarted, paintReady] {
            if (paintStarted->elapsed() < 250) return false;
            auto* web = window.docView()->web();
            if (!*paintReady && !*queryPending) {
                *queryPending = true;
                web->page()->runJavaScript(
                    "(function(){if(!window.WizNativeEditorSetup||WizNativeEditorSetup.state!=='ready'||!window.MathJax||!MathJax.Hub)return false;"
                    "if(!window.WizFixturePaintQueued){window.WizFixturePaintQueued=true;MathJax.Hub.Queue(function(){"
                    "requestAnimationFrame(()=>requestAnimationFrame(()=>window.WizFixturePaintReady=true));});}"
                    "return !!window.WizFixturePaintReady&&!document.body.isContentEditable&&MathJax.Hub.getAllJax().length===1"
                    "&&document.querySelectorAll('.wiz-math-block').length===2;})()",
                    [paintReady, queryPending](const QVariant& value) {*paintReady=value.toBool();*queryPending=false;});
            }
            if (!*paintReady || !web->isVisible()) return false;
            const QImage frame=web->grab().toImage().convertToFormat(QImage::Format_RGB32);
            int ink=0;
            for (int y=12;y<qMin(frame.height()-12,600);y+=3)
                for (int x=12;x<frame.width()-12;x+=3) {
                    const QRgb pixel=frame.pixel(x,y);
                    if(qRed(pixel)<110&&qGreen(pixel)<110&&qBlue(pixel)<110)++ink;
                }
            if(ink<120)return false;
            capture(&window, "main-math-reader-painted.png");
            return true;
        }, [paintStarted] {paintStarted->start();}, 10000);
        const auto editing = std::make_shared<bool>(false);
        const auto editDiagnostic = std::make_shared<QString>();
        addCheck("native Markdown editing retains original labels and contains no preview decoration", [&, editing, markdown, editDiagnostic] {
            if (!*editing && !*queryPending && window.docView()->isEditing()) {
                *queryPending = true;
                window.docView()->web()->page()->runJavaScript(
                    "({source:WizEditor.getMarkdownSrc({unEscapeHtml:true}),bodyClass:document.body.className,temporary:document.querySelectorAll('.wiz-math-block,.wiz-math-reference').length})",
                    [=](const QVariant& value) {
                        const auto result = value.toMap(); *editing = result.value("source").toString() == *markdown
                            && result.value("temporary").toInt() == 0; *queryPending = false;
                        const QString summary = result.value("bodyClass").toString() + QString::number(result.value("temporary").toInt());
                        if (*editDiagnostic != summary) { qInfo() << "Native math editing diagnostic" << result; *editDiagnostic = summary; }
                    });
            }
            return *editing;
        }, [&] { window.docView()->setEditorMode(modeEditor); }, 15000);
        const auto restored = std::make_shared<bool>(false);
        addCheck("native Markdown preview restores numbering after edit mode with unchanged source", [&, restored, markdown] {
            if (!*restored && !*queryPending && !window.docView()->isEditing()) {
                *queryPending = true;
                window.docView()->web()->page()->runJavaScript(
                    "({source:WizEditor.getMarkdownSrc({unEscapeHtml:true}),count:document.querySelectorAll('.wiz-math-block').length})",
                    [=](const QVariant& value) {
                        const auto result = value.toMap(); *restored = result.value("source").toString() == *markdown
                            && result.value("count").toInt() == 2; *queryPending = false;
                    });
            }
            return *restored;
        }, [&] { window.docView()->setEditorMode(modeReader); }, 15000);
    }
    collectHeaderSteps = true;
    addCheck("Qt native delegate is retained, not replaced by the legacy frameless proxy", [&] {
        return [native.delegate respondsToSelector:@selector(windowShouldClose:)]
            && ![NSStringFromClass([native.delegate class]) isEqualToString:@"NSWindowProxyDelegate"];
    }, [&] { if (window.docView()) window.docView()->setEditorFocus(); });
    addCheck("native title bar and client toolbar share one expanded header",
        [&] { return native.styleMask & NSWindowStyleMaskFullSizeContentView; });
    QString headerDiagnostic;
    const auto nativeHeaderAligned = [&] {
        auto* account = window.findChild<QWidget*>("wiz-header-account");
        auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
        if (!account || !search || window.titleBar()->height() != 44
            || native.titleVisibility != NSWindowTitleHidden) return false;
        for (NSWindowButton type : {NSWindowCloseButton, NSWindowMiniaturizeButton, NSWindowZoomButton}) {
            NSButton* control = button(type);
            const NSRect rect = [native.contentView convertRect:control.bounds fromView:control];
            const CGFloat center = native.contentView.flipped ? NSMidY(rect)
                : NSHeight(native.contentView.bounds) - NSMidY(rect);
            const int searchY=search->mapTo(&window, search->rect().center()).y();
            const int accountX=account->mapTo(&window, QPoint()).x();
            if (qAbs(center-searchY)>2 || NSMaxX(rect)+8>accountX) {
                const QString current=QString("button=%1 centerY=%2 searchY=%3 maxX=%4 accountX=%5 window=%6x%7")
                    .arg(int(type)).arg(center).arg(searchY).arg(NSMaxX(rect)).arg(accountX)
                    .arg(window.width()).arg(window.height());
                if (headerDiagnostic!=current) {qInfo().noquote()<<"Synthetic header geometry:"<<current;headerDiagnostic=current;}
                return false;
            }
        }
        return true;
    };
    const auto addSettledHeaderCheck = [&](const char* label) {
        struct Settled { QElapsedTimer timer; bool started = false; };
        const auto settled = std::make_shared<Settled>();
        addCheck(label, [nativeHeaderAligned, settled] {
            if (!nativeHeaderAligned()) { settled->started = false; return false; }
            if (!settled->started) { settled->timer.start(); settled->started = true; }
            // AppKit can relayout after show/state-change returns. A single
            // matching event-loop tick must not hide a delayed reset.
            return settled->timer.elapsed() >= 500;
        });
    };
    addSettledHeaderCheck("native traffic lights align with the single header and do not cover account or search");
    addCheck("unified toolbar controls remain reachable through native pointer hit testing", [&] {
        NSView* qtView = reinterpret_cast<NSView*>(window.winId());
        NSView* frame = native.contentView.superview;
        for (const QString& name : {QStringLiteral("wiz-header-account"), QStringLiteral("wiz-header-sync"),
                                   QStringLiteral("wiz-note-search-field"), QStringLiteral("btn-newnote")}) {
            auto* control = window.findChild<QWidget*>(name);
            if (!control) return false;
            const QPoint center = control->mapTo(&window, control->rect().center());
            const NSPoint local = NSMakePoint(center.x(), qtView.flipped ? center.y() : NSHeight(qtView.bounds) - center.y());
            NSView* target = [frame hitTest:[qtView convertPoint:local toView:frame]];
            if (!target || (target != qtView && ![target isDescendantOf:qtView])) return false;
        }
        return true;
    });
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
        addCheck("fullscreen content respects system safe-area margins", [&] {
            return window.contentsMargins().top() >= window.windowHandle()->safeAreaMargins().top();
        });
        addFullscreenCheck(false, "actual main-window green action exits fullscreen");
        addSettledHeaderCheck("unified header alignment survives fullscreen exit");
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
        addSettledHeaderCheck("unified header alignment survives red close and reopen");
        addCheck("yellow action still works after red close and reopen",
            [&] { return minimizeComplete(true); }, [&] { [button(NSWindowMiniaturizeButton) performClick:nil]; });
        addCheck("final native minimize restoration", restoreComplete, restoreWindow);
        addFullscreenCheck(true, "green action still works after red close and reopen");
        addCheck("red close exits fullscreen and hides the actual main window", [&] {
            return *exitedFullscreen > beforeFull && !window.isVisible() && !native.visible && fullscreenComplete(false);
        }, [&] { beforeFull = *exitedFullscreen; [button(NSWindowCloseButton) performClick:nil]; });
        addCheck("actual main window restores after fullscreen red close", reopened, reopen);
        addSettledHeaderCheck("unified header alignment survives fullscreen red close");
        const int reopenCycles = qBound(0, qEnvironmentVariableIntValue("WIZNOTE_TEST_HEADER_REOPEN_CYCLES"), 20);
        for (int cycle = 0; cycle < reopenCycles; ++cycle) {
            addCheck("repeated red-close cycle hides main window", [&] {
                return !window.isVisible() && !native.visible;
            }, [&, cycle] {
                qInfo() << "EXTRA HEADER REOPEN CYCLE" << cycle + 1;
                [button(NSWindowCloseButton) performClick:nil];
            });
            addCheck("repeated red-close cycle reopens main window", reopened, reopen);
            addSettledHeaderCheck("header remains aligned throughout repeated red-close settling");
        }
        collectHeaderSteps = false;
        addCheck("actual quit preflight confirms synthetic Markdown is saved without preview decoration", [&] {
            bool progressShown = false;
            QTimer watcher;
            QObject::connect(&watcher, &QTimer::timeout, &window, [&] {
                for (QWidget* widget : QApplication::topLevelWidgets())
                    if (qobject_cast<QProgressDialog*>(widget) && widget->isVisible()) progressShown = true;
            });
            watcher.start(1);
            const bool prepared = window.prepareForQuit();
            watcher.stop();
            if (!prepared || progressShown || !window.findChildren<QProgressDialog*>().isEmpty()) return false;
            QString path;
            if (!manager.db().documentToTempHtmlFile(window.docView()->note(), path)) return false;
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) return false;
            const QByteArray source = file.readAll();
            return source.contains("\\label{thm:native}")
                && !source.contains("class=\"wiz-math-block") && !source.contains("class=\"wiz-math-reference");
        });
    }
    QTimer test;
    QElapsedTimer deadline;
    size_t step = 0;
    bool started = false;
    QTimer headerTrace;
    QString lastHeaderTrace;
    if (headerOnly || qEnvironmentVariableIntValue("WIZNOTE_TEST_HEADER_TRACE") == 1) {
        headerTrace.setTimerType(Qt::PreciseTimer);
        QObject::connect(&headerTrace, &QTimer::timeout, &app, [&] {
            auto* search = window.findChild<WizSearchEdit*>("wiz-note-search-field");
            NSButton* close = button(NSWindowCloseButton);
            if (!search || !close || !native.contentView) return;
            NSView* container = close.superview;
            const NSRect rect = [native.contentView convertRect:close.bounds fromView:close];
            const CGFloat centerY = native.contentView.flipped ? NSMidY(rect)
                : NSHeight(native.contentView.bounds) - NSMidY(rect);
            const QString now = QString("step=%1 qt=%2 visible=%3/%4 active=%5 key=%6 headerH=%7 searchY=%8 closeY=%9 container=%10 parentH=%11")
                .arg(step).arg(int(window.windowState())).arg(window.isVisible()).arg(bool(native.visible))
                .arg(bool(NSApp.active)).arg(bool(native.keyWindow)).arg(window.titleBar()->height())
                .arg(search->mapTo(&window, search->rect().center()).y()).arg(centerY)
                .arg(QString::fromNSString(NSStringFromRect(container.frame)))
                .arg(NSHeight(container.superview.bounds));
            if (now != lastHeaderTrace) {
                qInfo().noquote() << "HEADER TRACE" << now;
                lastHeaderTrace = now;
            }
        });
        headerTrace.start(20);
    }
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
    bool originalCleanup = false;
    QObject::connect(&app, &QApplication::aboutToQuit, &window, [&] {
        // Main-window handler was connected first, so this sees the actual
        // completed cleanup dialog rather than a separately fabricated widget.
        auto* progress = window.findChild<QProgressDialog*>();
        originalCleanup = progress && progress->windowTitle() == "Cleaning on Quit"
            && progress->minimum() == 0 && progress->maximum() == 11
            && progress->minimumDuration() == 0 && progress->width() == 400;
    });
    app.exec();
    if (!interactive) check(originalCleanup, "original eleven-stage cleanup dialog remains and is configured to display immediately");
    window.hide();
    for (id observer in observers) [center removeObserver:observer];
    const QString resultPath = qEnvironmentVariable("WIZNOTE_TEST_WINDOW_RESULT");
    if (!resultPath.isEmpty()) {
        QFile result(resultPath);
        const QJsonDocument record(QJsonObject{{"synthetic", true}, {"headerOnly", headerOnly},
            {"interactive", interactive}, {"namedChecks", recordedChecks}, {"failures", failures},
            {"completedSteps", int(step)}, {"totalSteps", int(steps.size())},
            {"appActiveAtEnd", bool(NSApp.active)}, {"exitCode", failures ? 1 : 0}});
        if (!QFileInfo(resultPath).isAbsolute() || !result.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                || result.write(record.toJson()) < 0) {
            qWarning("FAIL cannot create new isolated window-result report");
            return 2;
        }
    }
    return failures ? 1 : 0;
}
