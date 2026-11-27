"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py data.bin data.h out.bin

The game reads its pictures', fonts' and tiled pictures' numbers in place (src/res.h): a picture's head and colours,
a glyph's, a tiled picture's head, tile numbers and each tile's colours. pack.py writes them little-endian; here they
are turned around, in the packed blobs too (unpacked, turned, packed again as pack.py's zpack does, the offsets moved
with them). What the game reads a byte at a time (data.bin's table, a packed blob's size, the pixels, the glyphs'
bits, the masks, the mix tables) stays as it is."""
import re
import struct
import sys
import zlib

P_MIX = 4


class Turner:
    def __init__(self, b):
        self.b, self.done = bytearray(b), bytearray(len(b))   # (each number turned once: twice would be a bug)

    def __call__(self, fmt, pos, n=1):
        s = struct.Struct("<" + fmt)
        end = pos + n * s.size
        assert end <= len(self.b) and not any(self.done[pos:end]), (fmt, pos)
        self.done[pos:end] = b"\1" * (end - pos)
        for i in range(n):
            struct.pack_into(">" + fmt, self.b, pos + i * s.size, *s.unpack_from(self.b, pos + i * s.size))


def pic(b):
    t = Turner(b)
    n, flags = b[8] or 256, b[9]
    t("HHhhBBH", 0)
    if not flags & P_MIX:
        t("H", 12, n)
    return t.b


def font(b):
    t = Turner(b)
    t("BBbbBBHI", 4, b[1])
    return t.b


def tiles(b):
    t = Turner(b)
    tw, th, _, _, off = struct.unpack_from("<HHhhI", b)
    assert (len(b) - off) % 160 == 0
    t("HHhhI", 0)
    t("H", 12, tw * th)
    for k in range(off, len(b), 160):
        t("16H", k)
    return t.b


def unzip(z):
    size = struct.unpack_from("<I", z)[0]
    d = zlib.decompressobj(-15)
    raw = d.decompress(z[4:])
    assert len(raw) == size and d.eof and not any(d.unused_data), "not a packed blob"
    return raw


def zpack(blob):   # (pack.py's)
    c = zlib.compressobj(9, zlib.DEFLATED, -15)
    z = struct.pack("<I", len(blob)) + c.compress(blob) + c.flush()
    return z + b"\0" * (-len(z) % 4)


def convert(b, h):
    assert b[:4] == b"AUSD", "not an Among Us data.bin"
    npics, nfonts, n = struct.unpack_from("<III", b, 4)
    assert (npics, nfonts) == (h["N_PICS"], h["N_FONTS"])
    offs = list(struct.unpack_from(f"<{n}I", b, 16)) + [len(b)]
    blobs = [b[offs[i]:offs[i + 1]] for i in range(n)]
    packed = {h["BLOB_MEETING"], h["BLOB_KILLBG"]} | {v for k, v in h.items() if k.startswith("BLOB_MG_")}
    tiled = packed | {h["BLOB_SHIP"]}
    for part in [k[7:] for k in h if k.startswith("N_KILL_")]:
        packed |= set(range(h["PIC_KILL_" + part], h["PIC_KILL_" + part] + h["N_KILL_" + part]))
    out = []
    for i, blob in enumerate(blobs):
        raw = unzip(blob) if i in packed else blob
        if i < npics:
            raw = pic(raw)
        elif i < npics + nfonts:
            raw = font(raw)
        elif i in tiled:
            raw = tiles(raw)
        out.append(zpack(bytes(raw)) if i in packed else bytes(raw))
    head = bytearray(b[:16])
    pos, body, offs = 16 + 4 * n, bytearray(), []
    for blob in out:
        body += b"\0" * (-(pos + len(body)) % 4)
        offs.append(pos + len(body))
        body += blob
    return bytes(head + struct.pack(f"<{n}I", *offs) + body)


def numbers(path):
    h = {}
    for line in open(path):
        m = re.match(r"#define (\w+) (-?\d+)$", line.strip())
        if m:
            h[m.group(1)] = int(m.group(2))
    return h


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read(), numbers(sys.argv[2]))
    with open(sys.argv[3], "wb") as f:
        f.write(out)
