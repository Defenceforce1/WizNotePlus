// Isolated compatibility prototype. Uses the reader's existing public plugin
// and mode callbacks, and the bundled MathJax 2 API. Never rewrites note source.
function WizBareMathPrototypeCodePositions(source) {
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

function WizBareMathPrototypeBlocks(source) {
  var code = WizBareMathPrototypeCodePositions(source);
  var pattern = /(^|\n)( {0,3})(\\begin\{(equation\*?|align\*?)\}[\s\S]*?\\end\{\4\})(?=[ \t]*(?:\n|$))/g;
  var blocks = [], match;
  while ((match = pattern.exec(source))) {
    if (!code[match.index + match[1].length + match[2].length]) blocks.push(match[3]);
  }
  return blocks;
}

function WizConfigureBareMathPrototype(options) {
  var reader = options.reader || (options.reader = {});
  var plugins = reader.markdownPlugIn || (reader.markdownPlugIn = {});
  var original = plugins.customBlockGamut;
  plugins.customBlockGamut = function (source) {
    if (original) source = original(source);
    // The old reader doubles the first TeX row separator before this hook.
    // Recover each exact block from its public source getter instead of
    // treating that transient parser representation as authoritative text.
    var raw = WizEditor.getMarkdownSrc({unEscapeHtml:true});
    var originals = WizBareMathPrototypeBlocks(raw);
    var protectedCode = WizBareMathPrototypeCodePositions(source);
    var pattern = /(^|\n)( {0,3})(\\begin\{(equation\*?|align\*?)\}[\s\S]*?\\end\{\4\})(?=[ \t]*(?:\n|$))/g;
    return source.replace(pattern, function (all, newline, indent, tex, environment, offset) {
      if (protectedCode[offset + newline.length + indent.length]) return all;
      var originalTex = originals.find(function (candidate) {
        return candidate === tex || candidate.replace('\\\\','\\\\\\\\') === tex;
      });
      if (!originalTex) return all;
      // Numeric entities keep Markdown punctuation, TeX backslashes and line
      // breaks opaque through the remaining inline parser and HTML filter.
      var encoded = Array.from(originalTex).map(function (c) { return '&#' + c.codePointAt(0) + ';'; }).join('');
      return newline + '<div class="wiz-bare-math-environment">' + encoded + '</div>\n';
    });
  };
  return options;
}

var WizBareMathPrototypeLoader = null;
function WizLoadBareMathPrototype(dependencyUrl) {
  if (WizBareMathPrototypeLoader) return WizBareMathPrototypeLoader;
  WizBareMathPrototypeLoader = new Promise(function (resolve, reject) {
    // The original reader replaces body nodes on mode changes, including
    // MathJax's SVG glyph cache. Match its own fresh-engine-per-render policy.
    var previous = document.getElementById('wiz_bare_math_prototype_loader');
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
    script.id = 'wiz_bare_math_prototype_loader';
    script.setAttribute('name','wiz_inner_script');
    script.setAttribute('wiz_style','unsave');
    script.src = dependencyUrl.replace(/\/$/,'') + '/mathjax/MathJax.js?config=TeX-AMS-MML_SVG';
    script.onload = function () {
      if (window.MathJax && MathJax.Hub) MathJax.Hub.Queue(resolve);
      else reject(new Error('Bundled MathJax did not initialize'));
    };
    script.onerror = function () { reject(new Error('Bundled MathJax could not load')); };
    document.head.appendChild(script);
  }).finally(function () { WizBareMathPrototypeLoader = null; });
  return WizBareMathPrototypeLoader;
}

function WizInstallBareMathPrototype(dependencyUrl) {
  var off = WizEditor.off, on = WizEditor.on, sequence = 0;
  WizEditor.on = function () { ++sequence; return on.apply(this, arguments); };
  WizEditor.off = function (options, callback) {
    if (typeof options === 'function') { callback = options; options = null; }
    var token = ++sequence;
    return off.call(this, options, function () {
      var blocks = Array.from(document.querySelectorAll('.wiz-bare-math-environment'))
        .filter(function (block) { return !block.querySelector('script[type^="math/tex"]'); });
      if (!blocks.length) { if (callback) callback(); return; }
      WizLoadBareMathPrototype(dependencyUrl).then(function () {
        if (sequence !== token || document.body.isContentEditable) return;
        if (MathJax.InputJax.TeX.resetEquationNumbers) MathJax.InputJax.TeX.resetEquationNumbers();
        return new Promise(function (resolve) {
          MathJax.Hub.Queue(['Typeset', MathJax.Hub, blocks, resolve]);
        });
      }).catch(function (error) { console.error(error); }).finally(function () {
        if (callback) callback();
      });
    });
  };
}
