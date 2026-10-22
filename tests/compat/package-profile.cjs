// Exercise refusal paths before deployment; never build, sign or copy an App.
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const root=fs.mkdtempSync(path.join(os.tmpdir(),'wiz-package-profile-')),build=path.join(root,'build'),output=path.join(root,'output');
fs.mkdirSync(build);fs.mkdirSync(path.join(output,'WizNotePlus Apple Silicon.app'),{recursive:true});let count=0;
function run(value,allow=false) {const cache=path.join(build,'CMakeCache.txt');if(value===null){if(fs.existsSync(cache))fs.unlinkSync(cache);}else fs.writeFileSync(cache,'WIZNOTE_BUILD_COMPAT_TESTS:BOOL='+value+'\n');const env={...process.env};delete env.WIZNOTE_PACKAGE_DIAGNOSTIC;if(allow)env.WIZNOTE_PACKAGE_DIAGNOSTIC='1';return cp.spawnSync('bash',[path.resolve(__dirname,'../../build/osx/package-arm64.sh'),root,build,output],{env,encoding:'utf8'});}
try {
    for(const [value,allow,message] of [[null,false,'configured CMake'],['ON',false,'require WIZNOTE_BUILD_COMPAT_TESTS=OFF'],['OFF',false,'Refusing to overwrite'],['ON',true,'Refusing to overwrite'],['OFF',true,'Refusing to overwrite'],['UNKNOWN',true,'require WIZNOTE_BUILD_COMPAT_TESTS=OFF']]) {const r=run(value,allow);assert.notEqual(r.status,0);assert.ok(r.stderr.includes(message),r.stderr);++count;}
    assert.deepEqual(fs.readdirSync(output),['WizNotePlus Apple Silicon.app']);++count;
    assert.deepEqual(fs.readdirSync(path.join(output,'WizNotePlus Apple Silicon.app')),[]);++count;
    console.log('Package profile checks '+count+' failures 0');
} finally {fs.rmSync(root,{recursive:true,force:true});}
