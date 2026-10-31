#!/usr/bin/env python3
"""Checks of Undertale on a computer (make test): the host build, with
AddressSanitizer and UBSan, through the menus, every battle, the saves, the
whole Ruins, the whole of Snowdin and Waterfall, and Hotland as far as it goes.

    python3 tests/check.py build/play [--quick]

--quick skips playing the whole Ruins, Snowdin, Waterfall and Hotland (the longest parts)."""
import os
import re
import shutil
import subprocess
import sys
import tempfile

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
HERE = os.path.dirname(os.path.abspath(__file__))
QUICK = "--quick" in sys.argv
failures = []
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")

# the flags the checks look at (src/flags.h)
FLAGS = {}
src = open(os.path.join(HERE, "..", "src", "flags.h")).read()
body = re.sub(r"/\*.*?\*/", "", src[src.index("enum {") + 6:src.rindex("FLAG_COUNT")], flags=re.S)
for i, n in enumerate(n.strip() for n in body.split(",") if n.strip()):
    FLAGS[n] = i

MODE_WORLD, MODE_BATTLE, MODE_END, MODE_SHOP = 2, 3, 8, 9
# rooms and battles (src/data.h, src/monsters.h)
ROOMS = {m.group(1): int(m.group(2)) for m in
         re.finditer(r"#define ROOM_(\w+) (\d+)", open(os.path.join(HERE, "..", "src", "data.h")).read())}
FORM_SNOWDRAKE, FORM_PAPYRUS = 19, 31
(FORM_AARON, FORM_WOSHUA, FORM_WOSHUA_AARON, FORM_MOLDBYGG, FORM_MOLDBYGG_MOLDSMAL, FORM_MOLDBYGG_WOSHUA, FORM_SHYREN,
 FORM_MAD_DUMMY, FORM_TEMMIE, FORM_TEMMIE_MOLDBYGG, FORM_UNDYNE) = range(32, 43)
(FORM_VULKIN, FORM_TSUNDERPLANE, FORM_TSUNDER_VULKIN, FORM_PYROPE, FORM_PYROPE2, FORM_MTT_QUIZ, FORM_MTT_COOK,
 FORM_LASERS, FORM_RGUARDS, FORM_BOMB, FORM_MUFFET, FORM_MTT_TILES) = range(43, 55)
ITEM_OLD_TUTU, ITEM_BALLET_SHOES, ITEM_QUICHE, ITEM_PUNCH_CARD, ITEM_ARTIFACT, ITEM_ASTRONAUT_FOOD = range(17, 23)
ITEM_BURNT_PAN, ITEM_INSTANT_NOODLES, ITEM_HOT_DOG, ITEM_STAINED_APRON = 28, 29, 30, 31
ITEM_NICE_CREAM = 13
ITEM_STARFAIT, ITEM_GLAMBURGER = 32, 33
ITEM_WORN_DAGGER, ITEM_HEART_LOCKET = 41, 42
ITEM_ANNOYING_DOG, ITEM_DOG_RESIDUE = 43, 44


def run(args, frames, saves=None, what=""):
    """Plays (in a save folder, a new one if None); returns the end state: mode, room, flags, items..."""
    own = saves is None
    if own:
        saves = tempfile.mkdtemp(prefix="undertale_check_")
    p = subprocess.run([PLAY, "--saves", saves, "--frames", str(frames)] + args, capture_output=True, text=True,
                       env=ENV, timeout=900)
    if own:
        shutil.rmtree(saves, ignore_errors=True)
    if p.returncode or "runtime error" in p.stderr or "AddressSanitizer" in p.stderr:
        failures.append(f"{what or args}: crashed or a sanitizer error\n{p.stderr[-2000:]}")
        return None
    m = re.search(r"END mode (\d+) room (-?\d+) at (-?\d+),(-?\d+) hp (-?\d+) lv (\d+) gold (\d+) kills (\d+) path (\w+)",
                  p.stdout)
    if not m:
        failures.append(f"{what}: no end state\n{p.stdout[-500:]}")
        return None
    s = dict(zip(("mode", "room", "x", "y", "hp", "lv", "gold", "kills"), map(int, m.groups()[:8])))
    s["path"] = m.group(9)
    fl = re.search(r"FLAGS(.*)", p.stdout).group(1).split()
    s["flags"] = {int(a): int(b) for a, b in (f.split("=") for f in fl)}
    s["items"] = [int(v) for v in re.search(r"ITEMS(.*)", p.stdout).group(1).split()]
    return s


def flag(s, name):
    return s["flags"].get(FLAGS["F_" + name], 0)


def check(cond, msg):
    if not cond:
        failures.append(msg)


# the intro, the title, the instructions, naming the fallen human "A", Done, Yes: the flowers
keys = "30:z,60:z,90:z,120:z," + ",".join("%d:down" % (150 + 10 * i) for i in range(8)) + \
       ",230:right,240:right,250:z,290:right,300:z"
s = run(["--keys", keys], 700, what="the menus to a new game")
check(s and s["mode"] == MODE_WORLD and s["room"] == 0 and s["hp"] == 20, f"the menus did not start a game: {s}")

# every battle of the Ruins, fled from (Toriel's can't be: she is spared) and fought to the end
for form in range(18):
    s = run(["--god", "--battle", str(form), "--path", "flee"], 1500, what=f"battle {form}, fleeing")
    if form == 17:
        continue
    check(s and s["mode"] == MODE_WORLD, f"battle {form}: fleeing did not end it: {s}")
for form in (2, 3, 6, 10, 12):
    s = run(["--god", "--battle", str(form), "--path", "fight"], 3000, what=f"battle {form}, fighting")
    check(s and s["mode"] == MODE_WORLD and s["kills"] >= 1, f"battle {form}: FIGHT did not win it: {s}")
s = run(["--god", "--battle", "16", "--path", "act:3"], 6000, what="Napstablook, cheered up")
check(s and s["mode"] == MODE_WORLD and s["kills"] == 0, f"Napstablook was not spared by cheering: {s}")

# Snowdin's battles: fled from, fought (the Snowdrake, the Gyftrot, the Glyde, the Greater Dog...), and Papyrus,
# who can't be fled: spared once he spares you, or (the SOUL hurt) he captures you
for form in range(FORM_SNOWDRAKE, FORM_PAPYRUS):
    s = run(["--god", "--battle", str(form), "--path", "flee"], 1500, what=f"battle {form}, fleeing")
    check(s and s["mode"] == MODE_WORLD, f"battle {form}: fleeing did not end it: {s}")
for form in (19, 20, 26, 27, 28, 30):
    s = run(["--god", "--battle", str(form), "--path", "fight"], 4000, what=f"battle {form}, fighting")
    check(s and s["mode"] == MODE_WORLD and s["kills"] >= 1, f"battle {form}: FIGHT did not win it: {s}")
s = run(["--god", "--battle", str(FORM_PAPYRUS), "--path", "spare"], 15000, what="Papyrus, spared")
check(s and s["mode"] == MODE_WORLD and s["kills"] == 0, f"Papyrus was not spared: {s}")
s = run(["--battle", str(FORM_PAPYRUS), "--path", "spare"], 6000, what="Papyrus, captured")
check(s and s["mode"] == MODE_WORLD and s["hp"] == 1, f"Papyrus did not capture Frisk: {s}")

# Waterfall's battles: fled from, fought, and each spared its own way (Aaron out-flexed, Woshua's green caught after
# asking to be cleaned, Moldbygg hugged then let go, Shyren hummed along with, Temmie talked to); Mad Dummy can't be
# fled or hurt: its fight lasts until Napstablook comes. Undyne can't be spared: fled from (her arrows blocked with
# the green SOUL's shield)
for form in list(range(FORM_AARON, FORM_SHYREN + 1)) + [FORM_TEMMIE, FORM_TEMMIE_MOLDBYGG, FORM_UNDYNE]:
    s = run(["--god", "--battle", str(form), "--path", "flee"], 1500, what=f"battle {form}, fleeing")
    check(s and s["mode"] == MODE_WORLD, f"battle {form}: fleeing did not end it: {s}")
for form in (FORM_AARON, FORM_WOSHUA, FORM_MOLDBYGG, FORM_SHYREN, FORM_TEMMIE):
    s = run(["--god", "--battle", str(form), "--path", "fight"], 4000, what=f"battle {form}, fighting")
    check(s and s["mode"] == MODE_WORLD and s["kills"] >= 1, f"battle {form}: FIGHT did not win it: {s}")
for form, seq, gold in ((FORM_AARON, "1,1,1", 30), (FORM_WOSHUA, "1,1,1", 25), (FORM_WOSHUA_AARON, "1,1@2,1@2,1@2", 55),
                        (FORM_MOLDBYGG, "1,3", 20), (FORM_MOLDBYGG_WOSHUA, "1,3,1@2,1@2,1@2", 45),
                        (FORM_SHYREN, "2,2,2,2,2", 20), (FORM_TEMMIE, "2", 0), (FORM_TEMMIE_MOLDBYGG, "2,1@2,3@2", 20)):
    s = run(["--god", "--battle", str(form), "--path", "seq:" + seq], 9000, what=f"battle {form}, sparing")
    check(s and s["mode"] == MODE_WORLD and s["kills"] == 0 and s["gold"] == gold,
          f"battle {form}: the friendly way did not spare it: {s}")
s = run(["--god", "--battle", str(FORM_MAD_DUMMY), "--path", "act:1"], 9000, what="Mad Dummy")
check(s and s["mode"] == MODE_WORLD and s["kills"] == 0, f"Mad Dummy's fight did not end: {s}")

# Hotland's battles: fled from, fought, spared (Vulkin encouraged, Pyrope heated up twice); Mettaton's quiz, his
# cooking show's jetpack and a laser's alarm play their own way to their end
for form in range(FORM_VULKIN, FORM_PYROPE2 + 1):
    s = run(["--god", "--battle", str(form), "--path", "flee"], 1500, what=f"battle {form}, fleeing")
    check(s and s["mode"] == MODE_WORLD, f"battle {form}: fleeing did not end it: {s}")
for form in (FORM_VULKIN, FORM_TSUNDERPLANE, FORM_PYROPE):
    s = run(["--god", "--battle", str(form), "--path", "fight"], 4000, what=f"battle {form}, fighting")
    check(s and s["mode"] == MODE_WORLD and s["kills"] >= 1, f"battle {form}: FIGHT did not win it: {s}")
for form, seq, gold in ((FORM_VULKIN, "2", 40), (FORM_PYROPE, "1,1", 45), (FORM_PYROPE2, "1,1,1@2,1@2", 90)):
    s = run(["--god", "--battle", str(form), "--path", "seq:" + seq], 9000, what=f"battle {form}, sparing")
    check(s and s["mode"] == MODE_WORLD and s["kills"] == 0 and s["gold"] == gold,
          f"battle {form}: the friendly way did not spare it: {s}")
for form in (FORM_MTT_QUIZ, FORM_MTT_COOK, FORM_LASERS):
    s = run(["--god", "--battle", str(form), "--path", "talk"], 3000, what=f"show {form}")
    check(s and s["mode"] == MODE_WORLD, f"show {form} did not end: {s}")
# the Royal Guards (no running from them): fought, or spared (02's armor cleaned twice, its green stars caught, a
# whisper to 01); a bomb of the news report (the dog), defused by Defuse or by FIGHT, in the DEFUSE ZONE
s = run(["--god", "--battle", str(FORM_RGUARDS), "--path", "fight"], 9000, what="the Royal Guards, fought")
check(s and s["mode"] == MODE_WORLD and s["kills"] == 2, f"the Royal Guards: FIGHT did not win it: {s}")
s = run(["--god", "--battle", str(FORM_RGUARDS), "--path", "seq:1@2,1@2,2@1"], 9000, what="the Royal Guards, spared")
check(s and s["mode"] == MODE_WORLD and s["kills"] == 0 and s["gold"] == 100,
      f"the Royal Guards: the friendly way did not spare them: {s}")
for how in ("act:1", "fight"):
    s = run(["--god", "--battle", str(FORM_BOMB), "--path", how], 9000, what=f"a bomb, {how}")
    check(s and s["mode"] == MODE_WORLD and s["kills"] == 0, f"a bomb: {how} did not defuse it: {s}")
# Muffet (no running from her): spared once her spiders send word, her 100G; Mettaton after the tile puzzle: ACT
# Yellow, and the yellow SOUL's shots drain his power
s = run(["--god", "--battle", str(FORM_MUFFET), "--path", "spare"], 9000, what="Muffet, spared")
check(s and s["mode"] == MODE_WORLD and s["kills"] == 0 and s["gold"] == 100, f"Muffet was not spared: {s}")
s = run(["--god", "--battle", str(FORM_MTT_TILES), "--path", "act:2"], 4000, what="Mettaton's yellow button")
check(s and s["mode"] == MODE_WORLD and s["kills"] == 0, f"Mettaton's fight did not end with the yellow button: {s}")

# the shop in Snowdin: a Cinnamon Bunny for 25G; Papyrus's date: his phone number
s = run(["--name", "Frisk", "--noenc", "--room", str(ROOMS["SNOWTOWN"]), "--at", "446,1092", "--gold", "100", "--keys",
         "2-24:up,60:z,90:down,100:down,110:down,120:z,140:z,170:x,190:down,200:down,210:down,220:z"], 400, what="the shop")
check(s and s["gold"] == 75 and s["items"][0] == 16, f"the shop did not sell a Cinnamon Bunny: {s}")
s = run(["--name", "Frisk", "--noenc", "--room", str(ROOMS["PSHOUSE"]), "--at", "2436,914", "--flag",
         f"{FLAGS['F_PAPYRUS']}=1", "--flag", f"{FLAGS['F_PAP_DATE']}=1", "--path",
         "go:2300,918;l;w5;l;w5;l;w5;z;pick:0;talk;w60"], 2500, what="Papyrus's date")
check(s and flag(s, "PAP_DATE") == 2, f"the date did not end with Papyrus's number: {s}")

# the leaf pile's SAVE, then Continue from the title
d = tempfile.mkdtemp(prefix="undertale_check_")
s = run(["--name", "Frisk", "--noenc", "--room", "8", "--at", "5012,4498", "--path", "u;z;w20;z;w30;x"], 300, saves=d,
        what="saving at the leaf pile")
check(os.path.exists(d + "/undertale.sav") and os.path.exists(d + "/undertale_saves.py"), "the SAVE Point did not save")
s = run(["--keys", "30:z,60:z,90:z,120:z"], 300, saves=d, what="Continue")
check(s and s["mode"] == MODE_WORLD and s["room"] == 8, f"Continue did not go back to the leaf pile: {s}")
# Home keeps the moment; the next start is back there
s = run(["--keys", "30:z,60:z,90:z,120:z,130-170:right,200:home"], 300, saves=d, what="Home")
s = run([], 30, saves=d, what="back after Home")
check(s and s["mode"] == MODE_WORLD and s["room"] == 8 and s["x"] > 5000, f"Home did not bring the game back: {s}")
shutil.rmtree(d, ignore_errors=True)

# the whole Ruins, the friendly way: Flowey, Toriel's puzzles, the candy, the rocks, Napstablook, the switches,
# Toriel's home, her fight spared, Flowey's judgement, the door out
if not QUICK:
    s = run(["--name", "Frisk", "--noenc", "--god", "--path-file", os.path.join(HERE, "ruins.path")], 40000,
            what="the Ruins")
    check(s and s["mode"] == MODE_WORLD and s["room"] == ROOMS["SNOWDIN1"], f"the Ruins' door did not lead to Snowdin: {s}")
    if s:
        for name, v in (("FLOWEY_MET", 1), ("DUMMY", 1), ("RUINS5_DONE", 1), ("RUINS6_DONE", 1), ("PHONE", 1),
                        ("RUINS9_ROCK", 1), ("RUINS11_ROCK", 5), ("NAPSTA", 1), ("ONESWITCH", 1), ("ROTATE4", 1),
                        ("TORIEL_HOME", 1), ("HOME_TOUR", 1), ("TORIEL_BASEMENT", 1), ("BASEMENT_TALK", 3),
                        ("TORIEL_FIGHT", 1), ("FLOWEY_END", 1), ("RUINS_EXIT", 1)):
            check(flag(s, name) == v, f"the Ruins: F_{name} is {flag(s, name)}, not {v}")
        check(s["kills"] == 0, f"the Ruins: {s['kills']} killed on the friendly way")
        check(s["items"].count(1) == 2, f"the Ruins: not the two pieces of Monster Candy: {s['items']}")

# the whole of Snowdin, the friendly way: Sans, Papyrus's puzzles (the maze, the X/O ones, the ice, the tiles),
# Doggo, Dogamy and Dogaressa, Greater Dog, the Gauntlet, the town, Papyrus spared, the way on
if not QUICK:
    s = run(["--name", "Frisk", "--noenc", "--god", "--room", str(ROOMS["SNOWDIN1"]), "--at", "34,392", "--path-file",
             os.path.join(HERE, "snowdin.path")], 30000, what="Snowdin")
    check(s and s["mode"] == MODE_WORLD and s["room"] == ROOMS["WF1"], f"Snowdin did not lead on to Waterfall: {s}")
    if s:
        for name, v in (("BRANCH", 1), ("SANS_MET", 1), ("PAP_SENTRY", 1), ("DOGGO", 1), ("ELECMAZE", 2),
                        ("WORDSEARCH", 1), ("SNOWPUZZLE", 1), ("DOGI", 1), ("XOXO1", 1), ("XOXO2", 1), ("XOXO3", 1),
                        ("PAP_XOXO1", 2), ("TILES", 1), ("GREATERDOG", 1), ("GAUNTLET", 1), ("PAPYRUS", 1),
                        ("PAP_DATE", 1), ("TOWN_HOUSE", 1), ("SNOW_EXIT", 1)):
            check(flag(s, name) == v, f"Snowdin: F_{name} is {flag(s, name)}, not {v}")
        check(s["kills"] == 0, f"Snowdin: {s['kills']} killed on the friendly way")

# Waterfall, the friendly way: Monster Kid, Grillby's with Sans, the tutu, Papyrus's report, the bridge seeds (and
# the secret one to the quiche), the wishing room, Undyne's spears, the telescope, the punch card, the marsh (the
# ballet shoes), Onionsan, the piano and the artifact, Shyren, the umbrellas, Monster Kid in the rain, the bridge cut,
# the garbage dump, Mad Dummy, Napstablook's house, the snail farm, Gerson, the mushroom maze and Temmie Village, the
# dead end, Monster Kid helped up on the chasm's bridge, Undyne's fight (fled), her chase, Papyrus's call, and Hotland,
# where she falls in the heat and gets the cooler's water
if not QUICK:
    s = run(["--name", "Frisk", "--noenc", "--god", "--room", str(ROOMS["WF1"]), "--at", "30,1480", "--path-file",
             os.path.join(HERE, "waterfall.path")], 60000, what="Waterfall")
    check(s and s["mode"] == MODE_WORLD and s["room"] == ROOMS["HT_LABFRONT"], f"Waterfall did not end in Hotland: {s}")
    if s:
        for name, v in (("WF_MK", 1), ("GRILLBYS", 1), ("WF_TUTU", 1), ("WF_UNDYNE1", 1), ("SEEDS1", 1), ("SEEDS2", 1),
                        ("SEEDS3", 1), ("WF_QUICHE", 1), ("CHASE1", 1), ("WF_TELESCOPE", 1), ("WF_PUNCH", 1), ("WF_BALLET", 1),
                        ("WF_ONION", 2), ("PIANO", 1), ("WF_ARTIFACT", 3), ("SHYREN", 1),
                        ("UMBRELLA", 3), ("MK_RAIN", 2), ("WF_BRIDGE", 1), ("WF_DUMP", 1), ("WF_COOLER", 2),
                        ("MAD_DUMMY", 1), ("NAPSTA_HOME", 2), ("TEM_VILLAGE", 1), ("DEADEND", 1), ("MK_CHASM", 2),
                        ("UNDYNE", 4), ("WATER_CUP", 2), ("PAP_NEON", 1)):
            check(flag(s, name) == v, f"Waterfall: F_{name} is {flag(s, name)}, not {v}")
        check(s["kills"] == 0, f"Waterfall: {s['kills']} killed on the friendly way")
        for item in (ITEM_OLD_TUTU, ITEM_BALLET_SHOES, ITEM_QUICHE, ITEM_PUNCH_CARD, ITEM_DOG_RESIDUE, ITEM_ASTRONAUT_FOOD):
            check(item in s["items"], f"Waterfall: item {item} not picked up: {s['items']}")

# the Legendary Artifact (issue #216): the first try fills the inventory with Annoying Dogs; one deployed there absorbs
# the artifact and leaves Dog Residue; with the dogs gone (out of the room), the artifact is taken
art = ["--name", "Frisk", "--noenc", "--god", "--room", str(ROOMS["WF_PIANO"]), "--at", "10140,240",
       "--flag", "%d=1" % FLAGS["F_PIANO"]]
s = run(art + ["--path", "go:10150,162;u;z;talk"], 1500, what="the artifact, the first try")
check(s and s["items"] == [ITEM_ANNOYING_DOG] * 8 and flag(s, "WF_ARTIFACT") == 2, f"the artifact: no dogs: {s and s['items']}")
s = run(art + ["--path", "go:10150,162;u;z;talk;c;z;z;z;talk;u;z;talk"], 2500, what="the artifact, a dog deployed")
check(s and s["items"] == [ITEM_DOG_RESIDUE] + [0] * 7 and flag(s, "WF_ARTIFACT") == 3,
      f"the artifact: the dog did not absorb it: {s and s['items']}")
s = run(art + ["--flag", "%d=2" % FLAGS["F_WF_ARTIFACT"], "--path", "go:10150,162;u;z;talk"], 1500,
        what="the artifact, the dogs gone")
check(s and s["items"][0] == ITEM_ARTIFACT and flag(s, "WF_ARTIFACT") == 1, f"the artifact not taken: {s and s['items']}")

# every room: all there is to talk to (signs, glyphs on the walls, the townsfolk) reached from a floor Frisk walks on,
# facing it (issue #216: Waterfall's lore panels and signs out of reach)
with tempfile.NamedTemporaryFile("w", suffix=".txt", delete=False) as f:
    f.write("".join("room %d\n" % r for r in sorted(ROOMS.values())))
p = subprocess.run([PLAY, "--saves", tempfile.mkdtemp(prefix="undertale_check_"), "--name", "Frisk", "--rooms", f.name],
                   capture_output=True, text=True, env=ENV, timeout=900)
os.remove(f.name)
m = re.search(r"ROOMS (\d+) checked, (\d+) failed", p.stdout)
check(m and int(m.group(1)) == len(ROOMS) and m.group(2) == "0",
      "rooms: " + "\n".join(l for l in p.stdout.splitlines() if l.startswith("ROOMS")) + p.stderr[-1000:])

# Hotland's first floor: Alphys and the quiz, the noodles, the belts, the vents, the lasers, both shooting puzzles, the
# cooking show, up the elevator to the second floor
if not QUICK:
    s = run(["--name", "Frisk", "--noenc", "--god", "--room", str(ROOMS["HT_LABFRONT"]), "--at", "1000,6230",
             "--path-file", os.path.join(HERE, "hotland.path")], 16000, what="Hotland")
    check(s and s["mode"] == MODE_WORLD and s["room"] == ROOMS["HT_HOTDOG"],
          f"Hotland did not end up the elevator, on the second floor: {s}")
    if s:
        for name, v in (("HT_LAB", 2), ("HT_NOODLES", 1), ("HT_CALL_LASER", 1), ("HT_CALL_HUB", 1), ("HT_CALL_ISLE", 1),
                        ("HT_CALL_CORE", 1), ("HT_PUZ_R", 1), ("HT_PUZ_L", 1), ("HT_BLUE_OFF", 1), ("HT_COOK", 1),
                        ("HT_FLOOR1", 1)):
            check(flag(s, name) == v, f"Hotland: F_{name} is {flag(s, name)}, not {v}")
        check(ITEM_INSTANT_NOODLES in s["items"], f"Hotland: the noodles not taken: {s['items']}")

# Hotland's second floor: Heats Flamesman, Sans's hot dog, the apron, the Art Club's room, the belt and vent puzzles,
# the Bad Opinion Zone, the Royal Guards spared, Mettaton's news report, up the elevator to the third floor
if not QUICK:
    s = run(["--name", "Frisk", "--noenc", "--god", "--gold", "100", "--room", str(ROOMS["HT_HOTDOG"]), "--at", "6966,2380",
             "--path-file", os.path.join(HERE, "hotland2.path")], 13000, what="Hotland's second floor")
    check(s and s["mode"] == MODE_WORLD and s["room"] == ROOMS["HT_L3"],
          f"Hotland's second floor did not end up the elevator, on the third floor: {s}")
    if s:
        for name, v in (("HT_HEATS", 1), ("HT_APRON", 1), ("HT_CALL_BELT", 1), ("HT_BELT", 1), ("HT_VENTPUZ", 1),
                        ("HT_CALL_BOZ", 1), ("HT_GUARDS", 1), ("HT_NEWS", 2), ("HT_CALL_CORE2", 1), ("HT_FLOOR2", 1)):
            check(flag(s, name) == v, f"Hotland's second floor: F_{name} is {flag(s, name)}, not {v}")
        check(s["kills"] == 0, f"Hotland's second floor: {s['kills']} killed on the friendly way")
        check(s["gold"] == 170, f"Hotland's second floor: the hot dog or the guards' gold went wrong: {s['gold']}")
        for item in (ITEM_HOT_DOG, ITEM_STAINED_APRON):
            check(item in s["items"], f"Hotland's second floor: item {item} not picked up: {s['items']}")

# Hotland's third floor: the bake sale, the vent islands, both shooting puzzles (the Diamond Boys, Alphys's anime),
# the belts past the drones, Muffet spared, Mettaton's musical, the tile puzzle and his fight, the crossroads (a Nice
# Cream, Heats Flamesman), up the stairs to the MTT Resort
if not QUICK:
    s = run(["--name", "Frisk", "--noenc", "--god", "--gold", "100", "--room", str(ROOMS["HT_L3"]), "--at", "3770,842",
             "--path-file", os.path.join(HERE, "hotland3.path")], 14000, what="Hotland's third floor")
    check(s and s["mode"] == MODE_WORLD and s["room"] == ROOMS["RES_OUTSIDE"],
          f"Hotland's third floor did not end at the MTT Resort: {s}")
    if s:
        for name, v in (("HT_CALL_HUB3", 1), ("HT3_PUZ_N", 1), ("HT3_PUZ_S", 1), ("HT_ANIME", 1), ("HT_MUFFET", 1),
                        ("HT_MUSICAL", 1), ("HT_TILES3", 1), ("HT_HEATS3", 1), ("HT_FLOOR3", 1)):
            check(flag(s, name) == v, f"Hotland's third floor: F_{name} is {flag(s, name)}, not {v}")
        check(s["kills"] == 0, f"Hotland's third floor: {s['kills']} killed on the friendly way")
        check(s["gold"] == 188, f"Hotland's third floor: Muffet's gold or the Nice Cream went wrong: {s['gold']}")
        check(ITEM_NICE_CREAM in s["items"], f"Hotland's third floor: no Nice Cream: {s['items']}")

# the MTT Resort: dinner with Sans, a Starfait from Burgerpants, a night in the hotel (200G), room service and the
# dog's door (nothing to give), through the door north into the CORE
if not QUICK:
    s = run(["--name", "Frisk", "--noenc", "--god", "--gold", "400", "--room", str(ROOMS["HT_CROSS3"]), "--at", "7550,420",
             "--path-file", os.path.join(HERE, "resort.path")], 5000, what="the MTT Resort")
    check(s and s["mode"] == MODE_WORLD and s["room"] == ROOMS["CORE_APPROACH"], f"the MTT Resort did not end in the CORE: {s}")
    if s:
        for name, v in (("HT_FLOOR3", 1), ("RES_SANS", 1), ("RES_ROOM", 2), ("RES_TEA", 0), ("RES_DOG", 0)):
            check(flag(s, name) == v, f"the MTT Resort: F_{name} is {flag(s, name)}, not {v}")
        check(s["gold"] == 140, f"the MTT Resort: the Starfait or the room went wrong: {s['gold']}")
        check(ITEM_STARFAIT in s["items"], f"the MTT Resort: no Starfait: {s['items']}")

# the CORE: Madjick and Knight Knight spared, the switch's lasers, the bridge's power, the sage's puzzle, a Glamburger,
# Mettaton turned round and his show a hit, Alphys by the last elevator, up to New Home
if not QUICK:
    s = run(["--name", "Frisk", "--noenc", "--god", "--room", str(ROOMS["RES_LOBBY"]), "--at", "1210,160",
             "--path-file", os.path.join(HERE, "core.path")], 24000, what="the CORE")
    check(s and s["mode"] == MODE_WORLD and s["room"] == ROOMS["NH_TOWN"], f"the CORE did not end up in New Home: {s}")
    if s:
        for name, v in (("CORE_IN", 1), ("CORE_MADJICK", 1), ("CORE_KNIGHT", 1), ("CORE_SWITCH", 1), ("CORE_POWER", 2),
                        ("CORE_SAGE", 1), ("CORE_FIELD", 1), ("CORE_TRASH1", 1), ("CORE_END", 1), ("CORE_MTT", 1),
                        ("CORE_HALL", 1), ("CORE_DONE", 1)):
            check(flag(s, name) == v, f"the CORE: F_{name} is {flag(s, name)}, not {v}")
        check(s["kills"] == 0, f"the CORE: {s['kills']} killed on the friendly way")
        check(s["gold"] == 130, f"the CORE: Madjick's and Knight Knight's gold went wrong: {s['gold']}")
        check(ITEM_GLAMBURGER in s["items"], f"the CORE: no Glamburger from the trash can: {s['items']}")

# New Home: the keys and the chain, the presents, the story, Sans's judgment, Asgore in the throne room; at the
# barrier, Asgore beaten (FIGHT) and spared; Omega Flowey (the six SOULs called on); Flowey spared; the end
if not QUICK:
    s = run(["--name", "Frisk", "--noenc", "--god", "--room", str(ROOMS["NH_TOWN"]), "--at", "90,4545",
             "--path-file", os.path.join(HERE, "newhome.path")], 26000, what="New Home")
    check(s and s["mode"] == MODE_END, f"New Home did not reach the end: {s}")
    if s:
        for name, v in (("NH_KEY1", 1), ("NH_KEY2", 1), ("NH_CHAIN", 1), ("NH_DAGGER", 1), ("NH_LOCKET", 1),
                        ("NH_STORY", 11), ("NH_SANS", 1), ("NH_THRONE", 1), ("NH_ASGORE", 1), ("NH_FLOWEY", 1),
                        ("NH_DONE", 1)):
            check(flag(s, name) == v, f"New Home: F_{name} is {flag(s, name)}, not {v}")
        check(s["kills"] == 0, f"New Home: {s['kills']} killed on the friendly way")
        for item in (ITEM_WORN_DAGGER, ITEM_HEART_LOCKET):
            check(item in s["items"], f"New Home: item {item} not found: {s['items']}")

if failures:
    print("\n".join("FAIL " + f for f in failures))
    sys.exit(1)
print("all checks passed")
