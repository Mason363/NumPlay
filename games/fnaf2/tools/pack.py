#!/usr/bin/env python3
"""Five Nights at Freddy's 2: its pictures and fonts -> src/data.bin + src/data.h.

    python3 -I tools/pack.py REF        the game's pictures (REF: the folder tools/fetch.py filled, with its
                                        map.txt filled in; Python + Pillow + NumPy)
    python3 -I tools/pack.py            stand-in pictures: the rooms as plain shapes, everyone as a name plate,
                                        to build and test the game before the pictures are gathered

REF/map.txt names the picture each of the game's states comes from, a line each: "NAME path" (the path from
REF). PICTURES below lists the names and what each shows. The office's states (the hallway and the vents lit,
who is in the room) are whole office pictures (1600 x 768): pack.py cuts the part that changes. A jumpscare's
frames are lines of the same name, in order. TITLE_TEXT and STAR are white on transparent; ICON is the face for
the app's icon.

data.bin: "FNF2", the number of sections, their offsets, then the sections (SEC_* in data.h):
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
- IMGS: small pictures with alpha (IMG_*): width, height (u16), then 4-bit alpha (or 4-bit grey and alpha)
- MAP: the camera map: its width and height (u16), its walls (1 bit a pixel), where the 12 buttons are

The office is the game's 1600 x 768 at 240 high (500 x 240): the calculator shows 320 of it and turns. The
cameras are its 1024 x 768 at 320 x 240, but CAM 07 to 12, 1600 x 768 as the office, which the camera pans across."""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import codec as C  # noqa: E402

OUT_BIN = os.path.join(HERE, "..", "src", "data.bin")
OUT_H = os.path.join(HERE, "..", "src", "data.h")
ROOT = os.path.join(HERE, "..", "..", "..")
SECTIONS = ["HUFF", "QUANT", "PICS", "TABLE", "BANK", "FONTS", "IMGS", "MAP"]
QUALITY = {"scene": 80, "scare": 70, "paper": 84, "face": 76}
FONT_DIRS = [os.environ.get("FONTS", ""), "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/TTF",
             "/usr/local/share/fonts", "/Library/Fonts", os.path.expanduser("~/Library/Fonts"),
             os.path.expanduser("~/Documents/Assets/gamesrc/fonts/dejavu")]

OW, OH = 500, 240       # the office on the calculator
CW, CH = 320, 240       # a camera
# the parts of the office that change, drawn over it (x, y, w, h in the 500 x 240 office)
# (where the game's own pictures change: the hallway's light reaches the floor, Toy Freddy stands right of the desk)
PATCHES = {
    "HALL": (176, 64, 128, 176),     # the hallway, in the flashlight
    "LVENT": (16, 128, 128, 112),    # the left vent's blind spot, in its light
    "RVENT": (368, 128, 128, 112),   # the right one
    "OCC": (80, 0, 336, 240),        # whoever stands in front of the desk
    "MANGLE": (208, 0, 176, 64),     # Mangle on the ceiling
    "BBO": (80, 80, 128, 144),       # Balloon Boy in the office
}
WHO = {"TF": "Toy Freddy", "TB": "Toy Bonnie", "TC": "Toy Chica", "MANGLE": "Mangle", "BB": "Balloon Boy",
       "WF": "Withered Freddy", "WB": "Withered Bonnie", "WC": "Withered Chica", "FOXY": "Withered Foxy",
       "GF": "Golden Freddy", "PUPPET": "The Puppet", "TFFAR": "Toy Freddy"}
# the stand-ins' colours (a stripe on each name plate)
COLOUR = {"TF": (150, 95, 50), "TB": (70, 120, 220), "TC": (235, 205, 60), "MANGLE": (235, 230, 235),
          "BB": (220, 60, 50), "WF": (110, 70, 40), "WB": (100, 80, 170), "WC": (220, 150, 40),
          "FOXY": (170, 60, 40), "GF": (225, 185, 60), "PUPPET": (240, 240, 240), "TFFAR": (150, 95, 50)}

# Every picture, in order (the code knows them by name): what it is, how big, and what it shows.
#   office: the whole office; patch P: the part P of an office picture; cam: a camera; scare: a jumpscare's
#   frames; screen: a menu picture; face: a custom night tile
PICTURES = [("OFFICE", "office", "the office, the lights out down the hall and in the vents")]
PICTURES += [(f"HALL_{s}", "patch HALL", f"the hallway in the flashlight: {d}") for s, d in (
    ("LIT", "no one"), ("TFFAR", "Toy Freddy far down"), ("TF", "Toy Freddy close"), ("TC", "Toy Chica"),
    ("MANGLE", "Mangle"), ("WF", "Withered Freddy"), ("WB", "Withered Bonnie"), ("FOXY", "Withered Foxy"),
    ("GF", "Golden Freddy's head"))]
PICTURES += [(f"LVENT_{s}", "patch LVENT", f"the left vent lit: {d}") for s, d in (
    ("LIT", "no one"), ("TC", "Toy Chica"), ("BB", "Balloon Boy"))]
PICTURES += [(f"RVENT_{s}", "patch RVENT", f"the right vent lit: {d}") for s, d in (
    ("LIT", "no one"), ("TB", "Toy Bonnie"), ("MANGLE", "Mangle"))]
PICTURES += [(f"OCC_{s}", "patch OCC", f"{WHO[s]} in the office, in front of the desk")
             for s in ("TF", "TB", "TC", "WF", "WB", "WC", "GF")]
PICTURES += [("OFFICE_MANGLE", "patch MANGLE", "Mangle on the office's ceiling"),
             ("OFFICE_BB", "patch BBO", "Balloon Boy standing in the office")]
# Each camera's states, as its picture shows them; the states starting with D are with the camera's light off (a
# few of the dark rooms show who stands close: DWC is Withered Chica in the dark, D0 to D7 the Show Stage's).
CAMS = [
    ("01", "Party Room 1", ["DARK", "LIT", "TC", "WB", "MANGLE", "PUPPET"]),
    ("02", "Party Room 2", ["DARK", "DWC", "LIT", "TB", "WC", "MANGLE", "PUPPET"]),
    ("03", "Party Room 3", ["DARK", "DWF", "LIT", "TB", "WF", "PUPPET"]),
    ("04", "Party Room 4", ["DARK", "DTB", "LIT", "TB", "WC", "PUPPET"]),
    ("05", "Left Air Vent", ["DARK", "LIT", "TC", "BB", "WB"]),
    ("06", "Right Air Vent", ["DARK", "LIT", "TB", "WC", "MANGLE"]),
    ("07", "Main Hall", ["DARK", "EMPTY", "TC", "MANGLE", "WF", "WB", "PUPPET"]),
    ("08", "Parts/Service", ["DARK"] + [f"S{m}" for m in range(8)] + ["FOXY"]),
    ("09", "Show Stage", [f"S{m}" for m in range(8)] + [f"D{m}" for m in range(8)]),
    ("10", "Game Area", ["DARK", "EMPTY", "TF", "BB", "TFBB", "MANGLE"]),
    ("11", "Prize Corner", ["DARK", "P0", "P1", "P2", "P3", "EMPTY", "MANGLE"]),
    ("12", "Kid's Cove", ["DARK", "MANGLE", "EMPTY"]),
]
for num, room, states in CAMS:
    for s in states:
        PICTURES.append((f"CAM{num}_{s}", "cam", f"CAM {num}, {room}: {s}"))
SCARES = ["TF", "TB", "TC", "MANGLE", "WF", "WB", "WC", "FOXY", "GF", "PUPPET"]
PICTURES += [(f"SCARE_{s}", "scare", f"{WHO[s]}'s jumpscare, its frames in order") for s in SCARES]
PICTURES += [("TITLE", "screen", "the title's background"), ("NEWSPAPER", "screen", "the newspaper before night 1"),
             ("PAYCHECK", "screen", "the paycheck after night 5"), ("OVERTIME", "screen", "the check after night 6"),
             ("PINKSLIP", "screen", "the pink slip after the custom night"),
             ("GAMEOVER", "screen", "the game over")]
FACES = ["WF", "WB", "WC", "FOXY", "BB", "TF", "TB", "TC", "MANGLE", "GF"]
PICTURES += [(f"FACE{i}", "face", f"{WHO[s]}'s face on the custom night") for i, s in enumerate(FACES)]
# Parts/Service's S<m> and the Show Stage's: who is there, a bit each (1 the Freddy, 2 the Bonnie, 4 the Chica)

REF = None
MAP = {}


def find_font(name):
    for d in FONT_DIRS:
        p = os.path.join(d, name) if d else ""
        if p and os.path.exists(p):
            return p
    raise SystemExit(f"pack: {name} not found (the DejaVu fonts; FONTS=<their folder>)")


def font(size, bold=False, mono=False, serif=False):
    name = "DejaVu" + ("SansMono" if mono else "Serif" if serif else "Sans") + ("-Bold" if bold else "") + ".ttf"
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


# ------------------------------------------------------------------ the camera map
# where the cameras' buttons are, on a plan of the building (the office at the bottom): its rooms as boxes
MAP_W, MAP_H = 148, 104
MAP_ROOMS = [(2, 2, 32, 22), (36, 2, 104, 22), (108, 2, 146, 22), (2, 24, 32, 46), (36, 24, 104, 46),
             (108, 24, 146, 46), (2, 48, 30, 68), (30, 48, 56, 68), (2, 70, 30, 102), (30, 70, 56, 102),
             (58, 70, 84, 102), (110, 70, 146, 102)]
MAP_BUTTONS = {"01": (16, 86), "02": (43, 86), "03": (16, 58), "04": (43, 58), "05": (71, 86), "06": (128, 86),
               "07": (70, 35), "08": (17, 35), "09": (70, 12), "10": (127, 35), "11": (127, 12), "12": (17, 12)}


def camera_map():
    im = Image.new("L", (MAP_W, MAP_H))
    d = ImageDraw.Draw(im)
    for box in MAP_ROOMS:
        d.rectangle(box, outline=255)
    d.rectangle((86, 84, 108, 102), fill=0, outline=255)   # the office
    d.text((97, 93), "YOU", font=font(7, bold=True), fill=255, anchor="mm")
    bits = np.asarray(im) > 128
    stride = (MAP_W + 7) // 8
    out = struct.pack("<HH", MAP_W, MAP_H)
    for r in range(MAP_H):
        row = bytearray(stride)
        for c in range(MAP_W):
            if bits[r, c]:
                row[c >> 3] |= 1 << (c & 7)
        out += row
    for k in sorted(MAP_BUTTONS):
        out += struct.pack("<HH", *MAP_BUTTONS[k])
    return out


# ------------------------------------------------------------------ the stand-in pictures
RNG = np.random.default_rng(2)


def grain(a, s=4.0):
    return a   # (the stand-ins stay flat: they take little room)


def plate(d, cx, cy, who, scale=1.0, text=None):
    """a name plate where someone stands: a dark card, the colour stripe, the name"""
    name = (text or WHO[who]).upper()
    f = font(max(8, int(11 * scale)), bold=True)
    w = int(f.getlength(name)) + int(16 * scale)
    h = int(30 * scale)
    x0, y0 = int(cx - w / 2), int(cy - h / 2)
    d.rounded_rectangle((x0, y0, x0 + w, y0 + h), radius=int(5 * scale), fill=(24, 24, 28), outline=(200, 200, 200))
    d.rectangle((x0 + 3, y0 + 3, x0 + w - 3, y0 + int(8 * scale)), fill=COLOUR[who])
    d.text((cx, y0 + int(19 * scale)), name, font=f, fill=(240, 240, 240), anchor="mm")


def stand_in_office():
    im = Image.new("RGB", (OW, OH), (30, 26, 24))
    d = ImageDraw.Draw(im)
    d.rectangle((0, 170, OW, OH), fill=(42, 36, 32))             # the floor
    d.polygon([(176, 32), (320, 32), (300, 150), (196, 150)], fill=(8, 8, 10))   # the hallway
    d.rectangle((168, 26, 328, 34), fill=(50, 44, 40))
    for x0 in (8, 412):                                          # the vents
        d.rectangle((x0, 120, x0 + 80, 184), fill=(10, 10, 12), outline=(70, 70, 74))
        for k in range(1, 4):
            d.line((x0, 120 + 16 * k, x0 + 80, 120 + 16 * k), fill=(40, 40, 44))
    d.rectangle((150, 176, 350, 240), fill=(58, 50, 44))         # the desk
    d.rectangle((222, 150, 278, 180), fill=(20, 22, 24), outline=(80, 80, 80))
    return grain(arr(im))


def stand_in_patch(office, part, who=None, lit=True, pos=None, scale=1.0, text=None):
    x, y, w, h = PATCHES[part]
    im = Image.fromarray(np.clip(office, 0, 255).astype(np.uint8))
    d = ImageDraw.Draw(im)
    if lit and part == "HALL":
        d.polygon([(176, 32), (320, 32), (300, 150), (196, 150)], fill=(84, 78, 68))
        d.polygon([(220, 60), (276, 60), (270, 120), (226, 120)], fill=(40, 38, 34))
    if lit and part in ("LVENT", "RVENT"):
        x0 = 8 if part == "LVENT" else 412
        d.rectangle((x0, 120, x0 + 80, 184), fill=(70, 66, 58), outline=(120, 120, 120))
    if who:
        cx, cy = pos or (x + w / 2, y + h / 2)
        plate(d, cx, cy, who, scale, text)
    return arr(im)[y:y + h, x:x + w]


def stand_in_room(num, room, lit, who=(), extra=None):
    im = Image.new("RGB", (CW, CH), (36, 34, 40) if lit else (6, 6, 8))
    d = ImageDraw.Draw(im)
    wall, floor = ((70, 64, 72), (52, 46, 44)) if lit else ((12, 12, 14), (9, 9, 10))
    d.rectangle((0, 0, CW, 150), fill=wall)
    d.polygon([(0, 240), (320, 240), (260, 150), (60, 150)], fill=floor)
    if "Vent" in room:
        d.rectangle((0, 0, CW, CH), fill=(14, 14, 16) if not lit else (60, 58, 56))
        d.polygon([(40, 30), (280, 30), (230, 200), (90, 200)], fill=(4, 4, 6) if not lit else (34, 32, 30))
    d.text((CW - 14, 30), room.upper(), font=font(9), fill=(90, 90, 90) if lit else (30, 30, 30), anchor="rm")
    n = len(who)
    for i, w in enumerate(who):
        plate(d, CW / 2 + (i - (n - 1) / 2) * 96, 84, w, 0.9 if n > 1 else 1.1)
    if extra:
        d.text((CW / 2, 112), extra, font=font(11), fill=(220, 220, 220), anchor="mm")
    return grain(arr(im), 5)


WIDE = ("07", "08", "09", "10", "11", "12")   # (the cameras 1600 wide, which pan)


def widen(num, a):
    """a stand-in camera as wide as its camera's picture"""
    return np.pad(a, ((0, 0), ((OW - CW) // 2, OW - CW - (OW - CW) // 2), (0, 0)), mode="edge") if num in WIDE else a


def stand_in_cams():
    define("CAM_PAN", OW - CW)
    for num, room, states in CAMS:
        base = None
        for s in states:
            name = f"CAM{num}_{s}"
            if s == "DARK":
                pic(name, widen(num, stand_in_room(num, room, False)))
                continue
            if s in ("DWC", "DWF", "DTB") or (num == "09" and s.startswith("D")):
                m = int(s[1:]) if num == "09" else 0
                who = [["TF", "TB", "TC"][k] for k in range(3) if m >> k & 1] if num == "09" else [s[1:]]
                pic(name, widen(num, stand_in_room(num, room, False, who)))
                continue
            if s == "TFBB":
                who = ["TF", "BB"]
            elif num in ("08", "09") and s.startswith("S"):
                m = int(s[1:])
                trio = ["WF", "WB", "WC"] if num == "08" else ["TF", "TB", "TC"]
                who = [trio[k] for k in range(3) if m >> k & 1]
            elif s in ("LIT", "EMPTY"):
                who = []
            elif s.startswith("P") and num == "11":
                who = ["PUPPET"]
            else:
                who = [s]
            extra = None
            if num == "11" and s.startswith("P"):
                extra = ["in the box", "partly out", "mostly out", "out of the box"][int(s[1:])]
            if num == "11" and s == "MANGLE":
                extra = "the music box shut"
            a = widen(num, stand_in_room(num, room, True, who, extra))
            p = pic(name, a, base=base)
            base = base or p


def stand_in_scares():
    for who in SCARES:
        first = prev = None
        for k in range(3):
            im = Image.new("RGB", (CW, CH), (60 + 40 * k, 8, 8))
            d = ImageDraw.Draw(im)
            plate(d, CW / 2, CH / 2, who, 2.2 + 0.5 * k)
            p = pic(f"SCARE_{who}{k}", grain(arr(im), 10), "scare", base=first, refs=[prev] if prev else [])
            first, prev = first or p, p
        define(f"SCARE_{who}_N", 3)


def paper(color):
    im = Image.new("RGB", (CW, CH), (12, 12, 12))
    d = ImageDraw.Draw(im)
    d.rectangle((22, 30, 297, 210), fill=color)
    return im


def text(d, xy, s, size, bold=False, serif=False, fill=(30, 30, 30), anchor="la"):
    d.text(xy, s, font=font(size, bold, serif=serif), fill=fill, anchor=anchor)


def cheque(amount, words, memo):
    im = paper((222, 228, 214))
    d = ImageDraw.Draw(im)
    d.rectangle((26, 34, 293, 206), outline=(120, 140, 120))
    text(d, (34, 42), "Fazbear Entertainment, Inc.", 11, bold=True, serif=True)
    text(d, (34, 56), "Freddy Fazbear's Pizza", 9, serif=True, fill=(70, 70, 70))
    text(d, (286, 42), "No. 1987", 9, fill=(90, 90, 90), anchor="ra")
    text(d, (34, 88), "PAY TO THE", 8, fill=(80, 80, 80))
    text(d, (34, 98), "ORDER OF", 8, fill=(80, 80, 80))
    text(d, (92, 92), "Jeremy Fitzgerald", 13, serif=True, fill=(20, 30, 90))
    d.line((90, 110, 220, 110), fill=(80, 80, 80))
    d.rectangle((228, 86, 290, 106), outline=(80, 80, 80))
    text(d, (286, 96), amount, 13, bold=True, fill=(20, 30, 90), anchor="rm")
    text(d, (34, 124), words, 10, serif=True, fill=(20, 30, 90))
    d.line((34, 138, 286, 138), fill=(80, 80, 80))
    text(d, (34, 172), "MEMO", 8, fill=(80, 80, 80))
    text(d, (64, 168), memo, 11, serif=True, fill=(20, 30, 90))
    d.line((62, 184, 150, 184), fill=(80, 80, 80))
    d.line((180, 184, 286, 184), fill=(80, 80, 80))
    text(d, (233, 188), "Fazbear Entertainment", 7, fill=(80, 80, 80), anchor="ma")
    return im


def pink_slip():
    im = paper((236, 178, 190))
    d = ImageDraw.Draw(im)
    text(d, (160, 40), "NOTICE OF TERMINATION", 13, bold=True, serif=True, anchor="ma")
    text(d, (160, 58), "Freddy Fazbear's Pizza", 9, serif=True, fill=(80, 40, 50), anchor="ma")
    d.line((34, 74, 286, 74), fill=(120, 60, 70))
    rows = [("Employee:", "Fritz Smith"), ("Position:", "Night security"), ("Reason:", "Tampering with the"),
            ("", "animatronics, odor.")]
    y = 88
    for k, v in rows:
        text(d, (36, y), k, 10, bold=True, fill=(60, 20, 30))
        text(d, (110, y), v, 11, serif=True, fill=(30, 20, 20))
        y += 18
    text(d, (160, 186), "Please return your uniform.", 9, serif=True, fill=(80, 40, 50), anchor="ma")
    return im


def newspaper():
    im = paper((214, 210, 196))
    d = ImageDraw.Draw(im)
    text(d, (160, 38), "HELP WANTED", 20, bold=True, serif=True, anchor="ma")
    d.line((34, 64, 286, 64), fill=(60, 60, 60), width=2)
    lines = ["Freddy Fazbear's Pizza, open again", "with all new characters, needs a", "night security guard.",
             "", "Watch the cameras from 12 to 6 AM.", "Keep an eye on the place.", "", "$100.50 a week."]
    for i, s in enumerate(lines):
        text(d, (160, 74 + 15 * i), s, 11, serif=True, fill=(30, 30, 30), anchor="ma")
    return im


def stand_in_screens():
    title = Image.new("RGB", (CW, CH), (10, 8, 8))
    d = ImageDraw.Draw(title)
    for k in range(40):
        d.line((0, 6 * k, CW, 6 * k), fill=(16, 12, 12))
    pic("TITLE", grain(arr(title), 6), "face")
    pic("NEWSPAPER", arr(newspaper()), "paper")
    pic("PAYCHECK", arr(cheque("$100.50", "One hundred and 50/100 dollars", "One week's work")), "paper")
    pic("OVERTIME", arr(cheque("$20.10", "Twenty and 10/100 dollars", "Overtime")), "paper")
    pic("PINKSLIP", arr(pink_slip()), "paper")
    over = stand_in_room("08", "Parts/Service", True, ["WF", "WB", "WC"]) * 0.6
    pic("GAMEOVER", over, "scene")


def stand_in_faces():
    for i, who in enumerate(FACES):
        im = Image.new("RGB", (48, 48), (20, 20, 24))
        d = ImageDraw.Draw(im)
        d.rectangle((2, 2, 45, 12), fill=COLOUR[who])
        initials = "".join(w[0] for w in WHO[who].replace("The ", "").split())
        d.text((24, 30), initials, font=font(16, bold=True), fill=(235, 235, 235), anchor="mm")
        pic(f"FACE{i}", arr(im), "face")


def stand_in_title_text():
    im = Image.new("L", (190, 112))
    d = ImageDraw.Draw(im)
    f = font(22, bold=True, serif=True)
    for i, s in enumerate(["Five", "Nights", "at Freddy's"]):
        d.text((0, 26 * i), s, font=f, fill=255)
    d.text((150, 52), "2", font=font(46, bold=True, serif=True), fill=255)
    img("TITLE", np.asarray(im).astype(np.float64))


def star_img(alpha=None):
    if alpha is None:
        im = Image.new("L", (22 * 4, 21 * 4))
        pts = []
        for k in range(10):
            r = 42 if k % 2 == 0 else 17
            a = np.pi / 2 + k * np.pi / 5
            pts.append((44 + r * np.cos(a), 46 - r * np.sin(a)))
        ImageDraw.Draw(im).polygon(pts, fill=255)
        alpha = np.asarray(im.resize((22, 21), Image.LANCZOS)).astype(np.float64)
    img("STAR", alpha)


def stand_in_icon():
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from make_icon import corner_mask
    im = Image.new("RGB", (55, 56), (18, 10, 10))
    d = ImageDraw.Draw(im)
    d.text((27, 22), "FNaF", font=font(15, bold=True, serif=True), fill=(230, 230, 230), anchor="mm")
    d.text((27, 42), "2", font=font(18, bold=True, serif=True), fill=(220, 40, 40), anchor="mm")
    im.putalpha(corner_mask(55, 56))
    im.save(os.path.join(HERE, "..", "src", "icon.png"), optimize=True)


def stand_ins():
    office = stand_in_office()
    o = pic("OFFICE", office)
    who_pos = {"TFFAR": ((248, 100), 0.6, None)}
    for name, kind, _ in PICTURES:
        if not kind.startswith("patch"):
            continue
        part = kind.split()[1]
        state = name.split("_", 1)[1] if part != "MANGLE" and part != "BBO" else None
        who = None if state in (None, "LIT") else state
        if part == "MANGLE":
            who = "MANGLE"
        if part == "BBO":
            who = "BB"
        pos, scale, txt = who_pos.get(who, (None, 1.0, None))
        a = stand_in_patch(office, part, who, True, pos, scale, txt)
        if part in ("MANGLE", "BBO", "OCC"):
            a = stand_in_patch(office, part, who, False, pos, 0.85 if part == "BBO" else scale, txt)
        pic(name, a)
    del o
    stand_in_cams()
    stand_in_scares()
    stand_in_screens()
    stand_in_faces()
    stand_in_title_text()
    star_img()
    stand_in_icon()


# ------------------------------------------------------------------ the game's own pictures (REF/map.txt)
def ref_files(name):
    return [os.path.join(REF, p) for p in MAP.get(name, [])]


def ref_image(name, i=0):
    files = ref_files(name)
    if not files:
        raise SystemExit(f"pack: map.txt has no picture for {name}")
    return Image.open(files[i])


def cover(im, w, h):
    """im resized to fill w x h, its middle kept"""
    s = max(w / im.width, h / im.height)
    r = im.convert("RGB").resize((max(w, round(im.width * s)), max(h, round(im.height * s))), Image.LANCZOS)
    x, y = (r.width - w) // 2, (r.height - h) // 2
    return arr(r)[y:y + h, x:x + w]


def from_ref():
    office = cover(ref_image("OFFICE"), OW, OH)
    pic("OFFICE", office)
    define("CAM_PAN", OW - CW)
    bases = {}
    for name, kind, _ in PICTURES:
        if kind.startswith("patch"):
            x, y, w, h = PATCHES[kind.split()[1]]
            a = cover(ref_image(name), OW, OH)[y:y + h, x:x + w]
            part = kind.split()[1]
            p = pic(name, a, base=bases.get(part))
            if part in ("HALL", "LVENT", "RVENT") and part not in bases:
                bases[part] = p
        elif kind == "cam":
            num, state = name[3:5], name[6:]
            a = cover(ref_image(name), OW if num in WIDE else CW, OH if num in WIDE else CH)
            group = num + ("D" if state.startswith("D") else "")   # (the dark pictures share with each other)
            first = bases.get(group)
            p = pic(name, a, base=first)
            if not first:
                bases[group] = p
    for who in SCARES:
        files = ref_files(f"SCARE_{who}")
        first = prev = None
        for k in range(len(files)):
            p = pic(f"SCARE_{who}{k}", cover(Image.open(files[k]), CW, CH), "scare", base=first,
                    refs=[prev] if prev else [])
            first, prev = first or p, p
        define(f"SCARE_{who}_N", len(files))
    for name, kind, _ in PICTURES:
        if kind == "screen":
            pic(name, cover(ref_image(name), CW, CH), "paper" if name in ("NEWSPAPER", "PAYCHECK", "OVERTIME",
                                                                           "PINKSLIP") else "face")
        elif kind == "face":
            pic(name, cover(ref_image(name), 48, 48), "face")
    t = ref_image("TITLE_TEXT").convert("RGBA")
    t = t.resize((94, int(t.height * 94 / t.width)), Image.LANCZOS)   # (the game's 300 x 280 on its 1024)
    a = np.asarray(t).astype(np.float64)
    img("TITLE", a[..., 3] * a[..., :3].mean(axis=2) / 255)
    s = np.asarray(ref_image("STAR").convert("RGBA").resize((22, 21), Image.LANCZOS)).astype(np.float64)
    star_img(s[..., 3] * s[..., :3].mean(axis=2) / 255)
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from make_icon import corner_mask
    icon = Image.fromarray(cover(ref_image("ICON"), 55, 56).astype(np.uint8))
    icon.putalpha(corner_mask(55, 56))
    icon.save(os.path.join(HERE, "..", "src", "icon.png"), optimize=True)


def read_map():
    path = os.path.join(REF, "map.txt")
    for line in open(path):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        name, file = line.split(None, 1)
        if file != "?":
            MAP.setdefault(name, []).append(file)


def main():
    global REF
    if len(sys.argv) > 2:
        raise SystemExit(__doc__)
    if len(sys.argv) == 2:
        REF = sys.argv[1]
        read_map()
        from_ref()
    else:
        stand_ins()
    names = [p.name for p in PICS]
    for n, k, _ in PICTURES:   # (the same pictures either way, for the code)
        assert k == "scare" or n in names, n
    for part, (x, y, w, h) in PATCHES.items():
        define(f"{part}_X", x)
        define(f"{part}_Y", y)
    huff, quant, pics, table, bank = encode_all()
    fonts = [
        ttf_font(font(11, bold=True, mono=True)),   # FONT_HUD
        ttf_font(font(17, mono=True)),              # FONT_BIG
        ttf_font(font(11)),                         # FONT_TEXT
        ttf_font(font(24, bold=True)),              # FONT_CLOCK
        ttf_font(font(24, mono=True)),              # FONT_HUGE
    ]
    sections = [huff, quant, pics, table, bank, pack_fonts(fonts), pack_imgs(), camera_map()]
    head = b"FNF2" + struct.pack("<I", len(sections))
    offs, p = [], len(head) + 4 * len(sections)
    for s in sections:
        offs.append(p)
        p += len(s) + (-len(s) % 4)
    out = head + b"".join(struct.pack("<I", o) for o in offs)
    for s in sections:
        out += s + bytes(-len(s) % 4)
    open(OUT_BIN, "wb").write(out)
    h = ["/* Made by tools/pack.py: what is in data.bin. */", "#ifndef FNAF2_DATA_H", "#define FNAF2_DATA_H", ""]
    h += [f"#define SEC_{n} {i}" for i, n in enumerate(SECTIONS)]
    h += [f"#define PIC_{p.name} {i}" for i, p in enumerate(PICS)]
    h += [f"#define PIC_COUNT {len(PICS)}"]
    h += [f"#define FONT_{n} {i}" for i, n in enumerate(["HUD", "BIG", "TEXT", "CLOCK", "HUGE"])]
    h += [f"#define {n} {v}" for n, v in DEFINES]
    h += [f"#define STAND_INS {0 if REF else 1}"]
    h += ["", "#endif", ""]
    open(OUT_H, "w").write("\n".join(h))
    print(f"data.bin: {len(out)} bytes" + ("" if REF else " (stand-in pictures)"))


if __name__ == "__main__":
    main()
