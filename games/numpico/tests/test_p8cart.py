#!/usr/bin/env python3
"""tools/p8cart.py: the same cart as .p8 and .p8.png reads the same (femto8's carts come both ways: the
ROM byte for byte; the code too, but for a few letters, in the pairs PICO-8 saved from the same version),
and the table holds together as src/carts.h says.

usage: test_p8cart.py DIR (the .p8 and .p8.png pairs: femto8's carts/)"""
import difflib
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
import p8cart  # noqa: E402


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Documents/Assets/gamesrc/numpico/femto8/carts")
    pairs = codes = 0
    for f in sorted(os.listdir(d)):
        if not f.endswith(".p8.png") or not os.path.exists(os.path.join(d, f[:-4])):
            continue
        rom_png, code_png, label_png = p8cart.read_cart(os.path.join(d, f))
        rom_p8, code_p8, label_p8 = p8cart.read_cart(os.path.join(d, f[:-4]))
        assert rom_png == rom_p8, f"{f}: the ROMs differ at {next(i for i in range(len(rom_p8)) if rom_png[i] != rom_p8[i]):#x}"
        a, b = code_png.replace(b"\r", b"").strip(), code_p8.replace(b"\r", b"").strip()
        if difflib.SequenceMatcher(None, a, b, autojunk=False).ratio() > 0.995:
            codes += 1
        if label_p8:
            same = sum(1 for x, y in zip(label_png, label_p8) if x == y)
            assert same > 0.99 * len(label_p8), f"{f}: the labels differ ({same} pixels the same)"
        pairs += 1
    assert pairs >= 8 and codes >= 6, f"{pairs} carts, {codes} with the same code"
    # the table
    carts = [(b"a", bytes(range(256)) * 67, b"print(1)", None), (p8cart.cart_name("été.p8.png"), bytes(0x4300), b"x", [3] * 16384)]
    t = p8cart.table(carts)
    magic, version, count = struct.unpack_from("<III", t)
    assert (magic, version, count) == (0x3850504E, 1, 2) and len(t) % 4 == 0
    rom, code, n, label, name = struct.unpack_from("<IIII48s", t, 12)
    assert rom == 12 + 64 * 2 and code == rom + 0x4300 and n == 8 and label == 0 and name.rstrip(b"\0") == b"a"
    assert t[code:code + n] == b"print(1)"
    rom, code, n, label, name = struct.unpack_from("<IIII48s", t, 12 + 64)
    assert rom % 4 == 0 and label % 4 == 0 and label > code and name.rstrip(b"\0") == "été".encode()
    assert t[label] == 3 and t[label + 16] == 0
    print(f"test_p8cart: {pairs} carts the same as .p8 and .p8.png ({codes} with the same code), the table as "
          "src/carts.h says")


if __name__ == "__main__":
    main()
