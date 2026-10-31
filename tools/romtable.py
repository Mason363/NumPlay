#!/usr/bin/env python3
"""Makes Retro-Go's table of games, np_roms, as an object file to link into RetroGo.nwa or NumPlay.

The table (the customizer website makes the same one; games/retrogo reads it in place, in flash)
is one read-only section, .rodata.np_roms, 4-byte aligned, global symbol np_roms:
  u32 magic 0x4D52504E ("NPRM"), u32 version 1, u32 count;
  count entries of 52 bytes: u32 offset (from the start of np_roms to the ROM's bytes), u32 size,
    u8 system (1 Game Boy, 2 Game Boy Color, 3 NES, 4 Master System, 5 Game Gear), u8 reserved[3]
    (zero), char name[40] (UTF-8, zero-padded);
  then the ROMs' bytes, each starting 4-byte aligned.
All numbers are little-endian. To be the same byte for byte as the website's for the same files:
- the ROMs come in the order given, the first at 12 + 52 * count rounded up to a multiple of 4,
  each next one at the end of the one before rounded up to a multiple of 4, zeros in between;
- the table ends with the last ROM's bytes, then zeros up to a multiple of 4 (no ROMs: the 12-byte
  header with count 0);
- the system comes from the file's extension, in any case: .gb 1, .gbc 2, .nes 3, .sms 4, .gg 5, .sg 6;
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
SYSTEMS = {".gb": 1, ".gbc": 2, ".nes": 3, ".sms": 4, ".gg": 5, ".sg": 6}


def entry_name(path):
    name = os.path.splitext(os.path.basename(path))[0].encode("utf-8")
    k = min(len(name), NAME)
    while k < len(name) and k > 0 and (name[k] & 0xC0) == 0x80:  # not inside a letter
        k -= 1
    return name[:k]


def table(paths):
    roms = []
    for p in paths:
        ext = os.path.splitext(p)[1].lower()
        if ext not in SYSTEMS:
            sys.exit(f"romtable: {p}: not a .gb, .gbc, .nes, .sms, .gg or .sg file")
        roms.append((open(p, "rb").read(), SYSTEMS[ext], entry_name(p)))
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
