/* Among Us: a small raw DEFLATE decoder (RFC 1951) for the packed pictures in data.bin. */
#include <string.h>
#include "inflate.h"

typedef struct {
  uint16_t counts[16];   /* codes of each length */
  uint16_t symbols[288]; /* symbols by code */
} Huff;

typedef struct {
  const uint8_t *in, *end;
  uint32_t bits;
  int nbits;
  uint8_t *out;
  uint32_t n, cap;
  int err;
} St;

static int bit(St *s) {
  if (!s->nbits) {
    if (s->in >= s->end) {
      s->err = 1;
      return 0;
    }
    s->bits = *s->in++;
    s->nbits = 8;
  }
  int b = s->bits & 1;
  s->bits >>= 1;
  s->nbits--;
  return b;
}

static uint32_t bits(St *s, int n) {
  uint32_t v = 0;
  for (int i = 0; i < n; i++) v |= (uint32_t)bit(s) << i;
  return v;
}

static void build(Huff *h, const uint8_t *lens, int n) {
  uint16_t offs[16];
  memset(h->counts, 0, sizeof h->counts);
  for (int i = 0; i < n; i++) h->counts[lens[i]]++;
  h->counts[0] = 0;
  offs[1] = 0;
  for (int i = 1; i < 15; i++) offs[i + 1] = (uint16_t)(offs[i] + h->counts[i]);
  for (int i = 0; i < n; i++)
    if (lens[i]) h->symbols[offs[lens[i]]++] = (uint16_t)i;
}

static int decode(St *s, const Huff *h) {
  int code = 0, first = 0, index = 0;
  for (int len = 1; len < 16; len++) {
    code |= bit(s);
    int count = h->counts[len];
    if (code - count < first) return h->symbols[index + (code - first)];
    index += count;
    first += count;
    first <<= 1;
    code <<= 1;
    if (s->err) return -1;
  }
  s->err = 1;
  return -1;
}

static const uint16_t LBASE[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                   31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
static const uint8_t LEXT[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
static const uint16_t DBASE[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,   33,   49,   65,    97,    129,
                                   193, 257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
static const uint8_t DEXT[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

static void block(St *s, const Huff *lit, const Huff *dist) {
  for (;;) {
    int sym = decode(s, lit);
    if (s->err) return;
    if (sym < 256) {
      if (s->n >= s->cap) {
        s->err = 1;
        return;
      }
      s->out[s->n++] = (uint8_t)sym;
    } else if (sym == 256) {
      return;
    } else {
      sym -= 257;
      if (sym >= 29) {
        s->err = 1;
        return;
      }
      uint32_t len = LBASE[sym] + bits(s, LEXT[sym]);
      int ds = decode(s, dist);
      if (ds < 0 || ds >= 30) {
        s->err = 1;
        return;
      }
      uint32_t d = DBASE[ds] + bits(s, DEXT[ds]);
      if (d > s->n || s->n + len > s->cap) {
        s->err = 1;
        return;
      }
      for (uint32_t i = 0; i < len; i++, s->n++) s->out[s->n] = s->out[s->n - d];
    }
  }
}

int32_t inflate_raw(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap) {
  static Huff lit, dist;
  St s = {in, in + n, 0, 0, out, 0, cap, 0};
  int last;
  do {
    last = bit(&s);
    int type = (int)bits(&s, 2);
    if (type == 0) {
      s.nbits = 0;
      if (s.in + 4 > s.end) return -1;
      uint32_t len = s.in[0] | s.in[1] << 8;
      s.in += 4;
      if (s.in + len > s.end || s.n + len > cap) return -1;
      memcpy(out + s.n, s.in, len);
      s.in += len, s.n += len;
    } else if (type == 1) {
      uint8_t lens[288];
      for (int i = 0; i < 144; i++) lens[i] = 8;
      for (int i = 144; i < 256; i++) lens[i] = 9;
      for (int i = 256; i < 280; i++) lens[i] = 7;
      for (int i = 280; i < 288; i++) lens[i] = 8;
      build(&lit, lens, 288);
      for (int i = 0; i < 30; i++) lens[i] = 5;
      build(&dist, lens, 30);
      block(&s, &lit, &dist);
    } else if (type == 2) {
      static const uint8_t ORDER[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
      int hlit = (int)bits(&s, 5) + 257, hdist = (int)bits(&s, 5) + 1, hclen = (int)bits(&s, 4) + 4;
      uint8_t lens[320];
      memset(lens, 0, 19);
      for (int i = 0; i < hclen; i++) lens[ORDER[i]] = (uint8_t)bits(&s, 3);
      Huff code;
      build(&code, lens, 19);
      int k = 0;
      while (k < hlit + hdist) {
        int sym = decode(&s, &code);
        if (s.err) return -1;
        if (sym < 16) lens[k++] = (uint8_t)sym;
        else {
          int rep = 0;
          uint8_t v = 0;
          if (sym == 16) {
            if (!k) return -1;
            v = lens[k - 1], rep = 3 + (int)bits(&s, 2);
          } else if (sym == 17) rep = 3 + (int)bits(&s, 3);
          else rep = 11 + (int)bits(&s, 7);
          if (k + rep > hlit + hdist) return -1;
          while (rep--) lens[k++] = v;
        }
      }
      build(&lit, lens, hlit);
      build(&dist, lens + hlit, hdist);
      block(&s, &lit, &dist);
    } else
      return -1;
    if (s.err) return -1;
  } while (!last);
  return (int32_t)s.n;
}
