#include "synthetic-input-guard.h"
#include <QApplication>
#include <QLineEdit>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QDebug>

int main(int argc,char** argv) {
    QApplication app(argc,argv);QLineEdit field;field.setText("fixture");field.setCursorPosition(7);
    int checks=0,failures=0;
    auto check=[&](bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;};
    struct MouseReceiver final : QObject {
        int presses=0;
        bool event(QEvent* event) override {if(event->type()==QEvent::MouseButtonPress){++presses;return true;}return QObject::event(event);}
    } receiver;
    {
        ScopedSyntheticInputGuard guard(app);
        QKeyEvent key(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");
        QCoreApplication::sendEvent(&field,&key);
        check(field.text()=="fixture","isolated fixture ignores keyboard text without recording it");
        QInputMethodEvent input;input.setCommitString(QStringLiteral("测试"));QCoreApplication::sendEvent(&field,&input);
        check(field.text()=="fixture","isolated fixture ignores input-method commits");
        QMouseEvent click(QEvent::MouseButtonPress,QPointF(1,1),QPointF(1,1),Qt::RightButton,Qt::RightButton,Qt::NoModifier);
        QCoreApplication::sendEvent(&receiver,&click);
        check(receiver.presses==1,"scripted mouse probes remain usable");
        check(guard.blockedEvents()>=2,"guard counts blocked events without saving their contents");
        field.setText("scripted fixture");field.setCursorPosition(field.text().size());
        check(field.text()=="scripted fixture","programmatic fixture edits are unaffected");
    }
    QKeyEvent key(QEvent::KeyPress,Qt::Key_X,Qt::NoModifier,"x");QCoreApplication::sendEvent(&field,&key);
    check(field.text()=="scripted fixturex","leaving the test guard restores ordinary input handling");
    qInfo()<<"Synthetic input guard checks:"<<checks<<"failures:"<<failures;
    return failures?1:0;
}
