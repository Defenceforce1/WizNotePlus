// Owned presentation CSS lifecycle and ordering regression; synthetic DOM only.
const fs=require('node:fs'),vm=require('node:vm'),path=require('node:path'),assert=require('node:assert/strict');
const input=process.argv[2]||path.join(__dirname,'../../share/files/wizeditor/editorHelper.js');
const text=fs.readFileSync(input,'utf8'),start=text.indexOf('var WizNoteStyleApplications = new WeakMap();');assert(start>=0);
const code=text.slice(start,text.indexOf('// Optional read-only decoration',start));
function fixture(){
 const head={},state={node:null,writes:0,removes:0,decorations:0,fail:false,normalize:false};
 function node(css){return {tagName:'STYLE',parentNode:head,textContent:css,disabled:false,sheet:{disabled:false},attributes:[{name:'id',value:'wiz_tmp_style_note_presentation'},{name:'wiz_style',value:'unsave'}]};}
 const editor={insertCustomStyle(id,css){state.writes++;if(state.fail)throw new Error('same failure');state.node=node(state.normalize?css+' ':css);},removeStyleById(){state.removes++;state.node=null;}};
 const context={window:null,WizEditor:editor,document:{head,getElementById(){return state.node;}},WizApplyMathEnvironments(){state.decorations++;}};context.window=context;vm.createContext(context);vm.runInContext(code,context);
 return {state,context,node,apply(css){context.WizApplyNoteStyle(css);}};
}
const results=[];
function check(name,fn){fn();results.push({name,passed:true});}
check('unchanged CSS writes once while decoration executes every time',()=>{const f=fixture();for(let i=0;i<12;i++)f.apply('body{color:red}');assert.equal(f.state.writes,1);assert.equal(f.state.decorations,12);});
check('changed CSS rewrites',()=>{const f=fixture();f.apply('a{}');f.apply('b{}');assert.equal(f.state.writes,2);assert.equal(f.state.node.textContent,'b{}');});
check('externally changed text rewrites',()=>{const f=fixture();f.apply('a{}');f.state.node.textContent='other';f.apply();assert.equal(f.state.writes,2);});
check('replacement identity rewrites even with same text',()=>{const f=fixture();f.apply('a{}');f.state.node=f.node('a{}');f.apply();assert.equal(f.state.writes,2);});
check('removed node rewrites',()=>{const f=fixture();f.apply('a{}');f.state.node=null;f.apply();assert.equal(f.state.writes,2);});
check('moved node rewrites',()=>{const f=fixture();f.apply('a{}');f.state.node.parentNode={};f.apply();assert.equal(f.state.writes,2);});
check('changed attributes rewrite',()=>{const f=fixture();f.apply('a{}');f.state.node.attributes.push({name:'media',value:'print'});f.apply();assert.equal(f.state.writes,2);});
for(const key of ['disabled','sheet.disabled'])check(key+' rewrites',()=>{const f=fixture();f.apply('a{}');if(key==='disabled')f.state.node.disabled=true;else f.state.node.sheet.disabled=true;f.apply();assert.equal(f.state.writes,2);});
check('replaced API is invoked',()=>{const f=fixture();f.apply('a{}');const orig=f.context.WizEditor.insertCustomStyle;let calls=0;f.context.WizEditor.insertCustomStyle=function(...a){calls++;return orig(...a);};f.apply();assert.equal(calls,1);});
check('empty CSS removes presentation style',()=>{const f=fixture();f.apply('a{}');f.apply('');assert.equal(f.state.node,null);assert.equal(f.state.removes,1);assert.equal(f.state.decorations,2);});
check('writer failure propagates and is not cached',()=>{const f=fixture();f.state.fail=true;assert.throws(()=>f.apply('a{}'),/same failure/);f.state.fail=false;f.apply();assert.equal(f.state.writes,2);assert.equal(f.state.decorations,1);});
check('normalized mismatching output is not cached',()=>{const f=fixture();f.state.normalize=true;f.apply('a{}');f.apply();assert.equal(f.state.writes,2);});
check('unavailable engine does not run decoration',()=>{const f=fixture();f.context.WizEditor=null;f.apply('a{}');assert.equal(f.state.writes,0);assert.equal(f.state.decorations,0);});
check('a following stylesheet preserves cascade priority by reapplying',()=>{const f=fixture();f.apply('a{}');f.state.node.nextElementSibling={tagName:'STYLE',nextElementSibling:null};f.apply();assert.equal(f.state.writes,2);});
check('a following stylesheet link also forces reapply',()=>{const f=fixture();f.apply('a{}');f.state.node.nextElementSibling={tagName:'LINK',getAttribute(){return 'alternate STYLESHEET';},nextElementSibling:null};f.apply();assert.equal(f.state.writes,2);});
check('a following script does not force a stylesheet rewrite',()=>{const f=fixture();f.apply('a{}');f.state.node.nextElementSibling={tagName:'SCRIPT',nextElementSibling:null};f.apply();assert.equal(f.state.writes,1);});
console.log(JSON.stringify({cases:results.length,failures:0,results}));
