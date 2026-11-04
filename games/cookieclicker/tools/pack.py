#!/usr/bin/env python3
"""Cookie Clicker's numbers, texts, pictures and fonts -> src/data.bin + src/data.h.

    node tools/extract.js REF build/game.json
    python3 -I tools/pack.py REF build/game.json      (REF: the folder tools/fetch.py filled)

The game's left column (about 384 pixels wide in a browser) is drawn at 0.37 of its size, its store at about half.

data.bin, read where it lies in flash (offsets from its start; data.h names them):
- IMAGES: 16 bytes each (IM_*), and the pictures' palettes and pixels: see tools/pictures.py
- ICONS: the game's icon sheet (icons.png, 36 x 37 icons of 48 pixels) at 24 pixels: an image index (u16, 0xFFFF
  none) for each icon, row by row
- FONTS: the offset (u32) of each (F_*): first character, count, ascent, line height (u8 x 4), then 8 bytes a glyph
  (w, h, x, y from the baseline, advance (u8, u8, s8, s8, u8), 0, its data's offset from the glyphs' end (u16)):
  4-bit coverage, 2 pixels a byte, rows padded to a byte
- TEXT: the texts, one byte a character (ASCII, and the few others they use from 0x80: see CHARS), each ending
  with 0. In them: 0x01 bold, 0x02 back to plain, 0x03 italic, 0x04 back, 0x05 a line across, \\n a new line,
  0x06 nothing (it keeps two numbers apart when the game rewrites numbers: src/text.c)
- the tables of the game's buildings, upgrades, achievements and tiers (their C structures are in data.h)
"""
import html
import json
import math
import os
import re
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pictures  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_BIN = os.path.join(HERE, "..", "src", "data.bin")
OUT_H = os.path.join(HERE, "..", "src", "data.h")
OUT_ICON = os.path.join(HERE, "..", "src", "icon.png")
S = 0.37            # the left column's pictures (the big cookie: 256 -> 95 pixels)

# the characters past ASCII the texts and fonts have, from 0x80 on
CHARS = "éèêëàâäïîôöûüùçñÉÀ×÷°•…’‘“”–—½¼¾²³™©®♥"


# ------------------------------------------------------------------ pictures
def rgb565(r, g, b):
    return (int(r) >> 3) << 11 | (int(g) >> 2) << 5 | int(b) >> 3


def premul_resize(img, size, box=None):
    """an RGBA picture resized without dark fringes (its colours weighted by their opacity)"""
    img = img.convert("RGBA")
    if box:
        img = img.crop(box)
    a = np.asarray(img).astype(np.float32) / 255
    a[..., :3] *= a[..., 3:4]
    pm = Image.fromarray((a * 255 + 0.5).astype(np.uint8), "RGBA").resize(size, Image.LANCZOS)
    b = np.asarray(pm).astype(np.float32) / 255
    al = b[..., 3:4]
    b[..., :3] = np.where(al > 0, b[..., :3] / np.maximum(al, 1e-6), 0)
    return Image.fromarray((np.clip(b, 0, 1) * 255 + 0.5).astype(np.uint8), "RGBA")


def quantize(img, n):
    """palette (RGBA tuples) and indices, at most n colours; the fully clear pixels all at index 0"""
    arr = np.asarray(img.convert("RGBA"))
    clear = arr[..., 3] < 8
    if clear.all():
        return [(0, 0, 0, 0)], np.zeros(arr.shape[:2], np.uint8)
    q = img.convert("RGBA").quantize(n - (1 if clear.any() else 0), method=Image.Quantize.FASTOCTREE)
    pal = q.getpalette("RGBA")
    if pal is None:  # (Pillow keeps the alpha in the palette of an RGBA quantize)
        raise SystemExit("pack: no RGBA palette")
    idx = np.asarray(q).astype(np.int32)
    used = sorted(set(idx[~clear].ravel().tolist()))
    remap = {}
    out_pal = [(0, 0, 0, 0)] if clear.any() else []
    for u in used:
        remap[u] = len(out_pal)
        out_pal.append(tuple(pal[4 * u:4 * u + 4]))
    out = np.zeros(idx.shape, np.uint8)
    for u, v in remap.items():
        out[idx == u] = v
    out[clear] = 0
    return out_pal, out


class Pack:
    def __init__(self):
        self.blob = bytearray(pictures.MAGIC)
        self.images = []          # the pictures, as drawn (pictures.plane)
        self.labels = []          # their names, the icons' "icon <x> <y>" (pictures.lay_out)
        self.names = {}
        self.recs = self.priors = None
        self.defines = []

    def align(self, n=4):
        while len(self.blob) % n:
            self.blob.append(0)

    def put(self, data, align=4):
        self.align(align)
        off = len(self.blob)
        self.blob += data
        return off

    def image(self, name, img, fmt="p8", ncolors=256, place=None):
        """adds a picture (RGB565, or quantized to a palette of ncolors, 16 for p4); returns its index"""
        w, h = img.size
        if fmt == "rgb":
            arr = np.asarray(img.convert("RGB")).astype(np.uint16)
            px = (arr[..., 0] >> 3) << 11 | (arr[..., 1] >> 2) << 5 | arr[..., 2] >> 3
            self.images.append(pictures.plane(px, np.full((h, w), 255)))
        else:
            pal, idx = quantize(img, 16 if fmt == "p4" else ncolors)
            cols = np.array([rgb565(*c[:3]) for c in pal])
            opas = np.array([c[3] for c in pal])
            self.images.append(pictures.plane(cols[idx], opas[idx]))
        if name:
            if name in self.names:
                raise SystemExit(f"pack: two pictures named {name}")
            self.names[name] = len(self.images) - 1
        self.labels.append(name or "icon %d %d" % place)
        return len(self.images) - 1

    def lay_out(self):
        """the pictures into data.bin"""
        self.recs, self.priors = pictures.lay_out(self.images, self.labels, self.put)


def load(ref, rel):
    return Image.open(os.path.join(ref, "img", rel)).convert("RGBA")


def scaled(ref, rel, size, box=None):
    return premul_resize(load(ref, rel), size, box)


def opaque(ref, rel, size, box=None):
    img = Image.open(os.path.join(ref, "img", rel)).convert("RGB")
    if box:
        img = img.crop(box)
    return img.resize(size, Image.LANCZOS)


# ------------------------------------------------------------------ fonts
def load_font(path, size, axes=None):
    f = ImageFont.truetype(path, size)
    if axes:
        f.set_variation_by_axes(axes)
    return f


def font_bytes(f, chars, line=None):
    """first character 32, all of CHARS mapped from 0x80 on"""
    asc, desc = f.getmetrics()
    glyphs, data = [], bytearray()
    for ch in chars:
        if ch is None:
            glyphs.append((0, 0, 0, 0, 0, len(data)))
            continue
        adv = int(round(f.getlength(ch)))
        mask, off = f.getmask2(ch, mode="L", anchor="ls")
        w, h = mask.size
        if w == 0 or h == 0:
            glyphs.append((0, 0, 0, 0, adv, len(data)))
            continue
        a = np.asarray(Image.frombytes("L", (w, h), bytes(mask))).astype(np.int32)
        a4 = (a * 15 + 127) // 255
        ys, xs = np.nonzero(a4)
        if len(xs) == 0:
            glyphs.append((0, 0, 0, 0, adv, len(data)))
            continue
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        a4 = a4[y0:y1, x0:x1]
        gw, gh = x1 - x0, y1 - y0
        if gw % 2:
            a4 = np.concatenate([a4, np.zeros((gh, 1), np.int32)], 1)
        packed = (a4[:, 0::2] | a4[:, 1::2] << 4).astype(np.uint8)
        glyphs.append((gw, gh, off[0] + x0, off[1] + y0, adv, len(data)))
        data += packed.tobytes()
        if len(data) > 0xFFFF:
            raise SystemExit("pack: a font's glyphs past 64 KB")
    out = bytearray(struct.pack("<BBBB", 32, len(chars), asc, line or (asc + desc)))
    for gw, gh, gx, gy, adv, o in glyphs:
        out += struct.pack("<BBbbBBH", gw, gh, gx, gy, adv, 0, o)
    return out + data


FONT_CHARS = [chr(c) for c in range(32, 127)] + [None] + list(CHARS)


def char_code(c):
    if 32 <= ord(c) < 127 or c == "\n":
        return ord(c)
    i = CHARS.find(c)
    return 0x80 + i if i >= 0 else None


# ------------------------------------------------------------------ texts
ENTITIES = {"&bull;": "•", "&nbsp;": " ", "&eacute;": "é", "&deg;": "°", "&reg;": "®", "&agrave;": "à"}
UNKNOWN = {}


def beautify_int(n):
    return f"{n:,}"


def beautify_in_text(s):
    # the game's BeautifyInText: every run of digits (and commas) written again with commas
    return re.sub(r"(([\d]+[,]*)+)", lambda m: beautify_int(int(m.group(0).replace(",", "") or 0))
                  if m.group(0).replace(",", "").isdigit() else m.group(0), s)


def clean(s, numbers=True):
    """the game's HTML as the calculator's markup (see the TEXT table)"""
    if s is None:
        return ""
    s = s.replace("\r", "").replace("\t", "")
    s = s.replace("<span></span>", "\x06")   # (keeps two numbers apart: see text.c)
    s = re.sub(r"<div class=\"line\"></div>", "\x05", s)
    s = re.sub(r"<br\s*/?>", "\n", s)
    s = re.sub(r"</?(b|strong)>", lambda m: "\x02" if m.group(0).startswith("</") else "\x01", s)
    s = re.sub(r"</?(i|q|em)>", lambda m: "\x04" if m.group(0).startswith("</") else "\x03", s)
    s = re.sub(r"<div[^>]*>", "\n", s)
    s = re.sub(r"</div>", "\n", s)
    s = re.sub(r"<[^>]*>", "", s)
    for k, v in ENTITIES.items():
        s = s.replace(k, v)
    s = html.unescape(s)
    out = []
    for c in s:
        if c in "\x01\x02\x03\x04\x05\x06":
            out.append(c)
            continue
        if unicodedata_combining(c):
            continue
        code = char_code(c)
        if code is None:
            UNKNOWN[c] = UNKNOWN.get(c, 0) + 1
            out.append("?")
        else:
            out.append(chr(code))
    s = "".join(out)
    s = re.sub(r"\n{2,}", "\n", s).strip("\n")
    return s


def unicodedata_combining(c):
    import unicodedata
    return unicodedata.combining(c) != 0


def split_quote(s):
    """a description and its quote (the game's <q>, said in italics)"""
    if s is None:
        return "", ""
    m = re.search(r"<q>(.*)</q>\s*$", s, re.S)
    if m:
        return s[:m.start()], m.group(1)
    return s, ""


# CC_CCK2_TEXT_BPE_V1: deterministic per-string BPE, same rule bytes as text.c
CC_TEXT_BPE_SLOT = 1024
CC_TEXT_BPE_MIN = 64
CC_TEXT_BPE_MIN_GAIN = 4
CC_TEXT_BPE_RULES = (
    (255, 101, 32),
    (254, 115, 32),
    (253, 105, 110),
    (252, 116, 104),
    (251, 101, 114),
    (250, 116, 32),
    (249, 48, 48),
    (248, 100, 32),
    (247, 111, 117),
    (246, 97, 110),
    (245, 111, 110),
    (244, 121, 32),
    (243, 101, 110),
    (242, 111, 114),
    (241, 44, 32),
    (240, 253, 103),
    (239, 97, 108),
    (238, 101, 115),
    (237, 97, 114),
    (236, 111, 32),
    (235, 249, 249),
    (234, 116, 105),
    (233, 32, 99),
    (232, 111, 102),
    (231, 252, 255),
    (230, 240, 32),
    (229, 101, 248),
    (228, 101, 254),
    (227, 121, 247),
    (226, 116, 236),
    (225, 111, 111),
    (224, 114, 101),
    (223, 232, 32),
    (222, 251, 32),
    (221, 97, 32),
    (220, 105, 116),
    (219, 97, 116),
    (218, 115, 116),
    (217, 253, 32),
    (216, 225, 107),
    (215, 111, 109),
    (214, 99, 104),
    (213, 216, 105),
    (212, 108, 244),
    (211, 46, 32),
    (210, 111, 119),
    (209, 119, 105),
    (208, 246, 248),
    (207, 105, 254),
    (206, 101, 108),
    (205, 235, 235),
    (204, 97, 99),
    (203, 97, 115),
    (202, 32, 112),
    (201, 108, 105),
    (200, 97, 107),
    (199, 114, 32),
    (198, 111, 108),
    (197, 242, 32),
    (196, 101, 109),
    (195, 117, 110),
    (194, 97, 250),
    (193, 117, 114),
    (192, 114, 105),
    (191, 245, 32),
    (190, 118, 255),
    (189, 209, 252),
    (188, 118, 251),
    (187, 233, 213),
    (186, 108, 32),
    (185, 97, 100),
    (184, 97, 254),
    (183, 119, 104),
    (182, 115, 250),
    (181, 114, 111),
    (180, 101, 99),
    (179, 105, 108),
    (178, 2, 32),
    (177, 46, 10),
    (176, 101, 119),
    (175, 105, 99),
    (174, 103, 104),
    (173, 108, 101),
    (172, 189, 32),
    (171, 97, 109),
    (170, 84, 104),
    (169, 103, 114),
    (168, 115, 105),
    (167, 115, 104),
    (166, 117, 108),
    (165, 239, 32),
    (163, 243, 32),
    (162, 227, 199),
    (161, 102, 197),
    (160, 108, 255),
    (159, 246, 32)
)
def _cc_text_bpe_encode(raw):
    for code, a, b in CC_TEXT_BPE_RULES:
        raw = raw.replace(bytes((a, b)), bytes((code,)))
    return raw


class Texts:
    def __init__(self):
        self.blob = bytearray()
        self.index = {}

    def add(self, s):
        if s in self.index:
            return self.index[s]
        off = len(self.blob)
        original = s.encode("latin-1")
        coded = _cc_text_bpe_encode(original)
        if (CC_TEXT_BPE_MIN <= len(original) < CC_TEXT_BPE_SLOT and
                len(original) - len(coded) >= CC_TEXT_BPE_MIN_GAIN):
            off |= 0x80000000
            self.blob += coded + b"\0"
        else:
            self.blob += original + b"\0"
        self.index[s] = off
        return off


# ------------------------------------------------------------------ the game's tables
POOLS = {"": 0, "cookie": 1, "prestige": 2, "toggle": 3, "tech": 4, "debug": 5}
ACH_POOLS = {"normal": 0, "shadow": 1, "dungeon": 2}
SEASONS = {None: 0, "christmas": 1, "halloween": 2, "valentines": 3, "easter": 4, "fools": 5}


def cname(s):
    s = s.upper().replace("'", "").replace("&", "AND")
    s = re.sub(r"[^A-Z0-9]+", "_", s).strip("_")
    if s and s[0].isdigit():
        s = "_" + s
    return s


# ------------------------------------------------------------------ the news ticker
# The game's getNewTicker, read from main.js: each list.push(...) of its English branches becomes a template
# (choices nested in \x10 ... \x11 ... \x12, the bakery's name and numbers as \x13 codes, random numbers as
# \x14<n>:<add>;) with what makes it come up (NEWS_KINDS, read by news.c).
NEWS_KINDS = ["GRANDMA", "GRANDMA_THREAT", "GRANDMA_ANGRY", "GRANDMA_RETURN", "SOLD_GRANDMA", "BUILDING", "SEASON",
              "RARE_ACHIEV", "RARE_UPGRADE", "DUDE_SWEET", "WILL_WRIGHT", "MISC", "BUSINESS", "LORE", "WRATH",
              "FOOLS", "FOOLS_RARE", "FOOLS_BUILDING", "FOOLS_LORE"]


def js_tokens(src):
    toks = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c in " \t\r\n":
            i += 1
        elif src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
        elif src.startswith("/*", i):
            i = src.index("*/", i) + 2
        elif c in "'\"":
            j, out = i + 1, []
            while src[j] != c:
                if src[j] == "\\":
                    nx = src[j + 1]
                    out.append({"n": "\n", "t": "\t"}.get(nx, nx))
                    j += 2
                else:
                    out.append(src[j])
                    j += 1
            toks.append(("s", "".join(out)))
            i = j + 1
        elif c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] in "_."):
                j += 1
            toks.append(("i", src[i:j]))
            i = j
        elif c.isdigit():
            j = i
            while j < n and (src[j].isdigit() or src[j] == "."):
                j += 1
            toks.append(("n", src[i:j]))
            i = j
        else:
            two = src[i:i + 2]
            if two in ("==", "!=", ">=", "<=", "&&", "||"):
                toks.append(("p", src[i:i + 3] if src[i:i + 3] in ("===", "!==") else two))
                i += 3 if src[i:i + 3] in ("===", "!==") else 2
            else:
                toks.append(("p", c))
                i += 1
    return toks


class NewsParser:
    """the ticker's expressions: strings, +, choose([...]), loc(...), the few variables it uses"""

    def __init__(self, toks, en_lists, animals):
        self.t, self.i, self.en, self.animals = toks, 0, en_lists, animals

    def peek(self, k=0):
        return self.t[self.i + k] if self.i + k < len(self.t) else ("p", "")

    def take(self, v=None):
        tok = self.t[self.i]
        if v is not None and tok[1] != v:
            raise SystemExit(f"news: expected {v}, got {tok} near {self.t[self.i - 3:self.i + 3]}")
        self.i += 1
        return tok

    def args(self):
        self.take("(")
        out = []
        while self.peek()[1] != ")":
            out.append(self.expr())
            if self.peek()[1] == ",":
                self.take(",")
        self.take(")")
        return out

    def expr(self):
        e = self.add()
        if self.peek()[1] == "?":   # (EN ? a : b, or a plural)
            self.take("?")
            a = self.expr()
            self.take(":")
            b = self.expr()
            if e == ("EN",):
                return a
            if e == ("PLURAL1",):
                return ["\x13P"]
            raise SystemExit(f"news: a ? b : c on {e}")
        return e

    def add(self):
        parts = self.prim()
        while self.peek()[1] == "+":
            self.take("+")
            parts = self.cat(parts, self.prim())
        while self.peek()[1] in ("==", "<", ">"):   # (a comparison: only the plural's)
            op = self.take()[1]
            rhs = self.prim()
            if parts == ["\x13Y"] and op == "==" and rhs == ["1"]:
                parts = ("PLURAL1",)
            else:
                raise SystemExit(f"news: comparison {parts} {op} {rhs}")
        return parts

    @staticmethod
    def cat(a, b):
        if isinstance(a, tuple) or isinstance(b, tuple):
            raise SystemExit(f"news: can't join {a} and {b}")
        return a + b

    def choice(self, alts):
        return ["\x10" + "\x11".join("".join(x) for x in alts) + "\x12"]

    def prim(self):
        kind, v = self.take()
        if kind == "s":   # (a grandma's words: "...", signed)
            return [clean(v.replace("<sig>", " \u2013 ").replace("</sig>", "").replace("<q>", "\u201c")
                          .replace("</q>", "\u201d"), False)]
        if kind == "n":
            return [v]
        if v == "(":
            e = self.expr()
            self.take(")")
            return e
        if v == "[":
            items = []
            while self.peek()[1] != "]":
                items.append(self.expr())
                if self.peek()[1] == ",":
                    self.take(",")
            self.take("]")
            return ("LIST", items)
        if v == "choose":
            (a,) = self.args()
            if isinstance(a, tuple) and a[0] == "LIST":
                return self.choice(a[1])
            if isinstance(a, tuple) and a[0] == "LOCLIST":
                return self.choice([[x] for x in a[1]])
            if isinstance(a, list) and len(a) == 1 and a[0].startswith("\x10"):   # (choose(animals))
                return a
            raise SystemExit(f"news: choose of {a}")
        if v == "loc":
            a = self.args()
            key = "".join(a[0])
            if key in self.en and isinstance(self.en[key], list):
                return ("LOCLIST", [clean(x, False) for x in self.en[key]])
            text = self.en.get(key, key)
            if isinstance(text, list):
                text = text[0]
            if text == "/":   # (EN.js: the key itself)
                text = key
            for k, arg in enumerate(a[1:]):
                text = text.replace(f"%{k + 1}", "".join(arg))
            return [clean(text, False)]
        if v == "EN":
            return ("EN",)
        if v == "NEWS":
            return ["News : "]
        if v == "animals":
            return self.choice([[clean(x, False)] for x in self.animals])
        if v == "Game.bakeryName":
            return ["\x13B"]
        if v == "Game.Objects":   # (only Game.Objects['Grandma'].single)
            self.take("[")
            self.take()
            self.take("]")
            self.take(".")
            assert self.take()[1] == "single"
            return ["grandma"]
        if v == "Beautify":
            (a,) = self.args()
            return a
        if v == "Game.cookiesEarned":
            return ["\x13E"]
        if v == "Math.ceil":   # (Math.ceil(Game.cookiesEarned/8000000000))
            self.take("(")
            assert self.take()[1] == "Game.cookiesEarned" and self.take()[1] == "/" and self.take()[1] == "8000000000"
            self.take(")")
            return ["\x13Y"]
        if v == "Math.floor":   # (Math.floor(Math.random()*n+add))
            self.take("(")
            assert self.take()[1] == "Math.random"
            self.take("(")
            self.take(")")
            self.take("*")
            n = self.take()[1]
            self.take("+")
            add = self.take()[1]
            self.take(")")
            return [f"\x14{n}:{add};"]
        raise SystemExit(f"news: unknown {kind} {v}")


def news_templates(ref):
    """[(kind, arg, template)] of the game's English ticker"""
    src = open(os.path.join(ref, "main.js"), encoding="utf-8").read()
    a = src.index("Game.getNewTicker=function(manual)")
    b = src.index("for (var i=0;i<Game.modHooks['ticker'].length;i++)", a)
    body = src[a:b]
    en_src = open(os.path.join(ref, "loc", "EN.js"), encoding="utf-8").read()
    en = {}
    for m in re.finditer(r'"(Ticker \([^"]*\))":\s*\[(.*?)\]', en_src, re.S):
        en[m.group(1)] = [x for _, x in js_tokens("[" + m.group(2) + "]") if _ == "s"]
    for m in re.finditer(r'^\s*"([^"]+)":\s*"((?:[^"\\]|\\.)*)"', en_src, re.M):
        en.setdefault(m.group(1), m.group(2).replace('\\"', '"'))
    animals = re.search(r"var animals=\[(.*?)\];", body).group(1)
    animals = [v for k, v in js_tokens("[" + animals + "]") if k == "s"]
    toks = js_tokens(body)
    out = []
    # walk the tokens: the blocks the pushes are in (their conditions), and each push's own condition
    stack, pending, cond = [], None, []
    i = 0
    while i < len(toks):
        k, v = toks[i]
        if v == "if":
            # the condition's tokens
            j, depth = i + 1, 0
            while True:
                if toks[j][1] == "(":
                    depth += 1
                elif toks[j][1] == ")":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            cond = toks[i + 2:j]
            i = j + 1
            if toks[i][1] == "{":
                stack.append(cond)
                cond = []
                i += 1
            continue
        if v == "else":
            i += 1
            if toks[i][1] == "{":
                stack.append([("i", "ELSE")])
                i += 1
            continue
        if v == "{":
            stack.append([])
            i += 1
            continue
        if v == "}":
            stack.pop()
            i += 1
            continue
        if v == "list.push":
            js = lambda ts: "".join("'" + t[1] + "'" if t[0] == "s" else t[1] for t in ts)
            ctx = " ".join(js(c) for c in stack)
            own = js(cond)
            cond = []
            if "!EN" in ctx or own.startswith("!EN"):   # (the other languages' lists: past them)
                j, depth = i + 1, 0
                while True:
                    if toks[j][0] == "p":
                        depth += {"(": 1, ")": -1}.get(toks[j][1], 0)
                    j += 1
                    if depth == 0:
                        break
                i = j
                continue
            p = NewsParser(toks, en, animals)
            p.i = i + 1
            args = p.args()
            i = p.i
            for arg in args:
                if isinstance(arg, tuple) and arg[0] == "LOCLIST":
                    arg = p.choice([[x] for x in arg[1]])
                if isinstance(arg, tuple):
                    raise SystemExit(f"news: push of {arg}")
                kind, karg = news_kind(ctx, own)
                if kind:
                    out.append((kind, karg, "".join(arg)))
            continue
        if v == ";":
            cond = []
        i += 1
    return out


def news_kind(ctx, own):
    """what makes a push come up: its block (ctx) and its own condition"""
    fools = "Game.season=='fools'" in ctx and "EN&&" in ctx
    m = re.search(r"Game\.Objects\['([^']+)'\]\.amount>0", own)
    if fools:
        if "Game.TickerN%2==0" in ctx and m:
            return "FOOLS_BUILDING", m.group(1)
        lm = re.match(r"loreProgress<=(\d+)$", own)
        if lm:
            return "FOOLS_LORE", int(lm.group(1))
        if own == "Game.cookiesEarned>=1000":
            return "FOOLS", 0
        if own == "Game.cookiesEarned>=1000&&Math.random()<0.05":
            return "FOOLS_RARE", 0
        raise SystemExit(f"news: fools push under {own}")
    if "Game.elderWrath>0" in ctx:
        wm = re.match(r"Game\.elderWrath==(\d)$", own)
        return "WRATH", int(wm.group(1))
    if "list.length==0" in ctx:
        lm = re.match(r"loreProgress<=(\d+)$", own)
        return "LORE", int(lm.group(1))
    if own.startswith("Game.Objects['Grandma'].amount>0"):
        return "GRANDMA", 0
    if own == "!Game.prefs.notScary&&Game.Objects['Grandma'].amount>=50":
        return "GRANDMA_THREAT", 0
    if own == "EN&&Game.HasAchiev('Just wrong')&&Math.random()<0.05":
        return "SOLD_GRANDMA", 0
    if own == "!Game.prefs.notScary&&Game.HasAchiev('Just wrong')&&Math.random()<0.4":
        return "GRANDMA_ANGRY", 0
    if own.startswith("!Game.prefs.notScary&&Game.Objects['Grandma'].amount>=1&&Game.pledges>0"):
        return "GRANDMA_RETURN", 0
    if m:
        return "BUILDING", m.group(1)
    sm = re.match(r"Game\.season=='(\w+)'&&Game\.cookiesEarned>=1000$", own)
    if sm:
        return "SEASON", sm.group(1)
    if "Math.random()<0.05" in ctx:
        am = re.match(r"Game\.HasAchiev\('([^']+)'\)$", own)
        if am:
            return "RARE_ACHIEV", am.group(1)
        um = re.match(r"Game\.Has\('([^']+)'\)$", own)
        if um:
            return "RARE_UPGRADE", um.group(1)
    if own == "Game.HasAchiev('Dude, sweet')&&Math.random()<0.2":
        return "DUDE_SWEET", 0
    if "Math.random()<0.001" in ctx:
        return "WILL_WRIGHT", 0
    if own == "Game.cookiesEarned>=10000":
        return "MISC", 0
    raise SystemExit(f"news: a push under '{ctx}' / '{own}'")


ROW_FRAMES = {"javascriptconsole": 2, "idleverse": 4, "cortex": 4}


def main(ref, game_json):
    G = json.load(open(game_json))
    P = Pack()
    T = Texts()
    T.add("")
    defs = []

    # ---- the left column
    L = lambda name, rel, size, fmt="p8", box=None, n=256: P.image(name, scaled(ref, rel, size, box), fmt, n)
    big = round(256 * S)   # (drawn as they are, but when the cookie wobbles)
    L("COOKIE", "perfectCookie.png", (big, big))
    L("COOKIE_SHADOW", "cookieShadow.png", (big, big))
    L("FLARE", "flare.png", (112, 112))
    L("FLARE_GOLD", "flareGold.png", (112, 112))
    L("FLARE_RED", "flareSuckRed.png", (112, 112))
    L("GLINT", "glint.png", (16, 16))
    tile = round(512 * S)
    P.image("BG_BLUE", opaque(ref, "bgBlue.jpg", (tile, tile)), "p8")
    for i in (1, 2, 3):
        P.image(f"BG_GRANDMAS{i}", opaque(ref, f"grandmas{i}.jpg", (128, 128)), "p8")   # (drawn at tile)
    for k, f in (("SOFT", "shadedBordersSoft.png"), ("GOLD", "shadedBordersGold.png"), ("RED", "shadedBordersRed.png")):
        L("BORDER_" + k, f, (64, 64))
    for i in (1, 2, 3):
        L(f"SHOWER{i}", f"cookieShower{i}.png", (tile, tile))
    for i in range(8):
        L(f"SMALL_COOKIE{i}", "smallCookies.png", (24, 24), box=(64 * i, 0, 64 * i + 64, 64))
    L("CURSOR", "cursor.png", (32, 32), box=(0, 0, 32, 32))
    # the cursors around the cookie, turned beforehand: 32 turns of the 12-pixel cursor (drawn 4 times larger,
    # turned, then reduced: smooth)
    cur = load(ref, "cursor.png").crop((0, 0, 32, 32))
    side = round(32 * S)
    for k in range(32):
        big4 = Image.new("RGBA", (64, 64), (0, 0, 0, 0))
        c4 = premul_resize(cur, (side * 4, side * 4))
        big4.paste(c4, ((64 - side * 4) // 2, (64 - side * 4) // 2))
        turned = big4.rotate(-k * 360 / 32, resample=Image.BICUBIC)
        P.image(f"CURSOR_R{k}", premul_resize(turned, (16, 16)), "p4")
    L("GOLD_COOKIE", "goldCookie.png", (48, 48))
    L("WRATH_COOKIE", "wrathCookie.png", (48, 48))
    # the milks: the game draws its 240-pixel pictures at 480; here at 178 (480 x S): the top of them that the
    # milk shows, at most 0.35 of the column
    milk_tile = round(480 * S)
    milk_w, milk_h = milk_tile, math.ceil(0.35 * 220 + 3)   # (at the size drawn, 16 colours)
    defs.append(f"#define MILK_TILE {milk_tile}")
    for m in G["milks"]:
        rows = round(milk_h / milk_w * 240)
        L("MILK_" + cname(m["name"].replace(" milk", "")), m["pic"], (milk_w, milk_h), "p4", box=(0, 0, 240, rows))
    L("WRINKLER", "wrinkler.png", (38, 76))
    L("SHINY_WRINKLER", "shinyWrinkler.png", (38, 76))
    L("SHIMMERING_VEIL", "shimmeringVeil.png", (64, 64))
    L("DRAGON_BG", "dragonBG2.png", (112, 112))
    for i in range(10):
        L(f"BROKEN_COOKIE{i}", "brokenCookie.png", (64, 64), box=(256 * i, 0, 256 * i + 256, 256))
    L("BROKEN_HALO", "brokenCookieHalo.png", (72, 72))
    L("SHINE_SPOKE", "shineSpoke.png", (48, 48))
    P.image("HEAVEN_RING1", opaque(ref, "heavenRing1.jpg", (64, 64)), "p8")
    P.image("HEAVEN_RING2", opaque(ref, "heavenRing2.jpg", (64, 64)), "p8")
    P.image("STARS", opaque(ref, "starbg.jpg", (192, 192)), "p8")

    # ---- the store
    for i in range(4):
        P.image(f"STORE_TILE{i}", opaque(ref, "storeTile.jpg", (174, 32), (0, 64 * i, 300, 64 * i + 64)), "rgb")
    L("UPGRADE_FRAME", "upgradeFrame.png", (87, 58))
    L("MONEY", "money.png", (10, 10))
    L("HEAVENLY_MONEY", "heavenlyMoney.png", (10, 10))
    L("LUMP_MONEY", "sugarLump.png", (10, 10))
    P.image("PANEL_V", opaque(ref, "panelVertical.png", (4, 128), (0, 0, 16, 512)), "rgb")
    P.image("PANEL_H", opaque(ref, "panelHorizontal.png", (128, 4), (0, 0, 512, 16)), "rgb")
    P.image("DARK_NOISE", opaque(ref, "darkNoise.jpg", (128, 128)), "p8")
    for b in G["objects"]:
        r = b["icon"]
        for col, tag in ((0, ""), (1, "_OFF")):
            L(f"BUILD_{cname(b['name'])}{tag}", "buildings.png", (32, 32), box=(64 * col, 64 * r, 64 * col + 64, 64 * r + 64))

    # ---- the buildings' rows (the middle column), at 0.375: rows 48 pixels high
    pics = ["grandma", "farm", "mine", "factory", "bank", "temple", "wizardtower", "shipment", "alchemylab", "portal",
            "timemachine", "antimattercondenser", "prism", "chancemaker", "fractalEngine", "javascriptconsole",
            "idleverse", "cortex", "you"]
    for p in pics:
        if p != "you":
            L("ROW_BG_" + cname(p), p + "Background.png", (48, 48))
        else:
            L("ROW_BG_YOU", "youBackground.png", (48, 48))
        src = load(ref, p + ".png")
        frames = ROW_FRAMES.get(p, 1)
        if frames == 1:
            L("ROW_" + cname(p), p + ".png", (24, 24))
        else:   # (slices side by side, as the game cuts them)
            fw, fh = src.size[0] // frames, src.size[1]
            for k in range(frames):
                L(f"ROW_{cname(p)}{k}", p + ".png", (max(1, round(fw * 0.375)), round(fh * 0.375)),
                  box=(fw * k, 0, fw * k + fw, fh))
    grandmas = ["farmerGrandma", "workerGrandma", "minerGrandma", "cosmicGrandma", "transmutedGrandma",
                "alteredGrandma", "grandmasGrandma", "antiGrandma", "rainbowGrandma", "bankGrandma", "templeGrandma",
                "witchGrandma", "luckyGrandma", "metaGrandma", "scriptGrandma", "alternateGrandma", "brainyGrandma",
                "cloneGrandma", "elfGrandma", "bunnyGrandma"]
    for g in grandmas:
        L("ROW_" + cname(g), g + ".png", (24, 24))
    for k, f in (("GRIMOIRE", "BGgrimoire.jpg"), ("PANTHEON", "BGpantheon.jpg"), ("GARDEN", "BGgarden.jpg"),
                 ("MARKET", "BGmarket.jpg")):
        P.image("MG_BG_" + k, opaque(ref, f, (192, 48)), "p8")
    for i in range(9):
        L(f"DRAGON{i}", "dragon.png", (48, 48), box=(96 * i, 0, 96 * i + 96, 96))
    for i in range(15):
        L(f"SANTA{i}", "santa.png", (48, 48), box=(96 * i, 0, 96 * i + 96, 96))
    L("SPELL_BG", "spellBG.png", (120, 111))
    for i in range(3):   # (the heavenly upgrades' slots: bought, chosen, not bought)
        L(f"ASCEND_SLOT{i}", "ascendSlot.png", (40, 40), box=(88 * i, 0, 88 * i + 88, 88))

    # ---- the seasons' shimmers: reindeer, and golden cookies as wreaths, hearts, eggs, familiars
    L("REINDEER", "frostedReindeer.png", (62, 78))
    L("GOLD_WREATH", "goldCookieWreath.png", (48, 48))
    L("WRATH_WREATH", "wrathCookieWreath.png", (48, 48))
    for name, f, n in (("HEART", "hearts.png", 8), ("BUNNY", "bunnies.png", 4), ("FAMILIAR", "familiars.png", 4)):
        for row in range(2):
            for k in range(n):
                L(f"{name}{row}_{k}", f, (48, 48), box=(96 * k, 96 * row, 96 * k + 96, 96 * row + 96))

    # ---- the icons: 36 x 37 of 48 pixels, at 24
    sheet = load(ref, "icons.png")
    cols, rows = sheet.size[0] // 48, sheet.size[1] // 48
    icon_index = []
    for y in range(rows):
        for x in range(cols):
            cell = sheet.crop((48 * x, 48 * y, 48 * x + 48, 48 * y + 48))
            if np.asarray(cell)[..., 3].max() < 8:
                icon_index.append(0xFFFF)
                continue
            icon_index.append(P.image(None, premul_resize(cell, (24, 24)), "p4", place=(x, y)))
    P.lay_out()
    icons_off = P.put(struct.pack(f"<{len(icon_index)}H", *icon_index))
    defs += [f"#define ICON_COLS {cols}", f"#define ICON_ROWS {rows}", f"#define OFF_ICONS {icons_off}",
             f"#define OFF_PRIORS {P.priors}"]

    # ---- fonts
    F = os.path.join(ref, "fonts")
    mw = os.path.join(F, "Merriweather.ttf")
    ns = os.path.join(F, "NotoSans.ttf")
    nsi = os.path.join(F, "NotoSans-Italic.ttf")
    fonts = [
        ("TITLE", load_font(mw, 13, [900, 100, 18]), None),
        ("NAME", load_font(mw, 11, [900, 100, 18]), None),
        ("NAME_SMALL", load_font(mw, 9, [900, 100, 18]), None),
        ("OWNED", load_font(mw, 19, [900, 100, 18]), None),
        ("BODY", load_font(ns, 10, [500, 100]), 12),
        ("BOLD", load_font(ns, 10, [800, 100]), 12),
        ("ITALIC", load_font(nsi, 10, [500, 100]), 12),
        ("SMALL", load_font(ns, 9, [700, 100]), 11),
        ("KAVOON", load_font(os.path.join(F, "Kavoon.ttf"), 14), None),
    ]
    font_offs = []
    for name, f, line in fonts:
        font_offs.append(P.put(font_bytes(f, FONT_CHARS, line)))
        defs.append(f"#define F_{name} {len(font_offs) - 1}")
    fonts_off = P.put(struct.pack(f"<{len(font_offs)}I", *font_offs))
    defs.append(f"#define OFF_FONTS {fonts_off}")

    # ---- the game: buildings, tiers, upgrades, achievements
    up_by_name = {u["name"]: u["id"] for u in G["upgrades"]}
    obj_by_name = {o["name"]: o["id"] for o in G["objects"]}
    tier_keys = list(G["tiers"].keys())

    def icon_of(ic):
        if not ic:
            return 0xFFFF
        return ic[1] * cols + ic[0]

    # buildings: 56 bytes each
    brec = bytearray()
    for o in G["objects"]:
        brec += struct.pack("<ddIIIIIIIIhhhH", o["basePrice"], o.get("baseCps") or 0.1, T.add(clean(o["name"], False)),
                            T.add(clean(o["single"], False)), T.add(clean(o["plural"], False)),
                            T.add(clean(o["actionName"], False)), T.add(clean(o["extraName"] or "", False)),
                            T.add(clean(o["extraPlural"] or "", False)), T.add(clean(o["desc"])),
                            0, o["icon"], o["iconColumn"], 0, 0)
    assert len(brec) == 56 * len(G["objects"])
    defs.append(f"#define OFF_BUILDINGS {P.put(bytes(brec), 8)}")
    defs.append(f"#define N_BUILDINGS {len(G['objects'])}")
    for o in G["objects"]:
        defs.append(f"#define B_{cname(o['name'])} {o['id']}")

    # tiers: 24 bytes each
    trec = bytearray()
    for k in tier_keys:
        t = G["tiers"][k]
        c = int(t["color"][1:], 16)
        trec += struct.pack("<dIhhhhHH", float(t["price"]), T.add(clean(t["name"], False)), t["unlock"],
                            t.get("achievUnlock", -1), t["iconRow"], 1 if t.get("special") else 0,
                            rgb565(c >> 16, c >> 8 & 255, c & 255),
                            up_by_name.get(t.get("req"), 0xFFFF) if t.get("req") else 0xFFFF)
    assert len(trec) == 24 * len(tier_keys)
    defs.append(f"#define OFF_TIERS {P.put(bytes(trec), 8)}")
    defs.append(f"#define N_TIERS {len(tier_keys)}")
    for i, k in enumerate(tier_keys):
        defs.append(f"#define TIER_{cname(k)} {i}")

    # upgrades: 56 bytes each
    urec = bytearray()
    for u in G["upgrades"]:
        desc, quote = split_quote(u["desc"])
        tier = tier_keys.index(str(u["tier"])) if str(u.get("tier", 0)) in tier_keys else 0xFF
        power = u["power"] if isinstance(u.get("power"), (int, float)) else 0
        ua = u.get("unlockAt") or {}
        req = up_by_name.get(ua.get("require"), 0xFFFF) if ua.get("require") else 0xFFFF
        parents = [up_by_name[p] for p in u.get("parents", [])][:6] + [0xFFFF] * 6
        urec += struct.pack("<dfIIIHBBbbBBfH6HhhHd", float(u["basePrice"] or 0), float(u["order"] or 0),
                            T.add(clean(u["name"], False)), T.add(clean(desc)), T.add(clean(quote)),
                            icon_of(u["icon"]), POOLS[u["pool"]], tier,
                            obj_by_name.get(u.get("buildingTie1") or u.get("buildingTie"), -1),
                            obj_by_name.get(u.get("buildingTie2"), -1), 1 if u.get("kitten") else 0,
                            SEASONS.get(u.get("season"), 0) | (0x80 if u.get("vanilla") else 0), float(power),
                            req, *parents[:6], int(u.get("posX") or 0), int(u.get("posY") or 0), 0,
                            float(ua.get("cookies") or 0))
    assert len(urec) == 64 * len(G["upgrades"]), len(urec) / len(G["upgrades"])
    defs.append(f"#define OFF_UPGRADES {P.put(bytes(urec), 8)}")
    defs.append(f"#define N_UPGRADES {len(G['upgrades'])}")
    seen = {}
    for u in G["upgrades"]:
        n = cname(u["name"])
        if n in seen:
            n = f"{n}_{u['id']}"
        seen[n] = 1
        defs.append(f"#define U_{n} {u['id']}")

    # achievements: 20 bytes each
    arec = bytearray()
    for a in G["achievements"]:
        desc, quote = split_quote(a["desc"])
        arec += struct.pack("<IIIfHBb", T.add(clean(a["name"], False)), T.add(clean(desc)), T.add(clean(quote)),
                            float(a["order"] or 0), icon_of(a["icon"]), ACH_POOLS[a["pool"]],
                            obj_by_name.get(a.get("buildingTie"), -1))
    assert len(arec) == 20 * len(G["achievements"])
    defs.append(f"#define OFF_ACHIEVS {P.put(bytes(arec), 4)}")
    defs.append(f"#define N_ACHIEVS {len(G['achievements'])}")
    seen = {}
    for a in G["achievements"]:
        n = cname(a["name"])
        if n in seen:
            n = f"{n}_{a['id']}"
        seen[n] = 1
        defs.append(f"#define A_{n} {a['id']}")

    # milks
    defs.append(f"#define N_MILKS {len(G['milks'])}")
    mrec = b"".join(struct.pack("<IH", T.add(clean(m["name"], False)), icon_of(m["icon"])) + b"\0\0"
                    for m in G["milks"])
    defs.append(f"#define OFF_MILKS {P.put(mrec)}")

    # ---- what ties them together (res.h's BuildingLinks; lists of ids; thresholds)
    L = G["links"]
    NONE = 0xFFFF
    u16 = lambda v: NONE if v is None or v < 0 else v
    lrec = bytearray()
    for o in L["objects"]:
        tiered = [u16(o["tieredUpgrades"].get(k, -1)) for k in tier_keys]
        tach = [u16(o["tieredAchievs"].get(k, -1)) for k in tier_keys]
        syn = (o["synergies"] + [-1] * 6)[:6]
        prod = (o["production"] + [[0, -1]] * 4)[:3]
        lrec += struct.pack(f"<{len(tier_keys)}H{len(tier_keys)}H6HHHHH4HHH3d", *tiered, *tach, *map(u16, syn),
                            u16(o["fortune"]), u16(o["grandma"]), u16(o["unshackle"]), u16(o["level10"]),
                            *[u16(p[1]) for p in prod], NONE, 0, 0, *[float(p[0]) for p in prod])
    assert len(lrec) == 128 * len(L["objects"]), len(lrec)
    defs.append(f"#define OFF_BUILDING_LINKS {P.put(bytes(lrec), 8)}")

    def id_list(name, ids):
        defs.append(f"#define OFF_{name} {P.put(struct.pack(f'<{len(ids)}H', *map(u16, ids)) if ids else b'')}")
        defs.append(f"#define N_{name} {len(ids)}")

    id_list("TIER_UNSHACKLE", [L["tierUnshackle"].get(k, -1) for k in tier_keys])
    for name, key in (("COOKIE_UPGRADES", "cookieUpgrades"), ("GRANDMA_SYNERGIES", "grandmaSynergies"),
                      ("KITTENS", "kittens"), ("GOLDEN_UPGRADES", "goldenCookieUpgrades"), ("LASTING", "lasting"),
                      ("NO_PERM", "noPerm"), ("HEAVENLY", "heavenly"), ("EASTER_EGGS", "easterEggs"),
                      ("HALLOWEEN_DROPS", "halloweenDrops"), ("HEART_DROPS", "heartDrops"),
                      ("SANTA_DROPS", "santaDrops"), ("REINDEER_DROPS", "reindeerDrops"), ("FORTUNES", "fortuneTier"),
                      ("SHOW_IF", "showIf"), ("CONFIRM", "requiresConfirmation")):
        id_list(name, L[key])
    for name, key in (("BANK_ACHIEVS", "bank"), ("CPS_ACHIEVS", "cps")):
        rec = b"".join(struct.pack("<dHHI", float(t), u16(i), 0, 0) for t, i in L[key])
        defs.append(f"#define OFF_{name} {P.put(rec, 8)}")
        defs.append(f"#define N_{name} {len(L[key])}")
    rec = b"".join(struct.pack("<dHHHHB7x", float(u["cookies"]), u16(u["up"]), u16(u["ach"]), u16(u["requireUp"]),
                               u16(u["requireAch"]), SEASONS.get(u["season"], 0)) for u in L["unlockAt"])
    defs.append(f"#define OFF_UNLOCK_AT {P.put(rec, 8)}")
    defs.append(f"#define N_UNLOCK_AT {len(L['unlockAt'])}")

    # ---- the news ticker: its templates, and what brings each up (news.c)
    news = news_templates(ref)
    objects = [o["name"] for o in G["objects"]]
    ach_by_name = {a["name"]: a["id"] for a in G["achievements"]}
    nrec = bytearray()
    for kind, arg, text in news:
        if kind in ("BUILDING", "FOOLS_BUILDING"):
            arg = objects.index(arg)
        elif kind == "SEASON":
            arg = SEASONS[arg]
        elif kind == "RARE_ACHIEV":
            arg = ach_by_name[arg]
        elif kind == "RARE_UPGRADE":
            arg = up_by_name[arg]
        nrec += struct.pack("<IBBH", T.add(text), NEWS_KINDS.index(kind), 0, arg)
    defs.append(f"#define OFF_NEWS {P.put(bytes(nrec))}")
    defs.append(f"#define N_NEWS {len(news)}")
    for i, k in enumerate(NEWS_KINDS):
        defs.append(f"#define NK_{k} {i}")

    # ---- the texts, the pictures' table
    text_off = P.put(bytes(T.blob))
    defs.append(f"#define OFF_TEXT {text_off}")
    img_off = P.put(pictures.records(P.recs))
    defs.append(f"#define OFF_IMAGES {img_off}")
    defs.append(f"#define N_IMAGES {len(P.images)}")
    for name, i in P.names.items():
        defs.append(f"#define IM_{name} {i}")
    defs.append(f"#define DATA_SIZE {len(P.blob)}")

    # ---- the app's icon: the big cookie on the blue background, its shine and shadow, rounded corners
    icon = Image.new("RGBA", (55, 56))
    bg = load(ref, "bgBlue.jpg").resize((110, 110), Image.LANCZOS).crop((27, 27, 82, 83))
    icon.alpha_composite(bg)
    icon.alpha_composite(premul_resize(load(ref, "shadedBordersSoft.png"), (55, 56)))
    shine = premul_resize(load(ref, "flare.png"), (60, 60))
    icon.alpha_composite(shine, (-3, -2))
    icon.alpha_composite(premul_resize(load(ref, "cookieShadow.png"), (46, 46)), (5, 9))
    icon.alpha_composite(premul_resize(load(ref, "perfectCookie.png"), (45, 45)), (5, 5))
    mask = Image.new("L", (55 * 4, 56 * 4), 0)
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, 55 * 4 - 1, 56 * 4 - 1), radius=40, fill=255)
    icon.putalpha(Image.fromarray(np.minimum(np.asarray(icon)[..., 3], np.asarray(mask.resize((55, 56), Image.LANCZOS)))))
    icon.save(OUT_ICON)

    with open(OUT_BIN, "wb") as f:
        f.write(P.blob)
    lines = ["/* Written by tools/pack.py: what data.bin holds. */", "#ifndef CC_DATA_H", "#define CC_DATA_H", ""]
    lines += defs
    lines += ["", "#endif", ""]
    with open(OUT_H, "w") as f:
        f.write("\n".join(lines))
    if UNKNOWN:
        print("pack: characters left out:", "".join(sorted(UNKNOWN)))
    pictures.check(P.images, bytes(P.blob), dict(N_IMAGES=len(P.images), OFF_IMAGES=img_off, OFF_PRIORS=P.priors))
    print(f"data.bin: {len(P.blob)} bytes, {len(P.images)} pictures ({icons_off} bytes), texts {len(T.blob)}")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    main(sys.argv[1], sys.argv[2])
