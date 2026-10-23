'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto'),{chromium}=require('playwright');
const round=Number(process.argv[2]),before=process.argv[3],output=process.argv[4],repo=path.resolve(__dirname,'../..');
if(![7,8,10].includes(round)||!path.isAbsolute(before||'')||!path.isAbsolute(output||''))throw Error('round7/8/10, absolute before directory/output required');
const source=old=>fs.readFileSync(old?path.join(before,round===10?'index.js':'editorHelper.js'):path.join(repo,round===10?'share/files/welcomepage/js/index.js':'share/files/wizeditor/editorHelper.js'),'utf8');
const sourceHashes={before:crypto.createHash('sha256').update(source(true)).digest('hex'),after:crypto.createHash('sha256').update(source(false)).digest('hex')};
(async()=>{
  const browser=await chromium.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:true,chromiumSandbox:true});
  const failures=[],errors=[],requests=[],rows=[];let checks=0;const check=(v,m)=>{++checks;if(!v)failures.push(m);};
  try{
    const context=await browser.newContext({serviceWorkers:'block'});await context.route('**/*',r=>{requests.push(r.request().url());return r.abort();});
    for(const old of [true,false]){
      const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
      await page.setContent('<!doctype html><head><meta charset="utf-8"></head><body></body>');
      await page.evaluate(()=>{window.$=()=>({ready(){}});window.i18next={t:key=>key};});
      await page.addScriptTag({content:source(old)});
      const data=await page.evaluate(async round=>{
        const assert=(v,m)=>{if(!v)throw Error(m);};
        if(round===7){
          document.body.innerHTML='<p id="many">'+Array.from({length:6000},(_,i)=>'<span>ordinary '+i+' </span>').join('')
            +'<em id="reference">\\ref{thm:x}</em> $x+<strong>y</strong>$ <b id="inside">\\ref{thm:y}</b></p>'
            +'<div><p><i id="nested">\\ref{thm:z}</i></p><pre>not prose</pre></div>';
          const nodes=['reference','inside','nested'].map(id=>document.getElementById(id).firstChild);
          function run(){return WizMathTextContexts(nodes,document.body);}
          const map=run(),values=nodes.map(n=>{const c=map.get(n);assert(c,'reference has logical context');return{offset:c.offset,flags:Array.from(c.protectedPositions).reduce((a,b)=>Math.imul(a^b,16777619)>>>0,2166136261),length:c.protectedPositions.length};});
          run();const times=[];for(let i=0;i<7;i++){const t=performance.now();const v=run();times.push(performance.now()-t);assert(v.has(nodes[0]),'reference record remains');}times.sort((a,b)=>a-b);
          return{mapEntries:map.size,requestedNodes:nodes.length,values,medianMs:times[3],scope:'context record count, not browser resident memory'};
        }
        if(round===8){
          window.WizEditor={insertCustomStyle(){},removeStyleById(){},getContentHtml:()=>document.body.innerHTML,getMarkdownSrc:()=>''};
          window.WizNoteCss='/* WizNotePlus scientific math environments v1 */';
          document.body.className='markdown-body wiz-readonly';
          document.body.innerHTML=Array.from({length:500},(_,i)=>'<blockquote><p><strong>定理（A'+i+'）</strong> \\label{thm:'+i+'}</p><p>content '+i+'</p>'
            +'<p><strong>引理（B'+i+'）</strong> \\label{lem:'+i+'}</p><p>other '+i+'</p></blockquote>').join('');
          const original=document.body.innerHTML,match=String.prototype.match;let parses=0;
          String.prototype.match=function(pattern){if(String(pattern).includes('proposition')&&String(this).startsWith('定理')||String(pattern).includes('proposition')&&String(this).startsWith('引理'))++parses;return match.apply(this,arguments);};
          try{WizApplyMathEnvironments();}finally{String.prototype.match=match;}
          const projected=document.body.innerHTML,count=WizMathDecoration.blocks.length,splitCount=WizMathDecoration.splits.length;
          const headings=Array.from(document.querySelectorAll('.wiz-math-block')).map(e=>[e.id,e.getAttribute('data-wiz-math-heading')]);
          WizClearMathEnvironments();assert(document.body.innerHTML===original,'exact source restored after split and projection');
          const times=[];for(let i=0;i<7;i++){const t=performance.now();WizApplyMathEnvironments();times.push(performance.now()-t);assert(document.body.innerHTML===projected,'repeat same projection');WizClearMathEnvironments();assert(document.body.innerHTML===original,'repeat exact restoration');}times.sort((a,b)=>a-b);
          return{parseCalls:parses,blocks:count,splits:splitCount,headings,projectionLength:projected.length,sourceRestored:true,medianMs:times[3]};
        }
        document.body.innerHTML='<ul id="listTodo"></ul>';
        let release,entered;const firstEntered=new Promise(r=>entered=r),blocked=new Promise(r=>release=r);
        const counters={oldCalls:0,newCalls:0,oldDeleted:0,newDeleted:0};
        function proxy(i,latest){return{GUID:(latest?'new':'old')+i,Title:(latest?'new':'old')+' task '+i,async GetParamValue(key){++counters[latest?'newCalls':'oldCalls'];if(!latest&&i===0&&key==='CALENDAR_START'){entered();await blocked;}return key==='CALENDAR_START'?'2026-10-25 00:00:00':'1';},deleteLater(){++counters[latest?'newDeleted':'oldDeleted'];}};}
        const stale=Array.from({length:1000},(_,i)=>proxy(i,false)),latest=[proxy(0,true)];let queries=0;
        objDatabase={DocumentsFromSQLWhere:async()=>++queries===1?stale:latest};
        const oldRequest=listEvents();await firstEntered;const currentRequest=listEvents();await currentRequest;release();await oldRequest;
        assert(document.getElementById('listTodo').textContent.includes('new task 0')&&!document.getElementById('listTodo').textContent.includes('old task'),'late refresh never overwrites latest UI');
        assert(counters.oldDeleted===1000&&counters.newDeleted===1,'all QWebChannel proxy models released exactly once');
        return{...counters,queries,latestUi:document.getElementById('listTodo').textContent,sourceNotesChanged:false};
      },round);
      rows.push({variant:old?'before':'after',...data});await page.close();
    }
    const [a,b]=rows;
    if(round===7){check(JSON.stringify(a.values)===JSON.stringify(b.values),'logical context bytes and offsets unchanged');check(b.mapEntries===b.requestedNodes&&a.mapEntries>6000,'only requested reference contexts retained');check(b.mapEntries<a.mapEntries/100,'context record allocation reduced');}
    if(round===8){check(JSON.stringify(a.headings)===JSON.stringify(b.headings),'all projected headings/IDs/numbers unchanged');check(a.blocks===1000&&b.blocks===1000&&a.splits===500&&b.splits===500,'same split and decoration counts');check(a.sourceRestored&&b.sourceRestored,'exact source restoration');check(b.parseCalls===1000&&a.parseCalls===2000,'heading parsed once per decoration pass');}
    if(round===10){check(a.latestUi===b.latestUi,'latest welcome task UI identical');check(b.oldCalls===1&&a.oldCalls===2000,'obsolete refresh stops after its outstanding parameter read');check(a.oldDeleted===b.oldDeleted&&a.newDeleted===b.newDeleted,'no QWebChannel proxy retained or prematurely deleted');check(a.newCalls===b.newCalls&&b.newCalls===2,'latest task still reads both original parameters');}
    check(!errors.length&&!requests.length,'no page errors or network requests');
    const report={round,accepted:!failures.length,checks,failures,rows,pageErrors:errors,remoteRequests:requests,sourceHashes,scope:'synthetic owned code in headless Chrome; not normal account/full client acceptance'};
    fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n',{flag:'wx'});
    console.log(JSON.stringify({...report,rows:rows.map(r=>({...r,headings:r.headings?{count:r.headings.length}:undefined}))}));if(failures.length)process.exitCode=1;
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
