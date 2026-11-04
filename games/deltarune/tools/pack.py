#!/usr/bin/env python3
"""Deltarune Chapter 1's pictures, rooms, fonts and texts -> src/data.bin + src/data.h.

    python3 -I tools/pack.py REF        (REF: the folder with data.win and lang_en.json, from tools/fetch.py)

What goes in is what the game's C code names: every identifier in src/*.c that is the name of one of the
original's sprites (spr_krisd), rooms (room_krisroom), objects (obj_doorA) or texts (obj_krisroom_slash_Step_0_gml_92_0)
is packed, and data.h gives it its number under that same name.

data.bin: "DRD1", the number of sections, their offsets, then the sections (SEC_* in data.h):
- SPRITES: for each sprite its first frame and its number of frames (u16, u16)
- FRAMES: each frame: w, h, origin x, y (u16, u16, s16, s16), bpp, colours (u8, u8), the colours (RGB565), each
  row's offset (u16) from the rows' data, then the rows: runs of (skip, length, length pixels of bpp bits, lowest
  first, the run padded to a byte), until the row's width
- TILES: 20 x 20 pixel tiles of the rooms, as the screen shows them: bpp (0: one colour; +0x80: colour 0 is
  see-through), colours, the colours (RGB565), 400 pixels of bpp bits
- ROOMS: 32 bytes each (res.c room_info): its size in the original's units, its tile grid (and a foreground
  grid drawn over the characters), its solids and its instances, its flags (1: the Dark World, drawn at half size)
- GRID: the rooms' tile ids (u16, 0xFFFF none)
- SOLIDS: 10 bytes each: the shape (0 a box; 1-4 a right triangle, its square corner bottom-left, bottom-right,
  top-left, top-right; 5 an ellipse), 1 if only the enemies' (obj_solidenemy), and its box (s16 x0, y0, x1, y1, the original's units, x1 and y1 past it)
- INSTS: 12 bytes each: the object (OBJ number in data.h), x, y (s16), its scale x and y (s16, 1/100), the
  instance's place among the room's instances (u16)
- FONTS: height, first character, count, flags, then 8 bytes a glyph (advance, w, h, ox, oy, 0, bitmap
  offset u16) and the bitmaps (rows of whole bytes, highest bit first)
- STRINGS: the texts (u32 count, u32 offsets, then each text, ending in 0)
- BLOCKS: the frames and tiles themselves, in LZMA streams the game unpacks when it needs them (blk.c)
- ORDER: each room's place in the original's order (u16: its doors go to the next or previous rooms)"""
import json
import lzma
import os
import re
import struct
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gm as GM  # noqa: E402

SRC = os.path.join(HERE, "..", "src")
OUT_BIN = os.path.join(SRC, "data.bin")
OUT_H = os.path.join(SRC, "data.h")
SECTIONS = ["SPRITES", "FRAMES", "TILES", "ROOMS", "GRID", "SOLIDS", "INSTS", "FONTS", "STRINGS", "BLOCKS", "ORDER"]
BLOCK_TILES, BLOCK_FRAMES = 2048, 16384
MAX_BLOCKS = 4096
LZMA_FILTERS = [{"id": lzma.FILTER_LZMA1, "dict_size": 1 << 16, "lc": 0, "lp": 0, "pb": 0}]
# the fonts the game draws with: the original's own, from its texture pages
FONTS = ["fnt_main", "fnt_small", "fnt_tinynoelle", "fnt_dotumche"]
# solids: the objects (and their children) Kris can't walk through, and their shapes
SOLID_SHAPES = {"obj_sdl": 1, "obj_sdl_dark": 1, "obj_sdr": 2, "obj_sul": 3, "obj_sur": 4, "obj_sur_dark": 4,
                "obj_solidcircle": 5}
# characters' depth (scr_depth: 100000 - 10 (y + height)): tiles nearer than this are drawn over them
FRONT_DEPTH = 50000


class Blocks:
    def __init__(self):
        self.raw = []

    def place(self, entries, groups):
        where = [None] * len(entries)
        for g in groups:
            data = bytearray()
            for i in g:
                where[i] = (len(self.raw), len(data))
                data += entries[i]
            assert len(data) < 65536
            self.raw.append(bytes(data))
        return struct.pack("<I", len(entries)) + b"".join(struct.pack("<HH", b, o) for b, o in where)

    def section(self):
        assert len(self.raw) <= MAX_BLOCKS
        streams = [lzma.compress(r, format=lzma.FORMAT_RAW, filters=LZMA_FILTERS) for r in self.raw]
        props = bytes([0]) + struct.pack("<I", 1 << 16) + bytes(3)
        head = struct.pack("<I", len(streams)) + props
        base = len(head) + 8 * len(streams)
        table, body = bytearray(), bytearray()
        for r, c in zip(self.raw, streams):
            table += struct.pack("<II", base + len(body), len(r))
            body += c
        return head + bytes(table) + bytes(body)


def groups_by_size(sizes, limit, starts=()):
    out, cur, n = [], [], 0
    starts = set(starts)
    for i, sz in enumerate(sizes):
        if cur and (n + sz > limit or i in starts):
            out.append(cur)
            cur, n = [], 0
        cur.append(i)
        n += sz
    if cur:
        out.append(cur)
    return out


def rgb565(r, g, b):
    r, g, b = int(r), int(g), int(b)
    return ((r * 31 + 127) // 255) << 11 | ((g * 63 + 127) // 255) << 5 | ((b * 31 + 127) // 255)


def rgb565_array(a):
    a = a.astype(np.uint32)
    return (((a[..., 0] * 31 + 127) // 255) << 11 | ((a[..., 1] * 63 + 127) // 255) << 5 |
            ((a[..., 2] * 31 + 127) // 255)).astype(np.uint16)


def table(entries):
    out, offs = bytearray(), []
    base = 4 + 4 * len(entries)
    for e in entries:
        while (base + len(out)) % 2:
            out.append(0)
        offs.append(base + len(out))
        out += e
    return struct.pack("<I", len(entries)) + b"".join(struct.pack("<I", o) for o in offs) + bytes(out)


def bits_pack(values, bpp):
    out = bytearray((len(values) * bpp + 7) // 8)
    for k, v in enumerate(values):
        b = k * bpp
        out[b >> 3] |= v << (b & 7)
    return out


def bpp_for(n):
    return 1 if n <= 2 else 2 if n <= 4 else 4 if n <= 16 else 8


# ------------------------------------------------------------------ frames
def encode_frame(img, ox, oy):
    a = np.array(img.convert("RGBA"))
    h, w = a.shape[:2]
    opaque = a[:, :, 3] >= 128
    cols = {}
    for c in map(tuple, a[opaque][:, :3]):
        k = rgb565(*c)
        cols[k] = cols.get(k, 0) + 1
    pal = sorted(cols, key=lambda k: -cols[k])
    if len(pal) > 255:
        # (a rare frame with too many colours: the least used ones folded into their nearest)
        keep = pal[:255]
        def near(k):
            r, g, b = k >> 11, (k >> 5) & 63, k & 31
            return min(keep, key=lambda q: (r - (q >> 11)) ** 2 + ((g - ((q >> 5) & 63)) // 2) ** 2 + (b - (q & 31)) ** 2)
        remap = {k: (k if k in keep else near(k)) for k in pal}
        pal = keep
    else:
        remap = {k: k for k in pal}
    index = {k: i for i, k in enumerate(pal)}
    bpp = bpp_for(max(len(pal), 1))
    rows, data = [], bytearray()
    for y in range(h):
        rows.append(len(data))
        x = 0
        while x < w:
            skip = 0
            while x < w and not opaque[y, x] and skip < 255:
                x += 1
                skip += 1
            run = []
            while x < w and opaque[y, x] and len(run) < 255:
                run.append(index[remap[rgb565(*a[y, x, :3])]])
                x += 1
            data += bytes([skip, len(run)]) + bits_pack(run, bpp)
        if len(data) > 0xFFFF:
            raise ValueError("a frame too big")
    head = struct.pack("<HHhhBB", w, h, ox, oy, bpp, len(pal)) + b"".join(struct.pack("<H", k) for k in pal)
    return head + b"".join(struct.pack("<H", r) for r in rows) + bytes(data)


def halve(img):
    """half the size: of each 2 x 2, the most opaque pixel, then the brightest (thin lines stay)"""
    a = np.array(img.convert("RGBA")).astype(np.int32)
    h, w = a.shape[0] // 2 * 2, a.shape[1] // 2 * 2
    a = np.pad(a, ((0, a.shape[0] % 2), (0, a.shape[1] % 2), (0, 0)))
    h, w = a.shape[0], a.shape[1]
    q = [a[0:h:2, 0:w:2], a[1:h:2, 0:w:2], a[0:h:2, 1:w:2], a[1:h:2, 1:w:2]]
    key = np.stack([(p[..., 3] >= 128) * 1000 + p[..., 0] * 3 + p[..., 1] * 6 + p[..., 2] for p in q])
    best = key.argmax(axis=0)
    out = np.zeros_like(q[0])
    for k in range(4):
        out[best == k] = q[k][best == k]
    return Image.fromarray(out.astype(np.uint8), "RGBA")


def crop_frames(frames):
    box = None
    for f in frames:
        b = f.getchannel("A").point(lambda v: 255 if v >= 128 else 0).getbbox()
        if b:
            box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    if box is None:
        return [Image.new("RGBA", (1, 1)) for _ in frames], 0, 0
    return [f.crop(box) for f in frames], box[0], box[1]


# ------------------------------------------------------------------ fonts
def make_font(g, name):
    f = g.font(name)
    page = g.tpag_image(f["tpag"])
    a = np.array(page)
    glyphs = {x["c"]: x for x in f["glyphs"]}
    first, last = 32, 127
    gl, bits = bytearray(), bytearray()
    height = max(x["h"] for x in f["glyphs"])
    for c in range(first, last):
        x = glyphs.get(c) or glyphs.get(ord("?"))
        sub = a[x["y"]:x["y"] + x["h"], x["x"]:x["x"] + x["w"], 3] >= 128
        ys, xs = np.nonzero(sub)
        if len(xs) == 0:
            gl += struct.pack("<BBBbbBH", x["shift"], 0, 0, 0, 0, 0, len(bits))
            continue
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        box = sub[y0:y1, x0:x1]
        off = len(bits)
        for row in box:
            rb = bytearray((box.shape[1] + 7) // 8)
            for k, v in enumerate(row):
                if v:
                    rb[k >> 3] |= 0x80 >> (k & 7)
            bits += rb
        gl += struct.pack("<BBBbbBH", x["shift"], box.shape[1], box.shape[0], x0 + x["offset"], y0, 0, off)
    return struct.pack("<BBBB", height, first, last - first, 0) + bytes(gl) + bytes(bits)


# ------------------------------------------------------------------ rooms
class TileSet:
    def __init__(self):
        self.ids, self.blobs = {}, []

    def add(self, block, clear=None):
        """a 20 x 20 block of RGB565 values (and its see-through mask) -> its tile id"""
        key = block.tobytes() + (clear.tobytes() if clear is not None else b"")
        if key in self.ids:
            return self.ids[key]
        flat = block.reshape(-1)
        if clear is not None:
            cflat = clear.reshape(-1)
            cols, counts = np.unique(flat[~cflat], return_counts=True)
            order = [None] + [int(c) for c, _ in sorted(zip(cols, counts), key=lambda t: -t[1])]
            index = {c: i for i, c in enumerate(order)}
            bpp = bpp_for(len(order))
            blob = struct.pack("<BB", 0x80 | bpp, len(order)) + struct.pack("<H", 0)
            blob += b"".join(struct.pack("<H", c) for c in order[1:])
            blob += bits_pack([0 if cl else index[int(v)] for v, cl in zip(flat, cflat)], bpp)
        else:
            cols, counts = np.unique(flat, return_counts=True)
            order = [int(c) for c, _ in sorted(zip(cols, counts), key=lambda t: -t[1])]
            if len(order) == 1:
                blob = struct.pack("<BBH", 0, 1, order[0])
            else:
                index = {c: i for i, c in enumerate(order)}
                bpp = bpp_for(len(order))
                blob = struct.pack("<BB", bpp, len(order)) + b"".join(struct.pack("<H", c) for c in order)
                blob += bits_pack([index[int(v)] for v in flat], bpp)
        self.ids[key] = len(self.blobs)
        self.blobs.append(blob)
        return self.ids[key]


def is_dark(r):
    return any(v["enabled"] and v["w"] == 640 for v in r["views"]) or (r["w"] >= 640 and not any(v["enabled"] for v in r["views"]))


def render_room(g, r, front):
    """the room's tiles and backgrounds (front: only those drawn over the characters), at the original's size"""
    im = Image.new("RGBA", (r["w"], r["h"]), (0, 0, 0, 0))
    for L in sorted(r["layers"], key=lambda L: -L["depth"]):
        if (L["depth"] < FRONT_DEPTH) != front or not L["visible"]:
            continue
        if L["type"] == 1 and L["sprite"] >= 0 and L["bg_visible"]:
            fr = g.sprite_frames(L["sprite"])[0]
            x0, y0 = int(L["xo"]), int(L["yo"])
            xs = range(x0 % fr.width - fr.width, r["w"], fr.width) if L["tiledh"] else [x0]
            ys = range(y0 % fr.height - fr.height, r["h"], fr.height) if L["tiledv"] else [y0]
            for yy in ys:
                for xx in xs:
                    im.alpha_composite(fr, (xx, yy)) if xx >= 0 and yy >= 0 else im.paste(fr, (xx, yy), fr)
        elif L["type"] == 3:
            for t in L["tiles"]:
                fr = sprite_frame0(g, t["spr"])
                piece = fr.crop((t["sx"], t["sy"], t["sx"] + t["w"], t["sy"] + t["h"]))
                if t["scx"] != 1 or t["scy"] != 1:
                    piece = piece.resize((max(1, round(t["w"] * abs(t["scx"]))), max(1, round(t["h"] * abs(t["scy"])))),
                                         Image.NEAREST)
                    if t["scx"] < 0:
                        piece = piece.transpose(Image.FLIP_LEFT_RIGHT)
                    if t["scy"] < 0:
                        piece = piece.transpose(Image.FLIP_TOP_BOTTOM)
                im.paste(piece, (t["x"], t["y"]), piece)
    return im


_frame0 = {}


def sprite_frame0(g, i):
    if i not in _frame0:
        _frame0[i] = g.sprite_frames(i)[0]
    return _frame0[i]


def solid_kind(g, oi):
    """the shape of a solid object, None if it isn't one"""
    o = oi
    while o >= 0:
        name = g.objects[o]["name"]
        if name in SOLID_SHAPES:
            return SOLID_SHAPES[name]
        if name in ("obj_solidblock", "obj_solidenemy"):
            return 0
        o = g.objects[o]["parent"]
    return None


def is_child_of(g, oi, parent):
    while oi >= 0:
        if g.objects[oi]["name"] == parent:
            return True
        oi = g.objects[oi]["parent"]
    return False


def solid_box(g, inst):
    """an instance's mask box (its sprite's bounding box, scaled), the original's units"""
    o = g.objects[inst["obj"]]
    si = o["mask"] if o["mask"] >= 0 else o["sprite"]
    s = g.sprites[si]
    # (bbox margins: left, right, bottom, top, the right and bottom ones included)
    l, rr, b, t = s["ml"], s["mr"] + 1, s["mb"] + 1, s["mt"]
    sx, sy = inst["sx"], inst["sy"]
    xa, xb = inst["x"] + (l - s["ox"]) * sx, inst["x"] + (rr - s["ox"]) * sx
    ya, yb = inst["y"] + (t - s["oy"]) * sy, inst["y"] + (b - s["oy"]) * sy
    return int(round(min(xa, xb))), int(round(min(ya, yb))), int(round(max(xa, xb))), int(round(max(ya, yb)))


def pack_room(g, r, tiles, used_objs, obj_ids):
    dark = is_dark(r)
    k = 2 if dark else 1
    w, h = r["w"] // k, r["h"] // k
    gcols, grows = (w + 19) // 20, (h + 19) // 20

    def grid_of(img, front):
        a = np.array(img)
        if k == 2:
            a = a[::2, ::2]
        full = np.zeros((grows * 20, gcols * 20, 4), np.uint8)
        full[:a.shape[0], :a.shape[1]] = a[:grows * 20, :gcols * 20]
        rgb = rgb565_array(full[..., :3])
        clear = full[..., 3] < 128
        ids = []
        any_tile = False
        for ty in range(grows):
            for tx in range(gcols):
                blk = rgb[ty * 20:ty * 20 + 20, tx * 20:tx * 20 + 20]
                cl = clear[ty * 20:ty * 20 + 20, tx * 20:tx * 20 + 20]
                if cl.all():
                    ids.append(0xFFFF)
                elif front or cl.any():
                    if not front:
                        blk = blk.copy()
                        blk[cl] = bgcol
                        ids.append(tiles.add(blk))
                    else:
                        ids.append(tiles.add(blk, cl))
                    any_tile = True
                else:
                    ids.append(tiles.add(blk))
                    any_tile = True
        return ids, any_tile

    bgcol = 0
    for L in r["layers"]:
        if L["type"] == 1 and L["sprite"] < 0 and L["visible"]:
            c = L["color"]
            bgcol = rgb565(c & 255, (c >> 8) & 255, (c >> 16) & 255)
    back = render_room(g, r, False)
    front = render_room(g, r, True)
    grid, _ = grid_of(back, False)
    fgrid, has_front = grid_of(front, True)
    solids, insts = [], []
    for n, inst in enumerate(r["objects"]):
        oi = inst["obj"]
        if oi < 0:
            continue
        kind = solid_kind(g, oi)
        if kind is not None and not is_child_of(g, oi, "obj_interactablesolid") and g.objects[oi]["name"] not in used_objs:
            x0, y0, x1, y1 = solid_box(g, inst)
            if kind in (1, 2, 3, 4) and (inst["sx"] < 0) != (inst["sy"] < 0) and False:
                pass
            if inst["sx"] < 0:   # a mirrored triangle: its square corner on the other side
                kind = {1: 2, 2: 1, 3: 4, 4: 3}.get(kind, kind)
            if inst["sy"] < 0:
                kind = {1: 3, 3: 1, 2: 4, 4: 2}.get(kind, kind)
            # (byte 1: 1 for the enemies' own walls, obj_solidenemy: Kris walks through them)
            solids.append(struct.pack("<BBhhhh", kind, 1 if g.objects[oi]["name"] == "obj_solidenemy" else 0,
                                      x0, y0, x1, y1))
        name = g.objects[oi]["name"]
        if name in used_objs:
            insts.append(struct.pack("<Hhhhhh", obj_ids[name], inst["x"], inst["y"], int(round(inst["sx"] * 100)),
                                     int(round(inst["sy"] * 100)), n))
    return dict(dark=dark, w=r["w"], h=r["h"], gcols=gcols, grows=grows, grid=grid,
                fgrid=fgrid if has_front else None, solids=solids, insts=insts, bgcol=bgcol)


# ------------------------------------------------------------------ what the code names
def names_in_sources():
    words = set()
    for fn in sorted(os.listdir(SRC)):
        if fn.endswith((".c", ".h")) and fn != "data.h":
            with open(os.path.join(SRC, fn), encoding="utf-8") as f:
                words |= set(re.findall(r"\b[A-Za-z_][A-Za-z0-9_]*\b", f.read()))
    return words


def text_bytes(s):
    out = bytearray()
    for ch in s:
        o = ord(ch)
        out += bytes([o]) if o < 256 else b"?"
    return bytes(out)


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    ref = sys.argv[1]
    g = GM.GM(os.path.join(ref, "data.win"))
    with open(os.path.join(ref, "lang_en.json"), encoding="utf-8") as f:
        lang = json.load(f)
    words = names_in_sources()
    spr_names = [s["name"] for s in g.sprites]
    room_names = [r["name"] for r in g.rooms]
    obj_names = [o["name"] for o in g.objects]
    # a sprite named with _h after it (spr_btfight_h): the same at half its size (the battle's own pictures,
    # drawn at 640 x 480 there)
    sprites = [n for n in spr_names if n in words] + [n + "_h" for n in spr_names if n + "_h" in words]
    rooms = [n for n in room_names if n in words]
    objs = sorted(n for n in obj_names if n in words)
    texts = sorted(k for k in lang if k in words)
    obj_ids = {n: i for i, n in enumerate(objs)}
    blocks = Blocks()
    secs = {}

    # sprites
    spr_table, frames, starts = bytearray(), [], []
    for name in sprites:
        half = name.endswith("_h") and name not in spr_names
        s = g.sprite(name[:-2] if half else name)
        fr = g.sprite_frames(s["name"])
        if half:
            fr = [halve(f) for f in fr]
            s = dict(s, ox=s["ox"] // 2, oy=s["oy"] // 2)
        fr, cx, cy = crop_frames(fr)
        starts.append(len(frames))
        spr_table += struct.pack("<HH", len(frames), len(fr))
        for im in fr:
            frames.append(encode_frame(im, s["ox"] - cx, s["oy"] - cy))
    secs["SPRITES"] = struct.pack("<I", len(sprites)) + bytes(spr_table)
    secs["FRAMES"] = blocks.place(frames, groups_by_size([len(f) for f in frames], BLOCK_FRAMES, starts))

    # rooms
    tiles = TileSet()
    room_recs, grid, solids, insts = bytearray(), bytearray(), bytearray(), bytearray()
    tile_order = []
    for name in rooms:
        pr = pack_room(g, g.room(name), tiles, set(objs), obj_ids)
        goff = len(grid) // 2
        grid += b"".join(struct.pack("<H", t) for t in pr["grid"])
        foff = 0xFFFFFFFF
        if pr["fgrid"]:
            foff = len(grid) // 2
            grid += b"".join(struct.pack("<H", t) for t in pr["fgrid"])
        soff, ioff = len(solids) // 10, len(insts) // 12
        solids += b"".join(pr["solids"])
        insts += b"".join(pr["insts"])
        room_recs += struct.pack("<HHHHIIIHIHBBH", pr["w"], pr["h"], pr["gcols"], pr["grows"], goff, foff,
                                 soff, len(pr["solids"]), ioff, len(pr["insts"]), 1 if pr["dark"] else 0, 0, pr["bgcol"])
        tile_order.append(len(tiles.blobs))
    assert len(room_recs) == 32 * len(rooms)
    secs["TILES"] = blocks.place(tiles.blobs, groups_by_size([len(b) for b in tiles.blobs], BLOCK_TILES, tile_order))
    secs["ROOMS"] = struct.pack("<I", len(rooms)) + bytes(room_recs)
    secs["GRID"] = bytes(grid)
    secs["SOLIDS"] = bytes(solids)
    secs["INSTS"] = bytes(insts)
    secs["FONTS"] = table([make_font(g, f) for f in FONTS])
    # texts
    out, offs = bytearray(), []
    base = 4 + 4 * len(texts)
    for k in texts:
        offs.append(base + len(out))
        out += text_bytes(lang[k]) + b"\0"
    secs["STRINGS"] = struct.pack("<I", len(texts)) + b"".join(struct.pack("<I", o) for o in offs) + bytes(out)
    secs["BLOCKS"] = blocks.section()
    secs["ORDER"] = b"".join(struct.pack("<H", room_names.index(n)) for n in rooms)

    head = b"DRD1" + struct.pack("<I", len(SECTIONS))
    pos = len(head) + 4 * len(SECTIONS)
    body, offs = bytearray(), []
    for s in SECTIONS:
        while (pos + len(body)) % 4:
            body.append(0)
        offs.append(pos + len(body))
        body += secs[s]
    data = head + b"".join(struct.pack("<I", o) for o in offs) + bytes(body)
    with open(OUT_BIN, "wb") as f:
        f.write(data)

    hl = ["/* made by tools/pack.py: the numbers of what data.bin holds, by the original's names */",
          "#ifndef DR_DATA_H", "#define DR_DATA_H", "enum {"]
    hl += ["  SEC_%s = %d," % (s, i) for i, s in enumerate(SECTIONS)]
    hl += ["};", "enum {"]
    hl += ["  %s = %d," % (n, i) for i, n in enumerate(sprites)]
    hl += ["  NUM_SPRITES = %d" % len(sprites), "};", "enum {"]
    hl += ["  %s = %d," % (n, i) for i, n in enumerate(rooms)]
    hl += ["  NUM_ROOMS = %d" % len(rooms), "};", "enum {"]
    hl += ["  %s = %d," % (n, i) for i, n in enumerate(objs)]
    hl += ["  NUM_OBJS = %d" % len(objs), "};", "enum {"]
    hl += ["  %s = %d," % (k, i) for i, k in enumerate(texts)]
    hl += ["  NUM_TEXTS = %d" % len(texts), "};", "enum {"]
    hl += ["  FONT_%s = %d," % (f[4:].upper(), i) for i, f in enumerate(FONTS)]
    hl += ["};", "#endif", ""]
    with open(OUT_H, "w") as f:
        f.write("\n".join(hl))
    print("data.bin %d bytes: %d sprites (%d frames), %d rooms, %d tiles, %d objects, %d texts" % (
        len(data), len(sprites), len(frames), len(rooms), len(tiles.blobs), len(objs), len(texts)))
    for s in SECTIONS:
        print("  %-8s %8d" % (s, len(secs[s])))


if __name__ == "__main__":
    main()
