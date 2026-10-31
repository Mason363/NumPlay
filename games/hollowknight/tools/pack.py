"""Hollow Knight for the NumWorks calculator: makes src/data.bin (and src/data.h) from the game's files.
HOLLOWKNIGHT=<the game's Data folder> python3 pack.py"""
import os, sys, struct, math, lzma, time, multiprocessing as mp
import numpy as np
HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import unity, scene, visible, art, actors, coll, ents, font, text, vm

SRC = os.path.join(HERE, "..", "src")
ALL_ROOMS = [l.strip() for l in open(os.path.join(HERE, "rooms.txt")) if l.strip() and not l.startswith("#")]
# (scenes loaded with others are part of those: two variants of the room, by a PlayerData bool)
ROOMS = [r for r in ALL_ROOMS if r not in scene.additive_scenes(ALL_ROOMS)]
VARIANT_ROOMS = [r for r in ROOMS if scene.additive(r)]
STRINGS = ents.Strings()
PERSIST = ents.Persist()
SPRITES = ents.Sprites()
TEXTS = text.Texts()
# the prompt markers' labels (Arrow Prompt New's)
PROMPTS = ["Listen", "Rest", "Inspect", "Enter", "Sit", "Shop", "Travel", "Ascend", "Descend", "Exit", "Accept", "Trade",
           "Watch", "Challenge"]
ROOM_DATA = {}   # name -> (coll.room's, ents.room's)
ROOM_OWNERS = {}   # (name, variant) -> each collider's owner: (scene, object id)


def room_data(name, variant=0):
    """A room's ground and objects (once: the objects' sprites go into SPRITES) -> (coll.room's, (records, {(scene,
    object id): group}), {which: its groups (first, last)}). With the scenes it loads with it: variant 0 has the first's
    (which 1), variant 1 the alternative's (which 2); the groups of both numbered the same in either, after the room's;
    (scene, None): the group of a scene's sprites with none of their own."""
    if (name, variant) not in ROOM_DATA:
        d = unity.scene(name)
        st = scene.settings(d)
        w, h = st["size"] if st["size"] else (40, 24)
        solid, segs, cols, owners = coll.room(d, w, h)
        recs, gr = ents.room(d, ROOMS, STRINGS, PERSIST, name, SPRITES, owners)
        groups = {(name, k): g for k, g in gr.items()}
        segs, cols, recs = list(segs), list(cols), list(recs)
        own = [(name, oid) for oid in owners]
        gb = max(gr.values(), default=0)
        spans = {}
        for an, flag, val, which in scene.additive(name):
            da = unity.scene(an)
            _, asegs, acols, aown = coll.room(da, w, h)
            ar, ag = ents.room(da, ROOMS, STRINGS, PERSIST, an, SPRITES, aown, rec_base=len(recs), col_base=len(cols),
                               group_base=gb)
            cond = max(ag.values(), default=gb) + 1
            groups.update({(an, k): g for k, g in ag.items()})
            groups[(an, None)] = cond
            spans[which] = (gb + 1, cond)
            gb = cond
            if (which == 1) == (variant == 0):
                segs += [(x0, y0, x1, y1, ci + len(cols)) for x0, y0, x1, y1, ci in asegs]
                cols += acols
                own += [(an, oid) for oid in aown]
                recs += ar
        assert gb < ents.MAX_GROUPS, (name, gb)
        ROOM_DATA[(name, variant)] = ((solid, segs, cols, owners), (recs, groups), spans)
        ROOM_OWNERS[(name, variant)] = own
    return ROOM_DATA[(name, variant)]

SECTIONS = ["TEX", "PAL", "TDAT", "ROOMS", "RBLOB", "PRIOR", "SOFT", "SPR", "CLIP", "STR", "TEXT", "FONT", "PHASH",
            "SCOL", "RVAR", "HEADS", "VM", "VMDEF"]


def _pdf_flags():
    return ents.pd_flags()
BLENDS = {"alpha": 0, "add": 1, "screen": 2, "linearlight": 3, "overlay": 4, "multiply": 5}
F_LIT, F_ROT, F_SOLID, F_DYN, F_GRASS = 8, 16, 32, 64, 128

LZ = [{"id": lzma.FILTER_LZMA1, "lc": 0, "lp": 0, "pb": 0, "dict_size": 1 << 16, "preset": 9 | lzma.PRESET_EXTREME}]


def lz(b):
    return lzma.compress(b, format=lzma.FORMAT_RAW, filters=LZ)


def f16(x):
    return int(np.array(x, np.float16).view(np.uint16))


def i16(x):
    return max(-32768, min(32767, int(round(x))))


def f16s(x):
    """A half float's bits as a signed 16-bit value (for struct 'h')."""
    v = f16(x)
    return v - 65536 if v >= 32768 else v


class Blob:
    def __init__(self):
        self.b = bytearray()

    def align(self, n=4):
        while len(self.b) % n:
            self.b.append(0)

    def add(self, data, align=4):
        self.align(align)
        o = len(self.b)
        self.b += data
        return o


SECTOR = 16.0        # sectors along the room's long camera axis (units)
GLOBAL_SPAN = 40.0   # instances seen over a longer stretch of camera positions are always kept
MARGIN = 2.0


def inst_record(it, tex_id, tints):
    """An instance: where, its texture, how it is drawn."""
    q = it.quad      # bl, br, tr, tl (world)
    tl, tr, bl = q[3], q[2], q[0]
    flags = BLENDS.get(it.blend, 0)
    if it.lit:
        flags |= F_LIT
    if it.group:
        flags |= F_DYN   # (in a render group: a byte more)
    if it.grass:
        flags |= F_GRASS
    color = it.color
    if it.solid and it.sprite.name == "black_solid":
        color = (0, 0, 0, color[3])
    col = tuple(int(round(min(1, max(0, c)) * 255)) for c in color)
    if col not in tints:
        tints.append(col)
    tint = min(255, tints.index(col))
    rot = 0
    if it.solid:
        flags |= F_SOLID
        tex = 0xFFFF
        ax, ay = min(p[0] for p in q), max(p[1] for p in q)
        a, b = max(p[0] for p in q) - ax, -(ay - min(p[1] for p in q))
    else:
        t = tex_id[id(it.variant)]
        tex, tw, th = t
        eu = (tr - tl) / tw
        ev = (bl - tl) / th
        ax, ay = tl[0], tl[1]
        a = math.hypot(eu[0], eu[1])
        ang = math.atan2(eu[1], eu[0])
        # flips: keep the angle small, put the sign in a
        if abs(ang) > math.pi / 2:
            ang = ang - math.pi if ang > 0 else ang + math.pi
            a = -a
        c, s_ = math.cos(ang), math.sin(ang)
        b = ev[0] * -s_ + ev[1] * c        # along (-sin, cos)
        rot = int(round(ang / (2 * math.pi) * 65536))
        if rot:
            flags |= F_ROT
    return (ax, ay, it.z, tex, tint, flags, a, b, rot, it.group)


def uvar(u):
    out = bytearray()
    while u >= 128:
        out.append(u & 127 | 128)
        u >>= 7
    out.append(u)
    return out


def svar(v):
    return uvar(v << 1 if v >= 0 else ((-v) << 1) - 1)


A_XY12 = 64       # (aux: x and y steps in 12 bits each)
MAX_STEP = 3      # (rank steps a head can hold)


def stream_insts(recs):
    """A sector's instances as sector_stream codes them: (flags, aux, rank step, whether x and y fit 12 bits, the
    bytes before x and y, x and y steps, the bytes after)."""
    out = []
    pr, pt, px, py, pz, pa, pb, pg = -1, 0, 0, 0, 0, 0, 0, None
    for rank, (ax, ay, z, tex, tint, flags, a, b, rot, rgroup), group in recs:
        X, Y, Z, A, B = i16(ax * 64), i16(ay * 64), i16(z * 128), f16s(a) & 0xFFFF, f16s(b) & 0xFFFF
        aux = 0
        if tint:
            aux |= 1
        if tex == pt:
            aux |= 2
        if Z == pz:
            aux |= 4
        bm = 1 if B == A else 2 if B == A ^ 0x8000 else 3 if B == pb else 0
        aux |= bm << 3
        if A == pa:
            aux |= 32
        if group != pg:
            aux |= 128
        pre = bytearray()
        if aux & 128:
            pre += bytes([group[0]]) + svar(group[1])
        if not aux & 2:
            pre += svar((tex - pt + 32768 & 0xFFFF) - 32768)   # (the game's sum wraps at 16 bits)
        if tint:
            pre.append(tint)
        post = bytearray()
        if not aux & 4:
            post += svar(Z - pz)
        if not aux & 32:
            post += struct.pack("<H", A)
        if bm == 0:
            post += struct.pack("<H", B)
        if flags & F_ROT:
            post += struct.pack("<h", rot)
        if flags & F_DYN:
            post.append(rgroup)
        dx, dy = X - px, Y - py
        out.append((flags, aux, rank - pr, -2048 <= dx < 2048 and -2048 <= dy < 2048, bytes(pre), dx, dy, bytes(post)))
        pr, pt, px, py, pz, pa, pb, pg = rank, tex, X, Y, Z, A, B, group
    return out


def head_symbol(inst):
    """The head an instance would rather have: (flags, aux, rank step or 0 if it is given after)"""
    flags, aux, step, xy12 = inst[:4]
    return flags, aux | (A_XY12 if xy12 else 0), step if step <= MAX_STEP else 0


def head_table(streams):
    """The 255 most common heads (sector_stream) of all the rooms' instances: (bytes, symbol -> code)"""
    import collections
    count = collections.Counter(head_symbol(i) for s in streams for i in s)
    top = [k for k, _ in count.most_common(255)]
    return b"".join(struct.pack("<I", f | a << 8 | s << 16) for f, a, s in top), {k: i for i, k in enumerate(top)}


def sector_stream(insts, heads):
    """A sector's instances in draw order, each relative to the one before (read in place by room.c): a head (a code
    of the HEADS table: flags, aux, rank step; 255: flags and aux follow), [rank step - 1 (none in the head)], [layer,
    order], [texture step], [tint], x step, y step (12 bits each if A_XY12, else on their own), [z step], [a], [b],
    [angle], [render group (F_DYN)]. aux: 1 tint, 2 same texture, 4 same z, 8|16 b: 0 its own, 1 = a, 2 = -a, 3 same as
    before, 32 same a, 64 A_XY12, 128 another sorting layer or order."""
    out = bytearray()
    for flags, aux, step, xy12, pre, dx, dy, post in insts:
        xy = struct.pack("<I", (dx & 0xFFF) | (dy & 0xFFF) << 12)[:3] if xy12 else svar(dx) + svar(dy)
        best = None
        # (the cheapest head: the one wanted, else one with the steps on their own)
        for x12 in ((True, False) if xy12 else (False,)):
            for st in ((step, 0) if step <= MAX_STEP else (0,)):
                code = heads.get((flags, aux | (A_XY12 if x12 else 0), st))
                if code is None:
                    continue
                b = bytes([code]) + (uvar(step - 1) if not st else b"") + pre + \
                    (xy if x12 or not xy12 else svar(dx) + svar(dy)) + post
                if best is None or len(b) < len(best):
                    best = b
        if best is None:
            a = aux | (A_XY12 if xy12 else 0)
            best = bytes([255, flags, a]) + uvar(step - 1) + pre + xy + post
        out += best
    return bytes(out)


def room_blobs(name, keep, st, tex_id, variant=0):
    """A room: its header (kept in RAM) and its instances in sectors (decoded when the camera is near)."""
    w, h = st["size"] if st["size"] else (40, 24)
    cx0, cx1, cy0, cy1 = scene.camera_range(st) if st["size"] else (20, 20, 12, 12)
    axis = 0 if (cx1 - cx0) >= (cy1 - cy0) else 1
    lo, hi = (cx0, cx1) if axis == 0 else (cy0, cy1)
    HWU, HHU = scene.VIEW_W / 2 / scene.FOCAL, scene.VIEW_H / 2 / scene.FOCAL
    (solid, segs, cols, _), (erecs, groups), _ = room_data(name, variant)
    for it in keep:
        it.group = groups.get((it.scene, it.obj["id"]), groups.get((it.scene, None), 0) if it.which else 0)
    tints = [(255, 255, 255, 255)]
    secs = {}
    for rank, it in enumerate(keep):
        rec = inst_record(it, tex_id, tints)
        q = np.array(it.quad)
        dist = max(0.5, it.z - scene.CAMZ)
        half = (HWU if axis == 0 else HHU) * dist
        p0, p1 = (q[:, 0].min(), q[:, 0].max()) if axis == 0 else (q[:, 1].min(), q[:, 1].max())
        va, vb = max(lo, p0 - half - MARGIN), min(hi, p1 + half + MARGIN)
        if it.dynamic or vb - va > GLOBAL_SPAN or vb < va:
            sec = -1
        else:
            sec = int((va - lo) // SECTOR)
        e = secs.setdefault(sec, [[], 1e9, -1e9])
        e[0].append((rank, rec, (it.layer, it.order)))
        e[1] = min(e[1], va)
        e[2] = max(e[2], vb)
    order = sorted(secs.keys())
    luts, sat = scene.grading_luts(st, 64)
    lut = bytes(int(round(luts[c][i] * 255)) for c in range(3) for i in range(64))
    bz = st["blur_z"] if st["blur_z"] is not None else 1000.0
    blobs = []
    table = bytearray()
    for k in order:
        recs, va, vb = secs[k]
        blobs.append((stream_insts(recs), len(recs)))
        if k < 0:
            va, vb = -1e9, 1e9
        table += struct.pack("<IIHHff", 0, 0, len(recs), 0, va, vb)   # (offsets filled in by main)
    HDR = 84 + 192
    gw, gh, cells = coll.grid(segs, w, h)
    assert len(tints) <= 224, (name, len(tints))   # (src/gfx.c: DYN_TINT, the colors the game sets after)
    hdr = struct.pack("<4H4f4f4fff6HII", 6, len(order), len(tints), axis, w, h, bz, sat, *st["ambient"], 0.0,
                      *st["hero_light"], lo, hi, len(keep), len(segs), len(cols), gw, gh, len(erecs), 0, 0) + lut
    assert len(hdr) == HDR, len(hdr)
    body = bytearray(hdr)
    body += b"".join(struct.pack("<4B", *t) for t in tints)
    while len(body) % 4:
        body.append(0)
    o_sec = len(body)
    body += table
    # the ground (read in place): colliders' flags (0: the tilemap's), the tilemap's solid tiles (1 bit each, rows from
    # the bottom), segments (1/128 unit), their colliders, cells' first entries, entries
    cb = bytearray(bytes(cols))
    if len(cb) % 2:
        cb.append(0)
    th, tw = solid.shape
    cb += struct.pack("<HH", tw, th)
    rowb = (tw + 7) // 8
    for y in range(th):
        row = bytearray(rowb)
        for x in range(tw):
            if solid[y, x]:
                row[x >> 3] |= 1 << (x & 7)
        cb += row
    if len(cb) % 2:
        cb.append(0)
    for x0, y0, x1, y1, ci in segs:
        cb += struct.pack("<4h", *(i16(c * coll.UNIT) for c in (x0, y0, x1, y1)))
    cb += bytes(ci for x0, y0, x1, y1, ci in segs)
    if len(cb) % 2:
        cb.append(0)
    first = 0
    for c in cells:
        cb += struct.pack("<H", first)
        first += len(c)
    cb += struct.pack("<H", first)
    for c in cells:
        cb += struct.pack("<%dH" % len(c), *c)
    return body, o_sec, blobs, bytes(cb), b"".join(erecs)


def pal_colors(pal):
    """A palette's 15 colors as the game has them: (red 5 bits, green 6, blue 5, alpha 8)"""
    return tuple(((int(r) * 31 + 127) // 255, (int(g) * 63 + 127) // 255, (int(b) * 31 + 127) // 255, int(a))
                 for r, g, b, a in pal)


def pal_steps(cols):
    """What pal_code codes of a palette's colors: per color, the steps (zigzag) from the one before in green, then in
    red and blue less half the green."""
    def zz(v):
        return v << 1 if v >= 0 else (-v << 1) - 1
    out, pg, pr, pb = [], 0, 0, 0
    for r, g, b, a in cols:
        out.append((zz(g - pg), zz(r - (g >> 1) - pr), zz(b - (g >> 1) - pb)))
        pg, pr, pb = g, r - (g >> 1), b - (g >> 1)
    return out


def pal_ks(pals):
    """The Rice codes' parameters that code the steps of all the palettes best (green, red, blue)."""
    steps = [st for c in pals for st in pal_steps(c)]
    return [min(range(9), key=lambda k: sum((st[ch] >> k) + 1 + k for st in steps)) for ch in range(3)]


def pal_code(cols, ks):
    """A palette (src/gfx.c: pal_get): per color, its steps (pal_steps: a Rice code each, ks: green, red, blue), then
    a bit: 1 opaque, 0 its alpha follows (8 bits); bits from the low end of each byte."""
    bits = []
    def put(v, n):
        bits.extend(v >> i & 1 for i in range(n))
    for (r, g, b, a), st in zip(cols, pal_steps(cols)):
        for ch in range(3):
            bits.extend([1] * (st[ch] >> ks[ch]) + [0])
            put(st[ch], ks[ch])
        put(a == 255, 1)
        if a != 255:
            put(a, 8)
    bits += [0] * (-len(bits) % 8)
    return bytes(sum(bits[i + j] << j for j in range(8)) for i in range(0, len(bits), 8))


TEX_SMOOTH, TEX_FULL, TEX_WIDE = 1, 2, 4


def tex_record(w, h, tw, th, pal, off, fmt, flags):
    """A texture's record (src/hk.h: TexRec): its size, tiles across and down, palette, then where its data is (27
    bits), its format (2) and flags (3)."""
    assert off < 1 << 27 and fmt < 4 and flags < 8
    return struct.pack("<HHBBHI", w, h, tw, th, pal, off | fmt << 27 | flags << 29)


def tile_map(t):
    """A texture's tile map: per row, the rank of its first tile (a byte, 2 if TEX_WIDE), then 2 bits a tile (0 empty, 1
    tile, 3 opaque tile); none at all (TEX_FULL) if every tile is kept and none is opaque. -> (bytes, flags)"""
    codes = [[0 if t.empty[ty, tx] else (3 if t.opaque[ty, tx] else 1) for tx in range(t.tw)] for ty in range(t.th)]
    if all(c == 1 for row in codes for c in row):
        return b"", TEX_FULL
    wide = t.ntiles > 255
    assert t.ntiles < 65536, ("too many tiles", t.w, t.h, getattr(t, "name", None), getattr(t, "key", None))
    m = bytearray()
    rank = 0
    for row in codes:
        m += struct.pack("<H" if wide else "<B", rank)
        b = bytearray((t.tw + 3) // 4)
        for tx, code in enumerate(row):
            b[tx >> 2] |= code << ((tx & 3) * 2)
            rank += code != 0
        m += b
    return bytes(m), TEX_WIDE if wide else 0


def tile_blocks(t):
    """A texture's tiles in blocks: (bits, tiles, per tile the one to its left and the one above in the block (255:
    none), as src/tex.c finds them)"""
    pos = [(tx, ty) for ty in range(t.th) for tx in range(t.tw) if not t.empty[ty, tx]]
    assert len(pos) == t.ntiles
    out = []
    for i in range(0, t.ntiles, art.BLOCK_TILES):
        at = {p: j for j, p in enumerate(pos[i:i + art.BLOCK_TILES])}
        nb = b"".join(bytes([at.get((tx - 1, ty), 255), at.get((tx, ty - 1), 255)]) for tx, ty in pos[i:i + art.BLOCK_TILES])
        out.append((t.bits, t.tiles[i:i + art.BLOCK_TILES], nb))
    return out


def tilecode(blocks):
    """Trains the tile coder's starting probabilities on every block and encodes them (tools/tilecode.c)."""
    import subprocess, tempfile
    tmp = tempfile.mkdtemp()
    exe = os.path.join(tmp, "tilecode")
    subprocess.check_call(["cc", "-O2", "-o", exe, os.path.join(HERE, "tilecode.c"), "-lm"])
    job = bytearray(struct.pack("<I", len(blocks)))
    for bits, tiles, nb in blocks:
        job += struct.pack("<BBH", bits, 0, len(tiles)) + nb + b"".join(tiles)
    open(os.path.join(tmp, "in.bin"), "wb").write(job)
    subprocess.check_call([exe, "train", os.path.join(tmp, "in.bin"), os.path.join(tmp, "prior.bin")])
    subprocess.check_call([exe, "encode", os.path.join(tmp, "in.bin"), os.path.join(tmp, "prior.bin"), os.path.join(tmp, "out.bin")])
    prior = open(os.path.join(tmp, "prior.bin"), "rb").read()
    out = open(os.path.join(tmp, "out.bin"), "rb").read()
    res, p = [], 0
    for _ in blocks:
        n = struct.unpack_from("<I", out, p)[0]
        res.append(out[p + 4:p + 4 + n])
        p += 4 + n
    return prior, res


def main():
    t0 = time.time()
    fp = os.path.join(HERE, "fit.json")
    if os.path.exists(fp):
        import json
        art.FIT.update(json.load(open(fp)))
    actors.add_props(ALL_ROOMS)
    vm.prepare(ROOMS, SPRITES, TEXTS)   # (the characters' scripts, and their libraries as actors)
    # (the scripts' objects: drawn by them, not with the room)
    variants, per_room = art.build_variants(ROOMS, visible.compute, vm.owned)
    print("variants", len(variants), "%.0fs" % (time.time() - t0), flush=True)
    jobs = [art.Job(v) for v in variants]
    sprites, clips = actors.build()
    ents.CLIP_INDEX.update({c["id"]: i for i, c in enumerate(clips)})
    # the rooms' objects, and the sprites they show (after the actors')
    SPRITES.base = len(sprites)
    named = actors.named_sprites(SPRITES)
    prompt_ids = {p: TEXTS.id("Prompts", p.upper(), "PROMPT") for p in PROMPTS}
    TEXTS.id("Elderbug", "ELDERBUG_INTRO_MAIN")   # (until the NPCs: a conversation to try)
    # the titles (Area Title): each one's main line as the large and the small title show it, its lines below and above
    titles = []
    for t in text.TITLES:
        row = []
        for key, style in (("_MAIN", "TITLE_L"), ("_MAIN", "TITLE_S"), ("_SUB", "TITLE_SUB"), ("_SUPER", "TITLE_SUB")):
            line = text.clean(text.sheets()["Titles"].get(t + key) or "")
            row.append(TEXTS.add(line, style) if line else -1)
        titles.append("{%s}" % ", ".join(str(i) for i in row))
    fleurs = actors.title_fleurs(SPRITES)
    fleurs["VM_PIECE_BASE"] = SPRITES.base   # (the sprites the scripts' debris named, before it was known: theirs from it)
    # the message as an item is taken (UI Msg Get Item, on the HUD): each item's texts, the message's sprites, the
    # items' icons
    msgs = []
    for item, (name, prefix, press, m1, m2, key) in text.MSGS.items():
        msgs.append("{%d, %d, %d, %d, %d, %d}" % (TEXTS.id("UI", name, "MSG_NAME"), TEXTS.id("Prompts", prefix, "MSG"),
                                                  TEXTS.id("Prompts", press, "MSG"), TEXTS.id("Prompts", m1, "MSG"),
                                                  TEXTS.id("Prompts", m2, "MSG"), TEXTS.add(key, "PROMPT")))
    pmsg = unity.prefab("sharedassets6.assets", 493)
    hud_res = actors.HUD_K / actors.K0
    for name, path, res, ref in (("BG", "BG", 0.003, None), ("STOP", "Stop", hud_res, None),
                                 ("FLEUR", "Fleur", hud_res, [4, 2086]), ("KEY", "Button", hud_res, None)):
        # (the fleur: its animation's last frame)
        o = next(q for q in pmsg["objects"] if q["path"] == "UI Msg Get Item/" + path)
        sr = next(c["v"] for c in o["c"] if c["type"] == "SpriteRenderer")
        fleurs["SPRITE_MSG_" + name] = SPRITES.id(pmsg, ref or sr["m_Sprite"], max(o["lscale"][:2]), res)
    fleurs["SPRITE_MSG_ICON_FIREBALL"] = SPRITES.id(unity.scene("Crossroads_ShamanTemple"), [4, 44], 1.5607, hud_res)
    fleurs["SPRITE_MSG_ICON_DASH"] = SPRITES.id(unity.scene("Fungus1_04"), [10, 22], 1.5607, hud_res)
    # (a Moss Knight's grass ball: a Unity sprite)
    pgb = unity.prefab("sharedassets149.assets", 34)
    fleurs["SPRITE_GRASS_BALL"] = SPRITES.id(pgb, next(c["v"] for c in pgb["objects"][0]["c"] if c["type"] == "SpriteRenderer")["m_Sprite"], 1)
    # (a soul orb: Soul Orb R's sprite, as stretched as it gets; its sorting)
    pso = unity.prefab(*vm.SOUL_ORB)
    fleurs["SPRITE_SOUL_ORB"] = SPRITES.id(pso, next(c["v"] for c in pso["objects"][0]["c"] if c["type"] == "SpriteRenderer")["m_Sprite"], 2)
    fleurs["SOUL_ORB_SORT"] = "{%d, %d}" % ents._sorting(pso["objects"][0])
    fleurs["MSG_TABLE"] = "{%s}" % ", ".join(msgs)
    # the notices (Relic Get Msg, Charm Get Msg) and the charm tutorial (Charm Tutorial Msg): their pieces, the icons the
    # scripts set, the charms' icons (CharmIconList) and names
    def piece(doc, o, scale=None):
        """a prefab object's sprite at its scale: (sprite, its x scale / y scale as drawn)"""
        sr = next(c["v"] for c in o["c"] if c["type"] == "SpriteRenderer")
        sx, sy = (scale or o["lscale"])[:2]
        return SPRITES.id(doc, sr["m_Sprite"], max(sx, sy), hud_res), sx / max(sx, sy), sy / max(sx, sy)
    prel, ptute = unity.prefab("resources.assets", 4251), unity.prefab("sharedassets6.assets", 491)
    by_rel = {o["path"]: o for o in prel["objects"]}
    by_tute = {o["path"]: o for o in ptute["objects"]}
    s, kx, ky = piece(prel, by_rel["Relic Get Msg/pop_up_backboard"])
    fleurs["NOTICE_BACKBOARD"] = "{%d, %.4ff, %.4ff}" % (s, kx, ky)
    for name, path in (("FLEUR", "Warning_Fleur0008"), ("BACKBOARD", "backboard"), ("IMAGE", "Image"), ("STOP", "Stop")):
        s, kx, ky = piece(ptute, by_tute["Charm Tutorial Msg/" + path])
        fleurs["TUTE_" + name] = "{%d, %.4ff, %.4ff}" % (s, kx, ky)
    # the focus tutorial King's Pass's first tablet shows (Focus_prompt_temp, the HUD's where it is): its picture, its
    # lines (Prompts: in the message's style) by their rects' tops, centred or ending at x, the key to hold (Cast's: its
    # box's left); a line no nearer the one above than its height (the text bigger)
    fdoc = unity.scene("Tutorial_01")
    fby = {o["path"]: o for o in fdoc["objects"]}
    froot = "_Props/Tut_tablet_top/Focus_prompt_temp"
    fo = fby[froot]
    s, kx, ky = piece(fdoc, fo, scale=[math.hypot(fo["m3"][0], fo["m3"][3]), math.hypot(fo["m3"][1], fo["m3"][4])])
    fleurs["FOCUS_PROMPT_BG"] = "{%d, %.4ff, %.4ff, %.4ff, %.4ff}" % (s, fo["pos"][0], fo["pos"][1], kx, ky)
    rows = []
    for n in ("Text1", "Text1B", "Text2", "Text3"):
        q = fby[froot + "/" + n]
        tc = next(c["v"] for c in q["c"] if c["type"] == "TextContainer" or c.get("class") == "TextContainer")
        tm = next(c["v"] for c in q["c"] if c.get("class") == "TextMeshPro")
        gt = next(c["v"] for c in q["c"] if c.get("class") == "SetTextMeshProGameText")
        k = math.hypot(q["m3"][0], q["m3"][3])
        w, h, mg = tc["m_rect"]["width"], tc["m_rect"]["height"], tc["m_margins"]
        x0 = q["pos"][0] + (-tc["m_pivot"]["x"] * w + mg["x"]) * k
        x1 = q["pos"][0] + ((1 - tc["m_pivot"]["x"]) * w - mg["z"]) * k
        top = q["pos"][1] + ((1 - tc["m_pivot"]["y"]) * h - mg["y"]) * k
        al = tm["m_textAlignment"] % 4
        assert al in (1, 2), n
        if rows:
            top = min(top, prev - text.STYLES["MSG"][2] / text.HUD_PX)
        prev = top
        rows.append("{%d, %.4ff, %.4ff, %d}" % (TEXTS.id(gt["sheetName"], gt["convName"], "MSG"),
                                                (x0 + x1) / 2 if al == 1 else x1, top, al - 1))
    fleurs["FOCUS_PROMPT_TEXTS"] = "{%s}" % ", ".join(rows)
    q = fby[froot + "/ActionButtonIcon"]
    sr = next(c["v"] for c in q["c"] if c["type"] == "SpriteRenderer")
    ks = unity.sprite(fdoc["level"], fdoc["externals"], *sr["m_Sprite"])
    fleurs["FOCUS_PROMPT_KEY"] = "{%d, %.4ff, %.4ff}" % (TEXTS.add("alpha", "PROMPT"), q["pos"][0] - ks.w / ks.ppu / 2 *
                                                        math.hypot(q["m3"][0], q["m3"][3]), q["pos"][1])
    fleurs["NOTICE_ICONS"] = "{%s}" % ", ".join(str(SPRITES.id(dd, list(rr), 1.0, hud_res)) for dd, rr in vm.NOTICE_ICONS) \
        if vm.NOTICE_ICONS else "{-1}"
    pmenu = unity.scene("Menu_Title")
    icl = next(c["v"] for o in pmenu["objects"] for c in o["c"] if c.get("class") == "CharmIconList")["spriteList"]
    # (the charms' icons as big as the inventory shows them: its details', CHARM_ICON_K; smaller elsewhere)
    icon_k = 1.5
    charm_icon = lambda i: SPRITES.id(pmenu, icl[i], icon_k, hud_res)
    fleurs["CHARM_ICON_K"] = "%.4ff" % icon_k
    fleurs["CHARM_ICONS"] = "{%s}" % ", ".join(str(charm_icon(i)) if i in vm.CHARMS else "-1" for i in range(41))
    fleurs["CHARM_NAMES"] = "{%s}" % ", ".join(
        str(TEXTS.add(text.clean(text.sheets()["UI"]["CHARM_NAME_%d" % i]), "NOTICE")) if i in vm.CHARMS else "-1"
        for i in range(41))
    for key, style in (("CHARM_TUTE_TITLE", "TUTE_TITLE"), ("CHARM_TUTE_SUB", "TUTE"), ("CHARM_REMINDER", "TUTE")):
        fleurs["TXT_" + key] = TEXTS.add(text.clean(text.sheets()["Prompts"][key]), style)
    # the yes or no box: Yes, No (Prompts, TextMeshPro 10), Not enough Geo (5)
    for name, key, style in (("YES", "YES", "YN"), ("NO", "NO", "YN"), ("NOT_ENOUGH", "NOT_ENOUGH_GEO", "MSG")):
        fleurs["TXT_YN_" + name] = TEXTS.id("Prompts", key, style)
    # the title screen, the save profiles, the pause menu; each room's map zone (the save slots' area)
    import menu, inv, shop, collect, stag
    fleurs.update(menu.build(SPRITES, TEXTS))
    fleurs.update(inv.build(SPRITES, TEXTS, hud_res, vm.CHARMS, charm_icon))
    fleurs.update(shop.build(SPRITES, TEXTS, hud_res, actors.clip_id, vm.CHARMS))
    fleurs.update(collect.build())
    fleurs.update(stag.build(SPRITES, TEXTS, hud_res, actors.clip_id, ents.pd_flags(), vm.STR.id, ROOMS))
    import gamemap
    fleurs.update(gamemap.build(SPRITES, TEXTS, hud_res, list(ROOMS) + list(vm.MAP_SCENES), ents.pd_flags(),
                                lambda r: scene.settings(unity.scene(vm.MAP_SCENES.get(r, r))).get("map_zone") or 0))
    fleurs["ROOM_ZONES"] = "{%s}" % ", ".join(str(scene.settings(unity.scene(r)).get("map_zone") or 0) for r in ROOMS)
    for i, item in enumerate(text.MSGS):
        fleurs["MSG_" + item] = i
    for r in ROOMS:
        room_data(r)
        if r in VARIANT_ROOMS:
            room_data(r, 1)
    clip_index = {c["id"]: i for i, c in enumerate(clips)}
    vm_rooms = [vm.room_blob(r, clip_index, SPRITES, ROOM_OWNERS.get((r, 0), []), PERSIST) for r in ROOMS]
    # (the save's semi persistent bits: cleared as the Knight rests at a bench or dies)
    fleurs["SEMI_PERSIST"] = "{%s}" % ", ".join(str(b) for b in PERSIST.semi) if PERSIST.semi else "{0xFFFF}"
    sheet = text.sheets()["Prompts"]
    vm_prompts = {s: TEXTS.id("Prompts", s.upper(), "PROMPT") for s in vm.STR.list if s.upper() in sheet}
    for s in ("NPC Title", "Visited", "Display Right", "Hornet Saver", "Item"):
        vm.STR.id(s)
    sprites += actors.unity_sprites(SPRITES.list)
    # the HUD's geo count: TrajanPro-Bold digits (TextMesh: size 45, character size 1, scale 0.1527)
    digit_base = len(sprites)
    digits, digit_adv = font.glyphs("TrajanPro-Bold", "0123456789", 45 * 0.1 * 0.1527 * actors.HUD_K, 1 / actors.HUD_K)
    sprites += digits
    print("actor frames", len(sprites), "clips", len(clips), "%.0fs" % (time.time() - t0), flush=True)
    jobs += [sp["job"] for sp in sprites]
    with mp.get_context("spawn").Pool(min(4, os.cpu_count() or 1)) as pool:
        texs = pool.map(art.encode_job, jobs, chunksize=4)
    # (the actors' textures come after the scenery's; a smooth one kept smaller: units a texel of what is kept)
    for sp, t in zip(sprites, texs[len(texs) - len(sprites):]):
        h, w = sp["job"].arr.shape[:2]
        if (t.w, t.h) != (w, h):
            sp["tu"], sp["tv"] = sp["tu"] * w / t.w, sp["tv"] * h / t.h
    variants = variants + [sp["job"] for sp in sprites]
    print("textures %.0fs" % (time.time() - t0), flush=True)
    blocks = []
    for t in texs:
        t.first_block = len(blocks)
        if t.ntiles:
            blocks += tile_blocks(t)
    prior, coded = tilecode(blocks)
    print("tiles coded %.0fs" % (time.time() - t0), flush=True)
    secs = {k: Blob() for k in SECTIONS}
    # the starting probabilities as the game uses them (tilecode.h)
    secs["PRIOR"].b += prior
    tex_id = {}
    # the palettes (shared: once each), coded: where each one is, 2 bytes a unit
    pal_at = {pal_colors(t.pal): 0 for t in texs if t.pal is not None}
    ks = pal_ks(pal_at)
    secs["PAL"].b += bytes(ks) + bytes(1)
    for c in pal_at:
        secs["PAL"].align(2)
        pal_at[c] = len(secs["PAL"].b) // 2
        secs["PAL"].b += pal_code(c, ks)
    assert len(secs["PAL"].b) < 2 << 16
    texrec = bytearray()
    for i, (v, t) in enumerate(zip(variants, texs)):
        tex_id[id(v)] = (i, t.w, t.h)
        pal = pal_at[pal_colors(t.pal)] if t.pal is not None else 0
        if t.fmt == art.FMT_SOFTA:   # (its color, then its alpha: read in place)
            off = secs["SOFT"].add(struct.pack("<I", t.base) + t.soft)
            texrec += tex_record(t.w, t.h, 0, 0, pal, off, t.fmt, 0)
            continue
        if t.fmt == art.FMT_SOFT:
            c = lz(t.soft)
            off = secs["SOFT"].add(struct.pack("<I", len(c)) + c)
            texrec += tex_record(t.w, t.h, 0, 0, pal, off, t.fmt, 0)
            continue
        bl = coded[t.first_block:t.first_block + (t.ntiles + art.BLOCK_TILES - 1) // art.BLOCK_TILES]
        m, flags = tile_map(t)
        if v.blur:
            flags |= TEX_SMOOTH
        # its data: the tile map, where its blocks after the first start (from its data), its blocks
        d = bytearray(m)
        at = len(d) + 2 * (len(bl) - 1)
        for b in bl[:-1]:
            at += len(b)
            d += struct.pack("<H", at)
        for b in bl:
            d += b
        assert at < 65536, (i, at)
        texrec += tex_record(t.w, t.h, t.tw, t.th, pal, secs["TDAT"].add(bytes(d), 1), t.fmt, flags)
    secs["TDAT"].b += bytes(3)   # (the game reads up to 3 bytes past a block)
    if os.environ.get("ACTSTATS"):
        import collections
        tot = collections.Counter()
        for j, sp in enumerate(sprites):
            t = texs[len(variants) - len(sprites) + j]
            nb = sum(len(b) for b in coded[t.first_block:t.first_block + (t.ntiles + art.BLOCK_TILES - 1) // art.BLOCK_TILES]) if t.fmt not in (art.FMT_SOFTA, art.FMT_SOFT) else len(t.soft or b"")
            nb += (len(t.pal.tobytes()) if t.pal is not None else 0)
            tot[sp["job"].name if os.environ.get("ACTSTATS") == "clip" else sp["job"].name.split("/")[0]] += nb
        for k, v in tot.most_common(80):
            print("ACT", v, k)
    secs["TEX"].b += struct.pack("<I", len(variants)) + texrec
    # actors: each sprite frame's texture and where its top-left corner is (local units), units a texel; the clips
    first_actor = len(variants) - len(sprites)
    # (their colliders, as frames set them: 0 none given, 1 none, 2 + an entry: a box or a shape)
    col_ids, col_entries = {}, []
    for i, sp in enumerate(sprites):
        c, cid = sp.get("col"), 0
        if c and c[0] == "none":
            cid = 1
        elif c:
            b = struct.pack("<BBH4f", 2, 0, 0, *c[1:]) if c[0] == "box" else \
                struct.pack("<BBH", 3, len(c[1]), 0) + b"".join(struct.pack("<2f", x, y) for x, y in c[1])
            if b not in col_ids:
                col_ids[b] = len(col_entries)
                col_entries.append(b)
            cid = 2 + col_ids[b]
        # (src/hk.h: SpriteRec; its corner in 1/64 units, its texel's size a half float)
        lx, ty = round(sp["lx"] * 64), round(sp["ty"] * 64)
        assert -32768 <= lx < 32768 and -32768 <= ty < 32768, sp["job"].name
        f16 = lambda v: int(np.array(v, np.float16).view(np.uint16))
        secs["SPR"].b += struct.pack("<HHhhHH", first_actor + i, cid, lx, ty, f16(sp["tu"]), f16(sp["tv"]))
    at = 4 + 4 * len(col_entries)
    secs["SCOL"].b += struct.pack("<I", len(col_entries))
    for b in col_entries:
        secs["SCOL"].b += struct.pack("<I", at)
        at += len(b)
    for b in col_entries:
        secs["SCOL"].b += b
    secs["CLIP"].b += struct.pack("<I", len(clips))
    frames = []
    for c in clips:
        secs["CLIP"].b += struct.pack("<HHfBBH", len(frames), len(c["frames"]), c["fps"], c["wrap"], c["loop"], 0)
        frames += [sid | (0x8000 if trig else 0) for sid, trig in c["frames"]]
    secs["CLIP"].b += struct.pack("<%dH" % len(frames), *frames)
    with open(os.path.join(unity.CACHE, "texnames.txt"), "w") as f:
        for i, (v, t) in enumerate(zip(variants, texs)):
            name = v.name if isinstance(v, art.ImageJob) else v.sprite.name
            f.write("%d %s %.3f %d %d %dx%d %d\n" % (i, name.replace(" ", "_"), getattr(v, "scale", 1.0), v.blur, v.alpha_only,
                                                      t.w, t.h, t.ntiles))
    rooms = bytearray(struct.pack("<I", len(ROOMS) + len(VARIANT_ROOMS)))
    sector_offs, rvar = {}, bytearray(struct.pack("<I", len(VARIANT_ROOMS)))
    pdf = _pdf_flags()
    made = {}
    for r in ROOMS + [r + "@1" for r in VARIANT_ROOMS]:
        base, variant = (r[:-2], 1) if r.endswith("@1") else (r, 0)
        keep, st = per_room[base]
        made[r] = room_blobs(base, keep, st, tex_id, variant)
    if os.environ.get("RECSTATS"):
        import collections
        rs = struct.calcsize(ents.REC)
        nt, fl = collections.Counter(), collections.Counter()
        for r in ROOMS:
            recs = made[r][4]
            for i in range(0, len(recs), rs):
                nt[recs[i]] += 1
                if recs[i] == ents.ENT_OBJ:
                    fl[recs[i + 1]] += 1
        print("RECS", rs, sorted(nt.items()), "objs", sorted(fl.items()))
        for r in ROOMS:
            recs = made[r][4]
            ne = sum(1 for i in range(0, len(recs), rs) if recs[i] == ents.ENT_OBJ and recs[i + 1] == ents.OK_ENEMY)
            no = sum(1 for i in range(0, len(recs), rs) if recs[i] == ents.ENT_OBJ)
            print("ROOMRECS", r, "enemies", ne, "objs", no)
    # the instances' heads: the most common of all the rooms' (a variant's sectors are its room's)
    table, heads = head_table([s for r in ROOMS for s, n in made[r][2]])
    secs["HEADS"].b += table
    for r in ROOMS + [r + "@1" for r in VARIANT_ROOMS]:
        base, variant = (r[:-2], 1) if r.endswith("@1") else (r, 0)
        keep, st = per_room[base]
        body, o_sec, blobs, ground, recs = made[r]
        struct.pack_into("<I", body, 76, secs["RBLOB"].add(ground))
        struct.pack_into("<I", body, 80, secs["RBLOB"].add(recs))
        packed = 0
        for i, (insts, n) in enumerate(blobs):
            c = sector_stream(insts, heads)
            # (a room's variant: the same sectors)
            off = sector_offs[(base, i)] if variant else secs["RBLOB"].add(c)
            sector_offs[(base, i)] = off
            struct.pack_into("<II", body, o_sec + 20 * i, off, len(c))
            packed += len(c)
        if variant:
            # its variants: which entry, by which PlayerData bool, and each one's groups to hide in the other
            (flag, val), spans = next(((a[1], a[2]) for a in scene.additive(base)), ("", False)), room_data(base)[2]
            s1, s2 = spans.get(1, (0, 0)), spans.get(2, (0, 0))
            rvar += struct.pack("<HHHBB4H", ROOMS.index(base), len(ROOMS) + VARIANT_ROOMS.index(base), pdf[flag], val, 0,
                                *s1, *s2)
        hc = lz(bytes(body))
        off = secs["RBLOB"].add(hc)
        assert len(r) < 32, r
        rooms += struct.pack("<32sIII", base.encode(), off, len(hc), len(body))
        print("room %-26s %5d instances in %2d sectors, %6d packed, ground %d, recs %d" % (r, len(keep), len(blobs), packed + len(hc), len(ground), len(recs)), flush=True)
    secs["ROOMS"].b += rooms
    secs["RVAR"].b += rvar
    # (the children enemy.c knows by name)
    str_ids = {n: STRINGS.id(n) for n in ("Attack Range", "Evade Range", "Spit Range", "Wake Box", "Slash Hitbox",
                                          "Slash2 Hitbox", "door_stagExit")}
    secs["STR"].b += STRINGS.blob()
    secs["TEXT"].b += TEXTS.blob()
    secs["FONT"].b += TEXTS.fonts()
    secs["PHASH"].b += PERSIST.blob()
    # the scripts: each room's offset (0: none), then the rooms' (4-byte aligned)
    vmb = bytearray(4 * len(ROOMS))
    for i, b in enumerate(vm_rooms):
        if b:
            while len(vmb) & 3:
                vmb.append(0)
            struct.pack_into("<I", vmb, 4 * i, len(vmb))
            vmb += b
    secs["VM"].b += bytes(vmb)
    secs["VMDEF"].b += vm.tables(clip_index, vm_prompts)
    # the file: "HKNW", count, then (offset, size) per section
    head = struct.pack("<4sI", b"HKNW", len(SECTIONS))
    pos = len(head) + 8 * len(SECTIONS)
    table = bytearray()
    body = bytearray()
    for k in SECTIONS:
        while (pos + len(body)) % 4:
            body.append(0)
        table += struct.pack("<II", pos + len(body), len(secs[k].b))
        body += secs[k].b
    data = head + table + body
    open(os.path.join(SRC, "data.bin"), "wb").write(data)
    sizes = {k: len(secs[k].b) for k in SECTIONS}
    print("data.bin", len(data), sizes, "%.0fs" % (time.time() - t0))
    with open(os.path.join(SRC, "data.h"), "w") as f:
        f.write("/* made by tools/pack.py */\n#pragma once\n")
        for i, k in enumerate(SECTIONS):
            f.write("#define SEC_%s %d\n" % (k, i))
        f.write("#define NUM_ROOMS %d\n" % len(ROOMS))
        for i, r in enumerate(ROOMS):
            f.write("#define ROOM_%s %d\n" % (r.upper(), i))
        for i, c in enumerate(clips):
            f.write("#define %s %d\n" % (c["id"], i))
        for k, v in named.items():
            f.write("#define SPRITE_%s %d\n" % (k, v))
        f.write("#define SPRITE_DIGIT0 %d\n" % digit_base)
        for i, k in enumerate(text.STYLES):
            f.write("#define STYLE_%s %d\n" % (k, i))
        f.write("#define TEXT_K %.6ff\n#define FONT_PHASES %d\n" % (text.TEXT_K, text.PHASES))
        for i, k in enumerate(actors.ENEMY_KINDS):
            f.write("#define EK_%s %d\n" % (k[0].upper(), i + 1))
        f.write("#define NUM_KINDS %d\n" % (len(actors.ENEMY_KINDS) + 1))
        f.write("#define KIND_TABLE %s\n" % actors.kind_table({c["id"]: i for i, c in enumerate(clips)}))
        for k, v in prompt_ids.items():
            f.write("#define TXT_PROMPT_%s %d\n" % (k.upper(), v))
        f.write("#define PROMPT_SORT %du\n" % ((scene.layer_index(-349214895) << 16) | (1 + 32768)))
        f.write("#define DIGIT_ADV {%s}\n" % ", ".join("%.4ff" % a for a in digit_adv))
        for name, (code, _) in vm.OPS.items():
            f.write("#define VMOP_%s %d\n" % (name.upper(), code))
        for i, e in enumerate(vm.EVENTS.list[:vm.FIXED_EVENTS]):
            f.write("#define VMEV_%s %d\n" % (e.upper().replace(" ", "_"), i))
        for e in vm.NAMED_EVENTS:   # (each room's number for it; 255: its scripts don't know it)
            ids = [vm.BUILT[r].named_events.get(e, 255) if r in vm.BUILT else 255 for r in ROOMS]
            f.write("#define VMEV_%s {%s}\n" % (e.upper().replace(" ", "_"), ", ".join(str(i) for i in ids)))
        for name in ("NPC Title", "Visited", "Display Right", "Hornet Saver", "Item"):
            f.write("#define VMSTR_%s %d\n" % (name.upper().replace(" ", "_"), vm.STR.index[name]))
        for name, i in str_ids.items():
            f.write("#define STR_%s %d\n" % (name.upper().replace(" ", "_"), i))
        for i, t in enumerate(text.TITLES):
            f.write("#define TITLE_%s %d\n" % (t, i))
        f.write("#define NUM_TITLES %d\n#define TITLE_TABLE {%s}\n" % (len(titles), ", ".join(titles)))
        for k, v in fleurs.items():
            f.write("#define %s %s\n" % (k, v))


if __name__ == "__main__":
    main()
