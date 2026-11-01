#!/usr/bin/env python3
"""How fast Temple Run draws a run at an N0110's speed (../../tools/emu.py): bench.py build/bench/templerun.nwa
[SECONDS] [PICTURES_DIR] (make bench). The timing build (EXTRA=-DTR_BENCH) takes two game steps a drawing, so every
build draws the same frames; nothing hurts the runner (it is saved where it would fall). Prints the drawing's
milliseconds from 8 s on."""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import emu  # noqa: E402

nwa = sys.argv[1]
secs = int(sys.argv[2]) if len(sys.argv) > 2 else 40
shots = sys.argv[3] if len(sys.argv) > 3 else None
emu.configure("n0110")
c = emu.Calculator(nwa)
c.keys = emu.parse_keys("1500-1600:ok,2500-2600:ok,3500-3600:ok")


def rd(name):
    return struct.unpack("<I", bytes(c.uc.mem_read(c.symbols[name], 4)))[0]


c.run(100)
c.uc.mem_write(c.symbols["host_god"], struct.pack("<I", 1))
c.run(8000)
f0, d0 = rd("perf_frames"), rd("perf_draw")
for t in range(8000, secs * 1000 + 1, 4000):
    c.run(t)
    if shots:
        os.makedirs(shots, exist_ok=True)
        c.save_png(os.path.join(shots, f"t{t // 1000:02d}.png"))
c.run(secs * 1000)
n = rd("perf_frames") - f0
print(f"{n} frames: drawing {(rd('perf_draw') - d0) / max(n, 1):.2f} ms a frame; slowest {rd('perf_max')} ms, "
      f"{rd('perf_slow')} over 33 ms")
