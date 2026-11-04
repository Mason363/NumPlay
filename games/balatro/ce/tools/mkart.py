#!/usr/bin/env python3
"""The TI-84 Plus CE version's art and tables, made from the NumWorks version's (src/assets.c) and Balatro's shaders
as the NumWorks version works them out (src/bg.c, fx.c, flame.c). Python 3 only (and convbin for zx0).

  mkart.py OUTDIR CONVBIN

writes OUTDIR/ce_art.c (the sprites' table, the fonts, the fixed colours), OUTDIR/ce_art.h (constants), and the appvars'
contents as OUTDIR/<NAME>.bin, listed in OUTDIR/appvars.txt.

Colours: 0..175 fixed (tools/palette.py), 176..191 taken by screens as they need them, 192..255 the background's
(16 levels of its paint, in 4 banks: as it is, then darker under shaded panels and shadows).
Sprites: rows of runs (a skip, a length, the pixels), after a table of each row's start; the cards' size of art
(Jokers, Tarots, Planets, Spectrals, Vouchers, packs) is also zx0-packed, unpacked when shown (src/art_ce.c)."""
import math
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from artsrc import GAME, Art, to888  # noqa: E402
from palette import PALETTE  # noqa: E402

GAME_SRC = os.path.join(GAME, "src")
NSTATIC, DYN0, SWIRL0, LEVELS, BANKS = 176, 176, 192, 16, 4
BANK_DARK = (0, 9, 15, 21)  # how dark each bank is, of 32
MAXVAR = 65000              # bytes an appvar holds here
TRANSPARENT = 255


# ------------------------------------------------------------------ colours
def lab(rgb):
    r, g, b = (v / 255.0 for v in rgb)
    y = 0.2126 * r ** 2.2 + 0.7152 * g ** 2.2 + 0.0722 * b ** 2.2
    return (y ** (1 / 3) * 100, (r - g) * 60, (g - b) * 40, (r - b) * 30)


PLAB = [lab(c) for c in PALETTE]
_near = {}


def nearest(rgb):
    """the closest fixed colour"""
    rgb = tuple(int(v) for v in rgb)
    if rgb not in _near:
        x = lab(rgb)
        best, bd = 0, 1e30
        for i, p in enumerate(PLAB):
            d = (p[0] - x[0]) ** 2 + (p[1] - x[1]) ** 2 + (p[2] - x[2]) ** 2 + (p[3] - x[3]) ** 2
            if d < bd:
                bd, best = d, i
        _near[rgb] = best
    return _near[rgb]


def c1555(rgb):
    r, g, b = rgb
    return (r & 248) << 7 | (g & 248) << 2 | b >> 3


def clamp01(v):
    return 0.0 if v < 0 else 1.0 if v > 1 else v


# ------------------------------------------------------------------ sprites
def rle_rows(rows, w):
    """[row table: h x u16 offsets from the sprite's start] then the rows: (skip, length, pixels) until w"""
    h = len(rows)
    body = bytearray()
    offs = []
    for r in rows:
        offs.append(2 * h + len(body))
        x = 0
        while x < w:
            s = x
            while x < w and r[x] == TRANSPARENT and x - s < 255:
                x += 1
            l0 = x
            while x < w and r[x] != TRANSPARENT and x - l0 < 255:
                x += 1
            body += bytes((l0 - s, x - l0)) + bytes(r[l0:x])
    out = bytearray()
    for o in offs:
        out += bytes((o & 255, o >> 8))
    return bytes(out + body)


def zx0(data, convbin, tmp):
    src, dst = os.path.join(tmp, "zx.in"), os.path.join(tmp, "zx.out")
    open(src, "wb").write(data)
    subprocess.run([convbin, "-j", "bin", "-i", src, "-k", "bin", "-c", "zx0", "-o", dst, "-l", "0"], check=True)
    return open(dst, "rb").read()


# ------------------------------------------------------------------ the background (src/bg.c)
PI = math.pi


def paint(sx, sy, mode, t_real, t_spin, spin, contrast):
    diag = 400.0
    ux, uy = (sx - 160.0) / diag, (sy - 120.0) / diag
    if mode == 0:
        ux -= 0.12
    ln = math.sqrt(ux * ux + uy * uy)
    if mode == 0:
        sp = t_spin * 0.5 * 0.2 + 302.2
        ang = math.atan2(uy, ux) + sp - 0.5 * 20.0 * (spin * ln + (1.0 - spin))
        vx, vy = ln * math.cos(ang) * 30.0, ln * math.sin(ang) * 30.0
        speed = t_real * 2.0
    else:
        s = t_real * 0.4
        ms = min(6.0, s)
        ang = math.atan2(uy, ux) + (2.2 + 0.4 * ms) * ln - 1.0 - s * 0.05 - ms * s * 0.02
        vx, vy = ln * math.cos(ang) * 30.0, ln * math.sin(ang) * 30.0
        speed = t_real * 6.0 * 0.4 + 1033.0
    u2x = u2y = vx + vy
    for _ in range(5):
        m = math.sin(max(vx, vy))
        u2x += m + vx
        u2y += m + vy
        vx += 0.5 * math.cos(5.1123314 + 0.353 * u2y + speed * 0.131121)
        vy += 0.5 * math.sin(u2x - 0.113 * speed)
        d = math.cos(vx + vy) - math.sin(vx * 0.711 - vy)
        vx -= d
        vy -= d
    l = math.sqrt(vx * vx + vy * vy)
    if mode == 0:
        cm = 0.25 * contrast + 0.5 * spin + 1.2
        r = min(2.0, max(0.0, l * 0.035 * cm))
    else:
        r = min(2.0, max(-2.0, 1.5 + l * 0.12 - 0.17 * 10.0))
        if r < 0.2:
            r = (r - 0.2) * 0.6 + 0.2
        r = (r + 2.0) * 0.5
    return min(255, max(0, int(r * 127.5 + 0.5)))


BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def field(mode, x0, w, h, step):
    """the paint (0..255) every `step` pixels, then as levels 0..15 (ordered dither), as palette indices"""
    out = bytearray()
    for y in range(h):
        for x in range(w):
            sx, sy = x0 + x * step + (step - 1) / 2, y * step + (step - 1) / 2
            v = paint(sx, sy, mode, 20.0 if mode else 36.0, 0.0, 0.0, 1.0)
            lv = (v * (LEVELS - 1) * 16 // 255 + BAYER[y & 3][x & 3]) // 16
            out.append(SWIRL0 + min(LEVELS - 1, lv))
    return bytes(out)


# ------------------------------------------------------------------ editions (src/fx.c)
def unpack(c):
    return c[0] / 255.0, c[1] / 255.0, c[2] / 255.0


def over_art(o, n, k):
    d = (.3 * o[0] + .59 * o[1] + .11 * o[2]) - (.3 * n[0] + .59 * n[1] + .11 * n[2])
    n = [v + d for v in n]
    return [o[i] + (n[i] - o[i]) * k for i in range(3)]


def hsl(r, g, b):
    lo, hi = min(r, g, b), max(r, g, b)
    d, s_ = hi - lo, hi + lo
    l = .5 * s_
    if d <= 0:
        return 0.0, 0.0, l
    s = d / s_ if l < .5 else d / (2.0 - s_)
    x = (g - b) / d if hi == r else (b - r) / d + 2.0 if hi == g else (r - g) / d + 4.0
    x /= 6
    return x - math.floor(x), s, l


def hue(s, t, h):
    hs = (h - math.floor(h)) * 6.0
    if hs < 1:
        return (t - s) * hs + s
    if hs < 3:
        return t
    if hs < 4:
        return (t - s) * (4.0 - hs) + s
    return s


def rgbh(h, s, l):
    if s < 0.0001:
        return l, l, l
    t = s * l + l if l < .5 else -s * l + (s + l)
    q = 2.0 * l - t
    return hue(q, t, h + 1 / 3), hue(q, t, h), hue(q, t, h - 1 / 3)


def ed_foil(c, m):
    r, g, b = unpack(c)
    lo, hi = min(r, g, b), max(r, g, b)
    d = max(1.0 - lo, .5)
    d = min(d, hi)
    fr, fg, fb = r - d + d * m * .3, g - d + d * m * .3, b + d * m * 1.9
    a = (.3 + .9 * min(m * .1, .5)) * .8
    fr, fg, fb = clamp01(fr), clamp01(fg), clamp01(fb)
    return r + (fr - r) * a, g + (fg - g) * a, b + (fb - b) * a


def ed_holo(c, shift):
    r, g, b = unpack(c)
    h, s, l = hsl(.5 * r, .5 * g, .5 * b + .5)
    lo, hi = min(r, g, b), max(r, g, b)
    d = .2 + .3 * (hi - lo) + .1 * hi
    hr, hg, hb = rgbh(h + shift, min(s * 1.3, 1), l * .6 + .4)
    n = [clamp01((1 - d) * r + d * hr * .9), clamp01((1 - d) * g + d * hg * .8), clamp01((1 - d) * b + d * hb * 1.2)]
    return over_art((r, g, b), n, .8)


def ed_poly(c, shift):
    r, g, b = unpack(c)
    lo, hi = min(r, g, b), max(r, g, b)
    sf = 1.0 - (.05 * (1.1 - (hi - lo)) if 1.1 - (hi - lo) > 0 else 0)
    h, s, l = hsl(r * sf, g * sf, b)
    n = rgbh(h + shift, min(s + .5, .6), l)
    return over_art((r, g, b), n, .7)


def ed_neg(c, sh):
    r, g, b = unpack(c)
    h, s, l = hsl(r, g, b)
    nr, ng, nb = rgbh(.2 - h, s, 1.0 - l)
    nr += .8 * 79 / 255
    ng += .8 * 99 / 255
    nb += .8 * 103 / 255
    return nr + sh * .6, ng + sh * .6, nb + sh


def fsin(x):
    return math.sin(x)


def fcos(x):
    return math.cos(x)


def efield(u, v, k, t):
    x, y = (u - .5) * k, (v - .5) * k
    x1, y1 = x + 50 * fsin(-t / 143.634), y + 50 * fcos(-t / 99.4324)
    x2, y2 = x + 50 * fcos(t / 53.1532), y + 50 * fcos(t / 61.4532)
    x3, y3 = x + 50 * fsin(-t / 87.53218), y + 50 * fsin(-t / 49.0)
    return (1 + fcos(math.sqrt(x1 * x1 + y1 * y1) / 19.483) + fsin(math.sqrt(x2 * x2 + y2 * y2) / 33.155) * fcos(y2 / 15.73)
            + fcos(math.sqrt(x3 * x3 + y3 * y3) / 27.193) * fsin(x3 / 21.92)) * .5


def cl(v, a, b):
    return a if v < a else b if v > b else v


def foil_pattern(u, v, T):
    fr, fg = T / 28.0, T
    ax, ay = (u - .5) * (35.0 / 47.0), v - .5
    L = math.sqrt(ax * ax + ay * ay) + 1e-4
    fac = cl(2 * fsin(L * 90 + fr * 2 + 3 * (1 + .8 * fcos(L * 113.1121 - fr * 3.121))) - 1 - max(5 - L * 90, 0), 0, 1)
    rx, ry = fcos(fr * .1221), fsin(fr * .3512)
    ang = (rx * ax + ry * ay) / (math.sqrt(rx * rx + ry * ry) * L)
    fac2 = cl(5 * fcos(fg * .3 + ang * 3.14 * (2.2 + .9 * fsin(fr * 1.65 + .2 * fg))) - 4 - max(2 - L * 20, 0), 0, 1)
    fac3 = .3 * cl(2 * fsin(fr * 5 + u * 3 + 3 * (1 + .5 * fcos(fr * 7))) - 1, -1, 1)
    fac4 = .3 * cl(2 * fsin(fr * 6.66 + v * 3.8 + 3 * (1 + .5 * fcos(fr * 3.414))) - 1, -1, 1)
    mx = max(fac, fac2, fac3, fac4, 0)
    return max(0.0, mx + 2.2 * (fac + fac2 + fac3 + fac4))


def holo_pattern(u, v, T):
    t = T * 8.221
    res = .5 + .5 * fcos((T / 28.0) * 2.612 + (efield(u, v, 125.0, t) - .5) * 3.14)
    gs = .79 * .5
    f1 = 7 * abs(fcos(u * gs * 20)) - 6
    f2 = 7 * fcos(v * gs * 45 + u * gs * 20) - 6
    f3 = 7 * fcos(v * gs * 45 - u * gs * 20) - 6
    fac = max(f1, f2, f3)
    return res + (.5 * fac if fac > 0 else 0)


def poly_pattern(u, v, T):
    t = T * 3.221
    return .5 + .5 * fcos((T / 28.0) * 2.612 + (efield(u, v, 50.0, t) - .5) * 3.14)


def neg_pattern(u, v, T):
    sh = .5 + .5 * fsin(11 * u + 4.32 * v + T * .43 * 12 + fcos(T * .19 + v * 4.2 - u * 4))
    return sh * sh * .12


ED_FRAMES, ED_LEVELS, ED_CW, ED_CH = 32, 16, 18, 24
ED_DT = 0.3  # seconds between frames
# each edition's pattern range, cut into ED_LEVELS levels: (low, high, wraps)
ED_RANGE = [(0.0, 4.0, False), (0.0, 1.0, True), (0.0, 1.0, True), (0.0, 0.12, False)]
ED_FUN = [(foil_pattern, ed_foil), (holo_pattern, ed_holo), (poly_pattern, ed_poly), (neg_pattern, ed_neg)]


def ed_level(e, val):
    lo, hi, wraps = ED_RANGE[e]
    f = (val - lo) / (hi - lo)
    if wraps:
        f -= math.floor(f)
        return int(f * ED_LEVELS) % ED_LEVELS
    return max(0, min(ED_LEVELS - 1, int(f * (ED_LEVELS - 1) + 0.5)))


def ed_value(e, lv):
    lo, hi, wraps = ED_RANGE[e]
    if wraps:
        return lo + (hi - lo) * (lv + .5) / ED_LEVELS
    return lo + (hi - lo) * lv / (ED_LEVELS - 1)


# ------------------------------------------------------------------ the flames (src/flame.c)
def len_term(ln):
    return 0.3 * (fcos(ln * 0.411) + 0.3344 * fsin(ln) - 0.23 * fcos(ln))


def flame_params(k, I, t):
    idv = 91.0 if k else 37.0
    up = 4.0 * t - 5000.0 + 1.781 * idv
    scale = 7.5 + 3.0 / (2.0 + 2.0 * I)
    speed = (91.071 if k else 68.897) + fsin(t + idv) * fcos(t * 0.151 + idv)
    return dict(up=up, scale=scale, sp1=speed * 0.0812, sp2=speed * 0.1531, sp3=0.113785 * speed, sp4=0.13582 * speed,
                top_cut=max(0.0, 2.0 - 0.3 * I), damp=2.0 / (2.0 + I * 0.2), w1=0.2 * t, w2=0.931 * t)


def smoke_at(q, fx, fy):
    if q["top_cut"] * 2 * (fy - .5) * (fy - .5) > 1.02:
        return 9.0
    wob = 0.01 * fsin(-1.123 * fx + q["w1"]) * fcos(5.3332 * fy + q["w2"])
    ucx, ucy = fx + fx * wob, fy + fy * wob
    svx, svy = ucx * q["scale"], ucy * q["scale"] + q["up"]
    s2x = s2y = 0.0
    for _ in range(5):
        ln = math.sqrt(svx * svx + svy * svy)
        add = len_term(ln)
        nx, ny = s2x + svx + 0.05 * s2y + add, s2y + svy + 0.05 * s2x + add
        s2x, s2y = nx, ny
        svx += 0.5 * fcos(fcos(s2y) + q["sp1"]) * fsin(3.22 + s2x - q["sp2"])
        svy += 0.5 * fsin(-s2x * 1.21222 + q["sp3"]) * fcos(s2y * 0.91213 - q["sp4"])
    dx, dy = svx / q["scale"] * 5, (svy - q["up"]) / q["scale"] * 5
    sm = max(0.0, (math.sqrt(dx * dx + dy * dy) + 0.1 * (math.sqrt(ucx * ucx + ucy * ucy) - .5)) * q["damp"])
    e = ucy - .5
    sm += q["top_cut"] * 2 * e * e
    if abs(fx) > 0.4:
        sm += 10 * (abs(fx) - 0.4)
    return sm


FL_W, FL_H, FL_FRAMES, FL_LEVELS = 54, 47, 24, 5
FL_I = (0.6, 1.5, 3.0, 5.0, 8.0)  # intensities kept
FL_GC, FL_GR = FL_W // 2 + 1, int(0.62 * FL_H) // 2 + 2
FL_ROWS = int(0.62 * FL_H)
COL1 = ((0.0, 0.616, 1.0), (0.996, 0.373, 0.333))
COL2 = ((0.36, 0.824, 0.36), (1.0, 0.618, 0.1))


def flame_tables():
    """grids: [level][frame] FL_GR x FL_GC bytes, the smoke in 1/128 units capped at 17 (the flame shows under 16);
    colours: [k][level][row][17] palette indices"""
    grids = bytearray()
    for li, I in enumerate(FL_I):
        for f in range(FL_FRAMES):
            q = flame_params(1, I, f * 0.11)
            for j in range(FL_GR):
                for i in range(FL_GC):
                    sm = smoke_at(q, i * 2.0 / FL_W - .5, j * 2.0 / FL_H - .5)
                    grids.append(min(17, int(sm * 2048) >> 7))
    cols = bytearray()
    for k in range(2):
        for I in FL_I:
            for r in range(FL_ROWS):
                uy = (r + .5) / FL_H - .5
                d = 0.12 - uy
                base = [COL1[k][ch] * (1 - .5 * d) + 2.5 * d * COL2[k][ch] for ch in range(3)]
                for n in range(17):
                    m = 1.0 + (-2.0 + 0.5 * I * (n / 16)) * d
                    cols.append(nearest(tuple(int(clamp01(base[ch] * m) * 255 + .5) for ch in range(3))))
    return bytes(grids), bytes(cols)


# ------------------------------------------------------------------ tables
def shade_luts():
    """33 x 176: each fixed colour darkened by a/32"""
    out = bytearray()
    for a in range(33):
        for c in PALETTE:
            out.append(nearest(tuple(v * (32 - a) // 32 for v in c)))
    return bytes(out)


def grey_lut():
    out = bytearray()
    for c in PALETTE:
        r, g, b = c[0] >> 3, c[1] >> 2, c[2] >> 3
        l = (r * 2 + g + b * 2) // 5
        out.append(nearest((l << 3, l << 3, l << 3)))
    return bytes(out)


def edition_luts():
    out = bytearray()
    for e in range(4):
        for lv in range(ED_LEVELS):
            val = ed_value(e, lv)
            for c in PALETTE:
                r, g, b = ED_FUN[e][1](c, val)
                out.append(nearest((int(clamp01(r) * 255 + .5), int(clamp01(g) * 255 + .5), int(clamp01(b) * 255 + .5))))
    return bytes(out)


def edition_patterns():
    """[ed][frame][24 rows][9 bytes]: two cells a byte (low nibble first)"""
    out = bytearray()
    for e in range(4):
        pat = ED_FUN[e][0]
        for f in range(ED_FRAMES):
            T = 4.0 + f * ED_DT
            for cy in range(ED_CH):
                v = (cy * 2 + 1) / 47.0
                row = [ed_level(e, pat((cx * 2 + 1) / 35.0, v, T)) for cx in range(ED_CW)]
                for i in range(0, ED_CW, 2):
                    out.append(row[i] | row[i + 1] << 4)
    return bytes(out)


# ------------------------------------------------------------------ output
COMPRESSED_GROUPS = (0, 1, 2)  # jokers, consumables, vouchers and packs


def main():
    outdir, convbin = sys.argv[1], sys.argv[2]
    os.makedirs(outdir, exist_ok=True)
    art = Art()
    n = len(art.table)
    sprites = []  # (appvar, offset, w, h, packed)
    vars_ = {}
    order = []

    def put(prefix, data):
        """data into the first appvar of that prefix with room: (its index in order, offset)"""
        k = 0
        while True:
            name = f"{prefix}{k}"
            if name not in vars_:
                vars_[name] = bytearray()
                order.append(name)
            if len(vars_[name]) + len(data) <= MAXVAR:
                off = len(vars_[name])
                vars_[name] += data
                return order.index(name), off
            k += 1

    maxpacked = 0
    for i in range(n):
        w, h = art.size(i)
        g = art.table[i][1]
        px = art.rgb565(i)
        rows = [[TRANSPARENT if v is None else nearest(to888(v)) for v in r] for r in px]
        data = rle_rows(rows, w)
        packed = g in COMPRESSED_GROUPS and art.tmpl_of_group[g] != i
        if packed:
            maxpacked = max(maxpacked, len(data))
            data = zx0(data, convbin, outdir)
        v, off = put("BALART", data)
        sprites.append((v, off, w, h, packed))
    lut = bytearray()
    shade = shade_luts()
    grey = grey_lut()
    edl = edition_luts()
    edp = edition_patterns()
    fgrids, fcols = flame_tables()
    sys.path.insert(0, HERE)
    from cedata import blob as text_blob  # noqa: E402
    texts = text_blob(open(os.path.join(GAME_SRC, "data.c"), encoding="utf-8").read())
    fonts = []
    for name, (glyphs, bits) in art.fonts.items():
        tab = bytearray()
        for o, w, a in glyphs:
            tab += bytes((o & 255, o >> 8, w, a))
        fonts += [(name, tab), (name + "_bits", bits)]
    parts = {}
    for name, blob in (("shade", shade), ("grey", grey), ("edlut", edl), ("fcol", fcols), ("text", texts)) + tuple(fonts):
        parts[name] = len(lut)
        lut += blob
    # the background: the run's paint right of the sidebar, the title's at half size
    bg_run = field(0, 102, 218, 240, 1)
    bg_title = field(1, 0, 160, 120, 2)
    blobs = dict(vars_)
    blobs["BALLUT"] = lut
    blobs["BALBG0"] = bg_run
    blobs["BALBG1"] = bg_title
    blobs["BALFIRE"] = fgrids
    blobs["BALED"] = edp
    if max(len(b) for b in blobs.values()) > MAXVAR:
        raise SystemExit("mkart: an appvar is too big")
    for name, blob in blobs.items():
        open(os.path.join(outdir, name + ".bin"), "wb").write(bytes(blob))
    open(os.path.join(outdir, "appvars.txt"), "w").write("\n".join(blobs) + "\n")
    # C: the table (the NumWorks's art_sprite_t: off = offset in its appvar, group = appvar * 2 + packed)
    c = ["/* Made by tools/mkart.py from src/assets.c: do not edit. */", '#include "ce_art.h"', '#include "assets.h"', ""]
    c.append("const art_sprite_t art_sprites[ART_NSPRITES] = {")
    for v, off, w, h, packed in sprites:
        c.append(f"  {{{off}, {v * 2 + packed}, {w}, {h}}},")
    c.append("};")
    c.append("const char ce_art_vars[CE_ART_NVARS][9] = {" + ", ".join(f'"{x}"' for x in order) + "};")
    c.append("const uint16_t ce_palette[176] = {" + ",".join(str(c1555(p)) for p in PALETTE) + "};")
    open(os.path.join(outdir, "ce_art.c"), "w").write("\n".join(c) + "\n")
    h = ["/* Made by tools/mkart.py: do not edit. */", "#ifndef CE_ART_GEN_H", "#define CE_ART_GEN_H",
         "#include <stdint.h>",
         f"#define CE_ART_NVARS {len(order)}", f"#define CE_PACKED_MAX {maxpacked}",
         f"#define CE_NSTATIC {NSTATIC}", f"#define CE_DYN0 {DYN0}", f"#define CE_SWIRL0 {SWIRL0}",
         f"#define CE_LEVELS {LEVELS}", f"#define CE_BANKS {BANKS}",
         "#define CE_BANK_DARK {" + ",".join(map(str, BANK_DARK)) + "}",
         f"#define CE_WHITE {nearest((255, 255, 255))}",
         f"#define LUT_SHADE {parts['shade']}", f"#define LUT_GREY {parts['grey']}", f"#define LUT_EDLUT {parts['edlut']}",
         f"#define LUT_FCOL {parts['fcol']}", f"#define LUT_TEXT {parts['text']}",
         ] + [f"#define LUT_{name.upper()} {parts[name]}" for name, _ in fonts] + [
         f"#define ED_FRAMES {ED_FRAMES}", f"#define ED_LEVELS {ED_LEVELS}", f"#define ED_CW {ED_CW}",
         f"#define ED_CH {ED_CH}", f"#define ED_DT_MS {int(ED_DT * 1000)}",
         f"#define FL_W {FL_W}", f"#define FL_H {FL_H}", f"#define FL_FRAMES {FL_FRAMES}", f"#define FL_LEVELS {FL_LEVELS}",
         f"#define FL_GC {FL_GC}", f"#define FL_GR {FL_GR}", f"#define FL_ROWS {FL_ROWS}",
         "#define FL_I {" + ",".join(f"{v}f" for v in FL_I) + "}",
         "#define BG_RUN_X 102", "#define BG_RUN_W 218", "#define BG_TITLE_W 160", "#define BG_TITLE_H 120",
         "extern const char ce_art_vars[CE_ART_NVARS][9];", "extern const uint16_t ce_palette[176];", "#endif"]
    open(os.path.join(outdir, "ce_art.h"), "w").write("\n".join(h) + "\n")
    total = sum(len(b) for b in blobs.values())
    print(f"mkart: {n} sprites, {len(order)} art appvars, {total} bytes in all appvars")


if __name__ == "__main__":
    main()
