"""Object catalogue of Dash (level 22, GD 2.2): its gameplay objects (spider
and swing portals, dash, spider, red and toggle rings, the red pad, force
fields, collision blocks, the 2.2 teleport, modifier blocks, pixel
collectables, monsters) and its pixel-art decoration.

The pixel art is redrawn here as small tile programs: an 8 x 8 grid of
two-unit cells per tile (GD scales its 16 unit tiles to a block), drawn in
the tile's base colour, its detail colour and their darker and lighter
shades. The designs are NumDash's own, after the originals' general shapes
(bricks, stone, lava, pillars, edges); tiles not drawn here get a generic
stone pattern in their colours.
"""
import zlib
from objdefs_dl import R, P, VG, seg, prog, radial, outline, outline_glow, glow_band, NONE, SOLID, HAZARD, SPECIAL, F_PULSE, F_QUAD, F_TERRAIN, \
    B2, B1, T1, GL

OBJS = []


def obj(name, ids, hit, w, h, parts, special='NONE', edy=0, **extra):
    OBJS.append((name, ids, hit, w, h, special, edy, parts, extra))


# ---------------------------------------------------------------- pixel tiles
PIX_CT = {'b': 'BASE', 'd': 'DETAIL', 'D': 'BASE_D', 'L': 'BASE_L', 'e': 'DETAIL_D', 'k': 'BLACK', 'w': 'WHITE'}


def pix_ops(rows, cell=2.0):
    """Rectangles for an 8 x 8 (or smaller) character grid, runs merged
    horizontally then vertically. y up: rows[0] is the top."""
    n = len(rows)
    w = max(len(r) for r in rows)
    runs = []
    for j, row in enumerate(rows):
        i = 0
        while i < len(row):
            ch = row[i]
            if ch == '.' or ch == ' ':
                i += 1
                continue
            k = i
            while k < len(row) and row[k] == ch:
                k += 1
            runs.append([i, k, j, j + 1, ch])
            i = k
    # merge vertically identical runs
    merged = []
    for r in runs:
        for m in merged:
            if m[0] == r[0] and m[1] == r[1] and m[3] == r[2] and m[4] == r[4]:
                m[3] = r[3]
                break
        else:
            merged.append(r)
    x0, y0 = -w * cell / 2, n * cell / 2
    return [R(x0 + a * cell, y0 - d * cell, x0 + b * cell, y0 - c * cell, PIX_CT[ch]) for a, b, c, d, ch in merged]


def pix(name, ids, rows, layer=B1, **kw):
    obj(name, ids, NONE, 0, 0, [(prog('px_' + name.lower(), pix_ops(rows)), 0, 0, layer, 'BASE', F_TERRAIN)], base=1, detail=2, **kw)


# bricks and stone blocks
pix('PX_LAVA_BRICK', [2467], ['DbbbDbbb', 'bdbbDbdb', 'DDDDDDDD', 'bbDbbbbD', 'bdDbbdbD', 'DDDDDDDD', 'bbbDbbbb', 'dbbDbbdb'])
pix('PX_SOLID', [2466], ['bbbbbbbb'] * 8)
obj('PX_UNIT', [3092, 3097, 2695, 2549], NONE, 0, 0, [(prog('px_unit', [R(-.5, -.5, .5, .5)]), 0, 0, B1, 'BASE', F_TERRAIN)], base=1)
pix('PX_BRICK_TOP', [2465, 2463, 2462, 2464], ['LLLLLLLL', 'bdbbbdbb', 'DDDDDDDD', 'bbbDbbbb', 'bdbDbbdb', 'DDDDDDDD', 'bDbbbbDb', 'bDbbdbDb'])
pix('PX_BRICK', [2468, 2491, 2572, 2412, 2550, 2548, 2540, 4001, 4003, 4008, 4009, 2490, 2476, 2470, 2471, 2472, 2475, 2480],
    ['bbbDbbbb', 'bdbDbbdb', 'DDDDDDDD', 'bDbbbbDb', 'bDbdbbDb', 'DDDDDDDD', 'bbbDbbbb', 'dbbDbbbd'])
pix('PX_STONE', [2596, 2593, 2595, 2597, 2594, 2613, 2555, 2557, 2541, 2543, 2547],
    ['LbbbbLbb', 'bbDbbbbL', 'bDDbbDbb', 'bbbbDDbb', 'LbbbbbbD', 'bbDbbbDD', 'bDDbLbbb', 'bbbbbbbb'])
pix('PX_LAVA_TOP', [4318, 1591], ['........', 'dd..dd.d', 'dbddbbdd', 'bbbbbbbb', 'bdbbbbdb', 'bbbbdbbb', 'bbbbbbbb', 'bbdbbbbb'])
pix('PX_EDGE_V', [4372, 4371, 4375, 4373, 4374], ['......LL', '......L.', '......LL', '......L.', '......LL', '......L.', '......LL', '......L.'])
pix('PX_EDGE_H', [4368, 4369, 4370], ['LLLLLLLL', 'L.L.L.L.', '........', '........', '........', '........', '........', '........'])
pix('PX_SPIKES3', [2538, 2598], ['.b...b..', '.b..bb..', 'bd.bdb.b', 'bd.bdbbd', 'bdbbddbd', 'bddbddbd', 'bddbddbd', 'bbbbbbbb'])
pix('PX_SPIKE_WIDE', [2310, 2306], ['........', '........', '...bb...', '..bddb..', '..bddb..', '.bdddDb.', '.bddddb.', 'bbbbbbbb'])
pix('PX_SPIKE_THIN', [2255, 2316], ['...b....', '...b....', '..bd....', '..bd....', '..bdb...', '.bddb...', '.bddb...', 'bbbbbb..'])
pix('PX_DOTS', [2312, 2315], ['...d....', '...b....', '........', '...d....', '...b....', '........', '...d....', '...b....'])
pix('PX_PILLAR_TOP', [2360, 2364], ['LLLLLLLL', 'bbbbbbbb', '.bdbbdb.', '.bdbbdb.', '..bddb..', '..bddb..', '..bddb..', '..bddb..'])
pix('PX_PILLAR', [2361, 2362], ['..bddb..'] * 8)
pix('PX_CHAIN', [4187, 2622, 2624, 2625], ['..bbb...', '.b...b..', '.b...b..', '..bbb...', '..bbb...', '.b...b..', '.b...b..', '..bbb...'])
pix('PX_SPARKS', [4322, 4321, 4320, 2630, 2629], ['........', '..d.....', '.ddd..d.', '..dbd.dd', '...bbd..', '..d.b...', '.d......', '........'])
pix('PX_GRASS', [2481, 2338], ['..d...d.', '.dd.d.dd', 'ddddddd.', 'dbdbddbd', 'bbbbbbbb', 'bDbbbDbb', 'bbbbbbbb', 'bbDbbbbb'])
pix('PX_CRATE', [2417], ['DDDDDDDD', 'DbbbbbdD', 'DbDbbdbD', 'DbbDdbbD', 'DbbdDbbD', 'DbdbbDbD', 'DdbbbbbD', 'DDDDDDDD'])
pix('PX_WEB', [2620, 2619, 2621], ['LLLLLLLL', 'L.L..L..', 'L..L.L..', 'LLLLLLL.', 'L...LL..', 'L..L.L..', 'L.L..L..', 'LL......'])
pix('PX_SKULL', [1602], ['..LLLL..', '.LLLLLL.', 'LkkLLkkL', 'LkkLLkkL', 'LLLkkLLL', '.LLLLLL.', '..L.L.L.', '........'])


def generic_pixel(gid):
    """A stone tile in the object's colours, varied by its id."""
    h = zlib.crc32(str(gid).encode())
    rows = []
    for j in range(8):
        r = ''
        for i in range(8):
            v = (h >> ((i * 3 + j * 5) % 29)) & 7
            r += 'd' if v == 0 else 'D' if v == 1 else 'L' if v == 2 else 'b'
        rows.append(r)
    return rows


DONE = {i for o in OBJS for i in o[1]}
GENERIC_PIXEL = [2101, 2103, 2104, 2118, 2119, 2123, 2124, 2125, 2143, 2171, 2172, 2222, 2372, 2373, 2374, 2375, 2407, 2414, 2452, 2469,
                 2488, 2489, 2498, 2499, 2501, 2506, 2507, 2529, 2530, 2536, 2537, 2542, 2546, 2551, 2554, 2570, 2578, 2579, 2600, 2601,
                 2602, 2603, 2604, 2632, 2646, 2694, 3458, 3576, 3577, 3578, 4000, 4082, 4231, 4233, 4269, 4300, 4384, 2083]
for gid in GENERIC_PIXEL:
    if gid not in DONE:
        pix('PX_%d' % gid, [gid], generic_pixel(gid))

# ---------------------------------------------------------------- glows (base colour, soft)
obj('GLOW_ORB_L', [1888], NONE, 0, 0, [(prog('go_l', radial(0, 0, 15, 0, 360, .7, 0, 5)), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_ORB_M', [1886], NONE, 0, 0, [(prog('go_m', radial(0, 0, 10, 0, 360, .7, 0, 4)), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_ORB_S', [1887], NONE, 0, 0, [(prog('go_s', radial(0, 0, 5, 0, 360, .7, 0, 3)), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_SLOPE_L', [1762], NONE, 0, 0, [(prog('gs_l', [P([(-15, -15), (15, 15), (15, 5), (-5, -15)], 'BASE', .5), P([(-5, -15), (15, 5), (15, -5), (5, -15)], 'BASE', .25)]), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_SLOPE_M2', [1759], NONE, 0, 0, [(prog('gs_m2', [P([(-30, -15), (30, 15), (30, 7), (-14, -15)], 'BASE', .5)]), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_SLOPE_CORNER', [1269], NONE, 0, 0, [(prog('gs_c', [P([(-15, 15), (15, 15), (15, 5), (-5, 5)], 'BASE', .45)]), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_OUTER_S', [1009], NONE, 0, 0, [(prog('g_outer_s', radial(5, -5, 10, 90, 180, .8, 0, 3)), 0, 0, B2, 'BASE', 0)], base=1)
obj('GLOW_INNER_L', [1013], NONE, 0, 0, [(prog('g_inner_l', [VG(-15, -15, 15, 15, 'BASE', 0, .7)]), 0, 0, B2, 'BASE', 0)], base=1)

# ---------------------------------------------------------------- blocks, slopes, spikes
obj('GRID_SLOPE', [1743], SOLID, 30, 30, [(prog('o_gslope', [seg(-15, -15, 15, 15, 1.8), R(13.2, -15, 15, 15)]), 0, 0, T1, 'BASE', 0),
                                          (prog('og_slope', glow_band(-15, -15, 15, 15, 5)), 0, 0, GL, 'GLOW', 0)], shape='SLOPE')
obj('GRID_WSLOPE', [1744, 1746], SOLID, 60, 30, [(prog('o_gwslope', [seg(-30, -15, 30, 15, 1.8), R(28.2, -15, 30, 15)]), 0, 0, T1, 'BASE', 0),
                                                 (prog('og_wslope', glow_band(-30, -15, 30, 15, 5)), 0, 0, GL, 'GLOW', 0)], shape='SLOPE')
obj('GRASS_BASE', [955], NONE, 0, 0, [(prog('grass_base', [R(-15, -15, 15, 15, 'BLACK')]), 0, 0, B2, 'BASE', 0)])
obj('OUT_SMALL_SQUARE', [661], SOLID, 15, 15, [(prog('o_sq_s', outline('tblr', 1.5, 7.5, 7.5)), 0, 0, T1, 'BASE', 0),
                                               (prog('og_sq_s', outline_glow('tblr', 7.5, 7.5, 4)), 0, 0, GL, 'GLOW', 0)])
obj('GRAD_SLAB', [1903], SOLID, 30, 14, [(prog('gr_slab', [VG(-15, -7, 15, 7, 'DETAIL', .9, .2)] + outline('tblr', 1.5, 7, 15)), 0, 0, T1, 'BASE', 0)], detail=1)
obj('GRAD_SMALL', [1910], SOLID, 15, 15, [(prog('gr_small', [VG(-7.5, -7.5, 7.5, 7.5, 'DETAIL', .9, .2)] + outline('tblr', 1.5, 7.5, 7.5)), 0, 0, T1, 'BASE', 0)], detail=1)
obj('BSPIKE_TINY', [392], HAZARD, 2.6, 4.8, [(prog('bs_tiny', [P([(-6, -6), (0, 6), (6, -6)], 'BLACK'), seg(-6, -6, 0, 6, 1, 'BASE'), seg(0, 6, 6, -6, 1, 'BASE')]),
                                               0, 0, T1, 'BASE', 0)], edy=-9)
obj('CHAIN_LINK', [498], NONE, 0, 0, [(prog('chain_link', [R(-2.5, -9, -1, 9), R(1, -9, 2.5, 9), R(-2.5, 7.5, 2.5, 9), R(-2.5, -9, 2.5, -7.5)]), 0, 0, B1, 'BASE', 0)], base=1)

# ---------------------------------------------------------------- hazards
obj('MONSTER_S', [1327], HAZARD, 4, 4, [(prog('monster_s', radial(0, 0, 12, 0, 360, 1, 1, 1) + [R(3, 2, 7, 6, 'DETAIL'), R(4, -6, 9, -4, 'DETAIL')]),
                                         0, 0, T1, 'BASE', 0)], shape='CIRCLE', base=1010, detail=1011)
obj('MONSTER_SPIKED', [2012], HAZARD, 12, 12, [('cogsaw_s', 0, 0, T1, 'BASE', 0), ('beast_eye', 4, 3, T1, 'DETAIL', 0)], shape='CIRCLE', anim='SAW',
    base=1010, detail=1011)
obj('SCYTHE', [1619], HAZARD, 25, 25, [('darkblade_m', 0, 0, T1, 'BASE', 0), ('darkblade_m_core', 0, 0, T1, 'DETAIL', 0)], shape='CIRCLE', anim='SAW',
    base=1004, detail=1)
obj('FIREBALL', [1583], HAZARD, 4, 4, [('fire_t1', 0, 0, T1, 'BASE', 0), ('fire_t1_in', 0, -3, T1, 'DETAIL', 0)], shape='CIRCLE', anim='FLAME',
    base=1, detail=1012)

# ---------------------------------------------------------------- animated decoration (simple stand-ins)
obj('ENERGY_BALL', [2032], NONE, 0, 0, [(prog('en_ball', radial(0, 0, 9, 0, 360, 1, .3, 3)), 0, 0, T1, 'BASE', F_PULSE),
                                        (prog('en_core', radial(0, 0, 4, 0, 360, 1, .6, 2, 'DETAIL')), 0, 0, T1, 'BASE', 0)], base=1, detail=2)
obj('CIRCLE_WAVE_ANIM', [3000], NONE, 0, 0, [('pickup_ring2', 0, 0, B1, 'BASE', F_PULSE)], base=1, anim='SPIN')
obj('ELECTRIC', [2888, 1857, 2893, 2887], NONE, 0, 0, [(prog('electric', [seg(-20, 10, -5, 2, 2, 'BASE'), seg(-5, 2, 3, 12, 2, 'BASE'), seg(3, 12, 20, -4, 2, 'BASE'),
                                                                         seg(-5, 2, 3, 12, .8, 'DETAIL')]), 0, 0, T1, 'BASE', F_PULSE)], base=1, detail=2, anim='FLAME')
obj('BUBBLES', [1856], NONE, 0, 0, [('pickup_ring1', 0, 0, B1, 'BASE', F_PULSE)], base=1011, anim='SPIN')
obj('SPARKLE', [1519, 2024], NONE, 0, 0, [(prog('sparkle', [P([(0, 6), (1.5, 1.5), (6, 0), (1.5, -1.5), (0, -6), (-1.5, -1.5), (-6, 0), (-1.5, 1.5)][:8])]), 0, 0, T1, 'BASE', F_PULSE)],
    base=1011, anim='SPIN')
obj('BLOBS', [1936, 1937, 1939], NONE, 0, 0, [('fire_t1', 0, 0, T1, 'BASE', 0)], base=1011, anim='FLAME')
obj('SMOKE', [2042, 2041], NONE, 0, 0, [(prog('smoke', radial(0, 0, 20, 0, 360, .35, 0, 4)), 0, 0, B1, 'BASE', 0)], base=1)
obj('FLAME_WIDE', [2864, 2865, 2047], NONE, 0, 0, [('fire_l', 0, 0, T1, 'BASE', 0), ('fire_l_in', 0, -4, T1, 'DETAIL', 0)], base=1, detail=2, anim='FLAME')
obj('SLASH', [2027, 2031, 2043, 2044], NONE, 0, 0, [('swirl_m', 0, 0, B1, 'BASE', 0)], base=1, anim='SPIN')
obj('TRI_SWIRL', [1752], NONE, 0, 0, [('swirl_l', 0, 0, B1, 'BASE', 0)], base=1007, anim='SPIN')
obj('ARROW2', [460], NONE, 0, 0, [(prog('arrow2', [P([(-14, -20), (14, 0), (-14, 20), (-6, 0)])]), 0, 0, B1, 'BASE', F_PULSE)], base=1006)
obj('THIN_LINE', [1753, 1754], NONE, 0, 0, [(prog('thin_line', [R(-15, -.5, 15, .5)]), 0, 0, B1, 'BASE', 0)], base=1007)
obj('SIGN', [1600], NONE, 0, 0, [(prog('sign', [R(-12, -2, 12, 12, 'BASE'), R(-10, 0, 10, 10, 'DETAIL'), R(-1.5, -14, 1.5, -2, 'BASE')]), 0, 0, B1, 'BASE', 0)],
    base=1011, detail=1)
obj('SIGN_POST', [1601], NONE, 0, 0, [(prog('sign_post', [R(-1.5, -20, 1.5, 20, 'BASE'), R(-3, 17, 3, 20, 'DETAIL')]), 0, 0, B1, 'BASE', 0)], base=1011, detail=1)
obj('CARTOON_ARROW', [1603], NONE, 0, 0, [(prog('c_arrow', [R(-8, -2.5, 2, 2.5), P([(2, -7), (9, 0), (2, 7)])]), 0, 0, B1, 'BASE', 0)], base=1011)
obj('GRASS_PATCH', [942], NONE, 0, 0, [(prog('grass_patch', [P([(-5, -4), (-3, 4), (-1, -4)]), P([(0, -4), (2, 3), (4, -4)])]), 0, 0, B1, 'BASE', 0)], base=1)

# ---------------------------------------------------------------- gameplay
obj('SPIDER_PORTAL', [1331], SPECIAL, 34, 86, [('portal_spider_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_spider_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)],
    special='PORTAL_SPIDER')
obj('SWING_PORTAL', [1933], SPECIAL, 34, 86, [('portal_swing_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_swing_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)],
    special='PORTAL_SWING')
obj('DASH_ORB', [1704], SPECIAL, 36, 36, [('orb_dash', 0, 0, 'SPECIAL', 'WHITE', F_PULSE), ('glow_orb_green', 0, 0, 'SPECIAL_GLOW', 'GLOW_Y', 0)], special='ORB_DASH')
obj('DASH_ORB_G', [1751], SPECIAL, 36, 36, [('orb_dash_g', 0, 0, 'SPECIAL', 'WHITE', F_PULSE), ('glow_orb_red', 0, 0, 'SPECIAL_GLOW', 'GLOW_P', 0)],
    special='ORB_DASH_G')
obj('SPIDER_ORB', [3004], SPECIAL, 36, 36, [('orb_spider', 0, 0, 'SPECIAL', 'WHITE', F_PULSE), ('glow_orb_red', 0, 0, 'SPECIAL_GLOW', 'GLOW_P', 0)],
    special='ORB_SPIDER')
obj('RED_ORB', [1333], SPECIAL, 36, 36, [('orb_red', 0, 0, 'SPECIAL', 'WHITE', F_PULSE), ('glow_orb_red', 0, 0, 'SPECIAL_GLOW', 'GLOW_P', 0)], special='ORB_R')
obj('RED_PAD', [1332], SPECIAL, 25, 4, [('pad_red', 0, -1, 'SPECIAL', 'WHITE', 0)], special='PAD_R', edy=-12)
obj('TOGGLE_ORB', [1594], SPECIAL, 36, 36, [('orb_toggle', 0, 0, 'SPECIAL', 'BASE', F_PULSE), ('orb_toggle_detail', 0, 0, 'SPECIAL', 'DETAIL', 0)],
    special='ORB_T', base=1011, detail=1)
obj('TELEPORT2', [2902], SPECIAL, 25, 90, [('portal_tele_front', 5, 0, 'PORTAL_FRONT', 'WHITE', 0), ('portal_tele_back', -6, 0, 'PORTAL_BACK', 'WHITE', 0)],
    special='TELEPORT2', hx=12)
obj('COLLECT_ITEM', [4401, 4506, 4507, 4508], SPECIAL, 15, 15, [('pickup_item', 0, 0, 'COIN', 'BASE', 0), ('pickup_item_detail', 0, 0, 'COIN', 'DETAIL', 0)],
    special='ITEM', base=1, detail=1011)
# editor-only blocks: invisible in play
obj('FORCE_BLOCK', [2069], SPECIAL, 30, 30, [], special='FORCE')
obj('FORCE_CIRCLE', [3645], SPECIAL, 15, 15, [], special='FORCE_CIRCLE', shape='CIRCLE')
obj('COLLISION_BLOCK', [1816], NONE, 30, 30, [])
obj('STOP_DASH', [1829], SPECIAL, 30, 30, [], special='STOP_DASH')
# gameplay modifiers: touching one arms a counter for this tick and the next
obj('HEAD_COLLIDE', [1859], SPECIAL, 30, 30, [], special='ARM_HEAD')
obj('WAVE_DRAG', [1755], SPECIAL, 30, 30, [], special='ARM_SLIDE')
obj('GRAV_MODIFIER', [2866], SPECIAL, 30, 30, [], special='ARM_FLIP')
obj('STOP_BUFFER', [1813], SPECIAL, 30, 30, [], special='ARM_NOAUTO')

RADIUS = {'GLOW_SLOPE_M2': 50, 'GRID_WSLOPE': 50, 'SCYTHE': 60, 'ELECTRIC': 40, 'SMOKE': 40, 'FLAME_WIDE': 55, 'SPIDER_PORTAL': 50,
          'SWING_PORTAL': 50, 'TELEPORT2': 60, 'MONSTER_SPIKED': 30}
