"""Makes resources/fonts/NadirClock-Regular.ttf: Bruno Ace with every letter's
advance cut by 5% of the em, so the clock's digits sit closer together and the
time can be drawn larger in the same width. Bruno Ace is SIL OFL 1.1 with no
Reserved Font Name; the copy is renamed anyway because it is a modified
version.

    pip install fonttools
    python tools/make-clock-font.py resources/fonts/BrunoAce-Regular.ttf resources/fonts/NadirClock-Regular.ttf
"""
import sys

from fontTools.ttLib import TTFont

TIGHTEN = 0.05  # of the em, taken off each advance

source, target = sys.argv[1], sys.argv[2]
font = TTFont(source)
cut = round(font["head"].unitsPerEm * TIGHTEN)
hmtx = font["hmtx"]
for name, (advance, lsb) in hmtx.metrics.items():
    if advance:
        hmtx.metrics[name] = (max(0, advance - cut), lsb)

names = font["name"]
for record in names.names:
    if record.nameID in (1, 16):
        record.string = "Nadir Clock"
    elif record.nameID == 3:
        record.string = "Nadir Clock Regular; derived from Bruno Ace 1.100"
    elif record.nameID in (4,):
        record.string = "Nadir Clock Regular"
    elif record.nameID == 6:
        record.string = "NadirClock-Regular"
font.save(target)
print("cut %d units from each advance" % cut)
