// Local synthetic fixture for the actual, unchanged editor and math engine.
const fs=require('node:fs'),path=require('node:path'),{pathToFileURL}=require('node:url');
const [root,output,variant]=process.argv.slice(2);
if(variant&&variant!=='--plain-equation-label')throw Error('Unknown synthetic fixture variant');
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
if(variant==='--plain-equation-label')source=source.replaceAll('eq:pdf-eq','eq1');
const json=x=>JSON.stringify(x).replace(/</g,'\\u003c');
const escape=x=>x.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
const files=path.join(root,'share/files/wizeditor');
const scripts=['wizEditorForMac.js','editorHelper.js'].map(n=>'<script src="'+escape(pathToFileURL(path.join(files,n)).href)+'"></script>').join('');
const html='<!doctype html><meta charset="utf-8"><body>'+escape(source).replace(/\n/g,'<br>')+scripts+'<script>'+
  'window.FixtureSource='+json(source)+';window.FixtureExpectedJax=3;window.WizNoteCss='+json(css[1])+';'+
  'WizEditor.init({document:document,lang:"zh-cn",clientType:"web",noteType:"markdown",dependencyUrl:'+json(pathToFileURL(path.join(files,'dependency')).href)+',reader:{type:"markdown",callback:{markdown:function(){WizApplyNoteStyle();},mathJax:function(){WizApplyNoteStyle();}}}});'+
  'WizEditor.off({reader:{type:"markdown"}},function(){MathJax.Hub.Queue(function(){WizApplyNoteStyle();document.fonts.ready.then(function(){requestAnimationFrame(function(){requestAnimationFrame(function(){window.FixtureReady=true;});});});});});'+
  '</script></body>';
fs.writeFileSync(output,html,{flag:'wx'});
console.log(JSON.stringify({synthetic:true,markdownCharacters:source.length,expectedMathObjects:3,sourceUnchanged:true}));
