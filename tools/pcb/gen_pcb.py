# CellDoc Nano – Platine erzeugen (KiCad pcbnew, Version 7+)
# Aufruf: python3 gen_pcb.py <ausgabe.kicad_pcb> <lib-Ordner CellDoc.pretty>
import os
import sys

import pcbnew as P

OUT = sys.argv[1]
LIBOUT = sys.argv[2]
KL = "/usr/share/kicad/footprints/"
mm = P.FromMM
W, H = 95.0, 100.0

b = P.BOARD()
ds = b.GetDesignSettings()
ds.SetCopperLayerCount(2)
ds.m_TrackMinWidth = mm(0.25)
ds.m_MinClearance = mm(0.2)
ds.m_ViasMinSize = mm(0.6)

nets = {}


def net(name):
    if name is None:
        return None
    if name not in nets:
        n = P.NETINFO_ITEM(b, name)
        b.Add(n)
        nets[name] = n
    return nets[name]


# ------------------------------------------------------------------ eigene Footprints
def fp_new(name):
    f = P.FOOTPRINT(b)
    f.SetFPID(P.LIB_ID("CellDoc", name))
    return f


def th_pad(f, num, x, y, drill=1.0, size=1.8, square=False):
    p = P.PAD(f)
    p.SetNumber(str(num))
    p.SetAttribute(P.PAD_ATTRIB_PTH)
    p.SetShape(P.PAD_SHAPE_RECT if square else P.PAD_SHAPE_CIRCLE)
    p.SetSize(P.VECTOR2I(mm(size), mm(size)))
    p.SetDrillSize(P.VECTOR2I(mm(drill), mm(drill)))
    p.SetLayerSet(P.PAD.PTHMask())
    p.SetPosition(P.VECTOR2I(mm(x), mm(y)))
    p.SetPos0(P.VECTOR2I(mm(x), mm(y))) if hasattr(p, "SetPos0") else None
    f.Add(p)
    return p


def npth(f, x, y, d):
    p = P.PAD(f)
    p.SetAttribute(P.PAD_ATTRIB_NPTH)
    p.SetShape(P.PAD_SHAPE_CIRCLE)
    p.SetSize(P.VECTOR2I(mm(d), mm(d)))
    p.SetDrillSize(P.VECTOR2I(mm(d), mm(d)))
    p.SetLayerSet(P.PAD.UnplatedHoleMask())
    p.SetPosition(P.VECTOR2I(mm(x), mm(y)))
    p.SetPos0(P.VECTOR2I(mm(x), mm(y))) if hasattr(p, "SetPos0") else None
    f.Add(p)


def oval_pad(f, num, x, y, w, h, drill):
    p = th_pad(f, num, x, y, drill=drill)
    p.SetShape(P.PAD_SHAPE_OVAL)
    p.SetSize(P.VECTOR2I(mm(w), mm(h)))
    return p


def fp_rect(f, x0, y0, x1, y1, layer, w=0.15):
    for a, c in [((x0, y0), (x1, y0)), ((x1, y0), (x1, y1)), ((x1, y1), (x0, y1)), ((x0, y1), (x0, y0))]:
        s = P.FP_SHAPE(f, P.SHAPE_T_SEGMENT)
        s.SetStart0(P.VECTOR2I(mm(a[0]), mm(a[1])))
        s.SetEnd0(P.VECTOR2I(mm(c[0]), mm(c[1])))
        s.SetLayer(layer)
        s.SetWidth(mm(w))
        f.Add(s)


def fp_text(f, txt, x, y, size=1.0, layer=P.F_SilkS):
    t = P.FP_TEXT(f)
    t.SetText(txt)
    t.SetPos0(P.VECTOR2I(mm(x), mm(y)))
    t.SetLayer(layer)
    t.SetTextSize(P.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(size * 0.15))
    f.Add(t)


def finish(f, x0, y0, x1, y1, label):
    fp_rect(f, x0, y0, x1, y1, P.F_SilkS)
    fp_rect(f, x0 - 0.25, y0 - 0.25, x1 + 0.25, y1 + 0.25, P.F_CrtYd, 0.05)
    fp_rect(f, x0, y0, x1, y1, P.F_Fab, 0.1)
    f.Value().SetText(label)
    f.Value().SetLayer(P.F_Fab)
    f.Reference().SetLayer(P.F_SilkS)
    f.Reference().SetPos0(P.VECTOR2I(mm(x0 + 1.5), mm(y0 - 1.0)))
    f.SetPosition(P.VECTOR2I(0, 0))
    for item in list(f.Pads()):
        item.SetPosition(item.GetPosition())
    f.SetPosition(P.VECTOR2I(0, 0))
    return f


def make_c3():
    # ESP32-C3 SuperMini, Chip nach oben, Antenne nach -y, USB-C nach +y
    # (v1.1 hatte die beiden Reihen vertauscht, am echten Modul geprüft)
    f = fp_new("ESP32-C3_SuperMini")
    left = ["0", "1", "2", "3", "4", "3V3", "GND", "5V"]
    right = ["21", "20", "10", "9", "8", "7", "6", "5"]
    for k in range(8):
        y = -8.89 + k * 2.54
        th_pad(f, left[k], -7.62, y, square=(left[k] == "5V"))
        th_pad(f, right[k], 7.62, y)
    for k in range(8):
        y = -8.89 + k * 2.54
        fp_text(f, "IO" + left[k] if left[k].isdigit() else left[k], -4.9, y, 0.8)
        fp_text(f, ("IO" + right[k]) if right[k].isdigit() else right[k], 4.9, y, 0.8)
    fp_text(f, "USB-C", 0, 9.6, 0.9)
    fp_text(f, "Antenne", 0, -9.6, 0.9)
    return finish(f, -9.0, -11.26, 9.0, 11.26, "ESP32-C3 SuperMini")


def make_ina():
    # CJMCU-226: 8 Pins im Raster 2,54, Platine rechts der Leiste
    f = fp_new("INA226_CJMCU-226")
    # Reihenfolge laut Aufdruck am verbauten Modul (SCL vor SDA)
    names = ["IN+", "IN-", "VBS", "ALE", "SCL", "SDA", "GND", "VCC"]
    for k, n in enumerate(names):
        th_pad(f, n, 0, k * 2.54, square=(n == "VCC"))
        fp_text(f, n, 3.4, k * 2.54, 0.8)
    fp_text(f, "INA226", 12, 9, 1.2)
    return finish(f, -1.6, -2.0, 21.0, 19.8, "INA226-Modul CJMCU-226")


def make_tp():
    # TP4056 USB-C mit Schutz, 27,9 x 17,3 mm, Ursprung oben links, USB links
    f = fp_new("TP4056_USB-C_Protected")
    pads = [("IN+", 1.6, 1.6), ("IN-", 1.6, 15.7), ("OUT+", 24.5, 1.6), ("B+", 24.5, 4.75), ("B-", 24.5, 11.8), ("OUT-", 24.5, 15.5)]
    for n, x, y in pads:
        th_pad(f, n, x, y, drill=1.1, size=2.0, square=(n == "IN+"))
        fp_text(f, n, x + (3.0 if x < 10 else -3.4), y, 0.8)
    fp_text(f, "TP4056", 13, 8.6, 1.2)
    fp_text(f, "USB frei", 6.5, 8.6, 0.8)
    return finish(f, 0, 0, 27.9, 17.3, "TP4056 USB-C mit Schutz")


def make_term35_50():
    # Schraubklemme 3-polig für 3,5 mm (KF350) UND 5 mm (KF301): Mittelloch gemeinsam,
    # äußere Löcher als Langloch von 3,5 bis 5 mm. Ursprung = mittlerer Pin, Öffnung nach +y.
    f = fp_new("Klemme_3pol_3.5_oder_5mm")
    for num, x in (("1", -4.25), ("3", 4.25)):
        p = th_pad(f, num, x, 0, drill=1.3, size=2.6, square=(num == "1"))
        p.SetShape(P.PAD_SHAPE_ROUNDRECT if num == "1" else P.PAD_SHAPE_OVAL)
        if num == "1":
            p.SetRoundRectRadiusRatio(0.25)
        p.SetSize(P.VECTOR2I(mm(4.1), mm(2.6)))
        p.SetDrillShape(P.PAD_DRILL_SHAPE_OBLONG)
        p.SetDrillSize(P.VECTOR2I(mm(2.8), mm(1.3)))
    th_pad(f, "2", 0, 0, drill=1.3, size=2.6)
    fp_rect(f, -7.6, -5.3, 7.6, 4.7, P.F_SilkS, 0.12)
    s_ = P.FP_SHAPE(f, P.SHAPE_T_SEGMENT)
    s_.SetStart0(P.VECTOR2I(mm(-7.4), mm(3.6))); s_.SetEnd0(P.VECTOR2I(mm(7.4), mm(3.6)))
    s_.SetLayer(P.F_SilkS); s_.SetWidth(mm(0.12)); f.Add(s_)
    fp_rect(f, -8.0, -5.75, 8.0, 5.15, P.F_CrtYd, 0.05)
    fp_rect(f, -7.75, -5.45, 7.75, 4.85, P.F_Fab, 0.1)
    f.Value().SetText("Klemme 3pol 3,5/5 mm")
    f.Value().SetLayer(P.F_Fab)
    f.Reference().SetLayer(P.F_SilkS)
    return f


def make_term2_35_50():
    # Schraubklemme 2-polig für 3,5 mm UND 5 mm Raster: beide Löcher als Langloch (±1,75 bis ±2,5 mm).
    # Ursprung = Mitte zwischen den Pins, Öffnung nach +y.
    f = fp_new("Klemme_2pol_3.5_oder_5mm")
    for num, x in (("1", -2.125), ("2", 2.125)):
        p = th_pad(f, num, x, 0, drill=1.3, size=2.6)
        p.SetShape(P.PAD_SHAPE_ROUNDRECT if num == "1" else P.PAD_SHAPE_OVAL)
        if num == "1":
            p.SetRoundRectRadiusRatio(0.25)
        p.SetSize(P.VECTOR2I(mm(3.35), mm(2.6)))
        p.SetDrillShape(P.PAD_DRILL_SHAPE_OBLONG)
        p.SetDrillSize(P.VECTOR2I(mm(2.05), mm(1.3)))
    fp_rect(f, -5.25, -5.45, 5.25, 4.85, P.F_SilkS, 0.12)
    s_ = P.FP_SHAPE(f, P.SHAPE_T_SEGMENT)
    s_.SetStart0(P.VECTOR2I(mm(-5.25), mm(3.6))); s_.SetEnd0(P.VECTOR2I(mm(5.25), mm(3.6)))
    s_.SetLayer(P.F_SilkS); s_.SetWidth(mm(0.12)); f.Add(s_)
    fp_rect(f, -5.5, -5.75, 5.5, 5.15, P.F_CrtYd, 0.05)
    fp_rect(f, -5.25, -5.45, 5.25, 4.85, P.F_Fab, 0.1)
    f.Value().SetText("Klemme 2pol 3,5/5 mm")
    f.Value().SetLayer(P.F_Fab)
    f.Reference().SetLayer(P.F_SilkS)
    return f


def make_holder():
    # 18650-Halter mit Kabeln (ca. 77 x 21 mm), Schraubloch in der Mitte, Lötaugen für die Kabel an beiden Enden
    f = fp_new("18650_Halter_Kabel")
    npth(f, 0, 0, 3.2)
    th_pad(f, "+", 41.5, -3.5, drill=1.3, size=2.6, square=True)
    th_pad(f, "-", 41.5, 3.5, drill=1.3, size=2.6)
    fp_text(f, "+", 44.6, -3.5, 1.4)
    fp_text(f, "−", 44.6, 3.5, 1.4)
    # zweites Paar links: für Halter mit je einem Kabel an jedem Ende (z. B. + rechts, − links)
    th_pad(f, "+", -41.5, -3.5, drill=1.3, size=2.6, square=True)
    th_pad(f, "-", -41.5, 3.5, drill=1.3, size=2.6)
    fp_text(f, "+", -41.5, -6.0, 1.2)
    fp_text(f, "−", -41.5, 6.0, 1.2)
    fp_text(f, "18650-Halter, M3 mittig", 0, 6.5, 1.0)
    fp_text(f, "Kabel an + und − links oder rechts anlöten", 0, 8.3, 0.8)
    return finish(f, -38.5, -10.5, 38.5, 10.5, "18650-Halter mit Kabeln")


def make_fuse():
    # Kabel-Sicherungshalter 5 x 20, Kabel gekürzt in die Lötaugen
    f = fp_new("Sicherungshalter_Kabel_5x20")
    th_pad(f, "1", -24.0, 0, drill=1.3, size=2.6, square=True)
    th_pad(f, "2", 24.0, 0, drill=1.3, size=2.6)
    fp_text(f, "Sicherung 3 A flink", 0, 0, 1.0)
    return finish(f, -20.0, -5.0, 20.0, 5.0, "Sicherungshalter 5x20 mit Kabel")


def make_rl():
    # Alu-Lastwiderstand 50 W: Körper 50 x 16, Laschen-Löcher 3,5 mm diagonal (41 mm), Lötfahnen auf Lötaugen
    f = fp_new("Lastwiderstand_50W_Alu")
    npth(f, -20.5, 10.5, 3.5)
    npth(f, 20.5, -10.5, 3.5)
    oval_pad(f, "1", -33.0, 0, 6.0, 3.5, 1.3)
    oval_pad(f, "2", 33.0, 0, 6.0, 3.5, 1.3)
    fp_rect(f, -24.5, 8.0, -16.5, 13.5, P.F_Fab, 0.1)
    fp_rect(f, 16.5, -13.5, 24.5, -8.0, P.F_Fab, 0.1)
    fp_text(f, "Lastwiderstand 3 Ω / 50 W", 0, -4.0, 1.4)
    fp_text(f, "mit 2 Schrauben M3 befestigen", 0, 4.0, 1.0)
    return finish(f, -25.0, -8.0, 25.0, 8.0, "Lastwiderstand 3R 50W Alu")


custom = {"term": make_term35_50(), "term2": make_term2_35_50(), "c3": make_c3(), "ina": make_ina(), "tp": make_tp(), "bat": make_holder(), "fuse": make_fuse(), "rl": make_rl()}
os.makedirs(LIBOUT, exist_ok=True)
for f in custom.values():
    P.FootprintSave(LIBOUT, f)


def load(lib, name):
    return P.FootprintLoad(KL + lib + ".pretty", name)


R_FP = ("Resistor_THT", "R_Axial_DIN0207_L6.3mm_D2.5mm_P7.62mm_Horizontal")
SOT = ("Package_TO_SOT_SMD", "SOT-23")

# ------------------------------------------------------------------ Bauteile
# ref, footprint, x, y, rot, wert, {pad: netz}
parts = [
    ("BT1", "bat", 47.5, 14.0, 0, "18650-Halter", {"+": "BAT+", "-": "GND"}),
    # Leiterplatten-Sicherungshalter 5x20, Pins an den Enden, 22,6 mm Abstand (v1.2)
    ("F1", ("Fuse", "Fuseholder_Cylinder-5x20mm_Stelvio-Kontek_PTF78_Horizontal_Open"), 68.6, 31.0, 180, "F3A 5x20", {"1": "BAT_P", "2": "BAT_F"}),
    # Verpolungsschutz: P-Kanal hinter dem Zell-Pluspol, Gate an GND. Verpolt sperrt er.
    ("Q5", SOT, 74.0, 28.5, 0, "AO3401", {"1": "GND", "2": "BAT_P", "3": "BAT+"}),
    # VBS misst am Zellpol; 2,2 kOhm begrenzen den Strom in den INA226, falls die Zelle verpolt ist
    ("R9", R_FP, 78.0, 26.5, 270, "2k2", {"1": "BAT+", "2": "VBS_IN"}),
    # Anschluss für externe Akkus, parallel zum Zellhalter (immer nur eine Zelle)
    ("J6", "term2", 88.5, 33.5, 90, "Ext. Zelle", {"1": "BAT+", "2": "GND"}),
    ("U1", "c3", 12.5, 48.5, 270, "ESP32-C3 SuperMini",
     {"5V": "+5V", "GND": "GND", "3V3": "+3V3", "3": "LOAD_PWM", "4": "ONEWIRE", "5": "SDA", "6": "SCL", "7": "CHG", "10": "BUZZER", "20": "SCAN_RX", "21": "SCAN_TX"}),
    ("U2", "ina", 59.0, 40.0, 0, "INA226 (Shunt R010)",
     {"IN+": "BAT_F", "IN-": "CELL", "VBS": "VBS_IN", "SDA": "SDA", "SCL": "SCL", "GND": "GND", "VCC": "+3V3"}),
    ("U3", "tp", 26.5, 39.0, 0, "TP4056 USB-C",
     {"IN+": "TP_IN", "IN-": "GND", "B+": "CELL", "B-": "GND"}),
    ("RL", "rl", 47.5, 86.5, 0, "3R 50W", {"1": "LOAD_D", "2": "CELL"}),
    ("Q3", SOT, 7.5, 80.0, 0, "AO3400", {"1": "LOAD_G", "2": "GND", "3": "LOAD_D"}),
    ("R3", R_FP, 3.0, 75.0, 270, "1k", {"1": "LOAD_PWM", "2": "LOAD_G"}),
    ("R5", R_FP, 9.5, 84.0, 270, "10k", {"1": "LOAD_G", "2": "GND"}),
    ("Q2", SOT, 5.0, 63.0, 0, "AO3401", {"1": "Q2_G", "2": "+5V", "3": "TP_IN"}),
    ("Q1", SOT, 5.0, 69.0, 0, "AO3400", {"1": "CHG", "2": "GND", "3": "Q2_G"}),
    ("R1", R_FP, 9.0, 62.5, 0, "10k", {"1": "Q2_G", "2": "+5V"}),
    ("R2", R_FP, 9.0, 66.5, 0, "10k", {"1": "CHG", "2": "GND"}),
    ("C1", ("Capacitor_THT", "CP_Radial_D6.3mm_P2.50mm"), 16.5, 73.8, 0, "100uF 10V", {"1": "+5V", "2": "GND"}),
    ("R4", R_FP, 84.0, 42.0, 270, "4k7", {"1": "+3V3", "2": "ONEWIRE"}),
    ("R6", R_FP, 87.5, 42.0, 270, "4k7 opt.", {"1": "+3V3", "2": "SDA"}),
    ("R7", R_FP, 91.0, 42.0, 270, "4k7 opt.", {"1": "+3V3", "2": "SCL"}),
    ("J2", "term", 21.0, 31.7, 180, "DS18B20 Zelle", {"1": "+3V3", "2": "ONEWIRE", "3": "GND"}),
    ("J3", "term", 58.5, 72.0, 0, "DS18B20 Last opt.", {"1": "+3V3", "2": "ONEWIRE", "3": "GND"}),
    ("J4", ("Connector_PinHeader_2.54mm", "PinHeader_1x04_P2.54mm_Vertical"), 37.5, 63.0, 90, "OLED opt.", {"1": "GND", "2": "+3V3", "3": "SCL", "4": "SDA"}),
    ("J5", ("Connector_PinHeader_2.54mm", "PinHeader_1x04_P2.54mm_Vertical"), 37.5, 68.5, 90, "QR GM861S opt.", {"1": "+3V3", "2": "GND", "3": "SCAN_RX", "4": "SCAN_TX"}),
    ("BZ1", ("Buzzer_Beeper", "Buzzer_12x9.5RM7.6"), 23.5, 66.5, 0, "Summer 5V", {"1": "+5V", "2": "BZ_N"}),
    ("Q4", SOT, 70.0, 63.0, 0, "AO3400", {"1": "BUZZER", "2": "GND", "3": "BZ_N"}),
    ("R8", R_FP, 70.0, 67.0, 0, "10k", {"1": "BUZZER", "2": "GND"}),
]

for ref, fpn, x, y, rot, val, padnets in parts:
    f = P.FOOTPRINT(custom[fpn]) if isinstance(fpn, str) else load(*fpn)
    f.SetReference(ref)
    f.SetValue(val)
    b.Add(f)
    f.SetPosition(P.VECTOR2I(mm(x), mm(y)))
    f.SetOrientationDegrees(rot)
    for p in f.Pads():
        n = padnets.get(p.GetNumber())
        if n:
            p.SetNet(net(n))
        if n == "GND":
            p.SetZoneConnection(P.ZONE_CONNECTION_FULL)
    f.Reference().SetTextSize(P.VECTOR2I(mm(1.0), mm(1.0)))
    if ref.startswith("R") and fpn == R_FP:
        cx, cy = f.Pads()[0].GetPosition(), f.Pads()[1].GetPosition()
        f.Reference().SetPosition(P.VECTOR2I((cx.x + cy.x) // 2, (cx.y + cy.y) // 2))
        f.Reference().SetTextSize(P.VECTOR2I(mm(0.9), mm(0.9)))
    if ref in ("J2", "J3", "J4", "J5", "J6"):
        f.Reference().SetVisible(False)
    if ref == "C1":
        f.Reference().SetPosition(P.VECTOR2I(mm(x + 1.25), mm(y + 4.6)))
    if ref == "BZ1":
        f.Reference().SetPosition(P.VECTOR2I(mm(x + 3.8), mm(y - 7.0)))
    if ref == "Q4":
        f.Reference().SetPosition(P.VECTOR2I(mm(x + 3.2), mm(y)))
    f.Reference().SetTextThickness(mm(0.15))

# ------------------------------------------------------------------ Umriss, Löcher
def edge(x0, y0, x1, y1):
    s = P.PCB_SHAPE(b, P.SHAPE_T_SEGMENT)
    s.SetStart(P.VECTOR2I(mm(x0), mm(y0)))
    s.SetEnd(P.VECTOR2I(mm(x1), mm(y1)))
    s.SetLayer(P.Edge_Cuts)
    s.SetWidth(mm(0.1))
    b.Add(s)


R = 3.0
edge(R, 0, W - R, 0); edge(W, R, W, H - R); edge(W - R, H, R, H); edge(0, H - R, 0, R)
for cx, cy, a in [(R, R, 180), (W - R, R, 270), (W - R, H - R, 0), (R, H - R, 90)]:
    s = P.PCB_SHAPE(b, P.SHAPE_T_ARC)
    s.SetLayer(P.Edge_Cuts)
    s.SetWidth(mm(0.1))
    s.SetCenter(P.VECTOR2I(mm(cx), mm(cy)))
    import math
    st = (cx + R * math.cos(math.radians(a)), cy + R * math.sin(math.radians(a)))
    s.SetStart(P.VECTOR2I(mm(st[0]), mm(st[1])))
    s.SetArcAngleAndEnd(P.EDA_ANGLE(90, P.DEGREES_T))
    b.Add(s)

for k, (x, y) in enumerate([(3.5, 3.5), (91.5, 3.5), (3.5, 96.5), (91.5, 96.5)]):
    f = load("MountingHole", "MountingHole_3.2mm_M3")
    f.SetReference("H%d" % (k + 1))
    f.Reference().SetVisible(False)
    b.Add(f)
    f.SetPosition(P.VECTOR2I(mm(x), mm(y)))


# ------------------------------------------------------------------ Texte
def text(s, x, y, size=1.2, layer=P.F_SilkS, bold=False):
    t = P.PCB_TEXT(b)
    t.SetText(s)
    t.SetPosition(P.VECTOR2I(mm(x), mm(y)))
    t.SetLayer(layer)
    t.SetTextSize(P.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(size * (0.2 if bold else 0.15)))
    if layer == P.B_SilkS:
        t.SetMirrored(True)
    b.Add(t)
    return t


text("CellDoc Nano", 34.0, 75.8, 2.0, bold=True)
text("v1.2 · 2026-11", 86.0, 71.5, 1.0)
# Fühlerklemmen: Öffnung zur Zelle bzw. zum Lastwiderstand, Aufdruck je Pin auf der Rückseite der Klemme
def term_labels(x0, y0, rot, caption, cap_x):
    # x0/y0 = mittlerer Pin; Aufdruck hinter der Klemme
    sgn = 1 if rot == 0 else -1
    ly = y0 - 6.3 * sgn
    for dx, l in ((-4.25, "+"), (0, "D"), (4.25, "−")):
        text(l, x0 + sgn * dx, ly, 1.0)
    if cap_x:
        text(caption, cap_x, ly, 0.8)
    else:
        text(caption, x0, ly - 1.4 * sgn, 0.8)


term_labels(21.0, 31.7, 180, "J2 Zelle", 12.2)
term_labels(58.5, 72.0, 0, "J3 Last", 0)
# J6: Pins senkrecht, Öffnung zum rechten Rand
text("+", 82.0, 35.6, 1.2)
text("−", 82.0, 31.4, 1.2)
text("J6 Ext. Zelle", 89.0, 26.6, 0.8)
text("nur 1 Zelle", 89.0, 40.0, 0.8)
text("GND VCC SCL SDA", 41.3, 60.6, 0.8)
text("OLED", 48.8, 63.0, 0.9)
text("3V3 GND RX TX", 41.3, 71.4, 0.8)
text("QR-Scanner", 47.9, 68.6, 0.8).SetTextAngleDegrees(90)
text("github.com/mariofritzer/CellDoc", 47.5, 92.0, 1.2, P.B_SilkS)

# Keine Kupferfläche unter der C3-Antenne
def keepout(x0, y0, x1, y1, pour=False):
    z = P.ZONE(b)
    z.SetIsRuleArea(True)
    z.SetDoNotAllowCopperPour(not pour)
    z.SetDoNotAllowTracks(True)
    z.SetDoNotAllowVias(True)
    z.SetDoNotAllowPads(False)
    z.SetDoNotAllowFootprints(False)
    ls = P.LSET(); ls.AddLayer(P.F_Cu); ls.AddLayer(P.B_Cu)
    z.SetLayerSet(ls)
    o = z.Outline(); o.NewOutline()
    for x, y in [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]:
        o.Append(mm(x), mm(y))
    b.Add(z)


keepout(19.0, 42.8, 24.2, 54.2)
# Rand frei halten (Leiterbahnen, Vias)
for r in [(0, 0, W, 1.0), (0, H - 1.0, W, H), (0, 0, 1.0, H), (W - 1.0, 0, W, H)]:
    keepout(*r, pour=True)

b.Save(OUT)
print("ok", len(parts), "Teile,", len(nets), "Netze")
