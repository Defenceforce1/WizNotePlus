#!/usr/bin/env node
'use strict';
// Offline characterization of the existing bundled reader. Fixtures and the
// report contain synthetic text only; remote requests are blocked.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const crypto = require('node:crypto');
const {chromium} = require('playwright');
const root = path.resolve(__dirname, '../..');
const output = process.argv[2];
const prototype = process.argv[3] === '--prototype';
const product = process.argv[3] === '--product';
const initialType = process.env.WIZ_MATH_ENV_INITIAL_TYPE || 'markdown';
if (!['common','markdown'].includes(initialType)) throw Error('Unsupported initial note type');
const selected = process.env.WIZ_BARE_MATH_CASES ? new Set(process.env.WIZ_BARE_MATH_CASES.split(',')) : null;
if (!output || !path.isAbsolute(output) || fs.existsSync(output))
  throw Error('Provide a new absolute JSON report path');
const assets = path.join(root, 'share/files/wizeditor');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'wiz-bare-math-'));
const eq = '\\begin{equation}\nx_1^2+y_1^2=z_1^2\\label{eq:bare}\n\\end{equation}';
const align = '\\begin{align}\na_1&=b_1+c_1\\\\\na_2&=b_2+c_2\\label{eq:align}\n\\end{align}';
const cases = [
  ['bare-equation', eq, 1],
  ['bare-align', align, 1],
  ['screenshot-nested-cases', '\\begin{equation}\\label{eq1}\nA+C-D=\\begin{cases}A\\\\B,\n\\end{cases}\n\\end{equation}\n\n\\eqref{eq1}', 2],
  ['equation-with-inline-trigger', '$x$\n\n'+eq, 2],
  ['align-with-inline-trigger', '$x$\n\n'+align, 2],
  ['wrapped-equation', '$$\n'+eq+'\n$$', 1],
  ['wrapped-align', '$$\n'+align+'\n$$', 1],
  ['starred-equation', eq.replaceAll('{equation}','{equation*}'), 1],
  ['starred-align', align.replaceAll('{align}','{align*}'), 1],
  ['multiple-environments', eq+'\n\n'+align, 2],
  ['nested-aligned', '\\begin{equation}\n\\begin{aligned}\na&=b\\\\\nc&=d\n\\end{aligned}\n\\end{equation}', 1],
  ['blank-line', '\\begin{equation}\nx=1\n\n+y\n\\end{equation}', 1],
  ['after-prose-no-blank', 'Before the display\n'+eq, 1],
  ['before-prose-no-blank', eq+'\nAfter the display', 1],
  ['one-line-equation', '\\begin{equation}x=1\\label{eq:one}\\end{equation}', 1],
  ['indented-two-spaces', eq.split('\n').map(line=>'  '+line).join('\n'), 1],
  ['quoted-equation', eq.split('\n').map(line=>'> '+line).join('\n'), 1],
  ['list-equation', '- Display:\n\n'+eq.split('\n').map(line=>'  '+line).join('\n'), 1],
  ['list-four-space-equation', '- Display:\n\n'+eq.split('\n').map(line=>'    '+line).join('\n'), 1],
  ['ordered-list-equation', '1. Display:\n\n'+eq.split('\n').map(line=>'   '+line).join('\n'), 1],
  ['list-indented-code', '- Code:\n\n'+eq.split('\n').map(line=>'        '+line).join('\n'), 0],
  ['quote-indented-code', eq.split('\n').map(line=>'>     '+line).join('\n'), 0],
  ['html-entities', '\\begin{equation}\na < b \\text{A\\&B}\n\\end{equation}', 1],
  ['gather-environment', '\\begin{gather}\na=b\\\\\nc=d\n\\end{gather}', 1],
  ['multline-environment', '\\begin{multline}\na+b+c\\\\\n=d+e\n\\end{multline}', 1],
  ['unsupported-flalign-preserved', '\\begin{flalign}\na&=b&\\\\\nc&=d&\n\\end{flalign}', 0],
  ['alignat-environment', '\\begin{alignat}{2}\na&=b&c&=d\\\\\ne&=f&g&=h\n\\end{alignat}', 1],
  ['quoted-fenced-equation', ('```latex\n'+eq+'\n```').split('\n').map(line=>'> '+line).join('\n'), 0],
  ['quoted-nested-equation', eq.split('\n').map(line=>'> > '+line).join('\n'), 1],
  ['quoted-mixed-code-and-real', ('```latex\n'+eq+'\n```\n\n'+eq).split('\n').map(line=>'> '+line).join('\n'), 1],
  ['list-fenced-equation', '- Code:\n\n'+('```latex\n'+eq+'\n```').split('\n').map(line=>'  '+line).join('\n'), 0],
  ['fenced-equation', '```latex\n'+eq+'\n```', 0],
  ['tilde-fence', '~~~latex\n'+eq+'\n~~~', 0],
  ['mixed-code-and-real', '~~~latex\n'+eq+'\n~~~\n\n'+eq, 1],
  ['indented-code', eq.split('\n').map(line=>'    '+line).join('\n'), 0],
  ['inline-code', '`\\begin{equation}x\\end{equation}`', 0],
  ['multiline-inline-code', '`example\n'+eq+'\n`', 0],
  ['unfinished', '\\begin{align}\na_1&=b_1', 0]
];
const escape = s => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const json = s => JSON.stringify(s).replace(/</g, '\\u003c');
const report = {schema:1, kind:'offline-bare-math-environment-characterization', prototype, product, initialType,
  engine:'headless Chrome, not Qt', remoteRequests:[], pageErrors:[], cases:[]};
(async () => {
  const browser = await chromium.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',
    headless:true, chromiumSandbox:true, args:['--allow-file-access-from-files']});
  try {
    for (const [name, input, expectedJax] of cases) {
      if (selected && !selected.has(name)) continue;
      const source = input + '\n';
      const context = await browser.newContext({serviceWorkers:'block'});
      await context.route('**/*', route => {
        const url = route.request().url();
        if (/^(file|data|about|blob):/.test(url)) return route.continue();
        report.remoteRequests.push(url); return route.abort();
      });
      const page = await context.newPage();
      page.on('pageerror', error => report.pageErrors.push({name,message:error.message}));
      const scripts = ['wizEditorForMac.js',...(product?['dependency/marked/marked.umd.js','latexTheorems.js','markdownEnvironments.js']:[]),'editorHelper.js'].map(file =>
        '<script name="wiz_inner_script" wiz_style="unsave" src="'+escape(pathToFileURL(path.join(assets,file)).href)+'"></script>').join('');
      const prototypeScript = prototype ? '<script name="wiz_inner_script" wiz_style="unsave" src="'+
        escape(pathToFileURL(path.join(__dirname,'bare-math-environments-prototype.js')).href)+'"></script>' : '';
      const dependency = json(pathToFileURL(path.join(assets,'dependency')).href);
      const html = '<!doctype html><meta charset="utf-8"><body>'+escape(source).replace(/\n/g,'<br>')+scripts+prototypeScript+
        '<script name="wiz_inner_script" wiz_style="unsave">window.FixtureSource='+json(source)+';var options={document:document,lang:"zh-cn",clientType:"web",noteType:'+json(initialType)+',dependencyUrl:'+
        dependency+',reader:{type:'+json(initialType)+'}};'+
        (prototype ? 'WizConfigureBareMathPrototype(options);' : '')+(product?'WizInitMarkdownEditor(options);':'WizEditor.init(options);')+
        (prototype ? 'WizInstallBareMathPrototype('+dependency+');' : '')+
        'WizEditor.off({reader:{type:"markdown"}},function(){window.FixtureReady=true;});</script></body>';
      const file = path.join(temporary, name+'.html');
      fs.writeFileSync(file,html,{flag:'wx'});
      await page.goto(pathToFileURL(file).href);
      await page.waitForFunction(() => window.FixtureReady, null, {timeout:30000});
      const state = await page.evaluate(() => ({
        jax:window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().map(j=>j.originalText):[],
        errors:document.querySelectorAll('.MathJax_Error,merror,.noError').length,
        sourceSame:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource,
        savedSource:WizEditor.getMarkdownSrc({unEscapeHtml:true}),
        labels:Array.from(document.querySelectorAll('[id^="mjx-eqn-"]')).map(n=>n.id),
        text:document.body.innerText.slice(0,1600), html:document.body.innerHTML.slice(0,3000)
      }));
      const cycles = [];
      if (prototype || product) for (let index=0; index<2; ++index) {
        await page.evaluate(() => new Promise(resolve => WizEditor.on(null,resolve)));
        const editingSame = await page.evaluate(() => WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource);
        await page.evaluate(() => new Promise(resolve => WizEditor.off({reader:{type:'markdown'}},resolve)));
        cycles.push(await page.evaluate(() => ({sourceSame:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource,
          jax:window.MathJax&&MathJax.Hub?MathJax.Hub.getAllJax().length:0,
          errorText:Array.from(document.querySelectorAll('.MathJax_Error,merror')).map(n=>n.textContent),
          errors:document.querySelectorAll('.MathJax_Error,merror,.noError').length})))
        cycles[cycles.length-1].editingSame=editingSame;
      }
      const expectedEnvironments = {
        'bare-equation':[eq], 'bare-align':[align],
        'equation-with-inline-trigger':[eq], 'align-with-inline-trigger':[align],
        'wrapped-equation':[eq], 'wrapped-align':[align],
        'quoted-equation':[eq], 'quoted-nested-equation':[eq],
        'quoted-mixed-code-and-real':[eq], 'list-equation':[eq],
        'list-four-space-equation':[eq], 'ordered-list-equation':[eq],
        'multiple-environments':[eq,align], 'mixed-code-and-real':[eq]
      }[name] || [];
      const exactEnvironment = expectedEnvironments.every(tex=>state.jax.some(actual=>actual.trim()===tex));
      const ok=state.sourceSame&&state.jax.length===expectedJax&&!state.errors&&exactEnvironment&&cycles.every(c=>
        c.sourceSame&&c.editingSame&&c.jax===expectedJax&&!c.errors);
      report.cases.push({name,expectedJax,...state,exactEnvironment,cycles,ok});
      console.log(JSON.stringify({name,expectedJax,jax:state.jax.length,errors:state.errors,sourceSame:state.sourceSame}));
      await context.close();
    }
    report.assetHashes = Object.fromEntries(['wizEditorForMac.js','editorHelper.js'].map(file=>
      [file,crypto.createHash('sha256').update(fs.readFileSync(path.join(assets,file))).digest('hex')]));
    if (product && process.env.WIZ_MATH_ENV_LIFECYCLE === '1') {
      report.lifecycle=[];
      const fixture=path.join(temporary,'bare-equation.html');
      if (!fs.existsSync(fixture)) throw Error('Include bare-equation for lifecycle checks');
      for (const mode of ['dependency-failure','edit-during-load']) {
        const context=await browser.newContext({serviceWorkers:'block'});
        let held;
        await context.route('**/*',route=>{
          const url=route.request().url();
          if(url.includes('/mathjax/MathJax.js')) {
            if(mode==='dependency-failure')return route.abort();
            held=route;return;
          }
          return /^(file|data|about|blob):/.test(url)?route.continue():route.abort();
        });
        const page=await context.newPage();
        await page.goto(pathToFileURL(fixture).href,{waitUntil:'domcontentloaded'});
        if(mode==='edit-during-load') {
          await page.waitForFunction(()=>window.WizMarkdownEnvironmentRenderState==='pending');
          while(!held)await new Promise(resolve=>setTimeout(resolve,10));
          await page.evaluate(()=>{window.CancelReady=false;WizEditor.on(null,()=>{window.CancelReady=true;});});
          await held.continue();
          await page.waitForFunction(()=>window.CancelReady&&document.body.isContentEditable);
        } else await page.waitForFunction(()=>window.FixtureReady);
        const state=await page.evaluate(()=>({state:window.WizMarkdownEnvironmentRenderState,
          sourceSame:WizEditor.getMarkdownSrc({unEscapeHtml:true})===FixtureSource,
          sourceHtmlClean:!WizEditor.getContentHtml().includes('wiz-bare-math-environment')
            &&!WizEditor.getContentHtml().includes('MathJax_SVG_glyphs'),
          editable:document.body.isContentEditable}));
        state.ok=state.sourceSame&&state.sourceHtmlClean
          &&(mode==='dependency-failure'?state.state==='failed':state.editable&&state.state==='cancelled');
        report.lifecycle.push({mode,...state});
        await context.close();
      }
    }
    report.fixtureDirectory = temporary;
    report.failures = report.cases.filter(c=>!c.ok).map(c=>c.name);
    fs.writeFileSync(output,JSON.stringify(report,null,2)+'\n',{flag:'wx'});
    if ((prototype || product) && (report.failures.length || report.pageErrors.length || report.remoteRequests.length
      ||report.lifecycle?.some(c=>!c.ok)))
      process.exitCode=1;
  } finally { await browser.close(); }
})().catch(error => { console.error(error); process.exitCode=1; });
