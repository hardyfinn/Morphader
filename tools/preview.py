#!/usr/bin/env python3
# Picture of the panel with Rack's own component graphics and a sample screen.
#   python3 tools/preview.py out.png [path/to/Rack/res/ComponentLibrary]
import os, sys, re, base64, subprocess
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "preview.png")
LIB = sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "..", "Rack-src", "res", "ComponentLibrary")
svg = open(os.path.join(HERE, "..", "res", "Morphader.svg")).read()
L = {}
src = open(os.path.join(HERE, "..", "res", "make_panel.py")).read()
exec(src.split("# ---- LAYOUT")[1].split("CONTROL =")[0].split("\n", 1)[1], L)
W = 101.6
PX = 25.4 / 75.0
g = []

def comp(name, x, y, angle=0.0):
    s = open(os.path.join(LIB, name + ".svg")).read()
    m = re.search(r'width="([0-9.]+)(px|mm)?"', s); n = re.search(r'height="([0-9.]+)(px|mm)?"', s)
    w = float(m.group(1)) * (1 if m.group(2) == "mm" else PX); h = float(n.group(1)) * (1 if n.group(2) == "mm" else PX)
    data = base64.b64encode(s.encode()).decode()
    g.append(f'<g transform="rotate({angle} {x} {y})"><image href="data:image/svg+xml;base64,{data}" x="{x - w/2}" y="{y - h/2}" width="{w}" height="{h}"/></g>')

def knob(base, x, y, v):
    if os.path.exists(os.path.join(LIB, base + "_bg.svg")):
        comp(base + "_bg", x, y)
    comp(base, x, y, -135 + 270 * v)

C = L["COL"]
knob("RoundLargeBlackKnob", L["MODE_X"], L["MODE_Y"], 5 / 13)
knob("RoundHugeBlackKnob", L["FADE_X"], L["FADE_Y"], 0.35)
knob("RoundLargeBlackKnob", L["CHAR_X"], L["CHAR_Y"], 0.6)
knob("RoundBlackKnob", C[0], L["ROW2_Y"], 0.3)
knob("Trimpot", C[1], L["ROW2_Y"], 0.5)
knob("Trimpot", C[3], L["ROW2_Y"], 0.5)
comp("VCVBezel", C[4], L["ROW2_Y"])
# A/B lights (A brighter at fade 0.35)
for dx, col, a in ((-4.0, "#F2A541", 0.85), (4.0, "#5FC9D8", 0.45)):
    g.append(f'<circle cx="{C[2] + dx}" cy="{L["ROW2_Y"]}" r="1.5" fill="{col}" fill-opacity="{a}" stroke="#000" stroke-width="0.2"/>')
for x in C: comp("PJ301M", x, L["CV_Y"])
for i in (0, 1, 3, 4): comp("PJ301M", C[i], L["IN_Y"])
for x in (40.8, 60.8): comp("PJ301M", x, L["OUT_Y"])
for x in (5.08, W - 2 * 5.08):
    for y in (0.0, 128.5 - 5.08):
        comp("ScrewSilver", x + 2.54, y + 2.54)

# sample screen: mode name, character readout, A->B position bar
dx, dy, dw, dh = L["DISP_X"], L["DISP_Y"], L["DISP_W"], L["DISP_H"]
mono = "font-family:'DejaVu Sans Mono',monospace;font-weight:bold"
g.append(f'<text x="{dx + 2.4}" y="{dy + 6.0}" font-size="4.4" fill="#EEE8DC" style="{mono}">06 TAPE STOP</text>')
g.append(f'<text x="{dx + dw - 2.4}" y="{dy + 6.0}" font-size="2.6" text-anchor="end" fill="#A9A39A" style="{mono}">slow spin 60%</text>')
by = dy + 11.6
g.append(f'<text x="{dx + 2.4}" y="{by + 1.2}" font-size="3.2" fill="#F2A541" style="{mono}">A</text>')
g.append(f'<text x="{dx + dw - 2.4}" y="{by + 1.2}" font-size="3.2" text-anchor="end" fill="#5FC9D8" style="{mono}">B</text>')
x0, x1 = dx + 7.0, dx + dw - 7.0
g.append(f'<rect x="{x0}" y="{by - 0.9}" width="{x1 - x0}" height="1.8" rx="0.9" fill="url(#ab)" fill-opacity="0.35"/>')
px = x0 + (x1 - x0) * 0.35
g.append(f'<rect x="{x0}" y="{by - 0.9}" width="{px - x0}" height="1.8" rx="0.9" fill="url(#ab)"/>')
g.append(f'<circle cx="{px}" cy="{by}" r="1.6" fill="#EEE8DC"/>')
g.append(f'<text x="{dx + dw / 2}" y="{dy + dh - 1.2}" font-size="2.3" text-anchor="middle" fill="#6E6862" style="{mono}">glide 1.2 s</text>')

svg = svg.replace("</svg>", "\n".join(g) + "\n</svg>")
tmp = OUT + ".svg"; open(tmp, "w").write(svg)
pw, ph = 508, 643
open(tmp + ".html", "w").write(f'<html><body style="margin:0;background:#111"><img src="{os.path.basename(tmp)}" style="width:{pw}px;height:{ph}px"></body></html>')
js = f"""const {{chromium}} = require('playwright');
(async () => {{ const b = await chromium.launch(); const p = await b.newPage({{viewport: {{width: {pw}, height: {ph}}}, deviceScaleFactor: 2}});
await p.goto('file://{tmp}.html'); await p.waitForTimeout(400);
await p.screenshot({{path: '{OUT}'}}); await b.close(); }})();"""
open(OUT + ".js", "w").write(js)
subprocess.run(["node", OUT + ".js"], check=True, cwd="/tmp")
os.remove(OUT + ".js"); os.remove(tmp); os.remove(tmp + ".html")
print("wrote", OUT)
