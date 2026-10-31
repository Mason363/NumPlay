#!/usr/bin/env python3
"""Makes src/icon.png: a stone path running into the jungle's haze, a coin above it (drawn here, 55x56).
Usage: icon.py src/icon.png   (then tools/round_icons.py rounds its corners)"""
import sys

from PIL import Image, ImageDraw

UP = 8
W, H = 55 * UP, 56 * UP
im = Image.new("RGB", (W, H))
d = ImageDraw.Draw(im)
for y in range(H):   # sky to haze, then the depths
    t = y / H
    top, mid, low = (111, 166, 210), (191, 211, 198), (43, 74, 48)
    c = [int(top[i] + (mid[i] - top[i]) * min(1, t / 0.45)) if t < 0.45 else int(mid[i] + (low[i] - mid[i]) * (t - 0.45) / 0.55) for i in range(3)]
    d.line([(0, y), (W, y)], fill=tuple(c))
hy = int(H * 0.45)
for x0, top in ((-20, 0.12), (W - 70, 0.18)):   # jungle trees
    d.rectangle([x0 + 34, int(H * top) + 40, x0 + 52, H], fill=(106, 74, 45))
    d.rectangle([x0, int(H * top), x0 + 90, int(H * top) + 52], fill=(58, 107, 44))
# the path: stripes of stone, sides going down
pts = lambda y0, y1: [(W / 2 - (12 + (y0 - hy) * 1.1), y0), (W / 2 + 12 + (y0 - hy) * 1.1, y0),
                      (W / 2 + 12 + (y1 - hy) * 1.1, y1), (W / 2 - (12 + (y1 - hy) * 1.1), y1)]
d.polygon([(W / 2 - 12, hy), (W / 2 + 12, hy), (W / 2 + 12 + (H - hy) * 1.1, H), (W / 2 - 12 - (H - hy) * 1.1, H)], fill=(118, 104, 79))
y, k, step = hy, 0, 8
while y < H:
    y1 = min(H, y + step)
    p = pts(y, y1)
    p = [(p[0][0] + 10, y), (p[1][0] - 10, y), (p[2][0] - 10, y1), (p[3][0] + 10, y1)]
    d.polygon(p, fill=(168, 151, 122) if k % 2 == 0 else (156, 139, 111))
    y, k, step = y1, k + 1, step * 1.35
# a coin
cx, cy, r = W / 2, H * 0.36, 70
d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(200, 154, 16))
d.ellipse([cx - r * 0.72, cy - r * 0.72, cx + r * 0.72, cy + r * 0.72], fill=(255, 210, 30))
im.resize((55, 56), Image.LANCZOS).convert("RGBA").save(sys.argv[1])
