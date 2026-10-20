#ifndef WIZ_PDF_PAPER_PAGE_H
#define WIZ_PDF_PAPER_PAGE_H

#include <QWebEngineView>
#include <QWebEngineProfile>
#include <QWebEngineSettings>
#include <QWebEngineUrlRequestInterceptor>
#include <QWebEngineUrlRequestInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QFileInfo>
#include <QPointer>
#include <QTimer>
#include <atomic>
#include <functional>
#include <memory>
#include <cmath>

// An owned, off-screen paper-layout document. The source document is only read;
// neither its viewport, DOM, MathJax objects nor modification state is changed.
class WizPdfPaperPage final : public QObject
{
public:
    using Valid=std::function<bool()>;
    using Ready=std::function<void(WizPdfPaperPage*,QWebEnginePage*)>;
    ~WizPdfPaperPage() override {
        auto ready=std::move(m_ready);
        if(ready)ready(nullptr,nullptr);
    }
    static QString captureScript() {
        return QString::fromLatin1(R"JS((()=>{
          try {
            if(!document.body)return {error:'No document body'};
            const root=document.documentElement.cloneNode(true);
            const scripts=Array.from(document.querySelectorAll('script')).filter(s=>/^math\/tex(?:;|$)/.test(s.type));
            if(scripts.length && (!window.MathJax?.Hub || MathJax.Hub.getAllJax().length!==scripts.length
              ||document.querySelector('.MathJax_Error,merror,.MathJax_Processing')))return {error:'Math is not ready'};
            for(const s of Array.from(root.querySelectorAll('script')))if(!/^math\/tex(?:;|$)/.test(s.type))s.remove();
            // Only remove generated frames which have a retained TeX input.
            for(const s of scripts){
              const frame=root.querySelector('[id="'+CSS.escape(s.id+'-Frame')+'"]');
              if(frame){const display=frame.closest('.MathJax_SVG_Display');(display||frame).remove();}
            }
            root.querySelectorAll('.MathJax_Preview,#MathJax_SVG_Hidden,#MathJax_Message').forEach(e=>e.remove());
            const originals=Array.from(document.querySelectorAll('img,canvas,iframe,object,embed,video,audio'));
            const copies=Array.from(root.querySelectorAll('img,canvas,iframe,object,embed,video,audio'));
            if(originals.length!==copies.length)return {error:'Unsupported generated media structure'};
            for(let i=0;i<originals.length;++i){
              const a=originals[i],b=copies[i],rect=a.getBoundingClientRect();
              if(a.tagName==='IMG'){
                const url=new URL(a.currentSrc||a.src,document.baseURI);
                if(!['file:','data:','qrc:','blob:'].includes(url.protocol))return {error:'Remote images require a local copy'};
                b.src=url.href;b.removeAttribute('srcset');b.removeAttribute('loading');
                b.closest('picture')?.querySelectorAll('source').forEach(s=>s.remove());
              }else if(a.tagName==='CANVAS'){
                const img=document.createElement('img');
                for(const attr of b.attributes)img.setAttribute(attr.name,attr.value);
                img.src=a.toDataURL();img.width=a.width;img.height=a.height;b.replaceWith(img);
              }else{
                if(rect.width>0&&rect.height>0)return {error:'Embedded interactive media needs a static local copy'};
                b.remove();
              }
            }
            root.querySelectorAll('meta[http-equiv]').forEach(e=>{if(e.httpEquiv.toLowerCase()==='refresh')e.remove();});
            root.querySelectorAll('*').forEach(e=>{
              for(const a of Array.from(e.attributes))if(/^on/i.test(a.name))e.removeAttribute(a.name);
              if(e.hasAttribute('contenteditable'))e.setAttribute('contenteditable','false');
            });
            let base=root.querySelector('base');
            if(!base){base=document.createElement('base');root.querySelector('head').prepend(base);}
            base.href=document.baseURI;
            const tex=window.MathJax?.Hub?.config?.TeX||{};
            return {html:'<!doctype html>'+root.outerHTML,sourceUrl:document.location.href,
              mathCount:scripts.length,tex:JSON.parse(JSON.stringify(tex)),
              targets:Array.from(document.querySelectorAll('[id^="mjx-eqn-"],[id^="wiz-math-"]')).map(e=>e.id)};
          }catch(e){return {error:String(e.message||e)};}
        })())JS");
    }

    static WizPdfPaperPage* create(QObject* owner,QWebEnginePage* source,const QVariantMap& captured,
                                  double widthPoints,const QUrl& mathJax,const Valid& valid,const Ready& ready) {
        auto* session=new WizPdfPaperPage(owner,source,valid,ready);
        session->start(captured,widthPoints,mathJax);
        return session;
    }
    void finish() {
        m_finished=true;m_poll.stop();m_deadline.stop();deleteLater();
    }
    bool usable() const {return !m_failed&&!m_finished&&m_source&&m_valid();}
    static QString printPreparationScript() {
        return QString::fromLatin1(R"JS((()=>{
          if(globalThis.__wizPaperBounds)return;
          const state=globalThis.__wizPaperBounds={ok:false,scaled:0};
          addEventListener('beforeprint',event=>{
            if(!event.isTrusted)return;
            state.ok=false;state.scaled=0;
            // The shared SVG helper has already fitted supported multline rows
            // and copied normal-size number badges. Only a still-unbreakable
            // display is locally scaled, never the page or ordinary text.
            for(const frame of document.querySelectorAll('.MathJax_SVG_Display .MathJax_SVG')){
              if(getComputedStyle(frame).display==='none')continue;
              const image=frame.querySelector(':scope > svg'),box=frame.closest('.MathJax_SVG_Display');
              if(!image||!box)continue;
              const rect=image.getBoundingClientRect(),width=box.getBoundingClientRect().width;
              if(!(rect.width>width+1&&rect.height>0&&width>0))continue;
              const tags=Array.from(box.children).filter(e=>e.localName==='svg'&&e.getAttribute('aria-hidden')==='true');
              const tagWidth=Math.max(0,...tags.map(e=>e.getBoundingClientRect().width));
              const available=Math.max(1,width-tagWidth-(tagWidth?1.6*parseFloat(getComputedStyle(box).fontSize):0));
              frame.style.setProperty('min-width','0','important');
              frame.style.setProperty('width',available+'px','important');
              frame.style.setProperty('max-width','100%','important');
              image.style.setProperty('min-width','0','important');
              image.style.setProperty('width',available+'px','important');
              image.style.setProperty('height',rect.height+'px','important');
              image.setAttribute('preserveAspectRatio','xMidYMid meet');
              ++state.scaled;
            }
            const width=document.documentElement.clientWidth;
            state.ok=width>0&&document.documentElement.scrollWidth<=width+1;
          });
        })())JS");
    }
private:
    class LocalOnly final : public QWebEngineUrlRequestInterceptor {
    public:
        std::atomic<bool> blocked{false};
        void interceptRequest(QWebEngineUrlRequestInfo& info) override {
            const auto scheme=info.requestUrl().scheme();
            if(scheme!="file"&&scheme!="data"&&scheme!="qrc"&&scheme!="about"&&scheme!="blob") {
                blocked=true;info.block(true);
            }else if(scheme=="file"){
                const QFileInfo file(info.requestUrl().toLocalFile());
                if(!file.isFile()||!file.isReadable()){blocked=true;info.block(true);}
            }
        }
    };
    QPointer<QWebEnginePage> m_source;
    Valid m_valid;
    Ready m_ready;
    QTemporaryDir m_directory;
    LocalOnly m_requests;
    std::unique_ptr<QWebEngineView> m_view;
    QTimer m_poll,m_deadline;
    bool m_failed=false,m_finished=false,m_prepared=false,m_evaluating=false;

    WizPdfPaperPage(QObject* owner,QWebEnginePage* source,const Valid& valid,const Ready& ready)
        :QObject(owner),m_source(source),m_valid(valid),m_ready(ready) {
        setObjectName(QStringLiteral("wizPdfPaperSession"));
    }
    void fail(const QString& error) {
        if(m_finished||m_failed)return;
        m_failed=true;
        qWarning()<<"Paper PDF preparation failed:"<<error;
        auto ready=std::move(m_ready);
        const QPointer<WizPdfPaperPage> self(this);
        if(ready)ready(this,nullptr);
        if(self)finish();
    }
    void start(const QVariantMap& data,double widthPoints,const QUrl& mathJax) {
        // Queue startup so even synchronous validation failure cannot delete a
        // half-constructed caller or invoke a callback before create returns.
        QTimer::singleShot(0,this,[this,data,widthPoints,mathJax] {
            if(!usable()||!m_directory.isValid()||data.contains("error")||!data.contains("html")) {
                fail(data.value("error",QStringLiteral("Invalid or cancelled paper snapshot")).toString());return;
            }
            const int width=int(std::floor(widthPoints*96.0/72.0))-2;
            if(width<1||width>19200){fail(QStringLiteral("Invalid paper content width"));return;}
            const int mathCount=data.value("mathCount").toInt();
            if(mathCount && (!mathJax.isLocalFile()||!QFileInfo::exists(mathJax.toLocalFile()))) {
                fail(QStringLiteral("Bundled math renderer is unavailable"));return;
            }
            const auto file=m_directory.filePath(QStringLiteral("paper.html"));
            QSaveFile output(file);output.setDirectWriteFallback(false);
            const auto html=data.value("html").toString().toUtf8();
            if(!output.open(QIODevice::WriteOnly)||!output.setPermissions(QFileDevice::ReadOwner|QFileDevice::WriteOwner)
               ||output.write(html)!=html.size()||!output.commit()) {fail(QStringLiteral("Cannot create private paper snapshot"));return;}
            m_view=std::make_unique<QWebEngineView>();
            m_view->setWindowFlags(Qt::Tool|Qt::FramelessWindowHint|Qt::WindowDoesNotAcceptFocus);
            m_view->setAttribute(Qt::WA_DontShowOnScreen);
            m_view->setAttribute(Qt::WA_ShowWithoutActivating);
            m_view->setPage(new QWebEnginePage(m_source->profile(),m_view.get()));
            m_view->page()->setProperty("wizPdfPaperBoundsRequired",true);
            m_view->page()->setUrlRequestInterceptor(&m_requests);
            m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls,false);
            m_view->resize(width,900);m_view->show();
            const QPointer<WizPdfPaperPage> self(this);
            QObject::connect(m_view->page(),&QWebEnginePage::loadFinished,this,[self,data,mathJax](bool ok) {
                if(!self)return;
                if(!ok||!self->usable()){self->fail(QStringLiteral("Paper page load failed or cancelled"));return;}
                QJsonObject options=QJsonObject::fromVariantMap(data);
                options.remove("html");options.insert("mathJax",mathJax.toString()+"?config=TeX-AMS-MML_SVG");
                const auto json=QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact));
                self->m_view->page()->runJavaScript(bootstrap()+"("+json+")");
            });
            QObject::connect(&m_deadline,&QTimer::timeout,this,[this]{fail(QStringLiteral("Paper export timed out"));});
            m_deadline.setSingleShot(true);m_deadline.start(45000);
            QObject::connect(&m_poll,&QTimer::timeout,this,[this] {
                if(!usable()||m_requests.blocked){fail(QStringLiteral("Paper export cancelled or a resource is unavailable locally"));return;}
                if(m_prepared||m_evaluating)return;
                m_evaluating=true;const QPointer<WizPdfPaperPage> self(this);
                m_view->page()->runJavaScript("globalThis.__wizPaperResult",[self](const QVariant& value) {
                    if(!self)return;self->m_evaluating=false;
                    const auto result=value.toMap();
                    if(result.contains("error")){self->fail(result.value("error").toString());return;}
                    if(!result.value("ready").toBool())return;
                    if(!self->usable()) {self->fail(QStringLiteral("Paper document changed"));return;}
                    self->m_prepared=true;
                    self->m_view->page()->setProperty("wizPdfPaperGeometry",result);
                    auto ready=std::move(self->m_ready);
                    if(ready)ready(self,self->m_view->page());
                });
            });
            m_poll.start(50);m_view->load(QUrl::fromLocalFile(file));
        });
    }
    static QString bootstrap() {
        return QString::fromLatin1(R"JS((function(o){
          const state=globalThis.__wizPaperResult={};
          const finish=()=>document.fonts.ready.then(()=>Promise.all(Array.from(document.images).map(img=>
            img.complete?Promise.resolve():new Promise((resolve,reject)=>{img.onload=resolve;img.onerror=()=>reject(Error('Image load failed'));}))))
            .then(()=>new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve))))
            .then(()=>{
              if(Array.from(document.images).some(i=>!i.naturalWidth))throw Error('Missing paper image');
              if(Array.from(document.querySelectorAll('link[rel~="stylesheet"]')).some(s=>!s.disabled&&!s.sheet))throw Error('Missing paper stylesheet');
              if(Array.from(document.fonts).some(f=>f.status==='error'))throw Error('Missing paper font');
              for(const table of document.querySelectorAll('table')){
                const available=table.parentElement?.clientWidth||0;
                if(available>0&&table.getBoundingClientRect().width>available+1){
                  table.style.width='100%';table.style.tableLayout='fixed';
                }
              }
              if(document.querySelector('.MathJax_Error,merror'))throw Error('Math typesetting failed');
              if(o.mathCount && MathJax.Hub.getAllJax().length!==o.mathCount)throw Error('Math object count changed');
              // Assistive MathML repeats visual tag IDs. Chromium otherwise
              // records the hidden MathML box instead of the printable SVG
              // destination. Keep its semantics, removing only duplicate IDs
              // in this private output document, never in the original note.
              const visualIds=new Set(Array.from(document.querySelectorAll('svg [id]')).map(e=>e.id));
              document.querySelectorAll('.MJX_Assistive_MathML [id]').forEach(e=>{
                if(visualIds.has(e.id))e.removeAttribute('id');
              });
              if(o.targets.some(id=>!document.getElementById(id)))throw Error('A math destination changed');
              const strip=u=>u.replace(/#.*$/,'');
              for(const a of document.querySelectorAll('a'))for(const attr of Array.from(a.attributes)){
                if(attr.localName!=='href')continue;
                const raw=attr.value;
                try{const url=new URL(raw,document.baseURI);
                  if(raw.startsWith('#')||strip(url.href)===strip(o.sourceUrl)||strip(url.href)===strip(location.href)){
                    const href=url.hash;
                    if(attr.namespaceURI)a.setAttributeNS(attr.namespaceURI,attr.name,href);else a.setAttribute(attr.name,href);
                  }
                }catch(_){}
              }
              const width=document.documentElement.clientWidth;
              if(!(width>0))throw Error('Paper viewport is unavailable');
              Object.assign(state,{ready:true,width,scrollWidth:document.documentElement.scrollWidth,math:o.mathCount});
            }).catch(e=>{state.error=String(e.message||e);});
          const style=document.createElement('style');
          style.textContent='html,body{min-width:0!important;width:auto!important;max-width:100%!important;} '
            +'body{overflow-wrap:break-word;} img,canvas{max-width:100%!important;height:auto!important;} '
            +'pre{white-space:pre-wrap!important;overflow-wrap:anywhere;} table{max-width:100%;} td,th{overflow-wrap:anywhere;}';
          document.head.appendChild(style);
          if(!o.mathCount){finish();return;}
          window.MathJax={skipStartupTypeset:true,messageStyle:'none',
            SVG:{linebreaks:{automatic:true,width:'container'}},TeX:o.tex};
          const script=document.createElement('script');script.src=o.mathJax;
          script.onerror=()=>{state.error='Bundled math renderer failed to load';};
          script.onload=()=>{
            if(!window.MathJax?.Hub){state.error='Bundled math renderer did not initialize';return;}
            // The reader already identified math. Process retained TeX scripts
            // only: a second tex2jax preprocessing pass could turn escaped
            // dollar text or legacy code fences into unintended formulas.
            MathJax.Hub.Queue(['Process',MathJax.Hub,document.body],finish);
          };
          document.head.appendChild(script);
        }))JS");
    }
};
#endif
