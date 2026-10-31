#!/usr/bin/env python3
"""Makes src/data.c and src/data.h: every model Temple Run draws, as a table of meshes (src/models.h).

These are plain stand-in models: boxes and wedges in the game's colours for the runner, the pursuers, the
coins and power-ups and the scenery (named after the game's scenery prefabs: treeAlone, rock, cliff...). The path pieces are named after the game's prefabs
(tools/pieces.py: M_templeStraightVert...), and their stand-ins are made from where each one has ground and
obstacles (src/floor.c, tools/floor.py): flat tops, sides going down, a box for each obstacle. Each model is a
mesh by number (data.h), its far version a second, simpler mesh. The game only draws meshes by number, so the
art can be made again by another tool writing the same table, without touching the game.

Units are the game's (a path tile is 60 across), stored x 4. Usage: models.py src"""
import math
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pieces  # noqa: E402

Q = 4             # stored units a game unit
MODELS = []       # (name, faces, far name or None)

# ---------------------------------------------------------------- colours
STONE = dict(top=0xA8977A, top2=0x9C8B6F, side=0x76684F, curb=0x8B7B60, base=0x5F5545)
PLANK = dict(top=0x8D5E35, top2=0x7B5030, side=0x5A3B20, curb=0x6E4A2A, base=0x4A321C)
CLIFF = dict(top=0x928A7C, top2=0x857D70, side=0x6C655A, curb=0x7B7366, base=0x585248)
SHIRT, TROUSERS, SKIN, HAIR, BOOTS, BELT = 0x5B7F99, 0x6F5B42, 0xD8A476, 0x3B2A1C, 0x3A2E24, 0x2E2620
FUR, FUR2, FACE, EYE = 0x3E3631, 0x2F2925, 0x8C7262, 0xF0482A
ROOT, TRUNK, BARK, LEAF, LEAF2 = 0x5B3F24, 0x6A4A2D, 0x55391F, 0x3A6B2C, 0x2F5A26
FIRE, FIRE2, VENT = 0xFF8A1E, 0xFFD448, 0x4E463C
COIN_RGB = {"Y": (0xFFD21E, 0xC89A10), "R": (0xE4352C, 0xA82018), "B": (0x3A78F2, 0x2050B0)}


# ---------------------------------------------------------------- geometry
def sub(a, b):
    return [a[i] - b[i] for i in range(3)]


def cross(a, b):
    return [a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]]


def dot(a, b):
    return sum(a[i] * b[i] for i in range(3))


def norm(a):
    n = math.sqrt(dot(a, a)) or 1
    return [x / n for x in a]


def face(pts, rgb, normal=None, two=False):
    """a convex face; its corners put clockwise seen from the side the normal points to"""
    n = cross(sub(pts[1], pts[0]), sub(pts[2], pts[0]))
    if normal is not None and dot(n, normal) < 0:
        pts = pts[::-1]
        n = [-x for x in n]
    return (pts, rgb, norm(n), two)


def quad_outward(pts, rgb, centre, two=False):
    """a face of a solid: its normal away from the solid's centre"""
    mid = [sum(p[i] for p in pts) / len(pts) for i in range(3)]
    n = cross(sub(pts[1], pts[0]), sub(pts[2], pts[0]))
    if dot(n, sub(mid, centre)) < 0:
        pts = pts[::-1]
    return face(pts, rgb, None, two)


def rot_x(p, a, o):
    y, z = p[1] - o[1], p[2] - o[2]
    c, s = math.cos(a), math.sin(a)
    return [p[0], o[1] + y * c - z * s, o[2] + y * s + z * c]


def rot_z(p, a, o):
    x, y = p[0] - o[0], p[1] - o[1]
    c, s = math.cos(a), math.sin(a)
    return [o[0] + x * c - y * s, o[1] + x * s + y * c, p[2]]


def box(x0, y0, z0, x1, y1, z1, rgb, top=None, skip="", xf=None):
    """a box's faces; skip leaves out faces: b(ottom) t(op) l r f(ront, +z) k (back, -z); xf moves corners"""
    c = [[x, y, z] for x in (x0, x1) for y in (y0, y1) for z in (z0, z1)]
    if xf:
        c = [xf(p) for p in c]
    centre = [sum(p[i] for p in c) / 8 for i in range(3)]
    sides = {"l": [0, 1, 3, 2], "r": [4, 6, 7, 5], "b": [0, 2, 6, 4], "t": [1, 5, 7, 3], "k": [0, 4, 5, 1],
             "f": [2, 3, 7, 6]}
    out = []
    for k, ids in sides.items():
        if k in skip:
            continue
        out.append(quad_outward([c[i] for i in ids], top if (k == "t" and top is not None) else rgb, centre))
    return out


def add(name, faces, far=None):
    MODELS.append((name, faces, far))


# ---------------------------------------------------------------- the runner and the pursuers
def person(phase=0.0, pose="run"):
    """a stand-in runner: legs, body, arms and head, facing +z (the camera sees the back)"""
    hip = [0, 4.3, 0]
    lean, legs, arms, body_y = 0.18, (0, 0), (0, 0), 0.0
    if pose == "run":
        s = math.sin(phase)
        legs = (0.65 * s, -0.65 * s)
        arms = (-0.75 * s, 0.75 * s)
        body_y = 0.25 * abs(math.cos(phase))
    elif pose == "jump":
        legs, arms, lean = (0.9, 0.4), (-2.4, -2.4), 0.1
    elif pose == "stumble":
        legs, arms, lean = (0.5, -0.3), (-1.4, -0.9), 0.65
    elif pose == "fall":
        legs, arms, lean = (0.2, -0.2), (-2.9, -2.9), -0.1
    out = []
    shoulder_y = hip[1] + 3.3

    def body_xf(p):
        p = [p[0], p[1] + body_y, p[2]]
        return rot_x(p, lean, [0, hip[1] + body_y, 0])

    for side, a in ((-1, legs[0]), (1, legs[1])):
        x0 = 0.15 if side > 0 else -1.25
        xf = lambda p, a=a: [p[0], p[1] + body_y, p[2]] if False else rot_x([p[0], p[1] + body_y, p[2]], a, [0, hip[1] + body_y, 0])
        out += box(x0, 0.0, -0.55, x0 + 1.1, hip[1], 0.55, TROUSERS, xf=xf, skip="")
        out += box(x0 - 0.05, 0.0, -0.6, x0 + 1.15, 0.9, 0.85, BOOTS, xf=xf)
    out += box(-1.5, hip[1] - 0.2, -0.85, 1.5, hip[1] + 0.5, 0.85, BELT, xf=body_xf, skip="b")
    out += box(-1.5, hip[1] + 0.5, -0.85, 1.5, shoulder_y, 0.85, SHIRT, xf=body_xf, skip="b")
    out += box(-0.8, shoulder_y, -0.8, 0.8, shoulder_y + 1.7, 0.8, SKIN, top=HAIR, xf=body_xf)
    out += box(-0.85, shoulder_y + 1.15, -0.9, 0.85, shoulder_y + 1.8, 0.75, HAIR, xf=body_xf, skip="b")
    for side, a in ((-1, arms[0]), (1, arms[1])):
        x0 = 1.5 if side > 0 else -2.3
        xf = lambda p, a=a: body_xf(rot_x(p, a, [0, shoulder_y - 0.3, 0]))
        out += box(x0, shoulder_y - 3.2, -0.4, x0 + 0.8, shoulder_y - 0.1, 0.4, SHIRT, xf=xf)
        out += box(x0, shoulder_y - 3.7, -0.4, x0 + 0.8, shoulder_y - 3.2, 0.4, SKIN, xf=xf)
    return out


def person_slide():
    """sliding on the back, feet first, under 4 units"""
    out = box(-1.25, 0.2, 0.0, 1.25, 1.3, 4.6, TROUSERS)
    out += box(-1.3, 0.1, 4.6, 1.3, 1.4, 5.5, BOOTS)
    xf = lambda p: rot_x(p, 1.05, [0, 0.6, 0])
    out += box(-1.5, 0.0, -0.85, 1.5, 3.4, 0.85, SHIRT, xf=xf)
    out += box(-0.8, 3.4, -0.8, 0.8, 5.0, 0.8, SKIN, top=HAIR, xf=xf)
    out += box(-2.3, 0.2, -0.4, -1.5, 3.0, 0.4, SHIRT, xf=xf)
    out += box(1.5, 0.2, -0.4, 2.3, 3.0, 0.4, SHIRT, xf=xf)
    return out


def person_down():
    """lying face down, caught or fallen"""
    xf = lambda p: rot_x(p, 1.5, [0, 0.9, 0])
    out = box(-1.25, 0.0, -0.55, 1.25, 4.3, 0.55, TROUSERS, xf=xf)
    out += box(-1.5, 4.3, -0.85, 1.5, 7.6, 0.85, SHIRT, xf=xf)
    out += box(-0.8, 7.6, -0.8, 0.8, 9.3, 0.8, SKIN, top=HAIR, xf=xf)
    return out


def pursuer(phase):
    """a stand-in pursuer: a dark, hunched four-limbed shape with a pale face and two red eyes"""
    s = math.sin(phase)
    out = []
    hip = [0, 3.0, 0]
    bxf = lambda p: rot_x([p[0], p[1] + 0.3 * abs(math.cos(phase)), p[2]], 0.75, hip)
    out += box(-1.9, 2.6, -1.4, 1.9, 6.6, 1.4, FUR, top=FUR2, xf=bxf)
    out += box(-1.3, 6.4, -0.9, 1.3, 8.4, 1.5, FUR, xf=bxf)
    out += box(-1.0, 6.7, 1.5, 1.0, 8.0, 1.9, FACE, xf=bxf, skip="k")
    for x in (-0.65, 0.35):
        out += box(x, 7.35, 1.9, x + 0.3, 7.7, 2.0, EYE, xf=bxf, skip="k")
    for side, a in ((-1, 0.8 * s), (1, -0.8 * s)):
        x0 = 0.4 if side > 0 else -1.6
        xf = lambda p, a=a: rot_x(p, a, hip)
        out += box(x0, 0.0, -0.6, x0 + 1.2, hip[1] + 0.3, 0.6, FUR2, xf=xf)
    for side, a in ((-1, -0.9 * s - 0.5), (1, 0.9 * s - 0.5)):
        x0 = 1.9 if side > 0 else -2.9
        xf = lambda p, a=a: bxf(rot_x(p, a, [0, 6.2, 0]))
        out += box(x0, 1.0, -0.5, x0 + 1.0, 6.4, 0.5, FUR, xf=xf)
    return out


# ---------------------------------------------------------------- path pieces
W = 10.0          # half the walkway's width
LEVEL = {"temple": 0.0, "plank": -15.0, "cliff": 15.0}   # (the game's GetPathHeight; prefabs are built at it)


def read_floor(src):
    """src/floor.c (tools/floor.py): each prefab's ground cells (30 rows of 30 bits, or None) and its obstacles
    (flags, height, polygon)"""
    text = Path(src, "floor.c").read_text()
    tables = {m.group(1): [int(v, 16) for v in m.group(2).split(",")]
              for m in re.finditer(r"static const uint32_t (g\d+)\[FLOOR_CELLS\] = \{([^}]*)\};", text)}
    cells = {}
    for m in re.finditer(r"^  (NULL|g\d+),   /\* (\w+) \*/", text, re.M):
        cells[m.group(2)] = None if m.group(1) == "NULL" else tables[m.group(1)]
    obstacles = {}
    for m in re.finditer(r"^  \{(\d+), (\d+), (\d+), 0, (-?[\d.]+)f, \{(.*)\}\},   /\* (\w+) \*/", text, re.M):
        pts = [(int(x) / 4, int(z) / 4) for x, z in re.findall(r"\{(-?\d+), (-?\d+)\}", m.group(5))]
        obstacles.setdefault(m.group(6), []).append((int(m.group(2)), float(m.group(4)), pts))
    return cells, obstacles


def theme_of(name):
    for t, c in (("plank", PLANK), ("cliff", CLIFF)):
        if name.startswith(t):
            return t, c
    return "temple", STONE


def ground(rows, c, depth, stripes=True):
    """a stand-in for a piece's ground: the cells' tops (in stripes 10 units deep, to show the speed) and the sides
    going down where the ground ends inside the tile"""
    on = lambda r, k: 0 <= r < 30 and 0 <= k < 30 and rows[r] >> k & 1
    out = []
    band = 5 if stripes else 30
    for b0 in range(0, 30, band):
        # runs in each row of the band, merged down the band where they are the same
        open_ = {}
        for r in list(range(b0, min(30, b0 + band))) + [None]:
            runs = set()
            if r is not None:
                k = 0
                while k < 30:
                    if on(r, k):
                        k0 = k
                        while k < 30 and on(r, k):
                            k += 1
                        runs.add((k0, k))
                    k += 1
            for run in list(open_):
                if run not in runs:
                    r0 = open_.pop(run)
                    x0, x1, z0, z1 = -30 + 2 * run[0], -30 + 2 * run[1], -30 + 2 * r0, -30 + 2 * (r if r is not None else min(30, b0 + band))
                    col = c["top"] if (b0 // band) % 2 == 0 else c["top2"]
                    out.append(face([[x0, 0, z0], [x1, 0, z0], [x1, 0, z1], [x0, 0, z1]], col, [0, 1, 0]))
            for run in runs:
                open_.setdefault(run, r)
    # the sides: along x where a row's ground ends, along z where a column's does (merged along the edge)
    def sides(edge, n_out, horiz):
        k = 0
        while k < 30:
            if edge(k):
                k0 = k
                while k < 30 and edge(k):
                    k += 1
                yield k0, k
            else:
                k += 1
    for j in range(30):
        for side in (-1, 1):
            # x edges: between columns j and j+side, along z
            e = lambda r, j=j, side=side: on(r, j) and not on(r, j + side) and 0 <= j + side < 30
            for r0, r1 in sides(e, None, False):
                x = -30 + 2 * j + (2 if side > 0 else 0)
                z0, z1 = -30 + 2 * r0, -30 + 2 * r1
                out.append(face([[x, 0, z0], [x, 0, z1], [x, depth, z1], [x, depth, z0]], c["side"], [side, 0, 0]))
            # z edges: between rows j and j+side, along x
            e = lambda k, j=j, side=side: on(j, k) and not on(j + side, k) and 0 <= j + side < 30
            for k0, k1 in sides(e, None, True):
                z = -30 + 2 * j + (2 if side > 0 else 0)
                x0, x1 = -30 + 2 * k0, -30 + 2 * k1
                out.append(face([[x0, 0, z], [x1, 0, z], [x1, depth, z], [x0, depth, z]], c["side"], [0, 0, side]))
    return out


def obstacle_shapes(flags, h, pts):
    """a stand-in for an obstacle region: roots and trunks to jump up to its height, a branch to slide under"""
    xs, zs = [p[0] for p in pts], [p[1] for p in pts]
    x0, x1, z0, z1 = min(xs), max(xs), min(zs), max(zs)
    if flags & 1 and flags & 2:
        return box(x0, 0.0, z0, x0 + 1.5, 9.0, z1, VENT) + box(x1 - 1.5, 0.0, z0, x1, 9.0, z1, VENT)   # (the fire: scene.c)
    if flags & 2:   # slide under it
        out = box(x0, 5.2, z0, x1, 7.6, z1, TRUNK, top=0x7B5A38)
        return out + box(x0, 7.4, z0, x1, 9.8, z1, LEAF, top=LEAF2, skip="b")
    if h <= 0:
        return []
    col, top = (ROOT, 0x6E4E2E) if h < 3 else (TRUNK, 0x7B5A38)
    return box(x0, 0.0, z0, x1, max(h, 1.6), z1, col, top=top, skip="b")


def path_piece(name, cells, obstacles, far=False):
    theme, c = theme_of(name)
    out = []
    rows = cells.get(name)
    level = LEVEL[theme]
    if rows:
        out = ground(rows, c, -40.0 if theme != "cliff" else -55.0, stripes=not far)
    if not far:
        for flags, h, pts in obstacles.get(name, []):
            out += obstacle_shapes(flags, h - level, pts)
        if name == "temple":
            out += temple()
    # (the game's prefabs are built at their level's height, and laid at 0)
    return [([[q[0], q[1] + level, q[2]] for q in pts], rgb, n, two) for pts, rgb, n, two in out]


# ---------------------------------------------------------------- obstacles, coins, power-ups, scenery
def fire(t):
    """the fire across a flame tower piece (jump over it or slide under it): tongues between the towers, at the
    height the game's obstacle has (from 3.5 up to 8 above the floor)"""
    out = []
    for i, x in enumerate((-8.5, -4.0, 0.5, 5.0)):
        w = 2.4 + 0.5 * math.sin(t * 2.1 + i * 1.7)
        y0, y1 = 3.6, 7.8 + 0.5 * math.sin(t * 1.3 + i)
        for k, (inset, col) in enumerate(((0.0, FIRE), (0.8, FIRE2))):
            pts = [[x + inset, y0 + inset, 0.1 * k], [x + w + 1.5 - inset, (y0 + y1) / 2, 0.1 * k], [x + inset, y1 - inset, 0.1 * k]]
            out.append(face(pts, col, [0, 0, -1], two=True))
    return out


def coin(rgb):
    """a coin: an octagon, both sides"""
    r, n = 1.7, 8
    pts = [[r * math.cos(2 * math.pi * (i + 0.5) / n), 3.0 + r * math.sin(2 * math.pi * (i + 0.5) / n), 0] for i in range(n)]
    inner = [[p[0] * 0.6, 3.0 + (p[1] - 3.0) * 0.6, 0] for p in pts]
    return [face(pts, rgb[1], [0, 0, -1], two=True), face(inner, rgb[0], [0, 0, -1], two=True)]


def magnet():
    out = box(-2.0, 2.4, -0.5, -1.0, 6.0, 0.5, 0xD02A28) + box(1.0, 2.4, -0.5, 2.0, 6.0, 0.5, 0xD02A28)
    out += box(-2.0, 6.0, -0.5, 2.0, 7.0, 0.5, 0xD02A28)
    out += box(-2.0, 1.6, -0.5, -1.0, 2.4, 0.5, 0xE0E0E0) + box(1.0, 1.6, -0.5, 2.0, 2.4, 0.5, 0xE0E0E0)
    return out


def diamond(rgb, rgb2, y=4.2, r=2.0, h=2.6):
    """a double pyramid (the invisibility, the coin bonus)"""
    out = []
    ring = [[r * math.cos(a), y, r * math.sin(a)] for a in (0, math.pi / 2, math.pi, 3 * math.pi / 2)]
    centre = [0, y, 0]
    for i in range(4):
        a, b = ring[i], ring[(i + 1) % 4]
        out.append(quad_outward([a, b, [0, y + h, 0]], rgb if i % 2 else rgb2, centre))
        out.append(quad_outward([a, b, [0, y - h, 0]], rgb2 if i % 2 else rgb, centre))
    return out


def boost():
    """an arrow pointing ahead"""
    out = box(-0.8, 3.0, -2.6, 0.8, 5.0, 0.4, 0x3FCF5F)
    centre = [0, 4.0, 1.5]
    for pts in ([[-2.2, 3.0, 0.4], [2.2, 3.0, 0.4], [0, 3.0, 3.2]], [[-2.2, 5.0, 0.4], [2.2, 5.0, 0.4], [0, 5.0, 3.2]],
                [[-2.2, 3.0, 0.4], [-2.2, 5.0, 0.4], [0, 5.0, 3.2], [0, 3.0, 3.2]],
                [[2.2, 3.0, 0.4], [0, 3.0, 3.2], [0, 5.0, 3.2], [2.2, 5.0, 0.4]],
                [[-2.2, 3.0, 0.4], [2.2, 3.0, 0.4], [2.2, 5.0, 0.4], [-2.2, 5.0, 0.4]]):
        out.append(quad_outward(pts, 0x56E070, centre))
    return out


def temple():
    """the temple the run starts from: a stone block with a dark doorway (behind the start)"""
    out = box(-34, -40, -50, 34, 46, -30, 0x8A7A5E, top=0x6F7F4A, skip="b")
    out.append(face([[-9, 0, -29.9], [9, 0, -29.9], [9, 20, -29.9], [-9, 20, -29.9]], 0x221C16, [0, 0, 1]))
    out += box(-26, 46, -46, 26, 58, -34, 0x7C6C52, skip="b")
    return out


def scenery():
    """stand-ins for the game's scenery prefabs, about their size: trees about 150 tall rising from below the
    path, rocks, cliffs, a stone head of the temple, a patch of temple ground (laid at y 0, around their cell)"""
    out = []
    for i, (h, w, col) in enumerate(((150, 44, LEAF), (140, 40, LEAF2), (160, 48, 0x447A34), (130, 38, 0x3E7030))):
        near = box(-3.5, -90, -3.5, 3.5, h - 30, 3.5, TRUNK, skip="b")
        near += box(-w / 2, h - 34, -w / 2, w / 2, h - 8, w / 2, col, top=0x4F8A3C)
        near += box(-w / 3, h - 8, -w / 3, w / 3, h, w / 3, LEAF2, top=0x5A9444, skip="b")
        far = box(-w / 2, h - 34, -w / 2, w / 2, h, w / 2, col, top=0x4F8A3C, skip="b") + box(-3.5, -60, -3.5, 3.5, h - 34, 3.5, TRUNK, skip="btfk")
        out.append((f"treeAlone{i + 1 if i else ''}", near, far))
    for i, (w, h, d) in enumerate(((40, 26, 34), (30, 18, 28), (50, 34, 40))):
        near = box(-w / 2, -40, -d / 2, w / 2, h, d / 2, 0x6C665C, top=0x847E72, skip="b")
        near += box(-w / 3, h, -d / 3, w / 4, h + 8, d / 4, 0x787266, top=0x8E887C, skip="b")
        out.append((f"rock{i + 1 if i else ''}", near, box(-w / 2, -30, -d / 2, w / 2, h, d / 2, 0x6C665C, top=0x847E72, skip="b")))
    for i, (w, h, d) in enumerate(((56, 90, 44), (48, 110, 40), (64, 80, 52))):
        near = box(-w / 2, -90, -d / 2, w / 2, h, d / 2, 0x7A7266, top=0x56743A, skip="b")
        near += box(-w / 3, h, -d / 3, w / 3, h + 14, d / 3, 0x847C70, top=0x5E7E40, skip="b")
        out.append((f"cliff{i + 1 if i else ''}", near, box(-w / 2, -60, -d / 2, w / 2, h, d / 2, 0x7A7266, top=0x56743A, skip="b")))
    near, far = [], []
    for dx, dz, h in ((-14, -8, 140), (12, -4, 125), (0, 14, 150)):
        near += box(dx - 3, -90, dz - 3, dx + 3, h - 30, dz + 3, TRUNK, skip="b")
        near += box(dx - 18, h - 32, dz - 18, dx + 18, h, dz + 18, LEAF, top=0x4F8A3C, skip="b")
        far += box(dx - 18, h - 32, dz - 18, dx + 18, h, dz + 18, LEAF, top=0x4F8A3C, skip="b")
    out.append(("overgrowthTrees", near, far))
    near = box(-16, -40, -14, 16, 30, 14, 0x8C7C62, top=0x6F7F4A, skip="b")
    near += box(-12, 30, -12, 12, 40, 12, 0x9A8A70, top=0x6F7F4A, skip="b")
    near.append(face([[-8, 6, 14.1], [8, 6, 14.1], [8, 20, 14.1], [-8, 20, 14.1]], 0x3A3026, [0, 0, 1]))
    out.append(("templeHead", near, box(-16, -30, -14, 16, 40, 14, 0x8C7C62, top=0x6F7F4A, skip="b")))
    near = box(-26, -6, -26, 26, 0, 26, 0x8E7E64, top=0x9C8B6F, skip="b")
    near += box(-20, 0, 8, -6, 6, 18, 0x8C7C62, skip="b") + box(10, 0, -16, 18, 4, -4, 0x8C7C62, skip="b")
    out.append(("groundPatchTemple", near, box(-26, -6, -26, 26, 0, 26, 0x8E7E64, top=0x9C8B6F, skip="b")))
    return out


def build(src):
    # the game's path pieces first, in floor.c's order (their numbers are the same), then their far versions
    cells, obstacles = read_floor(src)
    for name in pieces.PREFABS:
        add(name, path_piece(name, cells, obstacles), name + "_far")
    for name in pieces.PREFABS:
        add(name + "_far", path_piece(name, cells, obstacles, far=True))
    for i in range(4):
        add(f"RUNNER_RUN{i}", person(i * math.pi / 2), None)
    add("RUNNER_JUMP", person(0, "jump"))
    add("RUNNER_SLIDE", person_slide())
    add("RUNNER_STUMBLE", person(0, "stumble"))
    add("RUNNER_FALL", person(0, "fall"))
    add("RUNNER_DOWN", person_down())
    add("PURSUER_RUN0", pursuer(0.0))
    add("PURSUER_RUN1", pursuer(math.pi / 2))
    add("PURSUER_RUN2", pursuer(math.pi))
    add("PURSUER_RUN3", pursuer(3 * math.pi / 2))
    for i in range(4):
        add(f"FIRE{i}", fire(i * math.pi / 2))
    for k, rgb in COIN_RGB.items():
        add(f"COIN_{k}", coin(rgb))
    add("MAGNET", magnet())
    add("INVISIBILITY", diamond(0x9FE8FF, 0x6CC4E8))
    add("BOOST", boost())
    add("COIN_BONUS", diamond(0xFFD21E, 0xE0A818, y=4.2, r=2.2, h=2.2))
    # the game's scenery (SceneryManager: Trees/...), and their far versions
    for name, faces, far in scenery():
        add(name, faces, name + "_far")
        add(name + "_far", far)


def write(src):
    build(src)
    emit(src)


def emit(src, made_by="tools/models.py", what="Temple Run's stand-in models (boxes and wedges)"):
    """MODELS as src/data.c and src/data.h"""
    names = [m[0] for m in MODELS]
    h = [f"/* Temple Run: the mesh numbers (data.c, made by {made_by}). */", "#ifndef TR_DATA_H", "#define TR_DATA_H",
         '#include "models.h"', '#include "r3d.h"', "", "enum {"]
    h += [f"  M_{n}," for n in names] + ["  M_COUNT,", f"  M_PIECES = {len(pieces.PREFABS)}   /* (the path pieces: floor.c's, in its order) */", "};", "#endif", ""]
    c = [f"/* Made by {made_by}: {what}, as a mesh table. */",
         '#include "data.h"', "", "const Texture *const r_tex[1] = {0};", ""]
    table = []
    for name, faces, far in MODELS:
        verts, index, vmap, rows, rmax = [], [], {}, [], 0.0
        for pts, rgb, n, two in faces:
            first = len(index)
            for p in pts:
                key = tuple(round(v * Q) for v in p)
                rmax = max(rmax, math.sqrt(sum((v / Q) ** 2 for v in key)))
                if key not in vmap:
                    vmap[key] = len(verts)
                    verts.append(key)
                index.append(vmap[key])
            rows.append((first, len(pts), [max(-127, min(127, round(v * 127))) for v in n], 1 if two else 0, rgb))
        low = name.lower()
        c.append(f"static const MVert v_{low}[] = {{" + ", ".join("{%d,%d,%d}" % v for v in verts) + "};")
        c.append(f"static const uint16_t i_{low}[] = {{" + ",".join(str(i) for i in index) + "};")
        c.append(f"static const MFace f_{low}[] = {{" + ", ".join(
            "{%d,%d,%d,%d,%d,%d,0,0x%06X}" % (f, n, nn[0], nn[1], nn[2], two, rgb) for f, n, nn, two, rgb in rows) + "};")
        farv = f"M_{far}" if far else "-1"
        table.append(f"  {{v_{low}, i_{low}, f_{low}, {len(rows)}, {farv}, {rmax + 0.5:.1f}f}},")
        assert len(verts) < 65536
    c += ["", "const Mesh meshes[M_COUNT] = {"] + table + ["};", ""]
    Path(src, "data.h").write_text("\n".join(h))
    Path(src, "data.c").write_text("\n".join(c))
    nf = sum(len(m[1]) for m in MODELS)
    print(f"{len(MODELS)} meshes, {nf} faces")


if __name__ == "__main__":
    write(sys.argv[1] if len(sys.argv) > 1 else "src")
