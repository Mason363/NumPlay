#!/usr/bin/env python3
"""Makes src/art.h: Meowdoku's own pictures, shrunk for the calculator: the cat on a found cell, the fish (lives,
full and lost), the hint lamp, the Locate cat and the hard level's flame. They come from the game's Android package
(its ASTC textures decoded to PNG, kept in ~/Documents/Assets/gamesrc/meowdoku/png, never in the repo).

Each picture: up to 16 colours, each with its own opacity, 4 bits a pixel.

Usage: art.py [png_dir] [src/art.h]
"""
import os
import sys

from PIL import Image

PNG = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Documents/Assets/gamesrc/meowdoku/png")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "src", "art.h")
ART = [  # name, file, width, height
    ("cat", "atlas/cat_face.png", 44, 40),
    ("fish", "fish_full.png", 20, 20),
    ("fish_dim", "atlas/fish_dim.png", 20, 20),
    ("lamp", "atlas/icon_hint_lamp.png", 22, 22),
    ("locate", "atlas/tool_cat_item.png", 22, 22),
    ("flame", "atlas/hard.png", 12, 16),
]


def pack(im):
    """16 RGBA colours (premultiplied fit), the pixels as 4-bit indices."""
    w, h = im.size
    q = im.quantize(16, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    pal = q.getpalette("RGBA")[:64]
    px = list(q.getdata())
    # the true colour and opacity of each index: the mean of its pixels
    sums = [[0, 0, 0, 0, 0] for _ in range(16)]
    for i, p in zip(px, im.getdata()):
        r, g, b, a = p
        s = sums[i]
        s[0] += r * a
        s[1] += g * a
        s[2] += b * a
        s[3] += a
        s[4] += 1
    cols = []
    for s in sums:
        if s[4] == 0 or s[3] == 0:
            cols.append((0, 0, 0, 0))
        else:
            cols.append((s[0] // s[3], s[1] // s[3], s[2] // s[3], s[3] // s[4]))
    data = bytearray()
    for y in range(h):
        row = px[y * w:(y + 1) * w] + [0]
        for x in range(0, w, 2):
            data.append(row[x] << 4 | (row[x + 1] if x + 1 < w else 0))
    return cols, bytes(data)


with open(OUT, "w") as f:
    f.write("/* Made by tools/art.py from Meowdoku's own pictures. */\n")
    total = 0
    for name, path, w, h in ART:
        im = Image.open(os.path.join(PNG, path)).convert("RGBA")
        im = im.resize((w, h), Image.LANCZOS)
        cols, data = pack(im)
        total += len(data) + 48
        f.write("static const uint16_t %s_pal[16] = {%s};\n" % (name, ",".join(
            "0x%04X" % ((r >> 3) << 11 | (g >> 2) << 5 | b >> 3) for r, g, b, a in cols)))
        f.write("static const uint8_t %s_alpha[16] = {%s};\n" % (name, ",".join(str(a) for r, g, b, a in cols)))
        f.write("static const uint8_t %s_px[%d] = {%s};\n" % (name, len(data), ",".join(str(b) for b in data)))
        f.write("static const sprite_t spr_%s = {%d, %d, %s_pal, %s_alpha, %s_px};\n" % (name, w, h, name, name, name))
print("art: %d bytes" % total)
