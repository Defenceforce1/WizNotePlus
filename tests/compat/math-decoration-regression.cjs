#!/usr/bin/env node
'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {chromium}=require('playwright');
const helper=process.env.WIZNOTE_EXPLORE_HELPER_FILE||path.resolve(__dirname,'../../share/files/wizeditor/editorHelper.js');
const output=process.argv[2];
if(!output||!path.isAbsolute(output))throw new Error('Absolute output JSON path required');
const helperSource=fs.readFileSync(helper,'utf8');
const executed=process.env.WIZNOTE_EXPLORE_BOUNDARY_CONTEXT?require('./math-boundary-prototype.cjs')(helperSource):helperSource;
(async()=>{
  const browser=await chromium.launch({executablePath:'/Applications/Google Chrome.app/Contents/MacOS/Google Chrome',headless:true,chromiumSandbox:true});
  try{
    const context=await browser.newContext({serviceWorkers:'block'});
    await context.route('**/*',route=>route.abort());
    const page=await context.newPage(),errors=[];
    page.on('pageerror',error=>errors.push(error.message));
    await page.setContent('<!doctype html><html><head><meta charset="utf-8"></head><body></body></html>');
    await page.addScriptTag({content:executed});
    await page.addScriptTag({path:path.join(__dirname,'math-decoration-regression.js')});
    const result=await page.evaluate(()=>WizRunMathDecorationRegression());
    if(process.env.WIZNOTE_EXPLORE_BOUNDARY_CONTEXT||process.env.WIZNOTE_REQUIRE_MATH_BOUNDARIES){
      await page.addScriptTag({path:path.join(__dirname,'math-boundary-regression.js')});
      result.boundaryRegression=await page.evaluate(()=>WizRunMathBoundaryRegression());
    }
    if(process.env.WIZNOTE_DECORATION_BENCH){
      result.microbenchmarks=await page.evaluate(()=>WizRunMathDecorationMicrobenchmark());
    }
    if(process.env.WIZNOTE_BLOCK_LOOKUP_BENCH){
      result.blockLookupBench=await page.evaluate(()=>WizRunMathBlockLookupBenchmark());
    }
    Object.assign(result,{engine:browser.version(),helper,helperSha256:crypto.createHash('sha256').update(helperSource).digest('hex'),
      executedHelperSha256:crypto.createHash('sha256').update(executed).digest('hex'),pageErrors:errors});
    fs.writeFileSync(output,JSON.stringify(result,null,2)+'\n');
    console.log(JSON.stringify({passed:result.passed,failures:result.failures,
      boundary:result.boundaryRegression&&{passed:result.boundaryRegression.passed,failures:result.boundaryRegression.failures},
      microbenchmarks:result.microbenchmarks,pageErrors:errors}));
    process.exitCode=result.failures.length||errors.length||result.boundaryRegression?.failures.length
      ||result.blockLookupBench?.some(record=>!record.sourceNumberAndDestinationSame)?1:0;
  }finally{await browser.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
