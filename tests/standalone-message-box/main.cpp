#include "WizMessageBox.h"
#include <QApplication>
#include <QDebug>
#include <QPointer>
#include <QPushButton>
#include <QTimer>
#include <memory>
#include <vector>

using Button=QMessageBox::StandardButton;
struct Case {
    QMessageBox::Icon kind;
    QMessageBox::StandardButtons buttons;
    Button defaultButton,choice;
    bool parentless=false,appTitle=false;
};
static Button invoke(const Case& item,QWidget* parent,const QString& title,const QString& text)
{
    if(item.appTitle)return WizMessageBox::information(parent,text,item.buttons,item.defaultButton);
    switch(item.kind) {
    case QMessageBox::Warning:return WizMessageBox::warning(parent,title,text,item.buttons,item.defaultButton);
    case QMessageBox::Information:return WizMessageBox::information(parent,title,text,item.buttons,item.defaultButton);
    case QMessageBox::Question:return WizMessageBox::question(parent,title,text,item.buttons,item.defaultButton);
    default:return WizMessageBox::critical(parent,title,text,item.buttons,item.defaultButton);
    }
}
static QString label(Button button)
{
    switch(button) {
    case QMessageBox::Ok:return QObject::tr("OK");
    case QMessageBox::Cancel:return QObject::tr("Cancel");
    case QMessageBox::Yes:return QObject::tr("Yes");
    case QMessageBox::No:return QObject::tr("No");
    default:return QString();
    }
}
int main(int argc,char** argv)
{
    QApplication app(argc,argv);
    app.setApplicationName("Isolated message-box regression");
    app.setQuitOnLastWindowClosed(false);
    if(QApplication::platformName()!="offscreen")return 2;
    const bool destroyParent=argc==2&&QByteArray(argv[1])=="--destroy-parent";
    if(argc!=1&&!destroyParent)return 2;
    qInfo()<<"DIAGNOSTIC"<<WIZ_MESSAGE_BOX_MODE<<"source"<<WIZ_MESSAGE_BOX_SOURCE_SHA
           <<"candidate"<<WIZ_MESSAGE_BOX_CANDIDATE_SHA<<"destroyParent"<<destroyParent;
    std::vector<Case> cases=destroyParent?std::vector<Case>{
        {QMessageBox::Warning,QMessageBox::Ok,QMessageBox::Ok,QMessageBox::Ok},
        {QMessageBox::Information,QMessageBox::Ok,QMessageBox::Ok,QMessageBox::Ok},
        {QMessageBox::Question,QMessageBox::Yes|QMessageBox::No,QMessageBox::No,QMessageBox::Yes},
        {QMessageBox::Critical,QMessageBox::Ok|QMessageBox::Cancel,QMessageBox::Cancel,QMessageBox::Cancel}
    }:std::vector<Case>{
        {QMessageBox::Warning,QMessageBox::Ok,QMessageBox::Ok,QMessageBox::Ok},
        {QMessageBox::Information,QMessageBox::Ok|QMessageBox::Cancel,QMessageBox::Cancel,QMessageBox::Cancel},
        {QMessageBox::Question,QMessageBox::Yes|QMessageBox::No,QMessageBox::No,QMessageBox::Yes},
        {QMessageBox::Question,QMessageBox::Yes|QMessageBox::No,QMessageBox::No,QMessageBox::No},
        {QMessageBox::Critical,QMessageBox::Ok|QMessageBox::Cancel,QMessageBox::Cancel,QMessageBox::Cancel},
        {QMessageBox::Information,QMessageBox::Ok,QMessageBox::Ok,QMessageBox::Ok,true,true}
    };
    int checks=0,failures=0,index=0;
    const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<index<<text;};
    for(const auto& item:cases) {
        ++index;
        auto parent=std::make_unique<QWidget>();parent->setGeometry(40,50,600,400);
        QWidget* owner=item.parentless?nullptr:parent.get();
        const QPointer<QWidget> originalParent(owner);
        QPointer<WizMessageBox> question;
        const QString title=item.appTitle?app.applicationName():QString::fromUtf8("合成提示 / Original title");
#ifdef Q_OS_MAC
        // QMessageBox::setWindowTitle deliberately ignores titles on macOS.
        const QString expectedTitle;
#else
        const QString expectedTitle=title;
#endif
        const QString text=QString::fromUtf8("合成正文与数学 x² / Original message");
        bool observed=false,attributes=false,timedOut=false;
        QTimer poll,deadline;deadline.setSingleShot(true);
        QObject::connect(&poll,&QTimer::timeout,&app,[&] {
            auto* box=qobject_cast<WizMessageBox*>(QApplication::activeModalWidget());
            if(!box)return;
            poll.stop();question=box;observed=true;
            attributes=box->parentWidget()==owner&&box->windowTitle()==expectedTitle&&box->text()==text
                &&box->icon()==item.kind&&box->standardButtons()==item.buttons&&box->isModal()
                &&box->defaultButton()&&box->standardButton(box->defaultButton())==item.defaultButton;
            for(Button button:{QMessageBox::Ok,QMessageBox::Cancel,QMessageBox::Yes,QMessageBox::No})
                if(item.buttons.testFlag(button))attributes&=box->button(button)&&box->button(button)->text()==label(button);
            if(!attributes)qInfo()<<"ATTRIBUTE MISMATCH"<<"parent"<<(box->parentWidget()==owner)
                <<"title"<<box->windowTitle()<<"text"<<(box->text()==text)<<"icon"<<box->icon()
                <<"buttons"<<box->standardButtons()<<"modal"<<box->isModal()
                <<"default"<<(box->defaultButton()?box->standardButton(box->defaultButton()):QMessageBox::NoButton);
            if(destroyParent) {
                qInfo()<<"DESTROYING SYNTHETIC PARENT"<<index;
                parent.reset();
            } else if(auto* button=box->button(item.choice))button->click();
        });
        QObject::connect(&deadline,&QTimer::timeout,&app,[&] {
            timedOut=true;poll.stop();
            if(auto* modal=qobject_cast<QDialog*>(QApplication::activeModalWidget()))modal->reject();
        });
        poll.start(5);deadline.start(2000);
        const auto result=invoke(item,owner,title,text);
        poll.stop();deadline.stop();
        check(observed&&!timedOut&&attributes,"original platform title policy, icon/text/buttons/default/localized labels and modality preserved");
        check(result==(destroyParent?QMessageBox::NoButton:item.choice),"returned original clicked choice or explicit no-button after parent destruction");
        check(question.isNull()&&(!destroyParent||originalParent.isNull())&&!QApplication::activeModalWidget(),
            "dialog and requested parent cleaned up without a remaining modal");
    }
    qInfo()<<"Message-box checks:"<<checks<<"failures:"<<failures;
    return failures?1:0;
}
