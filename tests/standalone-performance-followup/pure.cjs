'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),crypto=require('crypto'),{performance}=require('perf_hooks');
const round=Number(process.argv[2]),before=process.argv[3],output=process.argv[4];
if(round<1||round>6||!path.isAbsolute(before||'')||!path.isAbsolute(output||''))throw Error('round1-6, absolute before/output');
const repo=path.resolve(__dirname,'../..'),names=['editorHelper.js','markdownEnvironments.js','latexTheorems.js'];
let checks=0;const failures=[];function check(v,m){checks++;if(!v)failures.push(m);}
const eq=(a,b)=>JSON.stringify(a)===JSON.stringify(b);
function context(previous){
  const c={console,setTimeout,clearTimeout};c.window=c;c.raw='';c.count={};
  c.WizEditor={getMarkdownSrc(){c.count.raw=(c.count.raw||0)+1;return c.raw;}};c.marked={parse:s=>s};vm.createContext(c);
  for(const name of names)vm.runInContext(fs.readFileSync(previous?path.join(before,name):repo+'/share/files/wizeditor/'+name,'utf8'),c,{filename:name});
  for(const name of ['WizMarkdownEnvironmentCodePositions','WizMathProtectedPositions','WizMarkdownEnvironmentBlocks']){
    const fn=c[name];c[name]=function(){c.count[name]=(c.count[name]||0)+1;return fn.apply(this,arguments);};
  }
  return c;
}
const old=context(true),next=context(false),samples=['','plain\nMarkdown\n','中😀\r\n','\\begin{equation}\nx=y\n\\end{equation}',
  '\\begin{theorem}\nBody\n\\end{theorem}','\\begin{lemma*}[A {B}]\\label{th:x}\n$x$\n\\end{lemma*}',
  '\\begin{example}\n\\begin{equation}\\label{eq:a}\nx=y\n\\end{equation}\n\\end{example}',
  '```tex\n\\begin{theorem}\nA\n\\end{theorem}\n```', '    \\begin{theorem}\nA\n    \\end{theorem}',
  '`\\ref{eq:a}` <pre>\\eqref{eq:a}</pre>', '$\\ref{eq:a}$ and \\eqref{eq:a}',
  '\\begin{theorem}\n\\begin{lemma}\nA\n\\end{lemma}\n\\end{theorem}',
  '\\begin{theorem}\nA\n\\end{lemma}', '\\begin{theorem*}\nA\n\\end{theorem*}',
  '> \\begin{align}\n> a&=b\\\\\n> c&=d\n> \\end{align}', '\\begin{align}\na&=b\\\\\\\\\nc&=d\n\\end{align}',
  'normal $x$ \\(y\\) \\[z\\] $$w$$', '\\$literal\\$ **Markdown**', '\\eqref{eq:a} and \\ref{eq:a} \\ref{th:x}',
  '\\begin{equation}\nx=y\n\\end{equation}\n\\ref{eq:a}', '\\begin{theorem*}inline\\end{theorem*}'];
for(let i=0;i<28;i++)samples.push(('plain '+i+' 😀 **text**\n\n').repeat(i+1));
function invoke(c,s){
  if(round===1)return c.WizProtectMarkdownEquationReferences(s);
  if(round===2)return c.WizLatexTheoremRecords(s);
  if(round===3||round===4)return c.WizLatexTheoremBody(s,0);
  return c.WizConfigureMarkdownEnvironments({}).reader.markdownPlugIn.customBlockGamut(s);
}
const raw='\\begin{equation}\\label{eq:a}\nx=y\n\\end{equation}\n```tex\n\\begin{equation}\\label{eq:code}\nx\n\\end{equation}\n```';
for(const s of samples){old.raw=next.raw=raw+'\n'+s;check(eq(invoke(old,s),invoke(next,s)),'same projection/protection '+s.length);}
for(const s of ['\\eqref{eq:a}','\\ref{eq:a}','\\ref{th:x}','`\\ref{eq:a}`','\\ref{eq:code}']){
  old.raw=next.raw=raw;check(eq(invoke(old,s),invoke(next,s)),'same known/unknown/code label '+s);
}
let fixture;
if(round===1)fixture='By \\eqref{eq:a}.\n'.repeat(1800);
if(round===2)fixture=('A plain paragraph with $x$ and **Markdown**.\n').repeat(14000);
if(round===3)fixture=('Theorem prose $x+y=z$ and $$a=b$$\n').repeat(2000);
if(round===4)fixture=('Plain theorem body **bold** with no math commands.\n').repeat(14000);
if(round===5)fixture=Array.from({length:700},(_,i)=>'\\begin{equation}\nx_'+i+'=y\n\\end{equation}').join('\n');
if(round===6)fixture='```tex\n'+Array.from({length:700},(_,i)=>'\\begin{equation}\nx_'+i+'=y\n\\end{equation}').join('\n')+'\n```';
old.raw=next.raw=fixture+'\n'+raw;
function measured(c){c.count={};const result=invoke(c,fixture);return{result,counts:{...c.count}};}
const a=measured(old),b=measured(next);check(eq(a.result,b.result),'large measured fixture exact output');
if(round===1){check(a.counts.raw===1&&!b.counts.raw,'eqref alone does not read raw source');
  next.raw=raw;next.count={};next.WizProtectMarkdownEquationReferences('\\ref{eq:a} \\ref{eq:a}');check(next.count.raw===1,'plain references share one call-local lookup');
  next.raw='';check(next.WizProtectMarkdownEquationReferences('\\ref{eq:a}')==='\\ref{eq:a}','no label cache leaks between source versions');
  next.count={};next.WizProtectMarkdownEquationReferences('`\\ref{eq:a}`');check(!next.count.raw,'code-only reference never fetches source');
}
if(round===2)check(a.counts.WizMarkdownEnvironmentCodePositions===1&&a.counts.WizMathProtectedPositions===1&&!b.counts.WizMarkdownEnvironmentCodePositions&&!b.counts.WizMathProtectedPositions,'no theorem token avoids both note-sized protection arrays');
if(round===3)check(a.counts.WizMathProtectedPositions===1&&!b.counts.WizMathProtectedPositions,'no eqref avoids theorem reference protection array');
if(round===4)check(a.counts.WizMarkdownEnvironmentCodePositions===1&&!b.counts.WizMarkdownEnvironmentCodePositions,'plain theorem prose avoids math projection code mask');
if(round===5)check(a.counts.WizMarkdownEnvironmentCodePositions-b.counts.WizMarkdownEnvironmentCodePositions===1,'unstarred display blocks do not allocate starred marker mask');
if(round===6)check(a.counts.raw===1&&!b.counts.raw&&a.counts.WizMarkdownEnvironmentBlocks===1&&!b.counts.WizMarkdownEnvironmentBlocks,'code-only environments avoid raw getter and original lookup');
function time(c){for(let i=0;i<2;i++)invoke(c,fixture);const times=[];for(let i=0;i<7;i++){const t=performance.now();invoke(c,fixture);times.push(performance.now()-t);}times.sort((a,b)=>a-b);return{medianMs:+times[3].toFixed(3),samplesMs:times.map(n=>+n.toFixed(3))};}
const hashes=previous=>Object.fromEntries(names.map(n=>[n,crypto.createHash('sha256').update(fs.readFileSync(previous?path.join(before,n):repo+'/share/files/wizeditor/'+n)).digest('hex')]));
const report={round,accepted:!failures.length,checks,failures,fixtureCharacters:fixture.length,metrics:{before:a.counts,after:b.counts},timing:{before:time(old),after:time(next)},beforeHashes:hashes(true),afterHashes:hashes(false),scope:'synthetic owned conversion; array/admission counts are not whole-App resident-memory measurements'};
fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(report));if(failures.length)process.exitCode=1;
