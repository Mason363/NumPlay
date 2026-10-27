#!/usr/bin/env python3
"""Undertale's pictures, rooms and fonts -> src/data.bin + src/data.h.

    python3 -I tools/pack.py REF        (REF: the folder tools/fetch.py filled)

data.bin: "UTD1", the number of sections, their offsets, then the sections (SEC_* in data.h):
- SPRITES: for each sprite (SPR_*) its first frame and its number of frames (u16, u16)
- FRAMES: each frame: w, h, origin x, y (u16, u16, s16, s16), bpp, colours (u8, u8), the colours (RGB565),
  each row's offset (u16) from the rows' data, then the rows: runs of (skip, length, length pixels of bpp bits,
  lowest first, the run padded to a byte), until the row's width
- TILES: the areas' 20 x 20 tiles: bpp (0: one colour), colours, the colours (RGB565), 400 pixels of bpp bits
- ROOMS: 32 bytes each (res.c room_info): the room's rectangle on its area's map, its tile grid and
  collision map (4-pixel cells, 1 walkable), its area, flags (1: ice, its bits after the collision's) and name
- GRID, COLL: the rooms' tile ids (u16, 0xFFFF none) and collision bits
- FONTS: height, first character, count, flags, then 8 bytes a glyph (advance, w, h, ox, oy, 0,
  bitmap offset u16) and the bitmaps (rows of whole bytes, highest bit first)
- STRINGS: the room names and other texts the data refers to
- DOORS: for each room its first door and how many (u16, u16), then the doors: 16 bytes each (x, y, w, h on
  the map, the room it leads to, where Frisk's top-left lands, the way he faces)
- VIEWS: for each room what the camera keeps to (x, y, w, h on the map): the game's own room, where the room's
  rectangle is less than it (its tiles cover both)
- BLOCKS: the frames and tiles themselves, in LZMA streams the game unpacks when it needs them (blk.c): the
  count, the 5 LZMA properties bytes (3 of padding), then for each block its stream's offset (u32) and its size
  unpacked (u32), then the streams. FRAMES and TILES only say where each frame or tile is: its block and its
  offset in it (u16, u16). A sprite's frames share blocks (of 16 KB at most); a room's tiles go in 8 x 8 squares,
  so the tiles a screen shows sit in few blocks (of 2 KB)."""
import lzma
import os
import struct
import sys

import numpy as np
from scipy import ndimage, signal
from PIL import Image, ImageDraw, ImageFont, ImageSequence

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rooms as R  # noqa: E402
import sprites as S  # noqa: E402

OUT_BIN = os.path.join(HERE, "..", "src", "data.bin")
OUT_H = os.path.join(HERE, "..", "src", "data.h")
SECTIONS = ["SPRITES", "FRAMES", "TILES", "ROOMS", "GRID", "COLL", "FONTS", "STRINGS", "DOORS", "BLOCKS", "VIEWS"]
BLOCK_TILES, BLOCK_FRAMES = 2048, 16384   # the most bytes a block holds, unpacked (a frame bigger: alone)
MAX_BLOCKS = 4096                         # (blk.c's table)
LZMA_FILTERS = [{"id": lzma.FILTER_LZMA1, "dict_size": 1 << 16, "lc": 0, "lp": 0, "pb": 0}]


class Blocks:
    """the BLOCKS section, and the tables that point into it"""
    def __init__(self):
        self.raw = []

    def place(self, entries, groups):
        """entries (bytes) put in blocks, one per group of their indices -> u32 count, (block, offset) each"""
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
        props = bytes([0]) + struct.pack("<I", 1 << 16) + bytes(3)   # lc = lp = pb = 0, a 64 KB dictionary
        head = struct.pack("<I", len(streams)) + props
        base = len(head) + 8 * len(streams)
        table, body = bytearray(), bytearray()
        for r, c in zip(self.raw, streams):
            table += struct.pack("<II", base + len(body), len(r))
            body += c
        return head + bytes(table) + bytes(body)


def groups_by_size(sizes, limit, starts=()):
    """consecutive indices grouped up to limit bytes, a new group at each of starts too"""
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


class Strings:
    def __init__(self):
        self.list, self.ids = [], {}

    def add(self, s):
        if s not in self.ids:
            self.ids[s] = len(self.list)
            self.list.append(s)
        return self.ids[s]

    def blob(self):
        out, offs = bytearray(), []
        base = 4 + 4 * len(self.list)
        for s in self.list:
            offs.append(base + len(out))
            out += s.encode("latin-1") + b"\0"
        return struct.pack("<I", len(self.list)) + b"".join(struct.pack("<I", o) for o in offs) + bytes(out)


def table(entries):
    """u32 count, u32 offsets (from the section's start), then the entries"""
    out, offs = bytearray(), []
    base = 4 + 4 * len(entries)
    for e in entries:
        while (base + len(out)) % 2:
            out.append(0)
        offs.append(base + len(out))
        out += e
    return struct.pack("<I", len(entries)) + b"".join(struct.pack("<I", o) for o in offs) + bytes(out)


def bits_pack(values, bpp):
    """values (ints < 2**bpp), lowest bits first"""
    out = bytearray((len(values) * bpp + 7) // 8)
    for k, v in enumerate(values):
        b = k * bpp
        out[b >> 3] |= v << (b & 7)
    return out


def bpp_for(n):
    return 1 if n <= 2 else 2 if n <= 4 else 4 if n <= 16 else 8


# ------------------------------------------------------------------ frames
def encode_frame(img, ox, oy):
    """an RGBA image -> the frame's bytes (row RLE)"""
    a = np.array(img.convert("RGBA"))
    h, w = a.shape[:2]
    opaque = a[:, :, 3] >= 128
    cols = {}
    for c in map(tuple, a[opaque][:, :3]):
        k = rgb565(*c)
        cols[k] = cols.get(k, 0) + 1
    pal = sorted(cols, key=lambda k: -cols[k])
    if len(pal) > 255:
        raise ValueError("a frame with more than 255 colours")
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
                run.append(index[rgb565(*a[y, x, :3])])
                x += 1
            data += bytes([skip, len(run)]) + bits_pack(run, bpp)
        if len(data) > 0xFFFF:
            raise ValueError("a frame too big")
    head = struct.pack("<HHhhBB", w, h, ox, oy, bpp, len(pal)) + b"".join(struct.pack("<H", k) for k in pal)
    return head + b"".join(struct.pack("<H", r) for r in rows) + bytes(data)


def halve(img):
    """a sprite the wiki shows at twice the game's size -> the game's size (the phase where 2 x 2 blocks agree)"""
    a = np.array(img.convert("RGBA")).astype(np.int32)
    h, w = a.shape[:2]
    best = None
    for py in (0, 1):
        for px in (0, 1):
            b = a[py:py + (h - py) // 2 * 2, px:px + (w - px) // 2 * 2]
            if not b.size:
                continue
            q = b.reshape(b.shape[0] // 2, 2, b.shape[1] // 2, 2, 4)
            err = np.abs(q - q[:, :1, :, :1]).sum()
            if best is None or err < best[0]:
                best = (err, py, px)
    _, py, px = best
    b = a[py::2, px::2]
    return Image.fromarray(b.astype(np.uint8), "RGBA")


def shrink(img):
    """art drawn for the original's 640 x 480 battle -> half its size: each 2 x 2 block becomes its most common
    opaque colour (or clear, if most of it is)"""
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
            # ties: the darker colour keeps outlines
            best = sorted(zip(counts, [-int(c.sum()) for c in cols], range(len(cols))))[-1][2]
            out[y, x, :3] = cols[best]
            out[y, x, 3] = 255
    return Image.fromarray(out, "RGBA")


def load_frames(ref, src):
    """'demo:spr_name' (all its numbered frames), 'wiki:File.png' or .gif (all frames, halved), 'png:path'"""
    kind, name = src.split(":", 1)
    if kind in ("demo", "demo2"):
        d = os.path.join(ref, "demo", "mac", "sprites")
        out, k = [], 0
        while os.path.exists(os.path.join(d, "%s %d.png" % (name, k))):
            out.append(Image.open(os.path.join(d, "%s %d.png" % (name, k))).convert("RGBA"))
            k += 1
        if not out:
            raise FileNotFoundError(src)
        return [shrink(f) for f in out] if kind == "demo2" else out
    if kind == "demobg":
        return [Image.open(os.path.join(ref, "demo", "mac", "bgs", name + ".png")).convert("RGBA")]
    if kind in ("wiki", "wiki1"):
        im = Image.open(os.path.join(ref, "wiki", "sprites", name))
        frames = []
        for fr in ImageSequence.Iterator(im):
            f = fr.convert("RGBA")
            frames.append(halve(f) if kind == "wiki" else f)
        return frames
    if kind == "png":
        return [Image.open(os.path.join(HERE, name)).convert("RGBA")]
    if kind == "move":
        # 'move:File.gif:x0,y0,x1,y1:i,j,...': the biggest thing that moves in a part of a screenshot GIF (against
        # its median frame), in frames i, j...; halved
        name, box, which = name.split(":")
        x0, y0, x1, y1 = map(int, box.split(","))
        im = Image.open(os.path.join(ref, "wiki", "screens", name))
        st = np.stack([np.asarray(f.convert("RGB"))[y0:y1, x0:x1] for f in ImageSequence.Iterator(im)]).astype(int)
        bg = np.median(st, axis=0)
        out = []
        for i in map(int, which.split(",")):
            m = np.abs(st[i] - bg).sum(2) > 30
            lab, n = ndimage.label(ndimage.binary_dilation(m, iterations=4))
            k = int(np.argmax(ndimage.sum(m, lab, range(1, n + 1)))) + 1
            sl = ndimage.find_objects(lab)[k - 1]
            a = (m & (lab == k))[sl]
            rgba = np.dstack([st[i][sl].astype(np.uint8), (a * 255).astype(np.uint8)])
            # each frame on a canvas of the window's size, its feet at the bottom middle: frames line up
            f = Image.fromarray(rgba, "RGBA")
            canvas = Image.new("RGBA", (x1 - x0, y1 - y0), (0, 0, 0, 0))
            canvas.paste(f, ((x1 - x0 - f.width) // 2, y1 - y0 - f.height), f)
            out.append(halve(canvas))
        return out
    if kind == "map":
        # 'map:Map.png:x,y,w,h': a piece of an area's map, as it is
        name, box = name.split(":")
        x, y, w, h = map(int, box.split(","))
        return [Image.open(os.path.join(ref, "wiki", "maps", name)).convert("RGBA").crop((x, y, x + w, y + h))]
    if kind == "gen":   # drawn by tools/sprites.py
        return S.GEN[name]()
    if kind == "wiki2":   # a wiki file of the 640 x 480 battle, at the game's size: halved
        im = Image.open(os.path.join(ref, "wiki", "sprites", name))
        return [shrink(fr.convert("RGBA")) for fr in ImageSequence.Iterator(im)]
    raise ValueError(src)


def crop_frames(frames):
    """all frames cropped to the union of their opaque pixels; returns the frames and the crop's top-left"""
    box = None
    for f in frames:
        b = f.getchannel("A").point(lambda v: 255 if v >= 128 else 0).getbbox()
        if b:
            box = b if box is None else (min(box[0], b[0]), min(box[1], b[1]), max(box[2], b[2]), max(box[3], b[3]))
    if box is None:
        return frames, 0, 0
    return [f.crop(box) for f in frames], box[0], box[1]


# ------------------------------------------------------------------ fonts
def make_font(ref, spec):
    path = os.path.join(ref, "fonts", spec["ttf"]) if not spec["ttf"].startswith("/") else spec["ttf"]
    font = ImageFont.truetype(path, spec["size"])
    first, last = 32, 127
    glyphs, bits = bytearray(), bytearray()
    asc = spec.get("ascent", spec["size"])
    for c in range(first, last):
        ch = chr(c) if c < 127 else "♥"
        if c == 127 and "heart" not in spec:
            ch = "?"
        im = Image.new("L", (spec["size"] * 3, spec["size"] * 3), 0)
        d = ImageDraw.Draw(im)
        d.fontmode = "1"
        d.text((spec["size"], spec["size"]), ch, font=font, fill=255)
        adv = int(round(font.getlength(ch))) if "advance" not in spec else spec["advance"]
        bb = im.getbbox()
        if bb is None:
            glyphs += struct.pack("<BBBbbBH", adv, 0, 0, 0, 0, 0, len(bits))
            continue
        g = np.array(im.crop(bb)) >= 128
        gh, gw = g.shape
        ox, oy = bb[0] - spec["size"], bb[1] - spec["size"] - (spec["size"] - asc)
        off = len(bits)
        for row in g:
            rb = bytearray((gw + 7) // 8)
            for x, v in enumerate(row):
                if v:
                    rb[x >> 3] |= 0x80 >> (x & 7)
            bits += rb
        glyphs += struct.pack("<BBBbbBH", adv, gw, gh, ox, oy, 0, off)
    return struct.pack("<BBBB", spec["height"], first, last - first, 0) + bytes(glyphs) + bytes(bits)


# ------------------------------------------------------------------ areas and rooms
class Area:
    """an area's map: its colours, as indices into a palette, and its tiles"""

    def __init__(self, ref, spec):
        self.ref = ref
        im = Image.open(os.path.join(ref, "wiki", "maps", spec["map"]))
        if im.mode == "P":
            self.idx = np.array(im)
            p = im.getpalette()
            self.rgb = [tuple(p[3 * i:3 * i + 3]) for i in range(len(p) // 3)]
            self.transparent = im.info.get("transparency", None)
            if not isinstance(self.transparent, int):   # (bytes: none of the palette's)
                self.transparent = None
        else:
            # every colour exactly (some maps have more than 256)
            a = np.asarray(im.convert("RGB")).astype(np.int32)
            cols, inv = np.unique((a[..., 0] << 16) | (a[..., 1] << 8) | a[..., 2], return_inverse=True)
            self.idx = inv.reshape(a.shape[:2]).astype(np.int32)
            self.rgb = [(int(c) >> 16, int(c) >> 8 & 255, int(c) & 255) for c in cols]
            self.transparent = None
        self.spec = spec
        # (what the patches take out: no background is copied from there)
        self.covered = [tuple(q[1:5]) for q in spec.get("patches", []) if q[0] in ("erase", "heal")]
        for patch in spec.get("patches", []):
            self.patch(patch)

    def black_index(self):
        for i, c in enumerate(self.rgb):
            if c == (0, 0, 0):
                return i
        return None

    def patch(self, p):
        """changes to the map, before rooms are cut from it: ('fill', x, y, w, h, (r, g, b)),
        ('copy', sx, sy, w, h, dx, dy) or ('mirror', ...) the same, flipped left to right, or ('erase', x, y, w, h,
        [colours]): what isn't one of the colours takes the colour of the nearest pixel that is (a monster painted
        on the floor)"""
        if p[0] == "fill":
            _, x, y, w, h, c = p
            self.idx[y:y + h, x:x + w] = self.color_index(c)
        elif p[0] == "copy":
            _, sx, sy, w, h, dx, dy = p
            self.idx[dy:dy + h, dx:dx + w] = self.idx[sy:sy + h, sx:sx + w].copy()
        elif p[0] == "erase":
            # the nearest pixel of the colours, looking a little around the rectangle (only it changes)
            _, x, y, w, h, cols, *drop = p
            if cols == "ring":   # a little more than the box: the outline's last pixels
                x, y, w, h = x - 2, y - 2, w + 4, h + 4
            m = 4
            x0, y0 = max(0, x - m), max(0, y - m)
            win = self.idx[y0:y + h + m, x0:x + w + m]
            if cols == "ring":
                # the colours just around the rectangle (but those of drop): what's behind the monster
                ring = np.ones(win.shape, bool)
                ring[y - y0:y - y0 + h, x - x0:x - x0 + w] = False
                # (black: the outlines of what's erased, unless the patch says otherwise)
                dropped = drop[0] if drop else [(0, 0, 0)]
                # (only colours common around it: a few pixels of a colour are the monster's own, sticking out)
                u, n = np.unique(win[ring], return_counts=True)
                ids = {int(i) for i, c in zip(u, n) if c >= 0.05 * ring.sum()} - \
                    {i for i, k in enumerate(self.rgb) if k in dropped}
                keep = np.isin(win, list(ids))
            else:
                keep = np.isin(win, [i for i, k in enumerate(self.rgb) if k in [tuple(c) for c in cols]])
            if not keep.any():
                raise ValueError("erase %s: none of its colours there" % (p,))
            _, (iy, ix) = ndimage.distance_transform_edt(~keep, return_indices=True)
            filled = win[iy, ix]
            self.idx[y:y + h, x:x + w] = filled[y - y0:y - y0 + h, x - x0:x - x0 + w]
        elif p[0] == "mirror":
            _, sx, sy, w, h, dx, dy = p
            self.idx[dy:dy + h, dx:dx + w] = self.idx[sy:sy + h, sx:sx + w][:, ::-1].copy()
        elif p[0] == "heal":
            self.heal(*p[1:])
        elif p[0] == "shot":
            self.shot(*p[1:])
        else:
            raise ValueError(p)

    def shot(self, name, sx, sy, x, y, w, h):
        """('shot', file, sx, sy, x, y, w, h): the rectangle taken from one of the wiki's screenshots of the game
        (REF/wiki/sprites or screens, 640 x 480: the game's screen doubled), its top-left at (sx, sy) on the map: what
        the room looks like when no one stands there"""
        for d in ("sprites", "screens"):
            path = os.path.join(self.ref, "wiki", d, name)
            if os.path.exists(path):
                break
        im = np.asarray(Image.open(path).convert("RGB"))[::2, ::2].astype(np.int32)
        part = im[y - sy:y - sy + h, x - sx:x - sx + w]
        assert part.shape[:2] == (h, w), ("shot %s: the rectangle isn't in it" % name)
        key = (part[..., 0] << 16) | (part[..., 1] << 8) | part[..., 2]
        pal = np.array([(r << 16) | (g << 8) | b for r, g, b in self.rgb], np.int64)
        out = np.zeros(key.shape, self.idx.dtype)
        for k in np.unique(key):
            hit = np.nonzero(pal == k)[0]
            if len(hit):
                i = int(hit[0])
            else:   # (a colour the map hasn't: its nearest)
                c = np.array([k >> 16, k >> 8 & 255, k & 255])
                i = int(np.argmin(((np.array(self.rgb) - c) ** 2).sum(axis=1)))
            out[key == k] = i
        self.idx[y:y + h, x:x + w] = out

    def heal(self, x, y, w, h, radius=320, m=6, rows=None):
        """('heal', x, y, w, h[, radius[, border[, rows]]]): what's drawn there taken out, the background put back:
        the rectangle of the map nearby (within radius; within `rows` up or down if given) whose border (m pixels
        around it) is most like this one's is copied over it (pixel art repeats its tiles and patterns: the same
        planks, bricks or snow)"""
        H, W = self.idx.shape
        tx0, ty0, tx1, ty1 = max(0, x - m), max(0, y - m), min(W, x + w + m), min(H, y + h + m)
        t = self.idx[ty0:ty1, tx0:tx1]
        ring = np.ones(t.shape, bool)
        ring[y - ty0:y - ty0 + h, x - tx0:x - tx0 + w] = False
        # (not what other patches take out, around it)
        for cx, cy, cw, ch in self.covered:
            if (cx, cy, cw, ch) != (x, y, w, h):
                ring[max(0, cy - ty0):max(0, cy + ch - ty0), max(0, cx - tx0):max(0, cx + cw - tx0)] = False
        rx0, ry0 = max(0, tx0 - radius), max(0, ty0 - radius)
        rx1, ry1 = min(W, tx1 + radius), min(H, ty1 + radius)
        reg = self.idx[ry0:ry1, rx0:rx1]
        score = 0
        for c in np.unique(t[ring]):
            k = ((t == c) & ring).astype(np.float32)
            score = score + signal.fftconvolve((reg == c).astype(np.float32), k[::-1, ::-1], mode="valid")
        score = np.rint(score)
        oh, ow = score.shape
        sy = ry0 + np.arange(oh)[:, None] + (y - ty0)   # the source rectangle's top-left, for each offset
        sx = rx0 + np.arange(ow)[None, :] + (x - tx0)
        bad = (np.abs(sx - x) < w + m) & (np.abs(sy - y) < h + m)
        if rows is not None:
            bad |= np.abs(sy - y) > rows
        for cx, cy, cw, ch in self.covered:
            bad |= (sx < cx + cw) & (cx < sx + w) & (sy < cy + ch) & (cy < sy + h)
        score[bad] = -1
        best = score.max()
        cand = np.argwhere(score == best)
        d = (sx[0, cand[:, 1]] - x) ** 2 + (sy[cand[:, 0], 0] - y) ** 2
        oy, ox = cand[int(np.argmin(d))]
        bx, by = int(sx[0, ox]), int(sy[oy, 0])
        self.idx[y:y + h, x:x + w] = self.idx[by:by + h, bx:bx + w].copy()
        miss = int(ring.sum() - best)
        if miss:
            print("heal %s %d,%d %dx%d: from %d,%d, %d of %d border pixels differ" % (
                self.spec["name"], x, y, w, h, bx, by, miss, int(ring.sum())))

    def color_index(self, c):
        for i, k in enumerate(self.rgb):
            if k == tuple(c) and i != self.transparent:
                return i
        raise ValueError("colour %s not in the map" % (c,))


def room_pixels(area, r):
    """the room's rectangle of the map, as RGB565 per pixel; outside the map and its clear pixels black"""
    x, y, w, h = r["rect"]
    H, W = area.idx.shape
    out = np.zeros((h, w), np.int32)
    xs0, ys0, xs1, ys1 = max(x, 0), max(y, 0), min(x + w, W), min(y + h, H)
    sub = area.idx[ys0:ys1, xs0:xs1]
    lut = np.array([rgb565(*c) for c in area.rgb] + [0] * max(0, 256 - len(area.rgb)), np.int32)
    if area.transparent is not None:
        lut[area.transparent] = 0
    out[ys0 - y:ys1 - y, xs0 - x:xs1 - x] = lut[sub]
    return out, sub, (xs0 - x, ys0 - y)


def floor_mask(area, r, x0, y0, x1, y1):
    """the pixels of a rectangle of the map in the room's floor colours"""
    walk_idx = [i for c in area.spec["floor"] + r.get("floor_colors", []) for i, k in enumerate(area.rgb)
                if k == tuple(c) and i != area.transparent]
    return np.isin(area.idx[y0:y1, x0:x1], walk_idx)


def wave_walls(area, x0, y0, x1, y1):
    """the pixels of a rectangle of the map that are a wall drawn with waves (the area's "waves": line colour,
    background colours, reach, gap): Waterfall draws its walls and its darker floors in the same colours, the walls
    with waves across them, the floors dotted. A wave is a thin curve of the line colour, 12 pixels across or more;
    the wall is what's between two waves `gap` pixels apart or less, in each column, and `reach` pixels past the
    first and the last, up to the first pixel of another colour (the black line at the wall's foot)"""
    H, W = area.idx.shape
    pad = 24
    out = np.zeros((y1 - y0, x1 - x0), bool)
    xs0, ys0, xs1, ys1 = max(x0, 0), max(y0, 0), min(x1, W), min(y1, H)
    if xs1 <= xs0 or ys1 <= ys0:
        return out
    X0, Y0, X1, Y1 = max(0, xs0 - pad), max(0, ys0 - pad), min(W, xs1 + pad), min(H, ys1 + pad)
    win = area.idx[Y0:Y1, X0:X1]
    wall = np.zeros(win.shape, bool)
    for line, backs, reach, gap in area.spec.get("waves", []):
        is_line = np.isin(win, [i for i, k in enumerate(area.rgb) if k == tuple(line)])
        ok = is_line | np.isin(win, [i for i, k in enumerate(area.rgb) if k in [tuple(c) for c in backs]])
        lab, n = ndimage.label(is_line, structure=np.ones((3, 3), int))
        if not n:
            continue
        # thin (few of its pixels have the line colour all round) and long
        inner = is_line.copy()
        inner[1:] &= is_line[:-1]
        inner[:-1] &= is_line[1:]
        inner[:, 1:] &= is_line[:, :-1]
        inner[:, :-1] &= is_line[:, 1:]
        size = ndimage.sum(is_line, lab, range(n + 1))
        full = ndimage.sum(inner, lab, range(n + 1))
        good = np.zeros(n + 1, bool)
        for i, sl in enumerate(ndimage.find_objects(lab), 1):
            good[i] = sl[1].stop - sl[1].start >= 12 and full[i] <= 0.1 * size[i]
        wave = good[lab]
        # in each column, the last wave above and the last break (a pixel of another colour) above; the same below
        ys = np.arange(win.shape[0])[:, None]
        wa = np.maximum.accumulate(np.where(wave, ys, -1), axis=0)
        ba = np.maximum.accumulate(np.where(ok, -1, ys), axis=0)
        # (below: the same, rows counted from the bottom)
        wb = np.maximum.accumulate(np.where(wave[::-1], ys, -1), axis=0)[::-1]
        bb = np.maximum.accumulate(np.where(ok[::-1], -1, ys), axis=0)[::-1]
        above = wa > ba
        below = wb > bb
        top = win.shape[0] - 1
        da, db = ys - wa, (top - ys) - wb
        wall |= ok & ((above & below & (da + db <= gap)) | (above & (da <= reach)) | (below & (db <= reach)))
    out[ys0 - y0:ys1 - y0, xs0 - x0:xs1 - x0] = wall[ys0 - Y0:ys1 - Y0, xs0 - X0:xs1 - X0]
    return out


KEEP = -32768   # a door's landing: Frisk's own x or y, kept


def apply_links(area, rooms):
    """the links between rooms one above the other through a dark doorway, ("v", lower, upper, x, w, y): a door
    each way between the lower room's floor under the doorway (x .. x + w, y in it) and the upper's above it, the
    floor made walkable up to them. ("up", lower, x, w, y, to, (tx, ty)): a one-way door, up a chute"""
    for link in R.LINKS:
        lo = rooms.get(link[1])
        if lo is None or lo["area"] != area.spec["name"]:
            continue
        if link[0] == "h":
            # side by side: the left room's right edge and the right one's left edge, Frisk's height kept
            ri = rooms[link[2]]
            y, h = link[3:5]
            lx, _, lw, _ = lo["rect"]
            rx = ri["rect"][0]
            lo.setdefault("fix", []).append(("walk", lx + lw - 8, y, 8, h))
            ri.setdefault("fix", []).append(("walk", rx, y, 8, h))
            lo.setdefault("doors", []).append(((lx + lw - 2, y, 2, h), link[2], (rx + 2, KEEP), "right"))
            ri.setdefault("doors", []).append(((rx, y, 2, h), link[1], (lx + lw - 22, KEEP), "left"))
            continue
        x, w, yd = link[3:6] if link[0] == "v" else link[2:5]
        cx = x + w // 2
        lx, ly, lw, lh = lo["rect"]
        col = floor_mask(area, lo, cx, yd, cx + 1, ly + lh)[:, 0]
        if not col.any():
            sys.exit("link %s: no floor under the doorway" % (link,))
        ya = yd + int(np.argmax(col))           # the lower room's floor, where the doorway ends
        if link[0] == "up":
            # a chute is narrow: a little room either side to walk into it
            to, (tx, ty) = link[5:7]
            lo.setdefault("fix", []).append(("walk", x - 4, ya - 8, w + 8, 12))
            lo.setdefault("doors", []).append(((x - 2, ya - 6, w + 4, 4), to, (tx, ty), "down"))
            continue
        lo.setdefault("fix", []).append(("walk", x, ya - 8, w, 12))
        up = rooms[link[2]]
        ux, uy, uw, uh = up["rect"]
        col = floor_mask(area, up, cx, uy, cx + 1, min(uy + uh, yd))[:, 0]
        if not col.any():
            sys.exit("link %s: no floor above the doorway" % (link,))
        yb = uy + len(col) - int(np.argmax(col[::-1]))   # the upper room's floor, where the doorway starts
        up.setdefault("fix", []).append(("walk", x, yb - 4, w, 4))
        lo.setdefault("doors", []).append(((x, ya - 6, w, 4), link[2], (cx - 10, yb - 34), "up"))
        up.setdefault("doors", []).append(((x, yb - 2, w, 2), link[1], (cx - 10, ya - 12), "down"))
        print("link: %s floor at %d, %s at %d" % (link[1], ya, link[2], yb))


def collision(area, r):
    """4-pixel cells over the room: walkable where the floor's colours fill at least half the cell and
    inside one of its floor rectangles (all of it if it has none), then its own fixes"""
    x, y, w, h = r["rect"]
    cw, ch = (w + 3) // 4, (h + 3) // 4
    H, W = area.idx.shape
    walk_idx = set()
    for c in area.spec["floor"] + r.get("floor_colors", []):
        for i, k in enumerate(area.rgb):
            if k == tuple(c) and i != area.transparent:
                walk_idx.add(i)
    # the map's pixels over the cells, and `margin` more around (what's beyond the room's edge counts too)
    margin = r.get("margin", area.spec.get("margin", 0))
    X0, Y0, X1, Y1 = x - margin, y - margin, x + cw * 4 + margin, y + ch * 4 + margin
    big = np.zeros((Y1 - Y0, X1 - X0), bool)
    xs0, ys0, xs1, ys1 = max(X0, 0), max(Y0, 0), min(X1, W), min(Y1, H)
    big[ys0 - Y0:ys1 - Y0, xs0 - X0:xs1 - X0] = np.isin(area.idx[ys0:ys1, xs0:xs1], list(walk_idx))
    if area.spec.get("waves"):   # (walls in the floor's colours, told apart by their waves)
        big &= ~wave_walls(area, X0, Y0, X1, Y1)
    sub = big[margin:margin + ch * 4, margin:margin + cw * 4]
    if r.get("open"):   # floors of many colours (a carpet, planks): its floor_rects alone say
        sub = np.ones_like(sub)
    cells = sub.reshape(ch, 4, cw, 4).mean(axis=(1, 3)) >= 0.5
    # piles of leaves are floor, outline and all
    soft = area.spec.get("piles")
    pile_big = np.zeros_like(big)
    if soft:
        win = np.zeros(big.shape, area.idx.dtype)
        win[ys0 - Y0:ys1 - Y0, xs0 - X0:xs1 - X0] = area.idx[ys0:ys1, xs0:xs1]
        pile = np.isin(win, [i for i, k in enumerate(area.rgb) if k in [tuple(c) for c in soft]])
        lab, n = ndimage.label(ndimage.binary_dilation(pile, iterations=3))
        for sl in ndimage.find_objects(lab):
            if (sl[0].stop - sl[0].start) * (sl[1].stop - sl[1].start) >= 16:
                pile_big[sl] = True
    # walls drawn in the floor's colours give themselves away by their lines: a cell is floor only with no
    # other colour within `margin` pixels of it (a pile's leaves don't count)
    if margin:
        other = ~big & ~pile_big
        # lines drawn on the floor (a path's outline) aren't walls
        lines = area.spec.get("lines", []) + r.get("lines", [])
        if lines:
            win = np.zeros(big.shape, area.idx.dtype)
            win[ys0 - Y0:ys1 - Y0, xs0 - X0:xs1 - X0] = area.idx[ys0:ys1, xs0:xs1]
            other &= ~np.isin(win, [i for i, k in enumerate(area.rgb) if k in [tuple(c) for c in lines]])
        near = np.zeros_like(sub)
        for dy in range(2 * margin + 1):
            for dx in range(2 * margin + 1):
                near |= other[dy:dy + sub.shape[0], dx:dx + sub.shape[1]]
        cells &= ~near.reshape(ch, 4, cw, 4).any(axis=(1, 3))
    piles = pile_big[margin:margin + ch * 4, margin:margin + cw * 4]
    cells |= piles.reshape(ch, 4, cw, 4).mean(axis=(1, 3)) >= 0.5
    if r.get("close", area.spec.get("close")):
        # a floor's own lines one cell thin (the seams between planks) aren't walls: a cell with floor either side
        up, down = np.zeros_like(cells), np.zeros_like(cells)
        up[1:], down[:-1] = cells[:-1], cells[1:]
        left, right = np.zeros_like(cells), np.zeros_like(cells)
        left[:, 1:], right[:, :-1] = cells[:, :-1], cells[:, 1:]
        cells = cells | (up & down) | (left & right)
    if r.get("floor_rects"):
        keep = np.zeros_like(cells)
        for fx, fy, fw, fh in r["floor_rects"]:
            keep[max(0, (fy - y) // 4):max(0, (fy + fh - y + 3) // 4), max(0, (fx - x) // 4):max(0, (fx + fw - x + 3) // 4)] = True
        cells &= keep
    raw = sub.reshape(ch, 4, cw, 4).mean(axis=(1, 3)) >= 0.5
    for kind, fx, fy, fw, fh, *how in r.get("fix", []):
        sl = (slice(max(0, (fy - y) // 4), max(0, (fy + fh - y + 3) // 4)),
              slice(max(0, (fx - x) // 4), max(0, (fx + fw - x + 3) // 4)))
        # "floor": walkable only where the floor's colours are (the edge of a room, past the margin rule)
        cells[sl] = raw[sl] if how == ["floor"] else kind == "walk"
    out = bytearray()
    for row in cells:
        rb = bytearray((cw + 7) // 8)
        for i, v in enumerate(row):
            if v:
                rb[i >> 3] |= 1 << (i & 7)
        out += rb
    return out, cw, ch


class TileSet:
    def __init__(self):
        self.ids, self.blobs = {}, []
        self.blobs_size = 0

    def add(self, block):
        """a 20 x 20 block of RGB565 values -> its tile id"""
        key = block.tobytes()
        if key in self.ids:
            return self.ids[key]
        flat = block.reshape(-1)
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


def pack_room(area, r, tiles, strings):
    x, y, w, h = r["rect"]
    # what the camera shows: the game's room ("view", when the rectangle is less than it), the map drawn over all of it
    vx, vy, vw, vh = r.get("view", r["rect"])
    ux, uy = min(x, vx), min(y, vy)
    uw, uh = max(x + w, vx + vw) - ux, max(y + h, vy + vh) - uy
    px, _, _ = room_pixels(area, dict(r, rect=(ux, uy, uw, uh)))
    # the grid: 20-pixel tiles aligned on the map's own 20-pixel grid
    gx, gy = ux // 20 * 20, uy // 20 * 20
    gx1, gy1 = (ux + uw + 19) // 20 * 20, (uy + uh + 19) // 20 * 20
    cols, rows = (gx1 - gx) // 20, (gy1 - gy) // 20
    big = np.zeros((rows * 20, cols * 20), np.int32)
    big[uy - gy:uy - gy + uh, ux - gx:ux - gx + uw] = px
    # (new tiles numbered square by square: a screen's tiles are close together, in few blocks)
    ids = {}
    for cy in range(0, rows, 8):
        for cx in range(0, cols, 8):
            for ty in range(cy, min(rows, cy + 8)):
                for tx in range(cx, min(cols, cx + 8)):
                    block = big[ty * 20:ty * 20 + 20, tx * 20:tx * 20 + 20]
                    if block.any():
                        ids[ty, tx] = tiles.add(block.astype(np.uint16))
    grid = b"".join(struct.pack("<H", ids.get((ty, tx), 0xFFFF)) for ty in range(rows) for tx in range(cols))
    coll, cw, ch = collision(area, r)
    flags = r.get("flags", 0)
    if r.get("ice"):
        # the ice: cells mostly of the area's ice colours, after the collision bits (ROOM_ICE)
        ice_idx = [i for i, k in enumerate(area.rgb) if k in [tuple(c) for c in area.spec.get("ice", [])]]
        m = np.zeros((ch * 4, cw * 4), bool)
        sub = np.isin(area.idx[y:y + h, x:x + w], ice_idx)
        m[:sub.shape[0], :sub.shape[1]] = sub
        cells = m.reshape(ch, 4, cw, 4).mean(axis=(1, 3)) >= 0.5
        for fx, fy, fw, fh in r.get("no_ice", []):
            cells[max(0, (fy - y) // 4):(fy + fh - y + 3) // 4, max(0, (fx - x) // 4):(fx + fw - x + 3) // 4] = False
        for row in cells:
            rb = bytearray((cw + 7) // 8)
            for i, v in enumerate(row):
                if v:
                    rb[i >> 3] |= 1 << (i & 7)
            coll += rb
        flags |= 1
    return dict(rect=(x, y, w, h), view=(vx, vy, vw, vh), g=(gx, gy, cols, rows), grid=bytes(grid), coll=bytes(coll), cw=cw, ch=ch,
                area=r["area_id"], flags=flags, name=strings.add(r.get("title", "")))


# ------------------------------------------------------------------ main
def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    ref = sys.argv[1]
    strings = Strings()
    strings.add("")
    defines = []

    # sprites
    spr_entries, frames = [], []
    for k, spec in enumerate(S.SPRITES):
        name, src = spec[0], spec[1]
        opts = spec[2] if len(spec) > 2 else {}
        imgs = []
        for s in (src if isinstance(src, list) else [src]):
            imgs += load_frames(ref, s)
        if opts.get("clear_black"):   # drawn over black anyway: its black is clear, and costs nothing
            out = []
            for im in imgs:
                a = np.array(im)
                a[(a[:, :, 0] < 8) & (a[:, :, 1] < 8) & (a[:, :, 2] < 8)] = 0
                out.append(Image.fromarray(a, "RGBA"))
            imgs = out
        if opts.get("dedupe"):   # GIFs repeat a frame for its duration: once each
            out = []
            for im in imgs:
                if not out or im.tobytes() != out[-1].tobytes():
                    out.append(im)
            imgs = out
        if "box" in opts:
            bx, by, bw, bh = opts["box"]
            imgs = [im.crop((bx, by, bx + bw, by + bh)) for im in imgs]
        if "every" in opts:
            imgs = imgs[::opts["every"]]
        if "frames" in opts:
            imgs = [imgs[i] for i in opts["frames"]]
        if opts.get("clear_bg"):   # the black around a monster (the wiki's GIFs have no see-through) is clear
            out = []
            for im in imgs:
                a = np.array(im)
                dark = (a[:, :, 0] < 8) & (a[:, :, 1] < 8) & (a[:, :, 2] < 8) | (a[:, :, 3] < 128)
                lab, _ = ndimage.label(dark)
                edge = set(np.unique(np.concatenate([lab[0], lab[-1], lab[:, 0], lab[:, -1]]))) - {0}
                a[np.isin(lab, list(edge))] = 0
                out.append(Image.fromarray(a, "RGBA"))
            imgs = out
        if "clear" in opts:   # these colours are see-through (a map's floor behind a thing cut from it)
            out = []
            for im in imgs:
                a = np.array(im)
                for c in opts["clear"]:
                    a[(a[:, :, 0] == c[0]) & (a[:, :, 1] == c[1]) & (a[:, :, 2] == c[2])] = 0
                out.append(Image.fromarray(a, "RGBA"))
            imgs = out
        if "only" in opts:   # only these colours kept (a thing cut from a map that shares the map's colours)
            out = []
            for im in imgs:
                a = np.array(im)
                keep = np.zeros(a.shape[:2], bool)
                for c in opts["only"]:
                    keep |= (a[:, :, 0] == c[0]) & (a[:, :, 1] == c[1]) & (a[:, :, 2] == c[2])
                a[~keep] = 0
                out.append(Image.fromarray(a, "RGBA"))
            imgs = out
        if opts.get("mirror"):
            imgs = [im.transpose(Image.FLIP_LEFT_RIGHT) for im in imgs]
        if "walk" in opts:
            # a walk from a picture standing still: stand, a step (one foot up, the body down a pixel), stand, the
            # other step; the legs are the rows from `top`, split at column `mid`
            top, mid = opts["walk"]
            a = np.array(imgs[0].convert("RGBA"))

            def stepped(c0, c1):
                b = a.copy()
                leg = a[top:, c0:c1].copy()
                b[top:, c0:c1] = 0
                part = b[top - 1:-1, c0:c1]
                b[top - 1:-1, c0:c1] = np.where(leg[..., 3:4] > 0, leg, part)
                body = np.zeros_like(b)
                body[1:top] = b[:top - 1]
                body[top:] = b[top:]
                return Image.fromarray(body, "RGBA")
            imgs = [imgs[0], stepped(0, mid), imgs[0], stepped(mid, a.shape[1])]
        if "tints" in opts:   # the first frame, then it again in each colour (the SOUL's modes)
            a = np.array(imgs[0].convert("RGBA"))
            lum = a[:, :, :3].max(axis=2).astype(np.float32) / 255
            imgs = [imgs[0]]
            for t in opts["tints"]:
                b = a.copy()
                b[:, :, :3] = (lum[:, :, None] * np.array(t, np.float32)).astype(np.uint8)
                imgs.append(Image.fromarray(b, "RGBA"))
        if "scale" in opts:
            imgs = [im.resize((im.width * opts["scale"], im.height * opts["scale"]), Image.NEAREST) for im in imgs]
        w0, h0 = imgs[0].size
        ox, oy = opts.get("origin", (0, 0))
        if opts.get("center"):
            ox, oy = w0 // 2, h0 // 2
        if opts.get("feet"):
            ox, oy = w0 // 2, h0
        if not opts.get("keep"):
            imgs, cx, cy = crop_frames(imgs)
            ox, oy = ox - cx, oy - cy
        spr_entries.append((len(frames), len(imgs)))
        for im in imgs:
            frames.append(encode_frame(im, ox, oy))
        defines.append(("SPR_" + name.upper(), k))
    sec_sprites = struct.pack("<I", len(spr_entries)) + b"".join(struct.pack("<HH", a, b) for a, b in spr_entries)
    blocks = Blocks()
    sec_frames = blocks.place(frames, groups_by_size([len(f) for f in frames], BLOCK_FRAMES, [a for a, _ in spr_entries]))

    # areas and rooms
    tiles = TileSet()
    packed = []
    for ai, aspec in enumerate(R.AREAS):
        area = Area(ref, aspec)
        defines.append(("AREA_" + aspec["name"].upper(), ai))
        apply_links(area, {r["name"]: r for r in R.ROOMS})
        for r in R.ROOMS:
            if r["area"] != aspec["name"]:
                continue
            r["area_id"] = ai
            packed.append((r, pack_room(area, r, tiles, strings)))
    room_tab, grid_blob, coll_blob = bytearray(), bytearray(), bytearray()
    for k, (r, p) in enumerate(packed):
        defines.append(("ROOM_" + r["name"].upper(), k))
        x, y, w, h = p["rect"]
        gx, gy, cols, rows = p["g"]
        room_tab += struct.pack("<hhhhhhHHIIHHBBH", x, y, w, h, gx, gy, cols, rows, len(grid_blob), len(coll_blob),
                                p["cw"], p["ch"], p["area"], p["flags"], p["name"])
        grid_blob += p["grid"]
        coll_blob += p["coll"]
        while len(coll_blob) % 2:
            coll_blob.append(0)
    sec_rooms = struct.pack("<I", len(packed)) + bytes(room_tab)
    sec_views = b"".join(struct.pack("<hhhh", *p["view"]) for _, p in packed)
    names = {r["name"]: k for k, (r, _) in enumerate(packed)}
    dirs = {"down": 0, "up": 1, "left": 2, "right": 3}
    door_index, doors = bytearray(), bytearray()
    ndoors = 0
    for r, _ in packed:
        ds = r.get("doors", [])
        door_index += struct.pack("<HH", ndoors, len(ds))
        for (dx, dy, dw, dh), to, (tx, ty), d in ds:
            doors += struct.pack("<hhhhHhhH", dx, dy, dw, dh, names[to], tx, ty, dirs[d])
            ndoors += 1
    sec_doors = bytes(door_index) + bytes(doors)
    sec_tiles = blocks.place(tiles.blobs, groups_by_size([len(t) for t in tiles.blobs], BLOCK_TILES))

    # fonts
    fonts = []
    for k, spec in enumerate(S.FONTS):
        fonts.append(make_font(ref, spec))
        defines.append(("FONT_" + spec["name"].upper(), k))
    sec_fonts = table(fonts)

    sections = [sec_sprites, sec_frames, sec_tiles, sec_rooms, bytes(grid_blob), bytes(coll_blob), sec_fonts,
                strings.blob(), sec_doors, blocks.section(), sec_views]
    head = b"UTD1" + struct.pack("<I", len(sections))
    offs, body = [], bytearray()
    base = len(head) + 4 * len(sections)
    for s in sections:
        while (base + len(body)) % 4:
            body.append(0)
        offs.append(base + len(body))
        body += s
    blob = head + b"".join(struct.pack("<I", o) for o in offs) + bytes(body)
    with open(OUT_BIN, "wb") as f:
        f.write(blob)

    with open(OUT_H, "w") as f:
        f.write("/* made by tools/pack.py: the contents of data.bin */\n#ifndef UT_DATA_H\n#define UT_DATA_H\n")
        for i, s in enumerate(SECTIONS):
            f.write("#define SEC_%s %d\n" % (s, i))
        for n, v in defines:
            f.write("#define %s %d\n" % (n, v))
        f.write("#define NUM_SPRITES %d\n#define NUM_ROOMS %d\n#define NUM_FONTS %d\n" % (len(spr_entries), len(packed), len(fonts)))
        f.write("#endif\n")
    sizes = dict(zip(SECTIONS, map(len, sections)))
    print("data.bin: %d bytes (%s); %d tiles, %d frames, %d rooms, %d blocks (%d bytes unpacked)" % (
        len(blob), ", ".join("%s %d" % kv for kv in sizes.items()), len(tiles.blobs), len(frames), len(packed),
        len(blocks.raw), sum(map(len, blocks.raw))))


if __name__ == "__main__":
    main()
