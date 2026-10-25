# Packt web/index.html gzip-komprimiert als C-Array in src/web_index.h
# und setzt die Versionsnummer. Läuft automatisch vor jedem PlatformIO-Build.
import gzip
import os
import subprocess

try:
    Import("env")  # noqa: F821  (von PlatformIO bereitgestellt)
except NameError:
    env = None

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__))) if env is None else env["PROJECT_DIR"]
SRC = os.path.join(ROOT, "web", "index.html")
DST = os.path.join(ROOT, "src", "web_index.h")


def version():
    v = os.environ.get("CELLDOC_VERSION", "").strip()
    if v:
        return v
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"], cwd=ROOT, stderr=subprocess.DEVNULL
        ).decode().strip()
    except Exception:
        return "dev"


def inline_scripts(html):
    # <script src="vendor/…"></script> wird in die Seite eingebettet, damit CellDoc ohne Internet auskommt
    import re

    def rep(m):
        path = os.path.join(ROOT, "web", m.group(1))
        with open(path, "rb") as f:
            return b"<script>" + f.read() + b"</script>"

    return re.sub(rb'<script src="(vendor/[A-Za-z0-9_.-]+)"></script>', rep, html)


def main():
    with open(SRC, "rb") as f:
        raw = inline_scripts(f.read())
    data = gzip.compress(raw, compresslevel=9, mtime=0)
    lines = []
    for k in range(0, len(data), 20):
        lines.append("  " + ",".join("0x%02x" % b for b in data[k : k + 20]) + ",")
    out = (
        "// Automatisch erzeugt aus web/index.html – nicht von Hand bearbeiten\n"
        "#pragma once\n#include <Arduino.h>\n"
        "static const size_t INDEX_GZ_LEN = %d;\n"
        "static const uint8_t INDEX_GZ[] PROGMEM = {\n%s\n};\n" % (len(data), "\n".join(lines))
    )
    old = open(DST).read() if os.path.exists(DST) else ""
    if old != out:
        with open(DST, "w") as f:
            f.write(out)
    print("Weboberfläche: %d Bytes -> %d Bytes gzip" % (len(raw), len(data)))


main()
if env is not None:
    v = version()
    env.Append(CPPDEFINES=[("CELLDOC_VERSION", env.StringifyMacro(v))])
    print("Version:", v)
