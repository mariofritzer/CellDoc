# Aufruf über build.sh
# Routet die Platine mit Freerouting und legt GND-Flächen an
import re, subprocess, sys
import pcbnew as P
src, dst = sys.argv[1], sys.argv[2]
mm = P.FromMM
b = P.LoadBoard(src)
dsn = "/tmp/nano.dsn"; ses = "/tmp/nano.ses"
P.ExportSpecctraDSN(b, dsn)
d = open(dsn).read()
POWER = ["BAT+", "BAT_F", "CELL", "LOAD_D", "TP_IN"]
SUPPLY = ["GND", "+5V"]
def cls(name, netsl, w, clr):
    q = " ".join('"%s"' % n if not re.match(r"^[A-Za-z0-9_]+$", n) else n for n in netsl)
    return '    (class %s %s\n      (circuit (use_via Via[0-1]_600:300_um))\n      (rule (width %d) (clearance %d))\n    )\n' % (name, q, w, clr)
extra = cls("power", POWER, 1500, 300) + cls("supply", SUPPLY, 800, 250)
# Netze aus der Default-Klasse entfernen
m = re.search(r"\(class kicad_default(.*?)\(circuit", d, re.S)
body = m.group(1)
for n in POWER + SUPPLY:
    body = re.sub(r'(?<![\w+])"?%s"?(?![\w+])' % re.escape(n), " ", body, count=1)
d = d[:m.start(1)] + body + d[m.end(1):]
i = d.rfind("  )\n  (wiring")
d = d[:i] + extra + d[i:]
d = d.replace("(width 200)\n        (clearance 200.1)", "(width 300)\n        (clearance 250)")
open(dsn, "w").write(d)
r = subprocess.run(["java", "-jar", "fr.jar", "-de", dsn, "-do", ses, "-mp", "30", "--gui.enabled=false"],
                   capture_output=True, text=True, timeout=900)
print(r.stdout[-1500:], r.stderr[-1500:])
