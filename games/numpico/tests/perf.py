#!/usr/bin/env python3
"""Carts on the calculator's CPU (tools/emu.py, an N0110's speed and the RAM of custom 25.2 builds, 133652
bytes): each cart alone in output/perf.nwa (make output/perf.nwa PERF_CART=...), played for a while with
keys held now and then. Prints the pictures shown and the frames run a second, Lua's memory at its most
against what it has, and how the cart ended.

usage: perf.py [--ms N] [--shots DIR] CART|DIR..."""
import argparse
import os
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
GAME = os.path.join(HERE, "..")
sys.path.insert(0, os.path.join(GAME, "..", "..", "tools"))
import emu  # noqa: E402

# OK to start (the first cart starts at once), then a few presses of every button
KEYS = "3000-3200:ok,4000-4600:right,5000-5100:ok,6000-6600:left,7000-7100:back,8000-8400:up,9000-9200:exe," \
       "10000-10800:right,11000-11100:ok,12000-12300:down,13000-13100:back"


def carts(args):
    for a in args:
        if os.path.isdir(a):
            yield from sorted(os.path.join(a, f) for f in os.listdir(a) if f.lower().endswith((".p8", ".p8.png")))
        else:
            yield a


def word(c, name):
    return struct.unpack("<I", bytes(c.uc.mem_read(c.symbols[name], 4)))[0] if name in c.symbols else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ms", type=int, default=15000)
    ap.add_argument("--shots", help="a picture of each cart at the end, here")
    ap.add_argument("--ram", default="133652", help="the RAM apps get (several: each in turn)")
    ap.add_argument("carts", nargs="+")
    a = ap.parse_args()
    nwlink = os.environ.get("NWLINK", "nwlink")
    for ram, cart in ((int(r), c) for c in carts(a.carts) for r in a.ram.split(",")):
        emu.configure("n0110", "epsilon", ram)
        name = os.path.basename(cart)
        r = subprocess.run(["make", "-s", "-C", GAME, "output/perf.nwa", f"PERF_CART={os.path.abspath(cart)}",
                            f"NWLINK={nwlink}"], capture_output=True, text=True)
        if r.returncode:
            print(f"{name}: no build: {(r.stdout + r.stderr).strip()[-300:]}")
            continue
        c = emu.Calculator(os.path.join(GAME, "output", "perf.nwa"), None, None, nwlink)
        c.keys = emu.parse_keys(KEYS)
        try:
            c.run(a.ms)
        except RuntimeError as e:
            print(f"{name}: CRASH {e}")
            continue
        secs = max(c.now_ms, 1) / 1000
        frames, updates = word(c, "perf_frames"), word(c, "perf_updates")
        heap, size = word(c, "perf_heap"), word(c, "perf_heap_size")
        end = ["playing", "quit", "home", "error", "OUT OF MEMORY", "load"][min(word(c, "perf_end"), 5)]
        print(f"{name:28s} {frames / secs:5.1f} pictures/s {updates / secs:5.1f} frames/s  Lua {heap // 1024:3d} KB "
              f"of {size // 1024} KB  {end}  (API {word(c, 'perf_base') // 1024} KB, code {word(c, 'perf_load_used') // 1024} KB, "
              f"{word(c, 'perf_load_peak') // 1024} KB while read)", flush=True)
        # the stack: the deepest word written (it starts as zeros)
        st = bytes(c.uc.mem_read(emu.EXT_RAM_END, emu.STACK_TOP - emu.EXT_RAM_END))
        deep = next((i for i in range(0, len(st), 4) if st[i:i + 4] != bytes(4)), len(st))
        print(f"  stack {(len(st) - deep) // 1024} KB of {len(st) // 1024} KB")
        if word(c, "perf_fail_size"):
            print(f"  last failed: {word(c, 'perf_fail_size')} bytes with {word(c, 'perf_fail_used')} used, "
                  f"the largest free block {word(c, 'perf_fail_largest')}")
            if os.environ.get("MAP"):
                m = struct.unpack("<128I", bytes(c.uc.mem_read(c.symbols["perf_fail_map"], 512)))
                print("  free:", " ".join(f"{m[i] & 0xfffff:05x}+{m[i + 1]}" for i in range(0, 128, 2) if m[i]))
        if a.shots:
            os.makedirs(a.shots, exist_ok=True)
            c.save_png(os.path.join(a.shots, name.split(".")[0] + ".png"))


if __name__ == "__main__":
    main()
