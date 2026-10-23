// The existing temporary-database fixture, without the normal client entry.
#include "database/WizDatabaseManager.h"
#include <QCoreApplication>
#include <QDebug>
#include <memory>
int wizThumbnailCacheSelfTest(int,char**);
int main(int argc,char** argv){
    if(argc==2&&QByteArray(argv[1])=="--closed-manager"){
        QCoreApplication app(argc,argv);int checks=0,failures=0;
        const auto check=[&](bool v,const char* m){++checks;failures+=!v;qInfo().noquote()<<(v?"PASS":"FAIL")<<m;};
        auto manager=std::make_unique<WizDatabaseManager>("synthetic-unopened-only");
        check(!manager->isOpened(),"new synthetic manager has no open database");
        check(!manager->close(),"explicit close of an unopened database returns false without dereferencing it");
        manager->closeAll();check(!manager->isOpened(),"closeAll of unopened state is harmless");
        manager.reset();check(!WizDatabaseManager::instance(),"unopened manager destruction releases its singleton without crashing");
        qInfo()<<"Closed manager checks"<<checks<<"failures"<<failures;return failures?1:0;
    }
    return wizThumbnailCacheSelfTest(argc,argv);
}
