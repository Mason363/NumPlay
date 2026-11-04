#!/usr/bin/env python3
"""TI-84 Plus CE appvars (.8xv, archived) of files: each file's name without its extension is the appvar's.

    python3 tools/appvar.py OUT_DIR data/CELG0.bin data/CEL10.bin ..."""
import os
import struct
import sys


def appvar(name, data, archived=True):
    assert len(data) <= 65505, (name, len(data))
    var = struct.pack("<H", len(data)) + data
    entry = struct.pack("<HHB8sBBH", 13, len(var), 0x15, name.encode().ljust(8, b"\0"), 0, 0x80 if archived else 0,
                        len(var)) + var
    out = b"**TI83F*\x1a\x0a\x00" + b"Celeste for the TI-84 Plus CE".ljust(42, b"\0") + struct.pack("<H", len(entry))
    return out + entry + struct.pack("<H", sum(entry) & 0xFFFF)


if __name__ == "__main__":
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for path in sys.argv[2:]:
        name = os.path.splitext(os.path.basename(path))[0]
        open(os.path.join(out, name + ".8xv"), "wb").write(appvar(name, open(path, "rb").read()))
