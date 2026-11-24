#!/usr/bin/env python3
"""Checks the home screen's moving backgrounds (launcher/src/live.c), NumVisuals' 11 (Mason363/NumPlay#463):

1. Backspace goes through all 11 in NumVisuals' order, each one kept in npbg.set and drawn over the plain
   home screen, then none again;
2. an npbg.set of version 1 (six backgrounds: Ocean was 5, none 6) comes back as Ocean and none;
3. Tunnel and Fire, whose tables are the biggest, show on custom builds of Epsilon 25.2 (133652 bytes of RAM)
   and on Upsilon (107674 bytes), and nothing is written outside the app's RAM.

Usage: test_backgrounds.py build/compiled/NumPlay.nwa [build/compiled/NumPlay-Upsilon.nwa] [--out DIR]
"""
import argparse
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import emu  # noqa: E402
from test_progress import records, write_storage  # noqa: E402

NAMES = ["Aurora", "Sunset Drive", "Plasma", "Pastel", "Lava Lamp", "Warp", "Tunnel", "Fire", "Ocean", "Bokeh",
         "Code Rain"]
TUNNEL, FIRE, NONE = 6, 7, 11
LEFT = (0, 30, 60, 180)  # left of the card in front: only the background
T0, GAP = 1500, 700


def changed(a, b, box=None):
    """the share of pixels that differ between two screenshots (or a part of them)"""
    from PIL import Image, ImageChops
    a, b = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
    if box:
        a, b = a.crop(box), b.crop(box)
    d = ImageChops.difference(a, b).convert("L")
    return sum(d.histogram()[25:]) / (d.width * d.height)


def start(nwa, out, name, bg, system="epsilon", ram=None):
    """NumPlay with npbg.set holding bg (b"B", its version, the background; or none)"""
    emu.configure("n0110" if system == "upsilon" else "n0120", system, ram)  # (Upsilon: the N0110 and N0115)
    st = os.path.join(out, f"{name}.bin")
    write_storage(st, [("npbg.set", bg)] if bg else [])
    c = emu.Calculator(nwa, None, st, "nwlink")
    c.enable_checks()
    return c


def finish(c, name):
    """what went wrong, and npbg.set"""
    bad = sorted(set(c.violations)) + [f"the software's RAM written at {x:#x}" for x in c.firmware_ram_changes()[:3]]
    return [f"{name}: {b}" for b in bad], dict(records(c.storage_bytes())).get("npbg.set")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("numplay")
    ap.add_argument("upsilon", nargs="?")
    ap.add_argument("--out")
    a = ap.parse_args()
    out = a.out or tempfile.mkdtemp()
    os.makedirs(out, exist_ok=True)
    fails = []
    bs = emu.KEYS["backspace"]

    # 1. every background, then none
    c = start(a.numplay, out, "all", None)
    c.keys = [(T0 + i * GAP, T0 + i * GAP + 90, bs) for i in range(NONE + 1)]
    plain = os.path.join(out, "all_plain.png")
    c.run(T0 - 100)
    c.save_png(plain)
    for i in range(NONE + 1):
        c.run(T0 + (i + 1) * GAP - 100)
        pic = os.path.join(out, f"all_{i:02d}.png")
        c.save_png(pic)
        name = NAMES[i] if i < NONE else "none"
        saved = dict(records(c.storage_bytes())).get("npbg.set")
        if saved != b"B\x02" + bytes([i]):
            fails.append(f"Backspace {i + 1} times: npbg.set is {saved!r}, not {name} ({i})")
        drawn = changed(pic, plain, LEFT)
        if i < NONE and drawn < 0.2:
            fails.append(f"{name} (Backspace {i + 1} times) looks like the plain home screen")
        if i == NONE and drawn > 0.02:
            fails.append("after Code Rain, Backspace doesn't go back to the plain home screen")
    bad, _ = finish(c, "all")
    fails += bad
    print(f"test_backgrounds: Backspace went through {', '.join(NAMES)}, then none")

    # 2. the six of version 1: Ocean (5) comes back as Ocean, so the next is Bokeh; none (6) stays none
    c = start(a.numplay, out, "v1_ocean", b"B\x01\x05")
    c.keys = [(T0, T0 + 90, bs)]
    c.run(T0 + 400)
    bad, saved = finish(c, "v1_ocean")
    fails += bad
    if saved != b"B\x02\x09":
        fails.append(f"Ocean of version 1, then Backspace: npbg.set is {saved!r}, not Bokeh")
    c = start(a.numplay, out, "v1_none", b"B\x01\x06")
    c.run(T0 - 100)
    pic = os.path.join(out, "v1_none.png")
    c.save_png(pic)
    bad, _ = finish(c, "v1_none")
    fails += bad
    if changed(pic, plain, LEFT) > 0.02:
        fails.append("none of version 1 doesn't start on the plain home screen")
    print("test_backgrounds: version 1's Ocean and none come back")

    # 3. the biggest tables, on less RAM
    for nwa, system, ram in ((a.numplay, "epsilon", 133652), (a.upsilon, "upsilon", None)):
        if not nwa:
            continue
        for bg in (TUNNEL, FIRE):
            name = f"{system}_{NAMES[bg].lower()}"
            c = start(nwa, out, name, b"B\x02" + bytes([bg]), system, ram)
            c.run(T0)
            pic = os.path.join(out, name + ".png")
            c.save_png(pic)
            bad, _ = finish(c, name)
            fails += bad
            if changed(pic, plain, LEFT) < 0.2:
                fails.append(f"{NAMES[bg]} doesn't show on {system} with {ram or emu.EXT_RAM_LEN} bytes of RAM")
        print(f"test_backgrounds: Tunnel and Fire show on {system}")

    for f in fails:
        print("FAIL:", f)
    print("PASS" if not fails else "FAIL")
    sys.exit(1 if fails else 0)


if __name__ == "__main__":
    main()
