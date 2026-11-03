#!/usr/bin/env python3
"""Packs Moto X3M's src/data.bin, src/data.h and src/bike_data.h from the game's HTML5 build (REF), read only:

    python3 -I tools/pack.py REF

REF holds the build's files as the web serves them: assets/levels/map1..50.json, objects.json, bike.json, 1.json;
assets/atlases/x1/theme, cars_game and garage (.png and .json); assets/images/x1/0bg_garage.png;
assets/fonts/Impact/impact.woff. Python 3 with Pillow."""
import json
import math
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from geom import nape_poly  # noqa: E402
from levels import (CB, C_FRONT, C_LAND, C_OBJ, C_SHAPES, FILTERS, Level, load, BG)  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")
NLEVELS = 50

warnings = []


def warn(s):
    warnings.append(s)


class Atlas:
    def __init__(self, ref, name):
        base = os.path.join(ref, "assets", "atlases", "x1", name)
        self.img = Image.open(base + ".png").convert("RGBA")
        d = load(base + ".json")
        fr = d["frames"]
        self.frames = fr if isinstance(fr, dict) else {f["filename"]: f for f in fr}

    def has(self, name):
        return name in self.frames

    def get(self, name):
        """a frame as its full source picture (trimmed borders back) and its size"""
        f = self.frames[name]
        r = f["frame"]
        if f.get("rotated"):
            raise ValueError("rotated frame " + name)
        crop = self.img.crop((r["x"], r["y"], r["x"] + r["w"], r["y"] + r["h"]))
        ss = f.get("sourceSize", {"w": r["w"], "h": r["h"]})
        sp = f.get("spriteSourceSize", {"x": 0, "y": 0})
        full = Image.new("RGBA", (ss["w"], ss["h"]), (0, 0, 0, 0))
        full.paste(crop, (sp["x"], sp["y"]))
        return full


# ---- pictures ----

class Pics:
    def __init__(self):
        self.list = []      # (name, bytes)
        self.ids = {}

    def add(self, name, im, pivot, scale, tile=False, colors=255):
        """im: RGBA source; pivot in source pixels; stored scaled, cropped to its visible box unless a tile"""
        if name in self.ids:
            return self.ids[name]
        w, h = im.size
        sw, sh = max(1, round(w * scale)), max(1, round(h * scale))
        if tile:
            sw, sh = 1 << max(0, round(math.log2(sw))), 1 << max(0, round(math.log2(sh)))
        im = premul_resize(im, (sw, sh))
        px, py = pivot[0] * sw / w, pivot[1] * sh / h
        ox = oy = 0
        if not tile:
            bbox = im.getchannel("A").point(lambda a: 255 if a > 8 else 0).getbbox()
            if bbox is None:
                bbox = (0, 0, 1, 1)
            im = im.crop(bbox)
            ox, oy = bbox[0], bbox[1]
        data = encode(im, px - ox, py - oy, tile, colors)
        self.ids[name] = len(self.list)
        self.list.append((name, data))
        return self.ids[name]

    def blobs(self):
        """each picture's bytes once: repeated frames share them"""
        seen = {}
        out = []
        for name, data in self.list:
            out.append(seen.setdefault(data, len(seen)))
        uniq = sorted(seen, key=seen.get)
        return out, uniq


def premul_resize(im, size):
    if im.size == size:
        return im
    a = np.asarray(im, dtype=np.float32) / 255
    pm = a.copy()
    pm[..., :3] *= pm[..., 3:4]
    r = np.stack([np.asarray(Image.fromarray((pm[..., k] * 255).astype(np.uint8), "L").resize(size, Image.LANCZOS),
                             dtype=np.float32) / 255 for k in range(4)], axis=-1)
    al = r[..., 3:4]
    rgb = np.where(al > 0, np.clip(r[..., :3] / np.maximum(al, 1e-6), 0, 1), 0)
    out = np.concatenate([rgb, al], axis=-1)
    return Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA")


def rgb565(r, g, b):
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def encode(im, px, py, tile, colors):
    """w, h, pivot, colours (RGB565 + alpha), then a byte a pixel"""
    w, h = im.size
    # alpha in 5 steps so the drawing can skip, write or blend quickly
    arr = np.asarray(im).copy()
    al = arr[..., 3].astype(np.int32)
    al = np.where(al < 24, 0, np.where(al > 232, 255, np.minimum(255, (al + 32) // 64 * 64)))
    arr[..., 3] = al
    arr[al == 0, :3] = 0
    q = Image.fromarray(arr, "RGBA")
    if colors <= 16:
        pal_img = q.convert("RGB").quantize(colors=colors, method=Image.Quantize.MEDIANCUT,
                                            dither=Image.Dither.FLOYDSTEINBERG)
        if q.getchannel("A").getextrema()[0] < 255:
            pal_img = q.quantize(colors=colors, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.FLOYDSTEINBERG)
    else:
        pal_img = q.quantize(colors=colors, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
    pal = pal_img.getpalette(rawmode="RGBA")
    n = len(pal) // 4
    idx = pal_img.tobytes()
    used = max(idx) + 1 if idx else 1
    n = min(n, used)
    opaque = all(pal[4 * i + 3] == 255 for i in range(n) if i in set(idx))
    flags = (1 if tile else 0) | (2 if opaque else 0)
    if n <= 16:
        flags |= 4    # two pixels a byte, the left one in the high bits
        rows = []
        for y in range(h):
            row = idx[y * w:(y + 1) * w] + (b"\0" if w & 1 else b"")
            rows.append(bytes((row[i] << 4) | row[i + 1] for i in range(0, len(row), 2)))
        idx = b"".join(rows)
    out = struct.pack("<HHffHBB", w, h, px, py, n, flags, 0)
    out += b"".join(struct.pack("<H", rgb565(pal[4 * i], pal[4 * i + 1], pal[4 * i + 2])) for i in range(n))
    out += bytes(min(255, pal[4 * i + 3]) for i in range(n))
    out += b"\0" * (-len(out) % 4)
    out += idx
    out += b"\0" * (-len(out) % 4)
    return out


def anchor_pivot(im, ax, ay):
    return (ax * im.size[0], ay * im.size[1])


# ---- fonts ----

def make_font(path, size, first=32, last=126):
    f = ImageFont.truetype(path, size)
    asc, desc = f.getmetrics()
    glyphs = []
    bits = b""
    for c in range(first, last + 1):
        ch = chr(c)
        bb = f.getbbox(ch)
        adv = round(f.getlength(ch))
        if bb[2] <= bb[0] or bb[3] <= bb[1]:
            glyphs.append((0, 0, 0, 0, adv, len(bits)))
            continue
        w, h = bb[2] - bb[0], bb[3] - bb[1]
        im = Image.new("L", (w, h), 0)
        ImageDraw.Draw(im).text((-bb[0], -bb[1]), ch, font=f, fill=255)
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


# ---- levels ----

class Pool:
    """a level's numbers, each list stored once (repeats share it)"""

    def __init__(self, fmt):
        self.fmt = fmt
        self.items = []
        self.seen = {}

    def add(self, vals):
        key = tuple(vals)
        if key in self.seen:
            return self.seen[key]
        off = len(self.items)
        self.items += list(vals)
        self.seen[key] = off
        return off

    def data(self):
        return struct.pack("<%d%s" % (len(self.items), self.fmt), *self.items)


def i16(v):
    v = int(round(v))
    assert -32768 <= v <= 32767, v
    return v


def pack_level(L, pics, theme_pic):
    mats = []

    def mat_index(m):
        m = tuple(float(x) for x in m)
        if m not in mats:
            mats.append(m)
        return mats.index(m)

    statics = [b for b in L.bodies if b.kind == "static"]
    dyns = [b for b in L.bodies if b.kind != "static"]
    sidx = {id(b): i for i, b in enumerate(statics)}
    didx = {id(b): i for i, b in enumerate(dyns)}

    # static shapes in world coordinates: their box, and their vertices in quarter pixels from its corner
    sshapes = []
    sverts = Pool("h")
    for bi, b in enumerate(statics):
        for s in b.shapes:
            if s["type"] != "poly":
                c = b.to_world(s["c"])
                verts = [(c[0] + s["r"] * math.cos(k * math.pi / 6), c[1] + s["r"] * math.sin(k * math.pi / 6))
                         for k in range(12)]
            else:
                verts = [b.to_world(v) for v in s["v"]]
            p = nape_poly(verts)
            xs = [v[0] for v in p["v"]]
            ys = [v[1] for v in p["v"]]
            x0, y0 = math.floor(min(xs)), math.floor(min(ys))
            span = max(max(xs) - x0, max(ys) - y0)
            unit = 0
            while span * (4 >> unit if unit < 3 else 0.5) > 32000:
                unit += 1
            k = [4, 2, 1, 0.5][unit]
            q = []
            for v in p["v"]:
                q += [i16((v[0] - x0) * k), i16((v[1] - y0) * k)]
            flags = (1 if s["sensor"] else 0) | (2 if b.fluid else 0) | (unit << 4)
            off = sverts.add(q)
            assert off % 2 == 0 and off // 2 < 65536 and len(p["v"]) <= 48, (off, len(p["v"]))
            sshapes.append(struct.pack("<ffhhHBBBBH", x0, y0, i16(math.ceil(max(xs)) - x0 + 1),
                                       i16(math.ceil(max(ys)) - y0 + 1), off // 2, len(p["v"]), s["filter"],
                                       mat_index(s["mat"]), flags, bi))
    sbodies = []
    for b in statics:
        vis, den = b.fluid if b.fluid else (0.0, 0.0)
        sbodies.append(struct.pack("<HBBff", b.cb, 1, 1 if b.ground else 0, vis, den))

    # buckets: 256 px columns of the static shapes
    boxes = []
    for rec in sshapes:
        x0, y0, w, h = struct.unpack_from("<ffhh", rec)
        boxes.append((x0, x0 + w))
    if boxes:
        bx0 = math.floor(min(b0 for b0, _ in boxes) / 256) * 256 - 256
        bx1 = max(b1 for _, b1 in boxes) + 256
    else:
        bx0, bx1 = -256, 256
    nb = int((bx1 - bx0) // 256) + 1
    cols = [[] for _ in range(nb)]
    wide = []
    for i, (b0, b1) in enumerate(boxes):
        if b1 - b0 > 1536:
            wide.append(i)     # checked every time
            continue
        for c in range(int((b0 - bx0) // 256), int((b1 - bx0) // 256) + 1):
            cols[c].append(i)
    buckets, blist = [], []
    for c in cols:
        buckets += [len(blist), len(c)]
        blist += c
    buckets += [len(blist), len(wide)]
    blist += wide
    assert len(blist) < 65536

    # dynamic and kinematic bodies, shapes in body coordinates (repeated shapes share their numbers)
    dbodies, dshapes = [], []
    ddata = Pool("f")
    for b in dyns:
        first = len(dshapes)
        for s in b.shapes:
            if s["type"] == "poly":
                p = nape_poly(s["v"])
                vals = []
                for v in p["v"]:
                    vals += [v[0], v[1]]
                vals = [struct.unpack("<f", struct.pack("<f", v))[0] for v in vals]
                dshapes.append(struct.pack("<BBBBB3xfffffI", 1, len(p["v"]), s["filter"],
                                           1 if s["sensor"] else 0, mat_index(s["mat"]), 0.0, p["com"][0], p["com"][1],
                                           p["area"], p["inertia"], ddata.add(vals)))
            else:
                r, c = s["r"], s["c"]
                dshapes.append(struct.pack("<BBBBB3xfffffI", 0, 0, s["filter"], 1 if s["sensor"] else 0,
                                           mat_index(s["mat"]), r, c[0], c[1], math.pi * r * r,
                                           r * r * 0.5 + c[0] ** 2 + c[1] ** 2, 0))
        flags = (4 if b.norot else 0) | (0x10 if b.ground else 0)
        dbodies.append(struct.pack("<fffHBBHBB", b.x, b.y, b.rot, first, len(b.shapes),
                                   1 if b.kind == "dynamic" else 2, b.cb, flags, 0))

    params = Pool("f")
    # objects
    objs, aux = [], []
    for o in L.objs:
        b = o["body"]
        bi = di = -1
        if b is not None:
            if b.kind == "static":
                bi = sidx[id(b)]
            else:
                di = didx[id(b)]
        b2 = o["body2"]
        d2 = -1
        if b2 is not None and b2.kind != "static":
            d2 = didx[id(b2)]
        elif b2 is not None and bi < 0:
            bi = sidx[id(b2)]    # a joint to a static body: its index (the joint ignores their collisions)
        img = theme_pic(o["img"]) if o["img"] else 0xFFFF
        a = len(aux) // 2
        for q in o["aux"]:
            aux += [q[0], q[1]]
        p = (list(o["p"]) + [0.0] * 8)[:8]
        po = params.add([float(v) for v in p]) if any(p) else 0
        objs.append(struct.pack("<BBhhhhhHHHH3f", o["kind"], o["flags"], int(o["id"] if o["id"] is not None else -1),
                                int(o["id2"] if o["id2"] is not None else -1), di, bi, d2, img, len(o["aux"]), a, po,
                                o["x"], o["y"], o["rot"]))

    # drawings, back to front: the containers in order, landscape's back items first (in reverse)
    def key(d):
        if d["cont"] == C_LAND and d["back"]:
            return (d["cont"], 0, -d["order"])
        return (d["cont"], 1, d["order"])

    draws = []
    dpts = Pool("h")
    for d in sorted(L.draws, key=key):
        img = theme_pic(d["img"]) if d["img"] else 0xFFFF
        if d["img"] and img == 0xFFFF:
            continue
        pts = d["pts"]
        flat = []
        for q in pts:
            flat += [i16(q[0]), i16(q[1])]
        off = dpts.add(flat) // 2 if flat else 0
        if d["kind"] == 0 and len(flat) > 8:
            dpts.seen.setdefault(tuple(flat[2:-2]), off * 2 + 2)
        if pts:
            xs = [q[0] for q in pts]
            ys = [q[1] for q in pts]
            bnd = (math.floor(min(xs)), math.floor(min(ys)), math.ceil(max(xs)), math.ceil(max(ys)))
        else:
            bnd = (0, 0, 0, 0)
        owner = d["owner"]["index"] if d["owner"] is not None else -1
        if d["kind"] == 2:
            pv = d["p"][:6]            # sprite: x, y, angle, scale x, y, frames
        elif d["kind"] == 0:
            pv = d["p"][:2]            # polygon: its texture's origin
        else:
            pv = d["p"][:1]            # strip: rope; water: background
        po = params.add([float(v) for v in pv])
        draws.append(struct.pack("<BBBBhHHHH4h2x", d["kind"], d["cont"], d["flags"], 1 if d["back"] else 0, owner,
                                 len(pts), img, off, po, *[i16(v) for v in bnd]))

    cams = b""
    for c, dy in L.cams:
        flat = []
        for q in c:
            flat += [i16(q[0]), i16(q[1])]
        off = dpts.add(flat)
        cams += struct.pack("<HHf", off // 2, len(c), float(dy))

    parts = [("mats", b"".join(struct.pack("<5f", *m) for m in mats)),
             ("sshapes", b"".join(sshapes)),
             ("sverts", sverts.data()),
             ("sbodies", b"".join(sbodies)),
             ("buckets", struct.pack("<%dH" % len(buckets), *buckets)),
             ("blist", struct.pack("<%dH" % len(blist), *blist)),
             ("dbodies", b"".join(dbodies)),
             ("dshapes", b"".join(dshapes)),
             ("ddata", ddata.data()),
             ("objs", b"".join(objs)),
             ("aux", struct.pack("<%df" % len(aux), *aux)),
             ("draws", b"".join(draws)),
             ("dpts", dpts.data()),
             ("params", params.data()),
             ("cams", cams)]
    hdr_size = 4 + 2 * 10 + 4 * 8 + 4 * 15
    blob = b""
    offs = []
    sizes = {}
    for name, data in parts:
        blob += b"\0" * (-(hdr_size + len(blob)) % 4)
        offs.append(hdr_size + len(blob))
        blob += data
        sizes[name] = len(data)
    hdr = struct.pack("<BBBB", L.theme, len(mats), 0, 0)
    hdr += struct.pack("<10H", len(L.objs), len(sshapes), len(statics), len(dyns), len(dshapes), nb, len(draws),
                       len(L.cams), len(aux) // 2, 0)
    hdr += struct.pack("<8f", 500.0 if not L.settings.get("gravityY") else float(L.settings["gravityY"]),
                       *L.prizes[:3], L.start[0], L.start[1], L.finish_x, float(bx0))
    hdr += struct.pack("<15I", *offs)
    assert len(hdr) == hdr_size
    sizes["dverts"] = len(ddata.items) // 2
    return hdr + blob, sizes


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    ref = sys.argv[1]
    lv = os.path.join(ref, "assets", "levels")
    objects = load(os.path.join(lv, "objects.json"))
    theme = Atlas(ref, "theme")
    cars = Atlas(ref, "cars_game")
    garage = Atlas(ref, "garage")
    pics = Pics()

    # the levels first, to know which pictures they use
    levels = [Level(n, load(os.path.join(lv, "map%d.json" % n)), objects, warn) for n in range(1, NLEVELS + 1)]
    used = set()
    for L in levels:
        used |= L.images

    WORLD = 0.5
    ANCHOR = {"Dec12": (0.5, 0.7)}
    TILES = {"tile0", "tile1", "tile2", "tile_bg0", "tile_bg1", "tile_stone", "tile_saw", "tile_rivet",
             "tile_rivet_bg", "RoadTile0_1", "finish"}
    STRIPS = {"line0", "line1", "line2", "line_top_1", "line_top_2", "line_wood_g", "line_metal_g", "line_stone_shape",
              "RoadLine0_4", "RoadLine0_Bg0", "RoadLine0_Bg1", "line_top1", "UnLine0", "UnLine1", "line_rivet",
              "Plank0_line", "RoadLine0_7", "RoadLine0_8", "line_ivy", "LineChain", "RoadPillar0_0", "RoadPillar0_1",
              "RoadPillar0_2"}
    frames_of = {"Dec12": 22, "SafePointAnim": 2, "boost_anim": 10, "geyser_anim": 6}

    def theme_pic(name):
        if name in pics.ids:
            return pics.ids[name]
        if name.startswith("strip:"):
            src = name[6:]
            if not theme.has(src + "0000"):
                warn("no picture " + src)
                return 0xFFFF
            im = theme.get(src + "0000")
            return pics.add(name, im, (0, im.size[1] / 2), WORLD, tile=True)
        nframes = frames_of.get(name, 1)
        first = None
        for k in range(nframes):
            fname = "%s%04d" % (name, k)
            if not theme.has(fname):
                if k == 0:
                    warn("no picture " + name)
                    return 0xFFFF
                break
            im = theme.get(fname)
            if name in TILES:
                i = pics.add(name if k == 0 else "%s_%d" % (name, k), im, (0, 0), WORLD, tile=True, colors=16)
            elif name in STRIPS:
                i = pics.add(name if k == 0 else "%s_%d" % (name, k), im, (0, im.size[1] / 2), WORLD, tile=True)
            else:
                ax, ay = ANCHOR.get(name, (0.5, 0.5))
                i = pics.add(name if k == 0 else "%s_%d" % (name, k), im, anchor_pivot(im, ax, ay), WORLD)
            if first is None:
                first = i
        return first

    # pictures every level may use
    for n in ["CarExplode", "smoke_part_anim", "ground_part_anim"] + ["GlassCrashed0_%d" % k for k in range(6)]:
        frames_of.setdefault("CarExplode", 27)
        frames_of.setdefault("smoke_part_anim", 15)
        frames_of.setdefault("ground_part_anim", 26)
        theme_pic(n)
    for n in sorted(used):
        theme_pic(n)
    # strips: their pictures repeat along the line; Plank0 is also a sprite, so its strip gets its own name
    # backgrounds, at the screen's scale (the game draws them twice their size on a 720 x 480 screen)
    bg_ids = []
    for t in range(3):
        row = []
        for k in range(3):
            nm = BG[t][k]
            if nm:
                im = theme.get(nm + "0000")
                row.append(pics.add(nm, im, (0, 0), 1.0, colors=16))
            else:
                row.append(0xFFFF)
        bg_ids.append(row)

    # the bikes (cars atlas, world scale)
    from levels import TO_RAD  # noqa: F401
    ASSETS = load(os.path.join(HERE, "bike_anchors.json"))
    bike_ids = []
    for skin in range(1, 7):
        k = skin - 1
        ids = []
        im = cars.get("bike_%d_base0000" % skin)
        ids.append(pics.add("bike_%d_base" % skin, im, anchor_pivot(im, ASSETS["FRAMES_ANCHORX"][k],
                                                                      ASSETS["FRAMES_ANCHORY"][k]), WORLD))
        im = cars.get("Car%dFrameExploded0000" % skin)
        ids.append(pics.add("Car%dFrameExploded" % skin, im, anchor_pivot(im, ASSETS["FRAMES_ANCHORX"][k],
                                                                            ASSETS["FRAMES_ANCHORY"][k]), WORLD))
        wname = ASSETS["WHEELS"][k]
        im = cars.get(wname + "0000")
        ids.append(pics.add(wname, im, anchor_pivot(im, 0.5, 0.5), WORLD))
        for part in ["driver_head", "driver_corpse", "driver_arm", "driver_forearm", "driver_hip", "driver_shin"]:
            im = cars.get("%s%d0000" % (part, skin))
            ids.append(pics.add("%s%d" % (part, skin), im, anchor_pivot(im, ASSETS["ARCHORS_X"][part][k],
                                                                        ASSETS["ARCHORS_Y"][part][k]), WORLD))
        im = cars.get("suspend_back%d0000" % skin)
        ids.append(pics.add("suspend_back%d" % skin, im, anchor_pivot(im, ASSETS["SUSPEND_BACK_ANCHORX"][k],
                                                                     ASSETS["SUSPEND_BACK_ANCHORY"][k]), WORLD))
        im = cars.get("suspend_front%d0000" % skin)
        ids.append(pics.add("suspend_front%d" % skin, im, anchor_pivot(im, ASSETS["SUSPEND_FRONT_ANCHORX"][k],
                                                                      ASSETS["SUSPEND_FRONT_ANCHORY"][k]), WORLD))
        bike_ids.append(ids)
    rocket = None
    for k in range(6):
        im = cars.get("RocketAnim%04d" % k)
        i = pics.add("RocketAnim_%d" % k, im, anchor_pivot(im, 1.0, 0.5), WORLD)
        rocket = i if rocket is None else rocket

    # the bikes as the menus show them
    def part_pic(g):
        for skin in range(1, 7):
            k = skin - 1
            for slot, nm in enumerate(["bike_%d_base" % skin, "Car%dFrameExploded" % skin, ASSETS["WHEELS"][k]]):
                if g == nm:
                    return bike_ids[k][slot]
            if g.startswith("Wheel_"):
                return bike_ids[0][2] if g == "Wheel_1_0" else pics.ids.get(g, bike_ids[0][2])
            for slot, part in enumerate(["driver_head", "driver_corpse", "driver_arm", "driver_forearm", "driver_hip",
                                         "driver_shin", "suspend_back", "suspend_front"]):
                if g == "%s%d" % (part, skin):
                    return bike_ids[k][3 + slot]
        raise KeyError(g)
    layouts = []
    b11 = objects["bike11"]
    lay = []
    for key in ["Wheel0", "Wheel1", "suspend_back", "base", "driver_corpse", "driver_hip", "driver_shin", "driver_arm",
                "driver_forearm", "driver_head", "suspend_front"]:
        q = b11[key]
        lay.append((part_pic(q["g"]), round(q["x"] * 2), round(q["y"] * 2), round(q["r"])))
    layouts.append(lay)
    for skin in range(1, 7):
        lay = []
        for q in objects["bike%d" % skin]:
            lay.append((part_pic(q["g"]), round(q["x"] * 2), round(q["y"] * 2), round(q["r"])))
        while len(lay) < 11:
            lay.append(lay[-1])
        layouts.append(lay[:11])
    fire = None
    for k in range(11):
        im = theme.get("hell_rider_head%04d" % k)
        i = pics.add("hell_rider_head_%d" % k, im, anchor_pivot(im, .5, .5), WORLD)
        fire = i if fire is None else fire

    # the menus (garage atlas, half size)
    UI = 0.5
    ui = {}
    for nm, ax, ay in [("timer_bg", .5, .5), ("star0", .5, .5), ("star1", .5, .5), ("w_star", .5, .5),
                       ("orange_btn", .5, .5), ("orange_btn_big", .5, .5), ("orange_btn_big_green", .5, .5),
                       ("next_btn", .5, .5), ("exit_btn", .5, .5), ("restart_btn", 0, 0), ("improved_bg", .5, .5),
                       ("0logo", .5, .5), ("top_bg", 0, 0), ("pause_bg", 0, 0), ("0popup_bg", .5, .5),
                       ("select_level", .5, .5), ("arrow_btn", .5, .5), ("play_btn", .5, .5), ("shop_btn", .5, .5)]:
        im = garage.get(nm + "0000")
        ui[nm] = pics.add("ui_" + nm, im, anchor_pivot(im, ax, ay), UI, colors=16 if nm == "0popup_bg" else 255)
    for k in range(6):
        im = garage.get("LeveMapItemLocked%04d" % k)
        i = pics.add("ui_map%d" % k, im, anchor_pivot(im, .5, .5), UI)
        if k == 0:
            ui["map"] = i
    for k in range(2):
        im = garage.get("arrow_keys%04d" % k)
        i = pics.add("ui_arrows%d" % k, im, anchor_pivot(im, 0, 0), UI)
        if k == 0:
            ui["arrows"] = i
    im = garage.get("0logo0000").rotate(-90, expand=True)
    ui["logo"] = pics.add("ui_logo", im, anchor_pivot(im, .5, .5), UI)
    im = Image.open(os.path.join(ref, "assets", "images", "x1", "0bg_garage.png")).convert("RGBA")
    ui["mapbg"] = pics.add("ui_mapbg", im, (0, 0), UI, colors=16)

    # fonts: the game's Impact and CF Crack and Bold, at half their sizes
    impact = os.path.join(ref, "assets", "fonts", "Impact", "impact.woff")
    crack = os.path.join(ref, "assets", "fonts", "CfCrackBold", "cfcrackandbold.woff")
    fonts = [make_font(impact, 11), make_font(crack, 15), make_font(crack, 20), make_font(crack, 36)]

    # levels
    lblobs = []
    stats = []
    for L in levels:
        blob, st = pack_level(L, pics, theme_pic)
        lblobs.append(blob)
        stats.append(st)

    # data.bin: "MX3D", counts, offsets, then everything
    nimg, nfont, nlev = len(pics.list), len(fonts), len(lblobs)
    head = struct.pack("<4sIII", b"MX3D", nimg, nfont, nlev)
    table_size = 4 * (nimg + nfont + nlev)
    pos = len(head) + table_size
    offs = []
    body = b""
    which, uniq = pics.blobs()
    uoff = []
    for data in uniq:
        uoff.append(pos + len(body))
        body += data
    for k in which:
        offs.append(uoff[k])
    for data in fonts:
        offs.append(pos + len(body))
        body += data
    for data in lblobs:
        body += b"\0" * (-(pos + len(body)) % 4)
        offs.append(pos + len(body))
        body += data
    out = head + struct.pack("<%dI" % len(offs), *offs) + body
    with open(os.path.join(SRC, "data.bin"), "wb") as f:
        f.write(out)

    # data.h
    with open(os.path.join(SRC, "data.h"), "w") as f:
        f.write("/* Moto X3M: what's in data.bin (made by tools/pack.py) */\n#ifndef MX_DATA_H\n#define MX_DATA_H\n")
        f.write("#define N_PICS %d\n#define N_FONTS %d\n#define N_LEVELS %d\n" % (nimg, nfont, nlev))
        for nm in sorted(ui):
            f.write("#define PIC_UI_%s %d\n" % (nm.upper(), ui[nm]))
        f.write("#define PIC_ROCKET %d\n" % rocket)
        for nm in ["CarExplode", "smoke_part_anim", "ground_part_anim", "GlassCrashed0_0", "GeyserBase",
                   "geyser_anim", "particle_boost"]:
            if nm in pics.ids:
                f.write("#define PIC_%s %d\n" % (nm.upper(), pics.ids[nm]))
        f.write("static const uint16_t PIC_BG[3][3] = {%s};\n" %
                ", ".join("{%s}" % ", ".join(str(v) for v in r) for r in bg_ids))
        f.write("/* per bike: frame, frame exploded, wheel, head, corpse, arm, forearm, hip, shin, suspend back, front */\n")
        f.write("static const uint16_t PIC_BIKE[6][11] = {%s};\n" %
                ", ".join("{%s}" % ", ".join(str(v) for v in r) for r in bike_ids))
        f.write("enum { FONT_SMALL, FONT_MID, FONT_BIG, FONT_HUGE };\n")
        f.write("static const int16_t MAP_PTS[50][2] = {%s};\n" % ", ".join(
            "{%d, %d}" % (objects["point%d" % k]["x"], objects["point%d" % k]["y"]) for k in range(50)))
        f.write("/* bikes as the menus show them (bike11: the pause screen's, then bike1-6): picture, x, y, angle */\n")
        f.write("typedef struct { uint16_t pic; int16_t x, y, deg; } BikePart;\n")
        f.write("static const BikePart BIKE_LAYOUT[7][11] = {%s};\n" % ", ".join(
            "{%s}" % ", ".join("{%d, %d, %d, %d}" % q for q in lay) for lay in layouts))
        f.write("#define PIC_FIRE %d\n" % fire)
        f.write("#endif\n")
    # bike_data.h: the bike's numbers (levels/1.json) and its rider's parts and joints (levels/bike.json)
    car = load(os.path.join(lv, "1.json"))
    geo = load(os.path.join(lv, "bike.json"))
    def fl(v):
        t = "%.9g" % float(v)
        return (t if ("." in t or "e" in t) else t + ".0") + "f"

    with open(os.path.join(SRC, "bike_data.h"), "w") as f:
        f.write("/* Moto X3M: the bike, from the game's levels/1.json and levels/bike.json (made by tools/pack.py) */\n")
        f.write("#ifndef MX_BIKE_DATA_H\n#define MX_BIKE_DATA_H\n")
        for k in sorted(car):
            f.write("#define CAR_%s %s\n" % (k, fl(car[k])))
        f.write("typedef struct { float x, y, w, h, r; } PartGeo;\n")
        for k in ["driver_head", "driver_corpse", "driver_arm", "driver_forearm", "driver_hip", "driver_shin"]:
            g = geo[k]
            f.write("static const PartGeo GEO_%s = {%s, %s, %s, %s, %s};\n" % (
                k.upper(), fl(g["x"]), fl(g["y"]), fl(g["w"]), fl(g["h"]), fl(g["r"])))
        for k in ["hip_joint", "corpse_joint", "forearm_joint", "arm_joint", "head_joint", "steer_joint", "shin_joint",
                  "suspend_back"]:
            g = geo[k]
            f.write("static const V2 GEO_%s = {%s, %s};\n" % (k.upper(), fl(g["x"]), fl(g["y"])))
        f.write("#define GEO_SUSPEND_BACK_W %s\n" % fl(geo["suspend_back_w"]))
        pts = geo["points"]
        f.write("static const V2 GEO_POINTS[%d] = {%s};\n" % (len(pts), ", ".join(
            "{%d, %d}" % (int(q["x"]), int(q["y"])) for q in pts)))
        f.write("#endif\n")
    tot = sum(len(d) for d in uniq)
    print("data.bin: %d bytes (pictures %d in %d, fonts %d, levels %d)" % (
        len(out), tot, nimg, sum(len(d) for d in fonts), sum(len(b) for b in lblobs)))
    tot = {}
    for st in stats:
        for k, v in st.items():
            tot[k] = tot.get(k, 0) + v
    print("levels:", tot)
    for w in sorted(set(warnings)):
        print("note:", w)


if __name__ == "__main__":
    main()
