#!/usr/bin/env python3
"""Plays Temple Run on a computer (tests/play.c, built with the sanitizers) and checks it:

- a first start shows how to play, then the title; Home saves (templerun.sav and its copy, templerun_saves.py);
- runs played by the bot on many seeds, on the game's own path: corners turned, gaps jumped, steps up a level jumped,
  roots and trunks jumped, branches and fire slid under, narrow paths leaned along; the score adds up;
- a missed corner is a fall; a stumble in the first seconds, or two close together, and they catch you;
- long runs where nothing hurts, with every power-up: minutes of path, no fault;
- the store: upgrades and things to use bought, coins taken, all kept, the upgrade objectives met;
- wings save a run, a head start runs ahead;
- the save comes back from its copy when the file is gone (as after installing apps);
- pause, Quit game, Yes: the run's coins are kept;
- random keys for hundreds of sessions: no fault.

Usage: check.py build/play"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
# templerun.sav: magic, version, size, then the progress (game.h)
FMT = "<3I" + "6I" + "5BBBx" + "3HH" + "2I"
FIELDS = ["magic", "version", "size", "coins", "best", "best_dist", "runs", "life_coins", "life_dist",
          "u0", "u1", "u2", "u3", "u4", "help", "mult", "wings", "headstarts", "megas", "pad", "done0", "done1"]
FALL, TREE, BRANCH, FIRE, CAUGHT, LEDGE, TANGLE = 1, 2, 3, 4, 5, 6, 7
failed = []


def play(saves, *args):
    r = subprocess.run([PLAY, "--saves", saves, "--out", os.path.join(saves, "shots"), "--stats", *args],
                       capture_output=True, text=True, timeout=1800)
    if r.returncode != 0 or "runtime error" in r.stderr or "AddressSanitizer" in r.stderr:
        raise AssertionError(f"play {' '.join(args)} failed:\n{r.stderr[-3000:]}")
    stats = {}
    for line in r.stdout.splitlines():
        if line.startswith("frames "):
            parts = line.split()
            for k, v in zip(parts[0::2], parts[1::2]):
                stats[k] = float(v)
    return stats


def save(saves):
    data = open(os.path.join(saves, "templerun.sav"), "rb").read()
    if len(data) != struct.calcsize(FMT):
        raise AssertionError(f"templerun.sav is {len(data)} bytes")
    return dict(zip(FIELDS, struct.unpack(FMT, data)))


def check(name, fn):
    d = tempfile.mkdtemp()
    try:
        fn(d)
        print(f"ok    {name}")
    except AssertionError as e:
        failed.append(name)
        print(f"FAIL  {name}: {e}")
    finally:
        shutil.rmtree(d)


def fresh(d):
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)


def first_start(d):
    # how to play, OK, the title, Home
    play(d, "--frames", "200", "--keys", "30:ok,150:home")
    s = save(d)
    assert s["magic"] == 0x31524E54 and s["version"] == 1 and s["help"] == 1 and s["mult"] == 10, s
    copy = open(os.path.join(d, "templerun_saves.py"), "rb").read()
    assert copy[0] == 0 and b"#>templerun.sav:" in copy, copy[:80]
    # the next start: the title (OK runs)
    st = play(d, "--frames", "400", "--keys", "30:ok,350:home")
    assert st["dist"] > 10, st


def bot_runs(d):
    best = 0
    for seed in range(1, 9):
        fresh(d)
        st = play(d, "--seed", str(seed), "--frames", "7000", "--keys", "30:ok,90:ok", "--bot", "100", "--draw-every", "9")
        assert st["dist"] > 250 and st["turns"] >= 2, (seed, st)
        assert st["coins"] > 10 and st["score"] >= st["dist"] * 10, (seed, st)
        best = max(best, st["dist"])
    assert best > 2000, best


def missed_corner(d):
    # nothing pressed, plain pieces up to the first corner: the runner runs off it and falls
    for seed in (1, 2, 3):
        fresh(d)
        st = play(d, "--seed", str(seed), "--plain", "100", "--frames", "2400", "--keys", "30:ok,90:ok")
        assert st["dead"] == 1 and st["death"] == FALL and st["turns"] == 0, (seed, st)


def stumbles(d):
    # a root in the first seconds: caught at once (the pursuers are close)
    st = play(d, "--frames", "300", "--keys", "30:ok,90:ok", "--root", "100")
    assert st["dead"] == 1 and st["death"] == CAUGHT and st["stumbles"] == 0, st
    # long after: a stumble, the run goes on; a second one soon after: caught
    fresh(d)
    st = play(d, "--frames", "260", "--keys", "30:ok,90:ok", "--calm", "99", "--root", "100")
    assert st["dead"] == 0 and st["stumbles"] == 1, st
    fresh(d)
    st = play(d, "--frames", "400", "--keys", "30:ok,90:ok", "--calm", "99", "--root", "100", "--root", "250")
    assert st["dead"] == 1 and st["death"] == CAUGHT and st["stumbles"] == 1, st


def long_runs(d):
    for seed in (11, 12, 13):
        fresh(d)
        st = play(d, "--seed", str(seed), "--god", "--up", "5", "--frames", "21000", "--keys", "30:ok,90:ok",
                  "--bot", "100", "--draw-every", "11")
        assert st["dist"] > 6000 and not st["dead"], (seed, st)
        assert st["polys"] < 1700, st   # (the renderer keeps 1800)
        assert st["turns"] > 40, st


def store(d):
    # Down: the store. Coin magnet five times, Invisibility once, Wings twice, a head start; Back, Home
    keys = ["30:ok", "60:down", "70:ok"] + [f"{100 + 10 * i}:ok" for i in range(5)]
    keys += ["160:down", "170:ok", "180:down", "190:down", "200:down", "210:down", "220:ok", "230:ok", "240:down",
             "250:ok", "260:ok", "290:back", "320:home"]
    play(d, "--coins", "100000", "--frames", "400", "--keys", ",".join(keys), "--shots", "275")
    s = save(d)
    assert [s["u0"], s["u1"], s["u2"], s["u3"], s["u4"]] == [5, 1, 0, 0, 0], s
    assert s["wings"] == 2 and s["headstarts"] == 2, s
    assert s["coins"] == 100000 - (250 + 1000 + 2500 + 5000 + 7500) - 250 - 2 * 500 - 2 * 2500, s
    assert s["mult"] == 12, s   # (Buy an upgrade, Fully upgrade a power-up)


def wings_and_headstart(d):
    # OK in a run: the wings; with no one playing the runner dies, and the wings bring it back
    st = play(d, "--use", "3", "--frames", "1500", "--keys", "30:ok,90:ok,200:ok")
    assert st["saved"] >= 1, st
    assert save(d)["wings"] == 2, save(d)
    # 1 near the start: a head start, far ahead in seconds
    fresh(d)
    st = play(d, "--use", "1", "--frames", "1100", "--keys", "30:ok,90:ok,300:one,330:one,360:one,390:one,420:one", "--bot", "100")
    assert st["dist"] > 950, st
    assert save(d)["headstarts"] == 0, save(d)


def copy_restore(d):
    play(d, "--frames", "900", "--keys", "30:ok,90:ok,850:home", "--bot", "100")
    before = save(d)
    assert before["runs"] == 1 and before["coins"] > 0, before
    os.remove(os.path.join(d, "templerun.sav"))
    play(d, "--frames", "60")
    after = save(d)
    assert after == before, (before, after)


def pause_quit(d):
    # run a little, Back: pause, Quit game, Yes: the results, the coins kept
    play(d, "--god", "--frames", "1200", "--bot", "100",
         "--keys", "30:ok,90:ok,900:back,920:down,940:down,960:ok,980:right,1000:ok,1100:home")
    s = save(d)
    assert s["runs"] == 1 and s["coins"] > 0 and s["best"] > 0, s


def fuzz(d):
    for seed in range(200):
        fresh(d)
        play(d, "--seed", str(seed), "--fuzz", "0", "--coins", str(seed * 300), "--frames", "1500", "--draw-every", "5")


check("first start", first_start)
check("bot runs", bot_runs)
check("missed corner", missed_corner)
check("stumbles", stumbles)
check("long runs", long_runs)
check("store", store)
check("wings and head start", wings_and_headstart)
check("save copy", copy_restore)
check("pause, quit game", pause_quit)
check("random keys", fuzz)
sys.exit(1 if failed else 0)
