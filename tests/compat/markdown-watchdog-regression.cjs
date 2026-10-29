// Derived from the licensed Wiz Markdown render completion method; see docs/third-party-licenses.md.
// Only the reviewed completion method is matched and executed. No application/user data.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const source=fs.readFileSync(path.join(__dirname,'../../share/files/wizeditor/wizEditorForMac.js'),'utf8');
const current="function(e,t){e.container&&(f=e.container),\"string\"==typeof e.markdownSrc?(S=e.markdownSrc,E=!0):(S=\"\",E=!1);!function(e){u.addCallback(c,e),u.getLoading(c)||(n.$&&n.Diagram&&n.flowchart&&n.prettyPrint?u.callback(c):(u.setLoading(c,!0),s.loadJs(c,s.getDependencyFiles(m.dependency,\"js\",\"markdown\"),(function(){u.setLoading(c,!1),u.callback(c)}))))}((function(){b.addClass(f,o.CLASS.READONLY);var e=m.options.reader.timeout.markdown;t=t||h;var n=!1,i=function(){t&&/^function$/i.test(r(t))&&!n&&(t(),n=!0,clearTimeout(wizMarkdownWatchdog))};var wizMarkdownWatchdog=setTimeout(i,e);C((function(){i()}))}))}";assert.equal(source.split(current).length-1,1,'tested completion function must match current product');
global.window=global;
(function(){
const frozen={"reference": "function(e,t){e.container&&(f=e.container),\"string\"==typeof e.markdownSrc?(S=e.markdownSrc,E=!0):(S=\"\",E=!1);!function(e){u.addCallback(c,e),u.getLoading(c)||(n.$&&n.Diagram&&n.flowchart&&n.prettyPrint?u.callback(c):(u.setLoading(c,!0),s.loadJs(c,s.getDependencyFiles(m.dependency,\"js\",\"markdown\"),(function(){u.setLoading(c,!1),u.callback(c)}))))}((function(){b.addClass(f,o.CLASS.READONLY);var e=m.options.reader.timeout.markdown;t=t||h;var n=!1,i=function(){t&&/^function$/i.test(r(t))&&!n&&(t(),n=!0)};setTimeout(i,e),C((function(){i()}))}))}", "candidate": "function(e,t){e.container&&(f=e.container),\"string\"==typeof e.markdownSrc?(S=e.markdownSrc,E=!0):(S=\"\",E=!1);!function(e){u.addCallback(c,e),u.getLoading(c)||(n.$&&n.Diagram&&n.flowchart&&n.prettyPrint?u.callback(c):(u.setLoading(c,!0),s.loadJs(c,s.getDependencyFiles(m.dependency,\"js\",\"markdown\"),(function(){u.setLoading(c,!1),u.callback(c)}))))}((function(){b.addClass(f,o.CLASS.READONLY);var e=m.options.reader.timeout.markdown;t=t||h;var n=!1,i=function(){t&&/^function$/i.test(r(t))&&!n&&(t(),n=!0,clearTimeout(wizMarkdownWatchdog))};var wizMarkdownWatchdog=setTimeout(i,e);C((function(){i()}))}))}"};
window.Wiz316RunWatchdogCases=function(){
 const cases=['asynchronous-success','synchronous-success','timeout-first','callback-throws-once','callback-always-throws','reentrant-once','duplicate-completions','invalid-callback','default-callback','no-callback','load-deferred','callback-null'];
 const out={cases:[],failures:0};
 function run(source,name){
  let seq=1,pending=new Map(),events=[],ready,loadDone,dependencyReady,callbackCount=0,clears=0,errorToken={},throwCount=0;
  const schedule=(fn,ms)=>{const id=seq++;pending.set(id,{fn,ms});return id;};
  const clear=id=>{++clears;pending.delete(id);};
  const callback=()=>{++callbackCount;events.push('callback');if(name==='callback-always-throws'||name==='callback-throws-once'&&throwCount++===0)throw errorToken;if(name==='reentrant-once'&&callbackCount===1)ready();};
  const n={};if(name!=='load-deferred')Object.assign(n,{$:{},Diagram:{},flowchart:{},prettyPrint:{}});
  const u={addCallback:(doc,fn)=>{dependencyReady=fn;},getLoading:()=>false,callback:()=>dependencyReady(),setLoading:()=>{}};
  const m={options:{reader:{timeout:{markdown:30000}}}};
  const C=fn=>{ready=fn;events.push('render-start');if(name==='synchronous-success')fn();};
  const s={getDependencyFiles:()=>[],loadJs:(doc,files,done)=>{loadDone=done;}};
  const fn=new Function('n','u','c','s','m','b','o','h','r','C','setTimeout','clearTimeout','var f,S,E;return ('+source+');')
   (n,u,{},s,m,{addClass:()=>events.push('readonly')},{CLASS:{READONLY:'readonly'}},name==='no-callback'?undefined:callback,x=>typeof x,C,schedule,clear);
  const invoke=fn=>{try{fn();}catch(error){events.push(error===errorToken?'same-error':'other-error');}};
  let t=name==='invalid-callback'?'invalid':name==='default-callback'||name==='no-callback'?undefined:name==='callback-null'?null:callback;
  invoke(()=>fn({container:{},markdownSrc:'synthetic'},t));
  if(name==='load-deferred')invoke(()=>loadDone());
  const fire=()=>{for(const [id,timer] of [...pending]){pending.delete(id);invoke(timer.fn);}};
  if(name==='timeout-first')fire();
  if(name!=='synchronous-success')invoke(()=>ready());
  if(name==='duplicate-completions')invoke(()=>ready());
  const beforeFallback=pending.size;fire();
  return {events,callbackCount,remainingAfterSuccess:beforeFallback,pendingAfterFinal:pending.size,clearCalls:clears};
 }
 cases.forEach(name=>{const a=run(frozen.reference,name),b=run(frozen.candidate,name),same=JSON.stringify(a.events)===JSON.stringify(b.events)&&a.callbackCount===b.callbackCount&&a.pendingAfterFinal===b.pendingAfterFinal;
  const shouldClear=!['timeout-first','callback-throws-once','callback-always-throws','invalid-callback','no-callback'].includes(name);
  const cleanup=shouldClear?b.remainingAfterSuccess===0:b.remainingAfterSuccess===a.remainingAfterSuccess;
  if(!same||!cleanup)++out.failures;
  out.cases.push({name,matched:same,cleanupValid:cleanup,referenceCallbacks:a.callbackCount,candidateCallbacks:b.callbackCount,referencePendingAfterSuccess:a.remainingAfterSuccess,candidatePendingAfterSuccess:b.remainingAfterSuccess,candidateClearCalls:b.clearCalls});});
 window.Wiz316WatchdogCases=out;return out;
};
})();

const result=Wiz316RunWatchdogCases();console.log(JSON.stringify(result));assert.equal(result.failures,0);
