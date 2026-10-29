// Run: node tests/compat/mathjax-native-fit.cjs [candidate bridge]
// Exercise the actual owned fit loop, not a copied candidate implementation.
// Real-engine rendering, native memory and PDF restore are tested separately.
const fs=require('fs'),path=require('path'),assert=require('assert');
const file=process.argv[2]||path.resolve(__dirname,'../../share/files/wizeditor/dependency/mathjax/MathJax.js');
const text=fs.readFileSync(file,'utf8'),a=text.indexOf('    pendingFits.forEach(function (item) {'),z=text.indexOf('    // Keep the established',a);
assert(a>0&&z>a);const fit=new Function('pendingFits','window',text.slice(a,z));
function run({width=900,height=16.1875,ink={x:415,y:-2.086162567138672e-7,width:483,height:16.1796932220459},box={x:0,y:0,width:0,height:0},paper=false,data=true,display=true}={}){
 const values=new Map(data?[['data-mjx-viewBox','native']]:[]),style={},rootStyle={},calls=[];
 const svg={style,viewBox:{baseVal:box},getBBox(){calls.push('bbox');return ink;},getBoundingClientRect(){calls.push('rect');return {width,height};},hasAttribute(k){return values.has(k);},setAttribute(k,v){values.set(k,v);}};
 const root={style:rootStyle,querySelectorAll(){return [svg];}};
 fit([{display,typesetRoot:root}],{__wizPaperResult:paper?{}:undefined});return {values,style,rootStyle,calls};
}
let checks=0;function check(b){assert(b);checks++;}
for(const height of [16.1875,16.171875]){
 const r=run({height});check(!r.values.has('viewBox'));check(Object.keys(r.style).length===0&&Object.keys(r.rootStyle).length===0);check(r.calls.join(',')==='bbox,rect');
}
for(const paper of [false,true]){
 const r=run({paper,ink:{x:0,y:0,width:1200,height:16}});check(r.values.get('viewBox')==='0 0 1200 16.1875');check(r.values.get('width')==='1200px');check(r.style.maxWidth==='100%'&&r.rootStyle.minWidth==='0');
}
const paper=run({paper:true});check(paper.values.get('viewBox')==='0 0 900 16.1875');check(paper.values.get('preserveAspectRatio')==='xMidYMid meet');
const tall=run({ink:{x:0,y:0,width:800,height:20}});check(tall.values.get('viewBox')==='0 0 900 20');
const horizontal=run({ink:{x:0,y:0,width:900.0001,height:16}});check(horizontal.values.has('viewBox'));
const at=run({height:16,ink:{x:0,y:0,width:900,height:16+1/64}});check(!at.values.has('viewBox'));
const above=run({height:16,ink:{x:0,y:0,width:900,height:16+1/64+0.00001}});check(above.values.has('viewBox'));
const natural=run({data:false});check(!natural.values.has('viewBox'));
const existing=run({box:{x:0,y:0,width:1000,height:20},ink:{x:0,y:0,width:900,height:16}});check(!existing.values.has('data-wiz-mjx4-ink-fit'));
const wideExisting=run({box:{x:0,y:0,width:1000,height:20},ink:{x:0,y:0,width:1200,height:16}});check(wideExisting.values.get('viewBox')==='0 0 1200 20');check(wideExisting.values.get('width')==='1080px');
const inline=run({display:false});check(inline.calls.length===0);
console.log(JSON.stringify({accepted:true,checks,actualBridgeLoop:true,normalNativeGeometryPreserved:true,paperAndOverflowRetainFixedGeometry:true,fullClientAccepted:false}));
