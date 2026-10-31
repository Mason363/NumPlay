#!/usr/bin/env python3
"""The frame rate of the attacks at an N0110's speed (tools/emu.py): armperf.py output/armperf.nwa [ATTACK...]
(numbers as in src/attacks.h; the busiest by default, several at a time). Each attack is played once, start to end,
the heart unhurt, every frame drawn as in the game and timed. Steady 30 fps: no frame over 33 ms."""
import os
import struct
import sys
from concurrent.futures import ProcessPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))
import emu  # noqa: E402

NAMES = ["Intro", "Bone gap", "Blue bones", "Bone gap 2", "Platforms", "Platforms 2", "Platforms 3", "Platforms 4",
         "Platform blasters", "Platforms 4 hard", "Bone gap fast", "Bone slide", "Platform blasters fast",
         "The break", "Multi 1", "Random blasters", "Multi 2", "Bone stab", "Bone stab 2", "Random blasters 2",
         "Bone slide 2", "Multi 3", "Bone stab 3", "The final attack"]
BUSIEST = [0, 5, 12, 16, 19, 21, 22, 23]


def one(args):
    nwa, a = args
    emu.configure("n0110")
    c = emu.Calculator(nwa)
    c.run(300)  # (the title, waiting)
    c.uc.mem_write(c.symbols["perf_attack"], struct.pack("<I", a))
    c.uc.mem_write(c.symbols["perf_go"], struct.pack("<I", 1))
    t = 300
    while t < 120_000:
        t += 5000
        c.run(t)
        if struct.unpack("<I", bytes(c.uc.mem_read(c.symbols["perf_done"], 4)))[0]:
            break
    out = struct.unpack("<8I", bytes(c.uc.mem_read(c.symbols["perf_out"], 32)))
    done, = struct.unpack("<I", bytes(c.uc.mem_read(c.symbols["perf_done"], 4)))
    return a, out, done


def main():
    nwa = sys.argv[1]
    attacks = [int(x) for x in sys.argv[2:]] or BUSIEST
    bad = 0
    with ProcessPoolExecutor(max_workers=min(8, len(attacks))) as ex:
        for a, o, done in ex.map(one, [(nwa, a) for a in attacks]):
            frames, work, worst, over = o[0], o[1], o[2], o[3]
            print(f"{NAMES[a]:22}: {frames:5} frames, {work / max(frames, 1):5.1f} ms a frame, slowest {worst:3} ms, "
                  f"{over:3} over 33 ms, up to {o[4]} bones and {o[5]} blasters" + ("" if done else ", NOT done"))
            bad += over > 0 or not done
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
