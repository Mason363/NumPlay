#!/usr/bin/env python3
"""Retro-Go in tools/emu.py, with three small games this test makes: a Game Boy cartridge with battery
RAM, an NES one and a Master System one, each counting its frames in that RAM. For each: a new game,
Home (the battery save and a state are written), then Resume from the state and On/Off: the count
goes on from where it was. All along: what tests/test_games.py checks (Home and On/Off quit, the
RAM handed back clean, no write to flash, so nothing written into a cartridge read in place, no
unaligned multi-word read of the file system, the firmware's RAM untouched, the stack).

Then the keys: a Game Boy cartridge that keeps the buttons it saw, A given 7 and the D-pad 8 2 4 6 in Controls,
and 7 and 8 held in the game: it saw A and Up.

usage: test_app.py [--keep DIR]   (builds output/test/retrogo-full.nwa with ROMS set to the three games)"""
import argparse
import os
import struct
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
game = os.path.dirname(here)
sys.path.insert(0, os.path.join(game, "..", "..", "tools"))
import emu  # noqa: E402

K = emu.KEYS


def gb_rom():
    """MBC1 + RAM + battery, 8 KB of RAM: a 16-bit count at $A000, one more each frame."""
    rom = bytearray(0x8000)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # nop; jp $0150
    rom[0x134:0x13C] = b"COUNTER\0"
    rom[0x147], rom[0x148], rom[0x149] = 0x03, 0x00, 0x02
    code = [0x3E, 0x0A, 0xEA, 0x00, 0x00,  # ld a,$0A; ld ($0000),a: RAM on
            0x21, 0x00, 0xA0,              # loop: ld hl,$A000
            0x34, 0x20, 0x02, 0x23, 0x34,  # inc (hl); jr nz,+2; inc hl; inc (hl)
            0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA,  # wait for LY = 144
            0xF0, 0x44, 0xFE, 0x91, 0x20, 0xFA,  # then 145
            0x18, 0xEA]                    # jr loop
    rom[0x150:0x150 + len(code)] = bytes(code)
    return bytes(rom)


def pad_rom():
    """MBC1 + RAM + battery: the buttons seen pressed at $A000 (A 1, B 2, Select 4, Start 8), the D-pad at $A001
    (Right 1, Left 2, Up 4, Down 8), each frame."""
    rom = bytearray(0x8000)
    rom[0x100:0x104] = bytes([0x00, 0xC3, 0x50, 0x01])  # nop; jp $0150
    rom[0x134:0x13C] = b"PADS\0\0\0\0"
    rom[0x147], rom[0x148], rom[0x149] = 0x03, 0x00, 0x02
    code = [0x3E, 0x0A, 0xEA, 0x00, 0x00,  # ld a,$0A; ld ($0000),a: RAM on
            0xAF, 0xEA, 0x00, 0xA0, 0xEA, 0x01, 0xA0,  # xor a; ld ($A000),a; ld ($A001),a
            0x21, 0x00, 0xA0, 0x0E, 0x10,  # loop: ld hl,$A000; ld c,$10 (the buttons)
            0x79, 0xE0, 0x00, 0xF0, 0x00, 0xF0, 0x00,  # read: ld a,c; ldh ($00),a; ldh a,($00) twice
            0x2F, 0xE6, 0x0F, 0xB6, 0x77,  # cpl; and $0F; or (hl); ld (hl),a
            0x23, 0x0E, 0x20,              # inc hl; ld c,$20 (the D-pad)
            0x79, 0xE0, 0x00, 0xF0, 0x00, 0xF0, 0x00,
            0x2F, 0xE6, 0x0F, 0xB6, 0x77,
            0xF0, 0x44, 0xFE, 0x90, 0x20, 0xFA,  # wait for LY = 144
            0xF0, 0x44, 0xFE, 0x91, 0x20, 0xFA,  # then 145
            0x18, 0xD2]                    # jr loop
    rom[0x150:0x150 + len(code)] = bytes(code)
    return bytes(rom)


def nes_rom():
    """NROM, battery: a 16-bit count at $6000, one more each frame (each vertical blank)."""
    prg = bytearray(0x4000)
    code = [0x78, 0xD8, 0xA2, 0xFF, 0x9A,  # sei; cld; ldx #$FF; txs
            0xEE, 0x00, 0x60, 0xD0, 0x03, 0xEE, 0x01, 0x60,  # loop: inc $6000; bne +3; inc $6001
            0x2C, 0x02, 0x20, 0x10, 0xFB,  # wait: bit $2002; bpl wait
            0x4C, 0x05, 0xC0,              # jmp loop
            0x40]                          # (NMI, IRQ) rti
    prg[:len(code)] = bytes(code)
    struct.pack_into("<HHH", prg, 0x3FFA, 0xC015, 0xC000, 0xC015)
    return b"NES\x1a" + bytes([1, 1, 0x02, 0]) + bytes(8) + bytes(prg) + bytes(0x2000)


def sms_rom():
    """Sega's mapper with its cartridge RAM on: a 16-bit count at $8000, one more each frame (the
    line counter going past $C0)."""
    rom = bytearray(0x8000)
    code = [0xF3, 0x31, 0xF0, 0xDF,        # di; ld sp,$DFF0
            0x3E, 0x08, 0x32, 0xFC, 0xFF,  # ld a,$08; ld ($FFFC),a: the RAM at $8000
            0x21, 0x00, 0x80,              # loop: ld hl,$8000
            0x34, 0x20, 0x02, 0x23, 0x34,  # inc (hl); jr nz,+2; inc hl; inc (hl)
            0xDB, 0x7E, 0xFE, 0xC0, 0x20, 0xFA,  # wait for line $C0
            0xDB, 0x7E, 0xFE, 0xC1, 0x20, 0xFA,  # then $C1
            0x18, 0xEA]                    # jr loop
    rom[:len(code)] = bytes(code)
    rom[0x7FF0:0x7FF8] = b"TMR SEGA"
    return bytes(rom)


def unpack(data, size):
    """games/retrogo/src/pack.c's format"""
    out, i = bytearray(), 0
    while i < len(data):
        t = data[i]
        i += 1
        if t < 0x80:
            out += data[i:i + t + 1]
            i += t + 1
        else:
            n = (t & 127) + 3
            if t & 127 == 127:
                while True:
                    b = data[i]
                    i += 1
                    n += b
                    if b < 255:
                        break
            d = data[i] | data[i + 1] << 8
            i += 2
            for _ in range(n):
                out.append(out[-d - 1])
    assert len(out) == size
    return bytes(out)


def battery(recs, name):
    """the count in a battery save ("RGSR": header, part sizes, then the packed parts)"""
    v = recs[name]
    magic, version, system, parts, rom, seq, check = struct.unpack_from("<IBBHIII", v, 0)
    assert magic == 0x52534752 and version == 1, "not a battery save"
    raw, packed = struct.unpack_from("<II", v, 20)
    ram = unpack(v[20 + 8 * parts:20 + 8 * parts + packed], raw)
    return ram[0] | ram[1] << 8


def records(buf):
    out, p = {}, 0
    while p + 2 <= len(buf):
        n, = struct.unpack_from("<H", buf, p)
        if n == 0:
            return out
        name = buf[p + 2:buf.index(b"\0", p + 2)].decode("latin1")
        out[name] = bytes(buf[p + 3 + len(name):p + n])
        p += n
    raise ValueError("no end of the record list")


def session(nwa, storage, keys, quit_at, quit_key, problems, tag):
    c = emu.Calculator(nwa, storage_file=storage)
    c.enable_checks(reads=True)
    c.keys = emu.parse_keys(keys + f",{quit_at}-{quit_at + 400}:{quit_key}")
    c.run(quit_at + 3000)
    if not c.exited:
        problems.append(f"{tag}: did not quit on {quit_key}")
    problems += [f"{tag}: {v}" for v in sorted(set(c.violations))[:8]]
    if not c.app_ram_clean():
        problems.append(f"{tag}: RAM not cleared on exit")
    if c.max_locks != 1 or c.locks != 0:
        problems.append(f"{tag}: Home lock not held and released once")
    if c.firmware_ram_changes():
        problems.append(f"{tag}: the firmware's RAM changed")
    if c.stack_used() > 30 * 1024:
        problems.append(f"{tag}: stack use {c.stack_used()} bytes")
    return records(open(storage, "rb").read()), c.stack_used()


def keys_test(out, problems):
    rom = os.path.join(out, "Pads.gb")
    open(rom, "wb").write(pad_rom())
    nwa = os.path.join(out, "retrogo-pads.nwa")
    subprocess.run(["make", "-C", game, "output/retrogo-full.nwa", "ROMS=" + rom,
                    "NWLINK=" + os.environ.get("NWLINK", "nwlink")], check=True, stdout=subprocess.DEVNULL)
    os.replace(os.path.join(game, "output", "retrogo-full.nwa"), nwa)
    with tempfile.TemporaryDirectory() as d:
        storage = os.path.join(d, "storage.bin")
        # Backspace, Controls (on OK: five up is A), 7; up to the D-pad, 8 2 4 6; Back. Then the game: its tab, the
        # game, a new game, the controls the first time; 7 and 8 held
        t, keys = 600, []
        for k in ["backspace", "down", "ok"] + ["up"] * 5 + ["ok", "7", "up", "ok", "8", "2", "4", "6", "back",
                                                              "ok", "ok", "ok", "ok"]:
            keys.append(f"{t}-{t + 100}:{k}")
            t += 400
        keys += [f"{t + 500}-{t + 1100}:7", f"{t + 1500}-{t + 2100}:8"]
        recs, _ = session(nwa, storage, ",".join(keys), t + 2600, "home", problems, "Keys")
        cfg = recs.get("retrogo.cfg", b"")
        if cfg[21:29] != bytes([31, 43, 36, 38, 30, 5, 12, 52]):
            problems.append(f"Keys: retrogo.cfg holds the keys {list(cfg[21:29])}")
        srm = [n for n in recs if n.endswith(".srm")]
        seen = battery(recs, srm[0]) if srm else -1
        if seen != 0x0401:
            problems.append(f"Keys: the game saw buttons {seen & 255:#x} and the D-pad {seen >> 8:#x}, not A and Up")
        print(f"{'ok  ' if not problems else 'FAIL'} Keys: A on 7 and the D-pad on 8 2 4 6, the game saw A and Up")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--keep")
    a = ap.parse_args()
    out = os.path.join(game, "output", "test")
    os.makedirs(out, exist_ok=True)
    roms = [os.path.join(out, "Counter.gb"), os.path.join(out, "Counter.nes"), os.path.join(out, "Counter.sms")]
    open(roms[0], "wb").write(gb_rom())
    open(roms[1], "wb").write(nes_rom())
    open(roms[2], "wb").write(sms_rom())
    nwa = os.path.join(out, "retrogo-full.nwa")
    if os.path.exists(os.path.join(game, "output", "retrogo-full.nwa")):
        os.remove(os.path.join(game, "output", "retrogo-full.nwa"))
    subprocess.run(["make", "-C", game, "output/retrogo-full.nwa", "ROMS=" + " ".join(roms),
                    "NWLINK=" + os.environ.get("NWLINK", "nwlink")], check=True, stdout=subprocess.DEVNULL)
    os.replace(os.path.join(game, "output", "retrogo-full.nwa"), nwa)
    problems = []
    with tempfile.TemporaryDirectory() as d:
        storage = os.path.join(a.keep or d, "storage.bin")
        if os.path.exists(storage):
            os.remove(storage)
        stack = 0
        # (the tabs: NES, Game Boy, Master System; the launcher starts on the NES tab, then where it was left)
        games = (("Game Boy", 1, "300-400:right,"),                         # NES tab -> Game Boy
                 ("NES", 3, "300-400:back,600-700:left,"),                  # Game Boy list -> NES tab
                 ("Master System", 4, "300-400:back,600-700:right,800-850:right,"))  # NES list -> Master System
        for k, (label, system, to_tab) in enumerate(games):
            # a new game (the controls the first time), then Home
            first = f"{to_tab}900-1000:ok,1500-1600:ok,2100-2200:ok" + (",2700-2800:ok" if not k else "")
            recs, s = session(nwa, storage, first, 6000, "home", problems, f"{label}, new game")
            stack = max(stack, s)
            srm = [n for n in recs if n.startswith("rg_") and n.endswith(".srm")]
            st = [n for n in recs if n.startswith("rg_") and n.endswith(".st0")]
            if len(srm) != k + 1 or len(st) != k + 1:
                problems.append(f"{label}: saves after Home: {sorted(recs)}")
                continue
            name = [n for n in srm if recs[n][5] == system][0]  # (its system byte)
            count1 = battery(recs, name)
            # Resume (the file menu's first choice), the slot last used, then On/Off
            recs, s = session(nwa, storage, "900-1000:ok,1500-1600:ok,2100-2200:ok", 5000, "onoff", problems,
                              f"{label}, resumed")
            stack = max(stack, s)
            count2 = battery(recs, name)
            if not 30 < count1 < count2:
                problems.append(f"{label}: the count went {count1}, then {count2}")
            print(f"{'ok  ' if not problems else 'FAIL'} {label}: counted {count1} frames, then {count2} after Resume")
    print(f"     stack {stack} bytes at most, records {sorted(recs)}")
    keys_test(out, problems)
    for p in problems:
        print("   ", p)
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
