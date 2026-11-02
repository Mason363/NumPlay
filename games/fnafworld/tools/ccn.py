"""FNAF World's application data (Clickteam Fusion 2.5, build 284), read only; tools/pack.py uses it.

The game's fnaf-world.exe holds its data encrypted; ~/Documents/Assets/gamesrc/fnafworld/tools/unpack.py writes it out
plain as app.ccn: chunks (u16 id, u16 flags 0, u32 size, the data). The ones read here:
- 0x6666, the pictures: u32 count, then each: i32 its number + 1, u32 size, u32 packed size, zlib data: i32, i32,
  u32 size, i16 width, i16 height, u8 mode (4: BGR), u8 flags (16: an alpha plane follows), u16, 4 x i16 hot spot and
  action point, 4 bytes the clear colour, then the rows (BGR, padded to an even number of bytes; the alpha rows to 4).
- 0x2229, the objects: u32 count, then each a run of chunks: 0x4444 (u16 its number, i16 kind: 0 quick backdrop,
  1 backdrop, 2 active, 3 string, 7 counter), 0x4445 (its name, UTF-16), 0x4446 (its properties: a backdrop's
  picture at byte 16; a quick backdrop's size at 8, its fill at 24 (1 a colour, 3 a picture) and the colour or picture at 26; an active's animations from the
  offset at byte 4: u16 size, u16 count, u16 offsets, each animation 32 directions; a direction: u8 its slowest and
  fastest speed, u16 how many times it plays (0 for ever), u16 the picture it loops back to, u16 the picture count,
  the pictures).
- 0x3333, the frames: chunks of their own, 0x3334 the size, 0x3335 the name, 0x3341 the layers (u32 count, each:
  u32 flags, f32 x and y scroll, u32, u32, the name), 0x3338 the objects placed (u32 count, then 20 bytes each:
  u16, u16 the object, i32 x, i32 y, i16, i16, i16 the layer, i16)."""
import os
import struct
import zlib

from PIL import Image

CCN = os.path.expanduser("~/Documents/Assets/gamesrc/fnafworld/dl/app.ccn")


def chunks(b, p=0):
    out = []
    while p + 8 <= len(b):
        cid, _, size = struct.unpack_from("<HHI", b, p)
        out.append((cid, b[p + 8:p + 8 + size]))
        p += 8 + size
        if cid == 0x7F7F:
            break
    return out


def wstr(b):
    return b.decode("utf-16-le", "replace").split("\0")[0]


class Frame:
    def __init__(self, sub):
        self.name = wstr(sub[0x3335])
        self.w, self.h = struct.unpack_from("<ii", sub[0x3334], 0)
        self.layers = []
        if 0x3341 in sub:
            b = sub[0x3341]
            q = 4
            for _ in range(struct.unpack_from("<I", b, 0)[0]):
                fl, xc, yc, _, _ = struct.unpack_from("<IffII", b, q)
                q += 20
                e = q
                while b[e:e + 2] != b"\0\0":
                    e += 2
                self.layers.append((wstr(b[q:e + 2]), fl, xc, yc))
                q = e + 2
        self.placed = []   # (object, x, y, layer)
        x = sub.get(0x3338, b"\0\0\0\0")
        for k in range(struct.unpack_from("<I", x, 0)[0]):
            _, obj, px, py, _, _, layer, _ = struct.unpack_from("<HHiihhhh", x, 4 + 20 * k)
            self.placed.append((obj, px, py, layer))
        self.events = sub.get(0x333D, b"")


class App:
    def __init__(self, path=CCN):
        d = open(path, "rb").read()
        assert d[:4] == b"PAMU", "not a Clickteam application"
        top = chunks(d, 16)
        self.pics = {}
        bank = next(c for i, c in top if i == 0x6666)
        q = 4
        for _ in range(struct.unpack_from("<I", bank, 0)[0]):
            h, _, csize = struct.unpack_from("<iII", bank, q)
            self.pics[h - 1] = (q + 12, csize)
            q += 12 + csize
        self.bank = bank
        self._cache = {}
        self.objects, self.meta = {}, {}
        self.ink, self.visible, self.obstacle, self.fine, self.values = {}, {}, {}, {}, {}
        b = next(c for i, c in top if i == 0x2229)
        q = 4
        for _ in range(struct.unpack_from("<I", b, 0)[0]):
            head = name = props = None
            while True:
                sid, _, size = struct.unpack_from("<HHI", b, q)
                body = b[q + 8:q + 8 + size]
                q += 8 + size
                if sid == 0x4444:
                    head = struct.unpack_from("<Hh", body, 0)
                    ink = struct.unpack_from("<II", body, 8) if len(body) >= 16 else (0, 0xFFFFFFFF)
                elif sid == 0x4445:
                    name = wstr(body)
                elif sid == 0x4446:
                    props = body
                elif sid == 0x7F7F:
                    break
            kind = head[1]
            anims, meta, image = {}, {}, None
            if kind == 2 and props:
                anims, meta = self._anims(props)
            elif kind == 1 and props and len(props) >= 18:
                image = struct.unpack_from("<H", props, 16)[0]
            elif kind == 0 and props and len(props) >= 34:
                w, hh = struct.unpack_from("<ii", props, 8)
                shape, fill = struct.unpack_from("<HH", props, 22)
                image = (w, hh, struct.unpack_from("<H", props, 26)[0] if fill == 3 else None,
                         struct.unpack_from("<I", props, 26)[0] if fill == 1 else None)
            self.objects[head[0]] = (kind, name, anims, image)
            self.ink[head[0]] = ink
            vis, obst, fine = True, False, False
            if kind == 2 and props and len(props) > 44:
                nf = struct.unpack_from("<H", props, 42)[0]
                vis, obst = bool(nf & 8), (nf & 0x30) == 0x10
            elif kind in (0, 1) and props and len(props) >= 8:
                ob, col = struct.unpack_from("<HH", props, 4)
                obst, fine = ob == 1, col == 1
            self.visible[head[0]], self.obstacle[head[0]], self.fine[head[0]] = vis, obst, fine
            self.meta[head[0]] = meta
        self.frames = {}
        for i, c in top:
            if i == 0x3333:
                f = Frame(dict(chunks(c)))
                self.frames[f.name] = f

    @staticmethod
    def _anims(p):
        at = struct.unpack_from("<H", p, 4)[0]
        if not at:
            return {}, {}
        out, meta = {}, {}
        count = struct.unpack_from("<H", p, at + 2)[0]
        for a, off in enumerate(struct.unpack_from(f"<{count}H", p, at + 4)):
            if not off:
                continue
            dirs = struct.unpack_from("<32H", p, at + off)
            for di, doff in enumerate(dirs):
                if not doff:
                    continue
                q = at + off + doff
                lo, hi, rep, back, n = struct.unpack_from("<BBHHH", p, q)
                out[(a, di)] = list(struct.unpack_from(f"<{n}H", p, q + 8))
                meta[(a, di)] = (hi, rep, back)
        return out, meta

    def _raw(self, h):
        if h not in self._cache:
            at, csize = self.pics[h]
            raw = zlib.decompress(self.bank[at:at + csize])
            _, _, _, w, hh, mode, flags, _, hx, hy, ax, ay = struct.unpack_from("<iiIhhBBHhhhh", raw, 0)
            self._cache[h] = (raw, w, hh, mode, flags, hx, hy, ax, ay)
        return self._cache[h]

    def picture(self, h):
        """a picture by its number, RGBA"""
        raw, w, hh, mode, flags, *_ = self._raw(h)
        tr = raw[28:32]
        px = raw[32:]
        if flags & 8:
            px = zlib.decompress(px[4:])
        assert mode == 4 and not flags & 7, (h, mode, flags)
        pitch = w * 3 + (w * 3) % 2 * 3 if False else w * 3
        pad = (2 - (w * 3) % 2) % 2
        padp = -(-pad // 3) if pad else 0
        pitch = (w + padp) * 3
        import numpy as np
        rows = np.frombuffer(px[:pitch * hh], dtype=np.uint8).reshape(hh, pitch)[:, :w * 3].reshape(hh, w, 3)
        rgb = rows[..., ::-1]
        if flags & 16:
            ap = w + (4 - w % 4) % 4
            a = np.frombuffer(px[pitch * hh:pitch * hh + ap * hh], dtype=np.uint8).reshape(hh, ap)[:, :w]
        else:
            a = np.where((rows == np.frombuffer(tr[:3], dtype=np.uint8)).all(axis=2), 0, 255).astype(np.uint8)
        return Image.fromarray(np.dstack([rgb, a]).astype(np.uint8), "RGBA")

    def size(self, h):
        return self._raw(h)[1:3]

    def hotspot(self, h):
        """a picture's hot spot: where an object placed at (x, y) has it"""
        return self._raw(h)[5:7]

    def action(self, h):
        return self._raw(h)[7:9]

    def find(self, name, frame=None):
        """objects by name (those placed in that frame, when given)"""
        hits = [h for h, o in self.objects.items() if o[1] == name]
        if frame is not None:
            placed = {o for o, *_ in self.frames[frame].placed}
            hits = [h for h in hits if h in placed]
        return hits

    def obj(self, name, frame=None):
        hits = self.find(name, frame)
        assert len(hits) == 1, (name, frame, hits)
        return hits[0]

    def anim(self, name, a=0, d=0, frame=None):
        o = name if isinstance(name, int) else self.obj(name, frame)
        return self.objects[o][2][(a, d)]

    def anims(self, name, frame=None):
        o = name if isinstance(name, int) else self.obj(name, frame)
        return self.objects[o][2]

    def speed(self, name, a=0, d=0, frame=None):
        o = name if isinstance(name, int) else self.obj(name, frame)
        return self.meta[o][(a, d)]
