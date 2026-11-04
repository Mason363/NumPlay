"""Moto X3M packer: Nape's polygon rules (winding, edges, area, inertia, centre of mass) and a convex
decomposition for the concave shapes the levels draw."""
import math

EPS = 1e-8


def js_round(v):
    """JavaScript's Math.round (halves go up)."""
    return math.floor(v + 0.5)


def cross(ax, ay, bx, by):
    return ax * by - ay * bx


def nape_area2(vs):
    """twice Nape's signed area (Polygon.__validate_area_inertia)"""
    n = len(vs)
    a = 0.0
    for i in range(n):
        u, v, w = vs[i - 1], vs[i], vs[(i + 1) % n]
        a += v[0] * (w[1] - u[1])
    return a


def clean(vs):
    """drops repeated and collinear vertices (Polygon.splice_collinear)"""
    out = []
    for p in vs:
        if not out or (p[0] - out[-1][0]) ** 2 + (p[1] - out[-1][1]) ** 2 >= EPS * EPS:
            out.append((float(p[0]), float(p[1])))
    while len(out) > 1 and (out[0][0] - out[-1][0]) ** 2 + (out[0][1] - out[-1][1]) ** 2 < EPS * EPS:
        out.pop()
    changed = True
    while changed and len(out) > 3:
        changed = False
        for i in range(len(out)):
            u, v, w = out[i - 1], out[i], out[(i + 1) % len(out)]
            c = cross(v[0] - u[0], v[1] - u[1], w[0] - v[0], w[1] - v[1])
            if c * c < EPS * EPS:
                out.pop(i)
                changed = True
                break
    return out


def nape_poly(vs):
    """a polygon as Nape keeps it: vertices with positive area, outward edge normals and projections, area,
    inertia per unit mass about the origin, centre of mass"""
    vs = clean(vs)
    if nape_area2(vs) < 0:
        vs = vs[::-1]
    n = len(vs)
    norms, projs = [], []
    for i in range(n):
        u, v = vs[i], vs[(i + 1) % n]
        dx, dy = u[0] - v[0], u[1] - v[1]
        l = math.hypot(dx, dy)
        dx, dy = dx / l, dy / l
        nx, ny = -dy, dx
        norms.append((nx, ny))
        projs.append(nx * u[0] + ny * u[1])
    s1 = s2 = 0.0
    for i in range(n):
        u, v = vs[i - 1], vs[i]
        a = cross(u[0], u[1], v[0], v[1])
        b = v[0] * v[0] + v[1] * v[1] + v[0] * u[0] + v[1] * u[1] + u[0] * u[0] + u[1] * u[1]
        s1 += a * b
        s2 += a
    area2 = nape_area2(vs)
    area = area2 / 2
    inertia = s1 / (6 * s2) if s2 else 0.0
    cx = cy = 0.0
    for i in range(n):
        v, w = vs[i], vs[(i + 1) % n]
        cf = cross(v[0], v[1], w[0], w[1])
        cx += (v[0] + w[0]) * cf
        cy += (v[1] + w[1]) * cf
    k = 1 / (3 * area2)
    return {"v": vs, "n": norms, "p": projs, "area": area, "inertia": inertia, "com": (cx * k, cy * k)}


def is_convex(vs):
    vs = clean(vs)
    n = len(vs)
    if n < 3:
        return False
    sign = 0
    for i in range(n):
        u, v, w = vs[i - 1], vs[i], vs[(i + 1) % n]
        c = cross(v[0] - u[0], v[1] - u[1], w[0] - v[0], w[1] - v[1])
        if abs(c) < 1e-9:
            continue
        s = 1 if c > 0 else -1
        if sign == 0:
            sign = s
        elif s != sign:
            return False
    return True


def _area(vs):
    a = 0.0
    for i in range(len(vs)):
        x0, y0 = vs[i - 1]
        x1, y1 = vs[i]
        a += x0 * y1 - x1 * y0
    return a / 2


def _in_tri(p, a, b, c):
    d1 = cross(b[0] - a[0], b[1] - a[1], p[0] - a[0], p[1] - a[1])
    d2 = cross(c[0] - b[0], c[1] - b[1], p[0] - b[0], p[1] - b[1])
    d3 = cross(a[0] - c[0], a[1] - c[1], p[0] - c[0], p[1] - c[1])
    return d1 >= -1e-9 and d2 >= -1e-9 and d3 >= -1e-9


def triangulate(vs):
    """ear clipping; returns triangles as index triples (counterclockwise in maths axes)"""
    vs = clean(vs)
    idx = list(range(len(vs)))
    if _area(vs) < 0:
        idx.reverse()
    tris = []
    guard = 0
    while len(idx) > 3 and guard < 10000:
        guard += 1
        n = len(idx)
        found = False
        for k in range(n):
            i0, i1, i2 = idx[k - 1], idx[k], idx[(k + 1) % n]
            a, b, c = vs[i0], vs[i1], vs[i2]
            if cross(b[0] - a[0], b[1] - a[1], c[0] - b[0], c[1] - b[1]) <= 1e-12:
                continue
            if any(_in_tri(vs[j], a, b, c) for j in idx if j not in (i0, i1, i2)):
                continue
            tris.append((i0, i1, i2))
            idx.pop(k)
            found = True
            break
        if not found:
            # not simple: cut the flattest corner anyway
            k = 0
            tris.append((idx[k - 1], idx[k], idx[(k + 1) % len(idx)]))
            idx.pop(k)
    if len(idx) == 3:
        tris.append(tuple(idx))
    return vs, tris


def convex_decompose(vs):
    """convex pieces: triangles merged across diagonals while they stay convex (Hertel-Mehlhorn)"""
    vs, tris = triangulate(vs)
    polys = [list(t) for t in tris]
    merged = True
    while merged:
        merged = False
        for i in range(len(polys)):
            for j in range(i + 1, len(polys)):
                a, b = polys[i], polys[j]
                # a shared edge: a has (p, q), b has (q, p)
                for ia in range(len(a)):
                    p, q = a[ia], a[(ia + 1) % len(a)]
                    for ib in range(len(b)):
                        if b[ib] == q and b[(ib + 1) % len(b)] == p:
                            # build merged ring: a from q around to p, then b from p around to q
                            ring = []
                            k = (ia + 1) % len(a)
                            for _ in range(len(a)):
                                ring.append(a[k])
                                k = (k + 1) % len(a)
                            # ring starts at q, ends at p; continue in b after p (skipping p) up to before q
                            k = (ib + 2) % len(b)
                            while b[k] != q:
                                ring.append(b[k])
                                k = (k + 1) % len(b)
                            pts = [vs[t] for t in ring]
                            if len(set(ring)) == len(ring) and is_convex(pts):
                                polys[i] = ring
                                polys.pop(j)
                                merged = True
                            break
                    if merged:
                        break
                if merged:
                    break
            if merged:
                break
    return [[vs[t] for t in p] for p in polys]


def convex_pieces(vs):
    vs = clean(vs)
    if len(vs) < 3:
        return []
    if is_convex(vs):
        return [vs]
    return [p for p in convex_decompose(vs) if len(clean(p)) >= 3 and abs(_area(p)) > 1e-6]


def contains(poly, p):
    """a point in a convex polygon given by nape_poly"""
    for (nx, ny), pr in zip(poly["n"], poly["p"]):
        if nx * p[0] + ny * p[1] > pr:
            return False
    return True
