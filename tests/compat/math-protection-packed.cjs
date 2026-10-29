// Run: node tests/compat/math-protection-packed.cjs [optional candidate helper]
// Compare the actual owned scanner, both default-dense and internal-packed,
// against its frozen pre-packing reference at every UTF16 position.
const fs=require('fs'),path=require('path'),assert=require('assert');
const file=process.argv[2]||path.resolve(__dirname,'../../share/files/wizeditor/editorHelper.js');
const source=fs.readFileSync(file,'utf8'),start=source.indexOf('function WizMathProtectedPositions('),end=source.indexOf('\nfunction WizMathTextContexts(',start);
assert(start>=0&&end>start);const candidate=(new Function('return ('+source.slice(start,end).trim()+')'))();
const reference=(new Function('return ('+"function WizMathProtectedPositions(source) {\n  var flags = new Uint8Array(source.length + 1);\n  var delimiter = null, braces = 0, environments = [];\n  var environmentPattern = /\\\\(begin|end)\\{([^{}]+)\\}/y;\n  for (var i = 0; i < source.length;) {\n    var before = delimiter !== null || environments.length !== 0;\n    var c = source[i], next = source[i+1], length = 1;\n    if (c === '\\\\') {\n      if (next === '\\\\' || next === '$' || next === '{' || next === '}') length = 2;\n      else {\n        environmentPattern.lastIndex = i;\n        var environment = environmentPattern.exec(source);\n        if (environment) {\n          if (environment[1] === 'begin') environments.push(environment[2]);\n          else if (environments[environments.length-1] === environment[2]) environments.pop();\n          length = environment[0].length;\n        } else if (!delimiter && (next === '(' || next === '[')) {\n          delimiter = next === '(' ? ')' : ']'; length = 2;\n        } else if (!braces && !environments.length && delimiter === next && (next === ')' || next === ']')) {\n          delimiter = null; length = 2;\n        }\n      }\n    } else if (delimiter && c === '{') ++braces;\n    else if (delimiter && c === '}' && braces) --braces;\n    else if (c === '$' && !environments.length) {\n      var token = next === '$' ? '$$' : '$';\n      if (!delimiter) delimiter = token;\n      else if (delimiter === token && !braces) delimiter = null;\n      length = token.length;\n    }\n    flags[i] = before ? 1 : 0;\n    // A delimiter's interior cannot start another inline-reference wrapper.\n    var inside = before || delimiter !== null || environments.length !== 0;\n    for (var j = 1; j < length && i+j < source.length; ++j) flags[i+j] = inside ? 1 : 0;\n    i += length;\n  }\n  flags[source.length] = delimiter !== null || environments.length !== 0 ? 1 : 0;\n  return flags;\n}"+')'))();

const samples=["", "x", "$", "$$", "\\$", "\\\\", "\\(x\\)", "\\[x\\]", "\\begin{align}x\\end{align}", "\\begin{align}\\begin{cases}x\\end{cases}\\end{align}", "${x$}y$", "\ud83d\ude00\u4e2d\ud800 $x$", "\\(x{\\)}y}\\)", "\\begin{a}x\\end{b}", "$unclosed", "\\[unclosed", "\\begin{a}unclosed"];
const tokens=["plain", "\u4e2d\ud83d\ude00", "\\", "\\\\", "\\$", "$", "$$", "\\(", "\\)", "\\[", "\\]", "{", "}", "\\begin{align}", "\\end{align}", "\\begin{cases}", "\\end{cases}", "\\ref{target}", "\n"];
const boundaryTokens=["$x$", "\\(x\\)", "\\begin{align}x\\end{align}", "\\$x"];

let seed=285;const random=n=>(seed=(Math.imul(seed,1664525)+1013904223)>>>0)%n;
for(let n=0;n<300;n++){let text='';for(let i=0,m=1+random(70);i<m;i++)text+=tokens[random(tokens.length)];samples.push(text);}
for(let n=0;n<40;n++)for(const text of boundaryTokens)samples.push('x'.repeat(n)+text+'y'.repeat(n%9));
samples.push(('long plain 中文 $x+y$ ').repeat(60000)+String.fromCharCode(92)+'ref{target}');
// Long zero-state and late-opener cases exercise the guarded native precheck.
const slash=String.fromCharCode(92);
for(const n of [4095,4096,4097])samples.push('x'.repeat(n-(slash+'ref{target}').length)+slash+'ref{target}');
samples.push(('plain 中文 '+slash+'ref{target} ').repeat(1000));
samples.push('x'.repeat(5000)+slash+'end{align}'+slash+')'+slash+']');
for(const opener of ['$',slash+'(',slash+'[',slash+'begin{align}']){
 samples.push('x'.repeat(5000)+opener+'z');
 samples.push(opener+'z'+'x'.repeat(5000));
}
samples.push('x'.repeat(5000)+slash+slash+'('+'z');
let positions=0;for(const text of samples){
 const expected=reference(text),dense=candidate(text),packed=candidate(text,true);
 assert(dense instanceof Uint8Array&&packed instanceof Uint8Array);
 assert.strictEqual(dense.length,text.length+1);assert.strictEqual(packed.byteLength,Math.ceil((text.length+1)/8));
 for(let i=0;i<=text.length;i++){assert.strictEqual(dense[i],expected[i],'dense API at '+i);assert.strictEqual((packed[i>>>3]>>>(i&7))&1,expected[i],'packed state at '+i);positions++;}
 for(let i=text.length+1;i<packed.length*8;i++)assert.strictEqual((packed[i>>>3]>>>(i&7))&1,0);
}
console.log(JSON.stringify({accepted:true,sourceCases:samples.length,positionComparisons:positions,defaultDenseApiPreserved:true,
 everyPackedPositionMatchesReference:true,unusedPaddingBitsZero:true,wholeClientAccepted:false}));
