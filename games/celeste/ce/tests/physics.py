#!/usr/bin/env python3
"""Madeline on the CE against the NumWorks port (whose floats are Celeste's): the same keys in the same rooms, and
every frame's position compared.

    python3 tests/physics.py CE_HOST NUMWORKS_PLAY [--runs N] [--frames N] [--seed S] [-v]

CE_HOST is ce/build/host/celeste, NUMWORKS_PLAY games/celeste/build/play (make host in each). Random key runs,
held for a few frames each, in rooms of Forsaken City; a run stops being compared when Madeline dies or leaves the
room (the CE version's rooms then differ). Prints how many frames matched."""
import os
import random
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
CE_DIR = os.path.join(HERE, "..")
NW_DATA = os.path.join(HERE, "..", "..", "src", "data.bin")
# (room, x, y): Madeline's start in rooms with walls, ledges and room to move (no moving blocks)
ROOMS = ["1", "2", "3", "4", "3b", "5", "6", "6a", "6b", "6c", "7", "8", "8b", "9", "9b", "10a", "11", "12", "12a"]
KEYS = ["", "r", "l", "rj", "lj", "j", "x", "rx", "lx", "ux", "urx", "ulx", "dx", "drx", "rg", "lg", "rgu", "lgu", "g",
        "gu", "gd", "d", "rd", "rjx"]


def ce_keys(seq):
    out = []
    for k, n in seq:
        c = "".join({"r": "R", "l": "L", "u": "U", "d": "D", "j": "J", "x": "X", "g": "G"}[ch] for ch in k) or "_"
        out.append("%s*%d" % (c, n))
    return " ".join(out)


def nw_keys(seq):
    out, f = [], 0
    for k, n in seq:
        if k:
            out.append("%d-%d:%s" % (f, f + n - 1, k))
        f += n
    return ",".join(out)


def run_ce(host, room, seq, frames):
    env = dict(os.environ, CEL_START="1,%s" % room, CEL_TRACE="1", CEL_KEYS=ce_keys(seq),
               CEL_FRAMES=str(frames + 2), CEL_DATA=os.path.join(CE_DIR, "data"), CEL_OUT="/tmp")
    out = subprocess.run([host], env=env, capture_output=True, text=True).stdout.split("\n")
    pos = []
    for line in out:
        p = line.split()
        if len(p) > 10 and p[1] == "st":
            pos.append((int(p[6]), int(p[8]), int(p[2]), p[-1], int(p[-3])))
    return pos[1:]   # (line n: before update n = after n - 1)


def run_nw(play, room, seq, frames):
    env = dict(os.environ, TRACE="1")
    out = subprocess.run([play, NW_DATA, "--chapter", "1", "--room", room, "--frames",
                          str(frames), "--keys", nw_keys(seq), "--nowipe"], env=env, capture_output=True, text=True).stdout
    pos = []
    for line in out.split("\n"):
        if line.startswith("f") and " pos " in line:
            p = line.split()
            a, b = p[2].split(",")
            pos.append((int(a), int(b), int(p[6])))
    return pos


def main():
    ce, nw = sys.argv[1], sys.argv[2]
    runs = int(sys.argv[sys.argv.index("--runs") + 1]) if "--runs" in sys.argv else 40
    frames = int(sys.argv[sys.argv.index("--frames") + 1]) if "--frames" in sys.argv else 240
    seed = int(sys.argv[sys.argv.index("--seed") + 1]) if "--seed" in sys.argv else 1
    verbose = "-v" in sys.argv
    rnd = random.Random(seed)
    total = same = bad = 0
    for r in range(runs):
        room = ROOMS[r % len(ROOMS)]
        seq, n = [], 0
        while n < frames:
            k, d = rnd.choice(KEYS), rnd.choice([1, 2, 3, 5, 8, 12, 20, 30])
            seq.append((k, d))
            n += d
        a = run_ce(ce, room, seq, frames)
        b = run_nw(nw, room, seq, frames)
        m = 0
        for i in range(min(len(a), len(b))):
            if a[i][3] != room or a[i][4] or a[i][2] not in (0, 1, 2):
                break
            if a[i][:2] != b[i][:2]:
                bad += 1
                if verbose:
                    print(f"room {room} run {r}: frame {i} CE {a[i][:3]} NumWorks {b[i]} (keys {nw_keys(seq)})")
                break
            m += 1
        total += min(len(a), len(b))
        same += m
    print(f"{runs} runs: {same} frames the same, {bad} runs differ")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
