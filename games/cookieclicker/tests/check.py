#!/usr/bin/env python3
"""Checks of Cookie Clicker on a computer (make test): the host build, with AddressSanitizer and UBSan,
through the first start, the bakery kept by Home and brought back, the save coming back from
cookieclicker_saves.py, every screen pressed through (the store, the menus, statistics, the buildings' rows,
the Grimoire, golden and wrath cookies, wrinklers, Santa and the dragon), an ascension from the Legacy
prompt to the heavens and back, and half an hour of play by a robot (tests/play.c --bot).

    python3 tests/check.py build/play [--quick]

--quick plays five minutes instead of half an hour."""
import os
import re
import shutil
import subprocess
import sys
import tempfile

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
QUICK = "--quick" in sys.argv
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
failures = []


def check(cond, msg):
    if not cond:
        failures.append(msg)


def run(args, what, timeout=1200):
    p = subprocess.run([PLAY] + args, capture_output=True, text=True, env=ENV, timeout=timeout)
    if p.returncode != 0 or "runtime error" in p.stderr or "AddressSanitizer" in p.stderr:
        failures.append(f"{what}: crashed or a sanitizer error\n{p.stderr[-2000:]}")
        return None
    return p.stdout


def end_state(out, what):
    m = re.search(r"frames (\d+) cookies (\d+) earned (\d+) cps ([\d.]+) clicks (\d+) buildings (\d+) upgrades (\d+) "
                  r"achievements (\d+) prestige (\d+) chips (\d+) golden (\d+) ascending (\d+) running (\d+)", out or "")
    if not m:
        failures.append(f"{what}: no end state\n{(out or '')[-500:]}")
        return None
    keys = ("frames", "cookies", "earned", "cps", "clicks", "buildings", "upgrades", "achievements", "prestige",
            "chips", "golden", "ascending", "running")
    return {k: float(v) if k == "cps" else int(v) for k, v in zip(keys, m.groups())}


def play(saves, keys, frames, what, extra=()):
    shots = tempfile.mkdtemp(prefix="cc_shots_")
    out = run(["--saves", saves, "--out", shots, "--frames", str(frames), "--keys", keys, "--quiet"] + list(extra), what)
    shutil.rmtree(shots, ignore_errors=True)
    return end_state(out, what)


def presses(start, key, n, gap=3):
    return ",".join(f"{start + i * gap}:{key}" for i in range(n))


saves = tempfile.mkdtemp(prefix="cc_check_")
try:
    # the first start: the keys, then 20 clicks; a cursor bought (EXE on the store's first building); Home
    s = play(saves, "10:ok," + presses(20, "ok", 20) + ",100:exe,130:home", 200, "first start")
    if s:
        check(s["running"] == 0, "Home did not quit")
        check(s["clicks"] == 20 and s["buildings"] == 1, f"20 clicks and a cursor expected: {s}")
    for f in ("cookieclicker.sav", "cookieclicker_saves.py"):
        check(os.path.exists(os.path.join(saves, f)), f"{f} not written by Home")
    kept = tempfile.mkdtemp(prefix="cc_copy_")
    for f in os.listdir(saves):
        shutil.copy(os.path.join(saves, f), kept)

    # next time: no keys sheet, the bakery as it was; the cursor bakes
    s = play(saves, presses(10, "ok", 5) + ",400:home", 500, "continue")
    if s:
        check(s["clicks"] == 25 and s["buildings"] == 1 and s["earned"] >= 25, f"the bakery did not go on: {s}")

    # installing apps again erases everything but scripts: the save comes back from cookieclicker_saves.py
    for f in os.listdir(kept):
        if not f.endswith(".py"):
            os.remove(os.path.join(kept, f))
    copy = open(os.path.join(kept, "cookieclicker_saves.py"), "rb").read()
    s = play(kept, presses(10, "ok", 2) + ",60:home", 100, "from the copy")
    if s:
        check(s["clicks"] == 22 and s["buildings"] == 1, f"the copy did not bring the bakery back: {s}")
    # a copy written by 1.16.0 and before (another head over the same line) too
    old = tempfile.mkdtemp(prefix="cc_old_")
    line = copy[copy.index(b"#>cookieclicker.sav:"):]
    open(os.path.join(old, "cookieclicker_saves.py"), "wb").write(
        b"\0# Cookie Clicker keeps a copy of your bakery here, so that\n# reinstalling it doesn't erase it. If you "
        b"delete this file,\n# the game writes it again: your bakery stays.\n" + line)
    s = play(old, presses(10, "ok", 2) + ",60:home", 100, "from an old copy")
    if s:
        check(s["clicks"] == 22 and s["buildings"] == 1, f"an old copy did not bring the bakery back: {s}")
    shutil.rmtree(old, ignore_errors=True)

    # every screen: the store (arrows, bulk, sell), the menu and its pages, statistics' tabs, the rows
    keys = ",".join([
        presses(10, "down", 6, 4), "40:exe", "45:mul", "50:exe", "55:mul", "60:div", "65:exe", "70:div", "75:mul",
        presses(80, "up", 8, 4), presses(115, "right", 3, 4), "130:exe",
        "150:back", "160:down", "170:ok", presses(180, "down", 8, 4), "215:ok", "225:ok", "235:back",  # options
        "245:down", "250:ok", presses(260, "right", 4, 5), presses(285, "down", 3, 5), "300:left", "310:back",
        "320:down", "325:ok", "335:down", "340:back",  # info
        "350:back", "360:toolbox", presses(370, "right", 2, 6), presses(385, "down", 4, 4), "405:back",
        "420:alpha", presses(430, "down", 5, 4), "455:exe", "465:down", "470:back", "480:up", "485:exe", "495:ok",
        "510:alpha", "520:del", "525:del", "530:xnt", "540:var", "550:shift"])
    for preset in ("early", "mid", "late"):
        s = play(saves, keys, 600, f"screens ({preset})", ["--preset", preset])
        if s:
            check(s["running"] == 1, f"screens ({preset}): quit")
    # golden and wrath cookies clicked, wrinklers popped
    s = play(saves, "20:shift,40:shift", 60, "golden", ["--preset", "golden"])
    if s:
        check(s["golden"] >= 1, f"the golden cookie was not clicked: {s}")
    # quit while a golden cookie shows: next time they still come (within 15 minutes, the longest wait)
    goldens = tempfile.mkdtemp(prefix="cc_golden_")
    play(goldens, "10:home", 20, "home on a golden cookie", ["--preset", "golden"])
    s = play(goldens, presses(30, "shift", 2700, 10), 27100, "golden cookies after a save with one showing")
    if s:
        check(s["golden"] >= 1, f"no golden cookie came back after the save: {s}")
    shutil.rmtree(goldens, ignore_errors=True)
    play(saves, "20:shift,40:shift", 60, "wrath", ["--preset", "wrath"])
    s = play(saves, presses(20, "var", 30, 2), 200, "wrinklers", ["--preset", "wrinklers"])
    # the Grimoire: every spell cast, the lump refill
    keys = ",".join([f"{20 + 30 * i}:{i + 1}" for i in range(9)] + ["300:down", "310:down", "320:exe", "330:back"])
    play(saves, keys, 400, "grimoire", ["--preset", "grimoire"])
    # an ascension: the Legacy prompt, the cookie bursting, the heavens (a heavenly upgrade bought, the arrows),
    # then reincarnated
    keys = ",".join(["10:back", "20:down", "25:down", "30:down", "40:ok", "50:ok",  # Legacy, Ascend
                     presses(260, "right", 3, 8), "300:exe", presses(310, "up", 3, 8), "340:exe", "350:shift",
                     "360:shift", "380:back", "390:ok"])
    s = play(saves, keys, 480, "ascension", ["--preset", "late"])
    if s:
        check(s["ascending"] == 0 and s["prestige"] > 0 and s["cookies"] < 1e6, f"not ascended and back: {s}")
    s = play(saves, "10:back,20:down,25:down,30:down,40:ok,50:ok,400:home", 500, "quit while ascending",
             ["--preset", "late"])
    s = play(saves, "", 30, "back on the heavens")
    if s:
        check(s["ascending"] == 1, f"quitting while ascending did not come back to the heavens: {s}")

    # a robot plays (half an hour, or five minutes)
    robot = tempfile.mkdtemp(prefix="cc_robot_")
    frames = 9000 if QUICK else 54000
    s = play(robot, "10:ok", frames, "robot", ["--bot"])
    if s:
        # (a computer's maths library rounds pow() its own way, so the robot's choices differ a little from one
        # to another: on a Mac it gets about 135 buildings, on Linux about 150)
        check(s["buildings"] >= (40 if QUICK else 120) and s["cps"] > (20 if QUICK else 1000),
              f"the robot's bakery did not grow: {s}")
    shutil.rmtree(robot, ignore_errors=True)
finally:
    shutil.rmtree(saves, ignore_errors=True)

for f in failures:
    print("FAIL", f)
print("cookieclicker host checks:", "ok" if not failures else f"{len(failures)} failed")
sys.exit(1 if failures else 0)
