"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.bin out.bin

The game reads two of its sprites' numbers in place (src/gfx.c): their RGB565 colours (u16 each) and their slabs' row
offsets (u16 or u32 each). pack.py writes them little-endian; here they are turned around. The rest is read a byte at
a time (rd16, rd32) and stays as it is."""
import struct
import sys


def convert(b):
    b = bytearray(b)
    assert b[:4] == b"FE2D", "not a Final Earth 2 data.bin"
    nspr, _, spr_table = struct.unpack_from("<HHI", b, 4)
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(fmt, pos, n):
        s = struct.Struct("<" + fmt)
        end = pos + n * s.size
        assert end <= len(b) and not any(done[pos:end]), (fmt, pos)
        done[pos:end] = b"\1" * (end - pos)
        for i in range(n):
            struct.pack_into(">" + fmt, b, pos + i * s.size, *s.unpack_from(b, pos + i * s.size))

    for i in range(nspr):
        d, w, h = struct.unpack_from("<IHH", b, spr_table + 8 * i)
        ncol, mode, nslab = b[d], b[d + 1], b[d + 2]
        assert ncol or mode & 3 != 1, "a sprite of 256 colours"
        turn("H", d + 4, ncol)
        slabs = d + ((4 + 3 * ncol + 3) & ~3)
        for k in range(nslab):
            rows = d + struct.unpack_from("<I", b, slabs + 8 * k + 4)[0]
            turn("I" if mode & 4 else "H", rows, h + 1)
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(out)
