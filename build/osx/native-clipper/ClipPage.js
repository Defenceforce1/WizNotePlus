// SPDX-License-Identifier: GPL-3.0-or-later
// Independently written Safari share preprocessing. It never edits the page,
// reads cookies/passwords/storage or fetches any network resource.
var ExtensionPreprocessingJS = {
  run: function (args) {
    function snapshot(node) {
      var copy = node.cloneNode(true);
      var originals = Array.from(node.querySelectorAll('*'));
      var clones = Array.from(copy.querySelectorAll('*'));
      var originalImages = new WeakMap();
      if (clones.length > 30000) throw new Error('Page too complex');
      // Capture typography/basic layout from the live page before removing
      // stylesheets. This is independently written; no old clipper JS reused.
      var properties = ['color', 'background-color', 'font-family', 'font-size', 'font-weight', 'font-style',
        'line-height', 'text-align', 'text-decoration', 'white-space', 'vertical-align',
        'margin-top', 'margin-bottom', 'padding', 'border-color', 'border-width', 'border-style', 'border-collapse'];
      function inlineStyle(target, original) {
        if (!original || !original.isConnected) return;
        var computed = window.getComputedStyle(original);
        var result = [];
        properties.forEach(function (name) {
          var value = computed.getPropertyValue(name);
          if (value && value.length <= 160 && !/url\s*\(|expression\s*\(|[{}<>\\\\]/i.test(value)) result.push(name + ':' + value);
        });
        if (target.tagName === 'IMG') result.push('max-width:100%;height:auto');
        target.setAttribute('style', result.join(';'));
      }
      clones.forEach(function (item, i) {
        inlineStyle(item, originals[i]);
        if (item.tagName === 'IMG' && originals[i] && originals[i].isConnected) {
          originalImages.set(item, originals[i]);
          item.setAttribute('src', originals[i].currentSrc || originals[i].src || '');
        }
      });
      inlineStyle(copy, node.isConnected ? node : document.body);
      // The dark page background may live on html rather than body. Preserve
      // that inherited backdrop so captured white text remains readable.
      if (document.body) {
        var backdrop = document.body;
        while (backdrop) {
          var color = window.getComputedStyle(backdrop).backgroundColor;
          if (color && color !== 'transparent' && !/rgba\([^)]*,\s*0\s*\)$/.test(color)) {
            copy.style.backgroundColor = color; break;
          }
          backdrop = backdrop.parentElement;
        }
      }
      var embedded = 0;
      var images = Array.from(document.images);
      copy.querySelectorAll('script,style,iframe,object,embed,input,button,select,textarea,template').forEach(function (item) { item.remove(); });
      copy.querySelectorAll('*').forEach(function (item) {
        Array.from(item.attributes).forEach(function (attribute) {
          if (/^on/i.test(attribute.name) || /^(srcdoc|formaction)$/i.test(attribute.name)) item.removeAttribute(attribute.name);
        });
        ['href', 'src'].forEach(function (name) {
          var raw = item.getAttribute(name);
          if (!raw || raw.charAt(0) === '#' || raw.indexOf('data:image/') === 0) return;
          try {
            var url = new URL(raw, document.baseURI);
            if (url.protocol === 'http:' || url.protocol === 'https:') item.setAttribute(name, url.href);
            else item.removeAttribute(name);
          } catch (_) { item.removeAttribute(name); }
        });
        if (item.tagName === 'IMG' && embedded < 1000000) {
          item.removeAttribute('srcset'); item.removeAttribute('sizes');
          var image = originalImages.get(item) || images.find(function (original) {
            return original.currentSrc === item.src || original.src === item.src;
          });
          if (!image || !image.complete || image.naturalWidth * image.naturalHeight > 2000000 || !image.naturalWidth) return;
          try {
            var canvas = document.createElement('canvas');
            canvas.width = image.naturalWidth; canvas.height = image.naturalHeight;
            canvas.getContext('2d').drawImage(image, 0, 0);
            var data = canvas.toDataURL('image/png');
            if (data.length < 500000 && embedded + data.length < 1000000) { item.setAttribute('src', data); embedded += data.length; }
          } catch (_) { /* Cross-origin images remain references; no extra request. */ }
        }
      });
      var wrapper = document.createElement('div');
      wrapper.setAttribute('style', copy.getAttribute('style') || '');
      wrapper.innerHTML = copy.innerHTML;
      return wrapper.outerHTML;
    }
    try {
      var selection = window.getSelection();
      var selected = document.createElement('div');
      if (selection) for (var i = 0; i < selection.rangeCount; i++) selected.appendChild(selection.getRangeAt(i).cloneContents());
      var page = document.body ? snapshot(document.body) : '';
      var chosen = snapshot(selected);
      // Never silently truncate saved HTML. Preserve selection as a fallback.
      if (new Blob([page]).size > 2 * 1024 * 1024) page = '';
      if (new Blob([chosen]).size > 2 * 1024 * 1024) chosen = '';
      args.completionFunction({ title: document.title, url: location.href, pageHTML: page, selectionHTML: chosen,
        text: (document.body ? document.body.innerText : '').slice(0, 50000), selectionText: selection ? selection.toString().slice(0, 50000) : '' });
    } catch (_) { args.completionFunction({title: document.title, url: location.href, pageHTML: '', selectionHTML: ''}); }
  }
};
