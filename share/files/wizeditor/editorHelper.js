
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
  return new Promise( (resolve, reject) => {
    // Build web channel to C++ side
    new QWebChannel(qt.webChannelTransport, channel => {
      // Init WizNotePlus APIs
      const objectNames = ["WizExplorerApp", "WizQtEditor"];
      for (let i = 0; i < objectNames.length; i++) {
          const key = objectNames[i];
          window[key] = channel.objects[key];
      }
      // Init WizEditor
      try {
        if (!WizEditor) {
          alert("WizEditor is null!");
          return;
        }
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
        WizEditor.init(editorOptions);
        WizEditor.addListener(WizEditor.ListenerType.SelectionChange, WizOnSelectionChange)
        resolve(true);
      }
      catch (e) {
        console.log(e.toString());
        reject(false);
      }
    });
  });
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
      if (record.attrs[name] === null) record.element.removeAttribute(name);
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
function WizApplyMathEnvironments() {
  WizClearMathEnvironments();
  var body = document.body;
  if (!body || !body.classList.contains('markdown-body') || !body.classList.contains('wiz-readonly')
      || !window.WizNoteCss || window.WizNoteCss.indexOf('/* WizNotePlus scientific math environments v1 */') < 0) return;
  WizMathInstallSerializationGuards();
  var names = { '定理':'theorem', '引理':'lemma', '命题':'proposition', '推论':'corollary',
    '定义':'definition', '例':'example', '例子':'example', '注':'remark', '注记':'remark', '证明':'proof',
    theorem:'theorem', lemma:'lemma', proposition:'proposition', corollary:'corollary',
    definition:'definition', example:'example', remark:'remark', proof:'proof' };
  var display = { theorem:'定理', lemma:'引理', proposition:'命题', corollary:'推论',
    definition:'定义', example:'例', remark:'注', proof:'证明' };
  var labels = Object.create(null), duplicates = Object.create(null);
  var attributes = ['class','id','style','data-wiz-math-title','data-wiz-math-number','data-wiz-math-heading'];
  function parseHeading(heading) {
    if (!heading || heading.tagName !== 'P') return null;
    var strong = heading.firstElementChild;
    if (!strong || strong.tagName !== 'STRONG' || strong !== heading.children[0]) return null;
    var rest = Array.from(heading.childNodes).filter(function (node) { return node !== strong; });
    if (rest.some(function (node) { return node.nodeType !== Node.TEXT_NODE; })) return null;
    var suffix = rest.map(function (node) { return node.textContent; }).join('').trim();
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
    var record = parseHeading(block.firstElementChild);
    if (!record) return;
    record.element = block;
    var heading = record.heading, kind = record.kind;
    attributes.forEach(function (name) { record.attrs[name] = block.getAttribute(name); });
    WizMathDecoration.blocks.push(record);
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
    var record = WizMathDecoration.blocks.find(function (item) { return item.element === block; });
    var kind = record ? record.kind : Object.keys(display).find(function (name) { return block.classList.contains(name); });
    var group = /^(theorem|lemma|proposition|corollary)$/.test(kind) ? 'theorem'
      : kind === 'definition' || kind === 'example' ? kind : null;
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
  var walker = document.createTreeWalker(body, NodeFilter.SHOW_TEXT, {
    acceptNode: function (node) {
      return node.parentElement && !node.parentElement.closest('pre,code,script,style,textarea,a,.MathJax,.MathJax_Display,.MathJax_SVG,.wiz-math-source-heading')
        && node.nodeValue.indexOf('\\ref{') >= 0 ? NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_REJECT;
    }
  });
  var nodes = [], node;
  while ((node = walker.nextNode())) nodes.push(node);
  nodes.forEach(function (text) {
    var source = text.nodeValue;
    // A complete standalone inline reference is allowed; compound formulas are
    // left to the original MathJax engine. Ordinary prose references need no $.
    var pattern = /\$\s*\\ref\{([A-Za-z0-9_:.\/-]{1,128})\}\s*\$|\\\(\s*\\ref\{([A-Za-z0-9_:.\/-]{1,128})\}\s*\\\)|\\ref\{([A-Za-z0-9_:.\/-]{1,128})\}/g;
    var result, offset = 0, fragment = document.createDocumentFragment(), changed = false;
    while ((result = pattern.exec(source))) {
      var key = result[1] || result[2] || result[3], target = labels[key];
      if (!target || duplicates[key] || !target.anchor) continue;
      if (result[3] && (source.slice(0, result.index).replace(/\\\$/g,'').match(/\$/g) || []).length % 2) continue;
      fragment.appendChild(document.createTextNode(source.slice(offset, result.index)));
      var link = document.createElement('a'); link.href = '#' + target.anchor;
      link.className = 'wiz-math-reference'; link.textContent = target.number || display[target.kind];
      link.title = target.element.getAttribute('data-wiz-math-heading');
      link.addEventListener('click', function (event) {
        // Native link forwarding opens URLs externally. Keep block references
        // entirely in this page instead of asking macOS to open the note file.
        event.preventDefault();
        var destination = document.getElementById(this.getAttribute('href').slice(1));
        if (destination) destination.scrollIntoView({block:'start'});
      });
      fragment.appendChild(link); WizMathDecoration.links.push({element:link, source:result[0]});
      offset = pattern.lastIndex; changed = true;
    }
    if (changed) { fragment.appendChild(document.createTextNode(source.slice(offset))); text.replaceWith(fragment); }
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
