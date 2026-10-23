'use strict';
// Owned extension only. Synthetic public editor API/DOM fixtures, no account,
// network, vendor internals, or whole-client/performance acceptance claim.
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),assert=require('node:assert/strict'),{performance}=require('node:perf_hooks');
const candidate=process.argv[2]||path.resolve(__dirname,'../../share/files/wizeditor/markdownEnvironments.js');
const baseline=process.argv[3],source=fs.readFileSync(candidate,'utf8');let checks=0,failures=[];
const check=(v,m)=>{++checks;if(!v)failures.push(m);};
const flush=()=>new Promise(resolve=>setImmediate(resolve));
function fixture(code=source){
 const events=[],loads=[],listeners=new Map(),block={querySelector:()=>null,isConnected:true,textContent:'Synthetic',tagName:'DIV',replaceWith(){}};
 const context={console:{error(){}},Map,Uint8Array,Promise,setTimeout,clearTimeout,document:{body:{classList:{contains:()=>true},isContentEditable:false},querySelectorAll:()=>[block]},addEventListener:(n,f)=>listeners.set(n,f),removeEventListener:(n,f)=>{if(listeners.get(n)===f)listeners.delete(n);}};
 context.window=context;context.WizEditor={on:function(o,cb){events.push('on');context.document.body.isContentEditable=true;if(typeof o==='function')o();else if(cb)cb();return this;},off:function(o,cb){events.push('off');context.document.body.isContentEditable=false;if(cb)cb();return this;}};
 context.WizReader={};context.MathJax={InputJax:{TeX:{resetEquationNumbers(){}}},Hub:{Queue(args,callback){events.push(typeof args==='function'?'callback':args[0]);if(typeof args==='function')args();else if(args[3])args[3]();if(callback)callback();}}};
 vm.createContext(context);vm.runInContext(code,context);context.WizLoadMarkdownEnvironmentMathJax=()=>new Promise((resolve,reject)=>loads.push({resolve,reject}));
 context.WizInstallMarkdownEnvironmentRendering('synthetic-local',{});
 return {context,events,loads,listeners};
}
async function modeChecks(){
 for(const operations of [['on','off'],['on','on'],['on','off','on'],['off','on','off']]){
  const f=fixture(),callbacks=[];f.context.WizEditor.off(null,()=>callbacks.push('old'));
  for(const [i,operation]of operations.entries())f.context.WizEditor[operation](null,()=>callbacks.push(String(i)));
  for(let i=0;i<8;i++){for(const load of f.loads.splice(0))load.resolve();await flush();}
  const last=operations.at(-1),lastIndex=String(operations.length-1);
  check(f.context.document.body.isContentEditable===(last==='on'),'latest mode wins '+operations.join('/'));
  check(callbacks.length===1&&callbacks[0]===lastIndex,'superseded completions suppressed '+operations.join('/'));
 }
 const failed=fixture();let opened=0;failed.context.WizEditor.off();failed.context.WizEditor.on(null,()=>++opened);failed.loads.shift().reject(Error('synthetic dependency failure'));await flush();await flush();
 check(opened===1&&failed.context.document.body.isContentEditable,'latest edit resumes after dependency failure');
 const leaving=fixture();let resumed=0;leaving.context.WizEditor.off();leaving.context.WizEditor.on(null,()=>++resumed);leaving.listeners.get('pagehide')?.();leaving.loads.shift().resolve();await flush();await flush();
 check(resumed===0,'page teardown retires queued edit');
 check(leaving.listeners.size===0,'terminal pagehide releases mode listener');
 const cached=fixture();cached.listeners.get('pagehide')?.({persisted:true});let restored=0;cached.context.WizEditor.on(null,()=>++restored);check(restored===1,'cached-page suspension does not permanently retire the editor');cached.listeners.get('pagehide')?.();check(cached.listeners.size===0,'later terminal pagehide still retires cached-page listener');
}
function loadCleanupChecks(){
 let timerId=0;const timers=new Map(),scripts=[],listeners=new Map();
 const context={console,Map,Uint8Array,Promise,setTimeout:fn=>{timers.set(++timerId,fn);return timerId;},clearTimeout:id=>timers.delete(id),document:{getElementById:()=>null,head:{appendChild:s=>scripts.push(s)},createElement:()=>({setAttribute(){},remove(){}})}};context.window=context;vm.createContext(context);vm.runInContext(source,context);
 return(async()=>{const first=context.WizLoadMarkdownEnvironmentMathJax('synthetic-local');const same=context.WizLoadMarkdownEnvironmentMathJax('synthetic-local');check(first===same,'dependency load is coalesced');const script=scripts.find(s=>s.id==='wiz_markdown_environment_loader');script.onerror();try{await first;}catch(_){}
 check(timers.size===0,'failed dependency load clears deadline');check(script.onload===null&&script.onerror===null,'settled dependency releases event closures');
 const again=context.WizLoadMarkdownEnvironmentMathJax('synthetic-local');check(again!==first,'failure allows later fresh dependency attempt');scripts.at(-1).onerror();try{await again;}catch(_){}check(timers.size===0,'repeated failure leaves no timers');
 const expired=context.WizLoadMarkdownEnvironmentMathJax('synthetic-local');const late=scripts.at(-1);[...timers.values()][0]();try{await expired;}catch(_){}check(timers.size===0&&late.onload===null&&late.onerror===null,'dependency timeout clears deadline and handlers');
 const success=context.WizLoadMarkdownEnvironmentMathJax('synthetic-local');const loaded=scripts.at(-1);context.MathJax={Hub:{Queue:fn=>fn()}};loaded.onload();await success;check(timers.size===0&&loaded.onload===null&&loaded.onerror===null&&context.WizMarkdownEnvironmentLoader===null,'successful dependency completion releases timer handlers and global request');})();
}
function transform(code,raw,transient=raw){const c={console,Map,Uint8Array,WizEditor:{getMarkdownSrc:()=>raw}};vm.createContext(c);vm.runInContext(code,c);const options=c.WizConfigureMarkdownEnvironments({reader:{}});return options.reader.markdownPlugIn.customBlockGamut(transient);}
function lookupChecks(){if(!baseline)return null;const before=fs.readFileSync(baseline,'utf8');
 const samples=['\\begin{align}\na&=b\\\\\nc&=d\n\\end{align}', '\\begin{equation*}\na_b+c\n\\end{equation*}', '```tex\n\\begin{equation}\nx\n\\end{equation}\n```', '> \\begin{equation}\n> A\n> \\end{equation}'];
 const many=Array.from({length:1800},(_,i)=>'\\begin{equation}\nx_{'+i+'}+y_{'+i+'}=z_{'+i+'}\n\\end{equation}').join('\n\n');samples.push(many);
 for(const raw of samples){check(transform(before,raw)===transform(source,raw),'render transform equivalent '+raw.length);}
 const collision='\\begin{align}\na&=b\\\\\nc&=d\n\\end{align}\n\n\\begin{align}\na&=b\\\\\\\\\nc&=d\n\\end{align}';
 const transient=collision.replace('\\\\','\\\\\\\\');check(transform(before,collision,transient)===transform(source,collision,transient),'first matching original retained for transformed-row collision');
 let a=performance.now();const old=transform(before,many);const oldMs=performance.now()-a;a=performance.now();const next=transform(source,many);const newMs=performance.now()-a;assert.equal(old,next);return{blocks:1800,oldMs:+oldMs.toFixed(2),newMs:+newMs.toFixed(2),scope:'synthetic hook throughput,not whole-App speedup'};
}
(async()=>{await modeChecks();await loadCleanupChecks();const throughput=lookupChecks();const result={checks,failures:failures.length,messages:failures,throughput};console.log(JSON.stringify(result,null,2));process.exitCode=failures.length?1:0;})().catch(e=>{console.error(e);process.exitCode=1;});
