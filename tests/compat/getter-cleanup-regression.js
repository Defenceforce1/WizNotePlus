// Source-level frozen-reference differential on synthetic DOM, not editor
// runtime introspection. No account, real note or global DOM prototype changes.
function WizGetterCleanupRegression(referenceOuter,candidateOuter,referenceInner,candidateInner){
  // Frozen pre-cleanup functions from reviewed GPL Wiz modules217 and506.
  referenceOuter=referenceOuter||"function(e){var n,r;v&&v.saveToText(),e=e||{},r=m.readonly?S.replace(/\\n/g,\"<div><br/></div>\"):m.body.innerHTML;var i=m.doc.createElement(\"div\");e.escapeHtml&&(r=r.replace(/&/g,\"&amp;\")),i.innerHTML=r,b.html2Markdown(i,{}),b.css(i,{opacity:0,position:\"absolute\",top:\"-9999px\",left:\"-9999px\",width:\"max-content\",height:\"1px\",overflow:\"hidden\"}),m.body.appendChild(i),v&&v.clearCodeForMarkdown(i);for(var a=i.querySelectorAll(o.TAG.TMP_TAG),l=a.length-1;l>=0;l--)b.remove(a[l]);return n=t.getMarkdownSrc(i),n=b.restoreImgPath(n),m.body.removeChild(i),i=null,n}";
  referenceInner=referenceInner||"function(e){var t=\"\",r=function(e,t){if(e.style[t])return e.style[t];var r=e.currentStyle||e.ownerDocument.defaultView.getComputedStyle(e,null);return n.isTag(e,\"script\")?\"none\":r[t]?\"block\"===r[t]&&n.isTag(e,\"td\")?\"feaux-inline\":r[t]:n.isTag(e,[\"li\",\"p\",\"tr\"])?\"block\":e.style[t]},o=e.cloneNode(!0);o.innerHTML=o.innerHTML.replace(/<br[\\/]?>/gi,\"{{WIZ_NEW_LINE_STR}}\");var i=n.env.doc.createElement(d.TAG.TMP_TAG);n.css(i,{opacity:0,width:\"1px\",height:\"1px\",position:\"absolute\",top:\"-10px\",left:\"-10px\",overflow:\"hidden\"}),i.appendChild(o),n.env.body.appendChild(i),function(e){var t=[];n.eachChild(e,(function(e){return!n.getParentByTagName(e,\"pre\")&&3===e.nodeType&&function(e){return!/[^\\t\\n\\r ]/.test(e.nodeValue)}(e)&&t.push(e),e.hasChildNodes()}),(function(){}));for(var r=0;r<t.length;r++)t[r].parentNode.removeChild(t[r])}(o);var a,l=(a=function e(o){if(/pre/.test(r(o,\"whiteSpace\"))){var i=o.innerText||o.textContent;return t+=i.replace(/\\t/g,\" \")+\"\\n\",\"\"}if(\"none\"===r(o,\"display\"))return\"\";for(var a=function(e){var t=r(e,\"display\")||\"feaux-inline\";return\"table-row,block,list-item\".indexOf(t)>-1}(o),l=a?\"\\n\":\"\",s=0;s<o.childNodes.length;s++){var c=o.childNodes[s];3===c.nodeType&&(t+=c.nodeValue),!n.isTag(c,\"br\")||a&&!c.nextSibling||(t+=\"\\n\"),c.childNodes.length&&e(c)}return t+=l}(o))?a.replace(/[\\t]+/gm,\"    \").replace(h,\"\\n\").replace(p,\"\\n\"):\"\";return n.remove(i),l}";

  var checks=[],rows=[];function check(name,value){checks.push({name:name,passed:!!value});}
  function run(outerSource,innerSource,spec){
    var host=document.createElement('section');host.innerHTML='A<br>B';document.body.appendChild(host);
    var initial=new Set(Array.from(host.childNodes)),primary=spec.value,cleanup=new Error('synthetic cleanup fault'),errors=[],counts=[];
    var originalRemove=host.removeChild;
    if(spec.failDomCleanup)host.removeChild=function(){throw cleanup;};
    var n={env:{doc:document,body:host},css:function(e,values){Object.keys(values).forEach(function(k){e.style[k]=values[k];});},remove:function(e){if(spec.failHelperRemove)throw cleanup;if(e.parentNode)e.parentNode.removeChild(e);},isTag:function(e,t){return e&&e.nodeType===1&&(Array.isArray(t)?t:[t]).some(function(v){return e.tagName.toLowerCase()===v;});},getParentByTagName:function(e,tag){if(spec.failInnerWalk)throw primary;for(var p=e.parentNode;p&&p.nodeType===1;p=p.parentNode)if(p.tagName.toLowerCase()===tag)return p;return null;},eachChild:function(root,enter,leave){function walk(e){Array.from(e.childNodes).forEach(function(c){if(enter(c))walk(c);leave(c);});}walk(root);}};
    var d={TAG:{TMP_TAG:'wiz_tmp_tag'}},h=/\{\{WIZ_NEW_LINE_STR\}\}/g,p=/\r\n?/g;
    var inner=(new Function('n','d','h','p','return ('+innerSource+');'))(n,d,h,p);
    var b={html2Markdown:function(){},css:n.css,remove:n.remove,restoreImgPath:function(s){if(spec.failRestore)throw primary;return s;}};
    var t={getMarkdownSrc:function(node){if(spec.failOuterRead)throw primary;if(spec.nested)return inner(node);return 'canonical';}},v=null,m={doc:document,body:host,readonly:spec.readonly!==false},S='A<br>B',o={TAG:{TMP_TAG:'wiz_tmp_tag'}};
    var outer=(new Function('v','m','S','b','t','o','return ('+outerSource+');'))(v,m,S,b,t,o),values=[];
    try{
      for(var k=0;k<(spec.retries||1);k++){
        var caught=false,error;
        try{values.push(spec.innerOnly?inner(host):outer({unEscapeHtml:true}));}catch(e){caught=true;error=e;}
        errors.push({caught:caught,primarySame:error===primary,cleanupSame:error===cleanup});
        counts.push(Array.from(host.childNodes).filter(function(e){return !initial.has(e);}).length);
      }
      return {errors:errors,retainedRootCounts:counts,values:values,initialNodesConnected:Array.from(initial).every(function(n){return n.parentNode===host;})};
    }finally{host.removeChild=originalRemove;host.remove();}
  }
  var specs=[{name:'normal-outer'},{name:'normal-inner',innerOnly:true},{name:'outer-read-error',failOuterRead:true,value:new Error('synthetic read fault')},{name:'restore-error',failRestore:true,value:new Error('synthetic path fault')},{name:'inner-walk-error',innerOnly:true,failInnerWalk:true,value:new Error('synthetic walk fault')},{name:'nested-walk-error',nested:true,failInnerWalk:true,value:new Error('synthetic nested fault')},{name:'helper-cleanup-error',innerOnly:true,failHelperRemove:true,value:null},{name:'source-error-plus-broken-dom-cleanup',failOuterRead:true,failDomCleanup:true,value:new Error('synthetic primary preserved')},{name:'normal-but-broken-dom-cleanup',failDomCleanup:true,value:null},{name:'three-failed-retries',nested:true,failInnerWalk:true,retries:3,value:new Error('synthetic retry fault')}];
  [undefined,null,false,0,''].forEach(function(value,index){specs.push({name:'falsy-'+index,failOuterRead:true,value:value});});
  for(var spec of specs){var a=run(referenceOuter,referenceInner,spec),b=run(candidateOuter,candidateInner,spec);rows.push({name:spec.name,reference:a,candidate:b});check(spec.name+' same returned values',JSON.stringify(a.values)===JSON.stringify(b.values));check(spec.name+' original error identity preserved',JSON.stringify(a.errors)===JSON.stringify(b.errors));check(spec.name+' initial source nodes remain',a.initialNodesConnected&&b.initialNodesConnected);if(!spec.failDomCleanup)check(spec.name+' candidate has no owned attached roots',b.retainedRootCounts.every(function(n){return n===0;}));}
  var nested=rows.find(function(r){return r.name==='nested-walk-error';});check('nested reference leaks both owned roots',nested.reference.retainedRootCounts[0]===2);
  var retry=rows.find(function(r){return r.name==='three-failed-retries';});check('reference retry growth is2/4/6 roots',retry.reference.retainedRootCounts.join(',')==='2,4,6');check('candidate retries remain0/0/0 roots',retry.candidate.retainedRootCounts.join(',')==='0,0,0');
  return {checks:checks,failures:checks.filter(function(c){return !c.passed;}).length,rows:rows,onlySyntheticDomAndSourceFunctions:true,noOriginalSaveFailureUiInvestigated:true,noNativeHighWaterImprovementClaim:true};
}
