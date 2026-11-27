"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.bin out.bin

The game reads one kind of number in place (src/res.h's pal_colors): the palettes' colours (RGB565, u16 each).
pack.py writes them little-endian; here they are turned around. Everything else it reads a byte at a time."""
import struct
import sys

T_PALS = 2


def convert(b):
    b = bytearray(b)
    assert b[:4] == b"PVZ1", "not a Plants vs. Zombies data.bin"
    pals = struct.unpack_from("<6I", b, 4)[T_PALS]
    first, = struct.unpack_from("<I", b, pals)
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)
    for o in struct.unpack_from(f"<{(first - pals) // 4}I", b, pals):
        n, = struct.unpack_from("<H", b, o)
        pos, end = o + 4, o + 4 + 2 * n
        assert end <= len(b) and not any(done[pos:end]), o
        done[pos:end] = b"\1" * (end - pos)
        for i in range(pos, end, 2):
            b[i], b[i + 1] = b[i + 1], b[i]
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(out)
