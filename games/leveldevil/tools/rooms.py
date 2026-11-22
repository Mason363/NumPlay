#!/usr/bin/env python3
"""Level Devil's 80 rooms, as the game has them.

tools/scenes.json (written by tools/fetch.py from the game's own files) has each room's tiles and
actors: where they are, their type, scale and angle. What the traps do is in the game's scene
scripts (a script a room: what each trigger sets off, and when); they are written out below, room
by room, with the game's numbers: positions in the scene's pixels (16 a tile, the screen showing
x 50 to 477 and y 0 to 240), times in milliseconds, speeds in its units (10 pixels a second).
This writes OUT/rooms.inc, the rooms packed for src/main.c.

A room, packed:
  its modes (R_*) and where the runner starts (x, y: the corner of its 16 x 16 picture),
  its ground: 33 columns of 15 tiles, a bit a tile,
  its objects, in spans (so many alike in a row, 16 pixels apart, across or down): kind (K_*) and
    turn, flags (F_*), the event a trigger sets off, the first one's centre x and y, half its width
    and height (pixels) and how many,
  its script: steps (event, frame, op, the first object and how many, which times of the event,
    three numbers, and how many 256ths of a step later each next object goes), in order of event
    then time. Event 0 is the room starting; 1 to 8 its triggers (the runner touching a
    Trigger_N); EV_KEY the jump key (once a step has armed it), EV_MOVE the first step taken.
All of it is written column by column: each field of all the rooms, then the next.

Usage: rooms.py OUT
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
FPS = 60

# kinds, flags, modes, ops (src/main.c)
(K_BLOCK, K_SPIKE, K_SAW, K_SPRING, K_DOOR, K_TRIG, K_COIN, K_BAD, K_WARP, K_BUTTON, K_THRU, K_GSPIKE,
 K_DEATH, K_SIGN) = range(14)
F_HIDE, F_GHOST, F_OFF, F_BIG, F_USED, F_ICE = 1, 2, 4, 8, 16, 32
R_WRAP, R_WRAPY, R_SWAP, R_FLAPPY, R_GRAVITY = 1, 2, 4, 16, 32
(O_VEL, O_MOVE, O_SET, O_GROW, O_FLAG, O_SPAWN, O_WARP, O_HERO, O_GRAV, O_HGROW, O_SHAKE, O_FIRE, O_REARM,
 O_TOWARD, O_RULE, O_ACCEL, O_HOP, O_KEY) = range(18)
# O_HERO: what of the runner (speeds in pixels a second)
H_RUN, H_JUMP, H_MAXUP, H_MAXDOWN, H_BACK, H_DOWNJUMP, H_DIR, H_SLIDE, H_PUSHX = range(9)
# O_RULE: what an object does every step until told otherwise
U_NONE, U_PATROL, U_FOLLOW, U_STOPX, U_WRAPX = range(5)
EV_KEY, EV_MOVE = 9, 10
NOBODY = 255

WORLDS = ["Pits", "Spikes", "Push", "Coins", "Controls", "Platforms", "Springs", "Warps", "Scale", "Doors", "Saws",
          "Flappy", "Gravity", "Movement", "Wraparound", "Boss"]
FAKE = [600, 645, 647, 649, 651, 653, 655, 657]           # Fake_Tile_1 to 8 (Enemy_Fake_Tile is a 600 too)
TRIGGER = [602, 604, 615, 617, 665, 667, 673, 675]        # Trigger_1 to 8
KINDS = {609: K_SPIKE, 684: K_SAW, 621: K_SPRING, 598: K_DOOR, 613: K_COIN, 659: K_BAD, 619: K_WARP, 661: K_BUTTON,
         562: K_THRU, 698: K_GSPIKE, 846: K_DEATH, 682: K_DOOR, 766: K_SIGN}
SKIP = {596, 682}                                         # blood, the door's double (Doors_5)
SCENES = {r["name"]: r for r in json.load(open(os.path.join(HERE, "scenes.json")))}


def frames(ms):
    return int(round(ms * FPS / 1000))


class Obj:
    def __init__(self, kind, x, y, sx=1.0, sy=1.0, angle=0, typ=None, name="", ev=0):
        self.kind, self.x, self.y, self.sx, self.sy, self.angle = kind, x, y, sx, sy, angle
        self.typ, self.name, self.ev = typ, name, ev
        self.flags, self.back, self.index, self.made = 0, False, None, False
        self.iw, self.ih = {K_GSPIKE: (16, 26), K_SIGN: (50, 31)}.get(kind, (16, 16))  # (the sign: the arrow keys)

    @property
    def cx(self):
        return self.x + self.iw / 2

    @property
    def cy(self):
        return self.y + self.ih / 2

    def __repr__(self):
        return f"{self.name}@{self.x},{self.y}"


def lst(o):
    return list(o) if isinstance(o, (list, tuple)) else [o]


def flat(ops):
    out = []
    for o in ops:
        if isinstance(o, (list, tuple)):
            out += flat(o)
        else:
            out.append(o)
    return out


# ------------------------------------------------------------ steps of a script
class Op:
    def __init__(self, code, obj=None, a=0, b=0, c=0):
        self.code, self.obj, self.a, self.b, self.c, self.t, self.cond = code, obj, a, b, c, 0, 0


def at(ms, *ops):
    out = flat(ops)
    for p in out:
        p.t += ms
    return out


def when(cond, ops):
    out = flat(ops)
    for p in out:
        p.cond = cond
    return out


def nth(n, *ops):  # only the n-th time the event goes off
    return when(n, ops)


def ifev(ev, *ops):  # only if event ev has gone off (by then)
    return when(0x10 | ev, ops)


def ifnot(ev, *ops):  # only if it has not
    return when(0x20 | ev, ops)


def vel(objs, vx, vy):
    """setXVelocity and setYVelocity (the game's units)"""
    return [Op(O_VEL, o, round(vx * 10), round(vy * 10)) for o in lst(objs)]


def move(objs, dx, dy, ms=0):
    """moved by (dx, dy) pixels, evenly over ms (setX in a loop, or a tween)"""
    return [Op(O_MOVE, o, round(dx), round(dy), frames(ms)) for o in lst(objs)]


def steps(objs, dx, dy, n, rate):
    """for (e = 0; n > e;) runLater(e / rate * 1000, setX(getX() + dx)): n steps, rate a second"""
    return move(objs, dx * n, dy * n, 1000 * n / rate)


def setpos(objs, x=None, y=None):
    """setX, setY (the corner of its picture)"""
    out = []
    for o in lst(objs):
        nx, ny = (o.x if x is None else x), (o.y if y is None else y)
        out.append(Op(O_SET, o, round(nx + o.iw / 2), round(ny + o.ih / 2), (x is not None) | (y is not None) << 1))
    return out


def grow(objs, sx, sy, s=0.0):
    """growTo(sx, sy, s): sx and sy times the size of its picture, over s seconds, about its centre"""
    return [Op(O_GROW, o, round(o.iw * sx / 2), round(o.ih * sy / 2), frames(s * 1000)) for o in lst(objs)]


def flag(objs, on, off):
    return [Op(O_FLAG, o, on, off) for o in lst(objs)]


def gone(objs):  # recycled
    return flag(objs, F_OFF, 0)


def hide(objs):  # disableActorDrawing
    return flag(objs, F_HIDE, 0)


def show(objs):
    return flag(objs, 0, F_HIDE)


def ghost(objs):  # nothing to stand on: its group changed, or its body destroyed
    return flag(objs, F_GHOST, 0)


def solid(objs):
    return flag(objs, 0, F_GHOST)


def big(objs):  # a saw's second picture, twice as big
    return flag(objs, F_BIG, 0)


def spawn(objs, src=None, dx=0, dy=0):
    """createRecycledActor: there from now on, where it was made or at src's centre and (dx, dy)"""
    return [Op(O_SPAWN, o, NOBODY if src is None else src, round(dx), round(dy)) for o in lst(objs)]


def warp(x, y):
    """the room's warp: the runner at (x, y) at once, hidden and still for 750 ms"""
    return [Op(O_WARP, None, x, y)]


def hero(what, v):
    return [Op(O_HERO, None, what, round(v))]


def run(v):  # _MaximumRunningSpeed
    return hero(H_RUN, v * 10)


def jump(force):  # _JumpingForce: the runner's push up (18 gives 188 pixels a second)
    return hero(H_JUMP, force * 188 / 18)


def gravity(gx, gy):
    return [Op(O_GRAV, None, round(gx * 10), round(gy * 10))]


def hgrow(sx, sy, s):
    """the runner's growTo"""
    return [Op(O_HGROW, None, round(sx * 100), round(sy * 100), frames(s * 1000))]


def shake(amount, s):
    return [Op(O_SHAKE, None, round(amount * 1000), frames(s * 1000))]


def fire(ev):  # shoutToScene
    return [Op(O_FIRE, None, ev)]


def rearm(objs):
    return [Op(O_REARM, o) for o in lst(objs)]


def toward(objs, v):  # setXVelocity(±v): towards the runner
    return [Op(O_TOWARD, o, round(v * 10)) for o in lst(objs)]


def patrol(objs, lo, hi):
    """turns round when its corner is at lo or less, hi or more"""
    return [Op(O_RULE, o, U_PATROL, round(lo + o.iw / 2), round(hi + o.iw / 2)) for o in lst(objs)]


def follow(objs, target, dx=0):
    """its x kept at target's and dx"""
    return [Op(O_RULE, o, U_FOLLOW, target, round(dx)) for o in lst(objs)]


def stopx(objs, x, snap=None):
    """stops when its corner gets to x (from either side), put at snap"""
    return [Op(O_RULE, o, U_STOPX, round(x + o.iw / 2), round((x if snap is None else snap) + o.iw / 2))
            for o in lst(objs)]


def wrapx(objs, lo, hi):
    """moving left past lo it comes back at hi, moving right past hi at lo (its corner)"""
    return [Op(O_RULE, o, U_WRAPX, round(lo + o.iw / 2), round(hi + o.iw / 2)) for o in lst(objs)]


def norule(objs, what=U_NONE):
    return [Op(O_RULE, o, U_NONE, what) for o in lst(objs)]


def accel(objs, ax, ay):
    """pushed along: ax, ay pixels a second faster each second (held back by the ground)"""
    return [Op(O_ACCEL, o, round(ax), round(ay)) for o in lst(objs)]


def hop(objs, v):
    """thrown up at v (the game's units) if at rest, and falling back to where it was"""
    return [Op(O_HOP, o, round(v * 10)) for o in lst(objs)]


def key(ev):
    """from now on the jump key sets off ev, each time it is pressed"""
    return [Op(O_KEY, None, ev)]


# ------------------------------------------------------------ a room
class Room:
    def __init__(self, name):
        self.name = name
        world = WORLDS.index(name.split("_")[0])
        self.modes = {11: R_FLAPPY, 12: R_GRAVITY, 14: R_WRAP}.get(world, 0)
        if name == "Boss_4":
            self.modes = R_WRAP | R_WRAPY | R_GRAVITY
        s = SCENES[name]
        self.ground = s["ground"]
        self.objs, self.events, self.hero = [], {}, None
        for a in s["actors"]:
            t = a["type"]
            if t in (590, 692):
                self.hero = (a["x"], a["y"])
            elif t not in SKIP:
                o = self.new(t, a["x"], a["y"], a["sx"], a["sy"], a["angle"], a["name"])
                if t == 846:  # Generic_Death_Square: not drawn
                    o.flags |= F_HIDE
        assert self.hero, name

    def new(self, typ, x, y, sx=1.0, sy=1.0, angle=0, name="made"):
        if typ in FAKE:
            kind, ev = K_BLOCK, 0
        elif typ in TRIGGER:
            kind, ev = K_TRIG, TRIGGER.index(typ) + 1
        else:
            kind, ev = KINDS[typ], 0
        o = Obj(kind, x, y, sx, sy, angle, typ, name, ev)
        self.objs.append(o)
        return o

    # the actors, found as the scripts find them
    def all(self, typ, x=None, y=None, cond=None):
        return [o for o in self.objs if o.typ == typ and not o.made and (x is None or o.x == x)
                and (y is None or o.y == y) and (cond is None or cond(o))]

    def one(self, typ, x=None, y=None):
        out = self.all(typ, x, y)
        assert len(out) == 1, (self.name, typ, x, y, out)
        return out[0]

    def make(self, typ, x, y, sx=1.0, sy=1.0, angle=0):
        """an actor the script creates (createRecycledActor): there once spawn()ed"""
        o = self.new(typ, x, y, sx, sy, angle % 360)
        o.flags |= F_OFF
        o.made = True
        return o

    def init(self, *ops):
        self.on(0, *ops)

    def on(self, ev, *ops):
        self.events.setdefault(ev, []).extend(flat(ops))

    def settle(self):
        """what init does at once to the actors there (growTo over .01 s, hiding): done here"""
        keep = []
        for op in self.events.get(0, []):
            o = op.obj
            if op.t == 0 and op.cond == 0 and isinstance(o, Obj) and not o.made:
                if op.code == O_GROW and op.c <= 1:
                    o.sx, o.sy = op.a * 2 / o.iw, op.b * 2 / o.ih
                    continue
                if op.code == O_FLAG:
                    o.flags = (o.flags | op.a) & ~op.b
                    continue
            keep.append(op)
        self.events[0] = keep

    def pack(self):
        """the room as spans of objects (n alike, 16 pixels apart) and steps that act on runs of them"""
        self.settle()
        objs = [o for o in self.objs if not (o.kind == K_COIN and o.x >= 500)]  # (coins never seen)
        seen = {id(o) for o in objs}
        evs = sorted(self.events)
        sig = {id(o): [] for o in objs}
        for ev in evs:
            for op in self.events[ev]:
                if isinstance(op.obj, Obj):
                    assert id(op.obj) in seen, (self.name, op.obj)
                    sig[id(op.obj)].append((ev, op.code, op.cond, ref(op.a), ref(op.b), ref(op.c)))
        for o in objs:
            o.sig = tuple(sorted(sig[id(o)]))
            o.hw, o.hh = u8(o.iw * o.sx / 2)[0], u8(o.ih * o.sy / 2)[0]
            o.vert = o.kind == K_SPIKE and o.angle in (90, 270)
            o.look = (o.kind != K_DOOR, o.kind, o.angle, o.flags, o.ev, o.hw, o.hh, o.vert, o.sig)
        # (all is drawn in the ground's colour but the doors' windows: only the doors need to be first)
        objs.sort(key=lambda o: o.look + ((o.cx, o.cy) if o.vert else (o.cy, o.cx)))
        spans = []
        for o in objs:
            s = spans[-1] if spans else None
            if s and s[0].look == o.look and len(s) < 63 and (
                    (o.cx == s[-1].cx and o.cy == s[-1].cy + 16) if o.vert else (o.cy == s[-1].cy and o.cx == s[-1].cx + 16)):
                s.append(o)
            else:
                spans.append([o])
        objs = [o for s in spans for o in s]
        for i, o in enumerate(objs):
            o.index = i
        assert len(objs) <= 120, (self.name, len(objs))
        span_rec = [(s[0].kind | (s[0].angle // 90) << 4 | s[0].vert << 6, s[0].flags, s[0].ev, round(s[0].cx), round(s[0].cy),
                     s[0].hw, s[0].hh, len(s)) for s in spans]
        # the steps: those alike on objects one after the other, at the same time or at times along x
        groups = {}
        order = []
        seq = {}  # (at the same time, the steps go in the script's order)
        for ev in evs:
            for i, op in enumerate(self.events[ev]):
                seq[id(op)] = i
                key = (ev, op.code, op.cond, ref(op.a), ref(op.b), ref(op.c))
                if key not in groups:
                    groups[key] = []
                    order.append(key)
                groups[key].append(op)
        recs = []
        for key in order:
            ops = sorted(groups[key], key=lambda p: (p.obj.index if isinstance(p.obj, Obj) else -1, p.t))
            i = 0
            while i < len(ops):
                p = ops[i]
                j, k = i + 1, 0
                if isinstance(p.obj, Obj):
                    while j < len(ops) and isinstance(ops[j].obj, Obj) and ops[j].obj.index == ops[j - 1].obj.index + 1:
                        q = ops[j]
                        if j == i + 1:  # the slope the first two give, 256ths of a frame a pixel
                            dx = q.obj.cx - p.obj.cx
                            kk = round((frames(q.t) - frames(p.t)) * 256 / dx) if dx else 0
                            if (frames(q.t) != frames(p.t) and not dx) or abs(kk) > 32767:
                                break
                        else:
                            kk = k
                        if abs(frames(p.t) + int((q.obj.cx - p.obj.cx) * kk // 256) - frames(q.t)) > 1:
                            break
                        k = kk
                        j += 1
                n = j - i
                if n == 1:
                    k = 0
                a, b, c = (v.index if isinstance(v, Obj) else v for v in (p.a, p.b, p.c))
                recs.append((key[0], frames(p.t), p.code, NOBODY if p.obj is None else p.obj.index, n, key[2], a, b, c, k,
                             seq[id(p)]))
                i = j
        recs.sort(key=lambda r: (r[0], r[1], r[-1]))
        recs = [r[:-1] for r in recs]
        self.expanded = sum(r[4] for r in recs)
        assert len(recs) < 256 and self.expanded <= 240, (self.name, len(recs), self.expanded)
        return span_rec, recs


def ref(v):
    return ("obj", id(v)) if isinstance(v, Obj) else v


def i16(v):
    v = int(round(v))
    assert -32768 <= v < 32768, v
    return (v & 0xFFFF).to_bytes(2, "little")


def u16(v):
    assert 0 <= v < 65536, v
    return v.to_bytes(2, "little")


def u8(v):
    return bytes([max(0, min(255, int(round(v))))])


ROOMS = []


def room(name):
    r = Room(name)
    ROOMS.append(r)
    return r


def fakes(r):
    """Fake_Tile_1 to 8 of a room"""
    return [r.all(t) for t in FAKE]


# ================================================================ 1. Pits
r = room("Pits_1")
f1 = r.one(600)
r.init(grow(f1, 1, 5))
r.on(1, vel(f1, 0, 20), at(2000, vel(f1, 0, 0)))

r = room("Pits_2")
f1, f2 = r.one(600), r.one(645)
r.init(grow([f1, f2], 1, 5))
r.on(1, vel(f1, 0, 10), at(2000, vel(f1, 0, 0)))
r.on(2, vel(f2, 0, 20), at(2000, vel(f2, 0, 0)))

r = room("Pits_3")
f1, f2 = r.one(600), r.one(645)
r.init(grow([f1, f2], 7, 5))
r.on(1, move([f1, f2], -64, 0, 500))

r = room("Pits_4")
f1 = r.one(600)
f1.back = True
r.init(grow(f1, 13, 5))
r.on(1, move(f1, 16, 0, 100))
r.on(2, move(f1, 192, 0, 1700))

r = room("Pits_5")
f1, f2 = r.one(600), r.one(645)
f1.back = f2.back = True
r.init(grow(f1, 15, 5), grow(f2, 5, 5))
r.on(1, move(f1, 240, 0, 2000), at(2000, fire(2), setpos(f1, 500)))
r.on(2, move(f2, -16, 0, 100))

# ================================================================ 2. Spikes
r = room("Spikes_1")
r.on(1, move(r.one(609, 240), -16, 0, 250))

r = room("Spikes_2")
r.on(1, steps(r.one(609, 256), -1, 0, 16, 50))
r.on(2, steps(r.one(609, 352), 1, 0, 16, 50))

r = room("Spikes_3")
s1, s2 = r.one(609, 272), r.one(609, 336)
s2.back = True
r.on(1, move(s1, -32, 0, 250))  # (two Trigger_1: each moves it again)
r.on(2, move([s1, s2], -400, 0, 7000))

r = room("Spikes_4")
r.on(1, [at(1000 * s.x / 120, move(s, 0, -16)) for s in r.all(609)])

r = room("Spikes_5")
r.on(1, [at(1000 * s.x / 150, move(s, 0, -16), at(400, move(s, 0, 16))) for s in r.all(609)])
r.on(2, [at(1000 * (528 - s.x) / 250, move(s, 0, -16)) for s in r.all(609)])

# ================================================================ 3. Push
r = room("Push_1")
f1 = r.one(600)
r.init(grow(f1, 1, 2))
r.on(1, move(f1, 0, -32, 250), at(1000, move(f1, -32, 0, 250)), at(1500, move(f1, 0, 32, 500)))

r = room("Push_2")
f1, f2 = r.one(600), r.one(645)
r.init(grow(f1, 1, 2), grow(f2, 2, 5))
r.on(1, vel(f2, 0, 15))
r.on(2, move(f1, 0, -16, 250), at(250, move(f1, 32, 0, 250)), at(500, move(f1, 0, 32, 500)))

r = room("Push_3")
f1 = r.one(600)
r.init(grow(f1, 1, 2))
r.on(1, move(f1, 0, -16, 250), at(250, move(f1, -32, 0, 250)))
r.on(2, move(f1, 176, 0, 750))

r = room("Push_4")
f1, f2, f3 = r.one(600), r.one(645), r.one(647)
r.init(grow(f1, 5, 3), grow([f2, f3], 2, 1))
r.on(1, steps(f2, 0, -1, 16, 100), at(750, steps(f2, 1, 0, 32, 100)),
     at(1750, steps(f1, -1, 0, 80, 100), vel(f2, 6, 0)))
r.on(2, steps(f3, -1, 0, 32, 75))

r = room("Push_5")
f1, f2 = r.one(600), r.one(645)
r.init(grow(f1, 17, 5), grow(f2, 1, 5))
# 256 steps of a pixel, the c-th after c / (50 + c / 5) seconds: faster and faster (in four moves)
r.on(1, [at(500 + 1000 * c / (50 + c / 5), move(f1, -64, 0, 1000 * ((c + 64) / (50 + (c + 64) / 5) - c / (50 + c / 5))))
         for c in (0, 64, 128, 192)])
r.on(2, steps(f2, 0, -1, 32, 100), at(500, steps(f2, 0, 1, 32, 100)))

# ================================================================ 4. Coins
r = room("Coins_1")
f1 = r.one(600)
r.init(grow(f1, 2, 3))
r.on(1, vel(f1, 15, 0), at(2000, vel(f1, 0, 0)))

r = room("Coins_2")
f1 = r.one(600)
r.init(grow(f1, 7, 1), hide(f1))

r = room("Coins_3")
f1 = r.one(600)
r.init(grow(f1, 15, 3))
r.on(1, gone(r.all(602)), vel(f1, 0, 20), at(2000, vel(f1, 0, 0)))

r = room("Coins_4")
r.init(hide(r.all(600)), grow(r.all(600), 1.3, 1))
r.on(1, move(r.one(613, 288), 16, 0, 100))

r = room("Coins_5")  # the bad coins pushed right (pushInDirection, 1000 times in 6.7 s)
r.on(1, accel(r.all(659), 90, 0), at(6667, accel(r.all(659), 0, 0)))

# ================================================================ 5. Controls
r = room("Controls_1")
r.init(at(50, hero(H_BACK, 1)))

r = room("Controls_2")
r.init(at(50, hero(H_BACK, 1)))
r.on(1, steps(r.one(609, 352), 1, 0, 16, 100))

r = room("Controls_3")  # jumping is Down; Up throws the spikes up
r.init(at(50, hero(H_BACK, 1), hero(H_DOWNJUMP, 1)), hide(r.one(609, 400)), key(EV_KEY))
r.on(1, show(r.all(609)))
r.on(EV_KEY, hop(r.all(609), 30))

r = room("Controls_4")
r.init(at(100, jump(100), hero(H_MAXUP, 1000)))

r = room("Controls_5")
r.init(at(50, hero(H_BACK, 1)))
r.on(1, hero(H_BACK, 0), shake(.004, .2))
r.on(2, hero(H_BACK, 1), shake(.004, .2))
r.on(3, hero(H_BACK, 0), shake(.004, .2))

# ================================================================ 6. Platforms
r = room("Platforms_1")
f1, f2, f3 = r.one(600), r.one(645), r.one(647)
r.on(1, vel([f1, f2, f3], 5, 0), at(4800, vel(f2, 0, 0), setpos(f2, 384)))  # (Fake_2 stops at 384)
r.on(2, vel(f1, 0, 15))
r.on(3, vel(f3, 0, 15))

r = room("Platforms_2")  # a belt of platforms, round and round; the trigger turns it
ps = r.all(600)
r.init(grow(ps, 3, 1), wrapx(ps, 16, 496))
r.on(EV_MOVE, vel(ps, -5, 0))
r.on(1, vel(ps, 5, 0), at(1000, grow(ps, 2, 1, 5)))

r = room("Platforms_3")
fl, f2, f3, f4, f5, f6 = (r.one(t) for t in (600, 645, 647, 649, 651, 653))
r.init(grow([fl, f2, f3, f4, f5, f6], 3, 1))
slide = []
for f, d, ms in ((f2, -7, 0), (f3, 7, 1250), (f4, -7, 2500), (f5, 7, 3750), (f6, -7, 5000)):
    slide += at(ms, vel(f, d, 0), at(80000 / 70, vel(f, 0, 0), setpos(f, 256)))  # (stopped at 256)
r.on(1, slide, at(6500, vel([fl, f2, f3, f4, f5, f6], 0, 15)))

r = room("Platforms_4")
fs = [r.one(t) for t in (600, 645, 647, 649, 651)]
r.on(1, grow(fs, 0, 1, .1), ghost(fs))
back = lambda f: [grow(f, 1.1, 1, .1), solid(f)]  # noqa: E731
r.on(2, back(fs[0]), at(1500, back(fs[1])), at(3000, back(fs[2])), at(4500, back(fs[3])),
     at(7000, grow(fs[4], .5, 1, .1), at(100, grow(fs[4], 0, 1, .1)), at(1000, back(fs[4]))),
     at(2000, gone(r.all(615))))
r.on(4, at(500, grow(fs[0], 0, 1, .1), ghost(fs[0]), at(500, grow(fs[1], 0, 1, .1), ghost(fs[1])),
           at(1000, grow(fs[2], 0, 1, .1), ghost(fs[2]))))

r = room("Platforms_5")
fs = [r.one(t) for t in (600, 645, 647, 649, 651, 653, 655)]
r.init(grow(fs, 2, 1))
r.on(1, [at(600 + 300 * i, vel(fs[i], 0, 15)) for i in range(5)])
r.on(2, at(500, steps(fs[5], 0, -1, 32, 50)))
r.on(3, vel(fs[6], 0, 18))

# ================================================================ 7. Springs
r = room("Springs_1")
spk = r.all(609)
r.on(1, steps([s for s in spk if s.y == 85], 0, -1, 5, 50))
r.on(2, steps([s for s in spk if s.x in (176, 192)], 0, -1, 5, 50))
r.on(3, steps([s for s in spk if s.x in (272, 288)], 0, -1, 5, 50))
r.on(4, steps([s for s in spk if s.x in (352, 368, 384)], 0, 1, 5, 50))

r = room("Springs_2")
r.on(1, steps(r.all(600), 1, 0, 16, 100), steps(r.all(621, 144), 1, 0, 16, 100))
r.on(2, steps([s for s in r.all(609) if s.x in (256, 272)], 0, 1, 10, 100))
r.on(3, steps(r.all(645), 1, 0, 16, 100), steps(r.all(647), 0, -1, 32, 25), steps(r.one(621, 304, 160), 1, 0, 16, 100),
     steps(r.one(621, 304, 144), 0, -1, 32, 25), steps(r.one(609, 362), 0, -1, 16, 100))

r = room("Springs_3")
sp, f1 = r.one(621), r.one(600)
copy = r.make(600, 0, 0)
r.init(grow(sp, 1.5, 1))
r.on(1, vel([f1, sp], 5, 0), stopx([f1, sp], 368))
r.on(2, spawn(copy, f1), vel(copy, -10, 0), stopx(copy, 144), hide(f1))

r = room("Springs_4")
f2 = r.one(600, 96)
r.init(grow(f2, 1, 4))
r.on(1, steps(f2, -1, 0, 16, 100), steps(r.one(621, 96), -1, 0, 16, 100))
r.on(2, steps(r.all(609), 0, 1, 5, 50), steps(r.one(621, 384), 0, -1, 16, 100))

r = room("Springs_5")
f1, sp = r.all(600), r.all(621)
r.init(grow(sp, 1.5, 1))
for ev, d in ((1, 1), (2, -1), (3, 1), (4, 1)):
    r.on(ev, steps(f1 + sp, d, 0, 16, 100))

# ================================================================ 8. Warps
r = room("Warps_1")
f1 = r.one(600)
r.init(grow(f1, 1, 3))
r.on(1, warp(240, 32))
r.on(2, warp(352, 32))
r.on(3, vel([f1, r.one(609, 320)], 0, 30))
r.on(4, warp(448, 64))

r = room("Warps_2")
w4 = r.one(619, 136)
t2 = r.make(604, 136, 176)
r.on(1, warp(240, 32), spawn(t2), vel([t2, w4], 2, 0))
r.on(2, warp(336, 32), vel(r.all(619), 0, 0))
r.on(3, vel(r.one(619, 256), 0, 30))
r.on(4, steps(r.one(619, 352), 1, 0, 32, 100))
r.on(5, warp(432, 32))

r = room("Warps_3")
r.on(1, warp(296, 32))
r.on(2, steps(r.all(645), -1, 0, 16, 100))
r.on(3, steps(r.one(619, 160), -1, 0, 80, 100))
r.on(4, steps(r.all(600), -1, 0, 16, 100))
r.on(5, warp(336, 32))
r.on(6, steps(r.one(619, 352), -1, 0, 32, 100), steps(r.one(619, 384), -1, 0, 16, 100))
r.on(7, warp(432, 32))

r = room("Warps_4")  # each warp sends you back a step, and the step behind falls
fs = [r.one(t) for t in (600, 645, 647, 649, 651, 653)]
f7 = r.one(655)
ws = {w.x: w for w in r.all(619)}
w8, t8 = r.make(619, 200, 176), r.make(675, 200, 176)
r.init(grow(fs, 2, 1), grow(f7, 7, 1), hide([w for x, w in ws.items() if x != 104]))
for i, x in enumerate((104, 136, 168, 200, 232, 264)):
    r.on(i + 1, warp(x - 32, 96), gone(ws[x]), show(ws[x + 32]),
         at(1200, vel(fs[i], 0, 10) if i < 5 else [steps(fs[i], 0, 1, 80, 100), at(1500, spawn([w8, t8]))]))
r.on(7, gone(ws[296]), vel(f7, 100, 0))
r.on(8, warp(320, 96))

r = room("Warps_5")  # the first warp follows you about
w, t1 = r.one(619, 96), r.one(602, 96)
both = [w, t1]
r.on(1, warp(64, 176))
r.on(2, warp(224, 16), steps(both, 1, 0, 160, 800), gone(r.one(619, 128)), gone(r.all(604)))
r.on(3, steps(both, -1, 0, 64, 800))
r.on(4, warp(384, 16), gone(r.one(619, 192)))
r.on(5, warp(224, 16), steps(both, 1, 0, 160, 800), gone(r.one(619, 256)))
r.on(6, steps(both, 1, 0, 64, 800))
r.on(7, warp(416, 80), steps(both, -1, 0, 1000, 800), gone(r.one(619, 352)))
r.on(8, warp(448, 16), gone(r.one(619, 416)))

# ================================================================ 9. Scale (the runner grows and shrinks)
r = room("Scale_1")
r.on(1, hgrow(1, 3.5, 2))
r.on(2, hgrow(1, .5, .5), steps(r.one(609, 224), 0, 1, 16, 50))
r.on(3, hgrow(1, 12, 2.5))

r = room("Scale_2")
f1, f2 = r.one(600), r.one(645)
b5, t5 = r.one(661, 248), r.one(665)
r.init(grow(f1, 10, 3), grow(f2, 1, 3))
r.on(1, hgrow(6, 6, .5))
r.on(2, hgrow(.25, .25, .25), steps(f2, -1, 0, 16, 100), at(1000, steps(f1, 0, -1, 80, 100)))
r.on(4, hgrow(50, 50, 6), steps([b5, t5], 0, -1, 64, 100))
r.on(5, hgrow(1, 1, .2))

r = room("Scale_3")
f1 = r.one(600)
r.init(grow(f1, 5, 2))
r.on(1, hgrow(.2, 1, 1))
r.on(2, steps(f1, 1, 0, 6, 50), at(1000, steps(f1, 1, 0, 58, 100)))
r.on(3, hgrow(10, 1, 1), steps(r.one(621, 272), 0, -1, 16, 100))
r.on(4, hgrow(.25, .25, .1))
r.on(5, hgrow(1, 1, .5))

r = room("Scale_4")
r.on(1, hgrow(20, 1, 6), [at(1000 * s.x / 108, steps(s, 0, -1, 16, 30)) for s in r.all(609) if s.y > 128])
r.on(2, hgrow(28, 1, .2), at(2500, hgrow(1, 1, 1)))

r = room("Scale_5")
f2 = r.one(645)
b6, t6 = r.make(661, 273, 144, angle=180), r.make(667, 273, 140)
b5, t5 = r.make(661, 240, 144, angle=180), r.make(665, 240, 140)
r.init(grow(f2, 2, 1))
r.on(1, hgrow(.5, .5, .5), at(250, steps(r.all(600), -1, 0, 6, 100)))
r.on(3, hgrow(16, 1, 5), steps(r.one(609, 202), 1, 0, 6, 100), steps(r.one(609, 310, 160), -1, 0, 6, 100),
     at(750, spawn([b5, t5])))
r.on(4, hgrow(2, 2, .5), steps(r.one(609, 218), 1, 0, 6, 100), steps(r.one(609, 310, 96), -1, 0, 6, 100))
r.on(5, at(750, spawn([b6, t6])))
r.on(6, hgrow(1, 1, .5), steps(r.all(621), 0, -1, 6, 100), steps(f2, 1, 0, 16, 100))

# ================================================================ 10. Doors (the ground is in front of them)
r = room("Doors_1")
d = r.one(598)
k1, k2 = r.make(609, 0, 0), r.make(609, 0, 0)
k1.back = k2.back = True
t5 = r.make(665, 192, 144)
r.on(1, spawn(k1, d), steps(d, 1, 0, 16, 100))
r.on(2, steps(d, 1, 0, 32, 100))
r.on(3, spawn(k2, d), steps(d, 1, 0, 16, 100))
r.on(4, steps(d, 0, 1, 16, 100), at(200, setpos(d, 140)), at(210, steps(d, 0, -1, 16, 100)), spawn(t5))
r.on(5, steps(k1, -1, 0, 96, 150))

r = room("Doors_2")  # the doors in the floor come up as steps, on blocks you cannot see
ds = {o.x: o for o in r.all(598, y=160)}
fs = {o.x: o for o in r.all(600)}
r.init(hide(list(fs.values())), grow(list(fs.values()), 1.2, 1))
for i, x in enumerate((256, 224, 192, 160, 128, 96)):
    r.on(i + 1, steps([ds[x], fs.get(x, ds[x])] if x in fs else ds[x], 0, -1, 16, 100))
r.on(7, steps([o for x, o in ds.items() if x != 400], 0, 1, 16, 1.5), steps(list(fs.values()), 0, 1, 16, 2),
     steps(ds[400], 0, -1, 16, 25))

r = room("Doors_3")
r.on(1, steps(r.one(598), 1, 0, 144, 7.5))
r.on(2, steps(r.one(609, 256), 0, -1, 16, 100))

r = room("Doors_4")
d, f1, f2, sp = r.one(598), r.one(600), r.one(645), r.one(621)
d2 = r.make(598, 448, 176)
r.init(hide([f1, sp]), grow(sp, 2, 1))
r.on(1, at(250, vel([d, f1, f2, sp], 5, 0)))
r.on(2, vel([d, f1, f2, sp], 0, 20), at(100, spawn(d2), steps(d2, 0, -1, 16, 100)))

r = room("Doors_5")  # you look like the door, and the door like you
r.modes |= R_SWAP
f1, f2 = r.one(600), r.one(645)
r.init(grow(f1, 1, 5), grow(f2, 3, 5))
r.on(1, steps(f1, 1, 0, 8, 100), at(200, steps(f1, -1, 0, 8, 100)), at(750, steps(f2, 1, 0, 48, 200)))

# ================================================================ 11. Saws
r = room("Saws_1")
saw, t1 = r.one(684), r.one(602)
r.init(vel(saw, 7, 0), patrol(saw, 176, 352), follow(t1, saw))
r.on(1, norule([saw, t1]), vel(saw, 0, 0), at(500, grow(saw, 2, 2, .3), at(300, grow(saw, 1, 1), at(10, big(saw)))),
     at(1000, vel(saw, 15, 0)))

r = room("Saws_2")
saw, t1 = r.one(684), r.one(602)
t2, t3 = r.make(604, 0, 0), r.make(615, 0, 0)
r.init(vel(saw, 7, 0), patrol(saw, 192, 240), follow(t1, saw, 24), stopx(saw, 400, 351))
r.on(1, norule(t1), norule(saw, U_PATROL), vel(saw, 0, 0), at(500, spawn(t2, saw, 0, -16), follow(t2, saw)),
     at(1000, vel(saw, 20, 0)))
r.on(2, grow(saw, .5, .5), at(10, big(saw), grow(saw, 2.8, 2.8, 1.5)), at(250, vel(saw, 0, 0)),
     at(2000, vel(saw, -15, 0), stopx(saw, 144)),
     at(3000, grow(saw, .5, .5, .5), at(500, steps(saw, 0, 1, 16, 100)),
        at(1000, setpos(saw, 384), spawn(t3, saw, -32, -16))))
r.on(3, steps(saw, 0, -1, 16, 100))

r = room("Saws_3")
saw = r.one(684)
r.init(at(100, big(saw), grow(saw, 1.8, 1.8, 1)))
r.on(1, grow(saw, 4, 4, .25), at(250, grow(saw, 1.8, 1.8, .25)))
r.on(2, grow(saw, .5, .5, .5), at(2250, grow(saw, 4, 4, .25)), at(3000, vel(saw, -5, 0)))

r = room("Saws_4")
s1, s2, s3 = r.one(684, 136), r.one(684, 104), r.one(684, 360)
r.init(grow([s1, s2, s3], 1.5, 1.5))
r.on(1, steps(s1, -1, 0, 32, 300), steps(s2, 1, 0, 32, 300), at(1000, steps(s1, 0, 1, 128, 300)),
     at(1500, steps(s2, 0, 1, 64, 200)))
r.on(2, steps(s1, 1, 0, 192, 200), at(1750, steps(s1, 0, -1, 112, 50)))
r.on(3, steps(s1, 1, 0, 8, 50), at(250, steps(s1, -1, 0, 8, 50)))
r.on(4, steps(s1, 1, 0, 16, 300), at(500, grow(s3, 1, 1), steps(s3, 0, -1, 16, 50)))

r = room("Saws_5")
s1, s2, s3 = r.one(684, y=64), r.one(684, y=96), r.one(684, y=192)
f1, f3 = r.all(600), r.all(647)
r.init(grow([s1, s2, s3], 2, 2), hide(f1 + f3), grow(f1 + f3, 2, 2))
t = 1000 * (385 - 16) / 70  # (the first saw, and its block, come back from 384)
r.on(1, vel(f1 + [s1], 7, 0), at(t, vel(f1 + [s1], 0, 0), setpos(f1 + [s1], 384), at(250, vel(f1 + [s1], -5, 0))))
r.on(2, vel(s2, -7, 0))
r.on(3, vel(f3 + [s3], 7, 0))

# ================================================================ 12. Flappy (the runner flies: it keeps going, Up flaps)
# (The flier's own init runs after the scene's and sets it off to the right at 7: what a scene's init
# sets on it at once is lost, so Flappy 3 starts to the right, into its Trigger_1, and Flappy 5 at 7.)
r = room("Flappy_1")
r.on(1, steps([r.one(598), r.one(600)], 0, -1, 48, 100))

r = room("Flappy_2")
f2, f3 = r.one(645), r.one(647)
t3, b3 = r.make(615, 352, 112, 1, 9), r.make(661, 384, 160, angle=270)
b4, t4 = r.make(661, 368, 64, angle=270), r.make(617, 368, 64)
r.init(grow(f2, 1, 4))
r.on(1, hero(H_DIR, -1))
r.on(2, hero(H_DIR, 1), steps(f2, 0, -1, 120, 100), spawn([t3, b3]), steps(b3, -1, 0, 16, 100))
r.on(3, vel([f3, b3], 7, 0), at(3000, vel([f3, b3], 0, 0), gone([f3, b3])), spawn([b4, t4]))
r.on(4, hero(H_DIR, -1))

r = room("Flappy_3")
walls = r.all(609) + r.all(645)
r.init(grow(r.one(600), 3, 1), grow(r.all(645), 35, 11))
r.on(1, hero(H_DIR, -1), at(1000, grow(r.one(600), 1, 1, 1)))
r.on(2, hero(H_DIR, 1),
     at(1000, run(6), jump(20), vel([w for w in walls if w.y < 120], 0, .6), vel([w for w in walls if w.y > 120], 0, -.6)),
     at(2000, run(5), jump(18)), at(3000, run(4), jump(16)), at(4000, run(3), jump(14)),
     at(5000, run(2), jump(12)), at(7000, steps([r.one(598), r.one(600)], 1, 0, 16, 10)),
     at(13000, vel(walls, 0, 0)))

r = room("Flappy_4")
d = r.one(598)
r.init(hide(r.all(661)))
r.on(1, steps(d, 1, 0, 96, 100), at(1100, setpos(d, 32), steps(d, 1, 0, 32, 100)), show(r.all(661)))
r.on(2, hero(H_DIR, -1), steps(r.all(609), 1, 0, 15, 100))

r = room("Flappy_5")
f2, f5, f6 = r.one(645), r.one(651), r.one(653)
r.init(grow(f6, 1, 3))
b2, t2, t3 = r.make(661, 64, 144, angle=90), r.make(604, 112, 144, 1, 7), r.make(615, 56, 144)
b4, t4, t5 = r.make(661, 448, 64, angle=270), r.make(617, 368, 96, 1, 9), r.make(665, 456, 112)
b6, t6 = r.make(661, 64, 96, angle=90), r.make(667, 56, 96)
b7, t7 = r.make(661, 448, 48, angle=270), r.make(673, 302, 112, 1, 13)
b8, t8 = r.make(661, 382, 16, angle=180), r.make(675, 382, 16)
r.on(1, hero(H_DIR, -1), spawn([b2, t2, t3]))
r.on(2, steps([f2, b2, t3], 0, -1, 16, 50))
r.on(3, hero(H_DIR, 1), spawn([b4, t4, t5]))
r.on(4, steps(b4, 0, 1, 48, 100), steps(f5, 0, -1, 16, 50))
r.on(5, hero(H_DIR, -1), spawn([b6, t6]))
r.on(6, hero(H_DIR, 1), spawn([b7, t7]))
r.on(7, steps([b7, f5], 1, 0, 128, 50), spawn([b8, t8]))
r.on(8, hero(H_DIR, -1), steps(f6, 0, -1, 64, 50))

# ================================================================ 13. Gravity (Up turns gravity round)
r = room("Gravity_1")
r.init(grow(r.one(600), 3, 1))
r.on(1, vel(r.one(600), 0, 14))
r.on(2, at(250, steps(r.one(645), -1, 0, 48, 100)))

r = room("Gravity_2")
r.init(grow(r.one(600), 25, 1), grow(r.one(645), 3, 1), grow(r.one(647), 3, 1), grow(r.one(649), 2, 5))
r.on(1, vel(r.one(600), 5, 0))
r.on(2, grow(r.one(645), 1, 1, .25))
r.on(3, steps(r.one(647), -1, 0, 16, 100))
r.on(4, steps(r.one(649), -1, 0, 32, 100))

r = room("Gravity_3")  # Up after the trigger: gravity to the right, and you fly off
f1 = r.one(600)
r.init(grow(f1, 3, 1))
r.on(1, key(EV_KEY), vel(f1, 0, 3), vel([r.one(598), r.one(645)], 0, -3))
r.on(EV_KEY, vel(f1, 0, 0), at(10, gravity(12, 0), hero(H_PUSHX, 150)))

r = room("Gravity_4")
t2, t3 = r.make(604, 256, 16), r.make(615, 288, 176)
r.init(grow(r.one(647), 3, 1), grow(r.one(651), 3, 1))
b = r.one(661, 256)
r.on(1, steps(r.all(645) + [b], 0, 1, 64, 200), at(500, spawn(t2, b)))
r.on(2, steps(r.one(651), 0, 1, 64, 200), spawn(t3), grow(t3, 3, 1, .1))
r.on(3, move(r.one(649), 64, 0))

r = room("Gravity_5")  # (its other trap, gravity to the right, waits for the space bar: not on a calculator)
s1 = r.one(684, 496)
r.init(grow(s1, 3, 3), big(s1))
r.on(1, steps(r.all(600), 1, 0, 64, 100))
r.on(2, at(1500, steps(s1, -1, 0, 392, 150)), at(4000, ifev(3, steps(s1, 0, 1, 48, 25))))
r.on(3, steps(r.all(645), 1, 0, 64, 100))

# ================================================================ 14. Movement (each room moves differently)
r = room("Movement_1")  # ice, and no slowing down in the air
ice = r.all(600) + r.all(645)
for o in ice:
    o.flags |= F_ICE
big_ = {(256, 160): 25, (160, 112): 9, (336, 112): 11}
for o in r.all(600):
    r.init(grow(o, big_[(o.x, o.y)], 1))
r.init(at(100, hero(H_SLIDE, 1)))
r.on(1, steps(r.one(609, 256, 160), 0, -1, 16, 100))
r.on(2, steps(r.one(609, 288, 112), 0, -1, 16, 100))
r.on(3, steps(r.all(645), -1, 0, 16, 100))
r.on(4, steps(r.one(609, 96, 112), 0, -1, 16, 100))

r = room("Movement_2")  # floating down
f1, f2, f3, f4, f5 = (r.one(t) for t in (600, 645, 647, 649, 651))
t2 = r.make(604, 240, 112, 1, 10)
r.init(at(100, hero(H_MAXDOWN, 50), gravity(0, 20)), grow([f1, f2], 2, 3), grow([f3, f4, f5], 3, 1), ghost(f4))
r.on(1, steps(r.all(609), 0, -1, 16, 100), steps(f1, 0, -1, 64, 100), at(1000, spawn(t2), steps(f2, 0, -1, 64, 100)))
r.on(2, steps([f1, f2], 0, 1, 80, 25), steps(f3, 0, -1, 176, 25))
r.on(3, steps(r.all(684), 0, 1, 80, 50), steps([f4, f5], 0, -1, 300, 25))
r.on(4, grow(f5, 0, 1, 1), at(1000, ghost(f5)))

r = room("Movement_3")  # slow, then fast
saw, f1, f2 = r.one(684), r.one(600), r.one(645)
r.init(grow(saw, 2, 2))
r.on(1, run(3), steps(saw, -1, 0, 600, 65), at(2000, steps(f1, -1, 0, 16, 50)))
r.on(2, run(20), steps(r.all(609), 0, -1, 16, 100), setpos(saw, 496, 112))
r.on(3, steps(r.all(609), 0, 1, 16, 100), steps(f2, -1, 0, 16, 50), at(500, steps(saw, -1, 0, 416, 180)))
r.on(4, run(3), at(1000, steps(saw, 0, 1, 48, 50), at(2000, steps(saw, 1, 0, 600, 100))))

r = room("Movement_4")  # falling slowly, running fast: the saw on the door shrinks with each button
saw = r.one(684)
bt = {o.y: o for o in r.all(661)}
r.init(at(100, gravity(0, 20), hero(H_MAXDOWN, 20), run(30)), grow(saw, 0, 0), at(1000, grow(saw, 4, 4, 1.5), big(saw)))
r.on(1, steps([bt[64], r.one(604)], -1, 0, 32, 65))
r.on(2, grow(saw, 3, 3, 1), steps([bt[96], r.one(615)], 1, 0, 32, 65))
r.on(3, grow(saw, 2, 2, 1), steps([bt[128], r.one(617)], -1, 0, 32, 65))
b160 = {o.x: o for o in r.all(661, y=160)}
r.on(4, grow(saw, 1, 1, 1), steps(b160[32], 1, 0, 32, 65), at(1000, steps(b160[32], -1, 0, 32, 65)),
     at(1000, steps([b160[480], r.one(665)], -1, 0, 32, 65)))
r.on(5, grow(saw, .01, .01, 1), at(1000, move(saw, 0, 100)))

r = room("Movement_5")  # heavy
fs = {t: r.all(t) for t in FAKE}
r.init(at(20, gravity(0, 200), hero(H_MAXDOWN, 800)), grow(fs[600], 2.95, 1), grow(fs[645] + fs[647] + fs[649] + fs[651], 3, 1),
       grow(fs[655], 2, 10), hide(fs[657]))
r.on(1, gravity(0, 175), gone(fs[657]))
r.on(2, vel(fs[600] + fs[647], -5, 0), vel(fs[645] + fs[649], 0, -3), vel(fs[651], 5, 0), vel(fs[655], 0, 5),
     at(5000, vel(fs[653], 0, 5)),
     at(1000 * (544 - 32) / 30, vel(fs[649], 0, 0), setpos(fs[649], y=32)))  # (Fake_Tile_4 stops at y 32)

# ================================================================ 15. Wraparound (off one side, in at the other)
r = room("Wraparound_1")
r.on(1, steps(r.one(598), 1, 0, 32, 100), steps(r.all(609), 0, -1, 16, 100))

r = room("Wraparound_2")
r.on(1, steps(r.all(684), 0, -1, 64, 100), steps(r.all(609), 0, -1, 16, 100))
r.on(2, steps(r.all(684), 1, 0, 32, 50))

r = room("Wraparound_3")
r.on(1, steps([s for s in r.all(609) if s.y > 230] + r.all(600), 0, -1, 32, 100))
r.on(2, steps(r.all(645), 0, 1, 16, 25), steps(r.all(609, y=160), 0, -1, 16, 100))

r = room("Wraparound_4")
r.init(grow(r.all(684), 2, 2), grow(r.all(600) + r.all(645) + r.all(647) + r.all(649), 1, 3))
r.on(1, steps(r.all(600), 0, -1, 48, 100))
r.on(2, steps(r.all(645), 0, -1, 48, 100), steps(r.all(684, y=232), 1, 0, 432, 40))
r.on(3, steps(r.all(647), 0, -1, 48, 100))
r.on(4, steps(r.all(649), 0, -1, 48, 100), at(500, grow(r.all(649), .95, 3)))
r.on(5, steps(r.all(684, y=80), -1, 0, 20, 100))

r = room("Wraparound_5")
saws = {(o.x, o.y): o for o in r.all(684)}
r.init(grow(r.all(684), .9, .9))
r.on(1, steps(r.all(609), 0, -1, 16, 100))
r.on(2, [[steps(s, 0, -1, 8, 100), at(500, toward(s, 9)), at(5000, vel(s, 0, 0), gone(s))]
         for s in r.all(684, y=144)])
r.on(3, [[steps(s, 0, -1, 8, 100), at(250, steps(s, 0, 1, 8, 100))] for s in r.all(684, y=112)])
r.on(4, [[vel(s, 11, 0), at(5000, vel(s, 0, 0), gone(s))] for s in r.all(684, y=96)])
r.on(5, ifnot(4, [[vel(s, 20, 0), at(10000, vel(s, 0, 0), gone(s))] for s in r.all(684, y=96)]))
s80, s64 = saws[(432, 80)], saws[(-16, 64)]
r.on(6, steps(s80, 0, -1, 16, 100), at(200, steps(s80, -1, 0, 176, 100)), at(3500, vel(s80, 9, 0)),
     at(10000, vel(s80, 0, 0), gone(s80)), at(2000, vel(s64, 10, 0), at(10000, vel(s64, 0, 0), gone(s64))))
s52 = saws[(-72, -8)]
r.on(7, nth(1, grow(s52, 6, 6), vel(s52, 16, 0), at(5000, vel(s52, 0, 0), gone(s52))))  # (two Trigger_7)

# ================================================================ 16. Boss
r = room("Boss_1")
sw = {o.x: o for o in r.all(684)}
t2 = r.make(604, 256, 112)
t4 = r.make(617, 400, 112)
t5 = r.make(665, 102, 112)
r.init(grow(r.all(600), 1, 7))
r.on(1, steps(sw[32], 1, 0, 256, 90), at(2250, steps(sw[416], 0, -1, 16, 120)),
     at(3250, steps(sw[32], 0, 1, 16, 90), at(1000, setpos(sw[32], 32, 112)), steps(sw[416], -1, 0, 288, 70)),
     at(7000, steps(sw[32], 1, 0, 24, 90)),
     at(8000, steps(sw[32], -1, 0, 24, 90), steps(sw[416], 0, 1, 16, 90), spawn(t2)))
r.on(2, [at(1000 * (s.x - 112) / 150, steps(s, 0, 1, 16, 30)) for s in r.all(609)],
     at(1500, steps(r.all(619) + r.all(615), 0, -1, 16, 100)))
r.on(3, warp(80, 112), at(500, steps(r.all(609), 0, -1, 16, 50), steps(r.all(619), 0, 1, 16, 50), spawn(t4)))
r.on(4, steps(r.all(598), 1, 0, 48, 150), at(500, setpos(r.all(598), 32), steps(r.all(598), 1, 0, 48, 150), spawn(t5)))
r.on(5, steps(r.all(598) + r.all(600), -1, 0, 16, 100))

r = room("Boss_2")
fs = {t: r.one(t) for t in FAKE}
r.init(grow(fs[600], 1, 5), grow(fs[645], 1, 2), grow(fs[647], 1, 5), grow(fs[649], 1, 2), grow(fs[651], 1, 5),
       grow(fs[653], 2, 5), grow(fs[655], 1, 2), grow(fs[657], 1, 5))
r.on(1, steps(fs[600], 0, 1, 176, 300), at(2000, steps(fs[600], 0, -1, 176, 300)))
r.on(2, steps(fs[645], 0, -1, 16, 200), at(250, steps(fs[645], -1, 0, 32, 100)), at(500, steps(fs[645], 0, 1, 16, 100)),
     at(1000, steps(fs[647], 1, 0, 6, 100), at(200, steps(fs[647], -1, 0, 6, 300))))
r.on(3, steps(fs[649], 0, -1, 32, 200), at(250, steps(fs[649], -1, 0, 176, 150)), at(1750, steps(fs[649], 0, 1, 32, 150)),
     at(250, steps(fs[647], 1, 0, 16, 150)), at(1750, steps(fs[647], -1, 0, 16, 150)))
r.on(4, steps(fs[653], 1, 0, 32, 100), at(500, steps(fs[651], 0, 1, 176, 300), at(1500, steps(fs[651], 0, -1, 176, 300))))
r.on(5, steps(fs[655], 0, -1, 32, 200), at(500, steps(fs[655], 0, 1, 32, 200)))
r.on(6, steps([fs[657], r.one(598)], 1, 0, 16, 200))

r = room("Boss_3")
r.init(hide(r.all(647)))
w0, w208 = r.one(619, 336, 0), r.one(619, 144, 208)
r.on(1, steps(r.all(621) + r.all(600), 0, -1, 112, 40), at(2000, steps(r.all(609), 0, 1, 16, 100)))
r.on(2, steps(r.all(645), 1, 0, 16, 120))
r.on(3, steps(r.all(617) + [w0], 0, 1, 16, 100))
r.on(4, warp(192, 112), steps(r.all(665) + [w208], 1, 0, 224, 50))
r.on(5, nth(1, warp(240, 112)), nth(2, warp(288, 112)), nth(3, warp(336, 112)), at(250, rearm(r.all(665))))

r = room("Boss_4")  # gravity turns round, the sides wrap, and the saws bounce from side to side
sw = r.all(684)
t3, b3 = r.make(615, 192, 96), r.make(661, 192, 96)
t4, b4 = r.make(617, 416, 128), r.make(661, 416, 128, angle=180)
t5, b5, k5 = r.make(665, 144, 96), r.make(661, 144, 96), r.make(609, 160, 96)
t6, b6 = r.make(667, 256, 128), r.make(661, 256, 128, angle=180)
k6, k7 = r.make(609, 272, 128, angle=180), r.make(609, 240, 128, angle=180)
r.init(grow(r.all(600), 1, 7))
r.on(1, patrol(sw, 50, 478))  # (the screen's sides)
r.on(2, grow(sw, 1.25, 1.25, .5), steps(r.all(600), 1, 0, 16, 50), spawn([t3, b3]))
r.on(3, grow(sw, 1.5, 1.5, .5), steps(r.all(645), 1, 0, 16, 50), spawn([t4, b4]))
r.on(4, grow(sw, 1.75, 1.75, .5), steps(r.all(647), 1, 0, 16, 50), spawn([t5, b5, k5]))
r.on(5, grow(sw, 2, 2, .5), steps(r.all(649), 1, 0, 16, 50), spawn([t6, b6, k6, k7]))
r.on(6, grow(sw, 2.25, 2.25, .5), steps(r.all(651), 1, 0, 16, 50))

r = room("Boss_5")  # six buttons, each slower; then the door runs, and a giant saw comes
saw, door = r.one(684), r.one(598)
t7 = r.make(673, 144, 112)
r.init(grow(r.all(600), 1, 7), at(100, big(saw), grow(saw, 7, 7)))
for i, v in enumerate((8, 6, 5, 4, 3, 2)):
    r.on(i + 1, run(v), shake(.001 * (i + 1 if i < 5 else 7), 1))
r.on(6, spawn(t7), steps(door, 1, 0, 96, 100), at(1000, setpos(door, -16), steps(door, 1, 0, 96, 100)),
     at(5300, vel(saw, -6, 0)), steps(r.all(661), 0, 1, 16, 100))
r.on(7, steps(door, 1, 0, 48, 40))


# ------------------------------------------------------------ packing
def bitmap(ground):
    """the ground: 33 columns of 15 tiles, a bit each (the top tile the lowest bit)"""
    cols = [0] * 33
    for x, y, w, h in ground:
        for i in range(x, x + w):
            cols[i] |= ((1 << h) - 1) << y
    return b"".join(v.to_bytes(2, "little") for v in cols)


def packed():
    """everything, column by column (smaller packed): the rooms' modes and runners, their ground,
    how many spans each, the spans' fields, how many steps each, the steps' fields"""
    head, ground, nspan, nrec = bytearray(), bytearray(), bytearray(), bytearray()
    sc = [bytearray() for _ in range(8)]
    rc = [bytearray() for _ in range(10)]
    for r in ROOMS:
        spans, recs = r.pack()
        hx, hy = r.hero
        head += bytes([r.modes]) + i16(hx) + i16(hy)
        ground += bitmap(r.ground)
        nspan.append(len(spans))
        nrec.append(len(recs))
        for s in spans:
            for i, (v, f) in enumerate(zip(s, (u8, u8, u8, i16, i16, u8, u8, u8))):
                sc[i] += f(v)
        for q in recs:
            for i, (v, f) in enumerate(zip(q, (u8, u16, u8, u8, u8, u8, i16, i16, i16, i16))):
                rc[i] += f(v)
    return bytes(head + ground + nspan + b"".join(sc) + nrec + b"".join(rc)), sum(nspan), sum(nrec)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    assert len(ROOMS) == 80
    raw, spans, recs = packed()
    print(f"rooms: {spans} spans, {recs} steps, at most {max(r.expanded for r in ROOMS)} steps a room", file=sys.stderr)
    with open(os.path.join(out, "rooms.inc"), "w") as f:
        f.write("/* the rooms (tools/rooms.py) */\n")
        f.write(f"#define ROOM_BYTES {len(raw)}\n#define ROOM_COUNT {len(ROOMS)}\n#define SPANS {spans}\n#define STEPS {recs}\n")
        f.write("static const uint8_t data[ROOM_BYTES] = {")
        for i, b in enumerate(raw):
            f.write(("\n  " if i % 24 == 0 else "") + f"{b},")
        f.write("\n};\n")
    print(f"rooms: {len(raw)} bytes", file=sys.stderr)


if __name__ == "__main__":
    main()
