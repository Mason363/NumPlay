#!/usr/bin/env python3
"""What the Five Nights at Freddy's games' CE packers share (tools/pack.py of each game): pictures in palette groups
(tools/cvq.py), fonts and small pictures of 2-bit alpha, the appvars and the header that says where things are.

    p = Pack("FNAFD")                       the appvars' names: FNAFD0, FNAFD1...
    g = p.group("office")                   a palette group
    p.pic("OFFICE", rgb, g)                 a picture (rgb: (h, w, 3) floats), maybe over a base (keep: when)
    p.define("OFFICE_W", 534)
    p.font("HUD", line, ascent, glyphs)     (the NumWorks packers' ttf_font / fnt_font)
    p.blob("CAMMAP", data)                  anything else, ASSET_CAMMAP
    p.write(ce_dir)                         data/*.bin, src/assets.h, tests/pics.crc"""
import os
import struct
import subprocess
import tempfile
import zlib

import numpy as np
from PIL import Image, ImageFilter

import cvq as V

CONVBIN = os.path.join(os.environ.get("CEDEV", os.path.expanduser("~/Documents/Assets/toolchains/CEdev")), "bin",
                       "convbin")
VAR_ROOM = 65000
FONT_DIRS = [os.path.expanduser("~/Documents/Assets/gamesrc/fonts/dejavu")]

# the fixed colours, the last 32 of every palette (UI_* in assets.h)
UI = [("BLACK", 0x000000), ("WHITE", 0xFFFFFF), ("WHITE2", 0xAAAAAA), ("WHITE1", 0x555555),
      ("GREY", 0xC8C8C8), ("GREY2", 0x858585), ("GREY1", 0x434343), ("SILVER", 0xB0B0B0), ("DIM", 0x909090),
      ("CAPTION", 0xD8E8FF), ("CAPTION2", 0x9098A8), ("CAPTION1", 0x484C55),
      ("CALL", 0xF2F2C8), ("CALL2", 0xA0A084), ("CALL1", 0x505042),
      ("MINT", 0x8FD18F), ("MINT2", 0x5E8A5E), ("MINT1", 0x2F452F),
      ("REC", 0xE02020), ("BAR_G", 0x00C000), ("BAR_Y", 0xE6E600), ("BAR_R", 0xE00000),
      ("BAR_GL", 0x66D966), ("BAR_YL", 0xEEEE66), ("BAR_RL", 0xEB6666),
      ("MAP_ON", 0x8BC34A), ("MAP_LINE", 0xD0D0D0), ("FRAME", 0xA0A0A0), ("BOX", 0x202020), ("EDGE", 0x404040),
      ("MAP_OFF", 0x424242), ("SPARE", 0x707070)]
UI_RGB = [((v >> 16) & 255, (v >> 8) & 255, v & 255) for _, v in UI]
TIGHT = V.keep_within(12, 2.0)      # a variant: kept where its source is as good as the base's own
ANIM = V.keep_within(18, 3.5)       # an animation's frame: what hardly moved


def zx0(data):
    with tempfile.TemporaryDirectory() as d:
        i, o = os.path.join(d, "i"), os.path.join(d, "o")
        open(i, "wb").write(bytes(data))
        subprocess.run([CONVBIN, "-j", "bin", "-k", "bin", "-c", "zx0", "-i", i, "-o", o, "-l", "0"], check=True)
        return open(o, "rb").read()


# ------------------------------------------------------------------ pictures
def pad2(a):
    h, w = a.shape[:2]
    return np.pad(a, ((0, h % 2), (0, w % 2), (0, 0)), mode="edge")


def pad8(a):
    h, w = a.shape[:2]
    return np.pad(a, ((0, -h % 8), (0, -w % 8), (0, 0)), mode="edge")


def resize(a, w, h):
    return np.asarray(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).resize((w, h), Image.LANCZOS)).astype(
        np.float64)


def halfv(a, sharpen=False):
    """at half height (the calculator shows each row twice); a little sharper for it when asked"""
    im = Image.fromarray(np.clip(a, 0, 255).astype(np.uint8))
    im = im.resize((im.width, (im.height + 1) // 2), Image.LANCZOS)
    if sharpen:
        im = im.filter(ImageFilter.UnsharpMask(radius=1.0, percent=50, threshold=1))
    return np.asarray(im).astype(np.float64)


def box(a, b, thresh=10):
    """the squares of 8 x 8 where a and b differ: x0, y0, x1, y1 in pixels"""
    d = np.abs(pad8(a) - pad8(b)).max(axis=2)
    h, w = d.shape
    m = d.reshape(h // 8, 8, w // 8, 8).max(axis=(1, 3)) > thresh
    ys, xs = np.nonzero(m)
    return xs.min() * 8, ys.min() * 8, (xs.max() + 1) * 8, (ys.max() + 1) * 8


def remaps(g):
    """two 256-byte tables of a group's palette: each colour darkened to 45 %, and lightened a fifth of the way to
    white: the nearest colour (the fixed ones: among the fixed ones)"""
    from scipy.spatial import cKDTree
    pal = [tuple(c) for c in g.pal]
    cols = np.array([pal[i] if i < len(pal) else (0, 0, 0) for i in range(224)] + UI_RGB, dtype=np.float64)
    sets = [list(range(len(pal))), list(range(224, 256))]
    trees = [cKDTree(np.sqrt(cols[c] / 255)) for c in sets]
    out = b""
    for f in (lambda c: c * 0.45, lambda c: c + (255 - c) * 0.2):
        t = []
        for i in range(256):
            k = 0 if i < 224 else 1
            _, j = trees[k].query(np.sqrt(np.clip(f(cols[i]), 0, 255) / 255))
            t.append(sets[k][j])
        out += bytes(t)
    return out


# ------------------------------------------------------------------ fonts and small pictures
def unnibble(nib, w, h):
    if not w:
        return np.zeros((0, 0), np.int64)
    stride = (w + 1) // 2
    a = np.zeros((h, w), np.int64)
    for r in range(h):
        for c in range(w):
            b = nib[r * stride + c // 2]
            a[r, c] = (b >> 4) if c & 1 else b & 15
    return a


def pack2(a2):
    h, w = a2.shape if a2.size else (0, 0)
    out = bytearray()
    for r in range(h):
        row = list(a2[r]) + [0, 0, 0]
        for c in range(0, w, 4):
            out.append(row[c] << 6 | row[c + 1] << 4 | row[c + 2] << 2 | row[c + 3])
    return bytes(out)


def font_blob(line, asc, glyphs):
    """line height, ascent, then 95 glyphs (32..126) of 8 bytes: advance, width, height, x, y (s8), 0, offset
    (u16, from the font's start) of rows of 2-bit alpha (4 pixels a byte, the first in the top bits). glyphs: the
    NumWorks packers' (4-bit alpha, two pixels a byte)"""
    head = struct.pack("<BB", line, asc)
    rows = b""
    table = b""
    base = 2 + 8 * 95
    for adv, w, h, x, y, nib in glyphs:
        a2 = np.clip((unnibble(nib, w, h) * 3 + 7) // 15, 0, 3)
        table += struct.pack("<BBBbbBH", adv, w, h, x, y, 0, base + len(rows))
        rows += pack2(a2)
    return head + table + rows


def img_blob(alpha):
    """a small picture of 2-bit alpha: width, height (u16), rows"""
    a2 = np.clip(np.round(np.asarray(alpha, dtype=np.float64) / 255 * 3), 0, 3).astype(np.int64)
    h, w = a2.shape
    return struct.pack("<HH", w, h) + pack2(a2)


def rgba_alpha(path, scale=None, size=None):
    """a picture's alpha (times its brightness) for img_blob"""
    t = Image.open(path).convert("RGBA")
    if scale:
        t = t.resize((int(t.width * scale), int(t.height * scale)), Image.LANCZOS)
    if size:
        t = t.resize(size, Image.LANCZOS)
    a = np.asarray(t).astype(np.float64)
    return a[..., 3] * a[..., :3].mean(axis=2) / 255


# ------------------------------------------------------------------ all of it
class Entry:
    def __init__(self, name, vq, group):
        self.name, self.vq, self.group = name, vq, group


class Pack:
    def __init__(self, prefix):
        self.prefix = prefix
        self.pics, self.groups, self.defines, self.fonts, self.blobs = [], {}, [], [], []

    def group(self, name):
        self.groups[name] = V.Group(name, 224)
        return name

    def pic(self, name, rgb, grp, base=None, keep=None):
        e = Entry(name, V.Picture(pad2(rgb), base.vq if base else None, keep), grp)
        self.groups[grp].add(e.vq)
        self.pics.append(e)
        return e

    def define(self, name, value):
        self.defines.append((name, value))

    def font(self, name, line, asc, glyphs):
        self.fonts.append((name, font_blob(line, asc, glyphs)))

    def blob(self, name, data):
        self.blobs.append((name, bytes(data)))

    def write(self, ce):
        print(f"coding {len(self.pics)} pictures in {len(self.groups)} groups", flush=True)
        for g in self.groups.values():
            g.fit()
        for e in self.pics:
            self.groups[e.group].code(e.vq, e.vq.keep)
        crcs = [zlib.crc32(self.groups[e.group].indices_of(e.vq).astype(np.uint8).tobytes()) for e in self.pics]
        os.makedirs(os.path.join(ce, "tests"), exist_ok=True)
        open(os.path.join(ce, "tests", "pics.crc"), "w").write("".join(f"{c:08x}\n" for c in crcs))
        gnames = list(self.groups)
        from concurrent.futures import ThreadPoolExecutor
        ids = {id(e.vq): i for i, e in enumerate(self.pics)}
        with ThreadPoolExecutor(os.cpu_count() or 4) as ex:   # (zx0's compressor takes its time)
            maps = list(ex.map(lambda n: zx0(self.groups[n].blob(UI_RGB) + remaps(self.groups[n])), gnames))
            pics = list(ex.map(lambda e: V.write(e.vq, ids.get(id(e.vq.base), 0), zx0), self.pics))
        blobs = [("MAP_" + n.upper(), m) for n, m in zip(gnames, maps)]
        blobs += [("PIC_" + e.name, d) for e, d in zip(self.pics, pics)]
        blobs += [("FONT_" + n, d) for n, d in self.fonts] + self.blobs
        # into appvars of 64 KB at most
        data = os.path.join(ce, "data")
        os.makedirs(data, exist_ok=True)
        for f in os.listdir(data):
            if f.startswith(self.prefix) and f.endswith(".bin"):
                os.remove(os.path.join(data, f))
        vars_, at = [bytearray()], []
        for name, d in blobs:
            if len(vars_[-1]) + len(d) > VAR_ROOM:
                vars_.append(bytearray())
            at.append((len(vars_) - 1, len(vars_[-1])))
            vars_[-1] += d
        for i, v in enumerate(vars_):
            open(os.path.join(data, f"{self.prefix}{i}.bin"), "wb").write(v)
        names = [n for n, _ in blobs]
        h = [f"/* Made by tools/pack.py: what is in the appvars {self.prefix}0, {self.prefix}1... */",
             "#ifndef CE_ASSETS_H", "#define CE_ASSETS_H", "#include <stdint.h>", "",
             f'#define DATA_VAR "{self.prefix}"', f"#define DATA_VARS {len(vars_)}", f"#define ASSET_COUNT {len(blobs)}",
             f"#define MAP_FIRST {names.index('MAP_' + gnames[0].upper())}",
             f"#define PIC_FIRST {names.index('PIC_' + self.pics[0].name)}"]
        if self.fonts:
            h += [f"#define FONT_FIRST {names.index('FONT_' + self.fonts[0][0])}"]
        h += [f"#define ASSET_{n} {names.index(n)}" for n, _ in self.blobs]
        h += [f"#define PIC_{e.name} {i}" for i, e in enumerate(self.pics)] + [f"#define PIC_COUNT {len(self.pics)}"]
        h += [f"#define FONT_{n} {i}" for i, (n, _) in enumerate(self.fonts)]
        h += [f"#define GRP_{n.upper()} {i}" for i, n in enumerate(gnames)] + [f"#define GRP_COUNT {len(gnames)}"]
        h += [f"#define UI_{n} {224 + i}" for i, (n, _) in enumerate(UI)]
        h += [f"#define {n} {v}" for n, v in self.defines]
        h += ["/* each picture's group */",
              "static const uint8_t pic_group[] = {" + ", ".join(str(gnames.index(e.group)) for e in self.pics) + "};",
              "/* each asset: its appvar << 16 | its offset */",
              "static const uint32_t asset_at[] = {" + ", ".join(f"0x{v << 16 | o:X}" for v, o in at) + "};",
              "", "#endif", ""]
        open(os.path.join(ce, "src", "assets.h"), "w").write("\n".join(h))
        print(f"{len(vars_)} appvars, {sum(len(v) for v in vars_)} bytes")
        sizes = {}
        group_of = {"PIC_" + e.name: e.group for e in self.pics}
        for name, d in blobs:
            k = "pictures " + group_of[name] if name in group_of else name.split("_")[0]
            sizes[k] = sizes.get(k, 0) + len(d)
        for k, v in sorted(sizes.items(), key=lambda kv: -kv[1]):
            print(f"  {k}: {v}")
