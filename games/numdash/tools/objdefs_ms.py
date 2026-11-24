"""Object catalogue of Geometry Dash Meltdown's and SubZero's levels (GD 2.1
objects), only in NumDash.nwa (ND_EXTRA_LEVELS): the mirror portals, the
2.1 pits, light blades, sludge, thick and small outline blocks, slopes,
text, and the levels' decoration (tile sets of riveted metal, rock strata,
bricks and planks, clouds, grass).

Hitboxes follow gmdkit's hitbox table (OpenGD's offsets); the art is drawn
here as tile programs, NumDash's own simple designs after the originals'
general shapes, in the objects' own GD colour channels.
"""
import math
from objdefs_dl import R, P, VG, seg, prog, radial, outline, outline_glow, NONE, SOLID, HAZARD, SPECIAL, F_PULSE, F_QUAD, B2, B1, T1, GL

OBJS = []


def obj(name, ids, hit, w, h, parts, special='NONE', edy=0, **extra):
    OBJS.append((name, ids, hit, w, h, special, edy, parts, extra))


def ngon(cx, cy, r, n=8, ct='BASE', a=1.0, rot=22.5):
    return P([(cx + r * math.cos(math.radians(rot + 360 * k / n)), cy + r * math.sin(math.radians(rot + 360 * k / n))) for k in range(n)], ct, a)


def chamfer(x0, y0, x1, y1, c, ct='BASE', a=1.0):
    """A rectangle with its corners cut by c."""
    return P([(x0 + c, y0), (x1 - c, y0), (x1, y0 + c), (x1, y1 - c), (x1 - c, y1), (x0 + c, y1), (x0, y1 - c), (x0, y0 + c)], ct, a)


def deco(name, ids, ops, layer=B1, ct='BASE', flags=0, **kw):
    obj(name, ids, NONE, 0, 0, [(prog('ms_' + name.lower(), ops), 0, 0, layer, ct, flags)], **kw)


# ---------------------------------------------------------------- hazards
def bumps(x0, x1, y, h, n, ct='BASE'):
    """n rounded bumps (trapezoids) on top of y between x0 and x1."""
    w = (x1 - x0) / n
    return [P([(x0 + k * w, y), (x0 + k * w + w * .25, y + h), (x0 + k * w + w * .75, y + h), (x0 + (k + 1) * w, y)], ct) for k in range(n)]


# black sludge (The Seven Seas' sea, Viking Arena's pits): a band with a wavy top
obj('SLUDGE', [919], HAZARD, 25, 6, [(prog('ms_sludge', [R(-15, -5, 15, 1.5)] + bumps(-15, 15, 1.5, 2.5, 4)), 0, 0, T1, 'BASE', 0)], base=1010)


def shards(spikes, y0, h):
    """Jagged black shards over a band that fades away below."""
    return [P(pts, 'BLACK') for pts in spikes] + [R(-15, y0, 15, y0 + 3, 'BLACK'), VG(-15, y0 - h, 15, y0, 'BLACK', 1, 0)]


# the 2.1 pits: as 368, 421, 422, 667 and 720, in their colour (black by default)
obj('PIT2_SQUARE', [1730], HAZARD, 9, 6, [(prog('ms_pit_square', [R(-15, -7.5, 15, 0), R(-12, 0, -4, 7.5), R(4, 0, 12, 7.5)]), 0, 0, T1, 'BASE', 0)], base=1010)
obj('PIT2_DOT', [1731], HAZARD, 2.4, 3.2, [(prog('ms_pit_dot', [R(-4, -3.75, 4, 3.75)]), 0, 0, T1, 'BASE', 0)], base=1010)


def cspike_low(name, ids):
    obj(name, ids, HAZARD, 6, 5.6, [(prog('ms_' + name.lower(), [P([(-15, -7), (0, 7), (15, -7)], 'BASE'), P([(-10.5, -5), (0, 2.5), (10.5, -5)], 'DETAIL')]),
                                     0, 0, T1, 'BASE', 0), ('glow_spike_small', 0, 0, GL, 'GLOW', 0)], edy=-9, detail=1)


cspike_low('CSPIKE_LOW', [217])


# saws
def star(r0, r1, n, ct='BASE', rot=0.0):
    """n spikes from radius r0 out to r1."""
    ops = []
    for k in range(n):
        a = math.radians(rot + 360 * k / n)
        h = math.pi / n
        ops.append(P([(r0 * math.cos(a - h), r0 * math.sin(a - h)), (r1 * math.cos(a), r1 * math.sin(a)), (r0 * math.cos(a + h), r0 * math.sin(a + h))], ct))
    return ops


def light_blade(name, ids, r, r0, r1, n):
    obj(name, ids, HAZARD, r, r, [(prog('ms_' + name.lower(), [ngon(0, 0, r0 + .5)] + star(r0, r1, n) + [ngon(0, 0, r0 * .55, ct='DETAIL'), ngon(0, 0, r0 * .3, ct='BLACK')]),
                                   0, 0, T1, 'BASE', 0)], shape='CIRCLE', anim='SAW', detail=1)


light_blade('LIGHT_BLADE_L', [678], 30.4, 28, 40, 16)
light_blade('LIGHT_BLADE_M', [679], 18.72, 18, 26, 12)
light_blade('LIGHT_BLADE_S', [680], 10.8, 11, 17, 8)
obj('OBLADE_SMALL', [188], HAZARD, 12.6, 12.6, [(prog('ms_oblade_s', star(3, 17, 4, rot=45) + [ngon(0, 0, 4.5, ct='BLACK'), ngon(0, 0, 2.5)]), 0, 0, T1, 'BASE', 0)],
    shape='CIRCLE', anim='SAW')
# black cogwheels: the 2.0 cog saws with the colours the other way round

# ---------------------------------------------------------------- blocks and slopes


def small_outline(name, ids, hit, sides):
    obj(name, ids, hit, 15 if hit else 0, 15 if hit else 0, [(prog('ms_' + name.lower(), outline(sides, 1.5, 7.5, 7.5)), 0, 0, T1, 'BASE', 0),
                                                            (prog('msg_' + name.lower(), outline_glow(sides, 7.5, 7.5, 4)), 0, 0, GL, 'GLOW', 0)])


small_outline('SMALL_OUT_TOP', [1154], NONE, 't')
TGLOW = [VG(-15, 15, 15, 21, 'GLOW', 0, .4), VG(-15, 6, 15, 12, 'GLOW', .4, 0)]
obj('THICK_OUT_TL', [1203], SOLID, 30, 30, [(prog('ms_thick_tl', [P([(-15, 12), (-12, 15), (15, 15), (15, 12)])]), 0, 0, T1, 'BASE', 0),
                                            (prog('msg_thick_t', TGLOW), 0, 0, GL, 'GLOW', 0)])
obj('THICK_OUT_TB', [1204], SOLID, 30, 30, [(prog('ms_thick_tb', [P([(-15, 12), (-12, 15), (12, 15), (15, 12)])]), 0, 0, T1, 'BASE', 0),
                                            ('prog:msg_thick_t', 0, 0, GL, 'GLOW', 0)])
obj('THICK_OUT_TOP', [1209], SOLID, 30, 30, [(prog('ms_thick_top', [R(-15, 12, 15, 15)]), 0, 0, T1, 'BASE', 0), ('prog:msg_thick_t', 0, 0, GL, 'GLOW', 0)])
deco('THICK_OUT_DOT', [1205], [R(-15, 12, -12, 15)], T1)
obj('INVIS_OUT_LINE', [1343], SOLID, 25, 3, [(prog('ms_invis_line', [R(-12.5, -.75, -3, .75), R(3, -.75, 12.5, .75)]), 0, 0, T1, 'BASE', 0)], anim='INVIS')
# outline slopes (2.1) as 665 and 666, solid or not
obj('OUT_SLOPE_B', [1338], SOLID, 30, 30, [('prog:o_slope', 0, 0, T1, 'BASE', 0), ('prog:og_slope', 0, 0, GL, 'GLOW', 0)], shape='SLOPE')
obj('OUT_SLOPE_DECO', [1758], NONE, 0, 0, [('prog:o_slope', 0, 0, T1, 'BASE', 0)], base=1)
obj('TRI_SLOPE', [1745], SOLID, 30, 30, [(prog('ms_tri_slope', [P([(-15, -15), (15, 15), (15, -15)]), P([(1, -15), (15, -1), (15, -15)], 'BASE_D')]), 0, 0, T1, 'BASE', 0)],
    shape='SLOPE')
obj('TRI_WSLOPE', [1748], SOLID, 60, 30, [(prog('ms_tri_wslope', [P([(-30, -15), (30, 15), (30, -15)])]), 0, 0, T1, 'BASE', 0)], shape='SLOPE')

# ---------------------------------------------------------------- mirror portals


def arc(cx, r, w, a0, a1, n, ct, a=1.0):
    """A band between radii r and r + w about (cx, 0), angles a0..a1 (degrees)."""
    ops = []
    for k in range(n):
        t0, t1 = math.radians(a0 + (a1 - a0) * k / n), math.radians(a0 + (a1 - a0) * (k + 1) / n)
        ops.append(P([(cx + r * math.cos(t0), r * math.sin(t0)), (cx + (r + w) * math.cos(t0), (r + w) * math.sin(t0)),
                      (cx + (r + w) * math.cos(t1), (r + w) * math.sin(t1)), (cx + r * math.cos(t1), r * math.sin(t1))], ct, a))
    return ops


def mirror_portal(name, ids, special, c1, c2):
    front = arc(-44, 44, 3, -62, 62, 5, c1) + arc(-44, 50, 2.5, -55, 55, 5, c2) + [ngon(-44 + 47.5 * math.cos(math.radians(s * 64)), 47.5 * math.sin(math.radians(s * 64)), 2.5, 6, c1)
                                                                                  for s in (-1, 1)]
    back = arc(-44, 38, 4, -60, 60, 5, 'BLACK', .55)
    obj(name, ids, SPECIAL, 44, 92, [(prog('ms_' + name.lower(), front), 0, 0, 'PORTAL_FRONT', 'WHITE', 0),
                                     (prog('msb_' + name.lower(), back), 0, 0, 'PORTAL_BACK', 'WHITE', 0)], special=special)


mirror_portal('MIRROR_ON', [45], 'MIRROR_ON', 'RAIN1', 'RAIN2')
mirror_portal('MIRROR_OFF', [46], 'MIRROR_OFF', 'GLOW_B', 'RAIN4')

# ---------------------------------------------------------------- text and markers
# a text object: its string is the level's text table entry (style arg)
obj('TEXT', [914], NONE, 0, 0, [], base=1)
# hidden anchors of camera edges (1964)

# ---------------------------------------------------------------- decoration: The Seven Seas
deco('EXMARK', [133], [R(-3.5, -6, 3.5, 22), R(-3.5, -22, 3.5, -14)], base=1006)
deco('ROUND_CLOUD_Q', [222], [ngon(15 * math.cos(math.radians(a)) - 7, 15 * math.sin(math.radians(a)) - 7, 9) for a in (0, 30, 60, 90)], base=1005)
deco('ROUND_CLOUD', [223], [ngon(24 * math.cos(math.radians(a)), 24 * math.sin(math.radians(a)), 10) for a in range(0, 360, 45)], base=1005)


def grass(blades, y0=-6):
    return [P([(x - w, y0), (x + lean, y0 + h), (x + w, y0)]) for (x, w, h, lean) in blades]


deco('GRASS1', [406], grass([(-4, 2, 7, -1), (0, 2, 10, 0), (4, 2, 7, 1)]), base=1007)
deco('GRASS4', [414], grass([(-6, 2, 6, -1), (-2, 2, 10, 0), (2, 2, 8, 1), (6, 2, 5, 1)]), base=1007)
deco('GRASS5', [906], grass([(-12, 2, 6, -2), (-7, 2.5, 11, -1), (-2, 2.5, 14, 0), (3, 2.5, 10, 1), (8, 2, 12, 1), (12, 2, 6, 2)]), base=1007)
deco('TUFTS', [1048], [P([(-11, -3), (-7, 2), (-3, -3)]), P([(3, -3), (7, 2), (11, -3)])], T1)
deco('LIGHT_TRIANGLE', [693, 697], [P([(-15, -15), (15, 15), (15, -15)])], B2, base=1)
# planks of the ship: framed panels and brick piles (lighter detail)
deco('PANEL_LONG', [873], [R(-12.5, -5, 12.5, 5), R(-10.5, -3, 10.5, 3, 'DETAIL')], B2, detail=1012)
deco('PANEL_SMALL', [874], [R(-5, -5, 5, 5), R(-3, -3, 3, 3, 'DETAIL')], B2, detail=1012)


def small_bricks(rows):
    """Rows of 8 x 4 bricks (each row: the y of its bottom and the x of its bricks' left ends)."""
    ops = []
    for (y, xs) in rows:
        for x in xs:
            ops += [R(x, y, x + 8, y + 4), R(x + 1, y + 2.5, x + 7, y + 3.3, 'DETAIL')]
    return ops


deco('BRICKS9_COL', [885], small_bricks([(8, [-8]), (3, [-12, -3]), (-2, [-8, 1]), (-7, [-12, -3]), (-12, [-8])]), B2, detail=1012)
deco('BRICKS9_ROWS', [1062], small_bricks([(1, [-12, -3, 6]), (-4, [-8, 1])]), B2, detail=1012)
deco('BRICKS9_PILE', [1159], small_bricks([(6, [-12, -3, 6]), (1, [-8, 1]), (-4, [-12, -3, 6]), (-9, [-8, 1])]), B2, detail=1012)
deco('BRICKS9_TAPER', [1160], small_bricks([(6, [-12, -3, 6]), (1, [-8, 1]), (-4, [-3]), (-9, [-6])]), B2, detail=1012)
deco('BRICKS9_STEP', [1161], small_bricks([(6, [-12, -3, 6]), (1, [-12, -3]), (-4, [-12]), (-9, [-12])]), B2, detail=1012)
obj('BEAST_DECO', [1328], NONE, 0, 0, [('beast_top', 0, 8, T1, 'BASE', 8), ('beast_bot', 0, -14, T1, 'BASE', 8), ('beast_eye', 18, 12, T1, 'DETAIL', 0)],
    anim='CHOMP', base=1010, detail=1011)

# ---------------------------------------------------------------- Viking Arena: rock strata (base, its shades and the detail on top)


def strata(top=None):
    ops = [R(-15, -15, 15, 15)]
    for y in (4, -9):
        ops += [P([(-15 + 10 * k, y), (-10 + 10 * k, y + 3.5), (-5 + 10 * k, y), (-10 + 10 * k, y - 3.5)], 'BASE_D') for k in range(3)]
    if top == 'band':
        ops += [R(-15, 9, 15, 15, 'DETAIL')] + [P([(-15 + 7.5 * k, 9), (-11.25 + 7.5 * k, 5), (-7.5 + 7.5 * k, 9)], 'DETAIL') for k in range(4)]
    elif top == 'corner':
        ops += [P([(-15, 15), (15, 15), (15, 9), (-9, 9), (-15, 3)], 'DETAIL')]
    elif top == 'bowl':
        ops += [P([(-15, 15), (15, 15), (15, 9), (8, 5), (-8, 5), (-15, 9)], 'DETAIL')]
    return ops


deco('STRATA', [976], strata(), B2, base=1004)
deco('STRATA_TOP', [952], strata('band'), B2, detail=1)
deco('STRATA_CORNER', [974], strata('corner'), B2, detail=1)
deco('STRATA_BOWL', [975], strata('bowl'), B2, detail=1)
deco('ROCK', [905], [R(-15, -15, 15, 15), P([(-15, 2), (-8, 6), (0, 1), (8, 6), (15, 2), (15, -3), (-15, -3)], 'BASE_L', .25)], B2)
deco('ROCK_CHUNK', [1045], [P([(-12, -12), (10, -13), (14, -2), (8, 11), (-6, 13), (-14, 4)]), P([(-6, 2), (4, -4), (9, 3), (2, 8)], 'BASE_L', .2)], B2)
deco('GRADIENT', [1293], [VG(-7.5, -15, 7.5, 15, 'BASE', 0, 1)], B2, base=1)

# ---------------------------------------------------------------- Airborne Robots: riveted metal frames over plain fills (LBG)


def frame(stripes=''):
    ops = outline('tblr', 2.5) + [R(x, y, x + 3, y + 3, 'BASE_L') for x in (-14, 11) for y in (-14, 11)]
    for side in stripes:
        if side in 'tb':
            y = 9 if side == 't' else -12.5
            ops += [R(-12.5, y, 12.5, y + 3.5, 'DETAIL')] + [P([(x, y), (x + 2, y), (x + 4.5, y + 3.5), (x + 2.5, y + 3.5)], 'BLACK') for x in (-11, -5, 1, 7)]
        else:
            x = -12.5 if side == 'l' else 9
            ops += [R(x, -12.5, x + 3.5, 12.5, 'DETAIL')] + [P([(x, y), (x, y + 2), (x + 3.5, y + 4.5), (x + 3.5, y + 2.5)], 'BLACK') for y in (-11, -5, 1, 7)]
    return ops


deco('METAL_FRAME', [1090], frame(), T1)
for name, ids, st in [('METAL_FRAME_T', [1082], 't'), ('METAL_FRAME_TL', [1083], 'tl'), ('METAL_FRAME_LR', [1084], 'lr'),
                      ('METAL_FRAME_TLR', [1085], 'tlr'), ('METAL_FRAME_ALL', [1086], 'tblr')]:
    deco(name, ids, frame(st), T1, detail=1)
deco('METAL_SLOPE', [1091], [seg(-15, -15, 15, 15, 5, 'BASE')] + [seg(-11 + 7 * k, -11 + 7 * k, -8 + 7 * k, -8 + 7 * k, 3, 'DETAIL') for k in range(4)], T1, detail=1)
deco('METAL_SLOPE_SQ', [1093], frame() + [P([(-12.5, 12.5), (-5, 12.5), (-12.5, 5)], 'DETAIL')], T1, detail=1)
for name, ids, ops in [('METAL_FILL', [1099], [chamfer(-15, -15, 15, 15, 3)]),
                       ('METAL_FILL_T', [1100], [P([(-15, -15), (15, -15), (15, 12), (12, 15), (-12, 15), (-15, 12)])]),
                       ('METAL_FILL_M', [1101], [R(-11, -11, 11, 11)]), ('METAL_FILL_TALL', [1102], [R(-8, -13, 8, 13)]),
                       ('METAL_FILL_S', [1103], [R(-9, -9, 9, 9)]), ('METAL_FILL_XS', [1104], [R(-7, -7, 7, 7)]),
                       ('METAL_FILL_TRI', [1107], [P([(-15, -15), (15, 15), (15, -15)])]),
                       ('PIPE_FILL_CORNER', [1112], [P([(-15, -15), (15, -15), (15, 15), (-5, 15), (-15, 5)])]),
                       ('PIPE_FILL_BAR', [1114], [R(-15, -6, 15, 6)]),
                       ('PIPE_FILL_ARCH', [1116], [P([(-7, -15), (7, -15), (7, 6), (4, 10), (-4, 10), (-7, 6)])]),
                       ('PIPE_FILL_OCT_S', [1117], [ngon(0, 0, 8)]), ('PIPE_FILL_OCT', [1118], [ngon(0, 0, 13)])]:
    deco(name, ids, ops, B2, base=1007)
# pipes: metal edges (base) round a striped core (white detail)


def stripes_h(x0, x1, y0, y1):
    return [P([(x, y0), (x + 2, y0), (x + 2 + (y1 - y0) * .7, y1), (x + (y1 - y0) * .7, y1)], 'BASE_D') for x in range(int(x0), int(x1) - 6, 7)]


deco('PIPE_CORNER', [841], outline('tl', 2.5) + [P([(-12.5, 12.5), (-4, 12.5), (12.5, -4), (12.5, -12.5)], 'DETAIL'), seg(-8, 8, 8, -8, 1.5, 'BASE_D')], T1, detail=1011)
deco('PIPE_BAR', [843], [R(-15, 5, 15, 8), R(-15, -8, 15, -5), R(-15, -5, 15, 5, 'DETAIL')] + stripes_h(-15, 15, -5, 5), T1, detail=1011)
deco('PIPE_ARCH', [846], [P([(-9, -15), (9, -15), (9, 7), (5, 11), (-5, 11), (-9, 7)]), R(-4.5, -15, 4.5, 6, 'DETAIL')], T1, detail=1011)
deco('PIPE_OCT_S', [847], [ngon(0, 0, 10), ngon(0, 0, 6, ct='DETAIL')], T1, detail=1011)
deco('PIPE_OCT', [848], [ngon(0, 0, 14), ngon(0, 0, 9, ct='DETAIL')], T1, detail=1011)
deco('COG_DECO', [97], [ngon(0, 0, 7)] + [R(-2, 6, 2, 10), R(-2, -10, 2, -6), R(6, -2, 10, 2), R(-10, -2, -6, 2), ngon(0, 0, 2.5, ct='BLACK')], anim='SPIN', base=1005)
deco('DECO_BRICKS_S', [115], [VG(x, -12, x + w, -12 + h, 'BASE', 1, .1) for (x, w, h) in [(-30, 12, 10), (-18, 16, 18), (-2, 14, 24), (12, 16, 14), (28, 10, 8)]],
     base=1005)
deco('CHAIN_RING', [499], [R(-6, -6, 6, -4), R(-4, -4, -2.5, 4), R(2.5, -4, 4, 4), R(-4, 2.5, 4, 4)], base=1)

# ---------------------------------------------------------------- Press Start, Nock Em, Power Trip


deco('SMALL_BALL', [1764], [ngon(0, 0, 2.2)], base=1)
deco('SMALL_X', [1765], [seg(-2.5, -2.5, 2.5, 2.5, 1), seg(-2.5, 2.5, 2.5, -2.5, 1)], base=1)
deco('DARK_SQUARE', [734], [R(-15, -15, 15, 15), R(-13, -13, 13, 13, 'BASE_L', .12)], B2)

RADIUS = {'LIGHT_BLADE_L': 45, 'LIGHT_BLADE_M': 30, 'MIRROR_ON': 55, 'MIRROR_OFF': 55, 'TRI_WSLOPE': 45, 'DECO_BRICKS_S': 45,
          'BEAST_DECO': 60, 'TEXT': 150}
