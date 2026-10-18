#!/usr/bin/env node
'use strict';
// Attach only to this script's isolated --note-presentation-self-test process.
// Never connect to an existing user session, navigate a real note or install an App.
const fs=require('node:fs'),path=require('node:path'),net=require('node:net'),cp=require('node:child_process');
const helperFile=process.env.WIZNOTE_EXPLORE_HELPER_FILE||path.resolve(__dirname,'../../share/files/wizeditor/editorHelper.js');
const appBinary=process.env.WIZNOTE_EXPLORE_APP_BINARY;
if(!appBinary||!path.isAbsolute(appBinary)||!fs.statSync(appBinary).isFile())
  throw new Error('Supply an explicit absolute developer test executable; never auto-launch an installed App');
const resolvedBinary=fs.realpathSync(appBinary);
if(/^\/Applications\//.test(resolvedBinary)||/^\/Users\/[^/]+\/Applications\//.test(resolvedBinary))
  throw new Error('Installed Apps are not diagnostic hosts; use an isolated developer build');
fs.accessSync(resolvedBinary,fs.constants.X_OK);
const platform=process.env.WIZNOTE_EXPLORE_PLATFORM||'offscreen';
if(!['offscreen','cocoa'].includes(platform))throw new Error('Unsupported isolated test platform');
const rawHelperSource=fs.readFileSync(helperFile,'utf8');
const helperSource=process.env.WIZNOTE_EXPLORE_BOUNDARY_CONTEXT?require('./math-boundary-prototype.cjs')(rawHelperSource):rawHelperSource;
const regressionSource=fs.readFileSync(path.join(__dirname,'math-decoration-regression.js'),'utf8');
const boundaryRegressionSource=fs.readFileSync(path.join(__dirname,'math-boundary-regression.js'),'utf8');
const expectedGrowth=Number(process.env.WIZNOTE_EXPECT_TEXT_GROWTH||4000);
if(!Number.isInteger(expectedGrowth)||expectedGrowth<0)throw new Error('Expected text growth must be a nonnegative integer');
const output=process.argv[2];
if(!output||!path.isAbsolute(output))throw new Error('Absolute output directory required');
fs.mkdirSync(output,{recursive:true});
const pause=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function connect(url){
  const ws=new WebSocket(url),pending=new Map();let id=0;
  await new Promise((resolve,reject)=>{ws.addEventListener('open',resolve,{once:true});ws.addEventListener('error',reject,{once:true});});
  ws.addEventListener('message',event=>{const message=JSON.parse(event.data);const p=pending.get(message.id);if(!p)return;clearTimeout(p.timer);pending.delete(message.id);message.error?p.reject(new Error(message.error.message)):p.resolve(message.result);});
  ws.addEventListener('close',()=>{for(const p of pending.values()){clearTimeout(p.timer);p.reject(new Error('Synthetic test target closed'));}pending.clear();});
  return{async call(method,params={}){
    if(ws.readyState!==WebSocket.OPEN)throw new Error('Synthetic debugger is not connected');
    const current=++id;
    const promise=new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{pending.delete(current);reject(new Error(`Synthetic debugger timeout: ${method}`));},10000);
      pending.set(current,{resolve,reject,timer});
    });
    ws.send(JSON.stringify({id:current,method,params}));return promise;
  },close(){ws.close();}};
}
async function evaluate(session,expression){
  const result=await session.call('Runtime.evaluate',{expression,awaitPromise:true,returnByValue:true});
  if(result.exceptionDetails)throw new Error(result.exceptionDetails.exception?.description||result.exceptionDetails.text);
  return result.result.value;
}
(async()=>{
  const reservation=net.createServer();await new Promise(resolve=>reservation.listen(0,'127.0.0.1',resolve));
  const port=reservation.address().port;await new Promise(resolve=>reservation.close(resolve));
  const child=cp.spawn(appBinary,['--note-presentation-self-test'],
    {env:{...process.env,QT_QPA_PLATFORM:platform,QTWEBENGINE_REMOTE_DEBUGGING:`127.0.0.1:${port}`},stdio:['ignore','pipe','pipe']});
  let log='',ended=false;
  const logPath=path.join(output,'native-presentation.log');
  fs.writeFileSync(logPath,'');
  for(const stream of[child.stdout,child.stderr])stream.on('data',data=>{log+=data.toString();fs.appendFileSync(logPath,data);});
  const finished=new Promise(resolve=>child.on('close',(code,signal)=>{ended=true;resolve({code,signal});}));
  const result={kind:'Explicit developer Qt WebEngine; isolated synthetic presentation self-test only',appBinary,platform,helperFile,
    helperSha256:require('node:crypto').createHash('sha256').update(rawHelperSource).digest('hex'),
    executedHelperSha256:require('node:crypto').createHash('sha256').update(helperSource).digest('hex'),probe:null,testProcessId:child.pid,attachNotes:[]};
  let session;
  let gateInstalled=false;
  try{
    const start=Date.now();
    while(!ended&&!result.probe&&Date.now()-start<25000){
      try{
        // Confirm our newly spawned test has announced its own debugger first.
        if(!log.includes('DevTools listening on')){await pause(25);continue;}
        const targets=await(await fetch(`http://127.0.0.1:${port}/json/list`)).json();
        const target=targets.find(t=>t.type==='page'&&/^file:/.test(t.url)&&/\/math-block-[0-5]\.html$/.test(new URL(t.url).pathname));
        if(!target){await pause(25);continue;}
        session=await connect(target.webSocketDebuggerUrl);
        const gate=await evaluate(session,`(()=>{
          if(location.href!==${JSON.stringify(target.url)}||typeof WizApplyMathEnvironments!=='function'||window.__WizProbeGate||window.FixtureReady===true)return false;
          window.__WizProbeGate=true;let ready=window.FixtureReady;
          Object.defineProperty(window,'FixtureReady',{configurable:true,get:()=>false,set:v=>{ready=v;window.__WizProbeOriginalReady=v;}});
          return true;
        })()`);
        if(!gate){session.close();session=null;await pause(25);continue;}
        gateInstalled=true;
        const readyStart=Date.now();
        while(Date.now()-readyStart<12000&&!await evaluate(session,'window.__WizProbeOriginalReady===true'))await pause(25);
        if(!await evaluate(session,'window.__WizProbeOriginalReady===true'))throw new Error('Synthetic reader did not become ready before probe deadline');
        result.probe=await evaluate(session,`(async()=>{
          const frame=document.createElement('iframe');frame.style.cssText='position:fixed;left:-20000px;width:850px;height:660px';document.documentElement.appendChild(frame);
          try{
            const w=frame.contentWindow,d=frame.contentDocument;d.open();d.write('<!doctype html><html><head></head><body class="markdown-body wiz-readonly"></body></html>');d.close();
            w.eval(${JSON.stringify(helperSource)});
            function reset(html){d.body.innerHTML=html;w.WizMathDecoration={blocks:[],links:[],splits:[]};w.WizNoteCss='/* WizNotePlus scientific math environments v1 */';w.WizEditor={getContentHtml:()=>d.body.innerHTML,getMarkdownSrc:()=>d.body.innerHTML};}
            function count(){const walk=d.createTreeWalker(d.body,w.NodeFilter.SHOW_TEXT);let n=0;while(walk.nextNode())n++;return n;}
            const html=Array.from({length:100},(_,i)=>'<p>See \\\\ref{thm:'+i+'}.</p><blockquote><p><strong>定理</strong> \\\\label{thm:'+i+'}</p><p>Body.</p></blockquote>').join('');
            const baseline=w.WizApplyMathEnvironments;
            reset(html);baseline();const before=count();for(let i=0;i<20;i++)baseline();const after=count();const originalSame=w.WizEditor.getContentHtml()===html;
            let code=baseline.toString().replace('fragment.appendChild(document.createTextNode(source.slice(offset, result.index)));','if(result.index>offset)fragment.appendChild(document.createTextNode(source.slice(offset,result.index)));')
              .replace('fragment.appendChild(document.createTextNode(source.slice(offset))); text.replaceWith(fragment);','if(offset<source.length)fragment.appendChild(document.createTextNode(source.slice(offset)));text.replaceWith(fragment);');
            if(code===baseline.toString())throw new Error('Native prototype source anchor mismatch');
            const candidate=w.eval('('+code+')');w.WizApplyMathEnvironments=candidate;reset(html);candidate();const candidateBefore=count();for(let i=0;i<20;i++)candidate();const candidateAfter=count(),candidateSame=w.WizEditor.getContentHtml()===html;
            w.WizApplyMathEnvironments=baseline;
            const boundaries=[];
            const suffix='<blockquote><p><strong>定理</strong> \\\\label{thm:a}</p><p>Target.</p></blockquote>';
            for(const [name,text,expected]of[['prose','See \\\\ref{thm:a}',1],['compound-parens','\\\\(x+\\\\ref{thm:a}\\\\)',0],['compound-display','$$x+\\\\ref{thm:a}$$',0],['split-formula','\\\\(x+<em>\\\\ref{thm:a}</em>\\\\)',0]]){
              reset('<p>'+text+'</p>'+suffix);baseline();const links=d.querySelectorAll('.wiz-math-reference').length;boundaries.push({name,links,expected,matches:links===expected});
            }
            reset('<blockquote><p><strong>定理 7</strong></p><p>Body.</p></blockquote>');const clean=d.body.innerHTML;baseline();const serialized=w.WizEditor.getContentHtml();
            const originalClear=w.WizClearMathEnvironments;
            const clearCode=originalClear.toString().replace('if (record.attrs[name] === null) record.element.removeAttribute(name);',
              'if(record.attrs[name]===null){if(name==="style")record.element.getAttribute(name);record.element.removeAttribute(name);}');
            let styleFlushSame=serialized===clean;
            if(clearCode!==originalClear.toString()){
              w.WizClearMathEnvironments=w.eval('('+clearCode+')');
              reset(clean);baseline();styleFlushSame=w.WizEditor.getContentHtml()===clean;w.WizClearMathEnvironments=originalClear;
            }else if(!styleFlushSame)throw new Error('Native style prototype source anchor mismatch');
            w.eval(${JSON.stringify(regressionSource)});
            const regression=w.WizRunMathDecorationRegression();
            let boundaryRegression=null;
            if(${JSON.stringify(Boolean(process.env.WIZNOTE_EXPLORE_BOUNDARY_CONTEXT||process.env.WIZNOTE_REQUIRE_MATH_BOUNDARIES))}){
              w.eval(${JSON.stringify(boundaryRegressionSource)});boundaryRegression=w.WizRunMathBoundaryRegression();
            }
            const microbenchmarks=${JSON.stringify(Boolean(process.env.WIZNOTE_DECORATION_BENCH))}?w.WizRunMathDecorationMicrobenchmark():null;
            const blockLookupBench=${JSON.stringify(Boolean(process.env.WIZNOTE_BLOCK_LOOKUP_BENCH))}?w.WizRunMathBlockLookupBenchmark():null;
            return{regression,boundaryRegression,microbenchmarks,blockLookupBench,userAgent:navigator.userAgent,baseline:{before,after,growth:after-before,serializedSame:originalSame},
              skipEmptyPrototype:{before:candidateBefore,after:candidateAfter,growth:candidateAfter-candidateBefore,serializedSame:candidateSame},
              boundaries,manualCounter:{serializedSame:serialized===clean,hasEmptyStyle:/style=""/.test(serialized),styleFlushSame}};
          }finally{frame.remove();}
        })()`);
        result.targetFixture=path.basename(new URL(target.url).pathname);
        if(process.env.WIZNOTE_MATH_RETENTION_PROBE){
          result.mathRetention=await require('./explore-math-retention.cjs')(
            session,helperSource,process.env.WIZNOTE_EXPECT_RETENTION_BASELINE_SHA256);
        }
        if(process.env.WIZNOTE_EXPLORE_NATIVE_PDF){
          try{
            const printed=await session.call('Page.printToPDF',{paperWidth:210/25.4,paperHeight:297/25.4,
              marginTop:5/25.4,marginBottom:5/25.4,marginLeft:5/25.4,marginRight:5/25.4,printBackground:true});
            const bytes=Buffer.from(printed.data||'','base64');
            if(!bytes.subarray(0,5).equals(Buffer.from('%PDF-')))throw new Error('Native debug print returned no PDF bytes');
            fs.writeFileSync(path.join(output,'native-fixture-sanity.pdf'),bytes);result.pdfProtocol={supported:true,bytes:bytes.length};
          }catch(error){result.pdfProtocol={supported:false,error:error.message};}
        }
        await evaluate(session,"Object.defineProperty(window,'FixtureReady',{value:true,writable:true,configurable:true,enumerable:true});true");
        session.close();session=null;
      }catch(error){
        result.attachNotes.push(error.message);
        if(session){try{await evaluate(session,"Object.defineProperty(window,'FixtureReady',{value:true,writable:true,configurable:true});true");}catch{}session.close();session=null;}
        if(gateInstalled){result.probeError=error.message;break;}
        await pause(25);
      }
    }
  }finally{
    if(session)session.close();
    // No forced quit. Existing self-test owns its deadlines and exits normally.
    result.process=await finished;
    fs.writeFileSync(path.join(output,'native-presentation.log'),log);
    result.nativePasses=log.split('\n').filter(line=>line.startsWith('PASS')).length;
    result.nativeFailures=log.split('\n').filter(line=>line.startsWith('FAIL'));
    result.expectedProbeBehavior=!!result.probe&&result.probe.baseline.growth===expectedGrowth&&result.probe.skipEmptyPrototype.growth===0
      &&result.probe.baseline.serializedSame&&result.probe.skipEmptyPrototype.serializedSame;
    result.candidateRegressionPassed=!!result.probe&&result.probe.regression.failures.length===0
      &&(!result.probe.boundaryRegression||result.probe.boundaryRegression.failures.length===0)
      &&(!result.probe.blockLookupBench||result.probe.blockLookupBench.every(record=>record.sourceNumberAndDestinationSame))
      &&(!result.mathRetention||result.mathRetention.passed);
    fs.writeFileSync(path.join(output,'native-renderer-results.json'),JSON.stringify(result,null,2)+'\n');
  }
  console.log(JSON.stringify({helperSha256:result.helperSha256,expectedProbeBehavior:result.expectedProbeBehavior,
    candidateRegressionPassed:result.candidateRegressionPassed,domPasses:result.probe?.regression.passed,
    boundaryPasses:result.probe?.boundaryRegression?.passed,nativeHostPasses:result.nativePasses,
    nativeFailures:result.nativeFailures,process:result.process,attachNotes:result.attachNotes,probeError:result.probeError}));
  process.exitCode=result.process.code===0&&result.expectedProbeBehavior
    &&(!process.env.WIZNOTE_REQUIRE_CANDIDATE_REGRESSION||result.candidateRegressionPassed)?0:1;
})().catch(error=>{console.error(error);process.exitCode=1;});
