"""Mr Bullet: src/data.bin and src/data.h from the game's own files.

python3 -I tools/pack.py REF   (REF: the APK's assets folder: Levels/*.jg, Images/...)

Levels: the Classic, Grenades, Hostages and Friendly Fire worlds (Levels/world0, 1, 3, 4), each level its own raw
deflate stream of the format game.c reads (see level_bytes). Pictures: the city background cut to the play field,
the characters' torsos and heads, the interface's pictures; 16 colours each (the background 64)."""
import json, os, struct, sys, zlib
from PIL import Image

REF = sys.argv[1]
HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', 'src')

# the play field: 440 x 568 of the game's units on 240 rows
SCALE = 240 / 568.0
SHEET = 7.5                       # the body sheets' pixels for one unit (a torso: 216 pixels, 0.36 x 80 units)
MODES = [('world0', 20), ('world1', 7), ('world3', 7), ('world4', 9)]

FLAGS = ['ki', 'gki', 'hki', 'hgki', 'vi', 'hs', 'rh', 'si', 'b', 'f', 'co', 'be', 'hv', 'bo', 'br', 'by', 'sb',
         'ig', 'lg', 'rg', 'c', 'fa', 'm', 'mcw']


def q(v):
    r = int(round(v * 8))
    assert -32768 <= r < 32768, v
    return r


def pts(out, flat, cx=0, cy=0):
    for i in range(0, len(flat), 2):
        out += struct.pack('<hh', q(flat[i]), q(flat[i + 1]))


def area(pts):
    a = 0
    for i in range(len(pts)):
        x0, y0 = pts[i - 1]; x1, y1 = pts[i]
        a += x0 * y1 - x1 * y0
    return a / 2


def hull(pts):
    pts = sorted(set(pts))
    if len(pts) < 3: return pts
    def cross(o, a, b): return (a[0] - o[0]) * (b[1] - o[1]) - (a[1] - o[1]) * (b[0] - o[0])
    lo, hi = [], []
    for p in pts:
        while len(lo) >= 2 and cross(lo[-2], lo[-1], p) <= 0: lo.pop()
        lo.append(p)
    for p in reversed(pts):
        while len(hi) >= 2 and cross(hi[-2], hi[-1], p) <= 0: hi.pop()
        hi.append(p)
    return lo[:-1] + hi[:-1]


def simplify(pts, n):
    # Visvalingam: drop the point making the smallest triangle until n are left
    pts = list(pts)
    while len(pts) > n:
        best, bi = None, 0
        for i in range(len(pts)):
            a, b, c = pts[i - 1], pts[i], pts[(i + 1) % len(pts)]
            t = abs((b[0] - a[0]) * (c[1] - a[1]) - (c[0] - a[0]) * (b[1] - a[1]))
            if best is None or t < best: best, bi = t, i
        pts.pop(bi)
    return pts


def convex_parts(p):
    """A dynamic shape that is convex (most: rounded boxes) as one polygon of at most 16 points, rather than the
    editor's fan of overlapping parts (their boxes overlap their neighbours' in a pile: hundreds of contacts)."""
    tp = p.get('tp')
    if not tp or tp.get('h'): return p['p']
    o = [(tp['p'][i], tp['p'][i + 1]) for i in range(0, len(tp['p']), 2)]
    h = hull(o)
    if len(o) < 3 or abs(area(h)) - abs(area(o)) > 0.002 * abs(area(h)): return p['p']
    s = simplify(h, 8)
    return [[v for xy in s for v in xy]]


def split8(part):
    """a convex part of more than 8 points as fans of at most 8 (Box2D's polygons hold 8)"""
    pts = [(part[i], part[i + 1]) for i in range(0, len(part), 2)]
    if len(pts) <= 8: return [part]
    out, k = [], 1
    while k < len(pts) - 1:
        piece = [pts[0]] + pts[k:k + 7]
        out.append([v for xy in piece for v in xy])
        k += 6
    return out


def level_bytes(c):
    j = c['j']
    polys = j['poly']
    pins = j.get('pins', [])
    sws = j.get('switches', [])
    out = bytearray(struct.pack('<BBBB', c['tr'], len(polys), len(pins), len(sws)))
    for p in polys:
        bits = 0
        for k, name in enumerate(FLAGS):
            if p.get(name): bits |= 1 << k
        parts = p['p']
        is_char = any(p.get(k) for k in ('ki', 'gki', 'hki', 'hgki', 'vi', 'hs'))
        if is_char and len(parts) == 1 and len(parts[0]) > 4:
            parts = [parts[0][:4]]   # (a few have the editor's stray points after feet and head)
        if not is_char and not p.get('f') and not p.get('b') and len(parts) > 1:
            parts = convex_parts(p)
        if not is_char and not p.get('b') and (len(parts) > 1 or len(parts[0]) > 4):
            if p.get('f'):
                parts = []   # static: its outline is its shape (a loop of edges, game.c)
            else:
                parts = [q for part in parts for q in split8(part)]
        if not parts: kind = 0
        elif len(parts) == 1 and len(parts[0]) == 4: kind = 1
        elif len(parts) == 1 and len(parts[0]) == 2: kind = 2
        else: kind = 0
        cx, cy = p['ce']
        out += struct.pack('<BBI', kind, p['i'], bits)
        out += struct.pack('<hh', q(cx), q(cy))
        if kind == 1:
            pts(out, parts[0])
        elif kind == 2:
            pts(out, parts[0])
        else:
            out += struct.pack('<B', len(parts))
            for part in parts:
                assert 3 <= len(part) // 2 <= 8, len(part)
                out += struct.pack('<B', len(part) // 2)
                pts(out, part)
            tp = p.get('tp')
            outline = tp['p'] if tp else (parts[0] if len(parts) == 1 else p['v'][2:])
            holes = tp.get('h', []) if tp else []
            out += struct.pack('<H', len(outline) // 2)
            pts(out, outline)
            out += struct.pack('<B', len(holes))
            for h in holes:
                out += struct.pack('<B', len(h) // 2)
                pts(out, h)
    for p in pins:
        cx, cy = p['ce']
        v = p['v'][2:]
        out += struct.pack('<BbbB', p['t'], p.get('pia', -1), p.get('pib', -1), 1 if p.get('m') else 0)
        out += struct.pack('<hh', q(cx), q(cy))
        out += struct.pack('<B', len(v) // 2)
        pts(out, v)
        pts(out, p['la'] + p['lb'])
        out += struct.pack('<hhff', q(p.get('rl', 0)), q(p.get('as', 0)), p.get('ms', 0), p.get('mt', 0))
    for s in sws:
        cx, cy = s['ce']
        v = s['v'][2:]
        links = s.get('swi', [])
        out += struct.pack('<BBB', s['id'] & 255, s.get('swci', 0) & 255, len(links))
        out += bytes(links)
        out += struct.pack('<hh', q(cx), q(cy))
        pts(out, v[:4])
    return bytes(out)


def deflate(b):
    z = zlib.compressobj(9, zlib.DEFLATED, -15, 9)
    return z.compress(b) + z.flush()


# ---- pictures: 16 colours (index 0 clear) and 4 bits a pixel, or 64 colours a byte a pixel
def rgb565(r, g, b): return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def pic_bytes(im, colours=15, pivot=(0, 0), opaque=False):
    im = im.convert('RGBA')
    w, h = im.size
    alpha = im.getchannel('A')
    rgb = Image.new('RGB', im.size, (0, 0, 0))
    rgb.paste(im, mask=alpha)
    pq = rgb.quantize(colors=colours, method=Image.Quantize.MEDIANCUT, dither=Image.Dither.NONE)
    pal = pq.getpalette()[:colours * 3]
    idx = list(pq.tobytes()); a = list(alpha.tobytes())
    wide = colours > 15
    px = [0 if (a[i] < 128 and not opaque) else idx[i] + (0 if wide else 1) for i in range(w * h)]
    ncol = colours if wide else colours + 1
    palette = [0] * ncol
    for k in range(colours):
        palette[k + (0 if wide else 1)] = rgb565(*pal[3 * k:3 * k + 3])
    head = struct.pack('<HHhhBB', w, h, int(round(pivot[0] * 4)), int(round(pivot[1] * 4)), ncol,
                       (1 if opaque else 0) | (2 if wide else 0))
    body = struct.pack('<%dH' % ncol, *palette)
    if wide:
        data = bytes(px)
    else:
        data = bytearray((w * h + 1) // 2)
        for i, k in enumerate(px):
            data[i >> 1] |= k << ((i & 1) * 4)
    return head + body + bytes(data)


def img(path): return Image.open(os.path.join(REF, 'Images', path)).convert('RGBA')


def scaled(im, f):
    w, h = im.size
    return im.resize((max(1, round(w * f)), max(1, round(h * f))), Image.LANCZOS)


pics = []          # (name, bytes)
def add(name, b):
    pics.append((name, b)); return len(pics) - 1


# the background: the phone shows it 834 units tall, its bottom 97 units under the field (field +-284 units)
bg = img('Backgrounds/background_city.png')
k = 834 / 900.0                               # units a background pixel
top = (-284 + 834 - 97 - 284) / k             # field top in background rows: the picture's top is at -453
x0 = 250 - 220 / k; x1 = 250 + 220 / k
crop = bg.crop((round(x0), round(top), round(x1), round(top + 568 / k)))
add('BG', pic_bytes(crop.resize((186, 240), Image.LANCZOS), colours=64, opaque=True))

# character pieces: torso and heads as pictures (the limbs are drawn as the sheet's colours)
F = SCALE / SHEET
SKINS = [('HERO', 'blue_suit_body.png', 'Killer/killer_head.png', 'Killer/killer_head.png'),
         ('ENEMY', 'black_suit_body.png', 'gangster_head.png', 'gangster_head_dead.png'),
         ('HOSTAGE', 'hostage_body.png', 'hostage_head.png', 'hostage_head_dead.png')]
colours = {}
for name, body, head, dead in SKINS:
    sheet = img('Characters/' + body)
    torso = sheet.crop((204, 50, 420, 286))
    t = scaled(torso, F)
    add(name + '_TORSO', pic_bytes(t, pivot=(t.size[0] / 2, t.size[1] / 2)))
    for tag, hp in (('HEAD', head), ('DEAD', dead)):
        hi = img('Characters/' + hp).crop((34, 44, 212, 208))
        s = scaled(hi, F)
        add(name + '_' + tag, pic_bytes(s, pivot=(s.size[0] / 2, s.size[1])))   # pivot: the neck

    def avg(box):
        reg = sheet.crop(box).convert('RGBA')
        raw = reg.tobytes()
        px = [tuple(raw[i:i + 4]) for i in range(0, len(raw), 4) if raw[i + 3] > 200]
        px.sort(key=lambda p: p[0] + p[1] + p[2])
        p = px[len(px) // 2]
        return rgb565(*p[:3])
    colours[name] = [avg((50, 60, 104, 180)), avg((520, 60, 574, 180)), avg((60, 360, 100, 385)),
                     avg((204, 380, 258, 500)), avg((366, 380, 420, 500)), avg((210, 670, 280, 690))]
    # front sleeve, back sleeve, hand, front leg, back leg, shoe

# the interface
add('TITLE', pic_bytes(scaled(img('Misc/title.png'), 230 / 800.0)))
bullet = img('Misc/bullet.png')
add('BULLET', pic_bytes(scaled(bullet, 14 / 310.0), colours=7))
add('GRENADE', pic_bytes(scaled(img('Misc/grenade.png'), 12 / 140.0), colours=7))
star = img('Misc/star-md.png')
add('STAR', pic_bytes(scaled(star, 20 / 162.0), colours=7))
add('STAR_BIG', pic_bytes(scaled(star, 34 / 162.0), colours=11))
add('PADLOCK', pic_bytes(scaled(img('Misc/padlock.png'), 14 / img('Misc/padlock.png').size[1]), colours=7))

# the levels
levels = []
first = []
for world, chapters in MODES:
    first.append(len(levels))
    for ch in range(chapters):
        d = json.loads(zlib.decompress(open(os.path.join(REF, 'Levels', '%s-%d.jg' % (world, ch)), 'rb').read()))
        for c in d['cs']:
            levels.append(level_bytes(c))
first.append(len(levels))

blob = bytearray()
lvl_index = []
biggest = 0
for b in levels:
    z = deflate(b)
    lvl_index.append((len(blob), len(z), len(b)))
    biggest = max(biggest, len(b))
    blob += z
    while len(blob) % 4: blob += b'\0'
pic_index = []
pblob = bytearray()
for name, b in pics:
    pic_index.append(len(pblob))
    pblob += b
    while len(pblob) % 4: pblob += b'\0'

data = bytearray()
data += struct.pack('<IIII', len(levels), len(pics), 0, 0)
lo = 16
po = lo + 8 * len(levels)
bo = po + 4 * len(pics)
for off, n, u in lvl_index:
    data += struct.pack('<IHH', bo + off, n, u)
pb = bo + len(blob)
for off in pic_index:
    data += struct.pack('<I', pb + off)
data += blob + pblob
open(os.path.join(SRC, 'data.bin'), 'wb').write(data)

with open(os.path.join(SRC, 'data.h'), 'w') as h:
    h.write('/* Mr Bullet: what tools/pack.py put in data.bin (generated) */\n#ifndef MB_DATA_H\n#define MB_DATA_H\n')
    h.write('#define LEVEL_COUNT %d\n#define LEVEL_MAX_BYTES %d\n' % (len(levels), (biggest + 7) & ~7))
    h.write('#define MODE_COUNT %d\n' % len(MODES))
    h.write('#define MODE_FIRST {%s}\n' % ', '.join(str(x) for x in first))
    for k, (name, b) in enumerate(pics):
        h.write('#define PIC_%s %d\n' % (name, k))
    h.write('#define PIC_COUNT %d\n' % len(pics))
    for name, c in colours.items():
        h.write('#define COLOURS_%s {%s}\n' % (name, ', '.join('0x%04X' % x for x in c)))
    h.write('#endif\n')
print('levels', len(levels), 'packed', len(blob), 'biggest', biggest, 'pictures', len(pblob), 'data.bin', len(data))
