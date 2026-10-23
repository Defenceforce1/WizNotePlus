// Deterministic browser UMD packaging of pinned, unchanged CommonJS sources.
// No install scripts, downloaded build tooling, source rewriting or minifier.
const fs=require('fs'),path=require('path'),crypto=require('crypto'),assert=require('assert/strict');
const [input,output]=process.argv.slice(2);assert(input&&output,'usage: node bundle-flowchart.cjs VERIFIED_PACKAGE OUTPUT');
const dir=path.resolve(input),pkg=JSON.parse(fs.readFileSync(dir+'/package.json'));assert.equal(pkg.name,'flowchart.js');assert.equal(pkg.version,'1.18.1');
const modules=new Map(),queue=['index.js'],sha=b=>crypto.createHash('sha256').update(b).digest('hex');
while(queue.length){const id=queue.shift();if(modules.has(id))continue;assert(id==='index.js'||/^src\/[a-z0-9.-]+\.js$/.test(id),'bounded module');
const bytes=fs.readFileSync(dir+'/'+id),source=bytes.toString('utf8');assert(Buffer.from(source).equals(bytes),'UTF8');const dependencies={};
for(const m of source.matchAll(/\brequire\(\s*(['"])([^'"]+)\1\s*\)/g)){const request=m[2];if(request==='raphael'){dependencies[request]='@Raphael';continue;}assert(request.startsWith('.'),'only Raphael external');let target=path.posix.normalize(path.posix.join(path.posix.dirname(id),request));if(!target.endsWith('.js'))target+='.js';assert(target==='index.js'||target.startsWith('src/'),'bounded relative import');dependencies[request]=target;queue.push(target);}
modules.set(id,{source,dependencies,bytes:bytes.length,sha256:sha(bytes)});}
const ordered=[...modules].sort((a,b)=>a[0]<b[0]?-1:a[0]>b[0]?1:0);
const banner='/* flowchart.js '+pkg.version+'; MIT, Adriano Raiano. Generated from unchanged pinned CommonJS modules. */\n';
const loader='(function(root,factory){if(typeof exports==="object"&&typeof module==="object")module.exports=factory(require("Raphael"));else if(typeof define==="function"&&define.amd)define(["Raphael"],factory);else root.flowchart=factory(root.Raphael);})(typeof globalThis!=="undefined"?globalThis:this,function(Raphael){\nvar modules={\n';
const body=ordered.map(([id,m])=>JSON.stringify(id)+':[function(module,exports,require){\n'+m.source+'\n},'+JSON.stringify(m.dependencies)+']').join(',\n');
const tail='\n},cache={};function load(id){if(id==="@Raphael")return Raphael;if(Object.prototype.hasOwnProperty.call(cache,id))return cache[id].exports;var item=modules[id];if(!item)throw Error("Unknown bundled flowchart module: "+id);var module=cache[id]={exports:{}};item[0].call(module.exports,module,module.exports,function(name){if(!Object.prototype.hasOwnProperty.call(item[1],name))throw Error("Unknown flowchart dependency: "+name);return load(item[1][name]);});return module.exports;}return load("index.js");});\n';
const artifact=Buffer.from(banner+loader+body+tail);fs.writeFileSync(output,artifact,{flag:'wx'});
console.log(JSON.stringify({version:pkg.version,artifactSha256:sha(artifact),artifactBytes:artifact.length,modules:ordered.map(([path,{source,...m}])=>({path,...m})),upstreamModuleBytesPreserved:true,prebuiltUpstreamDistribution:false,onlyExternal:'Raphael'}));
