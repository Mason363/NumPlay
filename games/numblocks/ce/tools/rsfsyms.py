#!/usr/bin/env python3
"""The program's symbols, from its map, for the archive's copy of the inner loops (flash/rsf.s):
   rsfsyms.py bin/NUMBLOCK.map > obj/rsf_syms.inc"""
import re
import sys

seen = set()
for m in re.finditer(r"^\s+0x([0-9a-f]+)\s+([A-Za-z_]\w*)\s*$", open(sys.argv[1]).read(), re.M):
    addr, name = int(m.group(1), 16), m.group(2)
    if name in seen or name.startswith("b_"):
        continue
    seen.add(name)
    print(f"\t.equ\t{name}, 0x{addr:06X}")
