#!/usr/bin/env python3
"""Makes src/floor.c: where the runner has ground under it on each of the game's path pieces, and their obstacles.

The game decides both with the regions its prefabs carry (Region2D: polygons in the piece's own space). The runner
is over ground while its feet are inside one of the piece's ground regions; the floor's height is the piece's
level (the game's GetPathHeight), so only where it is matters. An obstacle is a region with an Obstacle on it: the
runner hits it when its step crosses the region's edge, unless it jumps over (its feet as high as the region's
height) or slides under, as the obstacle allows; it kills or makes the runner stumble.

This reads them from the game's Android package (tools/apk.py's Package reads the prefabs) and samples the ground
every 2 units across the 60 x 60 tile: a bit a 2 x 2 cell, set when at least half of it is ground, 30 rows of 30.
The obstacles keep their polygons. Each prefab's mesh top is compared with its ground, as a look at what the game
draws there (printed).

Usage: floor.py DATA [SRC]   (DATA: the package's assets/bin/Data, unzipped; nothing in it is run)"""
import os
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.append(os.path.join(HERE, "..", "..", "subway", "tools"))   # (its unity.py; its apk.py is its own)
import apk  # noqa: E402
import pieces  # noqa: E402
import unity  # noqa: E402

N, STEP = 30, 2.0      # cells a row, their size (-30 .. 30)


def scripts(pkg):
    ids = {o.read().m_ClassName: o.path_id for o in pkg.env.objects if o.type.name == "MonoScript"}
    return ids["Region2D"], ids["Obstacle"]


def regions(pkg, name, script, obstacle_script):
    """the prefab's regions: (polygon (x, z) in its own space, its height, the obstacle's flags (must jump over, must
    slide under, kills) when it is one, else None); all of them are ground"""
    out = []
    root = [c for c in pkg.comps(pkg.prefabs[name]) if c.object_reader.type.name == "Transform"][0]

    def walk(tr, M, depth):
        lp, lr, ls = tr.m_LocalPosition, tr.m_LocalRotation, tr.m_LocalScale
        W = M @ unity.trs((lp.x, lp.y, lp.z), (lr.x, lr.y, lr.z, lr.w), (ls.x, ls.y, ls.z)) if depth else np.eye(4)
        flags, found = None, []
        for c in tr.m_GameObject.read().m_Component:
            p = c[1] if isinstance(c, (list, tuple)) else getattr(c, "component", c)
            r = p.deref()
            if r.type.name != "MonoBehaviour":
                continue
            raw = r.get_raw_data()
            sid = struct.unpack_from("<i", raw, 16)[0]
            # m_GameObject, m_Enabled, m_Script, m_Name, then the script's fields
            nl = struct.unpack_from("<i", raw, 20)[0]
            off = 24 + ((nl + 3) & ~3)
            if sid == obstacle_script:
                flags = tuple(raw[off:off + 12])   # (three bools, 4 bytes each)
            elif sid == script:
                n = struct.unpack_from("<i", raw, off)[0]   # Points (Vector2), TestPoint, Height
                pts = np.array(struct.unpack_from(f"<{2 * n}f", raw, off + 4), np.float64).reshape(n, 2)
                height = struct.unpack_from("<f", raw, off + 4 + 8 * n + 8)[0]
                xyz = np.c_[pts[:, 0], np.zeros(n), pts[:, 1], np.ones(n)] @ W.T
                found.append((xyz[:, [0, 2]], height))
        # (the game: every region is ground, and an Obstacle is the first region on its object)
        out.extend((poly, h, flags if k == 0 else None) for k, (poly, h) in enumerate(found))
        for ch in tr.m_Children:
            walk(ch.read(), W, depth + 1)
    walk(root, np.eye(4), 0)
    return out


def inside(poly, x, z):
    """the game's test: a ray across, counting crossings"""
    c = np.zeros(x.shape, bool)
    j = len(poly) - 1
    for i in range(len(poly)):
        xi, zi = poly[i]
        xj, zj = poly[j]
        hit = ((zi > z) != (zj > z)) & (x < (xj - xi) * (z - zi) / np.where(zj - zi == 0, 1e-9, zj - zi) + xi)
        c ^= hit
        j = i
    return c


def top_of(pkg, name, x, z):
    """the highest upward face of the prefab over each point (nan where there is none)"""
    h = np.full(x.shape, np.nan)
    for pos, _, _, tris, _ in pkg.meshes(name):
        for t in tris:
            a, b, c = pos[t[0]], pos[t[1]], pos[t[2]]
            n = np.cross(b - a, c - a)
            if abs(n[1]) < 0.7 * np.linalg.norm(n):
                continue
            d = (b[2] - c[2]) * (a[0] - c[0]) + (c[0] - b[0]) * (a[2] - c[2])
            if abs(d) < 1e-9:
                continue
            l1 = ((b[2] - c[2]) * (x - c[0]) + (c[0] - b[0]) * (z - c[2])) / d
            l2 = ((c[2] - a[2]) * (x - c[0]) + (a[0] - c[0]) * (z - c[2])) / d
            l3 = 1 - l1 - l2
            m = (l1 >= -1e-6) & (l2 >= -1e-6) & (l3 >= -1e-6)
            y = l1 * a[1] + l2 * b[1] + l3 * c[1]
            h = np.where(m & (np.isnan(h) | (y > h)), y, h)
    return h


def main(data, src):
    pkg = apk.Package(data)
    script, obstacle_script = scripts(pkg)
    centre = -30 + STEP / 2 + np.arange(N) * STEP
    sub = [(-0.5, -0.5), (0.5, -0.5), (-0.5, 0.5), (0.5, 0.5)]
    masks, index, obstacles, report = [], [], [], []
    for p, name in enumerate(pieces.FLOOR_PREFABS):
        rs = regions(pkg, name, script, obstacle_script)
        count = np.zeros((N, N), int)
        for dx, dz in sub:
            x, z = np.meshgrid(centre + dx, centre + dz)
            m = np.zeros(x.shape, bool)
            for poly, h, fl in rs:
                m |= inside(poly, x, z)
            count += m
        m = count >= 2
        rows = tuple(sum(1 << k for k in range(N) if m[r, k]) for r in range(N))
        if not any(rows):
            index.append(None)
        else:
            if rows not in masks:
                masks.append(rows)
            index.append(masks.index(rows))
        for poly, h, fl in rs:
            if fl is not None:
                flags = (1 if fl[0] else 0) | (2 if fl[4] else 0) | (4 if fl[8] else 0)
                assert len(poly) <= 8, name
                obstacles.append((p, flags, h, [(round(a * 4), round(b * 4)) for a, b in poly]))
        x, z = np.meshgrid(centre, centre)
        top = top_of(pkg, name, x, z)
        level = [h for poly, h, fl in rs if fl is None] or [h for poly, h, fl in rs]
        walk = np.isfinite(top) & (np.abs(top - (level[0] if level else 0)) < 3)
        report.append(f"{name:40s} {m.sum():4d} ground cells, the mesh has a walkway at {(m & walk).sum():4d} of them, "
                      f"{(~m & walk).sum():4d} more; {sum(fl is not None for _, _, fl in rs)} obstacles")
    c = ["/* Made by tools/floor.py from Temple Run's own Android package: each path piece's ground and obstacles, from",
         " * the regions its prefab carries (Region2D). Ground: a bit a 2 x 2 cell, row r from z = -30 + 2 r, bit k from",
         " * x = -30 + 2 k. Obstacles: their polygons (units x 4), the height to jump over (the game's, absolute). */",
         '#include "floor.h"', ""]
    for i, rows in enumerate(masks):
        c.append(f"static const uint32_t g{i}[FLOOR_CELLS] = {{{', '.join(f'0x{v:08X}' for v in rows)}}};")
    c += ["", f"const uint32_t *const floor_cells[FLOOR_PIECES] = {{"]
    for name, i in zip(pieces.FLOOR_PREFABS, index):
        c.append(f"  {'NULL' if i is None else f'g{i}'},   /* {name} */")
    c += ["};", "", f"const FloorObstacle floor_obstacles[{len(obstacles)}] = {{"]
    for p, flags, h, pts in obstacles:
        pp = ", ".join(f"{{{a}, {b}}}" for a, b in pts)
        c.append(f"  {{{p}, {flags}, {len(pts)}, 0, {h:.2f}f, {{{pp}}}}},   /* {pieces.FLOOR_PREFABS[p]} */")
    c += ["};", f"const int floor_nobstacles = {len(obstacles)};", ""]
    open(os.path.join(src, "floor.c"), "w").write("\n".join(c))
    h = ["/* Temple Run: the path pieces' ground and obstacles (floor.c, made by tools/floor.py). */", "#ifndef TR_FLOOR_H",
         "#define TR_FLOOR_H", "#include <stddef.h>", "#include <stdint.h>", "",
         f"#define FLOOR_PIECES {len(pieces.FLOOR_PREFABS)}   /* (data.h's first meshes, in the same order) */",
         f"#define FLOOR_CELLS {N}", "enum { FO_JUMP = 1, FO_SLIDE = 2, FO_KILL = 4 };   /* jumped over, slid under; kills */",
         "typedef struct {", "  uint8_t piece, flags, n, pad;", "  float height;", "  int16_t p[8][2];", "} FloorObstacle;",
         "/* a piece's ground cells (NULL: none) */", "extern const uint32_t *const floor_cells[FLOOR_PIECES];",
         "extern const FloorObstacle floor_obstacles[];", "extern const int floor_nobstacles;", "#endif", ""]
    open(os.path.join(src, "floor.h"), "w").write("\n".join(h))
    print("\n".join(report))
    print(f"{len(masks)} ground tables, {len(obstacles)} obstacles")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "src"))
