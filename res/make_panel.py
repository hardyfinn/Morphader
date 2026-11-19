#!/usr/bin/env python3
# Draws res/Morphader.svg. Rack can't draw SVG <text>, so labels become paths.
# Positions are in mm; the LAYOUT block numbers are shared with src/Morphader.cpp.
#   pip install fonttools ; python3 res/make_panel.py
import os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

HERE = os.path.dirname(os.path.abspath(__file__))
FONT_BOLD = os.environ.get("PANEL_FONT_BOLD", "/usr/share/fonts/opentype/inter/Inter-Bold.otf")
FONT_MED = os.environ.get("PANEL_FONT_MED", "/usr/share/fonts/opentype/inter/Inter-Medium.otf")

W, H = 101.6, 128.5  # 20 HP

BG = "#22201F"
BG_DARK = "#161514"
LINE = "#3E3A37"
INK = "#EEE8DC"
SOFT = "#A9A39A"
ACOL = "#F2A541"   # source A (amber)
BCOL = "#5FC9D8"   # source B (cyan)
OUTC = "#E9E2D2"

# ---- LAYOUT (same numbers in src/Morphader.cpp) ----
DISP_X, DISP_Y, DISP_W, DISP_H = 6.0, 16.0, 89.6, 17.0
MODE_X, MODE_Y = 17.0, 50.0
FADE_X, FADE_Y = 50.8, 51.0
CHAR_X, CHAR_Y = 84.6, 50.0
COL = [12.7, 31.75, 50.8, 69.85, 88.9]
ROW2_Y = 72.0
CV_Y = 89.0
IN_Y = 104.0
OUT_Y = 117.5

CONTROL = 3.2   # control label size (project standard)
JACK = 2.9      # jack label size (project standard)

fonts = {}


def text(s, x, y, size, font=FONT_BOLD, fill=INK, anchor="middle", spacing=0.04):
    if font not in fonts:
        f = TTFont(font)
        fonts[font] = (f, f.getGlyphSet(), f.getBestCmap(), f["head"].unitsPerEm)
    f, gs, cmap, upm = fonts[font]
    scale = size / upm
    track = spacing * upm
    names = [cmap[ord(c)] for c in s]
    width = sum(gs[n].width + track for n in names) - track
    x0 = {"middle": x - width * scale / 2, "end": x - width * scale}.get(anchor, x)
    pen = SVGPathPen(gs)
    cx = 0.0
    for n in names:
        gs[n].draw(TransformPen(pen, (scale, 0, 0, -scale, x0 + cx * scale, y)))
        cx += gs[n].width + track
    return f'<path fill="{fill}" d="{pen.getCommands()}"/>'


parts = []
add = parts.append
add(f'<rect width="{W}" height="{H}" fill="{BG}"/>')

# title: "Morphader" with a little A->B gradient bar after it
add(text("Morphader", 5.0, 11.4, 5.2, FONT_BOLD, INK, anchor="start", spacing=0.03))
add('<defs><linearGradient id="ab" x1="0" x2="1"><stop offset="0" stop-color="%s"/>'
    '<stop offset="1" stop-color="%s"/></linearGradient></defs>' % (ACOL, BCOL))
add(f'<rect x="40.0" y="8.0" width="10.0" height="2.2" rx="1.1" fill="url(#ab)"/>')
add(text("14 ways from A to B", W - 5.0, 11.0, 2.5, FONT_MED, SOFT, anchor="end"))

# screen frame
add(f'<rect x="{DISP_X - 0.6}" y="{DISP_Y - 0.6}" width="{DISP_W + 1.2}" height="{DISP_H + 1.2}" '
    f'rx="1.0" fill="{BG_DARK}" stroke="{LINE}" stroke-width="0.3"/>')

# big knobs
add(text("MODE", MODE_X, MODE_Y - 10.2, CONTROL))
add(text("FADE", FADE_X, FADE_Y - 12.4, CONTROL))
add(text("CHARACTER", CHAR_X, CHAR_Y - 10.2, CONTROL))
add(text("A", FADE_X - 12.6, FADE_Y + 10.2, CONTROL, fill=ACOL))
add(text("B", FADE_X + 12.6, FADE_Y + 10.2, CONTROL, fill=BCOL))

# second row: glide, CV trims, A/B lights, GO button
add(text("GLIDE", COL[0], ROW2_Y - 6.4, CONTROL))
add(text("FADE CV", COL[1], ROW2_Y - 5.0, CONTROL))
add(text("CHAR CV", COL[3], ROW2_Y - 5.0, CONTROL))
add(text("GO", COL[4], ROW2_Y - 5.6, CONTROL))

# CV inputs on a dark strip
add(f'<rect x="2.6" y="{CV_Y - 9.2}" width="{W - 5.2}" height="14.4" rx="1.4" fill="{BG_DARK}"/>')
for x, n in zip(COL, ["GLIDE", "FADE", "MODE", "CHAR", "GO"]):
    add(text(n, x, CV_Y - 5.6, JACK, FONT_BOLD, SOFT))

# audio inputs: A pair (amber) and B pair (cyan)
for (x0, x1, col, lab) in ((COL[0], COL[1], ACOL, "A"), (COL[3], COL[4], BCOL, "B")):
    add(f'<rect x="{x0 - 8.4}" y="{IN_Y - 9.4}" width="{x1 - x0 + 16.8}" height="14.0" rx="1.4" '
        f'fill="{col}" fill-opacity="0.13"/>')
    add(text(lab + " L", x0, IN_Y - 5.6, JACK, fill=col))
    add(text(lab + " R", x1, IN_Y - 5.6, JACK, fill=col))
# arrow between them
ax = COL[2]
add(f'<path d="M{ax - 6} {IN_Y} L{ax + 5} {IN_Y} M{ax + 2.6} {IN_Y - 2.2} L{ax + 5.2} {IN_Y} L{ax + 2.6} {IN_Y + 2.2}" '
    f'stroke="{SOFT}" stroke-width="0.6" fill="none" stroke-linecap="round" stroke-linejoin="round"/>')
add(text("IN", ax, IN_Y - 5.6, JACK, fill=SOFT))

# outputs, light strip
OX = [40.8, 60.8]
add(f'<rect x="{OX[0] - 8.6}" y="{OUT_Y - 8.6}" width="{OX[1] - OX[0] + 17.2}" height="12.4" rx="1.4" '
    f'fill="{OUTC}" fill-opacity="0.12"/>')
add(text("OUT L", OX[0], OUT_Y - 5.6, JACK))
add(text("OUT R", OX[1], OUT_Y - 5.6, JACK))

svg = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="0 0 {W} {H}">\n'
       + "\n".join(p for p in parts if p) + "\n</svg>\n")
open(os.path.join(HERE, "Morphader.svg"), "w").write(svg)
print("ok")

# --- Haer maker logo (res/haer_logo.py) on the bottom rail
import sys as _sys
_sys.path.insert(0, HERE)
import haer_logo
haer_logo.add_to_file(os.path.join(HERE, "Morphader.svg"), x=W / 2, y=126.0, cap=3.0, bg=BG)
