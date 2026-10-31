#!/usr/bin/env python3
"""Makes src/data.c and src/data.h: every model Temple Run draws, as a table of meshes (src/models.h).

These are plain stand-in models: boxes and wedges in the game's colours for the runner, the pursuers, the
path pieces of its three places (the temple, the plank bridges, the cliffs), the roots, trees, branches and
fire, the coins and power-ups, the jungle beside the path. Each model is a mesh by number (data.h), its
far version a second, simpler mesh. The game only draws meshes by number, so the art can be made again by
another tool writing the same table, without touching the game.

Units are the game's (a path tile is 60 across), stored x 4. Usage: models.py src"""
import math
import sys
from pathlib import Path

Q = 4             # stored units a game unit
MODELS = []       # (name, faces, far name or None)

# ---------------------------------------------------------------- colours
STONE = dict(top=0xA8977A, top2=0x9C8B6F, side=0x76684F, curb=0x8B7B60, base=0x5F5545)
PLANK = dict(top=0x8D5E35, top2=0x7B5030, side=0x5A3B20, curb=0x6E4A2A, base=0x4A321C)
CLIFF = dict(top=0x928A7C, top2=0x857D70, side=0x6C655A, curb=0x7B7366, base=0x585248)
THEMES = [("TEMPLE", STONE), ("PLANK", PLANK), ("CLIFF", CLIFF)]
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
DEPTH = -42.0     # how far the stone goes down into the haze


def floor(x0, z0, x1, z1, c, stripes=True, y=0.0):
    """a walkway's top, in stripes across the run (they show the speed)"""
    out = []
    if not stripes:
        return [face([[x0, y, z0], [x1, y, z0], [x1, y, z1], [x0, y, z1]], c["top"], [0, 1, 0])]
    n = max(1, round(abs(z1 - z0) / 10)) if abs(z1 - z0) >= abs(x1 - x0) else max(1, round(abs(x1 - x0) / 10))
    along_z = abs(z1 - z0) >= abs(x1 - x0)
    for i in range(n):
        if along_z:
            a, b = z0 + (z1 - z0) * i / n, z0 + (z1 - z0) * (i + 1) / n
            pts = [[x0, y, a], [x1, y, a], [x1, y, b], [x0, y, b]]
        else:
            a, b = x0 + (x1 - x0) * i / n, x0 + (x1 - x0) * (i + 1) / n
            pts = [[a, y, z0], [b, y, z0], [b, y, z1], [a, y, z1]]
        out.append(face(pts, c["top"] if i % 2 == 0 else c["top2"], [0, 1, 0]))
    return out


def wall(p0, p1, normal, c, y0=DEPTH, y1=0.0, rgb=None):
    """a side of the walkway going down from its edge, from p0 to p1 (x, z)"""
    pts = [[p0[0], y1, p0[1]], [p1[0], y1, p1[1]], [p1[0], y0, p1[1]], [p0[0], y0, p0[1]]]
    return [face(pts, rgb if rgb is not None else c["side"], [normal[0], 0, normal[1]])]


def curb(p0, p1, inward, c, theme):
    """the low border along an edge: stone blocks in the temple, rails on the bridges, rock on the cliffs"""
    (x0, z0), (x1, z1) = p0, p1
    ix, iz = inward
    t = 1.6
    lo, hi = (min(x0, x1, x0 + ix * t), max(x0, x1, x0 + ix * t)), (min(z0, z1, z0 + iz * t), max(z0, z1, z0 + iz * t))
    if theme == "PLANK":
        out = []
        along = (x1 - x0, z1 - z0)
        n = 3
        for i in range(n + 1):
            px, pz = x0 + along[0] * i / n + ix * 0.6, z0 + along[1] * i / n + iz * 0.6
            out += box(px - 0.5, 0.0, pz - 0.5, px + 0.5, 4.5, pz + 0.5, c["curb"], skip="b")
        out += box(lo[0] + (0.3 if ix else 0), 3.6, lo[1] + (0.3 if iz else 0), hi[0] - (0.3 if ix else 0) if ix else hi[0],
                   4.0, hi[1] - (0.3 if iz else 0) if iz else hi[1], c["curb"], skip="b")
        return out
    h = 1.6 if theme == "TEMPLE" else 2.4
    return box(lo[0], 0.0, lo[1], hi[0], h, hi[1], c["curb"], skip="b")


def piece(kind, c, theme):
    """the faces of one path tile (the run comes from -z)"""
    out = []
    if kind == "STRAIGHT":
        out += floor(-W, -30, W, 30, c)
        out += wall((-W, 30), (-W, -30), (-1, 0), c) + wall((W, -30), (W, 30), (1, 0), c)
        out += curb((-W, -30), (-W, 30), (1, 0), c, theme) + curb((W, -30), (W, 30), (-1, 0), c, theme)
    elif kind in ("HOLE", "HOLE_LONG"):
        g = 10 if kind == "HOLE" else 17
        for z0, z1 in ((-30, -g), (g, 30)):
            out += floor(-W, z0, W, z1, c, stripes=False)
            out += wall((-W, z1), (-W, z0), (-1, 0), c) + wall((W, z0), (W, z1), (1, 0), c)
            out += curb((-W, z0), (-W, z1), (1, 0), c, theme) + curb((W, z0), (W, z1), (-1, 0), c, theme)
        out += wall((-W, -g), (W, -g), (0, 1), c) + wall((W, g), (-W, g), (0, -1), c)
    elif kind in ("NARROW_L", "NARROW_R"):
        s = -1 if kind == "NARROW_L" else 1
        a, b = sorted((s * W, s * 2.0))
        out += floor(a, -30, b, 30, c)
        out += wall((a, 30), (a, -30), (-1, 0), c) + wall((b, -30), (b, 30), (1, 0), c)
        out += curb((s * W, -30), (s * W, 30), (-s, 0), c, theme)
        # the broken edge where the walkway fell away
        out += floor(-W, -30, W, -26, c, stripes=False) + floor(-W, 26, W, 30, c, stripes=False)
        out += wall((-s * W, -26), (s * 2.0, -26), (0, 1), c) + wall((s * 2.0, 26), (-s * W, 26), (0, -1), c)
    elif kind in ("CORNER_L", "CORNER_R"):
        s = 1 if kind == "CORNER_R" else -1
        out += floor(-W, -30, W, W, c)
        out += floor(min(s * W, s * 30), -W, max(s * W, s * 30), W, c)
        out += wall((-s * W, -30), (-s * W, W), (-s, 0), c) + wall((-s * W, W), (s * 30, W), (0, 1), c)
        out += wall((s * W, W - 2 * W), (s * W, -30), (s, 0), c) + wall((s * 30, -W), (s * W, -W), (0, -1), c)
        out += curb((-s * W, -30), (-s * W, W), (s, 0), c, theme) + curb((-s * W, W), (s * 30, W), (0, -1), c, theme)
        out += curb((s * W, -30), (s * W, -W), (-s, 0), c, theme) + curb((s * W, -W), (s * 30, -W), (0, 1), c, theme)
    elif kind == "T":
        out += floor(-W, -30, W, -W, c)
        out += floor(-30, -W, 30, W, c)
        out += wall((-30, W), (30, W), (0, 1), c)
        out += wall((-W, -W), (-W, -30), (-1, 0), c) + wall((W, -30), (W, -W), (1, 0), c)
        out += wall((-W, -W), (-30, -W), (0, -1), c) + wall((30, -W), (W, -W), (0, -1), c)
        out += curb((-30, W), (30, W), (0, -1), c, theme)
        out += curb((-W, -30), (-W, -W), (1, 0), c, theme) + curb((W, -30), (W, -W), (-1, 0), c, theme)
        out += curb((-30, -W), (-W, -W), (0, 1), c, theme) + curb((W, -W), (30, -W), (0, 1), c, theme)
    elif kind in ("STEP_UP", "STEP_DOWN"):
        # 15 units up or down over the tile, in five steps of 3 (the game's floor: game.c)
        up = kind == "STEP_UP"
        hs = [3.0 * (i + 1) if up else -3.0 * i for i in range(5)]
        for i in range(5):
            z0, z1, y = -30 + 12 * i, -18 + 12 * i, hs[i]
            out.append(face([[-W, y, z0], [W, y, z0], [W, y, z1], [-W, y, z1]], c["top"] if i % 2 == 0 else c["top2"], [0, 1, 0]))
            out += wall((-W, z1), (-W, z0), (-1, 0), c, y1=y) + wall((W, z0), (W, z1), (1, 0), c, y1=y)
            py = hs[i - 1] if i else (0.0 if up else None)
            if py is not None and y > py:
                out.append(face([[-W, py, z0], [W, py, z0], [W, y, z0], [-W, y, z0]], c["side"], [0, 0, -1]))
    return out


def cliff_face():
    """the rock wall rising on the cliff path's left"""
    out = [face([[-W - 5, 0, -30], [-W - 5, 0, 30], [-W - 9, 20, 30], [-W - 9, 20, -30]], 0x8A8274, [1, 0, 0])]
    out.append(face([[-W - 9, 20, -30], [-W - 9, 20, 30], [-W - 24, 20, 30], [-W - 24, 20, -30]], 0x56743A, [0, 1, 0]))
    return out


def far_piece(kind, c):
    """far off: only the walkway's top and its sides"""
    full = {"STRAIGHT": [(-W, -30, W, 30)], "HOLE": [(-W, -30, W, -10), (-W, 10, W, 30)],
            "HOLE_LONG": [(-W, -30, W, -17), (-W, 17, W, 30)], "NARROW_L": [(-W, -30, -2, 30)],
            "NARROW_R": [(2, -30, W, 30)], "CORNER_L": [(-W, -30, W, W), (-30, -W, -W, W)],
            "CORNER_R": [(-W, -30, W, W), (W, -W, 30, W)], "T": [(-W, -30, W, -W), (-30, -W, 30, W)]}
    out = []
    for x0, z0, x1, z1 in full.get(kind, []):
        out += floor(x0, z0, x1, z1, c, stripes=False)
    if kind in ("STRAIGHT", "HOLE", "HOLE_LONG"):
        out += wall((-W, 30), (-W, -30), (-1, 0), c, y0=-24) + wall((W, -30), (W, 30), (1, 0), c, y0=-24)
    if kind in ("STEP_UP", "STEP_DOWN"):
        a, b = (3, 15) if kind == "STEP_UP" else (0, -12)
        out.append(face([[-W, a, -30], [W, a, -30], [W, b, 30], [-W, b, 30]], c["top"], [0, 1, 0]))
    return out


# ---------------------------------------------------------------- obstacles, coins, power-ups, scenery
def root_low():
    """a root across the path, low enough to run into and stumble (jump it)"""
    out = []
    for i, (x0, x1) in enumerate(((-W, -4), (-4.5, 3.5), (3, W))):
        h = 1.6 + 0.5 * (i % 2)
        out += box(x0, 0.0, -0.9, x1, h, 0.9, ROOT, top=0x6E4E2E, skip="b")
    return out


def tree_jump():
    """a fallen trunk across the path: jump it"""
    out = box(-W - 3, 0.0, -1.8, W + 3, 4.2, 1.8, TRUNK, top=0x7B5A38, skip="b")
    out += box(-W - 4, -3.0, -2.2, -W - 1, 5.0, 2.2, BARK, skip="b")
    out += box(W + 1, -3.0, -2.2, W + 4, 5.0, 2.2, BARK, skip="b")
    return out


def branch_slide():
    """a branch across the path at head height: slide under it"""
    out = box(-W - 3, 5.2, -1.3, W + 3, 7.4, 1.3, TRUNK, top=0x7B5A38)
    for x in (-6.0, 1.0, 6.5):
        out += box(x, 7.2, -2.6, x + 3.4, 9.6, 2.6, LEAF, top=LEAF2)
    out += box(-W - 3.5, 0.0, -1.6, -W - 1.0, 7.6, 1.6, BARK, skip="b")
    out += box(W + 1.0, 0.0, -1.6, W + 3.5, 7.6, 1.6, BARK, skip="b")
    return out


def fire(t):
    """fire jets across the path from vents in the floor: jump them"""
    out = []
    for i, x in enumerate((-7.0, -2.0, 3.0)):
        out += box(x, 0.0, -1.4, x + 4.0, 0.6, 1.4, VENT, skip="b")
        h = 4.6 + 1.3 * math.sin(t * 2.1 + i * 1.7)
        cx = x + 2.0
        for k, (wd, hh, col) in enumerate(((1.7, h, FIRE), (0.9, h * 0.7, FIRE2))):
            for zs in (-1, 1):
                pts = [[cx - wd, 0.6, zs * 0.25 * (k + 1)], [cx + wd, 0.6, zs * 0.25 * (k + 1)], [cx, 0.6 + hh, 0]]
                out.append(face(pts, col, [0, 0, -1 if zs < 0 else 1], two=True))
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


def jungle_tree(tall):
    """a jungle tree rising from below the path, its leaves above"""
    out = box(-2.2, -70, -2.2, 2.2, tall, 2.2, TRUNK, skip="b")
    out += box(-9, tall - 2, -9, 9, tall + 6, 9, LEAF, top=0x447A34)
    out += box(-6, tall + 6, -6, 6, tall + 10, 6, LEAF2, top=0x4F8A3C)
    return out


def jungle_far(tall):
    return box(-9, tall - 2, -9, 9, tall + 6, 9, LEAF, top=0x447A34, skip="b") + box(-2.2, -40, -2.2, 2.2, tall - 2, 2.2, TRUNK, skip="btfk")


def pillar():
    """a broken temple pillar beside the path"""
    out = box(-3, -50, -3, 3, 14, 3, 0x8E7F66, top=0x9E8F74, skip="b")
    out += box(-4, 14, -4, 4, 16.5, 4, 0x9E8F74, skip="")
    return out


def temple():
    """the temple the run starts from: a stone block with a dark doorway (behind the start)"""
    out = box(-34, -40, -50, 34, 46, -30, 0x8A7A5E, top=0x6F7F4A, skip="b")
    out.append(face([[-9, 0, -29.9], [9, 0, -29.9], [9, 20, -29.9], [-9, 20, -29.9]], 0x221C16, [0, 0, 1]))
    out += box(-26, 46, -46, 26, 58, -34, 0x7C6C52, skip="b")
    return out


def build():
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
    kinds = ["STRAIGHT", "HOLE", "HOLE_LONG", "NARROW_L", "NARROW_R", "CORNER_L", "CORNER_R", "T", "STEP_UP", "STEP_DOWN"]
    for theme, c in THEMES:
        for k in kinds:
            add(f"{theme}_{k}", piece(k, c, theme), f"{theme}_{k}_FAR")
            add(f"{theme}_{k}_FAR", far_piece(k, c))
    add("ROOT_LOW", root_low())
    add("TREE_JUMP", tree_jump())
    add("BRANCH_SLIDE", branch_slide())
    for i in range(4):
        add(f"FIRE{i}", fire(i * math.pi / 2))
    for k, rgb in COIN_RGB.items():
        add(f"COIN_{k}", coin(rgb))
    add("MAGNET", magnet())
    add("INVISIBILITY", diamond(0x9FE8FF, 0x6CC4E8))
    add("BOOST", boost())
    add("COIN_BONUS", diamond(0xFFD21E, 0xE0A818, y=4.2, r=2.2, h=2.2))
    add("JUNGLE_TREE", jungle_tree(26), "JUNGLE_TREE_FAR")
    add("JUNGLE_TREE_FAR", jungle_far(26))
    add("JUNGLE_TALL", jungle_tree(44), "JUNGLE_TALL_FAR")
    add("JUNGLE_TALL_FAR", jungle_far(44))
    add("PILLAR", pillar())
    add("CLIFF_WALL", cliff_face())
    add("TEMPLE", temple())


def write(src):
    build()
    names = [m[0] for m in MODELS]
    h = ["/* Temple Run: the mesh numbers (data.c, made by tools/models.py). */", "#ifndef TR_DATA_H", "#define TR_DATA_H",
         '#include "models.h"', '#include "r3d.h"', "", "enum {"]
    h += [f"  M_{n}," for n in names] + ["  M_COUNT", "};", "#endif", ""]
    c = ["/* Made by tools/models.py: Temple Run's stand-in models (boxes and wedges), as a mesh table. */",
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
