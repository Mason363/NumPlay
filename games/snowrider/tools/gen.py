"""Snow Rider 3D's world for the calculator, read from the game's Unity project: src/data.c and src/data.h.

  python3 tools/gen.py <the Unity project's Assets folder> src

What is taken as it is: the generator's settings, its biomes, structures and chunks (and the modifiers that chain
structures: tunnels, cracks, floating islands, snowballs), every prefab they place with its meshes and colliders
(boxes, spheres, capsules and collision meshes, on their layers: 8 surface, 9 obstacle, 10 breakable, 13 gift),
the random turns of RandSettings, the sleds of Skins with their crash pieces, the camp of the menu, the sled's
numbers (BasicSleds) and the scene's camera, light, sky and collision points.
What changes: meshes have fewer triangles (tools/decimate.py, edge collapses that keep the colour seams), a far
version each, and a colour per triangle read from the palette picture (mega_texture) at its middle."""
import os, sys, math
import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(__file__))
import unity
from decimate import decimate as _decimate
import pickle, hashlib

CACHE = os.path.join(os.path.dirname(__file__), '..', 'build', 'decimate.cache')
_cache = pickle.load(open(CACHE, 'rb')) if os.path.exists(CACHE) else {}


def decimate(pos, uv, bone, faces, target):
    """tools/decimate.py, its answers kept in build/ (it takes minutes)"""
    key = hashlib.sha1(repr((np.round(np.asarray(pos, float), 6).tolist(), [tuple(map(int, f)) for f in faces], target)).encode()).hexdigest()
    if key not in _cache:
        _cache[key] = [tuple(map(int, f)) for f in _decimate(pos, uv, bone, faces, target)]
        os.makedirs(os.path.dirname(CACHE), exist_ok=True)
        pickle.dump(_cache, open(CACHE, 'wb'))
    return _cache[key]

ROOT, OUT = sys.argv[1], sys.argv[2]
P = unity.Project(ROOT)
GUID = {os.path.relpath(v, ROOT): k for k, v in P.guids.items()}


def ref_of(rel, name=None):
    g = GUID[rel]
    docs = P.docs(P.path(g))
    if name is None:
        return {'guid': g, 'fileID': next(iter(docs))}
    return {'guid': g, 'fileID': [k for k, (c, b, s) in docs.items() if c == 1 and b.get('m_Name') == name][0]}


def asset(rel):
    return P.asset({'guid': GUID[rel], 'fileID': '11400000'})


def isref(r):
    return isinstance(r, dict) and r.get('guid') and P.path(r['guid'])


# ------------------------------------------------------------------ colours
textures = {}


def material(ref):
    """(the picture as an RGBA array, the colour it is multiplied by)"""
    if not isref(ref):
        return None, (1, 1, 1)
    m = P.asset(ref)
    props = m['m_SavedProperties']
    tex = None
    envs = props['m_TexEnvs']
    envs = envs if isinstance(envs, dict) else {k: v for e in envs for k, v in e.items()}
    t = envs.get('_MainTex') or envs.get('_BaseMap')
    if t and isref(t['m_Texture']):
        path = P.path(t['m_Texture']['guid'])
        if path not in textures:
            textures[path] = np.asarray(Image.open(path).convert('RGBA'), float) / 255
        tex = textures[path]
    cols = props['m_Colors']
    cols = cols if isinstance(cols, dict) else {k: v for e in cols for k, v in e.items()}
    c = cols.get('_Color', {'r': 1, 'g': 1, 'b': 1})
    return tex, (float(c['r']), float(c['g']), float(c['b']))


palette = []
pal_index = {}


def colour(rgb):
    key = tuple(int(round(max(0, min(1, x)) * 255)) for x in rgb)
    if key not in pal_index:
        pal_index[key] = len(palette)
        palette.append(key)
    return pal_index[key]


def sample(tex, uv):
    if tex is None:
        return (1, 1, 1)
    h, w = tex.shape[:2]
    x = int(math.floor((uv[0] % 1) * w)) % w
    y = int(math.floor((1 - uv[1] % 1) * h)) % h   # (rows go down, v goes up)
    return tex[y, x, :3]


# ------------------------------------------------------------------ meshes
def budget(name, n):
    """triangles kept near and far"""
    for key, near, far in (('sled', 150, 150), ('santa', 150, 150), ('PLASTIC', 150, 150), ('Old_sleds', 150, 150),
                           ('tree_snow', 90, 30), ('tree_noLeaves', 80, 30), ('rockP', 70, 26), ('snowman', 60, 24),
                           ('camp_main', 1000, 1000), ('Cylinder', 160, 160), ('campBillboard', 70, 70),
                           ('floating', 110, 50), ('snowball', 40, 20), ('gift', 40, 16), ('fence', 60, 24),
                           ('skull', 60, 24), ('stump', 40, 16), ('root', 40, 16)):
        if key in name:
            return min(n, near), min(n, far)
    if name.startswith('Ground'):
        return min(n, max(40, int(n * 0.36))), min(n, max(24, int(n * 0.16)))
    return min(n, max(24, int(n * 0.3))), min(n, max(12, int(n * 0.15)))


rmeshes, rmesh_key = [], {}
cmeshes, cmesh_key = [], {}
stats = {'render': 0, 'coll': 0}


def mesh_name(ref):
    return os.path.basename(P.path(ref['guid']))[:-6]


ROLE = [None]   # 'sled' while reading the sleds, 'piece' their pieces and those of what breaks


def render_mesh(ref, mat):
    """(near, far) indices of a mesh drawn with a material"""
    key = (ref['guid'], ref['fileID'], mat['guid'] if isref(mat) else None)
    if key in rmesh_key:
        return rmesh_key[key]
    pos, nrm, uv, tris = unity.mesh(P, ref)
    tex, tint = material(mat)
    name = mesh_name(ref)
    near, far = {'sled': (350, 350), 'piece': (60, 60)}.get(ROLE[0]) or budget(name, len(tris))
    near, far = min(near, len(tris)), min(far, len(tris))
    # the triangles as they are: their colour, middle and normal
    P3 = np.array(pos, float)
    T = np.array(tris, int)
    a, b, c = P3[T[:, 0]], P3[T[:, 1]], P3[T[:, 2]]
    cn = np.cross(b - a, c - a)
    ln = np.linalg.norm(cn, axis=1)
    ok = ln > 1e-12
    T, a, b, c, cn, ln = T[ok], a[ok], b[ok], c[ok], cn[ok], ln[ok]
    onrm = cn / ln[:, None]
    octr = (a + b + c) / 3
    ocol = [colour(np.array(sample(tex, np.mean([uv[i] for i in t], axis=0) if uv else (0, 0))) * np.array(tint)) for t in T]
    # one point for corners at one place (the colour seams split them all)
    keymap, wpos, wid = {}, [], []
    for p in P3:
        k = tuple(np.round(p, 5))
        if k not in keymap:
            keymap[k] = len(wpos)
            wpos.append(p)
        wid.append(keymap[k])
    wid = np.array(wid)
    wfaces = [tuple(f) for f in wid[T] if len(set(f)) == 3]
    out = []
    for target in (near, far) if far < near else (near,):
        faces = wfaces if target >= len(wfaces) else [tuple(f) for f in decimate(np.array(wpos), None, None, wfaces, target)]
        out.append(pack_render(name, wpos, faces, octr, onrm, ocol))
    idx = []
    for m in out:
        rmeshes.append(m)
        idx.append(len(rmeshes) - 1)
    r = (idx[0], idx[-1])
    rmesh_key[key] = r
    return r


def pack_render(name, wpos, faces, octr, onrm, ocol):
    """a mesh of the triangles left, each coloured like the first one's nearest to it facing its way"""
    used = sorted({i for f in faces for i in f})
    remap = {c: k for k, c in enumerate(used)}
    pts = [wpos[c] for c in used]
    tris, cols, nrms = [], [], []
    for f in faces:
        a, b, c = (np.asarray(wpos[i]) for i in f)
        n = np.cross(b - a, c - a)
        ln = np.linalg.norm(n)
        if ln < 1e-12:
            continue
        n /= ln
        ctr = (a + b + c) / 3
        d = ((octr - ctr) ** 2).sum(1) + 0.02 * (1 - onrm @ n)
        tris.append([remap[i] for i in f])
        cols.append(ocol[int(np.argmin(d))])
        nrms.append([int(round(x * 127)) for x in n])
    stats['render'] += len(tris)
    return {'name': name, 'v': pts, 't': tris, 'c': cols, 'n': nrms}


def coll_mesh(ref):
    key = (ref['guid'], ref['fileID'])
    if key in cmesh_key:
        return cmesh_key[key]
    pos, nrm, uv, tris = unity.mesh(P, ref)
    name = mesh_name(ref)
    target = len(tris) if len(tris) <= 260 else max(260, int(len(tris) * 0.4))
    faces = tris if target >= len(tris) else [tuple(f) for f in decimate(pos, None, None, tris, target)]
    pts, remap, out = [], {}, []
    for f in faces:
        t = []
        for i in f:
            k = tuple(round(x, 4) for x in pos[i])
            if k not in remap:
                remap[k] = len(pts)
                pts.append(pos[i])
            t.append(remap[k])
        if len(set(t)) == 3:
            out.append(t)
    stats['coll'] += len(out)
    cmeshes.append({'name': name, 'v': pts, 't': out})
    cmesh_key[key] = len(cmeshes) - 1
    return cmesh_key[key]


# ------------------------------------------------------------------ prefabs
LAYER = {8: 1, 9: 2, 10: 4, 13: 8}   # surface, obstacle, breakable, gift
prefabs, prefab_key = [], {}
chunks, chunk_key = [], {}


def mat_scale(m, s):
    return [[m[i][0] * s[0], m[i][1] * s[1], m[i][2] * s[2], m[i][3]] for i in range(3)]


def mat_move(m, c):
    p = unity.apply(m, c)
    return [[m[i][0], m[i][1], m[i][2], p[i]] for i in range(3)]


def prefab(ref):
    key = (ref['guid'], ref['fileID'])
    if key in prefab_key:
        return prefab_key[key]
    root = unity.prefab(P, ref)
    pf = {'name': root.name, 'parts': [], 'colls': [], 'flags': 0, 'pieces': -1, 'spawner': None}
    prefab_key[key] = len(prefabs)
    prefabs.append(pf)

    def rec(n):
        if not n.active:
            return
        for mf, mr in zip(n.comps.get('MeshFilter', []), n.comps.get('MeshRenderer', [])):
            if mr.get('m_Enabled', '1') != '1' or not isref(mf['m_Mesh']):
                continue
            near, far = render_mesh(mf['m_Mesh'], mr['m_Materials'][0] if mr['m_Materials'] else None)
            pf['parts'].append((near, far, n.m))
        lay = LAYER.get(n.layer, 0)
        for kind, bodies in n.comps.items():
            for b in bodies:
                if not lay or b.get('m_Enabled', '1') != '1' or kind not in ('BoxCollider', 'SphereCollider', 'CapsuleCollider', 'MeshCollider'):
                    continue
                trig = b.get('m_IsTrigger', '0') == '1'
                if kind == 'BoxCollider':
                    m = mat_scale(mat_move(n.m, unity.v3(b['m_Center'])), [x / 2 for x in unity.v3(b['m_Size'])])
                    pf['colls'].append((0, lay, trig, 0, m, 0, 0))
                elif kind == 'SphereCollider':
                    r = float(b['m_Radius'])
                    m = mat_scale(mat_move(n.m, unity.v3(b['m_Center'])), (r, r, r))
                    pf['colls'].append((1, lay, trig, 0, m, 0, 0))
                elif kind == 'CapsuleCollider':
                    m = mat_move(n.m, unity.v3(b['m_Center']))
                    pf['colls'].append((2, lay, trig, int(b.get('m_Direction', 1)), m, float(b['m_Radius']), float(b['m_Height'])))
                elif isref(b['m_Mesh']):
                    pf['colls'].append((3, lay, trig, coll_mesh(b['m_Mesh']), n.m, 0, 0))
        if n.tag == 'Spawner':
            pf['spawner'] = [n.m[i][3] for i in range(3)]
        for c in n.children:
            rec(c)

    rec(root)
    if root.tag == 'Gift':
        pf['flags'] |= 1
    for d in root.comps.get('DestructibleObj', []):
        pf['flags'] |= 2
        ROLE[0], keep = 'piece', ROLE[0]
        pf['pieces'] = chunk(d['prefab'])
        ROLE[0] = keep
    if root.comps.get('Rigidbody'):
        pf['flags'] |= 4
    return pf_index(key)


def pf_index(key):
    return prefab_key[key]


def chunk(ref):
    key = ref['guid']
    if key in chunk_key:
        return chunk_key[key]
    a = P.asset(ref)
    ch = {'name': a['m_Name'], 'obj': []}
    chunk_key[key] = len(chunks)
    chunks.append(ch)
    for o in a['obj']:
        if not isref(o['prefab']):
            continue
        ch['obj'].append((prefab(o['prefab']), unity.v3(o['pos']), unity.q4(o['rot']), unity.v3(o['scale'])))
    return chunk_key[key]


# ------------------------------------------------------------------ the generator
gen = asset('MonoBehaviour/GenSettings.asset')
structs, struct_key = [], {}
MODS = {'Tunnel': 1, 'CrackStart': 2, 'Crack': 3, 'Floats': 3, 'FloatsEndMod': 3, 'SnowBalls': 4}


def struct(ref):
    key = ref['guid']
    if key in struct_key:
        return struct_key[key]
    s = P.asset(ref)
    st = {'name': s['name'] or s['m_Name'], 'min': int(s['minScoreToSpawn']), 'max': min(int(s['maxScoreToSpawn']), 2 ** 31 - 1),
          'p': float(s['probability']), 'lmin': int(s['minLenght']), 'lmax': int(s['maxLength']), 'mod': 0, 'next': -1,
          'a': -1, 'b': -1, 'list': []}
    struct_key[key] = len(structs)
    structs.append(st)
    st['mid'] = [chunk(c['data']) for c in s['midChunks']]
    st['side'] = [chunk(c['data']) for c in s['sideChunks']]
    if isref(s['mod']):
        docs = P.docs(P.path(s['mod']['guid']))
        mb = docs.get(s['mod']['fileID'])
        if not mb or mb[0] != 114:
            mb = next(d for d in docs.values() if d[0] == 114)
        body = mb[1]
        script = os.path.basename(P.path(body['m_Script']['guid']))[:-3]
        st['mod'] = MODS[script]
        nxt = body.get('nextStruct') or body.get('nextStructure')
        if isref(nxt):
            st['next'] = struct(nxt)
        if script == 'Tunnel':
            st['list'] = [chunk(c) for c in body['tunnelPrefab']]
        elif script == 'CrackStart':
            st['a'], st['b'] = chunk(body['rampPrefab']), chunk(body['sideRampprefab'])
        elif script == 'SnowBalls':
            st['a'], st['b'] = chunk(body['startChunk']), chunk(body['endChunk'])
            sp = P.docs(P.path(body['spawner']['guid']))
            spb = next(d[1] for d in sp.values() if d[0] == 114)
            ball = unity.prefab(P, spb['snowballBigPrefab'])
            sb = ball.comps['SnowBall'][0]
            st['ball'] = (prefab(spb['snowballBigPrefab']), float(spb['interval']), float(spb['speed']), float(spb['randomness']),
                          float(sb['minSize']), float(sb['maxSize']))
    return struct_key[key]


biomes = []
for b in [gen['WarmupBiome']] + gen['biomes']:
    B = P.asset(b)
    biomes.append((B['name'], int(B['minLength']), int(B['maxLength']), [struct(s) for s in B['structs']]))

rand = []
for o in asset('MonoBehaviour/RandSettings.asset')['objects']:
    if not isref(o['obj']):
        continue
    rand.append((prefab(o['obj']), unity.v3(o['minRotOffset']), unity.v3(o['maxRotOffset']), [prefab(x) for x in o.get('prefab') or [] if isref(x)]))

# ------------------------------------------------------------------ sleds, camp, scene
import re
_lang = open(os.path.join(ROOT, 'Resources', 'Language.en.bytes'), encoding='utf-8', errors='ignore').read()
TITLES = dict(re.findall(r'<data name="([^"]+)"[^>]*>\s*<value>([^<]*)</value>', _lang))
skins = []
for s in asset('MonoBehaviour/Skins.asset')['skins']:
    root = unity.prefab(P, s['model'])
    cam, box = (0, 0, 0), None
    for n in unity.walk(root):
        if n.tag == 'SledCamPoint':
            cam = [n.m[i][3] for i in range(3)]
        for b in n.comps.get('BoxCollider', []):
            box = (unity.apply(n.m, unity.v3(b['m_Center'])), [abs(x) for x in unity.applyv(n.m, unity.v3(b['m_Size']))])
    ROLE[0] = 'sled'
    model = prefab(s['model'])
    ROLE[0] = 'piece'
    skins.append((TITLES.get(s['title'], s['title'].replace('Title', '')), int(s['price']), model, chunk(s['physicsModel']), cam, box))
    ROLE[0] = None

camp_chunk = chunk(ref_of('MonoBehaviour/CampPivot.asset'))
camp_docs = P.docs(P.path(GUID['GameObject/CampPivot.prefab']))
camp_obj = P.asset(ref_of('MonoBehaviour/CampPivot.asset'))['obj']
points = {}
for o in camp_obj:
    nm = camp_docs[o['prefab']['fileID']][1]['m_Name']
    points[nm] = (unity.v3(o['pos']), unity.q4(o['rot']))

scene = P.docs(P.path(GUID['Scenes/GameWorld.unity']))
def scene_obj(name):
    for gid, (c, b, s) in scene.items():
        if c == 1 and b.get('m_Name') == name:
            return b, {scene[x['component']['fileID']][0]: scene[x['component']['fileID']][1] for x in b['m_Component'] if x['component']['fileID'] in scene}
light = scene_obj('Directional Light')[1]
lq = unity.q4(light[4]['m_LocalRotation'])
ldir = [-x for x in unity.applyv(unity.trs((0, 0, 0), lq, (1, 1, 1)), (0, 0, 1))]
lint = float(light[108]['m_Intensity'])
cam_mb = [b for gid, (c, b, s) in scene.items() if c == 114 and 'rotationAmplify' in b][0]
camera = [b for gid, (c, b, s) in scene.items() if c == 20][0]
point = [b for gid, (c, b, s) in scene.items() if c == 114 and 'gravityAcc' in b][0]
rays = []
for gid, (c, b, s) in scene.items():
    if c == 114 and 'rayLenght' in b:
        t = scene[b['m_GameObject']['fileID']]
        tr = [scene[x['component']['fileID']][1] for x in t[1]['m_Component'] if scene.get(x['component']['fileID'], (0,))[0] == 4][0]
        rays.append((unity.v3(tr['m_LocalPosition']), float(b['rayLenght'])))
cp = [scene[x['component']['fileID']][1] for x in scene_obj('Collision Point')[0]['m_Component'] if scene[x['component']['fileID']][0] == 4][0]
sled = asset('MonoBehaviour/BasicSleds.asset')
floats = P.asset({'guid': GUID['Material/World.mat'], 'fileID': '2100000'})['m_SavedProperties']['m_Floats']
curv = float((floats if isinstance(floats, dict) else {k: v for e in floats for k, v in e.items()}).get('_Curvature', 0.004))
rs = [b for gid, (c, b, s) in scene.items() if c == 104][0]
amb = rs['m_AmbientSkyColor']
sky = P.asset({'guid': GUID['Material/SkyBox.mat'], 'fileID': '2100000'})['m_SavedProperties']['m_Colors']
sky = sky if isinstance(sky, dict) else {k: v for e in sky for k, v in e.items()}


# ------------------------------------------------------------------ writing
def F(x):
    s = '%.6g' % x
    return s + ('f' if ('.' in s or 'e' in s) else '.0f') if s not in ('0', '-0') else '0'


def mat12(m):
    return '{' + ','.join(F(m[i][j]) for i in range(3) for j in range(4)) + '}'


def q16(pts):
    m = max(1e-6, max(abs(c) for p in pts for c in p))
    s = m / 32767
    return s, [[int(round(c / s)) for c in p] for p in pts]


c = ['/* Made by tools/gen.py from Snow Rider 3D\'s Unity project: do not edit. */', '#include "data.h"', '']
for i, m in enumerate(rmeshes):
    s, q = q16(m['v'])
    v = np.array(m['v'])
    ctr = (v.min(0) + v.max(0)) / 2
    rad = float(np.max(np.linalg.norm(v - ctr, axis=1))) if len(v) else 0
    m['s'], m['ctr'], m['rad'] = s, ctr, rad
    c.append('static const int16_t rv%d[] = {%s};' % (i, ','.join(str(x) for p in q for x in p)))
    c.append('static const uint16_t rt%d[] = {%s};' % (i, ','.join(str(x) for t in m['t'] for x in t)))
    c.append('static const uint8_t rc%d[] = {%s};' % (i, ','.join(str(x) for x in m['c'])))
    c.append('static const int8_t rn%d[] = {%s};' % (i, ','.join(str(x) for n in m['n'] for x in n)))
c.append('const RMesh rmeshes[] = {')
for i, m in enumerate(rmeshes):
    c.append('  {rv%d, rt%d, rc%d, rn%d, %d, %d, %s, {%s}, %s}, /* %s */' % (i, i, i, i, len(m['v']), len(m['t']), F(m['s']),
             ','.join(F(x) for x in m['ctr']), F(m['rad']), m['name']))
c.append('};')
for i, m in enumerate(cmeshes):
    s, q = q16(m['v'])
    v = np.array(m['v'])
    m['s'], m['lo'], m['hi'] = s, v.min(0), v.max(0)
    c.append('static const int16_t cv%d[] = {%s};' % (i, ','.join(str(x) for p in q for x in p)))
    c.append('static const uint16_t ct%d[] = {%s};' % (i, ','.join(str(x) for t in m['t'] for x in t)))
c.append('const CMesh cmeshes[] = {')
for i, m in enumerate(cmeshes):
    c.append('  {cv%d, ct%d, %d, %d, %s, {%s}, {%s}}, /* %s */' % (i, i, len(m['v']), len(m['t']), F(m['s']),
             ','.join(F(x) for x in m['lo']), ','.join(F(x) for x in m['hi']), m['name']))
c.append('};')
for i, p in enumerate(prefabs):
    if p['parts']:
        c.append('static const Part pp%d[] = {%s};' % (i, ','.join('{%d,%d,%s}' % (a, b, mat12(m)) for a, b, m in p['parts'])))
    if p['colls']:
        c.append('static const Coll pc%d[] = {%s};' % (i, ','.join('{%d,%d,%d,%d,%s,%s,%s}' % (k, l, int(t), d, mat12(m), F(r), F(h)) for k, l, t, d, m, r, h in p['colls'])))
for r_i, (pf, lo, hi, alts) in enumerate(rand):
    prefabs[pf]['rand'] = r_i
c.append('const Prefab prefabs[] = {')
for i, p in enumerate(prefabs):
    sp = p['spawner'] or (0, 0, 0)
    c.append('  {%s, %s, %d, %d, %d, %d, %d, {%s}}, /* %s */' % ('pp%d' % i if p['parts'] else '0', 'pc%d' % i if p['colls'] else '0',
             len(p['parts']), len(p['colls']), p['flags'] | (16 if p['spawner'] else 0), p['pieces'], p.get('rand', -1), ','.join(F(x) for x in sp), p['name']))
c.append('};')
for i, ch in enumerate(chunks):
    c.append('static const Obj co%d[] = {%s};' % (i, ','.join('{%d,{%s},{%s},{%s}}' % (pf, ','.join(F(x) for x in p), ','.join(F(x) for x in q), ','.join(F(x) for x in s)) for pf, p, q, s in ch['obj'])))
c.append('const Chunk chunks[] = {')
for i, ch in enumerate(chunks):
    c.append('  {co%d, %d}, /* %s */' % (i, len(ch['obj']), ch['name']))
c.append('};')
for i, st in enumerate(structs):
    c.append('static const uint16_t sm%d[] = {%s};' % (i, ','.join(map(str, st['mid'] or [0]))))
    c.append('static const uint16_t ss%d[] = {%s};' % (i, ','.join(map(str, st['side'] or [0]))))
    c.append('static const uint16_t sl%d[] = {%s};' % (i, ','.join(map(str, st['list'] or [0]))))
c.append('const Struct structs[] = {')
for i, st in enumerate(structs):
    ball = st.get('ball', (0, 0, 0, 0, 0, 0))
    c.append('  {%d, %d, %s, %d, %d, %d, %d, %d, %d, sm%d, %d, ss%d, %d, sl%d, %d, {%d, %s, %s, %s, %s, %s}}, /* %s */' % (
        st['min'], st['max'], F(st['p']), st['lmin'], st['lmax'], st['mod'], st['next'], st['a'], st['b'], i, len(st['mid']), i,
        len(st['side']), i, len(st['list']), ball[0], F(ball[1]), F(ball[2]), F(ball[3]), F(ball[4]), F(ball[5]), st['name']))
c.append('};')
for i, (name, lo, hi, ss) in enumerate(biomes):
    c.append('static const uint16_t bs%d[] = {%s};' % (i, ','.join(map(str, ss))))
c.append('const Biome biomes[] = {%s};' % ','.join('{%d,%d,bs%d,%d}' % (lo, hi, i, len(ss)) for i, (name, lo, hi, ss) in enumerate(biomes)))
for i, (pf, lo, hi, alts) in enumerate(rand):
    c.append('static const uint16_t ra%d[] = {%s};' % (i, ','.join(map(str, alts or [0]))))
c.append('const Rand rands[] = {%s};' % ','.join('{%d,{%s},{%s},ra%d,%d}' % (pf, ','.join(F(x) for x in lo), ','.join(F(x) for x in hi), i, len(alts)) for i, (pf, lo, hi, alts) in enumerate(rand)))
c.append('const Skin skins[] = {')
for name, price, pf, pieces, campt, box in skins:
    bc, bs = box
    c.append('  {"%s", %d, %d, %d, {%s}, {%s}, {%s}},' % (name, price, pf, pieces, ','.join(F(x) for x in campt), ','.join(F(x) for x in bc), ','.join(F(x / 2) for x in bs)))
c.append('};')
c.append('const uint16_t palette[] = {%s};' % ','.join('0x%04X' % (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)) for r, g, b in palette))
c.append('const uint8_t palette888[][3] = {%s};' % ','.join('{%d,%d,%d}' % p for p in palette))
open(os.path.join(OUT, 'data.c'), 'w').write('\n'.join(c) + '\n')


def V(v):
    return ', '.join(F(x) for x in v)


sp, sq = points['SledsPoint']
cpp, cpq = points['CamPoint']
fl = lambda d: (float(d['x']), float(d['y']), float(d['z']))
h = ['/* Made by tools/gen.py from Snow Rider 3D\'s Unity project: do not edit. */',
     '#ifndef SR_DATA_H', '#define SR_DATA_H', '#include <stdint.h>', '',
     '/* a mesh to draw: corners (int16 times s metres), triangles, a palette colour and a normal (int8, 127 = 1) each,',
     ' * and a sphere around it */',
     'typedef struct { const int16_t *v; const uint16_t *t; const uint8_t *c; const int8_t *n; uint16_t nv, nt; float s; float ctr[3], rad; } RMesh;',
     '/* a collision mesh: corners, triangles (their front: clockwise seen from outside), its box */',
     'typedef struct { const int16_t *v; const uint16_t *t; uint16_t nv, nt; float s; float lo[3], hi[3]; } CMesh;',
     '/* a mesh of a prefab, near and far, and where it lies in the prefab (3x4, rows) */',
     'typedef struct { uint16_t near, far; float m[12]; } Part;',
     '/* a collider: box (the matrix takes the unit cube to it), sphere (unit sphere), capsule (centre, along axis dir,',
     ' * radius r, height h) or mesh (cmeshes[mesh]); layer: L_ bits */',
     'enum { C_BOX, C_SPHERE, C_CAPSULE, C_MESH };',
     'enum { L_SURFACE = 1, L_OBSTACLE = 2, L_BREAK = 4, L_GIFT = 8 };',
     'typedef struct { uint8_t kind, layer, trigger, dir_or_mesh; float m[12]; float r, h; } Coll;',
     '#define COLL_MESH(c) ((c)->dir_or_mesh)',
     '/* flags: 1 gift, 2 breaks into pieces (chunks[pieces]), 4 a physics body, 16 a snowball spawner at spawner */',
     'enum { PF_GIFT = 1, PF_BREAK = 2, PF_BODY = 4, PF_SPAWNER = 16 };',
     'typedef struct { const Part *parts; const Coll *colls; uint8_t nparts, ncolls, flags; int16_t pieces, rand; float spawner[3]; } Prefab;',
     'typedef struct { uint16_t prefab; float pos[3], rot[4], scale[3]; } Obj;',
     'typedef struct { const Obj *obj; uint16_t n; } Chunk;',
     '/* modifiers: tunnel (list: tunnels put in chunk 1 and 2), crack start (a ramp chunk, b side ramp chunk), next (the',
     ' * structure after it is next), snowballs (a start chunk, b end chunk, a spawner rolling ball) */',
     'enum { MOD_NONE, MOD_TUNNEL, MOD_CRACKSTART, MOD_NEXT, MOD_SNOWBALLS };',
     'typedef struct { uint16_t prefab; float interval, speed, randomness, min_size, max_size; } Balls;',
     'typedef struct { int32_t min_score, max_score; float prob; uint8_t min_len, max_len, mod; int16_t next, a, b;',
     '  const uint16_t *mid; uint8_t nmid; const uint16_t *side; uint8_t nside; const uint16_t *list; uint8_t nlist; Balls balls; } Struct;',
     'typedef struct { uint8_t min_len, max_len; const uint16_t *structs; uint8_t n; } Biome;',
     'typedef struct { uint16_t prefab; float lo[3], hi[3]; const uint16_t *alt; uint8_t nalt; } Rand;',
     'typedef struct { const char *name; uint16_t price, prefab, pieces; float cam[3], box_c[3], box_h[3]; } Skin;',
     '',
     'extern const RMesh rmeshes[];', 'extern const CMesh cmeshes[];', 'extern const Prefab prefabs[];', 'extern const Chunk chunks[];',
     'extern const Struct structs[];', 'extern const Biome biomes[];', 'extern const Rand rands[];', 'extern const Skin skins[];',
     'extern const uint16_t palette[];', 'extern const uint8_t palette888[][3];',
     '#define NRANDS %d' % len(rand), '#define NCOLOURS %d' % len(palette), '#define NSKINS %d' % len(skins), '#define NBIOMES %d' % len(biomes),
     '#define CAMP_CHUNK %d' % camp_chunk, '',
     '/* GenSettings */',
     '#define VISIBLE_STRUCTS %s' % gen['visibleStructCount'], '#define STRUCT_W %s' % gen['structWidth'],
     '#define CHUNK_X %s' % F(float(gen['chunkSize']['x'])), '#define CHUNK_Z %s' % F(float(gen['chunkSize']['y'])),
     '#define WARMUP_LEN %s' % gen['warmupLength'],
     '/* BasicSleds */'] + ['#define SLED_%s %s' % (k.upper(), F(float(sled[k]))) for k in
     ('baseMoveSpeed', 'speedAcceleration', 'speedAccelerationAmplitude', 'rotationSpeed', 'rotationSmoothness', 'modelRotationSmoothness', 'jumpSpeed')] + [
     '/* the scene */',
     '#define GRAVITY_ACC %s' % F(float(point['gravityAcc'])),
     '#define POINT_POS {%s}' % V(unity.v3(cp['m_LocalPosition'])),
     '#define NRAYS %d' % len(rays), '#define RAYS {%s}' % ', '.join('{%s}' % V(p) for p, l in rays), '#define RAY_LEN %s' % F(rays[0][1]),
     '#define CAM_ROT_AMPLIFY {%s}' % V(fl(cam_mb['rotationAmplify'])), '#define CAM_OFFSET {%s}' % V(fl(cam_mb['positionOffset'])),
     '#define CAM_FOV %s' % F(float(camera['field of view'])), '#define CAM_INTRO_SPEED %s' % F(float(cam_mb['spd'])),
     '#define CURVATURE %s' % F(curv),
     '#define LIGHT_DIR {%s}' % V(ldir), '#define LIGHT_INTENSITY %s' % F(lint),
     '#define AMBIENT {%s}' % V((float(amb['r']), float(amb['g']), float(amb['b']))),
     '#define SKY_GROUND {%s}' % V((float(sky['_GroundColor']['r']), float(sky['_GroundColor']['g']), float(sky['_GroundColor']['b']))),
     '#define BACKGROUND {%s}' % V((float(camera['m_BackGroundColor']['r']), float(camera['m_BackGroundColor']['g']), float(camera['m_BackGroundColor']['b']))),
     '#define SLEDS_POINT {%s}' % V(sp), '#define SLEDS_POINT_ROT {%s}' % V(sq),
     '#define CAMP_CAM_POINT {%s}' % V(cpp), '#define CAMP_CAM_ROT {%s}' % V(cpq),
     '#endif']
open(os.path.join(OUT, 'data.h'), 'w').write('\n'.join(h) + '\n')
print('meshes %d (%d triangles), collision meshes %d (%d triangles), prefabs %d, chunks %d, structures %d, colours %d' % (
    len(rmeshes), stats['render'], len(cmeshes), stats['coll'], len(prefabs), len(chunks), len(structs), len(palette)))
