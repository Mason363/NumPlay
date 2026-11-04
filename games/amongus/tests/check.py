#!/usr/bin/env python3
"""Among Us played on a computer (build/play, with the sanitizers): the rules, every minigame solved with the keys,
whole games to a crew win and an impostor win, saves that come back and survive a broken file, random keys.

    python3 tests/check.py build/play [--fuzz N]"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

PLAY = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else "build/play")
FUZZ = int(sys.argv[sys.argv.index("--fuzz") + 1]) if "--fuzz" in sys.argv else 300
WORK = tempfile.mkdtemp(prefix="amongus-check-")
failed = []


def play(*args, saves=None, fresh=True):
    saves = saves or os.path.join(WORK, "saves")
    if fresh:
        shutil.rmtree(saves, ignore_errors=True)
    os.makedirs(saves, exist_ok=True)
    out = os.path.join(WORK, "shots")
    os.makedirs(out, exist_ok=True)
    r = subprocess.run([PLAY, "--saves", saves, "--out", out] + [str(a) for a in args], capture_output=True, text=True)
    if r.returncode not in (0, 1) or "ERROR" in r.stderr or "runtime error" in r.stderr:
        failed.append("crashed: %s\n%s" % (" ".join(map(str, args)), r.stderr[-2000:]))
    return r.stdout + r.stderr


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failed.append(what)


def end(out):
    m = re.search(r"END screen (\d+) winner (\d+) reason (\d+) t ([\d.]+)", out)
    return tuple(int(m.group(k)) for k in (1, 2, 3)) if m else None


# the rules
out = play("--rules")
m = re.search(r"RULES (\d+) checks, (\d+) failed", out)
check(m and m.group(2) == "0", "rules: %s" % (m.group(0) if m else out[-500:]))
for line in out.splitlines():
    if line.startswith("FAIL"):
        print("     " + line)

# every minigame, solved with the keys (24 is the cameras: nothing to solve)
NAMES = ["swipe card", "wires", "asteroids", "align engine", "scan", "sample", "fuel", "start reactor", "garbage",
         "upload", "calibrate", "chart course", "O2 filter", "manifolds", "steering", "shields", "divert power",
         "accept power", "reactor hands", "lights", "comms", "O2 keypad", "cameras", "emergency button"]
for k, name in enumerate(NAMES):
    if name == "cameras":
        continue
    out = play("--mg", k, "--solve", "--frames", 3000, "--seed", 5 + k)
    m = re.search(r"SOLVED mg \d+ at frame (\d+) step (\d+) sab (\d+)", out)
    if name in ("reactor hands", "lights", "comms"):
        ok = m and m.group(3) == "0"
    elif name == "O2 keypad":
        ok = m is not None
    elif name == "emergency button":
        ok = m is not None and "screen 4" in out
    else:
        ok = m and int(m.group(2)) >= 1
    check(bool(ok), "minigame %s%s" % (name, " (frame %s)" % m.group(1) if m else ""))

# whole games: through the menus to a crew win, an impostor's game to an impostor win
# (the first Play shows How to Play's three pages, then the settings with Start chosen; the first seed that the crew
# wins, and the first an impostor player wins)
for seed in range(1, 21):
    out = play("--auto", "--keys", "10:ok,20:ok,30:ok,40:ok,50:ok", "--frames", 90000, "--seed", seed, "--walled")
    if end(out) and end(out)[:2] == (7, 0):
        break
check(end(out) and end(out)[:2] == (7, 0) and "WALLED" not in out,
      "a crew game from the title to a crew win (seed %d): %s" % (seed, end(out)))
for seed in range(1, 21):
    out = play("--start", "imp", "--auto", "--frames", 90000, "--seed", seed, "--walled")
    if end(out) and end(out)[:2] == (7, 1):
        break
check(end(out) and end(out)[:2] == (7, 1) and "WALLED" not in out,
      "an impostor's game to an impostor win (seed %d): %s" % (seed, end(out)))
wins = {0: 0, 1: 0}
for s in range(1, 9):
    out = play("--start", "crew" if s & 1 else "imp", "--auto", "--frames", 90000, "--seed", 100 + s, "--walled",
               *(["--bots", 9, "--imps", 2] if s % 4 == 0 else []))
    e = end(out)
    check(e is not None and e[0] == 7 and "WALLED" not in out, "game %d ends: %s" % (s, e))
    if e:
        wins[e[1]] += 1
print("     wins: crew %d, impostors %d" % (wins[0], wins[1]))

# saves: Home in the middle of a round, then back where it was, paused
saves = os.path.join(WORK, "s1")
out = play("--start", "crew", "--frames", 400, "--keys", "100-200:left,250-300:down,399:home", "--seed", 7, saves=saves)
check(os.path.exists(os.path.join(saves, "amongus.sav")) and os.path.exists(os.path.join(saves, "amongus_saves.py")),
      "Home saves the game and its copy")
out = play("--frames", 5, saves=saves, fresh=False)
m = re.search(r"LOADED screen (\d+) back (\d+) t ([\d.]+)", out)
check(m and m.group(1) == "9" and m.group(2) == "3" and float(m.group(3)) > 10, "the game comes back, paused: %s" %
      (m.group(0) if m else None))
# Home from How to Play in the pause menu saves the round too
saves3 = os.path.join(WORK, "s3")
play("--start", "crew", "--frames", 200, "--keys", "100:back,110:down,120:ok,130:right,140:home", "--seed", 7, saves=saves3)
out3 = play("--frames", 5, saves=saves3, fresh=False)
check("LOADED screen 9 back 3" in out3, "Home from How to Play in a round: back paused")
# its copy alone (installing apps from the website keeps only scripts)
for f in ("amongus.sav", "amongus.set"):
    os.remove(os.path.join(saves, f))
out = play("--frames", 5, saves=saves, fresh=False)
m2 = re.search(r"LOADED screen (\d+) back (\d+) t ([\d.]+)", out)
check(m2 and m2.group(1) == "9" and m2.group(3) == m.group(3), "and from the Python copy alone")
# a broken file: a clean start, no crash
data = open(os.path.join(saves, "amongus_saves.py"), "rb").read()
rnd = random.Random(5)
for k in range(40):
    case = os.path.join(WORK, "broken%d" % k)
    shutil.rmtree(case, ignore_errors=True)
    os.makedirs(case)
    if k < 20:
        b = bytearray(data)
        for _ in range(1 + k):
            b[rnd.randrange(len(b))] = rnd.randrange(256)
        open(os.path.join(case, "amongus_saves.py"), "wb").write(bytes(b[:rnd.randrange(len(b) + 1)] if k & 1 else b))
    else:
        open(os.path.join(case, "amongus.sav"), "wb").write(bytes(rnd.randrange(256) for _ in range(rnd.randrange(4000))))
        open(os.path.join(case, "amongus.set"), "wb").write(bytes(rnd.randrange(256) for _ in range(rnd.randrange(64))))
    out = play("--frames", 60, "--keys", "10:ok,20:ok,40:ok", saves=case, fresh=False)
    if "LOADED" not in out:
        check(False, "broken save %d" % k)
        break
else:
    check(True, "40 broken saves: no crash")
# the settings kept
saves = os.path.join(WORK, "s2")
play("--frames", 60, "--keys", "10:ok,20:back,30:down,35:right,40:back", saves=saves)
out = play("--frames", 5, saves=saves, fresh=False)
check("bots 8" in out, "a setting kept: %s" % re.search(r"bots \d+", out).group(0))

# random keys, the app started again when it quits
crashes = 0
for k in range(FUZZ):
    args = ["--fuzz", k, "--frames", 2500, "--seed", k]
    if k % 3 == 1:
        args += ["--start", "crew"]
    elif k % 3 == 2:
        args += ["--start", "imp"]
    out = play(*args)
    if "END screen" not in out:
        crashes += 1
        failed.append("fuzz %d" % k)
check(crashes == 0, "%d random-key runs" % FUZZ)

shutil.rmtree(WORK, ignore_errors=True)
print("\n%d failed" % len(failed) if failed else "\nall passed")
for f in failed:
    print(" - " + f[:3000])
sys.exit(1 if failed else 0)
