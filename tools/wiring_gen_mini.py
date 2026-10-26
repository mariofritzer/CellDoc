# Erzeugt den Schaltplan der CellDoc Mini (1, 2 oder 4 Schächte)
# Aufruf: python3 tools/wiring_gen_mini.py <schächte> docs/img/schaltplan-mini-<n>.svg
import sys

from schematic import *  # noqa: F401,F403
from schematic import o

N = int(sys.argv[1]) if len(sys.argv) > 1 else 2
BOARD = "s3" if "s3" in sys.argv[3:] else "esp32"
SFX = "-s3" if BOARD == "s3" else ""
OUT = sys.argv[2] if len(sys.argv) > 2 else f"schaltplan-mini-{N}{SFX}.svg"
S3 = BOARD == "s3"
assert N in (1, 2, 4)
IMAX = 1.0 if N == 4 else 2.0
OPAMP = "LM324" if N > 2 else "LM358"
VUSB = 12 if N == 4 else 5  # Decoy-Spannung
SUPPLY = {1: "USB-C-Netzteil ab 10 W", 2: "USB-C-Netzteil 5 V / 3 A", 4: "USB-C-Netzteil mit 12 V, ≥ 36 W"}[N]

W = 2240
BLOCK = 286
H = 1584
frame(W, H)

# ================================================================ Linke Spalte
# ---------------------------------------------------------------- Stromversorgung
section(50, 54, 720, 276, "Stromversorgung")
a, b = usbc_decoy(90, 112, "J1", VUSB)
if N == 4:
    # 12 V aus dem Netzteil, 5-V-Regler mit 5 A für vier Lader
    fuse_h(a[0], 260, 135, "F0", "3 A")
    pwire((260, 135), (330, 135)); dot(300, 135); pwr(300, 135, "+12V")
    pwire(b, (330, 180)); dot(280, 180); gnd(280, 180)
    box(340, 110, 110, 100)
    t(340, 102, "U11", 12, REF); t(395, 228, "Mini560 · 5 V / 5 A", 10, REF, "middle")
    t(395, 166, "12V→5V", 12, PINNAME, "middle", "bold")
    pin(340, 135, "l", "IN+", 1, L=10); pin(340, 180, "l", "IN−", 2, L=10)
    c = pin(450, 135, "r", "OUT", 3, L=10); d = pin(450, 180, "r", "GND", 4, L=10)
    pwire(c, (640, 135)); dot(520, 135); pwr(520, 135, "+5V")
    pwire(d, (640, 180)); dot(500, 180); gnd(500, 180)
    dot(590, 135); dot(590, 180); cap_v(590, 135, 180, "C1", "470 µF")
else:
    fuse_h(a[0], 270, 135, "F0", "3 A")
    pwire((270, 135), (560, 135)); dot(360, 135); pwr(360, 135, "+5V")
    pwire(b, (560, 180)); dot(330, 180); gnd(330, 180)
    dot(460, 135); dot(460, 180); cap_v(460, 135, 180, "C1", "470 µF")
note(66, 246, "+5V: ESP32 (VIN), TP4056, Opamp, Lüfter, Summer, OLED." + (" +12V nur für U11." if N == 4 else ""))
note(66, 262, "+3V3 vom ESP32-Board: INA226, MCP4728, DS18B20, Pull-ups.")

# ---------------------------------------------------------------- Sollwerte
section(50, 292, 720, 640, f"Sollwerte und Freigabe der Last")
MX = 130
box(MX, 330, 130, 180)
t(MX, 322, "U2", 12, REF); t(MX + 130, 322, "MCP4728 · 0x60", 11, REF, "end")
t(MX + 65, 500, "DAC 4 × 12 Bit", 10, PINNAME, "middle")
p = pin(MX, 350, "l", "VDD", 1, L=16); pwr(p[0], p[1], "+3V3", "l")
p = pin(MX, 380, "l", "GND", 10, L=16); gnd(p[0], p[1], "l")
p = pin(MX, 410, "l", "SDA", 3, L=16); wire(p, (MX - 50, 410)); t(MX - 48, 406, "SDA", 10, "#000")
p = pin(MX, 440, "l", "SCL", 2, L=16); wire(p, (MX - 50, 440)); t(MX - 48, 436, "SCL", 10, "#000")
p = pin(MX, 470, "l", "LDAC", 4, L=16); gnd(p[0], p[1], "l")
GB = 480
for k in range(4):
    y = 350 + k * 44
    p = pin(MX + 130, y, "r", f"VOUT{'ABCD'[k]}", 6 + k)
    if k < N:
        res_h(p[0], 350, y, f"R{1 + k}", "7,5 kΩ")
        wire((350, y), (380, y)); dot(365, y)
        wire((365, y), (365, y - 14)); t(365, y - 18, f"SET{k + 1}", 11, "#000", "middle")
        res_h(380, 450, y, f"R{5 + k}", "1 kΩ")
        wire((450, y), (GB, y)); dot(GB, y)
    else:
        nc(*p)
wire((GB, 350), (GB, 350 + 44 * (N - 1) + 10)); gnd(GB, 350 + 44 * (N - 1) + 10)
note(66, 536, "Teiler 7,5 kΩ / 1 kΩ: 0–3,3 V am DAC → 0–0,39 V Sollwert.")
note(66, 552, f"I = SET ÷ 0,1 Ω je Schacht, Firmware begrenzt auf {IMAX:g} A.".replace(".", ",", 1) if False else f"I = SET ÷ 0,1 Ω je Schacht, die Firmware begrenzt auf {str(IMAX).replace('.', ',')} A.")
# Lastfreigabe
wire((90, 612), (300, 612)); label(90, 612, "LOAD_EN_N", "r")
dot(220, 612); res_v(220, 580, 612, "R9", "10 kΩ") if False else None
t(170, 628, "R9 10 kΩ nach +3V3", 10, REF)
t(310, 608, f"→ Gate-Klemmen Q{N * 3 + 1}…: BSS138 ziehen bei HIGH", 10, REF)
t(310, 622, "alle Last-MOSFETs auf GND (Booten, Absturz).", 10, REF)
# Opamp als Chip (Draufsicht, Kerbe oben): zeigt, wo welche Pins am LM324/LM358 sitzen
OUTR = [22 + 3 * k for k in range(4)]   # Gate-Widerstand je Schacht (R22, R25 …)
FBR = [24 + 3 * k for k in range(4)]    # Rückführung vom Messwiderstand (R24, R27 …)
if OPAMP == "LM324":
    left = [("1OUT", 1, f"R{OUTR[0]}"), ("1IN−", 2, f"R{FBR[0]}"), ("1IN+", 3, "SET1"), ("VCC", 4, "+5V"),
            ("2IN+", 5, "SET2"), ("2IN−", 6, f"R{FBR[1]}"), ("2OUT", 7, f"R{OUTR[1]}")]
    right = [("4OUT", 14, f"R{OUTR[3]}"), ("4IN−", 13, f"R{FBR[3]}"), ("4IN+", 12, "SET4"), ("GND", 11, "GND"),
             ("3IN+", 10, "SET3"), ("3IN−", 9, f"R{FBR[2]}"), ("3OUT", 8, f"R{OUTR[2]}")]
else:
    two = N > 1
    left = [("1OUT", 1, f"R{OUTR[0]}"), ("1IN−", 2, f"R{FBR[0]}"), ("1IN+", 3, "SET1"), ("GND", 4, "GND")]
    right = [("VCC", 8, "+5V"), ("2OUT", 7, f"R{OUTR[1]}" if two else "BRIDGE"), ("2IN−", 6, f"R{FBR[1]}" if two else "BRIDGE"),
             ("2IN+", 5, "SET2" if two else "GND")]
DX, DY, DW, RS = 560, 372, 80, 22
DH = (len(left) - 1) * RS + 24
t(DX, DY - 24, f"U3 {OPAMP} von oben", 11, REF, "start", "bold")
box(DX, DY - 12, DW, DH)
o.append(f'<path d="M{DX + DW / 2 - 7} {DY - 12} a7 7 0 0 0 14 0" fill="none" stroke="{COMP}" stroke-width="1.4"/>')
for i, (nm, num, net) in enumerate(left):
    y = DY + i * RS
    p = pin(DX, y, "l", nm, num, L=16)
    if net == "+5V": wire(p, (p[0] - 16, y)); pwr(p[0] - 16, y, "+5V", "l")
    elif net == "GND": wire(p, (p[0] - 16, y)); gnd(p[0] - 16, y, "l")
    else: wire(p, (p[0] - 34, y)); label(p[0] - 34, y, net, "r")
bridge = []
for i, (nm, num, net) in enumerate(right):
    y = DY + i * RS
    p = pin(DX + DW, y, "r", nm, num, L=16)
    if net == "+5V": wire(p, (p[0] + 16, y)); pwr(p[0] + 16, y, "+5V", "r")
    elif net == "GND": wire(p, (p[0] + 16, y)); gnd(p[0] + 16, y, "r")
    elif net == "BRIDGE": wire(p, (p[0] + 14, y)); bridge.append((p[0] + 14, y))
    else: wire(p, (p[0] + 34, y)); label(p[0] + 34, y, net, "l")
if len(bridge) == 2:
    wire(bridge[0], bridge[1])
yb = DY - 12 + DH
units = "U3A–U3D" if OPAMP == "LM324" else ("U3A/U3B" if N > 1 else "U3A")
for k, line_ in enumerate([f"Die Dreiecke {units}", "bei den Schächten sind", "die Opamps im Chip."] + (["Pin 6+7 brücken, 5 an GND."] if N == 1 else [])):
    t(DX - 40, yb + 16 + k * 14, line_, 10, REF)

# ---------------------------------------------------------------- Bedienung, Temperatur
section(50, 656, 720, 1548, "Bedienung, Temperatur, Lüfter")
# OLED
box(90, 700, 150, 110)
o.append(f'<rect x="105" y="715" width="80" height="44" fill="#e9e6da" stroke="{COMP}" stroke-width="1"/>')
t(145, 742, "128×64", 10, PINNAME, "middle")
t(90, 692, "U4", 12, REF); t(90, 828, "SH1106 1,3\" · 0x3C (optional)", 11, REF)
for k, (n_, net) in enumerate([("VCC", "+3V3"), ("GND", "GND"), ("SCL", "SCL"), ("SDA", "SDA")]):
    y = 720 + k * 24
    p = pin(240, y, "r", n_, k + 1)
    if net == "+3V3":
        wire(p, (290, y)); pwr(290, y, "+3V3", "r")
    elif net == "GND":
        wire(p, (290, y)); gnd(290, y, "r")
    else:
        wire(p, (330, y)); label(330, y, net, "l")
# Taster
for k, (ref, net, io) in enumerate([("SW1", "BTN_PREV", "IO17" if S3 else "IO34"), ("SW2", "BTN_OK", "IO18" if S3 else "IO35"), ("SW3", "BTN_NEXT", "IO11" if S3 else "IO39")]):
    x = 430 + k * 90
    wire((x, 760), (x, 790)); dot(x, 760)
    res_v(x, 700, 760, f"R{10 + k}", "10 kΩ")
    pwr(x, 700, "+3V3")
    o.append(f'<circle cx="{x}" cy="794" r="3.5" fill="none" stroke="{COMP}" stroke-width="1.5"/><circle cx="{x}" cy="830" r="3.5" fill="none" stroke="{COMP}" stroke-width="1.5"/>')
    line(x - 12, 796, x - 12, 828, COMP, 1.6); line(x - 12, 812, x - 22, 812, COMP, 1.6)
    t(x + 8, 816, ref, 10, REF)
    wire((x, 834), (x, 850)); gnd(x, 850)
    wire((x, 760), (x + 40, 760))
    t(x + 4, 884, net, 10, "#000")
    t(x + 4, 898, io, 9, PINNUM)
    wire((x + 40, 760), (x + 40, 870)); label(x + 40, 870, "", "r")
note(66, 930, "OLED und Taster sind optional. Die Firmware erkennt das Display beim Start.")
note(66, 946, "◀ / ▶ wählt den Schacht, OK kurz = Test starten, OK lang = stoppen.")

# DS18B20
rx = 170 + 100 * (N + 1) + 20
wire((110, 1000), (rx, 1000)); pwr(110, 1000, "+3V3", "l")
wire((110, 1060), (rx, 1060)); label(110, 1060, "ONEWIRE", "r")
res_v(rx, 1000, 1060, "R13", "4,7 kΩ", "l")
sensors = [(f"U{5 + k}", f"T{k + 1} Zelle {k + 1}") for k in range(N)] + [(f"U{5 + N}", "Kühlkörper")]
for k, (ref, where) in enumerate(sensors):
    bx = 170 + k * 100
    box(bx, 1090, 60, 70)
    t(bx + 30, 1122, "DS18B20", 9, PINNAME, "middle", "bold")
    t(bx + 5, 1154, ref, 11, REF)
    wire((bx + 12, 1090), (bx + 12, 1000)); dot(bx + 12, 1000)
    wire((bx + 32, 1090), (bx + 32, 1060)); dot(bx + 32, 1060)
    t(bx + 9, 1104, "V", 9, PINNAME); t(bx + 29, 1104, "Q", 9, PINNAME); t(bx + 47, 1156, "G", 9, PINNAME)
    line(bx + 50, 1160, bx + 50, 1174); gnd(bx + 50, 1174)
    t(bx + 30, 1212, where, 10, REF, "middle")
note(66, 1240, "Fühler nach Wahl TO-92 oder wasserdicht. Zellfühler im Halter unter der Zelle.")

# Lüfter und Summer
box(90, 1300, 80, 70)
o.append(f'<circle cx="130" cy="1335" r="20" fill="none" stroke="{COMP}" stroke-width="1.4"/>')
t(130, 1341, "M", 15, COMP, "middle", "bold")
t(90, 1292, "M1", 12, REF); t(180, 1292, "Lüfter 5 V (optional)", 11, REF)
p = pin(130, 1300, "u", None, None, L=0)
wire((130, 1300), (130, 1280)); pwr(130, 1280, "+5V")
wire((130, 1370), (130, 1400))
g, dr, sr = nmos(84, 1430, f"Q{3 * N + N + 1}", "AO3400")
wire(dr, (130, 1400))
gnd(*sr)
wire((60, 1430), g); label(60, 1430, "FAN", "r")
box(330, 1300, 60, 44)
o.append(f'<path d="M348 1312 h12 l10 -8 v36 l-10 -8 h-12 z" fill="none" stroke="{COMP}" stroke-width="1.3"/>')
t(330, 1292, "BZ1", 12, REF); t(360, 1362, "aktiv, 5 V", 10, REF, "middle")
p = pin(330, 1322, "l", None, None, L=20); pwr(p[0], p[1], "+5V", "l")
p = pin(390, 1322, "r", None, None, L=40)
g, dr, sr = nmos(420, 1430, f"Q{3 * N + N + 2}", "AO3400")
wire(p, (dr[0], 1322), dr); gnd(*sr)
wire((330, 1430), g); label(330, 1430, "BUZZER", "r")
note(66, 1500, "Lüfter und Summer werden über AO3400 nach GND geschaltet (Gate 10 kΩ Pulldown).")

# QR-Scanner (optional): GM861S, 3,3 V, UART 9600 Baud
t(520, 1274, "QR-Scanner (optional)", 11, REF, "start", "bold")
box(520, 1300, 70, 104)
t(555, 1356, "QR", 14, PINNAME, "middle", "bold")
t(520, 1292, f"U{6 + N}", 12, REF); t(590, 1420, "GM861S · 3,3 V", 10, REF, "end")
for k, (n_, net) in enumerate([("VCC", "+3V3"), ("GND", "GND"), ("TX", "SCAN_TX"), ("RX", "SCAN_RX")]):
    yy = 1300 + 16 + k * 24
    p = pin(590, yy, "r", n_, k + 1)
    if net == "+3V3":
        wire(p, (640, yy)); pwr(640, yy, "+3V3", "r")
    elif net == "GND":
        wire(p, (640, yy)); gnd(640, yy, "r")
    else:
        wire(p, (705, yy)); label(705, yy, net, "l")

# ================================================================ Mitte: ESP32
EX, EY, EW = 860, 96, 200
chg = {k: (f"CHG{k}" if k <= N else None) for k in range(1, 5)}
if S3:
    nets = {4: "ONEWIRE", 5: "LOAD_EN_N", 6: chg[1], 7: chg[2], 15: chg[3], 16: chg[4], 8: "SDA", 9: "SCL", 10: "FAN",
            11: "BTN_NEXT", 12: "BUZZER", 13: "SCAN_TX", 14: "SCAN_RX", 17: "BTN_PREV", 18: "BTN_OK"}
else:
    nets = {39: "BTN_NEXT", 34: "BTN_PREV", 35: "BTN_OK", 32: chg[3], 33: chg[4], 25: "FAN", 26: chg[1], 27: chg[2],
            22: "SCL", 21: "SDA", 19: "SCAN_TX", 18: "SCAN_RX", 17: "BUZZER", 16: "LOAD_EN_N", 4: "ONEWIRE"}
EH = mcu_block(BOARD, EX, EY, nets, EW)
note(EX - 110, EY + EH + 30, f"I²C: SDA = {'IO8' if S3 else 'IO21'}, SCL = {'IO9' if S3 else 'IO22'}, je 4,7 kΩ nach +3V3.")
note(EX - 110, EY + EH + 46, f"INA226 der Schächte: 0x40{', 0x41' if N > 1 else ''}{', 0x44, 0x45' if N > 2 else ''}.")
note(EX - 110, EY + EH + 62, "Adresse am Modul: 0x41 A0→VCC, 0x44 A1→VCC, 0x45 beide→VCC.")

# ---------------------------------------------------------------- Stückliste
nR = 4 * N + 6
rows = [
    ("U1", "ESP32-S3-DevKitC-1 N16R8 (44 Pin)" if S3 else "ESP32 DevKit V1 (WROOM-32, 30 Pin)", "1"),
    ("U2", "MCP4728-Modul (4 × DAC)", "1"),
    ("U3", f"{OPAMP} (DIP) + Sockel", "1"),
    ("U4", "OLED SH1106 1,3\" I²C (optional)", "(1)"),
    (f"U5–U{5 + N}", "DS18B20 TO-92 (oder wasserdicht)", str(N + 1)),
    ("INA", "INA226-Modul, Shunt auf 10 mΩ ändern", str(N)),
    ("TP", "TP4056-Modul ohne Schutzschaltung, 1 A", str(N)),
    ("Q (Last)", "IRLZ44N, isoliert auf Kühlkörper", str(N)),
    ("Q (Lader)", "AO3401 + AO3400", f"{N}+{N}"),
    ("Q (Klemme)", "BSS138", str(N)),
    ("Q (Lüfter, Summer)", "AO3400", "2"),
    ("R (Mess)", "0,1 Ω / 1 W", str(N)),
    ("R", f"10k ×{3 * N + 5} · 7,5k ×{N} · 1k ×{2 * N} · 4,7k ×3", ""),
    ("C", f"470 µF ×1 · 10 nF ×{N}", ""),
    ("F", f"F0 3 A · {N} × 3 A (je Schacht)", str(N + 1)),
    ("J1", f"USB-C-PD-Decoy, auf {VUSB} V gestellt", "1"),
] + ([("U11", "Step-Down Mini560, 5 V / 5 A", "1")] if N == 4 else []) + [
    ("BT", f"18650-Halter", str(N)),
    ("", SUPPLY, "1"),
    ("SW, BZ1, M1", "3 Taster (opt.) · Summer · Lüfter 5 V", ""),
    (f"U{6 + N}", "QR-Scanner GM861S, 3,3 V (optional)", "(1)"),
    ("", "Kühlkörper: " + ("ca. 60 × 40 mm" if N == 1 else "ca. 100 × 60 mm + Lüfter"), ""),
]
y0b = 730
section(740, y0b, 1180, y0b + 36 + len(rows) * 24 + 10, "Stückliste")
y = y0b + 36
for r in rows:
    t(752, y, r[0], 11, PINNUM); t(890, y, r[1], 11, "#222"); t(1168, y, r[2], 11, "#222", "end")
    line(748, y + 5, 1172, y + 5, COMP, .5, 'opacity=".4"'); y += 24
yh = y + 26
section(740, yh, 1180, 1548, "Hinweise")
for k, s_ in enumerate([
    "1. Nur 18650 Li-Ion (4,2 V). Kein LiFePO4 am TP4056!",
    "2. Dicke Linien = Akku- und Laststrom, 1 mm².",
    f"3. Entladung max. {str(IMAX).replace('.', ',')} A je Schacht, zusammen max. 16 W.",
    "4. SENSEk als eigene Leitung direkt vom Zellpol.",
    "5. TP4056 ohne Schutz-IC: Schutz macht die Firmware.",
    "6. Freie Pins (×): IO0/3/45/46 Boot, 35–37 PSRAM." if S3 else "6. Ungenutzte Pins (×) frei; IO0/2/5/12/15 = Boot.",
    "7. Netzlabel mit gleichem Namen sind verbunden.",
    "8. QR-Scanner optional, UART 9600 Baud, 3,3-V-Pegel."] + (["9. IO19/20 = USB, nicht belegen."] if S3 else [])):
    if yh + 40 + k * 21 < 1540:
        t(752, yh + 40 + k * 21, s_, 11, "#222")

# ================================================================ Rechte Spalte: Schächte
for k in range(1, N + 1):
    y0 = 54 + (k - 1) * BLOCK
    section(1200, y0, 2190, y0 + BLOCK - 16, f"Schacht {k}")
    # (a) Zelle und Messung
    cell_v(1250, y0 + 56, y0 + 176, f"BT{k}", ["18650"])
    gnd(1250, y0 + 176)
    pwire((1250, y0 + 56), (1280, y0 + 56))
    fuse_h(1280, 1360, y0 + 56, f"F{k}", "3 A", below=True)
    dot(1250, y0 + 56); wire((1250, y0 + 56), (1250, y0 + 40), (1214, y0 + 40)); label(1214, y0 + 40, f"SENSE{k}", "r")
    ux = 1400
    box(ux, y0 + 40, 130, 196)
    t(ux, y0 + 32, f"U{10 + k}" if False else f"INA{k}", 12, REF); t(ux + 130, y0 + 32, f"INA226 · 0x{[0x40, 0x41, 0x44, 0x45][k - 1]:02X}", 11, REF, "end")
    t(ux + 90, y0 + 138, "Shunt 10 mΩ", 10, PINNAME, "middle", rot=-90)
    pwire((1360, y0 + 56), (ux - 20, y0 + 56))
    pin(ux, y0 + 56, "l", "IN+", 1)
    p = pin(ux, y0 + 90, "l", "VBUS", 2); wire(p, (ux - 74, y0 + 90)); label(ux - 74, y0 + 90, f"SENSE{k}", "r")
    p = pin(ux, y0 + 124, "l", "VCC", 3); wire(p, (ux - 34, y0 + 124)); pwr(ux - 34, y0 + 124, "+3V3", "l")
    p = pin(ux, y0 + 156, "l", "GND", 4); wire(p, (ux - 34, y0 + 156)); gnd(ux - 34, y0 + 156, "l")
    p = pin(ux, y0 + 188, "l", "SDA", 5); wire(p, (ux - 70, y0 + 188)); label(ux - 70, y0 + 188, "SDA", "r")
    p = pin(ux, y0 + 218, "l", "SCL", 6); wire(p, (ux - 70, y0 + 218)); label(ux - 70, y0 + 218, "SCL", "r")
    p = pin(ux + 130, y0 + 56, "r", "IN−", 7); pwire(p, (1600, y0 + 56)); label(1600, y0 + 56, f"CELL{k}", "l")
    # (b) Lader: Q_n schaltet Q_p, Q_p versorgt den TP4056
    qn, qp = 3 * (k - 1) + 1, 3 * (k - 1) + 2
    gp, dp, sp_ = nmos(1640, y0 + 120, f"Q{qp}", "AO3401", side="r", pch=True)
    pwr(dp[0], dp[1], "+5V")
    dot(1620, y0 + 120); wire((1620, y0 + 120), gp)
    res_v(1620, y0 + 60, y0 + 120, f"R{14 + 2 * (k - 1)}", "10 kΩ", "l"); pwr(1620, y0 + 60, "+5V")
    gn, dn, sn = nmos(1640, y0 + 210, f"Q{qn}", "AO3400", side="r")
    wire(dn, (dn[0], y0 + 160), (1620, y0 + 160), (1620, y0 + 120))
    gnd(*sn)
    wire((1606, y0 + 210), gn); label(1606, y0 + 210, f"CHG{k}", "l")
    tx = 1760
    box(tx, y0 + 100, 120, 110)
    t(tx, y0 + 92, f"TP{k}", 12, REF); t(tx + 120, y0 + 92, "TP4056", 11, REF, "end")
    t(tx + 60, y0 + 196, "Rprog 1,2 kΩ → 1 A", 9, PINNAME, "middle")
    p = pin(tx, y0 + 150, "l", "IN+", 1); wire(sp_, (sp_[0], y0 + 150), p)
    p = pin(tx, y0 + 180, "l", "IN−", 2); gnd(p[0], p[1], "l") if False else (wire(p, (tx - 30, y0 + 180)), gnd(tx - 30, y0 + 180))
    p = pin(tx + 120, y0 + 130, "r", "BAT+", 3); pwire(p, (tx + 136, y0 + 130), (tx + 136, y0 + 56), (1680, y0 + 56)); label(1680, y0 + 56, f"CELL{k}", "r")
    p = pin(tx + 120, y0 + 170, "r", "BAT−", 4); gnd(p[0], p[1], "r")
    # (c) Konstantstromlast
    opins = {1: (3, 2, 1), 2: (5, 6, 7), 3: (10, 9, 8), 4: (12, 13, 14)}[k]
    ip, im, out = opamp(1960, y0 + 130, f"U3{'ABCD'[k - 1]}", "", pins=opins)
    wire((1904, ip[1]), ip); t(1906, ip[1] - 4, f"SET{k}", 11, "#000")
    res_h(out[0], 2080, y0 + 130, "", "")
    t(2062, y0 + 112, f"R{22 + 3 * (k - 1)} 1 kΩ", 9, REF, "middle")
    gm, dm, sm = nmos(2080, y0 + 130, f"Q{3 * (k - 1) + 3}", "IRLZ44N")
    pwire(dm, (dm[0], y0 + 40), (2030, y0 + 40)); t(2032, y0 + 36, f"CELL{k}", 12, "#000")
    sx, sy = sm
    pwire(sm, (sx, sy + 26)); dot(sx, sy + 26)
    res_v(sx, sy + 26, sy + 80, f"R{23 + 3 * (k - 1)}", "0,1 Ω 1 W", "l", w=3.4); gnd(sx, sy + 80)
    wire((sx, sy + 26), (2060, sy + 26))
    res_h(1990, 2060, sy + 26, f"R{24 + 3 * (k - 1)}", "10 kΩ")
    wire((1990, sy + 26), (1950, sy + 26), (1950, im[1]), im)
    note(1214, y0 + 258, f"Q{3 * (k - 1) + 3}-Gate: 10 kΩ nach GND + BSS138-Klemme an LOAD_EN_N.")
    note(1730, y0 + 258, f"Q{3 * (k - 1) + 1}-Gate: 10 kΩ nach GND.")

title_block(1616, 1430, f"CellDoc Mini {N}{' · ESP32-S3' if S3 else ''} – Verdrahtung", f"docs/img/schaltplan-mini-{N}{SFX}.svg", "1.0", "2026-10-26", "A2")
t(1214, 1440 if N < 4 else 1440, "Netzlabel mit gleichem Namen", 11, "#555", italic=True)
t(1214, 1456, "sind miteinander verbunden.", 11, "#555", italic=True)
t(1214, 1480, "CELLk = Pluspol Schacht k", 11, "#555", italic=True)
t(1214, 1496, "nach dem Shunt (Messknoten).", 11, "#555", italic=True)
save(OUT, W, H)
