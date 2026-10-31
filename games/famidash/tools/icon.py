#!/usr/bin/env python3
"""src/icon.png: Famidash's first cube (its bank 0 icon, tiles 0x00-0x03 as 8x16 sprites) in its default
colours, three times bigger, over the first level's blue. Usage: python3 -I tools/icon.py GAMESRC"""
import os
import sys
from PIL import Image

src = sys.argv[1]
here = os.path.dirname(os.path.abspath(__file__))
chr_ = open(os.path.join(src, "GRAPHICS", "Icons", "bankicon00.chr"), "rb").read()
NES = {0x0F: (0, 0, 0), 0x2A: (76, 208, 32), 0x2C: (56, 180, 204), 0x12: (48, 50, 236), 0x02: (8, 16, 144),
       0x30: (236, 238, 236)}
pal = [None, NES[0x0F], NES[0x2A], NES[0x2C]]  # sprite palette 3: color3, color1, color2


def tile(t, x, y):
    p = chr_[t * 16:t * 16 + 16]
    return ((p[y] >> (7 - x)) & 1) | (((p[y + 8] >> (7 - x)) & 1) << 1)


cube = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
for half, top in ((0, 0x00), (1, 0x02)):  # Cube_0: tiles 0x01 and 0x03 (8x16: 0x00/0x01, 0x02/0x03)
    for y in range(16):
        t = top + (y >> 3)
        for x in range(8):
            c = tile(t, x, y & 7)
            if c:
                cube.putpixel((half * 8 + x, y), pal[c] + (255,))
im = Image.new("RGBA", (55, 56), NES[0x12] + (255,))
for y in range(44, 56):
    for x in range(55):
        im.putpixel((x, y), NES[0x02] + (255,))
for x in range(55):
    im.putpixel((x, 44), NES[0x30] + (255,))
big = cube.resize((36, 36), Image.NEAREST)
im.alpha_composite(big, (10, 8))
im.save(os.path.join(here, "..", "src", "icon.png"))
