// Read-only audit of the explicitly enabled, isolated engine instrumentation.
// Balanced release CALLS do not prove GPU safety or full-application stability.
const fs=require('node:fs'),path=require('node:path'),{spawnSync}=require('node:child_process');
const [input,output,mode]=process.argv.slice(2);
if(!input||!output||process.argv.length!==5||!['--expect-balanced','--expect-final-retention'].includes(mode))
  throw Error('Usage: node audit-metal-lifetime.cjs LOG NEW_REPORT --expect-balanced|--expect-final-retention');
if(fs.existsSync(output))throw Error('Refusing existing report');
const checked=spawnSync(process.execPath,[path.join(__dirname,'../compat/summarize-editor-memory.cjs'),input],{encoding:'utf8'});
if(checked.status!==0)throw Error(checked.stderr||'Incomplete/failed isolated memory probe');
const workload=JSON.parse(checked.stdout).cases;
if(workload.length!==1||!workload[0].standalone||workload[0].graphicsApi!=='Metal')
  throw Error('One complete standalone Metal run required');
const live=new Map(),records=[];let devices=0;
const lines=fs.readFileSync(input,'utf8').split('\n');
for(let line=0;line<lines.length;++line){
  const match=lines[line].match(/WizMetalLifetime (create|release|wrapper-destroyed|device-destroyed) (?:native|current)=(0x[0-9a-f]+)/);
  if(!match)continue;
  const [,event,native]=match;
  if(event==='create'){
    if(live.has(native))throw Error('Native pointer created twice without release');
    const record={generation:records.length+1,native,created:line};
    records.push(record);live.set(native,record);continue;
  }
  const record=live.get(native);
  if(!record)throw Error('Unknown/already-released native pointer at '+event);
  const field={'release':'released','wrapper-destroyed':'wrapperDestroyed','device-destroyed':'deviceDestroyed'}[event];
  if(record[field]!==undefined)throw Error('Repeated native event: '+event);
  record[field]=line;
  if(event==='device-destroyed')++devices;
  if(event==='release'){
    if(record.wrapperDestroyed===undefined)throw Error('Release precedes QSG wrapper destruction');
    live.delete(native);
  }
}
if(!records.length||devices!==workload[0].pageOpenings+1)throw Error('Missing expected retained/transient view events');
if(records.some(r=>r.wrapperDestroyed===undefined))throw Error('A QSG wrapper survived process cleanup');
const retained=[...live.values()];
if(mode==='--expect-balanced'&&retained.length)throw Error('Native textures retained after final cleanup');
if(mode==='--expect-final-retention'&&(retained.length!==devices||retained.some(r=>r.deviceDestroyed===undefined)))
  throw Error('Did not reproduce exactly one final retained texture per view');
const result={readOnly:true,countsOnly:true,gpuSafetyClaimed:false,productFixed:false,
  expectation:mode.slice(2),creates:records.length,releases:records.filter(r=>r.released!==undefined).length,
  wrappersDestroyed:records.length,devicesDestroyed:devices,retainedNativeTextures:retained.length,
  workload:workload[0],records};
fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({expectation:result.expectation,creates:result.creates,releases:result.releases,
  devices:devices,retained:retained.length,gpuSafetyClaimed:false}));
