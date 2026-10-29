// Synthetic ready-editor page only; never inject into a real note.
window.WizReferenceRootRegression = {done: false};
(async function () {
  'use strict';
  var checks = [], initialBody = document.body, activeBody = initialBody;
  var originalNavigate = WizNavigateMathReference;
  var seen = [], source = WizEditor.getMarkdownSrc({unEscapeHtml: true});
  var html = WizEditor.getContentHtml(), modified = WizEditor.isModified();
  function check(name, ok) { checks.push({name: name, passed: !!ok}); }
  function click(target, options, cancelled) {
    var event = new MouseEvent('click', Object.assign({bubbles: true, cancelable: true, button: 0}, options || {}));
    if (cancelled) event.preventDefault();
    // Ignore paths must not follow real URLs; cancel at target after delegation.
    target.addEventListener('click', function stopDefault(e) { e.preventDefault(); }, {once: true});
    var before = seen.length;
    target.dispatchEvent(event);
    return seen.length - before;
  }
  try {
    WizNavigateMathReference = function (anchor) { seen.push(anchor); };
    var link = Array.from(document.querySelectorAll('a')).find(function (a) {
      return (a.getAttribute('href') || a.getAttributeNS('http://www.w3.org/1999/xlink', 'href') || '').startsWith('#mjx-eqn-');
    });
    if (!link) throw Error('synthetic rendered formula reference required');
    var target = link.querySelector('*') || link;
    check('Installed roots use weak membership without the former strong root', typeof WizMathReferenceRoot === 'undefined' && WizMathReferenceRoots instanceof WeakSet);
    check('The current reader body is tracked', WizMathReferenceRoots.has(initialBody));
    WizInstallMathReferenceNavigation(); WizInstallMathReferenceNavigation();
    check('Repeated installation delegates one click once', click(link) === 1);
    check('Nested SVG or HTML link content delegates once', click(target) === 1);
    var oldEditable = initialBody.getAttribute('contenteditable');
    initialBody.contentEditable = 'true';
    check('Editable content does not trigger reader navigation', click(target) === 0);
    if (oldEditable === null) initialBody.removeAttribute('contenteditable'); else initialBody.setAttribute('contenteditable', oldEditable);
    for (var modifier of ['ctrlKey', 'metaKey', 'altKey', 'shiftKey']) {
      var option = {}; option[modifier] = true;
      check(modifier + ' preserves normal click handling', click(target, option) === 0);
    }
    check('Non-primary click does not trigger navigation', click(target, {button: 1}) === 0);
    check('Already cancelled click does not trigger navigation', click(target, {}, true) === 0);
    var ordinary = document.createElement('a'); ordinary.href = '#mjx-eqn-switch%3A0'; initialBody.appendChild(ordinary);
    check('Ordinary links are not intercepted', click(ordinary) === 0);
    ordinary.className = 'wiz-math-reference'; ordinary.href = 'https://synthetic.invalid/#mjx-eqn-switch%3A0';
    check('External links keep ordinary handling without loading a URL', click(ordinary) === 0);
    ordinary.href = '#mjx-eqn-synthetic-missing';
    check('Missing targets are not intercepted', click(ordinary) === 0);
    ordinary.remove();
    var replacement = document.createElement('body');
    replacement.innerHTML = '<span id="mjx-eqn-synthetic-root"></span><a class="wiz-math-reference" href="#mjx-eqn-synthetic-root"><span>reference</span></a>';
    document.documentElement.replaceChild(replacement, initialBody); activeBody = replacement;
    WizInstallMathReferenceNavigation(); WizInstallMathReferenceNavigation();
    check('A replacement root is installed exactly once', WizMathReferenceRoots.has(replacement) && click(replacement.querySelector('a span')) === 1);
    check('A detached former root cannot navigate the current document', click(target) === 0);
    document.documentElement.replaceChild(initialBody, replacement); activeBody = initialBody;
    WizInstallMathReferenceNavigation();
    check('Restoring a previously installed root does not duplicate listeners', click(target) === 1);
    check('Root lifecycle preserves exact source HTML and modified state', WizEditor.getMarkdownSrc({unEscapeHtml: true}) === source && WizEditor.getContentHtml() === html && WizEditor.isModified() === modified);
    WizPrepareNoteModeChange(); await new Promise(function (resolve) { WizEditor.on(null, resolve); });
    check('Real edit transition preserves source', document.body.isContentEditable && WizEditor.getMarkdownSrc({unEscapeHtml: true}) === source);
    WizPrepareNoteModeChange(); await new Promise(function (resolve) { WizEditor.off(null, resolve); });
    WizApplyNoteStyle(); WizApplySpellCheck();
    check('Real reader transition preserves source and tracks the current root', !document.body.isContentEditable && WizEditor.getMarkdownSrc({unEscapeHtml: true}) === source && WizMathReferenceRoots.has(document.body));
    check('Reading remains free of math errors', !window.WizMathJax4Error && document.querySelectorAll('.MathJax_Error,merror').length === 0);
    window.WizReferenceRootRegression = {done: true, checks: checks, failures: checks.filter(function (r) { return !r.passed; }).length};
  } catch (error) {
    if (activeBody !== initialBody && activeBody.parentNode) activeBody.parentNode.replaceChild(initialBody, activeBody);
    window.WizReferenceRootRegression = {done: true, checks: checks, failures: 1, error: String(error)};
  } finally {
    WizNavigateMathReference = originalNavigate;
  }
})();
