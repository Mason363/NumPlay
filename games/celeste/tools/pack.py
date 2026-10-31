#!/usr/bin/env python3
"""Celeste's content (maps, art, animations, text) -> src/data.bin + src/data.h.

    CELESTE=<the game's Content folder> python3 tools/pack.py

data.bin layout: a header of section offsets (see SECTIONS), then the
sections. Art the game draws all the time (Madeline, her hair, the HUD font)
is stored "direct" (row-RLE the engine draws straight from flash); everything
else is in LZMA packs that the engine decodes into its texture cache when a
room needs them. See src/res.c for the reader."""
import collections
import glob
import lzma
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
from cel import CELESTE, Atlas, Element, read_map  # noqa: E402
import tiles as T  # noqa: E402
import entities as E  # noqa: E402
import pictures  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_BIN = os.path.join(HERE, "..", "src", "data.bin")
OUT_H = os.path.join(HERE, "..", "src", "data.h")
TORCH_BITS = 448   # Session.torches (src/level.h)
PIC_HEADER = []

MAP_AREA = {"0-Intro": 0, "1-ForsakenCity": 1, "1H-ForsakenCity": 1, "1X-ForsakenCity": 1, "2-OldSite": 2,
            "2H-OldSite": 2, "2X-OldSite": 2, "3-CelestialResort": 3, "3H-CelestialResort": 3,
            "3X-CelestialResort": 3, "4-GoldenRidge": 4, "4H-GoldenRidge": 4, "4X-GoldenRidge": 4,
            "5-MirrorTemple": 5, "5H-MirrorTemple": 5, "5X-MirrorTemple": 5, "6-Reflection": 6,
            "6H-Reflection": 6, "6X-Reflection": 6, "7-Summit": 7, "7H-Summit": 7, "7X-Summit": 7,
            "8-Epilogue": 8, "9-Core": 9, "9H-Core": 9, "9X-Core": 9, "LostLevels": 10}
ALL_MAPS = ["0-Intro", "1-ForsakenCity", "1H-ForsakenCity", "1X-ForsakenCity", "2-OldSite", "2H-OldSite",
            "2X-OldSite", "3-CelestialResort", "3H-CelestialResort", "3X-CelestialResort", "4-GoldenRidge",
            "4H-GoldenRidge", "4X-GoldenRidge", "5-MirrorTemple", "5H-MirrorTemple", "5X-MirrorTemple",
            "6-Reflection", "6H-Reflection", "6X-Reflection", "7-Summit", "7H-Summit", "7X-Summit",
            "8-Epilogue", "9-Core", "9H-Core", "9X-Core"]
MAPS = os.environ.get("MAPS", ",".join(ALL_MAPS)).split(",")   # MAPS=0-Intro,1-ForsakenCity: a few, for tests

PACK_RAW = 24 * 1024      # raw bytes per texture pack
# Madeline's and Badeline's animations only cutscenes play: packed, not straight in flash
CUTSCENE_ANIMS = {"tentacle_grab", "tentacle_grabbed", "tentacle_pull", "tentacle_dangling", "sleep", "asleep",
                  "halfWakeUp", "wakeUp", "carryTheoWalk", "carryTheoCollapse", "sitDown", "fallPose", "bagdown",
                  "bigFallRecover", "roll", "rollGetUp", "downed", "faint", "fainted", "hug", "spawn", "angry",
                  "laugh", "pretendDead"}
LAZY_ANIMS = []
DICT = 9600               # LZMA dictionary: the engine decodes through a ring buffer this big (its strip)
MARGIN = 2                # tiles kept around each room for the autotiler

# ------------------------------------------------------------------ writers

class W:
    def __init__(self):
        self.b = bytearray()

    def u8(self, v):
        self.b += struct.pack("<B", v & 0xFF)

    def s8(self, v):
        self.b += struct.pack("<b", v)

    def u16(self, v):
        assert 0 <= v < 65536, v
        self.b += struct.pack("<H", v)

    def s16(self, v):
        assert -32768 <= v < 32768, v
        self.b += struct.pack("<h", v)

    def u32(self, v):
        self.b += struct.pack("<I", v)

    def s32(self, v):
        self.b += struct.pack("<i", v)

    def f32(self, v):
        self.b += struct.pack("<f", v)

    def raw(self, b):
        self.b += b

    def align(self, n=4):
        while len(self.b) % n:
            self.b.append(0)

    def __len__(self):
        return len(self.b)


# CELESTE_LZMA_ENCODER_PICK_V1: choose the smallest aligned LZMA1 RAW stream.
# The game decoder still uses exactly dict=9600, lc=lp=pb=0; generation-only CPU.
def lz(data):
    src = bytes(data)
    base = {"id": lzma.FILTER_LZMA1, "preset": 9 | lzma.PRESET_EXTREME,
            "lc": 0, "lp": 0, "pb": 0, "dict_size": DICT}
    best = lzma.compress(src, format=lzma.FORMAT_RAW, filters=[base])
    best_aligned = (len(best) + 3) & ~3
    trials = (
        {"preset": 9},
        {"preset": 8 | lzma.PRESET_EXTREME},
        {"preset": 7 | lzma.PRESET_EXTREME},
        {"nice_len": 64},
        {"nice_len": 128},
        {"nice_len": 192},
        {"mf": lzma.MF_BT3},
        {"mf": lzma.MF_HC4},
        {"depth": 16},
        {"depth": 32},
    )
    for option in trials:
        params = dict(base)
        params.update(option)
        encoded = lzma.compress(src, format=lzma.FORMAT_RAW, filters=[params])
        aligned = (len(encoded) + 3) & ~3
        if aligned < best_aligned:
            best, best_aligned = encoded, aligned
    return best


class Strings:
    def __init__(self):
        self.ids = {}
        self.list = []

    def __call__(self, s):
        if s not in self.ids:
            self.ids[s] = len(self.list)
            self.list.append(s)
        return self.ids[s]


STR = Strings()
STR("")

# ------------------------------------------------------------------ textures

def to565(img):
    r = img[..., 0].astype(np.uint32)
    g = img[..., 1].astype(np.uint32)
    b = img[..., 2].astype(np.uint32)
    return (r >> 3) << 11 | (g >> 2) << 5 | (b >> 3)


class Tex:
    """A texture the game uses: its pixels as 8-bit indices into a palette.
    Index 0 is transparent. kind: 0 packed RLE, 1 direct RLE, 2 packed raw (tilesets)."""

    def __init__(self, tid, path, sub):
        self.id = tid
        self.path = path
        self.sub = sub
        self.kind = 0
        self.pal = None
        self.data = None
        self.pack = -1
        self.offset = 0
        self.scale = 1


class Textures:
    def __init__(self):
        self.atlases = [Atlas("Gameplay"), Atlas("Misc")]
        self.list = []
        self.by_path = {}

    def find(self, path):
        for a in self.atlases:
            if path in a:
                return a[path]
        return None

    def __call__(self, path, kind=0):
        """Id of a texture (added on first use), or None if the game has no such texture."""
        if path in self.by_path:
            t = self.list[self.by_path[path]]
            if kind == 1:
                t.kind = 1
            return t.id
        sub = self.find(path)
        if sub is None:
            return None
        t = Tex(len(self.list), path, sub)
        t.kind = kind
        self.list.append(t)
        self.by_path[path] = t.id
        return t.id

    def family(self, prefix, kind=0):
        """Ids of prefix + number textures, in number order (an animation's frames)."""
        out = []
        for a in self.atlases:
            for s in a.prefix(prefix):
                out.append(self(s.path, kind))
            if out:
                break
        if not out and self.find(prefix):
            out.append(self(prefix, kind))
        return out


TEX = Textures()


class Palettes:
    """Palettes of RGB565 colors + alpha, 256 entries max, shared when they fit."""

    def __init__(self):
        self.list = []  # list of lists of (c565, a)

    def assign(self, colors):
        """colors: set of (c565, a). Returns (palette id, {color: index})."""
        for i, p in enumerate(self.list):
            have = set(p)
            if colors <= have:
                return i, {c: p.index(c) + 1 for c in colors}
        for i, p in enumerate(self.list):
            have = set(p)
            if len(have | colors) <= 255 and i >= len(self.list) - 1:
                for c in sorted(colors - have):
                    p.append(c)
                return i, {c: p.index(c) + 1 for c in colors}
        p = sorted(colors)
        self.list.append(p)
        return len(self.list) - 1, {c: p.index(c) + 1 for c in colors}


PALS = Palettes()


GRADIENTS = {"purplesunset": 4, "mist": 4, "darkswamp": 4, "vignette": 4, "northernlights": 2}


def quantize(t):
    """Texture pixels -> (palette id, indices uint8 array h x w). Premultiplied alpha:
    fully transparent pixels become index 0. Tile sheets get their own palette
    (15 colors at most: they are cached at 4 bits a pixel); smooth gradients are
    stored smaller and drawn scaled up."""
    img = t.sub.trimmed()
    if t.scale > 1 and not isinstance(t.sub, HudSub):
        k = t.scale
        h, w = img.shape[0] // k * k, img.shape[1] // k * k
        img = img[:h, :w].astype(np.float32).reshape(h // k, k, w // k, k, 4).mean(axis=(1, 3)).round().astype(np.uint8)
    if len(np.unique(to565(img) | (img[..., 3].astype(np.uint32) >> 4) << 16)) > 255:
        # gradients: fewest colors that keep them smooth on a 16-bit screen
        from PIL import Image
        q = Image.fromarray(img, "RGBA").quantize(colors=255, method=Image.Quantize.FASTOCTREE, dither=Image.Dither.NONE)
        img = np.array(q.convert("RGBA"))
        print("quantized", t.path)
    a = img[..., 3].astype(np.uint32)
    # un-premultiply for the 565 color, keep alpha in 0..255 (quantized to 16 levels)
    rgb = img[..., :3].astype(np.float32)
    keyed = (a >> 4)  # 0..15
    c = to565(img).astype(np.uint32)
    key = np.where(keyed == 0, 0xFFFFFFFF, c | (keyed << 16))
    uniq = [int(k) for k in np.unique(key) if k != 0xFFFFFFFF]
    colors = set((k & 0xFFFF, k >> 16) for k in uniq)
    if t.kind == 2:
        assert len(colors) <= 15, t.path
        p = sorted(colors)
        PALS.list.append(p)
        pid, idx = len(PALS.list) - 1, {c: p.index(c) + 1 for c in colors}
    else:
        pid, idx = PALS.assign(colors)
    lut = {k: idx[(k & 0xFFFF, k >> 16)] for k in uniq}
    lut[0xFFFFFFFF] = 0
    u, inv = np.unique(key, return_inverse=True)
    out = np.array([lut[int(v)] for v in u], np.uint8)[inv].reshape(key.shape)
    return pid, out


def rle(idx):
    """Row RLE: per row, ops until the next row's offset.
    0x00-0x3F skip n+1, 0x40-0x7F repeat n+1 of the next byte, 0x80-0xFF n+1 literals."""
    h, w = idx.shape
    rows = []
    body = bytearray()
    for y in range(h):
        rows.append(len(body))
        row = idx[y]
        # trailing transparent pixels need nothing
        end = w
        while end > 0 and row[end - 1] == 0:
            end -= 1
        x = 0
        while x < end:
            v = row[x]
            n = 1
            while x + n < end and row[x + n] == v:
                n += 1
            if v == 0:
                while n > 0:
                    k = min(n, 64)
                    body.append(k - 1)
                    n -= k
                    x += k
                continue
            if n >= 3:
                while n >= 3:
                    k = min(n, 64)
                    body += bytes((0x40 + k - 1, v))
                    n -= k
                    x += k
                continue
            # literals until a transparent pixel or a run of 3
            s = x
            while x < end and x - s < 128:
                v = row[x]
                n = 1
                while x + n < end and row[x + n] == v:
                    n += 1
                if v == 0 or n >= 3:
                    break
                x += min(n, 128 - (x - s))
            body.append(0x80 + (x - s) - 1)
            body += bytes(row[s:x].tolist())
    rows.append(len(body))
    hdr = bytearray()
    for r in rows:
        hdr += struct.pack("<H", r + 2 * (h + 1))
    return bytes(hdr + body)


def rle_direct(idx):
    """A texture drawn straight from flash, as small as it goes: (format flags, bytes)."""
    flags, colors, body = rle_direct_parts(idx)
    return flags, colors + (b"\0" if colors and not flags & 4 and len(colors) & 1 else b"") + body


def rle_direct_parts(idx):
    """(format flags, local palette, pixels). With 15 colors or fewer (flag 8), a local palette (count,
    then the colors' indices, in the order the pixels first have them: textures alike but for their
    colors get the same pixels) and the literals two pixels a byte (low nibble first), runs' values
    local too; rows whose ops all take fewer than ROW8_MAX bytes (flag 4) get a byte each (their lengths)
    instead of u16 offsets (kept even, from the table's start)."""
    flags, colors, rows = rle_direct_rows(idx)
    out = bytearray()
    if flags & 4:
        out += bytes(len(r) for r in rows)
    else:
        off = 2 * (len(rows) + 1)
        for r in rows:
            out += struct.pack("<H", off)
            off += len(r)
        out += struct.pack("<H", off)
    for r in rows:
        out += r
    return flags, colors, bytes(out)


ROW8_MAX = 240   # a flag-4 row's length byte from ROW8_MAX up: the row is another's (direct_rows)


def direct_rows(rows, at, pool):
    """A flag-4 texture's rows written at `at` in DIRECT, the rows met before only pointing at them: length byte
    ROW8_MAX + (offset >> 16), then the row's length and the offset's low 16 bits (the engine: gfx.c row_ops)."""
    lens, ops = bytearray(), bytearray()
    base = at + len(rows)
    for r in rows:
        o = pool.get(r)
        if o is not None:
            assert o >> 16 < 256 - ROW8_MAX
            lens.append(ROW8_MAX + (o >> 16))
            ops += bytes((len(r), o & 255, o >> 8 & 255))
        else:
            if len(r) > 3:
                pool[r] = base + len(ops)
            lens.append(len(r))
            ops += r
    return bytes(lens + ops)


# CELESTE_DIRECT_OPTIMAL_ROW_RLE_V1: shortest existing RLE instructions, no decoder changes.
def _celeste_optimal_direct_row(row, nib):
    end = len(row)
    while end and row[end - 1] == 0:
        end -= 1
    dp = [10**9] * (end + 1)
    choice = [None] * end
    dp[end] = 0
    for i in range(end - 1, -1, -1):
        v = int(row[i])
        best = (10**9, None)
        if v == 0:
            n = 0
            while n < 64 and i + n < end and row[i + n] == 0:
                n += 1
                q = 1 + dp[i + n]
                if q < best[0]:
                    best = (q, (0, n))
        else:
            n = 0
            while n < 64 and i + n < end and row[i + n] == v:
                n += 1
                q = 2 + dp[i + n]
                if q < best[0]:
                    best = (q, (1, n))
            n = 0
            while n < 128 and i + n < end and row[i + n] != 0:
                n += 1
                q = 1 + ((n + 1) // 2 if nib else n) + dp[i + n]
                if q < best[0]:
                    best = (q, (2, n))
        dp[i], choice[i] = best
    out = bytearray()
    i = 0
    while i < end:
        kind, n = choice[i]
        if kind == 0:
            out.append(n - 1)
        elif kind == 1:
            out += bytes((0x40 + n - 1, int(row[i])))
        else:
            out.append(0x80 + n - 1)
            if nib:
                for j in range(0, n, 2):
                    out.append(int(row[i + j]) | (int(row[i + j + 1]) << 4 if j + 1 < n else 0))
            else:
                out += bytes(row[i:i + n])
        i += n
    return bytes(out)

def rle_direct_rows(idx):
    """rle_direct_parts' (format flags, local palette, ops of each row)."""
    flat = idx.ravel()
    _, first = np.unique(flat, return_index=True)
    uniq = [int(flat[i]) for i in sorted(first) if flat[i] != 0]
    nib = len(uniq) <= 15
    if nib:
        lut = np.zeros(256, np.uint8)
        for i, v in enumerate(uniq):
            lut[v] = i + 1
        idx = lut[idx]
    h, w = idx.shape
    rows = []
    for y in range(h):
        body = bytearray()
        row = idx[y]
        end = w
        while end > 0 and row[end - 1] == 0:
            end -= 1
        x = 0
        while x < end:
            v = row[x]
            n = 1
            while x + n < end and row[x + n] == v:
                n += 1
            if v == 0:
                while n > 0:
                    k = min(n, 64)
                    body.append(k - 1)
                    n -= k
                    x += k
                continue
            if n >= 3:
                while n >= 3:
                    k = min(n, 64)
                    body += bytes((0x40 + k - 1, int(v)))
                    n -= k
                    x += k
                continue
            st = x
            while x < end and x - st < 128:
                v = row[x]
                n = 1
                while x + n < end and row[x + n] == v:
                    n += 1
                if v == 0 or n >= 3:
                    break
                x += min(n, 128 - (x - st))
            body.append(0x80 + (x - st) - 1)
            lits = row[st:x].tolist()
            if nib:
                for i in range(0, len(lits), 2):
                    body.append(lits[i] | ((lits[i + 1] if i + 1 < len(lits) else 0) << 4))
            else:
                body += bytes(lits)
        rows.append(bytes(body))
    flags = 0
    colors = b""
    if nib:
        flags |= 8
        colors = bytes([len(uniq)] + uniq)
    if all(len(r) < ROW8_MAX for r in rows):
        rows = [_celeste_optimal_direct_row(idx[y], nib) for y in range(h)]
        flags |= 4
    return flags, colors, rows


# ------------------------------------------------------------------ maps

def grid_from(text, w, h):
    g = [["0"] * w for _ in range(h)]
    if text:
        for y, row in enumerate(text.replace("\r", "").split("\n")[:h]):
            for x, c in enumerate(row[:w]):
                g[y][x] = c
    return g


class Room:
    pass


class Chapter:
    def __init__(self, name):
        self.name = name
        self.package, self.root = read_map(os.path.join(CELESTE, "Maps", name + ".bin"))
        self.rooms = []
        for l in self.root.child("levels").children:
            r = Room()
            r.e = l
            r.name = l["name"]
            if r.name.startswith("lvl_"):
                r.name = r.name[4:]
            r.x, r.y, r.w, r.h = l["x"], l["y"], l["width"], l["height"]
            self.rooms.append(r)
        self.fillers = [(f["x"], f["y"], f["w"], f["h"]) for f in self.root.child("Filler").children] \
            if self.root.child("Filler") else []
        self.build_grids()

    def build_grids(self):
        """The map's tile grids and their tiles, as Celeste's LevelLoader builds them."""
        import re as _re
        x0 = min(r.x for r in self.rooms)
        y0 = min(r.y for r in self.rooms)
        x1 = max(r.x + r.w for r in self.rooms)
        y1 = max(r.y + r.h for r in self.rooms)
        for fx, fy, fw, fh in self.fillers:   # fillers are in tiles
            x0, y0 = min(x0, fx * 8), min(y0, fy * 8)
            x1, y1 = max(x1, (fx + fw) * 8), max(y1, (fy + fh) * 8)
        pad = 64
        bx, by, bw, bh = x0 - pad, y0 - pad, x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
        tb = (bx // 8, by // 8, -(-bw // 8), -(-bh // 8))
        self.tb = tb
        TX, TY, TW, TH = tb
        bg = T.VMap(TW, TH, "0")
        fg = T.VMap(TW, TH, "0")
        inl = T.VMap(TW, TH, False)
        level_bounds = []
        split = _re.compile("\\r\\n|\\n\\r|\\n|\\r")
        for r in self.rooms:
            left, top = r.x // 8, r.y // 8
            tw, th = -(-r.w // 8), -(-r.h // 8)
            for name, dst in (("bg", bg), ("solids", fg)):
                text = r.e.child(name).get("innerText", "") if r.e.child(name) else ""
                rows = split.split(text)
                for i, row in enumerate(rows):
                    for j, c in enumerate(row):
                        dst[left + j - TX, top + i - TY] = c
            for xx in range(left, left + tw):
                for yy in range(top, top + th):
                    inl[xx - TX, yy - TY] = True
            level_bounds.append((left - TX, top - TY, tw, th))
        for fx, fy, fw, fh in self.fillers:
            for m in range(fx, fx + fw):
                for n in range(fy, fy + fh):
                    c = "0"
                    if fy - TY > 0:
                        c2 = fg[m - TX, fy - TY - 1]
                        if c2 != "0":
                            c = c2
                    if c == "0" and fx - TX > 0:
                        c3 = fg[fx - TX - 1, n - TY]
                        if c3 != "0":
                            c = c3
                    if c == "0" and fx + fw - TX < TW - 1:
                        c4 = fg[fx + fw - TX, n - TY]
                        if c4 != "0":
                            c = c4
                    if c == "0" and fy + fh - TY < TH - 1:
                        c5 = fg[m - TX, fy + fh - TY]
                        if c5 != "0":
                            c = c5
                    if c == "0":
                        c = "1"
                    fg[m - TX, n - TY] = c
                    inl[m - TX, n - TY] = True

        def extend(vm, r):
            left, top = r.x // 8, r.y // 8
            right, bottom = left + -(-r.w // 8), top + -(-r.h // 8)
            for xx in range(left, right):
                v = vm[xx - TX, top - TY]
                k = 1
                while k < 4 and not inl[xx - TX, top - TY - k]:
                    vm[xx - TX, top - TY - k] = v
                    k += 1
                v = vm[xx - TX, bottom - 1 - TY]
                k = 1
                while k < 4 and not inl[xx - TX, bottom - 1 - TY + k]:
                    vm[xx - TX, bottom - 1 - TY + k] = v
                    k += 1
            for yy in range(top - 4, bottom + 4):
                v = vm[left - TX, yy - TY]
                k = 1
                while k < 4 and not inl[left - TX - k, yy - TY]:
                    vm[left - TX - k, yy - TY] = v
                    k += 1
                v = vm[right - 1 - TX, yy - TY]
                k = 1
                while k < 4 and not inl[right - 1 - TX + k, yy - TY]:
                    vm[right - 1 - TX + k, yy - TY] = v
                    k += 1
        for r in self.rooms:
            extend(bg, r)
        for r in self.rooms:
            left, top = r.x // 8, r.y // 8
            right, bottom = left + -(-r.w // 8), top + -(-r.h // 8)
            for xx in range(left, right):
                if fg[xx - TX, top - TY] == "0":
                    for k in range(1, 8):
                        inl[xx - TX, top - TY - k] = True
                if fg[xx - TX, bottom - 1 - TY] == "0":
                    for k in range(1, 8):
                        inl[xx - TX, bottom - 1 - TY + k] = True
        for r in self.rooms:
            extend(fg, r)
        self.bgmap, self.fgmap = bg, fg
        self.fg = [bytearray(ord(c) for c in row) for row in fg.data]
        self.bg = [bytearray(ord(c) for c in row) for row in bg.data]
        self.level_bounds = level_bounds

    def generate(self, fgsets, bgsets, anim_frames):
        """Celeste's Autotiler over the whole map, seeded like LevelLoader (area name)."""
        area = MAP_AREA[self.name]
        seed = sum(ord(c) for c in "area_%d" % area)
        rnd = T.NetRandom(seed)
        self.bgt = T.generate(bgsets, self.bgmap, T.Behaviour(True, False, False), self.level_bounds, rnd, anim_frames)
        self.fgt = T.generate(fgsets, self.fgmap, T.Behaviour(True, False, True), self.level_bounds, rnd, anim_frames)

    def cell(self, grid, x, y):
        """GetTile with EdgesExtend: map-wide tile coordinates."""
        x0, y0, W_, H_ = self.tb
        gx = min(max(x - x0, 0), W_ - 1)
        gy = min(max(y - y0, 0), H_ - 1)
        return chr(grid[gy][gx])


TERRAIN_INDEX = {}   # (layer, char) -> combined terrain index (the TILESETS order)


def room_cells_rle(ch, r, layer):
    """A room's final tiles (the game's autotiler, exact) with MARGIN cells around, as row RLE of
    (count u8, cell u16): 0 air, 63 solid without a tile, else terrain + 1 | quad << 6, bit 14: overlay."""
    tiles = ch.fgt if layer == 0 else ch.bgt
    vm = ch.fgmap if layer == 0 else ch.bgmap
    TX, TY, TW, TH = ch.tb
    tx, ty, tw, th = r.x // 8 - MARGIN, r.y // 8 - MARGIN, r.w // 8 + 2 * MARGIN, r.h // 8 + 2 * MARGIN
    rows = []
    body = bytearray()
    ovs = []
    for y in range(th):
        rows.append(len(body))
        line = []
        for x in range(tw):
            gx, gy = tx + x - TX, ty + y - TY
            c = vm[gx, gy]
            if is_air(c):
                line.append(0)
                continue
            t = tiles.get((gx, gy))
            if not t or t[0] is None:
                line.append(63)
                continue
            (qx, qy), ov = t[0], t[1]
            v = (TERRAIN_INDEX[(layer, c)] + 1) | (qy * 8 + qx) << 6
            if ov:
                v |= 1 << 14
                ovs.append((x, y, ov))
            line.append(v)
        x = 0
        while x < tw:
            v = line[x]
            n = 1
            while x + n < tw and line[x + n] == v and n < 255:
                n += 1
            body += struct.pack("<BH", n, v)
            x += n
    rows.append(len(body))
    return rows, bytes(body), ovs


def is_air(c):
    return c == "0" or c == "\0"


# ------------------------------------------------------------------ the room blob

def attr_kind(v):
    if isinstance(v, bool):
        return "b"
    if isinstance(v, int):
        return "i"
    if isinstance(v, float):
        return "f"
    return "s"


class Schema:
    """Per entity/trigger name: its attributes, each stored in a fixed slot."""

    def __init__(self):
        self.attrs = collections.OrderedDict()   # name -> OrderedDict(attr -> kind)
        self.ints = collections.defaultdict(lambda: (0, 0))

    def see(self, e):
        a = self.attrs.setdefault(e.name, collections.OrderedDict())
        for k, v in e.attr.items():
            if k in ("id", "x", "y", "originX", "originY"):
                continue
            kind = attr_kind(v)
            old = a.get(k)
            if old is None:
                a[k] = kind
            elif old != kind:
                if {old, kind} == {"i", "f"}:
                    a[k] = "f"
                elif "s" in (old, kind):
                    a[k] = "s"
                else:
                    a[k] = "f"
            if kind == "i":
                lo, hi = self.ints[(e.name, k)]
                self.ints[(e.name, k)] = (min(lo, v), max(hi, v))

    def layout(self, name):
        """[(attr, kind, offset, size)], record size."""
        out = []
        off = 0
        for k, kind in self.attrs.get(name, {}).items():
            if kind == "b":
                size = 1
            elif kind == "s":
                size = 2
            elif kind == "i":
                lo, hi = self.ints[(name, k)]
                size = 2 if -32768 <= lo and hi < 32768 else 4
            else:
                size = 4
            if size > 1 and off % 2:
                off += 1
            if size == 4 and off % 4 == 2:
                off += 2
            out.append((k, kind if not (kind == "i" and size == 4) else "I", off, size))
            off += size
        if off % 2:
            off += 1
        return out, off


SCHEMA = Schema()


def write_attrs(w, e):
    lay, size = SCHEMA.layout(e.name)
    rec = bytearray(size)
    for k, kind, off, sz in lay:
        v = E.attr(e, k)
        if kind == "b":
            struct.pack_into("<B", rec, off, 1 if v else 0)
        elif kind == "i":
            struct.pack_into("<h", rec, off, int(v or 0))
        elif kind == "I":
            struct.pack_into("<i", rec, off, int(v or 0))
        elif kind == "f":
            struct.pack_into("<f", rec, off, float(v or 0))
        else:
            struct.pack_into("<H", rec, off, STR(str(v) if v is not None else ""))
    w.raw(bytes(rec))


ENT_IDS = {}
TRIG_IDS = {}
ANIM_NAMES = ["grass_top_a", "dead_top_a"]
DECAL_ANIMS = {}


def ent_type(name, table):
    if name not in table:
        table[name] = len(table)
    return table[name]


def decal_name(texture):
    """Decal.Name without "decals/", lower case (the switch in Decal.Added): no extension, no trailing digits."""
    t = texture.replace("\\", "/")
    if "." in t.rsplit("/", 1)[-1]:
        t = t[:t.rfind(".")]
    return re.sub(r"\d+$", "", t).lower()


# Decal.Added's looks (src/tiles.c, by the decal's first texture, in DECALANIM): 1-3 Banners (MakeBanner: grass; rags and
# curtains; cliffside flowers, only if windy), 4-5 smoke (CreateSmoke: a chimney in the bg particles; a vent in the fg),
# 6 MakeParallax(0.1) (the Summit's clouds), 7 CoreSwapImage
DECAL_KINDS = {n: 1 for n in ("generic/grass_a", "generic/grass_b", "generic/grass_c", "generic/grass_d")}
DECAL_KINDS.update({n: 2 for n in ("1-forsakencity/rags", "1-forsakencity/ragsb", "3-resort/curtain_side_a",
                                   "3-resort/curtain_side_d")})
DECAL_KINDS.update({"4-cliffside/flower_" + c: 3 for c in "abcd"})
DECAL_KINDS.update({"0-prologue/house": 4, "3-resort/vent": 5})
DECAL_KINDS.update({"7-summit/cloud_" + c: 6 for c in ("a", "b", "bb", "bc", "bd", "c", "cb", "cc", "cd", "ce", "d", "db", "dc",
                                                         "dd", "e", "f", "g", "h", "j", "i")})
DECAL_KINDS.update({n: 7 for n in ("9-core/ball_a", "9-core/ball_a_ice", "9-core/rock_e", "9-core/rock_e_ice")})
DECAL_CORE_SWAP = {"9-core/ball_a": "9-core/ball_a_ice", "9-core/ball_a_ice": "9-core/ball_a",
                   "9-core/rock_e": "9-core/rock_e_ice", "9-core/rock_e_ice": "9-core/rock_e"}
DECAL_NOFLIP = {"9-core/heart_bevel_a", "9-core/heart_bevel_b", "9-core/heart_bevel_c", "9-core/heart_bevel_d"}   # scale = 1
# MakeMirror's: Depth 9500, behind the other bg decals (their reflections of Madeline are not drawn); the first 20 also
# get scale.Y = 1
DECAL_MIRROR_NOFLIPY = {"5-temple/bg_mirror_" + n for n in ["a", "b"] + ["shard_" + c for c in "abcdefghijk"] +
                        ["shard_group_" + g for g in ("a", "a_b", "a_c", "b", "c", "d", "e")]}
DECAL_MIRROR = DECAL_MIRROR_NOFLIPY | {"5-temple/bg_mirror_c", "5-temple/statue_d", "6-reflection/crystal_reflection"}


def decal_scaled(ids, k):
    """The frames of a decal drawn k times bigger about its middle (the Summit's clouds: scale *= 1.15), as textures of
    their own, the nearest pixels (point sampling)."""
    out = []
    for tid in ids:
        name = TEX.list[tid].path + "@%g" % k
        if name not in TEX.by_path:
            img = TEX.list[tid].sub.image()
            fh, fw = img.shape[:2]
            W, H = int(round(fw * k)), int(round(fh * k))
            xs = np.clip(np.floor((np.arange(W) + 0.5 - W / 2) / k + fw / 2).astype(int), 0, fw - 1)
            ys = np.clip(np.floor((np.arange(H) + 0.5 - H / 2) / k + fh / 2).astype(int), 0, fh - 1)
            big = img[ys][:, xs]
            yy, xx = np.nonzero(big[..., 3])
            y0, y1, x0, x1 = yy.min(), yy.max() + 1, xx.min(), xx.max() + 1
            t = Tex(len(TEX.list), name, HudSub(name, big[y0:y1, x0:x1], -int(x0), -int(y0), W, H))
            TEX.list.append(t)
            TEX.by_path[name] = t.id
        out.append(TEX.by_path[name])
    return out


# Decal.Added: the decals that are solid (MakeSolid: Position + (x, y), w x h, safe), by name: rects, and whether a
# fg one blocks waterfalls (a bg one never does: Depth 9000)
def _roofedge(sx):
    return [(-8 if sx >= 0 else 0, -4, 8, 8)]


DECAL_SOLIDS = dict([(n, lambda sx: [(-8, -4, 16, 8)]) for n in ("roofcenter", "roofcenter_b", "roofcenter_c", "roofcenter_d")] +
                    [(n, _roofedge) for n in ("roofedge", "roofedge_b", "roofedge_c", "roofedge_d")] +
                    [("bridgecolumntop", lambda sx: [(-8, -8, 16, 8), (-5, 0, 10, 8)]),
                     ("bridgecolumn", lambda sx: [(-5, -8, 10, 16)]),
                     ("brokenelevator", lambda sx: [(-16, -20, 32, 48)])])
DECAL_SOLIDS = {"3-resort/" + k: v for k, v in DECAL_SOLIDS.items()}
DECAL_SOLIDS["4-cliffside/bridge_a"] = lambda sx: [(-24, 0, 48, 8)]


def decal_solids(level):
    """The room's solid decals as "decalSolid" entities (x, y: top left; width, height; blockWaterfalls). Pieces side by
    side, or one on top of the other, of the same kind make one solid: the same to collide with, fewer entities."""
    rects = []
    for layer in ("fgdecals", "bgdecals"):
        el = level.child(layer)
        for d in (el.children if el else []):
            f = DECAL_SOLIDS.get(decal_name(d["texture"]))
            if f:
                blocks = layer == "fgdecals" or decal_name(d["texture"]) != "4-cliffside/bridge_a"   # (Depth != 9000)
                for x, y, w, h in f(d.get("scaleX", 1)):
                    rects.append([d["x"] + x, d["y"] + y, w, h, blocks])
    for horizontal in (True, False):
        merged = True
        while merged:
            merged = False
            for a in rects:
                for b in rects:
                    if a is b or a[4] != b[4]:
                        continue
                    if horizontal and a[1] == b[1] and a[3] == b[3] and a[0] + a[2] == b[0]:
                        a[2] += b[2]
                    elif not horizontal and a[0] == b[0] and a[2] == b[2] and a[1] + a[3] == b[1]:
                        a[3] += b[3]
                    else:
                        continue
                    rects.remove(b)
                    merged = True
                    break
                if merged:
                    break
    out = []
    for x, y, w, h, fg in rects:
        assert x == int(x) and y == int(y), (x, y)
        e = Element("decalSolid")
        e.attr.update({"x": int(x), "y": int(y), "width": int(w), "height": int(h), "blockWaterfalls": fg})
        out.append(e)
    return out


def decal_tex(path):
    p = "decals/" + path.replace("\\", "/")
    if p.endswith(".png"):
        p = p[:-4]
    # Decal(): its Name has no trailing digits, and GetAtlasSubtextures(Name) its frames ("clock00" plays all of them);
    # not Reflection's hidden game screens (secret_*: 6 to 50 frames each), for room
    ids = (not p.startswith("decals/6-reflection/secret_") and TEX.family(re.sub(r"\d+$", "", p))) or TEX.family(p)
    if not ids:
        print("missing decal", p)
    return ids


# AreaData: inventories (PlayerInventory), each mode's, and the checkpoints (level, inventory, flags:
# 1 dreaming, 2 core mode cold, 4 color grade feelingdown, 8 flag badeline_connection)
INVENTORIES = ["Prologue", "Default", "OldSite", "CH6End", "TheSummit", "Core", "Farewell"]
MODE_INVENTORY = {"0-Intro": "Prologue", "2-OldSite": "OldSite", "7-Summit": "TheSummit", "7H-Summit": "TheSummit",
                  "7X-Summit": "TheSummit", "8-Epilogue": "TheSummit", "9-Core": "Core", "9H-Core": "Core",
                  "9X-Core": "Core", "LostLevels": "Farewell"}
for _m in ALL_MAPS:
    MODE_INVENTORY.setdefault(_m, "Default")
_C = lambda *lv: [(x, None, 0) for x in lv]
CHECKPOINTS = {
    "1-ForsakenCity": _C("6", "9b"), "1H-ForsakenCity": _C("04", "08"),
    "2-OldSite": [("3", "Default", 1), ("end_3", None, 0)], "2H-OldSite": [("03", None, 1), ("08b", None, 1)],
    "3-CelestialResort": _C("08-a", "09-d", "00-d"), "3H-CelestialResort": _C("06", "11", "16"),
    "4-GoldenRidge": _C("b-00", "c-00", "d-00"), "4H-GoldenRidge": _C("b-00", "c-00", "d-00"),
    "5-MirrorTemple": [("b-00", None, 0), ("c-00", None, 1), ("d-00", None, 1), ("e-00", None, 1)],
    "5H-MirrorTemple": _C("b-00", "c-00", "d-00"),
    "6-Reflection": _C("00", "04", "b-00", "boss-00") + [("after-00", "CH6End", 8)],
    "6H-Reflection": _C("b-00", "c-00", "d-00"),
    "7-Summit": _C("b-00", "c-00", "d-00", "e-00b", "f-00", "g-00"),
    "7H-Summit": _C("b-00", "c-01", "d-00", "e-00", "f-00", "g-00"),
    "9-Core": [("a-00", None, 0), ("c-00", None, 2), ("d-00", None, 0)], "9H-Core": _C("a-00", "b-00", "c-01"),
    "LostLevels": _C("a-00", "c-00", "e-00z", "f-door", "h-00b") + [("i-00", None, 4), ("j-00", None, 4), ("j-16", None, 0)],
}


def room_tiles(ch, grid, r):
    """A room's tile types with MARGIN cells around, as stored and as in RAM (level.c room_load builds it).
    Stored: u8 ntypes, u8 bits, the type chars, then the rows; a row is bytes (type index << bits | count - 1)
    up to the room's width. In RAM, after the type chars (align 2): u16 row offsets[th] into the row data,
    then the rows, each different row once (a row the same as one before is that one's)."""
    tx, ty, tw, th = r.x // 8 - MARGIN, r.y // 8 - MARGIN, r.w // 8 + 2 * MARGIN, r.h // 8 + 2 * MARGIN
    lines = [[ch.cell(grid, tx + i, ty + y) for i in range(tw)] for y in range(th)]
    types = sorted(set(c for line in lines for c in line))
    assert len(types) <= 16, (ch.name, r.name, types)
    bits = 6 if len(types) <= 4 else 5 if len(types) <= 8 else 4
    head = bytes([len(types), bits]) + bytes(ord(c) for c in types)
    stored = bytearray(head)
    data = bytearray()
    seen = {}
    rows = []
    for line in lines:
        out = bytearray()
        x = 0
        while x < tw:
            c = line[x]
            n = 1
            while x + n < tw and line[x + n] == c and n < (1 << bits):
                n += 1
            out.append(types.index(c) << bits | (n - 1))
            x += n
        out = bytes(out)
        stored += out
        if out not in seen:
            seen[out] = len(data)
            data += out
        rows.append(seen[out])
    w = W()
    w.raw(head)
    w.align(2)
    for o in rows:
        w.u16(o)
    w.raw(bytes(data))
    w.align(2)
    return bytes(stored), bytes(w.b)


SPIN_X0 = 128   # cell x bias: spinners can be outside the room


def room_spinners(ch, r):
    """Crystal and dust spinners not attached to solids, by tile row (some are outside the room):
    u16 n, s16 first row, u16 rows, u16 first[rows + 1], then per spinner
    u16 cell x + SPIN_X0 | x % 8 << 10 | y % 8 << 13 (by row, then x), then a nibble per spinner:
    which of its quarters are inside solid tiles (CreateSprites' SolidCheck)."""
    ents = r.e.child("entities").children if r.e.child("entities") else []
    recs = []
    for e in ents:
        if e.name != "spinner" or e.get("attachToSolid", False):
            continue
        x, y = int(e["x"]), int(e["y"])
        cx, cy = x // 8 + SPIN_X0, y // 8
        assert 0 <= cx < 1024, (r.name, x, y)
        mask = 0
        for bit, (dx, dy) in enumerate(((-4, -4), (4, -4), (4, 4), (-4, 4))):
            if ch.cell(ch.fg, (r.x + x + dx) // 8, (r.y + y + dy) // 8) != "0":
                mask |= 1 << bit
        recs.append((cy, cx, x % 8, y % 8, mask))
    recs.sort()
    row0 = recs[0][0] if recs else 0
    rows = recs[-1][0] - row0 + 1 if recs else 0
    w = W()
    w.u16(len(recs))
    w.s16(row0)
    w.u16(rows)
    i = 0
    for row in range(rows + 1):
        while i < len(recs) and recs[i][0] - row0 < row:
            i += 1
        w.u16(i)
    for cy, cx, dx, dy, mask in recs:
        w.u16(cx | dx << 10 | dy << 13)
    for k in range(0, len(recs), 2):
        w.u8(recs[k][4] | (recs[k + 1][4] << 4 if k + 1 < len(recs) else 0))
    if dust_room(ch, r):
        # DustStaticSpinner: its DustGraphic's nodes (AddDustNodesIfInCamera, autoExpandDust), from the tiles:
        # per spinner u16: enabled (4 bits), expanded in x (4), in y (4), nodes up-left, up-right, down-left, down-right
        pos = [(r.x + (cx - SPIN_X0) * 8 + dx, r.y + cy * 8 + dy) for cy, cx, dx, dy, _ in recs]

        def solid_rect(x, y, w_, h_):
            for gy in range(y // 8, (y + h_ - 1) // 8 + 1):
                for gx in range(x // 8, (x + w_ - 1) // 8 + 1):
                    if ch.cell(ch.fg, gx, gy) != "0":
                        return True
            return False

        def dust_rect(me, x, y, w_, h_):   # another DustStaticSpinner's Circle(6) or Hitbox(16, 4, -8, -3)
            for k, (sx, sy) in enumerate(pos):
                if k == me:
                    continue
                nx, ny = min(max(sx, x), x + w_), min(max(sy, y), y + h_)
                if (sx - nx) ** 2 + (sy - ny) ** 2 < 36:
                    return True
                if x < sx + 8 and x + w_ > sx - 8 and y < sy + 1 and y + h_ > sy - 3:
                    return True
            return False

        for k, (x, y) in enumerate(pos):
            v = 0
            for bit, (nx, ny) in enumerate(((-1, -1), (1, -1), (-1, 1), (1, 1))):
                if not solid_rect(x - 8 if nx < 0 else x, y - 8 if ny < 0 else y, 8, 8):
                    v |= 1 << bit
                rx, ry = x - 4 + nx * 16, y - 4 + ny * 4
                if solid_rect(rx, ry, 8, 8) or dust_rect(k, rx, ry, 8, 8):
                    v |= 1 << (4 + bit)
                rx, ry = x - 4 + nx * 4, y - 4 + ny * 16
                if solid_rect(rx, ry, 8, 8) or dust_rect(k, rx, ry, 8, 8):
                    v |= 1 << (8 + bit)
            w.u16(v)
    return bytes(w.b), len(recs)


def dust_room(ch, r):
    """Level.LoadLevel: spinners are dust in the Celestial Resort and the Summit's d- rooms."""
    area = MAP_AREA.get(ch.name, 0)
    return area == 3 or (area == 7 and r.name.startswith("d-"))


DUST_TEX = []     # DustGraphic's textures (dust.c samples them as 4-bit pixels)
JUMPTHRU = ["wood", "wood", "wood", "wood", "cliffside", "temple", "reflection", "temple", "wood", "core", "wood"]
UI_PREFIXES = ("MENU_", "OPTIONS_", "KEY_CONFIG_", "OVERWORLD_", "AREA_", "AREACOMPLETE_", "CHECKPOINT_", "FILE_", "UI_",
               "POEM_")
ROOM_SIZES = {}   # (chapter, room) -> bytes resident in RAM
BLOB_PARTS = collections.Counter()
BLOB_BYTES = {}
ROOM_POOL_FULL = 19092



# CELESTE_DECAL_ZIGZAG_XY_V1: same-length, reversible decal x/y preconditioner.
# Every room uses this transform; level.c undoes it once after room loading.
def _cel_decals_zigzag_room(blob):
    out = bytearray(blob)
    for part in (3, 4):  # RB_DFG, RB_DBG
        start = struct.unpack_from('<H', blob, 2 * part)[0]
        n = struct.unpack_from('<H', blob, start)[0]
        nt = blob[start + 2]
        xat = start + 4 + 2 * nt
        for at in (xat, xat + 2 * n):
            previous = 0
            for i in range(n):
                value = struct.unpack_from('<H', blob, at + i * 2)[0]
                delta = (value - previous) & 65535
                signed = delta if delta < 32768 else delta - 65536
                z = ((signed << 1) ^ (signed >> 15)) & 65535
                struct.pack_into('<H', out, at + i * 2, z)
                previous = value
    return bytes(out)


def room_blob(ch, r):
    """Everything a room needs; the first part (up to RB_ENTS) stays in RAM while the room is loaded
    (level.c room_load), the entities and triggers are read once."""
    w = W()
    hdr_at = len(w)
    for _ in range(12):   # the parts' offsets (RB_FG .. RB_END), then RB_BG_RAM, RB_SHIFT
        w.u16(0)
    offs = []
    offs.append(len(w))
    fg, fg_ram = room_tiles(ch, ch.fg, r)
    w.raw(fg)
    offs.append(len(w))
    bg, bg_ram = room_tiles(ch, ch.bg, r)
    w.raw(bg)
    w.align(2)
    # in RAM the tiles take more (row offsets) or less (rows once): what follows them moves by `shift`
    bg_at = offs[0] + len(fg_ram)
    shift = (bg_at + len(bg_ram) - len(w) + 3) & ~3
    # extras: objtiles (tiles entities take), scenery tiles over fg and bg: u16 n, then (u16 x, y, tile)
    offs.append(len(w))

    def csv_cells(name):
        out = []
        el = r.e.child(name)
        if el is not None and el.get("innerText"):
            for y, row in enumerate(el.get("innerText").replace("\r", "").split("\n")):
                for x, v in enumerate(row.split(",")):
                    v = v.strip()
                    if v and v != "-1":
                        out.append((x, y, int(v)))
        return out
    for lst in (csv_cells("objtiles"), csv_cells("fgtiles"), csv_cells("bgtiles")):
        w.u16(len(lst))
        for x, y, v in lst:
            w.u16(x)
            w.u16(y)
            w.u16(v)
    # decals: u16 n, u8 textures, u16 ids[textures], (align 2), then in columns: s16 x[n], s16 y[n],
    # u8 texture index | flip x << 6 | flip y << 7 [n]; in the map's order (it is the drawing order)
    for layer in ("fgdecals", "bgdecals"):
        offs.append(len(w))
        ds = r.e.child(layer).children if r.e.child(layer) else []
        recs = []
        texs = []
        for d in ds:
            ids = decal_tex(d["texture"])
            if not ids:
                continue
            name = decal_name(d["texture"])
            kind = DECAL_KINDS.get(name, 0)
            if kind == 6:
                ids = decal_scaled(ids, 1.15)
            if len(ids) > 1 or kind:
                DECAL_ANIMS[ids[0]] = len(ids) | kind << 8
            if ids[0] not in texs:
                texs.append(ids[0])
            fl = (1 << 6 if d.get("scaleX", 1) < 0 else 0) | (1 << 7 if d.get("scaleY", 1) < 0 else 0)
            if name in DECAL_NOFLIP:
                fl = 0
            if name in DECAL_MIRROR_NOFLIPY:
                fl &= ~(1 << 7)
            assert not (name in DECAL_MIRROR and layer == "fgdecals"), (r.name, name)
            recs.append((texs.index(ids[0]) | fl, int(round(d["x"])), int(round(d["y"])), name in DECAL_MIRROR))
        recs.sort(key=lambda rec: not rec[3])   # (stable: the mirrors' Depth 9500 first, then the 9000s in order)
        recs = [rec[:3] for rec in recs]
        assert len(texs) <= 64, (ch.name, r.name, layer, len(texs))
        w.u16(len(recs))
        w.u8(len(texs))
        w.align(2)
        for t in texs:
            w.u16(t)
        for tid, x, y in recs:
            w.s16(x)
        for tid, x, y in recs:
            w.s16(y)
        for tid, x, y in recs:
            w.u8(tid)
        w.align(2)
    offs.append(len(w))
    spin, nspin = room_spinners(ch, r)
    w.raw(spin)
    w.align(2)
    # the textures loaded with the room: u16 n, ids
    offs.append(len(w))
    w.u16(len(r.need))
    for t in r.need:
        w.u16(t)
    w.align(4)
    # entities and triggers
    offs.append(len(w))
    resident = len(w) + ((nspin + 7) // 8 + 3 & ~3)   # and a bit per spinner, once destroyed
    ents = r.e.child("entities").children if r.e.child("entities") else []
    ents = [e for e in ents if E.keep(e.name) and not (e.name == "spinner" and not e.get("attachToSolid", False))]
    ents += decal_solids(r.e)   # (the decals come after the entities, as Level.LoadLevel adds them)
    w.u16(len(ents))
    for e in ents:
        write_entity(w, e, ent_type(e.name, ENT_IDS))
    offs.append(len(w))
    trigs = r.e.child("triggers").children if r.e.child("triggers") else []
    trigs = [e for e in trigs if E.keep_trigger(e.name)]
    w.u16(len(trigs))
    for e in trigs:
        write_entity(w, e, ent_type(e.name, TRIG_IDS))
    offs.append(len(w))
    assert len(w) < 65536
    for i, o in enumerate(offs):
        struct.pack_into("<H", w.b, hdr_at + 2 * i, o)
    struct.pack_into("<Hh", w.b, hdr_at + 2 * len(offs), bg_at, shift)
    assert resident + shift < 65536
    ROOM_SIZES[(ch.name, r.name)] = resident + shift
    for k, (a, b) in enumerate(zip(offs, offs[1:])):
        name = ("fg", "bg", "extras", "dfg", "dbg", "spin", "need", "ents", "trigs")[k]
        BLOB_PARTS[name] += b - a
        BLOB_BYTES.setdefault(name, bytearray()).extend(w.b[a:b])
    # CELESTE_DECAL_ZIGZAG_XY_V1: same raw room size and same offsets.
    return _cel_decals_zigzag_room(bytes(w.b))


def room_pool_size(chapters):
    """RAM for two rooms at once: the biggest pair of rooms that touch (a transition), or one room."""
    best = (0, "")
    for ch in chapters:
        rs = ch.rooms
        for i, a in enumerate(rs):
            sa = ROOM_SIZES[(ch.name, a.name)]
            if sa > best[0]:
                best = (sa, "%s:%s" % (ch.name, a.name))
            for b in rs[i + 1:]:
                if a.x <= b.x + b.w and b.x <= a.x + a.w and a.y <= b.y + b.h and b.y <= a.y + a.h:
                    sb = ROOM_SIZES[(ch.name, b.name)]
                    if sa + sb > best[0]:
                        best = (sa + sb, "%s:%s+%s" % (ch.name, a.name, b.name))
    return best


def write_entity(w, e, tid):
    nodes = [(n["x"], n["y"]) for n in e.children if n.name == "node"]
    w.u8(tid)
    w.u8(len(nodes))
    w.u16(e.get("id", 0) & 0xFFFF)
    w.s16(int(e.get("x", 0)))
    w.s16(int(e.get("y", 0)))
    write_attrs(w, e)
    for x, y in nodes:
        w.s16(int(x))
        w.s16(int(y))
    w.align(2)


# ------------------------------------------------------------------ sprite banks (Sprites.xml)

def parse_frames(spec, n):
    """Celeste's frame lists: "0-3,5,7*4" (* repeats); empty = all."""
    if spec is None or spec == "":
        return list(range(n))
    out = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        rep = 1
        if "*" in part:
            part, r = part.split("*")
            rep = int(r)
        if "-" in part:
            a, b = (int(v) for v in part.split("-"))
            rng = range(a, b + 1) if a <= b else range(a, b - 1, -1)
            out += [v for v in rng for _ in range(rep)]
        else:
            out += [int(part)] * rep
    return out


class Bank:
    def __init__(self, name):
        self.name = name
        self.path = ""
        self.origin = (0, 0, 0)   # mode (0 none, 1 origin px, 2 justify), x, y
        self.start = None
        self.anims = collections.OrderedDict()  # id -> dict(frames=[tex], delay, loop, goto=[(name, w)])
        self.meta = {}


SPRITE_XML = ET.parse(os.path.join(CELESTE, "Graphics", "Sprites.xml")).getroot()
SPRITE_EL = {c.tag: c for c in SPRITE_XML}
BANKS = collections.OrderedDict()


def _bank_add(b, el, path, fallback):
    for c in el:
        if c.tag == "Origin":
            b.origin = (1, float(c.get("x")), float(c.get("y")))
        elif c.tag == "Justify":
            b.origin = (2, float(c.get("x")), float(c.get("y")))
        elif c.tag == "Center":
            b.origin = (2, 0.5, 0.5)
        elif c.tag in ("Anim", "Loop"):
            p = c.get("path", "")
            fam = TEX.family(path + p)
            if not fam and fallback:
                fam = TEX.family(fallback + p)
            frames = parse_frames(c.get("frames"), len(fam))
            texs = [fam[i] for i in frames if i < len(fam)]
            if len(texs) != len(frames):
                print("bank", b.name, "anim", c.get("id"), "missing frames", path + p)
            goto = []
            if c.get("goto"):
                for g in c.get("goto").split(","):
                    if ":" in g:
                        n, wgt = g.split(":")
                        goto.append((n.strip(), int(wgt)))
                    else:
                        goto.append((g.strip(), 1))
            b.anims[c.get("id")] = dict(frames=texs, delay=float(c.get("delay", "0")), loop=c.tag == "Loop", goto=goto)
        elif c.tag == "Metadata":
            for f in c:
                if f.tag == "Frames":
                    b.meta[f.get("path")] = (f.get("hair"), f.get("carry"))


def bank(name):
    if name in BANKS:
        return BANKS[name]
    el = SPRITE_EL[name]
    b = Bank(name)
    b.path = el.get("path", "")
    if el.get("copy"):
        base = SPRITE_EL[el.get("copy")]
        _bank_add(b, base, b.path, base.get("path", ""))
        b.meta.update(bank(el.get("copy")).meta)
    _bank_add(b, el, b.path, SPRITE_EL[el.get("copy")].get("path", "") if el.get("copy") else None)
    b.start = el.get("start")
    BANKS[name] = b
    return b


def player_metadata():
    """Per texture of Madeline's banks: hair offset, hair frame, has hair, carry offset."""
    meta = {}
    for bname in ("player", "player_no_backpack", "player_badeline", "player_playback", "badeline"):
        if bname not in BANKS:
            continue
        b = BANKS[bname]
        for path, (hair, carry) in b.meta.items():
            for root in (b.path, "characters/player/"):
                fam = TEX.family(root + path)
                if fam:
                    break
            hairs = hair.split("|") if hair is not None else []
            carries = carry.split(",") if carry else []
            for i, tid in enumerate(fam):
                m = [0, 0, 0, True, 0]
                if i < len(hairs):
                    h = hairs[i].strip()
                    if h == "x":
                        m[3] = False
                    elif h:
                        if ":" in h:
                            h, fr = h.split(":")
                            m[2] = int(fr)
                        hx, hy = h.split(",")
                        m[0], m[1] = int(hx), int(hy)
                if i < len(carries):
                    m[4] = int(carries[i])
                meta[tid] = m
    return meta



# ------------------------------------------------------------------ the interface (1920x1080, shown at 1/6)

class HudSub:
    """A texture of the 1920x1080 interface (Gui and Portraits atlases) made screen sized:
    scaled by k, its colors cut down (shared by a family of frames), trimmed."""

    def __init__(self, path, img, fx, fy, fw, fh):
        self.path, self.img, self.fx, self.fy, self.fw, self.fh = path, img, fx, fy, fw, fh

    def trimmed(self):
        return self.img


def crystal_borders(col):
    """The crystal spinners' black border (the Border entity draws each sprite four times, a pixel up, down, left
    and right) as textures of their own, a pixel bigger all around: src/spinner.c draws each once."""
    out = []
    for fam in ("fg_", "bg_"):
        for tid in TEX.family("danger/crystal/" + fam + col):
            t = TEX.list[tid]
            name = t.path + "_border"
            if name not in TEX.by_path:
                img = t.sub.image()
                h, w = img.shape[:2]
                on = np.zeros((h + 2, w + 2), bool)
                a = img[..., 3] > 0
                on[1:h + 1, 0:w] |= a
                on[1:h + 1, 2:w + 2] |= a
                on[0:h, 1:w + 1] |= a
                on[2:h + 2, 1:w + 1] |= a
                full = np.zeros((h + 2, w + 2, 4), np.uint8)
                full[on, 3] = 255
                ys, xs = np.nonzero(on)
                y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
                b = Tex(len(TEX.list), name, HudSub(name, full[y0:y1, x0:x1], -int(x0), -int(y0), w + 2, h + 2))
                TEX.list.append(b)
                TEX.by_path[name] = b.id
            out.append(TEX.by_path[name])
    return out


def hud_family(atlas, paths, k, ncolors, div=1, hard=False, nib=False):
    """HUD textures for these atlas paths, scaled by k, with ncolors colors shared between them
    and 4 levels of transparency (hard: none); div > 1 stores them that many times smaller (drawn scaled up);
    nib: fewer colors if it takes that for each to be 15 colors or fewer (4 bits a pixel in the cache).
    Returns texture ids."""
    if nib:
        while ncolors > 4 and not hud_family_(atlas, paths, k, ncolors, div, hard, probe=True):
            ncolors -= 1
    return hud_family_(atlas, paths, k, ncolors, div, hard)


def hud_family_(atlas, paths, k, ncolors, div=1, hard=False, probe=False):
    from PIL import Image
    scaled = []
    for path in paths:
        sub = atlas[path]
        im = Image.fromarray(sub.image(), "RGBA")   # premultiplied: resampling it is right
        fw, fh = max(1, round(sub.fw * k / div)), max(1, round(sub.fh * k / div))
        a = np.array(im.resize((fw, fh), Image.LANCZOS)).astype(np.float32)
        scaled.append((path, a))
    # one palette for the family: un-premultiplied colors of the visible pixels
    lim = 128 if hard else 32
    vis = []
    for _, a in scaled:
        m = a[..., 3] >= lim
        rgb = a[..., :3][m] * 255 / a[..., 3][m][:, None]
        vis.append(np.clip(rgb, 0, 255).astype(np.uint8))
    allv = np.concatenate(vis) if vis else np.zeros((1, 3), np.uint8)
    pal_img = Image.fromarray(allv.reshape(1, -1, 3), "RGB").quantize(colors=ncolors, method=Image.Quantize.MEDIANCUT,
                                                                       dither=Image.Dither.NONE)
    ids = []
    for (path, a), v in zip(scaled, vis):
        m = a[..., 3] >= lim
        q = np.array(Image.fromarray(v.reshape(1, -1, 3), "RGB").quantize(palette=pal_img, dither=Image.Dither.NONE).convert("RGB")).reshape(-1, 3)
        alpha = np.full(m.sum(), 255.0) if hard else np.clip(np.round(a[..., 3][m] / 85) * 85, 85, 255)
        out = np.zeros(a.shape, np.uint8)
        out[m, :3] = (q.astype(np.float32) * alpha[:, None] / 255).round().astype(np.uint8)
        out[m, 3] = alpha.astype(np.uint8)
        if probe:   # its colors as quantize() keys them
            keys = np.unique((to565(out) | (out[..., 3].astype(np.uint32) >> 4) << 16)[out[..., 3] > 0])
            if len(keys) > 15:
                return False
            continue
        ys, xs = np.nonzero(out[..., 3])
        if len(ys):
            y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        else:
            y0 = x0 = 0
            y1 = x1 = 1
        sub = HudSub("@" + path, out[y0:y1, x0:x1], -int(x0) * div, -int(y0) * div, a.shape[1] * div, a.shape[0] * div)
        name = "@" + path
        if name in TEX.by_path:
            ids.append(TEX.by_path[name])
            continue
        t = Tex(len(TEX.list), name, sub)
        t.scale = div
        TEX.list.append(t)
        TEX.by_path[name] = t.id
        HUD_TEX.append(t.id)
        ids.append(t.id)
    return True if probe else ids


def hud_overlay():
    """HiresSnow's overlay (Overworld "overlay", added at White * 0.45 * 0.45 over the title's black) as an opaque
    picture of what it adds, a quarter of the screen's size (drawn 4 times bigger): the title's background."""
    from PIL import Image
    from cel import Atlas as _A
    sub = _A("Overworld")["overlay"]
    a = sub.image().astype(np.float32)[:sub.fh, :sub.fw, :3] * (0.45 * 0.45)   # premultiplied: rgb added as is
    div = 24   # 1920 x 1080 -> 80 x 45
    h, w = 1080 // div, 1920 // div
    src = np.zeros((1080, 1920, 3), np.float32)
    src[:min(1080, a.shape[0]), :min(1920, a.shape[1])] = a[:1080, :1920]
    small = src.reshape(h, div, w, div, 3).mean(axis=(1, 3))
    q = Image.fromarray(np.clip(small.round(), 0, 255).astype(np.uint8), "RGB").quantize(colors=15, method=Image.Quantize.MEDIANCUT,
                                                                                         dither=Image.Dither.NONE).convert("RGB")
    out = np.zeros((h, w, 4), np.uint8)
    out[..., :3] = np.array(q)
    out[..., 3] = 255
    path = "@overlay"
    t = Tex(len(TEX.list), path, HudSub(path, out, 0, 0, w * 4, h * 4))
    t.scale = 4
    TEX.list.append(t)
    TEX.by_path[path] = t.id
    HUD_TEX.append(t.id)


HUD_TEX = []          # interface textures, packed after the rooms' (in this order)
PORTRAIT_EL = {}


def portrait_bank(name):
    """A portrait (Portraits.xml) as a sprite bank, its frames at the size the textbox shows them:
    240 interface pixels for `size` pixels (160 by default), so 40 / size on screen."""
    if name in BANKS:
        return BANKS[name]
    from cel import Atlas as _A
    atlas = PORTRAIT_ATLAS[0]
    el = PORTRAIT_EL[name]
    src = PORTRAIT_EL[el.get("copy")] if el.get("copy") else el
    b = Bank(name)
    b.path = el.get("path", src.get("path", ""))
    k = 40 / float(src.get("size", 160))
    for c in list(src) + ([c for c in el] if el is not src else []):
        if c.tag == "Center":
            b.origin = (2, 0.5, 0.5)
        elif c.tag in ("Anim", "Loop"):
            fam = atlas.prefix(b.path + c.get("path", ""))
            paths = [s.path for s in fam]
            # the big frames (Badeline climbing out of her box) are kept at half that
            big = paths and atlas[paths[0]].fw >= 320
            ids = hud_family(atlas, paths, k, 15, 2 if big else 1, True) if paths else []
            frames = parse_frames(c.get("frames"), len(ids))
            goto = []
            if c.get("goto"):
                for g in c.get("goto").split(","):
                    n, _, wgt = g.partition(":")
                    goto.append((n.strip(), int(wgt) if wgt else 1))
            b.anims[c.get("id")] = dict(frames=[ids[i] for i in frames if i < len(ids)], delay=float(c.get("delay", "0")),
                                        loop=c.tag == "Loop", goto=goto)
    b.meta = {"textbox": el.get("textbox", src.get("textbox", "default")), "glitchy": el.get("glitchy", src.get("glitchy"))}
    BANKS[name] = b
    return b


def gui_bank(atlas, name, ncolors=16):
    """A sprite of SpritesGui.xml (the interface's) as bank "gui_NAME", its frames at 1/6."""
    key = "gui_" + name
    if key in BANKS:
        return BANKS[key]
    el = ET.parse(os.path.join(CELESTE, "Graphics", "SpritesGui.xml")).getroot().find(name)
    b = Bank(key)
    b.path = el.get("path", "")
    b.start = el.get("start")
    for c in el:
        if c.tag == "Center":
            b.origin = (2, 0.5, 0.5)
        elif c.tag == "Justify":
            b.origin = (2, float(c.get("x")), float(c.get("y")))
        elif c.tag in ("Anim", "Loop"):
            fam = atlas.prefix(b.path + c.get("path", ""))
            ids = hud_family(atlas, [s.path for s in fam], 1 / 6, ncolors) if fam else []
            frames = parse_frames(c.get("frames"), len(ids))
            goto = [(g.strip(), 1) for g in c.get("goto", "").split(",") if g.strip()]
            b.anims[c.get("id")] = dict(frames=[ids[i] for i in frames if i < len(ids)], delay=float(c.get("delay", "0")),
                                        loop=c.tag == "Loop", goto=goto)
    BANKS[key] = b
    return b


PORTRAIT_ATLAS = []
TEXTBOXES = ["default", "madeline", "theo", "badeline", "granny", "oshiro", "oshiro_overlay", "default_mini",
             "theo_mini", "badeline_mini", "madeline_ask", "theo_ask"]


def hud_assets():
    """Portraits, textboxes and interface pieces the text needs."""
    from cel import Atlas as _A
    PORTRAIT_ATLAS.append(_A("Portraits"))
    gui = _A("Gui")
    root = ET.parse(os.path.join(CELESTE, "Graphics", "Portraits.xml")).getroot()
    for c in root:
        PORTRAIT_EL[c.tag] = c
    for tb in TEXTBOXES:
        hud_family(PORTRAIT_ATLAS[0], ["textbox/" + tb], 1 / 6, 32, nib=True)
    hud_family(gui, ["textboxbutton"], 1 / 6, 16)
    # the story's pictures: Reflection's hug (CS06_BossEnd), Theo's selfies (Selfie), the breathing minigame
    for pic in ("hug1", "hug2", "hug-light2a", "hug-light2b", "hug-light2c", "selfie", "selfieFilter", "selfieGondola",
                "selfieCampfire"):
        hud_family(PORTRAIT_ATLAS[0], [pic], 1 / 6, 32, nib=True)
    for pic in ("finalbg", "final1", "final2", "final3", "final4", "final5"):   # the Epilogue's end (CS08_Ending)
        hud_family(PORTRAIT_ATLAS[0], [pic], 1 / 6, 32, div=2, nib=True)
    hud_family(gui, ["feather/" + n for n in ("border", "box", "feather0", "feather1", "feather2", "feather3",
                                              "feather_half0", "feather_half1", "particle", "slice")], 1 / 6, 15, nib=True)
    hud_family(gui, ["hover/idle", "hover/highlight"], 1 / 6, 16)
    # the crystal heart's poem (Poem): the big hearts, the poem's sides, the streaks
    for i in range(3):
        gui_bank(gui, "heartgem%d" % i)
    hud_family(gui, ["poemside"], 1 / 6, 16)
    hud_family(_A("Overworld"), ["snow"], 1 / 6, 4)
    hud_family(gui, ["collectables/cassette"], 1 / 6, 16)   # the cassette's message (Cassette.UnlockedBSide)
    # the menus (menu.c): the chapters' icons, their title banner, the main menu's icons, the logo, the stats' icons
    for icon in ("intro", "city", "oldsite", "resort", "cliffside", "temple", "reflection", "Summit", "core", "lock"):
        hud_family(gui, ["areas/" + icon], 1 / 6, 24)
    hud_family(gui, ["areaselect/title", "areaselect/accent"], 1 / 6, 8)
    for icon in ("start", "options", "credits", "exit"):
        hud_family(gui, ["menu/" + icon], 1 / 6, 16)
    hud_family(gui, ["logo"], 1 / 6, 24)   # the title screen's: the mountain on top of CELESTE (its letters: the reflection)
    hud_overlay()
    for icon in ("strawberry", "goldberry", "skullBlue"):
        hud_family(gui, ["collectables/" + icon], 1 / 6, 16)
    hud_family(gui, ["strawberryCountBG"], 1 / 6, 4)
    hud_family(gui, ["x"], 1 / 6, 4)
    hud_family(PORTRAIT_ATLAS[0], ["noise/%s" % s.path.split("/")[-1] for s in PORTRAIT_ATLAS[0].prefix("noise/")], 40 / 160, 8)
    for n in PORTRAIT_EL:
        if n.startswith("portrait_"):
            portrait_bank(n)


def portrait_command(content):
    """{portrait ...} (FancyText) as numbers: bank, begin, idle, talk animations (255: none), side,
    flags (1 upside down, 2 flipped, 4 pop, 8 glitchy), textbox, overlay textures (65535: none)."""
    words = content.split()
    if words and words[0] == "none":
        return "{portrait none}"
    side, flags, sprite, anim = 0, 0, None, None
    for w_ in words:
        if w_ == "upsidedown":
            flags |= 1
        elif w_ == "flip":
            flags |= 2
        elif w_ == "left":
            side = -1
        elif w_ == "right":
            side = 1
        elif w_ == "pop":
            flags |= 4
        elif sprite is None:
            sprite = w_
        else:
            anim = w_
    names = {n.lower(): n for n in BANKS if n.startswith("portrait_")}
    bname = names.get(("portrait_" + (sprite or "")).lower())
    if bname is None:
        return "{portrait 255,255,255,255,%d,%d,65535,65535}" % (side, flags)
    b = BANKS[bname]
    an = list(b.anims)
    pick = lambda pre: an.index(pre + (anim or "")) if pre + (anim or "") in an else 255
    if b.meta.get("glitchy") == "true":
        flags |= 8
    tb = b.meta.get("textbox") or "default"
    t1 = TEX.by_path.get("@textbox/" + tb, TEX.by_path["@textbox/default"])
    t2 = TEX.by_path.get("@textbox/" + tb + "_overlay", 65535)
    return "{portrait %d,%d,%d,%d,%d,%d,%d,%d}" % (list(BANKS).index(bname), pick("begin_"), pick("idle_"), pick("talk_"),
                                                  side, flags, t1, t2)


# ------------------------------------------------------------------ assets per room

def asset_prefetch(a):
    """What of an asset is loaded with its room: a sprite's first animation (the rest loads when drawn);
    "lazy:" before an asset: only its first texture."""
    if a.startswith("lazy:"):
        return asset_textures(a[5:])[:1]
    kind, _, what = a.partition(":")
    if kind == "bank":
        b = bank(what)
        an = b.start if b.start in b.anims else ("idle" if "idle" in b.anims else next(iter(b.anims), None))
        return list(b.anims[an]["frames"]) if an else []
    return asset_textures(a)


def asset_textures(a):
    if a.startswith("lazy:"):
        a = a[5:]
    kind, _, what = a.partition(":")
    if kind == "bank":
        b = bank(what)
        return [t for an in b.anims.values() for t in an["frames"]]
    if kind == "fam":
        return TEX.family(what)
    if kind == "tex":
        t = TEX(what)
        return [t] if t is not None else []
    raise ValueError(a)


def styleground_list(ch):
    """The chapter's stylegrounds in draw order, with 'apply' attributes folded in.
    Returns (backgrounds, foregrounds): lists of (name, attrs)."""
    out = []
    style = ch.root.child("Style")
    for layer in ("Backgrounds", "Foregrounds"):
        items = []
        el = style.child(layer) if style else None

        def walk(e, inherited):
            for c in e.children:
                a = dict(inherited)
                a.update(c.attr)
                if c.name == "apply":
                    walk(c, a)
                else:
                    items.append((c.name, a))
        if el:
            walk(el, {})
        out.append(items)
    return out


def room_matches(pattern, room):
    """Celeste's IsVisible room lists: comma separated, '*' wildcards, names without lvl_."""
    import fnmatch
    for p in pattern.split(","):
        p = p.strip()
        if not p:
            continue
        if fnmatch.fnmatchcase(room, p) or fnmatch.fnmatchcase("lvl_" + room, p):
            return True
    return False


# ------------------------------------------------------------------ main

SECTIONS = ["TEX", "PAL", "PACKS", "DIRECT", "BANKS", "PMETA", "TILESETS", "CHAPTERS", "STRINGS", "BLOBS", "ANIMTILES",
            "DECALANIM", "TEXNAMES", "FONTS", "DIALOG", "TEXDIM", "UISTR", "PICS"]


def fnv24(s, basis=2166136261):
    """FNV-1a of a name, xor-folded to 24 bits (src/res.c res_tex_by_name)."""
    h = basis
    for c in s.encode():
        h = ((h ^ c) * 16777619) & 0xFFFFFFFF
    return (h ^ (h >> 24)) & 0xFFFFFF



STYLE_EFFECTS = ["parallax", "snowFg", "snowBg", "windsnow", "stars", "reflectionfg", "godrays", "heatwave",
                 "corestarsfg", "mirrorfg", "bossStarField", "petals", "northernlights", "dreamstars", "planets",
                 "rain", "tentacles", "blackhole", "starfield"]

ROOM_ATTRS = ["dark", "space", "underwater", "whisper", "disableDownTransition"]
WIND = ["None", "Left", "Right", "LeftStrong", "RightStrong", "LeftOnOff", "RightOnOff", "LeftOnOffFast",
        "RightOnOffFast", "Alternating", "LeftGemsOnly", "RightCrazy", "Down", "Up", "Space"]


def tex_header(t, w):
    """w, h, ox, oy, fw, fh (in stored pixels: divided by scale), palette, format, scale,
    and the bytes the engine's cache needs for it."""
    s = t.sub
    k = t.scale
    h_, w_ = t.idx.shape
    w.u16(w_)
    w.u16(h_)
    w.s16(-s.fx // k)
    w.s16(-s.fy // k)
    w.u16(s.fw // k)
    w.u16(s.fh // k)
    w.u16(t.pal)
    fmt = 2 if t.kind == 2 else rle_direct(t.idx)[0] if t.kind == 1 else (8 if local_colors(t.idx) else 0)
    w.u8(fmt)
    w.u8(k)
    w.u32((w_ * h_ + 1) // 2 if fmt == 2 else len(rle(t.idx)))


def local_colors(idx):
    """A texture's colors when it has 15 or fewer (index 0 aside): the cache keeps it 4 bits a pixel (TF_NIB)."""
    uniq = sorted(set(np.unique(idx).tolist()) - {0})
    return uniq if 0 < len(uniq) <= 15 else None


def packed_body(t):
    """A packed texture's bytes after its header: its pixels, or with TF_NIB its colors (count, then
    their indices) and its pixels as indices into them."""
    uniq = local_colors(t.idx) if t.kind == 0 else None
    if not uniq:
        return t.idx.tobytes()
    lut = np.zeros(256, np.uint8)
    for i, v in enumerate(uniq):
        lut[v] = i + 1
    return bytes([len(uniq)] + uniq) + lut[t.idx].tobytes()


# CELESTE_PALETTE_PACK_SELECT_V1: improve LZMA packing without changing on-device code.
# Reorder local palette entries and indices together. Only whole-pack winners
# are kept; a pack's raw length and every texture offset remain unchanged.
def _cel_pal_reorder_pack(raw, entries, mode):
    source = bytes(raw)
    output = bytearray(source)
    changed = 0
    for at, expected_pixels in entries:
        if at < 0 or at + 20 > len(source):
            raise ValueError("texture record outside raw pack")
        if source[at+14] != 8:   # no local palette: normal packed or raw tile sheet
            continue
        w, h = struct.unpack_from("<HH", source, at)
        n = w * h
        if not w or not h or n != expected_pixels:
            raise ValueError("palette texture dimension mismatch")
        p = at+20
        k = source[p] if p < len(source) else 0
        if not 1 <= k <= 15 or p+1+k+n > len(source):
            raise ValueError("invalid packed local palette")
        oldpal = source[p+1:p+1+k]
        pixels = source[p+1+k:p+1+k+n]
        if len(set(oldpal)) != k or 0 in oldpal or max(pixels, default=0) > k:
            raise ValueError("invalid packed texture local indices")
        vals = list(oldpal)
        if mode == "palette_freq" or mode == "palette_freq_asc":
            cnt = collections.Counter(pixels)
            if mode == "palette_freq":
                vals.sort(key=lambda col: (-cnt[oldpal.index(col)+1], col))
            else:
                vals.sort(key=lambda col: (cnt[oldpal.index(col)+1], col))
        elif mode == "palette_reverse":
            vals.reverse()
        elif mode == "palette_first":
            order = list(dict.fromkeys(q for q in pixels if q))
            first = [oldpal[q-1] for q in order]
            vals = first + [c for c in oldpal if c not in first]
        else:
            raise ValueError("unknown palette method")
        if bytes(vals) == oldpal:
            continue
        tr = bytearray(range(256))
        lookup = {col: i+1 for i, col in enumerate(vals)}
        for i, col in enumerate(oldpal):
            tr[i+1] = lookup[col]
        newpix = pixels.translate(bytes(tr))
        assert bytes(([0]+vals)[q] for q in newpix) == bytes(([0]+list(oldpal))[q] for q in pixels)
        output[p+1:p+1+k] = bytes(vals)
        output[p+1+k:p+1+k+n] = newpix
        changed += 1
    if len(output) != len(source):
        raise AssertionError("palette change altered raw pack length")
    return bytes(output), changed


def _cel_pal_choose_pack(raw, entries, baseline=None):
    original = bytes(raw)
    if baseline is None:
        baseline = lz(original)   # the existing 11-option Celeste encoder picker
    best_z = baseline
    best_raw = original
    best_mode = "unchanged"
    best_count = 0
    aligned = (len(best_z)+3) & ~3
    for mode in ("palette_freq", "palette_first", "palette_reverse", "palette_freq_asc"):
        candidate, nchanged = _cel_pal_reorder_pack(original, entries, mode)
        if not nchanged:
            continue
        z1 = lzma.compress(candidate, format=lzma.FORMAT_RAW,
                           filters=[{"id": lzma.FILTER_LZMA1, "preset":9,
                                     "dict_size":DICT,"lc":0,"lp":0,"pb":0}])
        z2 = lzma.compress(candidate, format=lzma.FORMAT_RAW,
                           filters=[{"id": lzma.FILTER_LZMA1, "preset":9|lzma.PRESET_EXTREME,
                                     "dict_size":DICT,"lc":0,"lp":0,"pb":0}])
        candidate_z = z1 if len(z1) <= len(z2) else z2
        size = (len(candidate_z)+3) & ~3
        if size < aligned:
            best_z, best_raw, best_mode, best_count = candidate_z, candidate, mode, nchanged
            aligned = size
    return best_z, best_raw, best_mode, best_count


def _cel_pal_pack_stream(raw, ids):
    # TEX.list[].offset and .idx were populated when packs were constructed.
    entries = [(TEX.list[tid].offset, TEX.list[tid].idx.size) for tid in ids
               if TEX.list[tid].kind == 0]
    z, _, _, _ = _cel_pal_choose_pack(raw, entries)
    return z

def main():
    fgsets = T.read_tilesets("ForegroundTiles")
    bgsets = T.read_tilesets("BackgroundTiles")
    k = 0
    for layer, sets in ((0, fgsets), (1, bgsets)):
        for c in sets:
            if c != "z":
                TERRAIN_INDEX[(layer, c)] = k
                k += 1
    at = ET.parse(os.path.join(CELESTE, "Graphics", "AnimatedTiles.xml")).getroot()
    anim_frames = {sp.get("name"): len(TEX.family(sp.get("path"))) for sp in at.iter("sprite")}
    chapters = [Chapter(m) for m in MAPS]

    for m in ALL_MAPS:
        _, root = read_map(os.path.join(CELESTE, "Maps", m + ".bin"))
        for l in root.child("levels").children:
            for e in decal_solids(l):
                SCHEMA.see(e)
                ent_type(e.name, ENT_IDS)
            for k, table, keep in (("entities", ENT_IDS, E.keep), ("triggers", TRIG_IDS, E.keep_trigger)):
                if l.child(k):
                    for e in l.child(k).children:
                        SCHEMA.see(e)
                        if keep(e.name):
                            ent_type(e.name, table)
                        if keep(e.name):   # every texture the code can name has its id
                            for a in E.needs(e):
                                asset_textures(a)

    # every bank some entity uses, so the code always has its SB_/A_ names (textures only go in when a room needs them)
    for lst in list(E.ASSETS.values()) + list(E.NPC_ASSETS.values()):
        for a in lst:
            b = a[5:] if a.startswith("lazy:") else a
            if b.startswith("bank:") and b[5:] in SPRITE_EL:
                bank(b[5:])
    # banks every chapter needs; Madeline is drawn straight from flash, but for her cutscene poses, packed
    # (loaded when played: src/util.c spr_draw)
    keep = set()
    for b in ("player", "player_no_backpack", "badeline"):
        for name, an in bank(b).anims.items():
            if name not in CUTSCENE_ANIMS:
                keep.update(an["frames"])
    for t in keep:
        TEX.list[t].kind = 1
    LAZY_ANIMS[:] = [[t for t in an["frames"] if t not in keep]
                     for b in ("player", "player_no_backpack", "badeline")
                     for name, an in bank(b).anims.items() if name in CUTSCENE_ANIMS]
    lazy = {t for frames in LAZY_ANIMS for t in frames}
    for p in ("characters/player/hair00", "characters/player/bangs", "particles/smoke", "particles/zappysmoke",
              "particles/stars/", "particles/starfield/"):
        TEX.family(p, 1)
    for p in ("particles/blob", "particles/bubble", "particles/circle", "particles/cloud", "particles/confetti",
              "particles/feather", "particles/fire", "particles/petal", "particles/rect", "particles/shard",
              "particles/shatter", "particles/snow", "particles/triangle", "particles/triggerspike"):
        TEX(p, 1)
    for an in bank("player_sweat").anims.values():
        for t in an["frames"]:
            TEX.list[t].kind = 1
    hud_assets()
    pmeta = player_metadata()

    # tilesets: their images are packed raw
    for sets in (fgsets, bgsets):
        for t in sets.values():
            tid = TEX(t.path)
            if tid is not None:
                TEX.list[tid].kind = 2
    TEX("tilesets/scenery")
    DUST_TEX[:] = [TEX(p) for p in ("danger/dustcreature/base00", "danger/dustcreature/base01",
                                    "danger/dustcreature/base02", "danger/dustcreature/overlay00",
                                    "danger/dustcreature/overlay01", "danger/dustcreature/overlay02",
                                    "danger/dustcreature/center00", "danger/dustcreature/eyes00",
                                    "danger/dustcreature/eyes01", "danger/dustcreature/eyes02",
                                    "danger/dustcreature/templeeyes00", "danger/dustcreature/templeeyes01",
                                    "danger/dustcreature/templeeyes02", "danger/dustcreature/deadEyes")]
    for t in DUST_TEX:
        TEX.list[t].kind = 2

    # what each room needs
    for ch in chapters:
        ch.bgs, ch.fgs = styleground_list(ch)
        ch.style_tex = []
        for name, a in ch.bgs + ch.fgs:
            tid = TEX(a["texture"], 1) if name == "parallax" and "texture" in a else None
            if tid is not None and a["texture"] in GRADIENTS:
                TEX.list[tid].scale = GRADIENTS[a["texture"]]
            if name == "parallax" and tid is None:
                print("missing styleground texture", a.get("texture"))
            ch.style_tex.append(tid)
    mist = TEX.by_path.get("objects/heartdoor/mist")   # HeartGemDoor's: 320x320, most of Core's texture cache at full size
    if mist is not None:
        TEX.list[mist].scale = 2
    for ch in chapters:   # (once every styleground is known: one can be some room's entity's too)
        for r in ch.rooms:
            need = []
            for k in ("solids", "bg"):
                grid = r.e.child(k).get("innerText", "") if r.e.child(k) else ""
                sets = fgsets if k == "solids" else bgsets
                for c in sorted(set(grid) - set("0\r\n")):
                    if c in sets:
                        need.append(TEX(sets[c].path, 2))
            ents = r.e.child("entities").children if r.e.child("entities") else []
            trigs = r.e.child("triggers").children if r.e.child("triggers") else []
            uses = []
            for e in ents + [t for t in trigs if E.keep_trigger(t.name)]:
                if e.name == "jumpThru":   # its own look, or the area's (AreaData.Jumpthru): not every one
                    tx = E.attr(e, "texture")
                    tx = tx if tx != "default" else JUMPTHRU[MAP_AREA.get(ch.name, 0)]
                    t_ = TEX("objects/jumpthru/" + tx)
                    uses.append(t_)
                    need.append(t_)
                    continue
                for a in E.needs(e):
                    uses += asset_textures(a)
                    need += asset_prefetch(a)
                for k in ("tiletype", "tileType"):   # blocks' tiles (data.Char)
                    if (e.name, k) in E.CHARS:
                        c = str(E.attr(e, k))
                        if c in fgsets:
                            need.append(TEX(fgsets[c].path))
            for layer in ("fgdecals", "bgdecals"):
                for d in (r.e.child(layer).children if r.e.child(layer) else []):
                    ids = decal_tex(d["texture"])
                    need += decal_scaled(ids, 1.15) if DECAL_KINDS.get(decal_name(d["texture"])) == 6 else ids
                    other = DECAL_CORE_SWAP.get(decal_name(d["texture"]))
                    if other:
                        need += decal_tex(other)
            r.style_mask = 0
            for i, (name, a) in enumerate(ch.bgs + ch.fgs):
                vis = True
                if a.get("only") and not room_matches(a["only"], r.name):
                    vis = False
                if a.get("exclude") and room_matches(a["exclude"], r.name):
                    vis = False
                if vis:
                    r.style_mask |= 1 << i
                    if ch.style_tex[i] is not None:
                        need.append(ch.style_tex[i])
            dusty = dust_room(ch, r)
            if any(e.name in ("spinner", "rotateSpinner", "trackSpinner") for e in ents) and dusty:
                need += DUST_TEX   # src/dust.c draws the DustGraphics from 4-bit pixels
            elif any(e.name == "spinner" for e in ents):
                area = MAP_AREA[ch.name]
                for col in {5: ("red",), 6: ("purple",), 9: ("blue", "red")}.get(area, ("blue",)):
                    need += TEX.family("danger/crystal/fg_" + col) + TEX.family("danger/crystal/bg_" + col) + crystal_borders(col)
            if any(o for o in [r.e.child("objtiles")] if o is not None and o.get("innerText", "").strip(" \n-1,")):
                need.append(TEX("tilesets/scenery"))
            seen = set()
            seen |= lazy
            r.need = [t for t in need if t is not None and TEX.list[t].kind != 1 and not (t in seen or seen.add(t))]
            r.uses = r.need + [t for t in uses if t is not None and TEX.list[t].kind != 1 and not (t in seen or seen.add(t))]

    # pixels
    for t in TEX.list:
        t.pal, t.idx = quantize(t)

    # the texture cache a room needs (res.c: a 20-byte header, then RLE rows or 4-bit pixels)
    def cache_bytes(tid):
        t = TEX.list[tid]
        h_, w_ = t.idx.shape
        n = (w_ * h_ + 1) // 2 if t.kind == 2 else len(rle(t.idx)) + 2 * (h_ + 1)
        return (20 + n + 3) & ~3
    best = (0, "")
    for ch in chapters:
        sizes = [sum(cache_bytes(t) for t in r.need) for r in ch.rooms]
        for i, a in enumerate(ch.rooms):
            if sizes[i] > best[0]:
                best = (sizes[i], ch.name + ":" + a.name)
            for j in range(i + 1, len(ch.rooms)):
                b = ch.rooms[j]
                if a.x <= b.x + b.w and b.x <= a.x + a.w and a.y <= b.y + b.h and b.y <= a.y + a.h:
                    both = sum(cache_bytes(t) for t in set(a.need) | set(b.need))
                    if both > best[0]:
                        best = (both, "%s:%s+%s" % (ch.name, a.name, b.name))
    print("texture cache needed", best)
    if os.environ.get("CACHE_REPORT"):
        rows = []
        for ch in chapters:
            for r in ch.rooms:
                tot = sum(cache_bytes(t) for t in r.need)
                fams = collections.Counter()
                for t in r.need:
                    fams[TEX.list[t].path.split("/")[0] + "/" + (TEX.list[t].path.split("/")[1] if "/" in TEX.list[t].path else "")] += cache_bytes(t)
                rows.append((tot, ch.name, r.name, fams.most_common(4)))
        rows.sort()
        for x in rows[-15:]:
            print(x)
        print("rooms over 40K", sum(1 for x in rows if x[0] > 40000), "over 24K", sum(1 for x in rows if x[0] > 24000), "of", len(rows))

    # packs: each packed texture goes to the pack of the first room that needs it
    packs = []          # list of (raw bytes, [tex ids])
    cur = bytearray()
    cur_ids = []
    done = set()

    def flush():
        nonlocal cur, cur_ids
        if cur_ids:
            packs.append((bytes(cur), cur_ids))
        cur, cur_ids = bytearray(), []

    for ch in chapters:
        for r in ch.rooms:
            for tid in r.uses:
                if tid in done:
                    continue
                done.add(tid)
                t = TEX.list[tid]
                hw = W()
                tex_header(t, hw)
                body = packed_body(t)
                if len(cur) + len(hw) + len(body) > PACK_RAW and cur_ids:
                    flush()
                t.pack = len(packs)
                t.offset = len(cur)
                cur += hw.b + body
                cur_ids.append(tid)
        flush()
    if os.environ.get("HUD_REPORT"):
        fams = collections.Counter()
        for tid in HUD_TEX:
            t = TEX.list[tid]
            fams[re.sub(r"\d+$", "", t.path)] += t.idx.size
        print("hud bytes by family", fams.most_common(40), sum(fams.values()))
        groups = collections.defaultdict(bytearray)
        for tid in HUD_TEX:
            t = TEX.list[tid]
            groups["textbox" if "textbox" in t.path else "portraits"] += t.idx.tobytes()
        print("hud compressed", {k: len(lz(bytes(v))) for k, v in groups.items()})
    # the interface's textures, by family
    for tid in HUD_TEX:
        t = TEX.list[tid]
        if t.pack >= 0 or t.kind != 0:
            continue
        hw = W()
        tex_header(t, hw)
        body = packed_body(t)
        if len(cur) + len(hw) + len(body) > 2 * PACK_RAW and cur_ids:   # loaded a family at a time
            flush()
        t.pack = len(packs)
        t.offset = len(cur)
        cur += hw.b + body
        cur_ids.append(tid)
    flush()
    # Madeline's and Badeline's cutscene poses, an animation or a few to a pack
    for frames in LAZY_ANIMS:
        if len(cur) > PACK_RAW // 3:
            flush()
        for tid in frames:
            t = TEX.list[tid]
            if t.pack >= 0:
                continue
            hw = W()
            tex_header(t, hw)
            t.pack = len(packs)
            t.offset = len(cur)
            cur += hw.b + packed_body(t)
            cur_ids.append(tid)
    flush()

    if os.environ.get("DIRECT_REPORT"):   # bytes of direct textures by family, and those with more than 15 colors
        fam = collections.Counter()
        many = collections.Counter()
        for t in TEX.list:
            if t.kind == 1:
                f, b_ = rle_direct(t.idx)
                k = "/".join(t.path.split("/")[:2])
                fam[k] += len(b_) + 20
                if not f & 8:
                    many[k] += len(b_) + 20
        print("DIRECT", fam.most_common(14))
        print("DIRECT >15 colors", many.most_common(14))
    # direct textures (the same picture twice is stored once; the same pixels in other colors, once too:
    # TF_SHARED, then where they are)
    direct = W()
    same = {}
    bodies = {}
    pool = {}   # rows written, for the textures after them (direct_rows)
    for t in TEX.list:
        if t.kind == 1:
            hw = W()
            tex_header(t, hw)
            key = (bytes(hw.b), t.pal, t.idx.tobytes())
            if key in same:
                t.offset = same[key]
                continue
            direct.align(2)
            t.offset = len(direct)
            same[key] = t.offset
            flags, colors, body = rle_direct_parts(t.idx)
            if (flags, body) in bodies and len(body) > 8:
                hw.b[14] |= 16
                direct.raw(hw.b)
                direct.raw(colors)
                direct.u32(bodies[(flags, body)])
                continue
            direct.raw(hw.b)
            direct.raw(colors)
            if colors and not flags & 4 and len(colors) & 1:
                direct.u8(0)
            bodies[(flags, body)] = len(direct)
            direct.raw(direct_rows(rle_direct_rows(t.idx)[2], len(direct), pool) if flags & 4 else body)
    byfam = collections.Counter()
    for t in TEX.list:
        if t.kind == 1:
            byfam["/".join(t.path.split("/")[:2])] += len(rle(t.idx)) + 14
    print("direct by family", byfam.most_common())
    print("textures", len(TEX.list), "direct", sum(t.kind == 1 for t in TEX.list), "bytes", len(direct),
          "packs", len(packs), "raw", sum(len(p[0]) for p in packs))

    sec = {}
    blobs = W()

    # ---- TEX: u16 count, then u32 per texture: kind<<30 | pack<<18 | offset (packed) or offset (direct)
    w = W()
    w.u16(len(TEX.list))
    w.u16(0)
    for t in TEX.list:
        if t.kind == 1:
            w.u32(1 << 30 | t.offset)
        elif t.pack >= 0:
            assert t.offset < (1 << 18) and t.pack < (1 << 12)
            w.u32((t.kind & 3) << 30 | t.pack << 18 | t.offset)
        else:
            w.u32(0xFFFFFFFF)
    sec["TEX"] = w
    # ---- TEXDIM: u8 per texture, half the bigger side of its frame in 2 px (to cull before loading it), 255: more
    w = W()
    for t in TEX.list:
        w.u8(min(255, ((max(t.sub.fw, t.sub.fh) + 1) // 2 + 1) // 2))
    sec["TEXDIM"] = w

    # ---- PAL: u16 count, u32 offsets; each: u16 n (| 0x8000 if every color is opaque), u16 c565[n], u8 a[n]
    w = W()
    w.u16(len(PALS.list))
    w.u16(0)
    base = 4 + 4 * len(PALS.list)
    body = W()
    offs = []
    for p in PALS.list:
        body.align(2)
        offs.append(base + len(body))
        body.u16(len(p) | (0x8000 if all(a == 15 for c, a in p) else 0))
        for c, a in p:
            body.u16(c)
        for c, a in p:
            body.u8(a * 17)
    for o in offs:
        w.u32(o)
    w.raw(body.b)
    sec["PAL"] = w

    # ---- PACKS: u16 count; each: u32 blob offset, u32 compressed size, u32 raw size
    w = W()
    w.u16(len(packs))
    w.u16(0)
    comp_total = 0
    for raw, ids in packs:
        z = _cel_pal_pack_stream(raw, ids)
        comp_total += len(z)
        w.u32(len(blobs))
        w.u32(len(z))
        w.u32(len(raw))
        blobs.raw(z)
        blobs.align(4)
    sec["PACKS"] = w
    print("texture packs compressed", comp_total)

    sec["DIRECT"] = direct

    # ---- BANKS
    w = W()
    names = list(BANKS)
    w.u16(len(names))
    w.u16(0)
    base = 4 + 4 * len(names)
    body = W()
    offs = []
    for n in names:
        b = BANKS[n]
        body.align(4)
        offs.append(base + len(body))
        body.u8(b.origin[0])
        body.u8(0)
        anames = list(b.anims)
        body.u16(len(anames))
        body.f32(b.origin[1])
        body.f32(b.origin[2])
        body.u16(anames.index(b.start) if b.start in anames else 0xFFFF)
        body.u16(0)
        aoffs_at = len(body)
        for _ in anames:
            body.u16(0)
        body.align(4)
        for i, an in enumerate(anames):
            a = b.anims[an]
            body.align(4)
            struct.pack_into("<H", body.b, aoffs_at + 2 * i, len(body) - aoffs_at + 2 * len(anames) * 0)
            body.f32(a["delay"])
            body.u8(1 if a["loop"] else 0)
            body.u8(len(a["goto"]))
            body.u16(len(a["frames"]))
            for g, wgt in a["goto"]:
                body.u16(anames.index(g) if g in anames else 0xFFFF)
                body.u16(wgt)
            for f in a["frames"]:
                body.u16(f)
    for o in offs:
        w.u32(o)
    w.raw(body.b)
    sec["BANKS"] = w

    # ---- PMETA: per Madeline texture: u16 tex, s8 hair x, s8 hair y, u8 frame | hashair<<7, s8 carry (sorted by tex)
    w = W()
    w.u16(len(pmeta))
    w.u16(0)
    for tid in sorted(pmeta):
        hx, hy, fr, has, carry = pmeta[tid]
        w.u16(tid)
        w.s8(hx)
        w.s8(hy)
        w.u8(fr | (0x80 if has else 0))
        w.s8(carry)
    sec["PMETA"] = w

    # ---- TILESETS: two 128-byte maps char -> terrain index, then terrains
    w = W()
    terrains = []
    maps = []
    for sets in (fgsets, bgsets):
        m = bytearray([255] * 128)
        for c, t in sets.items():
            if c == "z":
                continue
            m[ord(c)] = len(terrains)
            terrains.append(t)
        maps.append(m)
    w.raw(maps[0])
    w.raw(maps[1])
    w.u16(len(terrains))
    w.u16(0)
    base = len(w) + 4 * len(terrains)
    body = W()
    offs = []

    def tile_list(lst):
        for tx, ty in lst:
            body.u8(ty * 8 + tx)

    anim_names = ANIM_NAMES
    for t in terrains:
        body.align(2)
        offs.append(base + len(body))
        body.u16(TEX(t.path))
        ign = "".join(sorted(t.ignores))
        body.u8(len(ign))
        for c in ign:
            body.u8(ord(c))
        body.u8(len(t.masks))
        body.u8(len(t.padded))
        body.u8(len(t.center))
        for m_, tl, sprites in t.masks:
            bits = care = 0
            order = [0, 1, 2, 3, 5, 6, 7, 8]
            for k, i in enumerate(order):
                if m_[i] != 2:
                    care |= 1 << (7 - k)
                    if m_[i] == 1:
                        bits |= 1 << (7 - k)
            body.u8(bits)
            body.u8(care)
            body.u8(len(tl))
            ov = 0
            if sprites:
                if sprites[0] not in anim_names:
                    anim_names.append(sprites[0])
                ov = anim_names.index(sprites[0]) + 1
            body.u8(ov)
            tile_list(tl)
        tile_list(t.padded)
        tile_list(t.center)
    for o in offs:
        w.u32(o)
    w.raw(body.b)
    sec["TILESETS"] = w

    # ---- ANIMTILES: animated tile overlays (AnimatedTiles.xml)
    w = W()
    at = ET.parse(os.path.join(CELESTE, "Graphics", "AnimatedTiles.xml")).getroot()
    defs = {s.get("name"): s for s in at.iter("sprite")}
    w.u16(len(anim_names))
    w.u16(0)
    for n in anim_names:
        s = defs[n]
        fam = TEX.family(s.get("path"))
        w.f32(float(s.get("delay")))
        w.s8(int(s.get("posX")))
        w.s8(int(s.get("posY")))
        w.s8(int(s.get("origX")))
        w.s8(int(s.get("origY")))
        w.u16(len(fam))
        for f in fam:
            w.u16(f)
    sec["ANIMTILES"] = w

    # ---- CHAPTERS
    room_packs = []
    w = W()
    w.u16(len(chapters))
    w.u16(0)
    base = 4 + 4 * len(chapters)
    body = W()
    offs = []
    rp_cur = bytearray()
    rp_ids = []
    room_locs = {}
    for ci, ch in enumerate(chapters):
        for ri, r in enumerate(ch.rooms):
            blob = room_blob(ch, r)
            if len(rp_cur) + len(blob) > 32 * 1024 and rp_cur:
                room_packs.append(bytes(rp_cur))
                rp_cur = bytearray()
            room_locs[(ci, ri)] = (len(room_packs), len(rp_cur), len(blob))
            rp_cur += blob
        if rp_cur:
            room_packs.append(bytes(rp_cur))
            rp_cur = bytearray()
    pack_base = len(packs)
    rcomp = 0
    room_pack_index = []
    for raw in room_packs:
        z = lz(raw)
        rcomp += len(z)
        room_pack_index.append((len(blobs), len(z), len(raw)))
        blobs.raw(z)
        blobs.align(4)
    # append room packs to the PACKS table
    pw = sec["PACKS"]
    struct.pack_into("<H", pw.b, 0, len(packs) + len(room_packs))
    for o, c, n in room_pack_index:
        pw.u32(o)
        pw.u32(c)
        pw.u32(n)
    print("rooms raw", sum(len(p) for p in room_packs), "compressed", rcomp, dict(BLOB_PARTS))
    if os.environ.get("PARTS_REPORT"):
        print("parts compressed", {k: len(lz(v)) for k, v in BLOB_BYTES.items()})

    global ROOM_POOL
    ROOM_POOL = room_pool_size(chapters)
    # the whole game needs this much (7-Summit g-00b + g-01): RAM is counted for it even with fewer maps
    if ROOM_POOL[0] < ROOM_POOL_FULL:
        ROOM_POOL = (ROOM_POOL_FULL, "the whole game")
    print("room pool", ROOM_POOL)
    for ci, ch in enumerate(chapters):
        body.align(4)
        offs.append(base + len(body))
        ch_start = len(body)
        body.u16(STR(ch.name))
        body.u16(len(ch.rooms))
        x0, y0, tw, th = ch.tb
        body.s16(x0)
        body.s16(y0)
        body.u16(tw)
        body.u16(th)
        styles = ch.bgs + ch.fgs
        body.u8(len(ch.bgs))
        body.u8(len(ch.fgs))
        # AreaData: the area, its mode, the inventory, checkpoints and strawberries (SaveData keeps them by index)
        mode = 2 if ch.name[1:2] == "X" or ch.name[2:3] == "X" else 1 if ch.name[1:2] == "H" or ch.name[2:3] == "H" else 0
        body.u8(MAP_AREA[ch.name])
        body.u8(mode)
        cps = CHECKPOINTS.get(ch.name, [])
        rnames = [r.name for r in ch.rooms]
        berries = []
        for ri, r in enumerate(ch.rooms):
            for e in (r.e.child("entities").children if r.e.child("entities") else []):
                if e.name in ("strawberry", "goldenBerry", "memorialTextController"):
                    golden = e.name != "strawberry"
                    fl = (1 if golden else 0) | (2 if e.get("moon", False) else 0) | \
                        (4 if e.get("winged", False) or e.name == "memorialTextController" else 0)
                    berries.append((golden, int(e.get("checkpointID", 0)), int(e.get("order", 0)), ri,
                                    e.get("id", 0) & 0xFFFF, fl))
        berries.sort()
        assert len(berries) <= 64, ch.name
        body.u8(INVENTORIES.index(MODE_INVENTORY[ch.name]))
        body.u8(len(cps))
        body.u8(len(berries))
        body.u8(sum(1 for b in berries if not b[0]))   # MapData.DetectedStrawberries
        extra_at = len(body)
        body.u16(0)
        body.u16(0)
        # the chapter files (src/chN.c) whose entities this map has: a bit each, N = 0..15
        files = 0
        for r in ch.rooms:
            for e in (r.e.child("entities").children if r.e.child("entities") else []) + \
                    (r.e.child("triggers").children if r.e.child("triggers") else []):
                if e.name in E.OWNER:
                    files |= 1 << E.OWNER[e.name]
        body.u16(files)
        body.u16(0)
        rooms_at = len(body)
        for r in ch.rooms:
            body.u32(0)
        sg_at = len(body)
        for _ in styles:
            body.u32(0)
        for i, (name, a) in enumerate(styles):
            body.align(4)
            struct.pack_into("<I", body.b, sg_at + 4 * i, len(body) - ch_start)
            body.u8(STYLE_EFFECTS.index(name) if name in STYLE_EFFECTS else 255)
            flags = 0
            for bit, k in enumerate(("loopx", "loopy", "flipx", "flipy", "instantIn", "instantOut", "fadeIn")):
                if a.get(k, k in ("loopx", "loopy")):
                    flags |= 1 << bit
            if a.get("blendmode", "") == "additive":
                flags |= 1 << 7
            body.u8(flags)
            tid = ch.style_tex[i]
            body.u16(tid if tid is not None else 0xFFFF)
            body.f32(float(a.get("x", 0)))
            body.f32(float(a.get("y", 0)))
            body.f32(float(a.get("scrollx", 1)))
            body.f32(float(a.get("scrolly", 1)))
            body.f32(float(a.get("speedx", 0)))
            body.f32(float(a.get("speedy", 0)))
            body.f32(float(a.get("alpha", 1)))
            body.u32(int(str(a.get("color", "ffffff")), 16) if a.get("color") else 0xFFFFFF)
            body.u16(STR(a.get("flag", "")))
            body.u16(STR(a.get("notflag", "")))
            body.u16(STR(a.get("tag", "")))
            body.u16(STR(a.get("fadex", "")))
            body.u16(STR(a.get("fadey", "")))
            body.u16(STR(a.get("only", "")))
        # Torch: the game keeps the ones lit as session flags; the port as bits, a room's from its first (in twos)
        torch_base, tb = [], 0
        for r in ch.rooms:
            torch_base.append(tb)
            ents = r.e.child("entities").children if r.e.child("entities") else []
            tb += (sum(1 for e in ents if e.name == "torch" and not E.attr(e, "startLit")) + 1) // 2
        assert tb <= TORCH_BITS // 2, ch.name
        for ri, r in enumerate(ch.rooms):
            body.align(4)
            struct.pack_into("<I", body.b, rooms_at + 4 * ri, len(body) - ch_start)
            pk, off, size = room_locs[(ci, ri)]
            body.u16(STR(r.name))
            body.u16(pack_base + pk)
            body.u32(off)
            body.u32(size)
            body.s32(r.x)
            body.s32(r.y)
            body.u16(r.w)
            body.u16(r.h)
            fl = 0
            for bit, k in enumerate(ROOM_ATTRS):
                if r.e.get(k):
                    fl |= 1 << bit
            body.u8(fl)
            body.u8(WIND.index(r.e.get("windPattern", "None")) if r.e.get("windPattern", "None") in WIND else 0)
            body.s8(int(r.e.get("cameraOffsetX", 0) or 0))
            body.s8(int(r.e.get("cameraOffsetY", 0) or 0))
            body.u32(r.style_mask & 0xFFFFFFFF)
            body.u32(r.style_mask >> 32)
            body.u16(STR(r.e.get("music", "") or ""))
            body.u8(int(r.e.get("enforceDashNumber", 0) or 0))
            body.u8(torch_base[ri])
        body.align(2)
        struct.pack_into("<H", body.b, extra_at, len(body) - ch_start)
        for golden, cp, order, ri, eid, fl in berries:
            body.u16(ri)
            body.u16(eid)
            body.u8(cp)
            body.u8(fl)
        struct.pack_into("<H", body.b, extra_at + 2, len(body) - ch_start)
        for level, inv, flags in cps:
            body.u16(rnames.index(level))
            body.u8(INVENTORIES.index(inv) if inv else 255)
            body.u8(flags)
    for o in offs:
        w.u32(o)
    w.raw(body.b)
    sec["CHAPTERS"] = w
    sec["BLOBS"] = blobs
    w = W()
    w.u16(len(DECAL_ANIMS))
    for t in sorted(DECAL_ANIMS):
        w.u16(t)
        w.u16(DECAL_ANIMS[t])
    sec["DECALANIM"] = w
    # names of the textures entities look up at run time: FNV-1a hashes folded to 24 bits (from the first offset
    # basis that tells them all apart), sorted; then their ids
    paths = [(t.path, t.id) for t in TEX.list if not t.path.startswith("@")
             if not t.path.startswith(("decals/", "bgs/", "characters/player", "characters/badeline"))]
    for basis in range(2166136261, 2166136261 + 1000):
        named = sorted((fnv24(p_, basis), i) for p_, i in paths)
        if len(set(h for h, _ in named)) == len(named):
            break
    else:
        raise SystemExit("no 24-bit hash tells the texture names apart")
    w = W()
    w.u16(len(named))
    w.u16(0)
    w.u32(basis)
    for h_, i in named:
        w.raw(struct.pack("<I", h_)[:3])
    w.align(2)
    for h_, i in named:
        w.u16(i)
    sec["TEXNAMES"] = w

    # ---- FONTS and DIALOG (textdata.py)
    import textdata
    dialog = textdata.read_dialog()
    for k_ in list(dialog):   # what nothing here shows: Farewell's (CH9_), its wavedash pages, the platforms', the journal
        if k_.startswith(("CH9_", "WAVEDASH", "POSTCARD", "ASSIST", "AUTOSAVING", "XB1", "BTN_", "JOURNAL", "STAT",
                          "PICO8", "SAVEFAILED", "LOADFAILED")):
            del dialog[k_]
    codes = set(range(32, 127))
    for v in dialog.values():
        codes.update(ord(c) for c in v)
    sec["FONTS"] = textdata.font_section(W, codes)
    # dialog: entries in LZMA packs of about 12 KB; table of key hashes (sorted) and places
    for k_ in dialog:
        dialog[k_] = re.sub(r"\{portrait ([^}]*)\}", lambda m: portrait_command(m.group(1)), dialog[k_])
    items = sorted((textdata.fnv(k), k) for k in dialog)
    assert len(set(h for h, _ in items)) == len(items)
    dpacks = []
    cur = bytearray()
    places = []
    for h_, k in items:
        b = dialog[k].encode("utf-8")
        if len(cur) + len(b) > 12 * 1024 and cur:
            dpacks.append(bytes(cur))
            cur = bytearray()
        places.append((len(dpacks), len(cur), len(b)))
        cur += b
    if cur:
        dpacks.append(bytes(cur))
    pw = sec["PACKS"]
    first = struct.unpack_from("<H", pw.b, 0)[0]
    struct.pack_into("<H", pw.b, 0, first + len(dpacks))
    dz = 0
    for raw in dpacks:
        z = lz(raw)
        dz += len(z)
        pw.u32(len(blobs))
        pw.u32(len(z))
        pw.u32(len(raw))
        blobs.raw(z)
        blobs.align(4)
    w = W()
    w.u16(len(items))
    w.u16(0)
    for h_, k in items:
        w.u32(h_)
    for pk, off, n in places:
        w.u16(first + pk)
        w.u16(n)
        w.u32(off)
    sec["DIALOG"] = w
    # ---- UISTR: the menus' words, read straight from flash: u16 n, u16 0, u32 key hashes (sorted),
    # u16 offsets, then the texts (0-terminated)
    ui = sorted((textdata.fnv(k), k) for k in dialog if k.startswith(UI_PREFIXES))
    w = W()
    w.u16(len(ui))
    w.u16(0)
    for h_, k in ui:
        w.u32(h_)
    texts = bytearray()
    offs = []
    for h_, k in ui:
        offs.append(len(texts))
        texts += dialog[k].encode("utf-8") + b"\0"
    for o in offs:
        w.u16(o)
    w.raw(bytes(texts))
    sec["UISTR"] = w
    print("dialog", len(items), "entries, compressed", dz, "fonts", len(sec["FONTS"]))

    # ---- STRINGS: u16 count, u16 offsets (from section start, 32-bit), NUL-terminated
    w = W()
    data = bytearray()
    offs = []
    for s_ in STR.list:
        offs.append(len(data))
        data += s_.encode("utf-8") + b"\0"
    w.u32(len(STR.list))
    base = 4 + 4 * len(STR.list)
    for o in offs:
        w.u32(base + o)
    w.raw(data)
    sec["STRINGS"] = w

    # ---- PICS: the end screens and the mountain (tools/pictures.py)
    w = W()
    pics, PIC_HEADER[:] = pictures.build()
    w.raw(pics)
    sec["PICS"] = w

    # ---- write
    out = W()
    out.raw(b"CLST")
    hdr = len(out)
    for _ in SECTIONS:
        out.u32(0)
    for i, name in enumerate(SECTIONS):
        out.align(4)
        struct.pack_into("<I", out.b, hdr + 4 * i, len(out))
        out.raw(sec[name].b)
        print("%-10s %8d" % (name, len(sec[name])))
    out.align(4)
    open(OUT_BIN, "wb").write(out.b)
    print("data.bin", len(out))
    write_header(names)


def cname(s):
    return "".join(c if c.isalnum() else "_" for c in s)


def write_header(bank_names):
    L = ["/* Generated by tools/pack.py: ids into data.bin. */", "#ifndef DATA_H", "#define DATA_H", ""]
    for i, s in enumerate(SECTIONS):
        L.append("#define SEC_%s %d" % (s, i))
    L.append("")
    for i, b in enumerate(bank_names):
        L.append("#define SB_%s %d" % (cname(b), i))
        for j, an in enumerate(BANKS[b].anims):
            L.append("#define A_%s_%s %d" % (cname(b), cname(an), j))
    L.append("")
    for n, i in ENT_IDS.items():
        L.append("#define ET_%s %d" % (cname(n), i))
    L.append("#define ET_COUNT %d" % len(ENT_IDS))
    for n, i in TRIG_IDS.items():
        L.append("#define TT_%s %d" % (cname(n), i))
    L.append("#define TT_COUNT %d" % len(TRIG_IDS))
    L.append("")
    for name in list(ENT_IDS) + list(TRIG_IDS):
        lay, size = SCHEMA.layout(name)
        for k, kind, off, sz in lay:
            L.append("#define EA_%s_%s %d" % (cname(name), cname(k), off))
            L.append("#define EK_%s_%s '%s'" % (cname(name), cname(k), kind))
        L.append("#define EA_%s__size %d" % (cname(name), size))
    L.append("static const uint8_t ENT_ATTR_SIZE[] = {" + ",".join(str(SCHEMA.layout(n)[1]) for n in ENT_IDS) + "};")
    L.append("static const uint8_t TRIG_ATTR_SIZE[] = {" + ",".join(str(SCHEMA.layout(n)[1]) for n in TRIG_IDS) + "};")
    L.append("")
    for t in TEX.list:
        L.append("#define T_%s %d" % (cname(t.path), t.id))
    L.append("#define TEX_COUNT %d" % len(TEX.list))
    L.append("#define ROOM_POOL_BYTES %d   /* %s */" % ((ROOM_POOL[0] + 64 + 3) & ~3, ROOM_POOL[1]))
    for i, s in enumerate(STYLE_EFFECTS):
        L.append("#define SG_%s %d" % (s, i))
    L += PIC_HEADER
    L.append("")
    L.append("#endif")
    open(OUT_H, "w").write("\n".join(L) + "\n")


if __name__ == "__main__":
    main()
