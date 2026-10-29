// Differential regression of the actual HTML-to-Markdown function, executed in
// a synthetic browser DOM. The frozen reference and candidate are provided by
// source-level extraction, never by reading editor runtime internals.
function WizHtmlToMarkdownLeafRegression(referenceSource, candidateSource) {
  // Frozen phase290 getter helper, from the reviewed GPL Wiz source module506.
  referenceSource = referenceSource || "function(e,t){var r,o,i;if(t=t||{},3===e.nodeType)return i=e.nodeValue,t.isInA&&(i=i.replace(/([\\[\\]])/g,\"\\\\$1\")),void(e.nodeValue=i);if(1===e.nodeType){t.isInA||(t.isInA=n.isTag(e,\"a\"));for(var a=(o=e.childNodes).length-1;a>=0;a--)n.html2Markdown(o[a],{isInA:t.isInA});if(!n.getParentByTagName(e,\"table\"))if(n.isTag(e,\"a\"))i=e.innerText,(r=n.env.doc.createElement(\"span\")).innerText=\"[\"+i+\"](\"+e.href+\")\",n.before(r,e),n.remove(e);else if(n.isTag(e,\"img\")&&!n.hasClass(e,d.CLASS.TODO_CHECKBOX))i=e.alt||u.getFileNameByUrl(e.src),(r=n.env.doc.createElement(\"span\")).innerText=\"![\"+i+\"](\"+e.src+\")\",n.before(r,e),n.remove(e);else if(n.isTag(e,[\"h1\",\"h2\",\"h3\",\"h4\",\"h5\",\"h6\",\"li\"]))i=e.innerText,(r=n.env.doc.createElement(\"div\")).innerText=i,n.before(r,e),n.remove(e);else if(n.isTag(e,[\"ol\",\"ul\"])){for(;e.lastChild;)n.after(e.lastChild,e);n.remove(e)}}}";

  var checks=[],observations=[];
  function check(name,ok){checks.push({name:name,passed:!!ok});}
  function make(source){var counts={tag:0,parents:0};var n={env:{doc:document},isTag:function(e,t){++counts.tag;return e&&e.nodeType===1&&(Array.isArray(t)?t:[t]).some(function(x){return e.tagName.toLowerCase()===x;});},getParentByTagName:function(e,t){++counts.parents;for(var p=e.parentNode;p&&p.nodeType===1;p=p.parentNode)if(p.tagName.toLowerCase()===t)return p;return null;},hasClass:function(e,c){return e.classList.contains(c);},before:function(a,b){b.parentNode.insertBefore(a,b);},after:function(a,b){b.parentNode.insertBefore(a,b.nextSibling);},remove:function(e){e.parentNode.removeChild(e);}};
    var d={CLASS:{TODO_CHECKBOX:'wiz-todo-checkbox'}},u={getFileNameByUrl:function(s){return s.split('/').pop();}};
    n.html2Markdown=(new Function('n','d','u','return ('+source+');'))(n,d,u);return {run:n.html2Markdown,counts:counts};}
  function tree(e){return {type:e.nodeType,name:e.nodeName,value:e.nodeType===3?e.nodeValue:null,attributes:e.nodeType===1?Array.from(e.attributes).map(function(a){return[a.name,a.value];}):[],children:Array.from(e.childNodes).map(tree)};}
  var fixtures=[
    {name:'plain-br',html:'Alpha<br>Beta<BR/>Gamma<br data-x="1">'},
    {name:'anchor-brackets',html:'<a href="https://example.invalid/path">[A]<br>[B]</a>'},
    {name:'table-pre-hidden',html:'<table><tbody><tr><td>A<br>B</td></tr></tbody></table><pre>A\t<br><span style="display:none">hidden</span>B</pre>'},
    {name:'heading-list',html:'<h2>A<br>B</h2><ol><li>C<br>D<ul><li>E<br>F</li></ul></li></ol>'},
    {name:'images-and-checkbox',html:'<img alt="A" src="data:image/png;base64,AA=="><br><img class="wiz-todo-checkbox" src="data:image/png;base64,AA=="><br>'},
    {name:'empty-and-entities',html:'<br><br><span>&amp; &lt; &#13; &#160; 中文 😀</span><br>'},
    {name:'pre-existing-options',html:'[a]<br>[b]',options:{isInA:true,other:7}},
    {name:'programmatic-br-child',html:'<a href="#x"></a>',setup:function(root){var br=document.createElement('br');br.appendChild(document.createTextNode('[child]'));root.firstChild.appendChild(br);}},
    {name:'svg-br',html:'',setup:function(root){root.appendChild(document.createElementNS('http://www.w3.org/2000/svg','br'));}},
    {name:'thousand-br',html:('line<br>').repeat(1000)}
  ];
  for(var f of fixtures){var rows=[];for(var source of [referenceSource,candidateSource]){var root=document.createElement('div');root.innerHTML=f.html;if(f.setup)f.setup(root);document.body.appendChild(root);var api=make(source),options=Object.assign({},f.options||{}),result;try{result=api.run(root,options);rows.push({tree:tree(root),options:options,returnsUndefined:result===undefined,counts:api.counts});}finally{root.remove();}}
    check(f.name+' DOM and text exact',JSON.stringify(rows[0].tree)===JSON.stringify(rows[1].tree));check(f.name+' options and return exact',JSON.stringify(rows[0].options)===JSON.stringify(rows[1].options)&&rows[0].returnsUndefined&&rows[1].returnsUndefined);observations.push({name:f.name,reference:rows[0].counts,candidate:rows[1].counts});}
  var many=observations[observations.length-1];check('1000 unchanged leaf BR ancestor walks removed',many.reference.parents-many.candidate.parents===1000);check('1000 unchanged leaf BR tag checks removed',many.reference.tag>many.candidate.tag);
  return {checks:checks,failures:checks.filter(function(c){return !c.passed;}).length,observations:observations,sourceLevelActualFunctions:true,noNativeMemoryOrTimingClaim:true};
}
