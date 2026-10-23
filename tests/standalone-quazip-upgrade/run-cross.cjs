// Two explicitly configured isolated builds, not the normal client or user data.
const fs=require('fs'),path=require('path'),cp=require('child_process'),crypto=require('crypto');
if(process.argv.length!==5)throw Error('usage: node run-cross.cjs <old-build> <new-build> <new-evidence-folder>');
const [oldBuild,newBuild,out]=process.argv.slice(2).map(p=>path.resolve(p)),builds={old:oldBuild,new:newBuild};
const caches=Object.fromEntries(Object.entries(builds).map(([v,p])=>[v,fs.readFileSync(p+'/CMakeCache.txt','utf8')]));
const setting=(v,key)=>{const m=caches[v].match(new RegExp('^'+key+':[^=]*=(.*)$','m'));if(!m)throw Error('missing setting '+key);return m[1];};
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
for(const v of ['old','new'])if(setting(v,'CMAKE_PROJECT_NAME')!=='WizQuaZipUpgrade')throw Error('not an isolated QuaZip build');
function inputs(){
 const files=new Set([__filename]);const visit=(dir,match)=>{for(const e of fs.readdirSync(dir,{withFileTypes:true})){if(e.name==='.git'||e.name==='AGENTS.md')continue;const p=dir+'/'+e.name;if(e.isDirectory())visit(p,match);else if(match(e.name))files.add(p);}};
 for(const v of ['old','new']){const source=setting(v,'CMAKE_HOME_DIRECTORY'),repo=path.resolve(source,'../..');files.add(source+'/CMakeLists.txt');files.add(source+'/wrapper.cpp');files.add(repo+'/src/share/WizZip.cpp');visit(repo+'/src',name=>/\.(h|hpp)$/.test(name));visit(setting(v,'WIZ_QUAZIP_SOURCE'),()=>true);for(const [dir,name]of [['share','WizMisc'],['share','WizQtHelper'],['utils','WizMisc']])files.add(setting(v,'WIZ_QUAZIP_SUPPORT_OBJECTS')+'/'+dir+'/'+name+'.cpp.o');}
 return Object.fromEntries([...files].sort().map(p=>[p,hash(p)]));
}
const before=inputs();fs.mkdirSync(out);const env={...process.env};for(const k of Object.keys(env))if(/^(DYLD_|QT_|WIZNOTE_)/.test(k))delete env[k];
function run(tag,exe,args){const fd=fs.openSync(out+'/'+tag+'.log','wx'),start=Date.now();const r=cp.spawnSync(exe,args,{env,stdio:['ignore',fd,fd],timeout:120000});fs.closeSync(fd);const result={tag,status:r.status,signal:r.signal,error:r.error?.message,elapsedMs:Date.now()-start};fs.writeFileSync(out+'/'+tag+'-result.json',JSON.stringify(result,null,2)+'\n',{flag:'wx'});if(r.status!==0)throw Error('failed '+tag);return result;}
for(const v of ['old','new'])run('build-'+v,setting(v,'CMAKE_COMMAND'),['--build',builds[v],'--parallel','4','--target','quazip-wrapper']);
if(JSON.stringify(before)!==JSON.stringify(inputs()))throw Error('inputs changed during build');
const executables=()=>Object.fromEntries(Object.entries(builds).map(([v,p])=>[v,hash(p+'/quazip-wrapper')])),executableHashes=executables();fs.writeFileSync(out+'/build-inputs.json',JSON.stringify({inputs:before,executableHashes},null,2)+'\n',{flag:'wx'});
const root=fs.mkdtempSync(out+'/synthetic-archives-');fs.writeFileSync(root+'/.wiz-synthetic-quazip-upgrade','synthetic-only\n',{flag:'wx'});const results=[],reports=[];
for(const creator of ['old','new']){const file=root+'/'+creator+'.ziw';for(const [reader,mode]of [[creator,'create'],...(creator==='old'?['new','old']:['old','new']).map(v=>[v,'check'])]){const tag=creator+'-'+mode+'-'+reader;results.push(run(tag,builds[reader]+'/quazip-wrapper',[mode,file]));const m=fs.readFileSync(out+'/'+tag+'.log','utf8').match(/QUAZIP_WRAPPER_REPORT (.+)/);if(!m)throw Error('missing report '+tag);const r=JSON.parse(m[1]);if(r.failures||!r.actualWizZip||!r.syntheticOnly||r.version!==({old:'1.3',new:'1.7.2'})[reader])throw Error('wrong or incomplete report '+tag);reports.push({tag,...r});}}
if(new Set(reports.map(r=>JSON.stringify(r.entries))).size!==1)throw Error('archive metadata mismatch');
if(JSON.stringify(before)!==JSON.stringify(inputs())||JSON.stringify(executableHashes)!==JSON.stringify(executables()))throw Error('test inputs or executables changed during run');
const summary={accepted:true,syntheticOnly:true,actualWizZip:true,results,reports,checks:reports.reduce((n,r)=>n+r.checks,0),archiveMetadataUnchanged:true,executableHashes,normalAccountOpened:false,realNotesAccessed:false};fs.writeFileSync(out+'/summary.json',JSON.stringify(summary,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({accepted:true,scenarios:reports.length,checks:summary.checks}));
