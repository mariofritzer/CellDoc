#!/bin/bash
# render.sh board.kicad_pcb out.png [layers]
L=${3:-F.Cu,B.Cu,F.SilkS,Edge.Cuts,F.CrtYd}
kicad-cli pcb export svg --layers $L --exclude-drawing-sheet --page-size-mode 2 -o /tmp/r.svg "$1" >/dev/null 2>&1
python3 -c "import cairosvg; cairosvg.svg2png(url='/tmp/r.svg',write_to='$2',output_width=1600,background_color='white')"
