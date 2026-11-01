#!/usr/bin/env python3
"""Retro-Go's emulators on this computer (tests/host_core.c, built with the address and undefined
behaviour sanitizers, the ROM read only like the calculator's flash):

- test ROMs give the pictures they should (Blargg's cpu_instrs and instr_timing, dmg-acid2 and
  cgb-acid2, nestest, zexdoc for the Master System, Cheril Perils Classic for the SG-1000), when they are in
  the test ROM folder, and the
  Master System's Z80 runs the CP/M instruction exercisers (prelim; zexdoc and zexall with ZEX=1)
  to the clock;
- every ROM there plays a few hundred runs of random keys without a crash;
- broken ROMs (random bytes, headers made up, files cut short) never crash either.

usage: test_cores.py HOST_CORE [TEST_ROM_FOLDER] [HOST_Z80]
Only freely distributable test ROMs and homebrew go in the folder (see games/retrogo/README.md)."""
import hashlib
import os
import random
import struct
import subprocess
import sys
import tempfile

core = sys.argv[1]
folder = sys.argv[2] if len(sys.argv) > 2 else ""
z80 = sys.argv[3] if len(sys.argv) > 3 else ""
tmp = tempfile.mkdtemp()
failures = []

# the pictures the test ROMs end on (checked by eye: "Passed all tests", the faces, all OK)
EXPECTED = {
    "blargg/cpu_instrs/cpu_instrs.gb": (3600, "", "a19050f9b9ef"),
    "blargg/instr_timing/instr_timing.gb": (600, "", "88e27ef287f2"),
    "dmg-acid2.gb": (120, "", "1b496ea223ab"),
    "cgb-acid2.gbc": (120, "", "3e99d1d0cb67"),
    "nestests/nes-test-roms-master/other/nestest.nes": (400, "30=8,35=0", "9c699fb5f8ff"),
    "sega/zexall/x/zexdoc.sms": (3000, "", "e20e5a6fb45d"),
    # (the SG-1000: the Mojon Twins' homebrew, its title, the menu, then Cheril in the first room)
    "sg1000/cheril_perils_ntsc.sg": (1500, "900=2,910=0,1000=2,1010=0,1100=128,1300=129,1320=128", "fd4688405166"),
}
# the CP/M exercisers and the clocks superzazu's z80 takes for them
ZEX = [("z80/prelim.com", 8721), ("z80/zexdoc.cim", 46734978649), ("z80/zexall.cim", 46734978649)]
SYSTEMS = (".gb", ".gbc", ".nes", ".sms", ".gg", ".sg")


def run(rom, frames, seed=0, script=""):
    out = os.path.join(tmp, "f.ppm")
    if os.path.exists(out):
        os.remove(out)
    r = subprocess.run([core, rom, str(frames), out, str(seed), script], capture_output=True, text=True,
                       timeout=600)
    picture = open(out, "rb").read() if os.path.exists(out) else b""
    return r, picture


def check(name, r, allow_load_error=False):
    if r.returncode == 0 or (allow_load_error and "load failed" in r.stderr):
        return True
    failures.append(f"{name}: exit {r.returncode}: {r.stderr.strip()[-600:]}")
    print("FAIL", name, r.stderr.strip()[-600:])
    return False


# ---- the test ROMs' pictures
roms = []
if folder and os.path.isdir(folder):
    for rel, (frames, script, digest) in EXPECTED.items():
        path = os.path.join(folder, rel)
        if not os.path.exists(path):
            print("skip", rel)
            continue
        r, pic = run(path, frames, 0, script)
        got = hashlib.sha1(pic).hexdigest()[:12]
        if check(rel, r) and got != digest:
            failures.append(f"{rel}: picture {got}, expected {digest}")
            print("FAIL", rel, "picture", got)
        else:
            print("ok  ", rel)
    for root, _, files in os.walk(folder):
        for f in sorted(files):
            if f.lower().endswith(SYSTEMS):
                roms.append(os.path.join(root, f))

# ---- the Z80 instruction exercisers
if z80 and folder:
    for rel, clocks in ZEX[:1] + (ZEX[1:] if os.environ.get("ZEX") else []):
        path = os.path.join(folder, rel)
        if not os.path.exists(path):
            print("skip", rel)
            continue
        r = subprocess.run([z80, path, str(clocks)], capture_output=True, text=True, timeout=1200)
        if r.returncode != 0 or "ERROR" in r.stdout:
            failures.append(f"{rel}: {r.stdout.strip()[-600:]}")
            print("FAIL", rel, r.stdout.strip()[-600:])
        else:
            print("ok  ", rel, r.stdout.strip().splitlines()[-1])

# ---- save states: the machine goes on the same after its state is put back, and random states
# don't crash it
for rel in ("libbet.gb", "ucity_compat.gbc", "cgb-acid2.gbc", "thwaite.nes", "croom.nes", "240pee.nes",
            "sega/raposa/x/raposadosol_v20.sms", "sega/waternet/sms/waternet.sms", "sega/waternet/gg/waternet.gg",
            "sega/tunnel/tunnel.gg"):
    path = os.path.join(folder, rel) if folder else ""
    if not os.path.exists(path):
        continue
    r = subprocess.run([core, path, "300", os.path.join(tmp, "s.ppm"), "5"], capture_output=True, text=True,
                       timeout=600, env=dict(os.environ, STATE="1"))
    if check(rel + " (states)", r) and ("state same" not in r.stdout or "no crash" not in r.stdout):
        failures.append(f"{rel}: {r.stdout.strip()}")
        print("FAIL", rel, r.stdout.strip())
    elif r.returncode == 0:
        print("ok  ", rel, "states")

# ---- random keys on every ROM
runs = int(os.environ.get("FUZZ_RUNS", "300"))
rng = random.Random(1)
if roms:
    for k in range(runs):
        rom = roms[k % len(roms)]
        r, _ = run(rom, rng.randint(30, 400), rng.randint(1, 1 << 30))
        check(f"{os.path.relpath(rom, folder)} (random keys, run {k})", r, allow_load_error=True)
    print(f"ok   {runs} runs of random keys on {len(roms)} ROMs")

# ---- broken ROMs
def write(name, data):
    path = os.path.join(tmp, name)
    open(path, "wb").write(data)
    return path


broken = []
for k in range(160):
    kind = k % 8
    if kind == 0:  # random bytes, a GB header's size
        data = bytes(rng.getrandbits(8) for _ in range(rng.choice([0x8000, 0x10000, 0x20000])))
        broken.append(write(f"rand{k}.gb", data))
    elif kind == 1:  # a made up GB header: any cartridge type, ROM and RAM size
        data = bytearray(rng.getrandbits(8) for _ in range(0x8000))
        data[0x143] = rng.choice([0, 0x80, 0xC0])
        data[0x147], data[0x148], data[0x149] = rng.getrandbits(8), rng.getrandbits(8), rng.getrandbits(8)
        broken.append(write(f"hdr{k}.gbc", bytes(data)))
    elif kind == 2:  # a GB ROM cut short
        broken.append(write(f"short{k}.gb", bytes(rng.getrandbits(8) for _ in range(rng.randint(0, 0x9000)))))
    elif kind == 3:  # an iNES header with any mapper and sizes, random contents
        prg, chr_ = rng.randint(0, 8), rng.randint(0, 8)
        head = b"NES\x1a" + bytes([prg, chr_, rng.getrandbits(8), rng.getrandbits(8)]) + bytes(8)
        body = bytes(rng.getrandbits(8) for _ in range(prg * 0x4000 + chr_ * 0x2000 + rng.choice([0, 512])))
        broken.append(write(f"ines{k}.nes", head + body))
    elif kind == 4:  # an iNES file cut short
        prg, chr_ = rng.randint(1, 8), rng.randint(0, 8)
        head = b"NES\x1a" + bytes([prg, chr_, rng.getrandbits(8) & 0xF4, 0]) + bytes(8)
        body = bytes(rng.getrandbits(8) for _ in range(rng.randint(0, prg * 0x4000 + chr_ * 0x2000)))
        broken.append(write(f"cut{k}.nes", head + body))
    elif kind == 5:  # random bytes as an NES file
        broken.append(write(f"rand{k}.nes", bytes(rng.getrandbits(8) for _ in range(rng.randint(0, 0x30000)))))
    elif kind == 6:  # random bytes as a Master System or Game Gear game, any size, a Codemasters header maybe
        data = bytearray(rng.getrandbits(8) for _ in range(rng.choice([0x400, 0x2000, 0x8000, 0xC000, 0x20000, 0x40200])))
        if len(data) > 0x8000 and k % 2:
            data[0x7FE6:0x7FEA] = bytes([0x34, 0x12, 0xCC, 0xED])
        broken.append(write(f"rand{k}." + rng.choice(["sms", "gg"]), bytes(data)))
    else:  # a Master System file cut short
        broken.append(write(f"short{k}.sms", bytes(rng.getrandbits(8) for _ in range(rng.randint(0, 0x5000)))))
for path in broken:
    r, _ = run(path, rng.randint(20, 120), rng.randint(1, 1 << 30))
    check(os.path.basename(path), r, allow_load_error=True)
print(f"ok   {len(broken)} broken ROMs")

if failures:
    print(f"{len(failures)} failures")
    sys.exit(1)
print("all good")
