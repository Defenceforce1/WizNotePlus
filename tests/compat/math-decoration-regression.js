// Browser-side synthetic DOM regression shared by Chrome and the Qt probe.
// This deliberately stubs serialization: full editor/MathJax tests are separate.
function WizRunMathDecorationRegression() {
  var checks = [];
  function check(name, ok, detail) { checks.push({name:name, ok:!!ok, detail:detail}); }
  function reset(html, options) {
    WizClearMathEnvironments();
    document.body.className = options && options.classes || 'markdown-body wiz-readonly';
    document.body.innerHTML = html;
    window.WizNoteCss = options && options.disabled ? '' : '/* WizNotePlus scientific math environments v1 */';
    window.WizEditor = {getContentHtml:function () { return document.body.innerHTML; },
      getMarkdownSrc:function () { return document.body.innerHTML; },
      insertCustomStyle:function (id, css) {
        var style = document.getElementById(id);
        if (!style) { style = document.createElement('style'); style.id = id; document.head.appendChild(style); }
        style.textContent = css;
      },
      removeStyleById:function (id) { var style = document.getElementById(id); if (style) style.remove(); }};
    return document.body.innerHTML;
  }
  function texts() {
    var walker = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT), n = 0;
    while (walker.nextNode()) ++n;
    return n;
  }
  var html = Array.from({length:100}, function (_, i) {
    return '<p>See \\ref{thm:' + i + '}.</p><blockquote><p><strong>定理</strong> \\label{thm:' + i + '}</p><p>Body.</p></blockquote>';
  }).join('');
  var clean = reset(html);
  WizApplyMathEnvironments();
  var initial = texts(), stable = true, sourceSame = true;
  for (var cycle = 0; cycle < 30; ++cycle) {
    WizApplyMathEnvironments();
    stable = stable && texts() === initial;
    sourceSame = sourceSame && WizEditor.getContentHtml() === clean;
    stable = stable && texts() === initial;
  }
  check('100 references: 30 decoration/serialization cycles have stable text-node count', stable,
    {before:initial, after:texts()});
  check('100 references: every serialization restores exact original HTML', sourceSame);
  check('100 references remain linked after all cycles', document.querySelectorAll('.wiz-math-reference').length === 100);
  var target = '<blockquote><p><strong>定义</strong> \\label{def:a}</p><p>Body.</p></blockquote>';
  var references = ['\\ref{def:a}', '前\\ref{def:a}后', '\\ref{def:a}\\ref{def:a}',
    '$\\ref{def:a}$，\\(\\ref{def:a}\\)', 'α &amp; β \\ref{def:a} 中文',
    '<code>\\ref{def:a}</code><pre>\\ref{def:a}</pre><a href="#old">\\ref{def:a}</a>'];
  references.forEach(function (reference, index) {
    var before = reset('<p>' + reference + '</p>' + target);
    WizApplyMathEnvironments();
    var n = texts(), exact = true, constant = true;
    for (var i = 0; i < 12; ++i) {
      exact = exact && WizEditor.getContentHtml() === before;
      constant = constant && texts() === n;
    }
    check('reference text/escaping and stable nodes: sample ' + index, exact && constant);
  });
  for (var seed = 0; seed < 32; ++seed) {
    var kinds = ['定理','定义','证明','引理','例','注'];
    var mixed = Array.from({length:8}, function (_, i) {
      // Include originally absent, empty and nonempty styles and existing IDs.
      var attrs = i % 4 === 0 ? '' : i % 4 === 1 ? ' style=""' : i % 4 === 2
        ? ' style="color: red; counter-reset: theorem-counter 2 !important; --local: 3"' : ' id="original-' + i + '"';
      return '<p>中文 &amp; α \\ref{case:' + seed + ':' + i + '}</p><blockquote class="original-' + i + '"' + attrs
        + '><p><strong>' + kinds[(seed+i)%kinds.length] + (i===3?' 7':'') + '（测试）</strong> \\label{case:' + seed + ':' + i
        + '}</p><p>Body <code>\\ref{case:' + seed + ':' + i + '}</code>.</p></blockquote>';
    }).join('');
    var original = reset(mixed), same = true;
    WizApplyMathEnvironments();
    for (var iteration = 0; iteration < 8; ++iteration) same = same && WizEditor.getContentHtml() === original;
    check('mixed manual numbering, Unicode and original attributes: seed ' + seed, same);
  }
  var joined = '<blockquote><p><strong>定理 7</strong> \\label{thm:joined}</p><p>A.</p>'
    + '<p><strong>定义</strong> \\label{def:joined}</p><p>B.</p></blockquote><p>\\ref{thm:joined} \\ref{def:joined}</p>';
  clean = reset(joined); WizApplyMathEnvironments();
  check('adjacent quoted environments split only in preview', document.querySelectorAll('.wiz-math-block').length === 2);
  check('adjacent quoted environments restore original order and attributes', WizEditor.getContentHtml() === clean);
  WizClearMathEnvironments();
  check('explicit clear restores exact original HTML', document.body.innerHTML === clean);
  [
    {name:'style opt-out', disabled:true},
    {name:'editing mode', classes:'markdown-body'},
    {name:'rich text', classes:'wiz-readonly'}
  ].forEach(function (options) {
    var before = reset(joined, options); WizApplyMathEnvironments();
    check(options.name + ' leaves content untouched', document.body.innerHTML === before);
  });
  function escapedText(value) {
    var element = document.createElement('span');
    element.textContent = value;
    return element.innerHTML;
  }
  // Keep the existing ASCII label grammar, including its length boundary.
  // These are preview/source round-trip checks, not new label syntax.
  ['a','thm:one','a/b.c-d_e','__proto__','constructor','toString','hasOwnProperty',
    '0','https://example.test/a','x'.repeat(127),'x'.repeat(128)].forEach(function (key,index) {
    var original = reset('<p>See \\ref{' + escapedText(key) + '}.</p><blockquote id="original-label-block"'
      + ' style="color: red"><p><strong>定义</strong> \\label{' + escapedText(key) + '}</p><p>Body.</p></blockquote>');
    WizApplyMathEnvironments();
    var block = document.querySelector('.wiz-math-block'), link = document.querySelector('.wiz-math-reference');
    var valid = block && link && document.querySelectorAll('.wiz-math-reference').length === 1
      && block.id === 'wiz-math-' + key && link.getAttribute('href') === '#' + block.id;
    var nodeCount = texts();
    for (var cycle = 0; cycle < 8; ++cycle)
      valid = valid && WizEditor.getContentHtml() === original && texts() === nodeCount;
    WizClearMathEnvironments();
    check('existing legal label and exact attribute/source restoration: sample ' + index,
      valid && document.body.innerHTML === original);
  });
  ['','x'.repeat(129),'中文','a b',"a'b",'a"b','a[b]','a{b}','a&b','a#b','a%b','a\\b'].forEach(function (key,index) {
    var original = reset('<p>See \\ref{' + escapedText(key) + '}.</p>'
      + '<blockquote><p><strong>定义</strong> \\label{' + escapedText(key) + '}</p><p>Body.</p></blockquote>');
    WizApplyMathEnvironments();
    check('out-of-grammar label remains original text without preview rewrite: sample ' + index,
      !document.querySelector('.wiz-math-block,.wiz-math-reference')
      && WizEditor.getContentHtml() === original && document.body.innerHTML === original);
  });
  ['__proto__','constructor','dup:ordinary'].forEach(function (key,index) {
    var original = reset('<p>See \\ref{' + key + '}.</p>' + [0,1].map(function () {
      return '<blockquote><p><strong>定理</strong> \\label{' + key + '}</p><p>Body.</p></blockquote>';
    }).join(''));
    WizApplyMathEnvironments();
    var blocks = Array.from(document.querySelectorAll('.wiz-math-block'));
    check('duplicate labels are ambiguous even for object-property names: sample ' + index,
      blocks.length === 2 && !document.querySelector('.wiz-math-reference')
      && blocks.every(function (block) { return block.getAttribute('data-wiz-math-heading').includes('标签重复：' + key); })
      && WizEditor.getContentHtml() === original);
  });
  ['old:anchor','__proto__'].forEach(function (key,index) {
    var anchor = 'wiz-math-' + key;
    var original = reset('<div id="' + anchor + '">Original destination</div><p>See \\ref{' + key + '}.</p>'
      + '<blockquote id="original-block"><p><strong>定义</strong> \\label{' + key + '}</p><p>Body.</p></blockquote>');
    WizApplyMathEnvironments();
    var collisionPreserved = !document.querySelector('.wiz-math-reference')
      && document.getElementById(anchor).tagName === 'DIV';
    var sourcePreserved = WizEditor.getContentHtml() === original;
    WizClearMathEnvironments();
    check('existing destination collision is not stolen by math decoration: sample ' + index,
      collisionPreserved && sourcePreserved && document.body.innerHTML === original);
  });
  WizClearMathEnvironments();
  return {kind:'synthetic DOM regression, not a full editor integration', checks:checks,
    passed:checks.filter(function (c) { return c.ok; }).length, failures:checks.filter(function (c) { return !c.ok; })};
}

// Same fixed local microbenchmark in both browser engines. Not whole-app timing.
function WizRunMathDecorationMicrobenchmark() {
  var records=[];
  [100,500,1000].forEach(function(count){[false,true].forEach(function(oneParagraph){
    WizClearMathEnvironments();document.body.className='markdown-body wiz-readonly';
    var references=Array.from({length:count},function(){return oneParagraph?'See \\ref{thm:bench}. ':'<p>See \\ref{thm:bench}.</p>';}).join('');
    document.body.innerHTML=(oneParagraph?'<p>'+references+'</p>':references)
      +'<blockquote><p><strong>定理</strong> \\label{thm:bench}</p><p>Body.</p></blockquote>';
    var clean=document.body.innerHTML;
    window.WizNoteCss='/* WizNotePlus scientific math environments v1 */';
    window.WizEditor={getContentHtml:function(){return document.body.innerHTML;},getMarkdownSrc:function(){return document.body.innerHTML;}};
    WizApplyMathEnvironments();WizApplyMathEnvironments();
    var iterationMs=[];
    for(var i=0;i<5;i++){var start=performance.now();WizApplyMathEnvironments();iterationMs.push(performance.now()-start);}
    records.push({count:count,oneParagraph:oneParagraph,iterationMs:iterationMs,serializedSame:WizEditor.getContentHtml()===clean,
      links:document.querySelectorAll('.wiz-math-reference').length});
  });});
  WizClearMathEnvironments();return records;
}

// Stable-node baseline versus an in-memory lookup-only prototype. Alternating
// order and exact source/number/anchor checks; not native paint or App timing.
function WizRunMathBlockLookupBenchmark() {
  var baseline=WizApplyMathEnvironments;
  if(!baseline.toString().includes('WizMathDecoration.blocks.find(function (item) { return item.element === block; })'))
    throw Error('Benchmark requires the preserved array-lookup baseline, not an already optimized helper');
  var code=baseline.toString()
    .replace('var labels = Object.create(null), duplicates = Object.create(null);',
      'var labels = Object.create(null), duplicates = Object.create(null); var blockRecords = new Map();')
    .replace('WizMathDecoration.blocks.push(record);',
      'WizMathDecoration.blocks.push(record); blockRecords.set(block, record);')
    .replace('WizMathDecoration.blocks.find(function (item) { return item.element === block; })',
      'blockRecords.get(block)');
  if(code===baseline.toString()||!code.includes('blockRecords.get(block)'))
    throw Error('Block lookup prototype source anchor mismatch');
  var candidate=(0,eval)('('+code+')'), records=[];
  try {
    [100,500,1500].forEach(function(count){
      WizClearMathEnvironments();
      document.body.className='markdown-body wiz-readonly';
      document.body.innerHTML=Array.from({length:count},function(_,i){
        return '<p>See \\ref{bench:'+i+'}.</p><blockquote><p><strong>'+(i%3?'定理':'定义')
          +'</strong> \\label{bench:'+i+'}</p><p>Body '+i+'.</p></blockquote>';
      }).join('');
      var clean=document.body.innerHTML;
      window.WizNoteCss='/* WizNotePlus scientific math environments v1 */';
      window.WizEditor={getContentHtml:function(){return document.body.innerHTML;},getMarkdownSrc:function(){return document.body.innerHTML;}};
      var shape=function(){return JSON.stringify(Array.from(document.querySelectorAll('.wiz-math-block'))
        .map(function(block){return [block.id,block.getAttribute('data-wiz-math-heading')];}));};
      WizApplyMathEnvironments=baseline; baseline(); baseline();
      var expected=shape(), same=true, trials=[];
      for(var round=0;round<8;round++) {
        var order=round%2?[['map',candidate],['array',baseline]]:[['array',baseline],['map',candidate]];
        order.forEach(function(pair){
          WizApplyMathEnvironments=pair[1];
          var start=performance.now();pair[1]();
          trials.push({variant:pair[0],ms:performance.now()-start});
          same=same&&shape()===expected&&document.querySelectorAll('.wiz-math-reference').length===count
            &&WizEditor.getContentHtml()===clean;
        });
      }
      records.push({count:count,trials:trials,sourceNumberAndDestinationSame:same});
    });
    return records;
  } finally { WizClearMathEnvironments();WizApplyMathEnvironments=baseline; }
}
