#pragma once
// Public pasteB64/undo/redo APIs and synthetic DOM events only. Never access the
// system clipboard or inspect the bundled editor's implementation.
static int runJqueryPaste(QTemporaryDir& root,const QString& resources,const QString& output){
 QFile marker(output+"/.wiz-synthetic-jquery-paste"),expectedFile(output+"/expected-jquery.txt");
 if(!marker.open(QIODevice::ReadOnly)||marker.readAll()!="synthetic-only\n"||!expectedFile.open(QIODevice::ReadOnly))return 4;
 const QString expected=QString::fromUtf8(expectedFile.readAll()).trimmed();if(expected!="3.3.1"&&expected!="3.7.1")return 4;
 int checks=0,failures=0;const auto check=[&](bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;return ok;};
 QWebEngineProfile profile;profile.setCachePath(root.filePath("cache"));profile.setPersistentStoragePath(root.filePath("profile"));LocalOnly local;profile.setUrlRequestInterceptor(&local);
 QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(900,600);auto* page=new QWebEnginePage(&profile,&view);view.setPage(page);QWebChannel channel;Bridge editor,explorer;channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);page->setWebChannel(&channel);
 const QString source="# 合成粘贴\n\n原文 中文 😀\n";
 editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+source.toHtmlEscaped().replace("\n","<br>")+"</body></html>";
 QString html=editor.original,scripts;for(const auto& name:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})scripts+="<script name='wiz_inner_script' wiz_style='unsave' src='"+QUrl::fromLocalFile(resources+name).toString().toHtmlEscaped()+"'></script>";
 html.insert(html.lastIndexOf("</body>"),scripts);QFile fixture(root.filePath("paste.html"));if(!fixture.open(QIODevice::WriteOnly))return 5;fixture.write(html.toUtf8());fixture.close();bool loaded=false,ok=false;QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool value){loaded=true;ok=value;});view.show();view.load(QUrl::fromLocalFile(fixture.fileName()));if(!check(wait([&]{return loaded;})&&ok,"synthetic paste fixture loads"))return 6;
 page->runJavaScript(WizDocumentPageReady::setupScript("'jquery-paste'","window.WizSpellCheckEnabled=false;window.WizNoteCss='';"+WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-paste","Synthetic",false,"markdown")+"await new Promise(r=>WizEditor.off({reader:{type:'markdown'}},r));"));
 if(!check(wait([&]{return js(page,"window.WizNativeEditorSetup?.state==='ready'").toBool();}),"actual public Markdown reader initializes"))return 7;
 check(js(page,"jQuery.fn.jquery==="+literal(expected)+"&&jQuery===$").toBool(),"expected real standalone jQuery and alias loaded");
 check(js(page,"typeof $.ajax==='function'&&typeof $.Deferred==='function'&&typeof $.fn.queue==='function'&&$.trim(' x ')==='x'&&$.isFunction(()=>{})").toBool(),"full build and legacy public APIs remain available");
 check(js(page,"(()=>{const d=$.Deferred();let n=0,value='';d.done(v=>{n++;value=v;});d.resolve('中文');d.resolve('wrong');return n===1&&value==='中文';})()").toBool(),"Deferred resolves once and retains Unicode value");
 js(page,"window.modeDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>modeDone=true);");if(!check(wait([&]{return js(page,"modeDone&&document.body.isContentEditable").toBool();}),"editing ready for synthetic paste payload"))return 8;
 check(js(page,R"JS((()=>{const host=document.createElement('div');host.contentEditable='true';document.body.appendChild(host);let seen=0;$(host).on('paste.wizSynthetic',e=>{const data=e.originalEvent.clipboardData;seen+=data.getData('text/plain')==='合成事件 😀'&&data.getData('text/html')==='<b>合成事件 😀</b>';e.preventDefault();});const data=new DataTransfer();data.setData('text/plain','合成事件 😀');data.setData('text/html','<b>合成事件 😀</b>');const event=new ClipboardEvent('paste',{bubbles:true,cancelable:true,clipboardData:data});host.dispatchEvent(event);$(host).off('.wizSynthetic');host.dispatchEvent(new ClipboardEvent('paste',{bubbles:true,cancelable:true,clipboardData:data}));host.remove();return seen===1&&event.defaultPrevented&&!event.isTrusted;})())JS").toBool(),"synthetic originalEvent clipboardData and event unbinding retain behavior");
 const auto atEnd=[&]{return js(page,"(()=>{document.body.focus();const r=document.createRange();r.selectNodeContents(document.body);r.collapse(false);const s=getSelection();s.removeAllRanges();s.addRange(r);return s.rangeCount===1;})()").toBool();};
 check(atEnd(),"selection prepared without system clipboard");
 const QString plain=" 合成纯文本 <tag>& 😀";const QString beforePlain=js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString();
 js(page,"WizEditor.pasteB64('',"+literal(QString::fromLatin1(plain.toUtf8().toBase64()))+");");
 check(wait([&]{return js(page,"document.body.innerText.includes("+literal(plain)+")&&!document.querySelector('tag')").toBool();},4000),"actual public plain-paste API inserts literal Unicode and angle brackets");
 const QString afterPlain=js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString();
 js(page,"WizEditor.undo();");check(wait([&]{return js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==beforePlain;},2000),"public editor undo restores pre-paste source");
 js(page,"WizEditor.redo();");check(wait([&]{return js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString()==afterPlain;},2000),"public editor redo restores exact plain-paste source");
 check(atEnd(),"selection restored for HTML payload");const QString rich="<p><strong>合成HTML粗体</strong> 😀</p>";
 js(page,"WizEditor.pasteB64("+literal(QString::fromLatin1(rich.toUtf8().toBase64()))+","+literal(QString::fromLatin1(QString("合成HTML粗体 😀").toUtf8().toBase64()))+");");
 check(wait([&]{return js(page,"document.body.innerText.includes('合成HTML粗体')&&document.body.innerText.includes('合成纯文本')").toBool();},4000),"actual public HTML-paste API retains both payloads");
 const auto saved=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(true,true)));
 check(saved.state==WizDocumentSaveSnapshot::State::Changed&&saved.mode=="editor"&&saved.html.contains("合成纯文本")&&saved.html.contains("合成HTML粗体")&&saved.html.contains("😀"),"actual current-page snapshot retains paste payloads and Unicode");
 js(page,"window.modeDone=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>modeDone=true);");
 check(wait([&]{return js(page,"modeDone&&!document.body.isContentEditable").toBool();})&&js(page,"document.body.innerText.includes('合成HTML粗体')&&document.body.innerText.includes('合成纯文本')").toBool(),"reading transition retains actual paste result");
 check(js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true})").toString().contains("合成HTML粗体"),"reader original source includes pasted content");
 check(local.blocked.load()==0,"no external request");check(!view.windowHandle()||!view.windowHandle()->isExposed(),"no foreground window exposed");
 const QJsonObject report{{"checks",checks},{"failures",failures},{"jquery",js(page,"jQuery.fn.jquery").toString()},{"externalBlocked",local.blocked.load()},{"systemClipboardReadOrWritten",false},{"syntheticEventAndPublicPastePayloadOnly",true},{"realDatabaseSaveAccepted",false}};QFile result(output+"/runtime.json");if(!result.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 9;result.write(QJsonDocument(report).toJson(QJsonDocument::Indented));profile.setUrlRequestInterceptor(nullptr);return failures?10:0;
}
