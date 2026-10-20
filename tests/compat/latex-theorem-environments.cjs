#!/usr/bin/env node
'use strict';
// Original editor and local licensed parser; synthetic notes, all remote URLs blocked.
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),crypto=require('node:crypto');
const {pathToFileURL}=require('node:url'),{chromium}=require('playwright');
const root=path.resolve(__dirname,'../..'),assets=path.join(root,'share/files/wizeditor'),output=process.argv[2];
if(!output||!path.isAbsolute(output)||fs.existsSync(output))throw Error('New absolute report path required');
const selected=process.env.WIZ_THEOREM_CASES?new Set(process.env.WIZ_THEOREM_CASES.split(',')):null;
const env=(name,body='A **bold** statement.',title='',label='')=>'\\begin{'+name+'}'+(title?'['+title+']':'')+(label?'\\label{'+label+'}':'')+'\n'+body+'\n\\end{'+name+'}';
const kinds=['theorem','lemma','proposition','corollary','definition','example','remark','proof'];
const aligned='\\begin{align}\na_1&=b_1+c_1\\\\\na_2&=b_2+c_2\\label{eq:thm-align}\n\\end{align}';
const cases=kinds.flatMap(kind=>[
  {name:kind,source:env(kind,'A **bold** statement.','Optional title','test:'+kind),blocks:1,math:0,numbers:[/^(remark|proof)$/.test(kind)?'':'1'],refs:0},
  {name:kind+'-star',source:env(kind+'*'),blocks:1,math:0,numbers:[''],refs:0}
]);
cases.push(
  {name:'bundled-example',source:fs.readFileSync(path.join(root,'share/files/examples/latex-theorem-environments.md'),'utf8').trimEnd(),
    blocks:8,math:8,numbers:['1','1','2','','3','','1',''],refs:2},
  {name:'all-kinds',source:kinds.map(k=>env(k)).join('\n\n'),blocks:8,math:0,numbers:['1','2','3','4','1','1','',''],refs:0},
  {name:'forward-reference',source:'See \\ref{thm:a}.\n\n'+env('theorem','A statement.','Title','thm:a'),blocks:1,math:0,numbers:['1'],refs:1},
  {name:'leading-body-label',source:'See \\ref{thm:a}.\n\n'+env('proposition','\\label{thm:a}\nA statement.'),blocks:1,math:0,numbers:['1'],refs:1},
  {name:'duplicate-label',source:'See \\ref{same}.\n\n'+env('theorem','First.','','same')+'\n\n'+env('corollary','Second.','','same'),blocks:2,math:0,numbers:['1','2'],refs:0},
  {name:'inline-and-display-math',source:env('theorem','For $x\\in\\mathbb{R}$,\n\n$$x^2\\ge0$$','Nonnegative','thm:square'),blocks:1,math:2,numbers:['1'],refs:0},
  {name:'bare-math-in-theorem',source:env('proposition','\\begin{equation}\\label{eq:inside}\nx=1\n\\end{equation}'),blocks:1,math:1,numbers:['1'],refs:0},
  {name:'bare-align-and-reference',source:env('proposition',aligned+'\n\n\\eqref{eq:thm-align}'),blocks:1,math:2,numbers:['1'],refs:0,exactMath:aligned},
  {name:'parenthesized-math',source:env('theorem','For \\(x\\in\\mathbb{R}\\), the statement holds.'),blocks:1,math:1,numbers:['1'],refs:0},
  {name:'standalone-math-reference',source:'See $\\ref{thm:inline}$.\n\n'+env('theorem','For $x$, the statement holds.','','thm:inline'),blocks:1,math:1,numbers:['1'],refs:1},
  {name:'nested-proof',source:env('theorem','A statement.\n\n'+env('proof','By definition.')),blocks:2,math:0,numbers:['1',''],refs:0},
  {name:'nested-lemma',source:env('theorem','A statement.\n\n'+env('lemma','A smaller statement.')),blocks:2,math:0,numbers:['1','2'],refs:0},
  {name:'star-does-not-increment',source:env('theorem*')+'\n\n'+env('proposition'),blocks:2,math:0,numbers:['','1'],refs:0},
  {name:'markdown-body',source:env('definition','**Bold** and *italic*.\n\n- One\n- Two\n\n| A | B |\n|---|---|\n| 1 | 2 |\n\n`\\begin{theorem}`'),blocks:1,math:0,numbers:['1'],refs:0,bodyMarkup:true},
  {name:'code-fence',source:'```latex\n'+env('theorem')+'\n```',blocks:0,math:0,numbers:[],refs:0},
  {name:'inline-code',source:'`\\begin{theorem}` and `\\end{theorem}`',blocks:0,math:0,numbers:[],refs:0},
  {name:'indented-code',source:env('theorem').split('\n').map(l=>'    '+l).join('\n'),blocks:0,math:0,numbers:[],refs:0},
  {name:'mixed-code-real',source:'```latex\n'+env('theorem')+'\n```\n\n'+env('lemma'),blocks:1,math:0,numbers:['1'],refs:0},
  {name:'unfinished',source:'\\begin{theorem}\nKeep all of this text.',blocks:0,math:0,numbers:[],refs:0},
  {name:'mismatched',source:'\\begin{theorem}\nKeep this.\n\\end{lemma}',blocks:0,math:0,numbers:[],refs:0},
  {name:'title-special-text',source:env('theorem','A statement.','名称 [子项] <A&B> "quoted"'),blocks:1,math:0,numbers:['1'],refs:0},
  {name:'adjacent-prose',source:'Before the environment\n'+env('theorem','A statement.')+'\nAfter the environment',blocks:1,math:0,numbers:['1'],refs:0,prose:true},
  {name:'legacy-opt-in',source:'> **定理** \\label{old}\n>\n> Legacy.\n\n'+env('corollary','See \\ref{old}.')+'\n\n'+env('definition'),blocks:2,allBlocks:3,math:0,numbers:['1','2','1'],refs:1,preset:true},
  {name:'legacy-no-preset',source:'> **定理**\n>\n> Ordinary quote.\n\n'+env('corollary'),blocks:1,math:0,numbers:['1'],refs:0},
  {name:'unknown-environment',source:'\\begin{customtheorem}\nNot configured.\n\\end{customtheorem}',blocks:0,math:0,numbers:[],refs:0}
);
const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'wiz-latex-theorems-'));
const json=s=>JSON.stringify(s).replace(/</g,'\\u003c'),escape=s=>s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
const report={schema:1,remoteRequests:[],pageErrors:[],cases:[]};
(async()=>{
  const browser=await chromium.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:true,chromiumSandbox:true,args:['--allow-file-access-from-files']});
  try {
    for(const item of cases) {
      if(selected&&!selected.has(item.name))continue;
      const source=item.source+'\n',context=await browser.newContext({serviceWorkers:'block'});
      await context.route('**/*',route=>{
        if(/^(file|data|about|blob):/.test(route.request().url()))return route.continue();
        report.remoteRequests.push(route.request().url());return route.abort();
      });
      const page=await context.newPage();page.on('pageerror',e=>report.pageErrors.push({name:item.name,error:e.message}));
      const scripts=['wizEditorForMac.js','dependency/marked/marked.umd.js','latexTheorems.js','markdownEnvironments.js','editorHelper.js'].map(file=>
        '<script name="wiz_inner_script" wiz_style="unsave" src="'+escape(pathToFileURL(path.join(assets,file)).href)+'"></script>').join('');
      const html='<!doctype html><meta charset="utf-8"><body>'+escape(source).replace(/\n/g,'<br>')+scripts+
        '<script name="wiz_inner_script" wiz_style="unsave">window.FixtureSource='+json(source)+';window.WizNoteCss='+json(item.preset?'/* WizNotePlus scientific math environments v1 */':'')+';'+
        'WizInitMarkdownEditor({document:document,lang:"zh-cn",clientType:"web",noteType:"common",dependencyUrl:'+json(pathToFileURL(path.join(assets,'dependency')).href)+',reader:{type:"common"}});'+
        'WizEditor.off({reader:{type:"markdown"}},()=>{window.FixtureReady=true;});</script></body>';
      const file=path.join(temporary,item.name+'.html');fs.writeFileSync(file,html,{flag:'wx'});
      await page.goto(pathToFileURL(file).href);await page.waitForFunction(()=>window.FixtureReady,null,{timeout:35000});
      const snapshot=()=>page.evaluate(()=>({
        blocks:document.querySelectorAll('.wiz-latex-theorem').length,
        numbers:Array.from(document.querySelectorAll('.wiz-math-block')).map(n=>n.getAttribute('data-wiz-math-number')),
        refs:document.querySelectorAll('.wiz-math-reference').length,
        referencesResolve:Array.from(document.querySelectorAll('.wiz-math-reference')).every(a=>!!document.getElementById(a.getAttribute('href').slice(1))),
        jax:window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().length:0,
        mathTexts:window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().map(j=>j.originalText):[],
        errors:document.querySelectorAll('.MathJax_Error,merror,.noError').length,
        sourceSame:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource,
        readOnlyHtmlContainsProjection:WizEditor.getContentHtml().includes('wiz-latex-theorem'),
        html:document.body.innerHTML,text:document.body.innerText,
        bodyMarkup:!!document.querySelector('.wiz-latex-theorem em')&&!!document.querySelector('.wiz-latex-theorem ul li')&&!!document.querySelector('.wiz-latex-theorem table')
      }));
      const initial=await snapshot(),cycles=[];
      for(let i=0;i<2;++i){
        await page.evaluate(()=>new Promise(resolve=>{WizPrepareNoteModeChange();WizEditor.on(null,resolve);}));
        const edit=await page.evaluate(()=>WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource
          &&!document.querySelector('.wiz-latex-theorem')&&!WizEditor.getContentHtml().includes('wiz-latex-theorem')
          &&!WizEditor.getContentHtml().includes('data-wiz-math-'));
        await page.evaluate(()=>new Promise(resolve=>{WizPrepareNoteModeChange();WizEditor.off({reader:{type:'markdown'}},resolve);}));
        const state=await snapshot();cycles.push({editingSame:edit,...state});
      }
      const check=s=>s.blocks===item.blocks&&JSON.stringify(s.numbers)===JSON.stringify(item.numbers)
        &&s.refs===item.refs&&s.referencesResolve&&s.jax===item.math&&!s.errors&&s.sourceSame
        &&(!item.exactMath||s.mathTexts.some(tex=>tex.trim()===item.exactMath))
        &&(!item.bodyMarkup||s.bodyMarkup)&&(!item.prose||s.text.includes('Before the environment')&&s.text.includes('After the environment'));
      const ok=check(initial)&&cycles.every(s=>s.editingSame&&check(s));
      report.cases.push({name:item.name,expected:item,initial,cycles,ok});
      console.log(JSON.stringify({name:item.name,ok,blocks:initial.blocks,numbers:initial.numbers,jax:initial.jax,errors:initial.errors,sourceSame:initial.sourceSame}));
      await context.close();
    }
    report.failures=report.cases.filter(c=>!c.ok).map(c=>c.name);
    report.hashes=Object.fromEntries(['latexTheorems.js','markdownEnvironments.js','editorHelper.js','dependency/marked/marked.umd.js'].map(file=>[file,crypto.createHash('sha256').update(fs.readFileSync(path.join(assets,file))).digest('hex')]));
    report.fixtureDirectory=temporary;fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n',{flag:'wx'});
    process.exitCode=report.failures.length||report.pageErrors.length||report.remoteRequests.length?1:0;
  } finally {await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
