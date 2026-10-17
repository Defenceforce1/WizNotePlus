// Isolated actual Qt printToPdf probe. Synthetic local input only. No main-app
// bootstrap, user preferences, account, DB, registration, signing or installation.
#include <QtWidgets/QApplication>
#include <QtGui/QPageLayout>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineCore/QWebEngineUrlRequestInterceptor>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFile>
#include <QtCore/QDir>
#include <QtCore/QTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QDebug>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <atomic>
#include <memory>
#include <cstdio>
#include "share/WizPdfExport.h"

class LocalOnly final : public QWebEngineUrlRequestInterceptor {
public:
    std::atomic<int> blocked{0};
    void interceptRequest(QWebEngineUrlRequestInfo& info) override {
        const auto scheme=info.requestUrl().scheme();
        if (scheme!="file" && scheme!="data" && scheme!="qrc" && scheme!="about" && scheme!="blob") {
            ++blocked; info.block(true);
        }
    }
};
static QVariant evaluate(QWebEnginePage* page, const QString& source) {
    struct State {QVariant result;bool completed=false;};
    auto state=std::make_shared<State>();QEventLoop loop;QTimer deadline;QPointer<QEventLoop> guard(&loop);
    deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
    page->runJavaScript(source,[state,guard](const QVariant& value){state->result=value;state->completed=true;if(guard)guard->quit();});
    deadline.start(10000); if (!state->completed) loop.exec();
    if (!state->completed) qCritical()<<"Synthetic JavaScript evaluation timed out";
    return state->result;
}
int main(int argc,char** argv) {
    if (argc!=4) { std::fprintf(stderr,"Usage: native-pdf APP_BUNDLE SYNTHETIC_HTML OUTPUT_DIR\n"); return 2; }
    const QString bundle=QString::fromLocal8Bit(argv[1]),input=QString::fromLocal8Bit(argv[2]),output=QString::fromLocal8Bit(argv[3]);
    if (!QFile::exists(input) || !QDir(output).exists() || !input.endsWith("native-pdf-fixture.html")) return 2;
    QTemporaryDir temporary; if (!temporary.isValid()) return 2;
    qputenv("XDG_CACHE_HOME",temporary.path().toUtf8());
    qputenv("QTWEBENGINEPROCESS_PATH",QFile::encodeName(bundle+"/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess"));
    qputenv("QTWEBENGINE_RESOURCES_PATH",QFile::encodeName(bundle+"/Contents/Frameworks/QtWebEngineCore.framework/Versions/A/Resources"));
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QCoreApplication::setLibraryPaths({bundle+"/Contents/PlugIns"});
    QApplication app(argc,argv); app.setApplicationName("WizNoteIsolatedPdfProbe");
    LocalOnly requests;
    QWebEngineProfile profile; // off-the-record, never the user's profile
    profile.setPersistentStoragePath(temporary.path()+"/profile");
    profile.setCachePath(temporary.path()+"/cache");
    profile.setUrlRequestInterceptor(&requests);
    QWebEngineView reader;
    reader.setPage(new QWebEnginePage(&profile,&reader));
    reader.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,false);
    reader.resize(850,660); reader.show(); reader.load(QUrl::fromLocalFile(input));
    bool ready=false; QEventLoop readyLoop; QTimer poll,deadline;
    QObject::connect(&poll,&QTimer::timeout,&readyLoop,[&]{
        ready=evaluate(reader.page(),"window.FixtureReady===true").toBool(); if (ready) readyLoop.quit();
    });
    QObject::connect(&deadline,&QTimer::timeout,&readyLoop,&QEventLoop::quit);
    poll.start(100);deadline.setSingleShot(true);deadline.start(65000);readyLoop.exec();poll.stop();deadline.stop();
    if (!ready) {qCritical()<<"Synthetic reader never became ready";return 3;}
    const bool sourceBefore=evaluate(reader.page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource").toBool();
    const auto htmlBefore=evaluate(reader.page(),"WizEditor.getContentHtml()").toString();
    const auto modifiedBefore=evaluate(reader.page(),"WizEditor.isModified()");
    evaluate(reader.page(),R"JS((()=>{
      const link=document.createElement('a');link.href='#control-target';link.textContent='Heading control';document.body.prepend(link);
      const heading=document.createElement('h2');heading.id='control-target';heading.textContent='CONTROL-TARGET';document.body.appendChild(heading);
      window.RestorePdfControl=()=>{link.remove();heading.remove();};return true;
    })())JS");
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EDGE_PROTOTYPE")) evaluate(reader.page(),R"JS((()=>{
      const block=document.getElementById('wiz-math-thm:pdf-a'),spacer=document.createElement('div');
      spacer.style.cssText='break-before:page;height:235mm';block.before(spacer);
      WizEditor.insertCustomStyle('wiz_tmp_style_pdf_edge_probe','@media print {.wiz-math-block {break-inside:auto!important;page-break-inside:auto!important;}}',true);
      window.RestorePdfEdgeProbe=()=>{spacer.remove();WizEditor.removeStyleById('wiz_tmp_style_pdf_edge_probe');};return true;
    })())JS");
    int failures=0;
    const auto print=[&](const QString& name,double margin) {
        if (QFile::exists(QDir(output).filePath(name))) {qCritical()<<"Refusing to overwrite synthetic output";return false;}
        struct State {bool done=false,valid=false;};
        auto state=std::make_shared<State>();QEventLoop loop;QTimer timer;QPointer<QEventLoop> guard(&loop);
        QObject::connect(&timer,&QTimer::timeout,&loop,&QEventLoop::quit);timer.setSingleShot(true);timer.start(30000);
        const bool cssMargins=qEnvironmentVariableIsSet("WIZNOTE_PDF_CSS_PAGE_MARGINS");
        if(cssMargins) evaluate(reader.page(),QString(R"JS((()=>{
          const style=document.createElement('style');style.id='wiz_tmp_style_pdf_page_margin_probe';
          style.textContent='@page {size:A4 portrait; margin:%1mm;}';document.head.appendChild(style);return true;
        })())JS").arg(margin));
        const QPageLayout layout=WizPdfExport::pageLayout(0,cssMargins?QMarginsF(0,0,0,0):QMarginsF(margin,margin,margin,margin));
        const QString destination=QDir(output).filePath(name);
        reader.page()->printToPdf([state,guard,destination](const QByteArray& bytes){
            state->done=true;
            if (guard && bytes.startsWith("%PDF-")) {
                QString error;state->valid=WizPdfExport::writeFile(destination,bytes,error);
                if(!state->valid)qWarning()<<"Synthetic PDF helper failure:"<<error;
            }
            if(guard)guard->quit();
        },layout);
        if (!state->done) loop.exec();
        if(cssMargins) evaluate(reader.page(),"document.getElementById('wiz_tmp_style_pdf_page_margin_probe')?.remove();true");
        qInfo().noquote()<<(state->valid?"PASS":"FAIL")<<name<<"requested margin mm"<<margin
            <<"margin path"<<(cssMargins?"CSS @page with zero API margins":"QPageLayout API");
        return state->valid;
    };
    failures+=!print("references-5mm.pdf",5);
    failures+=!print("references-20mm.pdf",20);
    evaluate(reader.page(),R"JS((()=>{
      const style=document.createElement('style');style.textContent='.wiz-math-block::before{display:none!important}';document.head.appendChild(style);
      const records=[];document.querySelectorAll('.wiz-math-block').forEach(block=>{
        const span=document.createElement('span');span.textContent=block.getAttribute('data-wiz-math-heading');
        span.style.cssText='display:block;font-weight:700;color:#405bb0;margin-bottom:.45em';
        span.id=block.id;block.removeAttribute('id');block.prepend(span);records.push({block,span});
      });window.RestorePdfHeadingProbe=()=>{records.forEach(({block,span})=>{block.id=span.id;span.remove();});style.remove();};return true;
    })())JS");
    failures+=!print("references-visible-heading.pdf",5);
    evaluate(reader.page(),"RestorePdfHeadingProbe();true");
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_ANCHOR_PROTOTYPE")) {
        // Diagnostic only: quantify whether a zero-layout anchor can compensate
        // the engine's missing page-margin offset without rewriting the PDF.
        evaluate(reader.page(),QString("window.WizPdfProxyTransform=%1;true").arg(qEnvironmentVariableIsSet("WIZNOTE_PDF_ANCHOR_TRANSFORM")?"true":"false"));
        evaluate(reader.page(),R"JS(window.PreparePdfAnchorProbe=function(marginMm){
          const records=[];
          document.querySelectorAll('.wiz-math-block[id]').forEach(block=>{
            const span=document.createElement('span'),id=block.id,style=block.getAttribute('style');
            const padding=parseFloat(getComputedStyle(block).paddingTop)||0;
            const shift=marginMm*96/25.4;
            span.id=id;span.style.cssText='position:absolute;left:0;width:0;height:0;pointer-events:none;top:'+(padding+(WizPdfProxyTransform?0:shift))+'px';
            if(WizPdfProxyTransform)span.style.transform='translateY('+shift+'px)';
            block.removeAttribute('id');block.style.position='relative';block.prepend(span);records.push({block,span,id,style});
          });
          window.RestorePdfAnchorProbe=()=>{records.forEach(({block,span,id,style})=>{
            span.remove();block.id=id;if(style===null){block.getAttribute('style');block.removeAttribute('style');}else block.setAttribute('style',style);
          });};return true;
        };true)JS");
        evaluate(reader.page(),"PreparePdfAnchorProbe(5);true");
        failures+=!print("references-proxy-5mm.pdf",5);
        evaluate(reader.page(),"RestorePdfAnchorProbe();PreparePdfAnchorProbe(20);true");
        failures+=!print("references-proxy-20mm.pdf",20);
        evaluate(reader.page(),"RestorePdfAnchorProbe();true");
    }
    evaluate(reader.page(),"RestorePdfControl();true");
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EDGE_PROTOTYPE"))evaluate(reader.page(),"RestorePdfEdgeProbe();true");
    const bool sourceAfter=evaluate(reader.page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource").toBool();
    const bool htmlSame=evaluate(reader.page(),"WizEditor.getContentHtml()").toString()==htmlBefore;
    const bool modifiedSame=evaluate(reader.page(),"WizEditor.isModified()")==modifiedBefore;
    const auto state=evaluate(reader.page(),"({blocks:document.querySelectorAll('.wiz-math-block').length,jax:MathJax.Hub.getAllJax().length,errors:document.querySelectorAll('.MathJax_Error,merror').length})").toMap();
    failures+=!sourceBefore||!sourceAfter||!htmlSame||!modifiedSame||requests.blocked.load()!=0;
    failures+=state.value("blocks").toInt()!=2||state.value("errors").toInt()!=0||state.value("jax").toInt()!=1;
    QJsonDocument result(QJsonObject{{"kind","actual Qt printToPdf; synthetic original reader, no user data"},
        {"qtRuntime",qVersion()},{"sourceBefore",sourceBefore},{"sourceAfter",sourceAfter},{"htmlSame",htmlSame},{"modifiedSame",modifiedSame},{"blockedRequests",requests.blocked.load()},
        {"productionFileHelper",true},{"cssPageMarginsProbe",qEnvironmentVariableIsSet("WIZNOTE_PDF_CSS_PAGE_MARGINS")},
        {"edgeFixture",qEnvironmentVariableIsSet("WIZNOTE_PDF_EDGE_PROTOTYPE")},
        {"anchorPrototype",qEnvironmentVariableIsSet("WIZNOTE_PDF_ANCHOR_PROTOTYPE")},{"transformVariant",qEnvironmentVariableIsSet("WIZNOTE_PDF_ANCHOR_TRANSFORM")},
        {"renderState",QJsonObject::fromVariantMap(state)},{"failures",failures}});
    QFile record(QDir(output).filePath("native-pdf-results.json"));
    if (!record.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return 4;
    record.write(result.toJson());qInfo().noquote()<<result.toJson();
    return failures?1:0;
}
