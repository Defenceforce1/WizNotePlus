#pragma once
// Actual public editor API, synthetic source, local resources, hidden native view.
// The navigation spy models the product's forwarding policy but never opens URLs.
class MathReferencePage final:public QWebEnginePage {
public:
 using QWebEnginePage::QWebEnginePage;
 QJsonArray forwarded;
 bool acceptNavigationRequest(const QUrl& url,NavigationType type,bool main)override{
  if(type==NavigationTypeLinkClicked&&main){forwarded.append(url.toString());return false;}
  return QWebEnginePage::acceptNavigationRequest(url,type,main);
 }
};
static int runMathReferences(QTemporaryDir& root,const QString& resources,const QString& output){
 QFile marker(output+"/.wiz-synthetic-math-references");if(!marker.open(QIODevice::ReadOnly)||marker.readAll()!="synthetic-only\n"||QGuiApplication::platformName()!="cocoa")return 4;
 int checks=0,failures=0;const auto check=[&](bool ok,const char* name){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<name;return ok;};
 QWebEngineProfile profile;profile.setCachePath(root.filePath("cache"));profile.setPersistentStoragePath(root.filePath("profile"));LocalOnly local;profile.setUrlRequestInterceptor(&local);
 QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(1000,700);auto* page=new MathReferencePage(&profile,&view);view.setPage(page);QWebChannel channel;Bridge editor,explorer;channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);page->setWebChannel(&channel);
 QString source=QStringLiteral("# Synthetic cross references\n\nForward \\eqref{eq:late} and \\ref{thm:late}.\n\n"
  "$$\\begin{equation}A+B=C\\label{eq:dollars}\\end{equation}$$\n\n"
  "\\begin{equation}\\label{eq:bare}\nx+y=z\n\\end{equation}\n\n"
  "\\begin{align}a&=b\\\\c&=d\\label{eq:align}\\end{align}\n\n"
  "By \\eqref{eq:dollars}, \\ref{eq:dollars}, $\\eqref{eq:bare}$ and $\\ref{eq:bare}$.\n\n"
  "By \\eqref{eq:bare}, \\ref{eq:bare}, \\eqref{eq:align}.\n\n"
  "\\begin{theorem}\\label{thm:late}\nFor all $x$, $x^2\\ge0$. By \\ref{eq:bare}.\n\\end{theorem}\n\n"
  "> **定义（合成）** \\label{def:quote}\n>\n> A synthetic definition.\n\n"
  "See \\ref{thm:late}, \\ref{def:quote}.\n\n")+QString("Padding ordinary paragraph.\n\n").repeated(100)+QStringLiteral("\\begin{equation}t=u\\label{eq:late}\\end{equation}\n\nLast \\eqref{eq:late}.\n");
 QFile provided(output+"/synthetic-source.md");if(provided.exists()){if(!provided.open(QIODevice::ReadOnly))return 5;source=QString::fromUtf8(provided.readAll());}
 editor.original="<!doctype html><html><head><meta charset='utf-8'><base href='"+QUrl::fromLocalFile(root.filePath("/")).toString()+"'></head><body>"+source.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
 QString html=editor.original,scripts;for(const auto& name:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})scripts+="<script name='wiz_inner_script' wiz_style='unsave' src='"+QUrl::fromLocalFile(resources+name).toString().toHtmlEscaped()+"'></script>";
 html.insert(html.lastIndexOf("</body>"),scripts);QFile fixture(root.filePath("references.html"));if(!fixture.open(QIODevice::WriteOnly))return 5;fixture.write(html.toUtf8());fixture.close();
 bool loaded=false,ok=false;QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool value){loaded=true;ok=value;});view.show();view.load(WizDocumentPageReady::navigationUrl(QUrl::fromLocalFile(fixture.fileName())));if(!check(wait([&]{return loaded;})&&ok,"synthetic base/query page loads"))return 6;
 page->runJavaScript(WizDocumentPageReady::setupScript("'references'","window.WizSpellCheckEnabled=false;window.WizNoteCss='/* WizNotePlus scientific math environments v1 */';"+WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-reference-user","Synthetic",false,"markdown")+"WizPrepareNoteModeChange();await new Promise(resolve=>WizEditor.off({reader:{type:'markdown'}},resolve));if(window.MathJax&&MathJax.Hub)await new Promise(resolve=>MathJax.Hub.Queue(resolve));WizApplyNoteStyle();"));
 if(!check(wait([&]{return js(page,"window.WizNativeEditorSetup?.state==='ready'").toBool();}),"real reader initialization completes"))return 7;
 if(provided.exists()&&source.startsWith("$$\\begin{equation}")&&source.contains("\\label{eq1}")){
  // Bounded split-pass reproduction of the product's supplemental loader.
  // The first pass has a live equation; the second accepted wrapper references
  // it. Reloading MathJax here loses labels and prints ?? in the new wrapper.
  js(page,"window.probeDone=false;window.probeHub=MathJax.Hub;WizLoadMarkdownEnvironmentMathJax("+literal(QUrl::fromLocalFile(resources+"wizeditor/dependency").toString())+").then(()=>{window.probeSameHub=probeHub===MathJax.Hub;window.probe=document.createElement('span');probe.className='wiz-latex-math';probe.textContent='\\\\eqref{eq1}';document.body.appendChild(probe);MathJax.Hub.Queue(['Typeset',MathJax.Hub,probe,()=>{const jax=MathJax.Hub.getAllJax(probe);window.probeText=jax.length?new DOMParser().parseFromString(jax[0].root.toMathML(''),'text/xml').documentElement.textContent:'missing';jax.forEach(j=>j.Remove());probe.remove();window.probeDone=true;}]);});");
  check(wait([&]{return js(page,"window.probeDone").toBool();}),"actual supplemental-loader split pass completes");
  check(js(page,"window.probeSameHub").toBool(),"live engine is not replaced by supplemental loader");
  check(js(page,"typeof probeText==='string'&&!probeText.includes('?')&&probeText.includes('1')").toBool(),"new wrapper resolves first-pass equation label instead of question marks");
  qInfo()<<"SPLIT_PASS_REFERENCE"<<js(page,"window.probeText");
 }
 const auto snapshot=[&]{return js(page,R"JS((()=>({url:location.href,version:MathJax.version,scroll:scrollY,refs:[...document.querySelectorAll('a')].map(a=>({href:a.getAttribute('href')||a.getAttributeNS('http://www.w3.org/1999/xlink','href'),text:a.textContent,html:a.outerHTML})),targets:[...document.querySelectorAll('[id^="mjx-eqn-"],[id^="wiz-math-"]')].map(e=>e.id),jax:MathJax.Hub.getAllJax().map(j=>j.originalText),errors:[...document.querySelectorAll('.MathJax_Error,merror,.merror,.noError')].map(e=>e.textContent)}))())JS").toMap();};
 QJsonArray stages;for(int cycle=0;cycle<2;++cycle){
  check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"exact source preserved");
  const auto state=snapshot();qInfo().noquote()<<"REF_STATE"<<QJsonDocument(QJsonObject::fromVariantMap(state)).toJson(QJsonDocument::Compact);stages.append(QJsonObject::fromVariantMap(state));
  check(state.value("errors").toList().isEmpty(),"no math errors");
  const auto refs=state.value("refs").toList();check(refs.size()==(provided.exists()?2:13),"all expected references render");
  check(js(page,"MathJax.Hub.getAllJax().filter(j=>/\\\\(?:eqref|ref)\\{/.test(j.originalText)).every(j=>!new DOMParser().parseFromString(j.root.toMathML(''),'text/xml').documentElement.textContent.includes('?'))").toBool(),"reference glyphs are resolved numbers not question marks");
  for(int i=0;i<refs.size();++i){
   js(page,"window.scrollTo(0,document.documentElement.scrollHeight/2)");const auto result=js(page,"(()=>{const a=[...document.querySelectorAll('a')]["+QString::number(i)+"];const href=a.getAttribute('href')||a.getAttributeNS('http://www.w3.org/1999/xlink','href');const u=new URL(href,document.baseURI);const target=document.getElementById(decodeURIComponent(u.hash.slice(1)));const event=new MouseEvent('click',{bubbles:true,cancelable:true});const child=a.querySelector('use,path')||a;child.dispatchEvent(event);return {href,text:a.textContent,target:!!target,prevented:event.defaultPrevented,top:target?.getBoundingClientRect().top,scroll:scrollY};})()").toMap();
   qInfo().noquote()<<"REF_CLICK"<<QJsonDocument(QJsonObject::fromVariantMap(result)).toJson(QJsonDocument::Compact);check(result.value("target").toBool(),"reference has actual target");check(result.value("prevented").toBool(),"math click handled locally before external forwarding");check(result.value("top").toDouble()>=-1&&result.value("top").toDouble()<700,"clicked target enters viewport");
  }
  check(page->forwarded.isEmpty(),"no reference forwarded to external opener");
  if(cycle==0){js(page,"window.modeDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>window.modeDone=true);");check(wait([&]{return js(page,"modeDone&&document.body.isContentEditable").toBool();}),"edit mode completes");check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"editing source intact");js(page,"window.modeDone=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>{WizApplyNoteStyle();window.modeDone=true;});");check(wait([&]{return js(page,"modeDone&&!document.body.isContentEditable").toBool();}),"return to reader completes");}
 }
 check(local.blocked.load()==0,"no external requests");check(!view.windowHandle()||!view.windowHandle()->isExposed(),"no exposed native window");
 const QJsonObject report{{"checks",checks},{"failures",failures},{"stages",stages},{"forwarded",page->forwarded},{"externalBlocked",local.blocked.load()},{"sourceIntact",js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source}};QFile result(output+"/runtime.json");if(!result.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 8;result.write(QJsonDocument(report).toJson(QJsonDocument::Indented));profile.setUrlRequestInterceptor(nullptr);return failures?9:0;
}
