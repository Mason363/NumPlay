"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py data.bin data.h out.bin

The game reads data.bin's numbers in place (src/res.h, gfx.c, news.c): the pictures' records (their offsets, u32),
palettes (RGB565, u16) and the packed pictures' chances (u16), the icons' table (u16), the fonts' offsets (u32) and
glyphs' (u16), the game's tables (res.h's structures: doubles, floats, u32, u16), the lists of ids (u16) and the news
(news.c's NewsRec). pack.py writes them little-endian; here they are turned around. What the game reads a byte at a
time (the pixels, the packed data, the texts) stays as it is."""
import re
import struct
import sys

CONTEXTS = 423   # (gfx.c's)
LISTS = ("TIER_UNSHACKLE", "COOKIE_UPGRADES", "GRANDMA_SYNERGIES", "KITTENS", "GOLDEN_UPGRADES", "LASTING", "NO_PERM",
         "HEAVENLY", "EASTER_EGGS", "HALLOWEEN_DROPS", "HEART_DROPS", "SANTA_DROPS", "REINDEER_DROPS", "FORTUNES",
         "SHOW_IF", "CONFIRM")


def convert(b, h):
    b = bytearray(b)
    assert b[:4] == b"CCK2" and len(b) == h["DATA_SIZE"], "not this data.h's data.bin"
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(fmt, pos, n=1):
        s = struct.Struct("<" + fmt)
        end = pos + n * s.size
        assert end <= len(b) and not any(done[pos:end]), (fmt, pos)
        done[pos:end] = b"\1" * (end - pos)
        for i in range(n):
            struct.pack_into(">" + fmt, b, pos + i * s.size, *s.unpack_from(b, pos + i * s.size))

    # the pictures: records, palettes (each once: pictures share them), chances
    pals = {}
    for i in range(h["N_IMAGES"]):
        at = h["OFF_IMAGES"] + 16 * i
        cw, n1, pal = b[at + 4], b[at + 7], struct.unpack_from("<I", b, at + 12)[0]
        if cw:
            assert pals.setdefault(pal, n1) == n1, pal
        turn("BBBBBBBBII", at)
    for pal, n1 in pals.items():
        turn("H", pal, n1 + 1)
    turn("H", h["OFF_PRIORS"], CONTEXTS)
    turn("H", h["OFF_ICONS"], h["ICON_COLS"] * h["ICON_ROWS"])
    # the fonts: their offsets, each glyph's
    nfonts = len([k for k in h if k.startswith("F_")])
    for f in range(nfonts):
        o = struct.unpack_from("<I", b, h["OFF_FONTS"] + 4 * f)[0]
        turn("BBbbBBH", o + 4, b[o + 1])
    turn("I", h["OFF_FONTS"], nfonts)
    # the game's tables (res.h)
    turn("ddIIIIIIIIhhhH", h["OFF_BUILDINGS"], h["N_BUILDINGS"])
    turn("dIhhhhHH", h["OFF_TIERS"], h["N_TIERS"])
    turn("dfIIIHBBbbBBfH6HhhHd", h["OFF_UPGRADES"], h["N_UPGRADES"])
    turn("IIIfHBb", h["OFF_ACHIEVS"], h["N_ACHIEVS"])
    turn("IHH", h["OFF_MILKS"], h["N_MILKS"])
    n = h["N_TIERS"]
    turn(f"{n}H{n}H6HHHHH4HHH3d", h["OFF_BUILDING_LINKS"], h["N_BUILDINGS"])
    for name in LISTS:
        turn("H", h["OFF_" + name], h["N_" + name])
    turn("dHHI", h["OFF_BANK_ACHIEVS"], h["N_BANK_ACHIEVS"])
    turn("dHHI", h["OFF_CPS_ACHIEVS"], h["N_CPS_ACHIEVS"])
    turn("dHHHHB7x", h["OFF_UNLOCK_AT"], h["N_UNLOCK_AT"])
    turn("IBBH", h["OFF_NEWS"], h["N_NEWS"])
    return bytes(b)


def numbers(path):
    h = {}
    for line in open(path):
        m = re.match(r"#define (\w+) (-?\d+)$", line.strip())
        if m:
            h[m.group(1)] = int(m.group(2))
    return h


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read(), numbers(sys.argv[2]))
    with open(sys.argv[3], "wb") as f:
        f.write(out)
