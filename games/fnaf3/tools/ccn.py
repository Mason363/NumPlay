"""Five Nights at Freddy's 3's application.ccn (Clickteam Fusion, from the game's Android package): its pictures, its
objects with their animations, and its frames with the objects in them. Read only; tools/pack.py uses it.

The file is chunks (u16 id, u16 flags, u32 size, the data). The ones read here:
- 0x6666, the pictures: u16 count, u16 the highest number, then each: u16 its number, u32 format (0: RGBA, 3: RGB with
  rows padded), u16 width, u16 height, 4 x i16 hot spot and action point, u32 size, zlib data.
- 0x2229, the objects: u32 count, then each a run of chunks: 0x4444 (u16 its number, i16 kind: 1 backdrop, 2 active,
  3 string, 7 counter), 0x4445 (its name, UTF-16), 0x4446 (its properties: an active's animations from the offset
  at byte 6: u16 count, u16 offsets, each animation 32 directions, a direction's u16 picture count at 6 and the
  pictures from 8).
- 0x3333, the frames: chunks of their own, 0x3335 the name, 0x3338 the objects placed (u32 count, then 20 bytes
  each: u16, u16 the object, i32 x, i32 y, 4 x i16)."""
import struct
import zlib

from PIL import Image


def chunks(b, p=0):
    out = []
    while p + 8 <= len(b):
        cid, _, size = struct.unpack_from("<HHI", b, p)
        out.append((cid, b[p + 8:p + 8 + size]))
        p += 8 + size
        if cid == 0x7F7F:
            break
    return out


class App:
    def __init__(self, path):
        d = open(path, "rb").read()
        assert d[:4] == b"PAMU", "not a Clickteam application"
        top = chunks(d, 16)
        self.pics = {}
        bank = next(c for i, c in top if i == 0x6666)
        q = 4
        while q < len(bank):
            h, fmt, w, hh, hx, hy, _, _, size = struct.unpack_from("<HIHHhhhhI", bank, q)
            self.pics[h] = (fmt, w, hh, q + 22, size, hx, hy)
            q += 22 + size
        self.bank = bank
        self.objects, self.by_name, self.props, self.dirs = {}, {}, {}, {}
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
                elif sid == 0x4445:
                    name = body.decode("utf-16-le").split("\0")[0]
                elif sid == 0x4446:
                    props = body
                elif sid == 0x7F7F:
                    break
            anims = self._anims(props) if head[1] == 2 and props else {}
            self.objects[head[0]] = (head[1], name, anims)
            self.props[head[0]] = props
            self.dirs[head[0]] = self._dirs(props) if head[1] == 2 and props else {}
        self.frames, self.frame_size = {}, {}
        for i, c in top:
            if i != 0x3333:
                continue
            sub = dict(chunks(c))
            name = sub[0x3335].decode("utf-16-le").split("\0")[0]
            placed = []
            x = sub.get(0x3338, b"\0\0\0\0")
            for k in range(struct.unpack_from("<I", x, 0)[0]):
                _, obj, px, py = struct.unpack_from("<HHii", x, 4 + 20 * k)
                placed.append((obj, px, py))
            self.frames[name] = placed
            self.frame_size[name] = struct.unpack_from("<ii", sub[0x3334], 0)

    @staticmethod
    def _anims(p):
        at = struct.unpack_from("<H", p, 6)[0]
        if not at:
            return {}
        out = {}
        count = struct.unpack_from("<H", p, at + 2)[0]
        for a, off in enumerate(struct.unpack_from(f"<{count}H", p, at + 4)):
            if not off:
                continue
            dirs = struct.unpack_from("<32H", p, at + off)
            if dirs[0]:
                q = at + off + dirs[0]
                n = struct.unpack_from("<H", p, q + 6)[0]
                out[a] = list(struct.unpack_from(f"<{n}H", p, q + 8))
        return out

    @staticmethod
    def _dirs(p):
        """every animation's directions: {(animation, direction): pictures}"""
        at = struct.unpack_from("<H", p, 6)[0]
        out = {}
        if not at:
            return out
        count = struct.unpack_from("<H", p, at + 2)[0]
        for a, off in enumerate(struct.unpack_from(f"<{count}H", p, at + 4)):
            if not off:
                continue
            for d, doff in enumerate(struct.unpack_from("<32H", p, at + off)):
                if doff:
                    q = at + off + doff
                    n = struct.unpack_from("<H", p, q + 6)[0]
                    out[(a, d)] = list(struct.unpack_from(f"<{n}H", p, q + 8))
        return out

    def backdrop(self, h):
        """a backdrop's obstacle type (1 solid), its size and picture"""
        p = self.props[h]
        obst = struct.unpack_from("<H", p, 4)[0]
        w, hh = struct.unpack_from("<II", p, 8)
        return obst, w, hh, struct.unpack_from("<H", p, 16)[0]

    def picture(self, h):
        """a picture by its number, RGBA"""
        fmt, w, hh, at, size = self.pics[h][:5]
        raw = zlib.decompress(self.bank[at:at + size])
        if fmt == 0:
            return Image.frombytes("RGBA", (w, hh), raw[:w * hh * 4])
        pitch = len(raw) // hh
        return Image.frombytes("RGB", (w, hh), b"".join(raw[y * pitch:y * pitch + w * 3] for y in range(hh))).convert("RGBA")

    def hotspot(self, h):
        """a picture's hot spot: where an object placed at (x, y) has it"""
        return self.pics[h][5:7]

    def obj(self, name, frame=None):
        """an object's number by its name (the one placed in that frame when several share it)"""
        hits = [h for h, o in self.objects.items() if o[1] == name]
        if frame is not None:
            placed = {o for o, _, _ in self.frames[frame]}
            hits = [h for h in hits if h in placed]
        assert len(hits) == 1, (name, frame, hits)
        return hits[0]

    def anim(self, name, a=0, frame=None):
        """an object's animation: its pictures' numbers"""
        return self.objects[self.obj(name, frame)][2][a]

    def placed(self, name, frame):
        """where an object is placed in a frame (its first place)"""
        o = self.obj(name, frame)
        return next((x, y) for h, x, y in self.frames[frame] if h == o)
