#!/usr/bin/env python3
"""src/data.c and src/data.h from Famidash's own files.

Usage: python3 -I tools/data.py GAMESRC [OUT_DIR]
GAMESRC is a checkout of github.com/tfdsoft/famidash (MIT): its CHR banks, metatiles, the level set A
exports (LZ + RLE columns, sprite lists, headers), the ground, menu nametables and palettes. Nothing in
it is run; files are only read. See ~/Documents/Assets/gamesrc/famidash/spec for the numbers.
"""
import json
import os
import re
import sys

SRC = sys.argv[1]
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "src")
LVL = os.path.join(SRC, "LEVELS", "include", "lvlset_A")


def rd(*p):
    with open(os.path.join(SRC, *p), "rb") as f:
        return f.read()


def txt(*p):
    with open(os.path.join(SRC, *p), encoding="utf-8", errors="replace") as f:
        return f.read()


# ------------------------------------------------------------------ CHR, laid out like crt0.s's GAMECHR
G = "GRAPHICS"
CHR_ORDER = [
    ("Level Tiles/SpikesA", 2), ("Level Tiles/SpikesB", 2), ("Level Tiles/SpikesC", 2),
    ("Level Tiles/BlocksA", 2), ("Level Tiles/BlocksB", 2), ("Level Tiles/BlocksC", 2), ("Level Tiles/BlocksD", 2),
    ("Level Tiles/SawbladesA", 2), ("Level Tiles/slopesA", 2),
    ("Gamemode/so_retro_v2", 1), ("Level Sprites/bankportals", 1),
    ("Gamemode/bankgamemodesA", 1), ("Level Sprites/bankportals", 1),
    ("Gamemode/bankgamemodesAretro", 1), ("Level Sprites/bankportals", 1),
    ("Gamemode/bankgamemodesB", 1), ("Level Sprites/bankportals", 1),
    ("Gamemode/bankgamemodesBretro", 1), ("Level Sprites/bankportals", 1),
    ("Level Sprites/bankmain", 1), ("Level Sprites/bankblank", 1),
    ("Level Sprites/bankmain", 1), ("Level Sprites/bankblank2", 1),
    ("Level Sprites/bankmain", 1), ("Level Sprites/bankblankcloud", 1),
    ("Level Sprites/bankmain", 1), ("Level Sprites/bankblankcloud2", 1),
    ("Level Sprites/bankmain", 1), ("Level Sprites/bankblankfingerdash", 1),
    ("Level Sprites/bankmain", 1), ("Level Sprites/bankblankfingerdash2", 1),
]
for i in range(9):
    CHR_ORDER += [("Icons/bankicon%02X" % i, 1), ("Level Sprites/bankportals", 1)]
for i in range(9):
    CHR_ORDER += [("@fan icon collection/CONTEST WINNERS/contest%d" % (i + 1), 1), ("Level Sprites/bankportals", 1)]
CHR_ORDER += [("@fan icon collection/starfox", 1), ("Level Sprites/bankportals", 1)]
CHR_ORDER += [("Level Tiles/SpikesD", 2), ("Level Tiles/BlocksE", 2), ("Level Tiles/BlocksF", 2),
              ("Level Tiles/slopesE", 2), ("Level Tiles/slopesF", 2), ("Level Tiles/slopesB", 2),
              ("Level Tiles/slopesD", 2),
              ("Gamemode/banktriangle", 1), ("Level Sprites/bankportals", 1),
              ("Gamemode/bankfootball", 1), ("Level Sprites/bankportals", 1),
              ("Menus/cursors", 2), ("Menus/menus", 4), ("Menus/menuicons", 4), ("Menus/levelcomplete", 4),
              ("Menus/practicecomplete", 1), ("Level Tiles/SawbladesNone", 1)]

rom = b""
for name, kb in CHR_ORDER:
    path = name[1:] if name.startswith("@") else os.path.join(G, name)
    d = rd(path + ".chr")
    assert len(d) == kb * 1024, (name, len(d))
    rom += d
NBANKS = len(rom) // 1024
assert NBANKS == 112, NBANKS
uniq, bank_map = [], []
for b in range(NBANKS):
    blk = rom[b * 1024:(b + 1) * 1024]
    if blk not in uniq:
        uniq.append(blk)
    bank_map.append(uniq.index(blk))

# the parallax: 144 banks (112..255), each the 48x72 picture of bank 0 moved 1 px further; the
# 10 tiles that are not part of the picture come in two versions, by the bank's parity
par = rd(G, "parallax.chr")
PCOLS = [[0x80, 0x90, 0xa0, 0xb0, 0x86, 0x96, 0xa6, 0xb6, 0x8c], [0x81, 0x91, 0xa1, 0xb1, 0x87, 0x97, 0xa7, 0xb7, 0x8d],
         [0x82, 0x92, 0xa2, 0xb2, 0x88, 0x98, 0xa8, 0xb8, 0x8e], [0x83, 0x93, 0xa3, 0xb3, 0x89, 0x99, 0xa9, 0xb9, 0x9c],
         [0x84, 0x94, 0xa4, 0xb4, 0x8a, 0x9a, 0xaa, 0xba, 0x9d], [0x85, 0x95, 0xa5, 0xb5, 0x8b, 0x9b, 0xab, 0xbb, 0x9e]]


def pix(data, base, t, x, y):
    p = data[base + t * 16:base + t * 16 + 16]
    return ((p[y] >> (7 - x)) & 1) | (((p[y + 8] >> (7 - x)) & 1) << 1)


par_img = bytearray(48 * 72)
for c in range(6):
    for r in range(9):
        for y in range(8):
            for x in range(8):
                par_img[(r * 8 + y) * 48 + c * 8 + x] = pix(par, 0, PCOLS[c][r] - 0x80, x, y)


def shifted(k):
    out = bytearray(48 * 72)
    for y in range(72):
        for x in range(48):
            sx, sy = x - k, y
            while sx >= 48:
                sx -= 48
                sy = (sy + 24) % 72
            while sx < 0:
                sx += 48
                sy = (sy - 24) % 72
            out[y * 48 + x] = par_img[sy * 48 + sx]
    return out


for k in (1, 7, 50, 143):  # the model holds (checked on every bank when it was written)
    img = bytearray(48 * 72)
    for c in range(6):
        for r in range(9):
            for y in range(8):
                for x in range(8):
                    img[(r * 8 + y) * 48 + c * 8 + x] = pix(par, k * 1024, PCOLS[c][r] - 0x80, x, y)
    assert img == shifted(k), k
par_static = [par[0:1024], par[1024:2048]]  # tile ids 0x80-0xBF; only the non-picture tiles are read
for blk in par_static:
    if blk not in uniq:
        uniq.append(blk)
par_banks = [uniq.index(b) for b in par_static]

# ------------------------------------------------------------------ metatiles
COL = {}
for m in re.finditer(r"^(COL_\w+)\s*=\s*\$?([0-9a-fA-F]+)", txt("METATILES", "metatiles_macro.inc"), re.M):
    COL[m.group(1)] = int(m.group(2), 16)
COL["COL_NONE"] = 0
mt = []
for m in re.finditer(r'^\s*Metatile\s+"([^"]+)",\s*\$(\w+),\s*\$(\w+),\s*\$(\w+),\s*\$(\w+)\s*,\s*PAL_(\d),\s*(\w+)',
                     txt("METATILES", "metatiles.inc"), re.M):
    mt.append((m.group(1), [int(m.group(i), 16) for i in range(2, 6)], int(m.group(6)), COL[m.group(7)]))
assert len(mt) <= 256, len(mt)
MTNAME = {name: i for i, (name, *_r) in enumerate(mt)}

# ------------------------------------------------------------------ levels
defs = {}
for m in re.finditer(r"^#define\s+(\w+)\s+(\d+)", txt("SAUCE", "defines", "space_defines.h"), re.M):
    defs["_" + m.group(1)] = int(m.group(2))
order = [m.group(1) for m in re.finditer(r"^#define level_(\w+) (\d+)", open(os.path.join(LVL, "level_defines.h")).read(), re.M)]
official = int(re.search(r"OFFICIAL_LEVEL_COUNT (\d+)", open(os.path.join(LVL, "level_defines.h")).read()).group(1))
lst = open(os.path.join(LVL, "levellist.h")).read()
DIFF = ["EASY", "NORMAL", "HARD", "HARDER", "INSANE", "DEMON", "AUTO"]
diffs = [DIFF.index(x) for x in re.findall(r"^\s*(EASY|NORMAL|HARD|HARDER|INSANE|DEMON|AUTO),", lst, re.M)]
stars = [int(x) for x in re.findall(r"^\s*(\d+),\s*//", lst.split("stars_list")[1], re.M)]
assert len(diffs) == len(order) == len(stars), (len(diffs), len(order), len(stars))


def json5(s):
    s = re.sub(r"/\*.*?\*/", "", s, flags=re.S)
    s = re.sub(r"//[^\n]*", "", s)
    s = re.sub(r"([{,\s])([A-Za-z_]\w*)\s*:", r'\1"\2":', s)
    s = re.sub(r",(\s*[\]}])", r"\1", s)
    s = re.sub(r"0x([0-9a-fA-F]+)", lambda m: str(int(m.group(1), 16)), s)
    s = re.sub(r":\s*\+(\d)", r": \1", s)
    return json.loads(s)


meta = json5(txt("LEVELS", "metadata", "lvlset_A_metadata.json5"))
meta_by = {m["level"]: m for m in meta["official_levels"] + meta["community_levels"]}

asm = open(os.path.join(LVL, "all_level_data.s")).read()
table = open(os.path.join(LVL, "all_level_table.s")).read()
lengths = {}
for m in re.finditer(r"\.byte \.lobyte\(\$([0-9A-Fa-f]+)\)\s*;\s*(\w+)", table):
    lengths[m.group(2)] = int(m.group(1), 16)


def ev(expr):
    e = expr.strip()
    e = re.sub(r"\$([0-9A-Fa-f]+)", lambda m: str(int(m.group(1), 16)), e)
    e = re.sub(r"\b_\w+\b", lambda m: str(defs[m.group(0)]), e)
    e = re.sub(r"\bsong_\w+\b", "0", e)  # (music: not used)
    return eval(e, {"__builtins__": {}})


def lz_unpack(d):
    out, buf, bi, i = bytearray(), bytearray(256), 0, 0
    while i < len(d):
        n = d[i]
        i += 1
        if n == 0xFF:
            break
        if n == 0x80:
            v = d[i]
            i += 1
            out.append(v)
            buf[bi] = v
            bi = (bi + 1) & 255
            continue
        o = d[i]
        i += 1
        seq = [buf[(o + k) & 255] for k in range(n)]
        lit = d[i]
        i += 1
        for v in seq + [lit]:
            out.append(v)
            buf[bi] = v
            bi = (bi + 1) & 255
    return out


levels = []
for name in order:
    m = re.search(r"level_data_%s:\n\t; Header\n((?:\t\t\.byte [^\n]*\n){14})" % name, asm)
    hdr = [ev(line.split(".byte", 1)[1].split(";")[0]) & 0xFF for line in m.group(1).strip().split("\n")[3:]]
    lz = open(os.path.join(LVL, "EXPORTS", "level", name + ".lz.bin"), "rb").read()
    rle = lz_unpack(lz)
    height = hdr[10]
    # the RLE must decode to whole columns
    n, i = 0, 0
    while i < len(rle):
        if rle[i] >= 0x80:
            n += 1
            i += 1
        else:
            n += rle[i] + 1
            i += 2
    # (the LZ packer pads its last 256 byte block with zeros: a few empty columns more)
    width = n // height
    spr = open(os.path.join(LVL, "EXPORTS", "sprite", name + ".bin"), "rb").read()
    assert spr[-1] == 0xFF and (len(spr) - 1) % 5 == 0, name
    md = meta_by[name]
    up = (md.get("upperText") or "").strip()
    lo = (md.get("lowerText") or "").strip()
    levels.append(dict(name=name, hdr=hdr, lz=lz, spr=spr, width=width, length=lengths[name], up=up, lo=lo))

# ground 0 (groundlist.h has one): RLE of 3 rows of 16 metatiles
gtxt = txt("LEVELS", "include", "lvlset_A", "grounddata.h").split("const unsigned char ground0[] = {")[1].split("}")[0]
gb = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", gtxt)]
ground, i = [], 0
while len(ground) < 48:
    if gb[i] & 0x80:
        ground.append(gb[i] & 0x7F)
        i += 1
    else:
        ground += [gb[i + 1]] * (gb[i] + 1)
        i += 2
ground = ground[:48]

# ------------------------------------------------------------------ menu nametables and palettes


def c_array(text, name, nth=0):
    hits = [m for m in re.finditer(r"const unsigned char %s\[\d*\]\s*=\s*\{(.*?)\};" % re.escape(name), text, re.S)]
    body = hits[nth].group(1)
    return bytes(int(x, 0) for x in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", body))


def unrle(d):
    tag, out, i, last = d[0], bytearray(), 1, 0
    while True:
        b = d[i]
        i += 1
        if b != tag:
            out.append(b)
            last = b
            continue
        n = d[i]
        i += 1
        if n == 0:
            break
        out += bytes([last]) * n
    return bytes(out)


NT = {}
x03 = txt("SAUCE", "defines", "nametable", "menunametable_XCD03.c")
NT["title"] = unrle(c_array(x03, "game_start_screen", 0))
NT["levelsel"] = unrle(c_array(x03, "level_select_screen", 1))
x06 = txt("SAUCE", "defines", "nametable", "menunametable_XCD06.c")
NT["leveldone"] = unrle(c_array(x06, "leveldone", 0))
NT["practicedone"] = unrle(c_array(x06, "practicedone", 0))
x02 = txt("SAUCE", "defines", "nametable", "menunametable_XCD02.c")
NT["settings"] = unrle(c_array(x02, "settingscreen", 0))
NT["credits"] = unrle(c_array(x02, "Credits", 0))
NT["credits2"] = unrle(c_array(x02, "Credits2", 0))
NT["credits3"] = unrle(c_array(x02, "Credits3", 0))
NT["funsettings"] = unrle(c_array(txt("SAUCE", "menustates", "funsettings.c"), "funsettingscreen"))
NT["customize"] = unrle(c_array(txt("SAUCE", "menustates", "customize.c"), "customizescreen"))
for k, v in NT.items():
    assert len(v) == 1024, (k, len(v))

PAL = {}
pp = txt("SAUCE", "defines", "palette", "palettes_PRG.c")
for nm in ("paletteDefault", "paletteDefaultSP", "paletteMenu", "splashMenu", "oldsplashMenu", "splashMenu2",
           "paletteLVLSelectSP"):
    m = re.search(r"const uint8_t %s\[16\]\s*=\s*\{(.*?)\};" % nm, pp, re.S)
    PAL[nm] = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", m.group(1))]
p2 = txt("SAUCE", "defines", "palette", "palettes_XCD02.c")
m = re.search(r"const uint8_t paletteSettings\[16\]\s*=\s*\{(.*?)\};", p2, re.S)
PAL["paletteSettings"] = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", m.group(1))]
m = re.search(r"#if !__VS_SYSTEM\s*const unsigned char palette_Credits\[16\]\s*=\s*\{(.*?)\};", p2, re.S)
PAL["paletteCredits"] = [int(x, 16) for x in re.findall(r"0x([0-9a-fA-F]+)", m.group(1))]
for k, v in PAL.items():
    assert len(v) == 16, k

# ------------------------------------------------------------------ the menus' character maps (cc65 charmaps)
CMAP = {}
for nm in ("mainmenu_charmap", "color1_charmap", "endlevel_charmap", "mainmenu_customize", "bgm_charmap"):
    m = list(range(128))
    for mm in re.finditer(r"^\s*#pragma charmap \((0x[0-9A-Fa-f]+),\s*(0x[0-9A-Fa-f]+)\)", txt("SAUCE", "defines", "charmap", nm + ".h"), re.M):
        a, b = int(mm.group(1), 16), int(mm.group(2), 16)
        if a < 128:
            m[a] = b
    CMAP[nm] = m

# ------------------------------------------------------------------ the metasprites: sprites.h as C
spr_src = txt("SAUCE", "defines", "sprites.h")
spr_src = "\n".join(l for l in spr_src.split("\n") if not l.lstrip().startswith("#pragma"))

# ------------------------------------------------------------------ physics: the 60 Hz half of the compiled tables
ph = txt("SAUCE", "defines", "physics_table_defines.cmp.h")
PHYS = {}
for m in re.finditer(r"const uint8_t (\w+)_lo\[\] = \{([^}]*)\};\s*const uint8_t \1_hi\[\] = \{([^}]*)\};", ph):
    lo = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m.group(2))]
    hi = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", m.group(3))]
    if len(lo) == 8:  # [framerate][mini][gravity]: keep NTSC (60 Hz)
        PHYS[m.group(1)] = [lo[i] | hi[i] << 8 for i in range(4, 8)]
for nm in ("POS_DUAL_CAP_CHECK", "NEG_DUAL_CAP_CHECK", "SHIP_SCROLL_SPEED"):
    lo = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", re.search(r"const uint8_t %s_lo\[\] = \{([^}]*)\}" % nm, ph).group(1))]
    hi = int(re.search(r"#define %s_hi 0x([0-9A-Fa-f]+)" % nm, ph).group(1), 16)
    PHYS[nm] = [lo[1] | hi << 8]
speed60 = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", re.search(r"CUBE_SPEED_60\[\] = \{([^}]*)\}", ph).group(1))]
speed60 = [((v & 0xFF) << 8) | (v >> 8) for v in speed60]  # (stored big-endian)
ADJ = []
for suf in ("mg", "mG", "Mg", "MG"):
    v = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", re.search(r"sprite_gamemode_adjust_heights_60_%s\[\] = \{([^}]*)\}" % suf, ph).group(1))]
    ADJ.append([((x & 0xFF) << 8) | (x >> 8) for x in v])
robot_time = [int(x, 16) for x in re.findall(r"0x([0-9A-Fa-f]+)", re.search(r"ROBOT_JUMP_TIME\[\] = \{([^}]*)\}", ph).group(1))][1]
assert speed60[0] == 0x02C4 and PHYS["JUMP_VEL"][0] == 0xFA70 and ADJ[0][0] == 0xFA70, (speed60, PHYS["JUMP_VEL"])

# ------------------------------------------------------------------ write
os.makedirs(OUT, exist_ok=True)


def arr(name, data, ctype="uint8_t", per=32):
    out = ["const %s %s[%d] = {" % (ctype, name, len(data))]
    for i in range(0, len(data), per):
        out.append(",".join(str(x) for x in data[i:i + per]) + ",")
    out.append("};")
    return "\n".join(out)


h = ["/* Generated by tools/data.py from Famidash (tfdsoft, MIT). Do not edit. */",
     "#ifndef FD_DATA_H", "#define FD_DATA_H", "#include <stdint.h>",
     "#define CHR_UNIQUE %d" % len(uniq), "#define PAR_BANK0 %d" % par_banks[0], "#define PAR_BANK1 %d" % par_banks[1],
     "#define NLEVELS %d" % len(levels), "#define NOFFICIAL %d" % official, "#define NMETATILES %d" % len(mt),
     "extern const uint8_t chr_data[CHR_UNIQUE][1024];", "extern const uint8_t chr_bank_map[112];",
     "extern const uint8_t parallax_img[48 * 72];",
     "extern const uint8_t mt_tl[256], mt_tr[256], mt_bl[256], mt_br[256], mt_pal[256], mt_coll[256];",
     "extern const uint8_t ground_mt[48];",
     "typedef struct { const uint8_t *lz; const uint8_t *spr; uint8_t hdr[11]; uint16_t width, length;",
     "  uint8_t difficulty, stars; const char *up, *lo; } level_t;",
     "extern const level_t level_list[NLEVELS];",
     "extern const uint16_t cube_speed[6], adjust_heights[4][72];", "#define ROBOT_JUMP_TIME %d" % robot_time]
for k in PHYS:
    h.append("extern const uint16_t %s[%d];" % (k, len(PHYS[k])))
for k in NT:
    h.append("extern const uint8_t nt_%s[1024];" % k)
for k in PAL:
    h.append("extern const uint8_t pal_%s[16];" % k)
for k in CMAP:
    h.append("extern const uint8_t cmap_%s[128];" % k)
for nm, i in sorted(MTNAME.items(), key=lambda x: x[1]):
    pass
h.append("#endif")
open(os.path.join(OUT, "data.h"), "w").write("\n".join(h) + "\n")

c = ["/* Generated by tools/data.py from Famidash (tfdsoft, MIT): its CHR banks, metatiles, level set A,",
     " * ground, menu screens and palettes. Do not edit. */", '#include "data.h"',
     "const uint8_t chr_data[CHR_UNIQUE][1024] = {"]
for blk in uniq:
    c.append("{" + ",".join(str(x) for x in blk) + "},")
c.append("};")
c.append(arr("chr_bank_map", bank_map))
c.append(arr("parallax_img", par_img, per=48))
for nm, idx in (("mt_tl", 0), ("mt_tr", 1), ("mt_bl", 2), ("mt_br", 3)):
    c.append(arr(nm, [m[1][idx] for m in mt] + [0] * (256 - len(mt))))
c.append(arr("mt_pal", [m[2] for m in mt] + [0] * (256 - len(mt))))
c.append(arr("mt_coll", [m[3] for m in mt] + [0] * (256 - len(mt))))
c.append(arr("ground_mt", ground))
for i, L in enumerate(levels):
    c.append(arr("lz_%d" % i, L["lz"]))
    c.append(arr("spr_%d" % i, L["spr"]))
c.append("const level_t level_list[NLEVELS] = {")
for i, L in enumerate(levels):
    c.append('  {lz_%d, spr_%d, {%s}, %d, %d, %d, %d, "%s", "%s"}, /* %s */' % (
        i, i, ",".join(str(x) for x in L["hdr"]), L["width"], L["length"], diffs[i], stars[i], L["up"], L["lo"],
        L["name"]))
c.append("};")
for k, v in NT.items():
    c.append(arr("nt_%s" % k, v))
c.append(arr("cube_speed", speed60, "uint16_t"))
c.append("const uint16_t adjust_heights[4][72] = {" + ",".join("{" + ",".join(str(x) for x in r) + "}" for r in ADJ) + "};")
for k, v in PHYS.items():
    c.append(arr(k, v, "uint16_t"))
for k, v in PAL.items():
    c.append(arr("pal_%s" % k, v))
for k, v in CMAP.items():
    c.append(arr("cmap_%s" % k, v))
open(os.path.join(OUT, "data.c"), "w").write("\n".join(c) + "\n")
open(os.path.join(OUT, "sprites_fd.h"), "w").write(
    "/* Famidash's metasprites (SAUCE/defines/sprites.h, tfdsoft, MIT), its cc65 pragmas taken out by"
    " tools/data.py. */\n" + spr_src + "\n")
print("CHR banks: %d unique of %d, levels: %d (%d main), metatiles: %d" % (len(uniq), NBANKS, len(levels), official, len(mt)))
for L in levels:
    print("  %-26s %4d x %2d  lz %5d  objects %4d  %s / %s" % (L["name"], L["width"], L["hdr"][10], len(L["lz"]),
                                                              (len(L["spr"]) - 1) // 5, L["up"], L["lo"]))
