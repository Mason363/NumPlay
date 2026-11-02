#!/usr/bin/env python3
"""The original's tables, read from its own scripts in data.win -> src/tables.c.

    python3 -I tools/tables.py REF        (REF: the folder with data.win)

The scripts are switches on their argument (scr_iteminfo, scr_weaponinfo, scr_armorinfo, scr_spellinfo,
scr_encountersetup) or chains of ifs (scr_monstersetup): each case's assignments become a row. Texts are
kept as the ids of the original's texts (data.h names them the same way)."""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gm as GM  # noqa: E402

OUT = os.path.join(HERE, "..", "src", "tables.c")
LANG = re.compile(r'scr_84_get_lang_string\("([^"]+)"\)')


def switch_cases(text, var="argument0"):
    """{case value: [(lhs, rhs), ...]} of a switch on var"""
    lines = text.split("\n")
    cases = {}
    for ln in lines:
        m = re.match(r"if \(%s == (\d+)\) goto L(\d+)$" % re.escape(var), ln)
        if m:
            cases.setdefault(int(m.group(1)), int(m.group(2)))
    labels = {}
    for i, ln in enumerate(lines):
        m = re.match(r"L(\d+):$", ln)
        if m:
            labels[int(m.group(1))] = i
    out = {}
    for v, lab in cases.items():
        i = labels[lab] + 1
        rows = []
        while i < len(lines):
            ln = lines[i]
            if ln.startswith("goto ") or (re.match(r"L\d+:$", ln) and int(ln[1:-1]) in cases.values() and int(ln[1:-1]) != lab):
                break
            m = re.match(r"([\w.\[\]() *+]+?) = (.+)$", ln)
            if m:
                rows.append((m.group(1), m.group(2)))
            i += 1
        out[v] = rows
    return out


def if_chain(text, cond):
    """{value: [(lhs, rhs)]} of a chain 'if !(cond == N) goto Lx ... Lx:'"""
    lines = text.split("\n")
    out = {}
    i = 0
    while i < len(lines):
        m = re.match(r"if !\(%s == (\d+)\) goto L(\d+)$" % re.escape(cond), lines[i])
        if m:
            v, end = int(m.group(1)), "L%s:" % m.group(2)
            rows = []
            i += 1
            while i < len(lines) and lines[i] != end:
                a = re.match(r"([\w.\[\]() *+]+?) = (.+)$", lines[i])
                if a:
                    rows.append((a.group(1), a.group(2)))
                i += 1
            out[v] = rows
        i += 1
    return out


def num(expr, **env):
    e = expr
    for k, v in env.items():
        e = re.sub(r"\b%s\b" % k, str(v), e)
    if not re.fullmatch(r"[\d\s.+\-*/()]+", e):
        return None
    try:
        return eval(e, {"__builtins__": {}})
    except Exception:
        return None


def lang(expr):
    m = LANG.search(expr)
    return m.group(1) if m else None


def field(rows, name, default=0, last=True):
    val = default
    for lhs, rhs in rows:
        if lhs == name:
            val = rhs
            if not last:
                break
    return val


def cstr(key):
    return key if key else "0xFFFF"


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    g = GM.GM(os.path.join(sys.argv[1], "data.win"))
    code = lambda n: g.decompile(n)
    out = ["/* made by tools/tables.py from the original's scripts: items, weapons, armors, spells, enemies and",
           " * battles (see tables.h) */", '#include "data.h"', '#include "tables.h"', ""]

    # items: scr_iteminfo
    items = switch_cases(code("gml_Script_scr_iteminfo"))
    n = max(items) + 1
    out.append("const ItemDef item_defs[%d] = {" % n)
    for i in range(n):
        r = items.get(i, [])
        out.append("  {%s, %s, %d, %d, %d}," % (
            cstr(lang(field(r, "itemnameb", ""))), cstr(lang(field(r, "itemdescb", ""))),
            num(str(field(r, "itemtarget", 0))) or 0, num(str(field(r, "value", 0))) or 0,
            num(str(field(r, "usable", 0))) or 0))
    out += ["};", "const int item_count = %d;" % n, ""]

    # weapons and armors
    for kind, script, p in (("weapon", "gml_Script_scr_weaponinfo", "weapon"), ("armor", "gml_Script_scr_armorinfo", "armor")):
        cases = switch_cases(code(script))
        n = max(cases) + 1
        out.append("const EquipDef %s_defs[%d] = {" % (kind, n))
        for i in range(n):
            r = cases.get(i, [])
            v = lambda k: num(str(field(r, p + k + "temp", 0))) or 0
            out.append("  {%s, %s, %d, %d, %d, %d, %d, %d, %d, %d}," % (
                cstr(lang(field(r, p + "nametemp", ""))), cstr(lang(field(r, p + "desctemp", ""))),
                v("at"), v("df"), v("mag"), v("char1"), v("char2"), v("char3"), v("icon"),
                num(str(field(r, "value", 0))) or 0))
        out += ["};", "const int %s_count = %d;" % (kind, n), ""]

    # spells
    sp = switch_cases(code("gml_Script_scr_spellinfo"))
    n = max(sp) + 1
    out.append("const SpellDef spell_defs[%d] = {" % n)
    for i in range(n):
        r = sp.get(i, [])
        out.append("  {%s, %s, %s, %d, %d, %d}," % (
            cstr(lang(field(r, "spellnameb", ""))), cstr(lang(field(r, "spelldescb", ""))),
            cstr(lang(field(r, "spellname", ""))),
            num(str(field(r, "cost", -1))) or 0, num(str(field(r, "spelltarget", 0))) or 0,
            num(str(field(r, "spellusable", 0))) or 0))
    out += ["};", "const int spell_count = %d;" % n, ""]

    # enemies: scr_monstersetup
    mons = if_chain(code("gml_Script_scr_monstersetup"), "global.monstertype[myself]")
    n = max(mons) + 1
    out.append("const MonsterDef monster_defs[%d] = {" % n)
    for i in range(n):
        r = mons.get(i, [])
        g_ = lambda k, d=0: num(str(field(r, "global.%s[myself]" % k, d))) or 0
        acts = []
        for k in range(6):
            nm = lang(field(r, "global.actname[((myself * 32000) + %d)]" % k, ""))
            can = num(str(field(r, "global.canact[((myself * 32000) + %d)]" % k, 0))) or 0
            desc = lang(field(r, "global.actdesc[((myself * 32000) + %d)]" % k, ""))
            cost = num(str(field(r, "global.actcost[((myself * 32000) + %d)]" % k, 0))) or 0
            actor = num(str(field(r, "global.actactor[((myself * 32000) + %d)]" % k, 1))) or 1
            acts.append("{%s, %s, %d, %d, %d}" % (cstr(nm), cstr(desc), can, cost, actor))
        out.append("  {%s, %d, %d, %d, %d, %d, %d, %d, %d, %s,\n   {%s}}," % (
            cstr(lang(field(r, "global.monstername[myself]", ""))), g_("monstermaxhp"), g_("monsterat"),
            g_("monsterdf"), g_("monsterexp"), g_("monstergold"), g_("sparepoint"), g_("mercymax"),
            g_("mercymod"), cstr(lang(field(r, "global.battlemsg[0]", ""))), ", ".join(acts)))
    out += ["};", "const int monster_count = %d;" % n, ""]

    objs = [o["name"] for o in g.objects]
    # battles: scr_encountersetup (what each encounter places, around the view's top-left)
    enc = switch_cases(code("gml_Script_scr_encountersetup"))
    n = max(enc) + 1
    out.append("const EncounterDef encounter_defs[%d] = {" % n)
    for i in range(n):
        r = enc.get(i, [])
        ms = []
        for k in range(3):
            t = num(str(field(r, "global.monstertype[%d]" % k, 0))) or 0
            it = num(str(field(r, "global.monsterinstancetype[%d]" % k, -1)))
            mx = num(str(field(r, "global.monstermakex[%d]" % k, 0)), xx=0, yy=0) or 0
            my = num(str(field(r, "global.monstermakey[%d]" % k, 0)), xx=0, yy=0) or 0
            ms.append("{%d, %s, %d, %d}" % (t, objs[int(it)] if it is not None and it >= 0 else "-1", mx, my))
        out.append("  {{%s}, %s}, /* %s */" % (", ".join(ms), cstr(lang(field(r, "global.battlemsg[0]", ""))),
                                              " ".join(objs[int(num(str(field(r, "global.monsterinstancetype[%d]" % k, 0))) or 0)]
                                                       for k in range(3)
                                                       if (num(str(field(r, "global.monstertype[%d]" % k, 0))) or 0))))
    out += ["};", "const int encounter_count = %d;" % n, ""]
    with open(OUT, "w") as f:
        f.write("\n".join(out))
    print("tables.c: %d items, %d spells, %d enemies, %d battles" % (len(items), len(sp), len(mons), len(enc)))


if __name__ == "__main__":
    main()
