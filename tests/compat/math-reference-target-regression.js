// Synthetic DOM checks for the no-target fast path. The real editor, MathJax
// geometry and PDF checks remain separate. Pass false for the pre-change helper.
function WizRunMathReferenceTargetRegression(expectFastPath) {
  if (expectFastPath === undefined) expectFastPath = true;
  var checks = [], originalContexts = WizMathTextContexts, calls = 0;
  function check(name, value) { checks.push({name:name,ok:!!value}); }
  WizMathTextContexts = function () { ++calls; return originalContexts.apply(this, arguments); };
  function setup(html) {
    WizClearMathEnvironments(); document.body.className = 'markdown-body wiz-readonly';
    document.body.innerHTML = html; window.WizNoteCss = '/* WizNotePlus scientific math environments v1 */';
    window.WizEditor = {getContentHtml:function () {return document.body.innerHTML;},
      getMarkdownSrc:function () {return document.body.innerHTML;},
      insertCustomStyle:function () {},removeStyleById:function () {}};
    calls = 0; return document.body.innerHTML;
  }
  function block(label) { return '<blockquote><p><strong>定理</strong>'
    + (label ? ' \\label{' + label + '}' : '') + '</p><p>Body.</p></blockquote>'; }
  var cases = [
    {name:'no theorem label',html:'<p>See \\ref{missing}.</p>',targets:false,links:0,blocks:0},
    {name:'unlabelled theorem still numbered',html:'<p>See \\ref{missing}.</p>'+block(),targets:false,links:0,blocks:1},
    {name:'all labels duplicate',html:'<p>\\ref{target}</p>'+block('target')+block('target'),targets:false,links:0,blocks:2},
    {name:'all destination anchors collide',html:'<div id="wiz-math-target">Old</div><p>\\ref{target}</p>'+block('target'),targets:false,links:0,blocks:1},
    {name:'valid target',html:'<p>See \\ref{target}.</p>'+block('target'),targets:true,links:1,blocks:1},
    {name:'valid and duplicate targets mixed',html:'<p>\\ref{target} \\ref{dup}</p>'+block('target')+block('dup')+block('dup'),targets:true,links:1,blocks:3},
    {name:'code reference remains excluded',html:'<pre>\\ref{target}</pre>'+block('target'),targets:true,links:0,blocks:1},
    {name:'compound formula remains with MathJax',html:'<p>$x+\\ref{target}$</p>'+block('target'),targets:true,links:0,blocks:1},
    {name:'invalid label grammar',html:'<p>\\ref{bad label}</p>'+block('bad label'),targets:false,links:0,blocks:0}
  ];
  try {
    cases.forEach(function (test) {
      var clean = setup(test.html); WizApplyMathEnvironments();
      check(test.name+': context eligibility', test.targets ? calls > 0 : expectFastPath ? calls === 0 : calls > 0);
      check(test.name+': links and blocks',document.querySelectorAll('.wiz-math-reference').length === test.links
        && document.querySelectorAll('.wiz-math-block').length === test.blocks);
      if (test.name === 'unlabelled theorem still numbered') check('unlabelled theorem number retained',
        document.querySelector('.wiz-math-block').getAttribute('data-wiz-math-number') === '1');
      check(test.name+': serialization exact',WizEditor.getContentHtml() === clean);
      WizClearMathEnvironments(); check(test.name+': explicit restore exact',document.body.innerHTML === clean);
    });
    setup('<p>\\ref{later}</p>'); WizApplyMathEnvironments();
    document.body.insertAdjacentHTML('beforeend',block('later')); calls = 0; WizApplyMathEnvironments();
    check('target added in a later pass is discovered',calls > 0 && document.querySelectorAll('.wiz-math-reference').length === 1);
    WizClearMathEnvironments();document.querySelector('blockquote').remove();calls = 0;WizApplyMathEnvironments();
    check('target removed in a later pass restores plain reference',!document.querySelector('.wiz-math-reference')
      && document.body.textContent.includes('\\ref{later}') && (expectFastPath ? calls === 0 : calls > 0));
    setup('<p>\\ref{later}</p>'+block('later'));WizApplyMathEnvironments();WizClearMathEnvironments();
    document.body.insertAdjacentHTML('beforeend',block('later'));calls = 0;WizApplyMathEnvironments();
    check('new duplicate invalidates a previously valid target',!document.querySelector('.wiz-math-reference')
      && (expectFastPath ? calls === 0 : calls > 0));
  } finally { WizClearMathEnvironments(); WizMathTextContexts = originalContexts; }
  return {checks:checks,passed:checks.filter(function(c){return c.ok;}).length,failures:checks.filter(function(c){return !c.ok;})};
}

// A skipped reference node can still supply delimiters for a requested node.
// The optimization may filter requests, never the text of their logical scopes.
function WizRunMathReferencePrefilterRegression(expectPrefilter) {
  if (expectPrefilter === undefined) expectPrefilter = true;
  var checks=[],savedContexts=WizMathTextContexts,requests=[];
  function check(name,value){checks.push({name:name,ok:!!value});}
  WizMathTextContexts=function(nodes,body){requests.push(nodes.length);return savedContexts.apply(this,arguments);};
  function block(key){return '<blockquote><p><strong>定理</strong> \\label{'+key+'}</p><p>Body.</p></blockquote>';}
  var cases=[
    {name:'unresolved reference only',html:'<p>\\ref{missing}</p>',requests:0,links:0},
    {name:'unresolved then valid in one node',html:'<p>\\ref{missing} \\ref{target}</p>',requests:1,links:1},
    {name:'unresolved separate node',html:'<p><span>\\ref{missing}</span><em>\\ref{target}</em></p>',requests:1,links:1},
    {name:'duplicate target not requested',html:'<p>\\ref{dup}</p>'+block('dup')+block('dup'),requests:0,links:0},
    {name:'colliding target not requested',html:'<div id="wiz-math-collision">Old</div><p>\\ref{collision}</p>'+block('collision'),requests:0,links:0},
    {name:'dollar opens in skipped node',html:'<p><span>$x+\\ref{missing}</span><em>+\\ref{target}</em><span>$</span></p>',requests:1,links:0},
    {name:'environment opens in skipped node',html:'<p><span>\\begin{align}\\ref{missing}</span><em>\\ref{target}</em><span>\\end{align}</span></p>',requests:1,links:0},
    {name:'math closes in skipped node',html:'<p><span>\\(\\ref{missing}\\)</span><em> \\ref{target}</em></p>',requests:1,links:1},
    {name:'dollar closes in requested node',html:'<p><span>$\\ref{missing}</span><em>x$ then \\ref{target}</em></p>',requests:1,links:1},
    {name:'nested scope not merged',html:'<div>\\ref{missing}<p>\\ref{target}</p></div>',requests:1,links:1},
    {name:'code delimiter remains excluded',html:'<p><code>$\\ref{missing}</code><em>\\ref{target}</em></p>',requests:1,links:1},
    {name:'regexp state resets for next node',html:'<p>\\ref{target}</p><p>\\ref{target}</p>',requests:2,links:2},
    {name:'overlength label followed by valid',html:'<p>\\ref{'+ 'x'.repeat(129)+'} \\ref{target}</p>',requests:1,links:1},
    {name:'standalone dollar reference',html:'<p>$\\ref{target}$</p>',requests:1,links:1},
    {name:'standalone parenthesized reference',html:'<p>\\(\\ref{target}\\)</p>',requests:1,links:1}
  ];
  try {
    cases.forEach(function(test){
      WizClearMathEnvironments();document.body.className='markdown-body wiz-readonly';
      document.body.innerHTML=test.html+block('target');window.WizNoteCss='/* WizNotePlus scientific math environments v1 */';
      window.WizEditor={getContentHtml:function(){return document.body.innerHTML;},getMarkdownSrc:function(){return document.body.innerHTML;},insertCustomStyle:function(){},removeStyleById:function(){}};
      var clean=document.body.innerHTML;requests=[];WizApplyMathEnvironments();
      check(test.name+': requested nodes',requests.length===1 && (expectPrefilter ? requests[0]===test.requests : requests[0]>=test.requests));
      check(test.name+': expected links',document.querySelectorAll('.wiz-math-reference').length===test.links);
      check(test.name+': exact serialization',WizEditor.getContentHtml()===clean);
      WizClearMathEnvironments();check(test.name+': exact restore',document.body.innerHTML===clean);
    });
  } finally {WizClearMathEnvironments();WizMathTextContexts=savedContexts;}
  return {checks:checks,passed:checks.filter(function(c){return c.ok;}).length,failures:checks.filter(function(c){return !c.ok;})};
}
