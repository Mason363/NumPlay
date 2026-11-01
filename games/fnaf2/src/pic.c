/* The pictures (tools/codec.py): 16 x 16 squares coded as JPEG codes them, decoded as the bands are drawn.
 *
 * A square: four 8 x 8 blocks of Y, one of Cb and one of Cr (at half resolution), each a Huffman-coded
 * list of quantized cosine coefficients. The inverse transform is libjpeg's "ifast" one (the AAN algorithm,
 * its scales folded into the quantization tables by tools/pack.py), with a shortcut for the many blocks
 * that are one flat colour in these dark rooms. Colours are dithered on the way to 16 bits, or the dark
 * gradients would band. */
#include "gfx.h"
#include "res.h"

typedef struct {
  const uint16_t *lut;     /* 9 bits ahead: length << 8 | symbol, 0 when the code is longer */
  const int32_t *maxcode;  /* JPEG's, by length (1 to 17) */
  const int32_t *valoff;
  const uint8_t *vals;
} Huff;

static const uint8_t zigzag[64 + 16] = {
  0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7,
  14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46,
  53, 60, 61, 54, 47, 55, 62, 63,
  63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63, 63   /* (a corrupt run lands here) */
};

static Huff huff_tab(int k) {
  const uint8_t *p = section(SEC_HUFF) + 1424 * k;
  return (Huff){(const uint16_t *)(const void *)p, (const int32_t *)(const void *)(p + 1024),
                (const int32_t *)(const void *)(p + 1096), p + 1168};
}

/* ------------------------------------------------------------------ bits */
typedef struct {
  const uint8_t *p;
  uint32_t buf;   /* the next bits, from the top */
  int n;
} Bits;

static inline void fill(Bits *b) {
  while (b->n <= 24) {
    b->buf |= (uint32_t)*b->p++ << (24 - b->n);
    b->n += 8;
  }
}
static inline uint32_t take(Bits *b, int k) {
  uint32_t v = b->buf >> (32 - k);
  b->buf <<= k;
  b->n -= k;
  return v;
}
static inline int decode(Bits *b, const Huff *h) {
  fill(b);
  uint32_t e = h->lut[b->buf >> 23];
  if (e) {
    b->buf <<= e >> 8;
    b->n -= (int)(e >> 8);
    return (int)(e & 0xFF);
  }
  for (int l = 10; l <= 16; l++) {
    int32_t code = (int32_t)(b->buf >> (32 - l));
    if (code <= h->maxcode[l]) {
      b->buf <<= l;
      b->n -= l;
      return h->vals[(code + h->valoff[l]) & 0xFF];
    }
  }
  b->buf <<= 16;   /* (corrupt: give up on this block) */
  b->n -= 16;
  return 0;
}
/* JPEG's extension of s bits to a signed value */
static inline int value(Bits *b, int s) {
  if (!s) return 0;
  fill(b);
  int v = (int)take(b, s);
  return v < 1 << (s - 1) ? v - (1 << s) + 1 : v;
}

/* ------------------------------------------------------------------ one block */
#define CONST_BITS 8
#define MUL(v, c) (((v) * (c)) >> CONST_BITS)
#define FIX_1_082392200 277
#define FIX_1_414213562 362
#define FIX_1_847759065 473
#define FIX_2_613125930 669

static inline uint8_t clamp8(int v) { return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v); }

/* decodes a block into out (8 x 8 bytes, stride 8); *pred: the DC before it */
static void block(Bits *b, const Huff *dc, const Huff *ac, int *pred, const uint16_t *q, uint8_t *out) {
  int32_t c[64];
  uint64_t cols = 0;   /* which coefficients are not 0, a bit each */
  int s = decode(b, dc);
  *pred += value(b, s);
  int dcv = *pred * q[0];
  for (int k = 1; k < 64;) {
    int rs = decode(b, ac), r = rs >> 4;
    s = rs & 15;
    if (!s) {
      if (r != 15) break;
      k += 16;
      continue;
    }
    k += r;
    if (k > 63) break;
    int z = zigzag[k++];
    cols |= (uint64_t)1 << z;
    c[z] = value(b, s) * q[z];
  }
  if (!cols) {   /* one colour */
    uint8_t v = clamp8(((dcv + 16) >> 5) + 128);
    uint32_t w = v * 0x01010101u;
    for (int i = 0; i < 16; i++) ((uint32_t *)(void *)out)[i] = w;
    return;
  }
  c[0] = dcv;
  int32_t ws[64];
  for (int x = 0; x < 8; x++) {   /* columns */
    int32_t *in = c + x, *o = ws + x;
#define IN(k) ((cols >> (x + 8 * (k)) & 1) ? in[8 * (k)] : 0)
    if (!(cols >> 8 & (0x01010101010101ull << x))) {
      int32_t d = x ? IN(0) : dcv;
      for (int k = 0; k < 8; k++) o[8 * k] = d;
      continue;
    }
    int32_t t0 = x ? IN(0) : dcv, t1 = IN(2), t2 = IN(4), t3 = IN(6);
    int32_t t10 = t0 + t2, t11 = t0 - t2, t13 = t1 + t3, t12 = MUL(t1 - t3, FIX_1_414213562) - t13;
    t0 = t10 + t13, t3 = t10 - t13, t1 = t11 + t12, t2 = t11 - t12;
    int32_t t4 = IN(1), t5 = IN(3), t6 = IN(5), t7 = IN(7);
#undef IN
    int32_t z13 = t6 + t5, z10 = t6 - t5, z11 = t4 + t7, z12 = t4 - t7;
    t7 = z11 + z13;
    t11 = MUL(z11 - z13, FIX_1_414213562);
    int32_t z5 = MUL(z10 + z12, FIX_1_847759065);
    t10 = MUL(z12, FIX_1_082392200) - z5;
    t12 = MUL(z10, -FIX_2_613125930) + z5;
    t6 = t12 - t7, t5 = t11 - t6, t4 = t10 + t5;
    o[0] = t0 + t7, o[56] = t0 - t7, o[8] = t1 + t6, o[48] = t1 - t6;
    o[16] = t2 + t5, o[40] = t2 - t5, o[32] = t3 + t4, o[24] = t3 - t4;
  }
  for (int y = 0; y < 8; y++) {   /* rows: +128 and rounding folded into the first */
    int32_t *w = ws + 8 * y;
    uint8_t *o = out + 8 * y;
    int32_t w0 = w[0] + (128 << 5) + 16;
    int32_t t10 = w0 + w[4], t11 = w0 - w[4], t13 = w[2] + w[6], t12 = MUL(w[2] - w[6], FIX_1_414213562) - t13;
    int32_t t0 = t10 + t13, t3 = t10 - t13, t1 = t11 + t12, t2 = t11 - t12;
    int32_t z13 = w[5] + w[3], z10 = w[5] - w[3], z11 = w[1] + w[7], z12 = w[1] - w[7];
    int32_t t7 = z11 + z13, t11b = MUL(z11 - z13, FIX_1_414213562), z5 = MUL(z10 + z12, FIX_1_847759065);
    int32_t t10b = MUL(z12, FIX_1_082392200) - z5, t12b = MUL(z10, -FIX_2_613125930) + z5;
    int32_t t6 = t12b - t7, t5 = t11b - t6, t4 = t10b + t5;
    o[0] = clamp8((t0 + t7) >> 5), o[7] = clamp8((t0 - t7) >> 5);
    o[1] = clamp8((t1 + t6) >> 5), o[6] = clamp8((t1 - t6) >> 5);
    o[2] = clamp8((t2 + t5) >> 5), o[5] = clamp8((t2 - t5) >> 5);
    o[4] = clamp8((t3 + t4) >> 5), o[3] = clamp8((t3 - t4) >> 5);
  }
}

/* ------------------------------------------------------------------ one square */
#if defined(__ARM_FEATURE_SAT)
#include <arm_acle.h>
#define SAT8(v) __usat(v, 8)
#else
#define SAT8(v) ((v) < 0 ? 0 : (v) > 255 ? 255 : (v))
#endif
/* a 4 x 4 ordered dither: what is added to red and blue (0 to 7) and green (0 to 3) before they lose bits */
static const uint8_t dither_rb[4][4] = {{0, 4, 1, 5}, {6, 2, 7, 3}, {1, 5, 0, 4}, {7, 3, 6, 2}};
static const uint8_t dither_g[4][4] = {{0, 2, 0, 2}, {3, 1, 3, 1}, {0, 2, 0, 2}, {3, 1, 3, 1}};

/* decodes the square at data into dst (16 x 16, rows stride apart) */
static void square(const uint8_t *data, int qset, uint16_t *dst, int stride) {
  static Huff h[4];
  static const uint8_t *loaded;
  if (loaded != fnaf2_bin) {
    for (int k = 0; k < 4; k++) h[k] = huff_tab(k);
    loaded = fnaf2_bin;
  }
  const uint16_t *qy = (const uint16_t *)(const void *)(section(SEC_QUANT) + 256 * qset), *qc = qy + 64;
  uint8_t y[4][64] __attribute__((aligned(4))), cb[64] __attribute__((aligned(4))), cr[64] __attribute__((aligned(4)));
  Bits b = {data, 0, 0};
  int pred = 0;
  for (int k = 0; k < 4; k++) block(&b, &h[0], &h[1], &pred, qy, y[k]);
  pred = 0;
  block(&b, &h[2], &h[3], &pred, qc, cb);
  pred = 0;
  block(&b, &h[2], &h[3], &pred, qc, cr);
  for (int rp = 0; rp < 8; rp++) {   /* two rows at a time, sharing their colours */
    int dr[8], dg[8], db[8];
    for (int c = 0; c < 8; c++) {
      int u = cb[rp * 8 + c] - 128, v = cr[rp * 8 + c] - 128;
      dr[c] = (91881 * v + 32768) >> 16;
      dg[c] = (-22554 * u - 46802 * v + 32768) >> 16;
      db[c] = (116130 * u + 32768) >> 16;
    }
    for (int r = 2 * rp; r < 2 * rp + 2; r++) {
      const uint8_t *yl = y[(r >> 3) * 2] + (r & 7) * 8, *drb = dither_rb[r & 3], *dgg = dither_g[r & 3];
      uint16_t *o = dst + r * stride;
      for (int c = 0; c < 16; c++) {
        if (c == 8) yl = y[(r >> 3) * 2 + 1] + (r & 7) * 8;
        int l = yl[c & 7], k = c >> 1;
        int R = SAT8(l + dr[k] + drb[c & 3]), G = SAT8(l + dg[k] + dgg[c & 3]), B = SAT8(l + db[k] + drb[(c + 2) & 3]);
        o[c] = (uint16_t)((R & 0xF8) << 8 | (G & 0xFC) << 3 | B >> 3);
      }
    }
  }
}

/* ------------------------------------------------------------------ pictures */
typedef struct {
  int mw, mh, q;
  const uint8_t *table, *base_table;   /* base_table: the picture whose squares it shares, or NULL */
  int count;                           /* its own squares then */
} Info;

static Info info(int id) {
  const uint8_t *p = section(SEC_PICS) + 12 * id, *t = section(SEC_TABLE);
  Info i = {p[0], p[1], p[2], t + rd32(p + 8), NULL, (int)rd16(p + 6)};
  uint32_t base = rd16(p + 4);
  if (base != 0xFFFF) i.base_table = t + rd32(section(SEC_PICS) + 12 * base + 8);
  return i;
}

static uint32_t square_at(const Info *i, int k) {
  if (!i->base_table) return rd24(i->table + 3 * k);
  int lo = 0, hi = i->count - 1;
  while (lo <= hi) {
    int mid = (lo + hi) >> 1, mk = (int)rd16(i->table + 5 * mid);
    if (mk == k) return rd24(i->table + 5 * mid + 2);
    if (mk < k) lo = mid + 1;
    else hi = mid - 1;
  }
  return rd24(i->base_table + 3 * k);
}

int pic_w(int id) { return 16 * section(SEC_PICS)[12 * id]; }
int pic_h(int id) { return 16 * section(SEC_PICS)[12 * id + 1]; }

void pic_part(int id, int x, int y, int sx, int sy, int w, int h) {
  Info in = info(id);
  /* clip to the screen, the band and the picture */
  int x0 = x > 0 ? x : 0, x1 = x + w < SCREEN_W ? x + w : SCREEN_W;
  int y0 = y > gfx_y0 ? y : gfx_y0, y1 = y + h < gfx_y0 + BAND_H ? y + h : gfx_y0 + BAND_H;
  if (x0 >= x1 || y0 >= y1) return;
  const uint8_t *bank = section(SEC_BANK);
  uint16_t px[256];
  /* in the picture: rows y0 - y + sy to y1 - y + sy, columns likewise */
  int pr0 = y0 - y + sy, pr1 = y1 - y + sy, pc0 = x0 - x + sx, pc1 = x1 - x + sx;
  for (int my = pr0 >> 4; my <= (pr1 - 1) >> 4 && my < in.mh; my++)
    for (int mx = pc0 >> 4; mx <= (pc1 - 1) >> 4 && mx < in.mw; mx++) {
      const uint8_t *data = bank + square_at(&in, my * in.mw + mx);
      int r0 = my * 16 > pr0 ? my * 16 : pr0, r1 = my * 16 + 16 < pr1 ? my * 16 + 16 : pr1;
      int c0 = mx * 16 > pc0 ? mx * 16 : pc0, c1 = mx * 16 + 16 < pc1 ? mx * 16 + 16 : pc1;
      if (r1 - r0 == 16 && c1 - c0 == 16) {   /* all of it shows: straight into the band */
        square(data, in.q, gfx_band + (r0 - sy + y - gfx_y0) * SCREEN_W + (c0 - sx + x), SCREEN_W);
        continue;
      }
      square(data, in.q, px, 16);
      for (int r = r0; r < r1; r++) {
        uint16_t *d = gfx_band + (r - sy + y - gfx_y0) * SCREEN_W + (c0 - sx + x);
        const uint16_t *s = px + (r - my * 16) * 16 + (c0 - mx * 16);
        for (int c = c0; c < c1; c++) *d++ = *s++;
      }
    }
}

void pic_draw(int id, int x, int y) { pic_part(id, x, y, 0, 0, pic_w(id), pic_h(id)); }
