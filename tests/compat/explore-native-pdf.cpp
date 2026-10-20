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
#include <QtCore/QElapsedTimer>
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
    const bool glyphPrototype=qEnvironmentVariableIsSet("WIZNOTE_PDF_GLYPH_PROTOTYPE");
    const QString pageMode=qEnvironmentVariable("WIZNOTE_PDF_PAGE_MODE");
    if (!pageMode.isEmpty() && pageMode!="reading" && pageMode!="a4") return 2;
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EVENT_LINKS")) {
        if(glyphPrototype) {
            QFile prototype(QStringLiteral(__FILE__).replace("explore-native-pdf.cpp","prototype-pdf-number-glyphs.js"));
            if(!prototype.open(QIODevice::ReadOnly))return 2;
            QString source=WizPdfSvgLinks::source();
            const QString marker="badge.appendChild(clone);";
            if(source.count(marker)!=1)return 2;
            source.replace(marker,QString::fromUtf8(prototype.readAll())+"\n"+marker);
            QWebEngineScript script;script.setName("wiz-pdf-svg-links");
            script.setWorldId(QWebEngineScript::ApplicationWorld);script.setInjectionPoint(QWebEngineScript::DocumentReady);
            script.setSourceCode(source);reader.page()->scripts().insert(script);
        } else WizPdfSvgLinks::install(reader.page());
    }
    reader.resize(viewportWidth,660); reader.show(); reader.load(QUrl::fromLocalFile(input));
    bool ready=false; QEventLoop readyLoop; QTimer poll,deadline;
    QObject::connect(&poll,&QTimer::timeout,&readyLoop,[&]{
        ready=evaluate(reader.page(),"window.FixtureReady===true").toBool(); if (ready) readyLoop.quit();
    });
    QObject::connect(&deadline,&QTimer::timeout,&readyLoop,&QEventLoop::quit);
    poll.start(100);deadline.setSingleShot(true);deadline.start(65000);readyLoop.exec();poll.stop();deadline.stop();
    if (!ready) {qCritical()<<"Synthetic reader never became ready";return 3;}
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_SOURCE_DIAGNOSTIC")) {
        auto details=evaluate(reader.page(),R"JS((()=>{
          const current=WizEditor.getMarkdownSrc({unEscapeHtml:true}),original=FixtureSource,
            initial=window.FixtureSourceAfterInit,html=WizEditor.getContentHtml(),changes=[];
          for(let i=0;i<Math.max(current.length,original.length)&&changes.length<10;++i)
            if(current[i]!==original[i])changes.push({offset:i,original:original.charCodeAt(i),current:current.charCodeAt(i)});
          return {providedCopy:window.FixtureProvidedCopy===true,originalLength:original.length,
            initializedLength:initial?.length,currentLength:current.length,
            initialMatchesProvided:initial===original,currentMatchesInitialized:current===initial,
            htmlMatchesInitialized:html===window.FixtureHtmlAfterInit,
            originalNbsp:(original.match(/\u00a0/g)||[]).length,
            initializedHtmlNbsp:(window.FixtureHtmlAfterInit.match(/\u00a0|&nbsp;|&#160;/g)||[]).length,
            currentHtmlNbsp:(html.match(/\u00a0|&nbsp;|&#160;/g)||[]).length,
            initialHtml:window.FixtureHtmlAfterInit,currentHtml:html,changes};
        })())JS").toMap();
        evaluate(reader.page(),"window.FixtureEditReady=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>{window.FixtureEditReady=true;});true");
        QElapsedTimer editingDeadline;editingDeadline.start();
        bool editingReady=false;
        while(editingDeadline.elapsed()<10000) {
            if(evaluate(reader.page(),"window.FixtureEditReady===true").toBool()){editingReady=true;break;}
            QEventLoop turn;QTimer::singleShot(20,&turn,&QEventLoop::quit);turn.exec();
        }
        details.insert("editingReady",editingReady);
        details.insert("editing",evaluate(reader.page(),R"JS((()=>{
          const html=WizEditor.getContentHtml();return {
            htmlMatchesInitialized:html===window.FixtureHtmlAfterInit,
            sourceMatchesInitialized:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSourceAfterInit,
            htmlNbsp:(html.match(/\u00a0|&nbsp;|&#160;/g)||[]).length,html};
        })())JS"));
        QFile record(QDir(output).filePath("source-diagnostic.json"));
        if(!record.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 4;
        record.write(QJsonDocument(QJsonObject::fromVariantMap(details)).toJson());
        return details.isEmpty()?3:0;
    }
    // The original reader uses document.open/write on editing transitions.
    // That keeps window globals but removes their event listeners. Exercise
    // the real editor path after the isolated print helper was installed.
    const int readerCycles=qEnvironmentVariableIntValue("WIZNOTE_PDF_READER_CYCLES");
    if(readerCycles<0||readerCycles>5)return 2;
    for(int cycle=0;cycle<readerCycles;++cycle) {
        const auto transition=[&](bool edit) {
            evaluate(reader.page(),QString("window.FixtureModeReady=false;WizPrepareNoteModeChange();"
                "WizEditor.%1(%2,()=>{window.FixtureModeReady=true;});true")
                .arg(edit?"on":"off",edit?"null":"{reader:{type:'markdown'}}"));
            QElapsedTimer elapsed;elapsed.start();
            while(elapsed.elapsed()<65000) {
                if(evaluate(reader.page(),"window.FixtureModeReady===true").toBool())return true;
                QEventLoop turn;QTimer::singleShot(10,&turn,&QEventLoop::quit);turn.exec();
            }
            return false;
        };
        if(!transition(true)||!transition(false)) {qCritical()<<"Reader transition timed out";return 3;}
    }
    const bool sourceBefore=evaluate(reader.page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource").toBool();
    const bool providedCopy=evaluate(reader.page(),"window.FixtureProvidedCopy===true").toBool();
    const auto htmlBefore=evaluate(reader.page(),"WizEditor.getContentHtml()").toString();
    const auto modifiedBefore=evaluate(reader.page(),"WizEditor.isModified()");
    const int expectedJax=evaluate(reader.page(),"window.FixtureExpectedJax||1").toInt();
    const int expectedBlocks=evaluate(reader.page(),"Number.isInteger(window.FixtureExpectedBlocks)?FixtureExpectedBlocks:2").toInt();
    const int expectedFitted=evaluate(reader.page(),"Number.isInteger(window.FixtureExpectedFitted)?FixtureExpectedFitted:-1").toInt();
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
        providedCopy:window.FixtureProvidedCopy===true,
        providedDetails:window.FixtureProvidedCopy===true?{
          mathSources:MathJax.Hub.getAllJax().map(j=>({inputID:j.inputID,source:j.originalText})),
          blocks:Array.from(document.querySelectorAll('.wiz-math-block')).map(b=>({id:b.id,heading:b.getAttribute('data-wiz-math-heading'),text:b.innerText})),
          displays:Array.from(document.querySelectorAll('.MathJax_SVG_Display')).map(b=>({html:b.outerHTML.slice(0,32000),width:b.getBoundingClientRect().width})),
          text:document.body.innerText
        }:null,
        viewport:{width:innerWidth,height:innerHeight,scrollWidth:document.documentElement.scrollWidth},
        equationTargets:Array.from(document.querySelectorAll('[id]')).filter(e=>e.id.startsWith('mjx-eqn-')).map(e=>({id:e.id,tag:e.tagName,namespace:e.namespaceURI,html:e.outerHTML.slice(0,1600)}))};
    })())JS").toMap();
    QVariantMap equationMarker;
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EQUATION_GEOMETRY")) {
        equationMarker=evaluate(reader.page(),R"JS((()=>{
          const targets=Array.from(document.querySelectorAll('[id]')).filter(e=>e.id.startsWith('mjx-eqn-')&&e.namespaceURI==='http://www.w3.org/2000/svg');
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
    if(!providedCopy)evaluate(reader.page(),R"JS((()=>{
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
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_MULTLINE_PROTOTYPE")) {
        QFile prototype(QStringLiteral(__FILE__).replace("explore-native-pdf.cpp","prototype-pdf-multline-fit.js"));
        if(!prototype.open(QIODevice::ReadOnly))return 2;
        evaluate(reader.page(),QString::fromUtf8(prototype.readAll()),QWebEngineScript::ApplicationWorld);
    }
    WizPdfPageOperations operations;
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_PAPER_DIAGNOSTIC")) {
        const auto captured=evaluate(reader.page(),WizPdfPaperPage::captureScript()).toMap();
        QEventLoop loop;bool accepted=false;
        const auto layout=WizPdfExport::pageLayout(0,QMarginsF(20,20,20,20));
        WizPdfPaperPage::create(&operations,reader.page(),captured,layout.paintRect(QPageLayout::Point).width(),
            QUrl::fromLocalFile(QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath()).absoluteFilePath(
                "../../share/files/wizeditor/dependency/mathjax/MathJax.js")),[]{return true;},
            [&](WizPdfPaperPage* session,QWebEnginePage* paper) {
            QTimer::singleShot(0,&operations,[&,session,paper] {
            if(paper){
                const auto data=evaluate(paper,"({html:document.body.innerHTML,renderer:MathJax.Hub.config.menuSettings.renderer,"
                    "outputs:MathJax.Hub.getAllJax().map(j=>j.outputJax),editable:document.body.isContentEditable,geometry:__wizPaperResult,"
                    "targets:Array.from(document.querySelectorAll('svg [id^=mjx-eqn-]')).map(e=>{const b=e.getBBox();return {id:e.id,bbox:{x:b.x,y:b.y,width:b.width,height:b.height},rect:e.getBoundingClientRect().toJSON()};})})");
                QFile record(QDir(output).filePath("paper-diagnostic.json"));
                accepted=record.open(QIODevice::WriteOnly|QIODevice::NewOnly);
                if(accepted)record.write(QJsonDocument(QJsonObject::fromVariantMap(data.toMap())).toJson());
                record.close();
                evaluate(paper,WizPdfSvgLinks::source(),QWebEngineScript::ApplicationWorld);
                evaluate(paper,"addEventListener('beforeprint',()=>{globalThis.paperDuring={html:document.body.innerHTML,status:__wizPdfSvgLinks.status()};});true",QWebEngineScript::ApplicationWorld);
                operations.print(paper,[]{return true;},layout,[&,session,paper](const QByteArray& bytes) {
                    QTimer::singleShot(0,&operations,[&,session,paper,bytes] {
                        const auto observed=evaluate(paper,"({during:paperDuring,after:__wizPdfSvgLinks.status()})",QWebEngineScript::ApplicationWorld);
                        QFile after(QDir(output).filePath("paper-print-diagnostic.json"));
                        if(after.open(QIODevice::WriteOnly|QIODevice::NewOnly))after.write(QJsonDocument(QJsonObject::fromVariantMap(observed.toMap())).toJson());
                        QString error;WizPdfExport::writeFile(QDir(output).filePath("paper-diagnostic.pdf"),bytes,error);
                        session->finish();loop.quit();
                    });
                });
                return;
            }
            loop.quit();
            });
        });
        loop.exec();return accepted?0:3;
    }
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
        if (!pageMode.isEmpty())
            operations.printWithMode(reader.page(),[]{return true;},0,QMarginsF(margin,margin,margin,margin),
                pageMode=="a4"?WizPdfExport::Mode::A4:WizPdfExport::Mode::ReadingWidth,completed,[]{return true;},
                QUrl::fromLocalFile(QDir(QFileInfo(QStringLiteral(__FILE__)).absolutePath()).absoluteFilePath(
                    "../../share/files/wizeditor/dependency/mathjax/MathJax.js")));
        else if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EVENT_LINKS"))
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
    if(!providedCopy)evaluate(reader.page(),"RestorePdfControl();true");
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EQUATION_GEOMETRY"))evaluate(reader.page(),"RestorePdfEquationGeometry();true");
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EDGE_PROTOTYPE"))evaluate(reader.page(),"RestorePdfEdgeProbe();true");
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_MULTLINE_PROTOTYPE"))evaluate(reader.page(),"RestoreMultlineFitPrototype();true",QWebEngineScript::ApplicationWorld);
    const bool sourceAfter=evaluate(reader.page(),"WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource").toBool();
    const bool htmlSame=evaluate(reader.page(),"WizEditor.getContentHtml()").toString()==htmlBefore;
    const bool modifiedSame=evaluate(reader.page(),"WizEditor.isModified()")==modifiedBefore;
    const auto state=evaluate(reader.page(),"({blocks:document.querySelectorAll('.wiz-math-block').length,jax:MathJax.Hub.getAllJax().length,errors:document.querySelectorAll('.MathJax_Error,merror').length})").toMap();
    // Provided copies may contain NBSP: the stock editor's Markdown getter
    // normalizes it before any preview. Keep raw-input comparison in the JSON,
    // but verify the stronger exact editable-body round trip below instead of
    // silently changing input or calling that legacy normalization a print bug.
    failures+=(!providedCopy&&(!sourceBefore||!sourceAfter))||!htmlSame||!modifiedSame||requests.blocked.load()!=0;
    failures+=state.value("blocks").toInt()!=expectedBlocks||state.value("errors").toInt()!=0||state.value("jax").toInt()!=expectedJax;
    QVariantMap eventLinks;
    if(qEnvironmentVariableIsSet("WIZNOTE_PDF_EVENT_LINKS")){
        eventLinks=evaluate(reader.page(),"globalThis.__wizPdfSvgLinks?.status()",QWebEngineScript::ApplicationWorld).toMap();
        const int expectedSourcePrints=pageMode=="a4"?0:3;
        failures+=eventLinks.isEmpty()||eventLinks.value("active").toBool()||eventLinks.value("pending").toInt()!=0
            ||eventLinks.value("prepares").toInt()!=expectedSourcePrints||eventLinks.value("restores").toInt()!=expectedSourcePrints||eventLinks.value("errors").toInt()!=0;
        if(pageMode!="a4"&&expectedFitted>=0&&!qEnvironmentVariableIsSet("WIZNOTE_PDF_MULTLINE_PROTOTYPE"))
            failures+=eventLinks.value("lastFitted").toInt()!=expectedFitted;
    }
    QVariantMap providedRoundTrip;
    if(providedCopy) {
        evaluate(reader.page(),"window.FixtureEditReady=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>{window.FixtureEditReady=true;});true");
        QElapsedTimer editingDeadline;editingDeadline.start();
        bool editingReady=false;
        while(editingDeadline.elapsed()<10000) {
            if(evaluate(reader.page(),"window.FixtureEditReady===true").toBool()){editingReady=true;break;}
            QEventLoop turn;QTimer::singleShot(20,&turn,&QEventLoop::quit);turn.exec();
        }
        providedRoundTrip=evaluate(reader.page(),R"JS((()=>{
          const html=WizEditor.getContentHtml(),parser=new DOMParser();
          const seed=parser.parseFromString(FixtureHtmlAfterInit,'text/html').body.innerHTML;
          const actual=parser.parseFromString(html,'text/html').body.innerHTML;
          const source=WizEditor.getMarkdownSrc({unEscapeHtml:true});
          return {canonicalGetterSame:source===FixtureSourceAfterInit,htmlBodySame:actual===seed,
            originalInputGetterSame:source===FixtureSource,
            initializedHtmlNbsp:(seed.match(/\u00a0|&nbsp;|&#160;/g)||[]).length,
            restoredHtmlNbsp:(actual.match(/\u00a0|&nbsp;|&#160;/g)||[]).length};
        })())JS").toMap();
        providedRoundTrip.insert("editingReady",editingReady);
        failures+=!editingReady||!providedRoundTrip.value("htmlBodySame").toBool()
            ||!providedRoundTrip.value("canonicalGetterSame").toBool();
    }
    QJsonDocument result(QJsonObject{{"kind",providedCopy?"actual Qt printToPdf; user-provided pasted copy, isolated original reader":"actual Qt printToPdf; synthetic original reader, no user data"},
        {"providedMarkdownCopy",providedCopy},
        {"pageMode",pageMode},{"cssViewportWidth",evaluate(reader.page(),"window.innerWidth").toDouble()},
        {"numberGlyphPrototype",glyphPrototype},
        {"providedSourceRoundTrip",QJsonObject::fromVariantMap(providedRoundTrip)},
        {"qtRuntime",qVersion()},{"webEngine",qWebEngineVersion()},{"chromium",qWebEngineChromiumVersion()},
        {"chromiumSecurityPatch",qWebEngineChromiumSecurityPatchVersion()},
        {"runtimeLibraries",runtimeLibraries},
        {"sdkRuntime",sdk},{"sourceBefore",sourceBefore},{"sourceAfter",sourceAfter},{"htmlSame",htmlSame},{"modifiedSame",modifiedSame},{"blockedRequests",requests.blocked.load()},
        {"productionFileHelper",true},{"cssPageMarginsProbe",qEnvironmentVariableIsSet("WIZNOTE_PDF_CSS_PAGE_MARGINS")},
        {"readerCycles",readerCycles},
        {"multlinePrototype",qEnvironmentVariableIsSet("WIZNOTE_PDF_MULTLINE_PROTOTYPE")},{"expectedFitted",expectedFitted},
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
