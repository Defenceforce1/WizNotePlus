// Diagnostic only, installed AFTER the print helper; not shipped or App-linked.
(() => {
  const records=[];
  function restore(){
    for(const r of records.splice(0)) {
      r.layout.remove();
      if(r.hadStyle)r.node.setAttribute('style',r.style);
      else{r.node.getAttribute('style');r.node.removeAttribute('style');}
    }
  }
  addEventListener('beforeprint',event=>{
    if(!event.isTrusted||document.body.isContentEditable)return;
    for(const frame of document.querySelectorAll('.MathJax_SVG_Display .MathJax_SVG')) {
      const input=document.getElementById(frame.id.replace(/-Frame$/,''));
      if(!input||!/^\s*\\begin\{multline\*?\}/.test(input.textContent))continue;
      const node=frame.querySelector('svg'),box=frame.closest('.MathJax_SVG_Display');
      if(!node||!box)continue;
      const boxRect=box.getBoundingClientRect();
      const columns=Array.from(node.firstElementChild?.children||[])
        .filter(g=>g.tagName==='g'&&!g.querySelector('[id^="wiz-pdf-svg-target-"],[id^="mjx-eqn-"]'));
      if(columns.length!==1||columns[0].children.length!==1)continue;
      const rows=Array.from(columns[0].firstElementChild.children);
      if(rows.length<2||rows.some(r=>r.tagName!=='g'||r.querySelector('[id]')))continue;
      const data=rows.map(row=>({row,rect:row.getBoundingClientRect(),bbox:row.getBBox()}));
      if(!(boxRect.width>0&&boxRect.height>0)||data.some(r=>!(r.rect.width>0&&r.rect.height>0)))continue;
      const layout=document.createElement('span');
      layout.style.cssText=`display:block;position:relative;width:100%;height:${boxRect.height}px`;
      data.forEach(({row,rect,bbox},index)=>{
        const image=document.createElementNS('http://www.w3.org/2000/svg','svg'),clone=row.cloneNode(true);
        clone.setAttribute('transform','matrix(1 0 0 -1 0 0)');
        image.setAttribute('viewBox',`${bbox.x} ${-bbox.y-bbox.height} ${bbox.width} ${bbox.height}`);
        image.setAttribute('aria-hidden','true');image.setAttribute('stroke','currentColor');
        image.setAttribute('fill','currentColor');image.setAttribute('stroke-width','0');
        image.style.cssText=`position:absolute;top:${rect.top-boxRect.top}px;width:${rect.width}px;height:auto;max-width:calc(100% - 5em);overflow:visible;pointer-events:none`;
        if(index===0)image.style.left=`max(.8em,${100*(rect.left-boxRect.left)/boxRect.width}%)`;
        else if(index===data.length-1)image.style.right=`max(3.2em,${100*(boxRect.right-rect.right)/boxRect.width}%)`;
        else{image.style.left='50%';image.style.transform='translateX(-50%)';}
        image.append(clone);layout.append(image);
      });
      records.push({node:frame,layout,hadStyle:frame.hasAttribute('style'),style:frame.getAttribute('style')});
      frame.style.setProperty('display','none','important');box.append(layout);
    }
  },true);
  addEventListener('afterprint',event=>{if(event.isTrusted)restore();},true);
  window.RestoreMultlineFitPrototype=restore;
})();
