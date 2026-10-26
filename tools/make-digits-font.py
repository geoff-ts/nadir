"""Makes resources/fonts/NadirDigits-Bold.ttf: Chakra Petch Bold's digits, colon
and minus sign squeezed horizontally so a five-character time fits the
panel at full height. Chakra Petch is SIL OFL 1.1 (Cadson Demak); the copy is
renamed because this is a modified version.

    pip install fonttools
    python tools/make-digits-font.py ChakraPetch-Bold.ttf resources/fonts/NadirDigits-Bold.ttf
"""
import sys

from fontTools.pens.transformPen import TransformPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont

SQUEEZE = 0.72
KEEP = "0123456789:-"

source, target = sys.argv[1], sys.argv[2]
font = TTFont(source)
cmap = font.getBestCmap()
glyph_set = font.getGlyphSet()
glyf, hmtx = font["glyf"], font["hmtx"]

for char in KEEP:
    name = cmap[ord(char)]
    pen = TTGlyphPen(glyph_set)
    glyph_set[name].draw(TransformPen(pen, (SQUEEZE, 0, 0, 1, 0, 0)))
    glyf[name] = pen.glyph()
    advance, _ = hmtx[name]
    hmtx[name] = (round(advance * SQUEEZE), 0)
    glyf[name].recalcBounds(glyf)
    hmtx[name] = (hmtx[name][0], glyf[name].xMin if hasattr(glyf[name], "xMin") else 0)

for record in font["name"].names:
    if record.nameID in (1, 4, 16):
        record.string = "Nadir Digits"
    elif record.nameID == 6:
        record.string = "NadirDigits-Bold"
    elif record.nameID in (3,):
        record.string = "NadirDigits-Bold"

font.save(target)
print("wrote", target)
