// Local synthetic fixture for the actual, unchanged editor and math engine.
const fs=require('node:fs'),path=require('node:path'),{pathToFileURL}=require('node:url');
const [root,output,variant,...providedArgs]=process.argv.slice(2);
const provided=variant==='--provided-markdown';
if(providedArgs.length&&!provided)throw Error('Unexpected fixture arguments');
let providedJax,providedBlocks;
if(provided) {
  const [input,jax,blocks]=providedArgs;
  providedJax=Number(jax);providedBlocks=Number(blocks);
  if(providedArgs.length!==3||!path.isAbsolute(input)||!fs.statSync(input).isFile()
    ||fs.statSync(input).size>1024*1024||![providedJax,providedBlocks].every(n=>Number.isInteger(n)&&n>=0&&n<=1000))
    throw Error('Provided copy requires an absolute Markdown file and explicit expected math/block counts');
}
const fitVariants=['--multline-fit-base','--multline-fit-star-base','--multline-fit-long-base','--multline-fit-contained-base','--multline-fit-ref-base'];
if(variant&&!['--plain-equation-label','--base-equation-label','--bare-environments-base','--bare-containers-base','--latex-theorems-base','--reader-equations-base','--paper-assets-base','--provided-markdown',...fitVariants].includes(variant))throw Error('Unknown fixture variant');
const readerEquations=variant==='--reader-equations-base';
const multlineFit=fitVariants.includes(variant);
const paperAssets=variant==='--paper-assets-base';
const bare=variant==='--bare-environments-base'||variant==='--bare-containers-base'||variant==='--latex-theorems-base'||readerEquations||multlineFit||paperAssets||provided;
if(!root||!output||!path.isAbsolute(root)||!path.isAbsolute(output)||fs.existsSync(output))
  throw Error('Absolute source root and a new output HTML path required');
if(path.basename(output)!=='native-pdf-fixture.html'||!fs.statSync(path.dirname(output)).isDirectory())
  throw Error('Use a new native-pdf-fixture.html inside an existing synthetic directory');
const cpp=fs.readFileSync(path.join(root,'src/WizNotePresentation.cpp'),'utf8');
const css=cpp.slice(cpp.indexOf('QString scientificMathStyle()')).match(/R"CSS\(([\s\S]*?)\)CSS"/);
if(!css)throw Error('Current scientific preset not found');
let source='# PDF reference fixture\n\nTheorem: \\ref{thm:pdf-a}; definition: \\ref{def:pdf-b}.\n\nEquation: $\\eqref{eq:pdf-eq}$.\n\n';
for(let i=0;i<12;++i)source+='## Prelude '+i+'\n\n'+'Synthetic paragraph for stable pagination. '.repeat(12)+'\n\n';
source+='> **定理（thm:pdf-a）** \\label{thm:pdf-a}\n>\n> PDF-TARGET-A. $$a^2+b^2=c^2$$\n\n<!-- -->\n\n';
for(let i=0;i<5;++i)source+='## Middle '+i+'\n\n'+'Middle paragraph to separate targets. '.repeat(10)+'\n\n';
source+='> **定义（def:pdf-b）** \\label{def:pdf-b}\n>\n> PDF-TARGET-B. This identifies the definition destination.\n\n<!-- -->\n\n';
source+='Equation target:\n\n$$x^2+y^2=z^2\\tag{1}\\label{eq:pdf-eq}$$\n';
if(variant==='--plain-equation-label'||variant==='--base-equation-label'||bare)source=source.replaceAll('eq:pdf-eq','eq1');
if(bare) {
  source=source.replace('$\\eqref{eq1}$','[Equation (1)](#mjx-eqn-eq1)')
    .replace('$$a^2+b^2=c^2$$','Pythagorean identity.')
    .replace('$$x^2+y^2=z^2\\tag{1}\\label{eq1}$$','\\begin{equation}\nx^2+y^2=z^2\\tag{1}\\label{eq1}\n\\end{equation}')
    +'\n\\begin{align}\na_1&=b_1+c_1+\\eqref{eq1}\\\\\na_2&=b_2+c_2\\label{eq:bare-align}\n\\end{align}\n';
}
if(variant==='--bare-containers-base') {
  source=source.replace(/\\begin\{equation\}[\s\S]*?\\end\{equation\}/, tex=>tex.split('\n').map(line=>'> '+line).join('\n'))
    .replace(/\\begin\{align\}[\s\S]*?\\end\{align\}/, tex=>'- Aligned display:\n\n'+tex.split('\n').map(line=>'    '+line).join('\n'))
    +'\n\\begin{gather}\nx=y\\\\\ny=z\\label{eq:gather}\n\\end{gather}\n\n'
    +'\\begin{multline}\na+b+c\\\\\n=d+e\\label{eq:multline}\n\\end{multline}\n';
}
if(variant==='--latex-theorems-base') {
  source=source.replace('> **定理（thm:pdf-a）** \\label{thm:pdf-a}\n>\n> PDF-TARGET-A. Pythagorean identity.',
    '\\begin{theorem}[PDF target A]\\label{thm:pdf-a}\nPDF-TARGET-A. Pythagorean identity.\n\\end{theorem}')
    .replace('> **定义（def:pdf-b）** \\label{def:pdf-b}\n>\n> PDF-TARGET-B. This identifies the definition destination.',
    '\\begin{definition}[PDF target B]\\label{def:pdf-b}\nPDF-TARGET-B. This identifies the definition destination.\n\\end{definition}')
    .replace('[Equation (1)](#mjx-eqn-eq1)','\\eqref{eq1}')
    .replace('# PDF reference fixture','# PDF reference fixture\n\nProposition \\ref{prop:pdf}; corollary \\ref{cor:pdf}.')
    +'\n\\begin{lemma}\nA supporting lemma.\n\\end{lemma}\n\n'
    +'\\begin{proposition}\\label{prop:pdf}\nA proposition.\n\\end{proposition}\n\n'
    +'\\begin{corollary}\\label{cor:pdf}\nA consequence.\n\\end{corollary}\n\n'
    +'\\begin{proof}\nBy the preceding proposition.\n\\end{proof}\n';
}
if(readerEquations) {
  source='\\begin{equation}\\label{eq1}\nA+C-D=\n\\end{equation}\n\n'
    +'\\begin{equation}\\label{eq2}\nA+C-D=\n\\end{equation}\n\n'
    +'\\begin{theorem}\\label{thm:reader}\n$A+V-$.\n\\end{theorem}\n\n'
    +'\\begin{example}\\label{ex:reader}\n$$A$$\n\\end{example}\n\n'
    +Array.from({length:30},(_,i)=>'Synthetic line '+(i+1)).join('\n\n')
    +'\n\n\\ref{ex:reader}\n\n\\ref{thm:reader}\n\n\\eqref{eq2}\n\n\\eqref{eq1}\n';
}
if(multlineFit) {
  const star=variant==='--multline-fit-star-base',long=variant==='--multline-fit-long-base';
  const display=(id)=>'\\begin{multline'+(star?'*':'')+'}\n\\color{red}{'+(long?'a+'.repeat(40)+'b':'a+b+c')+'}\\\\\n'
    +'\\color{green}{u+v=w}\\\\\n\\color{blue}{=d+e'+(variant==='--multline-fit-ref-base'?'+\\eqref{eq:source}':'')+'}'
    +(star?'':(long?'\\tag{12345}':'')+'\\label{'+id+'}')+'\n\\end{multline'+(star?'*':'')+'}\n\n';
  source='# Synthetic multline fit\n\n';
  if(variant==='--multline-fit-ref-base')source+='\\begin{equation}a=b\\label{eq:source}\\end{equation}\n\n';
  if(variant==='--multline-fit-contained-base')source+=display('eq:quote').trimEnd().split('\n').map(line=>'> '+line).join('\n')+'\n\n'
    +'- Synthetic list:\n\n'+display('eq:list').trimEnd().split('\n').map(line=>'    '+line).join('\n')+'\n\n';
  else source+=display('eq:multline');
  source+='Last row: '+(star?'complete.':variant==='--multline-fit-contained-base'?'\\eqref{eq:quote}; \\eqref{eq:list}':'\\eqref{eq:multline}')+'\n\n';
}
if(paperAssets){
  const imagePath=path.join(path.dirname(output),'local image #%.svg');
  fs.writeFileSync(imagePath,'<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="180" viewBox="0 0 1200 180"><rect width="1200" height="180" fill="#f519cb"/><text x="35" y="115" font-size="64">LOCAL IMAGE</text></svg>',{flag:'wx'});
  const image=pathToFileURL(imagePath).href;
  source='# Paper layout resources\n\n<code>$literal$</code> and ordinary text.\n\n'
    +'```tex\n\\begin{equation}code only\\end{equation}\n```\n\n'
    +'\\begin{equation}\\label{eq:paper}\n\\color{red}{'+('a+'.repeat(42))+'b}=\\color{blue}{c}\n\\end{equation}\n\n'
    +'\\begin{align}\\label{eq:align}\na&=b+c\\\\\nd&=e+f\n\\end{align}\n\n'
    +'\\begin{equation}\\label{eq:wide}\n\\color{purple}{\\underbrace{\\text{'+('M'.repeat(60))+'}}_{\\text{fixed group}}}\n\\end{equation}\n\n'
    +'\\begin{theorem}\\label{thm:paper}\nPaper theorem text.\n\\end{theorem}\n\n'
    +'![Local SVG image]('+image+')\n\n| Long cell | Value |\n|---|---|\n| '+('paper'.repeat(40))+' | End |\n\n'
    +'<span id="paper-target">Paper target</span>\n\n[Jump to heading](#paper-target)\n\n'
    +'See \\eqref{eq:paper}, \\eqref{eq:align}, \\eqref{eq:wide} and \\ref{thm:paper}.\n\nFINAL PAPER RESOURCE MARKER\n';
}
if(provided)source=fs.readFileSync(providedArgs[0],'utf8');
const expectedJax=provided?providedJax:paperAssets?6:variant==='--multline-fit-star-base'?1:variant==='--multline-fit-contained-base'?4:variant==='--multline-fit-ref-base'?3:readerEquations?6:variant==='--latex-theorems-base'?3:variant==='--bare-containers-base'?4:bare?2:3;
const expectedBlocks=provided?providedBlocks:paperAssets?1:multlineFit?0:variant==='--latex-theorems-base'?6:2;
const expectedFitted=variant==='--multline-fit-contained-base'?2:multlineFit||variant==='--bare-containers-base'?1:0;
const json=x=>JSON.stringify(x).replace(/</g,'\\u003c');
const escape=x=>x.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
const files=path.join(root,'share/files/wizeditor');
const scripts=['wizEditorForMac.js','dependency/marked/marked.umd.js','latexTheorems.js','markdownEnvironments.js','editorHelper.js'].map(n=>'<script src="'+escape(pathToFileURL(path.join(files,n)).href)+'"></script>').join('');
const base=variant==='--base-equation-label'||bare?'<base href="./">':'';
const defaultStyle=readerEquations||provided?'<link rel="stylesheet" href="'+escape(pathToFileURL(path.join(files,'default.css')).href)+'">':'';
const html='<!doctype html><meta charset="utf-8">'+base+defaultStyle+'<body>'+escape(source).replace(/\n/g,'<br>')+scripts+'<script>'+
  'window.FixtureSource='+json(source)+';window.FixtureProvidedCopy='+provided+';window.FixtureExpectedJax='+expectedJax+';window.FixtureExpectedBlocks='+expectedBlocks+';window.FixtureExpectedFitted='+expectedFitted+';window.WizNoteCss='+json(css[1])+';'+
  (provided&&process.env.WIZNOTE_FIXTURE_STOCK_READER==='1'?'WizEditor.init':'WizInitMarkdownEditor')+'({document:document,lang:"zh-cn",clientType:"web",noteType:"markdown",dependencyUrl:'+json(pathToFileURL(path.join(files,'dependency')).href)+',reader:{type:"markdown",callback:{markdown:function(){WizApplyNoteStyle();},mathJax:function(){WizApplyNoteStyle();}}}});'+
  (provided?'WizEditor.on(null,function(){window.FixtureSourceAfterInit=WizEditor.getMarkdownSrc({unEscapeHtml:true});window.FixtureHtmlAfterInit=WizEditor.getContentHtml();':'')+
  'WizEditor.off({reader:{type:"markdown"}},function(){MathJax.Hub.Queue(function(){WizApplyNoteStyle();document.fonts.ready.then(function(){requestAnimationFrame(function(){requestAnimationFrame(function(){window.FixtureReady=true;});});});});});'+
  (provided?'});':'')+
  '</script></body>';
fs.writeFileSync(output,html,{flag:'wx'});
console.log(JSON.stringify({synthetic:!provided,providedMarkdownCopy:provided,markdownCharacters:source.length,expectedMathObjects:expectedJax,expectedBlocks,sourceUnchanged:true}));
