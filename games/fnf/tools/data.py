#!/usr/bin/env python3
"""Friday Night Funkin' for the calculator, from the game's own files: its charts, characters, stages, notes and
menus, packed into C tables (data.c and ids.h in OUT). The pictures and charts are the Funkin' Crew's and are not
in this repository: tools/fetch.py brings the files this reads (tools/ref.txt) from their funkin.assets repository.

  python3 -I tools/data.py REF OUT        REF: funkin.assets at the pinned commit (tools/fetch.py)
      [--list FILE]                       also write the files read, with their git blob hashes (tools/ref.txt)

Pictures are drawn at the calculator's scale, 16 colours each (one palette per character), 4 bits a pixel, and
each is DEFLATE-packed (zopfli when it's installed). The charts keep every note: times in microseconds, deltas in
LEB128, each chart DEFLATE-packed. Nothing in REF is run. Needs Python 3 and Pillow."""
import argparse
import hashlib
import json
import math
import os
import re
import struct
import sys
import zlib
from pathlib import Path

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import atlas  # noqa: E402
from atlas import Reference, bounds, flatten, multiply  # noqa: E402

import numpy as np  # noqa: E402
from PIL import Image  # noqa: E402

try:
    import zopfli.zlib as _zopfli
except ImportError:
    _zopfli = None

# ------------------------------------------------------------------------------------------------- what's played
LEVELS = ["tutorial", "week1", "week2", "week3", "week4", "week5", "week6", "week7", "weekend1", "sserafim"]
VARIATIONS = ["default", "erect", "pico", "bf"]
DIFFICULTIES = ["easy", "normal", "hard", "erect", "nightmare"]
# the instrumental's length (ms), measured from the game's own audio files (songs/<song>/Inst[-variation].ogg):
# the song ends with it, after its last note
INST_MS = {
    "2hot": 120000, "blammed": 106970, "blammed-erect": 107294, "blammed-pico": 106909, "blazin": 122667,
    "bopeebo": 81600, "bopeebo-erect": 113659, "bopeebo-pico": 84000, "cocoa": 112800, "cocoa-erect": 115862,
    "cocoa-pico": 122688, "dadbattle": 89361, "dadbattle-erect": 133263, "dadbattle-pico": 89361,
    "darnell": 126968, "darnell-bf": 126968, "darnell-erect": 149635, "eggnog": 95200, "eggnog-erect": 130282,
    "eggnog-pico": 96031, "fresh": 84000, "fresh-erect": 78720, "fresh-pico": 84099, "guns": 140473,
    "guns-pico": 140432, "high": 105665, "high-erect": 105600, "lit-up": 114546, "lit-up-bf": 135000,
    "milf": 124736, "monster": 177679, "philly-nice": 98058, "philly-nice-erect": 115200,
    "philly-nice-pico": 102857, "pico": 84871, "pico-erect": 109630, "pico-pico": 86400, "roses": 92000,
    "roses-erect": 99375, "roses-pico": 96005, "satin-panties": 96080, "satin-panties-erect": 113778,
    "senpai": 98292, "senpai-erect": 116952, "senpai-pico": 102869, "south": 88792, "south-erect": 118644,
    "south-pico": 90182, "spaghetti": 173587, "spookeez": 99180, "spookeez-erect": 141687, "spookeez-pico": 97600,
    "stress": 124009, "stress-pico": 124045, "thorns": 103579, "thorns-erect": 113684, "tutorial": 67200,
    "ugh": 86617, "ugh-erect": 83294, "ugh-pico": 85875, "winter-horrorland": 132830,
}
EVENTS = ["SetCameraBop", "PlayAnimation", "ScrollSpeed", "SetHealthIcon", "sserafimSing", "sserafimShow",
          "sserafimDark"]
EASES = ["linear", "INSTANT", "quad", "cube", "quart", "quint", "sine", "expo", "circ", "back", "elastic", "bounce",
         "smoothStep", "smootherStep"]
EASE_DIRS = ["In", "Out", "InOut"]

# ------------------------------------------------------------------------------------------------- the screen
SCREEN_W, SCREEN_H = 320, 240
HUD = 0.25            # 1280x720 -> 320 wide
CHAR_SCALE = 0.17     # characters: world units -> pixels
# the frames kept of each animation (sampled over it, each shown when the original shows it)
DANCE_FRAMES, SING_FRAMES, MISS_FRAMES, OTHER_FRAMES = 4, 2, 2, 2
ALPHA_CUT = 110       # below: transparent


def die(msg):
    sys.exit(f"data.py: {msg}")


# ------------------------------------------------------------------------------------------------- packing
def deflate(raw):
    if _zopfli:
        z = _zopfli.compress(raw, numiterations=15)
        return z[2:-4]   # (raw DEFLATE: no zlib header or checksum)
    return zlib.compress(raw, 9, wbits=-15)


class Blob:
    def __init__(self):
        self.data = bytearray()
        self.seen = {}

    def add(self, b):
        b = bytes(b)
        if b in self.seen:
            return self.seen[b]
        at = len(self.data)
        self.data += b
        self.seen[b] = at
        return at


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


class Pictures:
    """Every picture: 4-bit pixels (0 transparent), a palette of 15 colours, DEFLATE-packed."""

    def __init__(self, blob):
        self.blob = blob
        self.palettes = []
        self.sprites = []     # (w, h, x, y, palette, at, size)
        self.bytes = {}

    def palette(self, colors, opaque=False):
        """A palette: 15 colours after the transparent 0, or (opaque) 16 colours."""
        p = ([] if opaque else [0]) + [rgb565(*c) for c in colors][:16 if opaque else 15]
        p += [0] * (16 - len(p))
        if p not in self.palettes:
            self.palettes.append(p)
        return self.palettes.index(p)

    def add(self, idx, w, h, x, y, pal, group, opaque=False):
        """idx: bytes, one palette index per pixel (0 transparent, unless opaque)."""
        row = (w + 1) // 2
        raw = bytearray(row * h)
        for j in range(h):
            line = idx[j * w:(j + 1) * w]
            for i in range(0, w, 2):
                a = line[i]
                b = line[i + 1] if i + 1 < w else 0
                raw[j * row + i // 2] = a | b << 4
        z = deflate(bytes(raw))
        at = self.blob.add(z)
        self.bytes[group] = self.bytes.get(group, 0) + len(z)
        key = (w, h, x, y, pal, at, len(z))
        self.sprites.append(key)
        return len(self.sprites) - 1


def quantize(images, colors=15, transparent=True):
    """One palette for all the images: (palette [(r,g,b)], [index bytes per image]). Index 0 is transparent
    (alpha below ALPHA_CUT) when transparent, else the 16 colours are all used."""
    n = colors if transparent else 16
    pixels = []
    for im in images:
        a = im.getchannel("A").tobytes()
        rgb = im.convert("RGB").tobytes()
        pixels += [rgb[i * 3:i * 3 + 3] for i in range(im.width * im.height) if not transparent or a[i] >= ALPHA_CUT]
    if not pixels:
        pixels = [b"\0\0\0"]
    strip = Image.frombytes("RGB", (len(pixels), 1), b"".join(pixels))
    q = strip.quantize(colors=n, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = q.getpalette()[:n * 3]
    pal += [0] * (n * 3 - len(pal))
    colours = [tuple(pal[i * 3:i * 3 + 3]) for i in range(n)]
    lut = np.array(colours, dtype=np.int32)
    out = []
    for im in images:
        rgb = np.asarray(im.convert("RGB"), dtype=np.int32).reshape(-1, 3)
        d = ((rgb[:, None, :] - lut[None, :, :]) ** 2).sum(axis=2)
        q = d.argmin(axis=1).astype(np.uint8)   # (argmin: the first of equals)
        if transparent:
            a = np.asarray(im.getchannel("A")).reshape(-1)
            q = np.where(a >= ALPHA_CUT, q + 1, 0).astype(np.uint8)
        out.append(q.tobytes())
    return colours, out


# ------------------------------------------------------------------------------------------------- charts
def leb(v):
    assert v >= 0
    out = bytearray()
    while v >= 128:
        out.append(v & 127 | 128)
        v >>= 7
    out.append(v)
    return out


def us(ms):
    return int(round(float(ms) * 1000))


class Songs:
    def __init__(self, ref):
        self.ref = ref
        self.levels = [dict(id=l, **ref.json(f"preload/data/levels/{l}.json")) for l in LEVELS]
        self.order = [s for l in self.levels for s in l["songs"]]
        self.songs = []
        for song in self.order:
            variations = []
            for v in VARIATIONS:
                suffix = "" if v == "default" else "-" + v
                mp = ref.root / f"preload/data/songs/{song}/{song}-metadata{suffix}.json"
                if not mp.exists():
                    continue
                meta = ref.json(mp.relative_to(ref.root))
                chart = ref.json(f"preload/data/songs/{song}/{song}-chart{suffix}.json")
                variations.append(dict(kind=v, meta=meta, chart=chart))
            self.songs.append(dict(id=song, variations=variations,
                                   level=next(i for i, l in enumerate(self.levels) if song in l["songs"])))

    def characters(self):
        names = set()
        for s in self.songs:
            for v in s["variations"]:
                c = v["meta"]["playData"]["characters"]
                for k in ("player", "opponent", "girlfriend"):
                    if c.get(k):
                        names.add(c[k])
        names |= set(SSERAFIM_EXTRA)
        return sorted(names)

    def players(self):
        return {v["meta"]["playData"]["characters"].get("player") for s in self.songs for v in s["variations"]} - {None}

    def stages(self):
        return sorted({v["meta"]["playData"]["stage"] for s in self.songs for v in s["variations"]})


OWNED = {}   # the characters Boyfriend's and Pico's freeplay lists are for (data/players)
SSERAFIM_EXTRA = ["sserafim-yunjin", "sserafim-chaewon", "sserafim-eunchae"]
SSERAFIM_FEET = [(-621, 154), (687, 98), (770, 675)]

# note kinds: the animation each one plays instead of sing<DIR> (as the characters' own scripts do)
WEEKEND = {"blockhigh": "block", "blocklow": "block", "blockspin": "block", "cockgun": "cock", "dodgehigh": "dodge",
           "dodgelow": "duck", "fakeout": "fakeout", "firegun": "shoot", "hithigh": "hitHigh", "hitlow": "hitLow",
           "idle": "idle", "kickcan": "kickCan", "kneecan": "kneeCan", "lightcan": "lightCan",
           "picouppercut": "uppercut", "picouppercutprep": "uppercutPrep", "darnelluppercut": "uppercut",
           "darnelluppercutprep": "uppercutPrep", "punchhigh": "punchHigh1", "punchhighblocked": "punchHigh2",
           "punchhighspin": "punchHigh2", "punchlow": "punchLow1", "punchlowblocked": "punchLow2",
           "punchlowdodged": "punchLow2", "punchlowspin": "punchLow2", "taunt": "taunt"}
DIRS = ["LEFT", "DOWN", "UP", "RIGHT"]


def kind_anims(kind):
    """The four animation names a note kind plays (None: the plain sing<DIR>; "": none at all)."""
    if kind in ("", "non_scoreable"):
        return None
    if kind == "noanim":
        return [""] * 4
    if kind == "mom":
        return [f"sing{d}-alt" for d in DIRS]
    if kind.startswith("sakura-"):
        return [f"sing{d}-{kind[7:]}" for d in DIRS]
    if kind == "censor":
        return [f"sing{d}-censor" for d in DIRS]
    if kind in ("ugh", "hehPrettyGood"):
        return [kind] * 4
    if kind.startswith("weekend-1-"):
        return [WEEKEND.get(kind[10:], f"sing{d}") for d in DIRS]
    return None


# ------------------------------------------------------------------------------------------------- characters
class Characters:
    def __init__(self, ref, pics, names, wanted_anims, stage_scale, players):
        self.ref, self.pics = ref, pics
        self.players = players
        self.rows, self.anims, self.anim_names, self.kinds = [], [], [], []
        self.index = {n: i for i, n in enumerate(names)}
        self.icons, self.icon_rows = {}, []
        self.preview = {}
        self.name_ids = {}
        self.stage_scale = stage_scale
        self.world = {}
        for n in names:
            self.convert(n, wanted_anims.get(n, set()))

    def name_id(self, name):
        if name not in self.name_ids:
            self.name_ids[name] = len(self.name_ids)
        return self.name_ids[name]

    def icon(self, ident):
        """A health icon (its frames: normal, losing, winning), by its id."""
        if ident in self.icons:
            return self.icons[ident]
        path = self.ref.root / f"preload/images/icons/icon-{ident}.png"
        if not path.exists():
            ident = "face"
            path = self.ref.root / "preload/images/icons/icon-face.png"
            if ident in self.icons:
                return self.icons[ident]
        im = atlas.open_image(path)
        size = im.height
        pixel = size < 50
        frames = []
        n = max(1, min(3, im.width // size))
        target = round(150 * HUD)
        for i in range(n):
            f = im.crop((i * size, 0, (i + 1) * size, size))
            f = f.resize((target, target), Image.Resampling.NEAREST if pixel else Image.Resampling.LANCZOS)
            frames.append(f)
        colours, idx = quantize(frames)
        pal = self.pics.palette(colours)
        ids = []
        for f, ix in zip(frames, idx):
            b = f.getbbox() or (0, 0, 1, 1)
            ids.append(self.pics.add(crop_idx(ix, target, target, b), b[2] - b[0], b[3] - b[1], b[0] - target // 2,
                                     b[1] - target // 2, pal, "icons"))
        while len(ids) < 3:
            ids.append(ids[0])
        self.icons[ident] = len(self.icon_rows)
        self.icon_rows.append(ids)
        return self.icons[ident]

    def convert(self, name, wanted):
        cfg = self.ref.json(f"preload/data/characters/{name}.json")
        scale = float(cfg.get("scale", 1)) * self.stage_scale.get(name, 1.0)
        # mirrored as the stage puts it: the player the other way from its data (Stage.addCharacter), so Boyfriend
        # and Pico face their opponent
        flip = bool(cfg.get("flipX", False)) != (name in self.players)
        base = self.ref.path(cfg["assetPath"])
        anims = {a["name"]: a for a in cfg["animations"]}
        alternate = "danceLeft" in anims
        keep = (["danceLeft", "danceRight"] if alternate else ["idle"]) + [f"sing{d}" for d in DIRS] + \
            [f"sing{d}miss" for d in DIRS]
        keep += sorted(a for a in wanted if a in anims and a not in keep)
        picked = {}
        for a in keep:
            if a not in anims:
                continue
            spec = anims[a]
            path = self.ref.path(spec["assetPath"]) if spec.get("assetPath") else base
            count = DANCE_FRAMES if a in ("idle", "danceLeft", "danceRight") else \
                MISS_FRAMES if a.endswith("miss") else SING_FRAMES if a.startswith("sing") else OTHER_FRAMES
            try:
                _, length = self.ref.frames(path, spec, [0])
            except (KeyError, FileNotFoundError) as e:
                print(f"  {name}: no {a} ({e})", file=sys.stderr)
                continue
            count = min(count, length)
            picks = sorted({round(i * (length - 1) / max(1, count - 1)) for i in range(count)}) if count > 1 else [0]
            leaves, _ = self.ref.frames(path, spec, picks)
            ox, oy = spec.get("offsets", [0, 0])
            picked[a] = (picks, leaves, length, (ox, oy), spec.get("frameRate", 24), bool(spec.get("looped", False)))
        if not picked:
            die(f"{name}: no animation")
        idle = "danceLeft" if alternate else "idle"
        if idle not in picked:
            idle = next(iter(picked))
        box = bounds(picked[idle][1][0])
        cx, foot = (box[0] + box[2]) / 2, box[3]
        # the animations' offsets, from the idle one's: the idle frame stands on the anchor (its feet), the others
        # where the game puts them from there (the character's own global offsets only move them all together)
        iox, ioy = picked[idle][3]
        k = scale * CHAR_SCALE
        images = []
        for a, (picks, leaves, length, (ox, oy), fps, loop) in picked.items():
            for lv in leaves:
                if flip:   # mirrored round the idle frame's middle; the offsets are not
                    lv = [(im, multiply((-1, 0, 0, 1, 2 * cx, 0), m), al) for im, m, al in lv]
                lv = [(im, multiply((1, 0, 0, 1, -(ox - iox), -(oy - ioy)), m), al) for im, m, al in lv]
                pixel = cfg.get("isPixel", False)
                im, x, y = flatten(lv, k, resample=Image.Resampling.NEAREST if pixel else Image.Resampling.BICUBIC)
                b = im.getbbox() or (0, 0, 1, 1)
                images.append((a, im.crop(b), x + b[0] - round(cx * k), y + b[1] - round(foot * k)))
        colours, idx = quantize([im for _, im, _, _ in images])
        pal = self.pics.palette(colours)
        first = len(self.anims)
        self.preview[name] = images[0][1:]
        slots = {}
        it = iter(zip(images, idx))
        for a, (picks, leaves, length, offsets, fps, loop) in picked.items():
            sprite0 = None
            for _ in picks:
                (_, im, x, y), ix = next(it)
                s = self.pics.add(ix, im.width, im.height, x, y, pal, "characters")
                sprite0 = s if sprite0 is None else sprite0
            slots[a] = len(self.anims) - first
            flags = int(loop) | (a.startswith("sing") and not a.endswith("-end")) << 1 | a.endswith("miss") << 2 | \
                (a.startswith("dance") or a.startswith("idle")) << 3
            self.anims.append([sprite0, len(picks), min(255, length), int(fps), flags, picks + [0] * (6 - len(picks))])
            self.anim_names.append(self.name_id(a))
        slot_names = ["danceLeft" if alternate else "idle", "danceRight" if alternate else "idle"] + \
            [f"sing{d}" for d in DIRS] + [f"sing{d}miss" for d in DIRS]
        slot = [slots.get(s, 255) for s in slot_names]
        hi = cfg.get("healthIcon") or {}
        icon = self.icon(hi.get("id", name.split("-")[0] if not (self.ref.root / f"preload/images/icons/icon-{name}.png").exists() else name))
        dance = float(cfg.get("danceEvery", 1))
        sing = float(cfg.get("singTime", 8))
        owner = 4 if name in OWNED["bf"] else 8 if name in OWNED["pico"] else 0
        owner |= 16 if hi.get("flipX", False) else 0   # (its health icon faces the other way)
        self.rows.append(dict(name=name, owner=owner, anim=first, count=len(self.anims) - first, dance=round(dance * 4),
                              sing=min(255, round(sing)), alternate=alternate, slot=slot, icon=icon,
                              pixel=bool(cfg.get("isPixel", False))))
        self.world[name] = (box[2] - box[0]) * scale, (box[3] - box[1]) * scale
        print(f"  {name}: {len(picked)} animations, {len(images)} frames, {self.pics.bytes.get('characters', 0)} bytes so far",
              file=sys.stderr)

    def kind_rows(self, kinds):
        """(character, kind, four animations) for each note kind a character has its own animations for."""
        for ci, row in enumerate(self.rows):
            names = {self.anim_names[row["anim"] + i]: i for i in range(row["count"])}
            inv = {v: k for k, v in self.name_ids.items()}
            have = {inv[n]: i for n, i in names.items()}
            for ki, kind in enumerate(kinds):
                want = kind_anims(kind)
                if want is None:
                    continue
                anims = [254 if w == "" else have.get(w, 255) for w in want]
                if all(a == 255 for a in anims):
                    continue
                self.kinds.append((ci, ki, anims))


def crop_idx(idx, w, h, b):
    x0, y0, x1, y1 = b
    return bytes(idx[y * w + x] for y in range(y0, y1) for x in range(x0, x1))


# ------------------------------------------------------------------------------------------------- stages
class Stages:
    def __init__(self, ref, pics, names, chars, cast):
        self.ref, self.pics, self.rows = ref, pics, []
        for n in names:
            self.convert(n, chars, cast.get(n, []))

    def convert(self, name, chars, cast):
        """cast: the (player, opponent) pairs of the songs on this stage."""
        cfg = self.ref.json(f"preload/data/stages/{name}.json")
        roles = cfg.get("characters", {})
        pos = {r: roles.get(r, {}).get("position", d) for r, d in (("bf", [990, 885]), ("dad", [335, 885]),
                                                                       ("gf", [750, 787]))}
        z = {r: roles.get(r, {}).get("zIndex", d) for r, d in (("bf", 300), ("dad", 200), ("gf", 100))}
        # the view: both singers whole, their feet low on the screen
        xs = []
        for pair in cast:
            for who, role in zip(pair, ("bf", "dad")):
                if who in chars.world:
                    w, _ = chars.world[who]
                    xs += [pos[role][0] - w / 2, pos[role][0] + w / 2]
        if not xs:
            xs = [pos["dad"][0] - 200, pos["bf"][0] + 200]
        if name == "sserafim":
            xs += [x - 150 for x, _ in SSERAFIM_FEET] + [x + 150 for x, _ in SSERAFIM_FEET]
        span = max(1440, (max(xs) - min(xs)) * 1.12)
        s = SCREEN_W / span
        left = (max(xs) + min(xs)) / 2 - span / 2
        top = max(pos["bf"][1], pos["dad"][1]) - 205 / s
        view = (left, top, left + span, top + SCREEN_H / s)
        order = sorted(z, key=lambda r: (z[r], r))
        layers = [Image.new("RGBA", (SCREEN_W, SCREEN_H), (0, 0, 0, 0)) for _ in range(4)]
        for prop in sorted(cfg.get("props", []), key=lambda p: p.get("zIndex", 0)):
            path = prop.get("assetPath", "")
            if not path or prop.get("alpha", 1) == 0:
                continue
            if name == "sserafim" and prop.get("name") in ("backTablesCutscene", "burgerCutscene", "truckDoor"):
                continue   # (the intro's)
            if prop.get("name") == "additionalLighten":
                continue   # (a flash the stage's script turns on)
            depth = sum(prop.get("zIndex", 0) > z[r] for r in order)
            if path.startswith("#"):   # a box of colour, scale = its size
                c = tuple(int(path[i:i + 2], 16) for i in (1, 3, 5))
                w, h = prop.get("scale", [1, 1])
                fx, fy = prop.get("scroll", [1, 1])
                px, py = prop.get("position", [0, 0])
                cxm, cym = (view[0] + view[2]) / 2, (view[1] + view[3]) / 2
                px -= (cxm - 640) * (fx - 1)
                py -= (cym - 360) * (fy - 1)
                x0, y0 = round((px - view[0]) * s), round((py - view[1]) * s)
                x1, y1 = round((px + w - view[0]) * s), round((py + h - view[1]) * s)
                box = Image.new("RGBA", (SCREEN_W, SCREEN_H), (0, 0, 0, 0))
                box.paste((*c, round(255 * prop.get("alpha", 1))), (max(0, x0), max(0, y0), min(SCREEN_W, x1), min(SCREEN_H, y1)))
                layers[depth].alpha_composite(box)
                continue
            try:
                anim = (prop.get("animations") or [dict(prefix="")])[0]
                start = prop.get("startingAnimation")
                if start:
                    anim = next((a for a in prop.get("animations", []) if a["name"] == start), anim)
                leaves = self.ref.frames(self.ref.path(path, cfg.get("directory", "shared")), anim, [0])[0][0]
            except (FileNotFoundError, KeyError) as e:
                print(f"  {name}: prop {path} ({e})", file=sys.stderr)
                continue
            sc = prop.get("scale", [1, 1])
            sc = [sc, sc] if isinstance(sc, (int, float)) else sc
            px, py = prop.get("position", [0, 0])
            fx, fy = prop.get("scroll", [1, 1])
            # parallax: the camera looks at the view's middle
            cxm, cym = (view[0] + view[2]) / 2, (view[1] + view[3]) / 2
            camx, camy = cxm - 640, cym - 360
            px -= camx * (fx - 1)
            py -= camy * (fy - 1)
            flip = -1 if prop.get("flipX") else 1
            m = (sc[0] * flip, 0, 0, sc[1], px, py)
            alpha = prop.get("alpha", 1)
            lv = [(im, multiply(m, mm), a * alpha) for im, mm, a in leaves]
            pixel = prop.get("isPixel", False)
            layer, _, _ = flatten(lv, s, view, Image.Resampling.NEAREST if pixel else Image.Resampling.BICUBIC)
            layers[depth].alpha_composite(layer.crop((0, 0, SCREEN_W, SCREEN_H)))
        bg = Image.new("RGBA", (SCREEN_W, SCREEN_H), (0, 0, 0, 255))
        bg.alpha_composite(layers[0])
        colours, idx = quantize([bg], transparent=False)
        pal = self.pics.palette(colours, opaque=True)
        bgid = self.pics.add(idx[0], SCREEN_W, SCREEN_H, 0, 0, pal, "stages", opaque=True)
        fronts = []
        for d in (1, 2, 3):
            im = layers[d]
            b = im.getbbox()
            if not b:
                fronts.append(0xFFFF)
                continue
            im = im.crop(b)
            colours, ix = quantize([im])
            fronts.append(self.pics.add(ix[0], im.width, im.height, b[0], b[1], self.pics.palette(colours), "stages"))
        feet = {r: (round((pos[r][0] - view[0]) * s), round((pos[r][1] - view[1]) * s)) for r in pos}
        if name == "phillyBlazin":   # (the two fighters share a place: their art stands them side by side)
            feet["dad"], feet["bf"] = (104, 214), (214, 214)
        extra = [(0, 0)] * 3
        if name == "sserafim":
            extra = [(round((x - view[0]) * s), round((y - view[1]) * s)) for x, y in SSERAFIM_FEET]
        self.rows.append(dict(name=name, bg=bgid, front=fronts, feet=feet, extra=extra, order=order, scale=s, view=view,
                              zoom=float(cfg.get("cameraZoom", 1))))
        if DEBUG:
            for d in (1, 2, 3):
                bg.alpha_composite(layers[d])
            bg.save(DEBUG / f"stage-{name}.png")


# ------------------------------------------------------------------------------------------------- HUD and menus
class Art:
    """The notes, the HUD and the menus' pictures; ids by name."""

    def __init__(self, ref, pics):
        self.ref, self.pics, self.ids = ref, pics, {}

    def put(self, name, im, ax, ay, group, colours=None):
        """An image, its top left (ax, ay) from its anchor."""
        b = im.getbbox() or (0, 0, 1, 1)
        im = im.crop(b)
        pal_colours, idx = quantize([im]) if colours is None else colours
        self.ids[name] = self.pics.add(idx[0], im.width, im.height, ax + b[0], ay + b[1],
                                       self.pics.palette(pal_colours), group)
        return self.ids[name]

    def sparrow(self, name, path, prefix, scale, pixel, group, pick=0, center=True):
        """A Sparrow frame, centred on its untrimmed box (as the game centres strum and splash frames)."""
        leaves, _ = self.ref.frames(path, dict(prefix=prefix), [pick])
        box = self.ref.boxes[0] if self.ref.boxes else bounds(leaves[0])
        im, x, y = flatten(leaves[0], scale, box, Image.Resampling.NEAREST if pixel else Image.Resampling.BICUBIC)
        ax = x - round((box[0] + box[2]) / 2 * scale) if center else x
        ay = y - round((box[1] + box[3]) / 2 * scale) if center else y
        return self.put(name, im, ax, ay, group)

    def image(self, name, path, scale, pixel, group, center=True, size=None):
        im = atlas.open_image(path)
        w, h = size or (max(1, round(im.width * scale)), max(1, round(im.height * scale)))
        im = im.resize((w, h), Image.Resampling.NEAREST if pixel else Image.Resampling.LANCZOS)
        return self.put(name, im, -(w // 2) if center else 0, -(h // 2) if center else 0, group)

    def styles(self):
        for style in ("funkin", "pixel"):
            a = self.ref.json(f"preload/data/notestyles/{style}.json")["assets"]
            base = self.ref.json("preload/data/notestyles/funkin.json")["assets"]
            pixel = style == "pixel"
            hud = HUD * 1.0
            # (pixel art at a whole number of pixels: the notes at 1.5 screen pixels for each of theirs)
            for d, dn in enumerate(["left", "down", "up", "right"]):
                Dn = dn.capitalize()
                n = a["note"]
                self.sparrow(f"{style}.note{d}", self.ref.path(n["assetPath"]), n["data"][dn]["prefix"],
                             n.get("scale", 1) * hud, pixel, "hud")
                st = a["noteStrumline"]
                for state in ("Static", "Press", "Confirm"):
                    self.sparrow(f"{style}.{state.lower()}{d}", self.ref.path(st["assetPath"]),
                                 st["data"][f"{dn}{state}"]["prefix"], st.get("scale", 1) * hud, pixel, "hud")
            # holds: a row of the body (stretched down the screen) and the end, for each direction
            h = a["holdNote"]
            im = atlas.open_image(self.ref.path(h["assetPath"]).with_suffix(".png"))
            cw = im.width // 8
            z = h.get("scale", 1) * hud
            for d in range(4):
                body = im.crop((2 * d * cw, 0, 2 * d * cw + cw, im.height))
                end = im.crop(((2 * d + 1) * cw, 0, (2 * d + 2) * cw, im.height))
                w = max(1, round(cw * z))
                rs = Image.Resampling.NEAREST if pixel else Image.Resampling.LANCZOS
                row = body.crop((0, body.height // 2, cw, body.height // 2 + 1)).resize((w, 1), rs)
                self.put(f"{style}.hold{d}", row, -(w // 2), 0, "hud")
                eh = max(1, round(end.height * z))
                self.put(f"{style}.end{d}", end.resize((w, eh), rs), -(w // 2), 0, "hud")
            # the splash on a Sick hit (its first two frames)
            sp = a.get("noteSplash", base["noteSplash"])
            for d, dn in enumerate(["left", "down", "up", "right"]):
                prefix = sp["data"][f"{dn}Splashes"][0]["prefix"]
                self.sparrow(f"{style}.splash{d}_0", self.ref.path(sp["assetPath"]), prefix,
                             sp.get("scale", 1) * hud, pixel, "hud", pick=1)
            # ratings, combo digits, countdown
            for r in ("sick", "good", "bad", "shit"):
                j = a[f"judgement{r.capitalize()}"]
                self.image(f"{style}.{r}", self.ref.path(j["assetPath"].replace("default:", "preload:")).with_suffix(".png"),
                           j.get("scale", 1) * hud, pixel, "hud")
            for n in range(10):
                j = a[f"comboNumber{n}"]
                self.image(f"{style}.num{n}", self.ref.path(j["assetPath"].replace("default:", "preload:")).with_suffix(".png"),
                           j.get("scale", 1) * hud, pixel, "hud")
            for c in ("Two", "One", "Go"):
                j = a[f"countdown{c}"]
                self.image(f"{style}.count{c}", self.ref.path(j["assetPath"]).with_suffix(".png"),
                           j.get("scale", 1) * hud, pixel, "hud")

    def menus(self):
        p = self.ref.path
        # the title: the logo (two frames of its bump) and Girlfriend dancing
        for i, f in enumerate((0, 6)):
            self.sparrow(f"logo{i}", p("logoBumpin", "preload"), "logo bumpin", HUD, False, "menus", pick=f)
        for i, f in enumerate((0, 7, 15, 22)):
            self.sparrow(f"gftitle{i}", p("gfDanceTitle", "preload"), "gfDance", HUD, False, "menus", pick=f)
        for name in ("menuBG", "menuDesat"):
            im = atlas.open_image(p(name, "preload").with_suffix(".png")).resize((SCREEN_W, SCREEN_H), Image.Resampling.LANCZOS)
            colours, idx = quantize([im], transparent=False)
            self.ids[name] = self.pics.add(idx[0], SCREEN_W, SCREEN_H, 0, 0, self.pics.palette(colours, opaque=True), "menus")

    def alphabet(self, scale=0.28):
        """The menus' bold letters (fonts/bold: white with a black outline), A-Z and signs; the digits, which it
        doesn't have, from fonts/default, filled white and outlined the same way. One palette for all; returns
        the sprite (or 0xFFFF) and the advance of each of ASCII 32..126."""
        from PIL import ImageFilter
        signs = {"-": "-dash-", "'": "-apostraphie-", "!": "-exclamation point-", "?": "-question mark-",
                 ".": "-period-", ",": "-comma-", "&": "-andpersand-", ":": ":", "(": "(", ")": ")", "+": "+",
                 "/": "-forward slash-", "*": "*", "%": "%", "#": "#", "$": "$"}
        bold = self.ref.path("fonts/bold", "preload")
        default = self.ref.path("fonts/default", "preload")
        frames = {}
        for path in (bold, default):
            for name, piece, m, fw, fh in self.ref.sparrow(path):
                frames.setdefault((path, re.sub(r"\d{4}$", "", name)), (piece, m, fw, fh))
        glyphs = {}
        for code in range(33, 127):
            c = chr(code)
            key = (bold, c if c.isalpha() and c.isupper() else signs.get(c, "\0"))
            if key in frames:
                piece, m, fw, fh = frames[key]
                im, x, y = flatten([(piece, m, 1)], scale, (0, 0, fw, fh))
            elif c.isdigit() and (default, c) in frames:
                piece, m, fw, fh = frames[(default, c)]
                pad = 8
                mask = Image.new("L", (fw + 2 * pad, fh + 2 * pad))
                mask.paste(piece.getchannel("A"), (pad + int(m[4]), pad + int(m[5])))
                outline = mask.filter(ImageFilter.MaxFilter(11))
                big = Image.new("RGBA", mask.size, (0, 0, 0, 0))
                big.paste((0, 0, 0, 255), (0, 0), outline)
                big.paste((255, 255, 255, 255), (0, 0), mask)
                im, x, y = flatten([(big, (1, 0, 0, 1, -pad, -pad), 1)], scale, (-pad, -pad, fw + pad, fh + pad))
                y += round(pad * scale)
            else:
                continue
            glyphs[c] = (im, x, y)
        # lower case: the capitals
        colours, idx = quantize([g[0] for g in glyphs.values()])
        pal = self.pics.palette(colours)
        table = []
        height = max(g[0].height + g[2] for g in glyphs.values())
        for code in range(32, 127):
            c = chr(code)
            c = c.upper() if c.isalpha() else c
            if c not in glyphs:
                table.append((0xFFFF, round(height * 0.5)))
                continue
            im, x, y = glyphs[c]
            ix = idx[list(glyphs).index(c)]
            b = im.getbbox() or (0, 0, 1, 1)
            sid = self.pics.add(crop_idx(ix, im.width, im.height, b), b[2] - b[0], b[3] - b[1], x + b[0], y + b[1],
                                pal, "menus") if (c not in self.ids) else self.ids[c]
            self.ids.setdefault(c, sid)
            table.append((sid, b[2] - b[0] + max(1, round(height * 0.04))))
        self.bold, self.bold_height = table, height
        return table


# ------------------------------------------------------------------------------------------------- the songs
def ease_code(ease, easedir):
    if ease in ("linear", "INSTANT"):
        return EASES.index(ease)
    base = re.sub("(InOut|In|Out)$", "", ease)
    d = ease[len(base):] or easedir or "Out"
    e = EASES.index(base) if base in EASES else 0
    return e | (EASE_DIRS.index(d) if d in EASE_DIRS else 1) << 4


def step_us(bpm):
    return 60e6 / bpm / 4


class Charts:
    def __init__(self, songs, chars, stages, blob, anim_name):
        self.songs, self.blob = songs, blob
        self.kinds = [""]
        for s in songs.songs:
            for v in s["variations"]:
                for d, ns in v["chart"]["notes"].items():
                    for n in ns:
                        k = n.get("k", "") or ""
                        if k not in self.kinds:
                            self.kinds.append(k)
        self.kinds = [""] + sorted(k for k in self.kinds if k)
        self.tempos, self.events, self.charts, self.vars, self.rows = [], [], [], [], []
        self.max_raw = self.max_notes = 0
        self.stats = dict(raw=0, packed=0, notes=0)
        for si, s in enumerate(songs.songs):
            v0 = len(self.vars)
            for v in s["variations"]:
                self.variation(si, s, v, chars, stages, anim_name)
            meta = s["variations"][0]["meta"]
            opp = meta["playData"]["characters"]["opponent"]
            name = meta["songName"]
            if len(name) > 18:   # ("SPAGHETTI (feat. j-hope of BTS) (Clean Version)": its title alone)
                name = re.sub(r"\s*\(.*$", "", name)
            self.rows.append(dict(name=name, level=s["level"], var=v0, nvar=len(self.vars) - v0,
                                  icon=chars.rows[chars.index[opp]]["icon"]))

    def variation(self, si, s, v, chars, stages, anim_name):
        meta, chart = v["meta"], v["chart"]
        pd = meta["playData"]
        c = pd["characters"]
        # the tempo: each change's time (us), its step length (1/256 us) and the steps before it (16.16)
        t0 = len(self.tempos)
        steps, last_t, last_len = 0.0, 0.0, None
        for tc in meta["timeChanges"]:
            t = float(tc["t"])
            if last_len is not None:
                steps += (t - last_t) * 1000 / last_len
            if "b" in tc and tc["b"] is not None and last_len is not None:
                steps = float(tc["b"]) * 4
            last_t, last_len = t, step_us(float(tc["bpm"]))
            self.tempos.append((us(t), round(last_len * 256), round(steps * 65536)))
        e0 = len(self.events)
        for e in sorted(chart.get("events", []), key=lambda e: float(e["t"])):
            ev = self.event(e, chars, anim_name, c, meta)
            if ev:
                self.events.append(ev)
        ch0 = len(self.charts)
        speeds = chart.get("scrollSpeed", {})
        for di, diff in enumerate(DIFFICULTIES):
            ns = chart["notes"].get(diff)
            if diff not in pd["difficulties"] or not ns:
                continue
            ns = sorted(ns, key=lambda n: (float(n["t"]), int(n["d"])))
            data, prev, total, players = bytearray(), 0, 0, 0
            for n in ns:
                t = us(n["t"])
                length = us(n.get("l", 0) or 0)
                k = self.kinds.index(n.get("k", "") or "")
                d = int(n["d"])
                assert 0 <= d < 8 and t >= prev
                data += leb(t - prev) + leb(d | (length > 0) << 3 | k << 4)
                if length > 0:
                    data += leb(length)
                prev = t
                if d < 4:
                    players += 1
                    if (n.get("k") or "") != "non_scoreable":
                        total += 1
            z = deflate(bytes(data))
            at = self.blob.add(z)
            self.stats["raw"] += len(data)
            self.stats["packed"] += len(z)
            self.stats["notes"] += len(ns)
            self.max_raw = max(self.max_raw, len(data))
            self.max_notes = max(self.max_notes, len(ns))
            speed = float(speeds.get(diff, speeds.get("default", 1)))
            self.charts.append(dict(var=len(self.vars), diff=di, at=at, size=len(z), raw=len(data), notes=len(ns),
                                    total=total, speed=round(speed * 1000)))
        stage = stages.index(pd["stage"])
        role = [c.get("player"), c.get("opponent"), c.get("girlfriend")]
        inst = s["id"] + ("" if v["kind"] == "default" else "-" + (c.get("instrumental") or v["kind"]))
        if inst not in INST_MS:
            inst = s["id"]
        last = max([float(n["t"]) + float(n.get("l", 0) or 0) for d in chart["notes"].values() for n in d] + [0])
        length = max(INST_MS.get(inst, 0), last + 1200)
        style = 1 if pd.get("noteStyle") == "pixel" else 0
        self.vars.append(dict(song=si, kind=VARIATIONS.index(v["kind"]), stage=stage, style=style,
                              chr=[chars.index[r] if r else 255 for r in role],
                              extra=1 if s["id"] == "spaghetti" else 0, tempo=t0, ntempo=len(self.tempos) - t0,
                              event=e0, nevent=len(self.events) - e0, chart=ch0, ncharts=len(self.charts) - ch0,
                              flags=(1 if c.get("opponent") in ("gf",) else 0) | (2 if s["id"] == "blazin" else 0),
                              length=round(length)))

    def event(self, e, chars, anim_name, c, meta):
        kind = e["e"]
        if kind not in EVENTS:
            return None
        v = e.get("v")
        v = v if isinstance(v, dict) else {"char": v}
        t = us(e["t"])
        typ = EVENTS.index(kind)
        a = b = cc = 0
        val = dur = 0
        if kind == "SetCameraBop":
            val = round(float(v.get("intensity", 1.0)) * 1000)
            dur = max(0, round(float(v.get("rate", 4)) * 4))     # every this many steps
            b = round(float(v.get("offset", 0) or 0) * 4) & 255
        elif kind == "PlayAnimation":
            target = v.get("target", "bf")
            a = {"boyfriend": 0, "bf": 0, "player": 0, "dad": 1, "opponent": 1, "girlfriend": 2, "gf": 2}.get(target, 255)
            if a == 255 or v.get("anim") not in anim_name:
                return None
            b = anim_name[v["anim"]]
            cc = 1 if v.get("force", False) else 0
        elif kind == "ScrollSpeed":
            val = round(float(v.get("scroll", 1)) * 1000)
            a = 1 if v.get("absolute", False) else 0
            b = ease_code(v.get("ease", "linear"), v.get("easeDir", ""))
            dur = round(float(v.get("duration", 4)) * 1000)   # steps x 1000
            cc = {"both": 0, "player": 1, "opponent": 2}.get(v.get("strumline", "both"), 0)
        elif kind == "SetHealthIcon":
            a = int(v.get("char", 0))
            b = chars.icon(v.get("id", "face"))
            cc = 1 if v.get("flipX", False) else 0
        elif kind == "sserafimSing":
            a = sum(1 << i for i, x in enumerate(v.get("singing", [])) if x)
        elif kind == "sserafimShow":
            a = sum(1 << i for i, x in enumerate(v.get("visible", [])) if x)
        elif kind == "sserafimDark":
            val = round(float(v.get("amount", 0)) * 1000)
            dur = round(float(v.get("duration", 0)) * 1000)   # seconds x 1000
        return (t, typ, a, b, cc, val, dur)


# ------------------------------------------------------------------------------------------------- C
def c_array(ctype, name, rows, dims=""):
    def lit(x):
        if isinstance(x, (list, tuple)):
            return "{" + ",".join(lit(y) for y in x) + "}"
        if isinstance(x, str):
            return x
        return str(x)
    out = [f"const {ctype} {name}[{len(rows)}]{dims} = {{"]
    out += ["  " + lit(r) + "," for r in rows]
    out.append("};")
    return out


def cstr(s):
    return json.dumps(s)


def ident(s):
    return re.sub("[^A-Z0-9]+", "_", s.upper()).strip("_")


def git_blob_sha(path):
    data = Path(path).read_bytes()
    return hashlib.sha1(b"blob %d\0" % len(data) + data).hexdigest()


DEBUG = None


def main():
    global DEBUG
    ap = argparse.ArgumentParser()
    ap.add_argument("ref")
    ap.add_argument("out")
    ap.add_argument("--list", help="write the files read, with their git blob hashes, here")
    ap.add_argument("--debug", help="save pictures of the stages here")
    args = ap.parse_args()
    if args.debug:
        DEBUG = Path(args.debug)
        DEBUG.mkdir(parents=True, exist_ok=True)
    ref = Reference(args.ref)
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    blob = Blob()
    pics = Pictures(blob)
    songs = Songs(ref)
    # the animations the charts' events and note kinds ask for, by character
    wanted = {}
    for s in songs.songs:
        for v in s["variations"]:
            c = v["meta"]["playData"]["characters"]
            roles = {0: c.get("player"), 1: c.get("opponent"), 2: c.get("girlfriend")}
            for e in v["chart"].get("events", []):
                if e["e"] == "PlayAnimation" and isinstance(e.get("v"), dict):
                    t = {"boyfriend": 0, "bf": 0, "player": 0, "dad": 1, "opponent": 1, "girlfriend": 2,
                         "gf": 2}.get(e["v"].get("target"), None)
                    if t is not None and roles.get(t):
                        wanted.setdefault(roles[t], set()).add(e["v"].get("anim"))
            for ns in v["chart"]["notes"].values():
                for n in ns:
                    names = kind_anims(n.get("k", "") or "")
                    if not names:
                        continue
                    who = roles[0] if int(n["d"]) < 4 else roles[1]
                    targets = [who] + (SSERAFIM_EXTRA + [c.get("player"), c.get("opponent"), c.get("girlfriend")]
                                       if s["id"] == "spaghetti" else [])
                    for wname in targets:
                        if wname:
                            wanted.setdefault(wname, set()).update(x for x in names if x)
    for c in songs.characters():   # Girlfriend's cheers and tears
        wanted.setdefault(c, set()).update(("combo50", "combo200", "drop70", "hey", "cheer"))
    # the stages' own scale for characters (Blazin's), where a character is only ever on such a stage
    seen = {}
    cast = {}
    for s in songs.songs:
        for v in s["variations"]:
            pd = v["meta"]["playData"]
            st = ref.json(f"preload/data/stages/{pd['stage']}.json").get("characters", {})
            c = pd["characters"]
            for k, role in (("player", "bf"), ("opponent", "dad"), ("girlfriend", "gf")):
                if c.get(k):
                    seen.setdefault(c[k], set()).add(float(st.get(role, {}).get("scale", 1)))
            cast.setdefault(pd["stage"], []).append((c.get("player"), c.get("opponent")))
    stage_scale = {n: next(iter(v)) for n, v in seen.items() if len(v) == 1 and next(iter(v)) != 1}
    for who in ("bf", "pico"):
        OWNED[who] = set(ref.json(f"preload/data/players/{who}.json").get("ownedChars", []))
    print("characters", file=sys.stderr)
    chars = Characters(ref, pics, songs.characters(), wanted, stage_scale, songs.players())
    print("stages", file=sys.stderr)
    stage_names = songs.stages()
    stages = Stages(ref, pics, stage_names, chars, cast)
    print("HUD and menus", file=sys.stderr)
    art = Art(ref, pics)
    art.styles()
    art.menus()
    glyphs = art.alphabet()
    charts = Charts(songs, chars, stage_names, blob, chars.name_ids)
    if DEBUG:   # each stage with its first song's characters
        done = set()
        for v in charts.vars:
            st = stages.rows[v["stage"]]
            if st["name"] in done:
                continue
            done.add(st["name"])
            im = Image.open(DEBUG / f"stage-{st['name']}.png").convert("RGBA")
            for role, key in ((2, "gf"), (1, "dad"), (0, "bf")):
                ci = v["chr"][role]
                if ci == 255:
                    continue
                pim, x, y = chars.preview[chars.rows[ci]["name"]]
                fx, fy = st["feet"][key]
                im.alpha_composite(pim, (max(0, fx + x), max(0, fy + y)))
            im.save(DEBUG / f"scene-{st['name']}.png")
    chars.kind_rows(charts.kinds)
    # ---- write
    h = ["/* Made by tools/data.py from the game's own files (see tools/ref.txt). */", "#ifndef FNF_IDS_H",
         "#define FNF_IDS_H"]
    counts = dict(SPRITES=len(pics.sprites), PALETTES=len(pics.palettes), ANIMS=len(chars.anims),
                  CHARS=len(chars.rows), KINDS=len(chars.kinds), NOTE_KINDS=len(charts.kinds),
                  ICONS=len(chars.icon_rows), STAGES=len(stages.rows), TEMPOS=len(charts.tempos),
                  EVENTS=len(charts.events), CHARTS=len(charts.charts), VARS=len(charts.vars),
                  SONGS=len(charts.rows), LEVELS=len(songs.levels), BLOB=len(blob.data),
                  CHART_RAW_MAX=charts.max_raw, CHART_NOTES_MAX=charts.max_notes)
    # RAM: what a song needs unpacked (tools/data.py works it out; draw.c lays it out)
    def raw(i):
        w, hh = pics.sprites[i][0], pics.sprites[i][1]
        return (w + 1) // 2 * hh
    frame_max = []
    for r in chars.rows:
        m = 0
        for a in range(r["anim"], r["anim"] + r["count"]):
            s0, n = chars.anims[a][0], chars.anims[a][1]
            m = max([m] + [raw(s0 + k) for k in range(n)])
        frame_max.append(m)
    front = [sum(raw(f) for f in st["front"] if f != 0xFFFF) for st in stages.rows]
    cast = 0
    for v in charts.vars:
        ids = [c for c in v["chr"] if c != 255]
        if v["extra"]:
            ids += [chars.index[n] for n in SSERAFIM_EXTRA]
        cast = max(cast, sum(frame_max[c] for c in ids) + front[v["stage"]])
    def style_bytes(style, names):
        return sum(raw(art.ids[f"{style}.{n}"]) for n in names)
    hud_names = [f"{k}{d}" for k in ("note", "static", "press", "confirm", "hold", "end") for d in range(4)] + \
        ["sick", "good", "bad", "shit"] + [f"num{n}" for n in range(10)]
    hud = max(style_bytes(st, hud_names) for st in ("funkin", "pixel"))
    transient = max(max(style_bytes(st, [f"splash{d}_0" for d in range(4)]),
                        max(raw(art.ids[f"{st}.count{c}"]) for c in ("Two", "One", "Go"))) for st in ("funkin", "pixel"))
    icon = 3 * max(raw(i) for row in chars.icon_rows for i in row)
    bold_bytes = sum((raw(i) + 3) & ~3 for i in sorted({g[0] for g in glyphs if g[0] != 0xFFFF}))
    counts.update(CAST_MAX=cast, HUD_MAX=hud, TRANSIENT_MAX=transient, ICON_MAX=icon, BOLD_BYTES=bold_bytes,
                  BG_BYTES=SCREEN_W * SCREEN_H // 2)
    for r, m in zip(chars.rows, frame_max):
        r["frame_max"] = m
    h += [f"#define FNF_{k} {v}" for k, v in counts.items()]
    h += [f"#define FNF_SPR_{ident(k)} {v}" for k, v in art.ids.items() if len(k) > 1]
    h.append(f"#define FNF_BOLD_HEIGHT {art.bold_height}")
    h += [f"#define FNF_CHR_{ident(r['name'])} {i}" for i, r in enumerate(chars.rows)]
    h += [f"#define FNF_ANIM_{ident(k)} {v}" for k, v in chars.name_ids.items()]
    h += [f"#define FNF_EV_{ident(k)} {i}" for i, k in enumerate(EVENTS)]
    h += [f"#define FNF_KIND_{ident(k or 'normal')} {i}" for i, k in enumerate(charts.kinds)]
    h += [f"#define FNF_STAGE_{ident(k)} {i}" for i, k in enumerate(stage_names)]
    h += ["#endif", ""]
    (out / "ids.h").write_text("\n".join(h), newline="\n")
    c = ["/* Made by tools/data.py from the game's own files (see tools/ref.txt): the Funkin' Crew's charts and",
         " * pictures, which are not in this repository. */", '#include "data.h"']
    c += c_array("uint16_t", "fnf_palette", pics.palettes, "[16]")
    c += c_array("fnf_sprite_t", "fnf_sprite", [[w, h_, x, y, p, 0, at, size] for w, h_, x, y, p, at, size in pics.sprites])
    c += c_array("fnf_anim_t", "fnf_anim", [[s, n, ln, fps, loop, picks] for s, n, ln, fps, loop, picks in chars.anims])
    c += c_array("uint8_t", "fnf_anim_name", chars.anim_names)
    c += c_array("fnf_char_t", "fnf_char", [[r["anim"], r["count"], r["dance"], r["sing"],
                                              (1 if r["alternate"] else 0) | (2 if r["pixel"] else 0) | r["owner"],
                                              r["icon"],
                                              r["slot"], r["frame_max"]] for r in chars.rows])
    c += c_array("fnf_kind_t", "fnf_kind", [[ci, ki, an] for ci, ki, an in chars.kinds] or [[255, 255, [255] * 4]])
    c += c_array("uint16_t", "fnf_icon", chars.icon_rows, "[3]")
    order_ix = {"bf": 0, "dad": 1, "gf": 2}
    c += c_array("fnf_stage_t", "fnf_stage", [[r["bg"], r["front"], [list(r["feet"][k]) for k in ("bf", "dad", "gf")],
                                                [list(e) for e in r["extra"]], [order_ix[o] for o in r["order"]],
                                                round(r["scale"] * 10000)]
                                               for r in stages.rows])
    c += c_array("fnf_tempo_t", "fnf_tempo", charts.tempos)
    c += c_array("fnf_event_t", "fnf_event", [[t, typ, a_, b, cc, val, dur] for t, typ, a_, b, cc, val, dur in charts.events])
    c += c_array("fnf_chart_t", "fnf_chart", [[r["var"], r["diff"], 0, r["at"], r["size"], r["raw"], r["notes"],
                                                r["total"], r["speed"]] for r in charts.charts])
    c += c_array("fnf_var_t", "fnf_var", [[r["song"], r["kind"], r["stage"], r["style"], r["chr"], r["extra"],
                                            r["tempo"], r["ntempo"], r["event"], r["nevent"], r["chart"],
                                            r["ncharts"], r["flags"], r["length"]] for r in charts.vars])
    c += c_array("fnf_song_t", "fnf_song", [[cstr(r["name"]), r["level"], r["var"], r["nvar"], r["icon"]]
                                             for r in charts.rows])
    c += c_array("fnf_level_t", "fnf_level", [[cstr(l["name"]), [songs.order.index(s) for s in l["songs"]] +
                                                [255] * (4 - len(l["songs"])), len(l["songs"])] for l in songs.levels])
    c += c_array("fnf_glyph_t", "fnf_bold", [list(g) for g in glyphs])
    c.append(f"const uint8_t fnf_blob[{len(blob.data)}] = {{")
    d = blob.data
    c += ["  " + ",".join(str(x) for x in d[i:i + 40]) + "," for i in range(0, len(d), 40)]
    c.append("};")
    (out / "data.c").write_text("\n".join(c) + "\n", newline="\n")
    print(f"pictures {sum(pics.bytes.values())} bytes {pics.bytes}; charts {charts.stats}, the longest "
          f"{charts.max_raw} bytes unpacked ({charts.max_notes} notes); blob {len(blob.data)}", file=sys.stderr)
    if args.list:
        root = ref.root.resolve()
        files = sorted({str(p.resolve().relative_to(root)) for p in atlas.opened})
        Path(args.list).write_text("".join(f"{git_blob_sha(root / f)} {f}\n" for f in files), newline="\n")


if __name__ == "__main__":
    main()
