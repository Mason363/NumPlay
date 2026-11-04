#!/usr/bin/env python3
"""Celeste's content -> the TI-84 Plus CE version's appvars (ce/bin/*.8xv) and ce/src/gen.h.

    CELESTE=<the game's Content folder> python3 tools/pack_ce.py

The CE draws at 8 bits a pixel: every chapter has its own 256 colours, the first COMMON of them the same in all of
them (Madeline, the strawberries, what every chapter has), the rest made from the chapter's own pictures. Everything
lives in archived appvars the game reads in place: CELG0.. (what every chapter draws), CEL<n>0.. (chapter n: its
tiles, rooms, decals and background). An appvar holds 64 KB at most, so a blob is laid out over several of them, and
its pointers are 3 bytes: the offset in the appvar, then which one (src/data.c)."""
import collections
import os
import random
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "tools"))
os.environ.setdefault("CELESTE_CACHE", os.path.join(HERE, "..", "build", "cache"))
from cel import CELESTE, Atlas  # noqa: E402
import tiles as T  # noqa: E402
import entities as E  # noqa: E402
import pack  # noqa: E402  (the NumWorks converter: its map reader, autotiler and sprite banks)

OUT = os.path.join(HERE, "..", "data")   # the appvars' contents (tools/appvar.py makes bin/*.8xv of them)
GEN_H = os.path.join(HERE, "..", "src", "gen.h")
PART_MAX = 65000        # bytes of an appvar
COMMON = 96             # colours every chapter shares (indices 0 .. COMMON - 1)
HAIR, HAIR_DARK, WHITE, BLACK = 1, 2, 3, 4   # fixed colours (HAIR and HAIR_DARK change as Madeline's hair does)
MARGIN = 4              # tiles kept around each room (transitions show the next one's edges)
FORMAT = 1              # bumped when the layout changes (the game checks it)

# (map, chapter number, side): the chapters this build has
CHAPTERS = [("1-ForsakenCity", 1)]
AREA_STYLE = {1: dict(jumpthru="wood", spike="default", crumble="default")}

GAMEPLAY = Atlas("Gameplay")
H = []                  # gen.h lines


def tex(path):
    return GAMEPLAY[path] if path in GAMEPLAY else None


def family(prefix):
    fam = GAMEPLAY.prefix(prefix)
    if not fam and prefix in GAMEPLAY:
        fam = [GAMEPLAY[prefix]]
    return fam


def cname(s):
    return re.sub(r"[^A-Za-z0-9]", "_", s)


# ------------------------------------------------------------------ blobs laid out over appvars

class Ref:
    __slots__ = ("item",)

    def __init__(self, item):
        self.item = item


class Item:
    def __init__(self, name):
        self.name = name
        self.chunks = []
        self.part = self.off = None

    def u8(self, v):
        assert 0 <= v < 256, (self.name, v)
        self.chunks.append(bytes((v,)))
        return self

    def s8(self, v):
        assert -128 <= v < 128, (self.name, v)
        self.chunks.append(struct.pack("<b", v))
        return self

    def u16(self, v):
        assert 0 <= v < 65536, (self.name, v)
        self.chunks.append(struct.pack("<H", v))
        return self

    def s16(self, v):
        assert -32768 <= v < 32768, (self.name, v)
        self.chunks.append(struct.pack("<h", v))
        return self

    def u24(self, v):
        assert 0 <= v < 1 << 24, (self.name, v)
        self.chunks.append(struct.pack("<I", v)[:3])
        return self

    def s24(self, v):
        assert -(1 << 23) <= v < 1 << 23, (self.name, v)
        self.chunks.append(struct.pack("<i", v)[:3])
        return self

    def raw(self, b):
        self.chunks.append(bytes(b))
        return self

    def ref(self, item):
        self.chunks.append(Ref(item))
        return self

    def size(self):
        return sum(3 if isinstance(c, Ref) else c.size() if isinstance(c, Item) else len(c) for c in self.chunks)

    def place(self, part, off):
        """Where it goes, and the items inside it (chunks that are items) with it."""
        self.part, self.off = part, off
        for c in self.chunks:
            if isinstance(c, Item):
                c.place(part, off)
            off += 3 if isinstance(c, Ref) else c.size() if isinstance(c, Item) else len(c)

    def bytes(self):
        out = bytearray()
        for c in self.chunks:
            if isinstance(c, Ref):
                it = c.item
                assert it.part is not None, it.name
                out += struct.pack("<HB", it.off, it.part)
            elif isinstance(c, Item):
                out += c.bytes()
            else:
                out += c
        return bytes(out)


class Blob:
    """Items laid out in order over appvars NAME0, NAME1... (an item never straddles two)."""

    def __init__(self, name):
        self.name = name
        self.items = []

    def item(self, name):
        it = Item(name)
        self.items.append(it)
        return it

    def add(self, it):
        self.items.append(it)
        return it

    def layout(self):
        parts = [bytearray()]
        for it in self.items:
            n = it.size()
            assert n <= PART_MAX, (it.name, n)
            if len(parts[-1]) + n > PART_MAX:
                parts.append(bytearray())
            it.place(len(parts) - 1, len(parts[-1]))
            parts[-1] += b"\0" * n
        for it in self.items:
            parts[it.part][it.off:it.off + it.size()] = it.bytes()
        return parts

    def write(self):
        parts = self.layout()
        total = 0
        for i, p in enumerate(parts):
            open(os.path.join(OUT, "%s%d.bin" % (self.name, i)), "wb").write(bytes(p))
            total += len(p)
        print(f"{self.name}: {len(parts)} appvars, {total} bytes")
        return len(parts)


# ------------------------------------------------------------------ colours

def c15(rgb):
    rgb = rgb.astype(np.int32)
    return (rgb[..., 0] >> 3) << 10 | (rgb[..., 1] >> 3) << 5 | (rgb[..., 2] >> 3)


def unpremultiply(img):
    """Celeste's textures have premultiplied alpha: the colour of each pixel, and its alpha."""
    a = img[..., 3].astype(np.int32)
    rgb = img[..., :3].astype(np.int32)
    u = np.where(a[..., None] > 0, np.minimum(255, rgb * 255 // np.maximum(a[..., None], 1)), 0)
    return u.astype(np.uint8), a


def rgb_of(c):
    r, g, b = (c >> 10) & 31, (c >> 5) & 31, c & 31
    return np.array([r << 3 | r >> 2, g << 3 | g >> 2, b << 3 | b >> 2], np.float64)


def kmeans(counts, fixed, k, iters=12):
    """At most k colours for a {15-bit colour: pixels} table, on top of `fixed` ones (15-bit) that stay: all of them
    when they fit, else k-means in RGB weighed by how many pixels have each colour."""
    fixed = set(fixed)
    cols = [c for c in counts if c not in fixed]
    if len(cols) <= k:
        return sorted(cols)
    pts = np.array([rgb_of(c) for c in cols])
    w = np.array([counts[c] for c in cols], np.float64)
    fx = np.array([rgb_of(c) for c in fixed])
    d = np.min(((pts[:, None, :] - fx[None]) ** 2).sum(-1), 1) if len(fx) else np.full(len(pts), np.inf)
    centers = []
    for _ in range(k):
        j = int(np.argmax(d * np.sqrt(w)))
        centers.append(pts[j])
        d = np.minimum(d, ((pts - pts[j]) ** 2).sum(-1))
    c = np.array(centers)
    for _ in range(iters):
        allc = np.concatenate([c, fx]) if len(fx) else c
        lab = np.argmin(((pts[:, None, :] - allc[None]) ** 2).sum(-1), 1)
        for j in range(k):
            m = lab == j
            if m.any():
                c[j] = (pts[m] * w[m, None]).sum(0) / w[m].sum()
    out = set()
    for v in c:
        v = np.clip(np.round(v), 0, 255).astype(int)
        q = int((v[0] >> 3) << 10 | (v[1] >> 3) << 5 | (v[2] >> 3))
        if q not in fixed:
            out.add(q)
    return sorted(out)


class Palette:
    """256 colours (15-bit), index 0 black (also the tiles' see-through key: art never maps to it)."""

    def __init__(self, colors, reserved=True):
        assert len(colors) <= 256
        self.reserved = reserved
        self.colors = list(colors) + [0] * (256 - len(colors))
        self.n = len(colors)
        self.rgb = np.array([rgb_of(c) for c in self.colors[:self.n]])
        self.cache = {}

    def index(self, q):
        if q in self.cache:
            return self.cache[q]
        d = ((self.rgb - rgb_of(q)) ** 2).sum(1)
        if self.reserved:
            d[0] = d[HAIR] = d[HAIR_DARK] = np.inf
        i = int(np.argmin(d))
        self.cache[q] = i
        return i

    def map(self, img, alpha_min=128):
        """RGBA (premultiplied) -> indices, 0 where see-through."""
        rgb, a = unpremultiply(img)
        q = c15(rgb)
        out = np.zeros(q.shape, np.uint8)
        vis = a >= alpha_min
        for v in np.unique(q[vis]):
            out[vis & (q == v)] = self.index(int(v))
        return out

    def rgb1555(self, lo=0, hi=256):
        return b"".join(struct.pack("<H", c) for c in self.colors[lo:hi])


def count_colors(img, counts, alpha_min=128):
    rgb, a = unpremultiply(img)
    q = c15(rgb)[a >= alpha_min]
    v, n = np.unique(q, return_counts=True)
    for c, k in zip(v.tolist(), n.tolist()):
        counts[c] += k


# ------------------------------------------------------------------ sprites

def rle_sprite(idx, name="sprite"):
    """A picture with see-through pixels (0) as src/draw.c draws it: u8 w, h of the frame, u8 x, y, w, h of what is
    stored, then per stored row: u8 runs, then each run (u8 skip from the end of the one before, u8 n, n pixels)."""
    fh, fw = idx.shape
    assert fw < 256 and fh < 256, (name, fw, fh)
    ys, xs = np.nonzero(idx)
    it = Item(name)
    if len(xs) == 0:
        return it.u8(fw).u8(fh).u8(0).u8(0).u8(0).u8(0)
    x0, x1, y0, y1 = int(xs.min()), int(xs.max()) + 1, int(ys.min()), int(ys.max()) + 1
    it.u8(fw).u8(fh).u8(x0).u8(y0).u8(x1 - x0).u8(y1 - y0)
    for y in range(y0, y1):
        row = idx[y, x0:x1]
        runs = []
        x = 0
        while x < len(row):
            if row[x] == 0:
                x += 1
                continue
            s = x
            while x < len(row) and row[x] != 0 and x - s < 255:
                x += 1
            runs.append((s, x))
        it.u8(len(runs))
        at = 0
        for s, e in runs:
            it.u8(s - at).u8(e - s).raw(bytes(row[s:e].tolist()))
            at = e
    return it


class SpriteSet:
    """A blob's sprites: a table item (u16 count, then a pointer each) and each one's picture."""

    def __init__(self, blob, prefix):
        self.blob = blob
        self.prefix = prefix
        self.ids = {}
        self.list = []

    def add(self, key, idx):
        if key in self.ids:
            return self.ids[key]
        it = rle_sprite(idx, str(key))
        self.blob.add(it)
        self.ids[key] = len(self.list)
        self.list.append(it)
        return self.ids[key]

    def table(self):
        t = Item(self.prefix + " sprite table")
        t.u16(len(self.list))
        for it in self.list:
            t.ref(it)
        return t


def bank_anims(name, anims=None):
    b = pack.bank(name)
    return b, [(a, an) for a, an in b.anims.items() if anims is None or a in anims]


class Anims:
    """Sprite banks (Sprites.xml): each anim u8 frames, u8 flags (1 loop), u16 delay (1/600 s), u8 goto (255: none),
    u8 its bank, then u16 sprite ids. gen.h names them A_<bank>_<anim>, and B_<bank> the bank (its first anim and its
    origin)."""

    def __init__(self, prefix):
        self.prefix = prefix
        self.list = []           # (bank, name, frames[keys], delay, loop, gotos)
        self.banks = collections.OrderedDict()

    def add_bank(self, name, image, anims=None, alias=None, missing_ok=True):
        b, an = bank_anims(name, anims)
        key = alias or name
        first = len(self.list)
        for a, d in an:
            keys = [image(pack.TEX.list[t].path) for t in d["frames"]]
            if not keys:
                continue
            self.list.append((key, a, keys, d["delay"], d["loop"], d["goto"]))
        self.banks[key] = (first, len(self.list), b.origin)
        return b

    def write(self, blob, sprites):
        it = blob.item(self.prefix + " anims")
        index = {(k, a): i for i, (k, a, *_r) in enumerate(self.list)}
        it.u16(len(self.list))
        recs = []
        for k, a, keys, delay, loop, gotos in self.list:
            g = 255
            if gotos:
                g = index.get((k, gotos[0][0]), 255)
            r = Item("anim")
            r.u8(len(keys)).u8(1 if loop else 0).u16(int(round(delay * 600))).u8(g).u8(list(self.banks).index(k))
            for key in keys:
                r.u16(sprites.ids[key])
            recs.append(r)
        for r in recs:
            it.ref(r)
        for r in recs:
            blob.add(r)
        p = self.prefix
        for bi, (k, (first, last, origin)) in enumerate(self.banks.items()):
            H.append(f"#define {p}B_{cname(k)} {bi}")
        for i, (k, a, *_r) in enumerate(self.list):
            H.append(f"#define {p}A_{cname(k)}_{cname(a)} {i}")
        H.append(f"/* banks' origins: mode (0 none, 1 pixels, 2 justify x256), x, y */")
        H.append(f"#define {p}BANK_ORIGINS {{" + ", ".join(
            "{%d, %d, %d}" % (o[0], round(o[1] * (256 if o[0] == 2 else 1)), round(o[2] * (256 if o[0] == 2 else 1)))
            for (first, last, o) in self.banks.values()) + "}")
        H.append(f"#define {p}NBANKS {len(self.banks)}")
        return it


# ------------------------------------------------------------------ the common sprites

PLAYER_ANIMS = ["idle", "idleA", "idleB", "idleC", "lookUp", "walk", "push", "runSlow", "runFast", "runStumble",
                "dash", "slide", "jumpSlow", "jumpFast", "fallSlow", "fallFast", "tired", "tiredStill", "wallslide",
                "climbLookBackStart", "climbLookBack", "climbup", "duck", "fallPose", "edge", "edgeBack", "flip",
                "skid", "dangling", "deadside", "deadup", "deaddown", "sitDown", "asleep", "wakeUp", "spin", "fall",
                "bigFall", "launch", "launchRecover"]


class Common:
    """What every chapter draws: Madeline, her hair, the pickups and the objects the chapters share."""

    def __init__(self):
        self.blob = Blob("CELG")
        self.head = self.blob.item("common header")
        self.images = collections.OrderedDict()   # key -> RGBA
        self.anims = Anims("")
        self.sprites = None
        self.meta = {}

    def image(self, path):
        if path not in self.images:
            s = tex(path)
            assert s is not None, path
            self.images[path] = s.image()
        return path

    def collect(self):
        b = self.anims.add_bank("player", self.image, PLAYER_ANIMS)
        for path, (hair, carry) in b.meta.items():
            fam = family("characters/player/" + path)
            hairs = hair.split("|") if hair is not None else []
            for i, s in enumerate(fam):
                m = [0, 0, 0, 1]
                if i < len(hairs):
                    h = hairs[i].strip()
                    if h == "x":
                        m[3] = 0
                    elif h:
                        if ":" in h:
                            h, fr = h.split(":")
                            m[2] = int(fr)
                        hx, hy = h.split(",")
                        m[0], m[1] = int(hx), int(hy)
                self.meta[s.path] = m
        for p in ("characters/player/hair00", "characters/player/bangs00", "characters/player/bangs01",
                  "characters/player/bangs02"):
            self.image(p)
        for bank in ("strawberry", "ghostberry", "goldberry", "strawberrySeed"):
            self.anims.add_bank(bank, self.image)
        for f in family("objects/refill/idle") + family("objects/refill/flash"):
            self.image(f.path)
        self.image("objects/refill/outline")
        for f in family("objects/spring/"):
            self.image(f.path)
        for f in family("collectables/strawberry/wings"):
            self.image(f.path)

    def hair_sprites(self):
        """Madeline's hair as src/player.c draws it: circles down it (hair00 scaled 0.25 + (1 - i / n) * 0.75, for 2-7
        pieces) and the bangs (bangs00-02, both ways), each also outlined (itself a pixel off each way, black). 16 x 16,
        the piece's place at (8, 8): S_hair_disc + 2 * ((n - 1) * (n - 2) / 2 + i - 1) (+1 outlined), S_hair_bangs +
        2 * (2 * bangs + flipped) (+1 outlined)."""
        def masks(p):
            img = self.images[p]
            on = [[img[y, x, 3] >= 128 for x in range(10)] for y in range(10)]
            dark = [[on[y][x] and img[y, x, 0] < 200 for x in range(10)] for y in range(10)]
            return on, dark

        def outlined(f):
            o = np.zeros_like(f)
            m = f != 0
            for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
                o[np.roll(np.roll(m, dy, 0), dx, 1)] = BLACK
            return o

        disc, _ = masks("characters/player/hair00")
        first = None
        for n in range(2, 8):
            for i in range(1, n):
                k = 64 + (256 - i * 256 // n) * 3 // 4
                half = (5 * k + 128) >> 8
                f = np.zeros((16, 16), np.uint8)
                for dy in range(-half, half):
                    v = 5 + (dy * 256 + 128) // k
                    if not 0 <= v < 10 or not any(disc[v]):
                        continue
                    us = [u for u in range(10) if disc[v][u]]
                    x0, x1 = (us[0] - 5) * k // 256, ((us[-1] - 4) * k - 1) // 256
                    f[dy + 8, x0 + 8:x1 + 9] = HAIR
                a = self.sprites.add(("hair disc", n, i), f)
                assert self.sprites.add(("hair disc outlined", n, i), outlined(f)) == a + 1
                first = a if first is None else first
        H.append(f"#define S_hair_disc {first}")
        first = None
        for b in range(3):
            on, dark = masks(f"characters/player/bangs0{b}")
            for flip in (0, 1):
                f = np.zeros((16, 16), np.uint8)
                for v in range(10):
                    for u in range(10):
                        if on[v][u]:
                            f[v - 5 + 8, (4 - u if flip else u - 5) + 8] = HAIR_DARK if dark[v][u] else HAIR
                a = self.sprites.add(("hair bangs", b, flip), f)
                assert self.sprites.add(("hair bangs outlined", b, flip), outlined(f)) == a + 1
                first = a if first is None else first
        H.append(f"#define S_hair_bangs {first}")

    def palette(self):
        counts = collections.Counter()
        for k, img in self.images.items():
            count_colors(img, counts)
        fixed = [0, 0x5466, 0x2C33, 0x7FFF, 0x0000]   # 0 black, HAIR, HAIR_DARK, white, black for the art
        extra = kmeans(counts, set(fixed), COMMON - len(fixed))
        cols = fixed + extra
        print(f"common: {len(counts)} colours -> {len(cols)}")
        return cols + [0] * (COMMON - len(cols))   # (the chapters' own start at COMMON)

    def build(self):
        self.collect()
        self.cols = self.palette()
        pal = Palette(self.cols)
        self.pal = pal
        self.sprites = SpriteSet(self.blob, "common")
        for k, img in self.images.items():
            self.sprites.add(k, pal.map(img))
        for k in self.images:
            H.append(f"#define S_{cname(k)} {self.sprites.ids[k]}")
        anims = self.anims.write(self.blob, self.sprites)
        self.hair_sprites()
        # Madeline's frames: hair x, y (signed), bangs frame | has hair << 7, one each sprite id (0 for the others)
        meta = self.blob.item("player meta")
        n = len(self.sprites.list)
        meta.u16(n)
        inv = {v: k for k, v in self.sprites.ids.items()}
        for i in range(n):
            m = self.meta.get(inv[i], [0, 0, 0, 1])
            meta.s8(m[0]).s8(m[1]).u8(m[2] | m[3] << 7)
        # hair: hair00 and bangs 0-2 as 1-bit masks 10x10 (bit x of row y), the bangs' shade (2: dark) as a second mask
        hair = self.blob.item("hair")
        for p in ("characters/player/hair00", "characters/player/bangs00", "characters/player/bangs01",
                  "characters/player/bangs02"):
            img = self.images[p]
            for y in range(10):
                v = sum(1 << x for x in range(10) if img[y, x, 3] >= 128)
                hair.u16(v)
            for y in range(10):
                v = sum(1 << x for x in range(10) if img[y, x, 3] >= 128 and img[y, x, 0] < 200)
                hair.u16(v)
        table = self.sprites.table()
        self.blob.add(table)
        h = self.head
        h.raw(b"CELG").u8(FORMAT)
        h.raw(pal.rgb1555(0, COMMON))
        h.ref(table).ref(anims).ref(meta).ref(hair)
        H.append(f"#define COMMON {COMMON}")
        H.append(f"#define HAIR {HAIR}\n#define HAIR_DARK {HAIR_DARK}\n#define WHITE {WHITE}\n#define BLACK {BLACK}")
        return self.blob.write()


# ------------------------------------------------------------------ chapters

def is_air(c):
    return c in ("0", "\0")


class Tiles:
    """A chapter's 8x8 tiles: 64 bytes each (0: see-through), deduplicated; tile 0 is none."""

    def __init__(self):
        self.ids = {bytes(64): 0}
        self.list = [bytes(64)]

    def add(self, idx):
        key = bytes(idx.astype(np.uint8).reshape(-1).tolist())
        if key not in self.ids:
            self.ids[key] = len(self.list)
            self.list.append(key)
        return self.ids[key]

    def opaque(self, i):
        return 0 not in self.list[i]


def autotile_rect(sets, get, rect, force, behaviour, level_bounds, rnd):
    """Autotiler.Generate for one rectangle (GenerateBox, GenerateOverlay): {(x, y): (qx, qy)} of the tileset of
    `force`. get(x, y): the map's character there (None: no map)."""
    rx, ry, rw, rh = rect

    def inside(x, y):
        return rx <= x < rx + rw and ry <= y < ry + rh

    def check(t, x, y):
        if inside(x, y):
            return True
        c = get(x, y)
        if c is None:
            return behaviour.edges_extend
        return not is_air(c) and not t.ignore(c)

    def same(x1, y1, x2, y2):
        return any(lx <= x1 < lx + lw and ly <= y1 < ly + lh and lx <= x2 < lx + lw and ly <= y2 < ly + lh
                   for lx, ly, lw, lh in level_bounds)

    out = {}
    t = sets[force]
    for x in range(rx, rx + rw):
        for y in range(ry, ry + rh):
            adj, full = [], True
            for i in (-1, 0, 1):
                for j in (-1, 0, 1):
                    f = check(t, x + j, y + i)
                    if not f and behaviour.edges_ignore_out and not same(x, y, x + j, y + i):
                        f = True
                    adj.append(1 if f else 0)
                    full = full and f
            tl = None
            if full:
                if behaviour.padding_ignore_out:
                    pad = any(not check(t, x + dx, y + dy) and same(x, y, x + dx, y + dy)
                              for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2)))
                else:
                    pad = any(not check(t, x + dx, y + dy) for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2)))
                tl = t.padded if pad else t.center
            else:
                for m, tiles, sprites in t.masks:
                    if all(m[k] == 2 or m[k] == adj[k] for k in range(9)):
                        tl = tiles
                        break
            if tl:
                out[(x, y)] = rnd.choice(tl)
    return out


# entity types: name -> (code name, extra fields); every kind the chapters have that the CE plays
ENTITY_TYPES = ["player", "spikesUp", "spikesDown", "spikesLeft", "spikesRight", "jumpThru", "zipMover", "strawberry",
                "goldenBerry", "crumbleBlock", "refill", "spring", "wallSpringLeft", "wallSpringRight", "cassetteBlock",
                "fallingBlock", "flutterbird", "fakeWall", "dashBlock", "lightbeam", "checkpoint", "bonfire",
                "cassette", "npc", "coverupWall", "memorial", "memorialTextController", "birdForsakenCityGem", "wire",
                "blackGem"]
TRIGGER_TYPES = ["cameraOffsetTrigger", "cameraTargetTrigger", "changeRespawnTrigger", "eventTrigger",
                 "goldenBerryCollectTrigger"]
POSITION_MODES = ["NoEffect", "LeftToRight", "RightToLeft", "TopToBottom", "BottomToTop", "HorizontalCenter",
                  "VerticalCenter"]


class Chapter:
    def __init__(self, name, number, common):
        self.name = name
        self.number = number
        self.style = AREA_STYLE.get(number, dict(jumpthru="wood", spike="default", crumble="default"))
        self.common = common
        self.ch = pack.Chapter(name)
        self.fgsets = T.read_tilesets("ForegroundTiles")
        self.bgsets = T.read_tilesets("BackgroundTiles")
        at = pack.ET.parse(os.path.join(CELESTE, "Graphics", "AnimatedTiles.xml")).getroot()
        anim_frames = {sp.get("name"): len(pack.TEX.family(sp.get("path"))) for sp in at.iter("sprite")}
        self.ch.generate(self.fgsets, self.bgsets, anim_frames)
        self.blob = Blob("CEL%d" % number)
        self.head = self.blob.item("chapter header")
        self.imgs = {}
        self.rnd = random.Random(number)
        self.nberries = 0

    # -------------------------------------------------------------- pictures
    def image(self, path):
        if path not in self.imgs:
            s = tex(path)
            assert s is not None, path
            self.imgs[path] = s.image()
        return self.imgs[path]

    def tileset(self, layer, c):
        t = (self.fgsets if layer == 0 else self.bgsets)[c]
        return self.image(t.path)

    def cell_rgba(self, layer, gx, gy):
        """The tile the game draws at map cell (gx, gy) of a layer, RGBA 8x8 (None: none)."""
        tiles = self.ch.fgt if layer == 0 else self.ch.bgt
        vm = self.ch.fgmap if layer == 0 else self.ch.bgmap
        c = vm[gx, gy]
        if is_air(c):
            return None
        t = tiles.get((gx, gy))
        if not t or t[0] is None:
            return None
        qx, qy = t[0]
        return self.tileset(layer, c)[qy * 8:qy * 8 + 8, qx * 8:qx * 8 + 8]

    def room_cells(self, r):
        """(layer, room cell x, y, RGBA) of a room's tiles with MARGIN cells around."""
        TX, TY, TW, TH = self.ch.tb
        x0, y0 = r.x // 8 - MARGIN - TX, r.y // 8 - MARGIN - TY
        tw, th = r.w // 8 + 2 * MARGIN, r.h // 8 + 2 * MARGIN
        for layer in (0, 1):
            for y in range(th):
                for x in range(tw):
                    img = self.cell_rgba(layer, x0 + x, y0 + y)
                    if img is not None:
                        yield layer, x, y, img
        # scenery tiles drawn over the autotiled ones (fgtiles / bgtiles: tilesets/scenery, 8x8 each)
        for layer, name in ((0, "fgtiles"), (1, "bgtiles")):
            for x, y, v in self.csv_cells(r, name):
                sc = self.image("tilesets/scenery")
                cols = sc.shape[1] // 8
                yield layer, x + MARGIN, y + MARGIN, ("scenery", sc[(v // cols) * 8:(v // cols) * 8 + 8,
                                                                 (v % cols) * 8:(v % cols) * 8 + 8])

    @staticmethod
    def csv_cells(r, name):
        out = []
        el = r.e.child(name)
        if el is not None and el.get("innerText"):
            for y, row in enumerate(el.get("innerText").replace("\r", "").split("\n")):
                for x, v in enumerate(row.split(",")):
                    v = v.strip()
                    if v and v != "-1":
                        out.append((x, y, int(v)))
        return out

    def solid_char(self, gx, gy):
        """The map's fg character at map cell (gx, gy) (map-wide grid coordinates), None outside the map."""
        TX, TY, TW, TH = self.ch.tb
        if not (0 <= gx < TW and 0 <= gy < TH):
            return None
        return self.ch.fgmap[gx, gy]

    def decal_paths(self):
        out = []
        for r in self.ch.rooms:
            for layer in ("fgdecals", "bgdecals"):
                el = r.e.child(layer)
                for d in (el.children if el else []):
                    p = decal_base(d["texture"])
                    if p not in out:
                        out.append(p)
        return out

    def background_layers(self):
        """The chapter's parallax backgrounds (stylegrounds) and its tint: the foreground's additive gradient
        (purplesunset), added to every colour (src/data.c)."""
        bgs, fgs = pack.styleground_list(self.ch)
        self.layers = [(n, a) for n, a in bgs if n == "parallax" and a.get("texture", "").startswith("bgs/")]
        self.tint = (0, 0, 0)
        for n, a in fgs:
            if n == "parallax" and a.get("blendmode") == "additive" and a.get("texture") == "purplesunset":
                g = Atlas("Misc")["purplesunset"].image()[..., :3].reshape(-1, 3).mean(0) * float(a.get("alpha", 1))
                self.tint = tuple(int(v) for v in g)
        # the mean of their scroll factors: the CE moves the room's picture of them by one factor (1/256)
        sx = [float(a.get("scrollx", 1)) for n, a in self.layers] or [0]
        sy = [float(a.get("scrolly", 1)) for n, a in self.layers] or [0]
        self.bg_scroll = (int(round(np.mean(sx) * 256)), int(round(np.mean(sy) * 256)))

    def background_at(self, r, cx, cy):
        """The backgrounds as Parallax.Render draws them with the camera at (cx, cy), in room r: 180 x 320 RGB."""
        W, Hh = 320, 180
        out = np.zeros((Hh, W, 3), np.float64)
        for n, a in self.layers:
            if a.get("only") and not pack.room_matches(a["only"], r.name):
                continue
            if a.get("exclude") and pack.room_matches(a["exclude"], r.name):
                continue
            s = tex(a["texture"])
            if s is None:
                continue
            img = s.image().astype(np.float64)
            alpha = float(a.get("alpha", 1))
            tw, th = img.shape[1], img.shape[0]
            px = int(np.floor(a.get("x", 0) - cx * float(a.get("scrollx", 0))))
            py = int(np.floor(a.get("y", 0) - cy * float(a.get("scrolly", 0))))
            if a.get("loopy"):
                py %= th
                py -= th if py > 0 else 0
            ys = np.arange(Hh) - py
            if a.get("loopy"):
                ys %= th
            ok = (ys >= 0) & (ys < th)
            rows = img[ys[ok]][:, (np.arange(W) - px) % tw]
            a1 = rows[..., 3:4] / 255 * alpha
            out[ok] = out[ok] * (1 - a1) + rows[..., :3] * alpha
        return np.clip(out, 0, 255).astype(np.uint8)

    def room_backgrounds(self):
        """Each room's backgrounds as seen from its first spawn point's camera (the room's picture of them)."""
        self.bgs = {}
        cache = {}
        if not self.layers:
            return
        for r in self.ch.rooms:
            ents = r.e.child("entities").children if r.e.child("entities") else []
            sp = [(e["x"], e["y"]) for e in ents if e.name == "player"] or [(0, r.h)]
            cx = min(max(r.x + sp[0][0] - 160, r.x), r.x + r.w - 320)
            cy = min(max(r.y + sp[0][1] - 90, r.y), r.y + r.h - 180)
            cx, cy = int(round(cx / 160)) * 160, int(round(cy / 64)) * 64   # (rooms near each other share one)
            key = (cx, cy, tuple(n for n, a in self.layers if (not a.get("only") or pack.room_matches(a["only"], r.name))
                                 and not (a.get("exclude") and pack.room_matches(a["exclude"], r.name))))
            if key not in cache:
                cache[key] = self.background_at(r, cx, cy)
            self.bgs[r.name] = (cx, cy, cache[key])

    # -------------------------------------------------------------- building
    def build(self):
        common = self.common
        self.background_layers()
        self.room_backgrounds()
        counts = collections.Counter()
        cells = {}
        for r in self.ch.rooms:
            cells[r.name] = list(self.room_cells(r))
            for layer, x, y, img in cells[r.name]:
                count_colors(img[1] if isinstance(img, tuple) else img, counts)
        decals = self.decal_paths()
        for p in decals:
            for s in decal_family(p):
                count_colors(s.image(), counts)
        bgcounts = collections.Counter()
        for cx, cy, bg in self.bgs.values():
            count_colors(np.concatenate([bg, np.full(bg.shape[:2] + (1,), 255, np.uint8)], 2), bgcounts)
        bgcols = kmeans(bgcounts, set(), 15)   # the backgrounds have 16 colours (4 bits a pixel in RAM)
        for c in bgcols:
            counts[c] += bgcounts.get(c, 1)
        ent_paths = self.entity_images()
        for p in ent_paths:
            count_colors(self.image(p), counts)
        extra = kmeans(counts, set(common.cols), 256 - COMMON)
        pal = Palette(common.cols + extra)
        self.pal = pal
        print(f"{self.name}: {len(counts)} colours, {len(extra)} of its own")
        self.tiles = Tiles()
        self.sprites = SpriteSet(self.blob, "chapter")
        # decals' sprites: each family's frames in a row (an animated decal plays them)
        self.decal_ids = {}
        for p in ent_paths:
            self.sprites.add(p, pal.map(self.image(p)))
        # the backgrounds' 16 colours (chapter palette indices), then each room's picture: 4 bits a pixel (the left
        # one low), zx0 packed; rooms with the same picture share it
        bgpal = Palette([0] + bgcols, reserved=False)
        bgmap = [BLACK] + [pal.index(c) for c in bgcols] + [0] * (15 - len(bgcols))
        self.bg_items = {}
        shared = {}
        for name, (cx, cy, bg) in self.bgs.items():
            q = bgpal.map(np.concatenate([bg, np.full(bg.shape[:2] + (1,), 255, np.uint8)], 2))
            flat = q.reshape(-1).astype(np.uint8)
            packed = bytes((flat[0::2] | (flat[1::2] << 4)).tolist())
            key = packed
            if key not in shared:
                shared[key] = self.blob.item("background " + name).raw(zx0(packed))
            self.bg_items[name] = (cx, cy, shared[key])
        print(f"{self.name}: {len(shared)} backgrounds, {sum(it.size() for it in shared.values())} bytes")
        rooms = []
        for r in self.ch.rooms:
            rooms.append(self.room(r, cells[r.name]))
        # tiles: 1000 a part
        chunks = []
        for i in range(0, len(self.tiles.list), 1000):
            it = self.blob.item("tiles %d" % i)
            for t in self.tiles.list[i:i + 1000]:
                it.raw(t)
            chunks.append(it)
        # each tile's rows: those without see-through pixels, those with something (bit 0: the top row)
        flags = self.blob.item("tile flags")
        for t in self.tiles.list:
            full = sum(1 << r for r in range(8) if 0 not in t[r * 8:r * 8 + 8])
            some = sum(1 << r for r in range(8) if any(t[r * 8:r * 8 + 8]))
            flags.u8(full).u8(some)
        table = self.sprites.table()
        self.blob.add(table)
        h = self.head
        h.raw(b"CELC").u8(FORMAT).u8(self.number)
        h.raw(pal.rgb1555(COMMON, 256))
        h.u8(self.tint[0]).u8(self.tint[1]).u8(self.tint[2]).u8(self.nberries)
        # remaps: each colour tinted red (Color.Red: Madeline tired), and halved (Color.Gray)
        for f in (lambda c: c * np.array([1, 0, 0]), lambda c: c * 0.5):
            for i in range(256):
                if i == 0 or i >= pal.n:
                    h.u8(i)
                    continue
                c = np.clip(f(rgb_of(pal.colors[i])), 0, 255).astype(int)
                h.u8(pal.index(int((c[0] >> 3) << 10 | (c[1] >> 3) << 5 | (c[2] >> 3))))
        h.u16(len(self.tiles.list)).u8(len(chunks))
        for c in chunks:
            h.ref(c)
        h.ref(flags).ref(table)
        h.u8(1 if self.bgs else 0).u8(self.bg_scroll[0]).u8(self.bg_scroll[1])
        for v in bgmap:
            h.u8(v)
        h.u16(len(rooms))
        for it in rooms:
            h.ref(it)
        print(f"{self.name}: {len(self.tiles.list)} tiles, {len(self.sprites.list)} sprites, {len(rooms)} rooms")
        return self.blob.write()

    def entity_images(self):
        """Textures the chapter's entities draw (they get the chapter's colours)."""
        st = self.style
        out = []
        for d in ("up", "down", "left", "right"):
            out += [s.path for s in family("danger/spikes/%s_%s" % (st["spike"], d))]
        return out

    def room(self, r, cells):
        ROOM_FLAGS = [("dark", 1), ("space", 2), ("underwater", 4), ("disableDownTransition", 8), ("whisper", 16)]
        tw, th = r.w // 8 + 2 * MARGIN, r.h // 8 + 2 * MARGIN
        grids = [[[0] * tw for _ in range(th)] for _ in range(2)]
        imgs = [[[None] * tw for _ in range(th)] for _ in range(2)]
        for layer, x, y, img in cells:
            if isinstance(img, tuple):   # scenery over the tile
                base = imgs[layer][y][x]
                sc = img[1]
                if base is None:
                    imgs[layer][y][x] = sc
                else:
                    m = sc[..., 3:4] >= 128
                    imgs[layer][y][x] = np.where(m, sc, base)
            else:
                imgs[layer][y][x] = img
        for layer in (0, 1):
            for y in range(th):
                for x in range(tw):
                    if imgs[layer][y][x] is not None:
                        grids[layer][y][x] = self.tiles.add(self.pal.map(imgs[layer][y][x]))
        # collision: the fg characters (solid even where no tile is drawn)
        TX, TY, TW, TH = self.ch.tb
        x0, y0 = r.x // 8 - MARGIN - TX, r.y // 8 - MARGIN - TY
        solid = [[0 if is_air(self.ch.fgmap[x0 + x, y0 + y]) else 1 for x in range(tw)] for y in range(th)]
        it = self.blob.item("room " + r.name)
        name = r.name.encode()
        it.u8(len(name)).raw(name)
        it.s24(r.x).s24(r.y).u16(r.w).u16(r.h)
        fl = sum(b for k, b in ROOM_FLAGS if r.e.get(k))
        it.u8(fl).s8(int(r.e.get("cameraOffsetX", 0))).s8(int(r.e.get("cameraOffsetY", 0)))
        it.u16(tw).u16(th)
        # the grids: each cell a byte, its tile's number in the room's own list of tiles (u16 chapter tiles), the fg
        # grid, the bg grid, then a bit a cell for what is solid (bit x & 7 of byte x >> 3 of each row), zx0 packed
        ids = sorted(set(v for g in grids for row in g for v in row) | {0})
        assert len(ids) <= 256, (r.name, len(ids))
        loc = {v: i for i, v in enumerate(ids)}
        raw = bytearray()
        for g in grids:
            for row in g:
                raw += bytes(loc[v] for v in row)
        rb = (tw + 7) // 8
        for row in solid:
            bits = bytearray(rb)
            for x, v in enumerate(row):
                if v:
                    bits[x >> 3] |= 1 << (x & 7)
            raw += bits
        packed = zx0(bytes(raw))
        GRID_MAX[0] = max(GRID_MAX[0], len(raw))
        it.u8(len(ids) - 1)
        for v in ids:
            it.u16(v)
        it.u16(len(raw))
        it.ref(self.blob.item("grid " + r.name).raw(packed))
        it.ref(self.decals(r))
        ents, trigs = self.entities(r)
        it.ref(ents).ref(trigs)
        if r.name in self.bg_items:
            cx, cy, bgit = self.bg_items[r.name]
            it.s24(cx).s24(cy).ref(bgit)
        else:
            it.s24(0).s24(0).u24(0xFFFFFF)
        return it

    def rows(self, r, grid, solid):
        """A layer's rows: a pointer each, then each row's runs: u8 n, u16 tile (bit 15: solid, fg only)."""
        th, tw = len(grid), len(grid[0])
        table = self.blob.item("rows " + r.name)
        data = []
        for y in range(th):
            line = [grid[y][x] | (0x8000 if solid and solid[y][x] else 0) for x in range(tw)]
            it = Item("row")
            x = 0
            while x < tw:
                v = line[x]
                n = 1
                while x + n < tw and line[x + n] == v and n < 255:
                    n += 1
                it.u8(n).u16(v)
                x += n
            data.append(it)
        for it in data:
            table.ref(it)
        # rows go after their table, packed into items of up to 4 KB (an item never straddles two appvars)
        cur = None
        for it in data:
            if cur is None or cur.size() + it.size() > 4096:
                cur = self.blob.item("row data " + r.name)
            # the row's pointer is to its own bytes inside `cur`: make it an item of its own laid out within cur
            cur.chunks.append(it)
        return table

    def decal_sprite(self, p, fl):
        """A decal's frames as sprites, turned as it is placed (fl: 1 flipped across, 2 upside down): the CE draws
        them as they are stored, fast. (first sprite, frames)"""
        if (p, fl) not in self.decal_ids:
            first = None
            fam = decal_family(p)
            for s in fam:
                img = s.image()
                if fl & 1:
                    img = img[:, ::-1]
                if fl & 2:
                    img = img[::-1]
                i = self.sprites.add((s.path, fl), self.pal.map(np.ascontiguousarray(img)))
                first = i if first is None else first
            self.decal_ids[(p, fl)] = (first, len(fam))
        return self.decal_ids[(p, fl)]

    def decals(self, r):
        """u8 bg count, u8 fg count, then each: u16 sprite, s16 x, s16 y (its centre, room coordinates),
        u8 flags (0: the sprites are turned already), u8 frames (an animated one plays them, 12 a second)."""
        it = self.blob.item("decals " + r.name)
        lists = []
        for layer in ("bgdecals", "fgdecals"):
            el = r.e.child(layer)
            lst = []
            for d in (el.children if el else []):
                p = decal_base(d["texture"])
                fl = (1 if d.get("scaleX", 1) < 0 else 0) | (2 if d.get("scaleY", 1) < 0 else 0)
                sid, n = self.decal_sprite(p, fl)
                if sid is None:
                    continue
                lst.append((sid, int(round(d["x"])), int(round(d["y"])), 0, n))
            lists.append(lst)
        it.u8(len(lists[0])).u8(len(lists[1]))
        for lst in lists:
            for sid, x, y, fl, n in lst:
                it.u16(sid).s16(x).s16(y).u8(fl).u8(n)
        return it

    def entities(self, r):
        """Records: u8 type, u8 size, s16 x, y (room), u16 w, h, u16 id, u8 nodes, nodes (s16 x, y), u8 flags,
        u8 n, u16 values[n] (each type's own: tiles, numbers)."""
        out = []
        for kind, types in (("entities", ENTITY_TYPES), ("triggers", TRIGGER_TYPES)):
            el = r.e.child(kind)
            it = self.blob.item(kind + " " + r.name)
            recs = []
            for e in (el.children if el else []):
                if e.name not in types:
                    if e.name not in E.DROP and e.name not in E.DROP_TRIGGERS:
                        MISSING[e.name] += 1
                    continue
                recs.append(self.entity(r, e, types.index(e.name)))
            it.u8(len(recs))
            for rec in recs:
                it.raw(rec)
            out.append(it)
        return out

    def entity(self, r, e, t):
        a = lambda k, d=None: (E.attr(e, k) if E.attr(e, k) is not None else d)   # noqa: E731
        x, y = int(e["x"]), int(e["y"])
        w, h = int(e.get("width", 0)), int(e.get("height", 0))
        nodes = [(int(n["x"]), int(n["y"])) for n in e.children]
        flags = 0
        vals = []
        n = e.name
        if n.startswith("spikes"):
            d = n[6:].lower()
            fam = family("danger/spikes/%s_%s" % (self.style["spike"], d))
            size = w if d in ("up", "down") else h
            for j in range(max(1, size // 8)):
                vals.append(self.sprites.ids[self.rnd.choice(fam).path])
        elif n == "jumpThru":
            tname = a("texture", "default")
            tname = self.style["jumpthru"] if tname in ("default", None) else tname
            img = self.image("objects/jumpthru/" + tname)
            cols = img.shape[1] // 8
            TX, TY, TW, TH = self.ch.tb
            gx, gy = (r.x + x) // 8 - TX, (r.y + y) // 8 - TY
            ncol = max(1, w // 8)
            for i in range(ncol):
                if i == 0:
                    c, row = 0, 0 if self.solid_at(gx - 1, gy, (r.x + x - 1) % 8) else 1
                elif i == ncol - 1:
                    c, row = cols - 1, 0 if self.solid_at(gx + ncol, gy, 0) else 1
                else:
                    c, row = 1 + self.rnd.randrange(cols - 2), self.rnd.choice((0, 1))
                vals.append(self.tiles.add(self.pal.map(img[row * 8:row * 8 + 8, c * 8:c * 8 + 8])))
        elif n == "crumbleBlock":
            img = self.image("objects/crumbleBlock/" + self.style["crumble"])
            out = self.image("objects/crumbleBlock/outline")
            for j in range(0, max(8, w), 8):
                k = ((abs(r.x + x) + j) // 8) % 4
                vals.append(self.tiles.add(self.pal.map(img[:8, k * 8:k * 8 + 8])))
            for j in range(0, max(8, w), 8):
                k = 3 if w <= 8 else 0 if j == 0 else 1 if j < w - 8 else 2
                vals.append(self.tiles.add(self.pal.map(out[:8, k * 8:k * 8 + 8])))
        elif n in ("fallingBlock", "dashBlock", "fakeWall", "coverupWall"):
            c = str(a("tiletype", "3"))
            blend = n in ("fakeWall", "coverupWall") or (n == "dashBlock" and a("blendin", False))
            if n == "fallingBlock":
                flags |= 1 if a("climbFall", True) else 0
                flags |= 2 if a("behind", False) else 0
            if n == "dashBlock":
                flags |= 1 if a("permanent", True) else 0
                flags |= 2 if a("canDash", True) else 0
                flags |= 4 if blend else 0
            vals += self.block_tiles(r, x, y, w, h, c, blend)
        elif n == "zipMover":
            pass
        elif n in ("strawberry", "goldenBerry"):
            flags |= 1 if a("winged", False) else 0
            flags |= 2 if a("moon", False) else 0
            if n == "strawberry":
                self.nberries += 1
            vals += [int(a("checkpointID", -1)) & 0xFFFF, int(a("order", -1)) & 0xFFFF,
                     self.nberries - 1 if n == "strawberry" else 0xFFFF]
        elif n == "refill":
            flags |= 1 if a("twoDash", False) else 0
            flags |= 2 if a("oneUse", False) else 0
        elif n == "cassetteBlock":
            vals += [int(a("index", 0)), int(round(float(a("tempo", 1)) * 100))]
        elif n == "cameraOffsetTrigger":
            vals += [int(round(float(a("cameraX", 0)) * 48)) & 0xFFFF, int(round(float(a("cameraY", 0)) * 32)) & 0xFFFF]
        elif n == "cameraTargetTrigger":
            vals += [int(round(float(a("lerpStrength", 0)) * 256)), POSITION_MODES.index(a("positionMode", "NoEffect"))]
            flags |= (1 if a("xOnly", False) else 0) | (2 if a("yOnly", False) else 0)
        elif n == "eventTrigger":
            vals += [EVENTS.setdefault(a("event", ""), len(EVENTS))]
        elif n == "bonfire":
            flags |= {"unlit": 0, "lit": 1, "smoking": 2}.get(str(a("mode", "unlit")).lower(), 0)
        elif n == "wire":
            flags |= 1 if a("above", False) else 0
        rec = bytearray(struct.pack("<BBhhHHH", t, 0, x, y, w, h, int(e.get("id", 0)) & 0xFFFF))
        rec.append(len(nodes))
        for nx, ny in nodes:
            rec += struct.pack("<hh", nx, ny)
        rec.append(flags)
        rec.append(len(vals))
        for v in vals:
            rec += struct.pack("<H", v)
        assert len(rec) < 256, (n, len(rec))
        rec[1] = len(rec)
        return bytes(rec)

    def solid_at(self, gx, gy, _sub=0):
        c = self.solid_char(gx, gy)
        return c is not None and not is_air(c)

    def block_tiles(self, r, x, y, w, h, c, blend):
        """An autotiled block's tiles (row-major, w/8 x h/8): GenerateBox, or GenerateOverlay over the map's solids
        (blending with what is around)."""
        tw, th = max(1, w // 8), max(1, h // 8)
        TX, TY, TW, TH = self.ch.tb
        gx, gy = (r.x + x) // 8 - TX, (r.y + y) // 8 - TY
        if blend:
            beh = T.Behaviour(True, True, True)
            got = autotile_rect(self.fgsets, self.solid_char, (gx, gy, tw, th), c, beh, self.ch.level_bounds, self.rnd)
        else:
            beh = T.Behaviour(False, False, False)
            got = autotile_rect(self.fgsets, lambda X, Y: None, (0, 0, tw, th), c, beh, [], self.rnd)
            got = {(gx + k[0], gy + k[1]): v for k, v in got.items()}
        img = self.image(self.fgsets[c].path)
        out = []
        for j in range(th):
            for i in range(tw):
                q = got.get((gx + i, gy + j))
                if q is None:
                    out.append(0)
                else:
                    qx, qy = q
                    out.append(self.tiles.add(self.pal.map(img[qy * 8:qy * 8 + 8, qx * 8:qx * 8 + 8])))
        return out


EVENTS = {}
GRID_MAX = [0]
MISSING = collections.Counter()


ZX0_CACHE = {}


def zx0(data):
    """data packed with ZX0 (the CE's zx0_Decompress unpacks it), by the CE toolchain's convbin."""
    import hashlib
    import subprocess
    import tempfile
    key = hashlib.sha1(data).hexdigest()
    cache = os.path.join(os.environ["CELESTE_CACHE"], "zx0")
    os.makedirs(cache, exist_ok=True)
    path = os.path.join(cache, key)
    if os.path.exists(path):
        return open(path, "rb").read()
    cedev = os.environ.get("CEDEV", os.path.expanduser("~/Documents/Assets/toolchains/CEdev"))
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, "in.bin"), os.path.join(d, "out.bin")
        open(src, "wb").write(data)
        subprocess.run([os.path.join(cedev, "bin", "convbin"), "-l", "0", "-j", "bin", "-k", "bin", "-c", "zx0", "-i", src,
                        "-o", dst], check=True)
        out = open(dst, "rb").read()
    open(path, "wb").write(out)
    return out


def decal_base(path):
    p = "decals/" + path.replace("\\", "/")
    if p.endswith(".png"):
        p = p[:-4]
    return p


def decal_family(p):
    base = re.sub(r"\d+$", "", p)
    fam = GAMEPLAY.prefix(base) if not p.startswith("decals/6-reflection/secret_") else []
    if not fam:
        fam = family(p)
    return fam


def main():
    os.makedirs(OUT, exist_ok=True)
    H[:] = ["/* Generated by tools/pack_ce.py: the ids of the appvars' contents. */", "#ifndef GEN_H", "#define GEN_H"]
    common = Common()
    common.build()
    for name, number in CHAPTERS:
        Chapter(name, number, common).build()
    for i, n in enumerate(ENTITY_TYPES):
        H.append(f"#define ET_{cname(n)} {i}")
    for i, n in enumerate(TRIGGER_TYPES):
        H.append(f"#define TT_{cname(n)} {i}")
    for n, i in EVENTS.items():
        H.append(f"#define EV_{cname(n)} {i}")
    H.append(f"#define FORMAT {FORMAT}")
    H.append(f"#define GRID_MAX {GRID_MAX[0]}   /* the biggest room's grids */")
    H.append(f"#define MARGIN {MARGIN}")
    H.append("#endif")
    open(GEN_H, "w").write("\n".join(H) + "\n")
    if MISSING:
        print("not in the CE version:", dict(MISSING))


if __name__ == "__main__":
    main()
