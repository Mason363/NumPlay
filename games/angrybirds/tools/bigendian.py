"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py data.bin data.h out.bin

The game reads its tables in place (src/res.h): the materials, pairs, shapes, types, levels, episodes, pictures and
backgrounds (their u16, u32 and floats), the lists of pictures and levels (u16), each picture's palette (16 RGB565).
pack.py writes them little-endian; here they are turned around. What the game reads a byte at a time (the table of
sections, the levels' packed streams, the pixels) stays as it is."""
import re
import struct
import sys

# each section's records (res.h), None: bytes
RECORDS = {"MATERIALS": "fffffB3x", "PAIRS": "BB2xff", "SHAPES": "BBhhhf12f", "TYPES": "HBBHBBf", "TYPE_SPRITES": "4H",
           "LEVELS": "IHHIIIBBBB8f", "LEVEL_STREAMS": None, "EPISODES": "HH", "STORY_BEFORE": None,
           "STORY_AFTER": None, "EPISODE_PICS": "H", "STORY_PICS": "H", "GOLDEN": None, "UNLOCKS": "H",
           "PICTURES": "IHHhhHH", "PIXELS": None, "BACKGROUNDS": "HHHH" + "HHffff" * 8}


def convert(b, h):
    b = bytearray(b)
    assert b[:4] == b"ABD1", "not an Angry Birds data.bin"
    n = struct.unpack_from("<I", b, 4)[0]
    sections = {k[2:]: struct.unpack_from("<II", b, 8 + 8 * v) for k, v in h.items() if k.startswith("D_")}
    assert len(sections) == n and set(sections) == set(RECORDS), sorted(sections)
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(fmt, pos, n=1):
        s = struct.Struct("<" + fmt)
        end = pos + n * s.size
        assert end <= len(b) and not any(done[pos:end]), (fmt, pos)
        done[pos:end] = b"\1" * (end - pos)
        for i in range(n):
            struct.pack_into(">" + fmt, b, pos + i * s.size, *s.unpack_from(b, pos + i * s.size))

    for name, fmt in RECORDS.items():
        if fmt:
            at, size = sections[name]
            assert size % struct.calcsize("<" + fmt) == 0, name
            turn(fmt, at, size // struct.calcsize("<" + fmt))
    # each picture's palette, at the start of its pixels
    pics, pixels = sections["PICTURES"], sections["PIXELS"][0]
    for off in sorted({struct.unpack_from(">I", b, pics[0] + 16 * i)[0] for i in range(pics[1] // 16)}):
        turn("16H", pixels + off)
    return bytes(b)


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
