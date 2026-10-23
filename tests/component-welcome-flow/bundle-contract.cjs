// Ordinary UMD-loading checks, not a security harness or a DOM implementation.
const fs=require('fs'),vm=require('vm'),assert=require('assert/strict');
const file=process.argv[2];assert(file,'usage: node bundle-contract.cjs GENERATED_FLOWCHART');const code=fs.readFileSync(file,'utf8');let checks=0;
for(const mode of ['global','amd','commonjs']){
 const Raphael=function(){throw Error('no SVG rendering in loader contract');};const sandbox={Raphael};sandbox.window=sandbox;let loaded,calls=0;
 if(mode==='amd'){sandbox.define=function(deps,factory){assert.deepEqual(Array.from(deps),['Raphael']);calls++;loaded=factory(Raphael);};sandbox.define.amd={};}
 if(mode==='commonjs'){sandbox.exports={};sandbox.module={exports:sandbox.exports};sandbox.require=name=>{assert.equal(name,'Raphael');calls++;return Raphael;};}
 vm.runInNewContext(code,sandbox,{timeout:2000,filename:'flowchart-public-bundle.js'});
 const value=mode==='global'?sandbox.flowchart:mode==='amd'?loaded:sandbox.module.exports;
 assert.equal(typeof value.parse,'function');checks++;assert.equal(sandbox.window.flowchart,value);checks++;assert.equal(calls,mode==='global'?0:1);checks++;
 const parsed=value.parse('st=>start: Start\ne=>end: End\nst->e');assert.equal(typeof parsed.drawSVG,'function');checks++;
}
console.log(JSON.stringify({accepted:true,checks,formats:['global','amd','commonjs'],svgRenderingAccepted:false}));
