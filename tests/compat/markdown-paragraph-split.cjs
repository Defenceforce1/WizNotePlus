// Run: node --expose-internals tests/compat/markdown-paragraph-split.cjs
// Synthetic exact-element and whole-converter regression. No browser or account.
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const assert = require('assert');
const acorn = require('internal/deps/acorn/acorn/dist/acorn');
const source = fs.readFileSync(path.resolve(__dirname, '../../share/files/wizeditor/wizEditorForMac.js'), 'utf8');
const walk = (node, visit) => {
    if (!node || typeof node !== 'object') return;
    visit(node);
    for (const value of Object.values(node)) {
        if (Array.isArray(value)) value.forEach(child => walk(child, visit));
        else if (value && typeof value === 'object') walk(value, visit);
    }
};
const ast = acorn.parse(source, {ecmaVersion: 'latest'});
let table;
walk(ast, node => {
    if (node.type !== 'ArrayExpression' || node.elements.length !== 568) return;
    const factory = node.elements[529];
    if (factory && factory.type === 'FunctionExpression') {
        assert(!table, 'unique legacy module table');
        table = node;
    }
});
assert(table, 'reviewed converter module');
const factory = table.elements[529];
const candidates = [];
walk(factory, node => {
    if (node.type !== 'CallExpression' || node.callee.type !== 'FunctionExpression') return;
    let hasParagraphDelimiter = false;
    walk(node.callee, child => {
        if (child.type === 'CallExpression' && child.callee.type === 'MemberExpression'
            && child.callee.property.name === 'indexOf' && child.arguments[0]?.value === '\n\n') {
            hasParagraphDelimiter = true;
        }
    });
    if (hasParagraphDelimiter) {
        candidates.push(node);
    }
});
candidates.sort((a, b) => (a.end - a.start) - (b.end - b.start));
const splitCall = candidates[0];
assert(splitCall && splitCall.arguments.length === 1, 'local synchronous paragraph scanner');
assert(candidates.every(node => node.start <= splitCall.start && node.end >= splitCall.end),
    'other matches only enclose the same scanner, no duplicate local implementation');
const scan = vm.runInNewContext('(' + source.slice(splitCall.callee.start, splitCall.callee.end) + ')');
let splitCases = 0;
const check = input => {
    assert.deepStrictEqual(Array.from(scan(input)), input.split(/\n{2,}/g));
    ++splitCases;
};
const alphabet = ['a', '\n', '\r', ' ', '😀'];
for (let number = 0; number < 3125; ++number) {
    let value = number, input = '';
    for (let index = 0; index < 5; ++index) {
        input += alphabet[value % alphabet.length];
        value = Math.floor(value / alphabet.length);
    }
    check(input);
}
for (const input of ['', '\n', '\n\n', 'a\n\nb', 'a\n\n\n\nb\n\n',
    '\ud800\n\n\udfff', '\u2028\u2029\n\n\0', ('paragraph 中文 😀\n\n').repeat(6000)]) check(input);
const unchangedInput = source.slice(splitCall.arguments[0].start, splitCall.arguments[0].end);
const baseline = source.slice(factory.start, splitCall.start)
    + '(' + unchangedInput + ').split(/\\n{2,}/g)'
    + source.slice(splitCall.end, factory.end);
const load = text => {
    const module = {exports: {}};
    vm.runInNewContext('(' + text + ')')(module, module.exports, () => {throw Error('unexpected dependency');});
    assert.equal(typeof module.exports.Converter, 'function');
    return module.exports;
};
const original = load(baseline), candidate = load(source.slice(factory.start, factory.end));
const blocks = ['plain text 中文 😀', 'line\nnext', '> quote', '- list\n- second', '<div>block</div>',
    '~~~ code', '`code` **bold**', '[link](https://example.invalid)', '    code\n    second',
    '~K123K', '$a+b=c$', '\\begin{equation}x\\end{equation}', 'text &amp; < >'];
const cases = ['', '# title\n\n> quote\n>\n> second\n\ntext **bold** *italic*\n\n',
    '[id]: https://example.invalid "title"\n\n[id]\n\n<pre>block</pre>\n\ntext',
    Array.from({length: 6000}, (_, i) => 'paragraph ' + i + ' 中文 😀 **bold**').join('\n\n'),
    '\\begin{align}a&=b\\\\c&=d\\end{align}\n\n\\eqref{eq:synthetic}',
    '| A | B |\n|---|---|\n| cell | $x$ |\n\n', '- [ ] pending\n- [x] done\n\n'];
let seed = 741923;
const random = limit => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) % limit;
for (let i = 0; i < 240; ++i) {
    let input = '';
    for (let j = 0, count = random(28); j < count; ++j) input += blocks[random(blocks.length)] + '\n'.repeat(random(5) + 1);
    cases.push(input);
}
for (const input of cases) {
    const options = {nonAsciiLetters: true, asteriskIntraWordEmphasis: true,
        imgSrcFilter: value => value, customBlockGamut: value => value};
    const a = new original.Converter(options), b = new candidate.Converter(options);
    assert.equal(b.makeHtml(input), a.makeHtml(input));
    assert.equal(b.makeHtml(input), a.makeHtml(input), 'reusing converter retains no stale paragraphs');
}
console.log(JSON.stringify({accepted: true, splitCases, fullConverterCases: cases.length,
    repeatedConversion: true, browserOrRealDataUsed: false}));
