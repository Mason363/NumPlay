/* Single-precision floats done with whole numbers, IEEE 754 (round to nearest, ties to even), the same on the CE and
 * on a computer: Celeste's movement is float arithmetic, and its pixel-exact paths come from how each operation rounds.
 * Finite numbers only; results too small for a normal float are 0 (the game's never are). tests/sf.c checks it against
 * the computer's own floats. */
#include "ce.h"

#define SIGN 0x80000000ul
static inline int fexp(f32 a) { return (int)(a >> 23 & 255); }
static inline uint32_t fman(f32 a) { return a & 0x7FFFFFul; }

/* m: the significand with its top bit at bit 26 (3 bits below the float's last: guard, round, sticky), e the exponent */
static f32 pack(uint32_t sign, int e, uint32_t m) {
  if (!m) return sign;
  while (m >= 1ul << 27) m = m >> 1 | (m & 1), e++;   /* (sticky) */
  while (m < 1ul << 26) m <<= 1, e--;
  if (e <= 0) return sign;   /* too small */
  uint32_t low = m & 7;
  m >>= 3;
  if (low > 4 || (low == 4 && (m & 1))) {
    m++;
    if (m >= 1ul << 24) m >>= 1, e++;
  }
  if (e >= 255) return sign | 0x7F800000ul;
  return sign | (uint32_t)e << 23 | (m & 0x7FFFFFul);
}

f32 f_add(f32 a, f32 b) {
  if (!(a & ~SIGN)) return (b & ~SIGN) ? b : (a & b);   /* 0 + b; -0 + -0 = -0 */
  if (!(b & ~SIGN)) return a;
  int ea = fexp(a), eb = fexp(b);
  if (!ea) return b;   /* (no denormals) */
  if (!eb) return a;
  uint32_t ma = (fman(a) | 0x800000ul) << 3, mb = (fman(b) | 0x800000ul) << 3;
  if (ea < eb || (ea == eb && ma < mb)) {
    f32 t = a;
    a = b, b = t;
    uint32_t u = ma;
    ma = mb, mb = u;
    int v = ea;
    ea = eb, eb = v;
  }
  int d = ea - eb;
  if (d > 27) mb = 1;   /* only the sticky bit */
  else if (d) {
    uint32_t lost = mb & ((1ul << d) - 1);
    mb = mb >> d | (lost != 0);
  }
  if ((a ^ b) & SIGN) {
    uint32_t m = ma - mb;
    if (!m) return 0;
    return pack(a & SIGN, ea, m);
  }
  return pack(a & SIGN, ea, ma + mb);
}
f32 f_sub(f32 a, f32 b) { return f_add(a, b ^ SIGN); }

f32 f_mul(f32 a, f32 b) {
  uint32_t sign = (a ^ b) & SIGN;
  int ea = fexp(a), eb = fexp(b);
  if (!ea || !eb) return sign;
  uint32_t ma = fman(a) | 0x800000ul, mb = fman(b) | 0x800000ul;
  /* the 48-bit product from 12-bit halves */
  uint32_t ah = ma >> 12, al = ma & 0xFFF, bh = mb >> 12, bl = mb & 0xFFF;
  uint32_t hh = ah * bh, hl = ah * bl + al * bh, ll = al * bl;
  /* product = hh << 24 + hl << 12 + ll */
  uint32_t lo = ll + ((hl & 0xFFF) << 12);
  uint32_t hi = hh + (hl >> 12) + (lo >> 24);
  lo &= 0xFFFFFF;
  /* the product's top bit is bit 47 or 46 of hi:lo (hi bits 23 or 22); keep 27 bits with sticky */
  int e = ea + eb - 127;
  uint32_t m;
  if (hi & 0x800000ul) {   /* 48 bits: shift down 21 */
    m = hi << 3 | lo >> 21;
    m |= (lo & 0x1FFFFF) != 0;
    e++;
  } else {
    m = hi << 4 | lo >> 20;
    m |= (lo & 0xFFFFF) != 0;
  }
  return pack(sign, e, m);
}

f32 f_div(f32 a, f32 b) {
  uint32_t sign = (a ^ b) & SIGN;
  int ea = fexp(a), eb = fexp(b);
  if (!ea) return sign;
  if (!eb) return sign | 0x7F800000ul;
  uint32_t ma = fman(a) | 0x800000ul, mb = fman(b) | 0x800000ul;
  int e = ea - eb + 127;
  if (ma < mb) ma <<= 1, e--;
  /* 27 quotient bits, a bit at a time, the rest as sticky */
  uint32_t q = 0, r = ma;
  for (int i = 0; i < 27; i++) {
    q <<= 1;
    if (r >= mb) r -= mb, q |= 1;
    r <<= 1;
  }
  q |= r != 0;
  return pack(sign, e, q);
}

/* comparisons: the bits in order (zeros equal) */
static inline int32_t key(f32 a) { return (a & SIGN) ? -(int32_t)(a & ~SIGN) : (int32_t)a; }
bool f_lt(f32 a, f32 b) { return key(a) < key(b); }
bool f_le(f32 a, f32 b) { return key(a) <= key(b); }

f32 f_int(int v) {   /* exact below 2^24 */
  if (!v) return 0;
  uint32_t sign = v < 0 ? SIGN : 0;
  uint32_t m = (uint32_t)(v < 0 ? -v : v);
  return pack(sign, 150 + 3 - 3, m << 3);
}
int f_trunc(f32 a) {   /* toward 0 */
  int e = fexp(a) - 127;
  if (e < 0) return 0;
  uint32_t m = fman(a) | 0x800000ul;
  int v = e >= 23 ? (int)(m << (e - 23)) : (int)(m >> (23 - e));
  return (a & SIGN) ? -v : v;
}
int f_floor(f32 a) {
  int t = f_trunc(a);
  if ((a & SIGN) && f_lt(a, f_int(t))) t--;
  return t;
}
int f_round(f32 a) {   /* Math.Round: to even */
  int e = fexp(a) - 127;
  if (e < -1) return 0;
  uint32_t m = fman(a) | 0x800000ul;   /* value = m * 2^(e - 23) */
  int v;
  if (e >= 23) v = (int)(m << (e - 23));
  else {
    int sh = 23 - e;
    uint32_t q = m >> sh, rem = m & ((1ul << sh) - 1), half = 1ul << (sh - 1);
    if (rem > half || (rem == half && (q & 1))) q++;
    v = (int)q;
  }
  return (a & SIGN) ? -v : v;
}
f32 f_approach(f32 v, f32 t, f32 d) {   /* Calc.Approach */
  if (f_le(v, t)) {
    f32 s = f_add(v, d);
    return f_lt(s, t) ? s : t;
  }
  f32 s = f_sub(v, d);
  return f_lt(t, s) ? s : t;
}
f32 f_min(f32 a, f32 b) { return f_lt(b, a) ? b : a; }
f32 f_max(f32 a, f32 b) { return f_lt(a, b) ? b : a; }
f32 f_abs(f32 a) { return a & ~SIGN; }
int f_sign(f32 a) { return (a & ~SIGN) ? ((a & SIGN) ? -1 : 1) : 0; }
f32 f_lerp(f32 a, f32 b, f32 t) { return f_add(a, f_mul(f_sub(b, a), t)); }
