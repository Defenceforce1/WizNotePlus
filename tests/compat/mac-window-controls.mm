#include "share/WizFramelessWindow.h"
#include "mac/WizMacHelper.h"
#include <QMainWindow>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <functional>
#include <memory>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QDebug>
#include <QWebEngineView>
#include <QWebEnginePage>
#include <QWebEngineProfile>
#import <Cocoa/Cocoa.h>

// Reproduce a button that highlights and dispatches, but never changes a window.
@interface WizInertWindowControlTarget : NSObject {
@public
    int clicks;
}
- (void)ignoreClick:(id)sender;
@end
@implementation WizInertWindowControlTarget
- (void)ignoreClick:(id)sender { Q_UNUSED(sender); ++clicks; }
@end

int wizMacWindowControlsSelfTest(int argc, char** argv)
{
    QTemporaryDir isolation;
    if (!isolation.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", isolation.filePath("store").toUtf8());
    qputenv("XDG_CACHE_HOME", isolation.filePath("cache").toUtf8());
    QApplication app(argc, argv);
    QCoreApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    app.setQuitOnLastWindowClosed(false);
    if (QGuiApplication::platformName() != "cocoa") return 2;
    // Mirror the login-to-main transition, retaining the hidden modal login.
    WizFramelessWindow<QDialog> login(nullptr, false);
    login.titleBar()->layoutTitleBar();
    login.titleBar()->setFixedHeight(40);
    login.setZoomBtnEnabled(false);
    login.setWindowModality(Qt::ApplicationModal);
    login.resize(400, 300);
    login.show(); app.processEvents(); login.hide(); app.processEvents();
    // Off-the-record synthetic editor, so focus follows the main app's WebEngine
    // path without loading notes, credentials, network URLs or persistent profiles.
    QWebEngineProfile profile;
    WizFramelessWindow<QMainWindow> window(nullptr, true);
    window.titleBar()->layoutTitleBar();
    window.titleBar()->setFixedHeight(40);
    window.clientLayout()->addWidget(new QLabel(QStringLiteral("隔离窗口按钮测试：不打开账号或笔记"), &window));
    auto* editor = new QWebEngineView(&window);
    editor->setPage(new QWebEnginePage(&profile, editor));
    window.clientLayout()->addWidget(editor);
    bool loaded = false;
    QObject::connect(editor, &QWebEngineView::loadFinished, &window, [&](bool ok) { loaded = ok; });
    editor->setHtml("<html><body><input autofocus value='Synthetic window control test'></body></html>");
    window.resize(800, 500);
    window.show();
    int failures = 0;
    auto check = [&](bool ok, const char* label) {
        qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        failures += !ok;
    };
    auto waitFor = [&](std::function<bool()> condition, int milliseconds = 8000) {
        QElapsedTimer timer; timer.start();
        do { app.processEvents(QEventLoop::AllEvents, 50); } while (!condition() && timer.elapsed() < milliseconds);
        return condition();
    };
    waitFor([&] { return window.isActiveWindow(); }, 2000);
    check(waitFor([&] { return loaded; }, 15000), "isolated off-the-record editor loads");
    editor->setFocus(); app.processEvents();
    NSWindow* native = [(NSView*)window.winId() window];
    // Qt's requested state/style mask may change before the AppKit animation
    // completes. Wait for completion notifications before requesting another state.
    const auto fullscreenTransition = std::make_shared<bool>(false);
    const auto minimizeTransition = std::make_shared<bool>(false);
    const auto deminiaturized = std::make_shared<int>(0);
    NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
    NSMutableArray* observers = [NSMutableArray array];
    for (NSString* name in @[NSWindowWillEnterFullScreenNotification, NSWindowWillExitFullScreenNotification])
        [observers addObject:[center addObserverForName:name object:native queue:nil usingBlock:^(NSNotification*) { *fullscreenTransition = true; }]];
    for (NSString* name in @[NSWindowDidEnterFullScreenNotification, NSWindowDidExitFullScreenNotification])
        [observers addObject:[center addObserverForName:name object:native queue:nil usingBlock:^(NSNotification*) { *fullscreenTransition = false; }]];
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
    auto restoreWindow = [&] {
        const int before = *deminiaturized;
        window.showNormal();
        return waitFor([&] { return *deminiaturized > before && minimizeComplete(false); });
    };
    auto button = [&](NSWindowButton type) { return [native standardWindowButton:type]; };
    check(button(NSWindowMiniaturizeButton).enabled && button(NSWindowZoomButton).enabled, "native traffic-light buttons enabled initially");
    auto hit = [&](NSButton* control) {
        NSView* frame = native.contentView.superview;
        NSPoint p = [control convertPoint:NSMakePoint(NSMidX(control.bounds), NSMidY(control.bounds)) toView:frame];
        NSView* target = [frame hitTest:p];
        return target == control || [target isDescendantOf:control];
    };
    check(hit(button(NSWindowMiniaturizeButton)), "native minimize button receives pointer hit testing");
    check(hit(button(NSWindowZoomButton)), "native fullscreen button receives pointer hit testing");
    [button(NSWindowMiniaturizeButton) performClick:nil];
    check(waitFor([&] { return minimizeComplete(true); }), "native yellow-button action minimizes window");
    check(restoreWindow(), "window restores after native minimize");
    [button(NSWindowZoomButton) performClick:nil];
    check(waitFor([&] { return fullscreenComplete(true); }), "native green-button action enters fullscreen");
    window.setWindowState(window.windowState() & ~Qt::WindowFullScreen);
    check(waitFor([&] { return fullscreenComplete(false); }), "native-button fullscreen restores to normal");
    window.setWindowState(window.windowState() | Qt::WindowFullScreen);
    check(waitFor([&] { return fullscreenComplete(true); }), "Qt/native fullscreen entry");
    window.setWindowState(window.windowState() & ~Qt::WindowFullScreen);
    check(waitFor([&] { return fullscreenComplete(false); }), "Qt/native fullscreen exit");
    QDialog modal(&window); modal.setWindowModality(Qt::ApplicationModal); modal.show();
    waitFor([&] { return QApplication::activeModalWidget() == &modal; }, 2000);
    modal.close();
    check(waitFor([&] { return !QApplication::activeModalWidget()
        && button(NSWindowMiniaturizeButton).enabled && button(NSWindowZoomButton).enabled; }), "button policy restores after modal closes");

    // Distinguish dead action routing from a window that cannot change state.
    WizInertWindowControlTarget* inert = [[WizInertWindowControlTarget alloc] init];
    [button(NSWindowMiniaturizeButton) setTarget:inert];
    [button(NSWindowMiniaturizeButton) setAction:@selector(ignoreClick:)];
    [button(NSWindowZoomButton) setTarget:inert];
    [button(NSWindowZoomButton) setAction:@selector(ignoreClick:)];
    [button(NSWindowMiniaturizeButton) performClick:nil];
    [button(NSWindowZoomButton) performClick:nil];
    check(inert->clicks == 2 && !window.isMinimized() && !window.isFullScreen(),
        "inert action fixture dispatches both clicks without changing the window");
    check(wizMacBindMainWindowControls(&window), "direct window action bridge binds both native buttons");
    [inert release];
    [button(NSWindowMiniaturizeButton) performClick:nil];
    check(waitFor([&] { return minimizeComplete(true); }), "direct yellow-button binding repairs inert native action");
    check(restoreWindow(), "direct-binding window restores");
    wizMacBindMainWindowControls(&window);
    editor->setFocus(); app.processEvents();
    [button(NSWindowZoomButton) performClick:nil];
    check(waitFor([&] { return fullscreenComplete(true); }),
        "direct green-button binding enters fullscreen with editor focus");
    wizMacBindMainWindowControls(&window);
    [button(NSWindowZoomButton) performClick:nil];
    check(waitFor([&] { return fullscreenComplete(false); }),
        "direct green-button binding exits fullscreen");
    // The action must address its owner, not whichever window happens to be active.
    QWidget auxiliary; auxiliary.resize(240, 120); auxiliary.show(); auxiliary.activateWindow();
    waitFor([&] { return QApplication::activeWindow() == &auxiliary; }, 2000);
    wizMacBindMainWindowControls(&window);
    [button(NSWindowMiniaturizeButton) performClick:nil];
    check(waitFor([&] { return minimizeComplete(true); }) && !auxiliary.isMinimized(),
        "direct action changes owning window, not another active window");
    auxiliary.close();
    check(restoreWindow(), "owning window restores before modal test");
    wizMacBindMainWindowControls(&window);
    QDialog blocked(&window); blocked.setWindowModality(Qt::ApplicationModal); blocked.show();
    waitFor([&] { return QApplication::activeModalWidget() == &blocked; }, 2000);
    check(minimizeComplete(false) && fullscreenComplete(false)
        && QApplication::activeModalWidget() == &blocked, "normal-state modal-blocking test precondition");
    // Invoke target directly to verify modal protection independently of AppKit.
    id direct = [button(NSWindowMiniaturizeButton) target];
    [direct performSelector:@selector(minimize:) withObject:nil];
    [direct performSelector:@selector(zoomOrFullscreen:) withObject:nil];
    check(!window.isMinimized() && !window.isFullScreen(), "direct bridge respects active modal blocking");
    blocked.close(); app.processEvents();
    // Repeating binding is safe; the target is owned by the native window.
    check(wizMacBindMainWindowControls(&window)
        && wizMacBindMainWindowControls(&window), "repeated bridge binding is idempotent");
    window.close();
    for (id observer in observers) [center removeObserver:observer];
    return failures ? 1 : 0;
}
