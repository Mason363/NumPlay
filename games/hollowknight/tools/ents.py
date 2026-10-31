"""A room's game objects that are not scenery: camera lock areas, gates to other rooms, respawn points and the triggers
that set them. Each becomes a record (src/game.h: Ent) with its trigger box (world units) and its own values; names go to
a shared string table."""
import math
import os
import struct
import numpy as np

# record types (src/game.h: ENT_*)
ENT_CAMLOCK, ENT_GATE, ENT_HAZARD_MARKER, ENT_RESPAWN, ENT_HAZARD_TRIGGER, ENT_MASK, ENT_DAMAGE, ENT_SHAPE, ENT_BOX, \
    ENT_OBJ, ENT_PIECE, ENT_SHADE_MARKER = 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12
# objects (ENT_OBJ's flags: src/obj.c)
OK_BREAKABLE, OK_ENEMY, OK_GREAT_DOOR, OK_GEO_ROCK, OK_CHEST, OK_BENCH, OK_BATTLE, OK_FK_FLOOR, OK_BGATE, OK_ARENA, \
    OK_EVENT, OK_SUMMON, OK_COND, OK_PROP, OK_DRIP, OK_COCOON, OK_AREA = \
    1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17
# area titles (AreaTitleController: OK_AREA's p3): wait for its trigger, only on a revisit, shown on the right, only
# when the Knight came in by a gate (s0), a sub area (always the small title), only once the Crossroads were visited
AF_TRIGGER, AF_REVISIT, AF_RIGHT, AF_DOOR, AF_SUB, AF_AFTER_CROSSROADS = 1, 2, 4, 8, 16, 32
# (its area list: identifier -> area number, sub area, the PlayerData bool it sets on a first visit)
AREAS = {"KINGSPASS": (0, True, None), "DIRTMOUTH": (1, False, "visitedDirtmouth"),
         "CROSSROADS": (2, False, "visitedCrossroads"), "EGGTEMPLE": (0, True, None),
         "SHAMANTEMPLE": (0, True, None), "GREENPATH": (3, False, "visitedGreenpath")}
# the battle gates' events (src/game.h: BG_*)
BG_EVENTS = ["BG CLOSE", "BG QUICK CLOSE", "BG OPEN", "BG QUICK OPEN", "BG DESTROY"]
# battle gates (BG Control: OK_BGATE's s0): closed at first, the bone ones' clips, gone once a PlayerData bool is set
BGF_START_CLOSED, BGF_BONE, BGF_PD = 1, 2, 4
# arenas (Battle Control, but the False Knight's: OK_ARENA's a): started by its trigger (else by an enemy), its
# Activate destroys the gates (else opens them at once), two waves (the second summoned), or no start (counting from
# the first)
ARF_TRIGGER, ARF_DESTROY_GATES, ARF_QUICK_OPEN, ARF_WAVES, ARF_NO_START = 1, 2, 4, 8, 16
# enemies (an OK_ENEMY's a: src/enemy.c), by their FSM and animation library
import actors as _actors
# enemies: by the FSM (or component) that runs them and their animation library -> their kind (actors.ENEMY_KINDS)
ENEMIES = {(k[1], k[2][0], k[2][1]): i + 1 for i, k in enumerate(_actors.ENEMY_KINDS) if k[1]}
# their FSMs' variables an enemy's record keeps (ET_VARS: up to four numbers, then bools as bits)
ENEMY_VARS = {"Zombie Swipe": (["Lunge Speed", "Idle Time"], ["Coward"]),
              "Bouncer Control": (["Speed"], ["Starts Inactive", "Start Up"]),
              "Roller": (["Acceleration", "Max Speed", "Roll time Min", "Roll time Max", "Stop Time"], ["Moving Right"]),
              "Blocker Control": (["Shot Y Speed"], ["Facing Right"]),
              "Zombie Leap": (["Idle Time"], []),
              "Zombie Guard": (["Chase Distance", "Roam Distance"], ["Start Facing Left"])}
# enemies whose body is a trigger, whose body is only their frames' colliders, and other children that are ranges
TRIGGER_BODIES = {"Pigeon"}
FRAME_BODIES = {"Plant Trap Control", "Mossy Control"}
RANGES = {"Moss Walker": ("Wake Range",), "Pigeon": ("Hero Range", "Enemy Range"), "Plant Trap Control": ("Detector",),
          "Moss Knight Control": ("Wake Box",), "Mender Bug Ctrl": ("Hero Detect",)}
# (enemies whose attacks start with their colliders off: those still kept, off)
HITBOXES_OFF = {"Moss Knight Control"}
# what follows an enemy's record: ENT_BOX records, tagged
ET_COLLIDER, ET_ALERT, ET_RANGE, ET_WALKER, ET_RECOIL, ET_CORPSE, ET_VARS, ET_TERRAIN, ET_HITBOX, ET_ZONE, ET_COND, \
    ET_CONTACT = 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11
# (FSM bools: First Crawler or Start Alert; Startles; one of an arena's Pre Battle Enemies; its death counts for its
# arena (HealthManager.battleScene); gone once its arena's fight is over; spawned by its mother's burster (Fly Spawn);
# there only once its arena's fight is over; its FSMs off till near the camera: FSMActivator)
EF_START, EF_STARTLES, EF_PREBATTLE, EF_BATTLE, EF_ARENA_GONE, EF_SPAWNED, EF_ARENA_LATER, EF_DORMANT = \
    1, 2, 4, 8, 16, 32, 64, 128
EF_DEATH_SHIFT = 8   # (bits 8-11: its enemyDeathType; 12-13: EnemyDeathEffects (0), Uninfected, NoEffect, BlackKnight)
EF_INVINCIBLE = 0x8000  # (its HealthManager's invincible at first)
EF_CONTACT = 0x4000  # (off till the Knight touches its parent's trigger: ActivateChildrenOnContact; ET_CONTACT)
# the layers the nail's slashes touch (Physics2D's collision matrix, layer 17 Attack)
ATTACK_HITS = {3, 6, 7, 8, 11, 12, 17, 19, 20, 21, 25, 31}
MAX_GROUPS = 256
# masks (the unmasker, remasker and remasker_inverse FSMs)
MK_SECRET, MK_REMASK, MK_SIMPLE = 1, 2, 4
# flags
CL_PREVENT_UP, CL_PREVENT_DOWN, CL_MAX_PRIORITY = 1, 2, 4
G_DOOR, G_ENTER_RIGHT, G_ENTER_LEFT, G_DONT_WALK_OUT, G_NON_HAZARD, G_HARD_LAND, G_ENTRY_ONLY = 1, 2, 4, 8, 16, 32, 64
FACING_RIGHT = 1
REC = "<BBBBHHffffffffHH"   # type, flags, group, group2, a, persist, box (x0, y0, x1, y1), p0..p3, s0, s1
NO_PERSIST = 0xFFFF


class Persist:
    """The objects whose state a save keeps (PersistentBoolItem): one bit each (or a few: a number)."""
    def __init__(self):
        self.keys, self.n, self.bits, self.semi = {}, 0, [], []

    def id(self, room, path, bits=1, semi=False):
        """semi: semi persistent (its bits cleared as the Knight rests at a bench or dies)"""
        if (room, path) not in self.keys:
            self.keys[(room, path)] = self.n
            self.bits.append(bits)
            if semi:
                self.semi += range(self.n, self.n + bits)
            self.n += bits
            assert self.n <= 1024, self.n   # (src/game.h: MAX_PERSIST)
        return self.keys[(room, path)]

    def blob(self):
        """PHASH: each bit's name (a CRC of its room, object and bit), as saves keep them: the same objects across
        versions of the game's data"""
        import zlib
        out = [0] * self.n
        for (room, path), first in self.keys.items():
            n = self.bits[list(self.keys).index((room, path))]
            for k in range(n):
                out[first + k] = zlib.crc32(("%s|%s|%d" % (room, path, k)).encode()) or 1
        assert len(set(out)) == len(out)
        return struct.pack("<I%dI" % len(out), len(out), *out)


class Strings:
    def __init__(self):
        self.list, self.index = [], {}

    def id(self, s):
        if s not in self.index:
            self.index[s] = len(self.list)
            self.list.append(s)
        return self.index[s]

    def blob(self):
        offs, body = [], bytearray()
        for s in self.list:
            offs.append(len(body))
            body += s.encode() + b"\0"
        return struct.pack("<I", len(self.list)) + b"".join(struct.pack("<I", o) for o in offs) + bytes(body)


# (the world's sprites: debris, props, the scripts' objects; a little less fine, to fit the flash)
PIECE_RES = float(os.environ.get("HK_PIECE_RES", "0.7"))


class Sprites:
    """Unity sprites objects show as actors (debris...): pack.py makes them like the actors' frames, after those."""
    def __init__(self):
        self.list, self.index, self.base = [], {}, 0

    def id(self, d, ref, scale, res=1.0, world=False):
        """(res: its texture's resolution, as a part of the screen's: smooth sprites can be stretched; world: one of
        the world's, PIECE_RES)"""
        return self._add((d["level"], tuple(d["externals"]), ref[0], ref[1], round(scale, 3), res * (PIECE_RES if world else 1)))

    def tk2d(self, path, col, name, scale=1.0, res=1.0):
        """A 2D Toolkit sprite of a collection (file, path id), by name."""
        return self._add(("tk2d", path, col, name, round(scale, 3), res))

    def _add(self, key):
        if key not in self.index:
            self.index[key] = len(self.list)
            self.list.append(key)
        return self.base + self.index[key]


def _colliders(o):
    return [c for c in o["c"] if c["type"] in ("BoxCollider2D", "PolygonCollider2D", "CircleCollider2D", "EdgeCollider2D")
            and c.get("v") and c["v"].get("m_Enabled", 1)]


HB_BOUNCE, HB_RECOIL = 1, 2


def _hit_boxes(o, by, depth=2):
    """Where the nail hits an object: its colliders, and its children's and grandchildren's (HitTaker looks three levels
    up from what it touched), on the layers the slashes touch -> [(box, HB_* flags)]: whether a down slash bounces off
    it, whether a slash recoils the Knight (NailSlash: enemies, attacks, interactive objects without NonBouncer)."""
    out = []
    if o["active"] and o["layer"] in ATTACK_HITS:
        nb = any(c.get("class") == "NonBouncer" and (c.get("v") or {}).get("active", True) for c in o["c"])
        fl = 0
        if not nb and o["layer"] in (11, 17, 19):
            fl |= HB_BOUNCE
        if not nb and o["layer"] == 11:
            fl |= HB_RECOIL
        out += [(b, fl) for b in (_box(o, c) for c in _colliders(o) if c["type"] != "EdgeCollider2D") if b]
    if depth:
        for ch in o.get("children", []):
            out += _hit_boxes(by[ch], by, depth - 1)
    return out


def _box(o, c):
    """A collider's world bounds (box colliders; others by their points)."""
    v = c["v"]
    m = np.array(o["m3"]).reshape(3, 3)
    p = np.array(o["pos"])
    off = (v["m_Offset"]["x"], v["m_Offset"]["y"])
    if c["type"] == "BoxCollider2D":
        sx, sy = v["m_Size"]["x"] / 2, v["m_Size"]["y"] / 2
        pts = [(-sx, -sy), (sx, -sy), (sx, sy), (-sx, sy)]
    elif c["type"] == "PolygonCollider2D":
        paths = v["m_Points"]["m_Paths"] if isinstance(v["m_Points"], dict) else v["m_Points"]
        pts = [(q["x"], q["y"]) for path in paths for q in path]
    elif c["type"] == "CircleCollider2D":
        r = v["m_Radius"]
        pts = [(-r, -r), (r, r)]
    else:
        return None
    w = [m @ np.array([x + off[0], y + off[1], 0.0]) + p for x, y in pts]
    xs, ys = [q[0] for q in w], [q[1] for q in w]
    return (min(xs), min(ys), max(xs), max(ys))


def _shape(o, c):
    """A collider's outline in world units (a box's four corners, a polygon's first path)."""
    v = c["v"]
    m = np.array(o["m3"]).reshape(3, 3)
    p = np.array(o["pos"])
    off = (v["m_Offset"]["x"], v["m_Offset"]["y"])
    if c["type"] == "BoxCollider2D":
        sx, sy = v["m_Size"]["x"] / 2, v["m_Size"]["y"] / 2
        pts = [(-sx, -sy), (sx, -sy), (sx, sy), (-sx, sy)]
    elif c["type"] == "PolygonCollider2D":
        paths = v["m_Points"]["m_Paths"] if isinstance(v["m_Points"], dict) else v["m_Points"]
        pts = [(q["x"], q["y"]) for q in paths[0]]
    else:
        return None
    return [tuple((m @ np.array([x + off[0], y + off[1], 0.0]) + p)[:2]) for x, y in pts]


def _is_axis_box(pts):
    xs, ys = sorted(set(round(q[0], 4) for q in pts)), sorted(set(round(q[1], 4) for q in pts))
    return len(pts) == 4 and len(xs) == 2 and len(ys) == 2


def _triggers(o):
    """The boxes of the object's trigger colliders (the events come from any of them)."""
    return [_box(o, c) for c in o["c"] if c["type"] in ("BoxCollider2D", "PolygonCollider2D", "CircleCollider2D") and
            c.get("v") and c["v"].get("m_IsTrigger") and c["v"].get("m_Enabled", 1)]


def _trigger(o):
    t = _triggers(o)
    return t[0] if t else None


def _more_boxes(o):
    """Records for the object's other trigger boxes: they follow its own."""
    return [rec(ENT_BOX, box=b) for b in _triggers(o)[1:]]


def _outline(o):
    """After a trigger's record and the boxes that follow it: its own collider's outline when that is a polygon, not a
    box (ENT_SHAPE: 4 points a record, the last repeated; src/game.c: triggers_tick)."""
    c = next((c for c in o["c"] if c["type"] in ("BoxCollider2D", "PolygonCollider2D", "CircleCollider2D") and
              c.get("v") and c["v"].get("m_IsTrigger") and c["v"].get("m_Enabled", 1)), None)
    pts = _shape(o, c) if c is not None and c["type"] == "PolygonCollider2D" else None
    out = []
    if pts and not _is_axis_box(pts):
        assert len(pts) <= 16, o["path"]
        for i in range(0, len(pts), 4):
            q = pts[i:i + 4] + [pts[-1]] * (4 - len(pts[i:i + 4]))
            out.append(rec(ENT_SHAPE, box=(q[0][0], q[0][1], q[1][0], q[1][1]), p=(q[2][0], q[2][1], q[3][0], q[3][1])))
    return out


def rec(type_, flags=0, box=(0, 0, 0, 0), p=(0, 0, 0, 0), a=0, group=0, group2=0, persist=NO_PERSIST, s0=0, s1=0):
    return struct.pack(REC, type_, flags, group, group2, a, persist, *box, *p, s0, s1)


# a door's prompts (src/game.c: door_labels)
DOOR_PROMPTS = ("Enter", "Ascend", "Descend")

# PlayerData bools whose names are not camel case
PD_RAW_NAMES = {"PDF_HORNET_F19": "hornet_f19", "PDF_HAS_MARKER_B": "hasMarker_b", "PDF_HAS_MARKER_R": "hasMarker_r",
                "PDF_HAS_MARKER_Y": "hasMarker_y", "PDF_HAS_MARKER_W": "hasMarker_w"}


def pd_flags():
    """PlayerData's bools as the game numbers them (src/game.h: PDF_*), by their names in the game ("falseKnightDefeated")"""
    import os, re
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "game.h")).read()
    names = re.sub(r"/\*.*?\*/", "", re.search(r"enum \{\s*(PDF_AT_BENCH[^}]*)\}", src).group(1), flags=re.S)
    ids = [n.strip() for n in names.replace("\n", " ").split(",") if n.strip() and n.strip() != "PDF_COUNT"]
    camel = lambda n: "".join(w.capitalize() for w in n.lower().split("_"))
    out = {}
    for i, n in enumerate(ids):
        if n in PD_RAW_NAMES:
            out[PD_RAW_NAMES[n]] = i
            continue
        m = re.match(r"PDF_(GOT|EQUIPPED)_CHARM_(\d+)$", n)
        if m:
            out[("gotCharm_" if m.group(1) == "GOT" else "equippedCharm_") + m.group(2)] = i
            continue
        # (Cornifer's: "corn_greenpathLeft")
        c = camel(n[9:]) if n.startswith("PDF_CORN_") else camel(n[4:])
        out[("corn_" if n.startswith("PDF_CORN_") else "") + c[0].lower() + c[1:]] = i
    return out


_FOUND = {}


def _found_names(d):
    """The names a scene's scripts find objects by (FindGameObject)."""
    key = id(d)
    if key not in _FOUND:
        out = set()
        for o in d["objects"]:
            for c in o["c"]:
                for st in (c.get("fsm") or {}).get("states", []):
                    for a in st["actions"]:
                        if a["name"] == "FindGameObject":
                            out.add(dict(a["params"]).get("objectName"))
        _FOUND[key] = (d, out)
    return _FOUND[key][1]


def _fsm(o, names):
    for c in o["c"]:
        f = c.get("fsm")
        if f and f.get("name") in names:
            return f
    return None


def _state(f, name):
    for st in f["states"]:
        if st["name"] == name:
            return st
    return None


def _param(action, key):
    for k, v in action["params"]:
        if k == key:
            return v
    return None


def _value(f, v, default=0.0):
    """A parameter: a number, or an FSM variable ("$Name")."""
    if isinstance(v, str) and v.startswith("$"):
        var = f.get("vars", {}).get(v[1:])
        return float(var[1]) if var and isinstance(var[1], (int, float)) else default
    return float(v) if isinstance(v, (int, float)) else default


def _fade_time(f, state):
    st = _state(f, state)
    for a in (st["actions"] if st else []):
        if a["name"] == "iTweenFadeTo" and a.get("enabled", True):
            return _value(f, _param(a, "time"), 0.5)
    return 0.5


def _owner_alpha(f, state):
    """The alpha the state fades its owner to (-1: none)."""
    st = _state(f, state)
    for a in (st["actions"] if st else []):
        if a["name"] == "iTweenFadeTo" and a.get("enabled", True) and _param(a, "gameObject") == "Owner":
            return _value(f, _param(a, "alpha"), 0)
    return -1.0


def _mask(o, f, objs_by_id):
    """-> (kind, fade time, pause, p2, p3, inverse child id) or None: p2, p3 the trigger kind (simple masks), or the
    alphas Idle and Fade Out give the owner (remaskers)."""
    names = [st["name"] for st in f["states"]]
    inverse = None
    for cid in o.get("children", []):
        if objs_by_id.get(cid, {}).get("name") == "Inverse Mask":
            inverse = cid
    pause = 0.0
    ps = _state(f, "Pause")
    if ps:
        for a in ps["actions"]:
            if a["name"] == "Wait" and a.get("enabled", True):
                pause = _value(f, _param(a, "time"), 0)
    if "Idle Stay" in names:
        return MK_SECRET, _fade_time(f, "Fade"), pause, 1, 0, None
    if "Fade Out" in names and "Fade In" in names:
        # (what Idle and Fade Out fade the owner to; Fade In the other way; an inverse mask child the other way still)
        return MK_REMASK, _fade_time(f, "Fade Out"), pause, _owner_alpha(f, "Idle"), _owner_alpha(f, "Fade Out"), inverse
    if "Fade" in names:
        trig = 3   # (no trigger: another object uncovers it)
        idle = _state(f, f["start"])
        for a in (idle["actions"] if idle else []):
            if a["name"] == "Trigger2dEvent" and a.get("enabled", True):
                trig = int(_param(a, "trigger") or 0)
        return MK_SIMPLE, _fade_time(f, "Fade"), 0.0, trig, 0, None
    return None


def _pieces(d, o, ids, by_id, sprites):
    """Debris an object flings (inactive children with a sprite and a Rigidbody2D) -> ENT_PIECE records: where each is
    from the object, its sprite, how it falls, bounces and spins."""
    import scene
    out = []
    for pid in ids:
        q = by_id.get(pid)
        if q is None:
            continue
        sr = next((c for c in q["c"] if c["type"] == "SpriteRenderer" and (c.get("v") or {}).get("m_Sprite")), None)
        if sr is None:
            continue
        v = sr["v"]
        m = np.array(q["m3"]).reshape(3, 3)
        sx, sy = float(np.hypot(m[0, 0], m[1, 0])), float(np.hypot(m[0, 1], m[1, 1]))
        mirror = -1.0 if np.linalg.det(m[:2, :2]) < 0 else 1.0
        rot = float(np.degrees(np.arctan2(m[1, 0], m[0, 0]))) if mirror > 0 else float(np.degrees(np.arctan2(-m[1, 0], -m[0, 0])))
        rb = next((c.get("v") or {} for c in q["c"] if c["type"] == "Rigidbody2D"), {})
        ob = next((c.get("v") or {} for c in q["c"] if c.get("class") == "ObjectBounce"), None)
        sp = next((c.get("v") or {} for c in q["c"] if c.get("class") == "SpinSelf"), None)
        sid = sprites.id(d, v["m_Sprite"], max(sx, sy), world=True)
        dx, dy = q["pos"][0] - o["pos"][0], q["pos"][1] - o["pos"][1]
        layer = scene.layer_index(v.get("m_SortingLayerID", 0))
        out.append(rec(ENT_PIECE, 1 if sp is not None else 0, (dx, dy, rot, q["pos"][2]),
                       (rb.get("m_GravityScale", 1), ob.get("bounceFactor", 0) if ob else -1,
                        sp.get("spinFactor", -7.5) if sp else 0, mirror),
                       a=v.get("m_SortingOrder", 0) + 32768, group=layer, s0=sid))
    return out


def _ref_file(d, ref):
    """A scene's (or prefab's) reference -> (asset file, path id)."""
    fid, pid = ref
    return (d["level"] if fid == 0 else d["externals"][fid - 1].split("/")[-1], pid)


def _alert_range(q):
    """An alert range's trigger (circle or box) -> (cx, cy, r or hx, hy or -1) in its owner's units, or None."""
    k = (abs(q["lscale"][0]), abs(q["lscale"][1]))
    for c in q["c"]:
        v = c.get("v")
        if not v or not v.get("m_Enabled", 1):
            continue
        if c["type"] == "CircleCollider2D":
            r = v["m_Radius"] * max(k)
            return (q["lpos"][0] + v["m_Offset"]["x"] * k[0], q["lpos"][1] + v["m_Offset"]["y"] * k[1], r, -1)
        if c["type"] == "BoxCollider2D":
            return (q["lpos"][0] + v["m_Offset"]["x"] * k[0], q["lpos"][1] + v["m_Offset"]["y"] * k[1],
                    v["m_Size"]["x"] / 2 * k[0], v["m_Size"]["y"] / 2 * k[1])
    return None


def _enemy(o, by_id, persist, name, d=None, strings=None, owners=None):
    """An enemy -> its records: ENT_OBJ (OK_ENEMY: its kind, place (x, y, z, x scale), hp, damage, small and medium
    geo; large geo in group; s0 how many records follow), then ENT_BOX records each tagged (flags, ET_*): its
    collider, alert ranges, Walker, Recoil, corpse, the terrain it holds (its subtree's colliders: a first, group how
    many); or None."""
    an = next((c.get("v") for c in o["c"] if c.get("class") == "tk2dSpriteAnimator" and c.get("v")), None)
    hm = next((c.get("v") for c in o["c"] if c.get("class") == "HealthManager" and c.get("v")), None)
    if not an or not hm:
        return None
    kind, fsm = None, {}
    lib = _ref_file(d, an["library"]) if d is not None else ("", an["library"][1])
    for c in o["c"]:
        f = c.get("fsm")
        key = (f["name"] if f else c.get("class"),) + lib
        if key in ENEMIES:
            kind, fsm = ENEMIES[key], f or {}
    if kind is None:
        return None
    dh = next((c.get("v") or {} for c in o["c"] if c.get("class") == "DamageHero"), {})
    box = next((c["v"] for c in o["c"] if c["type"] == "BoxCollider2D" and c.get("v") and not c["v"].get("m_IsTrigger")), None)
    if box is None and fsm.get("name") in TRIGGER_BODIES:
        # (a body that is a trigger: touches nothing, the nail still hits it)
        box = next((c["v"] for c in o["c"] if c["type"] == "BoxCollider2D" and c.get("v")), None)
    if box is None and fsm.get("name") in FRAME_BODIES:
        # (no collider but its frames': 2D Toolkit sets it as they show)
        box = {"m_Offset": {"x": 0, "y": 0}, "m_Size": {"x": 0, "y": 0}}
    if box is None:
        return None
    sx, sy = o["lscale"][0], o["lscale"][1]
    var = fsm.get("vars", {})
    fl = 0
    if (var.get("First Crawler") or var.get("Start Alert") or var.get("startAlert") or [0, False])[1]:
        fl |= EF_START
    if (var.get("Startles") or [0, False])[1]:
        fl |= EF_STARTLES
    if hm.get("battleScene"):
        fl |= EF_BATTLE
    if hm.get("invincible"):
        fl |= EF_INVINCIBLE
    fc = next((c for c in o["c"] if c.get("fsm") is fsm), None) if fsm else None
    if fc is not None and fc.get("enabled") is False and any(c.get("class") == "FSMActivator" for c in o["c"]):
        fl |= EF_DORMANT
    # (its death's effects: EnemyDeathEffects' enemyDeathType, and which of its kinds runs them)
    dcls = next((c.get("class") for c in o["c"] if (c.get("class") or "").startswith("EnemyDeathEffects")), None)
    dv = next((c.get("v") for c in o["c"] if c.get("class") == dcls and c.get("v")), None) or {}
    fl |= (dv.get("enemyDeathType", 0) & 15) << EF_DEATH_SHIFT
    fl |= {"EnemyDeathEffectsUninfected": 1, "EnemyDeathEffectsNoEffect": 2, "EnemyDeathEffectsBlackKnight": 3}.get(dcls, 0) << 12
    rb = next((c["v"] for c in o["c"] if c["type"] == "Rigidbody2D" and c.get("v")), {})
    # (p1: its rotation in quarter turns, p2: its y scale's sign; p3: a kinematic body)
    rq = int(round(math.degrees(2 * math.atan2(o["lrot"][2], o["lrot"][3])) / 90)) % 4
    out = [rec(ENT_BOX, ET_COLLIDER, box=(box["m_Offset"]["x"] * abs(sx), box["m_Offset"]["y"] * abs(sy),
                                          box["m_Size"]["x"] / 2 * abs(sx), box["m_Size"]["y"] / 2 * abs(sy)),
               p=(rb.get("m_GravityScale", 1), rq, -1 if sy < 0 else 1, 1 if rb.get("m_BodyType") == 1 else 0))]
    # its alert ranges (children with AlertRange): "Alert Range New" the main one, others by name
    for ch in o.get("children", []):
        q = by_id[ch]
        if not any(c.get("class") == "AlertRange" for c in q["c"]) and \
                q["name"] not in ("Alert Range New", "Unalert Range", "Wake Region") + RANGES.get(fsm.get("name"), ()):
            continue
        r = _alert_range(q)
        if r is None:
            continue
        # (p0, p1: its scale, for an FSM that changes it)
        k = (abs(q["lscale"][0]), abs(q["lscale"][1]), 0, 0)
        if q["name"] == "Alert Range New":
            out.append(rec(ENT_BOX, ET_ALERT, box=r, p=k))
        else:
            out.append(rec(ENT_BOX, ET_RANGE, box=r, p=k, s0=strings.id(q["name"]) if strings else 0))
    # its attacks: children that hurt the Knight (DamageHero), their outlines in its own units (x as if facing its
    # scale's way), then their shapes' points; a: on at first; s0: the child's name
    # (and the same for a range the Knight is in or not (Battle Range: HERO ENTER, HERO EXIT), no damage)
    sgn = -1.0 if o["lscale"][0] < 0 else 1.0
    for ch in o.get("children", []):
        q = by_id[ch]
        qd = next((c.get("v") for c in q["c"] if c.get("class") == "DamageHero" and c.get("v") is not None), None)
        qc = next((c for c in q["c"] if c["type"] in ("BoxCollider2D", "PolygonCollider2D") and c.get("v") and
                   c["v"].get("m_IsTrigger") and (c["v"].get("m_Enabled", 1) or fsm.get("name") in HITBOXES_OFF)), None)
        zone = q["name"] == "Battle Range"
        if (qd is None and not zone) or qc is None:
            continue
        pts = [((x - o["pos"][0]) * sgn, y - o["pos"][1]) for x, y in _shape(q, qc)][:8]
        xs, ys = [t[0] for t in pts], [t[1] for t in pts]
        nrec = (len(pts) + 3) // 4
        qd = qd or {"damageDealt": 0, "hazardType": 0}
        out.append(rec(ENT_BOX, ET_ZONE if zone else ET_HITBOX, box=(min(xs), min(ys), max(xs), max(ys)),
                       p=(len(pts), qd.get("damageDealt", 1), qd.get("hazardType", 1), 0),
                       a=1 if q["self_active"] and qc["v"].get("m_Enabled", 1) else 0,
                       group=nrec, s0=strings.id(q["name"]) if strings else 0))
        for i in range(nrec):
            t = pts[4 * i:4 * i + 4] + [pts[-1]] * (4 - len(pts[4 * i:4 * i + 4]))
            out.append(rec(ENT_SHAPE, box=(t[0][0], t[0][1], t[1][0], t[1][1]), p=(t[2][0], t[2][1], t[3][0], t[3][1])))
    w = next((c.get("v") for c in o["c"] if c.get("class") == "Walker" and c.get("v")), None)
    if w:
        wf = (1 if w.get("pauses") else 0) | (2 if w.get("ignoreHoles") else 0) | \
            (4 if w.get("preventTurningToFaceHero") or not w.get("alertRange") else 0) | \
            (8 if w.get("startInactive") else 0) | \
            (16 if w.get("ambush") else 0) | (32 if w.get("waitForHeroX") else 0) | (64 if w.get("preventTurn") else 0) | \
            (128 if w.get("preventScaleChange") else 0) | (256 if w.get("rightScale", 1) < 0 else 0)
        out.append(rec(ENT_BOX, ET_WALKER, box=(w.get("walkSpeedL", 0), w.get("walkSpeedR", 0), w.get("pauseTimeMin", 0),
                                                 w.get("pauseTimeMax", 0)),
                       p=(w.get("pauseWaitMin", 0), w.get("pauseWaitMax", 0), w.get("turnPause", 0), w.get("edgeXAdjuster", 0)),
                       group=w.get("turnAfterIdlePercentage", 0), a=wf,
                       s1=max(0, min(65535, int(round(w.get("waitHeroX", 0) * 16))))))
    cl = next((c.get("v") for c in o["c"] if c.get("class") == "Climber" and c.get("v")), None)
    if cl:
        # (Climber: speed, spin time, wall ray padding, min turn distance; start right; its start angle's quarter)
        ang = int(round(_angle(o) / 90)) % 4
        out.append(rec(ENT_BOX, ET_VARS, p=(cl.get("speed", 2), cl.get("spinTime", 0.25), cl.get("wallRayPadding", 0.1),
                                            cl.get("minTurnDistance", 0.25)), a=1 if cl.get("startRight", 1) else 0, s1=ang))
    nums, bools = ENEMY_VARS.get(fsm.get("name"), ([], []))
    if nums or bools:
        # (up to eight numbers: p0..p3, then the box's)
        vals = [float((var.get(k) or [0, 0])[1] or 0) for k in nums] + [0.0] * (8 - len(nums))
        bits = sum(1 << i for i, k in enumerate(bools) if (var.get(k) or [0, False])[1])
        if fsm.get("name") == "Zombie Swipe" and not _state(fsm, "Coward"):
            bits |= 2   # (an older Zombie Swipe: Ready deaf to TOOK DAMAGE, Reset plays Idle)
        out.append(rec(ENT_BOX, ET_VARS, p=tuple(vals[:4]), box=tuple(vals[4:8]), a=bits))
    if fsm.get("name") == "Moss Walker":
        # (its children's places, in its own units: Edge Range, Wall Range, Ground Range; Roams)
        kid = {by_id[ch]["name"]: by_id[ch]["lpos"] for ch in o.get("children", [])}
        e, w, g = (kid.get(k, [0, 0]) for k in ("Edge Range", "Wall Range", "Ground Range"))
        out.append(rec(ENT_BOX, ET_VARS, p=(e[0], e[1], w[0], w[1]), box=(g[0], g[1], 0, 0),
                       a=1 if (var.get("Roams") or [0, False])[1] else 0))
    if fsm.get("name") == "Moss Knight Control":
        # (Dormant, Lakeside; its Wake Box's Start Battle)
        wb = next((by_id[ch] for ch in o.get("children", []) if by_id[ch]["name"] == "Wake Box"), None)
        wf = next((c["fsm"] for c in wb["c"] if c.get("fsm")), {}) if wb else {}
        bits = (1 if (var.get("Dormant") or [0, False])[1] else 0) | (2 if (var.get("Lakeside") or [0, False])[1] else 0) | \
            (4 if (wf.get("vars", {}).get("Start Battle") or [0, False])[1] else 0)
        out.append(rec(ENT_BOX, ET_VARS, a=bits))
    if fsm.get("name") == "Mozzie":
        # (its TileDetector: a second box for the terrain, in its own units)
        q = next((by_id[ch] for ch in o.get("children", []) if by_id[ch]["name"] == "TileDetector"), None)
        bv = next((c["v"] for c in q["c"] if c["type"] == "BoxCollider2D" and c.get("v")), None) if q else None
        if bv:
            k = (abs(q["lscale"][0]), abs(q["lscale"][1]))
            out.append(rec(ENT_BOX, ET_VARS, box=(q["lpos"][0] + bv["m_Offset"]["x"] * k[0], q["lpos"][1] + bv["m_Offset"]["y"] * k[1],
                                                  bv["m_Size"]["x"] / 2 * k[0], bv["m_Size"]["y"] / 2 * k[1])))
    if fsm.get("name") == "Fungus Zombie Attack":
        # (its gas's hitbox grows from where its child is, from its scale then)
        q = next((by_id[ch] for ch in o.get("children", []) if by_id[ch]["name"] == "Gas Hit Box"), None)
        if q:
            out.append(rec(ENT_BOX, ET_VARS, p=(q["lpos"][0] * abs(sx), q["lpos"][1] * abs(sy), q["lscale"][0], 0)))
    if fsm.get("name") == "Big Fly Control" and d is not None:
        # (Gruz Mother: where her young wait, Fly Spawn; her burster brings them there)
        fs = next((q for q in d["objects"] if q["name"] == "Fly Spawn"), None)
        if fs:
            out.append(rec(ENT_BOX, ET_VARS, p=(fs["pos"][0], fs["pos"][1], 0, 0)))
    rc = next((c.get("v") for c in o["c"] if c.get("class") == "Recoil" and c.get("v")), None)
    if rc:
        out.append(rec(ENT_BOX, ET_RECOIL, box=(rc.get("recoilSpeedBase", 15), rc.get("recoilDuration", 0.15), 0, 0),
                       a=(1 if rc.get("stopVelocityXWhenRecoilingUp") else 0) | (2 if rc.get("freezeInPlace") else 0) |
                       (4 if rc.get("preventRecoilUp") else 0)))
    de = dv
    if de and de.get("corpsePrefab") and d is not None:
        try:
            import unity
            cf, cp = _ref_file(d, de["corpsePrefab"])
            cd = unity.prefab(cf, cp)
            co = cd["objects"][0]
            cb = next((c["v"] for c in co["c"] if c["type"] == "BoxCollider2D" and c.get("v")), None)
            rb = next((c["v"] for c in co["c"] if c["type"] == "Rigidbody2D" and c.get("v")), {})
            ob = next((c["v"] for c in co["c"] if c.get("class") == "ObjectBounce" and c.get("v")), None)
            cc = next((c for c in co["c"] if (c.get("class") or "").startswith("Corpse")), None)
            cv = (cc or {}).get("v") or {}
            k = (abs(co["lscale"][0]), abs(co["lscale"][1]))
            sp = de.get("corpseSpawnPoint") or {"y": 0}
            cfl = (1 if cv.get("breaker") else 0) | (2 if de.get("corpseFacesRight") else 0) | \
                (4 if de.get("lowCorpseArc") else 0) | (8 if cv.get("instantChunker") else 0) | (16 if cv.get("massless") else 0)
            # (a corpse with no collider falls through all: flag 32)
            box = (cb["m_Offset"]["x"] * k[0], cb["m_Offset"]["y"] * k[1], cb["m_Size"]["x"] / 2 * k[0],
                   cb["m_Size"]["y"] / 2 * k[1]) if cb else (0, 0, 0, 0)
            out.append(rec(ENT_BOX, ET_CORPSE, box=box, p=(rb.get("m_GravityScale", 1), ob.get("bounceFactor", -1) if ob else -1,
                                                           de.get("corpseFlingSpeed", 15), sp.get("y", 0)),
                           a=cfl | (0 if cb else 32)))
        except Exception as e:
            print("ents: %s: corpse %s" % (o["path"], e))
    if owners:
        import coll
        c0, cn = _subcols(d, owners, o["id"])
        if cn:
            out.append(rec(ENT_BOX, ET_TERRAIN, a=c0, group=cn))
    head = rec(ENT_OBJ, OK_ENEMY, (o["pos"][0], o["pos"][1], o["pos"][2], sx),
               (hm.get("hp", 1), dh.get("damageDealt", 0), hm.get("smallGeoDrops", 0), hm.get("mediumGeoDrops", 0)),
               a=kind, group=hm.get("largeGeoDrops", 0), persist=_persist(persist, name, o), s0=len(out), s1=fl)
    return [head] + out


def _persist(persist, name, o):
    """An object's PersistentBoolItem -> its bit in the save (NO_PERSIST: none). Semi persistent: cleared as the Knight
    rests at a bench, dies or is loaded at one (ResetSemiPersistentItems)."""
    pbi = next((c.get("v") or {} for c in o["c"] if c.get("class") == "PersistentBoolItem"), None)
    return persist.id(name, o["path"], semi=bool(pbi.get("semiPersistent"))) if pbi is not None else NO_PERSIST


COL_BASE = [0]   # (a scene loaded with another: its colliders numbered after that one's)


def _subcols(d, owners, oid):
    import coll
    c0, cn = coll.subtree_colliders(d, owners, oid)
    return (c0 + COL_BASE[0], cn) if cn else (0, 0)


def _tk2d_sprite(d, o):
    """An object's tk2dSprite -> (its collection's file, path id, the sprite's name) or None."""
    import tk2d, unity
    v = next((c.get("v") for c in o["c"] if c.get("class") == "tk2dSprite" and c.get("v")), None)
    if not v or not v.get("collection"):
        return None
    fid, pid = v["collection"]
    path = d["externals"][fid - 1] if fid else d["level"]
    col = tk2d.collection(path, pid)
    return path, pid, unity.S(col["spriteDefinitions"][v["_spriteId"]]["name"])


def _angle(o):
    m = np.array(o["m3"]).reshape(3, 3)
    return float(np.degrees(np.arctan2(m[1, 0], m[0, 0])))


def _props(d, o, by_id, rooms, strings, persist, sprites, owners, name):
    """Objects run by their own FSMs (src/obj.c) -> records, or None."""
    import coll
    var = lambda f, k, dflt: (f.get("vars", {}).get(k) or [0, dflt])[1]
    hits = _hit_boxes(o, by_id)
    c0, cn = _subcols(d, owners, o["id"]) if owners else (0, 0)
    persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
    f = _fsm(o, ("Great Door",))
    if f:
        sp = _tk2d_sprite(d, o)
        ids = [sprites.tk2d(sp[0], sp[1], "door_v%02d" % k) for k in (1, 2, 3)]
        return [rec(ENT_OBJ, OK_GREAT_DOOR, (o["pos"][0], o["pos"][1], o["pos"][2], 1), (ids[0], ids[1], ids[2],
                    rooms.index("Town") if "Town" in rooms else -1), a=c0, group=cn, s0=strings.id("left1"), s1=len(hits),
                    persist=persist.id(name, o["path"]) if persistent else NO_PERSIST)] + \
            [rec(ENT_BOX, fl, box=b) for b, fl in hits]
    f = _fsm(o, ("Geo Rock",))
    if f:
        variant = 2 if str(var(f, "Gleam Anim", "Gleam 1")).endswith("2") else 1
        return [rec(ENT_OBJ, OK_GEO_ROCK, (o["pos"][0], o["pos"][1], o["pos"][2], _angle(o)),
                    (var(f, "Hits", 5), var(f, "Geo Per Hit", 2), var(f, "Final Payout", 5), variant), a=c0, group=cn,
                    s1=len(hits), persist=persist.id(name, o["path"], 4))] + [rec(ENT_BOX, fl, box=b) for b, fl in hits]
    f = _fsm(o, ("Chest Control",))
    if f:
        # (opened: its Opened child's front and back sprites; inside, geo or a shiny item)
        opened = [q for q in (by_id[c] for c in o.get("children", [])) if q["name"] == "Opened"]
        parts = []
        for q in (by_id[c] for c in opened[0].get("children", [])) if opened else []:
            sp = _tk2d_sprite(d, q)
            if sp:
                parts.append(rec(ENT_PIECE, 0, (q["lpos"][0], q["lpos"][1], 0, o["pos"][2] + q["lpos"][2]),
                                 s0=sprites.tk2d(*sp)))
        sp = _tk2d_sprite(d, o)
        shiny = 0
        for q in d["objects"]:
            if q["path"].startswith(o["path"] + "/Item/"):
                sf = _fsm(q, ("Shiny Control",))
                if sf and var(sf, "Charm", False):
                    shiny = var(sf, "Charm ID", 0)
        return [rec(ENT_OBJ, OK_CHEST, (o["pos"][0], o["pos"][1], o["pos"][2], shiny),
                    (var(f, "Geo Small", 0), var(f, "Geo Med", 0), var(f, "Geo Large", 0), len(parts)), a=c0, group=cn,
                    s0=sprites.tk2d(*sp) if sp else 0, s1=len(hits),
                    persist=persist.id(name, o["path"]) if persistent else NO_PERSIST)] + \
            [rec(ENT_BOX, fl, box=b) for b, fl in hits] + parts
    return None


def _go_var(d, o, f, var, by_id):
    """An FSM's GameObject variable -> the object's id (a reference, or what FindGameObject or FindChild sets it to),
    or None."""
    v = (f.get("vars") or {}).get(var)
    if v and isinstance(v[1], list) and v[1][0] == "ref" and v[1][2] in by_id:
        return v[1][2]
    for st in f["states"]:
        for a in st["actions"]:
            if not a.get("enabled", True):
                continue
            if a["name"] == "FindGameObject" and _param(a, "store") == "$" + var:
                q = next((q for q in d["objects"] if q["name"] == _param(a, "objectName") and q["active"]), None)
                if q:
                    return q["id"]
            if a["name"] in ("FindChild", "GetChild") and _param(a, "storeResult") == "$" + var:
                q = next((by_id[c] for c in o.get("children", []) if by_id[c]["name"] == _param(a, "childName")), None)
                if q:
                    return q["id"]
    return None


def _arenas(d, by_id):
    """The arenas (Battle Control, but the False Knight's) -> (the objects their Activate destroys or hides, the enemies
    they count: SetBattleScene's, those their Detect hides: there only once the fight is over)."""
    gone, counted, later = set(), set(), set()
    for o in d["objects"]:
        f = _fsm(o, ("Battle Control",)) if o["active"] else None
        if not f or "False Knight" in f["vars"]:
            continue
        act = _state(f, "Activate")
        for a in act["actions"] if act else []:
            if not a.get("enabled", True):
                continue
            if a["name"] == "DestroyObject" or a["name"] == "ActivateGameObject" and _param(a, "activate") is False:
                g = _param(a, "gameObject")
                q = _go_var(d, o, f, g[1:], by_id) if isinstance(g, str) and g.startswith("$") else None
                todo = [q] if q is not None else []
                while todo:
                    i = todo.pop()
                    gone.add(i)
                    todo += by_id[i].get("children", [])
        det = _state(f, "Detect")
        for a in det["actions"] if det else []:
            if a.get("enabled", True) and a["name"] == "ActivateGameObject" and _param(a, "activate") is False:
                g = _param(a, "gameObject")
                q = _go_var(d, o, f, g[1:], by_id) if isinstance(g, str) and g.startswith("$") else None
                if q is not None:
                    later.add(q)
        for st in f["states"]:
            for a in st["actions"]:
                if a["name"] == "SetBattleScene" and a.get("enabled", True):
                    g = _param(a, "target")
                    q = _go_var(d, o, f, g[1:], by_id) if isinstance(g, str) and g.startswith("$") else None
                    if q is not None:
                        counted.add(q)
    return gone, counted, later


CLIP_INDEX = {}   # (actors' clips: their constant -> number, as pack.py builds them)


def prop(d, o, by_id):
    """A 2D Toolkit sprite that only shows: no FSM or script runs it or anything above it, and it is no sprite cache
    (sprite_set_*) -> (the animator's library (file, path id), its default clip's name) or (None, None) when it has no
    animator; None when it is no such thing."""
    import tk2d, unity
    mbs = {c.get("class") for c in o["c"] if c["type"] == "MonoBehaviour"}
    if "tk2dSprite" not in mbs or mbs - {"tk2dSprite", "tk2dSpriteAnimator"} or any(c.get("fsm") for c in o["c"]):
        return None
    mr = next((c.get("v") for c in o["c"] if c["type"] == "MeshRenderer"), None)
    if isinstance(mr, dict) and not mr.get("m_Enabled", 1):
        return None
    p = by_id.get(o.get("parent"))
    while p is not None:
        if any(c.get("fsm") or c.get("class") in ("HealthManager", "HealthCocoon") for c in p["c"]) or \
                p["name"].startswith("sprite_set_"):
            return None
        p = by_id.get(p.get("parent"))
    an = next((c.get("v") for c in o["c"] if c.get("class") == "tk2dSpriteAnimator" and c.get("v")), None)
    if not an:
        return (None, None)
    lib = _ref_file(d, an["library"])
    clips = tk2d.animation(*lib)["clips"]
    return lib, unity.S(clips[an["defaultClipId"]]["name"])


def _sorting(o):
    """Its renderer's sorting layer (index) and order."""
    import scene
    mr = next((c.get("v") for c in o["c"] if c["type"] in ("MeshRenderer", "SpriteRenderer") and isinstance(c.get("v"), dict)),
              {}) or {}
    return scene.layer_index(mr.get("m_SortingLayerID", 0)), mr.get("m_SortingOrder", 0)


def _gate(d, o, owners):
    """A battle gate (BG Control, or a gate closed till a PlayerData bool is set) -> its record, or None."""
    f = _fsm(o, ("BG Control",))
    fp = _fsm(o, ("Control",)) if f is None and "Battle Gate" in o["name"] else None
    check = _state(fp, "Check") if fp else None
    test = next((a for a in check["actions"] if a["name"] == "PlayerDataBoolTest"), None) if check else None
    if f is None and test is None:
        return None
    fl, pd = 0, 0
    if f:
        clip = {st["name"]: next((_param(a, "clipName") for a in st["actions"]
                                  if a["name"].startswith("Tk2dPlayAnimation") and a.get("enabled", True)), None)
                for st in f["states"]}
        if (clip.get("Opened") or "").startswith("Bone"):
            fl |= BGF_BONE
        if (f["vars"].get("Start Closed") or [0, False])[1]:
            fl |= BGF_START_CLOSED
    else:
        fl |= BGF_PD | BGF_START_CLOSED
        pd = pd_flags()[_param(test, "boolName")]
    c0, cn = _subcols(d, owners, o["id"]) if owners else (0, 0)
    m = np.array(o["m3"]).reshape(3, 3)
    sx = float(np.hypot(m[0, 0], m[1, 0])) * (-1.0 if np.linalg.det(m[:2, :2]) < 0 else 1.0)
    sy = float(np.hypot(m[0, 1], m[1, 1]))
    import vm
    # (p2: its name, as the scripts' strings number it: FindGameObject)
    return rec(ENT_OBJ, OK_BGATE, (o["pos"][0], o["pos"][1], o["pos"][2], sx), (sy, pd, vm.STR.id(o["name"]), 0), a=c0,
               group=cn, s0=fl)


def room(d, rooms, strings, persist, name, sprites=None, owners=None, rec_base=0, col_base=0, group_base=0):
    """-> (packed records, {object id: render group}). owners: each collider's object (coll.room); sprites: where the
    objects' own sprites go (Sprites)."""
    import coll
    sprites = sprites or Sprites()
    recs = []
    rec_of, receivers = {}, []   # (an object's record; the records that send HIT to another object's)
    cam_rec, battles = {}, []    # (camera locks' records; arenas, to point at theirs)
    prebattle = set()
    groups = {}
    marker_index = {}
    objs = [o for o in d["objects"] if o["active"]]
    by_id = {o["id"]: o for o in d["objects"]}
    arena_gone, arena_counted, arena_later = _arenas(d, by_id)
    import state
    pdf = pd_flags()

    def conds(o):
        """(PlayerData flag, off if true) of the object and those above it, for the bools that change as one plays"""
        out = []
        while o is not None:
            out += [(pdf[n], t) for n, t in state.conditions(o) if n in state.DYNAMIC]
            o = by_id.get(o.get("parent"))
        return out
    enemy_rec_of, summons = {}, []   # (enemies' records; summoners, to point at theirs)

    def subtree(oid, g):
        groups[oid] = g
        for c in by_id[oid].get("children", []):
            subtree(c, g)
    ngroups = [group_base]
    COL_BASE[0] = col_base

    def new_group(oid):
        ngroups[0] += 1
        subtree(oid, ngroups[0])
        return ngroups[0]
    # markers first: the triggers refer to them
    for o in objs:
        for c in o["c"]:
            cls, v = c.get("class"), c.get("v") or {}
            if cls == "HazardRespawnMarker":
                marker_index[o["id"]] = len(recs)
                x, y = o["pos"][:2]
                recs.append(rec(ENT_HAZARD_MARKER, FACING_RIGHT if v.get("respawnFacingRight") else 0, (x, y, x, y),
                                s1=strings.id(o["name"])))
            elif cls == "RespawnMarker":
                x, y = o["pos"][:2]
                recs.append(rec(ENT_RESPAWN, FACING_RIGHT if v.get("respawnFacingRight") else 0, (x, y, x, y),
                                s0=strings.id(o["name"])))
        f = _fsm(o, ("Spawn Offset",))
        if f:
            # (where a shade goes when the Knight dies near: the marker and its offset; its special type)
            var = lambda k, dflt: (f.get("vars", {}).get(k) or [0, dflt])[1]
            x, y = o["pos"][0] + var("X", 0), o["pos"][1] + var("Y", 0)
            recs.append(rec(ENT_SHADE_MARKER, box=(x, y, o["pos"][0], o["pos"][1]), a=var("Special Type", 0)))
    for o in objs:
        f = _fsm(o, ("unmasker", "remasker", "remasker_inverse"))
        if f:
            m = _mask(o, f, by_id)
            box = _trigger(o) or (0, 0, 0, 0)
            if m:
                kind, fade, pause, p2, p3, inverse = m
                rec_of[o["id"]] = len(recs)
                g = new_group(o["id"])
                g2 = new_group(inverse) if inverse else 0
                persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
                import vm
                recs.append(rec(ENT_MASK, kind, box, (fade, pause, p2, p3), group=g, group2=g2,
                                persist=persist.id(name, o["path"]) if persistent else NO_PERSIST, s1=vm.STR.id(o["name"])))
                recs += _more_boxes(o) + _outline(o)
        classes = {c.get("class") for c in o["c"]}
        f = _fsm(o, ("Bench Control",))
        if f:
            # a bench: its place, the Knight's offset sitting, its prompt's place; its Lit and Light sprites' groups
            # (they glow with the Knight near); then its own trigger (Rest) and its Detect Range's
            var = lambda k, dflt: (f.get("vars", {}).get(k) or [0, dflt])[1]
            adj = var("Adjust Vector", [0, 0, 0])
            kids = {by_id[c]["name"]: by_id[c] for c in o.get("children", [])}
            pm = kids.get("Prompt Marker")
            g_lit = new_group(kids["Lit"]["id"]) if "Lit" in kids else 0
            g_light = new_group(kids["Light"]["id"]) if "Light" in kids else 0
            box = _trigger(o) or (0, 0, 0, 0)
            dr = _trigger(kids["Detect Range"]) if "Detect Range" in kids else None
            recs.append(rec(ENT_OBJ, OK_BENCH, (o["pos"][0], o["pos"][1], adj[0], adj[1]),
                            (pm["pos"][0] if pm else o["pos"][0], pm["pos"][1] if pm else o["pos"][1] + 3, 0, 0),
                            group=g_lit, group2=g_light, s0=strings.id(o["name"])))
            recs.append(rec(ENT_BOX, 0, box))
            recs.append(rec(ENT_BOX, 0, dr or box))
        f = _fsm(o, ("Battle Control",))
        if f and "False Knight" in f["vars"]:
            # the False Knight's arena: its trigger, its camera locks (fixed up below), the armour left after the fight;
            # its pre-battle enemies marked
            fv = f["vars"]
            ref = lambda k: (fv.get(k) or [0, None])[1]
            arm = ref("Armour")
            g_arm = new_group(arm[2]) if arm and arm[2] in by_id else 0
            pre = ref("Pre Battle Enemies")
            if pre and pre[2] in by_id:
                todo = list(by_id[pre[2]].get("children", []))
                while todo:
                    q = todo.pop()
                    prebattle.add(q)
                    todo += by_id[q].get("children", [])
            persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
            battles.append((len(recs), (ref("CameraLock 1") or [0, 0, 0])[2], (ref("CameraLock 2") or [0, 0, 0])[2]))
            recs.append(rec(ENT_OBJ, OK_BATTLE, _trigger(o) or (0, 0, 0, 0), (-1, -1, 1, 0), group=g_arm,
                            persist=persist.id(name, o["path"]) if persistent else NO_PERSIST))
        if f and "False Knight" not in f["vars"]:
            # an arena: its trigger (or none: an enemy starts it), its camera lock (fixed up below), how many it counts
            # down and the wait after
            acts = lambda n: [a for a in ((_state(f, n) or {}).get("actions") or []) if a.get("enabled", True)]
            sends = lambda n: [_param(a, "sendEvent") for a in acts(n) if a["name"] == "SendEventByName"]
            fl = 0
            if any(a["name"] == "Trigger2dEvent" for a in acts("Detect")):
                fl |= ARF_TRIGGER
            if "BG DESTROY" in sends("Activate"):
                fl |= ARF_DESTROY_GATES
            if "BG QUICK OPEN" in sends("Activate"):
                fl |= ARF_QUICK_OPEN
            count = next((_param(a, "intValue") for a in acts("Start") + acts("Wave 1") if a["name"] == "SetIntValue"),
                         (f["vars"].get("Battle Enemies") or [0, 0])[1])
            wait = next((_param(a, "time") for a in acts("End Wait") if a["name"] == "Wait"), 2)
            pause, next_at = 0, 0
            if _state(f, "Wave 2"):
                # (the second wave once the first is down to so many, after a pause)
                fl |= ARF_WAVES
                next_at = next((_param(a, "integer2") for a in acts("Wave 1") if a["name"] == "IntCompare"), 0)
                pause = next((_param(a, "time") for a in acts("Wave Pause") if a["name"] == "Wait"), 0)
            if _state(f, "Complete") and not _state(f, "Start"):
                # (no start: done once its count is down, the gates opening a while after)
                fl |= ARF_NO_START
                wait = next((_param(a, "delay") for a in acts("Complete") if a["name"] == "SendEventByName"), 0)
            if _state(f, "Kill Zombies"):
                print("ents: %s: %s: arena state Kill Zombies not handled" % (name, o["path"]))
            persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
            battles.append((len(recs), _go_var(d, o, f, "Camera Lock", by_id), None))
            recs.append(rec(ENT_OBJ, OK_ARENA, _trigger(o) or (0, 0, 0, 0), (-1, -1, count, wait), a=fl,
                            persist=persist.id(name, o["path"]) if persistent else NO_PERSIST,
                            s0=int(round(pause * 100)), s1=int(next_at)))
        f = _fsm(o, ("summon",))
        if f and _go_var(d, o, f, "Buzzer", by_id) is not None:
            # a summoner: its place and way; the enemy it brings (fixed up below)
            summons.append((len(recs), _go_var(d, o, f, "Buzzer", by_id)))
            recs.append(rec(ENT_OBJ, OK_SUMMON, (o["pos"][0], o["pos"][1], o["pos"][2], o["lscale"][0]), (-1, 0, 0, 0)))
        g = _gate(d, o, owners)
        if g:
            recs.append(g)
        for flag, off_if in [(pdf[n], t) for n, t in state.conditions(o) if n in state.DYNAMIC]:
            # an object off as the room loads with a PlayerData bool so: its sprites (and what is under it), its colliders
            c0, cn = _subcols(d, owners, o["id"]) if owners else (0, 0)
            recs.append(rec(ENT_OBJ, OK_COND, p=(flag, 1 if off_if else 0, 0, 0), a=c0, group=cn, group2=new_group(o["id"])))
        pr = prop(d, o, by_id)
        if pr is not None:
            # a sprite that shows (its default clip, or its sprite); its place, scale, angle, its sorting
            import actors
            lib, clip = pr
            m = np.array(o["m3"]).reshape(3, 3)
            sx = float(np.hypot(m[0, 0], m[1, 0])) * (-1.0 if np.linalg.det(m[:2, :2]) < 0 else 1.0)
            sy = float(np.hypot(m[0, 1], m[1, 1]))
            ci = CLIP_INDEX.get(actors.clip_id(actors.PROP_ACTOR[lib], clip), -1) if lib else -1
            sp = _tk2d_sprite(d, o) if ci < 0 else None
            layer, order = _sorting(o)
            recs.append(rec(ENT_OBJ, OK_PROP, (o["pos"][0], o["pos"][1], o["pos"][2], sx), (sy, _angle(o), ci, 0),
                            a=order + 32768, group=layer, s0=sprites.tk2d(*sp) if sp else 0))
        hc = next((c.get("v") for c in o["c"] if c.get("class") == "HealthCocoon" and c.get("v")), None)
        if hc is not None:
            # a lifeblood cocoon: its place, its sweat's waits; its colliders, the children it hides as it breaks (a
            # group); its hit boxes, its cap (a piece), then its splat (sprite, effect clip, their place and sorting)
            import actors
            kids = {by_id[c]["name"]: by_id[c] for c in o.get("children", [])}
            ref_ids = lambda refs: [r[1] for r in refs or [] if r and r[1] in by_id]
            hidden = ref_ids(hc.get("disableChildren"))
            g_hide = new_group(hidden[0]) if hidden else 0
            for h in hidden[1:]:
                subtree(h, g_hide)
            c0, cn = _subcols(d, owners, o["id"]) if owners else (0, 0)
            hits = _hit_boxes(o, by_id, depth=0)
            cap = ref_ids([hc.get("cap")]) if hc.get("cap") else []
            pieces = _pieces(d, o, cap, by_id, sprites)
            splat, eff = kids.get("Splat Sprite"), kids.get("Splat Effect")
            sp = _tk2d_sprite(d, splat) if splat else None
            ean = next((c.get("v") for c in eff["c"] if c.get("class") == "tk2dSpriteAnimator"), None) if eff else None
            eclip = -1
            if ean:
                import tk2d, unity
                lib = _ref_file(d, ean["library"])
                eclip = CLIP_INDEX.get(actors.clip_id("fx", unity.S(tk2d.animation(*lib)["clips"][ean["defaultClipId"]]["name"])), -1)
            layer, order = _sorting(splat) if splat else (0, 0)
            # (semi persistent: whole again once the Knight rests at a bench or dies, unless in its room then)
            recs.append(rec(ENT_OBJ, OK_COCOON, (o["pos"][0], o["pos"][1], o["pos"][2], 0),
                            (hc.get("waitMin", 2), hc.get("waitMax", 6), 0, 0), a=c0, group=cn, group2=g_hide,
                            persist=_persist(persist, name, o), s0=len(pieces), s1=len(hits)))
            recs += [rec(ENT_BOX, fl, box=b) for b, fl in hits]
            recs += pieces
            recs.append(rec(ENT_BOX, 0, (splat["lpos"][0] if splat else 0, splat["lpos"][1] if splat else 0,
                                         splat["pos"][2] if splat else 0, 0),
                            (sprites.tk2d(*sp) if sp else -1, eclip, layer, order)))
        f = _fsm(o, ("Area Title Controller",))
        if f and any(c.get("class") == "AreaTitleController" for c in o["c"]):
            # an area's title: its trigger (if it waits for one), its pauses, its area, its flags; the title; the bool
            # its first visit sets
            import text
            v = {k: x[1] for k, x in f["vars"].items()}
            ident = v.get("Area Event", "")
            num, sub, vis = AREAS[ident]
            fl = (AF_TRIGGER if v.get("Wait for Trigger") else 0) | (AF_REVISIT if v.get("Only On Revisit") else 0) | \
                 (AF_RIGHT if v.get("Display Right") else 0) | (AF_DOOR if v.get("Door Trigger") else 0) | \
                 (AF_SUB if sub else 0) | (AF_AFTER_CROSSROADS if ident == "KINGSPASS" else 0)
            box = _trigger(o) if fl & AF_TRIGGER else None
            recs.append(rec(ENT_OBJ, OK_AREA, box or (0, 0, 0, 0), (v.get("Unvisited Pause", 2), v.get("Visited Pause", 2), num, fl),
                            a=text.TITLES.index(ident), group=pdf[vis] if vis else 255,
                            s0=strings.id(v["Door Trigger"]) if v.get("Door Trigger") else 0))
        dv = next((c.get("v") for c in o["c"] if c.get("class") == "WaterDrip" and c.get("v")), None)
        if dv is not None:
            # a water drip: its place, its idle times, fall speed and how far it sinks as it hits
            layer, order = _sorting(o)
            box = next((c["v"] for c in o["c"] if c["type"] == "BoxCollider2D" and c.get("v")), None)
            recs.append(rec(ENT_OBJ, OK_DRIP, (o["pos"][0], o["pos"][1], o["pos"][2], box["m_Offset"]["y"] - box["m_Size"]["y"] / 2 if box else 0),
                            (dv.get("idleTimeMin", 2), dv.get("idleTimeMax", 8), dv.get("fallVelocity", -7),
                             dv.get("impactTranslation", -0.5)), a=order + 32768, group=layer))
        ev = next((c.get("v") for c in o["c"] if c.get("class") == "SendPlaymakerEventOnEnable" and c.get("v")), None)
        if ev:
            # (an event broadcast as the room starts)
            if ev.get("eventName") in BG_EVENTS:
                recs.append(rec(ENT_OBJ, OK_EVENT, a=BG_EVENTS.index(ev["eventName"])))
            else:
                print("ents: %s: %s: event %s not handled" % (name, o["path"], ev.get("eventName")))
        f = _fsm(o, ("Floor Control",))
        if f:
            # the False Knight's floor: the colliders it loses (Break Floor's), its whole sprites' group, then a record
            # for each sprite it shows cracked and broken, and its bits (sprite, place, the speed they are flung at)
            import coll
            kids = {by_id[c]["name"]: by_id[c] for c in o.get("children", [])}
            c0, cn = _subcols(d, owners, kids["Break Floor"]["id"]) if owners and "Break Floor" in kids else (0, 0)
            g_norm = new_group(kids["Normal 1"]["id"]) if "Normal 1" in kids else 0
            if "Normal 2" in kids:
                subtree(kids["Normal 2"]["id"], g_norm)
            extra = []
            flings = {"Floor Bit 1": (-3, 8), "Floor Bit 2": (2, 6), "Floor Bit 3": (-2, 9)}
            for k, nm in enumerate(["Cracked 1", "Cracked 2", "Broken", "Floor Bit 1", "Floor Bit 2", "Floor Bit 3"]):
                q = kids.get(nm)
                sr = q and next((c["v"] for c in q["c"] if c["type"] == "SpriteRenderer" and (c.get("v") or {}).get("m_Sprite")), None)
                if not sr:
                    continue
                import scene
                m = np.array(q["m3"]).reshape(3, 3)
                sid = sprites.id(d, sr["m_Sprite"], float(np.hypot(m[0, 0], m[1, 0])), world=True)
                vx, vy = flings.get(nm, (0, 0))
                extra.append(rec(ENT_BOX, k, (q["pos"][0], q["pos"][1], q["pos"][2], 0), (vx, vy, 0, 0),
                                 a=sr.get("m_SortingOrder", 0) + 32768, group=scene.layer_index(sr.get("m_SortingLayerID", 0)),
                                 s0=sid))
            recs.append(rec(ENT_OBJ, OK_FK_FLOOR, (o["pos"][0], o["pos"][1], 0, 0), a=c0, group=cn, group2=g_norm,
                            s0=len(extra)))
            recs += extra
        en = _enemy(o, by_id, persist, name, d, strings, owners) or _props(d, o, by_id, rooms, strings, persist, sprites, owners, name)
        if not en and any(c.get("class") == "ActivateChildrenOnContact" for c in o["c"]):
            # (its children, off till the Knight touches its trigger: an enemy, flagged, the trigger after its records)
            for ch in o.get("children", []):
                q = by_id[ch]
                en = _enemy(q, by_id, persist, name, d, strings, owners) if not q["self_active"] else None
                if en:
                    en.append(rec(ENT_BOX, ET_CONTACT, box=_trigger(o) or (0, 0, 0, 0)))
                    b = bytearray(en[0])
                    struct.pack_into("<HH", b, 40, struct.unpack_from("<H", b, 40)[0] + 1,
                                     struct.unpack_from("<H", b, 42)[0] | EF_CONTACT)
                    en[0] = bytes(b)
                    o = q
                    break
        if en:
            if en[0][1] == OK_ENEMY:
                for flag, off_if in conds(o):
                    # (gone as the room loads, with the PlayerData bool so)
                    en.append(rec(ENT_BOX, ET_COND, p=(flag, 1 if off_if else 0, 0, 0)))
                    b = bytearray(en[0])
                    struct.pack_into("<H", b, 40, struct.unpack_from("<H", b, 40)[0] + 1)
                    en[0] = bytes(b)
                enemy_rec_of[o["id"]] = len(recs)
                fl = EF_PREBATTLE if o["id"] in prebattle else 0
                fl |= EF_ARENA_LATER if o["id"] in arena_later else 0
                fl |= EF_BATTLE if o["id"] in arena_counted else 0
                fl |= EF_ARENA_GONE if o["id"] in arena_gone else 0
                fl |= EF_SPAWNED if by_id.get(o.get("parent"), {}).get("name") == "Fly Spawn" else 0
                b = bytearray(en[0])
                struct.pack_into("<H", b, 42, struct.unpack_from("<H", b, 42)[0] | fl)
                en[0] = bytes(b)
            recs += en
        br = next((c for c in o["c"] if c.get("class") == "Breakable" and c.get("v") is not None), None)
        if br is not None:
            v = br["v"]
            z = o["pos"][2]
            hits = _hit_boxes(o, by_id)
            if hits and v.get("inertForegroundThreshold", -1) <= z <= v.get("inertBackgroundThreshold", 1):
                g = new_group(o["id"])
                rem = [r[1] for r in v.get("remnantParts") or [] if r and r[1] in by_id]
                g2 = 0
                if rem:
                    ngroups[0] += 1
                    g2 = ngroups[0]
                    for r in rem:
                        subtree(r, g2)
                c0, cn = _subcols(d, owners, o["id"]) if owners else (0, 0)
                if rem and owners and any(_subcols(d, owners, r)[1] for r in rem):
                    print("ents: %s: remnant colliders not handled" % o["path"])
                debris = [r[1] for r in v.get("debrisParts") or [] if r]
                pieces = _pieces(d, o, debris, by_id, sprites)
                sign = -1.0 if o.get("lscale", [1])[0] < 0 else 1.0
                persistent = any(c.get("class") == "PersistentBoolItem" for c in o["c"])
                if v.get("hitEventReciever"):
                    receivers.append((len(recs), v["hitEventReciever"][1]))
                recs.append(rec(ENT_OBJ, OK_BREAKABLE, (o["pos"][0], o["pos"][1], o["pos"][2], 0),
                                (v.get("flingSpeedMin", 10), v.get("flingSpeedMax", 17), v.get("angleOffset", -60) * sign,
                                 len(pieces)), a=c0, group=g, group2=g2,
                                persist=persist.id(name, o["path"]) if persistent else NO_PERSIST, s0=cn, s1=len(hits)))
                recs += [rec(ENT_BOX, fl, box=b) for b, fl in hits]
                recs += pieces
        dh = [c for c in o["c"] if c.get("class") == "DamageHero"]
        # (its own classes: o may be an enemy found under its ActivateChildrenOnContact parent, whose touch is its own)
        if dh and not {c.get("class") for c in o["c"]} & {"HealthManager", "StalactiteControl"}:
            # a hazard (spikes, acid): its colliders' outlines, as boxes or as shapes after it; p3: a down slash bounces
            # off it (NailSlash: the layers it bounces off, no NonBouncer on)
            v = dh[0].get("v") or {}
            nb = any(c.get("class") == "NonBouncer" and (c.get("v") or {}).get("active", True) for c in o["c"])
            bounce = HB_BOUNCE if o["layer"] in (11, 17, 19) and not nb else 0
            for c in o["c"]:
                if c["type"] not in ("BoxCollider2D", "PolygonCollider2D") or not c.get("v") or not c["v"].get("m_Enabled", 1):
                    continue
                pts = _shape(o, c)
                if not pts:
                    continue
                xs, ys = [q[0] for q in pts], [q[1] for q in pts]
                box = (min(xs), min(ys), max(xs), max(ys))
                if _is_axis_box(pts):
                    recs.append(rec(ENT_DAMAGE, box=box, p=(v.get("hazardType", 1), v.get("damageDealt", 1), 0, bounce)))
                else:
                    pts = pts[:8]
                    nrec = (len(pts) + 3) // 4
                    recs.append(rec(ENT_DAMAGE, box=box, p=(v.get("hazardType", 1), v.get("damageDealt", 1), len(pts), bounce),
                                    a=nrec))
                    for i in range(nrec):
                        q = pts[4 * i:4 * i + 4] + [pts[-1]] * (4 - len(pts[4 * i:4 * i + 4]))
                        recs.append(rec(ENT_SHAPE, box=(q[0][0], q[0][1], q[1][0], q[1][1]), p=(q[2][0], q[2][1], q[3][0], q[3][1])))
        for c in o["c"]:
            cls, v = c.get("class"), c.get("v") or {}
            if cls == "CameraLockArea":
                box = _trigger(o)
                if box is None:
                    continue
                fl = (CL_PREVENT_UP if v.get("preventLookUp") else 0) | (CL_PREVENT_DOWN if v.get("preventLookDown") else 0) | \
                    (CL_MAX_PRIORITY if v.get("maxPriority") else 0)
                cam_rec[o["id"]] = len(recs)
                # (its name, if scripts find it: FindGameObject)
                import vm
                nm = vm.STR.id(o["name"]) if o["name"] in _found_names(d) else 0xFFFF
                # (one the scripts hold: its object's number + 1, there as that is on; CameraLockArea's OnDisable)
                rmv = vm.BUILT.get(name)
                vo = rmv.obj_index.get(o["id"]) if rmv is not None else None
                vo = vo if vo is not None and rmv.objs[vo]["path"] == o["path"] and rmv.objs[vo].get("_scene") == name else None
                recs.append(rec(ENT_CAMLOCK, fl, box, (v["cameraXMin"], v["cameraYMin"], v["cameraXMax"], v["cameraYMax"]),
                                a=vo + 1 if vo is not None else 0, s1=nm))
                recs += _more_boxes(o) + _outline(o)
            elif cls == "TransitionPoint":
                box, entry_only = _trigger(o), 0
                if box is None:
                    # (its collider off: a place to come in at, never left by)
                    box = next((_box(o, c) for c in o["c"] if c["type"] == "BoxCollider2D" and c.get("v") and
                                c["v"].get("m_IsTrigger")), None)
                    entry_only = G_ENTRY_ONLY
                if box is None:
                    continue
                fl = entry_only | (G_DOOR if v.get("isADoor") else 0) | (G_ENTER_RIGHT if v.get("alwaysEnterRight") else 0) | \
                    (G_ENTER_LEFT if v.get("alwaysEnterLeft") else 0) | (G_DONT_WALK_OUT if v.get("dontWalkOutOfDoor") else 0) | \
                    (G_NON_HAZARD if v.get("nonHazardGate") else 0) | (G_HARD_LAND if v.get("hardLandOnExit") else 0)
                target = v.get("targetScene") or ""
                ti = rooms.index(target) if target in rooms else 0xFFFF
                # (its respawn marker: a component; the record of its object)
                ref = v.get("respawnMarker")
                mid = ref[1] if isinstance(ref, (list, tuple)) else None
                marker = next((marker_index[oo["id"]] + rec_base for oo in objs for cc in oo["c"] if cc.get("pid") == mid and
                               oo["id"] in marker_index), -1)
                off = v.get("entryOffset") or {"x": 0, "y": 0}
                recs.append(rec(ENT_GATE, fl, box, (float(v.get("entryDelay", 0)), off["x"], off["y"], marker + 1), a=ti,
                                s0=strings.id(o["name"]), s1=strings.id(v.get("entryPoint") or "")))
                recs += _more_boxes(o)
                # (the gate's own place, where the Knight comes in: a box record flagged 1, not part of its trigger)
                recs.append(rec(ENT_BOX, 1, (o["pos"][0], o["pos"][1], o["pos"][0], o["pos"][1])))
                # (off as the room loads while a PlayerData bool says its object is: flagged 2, the bool, off if true)
                for flag, off_if in conds(o):
                    recs.append(rec(ENT_BOX, 2, p=(flag, 1 if off_if else 0, 0, 0)))
                # (a door: its Door Control's scene, gate, pause and prompt; the prompt's place; flagged 4)
                dc = next((c2["fsm"] for c2 in o["c"] if c2.get("fsm") and c2["fsm"]["name"] == "Door Control" and
                           c2.get("enabled") is not False), None)
                if v.get("isADoor") and dc:
                    fv = lambda n, dflt=None: (dc["vars"].get(n) or [None, dflt])[1]
                    to = fv("New Scene") or ""
                    pm = next((by_id[c2] for c2 in o.get("children", []) if by_id[c2]["name"] == "Prompt Marker"), o)
                    recs.append(rec(ENT_BOX, 4, (pm["pos"][0], pm["pos"][1], 1 if fv("Crossroads Ascent") else 0,
                                                 1 if fv("Over Hero") else 0),
                                    (float(fv("Entry Pause", 0) or 0), DOOR_PROMPTS.index(fv("Prompt Name", "Enter")), 0, 0),
                                    a=rooms.index(to) if to in rooms else 0xFFFF, s1=strings.id(fv("Entry Gate") or "")))
                recs += _outline(o)
            elif cls == "HazardRespawnTrigger":
                box = _trigger(o)
                ref = v.get("respawnMarker")
                mid = ref[1] if isinstance(ref, (list, tuple)) else None
                # (the marker is a component: find its object)
                target = None
                for oo in objs:
                    for cc in oo["c"]:
                        if cc.get("pid") == mid:
                            target = oo["id"]
                if box is None or target not in marker_index:
                    continue
                recs.append(rec(ENT_HAZARD_TRIGGER, box=box, a=marker_index[target] + rec_base))
                recs += _more_boxes(o) + _outline(o)
    # (arenas: their camera locks' records)
    for i, c1, c2 in battles:
        b = bytearray(recs[i])
        struct.pack_into("<2f", b, 24, cam_rec[c1] + rec_base if c1 in cam_rec else -1, cam_rec[c2] + rec_base if c2 in cam_rec else -1)
        recs[i] = bytes(b)
    # (summoners: their enemies' records)
    for i, target in summons:
        b = bytearray(recs[i])
        struct.pack_into("<f", b, 24, enemy_rec_of[target] + rec_base if target in enemy_rec_of else -1)
        recs[i] = bytes(b)
    # (an object that sends HIT to another: that one's record + 1 in its box's y1)
    for i, target in receivers:
        if target in rec_of:
            b = bytearray(recs[i])
            struct.pack_into("<f", b, 20, rec_of[target] + rec_base + 1)
            recs[i] = bytes(b)
        else:
            print("ents: %s: HIT receiver %d has no record" % (name, target))
    assert ngroups[0] < MAX_GROUPS, ngroups[0]
    assert len(recs) <= 448, len(recs)   # (src/game.h: MAX_ENTS)
    return recs, groups
