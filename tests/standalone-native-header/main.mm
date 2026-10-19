#import <Cocoa/Cocoa.h>
#include <QApplication>
#include <QMainWindow>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QPointer>
#include <QTimer>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCommandLineParser>
#include <vector>
#include "header-under-test.inc"
namespace candidate {
#include "candidate-observer.inc"
}

static QString rectString(NSRect rect) { return QString::fromNSString(NSStringFromRect(rect)); }
static QString className(id object) { return object?QString::fromNSString(NSStringFromClass([object class])):QStringLiteral("nil"); }

int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    app.setQuitOnLastWindowClosed(false);
    app.setApplicationName("Wiz Native Header Probe — synthetic");
    QCommandLineParser parser;
    parser.addOption({"mode","system, current, candidate, geometry-only, accessory-right, accessory-top, or toolbar","mode"});
    parser.addOption({"out","New absolute private JSON result path","path"});
    parser.addOption({"seconds","Observation duration, 5–60 seconds","seconds","12"});
    parser.process(app);
    const QString mode=parser.value("mode"),out=parser.value("out");
    const QStringList modes={"system","current","candidate","geometry-only","accessory-right","accessory-top","toolbar"};
    bool numberOk=false;
    const int seconds=parser.value("seconds").toInt(&numberOk);
    if (!modes.contains(mode)||!QFileInfo(out).isAbsolute()||QFileInfo::exists(out)
        ||!QFileInfo(QFileInfo(out).absolutePath()).isDir()||!numberOk||seconds<5||seconds>60) return 2;
    QMainWindow window;
    window.setWindowFlags(window.windowFlags()|Qt::ExpandedClientAreaHint|Qt::NoTitleBarBackgroundHint);
    window.setAttribute(Qt::WA_ContentsMarginsRespectsSafeArea,false);
    window.setWindowTitle("Synthetic native header — "+mode);
    auto* root=new QWidget;
    auto* column=new QVBoxLayout(root);column->setContentsMargins(0,0,0,0);column->setSpacing(0);
    auto* header=new QWidget;header->setFixedHeight(44);
    auto* row=new QHBoxLayout(header);row->setContentsMargins(90,5,20,5);
    auto* search=new QLineEdit("Synthetic search");row->addWidget(search);
    column->addWidget(header);column->addStretch();window.setCentralWidget(root);
    window.resize(800,450);
    NSView* qtView=reinterpret_cast<NSView*>(window.winId());
    NSWindow* native=qtView.window;
    native.titleVisibility=NSWindowTitleHidden;
    if (mode=="current") wizMacConfigureUnifiedHeader(&window,44);
    if (mode=="candidate") candidate::wizMacConfigureUnifiedHeader(&window,44);
    if (mode=="geometry-only") candidate::applyUnifiedHeaderGeometry(&window,44);
    if (mode.startsWith("accessory-")) {
        auto* accessory=[[NSTitlebarAccessoryViewController alloc] init];
        auto* view=[[NSView alloc] initWithFrame:NSMakeRect(0,0,1,44)];
        accessory.view=view;
        accessory.layoutAttribute=mode=="accessory-top"?NSLayoutAttributeTop:NSLayoutAttributeRight;
        [native addTitlebarAccessoryViewController:accessory];
        [view release];[accessory release];
    }
    if (mode=="toolbar") {
        auto* toolbar=[[NSToolbar alloc] initWithIdentifier:@"synthetic-header-toolbar"];
        toolbar.displayMode=NSToolbarDisplayModeIconOnly;
        native.toolbar=toolbar;native.toolbarStyle=NSWindowToolbarStyleUnified;
        [toolbar release];
    }
    QJsonArray samples;
    QString last;
    QTimer sampler;
    QObject::connect(&sampler,&QTimer::timeout,&app,[&] {
        QJsonObject sample{{"mode",mode},{"visible",window.isVisible()},{"minimized",window.isMinimized()},
            {"fullscreen",window.isFullScreen()},{"key",bool(native.keyWindow)},
            {"searchY",search->mapTo(&window,search->rect().center()).y()},
            {"activeSharing",bool(native.hasActiveWindowSharingSession)},
            {"contentLayout",rectString(native.contentLayoutRect)}};
        QJsonArray controls;
        for (NSWindowButton kind:{NSWindowCloseButton,NSWindowMiniaturizeButton,NSWindowZoomButton}) {
            NSButton* control=[native standardWindowButton:kind];
            NSView* frame=native.contentView.superview;
            const NSPoint point=[control convertPoint:NSMakePoint(NSMidX(control.bounds),NSMidY(control.bounds)) toView:frame];
            NSView* target=[frame hitTest:point];
            QJsonArray ancestry;
            for (NSView* view=control;view;view=view.superview) ancestry.append(className(view)+":"+rectString(view.frame));
            const NSRect r=[native.contentView convertRect:control.bounds fromView:control];
            controls.append(QJsonObject{{"kind",int(kind)},{"hidden",bool(control.hiddenOrHasHiddenAncestor)},
                {"enabled",bool(control.enabled)},{"centerY",native.contentView.flipped?NSMidY(r):NSHeight(native.contentView.bounds)-NSMidY(r)},
                {"hit",className(target)},{"legacyHitMatches",target==control||bool([target isDescendantOf:control])},
                {"ancestry",ancestry}});
        }
        sample.insert("controls",controls);
        const QString text=QString::fromUtf8(QJsonDocument(sample).toJson(QJsonDocument::Compact));
        if (text!=last) {last=text;samples.append(sample);qInfo().noquote()<<"SAMPLE"<<text;}
    });
    sampler.start(100);
    window.show();window.raise();window.activateWindow();[NSApp activateIgnoringOtherApps:YES];
    QTimer::singleShot(seconds*1000,&app,&QApplication::quit);
    app.exec();sampler.stop();
    QFile output(out);
    if (!output.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return 3;
    const QJsonDocument record(QJsonObject{{"synthetic",true},{"sourceSha256",WIZ_HEADER_SOURCE_SHA},
        {"unacceptedCandidateSha256",WIZ_HEADER_CANDIDATE_SHA},{"qtVersion",qVersion()},
        {"mode",mode},{"samples",samples},{"normalExit",true}});
    return output.write(record.toJson())>=0?0:3;
}
