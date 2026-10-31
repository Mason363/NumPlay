#!/usr/bin/env python3
"""src/icon.png: the first bike, drawn from its outline in src/data.h, on a glowing line, in the game's colours."""
import os, re
from PIL import Image, ImageDraw, ImageFilter
here = os.path.dirname(os.path.abspath(__file__))
h = open(os.path.join(here, '..', 'src', 'data.h')).read()
xy = [int(v) for v in re.search(r'bike_xy\[\d+\] = \{([^}]*)\}', h).group(1).replace('\n', ' ').split(',') if v.strip()]
row = re.search(r'rd_bikes\[NBIKES\] = \{\n  \{([^}]*)\}', h).group(1).split(',')
o0, on = int(row[4]), int(row[5])
S = 8   # drawn 8 times bigger, then shrunk
W, H = 55 * S, 56 * S
im = Image.new('RGBA', (W, H), (16, 24, 26, 255))
glow = Image.new('RGBA', (W, H), (0, 0, 0, 0)); g = ImageDraw.Draw(glow)
y0 = 41 * S
g.line([(0, y0 + 9 * S), (W, y0 - 9 * S)], fill=(0, 255, 25, 255), width=7 * S)
im.alpha_composite(glow.filter(ImageFilter.GaussianBlur(3 * S)))
d = ImageDraw.Draw(im)
d.line([(0, y0 + 9 * S), (W, y0 - 9 * S)], fill=(0, 255, 25, 255), width=int(2.6 * S))
d.line([(0, y0 + 9 * S), (W, y0 - 9 * S)], fill=(219, 255, 222, 255), width=int(1.0 * S))
import math
a = math.atan2(-18, 55); z = 0.5 * S; cx, cy = 26.5 * S, 29.5 * S
c, s = math.cos(a), math.sin(a)
def P(lx, ly): return (cx + (c * lx - s * ly) * z, cy + (s * lx + c * ly) * z)
pts = [P(xy[2 * (o0 + i)] * 0.5, xy[2 * (o0 + i) + 1] * 0.5) for i in range(on)]
for k in (-34, 38):
    x, y = P(k, 13.3); r = 8.4 * z
    d.ellipse((x - r, y - r, x + r, y + r), fill=(255, 255, 255, 255))
    d.ellipse((x - r * 0.35, y - r * 0.35, x + r * 0.35, y + r * 0.35), fill=(16, 24, 26, 255))
d.polygon(pts, fill=(0, 232, 235, 255), outline=(255, 255, 255, 255), width=int(0.9 * S))
im.resize((55, 56), Image.LANCZOS).save(os.path.join(here, '..', 'src', 'icon.png'))
