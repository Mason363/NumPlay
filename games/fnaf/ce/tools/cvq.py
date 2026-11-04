#!/usr/bin/env python3
"""The CE pictures' codec: what engine/pic.c (and pic_ez80.s on the calculator) decodes. Made for the eZ80, where
every byte read costs cycles: a picture is a grid of 2 x 2 blocks, each one of a group's 255 codewords (four
palette indices), the grid's bytes compressed with zx0 (the CE toolchain's own decompressor). A group (the office,
a camera, a jumpscare...) has its palette and its codewords.

A picture may be coded over another one (its base: a camera with someone in it over the empty room, an
animation's frame over the frame before): a block that hardly changed is 255, kept as the base has it.

    picture: flags (1: a base, its id after the header; 2: blocks kept), blocks wide (u16), blocks tall (u8),
    [the base's id (u16)], then the zx0 stream of blocks wide x blocks tall bytes (rows of blocks, 255: kept).
    group (map): the codewords (four pages of 256 bytes: top left, top right, bottom left, bottom right; entry
    255 unused), then the palette (256 words), then the game's remap tables."""
import math

import numpy as np

KEEP = 255
K = 255


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


def lin(rgb):
    """the space the codewords are fitted in: closer steps in the dark, where the eye sees them"""
    return np.sqrt(np.clip(rgb, 0, 255) / 255)


def blocks_of(a):
    """(h, w, 3), h and w even -> (h/2 * w/2, 12): each block's 4 pixels (top left, top right, bottom left,
    bottom right), rows of blocks"""
    h, w = a.shape[:2]
    return a.reshape(h // 2, 2, w // 2, 2, 3).transpose(0, 2, 1, 3, 4).reshape(-1, 12)


def image_of(blocks, bw, bh):
    return blocks.reshape(bh, bw, 2, 2, 3).transpose(0, 2, 1, 3, 4).reshape(bh * 2, bw * 2, 3)


class Picture:
    def __init__(self, rgb, base=None, keep=None):
        h, w = rgb.shape[:2]
        assert h % 2 == 0 and w % 2 == 0
        self.rgb, self.bw, self.bh = rgb, w // 2, h // 2
        self.base, self.keep = base, keep
        self.idx = None    # the blocks' codewords (KEEP: as the base has it)
        self.shown = None  # each block's codeword as decoded (the base's where kept)
        self.src = None    # the source each shown block came from (no drift along frames)


class Group:
    """a palette group: its pictures, its codewords and palette"""

    def __init__(self, name, ncolors=224):
        self.name, self.ncolors = name, ncolors
        self.pics = []

    def add(self, pic):
        self.pics.append(pic)
        return pic

    def fit(self, seed=1):
        from scipy.cluster.vq import kmeans2
        from scipy.spatial import cKDTree
        allb = np.concatenate([lin(blocks_of(p.rgb)) for p in self.pics])
        rng = np.random.default_rng(seed)
        sample = allb[rng.choice(len(allb), min(len(allb), 60000), replace=False)] if len(allb) > K else allb
        k = min(K, len(np.unique(np.round(sample, 3), axis=0)))
        cb, _ = kmeans2(sample, k, minit="++", seed=seed, iter=16)
        # the palette, from the codewords' pixels (weighted by use), then the codewords in its colours
        _, use = cKDTree(cb).query(sample)
        weight = np.bincount(use, minlength=k).astype(np.float64) + 0.01
        px = cb.reshape(-1, 3)
        pw = np.repeat(weight, 4)
        n = min(self.ncolors, len(np.unique(np.round(px, 3), axis=0)))
        pick = rng.choice(len(px), 30000, p=pw / pw.sum())
        pal, _ = kmeans2(px[pick], n, minit="++", seed=seed, iter=25)
        pal = np.unique(to565((np.clip(pal, 0, 1) ** 2) * 255), axis=0)
        _, pidx = cKDTree(lin(pal)).query(px)
        self.pal = pal
        self.cw = pidx.reshape(k, 4)                          # codeword -> 4 palette indices
        self.cw_rgb = pal[self.cw].reshape(k, 12)             # what they look like
        self.tree = cKDTree(lin(self.cw_rgb))

    def code(self, pic, keep_test):
        """each block: its codeword (the nearest as shown), or KEEP where the base's block is near enough to it"""
        b = blocks_of(pic.rgb)
        _, idx = self.tree.query(lin(b))
        shown, src = idx.copy(), b.copy()
        if pic.base is not None:
            base = pic.base
            for i in range(len(idx)):
                if keep_test(b[i], base.src[i]):
                    idx[i] = KEEP
                    shown[i] = base.shown[i]
                    src[i] = base.src[i]
        pic.idx, pic.shown, pic.src = idx.astype(np.int64), shown, src

    def rgb_of(self, pic):
        """the picture as the calculator shows it"""
        return image_of(self.cw_rgb[pic.shown].reshape(-1, 12), pic.bw, pic.bh)

    def indices_of(self, pic):
        """the picture's palette indices as decoded: (h, w)"""
        q = self.cw[pic.shown].reshape(pic.bh, pic.bw, 2, 2)
        return q.transpose(0, 2, 1, 3).reshape(pic.bh * 2, pic.bw * 2)

    def blob(self, fixed):
        """the map: the codewords (four pages), the palette (the group's colours, then the fixed ones at the end)"""
        pages = np.zeros((4, 256), np.uint8)
        pages[:, :len(self.cw)] = self.cw.T
        words = [word1555(c) for c in self.pal]
        words += [0] * (256 - len(fixed) - len(words))
        words += [word1555(c) for c in fixed]
        assert len(words) == 256, (self.name, len(words))
        return pages.tobytes() + b"".join(w.to_bytes(2, "little") for w in words)


def keep_within(ymax, ymean):
    """a block is kept when its pixels are within ymax (each) and ymean (on average) of the base's source"""
    def test(new, old):
        d = np.abs(new - old)
        return d.max() <= ymax and d.mean() <= ymean
    return test


def write(pic, pic_id_of_base, zx0):
    """the picture's bytes"""
    flags = (1 if pic.base is not None else 0) | (2 if (pic.idx == KEEP).any() else 0)
    head = bytes([flags, pic.bw & 255, pic.bw >> 8, pic.bh])
    if pic.base is not None:
        head += bytes([pic_id_of_base & 255, pic_id_of_base >> 8])
    return head + zx0(bytes(pic.idx.astype(np.uint8).tolist()))
