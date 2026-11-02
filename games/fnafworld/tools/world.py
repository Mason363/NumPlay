"""FNAF World's overworld for tools/pack.py: the 6000 x 4000 map as the game lays it out (its frame "overworld").

Drawn at 0.4 (the game's 50-pixel grid is 20 pixels here). What never changes is a list of pictures placed in the
game's own order (its layers, the backdrops of a layer under its actives); the ground is the big tiled backdrops of
the bottom layer, kept as rectangles and their tile. Pictures are raw 16-bit colour with 4-bit alpha (SPRS: u16 width,
height, i16 hot spot x, y, u16 flags: 1 no alpha, u16 0, the pixels, then the alpha rows), fast to draw many of.

What the game's events act on (chests, bosses, switches, barriers, shops, the area zones...) is listed apart with
its box in the game's coordinates, and drawn by src/world.c as their state says. Walls: the obstacle backdrops,
a bit for each 10 x 10 pixels of the map (the game's feelers test them)."""
import struct

import numpy as np
from PIL import Image

S = 0.4
CELL = 10
SPR = []          # blobs
_SPR = {}


def ink_hidden(app, o):
    ink, param = app.ink[o]
    return bool(ink & 0x1000) and (param >> 24) == 0


def sprite(app, h, scale=S, opaque=False, size=None):
    """a picture as a raw sprite (shared when used again)"""
    key = (h, scale, opaque, size)
    if key in _SPR:
        return _SPR[key]
    im = app.picture(h)
    hx, hy = app.hotspot(h)
    w, hh = size or (max(1, round(im.width * scale)), max(1, round(im.height * scale)))
    a = np.asarray(im.resize((w, hh), Image.LANCZOS)).astype(np.int32)
    rgb = ((a[..., 0] >> 3) << 11) | ((a[..., 1] >> 2) << 5) | (a[..., 2] >> 3)
    blob = struct.pack("<HHhhHH", w, hh, round(hx * scale), round(hy * scale), 1 if opaque else 0, 0)
    blob += rgb.astype("<u2").tobytes()
    if not opaque:
        al = np.clip(np.round(a[..., 3] / 255 * 15), 0, 15).astype(np.uint8)
        for r in range(hh):
            row = list(al[r]) + [0]
            blob += bytes(int(row[c]) | int(row[c + 1]) << 4 for c in range(0, w, 2))
    _SPR[key] = len(SPR)
    SPR.append(blob)
    return _SPR[key]


def pack_sprites():
    out = struct.pack("<I", len(SPR))
    p = 4 + 4 * len(SPR)
    for b in SPR:
        out += struct.pack("<I", p)
        p += len(b) + (-len(b) % 4)
    return out + b"".join(b + bytes(-len(b) % 4) for b in SPR)


# what the events act on: name -> (kind, number); the kinds are src/world.h's O_*
CHESTS = [f"minor chest {k}" for k in range(1, 8)] + [f"Chest {k}" for k in range(1, 8)] + \
         [f"red chest {k}" for k in range(1, 8)]
BOSSES = {"boss 1": 1, "seagoon boss": 2, "guard chest": 3, "guard chest 2": 4, "brow boy": 5, "guard chest 3": 6,
          "showstopper": 7, "showstopper 2": 8, "snowman": 9, "snowman 2": 11, "showstopper 3": 12,
          "guard chest 4": 13, "owl": 14, "ch": 15, "Active 30": 19, "4": 10}
BARRIERS = {"barrier 1": 1, "barrier 2": 2, "barrier 3": 3, "fence block": 4, "Active 29": 5, "final barrier": 6,
            "final barrier 2": 7, "final barrier 3": 8, "final barrier 4": 9, "locked": 10}
ZONES = {"+ 0": 0, "+2": 2, "+ 5": 5, "+ 8": 8, "+ 11": 11, "+ 14": 14, "+ 17": 17, "+ 20": 20, "+ 23": 23,
         "+ 26": 26, "+ 40": 50}
KINDS = ["ZONE", "CHEST", "COINS", "BOSS", "SWITCH", "BARRIER", "SHOP", "KEY", "PORT"]
# the ways from place to place (events 1168-1185, 1395-1396, 1465-1504): touching (0), holding up a while (75 or
# 25 frames: 1, 2) or pressing down (3) on the first sends the walker to the second's place plus (dx, dy)
PORTS = [("enter mine", "mine entrance", 1, -43, 0), ("mine entrance", "enter mine", 28, 60, 0),
         ("grave entrance", "grave entrance 2", 3, 61, 0), ("grave entrance 2", "grave entrance", -8, -49, 0),
         ("mine entrance 2", "mine entrance 4", 1, -47, 0), ("mine entrance 3", "mine entrance 5", 3, -49, 0),
         ("mine entrance 4", "mine entrance 2", 0, 52, 0), ("mine entrance 5", "mine entrance 3", 29, 58, 0),
         ("whirlpool", "whirlpool 2", -1, -43, 0), ("whirlpool 2", "whirlpool", 0, 55, 0),
         ("Active 22", "circus entrance", 2, -46, 0), ("circus entrance", "Active 22", -1, 63, 0),
         ("secret", "secret 2", 77, 25, 0), ("secret 2", "secret", -25, 28, 0),
         ("enter tent", "circus dungeon entr", -1, -42, 1), ("circus dungeon entr", "enter tent", 0, 44, 0),
         ("layer 1", "layer 2", 0, -78, 1), ("layer 2", "layer 1", 1, 44, 3),
         ("layer 3", "layer 4", 1, -86, 1), ("layer 4", "layer 3", -1, 28, 3),
         ("layer 5", "layer 6", 1, -61, 1), ("layer 6", "layer 5", 0, 25, 3),
         ("enter mine 2", "enter tent 2", 1, 47, 0), ("enter tent 2", "enter mine 2", 6, -62, 2),
         ("Active 27", "Active 28", 24, 75, 0), ("Active 28", "Active 27", 25, -28, 0)]
for a, b, x, y in ((1, 9, 26, -28), (2, 7, 87, 29), (3, 8, 25, -30), (4, 10, -29, 26), (5, 11, 79, 27), (7, 2, -31, 29),
                   (8, 3, 25, 76), (9, 1, 25, 75), (10, 4, 80, 29), (11, 5, -27, 30), (6, 4, 78, 29), (12, 20, -25, 27),
                   (13, 16, 74, 31), (14, 6, -25, 27), (15, 3, 24, 77), (20, 12, 77, 29), (16, 13, -25, 28),
                   (17, 1, 24, 75), (18, 3, 25, 77), (19, 23, 25, 79), (23, 19, 24, -23), (21, 1, 23, 77),
                   (22, 24, -27, 31), (24, 22, 75, 27)):
    PORTS.append((f"treetentportal {a}", f"treetentportal {b}", x, y, 0))


def build(app):
    """-> (WORLD section bytes, defines)"""
    f = app.frames["overworld"]
    defines = []
    inst = []          # (layer, order, x, y, sprite)
    ground = []        # (x, y, w, h, sprite) scaled
    objs = []          # (kind, n, x0, y0, x1, y1, sprite0, sprite1) game coords
    walls = np.zeros((f.h // CELL, f.w // CELL), dtype=bool)

    def box(h, x, y):
        hx, hy = app.hotspot(h)
        w, hh = app.size(h)
        return x - hx, y - hy, x - hx + w, y - hy + hh

    def wall_pic(h, x0, y0, fine):
        im = app.picture(h)
        if fine:
            a = np.asarray(im)[..., 3] > 0
        else:
            a = np.ones((im.height, im.width), dtype=bool)
        H, W = a.shape
        for cy in range(max(0, y0 // CELL), min(walls.shape[0], (y0 + H + CELL - 1) // CELL)):
            for cx in range(max(0, x0 // CELL), min(walls.shape[1], (x0 + W + CELL - 1) // CELL)):
                ys, xs = cy * CELL - y0, cx * CELL - x0
                part = a[max(0, ys):max(0, ys + CELL), max(0, xs):max(0, xs + CELL)]
                if part.size and part.mean() >= 0.5:
                    walls[cy, cx] = True

    def battle_spot(x, y):
        return x < 880 and y < 560

    chest_n = {n: k + 1 for k, n in enumerate(CHESTS)}
    where = {}
    for o, x, y, layer in f.placed:
        if layer <= 2:
            where.setdefault(app.objects[o][1], (x, y))
    port_of = {}
    for a, b, dx, dy, mode in PORTS:
        bx, by = where[b]
        port_of[a] = (bx + dx, by + dy, mode)
    for order, (o, x, y, layer) in enumerate(f.placed):
        if layer > 2:
            continue
        kind, name, anims, image = app.objects[o]
        if kind == 1 and image is not None:
            if ink_hidden(app, o) and not app.obstacle[o]:
                continue
            x0, y0, x1, y1 = box(image, x, y)
            if battle_spot(x, y):
                continue
            if app.obstacle[o]:
                wall_pic(image, x0, y0, app.fine[o])
            if not ink_hidden(app, o):
                inst.append((layer, 0, order, round(x0 * S), round(y0 * S), sprite(app, image)))
        elif kind == 0 and image and image[2] is not None:
            w, hh, pat, _ = image
            if battle_spot(x, y):
                continue
            tw, th = app.size(pat)
            if app.obstacle[o]:
                for ty in range(0, hh, th):
                    for tx in range(0, w, tw):
                        wall_pic(pat, x + tx, y + ty, app.fine[o])
            if layer == 0 and (np.asarray(app.picture(pat))[..., 3] == 255).all():
                sw, sh = max(1, round(tw * S)), max(1, round(th * S))
                ground.append((round(x * S), round(y * S), round(w * S), round(hh * S),
                               sprite(app, pat, opaque=True, size=(sw, sh))))
            else:
                for ty in range(0, hh, th):
                    for tx in range(0, w, tw):
                        hx, hy = app.hotspot(pat)
                        inst.append((layer, 0, order, round((x + tx) * S), round((y + ty) * S),
                                     sprite(app, pat)))
        elif kind == 2 and anims:
            if battle_spot(x, y) or (x, y) == (0, 0):
                continue
            h = anims[min(anims)][0]
            x0, y0, x1, y1 = box(h, x, y)
            if name in chest_n:
                objs.append((1, chest_n[name], x0, y0, x1, y1, sprite(app, h), 0))
            elif name in ("small coin chest", "big coin chest"):
                continue   # (made by events)
            elif name in BOSSES:
                vis = app.visible[o]
                objs.append((3, BOSSES[name], x0, y0, x1, y1, sprite(app, h) if vis else 0xFFFF, 0))
            elif name.startswith("switch "):
                a12 = anims.get((12, 0), anims[min(anims)])
                objs.append((4, int(name.split()[1]), x0, y0, x1, y1, sprite(app, h), sprite(app, a12[0])))
            elif name in BARRIERS:
                objs.append((5, BARRIERS[name], x0, y0, x1, y1, sprite(app, h), 0))
            elif name in ZONES:
                objs.append((0, ZONES[name], x0, y0, x1, y1, 0xFFFF, 0))
            elif name in ("shop", "shop 2", "shop 3"):
                objs.append((6, {"shop": 1, "shop 2": 2, "shop 3": 3}[name], x0, y0, x1, y1, sprite(app, h), 0))
            elif name == "key":
                objs.append((7, 1, x0, y0, x1, y1, sprite(app, h), 0))
            elif name in port_of:
                tx, ty, mode = port_of[name]
                objs.append((8, mode, x0, y0, x1, y1, tx, ty))
                if app.visible[o] and not ink_hidden(app, o):
                    inst.append((layer, 1, order, round(x0 * S), round(y0 * S), sprite(app, h)))
            elif app.visible[o] and not ink_hidden(app, o):
                inst.append((layer, 1, order, round(x0 * S), round(y0 * S), sprite(app, h)))
    inst.sort(key=lambda t: (t[0], t[1], t[2]))
    # the walker: "Active 16", anim 1 in 4 directions (0 right, 8 up, 16 left, 24 down), anim 0 standing
    walker = app.obj("Active 16", "overworld")
    wa = app.anims(walker)
    first = len(SPR)
    for d in (0, 8, 16, 24):
        for h in wa[(1, d)]:
            sprite(app, h)
    stand = sprite(app, wa[(0, 24)][0])
    defines += [("SPR_WALK", first), ("WALK_N", len(wa[(1, 0)])), ("SPR_STAND", stand)]
    # on Lilygear Lake ("area" 8) the walker rides "lilly" (1161-1162): 4 directions
    la = app.anims(app.obj("lilly", "overworld"))
    first = len(SPR)
    for d in (0, 8, 16, 24):
        for h in la[(0, d)]:
            sprite(app, h)
    defines += [("SPR_LILLY", first), ("LILLY_N", len(la[(0, 0)]))]
    hit = app.anims(app.obj("8bit hitbox", "overworld"))
    hb = hit[min(hit)][0]
    defines += [("HIT_W", app.size(hb)[0]), ("HIT_H", app.size(hb)[1]), ("HIT_HX", app.hotspot(hb)[0]),
                ("HIT_HY", app.hotspot(hb)[1])]
    for nm in ("feel top", "feel bottom", "feel left", "feel right"):
        a = app.anims(app.obj(nm, "overworld"))
        h = a[min(a)][0]
        key = nm.split()[1].upper()
        defines += [(f"FEEL_{key}_W", app.size(h)[0]), (f"FEEL_{key}_H", app.size(h)[1]),
                    (f"FEEL_{key}_HX", app.hotspot(h)[0]), (f"FEEL_{key}_HY", app.hotspot(h)[1])]
    start = next((x, y) for o, x, y, l in f.placed if app.objects[o][1] == "8bit hitbox")
    defines += [("START_X", start[0]), ("START_Y", start[1]), ("MAP_W", f.w), ("MAP_H", f.h), ("CELL", CELL)]
    # the section: counts, then ground (i16 x, y, u16 w, h, sprite), pictures (i16 x, y, u16 sprite), objects
    # (u8 kind, number, i16 x0, y0, x1, y1, u16 sprite, sprite 2), walls (bits, rows of the map)
    rows = walls.shape[0]
    stride = (walls.shape[1] + 7) // 8
    wb = np.packbits(walls, axis=1, bitorder="little")
    out = struct.pack("<HHHHHH", len(ground), len(inst), len(objs), rows, stride, 0)
    for g in ground:
        out += struct.pack("<hhHHH", *g) + b"\0\0"
    for t in inst:
        out += struct.pack("<hhH", t[3], t[4], t[5])
    for o in objs:
        out += struct.pack("<BBhhhhHH", *o)
    out += wb.tobytes()
    print(f"world: ground {len(ground)}, pictures {len(inst)}, objects {len(objs)}, sprites {len(SPR)} "
          f"({sum(len(b) for b in SPR)} bytes), walls {wb.nbytes}")
    return out, defines
