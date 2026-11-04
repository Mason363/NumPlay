#!/usr/bin/env python3
"""Deltarune played on a computer (build/play, with the sanitizers):

    python3 tests/check.py build/play

- the battle's rules against the original's (spec: FIGHT's points and damage, TP, ACTs, sparing)
- a scripted run from Kris's room into the Dark World, and one from the landing to the castle, Lancer's
  fight and Ralsei; the Field's, the Great Board's and the Forest's cutscenes and battles
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

# the Field: Lancer's Hathys (he looks in on the battle), Susie back, Lancer grabbed and his Jigsawries
rc, s = play("--room", R["room_field_maze"], "--at", "1250,1150", "--plot", 35, "--party", "1,3,0", "--frames", 4000,
             "--keys", "1-20:right", "--mash")
check("the Hathy fight, won", rc == 0 and s.get("plot") == "36" and s.get("alive") == "0" and s.get("mode") == "1")
rc, s = play("--room", R["room_field_getsusie"], "--at", "120,240", "--plot", 36, "--party", "1,3,0", "--frames", 2500,
             "--keys", "1-80:right", "--mash")
check("Susie joins again", rc == 0 and s.get("plot") == "37" and s.get("party") == "1,3,2")
rc, s = play("--room", R["room_field3"], "--at", "10,220", "--plot", 42, "--party", "1,3,2", "--frames", 9000,
             "--keys", "1-200:right", "--mash")
check("Lancer grabbed, the Jigsawries beaten", rc == 0 and s.get("plot") == "45" and s.get("alive") == "0")
# the Great Board: Lancer's team name (the first choice), C. Round crowned K. Round
rc, s = play("--room", R["room_field_checkers3"], "--at", "start", "--plot", 50, "--party", "1,3,2", "--frames", 4000,
             "--mash", "--choose", "1")
check("the team's name", rc == 0 and s.get("plot") == "55")
rc, s = play("--battle", 7, "--party", "1,3,2", "--frames", 3000, "--mash", "--menukeys", "right,z,z,right,z")
check("C. Round: Susie's strike", rc == 0 and s.get("alive") == "0")
# the Forest: Susie in Lancer's gang, the suits with them, a shortcut door, and no way on past the suits room yet
rc, s = play("--room", R["room_forest_area1"], "--at", "300,160", "--plot", 60, "--party", "1,3,0", "--frames", 3000,
             "--keys", "1-30:right", "--mash")
check("Susie with Lancer's gang", rc == 0 and s.get("plot") == "70")
rc, s = play("--room", R["room_forest_puzzle1"], "--at", "start", "--plot", 70, "--party", "1,3,0", "--frames", 2500,
             "--keys", "1-30:right", "--mash")
check("the suits with Susie and Lancer", rc == 0 and s.get("plot") == "71")
rc, s = play("--room", R["room_forest_savepoint1"], "--at", "296,130", "--plot", 61, "--party", "1,3,0", "--frames", 900,
             "--keys", "1-6:up,10:z", "--mash", "--choose", "0")
check("a shortcut door to the Field", rc == 0 and s.get("room") == str(R["room_field3"]))
rc, s = play("--room", R["room_forest_puzzle1"], "--at", "560,220", "--plot", 72, "--party", "1,3,0", "--frames", 200,
             "--keys", "1-120:right")
check("the chapter ends at the suits room for now", rc == 0 and s.get("room") == str(R["room_forest_puzzle1"]))

# ---------------------------------------------------------------- random keys
starts = [("room_krisroom", "246,97", 0), ("room_schoollobby", "110,200", 3), ("room_dark1", "280,200", 10),
          ("room_dark_eyepuzzle", "200,300", 12), ("room_castle_town", "300,600", 30), ("room_field1", "450,310", 33),
          ("room_field_maze", "start", 35), ("room_field_puzzle2", "start", 37), ("room_field_boxpuzzle", "start", 45),
          ("room_field_checkers4", "start", 50), ("room_field_checkers5", "start", 55), ("room_forest_area1", "start", 60),
          ("room_forest_puzzle1", "start", 71)]
bad = 0
for i in range(78):
    room, at, plot = starts[i % len(starts)]
    party = "1,0,0" if plot < 33 else "1,3,2" if 37 <= plot < 60 else "1,3,0" if plot >= 60 else "1,2,3"
    rc, s = play("--room", R[room], "--at", at, "--plot", plot, "--party", party,
                 "--frames", 4000, "--fuzz", i + 1)
    bad += rc != 0
for i in range(28):
    rc, s = play("--battle", [2, 3, 4, 5, 6, 9, 7, 12, 13, 14, 16, 17, 22, 24][i % 14], "--party", "1,3,2", "--frames", 4000,
                 "--fuzz", 100 + i)
    bad += rc != 0
check("random keys: 106 sessions, nothing crashed", bad == 0)

print("%d failed" % fails if fails else "all passed")
sys.exit(1 if fails else 0)
