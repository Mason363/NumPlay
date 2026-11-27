"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.bin out.bin

The pictures' decoder (src/pic.c) reads its tables in place: each Huffman table's lookup (512 u16), maxcode and
valoff (18 int32 each), and the quantization tables (u16). pack.py writes them little-endian; here they are turned
around. The rest is read a byte at a time (res.h) and stays as it is."""
import struct
import sys

MAGIC = b"FNF1"
SEC_HUFF, SEC_QUANT = 0, 1
HUFF_SIZE = 1424   # (tools/codec.py's decode_tables)


def convert(b):
    b = bytearray(b)
    assert b[:4] == MAGIC, "not this game's data.bin"
    n = struct.unpack_from("<I", b, 4)[0]
    offs = struct.unpack_from(f"<{n}I", b, 8)
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(fmt, pos, count):
        s = struct.Struct("<" + fmt)
        end = pos + count * s.size
        assert end <= len(b) and not any(done[pos:end]), (fmt, pos)
        done[pos:end] = b"\1" * (end - pos)
        for i in range(count):
            struct.pack_into(">" + fmt, b, pos + i * s.size, *s.unpack_from(b, pos + i * s.size))

    huff, quant = offs[SEC_HUFF], offs[SEC_QUANT]
    assert quant - huff == 4 * HUFF_SIZE
    for k in range(4):
        p = huff + HUFF_SIZE * k
        turn("H", p, 512)
        turn("i", p + 1024, 18)
        turn("i", p + 1096, 18)
    size = offs[SEC_QUANT + 1] - quant
    assert size % 256 == 0
    turn("H", quant, size // 2)
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(out)
