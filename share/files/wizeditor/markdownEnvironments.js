// Markdown display-environment compatibility through existing public reader hooks.
// Source and the bundled editor/MathJax remain unchanged.
function WizMarkdownEnvironmentCodePositions(source) {
  var code = new Uint8Array(source.length), fence = null, offset = 0;
  source.split('\n').forEach(function (line) {
    var marker = line.match(/^ {0,3}(`{3,}|~{3,}|(?:~T){3,})(.*)$/);
    if (marker) marker[1] = marker[1].replace(/~T/g, '~');
    if (fence) {
      code.fill(1, offset, offset + line.length + 1);
      if (marker && marker[1][0] === fence[0] && marker[1].length >= fence.length
          && !marker[2].trim()) fence = null;
    } else if (marker) {
      fence = marker[1]; code.fill(1, offset, offset + line.length + 1);
    } else if (/^( {4}|\t)/.test(line)) code.fill(1, offset, offset + line.length + 1);
    offset += line.length + 1;
  });
  var ticks = /(`+)/g, open;
  while ((open = ticks.exec(source))) {
    if (code[open.index]) continue;
    var close = source.indexOf(open[0], ticks.lastIndex);
    if (close < 0) continue;
    code.fill(1, open.index, close + open[0].length);
    ticks.lastIndex = close + open[0].length;
  }
  var rawCode = /<(pre|code|script|textarea)\b[^>]*>[\s\S]*?<\/\1\s*>/gi, html;
  while ((html = rawCode.exec(source))) code.fill(1, html.index, rawCode.lastIndex);
  return code;
}

function WizMarkdownEnvironmentPattern(rawSource) {
  // The public hook also runs recursively inside Markdown blockquotes/lists.
  // Their container prefixes are absent there but still present in raw source.
  var prefix = rawSource ? String.raw`(?:[ \t]*>[ \t]?)*[ \t]*` : String.raw`(?: {0,3}>[ \t]?)* {0,3}`;
  return new RegExp(String.raw`(^|\n)(${prefix})(\\begin\{((?:equation|align|gather|multline|alignat)\*?)\}[\s\S]*?\\end\{\4\})(?=[ \t]*(?:\n|$))`, 'g');
}

function WizMarkdownEnvironmentContainerText(tex, prefix) {
  if (!prefix) return tex;
  return tex.split('\n').map(function (line, index) {
    return index && line.indexOf(prefix) === 0 ? line.slice(prefix.length) : line;
  }).join('\n');
}

function WizMarkdownEnvironmentBlocks(source) {
  // This pool only recovers original bytes. It must include list continuations
  // indented by four spaces: the recursive hook has already stripped their
  // container indentation. Eligibility is checked on that hook's source below,
  // where actual code blocks still remain excluded, not on this lookup pool.
  var pattern = WizMarkdownEnvironmentPattern(true);
  var blocks = [], match;
  while ((match = pattern.exec(source))) {
    blocks.push(WizMarkdownEnvironmentContainerText(match[3], match[2]));
  }
  return blocks;
}

function WizProtectMarkdownEquationReferences(source) {
  if (source.indexOf('\\eqref{') < 0) return source;
  var code = WizMarkdownEnvironmentCodePositions(source), math = WizMathProtectedPositions(source);
  return source.replace(/\\eqref\{[A-Za-z0-9_:./-]{1,128}\}/g, function (reference, offset) {
    if (code[offset] || math[offset]) return reference;
    var encoded = Array.from(reference).map(function (c) { return '&#' + c.codePointAt(0) + ';'; }).join('');
    return '<span class="wiz-latex-math">' + encoded + '</span>';
  });
}

function WizConfigureMarkdownEnvironments(options) {
  var reader = options.reader || (options.reader = {});
  var callbacks = reader.callback || (reader.callback = {});
  var beforeRender = callbacks.beforeMarkdownRender, markdownRendered = callbacks.markdown;
  callbacks.beforeMarkdownRender = function (html) {
    if (beforeRender) html = beforeRender(html);
    if (html.indexOf('\\begin{') < 0 || typeof WizRenderLatexTheorems !== 'function') return html;
    try { return WizRenderLatexTheorems(html, WizEditor.getMarkdownSrc({unEscapeHtml:true})); }
    catch (error) { console.error('Theorem preview retained original text', error); return html; }
  };
  callbacks.markdown = function () {
    if (markdownRendered) markdownRendered.apply(this, arguments);
    if (document.querySelector('.wiz-latex-theorem') && typeof WizApplyMathEnvironments === 'function'
        && !WizMathDecoration.blocks.some(function (record) {
          return record.element.isConnected && record.element.classList.contains('wiz-latex-theorem');
        })) WizApplyMathEnvironments();
  };
  var plugins = reader.markdownPlugIn || (reader.markdownPlugIn = {});
  var original = plugins.customBlockGamut;
  plugins.customBlockGamut = function (source) {
    if (original) source = original(source);
    if (!/\\(?:begin|end)\{(?:theorem|lemma|proposition|corollary|definition|example|remark|proof|equation|align|gather|multline|alignat)\*?\}/.test(source)) return WizProtectMarkdownEquationReferences(source);
    var markerCode = WizMarkdownEnvironmentCodePositions(source);
    source = source.replace(/\\(?:begin|end)\{(?:theorem|lemma|proposition|corollary|definition|example|remark|proof)\*\}/g,
      function (marker, offset) { return markerCode[offset] ? marker : marker.replace('*', '&#42;'); });
    if (!/\\begin\{(?:equation|align|gather|multline|alignat)\*?\}/.test(source)) return WizProtectMarkdownEquationReferences(source);
    // The old reader doubles the first TeX row separator before this hook.
    // Recover each exact block from its public source getter instead of
    // treating that transient parser representation as authoritative text.
    var raw = WizEditor.getMarkdownSrc({unEscapeHtml:true});
    var originals = WizMarkdownEnvironmentBlocks(raw);
    var protectedCode = WizMarkdownEnvironmentCodePositions(source);
    var pattern = WizMarkdownEnvironmentPattern();
    return WizProtectMarkdownEquationReferences(source.replace(pattern, function (all, newline, indent, tex, environment, offset) {
      if (protectedCode[offset + newline.length + indent.length]) return all;
      tex = WizMarkdownEnvironmentContainerText(tex, indent);
      var originalTex = originals.find(function (candidate) {
        return candidate === tex || candidate.replace('\\\\','\\\\\\\\') === tex;
      });
      if (!originalTex) return all;
      // Numeric entities keep Markdown punctuation, TeX backslashes and line
      // breaks opaque through the remaining inline parser and HTML filter.
      var encoded = Array.from(originalTex).map(function (c) { return '&#' + c.codePointAt(0) + ';'; }).join('');
      return newline + '<div class="wiz-bare-math-environment">' + encoded + '</div>\n';
    }));
  };
  return options;
}

var WizMarkdownEnvironmentLoader = null;
function WizLoadMarkdownEnvironmentMathJax(dependencyUrl) {
  if (WizMarkdownEnvironmentLoader) return WizMarkdownEnvironmentLoader;
  WizMarkdownEnvironmentLoader = new Promise(function (resolve, reject) {
    var settled = false;
    var deadline = setTimeout(function () { finish(new Error('Bundled MathJax initialization timed out')); }, 30000);
    function finish(error) {
      if (settled) return;
      settled = true; clearTimeout(deadline);
      if (error) reject(error); else resolve();
    }
    // The original reader replaces body nodes on mode changes, including
    // MathJax's SVG glyph cache. Match its own fresh-engine-per-render policy.
    var previous = document.getElementById('wiz_markdown_environment_loader');
    if (previous) previous.remove();
    window.MathJax = {
      skipStartupTypeset: true,
      tex2jax: {inlineMath:[['$','$'],['\\(','\\)']], displayMath:[['$$','$$'],['\\[','\\]']],
        processEscapes:true, processEnvironments:true},
      SVG:{linebreaks:{automatic:true}},
      TeX:{equationNumbers:{autoNumber:'AMS'},
        Safe:{allow:{URLs:'safe',classes:'safe',cssIDs:'safe',styles:'safe',fontsize:'all'}}},
      messageStyle:'none'
    };
    var script = document.createElement('script');
    script.id = 'wiz_markdown_environment_loader';
    script.setAttribute('name','wiz_inner_script');
    script.setAttribute('wiz_style','unsave');
    script.src = dependencyUrl.replace(/\/$/,'') + '/mathjax/MathJax.js?config=TeX-AMS-MML_SVG';
    script.onload = function () {
      if (window.MathJax && MathJax.Hub) MathJax.Hub.Queue(function () { finish(); });
      else finish(new Error('Bundled MathJax did not initialize'));
    };
    script.onerror = function () { finish(new Error('Bundled MathJax could not load')); };
    document.head.appendChild(script);
  }).finally(function () { WizMarkdownEnvironmentLoader = null; });
  return WizMarkdownEnvironmentLoader;
}

function WizInstallMarkdownEnvironmentRendering(dependencyUrl, callbacks) {
  var off = WizEditor.off, on = WizEditor.on, sequence = 0;
  var pending = null;
  WizEditor.on = function () {
    ++sequence;
    var receiver = this, args = arguments;
    // Finish local dependency initialization before restoring editable source;
    // MathJax startup must never insert its SVG cache into that source.
    if (pending) { pending.finally(function () { on.apply(receiver, args); }); return WizEditor; }
    return on.apply(receiver, args);
  };
  WizEditor.off = function (options, callback) {
    if (typeof options === 'function') { callback = options; options = null; }
    var token = ++sequence;
    return off.call(this, options, function () {
      var blocks = document.body.classList.contains('markdown-body')
        ? Array.from(document.querySelectorAll('.wiz-bare-math-environment,.wiz-latex-math'))
        .filter(function (block) { return !block.querySelector('script[type^="math/tex"]'); })
        : [];
      if (!blocks.length) { if (callback) callback(); return; }
      window.WizMarkdownEnvironmentRenderState = 'pending';
      pending = WizLoadMarkdownEnvironmentMathJax(dependencyUrl).then(function () {
        if (sequence !== token || document.body.isContentEditable) return;
        if (MathJax.InputJax.TeX.resetEquationNumbers) MathJax.InputJax.TeX.resetEquationNumbers();
        return new Promise(function (resolve) {
          // Only accepted math/reference wrappers: the legacy reader leaves
          // some tilde code fences as ordinary DOM text, not <pre> elements.
          MathJax.Hub.Queue(['Typeset', MathJax.Hub, blocks, resolve]);
        });
      }).then(function () {
        window.WizMarkdownEnvironmentRenderState = sequence === token ? 'ready' : 'cancelled';
        if (sequence === token && callbacks && typeof callbacks.mathJax === 'function') callbacks.mathJax();
      }).catch(function (error) {
        if (sequence === token) blocks.forEach(function (block) {
          if (!block.isConnected || block.querySelector('script[type^="math/tex"]')) return;
          var prose = document.createElement(block.tagName === 'SPAN' ? 'span' : 'p');
          prose.textContent = block.textContent; block.replaceWith(prose);
        });
        window.WizMarkdownEnvironmentRenderState = 'failed'; console.error(error);
      }).finally(function () {
        pending = null;
        if (callback) callback();
      });
    });
  };
  if (window.WizReader) WizReader.on = WizEditor.off;
}

// Both the native bridge and saved-reader bootstrap use this same entry point.
function WizInitMarkdownEditor(options) {
  // A new ordinary note can become Markdown after its title is changed to .md.
  // The native mode switch updates reader.type without reinitializing the editor.
  // Register the dormant hook for every initial type; it is only called by the
  // Markdown converter, and the rendering wrapper checks the actual body class.
  WizConfigureMarkdownEnvironments(options);
  var result = WizEditor.init(options);
  WizInstallMarkdownEnvironmentRendering(options.dependencyUrl, options.reader.callback);
  return result;
}
