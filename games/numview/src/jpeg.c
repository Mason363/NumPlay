/* NumView's baseline JPEG decoder: see jpeg.h. */
#include "jpeg.h"
#include <string.h>

/* the natural index of each coefficient in zigzag order, with 15 spares for broken data */
static const uint8_t dezigzag[64 + 16] = {
    0,  1,  8,  16, 9,  2,  3,  10, 17, 24, 32, 25, 18, 11, 4,  5,  12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,
    6,  7,  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31,
    39, 46, 53, 60, 61, 54, 47, 55, 62, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63};

/* ------------------------------------------------------------------ Huffman tables */
static bool build_huff(jpeg_huff_t *h, const uint8_t *count, const uint8_t *values, int total) {
  int k = 0;
  for (int i = 0; i < 16; i++)
    for (int n = 0; n < count[i]; n++) {
      if (k >= 256) return false;
      h->size[k++] = (uint8_t)(i + 1);
    }
  h->size[k] = 0;
  if (k != total) return false;
  uint32_t code = 0;
  k = 0;
  for (int len = 1; len <= 16; len++) {
    h->delta[len] = k - (int32_t)code;
    if (h->size[k] == len) {
      while (h->size[k] == len) h->code[k++] = (uint16_t)code++;
      if (code - 1 >= (1u << len)) return false;
    }
    h->maxcode[len] = code << (16 - len);
    code <<= 1;
  }
  h->maxcode[17] = 0xFFFFFFFFu;
  memset(h->fast, 255, sizeof h->fast);
  for (int i = 0; i < k; i++) {
    int s = h->size[i];
    if (s <= 9) {
      int c = h->code[i] << (9 - s), m = 1 << (9 - s);
      for (int n = 0; n < m; n++) h->fast[c + n] = (uint8_t)i;
    }
  }
  memcpy(h->values, values, (size_t)total);
  return true;
}

/* ------------------------------------------------------------------ bits */
typedef struct {
  const uint8_t *p, *end;
  uint32_t buf;
  int bits, marker;
  bool nomore;
} bits_t;

static void grow(bits_t *b) {
  do {
    uint32_t c = 0;
    if (!b->nomore) {
      if (b->p >= b->end) {
        b->nomore = true;
      } else {
        c = *b->p++;
        if (c == 0xFF) {
          int m = b->p < b->end ? *b->p++ : 0xD9;
          while (m == 0xFF && b->p < b->end) m = *b->p++;
          if (m != 0) {
            b->marker = m;
            b->nomore = true;
            return;
          }
        }
      }
    }
    b->buf |= c << (24 - b->bits);
    b->bits += 8;
  } while (b->bits <= 24);
}

static int huff(bits_t *b, const jpeg_huff_t *h) {
  if (b->bits < 16) grow(b);
  int k = h->fast[(b->buf >> 23) & 511];
  if (k < 255) {
    int s = h->size[k];
    if (s > b->bits) return -1;
    b->buf <<= s;
    b->bits -= s;
    return h->values[k];
  }
  uint32_t t = b->buf >> 16;
  for (k = 10; k < 17; k++)
    if (t < h->maxcode[k]) break;
  if (k >= 17 || k > b->bits) return -1;
  int c = (int)((b->buf >> (32 - k)) & ((1u << k) - 1)) + h->delta[k];
  if (c < 0 || c > 255) return -1;
  b->bits -= k;
  b->buf <<= k;
  return h->values[c];
}

static int extend(bits_t *b, int n) {
  if (n <= 0 || n > 16) return 0;
  if (b->bits < n) grow(b);
  if (b->bits < n) return 0;
  int v = (int)(b->buf >> (32 - n));
  b->buf <<= n;
  b->bits -= n;
  return v < (1 << (n - 1)) ? v + 1 - (1 << n) : v;
}

/* one block's coefficients, dequantized, in natural order: -1 broken, 0 its DC alone, 1 with AC */
static int block(bits_t *b, int16_t *data, const jpeg_huff_t *hdc, const jpeg_huff_t *hac, int *pred,
                 const uint16_t *q) {
  memset(data, 0, 64 * sizeof *data);
  int t = huff(b, hdc);
  if (t < 0 || t > 15) return -1;
  int dc = *pred + extend(b, t);
  *pred = dc;
  data[0] = (int16_t)(dc * q[0]);
  int ac = 0;
  for (int k = 1; k < 64;) {
    int rs = huff(b, hac);
    if (rs < 0) return -1;
    int s = rs & 15, r = rs >> 4;
    if (!s) {
      if (rs != 0xF0) break;
      k += 16;
    } else {
      k += r;
      int z = dezigzag[k++];
      data[z] = (int16_t)(extend(b, s) * q[z]);
      ac = 1;
    }
  }
  return ac;
}

/* ------------------------------------------------------------------ IDCT (stb_image's) */
#define F2F(x) ((int)((x)*4096 + 0.5f))
#define FSH(x) ((x)*4096)
#define IDCT_1D(s0, s1, s2, s3, s4, s5, s6, s7)                                                              \
  int t0, t1, t2, t3, p1, p2, p3, p4, p5, x0, x1, x2, x3;                                                    \
  p2 = s2, p3 = s6;                                                                                          \
  p1 = (p2 + p3) * F2F(0.5411961f);                                                                          \
  t2 = p1 + p3 * F2F(-1.847759065f);                                                                         \
  t3 = p1 + p2 * F2F(0.765366865f);                                                                          \
  p2 = s0, p3 = s4;                                                                                          \
  t0 = FSH(p2 + p3), t1 = FSH(p2 - p3);                                                                      \
  x0 = t0 + t3, x3 = t0 - t3, x1 = t1 + t2, x2 = t1 - t2;                                                     \
  t0 = s7, t1 = s5, t2 = s3, t3 = s1;                                                                        \
  p3 = t0 + t2, p4 = t1 + t3, p1 = t0 + t3, p2 = t1 + t2;                                                    \
  p5 = (p3 + p4) * F2F(1.175875602f);                                                                        \
  t0 = t0 * F2F(0.298631336f), t1 = t1 * F2F(2.053119869f), t2 = t2 * F2F(3.072711026f);                     \
  t3 = t3 * F2F(1.501321110f);                                                                               \
  p1 = p5 + p1 * F2F(-0.899976223f), p2 = p5 + p2 * F2F(-2.562915447f);                                      \
  p3 = p3 * F2F(-1.961570560f), p4 = p4 * F2F(-0.390180644f);                                                \
  t3 += p1 + p4, t2 += p2 + p3, t1 += p2 + p4, t0 += p1 + p3;

static inline uint8_t clamp8(int x) { return (unsigned)x > 255 ? (x < 0 ? 0 : 255) : (uint8_t)x; }

static void idct(uint8_t *out, const int16_t *d) {
  int val[64], *v = val;
  for (int i = 0; i < 8; i++, d++, v++) {
    if (!d[8] && !d[16] && !d[24] && !d[32] && !d[40] && !d[48] && !d[56]) {
      int dc = d[0] * 4;
      v[0] = v[8] = v[16] = v[24] = v[32] = v[40] = v[48] = v[56] = dc;
    } else {
      IDCT_1D(d[0], d[8], d[16], d[24], d[32], d[40], d[48], d[56])
      x0 += 512, x1 += 512, x2 += 512, x3 += 512;
      v[0] = (x0 + t3) >> 10, v[56] = (x0 - t3) >> 10;
      v[8] = (x1 + t2) >> 10, v[48] = (x1 - t2) >> 10;
      v[16] = (x2 + t1) >> 10, v[40] = (x2 - t1) >> 10;
      v[24] = (x3 + t0) >> 10, v[32] = (x3 - t0) >> 10;
    }
  }
  v = val;
  for (int i = 0; i < 8; i++, v += 8, out += 8) {
    IDCT_1D(v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7])
    x0 += 65536 + (128 << 17), x1 += 65536 + (128 << 17), x2 += 65536 + (128 << 17), x3 += 65536 + (128 << 17);
    out[0] = clamp8((x0 + t3) >> 17), out[7] = clamp8((x0 - t3) >> 17);
    out[1] = clamp8((x1 + t2) >> 17), out[6] = clamp8((x1 - t2) >> 17);
    out[2] = clamp8((x2 + t1) >> 17), out[5] = clamp8((x2 - t1) >> 17);
    out[3] = clamp8((x3 + t0) >> 17), out[4] = clamp8((x3 - t0) >> 17);
  }
}

/* ------------------------------------------------------------------ colour */
static inline uint16_t ycc565(int y, int cb, int cr) {
  cb -= 128, cr -= 128;
  int r = y + ((91881 * cr + 32768) >> 16);
  int g = y - ((22554 * cb + 46802 * cr - 32768) >> 16);
  int b = y + ((116130 * cb + 32768) >> 16);
  r = clamp8(r), g = clamp8(g), b = clamp8(b);
  return (uint16_t)((r & 0xF8) << 8 | (g & 0xFC) << 3 | b >> 3);
}
static inline uint16_t gray565(int y) { return (uint16_t)((y & 0xF8) << 8 | (y & 0xFC) << 3 | y >> 3); }

/* an MCU's pixels (blocks in pix[component][by * 2 + bx]) into out at 1/d */
static void put_mcu(const jpeg_t *j, uint8_t pix[3][4][64], int mw, int mh, int d, uint16_t *out, int stride,
                    int left, int top) {
  int ow = mw / d, oh = mh / d;
  if (ow > left) ow = left;
  if (oh > top) oh = top;
  bool color = j->ncomp == 3;
  int hs1 = color && j->comp[1].h < j->hmax, vs1 = color && j->comp[1].v < j->vmax;
  int hs2 = color && j->comp[2].h < j->hmax, vs2 = color && j->comp[2].v < j->vmax;
  for (int yy = 0; yy < oh; yy++) {
    uint16_t *row = out + yy * stride;
    int py = yy * d;
    for (int xx = 0; xx < ow; xx++) {
      int px = xx * d, y;
      const uint8_t *yb = pix[0][(py >> 3) * 2 + (px >> 3)];
      if (d == 1) y = yb[(py & 7) * 8 + (px & 7)];
      else if (d == 8) y = yb[0];
      else {
        int s = 0;
        for (int a = 0; a < d; a++)
          for (int c = 0; c < d; c++) s += yb[((py & 7) + a) * 8 + (px & 7) + c];
        y = s / (d * d);
      }
      if (!color) {
        row[xx] = gray565(y);
        continue;
      }
      int h = d >> 1; /* (the chroma under the middle of the pixel) */
      int cx = (px + h) >> hs1, cy = (py + h) >> vs1;
      const uint8_t *cb = pix[1][(cy >> 3) * 2 + (cx >> 3)] + (d == 8 ? 0 : (cy & 7) * 8 + (cx & 7));
      cx = (px + h) >> hs2, cy = (py + h) >> vs2;
      const uint8_t *cr = pix[2][(cy >> 3) * 2 + (cx >> 3)] + (d == 8 ? 0 : (cy & 7) * 8 + (cx & 7));
      row[xx] = ycc565(y, *cb, *cr);
    }
  }
}

bool jpeg_scan(const jpeg_t *j, const uint8_t *data, uint32_t n, int w, int h, int d, uint16_t *out, int stride) {
  if (!j->ok || w <= 0 || h <= 0 || (d != 1 && d != 2 && d != 4 && d != 8)) return false;
  bits_t b = {data, data + n, 0, 0, 0, false};
  bool one = j->ncomp == 1; /* (a single component's scan isn't interleaved: its blocks are its MCUs) */
  int mw = one ? 8 : 8 * j->hmax, mh = one ? 8 : 8 * j->vmax;
  int mx = (w + mw - 1) / mw, my = (h + mh - 1) / mh;
  int ow = (w + d - 1) / d, oh = (h + d - 1) / d;
  int pred[3] = {0, 0, 0}, todo = j->restart ? j->restart : 0x7FFFFFFF;
  static uint8_t pix[3][4][64];
  int16_t coef[64];
  for (int y = 0; y < my; y++)
    for (int x = 0; x < mx; x++) {
      for (int c = 0; c < j->ncomp; c++) {
        int bh = one ? 1 : j->comp[c].h, bv = one ? 1 : j->comp[c].v;
        for (int by = 0; by < bv; by++)
          for (int bx = 0; bx < bh; bx++) {
            int r = block(&b, coef, &j->dc[j->comp[c].td], &j->ac[j->comp[c].ta], &pred[c], j->q[j->comp[c].tq]);
            if (r < 0) return false;
            uint8_t *px = pix[c][by * 2 + bx];
            if (r == 0 || d == 8) memset(px, clamp8(128 + ((coef[0] + 4) >> 3)), d == 8 ? 1 : 64);
            else idct(px, coef);
          }
      }
      int ox = x * (mw / d), oy = y * (mh / d);
      put_mcu(j, pix, mw, mh, d, out + oy * stride + ox, stride, ow - ox, oh - oy);
      if (--todo <= 0) {
        if (b.bits < 24) grow(&b);
        if (b.marker < 0xD0 || b.marker > 0xD7) return true;
        b.buf = 0, b.bits = 0, b.nomore = false, b.marker = 0;
        pred[0] = pred[1] = pred[2] = 0;
        todo = j->restart;
      }
    }
  return true;
}

/* ------------------------------------------------------------------ headers */
/* reads segments up to and with SOS (or to the end); returns where the scan's data starts, or -1 */
static int32_t headers(jpeg_t *j, const uint8_t *p, uint32_t n) {
  uint32_t i = 0;
  bool sof = false;
  while (i + 2 <= n) {
    if (p[i] != 0xFF) {
      i++;
      continue;
    }
    int m = p[i + 1];
    i += 2;
    if (m == 0xD8 || m == 0x01 || m == 0xFF || (m >= 0xD0 && m <= 0xD7)) {
      if (m == 0xFF) i--;
      continue;
    }
    if (m == 0xD9) break;
    if (i + 2 > n) return -1;
    uint32_t len = (uint32_t)p[i] << 8 | p[i + 1];
    if (len < 2 || i + len > n) return -1;
    const uint8_t *s = p + i + 2;
    int32_t sl = (int32_t)len - 2;
    switch (m) {
      case 0xDB: /* DQT */
        while (sl > 0) {
          int pq = s[0] >> 4, tq = s[0] & 15;
          if (tq > 3 || sl < 1 + 64 * (pq + 1)) return -1;
          for (int k = 0; k < 64; k++)
            j->q[tq][dezigzag[k]] = pq ? (uint16_t)(s[1 + 2 * k] << 8 | s[2 + 2 * k]) : s[1 + k];
          s += 1 + 64 * (pq + 1), sl -= 1 + 64 * (pq + 1);
        }
        break;
      case 0xC4: /* DHT */
        while (sl > 0) {
          if (sl < 17) return -1;
          int tc = s[0] >> 4, th = s[0] & 15, total = 0;
          for (int k = 0; k < 16; k++) total += s[1 + k];
          if (tc > 1 || th > 1 || total > 256 || sl < 17 + total) return -1;
          if (!build_huff(tc ? &j->ac[th] : &j->dc[th], s + 1, s + 17, total)) return -1;
          s += 17 + total, sl -= 17 + total;
        }
        break;
      case 0xDD: /* DRI */
        if (sl < 2) return -1;
        j->restart = (uint16_t)(s[0] << 8 | s[1]);
        break;
      case 0xC0:
      case 0xC1: { /* SOF0, SOF1: baseline and extended Huffman */
        if (sl < 6 || s[0] != 8) return -1;
        j->h = (uint16_t)(s[1] << 8 | s[2]), j->w = (uint16_t)(s[3] << 8 | s[4]);
        int nc = s[5];
        if ((nc != 1 && nc != 3) || sl < 6 + 3 * nc) return -1;
        j->ncomp = (uint8_t)nc, j->hmax = j->vmax = 1;
        for (int c = 0; c < nc; c++) {
          const uint8_t *e = s + 6 + 3 * c;
          int hh = e[1] >> 4, vv = e[1] & 15;
          if (hh < 1 || hh > 2 || vv < 1 || vv > 2 || e[2] > 3) return -1;
          j->comp[c].id = e[0], j->comp[c].h = (uint8_t)hh, j->comp[c].v = (uint8_t)vv, j->comp[c].tq = e[2];
          if (hh > j->hmax) j->hmax = (uint8_t)hh;
          if (vv > j->vmax) j->vmax = (uint8_t)vv;
        }
        sof = true;
        break;
      }
      case 0xC2: case 0xC3: case 0xC5: case 0xC6: case 0xC7: case 0xC9: case 0xCA: case 0xCB: case 0xCD:
      case 0xCE: case 0xCF:
        return -1; /* progressive, lossless, arithmetic: not here */
      case 0xDA: { /* SOS */
        int ns = sl > 0 ? s[0] : 0;
        if (!sof || ns != j->ncomp || sl < 1 + 2 * ns) return -1;
        for (int k = 0; k < ns; k++) {
          int c = 0;
          while (c < j->ncomp && j->comp[c].id != s[1 + 2 * k]) c++;
          if (c == j->ncomp || (s[2 + 2 * k] >> 4) > 1 || (s[2 + 2 * k] & 15) > 1) return -1;
          j->comp[c].td = s[2 + 2 * k] >> 4, j->comp[c].ta = s[2 + 2 * k] & 15;
        }
        j->ok = 1;
        return (int32_t)(i + len);
      }
      default:
        break; /* APPn, COM */
    }
    i += len;
  }
  return (int32_t)n;
}

bool jpeg_setup(jpeg_t *j, const uint8_t *p, uint32_t n) {
  j->ok = 0, j->restart = 0;
  return headers(j, p, n) >= 0 && j->ok;
}

bool jpeg_decode(jpeg_t *j, const uint8_t *p, uint32_t n, int d, uint16_t *out, int stride, int *w, int *h) {
  j->ok = 0, j->restart = 0;
  int32_t at = headers(j, p, n);
  if (at < 0 || !j->ok) return false;
  if (w) *w = j->w;
  if (h) *h = j->h;
  return jpeg_scan(j, p + at, n - (uint32_t)at, j->w, j->h, d, out, stride);
}
