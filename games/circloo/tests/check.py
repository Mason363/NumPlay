"""circloO's tests on a computer: python3 tests/check.py build/play (built with the sanitizers by make host).

- the menus start level 1;
- every level, normal and hard, played through in a row (circles taken by the test), into the credits: the
  unlocks, the next level, hard mode and the save;
- the save comes back, from its file or from its copy in circloo_saves.py, and a broken one is ignored;
- the ball rolls up the walls: played by keys, level 1 gets at least five circles, never held still while fast;
- random keys through menus and levels, with no crash.
"""
import base64
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
fails = 0


def run(*args, saves=None):
    cmd = [PLAY] + [str(a) for a in args]
    if saves:
        cmd += ["--saves", saves]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
    if r.returncode:
        print(r.stdout[-2000:], r.stderr[-4000:])
        raise SystemExit("FAIL %s exited %d" % (" ".join(cmd), r.returncode))
    m = re.search(r"END state (\d+) level (\d+) hard (\d+) taken (\d+) complete (\d+) frames (\d+) R (\d+) stuck (\d+)", r.stdout)
    return dict(zip(["state", "level", "hard", "taken", "complete", "frames", "R", "stuck"], map(int, m.groups())))


def check(ok, what):
    global fails
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        fails += 1


def read_save(d):
    b = open(os.path.join(d, "circloo.sav"), "rb").read()
    magic, version, size = struct.unpack_from("<IHH", b)
    s = b[8:]
    return dict(magic=magic, version=version, size=size, unlocked=list(s[0:2]), bonus=list(s[2:4]),
                done=[list(s[4:20]), list(s[20:36])], best=[list(struct.unpack_from("<16H", s, 36)),
                                                            list(struct.unpack_from("<16H", s, 68))], raw=b)


tmp = tempfile.mkdtemp()
try:
    d = os.path.join(tmp, "a")
    os.makedirs(d)
    r = run("--frames", 120, "--keys", "10:ok", saves=d)
    check(r["state"] == 1 and r["level"] == 1, "OK on the main menu starts level 1")

    # every level in a row: a circle taken each 25 frames, OK on each "Level complete!" (Next level, then Hard Mode)
    keys = ",".join("%d:ok" % f for f in range(400, 60000, 100))
    r = run("--frames", 60000, "--keys", "10:ok," + keys, "--take", 25, saves=d)
    s = read_save(d)
    check(r["state"] in (0, 4), "all 14 levels and the 6 hard ones played in a row, through the credits (state %d)" % r["state"])
    check(s["magic"] == 0x534F4F43 and s["version"] == 1 and s["size"] == 104 and len(s["raw"]) == 112, "the save's header")
    check(s["done"][0][1:15] == [1] * 14 and s["done"][1][1:7] == [1] * 6, "every level marked done")
    # the game's unlocking: 2 at first, one a new level, one more at levels 5 and 10
    check(s["unlocked"] == [2 + 14 + 2, 2 + 6 + 1] and s["bonus"] == [2, 1], "levels unlocked %s, bonus %s" % (s["unlocked"], s["bonus"]))
    check(all(0 < t < 2000 for t in s["best"][0][1:15] + s["best"][1][1:7]), "best times kept")

    # the copy in circloo_saves.py
    py = open(os.path.join(d, "circloo_saves.py"), "rb").read()
    m = re.search(rb"#>circloo\.sav:([A-Za-z0-9+/=]+)", py)
    check(m and base64.b64decode(m.group(1)) == s["raw"], "circloo_saves.py holds the same save")
    e = os.path.join(tmp, "b")
    os.makedirs(e)
    shutil.copy(os.path.join(d, "circloo_saves.py"), e)
    run("--frames", 5, saves=e)
    check(read_save(e)["done"] == s["done"], "the save comes back from circloo_saves.py alone")

    # a broken save: the game starts fresh
    f = os.path.join(tmp, "c")
    os.makedirs(f)
    open(os.path.join(f, "circloo.sav"), "wb").write(s["raw"][:8] + b"\xff" * 104)
    r = run("--frames", 120, "--keys", "10:ok", saves=f)
    check(r["state"] == 1 and r["level"] == 1 and read_save(f)["unlocked"] == [2, 2], "a broken save is ignored")

    # by keys: the ball climbs the walls (level 1: five circles by a simple player), and never stops while fast
    g = os.path.join(tmp, "g")
    os.makedirs(g)
    r = run("--level", 1, "--auto", "--frames", 6000, saves=g)
    check(r["taken"] >= 5 and r["stuck"] == 0, "level 1 by keys: %d circles taken, %d frames held" % (r["taken"], r["stuck"]))
    for lv in (4, 7, 13):
        r = run("--level", lv, "--auto", "--frames", 3000, saves=g)
        check(r["stuck"] == 0, "level %d by keys: never held still while fast" % lv)

    # random keys
    n = int(os.environ.get("FUZZ", "120"))
    for seed in range(n):
        h = os.path.join(tmp, "f%d" % seed)
        os.makedirs(h)
        args = ["--fuzz", seed, "--frames", 2500]
        if seed % 3 == 1:
            args += ["--level", 1 + seed % 14]
        if seed % 3 == 2:
            args += ["--level", 1 + seed % 6, "--hard", "--take", 97]
        run(*args, saves=h)
    check(True, "%d random sessions without a crash" % n)
finally:
    shutil.rmtree(tmp)
print("circloO: %s" % ("all good" if not fails else "%d failed" % fails))
sys.exit(1 if fails else 0)
