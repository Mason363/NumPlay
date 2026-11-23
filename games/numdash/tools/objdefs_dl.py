"""Object catalogue of Deadlocked (level 20, GD 2.0): outline and neon
blocks, glows, 3D lines, beveled bricks, industrial blocks, pits, slopes,
animated decoration, new portals and hazards.

Hitboxes follow GD 2.0 (gmdkit's hitbox table and OpenGD's measurements);
the art is drawn here from rectangles and polygons (tile programs, units,
y up, centre origin) or from sprites in sprites_lv.py, redrawn in NumDash's
style after the originals' shapes. Colour types BASE and DETAIL follow the
object's GD colour channels (defaults in `extra`).
"""
import math

NONE, SOLID, HAZARD, SPECIAL = range(4)
F_PULSE, F_RANDOM3, F_COIN, F_ANIM, F_QUAD, F_HALF = 1, 2, 4, 8, 16, 32
F_TERRAIN = 64   # decoration that is a level's ground and walls (Dash's pixel art): low detail keeps it
# GD default z layers -> NumDash draw layers
B2, B1, T1, GL = 'RODS', 'DETAIL', 'BLOCK', 'BLOCK_GLOW'


def R(x0, y0, x1, y1, ct='BASE', a=1.0):
    return ('rect', min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1), ct, a)


def VG(x0, y0, x1, y1, ct, a_top, a_bottom):
    return ('vgrad', x0, y0, x1, y1, ct, a_top, a_bottom)


def P(pts, ct='BASE', a=1.0):
    return ('poly', [(round(x * 2) / 2, round(y * 2) / 2) for x, y in pts], ct, a)


def seg(x0, y0, x1, y1, w=1.5, ct='BASE', a=1.0):
    """A straight stroke of width w as a quad."""
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy) or 1
    nx, ny = -dy / L * w / 2, dx / L * w / 2
    return P([(x0 + nx, y0 + ny), (x1 + nx, y1 + ny), (x1 - nx, y1 - ny), (x0 - nx, y0 - ny)], ct, a)


def glow_band(x0, y0, x1, y1, w=4.5, a=0.45):
    """Soft glow on both sides of a stroke (two alpha ramps as bands)."""
    ops = []
    dx, dy = x1 - x0, y1 - y0
    L = math.hypot(dx, dy) or 1
    nx, ny = -dy / L, dx / L
    for k in range(3):
        d0, d1 = w * k / 3, w * (k + 1) / 3
        aa = a * (1 - (k + .5) / 3)
        for sgn in (1, -1):
            p0 = (x0 + nx * d0 * sgn, y0 + ny * d0 * sgn)
            p1 = (x1 + nx * d0 * sgn, y1 + ny * d0 * sgn)
            p2 = (x1 + nx * d1 * sgn, y1 + ny * d1 * sgn)
            p3 = (x0 + nx * d1 * sgn, y0 + ny * d1 * sgn)
            ops.append(P([p0, p1, p2, p3], 'GLOW', aa))
    return ops


def radial(cx, cy, r, a0, a1, a_from, a_to, bands=5, ct='BASE'):
    """Radial fade from centre (alpha a_from) to radius r (a_to) over the
    angle range a0..a1 (degrees, counter-clockwise from +x), as stacked pie
    slices (each band adds its share of the alpha)."""
    ops = []
    prev = 0.0
    for k in range(bands):
        rr = r * (1 - k / bands)
        target = a_to + (a_from - a_to) * (k + 1) / bands
        step = max(0.0, 1 - (1 - target) / max(1e-6, 1 - prev)) if target < 1 else 1.0
        prev = target
        pts = [(cx, cy)]
        n = 5
        for j in range(n + 1):
            t = math.radians(a0 + (a1 - a0) * j / n)
            pts.append((cx + rr * math.cos(t), cy + rr * math.sin(t)))
        # convex pie with up to 8 points: thin out the arc
        ops.append(P(pts[:8], ct, round(step, 3)))
    return ops


def outline(sides, w=1.5, h=15, x=15):
    """Edge strokes of a box: letters t b l r."""
    ops = []
    if 't' in sides: ops.append(R(-x, h - w, x, h))
    if 'b' in sides: ops.append(R(-x, -h, x, -h + w))
    if 'l' in sides: ops.append(R(-x, -h, -x + w, h))
    if 'r' in sides: ops.append(R(x - w, -h, x, h))
    return ops


def outline_glow(sides, h=15, x=15, w=5.0, a=0.4):
    ops = []
    if 't' in sides: ops += [VG(-x, h, x, h + w, 'GLOW', 0, a), VG(-x, h - w, x, h, 'GLOW', a, 0)]
    if 'b' in sides: ops += [VG(-x, -h, x, -h + w, 'GLOW', a, 0), VG(-x, -h - w, x, -h, 'GLOW', 0, a)]
    if 'l' in sides: ops += glow_band(-x + .75, -h, -x + .75, h, w, a)
    if 'r' in sides: ops += glow_band(x - .75, -h, x - .75, h, w, a)
    return ops


PROGS = {}
OBJS = []


def prog(name, ops):
    PROGS[name] = ops
    return 'prog:' + name


def obj(name, ids, hit, w, h, parts, special='NONE', edy=0, **extra):
    OBJS.append((name, ids, hit, w, h, special, edy, parts, extra))


# ---------------------------------------------------------------- glows
# gradients that line block edges: opaque at the block side, base colour
# (channel 1 by default)
obj('GLOW_MED', [503], NONE, 0, 0, [(prog('g_med', [VG(-15, -10, 15, 10, 'BASE', 0, .85)]), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_LARGE', [1011], NONE, 0, 0, [(prog('g_large', [VG(-15, -15, 15, 15, 'BASE', 0, .85)]), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_SMALL', [1292], NONE, 0, 0, [(prog('g_small', [VG(-7.5, -5, 7.5, 5, 'BASE', 0, .85)]), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_HALF', [1291], NONE, 0, 0, [(prog('g_half', [VG(-7.5, -10, 7.5, 10, 'BASE', 0, .85)]), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_OUTER', [504], NONE, 0, 0, [(prog('g_outer', radial(10, -10, 20, 90, 180, .85, 0)), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_OUTER_L', [1012], NONE, 0, 0, [(prog('g_outer_l', radial(15, -15, 30, 90, 180, .85, 0)), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_INNER', [505], NONE, 0, 0, [(prog('g_inner', [VG(-15, -15, 15, 0, 'BASE', 0, .8), P([(0, -15), (15, -15), (15, 15), (0, 15)], 'BASE', .0)]
                                             + [P([(15 - w, -15), (15, -15), (15, 15), (15 - w, 15)], 'BASE', .2) for w in (5, 10, 15)]),
                                        0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_SLOPE1', [1273], NONE, 0, 0, [(prog('g_slope1', [P([(-7, -10), (13, 10), (13, -10)], 'BASE', .25), P([(0, -10), (13, 3), (13, -10)], 'BASE', .3),
                                                           P([(6, -10), (13, -3), (13, -10)], 'BASE', .35)]), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_SLOPE2', [1274], NONE, 0, 0, [(prog('g_slope2', [P([(-4.5, -10), (15.5, 10), (15.5, -10)], 'BASE', .2), P([(5.5, -10), (15.5, 0), (15.5, -10)], 'BASE', .35)]),
                                         0, 0, B2, 'BASE', 0)], base=1)

# ---------------------------------------------------------------- outline blocks (white 1.5 unit lines)
def outline_obj(name, ids, hit, w, h, sides, bw=15, bh=15):
    obj(name, ids, hit, w, h, [(prog('o_' + name.lower(), outline(sides, 1.5, bh, bw)), 0, 0, T1, 'BASE', 0),
                              (prog('og_' + name.lower(), outline_glow(sides, bh, bw)), 0, 0, GL, 'GLOW', 0)])


outline_obj('OUT_SQUARE', [467], SOLID, 30, 30, 'tblr')
obj('OUT_TOP', [468], SOLID, 30, 1.5, [(prog('o_top', [R(-15, -.75, 15, .75)]), 0, 0, T1, 'BASE', 0),
                                       (prog('og_top', [VG(-15, .75, 15, 5.5, 'GLOW', 0, .4), VG(-15, -5.5, 15, -.75, 'GLOW', .4, 0)]), 0, 0, GL, 'GLOW', 0)])
outline_obj('OUT_CORNER', [469], SOLID, 30, 30, 'tl')
outline_obj('OUT_TOP_PILLAR', [470], SOLID, 30, 30, 'tlr')
outline_obj('OUT_PILLAR', [471], SOLID, 30, 30, 'lr')
obj('OUT_INNER', [472], NONE, 0, 0, [(prog('o_inner', [R(-15, 13.5, -13.5, 15)]), 0, 0, T1, 'BASE', 0)])
obj('OUT_SLOPE_CORNER', [473], NONE, 0, 0, [(prog('o_slc', [P([(-15, 13.5), (-15, 15), (-12, 15)])]), 0, 0, T1, 'BASE', 0)])
obj('OUT_WSLOPE_CORNER', [474], NONE, 0, 0, [(prog('o_wslc', [P([(-15, 14), (-15, 15), (-10, 15)])]), 0, 0, T1, 'BASE', 0)])
obj('OUT_SLOPE_SIDE', [475], SOLID, 30, 1.5, [(prog('o_ss', [P([(-15, -.75), (15, -.75), (15, .75), (-12, .75)])]), 0, 0, T1, 'BASE', 0),
                                              (prog('og_ss', [VG(-15, .75, 15, 5, 'GLOW', 0, .35), VG(-15, -5, 15, -.75, 'GLOW', .35, 0)]), 0, 0, GL, 'GLOW', 0)])
outline_obj('OUT_SLAB', [662], SOLID, 30, 15, 'tblr', 15, 7.5)
outline_obj('OUT_SLAB_MID', [663], SOLID, 30, 15, 'tb', 15, 7.5)
outline_obj('OUT_SLAB_SIDE', [664], SOLID, 30, 15, 'tbl', 15, 7.5)
obj('OUT_THICK_TOP', [1202], SOLID, 30, 3, [(prog('o_thtop', [R(-15, -1.5, 15, 1.5)]), 0, 0, T1, 'BASE', 0),
                                            (prog('og_thtop', [VG(-15, 1.5, 15, 7, 'GLOW', 0, .4), VG(-15, -7, 15, -1.5, 'GLOW', .4, 0)]), 0, 0, GL, 'GLOW', 0)])
obj('OUT_THICK_SMALL', [1208], SOLID, 15, 15, [(prog('o_thsm', outline('tblr', 3, 7.5, 7.5)), 0, 0, T1, 'BASE', 0),
                                               (prog('og_thsm', outline_glow('tblr', 7.5, 7.5, 5)), 0, 0, GL, 'GLOW', 0)])
obj('OUT_THICK_SQUARE', [1210], SOLID, 30, 30, [(prog('o_thsq', outline('tblr', 3)), 0, 0, T1, 'BASE', 0),
                                                (prog('og_thsq', outline_glow('tblr')), 0, 0, GL, 'GLOW', 0)])
# slopes: a diagonal stroke (the solid triangle is below it)
obj('OUT_SLOPE', [665], SOLID, 30, 30, [(prog('o_slope', [seg(-15, -15, 15, 15, 1.8)]), 0, 0, T1, 'BASE', 0),
                                        (prog('og_slope', glow_band(-15, -15, 15, 15, 5)), 0, 0, GL, 'GLOW', 0)], shape='SLOPE')
obj('OUT_WSLOPE', [666], SOLID, 60, 30, [(prog('o_wslope', [seg(-30, -15, 30, 15, 1.8)]), 0, 0, T1, 'BASE', 0),
                                         (prog('og_wslope', glow_band(-30, -15, 30, 15, 5)), 0, 0, GL, 'GLOW', 0)], shape='SLOPE')
obj('BLOCK_WSLOPE', [652], SOLID, 60, 30, [(prog('b_wslope', [P([(-30, -15), (30, 15), (30, -15)], 'BLACK'), seg(-30, -15, 30, 15, 1.8, 'BASE'),
                                                               P([(-18, -15), (30, 9), (30, 3), (-6, -15)], 'DETAIL', .5)]), 0, 0, T1, 'BASE', 0),
                                           (prog('og_wslope', glow_band(-30, -15, 30, 15, 5)), 0, 0, GL, 'GLOW', 0)], shape='SLOPE', detail=1)
# decorative black slopes
obj('DECO_SLOPE', [687], NONE, 0, 0, [(prog('d_slope', [P([(-15, -15), (15, 15), (15, -15)], 'BASE')]), 0, 0, B1, 'BASE', 0)])
obj('DECO_WSLOPE', [688], NONE, 0, 0, [(prog('d_wslope', [P([(-30, -15), (30, 15), (30, -15)], 'BASE')]), 0, 0, B1, 'BASE', 0)])
# rainbow slopes (fixed colours)
obj('RAINBOW_SLOPE', [1014], NONE, 0, 0, [(prog('rain_slope', [P([(-15 + 5 * k, -15), (15, 15 - 5 * k), (15, 15 - 5 * (k + 1)), (-15 + 5 * (k + 1), -15)], 'RAIN%d' % k)
                                                               for k in range(6)]), 0, 0, B2, 'BASE', 0)], base=1011)
obj('RAINBOW_SQUARE', [1016], NONE, 0, 0, [(prog('rain_sq', [P([(-15, 15 - 5 * k), (15 - 5 * k, -15), (15 - 5 * (k + 1), -15), (-15, 15 - 5 * (k + 1))], 'RAIN%d' % (5 - k))
                                                             for k in range(6) if k < 6] + [P([(-15, -15 + 0), (-15, -15), (-15, -15)], 'RAIN0', 0)][:0]),
                                            0, 0, B2, 'BASE', 0)], base=1011)

# ---------------------------------------------------------------- neon blocks (base body a shade darker, detail strips)
S = 7.0   # strip width


def neon(name, ids, body, strips, hit=NONE, w=0, h=0, **kw):
    ops = [P(b, 'BASE_D') if isinstance(b, list) else b for b in body] + strips
    obj(name, ids, hit, w, h, [(prog('n_' + name.lower(), ops), 0, 0, B2, 'BASE', 0)], detail=1, **kw)


def rb(x0, y0, x1, y1):
    return R(x0, y0, x1, y1, 'BASE_D')


def rd(x0, y0, x1, y1):
    return R(x0, y0, x1, y1, 'DETAIL')


neon('NEON_TOP', [1162], [rb(-15, -15, 15, 15 - S)], [rd(-15, 15 - S, 15, 15)])
neon('NEON_TL', [1163], [rb(-15 + S, -15, 15, 15 - S)], [rd(-15, 15 - S, 15, 15), rd(-15, -15, -15 + S, 15 - S)])
neon('NEON_VPILLAR_TOP', [1164, 1175], [rb(-7.5, -15, 7.5, 15 - S)], [rd(-7.5 - S, 15 - S, 7.5 + S, 15), rd(-7.5 - S, -15, -7.5, 15 - S), rd(7.5, -15, 7.5 + S, 15 - S)])
neon('NEON_VPILLAR', [1165], [rb(-7.5, -15, 7.5, 15)], [rd(-7.5 - S, -15, -7.5, 15), rd(7.5, -15, 7.5 + S, 15)])
neon('NEON_LEFT', [1167], [rb(-15 + S, -15, 15, 15)], [rd(-15, -15, -15 + S, 15)])
neon('NEON_TL_INNER', [1168], [rb(-15, -15, 15, 15)], [rd(-15, 15 - S, -15 + S, 15)])
neon('NEON_SQUARE', [1170], [rb(-15 + S, -15 + S, 15 - S, 15 - S)], [rd(-15, 15 - S, 15, 15), rd(-15, -15, 15, -15 + S), rd(-15, -15 + S, -15 + S, 15 - S),
                                                                       rd(15 - S, -15 + S, 15, 15 - S)])
neon('NEON_SIDE_TOP', [1171], [rb(-15 + S, -15, 15, 15)], [rd(-15, -15, -15 + S, 15), rd(15 - S, 15 - S, 15, 15)])
neon('NEON_PILLAR_TL', [1173], [rb(-15 + S, -15, 15, 15 - S)], [rd(-15, 15 - S, 15, 15), rd(-15, -15, -15 + S, 15 - S), rd(15 - S, -15, 15, -15 + S)])
neon('NEON_HPILLAR', [1174], [rb(-15, -7.5, 15, 7.5)], [rd(-15, 7.5, 15, 7.5 + S), rd(-15, -7.5 - S, 15, -7.5)])
neon('NEON_HPILLAR_L', [1176], [rb(-15 + S, -7.5, 15, 7.5)], [rd(-15, 7.5, 15, 7.5 + S), rd(-15, -7.5 - S, 15, -7.5), rd(-15, -7.5, -15 + S, 7.5)])
neon('NEON_INNER', [1186], [rb(-15, -15, 15, 15)], [])
neon('NEON_INV_BOTTOM_SIDE', [1322], [rb(-15, -15 + S, 15 - S, 15)], [rd(-15, -15, 15, -15 + S), rd(15 - S, -15 + S, 15, 15)])
neon('NEON_SLOPE', [1187], [[(-15, -15), (15, 15 - S * 1.41), (15, -15)]], [P([(-15, -15), (-15 + S * 1.41, -15), (15, 15 - S * 1.41), (15, 15)], 'DETAIL')])
neon('NEON_WSLOPE', [1188], [[(-30, -15), (30, 15 - S * 1.12), (30, -15)]], [P([(-30, -15), (-30 + S * 2.2, -15), (30, 15 - S * 1.12), (30, 15)], 'DETAIL')])
neon('NEON_SLOPE_CONN', [1189], [rb(-15, -15, 15, 15)], [P([(-15, 15 - S * 1.41), (-15, 15), (-15 + S * 1.41, 15)], 'DETAIL')])
neon('NEON_WSLOPE_CONN', [1190], [rb(-15, -15, 15, 15)], [P([(-15, 15 - S * 1.12), (-15, 15), (-15 + S * 2.2, 15)], 'DETAIL')])
neon('NEON_HSLOPE_CONN', [1325], [[(-15, -15), (-15, 15), (15, 15), (15, -15)]], [P([(-15, 15 - S * 1.41), (-15, 15), (-15 + S * 1.41, 15)], 'DETAIL')])
obj('NEON_OUTLINE_TOP', [1191], NONE, 0, 0, [(prog('n_otop', [rd(-15, 15 - S, 15, 15)]), 0, 0, T1, 'BASE', 0)], detail=1)
obj('NEON_OUTLINE_INNER', [1194], NONE, 0, 0, [(prog('n_oinner', [rd(-15, 15 - S, -15 + S, 15)]), 0, 0, T1, 'BASE', 0)], detail=1)
obj('QUARTER_COLOR', [916], NONE, 0, 0, [(prog('q_color', [R(-7.5, -7.5, 7.5, 7.5)]), 0, 0, B1, 'BASE', 0)], base=1)
obj('SIXTEENTH_COLOR', [917], NONE, 0, 0, [(prog('s_color', [R(-3.75, -3.75, 3.75, 3.75)]), 0, 0, B1, 'BASE', 0)], base=1)
obj('COLOR_SQUARE', [211], NONE, 0, 0, [(prog('c_square', [R(-15, -15, 15, 15)]), 0, 0, B2, 'BASE', 0)], base=1)

# ---------------------------------------------------------------- 3D lines (3DL channel)
L3 = 1.5
obj('L3_TOP_LEFT', [506], NONE, 0, 0, [(prog('l3_tl', [seg(-15, -5, -5, 5 - L3 / 2, L3), R(-5.5, 5 - L3, 15, 5)]), 0, 0, B1, 'BASE', 0)], base=1003)
obj('L3_TOP_MID', [507], NONE, 0, 0, [(prog('l3_tm', [R(-15, 4 - L3 / 2, 15, 4 + L3 / 2)]), 0, 0, B1, 'BASE', 0)], base=1003)
obj('L3_HALF_TOP_MID', [508], NONE, 0, 0, [(prog('l3_htm', [R(-7.5, 4 - L3 / 2, 7.5, 4 + L3 / 2)]), 0, 0, B1, 'BASE', 0)], base=1003)
obj('L3_TOP_RIGHT', [509], NONE, 0, 0, [(prog('l3_tr', [seg(-6, -6, 6, 6, L3), R(-1, 5.25, 6, 6.75), R(5.25, -1, 6.75, 6)]), 0, 0, B1, 'BASE', 0)], base=1003)
obj('L3_INNER', [510], NONE, 0, 0, [(prog('l3_in', [seg(-8, -8, 0, 0, L3), R(-1, -.75, 10, .75), R(-.75, 0, .75, 10)]), 0, 0, B1, 'BASE', 0)], base=1003)
obj('L3_OUTER', [511], NONE, 0, 0, [(prog('l3_out', [seg(-6, -6, 6, 6, L3), R(-6, 5.25, 6, 6.75)]), 0, 0, B1, 'BASE', 0)], base=1003)
obj('L3_HALF_TOP_LEFT', [512], NONE, 0, 0, [(prog('l3_htl', [seg(-7.5, -5, -2.5, 4.25, L3), R(-3, 4 - L3 / 2, 7.5, 4 + L3 / 2)]), 0, 0, B1, 'BASE', 0)], base=1003)
# coloured 3D blocks (filled tops, channel 3)
obj('C3_TOP_LEFT', [578], NONE, 0, 0, [(prog('c3_tl', [P([(-15, -5), (-5, 5), (0, 5), (0, -5)]), R(0, -5, 15, 5)]), 0, 0, B2, 'BASE', 0)], base=3)
obj('C3_TOP_MID', [579], NONE, 0, 0, [(prog('c3_tm', [R(-15, -5, 15, 5)]), 0, 0, B2, 'BASE', 0)], base=3)
obj('C3_OUTER', [583], NONE, 0, 0, [(prog('c3_out', [P([(-5, -5), (5, 5), (5, -5)])]), 0, 0, B2, 'BASE', 0)], base=3)
obj('C3_HALF_TOP_LEFT', [584], NONE, 0, 0, [(prog('c3_htl', [P([(-7.5, -5), (2.5, 5), (7.5, 5), (7.5, -5)])]), 0, 0, B2, 'BASE', 0)], base=3)
obj('C3_SQUARE_MID', [624], NONE, 0, 0, [(prog('c3_sqm', [R(-15, -5, 15, 5, 'BASE_D'), R(-15, 3.5, 15, 5, 'BASE')]), 0, 0, B2, 'BASE', 0)])
obj('C3_SQUARE_OUTER', [628], NONE, 0, 0, [(prog('c3_sqo', [P([(-5, -5), (5, 5), (5, -5)], 'BASE_D')]), 0, 0, B2, 'BASE', 0)])


def stripes(x0, x1, y0, y1, n, ct='DETAIL'):
    ops = []
    w = (x1 - x0) / n
    for k in range(n):
        xa = x0 + k * w
        ops.append(P([(xa, y0), (xa + w * .5, y0), (xa + w * .5 + (y1 - y0), y1), (xa + (y1 - y0), y1)][:4], ct))
    return ops


obj('S3_TOP_LEFT', [980], NONE, 0, 0, [(prog('s3_tl', [P([(-15, -5), (-5, 5), (15, 5), (15, -5)], 'BLACK')] + stripes(-8, 12, -5, 5, 4)), 0, 0, B2, 'BASE', 0)], detail=1)
obj('S3_TOP_MID', [981], NONE, 0, 0, [(prog('s3_tm', [R(-15, -5, 15, 5, 'BLACK')] + stripes(-17, 13, -5, 5, 5)), 0, 0, B2, 'BASE', 0)], detail=1)
obj('S3_TOP_RIGHT', [983], NONE, 0, 0, [(prog('s3_tr', [P([(-5, -5), (5, 5), (5, -5)], 'BLACK'), P([(-1, -5), (3, -5), (5, -3), (5, 1)], 'DETAIL')]), 0, 0, B2, 'BASE', 0)], detail=1)
obj('S3_HALF_TOP_LEFT', [986], NONE, 0, 0, [(prog('s3_htl', [P([(-7.5, -5), (2.5, 5), (7.5, 5), (7.5, -5)], 'BLACK')] + stripes(-2, 6, -5, 5, 2)), 0, 0, B2, 'BASE', 0)], detail=1)

# ---------------------------------------------------------------- beveled bricks (base grey, highlights in the lighter shade)
def brick(x0, y0, x1, y1, b=1.5):
    """One beveled brick: body, light top-left bevel, dark bottom-right."""
    return [R(x0, y0, x1, y1, 'BASE'), P([(x0, y1), (x1, y1), (x1 - b, y1 - b), (x0 + b, y1 - b)], 'DETAIL'),
            P([(x0, y0), (x0, y1), (x0 + b, y1 - b), (x0 + b, y0 + b)], 'DETAIL', .7),
            P([(x0, y0), (x1, y0), (x1 - b, y0 + b), (x0 + b, y0 + b)], 'BASE_D'),
            P([(x1, y0), (x1, y1), (x1 - b, y1 - b), (x1 - b, y0 + b)], 'BASE_D')]


def bricks(rows, mortar=True):
    ops = [R(-15, -15, 15, 15, 'BASE_D')] if mortar else []
    for (x0, y0, x1, y1) in rows:
        ops += brick(x0, y0, x1, y1)
    return ops


BIG2 = [(-14, 1, -.5, 14), (.5, 1, 14, 14), (-14, -14, 14, -1)]
obj('BRICK_MID', [869], NONE, 0, 0, [(prog('bv_mid', bricks([(-15, 1, -.5, 15), (.5, 1, 15, 15), (-15, -15, 15, -1)])), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BRICK_BOND', [870], NONE, 0, 0, [(prog('bv_bond', bricks([(-15, 1, 15, 15), (-15, -15, -.5, -1), (.5, -15, 15, -1)])), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BRICK_LEFT1', [871], NONE, 0, 0, [(prog('bv_l1', bricks([(-14, 1, -.5, 14), (.5, 1, 15, 14), (-14, -15, 15, -1)])), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BRICK_RIGHT1', [872], NONE, 0, 0, [(prog('bv_r1', bricks([(-15, 1, -.5, 14), (.5, 1, 14, 14), (-15, -15, 14, -1)])), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BRICK_LEFT2', [1266], NONE, 0, 0, [(prog('bv_l2', bricks([(-14, 1, -.5, 15), (.5, 1, 15, 15), (-14, -14, 15, -1)])), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BRICK_RIGHT2', [1267], NONE, 0, 0, [(prog('bv_r2', bricks([(-15, 1, -.5, 15), (.5, 1, 14, 15), (-15, -14, 14, -1)])), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BRICKS1', [867], NONE, 0, 0, [(prog('bv_1', bricks([(-14, 6, -2, 14), (-1, 6, 14, 14), (-14, -4, 4, 4), (5, -4, 14, 4), (-14, -14, -5, -6), (-4, -14, 14, -6)])), 0, 0, B2, 'BASE', 0)], detail=1012)
SMALL = [(-15, 8, -6, 15), (-5, 8, 5, 15), (6, 8, 15, 15), (-15, 0, 0, 7), (1, 0, 15, 7), (-15, -8, -6, -1), (-5, -8, 5, -1), (6, -8, 15, -1), (-15, -15, 0, -9), (1, -15, 15, -9)]
obj('SBRICKS', [880], NONE, 0, 0, [(prog('bv_s', bricks(SMALL)), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('SBRICKS_RECESS', [881], NONE, 0, 0, [(prog('bv_sr', bricks([b for b in SMALL if b[1] < 8], False) + [R(-15, -15, 15, 8, 'BASE_D')][:0]), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('SBRICK_PILE_L', [882], NONE, 0, 0, [(prog('bv_pl', bricks([(4, 8, 15, 15), (-4, 0, 15, 7), (-10, -8, 15, -1), (-15, -15, 15, -9)], False)), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('SBRICK_PILE_R', [883], NONE, 0, 0, [(prog('bv_pr', bricks([(-15, 8, -4, 15), (-15, 0, 4, 7), (-15, -8, 10, -1), (-15, -15, 15, -9)], False)), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('SBRICKS_LEFT', [884], NONE, 0, 0, [(prog('bv_sl', bricks([(-13, 8, -6, 15), (-5, 8, 5, 15), (6, 8, 15, 15), (-13, 0, 0, 7), (1, 0, 15, 7), (-13, -8, -6, -1), (-5, -8, 5, -1), (6, -8, 15, -1), (-13, -15, 0, -9), (1, -15, 15, -9)], False)),
                                         0, 0, B2, 'BASE', 0)], detail=1012)
obj('BEVEL_SLAB', [891], NONE, 0, 0, [(prog('bv_slab', brick(-15, -7.5, 15, 7.5, 2)), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BEVEL_PIPE', [893], NONE, 0, 0, [(prog('bv_pipe', brick(-5, -7.5, 5, 7.5, 1.5)), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BEVEL_PIPE_CONN', [853], NONE, 0, 0, [(prog('bv_pconn', brick(-7.5, -7.5, 7.5, 7.5, 1.5)), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BEVEL_PIPE_DOWN', [857], NONE, 0, 0, [(prog('bv_pdown', brick(-5, -7.5, 5, 7.5, 1.5) + [R(-7.5, 5, 7.5, 7.5, 'DETAIL')]), 0, 0, B2, 'BASE', 0)], detail=1012)
obj('BEVEL_PIPE_UP', [859], NONE, 0, 0, [(prog('bv_pup', brick(-5, -7.5, 5, 7.5, 1.5) + [R(-7.5, -7.5, 7.5, -5, 'BASE_D')]), 0, 0, B2, 'BASE', 0)], detail=1012)

# ---------------------------------------------------------------- industrial blocks (grey frame, hazard stripes)
def indus(dark):
    body = 'BASE_D' if dark else 'BASE'
    return [R(-15, -15, 15, 15, 'BASE_L'), R(-13, -13, 13, 13, 'BLACK'), R(-11, -11, 11, 11, body, .85)]


def hstripes(y0, y1, x0=-13, x1=13, n=5):
    ops = []
    w = (x1 - x0) / n
    for k in range(n):
        xa = x0 + k * w
        ops.append(P([(xa, y0), (xa + w * .45, y0), (xa + w * .45 + (y1 - y0) * .6, y1), (xa + (y1 - y0) * .6, y1)], 'DETAIL'))
    return ops


for (name, ids, dark, extra_ops) in [('INDUS_TOP_L', [807], False, hstripes(11, 13)), ('INDUS_TOP_D', [808], True, hstripes(11, 13)),
                                     ('INDUS_CORNER_L', [809], False, hstripes(11, 13) + [P([(-13, -13), (-11, -13), (-11, 11), (-13, 11)], 'DETAIL')]),
                                     ('INDUS_CORNER_D', [810], True, hstripes(11, 13) + [P([(-13, -13), (-11, -13), (-11, 11), (-13, 11)], 'DETAIL')]),
                                     ('INDUS_PILLAR_L', [811], False, [P([(-13, -13), (-11, -13), (-11, 13), (-13, 13)], 'DETAIL'), P([(11, -13), (13, -13), (13, 13), (11, 13)], 'DETAIL')]),
                                     ('INDUS_PILLAR_D', [812], True, [P([(-13, -13), (-11, -13), (-11, 13), (-13, 13)], 'DETAIL'), P([(11, -13), (13, -13), (13, 13), (11, 13)], 'DETAIL')]),
                                     ('INDUS_PTOP_L', [813], False, hstripes(11, 13) + [R(-13, -13, -11, 11, 'DETAIL'), R(11, -13, 13, 11, 'DETAIL')]),
                                     ('INDUS_PTOP_D', [814], True, hstripes(11, 13) + [R(-13, -13, -11, 11, 'DETAIL'), R(11, -13, 13, 11, 'DETAIL')]),
                                     ('INDUS_SQUARE_L', [815], False, hstripes(11, 13) + hstripes(-13, -11)),
                                     ('INDUS_BOLTS_L', [817], False, [R(-10, 9, -8, 11, 'BASE_L'), R(8, 9, 10, 11, 'BASE_L'), R(-10, -11, -8, -9, 'BASE_L'), R(8, -11, 10, -9, 'BASE_L')]),
                                     ('INDUS_HATCH_D', [820], True, [R(-5, 3, 5, 5, 'BLACK'), R(-1, -2, 1, 3, 'BLACK')]),
                                     ('INDUS_INNER_L', [823], False, []), ('INDUS_INNER_D', [824], True, [])]:
    obj(name, ids, NONE, 0, 0, [(prog('i_' + name.lower(), indus(dark) + extra_ops), 0, 0, B2, 'BASE', 0)], detail=1)
obj('INDUS_SLOPE_D', [827], NONE, 0, 0, [(prog('i_slope', [P([(-15, -15), (15, 15), (15, -15)], 'BASE_L'), P([(-11, -13), (13, 11), (13, -13)], 'BASE_D')] + [seg(-9 + 6 * k, -13 + 6 * k, -5 + 6 * k, -9 + 6 * k, 3, 'DETAIL') for k in range(3)]),
                                          0, 0, B2, 'BASE', 0)], detail=1)
obj('INDUS_SLOPE_CONN_L', [830], NONE, 0, 0, [(prog('i_sconn', indus(False) + [P([(-15, 11), (-15, 15), (-11, 15)], 'DETAIL')]), 0, 0, B2, 'BASE', 0)], detail=1)
for (name, ids, ends) in [('INDUS_SLAB_MID', [1079], ''), ('INDUS_SLAB_SIDE', [1080], 'l'), ('INDUS_SLAB_SINGLE', [1081], 'lr')]:
    ops = [R(-15, -7.5, 15, 7.5, 'BASE_L'), R(-15, -5.5, 15, 5.5, 'BLACK'), R(-15, -3.5, 15, 3.5, 'BASE_D', .85)] + hstripes(4, 6, -15, 15, 6) + hstripes(-6, -4, -15, 15, 6)
    if 'l' in ends: ops += [R(-15, -7.5, -12, 7.5, 'BASE_L')]
    if 'r' in ends: ops += [R(12, -7.5, 15, 7.5, 'BASE_L')]
    obj(name, ids, NONE, 0, 0, [(prog('i_' + name.lower(), ops), 0, 0, B2, 'BASE', 0)], detail=1)

# ---------------------------------------------------------------- grey patterned tiles (pixel art)
def pix_tile(body):
    return [P(b, 'BASE_L') for b in body]


obj('PIX_TILE', [738], NONE, 0, 0, [(prog('p_tile', [R(-15, -15, 15, 15, 'BASE_L'), R(-12, -12, 12, 12, 'BASE'), R(-12, 10, 12, 12, 'BASE_L', .6)]), 0, 0, B2, 'BASE', 0)])
obj('PIX_EDGE', [668], NONE, 0, 0, [(prog('p_edge', [R(-15, -7.5, 15, 7.5, 'BASE_L'), R(-12, -4.5, 12, 4.5, 'BASE'), R(-5, 4.5, 5, 7.5, 'BASE')]), 0, 0, B2, 'BASE', 0)])
obj('PIX_INNER', [669], NONE, 0, 0, [(prog('p_inner', [P([(-15, -15), (-15, -5), (-5, -5), (-5, 5), (5, 5), (5, 15), (15, 15), (15, -15)][:8], 'BASE_L'),
                                                        R(-12, -12, 12, -8, 'BASE'), R(8, -8, 12, 12, 'BASE')]), 0, 0, B2, 'BASE', 0)])
obj('PIX_PIPE', [671], NONE, 0, 0, [(prog('p_pipe', [R(-10, -15, 10, 15, 'BASE_L'), R(-6, -15, 6, 15, 'BASE'), R(-10, 3, 10, 7, 'BASE_L')]), 0, 0, B2, 'BASE', 0)])
obj('PIX_PIPE_BASE', [672], NONE, 0, 0, [(prog('p_pbase', [R(-15, -15, 15, 0, 'BASE_L'), R(-12, -12, 12, -3, 'BASE'), R(-10, 0, 10, 15, 'BASE_L'), R(-6, 0, 6, 15, 'BASE')]),
                                          0, 0, B2, 'BASE', 0)])

# ---------------------------------------------------------------- pits (black hazards) and their shine outlines
obj('PIT_CROWN', [421], HAZARD, 9, 5.2, [(prog('pt_crown', [P([(-15, -6.5), (-15, 1), (-10, -2), (-5, 5), (0, -1), (5, 5), (10, -2), (15, 1)][:8] + [], 'BLACK'),
                                                               R(-15, -6.5, 15, -1, 'BLACK')]), 0, 0, T1, 'BASE', 0)], edy=-8)
obj('PIT_BOWL', [446], HAZARD, 9, 7.2, [(prog('pt_bowl', [P([(-15, -9), (-15, 9), (-9, 1), (0, -2), (9, 1), (15, 9), (15, -9)], 'BLACK')]), 0, 0, T1, 'BASE', 0)])
obj('PIT_BOWL2', [447], HAZARD, 5.2, 7.2, [(prog('pt_bowl2', [P([(-15, -9), (-15, 9), (-5, -2), (15, -9)], 'BLACK')]), 0, 0, T1, 'BASE', 0)], hx=-5)
obj('PIT_SQUARE', [667], HAZARD, 9, 6, [(prog('pt_square', [R(-15, -7.5, 15, 0, 'BLACK'), R(-12, 0, -4, 7.5, 'BLACK'), R(4, 0, 12, 7.5, 'BLACK')]), 0, 0, T1, 'BASE', 0)])
obj('PIT_STAIR', [989], HAZARD, 9, 12, [(prog('pt_stair', [P([(-15, -15), (15, 15), (15, -15)], 'BLACK'), R(-7.5, -15, 0, 7.5, 'BLACK'), R(0, -15, 7.5, 15, 'BLACK')]), 0, 0, T1, 'BASE', 0)])
obj('PIT_TRI', [991], HAZARD, 2.4, 3.2, [(prog('pt_tri', [P([(-3.75, -3.75), (3.75, 3.75), (3.75, -3.75)], 'BLACK')]), 0, 0, T1, 'BASE', 0)])
obj('PIT_DOT', [720], HAZARD, 2.4, 3.2, [(prog('pt_dot', [R(-4, -3.75, 4, 3.75, 'BLACK')]), 0, 0, T1, 'BASE', 0)])
obj('PIT_WAVE_LOW', [368], HAZARD, 9, 4, [(prog('pt_wlow', [P([(-15, -5), (-15, 1), (-9, 4), (-3, 1), (3, 4), (9, 1), (15, 4), (15, -5)], 'BLACK')]), 0, 0, T1, 'BASE', 0)], edy=-8)
obj('PIT_TRI_LOW', [422], HAZARD, 6, 4.4, [(prog('pt_tlow', [P([(-15, -5), (-15, -1), (-8, 4), (15, -5)], 'BLACK')]), 0, 0, T1, 'BASE', 0)], hx=-5, edy=-8)
obj('PIT_NOTCH', [768], HAZARD, 4.5, 5.2, [(prog('pt_notch', [P([(-7.5, -4), (-7.5, 4), (0, 0), (7.5, 4), (7.5, -4)], 'BLACK')]), 0, 0, T1, 'BASE', 0)])
obj('PIT_SHINE', [719], NONE, 0, 0, [(prog('ps_sq', [R(-15, -1, -12, .5), R(-12, -1, -10.5, 8), R(-12, 7, -4, 8.5), R(-5.5, -1, -4, 8), R(-5.5, -1, 5.5, .5),
                                                     R(4, -1, 5.5, 8), R(4, 7, 12, 8.5), R(10.5, -1, 12, 8), R(12, -1, 15, .5)]), 0, 0, B1, 'BASE', 0)], base=1)
obj('PIT_SHINE_CORNER', [721], NONE, 0, 0, [(prog('ps_corner', [R(-5, 3.5, 5, 5), R(3.5, -5, 5, 5)]), 0, 0, B1, 'BASE', 0)], base=1)
obj('PIT_SHINE_STAIR', [990], NONE, 0, 0, [(prog('ps_stair', [seg(-15, -15, -8, -8, 1.5), R(-8.75, -8.75, -7.25, 0), R(-8.75, -.75, 0, .75), seg(0, 0, 8, 8, 1.5),
                                                              R(7.25, 8, 8.75, 15), R(8, 13.5, 15, 15)]), 0, 0, B1, 'BASE', 0)], base=1)
obj('PIT_SHINE_DIAG', [992], NONE, 0, 0, [(prog('ps_diag', [seg(-5, -5, 5, 5, 1.5)]), 0, 0, B1, 'BASE', 0)], base=1)

# ---------------------------------------------------------------- coloured spikes (outline in base, fill in detail)
def cspike(name, ids, w, h, hw, hh, edy=0):
    obj(name, ids, HAZARD, hw, hh, [(prog('cs_' + name.lower(), [P([(-w / 2, -h / 2), (0, h / 2), (w / 2, -h / 2)], 'BASE'),
                                                                 P([(-w / 2 + 3, -h / 2 + 2), (0, h / 2 - 4.5), (w / 2 - 3, -h / 2 + 2)], 'DETAIL')]), 0, 0, T1, 'BASE', 0),
                                    ('glow_spike', 0, 0, GL, 'GLOW', 0)], edy=edy, detail=1)


cspike('CSPIKE', [216], 30, 30, 6, 12)
cspike('CSPIKE_SMALL', [218], 20, 19, 4, 7.6, -6)
cspike('CSPIKE_TINY', [458], 12.5, 12, 2.6, 4.8, -9)
obj('ISPIKE_TINY', [459], HAZARD, 2.6, 4.8, [(prog('is_tiny', [seg(-6, -6, 0, 6, 1.2), seg(0, 6, 6, -6, 1.2), R(-6, -6.6, 6, -5.4)]), 0, 0, T1, 'BASE', 0)], edy=-9, anim='INVIS')
obj('ISPIKE_HALF', [205], HAZARD, 6, 5.6, [(prog('is_half', [seg(-15, -6.75, 0, 6.75, 1.5), seg(0, 6.75, 15, -6.75, 1.5), R(-15, -7.5, 15, -6)]), 0, 0, T1, 'BASE', 0)], edy=-9, anim='INVIS')
obj('INVIS_SLAB', [147], SOLID, 30, 14, [(prog('is_slab', outline('tblr', 1.5, 7, 15)), 0, 0, T1, 'BASE', 0)], edy=8, anim='INVIS')

# ---------------------------------------------------------------- misc decoration
obj('WAVE_SQUARE', [1228], NONE, 0, 0, [(prog('w_square', [VG(-15, -15, 15, 15, 'BASE', .15, .45)]), 0, 0, B1, 'BASE', 0)], base=1007)


def wave_box(inv):
    crest = [(-15, 6 if not inv else 13), (-7.5, 10 if not inv else 7), (0, 13 if not inv else 5.5), (7.5, 10 if not inv else 7), (15, 6 if not inv else 13)]
    ops = [VG(-15, -13, 15, 5.5, 'BASE', .55, .15)]
    for k in range(4):
        (xa, ya), (xb, yb) = crest[k], crest[k + 1]
        ops.append(P([(xa, 5.5), (xa, ya), (xb, yb), (xb, 5.5)], 'BASE', .55))
    return ops


obj('CIRCLE_WAVE', [1050], NONE, 0, 0, [(prog('cw_box', wave_box(False)), 0, 0, B1, 'BASE', 0)], base=1007)
obj('CIRCLE_WAVE_INV', [1052], NONE, 0, 0, [(prog('cw_inv', wave_box(True)), 0, 0, B1, 'BASE', 0)], base=1007)


def spike_wave(peaks):
    ops = [VG(-15, -12.75, 15, 3, 'BASE', .5, .15)]
    for (xa, xb, top) in peaks:
        ops.append(P([(xa, 3), ((xa + xb) / 2, top), (xb, 3)], 'BASE', .5))
    return ops


obj('WATER1', [419], NONE, 0, 0, [(prog('water1', spike_wave([(-15, -5, 12.75), (-5, 5, 8), (5, 15, 12.75)])), 0, 0, B1, 'BASE', 0)], base=1007)
obj('WATER2', [420], NONE, 0, 0, [(prog('water2', spike_wave([(-15, 0, 11), (0, 15, 8)])), 0, 0, B1, 'BASE', 0)], base=1007)
obj('JAGGED_HALF', [767], NONE, 0, 0, [(prog('jagged', spike_wave([(-7.5, 7.5, 12)])), 0, 0, B1, 'BASE', 0)], base=1007)
obj('STRIPE_SLAB', [1054], NONE, 0, 0, [(prog('st_slab', [R(-15, -7.5, 15, 7.5, 'DETAIL')] + stripes(-20, 15, -7.5, 7.5, 4, 'BASE')), 0, 0, B1, 'BASE', 0)],
    base=1, detail=1010)
obj('PULSE_ARROW3', [494], NONE, 0, 0, [(prog('pa3', [P([(-11, -22), (11, 0), (-11, 22)], 'BASE')]), 0, 0, B1, 'BASE', F_PULSE)], base=1005)
obj('PULSE_TRIANGLE', [149], NONE, 0, 0, [(prog('ptri', [P([(-13, -11), (0, 11), (13, -11)], 'BASE')]), 0, 0, B1, 'BASE', F_PULSE)], base=1006)


def link(lines):
    return [R(*l, 'BASE') for l in lines] + [P([(-2.5, 0), (0, 2.5), (2.5, 0), (0, -2.5)], 'DETAIL')]


obj('LINK_ADJ', [1002], NONE, 0, 0, [(prog('lk_adj', link([(-1, 1, 1, 15), (1, -1, 15, 1)])), 0, 0, B1, 'BASE', 0)], base=1005, detail=1006)
obj('LINK_THREE', [1003], NONE, 0, 0, [(prog('lk_three', link([(-15, -1, 15, 1), (-1, -15, 1, -1)])), 0, 0, B1, 'BASE', 0)], base=1005, detail=1006)
obj('LINK_OPP', [1004], NONE, 0, 0, [(prog('lk_opp', link([(-15, -1, -4, 1), (4, -1, 15, 1)])), 0, 0, B1, 'BASE', 0)], base=1005, detail=1006)
obj('LINK_ONE', [1005], NONE, 0, 0, [(prog('lk_one', link([(-1, -15, 1, -4)])), 0, 0, B1, 'BASE', 0)], base=1005, detail=1006)


def rays(n, r0, r1, w0, w1, a=.5):
    ops = []
    for k in range(n):
        t = 2 * math.pi * k / n
        c, s = math.cos(t), math.sin(t)
        pts = []
        for (r, w) in ((r0, w0), (r1, w1)):
            pts.append((c * r - s * w / 2, s * r + c * w / 2))
        for (r, w) in ((r1, w1), (r0, w0)):
            pts.append((c * r + s * w / 2, s * r - c * w / 2))
        ops.append(P(pts, 'BASE', a))
        mid = (r0 + r1) / 2
        ops.append(P([(c * r0 - s * w0 / 4, s * r0 + c * w0 / 4), (c * mid - s * w1 / 4, s * mid + c * w1 / 4), (c * mid + s * w1 / 4, s * mid - c * w1 / 4),
                      (c * r0 + s * w0 / 4, s * r0 - c * w0 / 4)], 'BASE', a * .6))
    return ops


obj('SHINE_L', [1019], NONE, 0, 0, [(prog('shine_l', rays(8, 12, 64, 4, 18, .35)), 0, 0, B1, 'BASE', 0)], base=1005, anim='SPIN')
obj('SHINE_M', [1020], NONE, 0, 0, [(prog('shine_m', rays(8, 10, 52, 3, 14, .35)), 0, 0, B1, 'BASE', 0)], base=1005, anim='SPIN')

# sprite based decoration (sprites_lv.py)
obj('SPLIT_CIRCLE_L', [997], NONE, 0, 0, [('ringseg_l', 0, 0, B1, 'BASE', F_QUAD)], base=1005, anim='SPIN')
obj('SPLIT_CIRCLE_M', [998], NONE, 0, 0, [('ringseg_m', 0, 0, B1, 'BASE', F_QUAD)], base=1005, anim='SPIN')
obj('SPLIT_CIRCLE_S', [999], NONE, 0, 0, [('ringseg_s', 0, 0, B1, 'BASE', F_QUAD)], base=1005, anim='SPIN')
obj('SPLIT_CIRCLE_XS', [1000], NONE, 0, 0, [('ringseg_xs', 0, 0, B1, 'BASE', F_QUAD)], base=1005, anim='SPIN')
obj('SWIRL_L', [1058], NONE, 0, 0, [('swirl_l', 0, 0, B1, 'BASE', 0)], base=1007, anim='SPIN')
obj('SWIRL_M', [1059], NONE, 0, 0, [('swirl_m', 0, 0, B1, 'BASE', 0)], base=1007, anim='SPIN')
obj('PICKUP_RING1', [1055], NONE, 0, 0, [('pickup_ring1', 0, 0, B1, 'BASE', F_PULSE)], base=1007, anim='SPIN')
obj('PICKUP_RING2', [1056], NONE, 0, 0, [('pickup_ring2', 0, 0, B1, 'BASE', F_PULSE)], base=1007, anim='SPIN')
obj('PICKUP_RING4', [1057], NONE, 0, 0, [('pickup_ring4', 0, 0, B1, 'BASE', F_PULSE)], base=1007, anim='SPIN')
obj('FIRE_LARGE', [920], NONE, 0, 0, [('fire_l', 0, 0, T1, 'BASE', 0), ('fire_l_in', 0, -4, T1, 'DETAIL', 0)], base=1011, detail=1, anim='FLAME')
obj('FIRE_BURST', [921], NONE, 0, 0, [('fire_burst', 0, 0, T1, 'BASE', 0), ('fire_burst_in', 0, -6, T1, 'DETAIL', 0)], base=1011, detail=1, anim='FLAME')
obj('FIRE_THIN1', [923], NONE, 0, 0, [('fire_t1', 0, 0, T1, 'BASE', 0), ('fire_t1_in', 0, -4, T1, 'DETAIL', 0)], base=1011, detail=1, anim='FLAME')
obj('FIRE_THIN2', [924], NONE, 0, 0, [('fire_t2', 0, 0, T1, 'BASE', 0), ('fire_t2_in', 0, -4, T1, 'DETAIL', 0)], base=1011, detail=1, anim='FLAME')

# ---------------------------------------------------------------- hazards with sprites
obj('COGSAW_L', [675], HAZARD, 32, 32, [('cogsaw_l', 0, 0, T1, 'BLACK', F_QUAD), ('cogsaw_l_ring', 0, 0, T1, 'BASE', F_QUAD)], shape='CIRCLE', anim='SAW')
obj('COGSAW_M', [676], HAZARD, 17.68, 17.68, [('cogsaw_m', 0, 0, T1, 'BLACK', 0), ('cogsaw_m_ring', 0, 0, T1, 'BASE', 0)], shape='CIRCLE', anim='SAW')
obj('COGSAW_S', [677], HAZARD, 12.48, 12.48, [('cogsaw_s', 0, 0, T1, 'BLACK', 0), ('cogsaw_s_ring', 0, 0, T1, 'BASE', 0)], shape='CIRCLE', anim='SAW')
obj('DARKBLADE_L', [397], HAZARD, 28.9, 28.9, [('darkblade_l', 0, 0, T1, 'BLACK', F_QUAD), ('darkblade_l_core', 0, 0, T1, 'BASE', 0)], shape='CIRCLE', anim='SAW', detail=1)
obj('DARKBLADE_M', [398], HAZARD, 17.6, 17.6, [('darkblade_m', 0, 0, T1, 'BLACK', 0), ('darkblade_m_core', 0, 0, T1, 'BASE', 0)], shape='CIRCLE', anim='SAW', detail=1)
obj('BEAST', [918], HAZARD, 24, 24, [('beast_top', 0, 8, T1, 'BASE', F_ANIM), ('beast_bot', 0, -14, T1, 'BASE', F_ANIM), ('beast_eye', 18, 12, T1, 'DETAIL', 0)],
    shape='CIRCLE', anim='CHOMP', base=1010, detail=1011)

# ---------------------------------------------------------------- portals, rings and collectables
obj('SPEED_SLOW', [200], SPECIAL, 35, 44, [('speed0', 0, 0, 'PORTAL_FRONT', 'WHITE', 0)], special='SPEED_0')
obj('SPEED_NORMAL', [201], SPECIAL, 33, 56, [('speed1', 0, 0, 'PORTAL_FRONT', 'WHITE', 0)], special='SPEED_1')
obj('SPEED_FAST', [202], SPECIAL, 51, 56, [('speed2', 0, 0, 'PORTAL_FRONT', 'WHITE', 0)], special='SPEED_2')
obj('SPEED_FASTER', [203], SPECIAL, 65, 56, [('speed3', 0, 0, 'PORTAL_FRONT', 'WHITE', 0)], special='SPEED_3')
obj('WAVE_PORTAL', [660], SPECIAL, 34, 86, [('portal_wave_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_wave_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)], special='PORTAL_WAVE')
obj('ROBOT_PORTAL', [745], SPECIAL, 34, 86, [('portal_robot_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_robot_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)], special='PORTAL_ROBOT')
obj('DUAL_PORTAL', [286], SPECIAL, 41, 91, [('portal_dual_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_dual_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)], special='DUAL_ON')
obj('DUAL_EXIT', [287], SPECIAL, 41, 91, [('portal_single_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_single_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)], special='DUAL_OFF')
obj('TELEPORT', [747], SPECIAL, 25, 90, [('portal_tele_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_tele_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)],
    special='TELEPORT', hx=12)
obj('ORB_GREEN', [1022], SPECIAL, 36, 36, [('orb_green', 0, 0, 'SPECIAL', 'WHITE', F_PULSE), ('glow_orb_green', 0, 0, 'SPECIAL_GLOW', 'GLOW_Y', 0)], special='ORB_G')
obj('KEY', [1275], SPECIAL, 25, 20, [('key', 0, 0, 'COIN', 'BASE', 0), ('key_detail', 0, 0, 'COIN', 'DETAIL', 0)], special='KEY', base=1, detail=1011)
obj('KEYHOLE', [1276], NONE, 0, 0, [('keyhole', 0, 0, T1, 'BASE', 0), ('keyhole_detail', 0, 0, T1, 'DETAIL', 0)], base=1, detail=1012)

# reach of the big ones (units), for streaming
RADIUS = {'GLOW_OUTER_L': 45, 'OUT_WSLOPE': 50, 'BLOCK_WSLOPE': 50, 'DECO_WSLOPE': 50, 'NEON_WSLOPE': 50, 'SHINE_L': 80, 'SHINE_M': 70,
          'SPLIT_CIRCLE_L': 70, 'SPLIT_CIRCLE_M': 60, 'SPLIT_CIRCLE_S': 50, 'COGSAW_L': 50, 'DARKBLADE_L': 50, 'BEAST': 60, 'FIRE_LARGE': 55,
          'FIRE_BURST': 50, 'TELEPORT': 60, 'DUAL_PORTAL': 50, 'DUAL_EXIT': 50, 'SPEED_FASTER': 45}
