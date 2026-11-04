#!/usr/bin/env python3
"""Moto X3M's checks on a computer (make test): every level loads and runs (the sanitizers on), a level is
finished and its stars and time are kept, a saved game comes back (from its copy too), and random keys never crash it.

    python3 tests/check.py build/play"""
import os
import re
import shutil
import subprocess
import sys

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
TMP = "build/check"
failed = []


def run(args, saves):
    os.makedirs(saves, exist_ok=True)
    p = subprocess.run([PLAY, "--saves", saves] + args, capture_output=True, text=True, timeout=600)
    out = p.stdout + p.stderr
    if p.returncode != 0 or "runtime error" in out or "Sanitizer" in out:
        failed.append(" ".join(args) + "\n" + out[-2000:])
        return None
    m = re.search(r"END state (\d+) level (\d+) timer ([-\d.]+) dead (\d) finished (\d) x ([-\d.]+) y ([-\d.]+) "
                  r"arena (\d+) overflow (\d)", out)
    return m


def check(cond, what):
    if not cond:
        failed.append(what)


shutil.rmtree(TMP, ignore_errors=True)
# every level: built, played 15 s with the gas held, nothing out of room, nothing through the floor
for n in range(1, 51):
    m = run(["--level", str(n), "--gas", "--frames", "450"], "%s/l%d" % (TMP, n))
    if m:
        check(int(m.group(9)) == 0, "level %d: contacts ran out" % n)
        check(float(m.group(7)) < 5000, "level %d: fell through the world (y %s)" % (n, m.group(7)))
        check(int(m.group(1)) in (3, 5), "level %d: did not start" % n)
# a finished level: stars and best time kept, the next level open (level 3, by the test driver)
m = run(["--level", "3", "--auto", "--frames", "1500"], TMP + "/fin")
check(m is not None and m.group(5) == "1", "level 3 not finished by the test driver")
save = os.path.join(TMP, "fin", "motox3m.sav")
check(os.path.exists(save), "no save after a finish")
if os.path.exists(save):
    d = open(save, "rb").read()
    check(d[:4] == b"MX3S", "the save's magic")
    stars = d[8:58]
    check(stars[0] == 0 and 1 <= stars[2] <= 3 and stars[3] == 0 and stars[4] == 0xFF,
          "stars kept: level 1 open, 3 starred, 4 opened, 5 locked (%s)" % list(stars[:6]))
# after a reinstall only the copy in motox3m_saves.py is left: the save comes back from it
if os.path.exists(save):
    os.makedirs(TMP + "/copy", exist_ok=True)
    shutil.copy(os.path.join(TMP, "fin", "motox3m_saves.py"), TMP + "/copy")
    run(["--frames", "20", "--keys", "10:home"], TMP + "/copy")
    back = os.path.join(TMP, "copy", "motox3m.sav")
    check(os.path.exists(back) and open(back, "rb").read() == d, "the save does not come back from its copy")
# a broken save resets cleanly
os.makedirs(TMP + "/bad", exist_ok=True)
open(TMP + "/bad/motox3m.sav", "wb").write(b"MX3S\x01\x00\xff\xff" + bytes(range(256)) * 2)
m = run(["--frames", "120", "--keys", "10:ok,30:ok,40-110:up"], TMP + "/bad")
check(m is not None and m.group(1) == "3", "a broken save stops the game")
# random keys through menus and runs
for seed in range(1, 41):
    run(["--fuzz", str(seed), "--frames", "6000"], "%s/fuzz%d" % (TMP, seed))

if failed:
    print("FAILED:\n" + "\n".join(failed))
    sys.exit(1)
print("all checks passed")
