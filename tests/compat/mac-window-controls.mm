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
    app.setStyleSheet(WizLoadSkinStyleSheet(settings.skin()));
    app.setFont(QFont(settings.UIFontFamily(), settings.UIFontSize()));
    WizAvatarHost avatar;
    WizThumbCache cache;
    WIZDOCUMENTDATA note;
    if (!manager.db().createDocumentAndInit(
            "<html><body><h1>隔离窗口测试</h1><p>这不是您的账号或笔记。仅验证主窗口按钮。</p></body></html>",
            "", 0, QStringLiteral("窗口按钮测试笔记"), "synthetic.html", "/Window test/", "", note)) return 1;
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
            << "native minimized" << native.miniaturized << "visible" << window.isVisible();
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
            [&] { return !QApplication::activeModalWidget() && controlsEnabled(); }, [&] { modal.close(); });
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
