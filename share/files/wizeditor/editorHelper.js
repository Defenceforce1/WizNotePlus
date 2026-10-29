
function WizStartEditorAmend() {
  try {
    if (!WizEditor) {
      alert("WizEditor is null!");
      return;
    }
    //
    WizEditor.amend.on();
  }
  catch (e) {
    console.log(e.toString());
  }
}

function WizStopEditorAmend() {
  try {
    if (!WizEditor) {
      alert("WizEditor is null!");
      return;
    }
    //
    WizEditor.amend.off();
  }
  catch (e) {
    console.log(e.toString());
  }
}

////////////////////////////////////////////////////////////////////////////////

/*
  function for C++ execute
*/
function WizEditorInit(basePath, browserLang, userGUID, userAlias, ignoreTable, noteType, enableNightMode) {
  const setup = window.WizNativeEditorSetup;
  window.WizNativeEditorInitialized = false;
  return new Promise( (resolve, reject) => {
    // Build web channel to C++ side
    new QWebChannel(qt.webChannelTransport, channel => {
      try {
        // Native readiness polling alone cannot prevent a late channel
        // callback from modifying an already superseded initialization.
        if (setup && window.WizNativeEditorSetup !== setup) {
          reject(new Error('Obsolete editor initialization'));
          return;
        }
        const objectNames = ["WizExplorerApp", "WizQtEditor"];
        if (!channel || !channel.objects || objectNames.some(key => !channel.objects[key]))
          throw new Error('Native editor bridge is unavailable');
        if (!window.WizEditor || typeof WizEditor.init !== 'function')
          throw new Error('Editor engine is unavailable');
        objectNames.forEach(key => { window[key] = channel.objects[key]; });
        // Init the existing editor only after validating the current bridge.
        var user = {
          user_guid: userGUID,
          user_name: userAlias
        };
        //
        var editorOptions = {
          document: document,
          lang: browserLang,
          noteType: noteType,
          userInfo: user,
          clientType: "mac",
          ignoreTable: ignoreTable,
          dependencyUrl: basePath + 'dependency',
          nightMode: {
            enable: enableNightMode,
            color: '#a6a6a6',
            bgColor: '#272727',
          },
          editor: {
            callback: {
              onKeyDown: WizOnKeyDown,
              markerUndo: WizOnUndoStatusChanged,
              markerInitiated: WizOnMarkerInitiated,
            }
          },
          reader: {
            type: noteType,
            callback: {
              markdown: function () { WizApplyNoteStyle(); },
              mathJax: function () { WizApplyNoteStyle(); }
            }
          }
        }
        //
        WizInitMarkdownEditor(editorOptions);
        window.WizNativeEditorInitialized = true;
        WizSetSpellCheck(!!window.WizSpellCheckEnabled);
        WizEditor.addListener(WizEditor.ListenerType.SelectionChange, WizOnSelectionChange)
        resolve(true);
      }
      catch (e) {
        window.WizNativeEditorInitialized = false;
        console.log(e.toString());
        reject(e);
      }
    });
  });
}

// Native browser spellchecking for both rich text and Markdown source. The
// existing input engine stays intact; CodeMirror code blocks stay unchecked.
var WizSpellCheckState = { enabled: false, records: new Map(), originals: new WeakMap(), bodyOriginals: null, observer: null, pending: false };
function WizRestoreSpellCheckAttributes() {
  WizSpellCheckState.records.forEach(function (attributes, element) {
    Object.keys(attributes).forEach(function (name) {
      var value = attributes[name];
      if (value === null) element.removeAttribute(name);
      else element.setAttribute(name, value);
    });
  });
  WizSpellCheckState.records.clear();
}
function WizApplySpellCheck() {
  var state = WizSpellCheckState;
  if (!document.body) return;
  state.records.forEach(function (_, element) {
    if (!element.isConnected) state.records.delete(element);
  });
  function attribute(element, name, value) {
    // The editor itself also writes spellcheck=false during mode/history
    // changes. Keep the first attribute value across serialization calls;
    // otherwise that engine reset can become a new, incorrectly saved value.
    var current = element.getAttribute(name);
    var baseline = state.originals.get(element) || {};
    if (element === document.body) {
      // The original reader replaces the body during on/off. Those new roots
      // still belong to the same note, not a new user-provided attribute set.
      if (!state.bodyOriginals) state.bodyOriginals = {
        spellcheck: element.getAttribute('spellcheck'), lang: element.getAttribute('lang')
      };
      if (!Object.prototype.hasOwnProperty.call(baseline, name)) baseline[name] = state.bodyOriginals[name];
    }
    if (!Object.prototype.hasOwnProperty.call(baseline, name)) baseline[name] = current;
    state.originals.set(element, baseline);
    if (current === value && current === baseline[name]) return;
    var original = state.records.get(element) || {};
    if (!Object.prototype.hasOwnProperty.call(original, name)) original[name] = baseline[name];
    state.records.set(element, original);
    if (current !== value) element.setAttribute(name, value);
  }
  // Editor.init/on/off and history restore reset spellcheck=false internally.
  // Track only the actual prose editing root, never URL/title/search fields.
  var roots = document.querySelectorAll('.wiz-editor-body');
  roots.forEach(function (body) {
    var editing = body.isContentEditable && !body.classList.contains('wiz-readonly');
    attribute(body, 'spellcheck', state.enabled && editing ? 'true' : 'false');
    if (state.enabled && editing) attribute(body, 'lang', 'en-US');
    body.querySelectorAll('pre,code,.wiz-code-container,.CodeMirror,.MathJax,.MathJax_Display,.MathJax_SVG,math')
      .forEach(function (code) { attribute(code, 'spellcheck', 'false'); });
  });
}
function WizSetSpellCheck(enabled) {
  var state = WizSpellCheckState;
  state.enabled = !!enabled;
  window.WizSpellCheckEnabled = state.enabled;
  WizRestoreSpellCheckAttributes();
  state.originals = new WeakMap();
  WizApplySpellCheck();
  if (!state.observer && document.body) {
    state.observer = new MutationObserver(function () {
      if (state.pending) return;
      state.pending = true;
      setTimeout(function () { state.pending = false; WizApplySpellCheck(); }, 50);
    });
    state.observer.observe(document.body, {subtree:true, childList:true, attributes:true,
      attributeFilter:['spellcheck','class','contenteditable']});
  }
  if (window.WizEditor && !WizEditor.wizSpellCheckSerializationGuards) {
    ['getContentHtml','getMarkdownSrc','isModified'].forEach(function (name) {
      var original = WizEditor[name];
      if (typeof original !== 'function') return;
      WizEditor[name] = function () {
        WizRestoreSpellCheckAttributes();
        try { return original.apply(WizEditor, arguments); }
        finally { WizApplySpellCheck(); }
      };
    });
    WizEditor.wizSpellCheckSerializationGuards = true;
  }
}

function WizPrepareNoteModeChange() {
  // Let the existing editor capture only source attributes, not temporary
  // spelling controls or read-only mathematical decoration, before cloning.
  WizRestoreSpellCheckAttributes();
  WizClearMathEnvironments();
}

// Temporary engine style: excluded from saved note HTML by the original editor.
function WizApplyNoteStyle(css) {
  if (typeof css === 'string') window.WizNoteCss = css;
  if (!window.WizEditor || typeof WizEditor.insertCustomStyle !== 'function') return;
  if (window.WizNoteCss) WizEditor.insertCustomStyle('wiz_tmp_style_note_presentation', window.WizNoteCss, true);
  else WizEditor.removeStyleById('wiz_tmp_style_note_presentation');
  WizApplyMathEnvironments();
}

// Optional read-only decoration. Never change the editor/parser or saved source.
// The preset marker is an explicit opt-in; normal quotes and code are untouched.
var WizMathDecoration = { blocks: [], links: [], splits: [] };
var WizMathReferenceNavigation = { timer: null, sequence: 0 };
// Tracking installed roots must not keep a replaced reader body alive.
var WizMathReferenceRoots = new WeakSet();
function WizInstallMathReferenceNavigation() {
  var root = document.body;
  if (!root || WizMathReferenceRoots.has(root)) return;
  WizMathReferenceRoots.add(root);
  // The reader replaces body/document nodes, losing MathJax's per-node click
  // handlers. Delegate on each new root and handle SVG children as well as HTML.
  root.addEventListener('click', function (event) {
    if (event.defaultPrevented || event.button !== 0 || event.ctrlKey || event.metaKey
        || event.altKey || event.shiftKey || document.body !== root || root.isContentEditable) return;
    var node = event.target && event.target.nodeType === 1 ? event.target : event.target.parentElement;
    var link = node && node.closest('a');
    if (!link || !(link.classList.contains('wiz-math-reference')
        || link.closest('.MathJax_SVG,.MathJax,.MathJax_Display'))) return;
    var href = link.getAttribute('href') || link.getAttributeNS('http://www.w3.org/1999/xlink','href');
    if (!href) return;
    try {
      var target = new URL(href, document.baseURI), current = new URL(location.href);
      var anchor = decodeURIComponent(target.hash.slice(1));
      target.hash = current.hash = '';
      if ((!href.startsWith('#') && target.href !== current.href)
          || !/^(?:mjx-eqn-|wiz-math-)[A-Za-z0-9_:./-]{1,128}$/.test(anchor)
          || !document.getElementById(anchor)) return;
      event.preventDefault(); event.stopPropagation();
      WizNavigateMathReference(anchor);
    } catch (_) { /* Ordinary or malformed links keep their original behavior. */ }
  }, true);
}
function WizClearMathReferenceNavigation() {
  var state = WizMathReferenceNavigation;
  if (state.timer !== null) clearTimeout(state.timer);
  state.timer = null; ++state.sequence;
  if (window.WizEditor && typeof WizEditor.removeStyleById === 'function')
    WizEditor.removeStyleById('wiz_tmp_style_math_reference_navigation');
}
function WizNavigateMathReference(anchor) {
  var destination = document.getElementById(anchor);
  if (!destination) return;
  WizClearMathReferenceNavigation();
  if (/^wiz-math-[A-Za-z0-9_:./-]{1,128}$/.test(anchor) && window.WizEditor
      && typeof WizEditor.insertCustomStyle === 'function') {
    WizEditor.insertCustomStyle('wiz_tmp_style_math_reference_navigation',
      '@media screen { body.wiz-readonly.markdown-body [id="' + anchor + '"] {'
      + 'outline:2px solid #4b69c6!important;outline-offset:4px!important;scroll-margin-top:24px!important;}}', true);
    var sequence = WizMathReferenceNavigation.sequence;
    WizMathReferenceNavigation.timer = setTimeout(function () {
      if (WizMathReferenceNavigation.sequence === sequence) WizClearMathReferenceNavigation();
    }, 1600);
  }
  destination.scrollIntoView({block:'start'});
}
function WizMathInstallSerializationGuards() {
  if (!window.WizEditor) return;
  if (!WizEditor.wizMathSerializationGuards) {
  ['getContentHtml', 'getMarkdownSrc'].forEach(function (name) {
    var original = WizEditor[name];
    if (typeof original !== 'function') return;
    WizEditor[name] = function () {
      WizClearMathEnvironments();
      try { return original.apply(WizEditor, arguments); }
      finally { WizApplyMathEnvironments(); }
    };
  });
  WizEditor.wizMathSerializationGuards = true;
  }
  if (window.WizReader && typeof WizReader.closeDocument === 'function' && !WizReader.wizMathSerializationGuard) {
    var closeDocument = WizReader.closeDocument;
    WizReader.closeDocument = function () {
      WizClearMathReferenceNavigation();
      WizClearMathEnvironments();
      try { return closeDocument.apply(WizReader, arguments); }
      finally { WizApplyMathEnvironments(); }
    };
    WizReader.wizMathSerializationGuard = true;
  }
}
function WizClearMathEnvironments() {
  WizMathDecoration.links.forEach(function (record) {
    if (record.element.parentNode) record.element.replaceWith(document.createTextNode(record.source));
  });
  WizMathDecoration.blocks.forEach(function (record) {
    Object.keys(record.attrs).forEach(function (name) {
      if (record.attrs[name] === null) {
        // Flush pending CSSOM serialization before restoring an absent style
        // attribute; otherwise Blink can materialize style="" after removal.
        if (name === 'style') record.element.getAttribute(name);
        record.element.removeAttribute(name);
      }
      else record.element.setAttribute(name, record.attrs[name]);
    });
    if (record.heading) {
      if (record.headingClass === null) record.heading.removeAttribute('class');
      else record.heading.setAttribute('class', record.headingClass);
    }
  });
  WizMathDecoration.splits.forEach(function (record) {
    record.children.forEach(function (child) { record.element.appendChild(child); });
    record.created.forEach(function (element) { element.remove(); });
  });
  WizMathDecoration = { blocks: [], links: [], splits: [] };
}
// Reference recognition must not consume TeX belonging to the original engine.
// Protection flags are built once per logical text scope, before DOM changes;
// their Maps/arrays live only for this decoration pass, not for the page lifetime.
function WizMathProtectedPositions(source, packed) {
  var flags = new Uint8Array(packed ? Math.ceil((source.length + 1) / 8) : source.length + 1);
  // With no possible math opener, every protection flag is already zero.
  // Limit the native precheck to long scopes; keep all original scan rules.
  if (source.length >= 4096 && source.indexOf("$") < 0 &&
      source.indexOf("\\(") < 0 &&
      source.indexOf("\\[") < 0 &&
      source.indexOf("\\begin{") < 0) return flags;
  var delimiter = null, braces = 0, environments = [];
  var environmentPattern = /\\(begin|end)\{([^{}]+)\}/y;
  for (var i = 0; i < source.length;) {
    var before = delimiter !== null || environments.length !== 0;
    var c = source[i], next = source[i+1], length = 1;
    if (c === '\\') {
      if (next === '\\' || next === '$' || next === '{' || next === '}') length = 2;
      else {
        environmentPattern.lastIndex = i;
        var environment = environmentPattern.exec(source);
        if (environment) {
          if (environment[1] === 'begin') environments.push(environment[2]);
          else if (environments[environments.length-1] === environment[2]) environments.pop();
          length = environment[0].length;
        } else if (!delimiter && (next === '(' || next === '[')) {
          delimiter = next === '(' ? ')' : ']'; length = 2;
        } else if (!braces && !environments.length && delimiter === next && (next === ')' || next === ']')) {
          delimiter = null; length = 2;
        }
      }
    } else if (delimiter && c === '{') ++braces;
    else if (delimiter && c === '}' && braces) --braces;
    else if (c === '$' && !environments.length) {
      var token = next === '$' ? '$$' : '$';
      if (!delimiter) delimiter = token;
      else if (delimiter === token && !braces) delimiter = null;
      length = token.length;
    }
    if (packed) { if (before) flags[i >>> 3] |= 1 << (i & 7); }
    else flags[i] = before ? 1 : 0;
    // A delimiter's interior cannot start another inline-reference wrapper.
    var inside = before || delimiter !== null || environments.length !== 0;
    for (var j = 1; j < length && i+j < source.length; ++j) {
      if (packed) { if (inside) flags[(i+j) >>> 3] |= 1 << ((i+j) & 7); }
      else flags[i+j] = inside ? 1 : 0;
    }
    i += length;
  }
  if (packed) {
    if (delimiter !== null || environments.length !== 0) flags[source.length >>> 3] |= 1 << (source.length & 7);
  } else flags[source.length] = delimiter !== null || environments.length !== 0 ? 1 : 0;
  return flags;
}
function WizMathTextContexts(nodes, body, packed) {
  var contexts = new Map(), scopes = new Set(), requested = new Set(nodes);
  var selector = 'p,li,td,th,blockquote,h1,h2,h3,h4,h5,h6,div';
  var excluded = 'pre,code,script,style,textarea,a,.MathJax,.MathJax_Display,.MathJax_SVG,.wiz-math-source-heading';
  nodes.forEach(function (text) { scopes.add(text.parentElement.closest(selector) || body); });
  scopes.forEach(function (scope) {
    if (scope.closest(excluded)) return;
    // An element-aware walker can reject whole nested scopes/code subtrees.
    // Text-only walkers still visit every descendant of those rejected areas.
    // A large scope with no rejected descendants needs no JavaScript filter.
    // Smaller scopes and any code/nested-block/math output keep the old walker.
    var plainInline = scope.childElementCount >= 64 && !scope.querySelector(excluded + ',' + selector);
    var walker = document.createTreeWalker(scope,
      plainInline ? NodeFilter.SHOW_TEXT : (NodeFilter.SHOW_ELEMENT | NodeFilter.SHOW_TEXT),
      plainInline ? null : {acceptNode:function (node) {
        if (node.nodeType === Node.ELEMENT_NODE)
          return node.matches(excluded) || node.matches(selector) ? NodeFilter.FILTER_REJECT : NodeFilter.FILTER_SKIP;
        return NodeFilter.FILTER_ACCEPT;
      }});
    var parts = [], offsets = [], length = 0, node;
    while ((node = walker.nextNode())) {
      if (requested.has(node)) offsets.push({node:node, offset:length});
      parts.push(node.nodeValue); length += node.nodeValue.length;
    }
    var protectedPositions = WizMathProtectedPositions(parts.join(''), packed);
    offsets.forEach(function (entry) { contexts.set(entry.node, {protectedPositions:protectedPositions, offset:entry.offset}); });
  });
  return contexts;
}
function WizApplyMathEnvironments() {
  WizInstallMathReferenceNavigation();
  WizClearMathEnvironments();
  var body = document.body;
  if (body && body.isContentEditable && window.MathJax && MathJax.Hub
      && typeof MathJax.Hub.WizRetireDisconnected === 'function')
    MathJax.Hub.WizRetireDisconnected();
  var preset = window.WizNoteCss && window.WizNoteCss.indexOf('/* WizNotePlus scientific math environments v1 */') >= 0;
  if (!body || !body.classList.contains('markdown-body') || !body.classList.contains('wiz-readonly')
      || !preset && !body.querySelector('.wiz-latex-theorem')) {
    WizClearMathReferenceNavigation();
    if (window.WizEditor && typeof WizEditor.removeStyleById === 'function') WizEditor.removeStyleById('wiz_tmp_style_latex_theorems');
    return;
  }
  // Explicit LaTeX environments opt in themselves. Ordinary quotes still need
  // the existing preset, and a minimal scoped fallback never alters their CSS.
  if (!preset) WizEditor.insertCustomStyle('wiz_tmp_style_latex_theorems',
    '.markdown-body .wiz-latex-theorem.wiz-math-block{border-left:3px solid #4b69c6;padding:.7em 1em;margin:1em 0;}'
    + '.markdown-body .wiz-latex-theorem.wiz-math-block::before{content:attr(data-wiz-math-heading);display:block;font-weight:bold;margin-bottom:.4em;}'
    + '.markdown-body .wiz-latex-theorem>.wiz-math-source-heading{display:none;}'
    + '.markdown-body .wiz-latex-theorem.proof::after{content:"□";display:block;text-align:right;}', true);
  else WizEditor.removeStyleById('wiz_tmp_style_latex_theorems');
  WizMathInstallSerializationGuards();
  var names = { '定理':'theorem', '引理':'lemma', '命题':'proposition', '推论':'corollary',
    '定义':'definition', '例':'example', '例子':'example', '注':'remark', '注记':'remark', '证明':'proof',
    theorem:'theorem', lemma:'lemma', proposition:'proposition', corollary:'corollary',
    definition:'definition', example:'example', remark:'remark', proof:'proof' };
  var display = { theorem:'定理', lemma:'引理', proposition:'命题', corollary:'推论',
    definition:'定义', example:'例', remark:'注', proof:'证明' };
  var labels = Object.create(null), duplicates = Object.create(null);
  var blockRecords = new Map();
  var headingRecords = new WeakMap();
  var attributes = ['class','id','style','data-wiz-math-title','data-wiz-math-number','data-wiz-math-heading'];
  function parseHeading(heading) {
    if (!heading || heading.tagName !== 'P') return null;
    if (!headingRecords.has(heading)) headingRecords.set(heading, readHeading(heading));
    return headingRecords.get(heading);
  }
  function readHeading(heading) {
    if (!heading || heading.tagName !== 'P') return null;
    var strong = heading.firstElementChild;
    if (!strong || strong.tagName !== 'STRONG' || strong !== heading.children[0]) return null;
    var suffix = '';
    for (var child = heading.firstChild; child; child = child.nextSibling) {
      if (child === strong) continue;
      if (child.nodeType !== Node.TEXT_NODE) return null;
      suffix += child.nodeValue;
    }
    suffix = suffix.trim();
    var label = suffix.match(/^\\label\{([A-Za-z0-9_:.\/-]{1,128})\}$/);
    if (suffix && !label) return null;
    var title = strong.textContent.trim();
    var match = title.match(/^(定理|引理|命题|推论|定义|例子|例|注记|注|证明|theorem|lemma|proposition|corollary|definition|example|remark|proof)(?=$|\s|[0-9（(:：])\s*(.*)$/i);
    if (!match) return null;
    var kind = names[match[1].toLowerCase()], caption = match[2];
    var manual = caption.match(/^([0-9]+(?:\.[0-9]+)*)(?=$|\s|[（(:：])\s*(.*)$/);
    return {heading:heading, headingClass:heading.getAttribute('class'), attrs:{},
      kind:kind, manual:manual && manual[1], caption:manual ? manual[2] : caption, label:label && label[1]};
  }
  // Markdown joins adjacent quoted paragraphs. Split only the rendered DOM at
  // explicit math headings; serialization restores the original node order.
  body.querySelectorAll('blockquote').forEach(function (block) {
    if (!preset && !block.classList.contains('wiz-latex-theorem')) return;
    var children = Array.from(block.childNodes), created = [], current = block;
    var hasContent = false;
    children.forEach(function (child) {
      if (child.nodeType === Node.ELEMENT_NODE && parseHeading(child) && hasContent) {
        var next = document.createElement('blockquote');
        current.after(next); current = next; created.push(next); hasContent = false;
      }
      if (current !== block) current.appendChild(child);
      if (child.nodeType === Node.ELEMENT_NODE || child.textContent.trim()) hasContent = true;
    });
    if (created.length) WizMathDecoration.splits.push({element:block,children:children,created:created});
  });
  body.querySelectorAll('blockquote').forEach(function (block) {
    if (!preset && !block.classList.contains('wiz-latex-theorem')) return;
    var record = parseHeading(block.firstElementChild);
    if (!record) return;
    record.element = block;
    var heading = record.heading, kind = record.kind;
    attributes.forEach(function (name) { record.attrs[name] = block.getAttribute(name); });
    WizMathDecoration.blocks.push(record);
    blockRecords.set(block, record);
    block.classList.add(kind, 'wiz-math-block');
    heading.classList.add('wiz-math-source-heading');
    block.setAttribute('data-wiz-math-title', record.caption);
    if (record.label) {
      if (labels[record.label]) duplicates[record.label] = true;
      else labels[record.label] = record;
    }
  });
  var counters = { theorem:0, definition:0, example:0 };
  // Walk original HTML environments as well, so mixed div/quote notes keep order.
  body.querySelectorAll('.theorem,.lemma,.proposition,.corollary,.definition,.example,.remark,.proof').forEach(function (block) {
    var record = blockRecords.get(block);
    var kind = record ? record.kind : Object.keys(display).find(function (name) { return block.classList.contains(name); });
    var group = /^(theorem|lemma|proposition|corollary)$/.test(kind) ? 'theorem'
      : kind === 'definition' || kind === 'example' ? kind : null;
    if (record && block.classList.contains('wiz-latex-unnumbered')) {
      group = null; block.style.counterIncrement = 'none';
    }
    if (group) ++counters[group];
    if (!record) return;
    record.number = record.manual || (group ? String(counters[group]) : '');
    if (record.manual && group && /^[0-9]+$/.test(record.manual)) {
      counters[group] = Number(record.manual);
      block.style.counterReset = group + '-counter ' + record.manual;
      block.style.counterIncrement = 'none';
    }
    block.setAttribute('data-wiz-math-number', record.number);
    var rendered = display[kind] + (record.number ? ' ' + record.number : '') + '. ' + record.caption;
    if (record.label && duplicates[record.label]) rendered += '（标签重复：' + record.label + '）';
    block.setAttribute('data-wiz-math-heading', rendered);
    if (record.label && !duplicates[record.label]) {
      record.anchor = 'wiz-math-' + record.label;
      if (document.getElementById(record.anchor) && document.getElementById(record.anchor) !== block) {
        duplicates[record.label] = true;
      } else block.id = record.anchor;
    }
  });
  // No theorem/definition target means this pass cannot create any prose link.
  // Keep equation references with MathJax and avoid an unused body-wide scan.
  if (!Object.keys(labels).some(function (key) {
    return !duplicates[key] && !!labels[key].anchor;
  })) return;
  // Only request contexts for nodes that can link to a current target.
  // Context construction still reads every text part in each requested scope,
  // including skipped nodes that may open or close a math delimiter.
  var referenceTargets = /\\ref\{([A-Za-z0-9_:.\/-]{1,128})\}/g;
  var walker = document.createTreeWalker(body, NodeFilter.SHOW_TEXT, {
    acceptNode: function (node) {
      if (node.nodeValue.indexOf('\\ref{') < 0 || !node.parentElement
          || node.parentElement.closest('pre,code,script,style,textarea,a,.MathJax,.MathJax_Display,.MathJax_SVG,.wiz-math-source-heading'))
        return NodeFilter.FILTER_REJECT;
      referenceTargets.lastIndex = 0;
      var match;
      while ((match = referenceTargets.exec(node.nodeValue))) {
        var target = labels[match[1]];
        if (target && !duplicates[match[1]] && target.anchor) return NodeFilter.FILTER_ACCEPT;
      }
      return NodeFilter.FILTER_REJECT;
    }
  });
  var nodes = [], node;
  while ((node = walker.nextNode())) nodes.push(node);
  var mathContexts = WizMathTextContexts(nodes, body, true);
  nodes.forEach(function (text) {
    var source = text.nodeValue;
    var mathContext = mathContexts.get(text);
    // A complete standalone inline reference is allowed; compound formulas are
    // left to the original MathJax engine. Ordinary prose references need no $.
    var pattern = /\$\s*\\ref\{([A-Za-z0-9_:.\/-]{1,128})\}\s*\$|\\\(\s*\\ref\{([A-Za-z0-9_:.\/-]{1,128})\}\s*\\\)|\\ref\{([A-Za-z0-9_:.\/-]{1,128})\}/g;
    var result, offset = 0, fragment = document.createDocumentFragment(), changed = false;
    while ((result = pattern.exec(source))) {
      var key = result[1] || result[2] || result[3], target = labels[key];
      if (!target || duplicates[key] || !target.anchor) continue;
      var protectedIndex = mathContext && mathContext.offset + result.index;
      if (!mathContext || (mathContext.protectedPositions[protectedIndex >>> 3] & (1 << (protectedIndex & 7)))) continue;
      // Serializers clear and rebuild these links. Avoid accumulating two empty
      // text nodes per reference on every read/save/style refresh.
      if (result.index > offset) fragment.appendChild(document.createTextNode(source.slice(offset, result.index)));
      var link = document.createElement('a'); link.href = '#' + target.anchor;
      link.className = 'wiz-math-reference'; link.textContent = target.number || display[target.kind];
      link.title = target.element.getAttribute('data-wiz-math-heading');
      link.addEventListener('click', function (event) {
        // Native link forwarding opens URLs externally. Keep block references
        // entirely in this page instead of asking macOS to open the note file.
        event.preventDefault();
        WizNavigateMathReference(this.getAttribute('href').slice(1));
      });
      fragment.appendChild(link); WizMathDecoration.links.push({element:link, source:result[0]});
      offset = pattern.lastIndex; changed = true;
    }
    if (changed) { if (offset < source.length) fragment.appendChild(document.createTextNode(source.slice(offset))); text.replaceWith(fragment); }
  });
}

function WizOnSelectionChange(style) {
  try {
    WizQtEditor.onSelectionChange(JSON.stringify(style));
  } catch (e) {

  }
}

function WizOnKeyDown(event) {
  try {
    //
    if (event.key === 'Enter') {
      if (!WizEditor.macFirstReturnKeyPressed) {
        WizEditor.macFirstReturnKeyPressed = true;
        //
        try {
          WizQtEditor.onReturn();
        } catch (err) {
          console.error(err);
        }
      }
    } else if (event.charCode === 99 && event.metaKey) {
        try {
            WizQtEditor.doCopy();
        } catch (err) {
            console.error(err);
        }
    }
  } catch (e) {
    console.error(e);
  } finally {
    return true;
  }
}

function WizOnUndoStatusChanged(data) {
  WizQtEditor.onMarkerUndoStatusChanged(data);
}

function WizOnMarkerInitiated(data) {
  WizQtEditor.onMarkerInitiated(data);
}



function WizAddCssForCode(cssFile) {
  console.log("WizAddCssForCode called , css file " + cssFile);
  var doc = document;
  if (!doc)
    return;
  //
  var oldLink = doc.getElementById('wiz_code_highlight_link');
  if (oldLink) {
    console.log("old css link find, try to remove");
    oldLink.parentNode.removeChild(oldLink);
  }
  //
  var link = doc.createElement('link');
  if (!link)
    return;
  try {
    link.type = 'text/css';
    link.rel = 'stylesheet';
    link.id = 'wiz_code_highlight_link';
    link.href = cssFile;
    //
    if (!doc.head) {
      doc.insertBefore(doc.createElement('head'), doc.body);
    }
    doc.head.appendChild(link);
  }
  catch (e) {

  }
}
