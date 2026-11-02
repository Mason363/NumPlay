#!/usr/bin/env python3
"""Clash Royale: the game's own numbers and pictures -> src/data.bin, src/data.h and src/tables.c.

    python3 -I tools/pack.py [APK]     APK: Clash Royale's 2016 Android package (archive.org item clash-royale-2016,
                                       CLASHROYALE2016.apk); by default ~/Documents/Assets/gamesrc/clashroyale/dl/cr2016.apk

Everything comes from the package: the cards, characters, buildings, projectiles and spells from assets/csv_logic
(tables.c), the pictures from assets/sc (tools/sc.py draws them), the names from assets/csv_client/texts.csv, the
font from assets/font.

The arena is turned a quarter for the calculator's wide screen: your side on the left. One tile is 10 pixels, the
field 320 x 180. The game draws a tile 29.68 units wide and 24.12 high (its camera looks down at an angle), so the
pictures are drawn at 10 / 29.68 of their size; the ground is resampled to square tiles.

data.bin: "CRD1", the number of sections, their offsets (SEC_* in data.h), then:
- IMGS: the number of pictures, their offsets; a picture: width, height, x and y of its anchor (u16, u16, s16,
  s16), its mode (0: opaque, 1: 6-bit colour and 2-bit alpha), 0, 0, 0, the offsets of its blue and red palettes
  (u32, from the file's start), then for each row the offset of the row (u16, from the picture's start); a row: the
  number of runs (u8), each run its x, its length (u8) and its colours (u8 each)
- PALS: palettes: the number of colours (u16), then RGB565 each
- FONTS: for each font (FONT_*): line height, baseline, 0, 0, then 95 glyphs (32 to 126): advance, width, height,
  x and y (s8), 0, 0, 0, the offset of its 4-bit alpha rows (u32, from the section)
- ARENAS: the number of arenas, their offsets; an arena: 256 colours (RGB565), the packed size (u32), then the
  320 x 180 picture as colour indices, packed (np_lz.h's LZSS)
"""
import csv
import io
import os
import struct
import sys
import zipfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont
from scipy.cluster.vq import kmeans2

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import sc as SCF  # noqa: E402

APK = os.path.expanduser('~/Documents/Assets/gamesrc/clashroyale/dl/cr2016.apk')
OUT = os.path.join(HERE, '..', 'src')
TILE = 10
SX, SY = 29.68, 24.115           # the game's units for a tile across and down
S = TILE / SX                    # pictures' scale
SECTIONS = ['IMGS', 'PALS', 'FONTS', 'ARENAS']
DIRS = (1, 3, 5, 7, 9)           # the game's nine directions, up to down on the right; five of them kept

# ------------------------------------------------------------------ the package


class Apk:
    def __init__(self, path):
        self.z = zipfile.ZipFile(path)
        self.cache = {}

    def read(self, name):
        return self.z.read('assets/' + name)

    def sc(self, name):
        if name not in self.cache:
            tex = name[:-3] + '_tex.sc'
            try:
                td = self.read(tex)
            except KeyError:
                td = None
            self.cache[name] = SCF.SC(self.read(name), td)
            self.cache[name].path = name
        return self.cache[name]

    def csv(self, name):
        rows = list(csv.reader(io.StringIO(self.read(name).decode('utf-8'))))
        h = rows[0]
        out, last = {}, None
        for row in rows[2:]:
            if row and row[0]:
                last = row[0]
                out[last] = {h[i]: v for i, v in enumerate(row) if i < len(h) and v != ''}
        return out


# ------------------------------------------------------------------ pictures


class Pics:
    def __init__(self):
        self.imgs = []       # bytes of each picture (palette offsets patched later)
        self.pals = []       # bytes of each palette
        self.names = {}

    def pal(self, colors):
        c = np.clip(np.round(np.asarray(colors, float) * 255), 0, 255).astype(int)
        b = struct.pack('<H', len(c)) + b''.join(struct.pack('<H', (r >> 3) << 11 | (g >> 2) << 5 | bl >> 3) for r, g, bl in c)
        if b in self.pals:
            return self.pals.index(b)
        self.pals.append(b)
        return len(self.pals) - 1

    def add(self, idx, mask, ax, ay, pb, pr, mode=0, name=None):
        """idx: colour indices (h x w), mask: which are drawn, (ax, ay) the anchor"""
        h, w = idx.shape
        ys, xs = np.nonzero(mask)
        if len(ys) == 0:
            ys, xs = np.array([0]), np.array([0])
            mask = np.zeros_like(mask)
        y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        idx, mask = idx[y0:y1, x0:x1], mask[y0:y1, x0:x1]
        h, w = idx.shape
        assert w < 256, w
        rows = []
        for y in range(h):
            runs, x = [], 0
            while x < w:
                if not mask[y, x]:
                    x += 1
                    continue
                s = x
                while x < w and mask[y, x] and x - s < 255:
                    x += 1
                runs.append(bytes([s, x - s]) + bytes(int(v) for v in idx[y, s:x]))
            rows.append(bytes([len(runs)]) + b''.join(runs))
        head = 20 + 2 * h
        offs, o = [], head
        for r in rows:
            offs.append(o)
            o += len(r)
        assert o < 65536
        b = struct.pack('<HHhhBBBB', w, h, int(ax - x0), int(ay - y0), mode, 0, 0, 0)
        b += struct.pack('<II', pb, pr) + b''.join(struct.pack('<H', v) for v in offs) + b''.join(rows)
        self.imgs.append(b)
        i = len(self.imgs) - 1
        if name:
            self.names[name] = i
        return i


P = Pics()


RCACHE = {}
RCACHE_FILE = os.path.join(HERE, '..', 'build', 'render-cache.pkl')


def render(scf, name, frame, scale, size=(160, 160), anchor=(80, 120), hide=()):
    """(kept in build/render-cache.pkl between runs: drawing is the slow part)"""
    key = (getattr(scf, 'path', ''), name, frame, round(scale, 6), tuple(size), tuple(anchor), tuple(hide))
    if key in RCACHE:
        return RCACHE[key].astype(np.float32) / 255
    cv = SCF.Canvas(size[0], size[1], anchor[0], anchor[1], scale)
    cv.draw(scf, scf.pieces(name, frame, hide=hide))
    out = cv.rgba()
    RCACHE[key] = np.round(out * 255).astype(np.uint8)
    return RCACHE[key].astype(np.float32) / 255


def quantize(pairs, k):
    """pairs: (n, 6) blue and red colours of the same pixels -> labels, blue palette, red palette"""
    uniq, inv = np.unique(np.round(pairs * 63).astype(np.int32), axis=0, return_inverse=True)
    inv = inv.reshape(-1)
    if len(uniq) <= k:
        cent = uniq / 63.0
        return inv, cent[:, :3], cent[:, 3:]
    rng = np.random.default_rng(1)
    sample = pairs if len(pairs) < 40000 else pairs[rng.choice(len(pairs), 40000, replace=False)]
    np.random.seed(1)
    cent, _ = kmeans2(sample.astype(np.float64), k, minit='++', iter=12, seed=1)
    d = ((pairs[:, None, :] - cent[None]) ** 2).sum(-1) if len(pairs) * k < 3e7 else None
    if d is None:
        lab = np.concatenate([((pairs[i:i + 20000, None, :] - cent[None]) ** 2).sum(-1).argmin(1)
                              for i in range(0, len(pairs), 20000)])
    else:
        lab = d.argmin(1)
    return lab, cent[:, :3], cent[:, 3:]


def add_group(frames, name=None, k=255, mode=0, thr=0.45):
    """frames: list of (blue rgba, red rgba or None, anchor x, anchor y); one palette pair for all -> picture ids"""
    pix, masks = [], []
    for fb, fr, ax, ay in frames:
        m = fb[..., 3] > thr
        if mode == 1:
            m = fb[..., 3] > 0.12
        masks.append(m)
        b = fb[..., :3][m]
        r = (fr if fr is not None else fb)[..., :3][m]
        pix.append(np.concatenate([b, r], 1))
    allp = np.concatenate(pix) if pix else np.zeros((1, 6))
    if len(allp) == 0:
        allp = np.zeros((1, 6))
    kk = 63 if mode == 1 else k
    lab, pb, pr = quantize(allp, kk)
    ib, ir = P.pal(pb), P.pal(pr)
    ids, o = [], 0
    for (fb, fr, ax, ay), m in zip(frames, masks):
        n = int(m.sum())
        idx = np.zeros(m.shape, np.int32)
        idx[m] = lab[o:o + n]
        o += n
        if mode == 1:
            a = np.clip(np.round(fb[..., 3] * 4 - 0.5), 0, 3).astype(np.int32)
            idx = idx | a << 6
        ids.append(P.add(idx, m, ax, ay, ib, ir, mode))
    if name:
        P.names[name] = ids[0]
    return ids


# ------------------------------------------------------------------ characters and buildings: sprite sets

SETS = []      # (name, {anim: (first picture, frames, dirs, action frame, the game's frame count)})


def sample(n, k, act=None):
    """k frames of n, evenly, the action frame among them"""
    if n <= k:
        return list(range(n))
    fr = sorted(set(int(i * n / k) for i in range(k)))
    if act is not None and act not in fr:
        j = min(range(len(fr)), key=lambda i: abs(fr[i] - act))
        fr[j] = act
    return sorted(set(fr))


def char_set(apk, setname, file, blue, red, scale, nrun=6, natt=6, size=(96, 112), anchor=(48, 92), dirs=DIRS):
    scf = apk.sc(file)
    anims = {}
    frames, meta = [], []
    for an, k in (('run', nrun), ('attack', natt), ('idle', 1)):
        src = an
        ds = [d for d in dirs if '%s_%s1_%d' % (blue, an, d) in scf.exports]
        for alt in ('idle', 'run', 'attack'):
            if ds:
                break
            src = alt
            ds = [d for d in dirs if '%s_%s1_%d' % (blue, alt, d) in scf.exports]
        nd = len(ds)
        n0 = scf.frames('%s_%s1_%d' % (blue, src, ds[0]))
        labels = scf.labels('%s_%s1_%d' % (blue, src, ds[0]))
        act = labels.get('action_frame')
        fr = sample(n0, k, act) if an != 'idle' else [0]
        a = fr.index(act) if act in fr else 255
        first = len(frames)
        for d in ds:
            bn, rn = '%s_%s1_%d' % (blue, src, d), '%s_%s1_%d' % (red, src, d)
            if rn not in scf.exports:
                rn = bn
            for f in fr:
                fb = render(scf, bn, f, scale * S, size, anchor)
                frr = render(scf, rn, f, scale * S, size, anchor) if rn != bn else None
                frames.append((fb, frr, anchor[0], anchor[1]))
        meta.append((an, first, len(fr), nd, a, n0))
    ids = add_group(frames)
    for an, first, nf, nd, a, n0 in meta:
        anims[an] = (ids[first], nf, nd, a, n0)
    SETS.append((setname, anims))
    return len(SETS) - 1


def still_set(apk, setname, file, blue, red, scale, frames_of=None, size=(110, 120), anchor=(55, 90), hide=()):
    """a building: one picture (or a few: frames_of = [frame numbers]) for each side; 'dirs' of one"""
    scf = apk.sc(file)
    if blue not in scf.exports and blue + '_idle1_1' in scf.exports:
        blue, red = blue + '_idle1_1', red + '_idle1_1'
    fl = frames_of or [0]
    frames = []
    for f in fl:
        fb = render(scf, blue, f, scale * S, size, anchor, hide)
        fr = render(scf, red, f, scale * S, size, anchor, hide) if red and red != blue else None
        frames.append((fb, fr, anchor[0], anchor[1]))
    ids = add_group(frames)
    a = {'idle': (ids[0], len(fl), 1, 255, len(fl))}
    SETS.append((setname, a))
    return len(SETS) - 1


def turret_set(apk, setname, file, blue, red, scale, n=19, size=(110, 120), anchor=(55, 90)):
    """cannon and X-Bow: 19 frames from up to down on the right; 5 kept"""
    scf = apk.sc(file)
    fl = [0, 4, 9, 14, 18] if n == 19 else [int(i * (n - 1) / 4) for i in range(5)]
    frames = []
    for f in fl:
        frames.append((render(scf, blue, f, scale * S, size, anchor), render(scf, red, f, scale * S, size, anchor), *anchor))
    ids = add_group(frames)
    SETS.append((setname, {'idle': (ids[0], 1, 5, 255, 1)}))
    return len(SETS) - 1


def effect(apk, file, name, scale, frames, size=(120, 120), anchor=(60, 60), mode=1, pname=None):
    scf = apk.sc(file)
    fl = [render(scf, name, f, scale * S, size, anchor) for f in frames]
    ids = add_group([(f, None, anchor[0], anchor[1]) for f in fl], mode=mode)
    if pname:
        P.names[pname] = ids[0]
    return ids


# ------------------------------------------------------------------ the tables


MULT = [100]
for _ in range(14):
    MULT.append(MULT[-1] * 110 // 100)


def num(d, k, default=0):
    return int(d.get(k, default))


def build(apk):
    chars = apk.csv('csv_logic/characters.csv')
    blds = apk.csv('csv_logic/buildings.csv')
    projs = apk.csv('csv_logic/projectiles.csv')
    aeos = apk.csv('csv_logic/area_effect_objects.csv')
    buffs = apk.csv('csv_logic/character_buffs.csv')
    glob = apk.csv('csv_logic/globals.csv')
    texts = {r[0]: r[1] for r in csv.reader(io.StringIO(apk.read('csv_client/texts.csv').decode('utf-8'))) if r}
    cards = []
    for kind, f in ((0, 'spells_characters'), (1, 'spells_buildings'), (2, 'spells_other')):
        for n, c in apk.csv('csv_logic/%s.csv' % f).items():
            if c.get('NotInUse') or n == 'Mirror':
                continue
            cards.append((kind, n, c))

    # units: everything a card summons, spawns, or leaves when it dies, and the towers
    unit_names = ['KingTower', 'PrincessTower']
    for kind, n, c in cards:
        if c.get('SummonCharacter'):
            unit_names.append(c['SummonCharacter'])
        p = c.get('Projectile')
        if p and projs[p].get('SpawnCharacter'):
            unit_names.append(projs[p]['SpawnCharacter'])
    i = 0
    while i < len(unit_names):
        u = chars.get(unit_names[i]) or blds[unit_names[i]]
        for k in ('SpawnCharacter', 'DeathSpawnCharacter', 'AttachedCharacter'):
            if u.get(k) and u[k] not in unit_names:
                unit_names.append(u[k])
        i += 1
    unit_names = list(dict.fromkeys(unit_names))
    proj_names = []
    for n in unit_names:
        u = chars.get(n) or blds[n]
        if u.get('Projectile'):
            proj_names.append(u['Projectile'])
    for kind, n, c in cards:
        # (Arrows: the first projectile, ArrowsSpell, does the damage; the rest of the volley is for show)
        if c.get('CustomFirstProjectile') or c.get('Projectile'):
            proj_names.append(c.get('CustomFirstProjectile') or c['Projectile'])
        a = c.get('AreaEffectObject')
        if a and aeos[a].get('Projectile'):
            proj_names.append(aeos[a]['Projectile'])
    proj_names = list(dict.fromkeys(proj_names))
    area_names = list(dict.fromkeys(c['AreaEffectObject'] for k, n, c in cards if c.get('AreaEffectObject')))
    return chars, blds, projs, aeos, buffs, glob, texts, cards, unit_names, proj_names, area_names


def cname(s):
    return ''.join(ch if ch.isalnum() else '_' for ch in s).upper()


APKREF, BOTN = [None], [0]


def main():
    apk = Apk(sys.argv[1] if len(sys.argv) > 1 else APK)
    APKREF[0] = apk
    import pickle
    if os.path.exists(RCACHE_FILE):
        RCACHE.update(pickle.load(open(RCACHE_FILE, 'rb')))
    try:
        run(apk)
    finally:
        if os.path.isdir(os.path.dirname(RCACHE_FILE)):
            pickle.dump(RCACHE, open(RCACHE_FILE, 'wb'))


def run(apk):
    chars, blds, projs, aeos, buffs, glob, texts, cards, unit_names, proj_names, area_names = build(apk)
    G = {k: v.get('NumberValue') for k, v in glob.items()}
    pvp = apk.csv('csv_logic/locations.csv')['PvP_goblin']
    G['MATCH_LENGTH'], G['OVERTIME_SECONDS'] = pvp['MatchLength'], pvp['OvertimeSeconds']

    # --- sprite sets for the units
    unit_set = {}
    for n in unit_names:
        u = chars.get(n)
        if u:
            sc_scale = num(u, 'Scale', 100) / 100
            if n == 'Golemite':
                sc_scale = 0.75
            big = num(u, 'CollisionRadius') >= 750 or n in ('Balloon', 'BabyDragon', 'Golem')
            size, anchor = ((140, 150), (70, 115)) if big else ((80, 96), (40, 76))
            unit_set[n] = char_set(apk, n, u['FileName'], u['BlueExportName'], u['RedExportName'], sc_scale,
                                   size=size, anchor=anchor, dirs=(1,) if n == 'Balloon' else DIRS)
            print('set', n, flush=True)
    for n in unit_names:
        b = blds.get(n)
        if not b or n in unit_set:
            continue
        f, be, re_ = b['FileName'], b['BlueExportName'], b['RedExportName']
        sc_scale = num(b, 'Scale', 100) / 100
        if n in ('Cannon', 'Xbow'):
            unit_set[n] = turret_set(apk, n, f, be, re_, sc_scale)
        elif n == 'Mortar':
            unit_set[n] = char_set(apk, n, f, be, re_, sc_scale, nrun=1, natt=3, size=(110, 120), anchor=(55, 90),
                                   dirs=(1, 2, 3, 4, 5))
        elif n == 'Tesla':
            scf = apk.sc(f)
            lab = scf.labels(be)
            unit_set[n] = still_set(apk, n, f, be, re_, sc_scale, [lab['idle'], lab['hide_start'] + 8, lab['hide_end']])
        elif n == 'KingTower':
            unit_set[n] = still_set(apk, n, f, be, re_, 1.0, [0], size=(130, 140), anchor=(65, 95))
        elif n == 'PrincessTower':
            unit_set[n] = still_set(apk, n, f, be, re_, 1.0, [0], size=(110, 120), anchor=(55, 90))
            unit_set['PrincessTowerTop'] = still_set(apk, 'PrincessTowerTop', f, b['BlueTopExportName'],
                                                     b['RedTopExportName'], 1.0, [0], size=(110, 80), anchor=(55, 60))
        elif n == 'BombTower':
            unit_set[n] = still_set(apk, n, f, be, re_, sc_scale)
            unit_set['BombTowerTop'] = still_set(apk, 'BombTowerTop', f, b['BlueTopExportName'], b['RedTopExportName'],
                                                 sc_scale, [0], size=(110, 80), anchor=(55, 60))
        elif n in ('GiantSkeletonBomb', 'BalloonBomb'):
            unit_set[n] = still_set(apk, n, f, be, re_, sc_scale, [0, 15, 28], size=(80, 80), anchor=(40, 60))
        else:
            unit_set[n] = still_set(apk, n, f, be, re_, sc_scale)
        print('set', n, flush=True)
    scf = apk.sc('sc/building_tower.sc')
    rub = render(scf, 'Tower_destroyed_ground1', 0, S, (110, 100), (55, 60))
    rub2 = render(scf, 'Tower_destroyed_ground2', 0, S, (110, 100), (55, 60))
    add_group([(rub, None, 55, 60), (rub2, None, 55, 60)], name='IMG_RUBBLE')

    # --- projectiles and effects: one picture each (mode 1: with alpha)
    proj_img = {}
    for n in proj_names:
        p = projs[n]
        if not p.get('ExportName'):
            continue
        e = p['ExportName']
        if e in ('projectile_arrow_basic_enemy', 'projectile_arrow_basic', 'projectile_spear_360'):
            continue          # drawn as lines
        if e not in proj_img:
            scf = apk.sc(p['FileName'])
            proj_img[e] = add_group([(render(scf, e, 0, S * 0.9, (60, 60), (30, 30)), None, 30, 30)], mode=1)[0]
    fx = {}
    fx['explosion'] = effect(apk, 'sc/effects.sc', 'RoundExplosion', 1.0, [0, 3, 6, 9, 12, 15], pname='IMG_EXPLOSION')
    fx['puff'] = effect(apk, 'sc/effects.sc', 'RoundExplosion', 0.45, [0, 4, 8, 12], (60, 60), (30, 30), pname='IMG_PUFF')
    fx['zap'] = effect(apk, 'sc/effects.sc', 'zap_bolt1', 0.9, [0, 3, 6], (80, 140), (40, 120), pname='IMG_ZAP')
    fx['lightning'] = effect(apk, 'sc/effects.sc', 'lightning_bolt', 0.9, [0, 3, 6], (80, 220), (40, 200), pname='IMG_LIGHTNING')
    scf = apk.sc('sc/effects.sc')
    add_group([(render(scf, 'crown_rotate_anim', 0, 0.45, (40, 40), (20, 24)), None, 20, 24)], name='IMG_CROWN')
    scf = apk.sc('sc/ui.sc')
    add_group([(render(scf, 'spell_card_ingame', 0, 0.16, (24, 24), (12, 12), hide=()), None, 12, 12)], name='IMG_CARDFRAME')
    # the elixir drop: the bottom-right of the in-game card
    big = render(scf, 'spell_card_ingame', 0, 1.0, (140, 170), (70, 85))
    drop = big[125:165, 95:135]
    dimg = Image.fromarray((drop * 255).astype(np.uint8), 'RGBA').resize((10, 10), Image.LANCZOS)
    add_group([(np.asarray(dimg, np.float32) / 255, None, 5, 5)], name='IMG_DROP')
    dimg = Image.fromarray((drop * 255).astype(np.uint8), 'RGBA').resize((16, 16), Image.LANCZOS)
    add_group([(np.asarray(dimg, np.float32) / 255, None, 8, 8)], name='IMG_DROP_BIG')

    # --- card pictures: 36 x 45
    scf = apk.sc('sc/ui_spells.sc')
    card_icon = {}
    for kind, n, c in cards:
        ic = c['IconFile']
        rgba = render(scf, ic, 0, 0.153, (38, 48), (19, 24))
        small = render(scf, ic, 0, 0.1, (26, 32), (13, 16))
        card_icon[n] = add_group([(rgba, None, 19, 24), (small, None, 13, 16)], k=255)[0]   # (the small one next)
    hidden = add_group([(render(scf, 'hidden', 0, 0.153, (38, 48), (19, 24)), None, 19, 24)])[0]
    P.names['IMG_CARD_HIDDEN'] = hidden

    # --- the title picture: the loading screen, 180 x 240
    scf = apk.sc('sc/loading.sc')
    lo, hi = scf.bounds('loading_screen')
    title = render(scf, 'loading_screen', 0, 248 / 1024, (180, 248), (90, 0))[:240]   # (without its loading bar)
    add_group([(title, None, 0, 0)], name='IMG_TITLE')

    # --- arenas
    arenas = []
    for loc in ('goblin_arena', 'bone_arena', 'barbarian_arena', 'dark_arena', 'spell_arena', 'royal_arena', 'legendary_arena'):
        arenas.append(arena(apk, loc))
        print('arena', loc, flush=True)

    # --- fonts
    fonts = font_section(apk)

    write(apk, chars, blds, projs, aeos, buffs, G, texts, cards, unit_names, proj_names, area_names, unit_set,
          proj_img, card_icon, fonts, arenas)


# ------------------------------------------------------------------ arenas


def arena(apk, loc):
    rows = list(csv.reader(io.StringIO(apk.read('locations/%s.csv' % loc).decode('utf-8'))))
    file, export = rows[3][1], rows[3][2]
    scf = apk.sc(file)
    # the field in the game's units: x -9 to 9 tiles, y 0 to 32 tiles; drawn 3 times the calculator's size
    k = 3
    W, H = 18 * TILE * k, 32 * TILE * k
    cv = SCF.Canvas(W, H, 0, 0, 1.0, ss=1)
    # a matrix from the game's picture to this canvas: x * (TILE*k/SX) + W/2, (y + 1.25) * (TILE*k/SY)
    m = (TILE * k / SX, 0, 0, TILE * k / SY, W / 2, 1.25 * TILE * k / SY)
    cv.draw(scf, scf.pieces(export, 0, m))
    ground = cv.rgba()
    img = Image.fromarray((ground[..., :3] * 255).astype(np.uint8)).resize((18 * TILE, 32 * TILE), Image.LANCZOS)
    img = img.transpose(Image.ROTATE_270)     # the game's top (the enemy) to the right, its left lane to the top
    base = np.asarray(img, np.float32) / 255
    # (the decorations at the river's ends would stand in the field once it is turned: left out)
    rgb = Image.fromarray((base * 255).astype(np.uint8))
    q = rgb.quantize(256, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array(q.getpalette()[:768], float).reshape(-1, 3) / 255
    pal = np.vstack([pal, np.zeros((256 - len(pal), 3))])
    idx = np.asarray(q, np.uint8).tobytes()
    packed = lz_pack(idx)
    c = np.clip(np.round(pal * 255), 0, 255).astype(int)
    b = b''.join(struct.pack('<H', (r >> 3) << 11 | (g >> 2) << 5 | bl >> 3) for r, g, bl in c)
    rgb.save(os.path.join(HERE, '..', 'build', 'arena_%s.png' % loc)) if os.path.isdir(os.path.join(HERE, '..', 'build')) else None
    return b + struct.pack('<I', len(packed)) + packed


def lz_pack(data):
    """np_lz.h's stream: a flag byte for 8 items (set: a literal), a match: distance - 1 (12 bits), length - 3 (4)"""
    n = len(data)
    out = bytearray()
    i = 0
    heads = {}
    while i < n:
        flag_at = len(out)
        out.append(0)
        for b in range(8):
            if i >= n:
                break
            best, bd = 0, 0
            key = data[i:i + 3]
            if len(key) == 3:
                for c in reversed(heads.get(key, [])[-48:]):
                    if i - c > 4096:
                        break
                    ln = 0
                    while ln < 18 and i + ln < n and data[c + ln] == data[i + ln]:
                        ln += 1
                    if ln > best:
                        best, bd = ln, i - c
                        if ln == 18:
                            break
            if best >= 3:
                out += bytes([(bd - 1) & 255, (bd - 1) >> 8 | (best - 3) << 4])
                for j in range(i, i + best):
                    heads.setdefault(data[j:j + 3], []).append(j)
                i += best
            else:
                out[flag_at] |= 1 << b
                out.append(data[i])
                heads.setdefault(data[i:i + 3], []).append(i)
                i += 1
    return bytes(out)


# ------------------------------------------------------------------ fonts


def font_section(apk):
    blobs = []
    # FONT_SMALL: the game's body font (CCBackBeat); FONT_MID, FONT_BIG, FONT_NUM: its titles' (Supercell-Magic)
    for file, size in (('sc_ccbackbeatregular.ttf', 11), ('supercell-magic_0.ttf', 13), ('supercell-magic_0.ttf', 20),
                       ('supercell-magic_0.ttf', 9)):
        f = ImageFont.truetype(io.BytesIO(apk.read('font/' + file)), size)
        asc, desc = f.getmetrics()
        glyphs, data = [], b''
        for ch in range(32, 127):
            s = chr(ch)
            adv = int(round(f.getlength(s)))
            bb = f.getbbox(s)
            if bb[2] <= bb[0]:
                glyphs.append((adv, 0, 0, 0, 0, 0))
                continue
            im = Image.new('L', (bb[2] - bb[0], bb[3] - bb[1]))
            ImageDraw.Draw(im).text((-bb[0], -bb[1]), s, font=f, fill=255)
            a = np.asarray(im)
            w, h = a.shape[1], a.shape[0]
            q = (a.astype(int) * 15 + 127) // 255
            rows = b''
            for y in range(h):
                row = list(q[y]) + ([0] if w & 1 else [])
                rows += bytes(row[i] | row[i + 1] << 4 for i in range(0, len(row), 2))
            glyphs.append((adv, w, h, bb[0], bb[1], len(data)))
            data += rows
        blobs.append((asc + desc, asc, glyphs, data))
    # FONTS: count, offsets; each: line, base, 0, 0, glyphs (12 bytes), data
    out = struct.pack('<I', len(blobs))
    heads = []
    body = b''
    for line, base, glyphs, data in blobs:
        g0 = 4 + 95 * 12
        b = struct.pack('<BBBB', line, base, 0, 0)
        for adv, w, h, x, y, off in glyphs:
            b += struct.pack('<BBBbbBBBI', adv, w, h, x, y, 0, 0, 0, off + g0)
        b += data
        heads.append(body)
        body += b
    offs = []
    o = 4 + 4 * len(blobs)
    for i, (line, base, glyphs, data) in enumerate(blobs):
        offs.append(o)
        o += 4 + 95 * 12 + len(data)
    return out + b''.join(struct.pack('<I', v) for v in offs) + body


# ------------------------------------------------------------------ writing


def write(apk, chars, blds, projs, aeos, buffs, G, texts, cards, unit_names, proj_names, area_names, unit_set,
          proj_img, card_icon, fonts, arenas):
    # data.bin
    imgs_head = 4 + 4 * len(P.imgs)
    pal_base = None
    sec = {}
    body = b''
    sec_imgs = struct.pack('<I', len(P.imgs))
    o = imgs_head
    offs = []
    for b in P.imgs:
        offs.append(o)
        o += len(b)
    head_len = 8 + 4 * len(SECTIONS)
    # the palettes' absolute offsets: after IMGS (padded to 4 bytes)
    imgs_len = (o + 3) & ~3
    pals_start = head_len + imgs_len
    pal_off, po = [], pals_start
    for p in P.pals:
        pal_off.append(po)
        po += len(p)
    fixed = []
    for b in P.imgs:
        pb, pr = struct.unpack_from('<II', b, 12)
        fixed.append(b[:12] + struct.pack('<II', pal_off[pb], pal_off[pr]) + b[20:])
    sec['IMGS'] = sec_imgs + b''.join(struct.pack('<I', v) for v in offs) + b''.join(fixed)
    sec['PALS'] = b''.join(P.pals)
    sec['FONTS'] = fonts
    ar = struct.pack('<I', len(arenas))
    ao = 4 + 4 * len(arenas)
    for a in arenas:
        ar += struct.pack('<I', ao)
        ao += len(a)
    sec['ARENAS'] = ar + b''.join(arenas)
    out = b'CRD1' + struct.pack('<I', len(SECTIONS))
    o = head_len
    for s in SECTIONS:
        out += struct.pack('<I', o)
        o += len(sec[s])
        while o % 4:
            o += 1
            sec[s] += b'\0'
    for s in SECTIONS:
        out += sec[s]
    assert out[head_len:head_len + 4] == sec['IMGS'][:4]
    open(os.path.join(OUT, 'data.bin'), 'wb').write(out)
    print('data.bin', len(out), 'bytes;', len(P.imgs), 'pictures,', len(P.pals), 'palettes;',
          'IMGS', len(sec['IMGS']), 'ARENAS', len(sec['ARENAS']), 'FONTS', len(sec['FONTS']))

    # data.h
    h = ['/* made by tools/pack.py */', '#ifndef CR_DATA_H', '#define CR_DATA_H', '']
    for i, s in enumerate(SECTIONS):
        h.append('#define SEC_%s %d' % (s, i))
    h.append('')
    for n, i in sorted(P.names.items(), key=lambda t: t[1]):
        h.append('#define %s %d' % (n, i))
    h += ['#define FONT_SMALL 0', '#define FONT_MID 1', '#define FONT_BIG 2', '#define FONT_NUM 3', '#define NARENAS %d' % len(arenas),
          '', '#endif', '']
    open(os.path.join(OUT, 'data.h'), 'w').write('\n'.join(h))

    tables(chars, blds, projs, aeos, buffs, G, texts, cards, unit_names, proj_names, area_names, unit_set, proj_img,
           card_icon)


def tables(chars, blds, projs, aeos, buffs, G, texts, cards, unit_names, proj_names, area_names, unit_set, proj_img,
           card_icon):
    L = ['/* made by tools/pack.py from Clash Royale\'s assets/csv_logic: see tables.h */', '#include "tables.h"', '']
    uidx = {n: i for i, n in enumerate(unit_names)}
    pidx = {n: i for i, n in enumerate(proj_names)}
    aidx = {n: i for i, n in enumerate(area_names)}
    # sprite sets
    L.append('const SetDef SETS[NSETS] = {')
    for name, anims in SETS:
        parts = []
        for an in ('run', 'attack', 'idle'):
            a = anims.get(an) or anims['idle']
            parts.append('{%d, %d, %d, %d, %d}' % a)
        L.append('  {%s}, /* %s */' % (', '.join(parts), name))
    L += ['};', '']
    # units
    L.append('const UnitDef UNITS[NUNITS] = {')
    for n in unit_names:
        u = chars.get(n) or blds[n]
        isb = n in blds
        fl = []
        if u.get('AttacksGround') == 'true':
            fl.append('U_GROUND')
        if u.get('AttacksAir') == 'true':
            fl.append('U_AIR')
        if u.get('TargetOnlyBuildings') == 'true':
            fl.append('U_BUILDINGS')
        if u.get('FlyingHeight'):
            fl.append('U_FLY')
        if u.get('IgnorePushback') == 'true' or isb:
            fl.append('U_NOPUSH')
        if u.get('SelfAsAoeCenter') == 'true':
            fl.append('U_SELFAOE')
        if u.get('JumpEnabled') == 'true':
            fl.append('U_JUMP')
        if isb:
            fl.append('U_BUILDING')
        if u.get('HidesWhenNotAttacking') == 'true':
            fl.append('U_HIDES')
        if n in ('KingTower', 'PrincessTower'):
            fl.append('U_TOWER')
        if n == 'KingTower':
            fl.append('U_KING')
        if n in ('GiantSkeletonBomb', 'BalloonBomb'):
            fl.append('U_BOMB')
        p = u.get('Projectile')
        dmg = num(u, 'Damage')
        title = texts.get(u.get('TID', ''), n)
        sl = num(u, 'SpawnCharacterLevelIndex')
        top = -1
        if n == 'PrincessTower':
            top = unit_set['PrincessTowerTop']
        if n == 'BombTower':
            top = unit_set['BombTowerTop']
        L.append('  {"%s", %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %s,' % (
            title, num(u, 'Hitpoints'), dmg, num(u, 'HitSpeed'), num(u, 'LoadTime'), num(u, 'Range'),
            num(u, 'MinimumRange'), num(u, 'SightRange'), num(u, 'Speed'), num(u, 'DeployTime'),
            num(u, 'CollisionRadius'), num(u, 'Mass'), pidx.get(p, 255) if p else 255, num(u, 'AreaDamageRadius'),
            num(u, 'LifeTime'), ' | '.join(fl) or '0'))
        L.append('   %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d, %d,'
                 ' %d, %d, %d, %d, %d}, /* %s */' % (
                     num(u, 'DeathDamage'), num(u, 'DeathDamageRadius'), num(u, 'DeathPushBack'),
                     uidx.get(u.get('DeathSpawnCharacter'), 255), num(u, 'DeathSpawnCount'),
                     uidx.get(u.get('SpawnCharacter'), 255), num(u, 'SpawnNumber'), sl,
                     num(u, 'SpawnInterval'), num(u, 'SpawnPauseTime'), num(u, 'SpawnStartTime'),
                     num(u, 'ChargeRange'), num(u, 'DamageSpecial'), num(u, 'ChargeSpeedMultiplier'),
                     num(u, 'VariableDamage2'), num(u, 'VariableDamageTime1'), num(u, 'VariableDamage3'),
                     num(u, 'VariableDamageTime2'), num(u, 'ManaGenerateTimeMs'), num(u, 'ManaCollectAmount'),
                     num(u, 'RandomDeployDelay'), num(u, 'FlyingHeight'), num(u, 'HideTimeMs'), num(u, 'UpTimeMs'),
                     unit_set.get(n, 0), num(u, 'ShadowScaleX'), num(u, 'ShadowScaleY'), num(u, 'ShadowX'),
                     num(u, 'ShadowY'), uidx.get(u.get('AttachedCharacter'), 255), num(u, 'AttachedCharacterHeight'),
                     top, n))
    L += ['};', '']
    # projectiles
    L.append('const ProjDef PROJS[NPROJS] = {')
    for n in proj_names:
        p = projs[n]
        fl = []
        if p.get('Homing') == 'true':
            fl.append('P_HOMING')
        if p.get('AoeToGround') == 'true':
            fl.append('P_AOE_GROUND')
        if p.get('AoeToAir') == 'true':
            fl.append('P_AOE_AIR')
        if p.get('ReducedTowerDamage', '').upper() == 'TRUE':
            fl.append('P_REDUCED')
        e = p.get('ExportName', '')
        if e in ('projectile_arrow_basic_enemy', 'projectile_arrow_basic'):
            fl.append('P_ARROW')
        if e == 'projectile_spear_360':
            fl.append('P_SPEAR')
        L.append('  {%d, %d, %d, %d, %s, %d, %d, %d, %d, %d, %d}, /* %s */' % (
            num(p, 'Speed'), num(p, 'Damage'), num(p, 'Radius'), num(p, 'Pushback'), ' | '.join(fl) or '0',
            uidx.get(p.get('SpawnCharacter'), 255), num(p, 'SpawnCharacterCount'), num(p, 'SpawnCharacterLevelIndex'),
            num(p, 'SpawnCharacterDeployTime'), num(p, 'Gravity'), proj_img.get(e, -1), n))
    L += ['};', '']
    # area effects
    L.append('const AreaDef AREAS[NAREAS] = {')
    for n in area_names:
        a = aeos[n]
        fl = []
        if a.get('OnlyEnemies', '').upper() == 'TRUE':
            fl.append('A_ENEMIES')
        if a.get('OnlyOwnTroops', '').upper() == 'TRUE':
            fl.append('A_OWN')
        if a.get('HitBiggestTargets', '').upper() == 'TRUE':
            fl.append('A_BIGGEST')
        if a.get('ReducedTowerDamage', '').upper() == 'TRUE':
            fl.append('A_REDUCED')
        if a.get('HitsGround', '').upper() == 'TRUE':
            fl.append('A_GROUND')
        if a.get('HitsAir', '').upper() == 'TRUE':
            fl.append('A_AIR')
        b = buffs.get(a.get('Buff', ''), {})
        L.append('  {%d, %d, %d, %d, %d, %s, %d, %d, %d, %d, %d, %d}, /* %s */' % (
            num(a, 'Radius'), num(a, 'LifeDuration'), num(a, 'LifeDurationIncreasePerLevel'), num(a, 'HitSpeed'),
            num(a, 'Damage'), ' | '.join(fl) or '0', num(a, 'BuffTime'), num(a, 'BuffTimeIncreasePerLevel'),
            pidx.get(a.get('Projectile'), 255), num(b, 'SpeedMultiplier'), num(b, 'HitSpeedMultiplier'),
            num(b, 'SpawnSpeedMultiplier'), n))
    L += ['};', '']
    # cards
    rar = {'Common': 0, 'Rare': 1, 'Epic': 2, 'Legendary': 3}
    L.append('const CardDef CARDS[NCARDS] = {')
    for kind, n, c in cards:
        title = texts.get(c.get('TID', ''), n)
        info = texts.get(c.get('TID_INFO', ''), '').replace('\\q', "'").replace('\\n', ' ').replace('"', "'")
        radius = num(c, 'Radius')
        if not radius and c.get('Projectile'):
            radius = num(projs[c.get('CustomFirstProjectile') or c['Projectile']], 'Radius')
        if not radius and c.get('AreaEffectObject'):
            radius = num(aeos[c['AreaEffectObject']], 'Radius')
        L.append('  {"%s", "%s", %d, %d, %d, %d, %d, %d, %d, %d, %d, %d}, /* %s */' % (
            title, info, rar[c['Rarity']], num(c, 'ManaCost'), kind, num(c, 'SummonNumber', 1) or 1,
            uidx.get(c.get('SummonCharacter'), 255), pidx.get(c.get('CustomFirstProjectile') or c.get('Projectile'), 255),
            aidx.get(c.get('AreaEffectObject'), 255), radius, num(c, 'MultipleProjectiles'), card_icon[n], n))
    L += ['};', '']
    # the training camp's decks (predefined_decks.csv) of eight cards: the bot's
    cidx = {n: i for i, (kind, n, c) in enumerate(cards)}
    rows = list(csv.reader(io.StringIO(APKREF[0].read('csv_logic/predefined_decks.csv').decode('utf-8'))))
    decks, cur = [], None
    for r in rows[2:]:
        if r and r[0]:
            cur = [r[0], []]
            decks.append(cur)
        if r and len(r) > 1 and r[1] and cur:
            cur[1].append(r[1])
    bot = [(n, d) for n, d in decks if len(d) == 8 and all(c in cidx for c in d)]
    L.append('const uint8_t BOT_DECKS[NBOTDECKS][8] = {')
    for n, d in bot:
        L.append('  {%s}, /* %s */' % (', '.join('CA_%s' % cname(c) for c in d), n))
    L += ['};', '']
    BOTN[0] = len(bot)
    # globals
    L.append('const int32_t GLOBALS[NGLOBALS] = {')
    for k in GLOBAL_NAMES:
        L.append('  %s, /* %s */' % (G.get(k) or 0, k))
    L += ['};', '']
    open(os.path.join(OUT, 'tables.c'), 'w').write('\n'.join(L))
    # counts and ids
    H = ['/* made by tools/pack.py */', '#ifndef CR_IDS_H', '#define CR_IDS_H',
         '#define NSETS %d' % len(SETS), '#define NUNITS %d' % len(unit_names), '#define NPROJS %d' % len(proj_names),
         '#define NAREAS %d' % len(area_names), '#define NCARDS %d' % len(cards), '#define NGLOBALS %d' % len(GLOBAL_NAMES),
         '#define NBOTDECKS %d' % BOTN[0]]
    for i, n in enumerate(unit_names):
        H.append('#define UN_%s %d' % (cname(n), i))
    for i, n in enumerate(proj_names):
        H.append('#define PR_%s %d' % (cname(n), i))
    for i, n in enumerate(area_names):
        H.append('#define AR_%s %d' % (cname(n), i))
    for i, (kind, n, c) in enumerate(cards):
        H.append('#define CA_%s %d' % (cname(n), i))
    for i, (name, a) in enumerate(SETS):
        H.append('#define SET_%s %d' % (cname(name), i))
    for i, k in enumerate(GLOBAL_NAMES):
        H.append('#define G_%s %d' % (k, i))
    H += ['#endif', '']
    open(os.path.join(OUT, 'ids.h'), 'w').write('\n'.join(H))


GLOBAL_NAMES = ['MAX_MANA', 'MANA_REGEN_MS', 'MANA_REGEN_MS_END', 'MANA_REGEN_MS_OVERTIME',
                'MANA_SPEED_UP_WHEN_REMAINING_SECONDS', 'START_MANA', 'NEXT_SPELL_COOLDOWN_MILLISECONDS',
                'NEXT_SPELL_COOLDOWN_MILLISECONDS_BOOST', 'NEXT_SPELL_COOLDOWN_MILLISECONDS_OVERTIME',
                'DAMAGE_INCREASE_PERCENT_PER_SPELL_LEVEL', 'HITPOINT_INCREASE_PERCENT_PER_SPELL_LEVEL',
                'DAMAGE_INCREASE_PERCENT_PER_KING_LEVEL', 'HITPOINT_INCREASE_PERCENT_PER_KING_LEVEL',
                'DAMAGE_INCREASE_PERCENT_PER_TOWER_LEVEL', 'HITPOINT_INCREASE_PERCENT_PER_TOWER_LEVEL',
                'MELEE_RANGE_LIMIT', 'REDUCED_TOWER_DAMAGE_PERCENT', 'KING_ACTIVATE_TIME_MS',
                'TOURNAMENT_MAX_EXP_LEVEL', 'TOURNAMENT_MAX_COMMON_LEVEL', 'TOURNAMENT_MAX_RARE_LEVEL',
                'TOURNAMENT_MAX_EPIC_LEVEL', 'TOURNAMENT_MATCH_LENGTH_SECONDS', 'MATCH_LENGTH', 'OVERTIME_SECONDS']

if __name__ == '__main__':
    main()
