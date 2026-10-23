#pragma once
#include "share/WizPdfPageOperations.h"
#include "WizNotePresentation.h"
#include "share/WizDocumentAsync.h"
#include <QWebEngineScript>

static QVariant mathjaxApplicationValue(QWebEnginePage* page,const QString& code){
 struct State{bool ready=false;QVariant value;};auto s=std::make_shared<State>();
 page->runJavaScript(code,QWebEngineScript::ApplicationWorld,[s](const QVariant& v){s->value=v;s->ready=true;});
 wait([s]{return s->ready;},5000);return s->value;
}

// Bounded component acceptance using the actual bridge/editor and product PDF
// entry point. Only synthetic source and explicitly marked local output.
static int runMathjaxUpgrade(QTemporaryDir& root,const QString& resources,
                             const QString& output,const QString& version){
 QFile marker(output+"/.wiz-synthetic-mathjax-upgrade");
 if(!marker.open(QIODevice::ReadOnly)||marker.readAll()!="synthetic-only\n"||QGuiApplication::platformName()!="cocoa")return 4;
 int checks=0,failures=0;const auto check=[&](bool ok,const char* label){checks++;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;return ok;};
 QWebEngineProfile profile;profile.setCachePath(root.filePath("cache"));profile.setPersistentStoragePath(root.filePath("profile"));LocalOnly local;profile.setUrlRequestInterceptor(&local);
 QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(1100,700);auto* page=new QWebEnginePage(&profile,&view);view.setPage(page);
 QWebChannel channel;Bridge editor,explorer;channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);page->setWebChannel(&channel);
 QString source=QStringLiteral("# Synthetic component formulas\n\n"
   "$$x+y=z$$\n\n"
   "\\begin{equation}\\label{component:equation}\nA+C-D=\\begin{cases}A\\\\B\\end{cases}\n\\end{equation}\n\n"
   "\\begin{align}a&=b\\\\c&=d\\label{component:align}\\end{align}\n\n"
   "\\begin{gather}u=v\\\\w=z\\label{component:gather}\\end{gather}\n\n"
   "\\begin{multline}a+b+c+d+e+f+g+h+i+j+k+l+m+n+o+p+q+r+s+t\\\\=u+v\\label{component:multline}\\end{multline}\n\n"
   "\\begin{theorem}\\label{component:theorem}\nFor every $x$, $x^2\\ge0$.\n\\begin{equation}y=x^2\\label{component:nested}\\end{equation}\n\\end{theorem}\n\n"
   "$$\\vphantom{\\frac{1}{2}}x+\\int_0^1 t\\,dt$$\n\n"
   "```latex\n\\begin{equation}code_only\\end{equation}\n```\n\n"
   "Reference \\eqref{component:equation}, \\eqref{component:align}, \\eqref{component:multline}, \\ref{component:theorem}.\n");
 if(version=="4.1.3")source+="\n\\begin{multline*}"+QString("a+b+c+d+e+f+g+h+").repeated(5)+"x\\\\=z\\end{multline*}\n";
 editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+source.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
 QString html=editor.original,scripts;for(const auto& name:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})scripts+="<script name='wiz_inner_script' wiz_style='unsave' src='"+QUrl::fromLocalFile(resources+name).toString().toHtmlEscaped()+"'></script>";
 html.insert(html.lastIndexOf("</body>"),scripts);QFile fixture(root.filePath("formula.html"));if(!fixture.open(QIODevice::WriteOnly))return 5;fixture.write(html.toUtf8());fixture.close();
 bool loaded=false,loadOk=false;QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool ok){loaded=true;loadOk=ok;});view.show();view.load(QUrl::fromLocalFile(fixture.fileName()));
 if(!check(wait([&]{return loaded;})&&loadOk,"local unchanged editor fixture loads"))return 6;
 const QString noteCss=version=="4.1.3"?WizNotePresentation::mathBlockStyle()+WizNotePresentation::scientificMathStyle():"/* WizNotePlus scientific math environments v1 */";
 const auto initialize=WizDocumentPageReady::setupScript("'component-upgrade'",
  "window.WizSpellCheckEnabled=false;window.WizNoteCss="+literal(noteCss)+";"+
  WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-component-user","Synthetic",false,"markdown")+
  "WizPrepareNoteModeChange();await new Promise(resolve=>WizEditor.off({reader:{type:'markdown'}},resolve));"
  "if(window.MathJax&&MathJax.Hub)await new Promise(resolve=>MathJax.Hub.Queue(resolve));WizApplyNoteStyle();");
 page->runJavaScript(initialize);
 if(!check(wait([&]{return js(page,"window.WizNativeEditorSetup?.state==='ready'").toBool();}),"actual bridge and formula initialization completes"))return 7;
 if(output.endsWith("serialization-only")){
  QFile saved(output+"/synthetic-serialized.html");if(!saved.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 8;saved.write(js(page,"WizEditor.getContentHtml()").toString().toUtf8());profile.setUrlRequestInterceptor(nullptr);return 0;
 }
 const QString original=js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString();check(original==source,"initial reading preserves exact source");
 check(js(page,"window.MathJax&&MathJax.version==="+literal(version)).toBool(),"expected actual MathJax version loaded");
 const QString valid="document.querySelectorAll('.MathJax_Error,merror,.merror,.noError').length===0&&document.querySelectorAll('.MathJax_SVG svg').length>=9&&['component:equation','component:align','component:gather','component:multline','component:nested'].every(k=>!!document.getElementById('mjx-eqn-'+k))&&!document.querySelector('pre .MathJax_SVG')";
 check(js(page,valid).toBool(),"bare environments nested cases theorem formulas and code exclusion render");
 check(js(page,R"JS((()=>{const refs=[...document.querySelectorAll('a')].map(a=>a.getAttribute('href')||a.getAttributeNS('http://www.w3.org/1999/xlink','href')).filter(h=>h&&h.startsWith('#'));return refs.length>=4&&refs.every(h=>!!document.getElementById(decodeURIComponent(h.slice(1))));})())JS").toBool(),"actual HTML and SVG references resolve to local targets");
 js(page,"window.changedMode=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>window.changedMode=true);");check(wait([&]{return js(page,"window.changedMode&&document.body.isContentEditable").toBool();}),"edit transition completes");
 check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source&&!js(page,"WizEditor.getContentHtml()").toString().isEmpty(),"editing source intact and serialization nonempty");
 if(version=="4.1.3"){
  const auto saved=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::editorScript(true,false)));
  check(saved.state==WizDocumentSaveSnapshot::State::Changed&&!saved.html.contains("<mjx-container")&&!saved.html.contains("WizMathJax4-Element-")&&js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"actual editing save snapshot retains original source without generated v4 markup");
 }
 js(page,"window.changedMode=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>window.changedMode=true);");check(wait([&]{return js(page,"window.changedMode&&!document.body.isContentEditable").toBool();})&&js(page,valid).toBool(),"return to reading retains rendered formulas");
 check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"reading after edit preserves exact source");
 if(version=="4.1.3"){
  QFile serialized(output+"/synthetic-serialized.html");if(serialized.open(QIODevice::WriteOnly|QIODevice::NewOnly))serialized.write(js(page,"WizEditor.getContentHtml()").toString().toUtf8());
  check(js(page,"!window.WizMathJax4Error&&MathJax.startup&&document.querySelectorAll('mjx-container').length>=13&&document.querySelectorAll('[data-semantic-type]').length===0").toBool(),"real v4 SVG engine without semantic enrichment or compatibility errors");
  const auto unchanged=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::readerScript(false)));
  check(unchanged.state==WizDocumentSaveSnapshot::State::Unchanged,"actual unchanged reader save snapshot does not write rendered HTML");
  check(js(page,"MathJax.Hub.getAllJax().filter(j=>/\\\\(?:eqref|ref)\\{/.test(j.originalText)).every(j=>!new DOMParser().parseFromString(j.root.toMathML(''),'text/xml').documentElement.textContent.includes('?'))").toBool(),"formula reference glyphs resolved not question marks");
 }
 const QString beforeHtml=js(page,"WizEditor.getContentHtml()").toString(),beforeDom=js(page,"document.documentElement.outerHTML").toString();const auto beforeModified=js(page,"WizEditor.isModified()");
 mathjaxApplicationValue(page,WizPdfSvgLinks::source());
 check(mathjaxApplicationValue(page,"(()=>{const n=document.querySelector('.CodeMirror > .CodeMirror-scroll');const before=n.getAttribute('style');dispatchEvent(new Event('beforeprint'));return n.getAttribute('style')===before&&!__wizPdfSvgLinks.status().active;})()").toBool(),"unarmed synthetic print does not adjust code gutters");
 check(mathjaxApplicationValue(page,"(()=>{const n=document.querySelector('.CodeMirror > .CodeMirror-scroll');const before=n.getAttribute('style');const armed=__wizPdfSvgLinks.arm();dispatchEvent(new Event('beforeprint'));return armed&&n.getAttribute('style')===before&&!__wizPdfSvgLinks.status().active&&__wizPdfSvgLinks.finish();})()").toBool(),"armed but untrusted print does not adjust code gutters");
 check(js(page,"document.documentElement.outerHTML").toString()==beforeDom,"print guard controls preserve exact reader DOM");
 const auto capture=js(page,WizPdfPaperPage::captureScript()).toMap();qInfo()<<"Component paper snapshot"<<capture.keys()<<capture.value("error")<<capture.value("mathCount")<<capture.value("html").toString().size();
 check(!capture.contains("error")&&js(page,"(()=>{const d=new DOMParser().parseFromString("+literal(capture.value("html").toString())+",'text/html');return d.querySelectorAll('.CodeMirror').length===0&&d.querySelector('[data-wiz-pdf-static-code]')?.textContent==='\\\\begin{equation}code_only\\\\end{equation}';})()").toBool(),"paper clone freezes actual code text without an interactive scroll layer");
 check(js(page,"document.querySelectorAll('.CodeMirror').length===1").toBool()&&js(page,"WizEditor.getContentHtml()").toString()==beforeHtml,"code capture leaves live editor and source intact");
 WizPdfPageOperations operations;QJsonArray pdfs;
 for(const auto mode:{WizPdfExport::Mode::A4,WizPdfExport::Mode::ReadingWidth}){
  const QString name=mode==WizPdfExport::Mode::A4?"a4":"reading";auto done=std::make_shared<bool>(false);auto bytes=std::make_shared<QByteArray>();
  auto paperSamples=std::make_shared<QJsonArray>();QTimer paperProbe;paperProbe.setInterval(20);
  QObject::connect(&paperProbe,&QTimer::timeout,[paperSamples,&view]{for(auto* widget:QApplication::allWidgets())if(auto* candidate=qobject_cast<QWebEngineView*>(widget);candidate&&candidate!=&view&&candidate->page()->property("wizPdfPaperBoundsRequired").toBool()){
   candidate->page()->runJavaScript("(()=>{const d=document.documentElement,b=document.body;return {bounds:globalThis.__wizPaperBounds,links:globalThis.__wizPdfSvgLinks?.status(),width:d.clientWidth,scroll:d.scrollWidth,roots:[d,b].map(e=>{const s=getComputedStyle(e);return {tag:e.tagName,rect:e.getBoundingClientRect().toJSON(),padding:s.padding,margin:s.margin,boxSizing:s.boxSizing,width:s.width};}),overflow:[...document.querySelectorAll('body *')].filter(e=>e.getBoundingClientRect().right>d.clientWidth+1).slice(0,8).map(e=>({tag:e.tagName,cls:e.className?.baseVal||e.className,id:e.id,rect:e.getBoundingClientRect().toJSON()}))};})()",QWebEngineScript::ApplicationWorld,[paperSamples](const QVariant& value){if(paperSamples->size()>=2)paperSamples->removeFirst();paperSamples->append(QJsonObject::fromVariantMap(value.toMap()));});
  }});paperProbe.start();
  operations.printWithMode(page,[]{return true;},0,QMarginsF(10,10,10,10),mode,[done,bytes](const QByteArray& data){*bytes=data;*done=true;},[]{return true;},QUrl::fromLocalFile(resources+"wizeditor/dependency/mathjax/MathJax.js"));
  const bool completed=wait([done]{return *done;},55000);paperProbe.stop();qInfo()<<"Component PDF callback"<<name<<completed<<bytes->size()<<QJsonDocument(*paperSamples).toJson(QJsonDocument::Compact);check(completed&&bytes->startsWith("%PDF-"),"actual product PDF mode completes");
  QFile pdf(output+"/"+name+".pdf");check(pdf.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&pdf.write(*bytes)==bytes->size(),"write isolated complete PDF without overwrite");pdf.close();
  check(js(page,"WizEditor.getContentHtml()").toString()==beforeHtml&&js(page,"document.documentElement.outerHTML").toString()==beforeDom&&js(page,"WizEditor.isModified()")==beforeModified&&js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"PDF mode restores exact source HTML DOM and modified state");
  check(js(page,valid).toBool(),"reader remains rendered after printing");pdfs.append(QJsonObject{{"mode",name},{"bytes",bytes->size()}});
 }
 check(local.blocked.load()==0,"no remote requests during rendering or PDF export");check(!view.windowHandle()||!view.windowHandle()->isExposed(),"native test window never exposed");
 const auto report=QJsonObject{{"checks",checks},{"failures",failures},{"mathJaxVersion",version},{"qtVersion",qVersion()},{"sourceIntact",original==source},{"pdfs",pdfs},{"blockedExternalRequests",local.blocked.load()},{"jaxCount",js(page,"MathJax.Hub.getAllJax().length").toInt()},{"svgCount",js(page,"document.querySelectorAll('.MathJax_SVG svg').length").toInt()},{"source",source}};
 QFile result(output+"/runtime.json");if(!result.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 8;result.write(QJsonDocument(report).toJson(QJsonDocument::Indented));
 profile.setUrlRequestInterceptor(nullptr);qInfo().noquote()<<"COMPONENT_REPORT"<<QJsonDocument(report).toJson(QJsonDocument::Compact);return failures?9:0;
}
