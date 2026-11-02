"""The stag's menu (Stag Map: its stations, the map of them, the selector) as the stations of this part of the game have
it: STAG_* defines for data.h. Its places in HUD units from the menu's (Open Stag puts it at (0.3, 0.3); it grows to
its full size, the prefab's, as it opens)."""
import math
import unity, ents, tk2d, text

MENU = ("resources.assets", 7203)
AT = (0.3, 0.3)
# the stations here: their list item, their map piece, the PlayerData bool that opens them (None: open from the start)
# (both apps list them all: a ride to a station of the other one is a way there)
STATIONS = (("Dirtmouth", None), ("Crossroads", "openedCrossroads"), ("Greenpath", "openedGreenpath"),
            ("Fungal Wastes", "openedFungalWastes"))
SCENES = ("Room_Town_Stag_Station", "Crossroads_47", "Fungus1_16_alt", "Fungus2_02")   # (their rooms: Stag Control's To Scene)


def build(sprites, texts, res, actor_clip, pd_flags, str_id, rooms):
    p = unity.prefab(*MENU)
    by = {o["path"]: o for o in p["objects"]}
    root = by["Stag Map"]
    rk = math.hypot(root["m3"][0], root["m3"][3])
    U = lambda v: "%.4ff" % v
    out = {}
    sh = text.sheets()

    def comp(o, cls):
        return next((c["v"] for c in o["c"] if (c.get("class") or c["type"]) == cls and c.get("v") is not None), None)

    def rel(o):
        return o["pos"][0] - root["pos"][0], o["pos"][1] - root["pos"][1]

    def piece(path, at=None):
        """its sprite at its place (from the menu's), its scale: "{sprite, x, y, kx, ky}" """
        o = by["Stag Map/" + path]
        x, y = at if at is not None else rel(o)
        m = o["m3"]
        sx = math.hypot(m[0], m[3]) * (1 if m[0] >= 0 else -1)
        sy = math.hypot(m[1], m[4]) * (1 if m[4] >= 0 else -1)
        s = max(abs(sx), abs(sy))
        sr = comp(o, "SpriteRenderer")
        if sr and sr.get("m_Sprite"):
            spr = sprites.id(p, sr["m_Sprite"], s, res)
        else:
            tk = comp(o, "tk2dSprite")
            cp, cpid = ents._ref_file(p, tk["collection"])
            col = tk2d.collection(cp, cpid)
            spr = sprites.tk2d(cp, cpid, unity.S(col["spriteDefinitions"][tk["_spriteId"]]["name"]), s, res)
        return "{%d, %s, %s, %s, %s}" % (spr, U(x), U(y), U(sx / s), U(sy / s))

    def tx(path):
        """a text's place, centred (TextMeshPro in its TextContainer, aligned top centre): "{x, top, width}" """
        o = by["Stag Map/" + path]
        tc = comp(o, "TextContainer")
        k = math.hypot(o["m3"][0], o["m3"][3])
        w, h = tc["m_rect"]["width"] * k, tc["m_rect"]["height"] * k
        x, y = rel(o)
        al = comp(o, "TextMeshPro")["m_textAlignment"] % 4
        x0 = x - tc["m_pivot"]["x"] * w
        return "{%s, %s, %s}" % (U(x0 + (0, w / 2, w, 0)[al]), U(y + (1 - tc["m_pivot"]["y"]) * h), U(w))

    out["STAG_N"] = str(len(STATIONS))
    out["STAG_AT"] = "{%s, %s}" % (U(AT[0]), U(AT[1]))
    out["STAG_K"] = U(rk)   # (its scale, open)
    out["STAG_BACKING"] = piece("backing")
    out["STAG_BACKING_A"] = U(comp(by["Stag Map/backing"], "SpriteRenderer")["m_Color"]["a"])
    # the map: its core, each station's piece (shown as it is opened), the selector at the chosen one's mark (its
    # move_stagmap_marker's Marker Pos, in the pieces' space), the Knight's icon at the station it is at
    pr = by["Stag Map/Stag_Map_Pieces"]
    pk = math.hypot(pr["m3"][0], pr["m3"][3])
    marks = []
    for name, _ in STATIONS:
        fsm = next(c["fsm"] for c in by["Stag Map/UI List Stag/" + name]["c"]
                   if c.get("fsm") and c["fsm"]["name"] == "move_stagmap_marker")
        mx, my, _ = fsm["vars"]["Marker Pos"][1]
        marks.append("{%s, %s}" % (U(pr["pos"][0] + mx * pk - root["pos"][0]), U(pr["pos"][1] + my * pk - root["pos"][1])))
    out["STAG_PIECES"] = "{%s}" % ", ".join(piece("Stag_Map_Pieces/" + n) for n in ["Core"] + [s for s, _ in STATIONS])
    out["STAG_OPENED"] = "{%s}" % ", ".join(str(pd_flags[b]) if b else "-1" for _, b in STATIONS)
    out["STAG_MARKS"] = "{%s}" % ", ".join(marks)
    sel = by["Stag Map/Stag_Map_Pieces/Map_selector"]
    out["STAG_SELECTOR"] = "{%s, %s, %s, %s}" % (U(0), U(0), U(math.hypot(sel["m3"][0], sel["m3"][3])),
                                                 U(math.hypot(sel["m3"][1], sel["m3"][4])))
    out["STAG_MAP_KNIGHT"] = piece("Stag_Map_Pieces/Map_selector/Knight Icon", at=(0, 0))
    ki, sd = by["Stag Map/Stag_Map_Pieces/Map_selector/Knight Icon"], by["Stag Map/Stag_Map_Pieces/Map_selector/Knight Icon/Shadow"]
    out["STAG_MAP_SHADOW"] = piece("Stag_Map_Pieces/Map_selector/Knight Icon/Shadow",
                                   at=(sd["pos"][0] - ki["pos"][0], sd["pos"][1] - ki["pos"][1]))
    # the list: its title, its divider, a row each opened station (1.0 apart, the first's place), the pointers either
    # side of the current one, the Knight's icon by the station it is at
    first = by["Stag Map/UI List Stag/" + STATIONS[0][0]]
    out["STAG_T_ROW"] = tx("UI List Stag/" + STATIONS[0][0])
    out["STAG_ROW_DY"] = U(first["pos"][1] - by["Stag Map/UI List Stag/" + STATIONS[1][0]]["pos"][1])
    ptr = by["Stag Map/UI List Stag/%s/Pointer R" % STATIONS[0][0]]
    dx, dy = ptr["pos"][0] - first["pos"][0], ptr["pos"][1] - first["pos"][1]
    fx, fy = rel(first)
    out["STAG_POINTER_R"] = piece("UI List Stag/%s/Pointer R" % STATIONS[0][0], at=(dx, dy))
    out["STAG_POINTER_L"] = piece("UI List Stag/%s/Pointer L" % STATIONS[0][0], at=(-dx, dy))
    out["STAG_ROW"] = "{%s, %s}" % (U(fx), U(fy))
    out["STAG_LIST_KNIGHT"] = piece("UI List Stag/Knight Icon")
    out["STAG_DIVIDER"] = piece("UI List Stag/Divider")
    out["STAG_T_STATIONS"] = tx("UI List Stag/Stations")
    out["TXT_STAG_STATIONS"] = texts.add(text.clean(sh["StagMenu"]["STATIONS"]), "MSG")
    out["TXT_STAG_NAMES"] = "{%s}" % ", ".join(str(texts.add(text.clean(sh["StagMenu"][n.upper().replace(" ", "_")]), "TUTE"))
                                               for n, _ in STATIONS)
    out["STAG_NAMES_STR"] = "{%s}" % ", ".join(str(str_id(n)) for n, _ in STATIONS)
    # the borders (Border Display: their clips up as it opens)
    for n in ("Top", "Bottom"):
        o = by["Stag Map/Border/Stag_Border_" + n]
        x, y = rel(o)
        out["STAG_BORDER_" + n.upper()] = "{%s, %s, %s, %s, %s, %s, 0}" % (
            U(x), U(y), U(math.hypot(o["m3"][0], o["m3"][3])), U(math.hypot(o["m3"][1], o["m3"][4])),
            actor_clip("stagui", "Stag_Border_%s Up" % n), actor_clip("stagui", "Stag_Border_%s Down" % n))
    # the action keys: Travel, Cancel (their texts right aligned, their keys' boxes)
    for n, key in (("CONFIRM", "CTRL_TRAVEL"), ("CANCEL", "CTRL_CANCEL")):
        out["STAG_T_ACT_" + n] = tx("Buttons/Action %s/Text" % n.capitalize())
        o = by["Stag Map/Buttons/Action %s/ActionButtonIcon" % n.capitalize()]
        out["STAG_KEY_" + n] = "{%s, %s}" % (U(rel(o)[0]), U(rel(o)[1]))
        out["TXT_STAG_" + key] = texts.add(text.clean(sh["UI"][key]), "MSG")
    # (the rides: the scripts' scene names, the rooms they are)
    out["STAG_SCENES_STR"] = "{%s}" % ", ".join(str(str_id(s)) for s in SCENES)
    # (a station in another part of the game: 0x8000 and its room's number among them all)
    out["STAG_SCENES_ROOM"] = "{%s}" % ", ".join(str(rooms.index(s) if s in rooms else 0x8000 | ents.GLOBAL_ROOMS.index(s))
                                                 for s in SCENES)
    return out
