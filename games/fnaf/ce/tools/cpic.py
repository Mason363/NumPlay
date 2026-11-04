#!/usr/bin/env python3
"""The CE pictures' codec: what src/pic.c decodes, made small enough for the calculator's archive.

A picture is coded like a little JPEG made for the eZ80: Y, Cb and Cr (the colour at half resolution), in
macroblocks of 8 x 8 pixels (four 4 x 4 blocks of Y, one of Cb, one of Cr), each 4 x 4 block a list of
coefficients of H.264's integer transform (adds and shifts only, no multiplications to undo it), quantized,
then Huffman coded (DC: the difference from the block before; AC: run and size, as JPEG). A picture may be
coded over another one (its base: a camera with someone in it over the empty room, an animation's frame over
the frame before): then only the macroblocks that differ are coded, each after the number skipped.

The calculator shows 256 colours: a picture group (the office, a camera, a jumpscare...) has its palette and
a table from (Y, Cb, Cr) to a palette index, with ordered dithering on the way (tables YQ, CQ). Decoding gives
palette indices straight away.

    bitstream: MSB first. Picture header (bytes): flags (1 base, 2 skips coded), mb width, mb height, quality,
    then (if base) the base picture's id (u16). Then for each coded macroblock (with skips: an Exp-Golomb
    count of macroblocks skipped first; the end is a count that runs past the last one): Y0 Y1 Y2 Y3 Cb Cr.
    A block: DC size (Huffman) + bits, then AC (run << 4 | size) + bits until EOB (0) or the 15th."""
import heapq
import math
from collections import Counter

import numpy as np

# the 4 x 4 zigzag: zigzag position -> raster index (row * 4 + column)
ZZ = [0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15]
ROW_NORM2 = [4.0, 2.5, 4.0, 2.5]   # the inverse core's basis rows: their squared norms


def inv_core(c):
    """H.264's inverse 4 x 4 transform, exactly as pic.c does it: c (..., 16) integers in raster order ->
    (..., 16) values ((x + 32) >> 6)"""
    c = c.reshape(c.shape[:-1] + (4, 4)).astype(np.int64)

    def one(d0, d1, d2, d3):
        e, f = d0 + d2, d0 - d2
        g, h = (d1 >> 1) - d3, d1 + (d3 >> 1)
        return e + h, f + g, f - g, e - h

    r = one(c[..., 0], c[..., 1], c[..., 2], c[..., 3])          # rows: along the last axis
    t = np.stack(r, axis=-1)
    col = one(t[..., 0, :], t[..., 1, :], t[..., 2, :], t[..., 3, :])
    u = np.stack(col, axis=-2)
    return ((u + 32) >> 6).reshape(u.shape[:-2] + (16,))


def _basis():
    """the decoder's output for a coefficient of 1 (without its roundings): 16 patterns of 16"""
    b = np.zeros((16, 16))
    one = [[1, 1, 1, 1], [1, 0.5, -0.5, -1], [1, -1, -1, 1], [0.5, -1, 1, -0.5]]
    for i in range(4):
        for j in range(4):
            b[i * 4 + j] = np.outer(one[i], one[j]).reshape(16) / 64
    return b


BASIS = _basis()
ANALYSE = np.linalg.inv(BASIS)            # x (16) @ ANALYSE -> the coefficients giving back x
NORM = np.array([math.sqrt(ROW_NORM2[i // 4] * ROW_NORM2[i % 4]) / 64 for i in range(16)])


def qtable(step, slope):
    """the dequantization steps (raster order): step (in pixel levels) for DC, rising with frequency"""
    q = []
    for k in range(16):
        i, j = k // 4, k % 4
        d = step * 4 * (1 + slope * (i + j))     # in the orthonormal transform's units
        q.append(max(1, int(round(d / NORM[k] / 4))))
    return np.array(q, dtype=np.int64)


# ------------------------------------------------------------------ colour
def ycc(rgb):
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    y = 0.299 * r + 0.587 * g + 0.114 * b
    cb = -0.168736 * r - 0.331264 * g + 0.5 * b
    cr = 0.5 * r - 0.418688 * g - 0.081312 * b
    return y, cb, cr


def rgb_of(y, cb, cr):
    return np.stack([y + 1.402 * cr, y - 0.344136 * cb - 0.714136 * cr, y + 1.772 * cb], -1)


# ------------------------------------------------------------------ coding a picture
class Coded:
    """a picture coded: its macroblocks' symbols, and what the decoder will have (Y, Cb, Cr planes)"""

    def __init__(self, mw, mh, quality, base):
        self.mw, self.mh, self.quality, self.base = mw, mh, quality, base
        self.mbs = []       # (index, [6 blocks of (dc level, [16 levels zigzag])])
        self.rec = None     # (y, cb, cr) integer planes, y at full size, chroma at half


def blocks_of(plane):
    h, w = plane.shape
    return plane.reshape(h // 4, 4, w // 4, 4).transpose(0, 2, 1, 3).reshape(h // 4, w // 4, 16)


def plane_of(blocks):
    nby, nbx, _ = blocks.shape
    return blocks.reshape(nby, nbx, 4, 4).transpose(0, 2, 1, 3).reshape(nby * 4, nbx * 4)


def quantize(x, q, dz):
    """x (..., 16) values -> levels (..., 16), raster order"""
    c = x @ ANALYSE
    v = c / q
    lv = np.sign(v) * np.floor(np.abs(v) + dz)
    lv[..., 0] = np.round(v[..., 0])
    return lv.astype(np.int64)


def code(rgb, quality, base=None, skip=None):
    """rgb: (h, w, 3) floats, h and w multiples of 8. quality: (qy, qc, dz). base: a Coded of the same size
    whose decoded planes this one starts from: a macroblock skip(new, old) finds near enough to the source the
    base's one was made from is kept as it is (no drift along an animation's frames)."""
    h, w = rgb.shape[:2]
    assert h % 8 == 0 and w % 8 == 0
    mw, mh = w // 8, h // 8
    qy, qc, dz = quality
    y, cb, cr = ycc(rgb)
    sub = lambda c: c.reshape(h // 2, 2, w // 2, 2).mean(axis=(1, 3))  # noqa: E731
    planes = [y - 128, sub(cb), sub(cr)]
    lv = [quantize(blocks_of(planes[0]), qy, dz), quantize(blocks_of(planes[1]), qc, dz),
          quantize(blocks_of(planes[2]), qc, dz)]
    rec = [plane_of(inv_core(lv[0] * qy)) + 128, plane_of(inv_core(lv[1] * qc)), plane_of(inv_core(lv[2] * qc))]
    src = [planes[0] + 128, planes[1], planes[2]]
    out = Coded(mw, mh, quality, base)
    keep = np.ones((mh, mw), bool)
    if base is not None:
        assert (base.mw, base.mh) == (mw, mh)
        for my in range(mh):
            for mx in range(mw):
                ys, xs = slice(my * 8, my * 8 + 8), slice(mx * 8, mx * 8 + 8)
                cs, cxs = slice(my * 4, my * 4 + 4), slice(mx * 4, mx * 4 + 4)
                new = (src[0][ys, xs], src[1][cs, cxs], src[2][cs, cxs])
                old = (base.src[0][ys, xs], base.src[1][cs, cxs], base.src[2][cs, cxs])
                if skip(new, old):
                    keep[my, mx] = False
                    for k, (a_, b_) in enumerate(((ys, xs), (cs, cxs), (cs, cxs))):
                        rec[k][a_, b_] = base.rec[k][a_, b_]
                        src[k][a_, b_] = base.src[k][a_, b_]
    for my in range(mh):
        for mx in range(mw):
            if not keep[my, mx]:
                continue
            bl = []
            for by_, bx_ in ((0, 0), (0, 1), (1, 0), (1, 1)):
                bl.append(lv[0][my * 2 + by_, mx * 2 + bx_])
            bl.append(lv[1][my, mx])
            bl.append(lv[2][my, mx])
            out.mbs.append((my * mw + mx, [[int(b[ZZ[k]]) for k in range(16)] for b in bl]))
    out.rec = rec
    out.src = src
    return out


def default_skip(ymax, ymean, cmax):
    def skip(new, old):
        dy = np.abs(new[0] - old[0])
        dc = max(np.abs(new[1] - old[1]).max(), np.abs(new[2] - old[2]).max())
        return dy.max() <= ymax and dy.mean() <= ymean and dc <= cmax
    return skip


# ------------------------------------------------------------------ the symbols, Huffman, bits
def size_of(v):
    v = abs(v)
    return v.bit_length()


def stream(coded):
    """what a coded picture writes, in order: ("g", n) an Exp-Golomb count, ("h", table, symbol, value, size)
    a Huffman symbol (tables 0 Y DC, 1 Y AC, 2 colour DC, 3 colour AC) and its size bits of value"""
    out = []
    pred = [0, 0, 0]
    last = -1
    skips = coded.base is not None
    total = coded.mw * coded.mh
    for idx, blocks in coded.mbs:
        if skips:
            out.append(("g", idx - last - 1))
        if idx != last + 1 or idx % coded.mw == 0:
            pred = [0, 0, 0]
        last = idx
        for k, blk in enumerate(blocks):
            comp = 0 if k < 4 else k - 3
            t = 0 if k < 4 else 2
            d = blk[0] - pred[comp]
            pred[comp] = blk[0]
            out.append(("h", t, size_of(d), d, size_of(d)))
            run = 0
            nz = [i for i in range(1, 16) if blk[i]]
            lastnz = nz[-1] if nz else 0
            for i in range(1, lastnz + 1):
                if blk[i] == 0:
                    run += 1
                    continue
                s = size_of(blk[i])
                out.append(("h", t + 1, run << 4 | s, blk[i], s))
                run = 0
            if lastnz < 15:
                out.append(("h", t + 1, 0, 0, 0))
    if skips:
        out.append(("g", total - last - 1))
    return out


def count(streams):
    """symbol counts of the four tables over many pictures"""
    freq = [Counter() for _ in range(4)]
    for st in streams:
        for it in st:
            if it[0] == "h":
                freq[it[1]][it[2]] += 1
    return freq


def huffman_lengths(freq, limit=16):
    """code lengths from symbol counts, at most limit bits (package-merge would be exact; this squeezes)"""
    if not freq:
        return {}
    if len(freq) == 1:
        return {next(iter(freq)): 1}
    heap = [(f, i, [s]) for i, (s, f) in enumerate(sorted(freq.items()))]
    heapq.heapify(heap)
    depth = {s: 0 for s in freq}
    n = len(heap)
    while len(heap) > 1:
        f1, _, s1 = heapq.heappop(heap)
        f2, _, s2 = heapq.heappop(heap)
        for s in s1 + s2:
            depth[s] += 1
        heapq.heappush(heap, (f1 + f2, n, s1 + s2))
        n += 1
    while max(depth.values()) > limit:   # flatten: the rarest symbols up, Kraft kept
        for s in sorted(depth, key=lambda s: (-depth[s], freq[s])):
            if depth[s] > limit:
                depth[s] = limit
        while sum(2.0 ** -d for d in depth.values()) > 1:
            s = max((s for s in depth if depth[s] < limit), key=lambda s: (depth[s], -freq[s]))
            depth[s] += 1
    return depth


def canonical(lengths):
    """lengths -> (codes {sym: (code, len)}, counts per length 1..16, symbols in order)"""
    items = sorted(lengths.items(), key=lambda kv: (kv[1], kv[0]))
    codes, code, prev = {}, 0, 0
    counts = [0] * 17
    for s, l in items:
        code <<= l - prev
        prev = l
        codes[s] = (code, l)
        code += 1
        counts[l] += 1
    return codes, counts[1:], [s for s, _ in items]


class Bits:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, v, n):
        for i in range(n - 1, -1, -1):
            self.acc = self.acc << 1 | (v >> i & 1)
            self.n += 1
            if self.n == 8:
                self.out.append(self.acc)
                self.acc = self.n = 0

    def golomb(self, v):
        """Exp-Golomb of v >= 0"""
        v += 1
        n = v.bit_length()
        self.put(0, n - 1)
        self.put(v, n)

    def done(self):
        if self.n:
            self.put(0, 8 - self.n)
        return bytes(self.out)


def write(coded, st, codes, quality_id, base_id=None):
    """a coded picture (and its stream) -> its bytes: the header, then the bits. codes: the 4 tables' codes"""
    flags = (1 if base_id is not None else 0) | (2 if coded.base is not None else 0)
    head = bytes([flags, coded.mw, coded.mh, quality_id])
    if base_id is not None:
        head += bytes([base_id & 255, base_id >> 8])
    b = Bits()
    for it in st:
        if it[0] == "g":
            b.golomb(it[1])
            continue
        _, t, sym, v, s = it
        c, l = codes[t][sym]
        b.put(c, l)
        if s:
            b.put(v if v > 0 else v + (1 << s) - 1, s)
    return head + b.done()


def tables_blob(qualities, codes_counts):
    """pic_init's tables: the qualities' steps (u16, raster: 16 Y then 16 colour), then the 4 Huffman tables
    (16 counts, then the symbols)"""
    out = bytearray([len(qualities)])
    for qy, qc, _ in qualities:
        for v in list(qy) + list(qc):
            out += int(v).to_bytes(2, "little")
    for counts, syms in codes_counts:
        out += bytes(counts) + bytes(syms)
    return bytes(out)


# ------------------------------------------------------------------ palettes: (Y, Cb, Cr) -> an index
Y_LEVELS, C_LEVELS = 32, 8
YQ_LO, YQ_HI, CQ_LO, CQ_HI = -96, 352, -192, 192
# the ordered dither (0..15) of a pixel in a macroblock, and of its 2 x 2's colour
B4 = np.array([[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]])
DY = np.array([[B4[r & 3][c & 3] for c in range(8)] for r in range(8)])
DCB = B4.copy()
DCR = 15 - DCB


def to565(c):
    c = np.clip(np.round(c), 0, 255)
    r = np.round(c[..., 0] * 31 / 255) * 255 / 31
    g = np.round(c[..., 1] * 63 / 255) * 255 / 63
    b = np.round(c[..., 2] * 31 / 255) * 255 / 31
    return np.stack([r, g, b], -1)


def word1555(c):
    """a 565 colour as the CE's palette holds it: 1555, the top bit green's lowest"""
    r, g, b = int(round(c[0] * 31 / 255)), int(round(c[1] * 63 / 255)), int(round(c[2] * 31 / 255))
    return (g & 1) << 15 | r << 10 | (g >> 1) << 5 | b


def spread(lo, hi, n, gamma):
    """n levels from lo to hi, closer together at the dark end (gamma > 1)"""
    t = np.linspace(0, 1, n) ** gamma
    return lo + (hi - lo) * t


def position(levels, v, scale):
    """v -> its place among the levels (0 .. n-1, linear between two), times scale, as integers"""
    p = np.interp(v, levels, np.arange(len(levels)))
    return np.clip(np.floor(p * scale), 0, (len(levels) - 1) * scale).astype(np.int64)


class Group:
    """a palette group: the pictures shown together (the office, a camera, a jumpscare...). Its colours (240,
    or 60 for 4 phases of static) and the decoder's tables to them"""

    def __init__(self, name, ncolors=240, phases=1):
        self.name, self.ncolors, self.phases = name, ncolors, phases
        self.planes = []     # decoded (y, cb, cr) of its pictures

    def fit(self, seed=1):
        from scipy.cluster.vq import kmeans2
        from scipy.spatial import cKDTree
        ys = np.concatenate([p[0].ravel() for p in self.planes]).astype(np.float64)
        cbs = np.concatenate([p[1].ravel() for p in self.planes]).astype(np.float64)
        crs = np.concatenate([p[2].ravel() for p in self.planes]).astype(np.float64)
        lo, hi = max(0.0, np.percentile(ys, 0.1) - 1), min(255.0, np.percentile(ys, 99.95) + 1)
        self.ylev = spread(lo, hi, Y_LEVELS, 1.6)
        self.cblev = np.linspace(np.percentile(cbs, 0.3) - 1, np.percentile(cbs, 99.7) + 1, C_LEVELS)
        self.crlev = np.linspace(np.percentile(crs, 0.3) - 1, np.percentile(crs, 99.7) + 1, C_LEVELS)
        # the cells the pictures use (as the decoder dithers them), weighted by use: k-means on their colours
        used = Counter()
        for y, cb, cr in self.planes:
            mapped = self.cells(y, cb, cr)
            used.update(mapped.ravel().tolist())
        cells = np.array(list(used.keys()))
        weight = np.array([used[c] for c in cells], dtype=np.float64)
        rgb = self.cell_rgb(cells)
        f = np.sqrt(rgb / 255) * 255
        n_pic = self.ncolors // self.phases
        rng = np.random.default_rng(seed)
        sample = f[rng.choice(len(f), 40000, p=weight / weight.sum())] if len(f) > n_pic else f
        k = min(n_pic, len(cells))
        cent, _ = kmeans2(sample, k, minit="++", seed=seed, iter=30)
        pal = np.unique(to565((np.clip(cent, 0, 255) / 255) ** 2 * 255), axis=0)
        self.pal = pal
        tree = cKDTree(np.sqrt(pal / 255) * 255)
        allc = np.arange(Y_LEVELS * C_LEVELS * C_LEVELS)
        _, idx = tree.query(np.sqrt(self.cell_rgb(allc) / 255) * 255)
        self.lut = idx.astype(np.int64) * self.phases

    def cell_rgb(self, cells):
        yl, cbl, crl = cells >> 6, cells >> 3 & 7, cells & 7
        return np.clip(rgb_of(self.ylev[yl], self.cblev[cbl], self.crlev[crl]), 0, 255)

    def tables(self):
        """YQ (u8 for Y + 128 from YQ_LO): its place among the levels times 8, CBQ/CRQ (Cb, Cr from CQ_LO): times 32"""
        yq = position(self.ylev, np.arange(YQ_LO, YQ_HI), 8)
        cbq = position(self.cblev, np.arange(CQ_LO, CQ_HI), 32)
        crq = position(self.crlev, np.arange(CQ_LO, CQ_HI), 32)
        return yq, cbq, crq

    def cells(self, y, cb, cr):
        """decoded planes (y: Y + 128, full size; cb, cr half) -> each pixel's cell (y level << 6 | cb << 3 | cr),
        as pic.c finds it"""
        yq, cbq, crq = self.tables()
        h, w = y.shape
        dy = np.tile(DY, (h // 8, w // 8)) >> 1
        dcb = np.tile(DCB, (h // 8, w // 8))
        dcr = np.tile(DCR, (h // 8, w // 8))
        yl = (yq[np.clip(y, YQ_LO, YQ_HI - 1) - YQ_LO] + dy) >> 3
        cbl = (cbq[np.clip(cb, CQ_LO, CQ_HI - 1) - CQ_LO] + (dcb << 1)) >> 5
        crl = (crq[np.clip(cr, CQ_LO, CQ_HI - 1) - CQ_LO] + (dcr << 1)) >> 5
        part = np.repeat(np.repeat(cbl << 3 | crl, 2, 0), 2, 1)
        return yl << 6 | part

    def indices(self, planes):
        return self.lut[self.cells(*planes)]

    def rgb(self, idx):
        return self.pal[idx // self.phases]

    def blob(self, fixed):
        """the map in the data: 256 palette words (the group's colours, then fixed at the end), YQ, CBQ, CRQ, LUT"""
        words = []
        for c in self.pal:
            words += [word1555(c)] * self.phases
        words += [0] * (256 - len(fixed) - len(words))
        words += [word1555(c) for c in fixed]
        assert len(words) == 256, (self.name, len(words))
        yq, cbq, crq = self.tables()
        out = b"".join(w.to_bytes(2, "little") for w in words)
        return out + bytes(yq.tolist()) + bytes(cbq.tolist()) + bytes(crq.tolist()) + bytes(self.lut.tolist())
