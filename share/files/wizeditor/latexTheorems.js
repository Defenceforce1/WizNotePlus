// Read-only LaTeX-style theorem projection. The original reader still owns
// source, editing and serialization; no translated Markdown is written back.
var WizLatexTheoremNames = {
  theorem:'定理', lemma:'引理', proposition:'命题', corollary:'推论',
  definition:'定义', example:'例', remark:'注', proof:'证明'
};

function WizLatexTheoremMask(source) {
  return source.replace(/\\(?:begin|end)\{(?:theorem|lemma|proposition|corollary|definition|example|remark|proof)\*?\}/g,
    function (text) { return ' '.repeat(text.length); });
}

function WizLatexTheoremRecords(source) {
  var code = WizMarkdownEnvironmentCodePositions(source);
  var math = WizMathProtectedPositions(WizLatexTheoremMask(source));
  var pattern = /(^|\n) {0,3}\\(begin|end)\{((?:theorem|lemma|proposition|corollary|definition|example|remark|proof)\*?)\}/g;
  var stack = [], records = [], match;
  while ((match = pattern.exec(source))) {
    var start = match.index + match[1].length, command = source.indexOf('\\', start);
    if (code[command] || math[command]) continue;
    var name = match[3];
    if (match[2] === 'begin') {
      var cursor = pattern.lastIndex, title = '';
      while (source[cursor] === ' ' || source[cursor] === '\t') ++cursor;
      if (source[cursor] === '[') {
        var first = ++cursor, brackets = 1, braces = 0;
        for (; cursor < source.length && source[cursor] !== '\n'; ++cursor) {
          var c = source[cursor];
          if (c === '\\') { ++cursor; continue; }
          if (c === '{') ++braces;
          else if (c === '}' && braces) --braces;
          else if (!braces && c === '[') ++brackets;
          else if (!braces && c === ']' && !--brackets) break;
        }
        if (brackets || cursor >= source.length) continue;
        title = source.slice(first, cursor++);
      }
      var lineEnd = source.indexOf('\n', cursor);
      if (lineEnd < 0) continue;
      var suffix = source.slice(cursor, lineEnd).trim();
      var label = suffix.match(/^\\label\{([A-Za-z0-9_:./-]{1,128})\}$/);
      if (suffix && !label) continue;
      if (stack.length >= 16) return [];
      stack.push({start:start, name:name, title:title, label:label && label[1], bodyStart:lineEnd + 1});
    } else {
      if (!stack.length) continue;
      if (stack[stack.length - 1].name !== name) return [];
      if (source.slice(pattern.lastIndex, source.indexOf('\n', pattern.lastIndex) < 0
        ? source.length : source.indexOf('\n', pattern.lastIndex)).trim()) return [];
      var record = stack.pop();
      record.end = pattern.lastIndex;
      record.body = source.slice(record.bodyStart, start);
      if (!record.label) {
        var leading = record.body.match(/^\s*\\label\{([A-Za-z0-9_:./-]{1,128})\}[ \t]*(?:\n|$)/);
        if (leading) { record.label = leading[1]; record.body = record.body.slice(leading[0].length); }
      }
      if (!stack.length) records.push(record);
    }
  }
  return records;
}

function WizLatexTheoremEntities(source) {
  return Array.from(source).map(function (c) { return '&#' + c.codePointAt(0) + ';'; }).join('');
}

function WizLatexTheoremBody(source, depth) {
  if (depth > 16) throw new Error('Theorem nesting exceeds the supported limit');
  WizLatexTheoremRecords(source).reverse().forEach(function (record) {
    source = source.slice(0, record.start) + '\n\n' + WizLatexTheoremHtml(record, depth + 1)
      + '\n\n' + source.slice(record.end);
  });
  var code = WizMarkdownEnvironmentCodePositions(source), spans = [], match;
  var environments = WizMarkdownEnvironmentPattern();
  while ((match = environments.exec(source))) {
    var offset = match.index + match[1].length + match[2].length;
    if (!code[offset]) spans.push({start:offset, end:environments.lastIndex,
      html:'\n<div class="wiz-bare-math-environment">'
        + WizLatexTheoremEntities(WizMarkdownEnvironmentContainerText(match[3], match[2])) + '</div>\n'});
  }
  var delimiters = /\$\$[\s\S]*?\$\$|\$(?:\\.|[^\n$\\])+?\$|\\\([\s\S]*?\\\)|\\\[[\s\S]*?\\\]/g;
  while ((match = delimiters.exec(source))) {
    var backslashes = 0;
    for (var i = match.index - 1; i >= 0 && source[i] === '\\'; --i) ++backslashes;
    if (code[match.index] || backslashes % 2 || spans.some(function (span) {
      return match.index < span.end && delimiters.lastIndex > span.start;
    })) continue;
    var block = match[0].slice(0, 2) === '$$' || match[0].slice(0, 2) === '\\[';
    var tag = block ? 'div' : 'span', className = block ? 'wiz-bare-math-environment' : 'wiz-latex-math';
    spans.push({start:match.index, end:delimiters.lastIndex,
      html:'<' + tag + ' class="' + className + '">' + WizLatexTheoremEntities(match[0]) + '</' + tag + '>'});
  }
  var references = /\\eqref\{[A-Za-z0-9_:./-]{1,128}\}/g;
  var math = WizMathProtectedPositions(source);
  while ((match = references.exec(source))) {
    if (code[match.index] || math[match.index] || spans.some(function (span) {
      return match.index < span.end && references.lastIndex > span.start;
    })) continue;
    spans.push({start:match.index,end:references.lastIndex,
      html:'<span class="wiz-latex-math">' + WizLatexTheoremEntities(match[0]) + '</span>'});
  }
  spans.sort(function (a, b) { return b.start - a.start; }).forEach(function (span) {
    source = source.slice(0, span.start) + span.html + source.slice(span.end);
  });
  if (!window.marked || typeof marked.parse !== 'function') throw new Error('Local theorem Markdown parser unavailable');
  return marked.parse(source, {async:false, gfm:true, breaks:true});
}

function WizLatexTheoremHtml(record, depth) {
  var kind = record.name.replace(/\*$/, ''), starred = /\*$/.test(record.name);
  var title = WizLatexTheoremNames[kind] + (record.title ? '（' + record.title + '）' : '');
  return '<blockquote class="wiz-latex-theorem' + (starred ? ' wiz-latex-unnumbered' : '') + '">'
    + '<p><strong>' + WizLatexTheoremEntities(title) + '</strong>'
    + (record.label ? ' \\label{' + record.label + '}' : '') + '</p>'
    + WizLatexTheoremBody(record.body, depth || 0) + '</blockquote>';
}

function WizRenderLatexTheorems(html, source) {
  var records = WizLatexTheoremRecords(source);
  if (!records.length) return html;
  var template = document.createElement('template'); template.innerHTML = html;
  var walker = document.createTreeWalker(template.content, NodeFilter.SHOW_TEXT, {acceptNode:function (node) {
    return node.parentElement && !node.parentElement.closest('pre,code,script,style,textarea')
      ? NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_REJECT;
  }});
  var entries = [], parts = [], length = 0, node;
  while ((node = walker.nextNode())) {
    entries.push({node:node, offset:length}); parts.push(node.nodeValue + '\n'); length += node.nodeValue.length + 1;
  }
  var text = parts.join(''), code = WizMarkdownEnvironmentCodePositions(text);
  var math = WizMathProtectedPositions(WizLatexTheoremMask(text));
  var tokens = /(^|\n) {0,3}\\(begin|end)\{((?:theorem|lemma|proposition|corollary|definition|example|remark|proof)\*?)\}/g;
  var stack = [], matches = [], match;
  function position(offset) {
    var entry = entries.find(function (item) { return offset >= item.offset && offset <= item.offset + item.node.length; });
    return entry && {node:entry.node, offset:offset - entry.offset};
  }
  while ((match = tokens.exec(text))) {
    var start = match.index + match[1].length, command = text.indexOf('\\', start);
    if (code[command] || math[command]) continue;
    if (match[2] === 'begin') stack.push({name:match[3], start:command});
    else if (stack.length) {
      if (stack[stack.length - 1].name !== match[3]) return html;
      var open = stack.pop();
      if (!stack.length) matches.push({name:open.name, start:position(open.start), end:position(tokens.lastIndex)});
    }
  }
  if (matches.length !== records.length || matches.some(function (item, index) {
    return item.name !== records[index].name || !item.start || !item.end;
  })) return html;
  // Construct all replacements before changing DOM: a parse failure retains the
  // entire original preview instead of leaving a partly translated environment.
  var replacements = records.map(function (record) { return WizLatexTheoremHtml(record, 0); });
  matches.reverse().forEach(function (item, reversedIndex) {
    var range = template.content.ownerDocument.createRange();
    range.setStart(item.start.node, item.start.offset); range.setEnd(item.end.node, item.end.offset);
    var first = item.start.node.parentElement.closest('p'), last = item.end.node.parentElement.closest('p');
    if (first) {
      var prefix = range.cloneRange(); prefix.selectNodeContents(first); prefix.setEnd(item.start.node, item.start.offset);
      if (!prefix.toString().trim()) range.setStartBefore(first);
    }
    if (last) {
      var suffix = range.cloneRange(); suffix.selectNodeContents(last); suffix.setStart(item.end.node, item.end.offset);
      if (!suffix.toString().trim()) range.setEndAfter(last);
    }
    var fragment = document.createElement('template'); fragment.innerHTML = replacements[records.length - 1 - reversedIndex];
    range.deleteContents(); range.insertNode(fragment.content);
  });
  return template.innerHTML;
}
