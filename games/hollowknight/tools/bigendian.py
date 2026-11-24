"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.bin out.bin

The game reads much of data.bin in place as its own numbers: the textures', sprites', clips' and objects' records, the
ground's segments and cells, the colliders' and the fonts' floats, the tile coder's probabilities, the instances'
heads. pack.py writes them little-endian; here they are turned around, each where src/hk.h, game.h, anim.c, phys.c,
room.c, text.c and vm.c read it. What the game reads a byte at a time (rd16, rd32: offsets, counts, the sector
streams, strings, scripts) stays as it is, and so do the rooms' headers, which are packed (room.c turns them around as
it unpacks them)."""
import lzma
import struct
import sys

SECTIONS = ["TEX", "PAL", "TDAT", "ROOMS", "RBLOB", "PRIOR", "SOFT", "SPR", "CLIP", "STR", "TEXT", "FONT", "PHASH", "SCOL",
            "RVAR", "HEADS", "VM", "VMDEF"]
FONT_PHASES = 4   # (data.h)
LZ = [{"id": lzma.FILTER_LZMA1, "lc": 0, "lp": 0, "pb": 0, "dict_size": 1 << 16}]
HDR = struct.Struct("<4H4f4f4fff6HII")   # (pack.py: room_blobs; src/hk.h: RoomHdr)
ENT = "BBBBHHffffffffHH"                 # (tools/ents.py: REC; src/game.h: Ent)
OBJ = "fffffffffHHHHHBBHHhHBBBBBBH"      # (tools/vm.py: OBJ; src/vm.c: ObjRec)
CLIP = "HHfBBH"                          # (src/anim.c: ClipRec)
SPR = "HHhhHH"                           # (src/hk.h: SpriteRec)


class Data:
    def __init__(self, b):
        self.b = bytearray(b)
        assert self.b[:4] == b"HKNW", "not a Hollow Knight data.bin"
        n, = struct.unpack_from("<I", self.b, 4)
        assert n == len(SECTIONS), n
        self.sec = {k: struct.unpack_from("<II", self.b, 8 + 8 * i) for i, k in enumerate(SECTIONS)}
        self.done = set()   # (what has been turned around: once only, wherever two things lead to it)

    def at(self, k):
        return self.sec[k][0]

    def rec(self, fmt, pos, count=1):
        """count records of fmt (little-endian) at pos, turned around"""
        s = struct.Struct("<" + fmt)
        for i in range(count):
            p = pos + i * s.size
            if p in self.done:
                continue
            self.done.add(p)
            v = s.unpack_from(self.b, p)
            struct.pack_into(">" + fmt, self.b, p, *v)

    def words(self, size, pos, n):
        """n numbers of size bytes at pos, turned around"""
        self.rec({2: "H", 4: "I"}[size], pos, n)


def texture_records(d):
    """TexRec: w, h (16 bits), tw, th, pal (16), then a 32-bit field of off (27 bits), fmt (2), flags (3); GCC puts the
    first of a field's parts in its low bits on a little-endian calculator, in its high bits on a big-endian one"""
    base, size = d.sec["TEX"]
    n = (size - 4) // 12
    for i in range(n):
        p = base + 4 + 12 * i
        w, h, tw, th, pal, f = struct.unpack_from("<HHBBHI", d.b, p)
        off, fmt, flags = f & (1 << 27) - 1, f >> 27 & 3, f >> 29
        struct.pack_into(">HHBBHI", d.b, p, w, h, tw, th, pal, off << 5 | fmt << 3 | flags)


def rooms(d):
    """each room's ground (phys.c: segments, cells) and objects (game.h: Ent), where its header says"""
    base, _ = d.sec["ROOMS"]
    blob = d.at("RBLOB")
    n, = struct.unpack_from("<I", d.b, base)
    for i in range(n):
        off, comp, raw = struct.unpack_from("<III", d.b, base + 4 + 44 * i + 32)
        hdr = lzma.decompress(bytes(d.b[blob + off:blob + off + comp]), format=lzma.FORMAT_RAW, filters=LZ)[:raw]
        v = HDR.unpack_from(hdr)
        nseg, ncol, gw, gh, nent, ground, ents = v[19], v[20], v[21], v[22], v[23], v[24], v[25]
        # the ground: the colliders' flags (bytes), the tile map's size and solid tiles, the segments, their colliders,
        # the cells' first entries, the entries
        p = blob + ground + (ncol + 1 & ~1)
        tw, th = struct.unpack_from("<HH", d.b, p)
        p += 4 + th * ((tw + 7) // 8) + 1 & ~1
        d.words(2, p, 4 * nseg)
        p += 8 * nseg
        p += nseg + 1 & ~1
        cells = gw * gh + 1
        q = p + 2 * (cells - 1)
        last, = struct.unpack_from(">H" if q in d.done else "<H", d.b, q)   # (the entries' count)
        d.words(2, p, cells)
        d.words(2, p + 2 * cells, last)
        d.rec(ENT, blob + ents, nent)


def sprites(d):
    base, size = d.sec["SPR"]
    d.rec(SPR, base, size // struct.calcsize("<" + SPR))


def clips(d):
    """ClipRec, then the frames (16 bits each) to the section's end"""
    base, size = d.sec["CLIP"]
    n, = struct.unpack_from("<I", d.b, base)
    d.rec(CLIP, base + 4, n)
    p = base + 4 + 12 * n
    d.words(2, p, (base + size - p) // 2)


def colliders(d):
    """SCOL: count, offsets (read a byte at a time), then each: kind, points, 2 bytes, then floats (a box's 4, a
    shape's 2 a point)"""
    base, _ = d.sec["SCOL"]
    n, = struct.unpack_from("<I", d.b, base)
    for i in range(n):
        o, = struct.unpack_from("<I", d.b, base + 4 + 4 * i)
        e = base + o
        kind, pts = d.b[e], d.b[e + 1]
        d.words(4, e + 4, 4 if kind == 2 else 2 * pts)


def fonts(d):
    """FONT: per style, its metrics (4 floats, read as floats), then its glyphs (Glyph: their advance, 16 bits)"""
    base, _ = d.sec["FONT"]
    n, = struct.unpack_from("<I", d.b, base)
    for i in range(n):
        o, = struct.unpack_from("<I", d.b, base + 4 + 4 * i)
        st = base + o
        lo, ncodes = d.b[st + 16], d.b[st + 17]
        for k in range(ncodes * FONT_PHASES):
            g, = struct.unpack_from("<H", d.b, st + 20 + 2 * k)
            if g:
                d.words(2, st + g + 4, 1)
        d.words(4, st, 4)


def scripts(d):
    """VM: each room's offset (0: none), then the rooms': their counts (16 bits, read a byte at a time), then their
    objects' records (ObjRec). The rooms: ROOMS' entries less their variants' (RVAR)"""
    base, _ = d.sec["VM"]
    nrooms = struct.unpack_from("<I", d.b, d.at("ROOMS"))[0] - struct.unpack_from("<I", d.b, d.at("RVAR"))[0]
    for i in range(nrooms):
        o, = struct.unpack_from("<I", d.b, base + 4 * i)
        if o:
            nobjs, = struct.unpack_from("<H", d.b, base + o)
            d.rec(OBJ, base + o + 8, nobjs)


def convert(b):
    d = Data(b)
    texture_records(d)
    rooms(d)
    base, size = d.sec["PRIOR"]
    d.words(2, base, size // 2)
    sprites(d)
    clips(d)
    colliders(d)
    fonts(d)
    base, size = d.sec["HEADS"]
    d.words(4, base, size // 4)
    scripts(d)
    return bytes(d.b)


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    out = convert(open(src, "rb").read())
    open(dst, "wb").write(out)
