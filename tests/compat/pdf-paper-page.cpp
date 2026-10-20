#include "share/WizPdfPageOperations.h"
#include "share/WizPdfExportRequest.h"
#include <QApplication>
#include <QEventLoop>
#include <QTemporaryDir>
#include <QFile>
#include <QDebug>

namespace {
QVariant evaluate(QWebEnginePage* page,const QString& source) {
    struct State {QVariant value;bool done=false;};
    auto s=std::make_shared<State>();QEventLoop loop;QPointer<QEventLoop> guard(&loop);QTimer timer;
    QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.setSingleShot(true);
    page->runJavaScript(source,[s,guard](const QVariant& value){s->value=value;s->done=true;if(guard)guard->quit();});
    timer.start(10000);if(!s->done)loop.exec();return s->value;
}
bool load(QWebEnginePage* page,const QString& html,const QUrl& base) {
    QEventLoop loop;QTimer timer;bool loaded=false;
    QObject::connect(page,&QWebEnginePage::loadFinished,&loop,[&](bool ok){loaded=ok;loop.quit();});
    QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.setSingleShot(true);
    page->setHtml(html,base);timer.start(10000);loop.exec();return loaded;
}
struct Prepared {int calls=0;QPointer<WizPdfPaperPage> session;QPointer<QWebEnginePage> page;};
std::shared_ptr<Prepared> prepare(QObject* owner,QWebEnginePage* source,const QVariantMap& data,
                                 const std::shared_ptr<bool>& valid,const std::function<void()>& during={},const QUrl& mathJax=QUrl()) {
    auto state=std::make_shared<Prepared>();QEventLoop loop;QPointer<QEventLoop> guard(&loop);QTimer timer;
    QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.setSingleShot(true);
    WizPdfPaperPage::create(owner,source,data,482,mathJax,[valid]{return *valid;},
        [state,guard](WizPdfPaperPage* session,QWebEnginePage* page){++state->calls;state->session=session;state->page=page;if(guard)guard->quit();});
    if(during)during();timer.start(10000);if(!state->calls)loop.exec();return state;
}
void settle() {QEventLoop loop;QTimer::singleShot(200,&loop,&QEventLoop::quit);loop.exec();}
}
int main(int argc,char** argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QApplication app(argc,argv);app.setApplicationName("WizPdfPaperPageTests");
    QTemporaryDir directory;if(!directory.isValid())return 2;
    QWebEngineProfile profile;auto source=std::make_unique<QWebEnginePage>(&profile);
    source->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,false);
    const auto base=QUrl::fromLocalFile(directory.filePath("source.html"));
    int count=0,failures=0;
    const auto check=[&](bool ok,const char* label){++count;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;};
    const QString html=QStringLiteral("<!doctype html><style>body{font:16px sans-serif;width:1400px;margin:32px}</style>"
        "<body onload='window.noteLoadCount=1'><h1 id='top'>Paper title</h1><p>Normal text <a href='#top'>heading link</a></p>"
        "<pre>code code code ")+QString(500,'x')+QStringLiteral("</pre><canvas id='drawing' width='20' height='10'></canvas>"
        "<script>window.noteLoadCount=1;document.getElementById('drawing').getContext('2d').fillRect(0,0,20,10);</script></body>");
    check(load(source.get(),html,base),"local synthetic rich note loads without account or network");
    const auto before=evaluate(source.get(),"document.documentElement.outerHTML").toString();
    const auto data=evaluate(source.get(),WizPdfPaperPage::captureScript()).toMap();
    check(data.contains("html")&&!data.contains("error"),"paper capture reads rich content and freezes a canvas");
    check(evaluate(source.get(),"document.documentElement.outerHTML").toString()==before,"capture leaves exact source HTML unchanged");
    QObject owner;auto valid=std::make_shared<bool>(true);
    auto prepared=prepare(&owner,source.get(),data,valid);
    check(prepared->calls==1&&prepared->page&&prepared->session,"off-screen paper document prepares once");
    if(prepared->page){
        const auto state=evaluate(prepared->page,"({width:innerWidth,scroll:document.documentElement.scrollWidth,font:getComputedStyle(document.body).fontSize,"
            "scripts:document.querySelectorAll('script:not([type])').length,loads:typeof noteLoadCount,"
            "canvas:document.querySelectorAll('canvas').length,images:document.images.length,"
            "link:document.querySelector('a').getAttribute('href'),editable:document.body.isContentEditable})").toMap();
        check(state.value("width").toInt()>0&&state.value("scroll").toInt()<=state.value("width").toInt()+1,"hidden output page has a finite paper viewport without overflow");
        check(state.value("font").toString()=="16px","paper layout retains normal text font size");
        check(state.value("loads").toString()=="undefined"&&state.value("scripts").toInt()==0,"note load hooks and scripts are not replayed in a second document");
        check(state.value("canvas").toInt()==0&&state.value("images").toInt()==1,"paper snapshot preserves canvas as a local image");
        check(state.value("link").toString()=="#top"&&!state.value("editable").toBool(),"paper clone retains local heading navigation without being editable");
        WizPdfPageOperations operations;auto result=std::make_shared<QByteArray>();auto done=std::make_shared<bool>(false);
        QEventLoop loop;QPointer<QEventLoop> guard(&loop);QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.setSingleShot(true);
        operations.print(prepared->page,[session=prepared->session]{return session&&session->usable();},
            WizPdfExport::pageLayout(0,QMarginsF(20,20,20,20)),[result,done,guard](const QByteArray& bytes){*result=bytes;*done=true;if(guard)guard->quit();});
        timer.start(10000);if(!*done)loop.exec();
        check(*done&&result->startsWith("%PDF-"),"prepared paper page uses actual controlled native printing");
        prepared->session->finish();settle();
        check(!prepared->session&&!prepared->page,"completed paper view and browser page are released");
    }
    check(evaluate(source.get(),"document.documentElement.outerHTML").toString()==before,"preparation and native print never alter source DOM");
    auto media=data;
    QString mediaHtml=data.value("html").toString();
    mediaHtml.replace("</body>","<table><tr><th>Long URL</th><th>Value</th></tr><tr><td>"+QString(300,'z')+"</td><td>End</td></tr></table></body>");
    media.insert("html",mediaHtml);
    auto table=prepare(&owner,source.get(),media,valid);
    check(table->calls==1&&table->page,"long table cells wrap within paper width rather than shrinking the whole document");
    if(table->session)table->session->finish();settle();
    auto missingCss=data;
    missingCss.insert("html",data.value("html").toString().replace("</head>","<link rel='stylesheet' href='missing.css'></head>"));
    auto cssFailure=prepare(&owner,source.get(),missingCss,valid);
    check(cssFailure->calls==1&&!cssFailure->page,"missing stylesheet refuses an incomplete paper export");
    WizPdfPageOperations exportOperations;
    QObject exportOwner;WizDocumentCallbackEpoch epoch;WizPdfExportRequest request;
    const QString destination=directory.filePath("existing.pdf");
    const QByteArray oldPdf("%PDF-1.7\n% previous output\n%%EOF\n");QString error;
    const auto readOutput=[&]{QFile f(destination);return f.open(QIODevice::ReadOnly)?f.readAll():QByteArray{};};
    const auto exportPaper=[&](bool cancel) {
        struct Result {bool done=false;WizPdfExportRequest::Result outcome=WizPdfExportRequest::Result::Canceled;};
        auto result=std::make_shared<Result>();QEventLoop loop;QPointer<QEventLoop> guard(&loop);QTimer timer;
        QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.setSingleShot(true);
        const auto ticket=request.begin(&exportOwner,source.get(),epoch.ticket());
        exportOperations.printWithMode(source.get(),[ticket]{return WizPdfExportRequest::current(ticket);},0,QMarginsF(20,20,20,20),
            WizPdfExport::Mode::A4,[result,ticket,guard,destination](const QByteArray& bytes){
                QString error;result->outcome=WizPdfExportRequest::complete(ticket,destination,bytes,error);result->done=true;if(guard)guard->quit();
            });
        if(cancel)request.cancel();timer.start(10000);if(!result->done)loop.exec();return result;
    };
    check(WizPdfExport::writeFile(destination,oldPdf,error),"synthetic old PDF is available for atomic paper-export tests");
    const auto saved=exportPaper(false);
    check(saved->done&&saved->outcome==WizPdfExportRequest::Result::Saved&&readOutput()!=oldPdf,
          "actual A4 mode commits a complete isolated paper PDF through the original writer");
    check(evaluate(source.get(),"document.documentElement.outerHTML").toString()==before,"A4 mode leaves exact original HTML after successful commit");
    check(WizPdfExport::writeFile(destination,oldPdf,error),"old output restored only inside the synthetic temporary directory");
    const auto noSave=exportPaper(true);
    check(noSave->done&&noSave->outcome==WizPdfExportRequest::Result::Canceled&&readOutput()==oldPdf,
          "cancelled A4 capture preserves old output instead of falling back to live-page printing");
    load(source.get(),"<!doctype html><body><img src='missing.png'><p>Source stays unchanged</p></body>",base);
    const auto missingBefore=evaluate(source.get(),"document.documentElement.outerHTML").toString();
    const auto missingImage=exportPaper(false);
    check(missingImage->done&&missingImage->outcome==WizPdfExportRequest::Result::Failed&&readOutput()==oldPdf,
          "unavailable note image refuses PDF replacement rather than silently dropping content");
    check(evaluate(source.get(),"document.documentElement.outerHTML").toString()==missingBefore,"failed paper preparation keeps original missing-resource document unchanged");
    load(source.get(),"<!doctype html><body><div style='width:5000px'>Unsupported fixed-width block</div></body>",base);
    const auto wideBefore=evaluate(source.get(),"document.documentElement.outerHTML").toString();
    const auto wide=exportPaper(false);
    check(wide->done&&wide->outcome==WizPdfExportRequest::Result::Failed&&readOutput()==oldPdf,
          "unfitted non-math overflow refuses replacement instead of shrinking all ordinary text");
    check(evaluate(source.get(),"document.documentElement.outerHTML").toString()==wideBefore,"paper print-bound failure never changes original fixed-width content");
    const auto imagePath=directory.filePath("lazy image #%.svg");
    QFile imageFile(imagePath);
    check(imageFile.open(QIODevice::WriteOnly)&&imageFile.write("<svg xmlns='http://www.w3.org/2000/svg' width='20' height='10'><rect width='20' height='10' fill='green'/></svg>")>0,
          "local lazy-image fixture created in its owned temporary directory");imageFile.close();
    load(source.get(),"<!doctype html><body><div style='height:50000px'></div><img loading='lazy' src='"
        +QUrl::fromLocalFile(imagePath).toString()+"'></body>",base);
    const auto lazyData=evaluate(source.get(),WizPdfPaperPage::captureScript()).toMap();
    auto lazy=prepare(&owner,source.get(),lazyData,valid);
    check(lazy->calls==1&&lazy->page,"local lazy image can load in output even without source viewport visibility");
    if(lazy->page){
        check(evaluate(lazy->page,"document.images[0].naturalWidth===20&&!document.images[0].hasAttribute('loading')").toBool(),
              "output loads the lazy image eagerly and preserves its original dimensions");
        lazy->session->finish();settle();
    }
    check(load(source.get(),html,base),"original rich fixture reloads after ordinary missing-resource failure");
    const QString mathHtml=QStringLiteral(R"HTML(<!doctype html><style>body{font:16px sans-serif}</style><body>
      <span id="literal">$not-math$</span><code>$code$</code>
      <script id="math-input" type="math/tex; mode=display">\begin{equation}\label{eq1}a=b\end{equation}</script>
      <script>window.MathJax={Hub:{getAllJax:()=>[{}],config:{TeX:{equationNumbers:{autoNumber:'AMS'}}}}};</script></body>)HTML");
    check(load(source.get(),mathHtml,base),"synthetic processed-input contract loads alongside literal dollar text");
    const auto mathBefore=evaluate(source.get(),"document.documentElement.outerHTML").toString();
    const auto mathData=evaluate(source.get(),WizPdfPaperPage::captureScript()).toMap();
    const auto mathJax=QUrl::fromLocalFile(QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath()).absoluteFilePath(
        "../../share/files/wizeditor/dependency/mathjax/MathJax.js"));
    auto math=prepare(&owner,source.get(),mathData,valid,{},mathJax);
    check(math->calls==1&&math->page,"paper renderer processes retained math input without a second Markdown pass");
    if(math->page){
        check(evaluate(math->page,"MathJax.Hub.getAllJax().length===1 && document.getElementById('literal').textContent==='$not-math$' && !document.getElementById('literal').querySelector('svg')").toBool(),
              "literal dollar text is not converted into an unintended formula");
        check(evaluate(math->page,"document.querySelectorAll('[id=\"mjx-eqn-eq1\"]').length===1 && !!document.querySelector('svg [id=\"mjx-eqn-eq1\"]')").toBool(),
              "generated assistive MathML cannot duplicate the visible equation destination ID");
        math->session->finish();settle();
    }
    check(evaluate(source.get(),"document.documentElement.outerHTML").toString()==mathBefore,"paper math reprocessing keeps exact original formula and literal source");
    auto noRenderer=prepare(&owner,source.get(),mathData,valid);
    check(noRenderer->calls==1&&!noRenderer->page,"missing bundled math renderer fails instead of silently retaining stale layout");
    check(load(source.get(),html,base),"rich source restored only in isolated fixture after math contract test");
    auto canceled=std::make_shared<bool>(false);
    auto canceledState=prepare(&owner,source.get(),data,canceled);
    check(canceledState->calls==1&&!canceledState->page,"cancelled request cannot prepare an output page");
    auto active=prepare(&owner,source.get(),data,valid);
    check(active->calls==1&&active->page,"a new request prepares after prior cancellation");
    *valid=false;settle();
    check(!active->session&&!active->page&&active->calls==1,"context cancellation releases prepared page without duplicate completion");
    valid=std::make_shared<bool>(true);
    auto* doomedOwner=new QObject;
    auto ownerState=prepare(doomedOwner,source.get(),data,valid,[&]{delete doomedOwner;});
    check(ownerState->calls==1&&!ownerState->page,"owner destruction during queued preparation settles the callback once");
    auto bad=data;bad.insert("error","synthetic unavailable resource");
    auto failed=prepare(&owner,source.get(),bad,valid);
    check(failed->calls==1&&!failed->page,"unavailable snapshot fails closed instead of producing incomplete output");
    auto sourceState=prepare(&owner,source.get(),data,valid,[&]{source.reset();});
    check(sourceState->calls==1&&!sourceState->page,"source destruction before layout cancels the output page");
    settle();
    check(owner.findChildren<QObject*>("wizPdfPaperSession").isEmpty(),"completed failed and cancelled sessions leave no owned paper windows");
    qInfo()<<"Paper page checks:"<<count<<"failures:"<<failures;return failures?1:0;
}
