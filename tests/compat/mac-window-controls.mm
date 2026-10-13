#include "share/WizFramelessWindow.h"
#include <QMainWindow>
#include <QApplication>
#include <QDialog>
#include <QLabel>
#include <functional>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QDebug>
#import <Cocoa/Cocoa.h>

int wizMacWindowControlsSelfTest(int argc, char** argv)
{
    QTemporaryDir isolation;
    if (!isolation.isValid()) return 1;
    qputenv("WIZNOTE_DATA_STORE", isolation.filePath("store").toUtf8());
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
    WizFramelessWindow<QMainWindow> window(nullptr, true);
    window.titleBar()->layoutTitleBar();
    window.titleBar()->setFixedHeight(40);
    window.clientLayout()->addWidget(new QLabel(QStringLiteral("隔离窗口按钮测试：不打开账号或笔记"), &window));
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
    NSWindow* native = [(NSView*)window.winId() window];
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
    check(waitFor([&] { return window.isMinimized(); }), "native yellow-button action minimizes window");
    window.showNormal();
    check(waitFor([&] { return !window.isMinimized(); }), "window restores after native minimize");
    [button(NSWindowZoomButton) performClick:nil];
    check(waitFor([&] { return window.isFullScreen() && bool(native.styleMask & NSWindowStyleMaskFullScreen); }), "native green-button action enters fullscreen");
    window.setWindowState(window.windowState() & ~Qt::WindowFullScreen);
    check(waitFor([&] { return !window.isFullScreen() && !(native.styleMask & NSWindowStyleMaskFullScreen); }), "native-button fullscreen restores to normal");
    window.setWindowState(window.windowState() | Qt::WindowFullScreen);
    check(waitFor([&] { return window.isFullScreen() && bool(native.styleMask & NSWindowStyleMaskFullScreen); }), "Qt/native fullscreen entry");
    window.setWindowState(window.windowState() & ~Qt::WindowFullScreen);
    check(waitFor([&] { return !window.isFullScreen() && !(native.styleMask & NSWindowStyleMaskFullScreen); }), "Qt/native fullscreen exit");
    QDialog modal(&window); modal.setWindowModality(Qt::ApplicationModal); modal.show();
    waitFor([&] { return QApplication::activeModalWidget() == &modal; }, 2000);
    modal.close();
    check(waitFor([&] { return !QApplication::activeModalWidget()
        && button(NSWindowMiniaturizeButton).enabled && button(NSWindowZoomButton).enabled; }), "button policy restores after modal closes");
    window.close();
    return failures ? 1 : 0;
}
