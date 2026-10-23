#pragma once
// Local welcome initialization only. No account, storage bridge, or product changes.
static int runWelcomeBenchmark(QTemporaryDir& root,const QString& resources,const QString& output){
 QFile marker(output+"/.wiz-synthetic-welcome-benchmark");if(!marker.open(QIODevice::ReadOnly)||marker.readAll()!="synthetic-only\n")return 4;
 QFile provider(output+"/provider-input.js"),original(resources+"welcomepage/index.html");if(!provider.open(QIODevice::ReadOnly)||!original.open(QIODevice::ReadOnly))return 5;
 QFile transport(root.filePath("synthetic-provider.js"));if(!transport.open(QIODevice::WriteOnly))return 5;transport.write(provider.readAll());transport.close();
 QString html=QString::fromUtf8(original.readAll());const QString base=QUrl::fromLocalFile(resources+"welcomepage/").toString().toHtmlEscaped();
 if(html.count("../webengine/wizwebchannel.js")!=1||html.contains("<base"))return 5;
 html.replace("src=\"js/","src=\""+base+"js/");html.replace("href=\"css/","href=\""+base+"css/");html.replace("../webengine/wizwebchannel.js",QUrl::fromLocalFile(transport.fileName()).toString());
 QFile business(output+"/business-input.js");if(business.exists()){if(!business.open(QIODevice::ReadOnly))return 5;QFile copy(root.filePath("synthetic-business.js"));if(!copy.open(QIODevice::WriteOnly))return 5;copy.write(business.readAll());copy.close();if(html.count(base+"js/index.js")!=1)return 5;html.replace(base+"js/index.js",QUrl::fromLocalFile(copy.fileName()).toString());}
 QFile fixture(root.filePath("welcome-benchmark.html"));if(!fixture.open(QIODevice::WriteOnly))return 5;fixture.write(html.toUtf8());fixture.close();
 QWebEngineProfile profile;profile.setCachePath(root.filePath("cache"));profile.setPersistentStoragePath(root.filePath("profile"));LocalOnly local;profile.setUrlRequestInterceptor(&local);
 QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(1100,800);auto* page=new QWebEnginePage(&profile,&view);view.setPage(page);
 QWebEngineScript script;script.setName("synthetic welcome timing");script.setInjectionPoint(QWebEngineScript::DocumentCreation);script.setWorldId(QWebEngineScript::MainWorld);
 script.setSourceCode(R"JS((()=>{let finished=false;const observer=new MutationObserver(check);function check(){if(finished||!window.syntheticWelcome||!document.body||getComputedStyle(document.body).visibility!=='visible'||!document.getElementById('listUnread')||listUnread.children.length!==1||!window.jQuery||!$('#datepicker').hasClass('hasDatepicker'))return;finished=true;observer.disconnect();window.welcomeTiming={readyMs:performance.now(),jquery:jQuery.fn.jquery,ui:jQuery.ui.version};requestAnimationFrame(()=>requestAnimationFrame(()=>{welcomeTiming.frameMs=performance.now();welcomeTiming.done=true;}));}observer.observe(document,{subtree:true,childList:true,attributes:true,attributeFilter:['style']});document.addEventListener('DOMContentLoaded',check);})();)JS");page->scripts().insert(script);view.show();
 QJsonArray rounds;int checks=0,failures=0;const auto check=[&](bool ok,const char* text){++checks;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<text;return ok;};
 for(int round=0;round<6;++round){bool loaded=false,loadOk=false;const auto connection=QObject::connect(&view,&QWebEngineView::loadFinished,[&](bool ok){loaded=true;loadOk=ok;});view.load(QUrl::fromLocalFile(fixture.fileName()));
  if(!check(wait([&]{return loaded;})&&loadOk,"local welcome benchmark load")||!check(wait([&]{return js(page,"window.welcomeTiming&&welcomeTiming.done===true").toBool();}),"visible initialized welcome reaches two frames")){QObject::disconnect(connection);return 6;}QObject::disconnect(connection);
  const auto timing=js(page,"welcomeTiming").toMap();if(!check(timing.value("readyMs").toDouble()>0&&timing.value("frameMs").toDouble()>=timing.value("readyMs").toDouble(),"monotonic browser performance clock"))return 7;
  QJsonObject row=QJsonObject::fromVariantMap(timing);row["warmup"]=round==0;row["round"]=round;rounds.append(row);
 }
 check(local.blocked.load()==0,"benchmark makes no external requests");check(!view.windowHandle()||!view.windowHandle()->isExposed(),"benchmark does not expose a foreground window");
 const QJsonObject report{{"checks",checks},{"failures",failures},{"rounds",rounds},{"warmups",1},{"measuredRounds",5},{"externalBlocked",local.blocked.load()},{"syntheticTransportOnly",true},{"fullClientPerformanceMeasured",false},{"memoryMeasured",false}};
 QFile result(output+"/benchmark.json");if(!result.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 8;result.write(QJsonDocument(report).toJson(QJsonDocument::Indented));profile.setUrlRequestInterceptor(nullptr);return failures?9:0;
}
