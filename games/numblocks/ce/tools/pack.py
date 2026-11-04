#!/usr/bin/env python3
"""Packs Minecraft 1.8.8's pictures for NumBlocks on the TI-84 Plus CE into src/data.c, src/data.h and src/gfx.c.

    python3 tools/pack.py CLIENT_JAR

CLIENT_JAR is the official 1.8.8 client (client.jar); only its pictures are read. The CE draws with 256 colours:
one palette for everything (the textures, the screens, the icons), chosen here, 1 to 15 left to the sky. Each
texture is 16 x 16, a byte a texel (0: see-through). The 3D view shades a texel through a row of shade_cm (32
levels of light, from full down to 0.027, 0.89 apart) and the fog through rows made at run time with cube (the
nearest colour of 16 x 16 x 16 points, closer together in the dark)."""
import io
import os
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
CE = os.path.dirname(HERE)
NB_TOOLS = os.path.join(CE, '..', 'tools')
sys.path.insert(0, NB_TOOLS)
import pack as nbpack  # noqa: E402  NumBlocks' own packer: the jar, textures, icons, font

N_SHADE = 32
SHADE_STEP = 0.89
FIRST_STATIC = 16          # palette 0: black (see-through in sprites), 1-15: the sky's, set at run time

# ---------------------------------------------------------------------------------------------- blocks
# name, textures (one, or top/side/bottom/front), model, options
# models: none, cube, cross (plants), liquid, leaves (a cube, its holes black: Fast graphics), glass (see-through
# texels), torch (a cross of the torch's texture)
# opts: hard (hardness), tool, level (pickaxe level to get a drop), light, drop (item name, None: itself, '': nothing),
#       tint ('grass', 'foliage', 'spruce', 'birch': the biome's colour on the tinted faces), solid (default: cube-like)
BLOCKS = []


def blk(name, tex=None, model='cube', **o):
    BLOCKS.append(dict(name=name, tex=tex, model=model, **o))


def tsb(top, side, bottom=None, front=None):
    d = {'top': top, 'side': side, 'bottom': bottom or top}
    if front:
        d['front'] = front
    return d


blk('AIR', model='none', hard=0)
blk('STONE', 'stone', hard=1.5, tool='pickaxe', drop='COBBLESTONE')
blk('GRASS', tsb('grass_top', '@grass_side', 'dirt'), tint='grass', hard=0.6, tool='shovel', drop='DIRT')
blk('DIRT', 'dirt', hard=0.5, tool='shovel')
blk('COBBLESTONE', 'cobblestone', hard=2.0, tool='pickaxe')
blk('PLANKS_OAK', 'planks_oak', hard=2.0, tool='axe')
blk('PLANKS_SPRUCE', 'planks_spruce', hard=2.0, tool='axe')
blk('PLANKS_BIRCH', 'planks_birch', hard=2.0, tool='axe')
blk('SAPLING_OAK', 'sapling_oak', 'cross', hard=0)
blk('SAPLING_SPRUCE', 'sapling_spruce', 'cross', hard=0)
blk('SAPLING_BIRCH', 'sapling_birch', 'cross', hard=0)
blk('BEDROCK', 'bedrock', hard=-1)
blk('WATER', 'water_still', 'liquid', hard=-1)
blk('LAVA', 'lava_still', 'liquid', hard=-1, light=15)
blk('SAND', 'sand', hard=0.5, tool='shovel')
blk('GRAVEL', 'gravel', hard=0.6, tool='shovel')
blk('GOLD_ORE', 'gold_ore', hard=3.0, tool='pickaxe', level=2)
blk('IRON_ORE', 'iron_ore', hard=3.0, tool='pickaxe', level=1)
blk('COAL_ORE', 'coal_ore', hard=3.0, tool='pickaxe', drop='COAL')
blk('LOG_OAK', tsb('log_oak_top', 'log_oak'), hard=2.0, tool='axe')
blk('LOG_SPRUCE', tsb('log_spruce_top', 'log_spruce'), hard=2.0, tool='axe')
blk('LOG_BIRCH', tsb('log_birch_top', 'log_birch'), hard=2.0, tool='axe')
blk('LEAVES_OAK', 'leaves_oak', 'leaves', tint='foliage', hard=0.2, drop='')
blk('LEAVES_SPRUCE', 'leaves_spruce', 'leaves', tint='spruce', hard=0.2, drop='')
blk('LEAVES_BIRCH', 'leaves_birch', 'leaves', tint='birch', hard=0.2, drop='')
blk('GLASS', 'glass', 'glass', hard=0.3, drop='')
blk('LAPIS_ORE', 'lapis_ore', hard=3.0, tool='pickaxe', level=1, drop='LAPIS')
blk('LAPIS_BLOCK', 'lapis_block', hard=3.0, tool='pickaxe', level=1)
blk('SANDSTONE', tsb('sandstone_top', 'sandstone_normal', 'sandstone_bottom'), hard=0.8, tool='pickaxe')
blk('TALL_GRASS', 'tallgrass', 'cross', tint='grass', hard=0, drop='')
blk('DEAD_BUSH', 'deadbush', 'cross', hard=0, drop='STICK')
blk('WOOL', 'wool_colored_white', hard=0.8)
blk('DANDELION', 'flower_dandelion', 'cross', hard=0)
blk('POPPY', 'flower_rose', 'cross', hard=0)
blk('GOLD_BLOCK', 'gold_block', hard=3.0, tool='pickaxe', level=2)
blk('IRON_BLOCK', 'iron_block', hard=5.0, tool='pickaxe', level=1)
blk('BRICKS', 'brick', hard=2.0, tool='pickaxe')
blk('BOOKSHELF', tsb('planks_oak', 'bookshelf'), hard=1.5, tool='axe')
blk('MOSSY_COBBLESTONE', 'cobblestone_mossy', hard=2.0, tool='pickaxe')
blk('OBSIDIAN', 'obsidian', hard=50.0, tool='pickaxe', level=3)
blk('TORCH', 'torch_on', 'torch', hard=0, light=14)
blk('CHEST', {'top': '@chest_top', 'side': '@chest_side', 'front': '@chest_front', 'bottom': '@chest_top'},
    hard=2.5, tool='axe')
blk('DIAMOND_ORE', 'diamond_ore', hard=3.0, tool='pickaxe', level=2, drop='DIAMOND')
blk('DIAMOND_BLOCK', 'diamond_block', hard=5.0, tool='pickaxe', level=2)
blk('CRAFTING_TABLE', {'top': 'crafting_table_top', 'side': 'crafting_table_side', 'front': 'crafting_table_front',
                       'bottom': 'planks_oak'}, hard=2.5, tool='axe')
blk('FURNACE', {'top': 'furnace_top', 'side': 'furnace_side', 'front': 'furnace_front_off',
                'bottom': 'furnace_top'}, hard=3.5, tool='pickaxe')
blk('FURNACE_LIT', {'top': 'furnace_top', 'side': 'furnace_side', 'front': 'furnace_front_on',
                    'bottom': 'furnace_top'}, hard=3.5, tool='pickaxe', light=13, drop='FURNACE')
blk('REDSTONE_ORE', 'redstone_ore', hard=3.0, tool='pickaxe', level=2, drop='REDSTONE')
blk('CACTUS', tsb('cactus_top', 'cactus_side', 'cactus_bottom'), hard=0.4)
blk('SUGAR_CANE', 'reeds', 'cross', tint='grass', hard=0, drop='REEDS')
blk('STONE_BRICKS', 'stonebrick', hard=1.5, tool='pickaxe')
blk('COAL_BLOCK', 'coal_block', hard=5.0, tool='pickaxe')
blk('CLAY', 'clay', hard=0.6, tool='shovel', drop='CLAY_BALL')
blk('SNOW', 'snow', hard=0.2, tool='shovel')
blk('ICE', 'ice', 'glass', hard=0.5, drop='')
blk('GRASS_SNOWED', tsb('snow', 'grass_side_snowed', 'dirt'), hard=0.6, tool='shovel', drop='DIRT')

BNAMES = [b['name'] for b in BLOCKS]

# ---------------------------------------------------------------------------------------------- items
# an item id is a block (0 .. B_N - 1) or 128 + an item below. kind: none, pickaxe, axe, shovel, sword
ITEMS = []
TOOL_MAT = {'WOODEN': ('wood', 0, 59, 2, 0), 'STONE': ('stone', 1, 131, 4, 1), 'IRON': ('iron', 2, 250, 6, 2),
            'GOLDEN': ('gold', 0, 32, 12, 0), 'DIAMOND': ('diamond', 3, 1561, 8, 3)}
TOOL_DAMAGE = {'sword': 4, 'axe': 3, 'pickaxe': 2, 'shovel': 1}


def item(name, label, tex, kind='none', stack=64, dur=0, tier=0, dmg=1, fuel=0, speed=1):
    ITEMS.append(dict(name=name, label=label, tex=tex, kind=kind, stack=stack, dur=dur, tier=tier, dmg=dmg, fuel=fuel,
                      speed=speed))


item('STICK', 'Stick', 'stick', fuel=100)
item('COAL', 'Coal', 'coal', fuel=1600)
item('CHARCOAL', 'Charcoal', 'charcoal', fuel=1600)
item('IRON_INGOT', 'Iron Ingot', 'iron_ingot')
item('GOLD_INGOT', 'Gold Ingot', 'gold_ingot')
item('DIAMOND', 'Diamond', 'diamond')
item('REDSTONE', 'Redstone', 'redstone_dust')
item('LAPIS', 'Lapis Lazuli', 'dye_powder_blue')
item('FLINT', 'Flint', 'flint')
item('CLAY_BALL', 'Clay', 'clay_ball')
item('BRICK', 'Brick', 'brick')
item('REEDS', 'Sugar Canes', 'reeds')
item('APPLE', 'Apple', 'apple')
for m, (tex, lvl, uses, eff, bonus) in TOOL_MAT.items():
    lab = {'WOODEN': 'Wooden', 'GOLDEN': 'Golden'}.get(m, m.capitalize())
    for kind in ['sword', 'shovel', 'pickaxe', 'axe']:
        item(f'{m}_{kind.upper()}', f'{lab} {kind.capitalize()}', f'{tex}_{kind}', kind, stack=1, dur=uses, tier=lvl,
             dmg=1 + TOOL_DAMAGE[kind] + bonus, fuel=200 if m == 'WOODEN' else 0, speed=eff)
INAMES = [x['name'] for x in ITEMS]
IT0 = 128


def iid(n):
    """an item id from a block or item name"""
    if n in BNAMES:
        return BNAMES.index(n)
    return IT0 + INAMES.index(n)


# ---------------------------------------------------------------------------------------------- recipes
# (output, count, rows, keys); 'G_PLANKS' and 'G_LOG' match any kind
RECIPES = []


def shaped(out, n, rows, keys):
    RECIPES.append((out, n, rows, keys))


for m, x in [('WOODEN', 'G_PLANKS'), ('STONE', 'COBBLESTONE'), ('IRON', 'IRON_INGOT'), ('GOLDEN', 'GOLD_INGOT'),
             ('DIAMOND', 'DIAMOND')]:
    shaped(f'{m}_PICKAXE', 1, ['XXX', ' # ', ' # '], {'X': x, '#': 'STICK'})
    shaped(f'{m}_SHOVEL', 1, ['X', '#', '#'], {'X': x, '#': 'STICK'})
    shaped(f'{m}_AXE', 1, ['XX', 'X#', ' #'], {'X': x, '#': 'STICK'})
    shaped(f'{m}_SWORD', 1, ['X', 'X', '#'], {'X': x, '#': 'STICK'})
for b, it in [('GOLD_BLOCK', 'GOLD_INGOT'), ('IRON_BLOCK', 'IRON_INGOT'), ('DIAMOND_BLOCK', 'DIAMOND'),
              ('LAPIS_BLOCK', 'LAPIS'), ('COAL_BLOCK', 'COAL')]:
    shaped(b, 1, ['###', '###', '###'], {'#': it})
    shaped(it, 9, ['#'], {'#': b})
shaped('CHEST', 1, ['###', '# #', '###'], {'#': 'G_PLANKS'})
shaped('FURNACE', 1, ['###', '# #', '###'], {'#': 'COBBLESTONE'})
shaped('CRAFTING_TABLE', 1, ['##', '##'], {'#': 'G_PLANKS'})
shaped('SANDSTONE', 1, ['##', '##'], {'#': 'SAND'})
shaped('STONE_BRICKS', 4, ['##', '##'], {'#': 'STONE'})
shaped('BRICKS', 1, ['##', '##'], {'#': 'BRICK'})
shaped('CLAY', 1, ['##', '##'], {'#': 'CLAY_BALL'})
for n in ['OAK', 'SPRUCE', 'BIRCH']:
    shaped(f'PLANKS_{n}', 4, ['#'], {'#': f'LOG_{n}'})
shaped('STICK', 4, ['#', '#'], {'#': 'G_PLANKS'})
shaped('TORCH', 4, ['X', '#'], {'X': 'COAL', '#': 'STICK'})
shaped('TORCH', 4, ['X', '#'], {'X': 'CHARCOAL', '#': 'STICK'})
SMELT = [('IRON_ORE', 'IRON_INGOT'), ('GOLD_ORE', 'GOLD_INGOT'), ('DIAMOND_ORE', 'DIAMOND'), ('SAND', 'GLASS'),
         ('COBBLESTONE', 'STONE'), ('CLAY_BALL', 'BRICK'), ('COAL_ORE', 'COAL'), ('REDSTONE_ORE', 'REDSTONE'),
         ('LAPIS_ORE', 'LAPIS'), ('LOG_OAK', 'CHARCOAL'), ('LOG_SPRUCE', 'CHARCOAL'), ('LOG_BIRCH', 'CHARCOAL'),
         ('CACTUS', 'CACTUS')]
SMELT = [s for s in SMELT if s[0] != s[1]]


def fuel_of(i):
    if i >= IT0:
        return ITEMS[i - IT0]['fuel']
    n = BNAMES[i]
    if n == 'COAL_BLOCK':
        return 16000
    if n.startswith('SAPLING_'):
        return 100
    if n.startswith(('PLANKS_', 'LOG_')) or n in ('CRAFTING_TABLE', 'CHEST', 'BOOKSHELF'):
        return 300
    return 0


# biomes and their grass and foliage colours (ColorizerGrass / ColorizerFoliage at their temperature and rain)
BIOMES = [('OCEAN', 0.5, 0.5), ('PLAINS', 0.8, 0.4), ('DESERT', 2.0, 0.0), ('EXTREME_HILLS', 0.2, 0.3),
          ('FOREST', 0.7, 0.8), ('TAIGA', 0.25, 0.8), ('BEACH', 0.8, 0.4), ('BIRCH_FOREST', 0.6, 0.6),
          ('COLD_TAIGA', -0.5, 0.4)]
# the textures that change with the biome get one copy per tint (TINTS: which biomes share one)
TINT_OF_BIOME = {'OCEAN': 0, 'PLAINS': 0, 'DESERT': 0, 'BEACH': 0, 'FOREST': 1, 'BIRCH_FOREST': 1, 'EXTREME_HILLS': 2,
                 'TAIGA': 2, 'COLD_TAIGA': 3}
N_TINTS = 4
TINT_BIOME = ['PLAINS', 'FOREST', 'EXTREME_HILLS', 'COLD_TAIGA']
FIXED_TINT = {'spruce': (0x61, 0x99, 0x61), 'birch': (0x80, 0xA7, 0x55)}


def pixels(im):
    return list(im.get_flattened_data() if hasattr(im, 'get_flattened_data') else im.getdata())


def colormaps(jar):
    grass = jar.image('colormap/grass').convert('RGB')
    foliage = jar.image('colormap/foliage').convert('RGB')

    def look(img, t, r):
        t = min(max(t, 0.0), 1.0)
        r = min(max(r, 0.0), 1.0) * t
        return img.getpixel((int((1 - t) * 255), int((1 - r) * 255)))
    out = []
    for b in TINT_BIOME:
        _, t, r = next(x for x in BIOMES if x[0] == b)
        out.append((look(grass, t, r), look(foliage, t, r)))
    return out


def tinted(im, k):
    o = Image.new('RGBA', im.size)
    o.putdata([(r * k[0] // 255, g * k[1] // 255, b * k[2] // 255, a) for r, g, b, a in pixels(im)])
    return o


def tex_image(jar, n):
    return nbpack.tex_image(jar, n)


def grass_side(jar, k):
    base = tex_image(jar, 'grass_side')
    over = tex_image(jar, 'grass_side_overlay')
    out = []
    for (r, g, b, a), (r2, g2, b2, a2) in zip(pixels(base), pixels(over)):
        if a2 >= 128:
            out.append((r2 * k[0] // 255, g2 * k[1] // 255, b2 * k[2] // 255, 255))
        else:
            out.append((r, g, b, a))
    o = Image.new('RGBA', (16, 16))
    o.putdata(out)
    return o


def build_textures(jar):
    """every texture the blocks use, in order: (name, image, see-through); tinted ones N_TINTS times in a row.
    Returns the list and, per block, its six faces (-Y +Y -Z +Z -X +X) and which of them follow the biome."""
    tints = colormaps(jar)
    names, images, alpha, index = [], [], [], {}

    def add(key, make):
        if key in index:
            return index[key]
        index[key] = len(names)
        for im in make():
            names.append(key)
            images.append(im)
            alpha.append(any(a < 128 for _, _, _, a in pixels(im)))
        return index[key]

    faces, tintmask = [], []
    for b in BLOCKS:
        t = b['tex']
        if t is None:
            faces.append([0] * 6)
            tintmask.append(0)
            continue
        if isinstance(t, str):
            t = {'top': t, 'side': t, 'bottom': t}
        tint = b.get('tint')
        f6, mask = [], 0
        for k, face in enumerate(['bottom', 'top', 'side', 'side', 'side', 'side']):
            n = t[face]
            if face == 'side' and 'front' in t and k == 3:   # the front faces south (+Z) as placed by default
                n = t['front']
            tinted_face = tint and not (b['name'] == 'GRASS' and face == 'bottom')
            if tinted_face and tint in ('grass', 'foliage'):
                if n == '@grass_side':
                    i = add(n, lambda: [grass_side(jar, tints[j][0]) for j in range(N_TINTS)])
                else:
                    which = 0 if tint == 'grass' else 1
                    i = add(n + ':' + tint, lambda n=n, w=which: [tinted(tex_image(jar, n), tints[j][w])
                                                                  for j in range(N_TINTS)])
                mask |= 1 << k
            elif tinted_face:
                i = add(n + ':' + tint, lambda n=n: [tinted(tex_image(jar, n), FIXED_TINT[tint])])
            else:
                i = add(n, lambda n=n: [tex_image(jar, n)])
            f6.append(i)
        faces.append(f6)
        tintmask.append(mask)
    # the leaves of Fast graphics: tinted, their holes black
    for key, first in index.items():
        if key.startswith('leaves_'):
            n, tint = key.split(':')
            src = tex_image(jar, n)
            cols = [tints[j][1] for j in range(N_TINTS)] if tint in ('grass', 'foliage') else [FIXED_TINT[tint]]
            for j, k in enumerate(cols):
                px = [(r * k[0] // 255, g * k[1] // 255, b * k[2] // 255, 255) if a >= 128 else (0, 0, 0, 255)
                      for r, g, b, a in pixels(src)]
                im = Image.new('RGBA', (16, 16))
                im.putdata(px)
                images[first + j] = im
                alpha[first + j] = False
    # the cracks of a block being mined (destroy_stage_0..9): drawn over its texture
    for s in range(10):
        add(f'destroy_stage_{s}', lambda s=s: [tex_image(jar, f'destroy_stage_{s}')])
    return names, images, alpha, faces, tintmask, index


# ---------------------------------------------------------------------------------------------- the palette
GUI_FIXED = [(0, 0, 0), (255, 255, 255), (160, 160, 160), (63, 63, 63), (224, 224, 224), (255, 255, 160),
             (255, 255, 0), (63, 63, 0), (139, 139, 139), (55, 55, 55), (198, 198, 198), (85, 85, 85),
             (128, 255, 32), (255, 85, 85), (64, 64, 64), (32, 32, 32), (112, 112, 112), (192, 192, 192)]


def to_space(c):
    """colours compared in a space closer to the eye's: gamma-ish, weighted"""
    c = np.asarray(c, dtype=np.float64) / 255.0
    c = np.sqrt(c)
    return c * np.array([0.9, 1.25, 0.75])


def kmeans(samples, weights, fixed, k, iters=24):
    X = to_space(samples)
    F = to_space(np.array(fixed, dtype=np.float64)) if fixed else np.zeros((0, 3))
    rng = np.random.default_rng(1)
    free = k - len(F)
    # start from weighted picks
    p = weights / weights.sum()
    C = X[rng.choice(len(X), free, replace=False, p=p)]
    for _ in range(iters):
        allc = np.vstack([F, C])
        d = ((X[:, None, :] - allc[None, :, :]) ** 2).sum(-1)
        a = d.argmin(1)
        for j in range(free):
            m = a == len(F) + j
            if m.any():
                C[j] = (X[m] * weights[m, None]).sum(0) / weights[m].sum()
            else:
                C[j] = X[rng.choice(len(X), p=p)]
    allc = np.vstack([F, C])
    # back to RGB
    rgb = (allc / np.array([0.9, 1.25, 0.75])).clip(0, 1) ** 2 * 255
    return np.round(rgb).astype(int)


def nearest(pal, cols):
    """indices into pal (FIRST_STATIC..255) of the nearest colours"""
    P = to_space(pal[FIRST_STATIC:])
    X = to_space(np.asarray(cols, dtype=np.float64))
    out = []
    for i in range(0, len(X), 4096):
        d = ((X[i:i + 4096, None, :] - P[None, :, :]) ** 2).sum(-1)
        out += list(d.argmin(1) + FIRST_STATIC)
    return out


def shade_level(k):
    return SHADE_STEP ** (N_SHADE - 1 - k)


CUBE_LEVELS = [round(255 * (i / 15) ** 1.8) for i in range(16)]


# ---------------------------------------------------------------------------------------------- GUI pictures
# name, file, x, y, w, h
SPRITES = [
    ('hotbar', 'gui/widgets', 0, 0, 182, 22),
    ('hotbar_sel', 'gui/widgets', 0, 22, 24, 24),
    ('button', 'gui/widgets', 0, 66, 200, 20),
    ('button_hover', 'gui/widgets', 0, 86, 200, 20),
    ('button_off', 'gui/widgets', 0, 46, 200, 20),
    ('heart_bg', 'gui/icons', 16, 0, 9, 9),
    ('heart', 'gui/icons', 52, 0, 9, 9),
    ('heart_half', 'gui/icons', 61, 0, 9, 9),
    ('food_bg', 'gui/icons', 16, 27, 9, 9),
    ('food', 'gui/icons', 52, 27, 9, 9),
    ('food_half', 'gui/icons', 61, 27, 9, 9),
    ('bubble', 'gui/icons', 16, 18, 9, 9),
    ('bubble_pop', 'gui/icons', 25, 18, 9, 9),
    ('xp_bg', 'gui/icons', 0, 64, 182, 5),
    ('xp', 'gui/icons', 0, 69, 182, 5),
    ('crosshair', 'gui/icons', 0, 0, 16, 16),
    # the containers: their tops, and the player's inventory below (rows 79 on: the same in all three)
    ('inventory', 'gui/container/inventory', 0, 0, 176, 79),
    ('crafting_table', 'gui/container/crafting_table', 0, 0, 176, 79),
    ('furnace', 'gui/container/furnace', 0, 0, 176, 79),
    ('inv_bottom', 'gui/container/inventory', 0, 79, 176, 87),
    ('flame', 'gui/container/furnace', 176, 0, 14, 14),
    ('arrow', 'gui/container/furnace', 176, 14, 24, 17),
    ('creative', 'gui/container/creative_inventory/tab_items', 0, 0, 195, 136),
    ('scroller', 'gui/container/creative_inventory/tabs', 232, 0, 12, 15),
    ('dirt', 'gui/options_background', 0, 0, 16, 16),
    ('sun', 'environment/sun', 8, 8, 16, 16),
] + [(f'moon_{k}', 'environment/moon_phases', (k % 4) * 32 + 8, (k // 4) * 32 + 8, 16, 16) for k in range(8)]


def chest_image(jar):
    """the chest's top (its 27 slots), drawn above the inventory's bottom at row 80"""
    g54 = jar.image('gui/container/generic_54')
    chest = Image.new('RGBA', (176, 80))
    chest.paste(g54.crop((0, 0, 176, 71)), (0, 0))
    chest.paste(g54.crop((0, 126, 176, 135)), (0, 71))
    return chest


def sprite_images(jar):
    out = []
    for n, f, x, y, w, h in SPRITES:
        out.append((n, jar.image(f).crop((x, y, x + w, y + h))))
    out.append(('chest', chest_image(jar)))
    out.append(('steve', nbpack.steve_front(jar)))
    return out


def rle_with_rows(idx, w, h):
    d, rows = rle(idx, w, h)
    tab = []
    for r in rows:
        tab += [r & 255, r >> 8]
    return tab + d


def icon_images(jar, tex_names, tex_images):
    """16 x 16 icons: every block that can be held (as the inventory draws it), then every item"""
    icons = []
    for b in BLOCKS:
        if b['model'] == 'none' or b['name'] in ('WATER', 'LAVA', 'FURNACE_LIT', 'GRASS_SNOWED'):
            icons.append(None)
            continue
        t = b['tex']
        st = dict(tex=t, tint=b.get('tint'), model={'leaves': 'cube', 'glass': 'cube', 'torch': 'cross'}.get(
            b['model'], b['model']), meta=0)
        if b['name'] == 'GRASS':
            st['tex'] = {'top': 'grass_top', 'side': 'grass_side', 'bottom': 'dirt'}
            icons.append(grass_icon(jar))
            continue
        if b['model'] == 'leaves':
            st['tint'] = {'foliage': 'foliage'}.get(b.get('tint'), b.get('tint'))
        icons.append(nbpack.iso_icon(jar, st, None))
    for it in ITEMS:
        icons.append(nbpack.first_frame(jar.image('items/' + it['tex'])))
    return icons


def grass_icon(jar):
    # the inventory's grass block: the plains' green on top, the side with its tinted overlay
    k = nbpack.ICON_TINT['grass']
    top = tinted(tex_image(jar, 'grass_top'), k)
    side = grass_side(jar, k)
    st = dict(tex={'top': 'stone', 'side': 'stone', 'bottom': 'stone'}, tint=None, model='cube', meta=0)
    # draw with NumBlocks' iso_icon by swapping the pictures in
    orig = nbpack.tex_image
    pics = {'grass_top!': top, 'grass_side!': side}
    nbpack.tex_image = lambda j, n: pics[n] if n in pics else orig(j, n)
    try:
        st['tex'] = {'top': 'grass_top!', 'side': 'grass_side!', 'bottom': 'grass_side!'}
        return nbpack.iso_icon(jar, st, None)
    finally:
        nbpack.tex_image = orig


def rle(idx, w, h):
    """8-bit runs, a row at a time: n < 128: n + 1 bytes follow as they are; n >= 128: the next byte n - 126
    times. Returns the bytes and each row's offset."""
    data, rows = [], []
    for y in range(h):
        rows.append(len(data))
        row = idx[y * w:(y + 1) * w]
        x = 0
        lit = []
        while x < w:
            n = 1
            while x + n < w and n < 129 and row[x + n] == row[x]:
                n += 1
            if n >= 3:
                while lit:
                    chunk = lit[:128]
                    lit = lit[128:]
                    data += [len(chunk) - 1] + chunk
                data += [n - 2 + 128, row[x]]
                x += n
            else:
                lit.append(row[x])
                x += 1
        while lit:
            chunk = lit[:128]
            lit = lit[128:]
            data += [len(chunk) - 1] + chunk
    return data, rows


def carr(name, ctype, vals, per=24):
    s = [f'const {ctype} {name} = {{']
    for i in range(0, len(vals), per):
        s.append('  ' + ','.join(str(v) for v in vals[i:i + per]) + ',')
    s.append('};')
    return s


def main():
    jar = nbpack.Jar(sys.argv[1] if len(sys.argv) > 1 else
                     os.path.expanduser('~/Documents/Assets/gamesrc/numblocks/client.jar'))
    tnames, timages, talpha, faces, tintmask, tindex = build_textures(jar)
    sprites = sprite_images(jar)
    icons = icon_images(jar, tnames, timages)
    font_bits, font_w = nbpack.font_bits(jar)

    # --- the palette: textures at their face shades and darker, a little fog; the screens and icons
    samples, weights = [], []

    def addpx(im, w, shades=(1.0,)):
        for r, g, b, a in pixels(im.convert('RGBA')):
            if a >= 128:
                for s in shades:
                    samples.append((r * s, g * s, b * s))
                    weights.append(w)
    for i, im in enumerate(timages):
        if tnames[i].startswith('destroy_stage'):
            continue
        addpx(im, 1.0, (1.0, 0.79, 0.62))
        addpx(im, 0.35, (0.49, 0.35, 0.22, 0.12, 0.06))
        fog = (192, 216, 255)
        for r, g, b, a in pixels(im):
            if a >= 128:
                for f in (0.3, 0.6):
                    samples.append((r + (fog[0] - r) * f, g + (fog[1] - g) * f, b + (fog[2] - b) * f))
                    weights.append(0.25)
    for n, im in sprites:
        addpx(im, 0.6)
    for im in icons:
        if im is not None:
            addpx(im, 0.5)
    S = np.array(samples, dtype=np.float64)
    W = np.array(weights, dtype=np.float64)
    # thin it out: the same colours many times
    key = (np.round(S).astype(int) @ np.array([65536, 256, 1]))
    uniq, inv = np.unique(key, return_inverse=True)
    Wu = np.bincount(inv, weights=W)
    Su = np.stack([uniq >> 16, (uniq >> 8) & 255, uniq & 255], 1).astype(np.float64)
    pal_static = kmeans(Su, Wu, GUI_FIXED, 256 - FIRST_STATIC)
    pal = np.zeros((256, 3), dtype=int)
    pal[FIRST_STATIC:] = pal_static
    # the sky's entries start as a day sky
    for i in range(1, FIRST_STATIC):
        pal[i] = (120, 167, 255)

    def to_idx(im, see_through):
        px = pixels(im.convert('RGBA'))
        cols = [(r, g, b) for r, g, b, a in px]
        idx = nearest(pal, cols)
        return [0 if (see_through and a < 128) else i for i, (r, g, b, a) in zip(idx, px)]

    tex_idx = []
    for i, im in enumerate(timages):
        if tnames[i].startswith('destroy_stage'):
            # cracks: 1 where they darken
            tex_idx.append([1 if a >= 128 else 0 for r, g, b, a in pixels(im)])
        else:
            tex_idx.append(to_idx(im, talpha[i]))

    # shade rows: each colour at 32 levels of light
    shade = []
    for k in range(N_SHADE):
        s = shade_level(k)
        row = nearest(pal, [(r * s, g * s, b * s) for r, g, b in pal])
        row[0] = 0
        for j in range(1, FIRST_STATIC):
            row[j] = j
        shade += row
    cube = nearest(pal, [(CUBE_LEVELS[r], CUBE_LEVELS[g], CUBE_LEVELS[b]) for r in range(16) for g in range(16)
                         for b in range(16)])

    # --- data.c: what the 3D view and the game need at hand
    B_N = len(BLOCKS)
    model_ids = ['none', 'cube', 'cross', 'liquid', 'leaves', 'glass', 'torch']
    tools = [None, 'pickaxe', 'axe', 'shovel']
    out = ['/* Made by tools/pack.py from Minecraft 1.8.8\'s pictures: see data.h. */', '#include "data.h"', '']
    out += carr('pal_rgb[256 * 3]', 'uint8_t', [int(v) for c in pal for v in c])
    # the appvar NBCETEX (gfx/NBCETEX.bin): the textures, the shade rows and the colour cube, read in place
    # (and the 3D view's division tables: 2^28 / k for k 16..2560, 3 bytes each; f * 256 / d for d 1..15, f < d)
    recip = b''.join((min((1 << 28) // k, (1 << 24) - 1)).to_bytes(3, 'little') for k in range(16, 2561))
    ratio = bytes((f * 256 // d) if d and f < d else 0 for d in range(16) for f in range(16))
    texbin = bytes([v for t in tex_idx for v in t]) + bytes(shade) + bytes(cube) + recip + ratio
    assert len(texbin) <= 65505, len(texbin)
    os.makedirs(os.path.join(CE, 'gfx'), exist_ok=True)
    open(os.path.join(CE, 'gfx', 'NBCETEX.bin'), 'wb').write(texbin)
    out += carr('blk_tex[B_N][6]', 'uint8_t', [v for f in faces for v in f])
    out += carr('blk_tint[B_N]', 'uint8_t', tintmask)
    out += carr('blk_model[B_N]', 'uint8_t', [model_ids.index(b['model']) for b in BLOCKS])
    flags = []
    for b in BLOCKS:
        m = b['model']
        f = 0
        if m in ('cube', 'leaves'):
            f |= 1      # BF_OPAQUE: hides the faces next to it
        if m in ('cube', 'leaves', 'glass'):
            f |= 2      # BF_SOLID
        if m in ('cross', 'torch', 'glass'):
            f |= 4      # BF_ALPHA: see-through texels
        if m == 'liquid':
            f |= 8      # BF_LIQUID
        if b['name'] == 'CACTUS':
            f |= 2
        flags.append(f)
    out += carr('blk_flags[B_N]', 'uint8_t', flags)
    out += carr('blk_light[B_N]', 'uint8_t', [b.get('light', 0) for b in BLOCKS])
    opacity = []
    for b in BLOCKS:
        m = b['model']
        opacity.append(15 if m == 'cube' else 1 if m == 'leaves' else 3 if b['name'] == 'WATER' else
                       15 if b['name'] == 'LAVA' else 0)
    out += carr('blk_opacity[B_N]', 'uint8_t', opacity)
    out += carr('blk_hard[B_N]', 'uint8_t', [255 if b.get('hard', 1) < 0 else int(round(b.get('hard', 1) * 4))
                                              for b in BLOCKS])
    out += carr('blk_tool[B_N]', 'uint8_t', [tools.index(b.get('tool')) | b.get('level', 0) << 4 for b in BLOCKS])
    drops = []
    for b in BLOCKS:
        d = b.get('drop')
        drops.append(255 if d == '' or b['model'] == 'none' else iid(d) if d else BNAMES.index(b['name']))
    out += carr('blk_drop[B_N]', 'uint8_t', drops)
    out += carr('it_fuel[B_N + IT_N]', 'uint16_t', [fuel_of(i) for i in range(B_N)] +
                [fuel_of(IT0 + i) for i in range(len(ITEMS))])
    kinds = ['none', 'pickaxe', 'axe', 'shovel', 'sword']
    out += carr('it_kind[IT_N]', 'uint8_t', [kinds.index(x['kind']) | x['tier'] << 4 for x in ITEMS])
    out += carr('it_dur[IT_N]', 'uint16_t', [x['dur'] for x in ITEMS])
    out += carr('it_speed[IT_N]', 'uint8_t', [x['speed'] for x in ITEMS])
    out += carr('it_dmg[IT_N]', 'uint8_t', [x['dmg'] for x in ITEMS])
    out.append('const char *const it_label[IT_N] = {' + ', '.join(f'"{x["label"]}"' for x in ITEMS) + '};')
    labels = {'PLANKS_OAK': 'Oak Wood Planks', 'PLANKS_SPRUCE': 'Spruce Wood Planks',
              'PLANKS_BIRCH': 'Birch Wood Planks', 'SAPLING_OAK': 'Oak Sapling', 'SAPLING_SPRUCE': 'Spruce Sapling',
              'SAPLING_BIRCH': 'Birch Sapling', 'LOG_OAK': 'Oak Wood', 'LOG_SPRUCE': 'Spruce Wood',
              'LOG_BIRCH': 'Birch Wood', 'LEAVES_OAK': 'Oak Leaves', 'LEAVES_SPRUCE': 'Spruce Leaves',
              'LEAVES_BIRCH': 'Birch Leaves', 'LAPIS_ORE': 'Lapis Lazuli Ore', 'LAPIS_BLOCK': 'Lapis Lazuli Block',
              'TALL_GRASS': 'Grass', 'GRASS': 'Grass Block', 'WOOL': 'Wool', 'POPPY': 'Poppy',
              'GOLD_BLOCK': 'Block of Gold', 'IRON_BLOCK': 'Block of Iron', 'DIAMOND_BLOCK': 'Block of Diamond',
              'COAL_BLOCK': 'Block of Coal', 'BRICKS': 'Bricks', 'STONE_BRICKS': 'Stone Bricks',
              'MOSSY_COBBLESTONE': 'Moss Stone', 'CRAFTING_TABLE': 'Crafting Table', 'SUGAR_CANE': 'Sugar Canes',
              'GRASS_SNOWED': 'Grass Block', 'FURNACE_LIT': 'Furnace'}
    out.append('const char *const blk_label[B_N] = {' + ', '.join(
        '"' + labels.get(b['name'], b['name'].replace('_', ' ').title()) + '"' for b in BLOCKS) + '};')
    # recipes: out (item id, 2 bytes), count, w, h, then w*h ingredient ids (255 none; 254 any planks, 253 any log)
    rec = []
    for o, n, rows, keys in RECIPES:
        w, h = max(len(r) for r in rows), len(rows)
        rec += [iid(o) & 255, iid(o) >> 8, n, w, h]
        for r in rows:
            for c in r.ljust(w):
                k = keys.get(c)
                rec.append(255 if k is None else 254 if k == 'G_PLANKS' else 253 if k == 'G_LOG' else iid(k))
    rec.append(0)
    rec.append(0)
    rec.append(0)
    out += carr('recipes[]', 'uint8_t', rec)
    out += carr('smelt[]', 'uint8_t', [v for a, b in SMELT for v in (iid(a), iid(b))] + [255, 255])
    open(os.path.join(CE, 'src', 'data.c'), 'w').write('\n'.join(out) + '\n')

    # --- the appvar NBCEDAT (gfx/NBCEDAT.bin): the screens' pictures, the icons and the font, read in place
    #   "NBGX", the version, then the parts at the offsets gfx.h names: sprites (a 2-byte offset each, then each
    #   one's rows: a 2-byte offset a row, then its runs), widths, heights; icons the same, 16 x 16 each, and which
    #   icon each block and item has (255 none); the font's bits and widths
    blob = bytearray(b'NBGX') + bytes([1, 0])
    parts = {}

    def part(name, data):
        parts[name] = len(blob)
        blob.extend(data)
    spr_bytes, spr_off = [], []
    for n, im in sprites:
        w, h = im.size
        spr_off.append(len(spr_bytes))
        spr_bytes += rle_with_rows(to_idx(im, True), w, h)
    n_spr = len(sprites)
    hdr = 2 * n_spr
    part('SPR', b''.join((o + hdr).to_bytes(2, 'little') for o in spr_off) + bytes(spr_bytes))
    part('SPR_W', bytes(im.size[0] for n, im in sprites))
    part('SPR_H', bytes(im.size[1] for n, im in sprites))
    ic_bytes, ic_off, icon_of = [], [], []
    for im in icons:
        if im is None:
            icon_of.append(255)
            continue
        icon_of.append(len(ic_off))
        ic_off.append(len(ic_bytes))
        ic_bytes += rle_with_rows(to_idx(im, True), 16, 16)
    hdr = 2 * len(ic_off)
    part('ICON', b''.join((o + hdr).to_bytes(2, 'little') for o in ic_off) + bytes(ic_bytes))
    part('ICON_OF', bytes(icon_of))
    part('FONT', bytes(font_bits))
    part('FONT_W', bytes(font_w))
    assert len(blob) <= 65505, len(blob)
    os.makedirs(os.path.join(CE, 'gfx'), exist_ok=True)
    open(os.path.join(CE, 'gfx', 'NBCEDAT.bin'), 'wb').write(bytes(blob))
    g = ['/* Made by tools/pack.py: where the parts of the appvar NBCEDAT (gfx/NBCEDAT.bin) are. */',
         '#ifndef NBCE_GFX_H', '#define NBCE_GFX_H', '#include <stdint.h>',
         'extern const uint8_t *gfx;   /* the appvar\'s data */', f'#define GFX_SIZE {len(blob)}',
         f'#define GFX_VERSION 1']
    for k, v in parts.items():
        g.append(f'#define GFX_{k} {v}')
    g += ['#endif', '']
    open(os.path.join(CE, 'src', 'gfx.h'), 'w').write('\n'.join(g))
    print(f'NBCEDAT: {len(blob)} bytes (sprites {len(spr_bytes)}, icons {len(ic_bytes)})')

    # --- data.h
    h = ['/* Made by tools/pack.py: the blocks, items, textures and pictures of NumBlocks on the CE. */',
         '#ifndef NBCE_DATA_H', '#define NBCE_DATA_H', '#include <stdint.h>', '',
         f'#define NTEX {len(tex_idx)}', f'#define N_SHADE {N_SHADE}', f'#define FIRST_STATIC {FIRST_STATIC}',
         f'#define N_TINTS {N_TINTS}', f'#define B_N {B_N}', f'#define IT_N {len(ITEMS)}', f'#define IT0 {IT0}',
         'enum {']
    h += [f'  B_{b["name"]},' for b in BLOCKS]
    h += ['};', 'enum {']
    h += [f'  I_{x["name"]} = IT0 + {i},' for i, x in enumerate(ITEMS)]
    h += ['};', 'enum { M_NONE, M_CUBE, M_CROSS, M_LIQUID, M_LEAVES, M_GLASS, M_TORCH };',
          'enum { BF_OPAQUE = 1, BF_SOLID = 2, BF_ALPHA = 4, BF_LIQUID = 8 };',
          'enum { T_NONE, T_PICKAXE, T_AXE, T_SHOVEL, T_SWORD };',
          'enum { ' + ', '.join(f'BIOME_{b[0]}' for b in BIOMES) + ', N_BIOMES };',
          'extern const uint8_t tint_of_biome[N_BIOMES];',
          'extern const uint8_t pal_rgb[256 * 3];   /* 1-15: the sky\'s (set at run time) */',
          '/* the appvar NBCETEX: the textures (v * 16 + u; 0 see-through), each colour at each level of light',
          ' * (31 full), the nearest colour of a point [r][g][b] of 16 levels each (cube_levels) */',
          'extern const uint8_t *texdat;',
          '#define TEXDAT_RECIP (NTEX * 256 + N_SHADE * 256 + 4096)',
          '#define TEXDAT_RATIO (TEXDAT_RECIP + 2545 * 3)',
          '#define TEXDAT_SIZE (TEXDAT_RATIO + 256)',
          '#define tex_px ((const uint8_t (*)[256])texdat)',
          '#define shade_cm ((const uint8_t (*)[256])(texdat + NTEX * 256))',
          '#define pal_cube (texdat + NTEX * 256 + N_SHADE * 256)',
          '/* faces -Y +Y -Z +Z -X +X; blk_tint: the faces whose texture has N_TINTS copies, one per biome tint */',
          'extern const uint8_t blk_tex[B_N][6], blk_tint[B_N], blk_model[B_N], blk_flags[B_N], blk_light[B_N];',
          'extern const uint8_t blk_opacity[B_N], blk_hard[B_N], blk_tool[B_N], blk_drop[B_N];',
          '/* blk_hard: hardness x 4 (255: unbreakable); blk_tool: T_* | the level needed << 4; blk_drop: 255 none */',
          'extern const uint16_t it_fuel[B_N + IT_N];',
          'extern const uint8_t it_kind[IT_N], it_speed[IT_N], it_dmg[IT_N];   /* it_kind: T_* | tier << 4 */',
          'extern const uint16_t it_dur[IT_N];',
          'extern const char *const it_label[IT_N], *const blk_label[B_N];',
          'extern const uint8_t recipes[], smelt[];',
          'extern const uint8_t cube_levels[16];',
          '#include "gfx.h"',
          '/* the pictures, in the appvar NBCEDAT */',
          '#define font_bits (gfx + GFX_FONT)',
          '#define font_w (gfx + GFX_FONT_W)',
          '#define icon_of (gfx + GFX_ICON_OF)']
    for i, n in enumerate(tnames):
        if i == 0 or tnames[i - 1] != n:
            h.append(f'#define TX_{n.upper().replace("@", "").replace(":", "_")} {i}')
    for i, (n, im) in enumerate(sprites):
        h.append(f'#define SP_{n.upper()} {i}')
    h += ['', '#endif', '']
    open(os.path.join(CE, 'src', 'data.h'), 'w').write('\n'.join(h))
    # the tint of each biome, and the cube's levels
    open(os.path.join(CE, 'src', 'data.c'), 'a').write('\n'.join(
        carr('tint_of_biome[N_BIOMES]', 'uint8_t', [TINT_OF_BIOME[b[0]] for b in BIOMES]) +
        carr('cube_levels[16]', 'uint8_t', CUBE_LEVELS)) + '\n')
    print(f'{len(tex_idx)} textures, {B_N} blocks, {len(ITEMS)} items, {len(RECIPES)} recipes, '
          f'{len(icons)} icons')


if __name__ == '__main__':
    main()
