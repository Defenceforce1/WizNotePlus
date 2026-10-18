// Read-only patch applicability/provenance check. Never install or modify SDKs.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {spawnSync}=require('node:child_process');
const manifest=JSON.parse(fs.readFileSync(path.join(__dirname,'source-manifest.json'),'utf8'));
const source=process.argv[2];
const candidate=process.argv[3]==='--candidate';
if(!source||(process.argv.length!==3&&!(process.argv.length===4&&candidate)))
  throw Error('Usage: node check-source.cjs EXACT_QTWEBENGINE_SOURCE_ROOT [--candidate]');
const root=fs.realpathSync(source),thirdParty=path.join(root,'src/3rdparty');
if(!fs.existsSync(path.join(root,'CMakeLists.txt')))throw Error('Not a QtWebEngine source root');
if(fs.readFileSync(path.join(root,'.tag'),'utf8').trim()!==manifest.moduleCommit)
  throw Error('Source archive module does not match the SDK SBOM');
const hash=p=>crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const patch=path.join(__dirname,manifest.patch);
if(hash(patch)!==manifest.patchSha256)throw Error('Draft patch changed; review and update its source manifest');
for(const file of manifest.files){
  const input=path.join(thirdParty,file.path);
  if(hash(input)!==(candidate?file.candidateSha256:file.originalSha256))
    throw Error('Not the pinned '+(candidate?'candidate':'unmodified')+' source: '+file.path);
}
const checked=spawnSync('git',['apply',...(candidate?['--reverse']:[]),'--check',patch],{cwd:thirdParty,encoding:'utf8'});
if(checked.status!==0)throw Error(checked.stderr||'Patch check failed');
console.log(JSON.stringify({sourceVerified:true,patchApplicable:true,files:manifest.files.length,
  sourceState:candidate?'candidate':'unmodified',
  operation:'read-only-source-applicability',compilationPerformed:false,testsPerformed:false,
  productChanged:false,candidateStatus:manifest.status}));
