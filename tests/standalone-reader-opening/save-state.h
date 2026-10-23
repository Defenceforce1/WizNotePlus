#pragma once
// Synthetic template-like rich text; unchanged editor through public APIs only.
static int runSaveState(QTemporaryDir& root,const QString& resources,const QString& output){
 QFile marker(output+"/.wiz-synthetic-save-state");if(!marker.open(QIODevice::ReadOnly)||marker.readAll()!="synthetic-only\n")return 4;
 QJsonArray cases;int failures=0,checks=0;const auto check=[&](bool ok,const char* label){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<label;};
 for(const QString kind:QStringList{"plain","legacy-class","nested-root","markdown"}){
  QWebEngineProfile profile;profile.setCachePath(root.filePath(kind+"-cache"));profile.setPersistentStoragePath(root.filePath(kind+"-profile"));LocalOnly local;profile.setUrlRequestInterceptor(&local);
  QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(1000,700);auto* page=new QWebEnginePage(&profile,&view);view.setPage(page);QWebChannel channel;Bridge editor,explorer;channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);page->setWebChannel(&channel);
  const QString attrs=kind=="legacy-class"?" class='wiz-readonly'":kind=="nested-root"?" contenteditable='false'":"";
  const QString content=kind=="markdown"?QStringLiteral("# 合成Markdown<br><br>普通内容 中文 😀<br>"):QStringLiteral("<h1>合成工作周报</h1><table><tbody><tr><th>本周工作</th><td id='entry'>合成会议记录 中文 😀</td></tr></tbody></table>");
  editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body"+attrs+">"+(kind=="nested-root"?"<div contenteditable='true'>"+content+"</div>":content)+"</body></html>";
  QString html=editor.original,scripts;for(const auto& name:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})scripts+="<script name='wiz_inner_script' wiz_style='unsave' src='"+QUrl::fromLocalFile(resources+name).toString().toHtmlEscaped()+"'></script>";
  html.insert(html.lastIndexOf("</body>"),scripts);QFile fixture(root.filePath(kind+".html"));if(!fixture.open(QIODevice::WriteOnly))return 5;fixture.write(html.toUtf8());fixture.close();bool loaded=false,ok=false;QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool v){loaded=true;ok=v;});view.show();view.load(QUrl::fromLocalFile(fixture.fileName()));if(!wait([&]{return loaded;})||!ok)return 6;
  const QString type=kind=="markdown"?"markdown":"common";
  page->runJavaScript(WizDocumentPageReady::setupScript("'save-state'","window.WizSpellCheckEnabled=false;window.WizNoteCss='';"+WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-save","Synthetic",false,type)+"await new Promise(r=>WizEditor.off({reader:{type:"+literal(type)+"}},r));"));
  if(!wait([&]{return js(page,"window.WizNativeEditorSetup?.state==='ready'").toBool();}))return 7;
  if(kind=="markdown"){
   const auto initial=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(false,false)));
   check(initial.state!=WizDocumentSaveSnapshot::State::Failed&&initial.mode=="reader","unchanged Markdown current-page reader capture remains valid");
   js(page,"window.modeDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>modeDone=true);");check(wait([&]{return js(page,"modeDone&&document.body.isContentEditable").toBool();}),"Markdown public editor transition completes");
   js(page,"document.body.appendChild(document.createTextNode(' 追加合成编辑')); ");
   const auto edited=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(true,true)));
   check(edited.state==WizDocumentSaveSnapshot::State::Changed&&edited.mode=="editor"&&edited.html.contains("追加合成编辑")&&js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true}).includes('# 合成Markdown')").toBool(),"Markdown current-page editor capture retains source and edit");
   js(page,"window.modeDone=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},()=>modeDone=true);");check(wait([&]{return js(page,"modeDone&&!document.body.isContentEditable").toBool();}),"Markdown reading return completes without formula suite rerun");
   const auto reading=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(false,true)));
   check(reading.state==WizDocumentSaveSnapshot::State::Changed&&reading.mode=="reader"&&reading.html.contains("追加合成编辑")&&js(page,"WizEditor.getMarkdownSrc({unEscapeHtml:true}).includes('# 合成Markdown')").toBool(),"Markdown post-transition snapshot retains source instead of rendered heading");
   check(WizDocumentSaveSnapshot::writeHtmlFile(root.filePath("markdown-saved.html"),reading.html),"Markdown associated source uses existing atomic writer");
   cases.push_back(QJsonObject{{"kind",kind},{"editorCaptureMode",edited.mode},{"afterSwitchMode",reading.mode},{"externalBlocked",local.blocked.load()}});if(local.blocked.load()||(view.windowHandle()&&view.windowHandle()->isExposed()))failures++;profile.setUrlRequestInterceptor(nullptr);continue;
  }
  const QString readerText=js(page,"document.body.innerText").toString(),readerDom=js(page,"document.documentElement.outerHTML").toString();
  const auto wrongEditor=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::editorScript(true,true)));
  check(wrongEditor.state==WizDocumentSaveSnapshot::State::Failed&&wrongEditor.reason=="mode-mismatch","old editor request rejects actual reader body before capture");
  const auto legacyReader=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::readerScript(false)));
  const bool legacyReaderDomChanged=js(page,"document.documentElement.outerHTML").toString()!=readerDom;
  check(legacyReader.state!=WizDocumentSaveSnapshot::State::Failed&&js(page,"document.body.innerText").toString()==readerText,"original reader close API preserves text; incidental DOM normalization recorded separately");
  const auto actualReader=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(true,false)));
  check(actualReader.state==WizDocumentSaveSnapshot::State::Unchanged&&actualReader.mode=="reader"&&js(page,"document.body.innerText").toString()==readerText&&js(page,"document.querySelectorAll('table').length===1").toBool(),"current-page capture acknowledges unchanged reader and preserves text and table");
  js(page,"window.modeDone=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>modeDone=true);");if(!wait([&]{return js(page,"modeDone").toBool();}))return 8;
  if(kind=="nested-root"){
   // Recreate reported public mode metadata without reading the real template:
   // template editor active, body false, ordinary local editing region active.
   js(page,"document.body.classList.add('wiz-template-client-pc','wiz-template-client-mac');document.body.contentEditable='false';document.getElementById('entry').contentEditable='true';");
   check(js(page,"!document.body.isContentEditable&&document.body.classList.contains('wiz-editing')&&!document.body.classList.contains('wiz-readonly')&&document.getElementById('entry').isContentEditable").toBool(),"template metadata reproduces old body-only guard rejection while local text remains editable");
  }
  const auto state=QJsonObject::fromVariantMap(js(page,"({bodyEditable:document.body.isContentEditable,readonlyClass:document.body.classList.contains('wiz-readonly'),designMode:document.designMode,bodyClass:document.body.className,roots:[...document.querySelectorAll('.wiz-editor-body')].map(x=>({tag:x.tagName,editable:x.isContentEditable,readonly:x.classList.contains('wiz-readonly')}))})").toMap());
  const bool inserted=js(page,"(()=>{const p=document.getElementById('entry');const r=document.createRange();r.selectNodeContents(p);r.collapse(false);const s=getSelection();s.removeAllRanges();s.addRange(r);return document.execCommand('insertText',false,' 保存测试');})()").toBool();
  const auto captured=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::editorScript(true,true)));
  check(inserted&&captured.state==WizDocumentSaveSnapshot::State::Changed&&captured.html.contains("保存测试"),"template-like ordinary editing and original strict snapshot remain valid");
  js(page,"document.getElementById('entry').appendChild(document.createTextNode(' 新的未保存内容'));window.WizSaveProbeBaseline=WizEditor.isModified();");
  const auto wrongReader=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::readerScript(true)));
  check(wrongReader.state==WizDocumentSaveSnapshot::State::Failed&&wrongReader.reason=="mode-mismatch"&&js(page,"WizEditor.isModified()===WizSaveProbeBaseline").toBool(),"old native reader request reproduces mismatch on edited body without clearing dirty baseline");
  const auto denied=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(true,true,false)));
  check(denied.state==WizDocumentSaveSnapshot::State::Failed&&denied.reason=="edit-permission-denied"&&js(page,"WizEditor.isModified()===WizSaveProbeBaseline").toBool(),"no edit permission refuses capture before changing source baseline");
  const auto actualEditor=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(false,true)));
  check(actualEditor.state==WizDocumentSaveSnapshot::State::Changed&&actualEditor.mode=="editor"&&actualEditor.html.contains("新的未保存内容")&&actualEditor.html.contains("<table")&&actualEditor.html.contains("😀"),"current-page capture retains new table text Unicode and actual editor mode");
  const QString savedPath=root.filePath(kind+"-saved.html");check(WizDocumentSaveSnapshot::writeHtmlFile(savedPath,actualEditor.html),"synthetic atomic writer commits exact captured source");QFile saved(savedPath);check(saved.open(QIODevice::ReadOnly)&&saved.readAll()==QByteArray::fromHex("efbbbf")+actualEditor.html.toUtf8(),"synthetic file readback preserves UTF8 BOM and all source bytes");
  js(page,"document.body.classList.add('wiz-readonly');");
  const auto conflict=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(true,true)));
  check(conflict.state==WizDocumentSaveSnapshot::State::Failed&&conflict.reason=="mode-mismatch","contradictory editable plus readonly DOM is still rejected");js(page,"document.body.classList.remove('wiz-readonly');WizNativeEditorSetup.state='pending';");
  check(WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(true,true))).reason=="setup-not-ready","pending initialization is not mistaken for saved content");js(page,"WizNativeEditorSetup.state='ready';");
  js(page,"document.getElementById('entry').appendChild(document.createTextNode(' 切换前最后修改'));window.modeDone=false;WizPrepareNoteModeChange();WizEditor.off({reader:{type:'common'}},()=>modeDone=true);");
  check(wait([&]{return js(page,"modeDone&&!document.body.isContentEditable").toBool();}),"edited rich-text public off completes back to reading");
  const auto afterSwitch=WizDocumentSaveSnapshot::parse(js(page,WizDocumentSaveSnapshot::currentPageScript(false,true)));
  check(afterSwitch.state==WizDocumentSaveSnapshot::State::Changed&&afterSwitch.mode=="reader"&&afterSwitch.html.contains("切换前最后修改")&&afterSwitch.html.contains("新的未保存内容")&&afterSwitch.html.contains("<table"),"reading save after edit transition keeps both edits and table source");
  const QString finalPath=root.filePath(kind+"-after-switch.html");check(WizDocumentSaveSnapshot::writeHtmlFile(finalPath,afterSwitch.html),"post-transition source commits through original atomic writer");
  QJsonObject row{{"kind",kind},{"state",state},{"insertion",inserted},{"snapshotReason",captured.reason},{"changed",captured.state==WizDocumentSaveSnapshot::State::Changed},{"textPreserved",captured.html.contains("保存测试")},{"legacyReaderDomNormalized",legacyReaderDomChanged},{"editorCaptureMode",actualEditor.mode},{"afterSwitchMode",afterSwitch.mode},{"externalBlocked",local.blocked.load()}};cases.push_back(row);qInfo().noquote()<<QJsonDocument(row).toJson(QJsonDocument::Compact);if(local.blocked.load()||(view.windowHandle()&&view.windowHandle()->isExposed()))failures++;
  profile.setUrlRequestInterceptor(nullptr);
 }
 check(WizDocumentSaveSnapshot::parse(QVariantMap{{"ok",true},{"changed",false},{"mode","unknown"}}).state==WizDocumentSaveSnapshot::State::Failed,"unknown mode payload is not accepted as save success");
 QFile result(output+"/runtime.json");if(!result.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 9;result.write(QJsonDocument(QJsonObject{{"cases",cases},{"checks",checks},{"failures",failures},{"syntheticOnly",true}}).toJson(QJsonDocument::Indented));return failures?10:0;
}
