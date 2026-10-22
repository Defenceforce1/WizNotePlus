// Synthetic log parser checks only; no browser, account, or graphics process.
const fs=require('node:fs'),os=require('node:os'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const dir=fs.mkdtempSync(path.join(os.tmpdir(),'wiz-memory-report-'));
const opened={phase:'four-open',batch:0,processCount:5,available:true,hiddenCocoa:true,visibleCocoa:false,visibleOffscreen:false,actualQuickBackends:Array.from({length:4},()=>({graphicsApi:6,windowClass:'QQuickWidgetOffscreenWindow'}))};
const closed={phase:'all-closed',batch:0,processCount:1,available:true,mainFootprintKiB:65536,mainRssKiB:65536};
const probe={hiddenCocoa:true,qtPlatform:'cocoa',samples:[opened,closed]};let count=0;
function run(report=probe,prefix='') {const file=path.join(dir,'fixture.log');fs.writeFileSync(file,prefix+'MEMORY_PROBE '+JSON.stringify(report)+'\nNative editor runtime checks: 2 failures: 0\n');return cp.spawnSync(process.execPath,[path.join(__dirname,'summarize-editor-memory.cjs'),file],{encoding:'utf8'});}
try {
    const good=run();assert.equal(good.status,0);assert.equal(JSON.parse(good.stdout).cases[0].hiddenCocoa,true);++count;
    for(const error of ['Unable to initialize SkSurface','SKIA: Failed to begin write access.','Context lost during MakeCurrent.','Failed to make current since context is marked as lost','GPU process exited unexpectedly']) {assert.notEqual(run(probe,error+'\n').status,0);++count;}
    const invalid=structuredClone(probe);invalid.samples[0].actualQuickBackends[0].graphicsApi=1;assert.notEqual(run(invalid).status,0);++count;
    const visible=structuredClone(probe);visible.samples[0].visibleCocoa=true;assert.notEqual(run(visible).status,0);++count;
    const remaining=structuredClone(probe);remaining.samples[1].processCount=2;assert.notEqual(run(remaining).status,0);++count;
    console.log('Memory report checks '+count+' failures 0');
} finally {fs.rmSync(dir,{recursive:true,force:true});}
