#!/usr/bin/env python3
"""Draws src/icon.png (55 x 56): the four players, red, blue, green and yellow, looking at each other on the game's
night violet. Then round it as every NumPlay icon: python3 ../../tools/round_icons.py src/icon.png

Usage (in games/fourplayer): python3 tools/icon.py"""
import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
K = 8                      # drawn 8 times bigger, then scaled down
W, H = 55 * K, 56 * K


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


im = Image.new("RGB", (W, H))
d = ImageDraw.Draw(im)
top, bottom = (0x2B, 0x24, 0x52), (0x10, 0x0D, 0x22)
for y in range(H):
    d.line([(0, y), (W, y)], fill=lerp(top, bottom, y / (H - 1)))

PLAYERS = [((0xF2, 0x45, 0x3D), 15, 15), ((0x2F, 0x7F, 0xF0), 40, 15), ((0x2D, 0xB6, 0x53), 15, 41),
           ((0xF5, 0xB4, 0x00), 40, 41)]
R = 11.5
for c, x, y in PLAYERS:
    x, y = x * K, (y + 0.5) * K
    look = math.atan2(28 * K - y, 27.5 * K - x)
    d.ellipse([x - R * K + 1.5 * K, y - R * K + 2.5 * K, x + R * K + 1.5 * K, y + R * K + 2.5 * K], fill=(8, 6, 18))
    d.ellipse([x - R * K, y - R * K, x + R * K, y + R * K], fill=lerp(c, (0, 0, 0), 0.3))
    d.ellipse([x - R * 0.9 * K - 0.6 * K, y - R * 0.9 * K - 1.1 * K, x + R * 0.9 * K - 0.6 * K, y + R * 0.9 * K - 1.1 * K], fill=c)
    ex, ey = x + math.cos(look) * 3.6 * K, y + math.sin(look) * 3.6 * K - 1.4 * K
    for s in (-1, 1):
        cx = ex + s * 3.8 * K
        d.ellipse([cx - 3.1 * K, ey - 3.1 * K, cx + 3.1 * K, ey + 3.1 * K], fill=(255, 255, 255))
        px, py = cx + math.cos(look) * 1.4 * K, ey + math.sin(look) * 1.4 * K
        d.ellipse([px - 1.7 * K, py - 1.7 * K, px + 1.7 * K, py + 1.7 * K], fill=(0x15, 0x12, 0x2A))

im = im.resize((55, 56), Image.LANCZOS)
im.save(os.path.join(HERE, "..", "src", "icon.png"))
print("src/icon.png")
