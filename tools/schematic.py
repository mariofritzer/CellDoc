# Symbolbibliothek für die CellDoc-Schaltpläne (Stil wie Liquid Harvest)
import math

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


DIP_SET = {5: "0 0 0", 9: "1 0 0", 12: "1 1 0", 15: "0 1 0", 20: "0 1 1"}


def usbc_decoy(x, y, ref, volts):
    """USB-C-PD-Decoy-Modul (Schalter S1–S3), Ausgang rechts: liefert (+, −)"""
    box(x, y, 70, 92)
    o.append(f'<rect x="{x + 14}" y="{y + 36}" width="26" height="12" rx="6" fill="#e9e6da" stroke="{COMP}" stroke-width="1.4"/>')
    o.append(f'<rect x="{x + 19}" y="{y + 40}" width="16" height="4" rx="2" fill="{COMP}"/>')
    t(x + 27, y + 66, "USB-C", 9, PINNAME, "middle")
    t(x + 27, y + 80, "PD", 9, PINNAME, "middle", "bold")
    t(x, y - 8, ref, 12, REF); t(x + 70, y - 8, f"PD-Decoy · {volts} V", 11, REF)
    t(x - 20, y + 110, f"S1 S2 S3 = {DIP_SET[volts]}", 10, REF)
    a = pin(x + 70, y + 23, "r", "+", 1)
    b = pin(x + 70, y + 68, "r", "−", 2)
    return a, b


def opamp(x, y, ref, val, power=False, pins=None):
    """Dreieck nach rechts; liefert (in+, in-, out) als Anschlusspunkte. pins = (in+, in-, out)"""
    o.append(f'<path d="M{x} {y-34} L{x} {y+34} L{x+64} {y} Z" fill="{FILL}" stroke="{COMP}" stroke-width="1.6"/>')
    t(x + 7, y - 11, "+", 13, COMP, weight="bold"); t(x + 7, y + 23, "−", 13, COMP, weight="bold")
    if pins is None:
        pins = (3, 2, 1) if ref.endswith("A") else (5, 6, 7)
    ip = pin(x, y - 15, "l", None, pins[0])
    im = pin(x, y + 15, "l", None, pins[1])
    out = pin(x + 64, y, "r", None, pins[2])
    t(x - 4, y - 40, ref, 11, REF, "end"); t(x + 42, y + 50, val, 11, REF)
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




def frame(W, H, rows="ABCDEF"):
    o.append(f'<rect width="{W}" height="{H}" fill="{BG}"/>')
    o.append(f'<rect x="20" y="20" width="{W-40}" height="{H-40}" fill="none" stroke="{COMP}" stroke-width="1.6"/>')
    o.append(f'<rect x="36" y="36" width="{W-72}" height="{H-72}" fill="none" stroke="{COMP}" stroke-width="1"/>')
    for i in range(8):
        x = 36 + (W - 72) * (i + .5) / 8
        t(x, 32, str(i + 1), 11, COMP, "middle"); t(x, H - 23, str(i + 1), 11, COMP, "middle")
        if i:
            xx = 36 + (W - 72) * i / 8
            line(xx, 20, xx, 36, COMP, 1); line(xx, H - 36, xx, H - 20, COMP, 1)
    n = len(rows)
    for i, c in enumerate(rows):
        y = 36 + (H - 72) * (i + .5) / n
        t(28, y + 4, c, 11, COMP, "middle"); t(W - 28, y + 4, c, 11, COMP, "middle")
        if i:
            yy = 36 + (H - 72) * i / n
            line(20, yy, 36, yy, COMP, 1); line(W - 36, yy, W - 20, yy, COMP, 1)


def title_block(X1, Y1, title, path, rev, date, fmt, project="github.com/mariofritzer/CellDoc"):
    X2, Y2 = X1 + 574, Y1 + 112
    o.append(f'<rect x="{X1}" y="{Y1}" width="{X2-X1}" height="{Y2-Y1}" fill="none" stroke="{COMP}" stroke-width="1.4"/>')
    line(X1, Y1 + 46, X2, Y1 + 46, COMP, 1); line(X1, Y1 + 80, X2, Y1 + 80, COMP, 1)
    line(X1 + 290, Y1 + 46, X1 + 290, Y2, COMP, 1); line(X1 + 430, Y1 + 46, X1 + 430, Y2, COMP, 1)
    t(X1 + 10, Y1 + 18, "Titel", 9, COMP); t(X1 + 10, Y1 + 38, title, 18, "#000", weight="bold")
    t(X1 + 10, Y1 + 58, "Datei", 9, COMP); t(X1 + 10, Y1 + 74, path, 11, "#000")
    t(X1 + 300, Y1 + 58, "Rev.", 9, COMP); t(X1 + 300, Y1 + 74, rev, 11, "#000")
    t(X1 + 440, Y1 + 58, "Blatt", 9, COMP); t(X1 + 440, Y1 + 74, "1 / 1", 11, "#000")
    t(X1 + 10, Y1 + 92, "Projekt", 9, COMP); t(X1 + 10, Y1 + 108, project, 11, "#000")
    t(X1 + 300, Y1 + 92, "Datum", 9, COMP); t(X1 + 300, Y1 + 108, date, 11, "#000")
    t(X1 + 440, Y1 + 92, "Format", 9, COMP); t(X1 + 440, Y1 + 108, fmt, 11, "#000")


def save(path, W, H):
    svg = (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" font-family="{FONT}">'
           + "".join(o) + "</svg>")
    open(path, "w", encoding="utf-8").write(svg)
    print("geschrieben:", path, len(svg), "Bytes")
