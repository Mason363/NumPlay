#!/usr/bin/env python3
"""Draws src/icon.png (55 x 56): a red figure with a question mark for a face, on the game's indigo.
Then round it as every NumPlay icon: python3 ../../tools/round_icons.py src/icon.png

Usage (in games/imposter): python3 tools/icon.py"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))
from fontgen import font_file  # noqa: E402  (Nunito, NumPlay's font)

K = 8                      # drawn 8 times bigger, then scaled down
W, H = 55 * K, 56 * K


def lerp(a, b, t):
    return tuple(round(x + (y - x) * t) for x, y in zip(a, b))


im = Image.new("RGB", (W, H))
top, bottom = (0x30, 0x38, 0x74), (0x12, 0x16, 0x30)
d = ImageDraw.Draw(im)
for y in range(H):
    d.line([(0, y), (W, y)], fill=lerp(top, bottom, y / (H - 1)))

red, dark_red = (0xE8, 0x36, 0x42), (0xB0, 0x1E, 0x2C)
cx = W / 2
# the shoulders, then the head
d.rounded_rectangle([cx - 21 * K, 37 * K, cx + 21 * K, 64 * K], radius=15 * K, fill=dark_red)
d.ellipse([cx - 14.5 * K, 7 * K, cx + 14.5 * K, 36 * K], fill=red)
font = ImageFont.truetype(font_file(), 26 * K)
font.set_variation_by_axes([1000])
d.text((cx, 22 * K), "?", font=font, fill=(255, 255, 255), anchor="mm")

im = im.resize((55, 56), Image.LANCZOS)
im.save(os.path.join(HERE, "..", "src", "icon.png"))
print("src/icon.png")
