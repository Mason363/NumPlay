"""The game's own scripts (PlayMaker FSMs) of the characters and set pieces this port runs as they are: compiled to a
small bytecode that src/vm.c interprets. A room's VM data: the objects the scripts use (their places, sprites, triggers,
children), then the FSMs (each a definition, shared by those made from the same template, and its variables' values).

Values are 32-bit slots: a definition's variables first (in RAM), then its constants (in the data). Objects are numbers:
the room's VM objects, or the game's own (the Knight, the dialogue box, the area title...)."""
import struct
import numpy as np
import unity, text, state

# rooms -> the objects whose scripts run here (and all under them)
ROOMS = {
    "Town": ["_NPCs/Elderbug", "_NPCs/Tiso Town NPC", "_Areas/Death Respawn Trigger", "_Areas/Death Respawn Trigger 1",
             "_Props/Mappers_house/closed/Inspect Region", "Interact Reminder"],
    "Room_temple": ["Quirrel"],
    "Crossroads_47": ["_NPCs/Tiso Bench NPC", "Stag", "_Scenery/Station Bell", "Stag_Tunnel/Stag_tunnel_Grate",
                      "Stag Blanker"],
    "Fungus1_16_alt": ["Stag", "Station Bell", "Stag_tunnel_Grate", "Stag Blanker"],
    "Room_Town_Stag_Station": ["Stag", "Station Bell", "Stag_Tunnel/Stag_tunnel_Grate", "Stag Blanker", "Stag Lift",
                               "Station Door", "Gate Switch"],
    "Room_ruinhouse": ["Sly Dazed"],
    "Crossroads_06": ["Set NPC Leave", "_Scenery/Raising Pillar", "_Props/Gate Switch"],
    "Fungus1_04": ["Hornet Infected Knight Encounter", "Hornet Saver", "Cloak Corpse", "Dream Scene Activate",
                   "Dreamer Scene 1", "Break Floor 1", "Camera Locks Boss"],
    "Crossroads_ShamanTemple": ["_Props/Shaman Meeting", "_Props/Shaman Trapped", "_Props/Shaman Killed Blocker",
                                "_Props/Knight Get Fireball", "Battle Scene/Reminder Cast", "Shiny Item", "Soul Totem 2",
                                "Bone Gate", "Reminder Cast (1)",
                                "_Areas/Death Respawn Trigger 1"],
    # items lying about (Shiny Item: a relic, a charm), the City Crest the False Knight leaves
    "Crossroads_01": ["Shiny Item", "Force Hard Landing", "_Scenery/sign_post_03/Crossroads Sign Post"],
    "Tutorial_01": ["_Props/Chest/Item", "_Props/Collapser Tute 01", "_Scenery/Break Floor 1", "Interact Reminder",
                    "_Props/Tute Door 5/Active/Jump Reminder", "_Props/Tut_tablet_top", "_Props/Tut_tablet_top (1)",
                    "_Props/Tut_tablet_top (2)", "Initial Fall Impact"],   # (its chest: src/obj.c, which turns on what is in it)
    "Fungus1_22": ["Shiny Item", "Gate Switch", "Metal Gate", "Breakable Wall"],
    # (Fk Break Wall's Escape Checker finds no False Knight: its scene loads after it looks, as SceneLoad's LoadBoss)
    "Crossroads_10": ["Key Giver", "Breakable Wall", "Fk Break Wall"],
    # the shops: their keepers, their regions (the menu: src/shop.c)
    "Room_shop": ["Basement Closed"],
    "Room_mapper": ["Iselda", "Shop Region"],
    "Room_Charm_Shop": ["Charm Slug", "Shop Region", "Scene Blanker"],
    # the grubs in their jars; the Grubfather, his rewards, the grubs back home
    "Crossroads_03": ["_Props/Grub Bottle", "_Props/Toll Gate", "_Props/Toll Gate 1", "_Props/Toll Gate Switch",
                      "Break Wall 2"],
    "Fungus1_21": ["Grub Bottle", "Inspect Region"],
    "Crossroads_38": ["Grub King"] + ["Saved Grubs/Grub Saved %d" % n for n in range(1, 3)],
    # Hornet seen in Greenpath before her arena
    "Fungus1_02": ["Hornet Encounter GP1"],
    "Fungus1_03": ["Set Hornet Encounter"],
    "Fungus1_17": ["Set Hornet Encounter", "Inspect Region"],
    "Fungus1_31": ["Hornet Encounter Control", "_Props/Breakable Wall", "Toll Gate Machine", "Toll Gate Machine (1)",
                   "Toll Gate", "Toll Gate (1)", "Gate Saver"],
    # the world's other scripted things: walls that break, floors, a soul totem, tablets, a wall that turns on
    "Crossroads_08": ["Break Wall 2"],
    "Crossroads_04": ["_Scenery/Break Floor 1", "CamLock Destroyer", "_Transition Gates/Mender Door"],
    "Crossroads_19": ["Soul Totem mini_two_horned"],
    "Crossroads_21": ["Breakable Wall", "Polygon_Collider_Cross_21 1/Roof Collider (1)", "Collapser Small",
                      "Map Scene Region"],
    "Crossroads_07": ["Breakable Wall_Silhouette"],
    "Crossroads_30": ["Spa Region"],
    "Crossroads_33": ["_Props/full_wall_left", "Cornifer", "Cornifer Card"],
    "Fungus1_32": ["Breakable Wall", "Inspect Region"],
    "Crossroads_11_alt": ["Inspect Region"],
}
def _piece(doc, q, rigid=True):
    """A piece of debris (a sprite with a Rigidbody2D: how it falls, bounces and spins) -> FlingPiece's operands."""
    import scene
    sr = next((c["v"] for c in q["c"] if c["type"] == "SpriteRenderer" and (c.get("v") or {}).get("m_Sprite")), None)
    rb = next((c.get("v") or {} for c in q["c"] if c["type"] == "Rigidbody2D"), None)
    if sr is None or (rigid and rb is None):
        return None
    m = np.array(q["m3"]).reshape(3, 3)
    sx, sy = float(np.hypot(m[0, 0], m[1, 0])), float(np.hypot(m[0, 1], m[1, 1]))
    mirror = -1.0 if np.linalg.det(m[:2, :2]) < 0 else 1.0
    rot = float(np.degrees(np.arctan2(m[1, 0], m[0, 0]))) if mirror > 0 else float(np.degrees(np.arctan2(-m[1, 0], -m[0, 0])))
    ob = next((c.get("v") or {} for c in q["c"] if c.get("class") == "ObjectBounce"), None)
    sp = next((c.get("v") or {} for c in q["c"] if c.get("class") == "SpinSelf"), None)
    ss = next((c.get("v") or {} for c in q["c"] if c.get("class") == "SpinSelfSimple"), None)
    sf = next((c["fsm"] for c in q["c"] if c.get("fsm") and _spins(c["fsm"])), None)
    if ss is None and sf is not None:
        # (a piece's FSM that spins it: as SpinSelfSimple)
        ss = {"spinFactor": sf["vars"]["Spin Factor"][1], "randomStartRotation": sf["vars"]["Random Start Rotation"][1]}
    # (SpinSelf: a push of its speed times its factor, turned at random first; SpinSelfSimple: against its speed)
    spin, flags = (sp.get("spinFactor", -7.5), 3) if sp is not None else (-ss.get("spinFactor", 0), 1 | (2 if ss.get("randomStartRotation") else 0)) if ss is not None else (0, 0)
    return {"sprite": PIECE_SPRITES(doc, sr["m_Sprite"], max(sx, sy), world=True), "layer": scene.layer_index(sr.get("m_SortingLayerID", 0)),
            "order": (sr.get("m_SortingOrder", 0) + 32768) & 0xFFFF, "flags": flags, "z": q["pos"][2], "rot": rot,
            "gravity": (rb or {}).get("m_GravityScale", 1), "bounce": ob.get("bounceFactor", 0) if ob else -1,
            "spin": spin, "mirror": mirror}
PIECE_SPRITES = None   # (prepare: the sprites' id maker)
ROOM_NAMES = []        # (prepare: the rooms, in their order)


def _fsm(name, start, vars_, states):
    """an FSM written here: states as (name, [(action, {params})], {event: state})"""
    act = lambda a, p: {"name": a, "enabled": True, "params": list(p.items())}
    evs = sorted({e for _, _, t in states for e in t})
    return {"name": name, "start": start, "vars": vars_, "global": [], "events": evs,
            "states": [{"name": n, "actions": [act(a, p) for a, p in acts], "transitions": [[e, t] for e, t in tr.items()]}
                       for n, acts, tr in states]}


def _behaviours(rm, o):
    """The game's own components that are scripts here (as the FSMs they would be)."""
    out = []
    rt = next((c.get("v") for c in o["c"] if c.get("class") == "RespawnTrigger" and c.get("v")), None)
    if rt is not None and isinstance(rt.get("respawnMarker"), list):
        # RespawnTrigger: the Knight coming into it makes its marker where he comes back from death (single use: once,
        # Activated kept by its PersistentBoolItem)
        import pack
        pid = rt["respawnMarker"][1]
        mk = next(q for q in rm.d["objects"] for c in q["c"] if c.get("pid") == pid)
        right = next(c.get("v") or {} for c in mk["c"] if c.get("pid") == pid).get("respawnFacingRight", 0)
        single = bool(rt.get("singleUse"))
        out.append({"fsm": _fsm("Respawn Trigger", "Init", {"Activated": ["bool", False]}, [
            ("Init", [("BoolTest", {"boolVariable": "$Activated", "isTrue": ["event", "DONE"], "isFalse": None,
                                    "everyFrame": False})], {"FINISHED": "Idle", "DONE": "Done"}),
            ("Idle", [("Trigger2dEvent", {"trigger": 0, "collideTag": "Player", "collideLayer": "",
                                          "sendEvent": ["event", "ENTER"], "storeCollider": None})], {"ENTER": "Set"}),
            ("Set", [("SetBenchRespawn", {"marker": pack.STRINGS.id(mk["name"]), "type": rt.get("respawnType", 0),
                                          "facing": 1 if right else 0}),
                     ("SetBoolValue", {"boolVariable": "$Activated", "boolValue": single, "everyFrame": False})],
             {"FINISHED": "Done" if single else "Idle"}),
            ("Done", [], {})])})
    g = next((c.get("v") for c in o["c"] if c.get("class") == "GrubBGControl" and c.get("v")), None)
    if g is not None:
        # GrubBGControl: a grub back home, there once that many are saved
        out.append({"fsm": _fsm("Grub BG", "Init", {"N": ["int", 0]}, [
            ("Init", [("GetPlayerDataInt", {"intName": "grubsCollected", "storeValue": "$N"}),
                      ("IntCompare", {"integer1": "$N", "integer2": g["grubNumber"], "lessThan": ["event", "HIDE"],
                                      "equal": None, "greaterThan": None, "everyFrame": False})], {"HIDE": "Hide"}),
            ("Hide", [("ActivateGameObject", {"gameObject": "Owner", "activate": False, "recursive": False,
                                              "resetOnExit": False, "everyFrame": False})], {})])})
    p = rm.by_id.get(o.get("parent"))
    if o["name"] == "Wave Region" and p is not None and any(c.get("class") == "GrubBGControl" for c in p["c"]):
        # (its wave as the Knight comes into its region; then its bounce again)
        out.append({"fsm": _fsm("Grub BG Wave", "Init", {"Parent": ["gameObject", None]}, [
            ("Init", [("GetParent", {"gameObject": "Owner", "storeResult": "$Parent"})], {"FINISHED": "Idle"}),
            ("Idle", [("Tk2dPlayAnimation", {"gameObject": "$Parent", "animLibName": "", "clipName": "Home Bounce"}),
                      ("Trigger2dEvent", {"trigger": 0, "collideTag": "", "collideLayer": "", "sendEvent": ["event", "WAVE"],
                                          "storeCollider": None})], {"WAVE": "Wave"}),
            ("Wave", [("Tk2dPlayAnimationWithEvents", {"gameObject": "$Parent", "clipName": "Home Wave",
                                                       "animationTriggerEvent": None,
                                                       "animationCompleteEvent": ["event", "FINISHED"]})],
             {"FINISHED": "Idle"})])})
    return out


# FSMs left out (what this port does not have: the dream nail, sounds...)
SKIP_FSMS = {"npc_dream_dialogue", "Dream Dialogue", "Rotate", "Shop Open Voice", "Enviro Region", "tink_effect",
             "RespawnTriggerFSM"}


def _spins(f):
    """A piece's FSM that spins it against its speed (as SpinSelfSimple: the piece's, src/obj.c)."""
    return f["name"] == "FSM" and {s["name"] for s in f["states"]} == {"Spin At Start?", "Wait", "Randomise z", "Spin"}


def _tinks(f):
    """An enemy's old health FSM, Invincible: all a hit does is its Blocked Hit (as tink_effect's)."""
    return f["name"] == "FSM" and f["vars"].get("Invincible") == ["bool", True] and \
        any(s["name"] == "Blocked Hit" for s in f["states"])


# objects left out (effects drawn by the C code, or nothing at all; the dream nail's; Dreamer Scene 1's Knight Lift,
# which nothing turns on)
SKIP_CLASSES = {"SpellGetOrb", "ParticleSystem"}
SKIP_NAMES = {"Dream Dialogue", "Dream Dialogue Flower", "Flower", "Flower Give", "white_light", "white_light 1",
              "Knight Lift"}
# (the Mender House's door: its inspect region, for a key there is none of, never on; the False Knight's arena wall's
# battle gate: src/obj.c's, which FindChild finds)
SKIP_PATHS = {("Crossroads_04", "_Transition Gates/Mender Door/Inspect"), ("Crossroads_10", "Fk Break Wall/Battle Gate 1")}
# levers pulled from the start (their PersistentBoolItem's Activated on): Crossroads_03's toll switch opens, from below, the
# tollbooth Crossroads_16 drops into; the game's Knight comes up to it first by rooms this port leaves out, so here the
# tollbooth is the way on to Crossroads_21 and the False Knight
PULLED = {("Crossroads_03", "_Props/Toll Gate Switch")}
MAX_OBJS, MAX_FSMS, MAX_VARS, MAX_ANIMS = 48, 29, 232, 20   # (src/vm.c)
# map pieces of their own a script marks visited (AddToScenesVisited), as their room's (src/map.c: after the rooms)
MAP_SCENES = {"Crossroads_21_b": "Crossroads_21"}
# the grubs in this part of the game (Crossroads_03, Fungus1_21): the Grubfather's rewards past them never come
GRUBS = 2
# the ints ConvertIntToString turns into strings here (the Grubfather's rewards given)
INT_STRINGS = range(0, GRUBS + 1)

# the game's own objects
SPECIAL = {"Hero": 0xFF00, "HeroLight": 0xFF01, "DialogueManager": 0xFF02, "DialogueText": 0xFF03, "AreaTitle": 0xFF04,
           "CameraParent": 0xFF05, "MainCamera": 0xFF06, "GameManager": 0xFF07, "HUD Blanker": 0xFF08,
           "DialogueTextYN": 0xFF09, "UIManager": 0xFF0A, "Enemy Dream Msg": 0xFF0B, "HUD Blanker White": 0xFF0D}
# a scene's objects that are the HUD's (drawn by the C code: msg.c's focus prompt; src/vm.c: O_FOCUS_PROMPT), by name
HUD_CHILDREN = {"Focus_prompt_temp": 0xFF12}
# objects scripts find by their tags: the Knight; a shop's menu (src/shop.c)
TAGGED = {"Player": 0xFF00, "Shop Window": 0xFF10}   # (src/vm.c: O_SHOP)
O_NONE = 0xFFFF
# PlayerData ints (src/vm.c: pd_int)
PD_INTS = ["MPCharge", "health", "maxHealth", "geo", "fireballLevel", "quakeLevel", "screamLevel", "shaman", "elderbug",
           "permadeathMode", "nailDamage", "hornetGreenpath", "quirrelEggTemple", "charmsOwned", "trinket1", "trinket2",
           "trinket3", "trinket4", "rancidEggs", "ore", "grubsCollected", "grubRewards", "stagPosition", "stationsOpened",
           "xunFlowerBrokeTimes", "menderState"]
# PlayerData bools kept elsewhere (src/vm.c)
PD_SPECIAL = {"disablePause": 0xFFF0, "hasSpell": 0xFFF1, "canDash": 0xFFF2}
# PlayerData bools that keep their new game value all through this part of the game
PD_CONST = {"openedTown": True, "kingsStationNonDisplay": False, "queensStationNonDisplay": False,
            "stagEggInspected": False, "stagHopeConvo": False, "openedStagNest": False, "stagRemember2": False,
            "stagRemember3": False, "stagConvoTram": False,
            "gotSlyCharm": False, "hasAllNailArts": False, "hasNailArt": False, "honedNail": False,
            "iseldaConvoGrimm": False, "iseldaNymmConvo": False, "slyConvoGrimm": False, "slyNymmConvo": False,
            "slyConvoNailArt": False, "slyConvoNailHoned": False, "backerCredits": False, "finalGrubRewardCollected": False, "gaveSlykey": False, "hasSlykey": False,
            "corn_greenpathEncountered": False, "corn_fogCanyonEncountered": False,
            "corn_fungalWastesEncountered": False, "corn_cityEncountered": False, "corn_waterwaysEncountered": False,
            "corn_minesEncountered": False, "corn_deepnestEncountered": False, "corn_cliffsEncountered": False,
            "corn_fogCanyonLeft": False, "corn_fungalWastesLeft": False, "corn_cityLeft": False,
            "corn_waterwaysLeft": False, "corn_minesLeft": False, "corn_cliffsLeft": False, "corn_deepnestLeft": False,
            "corn_outskirtsLeft": False, "corn_royalGardensLeft": False, "corn_abyssLeft": False,
            "visitedRestingGrounds": False, "hasTramPass": False, "equippedCharm_10": False, "elderbugGaveFlower": False, "elderbugRequestedFlower": False,
            "xunFlowerBroken": False, "hasXunFlower": False, "openedBlackEggDoor": False,
            "visitedCrossroadsInfected": False, "defeatedNightmareGrimm": False, "jijiDoorUnlocked": False,
            "visitedCliffs": False, "mineLiftOpened": False, "brettaRescued": False, "elderbugHistory2": False,
            "shamanFireball2Convo": False, "shamanScreamConvo": False, "shamanScream2Convo": False,
            "shamanQuakeConvo": False, "shamanQuake2Convo": False, "hasDreamNail": False,
            "equippedCharm_11": False, "dungDefenderEncounterReady": False, "elderbugSpeechBretta": False,
            "elderbugSpeechJiji": False, "elderbugSpeechKingsPass": False, "elderbugSpeechMinesLift": False,
            "elderbugSpeechInfectedCrossroads": False, "elderbugSpeechFinalBossDoor": False, "hasDoubleJump": False,
            "hasSuperDash": False, "hasWalljump": False, "mageLordDefeated": False, "elderbugConvoGrimm": False,
            "elderbugNymmConvo": False, "elderbugTroupeLeftConvo": False, "elderbugBrettaLeft": False,
            "hasAcidArmour": False}


class Strings:
    """Strings the scripts use as values (clip names, events, convo keys...): numbered."""
    def __init__(self):
        self.list, self.index = [], {}

    def id(self, s):
        if s not in self.index:
            self.index[s] = len(self.list)
            self.list.append(s)
        return self.index[s]

    def truncate(self, n):
        for s in self.list[n:]:
            del self.index[s]
        del self.list[n:]


STR = Strings()
EVENTS = Strings()
for e in ("FINISHED", "CONVO_FINISH", "CONVO START", "CONVO END", "BIG TITLE START", "BIG TITLE END", "HERO DAMAGED",
          "NPC TITLE DOWN", "NPC CONVO START", "BOX UP", "BOX DOWN", "LEAVING SCENE", "TAKE DAMAGE", "GET ITEM MSG END",
          "HORNET LEAVE", "BG CLOSE", "BG QUICK CLOSE", "BG OPEN", "BG QUICK OPEN", "BG DESTROY", "WAKE", "BOX UP DREAM",
          "BOX DOWN DREAM", "FADE IN", "FADE OUT", "FSM CANCEL", "CLOSE", "FK DEATH", "SHOP UP", "SHOP CLOSED",
          "SHOP CLOSED QUICK", "SHOP WINDOW UP", "RESET SHOP WINDOW", "CLOSE SHOP WINDOW", "BOX UP YN", "BOX DOWN YN",
          "YES", "NO", "CONTINUE", "RESET", "HIT", "UNCOVER", "UP", "DOWN", "FIRST MAP UP", "FIRST MAP DOWN",
          "FK RAGE COMPLETE"):
    EVENTS.id(e)
FIXED_EVENTS = len(EVENTS.list)   # (src/data.h: VMEV_*)
# events the game's code sends that only some rooms' scripts know, numbered as each room numbers its own (src/data.h:
# VMEV_*, by room): the Elder Baldur's corpse's, a shop's charm lesson's
NAMED_EVENTS = ("UPDATE SHAMAN", "SHOP REGION INACTIVE", "SHOP REGION ACTIVE")

# ---------------------------------------------------------------- the actions it runs: name -> (opcode, operands)
# operand kinds: f i b s o value slots (u16), F I B S O V output variables (u8: 255 none), e event (u8), t event target,
# p PlayerData bool (u16 flag; 0xFFFE false, 0xFFFD true), q PlayerData int (u8), x text (u16), n byte, k float,
# B* / b* lists
OPS = {}


def op(name, *params):
    OPS[name] = (len(OPS) + 1, params)


op("Nop")
op("Wait", ("time", "f"), ("finishEvent", "e"))
op("NextFrameEvent", ("sendEvent", "e"))
op("SendEvent", ("eventTarget", "t"), ("sendEvent", "e"), ("delay", "f"), ("everyFrame", "n"))
op("SendEventByName", ("eventTarget", "t"), ("sendEvent", "es"), ("delay", "f"), ("everyFrame", "n"))
op("SetBoolValue", ("boolVariable", "B"), ("boolValue", "b"), ("everyFrame", "n"))
op("SetFloatValue", ("floatVariable", "F"), ("floatValue", "f"), ("everyFrame", "n"))
op("SetIntValue", ("intVariable", "I"), ("intValue", "i"), ("everyFrame", "n"))
op("SetStringValue", ("stringVariable", "S"), ("stringValue", "s"), ("everyFrame", "n"))
op("SetGameObject", ("variable", "O"), ("gameObject", "o"), ("everyFrame", "n"))
op("GetOwner", ("storeGameObject", "O"))
op("GetHero", ("storeResult", "O"))
op("FindChild", ("gameObject", "o"), ("childName", "s"), ("storeResult", "O"))
op("GetParent", ("gameObject", "o"), ("storeResult", "O"))
op("FloatAdd", ("floatVariable", "F"), ("add", "f"), ("everyFrame", "n"), ("perSecond", "n"))
op("FloatSubtract", ("floatVariable", "F"), ("subtract", "f"), ("everyFrame", "n"), ("perSecond", "n"))
op("FloatMultiply", ("floatVariable", "F"), ("multiplyBy", "f"), ("everyFrame", "n"))
op("FloatCompare", ("float1", "f"), ("float2", "f"), ("tolerance", "f"), ("equal", "e"), ("lessThan", "e"),
   ("greaterThan", "e"), ("everyFrame", "n"))
op("FloatTestToBool", ("float1", "f"), ("float2", "f"), ("tolerance", "f"), ("equalBool", "B"), ("lessThanBool", "B"),
   ("greaterThanBool", "B"), ("everyFrame", "n"))
op("FloatInRange", ("floatVariable", "f"), ("lowerValue", "f"), ("upperValue", "f"), ("boolVariable", "B"),
   ("trueEvent", "e"), ("falseEvent", "e"), ("everyFrame", "n"))
op("IntCompare", ("integer1", "i"), ("integer2", "i"), ("equal", "e"), ("lessThan", "e"), ("greaterThan", "e"),
   ("everyFrame", "n"))
op("IntTestToBool", ("int1", "i"), ("int2", "i"), ("equalBool", "B"), ("lessThanBool", "B"), ("greaterThanBool", "B"),
   ("everyFrame", "n"))
op("IntCompareToBool", ("integer1", "i"), ("integer2", "i"), ("equalBool", "B"), ("lessThanBool", "B"),
   ("greaterThanBool", "B"), ("everyFrame", "n"))
op("IntAdd", ("intVariable", "I"), ("add", "i"), ("everyFrame", "n"))
op("IntSwitch", ("intVariable", "i"), ("compareTo", "i*"), ("sendEvent", "e*"), ("everyFrame", "n"))
op("FloatSwitch", ("floatVariable", "f"), ("lessThan", "f*"), ("sendEvent", "e*"), ("everyFrame", "n"))
op("BoolTest", ("boolVariable", "b"), ("isTrue", "e"), ("isFalse", "e"), ("everyFrame", "n"))
op("BoolAllTrue", ("boolVariables", "b*"), ("sendEvent", "e"), ("storeResult", "B"), ("everyFrame", "n"))
op("BoolAnyTrue", ("boolVariables", "b*"), ("sendEvent", "e"), ("storeResult", "B"), ("everyFrame", "n"))
op("BoolTestMulti", ("boolVariables", "b*"), ("boolStates", "b*"), ("trueEvent", "e"), ("falseEvent", "e"),
   ("storeResult", "B"), ("everyFrame", "n"))
op("StringCompare", ("stringVariable", "s"), ("compareTo", "s"), ("equalEvent", "e"), ("notEqualEvent", "e"),
   ("storeResult", "B"), ("everyFrame", "n"))
op("GetPlayerDataBool", ("boolName", "p"), ("storeValue", "B"))
op("SetPlayerDataBool", ("boolName", "p"), ("value", "b"))
op("PlayerDataBoolTest", ("boolName", "p"), ("isTrue", "e"), ("isFalse", "e"))
op("PlayerDataBoolTrueAndFalse", ("trueBool", "p"), ("falseBool", "p"), ("isTrue", "e"), ("isFalse", "e"))
op("GetPlayerDataInt", ("intName", "q"), ("storeValue", "I"))
op("SetPlayerDataInt", ("intName", "q"), ("value", "i"))
op("ActivateGameObject", ("gameObject", "o"), ("activate", "b"), ("recursive", "n"), ("resetOnExit", "n"),
   ("everyFrame", "n"))
op("SetSpriteRenderer", ("gameObject", "o"), ("active", "b"))
op("SetMeshRenderer", ("gameObject", "o"), ("active", "b"))
op("SetCollider", ("gameObject", "o"), ("active", "b"))
op("DestroyObject", ("gameObject", "o"), ("delay", "f"))
op("DestroySelf")
op("DestroyAllChildren", ("gameObject", "o"))
op("GetPosition", ("gameObject", "o"), ("vector", "V3"), ("x", "F"), ("y", "F"), ("z", "F"), ("space", "n"),
   ("everyFrame", "n"))
op("SetPosition", ("gameObject", "o"), ("vector", "v3"), ("x", "f"), ("y", "f"), ("z", "f"), ("space", "n"),
   ("everyFrame", "n"))
op("GetScale", ("gameObject", "o"), ("xScale", "F"), ("yScale", "F"))
op("SetScale", ("gameObject", "o"), ("x", "f"), ("y", "f"), ("everyFrame", "n"))
op("SetVelocity2d", ("gameObject", "o"), ("x", "f"), ("y", "f"), ("everyFrame", "n"))
op("SetGravity2dScale", ("gameObject", "o"), ("gravityScale", "f"))
op("CheckTargetDirection", ("gameObject", "o"), ("target", "o"), ("aboveEvent", "e"), ("belowEvent", "e"),
   ("rightEvent", "e"), ("leftEvent", "e"), ("everyFrame", "n"))
op("FaceObject", ("objectA", "o"), ("objectB", "o"), ("spriteFacesRight", "n"), ("playNewAnimation", "n"),
   ("newAnimationClip", "s"), ("resetFrame", "n"), ("everyFrame", "n"))
op("Tk2dPlayAnimation", ("gameObject", "o"), ("clipName", "s"))
op("Tk2dPlayAnimationWithEvents", ("gameObject", "o"), ("clipName", "s"), ("animationTriggerEvent", "e"),
   ("animationCompleteEvent", "e"))
op("Tk2dWatchAnimationEvents", ("gameObject", "o"), ("animationTriggerEvent", "e"), ("animationCompleteEvent", "e"))
op("Tk2dPlayFrame", ("gameObject", "o"), ("frame", "i"))
op("Tk2dSpriteGetId", ("gameObject", "o"), ("spriteID", "I"), ("everyframe", "n"))
op("Trigger2dEvent", ("trigger", "n"), ("sendEvent", "e"), ("tag", "n"))
op("ListenForUp", ("wasPressed", "e"), ("isPressed", "e"))
op("ListenForDown", ("wasPressed", "e"), ("isPressed", "e"))
op("ListenForLeft", ("wasPressed", "e"))
op("ListenForRight", ("wasPressed", "e"))
op("ListenForAttack", ("wasPressed", "e"))
op("ListenForJump", ("wasPressed", "e"))
op("ListenForCast", ("wasPressed", "e"))
op("CheckTrackTriggerCount", ("count", "i"), ("test", "n"), ("successEvent", "e"), ("everyFrame", "n"))
op("ShowPromptMarker", ("labelName", "s"), ("spawnPoint", "o"), ("storeObject", "O"))
op("HidePromptMarker", ("storedObject", "o"))
op("SetFsmBool", ("gameObject", "o"), ("fsmName", "s"), ("variableName", "s"), ("setValue", "b"), ("slot", "n"))
op("SetFsmString", ("gameObject", "o"), ("fsmName", "s"), ("variableName", "s"), ("setValue", "s"), ("slot", "n"))
op("StartConversation", ("text", "x"))
op("HeroCall", ("method", "n"), ("store", "B"), ("a", "f"))
op("Shake", ("kind", "n"))
op("SetRumble", ("kind", "n"), ("on", "b"))
op("EaseFloat", ("fromValue", "f"), ("toValue", "f"), ("floatVariable", "F"), ("time", "f"), ("finishEvent", "e"))
op("CameraZoom", ("z", "f"), ("time", "f"), ("delay", "f"))
op("Collision2dEvent", ("sendEvent", "e"))
op("HudBlanker", ("alpha", "f"), ("time", "f"), ("on", "n"))
op("CreateObject", ("gameObject", "o"), ("x", "f"), ("y", "f"), ("storeObject", "O"), ("spawnPoint", "o"))
op("GetItemMsg", ("item", "n"))
op("Blanker", ("alpha", "f"), ("everyFrame", "n"))
op("BlankerOn", ("on", "b"))
op("FindGameObject", ("objectName", "s"), ("store", "O"))
op("SetParent", ("gameObject", "o"))
op("IntOp", ("intVariable", "I"), ("value", "i"))
op("ActivateAllChildren", ("gameObject", "o"), ("activate", "b"))
op("iTweenMoveBy", ("gameObject", "o"), ("vector", "v3"), ("time", "f"), ("easeType", "n"), ("loopType", "n"),
   ("finishEvent", "e"))
op("SetFsmFloat", ("gameObject", "o"), ("fsmName", "s"), ("variableName", "s"), ("setValue", "f"), ("slot", "n"))
op("TextAlign", ("centre", "n"))
op("ObjAlpha", ("gameObject", "o"), ("alpha", "f"), ("everyFrame", "n"))
op("BuildString")   # (custom: its parts, the strings they can make, where it keeps it)
op("StartConversationOf")   # (custom: its key and sheet, then the conversations they can name)
op("NoticeIcon", ("icon", "n"))
op("NoticeText", ("text", "i"))
op("CharmNotice", ("id", "i"))
op("CharmTute")
op("GetObjAlpha", ("gameObject", "o"), ("store", "F"))
# (flung: its box, from the position; its bounce (ObjectBounce, -1 none), its friction)
op("FlingObject", ("flungObject", "o"), ("speedMin", "f"), ("speedMax", "f"), ("angleMin", "f"), ("angleMax", "f"),
   ("ox", "k"), ("oy", "k"), ("hx", "k"), ("hy", "k"), ("bounce", "k"), ("friction", "k"))
op("GetSpeed2d", ("gameObject", "o"), ("storeResult", "F"), ("everyFrame", "n"))
op("SendRandomEvent", ("events", "e*"), ("weights", "w*"))
# (FlingObjectsFromGlobalPool of geo: GeoControl's, the C code's (enemy.c), from a spawn point)
# (ConvertIntToString: the string of each int it can be, here from 0)
op("IntToString", ("intVariable", "i"), ("stringVariable", "S"), ("strings", "x*"))
op("FlingGeo", ("type", "n"), ("count", "n"), ("spawnPoint", "o"), ("speedMin", "f"), ("speedMax", "f"),
   ("angleMin", "f"), ("angleMax", "f"))
op("IncrementPlayerDataInt", ("intName", "q"))
op("ListenForInventory", ("wasPressed", "e"))
op("WaitRandom", ("timeMin", "f"), ("timeMax", "f"), ("finishEvent", "e"))
# (debris the C code flings (obj.c's pieces), from an object's place by (dx, dy): its sprite, how it falls, bounces and
# spins (flags: 1 spun, 2 turned at random); hide: that object off as its piece goes; snap: set on the ground below,
# dy above it (SnapToGround), not flung)
op("FlingPiece", ("gameObject", "o"), ("hide", "n"), ("snap", "n"), ("sprite", "x"), ("layer", "n"), ("order", "x"),
   ("flags", "n"), ("dx", "k"), ("dy", "k"), ("z", "k"), ("rot", "k"), ("gravity", "k"), ("bounce", "k"), ("spin", "k"),
   ("mirror", "k"), ("speedMin", "f"), ("speedMax", "f"), ("angleMin", "f"), ("angleMax", "f"))
# the yes or no box (DialogueTextYN): its question, its toll (geo, 0 none), who hears YES or NO
op("StartConversationYN", ("text", "x"))
op("SetToll", ("cost", "i"))
op("SetYNRequester", ("gameObject", "o"))
op("SendEventOf")   # (custom: to itself, the event a string variable names: the variable, then names and events)
op("HudSlide", ("out", "n"))
# the stag menu (src/stag.c): the station chosen to the variable, then CONTINUE; a ride: to nextScene
op("OpenStagMenu", ("result", "S"))
op("SetNextScene", ("scene", "s"))
op("StagTravel")
op("Translate", ("gameObject", "o"), ("x", "f"), ("y", "f"), ("perSecond", "n"), ("everyFrame", "n"))
op("HeroCollision", ("sendEvent", "e"))   # (Collision2dEvent with the Knight: him touching its owner's colliders)
op("PlatformStick", ("on", "n"))          # (HeroPlatformStick: the Knight on its owner moves with it)
op("WaitForHeroInPosition", ("sendEvent", "e"))
op("SendTrigger2DEvent", ("eventTarget", "t"), ("sendEvent", "e"))   # (the Knight in its owner's trigger: to another)
op("FreezeMoment", ("type", "n"))         # (GameManager.FreezeMoment)
op("DamagerInfo", ("which", "n"), ("store", "I"))   # (the hit's damages_enemy: 0 damageDealt, 1 attackType, 2 direction)
op("IntOperator", ("integer1", "i"), ("integer2", "i"), ("operation", "n"), ("storeResult", "I"), ("everyFrame", "n"))
op("GetDistance", ("gameObject", "o"), ("target", "o"), ("storeResult", "F"), ("everyFrame", "n"))
op("SoulOrbs", ("spawnMin", "i"), ("spawnMax", "i"), ("speedMin", "f"), ("speedMax", "f"), ("angleMin", "f"),
   ("angleMax", "f"), ("originVariationX", "f"), ("originVariationY", "f"))   # (Soul Orb R: flung, then to the Knight)
op("FadeTo", ("gameObject", "o"), ("alpha", "f"), ("time", "f"), ("includeChildren", "n"))   # (iTweenFadeTo)
op("SetBenchRespawn", ("marker", "x"), ("type", "n"), ("facing", "n"))   # (PlayerData.SetBenchRespawn: the marker's name)
op("FadeColorFader", ("target", "o"), ("fadeType", "n"), ("useChildren", "n"))   # (ColorFader.Fade: 0 up, 1 down)
op("BoolNoneTrue", ("boolVariables", "b*"), ("sendEvent", "e"), ("storeResult", "B"), ("everyFrame", "n"))
# (the dialogue text's place, its stop's, its arrow's (0, 1, 2: HUD units); the stop's pause before it shows)
op("DialoguePlace", ("which", "n"), ("y", "f"))
op("StopPause", ("time", "f"))
op("GetFsmBool", ("gameObject", "o"), ("fsmName", "s"), ("slot", "n"), ("storeValue", "B"), ("everyFrame", "n"))
op("ConvertIntToFloat", ("intVariable", "i"), ("floatVariable", "F"), ("everyFrame", "n"))
op("ConvertFloatToInt", ("floatVariable", "f"), ("intVariable", "I"), ("rounding", "n"), ("everyFrame", "n"))
op("SendEventByScale", ("gameObject", "o"), ("positiveEvent", "e"), ("negativeEvent", "e"))   # (its x scale's sign)
op("ListenForQuickMap", ("wasPressed", "e"))
op("MapUpdateMsg")   # (Map Update Msg: src/map.c's)
op("UpdateGameMap", ("store", "B"))   # (GameManager.UpdateGameMap: any room newly mapped)
op("SceneVisited", ("room", "n"))     # (GameManager.AddToScenesVisited: a map piece of its own, MAP_SCENES)

# HeroController's methods the scripts call (HeroCall's method)
HERO_METHODS = ["RelinquishControl", "RegainControl", "StopAnimationControl", "StartAnimationControl", "FaceLeft",
                "FaceRight", "CanTalk", "PreventCastByDialogueEnd", "SetBackOnGround", "AddMPCharge",
                "FindGroundPoint", "SetBenchRespawn", "SetHazardRespawn", "RelinquishControlNotVelocity",
                "SetCState", "SaveGame", "AffectedByGravity", "ResetHardLandingTimer", "StopPlayingAudio", "CanInspect",
                "CanInput", "GetState", "CancelHeroJump", "ForceHardLanding", "AddHealth", "TryAddMPChargeSpa", "RecoilLeft",
                "RecoilRight"]
# (GetState's states, by name: its argument)
HERO_STATES = ["onGround", "attacking", "upAttacking", "downAttacking", "dashing", "backDashing", "willHardLand",
               "spellQuake"]   # (backDashing, spellQuake: never here)
# the geo prefabs (Geo Small, Med, Large): GeoControl's types
GEO_PREFABS = {("resources.assets", 5736): 0, ("resources.assets", 6395): 1, ("resources.assets", 6376): 2}
SOUL_ORB = ("resources.assets", 4231)   # (Soul Orb R: src/obj.c's soul orbs)
# the prompt marker the pool gives (Arrow Prompt New): a script's own, shown and hidden as ShowPromptMarker's
PROMPT_PREFAB = ("resources.assets", 6142)
# prefabs CreateObject makes that the scripts go on with (made beforehand, off): (file, path id)
PREFAB_SPAWNS = {("sharedassets76.assets", 69), ("sharedassets133.assets", 23), ("sharedassets6.assets", 509)}
# prefabs the pool gives (SpawnObjectFromGlobalPool) that the scripts show (made beforehand, off)
POOL_SPAWNS = {("resources.assets", 5267)}
# the notices' prefabs (made by the C code: msg.c): a relic's, a charm's, the charm tutorial
MAP_GET_MSG = ("sharedassets64.assets", 18)      # (Map Get Msg: a relic's notice, as msg.c shows it)
MAP_UPDATE_MSG = ("sharedassets7.assets", 539)   # (Map Update Msg: src/map.c's)
NOTICE_PREFABS = {("resources.assets", 4251): "relic", ("sharedassets6.assets", 446): "charm",
                  ("sharedassets6.assets", 491): "tute"}
# the icons the scripts set on a notice (SetSpriteRendererSprite): (scene or prefab document, sprite ref), as NoticeIcon
# numbers them (pack.py makes the sprites)
NOTICE_ICONS = []
# the charms this part of the game has (their icons and names: msg.c, the inventory)
CHARMS = [1, 2, 3, 4, 6, 7, 8, 14, 18, 19, 20]
# the game's objects the scripts find by name that are not theirs (enemies): what they are to them
GAME_OBJECTS = {"Hornet Boss 1": 0xFF0C, "Prompt Blanker": 0xFF11}   # (the HUD's Prompt Blanker: msg.c's)
O_CHARM_TUTE = 0xFF0E   # (src/vm.c)
# what a trigger's Trigger2dEvent hears, by collideTag: the Knight (any), a spell
TRIGGER_TAGS = {None: 0, "": 0, "Player": 0, "Untagged": 0, "Hero Spell": 1, "Nail Attack": 2}
# the items the message shows (text.MSGS's order), by Msg Control's Item
MSG_ITEMS = {"Fireball": 0, "Dash": 1}
# camera shake events
SHAKES = {"EnemyKillShake": 1, "AverageShake": 2, "BigShake": 3, "SmallShake": 4}


class Room:
    """A room's VM data being built."""
    def __init__(self, name, d, sprites, texts, actor_of):
        self.name, self.d = name, d
        self.sprites, self.texts, self.actor_of = sprites, texts, actor_of
        self.by_id = {o["id"]: o for o in d["objects"]}
        self.by_path = {o["path"]: o for o in d["objects"]}
        for o in d["objects"]:
            o["_scene"], o["_raw"] = name, o["id"]
        # (the scenes it loads with it, the first of them: their objects there only as they are, by a PlayerData bool)
        import scene
        for an, flag, val, which in scene.additive(name):
            if which != 1:
                continue
            da = unity.scene(an)
            ids = {q["id"] for q in da["objects"]}
            k = lambda i, an=an: ("sc", an, i)
            for q in da["objects"]:
                c = dict(q)
                c["id"] = k(q["id"])
                c["parent"] = k(q["parent"]) if q.get("parent") in ids else None
                c["children"] = [k(x) for x in q.get("children", [])]
                c["_doc"], c["_key"], c["_scene"], c["_raw"] = da, k, an, q["id"]
                if c["parent"] is None:
                    c["_variant"] = (flag, val)
                self.by_id[c["id"]] = c
                self.by_path.setdefault(q["path"], c)
        self.objs, self.obj_index = [], {}
        self.folds = []   # (sprites drawn with an object of theirs, nothing else to them: (its object, them))
        self.fsms = []
        self.persist, self.persist_objs = [], []   # (PersistentBoolItem: FSM, its Activated's slot, its object)
        self.problems = []

    # ------------------------------------------------------------ objects
    def external_vars(self):
        """the variables FSMs set in other FSMs (SetFsmBool, PersistentBoolItem's Activated)"""
        if not hasattr(self, "_ext"):
            self._ext = {"Activated", "Selection Result"}   # (and the stag menu's: src/stag.c)
            for o in list(self.by_id.values()):
                for c in o["c"]:
                    for st in (c.get("fsm") or {}).get("states", []):
                        for a in st["actions"]:
                            if a["name"].startswith("SetFsm"):
                                self._ext.add(_one_string(self, c["fsm"], dict(a["params"]).get("variableName")))
        return self._ext

    def want(self, o):
        cls = {c.get("class") or c["type"] for c in o["c"]}
        if o["path"].startswith("Grub King/Rewards Parent/Reward ") and int(o["name"].split()[-1]) > GRUBS:
            return False
        if (self.name, o["path"]) in SKIP_PATHS or any((self.name, p) in SKIP_PATHS for p in _ancestors(o["path"])):
            return False
        return not (cls & SKIP_CLASSES) and o["name"] not in SKIP_NAMES and o["name"] not in HUD_CHILDREN

    def referenced(self):
        """the names and objects the scene's scripts can find (FindChild, FindGameObject, references)"""
        if not hasattr(self, "_refs"):
            names, ids = set(), set()

            def walk(v):
                if isinstance(v, str):
                    names.add(v)
                elif isinstance(v, (list, tuple)):
                    if len(v) == 3 and v[0] == "ref":
                        ids.add(v[2])
                    for x in v:
                        walk(x)
                elif isinstance(v, dict):
                    for x in v.values():
                        walk(x)
            for q in list(self.by_id.values()):
                for c in q["c"]:
                    f = c.get("fsm")
                    if f:
                        walk(f["vars"])
                        for st in f["states"]:
                            for a in st["actions"]:
                                walk(a["params"])
            self._refs = names, ids
        return self._refs

    def foldable(self, q):
        """a sprite and nothing else, on, under nothing, that no script finds: drawn with its parent's object"""
        if q.get("children") or not q.get("active") or not q.get("self_active", True):
            return False
        if {c.get("class") or c["type"] for c in q["c"]} != {"SpriteRenderer"}:
            return False
        sr = next(c.get("v") for c in q["c"])
        if not sr or not sr.get("m_Sprite") or not sr.get("m_Enabled", 1):
            return False
        # (its parent's own script destroying its children, as a collapsing floor does: each its own object, to go)
        p = self.by_id.get(q.get("parent"))
        if p is not None and any(a["name"] == "DestroyAllChildren" for c in p["c"] if c.get("fsm")
                                 for st in c["fsm"]["states"] for a in st["actions"]):
            return False
        names, ids = self.referenced()
        return q["name"] not in names and q.get("_raw", q["id"]) not in ids and not any(c.get("pid") in ids for c in q["c"])

    def add_tree(self, o):
        if not self.want(o):
            return
        i = self.obj(o)
        for c in o.get("children", []):
            q = self.by_id[c]
            if self.foldable(q):
                self.folds.append((i, q))
            else:
                self.add_tree(q)

    def obj(self, o):
        if o["id"] in self.obj_index:
            return self.obj_index[o["id"]]
        p = self.by_id.get(o.get("parent"))
        if p is not None and p["id"] not in self.obj_index and self.want(p) and any(
                o["path"].startswith(q + "/") for q in ROOMS.get(self.name, [])):
            self.obj(p)
        self.obj_index[o["id"]] = len(self.objs)
        self.objs.append(o)
        return self.obj_index[o["id"]]

    def add_prefab(self, f, pid, site=None):
        """A prefab's objects, made beforehand (off): its root's VM object (one for each site that makes it)."""
        key = ("pf", f, pid, site)
        if key in self.obj_index:
            return self.obj_index[key]
        d2 = unity.prefab(f, pid)
        ids = {q["id"] for q in d2["objects"]}
        k = lambda i: ("pf", f, pid, site, i)
        root = None
        for q in d2["objects"]:
            c = dict(q)
            c["id"] = k(q["id"])
            c["parent"] = k(q["parent"]) if q.get("parent") in ids else None
            c["children"] = [k(x) for x in q.get("children", [])]
            c["_doc"], c["_key"] = d2, k
            self.by_id[c["id"]] = c
            if c["parent"] is None:
                root = c
        root["active"] = root["self_active"] = False
        self.add_tree(root)
        self.obj_index[key] = self.obj_index[root["id"]]
        return self.obj_index[root["id"]]

    def ref_obj(self, r, key=None):
        """["ref", file, pid] -> a VM object (this scene's, or this prefab's, objects only)"""
        if not r or r[1] != 0:
            return O_NONE
        o = self.by_id.get(key(r[2]) if key else r[2])
        if o is None:
            return O_NONE
        # (a mask not the scripts' own: the game's, found by its name as the FSM runs)
        if o["id"] not in self.obj_index and any(c.get("fsm") and c["fsm"]["name"] in ("unmasker", "remasker", "remasker_inverse")
                                                 for c in o["c"]):
            return 0x10000 | STR.id(o["name"])
        if o["id"] not in self.obj_index:
            self.add_tree(o)
        return self.obj_index.get(o["id"], O_NONE)


def _string_values(rm, f, v, depth=0):
    """the strings a string operand of an FSM can be (constants: itself; a variable: as it starts and as it is set,
    by other FSMs or its own SetStringValue and BuildString)"""
    import itertools
    if not (isinstance(v, str) and v.startswith("$")):
        return [v or ""]
    n = v[1:]
    out = [f["vars"].get(n, ["string", ""])[1] or ""]
    for o in list(rm.by_id.values()):
        for c in o["c"]:
            for st in (c.get("fsm") or {}).get("states", []):
                for a in st["actions"]:
                    P = dict(a["params"])
                    if a["name"] == "SetFsmString" and P.get("fsmName") == f["name"] and P.get("variableName") == n:
                        out.append(P.get("setValue") or "")
    for st in f["states"]:
        for a in st["actions"]:
            if not a.get("enabled", True):
                continue
            P = dict(a["params"])
            if a["name"] == "SetStringValue" and P.get("stringVariable") == v and depth < 4:
                out += _string_values(rm, f, P.get("stringValue"), depth + 1)
            elif a["name"] == "ConvertIntToString" and P.get("stringVariable") == v:
                out += [str(i) for i in INT_STRINGS]
            elif a["name"] == "BuildString" and P.get("storeResult") == v and depth < 4:
                sep = P.get("separator") or ""
                parts = [_string_values(rm, f, q, depth + 1) for q in P.get("stringParts") or []]
                for row in itertools.product(*parts):
                    out.append(sep.join(row) + (sep if P.get("addToEnd") else ""))
    return sorted(set(out))


def _one_string(rm, f, v):
    """a string operand: its one value if a variable can be only that, else itself"""
    vals = _string_values(rm, f, v) if isinstance(v, str) and v.startswith("$") else [v]
    return vals[0] if len(vals) == 1 else v


def _active_chain(rm, o):
    while o is not None:
        if not o["active"]:
            return False
        o = rm.by_id.get(o.get("parent"))
    return True


# ---------------------------------------------------------------- compiling an FSM
def _slots(vars_, keep=None):
    """Its variables (those in keep) -> {name: (slot, type)}, the number of slots (vectors take 3, colors 4)."""
    out, n = {}, 0
    for name, (t, _) in vars_.items():
        if keep is not None and name not in keep:
            continue
        out[name] = (n, t)
        n += {"vector3": 3, "vector2": 2, "color": 4, "rect": 4}.get(t, 1)
    return out, n


def _ancestors(path):
    parts = path.split("/")
    return ["/".join(parts[:i]) for i in range(1, len(parts))]


def _dollars(v):
    """the variables a parameter names ($name), however deep"""
    if isinstance(v, str):
        return [v[1:]] if v.startswith("$") else []
    if isinstance(v, dict):
        return [n for q in v.values() for n in _dollars(q)]
    if isinstance(v, (list, tuple)):
        return [n for q in v for n in _dollars(q)]
    return []


SCALARS = ("int", "bool", "float", "string", "gameObject")


def _layout(f, external):
    """Its variables as the VM keeps them: those an action may change (or another FSM: external), in slots; those
    only read, constants (their values as they start); the rest, nothing -> (slots, their number, the constants'
    names). (By what its actions are, all of them: the same for each FSM made the same)"""
    reads, other = set(), set()
    for st in f["states"]:
        for a in st["actions"]:
            if not a.get("enabled", True):
                continue
            schema = dict(OPS[a["name"]][1]) if a["name"] in OPS else {}
            for k, v in a["params"]:
                kind = schema.get(k)
                (reads if kind and kind[0].islower() else other).update(_dollars(v))
    vars_ = f["vars"]
    inline = {n for n in reads - other - set(external) if n in vars_ and vars_[n][0] in SCALARS}
    keep = {n for n in reads | other if n in vars_} - inline
    slots, n = _slots(vars_, keep)
    return slots, n, inline


def _u32(v):
    if isinstance(v, bool):
        return int(v)
    if isinstance(v, float):
        return struct.unpack("<I", struct.pack("<f", v))[0]
    return v & 0xFFFFFFFF


OWNER = 0xFF0F


class Compiler:
    def __init__(self, rm, o, f):
        self.rm, self.o, self.f = rm, o, f
        self.slots, self.nvars, self.inline = _layout(f, rm.external_vars())
        self.consts = []
        self.states = {s["name"]: i for i, s in enumerate(f["states"])}
        self.where = ""
        # (a prompt marker it spawns from the pool, Arrow Prompt New: its spawn point, where it keeps it, its label)
        self.prompt = None
        for st in f["states"]:
            for a in st["actions"]:
                P = dict(a["params"])
                if a["name"] == "SpawnObjectFromGlobalPool" and P.get("storeObject"):
                    import ents
                    r = P.get("gameObject")
                    if isinstance(r, (list, tuple)) and ents._ref_file(o.get("_doc") or rm.d, [r[1], r[2]]) == PROMPT_PREFAB:
                        self.prompt = [P.get("spawnPoint"), P.get("storeObject"), None]
        for st in f["states"] if self.prompt else []:
            for a in st["actions"]:
                P = dict(a["params"])
                if a["name"] == "SetFsmString" and P.get("gameObject") == self.prompt[1] and P.get("variableName") == "Prompt Name":
                    self.prompt[2] = P.get("setValue")

    def alpha(self, c):
        """a color operand's alpha -> its value slot, None if not one"""
        if isinstance(c, str) and c[1:] in self.slots and self.slots[c[1:]][1] == "color":
            return self.slots[c[1:]][0] + 3
        if isinstance(c, (list, tuple)) and len(c) == 4:
            return self.value(float(c[3]), "f")
        return None

    def string_values(self, v):
        """the strings a string operand can be (constants: itself; a variable: as it starts and as it is set)"""
        return _string_values(self.rm, self.f, v)

    def build_string(self, P):
        """BuildString: each way its parts can be, the string they make (with its separator)"""
        import itertools
        parts = P.get("stringParts") or []
        sep = P.get("separator") or ""
        store = self.store(P.get("storeResult"))
        rows = list(itertools.product(*[self.string_values(q) for q in parts]))
        assert len(rows) < 64, rows
        body = bytes([len(parts)]) + b"".join(struct.pack("<H", self.value(q, "s")) for q in parts) + bytes([len(rows)])
        for r in rows:
            body += struct.pack("<H", STR.id(sep.join(r) + (sep if P.get("addToEnd") else "")))
            body += b"".join(struct.pack("<H", STR.id(x)) for x in r)
        body += bytes([store])
        return bytes([OPS["BuildString"][0], len(body)]) + body

    def fsm_slot(self, fsm_name, var):
        """another FSM's variable, by its FSM's name and its own (one of the room's): its slot, 255 not known or not
        kept (nothing reads it)"""
        found = set()
        for o in list(self.rm.by_id.values()):
            for c in o["c"]:
                q = c.get("fsm")
                if q and q["name"] == fsm_name and var in q["vars"]:
                    s = _layout(q, self.rm.external_vars())[0]
                    found.add(s[var][0] if var in s else 255)
        if len(found) > 1:
            self.problem("%s's %s: in different slots %s" % (fsm_name, var, sorted(found)))
        return min(found) if found else 255

    def problem(self, msg):
        self.rm.problems.append("%s: %s: %s: %s: %s" % (self.rm.name, self.o["path"], self.f["name"], self.where, msg))

    def const(self, v):
        v = _u32(v)
        if v not in self.consts:
            self.consts.append(v)
        return self.nvars + self.consts.index(v)

    def found_nothing(self, v):
        """a gameObject variable only FindChild sets, from its owner, naming a child it does not have"""
        if not (isinstance(v, str) and v.startswith("$")):
            return False
        sets = [dict(a["params"]) for s in self.f["states"] for a in s["actions"]
                if v in [p[1] for p in a["params"] if p[0] in ("storeResult", "store", "storeGameObject", "storeObject")]]
        return bool(sets) and all(Q.get("gameObject") == "Owner" and "childName" in Q and not any(
            q.get("parent") == self.o["id"] and q["name"] == Q["childName"] for q in self.rm.by_id.values()) for Q in sets)

    def dialogue_part(self, v):
        """the dialogue text ($DialogueText), or its stop or arrow (FindChild of the parent GetParent gives): 0, 1, 2"""
        if v == "$DialogueText":
            return 0
        if not (isinstance(v, str) and v.startswith("$")):
            return None
        acts = [dict(a["params"]) for s in self.f["states"] for a in s["actions"] if a["name"] in ("FindChild", "GetParent")]
        parents = {Q.get("storeResult") for Q in acts if Q.get("gameObject") == "$DialogueText" and "childName" not in Q}
        for Q in acts:
            if Q.get("storeResult") == v and Q.get("gameObject") in parents and Q.get("childName") in ("Stop", "Arrow"):
                return 1 if Q["childName"] == "Stop" else 2
        return None

    def owner_var(self, v):
        """a gameObject variable GetOwner sets (its owner)"""
        return isinstance(v, str) and v.startswith("$") and any(
            a["name"] == "GetOwner" and dict(a["params"]).get("storeGameObject") == v
            for s in self.f["states"] for a in s["actions"])

    def static_child(self, v):
        """a gameObject variable FindChild sets from its owner (always the same child) -> that child"""
        if not (isinstance(v, str) and v.startswith("$")):
            return None
        for s in self.f["states"]:
            for a in s["actions"]:
                Q = dict(a["params"])
                if a["name"] == "FindChild" and Q.get("storeResult") == v and (Q.get("gameObject") == "Owner" or
                                                                               self.owner_var(Q.get("gameObject"))):
                    return next((q for q in self.rm.by_id.values() if q.get("parent") == self.o["id"] and
                                 q["name"] == Q.get("childName")), None)
        return None

    def obj_value(self, v):
        if v == "Owner":
            return OWNER
        if isinstance(v, (list, tuple)) and v and v[0] == "ref":
            return self.rm.ref_obj(v, self.o.get("_key"))
        return O_NONE

    def init_value(self, t, v):
        """a variable's value as it starts (a number; a list for vectors, colors)"""
        if t == "gameObject":
            return self.obj_value(v) if v else O_NONE
        if t == "string":
            return STR.id(v or "")
        if t in ("vector3", "vector2", "color", "rect"):
            return [_u32(float(q)) for q in v or []]
        if t == "float":
            return _u32(float(v or 0))
        if t in ("int", "bool"):
            return _u32(int(v or 0))
        return 0

    def value(self, v, kind):
        """an input operand -> its slot (u16)"""
        if v is None:
            return 0xFFFF
        if isinstance(v, str) and v.startswith("$"):
            n = v[1:]
            if n in self.slots:
                return self.slots[n][0]
            if n in self.inline:
                t, iv = self.f["vars"][n]
                return self.const(self.init_value(t, iv))
            if n in SPECIAL:
                return self.const(SPECIAL[n])
            self.problem("variable %s unknown" % n)
            return 0xFFFF
        if kind == "o":
            if isinstance(v, tuple) and v[0] == "objindex":
                return self.const(v[1])
            return self.const(self.obj_value(v))
        if kind == "s":
            return self.const(STR.id(v if isinstance(v, str) else str(v)))
        if kind == "f":
            return self.const(float(v))
        if kind in ("i", "b"):
            return self.const(int(v))
        self.problem("value %r for %s" % (v, kind))
        return 0xFFFF

    def store(self, v):
        if v is None:
            return 255
        if isinstance(v, str) and v.startswith("$") and v[1:] in self.slots:
            return self.slots[v[1:]][0]
        if isinstance(v, str) and v.startswith("$"):
            self.problem("store to %s" % v)
        return 255

    def event(self, v):
        if v is None:
            return 255
        if isinstance(v, (list, tuple)) and v[0] == "event":
            return EVENTS.id(v[1])
        if isinstance(v, str) and not v.startswith("$"):
            return EVENTS.id(v)
        if isinstance(v, str) and v[1:] in self.f["vars"] and self.f["vars"][v[1:]][0] == "string" and \
                v[1:] not in self.rm.external_vars():
            # (an event named by a string nothing changes: that one, or none)
            name = self.f["vars"][v[1:]][1]
            return EVENTS.id(name) if name else 255
        self.problem("event %r" % (v,))
        return 255

    def pd_bool(self, name):
        import ents
        pdf = ents.pd_flags()
        if name in pdf:
            return pdf[name]
        if name in PD_SPECIAL:
            return PD_SPECIAL[name]
        if name in PD_CONST:
            return 0xFFFD if PD_CONST[name] else 0xFFFE
        if name in state.CONSTANT:
            return 0xFFFD if state.CONSTANT[name] else 0xFFFE
        self.problem("PlayerData bool %s" % name)
        return 0xFFFE

    def target(self, v):
        """an event target -> bytes: kind (0 self, 1 an object's FSMs, 2 all, 3 one FSM of an object; +16 its
        children's too, +32 not the sender's), object, FSM name"""
        if v == "Self" or v is None:
            return bytes([0]) + struct.pack("<HH", 0xFFFF, 0xFFFF)
        t = v.get("target")
        x = (16 if v.get("children") and t != "BroadcastAll" else 0) | (32 if v.get("excludeSelf") else 0)
        if t == "BroadcastAll":
            return bytes([2 | x]) + struct.pack("<HH", 0xFFFF, 0xFFFF)
        go = self.value(v.get("go"), "o")
        if t == "GameObject":
            return bytes([1 | x]) + struct.pack("<HH", go, 0xFFFF)
        if t == "GameObjectFSM":
            return bytes([3 | x]) + struct.pack("<HH", go, STR.id(v.get("fsm") or ""))
        self.problem("event target %r" % (v,))
        return bytes([0]) + struct.pack("<HH", 0xFFFF, 0xFFFF)

    def operand(self, kind, v):
        if kind in ("f", "i", "b", "s", "o"):
            return struct.pack("<H", self.value(v, kind))
        if kind in ("F", "I", "B", "S", "O", "V3"):
            return bytes([self.store(v)])
        if kind == "v3":
            if v is None:
                return struct.pack("<H", 0xFFFF)
            if isinstance(v, str):
                return struct.pack("<H", self.value(v, "f"))
            a = self.const(float(v[0]))
            self.const(float(v[1]))
            self.const(float(v[2]))
            return struct.pack("<H", a)
        if kind == "e":
            return bytes([self.event(v)])
        if kind == "es":
            return bytes([self.event(v)])
        if kind == "t":
            return self.target(v)
        if kind == "p":
            if isinstance(v, str) and not v.startswith("$"):
                self.pd_bool(v)   # (known?)
            return struct.pack("<H", self.value(v, "s"))
        if kind == "q":
            if v not in PD_INTS:
                self.problem("PlayerData int %s" % v)
                return bytes([255])
            return bytes([PD_INTS.index(v)])
        if kind == "n":
            return bytes([int(v or 0) & 255])
        if kind == "k":
            return struct.pack("<f", float(v or 0))
        if kind == "x":
            return struct.pack("<H", v)
        if kind == "w*":
            # (weights: their shares of 255; none: all alike)
            w = [float(x) for x in v or []]
            if len(set(w)) <= 1:
                return bytes([0])
            return bytes([len(w)]) + bytes(int(round(255 * x / sum(w))) for x in w)
        if kind == "x*":
            return bytes([len(v)]) + b"".join(struct.pack("<H", q) for q in v)
        if kind in ("i*", "b*", "e*", "f*"):
            v = v or []
            out = bytes([len(v)])
            for q in v:
                out += self.operand(kind[0], q)
            return out
        self.problem("operand kind %s" % kind)
        return b""

    def emit(self, name, params):
        code, schema = OPS[name]
        body = b"".join(self.operand(k, params.get(p)) for p, k in schema)
        assert len(body) < 256
        return bytes([code, len(body)]) + body

    # ------------------------------------------------------------ actions
    def action(self, a):
        n = a["name"]
        P = dict(a["params"])
        if n.startswith("SetFsm"):
            # (another FSM's variable named by variables: what they always are)
            P = dict(P, fsmName=_one_string(self.rm, self.f, P.get("fsmName")),
                     variableName=_one_string(self.rm, self.f, P.get("variableName")))
        if n in ("AudioPlayerOneShot", "AudioPlayerOneShotSingle", "AudioStop", "AudioPlay", "AudioPlaySimple",
                 "PlayParticleEmitter", "StopParticleEmitter", "SetParticleEmission", "SetParticleEmissionRate",
                 "ForceHeroFootstepSound", "SendEventToRegister", "AddTrackTrigger", "PlayVibration",
                 "VibrationPlayerStop", "TransitionToAudioSnapshot", "SetAudioPitch", "SetAudioVolume",
                 "AudioPlayInState", "FadeAudio", "PlayRandomSound", "SetRotation", "RandomFloat",
                 "GetLastEvent", "SetBoxCollider2DSize", "Tk2dSpriteSetColor", "SetTextMeshProColor",
                 "AudioPlayRandom", "SetName", "GameObjectIsNull", "PlayVibrationV2",
                 "SpawnFromPool", "SpawnRandomObjects", "Rotate", "GetEventSender", "GetMidPoint", "SetAudioClip"):
            return None
        if n in ("SetMaterialColor", "GetMaterialColor", "EaseColor") and self.f["name"] != "Blanker Control":
            # (colors: only the stag stations' Stag Blanker's, black as the room comes in, then faded out to show it)
            return None
        if n == "SendEventByNameV2":
            n = "SendEventByName"
        if n == "ReceivedDamage":
            # (a nail's hit on its collider)
            return self.emit("Trigger2dEvent", {"trigger": 0, "sendEvent": P.get("sendEvent"), "tag": TRIGGER_TAGS["Nail Attack"]})
        if n == "FindChild" and P.get("childName") in HUD_CHILDREN:
            return self.emit("SetGameObject", {"variable": P.get("storeResult"),
                                               "gameObject": ("objindex", HUD_CHILDREN[P.get("childName")])})
        if n == "GetFsmBool":
            go = P.get("gameObject")
            if self.found_nothing(go):
                return None   # (a child it does not have: the variable as it was)
            return self.emit(n, dict(P, slot=self.fsm_slot(_one_string(self.rm, self.f, P.get("fsmName")),
                                                            _one_string(self.rm, self.f, P.get("variableName")))))
        if n == "SetFsmGameObject" and P.get("gameObject") == "$DialogueText" and P.get("variableName") == "Requester":
            return None   # (who hears CONVO_FINISH: all do here)
        if n == "SetFsmFloat" and P.get("gameObject") == "$DialogueText" and P.get("variableName") == "Stop Pause":
            return self.emit("StopPause", {"time": P.get("setValue")})
        if n == "SetPosition" and self.dialogue_part(P.get("gameObject")) is not None:
            if P.get("vector") is not None or P.get("x") is not None or P.get("y") is None or P.get("everyFrame"):
                self.problem("SetPosition %r" % P)
                return None
            return self.emit("DialoguePlace", {"which": self.dialogue_part(P.get("gameObject")), "y": P.get("y")})
        if n in ("GetFsmInt", "GetFsmFloat") and P.get("gameObject") == "$Damager":
            # (the hit's damages_enemy: its damage, its attack's type (0 the nail, 2 a spell), its direction)
            if P.get("variableName") == "magnitudeMult":
                # (its magnitude: 1, the nail's and the spells' here)
                return self.emit("SetFloatValue", {"floatVariable": P.get("storeValue"), "floatValue": 1.0})
            which = {"damageDealt": 0, "attackType": 1, "direction": 2}.get(P.get("variableName"))
            if which is None:
                self.problem("%s Damager %s" % (n, P.get("variableName")))
                return None
            return self.emit("DamagerInfo", {"which": which, "store": P.get("storeValue")})
        if n == "GetLanguageString" and P.get("sheetName") == "Prices":
            # (a price: kept for ConvertStringToInt)
            import text
            keys = self.string_values(P.get("convName"))
            keys = [k for k in keys if k in text.sheets()["Prices"]]   # (a key built from parts: the one it can be)
            if len(keys) != 1:
                self.problem("price %r" % keys)
                return None
            self.prices[P.get("storeValue")] = int(text.sheets()["Prices"][keys[0]])
            return None
        if n == "ConvertStringToInt" and P.get("stringVariable") in self.prices:
            return self.emit("SetIntValue", {"intVariable": P.get("intVariable"), "intValue": self.prices[P.get("stringVariable")],
                                             "everyFrame": 0})
        if n == "WaitForBossLoad":
            return self.emit("NextFrameEvent", {"sendEvent": P.get("sendEvent")})
        if n == "SetPlayerDataString" and P.get("stringName") == "nextScene":
            return self.emit("SetNextScene", {"scene": P.get("value")})
        if n == "LoadLevel" and P.get("levelName") == "Cinematic_Stag_travel":
            return self.emit("StagTravel", {})
        if n == "SetFsmGameObject" and P.get("gameObject") == "$MenuHolder":
            return None   # (the stag menu's requester: OpenStagMenu's)
        if n in ("PlayerDataBoolTest",):
            return self.emit(n, P)
        if n == "CreateUIMsgGetItem":
            return None   # (made by the SetFsmString of its Item that follows)
        if n == "SetFsmString" and self.prompt and P.get("gameObject") == self.prompt[1]:
            return None   # (its prompt's label: kept for UP)
        if n == "SpawnObjectFromGlobalPool" and self.prompt and P.get("storeObject") == self.prompt[1]:
            return None
        if n == "SetFsmString" and P.get("fsmName") == "Map Msg" and P.get("variableName") == "Convo":
            # (a map's notice: its name, the UI sheet's)
            import text
            keys = [k for k in self.string_values(P.get("setValue")) if k in text.sheets()["UI"]]
            if len(keys) != 1:
                self.problem("map notice %r" % keys)
                return None
            return self.emit("NoticeText", {"text": self.rm.texts.add(text.clean(text.sheets()["UI"][keys[0]]), "NOTICE")})
        if n == "SetFsmString" and P.get("fsmName") == "Msg Control" and P.get("variableName") == "Item":
            if P.get("setValue") not in MSG_ITEMS:
                self.problem("item message %s" % P.get("setValue"))
                return None
            return self.emit("GetItemMsg", {"item": MSG_ITEMS[P.get("setValue")]})
        if n == "SetFsmString":
            return self.emit(n, dict(P, slot=self.fsm_slot(P.get("fsmName"), P.get("variableName"))))
        if n == "EaseColor":
            # (colors: their alphas only, what the scripts change)
            cv = P.get("colorVariable")
            if not (isinstance(cv, str) and cv[1:] in self.slots) or (P.get("easeType") or 0) != 21:
                self.problem("EaseColor to %r" % cv)
                return None
            slot = self.slots[cv[1:]][0] + 3
            fv, tv = self.alpha(P.get("fromValue")), self.alpha(P.get("toValue"))
            if fv is None or tv is None:
                self.problem("EaseColor from %r" % P.get("fromValue"))
                return None
            code, schema = OPS["EaseFloat"]
            body = struct.pack("<HH", fv, tv) + bytes([slot]) + \
                struct.pack("<H", self.value(P.get("time"), "f")) + bytes([self.event(P.get("finishEvent"))])
            return bytes([code, len(body)]) + body
        if n in ("SetColorValue", "GetColorRGBA"):
            # (a color's alpha into another's, or a float)
            src = self.alpha(P.get("color"))
            dst = P.get("colorVariable") if n == "SetColorValue" else P.get("storeAlpha")
            if src is None or not (isinstance(dst, str) and dst[1:] in self.slots):
                return None
            code, _ = OPS["SetFloatValue"]
            body = bytes([self.slots[dst[1:]][0] + (3 if n == "SetColorValue" else 0)]) + struct.pack("<H", src) + \
                bytes([int(P.get("everyFrame") or 0)])
            return bytes([code, len(body)]) + body
        if n == "GetMaterialColor" and isinstance(P.get("color"), str) and P.get("color")[1:] in self.slots:
            # (its alpha into the color's)
            body = struct.pack("<H", self.value(P.get("gameObject") or "Owner", "o")) + \
                bytes([self.slots[P.get("color")[1:]][0] + 3])
            return bytes([OPS["GetObjAlpha"][0], len(body)]) + body
        if n == "SetMaterialColor" and P.get("gameObject") not in ("$HUD Blanker",):
            a = self.alpha(P.get("color"))
            if a is None:
                self.problem("SetMaterialColor %r" % P.get("color"))
                return None
            code, _ = OPS["ObjAlpha"]
            body = struct.pack("<HH", self.value(P.get("gameObject") or "Owner", "o"), a) + bytes([int(P.get("everyFrame") or 0)])
            return bytes([code, len(body)]) + body
        if n == "SetTextMeshProAlignment":
            if P.get("gameObject") != "$DialogueText":
                return None
            return self.emit("TextAlign", {"centre": 1 if P.get("topCentre") else 0})
        if n == "SetFsmFloat":
            return self.emit(n, dict(P, slot=self.fsm_slot(P.get("fsmName"), P.get("variableName"))))
        if P.get("gameObject") == "$DialogueTextYN" and P.get("fsmName") == "Dialogue Page Control":
            if n == "SetFsmInt" and P.get("variableName") == "Toll Cost":
                return self.emit("SetToll", {"cost": P.get("setValue")})
            if n == "SetFsmGameObject" and P.get("variableName") == "Requester":
                return self.emit("SetYNRequester", {"gameObject": P.get("setValue")})
        if n == "SetFsmInt" and P.get("fsmName") == "Charm Msg" and P.get("variableName") == "ID":
            return self.emit("CharmNotice", {"id": P.get("setValue")})
        if n == "SetSpriteRendererSprite" and P.get("gameObject") == "$Msg Icon":
            r = P.get("sprite")
            if not (isinstance(r, (list, tuple)) and r and r[0] == "ref"):
                self.problem("notice icon %r" % (r,))
                return None
            key = (self.o.get("_doc") or self.rm.d, (r[1], r[2]))
            k = next((i for i, (dd, rr) in enumerate(NOTICE_ICONS) if dd is key[0] and rr == key[1]), None)
            if k is None:
                k = len(NOTICE_ICONS)
                NOTICE_ICONS.append(key)
            return self.emit("NoticeIcon", {"icon": k})
        if n == "GetLanguageString":
            # (a text of the game's: its number, in the notices' style)
            import text
            sh, key = P.get("sheetName"), P.get("convName")
            if not (isinstance(key, str) and not key.startswith("$") and key in text.sheets().get(sh, {})):
                self.problem("language string %s %s" % (sh, key))
                return None
            return self.emit("SetIntValue", {"intVariable": P.get("storeValue"),
                                             "intValue": self.rm.texts.add(text.clean(text.sheets()[sh][key]), "NOTICE")})
        if n == "SetTextMeshProText" and P.get("gameObject") == "$Msg Text":
            return self.emit("NoticeText", {"text": P.get("textString")})
        if n == "BuildString":
            return self.build_string(P)
        if n == "SetMaterialColor" and P.get("gameObject") == "$HUD Blanker":
            c = P.get("color")
            if isinstance(c, str) and c[1:] in self.slots:
                a = self.slots[c[1:]][0] + 3
            elif isinstance(c, (list, tuple)):
                a = self.value(float(c[3]), "f")
            else:
                self.problem("SetMaterialColor %r" % c)
                return None
            code, _ = OPS["Blanker"]
            body = struct.pack("<H", a) + bytes([int(P.get("everyFrame") or 0)])
            return bytes([code, len(body)]) + body
        if n == "SetSpriteRenderer" and P.get("gameObject") == "$HUD Blanker":
            return self.emit("BlankerOn", {"on": P.get("active")})
        if n == "FindGameObject":
            if P.get("objectName") in GAME_OBJECTS:
                return self.emit("SetGameObject", {"variable": P.get("store"),
                                                   "gameObject": ("objindex", GAME_OBJECTS[P.get("objectName")])})
            if P.get("objectName"):
                return self.emit("FindGameObject", P)
            if P.get("withTag") in TAGGED:
                return self.emit("SetGameObject", {"variable": P.get("store"),
                                                   "gameObject": ("objindex", TAGGED[P.get("withTag")])})
            return None   # (by tag: the main camera)
        if n == "FlingObjects":
            c = self.static_child(P.get("containerObject"))
            if c is None:
                self.problem("FlingObjects %r" % P.get("containerObject"))
                return None
            out = b""
            for cid in c.get("children", []):
                q = self.rm.by_id[cid]
                pp = _piece(q.get("_doc") or self.rm.d, q)
                if pp is not None:
                    out += self.emit("FlingPiece", dict(pp, gameObject=("objindex", self.rm.obj(q)), hide=1,
                                                        speedMin=P.get("speedMin"), speedMax=P.get("speedMax"),
                                                        angleMin=P.get("angleMin"), angleMax=P.get("angleMax")))
            return out or None
        if n in ("SpawnRandomObjects", "SpawnRandomObjectsV2"):
            r = P.get("gameObject")
            import ents
            f, pid = ents._ref_file(self.o.get("_doc") or self.rm.d, [r[1], r[2]])
            d2 = unity.prefab(f, pid)
            pp = _piece(d2, d2["objects"][0])
            if not any(c["type"] in ("SpriteRenderer", "MeshRenderer") for q in d2["objects"] for c in q["c"]):
                return None   # (particles: not drawn here)
            if pp is None or any(P.get(k) for k in ("originVariation", "originVariationX", "originVariationY")):
                self.problem("SpawnRandomObjects %s %s" % (f, pid))
                return None
            pos = P.get("position") or [0, 0, 0]
            one = self.emit("FlingPiece", dict(pp, gameObject=P.get("spawnPoint"), dx=pos[0], dy=pos[1],
                                               speedMin=P.get("speedMin"), speedMax=P.get("speedMax"),
                                               angleMin=P.get("angleMin"), angleMax=P.get("angleMax")))
            return one * int(P.get("spawnMax") or 1)   # (spawnMin .. spawnMax: as many each time here)
        if n == "CreateObject":
            r = P.get("gameObject")
            if isinstance(r, (list, tuple)) and r and r[0] == "ref":
                import ents
                f, pid = ents._ref_file(self.o.get("_doc") or self.rm.d, [r[1], r[2]])
                d2 = unity.prefab(f, pid)
                q = d2["objects"][0]
                if len(d2["objects"]) == 1 and any(c.get("class") == "SnapToGround" for c in q["c"]):
                    # (a prop set on the ground where it is made: a piece that stays)
                    pp, box = _piece(d2, q, rigid=False), next((ents._box(q, c) for c in q["c"] if c["type"] == "BoxCollider2D"), None)
                    pos = P.get("position") or [0, 0, 0]
                    return self.emit("FlingPiece", dict(pp, gameObject=P.get("spawnPoint"), snap=1, dx=pos[0],
                                                        dy=q["pos"][1] - box[1]))
                if (f, pid) in PREFAB_SPAWNS:
                    pos = P.get("position") or [None, None, None]
                    obj = self.rm.add_prefab(f, pid, (self.o["id"], self.where, P.get("storeObject")))
                    return self.emit("CreateObject", {"gameObject": ("objindex", obj), "x": pos[0], "y": pos[1],
                                                      "storeObject": P.get("storeObject")})
                if (f, pid) == MAP_UPDATE_MSG:
                    return self.emit("MapUpdateMsg", {})
                if (f, pid) == MAP_GET_MSG:
                    # (a map's notice, as a relic's: its own icon; its text below, by its Convo)
                    q = next(z for z in d2["objects"] if z["name"] == "Icon")
                    ref = next(c["v"]["m_Sprite"] for c in q["c"] if c["type"] == "SpriteRenderer")
                    key = (d2, (ref[0], ref[1]))
                    k = next((i for i, (dd, rr) in enumerate(NOTICE_ICONS) if dd is key[0] and rr == key[1]), None)
                    if k is None:
                        k = len(NOTICE_ICONS)
                        NOTICE_ICONS.append(key)
                    return self.emit("NoticeIcon", {"icon": k})
                if NOTICE_PREFABS.get((f, pid)) == "tute":
                    # (the charm tutorial: the C code's, as an object the scripts close)
                    return self.emit("CharmTute", {}) + self.emit("SetGameObject", {
                        "variable": P.get("storeObject"), "gameObject": ("objindex", O_CHARM_TUTE)})
                if (f, pid) in NOTICE_PREFABS:
                    return None   # (its parts set below: CharmNotice)
            return None   # (effects)
        if n == "SpawnObjectFromGlobalPool":
            r = P.get("gameObject")
            if isinstance(r, (list, tuple)) and r and r[0] == "ref":
                import ents
                f, pid = ents._ref_file(self.o.get("_doc") or self.rm.d, [r[1], r[2]])
                if (f, pid) in NOTICE_PREFABS:
                    return None   # (a relic's notice: its icon and text set below, NoticeIcon, NoticeText)
                if (f, pid) in POOL_SPAWNS:
                    pos = P.get("position") or [None, None, None]
                    obj = self.rm.add_prefab(f, pid, (self.o["id"], self.where, P.get("storeObject")))
                    return self.emit("CreateObject", {"gameObject": ("objindex", obj), "x": pos[0], "y": pos[1],
                                                      "storeObject": P.get("storeObject"), "spawnPoint": P.get("spawnPoint")})
            return None
        if n == "GetButtonDown":
            m = {"Jump": "ListenForJump", "Attack": "ListenForAttack", "Cast": "ListenForCast"}.get(P.get("buttonName"))
            return self.emit(m, {"wasPressed": P.get("sendEvent")}) if m else None
        if n == "SetParent":
            return None
        if n == "CallMethodProper":
            beh, m = P.get("behaviour"), P.get("methodName")
            args = P.get("parameters") or []
            if beh == "DialogueBox" and m == "StartConversation":
                arg = lambda q: q["s"] if isinstance(q, dict) else q
                key, sheet = arg(args[0]), arg(args[1])
                keys, sheets = self.string_values(key), self.string_values(sheet)
                import text
                rows = [(k, sh) for k in keys for sh in sheets if k in text.sheets().get(sh, {})]
                if not rows:
                    # (none the game has: it ends as it starts)
                    self.problem("conversation %s %s" % (keys, sheets))
                    return self.emit("SendEvent", {"eventTarget": "Self", "sendEvent": ["event", "CONVO_FINISH"], "delay": 0})
                if P.get("gameObject") == "$DialogueTextYN":
                    assert len(keys) == 1 and len(sheets) == 1, (keys, sheets)
                    return self.emit("StartConversationYN", {"text": self.rm.texts.id(sheets[0], keys[0])})
                if len(keys) == 1 and len(sheets) == 1:
                    return self.emit("StartConversation", {"text": self.rm.texts.id(sheets[0], keys[0])})
                # (by what its variables hold: each conversation they can name)
                body = struct.pack("<HHB", self.value(key, "s"), self.value(sheet, "s"), len(rows))
                for k, sh in rows:
                    body += struct.pack("<HHH", STR.id(k), STR.id(sh), self.rm.texts.id(sh, k))
                assert len(body) < 256, rows
                return bytes([OPS["StartConversationOf"][0], len(body)]) + body
            if beh == "GameManager" and m in ("CheckCharmAchievements", "AwardAchievement", "CheckStagStationAchievements",
                                              "SaveLevelState", "CheckMapAchievement"):
                return None
            if beh == "GameManager" and m == "UpdateGameMap":
                return self.emit("UpdateGameMap", {"store": P.get("storeResult")})
            if beh == "GameManager" and m == "AddToScenesVisited":
                names = self.string_values(args[0]["s"] if isinstance(args[0], dict) else args[0])
                assert len(names) == 1 and names[0] in MAP_SCENES, names
                return self.emit("SceneVisited", {"room": len(ROOM_NAMES) + list(MAP_SCENES).index(names[0])})
            if beh == "GameMap" and m == "SetupMap":
                return None   # (the map: drawn as PlayerData has it, src/map.c)
            if beh == "HeroPlatformStick" and m in ("Activate", "Deactivate"):
                return self.emit("PlatformStick", {"on": 1 if m == "Activate" else 0})
            if beh == "HeroController" and m in HERO_METHODS:
                a0 = args[0]["f"] if args and args[0].get("type") in (5, 0) else \
                    args[0]["i"] if args and args[0].get("type") == 1 else 0
                if m == "GetState":
                    a0 = HERO_STATES.index(args[0]["s"])
                return self.emit("HeroCall", {"method": HERO_METHODS.index(m), "store": P.get("storeResult"), "a": a0})
            self.problem("CallMethodProper %s.%s" % (beh, m))
            return None
        if n == "SendMessage":
            fn = (P.get("functionCall") or {}).get("fn")
            if fn in HERO_METHODS:
                v = (P.get("functionCall") or {}).get("value")
                return self.emit("HeroCall", {"method": HERO_METHODS.index(fn), "store": None,
                                              "a": float(v) if isinstance(v, (int, float)) and not isinstance(v, bool) else 0})
            if fn == "FreezeMoment":
                return self.emit("FreezeMoment", {"type": int((P.get("functionCall") or {}).get("value") or 0)})
            if fn and fn.startswith("StoryRecord_"):
                return None
            if fn in ("advanceTypewriter", "TimePasses", "StoryRecord_acquired", "StoryRecord_visited", "SetActionString",
                      "RefreshButtonIcon", "StopBounce", "CheckGrubAchievements", "AddToGrubList", "CountCharms",
                      "TriggerStartVideo", "CheckAllMaps", "CancelSuperDash"):
                return None
            self.problem("SendMessage %s" % fn)
            return None
        if n in ("SendEventByName", "SendEvent"):
            t = P.get("eventTarget")
            ev = P.get("sendEvent")
            evn = ev[1] if isinstance(ev, (list, tuple)) else ev
            go = t.get("go") if isinstance(t, dict) else None
            if go == "$CameraParent" or (isinstance(t, dict) and t.get("fsm") == "CameraShake"):
                if evn in SHAKES:
                    return self.emit("Shake", {"kind": SHAKES[evn]})
                return None
            if go == "$AudioManager":
                return None   # (music)
            if go == "$HUD Canvas" and evn in ("IN", "OUT"):
                return self.emit("HudSlide", {"out": 1 if evn == "OUT" else 0})
            if go == "$MenuHolder" and evn == "OPEN STAG MENU":
                return self.emit("OpenStagMenu", {"result": "$Selection Result"})
            if isinstance(ev, str) and ev.startswith("$") and (t == "Self" or t is None) and ev[1:] in self.rm.external_vars():
                # (an event a string set from outside names: those the state goes on by)
                rows = [(STR.id(e), EVENTS.id(e)) for e, _ in self.cur_state["transitions"] if e]
                body = struct.pack("<HB", self.value(ev, "s"), len(rows)) + b"".join(struct.pack("<HB", s, e) for s, e in rows)
                return bytes([OPS["SendEventOf"][0], len(body)]) + body
            if go in ("$DialogueManager",) and evn in ("BOX UP", "BOX DOWN", "BOX UP YN", "BOX DOWN YN"):
                return self.emit("SendEventByName", P)
            if self.prompt and go == self.prompt[1] and evn in ("UP", "DOWN"):
                # (a prompt marker it spawned: shown at its spawn point with its label, or hidden)
                if evn == "UP":
                    return self.emit("ShowPromptMarker", {"labelName": self.prompt[2], "spawnPoint": self.prompt[0],
                                                          "storeObject": go})
                return self.emit("HidePromptMarker", {"storedObject": go})
            return self.emit("SendEventByName" if n == "SendEventByName" else "SendEvent", P)
        if n == "SetFsmBool":
            go = P.get("gameObject")
            if go == "$CameraParent":
                k = {"RumblingSmall": 1, "RumblingMed": 2, "RumblingBig": 3}.get(P.get("variableName"), 0)
                return self.emit("SetRumble", {"kind": k, "on": P.get("setValue")}) if k else None
            if go == "$DialogueText":
                return None   # (Use Stop)
            return self.emit(n, dict(P, slot=self.fsm_slot(P.get("fsmName"), P.get("variableName"))))
        if n == "iTweenMoveBy":
            if P.get("gameObject") == "$Main Camera Obj" or P.get("gameObject") == "$MainCamera":
                vec = P.get("vector") or [0, 0, 0]
                return self.emit("CameraZoom", {"z": vec[2], "time": P.get("time"), "delay": P.get("delay") or 0})
            # (a bob: a move by a hair and back, the second after the first; under a pixel here)
            pair = [a for a in self.cur_state["actions"] if a["name"] == "iTweenMoveBy" and
                    dict(a["params"]).get("gameObject") == P.get("gameObject")]
            if len(pair) == 2 and all(max(abs(c) for c in dict(a["params"])["vector"]) < 0.1 for a in pair):
                return None
            if P.get("speed") is not None or (P.get("loopType") or 0) > 2 or P.get("space") or P.get("delay") or \
                    (P.get("easeType") or 0) > 21:
                self.problem("iTweenMoveBy %r" % P)
                return None
            return self.emit(n, P)
        if n == "GetScale":
            return self.emit(n, P)
        if n == "Trigger2dEvent" and P.get("collideTag") == "Wall Breaker":
            return None   # (nothing here breaks walls but the Knight)
        if n == "Trigger2dEvent":
            if P.get("collideTag") not in TRIGGER_TAGS:
                self.problem("Trigger2dEvent tag %s" % P.get("collideTag"))
                return None
            return self.emit(n, {"trigger": P.get("trigger"), "sendEvent": P.get("sendEvent"),
                                 "tag": TRIGGER_TAGS[P.get("collideTag")]})
        if n in ("Collision2dEvent", "Collision2dEventLayer"):
            # (the Knight against it; or, a body falling, the ground)
            if P.get("collideTag") == "Player" or (P.get("collideTag") in (None, "") and
                                                   not any(c["type"] == "Rigidbody2D" for c in self.o["c"])):
                return self.emit("HeroCollision", P)
            return self.emit("Collision2dEvent", P)
        if n == "WaitForHeroInPosition":
            return self.emit(n, P)
        if n in ("IntOperator", "GetDistance", "SetBenchRespawn"):
            return self.emit(n, P)
        if n in ("SetPlayerDataInt", "GetPlayerDataInt") and P.get("intName") in ("environmentType", "environmentTypeDefault"):
            return None   # (footsteps' sounds)
        if n == "FloatSwitch":
            return self.emit(n, P)
        if n == "SetPolygonCollider":
            return self.emit("SetCollider", P)
        if n == "SetProperty" and not (P.get("targetProperty") or {}).get("target"):
            return None
        if n == "SetProperty" and (P.get("targetProperty") or {}).get("prop") == "enabled":
            # (a collider of its object's switched off: as SetCollider)
            t = P["targetProperty"]["target"]
            q = next((q for q in self.rm.by_id.values() for c in q["c"] if c.get("pid") == t[2] and
                      c["type"] in ("BoxCollider2D", "PolygonCollider2D", "CircleCollider2D")), None)
            if q is not None and q["id"] in self.rm.obj_index:
                return self.emit("SetCollider", {"gameObject": ["ref", 0, q["id"]] if q is not self.o else "Owner",
                                                 "active": False})
        if n == "iTweenFadeTo":
            if P.get("delay") or (P.get("loopType") or 0) or P.get("finishEvent") or P.get("speed") is not None:
                self.problem("iTweenFadeTo %r" % P)
                return None
            return self.emit("FadeTo", dict(P, includeChildren=1 if P.get("includeChildren") else 0))
        if n == "SendTrigger2DEvent":
            if P.get("collideTag") not in (None, "", "Player") or P.get("collideLayer") or P.get("trigger") != 0:
                self.problem("SendTrigger2DEvent %r" % P)
                return None
            return self.emit(n, P)
        if n == "Translate":
            if P.get("vector") is not None or P.get("x") is not None or P.get("z") is not None or P.get("lateUpdate") \
                    or P.get("fixedUpdate"):
                self.problem("Translate %r" % P)
                return None
            return self.emit(n, dict(P, perSecond=1 if P.get("perSecond") else 0))
        if n in ("ListenForUp", "ListenForDown", "ListenForLeft", "ListenForRight", "ListenForAttack", "ListenForJump",
                 "ListenForCast", "ListenForInventory", "ListenForQuickMap"):
            if P.get("eventTarget") not in (None, "Self"):
                self.problem("%s to another" % n)
            return self.emit(n, P)
        if n == "GetTag":
            # (an object's tag: what it always is)
            o = self.o if P.get("gameObject") in ("Owner", "$Self") else None
            if o is None or P.get("everyFrame"):
                self.problem("GetTag %r" % P.get("gameObject"))
                return None
            return self.emit("SetStringValue", {"stringVariable": P.get("storeResult"),
                                                "stringValue": unity.tag_name(o.get("tag", 0)), "everyFrame": 0})
        if n == "SendRandomEvent":
            if (P.get("delay") or 0) >= 0.001:
                self.problem("SendRandomEvent %r" % P)
            return self.emit(n, P)
        if n == "FlingObjectsFromGlobalPool":
            import ents
            r = P.get("gameObject")
            ref = ents._ref_file(self.o.get("_doc") or self.rm.d, [r[1], r[2]]) if isinstance(r, (list, tuple)) else None
            if ref == SOUL_ORB:
                return self.emit("SoulOrbs", P)
            if ref not in GEO_PREFABS:
                return None   # (rocks, dust: what breaks)
            if P.get("spawnMin") != P.get("spawnMax") or P.get("originVariationX") or \
                    P.get("originVariationY") or P.get("position"):
                self.problem("FlingObjectsFromGlobalPool %r" % (ref,))
                return None
            return self.emit("FlingGeo", dict(P, type=GEO_PREFABS[ref], count=P.get("spawnMax")))
        if n == "ConvertIntToString":
            if P.get("format"):
                self.problem("ConvertIntToString format %r" % P.get("format"))
            return self.emit("IntToString", dict(P, strings=[STR.id(str(i)) for i in INT_STRINGS]))
        if n == "CheckGeoCap":
            # (the geo the Knight can hold: far more than there is here)
            return self.emit("SendEvent", {"eventTarget": "Self", "sendEvent": P.get("IsUnderCapEvent"), "delay": 0})
        if n == "FlingObject":
            o = self.o if P.get("flungObject") == "Owner" else None
            if o is None:
                self.problem("FlingObject %r" % P.get("flungObject"))
                return None
            import ents
            doc = o.get("_doc") or self.rm.d
            box = next((ents._box(o, c) for c in o["c"] if c["type"] == "BoxCollider2D" and c.get("v") and
                        not c["v"].get("m_IsTrigger")), None)
            if not box:
                self.problem("FlingObject: no box")
                return None
            ob = next((c["v"] for c in o["c"] if c.get("class") == "ObjectBounce" and c.get("v")), None)
            mat = next((c["v"].get("m_Material") for c in o["c"] if c["type"] == "BoxCollider2D" and c.get("v")), None)
            friction = 0.4   # (Unity's default material)
            if mat and mat[1]:
                friction = unity.physics_material(unity.ref_path(doc, mat[0]), mat[1]).get("friction", 0.4)
            return self.emit(n, dict(P, ox=(box[0] + box[2]) / 2 - o["pos"][0], oy=(box[1] + box[3]) / 2 - o["pos"][1],
                                     hx=(box[2] - box[0]) / 2, hy=(box[3] - box[1]) / 2,
                                     bounce=ob.get("bounceFactor", 0) if ob else -1, friction=friction))
        if n in OPS:
            return self.emit(n, P)
        self.problem("action %s" % n)
        return None

    def compile(self):
        """-> the definition's bytes (states, actions, constants), its variables' initial values"""
        f = self.f
        states = []
        live = reachable(f, self.rm.external_vars())
        self.prices = {}
        for s in f["states"]:
            self.where = s["name"]
            self.cur_state = s
            acts = []
            cut = len(s["actions"])
            if s["name"] in live:
                # (an event sent for sure, and taken: what follows never runs)
                _, sure, at = _events_of_state(f, s, _const_vars(f, self.rm.external_vars()), True)
                cut = at + 1 if sure else cut
            for a in s["actions"][:cut]:
                if not a.get("enabled", True) or s["name"] not in live:
                    continue
                b = self.action(a)
                if b:
                    acts.append(b)
            trans = []
            for ev, to in s["transitions"]:
                if to in self.states:
                    trans.append((EVENTS.id(ev), self.states[to]))
            states.append((trans, acts))
        glob = [(EVENTS.id(ev), self.states[to]) for ev, to in f["global"] if to in self.states]
        assert len(states) < 255 and self.nvars < 255
        # the definition: nstates, nvars, start, nglobal, nconsts, then the constants, the global transitions, each
        # state's offset (u16, from the definition), then the states: ntrans, nactions, transitions, actions
        head = struct.pack("<BBBBH", len(states), self.nvars, self.states.get(f["start"], 0), len(glob), len(self.consts))
        body = bytearray(head)
        body += b"".join(struct.pack("<I", c) for c in self.consts)
        body += b"".join(bytes([e, t]) for e, t in glob)
        if len(body) & 1:
            body.append(0)
        table_at = len(body)
        body += bytes(2 * len(states))
        for i, (trans, acts) in enumerate(states):
            struct.pack_into("<H", body, table_at + 2 * i, len(body))
            body += bytes([len(trans), len(acts)])
            body += b"".join(bytes([e, t]) for e, t in trans)
            body += b"".join(acts)
            if len(body) & 1:
                body.append(0)
        assert len(body) < 65536
        init = [0] * self.nvars
        for name, (slot, t) in self.slots.items():
            v = self.init_value(t, f["vars"][name][1])
            if isinstance(v, list):
                init[slot:slot + len(v)] = v
            else:
                init[slot] = v
        return bytes(body), init


# ---------------------------------------------------------------- the rooms
DEFS, DEF_INDEX = [], {}   # (definitions: shared)
BUILT = {}                 # room -> Room
LIBS = {}                  # (an animator's library: (file, pid)) -> actor name
ANIMATORS = {}             # (a Unity Animator's controller: (file, pid)) -> (actor, its clip's name)
TITLE_STRINGS = {}


def _animator(o):
    return next((c.get("v") for c in o["c"] if c.get("class") == "tk2dSpriteAnimator" and c.get("v")), None)


def prepare(rooms, sprites, texts):
    """Compiles the rooms' scripts; registers their characters' animation libraries as actors (before actors.build)."""
    import actors, ents, tk2d
    global PIECE_SPRITES
    PIECE_SPRITES = sprites.id
    ROOM_NAMES[:] = rooms
    assert len(rooms) + len(MAP_SCENES) <= 40   # (src/game.h: rooms_visited's bits)
    for name, prefixes in ROOMS.items():
        if name not in rooms:
            continue
        d = unity.scene(name)
        rm = Room(name, d, sprites, texts, None)
        # (events: the game's own (VMEV_*), then each room's own, numbered afresh)
        EVENTS.truncate(FIXED_EVENTS)
        for p in prefixes:
            if p in rm.by_path:
                rm.add_tree(rm.by_path[p])
            else:
                rm.problems.append("%s: no %s" % (name, p))
        done = 0
        while done < len(rm.objs):
            o = rm.objs[done]
            done += 1
            for c in o["c"] + _behaviours(rm, o):
                f = c.get("fsm")
                # (a door's own scripts: the game's gates are its doors here)
                if not f or f["name"] in SKIP_FSMS or _tinks(f) or _spins(f) or \
                        any(c2.get("class") == "TransitionPoint" for c2 in o["c"]):
                    continue
                cp = Compiler(rm, o, f)
                body, init = cp.compile()
                if (name, o["path"]) in PULLED and "Activated" in cp.slots:
                    init[cp.slots["Activated"][0]] = 1
                if body not in DEF_INDEX:
                    DEF_INDEX[body] = len(DEFS)
                    DEFS.append(body)
                rm.fsms.append((DEF_INDEX[body], rm.obj_index[o["id"]], init, STR.id(f["name"])))
                # (PersistentBoolItem: the save keeps its object's first FSM with an Activated bool's Activated)
                pbi = next((c2.get("v") for c2 in o["c"] if c2.get("class") == "PersistentBoolItem"), None)
                if pbi is not None and f["vars"].get("Activated", [None])[0] == "bool" and \
                        not any(p[0] == len(rm.fsms) - 1 for p in rm.persist) and \
                        not any(q[1] == rm.obj_index[o["id"]] for q in rm.persist_objs):
                    assert not pbi.get("semiPersistent"), o["path"]
                    if "Activated" in cp.slots:
                        rm.persist.append((len(rm.fsms) - 1, cp.slots["Activated"][0], o))
                    rm.persist_objs.append((len(rm.fsms) - 1, rm.obj_index[o["id"]]))
                # (PersistentIntItem: its FSM's Value, as 3 bits (none, -1 to 6); semi persistent: none again as the
                # Knight rests or dies)
                pii = next((c2.get("v") for c2 in o["c"] if c2.get("class") == "PersistentIntItem"), None)
                if pii is not None and f["vars"].get("Value", [None])[0] == "int" and "Value" in cp.slots and \
                        not any(p[0] == len(rm.fsms) - 1 for p in rm.persist):
                    rm.persist.append((len(rm.fsms) - 1, cp.slots["Value"][0] | 0x80, o,
                                       bool(pii.get("semiPersistent"))))
        for p in rm.problems:
            print("vm:", p)
        assert len(EVENTS.list) < 255, (name, len(EVENTS.list))
        nvars = sum(len(i) for _, _, i, _ in rm.fsms)
        nanims = sum(1 for o in rm.objs if _animator(o) or _unity_animator(o) or
                     any(c.get("class") in ("WaveEffectControl", "SimpleSpriteFade") for c in o["c"]))
        assert len(rm.objs) <= MAX_OBJS and len(rm.fsms) <= MAX_FSMS and nvars <= MAX_VARS and nanims <= MAX_ANIMS, \
            (name, len(rm.objs), len(rm.fsms), nvars, nanims)
        rm.named_events = {e: EVENTS.index[e] for e in NAMED_EVENTS if e in EVENTS.index}
        BUILT[name] = rm
    # Unity Animators (a sprite swapped each frame): their clips as actors'
    for rm in BUILT.values():
        for o in rm.objs:
            ua = _unity_animator(o)
            if not ua:
                continue
            import ents
            key = ents._ref_file(o.get("_doc") or rm.d, ua["m_Controller"])
            if key not in ANIMATORS:
                name, fps, loops, frames = actors.animator_clip(*key)
                m = np.array(o["m3"]).reshape(3, 3)
                k = float(max(np.hypot(m[0, 0], m[1, 0]), np.hypot(m[0, 1], m[1, 1])))
                a = "anim%d" % len(ANIMATORS)
                actors.ANIMATOR_ACTORS[a] = [(name, fps, loops, frames, k)]
                ANIMATORS[key] = (a, name)
    # the animators' libraries: actors with the clips the scripts name (and their first)
    names = set(STR.list)
    known = {(v[0], v[1]): k for k, v in actors.ACTORS.items()}
    for rm in BUILT.values():
        for o in rm.objs:
            an = _animator(o)
            if not an:
                continue
            lib = ents._ref_file(o.get("_doc") or rm.d, an["library"])
            clips = [unity.S(c["name"]) for c in tk2d.animation(*lib)["clips"]]
            want = {c for c in clips if c in names}
            if 0 <= an.get("defaultClipId", 0) < len(clips):
                want.add(clips[an.get("defaultClipId", 0)])
            want.discard("")
            if lib in known:
                a = known[lib]
                have = actors.ACTORS[a][2]
                if have is not None:
                    actors.ACTORS[a] = (lib[0], lib[1], sorted(set(have) | want))
                LIBS[lib] = a
            else:
                a = LIBS.get(lib) or "vm%d" % len(LIBS)
                prev = actors.ACTORS.get(a, (lib[0], lib[1], []))[2]
                actors.ACTORS[a] = (lib[0], lib[1], sorted(set(prev) | want))
                LIBS[lib] = a
                known[lib] = a


def _unity_animator(o):
    """its Unity Animator (with a SpriteRenderer it swaps the sprite of), or None"""
    if not any(c["type"] == "SpriteRenderer" for c in o["c"]):
        return None
    return next((c.get("v") for c in o["c"] if c["type"] == "Animator" and c.get("v") and c["v"].get("m_Controller")), None)


OBJ = "<fffffffffHHHHHBBHHhHBBBBBBH"   # x y z, sx sy, its trigger (center from its place, half size), parent, name, sprite,
#                                  first clip, clip map, its count, flags, condition, sorting layer, order, blend, its
#                                  colliders (first, count), its color (r g b a), (pad)
OF_ACTIVE, OF_RENDERER, OF_ANIMATOR, OF_TRIGGER, OF_COLLIDER, OF_ANIM_OFF, OF_WAVE, OF_FADE = 1, 2, 4, 8, 16, 32, 64, 128


def room_blob(name, clip_index, sprites, owners=(), persist=None):
    """A room's VM data (src/vm.c): counts, its objects, their clip maps, its FSMs (definition, owner, name, variables),
    what the save keeps (FSM, slot, its bit: PersistentBoolItem's Activated).
    owners: each collider's (scene, object id): the colliders an object has, the scripts turn on and off with it."""
    import actors, ents, tk2d
    rm = BUILT.get(name)
    if rm is None:
        return b""
    pdf = ents.pd_flags()
    objs, clipmap = bytearray(), bytearray()
    nmap = 0
    for o in rm.objs:
        p = rm.by_id.get(o.get("parent"))
        parent = rm.obj_index.get(p["id"], 0xFFFF) if p is not None else 0xFFFF
        m = np.array(o["m3"]).reshape(3, 3)
        sx = float(np.hypot(m[0, 0], m[1, 0])) * (1 if m[0, 0] >= 0 else -1)
        sy = float(np.hypot(m[0, 1], m[1, 1])) * (1 if m[1, 1] >= 0 else -1)
        fl = (OF_ACTIVE if o.get("self_active", o["active"]) else 0)
        box = None
        # (one its scripts hear the nail hit: its collider, a trigger or not, is what the nail's trigger meets)
        nail = any((a["name"] == "Trigger2dEvent" and dict(a["params"]).get("collideTag") == "Nail Attack") or
                   a["name"] == "ReceivedDamage"
                   for c in o["c"] if c.get("fsm") for s in c["fsm"]["states"] for a in s["actions"]) or \
            any(e == "TAKE DAMAGE" for c in o["c"] if c.get("fsm") for s in c["fsm"]["states"] for e, _ in s["transitions"])
        for c in o["c"]:
            if c["type"] in ("BoxCollider2D", "CircleCollider2D", "PolygonCollider2D") and c.get("v"):
                b = ents._box(o, c)
                if b and (c["v"].get("m_IsTrigger") or nail) and box is None:
                    box = b
                if b and c["v"].get("m_Enabled", 1):
                    fl |= OF_COLLIDER
        if box:
            fl |= OF_TRIGGER
        if any(c.get("class") == "DeactivateAfter2dtkAnimation" for c in o["c"]):
            fl |= OF_ANIM_OFF   # (off once its clip is over)
        bx, by = ((box[0] + box[2]) / 2 - o["pos"][0], (box[1] + box[3]) / 2 - o["pos"][1]) if box else (0, 0)
        wv = next((c.get("v") for c in o["c"] if c.get("class") == "WaveEffectControl" and c.get("v")), None)
        if wv:
            # (WaveEffectControl: it grows and fades as it is on; its speed, its scale)
            fl |= OF_WAVE
            bx, by = wv.get("accelStart", 5), wv.get("scaleMultiplier", 1) * abs(sx) / abs(o["lscale"][0] or 1)
        sf = next((c.get("v") for c in o["c"] if c.get("class") == "SimpleSpriteFade" and c.get("v")), None)
        if sf:
            # (SimpleSpriteFade: from its color to the fade's as it is on, in its time; then off, or back in its pool)
            assert sf.get("fadeInOnStart") and (sf.get("deactivateOnFadeIn") or sf.get("recycleOnFadeIn")), o["path"]
            fl |= OF_FADE
            bx, by = sf.get("fadeDuration", 1), (sf.get("fadeInColor") or {}).get("a", 0)
        bhx, bhy = ((box[2] - box[0]) / 2, (box[3] - box[1]) / 2) if box else (0, 0)
        sprite, first, start, count = 0xFFFF, 0xFFFF, nmap, 0
        an = _animator(o)
        mr = next((c.get("v") for c in o["c"] if c["type"] in ("MeshRenderer", "SpriteRenderer") and isinstance(c.get("v"), dict)), None)
        if mr is not None and mr.get("m_Enabled", 1):
            fl |= OF_RENDERER
        ua = _unity_animator(o)
        if ua:
            # (a Unity Animator: its clip, its first)
            fl |= OF_ANIMATOR
            a, cn = ANIMATORS[ents._ref_file(o.get("_doc") or rm.d, ua["m_Controller"])]
            first = clip_index[actors.clip_id(a, cn)]
        elif an:
            fl |= OF_ANIMATOR
            lib = ents._ref_file(o.get("_doc") or rm.d, an["library"])
            a = LIBS[lib]
            clips = [unity.S(c["name"]) for c in tk2d.animation(*lib)["clips"]]
            dc = an.get("defaultClipId", 0)
            if 0 <= dc < len(clips):
                first = clip_index.get(actors.clip_id(a, clips[dc]), 0xFFFF)
            for cn in clips:
                ci = clip_index.get(actors.clip_id(a, cn))
                if ci is not None and cn in STR.index:
                    clipmap += struct.pack("<HH", STR.index[cn], ci)
                    nmap += 1
                    count += 1
        else:
            sr = next((c.get("v") for c in o["c"] if c["type"] == "SpriteRenderer" and c.get("v")), None)
            if sr and sr.get("m_Sprite"):
                # (its own size, drawn at its scale: its texture as fine as it is drawn)
                sprite = sprites.id(o.get("_doc") or rm.d, sr["m_Sprite"], 1.0, max(abs(sx), abs(sy)), world=True)
        # (DeactivateIfPlayerdataTrue, False: two at most)
        conds = [pdf[bn] | (0x8000 if off_if else 0) for bn, off_if in state.conditions(o) if bn in pdf]
        conds = sorted(set(conds), key=conds.index)
        if o.get("_variant"):
            # (a scene loaded with the room's by a PlayerData bool: there only when it has that value)
            bn, val = o["_variant"]
            assert not conds, o["path"]
            conds = [pdf[bn] | (0 if val else 0x8000)]
        assert len(conds) <= 2, o["path"]
        cond, cond2 = (conds + [0xFFFF, 0xFFFF])[:2]
        cols = [k for k, ow in enumerate(owners) if k and ow == (o.get("_scene", name), o.get("_raw", o["id"]))]
        assert cols == list(range(cols[0], cols[-1] + 1)) if cols else True, o["path"]
        assert len(cols) < 256 and (not cols or cols[-1] < 256), o["path"]
        layer, order = ents._sorting(o)
        # (its material's blend: pack.BLENDS)
        import scene
        blend = 0
        if mr is not None and mr.get("m_Materials"):
            m0 = mr["m_Materials"][0]
            if m0 and m0[1]:
                doc = o.get("_doc") or rm.d
                blend = {"alpha": 0, "add": 1, "screen": 2, "linearlight": 3, "overlay": 4, "multiply": 5}[
                    scene.blend_of(unity.material(unity.ref_path(doc, m0[0]), m0[1])[1])]
        # (tink_effect: the nail clinks off it, the Knight recoils: blend's high bit)
        if any(c.get("fsm") and (c["fsm"]["name"] == "tink_effect" or _tinks(c["fsm"])) for c in o["c"]):
            blend |= 0x8000
        col = (mr or {}).get("m_Color") or {"r": 1, "g": 1, "b": 1, "a": 1}
        rgba = [max(0, min(255, int(round(col[k] * 255)))) for k in "rgba"]
        objs += struct.pack(OBJ, o["pos"][0], o["pos"][1], o["pos"][2], sx, sy, bx, by, bhx, bhy, parent,
                            STR.id(o["name"]), sprite, first, start, min(count, 255), fl, cond, layer, order, blend,
                            cols[0] if cols else 0, len(cols), *rgba, cond2)
    fsms = bytearray()
    for di, owner, init, fname in rm.fsms:
        fsms += struct.pack("<HHHH", di, owner, fname, len(init)) + b"".join(struct.pack("<I", v) for v in init)
    for fi, slot, o, *semi in rm.persist:
        bits = 3 if slot & 0x80 else 1
        fsms += struct.pack("<BBH", fi, slot, persist.id(o.get("_scene", name), o["path"], bits, semi[0] if semi else False))
    # (folded sprites: their object, sprite, place from it, depth, scale, sorting, color, blend)
    folds = struct.pack("<H", len(rm.folds))
    for i, q in rm.folds:
        o = rm.objs[i]
        m = np.array(q["m3"]).reshape(3, 3)
        sx = float(np.hypot(m[0, 0], m[1, 0])) * (1 if m[0, 0] >= 0 else -1)
        sy = float(np.hypot(m[0, 1], m[1, 1])) * (1 if m[1, 1] >= 0 else -1)
        sr = next(c["v"] for c in q["c"])
        spr = sprites.id(q.get("_doc") or rm.d, sr["m_Sprite"], 1.0, max(abs(sx), abs(sy)), world=True)
        layer, order = ents._sorting(q)
        import scene
        blend = 0
        m0 = (sr.get("m_Materials") or [None])[0]
        if m0 and m0[1]:
            doc = q.get("_doc") or rm.d
            blend = {"alpha": 0, "add": 1, "screen": 2, "linearlight": 3, "overlay": 4, "multiply": 5}[
                scene.blend_of(unity.material(unity.ref_path(doc, m0[0]), m0[1])[1])]
        col = sr.get("m_Color") or {"r": 1, "g": 1, "b": 1, "a": 1}
        rgba = [max(0, min(255, int(round(col[k] * 255)))) for k in "rgba"]
        folds += struct.pack("<BBhfffffHhBBBBHH", i, 0, spr, q["pos"][0] - o["pos"][0], q["pos"][1] - o["pos"][1], q["pos"][2],
                             sx, sy, layer, order, *rgba, blend, 0)
    # (ColorFader: the objects that have one: its down and up colors' alphas, their times and the up's delay, in 50ths
    # of a second)
    faders = [(i, c["v"]) for i, o in enumerate(rm.objs) for c in o["c"] if c.get("class") == "ColorFader" and c.get("v")]
    assert len(faders) < 256 and all(i < 256 for i, _ in faders), name
    A = lambda col: max(0, min(255, int(round((col or {}).get("a", 1) * 255))))
    T = lambda t: max(0, min(255, int(round((t or 0) * 50))))
    folds += struct.pack("<H", len(faders)) + b"".join(
        struct.pack("<BBBBBB", i, A(v.get("downColour")), A(v.get("upColour")), T(v.get("downTime")), T(v.get("upTime")),
                    T(v.get("upDelay"))) for i, v in faders)
    head = struct.pack("<HHHH", len(rm.objs), len(rm.fsms), nmap, len(rm.persist))
    return bytes(head + objs + clipmap + fsms + folds)


def tables(clip_index, prompt_text):
    """VMDEF: the definitions' offsets and the definitions; per string: its title (255 none), its prompt's text
    (0xFFFF none), the Knight's clip of that name (0xFFFF none)."""
    import actors
    defs = bytearray()
    offs = []
    for b in DEFS:
        while len(defs) & 3:
            defs.append(0)
        offs.append(len(defs))
        defs += b
    titles = bytes(text.TITLES.index(s) if s in text.TITLES else 255 for s in STR.list)
    pdb = b"".join(struct.pack("<H", _pd_code(s)) for s in STR.list)
    prompts = b"".join(struct.pack("<H", prompt_text.get(s, 0xFFFF)) for s in STR.list)
    knight = b"".join(struct.pack("<H", clip_index.get(actors.clip_id("knight", s), 0xFFFF)) for s in STR.list)
    head = struct.pack("<II", len(DEFS), len(STR.list))
    base = len(head) + 4 * len(DEFS)
    body = head + b"".join(struct.pack("<I", base + o) for o in offs) + bytes(defs)
    while len(body) & 3:
        body += b"\0"
    return body + titles + (b"\0" if len(titles) & 1 else b"") + prompts + knight + pdb


def _pd_code(name):
    """a string as a PlayerData bool's name -> its flag (0xFFFE always false, 0xFFFD always true, 0xFFF0... kept elsewhere,
    0xFFFF not one)"""
    import ents
    pdf = ents.pd_flags()
    if name in pdf:
        return pdf[name]
    if name in PD_SPECIAL:
        return PD_SPECIAL[name]
    if name in PD_CONST:
        return 0xFFFD if PD_CONST[name] else 0xFFFE
    if name in state.CONSTANT:
        return 0xFFFD if state.CONSTANT[name] else 0xFFFE
    return 0xFFFF


# ---------------------------------------------------------------- the states that can be reached (in this part of the
# game): PlayerData bools and ints that never change here decide some tests for good; a state's transition on an event
# its own actions send only counts if one of them can send it
PD_INT_CONST = {"permadeathMode": 0, "quakeLevel": 0, "screamLevel": 0}


def owned(room):
    """the room's objects the scripts have (and draw): (scene, object id)"""
    rm = BUILT.get(room)
    return [(o.get("_scene", room), o.get("_raw", o["id"])) for o in rm.objs + [q for _, q in rm.folds]] if rm else []


def _const_bool(name):
    if name in PD_CONST:
        return PD_CONST[name]
    if name in state.CONSTANT:
        return state.CONSTANT[name]
    return None


def _const_vars(f, external=()):
    """its bool variables nothing changes (none of its actions write them, no other FSM sets them), and its int
    variables only read: their values"""
    written = set(external)
    for st in f["states"]:
        for a in st["actions"]:
            for k, v in a["params"]:
                if isinstance(v, str) and v.startswith("$") and (k.startswith("store") or k.endswith("Bool") and k != "boolName" or
                                                                 (a["name"] in ("SetBoolValue", "BoolFlip") and k == "boolVariable") or
                                                                 k in _outputs(a["name"])):
                    written.add(v[1:])
    out = {n: bool(val) for n, (t, val) in f["vars"].items() if t == "bool" and n not in written}
    _, _, inline = _layout(f, external)
    out.update({n: int(f["vars"][n][1] or 0) for n in inline if f["vars"][n][0] == "int"})
    return out


def _outputs(name):
    """an action's parameters it writes (its op's stores: F, I, B, S, O, V3)"""
    return {k for k, kind in OPS.get(name, (0, ()))[1] if kind in ("F", "I", "B", "S", "O", "V3")}


def _events_of_state(f, s, consts=None, where=False):
    """the events this state's actions can send it (None: any of the transitions': unknown)"""
    known = dict(consts or {})   # (variables whose value is known here)
    takes = {e for e, _ in s["transitions"]} | {e for e, _ in f["global"]}
    out = set()

    def ev(v):
        return v[1] if isinstance(v, (list, tuple)) and v and v[0] == "event" else None

    def var(v):
        return v[1:] if isinstance(v, str) and v.startswith("$") else None

    def bval(v):
        if isinstance(v, bool):
            return v
        n = var(v)
        return known.get(n) if n else None

    for ai, a in enumerate(s["actions"]):
        if not a.get("enabled", True):
            continue
        P = dict(a["params"])
        n = a["name"]
        if n == "GetPlayerDataBool":
            c = _const_bool(P.get("boolName"))
            if var(P.get("storeValue")):
                known[var(P.get("storeValue"))] = c
            continue
        if n == "GetPlayerDataInt":
            if var(P.get("storeValue")):
                known[var(P.get("storeValue"))] = PD_INT_CONST.get(P.get("intName"))
            continue
        if n == "PlayerDataBoolTest":
            c = _const_bool(P.get("boolName"))
            for e, want in ((ev(P.get("isTrue")), True), (ev(P.get("isFalse")), False)):
                if e and (c is None or c == want):
                    out.add(e)
                    if c is not None and not P.get("everyFrame") and e in takes:
                        # (sent for sure, and taken: the state's actions stop there)
                        return (out, True, ai) if where else (out, True)
            continue
        if n == "PlayerDataBoolTrueAndFalse":
            t, fl = _const_bool(P.get("trueBool")), _const_bool(P.get("falseBool"))
            can_true = (t is None or t) and (fl is None or not fl)
            if ev(P.get("isTrue")) and can_true:
                out.add(ev(P.get("isTrue")))
            if ev(P.get("isFalse")):
                out.add(ev(P.get("isFalse")))
            continue
        if n == "BoolTest":
            b = bval(P.get("boolVariable"))
            for e, want in ((ev(P.get("isTrue")), True), (ev(P.get("isFalse")), False)):
                if e and (b is None or b == want):
                    out.add(e)
                    if b is not None and not P.get("everyFrame") and e in takes:
                        return (out, True, ai) if where else (out, True)
            continue
        if n == "BoolTestMulti":
            vals = [bval(v) for v in P.get("boolVariables") or []]
            states = P.get("boolStates") or []
            sure_false = any(v is not None and v != w for v, w in zip(vals, states))
            if ev(P.get("trueEvent")) and not sure_false:
                out.add(ev(P.get("trueEvent")))
            if ev(P.get("falseEvent")):
                out.add(ev(P.get("falseEvent")))
            continue
        if n in ("BoolAllTrue",):
            vals = [bval(v) for v in P.get("boolVariables") or []]
            if ev(P.get("sendEvent")) and not any(v is False for v in vals):
                out.add(ev(P.get("sendEvent")))
            continue
        if n == "IntCompare":
            a1 = known.get(var(P.get("integer1"))) if var(P.get("integer1")) else P.get("integer1")
            a2 = known.get(var(P.get("integer2"))) if var(P.get("integer2")) else P.get("integer2")
            for e, test in ((ev(P.get("equal")), lambda x, y: x == y), (ev(P.get("lessThan")), lambda x, y: x < y),
                            (ev(P.get("greaterThan")), lambda x, y: x > y)):
                if e and (a1 is None or a2 is None or test(a1, a2)):
                    out.add(e)
                    if a1 is not None and a2 is not None and not P.get("everyFrame") and e in takes:
                        return (out, True, ai) if where else (out, True)
            continue
        if n == "IntSwitch":
            x = known.get(var(P.get("intVariable"))) if var(P.get("intVariable")) else P.get("intVariable")
            for c, e in zip(P.get("compareTo") or [], P.get("sendEvent") or []):
                e = ev(e)
                c = known.get(var(c)) if var(c) else c
                if e and (x is None or c is None or x == c):
                    out.add(e)
                    if x is not None and c is not None and not P.get("everyFrame") and e in takes:
                        return (out, True, ai) if where else (out, True)
                if x is not None and c is not None and x == c:
                    break
            continue
        if n in ("IntCompareToBool", "IntTestToBool"):
            k1, k2 = ("integer1", "integer2") if n == "IntCompareToBool" else ("int1", "int2")
            a1 = known.get(var(P.get(k1))) if var(P.get(k1)) else P.get(k1)
            a2 = known.get(var(P.get(k2))) if var(P.get(k2)) else P.get(k2)
            for k, test in (("equalBool", lambda x, y: x == y), ("lessThanBool", lambda x, y: x < y),
                            ("greaterThanBool", lambda x, y: x > y)):
                if var(P.get(k)):
                    known[var(P.get(k))] = test(a1, a2) if a1 is not None and a2 is not None else None
            continue
        if n == "SetBoolValue":
            if var(P.get("boolVariable")):
                known[var(P.get("boolVariable"))] = bval(P.get("boolValue"))
            continue
        # anything else: whatever events it names (what it writes, no longer known)
        for k in _outputs(n):
            if var(P.get(k)):
                known[var(P.get(k))] = None
        for k, v in a["params"]:
            if isinstance(v, (list, tuple)) and v and v[0] == "event":
                out.add(v[1])
            elif isinstance(v, list):
                for q in v:
                    if isinstance(q, (list, tuple)) and q and q[0] == "event":
                        out.add(q[1])
            if k == "sendEvent" and isinstance(v, str):
                out.add(v)
    return (out, False, len(s["actions"])) if where else (out, False)


def reachable(f, external=()):
    """the names of the FSM's states that can be entered (external: the variables other FSMs set)"""
    consts = _const_vars(f, external)
    local = set()
    for s in f["states"]:
        for a in s["actions"]:
            for k, v in a["params"]:
                if isinstance(v, (list, tuple)) and v and v[0] == "event":
                    local.add(v[1])
                elif isinstance(v, list):
                    for q in v:
                        if isinstance(q, (list, tuple)) and q and q[0] == "event":
                            local.add(q[1])
    local.discard("FINISHED")
    by = {s["name"]: s for s in f["states"]}
    todo = [f["start"]] + [to for _, to in f["global"]]
    seen = set()
    while todo:
        n = todo.pop()
        if n in seen or n not in by:
            continue
        seen.add(n)
        s = by[n]
        can, sure = _events_of_state(f, s, consts)
        if any(dict(a["params"]).get("gameObject") == "$DialogueTextYN" for a in s["actions"]):
            can = set(can) | {"YES", "NO"}   # (the yes or no box's answer, to who asked)
        for e, to in s["transitions"]:
            if e in can or (e not in local and not (sure and e == "FINISHED")):
                todo.append(to)
    return seen
