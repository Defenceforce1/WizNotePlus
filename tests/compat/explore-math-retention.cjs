'use strict';
const crypto=require('node:crypto');
const hash=text=>crypto.createHash('sha256').update(text).digest('hex');

// Only an isolated renderer's temporary iframe. No normal note, account,
// network, persistent profile, browser screenshot or native GPU claim.
module.exports=async function mathRetention(session,helper,expectedBaselineSha){
  if(!/^[a-f0-9]{64}$/.test(expectedBaselineSha||''))throw Error('Explicit verified baseline helper hash required');
  const baseline=helper.replace(/^  var blockRecords = new Map\(\);\r?\n/m,'')
    .replace(/^    blockRecords\.set\(block, record\);\r?\n/m,'')
    .replace('var record = blockRecords.get(block);',
      'var record = WizMathDecoration.blocks.find(function (item) { return item.element === block; });');
  if(baseline===helper||hash(baseline)!==expectedBaselineSha)throw Error('Derived baseline bytes do not match the verified old helper');
  const evaluate=async expression=>{
    const reply=await session.call('Runtime.evaluate',{expression,returnByValue:true});
    if(reply.exceptionDetails)throw Error(reply.exceptionDetails.exception?.description||reply.exceptionDetails.text);
    return reply.result.value;
  };
  const collect=async()=>{
    await session.call('HeapProfiler.collectGarbage');
    await session.call('HeapProfiler.collectGarbage');
    return {heap:await session.call('Runtime.getHeapUsage'),dom:await session.call('Memory.getDOMCounters')};
  };
  await session.call('HeapProfiler.enable');
  const result={kind:'isolated math decoration DOM/JS retention; not whole-App or GPU memory',
    baselineHelperSha256:hash(baseline),candidateHelperSha256:hash(helper),blocks:200,cycles:150,variants:[]};
  for(const [variant,source]of[['array-baseline',baseline],['map-candidate',helper]]){
    const initial=await collect();
    let record={variant,samples:[]};
    try{
      await evaluate(`(()=>{
        if(window.__WizMathRetention)throw Error('Previous retention fixture still live');
        const frame=document.createElement('iframe');frame.style.cssText='position:fixed;left:-20000px;width:850px;height:600px';
        document.documentElement.appendChild(frame);
        const w=frame.contentWindow,d=frame.contentDocument;
        d.open();d.write('<!doctype html><html><head></head><body class="markdown-body wiz-readonly"></body></html>');d.close();
        w.eval(${JSON.stringify(source)});
        d.body.innerHTML=Array.from({length:200},(_,i)=>'<p>See \\\\ref{retention:'+i+'}.</p>'
          +'<blockquote><p><strong>定理</strong> \\\\label{retention:'+i+'}</p><p>Synthetic body '+i+'.</p></blockquote>').join('');
        const clean=d.body.innerHTML;
        w.WizNoteCss='/* WizNotePlus scientific math environments v1 */';
        w.WizEditor={getContentHtml:()=>d.body.innerHTML,getMarkdownSrc:()=>d.body.innerHTML};
        let sourceSame=true;
        window.__WizMathRetention={frame,run(count){
          for(let i=0;i<count;i++){w.WizApplyMathEnvironments();sourceSame=sourceSame&&w.WizEditor.getContentHtml()===clean;}
          const walker=d.createTreeWalker(d.body,w.NodeFilter.SHOW_ALL);let nodes=1;while(walker.nextNode())nodes++;
          return {sourceSame,nodes,blocks:d.querySelectorAll('.wiz-math-block').length,links:d.querySelectorAll('.wiz-math-reference').length};
        }};
        return true;
      })()`);
      for(const cycle of[0,25,50,75,100,125,150]){
        const local=await evaluate(`window.__WizMathRetention.run(${cycle===0?2:25})`);
        record.samples.push({cycle,...local,...await collect()});
      }
      const first=record.samples[0];
      record.stableLocalDom=record.samples.every(s=>s.sourceSame&&s.nodes===first.nodes&&s.blocks===200&&s.links===200);
      record.heapDeltaBytes=record.samples.at(-1).heap.usedSize-first.heap.usedSize;
    }finally{
      await evaluate('(()=>{const state=window.__WizMathRetention;if(state){state.frame.remove();delete window.__WizMathRetention;}return true;})()');
      record.cleanup={...await collect(),fixtureAbsent:await evaluate('!window.__WizMathRetention')};
      record.initial=initial;
      record.documentsRestored=record.cleanup.dom.documents===initial.dom.documents;
      result.variants.push(record);
    }
  }
  result.passed=result.variants.length===2&&result.variants.every(v=>v.stableLocalDom&&v.documentsRestored&&v.cleanup.fixtureAbsent);
  return result;
};
