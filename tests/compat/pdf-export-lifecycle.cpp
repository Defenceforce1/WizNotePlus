#include "share/WizPdfExportRequest.h"
#include "share/WizPdfSvgLinks.h"
#include "share/WizPdfPageOperations.h"
#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QTemporaryDir>
#include <QTimer>
#include <QWebEngineProfile>
#include <functional>
#include <array>

namespace {
QVariant evaluate(QWebEnginePage* page,const QString& source,quint32 world=0) {
    struct State { QVariant value;bool done=false; };
    auto state=std::make_shared<State>();QEventLoop loop;QPointer<QEventLoop> guard(&loop);QTimer timer;
    timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);
    page->runJavaScript(source,world,[state,guard](const QVariant& value){state->value=value;state->done=true;if(guard)guard->quit();});
    timer.start(10000);if(!state->done)loop.exec();return state->value;
}
bool load(QWebEnginePage* page,const QString& html) {
    QEventLoop loop;QTimer timer;bool ok=false;
    QObject::connect(page,&QWebEnginePage::loadFinished,&loop,[&](bool loaded){ok=loaded;loop.quit();});
    timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);
    page->setHtml(html);timer.start(10000);loop.exec();return ok;
}
QString html() {
    return QStringLiteral(R"HTML(<!doctype html><meta charset="utf-8"><div class="MathJax_SVG">
      <svg xmlns="http://www.w3.org/2000/svg" xmlns:xlink="http://www.w3.org/1999/xlink" width="400" height="150">
        <a id="legacy" xlink:href="#mjx-eqn-eq%3Atest"><text x="10" y="20">Equation reference</text></a>
        <g id="mjx-eqn-eq:test"><text x="10" y="60">x = 1</text></g>
        <a id="modern" href="#mjx-eqn-eq:test" xlink:href="#mjx-eqn-other"><text x="10" y="90">Modern</text></a>
        <a id="external" xlink:href="https://example.invalid/#mjx-eqn-eq:test"><text>External</text></a>
        <a id="ordinary" xlink:href="#ordinary-target"><text>Ordinary</text></a>
        <g id="ordinary-target"></g><a id="invalid" xlink:href="#mjx-eqn-%ZZ"><text>Invalid</text></a>
      </svg></div><p id="replacement">Synthetic local page</p><script>
      window.WizNativeEditorInitialized=true;window.WizNativeEditorSetup={state:'ready'};
      window.WizReader={closeDocument:()=>document.body.innerHTML};
      window.WizEditor={getContentHtml:()=>document.body.innerHTML,isModified:()=>true,setUnModified:()=>{}};
      </script>)HTML");
}
QVariantMap status(QWebEnginePage* page) {
    return evaluate(page,"globalThis.__wizPdfSvgLinks?.status()",QWebEngineScript::ApplicationWorld).toMap();
}
struct Printed {
    bool done=false,pageDestroyed=false;QByteArray bytes;QString error;
    WizPdfExportRequest::Result result=WizPdfExportRequest::Result::Canceled;
};
std::shared_ptr<Printed> print(QWebEnginePage* page,const WizPdfExportRequest::Ticket& ticket,
                               const QString& destination,const std::function<void()>& during={},
                               WizPdfPageOperations* shared=nullptr) {
    auto state=std::make_shared<Printed>();QEventLoop loop;QPointer<QEventLoop> guard(&loop);QTimer timer;
    WizPdfPageOperations local;auto& operations=shared?*shared:local;
    QObject::connect(page,&QObject::destroyed,&loop,[state,&loop]{state->pageDestroyed=true;loop.quit();});
    timer.setSingleShot(true);QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);
    operations.print(page,[ticket]{return WizPdfExportRequest::current(ticket);},
        WizPdfExport::pageLayout(0,QMarginsF(5,5,5,5)),[state,guard,ticket,destination](const QByteArray& bytes){
        state->bytes=bytes;state->result=WizPdfExportRequest::complete(ticket,destination,bytes,state->error);
        state->done=true;if(guard)guard->quit();
    });
    if(during)during();timer.start(20000);if(!state->done&&!state->pageDestroyed)loop.exec();return state;
}
QByteArray read(const QString& name) {QFile f(name);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
bool sentinel(const QString& name) {QFile f(name);return f.open(QIODevice::WriteOnly)&&f.write("original-destination")==20;}
}
int main(int argc,char** argv) {
    QTemporaryDir root;if(!root.isValid())return 2;
    qputenv("XDG_CACHE_HOME",root.filePath("cache").toUtf8());
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc,argv);app.setApplicationName("WizPdfExportLifecycleTests");
    QWebEngineProfile profile;
    auto page=std::make_unique<QWebEnginePage>(&profile);
    WizPdfSvgLinks::install(page.get());WizPdfSvgLinks::install(page.get());
    int count=0,failures=0;
    const auto check=[&](bool ok,const char* label){++count;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;};
    check(page->scripts().find("wiz-pdf-svg-links").size()==1,"print script installs once per page in its isolated world");
    check(load(page.get(),html()),"synthetic SVG page loads without an account or remote resource");
    const QString before=evaluate(page.get(),"document.body.innerHTML").toString();
    check(evaluate(page.get(),"typeof globalThis.__wizPdfSvgLinks").toString()=="undefined","application print state is not an editor-world global");
    evaluate(page.get(),"dispatchEvent(new Event('beforeprint'));true");
    check(status(page.get()).value("prepares").toInt()==0,"untrusted page events cannot start print augmentation");
    QObject owner;WizDocumentCallbackEpoch document;WizPdfExportRequest request;
    auto ticket=request.begin(&owner,page.get(),document.ticket());
    check(WizPdfExportRequest::current(ticket),"export reserves a live owner page and document before opening a dialog");
    check(!WizPdfExportRequest::current(request.begin(&owner,page.get(),document.ticket())),"overlapping export cannot reserve a second dialog or output");
    auto first=print(page.get(),ticket,root.filePath("first.pdf"));
    check(first->done&&first->result==WizPdfExportRequest::Result::Saved,"actual native PDF completes through the production atomic writer");
    check(read(root.filePath("first.pdf"))==first->bytes,"committed file exactly matches complete renderer bytes");
    auto events=status(page.get());
    check(events.value("prepares").toInt()==1&&events.value("restores").toInt()==1&&events.value("lastAdded").toInt()==1,
          "trusted print events augment only the eligible legacy equation link");
    check(!events.value("active").toBool()&&!events.value("pending").toInt()&&!events.value("errors").toInt(),"print leaves no pending attribute transaction or restore error");
    check(evaluate(page.get(),"document.body.innerHTML").toString()==before,"printing restores exact HTML including all existing link attributes");
    check(evaluate(page.get(),"document.getElementById('legacy').hasAttributeNS(null,'href')").toBool()==false,"temporary modern attribute is absent again after printing");
    QString error;
    check(WizPdfExportRequest::complete(ticket,root.filePath("duplicate.pdf"),first->bytes,error)==WizPdfExportRequest::Result::Canceled
          &&!QFile::exists(root.filePath("duplicate.pdf")),"late duplicate completion cannot write another destination");
    const auto reserved=request.begin(&owner,page.get(),document.ticket());
    check(!WizPdfExportRequest::ownsContext(ticket),"a new export expires old queued error notification context");
    WizPdfExportRequest::cancel(reserved);
    evaluate(page.get(),"document.body.contentEditable='true';true");
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto editing=print(page.get(),ticket,root.filePath("editing.pdf"));
    check(editing->done&&editing->result==WizPdfExportRequest::Result::Saved
          &&status(page.get()).value("prepares").toInt()==1
          &&evaluate(page.get(),"document.body.innerHTML").toString()==before,
          "editable rich text is not augmented or exposed to new attribute mutations");
    evaluate(page.get(),"document.body.removeAttribute('contenteditable');true");
    // Preserve the original failing control: unguarded serialization CAN see
    // temporary SVG2 attributes while Chromium is printing. Do not relabel
    // that unsafe route as fixed just because afterprint eventually restores.
    evaluate(page.get(),"globalThis.__wizPdfSvgLinks.arm()",QWebEngineScript::ApplicationWorld);
    auto exposed=std::make_shared<QString>();
    {
        QEventLoop loop;QPointer<QEventLoop> waiting(&loop);QTimer deadline;deadline.setSingleShot(true);
        QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        page->printToPdf([waiting](const QByteArray&){if(waiting)waiting->quit();});
        page->runJavaScript("document.body.innerHTML",[exposed](const QVariant& value){*exposed=value.toString();});
        deadline.start(20000);loop.exec();
    }
    evaluate(page.get(),"globalThis.__wizPdfSvgLinks.finish()",QWebEngineScript::ApplicationWorld);
    check(!exposed->isEmpty()&&*exposed!=before,"negative control reproduces unsafe raw serialization during physical printing");
    WizPdfPageOperations operations;
    auto queuedSnapshot=std::make_shared<QString>();
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto whileSaving=print(page.get(),ticket,root.filePath("queued-save.pdf"),[&]{
        operations.snapshot(page.get(),[]{return true;},"document.body.innerHTML",
            [queuedSnapshot](const QVariant& value){*queuedSnapshot=value.toString();});
    },&operations);
    for(int i=0;i<100&&queuedSnapshot->isEmpty();++i)evaluate(page.get(),"true");
    check(whileSaving->done&&whileSaving->result==WizPdfExportRequest::Result::Saved&&*queuedSnapshot==before,
          "production FIFO snapshot queued during printing observes restored original HTML");
    check(evaluate(page.get(),"document.body.innerHTML").toString()==before,"serialization barrier preserves exact source HTML without rewriting existing links");
    auto readerSnapshot=std::make_shared<QVariant>();
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto readerSaving=print(page.get(),ticket,root.filePath("reader-save.pdf"),[&]{
        operations.snapshot(page.get(),[]{return true;},WizDocumentSaveSnapshot::readerScript(true),
            [readerSnapshot](const QVariant& value){*readerSnapshot=value;});
    },&operations);
    for(int i=0;i<100&&!readerSnapshot->isValid();++i)evaluate(page.get(),"true");
    const auto readerResult=WizDocumentSaveSnapshot::parse(*readerSnapshot);
    check(readerSaving->result==WizPdfExportRequest::Result::Saved&&readerResult.state==WizDocumentSaveSnapshot::State::Changed
          &&readerResult.html==before,"actual production reader snapshot cannot persist temporary print attributes");
    auto order=std::make_shared<QString>();
    operations.snapshot(page.get(),[]{return true;},"document.body.innerHTML",[order,before](const QVariant& value){*order+=value.toString()==before?'S':'!';});
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto saveFirst=print(page.get(),ticket,root.filePath("save-first.pdf"),[&]{
        operations.snapshot(page.get(),[]{return true;},"document.body.innerHTML",[order,before](const QVariant& value){*order+=value.toString()==before?'T':'!';});
    },&operations);
    for(int i=0;i<100&&order->size()<2;++i)evaluate(page.get(),"true");
    check(saveFirst->result==WizPdfExportRequest::Result::Saved&&*order=="ST","snapshots before and after a print retain FIFO order and original HTML");
    const auto unarmedPrepares=status(page.get()).value("prepares").toInt();
    {
        QEventLoop loop;QPointer<QEventLoop> waiting(&loop);QTimer deadline;deadline.setSingleShot(true);
        QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        page->printToPdf([waiting](const QByteArray&){if(waiting)waiting->quit();});deadline.start(20000);loop.exec();
    }
    check(status(page.get()).value("prepares").toInt()==unarmedPrepares&&evaluate(page.get(),"document.body.innerHTML").toString()==before,
          "an unarmed page print never augments DOM outside the serialization gate");
    const auto kept=root.filePath("kept.pdf");check(sentinel(kept),"existing synthetic destination sentinel is prepared");
    check(WizPdfExportRequest::complete(reserved,kept,first->bytes,error)==WizPdfExportRequest::Result::Canceled
          &&read(kept)=="original-destination","native save-dialog cancellation preserves the old destination");
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto failed=print(page.get(),ticket,root.filePath("missing/destination.pdf"));
    check(failed->done&&failed->result==WizPdfExportRequest::Result::Failed&&!failed->error.isEmpty(),"write failure is reported after rendering rather than treated as success");
    check(evaluate(page.get(),"document.body.innerHTML").toString()==before&&!status(page.get()).value("active").toBool(),"write failure cannot leave printing links in editable HTML");
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto canceled=print(page.get(),ticket,kept,[&]{request.cancel();});
    check(canceled->done&&canceled->result==WizPdfExportRequest::Result::Canceled&&read(kept)=="original-destination","canceling an in-flight print suppresses a late atomic commit");
    check(evaluate(page.get(),"document.body.innerHTML").toString()==before&&!status(page.get()).value("active").toBool(),"canceled output still finishes the browser attribute restoration");
    ticket=request.begin(&owner,page.get(),document.ticket());
    document.invalidate();
    check(WizPdfExportRequest::complete(ticket,kept,first->bytes,error)==WizPdfExportRequest::Result::Canceled
          &&read(kept)=="original-destination","document change while a modal dialog is open prevents stale export");
    auto temporaryOwner=std::make_unique<QObject>();
    ticket=request.begin(temporaryOwner.get(),page.get(),document.ticket());
    auto ownerClosed=print(page.get(),ticket,kept,[&]{temporaryOwner.reset();});
    check(ownerClosed->done&&ownerClosed->result==WizPdfExportRequest::Result::Canceled&&read(kept)=="original-destination","destroyed owner cannot commit an outstanding print");
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto navigated=print(page.get(),ticket,kept,[&]{document.invalidate();request.cancel();page->setHtml("<!doctype html><p>Replacement document</p>");});
    check(navigated->done&&navigated->result==WizPdfExportRequest::Result::Canceled&&read(kept)=="original-destination","navigation during print discards old output without touching the replacement file");
    check(load(page.get(),"<!doctype html><p>Replacement document</p>"),"replacement document remains loadable after canceled print");
    check(evaluate(page.get(),"document.body.textContent").toString()=="Replacement document"&&!status(page.get()).value("active").toBool(),"replacement document has no old print transaction whether or not Qt injected listeners");
    check(load(page.get(),html()),"equation document reloads after navigation cancellation");
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto reloaded=print(page.get(),ticket,root.filePath("reloaded.pdf"));
    check(reloaded->done&&reloaded->result==WizPdfExportRequest::Result::Saved
          &&status(page.get()).value("prepares").toInt()==1&&status(page.get()).value("restores").toInt()==1
          &&status(page.get()).value("lastAdded").toInt()==1,
          "native entry point ensures a fresh isolated helper after canceled navigation");
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto pageClosed=print(page.get(),ticket,kept,[&]{request.cancel();page.reset();});
    check(pageClosed->pageDestroyed&&(!pageClosed->done||pageClosed->result==WizPdfExportRequest::Result::Canceled)
          &&!WizPdfExportRequest::current(ticket)&&read(kept)=="original-destination",
          "page destruction safely cancels output whether or not Qt delivers a callback");
    check(WizPdfExportRequest::complete(ticket,kept,first->bytes,error)==WizPdfExportRequest::Result::Canceled
          &&read(kept)=="original-destination","even an explicitly late completion after page destruction cannot write");
    page=std::make_unique<QWebEnginePage>(&profile);WizPdfSvgLinks::install(page.get());check(load(page.get(),html()),"fresh page loads after closing an in-flight print");
    auto closingRequest=std::make_unique<WizPdfExportRequest>();
    ticket=closingRequest->begin(&owner,page.get(),document.ticket());
    auto requestClosed=print(page.get(),ticket,kept,[&]{closingRequest.reset();});
    check(requestClosed->done&&requestClosed->result==WizPdfExportRequest::Result::Canceled&&read(kept)=="original-destination","destroyed reservation expires without keeping its owner alive");
    int released=0,invalid=0;
    auto closingOperations=std::make_unique<WizPdfPageOperations>();
    const auto releasedSnapshot=[&](const QVariant& value){++released;if(!value.isValid())++invalid;};
    closingOperations->snapshot(page.get(),[]{return true;},"document.body.innerHTML",releasedSnapshot);
    closingOperations->snapshot(page.get(),[]{return true;},"document.body.innerHTML",releasedSnapshot);
    closingOperations.reset();evaluate(page.get(),"true");
    check(released==2&&invalid==2,"destroyed operation queue releases active and pending save callbacks exactly once as failures");
    int obsolete=0;
    operations.snapshot(page.get(),[]{return false;},"document.body.innerHTML",[&](const QVariant& value){if(!value.isValid())++obsolete;});
    check(obsolete==1,"an obsolete queued document is rejected before executing its snapshot script");
    auto reentrant=std::make_shared<std::array<int,3>>();
    operations.snapshot(page.get(),[]{return true;},"true",[](const QVariant&){});
    operations.snapshot(page.get(),[]{return false;},"true",[&operations,page=page.get(),reentrant](const QVariant&){
        operations.snapshot(page,[reentrant]{if((*reentrant)[0])++(*reentrant)[1];return true;},"true",
            [reentrant](const QVariant&){(*reentrant)[2]=1;});
    });
    operations.snapshot(page.get(),[reentrant]{(*reentrant)[0]=1;return true;},"true",
        [reentrant](const QVariant&){(*reentrant)[0]=0;});
    for(int i=0;i<100&&!(*reentrant)[2];++i)evaluate(page.get(),"true");
    check((*reentrant)[2]&&(*reentrant)[1]==0,"reentrant failure callbacks cannot start a second concurrent operation");
    auto physical=std::make_shared<Printed>();auto physicalSnapshot=std::make_shared<QString>();
    ticket=request.begin(&owner,page.get(),document.ticket());
    const auto preparesBefore=status(page.get()).value("prepares").toInt();
    auto validationCount=std::make_shared<int>(0);
    {
        QEventLoop loop;QPointer<QEventLoop> waiting(&loop);QTimer deadline;deadline.setSingleShot(true);
        QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
        operations.print(page.get(),[ticket,validationCount,&owner,&request]{
            if(++*validationCount==2)QTimer::singleShot(0,&owner,[&request]{request.cancel();});
            return WizPdfExportRequest::current(ticket);
        },WizPdfExport::pageLayout(0,QMarginsF(5,5,5,5)),[physical,ticket,kept,waiting](const QByteArray& bytes){
            physical->result=WizPdfExportRequest::complete(ticket,kept,bytes,physical->error);physical->done=true;
            if(waiting)waiting->quit();
        });
        operations.snapshot(page.get(),[]{return true;},"document.body.innerHTML",[physicalSnapshot](const QVariant& value){*physicalSnapshot=value.toString();});
        deadline.start(20000);if(!physical->done)loop.exec();
    }
    for(int i=0;i<100&&physicalSnapshot->isEmpty();++i)evaluate(page.get(),"true");
    check(physical->done&&physical->result==WizPdfExportRequest::Result::Canceled
          &&status(page.get()).value("prepares").toInt()==preparesBefore+1&&read(kept)=="original-destination",
          "canceling after physical print dispatch suppresses output but does not abort browser restoration");
    check(*physicalSnapshot==before,"output cancellation cannot release a waiting save before physical printing restores HTML");
    evaluate(page.get(),"Element.prototype.removeAttributeNS=function(){throw new Error('synthetic restoration fault');};true",QWebEngineScript::ApplicationWorld);
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto unrestored=print(page.get(),ticket,kept,{},&operations);
    auto blockedSnapshot=std::make_shared<QVariant>();int blockedCalls=0;
    operations.snapshot(page.get(),[]{return true;},WizDocumentSaveSnapshot::readerScript(true),
        [blockedSnapshot,&blockedCalls](const QVariant& value){*blockedSnapshot=value;++blockedCalls;});
    evaluate(page.get(),"true");
    check(unrestored->done&&unrestored->result==WizPdfExportRequest::Result::Failed&&read(kept)=="original-destination",
          "restoration fault fails PDF output without replacing an existing destination");
    check(blockedCalls==1&&!blockedSnapshot->isValid(),"unverified print DOM blocks later save serialization instead of risking persistence");
    check(load(page.get(),html()),"a fresh document loads after a synthetic restoration fault");
    ticket=request.begin(&owner,page.get(),document.ticket());
    auto recovered=print(page.get(),ticket,root.filePath("recovered.pdf"),{},&operations);
    check(recovered->done&&recovered->result==WizPdfExportRequest::Result::Saved
          &&evaluate(page.get(),"document.body.innerHTML").toString()==before,
          "fresh-load recovery clears only the failed document and restores native export");
    qInfo()<<"PDF export lifecycle checks:"<<count<<"passed:"<<(count-failures)<<"failures:"<<failures;
    return failures?1:0;
}
