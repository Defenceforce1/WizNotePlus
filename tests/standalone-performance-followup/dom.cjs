'use strict';
const fs=require('fs'),path=require('path'),crypto=require('crypto'),{chromium}=require('playwright');
const round=Number(process.argv[2]),before=process.argv[3],output=process.argv[4],repo=path.resolve(__dirname,'../..');
if(![7,8,9,10].includes(round)||!path.isAbsolute(before||'')||!path.isAbsolute(output||''))throw Error('round7-10, absolute before/output');
const name=round===10?'index.js':'editorHelper.js',source=old=>fs.readFileSync(old?path.join(before,name):repo+(round===10?'/share/files/welcomepage/js/index.js':'/share/files/wizeditor/editorHelper.js'),'utf8');
(async()=>{
  const browser=await chromium.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:true,chromiumSandbox:true});
  const errors=[],requests=[],failures=[],rows=[];let checks=0;const check=(v,m)=>{++checks;if(!v)failures.push(m);};
  try{
    const context=await browser.newContext({serviceWorkers:'block'});await context.route('**/*',r=>{requests.push(r.request().url());return r.abort();});
    for(const old of [true,false]){
      const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));
      await page.setContent('<!doctype html><meta charset="utf-8"><body></body>');
      await page.evaluate(()=>{window.$=()=>({ready(){}});window.i18next={t:key=>key};});
      await page.addScriptTag({content:source(old)});
      const data=await page.evaluate(async round=>{
        const assertions=[];const assert=(v,m)=>{assertions.push(m);if(!v)throw Error(m);};
        function editor(){
          window.WizEditor={insertCustomStyle(){},removeStyleById(){},getContentHtml:()=>document.body.innerHTML,getMarkdownSrc:()=>''};
          window.WizNoteCss='/* WizNotePlus scientific math environments v1 */';document.body.className='markdown-body wiz-readonly';
        }
        if(round===7||round===8){
          editor();
          document.body.innerHTML=(round===7?Array.from({length:600},(_,i)=>'<blockquote><p> <strong>定理（'+i+'）</strong> \\label{th:'+i+'}</p><p>body</p></blockquote>').join(''):
            '<blockquote><p><strong>定理</strong> \\label{th:x}</p><p>body</p></blockquote>'+Array.from({length:9000},(_,i)=>'<p><b>plain '+i+'</b> prose</p>').join('')+'<p>See \\ref{th:x}</p>')
            +'<blockquote><p><strong>引理</strong><em>not a heading</em></p><p>plain</p></blockquote>';
          const original=document.body.innerHTML,from=Array.from,closest=Element.prototype.closest;let childArrays=0,ancestryMatches=0;
          Array.from=function(nodes){if(nodes instanceof NodeList&&nodes.length&&nodes[0].parentElement?.tagName==='P')childArrays+=nodes.length;return from.apply(this,arguments);};
          Element.prototype.closest=function(selector){if(selector.includes('.wiz-math-source-heading'))ancestryMatches++;return closest.apply(this,arguments);};
          try{WizApplyMathEnvironments();}finally{Array.from=from;Element.prototype.closest=closest;}
          const projected=document.body.innerHTML,headings=Array.from(document.querySelectorAll('.wiz-math-block')).map(e=>[e.id,e.getAttribute('data-wiz-math-heading')]),links=Array.from(document.querySelectorAll('a.wiz-math-reference')).map(e=>[e.getAttribute('href'),e.textContent]);
          assert(headings.length===(round===7?600:1),'only exact heading shape projected');assert(links.length===(round===7?0:1),'correct reference count');
          WizClearMathEnvironments();assert(document.body.innerHTML===original,'exact source HTML restored');
          const times=[];for(let i=0;i<7;i++){const t=performance.now();WizApplyMathEnvironments();times.push(performance.now()-t);assert(document.body.innerHTML===projected,'repeat projection '+i);WizClearMathEnvironments();assert(document.body.innerHTML===original,'repeat restoration '+i);}times.sort((a,b)=>a-b);
          return{assertions,childArrayElements:childArrays,ancestryMatches,headings,links,sourceLength:original.length,medianMs:times[3]};
        }
        if(round===9){
          document.body.innerHTML='<div id="scope"><em id="ref">\\ref{th:x}</em> $x+<b>y</b>$ '
            +Array.from({length:1500},(_,i)=>'<div><p>nested '+i+' <b>$n$</b></p></div><pre><span>code '+i+'</span></pre>').join('')
            +'<p><i id="inner">before \\ref{th:y}</i> $u+<span id="inside">\\ref{th:z}</span>$</p>'
            +'<a><b id="excluded">\\ref{th:a}</b></a> tail</div>';
          const nodes=['ref','inner','inside','excluded'].map(id=>document.getElementById(id).firstChild),closest=Element.prototype.closest;
          let ancestry=0;Element.prototype.closest=function(){ancestry++;return closest.apply(this,arguments);};
          let map;try{map=WizMathTextContexts(nodes,document.body);}finally{Element.prototype.closest=closest;}
          const values=nodes.map(n=>{const c=map.get(n);return c?{offset:c.offset,flags:Array.from(c.protectedPositions)}:null;});
          assert(map.has(nodes[0])&&map.has(nodes[1])&&map.has(nodes[2]),'all eligible scopes available');assert(!map.has(nodes[3]),'excluded anchor remains excluded');
          assert(!map.get(nodes[0]).protectedPositions[map.get(nodes[0]).offset],'outside formula unprotected');assert(map.get(nodes[2]).protectedPositions[map.get(nodes[2]).offset]===1,'cross-node formula reference remains protected');
          const times=[];for(let i=0;i<7;i++){const t=performance.now();const result=WizMathTextContexts(nodes,document.body);times.push(performance.now()-t);assert(result.size===3,'no excluded context retained '+i);}times.sort((a,b)=>a-b);
          return{assertions,ancestryMatches:ancestry,values,mapSize:map.size,medianMs:times[3]};
        }
        const wait=ms=>new Promise(r=>setTimeout(r,ms));
        async function until(test){for(let i=0;i<300&&!test();i++)await wait(2);assert(test(),'scheduled refresh reaches completion');}
        let entered,release;const entry=new Promise(r=>entered=r),blocked=new Promise(r=>release=r);let calls=0,active=0,maxActive=0,version=0,shown=-1;
        async function refresh(){const current=version;calls++;active++;maxActive=Math.max(maxActive,active);if(calls===1)entered();await blocked;shown=current;active--;}
        const scheduled=typeof welcomeScheduledRefresh==='function',notify=scheduled?welcomeScheduledRefresh(refresh):refresh;
        for(let i=0;i<64;i++)notify();await entry;version=1;for(let i=0;i<64;i++)notify();release();
        await until(()=>active===0&&shown===1);await wait(15);
        assert(shown===1,'latest notification not lost');assert(!active,'all refresh promises settled');
        const main={calls,maxActive,shown};
        if(scheduled){
          let retries=0,original=console.error,expectedErrors=0;console.error=()=>expectedErrors++;
          const again=welcomeScheduledRefresh(async()=>{if(++retries===1)throw Error('synthetic rejection');});
          try{again();await until(()=>retries===1);await wait(3);again();await until(()=>retries===2);await wait(3);assert(expectedErrors===1,'rejected refresh is handled and later notifications retry');}finally{console.error=original;}
          let retiredCalls=0;const retired=welcomeScheduledRefresh(async()=>retiredCalls++);retired();window.dispatchEvent(new PageTransitionEvent('pagehide',{persisted:true}));await wait(5);assert(retiredCalls===1,'bfcache pagehide does not retire refresh');
          retired();window.dispatchEvent(new PageTransitionEvent('pagehide',{persisted:false}));await wait(5);assert(retiredCalls===1,'retirement cancels scheduled refresh');retired();await wait(5);assert(retiredCalls===1,'retired wrapper never restarts');
        }
        return{assertions,...main,scheduled};
      },round);
      checks+=data.assertions.length;rows.push({variant:old?'before':'after',...data});await page.close();
    }
    const[a,b]=rows;
    if(round===7){check(JSON.stringify(a.headings)===JSON.stringify(b.headings)&&JSON.stringify(a.links)===JSON.stringify(b.links),'same heading projection');check(a.childArrayElements>=1200&&b.childArrayElements===0,'heading parser no longer allocates eager child arrays');}
    if(round===8){check(JSON.stringify(a.headings)===JSON.stringify(b.headings)&&JSON.stringify(a.links)===JSON.stringify(b.links),'same links and headings');check(a.ancestryMatches>18000&&b.ancestryMatches<20,'non-reference text rejected before ancestry query');}
    if(round===9){check(JSON.stringify(a.values)===JSON.stringify(b.values),'same context offsets and cross-node math protection bytes');check(a.ancestryMatches>7000&&b.ancestryMatches<20,'nested and excluded DOM subtrees pruned');}
    if(round===10){check(a.calls===128&&b.calls===2,'notification bursts require one active and one trailing refresh');check(a.maxActive===128&&b.maxActive===1,'one in-flight read limits overlapping refresh state');check(a.shown===b.shown&&b.shown===1,'latest UI state preserved');}
    check(!errors.length&&!requests.length,'no page errors or external requests');
    const report={round,accepted:!failures.length,checks,failures,rows,pageErrors:errors,remoteRequests:requests,sourceHashes:{before:crypto.createHash('sha256').update(source(true)).digest('hex'),after:crypto.createHash('sha256').update(source(false)).digest('hex')},scope:'synthetic owned DOM and notification workload; not complete client or native RSS measurements'};
    fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({...report,rows:rows.map(r=>({...r,headings:r.headings?{count:r.headings.length}:undefined}))}));if(failures.length)process.exitCode=1;
  }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
