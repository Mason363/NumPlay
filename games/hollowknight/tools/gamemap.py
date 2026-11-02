"""The map (Game_Map: the areas' rooms as Cornifer drew them, rough until the quill maps them; their pins, the arrows to
the areas next to them, the areas' names; the compass, the shade's mark) and the quick map's HUD (Quick Map): MAP_*
defines for data.h (src/map.c). Places are in the map's units from its root; the HUD shows them at its scale."""
import math
import unity, text

GAME_MAP = ("resources.assets", 4808)
HUD = ("resources.assets", 5473)
QUICK = "_GameCameras/HudCamera/Quick Map/"
# the areas of this part of the game: (Game_Map's child, the PlayerData bool that has its map (None: always), the quick
# map's place for it (Quick Map: SetPosition), its name (Map Zones), the inventory's wide map's piece of it, the place the
# map zooms to for it (UI Control: Zoom To Pos), the wide map's compass there (Compass Icon's Position), the pan's bounds
# it widens (WorldMap: min x, max x, min y, max y; None: not that one)). Greenpath's map is never had here: its rooms
# only (where the compass is, in the dark)
AREAS = (("Town_Tutorial", None, (4.07, -11.62), "TOWN", "Town", (4, -8.642), (2.86, -4.49), None),
         ("Crossroads", "mapCrossroads", (-0.04, -7.88), "CROSSROADS", "Crossroads", (-0.04, -7.58), (2.51, -5.65), None),
         ("Green_Path", "mapGreenpath", (16.31, -7.87), "GREEN_PATH", "Greenpath", (16.42, -7.32), (-0.12, -6),
          (None, 17.26, None, -6.22)))
QUICK_K = 1.55   # (the map's scale: the quick map's (Check Area))
# the inventory's map (Inventory/Map/World Map: UI Control): the Wide Map's place (Pos 1: the top areas' maps only),
# the map zoomed into from there (Map Zoom: the wide map's place and these, at 0.436; to 1.3), the pan's bounds
# (WorldMap's, before the areas widen them), its speed (GameMap's panSpeed)
INV = "_GameCameras/HudCamera/Inventory/"
WM = INV + "Map/World Map/"
WIDE_POS = (0.52, -2.58)
ZOOM_FROM, ZOOM_K0, ZOOM_K = (3.81, -7.77), 0.436, 1.3
PAN = (-1.44, 4.55, -8.642, -5.58)
# the map's key (Map Key: Draw Pins): its rows in order, by the pins had (in this part of the game)
KEYS = (("Bench", "pin_bench", "hasPinBench"), ("Cocoon", "Pin Icon", "hasPinCocoon"), ("Vendor", "Pin Icon", "hasPinShop"),
        ("Spa", "Pin Icon", "hasPinSpa"), ("Key Stag", "Pin Icon", "hasPinStag"))
MAP_MSG = ("sharedassets7.assets", 539)   # (Map Update Msg: what the bench makes as the map is updated)
MAP_MSG_AT = (-1.29, -6.83)              # (its Map Msg's Position)
COCOON = "hasPinCocoon"         # (pin_blue_health: SetupMap's, by the rooms a cocoon was broken in)


def build(sprites, texts, res, rooms, pd_flags, room_zone):
    """rooms: the rooms by their bits in the save (vm.room_bits); pd_flags: PlayerData bools -> flags; room_zone(room) ->
    its map zone (SceneManager's mapZone)"""
    d = unity.prefab(*GAME_MAP)
    by = {o["path"]: o for o in d["objects"]}
    byid = {o["id"]: o for o in d["objects"]}
    kids = {}
    for o in d["objects"]:
        kids.setdefault(o.get("parent"), []).append(o)
    U = lambda v: "%.4ff" % v
    sh = text.sheets()
    out = {}

    def comp(o, cls):
        return next((c["v"] for c in o["c"] if (c.get("class") or c["type"]) == cls and c.get("v") is not None), None)

    def fsm(o):
        return next((c["fsm"] for c in o["c"] if c.get("fsm")), None)

    def flag(name):
        """a PlayerData bool's flag; -2 one this part of the game never sets (so: never)"""
        return pd_flags.get(name, -2) if name else -1

    def scale(o):
        """its scale in the map's units"""
        k, q = 1.0, o
        while q is not None and q["id"] != root["id"]:
            k *= q["lscale"][0]
            q = byid.get(q.get("parent"))
        return k

    def at(o):
        """its place in the map's units from the map's root"""
        x = y = 0.0
        p = o
        while p is not None and p["id"] != root["id"]:
            k = scale(byid[p["parent"]]) if byid.get(p.get("parent")) and p["parent"] != root["id"] else 1.0
            x += p["lpos"][0] * k
            y += p["lpos"][1] * k
            p = byid.get(p.get("parent"))
        return x, y

    def spr(ref, k, doc=d):
        return sprites.id(doc, ref, k, res)

    def tx(o, style, doc_k=1.0):
        """a text's place (its rect's left, middle or right, as it is aligned), its rect's top, its color, its
        alignment (0 left, 1 centred, 2 right)"""
        tc, tm, gt = comp(o, "TextContainer"), comp(o, "TextMeshPro"), comp(o, "SetTextMeshProGameText")
        k = scale(o) * doc_k
        x, y = at(o)
        w, h = tc["m_rect"]["width"] * k, tc["m_rect"]["height"] * k
        al = tm["m_textAlignment"] % 4
        assert al < 3, o["path"]
        col = tm.get("m_fontColor") or {"r": 1, "g": 1, "b": 1}
        rgb = (int(round(col["r"] * 255)) << 16) | (int(round(col["g"] * 255)) << 8) | int(round(col["b"] * 255))
        t = texts.add(text.clean(sh[gt["sheetName"]][gt["convName"]]), style)
        return t, x + (al / 2 - tc["m_pivot"]["x"]) * w, y + (1 - tc["m_pivot"]["y"]) * h, rgb, al

    root = by["Game_Map"]
    rows, pins, nexts, names, areas = [], [], [], [], []
    for ai, (an, have, qpos, title, _, _, _, _) in enumerate(AREAS):
        a = by["Game_Map/" + an]
        first = len(rows)
        zones = 0
        never = have is not None and flag(have) == -2
        for r in kids.get(a["id"], []):
            sr = comp(r, "SpriteRenderer")
            cls = {(c.get("class") or c["type"]) for c in r["c"]}
            if never and r["name"] not in rooms:
                continue   # (a map never had: its rooms here only, for the compass)
            if "TextMeshPro" in cls:
                if never:
                    continue
                # (the area's name: the world map's only; a sub area's: both)
                t, mx, top, rgb, al = tx(r, "MSG")
                world_only = "DisplayOnWorldMapOnly" in cls or fsm(r) is not None
                names.append("{%d, -1, %d, %s, %s, 0x%06X, %d, %d}" % (ai, t, U(mx), U(top), rgb, al, world_only))
                continue
            if r["name"] == "Grub Pins":
                continue   # (the Collector's Map's: not here)
            rm = comp(r, "RoughMapRoom")
            ri = rooms.index(r["name"]) if r["name"] in rooms else 255
            rough = bool(r["self_active"] and rm) and not never
            if ri == 255 and not rough:
                continue   # (never mapped here, never shown)
            if ri != 255:
                zones |= 1 << room_zone(r["name"])
            k = scale(r)
            x, y = at(r)
            if sr is not None and sr.get("m_Sprite"):
                col = sr["m_Color"]
                rgb = sum(int(round(col[c] * 255)) << (16 - 8 * i) for i, c in enumerate("rgb"))
                s = unity.sprite(d["level"], d["externals"], *sr["m_Sprite"])
                sid = spr(sr["m_Sprite"], QUICK_K * k) if not never else -1
                full = spr(rm["fullSprite"], QUICK_K * k) if rm and not never else -1
                w, h = s.w / s.ppu * k, s.h / s.ppu * r["lscale"][1] * k / r["lscale"][0]
            else:
                sid, full, w, h, rgb = -1, -1, 0, 0, 0   # (Crossroads_ShamanTemple: its pins only)
            rows.append("{%d, %d, %d, %d, %s, %s, %s, %s, 0x%06X, %d}" % (ai, ri, sid, full, U(x), U(y), U(w), U(h), rgb,
                                                                         rough))
            mi = len(rows) - 1
            for q in ([] if never else kids.get(r["id"], [])):
                qc = {(c.get("class") or c["type"]) for c in q["c"]}
                if q["name"].startswith("pin_") and "SpriteRenderer" in qc:
                    f = fsm(q)
                    if f is not None:
                        # (pin FSM: its Pin Type Bool, its Specific Bool)
                        kind, f1, f2 = 0, flag(f["vars"]["Pin Type Bool"][1]), flag(f["vars"]["Specific Bool"][1])
                    elif q["name"] == "pin_blue_health":
                        kind, f1, f2 = 1, flag(COCOON), -1
                    else:
                        continue   # (pin_dream_tree: the dream nail's)
                    if f1 == -2 or f2 == -2:
                        continue
                    px, py = at(q)
                    pins.append("{%d, %d, %d, %s, %s, %d, %d}" % (
                        mi, kind, spr(comp(q, "SpriteRenderer")["m_Sprite"], QUICK_K * scale(q)), U(px), U(py), f1, f2))
                elif "TextMeshPro" in qc:
                    # (a sub area's name: shown with its room)
                    t, mx, top, rgb, al = tx(q, "MSG")
                    names.append("{%d, %d, %d, %s, %s, 0x%06X, %d, %d}" % (ai, mi, t, U(mx), U(top), rgb, al,
                                                                          "DisplayOnWorldMapOnly" in qc))
                elif "MapNextAreaDisplay" in qc:
                    # (an arrow to the area next to it, and its name: the quick map's, once that area was visited)
                    vs = comp(q, "MapNextAreaDisplay")["visitedString"]
                    vf = flag(vs) if vs else -1
                    if vf == -2:
                        continue
                    arrow = next(z for z in kids[q["id"]] if z["name"] == "Map_Arrow")
                    nm = next(z for z in kids[q["id"]] if z["name"] == "Area Name")
                    m3 = arrow["m3"]
                    ang = math.degrees(math.atan2(m3[3], m3[0]))
                    ax, ay = at(arrow)
                    t, mx, top, rgb, al = tx(nm, "MSG")
                    nexts.append("{%d, %d, %s, %s, %s, %d, %s, %s, %d, %d}" % (
                        mi, spr(comp(arrow, "SpriteRenderer")["m_Sprite"], QUICK_K * scale(arrow)), U(ax), U(ay),
                        U(ang), t, U(mx), U(top), al, vf))
        areas.append("{%d, %d, %d, %s, %s, %d, 0x%Xu}" % (first, len(rows) - first, flag(have) if have else -1,
                                                        U(qpos[0]), U(qpos[1]),
                                                        texts.add(text.clean(sh["Map Zones"][title]), "DIALOGUE"), zones))
    out["MAP_AREAS"] = "{%s}" % ", ".join(areas)
    out["MAP_ROOMS"] = "{%s}" % ", ".join(rows)
    out["MAP_PINS"] = "{%s}" % ", ".join(pins)
    out["MAP_NEXT"] = "{%s}" % ", ".join(nexts)
    out["MAP_NAMES"] = "{%s}" % ", ".join(names)
    out["MAP_QUICK_K"] = U(QUICK_K)
    # the shade's mark (Shade Pos: its fader, its head), the markers (Map Markers: B1, R1, Y1, W1)
    sp = by["Game_Map/Shade Pos"]
    parts = []
    for n in ("Map_shade_fader", "Map_shade_head"):
        q = by["Game_Map/Shade Pos/" + n]
        v = comp(q, "SpriteRenderer")
        parts.append("{%d, %s, %s, %d}" % (spr(v["m_Sprite"], QUICK_K * scale(q)), U(q["lpos"][0] * sp["lscale"][0]),
                                           U(q["lpos"][1] * sp["lscale"][1]), int(round(v["m_Color"]["a"] * 255))))
    out["MAP_SHADE"] = "{%s}" % ", ".join(parts)
    out["MAP_MARKERS"] = "{%s}" % ", ".join(
        str(spr(comp(by["Game_Map/Map Markers/%s1" % c], "SpriteRenderer")["m_Sprite"],
                QUICK_K * scale(by["Game_Map/Map Markers/%s1" % c]))) for c in "BRYW")
    out["MAP_COMPASS_K"] = U(scale(by["Game_Map/Compass Icon"]))
    # the quick map's HUD: its backdrop (BG: black, its color's alpha at its fade group's full), the area's name (its
    # rect's middle and top, on its backboard), the no map message (its symbol, its line)
    h = unity.prefab(*HUD)
    hb = {o["path"]: o for o in h["objects"]}
    bg = hb[QUICK + "BG"]
    out["MAP_BG_A"] = U(comp(bg, "SpriteRenderer")["m_Color"]["a"] * comp(bg, "FadeGroup")["fullAlpha"])
    an = hb[QUICK + "Area Name"]
    tc = comp(an, "TextContainer")
    k = an["lscale"][1]
    out["MAP_TITLE"] = "{%s, %s}" % (U(an["pos"][0]), U(an["pos"][1] + (1 - tc["m_pivot"]["y"]) * tc["m_rect"]["height"] * k))
    bb = hb[QUICK + "Area Name/Backboard"]
    v = comp(bb, "SpriteRenderer")
    out["MAP_TITLE_BACK"] = "{%d, %s, %s, %s, %s, %d}" % (
        sprites.id(h, v["m_Sprite"], max(bb["m3"][0], bb["m3"][4]), res), U(bb["pos"][0]), U(bb["pos"][1]),
        U(bb["m3"][0] / max(bb["m3"][0], bb["m3"][4])), U(bb["m3"][4] / max(bb["m3"][0], bb["m3"][4])),
        int(round(v["m_Color"]["a"] * 255)))
    sym = hb[QUICK + "No Map/No_Map_symbol"]
    out["MAP_NO_MAP"] = "{%d, %s, %s}" % (sprites.id(h, comp(sym, "SpriteRenderer")["m_Sprite"], sym["lscale"][0], res),
                                          U(sym["pos"][0]), U(sym["pos"][1]))
    msg = hb[QUICK + "No Map/Msg"]
    tc = comp(msg, "TextContainer")
    out["MAP_NO_MAP_MSG"] = "{%d, %s, %s}" % (
        texts.add(text.clean(sh["Prompts"]["NO_MAP"]), "DIALOGUE"), U(msg["pos"][0]),
        U(msg["pos"][1] + (1 - tc["m_pivot"]["y"]) * tc["m_rect"]["height"] * msg["lscale"][1]))
    # the map's update message (Map Update Msg: made at the HUD's (-1.29, -6.83) at its scale; its quill writing, its
    # line (left aligned, its middle), its large backboard)
    mm = unity.prefab(*MAP_MSG)
    mb = {o["path"]: o for o in mm["objects"]}
    r0 = mb["Map Update Msg"]
    k0 = r0["lscale"][0]
    rel = lambda o: ((o["pos"][0] - r0["pos"][0]), (o["pos"][1] - r0["pos"][1]))
    an = mb["Map Update Msg/Animation"]
    tq = mb["Map Update Msg/Text"]
    tc = comp(tq, "TextContainer")
    bb = mb["Map Update Msg/Backboard Large"]
    v = comp(bb, "SpriteRenderer")
    kx, ky = math.hypot(bb["m3"][0], bb["m3"][3]), math.hypot(bb["m3"][1], bb["m3"][4])
    out["MAP_MSG"] = "{%s, %s, %s, %s, %s, %s, %d, %d, %s, %s, %s, %s, %d}" % (
        U(MAP_MSG_AT[0]), U(MAP_MSG_AT[1]), U(rel(an)[0]), U(rel(an)[1]),
        U(rel(tq)[0] - tc["m_pivot"]["x"] * tc["m_rect"]["width"] * k0), U(rel(tq)[1]),
        texts.add(text.clean(sh["UI"]["MAP_UPDATED"]), "MSG"),
        sprites.id(mm, v["m_Sprite"], max(kx, ky), res), U(rel(bb)[0]), U(rel(bb)[1]), U(kx / max(kx, ky)),
        U(ky / max(kx, ky)), int(round(v["m_Color"]["a"] * 255)))
    out["MAP_MSG_K"] = U(k0)
    # the first map's lesson (Prompts/First Map, on FIRST MAP UP): its backdrop's alpha, its lines (their middles and
    # rects' tops: the title in the item message's name style), its picture, its divider, the stop, the key's place
    fm = lambda n: hb["_GameCameras/HudCamera/Prompts/First Map/" + n]
    out["FIRSTMAP_BG_A"] = U(comp(fm("BG"), "SpriteRenderer")["m_Color"]["a"])
    rows = []
    for n, style in (("Text 1", "MSG_NAME"), ("Text Hold", "MSG"), ("Text 2", "MSG"), ("Text 3", "MSG")):
        q = fm(n)
        tc, gt = comp(q, "TextContainer"), comp(q, "SetTextMeshProGameText")
        rows.append("{%d, %s, %s}" % (texts.add(text.clean(sh[gt["sheetName"]][gt["convName"]]), style), U(q["pos"][0]),
                                      U(q["pos"][1] + (1 - tc["m_pivot"]["y"]) * tc["m_rect"]["height"] * q["lscale"][1])))
    out["FIRSTMAP_TEXTS"] = "{%s}" % ", ".join(rows)
    parts = []
    for n in ("Map_prompt", "Inv_0017_divider", "Stop"):
        q = fm(n)
        kx, ky = q["lscale"][0], q["lscale"][1]
        parts.append("{%d, %s, %s, %s, %s}" % (sprites.id(h, comp(q, "SpriteRenderer")["m_Sprite"], max(kx, ky), res),
                                               U(q["pos"][0]), U(q["pos"][1]), U(kx / max(kx, ky)), U(ky / max(kx, ky))))
    out["FIRSTMAP_PIECES"] = "{%s}" % ", ".join(parts)
    out["FIRSTMAP_KEY"] = "{%d, %s, %s}" % (texts.add("var", "PROMPT"), U(fm("Button")["pos"][0]), U(fm("Button")["pos"][1]))
    out.update(pane(sprites, texts, res, h, hb, sh, flag))
    return out


def pane(sprites, texts, res, h, hb, sh, flag):
    """The inventory's map pane: places in HUD units from the inventory's (its panes' when shown), the Game_Map's from
    the HUD's middle"""
    U = lambda v: "%.4ff" % v
    inv = hb[INV[:-1]]
    out = {}

    def comp(o, cls):
        return next((c["v"] for c in o["c"] if (c.get("class") or c["type"]) == cls and c.get("v") is not None), None)

    def rel(o, dx=0.0, dy=0.0):
        return o["pos"][0] - inv["pos"][0] + dx, o["pos"][1] - inv["pos"][1] + dy

    def piece(path, dx=0.0, dy=0.0, k=1.0):
        """a sprite at its place: "{sprite, x, y, kx, ky}" (UiPiece)"""
        o = hb[path]
        x, y = rel(o, dx, dy)
        sx, sy = o["m3"][0] * k, o["m3"][4] * k
        m = max(abs(sx), abs(sy))
        sr = comp(o, "SpriteRenderer")
        return "{%d, %s, %s, %s, %s}" % (sprites.id(h, sr["m_Sprite"], m, res), U(x), U(y), U(sx / m), U(sy / m))

    def tx(path, dx=0.0, dy=0.0):
        """a text's place: "{x, top, w}" (UiText: x its rect's left, middle or right, as it is aligned)"""
        o = hb[path]
        x, y = rel(o, dx, dy)
        tc, tm = comp(o, "TextContainer"), comp(o, "TextMeshPro")
        k = o["m3"][0]
        w, ht = tc["m_rect"]["width"] * k, tc["m_rect"]["height"] * k
        al = tm["m_textAlignment"] % 4
        return "{%s, %s, %s}" % (U(x - tc["m_pivot"]["x"] * w + (0, w / 2, w, 0)[al]), U(y + (1 - tc["m_pivot"]["y"]) * ht), U(w))

    def at(path, dx=0.0, dy=0.0):
        return "{%s, %s}" % tuple(U(v) for v in rel(hb[path], dx, dy))

    def txt(key, style, sheet="UI"):
        return texts.add(text.clean(sh[sheet][key]), style)

    # the wide map: each area's piece (none: never had) and its name, where the map zooms to for it, the compass there,
    # the pan's bounds it widens; the pieces' grey (Selection Colour: unselected), their names' grey
    dx, dy = WIDE_POS
    rows = []
    for an, have, _, title, wide, zoom, compass, ext in AREAS:
        never = have is not None and flag(have) == -2
        e = ext or (None,) * 4
        ext_s = ", ".join(U(v if v is not None else (1e3, -1e3)[i % 2]) for i, v in enumerate(e))
        cx, cy = rel(hb[WM + "Wide Map"], dx + compass[0], dy + compass[1])
        if never:
            rows.append("{{-1, 0, 0, 0, 0}, -1, {0, 0, 0}, %s, %s, %s, %s, {%s}}" % (U(zoom[0]), U(zoom[1]), U(cx), U(cy), ext_s))
            continue
        base = WM + "Wide Map/" + wide
        rows.append("{%s, %d, %s, %s, %s, %s, %s, {%s}}" % (piece(base, dx, dy), txt(title, "MSG_S", "Map Zones"),
                                                           tx(base + "/Area Name", dx, dy), U(zoom[0]), U(zoom[1]),
                                                           U(cx), U(cy), ext_s))
    out["MAP_WIDE"] = "{%s}" % ", ".join(rows)
    out["MAP_WIDE_GREY"], out["MAP_WIDE_NAME_GREY"] = "0x5C", "0x5B"   # (0.3603, 0.3569)
    ci = hb[WM + "Wide Map/Compass Icon"]
    out["MAP_WIDE_COMPASS_K"] = U(ci["lscale"][0])
    wx, wy = rel(hb[WM + "Wide Map"], dx, dy)
    zx, zy = hb[WM + "Wide Map"]["pos"][0] + dx + ZOOM_FROM[0], hb[WM + "Wide Map"]["pos"][1] + dy + ZOOM_FROM[1]
    out["MAP_ZOOM"] = "{%s, %s, %s, %s}" % (U(zx), U(zy), U(ZOOM_K0), U(ZOOM_K))
    out["MAP_PAN"] = "{%s}" % ", ".join(U(v) for v in PAN)
    gm = unity.prefab(*GAME_MAP)
    gmc = next(c["v"] for c in gm["objects"][0]["c"] if (c.get("class") or c["type"]) == "GameMap")
    out["MAP_PAN_SPEED"] = U(gmc["panSpeed"])
    def turned(path):
        """a sprite turned: "{sprite, x, y, k, degrees}" """
        o = hb[path]
        x, y = rel(o)
        m = o["m3"]
        k = math.hypot(m[0], m[3])
        sr = comp(o, "SpriteRenderer")
        return "{%d, %s, %s, 1.0f, %s}" % (sprites.id(h, sr["m_Sprite"], k, res), U(x), U(y),
                                          U(math.degrees(math.atan2(m[3], m[0]))))
    out["MAP_PAN_ARROWS"] = "{%s}" % ", ".join(turned(WM + "Pan Arrows/Arrow " + d) for d in "UDLR")
    # the actions: their backboards, their keys' places, their texts (Confirm Action: right; the markers': left)
    def action(base, keys):
        return "{%s, %s, %s, {%s}}" % (piece(base + "/Backboard"), at(base + "/ActionButtonIcon"), tx(base + "/Text"),
                                       ", ".join(str(txt(k, "MSG")) for k in keys))
    out["MAP_ACT_CONFIRM"] = action(WM + "Confirm Action", ("CTRL_ZOOM_IN", "CTRL_ZOOM_OUT", "CTRL_ZOOM_OUT"))
    out["MAP_ACT_MARKER"] = action(WM + "Map Marker Action", ("CTRL_MARKERS", "CTRL_MARKER_PLACE", "CTRL_MARKER_REMOVE"))
    out["MAP_ACT_CANCEL"] = action(WM + "Map Markers/Marker Cancel Action", ("CTRL_CANCEL",) * 3)
    out["MAP_ACT_CHANGE"] = action(INV + "Map/Marker Change Action", ("CTRL_MARKER_CHANGE",) * 3)
    out["MAP_ACT_KEY"] = action(INV + "Map Key/Action", ("CTRL_HIDE_KEY", "CTRL_HIDE_PINS", "CTRL_SHOW_PINS_KEY"))
    # the map's key: its backboard (its top at Backboard First Y, lower a row's step each), its rows (Pin First Y, then
    # each its step lower): their pins, their names; the pins' flags
    ctl = next(c["fsm"] for c in hb[INV + "Map Key"]["c"] if c.get("fsm") and c["fsm"]["name"] == "Control")
    v = ctl["vars"]
    mk = hb[INV + "Map Key"]
    bb = hb[INV + "Map Key/Backboard Key"]
    out["MAP_KEY_BB"] = piece(INV + "Map Key/Backboard Key", 0, v["Backboard First Y"][1] - bb["lpos"][1])
    out["MAP_KEY_STEP"] = U(v["Pin Y Increment"][1])
    krows, kflags = [], []
    for n, icon, fl in KEYS:
        kb = hb[INV + "Map Key/Keys/" + n]
        ky = v["Pin First Y"][1] - kb["lpos"][1]
        krows.append("{%s, %d, %s}" % (piece(INV + "Map Key/Keys/%s/%s" % (n, icon), 0, ky),
                                       txt(comp(hb[INV + "Map Key/Keys/%s/Text" % n], "SetTextMeshProGameText")["convName"], "MSG"),
                                       tx(INV + "Map Key/Keys/%s/Text" % n, 0, ky)))
        kflags.append(str(flag(fl)))
    out["MAP_KEY_ROWS"] = "{%s}" % ", ".join(krows)
    out["MAP_KEY_FLAGS"] = "{%s}" % ", ".join(kflags)
    out["MAP_KEY_N"] = len(KEYS)
    # the markers' menu (MapMarkerMenu): its backboard and shadow, its cursor and its back, the markers (b, r, y, w: the
    # menu's; at 0.6, the chosen at 0.7) and their amounts' place under them, the first's x, the next's step, their y;
    # the placement cursor (its sprite; its first place, its bounds: from the menu's place), its speed, its pull to a
    # marker; the placement box's radius, a marker's (InvMarker)
    mm = hb[WM + "Map Markers"]
    mmc = comp(mm, "MapMarkerMenu")
    ox, oy = rel(mm)
    out["MAP_MM_PIECES"] = "{%s}" % ", ".join(piece(WM + "Map Markers/" + n) for n in (
        "Backboard", "Backboard/Backboard Shadow", "Cursor/Cursor Bg", "Cursor"))
    out["MAP_MM_MARKERS"] = "{%s}" % ", ".join(
        str(sprites.id(h, comp(hb[WM + "Map Markers/Marker_" + c], "SpriteRenderer")["m_Sprite"], 0.7, res)) for c in "bryw")
    # (an amount's text from its marker's place, the marker at 0.6)
    mb_ = hb[WM + "Map Markers/Marker_b"]
    out["MAP_MM_AMOUNT"] = tx(WM + "Map Markers/Marker_b/Amount B", -rel(mb_)[0], -rel(mb_)[1])
    out["MAP_MM_ROW"] = "{%s, %s, %s, %s, %s}" % (U(ox), U(oy), U(mmc["xPos_start"]), U(mmc["xPos_interval"]), U(mmc["markerY"]))
    pc = hb[WM + "Map Markers/Placement Cursor"]
    o = mmc["placementCursorOrigin"]
    out["MAP_MM_PLACE"] = "{%d, %s, %s, %s, %s, %s, %s, %s, %s}" % (
        sprites.id(h, comp(pc, "SpriteRenderer")["m_Sprite"], pc["lscale"][0], res), U(o["x"]), U(o["y"]),
        U(mmc["placementCursorMinX"]), U(mmc["placementCursorMaxX"]), U(mmc["placementCursorMinY"]),
        U(mmc["placementCursorMaxY"]), U(mmc["panSpeed"]), U(mmc["markerPullSpeed"]))
    pb = comp(hb[WM + "Map Markers/Placement Box"], "CircleCollider2D")
    gmb = {q["path"]: q for q in gm["objects"]}
    out["MAP_MM_RADII"] = U(pb["m_Radius"] + comp(gmb["Game_Map/Map Markers/B1/Collider"], "CircleCollider2D")["m_Radius"])
    out["MAP_MM_UI_PAUSE"] = U(mmc["uiPause"])
    # (the wide map's names, the actions' texts: their sizes as the game's MSG)
    texts.add("0123456789", "MSG")
    out["TXT_KEY_SHIFT"] = texts.add("shift", "MSG")   # (the dream nail's key here: the calculator's shift)
    return out
