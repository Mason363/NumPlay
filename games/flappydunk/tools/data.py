#!/usr/bin/env python3
"""Makes src/data.c and src/data.h from Flappy Dunk's own files.

  python3 -I tools/data.py GAMESRC src

GAMESRC is the folder of what was taken out of the game's Android build (see its spec/): sprites/ (the
game's pictures), ex/ar/ExportedProject (AssetRipper's export: sprite assets with their pivots and
pixels per unit, textures), ex/mb (raw MonoBehaviours: the Parameters curves), text_challenges.txt (the 51
challenges), ex/ar/.../AnimationClip/WTFHoop.anim (the mad hoop), and fonts/LilitaOne-Regular.ttf.

Pictures are stored as RGB565 and 8-bit alpha, at a known number of pixels per world unit:
balls and wings at 80/3 (a ball's 1.2 units in 32 pixels), hoop halves at 24 across and 12 down (their
Net's 0.5 already applied). Fonts are Lilita One, 4-bit coverage, in the launcher's np_font_t layout,
with an outlined copy of each."""
import math
import re
import struct
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFont

SRC = Path(sys.argv[1]).expanduser()
OUT = Path(sys.argv[2])
EXP = SRC / "ex/ar/ExportedProject/Assets"
LINES_C = []
LINES_H = []

BALLS = ["normal", "yellow", "purple", "green", "red", "blue", "pink", "black", "white", "rainbow", "football", "earth",
         "moon", "baseball", "bowling", "poolball", "beachball", "watermelon", "smiley", "poker", "cd", "yinyang",
         "fdban", "pizza", "buoy", "brain"]
WINGS = ["wing", "wingPink", "wingOrange", "wingGreen", "wingRed", "wingRainbow", "wingBlack", "wingPurple",
         "wingSmile", "wingInfinity"]
HOOPS = ["", "Black", "Pink", "Blue", "Green", "Purple", "Orange", "Rainbow", "Smile"]
# the Fevers collection's colours (Skin.Color), and a light core for each
FEVERS = [("NormalFever", (1.0, 0.53, 0.09)), ("YellowFever", (1.0, 0.97, 0.02)), ("PinkFever", (0.96, 0.23, 1.0)),
          ("GreenFever", (0.25, 1.0, 0.10)), ("RedFever", (0.91, 0.06, 0.05)), ("PurpleFever", (0.47, 0.0, 0.77))]

BALL_PPU = 80 / 3  # pixels per world unit for balls and wings: 1.2 units in 32 px
HOOP_PPU_X, HOOP_PPU_Y = 24, 12  # hoop halves, in the Net's own units (x times its width, y already halved)


def guid_files():
    m = {}
    for p in (EXP / "Texture2D").glob("*.meta"):
        g = re.search(r"guid: (\w+)", p.read_text(errors="replace"))
        if g:
            m[g.group(1)] = p.with_suffix("")
    return m


GUIDS = guid_files()


def sprite(name):
    """The sprite's picture (RGBA), its pivot in its pixels (from the top left) and its pixels per unit."""
    t = (EXP / "Sprite" / f"{name}.asset").read_text(errors="replace")
    ppu = float(re.search(r"m_PixelsToUnits: ([\d.]+)", t).group(1))
    pv = [float(v) for v in re.search(r"m_Pivot: \{x: ([-\d.e]+), y: ([-\d.e]+)\}", t).groups()]
    r = [float(v) for v in re.search(r"m_Rect:\s+serializedVersion: 2\s+x: ([-\d.e]+)\s+y: ([-\d.e]+)\s+"
                                     r"width: ([-\d.e]+)\s+height: ([-\d.e]+)", t).groups()]
    g = re.search(r"m_RD:\s+serializedVersion: \d+\s+texture: \{fileID: \d+, guid: (\w+)", t).group(1)
    tex = Image.open(GUIDS[g]).convert("RGBA")
    x0, w, h = round(r[0]), round(r[2]), round(r[3])
    y0 = tex.height - round(r[1]) - h
    im = tex.crop((x0, y0, x0 + w, y0 + h))
    return im, (pv[0] * w, (1 - pv[1]) * h), ppu


def premul_resize(im, size):
    a = np.asarray(im.convert("RGBA")).astype(np.float64) / 255
    rgb = a[..., :3] * a[..., 3:4]
    big = np.concatenate([rgb, a[..., 3:4]], axis=2)
    chans = [Image.fromarray((big[..., k] * 255).astype(np.uint8)).resize(size, Image.LANCZOS) for k in range(4)]
    out = np.stack([np.asarray(c).astype(np.float64) / 255 for c in chans], axis=2)
    alpha = np.clip(out[..., 3], 0, 1)
    rgb = np.where(alpha[..., None] > 0.004, out[..., :3] / np.maximum(alpha[..., None], 1e-6), 0)
    return np.clip(rgb, 0, 1), alpha


def rgb565(r, g, b):
    return (int(r * 255 + 0.5) & 0xF8) << 8 | (int(g * 255 + 0.5) & 0xFC) << 3 | int(b * 255 + 0.5) >> 3


def c_array(ctype, name, values, per=24):
    LINES_C.append(f"static const {ctype} {name}[{len(values)}] = {{")
    for i in range(0, len(values), per):
        LINES_C.append("  " + ",".join(str(v) for v in values[i:i + per]) + ",")
    LINES_C.append("};")


IMGS = []


def image(cname, rgb, alpha, pivot):
    """an fd_img_t: RGB565 and 8-bit alpha (alpha 0 where nothing is drawn), its pivot in pixels"""
    h, w = alpha.shape
    cols, alph = [], []
    for y in range(h):
        for x in range(w):
            a = alpha[y, x]
            alph.append(int(round(a * 255)))
            cols.append(rgb565(*rgb[y, x]) if a > 0.004 else 0)
    c_array("uint16_t", f"{cname}_c", cols, 16)
    c_array("uint8_t", f"{cname}_a", alph, 32)
    LINES_C.append(f"const fd_img_t {cname} = {{{w}, {h}, {round(pivot[0] * 4)}, {round(pivot[1] * 4)}, "
                   f"{cname}_c, {cname}_a}};")
    LINES_H.append(f"extern const fd_img_t {cname};")
    IMGS.append(w * h * 3)


def scaled(cname, name, ppu_out, sx=1.0, sy=1.0, png=None, pad=1):
    """a game sprite at ppu_out pixels per world unit (times sx, sy), pivot kept"""
    im, (px, py), ppu = sprite(name)
    if png:
        im = Image.open(png).convert("RGBA")
    k_x, k_y = ppu_out / ppu * sx, ppu_out / ppu * sy
    w, h = max(1, round(im.width * k_x)), max(1, round(im.height * k_y))
    rgb, a = premul_resize(im, (w, h))
    if pad:  # (a clear border: the rotated sampler reads none of the picture's edge as solid)
        rgb = np.pad(rgb, ((pad, pad), (pad, pad), (0, 0)))
        a = np.pad(a, ((pad, pad), (pad, pad)))
    image(cname, rgb, a, (px * w / im.width + pad, py * h / im.height + pad))
    return w, h


def png_image(cname, path, size, pivot=None):
    im = Image.open(path).convert("RGBA")
    rgb, a = premul_resize(im, size)
    image(cname, rgb, a, pivot or (size[0] / 2, size[1] / 2))


# ------------------------------------------------------------------ pictures
def pictures():
    LINES_H.append(f"#define FD_NBALLS {len(BALLS)}")
    LINES_H.append(f"#define FD_NWINGS {len(WINGS)}")
    LINES_H.append(f"#define FD_NHOOPS {len(HOOPS)}")
    LINES_H.append(f"#define FD_NFEVERS {len(FEVERS)}")
    for i, b in enumerate(BALLS):
        scaled(f"fd_ball_{i}", b, BALL_PPU * 0.6, pad=0)
    LINES_C.append("const fd_img_t *const fd_balls[FD_NBALLS] = {" + ",".join(f"&fd_ball_{i}" for i in range(len(BALLS))) + "};")
    for i, wg in enumerate(WINGS):
        # the wing's transform: scale 1.4 of the ball's 0.6
        scaled(f"fd_wing_{i}", wg, BALL_PPU * 0.84)
    LINES_C.append("const fd_img_t *const fd_wings[FD_NWINGS] = {" + ",".join(f"&fd_wing_{i}" for i in range(len(WINGS))) + "};")
    for i, hp in enumerate(HOOPS):
        scaled(f"fd_hoopb_{i}", f"hoop{hp}Back", HOOP_PPU_X, 1, HOOP_PPU_Y / HOOP_PPU_X)
        scaled(f"fd_hoopf_{i}", f"hoop{hp}Front", HOOP_PPU_X, 1, HOOP_PPU_Y / HOOP_PPU_X)
        png_image(f"fd_hoopstar_{i}", SRC / "sprites" / f"hoopStar{hp or ''}.png", (9, 9))
    LINES_C.append("const fd_img_t *const fd_hoop_back[FD_NHOOPS] = {" + ",".join(f"&fd_hoopb_{i}" for i in range(len(HOOPS))) + "};")
    LINES_C.append("const fd_img_t *const fd_hoop_front[FD_NHOOPS] = {" + ",".join(f"&fd_hoopf_{i}" for i in range(len(HOOPS))) + "};")
    LINES_C.append("const fd_img_t *const fd_hoop_star[FD_NHOOPS] = {" + ",".join(f"&fd_hoopstar_{i}" for i in range(len(HOOPS))) + "};")
    for name, size in (("fd_icon_flames", (20, 18)), ("fd_flames_big", (40, 36))):
        png_image(name, SRC / "sprites/flames.png", size)
    LINES_C.append("const uint32_t fd_fever_rgb[FD_NFEVERS] = {" + ",".join(
        "0x%02X%02X%02X" % tuple(round(c * 255) for c in col) for _, col in FEVERS) + "};")
    # the title's winged ball, a star, the shop's tile, the clouds
    png_image("fd_logo", SRC / "sprites/ball.png", (110, 102))
    png_image("fd_star", SRC / "sprites/star.png", (22, 21))
    png_image("fd_star_small", SRC / "sprites/star.png", (13, 12))
    png_image("fd_tile", SRC / "sprites/background_tile.png", (64, 64), (0, 0))
    png_image("fd_cloud", SRC / "sprites/cloud_corner.png", (40, 64), (0, 0))
    # icons for the menus, from the game's own sprites
    png_image("fd_icon_balls", SRC / "sprites/balls.png", (22, 22))
    png_image("fd_icon_hoops", SRC / "sprites/hoops.png", (26, 12))
    png_image("fd_icon_wings", SRC / "sprites/wings.png", (19, 23))
    png_image("fd_icon_challenge", SRC / "sprites/swish_challenge_icon.png", (26, 26))


def wall():
    """The brick wall (background.png, 1024 px for 20 world units: 240 px at 12 px a unit) as runs of 3 shades
    on rows: fd_wall_rows[y] indexes fd_wall_runs, (x, length, shade 1..3)"""
    im = Image.open(SRC / "sprites/Background.png").convert("L").resize((240, 240), Image.LANCZOS)
    v = np.asarray(im).astype(np.float64)
    shade = np.clip(np.round((249 - v) / (249 - 115) * 3), 0, 3).astype(int)
    runs, rows = [], []
    for y in range(240):
        rows.append(len(runs) // 3)
        x = 0
        while x < 240:
            s = shade[y, x]
            if not s:
                x += 1
                continue
            x1 = x
            while x1 < 240 and shade[y, x1] == s:
                x1 += 1
            runs += [x, x1 - x, s]
            x = x1
    rows.append(len(runs) // 3)
    c_array("uint8_t", "fd_wall_runs_", runs, 30)
    c_array("uint16_t", "fd_wall_rows_", rows, 30)
    LINES_C.append("const uint8_t *const fd_wall_runs = fd_wall_runs_;")
    LINES_C.append("const uint16_t *const fd_wall_rows = fd_wall_rows_;")
    LINES_H.append("extern const uint8_t *const fd_wall_runs;\nextern const uint16_t *const fd_wall_rows;")
    IMGS.append(len(runs) + 2 * len(rows))


# ------------------------------------------------------------------ fonts (Lilita One)
UP = 4


def load_font(cap):
    path = str(SRC.parent / "fonts" / "LilitaOne-Regular.ttf")
    for size in range(6, 400):
        f = ImageFont.truetype(path, size * UP)
        bb = f.getbbox("H", anchor="ls")
        if -bb[1] >= cap * UP - UP // 2:
            return f
    raise SystemExit("font size")


def glyph(f, ch, spacing, outline):
    adv = (f.getlength(ch) + spacing) / UP
    bb = f.getbbox(ch, anchor="ls")
    if bb[2] <= bb[0] or bb[3] <= bb[1]:
        return None, round(adv)
    pad = (2 + outline + 1) * UP
    x0, y0 = pad - bb[0], pad - bb[1]
    x0 += (-x0) % UP
    y0 += (-y0) % UP
    w, h = x0 + bb[2] + pad, y0 + bb[3] + pad
    w += (-w) % UP
    h += (-h) % UP
    im = Image.new("L", (w, h), 0)
    ImageDraw.Draw(im).text((x0, y0), ch, font=f, fill=255, anchor="ls")
    a = np.asarray(im).astype(np.float32) / 255
    if outline:
        r = outline * UP
        g = np.zeros_like(a)
        for dy in range(-r, r + 1):
            for dx in range(-r, r + 1):
                if dx * dx + dy * dy <= r * r + r:
                    g = np.maximum(g, np.roll(np.roll(a, dy, 0), dx, 1))
        a = np.maximum(g, np.roll(g, UP, 0))  # (one more below: the shadow)
    a = a.reshape(h // UP, UP, w // UP, UP).mean(axis=(1, 3))
    q = np.clip(np.round(a * 15), 0, 15).astype(np.uint8)
    ys, xs = np.nonzero(q)
    if not len(ys):
        return None, round(adv)
    q = q[ys.min():ys.max() + 1, xs.min():xs.max() + 1]
    return (q, xs.min() - x0 // UP, ys.min() - y0 // UP), round(adv)


def font(name, cap, spacing, outline, first=32, last=126):
    f = load_font(cap)
    data, glyphs, asc, desc = bytearray(), [], 0, 0
    for c in range(first, last + 1):
        g, adv = glyph(f, chr(c), spacing * UP / 4, outline)
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
    c_array("uint8_t", f"{name}_data", list(data), 32)
    LINES_C.append(f"static const np_glyph_t {name}_glyphs[{len(glyphs)}] = {{")
    LINES_C.extend([f"  {{{w}, {h}, {ox}, {oy}, {adv}, {off}}}," for w, h, ox, oy, adv, off in glyphs])
    LINES_C.append("};")
    LINES_C.append(f"const np_font_t {name} = {{{first}, {last - first + 1}, {asc}, {desc}, {name}_glyphs, {name}_data}};")
    LINES_H.append(f"extern const np_font_t {name};")
    IMGS.append(len(data) + 7 * len(glyphs))


def fonts():
    # the score, the titles, the buttons and texts: each plain and outlined (the outline is drawn under the
    # letters, offset by its width so both start at the pen)
    for name, cap, spacing, ol in (("fd_font_big", 24, 2, 2), ("fd_font_mid", 13, 1, 2), ("fd_font_small", 8, 2, 1)):
        font(name, cap, spacing, 0)
        font(name + "_o", cap, spacing, ol)


# ------------------------------------------------------------------ the endless mode's parameters
def parameters():
    """Parameters' MinMaxParameter list (ex/mb): name, type, min, max, scoreMin, scoreMax, minCurve keys"""
    b = next((SRC / "ex/mb").glob("*_Parameters.bin")).read_bytes()[32:]
    p = 0
    n, = struct.unpack_from("<i", b, p)
    p += 4
    out = []
    for _ in range(n):
        ln, = struct.unpack_from("<i", b, p)
        name = b[p + 4:p + 4 + ln].decode()
        p = (p + 4 + ln + 3) & ~3
        typ, mn, mx, s0, s1, nk = struct.unpack_from("<iffiii", b, p)
        p += 24
        keys = []
        for _ in range(nk):
            t, v, ins, outs, wm, inw, outw = struct.unpack_from("<ffffiff", b, p)
            keys.append((t, v, ins, outs))
            p += 28
        p += 12  # pre/post infinity, rotation order
        out.append((name, typ, mn, mx, s0, s1, keys))
    names = {"Scroll Speed": "P_SCROLL", "Moving Hoop Speed": "P_MOVE_SPEED", "Hoop Height": "P_HEIGHT",
             "Hoop Width": "P_WIDTH", "Moving Hoop Probability": "P_MOVING", "Hoop Angle": "P_ANGLE",
             "Lateral Hoop Probability": "P_LATERAL", "Gravity": "P_GRAVITY", "Flap Force": "P_FLAP"}
    LINES_H.append("enum { " + ", ".join(names[o[0]] for o in out) + ", P_COUNT };")
    keys = []
    rows = []
    for name, typ, mn, mx, s0, s1, ks in out:
        rows.append(f"  {{{typ}, {mn!r}f, {mx!r}f, {s0}, {s1}, {len(keys)}, {len(ks)}}}, /* {name} */")
        keys += ks
    LINES_C.append(f"const fd_param_t fd_params[{len(out)}] = {{")
    LINES_C.extend(rows)
    LINES_C.append("};")
    LINES_C.append(f"const fd_key_t fd_param_keys[{len(keys)}] = {{")
    LINES_C.extend([f"  {{{t!r}f, {v!r}f, {i!r}f, {o!r}f}}," for t, v, i, o in keys])
    LINES_C.append("};")


# ------------------------------------------------------------------ the mad hoop (WTFHoop.anim)
def hermite(keys, t):
    """Unity's curve between keys (time, value, inSlope, outSlope)"""
    if t <= keys[0][0]:
        return keys[0][1]
    for (t0, v0, _, o0), (t1, v1, i1, _) in zip(keys, keys[1:]):
        if t0 <= t <= t1:
            d = t1 - t0
            s = (t - t0) / d
            h00, h10, h01, h11 = 2 * s ** 3 - 3 * s ** 2 + 1, s ** 3 - 2 * s ** 2 + s, -2 * s ** 3 + 3 * s ** 2, s ** 3 - s ** 2
            return h00 * v0 + h10 * d * o0 + h01 * v1 + h11 * d * i1
    return keys[-1][1]


def wtf():
    t = (EXP / "AnimationClip/WTFHoop.anim").read_text()
    rot = t[t.index("m_RotationCurves:"):t.index("m_CompressedRotationCurves")]
    pos = t[t.index("m_PositionCurves:"):t.index("m_ScaleCurves")]
    key = r"time: ([-\d.e]+)\n\s+value: \{x: ([^,]+), y: ([^,]+), z: ([^,]+)(?:, w: ([^}]+))?\}\n\s+inSlope: \{x: ([^,]+), y: ([^,]+), z: ([^,]+)(?:, w: [^}]+)?\}\n\s+outSlope: \{x: ([^,]+), y: ([^,]+), z: ([^,]+)(?:, w: [^}]+)?\}"
    rk = [(float(m[0]), 2 * math.degrees(math.atan2(float(m[3]), float(m[4])))) for m in re.findall(key, rot)]
    pk = re.findall(key, pos)
    px = [(float(m[0]), float(m[1]), float(m[5]), float(m[8])) for m in pk]
    py = [(float(m[0]), float(m[2]), float(m[6]), float(m[9])) for m in pk]
    length = rk[-1][0]
    n = int(round(length * 15)) + 1
    ang, xs, ys = [], [], []
    for i in range(n):
        tt = i / 15
        # rotation keys come every 1/15 s
        j = min(range(len(rk)), key=lambda k: abs(rk[k][0] - tt))
        ang.append(round(rk[j][1] * 64))
        xs.append(round(hermite(px, tt) * 256))
        ys.append(round(hermite(py, tt) * 256))
    LINES_H.append(f"#define FD_WTF_N {n} /* 15 a second, looping */")
    c_array("int16_t", "fd_wtf_angle_", ang)
    c_array("int16_t", "fd_wtf_x_", xs)
    c_array("int16_t", "fd_wtf_y_", ys)
    LINES_C.append("const int16_t *const fd_wtf_angle = fd_wtf_angle_, *const fd_wtf_x = fd_wtf_x_, *const fd_wtf_y = fd_wtf_y_;")
    LINES_H.append("extern const int16_t *const fd_wtf_angle, *const fd_wtf_x, *const fd_wtf_y; /* 64ths of a degree, 256ths of a unit */")


# ------------------------------------------------------------------ the 51 challenges (text_challenges.txt)
def challenges():
    t = (SRC / "text_challenges.txt").read_text()
    chs = re.findall(r"<challenge>(.*?)</challenge>", t, re.S)
    assert len(chs) == 51, len(chs)
    hoops, rows, texts = [], [], []
    types = {"static": 0, "vertical": 1, "lateral": 2, "wtf": 3}
    for c in chs:
        head = re.split(r"<hoop", c)[0]
        tags = dict(re.findall(r'<(\w+) value="([^"]*)"', head))
        hs = re.findall(r'<hoop value="(\w+)"\s*(/>|>(.*?)</hoop>)', c, re.S)
        first = len(hoops)
        for typ, _, body in hs:
            v = dict(re.findall(r'<(\w+) value="([^"]*)"', body or ""))
            unknown = set(v) - {"height", "distance", "angle", "width", "speed", "bound"}
            assert not unknown, unknown
            hoops.append((types[typ], float(v.get("height", 0)), float(v.get("angle", 0)), float(v.get("width", 0.8)),
                          float(v.get("speed", 1)), float(v.get("distance", 9)), float(v.get("bound", 2))))
        win = tags.get("winSwishes", "")
        cond, amount = (0, -1) if not win else ((0 if win[0] == "=" else 1), int(win[1:]))
        mirror = {"": 0, "horizontal": 1, "vertical": 2, "both": 3}[tags.get("mirror", "")]
        unknown = set(tags) - {"difficulty", "continue", "description", "saveID", "winSwishes", "scrollSpeed",
                               "gravity", "flapForce", "finishDistance", "narrow", "pixel", "mirror"}
        assert not unknown, unknown
        rows.append(f"  {{{int(tags['difficulty'])}, {cond}, {amount}, {float(tags.get('scrollSpeed', 4))!r}f, "
                    f"{float(tags.get('gravity', -40))!r}f, {float(tags.get('flapForce', 12))!r}f, "
                    f"{float(tags.get('finishDistance', 9))!r}f, {int(tags.get('narrow') == 'true')}, "
                    f"{int(tags.get('pixel') == 'true')}, {mirror}, {first}, {len(hoops) - first}}},")
        texts.append(tags["description"])
    LINES_C.append(f"const fd_challenge_t fd_challenges[51] = {{")
    LINES_C.extend(rows)
    LINES_C.append("};")
    LINES_C.append(f"const fd_choop_t fd_choops[{len(hoops)}] = {{")
    LINES_C.extend([f"  {{{a}, {b!r}f, {c!r}f, {d!r}f, {e!r}f, {f!r}f, {g!r}f}}," for a, b, c, d, e, f, g in hoops])
    LINES_C.append("};")
    LINES_C.append("const char *const fd_challenge_text[51] = {")
    LINES_C.extend(["  " + '"' + s.replace('"', '\\"') + '",' for s in texts])
    LINES_C.append("};")


def main():
    LINES_H[:0] = ["/* Made by tools/data.py from Flappy Dunk's own files (see its docstring). */",
                   "#ifndef FD_DATA_H", "#define FD_DATA_H", "#include <stdint.h>", '#include "font.h"', "",
                   "typedef struct {", "  uint8_t w, h;", "  int16_t px, py; /* the pivot, in quarter pixels */",
                   "  const uint16_t *c; /* RGB565 */", "  const uint8_t *a; /* alpha */", "} fd_img_t;",
                   "typedef struct { float t, v, in, out; } fd_key_t;",
                   "typedef struct { int8_t type; float min, max; int16_t start, end, key, nkeys; } fd_param_t;",
                   "typedef struct {", "  uint8_t difficulty, condition; /* 0 exactly, 1 at least */",
                   "  int8_t swishes; /* -1: no swish goal */", "  float scroll, gravity, flap, finish;",
                   "  uint8_t narrow, pixel, mirror; /* mirror: 1 horizontal, 2 vertical, 3 both */",
                   "  uint16_t first, count;", "} fd_challenge_t;",
                   "typedef struct { uint8_t type; float height, angle, width, speed, distance, bound; } fd_choop_t;",
                   ""]
    LINES_C[:0] = ["/* Made by tools/data.py from Flappy Dunk's own files: its pictures (Voodoo), and the Lilita One font",
                   " * by Juan Montoreano (SIL Open Font License 1.1). */", '#include "data.h"', ""]
    pictures()
    wall()
    fonts()
    parameters()
    wtf()
    challenges()
    LINES_H.append("extern const fd_img_t *const fd_balls[FD_NBALLS], *const fd_wings[FD_NWINGS];")
    LINES_H.append("extern const fd_img_t *const fd_hoop_back[FD_NHOOPS], *const fd_hoop_front[FD_NHOOPS], "
                   "*const fd_hoop_star[FD_NHOOPS];")
    LINES_H.append("extern const uint32_t fd_fever_rgb[FD_NFEVERS];")
    LINES_H.append("extern const fd_param_t fd_params[P_COUNT];\nextern const fd_key_t fd_param_keys[];")
    LINES_H.append("extern const fd_challenge_t fd_challenges[51];\nextern const fd_choop_t fd_choops[];")
    LINES_H.append("extern const char *const fd_challenge_text[51];")
    LINES_H.append("#endif")
    (OUT / "data.c").write_text("\n".join(LINES_C) + "\n")
    (OUT / "data.h").write_text("\n".join(LINES_H) + "\n")
    print(f"data.c: {sum(IMGS)} bytes of pictures and fonts")


main()
