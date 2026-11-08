#!/usr/bin/env python3
"""Doodle Jump's pictures, made from the game's own files: src/data.c and src/data.h.

    python3 -I tools/pack.py GAMESRC src

GAMESRC holds the Android release (apk/dj.apk); the pictures are read from it as they are. The game's page is
320 x 480 points, shown at half size (160 x 240 pixels), and its pictures are drawn for screens of twice its
points ("_X"), so the page's pictures are shrunk to a quarter; the menus use the whole screen and their pictures
are shrunk to half.

What it writes:
- every picture as runs of pixels between see-through gaps, each pixel a byte into the picture's own palette of
  up to 64 colours, each with one of 33 levels of see-through (the font's letters share one: white, see-through
  by level);
- where each picture sits from its object's point (its "anchor"), in screen pixels;
- the game's own font (doodlejumpfont) at two sizes, the letters of ASCII only.
"""
import os
import plistlib
import sys
import zipfile

import numpy as np
from PIL import Image

SRC = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/Documents/Assets/gamesrc/doodlejump')
OUT = sys.argv[2] if len(sys.argv) > 2 else 'src'
APK = zipfile.ZipFile(os.path.join(SRC, 'apk', 'dj.apk'))
_cache = {}


def png(name):
    if name not in _cache:
        with APK.open('assets/' + name) as f:
            _cache[name] = Image.open(f).convert('RGBA')
            _cache[name].load()
    return _cache[name]


def shrink(im, scale):
    """Area-averaged resize with the colours weighted by their alpha."""
    w, h = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    a = np.asarray(im, dtype=np.float64) / 255.0
    pm = a.copy()
    pm[..., :3] *= pm[..., 3:4]
    src = Image.fromarray((pm * 255).round().astype(np.uint8), 'RGBA')
    r = np.asarray(src.resize((w, h), Image.BOX), dtype=np.float64) / 255.0
    al = r[..., 3:4]
    rgb = np.where(al > 0, r[..., :3] / np.maximum(al, 1e-9), 0)
    return np.concatenate([np.clip(rgb, 0, 1), al], axis=2)


Q, H = 0.25, 0.5  # the page's pictures, the menus'

# name: (file, (x, y, w, h) in it, scale, anchor (x, y) in the file's pixels: the object's point)
# The page: platforms by the middle of their top, things standing on a platform by the middle of their foot,
# the doodler by the middle of his feet, monsters and the rest by their middle.
SPRITES = []


def spr(name, file, rect, scale, anchor=None, turn=0):
    x, y, w, h = rect
    if anchor is None:
        anchor = (x + w / 2, y + h / 2)
    elif anchor == 'bottom':
        anchor = (x + w / 2, y + h)
    elif anchor == 'top':
        anchor = (x + w / 2, y)
    SPRITES.append((name, file, rect, scale, anchor, turn))


T = 'gametiles_X.png'
# platforms: the sheet keeps them in one column, their middle at x 59
plat = [('GREEN', (2, 2, 114, 30)), ('BLUE', (2, 36, 114, 30)), ('WHITE', (2, 109, 114, 30)),
        ('BROWN0', (2, 146, 120, 30)), ('BROWN1', (2, 182, 121, 41)), ('BROWN2', (4, 232, 114, 55)),
        ('BROWN3', (3, 298, 116, 66)),
        ('BOMB0', (2, 368, 114, 30)), ('BOMB1', (2, 404, 114, 30)), ('BOMB2', (2, 441, 114, 30)),
        ('BOMB3', (2, 477, 114, 30)), ('BOMB4', (2, 513, 114, 30)),
        ('BOOM0', (6, 546, 109, 35)), ('BOOM1', (2, 587, 121, 50)), ('BOOM2', (1, 641, 123, 57))]
for n, r in plat:
    spr('P_' + n, T, r, Q, (59, r[1]))
spr('SPRING0', T, (808, 198, 34, 23), Q, 'bottom')
spr('SPRING1', T, (808, 230, 34, 55), Q, 'bottom')
spr('TRAMP0', T, (377, 198, 71, 27), Q, 'bottom')
spr('TRAMP1', T, (948, 109, 71, 27), Q, 'bottom')
spr('TRAMP2', T, (299, 191, 71, 34), Q, 'bottom')
spr('PROP_ITEM', T, (664, 473, 61, 40), Q, 'bottom')
spr('JET_ITEM', T, (396, 529, 48, 74), Q, 'bottom')
# monsters
for i, r in enumerate([(298, 1, 151, 89), (299, 95, 151, 87), (457, 3, 156, 87), (619, 3, 156, 87), (782, 3, 156, 87)]):
    spr(f'M_FLY{i}', T, r, Q)
for i, r in enumerate([(138, 4, 122, 175), (135, 188, 126, 175), (130, 842, 127, 175), (262, 842, 127, 173),
                       (4, 842, 118, 173)]):
    spr(f'M_BAT{i}', T, r, Q)
spr('M_EYE0', T, (129, 373, 75, 99), Q)
spr('M_EYE1', T, (212, 373, 75, 99), Q)
spr('M_BLOB0', T, (2, 721, 163, 102), Q)
spr('M_BLOB1', T, (128, 481, 163, 102), Q)
spr('M_CROC0', T, (1026, 6, 182, 62), Q)
spr('M_CROC1', T, (1026, 73, 182, 63), Q)
spr('M_HAIRY', T, (1028, 141, 169, 108), Q)
spr('M_BUG0', T, (1027, 676, 126, 169), Q)
spr('M_BUG1', T, (1027, 852, 126, 169), Q)
spr('M_RED', T, (298, 531, 92, 71), Q)
spr('M_PURPLE', T, (674, 340, 110, 99), Q)
spr('M_HOLE', T, (455, 101, 145, 131), Q)
spr('M_UFO0', T, (864, 416, 157, 69), Q)
spr('M_UFO1', T, (857, 166, 160, 69), Q)
spr('M_BEAM', T, (857, 235, 160, 172), Q, 'top')
# the doodler: his four poses facing each way, the two of shooting, his nose (put on the shooting pose)
for n in ['likleft', 'likleftodskok', 'likright', 'likrightodskok', 'likpuca', 'likpucaodskok']:
    spr('D_' + n[3:].upper(), n + '_X.png', (0, 0, 124, 120), Q, (62, 120))
spr('D_NOSE', 'liknjuska_X.png', (0, 0, 28, 120), Q, (14, 120))
# turning over on a trampoline: the two that face a way, turned by an eighth at a time about his middle
for n in ['likleft', 'likright']:
    for k in range(1, 8):
        spr(f'D_TURN{n[3].upper()}{k}', n + '_X.png', (-40, -40, 204, 200), Q, (62, 75), 45 * k * (1 if n == 'likright' else -1))
# what he wears: the propeller hat (its blades still, then turning), the jetpack and its flames
for i, (x, y) in enumerate([(0, 0), (64, 0), (0, 64), (64, 64)]):
    spr(f'HAT{i}', 'propeller_X.png', (x, y, 64, 64), Q, (x + 32, y + 64))
for i in range(10):
    x, y = i % 4 * 64, i // 4 * 128
    spr(f'JET{i}', 'jetpack_X.png', (x, y, 64, 128), Q, (x + 32, y))
for i in range(3):
    spr(f'STARS{i}', f'stars{i + 1}_X.png', (0, 0, 96, 72), Q)
spr('BULLET', 'projectiletiles_X.png', (1, 2, 22, 22), Q)
# the bar at the top of the page and its digits
spr('TOPBAR', 'topscore_X.png', (0, 0, 640, 126), Q, (0, 0))
for d, r in zip('1234567890', [(644, 3, 9, 30), (655, 4, 29, 29), (688, 2, 25, 31), (714, 1, 21, 32), (738, 2, 27, 31),
                              (769, 2, 26, 31), (798, 2, 26, 31), (825, 3, 24, 30), (853, 1, 22, 32), (878, 1, 25, 33)]):
    spr('DIGIT' + d, 'topscore_X.png', r, Q, (r[0], 33))
spr('PAUSE', 'pausebutton_X.png', (0, 0, 32, 32), H)
# the menus: the title's pictures (the loading screen's), the buttons, "game over!"
L = 'loadingtile_X.png'
spr('LOGO', L, (0, 335, 412, 98), H, (0, 335))
spr('T_UFO', L, (3, 433, 156, 68), H)
spr('T_HOLE', L, (650, 194, 139, 131), H)
spr('T_GREEN', T, (2, 2, 114, 30), H, (59, 2))
spr('T_DOOD', 'likright_X.png', (0, 0, 124, 120), H, (62, 120))
spr('T_DOODJ', 'likrightodskok_X.png', (0, 0, 124, 120), H, (62, 120))
spr('T_EYE', T, (129, 373, 75, 99), H)
spr('BUTTON', 'ui_X.png', (2, 274, 224, 88), H)
spr('BUTTON_ON', 'ui_X.png', (2, 364, 224, 88), H)
spr('GAMEOVER', 'ui_X.png', (2, 2, 438, 102), H, (2 + 219, 2))

# ---------------------------------------------------------------- the font
FONT = plistlib.load(APK.open('assets/doodlejumpfont.plist'))['chars']
GLYPHS = {int(c['-id']): {k[1:]: int(v) for k, v in c.items() if k != '-id' and k != '-chnl'} for c in FONT['char']}
FONT_SIZES = [('BIG', 0.5), ('SMALL', 0.375)]  # (of the _X font: its letters 56 pixels apart)


# ---------------------------------------------------------------- packing
CAP = 64  # colours a picture


def quantize(px, mask):
    """px: N x 4 (rgb 0..1, alpha 0..1) of the seen pixels: (palette [(rgb565, alpha 0..32)], index per pixel)"""
    rgb = (px[:, :3] * 255).round().astype(np.int32)
    a = np.clip((px[:, 3] * 32).round().astype(np.int32), 0, 32)
    keys = np.stack([rgb[:, 0] >> 3, rgb[:, 1] >> 2, rgb[:, 2] >> 3, a], axis=1)
    uniq, inv = np.unique(keys, axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    if len(uniq) > CAP:  # too many: Pillow's quantizer on the colours and their see-through
        im = Image.fromarray(np.concatenate([rgb, (a * 255 // 32)[:, None]], axis=1).astype(np.uint8)[None, :, :], 'RGBA')
        q = im.quantize(CAP, method=Image.Quantize.FASTOCTREE)
        pal = np.array(q.getpalette(rawmode='RGBA')[:CAP * 4]).reshape(-1, 4)
        idx = np.asarray(q)[0]
        used = sorted(set(idx.tolist()))
        remap = {u: i for i, u in enumerate(used)}
        out = [((int(pal[u][0]) >> 3) << 11 | (int(pal[u][1]) >> 2) << 5 | int(pal[u][2]) >> 3,
                max(1, min(32, round(pal[u][3] * 32 / 255)))) for u in used]
        return out, np.array([remap[i] for i in idx.tolist()])
    out = [(int(k[0]) << 11 | int(k[1]) << 5 | int(k[2]), int(k[3])) for k in uniq]
    return out, inv


def encode(img, letter=False):
    """img: h x w x 4 floats. Rows of runs: a byte of runs, then each run's gap, length and that many bytes.
    A letter's pixels are its see-through level (the font's palette)."""
    h, w = img.shape[:2]
    seen = img[..., 3] * 32 >= 0.5
    ys, xs = np.nonzero(seen)
    if len(ys) == 0:
        return 0, 0, 0, 0, [(0, 32)], bytes([0])
    y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    img, seen = img[y0:y1, x0:x1], seen[y0:y1, x0:x1]
    h, w = img.shape[:2]
    if letter:
        pal, idx = None, np.clip((img[seen][:, 3] * 32).round().astype(np.int32), 1, 32)
    else:
        pal, idx = quantize(img[seen], seen)
    index = np.zeros((h, w), np.int32)
    index[seen] = idx
    data = bytearray()
    for y in range(h):
        runs, x = [], 0
        while x < w:
            gap = 0
            while x < w and not seen[y, x]:
                x += 1
                gap += 1
            if x == w:
                break
            n = 0
            while x + n < w and seen[y, x + n] and n < 255:
                n += 1
            while gap > 255:  # (never on these pictures, but kept right)
                runs.append((255, []))
                gap -= 255
            runs.append((gap, index[y, x:x + n].tolist()))
            x += n
        data.append(len(runs))
        for gap, px in runs:
            data += bytes([gap, len(px)]) + bytes(px)
    return int(x0), int(y0), w, h, pal, bytes(data)


def main():
    sprites, pal, pix = [], [], bytearray()
    names = []

    letters = None  # the font's palette: white, see-through by level

    def add(name, img, ax, ay):
        nonlocal letters
        x0, y0, w, h, p, data = encode(img, name.startswith('F_'))
        if p is None:
            if letters is None:
                letters = len(pal)
                pal.extend([(0xFFFF, a) for a in range(33)])
            at = letters
        else:
            at = len(pal)
            pal.extend(p)
        if len(pal) > 65535:
            raise SystemExit('palette too big')
        sprites.append((w, h, x0 - ax, y0 - ay, at, len(pix)))
        names.append(name)
        pix.extend(data)

    for name, file, (x, y, w, h), scale, (ax, ay), turn in SPRITES:
        im = png(file).crop((x, y, x + w, y + h))
        if turn:  # (clockwise on the screen)
            im = im.rotate(-turn, resample=Image.BICUBIC, center=(ax - x, ay - y))
        # (shrunk from the anchor's place in the file, so a picture's pixels fall where its anchor says)
        img = shrink(im, scale)
        add(name, img, round((ax - x) * scale), round((ay - y) * scale))
    fonts = []
    atlas = png('fonts/doodlejumpfont_X.png')
    for fname, scale in FONT_SIZES:
        glyphs = []
        base = 49  # the letters' baseline in their 56-pixel line ('A', 'a': yoffset + height)
        for c in range(32, 127):
            g = GLYPHS[c]
            im = atlas.crop((g['x'], g['y'], g['x'] + g['width'], g['y'] + g['height']))
            img = shrink(im, scale) if g['width'] and g['height'] else np.zeros((1, 1, 4))
            img[..., :3] = 1.0  # (white, tinted when drawn)
            first = len(sprites)
            add(f'F_{fname}_{c}', img, round(-g['xoffset'] * scale), round((base - g['yoffset']) * scale))
            glyphs.append((first, round(g['xadvance'] * scale)))
        fonts.append((fname, glyphs, round(56 * scale)))

    with open(os.path.join(OUT, 'data.h'), 'w') as f:
        f.write('/* Doodle Jump\'s pictures and font, made by tools/pack.py from the game\'s own files. */\n')
        f.write('#ifndef DJ_DATA_H\n#define DJ_DATA_H\n#include <stdint.h>\n\n')
        f.write('typedef struct {\n  uint8_t w, h;\n  int8_t x, y;     /* its top left, from its anchor */\n'
                '  uint16_t pal;    /* its first colour in dj_pal */\n  uint32_t pix;    /* its rows in dj_pix */\n'
                '} dj_sprite_t;\n\n')
        f.write('enum {\n')
        for n in names:
            if not n.startswith('F_'):
                f.write(f'  S_{n},\n')
        f.write('  S_COUNT\n};\n\n')
        f.write('typedef struct {\n  uint16_t sprite;  /* its picture */\n  uint8_t advance;\n} dj_glyph_t;\n\n')
        f.write('extern const dj_sprite_t dj_spr[];\nextern const uint16_t dj_pal[];\nextern const uint8_t dj_alpha[];\n'
                'extern const uint8_t dj_pix[];\n')
        for fname, glyphs, line in fonts:
            f.write(f'extern const dj_glyph_t dj_font_{fname.lower()}[95]; /* ASCII 32 to 126, {line} pixels a line */\n')
        f.write('#endif\n')
    with open(os.path.join(OUT, 'data.c'), 'w') as f:
        f.write('/* Doodle Jump\'s pictures and font, made by tools/pack.py from the game\'s own files. */\n')
        f.write('#include "data.h"\n\n')
        f.write(f'const dj_sprite_t dj_spr[{len(sprites)}] = {{\n')
        for (w, h, x, y, p, o), n in zip(sprites, names):
            f.write(f'  {{{w}, {h}, {x}, {y}, {p}, {o}}}, /* {n} */\n')
        f.write('};\n\n')
        f.write(f'const uint16_t dj_pal[{len(pal)}] = {{\n')
        for i in range(0, len(pal), 16):
            f.write('  ' + ', '.join(f'0x{c:04X}' for c, _ in pal[i:i + 16]) + ',\n')
        f.write('};\n\n')
        f.write(f'const uint8_t dj_alpha[{len(pal)}] = {{\n')
        for i in range(0, len(pal), 32):
            f.write('  ' + ', '.join(str(a) for _, a in pal[i:i + 32]) + ',\n')
        f.write('};\n\n')
        f.write(f'const uint8_t dj_pix[{len(pix)}] = {{\n')
        for i in range(0, len(pix), 32):
            f.write('  ' + ','.join(str(b) for b in pix[i:i + 32]) + ',\n')
        f.write('};\n\n')
        for fname, glyphs, line in fonts:
            f.write(f'const dj_glyph_t dj_font_{fname.lower()}[95] = {{\n')
            for i in range(0, 95, 8):
                f.write('  ' + ' '.join(f'{{{s}, {a}}},' for s, a in glyphs[i:i + 8]) + '\n')
            f.write('};\n\n')
    print(f'{len(sprites)} pictures, {len(pal)} colours, {len(pix)} bytes of pixels: '
          f'{len(sprites) * 10 + len(pal) * 3 + len(pix)} bytes')


main()
