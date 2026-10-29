// Run only in a synthetic, ready current-editor page. No account or real note.
window.WizMathLifetimeRegression = {done: false};
(async function () {
  'use strict';
  var checks = [], hub = MathJax.Hub;
  function check(name, ok) { checks.push({name: name, passed: !!ok, items: items().length, detached: detached(), liveA: typeof a === "undefined" ? null : hub.getAllJax(a).length, liveB: typeof b === "undefined" ? null : hub.getAllJax(b).length}); }
  function items() { return Array.from(MathJax.startup.document.math); }
  function detached() { return items().filter(function (m) { return m.typesetRoot && !m.typesetRoot.isConnected; }).length; }
  function run(method, node) { return hub.Queue([method, hub, node]); }
  try {
    var original = WizEditor.getMarkdownSrc({unEscapeHtml: true});
    var initialItems = items().length, initialJax = hub.getAllJax();
    var initialSvg = initialJax.map(function (j) { return j.frame.outerHTML; });
    var a = document.createElement('section'), b = document.createElement('section');
    document.body.append(a, b);
    a.innerHTML = '<script type="math/tex; mode=display">x+y=z</script>';
    await run('Process', a);
    check('Process preserves TeX source and creates one formula', hub.getAllJax(a).length === 1 && hub.getAllJax(a)[0].originalText === 'x+y=z');
    var frameA = hub.getAllJax(a)[0].frame;
    await run('Typeset', a);
    check('Repeated Typeset is idempotent for live output', hub.getAllJax(a).length === 1 && hub.getAllJax(a)[0].frame === frameA && items().length === initialItems + 1);
    b.textContent = '$$u+v=w$$';
    await run('Typeset', b);
    var frameB = hub.getAllJax(b)[0].frame;
    await run('Reprocess', a);
    check('Reprocess confines replacement to the selected scope', hub.getAllJax(a).length === 1 && hub.getAllJax(b)[0].frame === frameB && items().length === initialItems + 2);
    await run('Rerender', a);
    check('Rerender retains live source and other scope', hub.getAllJax(a).length === 1 && hub.getAllJax(a)[0].SourceElement().textContent === 'x+y=z' && hub.getAllJax(b)[0].frame === frameB && items().length === initialItems + 2);
    var jax = hub.getAllJax(a)[0];
    check('MathML serialization remains available', typeof jax.root.toMathML('') === 'string' && jax.root.toMathML('').includes('<math'));
    jax.Remove();
    check('Explicit Remove retires output but preserves input script', hub.getAllJax(a).length === 0 && a.querySelector('script').textContent === 'x+y=z' && items().length === initialItems + 1);
    await run('Process', a);
    check('Removed input can be processed again', hub.getAllJax(a).length === 1 && items().length === initialItems + 2);
    a.remove();
    await run('Typeset', b);
    check('Detached scope retires without touching live output', detached() === 0 && items().length === initialItems + 1 && hub.getAllJax(b)[0].frame === frameB);
    var history = [];
    for (var i = 0; i < 8; ++i) {
      b.textContent = '$$u_{' + i + '}+v=w$$';
      await run('Typeset', b);
      history.push({items: items().length, detached: detached()});
    }
    check('Repeated DOM replacement keeps only current formula items', history.every(function (r) { return r.items === initialItems + 1 && r.detached === 0; }));
    b.remove();
    await run('Typeset', document.body);
    check('Removing synthetic scopes restores original engine item count', detached() === 0 && items().length === initialItems && hub.getAllJax().length === initialJax.length);
    check('Original connected output is byte-identical', initialJax.every(function (j, i) { return j.frame.isConnected && j.frame.outerHTML === initialSvg[i]; }));
    var order = [];
    await hub.Queue(function () { order.push('first'); }, ['Config', hub, {}], function () { order.push('last'); });
    check('Queued callbacks keep order', order.join(',') === 'first,last');
    for (i = 0; i < 1024; ++i) hub.Config({});
    check('Recent-operation trace is bounded', WizMathJax4Trace.length === 256 && WizMathJax4Trace[255] === 'Config');
    check('Raw source and rendered references survive lifecycle checks', WizEditor.getMarkdownSrc({unEscapeHtml: true}) === original && !window.WizMathJax4Error && document.querySelectorAll('.MathJax_Error,merror').length === 0);
    window.WizMathLifetimeRegression = {done: true, checks: checks, failures: checks.filter(function (c) { return !c.passed; }).length, replacementHistory: history, initialItems: initialItems, finalItems: items().length, detached: detached(), traceLength: WizMathJax4Trace.length};
  } catch (error) {
    window.WizMathLifetimeRegression = {done: true, checks: checks, failures: 1, error: String(error)};
  }
})();
