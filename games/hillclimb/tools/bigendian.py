"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.bin out.bin

The game reads its pictures' and fonts' numbers in place (src/res.h): a picture's head (u16 each), its colours (a u32
each: R, G, B, A from the low byte), its RGB565 colours (u16), a glyph's bitmap offset (u32). pack.py writes them
little-endian; here they are turned around. What the game reads a byte at a time (res.c's offsets, the tiles' and
packed glyphs' row offsets, the pixels) stays as it is."""
import struct
import sys

IMG_OPAQUE = 1


def convert(b):
    b = bytearray(b)
    assert b[:4] == b"HCRD", "not a Hill Climb Racing data.bin"
    nimg, nfont = struct.unpack_from("<II", b, 4)
    offs = struct.unpack_from(f"<{nimg + nfont}I", b, 16)
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(fmt, pos, n=1):
        s = struct.Struct("<" + fmt)
        end = pos + n * s.size
        assert end <= len(b) and not any(done[pos:end]), (fmt, pos)
        done[pos:end] = b"\1" * (end - pos)
        for i in range(n):
            struct.pack_into(">" + fmt, b, pos + i * s.size, *s.unpack_from(b, pos + i * s.size))

    for o in offs[:nimg]:
        w, h, ncol, flags = struct.unpack_from("<HHHH", b, o)
        turn("HHHHHhhH", o)
        turn("I", o + 16, ncol)
        if flags & IMG_OPAQUE:
            turn("H", o + 16 + 4 * ncol, ncol)
    for o in offs[nimg:]:
        count = b[o + 1]
        for g in range(count):
            turn("I", o + 4 + 12 * g + 8)
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(out)
