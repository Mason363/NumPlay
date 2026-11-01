#!/usr/bin/env python3
"""Checks of Five Nights at Freddy's 2 on a computer (make test): the host build, with AddressSanitizer and
UBSan, through every picture, the menus, whole nights played by tests/play.c's own player, the paychecks, the
custom night, pausing, Home, damaged and lost saves, and a few hundred sessions of random keys.

    python3 tests/check.py build/play build/pictures [--quick]

--quick plays fewer nights and fuzz sessions."""
import concurrent.futures
import os
import re
import shutil
import subprocess
import sys
import tempfile

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
PICTURES = sys.argv[2] if len(sys.argv) > 2 else "build/pictures"
QUICK = "--quick" in sys.argv
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
failures = []
SC_TITLE, SC_NEWS, SC_KEYS, SC_INTRO, SC_NIGHT, SC_SCARE, SC_SIX, SC_PAPER, SC_OVER, SC_CUSTOM = range(10)
TF, TB, TC, MANGLE, BB, WF, WB, WC, FOXY, GF, PUPPET = range(11)
NIGHT = 60 * 6 * 68 + 400   # a night's steps, its card and a bit


def run(args, frames, saves=None, what=""):
    """Plays; returns the end state (a dict) and the events (6 AM, jumpscares) on the way"""
    own = saves is None
    if own:
        saves = tempfile.mkdtemp(prefix="fnaf2_check_")
    shots = tempfile.mkdtemp(prefix="fnaf2_shots_")
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


def record(saves, name):
    p = os.path.join(saves, name)
    return open(p, "rb").read() if os.path.exists(p) else None


def scared_by(ev, who):
    return any(e["kind"] == "scare" and int(e["who"]) == who for e in ev)


# every picture decodes
d = tempfile.mkdtemp(prefix="fnaf2_pics_")
p = subprocess.run([PICTURES, d], capture_output=True, text=True, env=ENV, timeout=600)
check(p.returncode == 0 and "runtime error" not in p.stderr, f"pictures: {p.stderr[-1500:]}")
check(len(os.listdir(d)) >= 138, f"pictures: only {len(os.listdir(d))} drawn")
shutil.rmtree(d, ignore_errors=True)

# the title, New Game, the newspaper (skipped), how to play, the first night's card, then the office
saves = tempfile.mkdtemp(prefix="fnaf2_check_")
s, _ = run(["--keys", "60:ok,200:ok,700:ok"], 1400, saves, "a new game")
if s:
    check(s["scene"] == SC_NIGHT and s["night"] == 1 and s["keys_seen"] == 1, f"a new game: {s}")
    check(record(saves, "fnaf2.sav") is not None and record(saves, "fnaf2_saves.py") is not None,
          "a new game: no save, or no copy of it")
# Back pauses; Quit night asks, and Yes goes back to the title
s, _ = run(["--night", "1", "--keys", "400:back,430:down,450:down,470:ok,500:right,520:ok"], 600, saves, "quitting")
if s:
    check(s["scene"] == SC_TITLE, f"quitting a night: {s}")
# ... No goes back to the pause menu, Back to the night
s, _ = run(["--night", "1", "--keys", "400:back,430:down,450:down,470:ok,520:ok,560:back"], 620, saves, "not quitting")
if s:
    check(s["scene"] == SC_NIGHT and s["paused"] == 0, f"not quitting: {s}")
shutil.rmtree(saves, ignore_errors=True)


# whole nights, played by the test's player: the first one always, the next ones with one of three tries
def night(n, seed):
    saves = tempfile.mkdtemp(prefix="fnaf2_check_")
    s, ev = run(["--night", str(n), "--bot", "--seed", str(seed)], NIGHT + 900, saves, f"night {n}")
    shutil.rmtree(saves, ignore_errors=True)
    six = [e for e in ev if e["kind"] == "six"]
    return s, six and int(six[0]["night"]) == n, ev


with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
    jobs = {(n, seed): pool.submit(night, n, seed) for n in ([1, 2] if QUICK else [1, 2, 3]) for seed in (1, 2, 3)}
    for n in sorted({k[0] for k in jobs}):
        results = [jobs[(n, seed)].result() for seed in (1, 2, 3)]
        won = [r for r in results if r[1]]
        if n == 1:
            check(len(won) == 3, f"night 1 not survived: {[r[2] for r in results]}")
        else:
            check(won, f"night {n} never survived: {[r[2] for r in results]}")
        for s, ok, ev in won:
            check(s["reached"] == n + 1 and s["night"] == n + 1, f"night {n}: the next night not reached: {s}")

# the fifth night's 6 AM: a star and the paycheck, then the title; the sixth's: the overtime, the custom night
s, ev = run(["--night", "5", "--hour", "5", "--ai", "0,0,0,0,0,0,0,0,0,0,0", "--bot"], 68 * 60 + 2400, None,
            "night 5's end")
if s:
    check(any(e["kind"] == "six" for e in ev) and s["stars"] & 1 and s["scene"] == SC_TITLE, f"night 5's end: {s} {ev}")
s, ev = run(["--night", "6", "--hour", "5", "--ai", "0,0,0,0,0,0,0,0,0,0,0", "--bot"], 68 * 60 + 2400, None,
            "night 6's end")
if s:
    check(any(e["kind"] == "six" for e in ev) and s["stars"] & 2 and s["scene"] == SC_CUSTOM, f"night 6's end: {s} {ev}")

# the music box left alone: the Puppet comes, through the mask
s, ev = run(["--night", "4", "--ai", "0,0,0,0,0,0,0,0,-1,0,9", "--box", "0", "--keys", "300:2"], 60 * 60, None,
            "the Puppet")
if s:
    check(scared_by(ev, PUPPET), f"the Puppet: {s} {ev}")

# Toy Bonnie waiting in the vent comes in with the monitor; down without the mask, up and down again: caught
s, ev = run(["--night", "1", "--step", "1:5", "--ready", "1", "--keys", "300:ok,400:ok,600:ok,700:ok"], 900, None,
            "Toy Bonnie")
if s:
    check(scared_by(ev, TB), f"Toy Bonnie: {s} {ev}")
# ... with the mask at once: he leaves
s, ev = run(["--night", "1", "--step", "1:5", "--ready", "1", "--keys", "300:ok,400:ok,422:2,1000:2"], 1100, None,
            "Toy Bonnie masked")
if s:
    check(not ev and s["occ_state"] == 0 and s["steps"][TB] == 1, f"Toy Bonnie masked: {s} {ev}")

# Balloon Boy gets in: no lights, but no jumpscare
s, ev = run(["--night", "2", "--ai", "0,0,0,0,0,0,0,0,-1,0,0", "--step", "4:3", "--ready", "4",
             "--keys", "300:ok,400:ok,500-600:5"], 1400, None, "Balloon Boy")
if s:
    check(not ev and s["bb_in"] == 1, f"Balloon Boy: {s} {ev}")

# Withered Foxy in the hallway, never flashed: he gets you; flashed, he goes back
s, ev = run(["--night", "4", "--ai", "0,0,0,0,0,0,0,0,17,0,0", "--foxy-hall"], 60 * 40, None, "Foxy")
if s:
    check(scared_by(ev, FOXY), f"Foxy: {s} {ev}")
flashes = ",".join(f"{300 + 20 * k}-{306 + 20 * k}:5" for k in range(6))
s, ev = run(["--night", "4", "--ai", "0,0,0,0,0,0,0,0,7,0,0", "--foxy-hall", "--keys", flashes + ",500:ok,600:ok"],
            700, None, "Foxy flashed")
if s:
    check(not ev and s["foxy_hall"] == 0, f"Foxy flashed: {s} {ev}")

# the custom night: the 20/20/20/20 mode starts night 7 with its levels
s, _ = run(["--stars", "3", "--keys", "100:down,120:down,140:down,160:ok,200:left,240:up,300:ok"], 600, None,
           "custom night 20")
if s:
    check(s["scene"] == SC_NIGHT and s["night"] == 7 and s["ai"][WF:FOXY + 1] == [20, 20, 20, 20] and s["ai"][PUPPET] == 15,
          f"custom night 20/20/20/20: {s}")

# Home keeps the night: the next start is back in it, paused, at the same moment; installing apps keeps only the
# scripts, and it comes back from the copy
saves = tempfile.mkdtemp(prefix="fnaf2_check_")
s, _ = run(["--night", "1", "--keys", "300:2,1500:home"], 1600, saves, "Home")
if s and record(saves, "fnaf2res.sav") is None:
    check(False, f"Home: no kept night ({s})")
elif s:
    check(s["running"] == 0, f"Home: {s}")
    os.remove(os.path.join(saves, "fnaf2res.sav"))
    s2, _ = run([], 30, saves, "back after Home")
    if s2:
        check(s2["scene"] == SC_NIGHT and s2["paused"] == 1 and s2["night"] == 1 and abs(s2["t"] - s["t"]) < 0.5
              and s2["mask"] == 1, f"back after Home, from the copy: {s2} (left at {s})")
        check(record(saves, "fnaf2res.sav") is None, "the kept night is read once")
shutil.rmtree(saves, ignore_errors=True)

# the progress: lost (comes back from the copy), and damaged (starts over, never crashes)
saves = tempfile.mkdtemp(prefix="fnaf2_check_")
s, _ = run(["--night", "1", "--hour", "5", "--ai", "0,0,0,0,0,0,0,0,-1,0,0", "--bot"], 68 * 60 + 1400, saves,
           "night 1 won")
if s and record(saves, "fnaf2.sav"):
    os.remove(os.path.join(saves, "fnaf2.sav"))
    s2, _ = run([], 30, saves, "after a reinstall")
    check(record(saves, "fnaf2.sav") is not None and s2 and s2["reached"] == 2,
          f"after a reinstall: fnaf2.sav not restored from fnaf2_saves.py ({s2})")
shutil.rmtree(saves, ignore_errors=True)
good = None
saves = tempfile.mkdtemp(prefix="fnaf2_check_")
run(["--keys", "60:ok"], 100, saves, "a save")
good = record(saves, "fnaf2.sav")
shutil.rmtree(saves, ignore_errors=True)
damaged = {"empty": b"", "short": good[:7], "long": good + b"\0", "magic": b"XXXX" + good[4:],
           "version": good[:4] + b"\x63" + good[5:], "garbage": bytes(range(len(good))),
           "levels": good[:8] + bytes([200] * 10) + good[18:]}
for name, data in damaged.items():
    saves = tempfile.mkdtemp(prefix="fnaf2_check_")
    open(os.path.join(saves, "fnaf2.sav"), "wb").write(data)
    s, _ = run(["--keys", "60:down,80:ok"], 400, saves, f"a damaged save ({name})")
    if s:
        check(1 <= s["reached"] <= 5 and s["scene"] in (SC_TITLE, SC_INTRO, SC_NIGHT, SC_CUSTOM),
              f"a damaged save ({name}): {s}")
    shutil.rmtree(saves, ignore_errors=True)
# a damaged kept night: thrown away, the title
saves = tempfile.mkdtemp(prefix="fnaf2_check_")
run(["--night", "3", "--keys", "500:home"], 600, saves, "a kept night")
night_bytes = record(saves, "fnaf2res.sav")
shutil.rmtree(saves, ignore_errors=True)
if night_bytes:
    for name, data in {"short": night_bytes[:40], "garbage": bytes((i * 37) & 255 for i in range(len(night_bytes))),
                       "places": night_bytes[:20] + bytes([99] * 30) + night_bytes[50:]}.items():
        saves = tempfile.mkdtemp(prefix="fnaf2_check_")
        open(os.path.join(saves, "fnaf2res.sav"), "wb").write(data)
        s, _ = run([], 60, saves, f"a damaged kept night ({name})")
        if s:
            check(s["scene"] == SC_TITLE and record(saves, "fnaf2res.sav") is None,
                  f"a damaged kept night ({name}): {s}")
        shutil.rmtree(saves, ignore_errors=True)


# random keys: a few hundred sessions from the title and from each night, never a crash or a sanitizer error
def fuzz(k):
    args = ["--fuzz", "--seed", str(k)]
    if k % 3:
        args += ["--night", str(1 + k % 7), "--stars", "15"]
    s, _ = run(args, 2400, None, f"random keys {k}")
    return s is not None


with concurrent.futures.ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
    n = 60 if QUICK else 300
    ok = sum(pool.map(fuzz, range(n)))
check(ok == n, f"random keys: {n - ok} of {n} sessions failed")

if failures:
    print(f"{len(failures)} failures:")
    for f in failures:
        print("-", f)
    sys.exit(1)
print("all checks passed")
