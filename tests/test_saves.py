#!/usr/bin/env python3
"""NumPlay Studio's Saves (customize/saves.js, usb.js; docs/saves.md): what the website reads from the calculator
and writes back is what the calculator's own code reads and writes.

- every game's files are told (games/games.json "records"): each has a base name and an extension Epsilon doesn't
  use for its own files, so the website backs it up as its game's and may put it back;
- the list of files changes as games/common/epsilon_files.h changes it, to the byte (tests/saves/files.c is that
  code built for this computer; tests/saves/check.js runs the same changes through saves.js);
- a change becomes writes that each leave a list of whole files, so a cable pulled between two loses no file's
  content;
- Epsilon's memory of the last file looked up is found as the games find it, with Epsilon's checksums
  (tools/emu.py's);
- the copies of saves in Python scripts unpack as games/common/np_lz.h packed them;
- the calculator, over USB (tests/saves/usb_check.js, with tests/saves/fake_calculator.js, which answers as
  Epsilon's own DFU code does): found on every model and software, read, changed, read back, and left whole when
  the cable is pulled at any transfer.

With the kit (--kit, `make kit`): its kit.json tells the website every game's files ("saves"), as games.json has them.

With NumPlay.nwa, in the ARM emulator too, with the launcher itself:
- the launcher's own copy (numplay_saves.py) reads back to its saves;
- a game's saves deleted by the website stay deleted when NumPlay opens (its line in the copy is struck out);
- every file put back by the website after a reset is there when NumPlay opens, and its copy follows.

Usage: test_saves.py [build/compiled/NumPlay.nwa] [--kit build/data/site-kit] [--seed N] [--rounds N] [--out DIR]
"""
import argparse
import json
import os
import random
import subprocess
import sys

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
HERE = os.path.join(ROOT, "tests", "saves")
sys.path.insert(0, os.path.join(ROOT, "tools"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

EPSILON = ["eq", "exp", "func", "lis", "seq", "mat", "reg", "pc", "rgm", "stm", "sys"]  # (saves.js's, checked there)


def told(games):
    """every game names its files, in a way the website can back up and put back"""
    problems = []
    for g in games:
        if not g.get("records"):
            problems.append(f"{g['id']} names no file (games.json \"records\"): the website couldn't back up its saves")
        for r in g.get("records", []):
            name = r[:-1] + "0.x" if r.endswith("*") else r
            base, dot, ext = name.partition(".")
            if not r.endswith("*") and (not dot or not base or not ext or "." in ext):
                problems.append(f"{g['id']}: {r} isn't base.extension")
            if ext in EPSILON:
                problems.append(f"{g['id']}: {r} has one of Epsilon's own extensions")
            if len(name) > 31 or not all(33 <= ord(c) < 127 for c in name):
                problems.append(f"{g['id']}: {r} is longer than 31 characters or not plain ASCII (the copies of saves name it)")
            if r.endswith("*") and len(r) < 3:
                problems.append(f"{g['id']}: {r} would take every file")
    return problems


def launcher(nwa, out, games):
    """the launcher itself, in the emulator: its copy, a game deleted by the website, everything put back by it"""
    import emu  # noqa: F401
    import test_progress as tp
    mine = [g for g in games if g.get("default", True)]
    rnd = random.Random(11)
    script = ("pi.py", b"\x01print(3.14159)\n\x00")
    saves = {"numplay.set": bytes([ord("N"), 1, 0, 0])}
    for g in mine:
        for r in g["records"]:
            for n in ([r[:-1] + "0_0.nbe", r[:-1] + "-1_2.nbe"] if r.endswith("*") else [r]):
                size = rnd.choice([1, 2, 8, 30, 64, 192, 700])
                data = (bytes(rnd.randrange(256) for _ in range(size)) if rnd.random() < 0.5 else
                        bytes(rnd.choice([0, 0, 0, i % 13, rnd.randrange(256)]) for i in range(size)))
                saves.setdefault(n, data)
    problems = []
    job = os.path.join(out, "job.json")

    def apply(src, dst, what):
        json.dump({**what, "games": games}, open(job, "w"))
        r = subprocess.run(["node", os.path.join(HERE, "apply.js"), src, dst, job], capture_output=True, text=True)
        if r.returncode:
            problems.append("apply.js: " + r.stderr.strip()[-300:])
            return None
        return json.loads(r.stdout)

    # 1. the launcher writes its copy: saves.js reads the saves back from it (check.js, with this list)
    st = os.path.join(out, "with_copy.bin")
    tp.write_storage(st, [script] + sorted(saves.items()))
    recs, p = tp.run(nwa, st, [], 2500, out, "saves_copy")
    problems += p + tp.check_copy(recs, saves)
    image = {"path": st, "files": {n: v.hex() for n, v in saves.items()}}

    # 2. the website deletes two games' saves: NumPlay opens, and they stay deleted
    gone = [g for g in mine if not any(r.endswith(".py") for r in g["records"])][:2]
    files = {r for g in gone for r in saves if any(r == x or (x.endswith("*") and r.startswith(x[:-1])) for x in g["records"])}
    st2 = os.path.join(out, "deleted.bin")
    did = apply(st, st2, {"delete": [g["id"] for g in gone]})
    if did is not None:
        if did["failed"]:
            problems.append(f"deleting {[g['id'] for g in gone]}: {did['failed']}")
        recs, p = tp.run(nwa, st2, [], 2500, out, "saves_deleted")
        problems += p
        back = sorted(f for f in files if f in recs)
        if back:
            problems.append(f"deleted by the website, back when NumPlay opened: {back}")
        kept = {n: v for n, v in saves.items() if n not in files}
        for n, v in kept.items():
            if recs.get(n) != v:
                problems.append(f"after the website deleted other games' saves, {n} " + ("is missing" if n not in recs else "differs"))
        problems += tp.check_copy(recs, kept)
        if recs.get(script[0]) != script[1]:
            problems.append("the other script changed when the website deleted saves")

    # 3. a reset (no file left), then the website puts every file of the backup back: NumPlay finds them all
    st3 = os.path.join(out, "restored.bin")
    tp.write_storage(st3, [])
    did = apply(st3, st3, {"restore": st})
    if did is not None:
        if did["failed"]:
            problems.append(f"putting a backup back: {did['failed']}")
        recs, p = tp.run(nwa, st3, [], 2500, out, "saves_restored")
        problems += p
        for n, v in saves.items():
            if recs.get(n) != v:
                problems.append(f"put back by the website, {n} " + ("is missing" if n not in recs else "differs") + " when NumPlay opened")
        problems += tp.check_copy(recs, saves)
        if recs.get(script[0]) != script[1]:
            problems.append("the script put back by the website changed")

    # 4. an update (the installer leaves the scripts only), then the website puts back the files that are missing
    #    over what is there: the same again
    st4 = os.path.join(out, "updated.bin")
    tp.write_storage(st4, [(n, v) for n, v in tp.records(open(st, "rb").read()) if n.endswith(".py")])
    did = apply(st4, st4, {"restore": st})
    if did is not None:
        recs, p = tp.run(nwa, st4, [], 2500, out, "saves_updated")
        problems += p
        for n, v in saves.items():
            if recs.get(n) != v:
                problems.append(f"put back after an update, {n} " + ("is missing" if n not in recs else "differs"))
    return image, problems


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nwa", nargs="?")
    ap.add_argument("--kit", help="the website's kit (make kit): its kit.json must tell every game's files")
    ap.add_argument("--seed", type=int, default=20261125)
    ap.add_argument("--rounds", type=int, default=3)
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "test_saves"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    games = json.load(open(os.path.join(ROOT, "games", "games.json")))
    problems = told(games)

    if a.kit:
        kit = json.load(open(os.path.join(a.kit, "kit.json")))
        want = [{"id": g["id"], "title": g["title"], "records": g["records"]} for g in games]
        if kit.get("saves") != want:
            problems.append(f"{a.kit}/kit.json's \"saves\" aren't games.json's files (tools/kit.py writes them): "
                            "the website couldn't tell whose save a file is")

    files = os.path.join(a.out, "files")
    cc = os.environ.get("CC", "cc")
    if subprocess.run([cc, "-O1", "-Wall", "-Wextra", "-Wno-unused-function", "-o", files,
                       os.path.join(HERE, "files.c")]).returncode:
        sys.exit("test_saves: tests/saves/files.c didn't build")

    crcs = {}
    try:
        import emu
        names = ["numdash.nds", "numplay_saves.py", "numplay.set", "a.py", "nb1r0_0.nbe", "x.sav", "hollowknight_saves.py"]
        crcs = {n: emu.record_crc(n) for n in names}
    except ImportError as e:
        print(f"test_saves: (without the emulator's checksums: {e})")

    images = []
    if a.nwa:
        image, p = launcher(a.nwa, a.out, games)
        images.append(image)
        problems += p

    facts = os.path.join(a.out, "facts.json")
    json.dump({"games": games, "crcs": crcs, "seed": a.seed, "rounds": a.rounds, "images": images}, open(facts, "w"))
    for script in ("check.js", "usb_check.js"):
        if not os.path.exists(os.path.join(HERE, script)):
            continue
        if subprocess.run(["node", os.path.join(HERE, script), files, facts]).returncode:
            problems.append(f"tests/saves/{script} failed")

    if problems:
        print("test_saves: FAILED")
        for p in problems:
            print("  " + p)
        sys.exit(1)
    print(f"test_saves: {len(games)} games' files are told; the website's Saves read and write as the calculator does"
          + ("" if a.nwa else " (without the launcher: give NumPlay.nwa)"))


if __name__ == "__main__":
    main()
