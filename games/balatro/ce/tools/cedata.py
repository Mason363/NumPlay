#!/usr/bin/env python3
"""The NumWorks version's data.c for the TI-84 Plus CE: the same file without its 11 KB of texts, which the CE reads
from the archive (tools/mkart.py puts them in BALLUT; src/art_ce.c points ce_text_blob at them).

Usage: cedata.py ../src/data.c gen/data.c"""
import re
import sys


def blob(src):
    """the texts' bytes, from data.c"""
    m = re.search(r"const uint8_t text_blob\[(\d+)\] = \{(.*?)\};", src, re.S)
    if not m:
        raise SystemExit("cedata: no text_blob in data.c")
    data = bytes(int(v, 0) for v in re.findall(r"0x[0-9A-Fa-f]+|\d+", m.group(2)))
    if len(data) != int(m.group(1)):
        raise SystemExit("cedata: text_blob's size")
    return data


def main():
    src = open(sys.argv[1], encoding="utf-8").read()
    blob(src)
    out = re.sub(r"const uint8_t text_blob\[\d+\] = \{.*?\};\n", "", src, count=1, flags=re.S)
    out = out.replace('#include "data.h"\n', '#include "../../src/data.h"\nextern const uint8_t *ce_text_blob;\n'
                      '#define text_blob ce_text_blob\n', 1)
    out = out.replace('#include "../../common/np_text.h"', '#include "../../../common/np_text.h"', 1)
    open(sys.argv[2], "w", encoding="utf-8").write("/* Made by tools/cedata.py from src/data.c: do not edit. */\n" + out)


if __name__ == "__main__":
    main()
