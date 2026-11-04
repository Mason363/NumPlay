/* Decoding the pictures (pic.h; tools/cpic.py writes them). Written for the eZ80 as much as C allows: int is
 * 24 bits there, so no value here needs more (the coefficients stay within +-2^17). */
#include "pic.h"
#include <string.h>

typedef struct {
  int32_t maxcode[18];   /* the largest code of each length (-1: none), 17 ends the search */
  int16_t valoff[17];    /* code - valoff[l]: the symbol's place in vals */
  const uint8_t *vals;
} Huff;

static Huff huff[4];         /* Y DC, Y AC, colour DC, colour AC */
static const uint8_t *quant; /* per quality: 16 Y steps then 16 colour steps (u16, raster order) */

static const uint8_t ZZ[16] = {0, 1, 4, 8, 5, 2, 3, 6, 9, 12, 13, 10, 7, 11, 14, 15};

void pic_init(const uint8_t *t) {
  int nq = *t++;
  quant = t;
  t += nq * 64;
  for (int k = 0; k < 4; k++) {
    Huff *h = &huff[k];
    const uint8_t *counts = t;
    t += 16;
    h->vals = t;
    int32_t code = 0;
    int n = 0;
    for (int l = 1; l <= 16; l++) {
      int c = counts[l - 1];
      h->valoff[l] = (int16_t)(code - n);
      code += c;
      n += c;
      h->maxcode[l] = c ? code - 1 : -1;
      code <<= 1;
    }
    h->maxcode[17] = 0x7FFFFFFF;
    t += n;
  }
}

int pic_width(const uint8_t *pic) { return pic[1] * 8; }
int pic_height(const uint8_t *pic) { return pic[2] * 8; }
int pic_base(const uint8_t *pic) { return pic[0] & 1 ? pic[4] | pic[5] << 8 : -1; }

/* ------------------------------------------------------------------ bits */
static const uint8_t *bp;
static uint8_t bitbuf, bitcnt;

static unsigned bit(void) {
  if (!bitcnt) bitbuf = *bp++, bitcnt = 8;
  bitcnt--;
  unsigned b = bitbuf >> 7;
  bitbuf = (uint8_t)(bitbuf << 1);
  return b;
}

static int bits(int n) {
  int v = 0;
  while (n--) v = v << 1 | (int)bit();
  return v;
}

/* JPEG's: s bits to a signed value */
static int extend(int s) {
  if (!s) return 0;
  int v = bits(s);
  return v < 1 << (s - 1) ? v - (1 << s) + 1 : v;
}

static int decode(const Huff *h) {
  int32_t code = 0;
  int l = 1;
  for (;; l++) {
    code = code << 1 | (int32_t)bit();
    if (code <= h->maxcode[l]) break;
    if (l == 16) return 0;   /* (corrupt) */
  }
  return h->vals[(code - h->valoff[l]) & 0xFF];
}

static int golomb(void) {
  int n = 0;
  while (!bit() && n < 20) n++;
  return ((1 << n) | bits(n)) - 1;
}

/* ------------------------------------------------------------------ a block */
/* decodes a 4 x 4 block into out (raster order): t the table pair (0 Y, 2 colour), q its steps */
static void block(int t, int *pred, const uint8_t *q, int *out) {
  int c[16];
  memset(c, 0, sizeof c);
  *pred += extend(decode(&huff[t]));
  c[0] = *pred * (q[0] | q[1] << 8);
  bool ac = false;
  for (int k = 1; k < 16; k++) {
    int rs = decode(&huff[t + 1]);
    if (!rs) break;
    k += rs >> 4;
    if (k > 15) break;
    int z = ZZ[k];
    c[z] = extend(rs & 15) * (q[2 * z] | q[2 * z + 1] << 8);
    ac = true;
  }
  if (!ac) {   /* one value */
    int v = (c[0] + 32) >> 6;
    for (int i = 0; i < 16; i++) out[i] = v;
    return;
  }
  int r[16];
  for (int i = 0; i < 16; i += 4) {   /* rows */
    int e = c[i] + c[i + 2], f = c[i] - c[i + 2];
    int g = (c[i + 1] >> 1) - c[i + 3], h = c[i + 1] + (c[i + 3] >> 1);
    r[i] = e + h, r[i + 1] = f + g, r[i + 2] = f - g, r[i + 3] = e - h;
  }
  for (int j = 0; j < 4; j++) {       /* columns */
    int e = r[j] + r[j + 8], f = r[j] - r[j + 8];
    int g = (r[j + 4] >> 1) - r[j + 12], h = r[j + 4] + (r[j + 12] >> 1);
    out[j] = (e + h + 32) >> 6, out[j + 4] = (f + g + 32) >> 6;
    out[j + 8] = (f - g + 32) >> 6, out[j + 12] = (e - h + 32) >> 6;
  }
}

/* ------------------------------------------------------------------ a picture */
/* the ordered dither: on Y (0..7) for each pixel of a macroblock, on Cb and Cr (0..30) for each 2 x 2 */
static const uint8_t DY[64] = {
  0, 4, 1, 5, 0, 4, 1, 5, 6, 2, 7, 3, 6, 2, 7, 3, 1, 5, 0, 4, 1, 5, 0, 4, 7, 3, 6, 2, 7, 3, 6, 2,
  0, 4, 1, 5, 0, 4, 1, 5, 6, 2, 7, 3, 6, 2, 7, 3, 1, 5, 0, 4, 1, 5, 0, 4, 7, 3, 6, 2, 7, 3, 6, 2};
static const uint8_t DCB[16] = {0, 16, 4, 20, 24, 8, 28, 12, 6, 22, 2, 18, 30, 14, 26, 10};
static const uint8_t DCR[16] = {30, 14, 26, 10, 6, 22, 2, 18, 24, 8, 28, 12, 0, 16, 4, 20};

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v >= hi ? hi - 1 : v; }

void pic_decode(const uint8_t *pic, uint8_t *dst, int stride, const PicMap *m) {
  int flags = pic[0], mw = pic[1], mh = pic[2];
  const uint8_t *qy = quant + 64 * pic[3], *qc = qy + 32;
  bp = pic + (flags & 1 ? 6 : 4);
  bitcnt = 0;
  bool skips = (flags & 2) != 0;
  int total = mw * mh, at = skips ? golomb() : 0, last = -1;
  int pred[3] = {0, 0, 0};
  int y[4][16], cb[16], cr[16];
  for (; at < total; at++) {
    int my = at / mw, mx = at - my * mw;
    if (at != last + 1 || mx == 0) pred[0] = pred[1] = pred[2] = 0;
    last = at;
    for (int k = 0; k < 4; k++) block(0, &pred[0], qy, y[k]);
    block(2, &pred[1], qc, cb);
    block(2, &pred[2], qc, cr);
    /* to palette indices: each 2 x 2 shares its colour */
    uint8_t *row = dst + my * 8 * stride + mx * 8;
    for (int ci = 0; ci < 16; ci++) {
      int cy = ci >> 2, cx = ci & 3;
      int part = (m->cbq[clampi(cb[ci], CQ_LO, CQ_HI) - CQ_LO] + DCB[ci]) >> 5 << 3 |
                 (m->crq[clampi(cr[ci], CQ_LO, CQ_HI) - CQ_LO] + DCR[ci]) >> 5;
      for (int dy = 0; dy < 2; dy++)
        for (int dx = 0; dx < 2; dx++) {
          int py = cy * 2 + dy, px = cx * 2 + dx;   /* in the macroblock */
          int v = y[(py >> 2) * 2 + (px >> 2)][(py & 3) * 4 + (px & 3)] + 128;
          uint8_t o = m->lut[(m->yq[clampi(v, YQ_LO, YQ_HI) - YQ_LO] + DY[py * 8 + px]) >> 3 << 6 | part];
          if (m->phase) o |= m->phase[(mx * 8 + px + m->phase[(my * 8 + py) & 255]) & 255] & 3;
          row[py * stride + px] = o;
        }
    }
    if (skips) at += golomb();
  }
}
