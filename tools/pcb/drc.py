# Aufruf über build.sh
import sys, re, collections
import pcbnew as P
b = P.LoadBoard(sys.argv[1])
P.WriteDRCReport(b, "/tmp/drc.txt", P.EDA_UNITS_MILLIMETRES, True)
t = open("/tmp/drc.txt").read()
print("\n".join(l for l in t.splitlines() if l.startswith("**")))
c = collections.Counter(re.findall(r"^\[(\w+)\]", t, re.M))
print(dict(c))
