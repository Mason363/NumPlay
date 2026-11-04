#!/usr/bin/env python3
"""A .gw file made here, as LCD-Game-Shrinker lays them out, for the Game & Watch tests (no real
Game & Watch game may be shared): an SM510 program that lights segments b1 (all four), c1 (two of
four) and, while the K1 to K4 keys are held (left, right, Game, Time), the four a1 segments.

  gwrom.py OUT.gw [--lz4]          the file (--lz4: packed as LCD-Game-Shrinker packs them)
  gwrom.py OUT.gw --expect KEYS OUT.ppm   also the picture the screen must show with those
                                    GW buttons held (left 1, right 4, Game 128, Time 64)"""
import struct
import sys

W, H = 320, 240
BTN_LEFT, BTN_RIGHT, BTN_A, BTN_B, BTN_TIME, BTN_GAME = 0x1, 0x4, 0x10, 0x20, 0x40, 0x80


def background():
    px = []
    for y in range(H):
        for x in range(W):
            r, g, b = (x * 31) // (W - 1), (y * 63) // (H - 1), ((x + y) * 31) // (W + H - 2)
            px.append(r << 11 | g << 5 | b)
    return px


# segment number -> (x, y, w, h); a1 H1..H4 = 0..3, b1 H1..H4 = 64..67, c1 H1..H4 = 192..195
SEGS = {}
for k in range(4):
    SEGS[k] = (10 + 75 * k, 20, 60, 40)
    SEGS[64 + k] = (10 + 75 * k, 90, 60, 30)
    SEGS[192 + k] = (10 + 75 * k, 150, 60, 50)


def seg_pixel(s, i, j):
    """the segment's 8-bit pixel: 0xFF (white) lets the background through"""
    if (i + j) % 7 == 0:
        return 0xFF
    return (s * 37 + i * 3 + j) & 0x7F


def program():
    """the SM510's program from its reset vector (page 0xDC0), its steps placed in the order its
    program counter (an LFSR) goes"""
    ops = [0x5F, 0x70, 0x2F, 0x10,          # LBL 70h, LAX 15, EXC: b1 = 1111
           0x5F, 0x50, 0x25, 0x10,          # LBL 50h, LAX 5, EXC: c1 = 0101
           0x5F, 0x60, 0x6A, 0x10]          # loop: LBL 60h, KTA, EXC: a1 = the K keys
    rom = bytearray(4096)
    pc, order = 0xDC0, []
    for _ in range(len(ops) + 1):
        order.append(pc)
        feed = 0 if ((pc >> 1 ^ pc) & 1) else 0x20
        pc = feed | (pc >> 1 & 0x1F) | (pc & ~0x3F)
    for at, op in zip(order, ops):
        rom[at] = op
    rom[order[len(ops)]] = 0x80 | (order[8] & 0x3F)  # T loop
    return bytes(rom)


def build():
    bg = background()
    nseg = 256
    xs, ys, ws, hs, offs = [0] * nseg, [0] * nseg, [0] * nseg, [0] * nseg, [0] * nseg
    pixels = bytearray()
    for s, (x, y, w, h) in sorted(SEGS.items()):
        xs[s], ys[s], ws[s], hs[s], offs[s] = x, y, w, h, len(pixels)
        pixels += bytes(seg_pixel(s, i, j) for j in range(h) for i in range(w))
    keyboard = [0] * 10
    keyboard[1] = BTN_LEFT | BTN_RIGHT << 8 | BTN_GAME << 16 | BTN_TIME << 24  # S2: K1..K4
    keyboard[2] = (BTN_B + BTN_GAME)                                         # S3: Game B
    parts = [struct.pack(f"<{W * H}H", *bg), bytes(pixels), struct.pack(f"<{nseg}I", *offs),
             struct.pack(f"<{nseg}H", *xs), struct.pack(f"<{nseg}H", *ys), struct.pack(f"<{nseg}H", *hs),
             struct.pack(f"<{nseg}H", *ws), b"", program(), struct.pack("<10I", *keyboard)]
    head_size = 8 + 8 + 8 + 4 + 80
    body, table = bytearray(), []
    for p in parts:
        body += bytes(-(head_size + len(body)) % 4)
        table += [head_size + len(body) if p else 0, len(p)]
        body += p
    head = struct.pack("<8s8s8BI20I", b"SM510\0\0\0", b"w_test01", *([0] * 8), 0, *table)
    return head + bytes(body), bg, xs, ys, ws, hs


def expect(keys):
    _, bg, xs, ys, ws, hs = build()
    out = list(bg)
    on = [64, 65, 66, 67, 192, 194]
    on += [k for k, b in enumerate((BTN_LEFT, BTN_RIGHT, BTN_GAME, BTN_TIME)) if keys & b]
    for s in sorted(on):
        x0, y0, w, h = SEGS[s]
        for j in range(h):
            for i in range(w):
                p = seg_pixel(s, i, j)
                if p == 0xFF:
                    continue
                c = out[(y0 + j) * W + x0 + i]
                r, g, b = ((c >> 11) * p) >> 8, (((c >> 5) & 63) * p) >> 8, ((c & 31) * p) >> 8
                out[(y0 + j) * W + x0 + i] = r << 11 | g << 5 | b
    return out


def ppm(path, px):
    with open(path, "wb") as f:
        f.write(b"P6\n%d %d\n255\n" % (W, H))
        for c in px:
            f.write(bytes(((c >> 11) << 3 | (c >> 13), ((c >> 5) & 63) << 2 | ((c >> 9) & 3), (c & 31) << 3 | ((c >> 2) & 7))))


def lz4_frame(data):
    """an LZ4 frame of uncompressed blocks (what an LZ4 reader must take as well as packed ones)"""
    out = bytearray(b"\x04\x22\x4d\x18")
    flg, bd = 0x60, 0x70  # version 1, independent blocks; 4 MB blocks
    out += bytes((flg, bd, 0x73))  # (header checksum byte: not checked by the readers here)
    for k in range(0, len(data), 4 << 20):
        blk = data[k:k + (4 << 20)]
        out += struct.pack("<I", len(blk) | 0x80000000) + blk
    out += b"\0\0\0\0"
    return bytes(out)


def main():
    out = sys.argv[1]
    data = build()[0]
    if "--lz4" in sys.argv:
        data = lz4_frame(data)
    open(out, "wb").write(data)
    if "--expect" in sys.argv:
        i = sys.argv.index("--expect")
        ppm(sys.argv[i + 2], expect(int(sys.argv[i + 1])))


if __name__ == "__main__":
    main()
