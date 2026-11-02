#!/usr/bin/env python3
"""Kingdom Rush: its pictures, levels and fonts -> src/data.bin, src/data.h and src/stats.c.

    python3 -I tools/pack.py [KR]      KR: the folder made from the game's Flash file (default
                                       ~/Documents/Assets/gamesrc/kingdomrush), holding:
      swf/full/*GLevelN/1.png   each level's terrain, 700 x 600 (JPEXS, sprite export)
      render/r/<set>/N.png      the game's sprites, frame by frame, at 0.4 scale, the origin in the middle of
                                the picture (tools/java/Render.java in that folder)
      render/d/d_<id>/1.png     the levels' decorations at full size, likewise
      swf/sprites.json          the sprites' frame labels (tools/swfinfo.py)
      spec/data/*.json          levels.json, settings.json, decor.json (tools/extract.py, tools/decor.py)

Everything is the game's own, at 0.4 of its size: the 700 x 600 battlefield is 280 x 240.

data.bin: "KRSH", the number of sections, their offsets, then the sections (SEC_* in data.h):
- HUFF, QUANT, PICS, TABLE, BANK: the terrains and screens, coded as JPEG codes them (tools/codec.py, src/pic.c)
- BGPAL: for each picture, 256 colours (RGB565) the game reduces it to when it loads it (8 bits a pixel in RAM)
- SPAL: the sprites' 256 colours (RGB565)
- FRAMES: u32 count, u32 offsets, then each frame: w, h (u8), ox, oy (s16: the origin from the top left), then
  its rows: a run count, and for each run its offset from the last (u8), its length (u8, bit 7: half see-through)
  and its colours
- ANIMS: 4 bytes an animation (AN_*): first frame (u16), frames (u8), ticks a frame (u8)
- FONTS: as Five Nights at Freddy's 4's (FONT_*)
- LEVELS: for each level, u32 offsets of: its build spots, paths, flags, then each mode's waves (see level.c)"""
import json
import os
import struct
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import codec as C  # noqa: E402

SRC = os.path.join(HERE, "..", "src")
KR = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else "~/Documents/Assets/gamesrc/kingdomrush")
SECTIONS = ["HUFF", "QUANT", "PICS", "TABLE", "BANK", "BGPAL", "SPAL", "FRAMES", "ANIMS", "FONTS", "LEVELS"]
QUALITY = {"terrain": 86}
SCALE = 0.4
FONT_DIRS = [os.environ.get("FONTS", ""), os.path.expanduser("~/Documents/Assets/gamesrc/fonts"),
             os.path.expanduser("~/Documents/Assets/gamesrc/fonts/dejavu"), "/Library/Fonts"]
NLEVELS = 12
MAP_Y0 = 17   # the map's rows shown: from 17 (of 274)
UPG_X = 52    # where the upgrades' panel (215 x 240) sits
TERRAIN_IDS = [1541, 1540, 1537, 1505, 1498, 1495, 1492, 1489, 1486, 1483, 1480, 1477]

DEFINES = []


def define(name, value):
    DEFINES.append((name, value))


def rgb565(r, g, b):
    return (int(r) >> 3) << 11 | (int(g) >> 2) << 5 | int(b) >> 3


# ------------------------------------------------------------------ pictures (JPEG-coded)
class Pic:
    def __init__(self, name, a, quality):
        self.name, self.q = name, quality
        self.h, self.w = a.shape[:2]
        H, W = -(-self.h // 16) * 16, -(-self.w // 16) * 16
        self.a = np.pad(a, ((0, H - self.h), (0, W - self.w), (0, 0)), mode="edge")
        self.mw, self.mh = W // 16, H // 16


PICS = []


def pic(name, im, quality="terrain"):
    a = np.asarray(im.convert("RGB")).astype(np.float64)
    define(f"PIC_{name}", len(PICS))
    PICS.append(Pic(name, a, quality))


def encode_pics():
    qt = {k: (C.qtable(C.STD_LUMA, v), C.qtable(C.STD_CHROMA, v)) for k, v in QUALITY.items()}
    qnames = list(QUALITY)
    for p in PICS:
        y, cb, cr = C.ycbcr(p.a)
        ql, qc = qt[p.q]
        yq = C.quantize(C.blocks_of(y), ql, 0.18)
        cbq = C.quantize(C.blocks_of(cb), qc, 0.22)
        crq = C.quantize(C.blocks_of(cr), qc, 0.22)
        p.sq = [tuple(C.mcu_symbols([yq[2 * my + i, 2 * mx + j] for i in (0, 1) for j in (0, 1)], cbq[my, mx], crq[my, mx]))
                for my in range(p.mh) for mx in range(p.mw)]
    freq = [[{}, {}], [{}, {}]]
    for p in PICS:
        for s in p.sq:
            for comp, tab, sym, _, _ in s:
                f = freq[comp][tab]
                f[sym] = f.get(sym, 0) + 1
    tables, huff = [[None, None], [None, None]], b""
    for comp in (0, 1):
        for tab in (0, 1):
            codes, bits, vals = C.canonical(C.huffman_lengths(freq[comp][tab]))
            tables[comp][tab] = codes
            huff += C.decode_tables(bits, vals)
    bank, where = bytearray(), {}
    for p in PICS:
        p.off = []
        for s in p.sq:
            data = C.encode_mcu(s, tables)
            if data not in where:
                where[data] = len(bank)
                bank += data
            p.off.append(where[data])
    bank += bytes(8)
    quant = b""
    for k in qnames:
        for t in qt[k]:
            quant += b"".join(struct.pack("<H", (int(t[i]) * C.AANSCALES[i] + 2048) >> 12) for i in range(64))
    pics, table = b"", bytearray()
    for p in PICS:
        pics += struct.pack("<BBBBHHI", p.mw, p.mh, qnames.index(p.q), 0, 0xFFFF, 0, len(table))
        for o in p.off:
            table += o.to_bytes(3, "little")
    # the colours each picture is reduced to: its own 256, from the picture as the calculator decodes it
    bgpal = b""
    for p in PICS:
        im = Image.fromarray(np.clip(p.a[:p.h, :p.w], 0, 255).astype(np.uint8))
        q = im.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
        pal = (q.getpalette() + [0] * 768)[:768]
        bgpal += b"".join(struct.pack("<H", rgb565(*pal[3 * i:3 * i + 3])) for i in range(256))
    print(f"pictures: {len(PICS)}, squares {len(bank)} bytes")
    return huff, quant, pics, bytes(table), bytes(bank), bgpal


def terrain(n, decor):
    im = Image.open(f"{KR}/swf/full/DefineSprite_{TERRAIN_IDS[n - 1]}_GLevel{n}/1.png").convert("RGBA")
    base = Image.new("RGBA", (700, 600), (0, 0, 0, 255))
    base.alpha_composite(im.crop((0, 0, 700, 600)) if im.width >= 700 else im)
    for d in decor.get(str(n), []):
        p = f"{KR}/render/d/d_{d['id']}/1.png"
        if not os.path.exists(p):
            continue
        s = Image.open(p).convert("RGBA")
        if d["flip"]:
            s = s.transpose(Image.FLIP_LEFT_RIGHT)
        x, y = round(d["x"] - s.width / 2), round(d["y"] - s.height / 2)
        layer = Image.new("RGBA", (700, 600))
        layer.paste(s, (x, y))
        base.alpha_composite(layer)
    return base.convert("RGB").resize((280, 240), Image.LANCZOS)


# ------------------------------------------------------------------ sprites
SPR = json.load(open(f"{KR}/swf/sprites.json"))
CLASS_ID = {v: k for k, v in SPR["classes"].items()}


def segments(set_id):
    """a sprite's labelled parts: label -> (first, last) frames (from 1), loops stop before their End frame"""
    frames = SPR["sprites"][str(set_id)]["frames"]
    marks = [(i + 1, l) for i, f in enumerate(frames) for l in f["labels"]]
    out = {}
    starts = [(f, l) for f, l in marks if not l.endswith("End")]
    if starts and starts[0][0] > 1:   # frames before the first label
        out["start"] = (1, starts[0][0] - 1)
    for k, (f, l) in enumerate(starts):
        end = next((g for g, m in marks if m in (l + "End", l.replace("thorn", "thor") + "End") and g >= f), None)
        nxt = starts[k + 1][0] - 1 if k + 1 < len(starts) else len(frames)
        last = end if end is not None else nxt
        if l.startswith("walking") or l.startswith("running") or l in ("travel",):
            last = (end - 1) if end is not None and end > f else nxt
        out[l] = (f, max(f, min(last, len(frames))))
    if not out:
        out["all"] = (1, len(frames))
    return out


FRAMES, FRAME_KEY, ANIMS = [], {}, []
ALPHA_LO, ALPHA_HI = 48, 176


def load_frame(path):
    im = Image.open(path).convert("RGBA")
    a = np.asarray(im).astype(np.int32)
    return a, im.width // 2, im.height // 2


SPRITE_PIXELS = []


def add_frame(a, ox, oy):
    """a frame (RGBA, origin at ox, oy) -> its index in FRAMES (shared when equal)"""
    al = a[..., 3]
    ys, xs = np.nonzero(al >= ALPHA_LO)
    if len(ys) == 0:
        a, x0, y0 = np.zeros((1, 1, 4), np.int32), ox, oy
    else:
        x0, x1, y0, y1 = xs.min(), xs.max() + 1, ys.min(), ys.max() + 1
        a = a[y0:y1, x0:x1]
    key = (a.tobytes(), a.shape, int(ox - x0), int(oy - y0))
    if key in FRAME_KEY:
        return FRAME_KEY[key]
    FRAME_KEY[key] = len(FRAMES)
    FRAMES.append((a, int(ox - x0), int(oy - y0)))
    SPRITE_PIXELS.append(a[a[..., 3] >= ALPHA_LO][:, :3])
    return len(FRAMES) - 1


def anim(name, frames, ticks):
    define(f"AN_{name}", len(ANIMS))
    ANIMS.append((frames, ticks))


def sprite_set(prefix, folder, set_id, keep=None, step=2, rename=None):
    """every labelled part of a rendered sprite as an animation AN_<prefix>_<LABEL>"""
    segs = segments(set_id)
    for label, (f0, f1) in segs.items():
        if keep is not None and label not in keep:
            continue
        n = f1 - f0 + 1
        st = step if n >= 6 else 1
        idx = [add_frame(*load_frame(f"{KR}/render/r/{folder}/{f}.png")) for f in range(f0, f1 + 1, st)]
        nm = (rename or {}).get(label, label)
        anim(f"{prefix}_{nm.upper()}" if nm else prefix, idx, st)


def single(prefix, folder, frame=1):
    anim(prefix, [add_frame(*load_frame(f"{KR}/render/r/{folder}/{frame}.png"))], 1)


ENEMIES = ['Goblin', 'FatOrc', 'WolfSmall', 'Shaman', 'Ogre', 'Bandit', 'Brigand', 'Marauder', 'Wolf', 'Spider',
           'SpiderSmall', 'SpiderTiny', 'ShadowArcher', 'DarkKnight', 'Gargoyle', 'Juggernaut', 'GolemHead', 'WhiteWolf',
           'Troll', 'TrollChieftain', 'TrollAxeThrower', 'Yeti', 'YetiBoss', 'Slayer', 'Rocketeer', 'Skeleton',
           'SkeletonBig', 'DemonWolf', 'Demon', 'DemonMage', 'Necromancer', 'DemonImp', 'LavaElemental', 'Veznan',
           'SheepGround', 'SheepFlying']
ENEMY_DROP = {"thorn", "thornFree", "teleport", "deathloop"}


def sprites():
    for e in ENEMIES:
        cid = CLASS_ID["Enemy" + e]
        keep = [l for l in segments(cid) if l not in ENEMY_DROP]
        sprite_set("E_" + e.upper(), "e_" + e, cid, keep, 3 if e in ("YetiBoss", "Veznan", "Juggernaut") else 2)
    # the generic arcane teleport and the thorns' hold: the goblin's, for every enemy
    sprite_set("FX_TELEPORT", "e_Goblin", CLASS_ID["EnemyGoblin"], ["teleport"], 1, {"teleport": ""})
    for c in ['ExplodeNormal', 'ExplodeBig', 'ExplodeAir', 'DesintegrateNormal', 'DesintegrateBig', 'DesintegrateAir',
              'PolymorphNormal', 'PolymorphBig', 'PolymorphAir']:
        sprite_set("FX_" + c.upper(), "fx_" + c, CLASS_ID["Enemy" + c], None, 1, {"explotion": "", "desintegrate": "",
                                                                                     "polymorph": ""})
    towers = {'archer1': 1038, 'archer2': 1036, 'archer3': 1012, 'ranger': 1062, 'musketeer': 1196,
              'mage1': 493, 'mage2': 481, 'mage3': 468, 'arcane': 702, 'sorcerer': 602,
              'eng1': 906, 'eng2': 863, 'eng3': 817, 'bfg': 988, 'tesla': 769,
              'bar1': 411, 'bar2': 403, 'bar3': 394, 'paladin': 340, 'barbarian': 349}
    for n, cid in towers.items():
        sprite_set("T_" + n.upper(), "t_" + n, cid, None, 2)
    for n, cid in {'holder': 382, 'base_archer': 1264, 'base_mage': 1240, 'base_bar': 1236}.items():
        sprite_set("T_" + n.upper(), "t_" + n, cid, None, 1)
    for n in ('build_archer', 'build_mage', 'build_bar', 'flag', 'rally'):
        single("T_" + n.upper(), "t_" + n)
    sprite_set("T_CLOUD", "t_cloud", 385, None, 1, {"all": "", "cloudEnd": "end"})
    sprite_set("T_SELL", "t_sell", 377, None, 1, {"all": ""})
    shooters = {'archer1': 252, 'archer2': 1035, 'archer3': 1011, 'ranger': 1061, 'musketeer': 1195,
                'mage': 457, 'arcane': 617, 'sorcerer': 585}
    for n, cid in shooters.items():
        sprite_set("S_" + n.upper(), "s_" + n, cid, None, 1)
    for c in ['SoldierStandar', 'SoldierStandarLevel2', 'SoldierStandarLevel3', 'SoldierPaladin', 'SoldierBarbarian',
              'SoldierElemental', 'SoldierFarmerJohn', 'SoldierFarmerPaul', 'SoldierFarmerRingo',
              'SoldierReinforcementLevel2A', 'SoldierReinforcementLevel2B', 'SoldierReinforcementLevel3A',
              'SoldierReinforcementLevel3B', 'SoldierReinforcementLevel4A', 'SoldierReinforcementLevel4B']:
        sprite_set("SO_" + c[7:].upper(), "so_" + c[7:], CLASS_ID[c], None, 1)
    for c in ['Arrow', 'ArrowEnemy', 'BoltNecromancer', 'BoltSorcerer', 'BombBfg', 'BombBlack', 'BombCommon',
              'BombDynamite', 'BombFragment', 'BombShrapnel', 'Fireball', 'Missile', 'MissileEnemy',
              'RayArcaneCommon', 'RayArcaneDesintegrate', 'RayPolymorph', 'RayTesla', 'Axe', 'AxeEnemy',
              'IceBlock', 'VeznanBlock', 'SpiderEgg', 'TeleportPortal', 'DemonFire', 'JuggernautEgg',
              'BloodDecal', 'BombDecal', 'ArrowDecal', 'Blood', 'BloodGreen', 'DefenseIndicator', 'WaveFlagReward']:
        sprite_set("B_" + c.upper(), "b_" + c, CLASS_ID[c], None, 1, {"all": ""})
    sprite_set("B_BOLT", "b_Bolt", 4729, None, 1)
    single("UI_WAVEFLAG", "ui_WaveFlag")
    sprite_set("UI_WAVEICO", "ui_WaveFlag_ico_175", CLASS_ID["_KingdomRush_RC_1_fla.WaveFlag_ico_175"], None, 1)
    ui_sprites()


def ui_frames(folder, frames):
    return [add_frame(*load_frame(f"{KR}/render/u/{folder}/{f}.png")) for f in frames]


def crop_icon(name, path, box):
    """a part of a picture, as a one-frame animation with its origin at its top left"""
    a = np.asarray(Image.open(path).convert("RGBA").crop(box)).astype(np.int32)
    anim(name, [add_frame(a, 0, 0)], 1)


def ui_sprites():
    # the tower menu's 40 icons (TowerMenuItem's frames: empty, sell, ..., see ICON_* in ui.c)
    anim("UI_ICONS", ui_frames("icons", range(1, 41)), 1)
    for nm, folder in (("UI_PFIRE", "pfire"), ("UI_PREINF", "preinf")):
        anim(nm + "_IDLE", ui_frames(folder, [1]), 1)
        anim(nm + "_SEL", ui_frames(folder, [20]), 1)
        anim(nm + "_OPEN", ui_frames(folder, range(21, 37)), 2)
        anim(nm + "_READY", ui_frames(folder, range(2, 19)), 1)
    hud = f"{KR}/render/u/hud8/1.png"
    crop_icon("UI_HEART", hud, (177, 165, 193, 181))
    crop_icon("UI_COIN", hud, (217, 167, 236, 181))
    crop_icon("UI_SKULL", hud, (185, 185, 200, 197))
    # the map's flags: a stage to play, one played (its stars), the heroic shield
    anim("UI_FLAGNEW", ui_frames("flagnew", [1]), 1)
    anim("UI_FLAGDONE", ui_frames("flagdone", [1]), 1)
    anim("UI_FLAGSEL", ui_frames("flagdone", [3]), 1)
    anim("UI_FLAGSTAR", ui_frames("flagstar", [1, 2]), 1)
    anim("UI_FLAGHEROIC", ui_frames("flagheroic", [60]), 1)
    # the map's road (its frames "1to2End" to "11to12End": the way so far), origin at the map's top left
    anim("UI_ROAD", ui_frames("road", [18, 34, 53, 81, 109, 146, 159, 175, 200, 225, 250]), 1)
    # MapStarProgress: its plank and star
    sp = f"{KR}/render/u/starprog/1.png"
    crop_icon("UI_STAR", sp, (101, 63, 115, 77))
    crop_icon("UI_STARBOARD", sp, (52, 56, 120, 84))
    # the upgrades' icons (in the game's order: barracks, archers, mages, artillery, rain, reinforcements) and their
    # frames: available, locked, bought
    anim("UI_UPICONS", ui_frames("upicons", range(1, 31)), 1)
    anim("UI_UPITEM", ui_frames("upitem", [1, 4, 5]), 1)
    for n in ("eng", "bfg", "tesla", "ranger", "musketeer"):
        sprite_set("T_BASE_" + n.upper(), "t_base_" + n, {"eng": 1248, "bfg": 1252, "tesla": 1244, "ranger": 1256,
                                                           "musketeer": 1260}[n], None, 1)


def pack_sprites():
    px = np.concatenate(SPRITE_PIXELS).astype(np.uint8)
    strip = Image.fromarray(px.reshape(1, -1, 3))
    q = strip.quantize(255, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array((q.getpalette() + [0] * 768)[:765]).reshape(255, 3)
    spal = struct.pack("<H", 0) + b"".join(struct.pack("<H", rgb565(*c)) for c in pal)
    # nearest of the 255 colours (1 to 255) for every RGB555 colour
    cells = np.stack(np.meshgrid(np.arange(32), np.arange(32), np.arange(32), indexing="ij"), -1).reshape(-1, 3) * 8 + 4
    inv = np.empty(len(cells), np.uint8)
    for i in range(0, len(cells), 4096):
        d = ((cells[i:i + 4096, None, :] - pal[None]) ** 2).sum(-1)
        inv[i:i + 4096] = d.argmin(1) + 1
    blobs = []
    for a, ox, oy in FRAMES:
        h, w = a.shape[:2]
        rgb = a[..., :3] >> 3
        idx = inv[(rgb[..., 0] * 32 + rgb[..., 1]) * 32 + rgb[..., 2]]
        al = a[..., 3]
        b = bytearray(struct.pack("<BBhh", w, h, ox, oy))
        for r in range(h):
            runs, c = [], 0
            while c < w:
                if al[r, c] < ALPHA_LO:
                    c += 1
                    continue
                half = al[r, c] < ALPHA_HI
                s = c
                while c < w and al[r, c] >= ALPHA_LO and (al[r, c] < ALPHA_HI) == half and c - s < 127:
                    c += 1
                runs.append((s, half, idx[r, s:c]))
            b.append(len(runs))
            last = 0
            for s, half, cols in runs:
                while s - last > 255:   # (never at this size)
                    raise SystemExit("pack: a run too far")
                b += bytes([s - last, len(cols) | (0x80 if half else 0)]) + bytes(cols)
                last = s + len(cols)
        blobs.append(bytes(b))
    # the frame table: an entry for each animation's frame in turn (equal frames share their bytes)
    slots, anims = [], b""
    for fr, t in ANIMS:
        anims += struct.pack("<HBB", len(slots), len(fr), t)
        slots += fr
    base = 4 + 4 * len(slots)
    starts, p = [], base
    for bl in blobs:
        starts.append(p)
        p += len(bl)
    frames = struct.pack("<I", len(slots)) + b"".join(struct.pack("<I", starts[i]) for i in slots) + b"".join(blobs)
    return spal, frames, anims


# ------------------------------------------------------------------ fonts (as Five Nights at Freddy's 4's)
def find_font(name):
    for d in FONT_DIRS:
        p = os.path.join(d, name) if d else ""
        if p and os.path.exists(p):
            return p
    raise SystemExit(f"pack: {name} not found (FONTS=<its folder>)")


def nibbles(alpha):
    h, w = alpha.shape
    out = bytearray()
    for r in range(h):
        row = list(alpha[r]) + [0]
        for c in range(0, w, 2):
            out.append(int(row[c]) | int(row[c + 1]) << 4)
    return bytes(out)


def ttf_font(f):
    asc, desc = f.getmetrics()
    line = asc + desc
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
        a = np.clip(np.round(np.asarray(im, dtype=np.float64) / 255 * 15), 0, 15).astype(np.uint8)
        glyphs.append((adv, im.width, im.height, box[0], box[1], nibbles(a)))
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


# ------------------------------------------------------------------ levels
LEVELS = json.load(open(f"{KR}/spec/data/levels.json"))[:NLEVELS]
ENEMY_IDS = {"Enemy" + e: i for i, e in enumerate(ENEMIES)}
NOTES = ["", "NotificationPowerReinforcement", "NotificationPowerFireball"]
MODES = ("campaign", "heroic", "iron")
TOWER_KIND = {"TowerHolder": 0, "TowerArcher": 1, "TowerSoldier": 2, "TowerMage": 3, "TowerEngineer": 4}
PREMADE = {"": 0, "level_1": 1, "level_2": 2, "level_3": 3}


def note(s):
    if s not in NOTES:
        NOTES.append(s)
    return NOTES.index(s)


def fx(v):
    return int(round(v * 16))


def pack_level(lv):
    out = {}
    # paths: the number of paths, then for each its lanes (u8), and for each lane its nodes (u16), each x and y
    # (s16, 1/16 px)
    b = bytearray(struct.pack("<B", len(lv["paths"])))
    for path in lv["paths"]:
        b.append(len(path))
        for lane in path:
            b += struct.pack("<H", len(lane))
            for x, y in lane:
                b += struct.pack("<hh", fx(x), fx(y))
    out["paths"] = bytes(b)
    b = bytearray(struct.pack("<B", len(lv["flags"])))
    for (x0, y0), (x1, y1) in lv["flags"]:
        b += struct.pack("<hhhh", round(x0), round(y0), round(x1), round(y1))
    b += struct.pack("<B", len(lv["defend"]))
    for x, y in lv["defend"]:
        b += struct.pack("<hh", round(x), round(y))
    for k in ("unlockMaxArchersLevel", "unlockMaxBarracksLevel", "unlockMaxEngineersLevel", "unlockMaxMagesLevel"):
        b.append(lv.get(k, 0))
    out["flags"] = bytes(b)
    for mode in MODES:
        hs = lv["holders"][mode]
        b = bytearray(struct.pack("<B", len(hs)))
        for h in hs:
            if h["cls"] == "ref":   # the level's special building (elf hall, frozen sasquatch, temple): later
                b += struct.pack("<BBhhhh", 0xFF, 0, 0, 0, 0, 0)
                continue
            kind = TOWER_KIND.get(h["cls"], 0xFE)
            pre = PREMADE.get(h["args"][0] if h["args"] else "", 0)
            rx, ry = h["rally"] if h["rally"] else (h["x"], h["y"])
            b += struct.pack("<BBhhhh", kind, pre, round(h["x"]), round(h["y"]), round(rx), round(ry))
        d = lv[mode]
        b += struct.pack("<HHBBBBBBB", d["initLives"], d["cash"], d.get("unlockMaxArchers", 0),
                         d.get("unlockMaxBarracks", 0), d.get("unlockMaxEngineers", 0), d.get("unlockMaxMages", 0),
                         d.get("maxUpgradeLevel", 0), int(bool(d.get("power1", True))), int(bool(d.get("power2", True))))
        b += struct.pack("<B", len(d["waves"]))
        for w in d["waves"]:
            b += struct.pack("<HBBBBB", w["interval"], w["path"], note(w["notification"]), note(w["notification2"]),
                             int(w["flying_flag"]), len(w["spawns"]))
            for s in w["spawns"]:
                b += struct.pack("<BBBHHHBB", ENEMY_IDS[s["creep"]], ENEMY_IDS[s["aux"]] if s["aux"] else 0xFF,
                                 s["max_same"], s["count"], s["interval"], s["next"], int(s["fixed_sub_path"]),
                                 s["sub_path"])
        out[mode] = bytes(b)
    return out


def pack_levels():
    blobs = []
    for lv in LEVELS:
        parts = pack_level(lv)
        order = ["paths", "flags"] + list(MODES)
        hdr = len(order) * 4
        b, offs = b"", []
        for k in order:
            offs.append(hdr + len(b))
            b += parts[k]
        blobs.append(b"".join(struct.pack("<I", o) for o in offs) + b)
    out = struct.pack("<I", len(blobs))
    p = 4 + 4 * len(blobs)
    for bl in blobs:
        out += struct.pack("<I", p)
        p += len(bl)
    return out + b"".join(blobs)


# ------------------------------------------------------------------ the numbers: src/stats.c
# what each enemy's class sets on its own (Enemy*.as init): attack charge, life bar (y, size), hit box, dead time
CLASS = {
    'Goblin': (14, -20, 0, 18, 24), 'FatOrc': (10, -23, 0, 21, 24), 'WolfSmall': (8, -20, 0, 23, 26, 15),
    'Shaman': (17, -25, 0, 26, 41), 'Ogre': (27, -39, 1, 30, 51), 'Bandit': (8, -23, 0, 11, 20),
    'Brigand': (13, -25, 0, 12, 24), 'Marauder': (13, -39, 1, 22, 35), 'Wolf': (8, -24, 0, 14, 18, 15),
    'Spider': (17, -25, 0, 20, 25), 'SpiderSmall': (17, -18, 0, 12, 17), 'SpiderTiny': (17, -13, 0, 10, 18),
    'ShadowArcher': (7, -25, 0, 22, 22), 'DarkKnight': (10, -28, 0, 22, 31), 'Gargoyle': (8, -60, 0, 23, 73),
    'Juggernaut': (28, -90, 2, 58, 92, 200), 'GolemHead': (17, -17, 0, 14, 20), 'WhiteWolf': (8, -24, 0, 23, 26, 15),
    'Troll': (12, -26, 0, 15, 26), 'TrollChieftain': (26, -39, 1, 25, 36), 'TrollAxeThrower': (15, -35, 0, 24, 31),
    'Yeti': (30, -45, 1, 33, 51), 'YetiBoss': (40, -110, 2, 72, 115, 90), 'Slayer': (13, -39, 1, 23, 41),
    'Rocketeer': (8, -68, 0, 24, 59), 'Skeleton': (20, -24, 0, 20, 24), 'SkeletonBig': (20, -32, 1, 23, 29),
    'DemonWolf': (13, -23, 0, 23, 25), 'Demon': (9, -23, 0, 10, 25), 'DemonMage': (18, -39, 1, 20, 37),
    'Necromancer': (19, -25, 0, 16, 24), 'DemonImp': (8, -60, 0, 23, 78), 'LavaElemental': (20, -45, 1, 37, 51),
    'Veznan': (37, -37, 1, 24, 38, 340), 'SheepGround': (8, -20, 0, 10, 18), 'SheepFlying': (8, -60, 0, 20, 60),
}
BLOOD = {'Spider': 1, 'SpiderSmall': 1, 'SpiderTiny': 1, 'Juggernaut': 2, 'GolemHead': 2, 'Skeleton': 2,
         'SkeletonBig': 2, 'LavaElemental': 2}
SETTING_KEY = {'Goblin': 'goblin', 'FatOrc': 'fatOrc', 'WolfSmall': 'wolfSmall', 'Shaman': 'shaman', 'Ogre': 'ogre',
               'Bandit': 'bandit', 'Brigand': 'brigand', 'Marauder': 'marauder', 'Wolf': 'wolf', 'Spider': 'spider',
               'SpiderSmall': 'spiderSmall', 'SpiderTiny': 'spiderTiny', 'ShadowArcher': 'shadowArcher',
               'DarkKnight': 'darkKnight', 'Gargoyle': 'gargoyle', 'Juggernaut': 'juggernaut', 'GolemHead': 'golemHead',
               'WhiteWolf': 'whiteWolf', 'Troll': 'troll', 'TrollChieftain': 'trollChieftain',
               'TrollAxeThrower': 'trollAxeThrower', 'Yeti': 'yeti', 'YetiBoss': 'yetiBoss', 'Slayer': 'slayer',
               'Rocketeer': 'rocketeer', 'Skeleton': 'skeleton', 'SkeletonBig': 'skeletonBig', 'DemonWolf': 'demonWolf',
               'Demon': 'demon', 'DemonMage': 'demonMage', 'Necromancer': 'necromancer', 'DemonImp': 'demonImp',
               'LavaElemental': 'lavaElemental', 'Veznan': 'veznan', 'SheepGround': 'goblin', 'SheepFlying': 'gargoyle'}
NAMES = {'Goblin': 'Goblin', 'FatOrc': 'Orc', 'WolfSmall': 'Wulf', 'Shaman': 'Shaman', 'Ogre': 'Ogre',
         'Bandit': 'Bandit', 'Brigand': 'Brigand', 'Marauder': 'Marauder', 'Wolf': 'Worg', 'Spider': 'Giant Spider',
         'SpiderSmall': 'Spider', 'SpiderTiny': 'Spiderling', 'ShadowArcher': 'Shadow Archer',
         'DarkKnight': 'Dark Knight', 'Gargoyle': 'Gargoyle', 'Juggernaut': 'Juggernaut', 'GolemHead': 'Golem Head',
         'WhiteWolf': 'Winter Wolf', 'Troll': 'Troll', 'TrollChieftain': 'Troll Chieftain',
         'TrollAxeThrower': 'Troll Axe Thrower', 'Yeti': 'Yeti', 'YetiBoss': "J.T.", 'Slayer': 'Dark Slayer',
         'Rocketeer': 'Goblin Rocketeer', 'Skeleton': 'Skeleton', 'SkeletonBig': 'Skeleton Knight',
         'DemonWolf': 'Demon Hound', 'Demon': 'Demon Spawn', 'DemonMage': 'Demon Lord', 'Necromancer': 'Necromancer',
         'DemonImp': 'Flareon', 'LavaElemental': 'Lava Elemental', 'Veznan': "Vez'nan", 'SheepGround': 'Sheep',
         'SheepFlying': 'Flying Sheep'}


def write_stats():
    s = json.load(open(f"{KR}/spec/data/settings.json"))
    out = ['/* Made by tools/pack.py from the game\'s GameSettings and its enemies\' classes. */',
           '#include "anim.h"', '#include "kr.h"', '', 'const EnemyStat enemy_stats[EN_COUNT] = {']
    for e in ENEMIES:
        st = s['enemies'][SETTING_KEY[e]]
        c = CLASS[e]
        sheep = e.startswith('Sheep')
        boss = 1 if e in ('Juggernaut', 'YetiBoss', 'Veznan') else 0
        dead = c[5] if len(c) > 5 else 50
        vals = [0 if sheep else st['health'], 0 if sheep else st['armor'], 0 if sheep else st['magicArmor'],
                0 if sheep else st.get('dodge', 0), 0 if sheep else st['gold'], st['cost'], st['minDamage'],
                st['maxDamage'], st['attackReloadTime']]
        flying = 1 if (st['isFlying'] and e != 'SheepGround') or e == 'SheepFlying' else 0
        blockable = 0 if flying else 1   # (each enemy's init: canBeBlocked = false when it flies)
        out.append(f"  {{{', '.join(str(v) for v in vals)}, {float(st['speed'])}f,{st['size']}, {flying}, {boss}, {blockable}, "
                   f"{c[0]}, {st['xSoldierAdjust']}, {st['xAdjust']}, {st['yAdjust']}, {c[1]}, {c[2]}, {c[3]}, {c[4]}, "
                   f"{dead}, {BLOOD.get(e, 0)}}},   /* {e} */")
    out += ['};', '']
    # each enemy's walk (sideways, up, down), idle, fight and death (anim.h's A_*): -1 when it has none
    have = {n for n, _ in DEFINES}
    out += ['const int16_t enemy_anim[EN_COUNT][6] = {']
    for e in ENEMIES:
        row = []
        for lab in ('WALKING', 'WALKINGUP', 'WALKINGDOWN', 'IDLE', 'FIGHTING', 'DEAD'):
            n = f"AN_E_{e.upper()}_{lab}"
            row.append(n if n in have else '-1')
        out.append(f"  {{{', '.join(row)}}},")
    out += ['};', '', 'const char *enemy_name(int type) {', '  static const char *const n[EN_COUNT] = {']
    out += [f'    "{NAMES[e]}",' for e in ENEMIES]
    out += ['  };', '  return type >= 0 && type < EN_COUNT ? n[type] : "";', '}', '']
    # a few settings the rules read by name
    flat = {}
    for grp in ('archers', 'mages', 'engineers', 'soldiers', 'powers', 'enemies'):
        for k, v in s[grp].items():
            for kk, vv in v.items():
                if isinstance(vv, (int, float)) and not isinstance(vv, bool):
                    flat[f"{grp}_{k}_{kk}"] = vv
    hdr = ['/* Made by tools/pack.py: the game\'s GameSettings, by name (S_<group>_<object>_<field>). */',
           '#ifndef KR_SETTINGS_H', '#define KR_SETTINGS_H']
    for k, v in sorted(flat.items()):
        hdr.append(f"#define S_{k} {repr(float(v)) + 'f' if isinstance(v, float) and not float(v).is_integer() else int(v)}")
    hdr += ['#endif', '']
    open(os.path.join(SRC, "stats.c"), "w").write("\n".join(out))
    open(os.path.join(SRC, "settings.h"), "w").write("\n".join(hdr))


# ------------------------------------------------------------------ the game's own words: src/strings.c
def load_strings():
    out = {}
    for line in open(f"{KR}/spec/strings_en.txt", encoding="utf-8", errors="replace"):
        if " = " not in line:
            continue
        k, v = line.rstrip("\n").split(" = ", 1)
        v = v.replace("\\'", "'").replace("\\n", " ").replace("’", "'").replace("‘", "'").replace("\\", "")
        v = " ".join(v.split())
        out[k] = v
    return out


def c_str(s):
    s = s.encode("ascii", "replace").decode().replace("?", "'") if any(ord(ch) > 126 for ch in s) else s
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'


ENEMY_KEY = {'Goblin': 'GOBLIN', 'FatOrc': 'FAT_ORC', 'WolfSmall': 'WULF', 'Shaman': 'SHAMAN', 'Ogre': 'OGRE',
             'Bandit': 'BANDIT', 'Brigand': 'BRIGAND', 'Marauder': 'MARAUDER', 'Wolf': 'WORG', 'Spider': 'SPIDER',
             'SpiderSmall': 'SPIDERSMALL', 'SpiderTiny': 'SPIDERTINY', 'ShadowArcher': 'SHADOW_ARCHER',
             'DarkKnight': 'DARK_NIGHT', 'Gargoyle': 'GARGOYLE', 'Juggernaut': 'JUGGERNAUT', 'GolemHead': 'GOLEM_HEAD',
             'WhiteWolf': 'WHITE_WOLF', 'Troll': 'TROLL', 'TrollChieftain': 'TROLL_CHIEFTAIN',
             'TrollAxeThrower': 'TROLL_AXE_THROWER', 'Yeti': 'YETI', 'YetiBoss': 'YETI_BOSS', 'Slayer': 'SLAYER',
             'Rocketeer': 'ROCKETEER', 'Skeleton': 'SKELETON', 'SkeletonBig': 'SKELETON_BIG', 'DemonWolf': 'DEMON_WOLF',
             'Demon': 'DEMON', 'DemonMage': 'DEMON_MAGE', 'Necromancer': 'NECROMANCER', 'DemonImp': 'DEMON_IMP',
             'LavaElemental': 'LAVA_ELEMENTAL', 'Veznan': 'VEZNAN', 'SheepGround': 'SHEEP', 'SheepFlying': 'SHEEP'}
TIP_OF = {"NotificationTipArmor": 1, "NotificationTipRallyPoint": 12, "NotificationTipStrategySoldier": 2,
          "NotificationTipArmorMagic": 11, "NotificationTipStrategyHeavyArmor": 9}
TOWER_OF = {"NotificationTowerLevel2": ("Level 2 towers", "Your towers can now be upgraded to level 2."),
            "NotificationTowerLevel3": ("Level 3 towers", "Your towers can now be upgraded to level 3."),
            "NotificationTowerArcherRanger": "RANGERS", "NotificationTowerMageArcane": "ARCANE",
            "NotificationTowerArcherMusketeer": "MUSKETEERS", "NotificationTowerSoldierBarbarian": "BARBARIANS",
            "NotificationTowerMageSorcerer": "SORCERER", "NotificationTowerEngineerTesla": "TESLA",
            "NotificationTowerSoldierPaladin": "PALADINS", "NotificationTowerEngineerBfg": "BFG"}


def write_strings():
    S = load_strings()
    out = ['/* Made by tools/pack.py: the game\'s own English words (its Locale strings). */', '#include "kr.h"',
           '#include "strings.h"', '']
    # notifications: a title and a text
    rows = []
    for n in NOTES:
        if not n:
            rows.append(("", ""))
        elif n.startswith("NotificationEnemy"):
            e = n[len("NotificationEnemy"):]
            k = ENEMY_KEY.get(e, e.upper())
            txt = S.get(f"ENEMY_{k}_DESCRIPTION", "")
            sp = S.get(f"ENEMY_{k}_SPECIAL", "").lstrip("*")
            rows.append(("New enemy: " + S.get(f"ENEMY_{k}_NAME", e), txt + (" " + sp + "." if sp and not sp.endswith(".") else (" " + sp if sp else ""))))
        elif n.startswith("NotificationPower"):
            k = "FIREBALL" if "Fireball" in n else "REINFORCEMENTS"
            key = "1" if k == "FIREBALL" else "2"
            rows.append(("New spell: " + S[f"POWER_{k}_NAME"], S[f"POWER_{k}_DESCRIPTION"] + f" Press {key} to aim it, OK to cast."))
        elif n.startswith("NotificationTip"):
            rows.append(("Tip", S.get(f"TIP_{TIP_OF.get(n, 1)}", "")))
        else:
            t = TOWER_OF.get(n)
            if isinstance(t, tuple):
                rows.append(t)
            elif t:
                rows.append(("New tower: " + S[f"TOWER_{t}_NAME"], S[f"TOWER_{t}_DESCRIPTION"]))
            else:
                rows.append(("", ""))
    out.append("static const char *const notes[][2] = {")
    out += [f"  {{{c_str(a)}, {c_str(b)}}}," for a, b in rows]
    out.append("};")
    # the picture beside each: an enemy (1, its type), a spell (2, 1 fire 2 reinforcements), a tower (3, TW_*)
    tw = {"RANGERS": "TW_RANGER", "ARCANE": "TW_ARCANE", "MUSKETEERS": "TW_MUSKETEER", "BARBARIANS": "TW_BARBARIAN",
          "SORCERER": "TW_SORCERER", "TESLA": "TW_TESLA", "PALADINS": "TW_PALADIN", "BFG": "TW_BFG"}
    pics = []
    for n in NOTES:
        if n.startswith("NotificationEnemy") and "Enemy" + n[len("NotificationEnemy"):] in ENEMY_IDS:
            pics.append(f"{{1, {ENEMY_IDS['Enemy' + n[len('NotificationEnemy'):]]}}}")
        elif n.startswith("NotificationPower"):
            pics.append("{2, 1}" if "Fireball" in n else "{2, 2}")
        elif isinstance(TOWER_OF.get(n), str):
            pics.append(f"{{3, {tw[TOWER_OF[n]]}}}")
        else:
            pics.append("{0, 0}")
    out.append("static const uint8_t note_pics[][2] = {" + ", ".join(pics) + "};")
    out.append("int note_pic(int n, int *arg) {")
    out.append("  if (n <= 0 || n >= (int)(sizeof note_pics / sizeof note_pics[0])) return 0;")
    out.append("  *arg = note_pics[n][1];")
    out.append("  return note_pics[n][0];")
    out.append("}")
    out += ["const char *note_title(int n) { return n > 0 && n < (int)(sizeof notes / sizeof notes[0]) ? notes[n][0] : 0; }",
            "const char *note_text(int n) { return n > 0 && n < (int)(sizeof notes / sizeof notes[0]) && notes[n][1][0] ? notes[n][1] : 0; }", ""]
    out.append("static const char *const enemy_desc_s[EN_COUNT] = {")
    out += [f"  {c_str(S.get('ENEMY_' + ENEMY_KEY[e] + '_DESCRIPTION', ''))}," for e in ENEMIES]
    out += ["};", "const char *enemy_desc(int t) { return t >= 0 && t < EN_COUNT ? enemy_desc_s[t] : \"\"; }", ""]
    titles = [S[f"LEVEL_{i}_TITLE"] for i in range(1, NLEVELS + 1)]
    out.append("static const char *const titles[NLEVELS] = {" + ", ".join(c_str(t) for t in titles) + "};")
    out.append("const char *level_title(int i) { return i >= 0 && i < NLEVELS ? titles[i] : \"\"; }")
    hist = [S[f"LEVEL_{i}_HISTORY"] for i in range(1, NLEVELS + 1)]
    out.append("static const char *const stories[NLEVELS] = {")
    out += [f"  {c_str(h)}," for h in hist]
    out += ["};", "const char *level_story(int i) { return i >= 0 && i < NLEVELS ? stories[i] : \"\"; }", ""]
    ups = ["archersUpSalvage", "archersUpEagleEye", "archersUpPiercing", "archersUpFarShots", "archersUpPrecision",
           "barracksUpSurvival", "barracksUpBetterArmor", "barracksUpImprovedDeployment", "barracksUpSurvival2",
           "barracksUpBarbedArmor", "magesUpSpellReach", "magesUpArcaneShatter", "magesUpHermeticStudy",
           "magesUpEmpoweredMagic", "magesUpSlowCurse", "engineersUpConcentratedFire", "engineersUpRangeFinder",
           "engineersUpFieldLogistics", "engineersUpIndustrialization", "engineersUpEfficiency", "rainUpBlazingSkies",
           "rainUpScorchedEarth", "rainsUpBiggerAndMeaner", "rainUpBlazingEarth", "rainUpCataclysm"]
    out.append("static const char *const ups[30][2] = {")
    for u in ups:
        k = u.upper()
        out.append(f"  {{{c_str(S.get('UPGRADE_' + k + '_NAME', u))}, {c_str(S.get('UPGRADE_' + k + '_DESCRIPTION', ''))}}},")
    for i in range(1, 6):
        out.append(f"  {{{c_str(S.get(f'UPGRADE_REINFORCEMENTLEVEL{i}_NAME', f'Reinforcements {i}'))}, "
                   f"{c_str(S.get(f'UPGRADE_REINFORCEMENTLEVEL{i}_DESCRIPTION', ''))}}},")
    out += ["};", "const char *upgrade_name(int line, int n) { return ups[line * 5 + n][0]; }",
            "const char *upgrade_desc(int line, int n) { return ups[line * 5 + n][1]; }", ""]
    # towers and their abilities: names and descriptions by key
    keys = [k for k in S if k.startswith("TOWER_") or k.startswith("POWER_")]
    out.append("static const char *const tw[][2] = {")
    for k in keys:
        out.append(f"  {{{c_str(k)}, {c_str(S[k])}}},")
    out += ["};", "const char *tower_text(const char *key) {",
            "  for (unsigned i = 0; i < sizeof tw / sizeof tw[0]; i++) {",
            "    const char *a = tw[i][0], *b = key;", "    while (*a && *a == *b) a++, b++;",
            "    if (!*a && !*b) return tw[i][1];", "  }", "  return \"\";", "}", ""]
    tips = [S[f"TIP_{i}"] for i in range(1, 21)]
    out.append("static const char *const tips[20] = {")
    out += [f"  {c_str(t)}," for t in tips]
    out += ["};", "const char *tip(int i) { return tips[i % 20]; }", ""]
    open(os.path.join(SRC, "strings.c"), "w").write("\n".join(out))


# ------------------------------------------------------------------ main
def main():
    decor = json.load(open(f"{KR}/spec/data/decor.json"))
    for n in range(1, NLEVELS + 1):
        pic(f"LEVEL{n}", terrain(n, decor))
    # the title (MainMenu, its buttons but START left out) and the world map (Map's terrain and its road), at
    # 320 / 700 of their size: 320 x 274, the calculator's 240 rows of them
    t = Image.open(f"{KR}/render/u/title/47.png").convert("RGBA").crop((366, 366, 686, 606))
    a = np.asarray(t).copy()
    a[127:139, 282:320] = a[115:127, 282:320]   # (the version number at the right edge)
    pic("TITLE", Image.fromarray(a).convert("RGB"))
    m = Image.open(f"{KR}/render/u/map/1.png").convert("RGBA")
    mp = m.crop((366, 366 + MAP_Y0, 686, 606 + MAP_Y0))
    pic("MAP", mp.convert("RGB"))
    define("MAP_Y0", MAP_Y0)
    # the upgrades (MenuUpgrades at 0.33, its buttons and bars left out) over the darkened map
    up = Image.open(f"{KR}/render/u/upgr/1.png").convert("RGBA").crop((155, 216, 370, 456))
    dark = Image.blend(mp.convert("RGB"), Image.new("RGB", mp.size, (0, 0, 0)), 0.55).convert("RGBA")
    dark.alpha_composite(up, (UPG_X, 0))
    pic("UPGRADES", dark.convert("RGB"))
    define("UPG_X", UPG_X)
    huff, quant, pics, table, bank, bgpal = encode_pics()
    sprites()
    write_stats()
    spal, frames, anims = pack_sprites()
    print(f"sprites: {len(FRAMES)} frames, {len(frames)} bytes, {len(ANIMS)} animations")
    fonts = [ttf_font(ImageFont.truetype(find_font("LilitaOne-Regular.ttf"), 13)),   # FONT_HUD
             ttf_font(ImageFont.truetype(find_font("LilitaOne-Regular.ttf"), 22)),   # FONT_BIG
             ttf_font(ImageFont.truetype(find_font("DejaVuSans-Bold.ttf"), 10)),     # FONT_SMALL
             ttf_font(ImageFont.truetype(find_font("DejaVuSans.ttf"), 11))]          # FONT_TEXT
    levels = pack_levels()
    write_strings()
    sections = [huff, quant, pics, table, bank, bgpal, spal, frames, anims, pack_fonts(fonts), levels]
    head = b"KRSH" + struct.pack("<I", len(sections))
    off = len(head) + 4 * len(sections)
    body = b""
    for s in sections:
        head += struct.pack("<I", off + len(body))
        body += s + bytes(-len(s) % 4)
    data = head + body
    open(os.path.join(SRC, "data.bin"), "wb").write(data)
    h = ["/* Made by tools/pack.py: what is in data.bin. */", "#ifndef KR_DATA_H", "#define KR_DATA_H", ""]
    h += [f"#define SEC_{n} {i}" for i, n in enumerate(SECTIONS)]
    h += [f"#define FONT_{n} {i}" for i, n in enumerate(["HUD", "BIG", "SMALL", "TEXT"])]
    h += [f"#define {n} {v}" for n, v in DEFINES]
    h += [f"#define EN_{e.upper()} {i}" for i, e in enumerate(ENEMIES)]
    h += [f"#define EN_COUNT {len(ENEMIES)}", f"#define NLEVELS {NLEVELS}", ""]
    h += [f"#define NOTE_{n.replace('Notification', '').upper() or 'NONE'} {i}" for i, n in enumerate(NOTES)]
    h += ["", "#endif", ""]
    open(os.path.join(SRC, "data.h"), "w").write("\n".join(h))
    print(f"data.bin: {len(data)} bytes ({', '.join(f'{n} {len(s)}' for n, s in zip(SECTIONS, sections))})")


if __name__ == "__main__":
    main()
