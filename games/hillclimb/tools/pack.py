#!/usr/bin/env python3
"""Hill Climb Racing's pictures and fonts -> src/data.bin, src/data.h and src/art.h.

    python3 -I tools/pack.py REF        (REF: the folder tools/fetch.py filled)

data.bin: "HCRD", the number of pictures and fonts, then a table of their offsets (u32), then each one:
- a picture: w, h, colours, flags (u16 each; flags 1: opaque), its pixels a meter x 4 (0: a picture of the menus),
  its pivot x 4 (s16, s16), 0; its colours as premultiplied R, G, B, A bytes; for an opaque one also as RGB565;
  then w x h colour indices, row by row
- a font: first character, count, line height, ascent (u8 each); then a glyph each (w, h, x and y of its top-left
  from the pen, advance, 3 bytes of 0, offset of its bitmap: u8 u8 s8 s8 u8, u32) and the bitmaps, a byte a pixel: the
  letter's coverage in the high 4 bits, its dark outline's in the low 4

data.h names them (IMG_*, FONT_*); art.h is tools/art.py's vehicles in meters, for src/run.c.
icon(REF) drew src/icon.png once (it is kept as it is now, with NumPlay's corners: tools/round_icons.py)."""
import math
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import art  # noqa: E402

SRC = os.path.join(HERE, "..", "src")
VPPM = 44   # pixels a meter the vehicles, Bill and the things on the way are kept at (the game draws them at 30 or less)

# name, wiki file, crop box or None, how it is sized: ("ppm", source pixels a meter) keeps VPPM pixels a meter;
# ("w", width) or ("h", height) or ("size", w, h) for the menus; pivot ("c": centre, or (x, y) source pixels)
PICTURES = [
    # coins and fuel cans: 1.5 m squares in the game (Coin::create, FuelCanister::create)
    ("COIN5", "Coin5.png", None, ("ppm", 64 / 1.5), "c"),
    ("COIN25", "Coin25.png", None, ("ppm", 64 / 1.5), "c"),
    ("COIN100", "Coin100.png", None, ("ppm", 64 / 1.5), "c"),
    ("COIN500", "Coin500.png", None, ("ppm", 64 / 1.5), "c"),
    ("FUELCAN", "Fuel-canister.png", None, ("ppm", 64 / 1.5), "c"),
    ("CLOUD", "Sky-cloud.png", None, ("w", 76), "c"),
    ("TREE_TRUNK", "Lowres-tree-part1.png", None, ("ppm", 64 / 1.3), (32, 31)),
    ("TREE_TOP", "Lowres-tree-part2.png", None, ("ppm", 64 / 1.9), (32, 31)),
    ("PLANK", "BridgeWoodBlock.png", None, ("ppm", 64 / 1.08), "c"),   # (a plank: 1.08 x 0.3 m, its picture 0.59 m high)
    ("FLAG", "Flag.png", None, ("ppm", 128 / 1.6), (4, 62)),
    ("ROCKS", "Rocks.png", None, ("ppm", 64 / 1.2), "c"),
    ("ROCKS_MARS", "Rocks-mars.png", None, ("ppm", 64 / 1.2), "c"),
    ("ICICLE", "Iceshard.png", None, ("ppm", 64 / 1.4), (32, 2)),
    ("SMOKE", "Smoke.png", None, ("w", 20), "c"),
    ("DIRT", "Dirt.png", None, ("w", 20), "c"),
    ("SNOW", "Snow.png", None, ("w", 20), "c"),
    ("GRASS", "Countryside_Particle.png", None, ("w", 20), "c"),
    ("SLIME", "Alien-slime.png", None, ("w", 20), "c"),
    ("DIRT_MUD", "game/dirt-mud.png", None, ("w", 20), "c"),
    ("FIRE", "game/fire.png", None, ("w", 20), "c"),
    # the new vehicles' parts on bodies of their own: the tractor's exhaust pipe (0.836571 x 1.67314 m), the snow
    # mobile's ski (1.861818 x 0.930909 m across its two ski wheels), the bus' passengers' heads (0.96 m squares)
    ("EXHAUST", "game/car-tractor-exhaust.png", None, ("ppm", 64 / 0.836571), "c"),
    ("SKI", "Car-snowmobile-ski.png", None, ("ppm", 256 / 1.861818), "c"),
    ("PASS1", "game/passanger1-head.png", None, ("ppm", 256 / 0.96), "c"),
    ("PASS2", "game/passanger2-head.png", None, ("ppm", 256 / 0.96), "c"),
    ("PASS3", "game/passanger3-head.png", None, ("ppm", 256 / 0.96), "c"),
    # skies (a column of colours from top to bottom) and the stages' ground
    ("SKY", "Sky-bg.png", None, ("size", 1, 32), "c"),
    ("SKY_CAVE", "Sky-bg-cave.png", None, ("size", 1, 32), "c"),
    ("SKY_FOREST", "Sky-bg-forest.png", None, ("size", 1, 32), "c"),
    ("SKY_MARS", "Sky-bg-mars.png", None, ("size", 1, 32), "c"),
    ("SKY_ARCTIC_CAVE", "Sky-bg-arctic-cave.png", None, ("size", 1, 32), "c"),
    ("SKY_HIGHWAY", "Highway-sky-bg.png", None, ("size", 1, 32), "c"),
    ("SKY_MOON", "Moon-bg.png", None, ("size", 1, 64), "c"),
    ("SKY_CLIFFS", "game/sky-bg-cliffs.png", None, ("size", 1, 32), "c"),
    ("SKY_MUD", "game/sky-bg-mud.png", None, ("size", 1, 32), "c"),
    ("SKY_VOLCANO", "game/sky-bg-volcano.png", None, ("size", 1, 32), "c"),
    ("SURF_COUNTRY", "Lowres-terrain-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_COUNTRY", "Lowres-terrain-ground.png", None, ("size", 128, 128), "c"),
    ("SURF_DESERT", "Lowres-desert-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_DESERT", "Lowres-desert-ground.png", None, ("size", 128, 128), "c"),
    ("SURF_ARCTIC", "Lowres-arctic-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_ARCTIC", "Lowres-arctic-ground.png", None, ("size", 128, 128), "c"),
    ("SURF_HIGHWAY", "Lowres-highway-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_HIGHWAY", "Lowres-highway-ground.png", None, ("size", 128, 128), "c"),
    ("SURF_CAVE", "Lowres-terrain-cave-surface.png", None, ("size", 256, 16), "c"),
    ("SURF_MOON", "Lowres-moon-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_MOON", "Lowres-moon-ground.png", None, ("size", 128, 128), "c"),
    ("SURF_MARS", "Lowres-terrain-mars-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_MARS", "Lowres-mars-ground.png", None, ("size", 128, 128), "c"),
    ("SURF_ALIEN", "Lowres-terrain-alien-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_ALIEN", "Lowres-terrain-alien.png", None, ("size", 128, 128), "c"),
    ("SURF_FOREST", "Lowres-terrain-forest-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_FOREST", "Lowres-terrain-forest-ground.png", None, ("size", 128, 128), "c"),
    ("GROUND_CLIFFS", "game/lowres-terrain-cliffs-ground.png", None, ("size", 128, 128), "c"),
    ("SURF_MUD", "game/lowres-terrain-mud-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_MUD", "game/lowres-terrain-mud.png", None, ("size", 128, 128), "c"),
    ("SURF_VOLCANO", "game/lowres-terrain-volcano-surface.png", None, ("size", 256, 16), "c"),
    ("GROUND_VOLCANO", "game/lowres-terrain-volcano.png", None, ("size", 128, 128), "c"),
    # the pits' surfaces (Pit::draw: mud.png, lava.png)
    ("PIT_MUD", "game/mud.png", None, ("size", 128, 32), "c"),
    ("PIT_LAVA", "game/lava.png", None, ("size", 128, 16), "c"),
    # the menus
    ("STAGE_COUNTRY", "Stage-icon-countryside.png", None, ("size", 144, 72), "c"),
    ("STAGE_DESERT", "Stage-icon-desert.png", None, ("size", 144, 72), "c"),
    ("STAGE_ARCTIC", "Stage-icon-arctic.png", None, ("size", 144, 72), "c"),
    ("STAGE_HIGHWAY", "Stage-icon-highway.png", None, ("size", 144, 72), "c"),
    ("STAGE_CAVE", "Stage-icon-cave.png", None, ("size", 144, 72), "c"),
    ("STAGE_MOON", "Stage-icon-moon.png", None, ("size", 144, 72), "c"),
    ("STAGE_MARS", "Stage-icon-mars.png", None, ("size", 144, 72), "c"),
    ("STAGE_ALIEN", "Stage-icon-alienplanet.png", None, ("size", 144, 72), "c"),
    ("STAGE_ARCTIC_CAVE", "Stage-icon-arctic-cave.png", None, ("size", 144, 72), "c"),
    ("STAGE_FOREST", "Stage-icon-forest.png", None, ("size", 144, 72), "c"),
    ("STAGE_MOUNTAIN", "game/stage-icon-cliffs.png", None, ("size", 144, 72), "c"),
    ("STAGE_MUDPOOL", "game/stage-icon-mud.png", None, ("size", 144, 72), "c"),
    ("STAGE_VOLCANO", "game/stage-icon-volcano.png", None, ("size", 144, 72), "c"),
    ("STAGE_LOCKED", "Stage-icon-locked.png", None, ("size", 144, 72), "c"),
    ("ICON_JEEP", "Mission-vehicle-jeep.png", None, ("w", 112), "c"),
    ("ICON_BIKE", "Mission-vehicle-bike.png", None, ("w", 112), "c"),
    ("ICON_MONSTER", "Mission-vehicle-monster.png", None, ("w", 112), "c"),
    ("ICON_QUAD", "Mission-vehicle-quadbike.png", None, ("w", 112), "c"),
    ("ICON_RACE", "Mission-vehicle-racecar.png", None, ("w", 112), "c"),
    ("ICON_RALLY", "Mission-vehicle-rallycar.png", None, ("w", 112), "c"),
    ("ICON_BUGGY", "Mission-vehicle-dunebuggy.png", None, ("w", 112), "c"),
    ("ICON_TANK", "Mission-vehicle-tank.png", None, ("w", 112), "c"),
    ("ICON_TRACTOR", "Mission-vehicle-tractor.png", None, ("w", 112), "c"),
    ("ICON_BUS", "Mission-vehicle-touristbus.png", None, ("w", 112), "c"),
    ("ICON_DIESEL", "Mission-vehicle-dieseltruck.png", None, ("w", 112), "c"),
    ("ICON_SNOWMOBILE", "Mission-vehicle-snowmobile.png", None, ("w", 112), "c"),
    ("ICON_TRUCK", "Mission-vehicle-truck.png", None, ("w", 112), "c"),
    ("UP_ENGINE", "MainMenuUpgradeEngine.png", None, ("w", 40), "c"),
    ("UP_SUSPENSION", "MainMenuUpgradeSuspension.png", None, ("w", 40), "c"),
    ("UP_TIRES", "MainMenuUpgradeTires.png", None, ("w", 40), "c"),
    ("UP_AWD", "MainMenuUpgradeAWD.png", None, ("w", 40), "c"),
    ("UP_AIR", "MainMenuUpgradeBikeRotation.png", None, ("w", 40), "c"),
    ("UP_DOWNFORCE", "MainMenuUpgradeDownforce.png", None, ("w", 40), "c"),
    ("UP_FUEL", "MainMenuUpgradeFuel.png", None, ("w", 40), "c"),
    ("PEDAL_GAS", "Pedal-gas-normal.png", "trim", ("h", 43), "c"),
    ("PEDAL_GAS_DOWN", "Pedal-gas-pressed.png", "trim", ("h", 43), "c"),
    ("PEDAL_BRAKE", "Pedal-brake-normal.png", "trim", ("h", 43), "c"),
    ("PEDAL_BRAKE_DOWN", "Pedal-brake-pressed.png", "trim", ("h", 43), "c"),
    ("METER_RPM", "Meter-rpm.png", None, ("w", 50), "c"),
    ("METER_BOOST", "Meter-boost.png", None, ("w", 50), "c"),
    ("NEEDLE", "Meter-needle.png", None, ("h", 27), (16, 100)),
    ("FUEL_ICON", "Fuel-icon.png", None, ("w", 20), "c"),
    ("FUEL_LOW", "Fuel-warning.png", None, ("w", 70), "c"),
    ("COIN_ICON", "Coin-icon.png", None, ("w", 18), "c"),
    ("MENU_BG", "Menu-bg.png", None, ("size", 320, 240), "c"),
    ("LOGO", "Game-logo.png", "trim", ("w", 200), "c"),
    ("LOCK", "Lock.png", None, ("w", 28), "c"),
    ("START", "Icon-start.png", None, ("w", 30), "c"),
]

# the pickers' strips: each vehicle's and stage's picture small (48 colours)
THUMBS_V = ["jeep", "bike", "monster", "quadbike", "racecar", "rallycar", "dunebuggy", "tank", "tractor", "touristbus",
            "dieseltruck", "snowmobile", "truck"]
THUMBS_S = ["Stage-icon-countryside.png", "Stage-icon-desert.png", "Stage-icon-arctic.png", "Stage-icon-highway.png",
            "Stage-icon-cave.png", "Stage-icon-moon.png", "Stage-icon-mars.png", "Stage-icon-alienplanet.png",
            "Stage-icon-arctic-cave.png", "Stage-icon-forest.png", "game/stage-icon-cliffs.png",
            "game/stage-icon-mud.png", "game/stage-icon-volcano.png"]
PICTURES += [(f"THUMB_V{i}", f"Mission-vehicle-{n}.png", "trim", ("h", 18), "c", 48) for i, n in enumerate(THUMBS_V)]
PICTURES += [(f"THUMB_S{i}", n, None, ("size", 34, 17), "c", 48) for i, n in enumerate(THUMBS_S)]

# the fonts, from Teko: name, the height of its capitals in pixels, weight, outline width
FONTS = [("FONT_S", 9, 560, 1), ("FONT_M", 13, 600, 1.5), ("FONT_L", 20, 620, 2), ("FONT_XL", 34, 640, 3)]


def ref_path(ref, name):
    return os.path.join(ref, "wiki", name)


def load(ref, name):
    if name == "buggy-tire":   # the Dune Buggy's tyre, from the picture of the whole buggy
        return Image.open(ref_path(ref, "Dune_Buggy_(Sprite).png")).convert("RGBA").crop((6, 133, 86, 213))
    if name.startswith("game/"):   # the game's own files (tools/fetch.py: its 1.10.2 package's assets)
        return Image.open(os.path.join(ref, name)).convert("RGBA")
    return Image.open(ref_path(ref, name)).convert("RGBA")


def trimmed(im, piv):
    """The picture without its fully clear margins, and its pivot moved with it."""
    box = im.getbbox()
    if not box:
        return im, piv
    return im.crop(box), (piv[0] - box[0], piv[1] - box[1])


def premul_resize(im, w, h):
    """Resized without the dark fringes straight alpha gives: in premultiplied colours."""
    a = np.asarray(im).astype(np.float32) / 255
    p = a.copy()
    p[..., :3] *= p[..., 3:4]
    planes = [Image.fromarray(p[..., i]).resize((w, h), Image.LANCZOS) for i in range(4)]
    q = np.stack([np.asarray(x) for x in planes], -1)
    q = np.clip(q, 0, 1)
    al = q[..., 3:4]
    rgb = np.where(al > 1e-4, q[..., :3] / np.maximum(al, 1e-4), 0)
    out = np.concatenate([np.clip(rgb, 0, 1), al], -1)
    return Image.fromarray((out * 255 + 0.5).astype(np.uint8), "RGBA")


def quantize(im, colors=256):
    """Palette and indices from pngquant (keeps the alpha's smooth edges)."""
    with tempfile.TemporaryDirectory() as d:
        src, dst = os.path.join(d, "in.png"), os.path.join(d, "out.png")
        im.save(src)
        subprocess.run(["pngquant", "--force", "--speed", "1", "--nofs", str(colors), "--output", dst, src],
                       check=True)
        q = Image.open(dst)
        q.load()
    if q.mode != "P":
        q = im.convert("P", palette=Image.ADAPTIVE, colors=colors)
    pal = q.getpalette()[:768]
    trns = q.info.get("transparency", b"")
    n = len(pal) // 3
    alpha = [255] * n
    if isinstance(trns, bytes):
        for i, a in enumerate(trns):
            alpha[i] = a
    elif isinstance(trns, int):
        alpha[trns] = 0
    idx = np.asarray(q, dtype=np.uint8)
    used = int(idx.max()) + 1
    cols = [(pal[3 * i], pal[3 * i + 1], pal[3 * i + 2], alpha[i]) for i in range(used)]
    return cols, idx


def rgb565(r, g, b):
    return (r >> 3) << 11 | (g >> 2) << 5 | b >> 3


def picture(im, ppm, pivot, opaque=False, colors=256):
    cols, idx = quantize(im, colors)
    h, w = idx.shape
    flags = 1 if opaque else 0
    out = bytearray(struct.pack("<HHHHHhhH", w, h, len(cols), flags, int(round(ppm * 4)),
                                int(round(pivot[0] * 4)), int(round(pivot[1] * 4)), 0))
    for r, g, b, a in cols:
        if opaque:
            a = 255
        out += bytes([r * a // 255, g * a // 255, b * a // 255, a])
    if opaque:
        for r, g, b, a in cols:
            out += struct.pack("<H", rgb565(r, g, b))
    out += idx.tobytes()
    while len(out) % 4:
        out.append(0)
    return out


# HC_LOSSLESS_CROP_V1: compact only transparent margins; terrain stays direct.
def _hc_crop_picture_blob(blob, name):
    if name.startswith(('GROUND_', 'SURF_', 'PIT_', 'SKY', 'DIRT',
                        'SNOW', 'GRASS', 'SLIME', 'FIRE')):
        return blob
    w, h, n, flags, ppm4, px4, py4, pad = struct.unpack_from('<HHHHHhhH', blob)
    if flags or pad or not w or not h or n > 256:
        return blob
    pal = blob[16:16 + 4*n]
    clear = [i for i in range(n) if pal[i*4 + 3] == 0]
    if not clear or any(pal[i*4:i*4+3] != b'\0\0\0' for i in clear):
        return blob
    visible = {i for i in range(n) if pal[i*4+3] != 0}
    px = blob[16+4*n:16+4*n+w*h]
    if len(px) != w*h:
        raise ValueError('Hill Climb image pixel data truncated')
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = px[y*w:(y+1)*w]
        for x, idx in enumerate(row):
            if idx in visible:
                x0 = min(x0, x); y0 = min(y0, y)
                x1 = max(x1, x); y1 = max(y1, y)
    if x1 < x0:
        return blob
    cw, ch = x1-x0+1, y1-y0+1
    if max(x0, y0, cw, ch) > 255:
        return blob
    hdr = bytearray(blob[:16])
    struct.pack_into('<H', hdr, 6, flags | 2)
    struct.pack_into('<H', hdr, 14, clear[0])
    out = hdr + pal + bytes((x0, y0, cw, ch))
    for y in range(y0, y0+ch):
        out += px[y*w+x0:y*w+x0+cw]
    while len(out) % 4:
        out.append(0)
    return out if len(out) < len(blob) else blob


# HC_PALETTE_COMPACT_V1: preserve exact premultiplied RGBA/RGB565.
def _hc_compact_palette_blob(blob):
    """Drop unused/duplicate colour entries in one already-cropped HCRD record.

    Works on both ordinary and HC_LOSSLESS_CROP_V1 records; the latter's
    outside-transparent index is remapped too. Never alters logical pixels.
    """
    w, h, n, flags, ppm4, px4, py4, pad = struct.unpack_from('<HHHHHhhH', blob)
    assert flags & ~3 == 0 and 1 <= n <= 256
    rgba = [blob[16 + 4*i:20 + 4*i] for i in range(n)]
    pal_end = 16 + 4*n
    rgb565 = [blob[pal_end + 2*i:pal_end + 2*i + 2] for i in range(n)] if flags & 1 else None
    at = pal_end + (2*n if flags & 1 else 0)
    if flags & 2:
        x0, y0, cw, ch = blob[at:at + 4]
        at += 4
    else:
        cw, ch = w, h
    px = blob[at:at + cw*ch]
    keep = set(px)
    if flags & 2:
        keep.add(pad)  # original transparent colour outside crop bounds
    unique, remap, colors = {}, {}, []
    for old in sorted(keep):
        key = rgba[old] + (rgb565[old] if rgb565 is not None else b'')
        if key not in unique:
            unique[key] = len(colors)
            colors.append(old)
        remap[old] = unique[key]
    hdr = bytearray(blob[:16])
    struct.pack_into('<H', hdr, 4, len(colors))
    if flags & 2:
        struct.pack_into('<H', hdr, 14, remap[pad])
    out = hdr + b''.join(rgba[i] for i in colors)
    if rgb565 is not None:
        out += b''.join(rgb565[i] for i in colors)
    if flags & 2:
        out += bytes((x0, y0, cw, ch))
    out += bytes(remap[i] for i in px)
    out += bytes((-len(out)) % 4)
    return bytes(out) if len(out) < len(blob) else bytes(blob)

# HC_MENU_TILES_EXPERIMENT_V1: lossless local-palette 8x8 menu images (16-bit tile offsets).
def _hc_menu_tile_blob(blob, name):
    if not (name in {'MENU_BG', 'LOGO', 'LOCK', 'START'} or
            name.startswith(('STAGE_', 'ICON_', 'UP_', 'THUMB_V', 'THUMB_S'))):
        return blob
    w, h, n, flags, _, _, _, _ = struct.unpack_from('<HHHHHhhH', blob)
    if flags & 4:
        raise ValueError('tile encoding applied twice')
    p = 16 + n*4 + (2*n if flags&1 else 0)
    if flags&2:
        cx, cy, cw, ch=blob[p:p+4]
        p+=4
    else:
        cw, ch=w,h
    pixels=blob[p:p+cw*ch]
    if len(pixels)!=cw*ch:
        raise ValueError('truncated input for menu tiling')
    cols=(cw+7)//8;rows=(ch+7)//8
    offsets=[];tiles=[];length=0
    for ty in range(rows):
        for tx in range(cols):
            tw=min(8,cw-8*tx);th=min(8,ch-8*ty)
            raw=b''.join(pixels[(8*ty+dy)*cw+8*tx:(8*ty+dy)*cw+8*tx+tw] for dy in range(th))
            cmap={}; pal=[]; codes=[]
            for val in raw:
                if val not in cmap:
                    cmap[val]=len(pal);pal.append(val)
                codes.append(cmap[val])
            count=len(pal)
            if count<=16:
                bits=0 if count==1 else 1 if count==2 else 2 if count<=4 else 4
                t=bytearray((count,))+bytearray(pal)
                if bits:
                    for k in range(0,len(codes),8//bits):
                        v=0
                        for j,c in enumerate(codes[k:k+8//bits]):
                            v|=c<<(8-bits*(j+1))
                        t.append(v)
                if len(t)>=1+len(raw):t=b'\0'+raw
            else:
                t=b'\0'+raw
            offsets.append(length);tiles.append(bytes(t));length+=len(t)
    if length>=65536:return blob
    data=b''.join(struct.pack('<H',o) for o in offsets)+b''.join(tiles)
    out=bytearray(blob[:p])+data
    struct.pack_into('<H',out,6,flags|4)
    while len(out)%4:out.append(0)
    return bytes(out) if len(blob)-len(out)>=96 and len(out)*100<=len(blob)*96 else blob

def sized(im, how, crop):
    if crop == "trim":
        im = im.crop(im.getbbox())
    elif crop:
        im = im.crop(crop)
    kind = how[0]
    if kind == "ppm":
        k = min(1.0, VPPM / how[1])
        return premul_resize(im, max(1, round(im.width * k)), max(1, round(im.height * k))), k, VPPM if k < 1 else how[1]
    if kind == "w":
        k = how[1] / im.width
    elif kind == "h":
        k = how[1] / im.height
    else:
        w, h = how[1], how[2]
        if w == 1:   # a sky: the middle column
            col = im.crop((im.width // 2, 0, im.width // 2 + 1, im.height))
            return col.resize((1, h), Image.BILINEAR), 1, 0
        return premul_resize(im, w, h), w / im.width, 0
    return premul_resize(im, max(1, round(im.width * k)), max(1, round(im.height * k))), k, 0


def vehicle_pictures(ref):
    """The vehicles' pictures, kept at VPPM pixels a meter, and art.h's tables."""
    pics = []
    lines = ["/* Generated by tools/pack.py from tools/art.py: how the vehicles and Bill are drawn, in meters. */",
             "#ifndef HC_ART_H", "#define HC_ART_H", '#include "data.h"', ""]
    B = art.BILL
    body, head, leg = load(ref, B["body"]), load(ref, B["head"]), load(ref, B["leg"])
    bppm = math.dist(B["body_neck"], B["body_hips"]) / B["torso_m"]
    hb = head.getbbox()
    hppm = (hb[2] - hb[0]) / B["head_m"]
    lppm = leg.height / B["leg_m"]
    for name, im, ppm, piv in (("BILL_BODY", body, bppm, B["body_hips"]),
                               ("BILL_HEAD", head, hppm, ((hb[0] + hb[2]) / 2, (hb[1] + hb[3]) / 2)),
                               ("BILL_LEG", leg, lppm, B["leg_hip"])):
        k = VPPM / ppm
        pics.append((name, premul_resize(im, round(im.width * k), round(im.height * k)), VPPM,
                     (piv[0] * k, piv[1] * k), False))
    # the body picture's neck from its hips, Bill upright (meters, y up)
    lines.append(f"#define BILL_NECK_X {(B['body_neck'][0] - B['body_hips'][0]) / bppm:.3f}f")
    lines.append(f"#define BILL_NECK_Y {-(B['body_neck'][1] - B['body_hips'][1]) / bppm:.3f}f")
    lines.append("")
    lines.append("typedef struct {")
    lines.append("  uint8_t chassis, wheel, legs, ntubes, nlayers;")
    lines.append("  float wheel_r;                  /* the radius the wheel's picture is drawn for */")
    lines.append("  float tube[2][5];               /* from (x, y) to axle (index), colour 0xRRGGBB, radius */")
    lines.append("  float layer[2][4];              /* picture, its centre (x, y), in front (1) or behind */")
    lines.append("} VehArt;")
    lines.append(f"#define NVEH {len(art.VEHICLES)}")
    lines.append("static const VehArt veh_art[NVEH] = {")
    f = lambda x: f"{x:.3f}f"  # noqa: E731

    def sized_on(im, size):
        """A picture stretched to size (meters), at VPPM pixels a meter, pivot at its centre."""
        w, h = max(1, round(size[0] * VPPM)), max(1, round(size[1] * VPPM))
        return premul_resize(im, w, h), (w / 2, h / 2)

    for v in art.VEHICLES:
        cname, wname = "CH_" + v["id"].upper(), "WHEEL_" + v["id"].upper()
        cim = load(ref, v["chassis"])
        for (upic, at) in v.get("under", []):   # pictures the game puts behind the chassis' (its children at z -1)
            u = load(ref, upic)
            back = Image.new("RGBA", cim.size)
            back.alpha_composite(u, (round(at[0] - u.width / 2), round(at[1] - u.height / 2)))
            back.alpha_composite(cim)
            cim = back
        im, piv = sized_on(cim, v["size"])
        if v.get("trim"):
            im, piv = trimmed(im, piv)
        pics.append((cname, im, VPPM, piv, False))
        if "wheel_reuse" in v:   # the same tyre picture as another vehicle's
            wname = "WHEEL_" + v["wheel_reuse"].upper()
        else:
            wim = load(ref, v["wheel"])
            if "wheel_px" in v:   # a tyre cropped from a bigger picture: its radius in pixels is the wheel's 1.04 r
                side = wim.width / v["wheel_px"] * 1.04 * v["wheel_r"]
            else:
                side = v.get("wheel_side", 2.15) * v["wheel_r"]
            im, piv = sized_on(wim, (side, side * wim.height / wim.width))
            pics.append((wname, im, VPPM, piv, False))
        layers = []
        for (lpic, size, at, front) in v.get("layers", []):
            lname = "LAYER_" + lpic.split("/")[-1].split(".")[0].upper().replace("-", "_")
            im, piv = sized_on(load(ref, lpic), size)
            if v.get("trim"):
                im, piv = trimmed(im, piv)
            pics.append((lname, im, VPPM, piv, False))
            layers.append((lname, at, front))
        tubes = [(p[0], p[1], axi, col, r) for (p, axi, col, r) in v.get("tubes", [])]
        tb = ", ".join("{" + ", ".join([f(t[0]), f(t[1]), f(t[2]), f"{float(t[3])}f", f(t[4])]) + "}" for t in tubes) or "{0}"
        ly = ", ".join("{IMG_" + n + ", " + f(p[0]) + ", " + f(p[1]) + ", " + ("1" if fr else "0") + "}"
                       for n, p, fr in layers) or "{0}"
        lines.append(f"  {{IMG_{cname}, IMG_{wname}, {1 if v.get('legs') else 0}, {len(tubes)}, {len(layers)}, "
                     f"{f(v['wheel_r'])}, {{{tb}}}, {{{ly}}}}},   /* {v['name']} */")
    lines.append("};")
    lines.append("#endif")
    return pics, lines


def font_data(ttf, cap, weight, outline):
    up = 4
    for size in range(6, 400):
        f = ImageFont.truetype(ttf, size * up)
        try:
            f.set_variation_by_axes([weight])
        except Exception:
            pass
        bb = f.getbbox("H", anchor="ls")
        if -bb[1] >= cap * up:
            break
    o = int(round(outline * up))
    glyphs, data = [], bytearray()
    asc = desc = 0
    for c in range(32, 127):
        ch = chr(c)
        adv = round(f.getlength(ch) / up + outline)
        bb = f.getbbox(ch, anchor="ls")
        if bb[2] <= bb[0]:
            glyphs.append((0, 0, 0, 0, adv, len(data)))
            continue
        pad = o + 2 * up
        x0, y0 = pad - bb[0], pad - bb[1]
        x0 += (-x0) % up
        y0 += (-y0) % up
        w, h = x0 + bb[2] + pad, y0 + bb[3] + pad
        w += (-w) % up
        h += (-h) % up
        fill = Image.new("L", (w, h))
        ImageDraw.Draw(fill).text((x0, y0), ch, font=f, fill=255, anchor="ls")
        line = Image.new("L", (w, h))
        ImageDraw.Draw(line).text((x0, y0), ch, font=f, fill=255, anchor="ls", stroke_width=o)
        fa = np.asarray(fill, np.float32).reshape(h // up, up, w // up, up).mean(axis=(1, 3)) / 255
        la = np.asarray(line, np.float32).reshape(h // up, up, w // up, up).mean(axis=(1, 3)) / 255
        fq = np.clip(np.round(fa * 15), 0, 15).astype(np.uint8)
        lq = np.clip(np.round(la * 15), 0, 15).astype(np.uint8)
        ys, xs = np.nonzero(lq)
        if not len(ys):
            glyphs.append((0, 0, 0, 0, adv, len(data)))
            continue
        y_a, y_b, x_a, x_b = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        fq, lq = fq[y_a:y_b, x_a:x_b], lq[y_a:y_b, x_a:x_b]
        gw, gh = x_b - x_a, y_b - y_a
        ox, oy = x_a - x0 // up, y_a - y0 // up
        if c < 127:
            asc, desc = max(asc, -oy), max(desc, gh + oy)
        glyphs.append((gw, gh, ox, oy, adv, len(data)))
        data += (fq << 4 | lq).astype(np.uint8).tobytes()
    out = bytearray(struct.pack("<BBBB", 32, len(glyphs), asc + desc, asc))
    for gw, gh, ox, oy, adv, off in glyphs:
        out += struct.pack("<BBbbBxxxI", gw, gh, ox, oy, adv, off)
    out += data
    while len(out) % 4:
        out.append(0)
    return _hc_pack_font_blob(out)


# HC_FONT_ROW_RLE_V1: lossless per-glyph row PackBits, direct row offsets.
def _hc_rle_font_row(raw):
    out = bytearray()
    i = 0
    while i < len(raw):
        j = i + 1
        while j < len(raw) and raw[j] == raw[i] and j - i < 128:
            j += 1
        if j - i >= 3:
            out += bytes((0x80 | (j-i-1), raw[i]))
            i = j
            continue
        start = i
        i += j - i
        while i < len(raw) and i - start < 128:
            j = i + 1
            while j < len(raw) and raw[j] == raw[i] and j - i < 128:
                j += 1
            if j - i >= 3:
                break
            i += min(j-i, 128-(i-start))
        out.append(i-start-1)
        out += raw[start:i]
    return bytes(out)


def _hc_pack_font_blob(blob):
    """Existing font/glyph structs, except pad[0]=1 marks compressed glyphs.

    Each compressed glyph payload starts with h+1 little-endian uint16 row
    offsets, followed by the PackBits streams; other glyphs stay direct.
    """
    first, count, height, ascent = struct.unpack_from('<BBBB', blob)
    base = 4 + 12*count
    out = bytearray(blob[:base])
    for j in range(count):
        p = 4 + 12*j
        w, h = struct.unpack_from('<BB', blob, p)
        oldoff = struct.unpack_from('<I', blob, p+8)[0]
        raw = bytes(blob[base+oldoff:base+oldoff+w*h])
        assert len(raw) == w*h and not any(blob[p+5:p+8])
        struct.pack_into('<I', out, p+8, len(out)-base)
        if w and h:
            rows = [_hc_rle_font_row(raw[y*w:(y+1)*w]) for y in range(h)]
            starts = [0]
            for row in rows:
                starts.append(starts[-1] + len(row))
            if starts[-1] <= 65535:
                candidate = struct.pack('<'+'H'*(h+1), *starts) + b''.join(rows)
                if len(candidate) < len(raw):
                    out[p+5] = 1
                    out += candidate
                    continue
        out += raw
    out += bytes((-len(out)) % 4)
    return out


def icon(ref):
    """The app's icon (55 x 56): the Hill Climber on a hill under the game's sky, in a rounded square."""
    k = 4
    W, H = 55 * k, 56 * k
    sky = load(ref, "Sky-bg.png").convert("RGB").resize((1, 32))
    im = Image.new("RGBA", (W, H))
    for y in range(H):
        im.paste(sky.getpixel((0, y * 32 // H)) + (255,), (0, y, W, y + 1))
    d = ImageDraw.Draw(im)
    pts = [(x, int(H * 0.74 - 18 * k * math.sin(x / W * 2.6 + 0.4))) for x in range(0, W + 1, 4)]
    d.polygon(pts + [(W, H), (0, H)], fill=(84, 52, 28, 255))
    d.line(pts, fill=(126, 198, 43, 255), width=5 * k)
    car = load(ref, "Mission-vehicle-jeep.png").crop((0, 0, 206, 145))
    car = car.resize((52 * k, round(52 * k * car.height / car.width)), Image.LANCZOS).rotate(8, Image.BICUBIC, expand=True)
    im.alpha_composite(car, ((W - car.width) // 2, H - car.height - 6 * k))
    head = load(ref, "Driver-head.png")
    head = head.resize((19 * k, 19 * k), Image.LANCZOS)
    im.alpha_composite(head, (W // 2 - 11 * k, 8 * k))
    mask = Image.new("L", (W, H))
    ImageDraw.Draw(mask).rounded_rectangle((0, 0, W - 1, H - 1), radius=12 * k, fill=255)
    im.putalpha(Image.fromarray(np.minimum(np.asarray(im)[..., 3], np.asarray(mask))))
    im.resize((55, 56), Image.LANCZOS).save(os.path.join(SRC, "icon.png"))


def main():
    ref = sys.argv[1]
    pics = []
    colors = {}
    for name, file, crop, how, pivot, *more in PICTURES:
        im, k, ppm = sized(load(ref, file), how, crop)
        if pivot == "c":
            pv = (im.width / 2, im.height / 2)
        else:
            pv = (pivot[0] * k, pivot[1] * k)
        opaque = name.startswith(("GROUND_", "SKY", "MENU_BG", "STAGE_", "PIT_", "THUMB_S"))
        pics.append((name, im, ppm, pv, opaque))
        if more:
            colors[name] = more[0]
    vp, art_lines = vehicle_pictures(ref)
    pics += vp
    blobs = [_hc_menu_tile_blob(_hc_compact_palette_blob(_hc_crop_picture_blob(picture(im, ppm, pv, opaque, colors.get(name, 256)), name)), name)
             for name, im, ppm, pv, opaque in pics]
    ttf = os.path.join(ref, "fonts", "Teko.ttf")
    fonts = [font_data(ttf, cap, wt, ol) for _, cap, wt, ol in FONTS]
    head = 16 + 4 * (len(blobs) + len(fonts))
    offs, at = [], head
    for b in blobs + fonts:
        offs.append(at)
        at += len(b)
    out = bytearray(b"HCRD" + struct.pack("<III", len(blobs), len(fonts), 0))
    out += b"".join(struct.pack("<I", o) for o in offs)
    for b in blobs + fonts:
        out += b
    with open(os.path.join(SRC, "data.bin"), "wb") as f:
        f.write(out)
    h = ["/* Generated by tools/pack.py: what src/data.bin holds. */", "#ifndef HC_DATA_H", "#define HC_DATA_H",
         "#include <stdint.h>", "", "enum {"]
    h += [f"  IMG_{name}," for name, *_ in pics]
    h += ["  IMG_COUNT", "};", "enum {"]
    h += [f"  {name}," for name, *_ in FONTS]
    h += ["  FONT_COUNT", "};", "#endif", ""]
    with open(os.path.join(SRC, "data.h"), "w") as f:
        f.write("\n".join(h))
    with open(os.path.join(SRC, "art.h"), "w") as f:
        f.write("\n".join(art_lines) + "\n")
    print(f"data.bin: {len(out)} bytes, {len(blobs)} pictures, {len(fonts)} fonts")
    for (name, im, *_), b in sorted(zip(pics, blobs), key=lambda x: -len(x[1]))[:8]:
        print(f"  {name}: {im.width}x{im.height}, {len(b)} bytes")


if __name__ == "__main__":
    main()
