"""Reads the Unity project of Snow Rider 3D (YAML scenes, prefabs, assets and meshes)."""
import os, re, struct
import yaml

DOC = re.compile(r'^--- !u!(\d+) &(-?\d+)( stripped)?\s*$', re.M)


class Project:
    def __init__(self, root):
        self.root = root
        self.guids = {}
        for d, _, fs in os.walk(root):
            for f in fs:
                if f.endswith('.meta'):
                    with open(os.path.join(d, f), errors='ignore') as fh:
                        m = re.search(r'guid: (\w+)', fh.read(4000))
                    if m:
                        self.guids[m.group(1)] = os.path.join(d, f[:-5])
        self.cache = {}

    def path(self, guid):
        return self.guids.get(guid)

    def docs(self, path):
        """{fileID: (classID, body dict, stripped)} of a YAML file."""
        if path in self.cache:
            return self.cache[path]
        text = open(path, errors='ignore').read()
        out = {}
        heads = list(DOC.finditer(text))
        for i, h in enumerate(heads):
            body = text[h.end():heads[i + 1].start() if i + 1 < len(heads) else len(text)]
            body = re.sub(r'!u!\d+', '', body)
            d = yaml.load(body, Loader=yaml.CBaseLoader if hasattr(yaml, 'CBaseLoader') else yaml.BaseLoader) or {}
            (_, v), = d.items() if len(d) == 1 else (('', d),)
            out[h.group(2)] = (int(h.group(1)), v, bool(h.group(3)))
        self.cache[path] = out
        return out

    def asset(self, ref):
        """The main object of an asset reference {fileID, guid}."""
        p = self.path(ref['guid'])
        if not p:
            return None
        docs = self.docs(p)
        d = docs.get(ref['fileID']) or next(iter(docs.values()))
        return d[1]


def f(x):
    return float(x)


def v3(d):
    return (f(d['x']), f(d['y']), f(d['z']))


def q4(d):
    return (f(d['x']), f(d['y']), f(d['z']), f(d['w']))


def hexbytes(s):
    s = re.sub(r'[^0-9a-fA-F]', '', s)
    return bytes.fromhex(s[:len(s) & ~1])


def mesh(proj, ref):
    """(positions, normals, uvs, triangles) of a Mesh asset."""
    p = proj.path(ref['guid'])
    docs = proj.docs(p)
    m = docs.get(ref['fileID'], next(iter(docs.values())))[1]
    vd = m['m_VertexData']
    n = int(vd['m_VertexCount'])
    data = hexbytes(vd['_typelessdata'])
    ch = vd['m_Channels']
    # streams: offsets of each stream, a stream's stride is the sum of its channels
    sizes = {0: 4, 1: 2, 2: 1, 3: 2}  # float32, float16, unorm8, unorm16 (enough for these meshes)
    strides = {}
    for c in ch:
        s, dim, fmt = int(c['stream']), int(c['dimension']) & 15, int(c['format'])
        if dim:
            strides[s] = max(strides.get(s, 0), int(c['offset']) + dim * sizes.get(fmt, 4))
    starts, o = {}, 0
    for s in sorted(strides):
        starts[s] = o
        o += strides[s] * n
        o = (o + 15) & ~15
    def chan(i):
        c = ch[i]
        dim, fmt = int(c['dimension']) & 15, int(c['format'])
        if not dim:
            return None
        s = int(c['stream'])
        base, stride = starts[s] + int(c['offset']), strides[s]
        out = []
        for k in range(n):
            a = base + k * stride
            if fmt == 0:
                out.append(struct.unpack_from('<%df' % dim, data, a))
            elif fmt == 1:
                out.append(struct.unpack_from('<%de' % dim, data, a))
            elif fmt == 2:
                out.append(tuple(b / 255 for b in data[a:a + dim]))
            else:
                raise ValueError('vertex format %d' % fmt)
        return out
    pos, nrm, uv = chan(0), chan(1), chan(4)
    ib = hexbytes(m['m_IndexBuffer'])
    wide = m.get('m_IndexFormat', '0') == '1'
    idx = struct.unpack('<%d%s' % (len(ib) // (4 if wide else 2), 'I' if wide else 'H'), ib)
    tris = []
    for sm in m['m_SubMeshes']:
        first = int(sm['firstByte']) // (4 if wide else 2)
        cnt, bv = int(sm['indexCount']), int(sm.get('baseVertex', 0))
        if sm.get('topology', '0') != '0':
            continue
        for k in range(first, first + cnt - 2, 3):
            tris.append((idx[k] + bv, idx[k + 1] + bv, idx[k + 2] + bv))
    return pos, nrm, uv, tris


def qmat(q):
    x, y, z, w = q
    return [[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
            [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
            [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]]


def trs(t, q, s):
    """A 3x4 matrix: translate t, rotate q, scale s."""
    r = qmat(q)
    return [[r[i][0] * s[0], r[i][1] * s[1], r[i][2] * s[2], t[i]] for i in range(3)]


def mul(a, b):
    return [[sum(a[i][k] * b[k][j] for k in range(3)) + (a[i][3] if j == 3 else 0) for j in range(4)] for i in range(3)]


def apply(m, p):
    return tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] + m[i][3] for i in range(3))


def applyv(m, p):
    return tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] for i in range(3))


IDENT = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]]


class Node:
    """A GameObject of a prefab: its name, tag, layer, matrix (relative to the prefab's root, the root's own
    transform left out), its components {class name: [bodies]} and children."""
    def __init__(self):
        self.children = []


CLASSES = {'4': 'Transform', '224': 'Transform', '33': 'MeshFilter', '23': 'MeshRenderer', '65': 'BoxCollider', '64': 'MeshCollider',
           '135': 'SphereCollider', '136': 'CapsuleCollider', '54': 'Rigidbody', '114': 'MonoBehaviour'}


def prefab(proj, ref):
    """The tree of Nodes of a prefab (or of a scene's object), from its root GameObject."""
    p = proj.path(ref['guid'])
    docs = proj.docs(p)
    return node(proj, docs, ref['fileID'], True)


def node(proj, docs, gid, root=False, parent=IDENT):
    cls, go, _ = docs[gid]
    n = Node()
    n.name, n.tag, n.layer, n.active = go['m_Name'], go.get('m_TagString'), int(go.get('m_Layer', 0)), go.get('m_IsActive', '1') == '1'
    n.comps = {}
    tr = None
    for c in go['m_Component']:
        cid = c['component']['fileID']
        if cid not in docs:
            continue
        ccls, body, _ = docs[cid]
        name = CLASSES.get(str(ccls), str(ccls))
        if name == 'MonoBehaviour':
            sp = proj.path(body['m_Script']['guid']) or ''
            name = os.path.basename(sp)[:-3] if sp.endswith('.cs') else 'MonoBehaviour'
        n.comps.setdefault(name, []).append(body)
        if ccls in (4, 224):
            tr = body
    local = IDENT if root else trs(v3(tr['m_LocalPosition']), q4(tr['m_LocalRotation']), v3(tr['m_LocalScale']))
    n.local = local
    n.m = mul(parent, local)
    for ch in tr.get('m_Children') or []:
        cid = ch['fileID']
        if cid in docs:
            n.children.append(node(proj, docs, docs[cid][1]['m_GameObject']['fileID'], False, n.m))
    return n


def walk(n):
    yield n
    for c in n.children:
        yield from walk(c)
