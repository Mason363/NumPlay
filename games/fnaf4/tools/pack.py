#!/usr/bin/env python3
"""Five Nights at Freddy's 4: its pictures and fonts -> src/data.bin, src/data.h and src/anims.h.

    python3 -I tools/pack.py [CCI]      CCI: the game's Application.cci, from its iOS package; by default
                                        ~/Documents/Assets/gamesrc/fnaf4/dl/Application.cci

Every picture is the game's own, read from the application by tools/ccn.py, at half size. The bedroom is 1000 x 480
in the game and pans: here it is 500 x 240, kept from x 32 to 464 (all the calculator's 320 ever shows of it). The
doors, the closet, the bed and the scares are the game's 800 x 480 screen: here the middle 640 of it, 320 x 240.

data.bin: "FNF4", the number of sections, their offsets, then the sections (SEC_* in data.h):
- HUFF: the four Huffman tables (Y DC, Y AC, colour DC, colour AC), 1424 bytes each (tools/codec.py)
- QUANT: for each quality, the Y and colour tables (64 u16 each, natural order, scaled for the IDCT)
- PICS: 12 bytes a picture (PIC_*): its width and height in 16 x 16 squares, its quality, 0, the picture it
  shares squares with (u16, 0xFFFF none), how many squares it has of its own then (u16) and where its table
  starts (u32)
- TABLE: a whole picture's squares (3 bytes each: where in BANK), or a picture sharing another's: the squares
  that differ (u16 the square's index, then 3 bytes)
- BANK: the squares, each from a byte boundary
- FONTS: for each font (FONT_*) its line height, baseline and 95 glyphs (32 to 126): advance, width, height,
  x and y from the top of the line (s8), 0, the offset of its 4-bit alpha rows (u32, from the section)
- IMGS: small pictures with alpha (IMG_*): width, height (u16), then 4-bit alpha

anims.h: the night's animations as the game plays them (AN_*): their pictures, speed (a picture lasts 100 / speed
frames) and how many times they play (0: for ever)."""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ccn  # noqa: E402
import codec as C  # noqa: E402

OUT_BIN = os.path.join(HERE, "..", "src", "data.bin")
OUT_H = os.path.join(HERE, "..", "src", "data.h")
OUT_ANIMS = os.path.join(HERE, "..", "src", "anims.h")
ROOT = os.path.join(HERE, "..", "..", "..")
CCI = os.path.expanduser("~/Documents/Assets/gamesrc/fnaf4/dl/Application.cci")
SECTIONS = ["HUFF", "QUANT", "PICS", "TABLE", "BANK", "FONTS", "IMGS"]
QUALITY = {"scene": 80, "scare": 70, "face": 76}
FONT_DIRS = [os.environ.get("FONTS", ""), "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/TTF",
             "/usr/local/share/fonts", "/Library/Fonts", os.path.expanduser("~/Library/Fonts"),
             os.path.expanduser("~/Documents/Assets/gamesrc/fonts/dejavu")]
L = "level"
BED_X0, BED_W = 32, 432   # the part of the half-size bedroom kept


def find_font(name):
    for d in FONT_DIRS:
        p = os.path.join(d, name) if d else ""
        if p and os.path.exists(p):
            return p
    raise SystemExit(f"pack: {name} not found (the DejaVu fonts; FONTS=<their folder>)")


def font(size, bold=False, mono=False, serif=False):
    name = "DejaVu" + ("Serif" if serif else "SansMono" if mono else "Sans") + ("-Bold" if bold else "") + ".ttf"
    return ImageFont.truetype(find_font(name), size)


def pad16(a):
    h, w = a.shape[:2]
    H, W = -(-h // 16) * 16, -(-w // 16) * 16
    return np.pad(a, ((0, H - h), (0, W - w), (0, 0)), mode="edge")


def arr(im):
    return np.asarray(im.convert("RGB")).astype(np.float64)


# ------------------------------------------------------------------ pictures
class Pic:
    def __init__(self, name, a, quality, base=None, refs=()):
        self.name, self.q, self.base = name, quality, base
        self.w, self.h = a.shape[1], a.shape[0]
        self.a = pad16(a)
        self.refs = [r for r in ([base] if base else []) + list(refs) if r]
        self.mw, self.mh = self.a.shape[1] // 16, self.a.shape[0] // 16


PICS = []
BY_NAME = {}


def pic(name, a, quality="scene", base=None, refs=()):
    p = Pic(name, np.clip(a, 0, 255), quality, base, refs)
    PICS.append(p)
    BY_NAME[name] = p
    return p


def same_square(a, b, strict):
    d = np.abs(a - b)
    return d.max() <= (6 if strict else 14) and d.mean() <= (1.2 if strict else 3.0)


def encode_all():
    qt = {k: (C.qtable(C.STD_LUMA, v), C.qtable(C.STD_CHROMA, v)) for k, v in QUALITY.items()}
    qnames = list(QUALITY)
    for p in PICS:
        y, cb, cr = C.ycbcr(p.a)
        ql, qc = qt[p.q]
        yq = C.quantize(C.blocks_of(y), ql, 0.18)
        cbq = C.quantize(C.blocks_of(cb), qc, 0.22)
        crq = C.quantize(C.blocks_of(cr), qc, 0.22)
        p.sq = []
        for my in range(p.mh):
            for mx in range(p.mw):
                src = None
                tile = p.a[my * 16:my * 16 + 16, mx * 16:mx * 16 + 16]
                for r in p.refs:
                    if mx < r.mw and my < r.mh and r.q == p.q:
                        if same_square(tile, r.a[my * 16:my * 16 + 16, mx * 16:mx * 16 + 16], r is not p.base):
                            src = r.sq[my * r.mw + mx]
                            break
                if src is None:
                    blocks = [yq[2 * my + i, 2 * mx + j] for i in (0, 1) for j in (0, 1)]
                    src = ("own", tuple(C.mcu_symbols(blocks, cbq[my, mx], crq[my, mx])))
                p.sq.append(src)
    freq = [[{}, {}], [{}, {}]]
    seen = set()
    for p in PICS:
        for s in p.sq:
            if id(s) in seen:
                continue
            seen.add(id(s))
            for comp, tab, sym, _, _ in s[1]:
                f = freq[comp][tab]
                f[sym] = f.get(sym, 0) + 1
    tables, huff = [[None, None], [None, None]], b""
    for comp in (0, 1):
        for tab in (0, 1):
            codes, bits, vals = C.canonical(C.huffman_lengths(freq[comp][tab]))
            tables[comp][tab] = codes
            huff += C.decode_tables(bits, vals)
    bank, where, by_id = bytearray(), {}, {}
    for p in PICS:
        p.off = []
        for s in p.sq:
            if id(s) not in by_id:
                data = C.encode_mcu(s[1], tables)
                if data not in where:
                    where[data] = len(bank)
                    bank += data
                by_id[id(s)] = where[data]
            p.off.append(by_id[id(s)])
    bank += bytes(8)
    assert len(bank) < 1 << 24
    quant = b""
    for k in qnames:
        for t in qt[k]:
            quant += b"".join(struct.pack("<H", (int(t[i]) * C.AANSCALES[i] + 2048) >> 12) for i in range(64))
    pics, table = b"", bytearray()
    for p in PICS:
        diff = []
        if p.base:
            b = p.base
            assert (b.mw, b.mh) == (p.mw, p.mh) and not b.base
            diff = [k for k in range(len(p.off)) if p.off[k] != b.off[k]]
        if p.base and 5 * len(diff) < 3 * len(p.off):
            pics += struct.pack("<BBBBHHI", p.mw, p.mh, qnames.index(p.q), 0, PICS.index(b), len(diff), len(table))
            for k in diff:
                table += struct.pack("<H", k) + p.off[k].to_bytes(3, "little")
        else:
            pics += struct.pack("<BBBBHHI", p.mw, p.mh, qnames.index(p.q), 0, 0xFFFF, 0, len(table))
            for o in p.off:
                table += o.to_bytes(3, "little")
    print(f"pictures: {len(PICS)}, squares {len(bank)} bytes, tables {len(table)} bytes")
    return huff, quant, pics, bytes(table), bytes(bank)


# ------------------------------------------------------------------ fonts and small pictures
def nibbles(alpha):
    h, w = alpha.shape
    out = bytearray()
    for r in range(h):
        row = list(alpha[r]) + [0]
        for c in range(0, w, 2):
            out.append(int(row[c]) | int(row[c + 1]) << 4)
    return bytes(out)


def to4(a):
    return np.clip(np.round(np.asarray(a, dtype=np.float64) / 255 * 15), 0, 15).astype(np.uint8)


def ttf_font(f, line=None):
    asc, desc = f.getmetrics()
    line = line or asc + desc
    glyphs = []
    for c in range(32, 127):
        ch = chr(c)
        adv = int(round(f.getlength(ch)))
        box = f.getbbox(ch)
        if box[2] <= box[0] or ch == " ":
            glyphs.append((adv, 0, 0, 0, 0, b""))
            continue
        im = Image.new("L", (box[2] - box[0], box[3] - box[1]))
        ImageDraw.Draw(im).text((-box[0], -box[1]), ch, font=f, fill=255)
        glyphs.append((adv, im.width, im.height, box[0], box[1], nibbles(to4(np.asarray(im)))))
    return line, asc, glyphs


def pack_fonts(fonts):
    blobs = []
    for line, asc, glyphs in fonts:
        b = struct.pack("<BBBB", line, asc, 32, 95)
        rows, offs = b"", []
        for adv, w, h, x, y, r in glyphs:
            offs.append(len(rows))
            rows += r
        g0 = len(b) + 12 * 95
        for (adv, w, h, x, y, r), o in zip(glyphs, offs):
            b += struct.pack("<BBBbbBHI", adv, w, h, x, y, 0, 0, g0 + o)
        blobs.append(b + rows)
    out = struct.pack("<I", len(fonts))
    p = 4 + 4 * len(fonts)
    for b in blobs:
        out += struct.pack("<I", p)
        p += len(b)
    return out + b"".join(blobs)


DEFINES = []


def define(name, value):
    DEFINES.append((name, value))


IMGS = []


def img(name, alpha):
    a = to4(alpha)
    h, w = a.shape
    define(f"IMG_{name}", len(IMGS))
    IMGS.append(struct.pack("<HHBBH", w, h, 0, 0, 0) + nibbles(a))


def pack_imgs():
    out = struct.pack("<I", len(IMGS))
    p = 4 + 4 * len(IMGS)
    for d in IMGS:
        out += struct.pack("<I", p)
        p += len(d) + (-len(d) % 4)
    return out + b"".join(d + bytes(-len(d) % 4) for d in IMGS)


def alpha_of(im, scale):
    """an RGBA picture's brightness times alpha, scaled, for IMGS"""
    im = im.convert("RGBA")
    im = im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))), Image.LANCZOS)
    a = np.asarray(im).astype(np.float64)
    return a[..., 3] * a[..., :3].max(axis=2) / 255


def colour_of(im):
    """an RGBA picture's main colour (its brightest opaque pixels), as 0xRRGGBB"""
    a = np.asarray(im.convert("RGBA")).reshape(-1, 4).astype(np.float64)
    a = a[a[:, 3] > 200]
    a = a[a[:, :3].max(axis=1) >= np.percentile(a[:, :3].max(axis=1), 75)]
    r, g, b = (int(round(v)) for v in a[:, :3].mean(axis=0))
    return r << 16 | g << 8 | b


# ------------------------------------------------------------------ the game's pictures, placed
APP = None


def half(im):
    return arr(im.convert("RGB").resize((im.width // 2, im.height // 2), Image.LANCZOS))


def screen(im):
    """the game's 800 x 480 screen at half size, its middle 320"""
    return half(im)[:, 40:360]


def bedroom(im):
    """the 1000 x 480 bedroom at half size, the part kept"""
    return half(im)[:, BED_X0:BED_X0 + BED_W]


def on_black(w, h, *parts):
    """a w x h black picture with (picture, x, y) parts pasted (their corner at x, y)"""
    c = Image.new("RGBA", (w, h), (0, 0, 0, 255))
    for im, x, y in parts:
        layer = Image.new("RGBA", (w, h))
        layer.paste(im.convert("RGBA"), (x, y))
        c.alpha_composite(layer)
    return c


def placed(h, x, y):
    """picture h with its hot spot at (x, y): (picture, its corner)"""
    hx, hy = APP.hotspot(h)
    return APP.picture(h), x - hx, y - hy


ANIMS = []          # (key, [picture indexes], speed, plays, wide)
BY_HANDLE = {}


def handle_pic(h, wide, quality, first, prev):
    """a picture of the game's by its number (the first of its animation's shares squares with the one before; the
    others list only where they differ from the first)"""
    key = (h, wide, quality)
    if key not in BY_HANDLE:
        im = APP.picture(h)
        a = bedroom(on_black(1000, 480, (im, 500 - APP.hotspot(h)[0], 240 - APP.hotspot(h)[1]))) if wide \
            else screen(on_black(800, 480, (im, 0, 0)))
        base = (first.base or first) if first else None
        BY_HANDLE[key] = pic(f"H{h}{'W' if wide else ''}", a, quality, base=base,
                             refs=[r for r in (prev, LAST.get(wide)) if r])
        LAST[wide] = BY_HANDLE[key]
    return BY_HANDLE[key]


LAST = {}


def anim(key, obj, a, wide=False, quality="scene", frame=L):
    """one of the game's animations: its pictures (each once, however many animations show it) and timing"""
    hs = APP.anim(obj, a, frame)
    speed, plays, _ = APP.speed(obj, a, frame)
    first = prev = None
    idx = []
    for h in hs:
        p = handle_pic(h, wide, quality, first, prev)
        idx.append(PICS.index(p))
        first, prev = first or p, p
    ANIMS.append((key, idx, speed, plays, int(wide)))


def night_pictures():
    for a in (0, 12, 13, 14, 15, 16, 17, 19):
        anim(f"ROOM_{a}", "Active", a, wide=True)
    for key, obj in (("OFF4", "off4"), ("ON4", "on4"), ("CREAK", "closetcreak"), ("DARKL", "dark left"),
                     ("DARKR", "dark right"), ("DARKC", "dark center")):
        anim(key, obj, 0, wide=True)
    for key, obj in (("CARPET", "carpet run"), ("LSTART", "left start"), ("RSTART", "right start"),
                     ("BHIDE", "bonnie hide"), ("CHIDE", "Chica hide"), ("BITE", "foxy bite"),
                     ("LEAVE3", "3leave"), ("LEAVE2", "2leave"), ("LEAVE1", "1leave")):
        anim(key, obj, 0)
    for a in range(12, 22):
        anim(f"PEEKL_{a}", "peek left", a)
        anim(f"PEEKR_{a}", "peek right", a)
    for a in [0] + list(range(12, 24)):
        anim(f"CLOSET_{a}", "closet peek", a)
    for a in (0, 12, 14, 16, 18, 19, 20, 21, 22, 23, 24, 25):
        anim(f"BED_{a}", "bed view", a)
    for a in (0, 12, 13, 14):
        anim(f"JUMP_{a}", "jumpscare", a, quality="scare")
    for key, obj in (("FREDDYATK", "freddy bedroom attack"), ("CUPCAKE", "cupcake attack"),
                     ("BONNIEATK", "bonnie bedroom attack"), ("FREDBEAR", "fredbear scare")):
        anim(key, obj, 0, quality="scare")
    for a in (0, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23):
        anim(f"PLUSH_{a}", "Active", a, frame="plushtrap game")
    pic("NIGHTMARE", screen(APP.picture(13)), "scare")
    img("WARNING", alpha_of(APP.picture(1175), 0.5))


def screens():
    # the title: the house, the logo over it (at the game's place, faint or less faint as it flickers), the
    # screen's right 640 so the logo shows whole
    def title(h, alpha):
        logo = APP.picture(h).copy()
        logo.putalpha(logo.getchannel("A").point(lambda v: v * alpha // 255))
        c = on_black(800, 480, (APP.picture(315), 0, 0), placed(h, 804, -2))
        return half(c)[:, 80:400]
    t = pic("TITLE", title(658, 120), "face")
    pic("TITLE_FAINT", title(658, 60), "face", base=t)
    for h, n in ((659, 1), (660, 2), (661, 3)):
        pic(f"TITLE_GLITCH{n}", title(h, 120), "face", base=t)
    define("TITLE_X0", 80)
    for nm, h in (("NEW", 730), ("CONTINUE", 737), ("SIXTH", 738), ("EXTRA", 731), ("ANIMATRONICS", 911),
                  ("MAKINGFOXY", 904), ("MAKINGFREDBEAR", 912), ("JUMPSCARES", 914), ("PLUSHTRAP", 913),
                  ("NIGHTMARE", 1133), ("TWENTY", 293), ("EXIT", 43)):
        img(f"M_{nm}", alpha_of(APP.picture(h), 0.5))
    define("MENU_RED", colour_of(APP.picture(730)))
    img("REDLINE", alpha_of(APP.picture(746), 0.5))
    img("STAR", alpha_of(APP.picture(937), 0.5))
    # the night's card: the bed in the dark, the black wipe the night's number sits on, "Night", the clock
    pic("WHATNIGHT", screen(on_black(800, 480, (APP.picture(319), 0, 0))), "face")
    img("NIGHTWORD", alpha_of(APP.picture(670), 0.5))
    img("CLOCK", alpha_of(APP.picture(656), 0.5))
    img("AM", alpha_of(APP.picture(652), 0.5))
    pic("GAMEOVER", screen(APP.picture(404)), "face")
    pic("TOOBAD", screen(on_black(800, 480, (APP.picture(46), 0, 0), placed(867, 404, 220))), "face")
    img("GREAT", alpha_of(APP.picture(868), 0.5))
    # 6 AM: the alarm clock's digits (red, the game's 0 to 9) and its colon
    for k, h in enumerate(APP.anim("num 1", 0, "night win")):
        img(f"DIGIT{k}", alpha_of(APP.picture(h), 0.5))
    img("COLON", alpha_of(APP.picture(678), 0.5))
    define("CLOCK_RED", colour_of(APP.picture(663)))
    # Fun with Plushtrap's card: the game's title line (its instructions are for a touch screen)
    card = screen(APP.picture(44))
    pic("PLUSHCARD", card, "face")
    # the box after the Nightmare: the box, its two locks (and their rattle), the words
    locks = lambda k: on_black(800, 480, (APP.picture(1294), 0, 0),
                               placed(APP.anim("Active 2", 12, "lockbox")[k], 363, 311),
                               placed(APP.anim("Active 2", 12, "lockbox")[k], 433, 312))
    b = pic("LOCKBOX", screen(locks(0)), "face")
    n = len(APP.anim("Active 2", 12, "lockbox"))
    for k in range(1, n):
        pic(f"LOCKBOX{k}", screen(locks(k)), "face", base=b)
    define("RATTLE_N", n)
    pic("LOCKBOX_OPEN", screen(on_black(800, 480, (APP.picture(1294), 0, 0))), "face", base=b)
    img("FORGOTTEN", alpha_of(APP.picture(733), 0.5))
    # the ending: the toys go one by one, the boy, the words at the top
    toys = [("Active 5", 1168, 524, 300), ("Active 4", 1167, 564, 260), ("Active 2", 1166, 644, 320),
            ("Active 24", 1169, 664, 260), ("Active 3", 1170, 356, 356)]
    for k in range(len(toys) + 3):
        parts = [(APP.picture(977), 0, 0), placed(1179, 24, 340)]
        parts += [placed(h, x, y) for _, h, x, y in toys[k:]]
        if k <= len(toys) + 1:
            parts.append(placed(1173 if k == len(toys) + 1 else 1172, 205, 361))
        c = on_black(800, 480, *parts)
        pic(f"ENDING{k}", half(c)[:, 40:360], "face", base=BY_NAME.get("ENDING0"))
    img("ARROW", alpha_of(APP.picture(1084), 0.5))
    # the extras: the animatronics standing, the making of Foxy and Fredbear, Plushtrap's picture
    def fit(h, w, hh):
        im = APP.picture(h).convert("RGBA")
        s = min(w / im.width, hh / im.height)
        im = im.resize((max(1, round(im.width * s)), max(1, round(im.height * s))), Image.LANCZOS)
        c = Image.new("RGBA", (w, hh), (0, 0, 0, 255))
        c.alpha_composite(im, ((w - im.width) // 2, hh - im.height))
        return arr(c)
    for k, a in enumerate((16, 0, 12, 13, 14, 15, 17, 18)):
        pic(f"ANIMATRONIC{k}", fit(APP.anim("animatronic", a, "extras")[0], 176, 208), "face")
    for nm, obj in (("FOXY", "making foxy 2"), ("FREDBEAR", "making foxy 3")):
        for k, h in enumerate(APP.anim(obj, 0, "extras")):
            pic(f"MAKING{nm}{k}", fit(h, 176, 176), "face")
    pic("PLUSHTRAP", fit(42, 176, 176), "face")


def icon():
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from make_icon import corner_mask
    im = APP.picture(APP.anim("animatronic", 16, "extras")[0]).convert("RGBA")
    face = im.crop((im.width // 2 - 70, 10, im.width // 2 + 70, 150))
    c = Image.new("RGBA", face.size, (0, 0, 0, 255))
    c.alpha_composite(face)
    c = c.convert("RGB").resize((55, 56), Image.LANCZOS)
    c.putalpha(corner_mask(55, 56))
    c.save(os.path.join(HERE, "..", "src", "icon.png"), optimize=True)


def write_anims():
    flat, lines = [], []
    for key, idx, speed, plays, wide in ANIMS:
        lines.append((key, len(flat), len(idx), speed, plays, wide))
        flat += idx
    h = ["/* Made by tools/pack.py: the game's animations (AN_*), its own speeds and plays. Included once (night.c). */",
         "#ifndef FNAF4_ANIMS_H", "#define FNAF4_ANIMS_H", "#include <stdint.h>", "",
         "typedef struct {", "  uint16_t first;   /* in ANIM_PICS */",
         "  uint8_t n, speed, plays, wide;   /* a picture lasts 100 / speed frames; plays 0: for ever; wide: the bedroom's */",
         "} AnimDef;", ""]
    h.append("enum {")
    h += [f"  AN_{k}," for k, *_ in lines]
    h += ["  AN_COUNT", "};", ""]
    h.append("static const uint16_t ANIM_PICS[] = {")
    for i in range(0, len(flat), 16):
        h.append("  " + ", ".join(str(v) for v in flat[i:i + 16]) + ",")
    h += ["};", "", "static const AnimDef ANIMS[AN_COUNT] = {"]
    h += [f"  {{{f}, {n}, {s}, {p}, {w}}},   /* {k} */" for k, f, n, s, p, w in lines]
    h += ["};", "", "#endif", ""]
    open(OUT_ANIMS, "w").write("\n".join(h))


def main():
    global APP
    APP = ccn.App(sys.argv[1] if len(sys.argv) > 1 else CCI)
    night_pictures()
    screens()
    icon()
    huff, quant, pics, table, bank = encode_all()
    fonts = [
        ttf_font(font(11, bold=True, mono=True)),   # FONT_HUD
        ttf_font(font(17, mono=True)),              # FONT_BIG
        ttf_font(font(11)),                         # FONT_TEXT
        ttf_font(font(24, bold=True)),              # FONT_CLOCK
        ttf_font(font(40, serif=True)),             # FONT_HUGE
        ttf_font(font(14, serif=True)),             # FONT_SERIF
    ]
    sections = [huff, quant, pics, table, bank, pack_fonts(fonts), pack_imgs()]
    head = b"FNF4" + struct.pack("<I", len(sections))
    offs, p = [], len(head) + 4 * len(sections)
    for s in sections:
        offs.append(p)
        p += len(s) + (-len(s) % 4)
    out = head + b"".join(struct.pack("<I", o) for o in offs)
    for s in sections:
        out += s + bytes(-len(s) % 4)
    open(OUT_BIN, "wb").write(out)
    h = ["/* Made by tools/pack.py: what is in data.bin. */", "#ifndef FNAF4_DATA_H", "#define FNAF4_DATA_H", ""]
    h += [f"#define SEC_{n} {i}" for i, n in enumerate(SECTIONS)]
    h += [f"#define PIC_{p.name} {i}" for i, p in enumerate(PICS) if not p.name.startswith("H")]
    h += [f"#define PIC_COUNT {len(PICS)}"]
    h += [f"#define FONT_{n} {i}" for i, n in enumerate(["HUD", "BIG", "TEXT", "CLOCK", "HUGE", "SERIF"])]
    h += [f"#define {n} {v}" for n, v in DEFINES]
    h += [f"#define BED_X0 {BED_X0}", f"#define BED_W {BED_W}"]
    h += ["", "#endif", ""]
    open(OUT_H, "w").write("\n".join(h))
    write_anims()
    print(f"data.bin: {len(out)} bytes")


if __name__ == "__main__":
    main()
