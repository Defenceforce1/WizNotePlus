// Reports measurement validity, not a claim that increasing native footprint is
// acceptable or that the complete client is leak-free. Uses synthetic logs only.
const fs=require('fs'),assert=require('node:assert/strict');
function summarize(text){
  assert(!/Unable to initialize SkSurface|SKIA: Failed to begin write access|Context lost during MakeCurrent|Failed to make current since context is marked as lost|GPU process exited unexpectedly/.test(text),'invalid graphics path');
  assert(!/^FAIL /m.test(text),'failed workload assertion');
  const reports=text.split('\n').filter(s=>s.startsWith('STRESS_REPORT '));assert.equal(reports.length,1,'one complete report');
  const r=JSON.parse(reports[0].slice(14));assert.equal(r.schema,1);assert.equal(r.rounds,8);assert.equal(r.paragraphs,6000);assert.equal(r.failures,0);assert.equal(r.blockedExternalRequests,0);
  assert(['stress-static','stress-static-no-grab','stress-prose','stress-math'].includes(r.kind));const math=r.kind==='stress-math',staticControl=r.kind.startsWith('stress-static');
  assert.equal(r.formulaCount,math?512:0);assert.equal(r.checks,staticControl?33:math?113:105);assert.equal(r.samples.length,18);
  const open=r.samples.filter(x=>x.phase==='open'),closed=r.samples.filter(x=>x.phase==='closed');assert.equal(open.length,8);assert.equal(closed.length,8);
  for(let i=0;i<8;++i){
    const o=open[i],s=closed[i];assert.equal(o.round,i);assert.equal(s.round,i);assert(o.sourceBytes>1000000);
    assert(o.backends.length>0&&o.backends.every(x=>x.graphicsApi===6&&x.windowClass==='QQuickWidgetOffscreenWindow'),'actual hidden Cocoa Metal backing');
    assert.equal(o.memory.processCount,2);assert.equal(o.memory.widgetCount,2);assert.equal(o.memory.windowCount,2);
    assert.equal(s.memory.processCount,1);assert.equal(s.memory.widgetCount,0);assert.equal(s.memory.windowCount,0);assert.equal(s.profileObjects,0);
    assert(Number.isFinite(s.memory.mainFootprintKiB)&&s.memory.mallocInUseKiB>0,'native footprint and heap observations present');
    if(!staticControl){assert.equal(o.telemetry.configNodes,1);assert.equal(o.telemetry.loaderSettled,true);assert(o.telemetry.ticks>0);assert(o.telemetry.domNodes>=12000);}
  }
  if(math)assert.equal((text.match(/^PASS both large document equation links have real targets$/gm)||[]).length,8,'nonvacuous SVG xlink target checks');
  const final=r.samples.at(-1);assert.equal(final.phase,'closed-10s');assert.equal(final.memory.processCount,1);assert.equal(final.memory.widgetCount,0);assert.equal(final.memory.windowCount,0);assert.equal(final.profileObjects,0);
  const after=text.split('\n').filter(s=>s.startsWith('STRESS_AFTER_SCOPE '));assert.equal(after.length,1,'post-profile/scope idle observed');const idle=JSON.parse(after[0].slice(19));assert.equal(idle.processCount,1);assert.equal(idle.widgetCount,0);assert.equal(idle.windowCount,0);
  const span=(xs,key)=>({min:Math.min(...xs.map(x=>x[key])),max:Math.max(...xs.map(x=>x[key]))});
  return {measurementValid:true,kind:r.kind,pixelGrab:r.pixelGrab!==false,rounds:r.rounds,checks:r.checks,failures:r.failures,sourceBytes:open[0].sourceBytes,formulaObjects:math?514:0,
    nativeMemoryRepresentativeOfUiTurns:false,profileHeldGrowthAttributionAllowed:false,nativeMemoryWarning:'Single outer callback retains autoreleased native resources;use retention-yield controls,not these samples,for per-turn attribution.',
    initialReadyMs:staticControl?null:span(open,'initialReadyMs'),readyAndRoundTripMs:span(open,'readyAndRoundTripMs'),
    closedFirst:closed[0].memory,closedLast:closed.at(-1).memory,closedIdle10s:final.memory,afterProfileAndScopeIdle10s:idle,
    closedFootprintGrowthKiB:closed.at(-1).memory.mainFootprintKiB-closed[0].memory.mainFootprintKiB,
    stableMemoryClaimed:false,wholeClientAcceptance:false,forcedGcOrCacheClear:false,productionBackendChanged:false,
    browserMaxTimerGapMs:staticControl?null:Math.max(...open.map(x=>x.telemetry.maxGapMs)),
    ownedInclusiveMaxMs:staticControl?null:Object.fromEntries(Object.keys(open[0].telemetry.ownedInclusiveTiming).map(k=>[k,Math.max(...open.map(x=>x.telemetry.ownedInclusiveTiming[k].maxMs))]))};
}
module.exports={summarize};
if(require.main===module){
  const self=process.argv[2]==='--self-test',files=process.argv.slice(self?3:2);assert(files.length>0,'provide completed stress log files');
  const result=files.map(file=>({file,...summarize(fs.readFileSync(file,'utf8'))}));
  if(self){const original=fs.readFileSync(files[0],'utf8');const change=fn=>original.replace(/^STRESS_REPORT (.+)$/m,(_,s)=>'STRESS_REPORT '+JSON.stringify(fn(JSON.parse(s))));
    assert.throws(()=>summarize(original+'\nUnable to initialize SkSurface\n'));
    assert.throws(()=>summarize(change(r=>{r.samples.find(x=>x.phase==='closed').memory.widgetCount=1;return r;})));
    assert.throws(()=>summarize(original.replace(/^STRESS_AFTER_SCOPE .+$/m,'')));
    assert.throws(()=>summarize(change(r=>{r.failures=1;return r;})));
    const math=files.find(f=>fs.readFileSync(f,'utf8').includes('"kind":"stress-math"'));
    assert(math,'include a math log for the vacuous-link control');assert.throws(()=>summarize(fs.readFileSync(math,'utf8').replace(/^PASS both large document equation links have real targets$/gm,'')));
    console.log(JSON.stringify({parserControls:5,failures:0,results:result}));
  }else console.log(JSON.stringify(result));
}
