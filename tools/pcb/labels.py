# Aufruf über build.sh: druckt Werte von R, Q, C auf den Bestückungsdruck
import sys
import pcbnew as P
f = sys.argv[1]
b = P.LoadBoard(f)
mm = P.FromMM
for fp in b.GetFootprints():
    ref = fp.GetReference()
    if ref == "F1":
        c = fp.GetPosition()
        fp.Reference().SetPosition(P.VECTOR2I(c.x, c.y - mm(2.5)))
    if not (ref[0] in "RQC" and ref[1:].isdigit()):
        continue
    v = fp.Value()
    v.SetLayer(P.F_SilkS)
    v.SetVisible(True)
    v.SetTextSize(P.VECTOR2I(mm(0.8), mm(0.8)))
    v.SetTextThickness(mm(0.13))
    r = fp.Reference()
    pads = list(fp.Pads())
    if ref[0] == "R":
        a, c = pads[0].GetPosition(), pads[1].GetPosition()
        cx, cy = (a.x + c.x) // 2, (a.y + c.y) // 2
        vert = abs(a.x - c.x) < abs(a.y - c.y)
        val = v.GetText().replace(" opt.", "")
        r.SetVisible(False); v.SetVisible(False)
        t = P.FP_TEXT(fp)
        t.SetText(ref + " " + val)
        t.SetLayer(P.F_SilkS)
        t.SetTextSize(P.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.13))
        t.SetPosition(P.VECTOR2I(cx, cy))
        fp.Add(t)
        t.SetTextAngleDegrees(((90 if vert else 0) - fp.GetOrientationDegrees()) % 360)
    else:
        p = fp.GetPosition()
        r.SetVisible(False); v.SetVisible(False)
        t = P.FP_TEXT(fp)
        t.SetText(ref + " " + v.GetText().replace(" opt.", "").replace("uF 10V", "µF"))
        t.SetLayer(P.F_SilkS)
        t.SetTextSize(P.VECTOR2I(mm(0.8), mm(0.8))); t.SetTextThickness(mm(0.13))
        if ref == "C1":
            t.SetPosition(P.VECTOR2I(p.x + mm(1.25), p.y + mm(4.6)))
        else:
            dy = {"Q1": 2.6, "Q4": -2.25}.get(ref, -2.6)
            dx = 1.4 if ref == "Q3" else 0
            t.SetPosition(P.VECTOR2I(p.x + mm(dx), p.y + mm(dy)))
        fp.Add(t)
        t.SetTextAngleDegrees((0 - fp.GetOrientationDegrees()) % 360)
    if v.GetText() == "3R 50W":
        v.SetVisible(False)
b.Save(f)
print("Werte gedruckt")
