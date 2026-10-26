# CellDoc – 3D-Szene des Aufbaus für die Dokumentation
# Aufruf: python3 tools/render/scene.py <ansicht> <ausgabe.png> [samples] [breite] [höhe]
#   ansicht: iso | top
# Maße in mm, 1 Blender-Einheit = 1 cm
import bpy, math, sys, json
from mathutils import Vector

S = 0.1  # mm -> BU
VIEW = sys.argv[1] if len(sys.argv) > 1 else "iso"
OUT = sys.argv[2] if len(sys.argv) > 2 else "/tmp/celldoc.png"
SAMPLES = int(sys.argv[3]) if len(sys.argv) > 3 else 64
RES_X = int(sys.argv[4]) if len(sys.argv) > 4 else 1600
RES_Y = int(sys.argv[5]) if len(sys.argv) > 5 else 1100

bpy.ops.wm.read_factory_settings(use_empty=True)
scene = bpy.context.scene
ANCHORS = {}  # Beschriftungspunkte für den Anschlussplan

# ---------------------------------------------------------------- Materialien
MATS = {}
def mat(name, rgb, metal=0.0, rough=0.5, coat=0.0, emit=None):
    if name in MATS:
        return MATS[name]
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*rgb, 1)
    b.inputs["Metallic"].default_value = metal
    b.inputs["Roughness"].default_value = rough
    if coat:
        b.inputs["Coat Weight"].default_value = coat
    if emit:
        b.inputs["Emission Color"].default_value = (*emit, 1)
        b.inputs["Emission Strength"].default_value = 3
    MATS[name] = m
    return m

def srgb(h):
    h = h.lstrip("#")
    c = [int(h[i:i + 2], 16) / 255 for i in (0, 2, 4)]
    return tuple(((x + 0.055) / 1.055) ** 2.4 if x > 0.04045 else x / 12.92 for x in c)

M = {
    "base": mat("base", srgb("#2b2f36"), rough=0.6),
    "fr4": None,
    "blue": mat("pcb_blue", srgb("#1f4fa8"), rough=0.35, coat=0.4),
    "purple": mat("pcb_purple", srgb("#5b2a86"), rough=0.35, coat=0.4),
    "black_pcb": mat("pcb_black", srgb("#18191c"), rough=0.35, coat=0.4),
    "red_pcb": mat("pcb_red", srgb("#a81f24"), rough=0.35, coat=0.4),
    "chip": mat("chip", srgb("#111214"), rough=0.45),
    "silver": mat("silver", srgb("#c8ccd2"), metal=1, rough=0.25),
    "alu": mat("alu", srgb("#c9cdd3"), metal=1, rough=0.35),
    "gold": mat("gold", srgb("#d9b25a"), metal=1, rough=0.25),
    "copper": mat("copper", srgb("#c47a46"), metal=1, rough=0.3),
    "plastic_black": mat("plastic_black", srgb("#1b1c1f"), rough=0.55),
    "plastic_white": mat("plastic_white", srgb("#ecebe6"), rough=0.5),
    "yellow": mat("xt60", srgb("#e7b416"), rough=0.4),
    "relay_blue": mat("relay_blue", srgb("#2a6fdb"), rough=0.4),
    "terminal": mat("terminal", srgb("#2f8f4e"), rough=0.5),
    "cell": mat("cell", srgb("#2b74c7"), rough=0.3, coat=0.6),
    "cell_ring": mat("cell_ring", srgb("#f2f2f2"), rough=0.4),
    "ceramic": mat("ceramic", srgb("#f1efe8"), rough=0.7),
    "fuse_red": mat("fuse_red", srgb("#d8332f"), rough=0.35),
    "fuse_blue": mat("fuse_blue", srgb("#2f6fd8"), rough=0.35),
    "fuse_brown": mat("fuse_brown", srgb("#8a5a2b"), rough=0.35),
    "silk": mat("silk", srgb("#f4f4f2"), rough=0.6),
    "brass": mat("brass", srgb("#b9973f"), metal=1, rough=0.35),
    "led_green": mat("led_green", srgb("#38e07b"), emit=srgb("#38e07b")),
    "led_red": mat("led_red", srgb("#ff4a3d"), emit=srgb("#ff4a3d")),
    "fan": mat("fan", srgb("#202226"), rough=0.5),
    "elko": mat("elko", srgb("#1d3d8f"), rough=0.35),
    "inductor": mat("inductor", srgb("#2a2a2a"), rough=0.6),
    "sensor": mat("sensor", srgb("#9aa0a8"), metal=1, rough=0.3),
    "kapton": mat("kapton", srgb("#d99a2b"), rough=0.4),
}

# Kabelfarben (Legende im Anschlussplan)
WIRES = {
    "12 V": "#e03131",
    "5 V": "#ff8787",
    "Masse": "#25262b",
    "Akku / Last": "#f08c00",
    "Ladestrom": "#2f9e44",
    "I²C SDA": "#1c7ed6",
    "I²C SCL": "#f2c200",
    "1-Wire (DS18B20)": "#12b886",
    "Steuersignale": "#9c36b5",
    "Balancer": "#adb5bd",
}
WM = {k: mat("wire_" + k, srgb(v), rough=0.45, coat=0.3) for k, v in WIRES.items()}

# Lochraster-Textur
def perfboard_material():
    n = 64
    img = bpy.data.images.new("holes", n, n)
    px = []
    base = srgb("#2e8a48")
    cu = srgb("#c98a55")
    for y in range(n):
        for x in range(n):
            d = math.hypot(x - n / 2 + 0.5, y - n / 2 + 0.5) / (n / 2)
            if d < 0.22:
                c = (0.03, 0.03, 0.03)
            elif d < 0.42:
                c = cu
            else:
                c = base
            px += [*c, 1]
    img.pixels = px
    m = bpy.data.materials.new("perfboard")
    m.use_nodes = True
    nt = m.node_tree
    b = nt.nodes["Principled BSDF"]
    b.inputs["Roughness"].default_value = 0.45
    b.inputs["Coat Weight"].default_value = 0.3
    tc = nt.nodes.new("ShaderNodeTexCoord")
    mp = nt.nodes.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (1 / (2.54 * S), 1 / (2.54 * S), 1)
    tx = nt.nodes.new("ShaderNodeTexImage")
    tx.image = img
    tx.interpolation = "Closest"
    nt.links.new(tc.outputs["Object"], mp.inputs["Vector"])
    nt.links.new(mp.outputs["Vector"], tx.inputs["Vector"])
    nt.links.new(tx.outputs["Color"], b.inputs["Base Color"])
    return m
M["fr4"] = perfboard_material()

# ---------------------------------------------------------------- Grundformen
def box(name, x, y, z, w, d, h, m, bevel=0.0, rot=0.0):
    """x,y = Mitte, z = Unterkante, w/d/h in mm"""
    bpy.ops.mesh.primitive_cube_add(location=(x * S, y * S, (z + h / 2) * S))
    o = bpy.context.object
    o.name = name
    o.scale = (w * S / 2, d * S / 2, h * S / 2)
    o.rotation_euler[2] = math.radians(rot)
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    if bevel:
        bm = o.modifiers.new("bevel", "BEVEL")
        bm.width = bevel * S
        bm.segments = 3
        bm.limit_method = "ANGLE"
    o.data.materials.append(m)
    for p in o.data.polygons:
        p.use_smooth = False
    return o

def cyl(name, x, y, z, r, h, m, axis="Z", verts=32, bevel=0.0):
    bpy.ops.mesh.primitive_cylinder_add(vertices=verts, radius=r * S, depth=h * S)
    o = bpy.context.object
    o.name = name
    if axis == "X":
        o.rotation_euler = (0, math.radians(90), 0)
        o.location = ((x + h / 2) * S, y * S, z * S)
    elif axis == "Y":
        o.rotation_euler = (math.radians(90), 0, 0)
        o.location = (x * S, (y + h / 2) * S, z * S)
    else:
        o.location = (x * S, y * S, (z + h / 2) * S)
    if bevel:
        bm = o.modifiers.new("bevel", "BEVEL")
        bm.width = bevel * S
        bm.segments = 2
    o.data.materials.append(m)
    bpy.ops.object.shade_smooth()
    return o

def text(s, x, y, z, size, m=None, rot=0.0, align="CENTER", extrude=0.05):
    cu = bpy.data.curves.new("txt", "FONT")
    cu.body = s
    cu.size = size * S
    cu.align_x = align
    cu.align_y = "CENTER"
    cu.extrude = extrude * S
    o = bpy.data.objects.new("txt_" + s[:12], cu)
    o.location = (x * S, y * S, z * S)
    o.rotation_euler[2] = math.radians(rot)
    bpy.context.collection.objects.link(o)
    o.data.materials.append(m or M["silk"])
    return o

def pins(x0, y0, z, n, dx, dy, m=None, h=6.0):
    for k in range(n):
        box("pin", x0 + k * dx, y0 + k * dy, z, 0.64, 0.64, h, m or M["gold"])

def header_strip(x0, y0, z, n, horizontal=True):
    """Stiftleiste mit schwarzem Kunststoffträger"""
    L = n * 2.54
    if horizontal:
        box("hdr", x0 + L / 2 - 1.27, y0, z, L, 2.5, 2.5, M["plastic_black"])
        pins(x0, y0, z + 1, n, 2.54, 0, h=6)
    else:
        box("hdr", x0, y0 + L / 2 - 1.27, z, 2.5, L, 2.5, M["plastic_black"])
        pins(x0, y0, z + 1, n, 0, 2.54, h=6)

def standoffs(x, y, w, d, z0, h):
    for sx in (-1, 1):
        for sy in (-1, 1):
            cyl("standoff", x + sx * (w / 2 - 3), y + sy * (d / 2 - 3), z0, 2.2, h, M["brass"], verts=6)

def smd(x, y, z, w=2.0, d=1.25, h=0.6, m=None):
    box("smd", x, y, z, w, d, h, m or M["chip"])
    box("smd_e", x - w / 2 + 0.2, y, z, 0.4, d, h + 0.02, M["silver"])
    box("smd_e", x + w / 2 - 0.2, y, z, 0.4, d, h + 0.02, M["silver"])

# ---------------------------------------------------------------- Kabel
def wire(kind, pts, r=0.7):
    """pts in mm (x, y, z); Bezier mit automatischen Griffen"""
    cu = bpy.data.curves.new("wire", "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = r * S
    cu.bevel_resolution = 4
    cu.use_fill_caps = True
    sp = cu.splines.new("BEZIER")
    sp.bezier_points.add(len(pts) - 1)
    for bp, p in zip(sp.bezier_points, pts):
        bp.co = Vector((p[0] * S, p[1] * S, p[2] * S))
        bp.handle_left_type = bp.handle_right_type = "AUTO"
    o = bpy.data.objects.new("wire_" + kind, cu)
    bpy.context.collection.objects.link(o)
    o.data.materials.append(WM[kind])
    return o

def arc(kind, a, b, h=14.0, r=0.7, mid=None):
    """Kabel von a nach b, steigt senkrecht auf und fällt senkrecht ab"""
    ax, ay, az = a
    bx, by, bz = b
    top = max(az, bz) + h
    pts = [a, (ax, ay, az + h * 0.45)]
    if mid:
        pts += [(mx, my, top) for (mx, my) in mid]
    else:
        pts += [((ax * 2 + bx) / 3, (ay * 2 + by) / 3, top), ((ax + bx * 2) / 3, (ay + by * 2) / 3, top)]
    pts += [(bx, by, bz + h * 0.45), b]
    return wire(kind, pts, r)

# ---------------------------------------------------------------- Module
BASE_Z = 5.0     # Oberkante Grundplatte
STAND = 8.0      # Abstandsbolzen
PB_Z = BASE_Z + STAND  # Unterkante Lochraster

# Grundplatte
box("Grundplatte", 172, 122, 0, 348, 248, BASE_Z, M["base"], bevel=2)
for (cx, cy) in [(6, 6), (338, 6), (6, 238), (338, 238)]:
    cyl("fuss", cx, cy, -3, 5, 3, M["plastic_black"])

# Lochraster
PB = dict(x=110, y=92, w=190, d=136)
box("Lochraster", PB["x"], PB["y"], PB_Z, PB["w"], PB["d"], 1.6, M["fr4"])
standoffs(PB["x"], PB["y"], PB["w"], PB["d"], BASE_Z, STAND)
PZ = PB_Z + 1.6  # Oberseite Lochraster

def module(name, x, y, w, d, pcb, label, sub="", raise_h=8.5, label_rot=0):
    """Modul auf Buchsenleisten über dem Lochraster"""
    z = PZ + raise_h
    box(name + "_buchse1", x, y - d / 2 + 2, PZ, w * 0.7, 2.5, raise_h, M["plastic_black"])
    box(name, x, y, z, w, d, 1.6, pcb, bevel=0.4)
    t = text(label, x, y + d * 0.18, z + 1.6, 3.2 if w > 22 else 2.6, rot=label_rot)
    if sub:
        text(sub, x, y - d * 0.05, z + 1.6, 2.0, rot=label_rot)
    return z + 1.6

# --- ESP32 DevKit
EX, EY = 48, 118
ez = PZ + 8.5
box("esp_buchse_l", EX, EY - 11.4, PZ, 38.1, 2.5, 8.5, M["plastic_black"])
box("esp_buchse_r", EX, EY + 11.4, PZ, 38.1, 2.5, 8.5, M["plastic_black"])
box("ESP32", EX, EY, ez, 51, 28, 1.6, M["black_pcb"], bevel=0.4)
et = ez + 1.6
box("wroom_pcb", EX + 13, EY, et, 25.5, 18, 0.8, M["black_pcb"])
box("wroom_can", EX + 15, EY, et + 0.8, 17.6, 15.6, 2.4, M["silver"], bevel=0.3)
text("ESP32-WROOM-32", EX + 15, EY, et + 3.25, 1.5, M["chip"], rot=90, extrude=0.01)
box("usb", EX - 24, EY, et, 6, 7.5, 2.8, M["silver"], bevel=0.3)
box("btn1", EX - 18, EY - 8, et, 4, 3, 1.6, M["silver"])
box("btn2", EX - 18, EY + 8, et, 4, 3, 1.6, M["silver"])
box("btn1c", EX - 18, EY - 8, et + 1.6, 2, 1.4, 0.8, M["plastic_black"])
box("btn2c", EX - 18, EY + 8, et + 1.6, 2, 1.4, 0.8, M["plastic_black"])
box("ldo", EX - 8, EY - 5, et, 6.5, 3.5, 1.6, M["chip"])
box("led", EX - 10, EY + 6, et, 1.6, 0.8, 0.5, M["led_red"])
text("ESP32 DevKit", EX - 6, EY + 2, et, 2.4)
ANCHORS["ESP32 DevKit"] = (EX, EY, et + 4)

# Steckerleisten des ESP32 (Lötaugen)
def pin_x(k):  # k = 0..14 entlang der Leiste
    return EX - 17.8 + k * 2.54
ESP_L = lambda k: (pin_x(k), EY - 11.4, ez + 1.6)
ESP_R = lambda k: (pin_x(k), EY + 11.4, ez + 1.6)

# --- INA226 A / B
def ina(name, x, y, label):
    z = module(name, x, y, 20, 26, M["purple"], "INA226", label)
    box(name + "_ic", x, y + 4, z, 3, 3, 1, M["chip"])
    box(name + "_shunt", x - 4, y - 6, z, 6.3, 3.2, 0.8, M["chip"])
    box(name + "_sh1", x - 7, y - 6, z, 0.9, 3.2, 0.82, M["silver"])
    box(name + "_sh2", x - 1, y - 6, z, 0.9, 3.2, 0.82, M["silver"])
    box(name + "_term", x + 4.5, y - 8, z, 8, 7, 7, M["terminal"], bevel=0.3)
    for k in (0, 1):
        cyl(name + "_screw", x + 2.6 + k * 3.8, y - 8, z + 7, 1.3, 0.4, M["silver"])
    header = [(x - 7.6 + k * 2.54, y + 11, z) for k in range(6)]
    for p in header:
        box("hp", p[0], p[1], p[2], 0.7, 0.7, 6, M["gold"])
    ANCHORS[f"INA226 {label[-1]}"] = (x, y, z + 6)
    return z, header, (x + 2.6, y - 8, z + 7), (x + 6.4, y - 8, z + 7)

inaA_z, inaA_h, inaA_in, inaA_out = ina("INA_A", 98, 132, "Anschluss A")
inaB_z, inaB_h, inaB_in, inaB_out = ina("INA_B", 124, 132, "Anschluss B")

# --- MCP4725
mz = module("MCP4725", 150, 136, 18, 16, M["purple"], "MCP4725", "DAC")
box("mcp_ic", 150, 132, mz, 3, 1.6, 0.9, M["chip"])
mcp_h = [(150 - 5 + k * 2.54, 142.5, mz) for k in range(4)]
ANCHORS["MCP4725"] = (150, 136, mz + 6)

# --- ADS1115 ×2
def ads(name, x, y, sub):
    z = module(name, x, y, 28, 17, M["blue"], "ADS1115", sub)
    box(name + "_ic", x + 7, y, z, 3, 3, 1, M["chip"])
    hp = [(x - 11.4 + k * 2.54, y - 6.5, z) for k in range(10)]
    for p in hp:
        box("hp", p[0], p[1], p[2], 0.7, 0.7, 6, M["gold"])
    return z, hp
ads1_z, ads1_h = ads("ADS1", 102, 96, "Abgriff 1–4")
ads2_z, ads2_h = ads("ADS2", 136, 96, "Abgriff 5–6")
ANCHORS["ADS1115 ×2"] = (119, 96, ads1_z + 6)

# --- Lastregler: LM358, BSS138, Widerstände
def dip8(x, y):
    box("dip_sockel", x, y, PZ, 10.5, 9.5, 3, M["plastic_black"])
    box("LM358", x, y, PZ + 3, 9.4, 6.4, 3.3, M["chip"], bevel=0.3)
    text("LM358", x, y, PZ + 6.31, 1.6, M["silk"], extrude=0.01)
    for k in range(4):
        for s in (-1, 1):
            box("dip_pin", x - 3.81 + k * 2.54, y + s * 3.6, PZ + 2.5, 0.5, 1.2, 1.5, M["silver"])
dip8(150, 70)
ANCHORS["Lastregler (LM358)"] = (150, 70, PZ + 8)
def resistor(x, y, rot=0, body=(0.85, 0.75, 0.55)):
    rm = mat("res_body", body, rough=0.5)
    cyl("res", x - 3, y, PZ + 1.6, 1.1, 6, rm, axis="X") if rot == 0 else cyl("res", x, y - 3, PZ + 1.6, 1.1, 6, rm, axis="Y")
    for k, c in enumerate(["#8b4513", "#000000", "#ff8c00"]):
        if rot == 0:
            cyl("band", x - 1.8 + k * 1.2, y, PZ + 1.6, 1.15, 0.4, mat("band" + c, srgb(c)), axis="X")
for k in range(4):
    resistor(132 + k * 0, 62 + k * 5)
for k in range(3):
    resistor(168, 60 + k * 6)
for k in range(3):
    smd(178 + k * 5, 78, PZ, 3, 1.6, 1.1)  # BSS138 (SOT-23 als Platzhalter)
for k in range(4):
    smd(178 + k * 5, 70, PZ, 3, 1.6, 1.1)  # AO3400
cyl("elko1", 132, 82, PZ, 2.5, 7, M["elko"])
cyl("elko1_top", 132, 82, PZ + 7, 2.3, 0.2, M["silver"])

# --- Summer
cyl("Summer", 188, 40, PZ, 6, 9, M["plastic_black"])
cyl("summer_loch", 188, 40, PZ + 9, 1, 0.1, M["chip"])
ANCHORS["Summer"] = (188, 40, PZ + 10)

# --- Pull-ups und Teiler-Feld für den Balancer (Widerstandsreihe)
for k in range(6):
    box("teiler", 100 + k * 4, 78, PZ, 1.6, 6, 1.6, mat("res_blue", srgb("#4f8fd8"), rough=0.5))
    box("teiler", 100 + k * 4, 70, PZ, 1.6, 6, 1.6, mat("res_blue", srgb("#4f8fd8"), rough=0.5))
ANCHORS["Balancer-Teiler"] = (110, 74, PZ + 4)

# --- Schraubklemmen am Lochraster-Rand (Leistungsanschlüsse)
def terminal(name, x, y, n, rot=0):
    box(name, x, y, PZ, 5 * n, 7.5, 9, M["terminal"], bevel=0.4, rot=rot)
    pts = []
    for k in range(n):
        px = x - 5 * n / 2 + 2.5 + k * 5
        cyl(name + "_s", px, y, PZ + 9, 1.4, 0.4, M["silver"])
        pts.append((px, y, PZ + 9.4))
    return pts
T_PWR = terminal("Klemme_Versorgung", 30, 160 - 6, 3)  # 12V, 5V, GND
T_LOAD = terminal("Klemme_Last", 196, 120, 2)        # Last+, GND

# =============================================================== Außerhalb des Lochrasters
# --- USB-C-PD-Decoy (auf 12 V gestellt) + F0
dz = BASE_Z + 3
for sx in (-1, 1):
    for sy in (-1, 1):
        cyl("decoy_steh", 16 + sx * 7, 214 + sy * 12, BASE_Z, 1.6, 3, M["brass"], verts=6)
box("USB-C-Decoy", 16, 214, dz, 20, 31, 1.2, M["purple"], bevel=0.3)
box("usbc", 8.5, 214, dz + 1.2, 7.4, 9.0, 3.2, M["silver"], bevel=1.1)
box("usbc_loch", 4.9, 214, dz + 1.95, 0.4, 7.4, 1.7, M["plastic_black"], bevel=0.5)
box("dip", 21, 214, dz + 1.2, 6, 8.6, 2.6, M["fuse_red"], bevel=0.2)
for k in range(3):
    box("dip_sw", 21 + (0.9 if k < 2 else -0.9), 211.2 + k * 2.8, dz + 3.8, 2.0, 1.4, 0.8, M["silk"])
box("decoy_ic", 14, 205, dz + 1.2, 3, 3, 0.8, M["chip"])
ANCHORS["USB-C-Decoy 12 V"] = (16, 214, dz + 6)
def fuse(name, x, y, color, amps, rot=0):
    box(name + "_halter", x, y, BASE_Z, 22, 9, 7, M["plastic_black"], bevel=0.6, rot=rot)
    box(name, x, y, BASE_Z + 7, 13, 4, 9, color, bevel=0.8, rot=rot)
    text(amps, x, y, BASE_Z + 16.05, 3.0, M["silk"], rot=rot, extrude=0.02)
    return x, y
fuse("F0", 38, 214, M["fuse_red"], "3")
ANCHORS["F0 3 A"] = (38, 214, BASE_Z + 18)

# --- Step-Down LM2596
BX, BY = 78, 210
box("Buck", BX, BY, BASE_Z + STAND, 43, 21, 1.6, M["blue"], bevel=0.4)
standoffs(BX, BY, 43, 21, BASE_Z, STAND)
bz = BASE_Z + STAND + 1.6
box("buck_ic", BX - 2, BY - 4, bz, 10, 9, 4.5, M["chip"])
box("buck_tab", BX - 2, BY - 9, bz, 10, 2, 1.2, M["silver"])
cyl("buck_spule", BX + 8, BY + 3, bz, 6, 7, M["inductor"])
cyl("buck_spule_cu", BX + 8, BY + 3, bz + 7, 4, 0.3, M["copper"])
cyl("buck_elko1", BX - 15, BY + 4, bz, 4, 11, M["elko"])
cyl("buck_elko2", BX + 17, BY - 4, bz, 4, 11, M["elko"])
box("buck_poti", BX - 12, BY - 6, bz, 9.5, 4.5, 9, mat("poti", srgb("#2f62c4"), rough=0.4))
cyl("buck_poti_s", BX - 16.5, BY - 6, bz + 7, 1.2, 1.5, M["brass"], axis="X")
text("LM2596  12 V → 5 V", BX, BY - 9.2, bz, 2.0)
BUCK_IN = (BX - 19, BY + 8, bz)
BUCK_OUT = (BX + 19, BY + 8, bz)
ANCHORS["Step-Down 5 V"] = (BX, BY, bz + 12)

# --- Relaismodul 2-fach
RX, RY = 145, 210
box("Relais", RX, RY, BASE_Z + STAND, 51, 39, 1.6, M["blue"], bevel=0.4)
standoffs(RX, RY, 51, 39, BASE_Z, STAND)
rz = BASE_Z + STAND + 1.6
for k, lab in enumerate(["K1", "K2"]):
    rx = RX - 11 + k * 22
    box("relais_" + lab, rx, RY + 3, rz, 15.5, 19, 15.5, M["relay_blue"], bevel=0.5)
    text("SRD-05VDC", rx, RY + 3, rz + 15.55, 1.6, M["silk"], rot=90, extrude=0.01)
    text(lab, rx, RY - 12, rz, 3.0)
    box("relais_klemme", rx, RY + 17, rz, 15, 7.5, 9, M["terminal"], bevel=0.3)
    for j in range(3):
        cyl("rk_s", rx - 5 + j * 5, RY + 17, rz + 9, 1.4, 0.4, M["silver"])
    box("relais_led", rx + 6, RY - 9, rz, 1.6, 0.8, 0.5, M["led_green"] if k == 0 else M["chip"])
hp_rel = [(RX - 18 + k * 2.54, RY - 16, rz) for k in range(4)]
for p in hp_rel:
    box("hp", p[0], p[1], p[2], 0.7, 0.7, 6, M["gold"])
K1_TERM = [(RX - 16 + j * 5, RY + 17, rz + 9.4) for j in range(3)]  # NO, COM, NC
K2_TERM = [(RX + 6 + j * 5, RY + 17, rz + 9.4) for j in range(3)]
ANCHORS["Relais K1 / K2"] = (RX, RY, rz + 18)

# --- TP5000
TX, TY = 196, 205
box("TP5000", TX, TY, BASE_Z + STAND, 26, 18, 1.6, M["blue"], bevel=0.4)
standoffs(TX, TY, 26, 18, BASE_Z, STAND)
tz = BASE_Z + STAND + 1.6
box("tp_ic", TX - 2, TY, tz, 4.9, 3.9, 1.5, M["chip"])
box("tp_spule", TX + 6, TY + 2, tz, 6, 6, 3, M["inductor"], bevel=0.5)
box("tp_rs", TX - 8, TY - 5, tz, 3.2, 1.6, 0.6, M["chip"])
box("tp_led", TX - 9, TY + 5, tz, 1.6, 0.8, 0.5, M["led_red"])
text("TP5000", TX + 2, TY - 6, tz, 2.4)
TP_IN = (TX - 12, TY + 6, tz)
TP_OUT = (TX + 12, TY + 6, tz)
ANCHORS["Lader TP5000"] = (TX, TY, tz + 6)

# --- 18650-Halter mit Zelle (Anschluss A)
HX, HY = 262, 30
box("Halter", HX, HY, BASE_Z, 78, 21, 2, M["plastic_black"], bevel=0.5)
box("halter_w1", HX, HY - 10, BASE_Z + 2, 78, 1.5, 12, M["plastic_black"])
box("halter_w2", HX, HY + 10, BASE_Z + 2, 78, 1.5, 12, M["plastic_black"])
box("halter_e1", HX - 38, HY, BASE_Z + 2, 2, 21, 17, M["plastic_black"])
box("halter_e2", HX + 38, HY, BASE_Z + 2, 2, 21, 17, M["plastic_black"])
cyl("Zelle", HX - 32.5, HY, BASE_Z + 2 + 9.2, 9.2, 64, M["cell"], axis="X", verts=48)
cyl("zelle_plus", HX + 31.5, HY, BASE_Z + 11.2, 3.5, 1.5, M["silver"], axis="X")
cyl("zelle_ring", HX + 30.5, HY, BASE_Z + 11.2, 8.6, 1.0, M["cell_ring"], axis="X", verts=48)
text("18650", HX - 8, HY - 5, BASE_Z + 20.3, 3.0, M["silk"], extrude=0.01)
# Kaptonband mit Fühler T1
box("kapton", HX - 8, HY + 3, BASE_Z + 19.8, 10, 12, 0.3, M["kapton"])
cyl("T1", HX - 14, HY + 3, BASE_Z + 20.4, 2.6, 14, M["sensor"], axis="X")
CELL_PLUS = (HX + 41, HY, BASE_Z + 6)
CELL_MINUS = (HX - 41, HY, BASE_Z + 6)
ANCHORS["Anschluss A · 18650"] = (HX, HY, BASE_Z + 24)
ANCHORS["T1 Zelle"] = (HX - 7, HY + 4, BASE_Z + 24)
# Lötpads/Schraubklemme parallel
box("Klemme_A", 332, 55, BASE_Z, 10, 8, 10, M["terminal"], bevel=0.4)
for k in (0, 1):
    cyl("ka_s", 329.5 + k * 5, 55, BASE_Z + 10, 1.4, 0.4, M["silver"])
fuse("F1", 300, 70, M["fuse_brown"], "7,5")
ANCHORS["F1 7,5 A"] = (300, 70, BASE_Z + 18)

# --- CPU-Kühler mit Lüfter und MOSFETs
CX, CY = 275, 178
cz = BASE_Z + 2
box("kuehler_basis", CX, CY, cz, 90, 90, 8, M["alu"], bevel=0.6)
for k in range(15):
    box("lamelle", CX - 42 + k * 6, CY, cz + 8, 1.4, 86, 42, M["alu"])
fz = cz + 50
box("luefter_rahmen", CX, CY, fz, 88, 88, 25, M["fan"], bevel=2.5)
cyl("luefter_loch", CX, CY, fz + 24.9, 40, 0.2, mat("fan_dark", (0.008, 0.008, 0.01), rough=0.8), verts=64)
cyl("luefter_nabe", CX, CY, fz + 10, 14, 15.3, M["fan"], verts=48)
# (Aufdruck am Lüfter weggelassen)
for k in range(7):
    a = k * 2 * math.pi / 7
    bpy.ops.mesh.primitive_cube_add(location=((CX + math.cos(a) * 27) * S, (CY + math.sin(a) * 27) * S, (fz + 21) * S))
    bl = bpy.context.object
    bl.scale = (12 * S, 5 * S, 0.6 * S)
    bl.rotation_euler = (math.radians(18), 0, a + math.radians(90))
    bl.data.materials.append(M["fan"])
for (sx, sy) in [(-38, -38), (38, -38), (-38, 38), (38, 38)]:
    cyl("schraube", CX + sx, CY + sy, fz + 25, 2, 1, M["silver"], verts=12)
ANCHORS["Kühler + Lüfter"] = (CX, CY, fz + 30)
ANCHORS["T3 Kühlkörper"] = (CX - 46, CY + 30, cz + 6)
cyl("T3", CX - 49, CY + 30, cz + 4, 2.6, 6, M["sensor"])
# MOSFETs vorne an der Basis (TO-247), Beine Richtung Lochraster
MOS = []
for k in range(2):
    mx = CX - 18 + k * 36
    my = CY - 46.5
    box("IRFP250N_tab", mx, my - 0.5, cz + 2, 16, 1.2, 21, M["copper"])
    box("IRFP250N", mx, my - 3, cz + 2, 15.6, 5, 20, M["chip"], bevel=0.4)
    text("IRFP250N", mx, my - 5.55, cz + 14, 1.6, M["silk"], rot=0, extrude=0.01).rotation_euler = (math.radians(90), 0, 0)
    for j in range(3):
        box("mos_bein", mx - 5.45 + j * 5.45, my - 3, cz - 2, 1.2, 0.6, 4.5, M["silver"])
    MOS.append([(mx - 5.45 + j * 5.45, my - 3, cz + 0.3) for j in range(3)])  # G, D, S
    cyl("mos_schraube", mx, my - 5.8, cz + 17, 2.3, 1, M["silver"], axis="Y", verts=12)
ANCHORS["MOSFETs 2× IRFP250N"] = (CX, CY - 52, cz + 24)
# Messwiderstände 0,1 Ω / 5 W
RS = []
for k in range(2):
    rx = 238 + k * 28
    box("Rsense", rx, 112, BASE_Z, 22, 10, 9, M["ceramic"], bevel=0.6)
    text("5W 0R1", rx, 112, BASE_Z + 9.05, 2.2, mat("print_red", srgb("#b01e1e")), extrude=0.01)
    for s in (-1, 1):
        box("rs_bein", rx + s * 13, 112, BASE_Z, 4, 0.8, 4, M["silver"])
    RS.append(((rx - 13, 112, BASE_Z + 3), (rx + 13, 112, BASE_Z + 3)))
ANCHORS["Messwiderstände 0,1 Ω"] = (252, 112, BASE_Z + 14)

# --- Anschluss B: XT60, F2, Balancer
box("XT60", 325, 112, BASE_Z, 16, 8, 16, M["yellow"], bevel=1.0)
text("XT60", 325, 107.9, BASE_Z + 8, 2.5, M["chip"], extrude=0.02).rotation_euler = (math.radians(90), 0, 0)
XT_P = (325 - 3.5, 116, BASE_Z + 16)
XT_M = (325 + 3.5, 116, BASE_Z + 16)
ANCHORS["Anschluss B · XT60"] = (325, 112, BASE_Z + 20)
fuse("F2", 300, 90, M["fuse_blue"], "10")
ANCHORS["F2 10 A"] = (300, 90, BASE_Z + 18)
box("balancer_pcb", 330, 30, BASE_Z, 22, 20, 1.6, M["red_pcb"], bevel=0.3)
for k, n in enumerate([3, 4, 5, 6, 7]):
    pass
box("jst_xh", 330, 30, BASE_Z + 1.6, 18, 6, 7, M["plastic_white"], bevel=0.3)
text("2S–6S", 330, 22, BASE_Z + 1.6, 2.0)
BAL = (324, 33, BASE_Z + 8.6)
ANCHORS["Balancer JST-XH"] = (330, 30, BASE_Z + 12)
cyl("T2", 323, 132, BASE_Z, 2.6, 14, M["sensor"], axis="X")
ANCHORS["T2 Pack"] = (330, 132, BASE_Z + 6)

# Verpolschutzdioden (axial, schwarz mit Ring)
for (dx, dy) in [(312, 58), (312, 80)]:
    cyl("diode", dx, dy, BASE_Z + 3, 2.6, 9, M["chip"], axis="X")
    cyl("diode_ring", dx + 7, dy, BASE_Z + 3, 2.65, 1.2, M["silver"], axis="X")

# ---------------------------------------------------------------- Verkabelung
# 12 V Versorgung
arc("12 V", (18, 210, BASE_Z + 8), (30, 214, BASE_Z + 7), h=5, r=0.9)
arc("12 V", (46, 214, BASE_Z + 7), BUCK_IN, h=6, r=0.9)
arc("12 V", (BX - 19, BY + 4, bz), T_PWR[0], h=10, r=0.9, mid=[(45, 190)])
arc("Masse", (14, 220, BASE_Z + 8), (BX - 19, BY - 2, bz), h=6, r=0.9, mid=[(30, 228), (50, 228)])
# 5 V vom Step-Down
arc("5 V", BUCK_OUT, T_PWR[1], h=12, r=0.8, mid=[(90, 186), (45, 172)])
arc("5 V", (BX + 19, BY + 4, bz), hp_rel[0], h=8, r=0.6)
arc("5 V", (BX + 19, BY + 2, bz), TP_IN, h=8, r=0.6, mid=[(120, 236), (175, 234)])
arc("Masse", (BX + 19, BY - 2, bz), T_PWR[2], h=10, r=0.8, mid=[(80, 182), (42, 172)])
# Lüfter
arc("12 V", T_PWR[0], (CX - 45, CY + 38, fz + 12), h=14, r=0.6, mid=[(60, 175), (200, 170)])
arc("Steuersignale", ESP_L(1), (CX - 45, CY + 34, fz + 12), h=14, r=0.45, mid=[(60, 168), (200, 165)])
# ESP32-Versorgung
arc("5 V", T_PWR[1], ESP_R(0), h=8, r=0.55)
arc("Masse", T_PWR[2], ESP_R(1), h=6, r=0.55)
# I²C-Bus
sda = [ESP_R(5), inaA_h[4], inaB_h[4], mcp_h[2]]
scl = [ESP_R(7), inaA_h[5], inaB_h[5], mcp_h[3]]
for a, b in zip(sda, sda[1:]):
    arc("I²C SDA", a, b, h=6, r=0.42)
for a, b in zip(scl, scl[1:]):
    arc("I²C SCL", a, b, h=7, r=0.42)
arc("I²C SDA", ESP_R(5), ads1_h[3], h=8, r=0.42)
arc("I²C SCL", ESP_R(7), ads1_h[2], h=9, r=0.42)
arc("I²C SDA", ads1_h[3], ads2_h[3], h=5, r=0.42)
arc("I²C SCL", ads1_h[2], ads2_h[2], h=6, r=0.42)
# Relais- und Lader-Steuerung
arc("Steuersignale", ESP_R(10), hp_rel[2], h=12, r=0.45, mid=[(80, 160), (120, 180)])
arc("Steuersignale", ESP_R(11), hp_rel[3], h=13, r=0.45, mid=[(84, 162), (124, 182)])
arc("Steuersignale", ESP_L(10), (TX - 12, TY - 6, tz), h=14, r=0.45, mid=[(70, 100), (170, 165)])
arc("Steuersignale", ESP_L(12), (150 - 3.8, 66, PZ + 6), h=8, r=0.45)
arc("Steuersignale", mcp_h[0], (150 + 3.8, 74, PZ + 6), h=8, r=0.45)
# 1-Wire zu den Fühlern
arc("1-Wire (DS18B20)", ESP_L(13), (HX - 21, HY + 3, BASE_Z + 20.4), h=12, r=0.5, mid=[(90, 60), (200, 40)])
arc("1-Wire (DS18B20)", ESP_L(13), (CX - 49, CY + 30, cz + 10), h=14, r=0.5, mid=[(120, 155), (215, 175)])
arc("1-Wire (DS18B20)", (CX - 49, CY + 30, cz + 10), (316, 132, BASE_Z + 2.6), h=10, r=0.5, mid=[(222, 140), (300, 128)])
# Anschluss A: Zelle + -> F1 -> INA226 A -> K2 NC ; Zelle - -> Masse
arc("Akku / Last", CELL_PLUS, (311, 70, BASE_Z + 7), h=6, r=1.2)
arc("Akku / Last", (289, 70, BASE_Z + 7), inaA_in, h=16, r=1.2, mid=[(220, 75), (120, 110)])
arc("Akku / Last", inaA_out, K2_TERM[2], h=18, r=1.2, mid=[(140, 175)])
arc("Masse", CELL_MINUS, T_LOAD[1], h=8, r=1.2, mid=[(212, 60)])
# Lader: TP5000 -> K1 -> Knoten A
arc("Ladestrom", TP_OUT, K1_TERM[0], h=8, r=0.9, mid=[(180, 234), (140, 234)])
arc("Ladestrom", K1_TERM[1], inaA_out, h=16, r=0.9, mid=[(118, 180)])
# Anschluss B: XT60 + -> F2 -> INA226 B -> K2 NO ; XT60 - -> Masse
arc("Akku / Last", XT_P, (311, 90, BASE_Z + 7), h=6, r=1.2)
arc("Akku / Last", (289, 90, BASE_Z + 7), inaB_in, h=18, r=1.2, mid=[(225, 95), (150, 112)])
arc("Akku / Last", inaB_out, K2_TERM[0], h=20, r=1.2, mid=[(138, 175)])
arc("Masse", XT_M, T_LOAD[1], h=12, r=1.2, mid=[(300, 104), (220, 104)])
# K2 COM -> Drains der MOSFETs
arc("Akku / Last", K2_TERM[1], T_LOAD[0], h=14, r=1.2, mid=[(185, 175)])
arc("Akku / Last", T_LOAD[0], MOS[0][1], h=8, r=1.0)
arc("Akku / Last", T_LOAD[0], MOS[1][1], h=10, r=1.0)
# Source -> Messwiderstand -> Masse
arc("Akku / Last", MOS[0][2], RS[0][1], h=5, r=1.0)
arc("Akku / Last", MOS[1][2], RS[1][1], h=5, r=1.0)
arc("Masse", RS[0][0], T_LOAD[1], h=6, r=1.0)
arc("Masse", RS[1][0], RS[0][0], h=4, r=1.0)
# Gates vom Regler
arc("Steuersignale", (154, 66, PZ + 6), MOS[0][0], h=10, r=0.45, mid=[(205, 95)])
arc("Steuersignale", (154, 74, PZ + 6), MOS[1][0], h=11, r=0.45, mid=[(210, 100)])
# Balancer -> Teiler -> ADS1115
for k in range(4):
    arc("Balancer", (BAL[0] - 3 + k * 2.5, BAL[1], BAL[2]), (100 + k * 4, 78, PZ + 1.6), h=8 + k, r=0.35, mid=[(300, 14 + k * 2), (150, 18 + k * 2)])
arc("Balancer", (100, 70, PZ + 1.6), ads1_h[6], h=5, r=0.35)
arc("Balancer", (112, 70, PZ + 1.6), ads2_h[6], h=6, r=0.35)

# ---------------------------------------------------------------- Licht, Boden, Kamera
bpy.ops.mesh.primitive_plane_add(size=600 * S, location=(172 * S, 122 * S, -3 * S))
floor = bpy.context.object
floor.is_shadow_catcher = True

def area(name, loc, rot, size, energy, color=(1, 1, 1)):
    l = bpy.data.lights.new(name, "AREA")
    l.size = size
    l.energy = energy
    l.color = color
    o = bpy.data.objects.new(name, l)
    o.location = loc
    o.rotation_euler = rot
    bpy.context.collection.objects.link(o)
area("key", (5, -20, 40), (math.radians(35), 0, math.radians(-15)), 30, 26000)
area("fill", (60, 50, 30), (math.radians(-40), math.radians(30), 0), 30, 9000, (0.9, 0.95, 1))
area("top", (17, 12, 60), (0, 0, 0), 40, 9000)

world = bpy.data.worlds.new("w")
scene.world = world
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (0.8, 0.82, 0.86, 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.6

cam_data = bpy.data.cameras.new("cam")
cam_data.type = "ORTHO"
cam = bpy.data.objects.new("cam", cam_data)
bpy.context.collection.objects.link(cam)
scene.camera = cam
center = Vector((172 * S, 122 * S, 30 * S))
if VIEW == "top":
    cam.location = (center.x, center.y, 100)
    cam.rotation_euler = (0, 0, 0)
    cam_data.ortho_scale = 37.5
else:
    # Isometrisch von vorne links oben (wie beim Lexion)
    d = Vector((-1, -1.25, 1.05)).normalized() * 120
    cam.location = center + d
    direction = center - cam.location
    cam.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()
    cam_data.ortho_scale = 44
cam_data.clip_end = 500

scene.render.engine = "CYCLES"
scene.cycles.device = "CPU"
scene.cycles.samples = SAMPLES
scene.cycles.use_denoising = True
scene.render.resolution_x = RES_X
scene.render.resolution_y = RES_Y
scene.render.film_transparent = True
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "AgX"
scene.view_settings.look = "AgX - Medium High Contrast"
scene.render.filepath = OUT
bpy.ops.render.render(write_still=True)

# Beschriftungspunkte in Bildkoordinaten exportieren
from bpy_extras.object_utils import world_to_camera_view
pts = {}
for k, (x, y, z) in ANCHORS.items():
    v = world_to_camera_view(scene, cam, Vector((x * S, y * S, z * S)))
    pts[k] = (v.x * RES_X, (1 - v.y) * RES_Y)
json.dump(pts, open(OUT.rsplit(".", 1)[0] + "_anchors.json", "w"), ensure_ascii=False, indent=1)
print("fertig", OUT)
