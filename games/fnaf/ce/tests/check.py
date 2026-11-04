#!/usr/bin/env python3
"""Checks of the CE version of Five Nights at Freddy's on a computer (make -f host.mk test): every picture decoded
as tools/pack.py made it, the menus, whole nights played by tests/play.c's own player, each animatronic's attack,
the power going out, Golden Freddy, the custom night, pausing, Home and the save, and random keys.

    python3 tests/check.py build/play build/pics [--quick]"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
PICTURES = sys.argv[2] if len(sys.argv) > 2 else "build/pics"
QUICK = "--quick" in sys.argv
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
failures = []
SC_TITLE, SC_NIGHT, SC_SCARE, SC_SIX, SC_PAPER, SC_OVER, SC_CUSTOM = 0, 4, 5, 6, 7, 8, 9
SCARE_BONNIE, SCARE_CHICA, SCARE_FREDDY, SCARE_FOXY, SCARE_DARK = range(5)
IN_OFFICE = 12
NIGHT = 60 * 535 + 200   # a night's steps, its card and a bit


def run(args, frames, saves=None, what=""):
    """Plays; returns the end state (a dict) and the events (6 AM, jumpscares) on the way"""
    own = saves is None
    if own:
        saves = tempfile.mkdtemp(prefix="fnafce_check_")
    shots = tempfile.mkdtemp(prefix="fnafce_shots_")
    p = subprocess.run([PLAY, "--saves", saves, "--out", shots, "--frames", str(frames)] + args, capture_output=True,
                       text=True, env=ENV, timeout=1800)
    shutil.rmtree(shots, ignore_errors=True)
    if own:
        shutil.rmtree(saves, ignore_errors=True)
    if p.returncode or "runtime error" in p.stderr or "AddressSanitizer" in p.stderr:
        failures.append(f"{what or args}: crashed or a sanitizer error\n{p.stderr[-2000:]}")
        return None, []
    m = re.search(r"END (.*)", p.stdout)
    if not m:
        failures.append(f"{what}: no end state\n{p.stdout[-500:]}")
        return None, []
    words = m.group(1).split()
    s = {}
    for k, v in zip(words[::2], words[1::2]):
        s[k] = [int(x) for x in v.split(",")] if "," in v else float(v) if "." in v else int(v)
    events = [dict(zip(e.split()[1::2], e.split()[2::2]), kind=e.split()[0])
              for e in re.findall(r"EVENT (.*)", p.stdout)]
    return s, events


def check(cond, msg):
    if not cond:
        failures.append(msg)


def save_file(saves):
    p = os.path.join(saves, "NPfnaf.8xv.bin")
    return open(p, "rb").read() if os.path.exists(p) else None


# every picture decodes as it was made
p = subprocess.run([PICTURES], capture_output=True, text=True, env=ENV, timeout=600)
check(p.returncode == 0 and "runtime error" not in p.stderr, f"pictures: {p.stdout[-300:]} {p.stderr[-1500:]}")

# the title, New Game, the newspaper (skipped), how to play, the first night's card, then the office
saves = tempfile.mkdtemp(prefix="fnafce_check_")
s, _ = run(["--keys", "60:ok,200:ok,700:ok"], 1400, saves, "a new game")
if s:
    check(s["scene"] == SC_NIGHT and s["night"] == 1 and s["keys_seen"] == 1, f"a new game: {s}")
    check(save_file(saves) is not None, "a new game: no save")
# clear pauses; Quit night asks, and Yes goes back to the title
s, _ = run(["--night", "1", "--keys", "400:back,430:down,450:down,470:ok,500:right,520:ok"], 600, saves, "quitting")
if s:
    check(s["scene"] == SC_TITLE, f"quitting a night: {s}")
shutil.rmtree(saves, ignore_errors=True)

# whole nights, played by the test's player: 6 AM, then the next night's card
for n in ([1] if QUICK else [1, 2, 3]):
    saves = tempfile.mkdtemp(prefix="fnafce_check_")
    s, ev = run(["--night", str(n), "--bot"], NIGHT + 800, saves, f"night {n}")
    if s:
        six = [e for e in ev if e["kind"] == "six"]
        check(six and int(six[0]["night"]) == n, f"night {n} not survived: {ev}")
        check(s["reached"] == n + 1 and s["night"] == n + 1, f"night {n}: the next night not reached: {s}")
    shutil.rmtree(saves, ignore_errors=True)

# the fifth night's 6 AM: a star and the paycheck, then the title
s, ev = run(["--night", "5", "--hour", "5", "--bot"], 89 * 60 + 2200, None, "night 5's end")
if s:
    check(any(e["kind"] == "six" for e in ev) and s["stars"] & 1 and s["scene"] == SC_TITLE,
          f"night 5's end: {s} {ev}")

# the doors' and lights' power: two doors and a light drain four points a second
s, _ = run(["--night", "1", "--keys", "200:7,300:9,400-430:left,440:1"], 140 + 60 * 20, None, "power")
if s:
    check(s["doors"] == [1, 1] and 900 < s["power"] < 960, f"doors and lights: {s}")

# the power goes out: Freddy's song, the dark, his jumpscare, the game over, the title
s, ev = run(["--night", "1", "--power", "3"], 60 * 75, None, "power out")
if s:
    check(any(e["kind"] == "scare" and int(e["who"]) == SCARE_DARK for e in ev) and s["scene"] == SC_TITLE,
          f"power out: {s} {ev}")

# Bonnie and Chica come in while the monitor is up; it comes down by itself, and they get you
s, ev = run(["--night", "1", "--ai", "0,20,20,0", "--keys", "300:8"], 60 * 80, None, "Bonnie and Chica")
if s:
    check(any(e["kind"] == "scare" and int(e["who"]) in (SCARE_BONNIE, SCARE_CHICA) for e in ev),
          f"Bonnie and Chica: {s} {ev}")
s, ev = run(["--night", "1", "--ai", "0,20,20,0", "--keys", "200:7,300:9"], 60 * 60, None, "doors closed")
if s:
    check(not ev and s["pos"][1] != IN_OFFICE and s["pos"][2] != IN_OFFICE, f"doors closed: {s} {ev}")

# Foxy: out of the cove with the cameras down, down the hall, in through the open door; or banging on it
s, ev = run(["--night", "1", "--ai", "0,0,0,20"], 60 * 60, None, "Foxy")
if s:
    check(any(e["kind"] == "scare" and int(e["who"]) == SCARE_FOXY for e in ev), f"Foxy: {s} {ev}")
s, ev = run(["--night", "1", "--ai", "0,0,0,20", "--keys", "200:7"], 60 * 60, None, "Foxy at the door")
if s:
    check(not ev and s["pounds"] >= 1, f"Foxy at the closed door: {s} {ev}")

# Freddy: in while the monitor looks elsewhere, then he gets you once it is down
s, ev = run(["--night", "1", "--ai", "20,0,0,0", "--pos", "0,5,7", "--keys", "3000:8,3300:8"], 60 * 80, None, "Freddy")
if s:
    check(any(e["kind"] == "scare" and int(e["who"]) == SCARE_FREDDY for e in ev), f"Freddy: {s} {ev}")

# Golden Freddy: his poster on 2B, then in the office, then the game is gone
s, ev = run(["--night", "1", "--gold", "--keys", "300:8,360:2,400:2,480:8"], 1400, None, "Golden Freddy")
if s:
    check(any(e["kind"] == "golden" for e in ev) and s["running"] == 0, f"Golden Freddy: {s} {ev}")

# the custom night: 20/20/20/20 starts the night; 1/9/8/7 is Golden Freddy's
s, ev = run(["--stars", "3", "--keys", "100:down,120:down,140:down,160:ok,200-560:up,600:right,620-980:up,"
             "1000:right,1020-1380:up,1400:right,1420-1780:up,1820:ok"], 2200, None, "custom night 20")
if s:
    check(s["scene"] == SC_NIGHT and s["night"] == 7 and s["ai"] == [20, 20, 20, 20], f"custom night 20/20/20/20: {s}")
s, ev = run(["--stars", "3", "--keys", "100:down,120:down,140:down,160:ok,200:up,240:right,"
             + ",".join(f"{260 + 12 * i}:up" for i in range(9)) + ",400:right,"
             + ",".join(f"{420 + 12 * i}:up" for i in range(8)) + ",560:right,"
             + ",".join(f"{580 + 12 * i}:up" for i in range(7)) + ",700:ok"], 1000, None, "custom night 1987")
if s:
    check(s["running"] == 0 and any(e["kind"] == "golden" for e in ev), f"custom night 1/9/8/7: {s} {ev}")

# mode keeps the night: the next start is back in it, paused, at the same moment, once
saves = tempfile.mkdtemp(prefix="fnafce_check_")
s, _ = run(["--night", "2", "--keys", "200:7,1500:home"], 1600, saves, "mode")
if s:
    check(s["running"] == 0, f"mode: {s}")
    s2, _ = run([], 30, saves, "back after mode")
    if s2:
        check(s2["scene"] == SC_NIGHT and s2["paused"] == 1 and s2["night"] == 2 and abs(s2["t"] - s["t"]) < 0.5
              and s2["doors"][0] == 1, f"back after mode: {s2} (left at {s})")
    s3, _ = run([], 30, saves, "the next start")
    if s3:
        check(s3["scene"] == SC_TITLE, f"the kept night is read once: {s3}")
shutil.rmtree(saves, ignore_errors=True)

# 0 mutes the call
s, _ = run(["--night", "1", "--keys", "600:0"], 800, None, "mute")
if s:
    check(s["muted"] == 1, f"mute: {s}")

# random keys: no crash
rng = random.Random(7)
names = ["left", "right", "up", "down", "2nd", "enter", "clear", "del"] + [str(d) for d in range(10)]
for k in range(4 if QUICK else 12):
    keys = ",".join(f"{t}:{rng.choice(names)}" for t in sorted(rng.sample(range(10, 3000), 160)))
    run(["--night", str(rng.randint(1, 7)), "--ai", "10,10,10,10", "--keys", keys], 3000, None, f"random keys {k}")

if failures:
    print(f"{len(failures)} failures:")
    for f in failures:
        print("-", f)
    sys.exit(1)
print("all checks passed")
