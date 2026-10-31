#!/usr/bin/env python3
"""Bad Time Simulator's pictures and letters -> src/art.c and src/art.h.

The pictures are the ones jcw87's Bad Time Simulator draws (Undertale's own sprites, by Toby Fox), from its
project folder REF (not committed: ~/Documents/Assets/gamesrc/badtime/ref, a clone of jcw87/c2-sans-fight), with
the hot spots and attach points its .caproj gives each frame. Sans is drawn there at twice his size on a 640 x 480
screen, so here at his own; what is drawn there at its own size (the blasters, the heart, the buttons, the HP and
KR labels, the speech bubble) is halved as games/undertale halves the game's battle art: each 2 x 2 block becomes
its most common opaque colour. The letters are Pixel Operator (CC0, by Jayvee Enaguas, the font Undertale credits),
as games/undertale draws them: Mono Bold for the box, Bold for the speech bubble.

Usage: art.py [REF] [--fonts DIR] [--preview DIR]   (DIR: Pixel Operator's .ttf files, gamesrc/undertale/fonts)"""
import html
import os
import re
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
REF = os.path.expanduser("~/Documents/Assets/gamesrc/badtime/ref")
FONTS = os.path.expanduser("~/Documents/Assets/gamesrc/undertale/fonts")

# name, the reference's object and animation, its frames (None: all), halved
PICTURES = [
    ("HEAD", "SansHead", ["Default", "LookLeft", "Wink", "ClosedEyes", "NoEyes", "BlueEye", "Tired1", "Tired2"], 0),
    ("HEAD_BLUE2", "SansHead", ["BlueEye#1"], 0),
    ("TORSO", "SansTorso", ["Default", "Shrug"], 0),
    ("LEGS", "SansLegs", ["Standing", "Sitting"], 0),
    ("BODY_DOWN", "SansBody", ["HandDown#*"], 0),
    ("BODY_UP", "SansBody", ["HandUp#*"], 0),
    ("BODY_RIGHT", "SansBody", ["HandRight#*"], 0),
    ("BODY_LEFT", "SansBody", ["HandLeft#*"], 0),
    ("SWEAT", "SansSweat", ["Sweat1", "Sweat2", "Sweat3"], 0),
    ("BLASTER", "GasterBlaster", ["Default", "Fire#*"], 0),   # (at its own size: the fight scales it)
    ("HEART", "PlayerHeart", ["Default", "Split"], 1),
    ("BUTTON", None, ["UIFight", "UIAct", "UIItem", "UIMercy"], 1),
    ("BUTTON_ON", None, ["UIFight", "UIAct", "UIItem", "UIMercy"], 1),
    ("LABEL", None, ["HP", "KR"], 1),
    ("BUBBLE", "SpeechBubble", ["Default"], 1),
]
# name, file, size, height, ascent, advance (None: the font's own)
LETTERS = [("BT_FONT_BOX", "PixelOperatorMono-Bold.ttf", 16, 12, 13, 8),
           ("BT_FONT_BUBBLE", "PixelOperator-Bold.ttf", 16, 12, 13, None)]


def hotspots(ref):
    """{object: {animation: [(hot x, hot y, {point: (x, y)}) for each frame]}}, as fractions of the frame"""
    s = open(os.path.join(ref, "Bad Time Simulator (Sans Fight).caproj"), encoding="utf-8").read()
    out = {}
    for om in re.finditer(r'<object-type[^>]*name="([^"]+)"(.*?)</object-type>', s, re.S):
        anims = {}
        for am in re.finditer(r'<animation[^>]*name="([^"]+)"[^>]*>(.*?)</animation>', om.group(2), re.S):
            frames = []
            for fm in re.finditer(r'<frame [^>]*hotspotX="([^"]+)" hotspotY="([^"]+)"[^>]*?(?:/>|>(.*?)</frame>)',
                                  am.group(2), re.S):
                pts = {html.unescape(n): (float(x), float(y)) for n, x, y in
                       re.findall(r'<image-point name="([^"]+)" x="([^"]+)" y="([^"]+)"', fm.group(3) or "")}
                frames.append((float(fm.group(1)), float(fm.group(2)), pts))
            anims[am.group(1)] = frames
        out[om.group(1)] = anims
    return out


def shrink(img):
    """art for a 640 x 480 screen -> half its size: each 2 x 2 block becomes its most common opaque colour (or clear,
    if most of it is), as games/undertale/tools/pack.py does"""
    a = np.array(img.convert("RGBA"))
    h, w = a.shape[:2]
    a = np.pad(a, ((0, h % 2), (0, w % 2), (0, 0)))
    H, W = a.shape[0] // 2, a.shape[1] // 2
    out = np.zeros((H, W, 4), np.uint8)
    for y in range(H):
        for x in range(W):
            block = a[2 * y:2 * y + 2, 2 * x:2 * x + 2].reshape(4, 4)
            op = block[block[:, 3] >= 128]
            if len(op) < 2:
                continue
            cols, counts = np.unique(op[:, :3], axis=0, return_counts=True)
            out[y, x, :3] = cols[np.argmax(counts)]
            out[y, x, 3] = 255
    return Image.fromarray(out, "RGBA")


def frames_of(ref, hot, obj, spec):
    """[(image, hot spot, attach point)] for 'Anim', 'Anim#n' (its frame n) or 'Anim#*' (all its frames)"""
    anim, _, which = spec.partition("#")
    if obj is None:   # (a sprite of its own: its Default animation)
        obj, anim = anim, "Default"
    d = os.path.join(ref, "Animations", obj, anim)
    files = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    idx = range(len(files)) if which == "*" else [int(which)] if which else [0]
    out = []
    for i in idx:
        hx, hy, pts = hot[obj][anim][i]
        point = next(iter(pts.values()), (hx, hy))
        out.append((Image.open(os.path.join(d, files[i])).convert("RGBA"), (hx, hy), point))
    return out


def pack(img, hot, point, half):
    """an art_t's fields: size, anchor, attach point, palette (index 0 clear) and 4-bit pixels"""
    w0, h0 = img.size
    if half:
        img = shrink(img)
    a = np.array(img)
    h, w = a.shape[:2]
    k = 2 if half else 1
    ax, ay = round(hot[0] * w0 / k), round(hot[1] * h0 / k)
    jx, jy = round(point[0] * w0 / k), round(point[1] * h0 / k)
    cols = []
    idx = np.zeros((h, w), np.uint8)
    for y in range(h):
        for x in range(w):
            if a[y, x, 3] < 128:
                continue
            c = (int(a[y, x, 0]) << 16) | (int(a[y, x, 1]) << 8) | int(a[y, x, 2])
            if c not in cols:
                cols.append(c)
            idx[y, x] = cols.index(c) + 1
    assert len(cols) <= 15, "%d colours" % len(cols)
    flat = list(idx.reshape(-1)) + [0]
    px = [flat[i] | flat[i + 1] << 4 for i in range(0, w * h, 2)]
    return w, h, ax, ay, jx, jy, ([0] + cols + [0] * 15)[:16], px


def letters(path, size, height, ascent, advance):
    """an np_font_t's glyphs (w, h, x, y from the baseline, advance, offset) and 4-bit coverage, ASCII 32 to 126"""
    font = ImageFont.truetype(path, size)
    base = size + font.getmetrics()[0]   # (the baseline where the letter is drawn below)
    glyphs, data = [], []
    for c in range(32, 127):
        ch = chr(c)
        im = Image.new("L", (size * 3, size * 3), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((size, size), ch, font=font, fill=255)
        adv = advance if advance else int(round(font.getlength(ch)))
        bb = im.getbbox()
        if bb is None:
            glyphs.append((0, 0, 0, 0, adv, 0))
            continue
        g = np.array(im.crop(bb)) >= 128
        gh, gw = g.shape
        ox, oy = bb[0] - size, bb[1] - base
        off = len(data)
        flat = [15 if v else 0 for v in g.reshape(-1)]
        if len(flat) & 1:
            flat.append(0)
        data += [flat[i] | flat[i + 1] << 4 for i in range(0, len(flat), 2)]
        glyphs.append((gw, gh, ox, oy, adv, off))
    return glyphs, data, height - ascent if height > ascent else 3


def main():
    args = sys.argv[1:]
    preview = args[args.index("--preview") + 1] if "--preview" in args else None
    fonts = args[args.index("--fonts") + 1] if "--fonts" in args else FONTS
    pos = [a for i, a in enumerate(args) if not a.startswith("--") and (i == 0 or args[i - 1] not in
                                                                      ("--preview", "--fonts"))]
    ref = pos[0] if pos else REF
    hot = hotspots(ref)
    out_c = ["/* Generated by tools/art.py: don't edit. Undertale's sprites (Toby Fox) as jcw87's Bad Time Simulator",
             " * draws them; letters: Pixel Operator by Jayvee Enaguas (CC0). */", '#include "art.h"', ""]
    out_h = ["/* Generated by tools/art.py: don't edit. */", "#ifndef BT_ART_H", "#define BT_ART_H",
             "#include <stdint.h>", '#include "font.h"', "",
             "typedef struct {", "  uint8_t w, h, ax, ay;      /* size, anchor */",
             "  uint8_t jx, jy;            /* where the next part attaches (Sans: the head, the torso; the head: sweat) */",
             "  const uint32_t *pal;        /* 0xRRGGBB; index 0 is see-through */",
             "  const uint8_t *px;          /* 4 bits a pixel, two a byte, the first in the low half */",
             "} art_t;", ""]
    shots = []
    for name, obj, specs, half in PICTURES:
        items = []
        for spec in specs:
            for img, hs, pt in frames_of(ref, hot, obj, spec):
                if name == "BUTTON_ON":
                    img = Image.open(os.path.join(ref, "Animations", spec, "Highlight", "000.png")).convert("RGBA")
                items.append(pack(img, hs, pt, half))
                shots.append((name, img if not half else shrink(img)))
        for i, (w, h, ax, ay, jx, jy, pal, px) in enumerate(items):
            out_c.append("static const uint32_t %s_pal%d[] = {%s};" % (name, i, ", ".join("0x%06X" % c for c in pal)))
            out_c.append("static const uint8_t %s_px%d[] = {%s};" % (name, i, ", ".join(str(b) for b in px)))
        out_c.append("const art_t ART_%s[%d] = {" % (name, len(items)))
        for i, (w, h, ax, ay, jx, jy, pal, px) in enumerate(items):
            out_c.append("  {%d, %d, %d, %d, %d, %d, %s_pal%d, %s_px%d}," % (w, h, ax, ay, jx, jy, name, i, name, i))
        out_c.append("};")
        out_c.append("")
        out_h.append("extern const art_t ART_%s[%d];" % (name, len(items)))
    out_h.append("")
    for name, ttf, size, height, ascent, advance in LETTERS:
        glyphs, data, desc = letters(os.path.join(fonts, ttf), size, height, ascent, advance)
        lo = name.lower()
        out_c.append("static const np_glyph_t %s_glyphs[95] = {" % lo)
        out_c += ["  {%d, %d, %d, %d, %d, %d}," % g for g in glyphs]
        out_c.append("};")
        out_c.append("static const uint8_t %s_data[] = {%s};" % (lo, ", ".join(str(b) for b in data)))
        out_c.append("const np_font_t %s = {32, 95, %d, %d, %s_glyphs, %s_data};" % (name, ascent, desc, lo, lo))
        out_c.append("")
        out_h.append("extern const np_font_t %s;" % name)
    out_h += ["#endif", ""]
    with open(os.path.join(SRC, "art.c"), "w") as f:
        f.write("\n".join(out_c))
    with open(os.path.join(SRC, "art.h"), "w") as f:
        f.write("\n".join(out_h))
    if preview:
        os.makedirs(preview, exist_ok=True)
        for i, (name, img) in enumerate(shots):
            img.resize((img.width * 4, img.height * 4), Image.NEAREST).save(os.path.join(preview, "%02d_%s.png" % (i, name)))


if __name__ == "__main__":
    main()
