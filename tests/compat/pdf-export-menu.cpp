#include "share/WizPdfExportMenu.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
#include <QDebug>

int main(int argc, char** argv)
{
    QApplication app(argc,argv);
    QTemporaryDir root;
    if (!root.isValid()) return 2;
    const QString settingsPath=root.filePath("settings.ini");
    { // An obsolete development preference must not affect new commands.
        QSettings legacy(settingsPath,QSettings::IniFormat);
        legacy.setValue("PDFExport/PageMode","a4");
        legacy.setValue("unrelated","preserve");
        legacy.sync();
    }
    const auto read=[&]{QFile f(settingsPath);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};};
    const auto before=read();
    int count=0, failures=0, direct=0;
    const auto check=[&](bool ok,const char* label){++count;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;};
    QWidget owner;
    QAction action(QStringLiteral("另存为 PDF…"),&owner);
    using Mode=WizPdfExport::Mode;
    QList<Mode> dispatched;
    QObject::connect(&action,&QAction::triggered,&owner,[&]{++direct;});
    const auto exportPdf=[&](Mode selected){dispatched.append(selected);};
    WizPdfExport::attachMenu(&action,&owner,exportPdf);
    auto* menu=action.menu();
    check(menu && menu->parent()==&owner,"submenu belongs to existing window");
    if (!menu) return 3;
    const auto items=menu->actions();
    check(items.size()==2,"exactly two independent width commands");
    if (items.size()!=2) return 3;
    check(items[0]->objectName()=="wizPdfReadingWidth" && items[1]->objectName()=="wizPdfA4Width","stable distinct mode actions");
    check(items[0]->text().contains(QStringLiteral("保持正文宽度")) && items[1]->text().contains("A4"),"self-describing width labels");
    const auto plain=[&]{return !items[0]->isCheckable() && !items[1]->isCheckable()
        && !items[0]->isChecked() && !items[1]->isChecked();};
    check(plain(),"no checkmarks or remembered-choice UI on first open");
    items[0]->trigger();
    check(dispatched==QList<Mode>{Mode::ReadingWidth} && direct==0,"reading command ignores legacy A4 setting and dispatches once");
    items[1]->trigger();
    check(dispatched==QList<Mode>{Mode::ReadingWidth,Mode::A4} && direct==0,"A4 command passes an explicit per-request mode without double dispatch");
    items[0]->trigger();
    check(dispatched.last()==Mode::ReadingWidth && dispatched.size()==3,"next reading command does not inherit previous A4 mode");
    check(plain(),"export commands never acquire a checkmark");
    QMetaObject::invokeMethod(menu,"aboutToShow",Qt::DirectConnection);
    check(plain() && dispatched.size()==3,"reopening submenu changes neither appearance nor requests");
    check(!before.isEmpty() && read()==before,"legacy and unrelated settings stay byte-identical");
    action.setEnabled(false);
    items[1]->trigger();
    check(dispatched.size()==3,"disabled original action prevents both export commands");
    action.setEnabled(true);
    WizPdfExport::attachMenu(&action,&owner,exportPdf);
    check(action.menu()==menu && menu->actions().size()==2,"reattachment does not duplicate commands or callbacks");
    items[1]->trigger();
    check(dispatched.size()==4 && dispatched.last()==Mode::A4,"reenabling exports the explicitly selected mode once");
    auto* transientAction=new QAction(&owner);
    WizPdfExport::attachMenu(transientAction,&owner,exportPdf);
    auto* orphan=transientAction->menu();
    delete transientAction;
    orphan->actions()[1]->trigger();
    check(dispatched.size()==4,"destroyed original action cannot export from surviving menu");
    QAction recreated(&owner);
    WizPdfExport::attachMenu(&recreated,&owner,exportPdf);
    check(!recreated.menu()->actions()[0]->isCheckable() && !recreated.menu()->actions()[1]->isCheckable(),"recreated menu does not restore a previous choice");
    recreated.menu()->actions()[0]->trigger();
    check(dispatched.size()==5 && dispatched.last()==Mode::ReadingWidth,"recreated reading command still passes its own mode");
    // Simulate a native save dialog dispatching another command reentrantly.
    QAction nested(&owner);
    QList<Mode> nestedModes;
    WizPdfExport::attachMenu(&nested,&owner,[&](Mode selected){
        nestedModes.append(selected);
        if (nestedModes.size()==1) nested.menu()->actions()[0]->trigger();
        nestedModes.append(selected);
    });
    nested.menu()->actions()[1]->trigger();
    check(nestedModes==QList<Mode>{Mode::A4,Mode::ReadingWidth,Mode::ReadingWidth,Mode::A4},
          "nested command cannot overwrite the outer request mode");
    check(read()==before,"nested and recreated commands never persist a mode");
    QAction invalid(&owner);
    WizPdfExport::attachMenu(nullptr,&owner,exportPdf);
    WizPdfExport::attachMenu(&invalid,nullptr,exportPdf);
    WizPdfExport::attachMenu(&invalid,&owner,{});
    check(!invalid.menu(),"missing action owner or callback does not create an unusable menu");
    qInfo()<<"PDF submenu checks:"<<count<<"failures:"<<failures;
    return failures?1:0;
}
