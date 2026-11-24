"""Attribute bytes in GNU ld's two maps without relinking the artifact."""
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

from elftools.elf.elffile import ELFFile


def inputs(path):
    body = path.read_text().split("Linker script and memory map", 1)[1]
    body = re.sub(r"^( \.[^\s]+)\n\s+(0x)", r"\1 \2", body, flags=re.M)
    output = None
    for line in body.splitlines():
        if line and not line[0].isspace():
            output = line.split()[0]
        match = re.match(r"^ (\S+)\s+0x[\da-f]+\s+(0x[\da-f]+)\s+(.+)", line)
        if match and int(match[2], 16):
            yield output, match[1], int(match[2], 16), match[3].strip()


def main(folder):
    owners = defaultdict(set)
    for output, section, size, source in inputs(folder / "kit/base.o.map"):
        if ".a(" in source:
            owner = "shared libraries"
        elif "ltrans" in source or "/arm/" in source:
            owner = "launcher"
        else:
            owner = "unclassified base inputs"
        owners[output, size].add(owner)
    app = folder / "TwiliPlay.nwa"
    with app.open("rb") as stream:
        allocated = {s.name: s.data_size for s in ELFFile(stream).iter_sections()
                     if s["sh_flags"] & 2 and s["sh_type"] != "SHT_NOBITS"
                     and s.name != ".rodata.eadk_api_level"}
    totals = Counter()
    for output, section, size, source in inputs(folder / "TwiliPlay.nwa.map"):
        if output not in allocated:
            continue
        if output.startswith(".rodata.np."):
            owner = "game blocks"
        elif source.endswith("/base.o"):
            choices = owners[section, size]
            owner = next(iter(choices)) if len(choices) == 1 else "ambiguous base inputs"
        elif "/variant/" in source:
            owner = "launcher"
        else:
            owner = "game table and other metadata"
        totals[owner] += size
    totals["linker padding"] = sum(allocated.values()) - sum(totals.values())
    totals["installer runtime, header and alignment"] = (folder / "TwiliPlay.bin").stat().st_size - sum(allocated.values())
    for label, size in totals.items():
        print(f"FOOTPRINT {label}: {size}")
    upper_bound = sum(totals[label] for label in ("launcher", "ambiguous base inputs", "unclassified base inputs"))
    if upper_bound > 102400:
        raise ValueError("launcher exceeds the 100 KiB design budget")
    if min(totals.values()) < 0:
        raise ValueError("map totals do not reconcile")


if __name__ == "__main__":
    main(Path(sys.argv[1]))
