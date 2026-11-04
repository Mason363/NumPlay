#!/usr/bin/env python3
"""Where Balatro's code goes on the TI-84 Plus CE (see ce.ld): the functions and constant data of the linked object,
biggest first, into the places outside the program where they fit (the screen memory's second half, the pixel
shadow, the room after the static data); the rest stays in the program. Writes gen/place.ld, which ce.ld includes.

Usage: place.py OBJDUMP obj/BALATRO.o gen/place.ld"""
import re
import subprocess
import sys

BSS_LOW, BSS_HIGH = 0xD052C6, 0xD13FD8
VRAM = 0x12C00          # 0xD52C00..0xD657FF
PSHADOW = 8400           # 0xD031F6..0xD052C6
KEEP = re.compile(r"^\.(text|rodata)\.boot|^\.text$|^\.text\.__")  # the loader's, the library's: in the program


def main():
    objdump, obj, out = sys.argv[1:4]
    txt = subprocess.run([objdump, "-h", obj], capture_output=True, text=True, check=True).stdout
    secs, bss = [], 0
    for m in re.finditer(r"^\s*\d+\s+(\S+)\s+([0-9a-f]+)\s", txt, re.M):
        name, size = m.group(1), int(m.group(2), 16)
        if name.startswith((".bss", "COMMON")):
            bss += size
        elif name.startswith((".text", ".rodata")) and size and not KEEP.match(name):
            secs.append((size, name))
    bss_size = (bss + 64 + 255) & ~255
    bins = [["vram", VRAM, []], ["pshadow", PSHADOW, []], ["bssx", BSS_HIGH - BSS_LOW - bss_size, []]]
    left = 0
    for size, name in sorted(secs, reverse=True):
        for b in bins:
            if size <= b[1]:
                b[1] -= size
                b[2].append(name)
                break
        else:
            left += size
    with open(out, "w") as f:
        f.write("/* Made by tools/place.py: do not edit. */\n")
        f.write(f"BSS_SIZE = {bss_size:#x};\n")
        for name, room, names in bins:
            f.write(f"/* {name}: {room} bytes left */\n")
        for name, _, names in bins:
            with open(out[:-3] + "_" + name + ".ld", "w") as g:
                g.write("".join(f"        *({n})\n" for n in sorted(names)))
    used = {b[0]: (VRAM if b[0] == "vram" else PSHADOW if b[0] == "pshadow" else BSS_HIGH - BSS_LOW - bss_size) - b[1]
            for b in bins}
    print(f"place: static data {bss_size}; code in the screen's half {used['vram']}, the pixel shadow "
          f"{used['pshadow']}, after the data {used['bssx']}; in the program {left} (and the loader's, the library's)")


if __name__ == "__main__":
    main()
