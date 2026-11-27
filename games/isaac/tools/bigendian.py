"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.bin out.bin

The game reads its pictures' numbers in place (src/res.c): a frame's head, colours and rows' offsets, a sprite's first
frame and count, a backdrop's head and colours (u16 each). pack.py writes them little-endian; here they are turned
around. What the game reads a byte at a time (the offsets, the fonts, the rooms, the pixels) stays as it is."""
import struct
import sys

FRAMES, SPRITES, BACKDROPS = 0, 1, 2


def convert(b):
    b = bytearray(b)
    assert b[:4] == b"ISA1", "not an Isaac data.bin"
    nsec, = struct.unpack_from("<I", b, 4)
    secs = struct.unpack_from(f"<{nsec}I", b, 8)
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(pos, n=1):
        end = pos + 2 * n
        assert end <= len(b) and not any(done[pos:end]), pos
        done[pos:end] = b"\1" * (end - pos)
        for i in range(pos, end, 2):
            b[i], b[i + 1] = b[i + 1], b[i]

    def table(s):
        n, = struct.unpack_from("<I", b, secs[s])
        return [secs[s] + o for o in struct.unpack_from(f"<{n}I", b, secs[s] + 4)]

    for o in table(FRAMES):
        w, h, ox, oy, n, _ = struct.unpack_from("<6H", b, o)
        turn(o, 6 + n)                          # (the head, the colours)
        turn(o + 12 + 2 * n + (n + 1 & ~1), h)  # (each row's offset)
    n, = struct.unpack_from("<I", b, secs[SPRITES])
    turn(secs[SPRITES] + 4, 2 * n)
    for o in table(BACKDROPS):
        w, h, n, _ = struct.unpack_from("<4H", b, o)
        turn(o, 4 + n)
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(out)
