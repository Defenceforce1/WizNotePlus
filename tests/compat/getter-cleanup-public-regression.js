window.WizEditing269={done:false};
(async function(){
 var checks=[],rows=[];function check(n,v){checks.push({name:n,passed:!!v});}
 try{
  if(typeof window.WizSynthetic269Source!=='string')throw Error('Synthetic fixture required');
  var canonical=WizEditor.getMarkdownSrc({unEscapeHtml:true});
  for(var kind of ['outer-query','inner-style']){
    var before=new Set(Array.from(document.body.childNodes)),sentinel=new Error('synthetic public getter '+kind),counts=[],errors=[];
    var createOwn=Object.getOwnPropertyDescriptor(document,'createElement'),styleOwn=Object.getOwnPropertyDescriptor(window,'getComputedStyle'),create=document.createElement,style=window.getComputedStyle;
    if(kind==='outer-query')document.createElement=function(name){var node=Reflect.apply(create,this,arguments);if(String(name).toLowerCase()==='div'){var query=node.querySelectorAll;node.querySelectorAll=function(){if(this.isConnected&&this.style.position==='absolute'&&this.style.left==='-9999px')throw sentinel;return Reflect.apply(query,this,arguments);};}return node;};
    else window.getComputedStyle=function(node){if(node&&node.closest&&node.closest('wiz_tmp_tag')&&node.closest('wiz_tmp_tag').isConnected)throw sentinel;return Reflect.apply(style,this,arguments);};
    try{
      for(var index=0;index<3;index++){var caught=false,same=false;try{WizEditor.getMarkdownSrc({unEscapeHtml:true});}catch(error){caught=true;same=error===sentinel;}errors.push({caught:caught,same: same});counts.push(Array.from(document.body.childNodes).filter(function(n){return !before.has(n);}).length);}
    }finally{
      if(createOwn)Object.defineProperty(document,'createElement',createOwn);else delete document.createElement;
      if(styleOwn)Object.defineProperty(window,'getComputedStyle',styleOwn);else delete window.getComputedStyle;
      Array.from(document.body.childNodes).forEach(function(n){if(!before.has(n))n.remove();});
    }
    check(kind+' original error object retained',errors.every(function(x){return x.caught&&x.same;}));
    check(kind+' owned temporary roots removed',counts.every(function(n){return n===0;}));
    check(kind+' original source nodes remain connected',Array.from(before).every(function(n){return n.parentNode===document.body;}));
    check(kind+' canonical source preserved',WizEditor.getMarkdownSrc({unEscapeHtml:true})===canonical);
    rows.push({kind:kind,counts:counts,errors:errors});
  }
  window.WizEditing269={done:true,checks:checks,failures:checks.filter(function(x){return !x.passed;}).length,rows:rows,sourceIntact:true,onlySyntheticGetterFaults:true,normalAppStarted:false};
 }catch(error){window.WizEditing269={done:true,checks:checks,failures:1,error:String(error)};}
})();
