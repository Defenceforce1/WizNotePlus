// Synthetic initialized page only. Snapshot scripts are supplied from the
// actual WizDocumentAsync.h producer by the independent native test driver.
window.WizEditing269 = {done: false};
(async function () {
  'use strict';
  var checks = [], snapshots = [], diffs = [];
  function check(name, ok) { checks.push({name: name, passed: !!ok}); }
  function state(name) {
    var doc = MathJax.startup.document, items = Array.from(doc.math);
    var roots = Array.isArray(doc.options.elements) ? doc.options.elements : [];
    var result = {name: name, items: items.length, detached: items.filter(function (m) { return m.typesetRoot && !m.typesetRoot.isConnected; }).length,
      detachedScopes: roots.filter(function (r) { return r && r.nodeType && !r.isConnected; }).length,
      live: MathJax.Hub.getAllJax().length, editable: document.body.isContentEditable};
    snapshots.push(result); return result;
  }
  function flush() { return MathJax.Hub.Queue(function () {}); }
  function apply() { WizApplyNoteStyle(); WizApplySpellCheck(); }
  function edit() { WizPrepareNoteModeChange(); return new Promise(function (r) { WizEditor.on(null, function () { apply(); r(); }); }); }
  function read() { WizPrepareNoteModeChange(); return new Promise(function (r) { WizEditor.off(null, function () { apply(); r(); }); }); }
  function capture() {
    // Public serialization may reorder temporary head styles. Observe the DOM
    // after those getters, so their own normalization is not blamed on cleanup.
    var html = WizEditor.getContentHtml(), source = WizEditor.getMarkdownSrc({unEscapeHtml: true});
    return {dom: document.documentElement.outerHTML, html: html, source: source, modified: WizEditor.isModified()};
  }
  function same(a, b) {
    var result = a.dom === b.dom && a.html === b.html && a.source === b.source && a.modified === b.modified;
    if (!result) { var entry = {}; for (var k of ['dom','html','source','modified']) if (a[k] !== b[k]) { var i = 0; if (typeof a[k] === 'string') { while (i < a[k].length && i < b[k].length && a[k][i] === b[k][i]) ++i; entry[k] = {offset:i,before:a[k].slice(Math.max(0,i-50),i+150),after:b[k].slice(Math.max(0,i-50),i+150)}; } else entry[k] = {before:a[k],after:b[k]}; } diffs.push(entry); }
    return result;
  }
  try {
    await new Promise(function (r) { requestAnimationFrame(function () { requestAnimationFrame(r); }); });
    var count = MathJax.Hub.getAllJax().length, initial = capture();
    check('current source exact before retirement', initial.source === WizSynthetic269Source);
    check('expected current math workload', count === 130 && state('initial-reader').detached === 0);
    await MathJax.Hub.WizRetireDisconnected();
    check('explicit retirement leaves connected reader byte-identical', same(initial, capture()) && state('connected-reader-after-retirement').live === count);
    await edit();
    var immediate = capture();
    document.body.insertAdjacentHTML('beforeend', '<div>UNSAVED269 中文 &amp; &lt; &gt; α</div>');
    var unsaved = capture();
    check('new unsaved DOM is authoritative', unsaved.source.includes('UNSAVED269') && unsaved.html.includes('UNSAVED269'));
    await flush();
    check('automatic editor retirement preserves current DOM source and modified flag', same(unsaved, capture()));
    var edited = state('editor-after-automatic-retirement');
    check('detached engine math items retired after editor callback', edited.editable && edited.items === 0 && edited.detached === 0);
    check('completed detached scan scopes retired', edited.detachedScopes === 0);
    check('read-only bridge records no longer expose the prior page', edited.live === 0);
    await MathJax.Hub.WizRetireDisconnected();
    check('repeated editor retirement is idempotent', same(unsaved, capture()) && state('editor-retirement-repeat').items === 0);
    var htmlGetter = WizEditor.getContentHtml;
    WizEditor.getContentHtml = function () { throw Error('synthetic snapshot failure'); };
    var failure;
    try { failure = (0, eval)(WizSynthetic269SaveScript); }
    finally { WizEditor.getContentHtml = htmlGetter; }
    check('actual save producer retains its failure result', failure.ok === false && failure.reason === 'capture-exception');
    check('snapshot failure leaves unsaved content and modified flag intact', same(unsaved, capture()));
    var saved = (0, eval)(WizSynthetic269SaveScript);
    check('actual save snapshot contains current unsaved source', saved.ok && saved.changed && saved.html.includes('UNSAVED269'));
    check('save snapshot excludes prior generated MathJax markup', !saved.html.includes('<mjx-container') && !saved.html.includes('WizMathJax4-Element-'));
    check('save capture does not replace current raw source', WizEditor.getMarkdownSrc({unEscapeHtml: true}) === unsaved.source);
    await read(); await flush();
    var restored = state('reader-after-edit');
    check('return to reader reconstructs the current math graph', !restored.editable && restored.items === count && restored.live === count && restored.detached === 0 && restored.detachedScopes === 0);
    check('unsaved text survives round trip', WizEditor.getMarkdownSrc({unEscapeHtml: true}).includes('UNSAVED269'));
    check('local equation references resolve', Array.from(document.querySelectorAll('a')).filter(function (a) { return (a.getAttribute('href') || '').startsWith('#mjx-eqn-'); }).length === 2 && ['mjx-eqn-editing:0', 'mjx-eqn-editing:127'].every(function (id) { return !!document.getElementById(id); }));
    check('reference glyphs have no unresolved question marks', MathJax.Hub.getAllJax().filter(function (j) { return /\\(?:eqref|ref)\{/.test(j.originalText); }).every(function (j) { return !new DOMParser().parseFromString(j.root.toMathML(''), 'text/xml').documentElement.textContent.includes('?'); }));
    await new Promise(function (r) { requestAnimationFrame(function () { requestAnimationFrame(r); }); });
    var readerBefore = capture();
    await MathJax.Hub.WizRetireDisconnected();
    check('late retirement preserves the reconstructed connected reader', same(readerBefore, capture()) && state('reader-after-late-retirement').items === count);
    var readerSaved = (0, eval)(WizSynthetic269ReaderSaveScript);
    check('actual unchanged reader save remains unchanged', readerSaved.ok && readerSaved.changed === false);
    await edit();
    var release, blocked = new Promise(function (r) { release = r; });
    MathJax.Hub.Queue(function () { return blocked; });
    apply(); // Queue editor cleanup while an earlier math task is unfinished.
    var resumed = read(); release();
    await resumed; await flush();
    var rapid = state('reader-after-delayed-editor-retirement');
    check('delayed editor retirement cannot clear a newer connected reader', !rapid.editable && rapid.items === count && rapid.live === count && rapid.detached === 0 && rapid.detachedScopes === 0);
    check('rapid mode sequence retains current source', WizEditor.getMarkdownSrc({unEscapeHtml: true}) === readerBefore.source);
    check('no compatibility or rendering errors after retirement', !window.WizMathJax4Error && document.querySelectorAll('.MathJax_Error,merror').length === 0);
    window.WizEditing269 = {done: true, checks: checks, failures: checks.filter(function (c) { return !c.passed; }).length, snapshots: snapshots, diffs: diffs};
  } catch (error) {
    window.WizEditing269 = {done: true, checks: checks, failures: 1, error: String(error), snapshots: snapshots, diffs: diffs};
  }
})();
