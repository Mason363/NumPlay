#!/usr/bin/env python3
"""Cat Ninja's icon (src/icon.png, 55 x 56) from the game's own pictures: the cat on its floor, before its grid.

    python3 -I tools/icon.py GAMESRC src/icon.png
"""
import os
import sys

from PIL import Image

SRC = os.path.expanduser(sys.argv[1])
IMG = os.path.join(SRC, 'ex/loader/image')


def tex(name):
    for f in os.listdir(IMG):
        if f.endswith('Textures_' + name + 'class.png'):
            return Image.open(os.path.join(IMG, f)).convert('RGBA')
    raise KeyError(name)


grid, ground, cat = tex('gridtile'), tex('groundtile'), tex('characteridle')
tint = ground.copy()  # the map's colour at the start: red times 1.4
px = tint.load()
for y in range(tint.height):
    for x in range(tint.width):
        r, g, b, a = px[x, y]
        px[x, y] = (min(255, int(r * 1.4)), g, b, a)
icon = Image.new('RGBA', (55, 56))
for y in range(-8, 56, 20):
    for x in range(-6, 55, 20):
        icon.paste(grid, (x, y))
for x in range(-6, 55, 20):
    icon.paste(tint, (x, 42))
big = cat.resize((cat.width * 2, cat.height * 2), Image.NEAREST)
icon.alpha_composite(big, (5, 2))
icon.save(sys.argv[2])
