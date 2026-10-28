# CellDoc Nano – Platine erzeugen (KiCad pcbnew, Version 7+)
# Aufruf: python3 gen_pcb.py <ausgabe.kicad_pcb> <lib-Ordner CellDoc.pretty>
import os
import sys

import pcbnew as P

OUT = sys.argv[1]
LIBOUT = sys.argv[2]
KL = "/usr/share/kicad/footprints/"
mm = P.FromMM
W, H = 95.0, 76.0

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
    # ESP32-C3 SuperMini von oben, Antenne nach -y, USB-C nach +y
    f = fp_new("ESP32-C3_SuperMini")
    left = ["21", "20", "10", "9", "8", "7", "6", "5"]
    right = ["0", "1", "2", "3", "4", "3V3", "GND", "5V"]
    for k in range(8):
        y = -8.89 + k * 2.54
        th_pad(f, left[k], -7.62, y)
        th_pad(f, right[k], 7.62, y, square=(right[k] == "5V"))
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
    names = ["IN+", "IN-", "VBS", "ALE", "SDA", "SCL", "GND", "VCC"]
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


custom = {"c3": make_c3(), "ina": make_ina(), "tp": make_tp()}
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
    ("BT1", ("Battery", "BatteryHolder_Keystone_1042_1x18650"), 47.5, 13.5, 180, "18650 Keystone 1042", {"1": "BAT+", "2": "GND"}),
    ("F1", ("Fuse", "Fuseholder_Clip-5x20mm_Littelfuse_111_Inline_P20.00x5.00mm_D1.05mm_Horizontal"), 66.0, 29.5, 0, "3 A flink 5x20", {"1": "BAT_F", "2": "BAT+"}),
    ("U1", "c3", 13.0, 64.0, 0, "ESP32-C3 SuperMini",
     {"5V": "+5V", "GND": "GND", "3V3": "+3V3", "3": "LOAD_PWM", "4": "ONEWIRE", "5": "SDA", "6": "SCL", "7": "CHG", "10": "BUZZER", "20": "SCAN_RX", "21": "SCAN_TX"}),
    ("U2", "ina", 56.0, 37.0, 0, "INA226 (Shunt R010)",
     {"IN+": "BAT_F", "IN-": "CELL", "VBS": "BAT+", "SDA": "SDA", "SCL": "SCL", "GND": "GND", "VCC": "+3V3"}),
    ("U3", "tp", 22.5, 27.0, 0, "TP4056 USB-C",
     {"IN+": "TP_IN", "IN-": "GND", "B+": "CELL", "B-": "GND"}),
    ("J1", ("TerminalBlock_Phoenix", "TerminalBlock_Phoenix_MKDS-1,5-2-5.08_1x02_P5.08mm_Horizontal"), 89.0, 44.5, 90, "Lastwiderstand 3 Ohm", {"1": "LOAD_D", "2": "CELL"}),
    ("Q3", SOT, 84.0, 53.5, 0, "AO3400", {"1": "LOAD_G", "2": "GND", "3": "LOAD_D"}),
    ("R3", R_FP, 79.5, 58.5, 0, "1k", {"1": "LOAD_PWM", "2": "LOAD_G"}),
    ("R5", R_FP, 79.5, 62.0, 0, "10k", {"1": "LOAD_G", "2": "GND"}),
    ("Q2", SOT, 12.0, 30.0, 270, "AO3401", {"1": "Q2_G", "2": "+5V", "3": "TP_IN"}),
    ("Q1", SOT, 12.0, 39.5, 0, "AO3400", {"1": "CHG", "2": "GND", "3": "Q2_G"}),
    ("R1", R_FP, 3.5, 27.0, 90, "10k", {"1": "Q2_G", "2": "+5V"}),
    ("R2", R_FP, 17.5, 36.0, 90, "10k", {"1": "CHG", "2": "GND"}),
    ("C1", ("Capacitor_THT", "CP_Radial_D6.3mm_P2.50mm"), 26.5, 50.0, 0, "100uF 10V", {"1": "+5V", "2": "GND"}),
    ("R4", R_FP, 25.0, 58.0, 0, "4k7", {"1": "+3V3", "2": "ONEWIRE"}),
    ("R6", R_FP, 25.0, 61.5, 0, "4k7 opt.", {"1": "+3V3", "2": "SDA"}),
    ("R7", R_FP, 25.0, 65.0, 0, "4k7 opt.", {"1": "+3V3", "2": "SCL"}),
    ("J2", ("TerminalBlock_Phoenix", "TerminalBlock_Phoenix_PT-1,5-3-3.5-H_1x03_P3.50mm_Horizontal"), 33.0, 70.5, 0, "DS18B20 Zelle", {"1": "+3V3", "2": "ONEWIRE", "3": "GND"}),
    ("J3", ("TerminalBlock_Phoenix", "TerminalBlock_Phoenix_PT-1,5-3-3.5-H_1x03_P3.50mm_Horizontal"), 44.5, 70.5, 0, "DS18B20 Last opt.", {"1": "+3V3", "2": "ONEWIRE", "3": "GND"}),
    ("J4", ("Connector_PinHeader_2.54mm", "PinHeader_1x04_P2.54mm_Vertical"), 36.0, 49.0, 90, "OLED opt.", {"1": "GND", "2": "+3V3", "3": "SCL", "4": "SDA"}),
    ("J5", ("Connector_PinHeader_2.54mm", "PinHeader_1x04_P2.54mm_Vertical"), 36.0, 54.0, 90, "QR GM861S opt.", {"1": "+3V3", "2": "GND", "3": "SCAN_RX", "4": "SCAN_TX"}),
    ("BZ1", ("Buzzer_Beeper", "Buzzer_12x9.5RM7.6"), 56.5, 68.5, 0, "Summer 5V opt.", {"1": "+5V", "2": "BZ_N"}),
    ("Q4", SOT, 70.0, 64.0, 0, "AO3400 opt.", {"1": "BUZZER", "2": "GND", "3": "BZ_N"}),
    ("R8", R_FP, 70.0, 60.0, 180, "10k opt.", {"1": "BUZZER", "2": "GND"}),
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
        if ref in ("BT1", "Q3") and n == "GND":
            p.SetZoneConnection(P.ZONE_CONNECTION_FULL)
    f.Reference().SetTextSize(P.VECTOR2I(mm(1.0), mm(1.0)))
    if ref.startswith("R") and fpn == R_FP:
        cx, cy = f.Pads()[0].GetPosition(), f.Pads()[1].GetPosition()
        f.Reference().SetPosition(P.VECTOR2I((cx.x + cy.x) // 2, (cx.y + cy.y) // 2))
        f.Reference().SetTextSize(P.VECTOR2I(mm(0.9), mm(0.9)))
    if ref in ("J2", "J3", "BT1"):
        f.Reference().SetVisible(False)
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

for k, (x, y) in enumerate([(3.5, 48.5), (91.5, 30.0), (91.5, 72.5)]):
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


text("CellDoc Nano", 83, 66.8, 2.0, bold=True)
text("v1.0 · 2026-10", 83, 69.4, 1.0)
text("+", 89.5, 7.5, 3.0, bold=True)
text("−", 5.5, 7.5, 3.0, bold=True)
text("Zelle nach Aufdruck einlegen", 47.5, 25.9, 1.0)
text("LAST 3 Ω", 85.5, 36.0, 1.0)
text("Zelle + D −", 37.0, 66.2, 0.8)
text("Last + D −", 48.5, 66.2, 0.8)
text("GND VCC SCL SDA", 39.8, 46.6, 0.8)
text("3V3 GND RX TX", 39.8, 56.6, 0.8)
text("github.com/mariofritzer/CellDoc", 74.0, 74.6, 0.9, P.B_SilkS)

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


keepout(6.6, 52.6, 19.4, 57.0)
# Rand frei halten (Leiterbahnen, Vias)
for r in [(0, 0, W, 1.0), (0, H - 1.0, W, H), (0, 0, 1.0, H), (W - 1.0, 0, W, H)]:
    keepout(*r, pour=True)

b.Save(OUT)
print("ok", len(parts), "Teile,", len(nets), "Netze")
