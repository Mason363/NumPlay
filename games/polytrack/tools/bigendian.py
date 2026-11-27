"""data.bin for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py data.bin data.h out.bin

The game reads data.bin's tables in place, as the types data.h gives them (its pt_blob + offset macros). pack.py writes
their numbers little-endian; here they are turned around."""
import re
import struct
import sys

# each table's records: the numbers in them (a byte is left as it is)
FMT = {"int8_t": "b", "uint8_t": "B", "int16_t": "h", "uint16_t": "H", "int32_t": "i", "uint32_t": "I",
       "pt_inst": "hhBBBBBB", "pt_spec": "HH"}


def convert(b, header):
    b = bytearray(b)
    done = bytearray(len(b))   # (each table turned around once)
    tables = re.findall(r"#define \w+ \(\(const (\w+) \*\)\(pt_blob \+ (\d+)\)\)\s+/\* (\d+) \*/", header)
    assert tables, "no tables in data.h"
    for ctype, off, n in tables:
        s = struct.Struct("<" + FMT[ctype])
        off, n = int(off), int(n)
        end = off + n * s.size
        assert end <= len(b) and not any(done[off:end]), (ctype, off)
        done[off:end] = b"\1" * (end - off)
        for i in range(n):
            struct.pack_into(">" + FMT[ctype], b, off + i * s.size, *s.unpack_from(b, off + i * s.size))
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f, open(sys.argv[2]) as h:
        out = convert(f.read(), h.read())
    with open(sys.argv[3], "wb") as f:
        f.write(out)
