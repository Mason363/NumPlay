#!/usr/bin/env python3
"""The 68000's opcode table for gwenesis' Musashi (src/md/m68kops.h), small.

Musashi builds a table of 65536 handlers (a function pointer each, 256 KB on the calculator) and
65536 cycle counts from its list of opcode patterns (m68k_opcode_handler_table, the builder
m68ki_build_opcode_table under BUILD_TABLES). This does the same here, at build time, and writes
each opcode as a 16-bit number of one of the (handler, cycles) pairs instead: 128 KB of table and
a short list of pairs.

  m68ktab.py src/md/m68kops.h OUT.h"""
import re
import sys

MUL = 7  # (m68kcpu.c: the cycles in the master clock's)


def patterns(src):
    body = src[src.index("m68k_opcode_handler_table[] =") :]
    body = body[: body.index("{0, 0, 0, 0}")]
    return [(m.group(1), int(m.group(2), 16), int(m.group(3), 16), int(m.group(4)))
            for m in re.finditer(r"\{(m68k_op_\w+)\s*,\s*0x([0-9a-fA-F]+),\s*0x([0-9a-fA-F]+),\s*(\d+)\}", body)]


def build(pats):
    """m68ki_build_opcode_table, as it is: the general patterns first, then the special masks"""
    handler, cycles = ["m68k_op_illegal"] * 0x10000, [0] * 0x10000  # (illegal: 0, as gwenesis' own table has it)
    k = 0

    def put(i, p):
        handler[i], cycles[i] = p[0], p[3] * MUL

    while k < len(pats) and pats[k][1] != 0xFF00:
        p = pats[k]
        for i in range(0x10000):
            if i & p[1] == p[2]:
                put(i, p)
        k += 1
    while k < len(pats) and pats[k][1] == 0xFF00:
        for i in range(0x100):
            put(pats[k][2] | i, pats[k])
        k += 1
    while k < len(pats) and pats[k][1] == 0xF1F8:
        for i in range(8):
            for j in range(8):
                put(pats[k][2] | i << 9 | j, pats[k])
        k += 1
    while k < len(pats) and pats[k][1] == 0xFFF0:
        for i in range(0x10):
            put(pats[k][2] | i, pats[k])
        k += 1
    while k < len(pats) and pats[k][1] == 0xF1FF:
        for i in range(8):
            put(pats[k][2] | i << 9, pats[k])
        k += 1
    while k < len(pats) and pats[k][1] == 0xFFF8:
        for i in range(8):
            put(pats[k][2] | i, pats[k])
        k += 1
    while k < len(pats) and pats[k][1] == 0xFFFF:
        put(pats[k][2], pats[k])
        k += 1
    if k != len(pats):
        sys.exit(f"m68ktab: pattern {pats[k]} not taken")
    return handler, cycles


def main():
    src = open(sys.argv[1], encoding="latin-1").read()
    handler, cycles = build(patterns(src))
    pairs, index = {}, []
    for h, c in zip(handler, cycles):
        index.append(pairs.setdefault((h, c), len(pairs)))
    names = sorted(pairs, key=pairs.get)
    out = ["/* Made by tools/m68ktab.py from src/md/m68kops.h: the 68000's opcodes, each the number of",
           " * its (handler, cycles) pair. */",
           "".join(f"static void {h}(void);\n" for h in sorted({h for h, _ in names})),
           f"static void (*const m68ki_pair_handler[{len(names)}])(void) = {{",
           ",".join(h for h, _ in names), "};",
           f"static const unsigned char m68ki_pair_cycles[{len(names)}] = {{",
           ",".join(str(c) for _, c in names), "};",
           "static const unsigned short m68ki_opcode_pair[0x10000] = {"]
    out += [",".join(str(v) for v in index[k:k + 64]) + "," for k in range(0, 0x10000, 64)]
    out.append("};")
    open(sys.argv[2], "w").write("\n".join(out) + "\n")
    print(f"M68K    {sys.argv[2]}: {len(names)} handler and cycle pairs")


if __name__ == "__main__":
    main()
