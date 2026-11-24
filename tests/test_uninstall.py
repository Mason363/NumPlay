#!/usr/bin/env python3
"""Uninstalls a game from NumPlay.nwa running in the ARM emulator and checks
what happened to the flash and to the calculator's files.

- the game's block of flash is wiped: whole sectors erased, the shared ends
  zeroed, and not one byte outside the block changed;
- the other games are intact and still start;
- the game's save file is gone, the other files are untouched, and Epsilon's
  record lookup cache no longer points into moved data;
- relaunching NumPlay shows the game gone and does not touch the flash again.

Usage: test_uninstall.py build/compiled/NumPlay.nwa [--out DIR]
"""
import argparse
import json
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import emu  # noqa: E402

SECTOR = 0x10000


def fail(msg):
    print("FAIL:", msg)
    sys.exit(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("nwa")
    ap.add_argument("--out", default="build/test_uninstall")
    ap.add_argument("--game", default="crossyroad")
    ap.add_argument("--cut-power", type=int, default=0,
                    help="stop the calculator after this many flash operations, then check that the next launch "
                         "finishes the job")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    c = emu.Calculator(a.nwa)
    manifest = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "games", "games.json")
    manifest = json.load(open(manifest))
    games = [g["id"] for g in manifest if f"np_{g['id']}_begin" in c.symbols]  # (the games of this NumPlay)
    order = sorted(games, key=lambda g: c.symbols[f"np_{g}_begin"])
    target = a.game
    if target not in order:
        fail(f"{target} not in {order}")
    begin, end = c.symbols[f"np_{target}_begin"], c.symbols[f"np_{target}_end"]
    flash_before = bytes(c.uc.mem_read(emu.FLASH, emu.FLASH_SIZE))

    # the game's own files go with it; a script and another game's save stay
    own = next(g["records"] for g in manifest if g["id"] == target)
    # a name ending with '*' stands for every file starting so (NumBlocks' world)
    prefixes = [n[:-1] for n in own if n.endswith("*")]
    own = [n for n in own if not n.endswith("*")]
    save = own[0] if own else None
    other = ("crossyroad.sav", bytes(range(24))) if target == "tetris" else ("tetris.set", struct.pack("<3I", 1, 1, 1234))
    records = [("pi.py", b"\x01print(3.14)\n"), (save or "none.sav", bytes(range(40))),
               ("script.py", b"\x01import math\nprint(math.e)\n"), other]
    records[3:3] = [(n, bytes(range(12))) for n in own[1:2]]
    records[3:3] = [(p + s, bytes(range(16))) for p in prefixes for s in ("0_0.nbe", "-1_2.nbe")]
    c.set_records(records, cached="script.py")

    # Home: the settings card is last. Settings: the games are listed in order,
    # each with Reset and Uninstall.
    installed = [g for g in order]
    row = installed.index(target) + (3 if os.path.basename(a.nwa) == "TwiliPlay.nwa" else 0)
    t = 1500
    keys = []
    for _ in range(len(installed)):
        keys.append((t, t + 90, emu.KEYS["right"]))
        t += 300
    t += 800
    keys.append((t, t + 90, emu.KEYS["ok"]))
    t += 1000
    for _ in range(row):
        keys.append((t, t + 90, emu.KEYS["down"]))
        t += 300
    t += 500
    keys.append((t, t + 90, emu.KEYS["right"]))      # Uninstall, next to Reset on the row
    t += 500
    keys.append((t, t + 90, emu.KEYS["ok"]))         # open the warning
    dialog = t + 600
    t += 1000
    keys.append((t, t + 90, emu.KEYS["right"]))      # choose Uninstall
    hold = t + 500
    keys.append((hold, hold + 1800, emu.KEYS["ok"]))  # hold OK to confirm
    t = hold + 1800 + 5000 + 400 * ((end - begin) // SECTOR)  # (a big game's sectors take a while to erase)
    done = t - 200
    keys.append((t, t + 90, emu.KEYS["ok"]))          # dismiss "was uninstalled"
    t += 1000
    listing = t - 100
    keys.append((t, t + 90, emu.KEYS["back"]))        # back to the carousel
    t += 1500
    home = t
    keys.append((t, t + 90, emu.KEYS["home"]))        # quit NumPlay
    c.keys = keys
    shots = {dialog: "1_warning", hold + 900: "2_holding", done: "3_done", listing: "4_list", home: "5_home"}
    c.pending_shots = sorted((ms, os.path.join(a.out, name + ".png")) for ms, name in shots.items())
    if a.cut_power:
        original = c._intr

        def intr(uc, intno, data):
            original(uc, intno, data)
            if len(c.flash_log) >= a.cut_power and not c.exited:
                c.exited = True  # power cut: nothing more runs
                uc.emu_stop()
        c.uc.hook_del(c.intr_hook)
        c.intr_hook = c.uc.hook_add(emu.UC_HOOK_INTR, intr)
        c.run(t + 3000)
        if len(c.flash_log) < a.cut_power:
            fail("the uninstall did not reach the power cut")
        print(f"power cut after {len(c.flash_log)} flash operations")
        # power back on: RAM is gone, flash stays; NumPlay starts and should finish the job
        c.uc.hook_del(c.intr_hook)
        c.intr_hook = c.uc.hook_add(emu.UC_HOOK_INTR, original)
        c.exited = False
        c.locks = c.max_locks = 0  # the kernel starts afresh too
        ops = len(c.flash_log)
        c.pc = c.entry | 1
        c.uc.reg_write(emu.UC_ARM_REG_SP, emu.STACK_TOP)
        c.uc.reg_write(emu.UC_ARM_REG_LR, emu.EXIT_HOOK | 1)
        start = c.now_ms
        c.keys = [(start + 2500, start + 2590, emu.KEYS["home"])]
        c.pending_shots = [(start + 2400, os.path.join(a.out, "after_power_cut.png"))]
        c.run(start + 4000)
        print(f"next launch: {len(c.flash_log) - ops} flash operations to finish the uninstall")
    else:
        c.run(t + 3000)
    if not c.exited:
        fail("NumPlay did not quit")

    # ---- flash
    flash = bytes(c.uc.mem_read(emu.FLASH, emu.FLASH_SIZE))
    s0 = (begin + SECTOR - 1) & ~(SECTOR - 1)
    s1 = end & ~(SECTOR - 1)
    if s1 < s0:
        s0 = s1 = end
    off = lambda x: x - emu.FLASH  # noqa: E731
    if any(flash[off(begin):off(s0)]):
        fail("the start of the block is not zeroed")
    if any(b != 0xFF for b in flash[off(s0):off(s1)]):
        fail("whole sectors of the block are not erased")
    if any(flash[off(s1):off(end)]):
        fail("the end of the block is not zeroed")
    outside = [i for i in range(len(flash)) if flash[i] != flash_before[i] and not (off(begin) <= i < off(end))]
    if outside:
        fail(f"{len(outside)} bytes changed outside the block, first at {emu.FLASH + outside[0]:#x}")
    for op in c.flash_log:
        if not op[-1]:
            fail(f"flash operation refused: {op}")
        if op[0] == "erase" and not (begin <= op[2] and op[2] + op[3] <= end):
            fail(f"erased a sector outside the block: {op}")
    for g in order:
        if g == target:
            continue
        b, e = c.symbols[f"np_{g}_begin"], c.symbols[f"np_{g}_end"]
        if struct.unpack_from("<I", flash, off(b))[0] != 0x3147504E or struct.unpack_from("<I", flash, off(e) - 4)[0] != 0x444E4550:
            fail(f"{g} lost its marks")
    # the session holds Home back, and the uninstall once more inside it
    if c.locks != 0 or c.max_locks != 2:
        fail(f"Home interrupt lock not balanced (depth {c.locks}, max {c.max_locks})")
    if not c.app_ram_clean():
        fail("NumPlay did not clear its RAM when quitting")
    erased = sum(1 for op in c.flash_log if op[0] == "erase")
    print(f"flash: {target} block {begin:#x}-{end:#x} ({(end - begin) / 1024:.1f} KB) wiped, "
          f"{erased} sectors erased, nothing else changed")

    # ---- files
    names = [n for n, _ in c.records()]
    for name in own:
        if name in names:
            fail(f"{name} is still there")
    for p in prefixes:
        if any(n.startswith(p) for n in names):
            fail(f"a file starting with {p} is still there")
    buf = c.storage_bytes()
    for name, content in records:
        if name in own or any(name.startswith(p) for p in prefixes):
            continue
        if name not in names:
            fail(f"{name} disappeared")
        p = 0
        while True:
            size, = struct.unpack_from("<H", buf, p)
            if buf[p + 2:p + 2 + len(name) + 1] == name.encode() + b"\0":
                if buf[p + 3 + len(name):p + size] != content:
                    fail(f"{name} changed")
                break
            p += size
    crc, ptr = c.cache()
    if save and (crc or ptr):
        fail("Epsilon's record cache still points into the moved records")
    print(f"files: {save or 'no save'} deleted, others intact ({names}), lookup cache cleared")

    # ---- relaunch: the game is gone, nothing is written to flash
    log = len(c.flash_log)
    c.exited = False
    c.pc = c.entry | 1
    c.uc.reg_write(emu.UC_ARM_REG_SP, emu.STACK_TOP)
    c.uc.reg_write(emu.UC_ARM_REG_LR, emu.EXIT_HOOK | 1)
    start = c.now_ms
    c.keys = [(start + 1500, start + 1590, emu.KEYS["home"])]
    c.pending_shots = [(start + 1400, os.path.join(a.out, "6_relaunch.png"))]
    c.run(start + 3000)
    if len(c.flash_log) != log:
        fail("the relaunch wrote to flash")
    if not c.exited:
        fail("NumPlay did not quit after the relaunch")
    print("relaunch: ok, no flash writes")
    print("PASS")


if __name__ == "__main__":
    main()
