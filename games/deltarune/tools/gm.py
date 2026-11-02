"""A reader for GameMaker Studio 1.4 data files (data.win, bytecode 16), as Deltarune's Chapter 1 (2018) has.

    python3 -I tools/gm.py DATA.WIN rooms            the rooms (name, size)
    python3 -I tools/gm.py DATA.WIN room NAME        a room's views, backgrounds, objects and tiles
    python3 -I tools/gm.py DATA.WIN objects          the objects (sprite, parent, events)
    python3 -I tools/gm.py DATA.WIN code NAME        a code entry, decompiled to plain statements
    python3 -I tools/gm.py DATA.WIN grep TEXT        the code entries that use TEXT (a name or string)
    python3 -I tools/gm.py DATA.WIN sprites          the sprites (size, origin, frames)
    python3 -I tools/gm.py DATA.WIN fonts            the fonts

Read only: nothing in the file is run. The decompiler turns the stack code back into expressions and
statements; jumps stay as labelled gotos (if (!x) goto L12), which is enough to read the game's rules."""
import io
import struct
import sys

from PIL import Image


class GM:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        self.chunks = {}
        p = 8
        while p < len(self.d):
            name = self.d[p:p + 4].decode()
            size = self.u32(p + 4)
            self.chunks[name] = (p + 8, size)
            p += 8 + size
        self._strings = None
        self._sprites = None
        self._backgrounds = None
        self._objects = None
        self._rooms = None
        self._code = None
        self._textures = {}
        self._fonts = None
        self._vars = None
        self._funcs = None

    # ---- primitives
    def u32(self, p):
        return struct.unpack_from("<I", self.d, p)[0]

    def i32(self, p):
        return struct.unpack_from("<i", self.d, p)[0]

    def f32(self, p):
        return struct.unpack_from("<f", self.d, p)[0]

    def u16(self, p):
        return struct.unpack_from("<H", self.d, p)[0]

    def s(self, p):
        """a string pointer (it points at the text, after its length)"""
        if p == 0:
            return None
        n = self.u32(p - 4)
        return self.d[p:p + n].decode("utf-8", "replace")

    def plist(self, p):
        n = self.u32(p)
        return [self.u32(p + 4 + 4 * i) for i in range(n)]

    def chunk_list(self, name):
        return self.plist(self.chunks[name][0])

    # ---- strings
    @property
    def strings(self):
        if self._strings is None:
            self._strings = [self.s(q + 4) for q in self.chunk_list("STRG")]
        return self._strings

    # ---- textures and texture pages
    def texture(self, i):
        if i not in self._textures:
            q = self.chunk_list("TXTR")[i]
            p = self.u32(q + 8)   # (scaled, generated mipmaps, then the PNG)
            # the PNG: up to its IEND
            end = self.d.index(b"IEND", p) + 8
            self._textures[i] = Image.open(io.BytesIO(self.d[p:end])).convert("RGBA")
        return self._textures[i]

    def tpag(self, p):
        v = struct.unpack_from("<11H", self.d, p)
        return dict(sx=v[0], sy=v[1], sw=v[2], sh=v[3], tx=v[4], ty=v[5], tw=v[6], th=v[7], bw=v[8], bh=v[9], page=v[10])

    def tpag_image(self, p):
        """the texture page item as the full bounding image (RGBA)"""
        t = self.tpag(p)
        im = Image.new("RGBA", (t["bw"], t["bh"]), (0, 0, 0, 0))
        src = self.texture(t["page"]).crop((t["sx"], t["sy"], t["sx"] + t["sw"], t["sy"] + t["sh"]))
        if (t["sw"], t["sh"]) != (t["tw"], t["th"]):
            src = src.resize((t["tw"], t["th"]), Image.NEAREST)
        im.paste(src, (t["tx"], t["ty"]))
        return im

    # ---- sprites
    @property
    def sprites(self):
        if self._sprites is None:
            out = []
            for p in self.chunk_list("SPRT"):
                v = struct.unpack_from("<i6i3i2I2i", self.d, p)
                name = self.s(v[0])
                q = p + 4 * 14
                if self.i32(q) == -1:   # GameMaker Studio 2: a version, a type, the playback speed and its kind
                    q += 4 * 5
                n = self.u32(q)
                frames = [self.u32(q + 4 + 4 * i) for i in range(n)]
                out.append(dict(name=name, w=v[1], h=v[2], ml=v[3], mr=v[4], mb=v[5], mt=v[6],
                                bbox_mode=v[10], ox=v[12], oy=v[13], frames=frames, ptr=p))
            self._sprites = out
        return self._sprites

    def sprite(self, name):
        for s in self.sprites:
            if s["name"] == name:
                return s
        raise KeyError(name)

    def sprite_index(self, name):
        for i, s in enumerate(self.sprites):
            if s["name"] == name:
                return i
        raise KeyError(name)

    def sprite_frames(self, name):
        s = self.sprite(name) if isinstance(name, str) else self.sprites[name]
        out = []
        for f in s["frames"]:
            t = self.tpag(f)
            im = Image.new("RGBA", (s["w"], s["h"]), (0, 0, 0, 0))
            src = self.texture(t["page"]).crop((t["sx"], t["sy"], t["sx"] + t["sw"], t["sy"] + t["sh"]))
            if (t["sw"], t["sh"]) != (t["tw"], t["th"]):
                src = src.resize((t["tw"], t["th"]), Image.NEAREST)
            im.paste(src, (t["tx"], t["ty"]))
            out.append(im)
        return out

    # ---- backgrounds (tilesets too)
    @property
    def backgrounds(self):
        if self._backgrounds is None:
            out = []
            for p in self.chunk_list("BGND"):
                # GameMaker Studio 2's tilesets: the tiles' size, their borders, columns and count
                tw, th, bx, by, cols, _, count = struct.unpack_from("<7I", self.d, p + 24)
                out.append(dict(name=self.s(self.u32(p)), tpag=self.u32(p + 16), tw=tw, th=th, bx=bx, by=by,
                                cols=cols, count=count))
            self._backgrounds = out
        return self._backgrounds

    def background_image(self, i):
        return self.tpag_image(self.backgrounds[i]["tpag"])

    # ---- fonts
    @property
    def fonts(self):
        if self._fonts is None:
            out = []
            for p in self.chunk_list("FONT"):
                name, disp, em, bold, italic = self.s(self.u32(p)), self.s(self.u32(p + 4)), self.u32(p + 8), self.u32(p + 12), self.u32(p + 16)
                rs = self.u16(p + 20)
                re_ = self.u32(p + 24)
                tp = self.u32(p + 28)
                sx, sy = self.f32(p + 32), self.f32(p + 36)
                glyphs = []
                for g in self.plist(p + 40):
                    c, x, y, w, h = struct.unpack_from("<5H", self.d, g)
                    shift, off = struct.unpack_from("<hh", self.d, g + 10)
                    glyphs.append(dict(c=c, x=x, y=y, w=w, h=h, shift=shift, offset=off))
                out.append(dict(name=name, display=disp, size=em, bold=bold, italic=italic, first=rs, last=re_,
                                tpag=tp, sx=sx, sy=sy, glyphs=glyphs))
            self._fonts = out
        return self._fonts

    def font(self, name):
        for f in self.fonts:
            if f["name"] == name:
                return f
        raise KeyError(name)

    # ---- objects
    @property
    def objects(self):
        if self._objects is None:
            out = []
            for p in self.chunk_list("OBJT"):
                name = self.s(self.u32(p))
                spr, vis, solid, depth, pers, parent, mask = struct.unpack_from("<i2Ii2i", self.d, p + 4)[0:0] or (
                    self.i32(p + 4), self.u32(p + 8), self.u32(p + 12), self.i32(p + 16), self.u32(p + 20),
                    self.i32(p + 24), self.i32(p + 28))
                # physics: uses, sensor, shape, density, restitution, group, lin damp, ang damp, vertex count,
                # friction, awake, kinematic, then the vertices
                q = p + 32
                nvert = self.i32(q + 32)
                q += 48 + 8 * nvert
                events = []
                for etype, ep in enumerate(self.plist(q)):
                    for e in self.plist(ep):
                        sub = self.u32(e)
                        codes = []
                        for a in self.plist(e + 4):
                            codes.append(self.i32(a + 32))
                        events.append((etype, sub, codes))
                out.append(dict(name=name, sprite=spr, visible=vis, solid=solid, depth=depth, persistent=pers,
                                parent=parent, mask=mask, events=events))
            self._objects = out
        return self._objects

    def object_index(self, name):
        for i, o in enumerate(self.objects):
            if o["name"] == name:
                return i
        raise KeyError(name)

    # ---- rooms
    @property
    def rooms(self):
        if self._rooms is None:
            self._rooms = [self._room(p) for p in self.chunk_list("ROOM")]
        return self._rooms

    def _room(self, p):
        r = dict(name=self.s(self.u32(p)), caption=self.s(self.u32(p + 4)), w=self.u32(p + 8), h=self.u32(p + 12),
                 speed=self.u32(p + 16), persistent=self.u32(p + 20), bgcolor=self.u32(p + 24),
                 draw_bgcolor=self.u32(p + 28), code=self.i32(p + 32), flags=self.u32(p + 36))
        bgs, views, objs, tiles = (self.u32(p + 40 + 4 * i) for i in range(4))
        r["backgrounds"] = []
        for b in self.plist(bgs):
            v = struct.unpack_from("<2I7iI", self.d, b)
            r["backgrounds"].append(dict(enabled=v[0], foreground=v[1], bg=v[2], x=v[3], y=v[4], tilex=v[5],
                                         tiley=v[6], speedx=v[7], speedy=v[8], stretch=v[9]))
        r["views"] = []
        for b in self.plist(views):
            v = struct.unpack_from("<I12i", self.d, b)
            r["views"].append(dict(enabled=v[0], x=v[1], y=v[2], w=v[3], h=v[4], px=v[5], py=v[6], pw=v[7],
                                   ph=v[8], bx=v[9], by=v[10], sx=v[11], sy=v[12], obj=self.i32(b + 52)))
        r["objects"] = []
        for b in self.plist(objs):
            x, y, o, inst, code = struct.unpack_from("<3iIi", self.d, b)
            sx, sy = self.f32(b + 20), self.f32(b + 24)
            col = self.u32(b + 28)
            rot = self.f32(b + 32)
            pre = self.i32(b + 36)
            r["objects"].append(dict(x=x, y=y, obj=o, id=inst, code=code, sx=sx, sy=sy, color=col, rot=rot, pre=pre))
        r["tiles"] = []
        for b in self.plist(tiles):
            x, y, bg, sx, sy, w, h, depth, inst = struct.unpack_from("<8iI", self.d, b)
            scx, scy = self.f32(b + 36), self.f32(b + 40)
            col = self.u32(b + 44)
            r["tiles"].append(dict(x=x, y=y, bg=bg, sx=sx, sy=sy, w=w, h=h, depth=depth, id=inst, scx=scx, scy=scy,
                                   color=col))
        # GameMaker Studio 2's layers: backgrounds, instances, assets (sprites) and tile grids
        r["layers"] = []
        lp = self.u32(p + 4 * 22)
        for b in self.plist(lp) if lp else []:
            name = self.s(self.u32(b))
            lid, ltype, depth = self.u32(b + 4), self.u32(b + 8), self.i32(b + 12)
            xo, yo, hs, vs = struct.unpack_from("<4f", self.d, b + 16)
            vis = self.u32(b + 32)
            L = dict(name=name, id=lid, type=ltype, depth=depth, xo=xo, yo=yo, hs=hs, vs=vs, visible=vis)
            q = b + 36
            if ltype == 1:
                v = struct.unpack_from("<2Ii3II", self.d, q)
                L.update(bg_visible=v[0], foreground=v[1], sprite=v[2], tiledh=v[3], tiledv=v[4], stretch=v[5],
                         color=v[6], frame=self.f32(q + 28), anim=self.f32(q + 32))
            elif ltype == 2:
                n = self.u32(q)
                L["instances"] = [self.u32(q + 4 + 4 * i) for i in range(n)]
            elif ltype == 3:
                legacy, sprs = self.u32(q), self.u32(q + 4)
                L["sprites"] = []
                for sp in self.plist(sprs):
                    sid, x, y = self.i32(sp + 4), self.i32(sp + 8), self.i32(sp + 12)
                    scx, scy = self.f32(sp + 16), self.f32(sp + 20)
                    col = self.u32(sp + 24)
                    fi, rot = self.f32(sp + 36), self.f32(sp + 40)
                    L["sprites"].append(dict(sprite=sid, x=x, y=y, sx=scx, sy=scy, color=col, frame=fi, rot=rot))
                # the legacy tiles (rooms made in GameMaker 1.4): a piece of a sprite each (not a background)
                L["tiles"] = []
                for t in self.plist(legacy):
                    x, y, spr, sx, sy, w, h, depth, inst = struct.unpack_from("<8iI", self.d, t)
                    scx, scy = self.f32(t + 36), self.f32(t + 40)
                    col = self.u32(t + 44)
                    L["tiles"].append(dict(x=x, y=y, spr=spr, sx=sx, sy=sy, w=w, h=h, depth=depth, scx=scx, scy=scy,
                                           color=col))
            elif ltype == 4:
                bg, cols, rows = self.i32(q), self.u32(q + 4), self.u32(q + 8)
                L.update(bg=bg, cols=cols, rows=rows,
                         data=list(struct.unpack_from("<%dI" % (cols * rows), self.d, q + 12)))
            r["layers"].append(L)
        return r

    def room(self, name):
        for r in self.rooms:
            if r["name"] == name:
                return r
        raise KeyError(name)

    # ---- code
    @property
    def code(self):
        if self._code is None:
            out = []
            for p in self.chunk_list("CODE"):
                name = self.s(self.u32(p))
                length = self.u32(p + 4)
                nloc, nargs = self.u16(p + 8), self.u16(p + 10)
                rel = self.i32(p + 12)
                start = p + 12 + rel
                off = self.u32(p + 16)
                out.append(dict(name=name, start=start, length=length, offset=off, nargs=nargs & 0x7FFF))
            self._code = out
        return self._code

    def code_named(self, name):
        for c in self.code:
            if c["name"] == name:
                return c
        raise KeyError(name)

    def _refs(self):
        """for each variable or function reference in the code: its name (by the instruction's address)"""
        if self._vars is not None:
            return
        self._vars, self._funcs = {}, {}
        p, size = self.chunks["VARI"]
        q = p + 12
        while q + 20 <= p + size:
            nm, itype, vid, occ, first = struct.unpack_from("<IiiIi", self.d, q)
            name = self.s(nm)
            a = first
            for _ in range(occ):
                self._vars[a] = (name, itype)
                nxt = self.u32(a + 4) & 0x07FFFFFF
                a += nxt
            q += 20
        p, size = self.chunks["FUNC"]
        n = self.u32(p)
        for i in range(n):
            nm, occ, first = struct.unpack_from("<IIi", self.d, p + 4 + 12 * i)
            name = self.s(nm)
            a = first
            for _ in range(occ):
                self._funcs[a] = name
                nxt = self.u32(a + 4) & 0x07FFFFFF
                a += nxt

    def disasm(self, entry):
        """the instructions of a code entry: (address in bytes from its start, opcode, fields)"""
        self._refs()
        if isinstance(entry, str):
            entry = self.code_named(entry)
        base = entry["start"]
        p, end = base + entry["offset"], base + entry["length"]
        out = []
        p = base
        while p < end:
            w = self.u32(p)
            op = w >> 24
            a = p - base
            t1, t2 = (w >> 16) & 0xF, (w >> 20) & 0xF
            ins = dict(a=a, op=op, t1=t1, t2=t2, raw=w)
            size = 4
            if op in (0xB6, 0xB7, 0xB8, 0xBA, 0xBB):
                off = w & 0x7FFFFF
                if off & 0x400000:
                    off -= 0x800000
                ins["target"] = a + off * 4
            elif op == 0x45:  # pop
                ins["inst"] = struct.unpack("<h", struct.pack("<H", w & 0xFFFF))[0]
                ins["var"] = self._vars.get(p, ("?", 0))[0]
                ins["vtype"] = self.u32(p + 4) >> 24 & 0xF8
                size = 8
            elif op in (0xC0, 0xC1, 0xC2, 0xC3, 0x84):
                if t1 == 0:
                    ins["v"] = struct.unpack_from("<d", self.d, p + 4)[0]; size = 12
                elif t1 == 1:
                    ins["v"] = self.f32(p + 4); size = 8
                elif t1 == 2:
                    ins["v"] = self.i32(p + 4); size = 8
                elif t1 == 3:
                    ins["v"] = struct.unpack_from("<q", self.d, p + 4)[0]; size = 12
                elif t1 == 4:
                    ins["v"] = self.u32(p + 4) != 0; size = 8
                elif t1 == 5:
                    ins["inst"] = struct.unpack("<h", struct.pack("<H", w & 0xFFFF))[0]
                    ins["var"] = self._vars.get(p, ("?", 0))[0]
                    ins["vtype"] = self.u32(p + 4) >> 24 & 0xF8
                    size = 8
                elif t1 == 6:
                    ins["v"] = self.strings[self.u32(p + 4)]; ins["str"] = True; size = 8
                elif t1 == 0xF:
                    ins["v"] = struct.unpack("<h", struct.pack("<H", w & 0xFFFF))[0]
            elif op == 0xD9:  # call
                ins["argc"] = w & 0xFFFF
                ins["func"] = self._funcs.get(p, "?")
                size = 8
            elif op == 0x15:
                ins["cmp"] = (w >> 8) & 0xFF
            elif op == 0x86:
                ins["dup"] = w & 0xFF
            elif op == 0xFF:
                ins["brk"] = w & 0xFFFF
                if t1 == 2:
                    size = 8
            out.append(ins)
            p += size
        return out

    def decompile(self, entry):
        return Decompiler(self).run(self.disasm(entry))

    def objname(self, i):
        if 0 <= i < len(self.objects):
            return self.objects[i]["name"]
        return str(i)


INST = {-1: "self", -2: "other", -3: "all", -4: "noone", -5: "global", -6: "builtin", -7: "local", -9: "stacktop"}
BIN = {0x08: "*", 0x09: "/", 0x0A: "div", 0x0B: "%", 0x0C: "+", 0x0D: "-", 0x0E: "&", 0x0F: "|", 0x10: "^",
       0x13: "<<", 0x14: ">>"}
CMP = {1: "<", 2: "<=", 3: "==", 4: "!=", 5: ">=", 6: ">"}


class Duped(str):
    """a stack value a dup copied (a compound assignment's instance or index)"""
    duped = True


class Decompiler:
    """stack code to statements: expressions rebuilt, jumps kept as gotos"""
    def __init__(self, gm):
        self.gm = gm

    def fmt(self, v):
        if isinstance(v, str):
            return v
        return repr(v)

    def varname(self, ins, stack):
        inst = ins["inst"]
        name = ins["var"]
        # the reference's kind: 0x00 an array (instance and index on the stack), 0x80 the instance on the
        # stack, 0xA0 plain, 0xE0 an instance's
        if ins["vtype"] == 0x00:
            idx = stack.pop() if stack else "?"
            who = stack.pop() if stack else "?"
            w = self.who(who)
            return "%s[%s]" % (name if w == "self" else "%s.%s" % (w, name), idx)
        if ins["vtype"] == 0x80:
            who = stack.pop() if stack else "?"
            w = self.who(who)
            return name if w == "self" else "%s.%s" % (w, name)
        if inst == -1 or inst == -7 or inst == -6:
            return name
        return "%s.%s" % (INST.get(inst, self.gm.objname(inst)), name)

    def who(self, w):
        try:
            i = int(float(w))
            if i in INST:
                return INST[i]
            return self.gm.objname(i)
        except (ValueError, TypeError):
            return "(%s)" % w

    def run(self, prog):
        out = []
        stack = []
        at = {ins["a"]: k for k, ins in enumerate(prog)}
        # the short-circuit && and ||: X bf La; Y; b Lb; La: push 0; Lb:  ->  (X && Y)  (bt and push 1: ||)
        logic = {}
        skip_to = {}
        for k, ins in enumerate(prog):
            if ins["op"] in (0xB7, 0xB8):
                la = ins["target"]
                j = at.get(la)
                if j is None or j == 0:
                    continue
                b, p = prog[j - 1], prog[j]
                want = 0 if ins["op"] == 0xB8 else 1
                if b["op"] == 0xB6 and b["target"] == la + 4 and p["op"] in (0x84, 0xC0) and p.get("v") == want and j - 1 > k:
                    logic[k] = (j - 1, "&&" if want == 0 else "||")
                    skip_to[j - 1] = la + 4
        used = set()
        for k, ins in enumerate(prog):
            if "target" in ins and k not in logic and k not in skip_to:
                used.add(ins["target"])
        targets = used
        pend = {}
        k = 0
        while k < len(prog):
            ins = prog[k]
            a = ins["a"]
            if k in logic:
                x = stack.pop() if stack else "?"
                pend[logic[k][0]] = (x, logic[k][1])
                k += 1
                continue
            if k in pend:
                x, op_ = pend.pop(k)
                y = stack.pop() if stack else "?"
                stack.append("(%s %s %s)" % (x, op_, y))
                k = at[skip_to[k]]
                continue
            k += 1
            if a in targets:
                if stack:
                    out.append("    // stack: %s" % ", ".join(stack))
                out.append("L%d:" % a)
            op = ins["op"]
            if op in BIN:
                b = stack.pop() if stack else "?"
                x = stack.pop() if stack else "?"
                stack.append("(%s %s %s)" % (x, BIN[op], b))
            elif op == 0x15:
                b = stack.pop() if stack else "?"
                x = stack.pop() if stack else "?"
                stack.append("(%s %s %s)" % (x, CMP.get(ins["cmp"], "?"), b))
            elif op == 0x11:
                stack.append("-" + (stack.pop() if stack else "?"))
            elif op == 0x12:
                stack.append("!" + (stack.pop() if stack else "?"))
            elif op == 0x07:
                pass
            elif op == 0x86:
                n = ins["dup"] + 1
                if ins["t1"] in (0, 3):   # 8-byte values: two stack words each
                    n = (ins["dup"] + 1)
                if len(stack) >= n:
                    for j in range(len(stack) - n, len(stack)):
                        stack[j] = Duped(stack[j])
                    stack.extend(str(s) for s in stack[-n:])
                else:
                    stack.extend(["?"] * n)
            elif op in (0xC0, 0xC1, 0xC2, 0xC3, 0x84):
                if ins["t1"] == 5:
                    stack.append(self.varname(ins, stack))
                elif ins.get("str"):
                    stack.append('"%s"' % ins["v"].replace("\n", "\\n"))
                else:
                    v = ins["v"]
                    if isinstance(v, float) and v == int(v):
                        v = int(v)
                    stack.append(str(v))
            elif op == 0x45:
                if ins["vtype"] in (0x00, 0x80):
                    # a variable's value (pop.v.v) is pushed after the instance and the index; another (pop.v.i)
                    # before them
                    if len(stack) >= 2 and getattr(stack[-2], "duped", False):
                        # (a compound assignment, a[i] += v: the instance and index, duplicated, came first)
                        val = stack.pop() if stack else "?"
                        name = self.varname(ins, stack)
                    else:
                        name = self.varname(ins, stack)
                        val = stack.pop() if stack else "?"
                    out.append("%s = %s" % (name, val))
                else:
                    val = stack.pop() if stack else "?"
                    inst = ins["inst"]
                    nm = ins["var"] if inst in (-1, -7, -6) else "%s.%s" % (INST.get(inst, self.gm.objname(inst)), ins["var"])
                    out.append("%s = %s" % (nm, val))
            elif op == 0xD9:
                n = ins["argc"]
                args = [stack.pop() if stack else "?" for _ in range(n)]
                stack.append("%s(%s)" % (ins["func"], ", ".join(args)))
            elif op == 0x9E:  # popz
                if stack:
                    out.append(stack.pop())
            elif op == 0xB6:
                out.append("goto L%d" % ins["target"])
            elif op == 0xB7:
                out.append("if %s goto L%d" % (stack.pop() if stack else "?", ins["target"]))
            elif op == 0xB8:
                out.append("if !%s goto L%d" % (stack.pop() if stack else "?", ins["target"]))
            elif op == 0xBA:
                out.append("with (%s) {   // to L%d" % (self.who(stack.pop() if stack else "?"), ins["target"]))
            elif op == 0xBB:
                out.append("} // end with")
            elif op == 0x9C:
                out.append("return %s" % (stack.pop() if stack else ""))
            elif op == 0x9D:
                out.append("exit")
            elif op == 0xFF:
                pass
            else:
                out.append("// op %02x" % op)
        if stack:
            out.append("// stack: %s" % ", ".join(stack))
        return "\n".join(out)


EVENTS = ["Create", "Destroy", "Alarm", "Step", "Collision", "Keyboard", "Mouse", "Other", "Draw", "KeyPress",
          "KeyRelease", "Trigger", "CleanUp", "Gesture", "PreCreate"]


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    gm = GM(sys.argv[1])
    cmd = sys.argv[2]
    arg = sys.argv[3] if len(sys.argv) > 3 else None
    if cmd == "rooms":
        for i, r in enumerate(gm.rooms):
            print(i, r["name"], r["w"], r["h"], len(r["objects"]), len(r["tiles"]))
    elif cmd == "room":
        r = gm.room(arg)
        print(r["name"], r["w"], r["h"], "code", r["code"], "bgcolor %06x" % r["bgcolor"])
        for v in r["views"]:
            if v["enabled"]:
                print("view", v)
        for b in r["backgrounds"]:
            if b["enabled"]:
                print("bg", gm.backgrounds[b["bg"]]["name"] if b["bg"] >= 0 else None, b)
        for o in r["objects"]:
            print("obj", gm.objname(o["obj"]), o["x"], o["y"], o["sx"], o["sy"], "code", o["code"],
                  gm.code[o["code"]]["name"] if o["code"] >= 0 else "")
        for L in r["layers"]:
            extra = ""
            if L["type"] == 4:
                extra = "%s %dx%d" % (gm.backgrounds[L["bg"]]["name"] if L["bg"] >= 0 else None, L["cols"], L["rows"])
            elif L["type"] == 1:
                extra = "sprite %s color %08x %s" % (gm.sprites[L["sprite"]]["name"] if L["sprite"] >= 0 else None,
                                                     L["color"], "visible" if L["bg_visible"] else "hidden")
            elif L["type"] == 3:
                extra = "%d sprites: %s; %d tiles: %s" % (
                    len(L["sprites"]), " ".join(sorted({gm.sprites[a["sprite"]]["name"] for a in L["sprites"]})),
                    len(L["tiles"]), " ".join(sorted({gm.sprites[a["spr"]]["name"] for a in L["tiles"]})))
            elif L["type"] == 2:
                extra = "%d instances" % len(L["instances"])
            print("layer", L["name"], "type", L["type"], "depth", L["depth"], "visible", L["visible"], extra)
        tl = {}
        for t in r["tiles"]:
            tl.setdefault((gm.backgrounds[t["bg"]]["name"], t["depth"]), 0)
            tl[(gm.backgrounds[t["bg"]]["name"], t["depth"])] += 1
        for k, v in tl.items():
            print("tiles", k, v)
    elif cmd == "objects":
        for i, o in enumerate(gm.objects):
            print(i, o["name"], gm.sprites[o["sprite"]]["name"] if o["sprite"] >= 0 else "-", "parent",
                  gm.objname(o["parent"]) if o["parent"] >= 0 else "-", "depth", o["depth"],
                  " ".join("%s%d" % (EVENTS[e], s) for e, s, _ in o["events"]))
    elif cmd == "code":
        if arg.isdigit():
            print(gm.decompile(gm.code[int(arg)]))
        else:
            print(gm.decompile(arg))
    elif cmd == "codes":
        for i, c in enumerate(gm.code):
            print(i, c["name"], c["length"])
    elif cmd == "grep":
        for c in gm.code:
            if c["length"] == 0:
                continue
            txt = gm.decompile(c)
            if arg in txt:
                print("==", c["name"])
                for line in txt.split("\n"):
                    if arg in line:
                        print("   ", line[:200])
    elif cmd == "sprites":
        for i, s in enumerate(gm.sprites):
            print(i, s["name"], s["w"], s["h"], s["ox"], s["oy"], len(s["frames"]))
    elif cmd == "backgrounds":
        for i, b in enumerate(gm.backgrounds):
            t = gm.tpag(b["tpag"])
            print(i, b["name"], t["bw"], t["bh"])
    elif cmd == "fonts":
        for f in gm.fonts:
            print(f["name"], f["display"], f["size"], f["first"], f["last"], len(f["glyphs"]), f["sx"], f["sy"])
    elif cmd == "strings":
        for i, s in enumerate(gm.strings):
            if arg is None or arg in s:
                print(i, repr(s))
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
