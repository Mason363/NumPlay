#!/usr/bin/env python3
"""Deltarune played on a computer (build/play, with the sanitizers):

    python3 tests/check.py build/play

- the battle's rules against the original's (spec: FIGHT's points and damage, TP, ACTs, sparing)
- a scripted run from Kris's room into the Dark World, and one from the landing to the castle, Lancer's
  fight and Ralsei
- random keys in many places, a few hundred thousand frames: nothing may crash"""
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PLAY = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "build", "play")
fails = 0


def ids():
    out = {}
    with open(os.path.join(HERE, "..", "src", "data.h")) as f:
        for ln in f:
            m = re.match(r"\s+(room_\w+) = (\d+),", ln)
            if m:
                out[m.group(1)] = int(m.group(2))
    return out


R = ids()


def play(*args):
    r = subprocess.run([PLAY, "--saves", os.path.join(HERE, "..", "build")] + [str(a) for a in args],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stdout[-2000:], r.stderr[-4000:])
    st = {}
    for ln in r.stdout.splitlines():
        if ln.startswith("STATE") or ln.startswith("BATTLE"):
            w = ln.split()[1:]
            for k in range(0, len(w) - 1, 2):
                st[w[k]] = w[k + 1]
    return r.returncode, st


def check(name, ok):
    global fails
    print(("ok    " if ok else "FAIL  ") + name)
    fails += not ok


# ---------------------------------------------------------------- the rules
# Rudinn: 120 HP, DF 0; Kris AT 10 with the Wood Blade. A perfect FIGHT (its bolt hit on the frame it reaches
# the line) scores 150 points: damage round(AT * 150 / 20 - DF * 3) = 75, TP + 150 / 10
rc, s = play("--battle", 4, "--party", "1,0,0", "--frames", 140, "--keys", "40:z,46:z", "--perfect")
check("FIGHT, a perfect hit: 75 damage, 15 TP", rc == 0 and s.get("enemies", "").startswith("45,") and s.get("tp") == "15")
# DEFEND: 40 TP (16%)
rc, s = play("--battle", 4, "--party", "1,0,0", "--frames", 60, "--keys", "40:right,44:right,48:right,52:right,56:z")
check("DEFEND: 40 TP", rc == 0 and s.get("tp") == "40")
# Rudinn's Convince: mercy 100; then SPARE wins, and the D$ come (30, and the TP's tenth)
rc, s = play("--battle", 4, "--party", "1,0,0", "--frames", 900, "--menukeys", "right,z,z,right,z,right,right,z,z",
             "--mash")
check("ACT Convince, then SPARE: the battle is won", rc == 0 and s.get("alive") == "0" and int(s.get("gold", 0)) >= 30)
# Lancer's first fight: Kris and Susie FIGHT until he's beaten, back to the overworld
rc, s = play("--battle", 2, "--party", "1,2,0", "--frames", 9000, "--mash")
check("Lancer's first fight, won", rc == 0 and s.get("alive") == "0" and s.get("mode") == "1")

# ---------------------------------------------------------------- scripted runs
rc, s = play("--room", R["room_krisroom"], "--at", "246,97", "--frames", 12000, "--mash", "--path",
             "150,216;420,82r;590,130r;460,100@%d;460,20@%d" % (R["room_schoollobby"], R["room_schoollobby"]))
check("Kris's room to the Dark World", rc == 0 and s.get("plot") == "10" and s.get("room") == str(R["room_dark1"]))
path = ";".join([
    "600,220@%d" % R["room_dark1"], "600,220@%d" % R["room_dark1a"], "3600,290@%d" % R["room_dark2"],
    "1480,880@%d" % R["room_dark3"], "1280,300@%d" % R["room_dark_wobbles"], "1310,300@%d" % R["room_dark_eyepuzzle"],
    "1270,620@%d" % R["room_dark7"], "2000,660@%d" % R["room_dark7"], "5360,800@%d" % R["room_dark_chase1"],
    "600,260@%d" % R["room_castle_outskirts"], "300,150@%d" % R["room_castle_town"]])
rc, s = play("--room", R["room_dark1"], "--at", "280,200", "--plot", 10, "--flag", "201=1", "--frames", 16000, "--mash",
             "--path", path)
check("the landing to the castle, Lancer's fight and Ralsei", rc == 0 and s.get("plot") == "30" and s.get("party") == "1,3,0")

# the tutorial: Ralsei offers it (Yes), then a lesson a turn: FIGHT, DEFEND, his PACIFY, the Hug, SPARE; the Manual
rc, s = play("--room", R["room_castle_tutorial"], "--at", "90,310", "--plot", 30, "--party", "1,3,0", "--frames", 7000,
             "--keys", "1-170:right,1200-1700:left", "--mash",
             "--menukeys", "z,z,left,z,right,z,z,z,right,right,z,z,right,z,right,right,z,z")
check("the tutorial, FIGHT to SPARE, and the Manual", rc == 0 and s.get("plot") == "31" and s.get("alive") == "0"
      and s.get("mercy", "").startswith("100"))

# ---------------------------------------------------------------- random keys
starts = [("room_krisroom", "246,97", 0), ("room_schoollobby", "110,200", 3), ("room_dark1", "280,200", 10),
          ("room_dark_eyepuzzle", "200,300", 12), ("room_castle_town", "300,600", 30), ("room_field1", "450,310", 33)]
bad = 0
for i in range(60):
    room, at, plot = starts[i % len(starts)]
    rc, s = play("--room", R[room], "--at", at, "--plot", plot, "--party", "1,2,3" if plot >= 33 else "1,0,0",
                 "--frames", 4000, "--fuzz", i + 1)
    bad += rc != 0
for i in range(20):
    rc, s = play("--battle", [2, 3, 4, 5, 6, 9][i % 6], "--party", "1,2,3", "--frames", 4000, "--fuzz", 100 + i)
    bad += rc != 0
check("random keys: 80 sessions, nothing crashed", bad == 0)

print("%d failed" % fails if fails else "all passed")
sys.exit(1 if fails else 0)
