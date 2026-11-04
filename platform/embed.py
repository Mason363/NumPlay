#!/usr/bin/env python3
"""A game's data files (its .S files: `.incbin` under a global label) as C, for targets with no assembler for them
(the web's WebAssembly). The .S files are read as the calculator's assembler would (ELF), their macros (DATA_BIN...)
from the flags given.

  python3 platform/embed.py out.c [-Dflags...] file.S...

Writes, for each label, `const unsigned char label[] = { #embed "file" };`, and for a `.long end - label` word,
`const unsigned int name = sizeof label;`."""
import os
import re
import subprocess
import sys


def main():
    out, rest = sys.argv[1], sys.argv[2:]
    flags = [a for a in rest if a.startswith(("-D", "-I", "-U"))]
    files = [a for a in rest if not a.startswith("-")]
    lines = ["/* made by platform/embed.py */", "#include <stdint.h>"]
    for f in files:
        text = subprocess.run(["cc", "-E", "-P", "-x", "assembler-with-cpp", "-U__APPLE__", "-D__ELF__", *flags, f],
                              capture_output=True, text=True, check=True).stdout
        glob, label, blobs = set(), None, {}
        for raw in text.splitlines():
            s = raw.split("/*")[0].split("@")[0].strip()
            m = re.match(r"\.globa?l\s+(\w+)", s)
            if m:
                glob.add(m.group(1))
                continue
            m = re.match(r"(\w+):\s*(.*)", s)
            if m:
                label, s = m.group(1), m.group(2)
            m = re.match(r"\.incbin\s+\"([^\"]+)\"", s)
            if m and label:
                path = m.group(1)
                for d in [".", os.path.dirname(f)] + [a[2:] for a in flags if a.startswith("-I")]:
                    if os.path.exists(os.path.join(d, path)):
                        path = os.path.join(d, path)
                        break
                path = os.path.abspath(path)
                blobs[label] = path
                continue
            m = re.match(r"\.(long|word|int)\s+(\w+)\s*-\s*(\w+)", s)
            if m and label in glob and m.group(3) in blobs:
                lines.append(f"const unsigned int {label} = {os.path.getsize(blobs[m.group(3)])};")
        for name, path in blobs.items():
            if name in glob:
                lines.append(f"__attribute__((aligned(4))) const unsigned char {name}[] = {{\n#embed \"{path}\"\n}};")
    open(out, "w").write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
