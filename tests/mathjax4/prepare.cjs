// Assemble ONLY a fresh experimental MathJax directory, never the live App.
// Usage: node prepare.cjs <verified-mathjax-package> <verified-tex-font-package> <new-output>
const fs=require('fs'),path=require('path'),crypto=require('crypto');
const [math,font,output]=process.argv.slice(2).map(p=>path.resolve(p));
if(!math||!font||!output||fs.existsSync(output))throw Error('provide two extracted pinned packages and a NEW output directory');
const lock=JSON.parse(fs.readFileSync(path.join(__dirname,'inputs.json'))),hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
for(const item of lock.files){const source=path.join(item.origin==='mathjax'?math:font,item.source);if(hash(source)!==item.sha256)throw Error('unverified input '+item.source);}
fs.mkdirSync(output,{recursive:true});fs.copyFileSync(path.join(__dirname,'MathJax.js'),path.join(output,'MathJax.js'));
for(const item of lock.files){const dest=path.join(output,item.path);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.copyFileSync(path.join(item.origin==='mathjax'?math:font,item.source),dest);}
fs.copyFileSync(path.join(__dirname,'inputs.json'),path.join(output,'inputs.json'));
console.log(JSON.stringify({candidateOnly:true,engine:'4.1.3',font:'mathjax-tex4.1.3',files:lock.files.length+2,speechAndSemanticEnrichment:false}));
