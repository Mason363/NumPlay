#!/usr/bin/env python3
"""The prize machine's characters, from Crossy Road's own models: writes src/chars.h.

    python3 -I tools/chars.py DATA [src/chars.h]

DATA is the Data folder of Crossy Road 1.3.7825 for iOS (Payload/*.app/Data in the .ipa). Needs UnityPy.

Each character is a voxel model coloured by its vertices, its voxels from 0.03 to 0.0625 of a tile (and some start
half a voxel off). Its faces are filled into
voxels, then the voxels anything can see (all but the ones under it) are covered by as few boxes as the search
finds, none overlapping, each of one colour (the hidden voxels inside take any). The boxes are checked against
the game's own drawing order (sort_boxes) in all four facings.

chars.h: the boxes of every character packed in 32 bits (in voxels from its west, bottom, south corner: x 4 bits,
y 5, z 5, width - 1: 4, height - 1: 5, depth - 1: 5, its colour 4), where each one's start (char_at), where its
corner is in half voxels and how big its voxels are, in 1/2000 tile (char_hdr), its colours (char_cols from
char_cols_at) and the colours, top and side, the way the game lights them (CHAR_PAL)."""
import itertools
import sys

# (mesh, the original's number for it) in the original's order; the Chicken first, as the one to start with
# (the ones whose boxes come to more than sort_boxes takes, or that it can't put in an order, are left out)
CHARS = [("Chicken", 19), ("Duck", 4), ("BabyDuck", 5), ("Penguin", 6), ("Zombie", 8), ("Wizard", 10),
         ("Ghost", 23), ("Kiwi", 25), ("Sheep", 30), ("Vampire", 34), ("Gray_Rabbit", 40), ("Frank", 49)]
MAXB = 40  # the boxes sort_boxes takes


def meshes(data):
    import UnityPy
    from UnityPy.helpers.MeshHelper import MeshHandler
    want = {m for m, _ in CHARS}
    out = {}
    for o in UnityPy.load(data).objects:
        if o.type.name != "Mesh":
            continue
        d = o.read()
        if d.m_Name not in want or d.m_Name in out:
            continue
        h = MeshHandler(d)
        h.process()
        if not h.m_Colors:
            continue
        V, C, tris = h.m_Vertices, h.m_Colors, h.get_triangles()
        flat = not isinstance(V[0], (list, tuple))
        n = len(V) // 3 if flat else len(V)
        vert = [tuple(V[3 * i:3 * i + 3]) if flat else tuple(V[i]) for i in range(n)]
        cflat = not isinstance(C[0], (list, tuple))
        col = [tuple(C[4 * i:4 * i + 3]) if cflat else tuple(C[i][:3]) for i in range(n)]
        col = [tuple(int(round(x * 255)) if isinstance(x, float) and x <= 1 else int(x) for x in c) for c in col]
        if tris and isinstance(tris[0], list) and tris[0] and isinstance(tris[0][0], (list, tuple)):
            tris = [t for sub in tris for t in sub]
        if tris and not isinstance(tris[0], (list, tuple)):
            tris = [tris[i:i + 3] for i in range(0, len(tris), 3)]
        out[d.m_Name] = (vert, col, [tuple(t) for t in tris])
    missing = want - set(out)
    if missing:
        raise SystemExit("not found: " + ", ".join(sorted(missing)))
    return out


def grid(vert):
    """the model's voxel size and, per axis, where its voxels start (0 or half a voxel)"""
    u = [sorted(set(round(p[k], 5) for p in vert)) for k in range(3)]
    g = min(b - a for k in range(3) for a, b in zip(u[k], u[k][1:]))
    for k in range(3):
        for v in u[k]:
            assert abs(v / g - round(v / g)) < 0.02 or abs(v / g - round(v / g + 0.5) + 0.5) < 0.02, (k, v, g)
    off = [round((u[k][0] / g) % 1 * 2) / 2 % 1 for k in range(3)]
    return g, off


def voxels(vert, col, tris):
    """voxel -> its colour (None inside, where no face says), in the model's own voxels (see grid)"""
    g, off = grid(vert)
    vert = [tuple(p[k] / g - off[k] for k in range(3)) for p in vert]
    S = 1
    surf = {}
    for t in tris:
        p = [vert[i] for i in t]
        e1 = [p[1][k] - p[0][k] for k in range(3)]
        e2 = [p[2][k] - p[0][k] for k in range(3)]
        nrm = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
        a = max(range(3), key=lambda k: abs(nrm[k]))
        if abs(nrm[a]) < 1e-12:
            continue
        sgn = 1 if nrm[a] > 0 else -1
        plane = round(p[0][a] / S)
        o = [k for k in range(3) if k != a]
        q = [(v[o[0]] / S, v[o[1]] / S) for v in p]

        def inside(x, y):
            def cr(u, v):
                return (v[0] - u[0]) * (y - u[1]) - (v[1] - u[1]) * (x - u[0])
            d = (cr(q[0], q[1]), cr(q[1], q[2]), cr(q[2], q[0]))
            return not (min(d) < 0 < max(d))
        for u in range(int(min(x for x, _ in q)) - 1, int(max(x for x, _ in q)) + 1):
            for v in range(int(min(y for _, y in q)) - 1, int(max(y for _, y in q)) + 1):
                if inside(u + 0.5, v + 0.5):
                    cell = [0, 0, 0]
                    cell[o[0]], cell[o[1]] = u, v
                    cell[a] = plane - 1 if sgn > 0 else plane
                    surf.setdefault((a, sgn, tuple(cell)), col[t[0]])
    cols, lines = {}, {}
    for (a, s, c), cl in surf.items():
        cols.setdefault(c, cl)
        if a == 0:
            lines.setdefault((c[1], c[2]), []).append((c[0], s))
    vox = {}
    for (y, z), marks in lines.items():  # inside: from a face looking west to the next looking east
        start = None
        for x, s in sorted(marks):
            if s < 0 and start is None:
                start = x
            elif s > 0 and start is not None:
                for xx in range(start, x + 1):
                    vox[(xx, y, z)] = cols.get((xx, y, z))
                start = None
    for c, cl in cols.items():
        vox.setdefault(c, cl)
    return vox


def boxes(vox):
    """the fewest boxes the search finds covering what can be seen, none overlapping"""
    def seen(c):
        x, y, z = c
        return any((x + a, y + b, z + d) not in vox for a, b, d in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, 0, 1), (0, 0, -1)))
    need = {c: vox[c] for c in vox if vox[c] is not None and seen(c)}
    taken, out, left = set(), [], set(need)

    def grow(c, k, order):
        lo, hi = list(c), [c[0] + 1, c[1] + 1, c[2] + 1]

        def ok(q):
            return q in vox and q not in taken and need.get(q, k) == k
        changed = True
        while changed:
            changed = False
            for ax in order:
                for d in (1, -1):
                    while True:
                        rng = [range(lo[i], hi[i]) for i in range(3)]
                        rng[ax] = [hi[ax]] if d > 0 else [lo[ax] - 1]
                        if all(ok(q) for q in itertools.product(*rng)):
                            if d > 0:
                                hi[ax] += 1
                            else:
                                lo[ax] -= 1
                            changed = True
                        else:
                            break
        cells = list(itertools.product(*[range(lo[i], hi[i]) for i in range(3)]))
        return sum(q in need for q in cells), lo, hi, cells
    while left:
        best = max((grow(c, need[c], order) + (need[c],) for c in sorted(left)
                    for order in itertools.permutations(range(3))), key=lambda g: g[0])
        gain, lo, hi, cells, k = best
        taken.update(cells)
        left.difference_update(cells)
        out.append((lo[0], lo[1], lo[2], hi[0] - lo[0], hi[1] - lo[1], hi[2] - lo[2], k))
    return out


def game_order_ok(bx):
    """crossy.c's sort_boxes after build_models' turn r: does every step find a box to draw next?"""
    for r in range(4):
        t = []
        for (x, y, z, w, h, d, k) in bx:
            x0, x1, z0, z1 = 2 * x, 2 * (x + w), 2 * z, 2 * (z + d)
            if r == 1:
                x0, x1, z0, z1 = z0, z1, -x1, -x0
            elif r == 2:
                x0, x1, z0, z1 = -x1, -x0, -z1, -z0
            elif r == 3:
                x0, x1, z0, z1 = -z1, -z0, x0, x1
            t.append((x0, 2 * y, z0, x1 - x0, 2 * h, z1 - z0))  # (half voxels: the order is the same)

        def behind(a, b):
            return a[0] + a[3] <= b[0] or a[2] >= b[2] + b[5] or a[1] + a[4] <= b[1]
        used = set()
        for _ in t:
            for i in range(len(t)):
                if i not in used and all(j == i or j in used or not (behind(t[j], t[i]) and not behind(t[i], t[j]))
                                         for j in range(len(t))):
                    used.add(i)
                    break
            else:
                return False
    return True


def lit(c):
    """the game's top and side colours for a vertex colour (fitted to its chicken)"""
    top = tuple(min(255, int(0.8 * v + 54)) for v in c)
    side = tuple(min(255, int(0.53 * v + 6)) for v in c)
    return top, side


def rgb565(c):
    return (c[0] >> 3) << 11 | (c[1] >> 2) << 5 | c[2] >> 3


def main():
    data = sys.argv[1]
    dest = sys.argv[2] if len(sys.argv) > 2 else "src/chars.h"
    M = meshes(data)
    pal, packed, at, cols, cols_at, hdr = [], [], [0], [], [0], []
    for name, num in CHARS:
        g, off = grid(M[name][0])
        bx = boxes(voxels(*M[name]))
        if len(bx) > MAXB or not game_order_ok(bx):
            raise SystemExit(f"{name}: {len(bx)} boxes, or no drawing order")
        x0 = min(b[0] for b in bx)
        z0 = min(b[2] for b in bx)
        assert min(b[1] for b in bx) == 0 and off[1] == 0
        hdr.append((2 * x0 + round(2 * off[0]), 2 * z0 + round(2 * off[2]), round(g * 2000)))
        assert abs(g * 2000 - hdr[-1][2]) < 1e-6 and all(-128 <= v < 128 for v in hdr[-1][:2])
        own = []
        for (x, y, z, w, h, d, k) in bx:
            if k not in own:
                own.append(k)
            v = (x - x0, y, z - z0, w - 1, h - 1, d - 1, own.index(k))
            for f, bits in zip(v, (4, 5, 5, 4, 5, 5, 4)):
                assert 0 <= f < 1 << bits, (name, v)
            packed.append(v[0] | v[1] << 4 | v[2] << 9 | v[3] << 14 | v[4] << 18 | v[5] << 23 | v[6] << 28)
        for k in own:
            c = lit(k)
            if c not in pal:
                pal.append(c)
            cols.append(pal.index(c))
        at.append(len(packed))
        cols_at.append(len(cols))
        print(f"{name} ({num:03d}): {len(bx)} boxes, {len(own)} colours, voxels {g} tile")
    with open(dest, "w") as f:
        f.write("// The prize machine's characters, from Crossy Road's own models (tools/chars.py writes it).\n")
        f.write(f"#define NCHARS {len(CHARS)}\n#define NCC {len(pal)}  // colours\n#define CHAR_MAXB {MAXB}\n")
        f.write("#define CHAR_PAL \\\n")
        for i, (top, side) in enumerate(pal):
            f.write(f"  [C_CHAR + {2 * i}] = 0x{rgb565(top):04X}, [C_CHAR + {2 * i + 1}] = 0x{rgb565(side):04X}, \\\n")
        f.write("\n// boxes: x (4 bits), y (5), z (5), width - 1 (4), height - 1 (5), depth - 1 (5), colour (4), in voxels\n")
        f.write("static const uint32_t char_box[] = {\n")
        for i in range(len(CHARS)):
            vals = [f"0x{v:08X}" for v in packed[at[i]:at[i + 1]]]
            f.write(f"  // {CHARS[i][0]}\n")
            for j in range(0, len(vals), 6):
                f.write("  " + ", ".join(vals[j:j + 6]) + ",\n")
        f.write("};\n")
        f.write("static const uint16_t char_at[NCHARS + 1] = {" + ", ".join(map(str, at)) + "};\n")
        f.write("// west and south corner (half voxels), voxel (1/2000 tile)\n")
        f.write("static const int8_t char_hdr[NCHARS][3] = {" + ", ".join("{%d, %d, %d}" % h for h in hdr) + "};\n")
        f.write("static const uint8_t char_cols_at[NCHARS + 1] = {" + ", ".join(map(str, cols_at)) + "};\n")
        f.write("static const uint8_t char_cols[] = {" + ", ".join(map(str, cols)) + "};\n")
    n = 4 * len(packed) + 2 * (len(CHARS) + 1) + 3 * len(CHARS) + len(CHARS) + 1 + len(cols) + 4 * len(pal)
    print(f"{len(packed)} boxes, {len(pal)} colours: {n} bytes")


if __name__ == "__main__":
    main()
