// SPDX-License-Identifier: GPL-3.0-or-later
// Detached static HTML, not Markdown. No credential/cookie/storage reads.
var ExtensionPreprocessingJS = {
  run: function (args) {
    var resources = [], warnings = 0, formulaCount = 0;
    var properties = ['color', 'background-color', 'font-family', 'font-size', 'font-weight', 'font-style',
      'line-height', 'text-align', 'text-decoration', 'white-space', 'vertical-align',
      'margin-top', 'margin-bottom', 'padding', 'border-color', 'border-width', 'border-style', 'border-collapse'];
    var htmlTags = new Set(('p div span article section main aside header footer h1 h2 h3 h4 h5 h6 a img br hr wbr strong b em i u s small sub sup pre code blockquote ul ol li dl dt dd table thead tbody tfoot tr td th caption colgroup col figure figcaption details summary').split(' '));
    var mathTags = new Set(('math mi mn mo mrow mfrac msqrt mroot msub msup msubsup mtext mover munder munderover mtable mtr mtd semantics annotation mstyle mpadded mspace menclose mfenced mmultiscripts mprescripts none maligngroup malignmark').split(' '));
    function inlineStyle(target, original) {
      if (!original || !original.isConnected || mathTags.has(target.localName)) return;
      var computed = window.getComputedStyle(original), result = [];
      properties.forEach(function (name) {
        var value = computed.getPropertyValue(name);
        if (value && value.length <= 160 && !/url\s*\(|expression\s*\(|[{}<>\\]/i.test(value)) result.push(name + ':' + value);
      });
      if (target.localName === 'img') result.push('max-width:100%;height:auto');
      target.setAttribute('style', result.join(';'));
    }
    function normalizeMath(root, original) {
      var math = root.querySelector('math');
      if (!math && original && window.MathJax && window.MathJax.Hub) {
        try {
          var jax = window.MathJax.Hub.getJaxFor(original);
          if (jax && jax.root && typeof jax.root.toMathML === 'function') {
            var parsed = new DOMParser().parseFromString(jax.root.toMathML(), 'application/xml');
            if (parsed.documentElement.localName === 'math') math = parsed.documentElement;
          }
        } catch (_) { /* Never load extra page scripts. */ }
      }
      if (!math) return false;
      var visible = math.cloneNode(true);
      [visible].concat(Array.from(visible.querySelectorAll('*'))).forEach(function (item) {
        item.removeAttribute('style'); item.removeAttribute('hidden'); item.removeAttribute('aria-hidden');
      });
      var display = root.classList.contains('MathJax_Display') || root.classList.contains('katex-display')
        || root.getAttribute('display') === 'true' || visible.getAttribute('display') === 'block';
      visible.setAttribute('display', display ? 'block' : 'inline');
      var wrapper = document.createElement(display ? 'div' : 'span');
      wrapper.setAttribute('class', 'wiz-clip-formula'); wrapper.appendChild(visible);
      root.replaceWith(wrapper); formulaCount++; return true;
    }
    function snapshot(node) {
      var copy = node.cloneNode(true), originals = Array.from(node.querySelectorAll('*')),
        clones = Array.from(copy.querySelectorAll('*')), originalNodes = new WeakMap(), embedded = 0;
      if (clones.length > 100000) throw new Error('Page too complex');
      clones.forEach(function (item, i) { originalNodes.set(item, originals[i]); inlineStyle(item, originals[i]); });
      inlineStyle(copy, node.isConnected ? node : document.body);
      var backdrop = document.body;
      while (backdrop) {
        var color = window.getComputedStyle(backdrop).backgroundColor;
        if (color && color !== 'transparent' && !/rgba\([^)]*,\s*0\s*\)$/.test(color)) { copy.style.backgroundColor = color; break; }
        backdrop = backdrop.parentElement;
      }
      copy.querySelectorAll('.katex-display,.MathJax_Display,mjx-container,.katex,.MathJax,.MathJax_SVG').forEach(function (root) {
        if (copy.contains(root)) normalizeMath(root, originalNodes.get(root));
      });
      copy.querySelectorAll('.MathJax_Preview,.MJX_Assistive_MathML,mjx-assistive-mml').forEach(function (item) {
        if (!normalizeMath(item, originalNodes.get(item))) item.remove();
      });
      copy.querySelectorAll('script,style,iframe,object,embed,input,button,select,textarea,template,link,meta,base').forEach(function (item) { item.remove(); });
      var walker = document.createTreeWalker(copy, NodeFilter.SHOW_COMMENT), comments = [];
      while (walker.nextNode()) comments.push(walker.currentNode);
      comments.forEach(function (item) { item.remove(); });
      copy.querySelectorAll('img').forEach(function (item) {
        var original = originalNodes.get(item), raw = original ? (original.currentSrc || original.src) : item.getAttribute('src');
        item.removeAttribute('srcset'); item.removeAttribute('sizes');
        item.removeAttribute('data-wiz-clip-resource');
        if (original && original.complete && original.naturalWidth && original.naturalWidth * original.naturalHeight <= 2000000) {
          try {
            var canvas = document.createElement('canvas'); canvas.width = original.naturalWidth; canvas.height = original.naturalHeight;
            canvas.getContext('2d').drawImage(original, 0, 0);
            var data = canvas.toDataURL('image/png');
            if (data.length < 500000 && embedded + data.length < 2000000) { raw = data; embedded += data.length; }
          } catch (_) { /* Cross-origin capture uses the native downloader. */ }
        }
        if (/^data:image\/(png|jpeg|gif|webp);base64,[A-Za-z0-9+/=]+$/.test(raw || '')) { item.setAttribute('src', raw); return; }
        try {
          var address = new URL(raw || '', document.baseURI);
          if (!raw || address.protocol !== 'https:' || address.username || address.password) throw new Error('unsafe image');
          var id = resources.indexOf(address.href);
          if (id < 0) { if (resources.length >= 48) throw new Error('too many images'); id = resources.push(address.href) - 1; }
          item.setAttribute('src', address.href); item.setAttribute('data-wiz-clip-resource', String(id));
        } catch (_) { item.replaceWith(document.createTextNode('[图片未嵌入，请查看原网页]')); warnings++; }
      });
      function clean(root) {
        Array.from(root.children).forEach(function (item) {
          var tag = item.localName.toLowerCase();
          if (tag === 'svg' || tag === 'canvas') {
            item.replaceWith(document.createTextNode('[图形未能静态保存，请查看原网页]')); warnings++; return;
          }
          clean(item);
          if (!htmlTags.has(tag) && !mathTags.has(tag)) { item.replaceWith.apply(item, Array.from(item.childNodes)); return; }
          Array.from(item.attributes).forEach(function (attr) {
            var name = attr.name.toLowerCase(), value = attr.value;
            var allow = ['id', 'class', 'style', 'alt', 'title', 'lang', 'dir', 'colspan', 'rowspan', 'width', 'height'].indexOf(name) >= 0;
            if (name === 'data-wiz-clip-resource' && tag === 'img' && /^\d+$/.test(value)) allow = true;
            if (mathTags.has(tag) && ['xmlns', 'display', 'encoding', 'mathvariant', 'mathsize', 'mathcolor', 'mathbackground', 'displaystyle', 'scriptlevel', 'columnalign', 'rowalign', 'columnspacing', 'rowspacing', 'stretchy', 'fence', 'separator', 'accent', 'accentunder', 'linethickness', 'lspace', 'rspace'].indexOf(name) >= 0) allow = true;
            if (name === 'href' && tag === 'a') {
              try { var url = new URL(value, document.baseURI); allow = ['http:', 'https:'].indexOf(url.protocol) >= 0 && !url.username && !url.password; if (allow) item.setAttribute(name, url.href); }
              catch (_) { allow = false; }
            }
            if (name === 'src' && tag === 'img') allow = true;
            if (name === 'style' && allow) {
              // Selected ranges are detached, so their original inline styles
              // have not necessarily been replaced with computed properties.
              item.setAttribute('style', value.split(';').filter(function (declaration) {
                var colon = declaration.indexOf(':'), key = declaration.slice(0, colon).trim().toLowerCase();
                var val = declaration.slice(colon + 1).trim();
                return colon > 0 && properties.concat(['max-width','height']).indexOf(key) >= 0 && val.length <= 160
                  && !/url\s*\(|expression\s*\(|[{}<>\\]/i.test(val);
              }).join(';'));
            }
            if (name === 'style' && mathTags.has(tag)) allow = false;
            if (!allow || /^on/i.test(name)) item.removeAttribute(attr.name);
          });
        });
      }
      clean(copy);
      var wrapper = document.createElement('div'); wrapper.setAttribute('style', copy.getAttribute('style') || '');
      wrapper.innerHTML = copy.innerHTML; return wrapper.outerHTML;
    }
    try {
      var selection = window.getSelection(), selected = document.createElement('div');
      if (selection) for (var i = 0; i < selection.rangeCount; i++) selected.appendChild(selection.getRangeAt(i).cloneContents());
      var page = document.body ? snapshot(document.body) : '', chosen = selected.childNodes.length ? snapshot(selected) : '';
      if (new Blob([page]).size > 8 * 1024 * 1024) page = '';
      if (new Blob([chosen]).size > 8 * 1024 * 1024) chosen = '';
      args.completionFunction({title: document.title, url: location.href, pageHTML: page, selectionHTML: chosen, resources: resources,
        formulaCount: formulaCount, warnings: warnings,
        text: (document.body ? document.body.innerText : '').slice(0, 50000), selectionText: selection ? selection.toString().slice(0, 50000) : ''});
    } catch (_) { args.completionFunction({title: document.title, url: location.href, pageHTML: '', selectionHTML: ''}); }
  }
};
