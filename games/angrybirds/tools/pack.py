#!/usr/bin/env python3
"""Angry Birds: src/data.bin and src/data.h from the Chrome Angry Birds web build.

    python3 -I tools/pack.py REF

REF is the web build's cors/fowl folder (json/, sprite_sheets/, images/) and, next to it, the beautified game
script the star limits are read from (REF/../../extract/pretty.js, or STARS=<file>). Nothing of the web build's code
goes into data.bin: only its levels, numbers and pictures.

data.bin: "ABD1", then the offsets of its tables (see src/data.h, written with it):
  materials, damage pairs, shapes, object types, levels (a header each, then its objects packed with deflate),
  episodes, pictures (16 colours each, 4 bits a pixel) and the backgrounds.
"""
import glob
import json
import math
import os
import re
import struct
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REF = os.path.abspath(sys.argv[1]) if len(sys.argv) > 1 else os.environ.get("REF", "")
if not REF or not os.path.isdir(os.path.join(REF, "json")):
    sys.exit("usage: pack.py <the web build's cors/fowl folder>")
J = os.path.join(REF, "json")


def rd(name):
    with open(os.path.join(J, name)) as f:
        return json.load(f)


objects = rd("Objects.json")
objects.update(rd("ChromeObjects.json"))
materials = rd("Materials.json")
materials.update(rd("ChromeMaterials.json"))
shapes = rd("Shapes.json")
shapes.update(rd("ChromeShapes.json"))
episodes = rd("Episodes.json")["episodes"]
cutscenes = rd("Cutscenes.json")
levels_json = {os.path.basename(p)[5:-5]: p for p in glob.glob(os.path.join(J, "Level*.json"))}

# star limits: the game script sets them (silver, gold, the Mighty Eagle's 100 %) for each level
stars_file = os.environ.get("STARS") or os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(REF))),
                                                      "extract", "pretty.js")
if not os.path.exists(stars_file):
    # the web build's own folder layout: .../web/public/assets/games/angry-birds/cors/fowl
    up = REF
    for _ in range(8):
        up = os.path.dirname(up)
        cand = os.path.join(up, "extract", "pretty.js")
        if os.path.exists(cand):
            stars_file = cand
            break
with open(stars_file) as f:
    game_script = f.read()
star_limits = {k.lower(): (int(a), int(b), int(c))
               for k, a, b, c in re.findall(r"K4\(this, 'Level([^']*)', (\d+), (\d+), (\d+)\)", game_script)}

# ------------------------------------------------------------------ the campaign, in the menu's order
# classic episodes first (Poached Eggs, Mighty Hoax, Danger Above, The Big Setup, Ham 'Em High, Chrome Dimension),
# then the seasons (Trick or Treat, Season's Greedings, Easter Eggs, Moon Festival, Cherry Blossom, Summer Pignic)
EPISODE_ORDER = [6, 7, 8, 9, 10, 11, 0, 1, 2, 3, 4, 5]
EPISODE_NAMES = {"1": "Poached Eggs", "2": "Mighty Hoax", "3": "Danger Above", "christmas": "Season's Greedings",
                 "chrome": "Chrome Dimension"}
lower_keys = {k.lower(): k for k in levels_json}
campaign = []     # (level key as in the file name, episode index)
episode_rows = []
for ei, src in enumerate(EPISODE_ORDER):
    e = episodes[src]
    first = len(campaign)
    for page, n in zip(e["pageIndexes"], e["levelsPerPage"]):
        for j in range(1, n + 1):
            campaign.append((lower_keys[f"{page}-{j}".lower()], ei))
    episode_rows.append(dict(name=EPISODE_NAMES.get(e["name"], e["name"]), first=first, count=len(campaign) - first,
                             image=e["menuImage"], src=e))
assert len(campaign) == 429, len(campaign)

# the golden eggs of the classic levels open the Chrome Dimension, one level each (in this order)
GOLDEN = ["1-16", "1-18", "1-20", "2-4", "2-20", "3-7", "3-14", "4-4", "4-13", "5-5", "6-9", "6-13", "7-8", "8-2",
          "9-8", "9-14", "10-15", "11-3", "12-12", "13-2", "14-9"]

# ------------------------------------------------------------------ object classes (the game's own kinds)
CLASSES = ["BLOCK", "RED", "BLUE", "YELLOW", "BLACK", "WHITE", "GREEN", "REDBIG", "SARDINE", "EAGLE", "PIG",
           "GROUND", "SLINGSHOT", "TNT", "EGG", "GOLDEN", "TRAMPOLINE", "STATICBLOCK", "EASTEREGG", "IMPLOSION",
           "FRONT", "BALLOON", "BOMBBALLOON"]
BIRD_CLASS = {"BIRD_RED": "RED", "BIRD_BLUE": "BLUE", "BIRD_YELLOW": "YELLOW", "BIRD_BLACK": "BLACK",
              "BIRD_WHITE": "WHITE", "BIRD_GREEN": "GREEN", "BIRD_REDBIG": "REDBIG", "BIRD_SARDINE": "SARDINE",
              "BIRD_ME": "EAGLE"}


def object_class(name):
    o = objects[name]
    if name in BIRD_CLASS:
        return BIRD_CLASS[name]
    t = o["type"]
    if t == "PIG":
        return "PIG"
    if t == "GROUND":
        return "GROUND"
    if t == "SLINGSHOT":
        return "SLINGSHOT"
    if name in ("MISC_EXPLOSIVE_TNT", "CHROME_BOMB"):
        return "TNT"
    if name == "MISC_FOOD_EGG":
        return "EGG"
    if name == "MISC_GOLDEN_EGG":
        return "GOLDEN"
    if name == "MISC_RUBBER_TRAMPOLINE":
        return "TRAMPOLINE"
    if name == "CHROME_IMPLOSION_BOMB":
        return "IMPLOSION"
    if name == "MISC_BALLOON_HELIUM":
        return "BALLOON"
    if name == "MISC_BALLOON_EPLOSIVE":
        return "BOMBBALLOON"
    if name.startswith("B_EASTEREGG_4"):
        return "EASTEREGG"
    if t == "BLOCK_STATIC" or name.startswith(("BREAKABLE_STATIC_BLOCK", "SNOW_", "BLOCK_STATIC_CLOUD_")):
        return "STATICBLOCK"
    if t == "BLOCK_FRONT":
        return "FRONT"
    return "BLOCK"


# ------------------------------------------------------------------ which objects are used
used = set()
level_data = {}
for key, _ in campaign:
    with open(levels_json[key]) as f:
        d = json.load(f)
    level_data[key] = d
    for v in d["world"].values():
        if "id" in v:
            used.add(v["id"])
used |= {"GROUND_HILLS", "SLINGSHOT", "BIRD_SARDINE", "BIRD_ME", "MISC_FOOD_EGG"}
type_names = sorted(used)
type_index = {n: i for i, n in enumerate(type_names)}
assert len(type_names) < 255

mat_names = sorted({objects[n]["material"] for n in type_names})
mat_index = {n: i for i, n in enumerate(mat_names)}
shape_names = sorted({objects[n]["shape"] for n in type_names})
shape_index = {n: i for i, n in enumerate(shape_names)}


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


# ------------------------------------------------------------------ physics tables
def shape_record(name):
    s = shapes[name]
    ox, oy = s.get("offsetX", 0) or 0, s.get("offsetY", 0) or 0
    if s["type"] == "CIRCLE":
        return 0, s["radius"] / 20, [], ox, oy
    if s["type"] == "RECTANGLE":
        w, h = s["width"] / 20, s["height"] / 20
        return 1, 0.0, [(w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2), (-w / 2, -h / 2)], ox, oy
    return 1, 0.0, [(x / 20, y / 20) for x, y in s["vertices"]], ox, oy


def polygon_area(v):
    return sum(v[i][0] * v[(i + 1) % len(v)][1] - v[(i + 1) % len(v)][0] * v[i][1] for i in range(len(v))) / 2


out_shapes = bytearray()
shape_area = {}
for name in shape_names:
    kind, r, verts, ox, oy = shape_record(name)
    assert len(verts) <= 6, name
    if kind:
        a = polygon_area(verts)
        assert a > 0, f"{name}: clockwise"
        shape_area[name] = a
    else:
        shape_area[name] = math.pi * r * r
    out_shapes += struct.pack("<BBhhhf", kind, len(verts), int(round(ox * 4)), int(round(oy * 4)), 0, r)
    for x, y in verts + [(0.0, 0.0)] * (6 - len(verts)):
        out_shapes += struct.pack("<ff", x, y)

out_mats = bytearray()
pairs = []
for name in mat_names:
    m = materials[name]
    static = 1 if m["bodyType"] == "STATIC" else 0
    out_mats += struct.pack("<fffffB3x", m["density"], m["friction"], m["restitution"], m.get("strength", 0),
                            m.get("defense", 0), static)
    dm, vm = m.get("damageMultipliers", {}), m.get("velocityMultipliers", {})
    for other in sorted(set(dm) | set(vm)):
        if other in mat_index:
            pairs.append((mat_index[name], mat_index[other], dm.get(other, 1.0), vm.get(other, 1.0)))
out_pairs = bytearray()
for a, b, dmul, vmul in sorted(pairs):
    out_pairs += struct.pack("<BB2xff", a, b, dmul, vmul)

out_types = bytearray()
for name in type_names:
    o = objects[name]
    m = materials[o["material"]]
    out_types += struct.pack("<HBBHBBf", shape_index[o["shape"]], mat_index[o["material"]],
                             CLASSES.index(object_class(name)), o.get("score", 0), o.get("damagedFrameCount", 0), 0,
                             float(o.get("maxHealth", 0)))

# ------------------------------------------------------------------ levels
THEMES = []


def theme_of(key, d, e):
    over = e["src"].get("levelThemeOverrides", {})
    t = over.get(key, d.get("theme", "BACKGROUND_BLUE_GRASS"))
    if t not in THEMES:
        THEMES.append(t)
    return THEMES.index(t)


def num(k):
    m = re.search(r"\d+", k)
    return int(m.group()) if m else 0


def merge_rects(rects):
    """Axis-aligned rectangles (x0, y0, x1, y1, material) merged where two share a whole side."""
    eps = 2e-3
    rects = [list(r) for r in rects]
    changed = True
    while changed:
        changed = False
        for i in range(len(rects)):
            a = rects[i]
            if a is None:
                continue
            for j in range(i + 1, len(rects)):
                b = rects[j]
                if b is None or a[4] != b[4]:
                    continue
                if abs(a[1] - b[1]) < eps and abs(a[3] - b[3]) < eps and b[0] <= a[2] + eps and a[0] <= b[2] + eps:
                    a[0], a[2] = min(a[0], b[0]), max(a[2], b[2])
                elif abs(a[0] - b[0]) < eps and abs(a[2] - b[2]) < eps and b[1] <= a[3] + eps and a[1] <= b[3] + eps:
                    a[1], a[3] = min(a[1], b[1]), max(a[3], b[3])
                else:
                    continue
                rects[j] = None
                changed = True
    return [tuple(r) for r in rects if r is not None]


level_header = bytearray()
level_streams = bytearray()
level_names = []
max_objects = max_joints = max_rects = max_unpacked = 0
stats = []
for li, (key, ei) in enumerate(campaign):
    d = level_data[key]
    w = d["world"]
    birds = sorted([(k, v) for k, v in w.items() if k.startswith("bird")], key=lambda kv: num(kv[0]))
    blocks = sorted([(k, v) for k, v in w.items() if k.startswith("block")], key=lambda kv: num(kv[0]))
    joints = sorted([(k, v) for k, v in w.items() if k.startswith("joint")], key=lambda kv: num(kv[0]))
    objs = blocks + birds          # (joints count blocks from 0)
    jointed = set()
    for _, j in joints:
        jointed.add(j["index1"])
        jointed.add(j["index2"])
    # static, unbreakable, square-cornered blocks no joint holds: one body of merged rectangles, drawn one by one
    scenery = []
    rects = []
    for i, (k, v) in enumerate(blocks):
        o = objects[v["id"]]
        m = materials[o["material"]]
        s = shapes[o["shape"]]
        a = (v.get("angle", 0) or 0) % 90
        if (m["bodyType"] == "STATIC" and m.get("defense", 0) < 0 and s["type"] == "RECTANGLE" and i not in jointed
                and (a < 0.01 or a > 89.99) and object_class(v["id"]) in ("STATICBLOCK", "BLOCK")):
            ang = (v.get("angle", 0) or 0) % 360
            quarter = int(round(ang / 90)) % 2
            hw, hh = s["width"] / 40, s["height"] / 40
            if quarter:
                hw, hh = hh, hw
            scenery.append(i)
            rects.append((v["x"] - hw, v["y"] - hh, v["x"] + hw, v["y"] + hh, mat_index[o["material"]]))
    merged = merge_rects(rects)
    flags_by_index = {i: 1 for i in scenery}
    raw = bytearray(struct.pack("<HHHBB", len(objs), len(joints), len(merged), len(birds), 0))
    raw += bytes(type_index[v["id"]] for _, v in objs)
    raw += bytes(flags_by_index.get(i, 0) for i in range(len(objs)))
    # x, y (meters) and the angle (degrees) to the thousandth, each as four planes of bytes (they pack better)
    for field in ("x", "y", "angle"):
        ints = struct.pack(f"<{len(objs)}i", *[int(round(float(v.get(field, 0) or 0) * 1000)) for _, v in objs])
        for plane in range(4):
            raw += ints[plane::4]
    for _, j in joints:
        flags = (int(bool(j.get("collideConnected", False))) | int(bool(j.get("motor", False))) << 1 |
                 int(bool(j.get("limit", False))) << 2 | int(bool(j.get("backAndForth", False))) << 3)
        raw += struct.pack("<BBHH", j["type"], flags, j["index1"], j["index2"])
        raw += struct.pack("<8f", *[float(j.get(k, 0) or 0) for k in ("x1", "y1", "x2", "y2", "motorSpeed",
                                                                         "maxTorque", "lowerLimit", "upperLimit")])
        raw += struct.pack("<I", int(j.get("destroyTimer", 0) or 0))
    for x0, y0, x1, y1, m in merged:
        raw += struct.pack("<B3x4f", m, x0, y0, x1, y1)
    packed = zlib.compress(bytes(raw), 9)[2:-4]   # raw deflate
    cams = {c["id"]: c for c in d.get("camera", [])}
    cam = []
    for cid in ("Slingshot", "Castle"):  # (a view of its size round its x and y: MFb)
        c = cams.get(cid, cams.get("Slingshot"))
        w, h = c["right"] - c["left"], c["bottom"] - c["top"]
        cam += [c["x"] - w / 2, c["y"] - h / 2, c["x"] + w / 2, c["y"] + h / 2]
    silver, gold, eagle = star_limits[key.lower()]
    level_header += struct.pack("<IHHIIIBBBB8f", len(level_streams), len(packed), len(raw), silver, gold, eagle,
                                theme_of(key, d, episode_rows[ei]), ei, len(birds), 0, *cam)
    level_streams += packed
    level_names.append(key)
    max_objects = max(max_objects, len(objs))
    max_joints = max(max_joints, len(joints))
    max_rects = max(max_rects, len(merged))
    max_unpacked = max(max_unpacked, len(raw))
    stats.append((key, len(objs), len(scenery), len(merged)))

print(f"levels: {len(campaign)}, {len(level_streams)} bytes packed; at most {max_objects} objects, "
      f"{max_joints} joints, {max_rects} ground rectangles, {max_unpacked} bytes unpacked", file=sys.stderr)
print("scenery merged:", sum(s[2] for s in stats), "->", sum(s[3] for s in stats), file=sys.stderr)

# episodes: name, first level, count, its picture (set with the pictures)
out_episodes = bytearray()
for e in episode_rows:
    out_episodes += struct.pack("<HH", e["first"], e["count"])

# story pictures: before a level (INTRO_) or after it
story_before = [0xFF] * len(campaign)
story_after = [0xFF] * len(campaign)
story_names = sorted(cutscenes)
for e in episode_rows:
    for k, v in (e["src"].get("cutscenes", {}) or {}).items():
        if v not in cutscenes:
            continue
        lk = k[6:] if k.startswith("INTRO_") else k
        idx = next((i for i, (n, _) in enumerate(campaign) if n.lower() == lk.lower()), None)
        if idx is None:
            continue
        (story_before if k.startswith("INTRO_") else story_after)[idx] = story_names.index(v)

golden = [next(i for i, (n, _) in enumerate(campaign) if n.lower() == g.lower()) for g in GOLDEN]


def level_at(name):
    return next(i for i, (n, _) in enumerate(campaign) if n.lower() == name.lower())


# the hidden eggs of three Easter levels open its specials; the summer's last opens with three stars on the others
unlocks = [(level_at("easter-4"), level_at("easterSpecial-1")), (level_at("easter-12"), level_at("easterSpecial-2")),
           (level_at("easter-17"), level_at("easterSpecial-3"))]


# ------------------------------------------------------------------ pictures
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import art   # noqa: E402  (tools/art.py: the pictures, 4 bits a pixel)

pictures = art.Pictures(REF)
type_sprites = []
for name in type_names:
    type_sprites.append(pictures.object_frames(name, objects[name], shapes[objects[name]["shape"]]))
ui = pictures.ui(episode_rows, story_names, cutscenes)
backgrounds = pictures.backgrounds(THEMES, art.Pictures.parse_themes(game_script))
for i, (first, count, fly, power) in enumerate(type_sprites):
    out_types[i * 12 + 7: i * 12 + 8] = bytes([count])
out_types2 = bytearray()
for first, count, fly, power in type_sprites:
    out_types2 += struct.pack("<4H", first, count, fly, power)

# ------------------------------------------------------------------ data.bin
sections = [
    ("MATERIALS", out_mats), ("PAIRS", out_pairs), ("SHAPES", out_shapes), ("TYPES", out_types),
    ("TYPE_SPRITES", out_types2), ("LEVELS", level_header), ("LEVEL_STREAMS", level_streams),
    ("EPISODES", out_episodes), ("STORY_BEFORE", bytes(story_before)), ("STORY_AFTER", bytes(story_after)),
    ("EPISODE_PICS", struct.pack(f"<{len(episode_rows)}H", *[pictures.names["EP_" + e["image"]] for e in episode_rows])),
    ("STORY_PICS", struct.pack(f"<{len(story_names)}H", *[pictures.names[n] for n in story_names])),
    ("GOLDEN", bytes(golden)), ("UNLOCKS", struct.pack(f"<{2 * len(unlocks)}H", *[x for u in unlocks for x in u])), ("PICTURES", pictures.table()), ("PIXELS", pictures.pixels()),
    ("BACKGROUNDS", backgrounds),
]
blob = bytearray(b"ABD1")
blob += struct.pack("<I", len(sections))
table_at = len(blob)
blob += bytes(8 * len(sections))
for i, (name, data) in enumerate(sections):
    while len(blob) % 4:
        blob.append(0)
    struct.pack_into("<II", blob, table_at + 8 * i, len(blob), len(data))
    blob += data
with open(os.path.join(ROOT, "src", "data.bin"), "wb") as f:
    f.write(blob)


def ident(s):
    return re.sub(r"[^A-Z0-9]+", "_", s.upper()).strip("_")


h = ["/* Generated by tools/pack.py: what src/data.bin holds (see data.c for the tables). */",
     "#ifndef AB_DATA_H", "#define AB_DATA_H", ""]
h += [f"#define D_{name} {i}" for i, (name, _) in enumerate(sections)]
h += ["", f"#define LEVEL_COUNT {len(campaign)}", f"#define EPISODE_COUNT {len(episode_rows)}",
      f"#define TYPE_COUNT {len(type_names)}", f"#define MATERIAL_COUNT {len(mat_names)}",
      f"#define SHAPE_COUNT {len(shape_names)}", f"#define PAIR_COUNT {len(pairs)}",
      f"#define THEME_COUNT {len(THEMES)}", f"#define STORY_COUNT {len(story_names)}",
      f"#define GOLDEN_COUNT {len(golden)}", f"#define MAX_OBJECTS {max_objects}", f"#define MAX_JOINTS {max_joints}",
      f"#define MAX_RECTS {max_rects}", f"#define MAX_UNPACKED {max_unpacked}", ""]
h += ["enum {"] + [f"  C_{c}," for c in CLASSES] + ["};", ""]
h += ["enum {"] + [f"  T_{ident(n)}," for n in type_names] + ["};", ""]
h += pictures.header() + [""]
h += ["extern const char *const episode_names[EPISODE_COUNT];", "#endif", ""]
with open(os.path.join(ROOT, "src", "data.h"), "w") as f:
    f.write("\n".join(h))
names_c = ["/* Generated by tools/pack.py: the episodes' names. */", '#include "data.h"', "",
           "const char *const episode_names[EPISODE_COUNT] = {"]
names_c += [f'  "{e["name"]}",' for e in episode_rows] + ["};", ""]
with open(os.path.join(ROOT, "src", "names.c"), "w") as f:
    f.write("\n".join(names_c))
print(f"data.bin: {len(blob)} bytes ({len(level_streams)} levels, {len(pictures.pixels())} pixels)", file=sys.stderr)
