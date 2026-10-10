#!/usr/bin/env python3
"""Plays each game's calculator build in the ARM emulator, twice, and checks
what used to crash calculators or lose saves:

- the app quits on Home by itself (Epsilon holds Home back while it runs) and
  hands back its RAM cleared, so Epsilon's memory pools come back safely;
- no unaligned multi-word access (a fault on the Cortex-M7), no write to flash
  or to the firmware's RAM outside the file system;
- the file system stays valid: the game's save is there, the other files are
  untouched, and a file added after the save does not stop the game from
  updating it on the second run.

Usage: test_games.py build/apps [--game NAME] [--out DIR]
"""
import argparse
import json
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import emu  # noqa: E402

K = emu.KEYS


def presses(*items):
    """(time, key[, duration]) -> key ranges."""
    out = []
    for it in items:
        t, k = it[0], it[1]
        d = it[2] if len(it) > 2 else 90
        out.append((t, t + d, K[k]))
    return out


def every(t0, t1, step, key, d=80):
    return [(t, t + d, K[key]) for t in range(t0, t1, step)]


# name: (nwa, save record, first run keys, home time, second run keys, home time)
GAMES = {
    "crossyroad": ("CrossyRoad.nwa", "crossyroad.sav",
                   every(1500, 9000, 350, "up"), 12000, every(1500, 4000, 400, "up"), 6000),
    "chess": ("NumChess.nwa", "numchess.sav", presses((1500, "ok"), (2500, "ok")), 4000, [], 2500),
    "numdash": ("NumDash.nwa", "numdash.nds", presses((2500, "ok"), (4000, "ok"), (5500, "ok")), 9000,
                presses((2500, "ok")), 5000),
    # pause, Levels, level list, next level, play: that saves the level to start from
    # (held keys: the pause card's frames are slow while it slides in)
    "numdrive": ("NumDrive.nwa", "drivemad.sav",
                 presses((1500, "ok", 250), (2000, "right", 250), (2500, "ok", 250), (3000, "right", 250),
                         (3500, "ok", 250)), 5500,
                 presses((1500, "ok", 250), (2000, "right", 250), (2500, "ok", 250), (3000, "left", 250),
                         (3500, "ok", 250)), 5500),
    "tetris": ("Tetris.nwa", "tetris.sav", presses((1200, "ok")) + every(2500, 7000, 500, "up"), 8000,
               presses((1200, "ok"), (2500, "ok")), 5000),
    # add-ons, the Counter (5th), count to 2; next time it opens there: one more
    "numvisuals": ("NumVisuals.nwa", "numvisuals.sav",
                   presses((1500, "ok"), (2000, "right"), (2300, "right"), (2600, "right"), (2900, "right"),
                           (3300, "ok"), (3700, "up"), (4000, "up")), 5000,
                   presses((1500, "up")), 2500),
    # settings: Speed to Fast, back, then a Custom game; next time Speed goes on to Insane
    "flappybird": ("FlappyBird.nwa", "flappy.sav",
                   presses((1500, "down"), (1800, "ok"), (2100, "right"), (2400, "back"), (2700, "up"),
                           (2900, "right"), (3100, "ok"), (3900, "ok")) + every(4300, 7500, 450, "ok"), 9000,
                   presses((1500, "down"), (1800, "ok"), (2100, "right"), (2400, "back")), 3500),
    # continue the saved game (level 2) and play a bit; then set Fast in the options
    "pacman": ("PacMan.nwa", "pacman.sav", presses((1500, "ok")), 8000,
               presses((1500, "down"), (1800, "ok"), (2100, "right"), (2400, "back")), 3500),
    # Play, straight through the first apple into the wall: a best of 1; next
    # time, Settings, the fruit to a banana and back: saved, the best kept
    "snake": ("Snake.nwa", "snake.sav", presses((1500, "ok"), (2000, "right")), 7000,
              presses((1500, "down"), (1800, "ok"), (2100, "right"), (2400, "back")), 3500),
    # vs the computer: the settings are saved; next time, the dark theme
    "connectfour": ("ConnectFour.nwa", "connect4.sav", presses((1500, "ok"), (2500, "4")), 5000,
                    presses((1500, "down"), (1700, "down"), (1900, "down"), (2100, "down"), (2300, "right")), 3500),
    # a new game (Vegas, draw 1 from the seeded options), two draws; then Continue and one more
    "solitaire": ("Solitaire.nwa", "solitaire.sav", presses((1500, "ok"), (3500, "exe"), (4000, "exe")), 5000,
                  presses((1500, "ok"), (2500, "exe")), 3500),
    # the 4x4 game in the save (40 moves): Continue, two moves; next time three more
    "g2048": ("2048.nwa", "g2048.sav", presses((1500, "ok"), (2500, "left"), (3000, "right")), 4000,
              presses((1500, "ok"), (2500, "left"), (3000, "right"), (3500, "left")), 4500),
    # Beginner, the first square, leave mid-game; next time Continue, then a
    # new game from the pause menu and its first square
    "minesweeper": ("Minesweeper.nwa", "mines.sav", presses((1500, "ok"), (2000, "ok"), (2400, "right")), 3000,
                    presses((1500, "ok"), (2000, "back"), (2300, "down"), (2600, "ok"), (3000, "ok")), 3600),
    # Play, serve, a brick or more: the best is kept; next time, Settings, screen shake off, back
    "breakout": ("BlockBreaker.nwa", "breakout.sav", presses((1500, "ok"), (2500, "ok")), 6000,
                 presses((1500, "down"), (1800, "ok"), (2100, "down"), (2400, "ok"), (2800, "back")), 4000),
    # Play, buy a lemonade stand (OK), run a sale (EXE); next time Continue, then the Upgrades tab (Right)
    "tycoon": ("NumTycoon.nwa", "tycoon.sav", presses((1500, "ok"), (2200, "ok"), (2600, "exe")), 5000,
               presses((1500, "ok"), (2200, "right")), 3500),
    # play, new run, the small blind, pick two cards and play them; then continue the run
    "balatro": ("Balatro.nwa", "balatro.sav",
                presses((1500, "ok"), (2500, "ok"), (3500, "ok"), (5000, "ok"), (5400, "right"), (5800, "ok"),
                        (6300, "exe")), 10000,
                presses((1500, "ok"), (2500, "ok")), 5000),
    # start, then through the bathroom and the hallway to the table; then continue
    "buckshot": ("BuckshotRoulette.nwa", "buckshot.sav", every(1500, 14000, 900, "ok"), 16000,
                 presses((1500, "ok")), 4000),
    # the intro film is skipped (seen), Lucky walks up the dock and talks to the
    # guardians; next time the game starts where it was left
    "championisland": ("ChampionIsland.nwa", "champion.sav",
                       presses((2000, "up", 1500)) + every(4000, 9000, 700, "ok"), 11000,
                       presses((2000, "up", 400)), 4000),
    # past the key sheet, Singleplayer, Create New World (Survival, a random seed), walk; then
    # Play Selected World
    "numblocks": ("NumBlocks.nwa", "nb1.nbw",
                  presses((3000, "ok"), (3600, "ok"), (4200, "ok"), (4800, "down"), (5100, "down"), (5400, "down"),
                          (5700, "down"), (6000, "down"), (6300, "ok"), (8700, "comma", 1500)), 12200,
                  presses((3000, "ok"), (3600, "ok")), 8000),
    # past the key sheet, the title, Climb, the chapter select and Start: the Prologue, walking; then Continue
    "celeste": ("Celeste.nwa", "celeste.sav",
                presses((2500, "ok"), (4000, "ok"), (5500, "ok"), (7000, "ok"), (8500, "ok"), (11000, "right", 2500)), 15000,
                presses((2500, "ok"), (4000, "ok"), (5500, "ok"), (7500, "right", 1500)), 11000),
    # Start Game, the first profile (a new game: past how to play's two pages, the Knight falls into King's Pass and
    # lies there a while), then Home, which saves as Quit to Menu; then the same profile, loaded
    "hollowknight": ("HollowKnight.nwa", "hk1.sav",
                     presses((2500, "ok"), (4500, "ok"), (5500, "ok"), (6500, "ok")) + every(18000, 22000, 600, "right", 400),
                     25000,
                     presses((2500, "ok"), (4500, "ok")) + every(9000, 11000, 600, "left", 400), 14000),
    # a level, its message, then play; then back to the level select
    "portal": ("PortalReturns.nwa", "portal.sav",
               presses((1500, "ok"), (2500, "ok"), (3500, "ok")) + every(4000, 7000, 400, "right", 300), 8000,
               presses((1500, "ok")), 4000),
}


# saves already on the calculator, and what each session must leave in them
SEEDS = {
    # the first level done and last played
    "numdrive": b"MD" + bytes([1] + [0] * 24) + b"\0\0\0",
    # puzzle rating 1234, a streak of 3, the bot Hugo, side 1, a 10-minute clock
    "chess": bytes([ord("C"), 1, 1234 & 255, 1234 >> 8, 3, 0, 7, 1 | 4 << 2]),
    # options (normal speed, 3 lives), best 9000, a game at level 2 with 5000 points, 3 lives, every dot
    "pacman": bytes([ord("P"), 1, 1, 2, 2, 1, 0, 0]) + struct.pack("<I", 9000) + bytes([1, 2, 3, 0])
              + struct.pack("<I", 5000) + bytes([255] * 30 + [15, 0]),
    # draw 1, Vegas, timed; 5 played, 2 won, a Vegas bank of -$100; no game in progress
    "solitaire": b"S\1\1\1\1\0\0\0" + bytes(8) + struct.pack("<6H3i2I", 5, 2, 1, 2, 200, 0, 1234, -100, 0, 0, 0)
                 + bytes(68),
    # 4x4 last played, with a game to continue: best 1000, score 500, 40 moves, a 2, a 4 and a 2
    "g2048": b"2\1\1" + bytes([0, 1, 0, 0, 0]) + struct.pack("<12I", 0, 1000, 0, 0, 0, 500, 0, 0, 0, 40, 0, 0)
             + bytes(9) + bytes([1, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1]) + bytes(61 + 2),
}
# games that keep a copy of their save in a Python script (the only files the
# NumWorks installer keeps): before the second session, as after installing
# the app again, only the scripts are left, and the save must come back
COPIES = {"championisland": "champion_saves.py", "numblocks": "numblocks_saves.py", "celeste": "celeste_saves.py",
          "hollowknight": "hollowknight_saves.py"}
CHECKS = {
    # the island remembers where Lucky was; after the reinstall, from the copy
    "championisland": (lambda v: v[:3] == b"CI1" and b"PLAYER_LOC" in v,) * 2,
    "numdrive": (lambda v: v[27] == 1, lambda v: v[27] == 0),  # last played level
    # a session saved (has_session at 1072, the play time at 1096); after the reinstall, from the copy, it goes on
    "celeste": (lambda v: v[:4] == b"SLEC" and v[1072] == 1 and struct.unpack_from("<I", v, 1096)[0] > 0,
                lambda v: v[:4] == b"SLEC" and v[1072] == 1 and struct.unpack_from("<I", v, 1096)[0] > 60),
    "chess": (lambda v: v == SEEDS["chess"],) * 2,  # read back and kept
    # a game saved ("HKSV", then the play time at 152); after the reinstall, from the copy, it goes on
    "hollowknight": (lambda v: v[:4] == b"HKSV" and struct.unpack_from("<f", v, 152)[0] > 0,
                     lambda v: v[:4] == b"HKSV" and struct.unpack_from("<f", v, 152)[0] > 10),
    "numvisuals": (lambda v: v[:2] == b"V\1" and v[3] == 4 and struct.unpack_from("<i", v, 8)[0] == 2,
                   lambda v: struct.unpack_from("<i", v, 8)[0] == 3),  # the add-on and its count
    "flappybird": (lambda v: len(v) == 26 and v[:3] == b"F\1\1" and v[8] == 3,  # Custom, Speed Fast
                   lambda v: v[2] == 1 and v[8] == 4),  # read back, Speed Insane
    # the game went on (more points, fewer dots), then Fast was chosen and the game kept
    "pacman": (lambda v: v[:2] == b"P\1" and v[12:14] == bytes([1, 2]) and struct.unpack_from("<I", v, 16)[0] > 5000
               and struct.unpack_from("<I", v, 8)[0] == 9000 and sum(bin(b).count("1") for b in v[20:52]) < 244,
               lambda v: v[2] == 2 and v[12:14] == bytes([1, 2]) and struct.unpack_from("<I", v, 16)[0] > 5000),
    # the default options and a best of 1 (Classic, 1 fruit, normal speed and size); then a banana
    "snake": (lambda v: len(v) == 658 and v[:9] == b"S\2" + bytes(7) and struct.unpack_from("<H", v, 10)[0] == 1,
              lambda v: len(v) == 658 and v[2] == 1 and struct.unpack_from("<H", v, 10)[0] == 1),
    "connectfour": (lambda v: v == b"C\1\1\2\1\0\5\1" + bytes(8),  # vs computer, 2 players, Normal, light
                    lambda v: v[5] == 1 and v[6] == 5),  # dark now; the tally's settings read back
    # the seeded options made a Vegas draw-1 game (-$52), counted as played; two moves, then three
    "solitaire": (lambda v: len(v) == 116 and v[:4] == b"S\1\1\1" and v[6] == 1 and v[8:10] == b"\1\1"
                  and struct.unpack_from("<2H", v, 16) == (6, 2) and struct.unpack_from("<H", v, 26)[0] == 2
                  and struct.unpack_from("<2i", v, 32) == (-100, -52),
                  lambda v: v[6] == 1 and struct.unpack_from("<H", v, 16)[0] == 6
                  and struct.unpack_from("<H", v, 26)[0] == 3),
    # continued (moves 40 + 2, then + 3), still in progress, score and best kept or higher
    "g2048": (lambda v: v[:3] == b"2\1\1" and v[4] & 1 and struct.unpack_from("<I", v, 44)[0] == 42
              and struct.unpack_from("<I", v, 28)[0] >= 500 and struct.unpack_from("<I", v, 12)[0] >= 1000,
              lambda v: v[4] & 1 and struct.unpack_from("<I", v, 44)[0] == 45),
    # a Beginner game kept (9x9, 10 mines, cursor moved right), one game played; then two
    "minesweeper": (lambda v: len(v) == 276 and v[:2] == b"M\1" and v[32:34] == bytes([9, 9]) and v[28] == 10
                    and v[34] == 5 and struct.unpack_from("<H", v, 14)[0] == 1,
                    lambda v: v[32] == 9 and struct.unpack_from("<H", v, 14)[0] == 2),
    # the world's record: its magic, then (offset 16) the time of day, which goes on from one session to the next
    "numblocks": (lambda v: v[:4] == b"NBW3" and struct.unpack_from("<I", v, 16)[0] > 0,
                  lambda v: v[:4] == b"NBW3" and struct.unpack_from("<I", v, 16)[0] > 60),
    # a stand bought (offset 32: businesses owned), the Biz tab; then Continue and the Upgrades tab (offset 4)
    "tycoon": (lambda v: len(v) == 168 and v[:2] == b"T\1" and v[7] == 1 and v[4] == 0
               and struct.unpack_from("<I", v, 32)[0] == 1,
               lambda v: v[7] == 1 and v[4] == 1 and struct.unpack_from("<I", v, 32)[0] == 1),
    "breakout": (lambda v: len(v) == 12 and v[:2] == b"B\2" and v[4] == 1 and struct.unpack_from("<I", v, 8)[0] >= 10,
                 lambda v: v[4] == 0 and struct.unpack_from("<I", v, 8)[0] >= 1),  # best kept, shake off
}


def records(buf):
    out, p = [], 0
    while p + 2 <= len(buf):
        n, = struct.unpack_from("<H", buf, p)
        if n == 0:
            return out
        if n < 4 or p + n > len(buf):
            raise ValueError(f"corrupt record at {p}")
        name = buf[p + 2:buf.index(b"\0", p + 2)].decode("latin1")
        out.append((name, bytes(buf[p + 3 + len(name):p + n])))
        p += n
    raise ValueError("no end of the record list")


def through_launcher(index, keys, home):
    """The same session, started from NumPlay's carousel (game `index`)."""
    lead = [(1200 + 300 * i, 1290 + 300 * i, K["right"]) for i in range(index)]
    ok = 1500 + 300 * index
    lead.append((ok, ok + 90, K["ok"]))
    shift = ok + 800
    return lead + [(a + shift, b + shift, k) for a, b, k in keys], home + shift


def run_once(nwa, storage, keys, home, out, tag):
    c = emu.Calculator(nwa, storage_file=storage)
    c.enable_checks(reads=True)
    c.keys = keys + [(home, home + 400, K["home"])]
    c.pending_shots = [(home - 100, os.path.join(out, f"{tag}.png"))]
    c.run(home + 3000)
    problems = []
    if not c.exited:
        problems.append("did not quit on Home")
    if c.violations:
        problems += sorted(set(c.violations))[:8]
    if not c.app_ram_clean():
        problems.append("RAM not cleared on exit")
    if c.max_locks != 1 or c.locks != 0:
        problems.append(f"Home lock not held and released once (max {c.max_locks}, now {c.locks})")
    changed = c.firmware_ram_changes()
    if changed:
        problems.append(f"{len(changed)} words of the firmware's RAM changed, first at {changed[0]:#x}")
    stack = c.stack_used()
    if stack > 30 * 1024:
        problems.append(f"stack use {stack} bytes, close to the 32 KB limit")
    return c, problems, stack


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("apps")
    ap.add_argument("--game")
    ap.add_argument("--out", default="build/test_games")
    ap.add_argument("--numplay", help="play the games inside this NumPlay.nwa instead")
    a = ap.parse_args()
    order = [g["id"] for g in json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "games",
                                                          "games.json")))]
    os.makedirs(a.out, exist_ok=True)
    failed = False
    for name, (nwa, save, keys1, home1, keys2, home2) in GAMES.items():
        if a.game and a.game != name:
            continue
        path = os.path.join(a.apps, nwa)
        if a.numplay:  # Home in a game quits NumPlay altogether
            path = a.numplay
            keys1, home1 = through_launcher(order.index(name), keys1, home1)
            keys2, home2 = through_launcher(order.index(name), keys2, home2)
        with tempfile.TemporaryDirectory() as d:
            storage = os.path.join(d, "storage.bin")
            # the calculator already has a script
            pre = [("pi.py", b"\x01print(3.14159)\n")]
            if name in SEEDS:
                pre.append((save, SEEDS[name]))
            buf = bytearray(emu.STORAGE_SIZE)
            p = 0
            for rn, content in pre:
                size = 2 + len(rn) + 1 + len(content)
                struct.pack_into("<H", buf, p, size)
                buf[p + 2:p + size] = rn.encode() + b"\0" + content
                p += size
            open(storage, "wb").write(buf)
            c, problems, stack = run_once(path, storage, keys1, home1, a.out, f"{name}_1")
            recs = dict(records(open(storage, "rb").read()))
            if recs.get("pi.py") != pre[0][1]:
                problems.append("pi.py changed")
            if save and save not in recs:
                problems.append(f"{save} not written")
            elif name in CHECKS and not CHECKS[name][0](recs[save]):
                problems.append(f"{save} not updated")
            # a script added after the save, then a second session
            buf = bytearray(open(storage, "rb").read())
            lst = records(buf)
            if name in COPIES:
                copy = dict(lst).get(COPIES[name], b"")
                tag = b"#>" + save.encode() + b":"
                line = copy[copy.find(tag) + len(tag):].split(b"\n")[0] if tag in copy else b""
                if not line or __import__("base64").b64decode(line) != dict(lst).get(save):
                    problems.append(f"{COPIES[name]} does not hold {save}")
                lst = [(n, v) for n, v in lst if n.endswith(".py")]
                buf = bytearray(emu.STORAGE_SIZE)
                q = 0
                for n, v in lst:
                    struct.pack_into("<H", buf, q, 2 + len(n) + 1 + len(v))
                    buf[q + 2:q + 3 + len(n) + len(v)] = n.encode() + b"\0" + v
                    q += 2 + len(n) + 1 + len(v)
            p = sum(2 + len(n) + 1 + len(v) for n, v in lst)
            late = b"\x01print('later')\n"
            size = 2 + len("late.py") + 1 + len(late)
            struct.pack_into("<H", buf, p, size)
            buf[p + 2:p + size] = b"late.py\0" + late
            struct.pack_into("<H", buf, p + size, 0)
            open(storage, "wb").write(buf)
            c2, problems2, stack2 = run_once(path, storage, keys2, home2, a.out, f"{name}_2")
            problems += [f"second run: {x}" for x in problems2]
            try:
                recs2 = dict(records(open(storage, "rb").read()))
                if recs2.get("pi.py") != pre[0][1] or recs2.get("late.py") != late:
                    problems.append("second run: other files changed")
                if save and save not in recs2:
                    problems.append(f"second run: {save} lost")
                elif name in CHECKS and not CHECKS[name][1](recs2[save]):
                    problems.append(f"second run: {save} not updated")
            except ValueError as e:
                problems.append(f"second run: file system corrupt ({e})")
        status = "ok" if not problems else "FAIL"
        failed |= bool(problems)
        print(f"{name:11s} {status}  stack {max(stack, stack2)} bytes, files {sorted(recs)}", flush=True)
        for x in problems:
            print("   ", x)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
