"""(Subway Surfers' tools/decimate.py.) Fewer triangles for the calculator: edge collapses ranked by how far they move the surface (quadric errors,
Garland and Heckbert), that keep the picture laid on it.

A mesh comes as corners (each with a position, a place in its picture and the bones it follows) and triangles.
Corners at the same position are one point of the surface; where the picture has a seam a point has several
corners. A collapse moves point a onto point b: each corner of a becomes the corner of b it shares a triangle with
(so the picture stays on), and a collapse that would need a corner b does not have (across a seam) is not made,
nor one that turns a triangle over. Its cost is the quadric error, plus how far the picture slides and a change of
the main bone (so knees and elbows keep bending well)."""
import heapq

import numpy as np


def _plane_quadric(p0, p1, p2):
    n = np.cross(p1 - p0, p2 - p0)
    area = np.linalg.norm(n)
    if area < 1e-12:
        return np.zeros((4, 4))
    n = n / area
    d = -n @ p0
    q = np.array([n[0], n[1], n[2], d])
    return np.outer(q, q) * area


def decimate(pos, uv, bone, faces, target, uv_weight=4.0, bone_weight=0.02, lock=None):
    """pos (n,3), uv (n,2) or None, bone (n,) main bone of each corner or None, faces (m,3) corner indices.
    Returns the faces left (their corner indices: corners are kept, some unused). lock: corners never moved."""
    pos = np.asarray(pos, float)
    n = len(pos)
    faces = [tuple(int(c) for c in f) for f in faces]
    # points: corners at one position
    key = {}
    point = np.zeros(n, int)
    for i, p in enumerate(np.round(pos, 5)):
        point[i] = key.setdefault(tuple(p), len(key))
    npts = len(key)
    ppos = np.zeros((npts, 3))
    for i in range(n):
        ppos[point[i]] = pos[i]
    alive = [True] * len(faces)
    corner_of = [c for c in range(n)]          # where a corner has gone (collapsed corners follow their new one)
    pt_faces = [set() for _ in range(npts)]
    for fi, f in enumerate(faces):
        for c in f:
            pt_faces[point[c]].add(fi)
    Q = [np.zeros((4, 4)) for _ in range(npts)]
    for f in faces:
        q = _plane_quadric(*(ppos[point[c]] for c in f))
        for c in f:
            Q[point[c]] += q
    # open edges (holes, not seams): heavy quadrics so the outline stays
    edge_count = {}
    for f in faces:
        for k in range(3):
            a, b = point[f[k]], point[f[(k + 1) % 3]]
            e = (min(a, b), max(a, b))
            edge_count[e] = edge_count.get(e, 0) + 1
    for (a, b), cnt in edge_count.items():
        if cnt == 1:
            # a plane through the edge, perpendicular to a face beside it
            for fi in pt_faces[a] & pt_faces[b]:
                f = faces[fi]
                p0, p1, p2 = (ppos[point[c]] for c in f)
                fn = np.cross(p1 - p0, p2 - p0)
                ev = ppos[b] - ppos[a]
                bn = np.cross(ev, fn)
                ln = np.linalg.norm(bn)
                if ln < 1e-12:
                    continue
                bn /= ln
                qv = np.array([bn[0], bn[1], bn[2], -bn @ ppos[a]])
                w = 1000.0 * (ev @ ev)
                Q[a] += np.outer(qv, qv) * w
                Q[b] += np.outer(qv, qv) * w
    locked = set(point[c] for c in (lock or []))
    pt_alive = [True] * npts
    version = [0] * npts

    def cur(c):
        while corner_of[c] != c:
            corner_of[c] = corner_of[corner_of[c]]
            c = corner_of[c]
        return c

    def collapse_plan(a, b):
        """corner map for moving point a onto b, or None"""
        fa = [fi for fi in pt_faces[a] if alive[fi]]
        both = [fi for fi in fa if b in (point[cur(c)] for c in faces[fi])]
        if not both:
            return None
        cmap = {}
        for fi in both:
            cs = [cur(c) for c in faces[fi]]
            ca = [c for c in cs if point[c] == a][0]
            cb = [c for c in cs if point[c] == b][0]
            if cmap.get(ca, cb) != cb:
                return None
            cmap[ca] = cb
        for fi in fa:
            for c in faces[fi]:
                c = cur(c)
                if point[c] == a and c not in cmap:
                    return None   # across a seam
        # no triangle turned over or squashed
        for fi in fa:
            if fi in both:
                continue
            cs = [cur(c) for c in faces[fi]]
            p = [ppos[point[c]] for c in cs]
            n0 = np.cross(p[1] - p[0], p[2] - p[0])
            q = [ppos[b] if point[c] == a else ppos[point[c]] for c in cs]
            n1 = np.cross(q[1] - q[0], q[2] - q[0])
            l0, l1 = np.linalg.norm(n0), np.linalg.norm(n1)
            if l1 < 1e-12 or (l0 > 1e-12 and n0 @ n1 < 0.2 * l0 * l1):
                return None
        return cmap

    def cost(a, b):
        if a in locked:
            return None
        cmap = collapse_plan(a, b)
        if cmap is None:
            return None
        v = np.append(ppos[b], 1.0)
        e = float(v @ (Q[a] + Q[b]) @ v)
        slide = 0.0
        if uv is not None:
            for ca, cb in cmap.items():
                slide = max(slide, float(((uv[ca] - uv[cb]) ** 2).sum()))
        size = float(((ppos[a] - ppos[b]) ** 2).sum())
        e += uv_weight * slide * size
        if bone is not None:
            for ca, cb in cmap.items():
                if bone[ca] != bone[cb]:
                    e += bone_weight * size
                    break
        return e

    heap = []

    def push_point(a):
        nb = set()
        for fi in pt_faces[a]:
            if alive[fi]:
                for c in faces[fi]:
                    p = point[cur(c)]
                    if p != a:
                        nb.add(p)
        for b in nb:
            for x, y in ((a, b), (b, a)):
                c = cost(x, y)
                if c is not None:
                    heapq.heappush(heap, (c, x, y, version[x], version[y]))

    for a in range(npts):
        push_point(a)
    nfaces = len(faces)
    while nfaces > target and heap:
        c, a, b, va, vb = heapq.heappop(heap)
        if not (pt_alive[a] and pt_alive[b]) or version[a] != va or version[b] != vb:
            continue
        cmap = collapse_plan(a, b)
        if cmap is None:
            continue
        for ca, cb in cmap.items():
            corner_of[ca] = cb
        for fi in list(pt_faces[a]):
            if not alive[fi]:
                continue
            cs = [point[cur(c)] for c in faces[fi]]
            if len(set(cs)) < 3:
                alive[fi] = False
                nfaces -= 1
            else:
                pt_faces[b].add(fi)
        pt_alive[a] = False
        Q[b] = Q[b] + Q[a]
        version[b] += 1
        touched = {b}
        for fi in pt_faces[b]:
            if alive[fi]:
                for cc in faces[fi]:
                    touched.add(point[cur(cc)])
        for p in touched:
            version[p] += 1
        for p in touched:
            push_point(p)
    out = []
    for fi, f in enumerate(faces):
        if alive[fi]:
            out.append([cur(c) for c in f])
    return np.array(out, int)
