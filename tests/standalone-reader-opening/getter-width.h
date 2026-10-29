// Independent synthetic regression for the authorized official getter copy.
#ifdef Q_OS_MAC
class GetterCases261 final : public QObject {
    QTemporaryDir& root;QString resources,output;std::unique_ptr<QWebEngineProfile> profile;LocalOnly local;
    std::unique_ptr<QWebEngineView> view;QPointer<QWebEnginePage> page;QWebChannel channel;Bridge editor,explorer;
    QJsonArray cases,results;int index=0,checks=0,failures=0;bool stopped=false;std::function<void(int)> done;
    void check(bool ok,const char* what){checks++;failures+=!ok;qInfo().noquote()<<(ok?"PASS":"FAIL")<<what;}
    void read(const QString& code,std::function<void(const QVariant&)> fn){page->runJavaScript(code,[this,fn=std::move(fn)](const QVariant& v){if(!stopped)fn(v);});}
    void later(std::function<void()> fn){QTimer::singleShot(0,this,[this,fn]{if(!stopped)fn();});}
    void poll(const QString& code,QDeadlineTimer limit,std::function<void()> fn){read(code,[this,code,limit,fn](const QVariant& v){if(v.toBool()){later(fn);return;}if(limit.hasExpired()){check(false,"getter cases deadline");finish();return;}QTimer::singleShot(25,this,[this,code,limit,fn]{poll(code,limit,fn);});});}
    void finish(){if(stopped)return;stopped=true;check(local.blocked.load()==0,"getter cases local-only requests");
        bool metal=true;const auto widgets=view->findChildren<QQuickWidget*>();for(auto* w:widgets)metal&=w->quickWindow()->rendererInterface()->graphicsApi()==QSGRendererInterface::Metal;
        check(metal&&!widgets.isEmpty()&&(!view->windowHandle()||!view->windowHandle()->isExposed()),"hidden Cocoa actual Metal");
        disconnect(view.get(),nullptr,this,nullptr);view.reset();QTimer::singleShot(500,this,[this]{check(page.isNull(),"case page destroyed before profile");profile->setUrlRequestInterceptor(nullptr);profile.reset();QFile f(output);check(f.open(QIODevice::WriteOnly|QIODevice::NewOnly),"unique synthetic case report");f.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"cases",results},{"qtVersion",qVersion()},{"externalRequests",local.blocked.load()}}).toJson(QJsonDocument::Indented));f.close();done(failures?15:0);});}
    void next(){if(index>=cases.size()){finish();return;}const auto c=cases[index].toObject();const auto source=c.value("source").toString();editor.original="<!doctype html><html><head><meta charset='utf-8'></head><body>"+source.toHtmlEscaped().replace("\n","<br>")+"</body></html>";QString html=editor.original,tags;
        for(const auto& name:QStringList{"webengine/wizwebchannel.js","wizeditor/wizEditorForMac.js","wizeditor/dependency/marked/marked.umd.js","wizeditor/latexTheorems.js","wizeditor/markdownEnvironments.js","wizeditor/editorHelper.js"})tags+="<script src='"+QUrl::fromLocalFile(resources+name).toString().toHtmlEscaped()+"' name='wiz_inner_script' wiz_style='unsave'></script>";
        html.insert(html.indexOf("</head>"),tags);QFile f(root.filePath(QString("getter-case-%1.html").arg(index)));if(!f.open(QIODevice::WriteOnly)){check(false,"synthetic fixture creation");finish();return;}f.write(html.toUtf8());f.close();view->load(QUrl::fromLocalFile(f.fileName()));
    }
    QString stateScript()const{if(cases[index].toObject().value("probeMathML").toBool())return mathProbeScript();return R"JS((()=>{
const modified=WizEditor.isModified(),before=document.body.innerHTML;
const normal=WizEditor.getMarkdownSrc({unEscapeHtml:true}),repeat=WizEditor.getMarkdownSrc({unEscapeHtml:true}),escaped=WizEditor.getMarkdownSrc({escapeHtml:true}),defaults=WizEditor.getMarkdownSrc();
const after=document.body.innerHTML;let first=0;while(first<before.length&&first<after.length&&before[first]===after[first])first++;const domDelta=before===after?null:{before:before.slice(Math.max(0,first-50),first+150),after:after.slice(Math.max(0,first-50),first+150)};
return {normal,escaped,defaults,domDelta,repeatEqual:repeat===normal,bodyUnchanged:document.body.innerHTML===before,modifiedUnchanged:WizEditor.isModified()===modified,editable:document.body.isContentEditable};
})())JS";}
    QString mathProbeScript()const{return R"JS((()=>{
const records=[],modifiedBefore=WizEditor.isModified(),bodyBefore=document.body.innerHTML;
const variants=[{name:'absent',math:false},{name:'visible',math:true},{name:'hidden',math:true,css:'math{display:none!important}'},{name:'visible-again',math:true},{name:'absent-again',math:false}];
for(const test of variants){
const old=document.getElementById('synthetic-math262');if(old)old.remove();const oldStyle=document.getElementById('synthetic-math-style262');if(oldStyle)oldStyle.remove();
if(test.css){const style=document.createElement('style');style.id='synthetic-math-style262';style.textContent=test.css;document.head.appendChild(style);}
if(test.math){const box=document.createElement('span');box.id='synthetic-math262';const math=document.createElementNS('http://www.w3.org/1998/Math/MathML','math');math.innerHTML='<mi>x</mi><mo>+</mo><mi>y</mi>';box.appendChild(math);document.body.appendChild(box);}
const before=document.body.innerHTML,mathBefore=document.getElementsByTagName('math').length,normal=WizEditor.getMarkdownSrc({unEscapeHtml:true}),repeat=WizEditor.getMarkdownSrc({unEscapeHtml:true});
records.push({name:test.name,mathBefore,mathAfter:document.getElementsByTagName('math').length,normal,repeat,bodyAfter:document.body.innerHTML,bodyUnchanged:before===document.body.innerHTML});
}
document.getElementById('synthetic-math262')?.remove();document.getElementById('synthetic-math-style262')?.remove();
return {records,normal:WizEditor.getMarkdownSrc({unEscapeHtml:true}),repeatEqual:records.every(x=>x.normal===x.repeat),bodyUnchanged:document.body.innerHTML===bodyBefore,modifiedUnchanged:WizEditor.isModified()===modifiedBefore,editable:document.body.isContentEditable};
})())JS";}
    void loaded(bool ok){if(!ok){check(false,"getter case load");finish();return;}QString setup="window.WizSpellCheckEnabled=false;window.WizNoteCss='body{font-size:16px;line-height:1.7;}';"+WizDocumentPageReady::initializeEditorScript(resources+"wizeditor/","zh-cn","synthetic-getter-cases","Synthetic",false,"markdown")+"WizPrepareNoteModeChange();await new Promise(resolve=>WizEditor.off(null,resolve));WizApplyNoteStyle();";
        page->runJavaScript(WizDocumentPageReady::setupScript(literal(QString("case-%1").arg(index)),setup));poll("window.WizNativeEditorSetup?.state==='ready'&&!document.body.isContentEditable",QDeadlineTimer(45000),[this]{reading();});
    }
    void reading(){read(stateScript(),[this](const QVariant& v){QJsonObject result{{"name",cases[index].toObject().value("name")},{"reading",QJsonObject::fromVariantMap(v.toMap())}};const auto r=result.value("reading").toObject();check(r.value("repeatEqual").toBool()&&r.value("bodyUnchanged").toBool()&&r.value("modifiedUnchanged").toBool()&&!r.value("editable").toBool(),"reading getter stable without modification-state changes; DOM delta compared with upstream");
        page->runJavaScript("window.wizModeDone261=false;WizPrepareNoteModeChange();WizEditor.on(null,()=>window.wizModeDone261=true);");poll("window.wizModeDone261&&document.body.isContentEditable",QDeadlineTimer(30000),[this,result]{editing(result);});});}
    void editing(QJsonObject result){read(stateScript(),[this,result](const QVariant& v)mutable{const auto r=QJsonObject::fromVariantMap(v.toMap());result.insert("editing",r);check(r.value("repeatEqual").toBool()&&r.value("bodyUnchanged").toBool()&&r.value("modifiedUnchanged").toBool()&&r.value("editable").toBool(),"editing getter stable without changing live DOM");
        read("document.body.insertAdjacentHTML('beforeend','<div>UNSAVED261 中文 &amp; &lt; &gt; \\u03b1</div>');true",[this,result](const QVariant&)mutable{read(stateScript(),[this,result](const QVariant& v)mutable{const auto r=QJsonObject::fromVariantMap(v.toMap());result.insert("unsavedEditing",r);check(r.value("normal").toString().contains("UNSAVED261")&&r.value("repeatEqual").toBool()&&r.value("bodyUnchanged").toBool()&&r.value("modifiedUnchanged").toBool(),"fresh getter includes current unsaved edit, repeated reads unchanged");
            page->runJavaScript("window.wizModeDone261=false;WizPrepareNoteModeChange();WizEditor.off(null,()=>window.wizModeDone261=true);");poll("window.wizModeDone261&&!document.body.isContentEditable",QDeadlineTimer(45000),[this,result]{read(stateScript(),[this,result](const QVariant& v)mutable{const auto r=QJsonObject::fromVariantMap(v.toMap());result.insert("readingAfterEdit",r);check(r.value("normal").toString().contains("UNSAVED261")&&r.value("repeatEqual").toBool()&&r.value("bodyUnchanged").toBool()&&r.value("modifiedUnchanged").toBool(),"reader after edit retains current source and modification state; DOM delta compared");results.append(result);++index;later([this]{next();});});});
        });});});}
public:
    GetterCases261(QTemporaryDir& root,const QString& resources,const QString& input,const QString& output,std::function<void(int)> done):root(root),resources(resources),output(output),done(std::move(done)){
        QFile f(input);if(!f.open(QIODevice::ReadOnly))qFatal("case file");cases=QJsonDocument::fromJson(f.readAll()).array();if(cases.size()<1||cases.size()>16)qFatal("bounded cases");profile=std::make_unique<QWebEngineProfile>();profile->setCachePath(root.filePath("cache"));profile->setPersistentStoragePath(root.filePath("profile"));profile->setUrlRequestInterceptor(&local);channel.registerObject("WizQtEditor",&editor);channel.registerObject("WizExplorerApp",&explorer);
    }
    void start(){view=std::make_unique<QWebEngineView>();view->setAttribute(Qt::WA_DontShowOnScreen);view->resize(900,600);page=new QWebEnginePage(profile.get(),view.get());view->setPage(page);page->setWebChannel(&channel);connect(view.get(),&QWebEngineView::loadFinished,this,[this](bool ok){later([this,ok]{loaded(ok);});});view->show();next();}
};
static int runGetterCases261(QTemporaryDir& root,const QString& resources,const QString& input,const QString& output){if(QGuiApplication::platformName()!="cocoa")return 4;int status=0;GetterCases261 p(root,resources,input,output,[&](int s){status=s;QCoreApplication::quit();});QTimer::singleShot(0,&p,[&]{p.start();});QCoreApplication::exec();return status;}
#endif
