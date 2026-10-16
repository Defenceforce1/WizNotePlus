#!/usr/bin/env node
// Exploration only. Uses bundled editor assets in an isolated headless browser;
// this is NOT a substitute for Qt WebEngine, native input or real-account tests.
'use strict';
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
const {chromium} = require('playwright');

const root = path.resolve(__dirname, '../..');
const out = process.argv[2];
if (!out || !path.isAbsolute(out)) throw new Error('Supply an absolute exploration output directory');
fs.mkdirSync(out, {recursive: true});
const editorRoot = path.join(root, 'share/files/wizeditor');
const rawHelper=fs.readFileSync(path.join(editorRoot,'editorHelper.js'),'utf8');
let servedHelper=rawHelper;
if(process.env.WIZNOTE_EXPLORE_BOUNDARY_CONTEXT){
  servedHelper=require('./math-boundary-prototype.cjs')(servedHelper);
}
if(process.env.WIZNOTE_EXPLORE_SKIP_EMPTY){
  servedHelper=servedHelper.replace('fragment.appendChild(document.createTextNode(source.slice(offset, result.index)));',
    'if (result.index > offset) fragment.appendChild(document.createTextNode(source.slice(offset, result.index)));');
  servedHelper=servedHelper.replace('fragment.appendChild(document.createTextNode(source.slice(offset))); text.replaceWith(fragment);',
    'if (offset < source.length) fragment.appendChild(document.createTextNode(source.slice(offset))); text.replaceWith(fragment);');
  if(servedHelper===rawHelper)throw new Error('Empty-text candidate source anchor mismatch');
}
const presentationSource = fs.readFileSync(path.join(root, 'src/WizNotePresentation.cpp'), 'utf8');
const scientificCss = presentationSource.split('QString scientificMathStyle()')[1].split('R"CSS(')[1].split(')CSS"')[0];
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const escape = text => text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const scenarios = new Map();
const selected = new Set((process.env.WIZNOTE_EXPLORE_SCENARIOS || '').split(',').filter(Boolean));
const modeCycles=Number(process.env.WIZNOTE_EXPLORE_MODE_CYCLES||5);
if(!Number.isInteger(modeCycles)||modeCycles<1||modeCycles>30)throw new Error('Mode cycles must be 1..30');
const outcome = {schema: 1, date: '2026-10-17', engine: 'isolated headless Chrome; not Qt',
  helperVariant:process.env.WIZNOTE_EXPLORE_BOUNDARY_CONTEXT?'in-memory DOM boundary prototype':process.env.WIZNOTE_EXPLORE_SKIP_EMPTY?'in-memory skip-empty-text prototype':'working-tree source',
  sources: {helperSha256: hash(fs.readFileSync(path.join(editorRoot, 'editorHelper.js'))),
    executedHelperSha256:hash(servedHelper),
    editorSha256: hash(fs.readFileSync(path.join(editorRoot, 'wizEditorForMac.js')))},
  checks: [], scenarios: [], blockedRequests: [], pageErrors: [], memory: []};
function check(name, ok, detail) {
  outcome.checks.push({name, ok: !!ok, ...(detail === undefined ? {} : {detail})});
  console.log(`${ok ? 'PASS' : 'FAIL'} ${name}${detail === undefined ? '' : ': ' + JSON.stringify(detail)}`);
}
function documentHtml(source) {
  return '<!doctype html><html><head><meta charset="utf-8"></head><body>'
    + escape(source).replace(/\n/g, '<br>')
    + '<script src="/assets/wizEditorForMac.js"></script><script src="/assets/editorHelper.js"></script>'
    + '<script>window.FixtureSource=' + JSON.stringify(source).replace(/</g, '\\u003c') + ';'
    + 'window.WizNoteCss=' + JSON.stringify(scientificCss).replace(/</g, '\\u003c') + ';'
    + 'window.FixtureStarted=performance.now();window.DecorationCalls=0;window.DecorationMs=0;'
    + 'var original=WizApplyMathEnvironments;WizApplyMathEnvironments=function(){var t=performance.now();'
    + 'try{return original.apply(this,arguments);}finally{DecorationCalls++;DecorationMs+=performance.now()-t;}};'
    + 'WizEditor.init({document:document,lang:"zh-cn",clientType:"web",noteType:"markdown",dependencyUrl:location.origin+"/assets/dependency",'
    + 'reader:{type:"markdown",callback:{markdown:function(){WizApplyNoteStyle();},mathJax:function(){WizApplyNoteStyle();}}}});'
    + 'WizEditor.off({reader:{type:"markdown"}},function(){var done=function(){WizApplyNoteStyle();'
    + 'requestAnimationFrame(function(){requestAnimationFrame(function(){window.FixtureMs=performance.now()-FixtureStarted;window.FixtureReady=true;});});};'
    + 'if(window.MathJax&&MathJax.Hub)MathJax.Hub.Queue(done);else done();});</script></body></html>';
}
function block(kind, id, body = 'Synthetic body 正文，不修改用户笔记。') {
  return `> **${kind}（${id}）** \\label{${id}}\n>\n> ${body}\n\n<!-- -->\n\n`;
}
const small = '# Synthetic reference note\n\n前向引用 \\ref{thm:a}，以及 $\\ref{def:b}$。\n\n'
  + block('定理', 'thm:a', 'TARGET-A. For real $x$, $x^2 \\ge 0$.')
  + block('定义', 'def:b', 'TARGET-B. $$a^2+b^2=c^2$$')
  + '```text\n\\ref{thm:a}\n\\label{code:untouched}\n```\n\n'
  + '未知引用 \\ref{missing:target}。\n';
scenarios.set('small', small);
scenarios.set('long-reference','# Long reference navigation\n\nGo to \\ref{thm:long-target}.\n\n'
  +Array.from({length:22},(_,i)=>`## Prelude ${i}\n\n`+'Synthetic text to exercise vertical scrolling. '.repeat(10)+'\n\n').join('')
  +block('定理','thm:long-target','LONG-TARGET. $x^2\\ge0$.')
  +Array.from({length:8},(_,i)=>`## Following ${i}\n\n`+'Following text. '.repeat(15)+'\n\n').join(''));
scenarios.set('navigation-lifecycle',scenarios.get('long-reference'));
scenarios.set('duplicates', '# Duplicate labels\n\n\\ref{duplicate:a}\n\n'
  + block('定理', 'duplicate:a') + block('定义', 'duplicate:a'));
scenarios.set('formula-boundaries', '# Full-reader boundary compatibility\n\n'
  + 'Ordinary reference: \\ref{thm:boundary}. Standalone $\\ref{thm:boundary}$.\n\n'
  + 'Compound inline: $x+\\ref{thm:boundary}$.\n\n'
  + '$$x+\\ref{thm:boundary}$$\n\n'
  + '$$\\ref{thm:boundary}$$\n\n'
  + '$$(\\ref{thm:boundary})+1$$\n\n'
  + block('定理','thm:boundary','Boundary target. $x^2\\ge0$.')
  + '```tex\n$\\ref{thm:boundary}$\n```\n');
scenarios.set('formula-boundary-edge', '# TeX boundary edge controls\n\n'
  + 'Prose reference \\ref{thm:edge}.\n\n'
  + '\\begin{equation}x+\\ref{thm:edge}\\end{equation}\n\n'
  + '$x+\\text{price $5}+\\ref{thm:edge}$\n\n'
  + '$$\\begin{aligned}x&=1\\\\y&=\\ref{thm:edge}\\end{aligned}$$\n\n'
  + block('定理','thm:edge','Edge target.'));
scenarios.set('math24', '# Mathematics stress fixture\n\n' + Array.from({length:24}, (_, i) =>
  `## Section ${i + 1}\n\n` + block('定理', `thm:m${i}`, `TARGET-${i}. $$\\sum_{k=1}^{n} k = \\frac{n(n+1)}{2}$$`)).join(''));
scenarios.set('math-compat', '# 数学兼容性样本\n\n'
  + '行内 $x^2+y^2$，黑板体 $\\mathbb{R}$，运算符 $\\operatorname{rank}(A)$。\n\n'
  + '$$E=mc^2\\label{eq:energy}$$\n\n公式引用 $\\eqref{eq:energy}$。\n\n'
  + '$$\\begin{aligned}a&=b+c\\\\d&=e+f\\end{aligned}$$\n\n'
  + '$$f(x)=\\begin{cases}x^2&x\\ge0\\\\-x&x<0\\end{cases}$$\n\n'
  + '$$A=\\begin{pmatrix}1&2\\\\3&4\\end{pmatrix}$$\n\n'
  + '$$\\int_0^1 x^2\\,dx=\\frac13,\\qquad\\sum_{n=1}^{\\infty}\\frac1{n^2}=\\frac{\\pi^2}{6}$$\n\n'
  + block('定义','def:compat','兼容定义中的 $\\alpha+\\beta$。')
  + '参见定义 \\ref{def:compat}。\n\n| 项目 | 值 |\n| --- | --- |\n| 公式 | $a+b$ |\n\n'
  + '```tex\n\\label{literal:code}\n\\ref{literal:code}\n\\begin{align}x&=y\\end{align}\n```\n');
scenarios.set('blocks500', '# Many environments\n\n' + Array.from({length:500}, (_, i) =>
  `See \\ref{thm:b${i}}.\n\n` + block('定理', `thm:b${i}`)).join(''));
scenarios.set('blocks1500', '# More environments\n\n' + Array.from({length:1500}, (_, i) =>
  `See \\ref{thm:b${i}}.\n\n` + block('定理', `thm:b${i}`)).join(''));
scenarios.set('prose200', '# Long prose\n\n' + Array.from({length:200}, (_, i) =>
  `## Heading ${i}\n\n` + 'Synthetic plain text 混合正文，用于验证长文渲染与原文往返。 '.repeat(8) + '\n\n').join(''));
scenarios.set('pdf', '# PDF reference fixture\n\n'
  + 'Reference A: \\ref{thm:pdf-a}; reference B: \\ref{def:pdf-b}.\n\n'
  + Array.from({length:12}, (_, i) => `## Prelude ${i}\n\n` + 'Synthetic paragraph for stable pagination. '.repeat(12) + '\n\n').join('')
  + block('定理', 'thm:pdf-a', 'PDF-TARGET-A. $$a^2+b^2=c^2$$')
  + Array.from({length:5}, (_, i) => `## Middle ${i}\n\n` + 'Middle paragraph to separate targets. '.repeat(10) + '\n\n').join('')
  + block('定义', 'def:pdf-b', 'PDF-TARGET-B. This text identifies the second destination.'));

if(process.env.WIZNOTE_NATIVE_FIXTURE_ONLY){
  const {pathToFileURL}=require('node:url');
  const assets=pathToFileURL(editorRoot+path.sep).href;
  const html=documentHtml(scenarios.get('pdf'))
    .replaceAll('src="/assets/', 'src="'+assets)
    .replace('location.origin+"/assets/dependency"',JSON.stringify(assets+'dependency'));
  fs.writeFileSync(path.join(out,'native-pdf-fixture.html'),html,{flag:'wx'});
  fs.writeFileSync(path.join(out,'native-pdf-fixture-provenance.json'),JSON.stringify({
    kind:'synthetic original-reader fixture for native Qt PDF tests',sources:outcome.sources,source:scenarios.get('pdf')},null,2)+'\n',{flag:'wx'});
  console.log('Native synthetic PDF fixture created; no browser, account or server started');
  process.exit(0);
}

const contentTypes = {'.js':'text/javascript', '.css':'text/css', '.woff':'font/woff', '.woff2':'font/woff2',
  '.ttf':'font/ttf', '.svg':'image/svg+xml', '.png':'image/png', '.html':'text/html'};
const server = http.createServer((req, res) => {
  try {
    const url = new URL(req.url, 'http://127.0.0.1');
    if (url.pathname.startsWith('/fixture/')) {
      const source = scenarios.get(url.pathname.slice('/fixture/'.length));
      if (source === undefined) { res.writeHead(404).end(); return; }
      res.setHeader('Content-Type', 'text/html;charset=utf-8'); res.end(documentHtml(source)); return;
    }
    if (!url.pathname.startsWith('/assets/')) { res.writeHead(404).end(); return; }
    if(url.pathname==='/assets/editorHelper.js'){res.setHeader('Content-Type','text/javascript');res.end(servedHelper);return;}
    const file = fs.realpathSync(path.join(editorRoot, decodeURIComponent(url.pathname.slice('/assets/'.length))));
    if (!file.startsWith(editorRoot + path.sep) || !fs.statSync(file).isFile()) { res.writeHead(403).end(); return; }
    res.setHeader('Content-Type', contentTypes[path.extname(file)] || 'application/octet-stream');
    res.end(fs.readFileSync(file));
  } catch { res.writeHead(404).end(); }
});

(async () => {
  await new Promise(resolve => server.listen(0, '127.0.0.1', resolve));
  const origin = `http://127.0.0.1:${server.address().port}`;
  let browser;
  try {
    browser = await chromium.launch({executablePath: '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
      headless: true, chromiumSandbox: true, args:['--disable-background-networking', '--disable-component-update']});
    outcome.browserVersion = browser.version();
    const context = await browser.newContext({viewport:{width:850,height:660}, serviceWorkers:'block'});
    await context.route('**/*', route => {
      if (route.request().url().startsWith(origin + '/')) return route.continue();
      outcome.blockedRequests.push(route.request().url()); return route.abort();
    });
    context.on('page', page => page.on('pageerror', error => outcome.pageErrors.push({message:error.message,stack:error.stack,url:page.url(),phase:outcome.activePhase})));
    for (const [name, source] of scenarios) {
      if(selected.size&&!selected.has(name))continue;
      const page = await context.newPage();
      outcome.activePhase=name+':load';
      console.log(`SCENARIO ${name}`);
      await page.goto(`${origin}/fixture/${name}`);
      await page.waitForFunction(() => window.FixtureReady === true, null, {timeout:60000});
      const initial = await page.evaluate(() => {
        const t = performance.now();
        const markdown = WizEditor.getMarkdownSrc({unEscapeHtml:true});
        const serialized = WizEditor.getContentHtml();
        return {renderMs:FixtureMs,decorationCalls:DecorationCalls,decorationMs:DecorationMs,
          serializationMs:performance.now()-t,markdownEqual:markdown===FixtureSource,
          htmlContainsDecoration:/wiz-math-block|data-wiz-math-heading|wiz-math-reference/.test(serialized),
          blocks:document.querySelectorAll('.wiz-math-block').length,
          links:document.querySelectorAll('.wiz-math-reference').length,
          jax:window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().length:0,
          nodes:document.querySelectorAll('*').length,
          heapBytes:performance.memory?performance.memory.usedJSHeapSize:null};
      });
      outcome.scenarios.push({name, sourceBytes:Buffer.byteLength(source),...initial});
      check(`${name}: original Markdown unchanged`, initial.markdownEqual);
      check(`${name}: no decoration in serialized HTML`, !initial.htmlContainsDecoration);
      if(name==='navigation-lifecycle'){
        const navigation=await page.evaluate(()=>{
          const beforeHtml=WizEditor.getContentHtml(),beforeModified=WizEditor.isModified(),beforeUrl=location.href;
          const target=document.getElementById('wiz-math-thm:long-target'),before=target.getBoundingClientRect();
          document.querySelector('.wiz-math-reference').click();
          const after=target.getBoundingClientRect();
          return{outline:getComputedStyle(target).outlineStyle,top:after.top,geometrySame:before.width===after.width&&before.height===after.height,
            sourceSame:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource,htmlSame:WizEditor.getContentHtml()===beforeHtml,
            modifiedSame:WizEditor.isModified()===beforeModified,urlSame:location.href===beforeUrl};
        });
        check('production reference navigation highlights with inset and preserves geometry/source/HTML/modified state',
          navigation.outline==='solid'&&Math.abs(navigation.top-24)<2&&navigation.geometrySame&&navigation.sourceSame
          &&navigation.htmlSame&&navigation.modifiedSame&&navigation.urlSame,navigation);
        await page.screenshot({path:path.join(out,'navigation-production.png')});
        await page.emulateMedia({media:'print'});
        check('production reference highlight is absent from print',await page.evaluate(()=>getComputedStyle(document.getElementById('wiz-math-thm:long-target')).outlineStyle==='none'));
        await page.emulateMedia({media:'screen'});
        await page.waitForFunction(()=>!document.getElementById('wiz_tmp_style_math_reference_navigation'),null,{timeout:4000});
        check('reference timeout releases its temporary style and timer',await page.evaluate(()=>WizMathReferenceNavigation.timer===null));
        check('repeated navigation keeps one style and disabling the preset cancels it',await page.evaluate(()=>{
          const before=WizEditor.getContentHtml();
          for(let i=0;i<12;i++)document.querySelector('.wiz-math-reference').click();
          const one=document.querySelectorAll('#wiz_tmp_style_math_reference_navigation').length===1;
          WizApplyNoteStyle('body {color:#263950;}');
          return one&&!document.getElementById('wiz_tmp_style_math_reference_navigation')&&WizMathReferenceNavigation.timer===null
            &&WizEditor.getContentHtml()===before&&WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource;
        }));
      }
      if(name==='formula-boundaries'){
        outcome.formulaBoundaries=await page.evaluate(()=>({
          links:Array.from(document.querySelectorAll('.wiz-math-reference')).map(a=>({text:a.textContent,parent:a.parentElement.textContent})),
          scripts:window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().map(j=>j.originalText):[],
          errors:document.querySelectorAll('.MathJax_Error,.merror').length,
          codeUntouched:!document.querySelector('pre .wiz-math-reference'),
          sourceSame:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource
        }));
        check('compound/display math keeps complete original TeX and only two prose/inline block references are linked',
          outcome.formulaBoundaries.links.length===2
          &&outcome.formulaBoundaries.scripts.filter(s=>s==='x+\\ref{thm:boundary}').length===2
          &&outcome.formulaBoundaries.scripts.includes('\\ref{thm:boundary}')
          &&outcome.formulaBoundaries.scripts.includes('(\\ref{thm:boundary})+1')
          &&outcome.formulaBoundaries.codeUntouched&&outcome.formulaBoundaries.sourceSame,
          outcome.formulaBoundaries);
      }
      if(name==='formula-boundary-edge'){
        outcome.formulaBoundaryEdge=await page.evaluate(()=>({
          links:Array.from(document.querySelectorAll('.wiz-math-reference')).map(a=>({text:a.textContent,parent:a.parentElement.textContent})),
          scripts:window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().map(j=>j.originalText):[],
          sourceSame:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource
        }));
        check('environment and brace-group TeX are not consumed by prose reference decoration',
          outcome.formulaBoundaryEdge.links.length===1
          &&outcome.formulaBoundaryEdge.scripts.includes('\\begin{equation}x+\\ref{thm:edge}\\end{equation}')
          &&outcome.formulaBoundaryEdge.scripts.includes('x+\\text{price $5}+\\ref{thm:edge}')
          &&outcome.formulaBoundaryEdge.scripts.includes('\\begin{aligned}x&=1\\\\y&=\\ref{thm:edge}\\end{aligned}')
          &&outcome.formulaBoundaryEdge.sourceSame,outcome.formulaBoundaryEdge);
      }
      if (name === 'small') {
        check('small: two targets and two references', initial.blocks===2 && initial.links===2, initial);
        const navigation = await page.evaluate(() => {
          const before=location.href, a=document.querySelector('.wiz-math-reference');
          const e=new MouseEvent('click',{bubbles:true,cancelable:true});a.dispatchEvent(e);
          const target=document.getElementById(a.getAttribute('href').slice(1));
          return {prevented:e.defaultPrevented,urlUnchanged:location.href===before,targetTop:target.getBoundingClientRect().top,
            outline:getComputedStyle(target).outlineStyle,outlineWidth:getComputedStyle(target).outlineWidth};
        });
        check('reference stays in current page', navigation.prevented&&navigation.urlUnchanged, navigation);
        const repeated = await page.evaluate(() => {
          let equal=true; const t=performance.now();
          for(let i=0;i<25;i++) equal=equal&&WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource;
          return {equal,elapsedMs:performance.now()-t,blocks:document.querySelectorAll('.wiz-math-block').length,links:document.querySelectorAll('.wiz-math-reference').length};
        });
        check('25 source reads preserve text and decoration counts', repeated.equal&&repeated.blocks===2&&repeated.links===2,repeated);
        await page.screenshot({path:path.join(out,'screen-reference-baseline.png'),fullPage:true});
        const prototype=await page.evaluate(async()=>{
          // Exploration prototype only: no product source or target attributes change.
          const originalHtml=WizEditor.getContentHtml(),modifiedBefore=WizEditor.isModified();
          const id='wiz_tmp_style_reference_probe';let timer=null;
          const listener=e=>{
            const a=e.target.closest('a.wiz-math-reference');if(!a)return;
            const target=a.getAttribute('href').slice(1);
            if(!/^wiz-math-[A-Za-z0-9_:./-]{1,128}$/.test(target))return;
            WizEditor.insertCustomStyle(id,'@media screen {body.wiz-readonly.markdown-body [id="'+target+'"] {outline:2px solid #4b69c6!important;outline-offset:4px!important;}}',true);
            if(timer!==null)clearTimeout(timer);
            timer=setTimeout(()=>{WizEditor.removeStyleById(id);timer=null;},1600);
          };
          document.addEventListener('click',listener,true);
          document.querySelector('.wiz-math-reference').click();
          const target=document.getElementById('wiz-math-thm:a');
          const styled=getComputedStyle(target).outlineStyle==='solid';
          const geometryUnchanged=target.getBoundingClientRect().width;
          const sourceUnchanged=WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource;
          const htmlUnchanged=WizEditor.getContentHtml()===originalHtml;
          window.CleanupReferenceProbe=()=>{document.removeEventListener('click',listener,true);if(timer!==null)clearTimeout(timer);WizEditor.removeStyleById(id);};
          return {styled,sourceUnchanged,htmlUnchanged,width:geometryUnchanged,modifiedBefore,modifiedAfter:WizEditor.isModified()};
        });
        check('navigation prototype highlights without serializing changes',prototype.styled&&prototype.sourceUnchanged&&prototype.htmlUnchanged
          &&prototype.modifiedBefore===prototype.modifiedAfter,prototype);
        await page.emulateMedia({media:'print'});
        check('navigation prototype does not appear in print',await page.evaluate(()=>getComputedStyle(document.getElementById('wiz-math-thm:a')).outlineStyle==='none'));
        await page.emulateMedia({media:'screen'});
        await page.screenshot({path:path.join(out,'screen-reference-prototype.png'),fullPage:true});
        await page.evaluate(()=>CleanupReferenceProbe());
        const modes=[];
        for(let cycle=0;cycle<modeCycles;cycle++){
          outcome.activePhase=`${name}:cycle-${cycle}:edit`;
          await page.evaluate(()=>{WizClearMathEnvironments();WizEditor.on();WizApplyNoteStyle();});
          await page.waitForFunction(()=>document.body.isContentEditable&&!document.body.classList.contains('wiz-readonly'),null,{timeout:10000});
          const editing=await page.evaluate(()=>({sourceEqual:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource,
            decorationAbsent:document.querySelectorAll('.wiz-math-block').length===0}));
          outcome.activePhase=`${name}:cycle-${cycle}:read`;
          await page.evaluate(()=>new Promise(resolve=>WizEditor.off(null,()=>{
            const done=()=>{WizApplyNoteStyle();requestAnimationFrame(()=>requestAnimationFrame(resolve));};
            if(window.MathJax&&MathJax.Hub)MathJax.Hub.Queue(done);else done();
          })));
          const reading=await page.evaluate(()=>({sourceEqual:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource,
            blocks:document.querySelectorAll('.wiz-math-block').length,links:document.querySelectorAll('.wiz-math-reference').length}));
          modes.push({cycle,editing,reading});
        }
        outcome.modeRoundTrips=modes;
        check(`${modeCycles} original edit/read mode round trips preserve source and read-only decoration`,modes.every(m=>m.editing.sourceEqual&&m.editing.decorationAbsent
          &&m.reading.sourceEqual&&m.reading.blocks===2&&m.reading.links===2),{cycles:modes.length});
      }
      if(name==='long-reference'){
        const navigation=await page.evaluate(async()=>{
          const target=document.getElementById('wiz-math-thm:long-target');
          document.querySelector('.wiz-math-reference').click();const baselineTop=target.getBoundingClientRect().top;
          window.scrollTo(0,0);
          const before=WizEditor.getContentHtml(),modifiedBefore=WizEditor.isModified();
          WizEditor.insertCustomStyle('wiz_tmp_style_reference_long_probe','@media screen {[id="wiz-math-thm:long-target"]{outline:2px solid #4b69c6!important;outline-offset:4px!important;scroll-margin-top:24px!important;}}',true);
          document.querySelector('.wiz-math-reference').click();
          await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
          return{baselineTop,prototypeTop:target.getBoundingClientRect().top,scrollY,sourceSame:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource,
            htmlSame:WizEditor.getContentHtml()===before,modifiedBefore,modifiedAfter:WizEditor.isModified()};
        });
        outcome.longNavigation=navigation;
        check('long-page navigation prototype leaves visible target inset without modifying content',navigation.scrollY>1000
          &&Math.abs(navigation.prototypeTop-24)<2&&navigation.sourceSame&&navigation.htmlSame&&navigation.modifiedBefore===navigation.modifiedAfter,navigation);
        await page.screenshot({path:path.join(out,'long-reference-prototype.png')});
      }
      if(name==='duplicates') check('ambiguous labels are not linked',initial.blocks===2&&initial.links===0);
      if(name==='math24') check('24 original MathJax display formulas complete',initial.jax===24,initial.jax);
      if(name==='math-compat'){
        const formula=await page.evaluate(()=>({jax:MathJax.Hub.getAllJax().length,errors:document.querySelectorAll('.MathJax_Error,merror').length,
          scripts:MathJax.Hub.getAllJax().map(j=>j.originalText),codeUntouched:document.querySelectorAll('pre .wiz-math-reference').length===0,
          table:document.querySelectorAll('table').length,definitionLinks:document.querySelectorAll('.wiz-math-reference').length}));
        outcome.mathCompatibility=formula;
        check('original formula engine handles AMS, matrices, cases, references and Markdown table',formula.jax>=10&&formula.errors===0
          &&formula.codeUntouched&&formula.table>=1&&formula.definitionLinks===1,formula);
      }
      if(name==='blocks500'||name==='blocks1500') {
        const timings=await page.evaluate(() => {
          const values=[];for(let i=0;i<5;i++){const t=performance.now();WizApplyMathEnvironments();values.push(performance.now()-t);}
          const t=performance.now();const markdown=WizEditor.getMarkdownSrc({unEscapeHtml:true});
          return {decorationMs:values,sourceReadMs:performance.now()-t,equal:markdown===FixtureSource};
        });
        outcome.scenarios[outcome.scenarios.length-1].timings=timings;
        check(`${name}: retain source after repeated decoration`,timings.equal,timings);
        const prototype=await page.evaluate(()=>{
          const baseline=original;
          let code=baseline.toString();
          code=code.replace('var labels = Object.create(null), duplicates = Object.create(null);',
            'var labels = Object.create(null), duplicates = Object.create(null); var blockRecords = new Map();');
          code=code.replace('WizMathDecoration.blocks.push(record);','WizMathDecoration.blocks.push(record); blockRecords.set(block, record);');
          code=code.replace('WizMathDecoration.blocks.find(function (item) { return item.element === block; })','blockRecords.get(block)');
          if(code===baseline.toString()||!code.includes('blockRecords.get(block)'))throw new Error('Prototype source anchor mismatch');
          const candidate=(0,eval)('('+code+')');
          function shape(){return Array.from(document.querySelectorAll('.wiz-math-block')).map(b=>[b.id,b.getAttribute('data-wiz-math-heading')]);}
          const before=JSON.stringify(shape()),trials=[];
          // Alternate order to reduce warm-up bias. Measurements remain single-host diagnostics.
          for(let i=0;i<8;i++)for(const [label,fn] of (i%2?[['map',candidate],['array',baseline]]:[['array',baseline],['map',candidate]])){
            const start=performance.now();fn();trials.push({label,ms:performance.now()-start});
          }
          const sameShape=JSON.stringify(shape())===before;
          const sameMarkdown=WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource;
          return {sameShape,sameMarkdown,trials};
        });
        outcome.scenarios[outcome.scenarios.length-1].mapPrototype=prototype;
        check(`${name}: Map lookup prototype preserves numbers, destinations and source`,prototype.sameShape&&prototype.sameMarkdown,prototype);
        const fragmentation=await page.evaluate(()=>{
          const baseline=original;
          function textCount(){const w=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);let n=0;while(w.nextNode())n++;return n;}
          const before=textCount(),t=performance.now();
          for(let i=0;i<12;i++)baseline();
          const baselineMs=performance.now()-t,after=textCount();
          let code=baseline.toString();
          code=code.replace('fragment.appendChild(document.createTextNode(source.slice(offset, result.index)));',
            'if (result.index > offset) fragment.appendChild(document.createTextNode(source.slice(offset, result.index)));');
          code=code.replace('fragment.appendChild(document.createTextNode(source.slice(offset))); text.replaceWith(fragment);',
            'if (offset < source.length) fragment.appendChild(document.createTextNode(source.slice(offset))); text.replaceWith(fragment);');
          if(!code.includes('if (result.index > offset)')||!code.includes('if (offset < source.length)'))throw new Error('Empty-text prototype anchor mismatch');
          const candidate=(0,eval)('('+code+')'),candidateBefore=textCount(),start=performance.now();
          for(let i=0;i<12;i++)candidate();
          const candidateMs=performance.now()-start,candidateAfter=textCount();
          // Exercise existing serialization guards using the prototype as well.
          const wrapped=WizApplyMathEnvironments;WizApplyMathEnvironments=candidate;
          let sameSource=true;
          for(let i=0;i<12;i++)sameSource=sameSource&&WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource;
          const afterSourceReads=textCount();WizApplyMathEnvironments=wrapped;
          return {cycles:12,before,after,baselineGrowth:after-before,baselineMs,candidateBefore,candidateAfter,
            candidateGrowth:candidateAfter-candidateBefore,candidateMs,afterSourceReads,sameSource,
            references:document.querySelectorAll('.wiz-math-reference').length};
        });
        outcome.scenarios[outcome.scenarios.length-1].textFragmentation=fragmentation;
        check(`${name}: baseline empty-text growth reproduced`,fragmentation.baselineGrowth===2*fragmentation.cycles*fragmentation.references,fragmentation);
        check(`${name}: skip-empty-text prototype stops growth and preserves source`,fragmentation.candidateGrowth===0
          &&fragmentation.afterSourceReads===fragmentation.candidateAfter&&fragmentation.sameSource);
        const spell=await page.evaluate(async()=>{
          let calls=0;const apply=WizApplySpellCheck;
          WizApplySpellCheck=function(){calls++;return apply.apply(this,arguments);};
          WizSetSpellCheck(false);
          await new Promise(resolve=>setTimeout(resolve,200));const idleStart=calls;
          await new Promise(resolve=>setTimeout(resolve,200));const idleEnd=calls;
          const start=performance.now();WizApplyMathEnvironments();const syncMs=performance.now()-start;
          await new Promise(resolve=>setTimeout(resolve,200));const afterDecoration=calls;
          const equal=WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource;
          return {idleStart,idleEnd,afterDecoration,syncMs,equal};
        });
        outcome.scenarios[outcome.scenarios.length-1].spellObserver=spell;
        check(`${name}: disabled spellcheck observer settles and preserves source`,spell.idleStart===spell.idleEnd&&spell.equal,spell);
      }
      if(name==='pdf') {
        await page.evaluate(()=>{
          // Native DOM control bypasses any legacy Markdown HTML sanitization.
          const link=document.createElement('a');link.href='#control-target';link.textContent='Heading control';document.body.prepend(link);
          const heading=document.createElement('h2');heading.id='control-target';heading.textContent='CONTROL-TARGET';document.body.appendChild(heading);
          window.RestorePdfControl=()=>{link.remove();heading.remove();};
        });
        outcome.pdfScreenTargets=await page.evaluate(() => Array.from(document.querySelectorAll('.wiz-math-block')).map(b=>({id:b.id,heading:b.getAttribute('data-wiz-math-heading'),top:b.getBoundingClientRect().top+scrollY})));
        for(const [label,margin] of [['5mm','5mm'],['20mm','20mm']]) {
          await page.pdf({path:path.join(out,`references-${label}.pdf`),format:'A4',printBackground:true,
            margin:{top:margin,bottom:margin,left:margin,right:margin}});
        }
        await page.evaluate(() => {
          // Diagnostic ONLY, never shipped: test a visible target heading without changing note source.
          const style=document.createElement('style');style.textContent='.wiz-math-block::before{display:none!important}';document.head.appendChild(style);
          const records=[];
          document.querySelectorAll('.wiz-math-block').forEach(block=>{
            const span=document.createElement('span');span.textContent=block.getAttribute('data-wiz-math-heading');
            span.style.cssText='display:block;font-weight:700;color:#405bb0;margin-bottom:.45em';
            span.id=block.id;block.removeAttribute('id');block.prepend(span);records.push({block,span});
          });
          window.RestorePdfHeadingProbe=()=>{records.forEach(({block,span})=>{block.id=span.id;span.remove();});style.remove();};
        });
        await page.pdf({path:path.join(out,'references-visible-heading.pdf'),format:'A4',printBackground:true,
          margin:{top:'5mm',bottom:'5mm',left:'5mm',right:'5mm'}});
        await page.evaluate(()=>RestorePdfHeadingProbe());
        await page.evaluate(()=>RestorePdfControl());
        check('diagnostic PDF variation leaves source unchanged',await page.evaluate(()=>WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource));
      }
      outcome.activePhase=name+':close';await page.close();
    }
    if(!process.env.WIZNOTE_EXPLORE_SKIP_MEMORY){
    // Browser heap/node counts are diagnostic, not an OS RSS or Qt leak verdict.
    const memoryPage=await context.newPage(), session=await context.newCDPSession(memoryPage);
    await session.send('HeapProfiler.enable');
    for(let cycle=0;cycle<6;cycle++) {
      await memoryPage.goto(`${origin}/fixture/${cycle%2?'math24':'small'}`);
      await memoryPage.waitForFunction(()=>window.FixtureReady===true,null,{timeout:60000});
      await session.send('HeapProfiler.collectGarbage');
      outcome.memory.push({cycle,scenario:cycle%2?'math24':'small',heap:await session.send('Runtime.getHeapUsage'),dom:await session.send('Memory.getDOMCounters')});
    }
    await memoryPage.goto('about:blank');await session.send('HeapProfiler.collectGarbage');
    outcome.memory.push({cycle:'blank',heap:await session.send('Runtime.getHeapUsage'),dom:await session.send('Memory.getDOMCounters')});
    await memoryPage.close();
    outcome.multiPage=[];
    const system=await browser.newBrowserCDPSession();
    async function snapshot(label){
      const {processInfo}=await system.send('SystemInfo.getProcessInfo');
      const pids=processInfo.map(p=>p.id).filter(Number.isInteger);
      const result=spawnSync('/bin/ps',['-o','rss=','-p',pids.join(',')],{encoding:'utf8'});
      const rssKiB=result.status===0?result.stdout.trim().split(/\s+/).map(Number).reduce((a,b)=>a+b,0):null;
      outcome.multiPage.push({label,processes:processInfo.length,rssKiB});
    }
    await snapshot('no open pages');
    const pages=[];
    try {
      for(let i=0;i<6;i++){
        const page=await context.newPage();pages.push(page);
        await page.goto(`${origin}/fixture/math24`);
        await page.waitForFunction(()=>window.FixtureReady===true,null,{timeout:60000});
        if([0,2,5].includes(i))await snapshot(`${i+1} math pages`);
      }
    } finally {for(const page of pages)await page.close();}
    await snapshot('six pages closed');
    }
    check('no remote resources attempted',outcome.blockedRequests.length===0,outcome.blockedRequests.length);
    check('no browser JavaScript exceptions',outcome.pageErrors.length===0,outcome.pageErrors);
    await context.close();
  } finally {
    if(browser) await browser.close();
    await new Promise(resolve=>server.close(resolve));
    fs.writeFileSync(path.join(out,'rendering-results.json'),JSON.stringify(outcome,null,2)+'\n');
  }
  process.exitCode=outcome.checks.some(c=>!c.ok)?1:0;
})().catch(error=>{console.error(error);server.close();process.exitCode=1;});
