#!/usr/bin/env python3
"""Retro-Go with big Game Boy Color games, in tools/emu.py as with Enlarged Extra Apps (nwagyu's Nwagra): the
cartridges tests/bigrom.py makes, 2 MB with an MBC3 (as Pokemon Gold) and 4 MB with an MBC5, each in a RetroGo.nwa
of its own. Each is started and must show green: every bank read back right through its bank controller, its battery
RAM held, and (the MBC3) its clock running; red or yellow is a bank or the RAM read wrong.

usage: test_big.py   (builds output/test/retrogo-big<N>.nwa)"""
import os
import subprocess
import sys
import tempfile

here = os.path.dirname(os.path.abspath(__file__))
game = os.path.dirname(here)
sys.path.insert(0, os.path.join(game, "..", "..", "tools"))
sys.path.insert(0, here)
import bigrom  # noqa: E402
import emu  # noqa: E402

K = emu.KEYS


def colours(c):
    """how much of the screen is green, red and yellow (the cartridge's RGB555 colours, as drawn)"""
    n = green = red = yellow = 0
    for i in range(0, len(c.screen), 2):
        px = c.screen[i] | c.screen[i + 1] << 8
        r, g, b = (px >> 11) & 31, (px >> 5) & 63, px & 31
        n += 1
        green += g > 50 and r < 10
        red += r > 25 and g < 20
        yellow += r > 25 and g > 50 and b < 10
    return green / n, red / n, yellow / n


def main():
    out = os.path.join(game, "output", "test")
    os.makedirs(out, exist_ok=True)
    problems = []
    emu.configure(xl=True)
    for mb in (2, 4):
        rom = os.path.join(out, f"Big{mb}.gbc")
        open(rom, "wb").write(bigrom.rom(mb))
        nwa = os.path.join(out, f"retrogo-big{mb}.nwa")
        subprocess.run(["make", "-C", game, "output/retrogo-full.nwa", "ROMS=" + rom,
                        "NWLINK=" + os.environ.get("NWLINK", "nwlink")], check=True, stdout=subprocess.DEVNULL)
        os.replace(os.path.join(game, "output", "retrogo-full.nwa"), nwa)
        with tempfile.TemporaryDirectory() as d:
            c = emu.Calculator(nwa, storage_file=os.path.join(d, "storage.bin"))
            # (the Game Boy Color's tab, its game, a new game, past the controls)
            c.keys = [(300, 400, K["right"]), (900, 1000, K["ok"]), (1500, 1600, K["ok"]), (2100, 2200, K["ok"]),
                      (2700, 2800, K["ok"])]
            c.run(7000)
            green, red, yellow = colours(c)
        ok = green > 0.2 and red == 0 and yellow == 0
        print(f"{'ok  ' if ok else 'FAIL'} {mb} MB: {green:.0%} green, {red:.1%} red, {yellow:.1%} yellow")
        if not ok:
            problems.append(f"{mb} MB")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
