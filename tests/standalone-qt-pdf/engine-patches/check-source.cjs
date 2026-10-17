// Read-only patch applicability/provenance check. Never install or modify SDKs.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {spawnSync}=require('node:child_process');
const manifest=JSON.parse(fs.readFileSync(path.join(__dirname,'source-manifest.json'),'utf8'));
const source=process.argv[2];
if(!source||process.argv.length!==3)throw Error('Usage: node check-source.cjs EXACT_QTWEBENGINE_SOURCE_ROOT');
const root=fs.realpathSync(source),thirdParty=path.join(root,'src/3rdparty');
if(!fs.existsSync(path.join(root,'CMakeLists.txt')))throw Error('Not a QtWebEngine source root');
if(fs.readFileSync(path.join(root,'.tag'),'utf8').trim()!==manifest.moduleCommit)
  throw Error('Source archive module does not match the SDK SBOM');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const patch=path.join(__dirname,manifest.patch);
if(hash(patch)!==manifest.patchSha256)throw Error('Draft patch changed; review and update its source manifest');
for(const file of manifest.files){
  const input=path.join(thirdParty,file.path);
  if(hash(input)!==file.originalSha256)throw Error('Not the unmodified pinned source: '+file.path);
}
const checked=spawnSync('git',['apply','--check',patch],{cwd:thirdParty,encoding:'utf8'});
if(checked.status!==0)throw Error(checked.stderr||'Patch check failed');
console.log(JSON.stringify({sourceVerified:true,patchApplicable:true,files:manifest.files.length,
  engineCompiled:false,engineTestsRun:false,productChanged:false,status:manifest.status}));
