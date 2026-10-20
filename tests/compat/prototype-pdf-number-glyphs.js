// WITHDRAWN diagnostic injection; no evidence of a product defect. The original
// and prototype PDF page rasters were byte-identical, including all numbers.
// Never treat this option as a production fix or link it into the application.
// Make the badge independent of glyph definitions in a different SVG subtree.
// Keep original nodes untouched; all copies are removed with this badge.
{
  const definitions=document.createElementNS(svg,'defs'),copies=new Map();
  for(const use of clone.querySelectorAll('use')) {
    const href=use.getAttributeNS(null,'href')||use.getAttributeNS(xlink,'href');
    if(!href||href[0]!=='#')continue;
    const glyph=document.getElementById(href.slice(1));
    if(!glyph||glyph.namespaceURI!==svg||glyph.localName!=='path')continue;
    let id=copies.get(glyph);
    if(!id) {
      id='wiz-pdf-badge-glyph-'+targets.length+'-'+copies.size;
      while(document.getElementById(id))id+='-';
      const copy=glyph.cloneNode(true);copy.id=id;
      definitions.appendChild(copy);copies.set(glyph,id);
    }
    use.setAttributeNS(xlink,'xlink:href','#'+id);
    use.setAttributeNS(null,'href','#'+id);
  }
  badge.appendChild(definitions);
}
