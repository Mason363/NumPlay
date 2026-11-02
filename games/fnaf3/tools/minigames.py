"""Five Nights at Freddy's 3's 8-bit minigames for tools/pack.py: each level's scenery, its sprites and its walls.

A level is one of the game's frames (BB, Mangle, Toy Chica, GFreddy, RWQFSFASXC, Marion), seen a screen of 800 x 480
at a time; here at 0.4, 320 x 192. What never moves (the backdrops and the actives no event touches) is drawn into a
picture for each screen; what does is a sprite (16 colours, one of them clear). The walls are the frame's solid
backdrops, kept in the game's own coordinates (the game is played in them, src/mini.c)."""
import re
import struct

import numpy as np
from PIL import Image

S = 0.4
SW, SH = 800, 480
# key, the game's frame, its events (gamesrc spec, for the objects they touch)
LEVELS = [("BB", "BB"), ("MANGLE", "Mangle"), ("CHICA", "Toy Chica"), ("STAGE", "GFreddy"),
          ("SHADOW", "RWQFSFASXC"), ("HAPPY", "Marion")]
LOGIC = {"hitbox", "feel right", "feel left", "feel top", "feel bottom", "feel bottom 2", "feel bottom 3",
         "watch screen", "right arrow", "right arrow 2", "right arrow 3", "view", "Active 2", "blip flash"}
TOUCHED = {
    "BB": "Active 5,Active 6,Active,balloons,big cake,collect,exit,get this",
    "MANGLE": "Active 4,Active 5,Active 6,Active 7,Active 8,Active 9,Active,balloons,collect,exit,get this,kid,"
              "shadow,wall",
    "CHICA": "Active 4,Active 5,Active 8,Active 9,Active,balloons,collect 4,collect,exit 2,exit,get this 2,get this",
    "STAGE": "Active 11,Active 5,Active 6,Active 8,Active 9,Active,balloons,big cake,collect,exit,get this,"
             "zip right,zip up",
    "SHADOW": "Active 11,Active 4,Active 5,Active 6,Active 8,Active 9,Active,balloons,big cake,block 2,block 3,"
              "block 4,block,exit,get this,kid,zip right,zip up",
    "HAPPY": "Active 13,Active 14,Active 15,Active 5,Active 6,Active 8,Active,balloons,big cake,collect 3,collect 4,"
             "collect,exit,get this,trigger",
}
FEELERS = ["hitbox", "feel right", "feel left", "feel top", "feel bottom", "feel bottom 2", "feel bottom 3"]

SPRITES = []
_SPR = {}


def c_name(s):
    return re.sub(r"[^A-Z0-9]", "_", s.upper())


def sprite(app, h):
    """a picture of the game's as a sprite at 0.4: 16 colours, 0 clear (shared when used twice)"""
    if h in _SPR:
        return _SPR[h]
    im = app.picture(h).convert("RGBA")
    hx, hy = app.hotspot(h)
    w, hh = max(1, round(im.width * S)), max(1, round(im.height * S))
    small = im.resize((w, hh), Image.BOX)
    a = np.asarray(small)
    solid = a[..., 3] >= 128
    rgb = Image.fromarray(a[..., :3].astype(np.uint8))
    q = rgb.quantize(colors=15, method=Image.Quantize.MEDIANCUT)
    pal = (q.getpalette() + [0] * 48)[:45]
    idx = np.asarray(q).astype(np.uint8) + 1
    idx[~solid] = 0
    colours = [0] + [((pal[3 * i] >> 3) << 11) | ((pal[3 * i + 1] >> 2) << 5) | (pal[3 * i + 2] >> 3) for i in range(15)]
    rows = bytearray()
    for r in range(hh):
        row = list(idx[r]) + [0]
        for c in range(0, w, 2):
            rows.append(int(row[c]) | int(row[c + 1]) << 4)
    blob = struct.pack("<HHhh", w, hh, round(hx * S), round(hy * S)) + struct.pack("<16H", *colours) + bytes(rows)
    _SPR[h] = len(SPRITES)
    SPRITES.append((blob, im.width, im.height, hx, hy))
    return _SPR[h]


def pack_sprites():
    out = struct.pack("<I", len(SPRITES))
    p = 4 + 4 * len(SPRITES)
    for b, *_ in SPRITES:
        out += struct.pack("<I", p)
        p += len(b) + (-len(b) % 4)
    return out + b"".join(b + bytes(-len(b) % 4) for b, *_ in SPRITES)


def build(app, pic, arr):
    """the levels: screen pictures (pic), sprites; returns the lines of src/mini_data.h"""
    h = ["/* Made by tools/pack.py (tools/minigames.py): the minigames' levels. Included once (mini.c). */",
         "#ifndef FNAF3_MINI_DATA_H", "#define FNAF3_MINI_DATA_H", "#include <stdint.h>", "",
         "typedef struct { int16_t x, y, w, h; } MgRect;",
         "typedef struct { uint8_t obj, pad; int16_t x, y; } MgInst;",
         "typedef struct { uint8_t anim, dir, n, pad; uint16_t first; } MgAnim;   /* sprites first.. */",
         "typedef struct { uint16_t anim0; uint8_t nanim, pad; int16_t w, h, hx, hy; } MgObj;   /* box: its first picture's */",
         "typedef struct { int16_t w, h; uint8_t sw, sh; uint16_t screen0; uint16_t solid0, nsolid, inst0, ninst;",
         "                 uint8_t obj0, nobj; int16_t start_x, start_y; } MgLevel;", ""]
    rects, insts, anims, objs, levels, screens, enums = [], [], [], [], [], [], []
    feel = None
    for key, frame in LEVELS:
        placed = app.frames[frame]
        W, H = frame_size(app, frame)
        sw, sh = -(-W // SW), -(-H // SH)
        touched = set(TOUCHED[key].split(","))
        # the scenery: backdrops, and actives no event touches, at their places
        canvas = Image.new("RGBA", (sw * SW, sh * SH), (0, 0, 0, 255))
        solid0 = len(rects)
        start = (0, 0)
        for o, x, y in placed:
            kind, name, an = app.objects[o]
            if kind == 1:
                obst, bw, bh, img = app.backdrop(o)
                im = app.picture(img)
                if im.size != (bw, bh):
                    im = im.resize((bw, bh))
                paste(canvas, im, x, y)
                if obst == 1:
                    rects.append((x, y, bw, bh))
            elif kind == 2 and name == "hitbox":
                start = (x, y)
            elif kind == 2 and name not in LOGIC and name not in touched and (0, 0) in app.dirs[o]:
                im = app.picture(app.dirs[o][(0, 0)][0])
                hx, hy = app.hotspot(app.dirs[o][(0, 0)][0])
                if x == 0 and y == 0 and im.width >= SW:
                    continue   # (a full-screen overlay of the first screen's: the code draws those)
                paste(canvas, im, x - hx, y - hy)
        screen0 = len(screens)
        for cy in range(sh):
            for cx in range(sw):
                part = canvas.crop((cx * SW, cy * SH, cx * SW + SW, cy * SH + SH)).convert("RGB")
                if not np.asarray(part).any():
                    screens.append(0xFFFF)
                    continue
                a = arr(part.resize((320, 192), Image.BOX))
                screens.append(pic(f"MG_{key}_{cx}_{cy}", a))
        # what moves: every object the events touch, each placed instance
        obj0 = len(objs)
        local = {}
        inst0 = len(insts)
        for o, x, y in placed:
            kind, name, an = app.objects[o]
            if kind != 2 or name not in touched:
                continue
            if o not in local:
                local[o] = len(objs) - obj0
                a0 = len(anims)
                for (a, d), ps in sorted(app.dirs[o].items()):
                    ids = [sprite(app, p) for p in ps]   # (an animation's pictures: a run of sprites)
                    if ids != list(range(ids[0], ids[0] + len(ids))):
                        base = len(SPRITES)
                        for p in ps:
                            blob = SPRITES[_SPR[p]]
                            SPRITES.append(blob)
                        ids = list(range(base, base + len(ps)))
                    anims.append((a, d, len(ps), ids[0]))
                p0 = app.dirs[o][min(app.dirs[o])][0]
                im = app.picture(p0)
                hx, hy = app.hotspot(p0)
                objs.append((a0, len(anims) - a0, im.width, im.height, hx, hy))
                enums.append(f"#define MGO_{key}_{c_name(name)} {local[o]}")
            insts.append((local[o], x, y))
        if feel is None:
            feel = {}
            for n in FEELERS:
                o = app.obj(n, frame)
                p0 = app.dirs[o][(0, 0)][0]
                im = app.picture(p0)
                feel[n] = (im.width, im.height) + tuple(app.hotspot(p0))
        levels.append((W, H, sw, sh, screen0, solid0, len(rects) - solid0, inst0, len(insts) - inst0, obj0,
                       len(objs) - obj0, start[0], start[1]))
        h.append(f"#define MGL_{key} {len(levels) - 1}")
    h += enums
    for n, (w, hh, hx, hy) in feel.items():
        h.append(f"#define MGF_{c_name(n)}_W {w}\n#define MGF_{c_name(n)}_H {hh}\n#define MGF_{c_name(n)}_HX {hx}\n"
                 f"#define MGF_{c_name(n)}_HY {hy}")
    h.append(f"#define MGL_COUNT {len(levels)}")
    h.append("")
    h.append("static const MgRect mg_solids[] = {")
    h += [f"  {{{x}, {y}, {w}, {hh}}}," for x, y, w, hh in rects]
    h += ["};", "static const MgInst mg_insts[] = {"]
    h += [f"  {{{o}, 0, {x}, {y}}}," for o, x, y in insts]
    h += ["};", "static const MgAnim mg_anims[] = {"]
    h += [f"  {{{a}, {d}, {n}, 0, {f}}}," for a, d, n, f in anims]
    h += ["};", "static const MgObj mg_objs[] = {"]
    h += [f"  {{{a0}, {n}, 0, {w}, {hh}, {hx}, {hy}}}," for a0, n, w, hh, hx, hy in objs]
    h += ["};", "static const uint16_t mg_screens[] = {"]
    for i in range(0, len(screens), 12):
        h.append("  " + ", ".join(str(s if s == 0xFFFF else s) for s in screens[i:i + 12]) + ",")
    h += ["};", "static const MgLevel mg_levels[MGL_COUNT] = {"]
    h += ["  {" + ", ".join(str(v) for v in lv) + "}," for lv in levels]
    h += ["};", "", "#endif", ""]
    return h


def paste(canvas, im, x, y):
    layer = Image.new("RGBA", canvas.size)
    layer.paste(im, (x, y))
    canvas.alpha_composite(layer)


def frame_size(app, frame):
    return app.frame_size[frame]
