// Product translation dictionaries and exact init expression in isolated VMs.
// No DOM, account, HTTP backend, plural-format migration or editor access.
const fs=require('fs'),vm=require('vm'),crypto=require('crypto');
const [before,after,report]=process.argv.slice(2);if(!report)throw Error('usage: before/files after/files report');
let checks=0,failures=0;const check=(v,label)=>{checks++;failures+=!v;console.log((v?'PASS ':'FAIL ')+label);};
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
(async()=>{
 const records=[];
 for(const [variant,resources]of [['before',before],['after',after]]){
  const code=fs.readFileSync(resources+'/welcomepage/js/index.js','utf8'),library=fs.readFileSync(resources+'/welcomepage/js/i18next.min.js');
  const start=code.indexOf('        const t = await i18next.init({'),end=code.indexOf('        });',start);
  if(start<0||end<0)throw Error('exact product init block');const initialization=code.slice(start,end+'        });'.length);
  for(const language of variant==='before'?['zh_CN','en_US']:['zh_CN','en_US','zh_TW','zh-Hant-TW','fr_FR','']){
   const logs=[],context={setTimeout,clearTimeout,lang:language,document:{},console:{log:(...a)=>logs.push(a.join(' ')),warn:(...a)=>logs.push(a.join(' ')),error:(...a)=>logs.push(a.join(' '))},$(){return{ready(){}};}};
   context.window=context;context.self=context;vm.createContext(context);vm.runInContext(library.toString('utf8'),context);vm.runInContext(code,context);
   if(variant==='after')context.lang=vm.runInContext('welcomeUiLocale(lang)',context);
   const expectedLanguage=variant==='after'?(/^zh(?:[-_]|$)/i.test(language)?'zh_CN':'en_US'):language;
   const result=await vm.runInContext('(async()=>{'+initialization+'return {ready:typeof t==="function",language:(i18next.default||i18next).language,expected:'+expectedLanguage+',values:Object.fromEntries(Object.keys(en_US).map(key=>[key,t(key)]))};})()',context);
   check(result.ready,variant+' '+language+' awaited init returns translation function');
   check(result.language===expectedLanguage,variant+' '+language+' selects one of two supported UI locales');
   for(const [key,value]of Object.entries(result.expected))check(result.values[key]===value,variant+' '+language+' '+key+' translation retained');
   check(logs.length===0,variant+' '+language+' has no initialization warnings or support banners');
   records.push({variant,language,resolvedLanguage:result.language,values:result.values,librarySha256:hash(library),sourceSha256:hash(code),logs});
  }
 }
 check(records.filter(r=>r.variant==='before').every(a=>JSON.stringify(a.values)===JSON.stringify(records.find(b=>b.variant==='after'&&b.language===a.language).values)),'all used welcome and task translations match across versions');
 const afterCode=fs.readFileSync(after+'/welcomepage/js/index.js','utf8'),afterHtml=fs.readFileSync(after+'/welcomepage/index.html','utf8');
 check(!afterCode.includes('const zh_TW')&&!afterCode.includes('"zh_TW":')&&!afterHtml.includes('datepicker-zh-TW.js'),'Traditional welcome dictionary and script reference removed');
 check(!fs.existsSync(after+'/welcomepage/js/datepicker-zh-TW.js'),'obsolete Traditional datepicker source removed');
 const result={checks,failures,records,syntheticOnly:true,actualInitAndDictionaries:true,noBackendInstalled:true,supportedUiLanguages:['zh_CN','en_US'],legacyAliasesOnlyFallback:true};fs.writeFileSync(report,JSON.stringify(result,null,2)+'\n',{flag:'wx'});if(failures)process.exitCode=1;
})().catch(e=>{console.error(e);process.exitCode=1;});
