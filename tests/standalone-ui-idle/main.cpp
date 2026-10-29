#include <QApplication>
#include <QComboBox>
#include <QTimer>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>
#include <functional>
static QElapsedTimer clockForTest;
static qint64 WizGetTickCount(){return clockForTest.elapsed()+1000;}
static void wait(int ms){QEventLoop loop;QTimer::singleShot(ms,&loop,&QEventLoop::quit);loop.exec();}
class ToolbarHarness:public QObject{
public:
 QString m_currentStyle; qint64 m_lastUpdateUIRequest=0; QTimer m_delayUpdateUITimer;
 int wakes=0,resets=0;QString applied;bool reenter=false;
 ToolbarHarness(){m_delayUpdateUITimer.setInterval(100);QObject::connect(&m_delayUpdateUITimer,&QTimer::timeout,this,[this]{++wakes;on_delay_updateToolbar();});}
 void on_delegate_selectionChanged(const QString& currentStyle);void on_delay_updateToolbar();
 void resetToolbar(const QString& style){++resets;applied=style;if(reenter){reenter=false;on_delegate_selectionChanged("reentrant");}}
};
#include "toolbar.inc"
class ComboHarness:public QComboBox{
public:
 QString m_strText;int paints=0;QString painted;
 #include "combo.inc"
 void paintEvent(QPaintEvent* e)override{++paints;painted=m_strText;QComboBox::paintEvent(e);}
};
class ShortcutHarness:public QObject{
public:
 QTimer* m_timerSaveShortcutState=nullptr;int saves=0;QString current,saved;
 void scheduleShortcutSave();
 void saveShortcutState(){
 #include "shortcut-save-preamble.inc"
 ++saves;saved=current;}

};
#include "shortcut.inc"
int main(int argc,char**argv){
 QCoreApplication::setAttribute(Qt::AA_PluginApplication);QApplication app(argc,argv);clockForTest.start();bool candidate=app.arguments().contains("candidate");QString route=app.arguments().value(1);int checks=0,failures=0;QJsonObject values;
 auto check=[&](bool b,const char* n){++checks;if(!b){++failures;qWarning()<<"FAIL"<<n;}};
 if(route=="timer"){
  ToolbarHarness t;t.on_delegate_selectionChanged("one");wait(125);check(t.resets==0,"quiet period preserved");t.on_delegate_selectionChanged("two");wait(420);check(t.resets==1&&t.applied=="two","latest style coalesced");int w=t.wakes;wait(450);values["idleWakeupsIn450ms"]=t.wakes-w;check(candidate?t.wakes==w:t.wakes>w,"idle behavior matches phase");
  t.on_delegate_selectionChanged("two");w=t.wakes;wait(130);check(!candidate||t.wakes==w,"unchanged status does not restart timer");
  t.on_delegate_selectionChanged("");wait(130);check(!candidate||!t.m_delayUpdateUITimer.isActive(),"empty state stops idle timer");
  ToolbarHarness re;re.reenter=true;re.on_delegate_selectionChanged("first");wait(850);values["reentrantResets"]=re.resets;check(candidate?(re.resets==2&&re.applied=="reentrant"):(re.resets==1),"reentrant new style retained in candidate");
  auto* dead=new ToolbarHarness;dead->on_delegate_selectionChanged("gone");delete dead;wait(150);check(true,"timer destruction safe");
 }
 if(route=="combo"){
  ComboHarness c;c.addItems({"Body","Heading","12","14"});int activated=0;QObject::connect(&c,qOverload<int>(&QComboBox::activated),[&](int){++activated;});c.resize(150,30);c.show();c.setText("Body");wait(60);c.paints=0;
  for(int i=0;i<100;++i)c.setText("Body");values["sameTextImmediatePaints"]=c.paints;check(candidate?c.paints==0:c.paints==100,"same text repaint qualification");wait(60);check(c.painted=="Body","unchanged visible text remains");
  c.paints=0;for(int i=0;i<50;++i)c.setText(i%2?"Heading":"Body");values["changedTextImmediatePaints"]=c.paints;check(candidate?c.paints==0:c.paints==50,"changed text queued painting");wait(60);values["changedTextPaintsAfterEventLoop"]=c.paints;check(c.painted=="Heading"&&c.currentIndex()==1,"latest text and selection shown");
  c.setText("unlisted");wait(60);check(c.m_strText=="unlisted"&&c.painted=="unlisted"&&c.currentIndex()==1,"unknown display keeps existing index");c.setCurrentIndex(3);c.setText("Heading");wait(60);check(c.currentIndex()==1&&c.painted=="Heading","selection repair not skipped");c.setText("");wait(60);check(c.m_strText.isEmpty()&&c.painted.isEmpty(),"empty display supported");check(activated==0,"programmatic updates do not activate edit commands");
 }
 if(route=="shortcut"){
  ShortcutHarness h;for(int i=0;i<100;++i){h.current=QString::number(i);h.scheduleShortcutSave();}wait(260);values["savesFor100Requests"]=h.saves;check(candidate?h.saves==1:h.saves==100,"shortcut burst coalesced");check(h.saved=="99","save observes final state");h.current="later";h.scheduleShortcutSave();wait(260);check(h.saved=="later","next batch not lost");int n=h.saves;wait(240);check(h.saves==n,"no idle save work");h.current="immediate";h.scheduleShortcutSave();h.saveShortcutState();wait(260);values["savesAfterImmediateFlush"]=h.saves-n;check(candidate?h.saves==n+1:h.saves==n+2,"immediate flush cancels duplicate");auto* d=new ShortcutHarness;d->scheduleShortcutSave();delete d;wait(240);check(true,"owner-bound callback retired");
 }
 values["checks"]=checks;values["failures"]=failures;values["route"]=route;values["candidate"]=candidate;qInfo().noquote()<<QJsonDocument(values).toJson(QJsonDocument::Compact);return failures?1:0;
}
