#!/usr/bin/env python3
"""Makes font.h for the PC test host: anti-aliased 7x12 and 10x18 glyphs (stand-ins for Epsilon's fonts)."""
from PIL import Image, ImageDraw, ImageFont
import sys
def cells(path, size, w, h, dy):
    f = ImageFont.truetype(path, size)
    out = []
    for c in range(32, 127):
        im = Image.new("L", (w, h), 0)
        ImageDraw.Draw(im).text((0, dy), chr(c), font=f, fill=255)
        out += list(im.tobytes())
    return out
reg = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf"
bold = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"
s = cells(reg, 11, 7, 12, -1)
l = cells(bold, 16, 10, 18, -2)
with open(sys.argv[1], "w") as o:
    for name, d in (("font_small", s), ("font_large", l)):
        o.write(f"static const unsigned char {name}[] = {{" + ",".join(map(str, d)) + "};\n")
