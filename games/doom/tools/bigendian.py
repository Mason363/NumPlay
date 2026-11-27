"""The WHX for a big-endian calculator (the Casio fx-CG50's SuperH): python3 bigendian.py in.whx out.whx

The engine reads the WHX's numbers in place (RP2040 Doom's format, tools/whd_gen; SHORT and LONG do nothing on the
calculators): its header, the lumps' offsets and named lumps' numbers, the levels' things, vertexes, subsectors,
nodes, sectors and the heads of their linedefs and blockmap, the textures, the sprites' sizes and frames, the
vpatches' lumps and each patch's column offsets (sprites, wall patches, splash screens). whd_gen writes them
little-endian; here they are turned around. What the engine reads a byte at a time (sides, segs, the blockmap's
cells, the textures' patch tables, pictures, flats, demos) stays as it is, and so does the mips file."""
import re
import struct
import sys

SPLASH = {"help", "help1", "help2", "credit", "endpic", "titlepic", "interpic", "victory2", "bossback", "pfub1",
          "pfub2"}
# a level's lumps after its marker: things, linedefs (their head), sidedefs, vertexes, segs, subsectors, nodes,
# sectors, reject, blockmap (its head); (numbers, all of it)
LEVEL = [("hhhhh", True), ("HHH", False), None, ("hh", True), None, ("H", True), ("hhhhHBBBB", True),
         ("hhhhhhh", True), None, ("hhhh", False)]


def convert(b):
    b = bytearray(b)
    assert b[:4] == b"IWHX", "not a WHX"
    done = bytearray(len(b))   # (each number turned around once: a byte turned twice would be a bug)

    def turn(fmt, pos, n=1):
        s = struct.Struct("<" + fmt)
        end = pos + n * s.size
        assert end <= len(b) and not any(done[pos:end]), (fmt, pos)
        done[pos:end] = b"\1" * (end - pos)
        for i in range(n):
            struct.pack_into(">" + fmt, b, pos + i * s.size, *s.unpack_from(b, pos + i * s.size))

    n, info = struct.unpack_from("<ii", b, 4)
    raw = struct.unpack_from(f"<{n + 1}I", b, info)
    named = struct.unpack_from("<H", b, 34)[0]
    at = info + 4 * (n + 1)
    names = {b[at + 12 * k:at + 12 * k + 10].split(b"\0")[0].decode(): struct.unpack_from("<H", b, at + 12 * k + 10)[0]
             for k in range(named)}

    def lump(i):   # (w_wad.c's lump_size)
        return raw[i] & 0xFFFFFF, ((raw[i + 1] - raw[i]) & 0xFFFFFF) - (raw[i] >> 30)

    turn("ii", 4)
    turn("II14sH", 12)
    turn("I", info, n + 1)
    for k in range(named):
        turn("H", at + 12 * k + 10)

    def table(i, fmt, whole=True):
        start, size = lump(i)
        unit = struct.calcsize("<" + fmt)
        assert size % unit == 0 if whole else size >= unit, (i, fmt, size)
        turn(fmt, start, size // unit if whole else 1)

    for name, m in names.items():
        if re.fullmatch(r"e\dm\d|map\d\d", name):
            for k, f in enumerate(LEVEL):
                if f:
                    table(m + 1 + k, *f)
    s_start, s_end, p_start, p_end = names["s_start"], names["s_end"], names["p_start"], names["p_end"]
    assert lump(s_start)[1] == 4 * (s_end - s_start - 1)
    table(s_start, "I")
    table(s_end, "H")
    table(p_start, "H")
    # the textures, and the patches they are made of
    patches = set(range(s_start + 1, s_end)) | set(range(p_start + 1, p_end))
    patches |= {m for name, m in names.items() if name in SPLASH or name.startswith("wimap")}
    tex, _ = lump(names["texture1"])
    count = b[tex] | b[tex + 1] << 8   # (read a byte at a time)
    for t in range(count):
        width, height, pc, p0 = struct.unpack_from("<HBBH", b, tex + 2 + 6 * t)
        if not width:   # (none)
            continue
        patches |= {p0} if not pc else {b[tex + 2 + p0 + 2 * k] | b[tex + 3 + p0 + 2 * k] << 8 for k in range(pc)}
    turn("HBBH", tex + 2, count)
    for p in sorted(patches):
        start, size = lump(p)
        if not size:   # (markers, and duplicates whd_gen emptied)
            continue
        w = b[start + 1] | (b[start + 2] & 1) << 8
        d = 3 + (b[start] & 1)   # (pd_render.cpp's get_patch_decoder: the head, the decoder, then the offsets)
        d += b[start + 2 * d]
        offs = struct.unpack_from(f"<{w + 1}H", b, start + 2 * d)
        assert 0 < w and start + 2 * (d + w + 1) <= start + size, p
        assert all(o < 0xFF00 or (o & 0xFF) < w for o in offs), p
        turn("H", start + 2 * d, w + 1)
    return bytes(b)


if __name__ == "__main__":
    with open(sys.argv[1], "rb") as f:
        out = convert(f.read())
    with open(sys.argv[2], "wb") as f:
        f.write(out)
