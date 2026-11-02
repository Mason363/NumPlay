"""FNAF World's pictures for src/pic.c: JPEG-coded 16 x 16 squares (tools/codec.py), with or without a mask.

A picture is a grid of squares (each coded alone, as in Five Nights at Freddy's 4). The battle's characters, enemies
and effects stand over the scene, so they also have a mask: their alpha in 4 levels (0 clear, 3 solid), row by row,
each row runs of one level (a byte: level << 6 | length - 1). Squares that are clear all through are not coded, and
under the clear pixels the colours are spread from the solid ones, which keeps the coded squares small.

PICS: 16 bytes a picture: width and height in squares, its quality, flags (1: a mask), its hot spot (i16 x, y: where
it is drawn from, in pixels from its top left corner), the picture it shares squares with (u16, 0xFFFF none), how many
squares it has of its own then (u16), where its table starts (u32). TABLE: with a mask first where it starts in MASK
(u32), then each square's place in BANK (u24, 0xFFFFFF clear) or, sharing, the squares that differ (u16 the square,
u24 its place). MASK: for each row of squares where its rows start (u16, from the mask's start), then the rows."""
import struct

import numpy as np
from PIL import Image

import codec as C

QUALITY = {"scene": 78, "sprite": 70, "face": 80}
QNAMES = list(QUALITY)


def pad16(a):
    h, w = a.shape[:2]
    H, W = -(-h // 16) * 16, -(-w // 16) * 16
    return np.pad(a, ((0, H - h), (0, W - w)) + ((0, 0),) * (a.ndim - 2), mode="edge" if a.ndim == 3 else "constant")


def spread(rgb, solid):
    """the colours of the solid pixels spread over the clear ones (so the squares code small)"""
    out = rgb * solid[..., None]
    w = solid.astype(np.float64)
    for _ in range(10):
        k = lambda x: x + np.roll(x, 1, 0) + np.roll(x, -1, 0) + np.roll(x, 1, 1) + np.roll(x, -1, 1)
        s = np.stack([k(out[..., i]) for i in range(3)], -1)
        ww = k(w)
        fill = (w == 0) & (ww > 0)
        if not fill.any():
            break
        out[fill] = s[fill] / ww[fill][:, None]
        w = np.where(fill, 1.0, w)
    if (w == 0).any():
        out[w == 0] = rgb[solid > 0].mean(axis=0) if solid.any() else 0
    return out


def mask_rows(a2):
    """a mask (rows of levels 0 to 3) -> its bytes: the start of each row of squares, then the rows' runs"""
    H, W = a2.shape
    rows = []
    for r in range(H):
        row = a2[r]
        b = bytearray()
        i = 0
        while i < W:
            j = i
            while j < W and row[j] == row[i] and j - i < 64:
                j += 1
            b.append(int(row[i]) << 6 | (j - i - 1))
            i = j
        rows.append(bytes(b))
    head, body = [], bytearray()
    for r in range(H):
        if r % 16 == 0:
            head.append(len(body))
        body += rows[r]
    n = len(head)
    out = b"".join(struct.pack("<H", 2 * n + h) for h in head) + bytes(body)
    assert len(out) < 65536
    return out


class Pic:
    def __init__(self, name, rgb, alpha, quality, hot, base=None, refs=()):
        self.name, self.q, self.base = name, quality, base
        self.hot = hot
        self.a = pad16(np.clip(rgb, 0, 255).astype(np.float64))
        self.mask = None
        if alpha is not None:
            al = pad16(alpha.astype(np.float64))
            # three levels: clear, half (the edges, the shadows) and solid; fewer runs than four
            a2 = np.where(al < 72, 0, np.where(al < 200, 2, 3)).astype(np.uint8)
            self.mask = a2
            self.a = spread(self.a, (a2 >= 2).astype(np.float64) if (a2 >= 2).any() else (a2 > 0).astype(np.float64))
        self.refs = [r for r in ([base] if base else []) + list(refs) if r]
        self.mh, self.mw = self.a.shape[0] // 16, self.a.shape[1] // 16


class Bank:
    def __init__(self):
        self.pics = []
        self.by_name = {}

    def add(self, name, rgb, alpha=None, quality="scene", hot=(0, 0), base=None, refs=()):
        assert name not in self.by_name, name
        p = Pic(name, rgb, alpha, quality, hot, base, refs)
        p.index = len(self.pics)
        self.pics.append(p)
        self.by_name[name] = p
        return p

    def sprite(self, name, im, scale, quality="sprite", refs=(), hot=(0, 0), trim=True):
        """an RGBA picture at scale with its hot spot (in the source picture), trimmed to what shows"""
        w, h = max(1, round(im.width * scale)), max(1, round(im.height * scale))
        a = np.asarray(im.convert("RGBA").resize((w, h), Image.LANCZOS)).astype(np.float64)
        al = a[..., 3]
        ys, xs = np.nonzero(al > 40)
        if trim and len(ys):
            y0, y1, x0, x1 = ys.min(), ys.max() + 1, xs.min(), xs.max() + 1
        else:
            y0, y1, x0, x1 = 0, h, 0, w
        hx, hy = round(hot[0] * scale) - x0, round(hot[1] * scale) - y0
        return self.add(name, a[y0:y1, x0:x1, :3], al[y0:y1, x0:x1], quality, (hx, hy), refs=refs)

    def encode(self):
        qt = {k: (C.qtable(C.STD_LUMA, v), C.qtable(C.STD_CHROMA, v)) for k, v in QUALITY.items()}
        for p in self.pics:
            y, cb, cr = C.ycbcr(p.a)
            ql, qc = qt[p.q]
            yq = C.quantize(C.blocks_of(y), ql, 0.18)
            cbq = C.quantize(C.blocks_of(cb), qc, 0.22)
            crq = C.quantize(C.blocks_of(cr), qc, 0.22)
            p.sq = []
            for my in range(p.mh):
                for mx in range(p.mw):
                    if p.mask is not None and not p.mask[my * 16:my * 16 + 16, mx * 16:mx * 16 + 16].any():
                        p.sq.append(None)
                        continue
                    src = None
                    tile = p.a[my * 16:my * 16 + 16, mx * 16:mx * 16 + 16]
                    for r in p.refs:
                        if mx < r.mw and my < r.mh and r.q == p.q and r.sq[my * r.mw + mx] is not None:
                            d = np.abs(tile - r.a[my * 16:my * 16 + 16, mx * 16:mx * 16 + 16])
                            strict = r is not p.base
                            if d.max() <= (6 if strict else 14) and d.mean() <= (1.2 if strict else 3.0):
                                src = r.sq[my * r.mw + mx]
                                break
                    if src is None:
                        blocks = [yq[2 * my + i, 2 * mx + j] for i in (0, 1) for j in (0, 1)]
                        src = ("own", tuple(C.mcu_symbols(blocks, cbq[my, mx], crq[my, mx])))
                    p.sq.append(src)
        freq = [[{}, {}], [{}, {}]]
        seen = set()
        for p in self.pics:
            for s in p.sq:
                if s is None or id(s) in seen:
                    continue
                seen.add(id(s))
                for comp, tab, sym, _, _ in s[1]:
                    f = freq[comp][tab]
                    f[sym] = f.get(sym, 0) + 1
        tables, huff = [[None, None], [None, None]], b""
        for comp in (0, 1):
            for tab in (0, 1):
                codes, bits, vals = C.canonical(C.huffman_lengths(freq[comp][tab]))
                tables[comp][tab] = codes
                huff += C.decode_tables(bits, vals)
        bank, where, by_id = bytearray(), {}, {}
        for p in self.pics:
            p.off = []
            for s in p.sq:
                if s is None:
                    p.off.append(0xFFFFFF)
                    continue
                if id(s) not in by_id:
                    data = C.encode_mcu(s[1], tables)
                    if data not in where:
                        where[data] = len(bank)
                        bank += data
                    by_id[id(s)] = where[data]
                p.off.append(by_id[id(s)])
        bank += bytes(8)
        assert len(bank) < 0xFFFFFF
        quant = b""
        for k in QNAMES:
            for t in qt[k]:
                quant += b"".join(struct.pack("<H", (int(t[i]) * C.AANSCALES[i] + 2048) >> 12) for i in range(64))
        masks, mask_at = bytearray(b"\0\0\0\0"), {}
        pics, table = b"", bytearray()
        for p in self.pics:
            flags = 0
            head = b""
            if p.mask is not None:
                m = mask_rows(p.mask)
                if m not in mask_at:
                    mask_at[m] = len(masks)
                    masks += m
                flags |= 1
                head = struct.pack("<I", mask_at[m])
            diff = []
            if p.base:
                b = p.base
                assert (b.mw, b.mh) == (p.mw, p.mh) and not b.base
                diff = [k for k in range(len(p.off)) if p.off[k] != b.off[k]]
            if p.base and 5 * len(diff) < 3 * len(p.off):
                pics += struct.pack("<BBBBhhHHI", p.mw, p.mh, QNAMES.index(p.q), flags, p.hot[0], p.hot[1],
                                    p.base.index, len(diff), len(table))
                table += head
                for k in diff:
                    table += struct.pack("<H", k) + p.off[k].to_bytes(3, "little")
            else:
                pics += struct.pack("<BBBBhhHHI", p.mw, p.mh, QNAMES.index(p.q), flags, p.hot[0], p.hot[1],
                                    0xFFFF, 0, len(table))
                table += head
                for o in p.off:
                    table += o.to_bytes(3, "little")
        print(f"pictures: {len(self.pics)}, squares {len(bank)} bytes, masks {len(masks)}, tables {len(table)}")
        return huff, quant, pics, bytes(table), bytes(bank), bytes(masks)
