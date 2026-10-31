#!/usr/bin/env python3
"""How data.bin keeps its pictures (tools/pack.py lays them out with this), and the pictures of src/data.bin laid
out again in place, every pixel drawn the same:

    python3 -I tools/pictures.py

A picture is its colours (RGB565) and opacities, pixel by pixel; a clear pixel's colour does not matter. Kept:
- only the part of it that isn't clear (outside it, clear);
- a palette of its colours and opacities, each once (a palette seen before is not stored again);
- its pixels as indices to it: none (one colour), half a byte a pixel (16 colours or fewer, the first pixel
  lowest) or a byte, each row a whole number of bytes. The raw pixels of each group of pictures (by name,
  BORDER_*, CURSOR_R*...) are laid out with tools/share.py: a string seen before, or inside another, is not
  stored again; tails overlap heads.

The pictures drawn on every frame of the game's screen (RAW, and the heavens' stars) are kept so, read where they
lie; the others are packed (gfx.c unpacks one into a cache in RAM when it is drawn, its pixels then as a raw
picture's): each pixel, in rows, is told from the ones already there by a range coder (LZMA's) with adaptive
chances, as gfx.c's unpack(): is it the pixel on its left, above, above on the left, above on the right (each
once)? If not, a colour not seen yet (the palette is in the order the colours come), or one seen before, by how
recently (its rank, past those neighbours). The chances start at PRIORS, the same for every picture, learnt from
them all. An icon can be told from another (its reference: one above it or on its left in the sheet, which may
have one too, up to DEPTH): the reference's colours mapped to its own are the first guess for each pixel. That is
how the icons that are the same picture in other colours (the tiers of the buildings' upgrades) are kept nearly
once.

Its record (IMAGES, 16 bytes; res.h's ImageRec): w, h, then the part kept: x, y, w, h (u8 each), the bits a pixel
(0, 4, 8; | PACKED, | REF), colours - 1 (u8 each), the pixels' offset (or the packed data's), the palette's (u32):
its colours (RGB565), then their opacities (u8 each). Packed data: with REF, the reference's index (u16) and a map
from its colours to the picture's, one byte each (0xFF none), the last one for where the reference is clear
outside its part; then the range coder's bytes."""
import os
import re
import struct
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "..", "tools"))
from share import share  # noqa: E402

MAGIC = b"CCK2"
PACKED, REF = 0x80, 0x40
DEPTH = 4
CACHE = 40960   # (gfx.c's CACHE_BYTES: a packed picture and its reference fit in half of it)
# drawn on every frame of the game's screen, or over all of it (the heavens' stars): kept raw
RAW = re.compile(r"(SHOWER|BG_BLUE|BG_GRANDMAS|COOKIE|FLARE|GLINT|CURSOR_R|SMALL_COOKIE|BORDER|STORE_TILE|PANEL|"
                 r"UPGRADE_FRAME|MONEY|HEAVENLY_MONEY|LUMP_MONEY|GOLD_COOKIE|WRATH_COOKIE|WRINKLER|SHINY_WRINKLER|"
                 r"DARK_NOISE|STARS)")

# ---------------------------------------------------------------------------------------------- packed pictures
# the coder's contexts (gfx.c's): a neighbour's guess (by which neighbour, its place among the guesses and how the
# neighbours match), a colour not seen yet (by how many guesses), a rank (Elias gamma: its length in unary, the
# first bit after its top one, the others)
GUESS, NEW, UNARY, FIRST, OTHER = 0, 400, 405, 413, 422
CONTEXTS = 423


def model(cw, ch, n, guess, bit, idx=None):
    """the pixels (cw x ch, n colours) coded (idx) or decoded (idx None) with bit(context, b or None), which codes
    b (or decodes it) and gives it; guess: the reference's guesses (-1 none) or None. Gives the pixels."""
    out = [[0] * cw for _ in range(ch)]
    mtf, seen = list(range(n)), 0   # (the colours, the latest first)
    for y in range(ch):
        row, up = out[y], out[y - 1] if y else None
        for x in range(cw):
            v = idx[y][x] if idx is not None else None
            L = row[x - 1] if x else -1
            U = up[x] if y else -1
            UL = up[x - 1] if x and y else -1
            UR = up[x + 1] if y and x + 1 < cw else -1
            shape = ((L >= 0 and L == U) | (U >= 0 and U == UL) << 1 | (L >= 0 and L == UL) << 2
                     | (U >= 0 and U == UR) << 3)
            cands, j, got = [], 0, None
            for s, c in enumerate((guess[y][x] if guess else -1, L, U, UL, UR)):
                if c < 0 or c in cands:
                    continue
                if not bit(GUESS + (s * 5 + j) * 16 + shape, None if v is None else int(v != c)):
                    got = c
                    break
                cands.append(c)
                j += 1
            if got is None:
                if seen < n and not bit(NEW + min(j, 4), None if v is None else int(v != seen)):
                    got = seen
                    seen += 1
                else:
                    others = [c for c in mtf if c not in cands]
                    q = others.index(v) + 1 if v is not None else 0
                    k = 0
                    while k < 8 and bit(UNARY + k, None if v is None else int(q >> (k + 1) > 0)):
                        k += 1
                    r = 1
                    for t in range(k - 1, -1, -1):
                        r = r << 1 | bit(FIRST + k if t == k - 1 else OTHER, None if v is None else q >> t & 1)
                    got = others[r - 1]
            row[x] = got
            if mtf[0] != got:
                mtf.remove(got)
                mtf.insert(0, got)
    return out


class Encoder:
    """LZMA's range coder; the first byte (always 0) left out, and only the bytes the decoder needs at the end (it
    reads two more: what follows)"""

    def __init__(self, probs):
        self.low, self.range, self.cache, self.pending, self.out, self.p = 0, 0xFFFFFFFF, 0, 1, bytearray(), probs

    def shift(self):
        if self.low < 0xFF000000 or self.low >= 1 << 32:
            carry, b = self.low >> 32, self.cache
            while self.pending:
                self.out.append((b + carry) & 255)
                b, self.pending = 255, self.pending - 1
            self.cache = self.low >> 24 & 255
        self.pending += 1
        self.low = self.low << 8 & 0xFFFFFFFF

    def bit(self, ctx, b):
        p = self.p[ctx]
        bound = (self.range >> 11) * p
        if b:
            self.low += bound
            self.range -= bound
            self.p[ctx] = p - (p >> 4)
        else:
            self.range = bound
            self.p[ctx] = p + ((2048 - p) >> 4)
        if self.range < 1 << 24:
            self.range <<= 8
            self.shift()
        return b

    def finish(self):
        self.low = (self.low + 0xFFFF) & ~0xFFFF   # (any two bytes after these read as this)
        for _ in range(5):
            self.shift()
        assert self.out[0] == 0 and self.out[-2:] == b"\0\0"
        return bytes(self.out[1:-2])


class Decoder:
    def __init__(self, data, at, probs):
        self.d, self.at, self.range, self.p = data, at + 4, 0xFFFFFFFF, probs
        self.code = int.from_bytes(data[at:at + 4], "big")

    def bit(self, ctx, _):
        p = self.p[ctx]
        bound = (self.range >> 11) * p
        if self.code < bound:
            self.range = bound
            self.p[ctx] = p + ((2048 - p) >> 4)
            b = 0
        else:
            self.code -= bound
            self.range -= bound
            self.p[ctx] = p - (p >> 4)
            b = 1
        if self.range < 1 << 24:
            self.range <<= 8
            self.code = (self.code << 8 | (self.d[self.at] if self.at < len(self.d) else 0)) & 0xFFFFFFFF
            self.at += 1
        return b


def guesses(part, ref, mapping):
    """the reference's guesses for a part (x, y, cw, ch) of a picture: its colours mapped (the last entry: where
    it is clear outside its part)"""
    x, y, cw, ch = part
    rx, ry, rcw, rch, ridx = ref
    out = []
    for yy in range(y, y + ch):
        row = []
        for xx in range(x, x + cw):
            v = ridx[yy - ry][xx - rx] if 0 <= xx - rx < rcw and 0 <= yy - ry < rch else len(mapping) - 1
            m = mapping[v]
            row.append(-1 if m == 255 else m)
        out.append(row)
    return out


def best_mapping(part, idx, ref, nref):
    """for each of the reference's colours (and its clear outside), the picture's colour found most with it"""
    x, y, cw, ch = part
    rx, ry, rcw, rch, ridx = ref
    count = {}
    for yy in range(ch):
        for xx in range(cw):
            X, Y = x + xx, y + yy
            v = ridx[Y - ry][X - rx] if 0 <= X - rx < rcw and 0 <= Y - ry < rch else nref
            k = (v, idx[yy][xx])
            count[k] = count.get(k, 0) + 1
    mapping = [255] * (nref + 1)
    for (a, b), _ in sorted(count.items(), key=lambda e: (-e[1], e[0])):
        if mapping[a] == 255:
            mapping[a] = b
    return mapping


def pack(idx, n, guess, priors):
    e = Encoder(list(priors))
    model(len(idx[0]), len(idx), n, guess, e.bit, idx)
    return e.finish()


def learn(pics):
    """chances to start from, from how often each context's bits are 0: pics, (idx, n, guess)"""
    zeros, total = [0] * CONTEXTS, [0] * CONTEXTS

    def bit(ctx, b):
        total[ctx] += 1
        zeros[ctx] += 1 - b
        return b
    for idx, n, guess in pics:
        model(len(idx[0]), len(idx), n, guess, bit, idx)
    return [min(max((4096 * zeros[c] + total[c]) // (2 * total[c]) if total[c] else 1024, 31), 2017)
            for c in range(CONTEXTS)]


def plane(colours, opacities):
    """a picture as one array: RGB565 << 8 | opacity, the clear pixels 0"""
    c = np.asarray(colours, np.uint32)
    a = np.asarray(opacities, np.uint32)
    return np.where(a > 0, c << 8 | a, 0).astype(np.uint32)


def lay_out(pictures, names, put):
    """pictures: (h x w) planes (plane()); names: their IM_ names, the icons' "icon <x> <y>" (their place in the
    sheet); put(bytes, align) adds bytes to data.bin, giving their offset. Returns the records (IMAGES), in order,
    and the offset of PRIORS."""
    kept, pals, recs = [], {}, []
    for p in pictures:
        h, w = p.shape
        if not (0 < w < 256 and 0 < h < 256):
            raise SystemExit(f"pictures: {w} x {h}: past 255 pixels")
        ys, xs = np.nonzero(p)
        if not len(xs):   # (all clear: nothing to read)
            kept.append(None)
            continue
        x0, x1, y0, y1 = int(xs.min()), int(xs.max()) + 1, int(ys.min()), int(ys.max()) + 1
        part = p[y0:y1, x0:x1]
        flat = part.ravel()
        _, first, inv = np.unique(flat, return_index=True, return_inverse=True)
        order = np.argsort(first)                # (the colours in the order they come)
        rank = np.empty(len(order), np.int64)
        rank[order] = np.arange(len(order))
        idx = rank[inv].reshape(part.shape).astype(np.uint8)
        colours = flat[first[order]]
        n = len(colours)
        if n > 256:
            raise SystemExit(f"pictures: a picture of {n} colours")
        bits = 0 if n == 1 else 4 if n <= 16 else 8
        pal = b"".join(struct.pack("<H", int(c) >> 8) for c in colours) + bytes(int(c) & 255 for c in colours)
        if pal not in pals:
            pals[pal] = put(pal, 2)
        kept.append(dict(w=w, h=h, part=(x0, y0, x1 - x0, y1 - y0), bits=bits, n=n, idx=idx.tolist(), pal=pals[pal]))
    packed = [i for i, k in enumerate(kept) if k and k["bits"] and not RAW.match(names[i])]

    # the icons' references: the one above or on the left (up to 3 away) that makes it shortest, if any
    priors = learn([(kept[i]["idx"], kept[i]["n"], None) for i in packed])
    at = {tuple(map(int, nm.split()[1:])): i for i, nm in enumerate(names) if nm.startswith("icon ")}
    ref, depth = {}, {}
    icons = [i for i in packed if names[i].startswith("icon ")]
    for i in sorted(icons, key=lambda i: tuple(map(int, names[i].split()[:0:-1]))):   # (row by row)
        k = kept[i]
        cx, cy = map(int, names[i].split()[1:])
        best = (len(pack(k["idx"], k["n"], None, priors)), None, None)
        for j in [at.get((cx, cy - d)) for d in (1, 2, 3)] + [at.get((cx - d, cy)) for d in (1, 2, 3)]:
            if j not in icons or depth.get(j, 0) >= DEPTH or (kept[j]["w"], kept[j]["h"]) != (k["w"], k["h"]):
                continue
            r = kept[j]["part"] + (kept[j]["idx"],)
            mapping = best_mapping(k["part"], k["idx"], r, kept[j]["n"])
            size = len(pack(k["idx"], k["n"], guesses(k["part"], r, mapping), priors)) + 2 + len(mapping)
            if size < best[0]:
                best = (size, j, mapping)
        if best[1] is not None:
            ref[i] = best[1:]
            depth[i] = depth.get(best[1], 0) + 1

    def guess_of(i):
        if i not in ref:
            return None
        j, mapping = ref[i]
        return guesses(kept[i]["part"], kept[j]["part"] + (kept[j]["idx"],), mapping)
    priors = learn([(kept[i]["idx"], kept[i]["n"], guess_of(i)) for i in packed])
    priors_at = put(struct.pack(f"<{CONTEXTS}H", *priors), 2)

    size = lambda i: (kept[i]["part"][2] * kept[i]["bits"] + 7) // 8 * kept[i]["part"][3]
    for i in packed:
        if size(i) + (size(ref[i][0]) if i in ref else 0) > CACHE // 2:
            raise SystemExit(f"pictures: {names[i]} too big to pack")
    data = {}
    for i in packed:
        head = struct.pack("<H", ref[i][0]) + bytes(ref[i][1]) if i in ref else b""
        data[i] = head + pack(kept[i]["idx"], kept[i]["n"], guess_of(i), priors)
    for i, k in enumerate(kept):
        if k and k["bits"] and i not in data:
            idx = np.array(k["idx"], np.uint8)
            if k["bits"] == 4:
                if idx.shape[1] % 2:
                    idx = np.concatenate([idx, np.zeros((idx.shape[0], 1), np.uint8)], 1)
                data[i] = (idx[:, 0::2] | idx[:, 1::2] << 4).astype(np.uint8).tobytes()
            else:
                data[i] = idx.tobytes()
    # the raw pixels of each group shared (tools/share.py); the packed data one after the other
    offs = {}
    groups = [group(nm) for nm in names]
    for g in dict.fromkeys(groups):
        mine = [i for i in data if i not in packed and groups[i] == g]
        if mine:
            blob, o = share([data[i] for i in mine])
            base = put(blob, 1)
            offs.update((i, base + x) for i, x in zip(mine, o))
    for i in packed:
        offs[i] = put(data[i], 1)
    put(b"\0\0", 1)   # (the range decoder reads two bytes past the last)
    for i, (p, k) in enumerate(zip(pictures, kept)):
        if k is None:
            recs.append((p.shape[1], p.shape[0], 0, 0, 0, 0, 0, 1, 0, 0))
        else:
            flags = (PACKED if i in packed else 0) | (REF if i in ref else 0)
            recs.append((k["w"], k["h"]) + k["part"] + (k["bits"] | flags, k["n"], offs.get(i, 0), k["pal"]))
    return recs, priors_at


def group(name):
    """the group a picture's raw pixels are shared in: by its name (ROW_FARM: ROW), the icons by rows of the sheet"""
    if name.startswith("icon "):
        return "icons" + name.split()[2]
    return re.sub(r"(_.*|\d+)$", "", name)


def names_of(data, h):
    names = ["?"] * h["N_IMAGES"]
    cols = h["ICON_COLS"]
    for k, i in enumerate(struct.unpack_from(f"<{cols * h['ICON_ROWS']}H", data, h["OFF_ICONS"])):
        if i != 0xFFFF:
            names[i] = f"icon {k % cols} {k // cols}"
    for k, v in h.items():
        if k.startswith("IM_"):
            names[v] = k[3:]
    return names


def records(recs):
    return b"".join(struct.pack("<BBBBBBBBII", w, h, x, y, cw, ch, bits, n - 1, px, pal)
                    for w, h, x, y, cw, ch, bits, n, px, pal in recs)


def pictures_of(data, h):
    """the pictures of a data.bin (and its data.h's numbers), as planes"""
    out = {}
    old = data[:4] == b"CCK1"   # (before: no part, pictures of a byte, half a byte or RGB565 a pixel)
    parts = {}   # picture -> (x, y, cw, ch, indices)

    def read(i):
        if i in out:
            return
        at = h["OFF_IMAGES"] + 16 * i
        if old:
            w, hh, fmt, n1, _, px, pal = struct.unpack_from("<HHBBHII", data, at)
            if fmt == 0:
                c = np.frombuffer(data, "<u2", w * hh, px).reshape(hh, w)
                out[i] = plane(c, np.full((hh, w), 255))
                return
            x, y, cw, ch, bits = 0, 0, w, hh, 8 if fmt == 1 else 4
        else:
            w, hh, x, y, cw, ch, bits, n1, px, pal = struct.unpack_from("<BBBBBBBBII", data, at)
        p = np.zeros((hh, w), np.uint32)
        if cw:
            n = n1 + 1
            cols = np.frombuffer(data, "<u2", n, pal).astype(np.uint32)
            opa = np.frombuffer(data, np.uint8, n, pal + 2 * n).astype(np.uint32)
            if bits & PACKED:
                guess = None
                if bits & REF:
                    j = struct.unpack_from("<H", data, px)[0]
                    read(j)
                    nref = struct.unpack_from("<B", data, h["OFF_IMAGES"] + 16 * j + 7)[0] + 1
                    mapping = list(data[px + 2:px + 3 + nref])
                    guess = guesses((x, y, cw, ch), parts[j], mapping)
                    px += 3 + nref
                priors = list(struct.unpack_from(f"<{CONTEXTS}H", data, h["OFF_PRIORS"]))
                idx = np.array(model(cw, ch, n, guess, Decoder(data, px, priors).bit), np.uint8)
            elif bits == 8:
                idx = np.frombuffer(data, np.uint8, cw * ch, px).reshape(ch, cw)
            elif bits == 4:
                b = np.frombuffer(data, np.uint8, (cw + 1) // 2 * ch, px).reshape(ch, (cw + 1) // 2)
                idx = np.stack([b & 15, b >> 4], 2).reshape(ch, -1)[:, :cw]
            else:
                idx = np.zeros((ch, cw), np.uint8)
            parts[i] = (x, y, cw, ch, idx.tolist())
            p[y:y + ch, x:x + cw] = plane(cols[idx], opa[idx])
        out[i] = p
    for i in range(h["N_IMAGES"]):
        read(i)
    return [out[i] for i in range(h["N_IMAGES"])]


def check(pictures, data, h):
    for i, (a, b) in enumerate(zip(pictures, pictures_of(data, h))):
        if a.shape != b.shape or (a != b).any():
            raise SystemExit(f"pictures: picture {i} not read back as it was")


def numbers(path):
    h = {}
    for line in open(path):
        m = re.match(r"#define (\w+) (-?\d+)$", line.strip())
        if m:
            h[m.group(1)] = int(m.group(2))
    return h


def main():
    """src/data.bin's pictures laid out again: the pictures first, then the rest (from the icons' table to the
    texts) moved as it is, then the pictures' table; data.h's offsets moved with them"""
    bin_path, h_path = os.path.join(HERE, "..", "src", "data.bin"), os.path.join(HERE, "..", "src", "data.h")
    data, h = open(bin_path, "rb").read(), numbers(h_path)
    pics = pictures_of(data, h)
    out = bytearray(MAGIC)

    def put(b, align):
        while len(out) % align:
            out.append(0)
        out.extend(b)
        return len(out) - len(b)
    recs, priors = lay_out(pics, names_of(data, h), put)
    start, end = h["OFF_ICONS"], h["OFF_IMAGES"]   # (the rest: what isn't pictures)
    while (len(out) - start) % 8:   # (its tables of doubles stay 8-aligned)
        out.append(0)
    moved = len(out) - start
    rest = bytearray(data[start:end])
    fonts = h["OFF_FONTS"] - start   # (the fonts' offsets, from data.bin's start)
    for f in range(len([k for k in h if k.startswith("F_")])):
        o = struct.unpack_from("<I", rest, fonts + 4 * f)[0]
        struct.pack_into("<I", rest, fonts + 4 * f, o + moved)
    out += rest
    while len(out) % 4:
        out.append(0)
    images = len(out)
    out += records(recs)
    check(pics, bytes(out), dict(h, OFF_IMAGES=images, OFF_PRIORS=priors))
    lines = []
    for line in open(h_path).read().split("\n"):
        m = re.match(r"#define (OFF_\w+|DATA_SIZE) (\d+)$", line)
        if m and m.group(1) == "OFF_PRIORS":
            continue
        if m:
            v = int(m.group(2))
            v = images if m.group(1) == "OFF_IMAGES" else len(out) if m.group(1) == "DATA_SIZE" else v + moved
            if m.group(1) == "OFF_IMAGES":
                lines.append(f"#define OFF_PRIORS {priors}")
            line = f"#define {m.group(1)} {v}"
        lines.append(line)
    open(bin_path, "wb").write(out)
    open(h_path, "w").write("\n".join(lines))
    print(f"data.bin: {len(data)} -> {len(out)} bytes, pictures {start} -> {start + moved}")


if __name__ == "__main__":
    main()
