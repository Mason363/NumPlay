#!/usr/bin/env python3
"""Checks NumPlay on calculator software that gives apps less RAM (tools/emu.py --ram-length). NumPlay installs
with less, and its games use the RAM after it up to the end of what there is (tools/gen_games.py):

1. custom builds of Epsilon 25.2 give apps 133652 bytes: NumBlocks, the hungriest game, starts there;
2. with 120000 bytes, NumBlocks doesn't fit: OK on its card doesn't start it (the card says how much it lacks),
   while NumDash, which fits, starts and makes no access outside the app's RAM.

Usage: test_small_ram.py build/NumPlay.nwa [--out DIR]
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu  # noqa: E402
from test_progress import presses, write_storage  # noqa: E402


def changed(a, b):
    """the share of pixels that differ between two screenshots"""
    from PIL import Image, ImageChops
    d = ImageChops.difference(Image.open(a).convert("RGB"), Image.open(b).convert("RGB")).convert("L")
    return sum(d.histogram()[25:]) / (d.width * d.height)


def run(nwa, out, ram, name, keys, shots, ms):
    emu.configure("n0120", "epsilon", ram)
    st = os.path.join(out, f"{name}.bin")
    write_storage(st, [("pi.py", b"\x01print(3.14159)\n\x00")])
    c = emu.Calculator(nwa, storage_file=st)
    c.enable_checks(reads=True)
    c.keys = presses(*keys)
    paths = [os.path.join(out, f"{name}_{t}.png") for t in shots]
    c.pending_shots = list(zip(shots, paths))
    c.run(ms)
    return c, paths


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nwa")
    ap.add_argument("--out", default="build/test_small_ram")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    problems = []
    # NumBlocks, third on the home screen: Right twice, then OK
    blocks = ((2500, "right"), (2800, "right"), (4000, "ok"))
    c, (before, after) = run(a.nwa, a.out, 133652, "numblocks_133652", blocks, (3900, 6800), 7000)
    problems += sorted(set(c.violations))
    if changed(before, after) < 0.3:
        problems.append("NumBlocks didn't start with 133652 bytes of RAM")
    c, (before, after) = run(a.nwa, a.out, 120000, "numblocks_120000", blocks, (3900, 6800), 7000)
    problems += sorted(set(c.violations))
    if changed(before, after) > 0.02:
        problems.append("NumBlocks started with 120000 bytes of RAM, too little")
    # NumDash, the first: OK
    c, (home, game) = run(a.nwa, a.out, 120000, "numdash_120000", ((2600, "ok"), (6000, "ok")), (2500, 8800), 9000)
    problems += sorted(set(c.violations))
    if changed(home, game) < 0.3:
        problems.append("NumDash didn't start with 120000 bytes of RAM")
    for p in problems:
        print("   ", p)
    print("PASS" if not problems else "FAIL")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
