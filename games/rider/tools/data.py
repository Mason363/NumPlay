#!/usr/bin/env python3
"""src/data.h from Rider's own files (the iOS app's data folder and its Lua tables, unpacked into
~/Documents/Assets/gamesrc/rider, see its spec/). Run with python3 -I tools/data.py GAMESRC_RIDER.

The tracks: every level and every piece of the endless run, each object as its physics outline (a PhysicsEditor
template, scaled, flipped, turned), its animation and its flags. The bikes: their outline traced from the body
picture's edge (drawn again here as a polygon, the picture itself is not kept), their wheels and their collision
outline. Colours, star times, level groups, unlock rules, challenges and texts as the game has them."""
import json, math, os, re, sys
from PIL import Image

RID = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Documents/Assets/gamesrc/rider'))
sys.path.insert(0, os.path.join(RID, 'tools'))
from levels import objects, load, APP, BASE, imgdim, world  # noqa: E402 (the reader of the game's files)
from sheets import frame  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'src', 'data.h')
props = lambda n: json.load(open(os.path.join(RID, 'props', n + '.json')))
cfg = props('code.config.properties')
out = []
def arr(ctype, name, vals, per=16):
    out.append('static const %s %s[%d] = {' % (ctype, name, len(vals)))
    for i in range(0, len(vals), per):
        out.append('  ' + ', '.join(str(v) for v in vals[i:i + per]) + ',')
    out.append('};')
def ff(x):
    t = '%.6g' % x
    return (t if ('.' in t or 'e' in t) else t + '.0') + 'f'
def rgb(c): return '0x%02X%02X%02X' % tuple(max(0, min(255, int(round(x * 255)))) for x in c[:3])
def truthy(v): return v in (True, 'True', 'true', 1)

# ---------------------------------------------------------------- templates (physics outlines)
cats = load(APP + '/data/levels/levelsCategories.json')['categories']
levels = [l for c in cats for l in c['levels']]
endless = load(APP + '/data/chunks/levelsEndless.json')
groups = [endless['levelChunks'][str(i)] for i in range(1, 5)]
chunks = ['levelChunkStart01'] + sorted(set(x for g in groups for x in g))
KIND = {'ground': 0, 'platform': 1, 'enemy': 2, 'boost': 3, 'currency': 4, 'finish': 5}
tpl_names = sorted(n for n in BASE if not n.startswith('motor'))
tpl_index = {n: i for i, n in enumerate(tpl_names)}
tpl_poly, poly_vert, verts = [], [], []
for n in tpl_names:
    tpl_poly.append(len(poly_vert))
    for fx in BASE[n].values():
        if fx.get('isSensor'): continue
        s = fx['shape']; pts = [(s[str(i)], s[str(i + 1)]) for i in range(1, len(s), 2)]
        # Box2D wants them anticlockwise in y-down screen terms; keep them one way round
        area = sum(pts[i][0] * pts[(i + 1) % len(pts)][1] - pts[(i + 1) % len(pts)][0] * pts[i][1] for i in range(len(pts)))
        if area < 0: pts.reverse()
        poly_vert.append(len(verts) // 2)
        for x, y in pts: verts += [int(round(x * 4)), int(round(y * 4))]
tpl_poly.append(len(poly_vert)); poly_vert.append(len(verts) // 2)

# ---------------------------------------------------------------- tracks
EASE = ['linear', 'inOutQuad', 'outBounce', 'outBack', 'inQuad', 'outQuad', 'inOutSine']
objs, anims, steps, tracks = [], [], [], []
def steps_of(v):
    if not v: return []
    if isinstance(v, str): v = json.loads(v)
    return [(float(a), int(b), EASE.index(c), int(d)) for a, b, c, d in v]
def track(path, kind):
    obs, d = objects(path)
    by_id = {o['id']: o for o in obs}
    start, finish, first = None, None, len(objs)
    kept = [o for o in obs if o['type'] in KIND or o['type'] == 'character']
    children = {}
    for o in kept:
        g = o['props'].get('animGroundChild')
        if g is not None and int(g) in by_id: children[int(g)] = o['id']
    order = [o for o in kept if o['type'] != 'character']
    # grounds first (drawn under everything), then the rest
    order.sort(key=lambda o: (o['type'] != 'ground',))
    index = {o['id']: first + i for i, o in enumerate(order)}
    for o in kept:
        if o['type'] == 'character': start = (o['cx'], o['cy'])
    width = d['width'] * d['tilewidth']
    for o in order:
        p = o['props']; k = KIND[o['type']]
        flags = 0
        if truthy(p.get('dontDieOnCollision')): flags |= 1
        falls = truthy(p.get('physicsActiveOnCollision'))
        if falls: flags |= 2
        if truthy(p.get('physicsActive')): flags |= 2 | 4   # moved by physics from the start: falls at once
        falls = falls or bool(flags & 4)
        rot_s, px_s, py_s = steps_of(p.get('animRotation')), steps_of(p.get('animPositionX')), steps_of(p.get('animPositionY'))
        anim = 0xFFFF
        parent = children.get(o['id'])
        if rot_s or px_s or py_s or falls or parent is not None:
            ax = float(p.get('animAnchorX', 0.5)) - 0.5; ay = float(p.get('animAnchorY', 0.5)) - 0.5
            # the pivot in the object's own frame (as drawn: y down), turned with it
            a = math.radians(o['rot']); lx, ly = ax * o['w'], ay * o['h']
            pvx, pvy = o['cx'] + math.cos(a) * lx - math.sin(a) * ly, o['cy'] + math.sin(a) * lx + math.cos(a) * ly
            loops = int(p.get('animLoops', -1))
            af = (1 if truthy(p.get('animStartOnCollision')) else 0)
            anim = len(anims)
            anims.append([af, max(-1, min(100, loops)), int(p.get('animStartDelay', 0)), int(p.get('physicsActiveDelay', 0)),
                          int(round(pvx)), int(round(pvy)), len(steps), len(rot_s), len(px_s), len(py_s),
                          index.get(parent, 0xFFFF) if parent is not None else 0xFFFF])
            for st in rot_s + px_s + py_s: steps.append(st)
        if o['type'] == 'finish': finish = o['cx']
        n = o['name']
        if n in tpl_index:
            W, H = imgdim(n); sx, sy = o['w'] / W * (-1 if o['fh'] else 1), o['h'] / H * (-1 if o['fv'] else 1)
            pts = [q for sh in o['shapes'] if not sh['sensor'] for q in world(o, sh['pts'])]
            tpl = tpl_index[n]
        else:
            sx, sy, tpl = o['w'], o['h'], 0xFF
            hw, hh = o['w'] / 2, o['h'] / 2
            pts = world(o, [(-hw, -hh), (hw, -hh), (hw, hh), (-hw, hh)])
        if not pts: pts = [(o['cx'], o['cy'])]
        bb = [int(math.floor(min(q[0] for q in pts))), int(math.floor(min(q[1] for q in pts))),
              int(math.ceil(max(q[0] for q in pts))), int(math.ceil(max(q[1] for q in pts)))]
        objs.append([k, tpl, flags, anim, int(round(o['cx'])), int(round(o['cy'])), int(round(o['rot'] * 100)) % 36000,
                     ff(sx), ff(sy)] + bb)
    if start is None:   # the endless run's start piece: on its first platform
        x = 150.0
        tops = [q[1] for o in order if o['type'] == 'platform' for sh in o['shapes'] for q in world(o, sh['pts']) if abs(q[0] - x) < 40]
        start = (x, min(tops) - 23) if tops else (100, 1300)
    tracks.append([first, len(objs) - first, int(width), int(round(start[0])), int(round(start[1])),
                   int(round(finish)) if finish is not None else 0])
for l in levels: track(APP + '/data/levels/%s.json' % l, 'level')
for c in chunks: track(APP + '/data/chunks/%s.json' % c, 'chunk')

out.append('/* Generated by tools/data.py from Rider\'s own level, physics and settings files. Do not edit. */')
out.append('#define NTPL %d\n#define NLEVELS %d\n#define NCHUNKS %d\n#define NOBJ %d\n#define NANIM %d' %
           (len(tpl_names), len(levels), len(chunks), len(objs), len(anims)))
out.append('/* the physics outlines: tpl -> polygons -> vertices (quarter pixels, around the picture\'s centre) */')
arr('uint16_t', 'tpl_poly', tpl_poly); arr('uint16_t', 'poly_vert', poly_vert); arr('int16_t', 'tpl_xy', verts)
out.append('static const char *const tpl_name[NTPL] = {' + ', '.join('"%s"' % n for n in tpl_names) + '};')
out.append('/* kind, template, flags (1 never kills, 2 falls once touched, 4 falls from the start), animation, centre, turn\n'
           ' * (1/100 degree), scale x and y (negative: flipped), bounding box */')
out.append('static const rd_obj rd_objs[NOBJ] = {')
for o in objs: out.append('  {%s},' % ', '.join(str(v) for v in o))
out.append('};')
out.append('/* start on touch, loops (-1 for ever), start delay, fall delay (ms), pivot, first step, rotation, x and y\n'
           ' * steps, the ground that moves with it */')
out.append('static const rd_anim rd_anims[%d] = {' % max(1, len(anims)))
for a in anims: out.append('  {%s},' % ', '.join(str(v) for v in a))
out.append('};')
out.append('static const rd_step rd_steps[%d] = {' % max(1, len(steps)))
for s in steps: out.append('  {%s, %d, %d, %d},' % (ff(s[0]), s[1], s[2], s[3]))
out.append('};')
out.append('/* first object, objects, width, start, finish x */')
out.append('static const rd_track rd_tracks[%d] = {' % len(tracks))
for t, nm in zip(tracks, levels + chunks): out.append('  {%s}, /* %s */' % (', '.join(str(v) for v in t), nm))
out.append('};')

# ---------------------------------------------------------------- levels, groups, endless
times = load(APP + '/data/levels/levelsStarTime.json')
arr('uint16_t', 'star2', [times[l]['2'] // 10 for l in levels]); arr('uint16_t', 'star3', [times[l]['3'] // 10 for l in levels])
out.append('static const char *const group_name[6] = {%s};' % ', '.join('"%s"' % (c['name'] + (' ' + c['nameSuffix'] if c['nameSuffix'] else '')) for c in cats))
open_at = cfg['levelsToUnlockCategories']
arr('uint8_t', 'group_open', [open_at[c['name']] for c in cats]); arr('uint8_t', 'group_reward', [c['reward'] for c in cats])
def theme(t):
    return [rgb(t[k]) for k in ('bike_body', 'pf_x', 'pf_glow_x', 'pf_ground_x', 'trailColorStart', 'pickup_fill_color', 'pickup_glow_color')]
scapes = load(APP + '/code/config/colorScapes/scapes.json')
out.append('/* colours: bike, track core, track glow, ground, trail, gem, gem glow; 6 level groups then 10 scapes */')
out.append('static const uint32_t themes[16][7] = {')
for c in cats: out.append('  {%s},' % ', '.join(theme(c['colorTheme'])))
for k in sorted(scapes, key=int): out.append('  {%s},' % ', '.join(theme(scapes[k]['data'])))
out.append('};')
arr('uint8_t', 'scape_unlock', [scapes[k].get('unlockAtChallengeCompleted', 0) for k in sorted(scapes, key=int)])
ranges = endless['difficultyGroupScoreRange']
arr('uint16_t', 'endless_from', [r[0] for r in ranges])
cidx = {c: NL for NL, c in enumerate(chunks)}
arr('uint8_t', 'endless_n', [len(g) for g in groups])
arr('uint8_t', 'endless_pick', [cidx[c] for g in groups for c in g])

# ---------------------------------------------------------------- bikes
chars = load(APP + '/data/charactersDefinition/characters.json')
motors = props('physics_motors')['data']
def trace(im):
    a = im.split()[3]; w, h = im.size
    m = [[a.getpixel((x, y)) > 110 for x in range(w)] for y in range(h)]
    # the outline: Moore neighbour tracing from the leftmost lit pixel of the middle rows
    start = None
    for x in range(w):
        for y in range(h):
            if m[y][x]: start = (x, y); break
        if start: break
    dirs = [(1, 0), (1, 1), (0, 1), (-1, 1), (-1, 0), (-1, -1), (0, -1), (1, -1)]
    lit = lambda x, y: 0 <= x < w and 0 <= y < h and m[y][x]
    pts, cur, d = [start], start, 6
    for _ in range(20000):
        for k in range(8):
            nd = (d + 6 + k) % 8; nx, ny = cur[0] + dirs[nd][0], cur[1] + dirs[nd][1]
            if lit(nx, ny): cur, d = (nx, ny), nd; break
        if cur == start: break
        pts.append(cur)
    return pts
def simplify(pts, eps):
    if len(pts) < 3: return pts
    def rdp(a, b):
        (x1, y1), (x2, y2) = pts[a], pts[b]; L = math.hypot(x2 - x1, y2 - y1) or 1e-9
        best, bi = -1, -1
        for i in range(a + 1, b):
            dd = abs((x2 - x1) * (y1 - pts[i][1]) - (x1 - pts[i][0]) * (y2 - y1)) / L
            if dd > best: best, bi = dd, i
        if best > eps: return rdp(a, bi)[:-1] + rdp(bi, b)
        return [pts[a], pts[b]]
    far = max(range(len(pts)), key=lambda i: (pts[i][0] - pts[0][0]) ** 2 + (pts[i][1] - pts[0][1]) ** 2)
    return rdp(0, far)[:-1] + rdp(far, len(pts) - 1)[:-1] + [pts[-1]]
CAT = {1: 0, 2: 1, 3: 2, 7: 3, 6: 4}
bike_rows, outline, hull_poly, hull = [], [], [], []
for c in chars:
    im = frame('motor_body_' + c['id'])
    w, h = im.size
    pts, eps = [], 1.0
    while True:
        pts = simplify(trace(im), eps)
        if len(pts) <= 40: break
        eps *= 1.3
    o0 = len(outline) // 2
    for x, y in pts:   # sprite pixels round the centre, x0.7, in half pixels
        outline += [int(round((x + 0.5 - w / 2) * 0.7 * 2)), int(round((y + 0.5 - h / 2) * 0.7 * 2))]
    h0 = len(hull_poly)
    for fx in (motors.get('motor_body_' + c['id']) or {}).values():
        s = fx['shape']; q = [(s[str(i)], s[str(i + 1)]) for i in range(1, len(s), 2)]
        area = sum(q[i][0] * q[(i + 1) % len(q)][1] - q[(i + 1) % len(q)][0] * q[i][1] for i in range(len(q)))
        if area < 0: q.reverse()
        hull_poly.append(len(hull) // 2)
        for x, y in q: hull += [int(round(x * 0.7 * 2)), int(round(y * 0.7 * 2))]
    rule, val = 0, 0
    for r, k in enumerate(('unlockAtChallengeCompleted', 'unlockAtAmountDeaths', 'unlockAtHighscore', 'unlockAtAmountStars'), 1):
        if k in c: rule, val = r, c[k]
    bike_rows.append([CAT[c['categoryIdx']], rule, val, 1 if c['graphicsData'].get('wheelsInFront') else 0, o0, len(pts), h0, len(hull_poly) - h0, '"%s"' % c['name']])
hull_poly.append(len(hull) // 2); outline_end = len(outline) // 2
out.append('#define NBIKES %d' % len(chars))
out.append('/* class (common, rare, epic, special, secret), unlock rule (1 challenges, 2 runs, 3 best score, 4 stars) and\n'
           ' * its number, wheels in front, outline (first point, points), collision outline (first polygon, polygons), name */')
out.append('static const rd_bike rd_bikes[NBIKES] = {')
for b in bike_rows: out.append('  {%s},' % ', '.join(str(v) for v in b))
out.append('};')
arr('int8_t', 'bike_xy', outline); arr('uint16_t', 'hull_poly', hull_poly); arr('int8_t', 'hull_xy', hull)

# ---------------------------------------------------------------- texts
ti = props('code.textIndicators.properties')
for n in range(2, 7):
    f = ti['fliptasticValues'][str(n)]
    for kind in ('perfect', 'sloppy'):
        v = [f[kind][k] for k in sorted(f[kind], key=int)]
        out.append('static const char *const flip%d_%s[] = {%s};' % (n, kind, ', '.join(json.dumps(s) for s in v)))
out.append('static const char *const *const flip_texts[5][2] = {%s};' % ', '.join('{flip%d_perfect, flip%d_sloppy}' % (n, n) for n in range(2, 7)))
arr('uint8_t', 'flip_ntexts', [len(ti['fliptasticValues'][str(n)][k]) for n in range(2, 7) for k in ('perfect', 'sloppy')])
air = ti['bigAirValues']
out.append('static const char *const air_texts[4] = {%s};' % ', '.join(json.dumps(air[k]['1']) for k in sorted(air, key=int)))
sm = ti['smoothLandingTexts']
out.append('static const char *const smooth_texts[%d] = {%s};' % (len(sm), ', '.join(json.dumps(sm[k]) for k in sorted(sm, key=int))))
ff = ti['frontFlipTexts']
out.append('static const char *const front_texts[%d] = {%s};' % (len(ff), ', '.join(json.dumps(ff[k]) for k in sorted(ff, key=int))))

body = '\n'.join(out) + '\n'
open(OUT, 'w').write(body)
print('%s: %d templates, %d polygons, %d objects, %d animations, %d steps, %d tracks, %d bikes, %d bytes' %
      (OUT, len(tpl_names), len(poly_vert) - 1, len(objs), len(anims), len(steps), len(tracks), len(chars), len(body)))
