/* Separate, temporary print rendering. Uses the bundled reader and formula engine. */
(function () {
  'use strict';
  var config = window.WizPrintConfig;
  var finished = false;
  function fail(error) { window.WizPrintError = String(error || '无法渲染打印副本'); }
  function ready() {
    if (finished) return;
    finished = true;
    var css = '@media print { html, body { background:white !important; color:#222; height:auto !important; min-height:0 !important; }'
      + '.wiz-editor-body, .wiz-reader-body, .markdown-body { height:auto !important; min-height:0 !important; }'
      + 'body { margin:0 !important; padding:0 !important; }'
      + '.wiz-editor-toolbar-container, .wiz-float-layer { display:none !important; }'
      + 'img, svg { max-width:100%; } h1,h2,h3 { break-after:avoid; }'
      + '.MathJax_Display, tr { break-inside:avoid; } pre { white-space:pre-wrap; }}\n' + config.css;
    WizReader.insertCustomStyle('wiz_tmp_style_print_presentation', css, true);
    var jobs = Array.prototype.map.call(document.images, function (image) {
      return new Promise(function (resolve, reject) {
        function check() { image.naturalWidth ? resolve() : reject(new Error('图片资源加载失败，尚未打印')); }
        if (image.complete) { check(); return; }
        image.addEventListener('load', check, { once: true });
        image.addEventListener('error', function () { reject(new Error('图片资源加载失败，尚未打印')); }, { once: true });
      });
    });
    if (document.fonts && document.fonts.ready) jobs.push(document.fonts.ready);
    Promise.all(jobs).then(function () {
      requestAnimationFrame(function () { requestAnimationFrame(function () { window.WizPrintReady = true; }); });
    }).catch(fail);
  }
  try {
    if (!config || !window.WizEditor) throw new Error('打印渲染资源缺失');
    WizEditor.init({
      document: document, lang: 'zh-cn', clientType: 'web', noteType: config.type,
      dependencyUrl: config.dependency, reader: { type: config.type, timeout: { markdown: 45000 },
        callback: { mathJax: function () {} } }
    });
    WizEditor.off({ reader: { type: config.type } }, function () {
      // Drain the original MathJax queue, including equation labels/references.
      if (window.MathJax && MathJax.Hub) MathJax.Hub.Queue(ready);
      else ready();
    });
  } catch (error) { fail(error); }
}());
