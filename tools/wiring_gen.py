# Erzeugt docs/img/schaltplan.svg als Schaltplan im Stil eines EDA-Programms (KiCad-ähnlich)
# Gleicher Blattstil wie bei Liquid Harvest.
# Aufruf: python3 tools/wiring_gen.py docs/img/schaltplan.svg
import math
import sys

W, H = 2240, 1584
BG, WIRE, COMP, FILL = "#FAF8F2", "#008400", "#840000", "#FFFFC2"
PINNAME, PINNUM, REF, NC = "#006464", "#A90000", "#006464", "#0000C8"
POWER = "#B85C00"  # dicke Leitungen mit hohem Strom
FONT = "'DejaVu Sans Mono','Liberation Mono',Consolas,'Courier New',monospace"
o = []


# ---------------------------------------------------------------- Grundelemente
def t(x, y, s, size=12, color="#000", anchor="start", weight="normal", rot=0, italic=False):
    tr = f' transform="rotate({rot} {x} {y})"' if rot else ""
    st = ' font-style="italic"' if italic else ""
    s = s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    o.append(f'<text x="{x}" y="{y}" font-size="{size}" fill="{color}" text-anchor="{anchor}" font-weight="{weight}"{st}{tr}>{s}</text>')


def wire(*pts, color=WIRE, w=1.6):
    o.append(f'<polyline points="{" ".join(f"{x},{y}" for x, y in pts)}" fill="none" stroke="{color}" stroke-width="{w}" stroke-linejoin="round"/>')


def pwire(*pts):
    """Leitung mit hohem Strom (Akku/Last) – dicker"""
    wire(*pts, color=WIRE, w=3.4)


def line(x1, y1, x2, y2, color=COMP, w=1.4, extra=""):
    o.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{color}" stroke-width="{w}" {extra}/>')


def dot(x, y, r=3.4):
    o.append(f'<circle cx="{x}" cy="{y}" r="{r}" fill="{WIRE}"/>')


def nc(x, y):
    line(x - 5, y - 5, x + 5, y + 5, NC, 1.5)
    line(x - 5, y + 5, x + 5, y - 5, NC, 1.5)


def box(x, y, w, h, dash=False):
    d = ' stroke-dasharray="5 3"' if dash else ""
    o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" fill="{FILL}" stroke="{COMP}" stroke-width="1.6"{d}/>')


def pin(x, y, d, name, num=None, L=20, namecol=PINNAME):
    """Pin am Gehäuse (x,y), zeigt in Richtung d (l/r/u/d). Liefert den Anschlusspunkt."""
    dx, dy = {"l": (-L, 0), "r": (L, 0), "u": (0, -L), "d": (0, L)}[d]
    line(x, y, x + dx, y + dy, COMP, 1.4)
    if name:
        if d == "l": t(x + 5, y + 4, name, 11, namecol)
        elif d == "r": t(x - 5, y + 4, name, 11, namecol, "end")
        elif d == "u": t(x, y + 14, name, 11, namecol, "middle")
        else: t(x, y - 6, name, 11, namecol, "middle")
    if num is not None:
        if d in "lr": t(x + dx / 2, y - 4, str(num), 9, PINNUM, "middle")
        else: t(x + 4, y + dy / 2, str(num), 9, PINNUM)
    return x + dx, y + dy


def label(x, y, name, side):
    """Netzlabel am Leitungsende; side r = Text rechts vom Punkt, l = links"""
    if side == "r": t(x + 3, y - 4, name, 12, "#000")
    else: t(x - 3, y - 4, name, 12, "#000", "end")


def pwr(x, y, name, d="u"):
    if d == "u":
        line(x, y, x, y - 14); line(x - 8, y - 14, x + 8, y - 14, COMP, 1.8); t(x, y - 19, name, 11, COMP, "middle")
    elif d == "d":
        line(x, y, x, y + 14); line(x - 8, y + 14, x + 8, y + 14, COMP, 1.8); t(x, y + 28, name, 11, COMP, "middle")
    elif d == "l":
        line(x, y, x - 14, y); line(x - 14, y - 8, x - 14, y + 8, COMP, 1.8); t(x - 19, y + 4, name, 11, COMP, "end")
    elif d == "r":
        line(x, y, x + 14, y); line(x + 14, y - 8, x + 14, y + 8, COMP, 1.8); t(x + 19, y + 4, name, 11, COMP)


def gnd(x, y, d="d"):
    if d == "d":
        line(x, y, x, y + 10); line(x - 9, y + 10, x + 9, y + 10, COMP, 1.8); line(x - 5.5, y + 14, x + 5.5, y + 14, COMP, 1.6); line(x - 2, y + 18, x + 2, y + 18, COMP, 1.6)
    elif d == "l":
        line(x, y, x - 10, y); line(x - 10, y - 9, x - 10, y + 9, COMP, 1.8); line(x - 14, y - 5.5, x - 14, y + 5.5, COMP, 1.6); line(x - 18, y - 2, x - 18, y + 2, COMP, 1.6)
    elif d == "r":
        line(x, y, x + 10, y); line(x + 10, y - 9, x + 10, y + 9, COMP, 1.8); line(x + 14, y - 5.5, x + 14, y + 5.5, COMP, 1.6); line(x + 18, y - 2, x + 18, y + 2, COMP, 1.6)


def res_h(x1, x2, y, ref, val, w=1.4):
    m = (x1 + x2) / 2
    line(x1, y, m - 15, y, w=w); line(m + 15, y, x2, y, w=w)
    o.append(f'<rect x="{m-15}" y="{y-5}" width="30" height="10" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
    t(m, y - 10, ref, 11, REF, "middle"); t(m, y + 19, val, 11, REF, "middle")


def res_v(x, y1, y2, ref, val, side="r", w=1.4):
    m = (y1 + y2) / 2
    line(x, y1, x, m - 15, w=w); line(x, m + 15, x, y2, w=w)
    o.append(f'<rect x="{x-5}" y="{m-15}" width="10" height="30" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
    if side == "r": t(x + 10, m - 3, ref, 11, REF); t(x + 10, m + 11, val, 11, REF)
    else: t(x - 10, m - 3, ref, 11, REF, "end"); t(x - 10, m + 11, val, 11, REF, "end")


def fuse_h(x1, x2, y, ref, val, below=False):
    m = (x1 + x2) / 2
    if below:
        line(x1, y, m - 18, y, w=3.4); line(m + 18, y, x2, y, w=3.4)
        o.append(f'<rect x="{m-18}" y="{y-6}" width="36" height="12" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
        line(m - 18, y, m + 18, y, COMP, 1.2)
        t(m, y + 21, f"{ref}  {val}", 11, REF, "middle")
        return
    line(x1, y, m - 18, y, w=3.4); line(m + 18, y, x2, y, w=3.4)
    o.append(f'<rect x="{m-18}" y="{y-6}" width="36" height="12" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
    line(m - 18, y, m + 18, y, COMP, 1.2)
    t(m, y - 11, ref, 11, REF, "middle"); t(m, y + 21, val, 11, REF, "middle")


def diode_v(x, y_k, y_a, ref, val, side="r"):
    """Diode senkrecht, Kathode oben (y_k), Anode unten (y_a)"""
    m = (y_k + y_a) / 2
    line(x, y_k, x, m - 9); line(x, m + 9, x, y_a)
    o.append(f'<path d="M{x-10} {m+9} h20 l-10 -18 z" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
    line(x - 10, m - 9, x + 10, m - 9, COMP, 1.8)
    line(x - 10, m - 9, x - 13, m - 5, COMP, 1.5); line(x + 10, m - 9, x + 13, m - 13, COMP, 1.5)  # Schottky-Haken
    if side == "r": t(x + 16, m - 2, ref, 11, REF); t(x + 16, m + 12, val, 11, REF)
    else: t(x - 16, m - 2, ref, 11, REF, "end"); t(x - 16, m + 12, val, 11, REF, "end")


def cell_v(x, y_plus, y_minus, ref, lines):
    """Akkuzelle senkrecht, Plus oben"""
    m = (y_plus + y_minus) / 2
    line(x, y_plus, x, m - 6, w=3.4); line(x, m + 6, x, y_minus, w=3.4)
    line(x - 18, m - 6, x + 18, m - 6, COMP, 1.6)
    line(x - 9, m + 6, x + 9, m + 6, COMP, 4)
    t(x - 26, m - 4, "+", 13, COMP, "middle", "bold")
    t(x + 26, m - 12, ref, 12, REF)
    for k, s in enumerate(lines):
        t(x + 26, m + 3 + k * 14, s, 11, REF)


def nmos(gx, gy, ref, val, side="r", pch=False):
    """MOSFET: Gate links bei (gx,gy). Liefert (gate, drain, source). Bei pch ist oben die Source."""
    bx = gx + 22
    line(gx, gy, bx, gy)
    line(bx, gy - 14, bx, gy + 14, COMP, 1.8)  # Gate-Platte
    for yy in (gy - 12, gy, gy + 12):
        line(bx + 6, yy - 5, bx + 6, yy + 5, COMP, 2)
    ox = bx + 24
    line(bx + 6, gy - 12, ox, gy - 12); line(ox, gy - 12, ox, gy - 30)
    line(bx + 6, gy + 12, ox, gy + 12); line(ox, gy + 12, ox, gy + 30)
    line(bx + 6, gy, ox, gy); line(ox, gy, ox, gy + 12)
    if pch:
        o.append(f'<path d="M{bx+18} {gy} l-9 -4 l0 8 z" fill="{COMP}" transform="rotate(180 {bx+13} {gy})"/>')
    else:
        o.append(f'<path d="M{bx+8} {gy} l9 -4 l0 8 z" fill="{COMP}"/>')
    o.append(f'<circle cx="{bx+12}" cy="{gy}" r="20" fill="none" stroke="{COMP}" stroke-width="1.2"/>')
    if side == "r":
        t(ox + 8, gy - 4, ref, 11, REF); t(ox + 8, gy + 10, val, 11, REF)
    else:
        t(gx - 4, gy - 20, ref, 11, REF, "end"); t(gx - 4, gy - 34, val, 11, REF, "end")
    return (gx, gy), (ox, gy - 30), (ox, gy + 30)


def npn(cx, cy, ref, val):
    """Basis links, Kollektor oben, Emitter unten"""
    o.append(f'<circle cx="{cx}" cy="{cy}" r="20" fill="none" stroke="{COMP}" stroke-width="1.5"/>')
    line(cx - 10, cy - 14, cx - 10, cy + 14, COMP, 2.2); line(cx - 22, cy, cx - 10, cy)
    line(cx - 10, cy - 7, cx + 8, cy - 20); line(cx + 8, cy - 20, cx + 8, cy - 36)
    line(cx - 10, cy + 7, cx + 8, cy + 20); line(cx + 8, cy + 20, cx + 8, cy + 34)
    o.append(f'<path d="M{cx+2} {cy+19} l6 1.5 l-3 -5.5 z" fill="{COMP}"/>')
    t(cx + 26, cy - 4, ref, 11, REF); t(cx + 26, cy + 10, val, 11, REF)
    return (cx - 22, cy), (cx + 8, cy - 36), (cx + 8, cy + 34)


def opamp(x, y, ref, val, power=False):
    """Dreieck nach rechts; liefert (in+, in-, out) als Anschlusspunkte"""
    o.append(f'<path d="M{x} {y-34} L{x} {y+34} L{x+64} {y} Z" fill="{FILL}" stroke="{COMP}" stroke-width="1.6"/>')
    t(x + 7, y - 11, "+", 13, COMP, weight="bold"); t(x + 7, y + 23, "−", 13, COMP, weight="bold")
    ip = pin(x, y - 15, "l", None, 3 if ref.endswith("A") else 5)
    im = pin(x, y + 15, "l", None, 2 if ref.endswith("A") else 6)
    out = pin(x + 64, y, "r", None, 1 if ref.endswith("A") else 7)
    t(x + 18, y - 40, ref, 11, REF); t(x + 42, y + 50, val, 11, REF)
    if power:
        line(x + 30, y - 19, x + 30, y - 40); pwr(x + 30, y - 40, "+12V")
        line(x + 30, y + 19, x + 30, y + 40); gnd(x + 30, y + 40)
        t(x + 34, y - 26, "8", 9, PINNUM); t(x + 34, y + 34, "4", 9, PINNUM)
    return ip, im, out


def switch_no(x1, x2, y, ref, note=""):
    """Schließer waagerecht zwischen x1 und x2"""
    m1, m2 = x1 + 14, x2 - 14
    line(x1, y, m1, y, w=3.4); line(m2, y, x2, y, w=3.4)
    o.append(f'<circle cx="{m1}" cy="{y}" r="3" fill="{FILL}" stroke="{COMP}" stroke-width="1.4"/><circle cx="{m2}" cy="{y}" r="3" fill="{FILL}" stroke="{COMP}" stroke-width="1.4"/>')
    line(m1 + 2, y - 1, m2 - 2, y - 16, COMP, 1.8)
    t((m1 + m2) / 2, y - 22, ref, 11, REF, "middle")
    if note: t((m1 + m2) / 2, y + 18, note, 10, REF, "middle")


def changeover(cx, cy, ref):
    """Wechsler: COM links (cx,cy), NO oben rechts, NC unten rechts. Liefert (com, no, nc)."""
    o.append(f'<circle cx="{cx}" cy="{cy}" r="3" fill="{FILL}" stroke="{COMP}" stroke-width="1.4"/>')
    no, ncp = (cx + 60, cy - 26), (cx + 60, cy + 26)
    for (px, py) in (no, ncp):
        o.append(f'<circle cx="{px}" cy="{py}" r="3" fill="{FILL}" stroke="{COMP}" stroke-width="1.4"/>')
    line(cx + 2, cy + 1, ncp[0] - 4, ncp[1] - 3, COMP, 1.8)
    t(cx + 26, cy - 34, ref, 11, REF, "middle")
    t(no[0] + 6, no[1] - 6, "NO", 9, PINNAME); t(ncp[0] + 6, ncp[1] + 14, "NC", 9, PINNAME)
    return (cx, cy), no, ncp


def coil(x, y, ref):
    o.append(f'<rect x="{x-11}" y="{y-16}" width="22" height="32" fill="{FILL}" stroke="{COMP}" stroke-width="1.5"/>')
    line(x - 11, y + 16, x + 11, y - 16, COMP, 1.2)
    t(x, y - 22, ref, 11, REF, "middle")


def cap_v(x, y1, y2, ref, val, side="r"):
    m = (y1 + y2) / 2
    line(x, y1, x, m - 4); line(x, m + 4, x, y2)
    line(x - 10, m - 4, x + 10, m - 4, COMP, 2); line(x - 10, m + 4, x + 10, m + 4, COMP, 2)
    if side == "r": t(x + 14, m - 2, ref, 11, REF); t(x + 14, m + 12, val, 11, REF)
    else: t(x - 14, m - 2, ref, 11, REF, "end"); t(x - 14, m + 12, val, 11, REF, "end")


def section(x1, y1, x2, y2, title):
    o.append(f'<rect x="{x1}" y="{y1}" width="{x2-x1}" height="{y2-y1}" fill="none" stroke="{COMP}" stroke-width="1" stroke-dasharray="7 4" opacity=".55"/>')
    t(x1 + 10, y1 + 18, title, 13, COMP, weight="bold")


def note(x, y, s):
    t(x, y, s, 11, "#555", italic=True)


# ---------------------------------------------------------------- Blatt
o.append(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
o.append(f'<rect x="20" y="20" width="{W-40}" height="{H-40}" fill="none" stroke="{COMP}" stroke-width="1.6"/>')
o.append(f'<rect x="36" y="36" width="{W-72}" height="{H-72}" fill="none" stroke="{COMP}" stroke-width="1"/>')
for i in range(8):
    x = 36 + (W - 72) * (i + .5) / 8
    t(x, 32, str(i + 1), 11, COMP, "middle"); t(x, H - 23, str(i + 1), 11, COMP, "middle")
    if i:
        xx = 36 + (W - 72) * i / 8
        line(xx, 20, xx, 36, COMP, 1); line(xx, H - 36, xx, H - 20, COMP, 1)
for i, c in enumerate("ABCDEF"):
    y = 36 + (H - 72) * (i + .5) / 6
    t(28, y + 4, c, 11, COMP, "middle"); t(W - 28, y + 4, c, 11, COMP, "middle")
    if i:
        yy = 36 + (H - 72) * i / 6
        line(20, yy, 36, yy, COMP, 1); line(W - 36, yy, W - 20, yy, COMP, 1)

# ================================================================ Linke Spalte
# ---------------------------------------------------------------- Stromversorgung
section(50, 54, 720, 300, "Stromversorgung")
box(90, 112, 70, 92)
o.append(f'<circle cx="112" cy="158" r="12" fill="#e9e6da" stroke="{COMP}" stroke-width="1.4"/><circle cx="112" cy="158" r="4" fill="{COMP}"/>')
t(90, 104, "J1", 12, REF); t(125, 222, "DC-Buchse 12 V", 11, REF, "middle")
a = pin(160, 135, "r", "+", 1); b = pin(160, 180, "r", "−", 2)
t(66, 146, "12 V", 11, "#333", "end") if False else None
fuse_h(a[0], 270, 135, "F0", "3 A")
wire((270, 135), (440, 135)); dot(330, 135); wire((330, 135), (330, 100)); pwr(330, 100, "+12V")
wire(b, (440, 180)); dot(300, 180); gnd(300, 180)
box(460, 110, 130, 100)
t(460, 102, "U2", 12, REF); t(525, 228, "LM2596 Step-Down", 11, REF, "middle")
t(525, 166, "12V→5V", 12, PINNAME, "middle", "bold")
pin(460, 135, "l", "IN+", 1); pin(460, 180, "l", "IN−", 2)
c = pin(590, 135, "r", "OUT+", 3); d = pin(590, 180, "r", "OUT−", 4)
wire(c, (700, 135)); dot(660, 135); wire((660, 135), (660, 100)); pwr(660, 100, "+5V")
wire(d, (700, 180)); dot(680, 180); gnd(680, 180)
note(66, 262, "+12V: Lüfter, LM358. +5V: ESP32 (VIN), TP5000, Relaismodul, Summer.")
note(66, 280, "+3V3 vom Regler auf dem ESP32-Board: INA226, ADS1115, MCP4725, DS18B20, Pull-ups.")

# ---------------------------------------------------------------- Anschluss B
section(50, 316, 720, 960, "Anschluss B – Packs 2S–6S, Balancer")
box(80, 362, 70, 76)
t(80, 354, "J3", 12, REF); t(115, 454, "XT60", 11, REF, "middle", "bold")
a = pin(150, 380, "r", "+", 1); b = pin(150, 420, "r", "−", 2)
fuse_h(a[0], 250, 380, "F2", "10 A", below=True)
pwire((250, 380), (360, 380)); dot(285, 380); diode_v(285, 380, 440, "D2", "15SQ045", "l"); gnd(285, 440)
pwire(b, (200, 420), (200, 470)); gnd(200, 470)
dot(180, 380); wire((180, 380), (180, 356), (250, 356)); label(250, 356, "SENSE_B", "l")
# U5 INA226 B
box(380, 360, 150, 190)
t(380, 352, "U5", 12, REF); t(530, 352, "INA226-Modul", 11, REF, "end")
t(475, 455, "INA226", 12, PINNAME, "middle", "bold", rot=-90)
t(500, 455, "0x41 · Shunt 10 mΩ", 10, PINNAME, "middle", rot=-90)
pin(380, 380, "l", "IN+", 1, L=20)
p = pin(380, 410, "l", "VBUS", 2); wire(p, (320, 410)); label(320, 410, "SENSE_B", "r")
p = pin(380, 440, "l", "VCC", 3); wire(p, (345, 440)); pwr(345, 440, "+3V3", "l")
p = pin(380, 470, "l", "GND", 4); wire(p, (345, 470)); gnd(345, 470, "l")
p = pin(380, 500, "l", "SDA", 5); wire(p, (320, 500)); label(320, 500, "SDA", "r")
p = pin(380, 530, "l", "SCL", 6); wire(p, (320, 530)); label(320, 530, "SCL", "r")
p = pin(530, 380, "r", "IN−", 7); pwire(p, (700, 380)); label(700, 380, "NODE_B", "l")
note(392, 568, "A0 → VCC (Adresse 0x41)")
# Balancer
box(80, 600, 70, 236)
t(80, 592, "J4", 12, REF); t(115, 852, "JST-XH 2S–6S", 11, REF, "middle", "bold")
for k in range(7):
    y = 620 + k * 32
    p = pin(150, y, "r", f"B{k}", k + 1)
    if k == 0:
        wire(p, (190, y), (190, y + 14)); gnd(190, y + 14)
    else:
        wire(p, (210, y))
box(210, 640, 90, 182)
t(210, 632, "RN1", 12, REF)
for k in range(6):
    y = 652 + k * 32
    line(210, y, 300, y, COMP, 0.6, 'stroke-dasharray="2 3"')
p = pin(255, 822, "d", "GND", None, L=10); gnd(255, 832)
t(320, 870, "6 × 100 kΩ / 10 kΩ", 10, REF, "middle"); t(320, 884, "0,1 %, Teiler 1 : 11", 10, REF, "middle")
taps = []
for k in range(6):
    y = 652 + k * 32
    taps.append((300, y))
# ADS1115 ×2
for idx, (ux, uy, ref, addr, chans) in enumerate([(430, 606, "U7", "0x48", 4), (430, 790, "U8", "0x49", 2)]):
    h = 150 if chans == 4 else 140
    box(ux, uy, 130, h)
    t(ux, uy - 8, ref, 12, REF); t(ux + 130, uy - 8, f"ADS1115 · {addr}", 11, REF, "end")
    for j in range(4):
        y = uy + 20 + j * 30 if chans == 4 else uy + 20 + j * 30
        p = pin(ux, y, "l", f"A{j}", j + 4)
        k = j + (0 if idx == 0 else 4)
        if j < chans:
            tx, ty = taps[k]
            wire(p, (ux - 40 - k * 4, y), (ux - 40 - k * 4, ty), (tx, ty)) if ty != y else wire(p, (tx, ty))
        else:
            nc(*p)
    yy = [uy + 20, uy + 48, uy + 76, uy + 104, uy + 132] if chans == 4 else [uy + 18, uy + 44, uy + 70, uy + 96, uy + 122]
    p = pin(ux + 130, yy[0], "r", "VDD", 8); wire(p, (ux + 170, yy[0])); pwr(ux + 170, yy[0], "+3V3", "r")
    p = pin(ux + 130, yy[1], "r", "GND", 1); wire(p, (ux + 170, yy[1])); gnd(ux + 170, yy[1], "r")
    p = pin(ux + 130, yy[2], "r", "SCL", 10); wire(p, (ux + 200, yy[2])); label(ux + 200, yy[2], "SCL", "l")
    p = pin(ux + 130, yy[3], "r", "SDA", 9); wire(p, (ux + 200, yy[3])); label(ux + 200, yy[3], "SDA", "l")
    p = pin(ux + 130, yy[4], "r", "ADDR", 2)
    if idx == 0:
        wire(p, (ux + 170, yy[4])); gnd(ux + 170, yy[4], "r")
    else:
        wire(p, (ux + 170, yy[4])); pwr(ux + 170, yy[4], "+3V3", "r")
note(66, 946, "B0 = Minus des Packs = GND. Teiler-Ausgang k = Summe der Zellen 1…k ÷ 11.")

# ---------------------------------------------------------------- Temperatur, Lüfter, Summer
section(50, 976, 720, 1548, "Temperatur, Lüfter, Summer")
# 1-Wire mit drei DS18B20
wire((130, 1040), (400, 1040)); pwr(130, 1040, "+3V3", "l")
wire((130, 1100), (400, 1100)); label(130, 1100, "ONEWIRE", "r")
res_v(400, 1040, 1100, "R27", "4,7 kΩ")
dot(400, 1040) if False else None
for k, (ref, where) in enumerate([("U12", "T1 Zelle"), ("U13", "T2 Pack"), ("U14", "T3 Kühlkörper")]):
    bx = 150 + k * 84
    box(bx, 1130, 64, 64)
    t(bx, 1122, ref, 11, REF)
    t(bx + 32, 1166, "DS18B20", 9, PINNAME, "middle", "bold")
    t(bx + 32, 1212, where, 10, REF, "middle")
    p = pin(bx + 14, 1130, "u", None, 3, L=0)
    wire((bx + 14, 1130), (bx + 14, 1040)); dot(bx + 14, 1040)
    wire((bx + 34, 1130), (bx + 34, 1100)); dot(bx + 34, 1100)
    t(bx + 9, 1146, "V", 9, PINNAME); t(bx + 30, 1146, "Q", 9, PINNAME)
    line(bx + 52, 1194, bx + 52, 1230); gnd(bx + 52, 1230)
    t(bx + 56, 1188, "G", 9, PINNAME)
note(66, 1272, "T1 mit Kaptonband an die Zelle, T2 lose aufs Pack, T3 zwischen die MOSFETs.")

# Lüfter
box(500, 1036, 120, 110)
o.append(f'<circle cx="560" cy="1100" r="22" fill="none" stroke="{COMP}" stroke-width="1.4"/>')
t(560, 1106, "M", 16, COMP, "middle", "bold")
t(500, 1028, "M1", 12, REF); t(626, 1116, "Lüfter", 11, REF); t(626, 1130, "12 V, 4-Pin", 11, REF)
pin(560, 1036, "u", None, 2, L=18); pwr(560, 1018, "+12V")
p = pin(560, 1146, "d", "GND", 1, L=18); gnd(*p)
p = pin(620, 1066, "r", "PWM", 4); wire(p, (700, 1066), (700, 1290))
p = pin(500, 1100, "l", "TACH", 3); wire(p, (440, 1100)); label(440, 1100, "FAN_TACH", "r")
t(442, 1120, "R29 10 kΩ", 10, REF); t(442, 1133, "nach +3V3", 10, REF)
# Q8 schaltet den PWM-Eingang des Lüfters (offener Drain)
g, dr, sr = nmos(600, 1320, "Q8", "AO3400")
wire(dr, (646, 1290), (700, 1290))
gnd(sr[0], sr[1])
wire((460, 1320), g); label(460, 1320, "FAN_PWM", "r")
res_v(530, 1320, 1380, "R28", "10 kΩ", "l"); dot(530, 1320); gnd(530, 1380)

# Summer
box(150, 1340, 60, 44)
o.append(f'<path d="M168 1352 h12 l10 -8 v36 l-10 -8 h-12 z" fill="none" stroke="{COMP}" stroke-width="1.3"/>')
t(150, 1332, "BZ1", 12, REF); t(180, 1402, "aktiv, 5 V", 10, REF, "middle")
p = pin(150, 1362, "l", None, 1, L=20); pwr(p[0], p[1], "+5V", "l")
p = pin(210, 1362, "r", None, 2, L=40)
g, dr, sr = nmos(240, 1450, "Q9", "AO3400")
wire(p, (dr[0], 1362), dr); gnd(sr[0], sr[1])
wire((110, 1450), g); label(110, 1450, "BUZZER", "r")
res_v(190, 1450, 1510, "R30", "10 kΩ", "l"); dot(190, 1450); gnd(190, 1510)

# Lüfter-Hinweise unten rechts im Abschnitt
note(400, 1430, "Q8 zieht den PWM-Eingang des Lüfters")
note(400, 1446, "nach GND (Signal invertiert, 25 kHz).")
note(400, 1470, "Ohne Drehzahl bei warmem Kühlkörper")
note(400, 1486, "schaltet die Firmware die Last ab.")

# ================================================================ Mitte: ESP32
EX, EY, EW, SP = 860, 96, 200, 36
EH = 14 * SP + 60
box(EX, EY, EW, EH)
o.append(f'<rect x="{EX+60}" y="{EY+8}" width="80" height="26" fill="none" stroke="{COMP}" stroke-width="1" stroke-dasharray="3 3"/>')
t(EX + 100, EY + 25, "Antenne", 10, PINNAME, "middle")
o.append(f'<rect x="{EX+78}" y="{EY+EH-18}" width="44" height="18" rx="4" fill="none" stroke="{COMP}" stroke-width="1.2"/>')
t(EX + 100, EY + EH - 5, "USB", 10, PINNAME, "middle")
t(EX, EY - 10, "U1", 12, REF); t(EX + EW, EY - 10, "ESP32 DevKit V1 (30 Pin)", 11, REF, "end")
t(EX + 100, EY + EH / 2, "ESP32-WROOM-32", 12, PINNAME, "middle", "bold", rot=-90)
left = [("EN", None), ("IO36/VP", None), ("IO39/VN", "FAN_TACH"), ("IO34", "CHG_CHRG"), ("IO35", "CHG_STDBY"), ("IO32", "CHG_42"), ("IO33", "CHG_STOP"),
        ("IO25", "FAN_PWM"), ("IO26", "K1_IN"), ("IO27", "K2_IN"), ("IO14", None), ("IO12", None), ("IO13", None), ("GND", "GND"), ("VIN", "+5V")]
right = [("IO23", None), ("IO22", "SCL"), ("TX0", None), ("RX0", None), ("IO21", "SDA"), ("IO19", None), ("IO18", None), ("IO5", None),
         ("TX2/IO17", "BUZZER"), ("RX2/IO16", "LOAD_EN_N"), ("IO4", "ONEWIRE"), ("IO2", None), ("IO15", None), ("GND", "GND"), ("3V3", "+3V3")]
for i, (n, net) in enumerate(left):
    y = EY + 40 + i * SP
    px, py = pin(EX, y, "l", n, i + 1, L=30)
    if net is None: nc(px, py)
    elif net == "GND": wire((px, py), (px - 20, py)); gnd(px - 20, py, "l")
    elif net == "+5V": wire((px, py), (px - 20, py)); pwr(px - 20, py, "+5V", "l")
    else: wire((px, py), (px - 80, py)); label(px - 80, py, net, "r")
for i, (n, net) in enumerate(right):
    y = EY + 40 + i * SP
    px, py = pin(EX + EW, y, "r", n, 30 - i, L=30)
    if net is None: nc(px, py)
    elif net == "GND": wire((px, py), (px + 20, py)); gnd(px + 20, py, "r")
    elif net == "+3V3": wire((px, py), (px + 20, py)); pwr(px + 20, py, "+3V3", "r")
    else: wire((px, py), (px + 80, py)); label(px + 80, py, net, "l")
# I²C-Pull-ups
for k, (net, y) in enumerate([("SCL", EY + 40 + SP), ("SDA", EY + 40 + 4 * SP)]):
    pass
note(EX - 110, EY + EH + 30, "I²C: SDA = IO21, SCL = IO22, je 4,7 kΩ (R31, R32) nach +3V3.")
note(EX - 110, EY + EH + 46, "BOOT-Taste (IO0) auf dem Board = Not-Aus im Betrieb.")

# ---------------------------------------------------------------- Stückliste
section(740, 730, 1180, 1290, "Stückliste")
rows = [
    ("U1", "ESP32 DevKit V1 (WROOM-32, 30 Pin)", "1"),
    ("U2", "Step-Down LM2596, auf 5,0 V einstellen", "1"),
    ("U4, U5", "INA226-Modul, Shunt auf 10 mΩ ändern", "2"),
    ("U6", "MCP4725-Modul (12-Bit-DAC)", "1"),
    ("U7, U8", "ADS1115-Modul", "2"),
    ("U9", "TP5000-Modul (4,2 V / 3,6 V)", "1"),
    ("U10", "LM358 (DIP-8) + Sockel", "1"),
    ("U11", "Relaismodul 2-fach 5 V, 10 A", "1"),
    ("U12–U14", "DS18B20 (wasserdicht)", "3"),
    ("Q2, Q3", "IRFP250N auf CPU-Kühler, isoliert", "2"),
    ("Q1, Q4, Q5", "BSS138", "3"),
    ("Q7–Q9 / Q6", "AO3400 / AO3401", "3/1"),
    ("Q10", "BC547", "1"),
    ("D1, D2", "Schottky 15SQ045", "2"),
    ("F0 / F1 / F2", "Flachsicherung 3 A / 7,5 A / 10 A", "3"),
    ("R16, R17", "0,1 Ω / 5 W", "2"),
    ("RN1", "6 × 100 kΩ + 6 × 10 kΩ, 0,1 %", "12"),
    ("R", "1 kΩ ×6 · 7,5 kΩ ×1 · 10 kΩ ×12 · 4,7 kΩ ×3", ""),
    ("C1, C2", "10 nF (Regler-Kompensation)", "2"),
    ("M1 / BZ1", "CPU-Kühler 4-Pin 12 V / Summer 5 V", "1/1"),
    ("J1–J4", "DC-Buchse · 18650-Halter · XT60 · JST-XH", ""),
]
y = 766
for r in rows:
    t(752, y, r[0], 11, PINNUM); t(870, y, r[1], 11, "#222"); t(1168, y, r[2], 11, "#222", "end")
    line(748, y + 5, 1172, y + 5, COMP, .5, 'opacity=".4"'); y += 24

# ---------------------------------------------------------------- Hinweise
section(740, 1306, 1180, 1548, "Hinweise")
for k, s in enumerate([
    "1. Dicke Linien = Akku- und Laststrom, 1,5 mm².",
    "2. Masse sternförmig am Minuspol der Last (R16/R17).",
    "3. SENSE_A/B als eigene Leitung direkt vom Akkupol",
    "   zum VBUS des INA226 (Kelvin), sonst ist Ri zu hoch.",
    "4. Relaismodul auf High-Level-Trigger stellen.",
    "5. Lader nur an Anschluss A: Packs nie über U9 laden.",
    "6. TP5000-Lötbrücke 3,6 V öffnen, CS steuert U1.",
    "7. Ungenutzte Pins (×) frei; IO0/2/5/12/15 = Boot.",
    "8. LiPo nur auf nicht brennbarer Unterlage testen."]):
    t(752, 1346 + k * 21, s, 11, "#222")

# ================================================================ Rechte Spalte
# ---------------------------------------------------------------- Anschluss A + Lader
section(1200, 54, 2190, 600, "Anschluss A – Einzelzelle und Lader")
# Zelle, F1, D1
cell_v(1260, 110, 230, "BT1", ["Zelle 1S", "18650-Halter,", "Klemme, Lötpads"])
gnd(1260, 230)
pwire((1260, 110), (1290, 110))
fuse_h(1290, 1380, 110, "F1", "7,5 A")
pwire((1380, 110), (1540, 110)); dot(1420, 110); diode_v(1420, 110, 190, "D1", "15SQ045"); gnd(1420, 190)
dot(1260, 110); wire((1260, 110), (1260, 92), (1212, 92)); label(1212, 92, "SENSE_A", "r")
# U4 INA226 A
box(1560, 90, 150, 190)
t(1560, 82, "U4", 12, REF); t(1710, 82, "INA226-Modul", 11, REF, "end")
t(1655, 185, "INA226", 12, PINNAME, "middle", "bold", rot=-90)
t(1680, 185, "0x40 · Shunt 10 mΩ", 10, PINNAME, "middle", rot=-90)
pin(1560, 110, "l", "IN+", 1)
p = pin(1560, 140, "l", "VBUS", 2); wire(p, (1500, 140)); label(1500, 140, "SENSE_A", "r")
p = pin(1560, 170, "l", "VCC", 3); wire(p, (1525, 170)); pwr(1525, 170, "+3V3", "l")
p = pin(1560, 200, "l", "GND", 4); wire(p, (1525, 200)); gnd(1525, 200, "l")
p = pin(1560, 230, "l", "SDA", 5); wire(p, (1490, 230)); label(1490, 230, "SDA", "r")
p = pin(1560, 260, "l", "SCL", 6); wire(p, (1490, 260)); label(1490, 260, "SCL", "r")
p = pin(1710, 110, "r", "IN−", 7); pwire(p, (2170, 110)); label(2170, 110, "NODE_A", "l")
# U9 TP5000
box(1600, 360, 160, 190)
t(1600, 352, "U9", 12, REF); t(1760, 352, "TP5000-Modul", 11, REF, "end")
t(1680, 470, "TP5000", 12, PINNAME, "middle", "bold")
t(1680, 486, "Rs 0,1 Ω → 1 A", 10, PINNAME, "middle")
p = pin(1640, 360, "u", "VIN", 1); pwr(1640, 340, "+5V")
p = pin(1660, 550, "d", "GND", 2, L=12); gnd(*p)
p = pin(1720, 550, "d", "TS", 3, L=12); gnd(*p)
csp = pin(1600, 440, "l", "CS", 4)
p = pin(1760, 380, "r", "BAT+", 5); wire(p, (1800, 380), (1800, 310)); switch_no(1800, 1890, 310, "K1", "Kontakt von U11")
wire((1890, 310), (1960, 310), (1960, 110)); dot(1960, 110)
p = pin(1760, 410, "r", "BAT−", 6); gnd(p[0], p[1], "r")
p = pin(1760, 450, "r", "CHRG", 7); res_h(p[0], 1870, 450, "R7", "1 kΩ"); wire((1870, 450), (2170, 450)); label(2170, 450, "CHG_CHRG", "l")
dot(1930, 450); res_v(1930, 390, 450, "R9", "10 kΩ"); pwr(1930, 390, "+3V3")
p = pin(1760, 500, "r", "STDBY", 8); res_h(p[0], 1870, 500, "R8", "1 kΩ"); wire((1870, 500), (2170, 500)); label(2170, 500, "CHG_STDBY", "l")
dot(2040, 500); res_v(2040, 500, 546, "R10", "10 kΩ"); pwr(2040, 546, "+3V3", "d")
# CS-Ansteuerung: Q6 (P) zieht CS auf +5V = 4,2 V, Q7 (N) zieht CS auf GND = aus, beide aus = 3,6 V
wire(csp, (1536, 440)); dot(1536, 440)
g6, d6, s6 = nmos(1490, 400, "Q6", "AO3401", side="r", pch=True)
# bei P-Kanal liegt die Source oben: Anschluss oben an +5V, unten an CS
pwr(d6[0], d6[1], "+5V")
wire(s6, (1536, 440))
g7, d7, s7 = nmos(1490, 500, "Q7", "AO3400", side="r")
wire(d7, (1536, 440))
gnd(*s7)
wire((1220, 500), g7); label(1220, 500, "CHG_STOP", "r")
dot(1460, 500); res_v(1460, 500, 560, "R12", "10 kΩ", "l"); gnd(1460, 560)
# Q6-Gate: 10 kΩ nach +5V, Q10 zieht es über 1 kΩ an IO32 nach GND
dot(1440, 400); wire((1440, 400), g6)
res_v(1440, 340, 400, "R11", "10 kΩ", "l"); pwr(1440, 340, "+5V")
b10, c10, e10 = npn(1380, 440, "Q10", "BC547")
wire(c10, (1388, 400), (1440, 400))
gnd(*e10)
res_h(1268, b10[0], 440, "R13", "1 kΩ"); wire((1220, 440), (1268, 440)); label(1220, 440, "CHG_42", "r")
note(1780, 556, "CS: Q6 an = 4,2 V (Li-Ion/LiPo)"); note(1780, 572, "    Q7 an = Laden aus"); note(1780, 588, "    beide aus = 3,6 V (LiFePO4)")

# ---------------------------------------------------------------- Relais
section(1200, 612, 2190, 790, "Relais K1 / K2 – 2-Kanal-Modul")
box(1290, 656, 190, 116)
t(1290, 650, "U11", 12, REF); t(1480, 650, "Relaismodul 5 V", 11, REF, "end")
p = pin(1290, 670, "l", "VCC", 1); wire(p, (1255, 670)); pwr(1255, 670, "+5V", "l")
p = pin(1290, 700, "l", "GND", 2); wire(p, (1255, 700)); gnd(1255, 700, "l")
p = pin(1290, 730, "l", "IN1", 3); wire(p, (1230, 730)); label(1230, 730, "K1_IN", "r")
p = pin(1290, 756, "l", "IN2", 4); wire(p, (1230, 756)); label(1230, 756, "K2_IN", "r")
coil(1400, 700, "K1"); coil(1450, 700, "K2")
t(1425, 750, "Optokoppler, Treiber", 9, PINNAME, "middle")
line(1461, 700, 1640, 700, COMP, 1, 'stroke-dasharray="4 4"')
com, no, ncp = changeover(1640, 700, "K2")
pwire((1560, 700), com); label(1560, 700, "LOAD+", "r")
pwire(no, (1840, 674)); label(1840, 674, "NODE_B", "l")
pwire(ncp, (1840, 726)); label(1840, 726, "NODE_A", "l")
note(1880, 664, "Ruhe: Last an A (NC).")
note(1880, 682, "K2 an: Last an B.")
note(1880, 712, "K1-Kontakt: siehe")
note(1880, 730, "Anschluss A (BAT+ → NODE_A).")

# ---------------------------------------------------------------- Konstantstromlast
section(1200, 806, 2190, 1410, "Konstantstromlast – 2 Kanäle, max. 30 W / 8 A")
box(1250, 850, 120, 130)
t(1250, 842, "U6", 12, REF); t(1370, 842, "MCP4725 · 0x60", 11, REF, "end")
t(1310, 966, "DAC 12 Bit", 10, PINNAME, "middle")
p = pin(1250, 870, "l", "VCC", 1); pwr(p[0], p[1], "+3V3", "u")
p = pin(1250, 900, "l", "GND", 2); gnd(p[0], p[1], "l")
p = pin(1250, 930, "l", "SDA", 3); wire(p, (1225, 930))
t(1223, 926, "SDA", 10, "#000", "end")
p = pin(1250, 955, "l", "SCL", 4); wire(p, (1225, 955))
t(1223, 951, "SCL", 10, "#000", "end")
vo = pin(1370, 880, "r", "VOUT", 5)
res_h(vo[0], 1480, 880, "R14", "7,5 kΩ")
wire((1480, 880), (1640, 880)); dot(1500, 880)
res_v(1500, 880, 950, "R15", "1 kΩ", "l"); gnd(1500, 950)
t(1520, 872, "SOLL 0–0,39 V", 10, "#000")
# Q1: sperrt den Sollwert, solange LOAD_EN_N HIGH ist
g1, d1_, s1 = nmos(1540, 1030, "Q1", "BSS138")
wire(d1_, (1586, 880)); dot(1586, 880)
gnd(*s1)
wire((1250, 1030), g1); label(1250, 1030, "LOAD_EN_N", "r")
dot(1420, 1030); res_v(1420, 970, 1030, "R18", "10 kΩ", "l"); pwr(1420, 970, "+3V3")
# Sollwert-Schiene zu beiden Kanälen
wire((1640, 880), (1640, 1145))
chan = []
for k, (cy, ref_o, ref_q, ref_rg, ref_rs, ref_rf, ref_c) in enumerate([
        (930, "U10A", "Q2", "R19", "R16", "R21", "C1"), (1160, "U10B", "Q3", "R20", "R17", "R22", "C2")]):
    ip, im, out = opamp(1700, cy, ref_o, "LM358", power=(k == 0))
    wire((1640, ip[1]), ip); dot(1640, ip[1]) if k == 0 else None
    res_h(out[0], 1840, cy, ref_rg, "1 kΩ")
    g, dr, sr = nmos(1860, cy, ref_q, "IRFP250N")
    # Drain an LOAD+
    if k == 0:
        pwire(dr, (dr[0], 850), (2170, 850))
    else:
        pwire(dr, (dr[0], cy - 50), (2060, cy - 50), (2060, 850)); dot(2060, 850)
    # Source über Messwiderstand an GND
    sx, sy = sr
    pwire(sr, (sx, sy + 50)); dot(sx, sy + 50)
    res_v(sx, sy + 50, sy + 110, ref_rs, "0,1 Ω 5 W", "r", w=3.4); gnd(sx, sy + 110)
    # Rückführung an den Minus-Eingang
    wire((sx, sy + 50), (1840, sy + 50))
    res_h(1760, 1840, sy + 50, ref_rf, "10 kΩ")
    wire((1760, sy + 50), (1660, sy + 50), (1660, im[1]), im)
    cap_v(1780, cy, sy - 10, ref_c, "10 nF", "l") if False else None
label(2170, 850, "LOAD+", "l")
note(1214, 1346, "Beide Kanäle bekommen denselben Sollwert und teilen sich den Strom. I = SOLL ÷ 0,1 Ω je Kanal,")
note(1214, 1362, "die Firmware regelt mit U4/U5 nach. C1/C2 (10 nF) je von Ausgang nach Minus-Eingang.")
note(1214, 1378, "Je Gate: R23/R24 10 kΩ nach GND und Q4/Q5 (BSS138, Gate an LOAD_EN_N, Drain am Gate, Source an GND).")
note(1214, 1394, "So sind die MOSFETs beim Booten, Flashen und bei Absturz sicher aus.")

# ---------------------------------------------------------------- Schriftfeld
X1, Y1, X2, Y2 = 1616, 1430, 2190, 1542
o.append(f'<rect x="{X1}" y="{Y1}" width="{X2-X1}" height="{Y2-Y1}" fill="none" stroke="{COMP}" stroke-width="1.4"/>')
line(X1, Y1 + 46, X2, Y1 + 46, COMP, 1); line(X1, Y1 + 80, X2, Y1 + 80, COMP, 1)
line(X1 + 290, Y1 + 46, X1 + 290, Y2, COMP, 1); line(X1 + 430, Y1 + 46, X1 + 430, Y2, COMP, 1)
t(X1 + 10, Y1 + 18, "Titel", 9, COMP); t(X1 + 10, Y1 + 38, "CellDoc – Verdrahtung", 18, "#000", weight="bold")
t(X1 + 10, Y1 + 58, "Datei", 9, COMP); t(X1 + 10, Y1 + 74, "docs/img/schaltplan.svg", 11, "#000")
t(X1 + 300, Y1 + 58, "Rev.", 9, COMP); t(X1 + 300, Y1 + 74, "1.0", 11, "#000")
t(X1 + 440, Y1 + 58, "Blatt", 9, COMP); t(X1 + 440, Y1 + 74, "1 / 1", 11, "#000")
t(X1 + 10, Y1 + 92, "Projekt", 9, COMP); t(X1 + 10, Y1 + 108, "github.com/mariofritzer/CellDoc", 11, "#000")
t(X1 + 300, Y1 + 92, "Datum", 9, COMP); t(X1 + 300, Y1 + 108, "2026-10-25", 11, "#000")
t(X1 + 440, Y1 + 92, "Format", 9, COMP); t(X1 + 440, Y1 + 108, "A2", 11, "#000")
t(1214, 1440, "Netzlabel mit gleichem Namen", 11, "#555", italic=True)
t(1214, 1456, "sind miteinander verbunden.", 11, "#555", italic=True)
t(1214, 1480, "Grün = Leitung, dick = Hochstrom,", 11, "#555", italic=True)
t(1214, 1496, "× = Pin bleibt frei.", 11, "#555", italic=True)

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{FONT}">'
       + "".join(o) + "</svg>")
open(sys.argv[1] if len(sys.argv) > 1 else "schaltplan.svg", "w", encoding="utf-8").write(svg)
print("geschrieben:", len(svg), "Bytes")
