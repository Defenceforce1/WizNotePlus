#!/usr/bin/env node
'use strict';
// Isolated DOM/scanner tests. No Markdown parser, MathJax queue or Qt is implied.
const fs=require('node:fs'),path=require('node:path');
const {chromium}=require('playwright');
const helper=process.env.WIZNOTE_EXPLORE_HELPER_FILE||path.resolve(__dirname,'../../share/files/wizeditor/editorHelper.js');
if(fs.readFileSync(helper,'utf8').includes('function WizMathProtectedPositions('))
  throw new Error('Historical prototype study requires the 14.4.16 helper via WIZNOTE_EXPLORE_HELPER_FILE; use math-decoration-regression.cjs for current candidates');
const output=process.argv[2];
if(!output||!path.isAbsolute(output))throw new Error('Absolute output path required');
const cases=[
  ['prose','See \\ref{thm:a}.',1],
  ['standalone-dollar','$\\ref{thm:a}$',1],
  ['standalone-parens','\\(\\ref{thm:a}\\)',1],
  ['compound-dollar','$x+\\ref{thm:a}$',0],
  ['compound-parens','\\(x+\\ref{thm:a}\\)',0],
  ['compound-display-dollar','$$x+\\ref{thm:a}$$',0],
  ['compound-display-brackets','\\[x+\\ref{thm:a}\\]',0],
  ['standalone-display-dollar','$$\\ref{thm:a}$$',0],
  ['standalone-display-brackets','\\[\\ref{thm:a}\\]',0],
  ['prose-after-formula','\\(x+1\\) see \\ref{thm:a}',1],
  ['prose-after-display','$$x+1$$ see \\ref{thm:a}',1],
  ['split-inline-formula','\\(x+<em>\\ref{thm:a}</em>\\)',0],
  ['split-display-formula','$$x+<span>\\ref{thm:a}</span>$$',0],
  ['escaped-dollar','Cost \\$10; see \\ref{thm:a}.',1],
  ['pre','<pre>\\ref{thm:a}</pre>',0],
  ['code','<code>\\ref{thm:a}</code>',0],
  ['existing-link','<a href="#original">\\ref{thm:a}</a>',0],
  ['mathjax','<span class="MathJax">\\ref{thm:a}</span>',0],
  ['script','<script type="text/plain">\\ref{thm:a}</script>',0],
  ['textarea','<textarea>\\ref{thm:a}</textarea>',0],
];
const result={kind:'DOM reference-scanner exploration, NOT parser/Qt integration',cases:[],benchmarks:[],roundTrips:[]};
async function prepare(page,html){
  await page.setContent('<!doctype html><html><head><meta charset="utf-8"></head><body class="wiz-editor-body markdown-body wiz-readonly">'+html+'</body></html>');
  await page.addScriptTag({path:helper});
  await page.evaluate(()=>{
    window.WizNoteCss='/* WizNotePlus scientific math environments v1 */';
    // Only exercise temporary-decoration serialization, not the real editor API.
    window.WizEditor={getContentHtml:()=>document.body.innerHTML,getMarkdownSrc:()=>document.body.innerHTML};
  });
}
const target='<blockquote><p><strong>定理</strong> \\label{thm:a}</p><p>Target.</p></blockquote>';
(async()=>{
  const browser=await chromium.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:true,chromiumSandbox:true});
  const context=await browser.newContext({serviceWorkers:'block'});
  await context.route('**/*',route=>route.abort());
  try{
    const page=await context.newPage();
    for(const [name,input,expected]of cases){
      await prepare(page,'<p>'+input+'</p>'+target);
      const actual=await page.evaluate(()=>{const before=document.body.innerHTML;WizApplyMathEnvironments();return{
        links:document.querySelectorAll('.wiz-math-reference').length,
        serializedSame:WizEditor.getContentHtml()===before};});
      // Start the candidate from the same untouched DOM, not the baseline's
      // already-split text nodes (which hide delimiters from a local scanner).
      await prepare(page,'<p>'+input+'</p>'+target);
      const prototype=await page.evaluate(()=>{
        // Diagnostic delimiter scanner only; not a new Markdown/TeX parser.
        window.WizProbeInsideMath=function(source,index){
          let delimiter=null;
          for(let i=0;i<index;i++){
            const c=source[i],next=source[i+1];
            if(c==='\\'){
              if(next==='\\'||next==='$'){i++;continue;}
              if(!delimiter&&(next==='('||next==='[')){delimiter=next==='('?')':']';i++;continue;}
              if(delimiter===next&&(next===')'||next===']')){delimiter=null;i++;continue;}
            }else if(c==='$'){
              const token=next==='$'?'$$':'$';
              if(!delimiter)delimiter=token;else if(delimiter===token)delimiter=null;
              if(token==='$$')i++;
            }
          }
          return delimiter!==null;
        };
        let code=WizApplyMathEnvironments.toString();
        const lines=code.split('\n');
        const index=lines.findIndex(line=>line.includes('if (result[3] && (source.slice(0, result.index)'));
        if(index<0)throw new Error('Math-boundary source anchor mismatch');
        lines[index]='if (WizProbeInsideMath(source,result.index)) continue;';
        WizApplyMathEnvironments=(0,eval)('('+lines.join('\n')+')');
        WizApplyMathEnvironments();
        const prototypeLinks=document.querySelectorAll('.wiz-math-reference').length;
        // A reference may live in an inline span/emphasis node whose delimiters
        // are siblings. A text-node-only lexer cannot see those delimiters.
        lines[index]='if (WizProbeInsideMath(mathContext,mathOffset+result.index)) continue;';
        let domCode=lines.join('\n');
        const contextCode=`var source = text.nodeValue;
          var scope=text.parentElement.closest('p,li,td,th,blockquote,h1,h2,h3,h4,h5,h6,div')||body;
          var contextWalker=document.createTreeWalker(scope,NodeFilter.SHOW_TEXT,{acceptNode:function(n){
            return n.parentElement&&!n.parentElement.closest('pre,code,script,style,textarea,a,.MathJax,.MathJax_Display,.MathJax_SVG,.wiz-math-source-heading')?NodeFilter.FILTER_ACCEPT:NodeFilter.FILTER_REJECT;
          }}),mathContext='',mathOffset=0,contextNode;
          while((contextNode=contextWalker.nextNode())){if(contextNode===text)mathOffset=mathContext.length;mathContext+=contextNode.nodeValue;}`;
        if(!domCode.includes('var source = text.nodeValue;'))throw new Error('DOM context source anchor mismatch');
        domCode=domCode.replace('var source = text.nodeValue;',contextCode);
        WizApplyMathEnvironments=(0,eval)('('+domCode+')');WizApplyMathEnvironments();
        return {prototypeLinks,domAwarePrototypeLinks:document.querySelectorAll('.wiz-math-reference').length};
      });
      result.cases.push({name,expectedLinks:expected,...actual,...prototype,matchesExpected:actual.links===expected,
        prototypeMatchesExpected:prototype.prototypeLinks===expected,domAwareMatchesExpected:prototype.domAwarePrototypeLinks===expected});
    }
    for(const count of [500,1500])for(const variant of ['baseline','skip-empty','skip-empty-map']){
      const html=Array.from({length:count},(_,i)=>`<p>See \\ref{thm:${i}}.</p><blockquote><p><strong>定理</strong> \\label{thm:${i}}</p><p>Body ${i}.</p></blockquote>`).join('');
      await prepare(page,html);
      const data=await page.evaluate(({variant})=>{
        const clean=document.body.innerHTML;
        if(variant!=='baseline'){
          let code=WizApplyMathEnvironments.toString();
          code=code.replace('fragment.appendChild(document.createTextNode(source.slice(offset, result.index)));',
            'if(result.index>offset)fragment.appendChild(document.createTextNode(source.slice(offset,result.index)));');
          code=code.replace('fragment.appendChild(document.createTextNode(source.slice(offset))); text.replaceWith(fragment);',
            'if(offset<source.length)fragment.appendChild(document.createTextNode(source.slice(offset)));text.replaceWith(fragment);');
          if(!code.includes('if(offset<source.length)'))throw new Error('Prototype source anchor mismatch');
          if(variant==='skip-empty-map'){
            code=code.replace('var labels = Object.create(null), duplicates = Object.create(null);',
              'var labels = Object.create(null), duplicates = Object.create(null);var recordsByElement=new Map();');
            code=code.replace('WizMathDecoration.blocks.push(record);','WizMathDecoration.blocks.push(record);recordsByElement.set(block,record);');
            code=code.replace('WizMathDecoration.blocks.find(function (item) { return item.element === block; })','recordsByElement.get(block)');
            if(!code.includes('recordsByElement.get(block)'))throw new Error('Map candidate source anchor mismatch');
          }
          WizApplyMathEnvironments=(0,eval)('('+code+')');
        }
        function countTexts(){const walker=document.createTreeWalker(document.body,NodeFilter.SHOW_TEXT);let n=0;while(walker.nextNode())n++;return n;}
        WizApplyMathEnvironments();const initial=countTexts(),times=[];
        for(let i=0;i<30;i++){const start=performance.now();WizApplyMathEnvironments();times.push(performance.now()-start);}
        const final=countTexts(),same=clean===WizEditor.getContentHtml();
        return{initialTextNodes:initial,finalTextNodes:final,growth:final-initial,iterationMs:times,serializationSame:same};
      },{variant});
      result.benchmarks.push({count,variant,...data});
    }
    // Deterministic mixed block headings, ordinary quotes, nested code and Unicode.
    for(let seed=0;seed<32;seed++){
      const kinds=['定理','定义','证明','引理','例','注'];
      const html=Array.from({length:8},(_,i)=>`<p>中文 &amp; α \\ref{case:${seed}:${i}}</p><blockquote class="original-${i}"><p><strong>${kinds[(seed+i)%kinds.length]}${i===3?' 7':''}（测试）</strong> \\label{case:${seed}:${i}}</p><p>Body <code>\\ref{case:${seed}:${i}}</code>.</p></blockquote>`).join('');
      await prepare(page,html);
      const baseline=await page.evaluate(seed=>{
        const before=document.body.innerHTML;WizApplyMathEnvironments();let equal=true,firstDifference=null;
        for(let i=0;i<8;i++){
          const actual=WizEditor.getContentHtml();equal=equal&&actual===before;
          if(actual!==before&&!firstDifference){let j=0;while(j<before.length&&before[j]===actual[j])j++;
            firstDifference={iteration:i,offset:j,expected:before.slice(Math.max(0,j-45),j+160),actual:actual.slice(Math.max(0,j-45),j+160)};}
        }
        return{seed,equal,firstDifference,codeUntouched:document.querySelectorAll('code .wiz-math-reference').length===0};
      },seed);
      await prepare(page,html);
      const prototype=await page.evaluate(()=>{
        // Flush pending CSSOM serialization before restoring an absent style
        // attribute. Chrome 155 otherwise materializes an empty style attribute.
        let code=WizClearMathEnvironments.toString();
        const original='if (record.attrs[name] === null) record.element.removeAttribute(name);';
        const replacement='if(record.attrs[name]===null){if(name==="style")record.element.getAttribute(name);record.element.removeAttribute(name);}';
        if(!code.includes(original))throw new Error('Style restore source anchor mismatch');
        code=code.replace(original,replacement);WizClearMathEnvironments=(0,eval)('('+code+')');
        const before=document.body.innerHTML;WizApplyMathEnvironments();let equal=true;
        for(let i=0;i<8;i++)equal=equal&&WizEditor.getContentHtml()===before;
        return{styleFlushPrototypeEqual:equal};
      });
      result.roundTrips.push({...baseline,...prototype});
    }
    await page.close();
  }finally{await context.close();await browser.close();}
  result.boundaryMismatches=result.cases.filter(x=>!x.matchesExpected);
  result.prototypeBoundaryMismatches=result.cases.filter(x=>!x.prototypeMatchesExpected);
  result.domAwareBoundaryMismatches=result.cases.filter(x=>!x.domAwareMatchesExpected);
  result.roundTripFailures=result.roundTrips.filter(x=>!x.equal||!x.codeUntouched);
  result.styleFlushPrototypeFailures=result.roundTrips.filter(x=>!x.styleFlushPrototypeEqual);
  fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n');
  console.log(JSON.stringify({cases:result.cases.length,boundaryMismatches:result.boundaryMismatches,prototypeBoundaryMismatches:result.prototypeBoundaryMismatches,
    domAwareBoundaryMismatches:result.domAwareBoundaryMismatches,
    benchmarks:result.benchmarks.map(b=>({...b,iterationMs:undefined})),roundTrips:result.roundTrips.length,
    roundTripFailureCount:result.roundTripFailures.length,styleFlushPrototypeFailures:result.styleFlushPrototypeFailures.length,
    firstRoundTripFailure:result.roundTripFailures[0]},null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
