// Isolated actual Qt printToPdf probe. Synthetic local input only. No main-app
// bootstrap, user preferences, account, DB, registration, signing or installation.
#include <QtWidgets/QApplication>
#include <QtGui/QPageLayout>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWebEngineCore/QWebEngineProfile>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineCore/QWebEngineUrlRequestInterceptor>
#include <QtWebEngineCore/qtwebenginecoreglobal.h>
#include <QtCore/QTemporaryDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
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
#ifdef Q_OS_MAC
#include <mach-o/dyld.h>
#endif
#include "share/WizPdfExport.h"
#include "share/WizPdfSvgLinks.h"
#include "share/WizPdfPageOperations.h"

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
static QVariant evaluate(QWebEnginePage* page, const QString& source,quint32 world=0) {
    struct State {QVariant result;bool completed=false;};
    auto state=std::make_shared<State>();QEventLoop loop;QTimer deadline;QPointer<QEventLoop> guard(&loop);
    deadline.setSingleShot(true);
    QObject::connect(&deadline,&QTimer::timeout,&loop,&QEventLoop::quit);
    page->runJavaScript(source,world,[state,guard](const QVariant& value){state->result=value;state->completed=true;if(guard)guard->quit();});
    deadline.start(10000); if (!state->completed) loop.exec();
    if (!state->completed) qCritical()<<"Synthetic JavaScript evaluation timed out";
    return state->result;
}
int main(int argc,char** argv) {
    if (argc!=4) { std::fprintf(stderr,"Usage: native-pdf APP_BUNDLE_OR_MATCHING_QT_SDK SYNTHETIC_HTML OUTPUT_DIR\n"); return 2; }
    const QString bundle=QDir(QString::fromLocal8Bit(argv[1])).absolutePath(),input=QFileInfo(QString::fromLocal8Bit(argv[2])).absoluteFilePath(),output=QDir(QString::fromLocal8Bit(argv[3])).absolutePath();
    if (!QFile::exists(input) || !QDir(output).exists() || !input.endsWith("native-pdf-fixture.html")) return 2;
    QTemporaryDir temporary; if (!temporary.isValid()) return 2;
    qputenv("XDG_CACHE_HOME",temporary.path().toUtf8());
    const bool sdk=QDir(bundle+"/lib/QtWebEngineCore.framework").exists();
    const QString core=bundle+(sdk?"/lib/QtWebEngineCore.framework/Versions/A":"/Contents/Frameworks/QtWebEngineCore.framework/Versions/A");
    const QString plugins=bundle+(sdk?"/plugins":"/Contents/PlugIns");
    const QString helper=core+"/Helpers/QtWebEngineProcess.app/Contents/MacOS/QtWebEngineProcess";
    QJsonObject runtimeLibraries;
#ifdef Q_OS_MAC
    const QString frameworks=bundle+(sdk?"/lib":"/Contents/Frameworks");
    for(const auto& name:QStringList{"QtCore","QtGui","QtWidgets","QtQuick",
                                    "QtQuickWidgets","QtWebEngineCore","QtWebEngineWidgets"}) {
        const QString suffix="/"+name+".framework/Versions/A/"+name;
        const QString expected=QFileInfo(frameworks+suffix).canonicalFilePath();
        QString loaded;
        for(uint32_t i=0;i<_dyld_image_count();++i) {
            const QString image=QString::fromUtf8(_dyld_get_image_name(i));
            if(image.endsWith(suffix)){loaded=QFileInfo(image).canonicalFilePath();break;}
        }
        if(expected.isEmpty()||loaded!=expected) {
            qCritical()<<"Matching runtime required before WebEngine startup:"<<name
                       <<"expected"<<expected<<"loaded"<<loaded;return 2;
        }
        runtimeLibraries.insert(name,loaded);
    }
#endif
    if(!QFile::exists(helper)||!QDir(core+"/Resources").exists()||!QDir(plugins).exists()) {
        qCritical()<<"A matching complete Qt SDK or App runtime is required";return 2;
    }
    qputenv("QTWEBENGINEPROCESS_PATH",QFile::encodeName(helper));
    qputenv("QTWEBENGINE_RESOURCES_PATH",QFile::encodeName(core+"/Resources"));
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QCoreApplication::setLibraryPaths({plugins});
    QApplication app(argc,argv); app.setApplicationName("WizNoteIsolatedPdfProbe");
    LocalOnly requests;
    QWebEngineProfile profile; // off-the-record, never the user's profile
    profile.setPersistentStoragePath(temporary.path()+"/profile");
    profile.setCachePath(temporary.path()+"/cache");
    profile.setUrlRequestInterceptor(&requests);
    QWebEngineView reader;
    reader.setPage(new QWebEnginePage(&profile,&reader));
    reader.settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,false);
    int viewportWidth=qEnvironmentVariableIntValue("WIZNOTE_PDF_VIEWPORT_WIDTH");
    if(!viewportWidth)viewportWidth=850;
    if(viewportWidth<400||viewportWidth>1200)return 2;
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EVENT_LINKS"))WizPdfSvgLinks::install(reader.page());
    reader.resize(viewportWidth,660); reader.show(); reader.load(QUrl::fromLocalFile(input));
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
    const int expectedJax=evaluate(reader.page(),"window.FixtureExpectedJax||1").toInt();
    const auto referenceDom=evaluate(reader.page(),R"JS((()=>{
      const links=Array.from(document.querySelectorAll('a')).flatMap(a=>{
        const href=a.getAttribute('href')||a.getAttributeNS('http://www.w3.org/1999/xlink','href');
        if(!href||!href.includes('#'))return [];
        const raw=href.slice(href.indexOf('#')+1);
        let id;try{id=decodeURIComponent(raw);}catch(_){id=raw;}
        const target=document.getElementById(id)||Array.from(document.getElementsByName(id))[0];
        const rect=target&&target.getBoundingClientRect();
        return [{id,raw,href,linkNamespace:a.namespaceURI,found:!!target,tag:target&&target.tagName,targetNamespace:target&&target.namespaceURI,
          css:target&&getComputedStyle(target).display,rect:rect&&{x:rect.x,y:rect.y,width:rect.width,height:rect.height}}];
      });return {mathJaxVersion:MathJax.version,jax:MathJax.Hub.getAllJax().length,links,
        viewport:{width:innerWidth,height:innerHeight,scrollWidth:document.documentElement.scrollWidth},
        equationTargets:Array.from(document.querySelectorAll('[id]')).filter(e=>e.id.startsWith('mjx-eqn-')).map(e=>({id:e.id,tag:e.tagName,namespace:e.namespaceURI,html:e.outerHTML.slice(0,1600)}))};
    })())JS").toMap();
    QVariantMap equationMarker;
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EQUATION_GEOMETRY")) {
        equationMarker=evaluate(reader.page(),R"JS((()=>{
          const targets=Array.from(document.querySelectorAll('[id]')).filter(e=>e.id.startsWith('mjx-eqn-'));
          if(targets.length!==1||targets[0].namespaceURI!=='http://www.w3.org/2000/svg')return {valid:false};
          const target=targets[0],box=target.getBBox(),before=target.getBoundingClientRect();
          const rect=document.createElementNS('http://www.w3.org/2000/svg','rect');
          for(const name of ['x','y','width','height'])rect.setAttribute(name,box[name]);
          rect.setAttribute('fill','rgb(250,13,204)');rect.setAttribute('stroke','none');
          target.prepend(rect);
          const after=target.getBoundingClientRect();
          const same=['x','y','width','height'].every(name=>Math.abs(before[name]-after[name])<.00001);
          window.RestorePdfEquationGeometry=()=>rect.remove();
          return {valid:same&&box.width>0&&box.height>0,id:target.id,
            bbox:{x:box.x,y:box.y,width:box.width,height:box.height},
            rectBefore:{x:before.x,y:before.y,width:before.width,height:before.height},
            rectAfter:{x:after.x,y:after.y,width:after.width,height:after.height},
            rgb:[250,13,204]};
        })())JS").toMap();
        if(!equationMarker.value("valid").toBool()) {
            qCritical()<<"FAIL synthetic equation marker changed geometry or target missing";return 3;
        }
    }
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
    WizPdfPageOperations operations;
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
        const auto completed=[state,guard,destination](const QByteArray& bytes){
            state->done=true;
            if (guard && bytes.startsWith("%PDF-")) {
                QString error;state->valid=WizPdfExport::writeFile(destination,bytes,error);
                if(!state->valid)qWarning()<<"Synthetic PDF helper failure:"<<error;
            }
            if(guard)guard->quit();
        };
        if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EVENT_LINKS"))
            operations.print(reader.page(),[]{return true;},layout,completed);
        else reader.page()->printToPdf(completed,layout);
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
    QVariantMap svgTrials;
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_SVG_TARGET_PROTOTYPE")) {
        // Test only: independently vary SVG target namespace and fragment
        // encoding. Reuse an existing HTML box; add no print layout/style.
        evaluate(reader.page(),R"JS((()=>{
          window.FixtureSvgTrials=[];
          window.PreparePdfSvgTrial=function(moveToHtml,rawHref,modernHref){
            const target=document.querySelector('svg [id^="mjx-eqn-"]');
            if(!target)return false;
            const id=target.getAttribute('id'),box=target.closest('.MathJax_SVG_Display');
            if(!box||box.namespaceURI!=='http://www.w3.org/1999/xhtml'||box.hasAttribute('id'))return false;
            const rect=()=>{const r=box.getBoundingClientRect();return [r.x,r.y,r.width,r.height];};
            const before=rect(),links=[];
            for(const a of document.querySelectorAll('a')){
              const attr=Array.from(a.attributes).find(x=>x.localName==='href'||x.name==='xlink:href');
              const href=attr&&attr.value;if(!href||!href.includes('#'))continue;
              let decoded;try{decoded=decodeURIComponent(href.slice(href.indexOf('#')+1));}catch(_){continue;}
              if(decoded!==id)continue;
              const set=value=>attr.namespaceURI?a.setAttributeNS(attr.namespaceURI,attr.name,value):a.setAttribute(attr.name,value);
              const oldModern=a.getAttributeNS(null,'href'),hadModern=a.hasAttributeNS(null,'href');
              links.push({set,href,name:attr.name,namespace:attr.namespaceURI,
                restore:()=>{if(modernHref){if(hadModern)a.setAttributeNS(null,'href',oldModern);else a.removeAttributeNS(null,'href');}else set(href);}});
              if(modernHref)a.setAttributeNS(null,'href',rawHref?'#'+id:href);
              else if(rawHref)set('#'+id);
            }
            if(moveToHtml){target.id='wiz-isolated-svg-target';box.id=id;}
            const record={moveToHtml,rawHref,modernHref,id,links:links.length,hrefAttributes:links.map(x=>({name:x.name,namespace:x.namespace})),targetTag:document.getElementById(id).tagName,
              namespace:document.getElementById(id).namespaceURI,before,after:rect()};
            FixtureSvgTrials.push(record);
            window.RestorePdfSvgTrial=()=>{
              if(moveToHtml){box.removeAttribute('id');target.id=id;}
              for(const x of links)x.restore();
              record.restoredRect=rect();
            };
            return links.length===1&&JSON.stringify(before)===JSON.stringify(record.after);
          };return true;
        })())JS");
        const auto trial=[&](const QString& filename,bool html,bool raw,bool modern=false){
            const bool prepared=evaluate(reader.page(),QString("PreparePdfSvgTrial(%1,%2,%3)")
                .arg(html?"true":"false",raw?"true":"false",modern?"true":"false")).toBool();
            failures+=!prepared;
            if(prepared)failures+=!print(filename,5);
            evaluate(reader.page(),"if(window.RestorePdfSvgTrial)RestorePdfSvgTrial();true");
        };
        trial("references-svg-raw.pdf",false,true);
        trial("references-html-encoded.pdf",true,false);
        trial("references-html-raw.pdf",true,true);
        trial("references-svg2-encoded.pdf",false,false,true);
        trial("references-svg2-raw.pdf",false,true,true);
        svgTrials=evaluate(reader.page(),"({trials:FixtureSvgTrials})").toMap();
    }
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
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EQUATION_GEOMETRY"))evaluate(reader.page(),"RestorePdfEquationGeometry();true");
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EDGE_PROTOTYPE"))evaluate(reader.page(),"RestorePdfEdgeProbe();true");
    const bool sourceAfter=evaluate(reader.page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource").toBool();
    const bool htmlSame=evaluate(reader.page(),"WizEditor.getContentHtml()").toString()==htmlBefore;
    const bool modifiedSame=evaluate(reader.page(),"WizEditor.isModified()")==modifiedBefore;
    const auto state=evaluate(reader.page(),"({blocks:document.querySelectorAll('.wiz-math-block').length,jax:MathJax.Hub.getAllJax().length,errors:document.querySelectorAll('.MathJax_Error,merror').length})").toMap();
    failures+=!sourceBefore||!sourceAfter||!htmlSame||!modifiedSame||requests.blocked.load()!=0;
    failures+=state.value("blocks").toInt()!=2||state.value("errors").toInt()!=0||state.value("jax").toInt()!=expectedJax;
    QVariantMap eventLinks;
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EVENT_LINKS")){
        eventLinks=evaluate(reader.page(),"globalThis.__wizPdfSvgLinks?.status()",QWebEngineScript::ApplicationWorld).toMap();
        failures+=eventLinks.isEmpty()||eventLinks.value("active").toBool()||eventLinks.value("pending").toInt()!=0
            ||eventLinks.value("prepares").toInt()!=3||eventLinks.value("restores").toInt()!=3||eventLinks.value("errors").toInt()!=0;
    }
    QJsonDocument result(QJsonObject{{"kind","actual Qt printToPdf; synthetic original reader, no user data"},
        {"qtRuntime",qVersion()},{"webEngine",qWebEngineVersion()},{"chromium",qWebEngineChromiumVersion()},
        {"chromiumSecurityPatch",qWebEngineChromiumSecurityPatchVersion()},
        {"runtimeLibraries",runtimeLibraries},
        {"sdkRuntime",sdk},{"sourceBefore",sourceBefore},{"sourceAfter",sourceAfter},{"htmlSame",htmlSame},{"modifiedSame",modifiedSame},{"blockedRequests",requests.blocked.load()},
        {"productionFileHelper",true},{"cssPageMarginsProbe",qEnvironmentVariableIsSet("WIZNOTE_PDF_CSS_PAGE_MARGINS")},
        {"referenceDom",QJsonObject::fromVariantMap(referenceDom)},
        {"equationGeometryMarker",QJsonObject::fromVariantMap(equationMarker)},
        {"svgTrials",QJsonObject::fromVariantMap(svgTrials)},
        {"eventLinks",QJsonObject::fromVariantMap(eventLinks)},
        {"edgeFixture",qEnvironmentVariableIsSet("WIZNOTE_PDF_EDGE_PROTOTYPE")},
        {"anchorPrototype",qEnvironmentVariableIsSet("WIZNOTE_PDF_ANCHOR_PROTOTYPE")},{"transformVariant",qEnvironmentVariableIsSet("WIZNOTE_PDF_ANCHOR_TRANSFORM")},
        {"renderState",QJsonObject::fromVariantMap(state)},{"failures",failures}});
    QFile record(QDir(output).filePath("native-pdf-results.json"));
    if (!record.open(QIODevice::WriteOnly|QIODevice::NewOnly)) return 4;
    record.write(result.toJson());qInfo().noquote()<<result.toJson();
    return failures?1:0;
}
