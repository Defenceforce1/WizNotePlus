// Run only in a configured standalone build and a new, dedicated evidence folder.
// A failed build cannot fall through to stale test executables.
const fs=require('fs'),path=require('path'),cp=require('child_process'),crypto=require('crypto');
if(process.argv.length!==5)throw Error('usage: node run-cross.cjs <standalone-build> <share-resources> <new-evidence-folder>');
const [build,share,out]=process.argv.slice(2).map(p=>path.resolve(p));
const cache=fs.readFileSync(build+'/CMakeCache.txt','utf8');
const value=key=>{const match=cache.match(new RegExp('^'+key+':[^=]*=(.*)$','m'));if(!match)throw Error('missing CMake setting '+key);return match[1];};
if(value('CMAKE_PROJECT_NAME')!=='WizSqliteUpgrade')throw Error('not a standalone SQLite build');
const source=value('CMAKE_HOME_DIRECTORY'),repo=path.resolve(source,'../..');
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function inputs(){
 const files=new Set([__filename,source+'/CMakeLists.txt',source+'/wrapper.cpp']);
 const visit=(dir,match)=>{for(const e of fs.readdirSync(dir,{withFileTypes:true})){const p=dir+'/'+e.name;if(e.isDirectory())visit(p,match);else if(match(e.name))files.add(p);}};
 visit(repo+'/src',name=>/\.(h|hpp)$/.test(name));visit(share+'/sql',()=>true);
 for(const p of ['cppsqlite3.cpp','WizIndexBase.cpp','WizThumbIndex.cpp'])files.add(repo+'/src/database/'+p);
 for(const dir of [value('WIZ_SQLITE_OLD_DIR'),value('WIZ_SQLITE_NEW_DIR')])for(const name of ['sqlite3.c','sqlite3.h','sqlite3ext.h'])files.add(dir+'/'+name);
 for(const name of ['WizMisc','WizQtHelper','WizObject'])files.add(value('WIZ_SQLITE_SUPPORT_OBJECTS')+'/share/'+name+'.cpp.o');
 return Object.fromEntries([...files].sort().map(file=>[file,sha(file)]));
}
const before=inputs();fs.mkdirSync(out);const env={...process.env};for(const k of Object.keys(env))if(/^(DYLD_|QT_|WIZNOTE_)/.test(k))delete env[k];
function run(tag,exe,args){
 const fd=fs.openSync(out+'/'+tag+'.log','wx'),start=Date.now();
 const r=cp.spawnSync(exe,args,{env,stdio:['ignore',fd,fd],timeout:120000});fs.closeSync(fd);
 const result={tag,status:r.status,signal:r.signal,error:r.error?.message,elapsedMs:Date.now()-start};
 fs.writeFileSync(out+'/'+tag+'-result.json',JSON.stringify(result,null,2)+'\n',{flag:'wx'});
 if(r.status!==0)throw Error('failed '+tag+'; see evidence log');return result;
}
run('build',value('CMAKE_COMMAND'),['--build',build,'--parallel','4','--target','sqlite-wrapper-old','sqlite-wrapper-new']);
if(JSON.stringify(before)!==JSON.stringify(inputs()))throw Error('test inputs changed during build');
const executables=Object.fromEntries(['old','new'].map(v=>[v,sha(build+'/sqlite-wrapper-'+v)]));
fs.writeFileSync(out+'/build-inputs.json',JSON.stringify({inputs:before,executables},null,2)+'\n',{flag:'wx'});
const root=fs.mkdtempSync(out+'/synthetic-db-');fs.writeFileSync(root+'/.wiz-synthetic-sqlite-upgrade','synthetic-only\n',{flag:'wx'});const results=[],reports=[];
for(const creator of ['old','new']){
 const db=root+'/'+creator+'-中文.db';
 const cases=[[creator,'create'],...[...(creator==='old'?['new','old']:['old','new'])].map(reader=>[reader,'check'])];
 for(const [reader,mode] of cases){
  const tag=creator+'-'+mode+'-'+reader;results.push(run(tag,build+'/sqlite-wrapper-'+reader,[share,mode,db]));
  const log=fs.readFileSync(out+'/'+tag+'.log','utf8'),match=log.match(/SQLITE_WRAPPER_REPORT (.+)/);if(!match)throw Error('missing report '+tag);
  const report=JSON.parse(match[1]);if(report.failures||!report.actualWrapper||!report.actualIndexMethods||!report.actualThumbnailMethods||!report.healthyDumpReadRoundtrip||!report.existingSchemaUpgradeTested)throw Error('incomplete report '+tag);reports.push({tag,...report});
 }
}
if(new Set(reports.map(r=>r.snapshotSha256)).size!==1)throw Error('cross-engine logical data mismatch');
if(JSON.stringify(before)!==JSON.stringify(inputs())||JSON.stringify(executables)!==JSON.stringify(Object.fromEntries(['old','new'].map(v=>[v,sha(build+'/sqlite-wrapper-'+v)]))))throw Error('test inputs or executables changed during run');
const summary={accepted:true,syntheticOnly:true,normalAccountOpened:false,realNotesAccessed:false,results,reports,checks:reports.reduce((n,r)=>n+r.checks,0),identicalLogicalSnapshots:true,executables};
fs.writeFileSync(out+'/summary.json',JSON.stringify(summary,null,2)+'\n',{flag:'wx'});console.log(JSON.stringify({accepted:true,scenarios:reports.length,checks:summary.checks}));
