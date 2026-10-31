#!/usr/bin/env python3
"""Bad Time Simulator's pictures, drawn here pixel by pixel (the game's own: the Judge, the gavels, the heart):
writes src/art.c and src/art.h, and with --preview DIR a picture of each at 4x (Pillow).

Each picture is rows of letters, a letter a pixel ('.' is see-through), in one of two palettes."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, "..", "src")

JUDGE = {  # the Judge: robe, gold, hat, the shadow under it, white gloves and bands
    "k": 0x1C1418, "R": 0x7D2433, "r": 0x521826, "L": 0xA83D4F, "g": 0xE0B84C, "G": 0xA27D2A, "H": 0x2D2D38,
    "h": 0x55556A, "f": 0x0B0B10, "w": 0xF4F2EE, "W": 0xBDB8AE, "B": 0x4A3324, "b": 0x3EC8FF, "c": 0xA8ECFF,
    "s": 0x9FD8FF,
}
GAVEL = {  # the gavels: wood, gold bands, the striking face that lights up
    "k": 0x1A120C, "D": 0x5C3A20, "M": 0x8A5A34, "l": 0xB07A48, "g": 0xE0B84C, "G": 0xA27D2A, "f": 0x2A1E16,
    "w": 0xFFFFFF, "c": 0xBFF2FF, "b": 0x3EC8FF, "y": 0xFFF3B0,
}

HEAD = """
..........kkkkkkkkkkkkkk..........
.........kHhhhhhhhhhhhhHk.........
.........kHhHHHHHHHHHHHHk.........
.........kHHHHHHHHHHHHHHk.........
.........kHHHHHHHHHHHHHHk.........
.........kggggggggggggggk.........
.........kGGGGGGGGGGGGGGk.........
...kkkkkkkHHHHHHHHHHHHHHkkkkkkk...
.kkhhhhhhhhhhhhhhhhhhhhhhhhhhhhkk.
kHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHk
.kkkkkkkkkkkkkkkkkkkkkkkkkkkkkkkk.
........kffffffffffffffffk........
........kffffffffffffffffk........
........kffffffffffffffffk........
........kffffffffffffffffk........
.........kffffffffffffffk.........
..........kffffffffffffk..........
...........kkffffffffkk...........
"""

TORSO = """
.......kkkkkkkkkkkkkk.......
....kkkLLLLLwwwwLLLLLkkk....
..kkLLLRRRRRwwwwRRRRRLLLkk..
.kLLRRRRRRRRkwwkRRRRRRRRLLk.
kLRRRRRRRRRRRwwRRRRRRRRRRRLk
kRRRRRRRRRRRRggRRRRRRRRRRRRk
kRRRRRRRRRgGRggRGgRRRRRRRRRk
kRRRRRRRRRRgGggGgRRRRRRRRRRk
kRRRRRRRRRRRggggRRRRRRRRRRRk
kRRRRRRRRRRRgGGgRRRRRRRRRRRk
kRRRRRRRRRRRRggRRRRRRRRRRRRk
kRRRRRRRRRRRRggRRRRRRRRRRRRk
krRRRRRRRRRRRggRRRRRRRRRRRrk
krRRRRRRRRRRRggRRRRRRRRRRRrk
krrRRRRRRRRRRggRRRRRRRRRRrrk
krrrRRRRRRRRRggRRRRRRRRRrrrk
"""

LEGS = """
..krrRRRRRRRRRRggRRRRRRRRRRrrk..
..krrRRRRRRRRRRggRRRRRRRRRRrrk..
.krrrRRRRRRRRRRggRRRRRRRRRRrrrk.
.krrrRRRRRRRRRRggRRRRRRRRRRrrrk.
.krrrrRRRRRRRRRggRRRRRRRRRrrrrk.
krrrrrRRRRRRRRRggRRRRRRRRRrrrrrk
krrrrrrRRRRRRRRggRRRRRRRRrrrrrrk
kGggggggggggggggggggggggggggggGk
.kkkkkkkkkkkkkkkkkkkkkkkkkkkkkk.
.......kBBBBBk....kBBBBBk.......
......kBBBBBBk....kBBBBBBk......
......kkkkkkkk....kkkkkkkk......
"""

# arms, the left ones (the right ones are mirrored); their shoulder joint is the top-middle
ARM_SIDE = """
..kkk.
.kLRRk
kLRRRk
kRRRrk
kRRRrk
kRRRrk
kRRrrk
kRRrrk
kRRrrk
krrrrk
kGgggk
kkwwkk
kwwwWk
kwwWWk
.kWWk.
..kk..
"""
ARM_UP = """
..kk..
.kwwk.
kwwwWk
kwwWWk
kkwWkk
kGgggk
krRRrk
kRRRrk
kRRRrk
kRRRrk
kRRRLk
kRRRLk
kRRLLk
kRLLLk
.kLLk.
..kk..
"""
ARM_OUT = """
..kkkkkkkkkkkk.....
.kLLLLLLLLLLLLkkkk.
kLRRRRRRRRRRRkGwwwk
kRRRRRRRRRRRRkgwwWk
krrrrrrrrrrrrkgwWWk
.kkkkkkkkkkkkkkkkk.
"""
ARM_DOWN = """
..kkk...
.kLRRk..
kLRRRk..
kRRRrk..
kRRRrk..
.kRRrrk.
.kRRrrk.
.kRRrrk.
..kRrrrk
..kRrrrk
..kGgggk
..kkwwkk
..kwwwWk
..kwwWWk
...kWWk.
....kk..
"""
ARM_SHRUG = """
.....kk.
....kwwk
.kkkkwWk
kLRRkwWk
kRRRkGgk
kRRRrkkk
kRRrk...
.krk....
"""

HEART = """
.rr..rr.
rrrrrrrr
rrrrrrrr
rrrrrrrr
.rrrrrr.
..rrrr..
...rr...
"""
HEART_BROKEN = """
.rr..rr.
rrr.rrrr
rrrr.rrr
rrr.rrrr
.rrr.rr.
..r.rr..
...rr...
"""

GAVEL_SHUT = """
............................
...kkk..kkkkkkkkkkk..kkkkk..
..kgGGkkklllllllllkkkfffffk.
..kgGGkMMlllllllllMkkfffffk.
.kggGGkMMMMMMMMMMMMkkffffffk
.kggGGkMMMMMMMMMMMMkkffffffk
.kggGGkMMMMMMMMMMMMkkffffffk
.kggGGkMMMMMMMMMMMMkkffffffk
.kggGGkMMMMMMMMMMMMkkffffffk
.kggGGkMMMMMMMMMMMMkkffffffk
..kgGGkDDDDDDDDDDDDkkfffffk.
..kgGGkDDDDDDDDDDDDkkfffffk.
...kkk.kkkkkMMkkkkk..kkkkk..
...........kMMk.............
...........kMMk.............
...........kMDk.............
...........kMDk.............
...........kMDk.............
...........kMDk.............
...........kGgk.............
...........kGgk.............
............kk..............
"""
GAVEL_OPEN = """
............................
...kkk..kkkkkkkkkkk..kkkkk..
..kgGGkkklllllllllkkkcccwck.
..kgGGkMMlllllllllMkkcwwwwck
.kggGGkMMMMMMMMMMMMkkcwwwwwc
.kggGGkMMMMMMMMMMMMkkcwwwwwc
.kggGGkMMMMMMMMMMMMkkcwwwwwc
.kggGGkMMMMMMMMMMMMkkcwwwwwc
.kggGGkMMMMMMMMMMMMkkcwwwwwc
.kggGGkMMMMMMMMMMMMkkcwwwwwc
..kgGGkDDDDDDDDDDDDkkcwwwwck
..kgGGkDDDDDDDDDDDDkkcccwck.
...kkk.kkkkkMMkkkkk..kkkkk..
...........kMMk.............
...........kMMk.............
...........kMDk.............
...........kMDk.............
...........kMDk.............
...........kMDk.............
...........kGgk.............
...........kGgk.............
............kk..............
"""

PICTURES = [  # name, rows, palette, anchor (x, y) from the top-left
    ("head", HEAD, "judge", (17, 18)),       # bottom-middle: the chin
    ("torso", TORSO, "judge", (14, 16)),     # bottom-middle
    ("legs", LEGS, "judge", (16, 12)),       # bottom-middle: the feet
    ("arm_side", ARM_SIDE, "judge", (3, 0)),
    ("arm_up", ARM_UP, "judge", (3, 15)),
    ("arm_out", ARM_OUT, "judge", (0, 1)),
    ("arm_down", ARM_DOWN, "judge", (3, 0)),
    ("arm_shrug", ARM_SHRUG, "judge", (1, 4)),
    ("heart", HEART, "heart", (4, 4)),
    ("heart_broken", HEART_BROKEN, "heart", (4, 4)),
    ("gavel_shut", GAVEL_SHUT, "gavel", (14, 7)),   # the head's middle: where it turns, and the beam's axis
    ("gavel_open", GAVEL_OPEN, "gavel", (14, 7)),
]
PALETTES = {"judge": JUDGE, "gavel": GAVEL, "heart": {"r": 0xFFFFFF}}   # (the heart is tinted when drawn)


def rows(text):
    r = [l for l in text.strip("\n").split("\n")]
    if len({len(l) for l in r}) != 1:
        sys.exit("art.py: uneven rows:\n" + "\n".join(f"{len(l):3} {l}" for l in r))
    return r


def main():
    preview = sys.argv[sys.argv.index("--preview") + 1] if "--preview" in sys.argv else None
    pals = {}
    for pn, pal in PALETTES.items():
        keys = list(pal)
        pals[pn] = (keys, [pal[k] for k in keys])
    out_c = ["/* Generated by tools/art.py: don't edit. The game's own pictures. */", '#include "art.h"', ""]
    out_h = ["/* Generated by tools/art.py: don't edit. */", "#ifndef BT_ART_H", "#define BT_ART_H",
             "#include <stdint.h>", "",
             "typedef struct {", "  uint8_t w, h, ax, ay;      /* size, anchor */",
             "  const uint32_t *pal;        /* 0xRRGGBB; index 0 is see-through */",
             "  const uint8_t *px;          /* 4 bits a pixel, two a byte, the first in the low half */",
             "} art_t;", ""]
    for pn, (keys, cols) in pals.items():
        out_c.append(f"static const uint32_t PAL_{pn.upper()}[16] = {{0, " + ", ".join(f"0x{c:06X}" for c in cols) + "};")
    out_c.append("")
    imgs = {}
    for name, text, pn, (ax, ay) in PICTURES:
        r = rows(text)
        keys, cols = pals[pn]
        w, h = len(r[0]), len(r)
        idx = []
        for line in r:
            for ch in line:
                idx.append(0 if ch == "." else keys.index(ch) + 1)
        if len(idx) % 2:
            idx.append(0)
        data = [idx[i] | idx[i + 1] << 4 for i in range(0, len(idx), 2)]
        out_c.append(f"static const uint8_t PX_{name.upper()}[{len(data)}] = {{")
        for i in range(0, len(data), 24):
            out_c.append("  " + ", ".join(str(b) for b in data[i:i + 24]) + ",")
        out_c.append("};")
        out_c.append(f"const art_t ART_{name.upper()} = {{{w}, {h}, {ax}, {ay}, PAL_{pn.upper()}, PX_{name.upper()}}};")
        out_h.append(f"extern const art_t ART_{name.upper()};")
        imgs[name] = (r, pn)
    out_h += ["#endif", ""]
    with open(os.path.join(SRC, "art.c"), "w") as f:
        f.write("\n".join(out_c) + "\n")
    with open(os.path.join(SRC, "art.h"), "w") as f:
        f.write("\n".join(out_h))
    if preview:
        from PIL import Image
        os.makedirs(preview, exist_ok=True)
        for name, (r, pn) in imgs.items():
            pal = PALETTES[pn]
            im = Image.new("RGB", (len(r[0]) * 6, len(r) * 6), (40, 40, 40))
            for y, line in enumerate(r):
                for x, ch in enumerate(line):
                    if ch != ".":
                        c = pal[ch]
                        for dy in range(6):
                            for dx in range(6):
                                im.putpixel((x * 6 + dx, y * 6 + dy), (c >> 16, c >> 8 & 255, c & 255))
            im.save(os.path.join(preview, name + ".png"))


if __name__ == "__main__":
    main()
