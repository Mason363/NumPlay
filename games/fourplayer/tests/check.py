#!/usr/bin/env python3
"""2 3 4 Players played on a computer (build/play and build/rules, with the sanitizers): the rules of every minigame
from inside, a match to a win in every game and every mode with the computer playing each player, Mix, the menus,
pause and Home, saves that come back and survive broken files, and random keys.

    python3 tests/check.py build/play build/rules [--fuzz N] [--docs]

--docs also saves the README's screenshots in docs/."""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

ARGS = [a for a in sys.argv[1:] if not a.startswith("-")]
PLAY = os.path.abspath(ARGS[0] if ARGS else "build/play")
RULES = os.path.abspath(ARGS[1] if len(ARGS) > 1 else "build/rules")
FUZZ = int(sys.argv[sys.argv.index("--fuzz") + 1]) if "--fuzz" in sys.argv else 300
WORK = tempfile.mkdtemp(prefix="fourplayer-check-")
SHOTS = os.path.join(WORK, "shots")
os.makedirs(SHOTS)
NG, MODES = 14, 5
GAMES = ["Sumo", "Racing", "Tanks", "Snakes", "Soccer", "Grab the Fish", "Skate Race", "Jump Rope", "Paint", "Balloons",
         "Birds", "Gravity Run", "Frog Jump", "Stop the Clock"]
SIDES = [2, 3, 4, 2, 2]
failed = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failed.append(what)


def run(args, saves):
    os.makedirs(saves, exist_ok=True)
    r = subprocess.run([PLAY, "--saves", saves, "--out", SHOTS] + [str(a) for a in args], capture_output=True, text=True)
    out = r.stdout + r.stderr
    if r.returncode != 0 or "runtime error" in out or "AddressSanitizer" in out:
        failed.append("crashed (%d): %s\n%s" % (r.returncode, " ".join(map(str, args))[:300], out[-1500:]))
        print("FAIL crashed: " + " ".join(map(str, args))[:200])
    return out


def save_bytes(mode=2, game=0, target=3, wins=None):
    w = wins or [[0] * 6 for _ in range(NG + 1)]
    return b"FOUR" + bytes([1, mode, game, target]) + b"".join(struct.pack("<6H", *row) for row in w)


def fresh(name, data=None):
    d = os.path.join(WORK, name)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    if data is not None:
        open(os.path.join(d, "fourplayer.sav"), "wb").write(data)
    return d


def state(out, what="STATE"):
    lines = [l for l in out.splitlines() if l.startswith(what + " ")]
    if not lines:
        return None
    m = re.match(r"\w+ (\w+) mode (\d+) game (\d+) cur (\d+) target (\d+) round (\d+) sides (\d+) score ([\d,]+) "
                 r"winner (-?\d+) matches (\d+)(?: wins (.*))?(?: frame \d+)?$", lines[-1])
    s = dict(screen=m[1], mode=int(m[2]), game=int(m[3]), cur=int(m[4]), target=int(m[5]), round=int(m[6]),
             sides=int(m[7]), score=[int(x) for x in m[8].split(",")], winner=int(m[9]), matches=int(m[10]))
    if m[11]:
        s["wins"] = [[int(x) for x in g.split(",")] for g in m[11].split()]
    return s


def screens(out):
    return [l.split()[1] for l in out.splitlines() if l.startswith("SCREEN ")]


def keys(steps, start=10):
    """'ok down w30 p1 ...': each key pressed 2 frames, 6 frames apart; wN waits N frames. Returns (keys, frame)."""
    f, ks = start, []
    for s in steps.split():
        if s.startswith("w") and s[1:].isdigit():
            f += int(s[1:])
        else:
            ks.append("%d-%d:%s" % (f, f + 1, s))
            f += 6
    return ",".join(ks), f


# ---- the rules, from inside
r = subprocess.run([RULES, WORK], capture_output=True, text=True)
out = r.stdout + r.stderr
for line in out.splitlines():
    if line.startswith("FAIL"):
        failed.append(line)
        print(line)
check(r.returncode == 0 and "0 failed" in out and "runtime error" not in out, "rules: " + out.strip().splitlines()[-1])


# ---- a match to a win, every game, every mode: the computer plays every player
def match(g, m, target=3, seed=0):
    d = fresh("m%d_%d_%d" % (g, m, seed), save_bytes(m, g, target))
    out = run(["--frames", 40000, "--keys", keys("ok ok ok")[0], "--bots", 40, "--every", 400, "--trace",
               "--seed", 3 + g * 31 + m * 7 + seed], d)
    over = [l for l in out.splitlines() if l.startswith("SCREEN over")]
    return out, d, over


jobs = [(g, m) for g in range(NG) for m in range(MODES)]
with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
    results = list(ex.map(lambda gm: (gm, match(*gm)), jobs))
lengths = {}
for (g, m), (out, d, over) in results:
    st = state(out)
    ok = bool(over) and st and st["screen"] == "over"
    if ok:
        s = state(over[0], "SCREEN")
        w = s["winner"]
        ok = s["score"][w] == 3 and all(x < 3 for i, x in enumerate(s["score"]) if i != w) and s["sides"] == SIDES[m]
        slot = 4 + w if m >= 3 else w
        ok = ok and st["wins"][g][slot] == 1 and sum(map(sum, st["wins"])) == 1
    rounds = len([l for l in out.splitlines() if l.startswith("SCREEN score")]) + 1
    lengths.setdefault(g, []).append(rounds)
    check(ok, "%s, %s: a match to a win, the win kept (%d rounds)" % (GAMES[g], ["2 players", "3 players", "4 players",
                                                                                  "2 v 2", "3 v 3"][m], rounds))

# ---- Mix: another game every round
out, d, over = match(NG, 2, 5)
plays = [int(re.search(r"cur (\d+)", l)[1]) for l in out.splitlines() if l.startswith("SCREEN rules")]
check(bool(over) and len(set(plays)) >= 4 and all(a != b for a, b in zip(plays, plays[1:])),
      "Mix: a match to 5, another game every round (%s)" % ",".join(map(str, plays)))
st = state(out)
check(st and st["wins"][NG][state(over[0], "SCREEN")["winner"]] == 1, "Mix: its wins kept apart")

# ---- the menus
d = fresh("menus")
k, f = keys("down down ok right right down ok")
out = run(["--frames", f + 20, "--keys", k, "--trace"], d)
st = state(out)
check(st and st["screen"] == "rules" and st["mode"] == 4 and st["game"] == 7, "menus: 3 v 3, then the grid to Jump Rope")
k, f = keys("ok ok w20 right right left back")
out = run(["--frames", f + 20, "--keys", k, "--trace"], d)
st = state(out)
check(st and st["screen"] == "games" and st["target"] == 4 and st["mode"] == 4,
      "menus: the mode and game kept; first to 4 on the rules; Back to the games")
k, f = keys("back back up up ok w10 up up right right")
out = run(["--frames", f + 20, "--keys", k, "--trace"], d)
st = state(out)
check(st and st["mode"] == 2 and st["game"] == NG and st["screen"] == "games", "menus: the grid wraps, Mix on top")
k, f = keys("back down ok")
out = run(["--frames", f + 20, "--keys", k, "--trace"], d)
check("QUIT" in out, "menus: Back on the players, Quit game? Yes quits")

# everyone ready: the round starts on its own
d = fresh("ready", save_bytes(2, 0))
k, f = keys("ok ok w10 p1 p2 p3")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check(state(out)["screen"] == "rules", "rules: three of four ready, still waiting")
k, f = keys("ok ok w10 p1 p2 p3 p4")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check(state(out)["screen"] == "play", "rules: the four ready, the round starts")
d = fresh("ready2", save_bytes(0, 0))
k, f = keys("ok ok w10 exe")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check(state(out)["screen"] == "play", "rules: two players, EXE starts too")
d = fresh("ready4", save_bytes(2, 0))
k, f = keys("ok ok w10 p4")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check(state(out)["screen"] == "rules", "rules: four players, EXE is a player's key, not OK")

# pause: Resume, Restart, Other games, Quit game
d = fresh("pause", save_bytes(2, 6))
k, f = keys("ok ok ok w200 back")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check(state(out)["screen"] == "pause", "pause: Back pauses a round")
k, f = keys("ok ok ok w200 back w20 back w200")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check(state(out)["screen"] == "play", "pause: Back again resumes")
k, f = keys("ok ok ok w300 back down ok w10")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
st = state(out)
check(st["screen"] == "play" and st["round"] == 0 and screens(out).count("play") == 2, "pause: Restart, a new match")
k, f = keys("ok ok ok w300 back down down ok")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check(state(out)["screen"] == "games", "pause: Other games")
k, f = keys("ok ok ok w300 back down down down ok ok")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check(state(out)["screen"] == "pause", "pause: Quit game? No")
k, f = keys("ok ok ok w300 back up ok down ok")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check("QUIT" in out, "pause: Quit game? Yes quits")

# Home in the middle of a match saves the settings and quits; next time they're back
d = fresh("home", save_bytes(1, 11, 5))
k, f = keys("ok ok w10 right ok w200 home")
out = run(["--frames", f + 10, "--keys", k, "--trace"], d)
check("QUIT" in out, "home: Home quits in a round")
out = run(["--frames", 20, "--trace"], d)
st = state(out)
check(st["mode"] == 1 and st["game"] == 11 and st["target"] == 6, "home: the mode, the game and the target come back")

# ---- saves
d = fresh("copy", save_bytes(3, 4, 7, [[i + g for i in range(6)] for g in range(NG + 1)]))
out = run(["--frames", 20, "--keys", "5:ok"], d)
copy = open(os.path.join(d, "fourplayer_saves.py"), "rb").read()
check(copy[:1] == b"\0" and b"fourplayer.sav" in copy, "saves: the copy in a Python script")
os.remove(os.path.join(d, "fourplayer.sav"))
out = run(["--frames", 20], d)
st = state(out)
check(st["mode"] == 3 and st["game"] == 4 and st["target"] == 7 and st["wins"][5] == [5, 6, 7, 8, 9, 10],
      "saves: the copy brings everything back when the save is gone")
for name, data in [("empty", b""), ("short", b"FOUR\1\2"), ("version", save_bytes()[:4] + b"\x09" + save_bytes()[5:]),
                   ("garbage", bytes(range(256)) * 2), ("mode", b"FOUR" + bytes([1, 99, 99, 0]) + bytes(180)),
                   ("long", save_bytes() + b"xx")]:
    d = fresh("bad_" + name, data)
    open(os.path.join(d, "fourplayer_saves.py"), "wb").write(b"\0# not a copy\n" if name != "mode" else b"\0#>x")
    out = run(["--frames", 400, "--keys", keys("ok ok ok")[0], "--trace"], d)
    st = state(out)
    check(st and st["mode"] in range(5) and st["game"] <= NG and 1 <= st["target"] <= 9 and st["screen"] == "play",
          "saves: a broken save (%s) starts clean and plays" % name)

# ---- random keys
def fuzz(i):
    d = fresh("fuzz%d" % i, save_bytes(i % 5, i % 15) if i % 3 else None)
    args = ["--frames", 3000, "--fuzz", i, "--every", 5, "--seed", i + 1]
    if i % 4 == 0:
        args += ["--bots", 50]
    out = run(args, d)
    return state(out) is not None


with ThreadPoolExecutor(os.cpu_count() or 4) as ex:
    ok = list(ex.map(fuzz, range(FUZZ)))
check(all(ok), "%d random-key sessions, no crash" % FUZZ)

# ---- the README's screenshots
if "--docs" in sys.argv:
    docs = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs")
    from PIL import Image

    def shot(name, data, steps, frames, seed=5, bots=True):
        d = fresh("docs_" + name, data)
        k, f = keys(steps)
        args = ["--frames", f + frames + 1, "--keys", k, "--shots", "%d:%s.ppm" % (f + frames, name), "--seed", seed]
        if bots:
            args += ["--bots", f]
        run(args, d)
        Image.open(os.path.join(SHOTS, name + ".ppm")).save(os.path.join(docs, name + ".png"), optimize=True)

    wins = [[0] * 6 for _ in range(NG + 1)]
    wins[0] = [4, 2, 5, 3, 1, 2]
    shot("players", save_bytes(2, 0, 3, wins), "", 5, bots=False)
    shot("games", save_bytes(2, 0, 3, wins), "ok", 5, bots=False)
    shot("rules", save_bytes(2, 0, 3, wins), "ok ok w10 p1 p3", 5, bots=False)
    for name, mode, game, frames, seed in [("sumo", 2, 0, 230, 5), ("racing", 2, 1, 520, 7), ("tanks", 2, 2, 320, 2),
                                           ("snakes", 2, 3, 420, 4), ("soccer", 2, 4, 330, 6), ("fish", 2, 5, 390, 3),
                                           ("skate", 4, 6, 330, 1), ("rope", 2, 7, 300, 9), ("paint", 2, 8, 700, 5),
                                           ("balloons", 2, 9, 520, 8), ("birds", 2, 10, 420, 6), ("gravity", 2, 11, 420, 3),
                                           ("frogs", 2, 12, 520, 4), ("clock", 2, 13, 700, 2), ("teams", 4, 4, 300, 3)]:
        shot(name, save_bytes(mode, game), "ok ok ok", frames, seed)
    shutil.copy(os.path.join(docs, "sumo.png"), os.path.join(docs, "shot.png"))
    print("docs: screenshots saved")

print()
if failed:
    print("%d FAILED" % len(failed))
    for f in failed[:40]:
        print("  " + f[:600])
    sys.exit(1)
print("all passed (%s)" % WORK)
shutil.rmtree(WORK, ignore_errors=True)
