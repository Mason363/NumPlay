"""The host build's checks (make test): check.py build/play src/data.bin

- every room's hazard respawn points: ground under them, in the room
- every gate and door (a door taking his control, as its FSM does): the Knight arrives in the next room, in control, on
  the ground, and can slash and move; and spikes there send him back to a good place
- the ground's seams, as the calculator rounds: landed a few float steps low, the Knight walks off both ways; jumping
  at a wall, he falls back down
- the text of the shops, the inventory (its items, charms and map), the menus, the messages and the stag's menu: in the
  screen, in its box, no line over another
- every arena won: its fight over and the room's battle gates open, also as the Knight comes back and after a save and
  a load
- a long walk of made up keys from every room: the Knight never out of the room, without control, wedged, undrawn or
  out of the view, and the view never jumping
- a bench: sat on, the inventory opened, a charm equipped, then up again (atBench off)
- the soul orb: dark with no soul, white with its liquid when full
- a version 2 save: loaded, its rooms' bits where they are now
- the charms: OK changes none away from a bench; at one, equipped (its effect on), none past the notches, unequipped,
  overcharmed on the fifth try and off again, kept by Home in the inventory and a load
- the stag stations: each seen (their blanker faded out), come in by its gate, by the stag (called by its bell) and
  loaded at its bench
- each shop's first charm bought: its lesson sent away by a key, the Knight free again
- Dirtmouth's map shop: at its door, Inspect while it is shut, Enter once it is open and never both, also as the Knight
  comes back to it
- the Snail Shaman's temple, played as one game: his spell taken, the Elder Baldur killed, his talk opening the gate, and
  so after a save and a load too; its lifeblood cocoon broken through a load and a rest there, whole after another bench
- the enemies the game keeps dead until a rest: still dead as the Knight comes back, out again after a rest at a bench,
  a death or a load at a bench, and after a rest in their own room; the persistent ones and a broken cocoon there not
- the way to the False Knight, played room by room as one game (the Knight unhurt): from a new game's fall into King's
  Pass, and from the Aspid Hunters' arena by the hot spring, to his arena, where he comes down
- the way to Hornet, played room by room as one game (the Knight unhurt): from Greenpath's first room, out of the room
  she is first seen in at its bottom right and up out of the Moss Knights' arena, to her arena, where she wakes
- every room played with its enemies hit, in a new game and after the bosses, drawn whole and fast (as a slow
  calculator draws it): the sanitizers stop on any fault
"""
import os
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor

PLAY, DATA = sys.argv[1], sys.argv[2]
# (the game in parts, each an app: the second's data is data_fw.bin; the other part's build, if made, for the ways
# between them)
PART = 2 if "data_fw" in os.path.basename(DATA) else 1
ROOM0 = ("Town", "60,12") if PART == 1 else ("Crossroads_18", "20,25")   # (a room of the part's, a place in it)
OTHER = (os.path.join(os.path.dirname(PLAY), "play-fw"), os.path.join(os.path.dirname(DATA), "data_fw.bin")) if PART == 1 else \
    (os.path.join(os.path.dirname(PLAY), "play"), os.path.join(os.path.dirname(DATA), "data.bin"))
WALKS = [(seed, 4000) for seed in range(1, 5)]   # (seed, ticks of 20 ms)
SWEEP_KEYS = "_*30,R*120,A*3,J*25,L*200,A*4,_*60,R*60,J*30,A*3,_*900"
WORLDS = ("", "7,11")
# Crossroads_47's bench: sit, the inventory, right to the charms, equip the first, close, get up
BENCH_KEYS = "*60,U*3,*150,I*2,*60,R*2,*15,R*2,*15,R*2,*15,R*2,*30,O*2,*30,I*2,*60,R*2,*80"   # PlayerData bools on, by number: none, then the False Knight and Hornet beaten
JOBS = os.cpu_count() or 2


def play(*args, env=None):
    p = subprocess.run([PLAY, DATA] + list(args), capture_output=True, text=True, env=dict(os.environ, **(env or {})))
    return p.returncode, p.stdout + p.stderr


def faults(out):
    return [l for l in out.splitlines() if "AddressSanitizer" in l or "runtime error" in l or l.startswith("SUMMARY")]


def report(name, rc, out, bad_prefixes):
    bad = [l for l in out.splitlines() if l.startswith(bad_prefixes)] + faults(out)
    ok = rc == 0 and not bad
    print("%s %s" % ("ok  " if ok else "FAIL", name))
    for l in bad[:20]:
        print("     " + l)
    return ok


def main():
    results = []
    rc, out = play("--gates", "markers")
    results.append(report("hazard respawn points", rc, out, ("BAD",)))
    rc, out = play("--gates", "all")
    results.append(report("gates", rc, out, ("FAIL",)))
    rc, out = play("--gates", "seams")
    results.append(report("seams", rc, out, ("FAIL",)))
    with tempfile.TemporaryDirectory() as saves:
        rc, out = play("--gates", "texts", env={"HKSAVES": saves})
    results.append(report("texts", rc, out, ("FAIL",)))
    with tempfile.TemporaryDirectory() as saves:
        rc, out = play("--gates", "arenas", env={"HKSAVES": saves})
    results.append(report("arenas", rc, out, ("FAIL",)))
    # the ways into the other part (app): taken, the save keeps the way; loaded in the other part's build (if made),
    # the Knight comes in there by it
    with tempfile.TemporaryDirectory() as saves:
        rc, out = play("--gates", "cross", env={"HKSAVES": saves})
        ways = [l.split() for l in out.splitlines() if l.startswith("ok   cross")]
        if os.path.exists(OTHER[0]) and ways:
            p = subprocess.run([OTHER[0], OTHER[1], "--room", "Crossroads_18" if PART == 1 else "Town", "--play", "_*150"],
                               capture_output=True, text=True,
                               env=dict(os.environ, HKSAVES=saves, HKLOAD="0", HKROOM="1"))
            room = next((l.split()[1] for l in p.stdout.splitlines() if l.startswith("room:")), "")
            want = ways[-1]
            # (its line: ok cross ROOM GATE -> room N (part P) by ENTRY; the room there is named by the other build)
            if p.returncode or not room:
                out += "\nFAIL the other part's build did not load the save: %s" % (p.stdout + p.stderr)[-300:]
            else:
                out += "\n   loaded in the other part: in %s" % room
    results.append(report("the ways into the other part, saved and loaded there", rc, out, ("FAIL",)))
    if PART == 2:
        return finish(results)
    # (HKFLAGS: a charm had, Gathering Swarm; HKPD: the bools on at the end, 77 its equipped one, 0 atBench)
    rc, out = play("--room", "Crossroads_47", "--at", "29.3,6.5", "--play", BENCH_KEYS, env={"HKFLAGS": "3,37", "HKPD": "1"})
    pd = next((l.split()[1:] for l in out.splitlines() if l.startswith("pd:")), [])
    pd = pd[:next((i for i, t in enumerate(pd) if not t.isdigit()), len(pd))]
    bench = "77" in pd and "0" not in pd
    results.append(report("bench: a charm equipped", rc, out if bench else out + "\nFAIL bools on: " + " ".join(pd), ("FAIL",)))
    with tempfile.TemporaryDirectory() as saves:
        rc, out = play("--gates", "charms", env={"HKSAVES": saves})
    results.append(report("charms: changed at a bench only", rc, out, ("FAIL",)))
    with tempfile.TemporaryDirectory() as saves:
        rc, out = play("--gates", "stations", env={"HKSAVES": saves})
    results.append(report("stag stations: seen, the rides", rc, out, ("FAIL",)))
    rc, out = play("--gates", "shopcharm")
    results.append(report("a shop's first charm: its lesson, the Knight free after it", rc, out, ("FAIL",)))
    rc, out = play("--gates", "mapshop")
    results.append(report("Dirtmouth's map shop: Inspect shut, Enter open, never both", rc, out, ("FAIL",)))
    with tempfile.TemporaryDirectory() as saves:
        rc, out = play("--gates", "shaman", env={"HKSAVES": saves})
    results.append(report("the Snail Shaman: his spell, the Elder Baldur, his gate, saved and loaded", rc, out, ("FAIL",)))
    with tempfile.TemporaryDirectory() as saves:
        rc, out = play("--gates", "respawn", env={"HKSAVES": saves})
    results.append(report("enemies out again after a rest, a death or a load at a bench", rc, out, ("FAIL",)))
    rc, out = play("--gates", "way")
    results.append(report("the way to the False Knight", rc, out, ("FAIL",)))
    rc, out = play("--gates", "greenpath")
    results.append(report("the way to Hornet", rc, out, ("FAIL",)))
    return finish(results)


def finish(results):
    # the soul orb: dark inside with no soul, its liquid white in it when full (in the screen's pixels at its middle)
    shades = []
    with tempfile.TemporaryDirectory() as d:
        for mp in (0, 99):
            path = os.path.join(d, "orb%d.ppm" % mp)
            rc, out = play("--room", ROOM0[0], "--at", ROOM0[1], "--play", "_*130", "--shot", path, env={"HKMP": str(mp)})
            with open(path, "rb") as f:
                _, size, _, px = f.read().split(b"\n", 3)
            w = int(size.split()[0])
            shades.append(sum(px[(y * w + x) * 3 + c] for y in range(42, 52) for x in range(12, 22) for c in range(3)) / 300)
    orb = shades[0] < 80 and shades[1] > 200
    results.append(report("the soul orb filled", rc, out if orb else out + "\nFAIL its middle: %.0f empty, %.0f full" % tuple(shades),
                          ("FAIL",)))
    # a version 2 save (Crossroads_47's bench; rooms 19 and 20 visited, 20 mapped, and the map's piece of Crossroads_21
    # at bit 38, as it was then): loaded, its rooms where they are now, the map's piece at the last bit
    with tempfile.TemporaryDirectory() as saves:
        with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "save_v2.sav"), "rb") as f, \
                open(os.path.join(saves, "hk1.sav"), "wb") as g:
            g.write(f.read())
        rc, out = play("--room", ROOM0[0], "--play", "*20", env={"HKSAVES": saves, "HKLOAD": "0", "HKPD": "1"})
    rooms = next((l for l in out.splitlines() if l.startswith("rooms:")), "")
    # (the second part: in at its first room too, where it meets the first)
    want = "rooms: 19 20m 127m" if PART == 1 else "rooms: 19 20m 38 127m"
    results.append(report("a version 2 save", rc, out if rooms == want else out + "\nFAIL " + rooms, ("FAIL",)))
    with ThreadPoolExecutor(JOBS) as pool:
        walks = list(pool.map(lambda w: play("--gates", "wander:all:%d:%d" % w, env={"HKFAST": "1"} if w[0] % 2 else None), WALKS))
    for (seed, ticks), (rc, out) in zip(WALKS, walks):
        results.append(report("walk %d (%d s from every room%s)" % (seed, ticks // 50, ", drawn fast" if seed % 2 else ""), rc, out, ("FAIL",)))
    rc, out = play("--list")
    rooms = out.split()
    runs = [(r, w, f) for r in rooms for w in WORLDS for f in ("", "1")]
    env = {"HKGOD": "1", "HKHIT": "50", "HKHITN": "20", "HKHITD": "10"}
    with ThreadPoolExecutor(JOBS) as pool:
        played = list(pool.map(lambda rwf: play("--room", rwf[0], "--play", SWEEP_KEYS, "--shot", os.devnull,
                                                env=dict(env, HKFLAGS=rwf[1], **({"HKFAST": "1"} if rwf[2] else {}))), runs))
    bad = [(r, w, f, rc, out) for (r, w, f), (rc, out) in zip(runs, played) if rc != 0 or faults(out)]
    print("%s every room played (%d runs)" % ("FAIL" if bad else "ok  ", len(runs)))
    for r, w, f, rc, out in bad:
        print("     %s [%s]%s exit %d" % (r, w, " fast" if f else "", rc))
        for l in faults(out)[:3]:
            print("       " + l)
    results.append(not bad)
    print("%d of %d checks failing" % (results.count(False), len(results)))
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
