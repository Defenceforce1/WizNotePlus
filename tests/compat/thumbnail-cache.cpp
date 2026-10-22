#include "WizThumbCache.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "share/WizGlobal.h"
#include "share/WizThreads.h"
#include <QApplication>
#include <QTemporaryDir>
#include <QSettings>
#include <QElapsedTimer>
#include <QThread>
#include <QSemaphore>
#include <QDebug>
#include <memory>

int wizThumbnailCacheSelfTest(int argc,char** argv)
{
    QTemporaryDir root;if(!root.isValid())return 2;
    qputenv("WIZNOTE_DATA_STORE",root.filePath("store").toUtf8());qputenv("XDG_CACHE_HOME",root.filePath("cache").toUtf8());
    QApplication app(argc,argv);app.setQuitOnLastWindowClosed(false);QSettings settings(root.filePath("ui.ini"),QSettings::IniFormat),global(root.filePath("global.ini"),QSettings::IniFormat);
    WizGlobal::setSettings(&settings);WizGlobal::setGlobalSettings(&global);
    int checks=0,failures=0;const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;};
    const auto until=[&](const std::function<bool()>& ready){QElapsedTimer timer;timer.start();while(!ready()&&timer.elapsed()<5000){app.processEvents();QThread::msleep(2);}return ready();};
    WizDatabaseManager manager("synthetic-thumbnail-only");if(!manager.open())return 2;WizQueuedThreadsInit();
    WIZABSTRACT source;source.guid="synthetic-preview";source.text="Synthetic preview only";source.image=QImage(64,48,QImage::Format_ARGB32);source.image.fill(Qt::red);
    check(manager.db().updatePadAbstract(source),"synthetic preview is stored in the actual temporary thumbnail index");
    WIZABSTRACT persisted;const bool readBack=manager.db().padAbstractFromGuid(source.guid,persisted);
    auto cache=std::make_unique<WizThumbCache>();int loaded=0;bool wrongThread=false;
    QObject::connect(cache.get(),&WizThumbCache::loaded,&app,[&](const QString&,const QString&){++loaded;wrongThread|=QThread::currentThread()!=app.thread();});
    WIZABSTRACT result;for(int i=0;i<500;++i)WizThumbCache::find({},source.guid,result);
    check(readBack&&until([&]{return WizThumbCache::find({},source.guid,result);})&&result.text==persisted.text&&result.image==persisted.image,"actual queued cache preserves the exact thumbnail stored by the existing index");
    check(WizThumbCache::cacheStatistics().value("loads").toULongLong()==1,"500 actual repaint lookups create only one database load");
    check(until([&]{return loaded==1;})&&!wrongThread,"actual completion signal reaches the GUI thread once");
    const auto bytes=WizThumbCache::cacheStatistics().value("costKiB").toInt();
    check(bytes>0&&bytes<=16*1024,"actual preview cache reports bounded estimated retained cost");
    source.text="Changed synthetic preview";source.image.fill(Qt::blue);check(manager.db().updatePadAbstract(source),"synthetic index update succeeds without a document body or note save");
    WIZDOCUMENTDATA doc;doc.strGUID=source.guid;manager.documentAbstractModified(doc);
    check(until([&]{return WizThumbCache::find({},source.guid,result)&&result.text==source.text;})&&result.image.pixelColor(0,0)==QColor(Qt::blue),"actual invalidation reloads changed text and image rather than old cache data");
    check(until([&]{return loaded==2;})&&WizThumbCache::cacheStatistics().value("loads").toULongLong()==2,"one abstract change causes only one replacement load");
    // Block our own default queue before enqueueing another cache read, then
    // destroy its owner. It must never dereference the retired QObject.
    auto entered=std::make_shared<QSemaphore>(),release=std::make_shared<QSemaphore>();
    WizExecuteOnThread(WIZ_THREAD_DEFAULT,[entered,release]{entered->release();release->tryAcquire(1,5000);});
    const bool blocked=entered->tryAcquire(1,3000);WizThumbCache::find({},"never-loaded-synthetic",result);cache.reset();release->release();
    check(blocked&&!WizThumbCache::instance()&&!WizThumbCache::find({},source.guid,result),"destroyed cache has no singleton or usable entry while its old request is queued");
    WizQueuedThreadsShutdown();app.processEvents();check(loaded==2,"retired queued load emits no late completion or dangling-owner signal");
    qInfo()<<"Thumbnail cache checks"<<checks<<"failures"<<failures;return failures?1:0;
}
