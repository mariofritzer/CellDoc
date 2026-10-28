# Aufruf über build.sh
# Liest eine Freerouting-.ses ein, legt GND-Flächen an und speichert
import re, sys
import pcbnew as P
src, ses, dst = sys.argv[1:4]
mm = P.FromMM
b = P.LoadBoard(src)
s = open(ses).read()
res = float(re.search(r"\(routes\s*\(resolution um (\d+)\)", s).group(1))
k = 1000.0 / res  # Einheit -> nm (1 um = 1000 nm)
def pt(x, y): return P.VECTOR2I(int(float(x) * k), int(-float(y) * k))
LAY = {"F.Cu": P.F_Cu, "B.Cu": P.B_Cu}
routes = s[s.index("(network_out"):]
nw = nv = 0
chunks = re.split(r'\n\s*\(net\s+', routes)[1:]
for ch in chunks:
    mm_ = re.match(r'("[^"]*"|\S+)', ch)
    name = mm_.group(1).strip('"'); body = ch[mm_.end():]
    n = b.FindNet(name)
    for w in re.finditer(r"\(path\s+(\S+)\s+(\d+)\s+([-\d\s]+)\)", body):
        layer, width, nums = w.group(1), int(w.group(2)), w.group(3).split()
        xy = [pt(nums[i], nums[i + 1]) for i in range(0, len(nums), 2)]
        for a, c in zip(xy, xy[1:]):
            t = P.PCB_TRACK(b); t.SetStart(a); t.SetEnd(c); t.SetWidth(int(width * k))
            t.SetLayer(LAY[layer]); t.SetNet(n); b.Add(t); nw += 1
    for v in re.finditer(r"\(via\s+\S+\s+(-?\d+)\s+(-?\d+)", body):
        via = P.PCB_VIA(b); via.SetPosition(pt(v.group(1), v.group(2)))
        via.SetWidth(mm(0.6)); via.SetDrill(mm(0.3)); via.SetNet(n)
        via.SetLayerPair(P.F_Cu, P.B_Cu); b.Add(via); nv += 1
# lose Stummel von Freerouting entfernen
pads = list(b.GetPads())
def connected(pt, layer, me):
    for t in b.GetTracks():
        if t.m_Uuid.AsString() == me.m_Uuid.AsString(): continue
        if t.Type() == P.PCB_VIA_T:
            if t.GetPosition() == pt: return True
        elif t.GetLayer() == layer and (t.GetStart() == pt or t.GetEnd() == pt):
            return True
    for p in pads:
        if p.IsOnLayer(layer) and p.HitTest(pt): return True
    return False
seen = set()
for t in list(b.GetTracks()):
    if t.Type() == P.PCB_VIA_T: continue
    a, c = (t.GetStart().x, t.GetStart().y), (t.GetEnd().x, t.GetEnd().y)
    key = (t.GetLayer(),) + tuple(sorted([a, c]))
    if key in seen or a == c: b.Remove(t)
    else: seen.add(key)
removed = 1
while removed:
    removed = 0
    for t in list(b.GetTracks()):
        if t.Type() == P.PCB_VIA_T: continue
        if not connected(t.GetStart(), t.GetLayer(), t) or not connected(t.GetEnd(), t.GetLayer(), t):
            b.Remove(t); removed += 1
    print("Stummel entfernt", removed)
gnd = b.FindNet("GND")
for layer in (P.F_Cu, P.B_Cu):
    z = P.ZONE(b)
    z.SetLayer(layer); z.SetNet(gnd)
    z.SetLocalClearance(mm(0.4)); z.SetMinThickness(mm(0.3))
    z.SetPadConnection(P.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(mm(0.4)); z.SetThermalReliefSpokeWidth(mm(0.6))
    z.SetAssignedPriority(0)
    o = z.Outline(); o.NewOutline()
    bb = b.GetBoardEdgesBoundingBox()
    X0, Y0, X1, Y1 = bb.GetX() + mm(0.3), bb.GetY() + mm(0.3), bb.GetRight() - mm(0.3), bb.GetBottom() - mm(0.3)
    for x, y in [(X0, Y0), (X1, Y0), (X1, Y1), (X0, Y1)]:
        o.Append(int(x), int(y))
    b.Add(z)
P.ZONE_FILLER(b).Fill(b.Zones())
b.Save(dst)
print("Segmente", nw, "Vias", nv)
