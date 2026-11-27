"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.bin out.bin

The game reads its pictures' numbers in place (src/res.h's Pic): a picture's w, h and pivot (u16, s16) and its
colours (RGB565, u16). pack.py writes them little-endian; here they are turned around. What it reads a byte at a time
(the index, the levels, the pixels) stays as it is."""
import struct
import sys


def convert(b):
    b = bytearray(b)
    nlev, npic = struct.unpack_from("<II", b, 0)
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(pos, n):
        end = pos + 2 * n
        assert end <= len(b) and not any(done[pos:end]), pos
        done[pos:end] = b"\1" * (end - pos)
        for i in range(pos, end, 2):
            b[i], b[i + 1] = b[i + 1], b[i]

    for o in struct.unpack_from(f"<{npic}I", b, 16 + 8 * nlev):
        turn(o, 4)
        turn(o + 10, b[o + 8])
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(out)
