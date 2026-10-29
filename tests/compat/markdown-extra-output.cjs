/*
 * Original reference: Wiz official module530, SHA256
 * 2e16badeeeb2c85b39949a325d3e9f76f82dceebffc4a1bb9386b87ae465d204.
 * PageDownExtra source permission and copyright:
 * Javascript Markdown Extra Extensions for Pagedown
 * Copyright © 2012-2013 Justin McManus
 * All rights reserved.
 * 
 * PHP Markdown & Extra
 * Copyright © 2004-2013 Michel Fortin
 * All rights reserved.
 * 
 * Original Markdown
 * Copyright © 2004-2006 John Gruber
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 * 
 * Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 * 
 * Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 * 
 * Neither the name “PHP Markdown” nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 * 
 * This software is provided by the copyright holders and contributors “as is” and any express or implied warranties, including, but not limited to, the implied warranties of merchantability and fitness for a particular purpose are disclaimed. In no event shall the copyright owner or contributors be liable for any direct, indirect, incidental, special, exemplary, or consequential damages (including, but not limited to, procurement of substitute goods or services; loss of use, data, or profits; or business interruption) however caused and on any theory of liability, whether in contract, strict liability, or tort (including negligence or otherwise) arising in any way out of the use of this software, even if advised of the possibility of such damage.
 */
// Run: node --expose-internals tests/compat/markdown-extra-output.cjs
// Frozen original is an output/state oracle, not a second implementation of fragments.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert');
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
let table;
walk(acorn.parse(source, {ecmaVersion: 'latest'}), node => {
    if (node.type === 'ArrayExpression' && node.elements.length === 568 && node.elements[530]?.type === 'FunctionExpression') {
        assert(!table, 'one reviewed bundle table'); table = node;
    }
});
assert(table, 'same-version extension module');
let implementation;
walk(table.elements[530], node => {
    if (node.type === 'AssignmentExpression' && node.left.type === 'MemberExpression' && node.left.property.name === 'educatePants') {
        assert(!implementation, 'one output assembly function'); implementation = source.slice(node.right.start, node.right.end);
    }
});
assert(implementation, 'reviewed function present');
const candidate = vm.runInNewContext('(' + implementation + ')');
const reference = function(e){var t=this,n="",r=0;e.replace(/(?:<!--[\s\S]*?-->)|(<)([a-zA-Z1-6]+)([^\n]*?>)([\s\S]*?)(<\/\2>)/g,(function(o,i,a,l,s,c,d){var u=e.substring(r,d);n+=t.applyPants(u),t.smartyPantsLastChar=n.substring(n.length-1),r=d+o.length,i?(/code|kbd|pre|script|noscript|iframe|math|ins|del|pre/i.test(a)?t.smartyPantsLastChar=s.substring(s.length-1):s=t.educatePants(s),n+=i+a+l+s+c):n+=o}));var o=e.substring(r);return n+=t.applyPants(o),t.smartyPantsLastChar=n.substring(n.length-1),n};
const cases = ['', 'x', '\ud800', '😀', '<p>alpha</p>\n<p>beta</p>', '<div><p>x</p><em>y</em></div>',
    '<!--comment--><p>"quote"</p>', '<pre><code>a &amp; b</code></pre><p>x</p>', '<math><mtext>"raw"</mtext></math>',
    '<script>x<y</script><p>last</p>', '<kbd>code</kbd><del>x</del>tail', '<h1 id="heading">title</h1>',
    '<span>中😀</span>', '<p></p><p></p>', ('<p>Paragraph 中文 **emphasis**.</p>\n').repeat(6000)];
let seed = 97531;
const random = limit => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) % limit;
const blocks = ['plain "quote"', '<p>x</p>', '<em>inner</em>', '<pre>raw "x"</pre>', '<!-- c -->',
    '<div><p>nested</p></div>', '😀中\ud800', '<math>y</math>', '<br>', ''];
for (let index = 0; index < 300; ++index) {
    let input = '';
    for (let i = 0, count = random(25); i < count; ++i) input += blocks[random(blocks.length)];
    cases.push(input);
}
const handlers = [value => value, value => value.replace(/"/g, '“'), value => value ? '[' + value + ']' : '',
    value => value.replace(/x/g, ''), () => '', function (value) { return this.smartyPantsLastChar + ':' + value; }];
function context(educate, apply, initial) {
    let last = initial;
    const object = {calls: [], writes: [], educatePants: educate};
    Object.defineProperty(object, 'smartyPantsLastChar', {
        get() { return last; }, set(value) { this.writes.push(value); last = value; }
    });
    object.applyPants = function (value) { this.calls.push([value, last]); return apply.call(this, value); };
    return object;
}
let contexts = 0;
function check(input, apply, initial = 'start') {
    const a = context(reference, apply, initial), b = context(candidate, apply, initial);
    assert.equal(b.educatePants(input), a.educatePants(input));
    assert.equal(b.smartyPantsLastChar, a.smartyPantsLastChar);
    assert.deepStrictEqual(b.calls, a.calls, 'same callback arguments, receiver state and call order');
    assert.deepStrictEqual(b.writes, a.writes, 'same observable state writes, including protected tags');
    ++contexts;
}
for (const apply of handlers) for (const input of cases) check(input, apply);
for (const initial of ['', 'x', '😀']) for (const input of ['', '<div><p>x</p><code>y</code></div>', 'text😀']) {
    for (const value of [null, undefined, 37, false]) check(input, () => value, initial);
    check(input, () => ({toString() { return 'object'; }}), initial);
}
console.log(JSON.stringify({accepted: true, sourceCases: cases.length, outputStateAndCallbackTraceContexts: contexts,
    protectedTagsUnicodeEmptyCoercionCovered: true, realDataOrBrowserUsed: false}));
