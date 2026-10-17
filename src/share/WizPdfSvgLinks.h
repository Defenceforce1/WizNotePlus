#ifndef WIZ_PDF_SVG_LINKS_H
#define WIZ_PDF_SVG_LINKS_H

#include <QWebEnginePage>
#include <QWebEngineScript>
#include <QWebEngineScriptCollection>

// No bridge, editor globals or stored-note rewriting. DOM augmentation exists
// only during an explicitly armed native print, in an isolated application
// world. Callers MUST serialize snapshots/printing via WizPdfPageOperations.
namespace WizPdfSvgLinks {
inline QString source()
{
    return QString::fromLatin1(R"JS((()=>{
      'use strict';
      if(globalThis.__wizPdfSvgLinks)return;
      const svg='http://www.w3.org/2000/svg',xlink='http://www.w3.org/1999/xlink';
      let records=[],active=false,armed=false,prepares=0,restores=0,lastAdded=0,errors=0;
      function restore(){
        if(!active)return;
        for(const record of records){
          try{if(record.node.getAttributeNS(null,'href')===record.value)record.node.removeAttributeNS(null,'href');}
          catch(_){++errors;}
        }
        records=[];active=false;++restores;
      }
      function prepare(event){
        // Editable rich-text observers may treat even restored attributes as
        // edits. Do not augment that surface or change its existing export.
        if(!event.isTrusted||!armed||active||document.body?.isContentEditable)return;
        active=true;++prepares;lastAdded=0;
        try{
          for(const node of document.querySelectorAll('.MathJax_SVG a')){
            if(node.namespaceURI!==svg||node.hasAttributeNS(null,'href'))continue;
            const value=node.getAttributeNS(xlink,'href');
            if(!value||value[0]!=='#')continue;
            let id;try{id=decodeURIComponent(value.slice(1));}catch(_){continue;}
            if(!id.startsWith('mjx-eqn-'))continue;
            const target=document.getElementById(id);
            if(!target||target.namespaceURI!==svg||!target.closest('.MathJax_SVG'))continue;
            records.push({node,value});
            node.setAttributeNS(null,'href',value);++lastAdded;
          }
        }catch(_){++errors;restore();}
      }
      addEventListener('beforeprint',prepare,true);
      addEventListener('afterprint',event=>{if(event.isTrusted)restore();},true);
      addEventListener('pagehide',event=>{if(event.isTrusted)restore();},true);
      Object.defineProperty(globalThis,'__wizPdfSvgLinks',{value:Object.freeze({
        arm:()=>{if(active||armed)return false;armed=true;return true;},
        finish:()=>{restore();armed=false;return !active&&!records.length&&!errors;},
        status:()=>({active,pending:records.length,prepares,restores,lastAdded,errors})
      })});
    })())JS");
}
inline void install(QWebEnginePage* page)
{
    if (!page || !page->scripts().find(QStringLiteral("wiz-pdf-svg-links")).isEmpty()) return;
    QWebEngineScript script;
    script.setName(QStringLiteral("wiz-pdf-svg-links"));
    script.setWorldId(QWebEngineScript::ApplicationWorld);
    script.setInjectionPoint(QWebEngineScript::DocumentReady);
    script.setRunsOnSubFrames(false);
    script.setSourceCode(source());
    page->scripts().insert(script);
}
}
#endif
