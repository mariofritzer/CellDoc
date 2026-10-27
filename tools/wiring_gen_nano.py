# Erzeugt den Schaltplan der CellDoc Nano (ESP32-C3 SuperMini, ein Schacht)
# Aufruf: python3 tools/wiring_gen_nano.py docs/img/schaltplan-nano.svg
import sys

from schematic import *  # noqa: F401,F403
from schematic import o

OUT = sys.argv[1] if len(sys.argv) > 1 else "schaltplan-nano.svg"
W, H = 1960, 1240
frame(W, H)

# ================================================================ Linke Spalte
# ---------------------------------------------------------------- Stromversorgung
section(50, 54, 690, 250, "Stromversorgung")
t(90, 96, "USB-C-Buchse des ESP32-C3 SuperMini", 11, REF, "start", "bold")
t(90, 114, "5-V-Netzteil ab 10 W (5 V / 2 A)", 11, REF)
wire((300, 150), (520, 150)); pwr(300, 150, "+5V", "l")
wire((360, 196), (520, 196)); gnd(360, 196)
dot(420, 150); dot(420, 196); cap_v(420, 150, 196, "C1", "100 µF / 10 V")
note(66, 236, "+5V = 5V-Pin des C3 (direkt an dessen USB-C). Versorgt TP4056.")

# ---------------------------------------------------------------- Lader
section(50, 266, 690, 640, "Laden: TP4056 über AO3401 schaltbar")
# Q2 (P-Kanal) zwischen +5V und TP4056 IN+, Q1 (N-Kanal) zieht sein Gate auf GND
gp, dp, sp_ = nmos(240, 400, "Q2", "AO3401", side="r", pch=True)
pwr(dp[0], dp[1], "+5V")
dot(220, 400); wire((220, 400), gp)
res_v(220, 320, 400, "R1", "10 kΩ", "l"); pwr(220, 320, "+5V")
gn, dn, sn = nmos(240, 520, "Q1", "AO3400", side="r")
wire(dn, (dn[0], 466), (220, 466), (220, 400))
gnd(*sn)
wire((140, 520), gn); label(140, 520, "CHG", "r")
dot(180, 520); res_v(180, 520, 600, "R2", "10 kΩ", "l"); gnd(180, 600)
# TP4056-Modul
tx = 420
box(tx, 360, 150, 150)
t(tx, 352, "U3", 12, REF); t(tx + 150, 352, "TP4056 USB-C", 11, REF, "end")
t(tx + 75, 430, "Laden 1 A", 10, PINNAME, "middle")
t(tx + 75, 446, "4,2 V", 10, PINNAME, "middle")
p = pin(tx, 430, "l", "IN+", 1); wire(sp_, p)
p = pin(tx, 480, "l", "IN−", 2); wire(p, (tx - 40, 480)); gnd(tx - 40, 480)
p = pin(tx + 150, 400, "r", "B+", 3); pwire(p, (640, 400)); label(640, 400, "CELL", "l")
p = pin(tx + 150, 480, "r", "B−", 4); wire(p, (610, 480)); gnd(610, 480)
for k, s_ in enumerate([
        "USB-C-Buchse am TP4056 bleibt frei.",
        "Modul mit Schutzschaltung: OUT+/OUT− frei lassen.",
        "B− ist intern mit IN− verbunden (GND).",
        "CHG HIGH: Q1 leitet, Q2 versorgt den TP4056."]):
    note(300, 562 + k * 18, s_)

# ---------------------------------------------------------------- Temperatur
section(50, 656, 690, 900, "Temperatur (DS18B20, TO-92 oder wasserdicht)")
wire((110, 720), (400, 720)); pwr(110, 720, "+3V3", "l")
wire((110, 770), (400, 770)); label(110, 770, "ONEWIRE", "r")
res_v(400, 720, 770, "R4", "4,7 kΩ", "r")
dot(400, 720); dot(400, 770)
for k, (ref, where, dash) in enumerate([("U4", "Zelle", False), ("U5", "Lastwiderstand (opt.)", True)]):
    bx = 170 + k * 120
    box(bx, 790, 60, 64, dash)
    t(bx + 30, 818, "DS18B20", 9, PINNAME, "middle", "bold")
    t(bx + 5, 848, ref, 11, REF)
    wire((bx + 12, 790), (bx + 12, 720)); dot(bx + 12, 720)
    wire((bx + 32, 790), (bx + 32, 770)); dot(bx + 32, 770)
    line(bx + 50, 854, bx + 50, 862); gnd(bx + 50, 862)
    t(bx + 30, 892, where, 10, REF, "middle")

# ---------------------------------------------------------------- Optional
section(50, 916, 690, 1190, "Optional: Summer, OLED, QR-Scanner")
box(90, 980, 60, 44)
o.append(f'<path d="M108 992 h12 l10 -8 v36 l-10 -8 h-12 z" fill="none" stroke="{COMP}" stroke-width="1.3"/>')
t(90, 972, "BZ1", 12, REF); t(120, 1042, "aktiv, 5 V", 10, REF, "middle")
p = pin(90, 1002, "l", None, None, L=20); pwr(p[0], p[1], "+5V", "u")
p = pin(150, 1002, "r", None, None, L=40)
g, dr, sr = nmos(180, 1100, "Q4", "AO3400")
wire(p, (dr[0], 1002), dr); gnd(*sr)
wire((100, 1100), g); label(100, 1100, "BUZZER", "r")
# OLED
box(330, 966, 110, 100)
o.append(f'<rect x="345" y="980" width="56" height="34" fill="#e9e6da" stroke="{COMP}" stroke-width="1"/>')
t(330, 958, "U6", 12, REF); t(330, 1084, "SH1106 1,3\" · 0x3C", 10, REF)
for k, (n_, net) in enumerate([("VCC", "+3V3"), ("GND", "GND"), ("SCL", "SCL"), ("SDA", "SDA")]):
    yy = 984 + k * 22
    p = pin(440, yy, "r", n_, k + 1)
    if net == "+3V3": wire(p, (480, yy)); pwr(480, yy, "+3V3", "r")
    elif net == "GND": wire(p, (480, yy)); gnd(480, yy, "r")
    else: wire(p, (520, yy)); label(520, yy, net, "l")
# QR
box(330, 1104, 110, 70)
t(385, 1146, "QR", 13, PINNAME, "middle", "bold")
t(330, 1096, "U7", 12, REF)
for k, (n_, net) in enumerate([("TX", "SCAN_RX"), ("RX", "SCAN_TX")]):
    yy = 1124 + k * 30
    p = pin(440, yy, "r", n_, k + 3)
    wire(p, (520, yy)); label(520, yy, net, "l")
t(450, 1186, "GM861S · VCC an +3V3, GND", 10, REF)

# ================================================================ Mitte: ESP32-C3
EX, EY, EW = 860, 96, 200
nets = {3: "LOAD_PWM", 4: "ONEWIRE", 5: "SDA", 6: "SCL", 7: "CHG", 10: "BUZZER", 20: "SCAN_RX", 21: "SCAN_TX"}
EH = mcu_block("c3", EX, EY, nets, EW)
note(EX - 110, EY + EH + 30, "IO8 = blaue LED (Status), IO9 = BOOT-Taste:")
note(EX - 110, EY + EH + 46, "kurz = Test starten/bestätigen, lang = stoppen.")
note(EX - 110, EY + EH + 62, "I²C: SDA = IO5, SCL = IO6, je 4,7 kΩ nach +3V3.")
note(EX - 110, EY + EH + 78, "IO0–2 frei. IO2/8/9 sind Boot-Pins.")

# ---------------------------------------------------------------- Stückliste
rows = [
    ("U1", "ESP32-C3 SuperMini (USB-C, 4 MB)", "1"),
    ("U2", "INA226-Modul, Shunt R100 → R010 tauschen", "1"),
    ("U3", "TP4056-Modul USB-C (mit/ohne Schutz)", "1"),
    ("U4, U5", "DS18B20 (U5 optional)", "1–2"),
    ("RL", "Lastwiderstand 3 Ω, 25–50 W, Alugehäuse", "1"),
    ("Q1, Q3", "AO3400 (N-Kanal, SOT-23)", "2"),
    ("Q2", "AO3401 (P-Kanal, SOT-23)", "1"),
    ("R", "10k ×3 · 4,7k ×3 · 1k ×1", ""),
    ("C1", "100 µF / 10 V", "1"),
    ("F1", "Sicherung 3 A mit Halter", "1"),
    ("BT1", "18650-Halter", "1"),
    ("", "USB-C-Netzteil 5 V / 2 A", "1"),
    ("opt.", "Summer + AO3400 · OLED · GM861S", ""),
]
y0b = 560
section(740, y0b, 1180, y0b + 36 + len(rows) * 24 + 10, "Stückliste")
y = y0b + 36
for r in rows:
    t(752, y, r[0], 11, PINNUM); t(830, y, r[1], 11, "#222"); t(1168, y, r[2], 11, "#222", "end")
    line(748, y + 5, 1172, y + 5, COMP, .5, 'opacity=".4"'); y += 24
yh = y + 26
section(740, yh, 1180, 1190, "Hinweise")
for k, s_ in enumerate([
    "1. Nur 18650 Li-Ion (4,2 V).",
    "2. Dicke Linien = Zellstrom, mind. 0,5 mm².",
    "3. Entladen per PWM 10 kHz, bis 1,2 A.",
    "4. SENSE eigene Leitung vom Zellpol.",
    "5. RL auf Metall schrauben, wird warm.",
    "6. Netzlabel gleichen Namens = verbunden."]):
    if yh + 40 + k * 20 < 1186:
        t(752, yh + 40 + k * 20, s_, 11, "#222")

# ================================================================ Rechts: Zelle, Messung, Last
section(1200, 54, 1910, 640, "Zelle, Messung und Last")
cell_v(1250, 140, 290, "BT1", ["18650"])
gnd(1250, 290)
pwire((1250, 140), (1290, 140))
fuse_h(1290, 1370, 140, "F1", "3 A", below=True)
dot(1250, 140); wire((1250, 140), (1250, 110), (1222, 110)); label(1222, 110, "SENSE", "r")
ux = 1430
box(ux, 120, 130, 196)
t(ux, 112, "U2", 12, REF); t(ux + 130, 112, "INA226 · 0x40", 11, REF, "end")
t(ux + 90, 218, "Shunt 10 mΩ", 10, PINNAME, "middle", rot=-90)
pwire((1370, 140), (ux - 20, 140))
pin(ux, 140, "l", "IN+", 1)
p = pin(ux, 172, "l", "VBUS", 2); wire(p, (ux - 70, 172)); label(ux - 70, 172, "SENSE", "r")
p = pin(ux, 204, "l", "VCC", 3); wire(p, (ux - 34, 204)); pwr(ux - 34, 204, "+3V3", "l")
p = pin(ux, 236, "l", "GND", 4); wire(p, (ux - 34, 236)); gnd(ux - 34, 236, "l")
p = pin(ux, 268, "l", "SDA", 5); wire(p, (ux - 70, 268)); label(ux - 70, 268, "SDA", "r")
p = pin(ux, 298, "l", "SCL", 6); wire(p, (ux - 70, 298)); label(ux - 70, 298, "SCL", "r")
p = pin(ux + 130, 140, "r", "IN−", 7); pwire(p, (1700, 140)); dot(1660, 140)
wire((1660, 140), (1660, 104)); label(1660, 104, "CELL", "r")
# Last: RL von CELL nach Drain Q3
pwire((1700, 140), (1800, 140), (1800, 190))
res_v(1800, 190, 290, "RL", "3 Ω / 50 W", "r", w=3.4)
gl, dl, sl = nmos(1756, 360, "Q3", "AO3400", side="r")
pwire((1800, 290), dl)
pwire(sl, (sl[0], 430)); gnd(sl[0], 430)
dot(1730, 360); wire((1730, 360), gl)
res_h(1640, 1730, 360, "R3", "1 kΩ")
wire((1600, 360), (1640, 360)); label(1600, 360, "LOAD_PWM", "l")
res_v(1730, 360, 440, "R5", "10 kΩ", "l"); gnd(1730, 440)
for k, s_ in enumerate([
        "Q3 schaltet RL mit 10 kHz. Der INA226 mittelt über",
        "viele Perioden, die Firmware regelt so auf Konstantstrom.",
        "R3 (1 kΩ) macht die Flanken weich: der drahtgewickelte",
        "Lastwiderstand erzeugt sonst Spannungsspitzen an Q3.",
        "R5 hält Q3 gesperrt, solange der C3 bootet.",
        "INA226: R100-Shunt gegen R010 tauschen, sonst Messfehler!",
        "TP4056 B+ und RL hängen am selben Knoten CELL (nach dem Shunt)."]):
    note(1214, 496 + k * 18, s_)

# Pinbelegung
t(1214, 770, "Pinbelegung ESP32-C3 SuperMini", 12, COMP, "start", "bold")
for k, (io, fn) in enumerate([
        ("IO3", "LOAD_PWM: Last (Q3), HIGH = an"), ("IO4", "1-Wire DS18B20"), ("IO5 / IO6", "I²C SDA / SCL"),
        ("IO7", "CHG: Lader an (Q1/Q2)"), ("IO8", "blaue LED auf dem Board"), ("IO9", "BOOT-Taste = Bedientaste"),
        ("IO10", "Summer (optional)"), ("IO20 / IO21", "QR-Scanner TX / RX (optional)")]):
    yy = 796 + k * 22
    t(1214, yy, io, 11, PINNUM); t(1330, yy, fn, 11, "#222")
    line(1210, yy + 6, 1640, yy + 6, COMP, .5, 'opacity=".4"')

title_block(1336, 1074, "CellDoc Nano – Verdrahtung", "docs/img/schaltplan-nano.svg", "1.0", "2026-10-27", "A3")
t(1214, 680, "Netzlabel mit gleichem Namen sind verbunden.", 11, "#555", italic=True)
t(1214, 698, "CELL = Pluspol der Zelle nach dem Shunt (Messknoten).", 11, "#555", italic=True)
t(1214, 716, "SENSE = Pluspol direkt an der Zelle (Spannungsmessung).", 11, "#555", italic=True)
save(OUT, W, H)
