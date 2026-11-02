#!/usr/bin/env python3
"""FNAF World: its pictures, its map and fonts -> src/data.bin, src/data.h and src/anims.h.

    python3 -I tools/pack.py [CCN]     CCN: the game's data, written plain by ~/Documents/Assets/gamesrc/fnafworld/
                                       tools/unpack.py from its fnaf-world.exe (Update 2); by default
                                       ~/Documents/Assets/gamesrc/fnafworld/dl/app.ccn

Every picture is the game's own, read by tools/ccn.py. The battle is the game's 800 x 480 screen at 0.4 (320 x 192),
its characters, enemies and effects pictures with masks (tools/pics.py); the map is at 0.4 too (tools/world.py);
title and menus at 0.5 (their middle 640 x 480).

data.bin: "FNFW", the number of sections, their offsets, then the sections (SEC_* in data.h): HUFF, QUANT, PICS,
TABLE, BANK, MASK (tools/pics.py), FONTS (as Five Nights at Freddy's 4's), IMGS (small 4-bit alpha pictures:
u16 width, height, 4 bytes, the rows), SPRS and WORLD (tools/world.py).

anims.h: the game's animations (AN_*): their pictures, speed (a picture each 100 / speed frames), how many times
they play (0: for ever) and the picture they go back to."""
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import ccn  # noqa: E402
import pics  # noqa: E402
import world  # noqa: E402

OUT_BIN = os.path.join(HERE, "..", "src", "data.bin")
OUT_H = os.path.join(HERE, "..", "src", "data.h")
OUT_ANIMS = os.path.join(HERE, "..", "src", "anims.h")
ROOT = os.path.join(HERE, "..", "..", "..")
SECTIONS = ["HUFF", "QUANT", "PICS", "TABLE", "BANK", "MASK", "FONTS", "IMGS", "SPRS", "WORLD"]
FONT_DIRS = [os.environ.get("FONTS", ""), "/usr/share/fonts/truetype/dejavu", "/usr/share/fonts/TTF",
             "/usr/local/share/fonts", "/Library/Fonts", os.path.expanduser("~/Library/Fonts"),
             os.path.expanduser("~/Documents/Assets/gamesrc/fonts/dejavu")]
S = 0.4
OW = "overworld"
APP = None
BANK = pics.Bank()
ANIMS = []          # (key, [picture indexes], speed, plays, back)
DEFINES = []


def define(name, value):
    DEFINES.append((name, value))


def find_font(name):
    for d in FONT_DIRS:
        p = os.path.join(d, name) if d else ""
        if p and os.path.exists(p):
            return p
    raise SystemExit(f"pack: {name} not found (the DejaVu fonts; FONTS=<their folder>)")


def font(size, bold=False, mono=False):
    name = "DejaVu" + ("SansMono" if mono else "Sans") + ("-Bold" if bold else "") + ".ttf"
    return ImageFont.truetype(find_font(name), size)


def arr(im):
    return np.asarray(im.convert("RGB")).astype(np.float64)


# ------------------------------------------------------------------ the game's pictures
_BY_H = {}


def spic(h, scale=S, quality="sprite", prev=None):
    """the game's picture h as a picture with a mask (each once)"""
    key = (h, scale)
    if key not in _BY_H:
        _BY_H[key] = BANK.sprite(f"H{h}_{int(scale * 100)}", APP.picture(h), scale, quality,
                                 refs=[prev] if prev else [], hot=APP.hotspot(h)).index
    return _BY_H[key]


def anim(key, obj, a=0, d=0, scale=S, frame=OW, quality="sprite", keep=None):
    """one of the game's animations: its pictures (each once) and its timing; keep: which pictures"""
    o = obj if isinstance(obj, int) else APP.obj(obj, frame)
    hs = APP.anim(o, a, d)
    speed, plays, back = APP.speed(o, a, d)
    idx, prev = [], None
    for i, h in enumerate(hs):
        if keep is not None and i not in keep:
            continue
        p = spic(h, scale, quality, BANK.pics[prev] if prev is not None else None)
        idx.append(p)
        prev = p
    ANIMS.append((key, idx, speed, plays, min(back, len(idx) - 1)))
    return key


def screen_pic(name, im, x0=80, scale=0.5, quality="face", base=None):
    """an 800 x 480 picture at scale, its 320 x 240 from x0 (in the scaled picture)"""
    a = arr(im.convert("RGB").resize((round(im.width * scale), round(im.height * scale)), Image.LANCZOS))
    return BANK.add(name, a[:240, x0:x0 + 320], None, quality, base=base).index


def composite(frame, moves=None, skip=()):
    """a frame of the game as it shows (its visible objects at their places), with some moved"""
    f = APP.frames[frame]
    c = Image.new("RGBA", (f.w, f.h), (0, 0, 0, 255))
    for o, x, y, layer in sorted(f.placed, key=lambda t: t[3]):
        kind, name, an, image = APP.objects[o]
        if kind == 1 and image is not None:
            h = image
        elif kind == 2 and an and APP.visible[o]:
            h = an[min(an)][0]
        else:
            continue
        if o in skip or name in skip:
            continue
        if moves and o in moves:
            x, y = moves[o](x, y)
        hx, hy = APP.hotspot(h)
        layer_im = Image.new("RGBA", (f.w, f.h))
        layer_im.paste(APP.picture(h), (x - hx, y - hy))
        c.alpha_composite(layer_im)
    return c


def char_anim(c):
    """character c (1 to 48)'s animation number in "character" and "attack" (the events 30-77, 419-466)"""
    return 0 if c == 1 else c + 10 if c <= 40 else c + 11


def battle():
    for c in range(1, 49):
        anim(f"IDLE{c}", "character", char_anim(c))
        anim(f"ATTACK{c}", "attack", char_anim(c))
    anim("KO", "character", 51)
    tg = APP.obj("target", OW)
    for a in [0] + list(range(12, 41)):
        anim(f"ENEMY{a}", tg, a)
    c2 = APP.obj("character 2", OW)
    for c in range(9, 41):
        anim(f"CHAL{c}", c2, c + 10)
    for a in range(51, 64):
        anim(f"BOSS{a}", c2, a)
    # the scenes: each "background" as the battle's camera shows it (the view from 23, 14)
    for n, nm in ((0, "background"), (2, "background 2"), (3, "background 3"), (4, "background 4"),
                  (5, "background 5"), (6, "background 6"), (7, "background 7"), (8, "background 8"),
                  (9, "background 9"), (11, "background 11")):
        h = APP.anim(nm, 0, 0, OW)[0]
        im = APP.picture(h).convert("RGB").crop((23, 14, 823, 494)).resize((320, 192), Image.LANCZOS)
        p = BANK.add(f"BG{n}", arr(im), None, "scene")
        define(f"PIC_BG{n}", p.index)
    # the effects
    ae = APP.obj("attack effect", OW)
    for a in [0] + list(range(12, 22)):
        anim(f"HIT{a}", ae, a)
    anim("SPARK", "Active 5", 0)
    for key, obj, a in (("MICSPIN", "micspin", 0), ("PIZZA", "pizza", 0), ("PIZZA2", "pizza", 12),
                        ("BITE", "bite", 0), ("BITE2", "bite 2", 0), ("BITE3", "bite 3", 0), ("MEGABITE", "megabite", 0),
                        ("BITE4", "bite 4", 0), ("TOXICBITE", "toxic bite", 0), ("HOOK", "Hook", 0),
                        ("SCARE", "Active 8", 0), ("NOTE", "happy note", 0), ("NOTE2", "happy note", 12),
                        ("TORNADO", "Active 7", 0), ("CLAW", "claw", 0), ("CLAW2", "claw", 12), ("DIZZY", "confused 2", 0),
                        ("BROKEN", "broken", 0), ("POW", "pow", 0), ("STAR", "star", 0), ("CLOUD", "cloud", 0),
                        ("PRIZE", "prize ball", 12), ("PRIZE2", "prize ball", 15), ("POPPER", "popper", 12),
                        ("MUNCHIE", "munchie", 0), ("MUNCHIE2", "munchie", 12), ("HEAD", "head", 0),
                        ("FREDDLE", "Freddle", 0), ("BADPIZZA", "bad pizza", 0), ("ENDO", "endo army", 12),
                        ("ENDO2", "endo army", 13), ("DAGGER", "dagger", 0), ("SAW", "saw", 0),
                        ("JACKO", "Active 35", 0), ("ANTI", "anti", 0), ("GIFT", "gift p 1", 0),
                        ("MIMIC", "mimic", 0), ("MIMIC2", "mimic", 12), ("SCREW", "screw", 0),
                        ("DRIP", "cheese drip", 0), ("BADNOTE", "bad note", 0), ("SYTH", "syth", 0),
                        ("STINGER", "stinger", 0), ("PBUBBLE", "P bubble", 0), ("SPARKLE", "sparkle1", 0),
                        ("VICTORY", "victory", 0), ("LEVELUP", "level up", 0), ("RUN", "run", 0),
                        ("CHALLENGER", "Active 26", 0), ("NEWMEMBER", "new member", 0), ("WALL", "wall", 0),
                        ("BUBBLE", "bubble", 0), ("DOWN", "down", 0), ("DOWN2", "down", 12), ("DOWN3", "down", 13)):
        anim(key, obj, a)
    for k, a in enumerate((0, 12, 13, 14)):
        anim(f"BALLOON{k}", "balloon", a)
    anim("WATER", "water", 0, keep=range(0, 6, 2))
    anim("WATER2", "water", 13, keep=range(0, 6, 2))
    anim("PUDDLE", "cheese puddle", 4)
    anim("SHIELD", "shield", 0, scale=0.4)
    eat = APP.obj("enemy attack", OW)
    for a in [0] + list(range(12, 22)):
        anim(f"SPECIAL{a}", eat, a)
    bi = APP.obj("Byte Icon", OW)
    for b in range(1, 22):
        anim(f"BYTE{b}", bi, 0 if b == 1 else b + 10)


def menus():
    # the title as it ends up (the logo slid in, the characters up, the button in)
    f = APP.frames["title screen"]
    moves = {}
    for o, x, y, layer in f.placed:
        n = APP.objects[o][1]
        if n == "Active 2" and x < 0:
            moves[o] = lambda x, y: (x + 800, y)
        elif n == "Active" and x > 900:
            moves[o] = lambda x, y: (x - 550, y)
        elif n == "Active 3":
            moves[o] = lambda x, y: (x, y - 416)
    define("PIC_TITLE", screen_pic("TITLE", composite("title screen", moves, skip=("Active 32",))))
    bg = APP.anim(APP.find("Active", "file setup")[0], 0, 0)[0]
    define("PIC_MENU", screen_pic("MENU", APP.picture(bg).convert("RGB")))
    # the buttons (the game's own, their words in them)
    for key, name in (("NEWGAME", "new game"), ("CONTINUE", "continue"), ("YES", "YES"), ("NO", "NO"),
                      ("ADVENTURE", "adventure button"), ("FIXED", "fixed party text"), ("NORMAL", "normal"),
                      ("HARD", "hard"), ("SLOT", "file slot 1")):
        o = APP.find(name, "file setup")[0]
        define(f"PIC_B_{key}", spic(APP.anim(o, 0, 0)[0], 0.5, "face"))
    # the character select's faces (each "char" animation's picture)
    o = APP.find("char", "character select")[0]
    for c in range(1, 49):
        h = APP.anim(o, 0 if c == 1 else c + 10, 0)[0]   # (no gap at 51 here: the events 52-72)
        im = APP.picture(h).convert("RGBA")
        bgc = Image.new("RGBA", im.size, (24, 24, 48, 255))
        bgc.alpha_composite(im)
        a = arr(bgc.resize((28, 28), Image.LANCZOS))
        p = BANK.add(f"FACE{c}", np.pad(a, ((0, 4), (0, 4), (0, 0)), mode="edge"), None, "face")
        if c == 1:
            define("PIC_FACE1", p.index)
    for k, n in enumerate(("trophy1", "trophy 2", "trophy 3", "pixel trophy", "spirit trophy", "end trophy", "trophy 4")):
        o = APP.find(n, "file setup")[0]
        define(f"PIC_TROPHY{k + 1}", spic(APP.anim(o, 0, 0)[0], 0.4, "face"))


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


def icons():
    """the arrow, the chip pictures"""
    up = Image.new("L", (9, 6))
    ImageDraw.Draw(up).polygon([(0, 5), (4, 0), (8, 5)], fill=255)
    img("UP", np.asarray(up))
    img("DOWN", np.asarray(up)[::-1])
    k = Image.new("L", (13, 9))   # the backspace key
    d = ImageDraw.Draw(k)
    d.polygon([(0, 4), (4, 0), (12, 0), (12, 8), (4, 8)], outline=255)
    d.line([(6, 2), (10, 6)], fill=255)
    d.line([(6, 6), (10, 2)], fill=255)
    img("DEL", np.asarray(k))


def make_icon():
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from make_icon import corner_mask
    o = APP.find("char", "character select")[0]
    im = APP.picture(APP.anim(o, 0, 0)[0]).convert("RGBA")
    c = Image.new("RGBA", im.size, (40, 90, 200, 255))
    c.alpha_composite(im)
    c = c.convert("RGB").resize((55, 56), Image.LANCZOS)
    c.putalpha(corner_mask(55, 56))
    c.save(os.path.join(HERE, "..", "src", "icon.png"), optimize=True)


def write_anims():
    flat, lines = [], []
    for key, idx, speed, plays, back in ANIMS:
        lines.append((key, len(flat), len(idx), speed, plays, back))
        flat += idx
    h = ["/* Made by tools/pack.py: the game's animations (AN_*), its own speeds and plays. Included once (anim.c). */",
         "#ifndef FNAFW_ANIMS_H", "#define FNAFW_ANIMS_H", "#include <stdint.h>", "",
         "typedef struct {", "  uint16_t first;   /* in ANIM_PICS */",
         "  uint8_t n, speed, plays, back;   /* a picture lasts 100 / speed frames; plays 0: for ever */",
         "} AnimDef;", ""]
    h.append("enum {")
    h += [f"  AN_{k}," for k, *_ in lines]
    h += ["  AN_COUNT", "};", ""]
    h.append("static const uint16_t ANIM_PICS[] = {")
    for i in range(0, len(flat), 16):
        h.append("  " + ", ".join(str(v) for v in flat[i:i + 16]) + ",")
    h += ["};", "", "static const AnimDef ANIMS[AN_COUNT] = {"]
    h += [f"  {{{f}, {n}, {s}, {min(p, 255)}, {b}}},   /* {k} */" for k, f, n, s, p, b in lines]
    h += ["};", "", "#endif", ""]
    open(OUT_ANIMS, "w").write("\n".join(h))


def main():
    global APP
    APP = ccn.App(sys.argv[1] if len(sys.argv) > 1 else ccn.CCN)
    menus()
    battle()
    icons()
    make_icon()
    wdata, wdefs = world.build(APP)
    DEFINES.extend(wdefs)
    huff, quant, pics_, table, bank, masks = BANK.encode()
    fonts = [
        ttf_font(font(11, bold=True)),              # FONT_SMALL
        ttf_font(font(13, bold=True)),              # FONT_TEXT
        ttf_font(font(18, bold=True)),              # FONT_BIG
        ttf_font(font(10, bold=True, mono=True)),   # FONT_NUM
        ttf_font(font(26, bold=True)),              # FONT_HUGE
    ]
    sections = [huff, quant, pics_, table, bank, masks, pack_fonts(fonts), pack_imgs(), world.pack_sprites(), wdata]
    head = b"FNFW" + struct.pack("<I", len(sections))
    offs, p = [], len(head) + 4 * len(sections)
    for s in sections:
        offs.append(p)
        p += len(s) + (-len(s) % 4)
    out = head + b"".join(struct.pack("<I", o) for o in offs)
    for s in sections:
        out += s + bytes(-len(s) % 4)
    open(OUT_BIN, "wb").write(out)
    h = ["/* Made by tools/pack.py: what is in data.bin. */", "#ifndef FNAFW_DATA_H", "#define FNAFW_DATA_H", ""]
    h += [f"#define SEC_{n} {i}" for i, n in enumerate(SECTIONS)]
    h += [f"#define PIC_COUNT {len(BANK.pics)}"]
    h += [f"#define FONT_{n} {i}" for i, n in enumerate(["SMALL", "TEXT", "BIG", "NUM", "HUGE"])]
    h += [f"#define {n} {v}" for n, v in DEFINES]
    h += ["", "#endif", ""]
    open(OUT_H, "w").write("\n".join(h))
    write_anims()
    for name, s in zip(SECTIONS, sections):
        print(f"  {name}: {len(s)} bytes")
    print(f"data.bin: {len(out)} bytes")


if __name__ == "__main__":
    main()
