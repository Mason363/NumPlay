#!/usr/bin/env python3
"""The original's text-choosing code, read from data.win and turned into C -> src/gmtexts.c.

    python3 -I tools/texts.py REF        (REF: the folder with data.win)

What a sign, a bed or a character says depends on the room, where it stands, the story so far: the
original decides it in long chains of ifs (obj_readable_room1, obj_npc_room, scr_text...). Those are
turned into C as they are (their ifs and gotos kept), with its texts as ids and its globals as the
game's (game.h). A statement the game has no use for is kept as a comment."""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import gm as GM  # noqa: E402

OUT = os.path.join(HERE, "..", "src", "gmtexts.c")
# code entry -> C function
ENTRIES = [
    ("gml_Object_obj_readable_room1_Other_10", "gm_readable_room1"),
    ("gml_Object_obj_npc_room_Create_0", "gm_npc_room_create"),
    ("gml_Object_obj_npc_room_Other_10", "gm_npc_room_talk"),
    ("gml_Object_obj_npc_sign_Create_0", "gm_npc_sign_create"),
    ("gml_Object_obj_npc_sign_Other_10", "gm_npc_sign_talk"),
    ("gml_Object_obj_npc_facing_Create_0", "gm_npc_facing_create"),
    ("gml_Object_obj_npc_facing_Other_10", "gm_npc_facing_talk"),
    ("gml_Script_scr_text", "gm_scr_text"),
]
LOCALS = {"tempvar", "afford", "itemflag", "itemtype", "t_itemid"}
FACES = {"scr_susface": "S", "scr_ralface": "R", "scr_noeface": "N", "scr_torface": "T", "scr_lanface": "L",
         "scr_rudface": "r", "scr_asgface": "A", "scr_kingface": "K", "scr_rurusface": "u"}
VARS = [
    (r"\bglobal\.plot\b", "sv.plot"), (r"\bglobal\.flag\[", "sv.flag["), (r"\bglobal\.tempflag\[", "c->tempflag["),
    (r"\bglobal\.filechoice\b", "0"), (r"\bglobal\.darkzone\b", "sv.darkzone"), (r"\bglobal\.gold\b", "sv.gold"),
    (r"\bglobal\.lgold\b", "sv.lgold"), (r"\bglobal\.hp\[", "sv.hp["), (r"\bglobal\.maxhp\[", "sv.maxhp["),
    (r"\bglobal\.char\[", "sv.chr["), (r"\bglobal\.choice\b", "c->choice"), (r"\bglobal\.lweapon\b", "sv.lweapon"),
    (r"\bglobal\.larmor\b", "sv.larmor"), (r"\bglobal\.facing\b", "c->facing"), (r"\bglobal\.item\[", "sv.item["),
    (r"\bglobal\.keyitem\[", "sv.keyitem["), (r"\bglobal\.lv\b", "sv.lv"), (r"\bglobal\.litem\[", "sv.litem["),
    (r"\bargument0\b", "c->arg"), (r"\bargument1\b", "c->arg1"), (r"\bglobal\.msc\b", "c->msc"),
    (r"\bscr_havechar\(", "have_char("), (r"\bscr_itemcheck\(", "gm_itemcheck("),
    (r"\bscr_keyitemcheck\(", "gm_keyitemcheck("), (r"\bscr_litemcheck\(", "gm_litemcheck("),
    (r"\bscr_weaponcheck_inventory\(", "gm_weaponcheck("), (r"\broom_width\b", "world_room_info()->w"),
    (r"\broom_height\b", "world_room_info()->h"), (r"\bx\b", "c->x"), (r"\by\b", "c->y"), (r"\bread\b", "c->read"),
    (r"\btalked\b", "c->talked"), (r"\bimage_index\b", "c->img"), (r"\bsprite_index\b", "c->spr"),
]


def load_names(g):
    return ([s["name"] for s in g.sprites], [r["name"] for r in g.rooms], [o["name"] for o in g.objects])


class Tr:
    def __init__(self, g):
        self.spr, self.rooms, self.objs = load_names(g)
        self.unknown = 0
        self.locals = set()

    def expr(self, e):
        """an expression in C, or None when it uses what the game hasn't got"""
        e = re.sub(r"\broom (==|!=|<|>|<=|>=) (\d+)\b", lambda m: "g_room %s %s" % (m.group(1), self.rooms[int(m.group(2))]), e)
        e = re.sub(r'scr_84_get_lang_string\("([^"]+)"\)', r"\1", e)
        for pat, rep in VARS:
            e = re.sub(pat, rep, e)
        e = e.replace("\\", "\\\\")
        # anything left that looks like a name the game hasn't got
        rest = re.sub(r'"[^"]*"', "", e)
        rest = re.sub(r"->\w+|sv\.\w+", "", rest)
        for w in re.findall(r"\b[A-Za-z_][A-Za-z0-9_.]*\b", rest):
            if w in ("g_room", "have_char", "gm_itemcheck", "gm_keyitemcheck", "gm_litemcheck", "gm_weaponcheck",
                     "world_room_info") or (w.startswith("room_") and w in self.rooms):
                continue
            if w in self.locals:
                continue
            if w in ("abs", "floor", "round"):
                continue
            if re.match(r"^[a-z0-9_]+_slash_", w):
                continue
            return None
        return e

    def stmt(self, ln, labels_used):
        m = re.match(r"if !(.+) goto (L\d+)$", ln)
        if m:
            c = self.expr(m.group(1))
            return "if (!%s) goto %s;" % (c, m.group(2)) if c is not None else "goto %s; /* gml: %s */" % (m.group(2), ln)
        m = re.match(r"if (.+) goto (L\d+)$", ln)
        if m:
            c = self.expr(m.group(1))
            return "if (%s) goto %s;" % (c, m.group(2)) if c is not None else "/* gml: %s */" % ln
        m = re.match(r"goto (L\d+)$", ln)
        if m:
            return "goto %s;" % m.group(1)
        m = re.match(r"(L\d+):$", ln)
        if m:
            return "%s:;" % m.group(1) if m.group(1) in labels_used else None
        m = re.match(r'global\.msg\[(\d+)\] = scr_84_get_lang_string\("([^"]+)"\)$', ln)
        if m:
            return "MSG(%s, %s);" % (m.group(1), m.group(2))
        m = re.match(r'global\.msg\[(\d+)\] = scr_84_get_subst_string\(scr_84_get_lang_string\("([^"]+)"\), (.+)\)$', ln)
        if m:
            return "MSG(%s, %s); /* gml: %s */" % (m.group(1), m.group(2), ln)
        m = re.match(r'global\.(msg|choicemsg)\[(\d+)\] = "([^"]*)"$', ln)
        if m:
            return ('txt_msg(c, %s, "%s");' if m.group(1) == "msg" else 'c->choicemsg[%s] = "%s";') % (m.group(2), m.group(3))
        m = re.match(r'global\.choicemsg\[(\d+)\] = scr_84_get_lang_string\("([^"]+)"\)$', ln)
        if m:
            return "CHOICE(%s, %s);" % (m.group(1), m.group(2))
        m = re.match(r'(scr_\w+face)\((\d+), "?(\w+)"?\)$', ln)
        if m and m.group(1) in FACES:
            fe = m.group(3)
            fe = str(10 + ord(fe) - ord("A")) if fe.isalpha() and len(fe) == 1 else fe
            return "FACE(%s, '%s', %s);" % (m.group(2), FACES[m.group(1)], fe)
        m = re.match(r"([drlu])tsprite = (\d+)$", ln)
        if m:
            return "c->tspr[%d] = %s;" % ("drul".index(m.group(1)), self.spr[int(m.group(2))])
        m = re.match(r"scr_noface\((\d+)\)$", ln)
        if m:
            return "NOFACE(%s);" % m.group(1)
        m = re.match(r"global\.fc = (\d+)$", ln)
        if m:
            return "c->fc = %s;" % m.group(1)
        m = re.match(r"global\.fe = (\d+)$", ln)
        if m:
            return "c->fe = %s;" % m.group(1)
        m = re.match(r"global\.msc = (.+)$", ln)
        if m:
            e = self.expr(m.group(1))
            return "c->msc = %s;" % e if e else "/* gml: %s */" % ln
        m = re.match(r"scr_text\((.+)\)$", ln)
        if m:
            e = self.expr(m.group(1))
            return "c->arg = %s; gm_scr_text(c);" % e if e else "/* gml: %s */" % ln
        m = re.match(r"sprite_index = (\d+)$", ln)
        if m:
            return "c->spr = %s;" % self.spr[int(m.group(1))]
        m = re.match(r"(global\.flag\[.+\]|global\.plot|global\.gold|global\.lgold|read|talked|c->\w+|image_index) = (.+)$", ln)
        if m:
            lhs, rhs = self.expr(m.group(1)), self.expr(m.group(2))
            if lhs and rhs:
                return "%s = %s;" % (lhs, rhs)
        m = re.match(r"(\w+) = (.+)$", ln)
        if m and m.group(1) in self.locals:
            rhs = self.expr(m.group(2))
            if rhs:
                return "%s = %s;" % (m.group(1), rhs)
        m = re.match(r"(scr_itemget|scr_keyitemget|scr_weaponget|scr_armorget|scr_litemget|scr_itemremove|scr_keyitemremove|scr_litemremove)\((.+)\)$", ln)
        if m:
            e = self.expr(m.group(2))
            if e:
                return "%s(%s);" % (m.group(1).replace("scr_", "gm_"), e)
        self.unknown += 1
        return "/* gml: %s */" % ln.replace("*/", "* /")

    def function(self, name, text):
        lines = text.split("\n")
        # the code's own counters (tempvar, afford...): locals of the function
        self.locals = set()
        for ln in lines:
            m = re.match(r"([a-z_][a-z0-9_]*) = (.+)$", ln)
            if m and m.group(1) in LOCALS:
                self.locals.add(m.group(1))
        used = set()
        for ln in lines:
            m = re.search(r"goto (L\d+)", ln)
            if m:
                used.add(m.group(1))
        body = []
        for ln in lines:
            if not ln.strip() or ln.startswith("    // stack") or ln.startswith("// stack"):
                continue
            if ln.startswith("with (") or ln.startswith("} // end with"):
                body.append("  /* gml: %s */" % ln.replace("*/", "* /"))
                continue
            s = self.stmt(ln, used)
            if s is not None:
                body.append(("" if re.match(r"L\d+:;", s) else "  ") + s)
        # labels never reached by a goto that the function jumps to past its end
        defined = {re.match(r"(L\d+):", b).group(1) for b in body if re.match(r"L\d+:", b)}
        for lab in sorted(used - defined, key=lambda s: int(s[1:])):
            body.append("%s:;" % lab)
        head = ["void %s(TxtCtx *c) {" % name]
        if self.locals:
            head.append("  int %s;" % ", ".join("%s = 0" % v for v in sorted(self.locals)))
        return head + body + ["}", ""]


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    g = GM.GM(os.path.join(sys.argv[1], "data.win"))
    tr = Tr(g)
    out = ["/* made by tools/texts.py from the original's code: what its signs, beds and people say (see",
           " * gmtexts.h). Its ifs and gotos are the original's; what the game has no use for stays a comment. */",
           "#if defined(__GNUC__) && !defined(__clang__)", '#pragma GCC optimize("Os")', "#endif",
           '#include "game.h"', '#include "gmtexts.h"', '#include "world.h"', "",
           "#if defined(__GNUC__)", '#pragma GCC diagnostic ignored "-Wunused-label"\n#pragma GCC diagnostic ignored "-Wunused-but-set-variable"', "#endif", ""]
    for entry, fn in ENTRIES:
        out += tr.function(fn, g.decompile(entry))
    with open(OUT, "w") as f:
        f.write("\n".join(out))
    print("gmtexts.c: %d functions, %d statements left as comments" % (len(ENTRIES), tr.unknown))


if __name__ == "__main__":
    main()
