#!/usr/bin/env python3
"""Checks of Hill Climb Racing on a computer (make test): the host build, with AddressSanitizer and UBSan, through
the menus, buying, every stage's ground, every stage with every vehicle, each vehicle's numbers, how often the ground
is worked out, the ends of a run (drawn too), the saves (and the old versions' carried over) and random keys.

    python3 tests/check.py build/play [--quick]"""
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

PLAY = sys.argv[1] if len(sys.argv) > 1 else "build/play"
QUICK = "--quick" in sys.argv
ENV = dict(os.environ, ASAN_OPTIONS="detect_leaks=0", UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
failures = []
STAGES = ["Countryside", "Desert", "Arctic", "Highway", "Cave", "Moon", "Mars", "Alien Planet", "Arctic Cave", "Forest",
          "Mountain", "Mudpool", "Volcano"]
VEHICLES = ["Hill Climber", "Motocross Bike", "Monster Truck", "Quad Bike", "Race Car", "Rally Car", "Dune Buggy", "Tank",
            "Tractor", "Tourist Bus", "Super Diesel 4x4", "Snow Mobile", "Truck"]
NEW_STAGES, NEW_VEHICLES = range(10, 13), range(8, 13)
MODE_RUN, MODE_END = 4, 6


def play(args, saves):
    return subprocess.run([PLAY, "--saves", saves, "--out", os.path.join(saves, "shots")] + args, capture_output=True,
                          text=True, env=ENV, timeout=900)


def run(args, saves, what):
    p = play(args, saves)
    if p.returncode or "runtime error" in p.stderr or "AddressSanitizer" in p.stderr:
        failures.append(f"{what}: crashed or a sanitizer error\n{p.stderr[-2000:]}")
        return None
    m = re.search(r"END mode (\d+) dist (-?\d+) coins (\d+) total (\d+) over (\d) end (\d) level (\d+) flips (\d+) "
                  r"saved (\d+) running (\d)", p.stdout)
    if not m:
        failures.append(f"{what}: no end state\n{p.stdout[-500:]}")
        return None
    k = ["mode", "dist", "coins", "total", "over", "end", "level", "flips", "saved", "running"]
    return dict(zip(k, map(int, m.groups())))


def check(cond, what):
    if not cond:
        failures.append(what)
    return cond


def fresh():
    return tempfile.mkdtemp(prefix="hillclimb_check_")


def drive(st, v, frames=2400, levels="6,6,6,6"):
    """A player driving the stage with the vehicle: no crash, and it gets somewhere."""
    d = fresh()
    s = run(["--run", f"{st},{v}", "--levels", levels, "--auto", "--frames", str(frames)], d,
            f"{STAGES[st]} with the {VEHICLES[v]}")
    shutil.rmtree(d, ignore_errors=True)
    return st, v, s


# the version 3 save (src/game.h): offsets in hillclimb.sav, after its 8-byte header
SAVE3 = 8 + 444
OFF3 = dict(version=8, coins=12, vehicles=16, stages=18, level=20, best=72, vehicle=410, stage=411, seen_help=412,
            runs=416, meters=420, flips=424, best_air=428, earned=432, stage_level=436)


def old_save(version, coins, levels, best00, stage_level0):
    """A save as versions 1 and 2 wrote it: 8 vehicles, 10 stages."""
    body = struct.pack("<IHH", coins, 0xFF, 0x3FF)
    lv = bytearray(32)
    for v in range(8):
        for u in range(4):
            lv[v * 4 + u] = levels.get((v, u), 1)
    body += bytes(lv)
    best = [0] * 80
    best[0] = best00
    best[9 * 8 + 7] = 4321   # the Forest with the Tank
    body += struct.pack("<80H", *best)
    body += bytes([7, 9, 1, 0]) + struct.pack("<5I", 12, 3456, 7, 1500, 99999)
    if version == 2:
        sl = bytearray(10)
        sl[0], sl[9] = stage_level0, 5
        body += bytes(sl) + b"\0\0"
    return b"HCR1" + struct.pack("<I", len(body)) + body


def main():
    # every stage's ground: finite, bridges and pits apart, coins and fuel cans over the ground
    for st in range(len(STAGES)):
        p = subprocess.run([PLAY, "--ground", str(st)], capture_output=True, text=True, env=ENV, timeout=300)
        check(p.returncode == 0 and "GROUND ok" in p.stdout, f"{STAGES[st]}'s ground: {p.stdout.strip()} {p.stderr[-500:]}")
        if st == 11 or st == 12:
            check(" pits 0 " not in p.stdout, f"{STAGES[st]} has no pits")

    # each vehicle's numbers at level 1 and at its last levels: the game's (PlayerCar*'s constructors)
    # speed, torque, tires, hz, damping, turbo cap, rate, 4WD, air, mass, wheels, soft tire balls; fuel
    expect = {
        8: ([15 * 1.05, 20, 0.8 * 1.15, 5, 1.35, 0.5 * 1.15, 0.3 * 1.1, 0.5, 30, None, 2, 16], 1.2),
        9: ([15 * 1.05, 10, 0.75 * 1.115, 5, 0.2 * 1.11, 0.5 * 1.15, 0.3, 0.5, 90, None, 2, 0], 1.5 - 1.1 / 26),
        10: ([17 * 1.05, 20, 0.95 * 1.115, 3, 0.2 * 1.11, 0.5 * 1.15, 0.5, 0.5, 45, None, 2, 0], 1.5 - 0.9 / 21),
        11: ([45.9 * 1.048 * 0.55, 6, 0.5 * 1.1612, 3, 1 - 0.9, 0.87 * 1.14, 3.33, 0.5, 3.5, None, 7, 0], 1.5 - 0.9 / 21),
        12: ([15 * 1.05, 10, 0.75 * 1.115, 4, 0.2 * 1.11, 0.5 * 1.15, 0.3, 0.5, 90, None, 3, 0], 1.5 - 1.1 / 26),
    }
    for v, (want, fuel) in expect.items():
        p = subprocess.run([PLAY, "--numbers", str(v), "--levels", "1,1,1,1"], capture_output=True, text=True, env=ENV)
        got = [float(x) for x in p.stdout.split()[1:13]] if p.stdout.startswith("NUMBERS") else []
        ok = len(got) == 12 and all(w is None or abs(g - w) <= 1e-4 * max(1, abs(w)) for g, w in zip(got, want))
        ok = ok and abs(float(p.stdout.split()[-1]) - fuel) < 1e-4
        check(ok, f"the {VEHICLES[v]}'s numbers: {p.stdout.strip()} (want {want}, fuel {fuel})")
        check(len(got) == 12 and 0.5 < got[9] < 20, f"the {VEHICLES[v]}'s chassis weighs {got[9] if got else '?'}")
    # at the last levels: the engine's, suspension's and tires' growth
    last = {8: "16,15,20,15", 9: "15,15,20,25", 10: "15,15,20,20", 11: "17,15,20,20", 12: "15,15,20,25"}
    tops = {8: (15 * 1.05 ** 16, 0.8 * 1.15 ** 15), 9: (15 * 1.05 ** 15, 0.75 * 1.115 ** 20),
            10: (17 * 1.05 ** 15, 0.95 * 1.115 ** 20), 11: (45.9 * 1.048 ** 17 * 0.55, 0.5 * 1.1612 ** 20),
            12: (15 * 1.05 ** 15, 0.75 * 1.115 ** 20)}
    for v, lv in last.items():
        p = subprocess.run([PLAY, "--numbers", str(v), "--levels", lv], capture_output=True, text=True, env=ENV)
        got = [float(x) for x in p.stdout.split()[1:13]] if p.stdout.startswith("NUMBERS") else []
        check(len(got) == 12 and abs(got[0] - tops[v][0]) < 1e-3 * tops[v][0] and abs(got[2] - tops[v][1]) < 1e-3 * tops[v][1],
              f"the {VEHICLES[v]} at its last levels: {p.stdout.strip()}")
    # the Snow Mobile's tracks grip three times on snow, 1.6 times on the Alien Planet
    for st, k in ((2, 3), (8, 3), (7, 1.6)):
        p = subprocess.run([PLAY, "--numbers", "11", "--levels", "1,1,1,1", "--run", f"{st},11"], capture_output=True,
                           text=True, env=ENV)
        got = float(p.stdout.split()[3]) if p.stdout.startswith("NUMBERS") else 0
        check(abs(got - 0.5 * 1.1612 * k) < 1e-4, f"the Snow Mobile's tracks on {STAGES[st]}: {got}")

    # the menus: title, garage, stage, how to drive, then the run; Home there keeps the run's coins and quits
    d = fresh()
    keys = "30:ok,70:ok,110:ok,150:ok,170-900:right,950:home"
    s = run(["--frames", "1000", "--keys", keys], d, "menus")
    if s and check(s["running"] == 0, "Home did not quit"):
        check(s["saved"] > 0 and s["saved"] == s["total"], f"the run's coins were not kept on Home: {s}")
        sav = os.path.join(d, "hillclimb.sav")
        check(os.path.exists(sav), "no hillclimb.sav")
        if os.path.exists(sav):
            b = open(sav, "rb").read()
            check(len(b) == SAVE3 and b[:4] == b"HCR1" and struct.unpack_from("<H", b, OFF3["version"])[0] == 3,
                  f"hillclimb.sav is not version 3 ({len(b)} bytes)")
        copy = open(os.path.join(d, "hillclimb_saves.py"), "rb").read() if os.path.exists(
            os.path.join(d, "hillclimb_saves.py")) else b""
        check(b"#>hillclimb.sav:" in copy, "hillclimb_saves.py does not hold the save")
        # installing apps again erases everything but scripts: the save comes back from the copy
        os.remove(os.path.join(d, "hillclimb.sav"))
        s2 = run(["--frames", "60", "--keys", "50:home"], d, "the save from its copy")
        if s2:
            check(s2["saved"] == s["saved"], f"the coins did not come back from hillclimb_saves.py: {s2['saved']}")
    shutil.rmtree(d, ignore_errors=True)

    # buying: with 10,000 coins, the Hill Climber's engine to level 2 (4,000), then its suspension (4,000)
    d = fresh()
    keys = "30:ok,70:down,90:ok,110:right,130:ok,150:right,170:ok,200:home"
    s = run(["--frames", "260", "--coins", "10000", "--keys", keys], d, "buying upgrades")
    if s:
        check(s["saved"] == 2000, f"two upgrades of 4,000 should leave 2,000 coins, not {s['saved']}")
    shutil.rmtree(d, ignore_errors=True)
    # the garage goes in the game's order: right from the Monster Truck is the Tractor (100,000), bought with OK, OK
    d = fresh()
    keys = "30:ok,60:right,80:right,100:right,130:ok,160:ok,200:home"
    s = run(["--frames", "260", "--coins", "150000", "--keys", keys], d, "buying the Tractor")
    if s:
        check(s["saved"] == 50000, f"the Tractor should cost 100,000 of 150,000: {s['saved']} left")
        b = open(os.path.join(d, "hillclimb.sav"), "rb").read()
        check(struct.unpack_from("<H", b, OFF3["vehicles"])[0] == 1 | 1 << 8, "the Tractor is not owned")
    shutil.rmtree(d, ignore_errors=True)

    # old saves (versions 1 and 2) carried over: coins, owned things, upgrades, bests, stats, levels kept; an upgrade
    # over its game's last level (the Rally Car's engine 14 of 13) brought down and paid back (2,000 x 14 x 14)
    for version in (1, 2):
        d = fresh()
        levels = {(5, 0): 14, (2, 2): 15, (0, 3): 10}
        open(os.path.join(d, "hillclimb.sav"), "wb").write(old_save(version, 123456, levels, 777, 3))
        s = run(["--frames", "40", "--keys", "30:home"], d, f"a version {version} save")
        if s:
            b = open(os.path.join(d, "hillclimb.sav"), "rb").read()
            u16 = lambda o: struct.unpack_from("<H", b, o)[0]
            u32 = lambda o: struct.unpack_from("<I", b, o)[0]
            lv = lambda v, u: b[OFF3["level"] + v * 4 + u]
            check(len(b) == SAVE3 and u16(OFF3["version"]) == 3, f"version {version} not saved again as 3")
            check(u32(OFF3["coins"]) == 123456 + 2000 * 14 * 14, f"version {version}: coins {u32(OFF3['coins'])}")
            check(u16(OFF3["vehicles"]) == 0xFF and u16(OFF3["stages"]) == 0x3FF, f"version {version}: owned things")
            check(lv(5, 0) == 13 and lv(2, 2) == 15 and lv(0, 3) == 10, f"version {version}: upgrades")
            check(u16(OFF3["best"]) == 777 and u16(OFF3["best"] + 2 * (9 * 13 + 7)) == 4321, f"version {version}: bests")
            check(b[OFF3["vehicle"]] == 7 and b[OFF3["stage"]] == 9 and u32(OFF3["runs"]) == 12
                  and u32(OFF3["meters"]) == 3456 and u32(OFF3["earned"]) == 99999, f"version {version}: the rest")
            if version == 2:
                check(b[OFF3["stage_level"]] == 3 and b[OFF3["stage_level"] + 9] == 5, "version 2: stage levels")
        shutil.rmtree(d, ignore_errors=True)
    # a broken save: a fresh start, no crash
    d = fresh()
    open(os.path.join(d, "hillclimb.sav"), "wb").write(b"HCR1" + struct.pack("<I", 444) + bytes(range(256)) * 2)
    s = run(["--frames", "40", "--keys", "30:home"], d, "a broken save")
    shutil.rmtree(d, ignore_errors=True)

    # every stage with every vehicle, a player driving: no crash, and it gets somewhere (the new ones on every stage
    # and every vehicle on the new stages always; the rest unless --quick)
    pairs = [(st, v) for st in range(len(STAGES)) for v in range(len(VEHICLES))
             if not QUICK or st in NEW_STAGES or v in NEW_VEHICLES]
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for st, v, s in pool.map(lambda p: drive(*p), pairs):
            if s:
                check(s["dist"] > 60, f"{STAGES[st]} with the {VEHICLES[v]}: only {s['dist']} m")
    # the new ones go on, well upgraded: 90 s on the new stages, and the new vehicles on stages they suit
    long = [(10, 2), (10, 10), (11, 8), (11, 9), (12, 10), (12, 0), (2, 11), (0, 12), (0, 9)]
    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for st, v, s in pool.map(lambda p: drive(*p, frames=5400, levels="12,12,12,10"), long):
            if s:
                check(s["dist"] > 400, f"90 s on {STAGES[st]} with the {VEHICLES[v]}: only {s['dist']} m")

    # the ground's formula (on the Volcano, sines of hundreds of radians: slow on the calculator) is worked out for new
    # ground only, not for every coin and fuel can at every step and drawing as in 1.16.0 (140 times a frame); the
    # player driving here asks for it twice a frame
    d = fresh()
    p = play(["--run", "12,8", "--levels", "12,12,12,10", "--auto", "--draw", "--frames", "3600"], d)
    m = re.search(r"EVALS (\d+)", p.stdout)
    check(m and int(m.group(1)) < 3600 * 4, f"the Volcano's ground worked out {m.group(1) if m else '?'} times in 3600 frames")
    shutil.rmtree(d, ignore_errors=True)

    # the ends of a run: Bill's neck (the bike flipped on the Moon, gas held) and an empty tank (reversing)
    d = fresh()
    s = run(["--run", "5,1", "--keys", "0-1500:right", "--frames", "1500"], d, "driver down")
    if s:
        check(s["mode"] == MODE_END and s["end"] == 0, f"the bike held at full gas on the Moon did not go down: {s}")
    shutil.rmtree(d, ignore_errors=True)
    d = fresh()
    s = run(["--run", "0,0", "--keys", "0-3200:left", "--frames", "3200"], d, "out of fuel")
    if s:
        check(s["mode"] == MODE_END and s["end"] == 1, f"reversing into the wall did not run out of fuel: {s}")
    shutil.rmtree(d, ignore_errors=True)

    # the end of a run, drawn: the bridges' planks (the Arctic Cave: the bike at full gas goes down just past the
    # second bridge) and a knocked tree (the Forest) as they were when the run's world went (they were read from it
    # after it was freed)
    for st, args in (("the Arctic Cave", ["--run", "8,1", "--keys", "0-2400:right", "--frames", "2600"]),
                     ("the Forest", ["--run", "9,1", "--keys", "0-1500:right", "--frames", "1700"])):
        d = fresh()
        s = run(args + ["--draw"], d, f"the end of a run on {st}, drawn")
        if s:
            check(s["mode"] == MODE_END, f"the run on {st} did not end: {s}")
        shutil.rmtree(d, ignore_errors=True)

    # a long run: levels reached, coins collected, the best kept
    d = fresh()
    s = run(["--run", "0,0", "--levels", "10,10,10,8", "--auto", "--frames", "9000"], d, "a long run")
    if s:
        check(s["level"] >= 2 and s["coins"] > 1000, f"a long run on the Countryside: {s}")
    shutil.rmtree(d, ignore_errors=True)

    # random keys through the menus and runs (with coins, so buying happens too): no crash, no sanitizer error
    sessions = 60 if QUICK else 300

    def fuzz(seed):
        d = fresh()
        p = play(["--fuzz", str(seed), "--coins", str(seed * 99991 % 6000000), "--frames", "4000"], d)
        shutil.rmtree(d, ignore_errors=True)
        bad = p.returncode or "runtime error" in p.stderr or "AddressSanitizer" in p.stderr
        return seed, (p.stderr[-1500:] if bad else None)

    def fuzz_run(seed):   # straight into a run, on every stage with every vehicle in turn
        d = fresh()
        st, v = seed % len(STAGES), seed // len(STAGES) % len(VEHICLES)
        lv = ",".join(str(1 + (seed * k) % 9) for k in (3, 5, 7, 2))
        p = play(["--fuzz", str(seed), "--run", f"{st},{v}", "--levels", lv, "--frames", "3000"], d)
        shutil.rmtree(d, ignore_errors=True)
        bad = p.returncode or "runtime error" in p.stderr or "AddressSanitizer" in p.stderr
        return seed, (p.stderr[-1500:] if bad else None)

    with ThreadPoolExecutor(max_workers=os.cpu_count() or 4) as pool:
        for seed, err in pool.map(fuzz, range(1, sessions + 1)):
            check(err is None, f"random keys {seed}: {err}")
        for seed, err in pool.map(fuzz_run, range(1, len(STAGES) * len(VEHICLES) + 1)):
            check(err is None, f"random keys in a run {seed}: {err}")

    for f in failures:
        print("FAIL", f)
    print("ok" if not failures else f"{len(failures)} failures")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
