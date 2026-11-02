"""Supercell's .sc files as Clash Royale's 2016 Android build has them: read and drawn.

An .sc file is "SC", a version and an MD5, then LZMA (5 bytes of properties, a 4-byte size). foo.sc holds the
shapes (pieces of a texture placed in the picture), movie clips (frames of shapes and clips with matrices and
colour transforms) and the names they are exported under; foo_tex.sc holds the textures.
"""
import lzma
import struct

import numpy as np
from PIL import Image, ImageDraw


def decompress(data):
    if data[:2] == b'SC':
        _, hl = struct.unpack('>II', data[2:10])
        data = data[10 + hl:]
    raw = data[:5] + data[5:9] + b'\0\0\0\0' + data[9:]
    return lzma.LZMADecompressor(format=lzma.FORMAT_ALONE).decompress(raw)


class Reader:
    def __init__(self, b):
        self.b, self.p = b, 0

    def u8(self):
        self.p += 1
        return self.b[self.p - 1]

    def u16(self):
        self.p += 2
        return struct.unpack_from('<H', self.b, self.p - 2)[0]

    def u32(self):
        self.p += 4
        return struct.unpack_from('<I', self.b, self.p - 4)[0]

    def i32(self):
        self.p += 4
        return struct.unpack_from('<i', self.b, self.p - 4)[0]

    def s(self):
        n = self.u8()
        if n == 255:
            return None
        self.p += n
        return self.b[self.p - n:self.p].decode('latin1')


def pixels(r, fmt, w, h):
    n = w * h
    if fmt in (0, 1):
        a = np.frombuffer(r.b, np.uint8, n * 4, r.p).reshape(h, w, 4).copy()
        r.p += n * 4
        return a
    v = None
    if fmt in (2, 3, 4):
        v = np.frombuffer(r.b, '<u2', n, r.p).reshape(h, w).astype(np.uint32)
        r.p += n * 2
    if fmt == 2:
        return (np.stack([v >> 12 & 15, v >> 8 & 15, v >> 4 & 15, v & 15], -1) * 17).astype(np.uint8)
    if fmt == 3:
        return np.stack([(v >> 11 & 31) * 255 // 31, (v >> 6 & 31) * 255 // 31, (v >> 1 & 31) * 255 // 31,
                         (v & 1) * 255], -1).astype(np.uint8)
    if fmt == 4:
        return np.stack([(v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31,
                         np.full_like(v, 255)], -1).astype(np.uint8)
    if fmt == 6:
        a = np.frombuffer(r.b, np.uint8, n * 2, r.p).reshape(h, w, 2)
        r.p += n * 2
        return np.stack([a[..., 1], a[..., 1], a[..., 1], a[..., 0]], -1).copy()
    if fmt == 10:
        a = np.frombuffer(r.b, np.uint8, n, r.p).reshape(h, w)
        r.p += n
        return np.stack([a, a, a, np.full_like(a, 255)], -1).copy()
    raise ValueError('pixel format %d' % fmt)


def untile(img, w, h):
    """textures stored in 32 x 32 blocks"""
    flat = img.reshape(-1, img.shape[-1])
    out = np.zeros_like(img)
    i = 0
    for by in range(0, h, 32):
        for bx in range(0, w, 32):
            bh, bw = min(32, h - by), min(32, w - bx)
            for y in range(bh):
                out[by + y, bx:bx + bw] = flat[i:i + bw]
                i += bw
    return out


def textures(data):
    b = decompress(data)
    r = Reader(b)
    out = []
    while r.p < len(b):
        tag, ln = r.u8(), r.u32()
        if tag == 0:
            break
        end = r.p + ln
        if tag in (1, 16, 19, 24, 27, 28, 29, 34):
            fmt, w, h = r.u8(), r.u16(), r.u16()
            img = pixels(r, fmt, w, h)
            out.append(untile(img, w, h) if tag in (27, 28, 29) else img)
        r.p = end
    # premultiplied, as floats, for drawing
    pm = []
    for t in out:
        f = t.astype(np.float32) / 255
        f[..., :3] *= f[..., 3:4]
        pm.append(f)
    return pm


class SC:
    def __init__(self, data, tex_data=None):
        self.tex = textures(tex_data) if tex_data else []
        b = decompress(data)
        r = Reader(b)
        r.p = 12 + 5
        n = r.u16()
        ids = [r.u16() for _ in range(n)]
        names = [r.s() for _ in range(n)]
        self.exports = {}
        for i, nm in zip(ids, names):
            self.exports.setdefault(nm, i)
        self.shapes, self.clips, self.matrices, self.colors = {}, {}, [], []
        while r.p < len(b):
            tag, ln = r.u8(), r.u32()
            end = r.p + ln
            if tag == 0:
                break
            if tag in (2, 18):
                self.shape(r, tag)
            elif tag in (10, 12, 35):
                self.clip(r, tag)
            elif tag == 8:
                a, bb, c, d, tx, ty = [r.i32() for _ in range(6)]
                self.matrices.append((a / 1024, bb / 1024, c / 1024, d / 1024, tx / 20, ty / 20))
            elif tag == 9:
                ra, ga, ba, am, rm, gm, bm = [r.u8() for _ in range(7)]
                self.colors.append((np.array([rm, gm, bm, am], np.float32) / 255, np.array([ra, ga, ba], np.float32) / 255))
            r.p = end

    def shape(self, r, tag):
        sid = r.u16()
        r.u16()
        if tag == 18:
            r.u16()
        regions = []
        while True:
            st, ln = r.u8(), r.u32()
            end = r.p + ln
            if st == 0:
                break
            if st in (4, 17, 22):
                t = r.u8()
                n = 4 if st == 4 else r.u8()
                xy = np.array([(r.i32() / 20, r.i32() / 20) for _ in range(n)])
                uv = np.array([(r.u16(), r.u16()) for _ in range(n)], float)
                if st == 22:
                    th, tw = self.tex[t].shape[:2]
                    uv = uv / 65535 * np.array([tw, th])
                regions.append((t, xy, uv))
            r.p = end
        self.shapes[sid] = regions

    def clip(self, r, tag):
        cid = r.u16()
        r.u8()
        r.u16()
        cnt = r.u32()
        elems = [(r.u16(), r.u16(), r.u16()) for _ in range(cnt)]
        nb = r.u16()
        binds = [r.u16() for _ in range(nb)]
        if tag in (12, 35):
            r.p += nb
        names = [r.s() for _ in range(nb)]
        frames = []
        ei = 0
        while True:
            st, ln = r.u8(), r.u32()
            end = r.p + ln
            if st == 0:
                break
            if st == 11:
                n = r.u16()
                frames.append((r.s(), elems[ei:ei + n]))
                ei += n
            r.p = end
        self.clips[cid] = (list(zip(binds, names)), frames)

    def frames(self, name):
        return len(self.clips[self.exports[name]][1])

    def labels(self, name):
        return {lab: i for i, (lab, _) in enumerate(self.clips[self.exports[name]][1]) if lab}

    def walk(self, cid, frame, m, col, out, hide=()):
        if cid in self.shapes:
            for t, xy, uv in self.shapes[cid]:
                out.append((t, xy, uv, m, col))
            return
        if cid not in self.clips:
            return
        binds, frames = self.clips[cid]
        if not frames:
            return
        for ci, mi, coi in frames[frame % len(frames)][1]:
            child, name = binds[ci]
            if name in hide:
                continue
            cm = m if mi == 0xFFFF else mul(m, self.matrices[mi])
            cc = col
            if coi != 0xFFFF:
                k, add = self.colors[coi]
                cc = (k, add) if col is None else (col[0] * k, col[1] + add)
            self.walk(child, frame, cm, cc, out, hide)

    def pieces(self, name, frame=0, m=(1, 0, 0, 1, 0, 0), hide=()):
        out = []
        self.walk(self.exports[name], frame, m, None, out, hide)
        return out

    def bounds(self, name, frame=0, m=(1, 0, 0, 1, 0, 0)):
        pts = []
        for t, xy, uv, mm, col in self.pieces(name, frame, m):
            a, b, c, d, tx, ty = mm
            pts += [(a * x + c * y + tx, b * x + d * y + ty) for x, y in xy]
        p = np.array(pts)
        return p.min(0), p.max(0)


def mul(m, n):
    a, b, c, d, tx, ty = m
    a2, b2, c2, d2, tx2, ty2 = n
    return (a * a2 + c * b2, b * a2 + d * b2, a * c2 + c * d2, b * c2 + d * d2, a * tx2 + c * ty2 + tx, b * tx2 + d * ty2 + ty)


class Canvas:
    """premultiplied RGBA floats; (ox, oy) is where the picture's (0, 0) lands; drawn ss times larger, then reduced"""

    def __init__(self, w, h, ox, oy, scale=1.0, ss=4):
        self.ss = ss
        self.w, self.h = w * ss, h * ss
        self.ox, self.oy, self.scale = ox * ss, oy * ss, scale * ss
        self.img = np.zeros((self.h, self.w, 4), np.float32)

    def draw(self, sc, pieces):
        for t, xy, uv, m, col in pieces:
            self.region(sc.tex[t], xy, uv, m, col)

    def region(self, tex, xy, uv, m, col):
        a, b, c, d, tx, ty = m
        pts = np.array([(a * x + c * y + tx, b * x + d * y + ty) for x, y in xy]) * self.scale + (self.ox, self.oy)
        x0, y0 = np.floor(pts.min(0)).astype(int)
        x1, y1 = np.ceil(pts.max(0)).astype(int)
        x0, y0, x1, y1 = max(x0, 0), max(y0, 0), min(x1, self.w), min(y1, self.h)
        if x0 >= x1 or y0 >= y1:
            return
        A, _, rank, _ = np.linalg.lstsq(np.c_[pts, np.ones(len(pts))], uv, rcond=None)
        if rank < 3:
            return
        mask = Image.new('L', (x1 - x0, y1 - y0), 0)
        ImageDraw.Draw(mask).polygon([(px - x0, py - y0) for px, py in pts], fill=255)
        mk = np.asarray(mask, np.float32)[..., None] / 255
        ys, xs = np.mgrid[y0:y1, x0:x1] + 0.5
        th, tw = tex.shape[:2]
        u = np.clip(A[0, 0] * xs + A[1, 0] * ys + A[2, 0] - 0.5, 0, tw - 1)
        v = np.clip(A[0, 1] * xs + A[1, 1] * ys + A[2, 1] - 0.5, 0, th - 1)
        ui, vi = u.astype(int), v.astype(int)
        uf, vf = (u - ui)[..., None], (v - vi)[..., None]
        u1, v1 = np.minimum(ui + 1, tw - 1), np.minimum(vi + 1, th - 1)
        s = (tex[vi, ui] * (1 - uf) * (1 - vf) + tex[vi, u1] * uf * (1 - vf) + tex[v1, ui] * (1 - uf) * vf
             + tex[v1, u1] * uf * vf)
        if col is not None:
            k, add = col
            s = s * np.array([k[0] * k[3], k[1] * k[3], k[2] * k[3], k[3]], np.float32)
            s[..., :3] = np.minimum(s[..., :3] + add * s[..., 3:4], s[..., 3:4])
        s = s * mk
        dst = self.img[y0:y1, x0:x1]
        self.img[y0:y1, x0:x1] = s + dst * (1 - s[..., 3:4])

    def rgba(self):
        """straight RGBA floats at the final size"""
        im = self.img
        if self.ss > 1:
            h, w = self.h // self.ss, self.w // self.ss
            im = im.reshape(h, self.ss, w, self.ss, 4).mean((1, 3))
        a = im[..., 3:4]
        rgb = np.where(a > 1e-4, im[..., :3] / np.maximum(a, 1e-4), 0)
        return np.concatenate([np.clip(rgb, 0, 1), np.clip(a, 0, 1)], -1)
