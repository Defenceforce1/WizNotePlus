/* Default public MathJax 2 API -> MathJax 4 compatibility bridge.
 * Preserves the existing editor API; MathJax 4 is the bundled default engine.
 * Real MathJax 4 performs all parsing/layout; retained TeX scripts are the
 * serialization contract used by the existing private-paper PDF pipeline.
 */
(function () {
  'use strict';
  var base = new URL('.', document.currentScript.src), legacy = window.MathJax || {};
  // The legacy editor stylesheet gives .MathJax a two-pixel inline padding.
  // V4 also uses that class, but its full-width SVG containers must not inherit
  // this V2-only gutter or A4 would overflow by four pixels.
  if (!document.getElementById('wiz_mathjax4_compat_style')) {
    var style = document.createElement('style'); style.id = 'wiz_mathjax4_compat_style';
    style.setAttribute('name','wiz_inner_style'); style.setAttribute('wiz_style','unsave');
    style.textContent = 'mjx-container.MathJax{padding:0!important;border:0!important;box-sizing:border-box!important}';
    document.head.appendChild(style);
  }
  var trace = window.WizMathJax4Trace || (window.WizMathJax4Trace = []);
  // Keep only recent diagnostics; a long-lived note must not accumulate a log.
  function traceOperation(value) {
    trace.push(value);
    if (trace.length > 256) trace.splice(0, trace.length - 256);
  }
  var engine, nextId = 0, queue = Promise.resolve(), ready;
  var records = [], placeholders = new WeakMap();
  function scopeNodes(nodes) {
    if (!nodes) return [document.body];
    return (Array.isArray(nodes) ? nodes : [nodes]).map(function (n) {
      return typeof n === 'string' ? document.getElementById(n) : n;
    }).filter(Boolean);
  }
  function contained(node, scopes) { return scopes.some(function (root) { return root === node || root.contains(node); }); }
  function liveRecords(nodes) {
    var scopes = scopeNodes(nodes);
    return records.filter(function (record) { return record.script.isConnected && contained(record.script, scopes); });
  }
  function removeVisual(record) {
    if (record.frame) record.frame.remove();
    if (record.placeholder) record.placeholder.remove();
  }
  function normalizedId(id) { return id.replace(/^mjx-eqn:/, 'mjx-eqn-'); }
  function finishOutput() {
    var pendingFits = [], knownItems = new Set(records.map(function (record) { return record.item; }));
    Array.from(engine.startup.document.math).forEach(function (item) {
      if (!item.typesetRoot || !item.typesetRoot.isConnected || knownItems.has(item)) return;
      // MathJax may replace item.start.node during output. Bind retained input
      // to the stable MathItem identity so repeated processing reuses its script.
      var input = placeholders.get(item);
      var script = input && input.script, placeholder = input ? input.node : item.start.node;
      if (!script) {
        script = document.createElement('script'); script.type = 'math/tex' + (item.display ? '; mode=display' : '');
        script.textContent = item.math; item.typesetRoot.after(script); placeholder = null;
      }
      if (!script.id) script.id = 'WizMathJax4-Element-' + (++nextId);
      var frame = document.createElement(item.display ? 'div' : 'span');
      frame.id = script.id + '-Frame'; frame.className = item.display ? 'MathJax_SVG_Display' : 'MathJax_SVG';
      item.typesetRoot.before(frame); frame.appendChild(item.typesetRoot);
      if (item.display) item.typesetRoot.classList.add('MathJax_SVG');
      if (item.display) pendingFits.push(item);
      var record = {item:item,script:script,frame:frame,placeholder:placeholder,originalText:item.math,inputID:script.id,
        SourceElement:function () { return script; },
        root:{toMathML:function () { return engine.startup.toMML(item.root); }},
        Remove:function () { engine.typesetClear([frame]); removeVisual(record); records = records.filter(function (r) { return r !== record; }); }};
      records.push(record); knownItems.add(item);
    });
    // Do not let retained record callbacks hold this pass-only identity index.
    knownItems = null;
    pendingFits.forEach(function (item) {
      // V4 may keep a container-width viewBox around an explicitly unbreakable
      // multline row, with visible ink outside that box. Include the real ink
      // before local SVG fitting, otherwise Chromium can silently clip it even
      // when document.scrollWidth fits. This never scales prose/the page.
      if (item.display) item.typesetRoot.querySelectorAll(':scope > svg').forEach(function (svg) {
        var box = svg.viewBox.baseVal, ink = svg.getBBox();
        if (!(box.width > 0) && svg.hasAttribute('data-mjx-viewBox')) {
          // Full-width V4 tables use pixel-transformed groups instead of an
          // SVG viewBox. Freeze that generated pixel geometry into a real
          // viewBox so local max-width fitting cannot crop a long row.
          var rect = svg.getBoundingClientRect();
          // Retain native full-width output when its ink already fits. DOMRect
          // heights at large page offsets can round by one layout unit (1/64px).
          // Paper retypesetting keeps the established fixed geometry: its SVG
          // number helper must survive the later print viewport change.
          if (!window.__wizPaperResult && ink.x + ink.width <= rect.width && ink.y + ink.height <= rect.height + 1 / 64) return;
          var pixelWidth = Math.max(rect.width,ink.x+ink.width), pixelHeight = Math.max(rect.height,ink.y+ink.height);
          if (!(pixelWidth > 0 && pixelHeight > 0)) return;
          svg.setAttribute('viewBox',[0,0,pixelWidth,pixelHeight].join(' '));
          svg.setAttribute('width',pixelWidth+'px'); svg.setAttribute('height',pixelHeight+'px');
          svg.style.width = pixelWidth+'px'; svg.style.maxWidth = '100%'; svg.style.minWidth = '0'; svg.style.height = 'auto';
          item.typesetRoot.style.minWidth = '0'; item.typesetRoot.style.maxWidth = '100%';
          svg.setAttribute('preserveAspectRatio','xMidYMid meet'); svg.setAttribute('data-wiz-mjx4-ink-fit','');
          return;
        }
        if (!(box.width > 0 && box.height > 0) || ink.x + ink.width <= box.x + box.width + 1) return;
        var scale = svg.getBoundingClientRect().width / box.width;
        var x = Math.min(box.x,ink.x), y = Math.min(box.y,ink.y);
        var width = Math.max(box.x+box.width,ink.x+ink.width)-x;
        var height = Math.max(box.y+box.height,ink.y+ink.height)-y;
        svg.setAttribute('viewBox',[x,y,width,height].join(' '));
        svg.setAttribute('width',width*scale+'px'); svg.setAttribute('height',height*scale+'px');
        svg.style.width = width*scale+'px'; svg.style.maxWidth = '100%'; svg.style.minWidth = '0'; svg.style.height = 'auto';
        svg.setAttribute('preserveAspectRatio','xMidYMid meet');
        svg.setAttribute('data-wiz-mjx4-ink-fit','');
      });
    });
    // Keep the established document-local target namespace. MathJax's label
    // table remains untouched; normalize only actual output nodes and links.
    document.querySelectorAll('[id^="mjx-eqn:"]').forEach(function (node) { node.id = normalizedId(node.id); });
    document.querySelectorAll('mjx-container a').forEach(function (a) {
      var href = a.getAttribute('href') || a.getAttributeNS('http://www.w3.org/1999/xlink','href') || a.getAttribute('data-mjx-href');
      if (!href) return;
      var url = new URL(href, location.href), current = new URL(location.href);
      if (url.protocol !== current.protocol || url.host !== current.host || url.pathname !== current.pathname || url.search !== current.search) return;
      var id = normalizedId(decodeURIComponent(url.hash.slice(1)));
      if (!id.startsWith('mjx-eqn-')) return;
      a.setAttribute('href', '#' + encodeURIComponent(id)); a.setAttributeNS('http://www.w3.org/1999/xlink','href','#' + encodeURIComponent(id));
    });
  }
  function findScripts(doc, scriptsOnly) {
    if (!scriptsOnly) doc.findMath();
    scopeNodes(doc.options.elements).forEach(function (root) {
      var scripts = (root.matches && root.matches('script[type^="math/tex"]') ? [root] : []).concat(Array.from(root.querySelectorAll('script[type^="math/tex"]')));
      scripts.forEach(function (script) {
        if (records.some(function (r) { return r.script === script; })) return;
        var item = new doc.options.MathItem(script.textContent, doc.inputJax[0], /;\s*mode=display/.test(script.type));
        var text = document.createTextNode(''); script.before(text); placeholders.set(item, {script:script,node:text});
        item.start = {node:text,delim:'',n:0}; item.end = {node:text,delim:'',n:0}; doc.math.push(item);
      });
    });
  }
  function retireDisconnectedOutput() {
    // DOM replacement can remove both source scripts and generated frames.
    // Retire their engine items before dropping our records, without touching
    // connected output or the TeX source used for serialization.
    var retired = records.filter(function (r) {
      return !r.script.isConnected && r.frame && !r.frame.isConnected;
    });
    if (retired.length) engine.typesetClear(retired.map(function (r) { return r.frame; }));
    records = records.filter(function (r) { return r.script.isConnected; });
    // typesetPromise stores its last scan roots. Completed detached scopes are
    // no longer input; every later render supplies its own explicit scopes.
    var elements = engine.startup.document.options.elements;
    if (Array.isArray(elements)) engine.startup.document.options.elements = elements.filter(function (node) {
      return !node || !node.nodeType || node.isConnected;
    });
  }
  var scriptsOnly = false;
  async function render(method, nodes) {
    await ready;
    if (method === 'PreProcess') return;
    var scopes = scopeNodes(nodes);
    retireDisconnectedOutput();
    if (method === 'Reprocess' || method === 'Rerender') {
      var removed = liveRecords(nodes); engine.typesetClear(scopes); removed.forEach(removeVisual);
      records = records.filter(function (r) { return removed.indexOf(r) < 0; }); engine.texReset();
    }
    scriptsOnly = method === 'Process';
    try { await engine.typesetPromise(scopes); finishOutput(); }
    finally { scriptsOnly = false; }
  }
  function invoke(task) {
    if (typeof task === 'function') return task();
    if (!Array.isArray(task)) throw Error('Unsupported MathJax compatibility queue item');
    var method = task[0], receiver = task[1], args = task.slice(2);
    traceOperation(String(method));
    if (['Typeset','Process','PreProcess','Reprocess','Rerender'].includes(method))
      return render(method,args[0]).then(function () { if (typeof args[1] === 'function') return args[1](); });
    if (receiver && typeof receiver[method] === 'function') return receiver[method].apply(receiver,args);
    throw Error('Unsupported MathJax compatibility method: ' + method);
  }
  function enqueue() {
    Array.from(arguments).forEach(function (task) { queue = queue.then(function () { return ready; }).then(function () { return invoke(task); }); });
    queue.catch(function (error) { window.WizMathJax4Error = String(error); console.error('MathJax 4 compatibility', error); });
    return queue;
  }
  var hub = {config:legacy,processSectionDelay:8,Queue:enqueue,
    Config:function (config) { traceOperation('Config'); Object.assign(hub.config,config); },
    getAllJax:liveRecords,Typeset:function (nodes,callback) { return enqueue(['Typeset',hub,nodes,callback]); },
    WizRetireDisconnected:function () { return enqueue(function () { retireDisconnectedOutput(); }); },
    Register:{StartupHook:function (name,callback) { traceOperation('StartupHook:'+name); return enqueue(callback); },MessageHook:function () {}},
    signal:{Post:function () {}},Startup:{signal:{Post:function () {}}}};
  legacy.Hub = hub; legacy.InputJax = {TeX:{resetEquationNumbers:function () { if (engine) engine.texReset(); }}};
  // Do not load speech, enrichment, menus or remote fonts in this experiment.
  var config = {Hub:hub,InputJax:legacy.InputJax,
    startup:{typeset:false},loader:{load:['ui/safe','[tex]/colorv2'],paths:{'mathjax-tex':new URL('tex-font',base).href}},
    options:{enableMenu:false,enableSpeech:false,enableBraille:false,menuOptions:{settings:{enrich:false,speech:false,braille:false,assistiveMml:false,collapsible:false}},
      renderActions:{find:[10,function (doc) { findScripts(doc,scriptsOnly); },'']}},
    output:{font:'mathjax-tex',fontPath:new URL('tex-font',base).href,displayOverflow:'linebreak',linebreaks:{inline:false}},
    svg:{fontCache:'local'},tex:{packages:{'[+]':['colorv2'],'[-]':['textmacros']},tags:'ams',inlineMath:[['$','$'],['\\(','\\)']],displayMath:[['$$','$$'],['\\[','\\]']],processEscapes:true,processEnvironments:true,processRefs:true}};
  window.MathJax = config;
  ready = new Promise(function (resolve,reject) {
    var script = document.createElement('script'); script.src = new URL('v4/tex-mml-svg-nofont.js',base).href;
    script.setAttribute('name','wiz_inner_script'); script.setAttribute('wiz_style','unsave');
    script.onerror = function () { reject(Error('Local MathJax 4 engine could not load')); };
    script.onload = function () { engine = window.MathJax; engine.Hub = hub; engine.InputJax = legacy.InputJax;
      engine.startup.promise.then(function () { resolve(); },reject); };
    document.head.appendChild(script);
  });
  window.WizMathJax4Ready = ready;
})();
