"""The title screen, the save profiles and the pause menu (Menu_Title's UI canvas): where its pieces are, in HUD units
from the screen's center, and the sprites they show (src/menu.c)."""
import json, os
import unity

CANVAS = (1920.0, 1200.0)   # (CanvasScaler: 1920 x 1080, Expand: a 16:10 screen's canvas is taller)
UNITS = 2 * 8.7107 / CANVAS[1]   # (HUD units a canvas unit: the canvas's height is the HUD camera's)


def rects():
    """Menu_Title's UI: per object path, its rect's center (canvas units from the canvas's center) and size, its scale
    (cached in tools/cache)."""
    p = os.path.join(unity.CACHE, "menu_rects.json")
    if os.path.exists(p):
        return json.load(open(p))
    lv = unity.asset_file("level%d" % unity.scene_index()["Menu_Title"])
    tr, gos = {}, {}
    for pid, o in lv.objects.items():
        if o.type.name in ("Transform", "RectTransform"):
            tr[pid] = o.read_typetree()
            tr[pid]["_rect"] = o.type.name == "RectTransform"
        elif o.type.name == "GameObject":
            gos[pid] = o.read_typetree()
    names = {d["m_GameObject"]["m_PathID"]: pid for pid, d in tr.items()}
    canvas = next(pid for pid, d in tr.items()
                  if unity.S(gos[d["m_GameObject"]["m_PathID"]]["m_Name"]) == "UICanvas")
    out, memo = {}, {}

    def rect(pid):
        """its rect in canvas units: center (x, y), size (w, h), accumulated scale"""
        if pid in memo:
            return memo[pid]
        d = tr[pid]
        if pid == canvas:
            r = (0.0, 0.0, CANVAS[0], CANVAS[1], 1.0)
        elif not d["_rect"] or d["m_Father"]["m_PathID"] not in tr:
            r = (0.0, 0.0, 0.0, 0.0, 1.0)
        else:
            px, py, pw, ph, ps = rect(d["m_Father"]["m_PathID"])
            amin, amax, piv = d["m_AnchorMin"], d["m_AnchorMax"], d["m_Pivot"]
            w = d["m_SizeDelta"]["x"] + (amax["x"] - amin["x"]) * pw
            h = d["m_SizeDelta"]["y"] + (amax["y"] - amin["y"]) * ph
            # (the pivot's place in the parent, from its own rect's center)
            ax = (amin["x"] + (amax["x"] - amin["x"]) * piv["x"] - 0.5) * pw + d["m_AnchoredPosition"]["x"]
            ay = (amin["y"] + (amax["y"] - amin["y"]) * piv["y"] - 0.5) * ph + d["m_AnchoredPosition"]["y"]
            cx, cy = ax + (0.5 - piv["x"]) * w, ay + (0.5 - piv["y"]) * h
            s = d["m_LocalScale"]["x"]
            # (in the parent's scale; its own scales its rect about its pivot)
            r = (px + (ax + (cx - ax) * s) * ps, py + (ay + (cy - ay) * s) * ps, w, h, ps * s)
        memo[pid] = r
        return r

    def path(gp):
        t = tr[names[gp]]
        f = t["m_Father"]["m_PathID"]
        up = path(tr[f]["m_GameObject"]["m_PathID"]) + "/" if f in tr else ""
        return up + unity.S(gos[gp]["m_Name"])
    for gp in gos:
        if gp not in names:
            continue
        pt = path(gp)
        if pt.startswith("_UIManager/UICanvas/"):
            out[pt[len("_UIManager/UICanvas/"):]] = rect(names[gp])
    json.dump(out, open(p, "w"))
    return out


def place(r):
    """a rect -> its center (HUD units from the screen's center), its size (HUD units, with its scale)"""
    x, y, w, h, s = r
    return x * UNITS, y * UNITS, w * s * UNITS, h * s * UNITS


# (the pieces: an object's Image (or a sprite of the scene's) fitted to its rect)
SLOT = "SaveProfileScreen/Content/SaveSlots/Slot%s"
ACTIVE = SLOT + "/ActiveSaveSlot%d"
ZONES = {2: "KINGS_PASS", 4: "TOWN", 5: "CROSSROADS", 6: "GREEN_PATH", 22: "SHAMAN_TEMPLE"}   # (GlobalEnums.MapZone)
# (the second part's, Hollow Knight: Fungal Wastes; the first part names them, without their backgrounds)
ZONES_FW = {9: "WASTES", 24: "QUEENS_STATION", 35: "MANTIS_VILLAGE"}
ZONES_ALL = {**ZONES, **ZONES_FW}
MENU_K = 1.6   # (the menus' text, bigger: the game's is hard to read at this screen's size)


def build(sprites, texts):
    """The menus' sprites and texts registered -> {define: value} for data.h (src/menu.c)"""
    import text, scene, actors
    d = unity.scene("Menu_Title")
    by = {o["path"].replace("_UIManager/UICanvas/", ""): o for o in d["objects"]}
    r = rects()
    res = actors.HUD_K / actors.K0
    out = {}
    U = lambda v: "%.4ff" % v

    def image(path):
        o = by[path]
        v = next(c["v"] for c in o["c"] if isinstance(c.get("v"), dict) and "m_Sprite" in c["v"])
        return v["m_Sprite"], bool(v.get("m_PreserveAspect"))

    def piece(ref, w, h, preserve=True):
        """a sprite drawn in a w x h rect (HUD units) -> its record: sprite, its pivot from the rect's center, its
        scale as drawn"""
        s = unity.sprite(d["level"], list(d["externals"]), ref[0], ref[1])
        wu, hu = s.w / s.ppu, s.h / s.ppu
        k = min(w / wu, h / hu) if preserve else min(w / wu, h / hu)
        sx, sy = (1.0, 1.0) if preserve else (w / wu / k, h / hu / k)
        W, H = wu * k * sx, hu * k * sy
        spr = sprites.id(d, ref, k, res)
        return "{%d, %s, %s, %s, %s}" % (spr, U((s.px - 0.5) * W), U((s.py - 0.5) * H), U(sx), U(sy))

    def at(path):
        x, y, w, h = place(r[path])
        return x, y, w, h

    def obj_piece(path, preserve=None, k=1.0, frame_of=None):
        """(frame_of: the sprite another object shows, its animation's last frame where this one shows its first)"""
        ref, pa = image(frame_of or path)
        x, y, w, h = at(path)
        return piece(ref, abs(w) * k, abs(h) * k, pa if preserve is None else preserve)
    # the title: the logo (its world sprite as the menu's camera shows it, over the buttons), its buttons
    lw = 0.52 * 320 / actors.HUD_K
    logo = unity.sprite(d["level"], list(d["externals"]), 2, 110)
    out["MENU_LOGO"] = piece([2, 110], lw, lw * logo.h / logo.w)
    out["MENU_LOGO_Y"] = U(100 / actors.HUD_K - 0.29 * 200 / actors.HUD_K)
    btn = "MainMenuScreen/MainMenuButtons/%sButton"
    out["MENU_START_Y"] = U(at(btn % "StartGame")[1])
    out["MENU_HELP_Y"] = U(at(btn % "Options")[1])   # (How to Play, where Options is)
    out["MENU_QUIT_Y"] = U(at(btn % "Achievements")[1])   # (the third of the buttons shown)
    cx, cy, cw, ch = at(btn % "StartGame" + "/Text/CursorLeft")
    tx, ty, tw, th = at(btn % "StartGame" + "/Text")
    # (the pointers as big as the text is made)
    out["MENU_POINTER"] = obj_piece(btn % "StartGame" + "/Text/CursorLeft", False, MENU_K)
    # (from the text's edge to the pointer's center: the game's gap, then half the pointer as drawn)
    out["MENU_POINTER_GAP"] = U((tx - abs(tw) / 2) - (cx + abs(cw) / 2) + abs(cw) * MENU_K / 2)
    # the save profiles: the title and its fleur, the slots (one's pieces, from its center), Back
    out["MENU_PROFILES_TITLE_Y"] = U(at("SaveProfileScreen/Title")[1])
    out["MENU_PROFILES_FLEUR"] = obj_piece("SaveProfileScreen/TopFleur")
    out["MENU_PROFILES_FLEUR_Y"] = U(at("SaveProfileScreen/TopFleur")[1])
    s1x, s1y, _, _ = at(SLOT % "One")
    out["MENU_SLOT_X"], out["MENU_SLOT_Y"] = U(s1x), U(s1y)
    out["MENU_SLOT_DY"] = U(at(SLOT % "Two")[1] - s1y)
    rel = lambda path: (at(path)[0] - s1x, at(path)[1] - s1y)
    bgx, bgy, bgw, bgh = at(SLOT % "One" + "/Background")
    zones = []
    part = int(os.environ.get("HK_PART") or 1)
    for z in sorted(ZONES_ALL):
        if part == 1 and z in ZONES_FW:
            zones.append("{%d, {-1, 0, 0, 1, 1}}" % z)
            continue
        ref = next(a["backgroundImage"] for a in next(c["v"] for c in by["SaveProfileScreen/Content/SaveSlots"]["c"]
                                                       if c.get("class") == "SaveSlotBackgrounds")["areaBackgrounds"]
                   if a["areaName"] == z)
        zones.append("{%d, %s}" % (z, piece(ref, abs(bgw), abs(bgh))))
    out["MENU_SLOT_ZONES"] = "{%s}" % ", ".join(zones)
    out["MENU_SLOT_NZONES"] = len(zones)
    out["MENU_SLOT_BG_DY"] = U(bgy - s1y)
    for name, path in (("FLEUR", "/Fleur"), ("SELECTOR", "/Selector"), ("CURSOR", "/CursorLeft"),
                       ("ORB", "/ActiveSaveSlot1/HUDLayout/SoulOrb"), ("GEO", "/ActiveSaveSlot1/HUDLayout/GeoIcon"),
                       ("HEALTH", "/ActiveSaveSlot1/HUDLayout/HealthSlots/HealthUnit1")):
        p = SLOT % "One" + path
        out["MENU_SLOT_" + name] = obj_piece(p, False if name == "CURSOR" else None, MENU_K if name == "CURSOR" else 1.0)
        x, y = rel(p)
        out["MENU_SLOT_%s_X" % name], out["MENU_SLOT_%s_Y" % name] = U(x), U(y)
    out["MENU_SLOT_CURSOR_X2"] = U(rel(SLOT % "One" + "/CursorRight")[0])
    out["MENU_SLOT_HEALTH_DX"] = U(rel(SLOT % "One" + "/ActiveSaveSlot1/HUDLayout/HealthSlots/HealthUnit2")[0] -
                                   rel(SLOT % "One" + "/ActiveSaveSlot1/HUDLayout/HealthSlots/HealthUnit1")[0])

    def edge(path, align):
        """a text's anchor (x: its left, center or right, as aligned; y: its middle) from the slot's center"""
        x, y, w, h = at(path)
        x -= s1x
        return U(x - abs(w) / 2 if align == 3 else x + abs(w) / 2 if align == 5 else x), U(y - s1y)
    out["MENU_SLOT_NUM_X"], _ = edge(SLOT % "One" + "/SlotNumberText", 4)
    out["MENU_SLOT_NEW_X"], _ = edge(SLOT % "One" + "/NewGameText", 3)
    out["MENU_SLOT_LOC_X"], out["MENU_SLOT_LOC_Y"] = edge(ACTIVE % ("One", 1) + "/LocationText", 5)
    out["MENU_SLOT_TIME_X"], out["MENU_SLOT_TIME_Y"] = edge(ACTIVE % ("One", 1) + "/PlayTimeText", 5)
    out["MENU_SLOT_GEOTXT_X"], out["MENU_SLOT_GEOTXT_Y"] = edge(ACTIVE % ("One", 1) + "/HUDLayout/GeoText", 3)
    out["MENU_SLOT_CLEAR_X"] = U(at("SaveProfileScreen/Content/ClearSaveButtons/ClearSaveButtonOne")[0] - s1x)
    pr = "SaveProfileScreen/Content/ClearSavePrompts/ClearSavePromptOne"
    out["MENU_SLOT_PROMPT_X"] = U(at(pr + "/ClearSaveText")[0] - s1x)
    out["MENU_SLOT_YES_X"] = U(at(pr + "/YesButton")[0] - s1x)
    out["MENU_SLOT_NO_X"] = U(at(pr + "/NoButton")[0] - s1x)
    out["MENU_BACK_Y"] = U(at("SaveProfileScreen/Controls/BackButton")[1])
    # the pause menu: its fleurs, Continue and Quit to Menu (between them, Options' place shared)
    out["MENU_PAUSE_TOP"] = obj_piece("PauseMenuScreen/TopFleur", frame_of="NewPauseMenuScreen/TopFleur")
    out["MENU_PAUSE_TOP_Y"] = U(at("PauseMenuScreen/TopFleur")[1])
    out["MENU_PAUSE_BOT"] = obj_piece("PauseMenuScreen/BottomFleur", frame_of="NewPauseMenuScreen/BottomFleur")
    out["MENU_PAUSE_BOT_Y"] = U(at("PauseMenuScreen/BottomFleur")[1])
    yc, yo, yq = (at("PauseMenuScreen/Controls/%sButton" % b)[1] for b in ("Continue", "Options", "Quit"))
    out["MENU_PAUSE_CONTINUE_Y"], out["MENU_PAUSE_HELP_Y"], out["MENU_PAUSE_QUIT_Y"] = U(yc), U(yo), U(yq)
    # how to play: the keyboard (the Options' Keyboard screen: its title, its fleur, its rows, its buttons' place), then
    # the basics, as that screen
    kb = "KeyboardMenuScreen/"
    out["MENU_KEYS_TITLE_Y"] = U(at(kb + "Title")[1])
    out["MENU_KEYS_FLEUR"] = obj_piece(kb + "TopFleur")
    out["MENU_KEYS_FLEUR_Y"] = U(at(kb + "TopFleur")[1])
    y0, y1 = at(kb + "Content/MappableKeys/UpButton")[1], at(kb + "Content/MappableKeys/DownButton")[1]
    out["MENU_KEYS_ROW_Y"], out["MENU_KEYS_DY"] = U(y0), U(y1 - y0)
    out["MENU_KEYS_BUTTON_Y"] = U(at(kb + "Controls/ApplyButton")[1])
    # the texts
    sh = text.sheets()
    mm = lambda k, st="MENU": texts.add(text.clean(sh["MainMenu"][k]), st)
    for k in ("MAIN_START", "MAIN_QUIT", "PROFILE_NEW_GAME", "PROFILE_CLEAR_BUTTON", "PROFILE_CLEAR_PROMPT", "NAV_YES",
              "NAV_NO", "NAV_BACK", "PAUSE_CONTINUE", "PAUSE_MAIN", "PROFILE_CORRUPTED", "PROFILE_LOADING"):
        out["TXT_" + k] = mm(k)
    out["TXT_SCREEN_SAVE_PROFILES"] = mm("SCREEN_SAVE_PROFILES", "MENU_TITLE")
    out["TXT_SCREEN_KEYBOARD"] = mm("SCREEN_KEYBOARD", "MENU_TITLE")
    # how to play: the game's names for what the keys do, then the calculator's keys
    keys = [(mm("BUTTON_MOVE", "MENU_SMALL"), "left, right"), (mm("BUTTON_LOOK", "MENU_SMALL"), "up, down"),
            (mm("BUTTON_JUMP", "MENU_SMALL"), "OK"), (mm("BUTTON_ATTACK", "MENU_SMALL"), "back"),
            (mm("BUTTON_DASH", "MENU_SMALL"), "shift"), (mm("BUTTON_CAST", "MENU_SMALL"), "alpha"),
            (texts.add("Listen / Rest / Enter", "MENU_SMALL"), "up"), (mm("BUTTON_MAP", "MENU_SMALL"), "var"),
            (mm("BUTTON_INVENTORY", "MENU_SMALL"), "toolbox"), (mm("BUTTON_PAUSE", "MENU_SMALL"), "backspace"),
            (texts.add("Save and Quit", "MENU_SMALL"), "home")]
    out["MENU_KEYS"] = "{%s}" % ", ".join("{%d, %d}" % (t, texts.add(k, "MENU_SMALL")) for t, k in keys)
    out["MENU_NKEYS"] = len(keys)
    out["TXT_HELP"] = texts.add("How to Play", "MENU")
    out["TXT_HELP_TITLE"] = texts.add("How to Play", "MENU_TITLE")
    out["TXT_NEXT"] = texts.add("Next", "MENU")
    out["TXT_HELP_LINES"] = texts.add("Strike enemies with your nail to gather Soul.\n"
                                      "Hold alpha to focus Soul and heal your masks.\n"
                                      "Rest at a bench to heal, save and change charms.\n"
                                      "If you fall, your shade keeps your geo:\n"
                                      "strike it down to win them back.\n"
                                      "Spend geo in Dirtmouth's shops, and find\n"
                                      "Cornifer in the Crossroads for a map.", "TUTE")
    out["TXT_ZONES"] = "{%s}" % ", ".join("%d" % texts.add(text.clean(sh["Map Zones"][ZONES_ALL[z]]), "MENU_SMALL")
                                          for z in sorted(ZONES_ALL))
    # (the parts of Hallownest, each an app: the way on from one into another, a new game in the first only)
    out["TXT_CROSS_TO"] = texts.add("The way on is in", "MENU")
    out["TXT_CROSS_APPS"] = "{%s}" % ", ".join(str(texts.add(n, "MENU")) for n in ("Hollow Knight", "Hollow Knight: Fungal Wastes"))
    out["CROSS_NAPPS"] = 2
    out["TXT_CROSS_SAVED"] = texts.add("Saved. Install it to go on.", "MENU_SMALL")
    out["TXT_CROSS_NEW"] = texts.add("Begins in Hollow Knight", "MENU_SMALL")
    # (the characters the numbers need: slots, geo, play time)
    texts.add("1234.", "MENU_TITLE")
    texts.add("0123456789hm", "MENU_SMALL")
    return out
