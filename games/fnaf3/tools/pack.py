#!/usr/bin/env python3
"""Five Nights at Freddy's 3: its pictures and fonts -> src/data.bin + src/data.h.

    python3 -I tools/pack.py [CCN]      CCN: the game's application.ccn, from its Android package (res/raw); by
                                        default ~/Documents/Assets/gamesrc/fnaf3/dl/application.ccn

Every picture is the game's own, read from the application by tools/ccn.py, its objects placed where the game
places them (an object's place is its picture's hot spot), at half size: the 1200 x 480 office is 600 x 240 here,
and the calculator shows 320 of it. What is seen through the office's window (Springtrap, Phantom Freddy walking,
Mangle) and the phantoms standing in the office are drawn into parts of the office (patches), as the game shows them.
The cameras are its 507 x 399 at 304 x 240.

data.bin: "FNF3", the number of sections, their offsets, then the sections (SEC_* in data.h):
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
- IMGS: small pictures with alpha (IMG_*): width, height (u16), then 4-bit alpha (or 4-bit grey and alpha)"""
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
ROOT = os.path.join(HERE, "..", "..", "..")
CCN = os.path.expanduser("~/Documents/Assets/gamesrc/fnaf3/dl/application.ccn")
SECTIONS = ["HUFF", "QUANT", "PICS", "TABLE", "BANK", "FONTS", "IMGS"]
QUALITY = {"scene": 80, "scare": 70, "face": 76}
FONT_DIRS = [os.environ.get("FONTS", ""), "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/TTF",
             "/usr/local/share/fonts", "/Library/Fonts", os.path.expanduser("~/Library/Fonts"),
             os.path.expanduser("~/Documents/Assets/gamesrc/fonts/dejavu")]
OW, OH = 600, 240       # the office here (the game's 1200 x 480 at half size)
CW, CH = 304, 240       # a camera
F = "Frame 1"           # the night


def find_font(name):
    for d in FONT_DIRS:
        p = os.path.join(d, name) if d else ""
        if p and os.path.exists(p):
            return p
    raise SystemExit(f"pack: {name} not found (the DejaVu fonts; FONTS=<their folder>)")


def font(size, bold=False, mono=False):
    name = "DejaVu" + ("SansMono" if mono else "Sans") + ("-Bold" if bold else "") + ".ttf"
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




def cover(im, w, h):
    """im resized to fill w x h, its middle kept"""
    s = max(w / im.width, h / im.height)
    r = im.convert("RGB").resize((max(w, round(im.width * s)), max(h, round(im.height * s))), Image.LANCZOS)
    x, y = (r.width - w) // 2, (r.height - h) // 2
    return arr(r)[y:y + h, x:x + w]




# ------------------------------------------------------------------ the game's objects, placed
APP = None


def sprite(name, anim=0, k=0, frame=F):
    """an object's picture: (RGBA image, its hot spot)"""
    h = APP.anim(name, anim, frame)[k]
    return APP.picture(h), APP.hotspot(h)


def frames_of(name, anim=0, frame=F):
    return len(APP.anim(name, anim, frame))


def put(canvas, name, x, y, anim=0, k=0, frame=F):
    """the object's picture onto canvas (an RGBA image in office coordinates) with its hot spot at (x, y)"""
    im, (hx, hy) = sprite(name, anim, k, frame)
    layer = Image.new("RGBA", canvas.size)
    layer.paste(im, (x - hx, y - hy))
    canvas.alpha_composite(layer)


def office(behind=(), front=(), red=False):
    """the 1200 x 480 office as the game shows it: what is behind its window, the office, what stands in it"""
    c = Image.new("RGBA", (1200, 480), (0, 0, 0, 255))
    c.alpha_composite(APP.picture(1173))   # (the night's Backdrop: the hall behind the window)
    for args in behind:
        put(c, *args)
    put(c, "foreground", 0, 0, 12 if red else 0)
    for args in front:
        put(c, *args)
    return c


def half(im):
    return arr(im.convert("RGB").resize((im.width // 2, im.height // 2), Image.LANCZOS))


def box16(x0, y0, x1, y1):
    """a rectangle of the half-size office, out to whole 16 x 16 squares"""
    x0, y0 = max(0, x0 // 2 // 16 * 16), max(0, y0 // 2 // 16 * 16)
    x1, y1 = min(OW, -(-(x1 // 2) // 16) * 16), min(OH, -(-(y1 // 2) // 16) * 16)
    return x0, y0, x1 - x0, y1 - y0


PATCH = {}


def patch(name, part, a, base=None):
    x, y, w, h = PATCH[part]
    return pic(name, a[y:y + h, x:x + w], base=base)


def office_pictures():
    o = pic("OFFICE", half(office()))
    pic("OFFICE_RED", half(office(red=True)), base=o)
    # the window: what is behind the glass (the game's window is x 505..1000, y 76..257)
    PATCH["WIN"] = box16(500, 60, 1010, 270)
    base = patch("WIN_SPRING", "WIN", half(office(behind=[("inwindow", 896, 234)])))
    # Springtrap running past the window (stage 2): 269 px left at path speed 70 (31 frames), 8 pictures
    n = frames_of("run past")
    for k in range(n):
        x = 1031 - 269 * k // (n - 1)   # (created at the window, inwindow +135)
        patch(f"WIN_RUN{k}", "WIN", half(office(behind=[("run past", x, 432, 0, k)])), base=base)
    define("RUN_N", n)
    # Phantom Freddy walking left behind the window from (1011, 90), 733 px, 8 pictures a cycle; a patch every
    # 8 px where he can be seen
    steps = []
    for x in range(1011, 1011 - 734, -8):
        im, (hx, hy) = sprite("fwalk", 0, 0)
        if x - hx + im.width < 505 or x - hx > 1000:
            continue
        steps.append(x)
    for i, x in enumerate(steps):
        k = (1011 - x) // 8 % frames_of("fwalk")
        patch(f"WIN_FREDDY{i}", "WIN", half(office(behind=[("fwalk", x, 90, 0, k)])), base=base)
    define("FREDDY_X0", steps[0])
    define("FREDDY_N", len(steps))
    # his lunge where he stands (anim 12, 11 pictures), for every place he can be seen: drawn from the nearest
    for k in range(frames_of("fwalk", 12)):
        patch(f"WIN_FLUNGE{k}", "WIN", half(office(behind=[("fwalk", 900, 90, 12, k)])), base=base)
    define("FLUNGE_N", frames_of("fwalk", 12))
    # Mangle in the window, rising 100 px from (694, 326): a patch every 10 px
    for i in range(11):
        patch(f"WIN_MANGLE{i}", "WIN", half(office(behind=[("mangle 2", 694, 326 - 10 * i)])), base=base)
    # standing in the office: Springtrap at the left door (head), hiding there, BB, Chica, Foxy
    PATCH["DOOR"] = box16(0, 80, 260, 410)
    d = patch("DOOR_HEAD", "DOOR", half(office(front=[("head", 78, 236)])))
    for k in range(frames_of("hide")):
        patch(f"DOOR_HIDE{k}", "DOOR", half(office(front=[("hide", 58, 231, 0, k)])), base=d)
    define("HIDE_N", frames_of("hide"))
    PATCH["BB"] = box16(730, 110, 1040, 480)
    first = None
    seq = [(0, k) for k in range(frames_of("BB scare"))] + [(12, k) for k in range(frames_of("BB scare", 12))]
    for i, (an, k) in enumerate(seq):
        p = patch(f"BB{i}", "BB", half(office(front=[("BB scare", 590, 77, an, k)])), base=first)
        first = first or p
    define("BB_N", len(seq))
    PATCH["CHICA"] = box16(150, 60, 700, 480)
    first = None
    seq = [(0, 0)] + [(12, k) for k in range(frames_of("chica 2", 12))]
    for i, (an, k) in enumerate(seq):
        p = patch(f"CHICA{i}", "CHICA", half(office(front=[("chica 2", 40, 18, an, k)])), base=first)
        first = first or p
    define("CHICA_N", len(seq))
    PATCH["FOXY"] = box16(350, 20, 560, 480)
    first = None
    seq = [(0, 0)] + [(12, k) for k in range(frames_of("foxy", 12))]
    for i, (an, k) in enumerate(seq):
        p = patch(f"FOXY{i}", "FOXY", half(office(front=[("foxy", 455, 471, an, k)])), base=first)
        first = first or p
    define("FOXY_N", len(seq))
    for part, (x, y, w, h) in PATCH.items():
        define(f"{part}_X", x), define(f"{part}_Y", y), define(f"{part}_W", w), define(f"{part}_H", h)


def view(im, xc):
    """the screen when the game's view is centred on office x xc: 640 of its 800 (the calculator's 320)"""
    a = half(im)
    x = max(0, min(OW - 320, (xc - 320) // 2))
    return a[:, x:x + 320]


def scare_pictures():
    # Springtrap's lunge: at 00 anchor 2 (-4, -77) from the door (the view pushed to x 512), (542, -80) from the
    # right (the view where the monitor was, 812)
    for side, (dx, dy, xc) in (("L", (-4, -77, 512)), ("R", (542, -80, 812))):
        first = prev = None
        for k in range(frames_of("jumpscare")):
            p = pic(f"JUMP{side}{k}", view(office(front=[("jumpscare", dx, dy, 0, k)]), xc), "scare", base=first,
                    refs=[prev] if prev else [])
            first, prev = first or p, p
    define("JUMP_N", frames_of("jumpscare"))
    # Phantom Freddy's scare, on the screen (00 anchor: the screen's corner), over the dark office
    first = prev = None
    for k in range(frames_of("fscare")):
        c = Image.new("RGBA", (800, 480), (0, 0, 0, 255))
        put(c, "fscare", 0, 0, 0, k)
        a = half(c)[:, 40:360]
        p = pic(f"FSCARE{k}", a, "scare", base=first, refs=[prev] if prev else [])
        first, prev = first or p, p
    define("FSCARE_N", frames_of("fscare"))


# the camera feed's animations the night shows (fnaf3_spec 7.5): rooms empty, with Springtrap, the vents,
# Mangle, the Puppet and Chica
FEEDS = [12, 13, 14, 15, 16, 17, 21, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 43] + list(range(45, 58))


def camera_pictures():
    for an in FEEDS:
        for k in range(len(APP.anim("camera screen", an, F))):
            pic(f"FEED{an}_{k}", cover(sprite("camera screen", an, k)[0], CW, CH))
        define(f"FEED{an}_N", len(APP.anim("camera screen", an, F)))
    pic("BBPEEK", cover(sprite("BBpeek")[0], CW, CH), "scare")


def alpha_of(im, scale):
    """an RGBA picture's brightness times alpha, scaled, for IMGS"""
    im = im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))), Image.LANCZOS)
    a = np.asarray(im).astype(np.float64)
    return a[..., 3] * a[..., :3].max(axis=2) / 255


MAP_SCALE = 0.37
MAP_X, MAP_Y = 152, 86   # the map's corner on the monitor


def map_imgs():
    for an, nm in ((0, "MAP"), (12, "VENTMAP")):
        img(nm, alpha_of(sprite("map", an)[0], MAP_SCALE))
    define("MAP_X", MAP_X), define("MAP_Y", MAP_Y)
    # the buttons: centres on the map (the game's office coordinates less the map's corner (779, 82))
    for n in [f"{i:02d}" for i in range(1, 11)] + [f"cam {i}" for i in range(11, 16)]:
        x, y = APP.placed(n, F)
        nm = n.replace("cam ", "")
        define(f"BTN{nm}_X", MAP_X + round((x - 779) * MAP_SCALE))
        define(f"BTN{nm}_Y", MAP_Y + round((y - 82) * MAP_SCALE))
    for i in range(11, 16):
        x, y = APP.placed(f"vent {i}", F)
        define(f"SEAL{i}_X", MAP_X + round((x - 779) * MAP_SCALE))
        define(f"SEAL{i}_Y", MAP_Y + round((y - 82) * MAP_SCALE))


def panel_imgs():
    s = 320 / 560
    for nm, obj in (("P_AUDIO", "audio text"), ("P_CAMERA", "camera text"), ("P_VENT", "ventilation text"),
                    ("P_ALL", "reboot all text"), ("P_EXIT", "exit text"), ("P_CURSOR", "cursor")):
        img(nm, alpha_of(sprite(obj)[0], s))
    img("P_ERROR", alpha_of(sprite("audio error")[0], s))
    for k in range(frames_of("progress bar")):
        img(f"P_DOTS{k}", alpha_of(sprite("progress bar", 0, k)[0], s))
    define("DOTS_N", frames_of("progress bar"))
    for nm, obj in (("M_AUDIO", "audio message"), ("M_VIDEO", "video message"), ("M_VENT", "ventilation error 2")):
        img(nm, alpha_of(sprite(obj)[0], 0.5))
    pic("PANEL", cover(sprite("screen2")[0], 320, 240), "face")


def head_imgs():
    """the Puppet's head, in front of everything (grey)"""
    seq = [(0, 0)] + [(12, k) for k in range(3)] + [(13, 0)]
    for i, (an, k) in enumerate(seq):
        img(f"PHEAD{i}", alpha_of(sprite("phantom head", an, k)[0], 0.5))


def screens():
    pic("GAMEOVER", cover(APP.picture(1062), 320, 240), "face")
    pic("BADEND", cover(APP.picture(1091), 320, 240), "face")
    pic("GOODEND", cover(APP.picture(1093), 320, 240), "face")
    pic("THEEND", cover(APP.picture(1095), 320, 240), "face")
    t = APP.anim("Active 2", 0, "title")
    pic("TITLE", cover(APP.picture(t[0]), 320, 240), "face")
    for an in (12, 13, 14, 15):
        pic(f"TITLE{an}", cover(APP.picture(APP.anim("Active 2", an, "title")[0]), 320, 240), "face")
    img("STAR", alpha_of(APP.picture(840), 0.5))


def icon():
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from make_icon import corner_mask
    im = APP.picture(APP.anim("Active 2", 0, "title")[0]).convert("RGB")
    s = min(im.width, im.height)
    face = im.crop(((im.width - s) // 2, 0, (im.width + s) // 2, s)).resize((55, 56), Image.LANCZOS)
    face.putalpha(corner_mask(55, 56))
    face.save(os.path.join(HERE, "..", "src", "icon.png"), optimize=True)


def main():
    global APP
    APP = ccn.App(sys.argv[1] if len(sys.argv) > 1 else CCN)
    office_pictures()
    scare_pictures()
    camera_pictures()
    screens()
    map_imgs()
    panel_imgs()
    head_imgs()
    icon()
    huff, quant, pics, table, bank = encode_all()
    fonts = [
        ttf_font(font(11, bold=True, mono=True)),   # FONT_HUD
        ttf_font(font(17, mono=True)),              # FONT_BIG
        ttf_font(font(11)),                         # FONT_TEXT
        ttf_font(font(24, bold=True)),              # FONT_CLOCK
        ttf_font(font(24, mono=True)),              # FONT_HUGE
    ]
    sections = [huff, quant, pics, table, bank, pack_fonts(fonts), pack_imgs()]
    head = b"FNF3" + struct.pack("<I", len(sections))
    offs, p = [], len(head) + 4 * len(sections)
    for s in sections:
        offs.append(p)
        p += len(s) + (-len(s) % 4)
    out = head + b"".join(struct.pack("<I", o) for o in offs)
    for s in sections:
        out += s + bytes(-len(s) % 4)
    open(OUT_BIN, "wb").write(out)
    h = ["/* Made by tools/pack.py: what is in data.bin. */", "#ifndef FNAF3_DATA_H", "#define FNAF3_DATA_H", ""]
    h += [f"#define SEC_{n} {i}" for i, n in enumerate(SECTIONS)]
    h += [f"#define PIC_{p.name} {i}" for i, p in enumerate(PICS)]
    h += [f"#define PIC_COUNT {len(PICS)}"]
    h += [f"#define FONT_{n} {i}" for i, n in enumerate(["HUD", "BIG", "TEXT", "CLOCK", "HUGE"])]
    h += [f"#define {n} {v}" for n, v in DEFINES]
    h += ["", "#endif", ""]
    open(OUT_H, "w").write("\n".join(h))
    print(f"data.bin: {len(out)} bytes")


if __name__ == "__main__":
    main()
