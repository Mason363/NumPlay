#!/usr/bin/env python3
"""Makes src/data.c and src/data.h from Subway Surfers' own Android package: the game's 3D models, textures,
animations, scenery, pick-ups and screen pictures, made small enough for the calculator.

- The people (Jake, Tricky, Fresh, Spike), the Inspector and his dog: the game's skinned meshes, with fewer
  triangles (tools/decimate.py), each corner following at most two bones, their textures at 128x128, and the
  game's own animation clips (running, jumping, rolling, dodging, surfing, flying, caught...) sampled 30 times a
  second.
- The trains, barriers, ramps and the city along the tracks: the game's meshes and its texture atlas, turned into
  a few flat faces each with the detail painted on (tools/bake.py), the way they are seen from the tracks.
- The tracks: the game's rails, sleepers and ballast, seen from above, as the colours of the ground.
- Coins, keys and power-ups: pictures of the game's models; the screens' icons, buttons and logo: the game's own
  sprites.

Units are metres (the game's units times SCALE), x right, y up, z ahead.
Usage: apk.py PACKAGE   (the folder the package was unzipped into; nothing in it is run)"""
import os
import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bake  # noqa: E402
import decimate  # noqa: E402
import unity  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
SCALE = 0.13          # metres a unit of the game: its tracks are 20 apart, 2.6 m here
FPS = 30              # animation keys a second


def cfloat(v):
    t = f"{v:.7g}"
    if "." not in t and "e" not in t:
        t += ".0"
    return t + "f"


def c565(r, g, b):
    return ((int(r) & 0xF8) << 8) | ((int(g) & 0xFC) << 3) | (int(b) >> 3)


class Out:
    def __init__(self):
        self.c = ["/* Made by tools/apk.py from Subway Surfers' own Android package (the game's models, textures,",
                  " * animations and pictures, by SYBO). */", '#include "data.h"', ""]
        self.h = ["/* Subway Surfers: the game's models, animations and pictures (data.c, made by tools/apk.py). */",
                  "#ifndef SS_DATA_H", "#define SS_DATA_H", '#include "models.h"', ""]
        self.textures = []
        self.bytes = 0
        self.sizes = {}

    def count(self, what, n):
        self.bytes += n
        self.sizes[what] = self.sizes.get(what, 0) + n

    def array(self, ctype, name, values, per=24, what="other", static=True):
        self.c.append(f"{'static ' if static else ''}const {ctype} {name}[{len(values)}] = {{")
        for i in range(0, len(values), per):
            self.c.append("  " + ",".join(str(int(v)) for v in values[i:i + per]) + ",")
        self.c.append("};")
        self.count(what, len(values) * (1 if "8" in ctype else 2 if "16" in ctype else 4))

    def texture(self, name, rgba, clear=False, colors=64, what="textures"):
        """A texture from a picture whose sides are powers of two; index 0 see-through when clear."""
        w, h = rgba.size
        assert w & (w - 1) == 0 and h & (h - 1) == 0, (name, w, h)
        px = np.asarray(rgba.convert("RGBA")).astype(np.int32)
        alpha = px[..., 3]
        first = 2 if clear else 1          # (0: see-through; then the average)
        n = colors - first
        seen = alpha >= 128
        rgb = px[..., :3].copy()
        if clear and seen.any():
            rgb[~seen] = rgb[seen].mean(axis=0)   # (so the palette is spent on what shows)
        q = Image.fromarray(rgb.astype(np.uint8), "RGB").quantize(colors=n, method=Image.Quantize.MEDIANCUT,
                                                                  dither=Image.Dither.NONE)
        pal = q.getpalette()[:3 * n]
        pal += [0] * (3 * n - len(pal))
        idx = np.asarray(q).astype(np.int32) + first
        if clear:
            idx[~seen] = 0
        avg = px[..., :3][seen].mean(axis=0) if seen.any() else np.array([128, 128, 128])
        palette = ([0] if clear else []) + [c565(*avg)] + [c565(*pal[3 * i:3 * i + 3]) for i in range(n)]
        self.array("uint8_t", f"{name}_px", idx.flatten().tolist(), 32, what)
        self.array("uint16_t", f"{name}_pal", palette, 12, what)
        wl, hl = w.bit_length() - 1, h.bit_length() - 1
        self.c.append(f"static const Texture {name} = {{{wl}, {hl}, {1 if clear else 0}, 0, {name}_px, {name}_pal}};")
        self.c.append("")
        self.textures.append(name)
        return len(self.textures) - 1

    def image(self, name, rgba, colors=32, what="screens"):
        """A picture for the screens: rows of runs (count, alpha 0..7 << 5 | colour 0..31)."""
        px = np.asarray(rgba.convert("RGBA")).astype(np.int32)
        h, w = px.shape[:2]
        assert w < 256 and h < 256, (name, w, h)
        a8 = np.clip(np.round(px[..., 3] / 255 * 7), 0, 7).astype(np.int32)
        rgb = px[..., :3].copy()
        if (a8 > 0).any():
            rgb[a8 == 0] = rgb[a8 > 0].mean(axis=0)
        q = Image.fromarray(rgb.astype(np.uint8), "RGB").quantize(colors=colors, method=Image.Quantize.MEDIANCUT,
                                                                  dither=Image.Dither.NONE)
        pal = q.getpalette()[:3 * colors]
        pal += [0] * (3 * colors - len(pal))
        idx = np.asarray(q).astype(np.int32)
        vals = np.where(a8 > 0, a8 << 5 | idx, 0)
        runs, rows = [], []
        for y in range(h):
            rows.append(len(runs))
            row = vals[y].tolist()
            x = 0
            while x < w:
                v, n = row[x], 1
                while x + n < w and row[x + n] == v and n < 255:
                    n += 1
                runs += [n, v]
                x += n
        rows.append(len(runs))
        self.array("uint8_t", f"{name}_runs", runs, 32, what)
        self.array("uint16_t", f"{name}_rows", rows, 16, what)
        self.array("uint16_t", f"{name}_pal", [c565(*pal[3 * i:3 * i + 3]) for i in range(colors)], 12, what)
        self.c.append(f"const Image {name} = {{{w}, {h}, {name}_pal, {name}_rows, {name}_runs}};")
        self.c.append("")
        self.h.append(f"extern const Image {name};")

    def write(self):
        self.h.append("")
        self.h.append("enum {")
        for i, n in enumerate(self.textures):
            self.h.append(f"  {n.upper()} = {i},")
        self.h.append("};")
        self.c.append(f"const Texture *const r_tex[{len(self.textures)}] = {{" + ", ".join("&" + n for n in self.textures)
                      + "};")
        self.c.append("")
        self.h += ["#endif", ""]
        (ROOT / "src" / "data.c").write_text("\n".join(self.c) + "\n")
        (ROOT / "src" / "data.h").write_text("\n".join(self.h) + "\n")
        for k, v in sorted(self.sizes.items()):
            print(f"  {k}: {v} bytes")
        print(f"data: {self.bytes} bytes")


# ---------------------------------------------------------------- rigs and animations
# the people's bones on the calculator: the game's skeleton without its fingers, toes and helpers (their corners
# follow the hand, the foot, the head)
RUNNER_RIG = ["", "Root", "Root_jnt", "LowerSpine_jnt", "Head_jnt", "L_Clavicle_jnt", "L_Shoulder_jnt", "L_Elbow_jnt",
              "L_Hand_jnt", "R_Clavicle_jnt", "R_Shoulder_jnt", "R_Elbow_jnt", "R_Hand_jnt", "L_Leg_jnt", "L_Knee_jnt",
              "L_Foot_jnt", "R_Leg_jnt", "R_Knee_jnt", "R_Foot_jnt"]
GUARD_RIG = ["", "guard_gameSkeleton", "root_A", "back_B", "chest_jnt", "arm_A_L", "arm_B_L", "wrist_L", "arm_A_R",
             "arm_B_R", "wrist_R", "neck", "leg_A_L", "leg_B_L", "foot_A_L", "leg_A_R", "leg_B_R", "foot_A_R"]
DOG_RIG = ["", "dog_gameSkeleton", "root", "IkLeg__A_L", "IkLeg__B_L", "IkLeg__C_L", "IkLeg__A_R", "IkLeg__B_R",
           "IkLeg__C_R", "spine", "chest", "neck_A", "head_ctrl", "Lower_Jaw", "shoulderBlade_B_L", "arm_L", "elbow_L",
           "shoulderBlade_B_R", "arm_R", "elbow_R"]

# the clips each rig plays: (the game's clip, the name here, loops). The runner's are the ones its
# Character_Animator_Default plays (its Run, Jump, Fall, Landing, Roll and LaneChange states' first clips); the
# capitalised Run, Roll, Dodge_L and Dodge_R are the ChaseTarget_Animator_Default's, and Jump, Hangtime and Landing
# no controller's
RUNNER_CLIPS = [
    ("run", "run", 1), ("jump", "jump", 0), ("hangtime", "hang", 1), ("landing", "land", 0), ("roll", "roll", 0),
    ("dodgeLeft", "dodge_l", 0), ("dodgeRight", "dodge_r", 0), ("stumbleSideLeft", "stumble_l", 0),
    ("stumbleSideRight", "stumble_r", 0), ("stumble_low", "stumble", 0), ("death_bounce", "death", 0),
    ("death_movingTrain", "death_train", 0), ("h_run", "board", 1), ("h_jump", "board_jump", 0),
    ("h_hangtime", "board_hang", 1), ("h_landing", "board_land", 0), ("h_roll", "board_roll", 0),
    ("h_left", "board_l", 0), ("h_right", "board_r", 0), ("Jetpack_forward_2", "fly", 1),
    ("Jetpack_changeLane_left", "fly_l", 0), ("Jackpack_changeLane_Right", "fly_r", 0), ("Breathe", "idle", 1),
    ("Intro", "intro", 0), ("superRun", "super_run", 1), ("jump_salto", "salto", 0),
]
GUARD_CLIPS = [("Guard_Run", "run", 1), ("Guard_jump", "jump", 0), ("Guard_Roll", "roll", 0),
               ("Guard_dodgeLeft", "dodge_l", 0), ("Guard_dodgeRight", "dodge_r", 0),
               ("Guard_Catch_Shoulder", "catch", 0, 2.0), ("Guard_whistle", "whistle", 0), ("playIntro", "intro", 0)]
DOG_CLIPS = [("Dog_Run", "run", 1), ("Dog_jump", "jump", 0), ("Dog_dodgeLeft", "dodge_l", 0),
             ("Dog_dodgeRight", "dodge_r", 0), ("Dog_Catch_Shoulder", "catch", 0, 2.0), ("playIntro", "intro", 0)]


def skeleton_of(env, go_name=None):
    """the skeleton under an Animator (the one on go_name, or the first)"""
    for o in env.objects:
        if o.type.name != "Animator":
            continue
        go = o.read().m_GameObject.read()
        if go_name and go.m_Name != go_name:
            continue
        for c in go.m_Component:
            co = c.component.read()
            if co.object_reader.type.name == "Transform":
                return unity.Skeleton(co)
    if go_name is None:
        # no animator: the top of the tree the skin's bones are in
        for o in env.objects:
            if o.type.name == "SkinnedMeshRenderer":
                t = o.read().m_RootBone.read()
                while t.m_Father.m_PathID:
                    t = t.m_Father.read()
                return unity.Skeleton(t)
    raise KeyError(go_name)


def rig_index(sk, rig):
    """each skeleton bone's bone in the rig: itself, or its nearest ancestor there"""
    names = {n: i for i, n in enumerate(rig)}
    out = []
    for i in range(len(sk.names)):
        j = i
        while j >= 0 and (sk.names[j] if j else "") not in names:
            j = sk.parent[j]
        out.append(names[sk.names[j] if j > 0 else ""] if j >= 0 else 0)
    return out


def rig_bones(sk, rig):
    """the skeleton's index of each rig bone"""
    idx = []
    for n in rig:
        if n == "":
            idx.append(0)
        else:
            idx.append([i for i, m in enumerate(sk.names) if m == n][0])
    return idx


def rig_parents(sk, rig):
    bi = rig_bones(sk, rig)
    pos = {s: r for r, s in enumerate(bi)}
    par = []
    for r, s in enumerate(bi):
        p = sk.parent[s]
        while p >= 0 and p not in pos:
            p = sk.parent[p]
        par.append(pos[p] if p >= 0 else -1)
    return par


def pack_clips(o, name, sk, rig, clips, bundle_clips):
    """each clip: frames of every rig bone's local rotation (x127), its bones' places (mm): once for those that
    stay, every frame for those that move"""
    bi = rig_bones(sk, rig)
    nb = len(bi)
    for src, short, loop, *more in clips:
        clip = unity.Clip(bundle_clips[src])
        length = min(clip.length, more[0]) if more else clip.length
        fps = FPS if length <= 1.5 else 15
        n = max(2, int(round(length * fps)) + (0 if loop else 1))
        q_all = np.zeros((n, nb, 4))
        t_all = np.zeros((n, nb, 3))
        for f in range(n):
            t = min(length, f / fps)
            local = clip.sample(sk, t)
            for r, s in enumerate(bi):
                tr = local.get(s, (None, None, None))
                t_all[f, r] = tr[0] if tr[0] is not None else sk.t[s]
                q = np.array(tr[1] if tr[1] is not None else sk.q[s], float)
                q /= np.linalg.norm(q) or 1
                if f > 0 and q_all[f - 1, r] @ q < 0:
                    q = -q   # (the same side as the frame before: no spin the long way round)
                q_all[f, r] = q
        t_mm = np.round(t_all * SCALE * 1000).astype(int)
        moving = [r for r in range(nb) if (np.abs(t_mm[:, r] - t_mm[0, r]) > 1).any()]
        cname = f"clip_{name}_{short}"
        qv = np.clip(np.round(q_all * 127), -127, 127).astype(int).reshape(-1, 4)
        o.c.append(f"static const int8_t {cname}_q[{len(qv)}][4] = {{")
        for i in range(0, len(qv), 8):
            o.c.append("  " + " ".join("{%d,%d,%d,%d}," % tuple(k) for k in qv[i:i + 8]))
        o.c.append("};")
        o.c.append(f"static const int16_t {cname}_t[{nb}][3] = {{" + ",".join("{%d,%d,%d}" % tuple(v) for v in t_mm[0]) + "};")
        if moving:
            o.array("uint8_t", f"{cname}_mv", moving, 24, "animations")
            mt = t_mm[:, moving].reshape(-1, 3)
            o.c.append(f"static const int16_t {cname}_mt[{len(mt)}][3] = {{")
            for i in range(0, len(mt), 8):
                o.c.append("  " + " ".join("{%d,%d,%d}," % tuple(k) for k in mt[i:i + 8]))
            o.c.append("};")
        o.count("animations", len(qv) * 4 + nb * 6 + (len(moving) * n * 6 + len(moving)))
        o.c.append(f"const Clip {cname} = {{{nb}, {n}, {loop}, {len(moving)}, {fps}, {name}_parent, {cname}_q, {cname}_t, "
                   f"{cname + '_mv' if moving else 'NULL'}, {cname + '_mt' if moving else 'NULL'}}};")
        o.h.append(f"extern const Clip {cname};")


# ---------------------------------------------------------------- the people
def skin_model(o, name, sk, rig, parts_src, targets, tex_index, eye_colour=0x2A1A14):
    """parts_src: [(SkinnedMeshRenderer, texture or None)]; a Model of all of them on the rig's bones"""
    ri = rig_index(sk, rig)
    bi = rig_bones(sk, rig)
    W = sk.world()
    verts, tris, parts, pal = [], [], [], []
    used_bones = {}
    for (smr, tex_size), target in zip(parts_src, targets):
        m = smr.m_Mesh.read()
        a = unity.mesh_arrays(m)
        bones = [sk.by_pid[b.m_PathID] for b in smr.m_Bones]
        P = a["pos"]
        # each corner's two strongest rig bones
        nv = len(P)
        rw = np.zeros((nv, len(rig)))
        for k in range(4):
            for j in range(len(bones)):
                sel = a["bidx"][:, k] == j
                rw[sel, ri[bones[j]]] += a["bw"][sel, k]
        order = np.argsort(-rw, axis=1)
        b0, b1 = order[:, 0], order[:, 1]
        w0, w1 = rw[np.arange(nv), b0], rw[np.arange(nv), b1]
        wb = np.where(w0 + w1 > 0, w1 / np.maximum(w0 + w1, 1e-9), 0)
        # the corners where the bind pose puts them, in the skeleton's rest (the mesh's own space)
        world = np.zeros_like(P)
        for k in range(4):
            for j in range(len(bones)):
                sel = a["bidx"][:, k] == j
                if sel.any():
                    M = W[bones[j]] @ a["bindpose"][j]
                    world[sel] += (np.c_[P[sel], np.ones(sel.sum())] @ M.T)[:, :3] * a["bw"][sel, k][:, None]
        N = a["nrm"] if a["nrm"] is not None else np.zeros_like(P)
        subs = a["subs"]
        for si, F in enumerate(subs):
            textured = si == 0 and tex_size is not None
            keep = decimate.decimate(world, a["uv"], b0, F, target if si == 0 else max(8, len(F) // 3))
            if textured:
                uv = a["uv"]
            # parts of at most 255 corners
            pending = [list(f) for f in keep]
            while pending:
                local, ptris = {}, []
                rest = []
                for f in pending:
                    new = [c for c in f if c not in local]
                    if len(local) + len(new) > 255:
                        rest.append(f)
                        continue
                    for c in new:
                        local[c] = len(local)
                    ptris.append(f)
                pending = rest
                v0, t0 = len(verts), len(tris)
                for c, li in sorted(local.items(), key=lambda kv: kv[1]):
                    x, y, z = (world[c] * SCALE * 1000).round().astype(int)
                    n = N[c] / (np.linalg.norm(N[c]) or 1)
                    if textured:
                        u = int(round(uv[c, 0] * tex_size * 4)) % (tex_size * 4 * 8)
                        v = int(round((1 - uv[c, 1]) * tex_size * 4)) % (tex_size * 4 * 8)
                    else:
                        u = v = 0
                    rb0, rb1 = int(b0[c]), int(b1[c])
                    w = int(round(wb[c] * 255)) if w1[c] > 0.02 else 0
                    if not w:
                        rb1 = rb0
                    for rb in (rb0, rb1):
                        used_bones.setdefault(rb, len(used_bones))
                    verts.append((x, y, z, int(round(n[0] * 127)), int(round(n[1] * 127)), int(round(n[2] * 127)), w, u, v,
                                  used_bones[rb0], used_bones[rb1]))
                col = 255 if textured else len(pal)
                if not textured:
                    pal.append(eye_colour)
                for f in ptris:
                    tris.append((local[f[0]], local[f[1]], local[f[2]], col))
                parts.append((v0, len(local), t0, len(ptris), tex_index if textured else -1))
    # the inverse bind of each bone used: rest world matrix, inverted (the corners are in rest world space)
    ib, bone = [], []
    for rb, k in sorted(used_bones.items(), key=lambda kv: kv[1]):
        M = W[bi[rb]].copy()
        M[:3, 3] *= SCALE
        inv = np.linalg.inv(M)
        ib.append(inv[:3, :].flatten().tolist())
        bone.append(rb)
    o.c.append(f"static const MV {name}_v[{len(verts)}] = {{")
    for i in range(0, len(verts), 4):
        o.c.append("  " + " ".join("{%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}," % v for v in verts[i:i + 4]))
    o.c.append("};")
    o.c.append(f"static const Tri {name}_t[{len(tris)}] = {{")
    for i in range(0, len(tris), 8):
        o.c.append("  " + " ".join("{%d,%d,%d,%d}," % t for t in tris[i:i + 8]))
    o.c.append("};")
    o.c.append(f"static const MPart {name}_p[{len(parts)}] = {{" + " ".join("{%d,%d,%d,%d,%d}," % p for p in parts) + "};")
    o.array("uint16_t", f"{name}_pal", [c565((c >> 16) & 255, (c >> 8) & 255, c & 255) for c in pal] or [0], 12, "people")
    o.c.append(f"static const float {name}_ib[{len(ib)}][12] = {{")
    for row in ib:
        o.c.append("  {" + ",".join(cfloat(v) for v in row) + "},")
    o.c.append("};")
    o.array("uint8_t", f"{name}_bone", bone, 24, "people")
    o.c.append(f"const Model model_{name} = {{{name}_p, {name}_v, {name}_t, {name}_pal, {name}_ib, {name}_bone, "
               f"{len(parts)}, {len(bone)}}};")
    o.c.append("")
    o.h.append(f"extern const Model model_{name};")
    o.count("people", len(verts) * 16 + len(tris) * 4 + len(ib) * 48)
    print(f"{name}: {len(verts)} corners, {len(tris)} triangles, {len(bone)} bones")


def renderers(env):
    out = {}
    for ob in env.objects:
        if ob.type.name == "SkinnedMeshRenderer":
            r = ob.read()
            out[r.m_Mesh.read().m_Name] = r
    return out


PEOPLE = [
    # name, bundle, meshes, texture, triangles to keep for each mesh
    ("jake", "jake", ["jake_Mesh", "jake_head"], "jake_outfit0_tex", [330, 90]),
    ("tricky", "tricky", ["tricky_Mesh", "tricky_head", "tricky_outfit_default"], "tricky_tex_low", [300, 80, 60]),
    ("fresh", "fresh", ["fresh_Mesh", "fresh_head"], "fresh_outfit0_tex_low", [330, 90]),
    ("spike", "spike", ["spike_Mesh", "spike_head", "spike_outfit_default"], "spike_tex_low", [300, 80, 30]),
]


def pack_people(o, pk):
    common = pk.env("common-builtin_assets_characters_common_animations")
    runner_clips = {ob.read().m_Name: ob.read() for ob in common.objects if ob.type.name == "AnimationClip"}
    rig_done = False
    for k, n in enumerate(RUNNER_RIG):
        if n:
            o.h.append(f"#define RB_{'HIPS' if n == 'Root_jnt' else n.replace('_jnt', '').upper()} {k}")
    o.h.append("#define RB_SPINE RB_LOWERSPINE")
    for name, bundle, meshes, tex, targets in PEOPLE:
        env = pk.env(f"characters-builtin_assets_{bundle}_common")
        sk = skeleton_of(env)
        if not rig_done:
            par = rig_parents(sk, RUNNER_RIG)
            o.c.append(f"static const int8_t runner_parent[{len(par)}] = {{" + ",".join(map(str, par)) + "};")
            pack_clips(o, "runner", sk, RUNNER_RIG, RUNNER_CLIPS, runner_clips)
            rig_done = True
        img = pk.texture(f"characters-builtin_assets_{bundle}_common", tex).resize((128, 128), Image.LANCZOS)
        ti = o.texture(f"tx_{name}", img, colors=192, what="people")
        smrs = renderers(env)
        skin_model(o, name, sk, RUNNER_RIG, [(smrs[m], 128) for m in meshes], targets, ti)
    # the Inspector (in the Sydney yard's uniform, the classic one) and his dog
    env = pk.env("gamemodes-builtin_assets_chaser_guard_and_dog")
    clips = {}
    for ob in env.objects:
        if ob.type.name == "AnimationClip":
            c = ob.read()
            clips.setdefault(c.m_Name, c)
    smrs = renderers(env)
    sydney = pk.texture("themes-builtin_assets_sydney_config", "Sydney_enemies").resize((128, 128), Image.LANCZOS)
    ti = o.texture("tx_inspector", sydney, colors=128, what="people")
    for name, go, rig, rclips, mesh, target in (("inspector", "Enemy_Guard_Movement", GUARD_RIG, GUARD_CLIPS, "polySurface9", 280),
                                                ("dog", "Enemy_Dog_Movement", DOG_RIG, DOG_CLIPS, "dog", 180)):
        sk = skeleton_of(env, go)
        par = rig_parents(sk, rig)
        o.c.append(f"static const int8_t {name}_parent[{len(par)}] = {{" + ",".join(map(str, par)) + "};")
        pack_clips(o, name, sk, rig, rclips, clips)
        skin_model(o, name, sk, rig, [(smrs[mesh], 128)], [target], ti)



# ---------------------------------------------------------------- the city, the trains, the barriers
THEME = "themes-builtin_assets_subwaycityplantinvasion_config"   # (the Subway City yard; its vines are left out)
ATLAS = "SubwayCityPlantInvasion_environment"


def material_kind(name):
    n = name.lower()
    if "shadow" in n:
        return "shadow"
    if "glass" in n:
        return "glass"
    if "fence" in n:
        return "cutout"
    if any(k in n for k in ("glow", "light", "vines", "spikes", "smoke", "?")):
        return "skip"
    return "opaque"


class Theme:
    def __init__(self, pk):
        self.pk = pk
        self.env = pk.env(THEME)
        self.meshes = pk.objects(THEME, "Mesh")
        self.mats = unity.mesh_materials(self.env)
        tex = pk.objects(THEME, "Texture2D")
        self.tex_names = ["atlas", "fence", "wood", "fountain", "start"]
        self.textures = [np.asarray(tex[ATLAS].image.convert("RGBA")),
                         np.asarray(tex["SubwayCityPlantInvasion_fence"].image.convert("RGBA")),
                         np.asarray(tex["SubwayCityPlantInvasion_woodenFenceMedFillers"].image.convert("RGBA")),
                         np.asarray(tex["SubwayCityPlantInvasion_fountain"].image.convert("RGBA")),
                         np.asarray(tex["SubwayCityPlantInvasion_train_start"].image.convert("RGBA"))]
        self.kinds = []
        self.mat_index = {}

    def material(self, name):
        if name not in self.mat_index:
            n = name.lower()
            tex = 1 if n.endswith("_fence") else 2 if "woodenfence" in n else 3 if "fountain" in n else 4 if "train_start" in n else 0
            kind = material_kind(name)
            self.mat_index[name] = len(self.kinds)
            self.kinds.append((tex, kind))
        return self.mat_index[name]

    def add(self, m, mesh, offset=(0, 0, 0), keep=None):
        a = unity.mesh_arrays(self.meshes[mesh])
        mats = self.mats.get(mesh, [])
        for si, F in enumerate(a["subs"]):
            mat = self.material(mats[si] if si < len(mats) else "?")
            m.add(a["pos"], a["uv"], F, mat, offset, keep)

    def bake(self, m, **kw):
        textures = [self.textures[t] for t, k in self.kinds]
        kinds = [k if k != "cutout" or t != 0 else "opaque" for t, k in self.kinds]
        return bake.bake(m, textures, kinds, **kw)


def corridor(c, n):
    """seen from the tracks (from where the camera goes, ahead of it)"""
    for x in (-25, 0, 25):
        for y in (36, 64):
            for dz in (40, 160):
                q = np.array([x, y, c[2] - dz])
                v = q - c
                if n @ v > 0.05 * np.linalg.norm(v):
                    return True
    return False


# where the camera looks at a stretch from: across the tracks, high and low, before it and inside it
ENV_CAMS = [((x, y, z), (x * 0.5, 15, z + 120)) for x in (-15, 0, 15) for y in (38, 64) for z in (-60, 20, 80)]


def env_weight(n, d):
    if abs(n[0]) > 0.9:
        return 1.0
    if n[2] < -0.9:
        return 0.8
    if n[1] > 0.9:
        return 0.5
    if n[1] < -0.9:
        return 0.15
    return 0.6


def write_bmodel(o, name, quads, atlas, colors=128, shift=(0, 0, 0), what="city"):
    """baked faces: each a convex polygon (corners in cm and their places in the texture), and its picture's
    average colour when it has few holes (far away it is drawn plain)"""
    ti = o.texture(f"tx_{name}", atlas, clear=True, colors=colors, what=what)
    A = np.asarray(atlas)
    faces, corners = [], []
    lo, hi = np.full(3, 1e9), np.full(3, -1e9)
    for pts, uv in quads:
        c = (np.asarray(pts) + np.asarray(shift)) * SCALE * 100
        lo, hi = np.minimum(lo, c.min(0)), np.maximum(hi, c.max(0))
        x0, y0 = int(uv[:, 0].min()), int(uv[:, 1].min())
        x1, y1 = int(np.ceil(uv[:, 0].max())), int(np.ceil(uv[:, 1].max()))
        pic = A[y0:y1, x0:x1]
        avg = 0
        if pic.size and (pic[..., 3] > 127).mean() > 0.92:
            rgb = pic[..., :3][pic[..., 3] > 127].mean(axis=0)
            avg = c565(*rgb) or 1
        faces.append((len(corners), len(pts), avg))
        for p, t in zip(c, uv):
            corners.append(tuple(int(round(v)) for v in p) + (int(round(t[0] * 4)), int(round(t[1] * 4))))
    o.c.append(f"static const BCorner {name}_c[{len(corners)}] = {{")
    for i in range(0, len(corners), 6):
        o.c.append("  " + " ".join("{%d,%d,%d,%d,%d}," % k for k in corners[i:i + 6]))
    o.c.append("};")
    o.c.append(f"static const BFace {name}_f[{len(faces)}] = {{" + " ".join("{%d,%d,%d}," % f for f in faces) + "};")
    box = [int(round(v)) for v in list(lo) + list(hi)] if faces else [0] * 6
    o.c.append(f"const BModel bm_{name} = {{{name}_f, {name}_c, {len(faces)}, {ti}, 0, {{{','.join(map(str, box))}}}}};")
    o.c.append("")
    o.h.append(f"extern const BModel bm_{name};")
    o.count(what, len(faces) * 6 + len(corners) * 10)
    return len(faces)


NEAR_X = 120          # (units: the city right along the tracks; beyond it, the sky)
STREETS = ["low_01", "low_02", "med_01", "med_02", "med_03", "high_01", "high_02", "high_03"]
SECTIONS = ["pillars_start", "pillars_mid", "pillars_end", "station_start", "station_mid", "station_end", "tube_start",
            "tube_mid", "tube_end"]


def pack_city(o, th):
    def near(p):
        return abs(p[:, 0].mean()) < NEAR_X
    for st in STREETS:
        for side, sign in (("left", -1), ("right", 1)):
            m = bake.Mesh()
            th.add(m, f"{st}_{side}", keep=near)
            if f"{st}_{side}_LOD1" in th.meshes:
                th.add(m, f"{st}_{side}_LOD1", keep=near)
            quads, atlas = th.bake(m, visible=corridor, texel=1.6, min_area=20, major_area=150, budget=24,
                                   weight=env_weight, atlas_max=(128, 256), name=f"{st}_{side}", cams=ENV_CAMS)
            n = write_bmodel(o, f"{st}_{side[0]}", quads, atlas)
            print(f"{st}_{side}: {len(m.P)} triangles, {n} faces")
    for sec in SECTIONS:
        m = bake.Mesh()
        th.add(m, sec, keep=near)
        if sec == "station_mid":
            th.add(m, "station_platforms", keep=near)
        # (the tunnel's hall is all arches: its planes taken more loosely)
        loose = dict(plane_cos=0.85, plane_gap=3.0) if sec.startswith("tube") else {}
        quads, atlas = th.bake(m, visible=corridor, texel=1.8, min_area=20, major_area=150, budget=40, weight=env_weight,
                               atlas_max=(256, 256), name=sec, cams=ENV_CAMS, **loose)
        n = write_bmodel(o, sec, quads, atlas, colors=192)
        print(f"{sec}: {len(m.P)} triangles, {n} faces")


def car_visible(c, n):
    return n[2] < 0.5 and n[1] > -0.5


CARS = [("sub", "train_sub_LOD0"), ("sub_wagon", "train_sub_wagon_LOD0"), ("standard", "train_standard_LOD0"),
        ("standard_wagon", "train_standard_wagon_LOD0"), ("cargo1", "train_cargo01_LOD0"), ("cargo2", "train_cargo02_LOD0"),
        ("freight", "train_freight_LOD0"), ("ramp", "train_ramp_LOD0")]
BARRIERS = [("jump", "blocker_jump"), ("roll", "blocker_roll"), ("standard", "blocker_standard")]


def pack_trains(o, th):
    for name, mesh in CARS:
        m = bake.Mesh()
        th.add(m, mesh)
        head = name in ("sub", "standard", "freight", "ramp")
        quads, atlas = th.bake(m, visible=car_visible, texel=0.5, min_area=6, major_area=60, budget=18,
                               weight=lambda n, d: 1.0 if n[2] < -0.9 else 0.9 if abs(n[0]) > 0.9 else 0.7,
                               atlas_max=(256, 128) if head else (128, 128), name=name)
        n = write_bmodel(o, f"car_{name}", quads, atlas, shift=(0, 0, 30), what="trains")
        print(f"car {name}: {len(m.P)} triangles, {n} faces")
    for name, mesh in BARRIERS:
        m = bake.Mesh()
        th.add(m, mesh)
        cx = 0   # (the barriers stand in the middle of their track; their shadow is beside them)
        quads, atlas = th.bake(m, visible=lambda c, n: n[2] < 0.3 and n[1] > -0.5, texel=0.35, min_area=3, major_area=20,
                               budget=10, atlas_max=(128, 64), name=name)
        n = write_bmodel(o, f"barrier_{name}", quads, atlas, shift=(-cx, 0, 0), what="trains")
        print(f"barrier {name}: {len(m.P)} triangles, {n} faces, centre x {cx:.1f}")


def pack_ground(o, th):
    """the tracks seen from above: the colours across the yard (from -6 m to 6 m, 32 a metre), between two sleepers
    and on one (the ground under them, the ballast, the sleepers and the rails of the game's track)"""
    m = bake.Mesh()
    for x in (-20, 0, 20):
        th.add(m, "track", offset=(x, 0, 0))
    P, UV, M = m.arrays()
    textures = [th.textures[t] for t, k in th.kinds]
    kinds = [k if k != "shadow" else "skip" for t, k in th.kinds]
    N = np.cross(P[:, 1] - P[:, 0], P[:, 2] - P[:, 0])
    up = N[:, 1] > 0
    W, H = 384, 120          # x: -6..6 m; z: 0..15 units (one sleeper to the next), 8 rows a unit
    P2 = np.stack([(P[..., 0] * SCALE + 6) * 32, P[..., 2] * 8], axis=2)
    lights = np.ones(len(P))
    img, _ = bake.raster_ortho(P2[up], P[up][..., 1], UV[up], M[up], textures, kinds, lights[up], W, H)
    Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGBA").save("/tmp/subway_ground.png") if os.environ.get("GROUND_PNG") else None
    rgb = img[..., :3]
    # outside the tracks: the darkest ground colour there is
    have = img[..., 3] > 0
    fill = rgb[have].reshape(-1, 3)
    out = []
    for row in (2, 56):        # (between two sleepers; through one)
        r = rgb[row].copy()
        miss = img[row, :, 3] == 0
        r[miss] = np.percentile(fill, 20, axis=0)
        out.append(r)
    vals = [int(c[0]) << 16 | int(c[1]) << 8 | int(c[2]) for r in out for c in r]
    o.array("uint32_t", "ground_rgb", vals, 12, "city", static=False)
    o.h.append("extern const uint32_t ground_rgb[2 * 384];   /* between two sleepers, then on one */")




# ---------------------------------------------------------------- power-ups, keys, coins, boards
def rigid_model(o, name, a, tex_size, tex_index, target, scale=1.0, centre=True, subs=None):
    """a model on one bone (its own space, in metres): the game's mesh with fewer triangles"""
    P = a["pos"].copy()
    F = np.concatenate([a["subs"][i] for i in (subs if subs is not None else range(len(a["subs"])))])
    used = np.unique(F)
    # (some meshes keep a far point for their effects: left out)
    if centre:
        lo, hi = P[used].min(0), P[used].max(0)
        P -= [(lo[0] + hi[0]) / 2, (lo[1] + hi[1]) / 2, (lo[2] + hi[2]) / 2]
    keep = decimate.decimate(P, a["uv"], None, F, target)
    N = a["nrm"] if a["nrm"] is not None else np.zeros_like(P)
    uv = a["uv"]
    verts, tris, parts = [], [], []
    pending = [list(f) for f in keep]
    while pending:
        local, ptris, rest = {}, [], []
        for f in pending:
            new = [c for c in f if c not in local]
            if len(local) + len(new) > 255:
                rest.append(f)
                continue
            for c in new:
                local[c] = len(local)
            ptris.append(f)
        pending = rest
        v0, t0 = len(verts), len(tris)
        for c, li in sorted(local.items(), key=lambda kv: kv[1]):
            x, y, z = (P[c] * SCALE * scale * 1000).round().astype(int)
            n = N[c] / (np.linalg.norm(N[c]) or 1)
            u = int(round(uv[c, 0] * tex_size * 4)) % (tex_size * 4 * 8)
            v = int(round((1 - uv[c, 1]) * tex_size * 4)) % (tex_size * 4 * 8)
            verts.append((x, y, z, int(round(n[0] * 127)), int(round(n[1] * 127)), int(round(n[2] * 127)), 0, u, v, 0, 0))
        for f in ptris:
            tris.append((local[f[0]], local[f[1]], local[f[2]], 255))
        parts.append((v0, len(local), t0, len(ptris), tex_index))
    o.c.append(f"static const MV {name}_v[{len(verts)}] = {{")
    for i in range(0, len(verts), 4):
        o.c.append("  " + " ".join("{%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d}," % v for v in verts[i:i + 4]))
    o.c.append("};")
    o.c.append(f"static const Tri {name}_t[{len(tris)}] = {{")
    for i in range(0, len(tris), 8):
        o.c.append("  " + " ".join("{%d,%d,%d,%d}," % t for t in tris[i:i + 8]))
    o.c.append("};")
    o.c.append(f"static const MPart {name}_p[{len(parts)}] = {{" + " ".join("{%d,%d,%d,%d,%d}," % p for p in parts) + "};")
    o.c.append(f"static const float {name}_ib[1][12] = {{{{1,0,0,0, 0,1,0,0, 0,0,1,0}}}};")
    o.c.append(f"static const uint8_t {name}_bone[1] = {{0}};")
    o.c.append(f"static const uint16_t {name}_pal[1] = {{0}};")
    o.c.append(f"const Model model_{name} = {{{name}_p, {name}_v, {name}_t, {name}_pal, {name}_ib, {name}_bone, {len(parts)}, 1}};")
    o.c.append("")
    o.h.append(f"extern const Model model_{name};")
    o.count("props", len(verts) * 16 + len(tris) * 4)
    print(f"{name}: {len(verts)} corners, {len(tris)} triangles")


def pack_props(o, pk):
    props = pk.texture("common-builtin_assets_textures_common", "common_props_tex").resize((128, 128), Image.LANCZOS)
    ti = o.texture("tx_props", props, colors=128, what="props")
    pu = "common-builtin_assets_models_powerups"
    for name, bundle, mesh, target, scale in (("p_jetpack", pu, "powerups_jetpack", 110, 1.0),
                                              ("p_magnet", pu, "powerups_coinMagnet", 60, 1.0),
                                              ("p_sneakers", pu, "powerups_superSneakers", 80, 1.0),
                                              ("p_double", pu, "powerups_2xMultiplier", 70, 1.0),
                                              ("p_mystery", pu, "powerups_mysteryBox_default", 90, 1.0),
                                              ("p_key", "common-builtin_assets_models_common", "currency_key", 60, 1.0)):
        a = pk.mesh(bundle, mesh)
        # (left out: the far point some keep for their effect)
        far = a["pos"][:, 1] < -20
        if far.any():
            for k, F in enumerate(a["subs"]):
                a["subs"][k] = F[~far[F].any(axis=1)]
        rigid_model(o, name, a, 128, ti, target, scale, subs=[0])
    for name, bundle, mesh, tex in (("p_board", "boards-builtin_assets_default_common", "board_default_base", "DefaultBoard_tex"),
                                    ("p_bouncer", "boards-builtin_assets_bouncer_common", "board_bouncer_base", "BouncerBoard_tex"),
                                    ("p_daredevil", "boards-builtin_assets_daredevil_common", "board_daredevil_base", "DareDevilBoard_tex")):
        img = pk.texture(bundle, tex).resize((64, 64), Image.LANCZOS)
        bi = o.texture(f"tx_{name}", img, colors=64, what="props")
        a = pk.mesh(bundle, mesh)
        rigid_model(o, name, a, 64, bi, 70, 1.0, centre=False)
    # the coin's face, seen straight on, for the coins in the world
    a = pk.mesh("common-builtin_assets_models_common", "currency_coin")
    coin = np.asarray(pk.texture("common-builtin_assets_textures_common", "coin_texture"))
    P, UV = a["pos"], a["uv"]
    F = np.concatenate(a["subs"])
    lo, hi = P.min(0), P.max(0)
    t = (hi[1] - lo[1]) / 64
    P2 = np.stack([(P[F][..., 0] - lo[0]) / t, (hi[1] - P[F][..., 1]) / t], axis=2)
    Z = -P[F][..., 2]
    img, _ = bake.raster_ortho(P2, Z, UV[F], np.zeros(len(F), int), [coin], ["opaque"], np.ones(len(F)), 64, 64)
    o.texture("tx_coin", Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGBA"), clear=True, colors=48, what="props")


# ---------------------------------------------------------------- the screens' pictures
ATLAS_UI = "sprites-builtin_assets_ui_spriteatlas_default"


def sprites(pk, bundle):
    """{name: RGBA picture} of a bundle's sprites"""
    out = {}
    for ob in pk.env(bundle).objects:
        if ob.type.name == "Sprite":
            d = ob.read()
            try:
                out[d.m_Name] = d.image.convert("RGBA")
            except Exception:
                pass
    return out


def fit(img, size):
    """a picture cropped to what shows and scaled to fit size x size, centred"""
    img = img.crop(img.getbbox())
    w, h = img.size
    k = size / max(w, h)
    img = img.resize((max(1, round(w * k)), max(1, round(h * k))), Image.LANCZOS)
    out = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    out.alpha_composite(img, ((size - img.width) // 2, (size - img.height) // 2))
    return out


def render_model(pk, bundle, mesh, tex, size, yaw=0.5, pitch=0.25):
    """the game's model seen from the front, a little turned, as a picture (for the power-ups' icons)"""
    a = pk.mesh(bundle, mesh)
    P = a["pos"].copy()
    F = a["subs"][0]
    F = F[~(P[F][..., 1] < -20).any(axis=1)]
    cy, sy, cp, sp = np.cos(yaw), np.sin(yaw), np.cos(pitch), np.sin(pitch)
    R = np.array([[cy, 0, sy], [0, 1, 0], [-sy, 0, cy]]) @ np.eye(3)
    R = np.array([[1, 0, 0], [0, cp, -sp], [0, sp, cp]]) @ R
    Q = P @ R.T
    lo, hi = Q[np.unique(F)].min(0), Q[np.unique(F)].max(0)
    big = size * 4
    t = max(hi[0] - lo[0], hi[1] - lo[1]) / (big - 8)
    P2 = np.stack([(Q[F][..., 0] - lo[0]) / t + 4, (hi[1] - Q[F][..., 1]) / t + 4], axis=2)
    N = np.cross(Q[F][:, 1] - Q[F][:, 0], Q[F][:, 2] - Q[F][:, 0])
    front = N[:, 2] < 0
    light = np.ones(len(F))
    img, _ = bake.raster_ortho(P2[front], -Q[F][front][..., 2], a["uv"][F][front], np.zeros(front.sum(), int), [tex], ["opaque"],
                               light[front], big, big)
    im = Image.fromarray(np.clip(img, 0, 255).astype(np.uint8), "RGBA")
    return fit(im, size)


def slice9(o, name, img, border, size_border):
    """a button or panel picture for stretching (its corners kept): scaled so its border is size_border pixels,
    kept as colours and alphas"""
    k = size_border / border
    w, h = max(2 * size_border + 2, round(img.width * k)), max(2 * size_border + 2, round(img.height * k))
    im = np.asarray(img.resize((w, h), Image.LANCZOS)).astype(int)
    px = [c565(*c[:3]) for c in im.reshape(-1, 4)]
    a = [int(c[3]) for c in im.reshape(-1, 4)]
    o.array("uint16_t", f"{name}_px", px, 16, "screens")
    o.array("uint8_t", f"{name}_a", a, 32, "screens")
    o.c.append(f"const Slice {name} = {{{w}, {h}, {size_border}, {size_border}, {name}_px, {name}_a}};")
    o.c.append("")
    o.h.append(f"extern const Slice {name};")


def pack_screens(o, pk):
    """the game's own screen pictures: buttons, icons, the pause button, the score's star, the logo"""
    ob = {}
    for x in pk.env(ATLAS_UI).objects:
        if x.type.name == "Sprite":
            d = x.read()
            ob[d.m_Name] = d
    ui = {n: d.image.convert("RGBA") for n, d in ob.items()}
    props = np.asarray(pk.texture("common-builtin_assets_textures_common", "common_props_tex"))
    pu = "common-builtin_assets_models_powerups"
    extra = {}
    for b in ("sprites-builtin_assets_ui_spriteatlas_fc_headstart", "sprites-builtin_assets_ui_spriteatlas_fc_booster",
              "sprites-builtin_assets_icon_feature_magnet"):
        extra.update(sprites(pk, b))
    # buttons and panels, stretched on the calculator round their corners
    for name, spr in (("sl_green", "Btn_Green_01"), ("sl_red", "Btn_Red_01"), ("sl_blue", "Btn_Blue_01"), ("sl_gold", "Btn_Gold_01"),
                      ("sl_grey", "Btn_Grey_01"), ("sl_purple", "Btn_Purple_01"), ("sl_light", "Btn_Blue_02")):
        d = ob[spr]
        bd = d.m_Border
        border = max(4.0, min(bd.x, bd.y, bd.z, bd.w) if min(bd.x, bd.y, bd.z, bd.w) > 0 else ui[spr].height / 3)
        slice9(o, name, ui[spr], border, 7)
    icons = {
        "coin": ui["Icon_Coin"], "key": ui["Icon_Key"], "board": ui["Icon_Board"], "jetpack": ui["icon_jetpack_small"],
        "mystery": ui["icon_box_mysteryBox_Outlined"], "headstart": extra["Headstart_0"], "booster": extra["ScoreBooster_0"],
        "star": ui["Icon_ScoreMultiplier_Small"], "check": ui["Icon_Check_02"], "lock": ui["Icon_Lock"],
        "trophy": ui["Icon_Trophy"], "gift": ui["Icon_Gift"], "timer": ui["Icon_Timer_01"],
    }
    for key, img in icons.items():
        for size, suffix in ((24, ""), (15, "_s")):
            o.image(f"im_{key}{suffix}", fit(img, size))
    for key, mesh in (("sneakers", "powerups_superSneakers"), ("magnet", "powerups_coinMagnet"), ("double", "powerups_2xMultiplier")):
        for size, suffix in ((24, ""), (15, "_s")):
            o.image(f"im_{key}{suffix}", render_model(pk, pu, mesh, props, size))
    # the pause button: the light blue button with its two bars
    S = 8
    pb = Image.new("RGBA", (26 * S, 26 * S), (0, 0, 0, 0))
    btn = ui["Btn_Blue_02"]
    bd = ob["Btn_Blue_02"].m_Border
    pb.alpha_composite(_nine(btn, (bd.x, bd.y, bd.z, bd.w), 26 * S, 26 * S, 8 * S))
    from PIL import ImageDraw
    dr = ImageDraw.Draw(pb)
    for cx in (9, 17):
        dr.rounded_rectangle(((cx - 3) * S, 6.5 * S, (cx + 3) * S, 18.5 * S), radius=2 * S, fill=(56, 64, 97, 255))
        dr.rounded_rectangle(((cx - 1.8) * S, 7.7 * S, (cx + 1.8) * S, 17.3 * S), radius=1.3 * S, fill=(255, 255, 255, 255))
    o.image("im_pause", pb.resize((26, 26), Image.LANCZOS))
    # the bricks the pause screen is decorated with (the game tints them its dark blue)
    br = ui["Spr_Dec_Bricks_01"]
    br = br.crop(br.getbbox())
    br = br.resize((56, round(br.height * 56 / br.width)), Image.LANCZOS)
    tint = np.asarray(br).copy()
    tint[..., 0], tint[..., 1], tint[..., 2] = 18, 31, 51
    o.image("im_bricks", Image.fromarray(tint, "RGBA"), colors=4)
    logo = sprites(pk, "challengeimages-builtin_assets_logo_subway_surfers")["logo_subway_surfers"]
    logo = logo.crop(logo.getbbox())
    o.image("im_logo", logo.resize((132, round(logo.height * 132 / logo.width)), Image.LANCZOS))


def _nine(img, border, w, h, b):
    """img stretched to w x h round its corners (border: its left, bottom, right, top in its pixels) drawn b wide"""
    l, bt, r, t = (int(round(v)) for v in border)
    if min(l, bt, r, t) <= 0:
        return img.resize((w, h), Image.LANCZOS)
    W, H = img.size
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    xs = [(0, l, 0, b), (l, W - r, b, w - b), (W - r, W, w - b, w)]
    ys = [(0, t, 0, b), (t, H - bt, b, h - b), (H - bt, H, h - b, h)]
    for sx0, sx1, dx0, dx1 in xs:
        for sy0, sy1, dy0, dy1 in ys:
            if sx1 > sx0 and sy1 > sy0 and dx1 > dx0 and dy1 > dy0:
                out.alpha_composite(img.crop((sx0, sy0, sx1, sy1)).resize((dx1 - dx0, dy1 - dy0), Image.LANCZOS), (dx0, dy0))
    return out



def main(package):
    folder = Path(package)
    aa = sorted((folder / "assets" / "aa" / "Android").iterdir())[-1]
    pk = unity.Package(str(aa))
    o = Out()
    if not os.environ.get("SKIP_PEOPLE"):
        pack_people(o, pk)
    pack_props(o, pk)
    pack_screens(o, pk)
    th = Theme(pk)
    pack_ground(o, th)
    pack_trains(o, th)
    pack_city(o, th)
    o.write()


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Documents/Assets/gamesrc/subway/apk"))
