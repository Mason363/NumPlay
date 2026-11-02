#!/usr/bin/env python3
"""Makes NumPico's table of carts, np_carts, from PICO-8 carts (.p8 and .p8.png), as an object file to
link into NumPico.nwa. NumPlay Studio makes the same table in the browser (customize/app.js).

A cart becomes its 0x4300 bytes of ROM (sprites, map, flags, music, sound), its Lua code (unpacked,
in P8SCII, the bytes PICO-8 runs) and its label (16 colours, then 128 x 128 pixels of 4 bits). The
table's layout is in src/carts.h. To be the same byte for byte as the website's for the same files:
- the carts come in the order given; their parts in the order ROM, code, label, each starting
  4-byte aligned after the one before (zeros in between), the first at 12 + 64 * count;
- the table ends with the last part, then zeros up to a multiple of 4;
- a cart's name is its file's without .p8.png or .p8 (in any case), its UTF-8 cut to the longest
  start of at most 48 bytes ending on a whole letter, zero-padded;
- the label: the 16 colours it uses most (ties: the lower colour first), in that order; a pixel of
  another colour takes the closest of those (the sum of squared differences of PICO-8's RGB; ties:
  the first). No label (a .p8 without __label__): 0.

Reading the carts: a .p8.png's pixels hold a byte each (the low 2 bits of alpha, red, green, blue);
its code is plain, or packed in PICO-8's old ":c:" format or its "pxa" one (0.2.0), unpacked with
Lexaloffle's algorithms (p8_compress.c, zlib licence; pxa_compress_snippets.c, MIT licence). A .p8 is
text: its code's UTF-8 symbols become P8SCII, its __gfx__, __gff__, __map__, __sfx__ and __music__
become memory as femto8's parser does (MIT licence).

usage:
  p8cart.py -o carts.o [CART...]     an ARM object for the calculator
  p8cart.py -o carts.o --list FILE   the carts named in FILE, one a line
  p8cart.py --c carts.c [CART...]    the same table as C, for the tests
  p8cart.py --bin carts.bin [CART...]"""
import argparse
import os
import struct
import sys
import zlib

MAGIC, VERSION, ENTRY, NAME = 0x3850504E, 1, 64, 48
ROM = 0x4300

# P8SCII as .p8 files write it (from femto8's table)
P8SCII = {1: "\u00b9", 2: "\u00b2", 3: "\u00b3", 4: "\u2074", 5: "\u2075", 6: "\u2076", 7: "\u2077", 8: "\u2078",
          11: "\u1d47", 12: "\u1d9c", 14: "\u1d49", 15: "\u1da0", 16: "\u25ae", 17: "\u25a0", 18: "\u25a1",
          19: "\u2059", 20: "\u2058", 21: "\u2016", 22: "\u25c0", 23: "\u25b6", 24: "\u300c", 25: "\u300d",
          26: "\u00a5", 27: "\u2022", 28: "\u3001", 29: "\u3002", 30: "\u309b", 31: "\u309c", 127: "\u25cb",
          128: "\u2588", 129: "\u2592", 130: "\U0001f431", 131: "\u2b07\ufe0f", 132: "\u2591", 133: "\u273d",
          134: "\u25cf", 135: "\u2665", 136: "\u2609", 137: "\uc6c3", 138: "\u2302", 139: "\u2b05\ufe0f",
          140: "\U0001f610", 141: "\u266a", 142: "\U0001f17e\ufe0f", 143: "\u25c6", 144: "\u2026",
          145: "\u27a1\ufe0f", 146: "\u2605", 147: "\u29d7", 148: "\u2b06\ufe0f", 149: "\u02c7", 150: "\u2227",
          151: "\u274e", 152: "\u25a4", 153: "\u25a5"}
for _i, _c in enumerate("あいうえおかきくけこさしすせそたちつてとなにぬねのはひふへほまみむめもやゆよらりるれろわをん"
                        "っゃゅょアイウエオカキクケコサシスセソタチツテトナニヌネノハヒフヘホマミムメモヤユヨラリルレロワヲン"
                        "ッャュョ◜◝"):
    P8SCII[154 + _i] = _c
SYMBOLS = sorted(((v.encode("utf-8"), k) for k, v in P8SCII.items()), key=lambda x: -len(x[0]))

# PICO-8's 32 colours (RGB), for labels
COLORS = [(0, 0, 0), (29, 43, 83), (126, 37, 83), (0, 135, 81), (171, 82, 54), (95, 87, 79), (194, 195, 199),
          (255, 241, 232), (255, 0, 77), (255, 163, 0), (255, 236, 39), (0, 228, 54), (41, 173, 255), (131, 118, 156),
          (255, 119, 168), (255, 204, 170), (41, 24, 20), (17, 29, 53), (66, 33, 54), (18, 83, 89), (116, 47, 41),
          (73, 51, 59), (162, 136, 121), (243, 239, 125), (190, 18, 80), (255, 108, 36), (168, 231, 46), (0, 181, 67),
          (6, 90, 181), (117, 70, 101), (255, 110, 89), (255, 157, 129)]


class CartError(Exception):
    pass


def utf8_len(data, i):
    """the length of the UTF-8 letter at i, or 0 when it isn't one"""
    c = data[i]
    n = 2 if c & 0xE0 == 0xC0 else 3 if c & 0xF0 == 0xE0 else 4 if c & 0xF8 == 0xF0 else 0
    if not n or i + n > len(data) or any(b & 0xC0 != 0x80 for b in data[i + 1:i + n]):
        return 0
    return n


def to_p8scii(data):
    """the code of a .p8 to P8SCII: its UTF-8 symbols to their letters; other UTF-8 letters are left out
    (PICO-8 has none), and bytes that aren't UTF-8 kept (old carts wrote P8SCII as it is)"""
    out = bytearray()
    i = 0
    while i < len(data):
        c = data[i]
        if c < 0x80:
            out.append(c)
            i += 1
            continue
        for s, k in SYMBOLS:
            if data.startswith(s, i):
                out.append(k)
                i += len(s)
                break
        else:
            n = utf8_len(data, i)
            if n:
                i += n
            else:
                out.append(c)
                i += 1
    return bytes(out)


# ------------------------------------------------------------------ .p8
def hexbytes(line):
    h = [c for c in line if c in "0123456789abcdefABCDEF"]
    return bytes(int(h[k] + h[k + 1], 16) for k in range(0, len(h) - 1, 2))


def parse_p8(data):
    raw = data.split(b"\n")
    lines = [r.decode("latin-1") for r in raw]
    if not lines or not lines[0].startswith("pico-8 cartridge"):
        raise CartError("not a PICO-8 cart")
    rom = bytearray(ROM)
    code, label = [], None
    section, n = None, 0
    music = 0
    for li, line in enumerate(lines[1:], 1):
        tag = line.rstrip("\r")
        if tag in ("__lua__", "__gfx__", "__gff__", "__label__", "__map__", "__sfx__", "__music__", "__gfx8__",
                   "__meta__", "__change_mask__") or (tag.startswith("__") and tag.endswith("__") and len(tag) > 4
                                                       and section != "__lua__"):
            section, n = tag, 0
            if tag == "__label__":
                label = [0] * (128 * 128)
            continue
        if section is None:
            continue
        if section == "__lua__":
            code.append(raw[li])
        elif section == "__gfx__":
            b = hexbytes(tag)
            at = n * 64
            for k, v in enumerate(b[:64]):
                if at + k < 0x2000:
                    rom[at + k] = ((v << 4) | (v >> 4)) & 0xFF
            if b:
                n += 1
        elif section == "__gff__":
            b = hexbytes(tag)
            at = 0x3000 + n * 128
            for k, v in enumerate(b[:128]):
                if at + k < 0x3100:
                    rom[at + k] = v
            if b:
                n += 1
        elif section == "__map__":
            b = hexbytes(tag)
            at = 0x2000 + n * 128
            for k, v in enumerate(b[:128]):
                if at + k < 0x3000:
                    rom[at + k] = v
            if b:
                n += 1
        elif section == "__sfx__":
            b = hexbytes(tag)
            if len(b) >= 84 and n < 64:
                at = 0x3200 + n * 68
                rom[at:at + 68] = sfx_bytes(b)
                n += 1
        elif section == "__music__":
            b = hexbytes(tag)
            if len(b) >= 5 and n < 64:
                flags = b[0]
                at = 0x3100 + n * 4
                for k in range(4):
                    rom[at + k] = (b[1 + k] & 0x7F) | (0x80 if flags & (1 << k) else 0)
                n += 1
                music = n
        elif section == "__label__" and label is not None:
            row = [c for c in tag if c in "0123456789abcdefghijklmnopqrstuv"]
            if row and n < 128:
                for x, c in enumerate(row[:128]):
                    label[n * 128 + x] = "0123456789abcdefghijklmnopqrstuv".index(c)
                n += 1
    for at in range(0x3100 + music * 4, 0x3200, 4):  # (missing patterns: PICO-8's empty ones)
        rom[at:at + 4] = b"\x41\x42\x43\x44"
    return bytes(rom), to_p8scii(b"\n".join(code)), label


def sfx_bytes(b):
    """a __sfx__ line (84 bytes) as memory (68): 32 notes of 16 bits, then editor mode, speed, loop start, end"""
    out = bytearray()
    for i in range(0, 32, 2):
        a, bb, c, d, e = b[4 + i * 5 // 2:4 + i * 5 // 2 + 5]
        n1 = (a, bb >> 4, bb & 15, c >> 4)                         # pitch, waveform, volume, effect
        n2 = (((c << 4) | (d >> 4)) & 0xFF, d & 15, e >> 4, e & 15)
        for pitch, wave, vol, fx in (n1, n2):
            v = (0x8000 if wave > 7 else 0) | (fx << 12) | (vol << 9) | ((wave & 7) << 6) | (pitch & 63)
            out += struct.pack("<H", v)
    out += bytes(b[0:4])
    return bytes(out)


# ------------------------------------------------------------------ .p8.png
def png_rgba(data):
    """the pixels of an 8-bit RGBA (or RGB) PNG, without interlacing: (width, height, bytes RGBA)"""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise CartError("not a PNG")
    at, idat, w = 8, bytearray(), None
    while at + 8 <= len(data):
        n, kind = struct.unpack(">I4s", data[at:at + 8])
        body = data[at + 8:at + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body[:13])
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise CartError("a PNG PICO-8 didn't make")
        elif kind == b"IDAT":
            idat += body
        elif kind == b"IEND":
            break
        at += 12 + n
    if w is None:
        raise CartError("not a PNG")
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(bytes(idat))
    stride = w * bpp
    out = bytearray(w * h * 4)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1:
                line[x] = (line[x] + a) & 0xFF
            elif f == 2:
                line[x] = (line[x] + b) & 0xFF
            elif f == 3:
                line[x] = (line[x] + ((a + b) >> 1)) & 0xFF
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        for x in range(w):
            px = line[x * bpp:x * bpp + bpp]
            out[(y * w + x) * 4:(y * w + x) * 4 + 4] = px if bpp == 4 else px + b"\xff"
        prev = line
    return w, h, bytes(out)


LITERALS = b"^\n 0123456789abcdefghijklmnopqrstuvwxyz!#%(){}[]<>+=/*:;.,~_"
FUTURE = (b"if(_update60)_update=function()_update60()_update60()end",
          b"if(_update60)_update=function()_update60()_update_buttons()_update60()end")


def unpack_old(d):
    """PICO-8's first packing (":c:"): Lexaloffle's decompress_mini"""
    n = d[4] * 256 + d[5]
    out = bytearray()
    at = 8
    while len(out) < n and at < len(d):
        v = d[at]
        at += 1
        if v < len(LITERALS):
            if v == 0:
                out.append(d[at])
                at += 1
            else:
                out.append(LITERALS[v])
        else:
            off = (v - len(LITERALS)) * 16 + d[at] % 16
            ln = d[at] // 16 + 2
            at += 1
            for _ in range(ln):
                out.append(out[-off] if 0 < off <= len(out) else 0)
    out = bytes(out[:n]).split(b"\0")[0]
    for f in FUTURE:
        if out.endswith(f):
            out = out[:-len(f)]
    return out


def unpack_pxa(d):
    """PICO-8's 0.2.0 packing ("pxa"): Lexaloffle's pxa_decompress"""
    raw_len, comp_len = d[4] * 256 + d[5], d[6] * 256 + d[7]
    st = {"pos": 8, "bit": 1}

    def bit():
        r = 1 if st["pos"] < len(d) and d[st["pos"]] & st["bit"] else 0
        st["bit"] <<= 1
        if st["bit"] == 256:
            st["bit"] = 1
            st["pos"] += 1
        return r

    def val(bits):
        v = 0
        for i in range(bits):
            if bit():
                v |= 1 << i
        return v

    def chain(link, most):
        top, v, read, vv = (1 << link) - 1, 0, 0, (1 << link) - 1
        while vv == top:
            vv = val(link)
            read += link
            v += vv
            if read >= most:
                return v
        return v

    lit = list(range(256))
    out = bytearray()
    while st["pos"] < comp_len and len(out) < raw_len:
        if bit() == 0:
            bits = (3 - chain(1, 2)) * 5
            num = val(bits)
            off = -1 if num == 0 and bits == 10 else num
            off += 1
            if off == 0:
                while len(out) < raw_len:
                    c = val(8)
                    if c == 0:
                        break
                    out.append(c)
            else:
                ln = chain(3, 100000) + 3
                for _ in range(ln):
                    out.append(out[-off] if off <= len(out) else 0)
        else:
            lpos, bits = 0, 0
            safety = 0
            while bit() == 1 and safety < 16:
                safety += 1
                lpos += 1 << (4 + bits)
                bits += 1
            lpos += val(bits + 4)
            if lpos > 255:
                raise CartError("damaged code")
            c = lit[lpos]
            out.append(c)
            del lit[lpos]
            lit.insert(0, c)
    return bytes(out[:raw_len]).split(b"\0")[0]


def parse_png(data):
    w, h, px = png_rgba(data)
    if (w, h) != (160, 205):
        raise CartError("a PNG that isn't a PICO-8 cart (160 x 205)")
    b = bytes(((px[i + 3] & 3) << 6) | ((px[i] & 3) << 4) | ((px[i + 1] & 3) << 2) | (px[i + 2] & 3)
              for i in range(0, len(px), 4))
    rom = b[:ROM]
    c = b[ROM:0x8000]
    if c[:4] == b":c:\0":
        code = unpack_old(c)
    elif c[:4] == b"\0pxa":
        code = unpack_pxa(c)
    else:
        code = c.split(b"\0")[0]
    # the label: each pixel the colour it is (the low 2 bits held data), else the closest
    exact = {(r & 0xFC, g & 0xFC, bb & 0xFC): k for k, (r, g, bb) in reversed(list(enumerate(COLORS)))}
    label = []
    for y in range(128):
        for x in range(128):
            i = ((24 + y) * 160 + 16 + x) * 4
            rgb = (px[i] & 0xFC, px[i + 1] & 0xFC, px[i + 2] & 0xFC)
            if rgb not in exact:
                exact[rgb] = min(range(32), key=lambda k: (sum((COLORS[k][j] - rgb[j]) ** 2 for j in range(3)), k))
            label.append(exact[rgb])
    return rom, code, label


# ------------------------------------------------------------------ the table
def label_bytes(label):
    use = [0] * 32
    for c in label:
        use[c] += 1
    keep = sorted(range(32), key=lambda c: (-use[c], c))[:16]
    keep = [c for c in keep if use[c]] or [0]
    keep += [0] * (16 - len(keep))
    near = {}
    for c in range(32):
        if c in keep:
            near[c] = keep.index(c)
        else:
            near[c] = min(range(16), key=lambda k: (sum((COLORS[c][j] - COLORS[keep[k]][j]) ** 2 for j in range(3)), k))
    out = bytearray(bytes((c & 15) | (0x80 if c >= 16 else 0) for c in keep))
    for i in range(0, 128 * 128, 2):
        out.append(near[label[i]] | near[label[i + 1]] << 4)
    return bytes(out)


def cart_name(path):
    base = os.path.basename(path)
    for ext in (".p8.png", ".p8"):
        if base.lower().endswith(ext):
            base = base[:-len(ext)]
            break
    name = base.encode("utf-8")
    k = min(len(name), NAME)
    while 0 < k < len(name) and (name[k] & 0xC0) == 0x80:
        k -= 1
    return name[:k]


def read_cart(path):
    data = open(path, "rb").read()
    rom, code, label = parse_png(data) if data[:8] == b"\x89PNG\r\n\x1a\n" else parse_p8(data)
    return rom, code, label


def table(carts):
    """carts: (name bytes, rom, code, label or None)"""
    out = bytearray(struct.pack("<III", MAGIC, VERSION, len(carts)))
    out += bytes(ENTRY * len(carts))
    for i, (name, rom, code, label) in enumerate(carts):
        offs = []
        for part in (rom, code, label_bytes(label) if label else None):
            if part is None:
                offs.append(0)
                continue
            out += bytes(-len(out) % 4)
            offs.append(len(out))
            out += part
        struct.pack_into("<IIII48s", out, 12 + ENTRY * i, offs[0], offs[1], len(code), offs[2], name.ljust(NAME, b"\0"))
    out += bytes(-len(out) % 4)
    return bytes(out)


def elf_object(blob):
    """A 32-bit ARM relocatable object: .rodata.np_carts, and np_carts in its symbol table."""
    shstr = b"\0.rodata.np_carts\0.symtab\0.strtab\0.shstrtab\0"
    strtab = b"\0np_carts\0"
    STB_GLOBAL, STT_OBJECT, STT_SECTION = 1, 1, 3
    symtab = struct.pack("<IIIBBH", 0, 0, 0, 0, 0, 0)
    symtab += struct.pack("<IIIBBH", 0, 0, 0, STT_SECTION, 0, 1)
    symtab += struct.pack("<IIIBBH", 1, 0, len(blob), STB_GLOBAL << 4 | STT_OBJECT, 0, 1)
    out = bytearray(52)
    offs = []
    for part in (blob, symtab, strtab, shstr):
        out += bytes(-len(out) % 4)
        offs.append(len(out))
        out += part
    out += bytes(-len(out) % 4)
    shoff = len(out)
    SHT_PROGBITS, SHT_SYMTAB, SHT_STRTAB, SHF_ALLOC = 1, 2, 3, 2
    sections = [
        (0, 0, 0, 0, 0, 0, 0, 0, 0, 0),
        (shstr.index(b".rodata.np_carts"), SHT_PROGBITS, SHF_ALLOC, 0, offs[0], len(blob), 0, 0, 4, 0),
        (shstr.index(b".symtab"), SHT_SYMTAB, 0, 0, offs[1], len(symtab), 3, 2, 4, 16),
        (shstr.index(b".strtab"), SHT_STRTAB, 0, 0, offs[2], len(strtab), 0, 0, 1, 0),
        (shstr.index(b".shstrtab"), SHT_STRTAB, 0, 0, offs[3], len(shstr), 0, 0, 1, 0),
    ]
    for s in sections:
        out += struct.pack("<10I", *s)
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<16sHHIIIIIHHHHHH", out, 0, ident, 1, 40, 1, 0, 0, shoff, 0, 52, 0, 0, 40, len(sections), 4)
    return bytes(out)


def c_source(blob):
    lines = [",".join(str(b) for b in blob[k:k + 32]) for k in range(0, len(blob), 32)]
    return ("/* Made by tools/p8cart.py: NumPico's table of carts (see that file). */\n#include <stdint.h>\n"
            f"static const uint8_t carts[{max(len(blob), 1)}] __attribute__((aligned(4))) = {{\n" +
            ",\n".join(lines) + "\n};\n"
            "const uint8_t *np_carts = carts;\n"
            f"uint32_t np_carts_size = {len(blob)};\n")


def main():
    ap = argparse.ArgumentParser(description="NumPico's table of carts (np_carts)")
    ap.add_argument("-o", dest="obj", help="ARM object to write")
    ap.add_argument("--c", dest="csrc", help="C source to write")
    ap.add_argument("--bin", dest="bin", help="raw table to write")
    ap.add_argument("--list", help="a file naming more carts, one a line")
    ap.add_argument("carts", nargs="*")
    a = ap.parse_args()
    if not (a.obj or a.csrc or a.bin):
        ap.error("say where: -o, --c or --bin")
    if a.list:
        a.carts += [line.strip() for line in open(a.list, encoding="utf-8") if line.strip()]
    carts = []
    for p in a.carts:
        try:
            rom, code, label = read_cart(p)
        except (CartError, zlib.error, IndexError, ValueError) as e:
            sys.exit(f"p8cart: {p}: {e}")
        carts.append((cart_name(p), rom, code, label))
    blob = table(carts)
    for path, data in ((a.obj, lambda: elf_object(blob)), (a.csrc, lambda: c_source(blob).encode()),
                       (a.bin, lambda: blob)):
        if not path:
            continue
        data = data()
        if not os.path.exists(path) or open(path, "rb").read() != data:
            open(path, "wb").write(data)
            print(f"CARTS   {path}: {len(carts)} carts, {len(blob)} bytes")


if __name__ == "__main__":
    main()
