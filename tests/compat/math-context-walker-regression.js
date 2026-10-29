// Compare actual context results against the frozen pre-fast-path walker.
var WizContextWalkerReference=(0,eval)('('+ "function WizMathTextContexts(nodes, body, packed) {\n  var contexts = new Map(), scopes = new Set(), requested = new Set(nodes);\n  var selector = 'p,li,td,th,blockquote,h1,h2,h3,h4,h5,h6,div';\n  var excluded = 'pre,code,script,style,textarea,a,.MathJax,.MathJax_Display,.MathJax_SVG,.wiz-math-source-heading';\n  nodes.forEach(function (text) { scopes.add(text.parentElement.closest(selector) || body); });\n  scopes.forEach(function (scope) {\n    if (scope.closest(excluded)) return;\n    // An element-aware walker can reject whole nested scopes/code subtrees.\n    // Text-only walkers still visit every descendant of those rejected areas.\n    var walker = document.createTreeWalker(scope, NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT, {acceptNode:function (node) {\n      if (node.nodeType === Node.ELEMENT_NODE)\n        return node.matches(excluded) || node.matches(selector) ? NodeFilter.FILTER_REJECT : NodeFilter.FILTER_SKIP;\n      return NodeFilter.FILTER_ACCEPT;\n    }});\n    var parts = [], offsets = [], length = 0, node;\n    while ((node = walker.nextNode())) {\n      if (requested.has(node)) offsets.push({node:node, offset:length});\n      parts.push(node.nodeValue); length += node.nodeValue.length;\n    }\n    var protectedPositions = WizMathProtectedPositions(parts.join(''), packed);\n    offsets.forEach(function (entry) { contexts.set(entry.node, {protectedPositions:protectedPositions, offset:entry.offset}); });\n  });\n  return contexts;\n}" + ')');
function WizRunMathContextWalkerRegression() {
  var checks=[];function check(name,value){checks.push({name:name,ok:!!value});}
  function equal(a,b){return a.size===b.size&&Array.from(a.keys()).every(function(n){var x=a.get(n),y=b.get(n);return !!y&&x.offset===y.offset&&x.protectedPositions.byteLength===y.protectedPositions.byteLength&&x.protectedPositions.every(function(v,i){return v===y.protectedPositions[i];});});}
  function build(count,kind){
    WizClearMathEnvironments();document.body.innerHTML='';var root=document.createElement('div');root.id='walker-scope';document.body.appendChild(root);var spans=[];
    for(var i=0;i<count;i++){var span=document.createElement('span');span.textContent=i===0?'$x+':i===1?'\\ref{target}':i===2?'$':i===count-1?'outside \\ref{target}':'plain';root.appendChild(span);spans.push(span);}
    var nodes=[spans[1].firstChild,spans[count-1].firstChild];
    if(kind){var element;
      if(kind.indexOf('class:')===0){element=document.createElement('span');element.className=kind.slice(6);}
      else if(kind==='svg'||kind==='svg-math'){element=document.createElementNS('http://www.w3.org/2000/svg','svg');if(kind==='svg-math')element.setAttribute('class','MathJax');}
      else if(kind==='foreign-p')element=document.createElementNS('urn:wiznote-test','p');
      else {element=document.createElement(kind);if(kind==='script')element.type='text/plain';}
      element.textContent=kind==='style'?'/* $ unclosed */':'$unclosed';root.insertBefore(element,spans[3]);nodes.push(element.firstChild);
    }
    return {root:root,nodes:nodes,spans:spans};
  }
  function run(name,state,expectedNative){
    var before=document.body.innerHTML,rows=[];
    for(var packed of [false,true]){
      var original=WizContextWalkerReference(state.nodes,document.body,packed),own=Object.getOwnPropertyDescriptor(document,'createTreeWalker'),create=document.createTreeWalker;
      document.createTreeWalker=function(root,type,filter){if(root===state.root)rows.push({type:type,filter:!!filter});return create.apply(this,arguments);};
      var result;try{result=WizMathTextContexts(state.nodes,document.body,packed);}finally{if(own)Object.defineProperty(document,'createTreeWalker',own);else delete document.createTreeWalker;}
      check(name+': '+(packed?'packed':'dense')+' context exact',equal(original,result));
    }
    check(name+': expected native or fallback path',rows.length===2&&rows.every(function(row){return expectedNative?row.type===NodeFilter.SHOW_TEXT&&!row.filter:row.type===(NodeFilter.SHOW_ELEMENT|NodeFilter.SHOW_TEXT)&&row.filter;}));
    check(name+': source exact',document.body.innerHTML===before);
  }
  [8,63,64,65,256].forEach(function(n){run('threshold '+n,build(n),n>=64);});
  ['pre','code','script','style','textarea','a','p','li','td','th','blockquote','h1','h2','h3','h4','h5','h6','div',
    'class:MathJax','class:MathJax_Display','class:MathJax_SVG','class:wiz-math-source-heading','foreign-p','svg','svg-math'].forEach(function(kind){run('excluded/nested '+kind,build(64,kind),kind==='svg');});
  var changing=build(64);run('plain before mutation',changing,true);changing.spans[4].className='MathJax';run('class added',changing,false);changing.spans[4].removeAttribute('class');run('class removed',changing,true);
  var parent=document.createElement('pre');changing.root.before(parent);parent.appendChild(changing.root);var a=WizContextWalkerReference(changing.nodes,document.body,true),b=WizMathTextContexts(changing.nodes,document.body,true);check('excluded ancestor remains excluded',a.size===0&&b.size===0);
  var integrated=build(256),target=document.createElement('blockquote');
  target.innerHTML='<p><strong>定理</strong> \\label{target}</p><p>Body.</p>';document.body.appendChild(target);
  document.body.className='markdown-body wiz-readonly';window.WizNoteCss='/* WizNotePlus scientific math environments v1 */';
  window.WizEditor={getContentHtml:function(){return document.body.innerHTML;},getMarkdownSrc:function(){return document.body.innerHTML;},insertCustomStyle:function(){},removeStyleById:function(){}};
  var clean=document.body.innerHTML,nativeUsed=false,own=Object.getOwnPropertyDescriptor(document,'createTreeWalker'),create=document.createTreeWalker;
  document.createTreeWalker=function(root,type,filter){if(root===integrated.root&&type===NodeFilter.SHOW_TEXT&&!filter)nativeUsed=true;return create.apply(this,arguments);};
  try{WizApplyMathEnvironments();}finally{if(own)Object.defineProperty(document,'createTreeWalker',own);else delete document.createTreeWalker;}
  check('actual decorator uses eligible native path',nativeUsed);
  check('actual decorator keeps compound formula protected and one outer link',document.querySelectorAll('.wiz-math-reference').length===1);
  check('actual decorator serialization exact',WizEditor.getContentHtml()===clean);
  WizClearMathEnvironments();check('actual decorator explicit restore exact',document.body.innerHTML===clean);
  WizClearMathEnvironments();return {checks:checks,passed:checks.filter(function(c){return c.ok;}).length,failures:checks.filter(function(c){return !c.ok;})};
}
