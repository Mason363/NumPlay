#!/usr/bin/env python3
"""Links NumPlays of your own from the kit (`make kit`), the way the customizer website does (GNU ld on
the kit's objects, with a script of script.json's parts), and plays their games through the launcher
in the ARM emulator, with tests/test_games.py's sessions:

- alone: each big game alone in a NumPlay of the calculator's app space (APP_SPACE), where it fits, and
  where it doesn't, its own app in the kit for the website to hand out (kit.json "alone");
- sets: several big games, and big games with small ones, in a NumPlay for Enlarged Extra Apps (up to
  6291456 bytes, 0x90200000 to the external flash's end: the emulator's apps go on there while the
  userland header still says the app area ends at 0x903F0000, as on a calculator with nwagyu's Nwagra);
- ram: each big game's RAM inside NumPlay, against custom builds of 25.2 (133652 bytes), 23.2 (148928)
  and Upsilon (107674). The games play with the least the calculators give where they fit, and where
  one doesn't, OK on its card doesn't start it (the card says how much it lacks);
- probe: each big game alone with --ram bytes of RAM, whatever it needs (to find the least it plays in);
- spaces: for each space for apps the website offers (kit.json "spaces": the app area from 0x90200000, or from
  0x90180000 as on Epsilon 26.4, each also on to 0x90800000 with Nwagra), the fullest NumPlay it lets you make
  there, NumPlay's games and the big games in their order while they fit, installed at that space's start, with
  the userland header's app area starting there: the last of NumPlay's games (without Nwagra) and the first and
  last big games played where they are, in the app area, past it, and into the second slot.

In each session the game starts and plays, saves, and finds its saves again from its own copy as after
installing NumPlay again; Home and On/Off take you back out of NumPlay, which clears the RAM, with no
access outside the app's RAM, no write to flash or to the firmware's RAM. nwlink nwa-bin links each
NumPlay at the app area's start with its whole space (6 MB for the sets).

Usage: test_kit.py [--kit build/data/site-kit] [--only alone,sets,ram,spaces,probe] [--game ID] [--set N]
                   [--space ID] [--ram N] [--out DIR]
"""
import argparse
import gzip
import json
import os
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu  # noqa: E402
import test_games  # noqa: E402
from unicorn import UC_HOOK_CODE  # noqa: E402
from unicorn.arm_const import UC_ARM_REG_R0  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
CUSTOM_RAM, RAM_LIMIT, UPSILON_RAM = 133652, 148928, 107674
MANIFEST = json.load(open(os.path.join(ROOT, "games", "games.json")))
XL = [g["id"] for g in MANIFEST if g.get("xl")]                  # the big games
SMALL = [g["id"] for g in MANIFEST if g.get("default", True)]    # NumPlay's own
# NumPlays for Enlarged Extra Apps: the three biggest; five more; four more; three with all of NumPlay's games; the
# four newest past the app area, into the second slot, after the biggest that fits there
SETS = [
    ["celeste", "hollowknight", "championisland"],
    ["undertale", "doom", "pvz", "numpowder", "leveldevil"],
    ["cookieclicker", "subway", "fnaf", "isaac"],
    SMALL + ["sonic", "hillclimb", "finalearth2"],
    ["cookieclicker", "jetpackjoyride", "stickmanhook", "donkeykong", "dune"],
]


class Kit:
    def __init__(self, path, tmp):
        self.path, self.tmp = path, tmp
        self.k = json.load(open(os.path.join(path, "kit.json")))
        self.games = {g["id"]: g for g in self.k["games"]}

    def obj(self, rel):
        out = os.path.join(self.tmp, rel.replace("/", "_"))
        if not os.path.exists(out):
            with gzip.open(os.path.join(self.path, rel + ".gz")) as f, open(out, "wb") as g:
                g.write(f.read())
        return out

    def link(self, ids, out):
        """A NumPlay with these games, in games.json's order (the big games last, past the small ones)."""
        ids = sorted(ids, key=lambda i: self.games[i]["index"])
        s = self.k["script"]
        script = out[:-4] + ".ld"
        open(script, "w").write(s["head"] + "".join(s["blocks"][i] for i in ids) + s["tail"])
        objs = [self.obj("base.o"), self.obj("arena.o"), self.obj("variants/NumPlay-name.o"),
                self.obj("variants/NumPlay-icon.o")] + [self.obj(f"games/{i}.o") for i in ids]
        subprocess.run(["arm-none-eabi-ld"] + self.k["link"] + ["-T", script] + objs + ["-o", out], check=True)
        subprocess.run(["arm-none-eabi-strip", "--strip-unneeded", out], check=True)
        return ids

    def ram(self, i):
        """what a game needs of the RAM apps get, inside NumPlay"""
        return self.k["launcherRam"] + self.games[i]["ram"]


def installed(nwa, space, ram=RAM_LIMIT, start=0x90200000):
    """the bytes nwlink installs, linked at the app area's start with this much space"""
    b = nwa[:-4] + ".bin"
    r = subprocess.run(["nwlink", "nwa-bin", "--flash-start", hex(start), "--flash-length", str(space),
                        "--ram-length", str(ram), nwa, b], capture_output=True, text=True)
    return os.path.getsize(b) if r.returncode == 0 else None, (r.stdout + r.stderr).strip()


def watch(c, kit_ids):
    """the games' entries and the heap their malloc takes (np_sbrk), as the emulator runs"""
    c.entered, c.heap, c.heap_max = {}, 0, 0
    for i in kit_ids:
        a = c.symbols.get(f"np_{i}_main")
        if a is not None:
            c.uc.hook_add(UC_HOOK_CODE, lambda uc, addr, size, gid: c.entered.setdefault(gid, c.now_ms), i,
                          begin=a & ~1, end=(a & ~1) + 1)
    a = c.symbols.get("np_sbrk")
    if a is not None:
        def sbrk(uc, addr, size, data):
            n = uc.reg_read(UC_ARM_REG_R0)
            c.heap += n - (1 << 32) if n & 0x80000000 else n
            c.heap_max = max(c.heap_max, c.heap)
        c.uc.hook_add(UC_HOOK_CODE, sbrk, begin=a & ~1, end=(a & ~1) + 1)


# the emulator's Calculator, watched (test_games.run_once makes its own)
_Calculator = emu.Calculator
LAST = []


class Watched(_Calculator):
    def __init__(self, *a, **k):
        super().__init__(*a, **k)
        watch(self, XL + SMALL)
        LAST.append(self)


emu.Calculator = Watched


def started(nwa, index):
    """how long after OK on its card the game at index starts in this NumPlay"""
    c = Watched(nwa)
    keys, _ = test_games.through_launcher(index, [], 0)
    c.keys = keys
    ok = keys[-1][0]
    while not c.entered and c.now_ms < ok + 5000:
        c.run(c.now_ms + 100)
    return int(min(c.entered.values()) - ok) + 1 if c.entered else 800


def session(nwa, ids, name, out, tag):
    """test_games' two sessions of a game in this NumPlay, with its keys from when it starts (as on its own);
    its own copy brings its saves back"""
    start = started(nwa, ids.index(name))
    LAST.clear()
    problems, stack, recs = test_games.play(name, nwa, out, ids.index(name), copy=test_games.COPIES.get(name)
                                            if name in XL else None, tag=tag, start=start)
    for c in LAST:
        if name not in c.entered:
            problems.append("the game did not start")
    heap = max((c.heap_max for c in LAST), default=0)
    return problems, stack, heap


def report(tag, problems, extra=""):
    print(f"{tag:34s} {'ok' if not problems else 'FAIL'}  {extra}", flush=True)
    for x in problems:
        print("   ", x)
    return bool(problems)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--kit", default=os.path.join(ROOT, "build", "data", "site-kit"))
    ap.add_argument("--only", default="alone,sets,ram,spaces")
    ap.add_argument("--game", help="only this big game (alone, ram)")
    ap.add_argument("--set", type=int, help="only this set (0, 1...)")
    ap.add_argument("--space", help="only this space (kit.json spaces: std, e26, xl, xl26)")
    ap.add_argument("--ram", type=int, help="the RAM apps get (alone, sets; default: the least they fit in)")
    ap.add_argument("--out", default=os.path.join(ROOT, "build", "data", "test_kit"))
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    only = a.only.split(",")
    failed = False
    tmp = tempfile.mkdtemp()
    kit = Kit(a.kit, tmp)
    k = kit.k
    xl = [i for i in XL if not a.game or i == a.game]
    print(f"kit {k['version']}: NumPlay's own flash {k['base']} bytes, its own RAM {k['launcherRam']} bytes, "
          f"app space {k['appSpace']} ({k['xlAppSpace']} with Enlarged Extra Apps)")

    if "alone" in only:
        for i in xl:
            nwa = os.path.join(tmp, f"alone-{i}.nwa")
            ids = kit.link([i], nwa)
            size, msg = installed(nwa, k["appSpace"])
            g = kit.games[i]
            if size is None or size > k["appSpace"]:
                # too big for the app space next to the launcher: in NumPlay only with more space (Epsilon 26's, or
                # Enlarged Extra Apps), and the website hands out its own app instead (kit.json "alone", as it is)
                big, msg2 = installed(nwa, k["xlAppSpace"])
                problems = [] if big and big > k["appSpace"] else [f"nwa-bin: {msg or msg2}"]
                own = g.get("alone") or {}
                gz = os.path.join(a.kit, "apps", i + ".nwa.gz")
                app = os.path.join(ROOT, "build", "data", "apps", own.get("file", "?"))
                if not own or not os.path.exists(gz):
                    problems.append("the kit hasn't got it on its own (kit.json alone, apps/)")
                else:
                    with gzip.open(gz) as f:
                        data = f.read()
                    if not os.path.exists(app) or open(app, "rb").read() != data:
                        problems.append(f"apps/{i}.nwa.gz isn't build/data/apps/{own['file']}")
                    mine = os.path.join(tmp, f"own-{i}.nwa")
                    open(mine, "wb").write(data)
                    n, msg3 = installed(mine, k["appSpace"])
                    if n is None:   # (one that only fits Nwagra's 6 MB: the Studio offers it there only)
                        n, msg3 = installed(mine, k["xlAppSpace"])
                    if n is None or n > k["xlAppSpace"] or n != own["size"]:
                        problems.append(f"on its own: {n} bytes, kit.json says {own['size']} ({msg3})")
                failed |= report(f"alone {i}", problems, f"{big} bytes, {big - k['appSpace']} over the app space: "
                                 f"in NumPlay with more space (Epsilon 26, Nwagra), else {own.get('file')} on its own" if big else "")
                continue
            emu.configure("n0120", "epsilon", a.ram or (CUSTOM_RAM if kit.ram(i) <= CUSTOM_RAM else RAM_LIMIT))
            problems, stack, heap = session(nwa, ids, i, a.out, f"alone_{i}")
            failed |= report(f"alone {i}", problems, f"{size} bytes ({k['appSpace'] - size} to spare), RAM "
                             f"{emu.EXT_RAM_LEN}, stack {stack}, malloc {heap}, game flash {g['flash']}")

    if "probe" in only:  # (each big game alone, with --ram bytes of RAM whatever it needs, 6 MB of app space)
        for i in xl:
            nwa = os.path.join(tmp, f"probe-{i}.nwa")
            ids = kit.link([i], nwa)
            emu.configure("n0120", "epsilon", a.ram, xl=True)
            problems, stack, heap = session(nwa, ids, i, a.out, f"probe_{i}_{a.ram}")
            failed |= report(f"probe {i}", problems, f"RAM {emu.EXT_RAM_LEN} (it needs {kit.ram(i)}), malloc {heap}")

    if "sets" in only:
        for n, s in enumerate(SETS):
            if a.set is not None and a.set != n:
                continue
            nwa = os.path.join(tmp, f"set{n}.nwa")
            ids = kit.link(s, nwa)
            size, msg = installed(nwa, k["xlAppSpace"])
            if size is None or size > k["xlAppSpace"]:
                failed |= report(f"set {n}", [f"does not install in {k['xlAppSpace']} bytes: {msg}"])
                continue
            print(f"set {n}: {len(ids)} games, {size} bytes ({k['xlAppSpace'] - size} to spare of 6 MB)", flush=True)
            emu.configure("n0120", "epsilon", a.ram, xl=True)
            # the big games, and a few of the small ones, played where they are
            for i in [x for x in ids if x in XL] + [x for x in ids if x in SMALL][:3]:
                problems, stack, heap = session(nwa, ids, i, a.out, f"set{n}_{i}")
                failed |= report(f"  set {n}: {i}", problems, f"stack {stack}, malloc {heap}")

    if "spaces" in only:
        for sp in k.get("spaces", []):
            if a.space and a.space != sp["id"]:
                continue
            size, start = sp["size"], sp["start"]
            ids = [g["id"] for g in sorted(k["games"], key=lambda g: g["index"]) if g["default"]]
            used = k["base"] + sum(kit.games[i]["flash"] for i in ids)
            for g in sorted(k["games"], key=lambda g: g["index"]):
                if g["xl"] and used + g["flash"] <= size:
                    ids.append(g["id"])
                    used += g["flash"]
            nwa = os.path.join(tmp, f"space-{sp['id']}.nwa")
            ids = kit.link(ids, nwa)
            n, msg = installed(nwa, size, start=start)
            if n is None or n > size or n > used:
                failed |= report(f"space {sp['id']}", [f"{n} bytes in {size} at {start:#x}, {used} by the kit: {msg}"])
                continue
            print(f"space {sp['id']}: {size} bytes from {start:#x}, {len(ids)} games installed as {n} bytes, "
                  f"up to {start + n:#x}", flush=True)
            xl = sp["end"] > emu.EXT_FLASH_END
            ram = a.ram or (CUSTOM_RAM if max(kit.ram(i) for i in ids) <= CUSTOM_RAM else RAM_LIMIT)
            emu.configure("n0120", "epsilon", ram, xl=xl, apps_start=start)
            # (where each game's block is, from the symbols of the NumPlay the emulator runs)
            c = _Calculator(nwa)
            at = {i: c.symbols.get(f"np_{i}_begin", 0) for i in ids}
            # the last of NumPlay's games in the app area, and the first and last big games: in it or just past it,
            # and the furthest out (the others are where the sets have them)
            big = [x for x in ids if x in XL]
            for i in ([] if xl else [x for x in ids if x in SMALL][-1:]) + big[:1] + big[1:][-1:]:
                where = ("past the app area, in the second slot" if at[i] >= 0x90400000 else
                         "past the app area" if at[i] >= emu.EXT_FLASH_END else "in the app area")
                problems, stack, heap = session(nwa, ids, i, a.out, f"space_{sp['id']}_{i}")
                failed |= report(f"  space {sp['id']}: {i}", problems, f"at {at[i]:#x}, {where}, stack {stack}")
            emu.configure("n0120", "epsilon")

    if "ram" in only:
        print(f"{'RAM in NumPlay':18s} {'needs':>7s} {'133652':>8s} {'148928':>8s} {'107674':>8s}")
        for i in xl:
            need = kit.ram(i)
            fit = ["fits" if need <= r else f"-{need - r}" for r in (CUSTOM_RAM, RAM_LIMIT, UPSILON_RAM)]
            print(f"  {i:16s} {need:7d} {fit[0]:>8s} {fit[1]:>8s} {fit[2]:>8s}   (heap {kit.games[i]['heap']})",
                  flush=True)
        # where a game doesn't fit, its card's OK doesn't start it, and Home leaves cleanly
        for i in xl:
            short = [r for r in (UPSILON_RAM, CUSTOM_RAM, RAM_LIMIT) if kit.ram(i) > r]
            if not short:
                continue
            nwa = os.path.join(tmp, f"alone-{i}.nwa")
            ids = kit.link([i], nwa)
            r = max(short)
            emu.configure("n0120", "epsilon", r, xl=True)
            st = os.path.join(tmp, f"short-{i}.bin")
            open(st, "wb").write(bytes(emu.STORAGE_SIZE))
            c = Watched(nwa, storage_file=st)
            c.enable_checks(reads=True)
            keys, home = test_games.through_launcher(0, [], 1500)
            c.keys = keys + [(home + 1500, home + 1900, emu.KEYS["home"])]
            c.pending_shots = [(home, os.path.join(a.out, f"short_{i}_{r}.png"))]
            c.run(home + 5000)
            problems = sorted(set(c.violations))[:8]
            if i in c.entered:
                problems.append(f"started with {r} bytes of RAM, {kit.ram(i) - r} too few")
            if not c.exited:
                problems.append("did not quit on Home")
            if not c.app_ram_clean():
                problems.append("RAM not cleared on exit")
            failed |= report(f"short {i} ({r})", problems, "not started: its card says it needs more")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
