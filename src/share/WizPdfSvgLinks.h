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
      let links=[],targets=[],boxes=[],layouts=[],nativeFits=[],active=false,armed=false,prepares=0,restores=0,lastAdded=0,lastFitted=0,errors=0;
      function restore(){
        if(!active)return;
        for(const record of layouts){
          try{
            record.layout.remove();
            if(record.hadStyle)record.node.setAttribute('style',record.style);
            else{record.node.getAttribute('style');record.node.removeAttribute('style');}
            if(record.aspect!==undefined){
              if(record.aspect===null)record.node.removeAttribute('preserveAspectRatio');
              else record.node.setAttribute('preserveAspectRatio',record.aspect);
            }
          }catch(_){++errors;}
        }
        for(const record of targets){
          try{
            if(record.proxy.getAttribute('id')===record.id&&record.node.getAttribute('id')===record.temporary){
              record.badge.remove();record.proxy.remove();
              if(record.nodeHadVisibility)record.node.setAttribute('visibility',record.nodeVisibility);else record.node.removeAttribute('visibility');
              record.node.setAttribute('id',record.id);
            }else ++errors;
          }catch(_){++errors;}
        }
        for(const record of boxes){
          try{
            if(record.hadStyle)record.node.setAttribute('style',record.style);
            else{record.node.getAttribute('style');record.node.removeAttribute('style');}
          }
          catch(_){++errors;}
        }
        for(const record of links){
          try{if(record.node.getAttributeNS(null,'href')===record.applied)record.node.removeAttributeNS(null,'href');}
          catch(_){++errors;}
        }
        for(const record of nativeFits){
          try{
            for(const [name,value] of record.attributes){
              if(value===null)record.image.removeAttribute(name);else record.image.setAttribute(name,value);
            }
            if(record.rootStyle===null)record.root.removeAttribute('style');else record.root.setAttribute('style',record.rootStyle);
          }catch(_){++errors;}
        }
        links=[];targets=[];boxes=[];layouts=[];nativeFits=[];active=false;++restores;
      }
      function fitMultlineRows(){
        // Pinned MathJax 2 SVG multline tables contain one body column. Keep
        // each row's glyph geometry/vertical position, but let HTML position
        // short rows inside the actual print width instead of the old screen
        // width. Never reinterpret TeX or guess an unfamiliar SVG structure.
        for(const frame of document.querySelectorAll('.MathJax_SVG_Display .MathJax_SVG')){
          const input=document.getElementById(frame.id.replace(/-Frame$/,''));
          if(!input||input.tagName!=='SCRIPT'||!/^math\/tex/.test(input.type)
            ||!/^\s*\\begin\{multline\*?\}/.test(input.textContent)
            ||/\\shove(?:left|right)\b/.test(input.textContent))continue;
          const image=frame.querySelector(':scope > svg'),box=frame.closest('.MathJax_SVG_Display');
          const root=image?.firstElementChild;
          if(!image||!box||root?.tagName!=='g'||root.getAttribute('transform')!=='matrix(1 0 0 -1 0 0)')continue;
          const columns=Array.from(root.children).filter(g=>g.tagName==='g'
            &&!g.querySelector('[id^="wiz-pdf-svg-target-"],[id^="mjx-eqn-"]'));
          if(columns.length!==1)continue;
          // The pinned renderer omits the one-column wrapper for starred or
          // internally wrapped multline output. Both shapes keep G rows.
          const rowParent=columns[0].children.length===1?columns[0].firstElementChild:
            columns[0];
          if(!rowParent)continue;
          const rows=Array.from(rowParent.children);
          if(rows.length<2||rows.some(row=>row.tagName!=='g'||row.hasAttribute('id')||row.querySelector('[id]')))continue;
          const boxRect=box.getBoundingClientRect();
          const data=rows.map(row=>({row,rect:row.getBoundingClientRect(),bounds:row.getBBox()}));
          if(!(boxRect.width>0&&boxRect.height>0)||data.some(r=>!(r.rect.width>0&&r.rect.height>0)))continue;
          const tagWidth=Math.max(0,...targets.filter(r=>frame.contains(r.node)).map(r=>r.node.getBoundingClientRect().width));
          const layout=document.createElement('span');
          layout.setAttribute('aria-hidden','true');
          layout.style.cssText=`display:block;position:relative;width:100%;height:${boxRect.height}px;color:${getComputedStyle(frame).color}`;
          data.forEach(({row,rect,bounds},index)=>{
            const rowImage=document.createElementNS(svg,'svg'),clone=row.cloneNode(true);
            clone.setAttribute('transform','matrix(1 0 0 -1 0 0)');
            rowImage.setAttribute('viewBox',`${bounds.x} ${-bounds.y-bounds.height} ${bounds.width} ${bounds.height}`);
            rowImage.setAttribute('aria-hidden','true');rowImage.setAttribute('stroke','currentColor');
            rowImage.setAttribute('fill','currentColor');rowImage.setAttribute('stroke-width','0');
            rowImage.style.cssText=`position:absolute;top:${rect.top-boxRect.top}px;width:${rect.width}px;height:auto;overflow:visible`;
            if(index===0){
              const inset=`max(.8em,${Math.max(0,100*(rect.left-boxRect.left)/boxRect.width)}%)`;
              // MathJax may place a multline tag beside its first row. The
              // print-only badge shares that row's right edge, not just the
              // final row's. Reserve its width before fitting the first row.
              rowImage.style.left=inset;rowImage.style.maxWidth=`calc(100% - ${inset} - ${tagWidth}px - ${tagWidth?1.6:.8}em)`;
            }else if(index===data.length-1){
              const inset=`max(${tagWidth}px + ${tagWidth?1.6:.8}em,${Math.max(0,100*(boxRect.right-rect.right)/boxRect.width)}%)`;
              rowImage.style.right=inset;rowImage.style.maxWidth=`calc(100% - ${inset} - .8em)`;
            }else{
              rowImage.style.left='50%';rowImage.style.transform='translateX(-50%)';
              rowImage.style.maxWidth='calc(100% - 1.6em)';
            }
            rowImage.appendChild(clone);layout.appendChild(rowImage);
          });
          const record={node:frame,layout,hadStyle:frame.hasAttribute('style'),style:frame.getAttribute('style')};
          layouts.push(record);
          frame.style.setProperty('display','none','important');box.appendChild(layout);++lastFitted;
        }
      }
      function sameDocumentFragment(value){
        // MathJax 2 emits document.location + fragment when the reader has a
        // <base>. Chromium only preserves the PDF GoTo when it sees the local
        // fragment form during printing.
        if(value[0]==='#')return value;
        try{
          const target=new URL(value,document.baseURI),current=new URL(document.location.href);
          if(!target.hash)return '';
          const fragment=target.hash;
          target.hash='';current.hash='';
          return target.href===current.href?fragment:'';
        }catch(_){return '';}
      }
      function prepare(event){
        // Editable rich-text observers may treat even restored attributes as
        // edits. Do not augment that surface or change its existing export.
        if(!event.isTrusted||!armed||active||document.body?.isContentEditable)return;
        active=true;++prepares;lastAdded=0;lastFitted=0;
        try{
          // CodeMirror reserves scrollbar gutters with negative right margins.
          // Chromium includes that extra width when deciding to shrink a PDF,
          // even for a short read-only code line. Remove only this known gutter
          // during an armed print; retain the same live node and restore its
          // exact inline style through the existing print/snapshot FIFO.
          for(const node of document.querySelectorAll('.CodeMirror > .CodeMirror-scroll')){
            if(!(parseFloat(getComputedStyle(node).marginRight)<0))continue;
            boxes.push({node,hadStyle:node.hasAttribute('style'),style:node.getAttribute('style')});
            node.style.setProperty('margin-right','0','important');
          }
          // Reading keeps already-fitting v4 full-width SVGs native. Freeze
          // only for this armed print, before number/layout helpers resize them.
          // Their pixel-positioned groups cannot follow a later print viewport
          // change without a viewBox. Restore every original attribute exactly.
          for(const image of document.querySelectorAll('.MathJax_SVG_Display mjx-container > svg[data-mjx-viewBox]')){
            if(image.viewBox.baseVal.width>0)continue;
            const root=image.parentElement,ink=image.getBBox(),rect=image.getBoundingClientRect();
            const width=Math.max(rect.width,ink.x+ink.width),height=Math.max(rect.height,ink.y+ink.height);
            if(!(width>0&&height>0))continue;
            nativeFits.push({image,root,rootStyle:root.getAttribute('style'),attributes:
              ['viewBox','width','height','style','preserveAspectRatio','data-wiz-mjx4-ink-fit'].map(name=>[name,image.getAttribute(name)])});
            image.setAttribute('viewBox',`0 0 ${width} ${height}`);
            image.setAttribute('width',width+'px');image.setAttribute('height',height+'px');
            image.style.width=width+'px';image.style.maxWidth='100%';image.style.minWidth='0';image.style.height='auto';
            root.style.minWidth='0';root.style.maxWidth='100%';
            image.setAttribute('preserveAspectRatio','xMidYMid meet');image.setAttribute('data-wiz-mjx4-ink-fit','');
          }
          const referenced=new Map();
          for(const node of document.querySelectorAll('.MathJax_SVG a')){
            if(node.namespaceURI!==svg||node.hasAttributeNS(null,'href'))continue;
            const value=node.getAttributeNS(xlink,'href');
            if(!value)continue;
            const applied=sameDocumentFragment(value);
            if(!applied)continue;
            let id;try{id=decodeURIComponent(applied.slice(1));}catch(_){continue;}
            if(!id.startsWith('mjx-eqn-'))continue;
            const target=document.getElementById(id);
            if(!target||target.namespaceURI!==svg||!target.closest('.MathJax_SVG'))continue;
            links.push({node,applied});
            node.setAttributeNS(null,'href',applied);++lastAdded;
            referenced.set(id,target);
          }
          // Preserve every equation row number, including unreferenced align
          // rows whose original SVG right edge lies beyond the print width.
          for(const node of document.querySelectorAll('.MathJax_SVG_Display [id^="mjx-eqn-"]')){
            if(node.namespaceURI===svg&&node.closest('.MathJax_SVG'))referenced.set(node.id,node);
          }
          for(const [id,node] of referenced){
            // A wide reading viewport can place the SVG equation number beyond
            // the printable width. Keep a zero-size HTML destination at the
            // equation row and paint a print-only copy inside the right edge.
            const box=node.closest('.MathJax_SVG_Display');
            if(!box||box.namespaceURI!=='http://www.w3.org/1999/xhtml')continue;
            const bounds=node.getBBox(),rect=node.getBoundingClientRect(),boxRect=box.getBoundingClientRect();
            if(!(bounds.width>0&&bounds.height>0&&rect.width>0&&rect.height>0))continue;
            const badge=document.createElementNS(svg,'svg'),clone=node.cloneNode(true),proxy=document.createElement('span');
            clone.removeAttribute('id');clone.setAttribute('transform','matrix(1 0 0 -1 0 0)');
            badge.setAttribute('viewBox',`${bounds.x} ${-bounds.y-bounds.height} ${bounds.width} ${bounds.height}`);
            badge.setAttribute('aria-hidden','true');
            badge.setAttribute('stroke','currentColor');badge.setAttribute('fill','currentColor');badge.setAttribute('stroke-width','0');
            badge.style.cssText=`position:absolute;right:.8em;top:${rect.top-boxRect.top}px;width:${rect.width}px;height:${rect.height}px;overflow:visible;pointer-events:none`;
            badge.appendChild(clone);
            const nodeHadVisibility=node.hasAttribute('visibility'),nodeVisibility=node.getAttribute('visibility');
            let temporary='wiz-pdf-svg-target-'+targets.length;
            while(document.getElementById(temporary))temporary+='-';
            node.setAttribute('id',temporary);
            let boxRecord=boxes.find(record=>record.node===box),addedBox=false;
            try{
              if(!boxRecord){
                boxRecord={node:box,hadStyle:box.hasAttribute('style'),style:box.getAttribute('style')};
                if(getComputedStyle(box).position==='static')box.style.position='relative';
                boxes.push(boxRecord);addedBox=true;
              }
              proxy.id=id;proxy.style.cssText=`position:absolute;left:0;top:${rect.top-boxRect.top}px;width:0;height:0;overflow:hidden;pointer-events:none`;
              node.setAttribute('visibility','hidden');box.append(proxy,badge);
              targets.push({node,id,temporary,badge,proxy,nodeHadVisibility,nodeVisibility});
            }catch(error){
              badge.remove();proxy.remove();
              if(addedBox){
                boxes.pop();
                if(boxRecord.hadStyle)box.setAttribute('style',boxRecord.style);
                else{box.getAttribute('style');box.removeAttribute('style');}
              }
              if(nodeHadVisibility)node.setAttribute('visibility',nodeVisibility);else node.removeAttribute('visibility');
              node.setAttribute('id',id);throw error;
            }
          }
          fitMultlineRows();
          // Automatic line wrapping can leave a single inseparable SVG body
          // instead of the recognized row table. Do not reinterpret that tree.
          // Fit only this still-wide multline body inside its tag gutter, as
          // the A4 page does; keep ordinary text and number badges unscaled.
          for(const frame of document.querySelectorAll('.MathJax_SVG_Display .MathJax_SVG')){
            if(getComputedStyle(frame).display==='none')continue;
            const input=document.getElementById(frame.id.replace(/-Frame$/,''));
            const image=frame.querySelector(':scope > svg'),box=frame.closest('.MathJax_SVG_Display');
            if(!image||!box||!input||input.tagName!=='SCRIPT'||!/^math\/tex/.test(input.type)
              ||!/^\s*\\begin\{multline\*?\}/.test(input.textContent))continue;
            const rect=image.getBoundingClientRect(),width=box.getBoundingClientRect().width;
            const tagWidth=Math.max(0,...targets.filter(r=>frame.contains(r.node)).map(r=>r.badge.getBoundingClientRect().width));
            const gap=tagWidth?1.6*parseFloat(getComputedStyle(box).fontSize):0;
            const available=width-tagWidth-gap;
            if(!(available>0&&rect.height>0&&rect.width>available+1))continue;
            for(const node of [frame,image])layouts.push({node,layout:document.createElement('span'),hadStyle:node.hasAttribute('style'),style:node.getAttribute('style'),...(node===image?{aspect:node.getAttribute('preserveAspectRatio')}:{})});
            frame.style.setProperty('min-width','0','important');frame.style.setProperty('width',available+'px','important');frame.style.setProperty('max-width','100%','important');
            image.style.setProperty('min-width','0','important');image.style.setProperty('width',available+'px','important');image.style.setProperty('height',rect.height+'px','important');image.setAttribute('preserveAspectRatio','xMidYMid meet');++lastFitted;
          }
        }catch(_){++errors;restore();}
      }
      const afterPrint=event=>{if(event.isTrusted)restore();};
      const pageHide=event=>{if(event.isTrusted)restore();};
      function listen(){
        addEventListener('beforeprint',prepare,true);
        addEventListener('afterprint',afterPrint,true);
        addEventListener('pagehide',pageHide,true);
      }
      listen();
      Object.defineProperty(globalThis,'__wizPdfSvgLinks',{value:Object.freeze({
        arm:()=>{
          if(active||armed)return false;
          // Reader edit transitions use document.open/write: window globals
          // survive, but listeners do not. Reattach the SAME functions before
          // every serialized print; addEventListener deduplicates intact ones.
          listen();armed=true;return true;
        },
        finish:()=>{restore();armed=false;return !active&&!links.length&&!targets.length&&!boxes.length&&!layouts.length&&!nativeFits.length&&!errors;},
        status:()=>({active,pending:links.length+targets.length+boxes.length+layouts.length+nativeFits.length,prepares,restores,lastAdded,lastFitted,errors})
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
