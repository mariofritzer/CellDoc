# CellDoc – Renders auf Hintergrund setzen, Anschlussplan beschriften
# Aufruf: python3 tools/render/compose.py <render-ordner> <ziel-ordner>
import json, math, sys
from PIL import Image, ImageDraw, ImageFont

SRC, DST = sys.argv[1], sys.argv[2]
FONT = "/usr/share/fonts/opentype/inter/Inter-SemiBold.otf"
FONT_R = "/usr/share/fonts/opentype/inter/Inter-Regular.otf"
THEMES = {
    "light": dict(bg="#eef1f4", card="#ffffff", fg="#14181d", muted="#6b7480", line="#9aa3ae"),
    "dark": dict(bg="#0b0d10", card="#1d2128", fg="#eef1f5", muted="#8d96a3", line="#5d6672"),
}
WIRES = [
    ("12 V", "#e03131"), ("5 V", "#ff8787"), ("Masse", "#25262b"), ("Akku / Last", "#f08c00"),
    ("Ladestrom", "#2f9e44"), ("I²C SDA", "#1c7ed6"), ("I²C SCL", "#f2c200"),
    ("1-Wire (DS18B20)", "#12b886"), ("Steuersignale", "#9c36b5"), ("Balancer", "#adb5bd"),
]


def on_bg(render, theme, pad=40, thr=8):
    t = THEMES[theme]
    im = Image.open(render).convert("RGBA")
    bbox = im.getchannel("A").point(lambda a: 255 if a > thr else 0).getbbox()
    im = im.crop((max(0, bbox[0] - pad), max(0, bbox[1] - pad), min(im.width, bbox[2] + pad), min(im.height, bbox[3] + pad)))
    bg = Image.new("RGBA", im.size, t["bg"])
    bg.alpha_composite(im)
    return bg, (max(0, bbox[0] - pad), max(0, bbox[1] - pad))


def iso(theme):
    img, _ = on_bg(f"{SRC}/iso.png", theme)
    img.convert("RGB").save(f"{DST}/aufbau-{theme}.jpg", quality=86, optimize=True, progressive=True)


def plan(theme):
    t = THEMES[theme]
    render, off = on_bg(f"{SRC}/top.png", theme, pad=0, thr=250)
    anchors = json.load(open(f"{SRC}/top_anchors.json"))
    margin_l, margin_r, margin_t, legend_h = 260, 300, 70, 150
    W = render.width + margin_l + margin_r
    H = render.height + margin_t + legend_h
    img = Image.new("RGBA", (W, H), t["bg"])
    img.alpha_composite(render, (margin_l, margin_t))
    d = ImageDraw.Draw(img)
    f = ImageFont.truetype(FONT, 22)
    ft = ImageFont.truetype(FONT, 34)
    fs = ImageFont.truetype(FONT_R, 22)
    d.text((40, 22), "CellDoc · Anschlussplan", font=ft, fill=t["fg"])

    # Beschriftungen links bzw. rechts der Platine, sortiert nach Höhe
    pts = []
    for name, (x, y) in anchors.items():
        px, py = x - off[0] + margin_l, y - off[1] + margin_t
        pts.append((name, px, py))
    cx = margin_l + render.width / 2
    left = sorted([p for p in pts if p[1] < cx], key=lambda p: p[2])
    right = sorted([p for p in pts if p[1] >= cx], key=lambda p: p[2])

    def place(group, side):
        min_gap = 44
        ys = []
        for _, _, py in group:
            y = max(py, (ys[-1] + min_gap) if ys else margin_t + 20)
            ys.append(y)
        # nach oben ausgleichen, falls unten zu eng
        limit = margin_t + render.height - 10
        if ys and ys[-1] > limit:
            over = ys[-1] - limit
            ys = [y - over for y in ys]
        for (name, px, py), ly in zip(group, ys):
            tw = d.textlength(name, font=f)
            if side == "l":
                tx = 24
                lx = tx + tw + 30
            else:
                tx = W - 24 - tw - 16
                lx = tx - 12
            box = (tx - 12, ly - 18, tx + tw + 12, ly + 18)
            d.line([(px, py), (lx, ly)], fill=t["line"], width=3)
            d.ellipse((px - 7, py - 7, px + 7, py + 7), fill=t["card"], outline=t["fg"], width=3)
            d.rounded_rectangle(box, radius=18, fill=t["card"], outline=t["line"], width=2)
            d.text((tx, ly), name, font=f, fill=t["fg"], anchor="lm")

    place(left, "l")
    place(right, "r")

    # Legende der Kabelfarben
    y0 = margin_t + render.height + 40
    d.text((40, y0 - 6), "Kabelfarben", font=f, fill=t["muted"])
    x, y = 220, y0
    for name, col in WIRES:
        w = d.textlength(name, font=fs)
        if x + 60 + w > W - 40:
            x, y = 220, y + 44
        d.rounded_rectangle((x, y + 2, x + 42, y + 12), radius=5, fill=col, outline=t["line"] if name == "Masse" else None)
        d.text((x + 54, y + 7), name, font=fs, fill=t["fg"], anchor="lm")
        x += 54 + w + 40
    img.convert("RGB").save(f"{DST}/anschlussplan-{theme}.jpg", quality=88, optimize=True, progressive=True)


for th in THEMES:
    iso(th)
    plan(th)
print("fertig")
