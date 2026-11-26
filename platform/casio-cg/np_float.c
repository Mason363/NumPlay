/* NumPlay on the Casio fx-CG50: float arithmetic in software, in place of libgcc's. The calculator's SuperH has no
 * floating-point unit, and the C version libgcc has for it unpacks each number into a struct and packs it back, two
 * hundred instructions an addition: Hollow Knight spent two thirds of its time there (Mason363/NumPlay#532).
 * These give the same numbers, IEEE single precision rounded to the nearest, in a third to a half of the
 * instructions. Checked against a computer's FPU on every float and int (the conversions) and 400 million pairs
 * (+, -, x, the comparisons), and against libgcc's in the SH simulator on every pair of edge values and 88 million
 * others: the same, but where a product or quotient is subnormal libgcc sometimes rounded twice, and these round
 * once, as the FPU does. */
#include <stdint.h>

typedef union {
  float f;
  uint32_t u;
} fbits;
static inline uint32_t U(float f) { return ((fbits){.f = f}).u; }
static inline float F(uint32_t u) { return ((fbits){.u = u}).f; }

#define SIGN 0x80000000u
#define INF 0x7F800000u
#define QNAN 0x7FC00000u
#define NOINLINE __attribute__((noinline))
#define INLINE static inline __attribute__((always_inline))

/* sign s, biased exponent e (1 to 254), significand m with its leading one at bit 31 and the bits that round below
   bit 8 (the last one set if anything was left out below it): rounded to the nearest, ties to even */
INLINE uint32_t round_pack(uint32_t s, int e, uint32_t m) {
  uint32_t x = s + ((uint32_t)(e - 1) << 23) + (m >> 8);
  return x + ((m & 0xFF) + (x & 1) > 0x80);
}
/* the same for any exponent: too big, infinity; too small, subnormal or 0 */
static NOINLINE uint32_t pack(uint32_t s, int e, uint32_t m) {
  if (e >= 0xFF) return s | INF;
  if (e <= 0) {
    uint32_t sh = (uint32_t)(1 - e);
    m = sh < 32 ? m >> sh | ((m << (32 - sh)) != 0) : (m != 0);
    e = 1;
  }
  return round_pack(s, e, m);
}
INLINE uint32_t pack_fast(uint32_t s, int e, uint32_t m) {
  return (uint32_t)(e - 1) < 0xFE ? round_pack(s, e, m) : pack(s, e, m);
}
/* m (not 0) shifted up to its leading one at bit 31, the exponent e lowered as much */
static NOINLINE uint32_t norm_pack(uint32_t s, int e, uint32_t m) {
  int sh = __builtin_clz(m);
  return pack(s, e - sh, m << sh);
}
static uint32_t nan2(uint32_t a, uint32_t b) { return ((a & ~SIGN) > INF ? a : b) | 0x400000; }

/* ---- addition ---- */
/* a and b, the bigger first, one of them 0, subnormal, infinite or NaN */
static NOINLINE uint32_t add_special(uint32_t a, uint32_t b) {
  int ea = (int)(a >> 23 & 0xFF), eb = (int)(b >> 23 & 0xFF);
  if (ea == 0xFF) { /* (a infinite or NaN, as is b if it is) */
    if ((a & ~SIGN) > INF) return a | 0x400000;
    return eb == 0xFF && (a ^ b) >> 31 ? QNAN : a;
  }
  if (!(b & ~SIGN)) return (a & ~SIGN) ? a : a & b; /* (x + 0; 0 + -0 is 0) */
  /* (b subnormal, a too or not: exact) */
  uint32_t ma = (a & 0x7FFFFF) << 7, mb = (b & 0x7FFFFF) << 7;
  if (ea) ma |= 1u << 30;
  else ea = 1;
  uint32_t d = (uint32_t)(ea - 1);
  if (d) mb = d < 32 ? mb >> d | ((mb << (32 - d)) != 0) : 1;
  uint32_t m = (a ^ b) >> 31 ? ma - mb : ma + mb;
  if (!m) return 0;
  return norm_pack(a & SIGN, ea + 1, m);
}
INLINE uint32_t add(uint32_t a, uint32_t b) {
  if (a << 1 < b << 1) {
    uint32_t t = a;
    a = b, b = t;
  }
  int ea = (int)(a >> 23 & 0xFF), eb = (int)(b >> 23 & 0xFF);
  if ((uint32_t)(ea - 1) >= 0xFE || (uint32_t)(eb - 1) >= 0xFE) return add_special(a, b);
  /* the significands with their leading one at bit 30, b's moved to a's exponent (what is left out: the last bit) */
  uint32_t ma = (a << 8 | SIGN) >> 1, mb = (b << 8 | SIGN) >> 1, d = (uint32_t)(ea - eb);
  if (d) mb = d < 31 ? mb >> d | ((mb << (32 - d)) != 0) : 1;
  uint32_t m;
  if ((int32_t)(a ^ b) < 0) {
    m = ma - mb;
    if (!(m >> 30)) return m ? norm_pack(a & SIGN, ea + 1, m) : 0; /* (cancelled) */
    m <<= 1;
  } else {
    m = ma + mb;
    if (m >> 31) ea++;
    else m <<= 1;
  }
  return pack_fast(a & SIGN, ea, m);
}
float __addsf3(float a, float b) { return F(add(U(a), U(b))); }
float __subsf3(float a, float b) { return __addsf3(a, F(U(b) ^ SIGN)); }

/* ---- multiplication and division ---- */
/* the significand of a subnormal (not 0) moved up to bit 23: its exponent */
static int norm23(uint32_t *m) {
  int sh = __builtin_clz(*m) - 8;
  *m <<= sh;
  return 1 - sh;
}
/* the significands' product, their leading ones at bit 23, the exponents biased */
INLINE uint32_t mul_core(uint32_t s, int ea, uint32_t ma, int eb, uint32_t mb) {
  uint64_t p = (uint64_t)(ma << 8) * (mb << 8); /* (its leading one at bit 63 or 62) */
  uint32_t hi = (uint32_t)(p >> 32), lo = (uint32_t)p;
  int e = ea + eb - 126;
  if (!(hi >> 31)) hi = hi << 1 | lo >> 31, lo <<= 1, e--;
  return pack_fast(s, e, hi | (lo != 0));
}
static NOINLINE uint32_t mul_special(uint32_t a, uint32_t b) {
  uint32_t s = (a ^ b) & SIGN, aa = a & ~SIGN, ab = b & ~SIGN, ma = a & 0x7FFFFF, mb = b & 0x7FFFFF;
  int ea = (int)(aa >> 23), eb = (int)(ab >> 23);
  if (aa > INF || ab > INF) return nan2(a, b);
  if (aa == INF || ab == INF) return aa && ab ? s | INF : QNAN; /* (infinity times 0: NaN) */
  if (!aa || !ab) return s;
  if (ea) ma |= 0x800000;
  else ea = norm23(&ma);
  if (eb) mb |= 0x800000;
  else eb = norm23(&mb);
  return mul_core(s, ea, ma, eb, mb);
}
float __mulsf3(float fa, float fb) {
  uint32_t a = U(fa), b = U(fb);
  int ea = (int)(a >> 23 & 0xFF), eb = (int)(b >> 23 & 0xFF);
  if ((uint32_t)(ea - 1) >= 0xFE || (uint32_t)(eb - 1) >= 0xFE) return F(mul_special(a, b));
  return F(mul_core((a ^ b) & SIGN, ea, (a & 0x7FFFFF) | 0x800000, eb, (b & 0x7FFFFF) | 0x800000));
}

static NOINLINE uint32_t div_core(uint32_t s, int ea, uint32_t ma, int eb, uint32_t mb) {
  /* q = ma 2^26 / mb (27 bits at most, ma / mb being under 2), by the CPU's division steps; exact or not, from
     q mb */
  uint32_t hi = ma >> 6, q = ma << 26;
  __asm__("div0u\n"
          ".rept 32\n"
          "rotcl %1\n"
          "div1 %2, %0\n"
          ".endr\n"
          "rotcl %1"
          : "+r"(hi), "+r"(q)
          : "r"(mb)
          : "t");
  uint64_t back = (uint64_t)q * mb;
  uint32_t inexact = (uint32_t)(back >> 32) != ma >> 6 || (uint32_t)back != ma << 26;
  int e = ea - eb + 127;
  if (q >> 26) return pack_fast(s, e, q << 5 | inexact);
  return pack_fast(s, e - 1, q << 6 | inexact);
}
static NOINLINE uint32_t div_special(uint32_t a, uint32_t b) {
  uint32_t s = (a ^ b) & SIGN, aa = a & ~SIGN, ab = b & ~SIGN, ma = a & 0x7FFFFF, mb = b & 0x7FFFFF;
  int ea = (int)(aa >> 23), eb = (int)(ab >> 23);
  if (aa > INF || ab > INF) return nan2(a, b);
  if (aa == INF) return ab == INF ? QNAN : s | INF;
  if (ab == INF) return s;
  if (!ab) return aa ? s | INF : QNAN; /* (x / 0; 0 / 0: NaN) */
  if (!aa) return s;
  if (ea) ma |= 0x800000;
  else ea = norm23(&ma);
  if (eb) mb |= 0x800000;
  else eb = norm23(&mb);
  return div_core(s, ea, ma, eb, mb);
}
float __divsf3(float fa, float fb) {
  uint32_t a = U(fa), b = U(fb);
  int ea = (int)(a >> 23 & 0xFF), eb = (int)(b >> 23 & 0xFF);
  if ((uint32_t)(ea - 1) >= 0xFE || (uint32_t)(eb - 1) >= 0xFE) return F(div_special(a, b));
  return F(div_core((a ^ b) & SIGN, ea, (a & 0x7FFFFF) | 0x800000, eb, (b & 0x7FFFFF) | 0x800000));
}

/* ---- conversions ---- */
INLINE uint32_t from_u(uint32_t s, uint32_t u) {
  if (!u) return 0;
  int e = 158;
  if (!(u >> 16)) u <<= 16, e -= 16;
  if (!(u >> 24)) u <<= 8, e -= 8;
  if (!(u >> 28)) u <<= 4, e -= 4;
  if (!(u >> 30)) u <<= 2, e -= 2;
  if (!(u >> 31)) u <<= 1, e -= 1;
  return round_pack(s, e, u);
}
float __floatsisf(int32_t i) { return F(i < 0 ? from_u(SIGN, -(uint32_t)i) : from_u(0, (uint32_t)i)); }
float __floatunsisf(uint32_t u) { return F(from_u(0, u)); }

int32_t __fixsfsi(float f) {
  uint32_t a = U(f);
  int e = (int)(a >> 23 & 0xFF);
  if (e < 127) return 0;
  if (e >= 158) /* (too big: the nearest int; NaN, 0) */
    return (a & ~SIGN) > INF ? 0 : a >> 31 ? INT32_MIN : INT32_MAX;
  uint32_t m = (a << 8 | SIGN) >> (158 - e);
  return (int32_t)a < 0 ? -(int32_t)m : (int32_t)m;
}
int64_t __fixsfdi(float f) { /* (libgcc's goes through double: 2 KB of its double arithmetic with it) */
  uint32_t a = U(f);
  int e = (int)(a >> 23 & 0xFF);
  if (e < 127) return 0;
  if (e >= 190) return (a & ~SIGN) > INF ? 0 : a >> 31 ? INT64_MIN : INT64_MAX;
  uint64_t m = (a << 8 | SIGN) >> 8;
  m = e >= 150 ? m << (e - 150) : m >> (150 - e);
  return a >> 31 ? -(int64_t)m : (int64_t)m;
}
uint32_t __fixunssfsi(float f) { /* (libgcc's: from 2^31 up, as unsigned; below, as __fixsfsi) */
  uint32_t a = U(f);
  int e = (int)(a >> 23 & 0xFF);
  if (a >> 31 || e < 158) return (uint32_t)__fixsfsi(f);
  if (e >= 159) return a > INF ? 0 : 0xFFFFFFFFu;
  return a << 8 | SIGN;
}

/* ---- comparisons: the order of a and b (-1, 0, 1), or `unordered` if one is NaN ---- */
INLINE int cmp(float fa, float fb, int unordered) {
  uint32_t a = U(fa), b = U(fb);
  if ((a & ~SIGN) > INF || (b & ~SIGN) > INF) return unordered;
  int32_t x = (int32_t)a < 0 ? -(int32_t)(a & ~SIGN) : (int32_t)a, y = (int32_t)b < 0 ? -(int32_t)(b & ~SIGN) : (int32_t)b;
  return x < y ? -1 : x > y;
}
int __eqsf2(float a, float b) { return cmp(a, b, 1); }
int __nesf2(float a, float b) { return cmp(a, b, 1); }
int __ltsf2(float a, float b) { return cmp(a, b, 1); }
int __lesf2(float a, float b) { return cmp(a, b, 1); }
int __gtsf2(float a, float b) { return cmp(a, b, -1); }
int __gesf2(float a, float b) { return cmp(a, b, -1); }
int __unordsf2(float a, float b) { return (U(a) & ~SIGN) > INF || (U(b) & ~SIGN) > INF; }

/* ---- the count of leading and trailing zero bits, with a table of 16 bytes for libgcc's 256 ---- */
int __clzsi2(uint32_t x) {
  static const uint8_t top[16] = {4, 3, 2, 2, 1, 1, 1, 1};
  int n = 0;
  if (!(x >> 16)) n += 16, x <<= 16;
  if (!(x >> 24)) n += 8, x <<= 8;
  if (!(x >> 28)) n += 4, x <<= 4;
  return n + top[x >> 28];
}
int __ctzsi2(uint32_t x) { return 31 - __clzsi2(x & -x); }

/* ---- an angle less the nearest multiple of pi/2 (y[0] + y[1]), and which multiple: sinf, cosf and tanf's first
 * step. As newlib's, to the bit, up to 2^7 pi/2 (201); past that (never an angle a game keeps) less precise, instead
 * of newlib's 3 KB of 2/pi's digits. ---- */
static const int32_t npio2_hw[32] = { /* (n pi/2, its float's top 24 bits) */
  0x3FC90F00, 0x40490F00, 0x4096CB00, 0x40C90F00, 0x40FB5300, 0x4116CB00, 0x412FED00, 0x41490F00,
  0x41623100, 0x417B5300, 0x418A3A00, 0x4196CB00, 0x41A35C00, 0x41AFED00, 0x41BC7E00, 0x41C90F00,
  0x41D5A000, 0x41E23100, 0x41EEC200, 0x41FB5300, 0x4203F200, 0x420A3A00, 0x42108300, 0x4216CB00,
  0x421D1400, 0x42235C00, 0x4229A500, 0x422FED00, 0x42363600, 0x423C7E00, 0x4242C700, 0x42490F00};
int32_t __ieee754_rem_pio2f(float x, float *y) {
  /* pi/2 in three parts of 17 bits (their products by n exact) and what each leaves */
  const float invpio2 = F(0x3F22F984), pio2_1 = F(0x3FC90F80), pio2_1t = F(0x37354443), pio2_2 = F(0x37354400),
              pio2_2t = F(0x2E85A308), pio2_3 = F(0x2E85A300), pio2_3t = F(0x248D3132);
  int32_t hx = (int32_t)U(x), ix = hx & 0x7FFFFFFF;
  float z, w, t, r, fn;
  if (ix <= 0x3F490FD8) { /* |x| up to pi/4 */
    y[0] = x, y[1] = 0;
    return 0;
  }
  if (ix < 0x4016CBE4) { /* |x| up to 3 pi/4: n is 1 or -1 */
    int near = (ix & 0xFFFFFFF0) == 0x3FC90FD0; /* (near pi/2: 72 bits of it) */
    float p = near ? pio2_2t : pio2_1t;
    if (hx > 0) {
      z = x - pio2_1;
      if (near) z -= pio2_2;
      y[0] = z - p, y[1] = (z - y[0]) - p;
      return 1;
    }
    z = x + pio2_1;
    if (near) z += pio2_2;
    y[0] = z + p, y[1] = (z - y[0]) + p;
    return -1;
  }
  if (ix >= 0x7F800000) { /* (infinite, NaN) */
    y[0] = y[1] = x - x;
    return 0;
  }
  t = F((uint32_t)ix);
  int32_t n = (int32_t)(t * invpio2 + 0.5f);
  fn = (float)n;
  r = t - fn * pio2_1;
  w = fn * pio2_1t;
  if (n < 32 && (ix & 0xFFFFFF00) != npio2_hw[n - 1]) /* (no cancellation) */
    y[0] = r - w;
  else {
    int32_t j = ix >> 23;
    y[0] = r - w;
    if (j - (int32_t)(U(y[0]) >> 23 & 0xFF) > 8 || ix > 0x43490F80) { /* (a second step, and a third) */
      t = r, w = fn * pio2_2, r = t - w, w = fn * pio2_2t - ((t - r) - w), y[0] = r - w;
      if (j - (int32_t)(U(y[0]) >> 23 & 0xFF) > 25 || ix > 0x43490F80)
        t = r, w = fn * pio2_3, r = t - w, w = fn * pio2_3t - ((t - r) - w), y[0] = r - w;
    }
  }
  y[1] = (r - y[0]) - w;
  if (hx < 0) {
    y[0] = -y[0], y[1] = -y[1];
    return -n;
  }
  return n;
}
