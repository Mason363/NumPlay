"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py data.bin data.h out.bin

The game reads many of its numbers in place (DATAW and the like, in src/): the arts' first patterns and counts,
the mappings', animations' and Sonic's pattern load cues' offsets and words, the 16x16 blocks, the 256x256 chunks,
the objects' places, the conveyor platforms, the start positions, the palettes, the tile maps, the pattern load
cues, the sine table (u16 or s16 each) and the offset tables (u32). pack.py writes them little-endian; here they are
turned around (data.h: where each is). What it reads a byte at a time (the patterns, the mapping pieces, the
animation scripts, the layouts, the collision, the demos, the special stages) stays as it is."""
import re
import struct
import sys


def convert(b, header):
    b = bytearray(b)
    d = {m.group(1): int(m.group(2)) for m in re.finditer(r"#define (\w+) (\d+)", header)}
    starts = sorted(v for k, v in d.items() if (k.startswith("D_") and not k.startswith(("D_OBJPOS_", "D_COL_")))
                    or k.startswith("OBJPOS_START_"))
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(pos, n=1, size=2):
        end = pos + size * n
        assert end <= len(b) and not any(done[pos:end]), pos
        done[pos:end] = b"\1" * (end - pos)
        for i in range(pos, end, size):
            b[i:i + size] = b[i:i + size][::-1]

    def u16(pos):
        return struct.unpack_from("<H", b, pos)[0]

    def size(name):   # (up to the next item)
        return next(s for s in starts if s > d[name]) - d[name]

    def offsets(pos):   # (a table: its count, not read, then each item's offset from the table)
        n = u16(pos)
        offs = [pos + u16(pos + 2 + 2 * i) for i in range(n)]
        turn(pos + 2, n)
        return offs

    turn(d["D_ARTS"], 2 * d["N_ARTS"])
    for table, n in (("D_MAPS", "N_MAPS"), ("D_ANIS", "N_ANIS")):
        items = struct.unpack_from(f"<{d[n]}I", b, d[table])
        turn(d[table], d[n], 4)
        for o in items:
            offsets(o)   # (a mapping's pieces and an animation's scripts are bytes)
    for e in offsets(d["D_SONICDPLC"]):
        turn(e + 2, b[e])
    for z in ("GHZ", "LZ", "MZ", "SLZ", "SYZ", "SBZ"):
        turn(d["D_BLK16_" + z], size("D_BLK16_" + z) // 2)
        turn(d["D_BLK256_" + z], 256 * d["N_BLK256_" + z])
    for k in d:
        if k.startswith("OBJPOS_START_"):   # (x, y, then the id and subtype: bytes)
            assert size(k) % 6 == 0, k
            for i in range(size(k) // 6):
                turn(d[k] + 6 * i, 2)
        elif k.startswith("D_PF_"):
            turn(d[k], 1 + 3 * (u16(d[k]) + 1))
        elif k.startswith("D_START_"):
            turn(d[k], size(k) // 2)
        elif k.startswith(("D_PAL_", "D_ENI_")):
            turn(d[k], d["N_" + k[2:]])
    turn(d["D_PLATFORMS"], 16, 4)
    for o in offsets(d["D_PLC"]):
        turn(o, 1 + 2 * u16(o))
    turn(d["D_TAB_SINE_DATA"], size("D_TAB_SINE_DATA") // 2)
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f, open(sys.argv[2]) as h:
        out = convert(f.read(), h.read())
    with open(sys.argv[3], "wb") as f:
        f.write(out)
