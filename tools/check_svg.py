# Findet Beschriftungen im Schaltplan, die von Linien oder Kästen geschnitten werden
# Aufruf: python3 tools/check_svg.py docs/img/schaltplan.svg
import json, pathlib, sys
from playwright.sync_api import sync_playwright

JS = r"""
() => {
  const svg = document.querySelector('svg');
  const segs = [];
  const add = (x1,y1,x2,y2,kind) => segs.push({x1,y1,x2,y2,kind});
  for (const l of svg.querySelectorAll('line')) add(+l.getAttribute('x1'),+l.getAttribute('y1'),+l.getAttribute('x2'),+l.getAttribute('y2'),'line');
  for (const p of svg.querySelectorAll('polyline')) {
    const pts = p.getAttribute('points').trim().split(/\s+/).map(s=>s.split(',').map(Number));
    for (let i=1;i<pts.length;i++) add(pts[i-1][0],pts[i-1][1],pts[i][0],pts[i][1],'wire');
  }
  for (const r of svg.querySelectorAll('rect')) {
    const x=+r.getAttribute('x')||0, y=+r.getAttribute('y')||0, w=+r.getAttribute('width'), h=+r.getAttribute('height');
    if (w >= 1500) continue;  // Hintergrund und Rahmen
    add(x,y,x+w,y,'rect'); add(x+w,y,x+w,y+h,'rect'); add(x,y+h,x+w,y+h,'rect'); add(x,y,x,y+h,'rect');
  }
  const hits = [];
  const inter = (s, b) => {
    // Liang-Barsky: schneidet das Segment das Rechteck?
    let t0=0,t1=1; const dx=s.x2-s.x1, dy=s.y2-s.y1;
    const p=[-dx,dx,-dy,dy], q=[s.x1-b.x0, b.x1-s.x1, s.y1-b.y0, b.y1-s.y1];
    for (let i=0;i<4;i++){ if (p[i]===0){ if(q[i]<0) return false; } else { const r=q[i]/p[i]; if(p[i]<0){ if(r>t1) return false; if(r>t0) t0=r;} else { if(r<t0) return false; if(r<t1) t1=r; } } }
    return true;
  };
  for (const t of svg.querySelectorAll('text')) {
    const s = t.textContent.trim(); if (!s) continue;
    const bb = t.getBBox();
    // nur der Schriftkörper, Unterlängen und Zeilenabstand abziehen
    const b = {x0: bb.x+1, x1: bb.x+bb.width-1, y0: bb.y+bb.height*0.22, y1: bb.y+bb.height*0.82};
    if (t.getAttribute('transform')) continue;
    for (const g of segs) if (inter(g,b)) { hits.push({text:s, x:Math.round(bb.x), y:Math.round(bb.y+bb.height), kind:g.kind, seg:[g.x1,g.y1,g.x2,g.y2].map(Math.round)}); break; }
  }
  // Text auf Text
  const boxes = [];
  for (const t of svg.querySelectorAll('text')) {
    const s = t.textContent.trim(); if (!s || t.getAttribute('transform')) continue;
    const bb = t.getBBox();
    boxes.push({s, x0: bb.x+1, x1: bb.x+bb.width-1, y0: bb.y+bb.height*0.22, y1: bb.y+bb.height*0.82});
  }
  for (let i=0;i<boxes.length;i++) for (let j=i+1;j<boxes.length;j++) {
    const a=boxes[i], c=boxes[j];
    if (a.x0<c.x1 && c.x0<a.x1 && a.y0<c.y1 && c.y0<a.y1) hits.push({text:a.s+' / '+c.s, x:Math.round(a.x0), y:Math.round(a.y1), kind:'text', seg:[]});
  }
  return hits;
}
"""
p = pathlib.Path(sys.argv[1]).resolve()
with sync_playwright() as pw:
    b = pw.chromium.launch(); pg = b.new_page(viewport={"width": 2240, "height": 1584})
    pg.goto(p.as_uri()); pg.wait_for_timeout(200)
    hits = pg.evaluate(JS); b.close()
for h in hits:
    print(f'{h["text"]!r:40} bei ({h["x"]},{h["y"]})  {h["kind"]} {h["seg"]}')
print(len(hits), "Überschneidungen")
