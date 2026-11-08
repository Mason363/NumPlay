#!/usr/bin/env python3
"""Plays Subway Surfers on a computer (tests/play.c, built with the sanitizers) and checks it:

- a first start shows how to play, then the title; Home saves (subway.sav and its copy, subway_saves.py);
- runs played by the bot on many seeds: no fault, the world keeps coming, the score and coins add up;
- long runs where nothing hurts: minutes of track at full speed, tunnels, stations, every power-up; the runner never
  jumps through a low roof;
- the shop: upgrades, items, characters and boards bought, coins taken, all kept;
- the save comes back from its copy when the file is gone (as after installing apps);
- Save me: keys spent, the run goes on; pause and Quit game: the coins are kept.

Usage: check.py build/play"""
import os
import shutil
import struct
import subprocess
import sys
import tempfile

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
# subway.sav: magic, size, then the progress (game.h)
FMT = "<2I" + "4I4H4B8B3I16I"
FIELDS = ["magic", "size", "coins", "keys", "best", "runs", "boards", "headstarts", "boosters", "mystery",
          "u0", "u1", "u2", "u3", "chars", "ownboards", "character", "board", "mset", "mdone", "help", "pad",
          "b0", "b1", "b2"] + [f"t{i}" for i in range(16)]
failed = []


def play(saves, *args):
    r = subprocess.run([PLAY, "--saves", saves, "--out", os.path.join(saves, "shots"), "--stats", *args],
                       capture_output=True, text=True, timeout=1200)
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
    data = open(os.path.join(saves, "subway.sav"), "rb").read()
    if len(data) != struct.calcsize(FMT):
        raise AssertionError(f"subway.sav is {len(data)} bytes")
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


def first_start(d):
    # how to play, OK, the title, Home
    play(d, "--frames", "200", "--keys", "30:ok,150:home")
    s = save(d)
    assert s["magic"] == 0x31425553 and s["help"] == 1, s
    copy = open(os.path.join(d, "subway_saves.py"), "rb").read()
    assert copy[0] == 0 and b"#>subway.sav:" in copy, copy[:80]
    # the next start: no how to play, straight to the title (OK plays at once)
    st = play(d, "--frames", "400", "--keys", "30:ok,350:home")
    assert st["dist"] > 30, st


def bot_runs(d):
    best = 0
    for seed in range(1, 9):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        st = play(d, "--seed", str(seed), "--frames", "4000", "--keys", "30:ok,90:ok", "--bot", "100", "--draw-every", "7")
        assert st["dist"] > 120, (seed, st)
        assert st["coins"] > 10 and st["score"] >= st["dist"] * 4.5, (seed, st)
        best = max(best, st["dist"])
    assert best > 600, best


def long_runs(d):
    for seed in (11, 12, 13):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
        st = play(d, "--seed", str(seed), "--god", "--frames", "20000", "--keys", "30:ok,90:ok,600:ok,8000:ok",
                  "--bot", "100", "--power", "2000:0", "--power", "5000:1", "--power", "9000:2", "--power", "12000:3",
                  "--draw-every", "9")
        assert st["dist"] > 6000 and not st["dead"], (seed, st)
        assert st["polys"] < 1750, st   # (the renderer keeps 1800)
        assert st["mset"] >= 1, st   # (missions done along the way)
        assert st["roof_over"] <= 0.01, st   # (under the elevated line and in stations, a jump stops at the roof)


def shop(d):
    # Down: the buttons, OK: the shop. Upgrades: OK six times on each line
    keys = ["30:ok", "60:down", "70:ok"]
    t = 100
    for row in range(4):
        for _ in range(6):
            keys.append(f"{t}:ok")
            t += 10
        keys.append(f"{t}:down")
        t += 10
    # items: a hoverboard twice, a headstart, a score booster, a mystery box
    keys += [f"{t}:right", f"{t + 10}:ok", f"{t + 20}:ok", f"{t + 30}:down", f"{t + 40}:ok", f"{t + 50}:down", f"{t + 60}:ok",
             f"{t + 70}:down", f"{t + 80}:ok"]
    t += 100
    # characters: Tricky, Fresh; boards: the Bouncer
    keys += [f"{t}:right", f"{t + 10}:down", f"{t + 20}:ok", f"{t + 30}:down", f"{t + 40}:ok", f"{t + 50}:right",
             f"{t + 60}:down", f"{t + 70}:ok", f"{t + 90}:back", f"{t + 110}:home"]
    play(d, "--coins", "400000", "--frames", str(t + 200), "--keys", ",".join(keys), "--shots", f"{t - 50},{t + 80}")
    s = save(d)
    assert [s["u0"], s["u1"], s["u2"], s["u3"]] == [6, 6, 6, 6], s
    assert s["boards"] >= 5 and s["headstarts"] >= 1 and s["boosters"] >= 1, s
    assert s["chars"] == 0b111 and s["character"] == 2, s
    assert s["ownboards"] == 0b11 and s["board"] == 1, s
    spent = 4 * (500 + 1000 + 2500 + 5000 + 15000 + 50000) + 600 + 2000 + 3000 + 500 + 6000 + 12000 + 7500
    assert s["coins"] <= 400000 - spent + 20000, s   # (a mystery box may give coins back)


def copy_restore(d):
    play(d, "--frames", "500", "--keys", "30:ok,90:ok,450:home")
    before = save(d)
    assert before["runs"] == 1 and before["coins"] > 0, before
    os.remove(os.path.join(d, "subway.sav"))
    play(d, "--frames", "60")
    after = save(d)
    assert after == before, (before, after)


def save_me(d):
    # no bot: the runner crashes into the first train or barrier; Save me, OK: one key, and on
    st = play(d, "--seed", "1", "--keys-owned", "3", "--frames", "1400", "--keys", "30:ok,90:ok,780:ok,1300:home")
    s = save(d)
    assert s["keys"] == 2, s
    assert st["dist"] > 130, st


def pause_quit(d):
    # run a little, Back: pause, Quit game, Yes: the results, the coins kept
    play(d, "--god", "--frames", "800", "--keys", "30:ok,90:ok,500:back,520:down,540:ok,560:right,580:ok,700:home")
    s = save(d)
    assert s["runs"] == 1 and s["coins"] > 0 and s["best"] > 0, s


check("first start", first_start)
check("bot runs", bot_runs)
check("long runs", long_runs)
check("shop", shop)
check("save copy", copy_restore)
check("save me", save_me)
check("pause, quit game", pause_quit)
sys.exit(1 if failed else 0)
