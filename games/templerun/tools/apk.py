#!/usr/bin/env python3
"""Makes src/data.c and src/data.h from Temple Run's own Android package (1.0.8, by Imangi Studios): the game's
models, as the mesh table src/models.h describes, in place of tools/models.py's stand-ins.

- The path, its obstacles, the coins and the scenery: the game's prefabs (its Resources/Prefabs/Temple), each a
  mesh laid with its texture and its light map. Each triangle gets one colour, what its texture and light map show
  over it on average, with fewer triangles for the calculator (games/subway/tools/decimate.py), and a far version.
- Guy Dangerous and the demon monkeys: the game's skinned meshes, posed by its own animation clips (Run, Jump,
  Slide Loop, Stumble) at the moments the game shows, coloured from their textures the same way.
What the game draws as particles or sprites (the fire, the power-ups) stays tools/models.py's.

The game's units are the port's (a path tile is 60 across, x right, y up, z ahead, the floor at 0), so its meshes
come as they are. Usage: apk.py DATA [SRC]   (DATA: the package's assets/bin/Data, unzipped; nothing in it is run.
Needs UnityPy and numpy.)"""
import math
import os
import sys

import numpy as np
import UnityPy
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(HERE, "..", "..", "subway", "tools"))
import decimate  # noqa: E402
import models  # noqa: E402
import unity  # noqa: E402

import pieces  # noqa: E402

LIGHT = 1.4                       # the light maps: Unity's mobile ones are x 2, a little less here, as the port shades faces too
FACES_NEAR, FACES_FAR = 60, 20   # triangles a path piece keeps, near and far (each of its meshes)
FACES_TEMPLE = 200                # (the temple the run starts from, seen up close)
FACES_RUNNER, FACES_PURSUER = 180, 80
# scenery and coins: the port's name (the game's prefab), its triangles and its far version's (None: no far one)
SCENERY = [("treeAlone", 36, 8), ("treeAlone2", 44, 8), ("treeAlone3", 50, 10), ("treeAlone4", 50, 10),
           ("rock", 30, 6), ("rock2", 30, 6), ("rock3", 26, 6), ("cliff", 56, 10), ("cliff2", 56, 10),
           ("cliff3", 56, 10), ("overgrowthTrees", 64, 12), ("templeHead", 44, 8), ("groundPatchTemple", 30, 6)]
COINS = [("COIN_Y", "coinYellow"), ("COIN_R", "coinRed"), ("COIN_B", "coinBlue")]


class Package:
    def __init__(self, data):
        self.env = UnityPy.load(data)
        self.copies, self.textures = {}, {}
        for o in self.env.objects:
            if o.type.name == "GameObject":
                go = o.read()
                if go.m_Name.endswith("_prefab"):
                    self.copies.setdefault(go.m_Name[:-7], []).append(go)
            elif o.type.name == "Texture2D":
                t = o.read()
                self.textures.setdefault(t.m_Name, t)
        self._img = {}
        self.prefabs = {n: c[0] for n, c in self.copies.items()}

    def image(self, name):
        if name not in self._img:
            self._img[name] = np.asarray(self.textures[name].image.convert("RGBA"), np.float64) / 255
        return self._img[name]

    @staticmethod
    def comps(go):
        out = []
        for c in go.m_Component:
            p = c[1] if isinstance(c, (list, tuple)) else getattr(c, "component", c)
            try:
                out.append(p.read())
            except Exception:
                pass
        return out

    def meshes(self, name):
        """the prefab's meshes, in its own space: [(pos, uv0, uv1, tris, [texture names])] (of the first of its copies
        in the package that still has them)"""
        for go in self.copies[name]:
            out = self._meshes(go)
            if out:
                return out
        return []

    def _meshes(self, go):
        out = []
        root = [c for c in self.comps(go) if c.object_reader.type.name == "Transform"][0]

        def walk(tr, M, depth):
            lp, lr, ls = tr.m_LocalPosition, tr.m_LocalRotation, tr.m_LocalScale
            W = M @ unity.trs((lp.x, lp.y, lp.z), (lr.x, lr.y, lr.z, lr.w), (ls.x, ls.y, ls.z)) if depth else np.eye(4)
            cs = self.comps(tr.m_GameObject.read())
            mf = [c for c in cs if c.object_reader.type.name == "MeshFilter"]
            mr = [c for c in cs if c.object_reader.type.name == "MeshRenderer"]
            if mf and mf[0].m_Mesh.m_PathID:   # (a few of the game's prefabs lost their mesh: they stay stand-ins)
                from UnityPy.helpers.MeshHelper import MeshHandler
                h = MeshHandler(mf[0].m_Mesh.read())
                h.process()
                pos = np.array(h.m_Vertices, np.float64).reshape(-1, 3)
                pos = (np.c_[pos, np.ones(len(pos))] @ W.T)[:, :3]
                uv0 = np.array(h.m_UV0, np.float64).reshape(-1, 2) if h.m_UV0 else None
                uv1 = np.array(h.m_UV1, np.float64).reshape(-1, 2) if h.m_UV1 else None
                tris = np.concatenate([np.array(t, np.int64).reshape(-1, 3) for t in h.get_triangles()])
                texs = []
                if mr:
                    for m in mr[0].m_Materials:
                        try:
                            for _, v in m.read().m_SavedProperties.m_TexEnvs:
                                if v.m_Texture.m_PathID:
                                    texs.append(v.m_Texture.read().m_Name)
                        except Exception:
                            pass
                out.append((pos, uv0, uv1, tris, texs))
            for ch in tr.m_Children:
                walk(ch.read(), W, depth + 1)
        walk(root, np.eye(4), 0)
        return out

    def sample(self, name, uv):
        """a texture's colours at uv (n,2), wrapped, as Unity reads them (v up)"""
        img = self.image(name)
        h, w = img.shape[:2]
        x = np.mod(uv[:, 0], 1.0) * w
        y = (1 - np.mod(uv[:, 1], 1.0)) * h
        return img[np.clip(y.astype(int), 0, h - 1), np.clip(x.astype(int), 0, w - 1)]


# ---------------------------------------------------------------- the people: skinned meshes, posed by their clips
def hermite(keys, t, comp):
    """a curve's value at t (Unity's legacy keyframes: Hermite between keys, held past the ends)"""
    if t <= keys[0].time:
        return comp(keys[0].value)
    for a, b in zip(keys, keys[1:]):
        if t <= b.time:
            dt = b.time - a.time
            u = (t - a.time) / dt if dt > 0 else 0
            p0, p1 = np.array(comp(a.value)), np.array(comp(b.value))
            m0, m1 = np.array(comp(a.outSlope)) * dt, np.array(comp(b.inSlope)) * dt
            if not (np.all(np.isfinite(m0)) and np.all(np.isfinite(m1))):
                return p0
            u2, u3 = u * u, u * u * u
            return (2 * u3 - 3 * u2 + 1) * p0 + (u3 - 2 * u2 + u) * m0 + (-2 * u3 + 3 * u2) * p1 + (u3 - u2) * m1
    return comp(keys[-1].value)


Q4 = lambda q: (q.x, q.y, q.z, q.w)  # noqa: E731
V3 = lambda v: (v.x, v.y, v.z)  # noqa: E731


class Person:
    """a skinned prefab (its first SkinnedMeshRenderer), its skeleton and the clips its rig plays"""

    def __init__(self, pkg, prefab, clip_owner):
        go = pkg.prefabs[prefab]
        root = [c for c in pkg.comps(go) if c.object_reader.type.name == "Transform"][0]
        self.sk = unity.Skeleton(root)
        # (a clip's paths start below the object that plays it, the prefab's first child)
        self.by_path = {p.split("/", 1)[1] if "/" in p else "": i for i, p in enumerate(self.sk.paths) if p}
        smr = None

        def find(tr):
            nonlocal smr
            for c in pkg.comps(tr.m_GameObject.read()):
                if c.object_reader.type.name == "SkinnedMeshRenderer" and smr is None:
                    smr = c
            for ch in tr.m_Children:
                find(ch.read())
        find(root)
        self.bones = [self.sk.by_pid[b.m_PathID] for b in smr.m_Bones]
        self.mesh = unity.mesh_arrays(smr.m_Mesh.read())
        self.tex = []
        for m in smr.m_Materials:
            for _, v in m.read().m_SavedProperties.m_TexEnvs:
                if v.m_Texture.m_PathID:
                    self.tex.append(v.m_Texture.read().m_Name)
        self.clips = {}
        for o in pkg.env.objects:
            if o.type.name == "Animation":
                a = o.read()
                if a.m_GameObject.read().m_Name != clip_owner:
                    continue
                for c in a.m_Animations:
                    try:
                        clip = c.read()
                    except Exception:
                        continue
                    self.clips.setdefault(clip.m_Name, clip)

    def length(self, clip):
        c = self.clips[clip]
        return max(k.time for kind in ("m_RotationCurves", "m_PositionCurves") for cv in getattr(c, kind, [])
                   for k in cv.curve.m_Curve)

    def pose(self, clip, t):
        """the mesh's corners where the clip puts them at t seconds (model space)"""
        c = self.clips[clip]
        local = {}
        for kind, comp in (("m_RotationCurves", Q4), ("m_PositionCurves", V3), ("m_ScaleCurves", V3)):
            for cv in getattr(c, kind, []):
                i = self.by_path.get(cv.path)
                if i is None or not cv.curve.m_Curve:
                    continue
                v = hermite(cv.curve.m_Curve, t, comp)
                cur = list(local.get(i, (None, None, None)))
                if kind == "m_RotationCurves":
                    cur[1] = tuple(np.asarray(v) / (np.linalg.norm(v) or 1))
                elif kind == "m_PositionCurves":
                    cur[0] = tuple(v)
                else:
                    cur[2] = tuple(v)
                local[i] = tuple(cur)
        W = self.sk.world(local)
        m = self.mesh
        skin = [W[b] @ m["bindpose"][k] for k, b in enumerate(self.bones)]
        p = np.c_[m["pos"], np.ones(len(m["pos"]))]
        out = np.zeros((len(p), 3))
        for j in range(4):
            M = np.array([skin[b] for b in m["bidx"][:, j]])
            out += m["bw"][:, j:j + 1] * np.einsum("nij,nj->ni", M, p)[:, :3]
        return out


def person_faces(pkg, person, clip, t, target, turn=0.0, scale=1.0):
    """a person posed, as faces coloured from its texture, at most target triangles"""
    pos = person.pose(clip, t) * scale
    if turn:
        c, s = math.cos(turn), math.sin(turn)
        pos = np.c_[pos[:, 0] * c + pos[:, 2] * s, pos[:, 1], -pos[:, 0] * s + pos[:, 2] * c]
    m = person.mesh
    tris = np.concatenate(m["subs"])
    # (the game's meshes split their corners at every seam: welded by place, so the decimator can take edges away;
    # each welded corner is coloured by the triangles round it, and a face left by its corners)
    cols, _ = colours(pkg, m["uv"][tris], None, person.tex)
    weld, wpos = {}, []
    wi = np.array([weld.setdefault(tuple(np.round(q, 4)), len(weld)) for q in pos])
    for q, i in zip(pos, wi):
        if i == len(wpos):
            wpos.append(q)
    wpos = np.array(wpos)
    wt = wi[tris]
    area = np.linalg.norm(np.cross(pos[tris[:, 1]] - pos[tris[:, 0]], pos[tris[:, 2]] - pos[tris[:, 0]]), axis=1) + 1e-9
    acc, wsum = np.zeros((len(wpos), 3)), np.zeros(len(wpos))
    for k in range(3):
        np.add.at(acc, wt[:, k], cols * area[:, None])
        np.add.at(wsum, wt[:, k], area)
    vcol = acc / np.maximum(wsum, 1e-9)[:, None]
    bone = np.zeros(len(wpos), np.int64)
    bone[wi] = m["bidx"][np.arange(len(pos)), m["bw"].argmax(1)]
    keep = decimate.decimate(wpos, None, bone, wt, target) if len(wt) > target else wt
    pos, cols = wpos, vcol[keep].mean(1)
    out = []
    for tr, col in zip(keep, cols):
        pts = [list(pos[i]) for i in tr]
        n = models.cross(models.sub(pts[1], pts[0]), models.sub(pts[2], pts[0]))
        if models.dot(n, n) < 1e-12:
            continue
        rgb = (int(col[0] * 255) << 16) | (int(col[1] * 255) << 8) | int(col[2] * 255)
        out.append((pts, rgb, models.norm(n), False))
    return out


# barycentric points a triangle's colour is the average of
BARY = np.array([(1 / 3, 1 / 3, 1 / 3)] + [(a, b, 1 - a - b) for a in (0.1, 0.45, 0.8) for b in (0.1, 0.45, 0.8) if a + b < 0.95])


def colours(pkg, tri_uv0, tri_uv1, texs):
    """each triangle's colour (k,3), what its texture shows over it where it isn't see-through, times its light map
    (x LIGHT); and whether it is mostly there (k,): the game's leaves are cards with see-through pictures, which as
    one colour would be solid sheets, so the mostly see-through ones go"""
    base, light = None, None
    for t in texs:
        if "lightMap" in t:
            light = t
        elif base is None:
            base = t
    k, m = len(tri_uv0), len(BARY)
    if base:
        s = pkg.sample(base, np.einsum("pj,kjc->kpc", BARY, tri_uv0).reshape(-1, 2)).reshape(k, m, 4)
        solid = s[:, :, 3] >= 0.5
        col = (s[:, :, :3] * solid[:, :, None]).sum(1) / np.maximum(solid.sum(1), 1)[:, None]
        there = solid.mean(1) >= 0.5
    else:
        col, there = np.full((k, 3), 0.6), np.ones(k, bool)
    if light and tri_uv1 is not None:
        lm = pkg.sample(light, np.einsum("pj,kjc->kpc", BARY, tri_uv1).reshape(-1, 2))[:, :3].reshape(k, m, 3).mean(1)
        col = col * np.clip(lm * LIGHT, 0, 1.6)
    # (the doorway's inside is painted black, for the temple's dark hall, which the port has nothing behind: dark stone)
    return np.clip(np.maximum(col, 0.14), 0, 1), there


def cluster(p, tris, target):
    """fewer triangles for a piece of scenery or path: its corners snapped to a grid, coarser until at most target
    triangles are left that still have three corners apart (the game's prefabs share no corners between
    triangles, so an edge collapser stalls on them). Each triangle left keeps its own texture place, so its colour.
    Returns the snapped corners and the triangles left."""
    if len(tris) <= target:
        return p, tris
    for cell in (0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0, 12.0, 16.0):
        q = np.round(p / cell) * cell
        seen, keep = set(), []
        for t in tris:
            a, b, c = (tuple(q[i]) for i in t)
            if a == b or b == c or a == c:
                continue
            n = np.cross(np.subtract(b, a), np.subtract(c, a))
            key = tuple(sorted((a, b, c)))
            if n @ n < 1e-6 or key in seen:
                continue
            seen.add(key)
            keep.append(t)
        if len(keep) <= target:
            break
    keep = np.array(keep, np.int64).reshape(-1, 3)
    if len(keep) > target:   # (still too many: the biggest)
        a = np.linalg.norm(np.cross(q[keep[:, 1]] - q[keep[:, 0]], q[keep[:, 2]] - q[keep[:, 0]]), axis=1)
        keep = keep[np.sort(np.argsort(-a)[:target])]
    return q, keep


def faces_of(pkg, name, target, turns=0, mirror=False):
    """a prefab as models.py's faces (pts, rgb, normal, two-sided), with at most target triangles"""
    out = []
    for pos, uv0, uv1, tris, texs in pkg.meshes(name):
        p = pos.copy()
        if mirror:
            p[:, 0] = -p[:, 0]
            tris = tris[:, ::-1]
        for _ in range(turns % 4):   # (a quarter turn to the right about y)
            p = np.c_[p[:, 2], p[:, 1], -p[:, 0]]
        q, keep = cluster(p, tris, target)
        cols, there = colours(pkg, uv0[keep], uv1[keep] if uv1 is not None else None, texs)
        for t, c in zip(keep[there], cols[there]):
            pts = [list(q[i]) for i in t]
            n = models.cross(models.sub(pts[1], pts[0]), models.sub(pts[2], pts[0]))
            if models.dot(n, n) < 1e-9:
                continue
            rgb = (int(c[0] * 255) << 16) | (int(c[1] * 255) << 8) | int(c[2] * 255)
            # (Unity's triangles are clockwise seen from their front, as models.py's)
            out.append((pts, rgb, models.norm(n), False))
    return out


def top_view(pkg, name, path, turns=0, mirror=False):
    """a picture of a piece from above (x right, z up), to see where it goes"""
    S, R = 4, 64
    img = Image.new("RGB", (2 * R * S // 2, 2 * R * S // 2), (20, 20, 30))
    from PIL import ImageDraw
    d = ImageDraw.Draw(img)
    fs = sorted(faces_of(pkg, name, 10 ** 6, turns, mirror), key=lambda f: max(p[1] for p in f[0]))
    for pts, rgb, n, _ in fs:
        d.polygon([((p[0] + R) * S / 2, (R - p[2]) * S / 2) for p in pts], fill=(rgb >> 16, rgb >> 8 & 255, rgb & 255))
    img.save(path)


def lying(faces):
    """a pose turned face down onto the ground (the runner caught or knocked down)"""
    out = []
    for pts, rgb, n, two in faces:
        q = [[p[0], p[2] * 0.25 + 1.0, p[1] - 4.0] for p in pts]   # (y up becomes z ahead, flattened)
        out.append((q, rgb, [n[0], n[2], n[1]], two))
    return out


def build(data, src):
    pkg = Package(data)
    models.MODELS.clear()
    models.build(src)
    at = {m[0]: i for i, m in enumerate(models.MODELS)}

    def put(name, faces):
        if not faces:
            print("kept the stand-in for", name)
            return
        i = at[name]
        models.MODELS[i] = (name, faces, models.MODELS[i][2])

    for name in pieces.PREFABS:
        # (the game's runner leaves the temple heading south, away from it; the port's heads north: the temple turns
        # a half turn, so it stands behind him)
        turns = 2 if name == "temple" else 0
        put(name, faces_of(pkg, name, FACES_TEMPLE if name == "temple" else FACES_NEAR, turns))
        put(name + "_far", faces_of(pkg, name, FACES_FAR, turns))
    for name, near, far in SCENERY:
        put(name, faces_of(pkg, name, near))
        put(name + "_far", faces_of(pkg, name, far))
    for name, prefab in COINS:
        put(name, faces_of(pkg, prefab, 12))
    guy = Person(pkg, "Jhony_Export3", "conquis_Export1")   # (Guy Dangerous; every runner's rig plays the same clips)
    monkey = Person(pkg, "monster", "monster")
    run = guy.length("Run")
    for i in range(4):
        put(f"RUNNER_RUN{i}", person_faces(pkg, guy, "Run", run * i / 4, FACES_RUNNER))
        put(f"PURSUER_RUN{i}", person_faces(pkg, monkey, "Run", monkey.length("Run") * i / 4, FACES_PURSUER))
    put("RUNNER_JUMP", person_faces(pkg, guy, "Jump", guy.length("Jump") * 0.4, FACES_RUNNER))
    put("RUNNER_FALL", person_faces(pkg, guy, "Jump", guy.length("Jump"), FACES_RUNNER))
    put("RUNNER_SLIDE", person_faces(pkg, guy, "Slide Loop", 0.0, FACES_RUNNER))
    put("RUNNER_STUMBLE", person_faces(pkg, guy, "Stumble", guy.length("Stumble") * 0.3, FACES_RUNNER))
    put("RUNNER_DOWN", lying(person_faces(pkg, guy, "Stumble", guy.length("Stumble"), FACES_RUNNER)))
    for name, faces, _ in models.MODELS:   # (scene.c's scene_mesh keeps MESH_CORNERS, 512, of a mesh)
        corners = {tuple(round(v * models.Q) for v in p) for f in faces for p in f[0]}
        assert len(corners) <= 512, f"{name}: {len(corners)} corners"
    models.emit(src, "tools/apk.py", "Temple Run's own models (by Imangi Studios, from its Android package)")


if __name__ == "__main__":
    if len(sys.argv) > 2 and sys.argv[2] == "--views":
        pkg = Package(sys.argv[1])
        os.makedirs(sys.argv[3], exist_ok=True)
        for n in sys.argv[4].split(","):
            top_view(pkg, n, os.path.join(sys.argv[3], n + ".png"))
    else:
        build(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else os.path.join(HERE, "..", "src"))
