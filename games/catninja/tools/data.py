#!/usr/bin/env python3
"""Cat Ninja's data, made from the game's own files: src/data.c and src/data.h.

    python3 -I tools/data.py GAMESRC src

GAMESRC holds what JPEXS took out of CatNinja.swf (see its spec/): levels/ (the 30 levels and the menu,
unzipped from Textures_levelsclass), ex/loader/image/ (the pictures) and ex/loader/font/ (its two fonts).

What it writes:
- one palette of 255 colours (with 8 levels of see-through) for every picture, so the game can tint the
  map (the original's ColourChanger) by changing 255 colours a frame;
- the pictures at half size (the calculator shows the original's 20 pixel tiles 10 pixels wide), turned
  and flipped as Textures.initiate() does; the TV screen at 0.8 and the big crystal at a quarter;
- the 31 maps, their tiles packed with DEFLATE, and their scripts turned into numbered steps;
- every text: the TV screens', the messages, the title cards, in runs of one font, size and colour;
- the two fonts ("square" and "cubic") at the sizes those texts use, only the letters they use.
"""
import os
import re
import sys
import zlib

import numpy as np
from PIL import Image, ImageDraw, ImageFont

SRC = os.path.expanduser(sys.argv[1] if len(sys.argv) > 1 else '~/Documents/Assets/gamesrc/catninja')
OUT = sys.argv[2] if len(sys.argv) > 2 else 'src'
IMG = os.path.join(SRC, 'ex/loader/image')
FONTS = os.path.join(SRC, 'ex/loader/font')
LEVELS = os.path.join(SRC, 'levels')


# ---------------------------------------------------------------- pictures
def tex(name):
    for f in os.listdir(IMG):
        if f.endswith('_' + name + '.png') or f.endswith('Textures_' + name + 'class.png'):
            return Image.open(os.path.join(IMG, f)).convert('RGBA')
    raise KeyError(name)


def rot(im, deg):  # PixelModifier.rotate: degrees clockwise on the screen
    return im.transpose(Image.ROTATE_270 if deg == 90 else Image.ROTATE_90)


def flip(im, sx, sy):
    if sx < 0:
        im = im.transpose(Image.FLIP_LEFT_RIGHT)
    if sy < 0:
        im = im.transpose(Image.FLIP_TOP_BOTTOM)
    return im


def shrink(im, w, h):
    """Area-averaged resize with the colours weighted by their alpha."""
    a = np.asarray(im, dtype=np.float64) / 255.0
    pm = a.copy()
    pm[..., :3] *= pm[..., 3:4]
    src = Image.fromarray((pm * 255).round().astype(np.uint8), 'RGBA')
    r = np.asarray(src.resize((w, h), Image.BOX), dtype=np.float64) / 255.0
    al = r[..., 3:4]
    rgb = np.where(al > 0, r[..., :3] / np.maximum(al, 1e-9), 0)
    out = np.concatenate([np.clip(rgb, 0, 1), al], axis=2)
    return Image.fromarray((out * 255).round().astype(np.uint8), 'RGBA')


def half(im):
    w, h = im.size
    if w % 2 or h % 2:
        p = Image.new('RGBA', (w + w % 2, h + h % 2), (0, 0, 0, 0))
        p.paste(im, (0, 0))
        im = p
    return shrink(im, im.width // 2, im.height // 2)


def sheet(im, w, h):
    return [im.crop((x, y, x + w, y + h)) for y in range(0, im.height // h * h, h) for x in range(0, im.width // w * w, w)]


# the tile pictures by tile type (Map.changeTile); the grid layer's are drawn untinted, the map's tinted
spike, blood = tex('spike'), tex('spikeblood')
lm, lmo, laser = tex('lasermachine'), tex('lasermachineoff'), tex('laser')
gb, bp, sticky = tex('gravityblock'), tex('buttonpress'), tex('stickygroundtile')
gc, sc, corner = tex('gravitychanger'), tex('spikecorner'), tex('groundtilecorner')
spring, tele, magnet, boost = tex('spring'), tex('teleporter'), tex('magnet'), tex('boost')
FG = {
    1: tex('groundtile'), 3: tex('darkgroundtile'), 4: tex('middarkgroundtile'),
    2: spike, 5: flip(spike, 1, -1), 6: rot(spike, 90), 7: rot(spike, -90),
    10: lm, 11: rot(lm, -90), 12: rot(lm, 90), 13: flip(lm, 1, -1),
    14: laser, 15: rot(laser, 90),
    20: lmo, 21: rot(lmo, -90), 22: rot(lmo, 90), 23: flip(lmo, 1, -1),
    24: gb, 25: rot(gb, 90), 26: rot(gb, -90), 27: flip(gb, 1, -1),
    28: bp, 29: rot(bp, 90), 30: rot(bp, -90), 31: flip(bp, 1, -1),
    36: sticky, 37: flip(sticky, -1, 1), 38: rot(sticky, -90), 39: rot(sticky, 90),
    40: gc, 41: rot(gc, 90), 42: rot(gc, -90), 43: flip(gc, 1, -1),
    44: tex('jetpack'),
    47: sc, 48: rot(sc, 90), 49: rot(sc, -90), 50: flip(sc, 1, -1),
    55: corner, 56: flip(corner, -1, 1), 57: rot(corner, -90), 58: flip(corner, -1, -1),
    59: spring, 60: rot(spring, 90), 61: rot(spring, -90), 62: flip(spring, 1, -1),
    63: tele, 64: rot(tele, 90), 65: rot(tele, -90), 66: flip(tele, 1, -1),
    75: magnet, 76: rot(magnet, 90), 77: rot(magnet, -90), 78: flip(magnet, 1, -1),
    79: tex('checkpointoff'), 80: tex('checkpointon'), 82: tex('speedup'), 83: tex('hallucinate'),
    89: tex('groundtile'), 90: tex('jailcell'),
}
BLOOD = {2: blood, 5: flip(blood, 1, -1), 6: rot(blood, 90), 7: rot(blood, -90)}
def over(a, b):  # b drawn over a (copyPixels with mergeAlpha)
    out = a.copy()
    out.alpha_composite(b)
    return out


gridtile = tex('gridtile')
GRID = {0: gridtile, 32: over(gridtile, tex('glass')), 51: over(gridtile, flip(boost, 1, -1)),
        52: over(gridtile, rot(boost, -90)), 53: over(gridtile, rot(boost, 90)), 54: over(gridtile, boost)}

cat = {k: tex('character' + k) for k in ('idle', 'idle2', 'walk1', 'walk2', 'jump', 'wallslide', 'duck', 'roll')}
mine = {k: tex(k) for k in ('mineblinkoff', 'mineblinkon', 'mineoff')}
button = tex('button')
shield, homing = sheet(tex('shieldshooter'), 20, 20), sheet(tex('homingshooter'), 20, 20)
fall = tex('fallspike')


def shooter(frames, o):
    if o == 0:
        return frames
    if o == 3:
        return [flip(f, 1, -1) for f in frames]
    # (the sheet turned, flipped, then cut: the frames come in the turned sheet's order)
    whole = Image.new('RGBA', (80, 20))
    for i, f in enumerate(frames):
        whole.paste(f, (i * 20, 0))
    t = flip(rot(whole, -90), 1, -1) if o == 1 else flip(rot(whole, -90), -1, -1)
    return sheet(t, 20, 20)


SPRITES = []  # (name, image at its size on the calculator)


def sprite(name, im):
    SPRITES.append((name, im))


for k in ('idle', 'idle2', 'walk1', 'walk2', 'jump', 'wallslide', 'duck', 'roll'):
    sprite('cat_' + k, half(cat[k]))
sprite('jetpackaddon', half(tex('jetpackaddon')))
for o, f in enumerate((lambda m: m, lambda m: flip(m, 1, -1), lambda m: rot(m, -90), lambda m: rot(m, 90))):
    for k in ('mineblinkoff', 'mineblinkon', 'mineoff'):
        sprite('%s_%d' % (k, o), half(f(mine[k])))
for i, b in enumerate((button, rot(button, 90), rot(button, -90), flip(button, 1, -1))):
    sprite('button_%d' % i, half(b))
for i, d in enumerate(sheet(tex('dirt'), 20, 20)):
    sprite('dirt_%d' % i, half(d))
sprite('crate', half(tex('crate')))
for kind, frames in (('shield', shield), ('homing', homing)):
    for o in range(4):
        for i, f in enumerate(shooter(frames, o)):
            sprite('%s_%d_%d' % (kind, o, i), half(f))
for i, f in enumerate((fall, flip(fall, 1, -1), rot(fall, 90), rot(fall, -90))):
    sprite('fallspike_%d' % i, half(f))
sprite('sunspike', half(tex('sunspike')))
sprite('bomb', half(tex('bomb')))
for i, f in enumerate(sheet(tex('sphere'), 20, 20)):
    sprite('sphere_%d' % i, half(f))
for i, f in enumerate(sheet(tex('bigsphere'), 167, 163)):
    sprite('bigsphere_%d' % i, shrink(f, 42, 41))
sprite('tv', shrink(tex('screen'), 96, 48))
logo = tex('Preloader_biglogoclass')
sprite('trackmill', shrink(logo, 192, 52))

TILES = {('fg', t): half(im) for t, im in FG.items()}
TILES.update({('blood', t): half(im) for t, im in BLOOD.items()})
TILES.update({('grid', t): half(im) for t, im in GRID.items()})

# ---------------------------------------------------------------- the palette
ALPHA = 8  # levels of see-through


def alpha_level(a):
    return int(round(int(a) * ALPHA / 255))


all_imgs = [im for im in TILES.values()] + [im for _, im in SPRITES]
rgb_list = []
for im in all_imgs:
    a = np.asarray(im)
    m = a[..., 3] > 255 // (2 * ALPHA)
    rgb_list.append(a[m][:, :3])
rgb_all = np.concatenate(rgb_list)


def build_palette(k):
    flat = Image.fromarray(rgb_all.reshape(1, -1, 3).astype(np.uint8), 'RGB')
    q = flat.quantize(colors=k, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = np.array(q.getpalette()[:3 * k], dtype=np.int32).reshape(-1, 3)
    return pal


def nearest(pal, rgb):
    d = ((rgb[:, None, :].astype(np.int32) - pal[None, :, :]) ** 2).sum(axis=2)
    return d.argmin(axis=1)


for k in range(255, 40, -5):
    pal = build_palette(k)
    pairs = set()
    for im in all_imgs:
        a = np.asarray(im)
        flat = a.reshape(-1, 4)
        lv = np.array([alpha_level(x) for x in flat[:, 3]])
        m = lv > 0
        if m.any():
            idx = nearest(pal, flat[m][:, :3])
            pairs.update(zip(idx.tolist(), lv[m].tolist()))
    if len(pairs) <= 255:
        break
pairs = sorted(pairs, key=lambda p: (p[1] != ALPHA, p))
PAIR = {p: i + 1 for i, p in enumerate(pairs)}
PAL = [(0, 0, 0, 0)] + [(int(pal[c][0]), int(pal[c][1]), int(pal[c][2]), lv) for c, lv in pairs]


def indexed(im):
    a = np.asarray(im)
    flat = a.reshape(-1, 4)
    out = np.zeros(len(flat), dtype=np.uint8)
    lv = np.array([alpha_level(x) for x in flat[:, 3]])
    m = lv > 0
    if m.any():
        idx = nearest(pal, flat[m][:, :3])
        out[m] = [PAIR[(c, l)] for c, l in zip(idx.tolist(), lv[m].tolist())]
    return out


# ---------------------------------------------------------------- fonts and texts
FONT_FILES = {'square': '10_Preloader_squarefont_square.ttf', 'cubic': '6_Preloader_cubicfont_cubic.ttf'}
FONT_SIZES = []  # (face, pixels): a font of the game
COLOURS = [0x000000, 0xFFFFFF]  # the texts' colours
used_chars = {}


def font_id(face, px):
    px = max(8, int(round(px)))
    key = (face, px)
    if key not in FONT_SIZES:
        FONT_SIZES.append(key)
    return FONT_SIZES.index(key)


def colour_id(c):
    if c not in COLOURS:
        COLOURS.append(c)
    return COLOURS.index(c)


def runs_of_html(html, face, scale, size=12, colour=0x000000):
    """The text's runs (font, colour, text) and line breaks, from Flash's htmlText (font tags, <br>)."""
    out = []
    stack = [(size, colour)]
    pos = 0
    for m in re.finditer(r"<font([^>]*)>|</font>|<br>", html):
        if m.start() > pos:
            out.append((stack[-1], html[pos:m.start()]))
        pos = m.end()
        tag = m.group(0)
        if tag == '<br>':
            out.append((None, '\n'))
        elif tag == '</font>':
            if len(stack) > 1:
                stack.pop()
        else:
            s, c = stack[-1]
            a = m.group(1)
            ms = re.search(r"size\s*=\s*'(\d+)'", a)
            mc = re.search(r"color\s*=\s*'#([0-9A-Fa-f]{6})'", a)
            if ms:
                s = int(ms.group(1))
            if mc:
                c = int(mc.group(1), 16)
            stack.append((s, c))
    if pos < len(html):
        out.append((stack[-1], html[pos:]))
    data = bytearray()
    cur = None
    for sc, t in out:
        if sc is None:
            data += b'\n'
            continue
        t = t.replace('&apos;', "'").replace('&amp;', '&').replace('&gt;', '>').replace('&lt;', '<')
        if not t:
            continue
        s, c = sc
        f = font_id(face, s * scale)
        key = (f, colour_id(c))
        if key != cur:
            data += bytes([1, key[0], 2, key[1]])
            cur = key
        used_chars.setdefault(f, set()).update(t)
        data += t.encode('latin-1')
    return bytes(data)


TEXTS = []  # (name, bytes)


def text(name, html, face, scale, size=12, colour=0x000000):
    TEXTS.append((name, runs_of_html(html, face, scale, size, colour)))
    return len(TEXTS) - 1


# Screen.updateDisplay: size[s|text] then colour[#rgb|text] become font tags, then the rest
def get_param(s, key, kind):
    out = ''
    while True:
        i = s.find(key)
        if i == -1:
            break
        out += s[:i]
        s = s[i:]
        parts = s[len(key):s.find(']')].split('|')
        s = s[s.find(']') + 1:]
        if kind == 0:
            out += "<font size = '" + parts[0] + "'>" + (parts[1] if len(parts) > 1 else 'undefined') + '</font>'
        else:
            out += "<font color = '" + parts[0] + "'>" + (parts[1] if len(parts) > 1 else 'undefined') + '</font>'
    return out + s


def screen_html(s):
    s = get_param(s, 'size[', 0)
    s = get_param(s, 'colour[', 1)
    s = s.replace('&[newline]', '<br>', 1).replace('${greaterthan}', '>', 1).replace('${lessthan}', '<', 1)
    return s


TV_SCALE = 0.8  # the TV screens are drawn at 0.8 of the original (the map at 0.5): readable
UI_SCALE = 0.4  # 800 x 600 on 320 x 240
KEY_WORDS = {  # what the screens say about the computer's keys, said for the calculator's
    'Press space to toggle the jetpack': 'Press shift to toggle the jetpack',
    'Pressing colour[#53C5FB|R] saves the day.': 'Pressing colour[#53C5FB|backspace] saves the day.',
    'colour[#0E60B1|R] might come in handy now.': 'colour[#0E60B1|Backspace] might come in handy now.',
    'Use the colour[#971CC6|arrow keys] to move.': 'Use the colour[#971CC6|arrow keys] to move.',
}

# the overlay texts of the code (TextManager, ScreenManager, Boss, BigSphere)
SPEC = {
    'spec1': "<font color='#E473EE' size='80'>Cat </font><font color='#1C6FD5' size='80'>Ninja:</font><br>"
             "<font color='#B95BEC' size='25'> The search for the </font><br><font color='#12DFF5' size='50'>magical</font>"
             "<font color='#3696E4' size='50'> energy </font><font color='#DB05E0' size='50'>crystals</font>",
    'spec2': "<font color='#1E84EA' size='35'> Programming </font><font color='#FFFFFF' size='25'> and </font>"
             "<font color='#2FC5F0' size='35'> Artwork </font><font color='#FFFFFF' size='25'> by </font><br>"
             "<font color='#BD55F4' size='40'>Richman Stewart</font>",
    'spec3': "<font color='#FFFFFF' size='25'>  In association with </font>",
    'spec4': "<font color='#CE4DCB' size='35'> Music </font><font color='#FFFFFF' size='25'> by </font>"
             "<font color='#D579F2' size='45'> BunnyMajs </font><font color='#FFFFFF' size='25'> and </font>"
             "<font color='#3379E3' size='45'> Eric Skiff </font>",
}
SPEC_IDS = {k: text(k, v, 'cubic', UI_SCALE) for k, v in SPEC.items()}
RESPAWN = ["It's a shame you feel pain", 'How did you die to that?', 'You are bad and you should feel bad',
           'That must have hurt', 'Where did your paws go?<br>Oh, found them.', 'Wow, you are a really bad kitty',
           'Come on, you really suck at this game.', 'Stupid kitty.', 'Silly kitty.']
IDLE = ["HEY. Don't leave me", 'Where did you go?', "Don't you dare leave me.", "Help, don't leave me!"]
RESPAWN_IDS = [text('die%d' % i, "<font color='#FF0000' size='25'>" + s + '</font>', 'square', UI_SCALE)
               for i, s in enumerate(RESPAWN)]
IDLE_IDS = [text('idle%d' % i, "<font color='#2D95EA' size='40'>" + s + '</font>', 'square', UI_SCALE)
            for i, s in enumerate(IDLE)]
BOSS_ID = text('boss', "<font size='60' color='#ff0000'>Kitty must die!</font>", 'cubic', UI_SCALE)
END_GOOD = text('endgood', "<font size='40' color='#00ff00'>Goob job Catninja!</font><br>"
                "<font size='20' color='#99FB97'>You collected enough crystals to save the world... or something!</font>",
                'square', UI_SCALE)
END_BAD = text('endbad', "<font size='40' color='#00ff00'>Goob job Catninja!</font><br>"
               "<font size='20' color='#99FB97'>Unfortunately you did not collect enough crystals<br>"
               "to save the world or something. You must try again!</font>", 'square', UI_SCALE)
HUD_FONT = font_id('square', 8)  # (Info: 15 pixels, at 0.4 too small: the smallest that reads)
used_chars.setdefault(HUD_FONT, set()).update('Deaths: Crystals Collected0123456789')
HUD_COLOUR = colour_id(0xFFFFFF)

# ---------------------------------------------------------------- the maps and their scripts
OPS = ['NOP', 'PAUSECAM', 'STOPMOVE', 'PAUSE', 'SETSPAWN', 'DIE', 'SETROOM', 'FUNCTION', 'GOTO', 'SETTYPEPATH',
       'SETTYPE', 'CREATELASER', 'MOVECAM', 'RESUMECAM', 'RESUMEMOVE', 'WALK', 'SHAKE', 'UPDATESCREEN', 'RESETLEVEL',
       'TELEPORT', 'CREATETEXT', 'RUNSCRIPT', 'IF', 'ELSE', 'ENDIF', 'EXIT', 'STARTBOSS', 'REMOVEBOSS', 'LOOP',
       'ENDLOOP', 'JUMP', 'SCALE']
OP = {n: i for i, n in enumerate(OPS)}
LABELS = ['endfunction', 'elseif', 'endif', 'unlocklaser', 'respawn']
GOTO_NAMES = {'unlocklaser': 3, 'respawn': 4}
VALUES = {'x': -1, 'y': -2, 'coordx': -3, 'coordy': -4}
TESTS = ['equals', 'doesnotequal', 'isgreaterthan', 'islessthan', 'isgreaterthan=', 'islessthan=']


def as_int(s):  # AS3's int(String)
    s = s.strip()
    m = re.match(r'[-+]?(\d+(\.\d*)?|\.\d+)', s)
    return int(float(m.group(0))) if m else 0


def params_of(s, start, n, sep=','):
    end = s.find(')')
    p = s[start:end if end >= 0 else 0].split(sep)
    return p if len(p) >= n else []


def compile_line(s):
    """GlitchInterpreter.interperate's tests, in its order: the step and its numbers."""
    op, args = 'NOP', []

    def st(pre):
        return s[:len(pre)] == pre
    if st('talk('):
        pass
    elif st('pausecamera'):
        op = 'PAUSECAM'
    elif st('stopmovement'):
        op = 'STOPMOVE'
    elif st('pause('):
        p = params_of(s, 6, 1)
        if p:
            op, args = 'PAUSE', [as_int(p[0])]
    elif st('setspawn('):
        p = params_of(s, 9, 2)
        if p:
            op, args = 'SETSPAWN', [as_int(p[0]), as_int(p[1])]
    elif st('die('):
        op = 'DIE'
    elif st('setroom('):
        p = params_of(s, 8, 1)
        if p:
            op, args = 'SETROOM', [1 if p[0][1:-1] == 'gravityRoom' else 0 if p[0][1:-1] == 'default' else -1]
    elif st('function('):
        if params_of(s, 9, 1):
            op = 'FUNCTION'
    elif st('goto('):
        p = params_of(s, 5, 1)
        if p:
            op, args = 'GOTO', [GOTO_NAMES[p[0]]]
    elif st('settypepath('):
        p = params_of(s, 12, 6)
        if p:
            op, args = 'SETTYPEPATH', [as_int(x) for x in p[:5]] + [1 if p[3] != '' else 0]
    elif st('settype('):
        p = params_of(s, 8, 4)
        if p:
            op, args = 'SETTYPE', [as_int(x) for x in p[:3]] + [1 if p[3] != '' else 0]
    elif st('createlaser('):
        p = params_of(s, 12, 5)
        if p:
            op, args = 'CREATELASER', [as_int(x) for x in p[:5]]
    elif st('createobject('):
        pass  # (no level makes any)
    elif st('scale('):
        p = params_of(s, 6, 2)
        if p:
            op, args = 'SCALE', [as_int(p[0]), int(float(p[1]) * 100), int(float(p[2]) * 100)]
    elif st('movecamera'):
        p = params_of(s, 11, 2)
        if p:
            op, args = 'MOVECAM', [as_int(p[0]), as_int(p[1])]
    elif st('resumecamera'):
        op = 'RESUMECAM'
    elif st('resumemovement'):
        op = 'RESUMEMOVE'
    elif st('walk('):
        p = params_of(s, 5, 1)
        if p:
            op, args = 'WALK', [as_int(p[0]), as_int(p[1]) if len(p) > 1 else 0]
    elif st('shake('):
        p = params_of(s, 6, 1)
        if len(p) >= 2:
            op, args = 'SHAKE', [as_int(p[0]), as_int(p[1])]
    elif st('jump('):
        p = params_of(s, 5, 2)
        if p:
            op, args = 'JUMP', [as_int(p[0]), as_int(p[1])]
    elif st('updatescreen('):
        p = params_of(s, 13, 2)
        if p:
            t = p[1].replace('"', '').replace('${com}', ',', 1).replace('${app}', "'", 1)
            for a, b in KEY_WORDS.items():
                if a in t:
                    t = t.replace(a, b)
            tid = text('tv', screen_html(t), 'square', TV_SCALE)
            op, args = 'UPDATESCREEN', [as_int(p[0]), tid, as_int(p[2]) if len(p) >= 3 else 0]
    elif st('resetlevel'):
        op = 'RESETLEVEL'
    elif st('teleport('):
        p = params_of(s, 9, 4)
        if p:
            op, args = 'TELEPORT', [as_int(x) for x in p[:4]]
    elif st('createtext('):
        p = params_of(s, 11, 5)
        if p:
            name = re.search(r'spec\d', p[0]).group(0)
            fade = 1 if len(p) > 5 and float(p[5]) != 0 else 0
            op, args = 'CREATETEXT', [SPEC_IDS[name], as_int(p[1]), as_int(p[2]), int(round(float(p[3]) * 1000)),
                                      as_int(p[4]), fade, as_int(p[6]) if len(p) > 6 else 0]
    elif st('runscript('):
        p = params_of(s, 10, 1)
        if p:
            op, args = 'RUNSCRIPT', [as_int(p[0])]
    # (the second chain: "if " and its else)
    if st('if '):
        p = params_of(s, 4, 3, ' ')
        if p:
            # each test: what is compared (0 a number, 1 x, 2 y, 3 coordx, 4 coordy) and its number, the test,
            # the same for the other side, then 1 if "and" follows, 2 if "or" follows
            conds = []
            n = 0
            while n < len(p):
                a = p[n]
                t = p[n + 1] if n + 1 < len(p) else ''
                b = p[n + 2] if n + 2 < len(p) else ''
                link = 0
                if n + 3 < len(p) and p[n + 3] == 'and':
                    n += 1
                    link = 1
                elif n + 3 < len(p) and p[n + 3] == 'or':
                    n += 1
                    link = 2
                conds += [-VALUES[a] if a in VALUES else 0, 0 if a in VALUES else as_int(a), TESTS.index(t) if t in TESTS else 15,
                          -VALUES[b] if b in VALUES else 0, 0 if b in VALUES else as_int(b), link]
                n += 3
            op, args = 'IF', conds
    elif op == 'NOP':
        if st('else '):
            op = 'ELSE'
        elif st('endif'):
            op = 'ENDIF'
        elif st('exit'):
            op = 'EXIT'
        elif st('startboss'):
            op = 'STARTBOSS'
        elif st('removeboss'):
            op = 'REMOVEBOSS'
    if st('loop('):
        p = params_of(s, 5, 1)
        if p:
            op, args = 'LOOP', [as_int(p[0])]
    elif st('endloop'):
        op = 'ENDLOOP'
    labels = sum(1 << i for i, l in enumerate(LABELS) if l in s)
    return OP[op], labels, args


def parse_map(name):
    t = open(os.path.join(LEVELS, name), encoding='latin-1').read()
    m = re.search(r"width\s*=\s*'(\d+)'\s*height\s*=\s*'(\d+)'\s*tiles\s*=\s*'([^']*)'", t)
    w, h = int(m.group(1)), int(m.group(2))
    toks = m.group(3).replace('\r\n', '').replace('\n', '').replace('\r', '').split(',')
    tiles = bytearray(w * h)
    marks = [0] * 12  # Map.playerpoints: [1], [2], [3], [4]: x, y and the level the edge leads to
    has = [0, 0, 0, 0]
    spawn = (0, 0)
    for i in range(w * h):
        s = toks[i] if i < len(toks) else ''
        x, y = i % w, i // w
        v = 0
        if '|' in s:
            parts = s[1:-1].split('|')
            s = '[' + parts[0] + ']'
        else:
            parts = []
        if s in ('[1]', '[2]', '[3]', '[4]'):
            k = int(s[1]) - 1
            marks[k * 3] = x - 1 if k == 1 else x
            marks[k * 3 + 1] = y
            has[k] = 1
            if len(parts) >= 2:
                marks[k * 3 + 2] = int(parts[1])
        elif s == 's':
            spawn = (x, y)
        else:
            try:
                v = int(s)
            except ValueError:
                v = 0
            if v > 91 or v < 0:
                v = 0  # (517 and 642: nothing)
        tiles[i] = v
    pages = []
    for n in range(1, 11):
        ms = re.search(r"<script id\s*=\s*'%d'\s*data\s*=\s*'(.*?)'></script>" % n, t, re.S)
        if ms:
            data = ms.group(1).replace('$(ap)', "'", 1)
            pages.append([compile_line(l) for l in data.replace('\r\n', '\n').split('\n')])
    return w, h, bytes(tiles), marks, spawn, pages


MAPS = [parse_map('menu1.xml')] + [parse_map('level%d.xml' % i) for i in range(1, 31)]

# the crystals, in order (the save keeps a bit for each)
CRYSTALS = []
for li, (w, h, tiles, *_ ) in enumerate(MAPS):
    for i, v in enumerate(tiles):
        if v == 81:
            CRYSTALS.append((li - 1, i % w, i // w))

# ---------------------------------------------------------------- fonts
GLYPHS = []  # per font: (first, count, ascent, descent, glyphs, data)


def make_font(face, px, chars):
    f = ImageFont.truetype(os.path.join(FONTS, FONT_FILES[face]), px)
    asc, desc = f.getmetrics()
    chars = sorted(set(c for c in chars if 32 <= ord(c) < 127) | {' '})
    first, last = ord(chars[0]), ord(chars[-1])
    glyphs, data = [], bytearray()
    seen = {}
    for code in range(first, last + 1):
        c = chr(code)
        if c not in chars:
            glyphs.append((0, 0, 0, 0, 0, 0))
            continue
        adv = int(round(f.getlength(c)))
        pad = px * 2
        canvas = Image.new('L', (pad * 3, pad * 3), 0)
        ImageDraw.Draw(canvas).text((pad, pad * 2), c, font=f, fill=255, anchor='ls')
        box = canvas.getbbox()
        if not box:
            glyphs.append((0, 0, 0, 0, adv, 0))
            continue
        mask = canvas.crop(box)
        off = (box[0] - pad, box[1] - pad * 2)
        w, h = mask.size
        px4 = [min(15, (int(v) * 15 + 127) // 255) for v in np.asarray(mask).reshape(-1)]
        blob = bytearray()
        for i in range(0, len(px4), 2):
            blob.append(px4[i] | ((px4[i + 1] if i + 1 < len(px4) else 0) << 4))
        key = bytes(blob) + bytes([w, h])
        if key in seen:
            o = seen[key]
        else:
            o = len(data)
            seen[key] = o
            data += blob
        glyphs.append((w, h, off[0], off[1], adv, o))
    return first, last - first + 1, asc, desc, glyphs, bytes(data)


# ---------------------------------------------------------------- writing it out
def c_bytes(b, per=24):
    s = []
    for i in range(0, len(b), per):
        s.append('  ' + ','.join(str(x) for x in b[i:i + per]) + ',')
    return '\n'.join(s)


def c_ints(b, per=16):
    s = []
    for i in range(0, len(b), per):
        s.append('  ' + ','.join(str(x) for x in b[i:i + per]) + ',')
    return '\n'.join(s)


def main():
    os.makedirs(OUT, exist_ok=True)
    h, c = [], []
    h.append('/* Made by tools/data.py from the game\'s own files: do not edit. */')
    h.append('#ifndef CN_DATA_H\n#define CN_DATA_H\n#include <stdint.h>\n#include "font.h"')
    c.append('/* Made by tools/data.py from Cat Ninja\'s own files (CatNinja.swf): do not edit. */')
    c.append('#include "data.h"')
    # palette
    h.append('#define CN_ALPHA %d' % ALPHA)
    h.append('extern const uint32_t cn_pal[256]; /* 0xAARRGGBB, AA: 0 to CN_ALPHA */')
    c.append('const uint32_t cn_pal[256] = {')
    c.append(c_ints(['0x%02X%02X%02X%02X' % (p[3], p[0], p[1], p[2]) for p in PAL] + ['0'] * (256 - len(PAL)), 8))
    c.append('};')
    # tiles
    keys = list(TILES.keys())
    pix = bytearray()
    for k in keys:
        pix += bytes(indexed(TILES[k]))
    h.append('#define CN_TILE_PX 100 /* 10 x 10 */')
    h.append('extern const uint8_t cn_tiles[%d];' % len(pix))
    c.append('const uint8_t cn_tiles[%d] = {\n%s\n};' % (len(pix), c_bytes(pix)))
    for layer in ('fg', 'blood', 'grid'):
        tab = [255] * 92
        for i, k in enumerate(keys):
            if k[0] == layer:
                tab[k[1]] = i
        h.append('extern const uint8_t cn_%s_tile[92]; /* the picture (cn_tiles, 100 each) of a tile type, or 255 */' % layer)
        c.append('const uint8_t cn_%s_tile[92] = {\n%s\n};' % (layer, c_bytes(tab, 23)))
    # sprites
    spx = bytearray()
    rows = []
    for name, im in SPRITES:
        rows.append((name, im.width, im.height, len(spx)))
        spx += bytes(indexed(im))
    h.append('typedef struct {\n  uint8_t w, h;\n  uint32_t at;\n} cn_sprite_t;')
    h.append('enum {')
    for name, *_ in rows:
        h.append('  SPR_%s,' % name.upper())
    h.append('  SPR_COUNT\n};')
    h.append('extern const cn_sprite_t cn_sprites[SPR_COUNT];')
    h.append('extern const uint8_t cn_sprite_px[%d];' % len(spx))
    c.append('const cn_sprite_t cn_sprites[SPR_COUNT] = {')
    for name, w, hh, at in rows:
        c.append('  {%d, %d, %d}, /* %s */' % (w, hh, at, name))
    c.append('};')
    c.append('const uint8_t cn_sprite_px[%d] = {\n%s\n};' % (len(spx), c_bytes(spx)))
    # maps
    blobs = bytearray()
    h.append('#define CN_MAPS %d /* the menu, then the 30 levels */' % len(MAPS))
    h.append('typedef struct {\n  uint8_t w, h;\n  int16_t marks[12]; /* [1] to [4]: x, y, the level that edge leads to */\n'
             '  uint8_t has[4], sx, sy; /* which marks; the spawn (s) */\n  uint32_t at, len; /* the tiles, DEFLATE */\n'
             '  uint16_t script; /* into cn_scripts: pages, then each page\'s length and steps */\n} cn_map_t;')
    h.append('extern const cn_map_t cn_maps[CN_MAPS];')
    script = []
    rows = []
    biggest = 0
    for (w, hh, tiles, marks, spawn, pages) in MAPS:
        comp = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
        z = comp.compress(tiles) + comp.flush()
        biggest = max(biggest, w * hh)
        at = len(script)
        script.append(len(pages))
        for pg in pages:
            script.append(len(pg))
            for op, labels, args in pg:
                script += [op | labels << 8, len(args)] + args
        has = [1 if (marks[k * 3] or marks[k * 3 + 1]) else 0 for k in range(4)]
        rows.append((w, hh, marks, spawn, len(blobs), len(z), at))
        blobs += z
    c.append('const cn_map_t cn_maps[CN_MAPS] = {')
    for w, hh, marks, spawn, at, ln, sat in rows:
        c.append('  {%d, %d, {%s}, {0, 0, 0, 0}, %d, %d, %d, %d, %d},' % (w, hh, ','.join(map(str, marks)), spawn[0],
                                                                        spawn[1], at, ln, sat))
    c.append('};')
    h.append('#define CN_MAP_MAX %d /* the most tiles a map has */' % biggest)
    h.append('extern const uint8_t cn_map_z[%d];' % len(blobs))
    c.append('const uint8_t cn_map_z[%d] = {\n%s\n};' % (len(blobs), c_bytes(blobs)))
    h.append('enum {')
    for n in OPS:
        h.append('  OP_%s,' % n)
    h.append('};')
    h.append('enum { LB_ENDFUNCTION = 1, LB_ELSEIF = 2, LB_ENDIF = 4, LB_UNLOCKLASER = 8, LB_RESPAWN = 16 };')
    h.append('extern const int16_t cn_scripts[%d];' % len(script))
    c.append('const int16_t cn_scripts[%d] = {\n%s\n};' % (len(script), c_ints(script, 20)))
    # crystals
    h.append('#define CN_CRYSTALS %d' % len(CRYSTALS))
    h.append('extern const uint8_t cn_crystals[CN_CRYSTALS][3]; /* level, x, y */')
    c.append('const uint8_t cn_crystals[CN_CRYSTALS][3] = {%s};' % ', '.join('{%d, %d, %d}' % (l & 255, x, y)
                                                                         for l, x, y in CRYSTALS))
    # texts
    tdata = bytearray()
    starts = []
    for name, b in TEXTS:
        starts.append(len(tdata))
        tdata += b + b'\0'
    h.append('#define CN_TEXTS %d' % len(TEXTS))
    h.append('extern const uint16_t cn_text_at[CN_TEXTS]; /* runs: 1 font, 2 colour, \\n, letters */')
    h.append('extern const char cn_text_data[%d];' % len(tdata))
    c.append('const uint16_t cn_text_at[CN_TEXTS] = {\n%s\n};' % c_ints(starts))
    c.append('const char cn_text_data[%d] = {\n%s\n};' % (len(tdata), c_bytes(tdata)))
    ids = {'SPEC1': SPEC_IDS['spec1'], 'SPEC2': SPEC_IDS['spec2'], 'SPEC3': SPEC_IDS['spec3'], 'SPEC4': SPEC_IDS['spec4'],
           'DIE0': RESPAWN_IDS[0], 'IDLE0': IDLE_IDS[0], 'BOSS': BOSS_ID, 'ENDGOOD': END_GOOD, 'ENDBAD': END_BAD}
    for k, v in ids.items():
        h.append('#define TX_%s %d' % (k, v))
    h.append('#define TX_DIES %d\n#define TX_IDLES %d' % (len(RESPAWN), len(IDLE)))
    h.append('#define CN_COLOURS %d' % len(COLOURS))
    h.append('extern const uint32_t cn_colours[CN_COLOURS];')
    c.append('const uint32_t cn_colours[CN_COLOURS] = {%s};' % ', '.join('0x%06X' % x for x in COLOURS))
    # fonts
    h.append('#define CN_FONTS %d\n#define FONT_HUD %d\n#define COLOUR_WHITE %d' % (len(FONT_SIZES), HUD_FONT, HUD_COLOUR))
    h.append('extern const np_font_t cn_fonts[CN_FONTS];')
    fdefs = []
    for i, (face, px) in enumerate(FONT_SIZES):
        first, count, asc, desc, glyphs, data = make_font(face, px, used_chars.get(i, set()) | set(' '))
        c.append('/* %s, %d pixels */' % (face, px))
        c.append('static const uint8_t font%d_data[%d] = {\n%s\n};' % (i, max(1, len(data)), c_bytes(data or b'\0')))
        c.append('static const np_glyph_t font%d_glyphs[%d] = {' % (i, count))
        for g in glyphs:
            c.append('  {%d, %d, %d, %d, %d, %d},' % g)
        c.append('};')
        fdefs.append('  {%d, %d, %d, %d, font%d_glyphs, font%d_data},' % (first, count, asc, desc, i, i))
    c.append('const np_font_t cn_fonts[CN_FONTS] = {\n%s\n};' % '\n'.join(fdefs))
    h.append('#endif')
    open(os.path.join(OUT, 'data.h'), 'w').write('\n'.join(h) + '\n')
    open(os.path.join(OUT, 'data.c'), 'w').write('\n'.join(c) + '\n')
    print('palette %d colours, tiles %d B, sprites %d B, maps %d B, scripts %d steps, texts %d B, fonts %s'
          % (len(PAL), len(pix), len(spx), len(blobs), len(script), len(tdata), FONT_SIZES))


main()
