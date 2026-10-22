// Attribution control, not a universal memory budget or whole-client leak test.
const fs=require('fs'),assert=require('node:assert/strict');
function summarize(text){
  assert(!/Unable to initialize SkSurface|SKIA: Failed to begin write access|Context lost during MakeCurrent|Failed to make current since context is marked as lost|GPU process exited unexpectedly/.test(text),'invalid graphics path');
  assert(!/^FAIL /m.test(text),'failed native assertion');const lines=text.split('\n').filter(s=>s.startsWith('RETENTION_REPORT '));assert.equal(lines.length,1);
  const r=JSON.parse(lines[0].slice(17));assert.equal(r.schema,1);assert.equal(r.failures,0);assert.equal(r.externalRequests,0);assert.equal(r.formulaTestsRun,false);
  assert(['retention-nested','retention-yield','retention-nested-pool','retention-yield-persistent'].includes(r.kind));
  const persistent=r.kind==='retention-yield-persistent';assert.equal(r.rounds,persistent?16:8);assert.equal(r.checks,r.rounds*5+2);assert(r.sourceBytes>1000000);
  assert.equal(r.explicitPoolIsDiagnosticOnly,r.kind==='retention-nested-pool');assert.equal(r.samples.length,2*r.rounds+5);
  const samples=r.samples,one=phase=>{const a=samples.filter(s=>s.phase===phase);assert.equal(a.length,1,'single '+phase);return a[0];};
  const baseline=one('baseline'),end=one('batch-callback-end'),alive=one('profile-live-idle-10s'),destroyed=one('profile-destroyed'),idle=one('profile-destroyed-idle-10s');
  const before=samples.filter(s=>s.phase==='before-round'),closed=samples.filter(s=>s.phase==='closed');assert.equal(before.length,r.rounds);assert.equal(closed.length,r.rounds);
  assert(samples.indexOf(alive)>samples.indexOf(end)&&samples.indexOf(destroyed)>samples.indexOf(alive)&&samples.at(-1)===idle,'separate callback-return and profile-destruction boundaries');
  for(const s of samples){
    assert.equal(s.profileGeneration,1);assert.equal(s.profileObjects,0);assert.equal(s.memory.available,true);assert.equal(s.memory.processCount,1);assert.equal(s.memory.widgetCount,0);assert.equal(s.memory.windowCount,0);assert(Number.isFinite(s.memory.mainFootprintKiB));
    const live=s!==destroyed&&s!==idle;assert.equal(s.profileLive,live,'same profile must survive callback-return observation');
    if(live&&persistent){assert.equal(s.offTheRecord,false,'actual named persistent profile');assert.equal(s.storageIsTemporary,true,'persistent profile storage and cache stay in temporary root');}
  }
  for(let i=0;i<r.rounds;++i){assert.equal(before[i].round,i);assert.equal(closed[i].round,i);assert(samples.indexOf(before[i])<samples.indexOf(closed[i]));}
  assert.equal((text.match(/^PASS hidden native Metal window remains unexposed without pixel grabs$/gm)||[]).length,r.rounds,'actual per-page Metal controls');
  const footprint=s=>s.memory.mainFootprintKiB,tail=closed.slice(r.rounds/2).map(footprint);
  return {measurementValid:true,kind:r.kind,rounds:r.rounds,checks:r.checks,formulaTestsRun:false,sourceBytes:r.sourceBytes,persistentProfile:persistent,
    explicitPoolIsDiagnosticOnly:r.explicitPoolIsDiagnosticOnly,closedFirst:closed[0].memory,closedLast:closed.at(-1).memory,
    baseline:baseline.memory,profileStillAliveIdle10s:alive.memory,profileDestroyed:destroyed.memory,profileDestroyedIdle10s:idle.memory,
    closedGrowthKiB:footprint(closed.at(-1))-footprint(closed[0]),callbackReturnDropWithProfileAliveKiB:footprint(end)-footprint(alive),
    additionalProfileDestructionDropKiB:footprint(alive)-footprint(idle),lastHalfClosedRangeKiB:Math.max(...tail)-Math.min(...tail),
    wholeClientOrLongTermStabilityClaimed:false,cacheCleared:false,productionPoolOrBackendChanged:false};
}
function compare(results){
  const get=kind=>{const found=results.find(r=>r.kind===kind);assert(found,'missing '+kind);return found;};
  const nested=get('retention-nested'),yielded=get('retention-yield'),pooled=get('retention-nested-pool'),persistent=get('retention-yield-persistent');
  assert(nested.closedGrowthKiB>65536&&nested.callbackReturnDropWithProfileAliveKiB>65536,'reproduce nested batch retention and drop BEFORE profile destruction');
  assert(Math.abs(nested.additionalProfileDestructionDropKiB)<8192,'profile destruction is not the large-drop boundary');
  for(const r of [yielded,pooled,persistent])assert(r.lastHalfClosedRangeKiB<32768&&r.closedGrowthKiB<nested.closedGrowthKiB/2,'bounded control lacks nested-batch scale growth');
  return {nestedCallbackArtifactConfirmed:true,diagnosticPoolSupportsAutoreleaseAttribution:true,persistentProfileControlPassed:true,criterionScope:'Local bounded experiment;not a general memory cap or complete client acceptance.'};
}
module.exports={summarize,compare};
if(require.main===module){
  const self=process.argv[2]==='--self-test',files=process.argv.slice(self?3:2);assert.equal(files.length,4,'provide nested,yield,pool,persistent logs');
  const texts=files.map(p=>fs.readFileSync(p,'utf8')),results=texts.map(summarize),comparison=compare(results);let parserControls=0;
  if(self){
    const original=texts[0],change=fn=>original.replace(/^RETENTION_REPORT (.+)$/m,(_,s)=>'RETENTION_REPORT '+JSON.stringify(fn(JSON.parse(s))));
    assert.throws(()=>summarize(original+'\nUnable to initialize SkSurface\n'));++parserControls;
    assert.throws(()=>summarize(change(r=>{r.samples.find(s=>s.phase==='profile-live-idle-10s').profileLive=false;return r;})));++parserControls;
    assert.throws(()=>summarize(change(r=>{r.samples=r.samples.filter(s=>s.phase!=='profile-live-idle-10s');return r;})));++parserControls;
    assert.throws(()=>summarize(change(r=>{r.formulaTestsRun=true;return r;})));++parserControls;
    assert.throws(()=>summarize(change(r=>{r.samples.find(s=>s.phase==='closed').memory.widgetCount=1;return r;})));++parserControls;
    const persistent=texts.find(t=>t.includes('"kind":"retention-yield-persistent"'));
    assert.throws(()=>summarize(persistent.replace(/^RETENTION_REPORT (.+)$/m,(_,s)=>{const r=JSON.parse(s);r.samples.find(s=>s.phase==='profile-live-idle-10s').offTheRecord=true;return 'RETENTION_REPORT '+JSON.stringify(r);})));++parserControls;
    const legacy=require('./summarize-reader-stress.cjs').summarize(fs.readFileSync(require('path').join(require('path').dirname(files[0]),'../phase208/stress-static-final.log'),'utf8'));
    assert.equal(legacy.profileHeldGrowthAttributionAllowed,false);assert.equal(legacy.nativeMemoryRepresentativeOfUiTurns,false);++parserControls;
  }
  console.log(JSON.stringify({results,comparison,parserControls,failures:0}));
}
