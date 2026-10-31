#!/usr/bin/env python3
"""Draws the faces of Mahjong's 42 kinds of tiles and writes src/art.h.

  art.py FONT.bdf [--preview out.png]

Each face is 18 x 24 pixels of 16 colours (0: the tile's own ivory shows through), packed four bits a pixel,
then with np_lz.h's LZSS. The dots and bamboo are drawn here; the characters (the numerals, 萬, the winds and the
dragons) are Fusion Pixel 12px's traditional Chinese letters (FONT: fusion-pixel-12px-monospaced-zh_hant.bdf, SIL
Open Font License 1.1, LICENSES/OFL-1.1-Fusion-Pixel.txt); the bird, flowers and seasons are drawn below.
Kinds: 0-8 dots, 9-17 bamboo, 18-26 characters, 27-30 the winds (east, south, west, north), 31-33 the dragons
(red, green, white), 34-37 the flowers, 38-41 the seasons."""
import math
import os
import re
import sys

W, H = 18, 24
PAL = [0x000000, 0x1A1A1A, 0x1F3C88, 0xC62828, 0x2E7D32, 0x7CC576, 0x1E66C8, 0x14532D,
       0xF2A900, 0xE5508A, 0x9147C8, 0xEC6A12, 0x3FA9E6, 0xFFFFFF, 0x7A4A2A, 0xF7C6D6]
INK, NAVY, RED, GREEN, LIGHT, BLUE, DARK, GOLD, PINK, PURPLE, ORANGE, SKY, WHITE, BROWN, BLUSH = range(1, 16)


def blank():
    return [[0] * W for _ in range(H)]


def disc(f, cx, cy, r, c):
    for y in range(H):
        for x in range(W):
            if (x + .5 - cx) ** 2 + (y + .5 - cy) ** 2 <= r * r:
                f[y][x] = c


def dot(f, cx, cy, r, c):
    """a dot as on the tiles: a ring of its colour, white inside, its centre coloured"""
    disc(f, cx, cy, r, c)
    disc(f, cx, cy, r - 1.15, WHITE)
    disc(f, cx, cy, max(r - 2.1, .75), c)


def dots(n):
    f = blank()
    B, G, R = BLUE, GREEN, RED
    if n == 1:
        disc(f, 9, 12, 8.4, G)
        disc(f, 9, 12, 7.2, WHITE)
        disc(f, 9, 12, 6.2, BLUE)
        disc(f, 9, 12, 5.0, WHITE)
        for k in range(8):
            a = k * math.pi / 4
            disc(f, 9 + 3.3 * math.cos(a), 12 + 3.3 * math.sin(a), 1.1, RED)
        disc(f, 9, 12, 1.6, RED)
        return f
    lay = {
        2: [(9, 6.5, 4.6, G), (9, 17.5, 4.6, B)],
        3: [(4, 4.5, 3.4, B), (9, 12, 3.4, R), (14, 19.5, 3.4, G)],
        4: [(4.5, 6, 3.6, B), (13.5, 6, 3.6, G), (4.5, 18, 3.6, G), (13.5, 18, 3.6, B)],
        5: [(4, 4.5, 3.3, B), (14, 4.5, 3.3, G), (9, 12, 3.3, R), (4, 19.5, 3.3, G), (14, 19.5, 3.3, B)],
        6: [(4.5, 4, 3.2, G), (13.5, 4, 3.2, G)] + [(x, y, 3.2, R) for y in (13, 20) for x in (4.5, 13.5)],
        7: [(3, 2.75, 2.75, G), (9, 5.75, 2.75, G), (15, 8.75, 2.75, G)] + [(x, y, 3, R) for y in (15, 21) for x in (5, 13)],
        8: [(x, y, 2.75, B) for y in (3, 9, 15, 21) for x in (5, 13)],
        9: [(x, y, 2.75, c) for y, c in ((4, B), (12, R), (20, G)) for x in (3, 9, 15)],
    }[n]
    for cx, cy, r, c in lay:
        dot(f, cx, cy, r, c)
    return f


STICK = [".G.",
         "GLG",
         "GLG",
         "ddd",
         "GLG",
         "GLG",
         ".G."]


def stick(f, x, y, c=GREEN, tall=7):
    rows = STICK if tall == 7 else STICK[:3] + STICK[3:4] + STICK[5:]
    for j, row in enumerate(rows):
        for i, ch in enumerate(row):
            if ch == '.':
                continue
            v = {'G': c, 'L': LIGHT if c == GREEN else BLUSH, 'd': DARK if c == GREEN else c}[ch]
            if 0 <= x + i < W and 0 <= y + j < H:
                f[y + j][x + i] = v


BIRD = """
......RR..........
.....RRRR.........
....RRKRRGG.......
....RRRRR.........
.....RRR..........
.....GGGG.........
....GGGGGG........
...GGLLGGGG.......
...GLLLLGGGG....B.
..GGLLLLLGGGG..BB.
..GGGLLLLLGGGBBBB.
...GGGGLLLGGBBBB..
....GGGGGGGBBBB...
.....GGGGGGBBB....
.......GGGG.......
.......O..O.......
......O..O........
....NNNNNNNNNN....
"""


def picture(f, art, x0, y0, colours):
    for j, row in enumerate(art.strip("\n").split("\n")):
        for i, ch in enumerate(row):
            if ch != '.':
                f[y0 + j][x0 + i] = colours[ch]


def bamboo(n):
    f = blank()
    if n == 1:
        picture(f, BIRD, 0, 3, {'R': RED, 'K': INK, 'G': GREEN, 'L': LIGHT, 'B': BLUE, 'O': ORANGE, 'N': BROWN})
        return f
    xs3, xs2 = (2, 7, 12), (4, 10)
    spots = {
        2: [(7, 2), (7, 14)],
        3: [(7, 2), (4, 14), (10, 14)],
        4: [(x, y) for y in (2, 14) for x in xs2],
        5: [(x, y) for y in (2, 14) for x in (2, 12)] + [(7, 8, RED)],
        6: [(x, y) for y in (2, 14) for x in xs3],
        7: [(7, 0, RED)] + [(x, y) for y in (8, 16) for x in xs3],
        8: [(x, y) for y in (2, 14) for x in (0, 5, 10, 15)],
        9: [(x, y, RED if x == 7 else GREEN) for y in (0, 8, 16) for x in xs3],
    }[n]
    for s in spots:
        stick(f, s[0], s[1], s[2] if len(s) > 2 else GREEN)
    return f


def load_bdf(path, chars):
    src = open(path, encoding="utf-8", errors="replace").read()
    out = {}
    for m in re.finditer(r"STARTCHAR.*?ENDCHAR", src, re.S):
        b = m.group(0)
        enc = int(re.search(r"ENCODING (-?\d+)", b).group(1))
        if enc <= 0 or chr(enc) not in chars:
            continue
        w, h, xo, yo = map(int, re.search(r"BBX (-?\d+) (-?\d+) (-?\d+) (-?\d+)", b).group(1, 2, 3, 4))
        rows = b.split("BITMAP\n")[1].split("\nENDCHAR")[0].split()
        bits = []
        for r in rows:
            v, n = int(r, 16), len(r) * 4
            bits.append([v >> (n - 1 - i) & 1 for i in range(w)])
        out[chr(enc)] = (xo, 10 - (yo + h), bits)  # (left, top in a 12 pixel line: 10 above the baseline)
    missing = set(chars) - set(out)
    if missing:
        raise SystemExit(f"{path}: no {''.join(sorted(missing))}")
    return out


def letter(f, glyphs, ch, x, y, c):
    xo, top, bits = glyphs[ch]
    for j, row in enumerate(bits):
        for i, b in enumerate(row):
            if b:
                f[y + top + j][x + xo + i] = c


DIGITS = {1: ".X.XX..X..X.XXX", 2: "XX...X.X.X..XXX", 3: "XX...X.X...XXX.", 4: "X.XX.XXXX..X..X"}


def corner(f, n, c):
    for k, ch in enumerate(DIGITS[n]):
        if ch == 'X':
            f[1 + k // 3][1 + k % 3] = c


def flower(n):
    f = blank()
    petal = (PINK, PURPLE, GOLD, ORANGE)[n - 1]
    cx, cy = 10, 8.5
    for k in range(5):
        a = -math.pi / 2 + k * 2 * math.pi / 5
        disc(f, cx + 3.3 * math.cos(a), cy + 3.3 * math.sin(a), 2.55, petal)
    disc(f, cx, cy, 1.6, GOLD if n != 3 else RED)
    for y in range(13, 23):
        f[y][10 - (y > 19)] = GREEN
    picture(f, "LL...\nLLLL.\n.LLLL\n..LL.", 4, 15, {'L': LIGHT})
    picture(f, "..LL\n.LLL\nLLL.", 11, 17, {'L': LIGHT})
    corner(f, n, RED)
    return f


SEASONS = [
    # spring: a sprout
    ("..GG.....GG..\n.GGGG...GGGG.\nGGGGGG.GGGGGG\n.GGGGGDGGGGG.\n...GGGDGGG...\n......D......\n......D......\n"
     "......D......\n....NNNNN....\n..NNNNNNNNN..", {'G': LIGHT, 'D': GREEN, 'N': BROWN}),
    # summer: the sun
    ("......R......\n.R....R....R.\n..R.......R..\n....OOOOO....\n...OOOOOOO...\n...OOOOOOO...\nRR.OOOOOOO.RR\n"
     "...OOOOOOO...\n...OOOOOOO...\n....OOOOO....\n..R.......R..\n.R....R....R.\n......R......",
     {'R': RED, 'O': ORANGE}),
    # autumn: a maple leaf
    ("......O......\n.....OOO.....\n..O..OOO..O..\n..OO.OOO.OO..\nOOOOOOOOOOOOO\n.OOOOOOOOOOO.\n..OOOOOOOOO..\n"
     ".OOOOOOOOOOO.\nOOOO.OOO.OOOO\n.....ROR.....\n......R......\n......R......", {'O': ORANGE, 'R': RED}),
    # winter: a snowflake
    ("......S......\n....S.S.S....\n.S...SSS...S.\n..S...S...S..\n...S..S..S...\nS...S.S.S...S\n.SSSSSSSSSSS.\n"
     "S...S.S.S...S\n...S..S..S...\n..S...S...S..\n.S...SSS...S.\n....S.S.S....\n......S......", {'S': SKY}),
]


def season(n):
    f = blank()
    art, colours = SEASONS[n - 1]
    rows = art.split("\n")
    picture(f, art, (W - len(rows[0]) + 1) // 2 + 1, 6 + (13 - len(rows)) // 2 + 2, colours)
    corner(f, n, BLUE)
    return f


def faces(glyphs):
    out = [dots(n) for n in range(1, 10)] + [bamboo(n) for n in range(1, 10)]
    for n in range(9):
        f = blank()
        letter(f, glyphs, "一二三四五六七八九"[n], 3, 0, NAVY)
        letter(f, glyphs, "萬", 3, 11, RED)
        out.append(f)
    for ch, c in zip("東南西北中發", (NAVY,) * 4 + (RED, GREEN)):
        f = blank()
        letter(f, glyphs, ch, 3, 5, c)
        out.append(f)
    f = blank()  # the white dragon: a blue frame
    for y in range(3, 21):
        for x in range(3, 15):
            if x < 5 or x > 12 or y < 5 or y > 18:
                f[y][x] = BLUE
    for y in range(6, 18):
        for x in range(6, 12):
            if x == 6 or x == 11 or y == 6 or y == 17:
                f[y][x] = SKY
    out.append(f)
    out += [flower(n) for n in range(1, 5)] + [season(n) for n in range(1, 5)]
    assert len(out) == 42
    return out


def lz_pack(d):
    """np_lz.h's stream, greedy, with the longest match up to 4096 back"""
    out, i, n = bytearray(), 0, len(d)
    while i < n:
        flag_at, flags = len(out), 0
        out.append(0)
        for b in range(8):
            if i >= n:
                break
            best, dist = 0, 0
            for j in range(max(0, i - 4096), i):
                k = 0
                while k < 18 and i + k < n and d[j + k] == d[i + k]:
                    k += 1
                if k > best:
                    best, dist = k, i - j
            if best >= 3:
                out += bytes([(dist - 1) & 255, ((dist - 1) >> 8) | (best - 3) << 4])
                i += best
            else:
                flags |= 1 << b
                out.append(d[i])
                i += 1
        out[flag_at] = flags
    return bytes(out)


def icon(glyphs, path):
    """the app's icon: a red dragon tile, its face and green back, on the felt"""
    from PIL import Image, ImageDraw
    im = Image.new("RGBA", (55, 56), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, 54, 55), 11, fill=(0x14, 0x51, 0x2F, 255))
    x, y, w, h = 10, 5, 30, 40
    for k, c in ((5, 0x1D603B), (4, 0x2E8A57), (3, 0x2E8A57), (2, 0xB9AA86), (1, 0xCDBF9C)):
        d.rounded_rectangle((x + k, y + k, x + k + w - 1, y + k + h - 1), 4, fill=(c >> 16, c >> 8 & 255, c & 255))
    d.rounded_rectangle((x, y, x + w - 1, y + h - 1), 4, fill=(0xF8, 0xF3, 0xE6), outline=(0x8E, 0x83, 0x68))
    xo, top, bits = glyphs["中"]
    for j, row in enumerate(bits):
        for i, b in enumerate(row):
            if b:
                d.rectangle((x + 4 + 2 * (xo + i), y + 7 + 2 * (top - 1 + j), x + 5 + 2 * (xo + i), y + 8 + 2 * (top - 1 + j)),
                            fill=(0xC6, 0x28, 0x28))
    im.save(path)


def main():
    bdf = sys.argv[1]
    glyphs = load_bdf(bdf, "一二三四五六七八九萬東南西北中發")
    fs = faces(glyphs)
    raw = bytearray()
    for f in fs:
        for y in range(H):
            for x in range(0, W, 2):
                raw.append(f[y][x] | f[y][x + 1] << 4)
    packed = lz_pack(raw)
    here = os.path.dirname(os.path.abspath(__file__))
    L = ["/* Generated by tools/art.py: the faces of the 42 kinds of tiles, 18 x 24 pixels of 16 colours (0: none),",
         " * four bits a pixel (the left one low), packed with np_lz.h. The characters are Fusion Pixel 12px's. */",
         f"#define ART_W {W}", f"#define ART_H {H}", "#define ART_KINDS 42", f"#define ART_RAW {len(raw)}",
         "static const uint32_t art_pal[16] = {" + ", ".join(f"0x{c:06X}" for c in PAL) + "};",
         f"static const uint8_t art_lz[{len(packed)}] = {{"]
    for k in range(0, len(packed), 24):
        L.append("  " + ", ".join(str(b) for b in packed[k:k + 24]) + ",")
    L.append("};")
    open(os.path.join(here, "..", "src", "art.h"), "w").write("\n".join(L) + "\n")
    print(f"src/art.h: {len(raw)} bytes packed to {len(packed)}")
    icon(glyphs, os.path.join(here, "..", "src", "icon.png"))
    if "--preview" in sys.argv:
        from PIL import Image
        s = 4
        img = Image.new("RGB", (9 * (W + 4) * s, 5 * (H + 4) * s), (40, 90, 60))
        for k, f in enumerate(fs):
            ox, oy = k % 9 * (W + 4) * s, k // 9 * (H + 4) * s
            for y in range(H):
                for x in range(W):
                    c = PAL[f[y][x]] if f[y][x] else 0xF4EEDC
                    for dy in range(s):
                        for dx in range(s):
                            img.putpixel((ox + x * s + dx, oy + y * s + dy), (c >> 16, c >> 8 & 255, c & 255))
        img.save(sys.argv[sys.argv.index("--preview") + 1])


if __name__ == "__main__":
    main()
