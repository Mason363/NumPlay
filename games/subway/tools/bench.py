#!/usr/bin/env python3
"""How fast Subway Surfers draws a run (../../tools/emu.py, which runs at an N0110's speed whatever the model; an N0120 is
about 2.5 times faster): bench.py build/bench/subway.nwa [--model n0110|n0120] [--seconds S] [--shots DIR] [--csv FILE]
(make bench). The timing build (EXTRA="-DSS_BENCH") takes two game steps a drawing, so every build draws the same frames;
tests/bot.h plays and nothing hurts the runner. Prints the drawing's milliseconds a frame: in all, in the run's first
5 s (the start), on the busiest tenth of the frames (the most polygons), and the slowest."""
import argparse
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..", "tools"))
import emu  # noqa: E402

ap = argparse.ArgumentParser()
ap.add_argument("nwa")
ap.add_argument("--model", choices=["n0110", "n0120"], default="n0110")
ap.add_argument("--seconds", type=int, default=40)
ap.add_argument("--shots", help="a picture every 4 s here")
ap.add_argument("--csv", help="each drawing in the run: frame, distance, polygons, milliseconds")
a = ap.parse_args()
emu.configure(a.model)
c = emu.Calculator(a.nwa)
c.keys = emu.parse_keys("1500-1600:ok,2500-2600:ok,3500-3600:ok")


def rd(name):
    return struct.unpack("<I", bytes(c.uc.mem_read(c.symbols[name], 4)))[0]


frames = []   # (distance, polygons, ms) of each drawing in the run
last = [0, 0]
next_shot = [4000]


def each_ms(calc):
    n = rd("perf_frames")
    if n == last[0]:
        return
    d = rd("perf_draw")
    if rd("bench_run"):
        frames.append((rd("bench_dist"), rd("bench_polys"), (d - last[1]) / (n - last[0])))
    last[0], last[1] = n, d
    if a.shots and calc.now_ms >= next_shot[0]:
        os.makedirs(a.shots, exist_ok=True)
        calc.save_png(os.path.join(a.shots, f"t{int(calc.now_ms) // 1000:02d}.png"))
        next_shot[0] += 4000


c.run(100)
last[0], last[1] = rd("perf_frames"), rd("perf_draw")
c.run(a.seconds * 1000, each_ms, 1)
if a.csv:
    with open(a.csv, "w") as f:
        f.write("frame,dist,polys,ms\n")
        for i, r in enumerate(frames):
            f.write(f"{i},{r[0]},{r[1]},{r[2]:.2f}\n")


def mean(rows):
    return sum(r[2] for r in rows) / max(len(rows), 1)


START = 150   # (5 s: two steps a drawing)
rest = frames[START:]
busy = sorted(rest, key=lambda r: -r[1])[:max(len(rest) // 10, 1)]
print(f"{a.model}: {len(frames)} frames, {mean(frames):.2f} ms a frame; start {mean(frames[:START]):.2f}, "
      f"after it {mean(rest):.2f}, busiest {mean(busy):.2f} ({len(busy)}, {min(r[1] for r in busy)}+ polygons); "
      f"slowest {max(r[2] for r in frames):.0f} ms, {sum(r[2] > 33 for r in frames)} over 33 ms")
