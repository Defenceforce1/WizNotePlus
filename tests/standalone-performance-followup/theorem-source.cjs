'use strict';
// Owned theorem projection only, with real DOM/marked and a synthetic public getter.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),{chromium}=require('playwright');
const before=process.argv[2],output=process.argv[3],mode=process.argv[4],repo=path.resolve(__dirname,'../..');
if(!path.isAbsolute(before||'')||!path.isAbsolute(output||'')||fs.existsSync(output)||!['prototype','product'].includes(mode))throw Error('absolute before/new result and prototype|product');
const old=fs.readFileSync(before+'/latexTheorems.js','utf8');
let proposed=old;for(const[a,b]of [
 ['function WizLatexTheoremBody(source, depth) {','function WizLatexTheoremBody(source, depth, readSource) {'],
 ['WizLatexTheoremHtml(record, depth + 1)','WizLatexTheoremHtml(record, depth + 1, readSource)'],
 ['WizProtectMarkdownEquationReferences(source);','WizProtectMarkdownEquationReferences(source, readSource);'],
 ['function WizLatexTheoremHtml(record, depth) {','function WizLatexTheoremHtml(record, depth, readSource) {'],
 ['WizLatexTheoremBody(record.body, depth || 0)','WizLatexTheoremBody(record.body, depth || 0, readSource)'],
 ['  var replacements = records.map(function (record) { return WizLatexTheoremHtml(record, 0); });','  // Reuse only the authoritative source already supplied to this synchronous\n  // projection tree. Nothing is retained across renders or editor callbacks.\n  function originalSource() { return source; }\n  var replacements = records.map(function (record) { return WizLatexTheoremHtml(record, 0, originalSource); });']
]){if(proposed.split(a).length!==2)throw Error('one expected owned transform');proposed=proposed.replace(a,b);}
const next=mode==='prototype'?proposed:fs.readFileSync(repo+'/share/files/wizeditor/latexTheorems.js','utf8');
if(next!==proposed)throw Error('product must exactly match the proven local prototype');
const hash=s=>crypto.createHash('sha256').update(s).digest('hex');
const isolated=JSON.parse(fs.readFileSync(path.dirname(before)+'/preflight.json')).base;
if(!path.isAbsolute(isolated)||!fs.existsSync(isolated+'/runtime'))throw Error('fresh isolated runtime required');
process.env.TMPDIR=isolated+'/runtime/';
(async()=>{
 const browser=await chromium.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:true,chromiumSandbox:true,env:{...process.env,HOME:isolated+'/home'}});
 const errors=[],requests=[],rows=[],failures=[];let checks=0;const check=(v,m)=>{checks++;if(!v)failures.push(m);};
 try{
  const context=await browser.newContext({serviceWorkers:'block'});await context.route('**/*',r=>{requests.push(r.request().url());return r.abort();});
  for(const previous of [true,false]){
   const page=await context.newPage();page.on('pageerror',e=>errors.push(e.message));await page.setContent('<!doctype html><meta charset="utf-8"><body></body>');
   await page.addScriptTag({content:fs.readFileSync(repo+'/share/files/wizeditor/dependency/marked/marked.umd.js','utf8')});
   for(const name of ['editorHelper.js','markdownEnvironments.js','latexTheorems.js'])await page.addScriptTag({content:name==='latexTheorems.js'?(previous?old:next):fs.readFileSync(before+'/'+name,'utf8')});
   const data=await page.evaluate(previous=>{
    const assertions=[],assert=(v,m)=>{assertions.push(m);if(!v)throw Error(m);};
    let raw='',reads=0,throwRead=false;window.WizEditor={getMarkdownSrc(){reads++;if(throwRead)throw Error('synthetic getter failure');return raw;}};
    const eq=(key='eq:a')=>'\\begin{equation}\\label{'+key+'}\nx=y\\\\\nu=v\n\\end{equation}\n';
    const th=(body,name='theorem',suffix='')=>'\\begin{'+name+'}'+suffix+'\n'+body+'\n\\end{'+name+'}\n';
    const escaped=s=>s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
    const html=s=>s.split('\n').map(l=>'<p>'+escaped(l)+'</p>').join('');
    const samples=['','Plain prose',th('Plain theorem'),eq()+th('See \\ref{eq:a}.'),eq()+th('See \\eqref{eq:a}.'),eq()+th('Unknown \\ref{missing}.'),th('Theorem \\ref{th:a}.','theorem','\\label{th:a}'),eq()+th('Outer \\ref{eq:a}.\n'+th('Nested \\ref{eq:a}.','proof')),eq()+th('A \\ref{eq:a}.','proposition')+th('B \\ref{eq:a}.','corollary*'),eq()+th('`\\ref{eq:a}` and \\ref{eq:a}.'),eq()+th('```tex\n\\ref{eq:a}\n```'),eq()+th('中😀 \\ref{eq:a}.').replace(/\n/g,'\r\n'),eq()+th('$\\ref{eq:a}$ and \\ref{eq:a}.'),th('Unknown code label \\ref{code}.')+'\n```tex\n'+eq('code')+'```',eq()+'\\begin{theorem}\nunterminated',eq()+'\\begin{theorem}\nwrong\n\\end{lemma}'];
    const results=samples.map((s,i)=>{raw=s;reads=0;const rendered=WizRenderLatexTheorems(html(s),s);const count=reads;if(!previous)assert(count===0,'provided source needs no getter sample '+i);return{rendered,reads:count};});
    const workload=eq()+th('Outer \\ref{eq:a}.\n'+th('Nested \\ref{eq:a}.','proof'))+th('Sibling \\ref{eq:a}.','corollary');raw=workload;reads=0;
    const options=WizConfigureMarkdownEnvironments({}),body=options.reader.callback.beforeMarkdownRender(html(workload)),callbackReads=reads;
    assert(callbackReads===(previous?4:1),'three theorem body lookups reuse supplied source');
    const fresh=[];for(let i=0;i<8;i++){raw=eq('eq:'+i)+th('Current \\ref{eq:'+i+'}, stale \\ref{eq:'+(i-1)+'}.');reads=0;const result=options.reader.callback.beforeMarkdownRender(html(raw));fresh.push(result);assert(reads===(previous?2:1),'new source is read once per top-level callback '+i);assert(result.includes('wiz-latex-math')&&result.includes('stale'),'fresh result is rendered '+i);}
    raw='stale';reads=0;const changed=eq('eq:fresh')+th('Updated \\ref{eq:fresh}.');
    const custom=WizConfigureMarkdownEnvironments({reader:{callback:{beforeMarkdownRender(){raw=changed;return html(changed);}}}}).reader.callback.beforeMarkdownRender;
    const customHtml=custom('ignored');assert(reads===(previous?2:1)&&customHtml.includes('wiz-latex-theorem'),'original callback runs before source capture');
    raw=workload;reads=0;const direct=WizLatexTheoremBody('See \\ref{eq:a}.',0);assert(reads===1,'direct body API without a provider retains fallback');
    reads=0;const record=WizLatexTheoremRecords(th('See \\ref{eq:a}.'))[0],directHtml=WizLatexTheoremHtml(record,0);assert(reads===1,'direct HTML helper retains fallback');
    const parser=window.marked;window.marked={parse(){throw Error('synthetic parser failure');}};let fallback;
    try{fallback=options.reader.callback.beforeMarkdownRender(html(workload));}finally{window.marked=parser;}
    assert(fallback===html(workload),'parse failure retains complete original preview');
    throwRead=true;fallback=options.reader.callback.beforeMarkdownRender(html(workload));throwRead=false;assert(fallback===html(workload),'top-level getter failure retains preview');
    raw=changed;reads=0;const recovered=options.reader.callback.beforeMarkdownRender(html(changed));assert(recovered.includes('wiz-latex-theorem')&&reads===(previous?2:1),'error does not retain old snapshot');
    return{assertions,results,body,callbackReads,fresh,customHtml,direct,directHtml,recovered};
   },previous);
   checks+=data.assertions.length;rows.push({variant:previous?'before':'after',...data});await page.close();
  }
  const[a,b]=rows;for(let i=0;i<a.results.length;i++)check(a.results[i].rendered===b.results[i].rendered,'exact theorem projection '+i);
  for(const k of ['body','fresh','customHtml','direct','directHtml','recovered'])check(JSON.stringify(a[k])===JSON.stringify(b[k]),'same byte output '+k);
  check(a.callbackReads===4&&b.callbackReads===1,'only redundant body reads removed');check(!errors.length&&!requests.length,'no page errors or external requests');
  const report={accepted:!failures.length,mode,checks,failures,sourceHashes:{before:hash(old),after:hash(next)},topLevelCallbackGetterCalls:{before:a.callbackReads,after:b.callbackReads},fixtures:a.results.length,sourceLifetime:'already supplied source within one synchronous theorem projection tree only',noCrossCallbackOrNoteCache:true,pageErrors:errors,externalRequests:requests,rows:rows.map(r=>({variant:r.variant,reads:r.results.map(f=>f.reads),callbackReads:r.callbackReads,outputSha256:hash(JSON.stringify([r.results.map(f=>f.rendered),r.body,r.fresh,r.customHtml,r.direct,r.directHtml,r.recovered]))})),fullClientAcceptance:false};
  fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify(report));if(failures.length)process.exitCode=1;
 }finally{await browser.close();}
})().catch(e=>{console.error(e);process.exitCode=1;});
