"""PlayerData as the scenes see it when they load (DeactivateIfPlayerdataTrue, DeactivateIfPlayerdataFalse): the bools
that keep the value they start with all through this part of the game turn their objects off for good here; those that
change are left to the room as it loads (ents.py: OK_COND)."""

# (as a new game has them, and as they stay up to Hornet)
CONSTANT = {"crossroadsInfected": False, "troupeInTown": False, "clothInTown": False, "killedTraitorLord": False,
            "clothKilled": False, "clothLeftTown": False, "brettaLeftTown": False, "zoteDefeated": False,
            "brettaRescued": False, "visitedRuins": False, "visitedDeepnest": False, "colosseumSilverOpened": False,
            "nymmInTown": False, "killedGrimm": False, "divineInTown": False, "fatGrubKing": False,
            "quirrelArchiveEncountered": False}
# (the Grimm Troupe's: none of it comes this early)
CONSTANT.update({"defeatedNightmareGrimm": False, "equippedCharm_40": False, "gotCharm_40": False})
CONSTANT_INTS = {"flamesCollected": 0, "flamesRequired": 3, "grimmChildLevel": 1}
DYNAMIC = {"visitedDirtmouth", "tisoEncounteredTown", "hasDash", "hornet1Defeated", "shamanPillar", "openedTownBuilding",
           "slyRescued", "openedMapperShop", "quirrelMantisEncountered", "enteredMantisLordArea", "defeatedMantisLords"}


def conditions(o):
    """An object's DeactivateIfPlayerdata components (and what its parent's Check Opened makes of it) -> [(bool name,
    off if true (else off if false))]."""
    out = list(o.get("_conds", []))
    for c in o["c"]:
        cl = c.get("class") or ""
        if cl in ("DeactivateIfPlayerdataTrue", "DeactivateIfPlayerdataFalse"):
            out.append(((c.get("v") or {}).get("boolName"), cl.endswith("True")))
    return out


def _check_opened(o, by):
    """Check Opened (Dirtmouth's buildings): its "open" child on with the bool, its "closed" one off"""
    f = next((c["fsm"] for c in o["c"] if c.get("fsm") and c["fsm"]["name"] == "Check Opened"), None)
    if f is None:
        return
    test = next(a for st in f["states"] for a in st["actions"] if a["name"] == "PlayerDataBoolTest")
    name = dict(test["params"])["boolName"]
    for ci in o.get("children", []):
        q = by[ci]
        if q["name"] in ("open", "closed"):
            q["_conds"] = [(name, q["name"] == "closed")]


# (actions that wait: their state finishes later, by FINISHED)
_WAITS = {"Wait", "NextFrameEvent", "WaitForHeroInPosition", "WaitForFinishedEnteringScene"}


def _event(v):
    return v[1] if isinstance(v, (list, tuple)) and len(v) == 2 and v[0] == "event" else None


def scripted_off(o):
    """What an object's own scripts turn off as its scene starts, whatever the Knight does, from the constant bools
    (a PlayerData bool test, then ActivateGameObject or ActivateAllChildren on itself, or DestroySelf, as the Grimm
    Troupe's tents and Flamebearers have it): "self", "children" or None. Followed while each step is certain; its
    children off only if the script then rests where nothing else can come."""
    out = None
    for c in o["c"]:
        f = c.get("fsm")
        if not f or not c.get("enabled", True):
            continue
        vars_ = {k: v[1] for k, v in (f.get("vars") or {}).items()}
        states = {st["name"]: st for st in f["states"]}

        def val(v):
            return vars_.get(v[1:]) if isinstance(v, str) and v.startswith("$") else v

        def me(v):
            return v == "Owner" or (isinstance(v, str) and v.startswith("$") and vars_.get(v[1:]) == "<self>")
        st, seen, children_off = states.get(f["start"]), set(), False
        while st is not None and st["name"] not in seen:
            seen.add(st["name"])
            ev, stop = None, False
            for a in st["actions"]:
                n, P = a["name"], dict(a.get("params") or [])
                if n == "GetOwner":
                    vars_[str(P.get("storeGameObject", ""))[1:]] = "<self>"
                elif n == "SetBoolValue":
                    vars_[str(P.get("boolVariable", ""))[1:]] = P.get("boolValue")
                elif n == "GetPlayerDataInt":
                    name = val(P.get("intName"))
                    if name not in CONSTANT_INTS:
                        stop = True
                        break
                    vars_[str(P.get("storeValue", ""))[1:]] = CONSTANT_INTS[name]
                elif n == "PlayerDataBoolTest":
                    name = val(P.get("boolName"))
                    if name not in CONSTANT:
                        stop = True
                        break
                    ev = _event(P.get("isTrue" if CONSTANT[name] else "isFalse"))
                elif n == "PlayerDataBoolTrueAndFalse":
                    t, fb = val(P.get("trueBool")), val(P.get("falseBool"))
                    if CONSTANT.get(t) is False or CONSTANT.get(fb) is True:
                        ev = _event(P.get("isFalse"))
                    elif t in CONSTANT and fb in CONSTANT:
                        ev = _event(P.get("isTrue"))
                    else:
                        stop = True
                        break
                elif n == "BoolTest":
                    v = val(P.get("boolVariable"))
                    if not isinstance(v, bool) or P.get("everyFrame"):
                        stop = True
                        break
                    ev = _event(P.get("isTrue" if v else "isFalse"))
                elif n == "IntCompare":
                    a1, a2 = val(P.get("integer1")), val(P.get("integer2"))
                    if not isinstance(a1, int) or not isinstance(a2, int) or P.get("everyFrame"):
                        stop = True
                        break
                    ev = _event(P.get("equal" if a1 == a2 else "lessThan" if a1 < a2 else "greaterThan"))
                elif n == "DestroySelf" or (n == "ActivateGameObject" and me(P.get("gameObject")) and
                                            P.get("activate") is False):
                    return "self"
                elif n == "ActivateAllChildren" and me(P.get("gameObject")):
                    children_off = P.get("activate") is False
                elif n in _WAITS:
                    if n == "Wait" and not (isinstance(P.get("time"), (int, float)) and P["time"] <= 0.5):
                        stop = True   # (on for a while first: as it is)
                        break
                elif any(_event(v) for v in P.values()):
                    stop = True   # (an event this does not follow)
                    break
                if ev:
                    break
            if stop:
                break
            if ev is None:
                ev = "FINISHED"
            to = next((t for e, t in st.get("transitions") or [] if e == ev), None)
            if to is None:
                # (at rest: its other events only its own tests send)
                own = {_event(v) for a in st["actions"] for v in dict(a.get("params") or []).values()}
                if children_off and all(e in own for e, _ in st.get("transitions") or []):
                    out = "children"
                break
            st = states.get(to)
    return out


def apply(d):
    """Turns off (with what is under them) the scene's objects that the constant bools turn off."""
    by = {o["id"]: o for o in d["objects"]}
    for o in d["objects"]:
        _check_opened(o, by)
    for o in d["objects"]:
        for name, off_if in conditions(o):
            if name in CONSTANT:
                if CONSTANT[name] == off_if:
                    todo = [o["id"]]
                    while todo:
                        q = by[todo.pop()]
                        q["active"] = False
                        todo += q.get("children", [])
            elif name not in DYNAMIC:
                print("state: %s: %s not known" % (o["path"], name))
    for o in d["objects"]:
        if not o["active"]:
            continue
        off = scripted_off(o)
        if off:
            todo = [o["id"]] if off == "self" else list(o.get("children", []))
            while todo:
                q = by[todo.pop()]
                q["active"] = False
                todo += q.get("children", [])
    return d
