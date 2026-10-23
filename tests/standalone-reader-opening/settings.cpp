#include "ui_WizPreferenceDialog.h"
#include <QApplication>
#include <QTranslator>
#include <QDebug>
#include <QTemporaryDir>
#include <QSettings>
#include <QFile>
int main(int argc,char** argv) {
    QCoreApplication::setAttribute(Qt::AA_PluginApplication);
    QApplication app(argc,argv);QDialog dialog;QTranslator zh;int checks=0,failures=0;
    const auto check=[&](bool v,const char* text){++checks;failures+=!v;qInfo().noquote()<<(v?"PASS":"FAIL")<<text;};
    check(argc==2&&zh.load(QString::fromLocal8Bit(argv[1])),"fresh Chinese catalog loads");app.installTranslator(&zh);
    Ui::WizPreferenceWindow ui;ui.setupUi(&dialog);
    check(ui.tabWidget->tabText(ui.tabWidget->indexOf(ui.tabPrint))==QStringLiteral("PDF 导出"),"PDF tab is Chinese");
    check(ui.groupBox_2->title()==QStringLiteral("页边距")&&ui.label_5->text()==QStringLiteral("单位："),"margin heading and unit are Chinese");
    check(ui.labelPdfMargins->text()==QStringLiteral("两种 PDF 版式共用此页边距。")&&ui.labelPdfMargins->wordWrap(),"short Chinese hint fits wrapping label");
    const QStringList units={QStringLiteral("毫米"),QStringLiteral("点"),QStringLiteral("英寸"),QStringLiteral("派卡"),QStringLiteral("迪多点"),QStringLiteral("西塞罗")};
    check(ui.comboBox_unit->count()==units.size(),"six stored-unit indexes preserved");
    for(int i=0;i<units.size();++i)check(ui.comboBox_unit->itemText(i)==units[i],"each unit has Chinese text at original index");
    check(ui.label->text()==QStringLiteral("上：")&&ui.label_2->text()==QStringLiteral("下：")&&ui.label_3->text()==QStringLiteral("左：")&&ui.label_4->text()==QStringLiteral("右："),"four margin labels are Chinese");
    ui.comboBox_unit->setCurrentIndex(4);ui.spinBox_top->setValue(12.5);ui.spinBox_bottom->setValue(13);ui.spinBox_left->setValue(14);ui.spinBox_right->setValue(15);
    check(ui.comboBox_unit->currentIndex()==4&&ui.spinBox_top->value()==12.5&&ui.spinBox_bottom->value()==13&&ui.spinBox_left->value()==14&&ui.spinBox_right->value()==15,"text cleanup leaves unit selections and numeric margins intact");
    app.removeTranslator(&zh);ui.retranslateUi(&dialog);check(ui.labelPdfMargins->text()=="Margins apply to both PDF layouts.","English catalog fallback remains concise");
    qInfo()<<"PDF settings labels checks"<<checks<<"failures"<<failures;return failures?1:0;
}
