#!/usr/bin/env python3
"""Draws NumTycoon's app icon (55x56, like the calculator's own apps) and makes the
README picture and the launcher thumbnail from a screenshot of the PC test host.
Usage: make_assets.py SHOT.png   (writes src/icon.png, docs/shot.png and ../../docs/media/thumb_tycoon.png)"""
import os
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")


def icon(w=55, h=56):
    im = Image.new("RGB", (w, h), (255, 255, 255))
    px = im.load()
    top, bot = (58, 110, 214), (255, 190, 120)
    for y in range(h):
        t = y / (h - 1)
        c = tuple(int(a + (b - a) * t) for a, b in zip(top, bot))
        for x in range(w):
            px[x, y] = c
    d = ImageDraw.Draw(im)
    # a few stars in the upper sky
    for x, y in ((8, 7), (44, 5), (31, 3), (14, 17), (49, 15)):
        d.point((x, y), fill=(255, 255, 255))
    # the skyline
    navy, navy2, win = (24, 32, 64), (38, 50, 92), (255, 214, 107)
    blds = [(0, 38, 9, 56), (9, 30, 19, 56), (19, 40, 27, 56), (27, 26, 37, 56), (37, 36, 46, 56), (46, 32, 55, 56)]
    for i, (x0, y0, x1, y1) in enumerate(blds):
        d.rectangle([x0, y0, x1 - 1, y1], fill=navy if i % 2 == 0 else navy2)
        for yy in range(y0 + 3, y1 - 2, 4):
            for xx in range(x0 + 2, x1 - 2, 3):
                if (xx * 7 + yy * 3 + i) % 5 != 0:
                    d.rectangle([xx, yy, xx + 1, yy + 1], fill=win)
    d.rectangle([22, 22, 23, 26], fill=navy2)  # antenna of the tall tower
    d.rectangle([31, 21, 32, 26], fill=navy)
    # the coin
    cx, cy, r = 27, 19, 14
    d.ellipse([cx - r, cy - r + 2, cx + r, cy + r + 2], fill=(150, 90, 10))
    d.ellipse([cx - r, cy - r, cx + r, cy + r], fill=(255, 201, 51))
    d.ellipse([cx - r + 3, cy - r + 3, cx + r - 3, cy + r - 3], fill=(255, 226, 120))
    d.ellipse([cx - r + 5, cy - r + 5, cx + r - 5, cy + r - 5], fill=(255, 201, 51))
    dark = (140, 84, 8)
    # a dollar sign made of pixels
    d.rectangle([cx - 4, cy - 7, cx + 4, cy - 6], fill=dark)
    d.rectangle([cx - 4, cy - 7, cx - 3, cy - 1], fill=dark)
    d.rectangle([cx - 4, cy - 2, cx + 4, cy - 1], fill=dark)
    d.rectangle([cx + 3, cy - 2, cx + 4, cy + 5], fill=dark)
    d.rectangle([cx - 4, cy + 4, cx + 4, cy + 5], fill=dark)
    d.rectangle([cx, cy - 10, cx + 1, cy + 8], fill=dark)
    # rounded corners, white like the other icons
    m = Image.new("L", (w, h), 0)
    ImageDraw.Draw(m).rounded_rectangle([0, 0, w - 1, h - 1], radius=9, fill=255)
    out = Image.new("RGB", (w, h), (255, 255, 255))
    out.paste(im, (0, 0), m)
    return out


if __name__ == "__main__":
    icon().save(os.path.join(ROOT, "src", "icon.png"))
    if len(sys.argv) > 1:
        shot = Image.open(sys.argv[1]).convert("RGB")
        shot.save(os.path.join(ROOT, "docs", "shot.png"))
        shot.resize((160, 120), Image.LANCZOS).convert("RGBA").save(os.path.join(ROOT, "..", "..", "docs", "media", "thumb_tycoon.png"))
