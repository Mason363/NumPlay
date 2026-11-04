#!/usr/bin/env python3
"""Makes Retro-Go's table of games, np_roms, as an object file to link into RetroGo.nwa or NumPlay.

The table (the customizer website makes the same one; games/retrogo reads it in place, in flash)
is one read-only section, .rodata.np_roms, 4-byte aligned, global symbol np_roms:
  u32 magic 0x4D52504E ("NPRM"), u32 version 1, u32 count;
  count entries of 52 bytes: u32 offset (from the start of np_roms to the ROM's bytes), u32 size,
    u8 system (1 Game Boy, 2 Game Boy Color, 3 NES, 4 Master System, 5 Game Gear, 6 SG-1000, 7 ColecoVision,
    8 Game & Watch, 9 Mega Drive), u8 reserved[3]
    (zero), char name[40] (UTF-8, zero-padded);
  then the ROMs' bytes, each starting 4-byte aligned.
All numbers are little-endian. To be the same byte for byte as the website's for the same files:
- the ROMs come in the order given, the first at 12 + 52 * count rounded up to a multiple of 4,
  each next one at the end of the one before rounded up to a multiple of 4, zeros in between;
- the table ends with the last ROM's bytes, then zeros up to a multiple of 4 (no ROMs: the 12-byte
  header with count 0);
- the system comes from the file's extension, in any case: .gb 1, .gbc 2, .nes 3, .sms 4, .gg 5, .sg 6, .col 7,
  .gw 8, .md .gen .bin .smd 9;
- the bytes are the file's, but: Game Boy and Sega 8-bit files lose their padding (trim below); a .gw
  file packed with LZ4 (an LZ4 frame, as LCD-Game-Shrinker writes them) is unpacked; a Mega Drive
  game is made plain (an .smd file's 512-byte header dropped and its 16 KB blocks put back in order,
  as gwenesis does) and its bytes swapped in pairs (the emulator reads its 16-bit words as they are,
  on a little-endian processor), padded with a zero to an even size;
- the name is the file's name without its last extension, its UTF-8 bytes cut to the longest
  start of at most 40 bytes that ends on a whole letter, zero-padded to 40 (no ending zero when it
  takes all 40).

usage:
  romtable.py -o roms.o [ROM...]       an ARM object for the calculator (the default)
  romtable.py -o roms.o --list FILE    the ROMs named in FILE, one a line (names with spaces)
  romtable.py --c roms.c [ROM...]      the same table as C, for the simulator and tests
  romtable.py --bin roms.bin [ROM...]  just the table's bytes"""
import argparse
import os
import struct
import sys

MAGIC, VERSION, ENTRY, NAME = 0x4D52504E, 1, 52, 40
SYSTEMS = {".gb": 1, ".gbc": 2, ".nes": 3, ".sms": 4, ".gg": 5, ".sg": 6, ".col": 7, ".gw": 8,
           ".md": 9, ".gen": 9, ".bin": 9, ".smd": 9}


def lz4_unpack(data):
    """an LZ4 frame's bytes (its block and content checksums not checked)"""
    if data[:4] != b"\x04\x22\x4d\x18":
        return data
    flg = data[4]
    i = 7 + (8 if flg & 0x08 else 0) + (4 if flg & 0x01 else 0)
    out = bytearray()
    while i + 4 <= len(data):
        n = struct.unpack_from("<I", data, i)[0]
        i += 4
        if n == 0:
            break
        raw, n = n & 0x80000000, n & 0x7FFFFFFF
        blk = data[i:i + n]
        i += n + (4 if flg & 0x10 else 0)
        if raw:
            out += blk
            continue
        j = 0
        while j < len(blk):
            tok = blk[j]
            j += 1
            lit = tok >> 4
            if lit == 15:
                while True:
                    b = blk[j]
                    j += 1
                    lit += b
                    if b != 255:
                        break
            out += blk[j:j + lit]
            j += lit
            if j >= len(blk):
                break
            off = blk[j] | blk[j + 1] << 8
            j += 2
            ml = (tok & 15) + 4
            if (tok & 15) == 15:
                while True:
                    b = blk[j]
                    j += 1
                    ml += b
                    if b != 255:
                        break
            if not 0 < off <= len(out):
                sys.exit("romtable: a broken LZ4 file")
            for _ in range(ml):
                out.append(out[-off])
    return bytes(out)


def mega_drive(data, smd):
    """a Mega Drive game as gwenesis reads it in place: plain (not .smd), its bytes swapped in pairs"""
    if (smd or (len(data) > 0x200 and data[1] == 3 and data[8] == 0xAA and data[9] == 0xBB)) and len(data) % 0x4000 == 0x200:
        body, plain = data[0x200:], bytearray()
        for k in range(0, len(body), 0x4000):
            blk = body[k:k + 0x4000]
            for j in range(0x2000):
                plain += bytes((blk[0x2000 + j], blk[j]))
        data = bytes(plain)
    if len(data) % 2:
        data += b"\0"
    swapped = bytearray(len(data))
    swapped[0::2], swapped[1::2] = data[1::2], data[0::2]
    return bytes(swapped)


def rom_bytes(path, system):
    data = open(path, "rb").read()
    if system == 8:
        return lz4_unpack(data)
    if system == 9:
        return mega_drive(data, path.lower().endswith(".smd"))
    return trim(data, system)


def entry_name(path):
    name = os.path.splitext(os.path.basename(path))[0].encode("utf-8")
    k = min(len(name), NAME)
    while k < len(name) and k > 0 and (name[k] & 0xC0) == 0x80:  # not inside a letter
        k -= 1
    return name[:k]


def trim(data, system):
    """The padding a ROM's file has to reach a power of two (16 KB banks at its end, all 0xFF or all 0x00) left out:
    the cores repeat the banks a game has for those it lacks, as a flash cart does, and games never read their padding.
    Not for NES files (their banks follow a header) or ColecoVision; the Master System's 32 KB rule stays as it was."""
    if system not in (1, 2, 4, 5, 6) or len(data) % 16384:
        return data
    keep = 0x8000 if system <= 2 else (len(data) if len(data) <= 0x8000 else 0xC000)
    pad, n = data[-1], len(data)
    if pad not in (0x00, 0xFF):
        return data
    while n - 16384 >= keep and data[n - 16384:n].count(pad) == 16384:
        n -= 16384
    return data[:n]


def table(paths):
    roms = []
    for p in paths:
        ext = os.path.splitext(p)[1].lower()
        if ext not in SYSTEMS:
            sys.exit(f"romtable: {p}: not a .gb, .gbc, .nes, .sms, .gg, .sg, .col, .gw, .md, .gen, .bin or .smd file")
        roms.append((rom_bytes(p, SYSTEMS[ext]), SYSTEMS[ext], entry_name(p)))
    out = bytearray(struct.pack("<III", MAGIC, VERSION, len(roms)))
    out += bytes(ENTRY * len(roms))
    for i, (data, system, name) in enumerate(roms):
        out += bytes(-len(out) % 4)
        struct.pack_into("<IIB3x40s", out, 12 + ENTRY * i, len(out), len(data), system, name.ljust(NAME, b"\0"))
        out += data
    out += bytes(-len(out) % 4)
    return bytes(out)


def elf_object(blob):
    """A 32-bit ARM relocatable object: .rodata.np_roms, and np_roms in its symbol table."""
    shstr = b"\0.rodata.np_roms\0.symtab\0.strtab\0.shstrtab\0"
    strtab = b"\0np_roms\0"
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
        (shstr.index(b".rodata.np_roms"), SHT_PROGBITS, SHF_ALLOC, 0, offs[0], len(blob), 0, 0, 4, 0),
        (shstr.index(b".symtab"), SHT_SYMTAB, 0, 0, offs[1], len(symtab), 3, 2, 4, 16),
        (shstr.index(b".strtab"), SHT_STRTAB, 0, 0, offs[2], len(strtab), 0, 0, 1, 0),
        (shstr.index(b".shstrtab"), SHT_STRTAB, 0, 0, offs[3], len(shstr), 0, 0, 1, 0),
    ]
    for s in sections:
        out += struct.pack("<10I", *s)
    ident = b"\x7fELF\x01\x01\x01" + bytes(9)
    struct.pack_into("<16sHHIIIIIHHHHHH", out, 0, ident, 1, 40, 1, 0, 0, shoff, 0, 52, 0, 0, 40,
                     len(sections), 4)
    return bytes(out)


def c_source(blob):
    lines = [",".join(str(b) for b in blob[k:k + 32]) for k in range(0, len(blob), 32)]
    return ("/* Made by tools/romtable.py: Retro-Go's table of games (see that file). */\n"
            "#include <stdint.h>\n"
            f"const uint8_t np_roms[{len(blob)}] __attribute__((aligned(4))) = {{\n" +
            ",\n".join(lines) + "\n};\n"
            f"const uint32_t np_roms_size = {len(blob)};\n")


def main():
    ap = argparse.ArgumentParser(description="Retro-Go's table of games (np_roms)")
    ap.add_argument("-o", dest="obj", help="ARM object to write")
    ap.add_argument("--c", dest="csrc", help="C source to write")
    ap.add_argument("--bin", dest="bin", help="raw table to write")
    ap.add_argument("--list", help="a file naming more ROMs, one a line (for names with spaces)")
    ap.add_argument("roms", nargs="*")
    a = ap.parse_args()
    if not (a.obj or a.csrc or a.bin):
        ap.error("say where: -o, --c or --bin")
    if a.list:
        a.roms += [line.strip() for line in open(a.list, encoding="utf-8") if line.strip()]
    blob = table(a.roms)
    for path, data in ((a.obj, lambda: elf_object(blob)), (a.csrc, lambda: c_source(blob).encode()),
                       (a.bin, lambda: blob)):
        if not path:
            continue
        data = data()
        # (a file that would stay the same is left alone: make links nothing again)
        if not os.path.exists(path) or open(path, "rb").read() != data:
            open(path, "wb").write(data)
            print(f"ROMS    {path}: {len(a.roms)} games, {len(blob)} bytes")


if __name__ == "__main__":
    main()
