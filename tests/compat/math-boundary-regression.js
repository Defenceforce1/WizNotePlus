// Shared synthetic DOM matrix. Tests recognition, not a complete TeX parser.
function WizRunMathBoundaryRegression() {
  var cases = [
    ['prose','See \\ref{thm:a}.',1],
    ['standalone-dollar','$\\ref{thm:a}$',1],
    ['standalone-parens','\\(\\ref{thm:a}\\)',1],
    ['compound-dollar','$x+\\ref{thm:a}$',0],
    ['compound-parens','\\(x+\\ref{thm:a}\\)',0],
    ['compound-display-dollar','$$x+\\ref{thm:a}$$',0],
    ['compound-display-brackets','\\[x+\\ref{thm:a}\\]',0],
    ['standalone-display-dollar','$$\\ref{thm:a}$$',0],
    ['standalone-display-brackets','\\[\\ref{thm:a}\\]',0],
    ['prose-after-formula','\\(x+1\\) see \\ref{thm:a}',1],
    ['prose-after-display','$$x+1$$ see \\ref{thm:a}',1],
    ['split-inline-formula','\\(x+<em>\\ref{thm:a}</em>\\)',0],
    ['split-display-formula','$$x+<span>\\ref{thm:a}</span>$$',0],
    ['escaped-dollar','Cost \\$10; see \\ref{thm:a}.',1],
    ['pre','<pre>\\ref{thm:a}</pre>',0],
    ['code','<code>\\ref{thm:a}</code>',0],
    ['existing-link','<a href="#original">\\ref{thm:a}</a>',0],
    ['mathjax','<span class="MathJax">\\ref{thm:a}</span>',0],
    ['script','<script type="text/plain">\\ref{thm:a}</script>',0],
    ['textarea','<textarea>\\ref{thm:a}</textarea>',0],
    ['equation-environment','\\begin{equation}x+\\ref{thm:a}\\end{equation}',0],
    ['nested-environment','\\begin{align}\\begin{matrix}\\ref{thm:a}\\end{matrix}\\end{align}',0],
    ['prose-after-environment','\\begin{equation}x+1\\end{equation} See \\ref{thm:a}.',1],
    ['split-environment','\\begin{align}x+<em>\\ref{thm:a}</em>\\end{align}',0],
    ['dollar-in-braces','$x+\\text{price $5}+\\ref{thm:a}$',0],
    ['closing-bracket-in-braces','\\[\\text{literal \\]}+\\ref{thm:a}\\]',0],
    ['nested-braces','$x+\\text{a {b $c}}+\\ref{thm:a}$',0],
    ['prose-after-braces','$x+\\text{price $5}$ then \\ref{thm:a}',1],
    ['escaped-braces','$x+\\{a\\}+\\ref{thm:a}$',0],
    ['multiline-display','$$x+\n\\ref{thm:a}\n$$',0],
    ['break-inside-math','$$x+<br>\\ref{thm:a}$$',0],
    ['multiple-prose-and-math','\\ref{thm:a}, $x+\\ref{thm:a}$, \\ref{thm:a}',2],
    ['formula-in-one-paragraph-only','$$x+1$$</p><p>\\ref{thm:a}',1],
    ['unknown-reference','\\ref{missing}',0],
    ['formula-reference-command','\\eqref{thm:a}',0]
  ];
  var checks = [];
  cases.forEach(function (entry) {
    WizClearMathEnvironments();
    document.body.className = 'markdown-body wiz-readonly';
    document.body.innerHTML = '<p>' + entry[1] + '</p><blockquote><p><strong>定理</strong> \\label{thm:a}</p><p>Target.</p></blockquote>';
    var before = document.body.innerHTML;
    window.WizNoteCss = '/* WizNotePlus scientific math environments v1 */';
    window.WizEditor = {getContentHtml:function () { return document.body.innerHTML; },getMarkdownSrc:function () { return document.body.innerHTML; },
      insertCustomStyle:function (id, css) {
        var style = document.getElementById(id);
        if (!style) { style = document.createElement('style'); style.id = id; document.head.appendChild(style); }
        style.textContent = css;
      },
      removeStyleById:function (id) { var style = document.getElementById(id); if (style) style.remove(); }};
    var links = [], exact = true;
    for (var round = 0; round < 4; ++round) {
      WizApplyMathEnvironments();
      links.push(document.querySelectorAll('.wiz-math-reference').length);
      exact = exact && WizEditor.getContentHtml() === before;
    }
    checks.push({name:entry[0],expected:entry[2],links:links,serializedSame:exact,
      ok:exact && links.every(function (n) { return n === entry[2]; })});
  });
  WizClearMathEnvironments();
  return {kind:'synthetic formula/reference DOM matrix; not full MathJax integration',checks:checks,
    passed:checks.filter(function (c) { return c.ok; }).length,failures:checks.filter(function (c) { return !c.ok; })};
}
