#!/bin/bash
# Erzeugt hardware/nano-pcb/CellDoc-Nano.kicad_pcb neu (KiCad 7+ Python, Java 21, Freerouting)
# Freerouting: https://github.com/freerouting/freerouting/releases (freerouting-2.1.0.jar als fr.jar hierher)
set -e
cd "$(dirname "$0")"
OUT=../../hardware/nano-pcb
python3 gen_pcb.py /tmp/nano-unrouted.kicad_pcb $OUT/CellDoc.pretty
python3 route.py /tmp/nano-unrouted.kicad_pcb /dev/null > /tmp/fr.log 2>&1 || true
python3 import_ses.py /tmp/nano-unrouted.kicad_pcb /tmp/nano.ses $OUT/CellDoc-Nano.kicad_pcb
python3 drc.py $OUT/CellDoc-Nano.kicad_pcb
