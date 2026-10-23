'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),crypto=require('crypto'),{performance}=require('perf_hooks');
const round=Number(process.argv[2]),before=process.argv[3],output=process.argv[4];
if(!before||!output||!path.isAbsolute(before)||!path.isAbsolute(output)||round<1||round>6)throw Error('round1-6, absolute before directory and output required');
const repo=path.resolve(__dirname,'../..'),names=['editorHelper.js','markdownEnvironments.js','latexTheorems.js'];
let checks=0;const failures=[];const check=(ok,name)=>{++checks;if(!ok)failures.push(name);};
function context(old){
  const c={console,Uint8Array,Map,Set,WeakMap,Promise,setTimeout,clearTimeout};c.window=c;c.raw='';
  c.WizEditor={getMarkdownSrc:()=>c.raw};c.marked={parse:s=>s};vm.createContext(c);
  for(const name of names)vm.runInContext(fs.readFileSync(old?path.join(before,name):path.join(repo,'share/files/wizeditor',name),'utf8'),c,{filename:name});
  return c;
}
const old=context(true),next=context(false);
const equal=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
function samples(){
  const rows=['','abc\n','abc','\r\n\n','😀'.repeat(8),'```tex\nx\n```\ny','~~~md\nx\n~~~','~T~T~T\nx\n~T~T~T','    code\n\tcode\nnormal','`x` ``y`` `z','<pre class="a">$x$</pre><code>y</code>','> ```\n> x\n> ```'];
  let state=731;const alphabet=['a',' ','\n','\r','\t','\\','`','~','~T','$','{','}','(',')','[',']','中','😀'];
  for(let n=0;n<160;n++){let s='';for(let i=0;i<20+n%47;i++){state=(Math.imul(state,1664525)+1013904223)>>>0;s+=alphabet[state%alphabet.length];}rows.push(s);}
  return rows;
}
function time(fn){for(let i=0;i<2;i++)fn();const times=[];for(let i=0;i<7;i++){const t=performance.now();fn();times.push(performance.now()-t);}times.sort((a,b)=>a-b);return{medianMs:+times[3].toFixed(3),samplesMs:times.map(n=>+n.toFixed(3))};}
function measure(c,source,invoke,kind){
  c.__length=source.length;c.__measure={splitElements:0,tailSliceCharacters:0,allSliceCharacters:0,arrayFromElements:0,spanPredicates:0};
  vm.runInContext(`(function(){
    var split=String.prototype.split,slice=String.prototype.slice,from=Array.from,some=Array.prototype.some;
    String.prototype.split=function(){var r=split.apply(this,arguments);if(this.length===__length)__measure.splitElements+=r.length;return r;};
    String.prototype.slice=function(a,b){var r=slice.apply(this,arguments);__measure.allSliceCharacters+=r.length;if(b===undefined&&this.length===__length)__measure.tailSliceCharacters+=r.length;return r;};
    Array.from=function(v){var r=from.apply(this,arguments);if(typeof v==='string')__measure.arrayFromElements+=r.length;return r;};
    Array.prototype.some=function(fn,receiver){return some.call(this,function(v,i,a){if(v&&typeof v.start==='number'&&typeof v.end==='number')++__measure.spanPredicates;return fn.call(receiver,v,i,a);});};
    __restore=function(){String.prototype.split=split;String.prototype.slice=slice;Array.from=from;Array.prototype.some=some;};
  })()`,c);
  try{invoke();return{kind,...c.__measure};}finally{c.__restore();}
}
let fixture,runOld,runNext,metrics;
if(round===1){
  for(const s of samples())check(equal(Array.from(old.WizMarkdownEnvironmentCodePositions(s)),Array.from(next.WizMarkdownEnvironmentCodePositions(s))),'code protection bytes '+s.length);
  fixture=Array.from({length:18000},(_,i)=>i%9===0?'```tex\nx_i\n```':'prose '+i).join('\n');
  runOld=()=>old.WizMarkdownEnvironmentCodePositions(fixture);runNext=()=>next.WizMarkdownEnvironmentCodePositions(fixture);
  metrics={before:measure(old,fixture,runOld,'line-mask allocations'),after:measure(next,fixture,runNext,'line-mask allocations')};
  check(metrics.after.splitElements===0&&metrics.before.splitElements>18000,'line mask no longer materializes whole line array');
}else if(round===2){
  for(const s of samples().concat(['\\begin{equation}a\\end{equation}','\\(a{b}\\) + \\ref{x}','$x\\$y$ \\ref{x}','\\begin{x}\\begin{y}a\\end{y}\\end{x}']))check(equal(Array.from(old.WizMathProtectedPositions(s)),Array.from(next.WizMathProtectedPositions(s))),'math protection bytes '+s.length);
  fixture=('\\alpha+\\beta+\\gamma+\\ref{eq:x} $x$ ordinary\n').repeat(4500);
  runOld=()=>old.WizMathProtectedPositions(fixture);runNext=()=>next.WizMathProtectedPositions(fixture);
  metrics={before:measure(old,fixture,runOld,'suffix slice characters, not retained heap bytes'),after:measure(next,fixture,runNext,'suffix slice characters, not retained heap bytes')};
  check(metrics.after.tailSliceCharacters===0&&metrics.before.tailSliceCharacters>fixture.length*100,'math scanner does not slice the remaining note at every command');
}else if(round===3){
  for(const s of samples().concat(['a😀中\uD800<&>\n','\\begin{align}\nx&=y\\\\\nz&=w\n\\end{align}']))check(old.WizLatexTheoremEntities(s)===next.WizLatexTheoremEntities(s),'same code point entities '+s.length);
  fixture=('a😀中\\ref{eq:x} &<>\n').repeat(3500);
  runOld=()=>old.WizLatexTheoremEntities(fixture);runNext=()=>next.WizLatexTheoremEntities(fixture);
  metrics={before:measure(old,fixture,runOld,'eager code point array elements'),after:measure(next,fixture,runNext,'eager code point array elements')};
  check(metrics.after.arrayFromElements===0&&metrics.before.arrayFromElements>10000,'entity encoder eliminates eager Array.from/map payload arrays');
  check(next.WizMarkdownEnvironmentEntities(fixture)===runOld(),'shared Markdown encoder matches theorem encoder');
}else if(round===6){
  const entries=[];let offset=0;
  for(let i=0;i<10000;i++){const node={length:i%19,id:i};entries.push({node,offset});offset+=node.length+1;}
  const linear=value=>{const e=entries.find(e=>value>=e.offset&&value<=e.offset+e.node.length);return e&&{node:e.node,offset:value-e.offset};};
  for(let i=-1;i<offset+2;i+=Math.max(1,Math.floor(offset/220)))check(equal(linear(i),next.WizLatexTheoremTextPosition(entries,i)),'ordered text offset same '+i);
  for(const e of entries.slice(0,24))for(const i of [e.offset-1,e.offset,e.offset+e.node.length,e.offset+e.node.length+1])check(equal(linear(i),next.WizLatexTheoremTextPosition(entries,i)),'empty node/newline/end boundary '+i);
  check(next.WizLatexTheoremTextPosition([],0)===undefined,'empty text projection');
  const probes=Array.from({length:2400},(_,i)=>(i*7919)%offset);
  runOld=()=>probes.map(linear);runNext=()=>probes.map(i=>next.WizLatexTheoremTextPosition(entries,i));
  let comparisons=0;for(const i of probes)entries.find(e=>{++comparisons;return i>=e.offset&&i<=e.offset+e.node.length;});
  metrics={entries:entries.length,lookups:probes.length,beforeLinearComparisons:comparisons,afterBinaryComparisonUpperBound:Math.ceil(Math.log2(entries.length+1))*probes.length};
  check(equal(runOld(),runNext()),'many DOM offsets preserve text-node identity');
}else{
  const cases=['plain text','中文 $x$ 😀','`$code$` and $x$','\\eqref{eq:x}', '$$a+b=c$$\n\n$y$','\\begin{equation}\nx=y\n\\end{equation}', '\\begin{theorem}\n$x$\n\\end{theorem}', '\\begin{lemma*}[Name]\nA\n\\end{lemma*}', '\\begin{example}\n\\begin{equation}\nx=y\n\\end{equation}\n\\end{example}', '```tex\n$x$ \\eqref{eq:x}\n```', '\\$literal\\$ and \\(x\\) and \\[y\\]'];
  for(let i=0;i<24;i++)cases.push(('normal $x_'+i+'$ \\eqref{eq:x}\n\n').repeat(i+1));
  for(const s of cases){old.raw=next.raw=s;check(old.WizLatexTheoremBody(s,0)===next.WizLatexTheoremBody(s,0),'theorem body byte equivalence '+s.length);}
  fixture=Array.from({length:700},(_,i)=>'text $x_'+i+'$\n\n\\begin{equation}\ny_'+i+'=x_'+i+'\n\\end{equation}\n\\eqref{eq:x}').join('\n\n');old.raw=next.raw=fixture;
  runOld=()=>old.WizLatexTheoremBody(fixture,0);runNext=()=>next.WizLatexTheoremBody(fixture,0);
  check(runOld()===runNext(),'large theorem projection exact bytes');
  metrics={before:measure(old,fixture,runOld,round===4?'substring characters handled, not physical copies':'linear overlap predicate calls'),after:measure(next,fixture,runNext,round===4?'substring characters handled, not physical copies':'linear overlap predicate calls')};
  if(round===4)check(metrics.after.allSliceCharacters<metrics.before.allSliceCharacters/8,'replacement assembly avoids repeated growing source prefix/suffix slicing');
  else {
    check(metrics.before.spanPredicates>100000&&metrics.after.spanPredicates===0,'span overlap no longer scans each preceding span');
    for(const spans of [[],[{start:3,end:7}],[{start:0,end:1},{start:3,end:7},{start:9,end:11}]])for(let a=0;a<13;a++)for(let z=a+1;z<14;z++)check(next.WizLatexTheoremOverlaps(spans,a,z)===spans.some(s=>a<s.end&&z>s.start),'binary overlap exact interval boundaries');
  }
}
const timing={before:time(runOld),after:time(runNext)};
check(equal(Array.from(runOld()),Array.from(runNext())),'measured operation same final output');
const result={round,accepted:!failures.length,checks,failures,metrics,timing,scope:'synthetic isolated owned extension; no whole-App speed or resident-memory claim',beforeHashes:Object.fromEntries(names.map(n=>[n,crypto.createHash('sha256').update(fs.readFileSync(path.join(before,n))).digest('hex')])),afterHashes:Object.fromEntries(names.map(n=>[n,crypto.createHash('sha256').update(fs.readFileSync(path.join(repo,'share/files/wizeditor',n))).digest('hex')]))};
fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({round,accepted:result.accepted,checks,failures,timing,metrics}));if(failures.length)process.exitCode=1;
