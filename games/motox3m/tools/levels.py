"""Moto X3M packer: the levels. Builds each level's bodies, objects and drawings the way the game's RaceBuilder
does (motox3m4.js: GroundObject, ShaperObject, PlankObject, NapeUtil, PhysicUtil, ...), so the calculator only
has to read them."""
import json
import math
import os

from geom import js_round, nape_poly, convex_pieces, is_convex, clean, contains

TO_RAD = math.pi / 180

# the game's callback types (bits)
CB = dict(GROUND=1, SPIKES=2, SAW=4, FINISH=8, SAFE=16, TOGGLE=32, TOGGLE_OFF=64, GLASS=128, WATER=256, BOOST=512,
          GEYSER=1024, WHEEL=2048, BODY=4096, DRIVER=8192, PART=16384)
# InteractionFilter(collisionGroup, collisionMask, sensorGroup, sensorMask), as Filters
FILTERS = [
    ("DEFAULT", 1, -1, 1, -1),
    ("NOTHING", 0, 0, 0, 0),
    ("SENSOR", 0, 0, 1, 4369),
    ("WITH_EARTH_ONLY", 1, 4369, 1, -1),
    ("WHEEL", 4096, 272, 17, 4369),
    ("DRIVER_PART", 4096, 272, 1, -1),
    ("GROUND", 17, 4369, 1, 4369),
    ("PLAYER_ONLY", 256, 4096, 1, 4369),
]
F = {f[0]: i for i, f in enumerate(FILTERS)}
F["TOGGLE"] = F["SENSOR"]
F["WATER"] = F["SENSOR"]
F["BODY_COLLIDER"] = F["WHEEL"]
# Material(elasticity, dynamicFriction, staticFriction, density, rollingFriction)
MAT_DEFAULT = (0.0, 1.0, 2.0, 1.0, 0.001)

# object kinds (the C side's O_*)
O = dict(GROUND=0, SHAPER=1, PLANK=2, DYN=3, REMOVED=4, TNT=5, SPIKES=6, TOGGLE=7, SAFE=8, FINISH=9, MOVER=10,
         MOTOR=11, PIVOT=12, DECOR=13, WATER=14, GEYSER=15, BOOST=16, GLASS=17)
# drawing containers, back to front (MaxGameView): shapes, landscape, objects, front
C_SHAPES, C_LAND, C_OBJ, C_FRONT = 0, 1, 2, 3
# drawing kinds
D_POLY, D_STRIP, D_SPRITE, D_WATER = 0, 1, 2, 3

BG = [["Bg0_0", "Bg1_0", "Bg2_0"], ["Bg0_1", None, None], ["Bg0_2", None, "Bg2_2"]]
ROAD_LINE = ["line", "RoadLine0_1", "RoadLine1_4", "RoadLine0_3", "RoadLine0_4", "Plank0", "RoadLine0_0", "RoadLine0_7",
             "RoadLine0_8", "line_ivy", "UnLine0", "UnLine1", "LineChain", "CandyGround", "RoadLine0_Bg"]
PILLAR_LINE = ["RoadPillar0_0", "RoadPillar0_1", "RoadPillar0_2"]
DECORS_ALL = ["Dec1", "Dec2", "Dec3", "Dec4", "Dec5", "Dec6", "Dec7", "Dec8", "Dec9", "Dec10", "Dec11", "Dec12", "Dec13",
              "Dec14", "Dec15", "Dec16", "Dec17", "Dec18", "Dec19", "Dec29", "Sign1", "Sign2", "Sign15", "Sign21",
              "SignPillar", "PillarTop0_0", "PillarTop0_1", "PillarTop0_2", "PillarBot0_0", "PillarBot0_1",
              "PillarBot0_2"]
DECORS = [DECORS_ALL, DECORS_ALL, DECORS_ALL[12:16] + DECORS_ALL[19:]]


def load(path):
    with open(path, encoding="utf-8-sig") as f:
        return json.load(f)


def jsmod(a, b):
    return math.fmod(a, b)


def rot(p, a):
    c, s = math.cos(a), math.sin(a)
    return (p[0] * c - p[1] * s, p[0] * s + p[1] * c)


def normalize_vertices(params):
    """Util.normalizeVertices: rounded vertices turned about the object's origin, in world coordinates"""
    x, y, r = params["x"], params["y"], TO_RAD * params["rotation"]
    out = []
    for v in params["vertices"]:
        d, p = js_round(v["x"]), js_round(v["y"])
        if d == 0 and p == 0:
            out.append({"x": x, "y": y})
        else:
            f = math.atan2(p, d) + r
            l = math.hypot(d, p)
            out.append({"x": x + math.cos(f) * l, "y": y + math.sin(f) * l})
    q = dict(params)
    q["vertices"] = out
    q["x"] = q["y"] = 0
    q["rotation"] = 0
    return q


class Body:
    def __init__(self, kind, x, y, cb=0):
        self.kind = kind          # "static", "dynamic", "kinematic"
        self.x, self.y, self.rot = float(x), float(y), 0.0
        self.shapes = []          # dicts: type poly/circle, v (local) or r + c, filter, mat, sensor, fluid
        self.cb = cb
        self.space = True
        self.norot = False
        self.fluid = None         # (viscosity, density)
        self.index = -1
        self.ground = False       # userData.isGround: the bike's wheels and frame count it as ground

    def poly(self, verts, mat, flt, sensor=False):
        for piece in convex_pieces(verts):
            self.shapes.append({"type": "poly", "v": piece, "mat": mat, "filter": flt, "sensor": sensor})

    def circle(self, r, mat, flt, sensor=False, c=(0.0, 0.0)):
        self.shapes.append({"type": "circle", "r": r, "c": c, "mat": mat, "filter": flt, "sensor": sensor})

    def mass_com(self):
        m = cx = cy = 0.0
        for s in self.shapes:
            if s["type"] == "poly":
                p = nape_poly(s["v"])
                a, c = p["area"], p["com"]
            else:
                a, c = math.pi * s["r"] ** 2, s["c"]
            d = a * s["mat"][3]
            m += d
            cx += c[0] * d
            cy += c[1] * d
        return m, ((cx / m, cy / m) if m else (0.0, 0.0))

    def align(self):
        """Body.align: the origin moves to the centre of mass"""
        m, (cx, cy) = self.mass_com()
        if not m:
            return
        for s in self.shapes:
            if s["type"] == "poly":
                s["v"] = [(v[0] - cx, v[1] - cy) for v in s["v"]]
            else:
                s["c"] = (s["c"][0] - cx, s["c"][1] - cy)
        w = rot((cx, cy), self.rot)
        self.x += w[0]
        self.y += w[1]

    def rotate(self, cx, cy, a):
        """Body.rotate(centre, angle)"""
        d = rot((self.x - cx, self.y - cy), a)
        self.x, self.y = cx + d[0], cy + d[1]
        self.rot += a

    def to_world(self, p):
        q = rot(p, self.rot)
        return (self.x + q[0], self.y + q[1])

    def world_shapes(self):
        out = []
        for s in self.shapes:
            if s["type"] == "poly":
                out.append(nape_poly([self.to_world(v) for v in s["v"]]))
            else:
                c = self.to_world(s["c"])
                out.append({"circle": True, "c": c, "r": s["r"]})
        return out

    def contains(self, pt):
        for s in self.world_shapes():
            if s.get("circle"):
                if (pt[0] - s["c"][0]) ** 2 + (pt[1] - s["c"][1]) ** 2 < s["r"] ** 2:
                    return True
            elif contains(s, pt):
                return True
        return False


def num(v, default=0.0):
    """a parameter as JavaScript would use it in arithmetic (some are strings)"""
    if v is None or v == "":
        return default
    try:
        return float(v)
    except (TypeError, ValueError):
        return default


def mat_with(density=None, base=MAT_DEFAULT):
    m = list(base)
    if density is not None:
        m[3] = num(density, 1.0)
    return tuple(m)


class Level:
    def __init__(self, n, data, objects, warn):
        self.n = n
        self.data = data
        self.settings = data["settings"]
        self.theme = self.settings["theme"] % 3
        self.objects_json = objects
        self.warn = warn
        self.bodies = []
        self.objs = []
        self.draws = []
        self.cams = []
        self.start = (360.0, 240.0)
        self.finish_x = 0.0
        self.images = set()
        self.prizes = [float(x) for x in self.settings["prizes"].split(",")]
        for li, layer in enumerate(data["layers"]):
            for o in layer or []:
                self.parse(o, li)

    # ---- helpers ----
    def body(self, kind, x, y, cb=0):
        b = Body(kind, x, y, cb)
        b.index = len(self.bodies)
        self.bodies.append(b)
        return b

    def obj(self, kind, **kw):
        o = {"kind": O[kind], "flags": 0, "id": -1, "id2": -1, "body": None, "body2": None, "x": 0.0, "y": 0.0,
             "rot": 0.0, "p": [0.0] * 8, "aux": [], "img": None, "cname": ""}
        o.update(kw)
        o["index"] = len(self.objs)
        self.objs.append(o)
        return o

    def draw(self, kind, cont, owner, img, pts=None, p=None, flags=0, back=False):
        if img is not None:
            self.images.add(img)
        d = {"kind": kind, "cont": cont, "owner": owner, "img": img, "pts": pts or [], "p": p or [0.0] * 6,
             "flags": flags, "back": back, "order": len(self.draws)}
        self.draws.append(d)
        return d

    def rect_body(self, kind, flt, sensor, w, h, x, y, rot_rad, cb, mat=MAT_DEFAULT):
        b = self.body(kind, x, y, cb)
        b.rot = rot_rad
        b.poly([(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)], mat, flt, sensor)
        return b

    def poly_body(self, params, mat, flt, kind):
        """NapeUtil.createPolyBody"""
        b = self.body(kind, params["x"], params["y"])
        b.poly([(v["x"], v["y"]) for v in params["vertices"]], mat, flt)
        return b

    def pather_body(self, params, mat, flt, thick):
        """NapeUtil.createPatherBody: a quad per segment, joints mitred"""
        b = self.body("static" if params.get("isStatic") else "dynamic", params["x"], params["y"])
        p = params["vertices"]
        y = len(p)
        s = thick
        a = [[0, 0], [0, 0], [0, 0], [0, 0]]
        for f in range(y - 1):
            lx, ly = p[f + 1]["x"] - p[f]["x"], p[f + 1]["y"] - p[f]["y"]
            if f == 0:
                g = math.atan2(ly, lx) + 0.5 * math.pi
                a[0] = [p[f]["x"] - 0.5 * s * math.cos(g), p[f]["y"] - 0.5 * s * math.sin(g)]
                a[1] = [p[f]["x"] + 0.5 * s * math.cos(g), p[f]["y"] + 0.5 * s * math.sin(g)]
            else:
                a[0] = list(a[3])
                a[1] = list(a[2])
            if f == y - 2:
                g = math.atan2(ly, lx) + 0.5 * math.pi
                a[3] = [p[f + 1]["x"] - 0.5 * s * math.cos(g), p[f + 1]["y"] - 0.5 * s * math.sin(g)]
                a[2] = [p[f + 1]["x"] + 0.5 * s * math.cos(g), p[f + 1]["y"] + 0.5 * s * math.sin(g)]
            else:
                ll = math.hypot(lx, ly)
                lx, ly = -lx / ll, -ly / ll
                ux, uy = p[f + 2]["x"] - p[f + 1]["x"], p[f + 2]["y"] - p[f + 1]["y"]
                ul = math.hypot(ux, uy)
                ux, uy = ux / ul, uy / ul
                g = math.atan2(ly + uy, lx + ux)
                a[3] = [p[f + 1]["x"] - 0.5 * s * math.cos(g), p[f + 1]["y"] - 0.5 * s * math.sin(g)]
                a[2] = [p[f + 1]["x"] + 0.5 * s * math.cos(g), p[f + 1]["y"] + 0.5 * s * math.sin(g)]
            q = [tuple(v) for v in a]
            if not is_convex(q):
                q = [q[0], q[1], q[3], q[2]]
            for piece in convex_pieces(q):
                b.shapes.append({"type": "poly", "v": piece, "mat": mat, "filter": flt, "sensor": False})
        b.align()
        return b

    def ground_body(self, params):
        """PhysicUtil.createGroundBody: convex runs of the ground line, thick downwards"""
        x, y = params["x"], params["y"]
        n = params["vertices"]
        u = float("nan")
        d = float("nan")
        thick = params.get("thick") if params.get("thick") is not None else 128
        f = params.get("direction", 0) * TO_RAD
        directed = params.get("directed")
        b = self.body("static", x, y, CB["GROUND"])

        def add(run, ua, la):
            pts = []
            for k, g in enumerate(run):
                if k == 0:
                    ang = f if directed else ua
                    pts.append((g["x"] + math.cos(ang) * thick, g["y"] + math.sin(ang) * thick))
                pts.append((g["x"], g["y"]))
                if k == len(run) - 1:
                    ang = f if directed else la
                    pts.append((g["x"] + math.cos(ang) * thick, g["y"] + math.sin(ang) * thick))
            if is_convex(pts):
                b.shapes.append({"type": "poly", "v": clean(pts), "mat": MAT_DEFAULT, "filter": F["GROUND"],
                                 "sensor": False})
            else:
                for piece in convex_pieces(pts):
                    b.shapes.append({"type": "poly", "v": piece, "mat": MAT_DEFAULT, "filter": F["GROUND"],
                                     "sensor": False})

        v = [n[0]]
        xang = 0.0
        for k in range(1, len(n)):
            bb = n[k]
            w = v[-1]
            if len(v) == 1:
                v.append(bb)
                xang = math.atan2(bb["y"] - w["y"], bb["x"] - w["x"])
                if math.isnan(u):
                    u = xang + 0.5 * math.pi
            else:
                c = math.atan2(bb["y"] - w["y"], bb["x"] - w["x"])
                da = c - xang
                if da > math.pi:
                    da -= 2 * math.pi
                elif da < -math.pi:
                    da += 2 * math.pi
                if da < 0:
                    if not directed:
                        tx, ty = math.cos(xang) - math.cos(c), math.sin(xang) - math.sin(c)
                        d = math.atan2(ty, tx)
                    add(v, u, d)
                    u = d
                    v = [w, bb]
                else:
                    v.append(bb)
                xang = c
        add(v, u, xang + 0.5 * math.pi)
        b.ground = True
        if not b.shapes:
            self.bodies.pop()
            return None
        return b

    def under(self, pt):
        return [b for b in self.bodies if b.space and b.contains(pt)]

    def sprite(self, cont, owner, img, x, y, rot_rad, sx=1.0, sy=1.0, frames=1, back=False, flags=0):
        return self.draw(D_SPRITE, cont, owner, img, p=[x, y, rot_rad, sx, sy, frames], back=back, flags=flags)

    def strip(self, cont, owner, img, pts, rope, back=False):
        if len(pts) < 2:
            return
        self.draw(D_STRIP, cont, owner, "strip:" + img, pts=pts, p=[1.0 if rope else 0.0, 0, 0, 0, 0, 0], back=back)

    # ---- objects ----
    def parse(self, o, li):
        cname = o["className"].split("::")[-1]
        p = o["params"]
        th = self.theme
        if cname == "PlayerWP":
            self.start = (p["x"], p["y"])
            return
        if "GroundPather" in cname:
            self.ground(p, cname)
        elif "LandscapeShaper" in cname or "FinishShaper" in cname:
            self.shaper(p, "Finish" in cname)
        elif "WaterShaper" in cname:
            self.water(p)
        elif "GeyserE" in cname:
            self.geyser(p)
        elif "MoverPather" in cname:
            self.mover(p)
        elif "DynamicPather" in cname or "PillarPather" in cname:
            self.pather(p, "PillarPather" in cname)
        elif cname.startswith("Glass"):
            self.glass(p, cname)
        elif cname.startswith("Dec") or cname.startswith("Sign") or cname.startswith("Pillar"):
            self.decor(p, cname, li)
        elif cname in ("BarrelE", "Plank0"):
            self.dyn(p, cname, "DYN")
        elif cname == "ToggleE":
            b = self.rect_body("static", F["TOGGLE"], True, p["width"], p["height"], p["x"], p["y"],
                               TO_RAD * p["rotation"], CB["TOGGLE"] | (CB["TOGGLE_OFF"] if p.get("off") else 0))
            self.obj("TOGGLE", id=p.get("id", -1), id2=p.get("id_off", -1) if p.get("id_off") else -1, body=b,
                     flags=1 if p.get("off") else 0, p=[float(p.get("sndId", -1) if p.get("sndId") is not None else -1),
                                                        0, 0, 0, 0, 0, 0, 0])
        elif cname == "BoostE":
            b = self.body("static", p["x"], p["y"], CB["BOOST"])
            b.circle(32, MAT_DEFAULT, F["SENSOR"], True)
            ob = self.obj("BOOST", body=b, x=p["x"], y=p["y"], p=[p["time"], p["force"], 0, 0, 0, 0, 0, 0])
            self.sprite(C_OBJ, ob, "boost_anim", p["x"], p["y"], 0, frames=10)
        elif cname == "SafePointE":
            b = self.rect_body("static", F["SENSOR"], True, p["width"], p["height"], p["x"], p["y"],
                               TO_RAD * p["rotation"], CB["SAFE"])
            ob = self.obj("SAFE", body=b, x=p["x"], y=p["y"], id=p.get("id", -1))
            self.sprite(C_OBJ, ob, "SafePointAnim", p["x"], p["y"], TO_RAD * p["rotation"], frames=2)
        elif cname == "MotorJointE":
            self.motor(p)
        elif cname == "PivotJointE":
            self.pivot(p)
        elif cname == "SpikesE":
            d = self.objects_json["SpikesE"]
            b = self.rect_body("static", F["GROUND"], False, d["w"], d["h"], p["x"], p["y"], TO_RAD * p["rotation"],
                               CB["SPIKES"])
            ob = self.obj("SPIKES", body=b)
            self.sprite(C_OBJ, ob, "SpikesE", p["x"], p["y"], TO_RAD * p["rotation"])
        elif cname.startswith("Removed"):
            self.dyn(p, cname, "REMOVED")
        elif cname.startswith("Tnt"):
            self.tnt(p, cname)
        elif cname == "FinishZone":
            b = self.rect_body("static", F["SENSOR"], True, p["width"], p["height"], p["x"], p["y"],
                               TO_RAD * p["rotation"], CB["FINISH"])
            ob = self.obj("FINISH", body=b)
            w, h = p["width"], p["height"]
            pts = [(p["x"] - w / 2, p["y"] - h / 2), (p["x"] + w / 2, p["y"] - h / 2), (p["x"] + w / 2, p["y"] + h / 2),
                   (p["x"] - w / 2, p["y"] + h / 2)]
            self.draw(D_POLY, C_LAND, None, "finish", pts=pts, p=[p["x"] - w / 2, p["y"] - h / 2, 0, 0, 0, 0])
            self.finish_x = p["x"]
        elif cname in ("EggE", "OvalTemplate"):
            pass
        else:
            self.warn("level %d: %s left out" % (self.n, cname))

    def ground(self, p, cname):
        th = self.theme
        if p.get("rotation", 0) != 0:
            p = normalize_vertices(p)
        line_id = p.get("lineId", 0)
        cont = C_LAND
        if line_id == 3 and th == 0:
            cont = C_FRONT
        elif line_id == 3 and th == 1:
            cont = C_OBJ
        if p.get("physic") is True:
            self.ground_body(p)
        vs = p["vertices"]
        x0, y0 = vs[0]["x"], vs[0]["y"]
        m = [(v["x"] - x0, v["y"] - y0) for v in vs]
        vmax = 0.0
        for q in m:
            if q[1] > vmax:
                vmax = q[1]
        ox, oy = p["x"] + x0, p["y"] + y0
        if p.get("shape") is True or p.get("line") is True:
            if p.get("shape") is True:
                tex = "finish" if line_id == 100 else "tile%d" % th
                gx = min(q[0] for q in m)
                gy = min(q[1] for q in m)
                poly = [(m[0][0], vmax + 480)] + list(m) + [(m[-1][0], vmax + 480)]
                pts = [(ox + q[0], oy + q[1]) for q in poly]
                self.draw(D_POLY, cont, None, tex, pts=pts, p=[gx, gy, 0, 0, 0, 0])
            if p.get("line") is True:
                key, rope = None, False
                if line_id == 0:
                    key, rope = "line%d" % th, True
                elif line_id == 1:
                    key, rope = ("line_top_%d" % th, True) if th > 0 else ("line_wood_g", False)
                elif line_id == 2:
                    key = None   # "line_bg" is not in the atlas
                elif line_id == 3:
                    key = "line_metal_g"
                if key:
                    self.strip(cont, None, key, [(ox + q[0], oy + q[1]) for q in m], rope)
        if p.get("camera") is True:
            off = num(p.get("cameraOffsetY", 0))
            self.cams.append(([(ox + q[0], oy + q[1]) for q in m], off))

    def shaper(self, e, finish):
        th = self.theme
        layer = e.get("layer", 0)
        cont = C_SHAPES if finish or layer == 1 else C_LAND
        n_, y_ = e["x"], e["y"]
        body = None
        ob = self.obj("SHAPER", x=n_, y=y_)
        if not finish:
            ob["id"] = e.get("id", -1) if e.get("id") is not None else -1
            safe = e.get("safeId", -1)
            if safe is not None and safe <= -10:
                ob["flags"] |= 2      # skipRevive
            if e.get("rem") is False:
                ob["flags"] |= 1      # appears when toggled instead of going away
        is_static = finish or e.get("isStatic")
        if finish or e.get("physic") is True:
            if finish:
                mat = MAT_DEFAULT
            elif num(e.get("density", 1)) == 1:
                mat = MAT_DEFAULT
            else:
                mat = mat_with(e["density"])
            if finish:
                flt = F["SENSOR"]
            elif e.get("plr") is True:
                flt = F["PLAYER_ONLY"]
            elif e.get("ignore") is True:
                flt = F["NOTHING"]
            else:
                flt = F["GROUND"]
            body = self.poly_body(e, mat, flt, "static" if is_static else "dynamic")
            body.cb = CB["GROUND"]
            body.ground = True
            if finish:
                for s in body.shapes:
                    s["sensor"] = True
                body.cb |= CB["FINISH"]
            else:
                if layer == 3:
                    body.cb |= CB["SAW"]
                if layer == 9:
                    body.cb |= CB["SPIKES"]
            body.align()
            ob["body"] = body
        bx, by = (n_ - body.x, y_ - body.y) if body else (0.0, 0.0)
        if finish or e.get("graphic") is True:
            tex = "tile_stone" if th == 1 else "RoadTile0_1"
            if finish:
                tex = "finish"
            elif layer == 1:
                tex = "tile_bg%d" % th
            elif layer == 2:
                tex = "tile%d" % th
            elif layer == 3:
                tex = "tile_saw"
            elif layer == 4:
                tex = "tile_rivet"
            elif layer == 5:
                tex = "tile_rivet_bg"
            elif layer == 6:
                tex = "tile_stone"
            line = None
            if not finish:
                if layer == 0:
                    if th == 1:
                        line, tex = "line_stone_shape", "tile_stone"
                    else:
                        line = "RoadLine0_4"
                elif layer == 1:
                    line = "RoadLine0_Bg%d" % th
                elif layer == 2:
                    if e.get("line") is True:
                        line = "line_top1"
                elif layer == 3:
                    line = "UnLine0"
                elif layer == 4:
                    line = "line_rivet"
                elif layer == 6:
                    line = "line_stone_shape"
            d = e["vertices"]
            local = [(v["x"] + bx, v["y"] + by) for v in d]
            mx = min(q[0] for q in local)
            my = min(q[1] for q in local)
            owner = ob
            # the texture's origin, in the drawing's own coordinates
            self.draw(D_POLY, cont, owner, tex, pts=local, p=[mx - jsmod(bx, 256), my - jsmod(by, 256), 0, 0, 0, 0])
            if line:
                outline = list(local)
                if abs(d[0]["x"] - d[-1]["x"]) > 4 or abs(d[0]["y"] - d[-1]["y"]) > 4:
                    mid = (0.5 * (d[0]["x"] + d[-1]["x"]) + bx, 0.5 * (d[0]["y"] + d[-1]["y"]) + by)
                    outline = [mid] + outline + [mid]
                self.strip(cont, owner, line, outline, "Bg" in line)
        if body:
            if e.get("rotation", 0) != 0:
                body.rotate(n_, y_, TO_RAD * e["rotation"])
            ob["x"], ob["y"], ob["rot"] = body.x, body.y, body.rot
        else:
            ob["x"], ob["y"], ob["rot"] = n_, y_, TO_RAD * e.get("rotation", 0)
        if body and e.get("isWheel"):
            ob["flags"] |= 4
            body.is_wheel = True

    def pather(self, e, pillar):
        if e.get("rotation", 0) != 0:
            e = normalize_vertices(e)
        t = e.get("type", 0)
        ob = self.obj("PLANK", x=e["x"], y=e["y"])
        body = None
        w = P = 0.0
        if e.get("physic"):
            c = e.get("shapeH")
            if t in (4, 5, 6):
                c = 20
            flt = F["NOTHING"] if t in (3, 7) else F["GROUND"]
            body = self.pather_body(e, mat_with(e.get("density", 1)), flt, c)
            body.cb = CB["SPIKES"] if t in (4, 5, 6) else CB["GROUND"]
            body.ground = True
            w, P = e["x"] - body.x, e["y"] - body.y
            ob["body"] = body
            ob["x"], ob["y"] = body.x, body.y
        if pillar or e.get("graphic"):
            if pillar:
                key = PILLAR_LINE[t] if t < len(PILLAR_LINE) else None
            elif t in (4, 5, 6):
                key = ROAD_LINE[9]
            else:
                key = ROAD_LINE[t + 5] if t + 5 < len(ROAD_LINE) else None
            if key == "line":
                key = None
            if key:
                pts = [(v["x"] + w, v["y"] + P) for v in e["vertices"]]
                if not body:
                    pts = [(q[0] + e["x"], q[1] + e["y"]) for q in pts]
                self.strip(C_LAND, ob if body else None, key, pts, False)

    def decor(self, p, cname, li):
        th = self.theme
        e = cname
        dup = e.find("_dup")
        if dup >= 0:
            e = e[:dup]
        if e not in DECORS[th]:
            return
        cont = C_LAND if li <= 1 else C_FRONT
        back = False
        if th in (0, 2):
            cont, back = C_LAND, True
            if cname.startswith("Dec") and int(cname[3:].split("_")[0]) <= 7:
                cont, back = C_FRONT, False
        if e.startswith("Sign") and e != "SignPillar":
            cont, back = C_LAND, False
        ob = None
        if p.get("id") is not None and p.get("id", -1) >= 0:
            ob = self.obj("DECOR", id=p["id"])
        sx = p.get("scaleX", 1) if p.get("scaleX") is not None else 1
        sy = p.get("scaleY", 1) if p.get("scaleY") is not None else 1
        frames = 22 if e == "Dec12" else 1
        self.sprite(cont, ob, e, p["x"], p["y"], TO_RAD * p.get("rotation", 0), sx, sy, frames, back,
                    flags=1 if e == "Dec12" else 0)

    def dyn(self, p, cname, kind):
        d = self.objects_json[cname]
        if d["shape"] == "circ":
            b = self.body("dynamic", p["x"], p["y"])
            b.rot = TO_RAD * p["rotation"]
            b.circle(0.5 * d["w"], MAT_DEFAULT, F["GROUND"])
        else:
            b = self.rect_body("dynamic", F["GROUND"], False, d["w"], d["h"], p["x"], p["y"], TO_RAD * p["rotation"],
                               0)
        if cname.startswith("Plank"):
            b.ground = True
        ob = self.obj(kind, body=b, x=b.x, y=b.y, rot=b.rot, id=p.get("id", -1) if p.get("id") is not None else -1)
        if p.get("active") is False:
            ob["flags"] |= 1
        self.sprite(C_OBJ, ob, cname, 0, 0, 0)

    def tnt(self, p, cname):
        d = self.objects_json[cname]
        b = None
        if p.get("physic"):
            b = self.rect_body("dynamic", F["WITH_EARTH_ONLY"], False, d["w"], d["h"], p["x"], p["y"],
                               TO_RAD * p["rotation"], 0)
        ob = self.obj("TNT", body=b, x=p["x"], y=p["y"], rot=TO_RAD * p["rotation"],
                      id=p.get("id", -1) if p.get("id") is not None else -1,
                      p=[float(p.get("impulse", 0) or 0), float(p.get("count", 0) or 0), float(p.get("radius", 0) or 0),
                         0, 0, 0, 0, 0])
        if b:
            self.sprite(C_OBJ, ob, cname, 0, 0, 0)
        else:
            self.sprite(C_OBJ, ob, cname, p["x"], p["y"], TO_RAD * p["rotation"])

    def glass(self, p, cname):
        d = self.objects_json[cname]
        b = self.rect_body("static", F["GROUND"], False, d["w"], d["h"], p["x"], p["y"], TO_RAD * p["rotation"],
                           CB["GLASS"] | CB["GROUND"])
        ob = self.obj("GLASS", body=b, x=p["x"], y=p["y"], rot=TO_RAD * p["rotation"],
                      p=[float(p.get("ttl", 0) or 0), d["w"], d["h"], 1.0 if cname == "GlassCrashed0" else 0.5, 0, 0,
                         0, 0], cname=cname)
        self.sprite(C_OBJ, ob, cname, p["x"], p["y"], TO_RAD * p["rotation"])
        for k in range(6):
            self.images.add("GlassCrashed0_%d" % k)

    def water(self, p):
        b = self.poly_body(p, MAT_DEFAULT, F["WATER"], "static")
        b.cb = CB["WATER"] | (CB["SAW"] if p.get("layer") == 3 else 0)
        b.fluid = (num(p.get("viscosity"), 1.0), num(p.get("density"), 1.0))
        b.align()
        n_, y_ = p["x"], p["y"]
        bx, by = n_ - b.x, y_ - b.y
        if p.get("rotation", 0) != 0:
            b.rotate(n_, y_, TO_RAD * p["rotation"])
        ob = self.obj("WATER", body=b, x=b.x, y=b.y, rot=b.rot)
        pts = [(v["x"] + bx, v["y"] + by) for v in p["vertices"]]
        if self.theme == 0:
            self.draw(D_WATER, C_SHAPES, ob, None, pts=pts, p=[1, 0, 0, 0, 0, 0])
        self.draw(D_WATER, C_LAND, ob, None, pts=pts)

    def geyser(self, p):
        b = self.rect_body("static", F["SENSOR"], True, p["width"], p["height"], p["x"], p["y"],
                           TO_RAD * p["rotation"], CB["GEYSER"])
        r = b.rot
        ob = self.obj("GEYSER", body=b, x=p["x"], y=p["y"], rot=r, id=p.get("id", -1),
                      p=[p["speed"] * math.cos(r), p["speed"] * math.sin(r), p["width"], p["height"],
                         float(p.get("rate", 1) if p.get("rate") is not None else 1), p["speed"], 0, 0])
        self.images.add("GeyserBase")
        self.images.add("geyser_anim")
        self.images.add("particle_boost")

    def mover(self, p):
        ob = self.obj("MOVER", id=p.get("id", -1), x=p["x"], y=p["y"])
        if p.get("body"):
            b = self.body("kinematic", 0, 0)
            b.circle(16, MAT_DEFAULT, F["NOTHING"])
        else:
            cands = [b for b in self.under((p["x"], p["y"])) if b.kind == "dynamic"]
            if not cands:
                self.warn("level %d: a mover with nothing to move" % self.n)
                self.objs.pop()
                return
            if len(cands) > 1:
                self.warn("level %d: a mover over %d bodies" % (self.n, len(cands)))
            b = cands[-1]
            b.kind = "kinematic"
        ob["body"] = b
        b.norot = True
        pts = [(p["x"] + v["x"], p["y"] + v["y"]) for v in p["vertices"]]
        ob["aux"] = pts
        accel = p.get("accelDist") or 0
        fl = 0
        if p.get("cycle") == 1:
            fl |= 1
        if p.get("active") == 1:
            fl |= 2
        if p.get("stopOnEnd") is True:
            fl |= 4
        if p.get("stopOnPoint") is True:
            fl |= 8
        ob["flags"] = fl
        ob["p"] = [float(p["speed"]), float(accel), float(p.get("startTime", 0) or 0),
                   float(p.get("sideWait", 0) or 0), float((p.get("sideAngle", 0) or 0) * TO_RAD),
                   float(p.get("position", 0) or 0), 0, 0]

    def motor(self, p):
        pt = (p["x"], p["y"])
        under = self.under(pt)
        s = None
        for b in reversed(under):
            if b.kind == "dynamic":
                s = b
                break
        if not s:
            self.warn("level %d: a motor over nothing" % self.n)
            return
        f = None
        for b in reversed(under):
            if b is s or (b.cb & CB["TOGGLE"]):
                continue
            f = b
            break
        ob = self.obj("MOTOR", id=p.get("id", -1), x=p["x"], y=p["y"])
        if f is not None and getattr(f, "is_wheel", False):
            f, s = s, f
        s.norot = True
        ob["body"] = s       # body2: the one that turns
        ob["body2"] = f      # body1: the other, or the world (a static body: its collisions with s are ignored)
        ob["p"] = [p.get("rate", 0) * TO_RAD, float(p.get("time", 0) or 0), 0, 0, 0, 0, 0, 0]
        g = p.get("graphic") or 0
        if g > 0:
            ob["img"] = "Motor%d" % (g - 1)
            self.images.add(ob["img"])

    def pivot(self, p):
        pt = (p["x"], p["y"])
        under = list(reversed(self.under(pt)))
        c = u = None
        for b in under:
            if b.kind == "dynamic":
                if c is None:
                    c = b
                elif u is None:
                    u = b
                    break
            elif u is None:
                u = b
        if c is None:
            return
        if u is not None and u.kind == "static":
            u = None   # the world
        ob = self.obj("PIVOT", id=p.get("id", -1), x=p["x"], y=p["y"], body=c, body2=u)
        g = p.get("graphic") or 0
        if g > 0:
            ob["img"] = "Pivot%d" % (g - 1)
            self.images.add(ob["img"])

