#!/usr/bin/env python3
"""Packs Among Us' src/data.bin, src/data.h and src/world.h from the art taken out of the game (REF), read only:

    python3 -I tools/pack.py REF

REF is the folder the extraction scripts wrote (ship.png, ship.json, ship/, spr/, spr.json, ui/, ui.json, fonts/):
the game's own pictures at 40 px a unit, the Skeld's walls, rooms, consoles, vents, doors and cameras.
Python 3 with Pillow and NumPy."""
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
REF = sys.argv[1] if len(sys.argv) > 1 else "."
PPU = 40.0


def rgb565(r, g, b):
    return ((int(r) >> 3) << 11) | ((int(g) >> 2) << 5) | (int(b) >> 3)


# ---- colour quantization: median cut, then k-means ----

def quantize(px, n, alpha=True):
    """px: (N, 4) uint8 RGBA. Returns (palette (K, 4) uint8, index (N,) uint8); entry 0 is clear when any pixel is."""
    px = np.asarray(px, dtype=np.uint8).reshape(-1, 4)
    clear = px[:, 3] < 8 if alpha else np.zeros(len(px), bool)
    solid = px[~clear].astype(np.int32)
    has_clear = bool(clear.any())
    k = n - (1 if has_clear else 0)
    if len(solid):
        if not alpha:
            solid[:, 3] = 255
        cols, inv, cnt = np.unique(solid, axis=0, return_inverse=True, return_counts=True)
        inv = inv.reshape(-1)
        if len(cols) <= k:
            centers = cols.astype(np.float64)
            lab = np.arange(len(cols))
        else:
            w = np.array([1.0, 1.2, 0.8, 1.5])
            pts = cols.astype(np.float64) * w
            boxes = [np.arange(len(cols))]
            while len(boxes) < k:
                best, bi = -1, -1
                for i, b in enumerate(boxes):
                    if len(b) < 2:
                        continue
                    rng = (pts[b].max(0) - pts[b].min(0)).max() * math.sqrt(cnt[b].sum())
                    if rng > best:
                        best, bi = rng, i
                if bi < 0:
                    break
                b = boxes.pop(bi)
                ch = np.argmax(pts[b].max(0) - pts[b].min(0))
                order = b[np.argsort(pts[b, ch], kind="stable")]
                cum = np.cumsum(cnt[order])
                cut = int(np.searchsorted(cum, cum[-1] / 2))
                cut = min(max(cut, 1), len(order) - 1)
                boxes += [order[:cut], order[cut:]]
            centers = np.array([(pts[b] * cnt[b, None]).sum(0) / cnt[b].sum() for b in boxes])
            for _ in range(6):
                d = ((pts[:, None, :] - centers[None, :, :]) ** 2).sum(2) if len(pts) * len(centers) < 4e7 else None
                if d is None:
                    break
                lab = d.argmin(1)
                for j in range(len(centers)):
                    m = lab == j
                    if m.any():
                        centers[j] = (pts[m] * cnt[m, None]).sum(0) / cnt[m].sum()
            d = ((pts[:, None, :] - centers[None, :, :]) ** 2).sum(2)
            lab = d.argmin(1)
            centers = centers / w
        pal = np.clip(np.round(centers), 0, 255).astype(np.uint8)
        idx = np.zeros(len(px), np.uint8)
        idx[~clear] = lab[inv] + (1 if has_clear else 0)
    else:
        pal = np.zeros((0, 4), np.uint8)
        idx = np.zeros(len(px), np.uint8)
    if has_clear:
        pal = np.vstack([np.zeros((1, 4), np.uint8), pal])
    return pal, idx


# ---- pictures: "Pic" {w, h, ox, oy, ncolors, flags}, RGB565 colours, alphas, then pixels ----

P_4BIT, P_OPAQUE, P_MIX = 1, 2, 4


class Pics:
    def __init__(self):
        self.blobs = []
        self.ids = {}

    def add(self, name, im, pivot=(0, 0), bits=8, opaque=False):
        im = im.convert("RGBA")
        a = np.asarray(im).reshape(-1, 4)
        ncol = 16 if bits == 4 else 256
        pal, idx = quantize(a, ncol, alpha=not opaque)
        flags = (P_4BIT if bits == 4 else 0) | (P_OPAQUE if opaque or (pal[:, 3] == 255).all() else 0)
        self._put(name, im.size, pivot, pal, idx, flags)
        return self.ids[name]

    def _put(self, name, size, pivot, pal, idx, flags):
        w, h = size
        head = struct.pack("<HHhhBBH", w, h, int(round(pivot[0])), int(round(pivot[1])), len(pal) & 255, flags, 0)
        body = b""
        if not flags & P_MIX:
            body += struct.pack("<%dH" % len(pal), *[rgb565(*c[:3]) for c in pal])
            body += bytes(int(c[3]) for c in pal)
            body += b"\0" * (-(len(head) + len(body)) % 4)
        idx = np.asarray(idx, np.uint8).reshape(h, w)
        if flags & P_4BIT:
            if w & 1:
                idx = np.hstack([idx, np.zeros((h, 1), np.uint8)])
            body += ((idx[:, 0::2] << 4) | idx[:, 1::2]).astype(np.uint8).tobytes()
        else:
            body += idx.tobytes()
        blob = head + body
        blob += b"\0" * (-len(blob) % 4)
        assert name not in self.ids, name
        self.ids[name] = len(self.blobs)
        self.blobs.append(blob)


# ---- fonts: Font {first, count, height, ascent}, Glyph[], 4-bit coverage ----

def make_font(path, size, first=32, last=126, bold=0):
    f = ImageFont.truetype(path, size)
    asc, desc = f.getmetrics()
    glyphs = []
    bits = b""
    for c in range(first, last + 1):
        ch = chr(c)
        bb = f.getbbox(ch, stroke_width=bold)
        adv = round(f.getlength(ch)) + bold
        if bb[2] <= bb[0] or bb[3] <= bb[1] or ch == " ":
            glyphs.append((0, 0, 0, 0, adv, 0))
            continue
        w, h = bb[2] - bb[0], bb[3] - bb[1]
        im = Image.new("L", (w, h), 0)
        ImageDraw.Draw(im).text((-bb[0], -bb[1]), ch, font=f, fill=255, stroke_width=bold, stroke_fill=255)
        data = bytes(v * 15 // 255 for v in im.tobytes())
        packed = bytes((data[i] << 4) | (data[i + 1] if i + 1 < len(data) else 0) for i in range(0, len(data), 2))
        glyphs.append((w, h, bb[0], bb[1] - asc, adv, len(bits)))
        bits += packed
    out = struct.pack("<BBBB", first, last - first + 1, asc + desc, asc)
    for g in glyphs:
        out += struct.pack("<BBbbBBHI", g[0], g[1], g[2], g[3], g[4], 0, 0, g[5])
    out += bits
    out += b"\0" * (-len(out) % 4)
    return out


# ---- big pictures (the Skeld's floor, the panels): 16x16 tiles of 16 colours each, the same tiles stored once ----

TILE = 16
T_CLEAR = 0x8000   # a tile with see-through pixels: colour 15 is clear


def make_tiles(im, pivot=(0, 0), back=None, name=""):
    """{u16 tw, th; i16 ox, oy; u32 tiles' offset}, a tile number each (0xFFFF: nothing there), the tiles.
    back: the colour see-through pixels get (the ship: space); else a tile keeps them clear"""
    im = im.convert("RGBA")
    a8 = np.asarray(im)[:, :, 3]
    if back is not None:
        bg = Image.new("RGBA", im.size, back + (255,))
        bg.alpha_composite(im)
        rgb = np.asarray(bg.convert("RGB"))
    else:
        rgb = np.asarray(im)[:, :, :3]
    W, H = im.size
    tw, th = (W + TILE - 1) // TILE, (H + TILE - 1) // TILE
    rgb = np.pad(rgb, ((0, th * TILE - H), (0, tw * TILE - W), (0, 0)))
    alpha = np.pad(a8, ((0, th * TILE - H), (0, tw * TILE - W)))
    tmap, tiles, seen = [], [], {}
    err = 0.0
    for ty in range(th):
        for tx in range(tw):
            a = alpha[ty * TILE:(ty + 1) * TILE, tx * TILE:(tx + 1) * TILE].reshape(-1)
            if a.max() < 8 or (back is None and a.max() < 128):
                tmap.append(0xFFFF)
                continue
            t = rgb[ty * TILE:(ty + 1) * TILE, tx * TILE:(tx + 1) * TILE].reshape(-1, 3)
            clear = (a < 128) if back is None else np.zeros(len(a), bool)
            px = np.hstack([t, np.full((len(t), 1), 255, np.uint8)])
            n = 15 if clear.any() else 16
            pal, idx = quantize(px[~clear] if clear.any() else px, n, alpha=False)
            full = np.full(len(t), 15, np.uint8)
            full[~clear] = idx
            err += float(((pal[idx][:, :3].astype(float) - t[~clear]) ** 2).sum())
            p = [rgb565(*c[:3]) for c in pal] + [0] * (16 - len(pal))
            full = full.reshape(TILE, TILE)
            data = struct.pack("<16H", *p) + ((full[:, 0::2] << 4) | full[:, 1::2]).astype(np.uint8).tobytes()
            if data not in seen:
                seen[data] = len(tiles)
                tiles.append(data)
            tmap.append(seen[data] | (T_CLEAR if clear.any() else 0))
    assert len(tiles) < 0x7FFF
    head_len = 12
    off = head_len + 2 * len(tmap)
    off += -off % 4
    blob = struct.pack("<HHhhI", tw, th, int(round(pivot[0])), int(round(pivot[1])), off)
    blob += struct.pack("<%dH" % len(tmap), *tmap)
    blob += b"\0" * (-len(blob) % 4)
    blob += b"".join(tiles)
    if name:
        print("%s: %dx%d tiles, %d stored, %d bytes, rms %.1f" % (name, tw, th, len(tiles), len(blob),
                                                                  math.sqrt(err / max(1, len(tiles) * 768))))
    return blob


# ---- the player's pictures: indices into one table of body/visor/shadow mixes, coloured at run time ----

class Mix:
    """frames of the player (the game colours red as the body, green as the visor, blue as the shadow)"""

    def __init__(self):
        self.frames = []   # (name, im, pivot)

    def add(self, name, im, pivot):
        self.frames.append((name, im.convert("RGBA"), pivot))

    def pack(self, pics):
        allpx = np.vstack([np.asarray(im).reshape(-1, 4) for _, im, _ in self.frames])
        pal, _ = quantize(allpx, 256)
        pf = pal.astype(np.float64)
        for name, im, pivot in self.frames:
            a = np.asarray(im).reshape(-1, 4).astype(np.float64)
            d = ((a[:, None, :] - pf[None, :, :]) ** 2 * np.array([1, 1, 1, 2])).sum(2)
            idx = d.argmin(1).astype(np.uint8)
            idx[a[:, 3] < 8] = 0
            pics._put(name, im.size, pivot, pal, idx, P_MIX)
        table = b"".join(struct.pack("<4B", *[int(v) for v in c]) for c in pal)
        table += b"\0" * (4 * (256 - len(pal)))
        return table, len(pal)


# ---- the world: walls, light walls, rooms, consoles, vents, doors, cameras, the bots' walking grid ----

def seg_grid(segs, x0, y0, gw, gh, margin):
    cells = [[] for _ in range(gw * gh)]
    for i, s in enumerate(segs):
        ax, ay, bx, by = s
        c0 = max(0, int(math.floor(min(ax, bx) - margin - x0)))
        c1 = min(gw - 1, int(math.floor(max(ax, bx) + margin - x0)))
        r0 = max(0, int(math.floor(min(ay, by) - margin - y0)))
        r1 = min(gh - 1, int(math.floor(max(ay, by) + margin - y0)))
        for r in range(r0, r1 + 1):
            for c in range(c0, c1 + 1):
                cells[r * gw + c].append(i)
    start, idx = [], []
    for c in cells:
        start.append(len(idx))
        idx += c
    start.append(len(idx))
    return start, idx


def point_seg_dist(px, py, segs):
    """distances from points (N,) to segments (M, 4): (N, M)"""
    a = segs[:, 0:2][None, :, :]
    b = segs[:, 2:4][None, :, :]
    p = np.stack([px, py], 1)[:, None, :]
    ab = b - a
    t = ((p - a) * ab).sum(2) / np.maximum((ab * ab).sum(2), 1e-12)
    t = np.clip(t, 0, 1)
    q = a + ab * t[:, :, None]
    return np.sqrt(((p - q) ** 2).sum(2))


def seg_cross(p, q, segs):
    """does p-q cross any of the segments"""
    a, b = segs[:, 0:2], segs[:, 2:4]

    def orient(u, v, w):
        return (v[..., 0] - u[..., 0]) * (w[..., 1] - u[..., 1]) - (v[..., 1] - u[..., 1]) * (w[..., 0] - u[..., 0])
    p = np.asarray(p, float)
    q = np.asarray(q, float)
    d1 = orient(a, b, p[None])
    d2 = orient(a, b, q[None])
    d3 = orient(p[None], q[None], a)
    d4 = orient(p[None], q[None], b)
    return bool((((d1 > 0) != (d2 > 0)) & ((d3 > 0) != (d4 > 0))).any())


def fl(v):
    t = "%.6g" % float(v)
    return (t if ("." in t or "e" in t) else t + ".0") + "f"


def make_world(m, out):
    walls = np.array(m["walls"], float)
    shadows = np.array(m["shadows"], float)
    doors = m["doors"]
    door_segs = np.array([s for d in doors for s in d["segs"]], float)
    gx0, gy0 = -27.0, -21.0
    gw, gh = 50, 30
    lines = []
    w = lines.append
    w("/* Among Us: the Skeld's walls, rooms, consoles, vents, doors and cameras (made by tools/pack.py from the")
    w(" * game's ShipStatus prefab) */")
    w("#ifndef AU_WORLD_H\n#define AU_WORLD_H\n#include \"ship.h\"")
    w("#define GRID_X0 %s\n#define GRID_Y0 %s\n#define GRID_W %d\n#define GRID_H %d" % (fl(gx0), fl(gy0), gw, gh))
    for name, segs, margin in (("WALL", walls, 0.35), ("SHADOW", shadows, 0.05)):
        start, idx = seg_grid(segs, gx0, gy0, gw, gh, margin)
        w("#define N_%s %d" % (name, len(segs)))
        w("static const Seg %s_SEGS[%d] = {%s};" % (name, len(segs), ", ".join(
            "{%s, %s, %s, %s}" % tuple(fl(v) for v in s) for s in segs)))
        w("static const uint16_t %s_START[%d] = {%s};" % (name, len(start), ", ".join(map(str, start))))
        w("static const uint16_t %s_IDX[%d] = {%s};" % (name, len(idx), ", ".join(map(str, idx))))
    # doors
    w("#define N_DOORS %d" % len(doors))
    w("static const DoorRec DOORS[%d] = {%s};" % (len(doors), ", ".join(
        "{%s, %s, %d, %d, {%s}}" % (fl(d["x"]), fl(d["y"]), d["room"], 1 if d["kind"] == "front" else 0,
                                     ", ".join("{%s}" % ", ".join(fl(v) for v in s) for s in d["segs"]))
        for d in doors)))
    # rooms (the area colliders, for the room's name and who is where)
    pts = []
    recs = []
    for r in m["rooms"]:
        for poly in r["area"]:
            recs.append((r["id"], len(pts), len(poly), r["name"]))
            pts += poly
    w("#define N_AREAS %d" % len(recs))
    w("static const AreaRec AREAS[%d] = {%s};" % (len(recs), ", ".join("{%d, %d, %d}" % r[:3] for r in recs)))
    w("static const V2 AREA_PTS[%d] = {%s};" % (len(pts), ", ".join("{%s, %s}" % (fl(p[0]), fl(p[1])) for p in pts)))

    # the bots' grid: cells of half a unit a player can stand in, reached from the spawn
    NX0, NY0, CS = -26.0, -20.0, 0.5
    nw, nh = 96, 58
    cx = NX0 + (np.arange(nw) + 0.5) * CS
    cy = NY0 + (np.arange(nh) + 0.5) * CS
    gxx, gyy = np.meshgrid(cx, cy)
    px, py = gxx.reshape(-1), gyy.reshape(-1)
    allw = np.vstack([walls, door_segs])
    dmin = np.full(len(px), 99.0)
    for i in range(0, len(walls), 64):
        dmin = np.minimum(dmin, point_seg_dist(px, py, walls[i:i + 64]).min(1))
    clear = (dmin >= 0.2).reshape(nh, nw)
    # flood from the spawn, through moves that cross no wall
    sx, sy = int((-0.72 - NX0) / CS), int((0.62 - 1.6 - NY0) / CS)
    reach = np.zeros((nh, nw), bool)
    stack = [(sx, sy)]
    assert clear[sy, sx]
    reach[sy, sx] = True
    near = {}
    while stack:
        x, y = stack.pop()
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (1, -1), (-1, 1), (-1, -1)):
            nx, ny = x + dx, y + dy
            if not (0 <= nx < nw and 0 <= ny < nh) or reach[ny, nx] or not clear[ny, nx]:
                continue
            if dx and dy and not (clear[y, nx] and clear[ny, x]):
                continue
            p = (cx[x], cy[y])
            q = (cx[nx], cy[ny])
            if seg_cross(p, q, walls):
                continue
            reach[ny, nx] = True
            stack.append((nx, ny))
    walk = reach
    print("nav: %d cells walkable" % walk.sum())
    bitsrow = (nw + 7) // 8
    nav = bytearray(bitsrow * nh)
    for y in range(nh):
        for x in range(nw):
            if walk[y, x]:
                nav[y * bitsrow + x // 8] |= 1 << (x & 7)
    w("#define NAV_X0 %s\n#define NAV_Y0 %s\n#define NAV_CS %s\n#define NAV_W %d\n#define NAV_H %d" % (
        fl(NX0), fl(NY0), fl(CS), nw, nh))
    w("static const uint8_t NAV_BITS[%d] = {%s};" % (len(nav), ", ".join(map(str, nav))))

    def stand_point(x, y, dist, below):
        """where a bot stands to use something at (x, y): clear of walls, in reach, and a straight walk from a
        walkable cell"""
        best = None
        g = np.arange(-dist, dist + 1e-6, 0.05)
        qx, qy = np.meshgrid(x + g, y + g)
        qx, qy = qx.reshape(-1), qy.reshape(-1)
        d = np.hypot(qx - x, qy - y)
        ok = d <= dist * 0.9
        if below:
            ok &= qy <= y
        qx, qy, d = qx[ok], qy[ok], d[ok]
        near = walls[(np.minimum(walls[:, 0], walls[:, 2]) < x + 3) & (np.maximum(walls[:, 0], walls[:, 2]) > x - 3) &
                     (np.minimum(walls[:, 1], walls[:, 3]) < y + 3) & (np.maximum(walls[:, 1], walls[:, 3]) > y - 3)]
        cl = point_seg_dist(qx, qy, near).min(1) if len(near) else np.full(len(qx), 9.0)
        cells = [(cx[xx], cy[yy]) for yy in range(nh) for xx in range(nw)
                 if walk[yy, xx] and abs(cx[xx] - x) < dist + 1.5 and abs(cy[yy] - y) < dist + 1.5]
        for k in np.argsort(d):
            if cl[k] < 0.18:
                continue
            for c in sorted(cells, key=lambda c: math.hypot(c[0] - qx[k], c[1] - qy[k])):
                if math.hypot(c[0] - qx[k], c[1] - qy[k]) > 1.2:
                    break
                if not seg_cross(c, (qx[k], qy[k]), near):
                    best = (qx[k], qy[k])
                    break
            if best:
                break
        if best is None:
            print("note: nothing to stand on near (%.2f, %.2f)" % (x, y))
            return x, y
        return best

    # consoles
    KIND = {"task": 0, "MapRoomConsole": 1, "EmergencyConsole": 2, "SurvConsole": 3}
    cons = [c for c in m["consoles"] if c["kind"] in KIND]
    rows = []
    for c in cons:
        tmask = 0
        for t in c.get("types", []):
            tmask |= 1 << t
        valid = [(v["taskType"], v["taskStep"]["min"], v["taskStep"]["max"]) for v in c.get("valid", [])]
        valid += [(255, 0, 0)] * (2 - len(valid))
        sx_, sy_ = stand_point(c["x"], c["y"], c["dist"], c["below"])
        rows.append("{%s, %s, %s, %s, %s, %du, %d, %d, %d, %d, %d, {{%d, %d, %d}, {%d, %d, %d}}}" % (
            fl(c["x"]), fl(c["y"]), fl(c["dist"]), fl(sx_), fl(sy_), tmask, KIND[c["kind"]], c.get("id", 0),
            c.get("room", 0), c["below"], c.get("imp", 0), *valid[0], *valid[1]))
    w("#define N_CONSOLES %d" % len(cons))
    w("static const ConsoleRec CONSOLES[%d] = {%s};" % (len(cons), ", ".join(rows)))
    w("/* " + ", ".join("%d %s" % (i, c["name"]) for i, c in enumerate(cons)) + " */")
    # vents
    vents = m["vents"]
    vrows = []
    for v in vents:
        sx_, sy_ = stand_point(v["x"], v["y"], 0.75, 0)
        vrows.append("{%s, %s, %s, %s, {%d, %d, %d}}" % (fl(v["x"]), fl(v["y"]), fl(sx_), fl(sy_), *v["links"]))
    w("#define N_VENTS %d" % len(vents))
    w("static const VentRec VENTS[%d] = {%s};" % (len(vents), ", ".join(vrows)))
    w("/* " + ", ".join("%d %s" % (v["id"], v["name"]) for v in vents) + " */")
    cams = m["cams"]
    w("#define N_CAMS %d" % len(cams))
    w("static const CamRec CAMS[%d] = {%s};" % (len(cams), ", ".join(
        "{%s, %s, %s, %s}" % (fl(c["x"] + c["off"][0]), fl(c["y"] + c["off"][1]), fl(c["size"]), fl(c["aspect"]))
        for c in cams)))
    w("#endif")
    open(out, "w").write("\n".join(lines) + "\n")


def cname(s):
    return "".join(c if c.isalnum() else "_" for c in s).upper()


# the minigames' panels sit with their prefab's root at this screen point (40 px a unit)
MG_X, MG_Y = 160, 112


def main():
    pics = Pics()
    tiles = []      # (name, blob)
    defs = []       # data.h lines
    D = defs.append
    # the ship
    sm = json.load(open(os.path.join(REF, "ship.json")))
    tiles.append(("SHIP", make_tiles(Image.open(os.path.join(REF, "ship.png")), back=(0, 0, 0), name="ship")))
    ox, oy = sm["origin"]
    # objects in front of players behind them: where they cover the floor (the floor's tiles show them)
    front = []
    masks = b""
    for f in sm["front"]:
        im = Image.open(os.path.join(REF, "ship", f["img"] + ".png")).convert("RGBA")
        a = np.asarray(im)[:, :, 3] >= 128
        w = (im.width + 7) // 8 * 8
        a = np.pad(a, ((0, 0), (0, w - im.width)))
        front.append((len(masks), f["x"], f["y"], im.width, im.height, f["z"]))
        masks += np.packbits(a, axis=1, bitorder="little").tobytes()
    tiles.append(("FRONT_MASKS", masks + b"\0" * (-len(masks) % 4)))
    door_ids = {}
    for kind in ("side", "front"):
        dm = sm["door_" + kind]
        ids = []
        for i, fr in enumerate(dm["frames"]):
            im = Image.open(os.path.join(REF, "ship", "door_%s%02d.png" % (kind, i)))
            ids.append(pics.add("door_%s%02d" % (kind, i), im, (-fr["dx"], -fr["dy"]), bits=4))
        door_ids[kind] = ids
    vent_ids = []
    for i, fr in enumerate(sm["vent_frames"]):
        im = Image.open(os.path.join(REF, "ship", "vent%02d.png" % i))
        vent_ids.append(pics.add("vent%02d" % i, im, (-fr["dx"], -fr["dy"]), bits=4))
    # the player's frames, at 0.7 (the player's scale) x 0.4
    spr = json.load(open(os.path.join(REF, "spr.json")))
    mix = Mix()

    def mixframe(name, scale=0.28, as_name=None):
        im = Image.open(os.path.join(REF, "spr", name + ".png")).convert("RGBA")
        piv = spr[name]["pivot"]
        k = scale * 100.0 / spr[name]["ppu"]
        w, h = max(1, round(im.width * k)), max(1, round(im.height * k))
        mix.add(as_name or name, im.resize((w, h), Image.LANCZOS), (piv[0] * w / im.width, piv[1] * h / im.height))
    mixframe("idle")
    for i in range(1, 13):
        mixframe("walkcolor%04d" % i)
    for i in range(1, 49, 2):
        mixframe("ghostbob%04d" % i)
    mixframe("Dead0040")
    for i in range(1, 8):
        mixframe("getinVent%04d" % i)
    mixframe("idle", 0.7, "idle_big")
    mix_table, nmix = mix.pack(pics)

    # the minigames: a tiled background, its moving parts, more frames
    mg = json.load(open(os.path.join(REF, "mg.json")))
    for name, m in mg.items():
        N = cname(name)
        if m["bg"]:
            im = Image.open(os.path.join(REF, "mg", name + "_bg.png"))
            bx, by = m["bg"]["ox"], m["bg"]["oy"]
            # (only what the screen shows: the panel's root at MG_X, MG_Y, the key line at the bottom)
            x0, y0 = max(0, int(bx - MG_X)), max(0, int(by - MG_Y))
            x1, y1 = min(im.width, int(bx + SCREEN_W_PX - MG_X)), min(im.height, int(by + 228 - MG_Y))
            im = im.crop((x0, y0, x1, y1))
            tiles.append(("MG_" + N, make_tiles(im, (bx - x0, by - y0), name="mg " + name)))
        for key, p in m["parts"].items():
            im = Image.open(os.path.join(REF, "mg", "%s_%s.png" % (name, key)))
            pid = pics.add("mg_%s_%s" % (name, key), im, (p["px"], p["py"]), bits=8 if im.width * im.height < 6000 else 4)
            D("#define PIC_MG_%s_%s %d" % (N, cname(key), pid))
            D("#define MG_%s_%s_X %d\n#define MG_%s_%s_Y %d" % (N, cname(key), round(MG_X + p["x"] * PPU), N,
                                                                cname(key), round(MG_Y - p["y"] * PPU)))
            if len(p["all"]) > 1:
                D("static const int16_t MG_%s_%s_XY[%d][2] = {%s};" % (N, cname(key), len(p["all"]), ", ".join(
                    "{%d, %d}" % (round(MG_X + x * PPU), round(MG_Y - y * PPU)) for x, y in p["all"])))
        for nm, e in m["extra"].items():
            im = Image.open(os.path.join(REF, "mg", "%s_x_%s.png" % (name, nm)))
            pid = pics.add("x_" + nm, im, (e["px"], e["py"]), bits=8 if im.width * im.height < 6000 else 4)
            D("#define PIC_X_%s %d" % (cname(nm), pid))
    # fonts
    fdir = os.path.join(REF, "fonts")
    fonts = [make_font(os.path.join(fdir, "LiberationSans-Regular.ttf"), 10),
             make_font(os.path.join(fdir, "LiberationSans-Bold.ttf"), 11),
             make_font(os.path.join(fdir, "LiberationSans-Bold.ttf"), 14),
             make_font(os.path.join(fdir, "LiberationSans-Bold.ttf"), 24)]

    # data.bin: "AUSD", counts, offsets: pictures, fonts, tiled pictures, the mix table
    blobs = pics.blobs + fonts + [b for _, b in tiles] + [mix_table]
    head = struct.pack("<4sIII", b"AUSD", len(pics.blobs), len(fonts), len(blobs))
    pos = len(head) + 4 * len(blobs)
    offs, body = [], b""
    for b in blobs:
        body += b"\0" * (-(pos + len(body)) % 4)
        offs.append(pos + len(body))
        body += b
    out = head + struct.pack("<%dI" % len(offs), *offs) + body
    open(os.path.join(SRC, "data.bin"), "wb").write(out)
    with open(os.path.join(SRC, "data.h"), "w") as f:
        f.write("/* Among Us: what's in data.bin (made by tools/pack.py) */\n#ifndef AU_DATA_H\n#define AU_DATA_H\n")
        f.write("#include <stdint.h>\n")
        f.write("#define N_PICS %d\n#define N_FONTS %d\n" % (len(pics.blobs), len(fonts)))
        base = len(pics.blobs) + len(fonts)
        for k, (nm, _) in enumerate(tiles):
            f.write("#define BLOB_%s %d\n" % (nm, base + k))
        f.write("#define BLOB_MIX %d\n#define N_MIX %d\n" % (len(blobs) - 1, nmix))
        f.write("#define MG_X %d\n#define MG_Y %d\n" % (MG_X, MG_Y))
        f.write("enum { FONT_SMALL, FONT_MID, FONT_BIG, FONT_HUGE };\n")
        f.write("#define SHIP_OX %d\n#define SHIP_OY %d\n" % (round(ox), round(oy)))
        f.write("#define PIC_IDLE %d\n#define PIC_WALK %d\n#define PIC_GHOST %d\n#define N_GHOST 24\n#define PIC_DEAD %d\n"
                "#define PIC_JUMP %d\n#define PIC_IDLE_BIG %d\n" % (
                    pics.ids["idle"], pics.ids["walkcolor0001"], pics.ids["ghostbob0001"], pics.ids["Dead0040"],
                    pics.ids["getinVent0001"], pics.ids["idle_big"]))
        f.write("#define PIC_DOOR_SIDE %d\n#define PIC_DOOR_FRONT %d\n#define DOOR_CLOSE_FRAMES %d\n"
                "#define DOOR_OPEN_FRAMES %d\n" % (door_ids["side"][0], door_ids["front"][0],
                                                   sm["door_side"]["close"], sm["door_side"]["open"]))
        f.write("#define PIC_VENT %d\n#define N_VENT_FRAMES %d\n" % (vent_ids[0], len(vent_ids)))
        f.write("/* where an object in front covers the floor: a bit a pixel, rows of whole bytes, lowest bit first */\n")
        f.write("typedef struct { uint32_t off; int16_t x, y; uint16_t w, h; float z; } FrontRec;\n")
        f.write("#define N_FRONT %d\nstatic const FrontRec FRONT[%d] = {%s};\n" % (len(front), len(front), ", ".join(
            "{%d, %d, %d, %d, %d, %s}" % (o, x, y, w, h, fl(z)) for o, x, y, w, h, z in front)))
        f.write("\n".join(defs) + "\n")
        f.write("#endif\n")
    make_world(sm, os.path.join(SRC, "world.h"))
    print("data.bin: %d bytes (pictures %d, fonts %d, tiled %d)" % (
        len(out), sum(len(b) for b in pics.blobs), sum(len(b) for b in fonts), sum(len(b) for _, b in tiles)))


SCREEN_W_PX = 320

if __name__ == "__main__":
    main()
