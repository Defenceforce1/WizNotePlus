#include "share/WizProgressiveSearch.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QThread>
#include <QStringList>
#include <cstdio>

int main(int argc,char** argv) {
    QCoreApplication app(argc,argv);
    using Pipeline=WizProgressiveSearch<QStringList>;
    Pipeline search;
    int checks=0,failed=0;
    auto check=[&](bool ok,const char* name){++checks;failed+=!ok;std::printf("%s %s\n",ok?"PASS":"FAIL",name);};
    const auto wait=[&](const auto& done){
        QElapsedTimer elapsed; elapsed.start();
        while(!done()&&elapsed.elapsed()<2000){app.processEvents();QThread::msleep(1);}
        app.processEvents();
        return done();
    };
    const auto immediate=[](const QString& text) {
        return [text](QStringList& result,const auto&) {result={text};return true;};
    };
    QStringList delivered;
    const auto publish=[&](bool ok,QStringList result){delivered<< (ok?result.join(','):"unavailable");};
    QSemaphore remote;
    std::atomic_bool started{false};
    search.start(immediate("title"),[&](QStringList& results,const auto& canceled){
        started=true;
        while(!remote.tryAcquire(1,5)&&!canceled()) {}
        if(canceled()) return false;
        results={"body"};
        return true;
    },publish,publish);
    check(wait([&]{return started&&delivered.size()==1;})&&delivered==QStringList{"title"},
          "slow full text cannot hold back local titles");
    remote.release();
    check(wait([&]{return delivered.size()==2;})&&delivered==QStringList{"title","body"},
          "full-text-only match delivered after titles");

    delivered.clear();
    QSemaphore local;
    std::atomic_bool fast{false};
    search.start([&](QStringList& results,const auto& canceled){
        while(!local.tryAcquire(1,5)&&!canceled()) {}
        results={"title"};
        return !canceled();
    },[&](QStringList& results,const auto&){results={"body"};fast=true;return true;},publish,publish);
    check(wait([&]{return fast.load();})&&delivered.isEmpty(),"early cloud result waits for ordered title delivery");
    local.release();
    check(wait([&]{return delivered.size()==2;})&&delivered==QStringList{"title","body"},
          "buffered cloud completion is delivered after local completion");

    delivered.clear();
    search.start(immediate("title"),[](QStringList&,const auto&) {return false;},publish,publish);
    check(wait([&]{return delivered.size()==2;})&&delivered==QStringList{"title","unavailable"},
          "offline completion reports unavailable without replacing title results");

    delivered.clear();
    started=false;
    std::atomic_bool canceledOld{false};
    search.start(immediate("old title"),[&](QStringList& results,const auto& canceled){
        started=true;
        while(!canceled()) QThread::msleep(5);
        canceledOld=true;
        results={"obsolete cloud"};
        return true; // A cancellation-ignorant publisher must also be ignored.
    },publish,publish);
    check(wait([&]{return started&&delivered.size()==1;}),"old query is in-flight before replacement");
    search.start(immediate("new title"),immediate("new body"),publish,publish);
    check(wait([&]{return canceledOld&&delivered.size()==3;})&&delivered==QStringList{"old title","new title","new body"},
          "replacement cancels old cloud work and suppresses its result");

    delivered.clear();
    started=false;canceledOld=false;
    search.start(immediate("local first"),[&](QStringList& r,const auto& canceled){
        started=true;while(!canceled())QThread::msleep(5);
        canceledOld=true;r={"retired"};return true;
    },publish,publish);
    check(wait([&]{return started&&delivered.size()==1;}),"in-flight request before clear or scope change");
    search.cancelAndWait();
    app.processEvents();
    check(!search.active()&&canceledOld&&delivered==QStringList{"local first"},
          "clear or scope change and quit drains workers without late publication");

    delivered.clear();
    search.start(immediate("filtered"),{},publish,publish);
    check(wait([&]{return delivered.size()==1;})&&delivered==QStringList{"filtered"},"filter without a keyword uses only local results");
    search.cancelAndWait();
    std::printf("%d checks; %d failures\n",checks,failed);
    return failed?1:0;
}
