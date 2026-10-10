#!/usr/bin/env python3
"""Checks of Celeste on a computer (make test): the host build, with
AddressSanitizer and UBSan, through the menus, the saves and every room.

    python3 tests/check.py build/play src/data.bin [--quick]

--quick skips playing every room (the longest part)."""
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
DATA = sys.argv[2] if len(sys.argv) > 2 else "src/data.bin"
QUICK = "--quick" in sys.argv
failures = []
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")


def run(args, frames, keys="", saves=None, env=None, what=""):
    """Plays (from a save folder, a new one if None); returns its output (the session line)."""
    own = saves is None
    if own:
        saves = tempfile.mkdtemp(prefix="celeste_check_")
    e = dict(ENV)
    e.update(env or {})
    p = subprocess.run([PLAY, DATA, "--saves", saves, "--frames", str(frames), "--keys", keys] + args,
                       capture_output=True, text=True, env=e, timeout=600)
    if own:
        shutil.rmtree(saves, ignore_errors=True)
    if p.returncode or "runtime error" in p.stderr or "AddressSanitizer" in p.stderr:
        failures.append(f"{what or args}: crashed or a sanitizer error\n{p.stderr[-2000:]}")
    return p.stdout


def session(out):
    m = re.search(r"session: chapter (\d+) area (\d+) mode (\d+) deaths (\d+) time (\d+)", out)
    return tuple(int(v) for v in m.groups()) if m else None


def check(cond, msg):
    if not cond:
        failures.append(msg)


# the first time: the key sheet, the title, the main menu, Climb, the chapter, Start: the Prologue
MENU_TO_PLAY = "40-41:o,100-101:o,160-161:o,220-221:o,280-281:o"
d = tempfile.mkdtemp(prefix="celeste_check_")
out = run([], 500, MENU_TO_PLAY + ",400-450:r", saves=d, what="the menus to the Prologue")
s = session(out)
check(s and s[1] == 0 and s[4] > 100, f"the menus did not start the Prologue: {s}")

# Home saves and leaves; Climb then shows Continue, which carries on the same run
out = run([], 700, MENU_TO_PLAY + ",600-601:h", saves=d, what="Home in the Prologue")
check(os.path.exists(d + "/celeste.sav") and os.path.exists(d + "/celeste_saves.py"), "Home did not save")
t0 = session(out)[4] if session(out) else 0
out = run([], 300, "40-41:o,100-101:o,160-161:o", saves=d, what="Continue after Home")
s = session(out)
check(s and s[4] > t0, f"Continue did not carry on the run ({t0} frames before): {s}")

# the copy in the Python script brings the save back when it is gone
os.remove(d + "/celeste.sav")
out = run([], 10, saves=d, what="the save from its copy")
s2 = session(out)
check(s2 and s2[4] >= t0, f"the save did not come back from celeste_saves.py: {s2}")
shutil.rmtree(d, ignore_errors=True)

# a 1.6.0 save: its keys on backspace (pause now) go to Back, as its defaults (dash and talk on backspace) did
def fnv(b):
    h = 2166136261
    for x in b:
        h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


for old, new in (([4, 17, 16, 17], "4 5 16 5"), ([17, 16, 4, 29], "5 16 4 29")):
    d = tempfile.mkdtemp(prefix="celeste_check_")
    v1 = bytearray(1552)
    struct.pack_into("<IHH", v1, 0, 0x43454C53, 1, 1552)
    v1[1071] = 1   # the key sheet seen
    v1[1100:1104] = bytes(old)
    struct.pack_into("<I", v1, 1544, fnv(v1[:1544]))
    open(d + "/celeste.sav", "wb").write(v1)
    out = run([], 10, saves=d, what="a 1.6.0 save's keys")
    check("keys " + new in out, f"the keys {old} of a 1.6.0 save did not become {new}: {out[-60:]}")
    shutil.rmtree(d, ignore_errors=True)

# the cheat code in the Prologue's room -1 (left, right, journal, grab, up, up, down, left, grab, confirm): cheat
# mode and every chapter; then the panel's room picker (6A, its 13th room)
d = tempfile.mkdtemp(prefix="celeste_check_")
run(["--chapter", "0", "--room", "-1", "--nowipe"], 400,
    "20-21:l,30-31:r,40-41:n,50-51:g,60-61:u,70-71:u,80-81:d,90-91:l,100-101:g,110-111:o", saves=d, what="the cheat code")
sav = open(d + "/celeste.sav", "rb").read() if os.path.exists(d + "/celeste.sav") else b""
check(len(sav) == 1680 and sav[1672] == 1 and sav[1066] == 10, "the cheat code did not turn cheat mode on")
out = run([], 700, "40-41:o,100-101:o,160-161:o," + ",".join("%d-%d:r" % (f, f + 1) for f in range(200, 360, 30))
          + ",380-381:o,420-421:u,440-441:o,470-471:d,490-491:d,510-511:r,540-541:o", saves=d, what="the room picker")
check("room 07 " in out and "area 6 mode 0" in out, f"the room picker did not start 6A in room 07: {out[-200:]}")
shutil.rmtree(d, ignore_errors=True)

# the pause menu: Retry is a death
out = run(["--chapter", "1", "--room", "1"], 300, "200-201:p,215-216:d,230-231:o", what="Retry from the pause menu")
s = session(out)
check(s and s[3] == 1, f"Retry did not count a death: {s}")

# 3A's start room (s3): onto the porch roof, up the posts with spikes on top (climbing on stops below them, as the
# game's LedgeBlocker makes it), over them to the key, then through the locked door into the lobby
out = run(["--chapter", "7", "--room", "s3", "--nowipe"], 780,
          "10-90:r,58-70:j,100-110:j,108-112:u,108-109:x,116-133:r,150-160:j,150-182:l,150-345:g,184-249:u,250-262:j,"
          "250-300:l,264-265:x,302-330:u,332-344:j,332-346:l,400-415:l,440-760:r,523-535:j", what="3A's key and locked door")
check("room 0x-a " in out and " deaths 0 " in out, f"3A's start room was not crossed with its key: {out[-160:]}")

# 5A's c-10: its two touch switches (below the middle cracked block, on top of it) open the temple gate (TouchSwitches)
# up on the right, the way to c-12
out = run(["--chapter", "13", "--room", "c-10", "--nowipe"], 520,
          "10-90:r,18-24:j,110-124:j,126-130:u,126-127:x,136-158:r,180-183:l,190-204:j,206-210:u,206-207:x,216-235:r,"
          "300-500:r,306-316:j,314-315:x,314-460:g,330-460:u", what="5A's c-10")
check("room c-12 " in out and " deaths 0 " in out, f"5A's c-10 was not crossed (its touch switches' gate): {out[-160:]}")

# 6A's boss-00: after the intro (its dialog, OK pressed through), Badeline hit twice dives through the floor, a dash block only
# she breaks, and the player follows her down into boss-01 (the second hit: the player put next to her, out of her shots)
out = run(["--chapter", "16", "--room", "boss-00", "--nowipe", "--at", "352,144", "--tp", "1680:546,112"], 2100,
          ",".join(f"{f}-{f + 2}:o" for f in range(60, 1450, 10)) + ",1500-1560:r,1505-1515:j,1522-1523:x,1760-1830:r,1762-1775:j",
          what="6A's boss-00")
check("room boss-01 " in out, f"6A's boss-00: Badeline didn't break the floor: {out[-160:]}")

# 7A's ascents: Badeline's boosts up each section's last room (the player put at each of her places, kept at the next
# while she flies there), her last one the summit launch, its cutscene (OK pressed through), the player up and out
# the top, landing in the next section's first room (e-13: her 9 places, the most)
ASCENTS = {"a-06": ("b-00", [(152, 696), (256, 576), (48, 520), (160, 408)]),
           "b-09": ("c-00", [(272, 1072), (88, 1048), (160, 664)]),
           "c-09": ("d-00", [(264, 888), (32, 768), (160, 616)]),
           "d-11": ("e-00b", [(160, 976), (160, 760), (40, 728), (280, 720), (160, 648)]),
           "e-13": ("f-00", [(272, 1352), (296, 1216), (56, 1208), (24, 1104), (288, 1000), (264, 848), (48, 840),
                             (64, 704), (160, 624)]),
           "f-11": ("g-00", [(80, 1672), (264, 1616), (264, 1320), (160, 528)])}
for room, (above, places) in ASCENTS.items():
    t, args = 30, ["--chapter", "19", "--room", room, "--nowipe"]
    for k, (x, y) in enumerate(places):
        args += ["--tp", f"{t}:{x},{y + 4}"]
        if k + 1 < len(places):
            arrive = t + 30 + math.ceil(min(3, math.dist(places[k], places[k + 1]) / 320) * 60)
            args += ["--tp", f"{t + 40}-{arrive}:{places[k + 1][0]},{places[k + 1][1] + 4}"]
            t = arrive
    out = run(args, t + 2400, ",".join(f"{f}-{f + 2}:o" for f in range(t + 60, t + 2200, 12)), what=f"7A's {room} ascent")
    check(f"room {above} " in out and " deaths 0 " in out and "state 0 " in out,
          f"7A's {room}: the ascent didn't land in {above}: {out[-160:]}")

# a chapter's end: its screen, then the chapter select
out = run(["--chapter", "1", "--room", "1"], 500, "400-401:o", env={"COMPLETE_AT": "60"}, what="a chapter's end")

# every room, moving and dashing about, drawn every frame
if not QUICK:
    rooms = subprocess.run([PLAY, DATA, "--list"], capture_output=True, text=True).stdout.split("\n")
    for line in filter(None, rooms):
        c, r = line.split(" ", 1)
        run(["--chapter", c, "--room", r, "--draw", "1"], 150, "10-140:r,30-36:j,50:x,70-90:l,80-86:j,100:x", what=f"room {c} {r}")

print("\n".join(failures) if failures else "all good")
sys.exit(1 if failures else 0)
