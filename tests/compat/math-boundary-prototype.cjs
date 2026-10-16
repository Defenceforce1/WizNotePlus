'use strict';
const fs=require('node:fs'),path=require('node:path');
module.exports=function boundaryPrototype(source){
  const lines=source.split('\n');
  const index=lines.findIndex(line=>line.includes('if (result[3] && (source.slice(0, result.index)'));
  if(index<0||!source.includes('var source = text.nodeValue;')||!source.includes('nodes.forEach(function (text) {'))throw new Error('Boundary prototype source anchor mismatch');
  lines[index]='if (mathContext && mathContext.protectedPositions[mathContext.offset + result.index]) continue;';
  return fs.readFileSync(path.join(__dirname,'prototype-math-boundaries.js'),'utf8')+'\n'
    +lines.join('\n').replace('nodes.forEach(function (text) {',
      'var mathContexts = WizProbeMathTextContexts(nodes, body); nodes.forEach(function (text) {')
      .replace('var source = text.nodeValue;', 'var source = text.nodeValue; var mathContext = mathContexts.get(text);');
};
