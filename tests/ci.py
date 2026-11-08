#!/usr/bin/env python3
"""CI's tests in N parts that run side by side (.github/workflows/build.yml): part K of N plays every Nth app in
the emulator (tests/test_games.py --shard) and runs its share of the other tests, shared out by how long each took
on CI (WEIGHT, in seconds), longest first, each to the part with the least so far. Runs every test of its part,
then prints their times and fails if any failed.

Usage: tests/ci.py --shard K --of N   (after tools/build.py, from the repository's root)"""
import argparse
import subprocess
import sys
import time

APPS = "build/compiled/games"
NUMPLAY = "build/compiled/NumPlay.nwa"
OTHER = [
    f"python3 tests/test_uninstall.py {NUMPLAY} --game numdrive",
    f"python3 tests/test_progress.py {NUMPLAY}",
    "python3 tests/test_numblocks_progress.py build/compiled/games/NumBlocks.nwa",
    "python3 tests/test_upsilon.py build/compiled/NumPlay-Upsilon.nwa",
    f"python3 tests/test_small_ram.py {NUMPLAY}",
    "python3 tests/test_apps.py build/data",
]
HOST = ["numblocks", "numdrive", "championisland", "undertale", "doom", "isaac", "pvz", "fnaf", "sonic", "subway",
        "hillclimb", "leveldevil", "cookieclicker", "jetpackjoyride", "donkeykong", "dune", "monkeymart",
        "tombofthemask", "invaders", "sudoku", "uttt", "sokoban", "twoplayer", "flappydunk", "towerblock", "suika",
        "doodlejump", "stickmanhook", "coreball", "uno", "fnf", "angrybirds", "polytrack", "paperclips", "badtime",
        "mahjong", "rider", "snowrider", "templerun", "fnaf2", "famidash", "catninja", "motox3m", "amongus", "imposter",
        "fourplayer", "circloo", "kingdomrush", "crossyroad"]
HOST_ARGS = {"fnf": " BUILD_DIR=../../build/data/games/fnf"}
# seconds on CI (v1.24.3's run); the emulator's 71 apps took 2450 in all, the Python tests about 380
WEIGHT = {"numblocks": 89, "numdrive": 20, "championisland": 69, "undertale": 445, "doom": 54, "isaac": 380,
          "pvz": 214, "fnaf": 215, "sonic": 53, "subway": 46, "hillclimb": 94, "leveldevil": 3, "cookieclicker": 61,
          "jetpackjoyride": 134, "donkeykong": 291, "dune": 135, "monkeymart": 85, "tombofthemask": 119,
          "invaders": 7, "sudoku": 3, "uttt": 2, "sokoban": 2, "twoplayer": 114, "flappydunk": 90, "towerblock": 2,
          "suika": 112, "doodlejump": 5, "stickmanhook": 103, "coreball": 26, "uno": 36, "fnf": 80, "angrybirds": 627,
          "polytrack": 116, "paperclips": 164, "badtime": 59, "mahjong": 25, "rider": 137, "snowrider": 30,
          "templerun": 384, "fnaf2": 654, "famidash": 7, "catninja": 60, "motox3m": 10, "amongus": 110, "imposter": 20,
          "fourplayer": 40, "circloo": 15, "kingdomrush": 30, "crossyroad": 2}
EMULATOR = 2450
PYTHON = 380


def parts(n):
    """the commands of each of n parts"""
    load = [EMULATOR / n] * n
    out = [[f"python3 tests/test_games.py {APPS} --shard {k + 1}/{n}"] for k in range(n)]
    jobs = [(PYTHON / len(OTHER), c) for c in OTHER]
    jobs += [(WEIGHT.get(g, 60), f"make -C games/{g} host test{HOST_ARGS.get(g, '')}") for g in HOST]
    for w, c in sorted(jobs, key=lambda j: -j[0]):
        k = load.index(min(load))
        load[k] += w
        out[k].append(c)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--shard", type=int, required=True)
    ap.add_argument("--of", type=int, required=True)
    a = ap.parse_args()
    times, failed = [], []
    for c in parts(a.of)[a.shard - 1]:
        print(f"::group::{c}", flush=True)
        t = time.time()
        ok = subprocess.run(c, shell=True).returncode == 0
        times.append((time.time() - t, c))
        print("::endgroup::", flush=True)
        if not ok:
            failed.append(c)
            print(f"::error::{c} failed", flush=True)
    for t, c in sorted(times, reverse=True):
        print(f"{t:7.0f} s  {c}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
