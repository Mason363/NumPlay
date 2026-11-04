#!/usr/bin/env python3
"""Five Nights at Freddy's on the TI-84 Plus CE: its pictures, fonts and tables -> data/*.bin + src/assets.h.

    python3 -I tools/pack.py REF      (REF: the folder ../tools/fetch.py filled; Python + Pillow + NumPy + SciPy,
                                       and the CE toolchain's convbin for zx0)

The pictures are the NumWorks version's (../tools/pack.py makes them from the game's), coded by tools/cpic.py:
the office and the cameras at 533 x 120 (the calculator doubles each row), the jumpscares and the monitor at
160 x 120 (doubled both ways), the newspaper, the paychecks and the like at 320 x 240.

data/FNAFD<n>.bin become the appvars FNAFD0, FNAFD1... (each under 64 KB): the blobs one after the other.
src/assets.h says where each one is (asset_at: the appvar << 16 | its offset), and the pictures' places."""
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
CE = os.path.dirname(HERE)
GAME = os.path.dirname(CE)
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(GAME, "tools"))
import cpic as C  # noqa: E402
import pack as NW  # noqa: E402  (the NumWorks version's pictures)

CONVBIN = os.path.join(os.environ.get("CEDEV", os.path.expanduser("~/Documents/Assets/toolchains/CEdev")), "bin",
                       "convbin")
VAR_PREFIX = "FNAFD"
VAR_ROOM = 65000
FONT_DIRS = [os.path.expanduser("~/Documents/Assets/gamesrc/fonts/dejavu")]

# the qualities: (step, slope, dead zone); colour steps 1.6 times Y's
QUALITY = {"office": (4.5, 0.3), "cam": (6.5, 0.35), "scare": (7.5, 0.35), "still": (5.0, 0.3), "small": (4.0, 0.3)}
QNAMES = list(QUALITY)
Q = [(C.qtable(s, sl), C.qtable(s * 1.6, sl), 0.36) for s, sl in QUALITY.values()]

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
assert len(UI) == 32
UI_RGB = [((v >> 16) & 255, (v >> 8) & 255, v & 255) for _, v in UI]


def zx0(data):
    with tempfile.TemporaryDirectory() as d:
        i, o = os.path.join(d, "i"), os.path.join(d, "o")
        open(i, "wb").write(bytes(data))
        subprocess.run([CONVBIN, "-j", "bin", "-k", "bin", "-c", "zx0", "-i", i, "-o", o, "-l", "0"], check=True)
        return open(o, "rb").read()


# ------------------------------------------------------------------ the pictures
def pad8(a):
    h, w = a.shape[:2]
    H, W = -(-h // 8) * 8, -(-w // 8) * 8
    return np.pad(a, ((0, H - h), (0, W - w), (0, 0)), mode="edge")


def resize(a, w, h):
    return np.asarray(Image.fromarray(np.clip(a, 0, 255).astype(np.uint8)).resize((w, h), Image.LANCZOS)).astype(
        np.float64)


def halfv(a):
    return resize(a, a.shape[1], (a.shape[0] + 1) // 2)


class Pic:
    def __init__(self, name, rgb, group, quality, base=None, skip=None, x=0, y=0):
        self.name, self.group, self.q = name, group, QNAMES.index(quality)
        self.rgb = pad8(rgb)
        self.base, self.skip, self.x, self.y = base, skip, x, y


PICS = []
GROUPS = {}
DEFINES = []


def group(name, phases=1):
    GROUPS[name] = C.Group(name, 224, phases)
    return name


def pic(name, rgb, grp, quality, base=None, skip=None, x=0, y=0):
    p = Pic(name, rgb, grp, quality, base, skip, x, y)
    PICS.append(p)
    return p


def define(name, value):
    DEFINES.append((name, value))


TIGHT = C.default_skip(10, 1.6, 8)      # variants: what differs from the base's own source
ANIM = C.default_skip(14, 2.5, 10)      # an animation's frames: what moved


def mb_box(a, b, thresh=10):
    """the macroblocks (8 x 8) where a and b differ: x0, y0, x1, y1 in pixels"""
    d = np.abs(pad8(a) - pad8(b)).max(axis=2)
    h, w = d.shape
    m = d.reshape(h // 8, 8, w // 8, 8).max(axis=(1, 3)) > thresh
    ys, xs = np.nonzero(m)
    return xs.min() * 8, ys.min() * 8, (xs.max() + 1) * 8, (ys.max() + 1) * 8


def office_pictures(N):
    g = group("office")
    office = halfv(N["OFFICE"])
    base = pic("OFFICE", office, g, "office")
    define("OFFICE_W", office.shape[1])
    # the lights: each side's lit doorway, a patch over the office (with Bonnie or Chica: over it again)
    for side, lit, who in (("L", "OFFICE_LL", "OFFICE_LLB"), ("R", "OFFICE_RL", "OFFICE_RLC")):
        a, b = halfv(N[lit]), halfv(N[who])
        x0, y0, x1, y1 = [min(v1, v2) if k < 2 else max(v1, v2)
                          for k, (v1, v2) in enumerate(zip(mb_box(a, office), mb_box(b, office)))]
        p = pic(f"LIGHT_{side}", pad8(a)[y0:y1, x0:x1], g, "office", x=x0, y=y0)
        pic(f"LIGHT_{side}_WHO", pad8(b)[y0:y1, x0:x1], g, "office", base=p, skip=TIGHT, x=x0, y=y0)
        define(f"LIGHT_{side}_X", x0)
        define(f"LIGHT_{side}_Y", y0)
        define(f"LIGHT_{side}_W", x1 - x0)
        define(f"LIGHT_{side}_H", y1 - y0)
    # the doors, buttons, fan and Golden Freddy, as the NumWorks version places them (rows halved)
    for n in ("DOOR_L", "DOOR_R"):
        pic(n, halfv(N[n]), g, "office")
    for n in [f"PANEL_{s}{k}" for s in "LR" for k in range(4)] + ["FAN0", "FAN1", "FAN2", "OFFICE_GOLD"]:
        pic(n, halfv(N[n]), g, "office")
    # out of power: the dark office, and Freddy's face in the doorway (a patch)
    dark = halfv(N["DARK"])
    pic("DARK", dark, g, "office")
    df = halfv(N["DARK_FREDDY"])
    x0, y0, x1, y1 = mb_box(df, dark, 6)
    pic("DARK_FREDDY", pad8(df)[y0:y1, x0:x1], g, "office", x=x0, y=y0)
    for k, v in zip("XYWH", (x0, y0, x1 - x0, y1 - y0)):
        define(f"DARK_FREDDY_{k}", v)


def camera_pictures(N):
    for cam, states in NW.CAMS.items():
        g = group("cam" + cam.lower(), 4)
        first = None
        for who, _ in states:
            name = f"CAM{cam}_{who.upper() or 'EMPTY'}"
            p = pic(name, halfv(N[name]), g, "cam", base=first, skip=TIGHT if first else None)
            first = first or p
        if cam == "2A":
            pic("CAM2A_DARK", halfv(N["CAM2A_DARK"]), g, "cam", base=first, skip=TIGHT)
            # Foxy running down the hall: the whole hall, each frame over the one before
            im = Image.open(NW.web("Foxy-Hallway.webp"))
            prev = first
            for i in range(min(im.n_frames, 18)):
                im.seek(i)
                a = halfv(np.asarray(im.convert("RGB").resize((NW.OW, NW.OH), Image.LANCZOS)).astype(np.float64))
                prev = pic(f"FOXYRUN{i}", a, g, "cam", base=prev, skip=ANIM)
            define("FOXYRUN_N", 18)
        if cam == "2B":
            pic("CAM2B_GOLD", halfv(N["CAM2B_GOLD"]), g, "cam", base=first, skip=TIGHT)


def anim(name, frames, grp, quality="scare"):
    prev = None
    for i, a in enumerate(frames):
        prev = pic(f"{name}{i}", resize(a, 160, 120), grp, quality, base=prev, skip=ANIM if prev else None)
    define(f"{name}_N", len(frames))


def scare_pictures(N):
    for name, n in (("BONNIE", 10), ("CHICA", 15), ("FREDDY", 20), ("FOXY", 19), ("POWEROUT", 21)):
        anim(name, [N[f"{name}{i}"] for i in range(n)], group(name.lower()))
    anim("FLIP", [N[f"FLIP{i}"] for i in range(11)], group("flip"))
    # the hallucinations' pictures (a frame now and then): small
    g = group("halluc")
    pic("HALLUC_GOLDEN", resize(N["GOLDEN"], 160, 120), g, "scare")
    pic("HALLUC_ENDO", resize(N["TITLE3"], 160, 120), g, "scare")


def menu_pictures(N):
    g = group("title", 4)
    t0 = halfv(N["TITLE0"])
    pic("TITLE0", t0, g, "still")
    for k in (1, 2, 3):   # Freddy's twitches: patches over the face
        a = halfv(N[f"TITLE{k}"])
        x0, y0, x1, y1 = mb_box(a, t0, 12)
        pic(f"TITLE{k}", pad8(a)[y0:y1, x0:x1], g, "still", x=x0, y=y0)
        for c, v in zip("XYWH", (x0, y0, x1 - x0, y1 - y0)):
            define(f"TITLE{k}_{c}", v)
    pic("NEWSPAPER", N["NEWSPAPER"], group("news"), "still")
    g = group("paper")
    for n in ("PAYCHECK", "OVERTIME", "PINKSLIP"):
        pic(n, N[n], g, "still")
    pic("GAMEOVER", N["GAMEOVER"], group("gameover", 4), "still")
    pic("GOLDEN", N["GOLDEN"], group("golden", 4), "still")
    g = group("faces")
    for n in ("FREDDY", "BONNIE", "CHICA", "FOXY"):
        pic(f"FACE_{n}", N[f"FACE_{n}"], g, "small")


# ------------------------------------------------------------------ fonts, small pictures, the map
def font_blob(line, asc, glyphs):
    """line height, ascent, then 95 glyphs (32..126) of 8 bytes: advance, width, height, x, y (s8), 0, offset
    (u16, from the font's start) of rows of 2-bit alpha (4 pixels a byte, the first in the top bits)"""
    head = struct.pack("<BB", line, asc)
    rows = b""
    table = b""
    base = 2 + 8 * 95
    for adv, w, h, x, y, nib in glyphs:
        a4 = unnibble(nib, w, h)
        a2 = np.clip((a4.astype(np.int64) * 3 + 7) // 15, 0, 3)
        data = pack2(a2)
        table += struct.pack("<BBBbbBH", adv, w, h, x, y, 0, base + len(rows))
        rows += data
    return head + table + rows


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


def img_blob(alpha):
    a2 = np.clip(np.round(np.asarray(alpha, dtype=np.float64) / 255 * 3), 0, 3).astype(np.int64)
    h, w = a2.shape
    return struct.pack("<HH", w, h) + pack2(a2)


def map_blob(raw):
    """the camera map: its walls as runs (count, then y, x, length as bytes) and the 11 buttons' centres"""
    size = struct.unpack_from("<H", raw, 0)[0]
    stride = (size + 7) // 8
    runs = []
    for r in range(size):
        row = raw[4 + r * stride:4 + (r + 1) * stride]
        c = 0
        while c < size:
            if row[c >> 3] >> (c & 7) & 1:
                s = c
                while c < size and row[c >> 3] >> (c & 7) & 1:
                    c += 1
                runs.append((r, s, c - s))
            else:
                c += 1
    centres = raw[4 + size * stride:4 + size * stride + 44]
    out = struct.pack("<BH", size, len(runs)) + b"".join(struct.pack("<BBB", *r) for r in runs)
    return out + bytes(v for i in range(11) for v in struct.unpack_from("<HH", centres, 4 * i))


# ------------------------------------------------------------------ all of it
def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    NW.REF = sys.argv[1]
    NW.FONT_DIRS[:0] = FONT_DIRS
    NW.office_pictures()
    NW.camera_pictures()
    NW.scare_pictures()
    NW.menu_pictures()
    N = {p.name: p.a[:p.h, :p.w].astype(np.float64) for p in NW.PICS}

    office_pictures(N)
    camera_pictures(N)
    scare_pictures(N)
    menu_pictures(N)

    # code every picture (bases first: they come first), then the palettes, then the Huffman tables
    print(f"coding {len(PICS)} pictures", flush=True)
    for p in PICS:
        p.coded = C.code(p.rgb, Q[p.q], base=p.base.coded if p.base else None, skip=p.skip)
        GROUPS[p.group].planes.append(p.coded.rec)
    for g in GROUPS.values():
        g.fit()
    streams = [C.stream(p.coded) for p in PICS]
    codes, cc = [], []
    for f in C.count(streams):
        cds, counts, syms = C.canonical(C.huffman_lengths(dict(f) or {0: 1}))
        codes.append(cds)
        cc.append((counts, syms))

    blobs = []   # (name, bytes)

    def blob(name, data):
        blobs.append((name, bytes(data)))

    blob("TABLES", C.tables_blob(Q, cc))
    gnames = list(GROUPS)
    for n in gnames:
        blob("MAP_" + n.upper(), zx0(GROUPS[n].blob(UI_RGB) + remaps(GROUPS[n])))
    pic_ids = {p.name: i for i, p in enumerate(PICS)}
    for p, st in zip(PICS, streams):
        blob("PIC_" + p.name, C.write(p.coded, st, codes, p.q, pic_ids[p.base.name] if p.base else None))
    fonts = [
        NW.ttf_font(NW.find_font("DejaVuSansMono-Bold.ttf"), 11),
        NW.ttf_font(NW.find_font("DejaVuSansMono.ttf"), 17),
        NW.ttf_font(NW.find_font("DejaVuSans.ttf"), 11),
        NW.fnt_font(NW.hx("numbers/NUMBER.fnt"), NW.hx("numbers/NUMBER.png"), 0.36),
        NW.fnt_font(NW.hx("numbers/PIXEL_NUMBER.fnt"), NW.hx("numbers/PIXEL_NUMBER.png"), 0.36),
        NW.ttf_font(NW.find_font("DejaVuSansMono.ttf"), 24),
    ]
    for n, f in zip(["HUD", "BIG", "TEXT", "CLOCK", "PIXEL", "HUGE"], fonts):
        blob("FONT_" + n, font_blob(*f))
    t = Image.open(NW.hx("mainmenu/texts/FIVE_GAME_TITLE.png")).convert("RGBA")
    t = t.resize((int(t.width * 0.5), int(t.height * 0.5)), Image.LANCZOS)
    a = np.asarray(t).astype(np.float64)
    blob("IMG_TITLE", img_blob(a[..., 3] * a[..., :3].mean(axis=2) / 255))
    s = Image.open(NW.hx("mainmenu/STAR.png")).convert("RGBA").resize((22, 21), Image.LANCZOS)
    a = np.asarray(s).astype(np.float64)
    blob("IMG_STAR", img_blob(a[..., 3] * a[..., :3].mean(axis=2) / 255))
    blob("CAMMAP", map_blob(NW.camera_map()))

    # into appvars
    os.makedirs(os.path.join(CE, "data"), exist_ok=True)
    for f in os.listdir(os.path.join(CE, "data")):
        if f.startswith(VAR_PREFIX) and f.endswith(".bin"):
            os.remove(os.path.join(CE, "data", f))
    vars_, at = [bytearray()], []
    for name, data in blobs:
        if len(vars_[-1]) + len(data) > VAR_ROOM:
            vars_.append(bytearray())
        at.append((len(vars_) - 1, len(vars_[-1])))
        vars_[-1] += data
    for i, v in enumerate(vars_):
        open(os.path.join(CE, "data", f"{VAR_PREFIX}{i}.bin"), "wb").write(v)
    h = ["/* Made by tools/pack.py: what is in the appvars FNAFD0, FNAFD1... */", "#ifndef FNAF_ASSETS_H",
         "#define FNAF_ASSETS_H", "#include <stdint.h>", "", f'#define DATA_VAR "{VAR_PREFIX}"',
         f"#define DATA_VARS {len(vars_)}", f"#define ASSET_COUNT {len(blobs)}", ""]
    names = [n for n, _ in blobs]
    h += [f"#define ASSET_TABLES {names.index('TABLES')}", f"#define MAP_FIRST {names.index('MAP_' + gnames[0].upper())}",
          f"#define PIC_FIRST {names.index('PIC_' + PICS[0].name)}", f"#define FONT_FIRST {names.index('FONT_HUD')}",
          f"#define ASSET_IMG_TITLE {names.index('IMG_TITLE')}", f"#define ASSET_IMG_STAR {names.index('IMG_STAR')}",
          f"#define ASSET_CAMMAP {names.index('CAMMAP')}", ""]
    h += [f"#define PIC_{p.name} {i}" for i, p in enumerate(PICS)]
    h += [f"#define PIC_COUNT {len(PICS)}"]
    h += [f"#define FONT_{n} {i}" for i, n in enumerate(["HUD", "BIG", "TEXT", "CLOCK", "PIXEL", "HUGE"])]
    h += [f"#define GRP_{n.upper()} {i}" for i, n in enumerate(gnames)]
    h += [f"#define GRP_COUNT {len(gnames)}"]
    h += [f"#define UI_{n} {224 + i}" for i, (n, _) in enumerate(UI)]
    h += [f"#define {n} {v}" for n, v in NW.DEFINES if n.startswith(("DOOR_", "PANEL_", "FAN_", "GOLD_", "MAP_"))]
    h += [f"#define {n} {v}" for n, v in DEFINES]
    h += ["/* each picture's group, and where it goes (pixels) */",
          "static const uint8_t pic_group[] = {" + ", ".join(str(gnames.index(p.group)) for p in PICS) + "};",
          "/* each asset: its appvar << 16 | its offset */",
          "static const uint32_t asset_at[] = {" + ", ".join(f"0x{v << 16 | o:X}" for v, o in at) + "};",
          "", "#endif", ""]
    open(os.path.join(CE, "src", "assets.h"), "w").write("\n".join(h))
    total = sum(len(v) for v in vars_)
    print(f"{len(vars_)} appvars, {total} bytes")
    sizes = {}
    for (name, data) in blobs:
        k = name.split("_")[0]
        if name.startswith("PIC_"):
            p = PICS[pic_ids[name[4:]]]
            k = "PIC " + p.group
        sizes[k] = sizes.get(k, 0) + len(data)
    for k, v in sorted(sizes.items(), key=lambda kv: -kv[1]):
        print(f"  {k}: {v}")


def remaps(g):
    """two 256-byte tables of the group's palette: each colour darkened to 45 %, and lightened a little (a
    fifth of the way to white): the nearest colour of the same phase (the fixed ones: among the fixed ones)"""
    from scipy.spatial import cKDTree
    pal = [tuple(c) for c in g.pal]
    cols = np.array([pal[i // g.phases] if i // g.phases < len(pal) else (0, 0, 0) for i in range(224)] + UI_RGB,
                    dtype=np.float64)
    used = len(pal) * g.phases
    sets = [[j for j in range(used) if j % g.phases == p] for p in range(g.phases)] + [list(range(224, 256))]
    trees = [cKDTree(np.sqrt(cols[c] / 255)) for c in sets]
    out = b""
    for f in (lambda c: c * 0.45, lambda c: c + (255 - c) * 0.2):
        t = []
        for i in range(256):
            k = (i % g.phases) if i < 224 else g.phases
            _, j = trees[k].query(np.sqrt(np.clip(f(cols[i]), 0, 255) / 255))
            t.append(sets[k][j])
        out += bytes(t)
    return out


if __name__ == "__main__":
    main()
