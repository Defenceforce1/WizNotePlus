// Read-only state/provenance check for THREE files, not a whole-tree audit.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {spawnSync}=require('node:child_process');
const [input,state]=process.argv.slice(2),states=['original','diagnostic','prototype','candidate'];
if(process.argv.length!==4||!input||!states.includes(state))throw Error('Usage: node check-source.cjs EXACT_QTWEBENGINE_SOURCE_ROOT original|diagnostic|prototype|candidate');
const root=fs.realpathSync(input),manifest=JSON.parse(fs.readFileSync(path.join(__dirname,'source-manifest.json')));
if(fs.readFileSync(path.join(root,'.tag'),'utf8').trim()!==manifest.moduleCommit)throw Error('Wrong source module');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
for(const file of manifest.files)if(hash(path.join(root,file.path))!==file[state+'Sha256'])throw Error('Unexpected file/state: '+file.path);
for(const patch of manifest.patches)if(hash(path.join(__dirname,patch.path))!==patch.sha256)throw Error('Patch hash mismatch');
const patch=path.join(__dirname,manifest.patches[state==='candidate'?2:state==='original'?0:1].path);
const result=spawnSync('git',['apply',...(['prototype','candidate'].includes(state)?['--reverse']:[]),'--check',patch],{cwd:root,encoding:'utf8'});
if(result.status!==0)throw Error(result.stderr||'Patch applicability failed');
console.log(JSON.stringify({readOnly:true,state,filesChecked:manifest.files.length,patchApplicable:true,
  compilationPerformed:false,testsPerformed:false,wholeTreeChecked:false,productChanged:false}));
