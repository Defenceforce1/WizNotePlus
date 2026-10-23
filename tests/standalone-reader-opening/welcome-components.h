#pragma once
// Actual local welcome page and business JS with a synthetic transport provider.
static int runWelcomeComponents(QTemporaryDir& root,const QString& resources,const QString& output,const QString& variant){
 QFile marker(output+"/.wiz-synthetic-welcome-components");if(!marker.open(QIODevice::ReadOnly)||marker.readAll()!="synthetic-only\n")return 4;
 const bool jquery4=variant=="jquery4"||variant=="jquery4-en",english=variant=="updated-en"||variant=="jquery4-en",updated=variant=="updated"||english||jquery4,candidate=variant=="candidate"||updated;if(!candidate&&variant!="baseline")return 4;
 int checks=0,failures=0;const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;return ok;};
 QFile provider(output+"/provider-input.js");if(!provider.open(QIODevice::ReadOnly))return 5;QFile providerCopy(root.filePath("synthetic-provider.js"));if(!providerCopy.open(QIODevice::WriteOnly))return 5;providerCopy.write(provider.readAll());providerCopy.close();
 QFile original(resources+"welcomepage/index.html");if(!original.open(QIODevice::ReadOnly))return 5;QString html=QString::fromUtf8(original.readAll());
 // A base URL different from the fixture URL makes jQuery UI treat #tabsRecent
 // as remote and reload a whole page. Absolutize assets, not fragment links.
 const QString base=QUrl::fromLocalFile(resources+"welcomepage/").toString().toHtmlEscaped();
 html.replace("src=\"js/","src=\""+base+"js/");html.replace("href=\"css/","href=\""+base+"css/");
 check(!html.contains("<base")&&html.contains("href=\"#tabsRecent\""),"fixture retains local tab fragment semantics");
 check(html.count("../webengine/wizwebchannel.js")==1,"only transport script reference is replaced in synthetic fixture");html.replace("../webengine/wizwebchannel.js",QUrl::fromLocalFile(providerCopy.fileName()).toString());
 if(!prepareWelcomeJquery4(html,root,output))return 5;
 QFile fixture(root.filePath("welcome.html"));if(!fixture.open(QIODevice::WriteOnly))return 5;fixture.write(html.toUtf8());fixture.close();
 QWebEngineProfile profile;profile.setCachePath(root.filePath("cache"));profile.setPersistentStoragePath(root.filePath("profile"));LocalOnly local;profile.setUrlRequestInterceptor(&local);
 QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(1100,800);auto* page=new QWebEnginePage(&profile,&view);view.setPage(page);
 bool loaded=false,loadOk=false;QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool ok){loaded=true;loadOk=ok;});view.show();view.load(QUrl::fromLocalFile(fixture.fileName()));
 if(!check(wait([&]{return loaded;})&&loadOk,"actual welcome page assets load locally"))return 6;
 if(!check(wait([&]{return js(page,"window.syntheticWelcome&&getComputedStyle(document.body).visibility==='visible'&&document.querySelectorAll('#listUnread li').length===1").toBool();}),"actual welcome initialization completes with synthetic data"))return 7;
 check(js(page,"jQuery.fn.jquery==="+literal(jquery4?"4.0.0":candidate?"3.7.1":"1.11.3")+"&&jQuery.ui.version==="+literal(updated?"1.14.2":candidate?"1.13.3":"1.12.1")).toBool(),"expected welcome-only jQuery and UI versions loaded");
 check(js(page,"document.title==="+literal(english?"Welcome to Wiz":"欢迎使用为知笔记")+"&&document.getElementById('labelRecentDocuments').textContent==="+literal(english?"Recent Documents":"近期文档")).toBool(),"supported UI title and headings retained");
 check(js(page,"$('#tabs').tabs('option','active')===0&&$('#datepicker').hasClass('hasDatepicker')").toBool(),"original tab and inline calendar initialize");
 check(js(page,"[listRecent.children.length,listRecentAccessed.children.length,listUnread.children.length,listTodo.children.length].join(',')==='2,1,1,1'").toBool(),"recent accessed unread and event lists display correct counts");
 check(js(page,"listRecent.textContent.includes('合成近期笔记')&&listUnread.textContent.includes('合成未读笔记')&&listTodo.textContent.includes('合成待办笔记')").toBool(),"existing note list text and Unicode retained");
 check(js(page,"syntheticWelcome.queries.filter(q=>q.method==='recent').every(q=>q.filter===''&&q.limit===10)&&syntheticWelcome.released===5").toBool(),"existing query arguments and document release callbacks retained");
 check(js(page,"Object.values(syntheticWelcome.handlers).reduce((n,a)=>n+a.length,0)===(document.getElementById('todoAdd')?8:7)").toBool(),"all existing document signal connections retained, with optional task creation refresh");
 check(js(page,"$('#datepicker').datepicker('option','dateFormat')==='yy-mm-dd'&&$('#datepicker').datepicker('option','prevText')===''&&$('#datepicker').datepicker('option','nextText')===''&&$('#datepicker').datepicker('option','firstDay')==="+QString::number(english?0:1)+"&&$('#datepicker').datepicker('option','monthNames')[9]==="+literal(english?"October":candidate?"十月":"??")).toBool(),english?"English calendar with original date format and icon-only navigation":candidate?"official Simplified Chinese calendar labels with original date rules and icon-only navigation":"baseline corrupted locale reproduced with original date rules");
 const auto metrics=js(page,"(()=>{const r=document.getElementById('datepicker').getBoundingClientRect();return{calendar:{x:r.x,y:r.y,width:r.width,height:r.height},bodyFont:getComputedStyle(document.body).fontSize,cells:document.querySelectorAll('.ui-datepicker-calendar td').length};})()").toMap();
 check(metrics.value("calendar").toMap().value("width").toDouble()>100&&metrics.value("cells").toInt()>=28,"calendar layout has visible usable dimensions");
 js(page,"window.painted=false;requestAnimationFrame(()=>requestAnimationFrame(()=>painted=true));");check(wait([&]{return js(page,"painted").toBool();}),"welcome frame paints without foreground window");
 check(view.grab().save(output+"/welcome.png"),"capture synthetic welcome page for visual review");
 js(page,"document.querySelector('#listRecent a').click();");check(wait([&]{return js(page,"syntheticWelcome.opened.length===1&&syntheticWelcome.opened[0].guid==='N1'&&syntheticWelcome.opened[0].flag===true").toBool();}),"clicking a note invokes existing view callback");
 const auto month=js(page,"$('.ui-datepicker-title').text()").toString();js(page,"$('.ui-datepicker-next').click();");check(js(page,"$('.ui-datepicker-title').text()").toString()!=month,"calendar next month navigation works");js(page,"$('.ui-datepicker-prev').click();");check(js(page,"$('.ui-datepicker-title').text()").toString()==month,"calendar previous month restores original month");
 js(page,"[...document.querySelectorAll('#datepicker td[data-month=\"9\"] a')].find(a=>a.textContent==='15').click();");check(wait([&]{return js(page,"syntheticWelcome.selected.length===1&&syntheticWelcome.selected[0].join(',')==='D1,D2'").toBool();}),"selecting a calendar day invokes existing document filter");
 check(js(page,"syntheticWelcome.queries.some(q=>q.method==='where'&&q.sql.includes(\"2026-10-15 00:00:00\")&&q.sql.includes(\"2026-10-15 23:59:59\"))").toBool(),"selected date preserves original query boundaries");
 js(page,"syntheticWelcome.recentRevision=1;syntheticWelcome.handlers.documentCreated.forEach(f=>f());");check(wait([&]{return js(page,"listRecent.textContent.includes('合成更新笔记')").toBool();}),"document created notification refreshes list");
 js(page,"syntheticWelcome.emptyUnread=true;window.emptyUnreadDone=false;listUnreadDocuments().then(()=>emptyUnreadDone=true);");
 check(wait([&]{return js(page,"emptyUnreadDone&&listUnread.textContent==="+literal(english?"No documents":"暂无文档")+"&&!listUnread.querySelector('a')").toBool();}),"actual empty unread query uses selected UI language");
 check(view.grab().save(output+"/welcome-empty.png"),"capture localized empty unread state");
 js(page,"syntheticWelcome.emptyUnread=false;window.restoreUnreadDone=false;listUnreadDocuments().then(()=>restoreUnreadDone=true);");
 check(wait([&]{return js(page,"restoreUnreadDone&&listUnread.querySelector('a')&&listUnread.textContent.includes('合成未读笔记')").toBool();}),"populated unread list returns after empty state without changing query behavior");
 checkWelcomeJquery4(page,output,check);
 check(local.blocked.load()==0,"no external requests or real account accessed");check(!view.windowHandle()||!view.windowHandle()->isExposed(),"native welcome test window not exposed");
 const QJsonObject report{{"checks",checks},{"failures",failures},{"variant",variant},{"jquery",js(page,"jQuery.fn.jquery").toString()},{"jqueryUI",js(page,"jQuery.ui.version").toString()},{"calendarMonth",js(page,"$('#datepicker').datepicker('option','monthNames')[9]").toString()},{"metrics",QJsonObject::fromVariantMap(metrics)},{"externalBlocked",local.blocked.load()},{"syntheticTransportOnly",true},{"actualDatabaseOrAccountAccepted",false},{"migrationDiagnostic",QFile::exists(output+"/migration-input.js")},{"migrationWarnings",QJsonArray::fromVariantList(js(page,"jQuery.migrateWarnings||[]").toList())}};QFile result(output+"/runtime.json");if(!result.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 8;result.write(QJsonDocument(report).toJson(QJsonDocument::Indented));profile.setUrlRequestInterceptor(nullptr);return failures?9:0;
}
