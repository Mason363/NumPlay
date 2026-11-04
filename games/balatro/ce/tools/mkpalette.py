#!/usr/bin/env python3
"""Makes tools/palette.py, the TI-84 Plus CE's fixed colours (run once; its output is kept in the repository so every
build maps the art the same way). The CE's 8-bit screen has 256 colours: 64 move with the background (src/bg_ce.c),
16 are taken as screens need them (rare interface colours: a boss's own), the other 176 are these: the interface's
colours as they are, then the art's most used colours (k-means over every sprite's pixels, in a perceptual space).
Needs numpy. Usage: mkpalette.py"""
import os
import re
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from artsrc import GAME, Art, to888  # noqa: E402

NSTATIC = 176


def ui_colours():
    """The interface's colours: HEXC(...) in the NumWorks interface's files and the C_ constants"""
    cols = set()
    for f in ("ui.h", "ui.c", "main.c", "draw.c", "gfx.c"):
        for h in re.findall(r"HEXC\(0x([0-9A-Fa-f]{6})\)", open(os.path.join(GAME, "src", f)).read()):
            cols.add(int(h, 16))
    cols |= {0xFFFFFF, 0x000000}
    out = []
    for c in sorted(cols):
        rgb = (c >> 16 & 0xF8, c >> 8 & 0xF8, c & 0xF8)  # as the CE shows it (5 bits a channel)
        if rgb not in out:
            out.append(rgb)
    return out


def lab(rgb):
    """a cheap perceptual space: weighted RGB with a lightness axis"""
    a = np.asarray(rgb, np.float64) / 255.0
    lin = a ** 2.2
    y = lin @ np.array([0.2126, 0.7152, 0.0722])
    return np.stack([y ** (1 / 3) * 100, (a[..., 0] - a[..., 1]) * 60, (a[..., 1] - a[..., 2]) * 40, (a[..., 0] - a[..., 2]) * 30], -1)


def main():
    art = Art()
    counts = {}
    for i in range(len(art.table)):
        w, h = art.size(i)
        weight = 1.0 if w * h <= 35 * 47 else 0.25  # the logo: big but seen once
        for row in art.rgb565(i):
            for v in row:
                if v is not None:
                    counts[v] = counts.get(v, 0) + weight
    cols = np.array([to888(c) for c in counts], np.float64)
    wts = np.array(list(counts.values()))
    fixed = np.array(ui_colours(), np.float64)
    k = NSTATIC - len(fixed)
    X, F = lab(cols), lab(fixed)
    rng = np.random.default_rng(1)
    # k-means++ seeds, away from the fixed colours
    d = ((X[:, None, :] - F[None, :, :]) ** 2).sum(-1).min(1)
    C = []
    for _ in range(k):
        p = d * wts
        c = X[rng.choice(len(X), p=p / p.sum())]
        C.append(c)
        d = np.minimum(d, ((X - c) ** 2).sum(-1))
    C = np.array(C)
    for it in range(60):
        allc = np.concatenate([F, C])
        lbl = ((X[:, None, :] - allc[None, :, :]) ** 2).sum(-1).argmin(1)
        newc = C.copy()
        for j in range(k):
            m = lbl == len(F) + j
            if m.any():
                newc[j] = (X[m] * wts[m, None]).sum(0) / wts[m].sum()
        if np.allclose(newc, C):
            break
        C = newc
    # back to RGB: the weighted mean of each cluster's colours
    allc = np.concatenate([F, C])
    lbl = ((X[:, None, :] - allc[None, :, :]) ** 2).sum(-1).argmin(1)
    pal = [tuple(int(v) for v in f) for f in fixed]
    for j in range(k):
        m = lbl == len(F) + j
        if not m.any():
            continue
        c = (cols[m] * wts[m, None]).sum(0) / wts[m].sum()
        rgb = tuple(int(round(v)) & 0xF8 for v in c)
        if rgb not in pal:
            pal.append(rgb)
    while len(pal) < NSTATIC:
        pal.append((0, 0, 0))
    with open(os.path.join(HERE, "palette.py"), "w") as f:
        f.write('"""Made by tools/mkpalette.py: the CE\'s 176 fixed colours (the interface\'s, then the art\'s)."""\n')
        f.write(f"NFIXED_UI = {len(fixed)}\nPALETTE = [\n")
        for i in range(0, len(pal), 6):
            f.write("    " + ", ".join(f"({r}, {g}, {b})" for r, g, b in pal[i:i + 6]) + ",\n")
        f.write("]\n")
    print(f"palette: {len(fixed)} interface colours, {len(pal) - len(fixed)} for the art")


if __name__ == "__main__":
    main()
