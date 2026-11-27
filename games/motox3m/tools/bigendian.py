"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.bin out.bin

The game reads its pictures', fonts' and levels' numbers in place (src/res.h): a picture's head and colours, a
glyph's, a level's header, records (res.h, nape.h) and lists of numbers (vertices, buckets, floats). pack.py writes
them little-endian; here they are turned around. What the game reads a byte at a time (data.bin's table, the pixels,
the glyphs' bits) stays as it is."""
import struct
import sys

# a level's parts (res.h's L_*): their records, and the header's count of them (None: a list filling the part)
PARTS = [("5f", "nmats"), ("ffhhHBBBBH", "nsshapes"), ("h", None), ("HBBff", "nsbodies"), ("HH", "nbuckets"),
         ("H", None), ("fffHBBHBB", "ndbodies"), ("BBBBB3xfffffI", "ndshapes"), ("f", None),
         ("BBhhhhhHHHH3f", "nobj"), ("ff", "naux"), ("BBBBhHHHH4h2x", "ndraws"), ("h", None), ("f", None),
         ("HHf", "ncams")]
HEAD = "BBBB10H8f15I"
COUNTS = ["nobj", "nsshapes", "nsbodies", "ndbodies", "ndshapes", "nbuckets", "ndraws", "ncams", "naux"]


def convert(b):
    b = bytearray(b)
    assert b[:4] == b"MX3D", "not a Moto X3M data.bin"
    nimg, nfont, nlev = struct.unpack_from("<III", b, 4)
    offs = struct.unpack_from(f"<{nimg + nfont + nlev}I", b, 16)
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(fmt, pos, n=1):
        s = struct.Struct("<" + fmt)
        end = pos + n * s.size
        assert end <= len(b) and not any(done[pos:end]), (fmt, pos)
        done[pos:end] = b"\1" * (end - pos)
        for i in range(n):
            struct.pack_into(">" + fmt, b, pos + i * s.size, *s.unpack_from(b, pos + i * s.size))

    for o in sorted(set(offs[:nimg])):   # (pictures seen twice are kept once)
        n = struct.unpack_from("<H", b, o + 12)[0]
        turn("HHffHBB", o)
        turn("H", o + 16, n)
    for o in offs[nimg:nimg + nfont]:
        turn("BBbbBBHI", o + 4, b[o + 1])
    levels = sorted(offs[nimg + nfont:])
    for k, o in enumerate(levels):
        end = levels[k + 1] if k + 1 < len(levels) else len(b)
        head = struct.unpack_from("<" + HEAD, b, o)
        count = dict(zip(COUNTS, head[4:13]), nmats=head[1])
        parts = [o + p for p in head[-15:]] + [end]
        turn(HEAD, o)
        for i, (fmt, what) in enumerate(PARTS):
            size = struct.calcsize("<" + fmt)
            room = parts[i + 1] - parts[i]
            n = room // size if what is None else count[what] + (what == "nbuckets")   # (buckets: then the wide)
            assert n * size <= room < n * size + 4, (k, i)
            turn(fmt, parts[i], n)
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(out)
