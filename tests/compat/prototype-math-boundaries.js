// Exploration only. Keep out of application assets until compatibility QA.
// All Maps/flags are local to one pass, never a persistent page cache.
function WizProbeMathStates(source) {
  var flags = new Uint8Array(source.length + 1);
  var delimiter = null, braces = 0, environments = [];
  for (var i = 0; i < source.length;) {
    var before = delimiter !== null || environments.length !== 0;
    var c = source[i], next = source[i+1], length = 1;
    if (c === '\\') {
      if (next === '\\' || next === '$' || next === '{' || next === '}') length = 2;
      else {
        var environment = source.slice(i).match(/^\\(begin|end)\{([^{}]+)\}/);
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
    flags[i] = before ? 1 : 0;
    // Interior positions of an opening/closing delimiter are not a new wrapper.
    var inside = before || delimiter !== null || environments.length !== 0;
    for (var j = 1; j < length && i+j < source.length; ++j) flags[i+j] = inside ? 1 : 0;
    i += length;
  }
  flags[source.length] = delimiter !== null || environments.length !== 0 ? 1 : 0;
  return flags;
}
function WizProbeMathTextContexts(nodes, body) {
  var contexts = new Map(), scopes = new Set();
  var selector = 'p,li,td,th,blockquote,h1,h2,h3,h4,h5,h6,div';
  var excluded = 'pre,code,script,style,textarea,a,.MathJax,.MathJax_Display,.MathJax_SVG,.wiz-math-source-heading';
  nodes.forEach(function (text) { scopes.add(text.parentElement.closest(selector) || body); });
  scopes.forEach(function (scope) {
    var walker = document.createTreeWalker(scope, NodeFilter.SHOW_TEXT, {acceptNode:function (node) {
      return node.parentElement && !node.parentElement.closest(excluded)
        && (node.parentElement.closest(selector) || body) === scope
        ? NodeFilter.FILTER_ACCEPT : NodeFilter.FILTER_REJECT;
    }});
    var parts = [], offsets = [], length = 0, node;
    while ((node = walker.nextNode())) {
      offsets.push({node:node, offset:length}); parts.push(node.nodeValue); length += node.nodeValue.length;
    }
    var protectedPositions = WizProbeMathStates(parts.join(''));
    offsets.forEach(function (entry) { contexts.set(entry.node, {protectedPositions:protectedPositions, offset:entry.offset}); });
  });
  return contexts;
}
