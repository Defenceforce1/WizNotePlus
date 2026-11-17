#include "share/WizQuitUiGuard.h"
#include "share/WizQuitSyncPolicy.h"
#include <QApplication>
#include <QMainWindow>
#include <QWindow>
#include <QMap>
#include <QElapsedTimer>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>
#include <QDebug>
#include <functional>

// Compile the actual prepareForQuit body. Only the surrounding services are
// synthetic; disk/snapshot implementations have their separate native checks.
struct View : QWidget {
    int calls=0, writes=0; bool dirty=false, fail=false, forced=false;
    std::function<void()> onSave;
    bool waitForSave(bool force) {
        ++calls; forced |= force;
        if(onSave) onSave();
        if(fail) return false;
        if(force || dirty) { ++writes; dirty=false; }
        return true;
    }
};
struct Saver {
    bool idle=true, failed=false; int resets=0;
    bool isIdleForTransfer() const {return idle;}
    void clearTransferError() {++resets;}
    bool hasTransferError() const {return failed;}
};
struct Sync {
    int starts=0, stops=0, polls=0, wakes=0; bool running=true, idle=true, started=true; quint64 activity=0;
    void startSyncAll(bool) {++starts;}
    void stopSync() {++stops; running=false;}
    void wakePendingSync() {++wakes;}
    bool hasNetworkStarted() const {return started;}
    quint64 networkActivity() const {return activity;}
    bool isRunning() {++polls; return running;}
    bool isIdle() {++polls; return idle;}
};
struct External {
    int checks=0; bool valid=true;
    bool verifyExternalChangesForQuit() {++checks;return valid;}
};
struct Viewer {View* view; View* docView() {return view;}};
struct ViewerManager {
    QMap<int,Viewer*> views;
    QMap<int,Viewer*> getDocumentViewerMap() {return views;}
};
struct Message {static inline int failures=0; static void critical(QWidget*,const QString&,const QString&) {++failures;}};
namespace QuitNetwork {inline int shutdowns=0;inline void beginShutdown(){++shutdowns;}}
struct QuitShell : QMainWindow {
    bool m_quitPrepared=false,m_preparingQuit=false,m_noteTransferBusy=false,m_quiting=false;
    QList<QPointer<View>> views;
    ViewerManager manager; ViewerManager* m_singleViewMgr=&manager;
    Saver* m_docSaver=nullptr;External* m_externalEditorLauncher=nullptr;
    Sync* m_syncFull=nullptr;Sync* m_syncQuick=nullptr;
    void processAllDocumentViews(const std::function<void(View*)>& f){for(const auto& v:views)if(v)f(v);}
    bool prepareForQuit();
};
#include "quit-flow-under-test.inc"

int main(int argc,char** argv) {
    QApplication app(argc,argv);
    int checks=0,failures=0;
    const auto check=[&](bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;};
    {
        QuitShell q;View clean,dirty,standalone;dirty.dirty=true;standalone.dirty=true;
        Viewer viewer{&standalone};q.manager.views.insert(1,&viewer);q.views={&clean,&dirty};
        Sync full,quick;q.m_syncFull=&full;q.m_syncQuick=&quick;
        Saver saver;q.m_docSaver=&saver;QElapsedTimer timer;timer.start();
        check(q.prepareForQuit(),"actual coordinator accepts confirmed local persistence");
        check(clean.writes==0&&dirty.writes==1&&standalone.writes==1,
              "unchanged note is skipped; changed tab and standalone view each save once");
        check(!clean.forced&&!dirty.forced&&!standalone.forced&&clean.calls==2&&dirty.calls==2,
              "both save checks use dirty state without forced resnapshots");
        check(full.starts==0&&quick.starts==0&&full.wakes==0&&quick.wakes==0&&timer.elapsed()<500,
              "idle sync adds no fixed wait or new full sync attempt");
        check(full.stops==1&&quick.stops==1,"existing sync workers receive cooperative stop after local success");
        const auto calls=dirty.calls;
        check(q.prepareForQuit()&&dirty.calls==calls&&full.stops==1,"repeated committed quit is idempotent");
    }
    {
        QuitShell q;View v;v.dirty=true;q.views={&v};Sync full,quick;
        q.m_syncFull=&full;q.m_syncQuick=&quick;quick.idle=false;
        QTimer progress;QObject::connect(&progress,&QTimer::timeout,&q,[&]{++quick.activity;});progress.start(200);
        QTimer::singleShot(3400,&q,[&]{quick.idle=true;});QElapsedTimer elapsed;elapsed.start();
        check(q.prepareForQuit()&&elapsed.elapsed()>=3300&&elapsed.elapsed()<5000&&quick.activity>5,
              "healthy ongoing sync completes beyond three seconds without truncating upload");
        check(v.writes==1&&full.starts==0&&quick.wakes==1&&quick.stops==1,
              "pending quick sync is awakened without starting a full-account sync");
    }
    {
        QuitShell q;View v;v.dirty=true;q.views={&v};Sync quick;quick.idle=false;quick.started=false;q.m_syncQuick=&quick;
        QElapsedTimer elapsed;elapsed.start();
        check(q.prepareForQuit()&&elapsed.elapsed()>=3000&&elapsed.elapsed()<4500&&v.writes==1,
              "unresponsive network ends waiting after local save with no retry");
        check(quick.wakes==1&&quick.starts==0&&quick.stops==1,"stalled network does not restart during final save check");
    }
    {
        QuitShell q;View v;v.dirty=true;q.views={&v};Sync quick;quick.idle=false;q.m_syncQuick=&quick;Saver saver;q.m_docSaver=&saver;
        bool lastWriteDone=false;
        v.onSave=[&]{if(v.calls==2){saver.idle=false;QTimer::singleShot(100,&q,[&]{lastWriteDone=true;saver.idle=true;});}};
        QWindow nativeInput; // Native keys enter through QWindow before QWidget delivery.
        QTimer::singleShot(40,&q,[&]{
            const auto key=QKeySequence("Ctrl+Q")[0];
            QKeyEvent override(QEvent::ShortcutOverride,key.key(),key.keyboardModifiers());QCoreApplication::sendEvent(&nativeInput,&override);
            QKeyEvent press(QEvent::KeyPress,key.key(),key.keyboardModifiers());QCoreApplication::sendEvent(&nativeInput,&press);
        });
        QElapsedTimer elapsed;elapsed.start();
        const bool accepted=q.prepareForQuit();
        check(accepted&&elapsed.elapsed()<1000&&lastWriteDone&&quick.stops==1,
              "repeated Quit skips network promptly but still drains final local disk work");
    }
    {
        QuitShell q;View v;v.dirty=true;q.views={&v};Sync quick;quick.idle=false;q.m_syncQuick=&quick;
        v.onSave=[&]{if(v.calls==2){quick.idle=false;QTimer::singleShot(60,&q,[&]{quick.idle=true;});}};
        QTimer::singleShot(40,&q,[&]{v.dirty=true;quick.idle=true;});
        check(q.prepareForQuit()&&v.writes==2&&quick.wakes==2&&quick.idle&&quick.starts==0,
              "late dirty content is persisted and its queued quick sync completes on healthy network");
    }
    {
        QObject receiver;WizQuitSyncPolicy::CancelOnRepeatQuit filter(qApp);
        const auto key=QKeySequence("Ctrl+Q")[0];
        QKeyEvent held(QEvent::KeyPress,key.key(),key.keyboardModifiers(),QString(),true);
        QCoreApplication::sendEvent(&receiver,&held);
        check(!filter.requested(),"holding initial Quit does not accidentally skip synchronization");
        QEvent quit(QEvent::Quit);QCoreApplication::sendEvent(qApp,&quit);
        check(filter.requested(),"repeated application Quit is intercepted without recursive shutdown");
    }
    {
        QuitShell q;View v;v.dirty=true;q.views={&v};Saver saver;q.m_docSaver=&saver;
        bool diskDone=false,lateEdit=false;
        v.onSave=[&]{if(v.calls==1){saver.idle=false;QTimer::singleShot(35,&q,[&]{diskDone=true;saver.idle=true;v.dirty=true;lateEdit=true;});}};
        check(q.prepareForQuit()&&diskDone&&lateEdit&&v.writes==2&&!v.dirty,
              "disk wait pumps completion and the final pass saves a late modification");
    }
    {
        QuitShell q;View v;v.dirty=true;q.views={&v};Saver saver;saver.idle=false;q.m_docSaver=&saver;
        bool diskDone=false;v.onSave=[&]{check(diskDone,"previous queued disk write completes before capturing current notes");};
        QTimer::singleShot(35,&q,[&]{diskDone=true;saver.idle=true;});
        check(q.prepareForQuit()&&v.writes==1,"preexisting saver work drains normally");
    }
    for(bool diskFailure:{false,true}) {
        QuitShell q;q.setCentralWidget(new QWidget);View v;v.dirty=true;q.views={&v};
        QAction action(&q);Sync full;q.m_syncFull=&full;Saver saver;q.m_docSaver=&saver;
        v.fail=!diskFailure;v.onSave=[&]{if(diskFailure)saver.failed=true;};
        const int messages=Message::failures,shutdowns=QuitNetwork::shutdowns;
        check(!q.prepareForQuit()&&!q.m_quitPrepared&&!q.m_quiting&&v.isEnabled()&&action.isEnabled(),
              diskFailure?"disk failure cancels exit and restores input":"snapshot failure cancels exit and restores input");
        check(full.stops==0&&QuitNetwork::shutdowns==shutdowns&&Message::failures==messages+1,
              "save failure does not commit network shutdown");
        saver.failed=false;v.fail=false;v.dirty=true;v.onSave={};
        check(q.prepareForQuit()&&full.stops==1,"explicit retry can save and quit after failure");
    }
    {
        QuitShell q;View v;q.views={&v};External external;q.m_externalEditorLauncher=&external;
        Sync full;q.m_syncFull=&full;
        v.onSave=[&]{if(v.calls==2)external.valid=false;};
        check(!q.prepareForQuit()&&external.checks==2&&full.stops==0,
              "late unadmitted external edit still blocks exit");
    }
    {
        QuitShell q;View v;q.views={&v};q.m_noteTransferBusy=true;
        check(!q.prepareForQuit()&&v.calls==0,"active note transfer blocks reentrant quit before save work");
        q.m_noteTransferBusy=false;q.m_preparingQuit=true;
        check(!q.prepareForQuit()&&v.calls==0,"nested quit is rejected without duplicate saves");
    }
    qInfo()<<"Quit flow checks:"<<checks<<"failures:"<<failures;
    return failures?1:0;
}
