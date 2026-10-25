# Rendert eine SVG-Datei als PNG (zur Kontrolle): python3 tools/svg2png.py in.svg out.png [x y w h]
import pathlib, sys
from playwright.sync_api import sync_playwright
src, dst = pathlib.Path(sys.argv[1]).resolve(), sys.argv[2]
clip = dict(zip("x y width height".split(), map(int, sys.argv[3:7]))) if len(sys.argv) > 6 else None
with sync_playwright() as pw:
    b = pw.chromium.launch(); pg = b.new_page(viewport={"width": 2240, "height": 1584})
    pg.goto(src.as_uri()); pg.wait_for_timeout(200)
    pg.screenshot(path=dst, clip=clip) if clip else pg.screenshot(path=dst)
    b.close()
