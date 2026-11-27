"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.bin out.bin

The game reads some of its numbers in place: the pictures' Huffman tables (src/pic.c: a lookup of u16, then maxcode
and valoffset, s32 each), their quantization tables (u16) and the sprites' colours (src/gfx.c: RGB565, u16).
pack.py writes them little-endian; here they are turned around. Everything else it reads a byte at a time."""
import struct
import sys

HUFF, QUANT, SPAL = 0, 1, 6
HUFF_TABLES, LUT, CODES = 4, 512, 18   # (tools/codec.py's decode_tables: 1424 bytes each)


def convert(b):
    b = bytearray(b)
    assert b[:4] == b"KRSH", "not a Kingdom Rush data.bin"
    nsec, = struct.unpack_from("<I", b, 4)
    secs = struct.unpack_from(f"<{nsec}I", b, 8)
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(size, pos, n):
        end = pos + size * n
        assert end <= len(b) and not any(done[pos:end]), pos
        done[pos:end] = b"\1" * (end - pos)
        for i in range(pos, end, size):
            b[i:i + size] = b[i:i + size][::-1]

    for k in range(HUFF_TABLES):
        p = secs[HUFF] + 1424 * k
        turn(2, p, LUT)
        turn(4, p + 2 * LUT, 2 * CODES)
    turn(2, secs[QUANT], (secs[QUANT + 1] - secs[QUANT]) // 2)
    turn(2, secs[SPAL], 256)
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(out)
