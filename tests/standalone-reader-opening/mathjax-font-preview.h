#pragma once
// Local SVG typography comparison only. Not an editor/PDF acceptance test.
static int runMathjaxFontPreview(QTemporaryDir& root,const QString& resources,const QString& output,const QString& font){
 QFile marker(output+"/.wiz-synthetic-font-preview");if(!marker.open(QIODevice::ReadOnly)||marker.readAll()!="synthetic-only\n"||!QStringList{"v2","newcm","tex"}.contains(font))return 4;
 QWebEngineProfile profile;profile.setCachePath(root.filePath("cache"));profile.setPersistentStoragePath(root.filePath("profile"));LocalOnly local;profile.setUrlRequestInterceptor(&local);
 QWebEngineView view;view.setAttribute(Qt::WA_DontShowOnScreen);view.resize(940,500);auto* page=new QWebEnginePage(&profile,&view);view.setPage(page);
 const bool old=font=="v2";const QString name=font=="tex"?"mathjax-tex":"mathjax-newcm",folder=font=="tex"?"tex-font":"newcm";
 const QString fontUrl=QUrl::fromLocalFile(resources+folder).toString();
 const QString config=old?"window.MathJax={skipStartupTypeset:true,showMathMenu:false,messageStyle:'none',TeX:{equationNumbers:{autoNumber:'AMS'}}};":"window.MathJax={startup:{typeset:false},loader:{paths:{'"+name+"':"+literal(fontUrl)+"}},options:{enableMenu:false,enableSpeech:false,enableBraille:false,menuOptions:{settings:{enrich:false,speech:false,braille:false,assistiveMml:false,collapsible:false}}},svg:{fontCache:'local'},output:{font:"+literal(name)+",fontPath:"+literal(fontUrl)+"},tex:{tags:'ams'}};";
 const QString engine=old?resources+"wizeditor/dependency/mathjax/MathJax.js":resources+"mathjax/tex-svg.js";
 const QString source=QStringLiteral("\\[ A+B=C,\\qquad x^2+y^2=z^2,\\qquad \\alpha+\\beta=\\gamma \\]\n"
 "\\[ \\frac{a^2+b^2}{1+x}=\\int_0^1 t^2\\,\\mathrm{d}t,\\qquad \\sum_{n=1}^{\\infty}\\frac{1}{n^2}=\\frac{\\pi^2}{6} \\]\n"
 "\\[ \\mathbb{R}\\quad\\mathcal{F}\\quad\\mathbf{ABC}\\quad\\boldsymbol{\\alpha\\beta}\\quad\\left(\\frac{x+1}{x-1}\\right) \\]\n"
 "\\begin{equation}f(x)=\\begin{cases}x^2,&x\\ge0\\\\-x,&x<0\\end{cases}\\label{preview:one}\\end{equation}");
 const QString title=old?"MathJax 2.7.8 · TeX（现有）":font=="tex"?"MathJax 4.1.3 · TeX（兼容风格候选）":"MathJax 4.1.3 · New Computer Modern（新版默认）";
 QString html="<!doctype html><html><head><meta charset='utf-8'><style>html,body{margin:0;background:#fff;color:#111}body{box-sizing:border-box;padding:20px 40px;width:940px;font:18px Georgia,serif}h2{font:500 20px -apple-system,sans-serif;margin:0 0 24px;color:#333}#math{font-size:22px}mjx-container[display=true],.MathJax_Display{margin:1.2em 0!important}</style><script>"+config+"</script><script src='"+QUrl::fromLocalFile(engine).toString().toHtmlEscaped()+(old?"?config=TeX-AMS-MML_SVG":"")+"' onload=\"(async()=>{try{if(MathJax.startup){await MathJax.startup.promise;await MathJax.typesetPromise([document.getElementById('math')]);}else await new Promise(r=>MathJax.Hub.Queue(['Typeset',MathJax.Hub,document.getElementById('math'),r]));await document.fonts.ready;requestAnimationFrame(()=>requestAnimationFrame(()=>window.previewReady=true));}catch(e){window.previewError=String(e)}})()\"></script></head><body><h2>"+title.toHtmlEscaped()+"</h2><div id='math'>"+source.toHtmlEscaped()+"</div></body></html>";
 QFile fixture(root.filePath("font-preview.html"));if(!fixture.open(QIODevice::WriteOnly))return 5;fixture.write(html.toUtf8());fixture.close();view.show();view.load(QUrl::fromLocalFile(fixture.fileName()));
 if(!wait([&]{return js(page,"!!(window.previewReady||window.previewError)").toBool();},45000)||!js(page,"window.previewReady").toBool()){qInfo()<<"PREVIEW_ERROR"<<js(page,"window.previewError");return 6;}
 const auto info=js(page,"({version:MathJax.version,svg:document.querySelectorAll('svg').length,errors:document.querySelectorAll('merror,.MathJax_Error').length,enriched:document.querySelectorAll('[data-semantic-type]').length,height:document.body.scrollHeight})").toMap();
 if(info.value("height").toInt()>500||info.value("errors").toInt()||local.blocked.load())return 7;
 const auto image=view.grab();if(image.isNull()||!image.save(output+"/preview.png"))return 8;
 QJsonObject result=QJsonObject::fromVariantMap(info);result["font"]=font;result["externalBlocked"]=local.blocked.load();result["nativeWindowExposed"]=bool(view.windowHandle()&&view.windowHandle()->isExposed());result["scope"]="same synthetic TeX,22px SVG,typography only;not editor or PDF acceptance";
 QFile evidence(output+"/runtime.json");if(!evidence.open(QIODevice::WriteOnly|QIODevice::NewOnly))return 9;evidence.write(QJsonDocument(result).toJson(QJsonDocument::Indented));profile.setUrlRequestInterceptor(nullptr);return result["nativeWindowExposed"].toBool()?10:0;
}
