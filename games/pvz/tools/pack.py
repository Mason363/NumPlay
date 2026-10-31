#!/usr/bin/env python3
"""Plants vs. Zombies' pictures and fonts -> src/data.bin + src/data.h.

    python3 -I tools/pack.py REF        (REF: the folder tools/fetch.py filled)

The game is drawn at 0.4 of its own size: its 800 x 600 screen becomes the calculator's 320 x 240.

data.bin: "PVZ1", the offsets (u32) of its 6 tables, the number of animations, frames, and the file's size (u32 x 3),
then the tables (offsets are from the start of the file):
- ANIMS: 12 bytes each (A_* and I_* in data.h): first frame, palette, the time a loop takes in hundredths of a
  second, the animation it is drawn over (u16 x 4; 0xFFFF none: an overlay holds only what differs from the
  same frame of that one), frames, bits a pixel (u8, u8: 4 or 8), 0 (u16)
- FRAMES: 12 bytes each: x, y of the top-left from the anchor (s16, s16), w, h (u16, u16), offset (u32) of its rows.
  4 bits a pixel: each row's length in bytes (u8 x h), then the rows; 8 bits: each row's offset (u16 x h, from
  the first), then the rows. A row is runs up to its last visible pixel: 0x00-0x3F n+1 transparent, 0x40-0x7F
  n-63 opaque pixels, 0x80-0xBF n-127 half-transparent ones (their colours follow: 4 bits a pixel, the first
  lowest, padded to a byte, or a byte each), 0xC0-0xFF one opaque colour repeated: with 4 bits 2 + bits 4-5 of
  n times colour n & 15, with 8 bits n-190 times the colour that follows
- PALETTES: the offset (u32) of each: the number of colours (u16), 0 (u16), then the colours (RGB565)
- BACKS: 16 bytes each (BG_*): w, h, colours, rows a chunk, palette, chunks (u16 x 6), the offset of the chunks'
  offsets (u32). Each chunk (one offset more for the end) is w x rows colour indices, raw DEFLATE
- FONTS: the offset (u32) of each (F_*): first character, count, ascent, descent (u8 x 4), then 8 bytes a glyph
  (w, h, x, y, advance, 0, its data's offset u16 from the glyphs' end): 4-bit coverage, 2 pixels a byte
- BOSS: Dr. Zomboss's Zombot as the game draws it, from parts the .reanim moves: the parts' table and the 4 rigs'
  offsets (u32 x 5). Parts: their number (u32), then 12 bytes each: w, h, palette (u16 x 3), 0 (u16), its pixels'
  offset (u32): rows of 4-bit colour indices (0 transparent), the first pixel lowest. A rig (the Zombot, its driver,
  the fire ball, the ice ball): tracks, frames kept, the frames' step (from the .reanim's), the track the driver is
  fixed to (u16 x 4), then a flag for each track (u8: 1 the arm and RV the attacks move), padded to 4, then for each
  frame its offset (u32): which tracks show (a bit each), then their poses, 14 bytes each: x, y in eighths of a pixel,
  scales in 4096ths, skews in sixteenths of a degree (s16 x 6), the part, the opacity (u8 x 2)
"""
import json
import os
import struct
import sys
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFont

try:
    import zopfli.zlib as zopfli
except ImportError:  # (plain zlib then: a few percent bigger)
    zopfli = None

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_BIN = os.path.join(HERE, "..", "src", "data.bin")
OUT_H = os.path.join(HERE, "..", "src", "data.h")
S = 0.4
NONE = 0xFFFF

# The animations of the game's characters, from its sheets: how many frames to keep (evenly spaced, flash is what
# limits it) and, for one that only differs from another by a hat or a tube, the one it is drawn over.
DEFAULT_KEEP = 8
ANIMS = {
    # zombies: a headless body, and the heads and hats over it
    "zombie-headless": (12, None), "zombie-walk": (12, "zombie-headless"), "cone-walk": (12, "zombie-headless"),
    "bucket-walk": (12, "zombie-headless"), "flag-walk": (10, None), "zombie-eat": (8, None),
    "cone-eat": (8, "zombie-eat"), "bucket-eat": (8, "zombie-eat"), "flag-eat": (7, None),
    "zombie-die": (8, None), "zombie-burn": (6, None),
    "pole-headless": (10, None), "pole-walk": (10, "pole-headless"), "pole-run": (10, None), "pole-jump": (8, None),
    "pole-land": (5, None), "pole-eat": (7, None), "pole-die": (7, None), "pole-burn": (6, None),
    "newspaper-headless": (8, None), "newspaper-walk": (8, "newspaper-headless"),
    "newspaper-mad-walk": (8, "newspaper-headless"), "newspaper-mad-eat": (7, None),
    "newspaper-eat": (7, "newspaper-mad-eat"), "newspaper-gasp": (6, None), "newspaper-die": (7, None),
    "newspaper-burn": (6, None),
    "door-walk": (8, None), "door-eat": (7, None),
    "football-headless": (8, None), "football-walk": (8, "football-headless"),
    "football-bare-walk": (8, "football-headless"), "football-bare-eat": (7, None),
    "football-eat": (7, "football-bare-eat"), "football-die": (7, None),
    "dancer-walk": (8, None), "dancer-eat": (7, None), "dancer-summon": (7, None), "dancer-headless": (8, None),
    "dancer-die": (7, None), "dancer-burn": (6, None),
    "backup-walk": (8, None), "backup-eat": (7, None), "backup-headless": (8, None), "backup-die": (7, None),
    "backup-burn": (6, None),
    "ducky1-walk": (8, None), "ducky2-walk": (8, "ducky1-walk"), "ducky3-walk": (8, "ducky1-walk"),
    "ducky1-eat": (7, None), "ducky2-eat": (7, "ducky1-eat"), "ducky3-eat": (7, "ducky1-eat"), "ducky-die": (7, None),
    "snorkel-walk": (8, None), "snorkel-swim": (4, None), "snorkel-eat": (7, None), "snorkel-die": (7, None),
    "zomboni-walk": (4, None), "zomboni-damaged": (3, None), "zomboni-critical": (3, None), "zomboni-die": (4, None),
    "bobsled-walk": (8, None), "bobsled-eat": (7, None), "bobsled-die": (7, None),
    "dolphin-walk": (8, None), "dolphin-ride": (4, None), "dolphin-jump": (8, None), "dolphin-swim": (8, None),
    "dolphin-eat": (7, None), "dolphin-die": (6, None),
    "jack-walk": (8, None), "jack-eat": (7, None), "jack-open": (8, None), "jack-headless": (8, None),
    "jack-die": (7, None),
    "balloon-fly": (8, None), "balloon-pop": (8, None), "balloon-walk": (8, None), "balloon-eat": (7, None),
    "balloon-die": (7, None),
    "digger-dig": (6, None), "digger-rise": (8, None), "digger-walk": (8, None), "digger-eat": (7, None),
    "digger-die": (7, None),
    "pogo-bounce": (8, None), "pogo-walk": (8, None), "pogo-eat": (7, None), "pogo-die": (7, None),
    "bungee-drop": (4, None), "bungee-idle": (8, None), "bungee-die": (6, None),
    "ladder-bare": (8, None), "ladder-walk": (8, "ladder-bare"), "ladder-place": (6, None), "ladder-eat": (7, None),
    "ladder-die": (7, None),
    "catapult-walk": (3, None), "catapult-idle": (3, None), "catapult-shoot": (6, None), "catapult-die": (3, None),
    "gargantuar-bare-walk": (6, None), "gargantuar-walk": (6, "gargantuar-bare-walk"),
    "gargantuar-bare-smash": (5, None), "gargantuar-smash": (5, "gargantuar-bare-smash"), "gargantuar-throw": (5, None),
    "gargantuar-bare-die": (5, None), "gargantuar-die": (5, "gargantuar-bare-die"),
    "imp-thrown": (6, None), "imp-land": (4, None), "imp-walk": (8, None), "imp-eat": (7, None), "imp-die": (7, None),
    # plants
    "peashooter": (10, None), "sunflower": (10, None), "cherrybomb": (8, None), "wallnut": (8, None),
    "wallnut-cracked1": (8, "wallnut"), "wallnut-cracked2": (8, "wallnut"), "wallnut-roll": (8, None),
    "explodenut": (4, None), "potatomine": (6, None), "potatomine-unarmed": (1, None), "snowpea": (8, None),
    "chomper": (8, None), "chomper-bite": (9, None), "chomper-digest": (6, None), "repeater": (8, None),
    "puffshroom": (8, None), "sunshroom": (8, None), "sunshroom-big": (8, None), "fumeshroom": (8, None),
    "fumeshroom-attack": (1, None), "fume-cloud": (6, None), "gravebuster": (8, None), "hypnoshroom": (8, None),
    "scaredyshroom": (8, None), "scaredyshroom-scared": (6, None), "iceshroom": (8, None), "doomshroom": (8, None),
    "lilypad": (1, None), "squash": (8, None), "squash-attack": (6, None), "threepeater": (8, None),
    "tanglekelp": (8, None), "tanglekelp-grab": (1, None), "jalapeno": (8, None), "jalapeno-fire": (6, None),
    "spikeweed": (8, None), "torchwood": (8, None), "tallnut": (8, None), "tallnut-cracked1": (8, "tallnut"),
    "tallnut-cracked2": (8, "tallnut"), "seashroom": (8, None), "plantern": (8, None), "cactus": (8, None),
    "cactus-tall": (8, None), "blover": (8, None), "blover-loop": (6, None), "splitpea": (8, None),
    "starfruit": (8, None), "pumpkin": (8, None), "pumpkin-cracked1": (8, "pumpkin"),
    "pumpkin-cracked2": (8, "pumpkin"), "magnetshroom": (8, None), "magnetshroom-attack": (8, None),
    "cabbagepult": (8, None), "cabbagepult-attack": (8, None), "flowerpot": (6, None), "kernelpult": (8, None),
    "kernelpult-attack": (8, None), "coffeebean": (8, None), "garlic": (6, None), "garlic-cracked1": (6, "garlic"),
    "garlic-cracked2": (6, "garlic"), "umbrella": (8, None), "umbrella-block": (4, None), "marigold": (8, None),
    "melonpult": (8, None), "melonpult-attack": (8, None),
    "sun": (9, None),
}
# the plants, in the Almanac's order: their seed packets' pictures and their Almanac portraits
PLANTS = ["peashooter", "sunflower", "cherrybomb", "wallnut", "potatomine", "snowpea", "chomper", "repeater",
          "puffshroom", "sunshroom", "fumeshroom", "gravebuster", "hypnoshroom", "scaredyshroom", "iceshroom",
          "doomshroom", "lilypad", "squash", "threepeater", "tanglekelp", "jalapeno", "spikeweed", "torchwood",
          "tallnut", "seashroom", "plantern", "cactus", "blover", "splitpea", "starfruit", "pumpkin", "magnetshroom",
          "cabbagepult", "flowerpot", "kernelpult", "coffeebean", "garlic", "umbrella", "marigold", "melonpult"]
ZOMBIES = ["normal", "flag", "cone", "pole", "bucket", "newspaper", "door", "football", "dancer", "backup", "ducky",
           "snorkel", "zomboni", "bobsled", "dolphin", "jack", "balloon", "digger", "pogo", "bungee", "ladder",
           "catapult", "gargantuar", "imp"]


def deflate(raw):
    """raw DEFLATE"""
    if zopfli:
        return zopfli.compress(bytes(raw), numiterations=15)[2:-4]
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    return c.compress(raw) + c.flush()


def rgb565(c):
    r, g, b = (int(round(min(255, max(0, v)))) for v in c[:3])
    return (r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3


def premul_resize(img, size):
    """RGBA image -> float RGBA (straight alpha, 0..1) at size, resampled with premultiplied colours"""
    a = np.asarray(img.convert("RGBA")).astype(np.float32) / 255
    a[..., :3] *= a[..., 3:4]
    chans = [np.asarray(Image.fromarray(a[..., k]).resize(size, Image.LANCZOS)) for k in range(4)]
    out = np.clip(np.stack(chans, -1), 0, 1)
    al = out[..., 3:4]
    out[..., :3] = np.where(al > 1e-3, out[..., :3] / np.maximum(al, 1e-3), 0)
    return np.clip(out, 0, 1)


def nearest(pts, pal):
    w = np.array([0.30, 0.59, 0.11], np.float32) * 3   # (green counts most)
    d = (((pts[:, None, :] - pal[None, :, :]) ** 2) * w).sum(-1)
    return d.argmin(1)


def kmeans_palette(pixels, n, iters=6):
    """pixels: (N, 3) 0..255 -> palette (k <= n, 3)"""
    if len(pixels) == 0:
        return np.zeros((1, 3), np.float32)
    uniq = np.unique(pixels.astype(np.uint8), axis=0)
    if len(uniq) <= n:
        return uniq.astype(np.float32)
    q = Image.fromarray(pixels.astype(np.uint8).reshape(-1, 1, 3), "RGB").quantize(n, method=Image.MEDIANCUT,
                                                                                  dither=Image.NONE)
    pal = np.array(q.getpalette()[:3 * n], np.float32).reshape(-1, 3)
    pts = pixels.astype(np.float32)
    if len(pts) > 60000:
        pts = pts[np.random.default_rng(1).choice(len(pts), 60000, replace=False)]
    for _ in range(iters):
        idx = nearest(pts, pal)
        for k in range(len(pal)):
            m = idx == k
            if m.any():
                pal[k] = pts[m].mean(0)
    return pal


def index_pixels(rgb, pal):
    flat = rgb.reshape(-1, 3).astype(np.float32)
    out = np.empty(len(flat), np.int32)
    for i in range(0, len(flat), 20000):
        out[i:i + 20000] = nearest(flat[i:i + 20000], pal)
    return out.reshape(rgb.shape[:2])


def alpha_levels(arr):
    al = arr[..., 3]
    return np.where(al < 0.25, 0, np.where(al < 0.75, 1, 2))


# PVZ_OPTIMAL_4BIT_RLE_V1: byte-minimal runs, unchanged decoder/pixel values.
def _pvz_optimal_row4(idx, alpha):
    """Minimum-byte encoding of one 4-bit row. Ties use fewer instructions."""
    px = [(int(alpha[i]), int(idx[i]) if int(alpha[i]) else 0)
          for i in range(len(alpha))]
    end = len(px)
    while end and px[end - 1][0] == 0:
        end -= 1
    best = [(10**9, 10**9)] * (end + 1)
    best[end] = (0, 0)
    choice = [None] * end
    for x in range(end - 1, -1, -1):
        vis, v = px[x]
        score = (10**9, 10**9)
        picked = None

        def consider(n, size, kind):
            nonlocal score, picked
            rest = best[x + n]
            candidate = (size + rest[0], 1 + rest[1])
            if candidate < score or (candidate == score and picked is not None and n > picked[0]):
                score, picked = candidate, (n, kind)

        if vis == 0:
            for n in range(1, min(64, end - x) + 1):
                if px[x + n - 1][0] != 0:
                    break
                consider(n, 1, 'skip')
        else:
            for n in range(1, min(64, end - x) + 1):
                if px[x + n - 1][0] != vis:
                    break
                consider(n, 1 + (n + 1) // 2, 'literal')
            if vis == 2:
                for n in range(2, min(5, end - x) + 1):
                    if px[x + n - 1] != (2, v):
                        break
                    consider(n, 1, 'repeat')
        if picked is None:
            raise ValueError('4-bit RLE encoder could not represent a row')
        best[x], choice[x] = score, picked
    out = bytearray()
    x = 0
    while x < end:
        n, kind = choice[x]
        if kind == 'skip':
            out.append(n - 1)
        elif kind == 'repeat':
            out.append(0xC0 | ((n - 2) << 4) | px[x][1])
        else:
            out.append((0x40 if px[x][0] == 2 else 0x80) + n - 1)
            for j in range(0, n, 2):
                lo = px[x + j][1]
                hi = px[x + j + 1][1] if j + 1 < n else 0
                out.append(lo | (hi << 4))
        x += n
    if len(out) != best[0][0] or len(out) > 255:
        raise ValueError("optimal row exceeds one-byte length limit")
    return bytes(out)


def encode_rows(idx, alpha, bpp):
    """idx: (h, w) colour indices, alpha: (h, w) 0 transparent, 1 half, 2 opaque -> the frame's rows"""
    h, w = idx.shape
    if bpp == 4:
        # PVZ_OPTIMAL_4BIT_RLE_V1: use the existing graphics opcodes optimally.
        rows = [_pvz_optimal_row4(idx[y], alpha[y]) for y in range(h)]
        return bytes(len(r) for r in rows) + b''.join(rows)
    rows = []
    maxrun = 5 if bpp == 4 else 65

    def lits(vals):
        if bpp == 8:
            return bytes(int(v) for v in vals)
        vals = [int(v) for v in vals] + [0]
        return bytes(vals[i] | vals[i + 1] << 4 for i in range(0, len(vals) - 1, 2))
    for y in range(h):
        out = bytearray()
        a, ix = alpha[y], idx[y]
        last = w
        while last > 0 and a[last - 1] == 0:
            last -= 1
        x = 0
        while x < last:
            if a[x] == 0:
                n = 1
                while x + n < last and a[x + n] == 0 and n < 64:
                    n += 1
                out.append(n - 1)
                x += n
            elif a[x] == 2:
                n = 1
                while x + n < last and a[x + n] == 2 and ix[x + n] == ix[x] and n < maxrun:
                    n += 1
                if n >= (2 if bpp == 4 else 3):
                    if bpp == 4:
                        out.append(0xC0 | (n - 2) << 4 | int(ix[x]))
                    else:
                        out += bytes([0xC0 + n - 2, int(ix[x])])
                    x += n
                    continue
                n = 1
                k = 2 if bpp == 4 else 3
                while x + n < last and a[x + n] == 2 and n < 64:
                    if x + n + k <= last and all(a[x + n + j] == 2 and ix[x + n + j] == ix[x + n] for j in range(k)):
                        break
                    n += 1
                out.append(0x40 + n - 1)
                out += lits(ix[x:x + n])
                x += n
            else:
                n = 1
                while x + n < last and a[x + n] == 1 and n < 64:
                    n += 1
                out.append(0x80 + n - 1)
                out += lits(ix[x:x + n])
                x += n
        rows.append(bytes(out))
    if bpp == 4:
        if max((len(r) for r in rows), default=0) > 255:
            raise SystemExit("a row too long for 4 bits a pixel")
        return bytes(len(r) for r in rows) + b"".join(rows)
    data = bytearray()
    offs = []
    for r in rows:
        offs.append(2 * h + len(data))
        data += r
    if 2 * h + len(data) > 65535:
        raise SystemExit("a frame too big for its row offsets")
    return b"".join(struct.pack("<H", o) for o in offs) + bytes(data)


def trim(arr, x0, y0):
    """crops a frame to its visible pixels"""
    ys, xs = np.nonzero(arr[..., 3] >= 0.25)
    if len(ys) == 0:
        return np.zeros((1, 1, 4), np.float32), 0, 0
    y1, y2, x1, x2 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
    return arr[y1:y2, x1:x2], x0 + x1, y0 + y1


def overlay(arr, x0, y0, base, bx0, by0):
    """what of a frame differs from the base's same frame: drawn over it, gives the frame"""
    h, w = arr.shape[:2]
    bh, bw = base.shape[:2]
    b = np.zeros_like(arr)
    # the base's pixels where the frame's are
    sx, sy = bx0 - x0, by0 - y0
    ya, yb = max(0, sy), min(h, sy + bh)
    xa, xb = max(0, sx), min(w, sx + bw)
    if ya < yb and xa < xb:
        b[ya:yb, xa:xb] = base[ya - sy:yb - sy, xa - sx:xb - sx]
    la, lb = alpha_levels(arr), alpha_levels(b)
    diff = np.abs(arr[..., :3] - b[..., :3]).sum(-1) * 255
    keep = (la > 0) & ((la != lb) | (diff > 36))
    out = arr.copy()
    out[..., 3] = np.where(keep, out[..., 3], 0)
    return out


class Pack:
    def __init__(self):
        self.anims = []      # (key, first frame, frames, palette, cycle, base, bpp)
        self.frames = []     # (x, y, w, h, data)
        self.pals = []       # lists of RGB565
        self.backs = []      # (key, w, h, colours, rows, palette, chunks)
        self.fonts = []      # (key, bytes)
        self.names = {}
        self.scaled = {}     # name -> its frames before trimming (for overlays)
        self.boss = b""

    def palette(self, pal):
        self.pals.append([rgb565(c) for c in pal])
        return len(self.pals) - 1

    def add_anim(self, key, frames, cycle=0, ncolors=16, base=None):
        """frames: (float RGBA array, x0, y0) at the final scale, (x0, y0) its top-left from the anchor"""
        bpp = 4 if ncolors <= 16 else 8
        if base is not None:
            bfr = self.scaled[base]
            frames = [(overlay(a, x, y, *bfr[i]), x, y) for i, (a, x, y) in enumerate(frames)]
        trimmed = [trim(*f) for f in frames]
        px = np.concatenate([t[0][t[0][..., 3] >= 0.25][:, :3] * 255 for t in trimmed])
        pal = kmeans_palette(px, ncolors)
        p = self.palette(pal)
        first = len(self.frames)
        for arr, x0, y0 in trimmed:
            h, w = arr.shape[:2]
            idx = index_pixels(arr[..., :3] * 255, pal)
            self.frames.append((int(x0), int(y0), w, h, encode_rows(idx, alpha_levels(arr), bpp)))
        self.names[key] = len(self.anims)
        self.anims.append((key, first, len(trimmed), p, int(round(cycle)), NONE if base is None else self.names[base], bpp))
        self.scaled[key] = frames if base is None else None
        return self.names[key]

    def add_image(self, name, arr, x0=0, y0=0, ncolors=16):
        return self.add_anim("I_" + name.upper().replace("-", "_"), [(arr, x0, y0)], 0, ncolors)

    def add_back(self, name, img, ncolors=128, rows=16, shared=None):
        """shared: (palette, its index) from an earlier background, for pictures drawn into each other"""
        img = img.convert("RGB")
        w, h = img.size
        if shared:
            pal, p = shared
        else:
            px = np.asarray(img).reshape(-1, 3).astype(np.float32)
            pal = kmeans_palette(px, ncolors, iters=5)
            p = self.palette(pal)
        idx = index_pixels(np.asarray(img), pal).astype(np.uint8)
        self.last_palette = (pal, p)
        chunks = [deflate(idx[y:y + rows].tobytes()) for y in range(0, h, rows)]
        key = "BG_" + name.upper().replace("-", "_")
        self.names[key] = len(self.backs)
        self.backs.append((key, w, h, len(pal), rows, p, chunks))

    def add_font(self, name, path, cap, weight=None, spacing=0, first=32, last=126):
        f = load_font(path, cap, weight)
        glyphs, data = [], bytearray()
        asc = desc = 0
        for c in range(first, last + 1):
            g, adv = render_glyph(f, chr(c), spacing)
            if g is None:
                glyphs.append((0, 0, 0, 0, adv, len(data)))
                continue
            q, ox, oy = g
            h, w = q.shape
            asc, desc = max(asc, -oy), max(desc, h + oy)
            flat = q.flatten().tolist()
            if len(flat) % 2:
                flat.append(0)
            off = len(data)
            for i in range(0, len(flat), 2):
                data.append(flat[i] | flat[i + 1] << 4)
            glyphs.append((w, h, ox, oy, adv, off))
        blob = struct.pack("<4B", first, last - first + 1, asc, desc)
        for w, h, ox, oy, adv, off in glyphs:
            blob += struct.pack("<BBbbBBH", w, h, ox, oy, adv, 0, off)
        blob += bytes(data)
        key = "F_" + name.upper()
        self.names[key] = len(self.fonts)
        self.fonts.append((key, blob))
        print(f"font {key}: cap {cap}, ascent {asc}, descent {desc}, {len(blob)} bytes")

    def write(self):
        out = bytearray(b"PVZ1" + bytes(4 * 9))

        def align():
            while len(out) % 4:
                out.append(0)
        tables = [len(out)]
        for key, first, n, p, cycle, base, bpp in self.anims:
            out += struct.pack("<HHHHBBH", first, p, min(cycle, 65535), base, n, bpp, 0)
        align()
        tables.append(len(out))
        frame_at = len(out)
        out += bytes(12 * len(self.frames))
        # PVZ_LOSSLESS_FRAME_SHARING_V1: share identical 4-bit frame payloads.
        # Frame positions and palettes remain independent; no pixel data changes.
        frame_bpp = [0] * len(self.frames)
        for _, first, count, _, _, _, bits in self.anims:
            for fi in range(first, first + count):
                if frame_bpp[fi] not in (0, bits):
                    raise ValueError("frame used with different pixel formats")
                frame_bpp[fi] = bits
        if any(bits not in (4, 8) for bits in frame_bpp):
            raise ValueError("frame pixel format missing")
        # PVZ_SHARED_RLE_ROWS_V1: rows deduplicated from earlier physical frames.
        # High bit of a 4-bit frame's offset flags 255 row-length markers.
        row_bank = {}
        shared_4bit = {}
        for i, (x, y, w, h, data) in enumerate(self.frames):
            key = (w, h, data) if frame_bpp[i] == 4 else None
            # The 8-bit decoder can scan a frame's final row up to the next
            # payload. Do not change the physical successor of an 8-bit frame.
            after_8bit = i > 0 and frame_bpp[i - 1] == 8
            if key is not None and key in shared_4bit and not after_8bit:
                frame_off = shared_4bit[key]
            else:
                if len(out) % 2:
                    out.append(0)
                frame_off = len(out)
                if frame_bpp[i] == 4:
                    if len(data) < h or h + sum(data[:h]) != len(data):
                        raise ValueError("bad 4-bit frame row lengths")
                    sizes = data[:h]
                    can_share_rows = not after_8bit and all(n < 255 for n in sizes)
                    lengths = bytearray()
                    bodies = bytearray()
                    src = h
                    has_refs = False
                    for n in sizes:
                        row = data[src:src + n]
                        src += n
                        if can_share_rows and n > 4 and row in row_bank:
                            ptr = row_bank[row]
                            if ptr >= (1 << 24):
                                raise ValueError("row reference offset exceeds 24 bits")
                            lengths.append(255)
                            bodies += struct.pack("<I", (n << 24) | ptr)
                            has_refs = True
                        else:
                            lengths.append(n)
                            if n > 4:
                                row_bank.setdefault(row, frame_off + h + len(bodies))
                            bodies += row
                    out += lengths + bodies
                    if has_refs:
                        frame_off |= 0x80000000
                else:
                    out += data
                if key is not None:
                    shared_4bit.setdefault(key, frame_off)
            struct.pack_into("<hhHHI", out, frame_at + 12 * i, x, y, w, h, frame_off)
        align()
        tables.append(len(out))
        pal_at = len(out)
        out += bytes(4 * len(self.pals))
        for i, pal in enumerate(self.pals):
            align()
            struct.pack_into("<I", out, pal_at + 4 * i, len(out))
            out += struct.pack("<HH", len(pal), 0) + b"".join(struct.pack("<H", c) for c in pal)
        align()
        tables.append(len(out))
        back_at = len(out)
        out += bytes(16 * len(self.backs))
        for i, (key, w, h, ncol, rows, p, chunks) in enumerate(self.backs):
            align()
            first = len(out)
            out += bytes(4 * (len(chunks) + 1))
            for j, c in enumerate(chunks):
                struct.pack_into("<I", out, first + 4 * j, len(out))
                out += c
            struct.pack_into("<I", out, first + 4 * len(chunks), len(out))
            struct.pack_into("<HHHHHHI", out, back_at + 16 * i, w, h, ncol, rows, p, len(chunks), first)
        align()
        tables.append(len(out))
        font_at = len(out)
        out += bytes(4 * len(self.fonts))
        for i, (key, blob) in enumerate(self.fonts):
            align()
            struct.pack_into("<I", out, font_at + 4 * i, len(out))
            out += blob
        align()
        tables.append(len(out))
        out += self.boss
        align()
        struct.pack_into("<6I3I", out, 4, *tables, len(self.anims), len(self.frames), len(out))
        with open(OUT_BIN, "wb") as f:
            f.write(out)
        lines = ["/* Written by tools/pack.py: what data.bin holds. */", "#ifndef PVZ_DATA_H", "#define PVZ_DATA_H",
                 ""]
        for key, *_ in self.anims:
            lines.append(f"#define {key} {self.names[key]}")
        lines += [f"#define N_ANIMS {len(self.anims)}", ""]
        for key, *_ in self.backs:
            lines.append(f"#define {key} {self.names[key]}")
        lines.append("")
        for key, _ in self.fonts:
            lines.append(f"#define {key} {self.names[key]}")
        lines += ["", "#endif", ""]
        with open(OUT_H, "w") as f:
            f.write("\n".join(lines))
        return len(out)


def load_font(path, cap, weight):
    up = 4
    for size in range(4, 200):
        f = ImageFont.truetype(path, size * up)
        if weight:
            f.set_variation_by_axes([weight])
        bb = f.getbbox("H", anchor="ls")
        if -bb[1] >= cap * up - up // 2:
            return f
    raise SystemExit("font size")


def render_glyph(f, ch, spacing):
    up = 4
    adv = (f.getlength(ch) + spacing * up / 4) / up
    bb = f.getbbox(ch, anchor="ls")
    if bb[2] <= bb[0] or bb[3] <= bb[1]:
        return None, round(adv)
    pad = 2 * up
    x0, y0 = pad - bb[0], pad - bb[1]
    x0 += (-x0) % up
    y0 += (-y0) % up
    w, h = x0 + bb[2] + pad, y0 + bb[3] + pad
    w += (-w) % up
    h += (-h) % up
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).text((x0, y0), ch, font=f, fill=255, anchor="ls")
    a = np.asarray(im).astype(np.float32) / 255
    a = a.reshape(h // up, up, w // up, up).mean(axis=(1, 3))
    q = np.clip(np.round(a * 15), 0, 15).astype(np.uint8)
    ys, xs = np.nonzero(q)
    if not len(ys):
        return None, round(adv)
    q = q[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    return (q, xs.min() - x0 // up, ys.min() - y0 // up), round(adv)


class Atlas:
    """the characters' animations, as the sheets hold them"""
    def __init__(self, ref):
        self.ref = ref
        self.d = json.load(open(os.path.join(ref, "pvz", "original", "animations.json")))["animations"]
        self.pages = {}

    def page(self, i):
        if i not in self.pages:
            self.pages[i] = Image.open(os.path.join(self.ref, "pvz", "original", f"actors-{i}.png")).convert("RGBA")
        return self.pages[i]

    def frames(self, name, keep, scale=S):
        """the kept frames of an animation at scale, each with its top-left from the anchor"""
        a = self.d[name]
        fr = a["frames"]
        n = len(fr)
        k = min(n, keep)
        picks = [int(i * n / k) for i in range(k)]
        ax, ay = a["anchor"]
        step = 1 / scale
        # one canvas for every frame, its corner on the grid of the anchor that the scale keeps whole, so frames don't
        # jitter
        cx0 = ax - np.ceil((ax - min(f[5] for f in fr)) / step) * step
        cy0 = ay - np.ceil((ay - min(f[6] for f in fr)) / step) * step
        W = np.ceil((max(f[5] + f[3] for f in fr) - cx0) / step) * step
        H = np.ceil((max(f[6] + f[4] for f in fr) - cy0) / step) * step
        sw, sh = int(round(W * scale)), int(round(H * scale))
        out = []
        for i in picks:
            p, x, y, w, h, ox, oy = fr[i]
            src = self.page(p).crop((x, y, x + w, y + h))
            canvas = Image.new("RGBA", (int(np.ceil(W)) + 3, int(np.ceil(H)) + 3))
            fx, fy = ox - cx0, oy - cy0
            canvas.paste(src, (int(np.floor(fx)), int(np.floor(fy))))
            dx, dy = fx - np.floor(fx), fy - np.floor(fy)
            if dx or dy:
                canvas = canvas.transform(canvas.size, Image.AFFINE, (1, 0, -dx, 0, 1, -dy), Image.BILINEAR)
            canvas = canvas.crop((0, 0, int(round(W)), int(round(H))))
            out.append((premul_resize(canvas, (sw, sh)), int(round((cx0 - ax) * scale)), int(round((cy0 - ay) * scale))))
        return out, sum(a["durations"])


def picture(ref, rel, scale=None, size=None, box=None):
    im = Image.open(os.path.join(ref, "pvz", rel)).convert("RGBA")
    if box:
        im = im.crop(box)
    w, h = im.size
    if size is None:
        size = (max(1, int(round(w * scale))), max(1, int(round(h * scale))))
    return premul_resize(im, size)


def opaque(img, size, box=None):
    im = img.convert("RGB")
    if box:
        im = im.crop(box)
    return im.resize(size, Image.LANCZOS)


def cells(ref, rel, cols, rows, n=None):
    """a sheet's pictures, left to right then down"""
    im = Image.open(os.path.join(ref, "pvz", rel)).convert("RGBA")
    w, h = im.size[0] // cols, im.size[1] // rows
    out = [im.crop((c * w, r * h, c * w + w, r * h + h)) for r in range(rows) for c in range(cols)]
    return out[:n] if n else out


def centred(img, scale, ax=0.5, ay=0.5):
    """a picture at scale, its anchor at (ax, ay) of it"""
    w, h = img.size
    sw, sh = max(1, int(round(w * scale))), max(1, int(round(h * scale)))
    return premul_resize(img, (sw, sh)), -int(round(sw * ax)), -int(round(sh * ay))


BOSS_PART = 0.16   # of the parts' pictures (twice the game's size): drawn 1.25 times bigger
BOSS_SKIP = ("MULTIPLY", "ADDITIVE", "SUPERGLOW", "HIGHLIGHT", "ICEFIRE_SHADOW")


def pack_boss(P, ref):
    """the Zombot's parts and its rigs' tracks (see BOSS above)"""
    d = json.load(open(os.path.join(ref, "pvz", "runtime", "boss", "tracks.json")))
    names = sorted({p[8] for rig in d.values() for t in rig["tracks"] for p in t["poses"] if p[8]})
    names = [n for n in names if not any(k in n for k in BOSS_SKIP)]
    part_id = {n: i for i, n in enumerate(names)}
    parts = []
    for n in names:
        im = Image.open(os.path.join(ref, "pvz", "runtime", "boss", n + ".webp")).convert("RGBA")
        w, h = im.size
        arr = premul_resize(im, (max(1, int(round(w * BOSS_PART))), max(1, int(round(h * BOSS_PART)))))
        px = arr[arr[..., 3] >= 0.5][:, :3] * 255
        pal = kmeans_palette(px, 15)
        idx = index_pixels(arr[..., :3] * 255, pal) + 1
        idx[arr[..., 3] < 0.5] = 0
        p = P.palette(np.concatenate([np.zeros((1, 3), np.float32), pal]))
        rows = b""
        for y in range(idx.shape[0]):
            r = list(idx[y]) + [0]
            rows += bytes(int(r[i]) | int(r[i + 1]) << 4 for i in range(0, idx.shape[1], 2))
        parts.append((idx.shape[1], idx.shape[0], p, rows))
    out = bytearray(bytes(4 * 5))

    def align():
        while len(out) % 4:
            out.append(0)
    struct.pack_into("<I", out, 0, len(out))
    table = len(out)
    out += struct.pack("<I", len(parts)) + bytes(12 * len(parts))
    for i, (w, h, p, rows) in enumerate(parts):
        align()
        struct.pack_into("<HHHHI", out, table + 4 + 12 * i, w, h, p, 0, len(out))
        out += rows
    for r, key in enumerate(("body", "driver", "fire", "ice")):
        rig = d[key]
        tracks = rig["tracks"]
        nf = len(tracks[0]["poses"])
        step = 3
        head = next((i for i, t in enumerate(tracks) if t["name"] == "Boss_head2"), 0xFFFF)
        align()
        struct.pack_into("<I", out, 4 + 4 * r, len(out))
        kept = (nf + step - 1) // step
        out += struct.pack("<HHHH", len(tracks), kept, step, head)
        out += bytes(1 if t["name"].startswith(("Boss_innerarm", "Boss_RV")) else 0 for t in tracks)
        align()
        offs = len(out)
        out += bytes(4 * kept)
        for f in range(kept):
            align()
            struct.pack_into("<I", out, offs + 4 * f, len(out))
            mask = bytearray((len(tracks) + 7) // 8)
            poses = b""
            for i, t in enumerate(tracks):
                x, y, sx, sy, kx, ky, fr, al, img = t["poses"][f * step]
                if not img or fr < 0 or al <= 0 or img not in part_id:
                    continue
                mask[i >> 3] |= 1 << (i & 7)
                poses += struct.pack("<hhhhhhBB", round(x * 8), round(y * 8), round(sx * 4096), round(sy * 4096),
                                     round(kx * 16), round(ky * 16), part_id[img], min(255, round(al * 255)))
            out += bytes(mask) + poses
    align()
    P.boss = bytes(out)
    print(f"boss: {len(parts)} parts, {len(out)} bytes")


def make_icon(ref, at):
    """src/icon.png: a peashooter on the lawn, a zombie's head coming (rounded by NumPlay's tools/round_icons.py)"""
    lawn = Image.open(os.path.join(ref, "pvz", "lawn.jpg")).convert("RGB")
    icon = lawn.crop((420, 250, 420 + 165, 250 + 168)).resize((55, 56), Image.LANCZOS).convert("RGBA")
    for name, scale, x, y in (("zombie-walk", 0.42, 44, 66), ("peashooter", 0.62, 19, 50)):
        fr, _ = at.frames(name, 1, scale)
        arr, x0, y0 = fr[0]
        im = Image.fromarray((np.clip(arr, 0, 1) * 255).astype(np.uint8), "RGBA")
        icon.alpha_composite(im, (x + x0, y + y0))
    out = os.path.join(HERE, "..", "src", "icon.png")
    icon.save(out)
    import subprocess
    subprocess.run([sys.executable, os.path.join(HERE, "..", "..", "..", "tools", "round_icons.py"), out], check=True)


def main(ref):
    P = Pack()
    at = Atlas(ref)
    make_icon(ref, at)
    pvz = os.path.join(ref, "pvz")
    for name, (keep, base) in ANIMS.items():
        frames, cycle = at.frames(name, keep)
        P.add_anim("A_" + name.upper().replace("-", "_"), frames, cycle,
                   base=None if base is None else "A_" + base.upper().replace("-", "_"))
    n_chars = len(P.frames)
    chars = sum(len(f[4]) for f in P.frames)

    # the plants on their seed packets (half their size on the lawn), and in the Almanac
    for name in PLANTS:
        fr, _ = at.frames(name, 1, S * 0.5)
        P.add_image("packet-" + name, *trim(*fr[0]))
    for name in PLANTS:
        fr, _ = at.frames(name, 1, S * 1.6)
        P.add_image("big-" + name, *trim(*fr[0]), ncolors=48)
    for name in ZOMBIES:
        rel = "runtime/almanac/imp.webp" if name == "imp" else f"portraits/almanac/{name}.png"
        im = Image.open(os.path.join(pvz, rel)).convert("RGBA")
        bb = im.getbbox()
        im = im.crop(bb)
        P.add_image("portrait-" + name, *centred(im, 84 / im.size[1], 0.5, 1.0), ncolors=48)

    # the lawn's things, at the game's scale (these pictures are twice the game's size: 0.2)
    R = "runtime/original/"
    things = {
        "pea": ("runtime/pea.webp", 0.2), "snow-pea": (R + "snow-pea.webp", 0.2), "puff": (R + "puff.webp", 0.2),
        "star": (R + "star.webp", 0.2), "spike": (R + "spike.webp", 0.2), "cabbage": (R + "cabbage.webp", 0.2),
        "kernel": (R + "kernel.webp", 0.2), "butter": (R + "butter.webp", 0.2), "melon": (R + "melon.webp", 0.2),
        "basketball": (R + "basketball.webp", 0.2), "coin-silver": (R + "coin-silver.webp", 0.16),
        "coin-gold": (R + "coin-gold.webp", 0.16), "coin-diamond": (R + "coin-diamond.webp", 0.16),
        "bungee-target": (R + "bungee-target.webp", 0.2), "hammer": (R + "hammer.webp", 0.2),
        "meter-flag": (R + "meter-flag.webp", 0.2), "meter-head": (R + "meter-head.webp", 0.2),
    }
    for name, (rel, sc) in things.items():
        P.add_image(name, *centred(Image.open(os.path.join(pvz, rel)).convert("RGBA"), sc))
    # (anchored at the middle of their bottom)
    for name, rel, sc in [("mower", "mower", 0.2), ("pool-cleaner", "pool-cleaner", 0.2),
                          ("roof-cleaner", "roof-cleaner", 0.2), ("grave", "grave", 0.2), ("ladder", "ladder", 0.18),
                          ("sled", "sled", 0.155)]:
        P.add_image(name, *centred(Image.open(os.path.join(pvz, R + rel + ".webp")).convert("RGBA"), sc, 0.5, 1.0))
    for i, im in enumerate(cells(ref, R + "crater.webp", 2, 1)):
        P.add_image(f"crater{i}", *centred(im, 0.2, 0.5, 0.7))
    for i, im in enumerate(cells(ref, R + "vases.webp", 3, 2)):
        P.add_image(f"vase{i}", *centred(im, 0.2, 0.5, 1.0))
    for i, im in enumerate(cells(ref, R + "fog-cloud.webp", 8, 1)):
        P.add_image(f"fog{i}", *centred(im, 0.2))
    fire = [im.crop(im.getbbox()) for im in cells(ref, R + "fire-pea.webp", 6, 5, 6)]
    P.add_anim("A_FIRE_PEA", [centred(im, 0.16) for im in fire], 24)
    P.add_image("ice-trail", picture(ref, R + "ice-trail.webp", 0.2))
    P.add_image("bungee-cord", picture(ref, R + "bungee-cord.webp", 0.2))
    P.add_image("bowling-stripe", picture(ref, R + "bowling-stripe.webp", 0.2), 0, -100)

    # the screen's frame: the seed bank, the shovel, the progress meter, the conveyor belt
    P.add_image("seedbank", picture(ref, R + "seedbank.webp", 0.2), ncolors=32)
    P.add_image("shovelbank", picture(ref, R + "shovelbank.webp", 0.2))
    P.add_image("shovel", *centred(Image.open(os.path.join(pvz, R + "shovel.webp")).convert("RGBA"), 0.15))
    P.add_image("packet", picture(ref, R + "seed-packet.webp", 0.1), ncolors=32)
    P.add_image("meter-empty", picture(ref, R + "meter-empty.webp", 0.2))
    P.add_image("meter-full", picture(ref, R + "meter-full.webp", 0.2))
    P.add_image("conveyor", picture(ref, R + "conveyor-back.webp", 0.2), ncolors=16)

    # menus
    P.add_image("logo", picture(ref, R + "logo.webp", 0.2), ncolors=64)
    P.add_image("dialog", picture(ref, R + "dialog.webp", 0.2), ncolors=48)
    P.add_image("button", picture(ref, R + "button.webp", size=(100, 32)), ncolors=32)
    P.add_image("dave", picture(ref, R + "crazy-dave.webp", 0.2), ncolors=48)
    P.add_image("trophy", picture(ref, R + "trophy.webp", 0.2, box=(0, 0, 314, 538)), ncolors=48)
    P.add_image("chooser", picture(ref, "runtime/ui/seed-chooser.webp", 0.4), ncolors=32)
    P.add_image("start", picture(ref, "runtime/ui/seed-start.webp", size=(84, 22)), ncolors=16)
    P.add_image("start-off", picture(ref, "runtime/ui/seed-start-disabled.webp", size=(84, 22)), ncolors=16)
    P.add_image("card", picture(ref, "runtime/ui/almanac-card.webp", 0.4), ncolors=32)
    P.add_image("shop-slots", picture(ref, R + "shop-slots.webp", 0.4))
    P.add_image("shop-roof", picture(ref, R + "shop-roof.webp", 0.4))
    P.add_image("shop-rake", picture(ref, R + "shop-rake.webp", 0.4))
    P.add_image("shop-firstaid", picture(ref, R + "shop-firstaid.webp", 0.4))
    images = sum(len(f[4]) for f in P.frames[n_chars:])

    # the lawns, whole (the screen shows 320 pixels of their 560, from 88), and the menus' backgrounds
    lawns = {"lawn": "lawn.jpg", "yard": "yard.jpg", "night": "original/night.jpg", "pool": "original/pool.png",
             "fog": "original/fog.png", "roof": "original/roof.png"}
    # (the day's lawn and its bare yard share a palette: levels 1-1 to 1-3 roll the lawn's sod over the yard)
    day = [Image.open(os.path.join(pvz, lawns[n])).convert("RGB").resize((560, 240), Image.LANCZOS) for n in ("lawn", "yard")]
    both = np.concatenate([np.asarray(im).reshape(-1, 3) for im in day]).astype(np.float32)
    shared = kmeans_palette(both, 160, iters=5)
    shared = (shared, P.palette(shared))
    for name, rel in lawns.items():
        img = day[0] if name == "lawn" else day[1] if name == "yard" else \
            Image.open(os.path.join(pvz, rel)).resize((560, 240), Image.LANCZOS)
        P.add_back(name, img, shared=shared if name in ("lawn", "yard") else None)
    for name, rel in [("menu", R + "menu.webp"), ("award", "original/award.jpg"),
                      ("almanac", "runtime/ui/challenge-background.webp"), ("shop", R + "shop-background.webp")]:
        P.add_back(name, Image.open(os.path.join(pvz, rel)).resize((320, 240), Image.LANCZOS))
    backs = sum(sum(len(c) for c in b[6]) for b in P.backs)

    pack_boss(P, ref)
    fonts = os.path.join(ref, "fonts")
    P.add_font("big", os.path.join(fonts, "RammettoOne.ttf"), 12)
    P.add_font("head", os.path.join(fonts, "RammettoOne.ttf"), 8, spacing=1)
    P.add_font("body", os.path.join(fonts, "Nunito.ttf"), 7, 800, 1)
    P.add_font("small", os.path.join(fonts, "Nunito.ttf"), 5, 900, 2)

    for key, first, n, p, cycle, base, bpp in P.anims:
        b = sum(len(P.frames[first + i][4]) for i in range(n))
        if key.startswith("I_") and b > 3000:
            print(f"  {key:24s} {b:6d}")
    total = P.write()
    print(f"data.bin: {total} bytes: characters {chars} ({n_chars} frames), other pictures {images}, lawns and "
          f"backgrounds {backs}")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[2] == "--icon":
        make_icon(sys.argv[1], Atlas(sys.argv[1]))
        sys.exit(0)
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    main(sys.argv[1])

