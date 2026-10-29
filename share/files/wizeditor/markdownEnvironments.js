// Markdown display-environment compatibility through existing public reader hooks.
// Source and the bundled editor/MathJax remain unchanged.
function WizMarkdownEnvironmentCodePositions(source) {
  var code = new Uint8Array(source.length), fence = null, offset = 0;
  // Keep only the current line, not an array of every line in a large note.
  while (offset <= source.length) {
    var end = source.indexOf('\n', offset);
    if (end < 0) end = source.length;
    var line = source.slice(offset, end);
    var marker = line.match(/^ {0,3}(`{3,}|~{3,}|(?:~T){3,})(.*)$/);
    if (marker) marker[1] = marker[1].replace(/~T/g, '~');
    if (fence) {
      code.fill(1, offset, offset + line.length + 1);
      if (marker && marker[1][0] === fence[0] && marker[1].length >= fence.length
          && !marker[2].trim()) fence = null;
    } else if (marker) {
      fence = marker[1]; code.fill(1, offset, offset + line.length + 1);
    } else if (/^( {4}|\t)/.test(line)) code.fill(1, offset, offset + line.length + 1);
    offset = end + 1;
  }
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

function WizMarkdownEnvironmentEntities(source) {
  // Bounded chunks avoid two eager arrays containing every Unicode code point.
  var chunks = [], chunk = '';
  for (var character of source) {
    chunk += '&#' + character.codePointAt(0) + ';';
    if (chunk.length >= 8192) { chunks.push(chunk); chunk = ''; }
  }
  if (chunk) chunks.push(chunk);
  return chunks.join('');
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

function WizProtectMarkdownEquationReferences(source, readSource) {
  if (!/\\(?:eqref|ref)\{/.test(source)) return source;
  var code = WizMarkdownEnvironmentCodePositions(source), math = WizMathProtectedPositions(source);
  // Plain \ref also needs the supplemental pass when there is no $ math to
  // start the old reader's engine. Only known mathematical labels qualify;
  // theorem/quote references remain owned by the existing prose decorator.
  var mathLabels = null;
  function hasMathLabel(key) {
    if (!mathLabels) {
      // Eqref is unconditional; only an eligible plain ref needs raw labels.
      // Keep this lookup local to the call so changed notes never reuse labels.
      var raw = typeof readSource === 'function' ? readSource()
        : WizEditor.getMarkdownSrc({unEscapeHtml:true});
      var rawCode = WizMarkdownEnvironmentCodePositions(raw);
      mathLabels = new Set();
      var environments = /\\begin\{((?:equation|align|gather|multline|alignat)\*?)\}[\s\S]*?\\end\{\1\}/g, environment, label;
      while ((environment = environments.exec(raw))) {
        if (rawCode[environment.index]) continue;
        var labels = /\\label\{([A-Za-z0-9_:./-]{1,128})\}/g;
        while ((label = labels.exec(environment[0]))) mathLabels.add(label[1]);
      }
    }
    return mathLabels.has(key);
  }
  return source.replace(/\\(eqref|ref)\{([A-Za-z0-9_:./-]{1,128})\}/g, function (reference, kind, key, offset) {
    if (code[offset] || math[offset]) return reference;
    if (kind === 'ref' && !hasMathLabel(key)) return reference;
    var encoded = WizMarkdownEnvironmentEntities(reference);
    return '<span class="wiz-latex-math">' + encoded + '</span>';
  });
}

function WizConfigureMarkdownEnvironments(options) {
  var reader = options.reader || (options.reader = {});
  var callbacks = reader.callback || (reader.callback = {});
  var beforeRender = callbacks.beforeMarkdownRender, markdownRendered = callbacks.markdown;
  callbacks.beforeMarkdownRender = function (html) {
    if (beforeRender) html = beforeRender(html);
    if (!/\\begin\{(?:theorem|lemma|proposition|corollary|definition|example|remark|proof)/.test(html)
        || typeof WizRenderLatexTheorems !== 'function') return html;
    try { return WizRenderLatexTheorems(html, function () {
      return WizEditor.getMarkdownSrc({unEscapeHtml:true});
    }); }
    catch (error) { console.error('Theorem preview retained original text', error); return html; }
  };
  callbacks.markdown = function () {
    if (markdownRendered) markdownRendered.apply(this, arguments);
    WizPrepareMarkdownMathJaxScheduling();
    if (document.querySelector('.wiz-latex-theorem') && typeof WizApplyMathEnvironments === 'function'
        && !WizMathDecoration.blocks.some(function (record) {
          return record.element.isConnected && record.element.classList.contains('wiz-latex-theorem');
        })) WizApplyMathEnvironments();
  };
  var plugins = reader.markdownPlugIn || (reader.markdownPlugIn = {});
  var original = plugins.customBlockGamut;
  plugins.customBlockGamut = function (source) {
    if (original) source = original(source);
    // Share only this synchronous hook's authoritative snapshot. The callback
    // never retains it, so later hooks/notes/mode changes always read afresh.
    // In particular, keep custom callbacks outside this snapshot's lifetime.
    var rawSource, hasRawSource = false;
    function originalSource() {
      if (!hasRawSource) {
        rawSource = WizEditor.getMarkdownSrc({unEscapeHtml:true});
        hasRawSource = true;
      }
      return rawSource;
    }
    if (!/\\(?:begin|end)\{(?:theorem|lemma|proposition|corollary|definition|example|remark|proof|equation|align|gather|multline|alignat)\*?\}/.test(source)) return WizProtectMarkdownEquationReferences(source, originalSource);
    if (/\\(?:begin|end)\{(?:theorem|lemma|proposition|corollary|definition|example|remark|proof)\*\}/.test(source)) {
      var markerCode = WizMarkdownEnvironmentCodePositions(source);
      source = source.replace(/\\(?:begin|end)\{(?:theorem|lemma|proposition|corollary|definition|example|remark|proof)\*\}/g,
        function (marker, offset) { return markerCode[offset] ? marker : marker.replace('*', '&#42;'); });
    }
    if (!/\\begin\{(?:equation|align|gather|multline|alignat)\*?\}/.test(source)) return WizProtectMarkdownEquationReferences(source, originalSource);
    // The old reader doubles the first TeX row separator before this hook.
    // Recover each exact block from its public source getter instead of
    // treating that transient parser representation as authoritative text.
    var originals = null;
    var protectedCode = WizMarkdownEnvironmentCodePositions(source);
    var pattern = WizMarkdownEnvironmentPattern();
    return WizProtectMarkdownEquationReferences(source.replace(pattern, function (all, newline, indent, tex, environment, offset) {
      if (protectedCode[offset + newline.length + indent.length]) return all;
      // A begin token may occur only in a code fence or an incomplete block.
      // Fetch/build the original-byte lookup only for a renderable candidate.
      if (!originals) {
        originals = new Map();
        WizMarkdownEnvironmentBlocks(originalSource()).forEach(function (candidate) {
          // Keep first-match order, including legacy doubled-row collisions.
          if (!originals.has(candidate)) originals.set(candidate, candidate);
          var expanded = candidate.replace('\\\\','\\\\\\\\');
          if (!originals.has(expanded)) originals.set(expanded, candidate);
        });
      }
      tex = WizMarkdownEnvironmentContainerText(tex, indent);
      var originalTex = originals.get(tex);
      if (!originalTex) return all;
      // Numeric entities keep Markdown punctuation, TeX backslashes and line
      // breaks opaque through the remaining inline parser and HTML filter.
      var encoded = WizMarkdownEnvironmentEntities(originalTex);
      return newline + '<div class="wiz-bare-math-environment">' + encoded + '</div>\n';
    }), originalSource);
  };
  return options;
}

// MathJax 2 supports these configuration scripts before a fresh engine starts.
// Shorten only its stock between-section pause; leave processing budgets and
// SVG chunk yields intact, and respect a caller's non-default scheduling.
// The reader starts a fresh engine on mode changes, so do not cache Hub objects.
function WizPrepareMarkdownMathJaxScheduling() {
  if (!document.head) return;
  var id = 'wiz_markdown_math_scheduling';
  var payload = 'if(MathJax.Hub.processSectionDelay===50){MathJax.Hub.processSectionDelay=8;}';
  var previous = document.getElementById(id);
  if (previous) {
    // MathJax marks config scripts as executed. A fresh engine on the next
    // reading pass must see the same owned config again, without duplicates.
    if (previous.tagName === 'SCRIPT' && previous.getAttribute('name') === 'wiz_inner_script'
        && previous.getAttribute('wiz_style') === 'unsave' && previous.textContent === payload)
      previous.type = 'text/x-mathjax-config';
    return;
  }
  var config = document.createElement('script');
  config.id = id;
  config.type = 'text/x-mathjax-config';
  config.setAttribute('name','wiz_inner_script');
  config.setAttribute('wiz_style','unsave');
  config.textContent = payload;
  document.head.appendChild(config);
}

var WizMarkdownEnvironmentLoader = null;
function WizLoadMarkdownEnvironmentMathJax(dependencyUrl) {
  if (WizMarkdownEnvironmentLoader) return WizMarkdownEnvironmentLoader;
  // A reading pass with ordinary $...$/$$...$$ has already created the
  // current engine and live jax. Replacing it discards its equation labels.
  // Never reuse a previous pass whose body/glyph cache has been removed.
  if (window.MathJax && MathJax.Hub && MathJax.Hub.getAllJax
      && MathJax.Hub.getAllJax(document.body).length)
    return new Promise(function (resolve) { MathJax.Hub.Queue(resolve); });
  var request = new Promise(function (resolve, reject) {
    var settled = false, script;
    var deadline = setTimeout(function () { finish(new Error('Bundled MathJax initialization timed out')); }, 30000);
    function finish(error) {
      if (settled) return;
      settled = true; clearTimeout(deadline);
      if (script) script.onload = script.onerror = null;
      if (error) reject(error); else resolve();
    }
    // The original reader replaces body nodes on mode changes, including
    // MathJax's SVG glyph cache. Match its own fresh-engine-per-render policy.
    var previous = document.getElementById('wiz_markdown_environment_loader');
    if (previous) previous.remove();
    WizPrepareMarkdownMathJaxScheduling();
    window.MathJax = {
      skipStartupTypeset: true,
      tex2jax: {inlineMath:[['$','$'],['\\(','\\)']], displayMath:[['$$','$$'],['\\[','\\]']],
        processEscapes:true, processEnvironments:true},
      SVG:{linebreaks:{automatic:true}},
      TeX:{equationNumbers:{autoNumber:'AMS'},
        Safe:{allow:{URLs:'safe',classes:'safe',cssIDs:'safe',styles:'safe',fontsize:'all'}}},
      messageStyle:'none'
    };
    script = document.createElement('script');
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
  });
  var tracked = request.finally(function () {
    if (WizMarkdownEnvironmentLoader === tracked) WizMarkdownEnvironmentLoader = null;
  });
  WizMarkdownEnvironmentLoader = tracked;
  return tracked;
}

function WizInstallMarkdownEnvironmentRendering(dependencyUrl, callbacks) {
  var off = WizEditor.off, on = WizEditor.on, sequence = 0;
  var pending = null, retired = false;
  function retire(event) {
    if (event && event.persisted) return;
    retired = true; ++sequence;
    window.removeEventListener('pagehide', retire);
  }
  window.addEventListener('pagehide', retire);
  function current(token) { return !retired && sequence === token; }
  function afterPending(token, start) {
    if (!current(token)) return WizEditor;
    if (pending) {
      var resume = function () { if (current(token)) start(); };
      pending.then(resume, resume);
      return WizEditor;
    }
    return start();
  }
  WizEditor.on = function () {
    var token = ++sequence, receiver = this, args = arguments;
    // Finish local dependency initialization before restoring editable source;
    // MathJax startup must never insert its SVG cache into that source.
    return afterPending(token, function () {
      if (window.WizMarkdownEnvironmentRenderState === 'pending')
        window.WizMarkdownEnvironmentRenderState = 'cancelled';
      return on.apply(receiver, args);
    });
  };
  WizEditor.off = function (options, callback) {
    if (typeof options === 'function') { callback = options; options = null; }
    var token = ++sequence, receiver = this, waitedForMath = !!pending;
    function readReady() {
      if (!current(token)) return;
      var blocks = document.body.classList.contains('markdown-body')
        ? Array.from(document.querySelectorAll('.wiz-bare-math-environment,.wiz-latex-math'))
        .filter(function (block) { return !block.querySelector('script[type^="math/tex"]'); })
        : [];
      if (!blocks.length) { if (callback) callback(); return; }
      window.WizMarkdownEnvironmentRenderState = 'pending';
      var render = WizLoadMarkdownEnvironmentMathJax(dependencyUrl).then(function () {
        if (!current(token) || document.body.isContentEditable) return;
        return new Promise(function (resolve) {
          // Preprocess only accepted wrappers (legacy tilde fences can remain
          // plain DOM text). Reprocess all live jax as one numbering pass so
          // previously unresolved forward references see supplemental labels.
          MathJax.Hub.Queue(['PreProcess', MathJax.Hub, blocks], function () {
            if (!current(token) || document.body.isContentEditable) { resolve(); return; }
            if (MathJax.InputJax.TeX.resetEquationNumbers) MathJax.InputJax.TeX.resetEquationNumbers();
            MathJax.Hub.Queue(['Reprocess', MathJax.Hub, document.body, resolve]);
          });
        });
      }).then(function () {
        if (!current(token)) return;
        window.WizMarkdownEnvironmentRenderState = 'ready';
        if (callbacks && typeof callbacks.mathJax === 'function') callbacks.mathJax();
        if (typeof WizInstallMathReferenceNavigation === 'function') WizInstallMathReferenceNavigation();
      }).catch(function (error) {
        if (!current(token)) return;
        blocks.forEach(function (block) {
          if (!block.isConnected || block.querySelector('script[type^="math/tex"]')) return;
          var prose = document.createElement(block.tagName === 'SPAN' ? 'span' : 'p');
          prose.textContent = block.textContent; block.replaceWith(prose);
        });
        window.WizMarkdownEnvironmentRenderState = 'failed'; console.error(error);
      }).finally(function () {
        if (pending === render) pending = null;
        if (current(token) && callback) callback();
      });
      pending = render;
    }
    return afterPending(token, function () {
      // A cancelled supplemental render may already have left the engine in
      // read mode. Its public off() is then a no-op without a callback. Resume
      // only our remaining math work instead of losing the newest completion.
      var sameReader = !options || (Object.keys(options).length === 1 && options.reader
        && Object.keys(options.reader).length === 1 && options.reader.type === 'markdown');
      if (waitedForMath && sameReader && !document.body.isContentEditable
          && document.body.classList.contains('wiz-readonly')
          && document.body.classList.contains('markdown-body')) {
        readReady(); return WizEditor;
      }
      return off.call(receiver, options, readReady);
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
