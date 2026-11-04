"""The inventory (_GameCameras/HudCamera/Inventory): its border, the Inventory pane and the Charms pane. Where their
pieces are (HUD units: the border's from the inventory's place, a pane's from the pane's own), the sprites they show,
their texts (src/inv.c)."""
import math
import unity, tk2d, text

ROOT = "_GameCameras/HudCamera/Inventory/"
# the items the Inventory pane lists after the fixed ones (Build Equipment List: its order), by what has them:
# (object, its name and description, the PlayerData that has it: a bool, or an int above 0)
EQUIPMENT = [("Dash Cloak", "DASH", "hasDash"), ("Mantis Claw", "WALLJUMP", "hasWalljump"), ("Lantern", "LANTERN", "hasLantern"),
             ("Map and Quill", "MAP", "hasMap"), ("City Key", "CITYKEY", "hasCityKey"), ("Simple Key", "SIMPLEKEY", "simpleKeys"),
             ("Rancid Egg", "RANCIDEGG", "rancidEggs")]
# (the map and the quill: Map's sprite, then its own, as they are had)
QUILL = "QUILL"
TRINKETS = 4


def _doc():
    return unity.prefab("resources.assets", 5473)


def build(sprites, texts, res, charms, charm_icon):
    """-> {define: value} for data.h. res: the HUD's resolution; charms: the charms this part of the game has;
    charm_icon(id) -> the charm's icon sprite (drawn at CHARM_ICON_K)"""
    d = _doc()
    by = {o["path"]: o for o in d["objects"]}
    U = lambda v: "%.4ff" % v
    out = {}
    sh = text.sheets()
    inv = by[ROOT[:-1]]

    def comp(o, cls):
        return next((c["v"] for c in o["c"] if (c.get("class") or c["type"]) == cls and c.get("v") is not None), None)

    def rel(o, base):
        return o["pos"][0] - base["pos"][0], o["pos"][1] - base["pos"][1]

    def turned(path, base):
        """an object's sprite turned: "{sprite, x, y, kx, ky, degrees}" """
        o = by[ROOT + path]
        x, y = rel(o, base)
        m = o["m3"]
        sx, sy = math.hypot(m[0], m[3]), math.hypot(m[1], m[4])
        deg = math.degrees(math.atan2(m[3], m[0]))
        sr = comp(o, "SpriteRenderer")
        sy = thick(sr, sy)
        k = max(sx, sy)
        return "{%d, %s, %s, %s, %s, %s}" % (sprites.id(d, sr["m_Sprite"], k, res), U(x), U(y), U(sx / k), U(sy / k), U(deg))

    def thick(sr, sy):
        """(a line thinner than a pixel here: one pixel, so that it shows)"""
        s = unity.sprite(d["level"], tuple(d["externals"]), *sr["m_Sprite"])
        px = s.h / s.ppu * sy * 100 / 8.7107
        return sy * 1.2 / px if px < 1.2 else sy

    def piece(path, base, scale=None, k=1.0):
        """an object's sprite (SpriteRenderer or 2D Toolkit) at its place from base's, its scale (or a given one) ->
        "{sprite, x, y, kx, ky}" (its sprite registered at the larger of its scales; drawn at kx, ky of that)"""
        o = by[ROOT + path]
        x, y = rel(o, base)
        sx, sy = (o["m3"][0] * k, o["m3"][4] * k) if scale is None else scale
        sr = comp(o, "SpriteRenderer")
        if sr and sr.get("m_Sprite"):
            sy = math.copysign(thick(sr, abs(sy)), sy)
        m = max(abs(sx), abs(sy))
        if sr and sr.get("m_Sprite"):
            spr = sprites.id(d, sr["m_Sprite"], m, res)
        else:
            tk = comp(o, "tk2dSprite")
            cp, cpid = tk2d.ref_file("resources.assets", {"m_FileID": tk["collection"][0], "m_PathID": tk["collection"][1]})
            col = tk2d.collection(cp, cpid)
            spr = sprites.tk2d(cp, cpid, unity.S(col["spriteDefinitions"][tk["_spriteId"]]["name"]), m, res)
        return "{%d, %s, %s, %s, %s}" % (spr, U(x), U(y), U(sx / m), U(sy / m))

    def at(path, base):
        x, y = rel(by[ROOT + path], base)
        return U(x), U(y)

    def box(path, k=1.0):
        """an object's collider: its size as the world has it (with its scales), its offset"""
        o = by[ROOT + path]
        bc = comp(o, "BoxCollider2D")
        sx, sy = abs(o["m3"][0]), abs(o["m3"][4])
        return "{%s, %s, %s, %s}" % (U(bc["m_Size"]["x"] * sx * k), U(bc["m_Size"]["y"] * sy * k),
                                     U(bc["m_Offset"]["x"] * sx * k), U(bc["m_Offset"]["y"] * sy * k))

    def tx(path, base):
        """a text's place (TextMeshPro in its TextContainer, top aligned): "{x, top, width}", x its rect's left, center or
        right as it is aligned (0 left, 1 center, 2 right)"""
        o = by[ROOT + path]
        x, y = rel(o, base)
        tc, tm = comp(o, "TextContainer"), comp(o, "TextMeshPro")
        k = o["m3"][0]
        w, h = tc["m_rect"]["width"] * k, tc["m_rect"]["height"] * k
        al = tm["m_textAlignment"] % 4
        x0 = x - tc["m_pivot"]["x"] * w
        top = y + (1 - tc["m_pivot"]["y"]) * h
        return "{%s, %s, %s}" % (U(x0 + (0, w / 2, w, 0)[al]), U(top), U(w))

    def txt(sheet, key, style):
        return texts.add(text.clean(sh[sheet][key]), style)

    # the inventory's place on the HUD; the border, from it
    out["INV_X"], out["INV_Y"] = U(inv["pos"][0] - by["_GameCameras/HudCamera"]["pos"][0]), \
        U(inv["pos"][1] - by["_GameCameras/HudCamera"]["pos"][1])
    border = ["Border/Inv_Border_Corner TL", "Border/Inv_Border_Corner TR", "Border/Inv_Border_Corner (1)",
              "Border/Inv_Border_Corner (2)", "Border/Inv_Border_Top", "Border/Inv_Border_Bottom"]
    out["INV_BORDER"] = "{%s}" % ", ".join(piece(p, inv) for p in border)
    out["INV_NBORDER"] = len(border)
    # (the dark frame about it: smooth, kept small)
    o = by[ROOT + "Border/Menu_Border_Black"]
    x, y = rel(o, inv)
    sr = comp(o, "SpriteRenderer")
    out["INV_FRAME"] = "{%d, %s, %s, 1.0f, 1.0f}" % (sprites.id(d, sr["m_Sprite"], o["m3"][0], res * 0.25), U(x), U(y))
    out["INV_ARROW_L"] = piece("Border/Arrow Left/Arrow Left", inv)
    out["INV_ARROW_R"] = piece("Border/Arrow Right/Arrow Right", inv)
    out["INV_PANE_ARROW_L"] = piece("Border/Pane Arrow L/Arrow", inv)
    out["INV_PANE_ARROW_R"] = piece("Border/Pane Arrow R/Arrow", inv)
    for n, p in (("NAME", "Pane Name"), ("NAME_L", "Pane Name L"), ("NAME_R", "Pane Name R")):
        out["INV_PANE_" + n] = tx("Border/" + p, inv)
    out["TXT_INV_PANES"] = "{%s}" % ", ".join("%d" % txt("UI", k, "TUTE") for k in ("PANE_INVENTORY", "PANE_CHARMS",
                                                                                     "PANE_JOURNAL", "PANE_MAP"))

    # ---------------------------------------------------------------- the Charms pane
    cp_ = by[ROOT + "Charms"]
    out["CH_BB"] = piece("Charms/Backboards/BB 1", cp_)
    out["CH_BB_POS"] = "{%s}" % ", ".join("{%s, %s}" % at("Charms/Backboards/BB %d" % i, cp_) for i in range(1, 41))
    out["CH_BB_BOX"] = box("Charms/Backboards/BB 1")
    # (each backboard's charm: InvCharmBackboard)
    out["CH_BB_CHARM"] = "{%s}" % ", ".join(str(comp(by[ROOT + "Charms/Backboards/BB %d" % i], "InvCharmBackboard")["charmNum"])
                                            for i in range(1, 41))
    out["CH_GLOW"] = piece("Charms/Collected Charms/1/Glow", by[ROOT + "Charms/Collected Charms/1"])
    gl = comp(by[ROOT + "Charms/Collected Charms/1/Glow"], "SpriteRenderer")["m_Color"]
    out["CH_GLOW_A"] = U(gl["a"])
    out["CH_ICONS"] = "{%s}" % ", ".join(str(charm_icon(i)) if i in charms else "-1" for i in range(41))
    eq = by[ROOT + "Charms/Equipped Charms"]
    nd = by[ROOT + "Charms/Equipped Charms/Next Dot"]
    out["CH_NEXT_DOT"] = piece("Charms/Equipped Charms/Next Dot/Sprite", nd)
    out["CH_NEXT_DOT_BOX"] = box("Charms/Equipped Charms/Next Dot")
    # (an equipped charm's collider: its prefab's, at the scale it is made)
    pc = unity.prefab("resources.assets", comp(eq, "BuildEquippedCharms")["gameObjectList"][0][1])
    bc = comp(pc["objects"][0], "BoxCollider2D")
    k = 1.15
    out["CH_EQ_BOX"] = "{%s, %s, %s, %s}" % (U(bc["m_Size"]["x"] * k), U(bc["m_Size"]["y"] * k), U(bc["m_Offset"]["x"] * k),
                                            U(bc["m_Offset"]["y"] * k))
    out["CH_TEXT_EQUIPPED"] = tx("Charms/Equipped Charms/Text Equipped", cp_)
    out["CH_NOTCH_FULL"] = piece("Charms/Equipped Charms/Notches/Charm Cost 1/Sprite Full",
                                 by[ROOT + "Charms/Equipped Charms/Notches/Charm Cost 1"])
    out["CH_NOTCH_EMPTY"] = piece("Charms/Equipped Charms/Notches/Charm Cost 1/Sprite Empty",
                                  by[ROOT + "Charms/Equipped Charms/Notches/Charm Cost 1"])
    out["CH_NOTCH_X"], out["CH_NOTCH_Y"] = at("Charms/Equipped Charms/Notches/Charm Cost 1", cp_)
    x1 = rel(by[ROOT + "Charms/Equipped Charms/Notches/Charm Cost 2"], cp_)[0]
    out["CH_NOTCH_DX"] = U(x1 - rel(by[ROOT + "Charms/Equipped Charms/Notches/Charm Cost 1"], cp_)[0])
    be = comp(eq, "BuildEquippedCharms")
    for n, c in (("FULL", be["notchFullColor"]), ("OVER", be["notchOverColor"])):
        out["CH_NOTCH_%s_RGB" % n] = "{%d, %d, %d}" % tuple(round(c[q] * 255) for q in "rgb")
    out["CH_TEXT_NOTCHES"] = tx("Charms/Equipped Charms/Notches/Text Notches", cp_)
    out["CH_DIVIDER"] = piece("Charms/Equipped Charms/Inv_0017_divider", cp_)
    out["CH_COST_TEXT"] = tx("Charms/Details/Cost/Text Cost", cp_)
    out["CH_COST_PIP"] = piece("Charms/Details/Cost/Cost 1", by[ROOT + "Charms/Details/Cost/Cost 1"])
    out["CH_COST_X"], out["CH_COST_Y"] = at("Charms/Details/Cost/Cost 1", cp_)
    out["CH_COST_DX"] = U(rel(by[ROOT + "Charms/Details/Cost/Cost 2"], cp_)[0] - rel(by[ROOT + "Charms/Details/Cost/Cost 1"], cp_)[0])
    out["CH_DETAIL_X"], out["CH_DETAIL_Y"] = at("Charms/Details/Detail Sprite", cp_)
    out["CH_NAME"] = tx("Charms/Text Name", cp_)
    out["CH_DESC"] = tx("Charms/Text Desc", cp_)
    out["CH_CONFIRM"] = tx("Charms/Confirm Action/Text", cp_)
    # (its button: the keyboard's key box, as ActionButtonIcon shows it with a keyboard)
    o = by[ROOT + "Charms/Confirm Action/ActionButtonIcon"]
    x, y = rel(o, cp_)
    key = comp(by[ROOT + "Inv/Item Control/IconDashSlash"], "SpriteRenderer")["m_Sprite"]
    out["CH_CONFIRM_KEY"] = "{%d, %s, %s, 1.0f, 1.0f}" % (sprites.id(d, key, o["m3"][0], res), U(x), U(y))
    ks = unity.sprite(d["level"], tuple(d["externals"]), *key)
    out["CH_CONFIRM_KEY_W"] = U(ks.w / ks.ppu * o["m3"][0])
    out["CH_CURSOR"] = piece("Charms/Cursor/TL/Sprite", by[ROOT + "Charms/Cursor/TL"])
    cur = by[ROOT + "Charms/Cursor"]
    out["CH_CURSOR_K"] = "{%s, %s}" % (U(cur["m3"][0]), U(cur["m3"][4]))
    # (the corners' own scales: TL, TR, BL, BR)
    out["CH_CURSOR_CORNERS"] = "{%s}" % ", ".join(
        "{%s, %s}" % (U(by[ROOT + "Charms/Cursor/" + c]["lscale"][0] / 1.4), U(by[ROOT + "Charms/Cursor/" + c]["lscale"][1] / 1.4))
        for c in ("TL", "TR", "BL", "BR"))
    for key, style in (("CHARM_TXT_EQUIPPED", "TUTE"), ("CHARM_TXT_OVERCHARMED", "TUTE"), ("CHARM_TXT_COST", "MSG"),
                       ("CTRL_EQUIP", "MSG"), ("CTRL_UNEQUIP", "MSG"), ("CHARM_ALERT_NONE", "MSG"),
                       ("CHARM_ALERT_NONE_BENCH", "MSG")):
        out["TXT_" + key] = txt("UI", key, style)
    out["TXT_CHARM_NOTCHES"] = texts.add(text.clean(sh["UI"].get("CHARM_TXT_NOTCHES", "Notches")), "MSG")
    out["TXT_CHARM_NAMES_INV"] = "{%s}" % ", ".join(
        str(txt("UI", "CHARM_NAME_%d" % i, "DIALOGUE")) if i in charms else "-1" for i in range(41))
    out["TXT_CHARM_DESCS"] = "{%s}" % ", ".join(
        str(txt("UI", "CHARM_DESC_%d" % i, "MSG")) if i in charms else "-1" for i in range(41))
    out["TXT_KEY_OK"] = texts.add("OK", "MSG")
    # (away from a bench, where Equip and its key would be)
    out["TXT_CHARM_BENCH"] = texts.add("Rest at a bench", "MSG")

    # ---------------------------------------------------------------- the Inventory pane
    ip = by[ROOT + "Inv"]
    items = by[ROOT + "Inv/Inv_Items"]
    fixed = [("Heart Pieces", None), ("Soul Orb", None), ("Nail", None), ("Spell Fireball", "Fireball Bg"),
             ("Spell Focus", "Focus Ring"), ("Geo", None)]
    out["INV_FIXED"] = "{%s}" % ", ".join(piece("Inv/Inv_Items/" + n, ip) for n, _ in fixed)
    out["INV_FIXED_BG"] = "{%s}" % ", ".join(piece("Inv/Inv_Items/%s/%s" % (n, b), ip) if b else "{-1, 0, 0, 0, 0}"
                                            for n, b in fixed)
    out["INV_FIXED_BOX"] = "{%s}" % ", ".join(box("Inv/Inv_Items/" + n) for n, _ in fixed)
    out["INV_HEART_PIECES"] = "{%s}" % ", ".join(piece("Inv/Inv_Items/Heart Pieces/Pieces %d" % i, ip) for i in range(1, 5))
    out["INV_VESSEL_PIECES"] = "{%s}" % ", ".join(piece("Inv/Inv_Items/Soul Orb/" + p, ip) for p in ("Piece 1", "Piece 2", "Piece All"))
    out["INV_GEO_TEXT"] = tx("Inv/Inv_Items/Geo/Geo Amount", ip)
    eqp = by[ROOT + "Inv/Equipment"]
    # (Build Equipment List's places: equip_position's first, its steps, four to a row)
    out["INV_EQ_X"], out["INV_EQ_Y"], out["INV_EQ_DX"], out["INV_EQ_DY"] = U(1.1), U(-3.8), U(2.08), U(-2.18)
    out["INV_EQ"] = "{%s}" % ", ".join(piece("Inv/Equipment/" + n, eqp) for n, _, _ in EQUIPMENT)
    out["INV_EQ_BOX"] = "{%s}" % ", ".join(box("Inv/Equipment/" + n) for n, _, _ in EQUIPMENT)
    out["INV_TRINKETS"] = "{%s}" % ", ".join(piece("Inv/Equipment/Trinket%d" % i, ip) for i in range(1, TRINKETS + 1))
    out["INV_TRINKET_BOX"] = "{%s}" % ", ".join(box("Inv/Equipment/Trinket%d" % i) for i in range(1, TRINKETS + 1))
    out["INV_TRINKET_TEXT"] = tx("Inv/Equipment/Trinket1/T1 Amount", by[ROOT + "Inv/Equipment/Trinket1"])
    out["INV_TRINKET_BB"] = piece("Inv/trinket_backboard", ip)
    out["INV_DIVIDERS"] = "{%s, %s}" % (turned("Inv/Divider L", ip), turned("Inv/Divider R", ip))
    out["INV_NAME"] = tx("Inv/Text Name", ip)
    out["INV_DESC"] = tx("Inv/Text Desc", ip)
    ui = lambda k: txt("UI", k, "DIALOGUE")
    uid = lambda k: txt("UI", k, "MSG")
    # (the fixed items' names and descriptions, by their state: Mask Shards none yet, 0..3, all; Vessel Fragments none
    # yet, 0..2, all; the nail; Vengeful Spirit; Focus; Geo)
    out["TXT_INV_HEART"] = "{%s}" % ", ".join("{%d, %d}" % (ui("INV_NAME_HEARTPIECE_" + h), uid("INV_DESC_HEARTPIECE_" + h))
                                             for h in ("NONE", "0", "1", "2", "3", "ALL"))
    out["TXT_INV_VESSEL"] = "{%s}" % ", ".join("{%d, %d}" % (ui("INV_NAME_SOULORBS_" + v), uid("INV_DESC_SOULORBS_" + v))
                                              for v in ("NONE", "0", "1", "2", "ALL"))
    out["TXT_INV_FIXED"] = "{%s}" % ", ".join("{%d, %d}" % (ui("INV_NAME_" + k), uid("INV_DESC_" + k))
                                             for k in ("NAIL1", "SPELL_FIREBALL1", "SPELL_FOCUS", "GEO"))
    out["TXT_INV_EQ"] = "{%s}" % ", ".join("{%d, %d}" % (ui("INV_NAME_" + k), uid("INV_DESC_" + k)) for _, k, _ in EQUIPMENT)
    out["TXT_INV_QUILL"] = "{%d, %d, %d, %d}" % (ui("INV_NAME_QUILL"), uid("INV_DESC_QUILL"), ui("INV_NAME_MAPQUILL"),
                                                 uid("INV_DESC_MAPQUILL"))
    out["TXT_INV_TRINKETS"] = "{%s}" % ", ".join("{%d, %d}" % (ui("INV_NAME_TRINKET%d" % i), uid("INV_DESC_TRINKET%d" % i))
                                                for i in range(1, TRINKETS + 1))
    texts.add("0123456789", "MSG")
    return out
