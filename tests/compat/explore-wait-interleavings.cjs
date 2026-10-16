#!/usr/bin/env node
'use strict';
// Deterministic protocol MODEL, not a Qt application/thread execution test.
// Enumerates the gap visible in DocumentLoaderSaver; never opens a database.
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo=path.resolve(__dirname,'../..');
const archive=process.env.WIZNOTE_EXPLORE_BASELINE_ARCHIVE;
function source(file){return archive?require('node:child_process').execFileSync('/usr/bin/tar',['-xOzf',archive,'./'+file],{encoding:'utf8',maxBuffer:2*1024*1024}):fs.readFileSync(path.join(repo,file),'utf8');}
const header=source('src/gui/documentviewer/DocumentLoaderSaver.h');
const implementation=source('src/gui/documentviewer/DocumentLoaderSaver.cpp');
const main=source('src/WizMainWindow.cpp');
assert(!header.includes('bool m_signaled'), 'Historical protocol study requires the frozen 14.4.16 archive via WIZNOTE_EXPLORE_BASELINE_ARCHIVE; current sticky event is verified by native tests');
assert(header.includes('m_waitForData.wait(&m_mutex);'));
assert(implementation.includes('if (isEmpty())\n    {\n        m_waitEvent.wait();'));
assert(implementation.includes('m_arrayData.push_back(data);'));
function interleave(left,right){
  if(!left.length)return[right];if(!right.length)return[left];
  return [...interleave(left.slice(1),right).map(x=>[left[0],...x]),
    ...interleave(left,right.slice(1)).map(x=>[right[0],...x])];
}
function execute(steps,stopping=false){
  const state={queue:0,stop:false,checkedEmpty:false,waiting:false};
  const trace=[];
  for(const step of steps){
    if(step==='worker checks empty')state.checkedEmpty=state.queue===0;
    if(step==='worker enters wait'&&state.checkedEmpty)state.waiting=true;
    if(step==='producer publishes'){
      if(stopping)state.stop=true;else state.queue++;
    }
    if(step==='producer wakes'&&state.waiting)state.waiting=false;
    trace.push({step,...state});
  }
  return {steps,trace,lostWake:state.waiting&&(state.queue>0||state.stop)};
}
const schedules=interleave(['worker checks empty','worker enters wait'],['producer publishes','producer wakes']);
const queueRuns=schedules.map(s=>execute(s)),stopRuns=schedules.map(s=>execute(s,true));
assert(queueRuns.some(x=>x.lostWake),'Expected source protocol model to expose the empty-check/wait gap');
assert(stopRuns.some(x=>x.lostWake),'Expected stop notification to be missable in same gap');
// Reference protocol: queue/stop predicate, condition wait and notify all use one
// lock, so the worker cannot begin waiting on a stale empty observation.
const corrected=interleave(['atomic predicate check and wait registration'],['atomic publish and wake']).map(steps=>{
  let queue=0,waiting=false;
  for(const step of steps){
    if(step.startsWith('atomic predicate'))waiting=queue===0;
    else {queue++;waiting=false;}
  }
  return {steps,lostWake:waiting&&queue>0};
});
assert(corrected.every(x=>!x.lostWake));
const connection='connect(m_docLoader, &WizDocumentLoaderThread::loadFailed, [this]';
const create=main.indexOf('WizDocumentView* WizMainWindow::createDocumentView()');
assert(create>=0&&main.indexOf(connection,create)>create,'Recheck shared-loader contextless callback before relying on finding');
assert(implementation.includes('m_strCurrentDocGUID = docGUID;'));
let slot=null;const requested=['note-A','note-B'];for(const note of requested)slot=note;
const taken=slot;slot=null;
assert(taken==='note-B');
console.log(JSON.stringify({kind:'deterministic protocol model, NOT native Qt reproduction',
  sourceProtocolQueueSchedules:queueRuns.length,queueLostWakeWitnesses:queueRuns.filter(x=>x.lostWake),
  sourceProtocolStopSchedules:stopRuns.length,stopLostWakeWitnesses:stopRuns.filter(x=>x.lostWake),
  singleLockReferenceSchedules:corrected,
  singleSlotLoadWitness:{requestedBeforeConsumerRuns:requested,consumerReceives:taken,missingCompletionFor:'note-A',
    limitation:'This demonstrates single-slot overwrite; whether affected views request again requires native integration testing'},
  sourceObservation:'createDocumentView connects the shared loader loadFailed signal without a receiver context for every document view',
  noRealThreadsAccountsOrData:true},null,2));
