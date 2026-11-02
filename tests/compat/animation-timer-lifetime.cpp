// Actual WizAnimateAction lifecycle regression; output must be a synthetic directory.
#include "share/WizAnimateAction.h"
#include <QApplication>
#include <QTimer>
#include <QPointer>
#include <QAction>
#include <QToolButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <cstdio>
#include <unistd.h>
static QPointer<QTimer> timers[110];static int made=0,ticks=0;
static bool observe(WizAnimateAction* a){auto* t=a->findChild<QTimer*>();if(!t||made>=110)return false;timers[made++]=t;QObject::connect(t,&QTimer::timeout,qApp,[]{ticks++;});return true;}
int main(int argc,char**argv){if(argc!=2)return 2;QCoreApplication::setAttribute(Qt::AA_PluginApplication);QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);int checks=0;bool pass=true;auto check=[&](bool b){checks++;pass&=b;};
QPixmap px(16,16);px.fill(Qt::blue);QAction action;action.setIcon(QIcon(px));const auto iconKey=action.icon().cacheKey();auto* owner=new QObject;auto* anim=new WizAnimateAction(owner);if(!observe(anim))return 10;anim->setAction(&action);check(!anim->isPlaying());check(timers[0]&&timers[0]->interval()==100);anim->startPlay();check(anim->isPlaying());check(action.property("animationStatus").toInt()==1);
QTimer::singleShot(340,&app,[&]{check(ticks>=2);anim->stopPlay();check(!anim->isPlaying());check(action.property("animationStatus").toInt()==0);check(action.icon().cacheKey()==iconKey);const int stoppedTicks=ticks;
 QTimer::singleShot(220,&app,[&,stoppedTicks]{check(ticks==stoppedTicks);anim->startPlay();check(anim->isPlaying());delete owner;check(timers[0].isNull());
  for(int i=0;i<100;i++){QToolButton button;button.setIcon(QIcon(px));QObject parent;auto* a=new WizAnimateAction(&parent);if(!observe(a))_exit(10);a->setToolButton(&button);a->startPlay();check(a->isPlaying());if(i%2==0){a->stopPlay();check(!a->isPlaying());}delete a;check(timers[made-1].isNull());}
  const int before=ticks;QTimer::singleShot(220,&app,[&,before]{check(ticks==before);int alive=0;for(int i=0;i<made;i++)if(timers[i])alive++;check(alive==0);QJsonObject r{{"passed",pass},{"checks",checks},{"created",made},{"aliveAfterOwnerDestruction",alive},{"timerIntervalMs",100},{"actualAnimationClass",true}};QFile f(QString::fromLocal8Bit(argv[1])+"/result.json");if(!f.open(QIODevice::WriteOnly))_exit(9);f.write(QJsonDocument(r).toJson());f.close();for(int i=0;i<made;i++)if(timers[i])delete timers[i].data();app.exit(pass?0:1);});
 });
});return app.exec();}
