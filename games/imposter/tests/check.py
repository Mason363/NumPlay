#!/usr/bin/env python3
"""Imposter played on a computer (build/play, with the sanitizers): the word lists, rounds from the setup to the
reveal with every role on the right player's screen, words never twice until their category runs out, saves that
come back and survive broken files, random keys.

    python3 tests/check.py build/play [--fuzz N]"""
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

PLAY = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 and not sys.argv[1].startswith("-") else "build/play")
FUZZ = int(sys.argv[sys.argv.index("--fuzz") + 1]) if "--fuzz" in sys.argv else 300
WORK = tempfile.mkdtemp(prefix="imposter-check-")
SAVES = os.path.join(WORK, "saves")
SHOTS = os.path.join(WORK, "shots")
os.makedirs(SHOTS)
failed = []


def check(ok, what):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failed.append(what)


def run(args, saves=SAVES):
    os.makedirs(saves, exist_ok=True)
    r = subprocess.run([PLAY, "--saves", saves, "--out", SHOTS] + [str(a) for a in args], capture_output=True, text=True)
    out = r.stdout + r.stderr
    if r.returncode != 0 or "ERROR" in out or "runtime error" in out or "AddressSanitizer" in out:
        failed.append("crashed (%d): %s\n%s" % (r.returncode, " ".join(map(str, args))[:300], out[-1500:]))
        print("FAIL crashed: " + " ".join(map(str, args))[:200])
    return out


def fresh(saves=SAVES):
    shutil.rmtree(saves, ignore_errors=True)
    os.makedirs(saves)


def walk(steps, extra=(), saves=SAVES):
    """Keys pressed 2 frames each, 6 frames apart: a key name, 'wN' waits N frames, 'shot:NAME' saves the screen.
    Returns the output and {NAME: picture path}."""
    f, keys, shots = 10, [], []
    for s in steps.split():
        if s.startswith("w") and s[1:].isdigit():
            f += int(s[1:])
        elif s.startswith("shot:"):
            shots.append("%d:%s.ppm" % (f, s[5:]))
            f += 1
        else:
            keys.append("%d-%d:%s" % (f, f + 1, s))
            f += 6
    args = ["--trace", "--frames", f + 10, "--keys", ",".join(keys)]
    if shots:
        args += ["--shots", ",".join(shots)]
    out = run(args + list(extra), saves)
    return out, {s.split(":")[1][:-4]: os.path.join(SHOTS, s.split(":")[1]) for s in shots}


def state(out):
    m = re.findall(r"^STATE n (\d+) names (.*?) imposters (\d+) hint (\d+) timer (\d+) cats (\d+) phase (\d+) turn (\d+) "
                   r"rounds (\d+) word (\d+) flags (\d+)$", out, re.M)
    if not m:
        return None
    n, names, imps, hint, timer, cats, phase, turn, rounds, word, flags = m[-1]
    return dict(n=int(n), names=names.split(",") if names else [], imposters=int(imps), hint=int(hint),
                timer=int(timer), cats=int(cats), phase=int(phase), turn=int(turn), rounds=int(rounds), word=int(word))


def pixel(path, x, y):
    with open(path, "rb") as f:
        data = f.read()
    head = data.split(b"\n", 3)
    w = int(head[1].split()[0])
    px = head[3]
    i = (y * w + x) * 3
    return px[i], px[i + 1], px[i + 2]


def typed(name):
    return " ".join("space" if c == " " else c.lower() for c in name)


# ---- the words
out = subprocess.run([PLAY, "--words"], capture_output=True, text=True).stdout
m = re.search(r"WORDS cats (\d+) total (\d+) min (\d+) max (\d+) bad (\d+)", out)
check(m and int(m.group(1)) >= 25 and int(m.group(3)) >= 40 and int(m.group(4)) <= 64 and m.group(5) == "0",
      "words: %s" % (m.group(0) if m else out[-300:]))
CATS = dict((name, int(n)) for name, n in re.findall(r"^CAT (.+) (\d+)$", out, re.M))
TOTAL = int(m.group(2)) if m else 0


def check_round(out, names, imposters, hint, what):
    """Every pass screen names the next player in order; the role after it is theirs; the talk and the reveal
    agree."""
    out = out[out.index("DRAW pass"):] if "DRAW pass" in out else out  # (from the round's first screen)
    draws = [l for l in out.splitlines() if l.startswith("DRAW ")]
    seq = [l for l in draws if l.startswith(("DRAW pass", "DRAW role", "DRAW talk", "DRAW reveal"))]
    passes = [re.match(r"DRAW pass dialog 0 turn (\d+) name (.+)$", l) for l in seq if l.startswith("DRAW pass")]
    roles = [l for l in seq if l.startswith("DRAW role")]
    order = []
    for p in passes:
        if p and (not order or order[-1] != (int(p.group(1)), p.group(2))):
            order.append((int(p.group(1)), p.group(2)))
    check(order == list(enumerate(names)), "%s: the calculator passed to %s" % (what, ", ".join(n for _, n in order)))
    word = cat = None
    seen_imps = set()
    ok = len(roles) >= len(names)
    for line in roles:
        m1 = re.match(r"DRAW role dialog 0 turn (\d+) imposter hint (.+)$", line)
        m2 = re.match(r"DRAW role dialog 0 turn (\d+) word (.+) cat (.+)$", line)
        if m1:
            seen_imps.add(int(m1.group(1)))
        elif m2:
            word, cat = (m2.group(2), m2.group(3)) if word is None else (word, cat)
            ok &= (m2.group(2), m2.group(3)) == (word, cat)
        else:
            ok = False
    for line in roles:
        m1 = re.match(r"DRAW role dialog 0 turn (\d+) imposter hint (.+)$", line)
        if m1:
            ok &= m1.group(2) == (cat if hint else "-")
    check(ok and len(seen_imps) == imposters,
          "%s: %d imposters (%s) saw their screen%s, the others %s (%s)" %
          (what, len(seen_imps), ", ".join(names[i] for i in sorted(seen_imps)),
           " with the category" if hint else " without a hint", word, cat))
    talk = re.search(r"DRAW talk dialog 0 starter (\d+) (.+) left", out)
    check(talk and names[int(talk.group(1))] == talk.group(2), "%s: %s starts" % (what, talk.group(2) if talk else "?"))
    rev = re.search(r"DRAW reveal dialog 0 word (.+) imps (\d+)$", out, re.M)
    mask = sum(1 << i for i in seen_imps)
    check(rev and rev.group(1) == word and int(rev.group(2)) == mask,
          "%s: the reveal shows %s and the imposters" % (what, rev.group(1) if rev else "?"))
    return word, seen_imps


# ---- a first game: the guide, names typed with the letter keys, the settings, a whole round
fresh()
NAMES = ["Ana", "Ben", "Chris", "Dana", "Mary Jo"]
steps = "ok ok ok " + " exe ".join(typed(n) for n in NAMES) + " exe back back"
steps += " down down down right right down shot:setup up up up up ok"
for i in range(len(NAMES)):
    steps += " w4 shot:pass%d ok w32 shot:role%d ok w32" % (i, i)
steps += " shot:talk ok shot:ask ok shot:reveal"
out, pics = walk(steps)
draws = [l for l in out.splitlines() if l.startswith("DRAW")]
check(draws and draws[0].startswith("DRAW help"), "the first start shows How to play")
st = state(out)
check(st and st["names"] == NAMES, "names typed with the letter keys: %s" % (st and st["names"]))
check(st and st["imposters"] == 2 and st["hint"] == 1, "two imposters, the category as a hint")
word, imps = check_round(out, NAMES, 2, True, "round 1")
colours_ok = True
for i in range(len(NAMES)):
    r, g, b = pixel(pics["role%d" % i], 4, 120)
    colours_ok &= (r > 150 and g < 100) if i in imps else (b > 150 and r < 100)
check(colours_ok, "imposter screens red, the others blue")
r, g, b = pixel(pics["pass0"], 4, 120)
check(b > r and r < 100, "the pass screen keeps the secret (the game's own colours)")

# the next round: a new word, new imposters drawn
out, _ = walk("ok " + " ".join(["ok w32 ok w32"] * len(NAMES)) + " ok ok")
word2, _ = check_round(out, NAMES, 2, True, "round 2")
check(word2 != word, "round 2 has a new word: %s" % word2)

# a session later: the same players and settings
out = run(["--frames", 5])
st2 = state(out)
check(st2 and st2["names"] == NAMES and st2["imposters"] == 2 and st2["rounds"] == 2,
      "the save comes back: %s, %d imposters, round %d" % (st2 and ",".join(st2["names"]), st2 and st2["imposters"],
                                                          st2 and st2["rounds"]))

# ---- names on the screen's letters (another target's keys carry none), a name already taken, removing one
fresh()
run(["--players", "Ana,Ben", "--frames", 2])
# B: right; O: down down right (rows keep the column as near as they can); then down to Done
out, _ = walk("ok ok right ok down down right ok z down down down ok back back", ["--no-typing"])
st = state(out)
check(st and st["names"] == ["Ana", "Ben", "Bo"], "a name picked on the screen's letters: %s" % (st and st["names"]))
out, _ = walk("down ok down down down ok " + typed("ben") + " exe w2 back back")
st = state(out)
check("DRAW name dialog 0 typed Ben" in out and st and st["names"] == ["Ana", "Ben", "Bo"],
      "a name already playing is refused: %s" % (st and st["names"]))
out, _ = walk("down ok down erase ok back")
st = state(out)
check(st and st["names"] == ["Ana", "Bo"], "a player removed after the question: %s" % (st and st["names"]))
out, _ = walk("ok")
check(out.strip().splitlines()[-2].startswith("DRAW players"), "Start with 2 players opens Players")

# ---- the rules: a word comes again only once its category has run out
PLAYERS = "Ann,Bob,Cal,Dee"
fresh()
out = run(["--players", PLAYERS, "--rounds", 2 * TOTAL])
rounds = [(int(w), int(c)) for w, c in re.findall(r"^ROUND \d+ word (\d+) .* cat (\d+) ", out, re.M)]
sizes = list(CATS.values())
played = [set() for _ in sizes]
early = 0
for w, c in rounds:
    if w in played[c]:
        early += len(played[c]) < sizes[c]
        played[c] = set()
    played[c].add(w)
first = len({w for w, _ in rounds[:TOTAL * 9 // 10]})
check(len(rounds) == 2 * TOTAL and early == 0,
      "%d rounds with every category: no word again before its category ran out (the first %d all different)" %
      (len(rounds), first))
idx = list(CATS).index("The sea")
n = CATS["The sea"]
fresh()
out = run(["--players", PLAYERS, "--cats", 1 << idx, "--rounds", 20])
out2 = run(["--rounds", n - 20])
w1 = re.findall(r"^ROUND \d+ word (\d+) \S.* cat (\d+) ", out + out2, re.M)
check(len(w1) == n and len(set(w for w, _ in w1)) == n and {c for _, c in w1} == {str(idx)},
      "The sea over two sessions: its %d words once each" % n)
out3 = run(["--rounds", n])
w2 = re.findall(r"^ROUND \d+ word (\d+) ", out3, re.M)
check(len(set(w2)) == n and w2[0] != w1[-1][0], "The sea run out: all its words again, not the last one first")
mask = (1 << 3) | (1 << 17)
fresh()
out = run(["--players", PLAYERS, "--cats", mask, "--rounds", 80])
cats = set(int(c) for c in re.findall(r"^ROUND \d+ word \d+ \S.* cat (\d+) ", out, re.M))
check(cats == {3, 17}, "only the chosen categories: %s" % sorted(cats))
for players, setting, want in [("Ann,Bob,Cal", 3, 1), ("Ann,Bob,Cal,Dee,Eve", 3, 2), ("A,B,C,D,E,F,G,H,I,J,K,L", 3, 3),
                               ("Ann,Bob,Cal,Dee,Eve,Fay,Gus", 2, 2)]:
    fresh()
    out = run(["--players", players, "--imposters", setting, "--rounds", 300])
    masks = [int(x) for x in re.findall(r"imps (\d+) starter", out)]
    starters = [int(x) for x in re.findall(r"starter (\d+)$", out, re.M)]
    k = len(players.split(","))
    counts = [sum(m >> p & 1 for m in masks) for p in range(k)]
    exp = 300 * want / k
    check(all(bin(m).count("1") == want for m in masks) and min(counts) > exp * 0.6 and max(counts) < exp * 1.4 and
          set(starters) == set(range(k)),
          "%d players, %d imposters asked: %d each round, everyone in turn (%d to %d times)" %
          (k, setting, want, min(counts), max(counts)))

# ---- a round goes on after Home
fresh()
run(["--players", ",".join(NAMES), "--frames", 2])
out, _ = walk("ok w2 ok w32 ok w32 ok w32 ok w32 home")
st = state(out)
out = run(["--trace", "--frames", 5])
st2 = state(out)
check(st and st["phase"] == 1 and st["turn"] == 2 and st2 and st2["word"] == st["word"] and
      "DRAW pass dialog 0 turn 2 name Chris" in out, "Home in a round, then back: Chris is next, the same word")

# ---- broken files
SAV, COPY = os.path.join(SAVES, "imposter.sav"), os.path.join(SAVES, "imposter_saves.py")
fresh()
run(["--players", ",".join(NAMES), "--imposters", 2, "--frames", 2])
good_sav, good_copy = open(SAV, "rb").read(), open(COPY, "rb").read()
check(good_copy.startswith(b"\0# Imposter's saves: keep this file\n#>imposter.sav:"), "the copy is a Python script")


def with_files(sav, copy, what, want_names):
    fresh()
    for path, data in ((SAV, sav), (COPY, copy)):
        if data is not None:
            open(path, "wb").write(data)
    out = run(["--frames", 3])
    st = state(out)
    check(st is not None and st["names"] == want_names, "%s: %s" % (what, st and (st["names"] or "a new game")))


with_files(good_sav[:10], good_copy, "a cut save: back from the copy", NAMES)
with_files(b"", good_copy, "an empty save: back from the copy", NAMES)
with_files(None, good_copy, "no save, the copy: back", NAMES)
with_files(good_sav, None, "the save without its copy", NAMES)
with_files(bytes(len(good_sav)), b"junk", "both broken: a new game", [])
with_files(None, good_copy[:60] + b"!!!!" + good_copy[64:], "no save, a broken copy: a new game", [])
rng = random.Random(7)
bad = 0
for k in range(150):
    sav = bytearray(good_sav)
    for _ in range(rng.choice([1, 2, 5, 40])):
        sav[rng.randrange(5, len(sav))] = rng.randrange(256)
    fresh()
    open(SAV, "wb").write(bytes(sav))
    out = run(["--fuzz", k, "--frames", 300])
    bad += "STATE" not in out
check(bad == 0 and not [f for f in failed if "crashed" in f], "150 damaged saves: each loads into a game it could be")

# ---- random keys
def fuzz(k):
    d = os.path.join(WORK, "fuzz%d" % k)
    os.makedirs(d, exist_ok=True)
    if k % 3 == 0:  # players already there: rounds sooner
        subprocess.run([PLAY, "--saves", d, "--players", ",".join(NAMES[: 3 + k % 6]), "--timer", str(k % 5),
                        "--frames", "1"], capture_output=True)
    return run(["--fuzz", k, "--frames", 3000], saves=d)


with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as ex:
    outs = list(ex.map(fuzz, range(FUZZ)))
sessions = sum(int(m) for o in outs for m in re.findall(r"FUZZ sessions (\d+)", o))
reached = sum("phase 3" in o or "phase 2" in o or "phase 1" in o for o in outs)
check(not [f for f in failed if "crashed" in f], "%d random-key runs (%d sessions, %d with rounds): no crash" %
      (FUZZ, sessions, reached))

shutil.rmtree(WORK, ignore_errors=True)
if failed:
    print("\n%d FAILED" % len(failed))
    for f in failed:
        print("  " + f[:2000])
    sys.exit(1)
print("\nall passed")
