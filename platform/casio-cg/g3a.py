#!/usr/bin/env python3
"""An add-in for the Casio fx-CG50 / Graph 90+E (a .g3a file): the game's code after a 0x7000-byte header with its
name and its two 92 x 64 icons (the layout of mkg3a, gitlab.com/taricorp/mkg3a).

  python3 platform/casio-cg/g3a.py code.bin out.g3a --name "Snake" --icon src/icon.png"""
import argparse
import os
import time

from PIL import Image

HEADER = 0x7000
ICON_W, ICON_H = 92, 64


def rgb565_be(im):
    out = bytearray()
    for r, g, b in im.convert("RGB").getdata():
        v = (r >> 3) << 11 | (g >> 2) << 5 | b >> 3
        out += bytes((v >> 8, v & 0xFF))
    return bytes(out)


def icon(path, selected):
    """The game's icon in the middle of the menu's 92 x 64 cell: white, or light blue when selected."""
    cell = Image.new("RGB", (ICON_W, ICON_H), (0xB8, 0xDC, 0xF8) if selected else (255, 255, 255))
    if path and os.path.exists(path):
        im = Image.open(path).convert("RGBA")
        im.thumbnail((ICON_H - 6, ICON_H - 6), Image.LANCZOS)
        cell.paste(im, ((ICON_W - im.width) // 2, (ICON_H - im.height) // 2), im)
    return rgb565_be(cell)


def put(h, at, data):
    h[at:at + len(data)] = data


def main():
    a = argparse.ArgumentParser()
    a.add_argument("code")
    a.add_argument("out")
    a.add_argument("--name", required=True)
    a.add_argument("--icon")
    a.add_argument("--version", default="01.00.0000")
    o = a.parse_args()
    code = open(o.code, "rb").read()
    size = len(code) + HEADER + 4
    h = bytearray(HEADER)
    put(h, 0x000, bytes([0xAA, 0xAC, 0xBD, 0xAF, 0x90, 0x88, 0x9A, 0x8D, 0xD3, 0xFF, 0xFE, 0xFF, 0xFE, 0xFF]))
    # copy protection: the file size's bytes inverted, and two bytes from them
    inv = bytes(~b & 0xFF for b in size.to_bytes(4, "big"))
    put(h, 0x010, inv)
    h[0x00E] = (h[0x013] - 0x41) & 0xFF
    h[0x00F] = 0xFE
    h[0x014] = (h[0x013] - 0xB8) & 0xFF
    h[0x024], h[0x025] = 0x01, 0x01
    put(h, 0x02E, len(code).to_bytes(4, "big"))  # where the checksum's copy is, after the code
    name = o.name.encode("ascii", "replace")
    put(h, 0x040, name[:0x1B])
    put(h, 0x05C, size.to_bytes(4, "big"))
    internal = b"@" + bytes(c for c in name.upper() if 0x20 < c < 0x7F)[:9]
    put(h, 0x060, internal)
    for i in range(8):  # its name in the menu's languages
        put(h, 0x06B + i * 0x18, name[:0x17])
    put(h, 0x130, o.version.encode()[:0xB])
    put(h, 0x13C, time.strftime("%Y.%m%d.%H%M").encode()[:14])
    put(h, 0x0EBC, os.path.basename(o.out).encode()[:0x143])
    put(h, 0x1000, icon(o.icon, False))
    put(h, 0x4000, icon(o.icon, True))
    cksum = (sum(h) + sum(code)) & 0xFFFFFFFF
    put(h, 0x020, cksum.to_bytes(4, "big"))
    with open(o.out, "wb") as f:
        f.write(h + code + cksum.to_bytes(4, "big"))


if __name__ == "__main__":
    main()
