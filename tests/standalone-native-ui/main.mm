#import <Cocoa/Cocoa.h>
#include "widgets/WizMarkdownExportRecoveryPrompt.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QDebug>
#include <functional>

using namespace WizMarkdownExportRecovery;
using Prompt = WizMarkdownExportRecoveryPrompt;
static QJsonArray results;
static QJsonArray keyboardObservations;
static int failures = 0;

static void check(bool ok, const QString& label)
{
    results.append(QJsonObject{{"label",label},{"passed",ok}});
    failures += !ok;
    qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
}

static bool waitFor(const std::function<bool()>& ready, int milliseconds = 2500)
{
    QElapsedTimer elapsed;
    elapsed.start();
    while (!ready() && elapsed.elapsed() < milliseconds) {
        QEventLoop loop;
        QTimer::singleShot(20, &loop, &QEventLoop::quit);
        loop.exec();
    }
    return ready();
}

static NSWindow* nativeWindow(QWidget& widget)
{
    return reinterpret_cast<NSView*>(widget.winId()).window;
}

static QPushButton* button(Prompt& prompt, const char* suffix)
{
    return prompt.findChild<QPushButton*>(QStringLiteral("recoveryPrompt") + QLatin1String(suffix));
}

static Inspection fixture()
{
    Inspection inspection;
    inspection.availability = Availability::AwaitingDecision;
    inspection.diagnosticDetail = QStringLiteral("/synthetic/only/export.md\n<literal>&详情，不是真实笔记");
    return inspection;
}

static bool openPrompt(Prompt& prompt)
{
    prompt.show();
    prompt.raise();
    prompt.activateWindow();
    [NSApp activateIgnoringOtherApps:YES];
    NSWindow* native = nativeWindow(prompt);
    [native makeKeyAndOrderFront:nil];
    const bool ready = waitFor([&] {
        return prompt.isVisible() && native.visible && native.keyWindow && NSApp.active
            && QApplication::activeModalWidget() == &prompt;
    });
    check(ready, QStringLiteral("actual Cocoa prompt visible, key and modal in active isolated app"));
    check(!native.hasActiveWindowSharingSession, QStringLiteral("prompt has no active window-sharing session"));
    check(button(prompt,"Retain")->hasFocus(), QStringLiteral("native prompt initially focuses retain"));
    return ready;
}

static void nativeKey(QWidget& prompt, unichar character, unsigned short keyCode,
                      NSEventModifierFlags modifiers = 0)
{
    NSWindow* native = nativeWindow(prompt);
    if (!native.keyWindow || !NSApp.active) {
        check(false, QStringLiteral("refuse key dispatch without own native key window"));
        return;
    }
    NSString* text = [NSString stringWithCharacters:&character length:1];
    for (NSEventType type : {NSEventTypeKeyDown, NSEventTypeKeyUp}) {
        NSEvent* event = [NSEvent keyEventWithType:type location:NSZeroPoint modifierFlags:modifiers
            timestamp:NSProcessInfo.processInfo.systemUptime windowNumber:native.windowNumber
            context:nil characters:text charactersIgnoringModifiers:text isARepeat:NO keyCode:keyCode];
        [NSApp sendEvent:event];
    }
    QApplication::processEvents();
}

static QJsonArray tabTrace(QWidget& widget)
{
    QJsonArray trace;
    for (NSEventModifierFlags modifiers : {NSEventModifierFlags(0),NSEventModifierFlags(0),
            NSEventModifierFlags(NSEventModifierFlagShift)}) {
        nativeKey(widget,'\t',48,modifiers);
        const auto* focus=QApplication::focusWidget();
        trace.append(focus ? focus->objectName() : QStringLiteral("no-focus"));
    }
    return trace;
}

static QJsonArray stockTabTrace(QWidget& parent)
{
    // Preserve the system's navigation policy. Compare the actual prompt with
    // a standard Qt dialog under the same policy rather than enabling a global
    // accessibility preference to force every button into Tab traversal.
    QDialog stock(&parent);
    stock.setWindowModality(Qt::WindowModal);
    stock.setWindowTitle(QStringLiteral("Synthetic stock keyboard control"));
    auto* layout=new QHBoxLayout(&stock);
    auto* details=new QPushButton(QStringLiteral("Details"),&stock);
    auto* retain=new QPushButton(QStringLiteral("Retain"),&stock);
    auto* confirm=new QPushButton(QStringLiteral("Confirm"),&stock);
    details->setObjectName(QStringLiteral("recoveryPromptShowDetails"));
    retain->setObjectName(QStringLiteral("recoveryPromptRetain"));
    confirm->setObjectName(QStringLiteral("recoveryPromptConfirm"));
    for (auto* b : {details,retain,confirm}) b->setAutoDefault(false);
    retain->setDefault(true);
    layout->addWidget(details);
    layout->addStretch();
    layout->addWidget(retain);
    layout->addWidget(confirm);
    QWidget::setTabOrder(retain,details);
    QWidget::setTabOrder(details,confirm);
    stock.show();
    [NSApp activateIgnoringOtherApps:YES];
    [nativeWindow(stock) makeKeyAndOrderFront:nil];
    const bool ready=waitFor([&] { return nativeWindow(stock).keyWindow && NSApp.active; });
    check(ready,QStringLiteral("stock native dialog control is key"));
    retain->setFocus();
    return ready ? tabTrace(stock) : QJsonArray();
}

static void testPrompt(QWidget& parent)
{
    for (int mode=0; mode<5; ++mode) {
        Prompt prompt(fixture(), LockState::Available, &parent);
        int published=0;
        QObject::connect(&prompt,&QDialog::finished,&parent,[&](int){++published;});
        if (!openPrompt(prompt)) { prompt.retainForExit(); continue; }
        if (mode==0) nativeKey(prompt,'\r',36);
        if (mode==1) nativeKey(prompt,0x1b,53);
        if (mode==2) {
            button(prompt,"Confirm")->setFocus();
            nativeKey(prompt,'\r',36);
        }
        if (mode==3) {
            NSWindow* native=nativeWindow(prompt);
            NSButton* close=[native standardWindowButton:NSWindowCloseButton];
            const bool available=close && close.enabled && !close.hiddenOrHasHiddenAncestor;
            qInfo()<<"Cocoa close availability"<<available<<"sheet"<<bool(native.sheetParent)
                <<"closable mask"<<bool(native.styleMask & NSWindowStyleMaskClosable);
            if (available) [native performClose:nil];
            else {
                check(!prompt.hasDecision(),QStringLiteral("sheet without native close has not confirmed"));
                prompt.close(); // Actual host close request for a sheet without a red control.
            }
        }
        if (mode==4) prompt.retainForExit();
        const bool retained = waitFor([&] { return prompt.hasDecision() && !prompt.isVisible(); });
        check(retained && prompt.decision()==Decision::Retain && published==1,
            QStringLiteral("native retain route %1 preserves once").arg(mode));
        prompt.accept();
        prompt.reject();
        check(published==1 && prompt.decision()==Decision::Retain,
            QStringLiteral("settled native retain route cannot later confirm"));
    }
    {
        const QJsonArray stockTrace=stockTabTrace(parent);
        Prompt prompt(fixture(),LockState::Available,&parent);
        if (openPrompt(prompt)) {
            const QJsonArray trace=tabTrace(prompt);
            keyboardObservations.append(QJsonObject{{"stock",stockTrace},{"prompt",trace},
                {"fullKeyboardAccessEnabled",bool(NSApp.fullKeyboardAccessEnabled)}});
            qInfo()<<"Cocoa Tab comparison"<<stockTrace<<trace<<"full keyboard"<<bool(NSApp.fullKeyboardAccessEnabled);
            check(!stockTrace.isEmpty() && trace==stockTrace,
                QStringLiteral("native Tab follows stock dialog under unchanged system navigation policy"));
            if (NSApp.fullKeyboardAccessEnabled) {
                check(trace==QJsonArray{QStringLiteral("recoveryPromptShowDetails"),
                    QStringLiteral("recoveryPromptConfirm"),QStringLiteral("recoveryPromptShowDetails")},
                    QStringLiteral("enabled full-keyboard mode traverses all buttons in intended order"));
            }
            button(prompt,"ShowDetails")->setFocus();
            nativeKey(prompt,' ',49);
            auto* detail=prompt.findChild<QPlainTextEdit*>(QStringLiteral("recoveryPromptDetails"));
            check(detail && detail->isVisible() && detail->isReadOnly()
                && detail->toPlainText()==fixture().diagnosticDetail && !prompt.hasDecision(),
                QStringLiteral("native Space reveals literal read-only detail without confirming"));
            button(prompt,"ShowDetails")->setFocus();
            nativeKey(prompt,' ',49);
            check(detail && !detail->isVisible() && detail->toPlainText().isEmpty(),
                QStringLiteral("native Space hides and clears detail"));
            button(prompt,"Confirm")->setFocus();
            nativeKey(prompt,' ',49);
            check(waitFor([&] { return prompt.hasDecision(); }) && prompt.decision()==Decision::Confirm,
                QStringLiteral("native Space explicitly confirms decision only; no backend linked"));
        }
    }
    {
        Prompt prompt(fixture(),LockState::Active,&parent);
        if (openPrompt(prompt)) {
            check(!button(prompt,"Confirm")->isEnabled(),QStringLiteral("busy native prompt cannot confirm"));
            nativeKey(prompt,'\r',36);
            check(prompt.hasDecision() && prompt.decision()==Decision::Retain,
                QStringLiteral("busy native prompt Enter preserves"));
        }
    }
    {
        auto* owner=new QWidget(&parent,Qt::Window);
        owner->setWindowTitle(QStringLiteral("Synthetic disposable owner"));
        owner->show();
        QPointer<Prompt> prompt=new Prompt(fixture(),LockState::Available,owner);
        openPrompt(*prompt);
        delete owner;
        check(prompt.isNull(),QStringLiteral("destroying synthetic native owner destroys pending prompt"));
    }
}

static void testPicker(QWidget& parent,const QString& root,bool accept)
{
    const QString path=root+(accept ? "/selected-only.pdf" : "/unchanged.pdf");
    if (!accept) {
        QFile sentinel(path);
        if (!sentinel.open(QIODevice::WriteOnly|QIODevice::NewOnly)
            || sentinel.write("synthetic sentinel")!=18) {
            check(false,QStringLiteral("create isolated picker sentinel"));
            return;
        }
    }
    QFileDialog dialog(&parent,QStringLiteral("Synthetic PDF path selection only"),root,
        QStringLiteral("PDF (*.pdf)"));
    dialog.setAcceptMode(QFileDialog::AcceptSave);
    dialog.setFileMode(QFileDialog::AnyFile);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.selectFile(path);
    int result=-1;
    QObject::connect(&dialog,&QDialog::finished,&parent,[&](int value){result=value;});
    dialog.open();
    NSSavePanel* panel=nil;
    const bool visible=waitFor([&] {
        for (NSWindow* window in NSApp.windows)
            if ([window isKindOfClass:NSSavePanel.class] && window.visible) {
                panel=(NSSavePanel*)window;
                return true;
            }
        return false;
    });
    check(visible,QStringLiteral("Qt file picker uses a visible public NSSavePanel"));
    if (!visible) { dialog.reject(); return; }
    check(!panel.hasActiveWindowSharingSession,QStringLiteral("native save panel is unshared"));
    if (accept) {
        check(waitFor([&] { return [panel.nameFieldStringValue isEqualToString:@"selected-only.pdf"]; }),
            QStringLiteral("native save panel displays exact synthetic filename"));
        // The current service-backed panel throws from NSSavePanel's public
        // ok: action. Never call it or private/service internals. A real UI
        // click is a separate explicit opt-in; path selection writes no PDF.
        qInfo()<<"MANUAL SAVE READY: click Save in this synthetic panel; no PDF will be written";
    } else [panel cancel:nil];
    const bool finished=waitFor([&] { return result!=-1 && !dialog.isVisible(); },accept ? 60000 : 2500);
    check(finished && result==(accept ? QDialog::Accepted : QDialog::Rejected),
        accept ? QStringLiteral("native Save accepts isolated path") : QStringLiteral("native Cancel returns rejected"));
    if (accept) {
        check(dialog.selectedFiles()==QStringList{path},QStringLiteral("native selected path exactly matches isolated target"));
        check(!QFileInfo::exists(path),QStringLiteral("path selection alone does not write an export"));
    } else {
        QFile sentinel(path);
        check(sentinel.open(QIODevice::ReadOnly) && sentinel.readAll()==QByteArray("synthetic sentinel"),
            QStringLiteral("native Cancel leaves synthetic destination byte-identical"));
    }
    if (!finished) [panel cancel:nil];
}

int main(int argc,char** argv)
{
    const bool manualSave=argc==4 && QByteArray(argv[2])=="--manual-save";
    if ((argc!=3 && !manualSave) || QByteArray(argv[1])!="--foreground-synthetic-test") return 2;
    const QString out=QString::fromLocal8Bit(argv[manualSave ? 3 : 2]);
    if (!QFileInfo(out).isAbsolute() || QFileInfo::exists(out)
        || !QFileInfo(QFileInfo(out).absolutePath()).isDir()) return 2;
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    qputenv("XDG_CACHE_HOME",(root.path()+"/cache").toUtf8());
    QApplication app(argc,argv);
    if (QGuiApplication::platformName()!=QStringLiteral("cocoa")) return 2;
    app.setApplicationName(QStringLiteral("Wiz Synthetic Native UI Probe"));
    app.setQuitOnLastWindowClosed(false);
    QWidget parent;
    parent.setWindowTitle(QStringLiteral("隔离桌面验收 — 仅合成资料"));
    parent.resize(700,400);
    parent.show();
    [NSApp activateIgnoringOtherApps:YES];
    [nativeWindow(parent) makeKeyAndOrderFront:nil];
    QTimer::singleShot(0,&app,[&] {
        check(waitFor([&] { return nativeWindow(parent).keyWindow && NSApp.active; }),
            QStringLiteral("synthetic parent is native key window"));
        testPrompt(parent);
        if (manualSave) testPicker(parent,root.path(),true);
        testPicker(parent,root.path(),false);
        app.quit();
    });
    app.exec();
    QFile report(out);
    const QJsonDocument record(QJsonObject{{"synthetic",true},{"platform",QGuiApplication::platformName()},
        {"promptSourceSha256",WIZ_NATIVE_PROMPT_SHA},{"qtVersion",qVersion()},
        {"fullKeyboardAccessEnabled",bool(NSApp.fullKeyboardAccessEnabled)},
        {"keyboardObservations",keyboardObservations},
        {"voiceOverTested",false},{"voiceOverRequiredForPersonalUse",false},
        {"manualSaveSelectionTested",manualSave},
        {"normalExit",true},{"checks",results.size()},{"failures",failures},{"results",results}});
    if (!report.open(QIODevice::WriteOnly|QIODevice::NewOnly) || report.write(record.toJson())<0) return 3;
    qInfo()<<"Native UI checks:"<<results.size()<<"failures:"<<failures;
    return failures ? 1 : 0;
}
