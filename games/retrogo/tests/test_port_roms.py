#!/usr/bin/env python3
"""The other targets' games (src/roms_port.c reads them from a folder) made as tools/romtable.py makes them for the
calculator: each game's system, name and bytes the same, Game Boy and Sega 8-bit files without their padding, a .gw
file unpacked, Mega Drive files (plain, odd-sized, .smd) plain and swapped, a TI-Nspire's .tns ending left out.

usage: test_port_roms.py HOST_PORT_ROMS"""
import os
import random
import struct
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(here, "..", "..", "..", "tools"))
import romtable  # noqa: E402

reader = sys.argv[1]
tmp = tempfile.mkdtemp()
folder = os.path.join(tmp, "retrogo")
os.makedirs(folder)
rnd = random.Random(7)
failures = []


def expect(what, ok):
    print(("ok   " if ok else "FAIL ") + what)
    if not ok:
        failures.append(what)


def noise(n):
    return bytes(rnd.randrange(256) for _ in range(n))


def lz4_frame(data):
    """an LZ4 frame as LCD-Game-Shrinker writes them, in two blocks: literals, and matches 1 or 3 bytes back"""
    def length(out, k):
        while k >= 255:
            out.append(255)
            k -= 255
        out.append(k)

    def block(chunk):
        out, i, lit = bytearray(), 0, bytearray()
        while i < len(chunk):
            best = (0, 0)
            for off in (1, 3):
                n = 0
                while i - off >= 0 and i + n < len(chunk) - 5 and chunk[i + n] == chunk[i + n - off]:
                    n += 1
                best = max(best, (n, off))
            if best[0] < 4:
                lit.append(chunk[i])
                i += 1
                continue
            n, ml = len(lit), best[0] - 4
            out.append((min(n, 15) << 4) | min(ml, 15))
            if n >= 15:
                length(out, n - 15)
            out += lit + struct.pack("<H", best[1])
            if ml >= 15:
                length(out, ml - 15)
            lit, i = bytearray(), i + best[0]
        out.append(min(len(lit), 15) << 4)
        if len(lit) >= 15:
            length(out, len(lit) - 15)
        return bytes(out + lit)
    half = len(data) // 2
    f = bytearray(b"\x04\x22\x4d\x18\x60\x40\x82")
    for chunk in (data[:half], data[half:]):
        b = block(chunk)
        f += struct.pack("<I", len(b)) + b
    f += struct.pack("<I", 0)
    return bytes(f)


def smd(plain):
    """a plain Mega Drive game as an .smd file: a 512-byte header, then each 16 KB its odd bytes then its even ones"""
    out = bytearray(512)
    out[1], out[8], out[9] = 3, 0xAA, 0xBB
    for k in range(0, len(plain), 0x4000):
        blk = plain[k:k + 0x4000]
        out += blk[1::2] + blk[0::2]
    return bytes(out)


gw_plain = bytes([1, 2, 3]) * 1000 + noise(5000) + bytes(900) + noise(77)
files = {
    "Padded.gb": noise(0x8000) + b"\xff" * 0x18000,
    "Color.gbc": noise(0x14000) + b"\x00" * 0xC000,
    "Small.gb": noise(0x8000),
    "Sega.sms": noise(0x10000) + b"\xff" * 0x30000,
    "Handheld.gg.tns": noise(0xC000) + b"\x00" * 0x4000,
    "Thousand.sg": noise(0x6000) + b"\xff" * 0x2000,
    "Nes.nes": noise(0x6010),
    "Coleco.col": noise(0x2000) + b"\xff" * 0x2000,
    "Watch.gw": lz4_frame(gw_plain),
    "Plain.gw": noise(3000),
    "Genesis.md": noise(0x20000),
    "Odd.gen": noise(0x1001),
    "Bin.bin.tns": noise(0x800),
    "Interleaved.smd": smd(noise(0x8000)),
}
for name, data in files.items():
    open(os.path.join(folder, name), "wb").write(data)
open(os.path.join(folder, "notes.txt"), "wb").write(b"not a game")

out = os.path.join(tmp, "t.bin")
r = subprocess.run([reader, folder, out], capture_output=True, text=True, timeout=60)
expect("the folder read", r.returncode == 0)
t = open(out, "rb").read() if r.returncode == 0 else b"\0" * 12
magic, version, count = struct.unpack_from("<III", t)
expect(f"{count} games, the text file left out", (magic, version, count) == (romtable.MAGIC, romtable.VERSION, len(files)))
got = {}
for i in range(count):
    off, size, system = struct.unpack_from("<IIB", t, 12 + romtable.ENTRY * i)
    name = t[12 + romtable.ENTRY * i + 12:12 + romtable.ENTRY * (i + 1)].rstrip(b"\0").decode()
    got[name] = (system, t[off:off + size])
for name, data in files.items():
    real = name[:-4] if name.endswith(".tns") else name
    path = os.path.join(tmp, real)
    open(path, "wb").write(data)
    ext = os.path.splitext(real)[1].lower()
    want = (romtable.SYSTEMS[ext], romtable.rom_bytes(path, romtable.SYSTEMS[ext]))
    key = os.path.splitext(real)[0]
    expect(f"{name}: system {want[0]}, {len(want[1])} bytes as the calculator's", got.get(key) == want)
expect("the .gw file unpacked", got.get("Watch", (0, b""))[1] == gw_plain)
expect("the padding left out", len(got.get("Padded", (0, b""))[1]) == 0x8000 and len(got.get("Sega", (0, b""))[1]) == 0x10000)

if failures:
    print(f"{len(failures)} failed")
    sys.exit(1)
print("port roms: all good")
