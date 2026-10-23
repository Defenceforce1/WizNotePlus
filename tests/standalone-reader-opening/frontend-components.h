#pragma once
// Public editor/CodeMirror APIs only; bounded synthetic compatibility test.
static int runFrontendComponents(QTemporaryDir& root,const QString& resources,const QString& output){
 QFile marker(output+"/.wiz-synthetic-frontend-components");if(!marker.open(QIODevice::ReadOnly)||marker.readAll()!="synthetic-only\n")return 4;
 int checks=0,failures=0;const auto check=[&](bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;return ok;};
 QWebEngineProfile profile;profile.setCachePath(root.filePath("cache"));profile.setPersistentStoragePath(root.filePath("profile"));LocalOnly local;profile.setUrlRequestInterceptor(&local);
 QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(1000,700);auto* page=new QWebEnginePage(&profile,&view);view.setPage(page);QWebChannel channel;Bridge editor,explorer;channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);page->setWebChannel(&channel);
 // The unchanged editor expands literal HTML tabs to four spaces before its
 // source getter (also reproduced with build220). Use canonical source here;
 // exercise literal tabs separately through CodeMirror's public value API.
 const QString code="const 文本 = '<tag>& 😀';\n    function square(x) { return x * x; }\n// \\begin{equation}code_only\\end{equation}";
 const QString source="# Synthetic frontend components\n\n```javascript\n"+code+"\n```\n\n\\begin{proposition}[Markdown]\\label{prop:frontend}\n**Bold** and *italic*, `x_y`, &amp; \\*.\n\n- First\n  - Nested\n- Last\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n> Quoted paragraph.\n\nFor $x$, $x^2\\ge0$.\n\\end{proposition}\n\nSee \\ref{prop:frontend}.\n";
 editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+source.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
 QString html=editor.original,scripts;for(const auto& name:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})scripts+="<script name='wiz_inner_script' wiz_style='unsave' src='"+QUrl::fromLocalFile(resources+name).toString().toHtmlEscaped()+"'></script>";
 html.insert(html.lastIndexOf("</body>"),scripts);QFile fixture(root.filePath("frontend.html"));if(!fixture.open(QIODevice::WriteOnly))return 5;fixture.write(html.toUtf8());fixture.close();bool loaded=false,loadOk=false;QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool ok){loaded=true;loadOk=ok;});view.show();view.load(QUrl::fromLocalFile(fixture.fileName()));if(!check(wait([&]{return loaded;})&&loadOk,"local original editor loads"))return 6;
 page->runJavaScript(WizDocumentPageReady::setupScript("'frontend-components'","window.WizSpellCheckEnabled=false;window.WizNoteCss="+literal(WizNotePresentation::mathBlockStyle()+WizNotePresentation::scientificMathStyle())+";"+WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-frontends","Synthetic",false,"markdown")+"await new Promise(r=>WizEditor.off({reader:{type:'markdown'}},r));WizApplyNoteStyle();"));
 if(!check(wait([&]{return js(page,"window.WizNativeEditorSetup?.state==='ready'").toBool();}),"real public reader initialization completes"))return 7;
 check(js(page,"CodeMirror.version==='5.65.21'").toBool(),"expected real CodeMirror5 maintenance version loaded");
 check(js(page,"typeof marked.parse==='function'&&typeof marked.parse('**plain**',{async:false})==='string'").toBool(),"local Marked synchronous API retained");
 const QString codeCheck="document.querySelector('.CodeMirror')?.CodeMirror?.getValue()==="+literal(code);
 check(js(page,codeCheck).toBool(),"actual code block preserves Unicode indentation punctuation and literal TeX");
 check(js(page,"!document.querySelector('.CodeMirror .MathJax_SVG,.CodeMirror mjx-container')&&MathJax.Hub.getAllJax().length===2").toBool(),"code remains code and only theorem math typesets");
 check(js(page,"(()=>{const b=document.querySelector('.wiz-latex-theorem');return !!(b?.querySelector('strong')&&b.querySelector('em')&&b.querySelector('ul ul li')&&b.querySelector('table td')&&b.querySelector('blockquote'));})()").toBool(),"theorem emphasis nested lists tables and quote retain markup");
 check(js(page,"document.querySelector('.wiz-math-reference')?.textContent==='1'&&!!document.getElementById(document.querySelector('.wiz-math-reference').hash.slice(1))").toBool(),"proposition numbering and reference resolve");
 check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"reader original Markdown intact");
 js(page,"window.codeHost=document.createElement('div');document.body.appendChild(codeHost);window.probeCode=CodeMirror(codeHost,{value:'function f(x) {\\n\\treturn x+1;\\n}',mode:'javascript',lineNumbers:true,matchBrackets:true,styleActiveLine:true});");
 check(js(page,"probeCode.getValue().includes('\\treturn')").toBool(),"CodeMirror literal tab value is not expanded");
 check(js(page,"!!probeCode.getWrapperElement().querySelector('.CodeMirror-scroll')&&!!probeCode.getWrapperElement().querySelector('.CodeMirror-linenumbers')").toBool(),"existing scroll and line-number DOM contract retained");
 check(js(page,"probeCode.getMode().name==='javascript'&&typeof probeCode.getTokenAt({line:0,ch:8}).type==='string'").toBool(),"existing language bundle still highlights JavaScript");
 check(js(page,"typeof CodeMirror.defineSimpleMode==='function'&&typeof probeCode.matchBrackets==='function'").toBool(),"simple-mode and matching-bracket addons remain available");
 check(js(page,"window.codeBefore=probeCode.getValue();probeCode.replaceRange('😀中文',{line:1,ch:2},{line:1,ch:8});probeCode.getValue().includes('😀中文')").toBool(),"CodeMirror public edit API preserves Unicode");
 check(js(page,"probeCode.undo();probeCode.getValue()===codeBefore").toBool(),"public undo restores exact code");
 check(js(page,"probeCode.redo();probeCode.getValue().includes('😀中文')").toBool(),"public redo restores code edit");
 check(js(page,"probeCode.setSelection({line:0,ch:0},{line:0,ch:8});probeCode.getSelection()==='function'").toBool(),"selection positions remain compatible");
 js(page,"probeCode.getInputField().blur();codeHost.remove();window.probeCode=null;window.codeHost=null;");
 check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"isolated code API checks do not alter note source");
 js(page,"window.modeDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>modeDone=true);");check(wait([&]{return js(page,"modeDone&&document.body.isContentEditable").toBool();}),"editing transition completes");
 const auto saved=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::editorScript(true,false)));
 check(saved.state==WizDocumentSaveSnapshot::State::Changed&&!saved.html.contains("<mjx-container")&&js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"actual save snapshot retains original Markdown");
 js(page,"window.modeDone=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>modeDone=true);");check(wait([&]{return js(page,"modeDone&&!document.body.isContentEditable").toBool();})&&js(page,codeCheck).toBool(),"return to reading retains exact code block");
 check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source,"full code and theorem cycle preserves source");
 check(local.blocked.load()==0,"no external requests");check(!view.windowHandle()||!view.windowHandle()->isExposed(),"native window not exposed");
 const QJsonObject report{{"checks",checks},{"failures",failures},{"codeMirrorVersion",js(page,"CodeMirror.version").toString()},{"mathjaxVersion",js(page,"MathJax.version").toString()},{"externalBlocked",local.blocked.load()},{"sourceIntact",js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==source},{"expectedSource",source},{"actualSource",js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()},{"expectedCode",code},{"actualCode",js(page,"document.querySelector('.CodeMirror')?.CodeMirror?.getValue()").toString()}};QFile result(output+"/runtime.json");if(!result.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 8;result.write(QJsonDocument(report).toJson(QJsonDocument::Indented));profile.setUrlRequestInterceptor(nullptr);return failures?9:0;
}
