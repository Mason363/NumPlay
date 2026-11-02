#!/usr/bin/env python3
"""How fast Temple Run draws a run (../../tools/emu.py, which runs at an N0110's speed whatever the model; an N0120 is
about 2.5 times faster): bench.py build/bench/templerun.nwa [--model n0110|n0120] [--seconds S] [--shots DIR]
[--csv FILE] (make bench). The timing build (EXTRA=-DTR_BENCH) takes two game steps a drawing, so every build draws the
same frames; nothing hurts the runner (it is saved where it would fall). Prints the drawing's milliseconds a frame:
in all, in the run's first 5 s (the start), on straights (a second or more from a turn), at turns (half a second or
less from one) and on the busiest tenth of the frames (the most polygons)."""
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
ap.add_argument("--csv", help="each drawing in the run: frame, distance, turns, polygons, milliseconds")
a = ap.parse_args()
emu.configure(a.model)
c = emu.Calculator(a.nwa)
c.keys = emu.parse_keys("1500-1600:ok,2500-2600:ok,3500-3600:ok")


def rd(name):
    return struct.unpack("<I", bytes(c.uc.mem_read(c.symbols[name], 4)))[0]


frames = []   # (distance, turns, polygons, ms) of each drawing in the run
last = [0, 0]


def each_ms(calc):
    n = rd("perf_frames")
    if n == last[0]:
        return
    d = rd("perf_draw")
    if rd("bench_run"):
        frames.append((rd("bench_dist"), rd("bench_turns"), rd("bench_polys"), (d - last[1]) / (n - last[0])))
    last[0], last[1] = n, d
    if a.shots and calc.now_ms >= next_shot[0]:
        os.makedirs(a.shots, exist_ok=True)
        calc.save_png(os.path.join(a.shots, f"t{int(calc.now_ms) // 1000:02d}.png"))
        next_shot[0] += 4000


next_shot = [4000]
c.run(100)
c.uc.mem_write(c.symbols["host_god"], struct.pack("<I", 1))
last[0], last[1] = rd("perf_frames"), rd("perf_draw")
c.run(a.seconds * 1000, each_ms, 1)
if a.csv:
    with open(a.csv, "w") as f:
        f.write("frame,dist,turns,polys,ms\n")
        for i, r in enumerate(frames):
            f.write(f"{i},{r[0]},{r[1]},{r[2]},{r[3]:.2f}\n")


def mean(rows):
    return sum(r[3] for r in rows) / max(len(rows), 1)


START = 150   # (5 s: two steps a drawing)
turn_at = [i for i in range(1, len(frames)) if frames[i][1] != frames[i - 1][1]]


def near_turn(i, w):
    return any(abs(i - t) <= w for t in turn_at)


rest = list(range(START, len(frames)))
straight = [frames[i] for i in rest if not near_turn(i, 30)]
turns = [frames[i] for i in rest if near_turn(i, 15)]
busy = sorted((frames[i] for i in rest), key=lambda r: -r[2])[:max(len(rest) // 10, 1)]
print(f"{a.model}: {len(frames)} frames, {mean(frames):.2f} ms a frame; start {mean(frames[:START]):.2f}, "
      f"straight {mean(straight):.2f} ({len(straight)}), turns {mean(turns):.2f} ({len(turns)}), "
      f"busiest {mean(busy):.2f} ({len(busy)}, {min(r[2] for r in busy)}+ polygons); "
      f"slowest {max(r[3] for r in frames):.0f} ms, {sum(r[3] > 33 for r in frames)} over 33 ms")
