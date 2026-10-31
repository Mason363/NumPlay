"""Five Nights at Freddy's: the pictures' codec (src/pic.c decodes it).

The game's pictures are dark, grainy renders: what keeps them small is what JPEG does, so this is JPEG's
baseline coding without its file format. A picture is a grid of 16 x 16 squares (MCUs): YCbCr, the colours at
half resolution (4:2:0), each 8 x 8 block cosine-transformed, quantized and Huffman-coded (JPEG's run/size
symbols and tables, one set for the whole game). Every square stands alone, starting on a byte, with no DC
prediction from the one before it (only between the four Y blocks of a square), so the game decodes only the
squares on screen, and pictures share squares: a camera with Bonnie in it is the empty camera's squares but
where he stands, the frames of a jumpscare share what doesn't move, and squares that come out the same anywhere
are stored once."""
import heapq

import numpy as np

ZIGZAG = [0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7,
          14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46,
          53, 60, 61, 54, 47, 55, 62, 63]
STD_LUMA = [16, 11, 10, 16, 24, 40, 51, 61, 12, 12, 14, 19, 26, 58, 60, 55, 14, 13, 16, 24, 40, 57, 69, 56,
            14, 17, 22, 29, 51, 87, 80, 62, 18, 22, 37, 56, 68, 109, 103, 77, 24, 35, 55, 64, 81, 104, 113, 92,
            49, 64, 78, 87, 103, 121, 120, 101, 72, 92, 95, 98, 112, 100, 103, 99]
STD_CHROMA = [17, 18, 24, 47, 99, 99, 99, 99, 18, 21, 26, 66, 99, 99, 99, 99, 24, 26, 56, 99, 99, 99, 99, 99,
              47, 66, 99, 99, 99, 99, 99, 99] + [99] * 32
# libjpeg's "ifast" IDCT folds these scales into the quantization table (src/pic.c)
AANSCALES = [16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520, 22725, 31521, 29692, 26722, 22725, 17855, 12299,
             6270, 21407, 29692, 27969, 25172, 21407, 16819, 11585, 5906, 19266, 26722, 25172, 22654, 19266, 15137,
             10426, 5315, 16384, 22725, 21407, 19266, 16384, 12873, 8867, 4520, 12873, 17855, 16819, 15137, 12873,
             10114, 6967, 3552, 8867, 12299, 11585, 10426, 8867, 6967, 4799, 2446, 4520, 6270, 5906, 5315, 4520,
             3552, 2446, 1247]
LUT_BITS = 9


def qtable(base, quality):
    s = 5000 / quality if quality < 50 else 200 - 2 * quality
    return np.array([min(255, max(1, int((b * s + 50) // 100))) for b in base], dtype=np.float64)


def dct_matrix():
    c = np.zeros((8, 8))
    for k in range(8):
        for n in range(8):
            c[k, n] = (np.sqrt(1 / 8) if k == 0 else np.sqrt(2 / 8)) * np.cos((2 * n + 1) * k * np.pi / 16)
    return c


DCT = dct_matrix()


def ycbcr(rgb):
    """an H x W x 3 picture (0-255) -> Y (H x W), Cb and Cr (H/2 x W/2), all level-shifted (-128)"""
    r, g, b = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    y = 0.299 * r + 0.587 * g + 0.114 * b - 128
    cb = -0.168736 * r - 0.331264 * g + 0.5 * b
    cr = 0.5 * r - 0.418688 * g - 0.081312 * b
    h, w = y.shape
    cb = cb.reshape(h // 2, 2, w // 2, 2).mean(axis=(1, 3))
    cr = cr.reshape(h // 2, 2, w // 2, 2).mean(axis=(1, 3))
    return y, cb, cr


def blocks_of(plane):
    """an H x W plane -> (H/8, W/8, 8, 8) of its DCT coefficients"""
    h, w = plane.shape
    b = plane.reshape(h // 8, 8, w // 8, 8).transpose(0, 2, 1, 3)
    return np.einsum("ij,abjk,lk->abil", DCT, b, DCT)


def quantize(coef, q, deadzone):
    """coefficients (..., 8, 8) / a natural-order table, rounded toward 0 a little (deadzone) on the AC ones"""
    x = coef / q.reshape(8, 8)
    r = np.sign(x) * np.floor(np.abs(x) + 0.5 - deadzone)
    r[..., 0, 0] = np.round(x[..., 0, 0])
    return np.maximum(r, 0 * r - 2047).clip(-2047, 2047).astype(np.int32)


def size_of(v):
    v = abs(int(v))
    n = 0
    while v:
        n += 1
        v >>= 1
    return n


def bits_of(v, n):
    return v if v >= 0 else v + (1 << n) - 1


def block_symbols(zz, pred):
    """one block's coefficients in zigzag order -> [(table: 0 DC 1 AC, symbol, extra bits value, extra bits count)]"""
    out = []
    d = int(zz[0]) - pred
    s = size_of(d)
    out.append((0, s, bits_of(d, s), s))
    run = 0
    last = 63
    while last > 0 and zz[last] == 0:
        last -= 1
    for k in range(1, last + 1):
        v = int(zz[k])
        if v == 0:
            run += 1
            continue
        while run > 15:
            out.append((1, 0xF0, 0, 0))
            run -= 16
        s = size_of(v)
        out.append((1, run << 4 | s, bits_of(v, s), s))
        run = 0
    if last < 63:
        out.append((1, 0x00, 0, 0))
    return out


def mcu_symbols(yq, cbq, crq):
    """the quantized blocks of one square: 4 Y (8 x 8 each, natural order), Cb, Cr -> [(component class, ...)]"""
    syms = []
    pred = 0
    for b in yq:
        zz = b.reshape(64)[ZIGZAG]
        syms += [(0,) + s for s in block_symbols(zz, pred)]
        pred = int(zz[0])
    for b in (cbq, crq):
        zz = b.reshape(64)[ZIGZAG]
        syms += [(1,) + s for s in block_symbols(zz, 0)]
    return syms


def huffman_lengths(freq):
    """symbol -> frequency, to code lengths of at most 16 bits (JPEG's Annex K.2 and K.3)"""
    # a symbol of its own takes the all-ones code, which JPEG never uses (the padding is ones)
    f = {s: float(c) for s, c in freq.items() if c > 0}
    f[256] = 0.5
    syms = list(f)
    heap = [(f[s], i, [s]) for i, s in enumerate(syms)]
    heapq.heapify(heap)
    length = {s: 0 for s in syms}
    n = len(syms)
    while len(heap) > 1:
        f1, _, a = heapq.heappop(heap)
        f2, _, b = heapq.heappop(heap)
        for s in a + b:
            length[s] += 1
        n += 1
        heapq.heappush(heap, (f1 + f2, n, a + b))
    bits = [0] * 64
    for s in syms:
        bits[length[s]] += 1
    i = 63
    while i > 16:
        while bits[i] > 0:
            j = i - 2
            while bits[j] == 0:
                j -= 1
            bits[i] -= 2
            bits[i - 1] += 1
            bits[j + 1] += 2
            bits[j] -= 1
        i -= 1
    while bits[i] == 0:
        i -= 1
    bits[i] -= 1   # (the reserved code goes)
    order = sorted(syms, key=lambda s: (length[s], s == 256, s))[:-1]
    out = {}
    k = 0
    for ln in range(1, 17):
        for _ in range(bits[ln]):
            out[order[k]] = ln
            k += 1
    return out


def canonical(lengths):
    """symbol -> length to (symbol -> (code, length), BITS[1..16], HUFFVAL)"""
    order = sorted(lengths, key=lambda s: (lengths[s], s))
    bits = [0] * 17
    for s in order:
        bits[lengths[s]] += 1
    codes = {}
    code = 0
    k = 0
    for ln in range(1, 17):
        for _ in range(bits[ln]):
            codes[order[k]] = (code, ln)
            code += 1
            k += 1
        code <<= 1
    return codes, bits[1:], order


def decode_tables(bits, vals):
    """BITS and HUFFVAL -> the decoder's tables: a LUT_BITS lookup (length << 8 | symbol, 0 when longer), then
    for the slow way JPEG's maxcode (17 int32, -1 when none) and valoffset (17 int32), then HUFFVAL (256 bytes)"""
    lut = [0] * (1 << LUT_BITS)
    maxcode = [-1] * 18
    valoff = [0] * 18
    code = 0
    k = 0
    for ln in range(1, 17):
        n = bits[ln - 1]
        if n:
            valoff[ln] = k - code
            for _ in range(n):
                if ln <= LUT_BITS:
                    base = code << (LUT_BITS - ln)
                    for j in range(1 << (LUT_BITS - ln)):
                        lut[base + j] = ln << 8 | vals[k]
                code += 1
                k += 1
            maxcode[ln] = code - 1
        code <<= 1
    maxcode[17] = 0x7FFFFFFF
    out = b"".join(v.to_bytes(2, "little") for v in lut)
    out += b"".join(v.to_bytes(4, "little", signed=True) for v in maxcode)
    out += b"".join(v.to_bytes(4, "little", signed=True) for v in valoff)
    out += bytes(vals) + bytes(256 - len(vals))
    return out


class BitWriter:
    def __init__(self):
        self.out = bytearray()
        self.acc = 0
        self.n = 0

    def put(self, v, n):
        if n == 0:
            return
        self.acc = self.acc << n | (v & ((1 << n) - 1))
        self.n += n
        while self.n >= 8:
            self.n -= 8
            self.out.append(self.acc >> self.n & 0xFF)
        self.acc &= (1 << self.n) - 1

    def flush(self):
        """pads with ones (like JPEG) to a byte"""
        if self.n:
            self.put((1 << (8 - self.n)) - 1, 8 - self.n)
        return bytes(self.out)


def encode_mcu(syms, tables):
    """a square's symbols -> its bytes; tables[component class][0 DC, 1 AC][symbol] = (code, length)"""
    w = BitWriter()
    for comp, tab, sym, val, n in syms:
        code, ln = tables[comp][tab][sym]
        w.put(code, ln)
        w.put(val, n)
    return w.flush()
