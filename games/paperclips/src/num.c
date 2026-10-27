/* num.h: JavaScript's arithmetic and number printing, the same on every machine.
 *
 * exp, log and sin reduce their argument with a split constant (Cody and Waite) and sum their series with plain
 * double steps; pow multiplies for whole powers and goes through exp and log otherwise. Nothing comes from the C
 * library, so the calculator and every computer get the same bits. Printing follows ECMAScript: the shortest digits
 * that read back as the same double (Steele and White's free-format method, as in Burger and Dybvig's paper),
 * toFixed from the exact value, with a small big-number type for both. */
#include "num.h"
#include <string.h>

typedef union {
  double d;
  uint64_t u;
} bits_t;

static double from_bits(uint64_t u) {
  bits_t b;
  b.u = u;
  return b.d;
}
static uint64_t to_bits(double d) {
  bits_t b;
  b.d = d;
  return b.u;
}

#define NAN_D from_bits(0x7FF8000000000000ull)
#define INF_D from_bits(0x7FF0000000000000ull)

bool js_finite(double x) { return (to_bits(x) & 0x7FF0000000000000ull) != 0x7FF0000000000000ull; }

double js_floor(double x) {
  if (!(x > -4503599627370496.0 && x < 4503599627370496.0)) return x;   /* NaN, infinities, already whole */
  double t = (double)(int64_t)x;
  return t > x ? t - 1 : t;
}
double js_ceil(double x) {
  if (!(x > -4503599627370496.0 && x < 4503599627370496.0)) return x;
  double t = (double)(int64_t)x;
  return t < x ? t + 1 : t;
}
double js_round(double x) {
  double f = js_floor(x);
  return x - f >= 0.5 ? f + 1 : f;
}
static double nearest(double x) {   /* to the nearest whole number (for range reduction only) */
  return js_floor(x + 0.5);
}
static double fabs_d(double x) { return x < 0 ? -x : x; }

/* 2^k for -1074 <= k <= 1023 */
static double two_to(int k) {
  if (k >= -1022) return from_bits((uint64_t)(k + 1023) << 52);
  return from_bits(1ull << (k + 1074));
}

static const double LN2_HI = 6.93147180369123816490e-01, LN2_LO = 1.90821492927058770002e-10,
                    INV_LN2 = 1.44269504088896338700e+00;

double js_exp(double x) {
  if (x != x) return x;
  if (x > 709.782712893384) return INF_D;
  if (x < -745.1332191019412) return 0;
  double kf = nearest(x * INV_LN2);
  double r = (x - kf * LN2_HI) - kf * LN2_LO;
  double p = 1.0 / 6227020800.0;
  p = 1.0 / 479001600.0 + r * p;
  p = 1.0 / 39916800.0 + r * p;
  p = 1.0 / 3628800.0 + r * p;
  p = 1.0 / 362880.0 + r * p;
  p = 1.0 / 40320.0 + r * p;
  p = 1.0 / 5040.0 + r * p;
  p = 1.0 / 720.0 + r * p;
  p = 1.0 / 120.0 + r * p;
  p = 1.0 / 24.0 + r * p;
  p = 1.0 / 6.0 + r * p;
  p = 0.5 + r * p;
  p = 1.0 + r * p;
  p = 1.0 + r * p;
  int k = (int)kf;
  if (k > 1023) return p * two_to(1023) * two_to(k - 1023);
  if (k < -1022) return p * two_to(k + 600) * two_to(-600);
  return p * two_to(k);
}

double js_log(double x) {
  if (x != x || x < 0) return NAN_D;
  if (x == 0) return -INF_D;
  if (!js_finite(x)) return x;
  int k = 0;
  uint64_t u = to_bits(x);
  if (!(u >> 52)) {   /* subnormal */
    x *= 18014398509481984.0;   /* 2^54 */
    k = -54;
    u = to_bits(x);
  }
  k += (int)(u >> 52) - 1023;
  u = (u & 0x000FFFFFFFFFFFFFull) | 0x3FF0000000000000ull;   /* m in [1, 2) */
  if (u > 0x3FF6A09E667F3BCDull) u -= 1ull << 52, k++;      /* above sqrt(2): m / 2 */
  double f = from_bits(u) - 1;
  double s = f / (2 + f), z = s * s;
  double R = 2.0 / 25.0;
  R = 2.0 / 23.0 + z * R;
  R = 2.0 / 21.0 + z * R;
  R = 2.0 / 19.0 + z * R;
  R = 2.0 / 17.0 + z * R;
  R = 2.0 / 15.0 + z * R;
  R = 2.0 / 13.0 + z * R;
  R = 2.0 / 11.0 + z * R;
  R = 2.0 / 9.0 + z * R;
  R = 2.0 / 7.0 + z * R;
  R = 2.0 / 5.0 + z * R;
  R = 2.0 / 3.0 + z * R;
  R = z * R;
  double hfsq = 0.5 * f * f, dk = k;
  return dk * LN2_HI - ((hfsq - (s * (hfsq + R) + dk * LN2_LO)) - f);
}

double js_log10(double x) {
  if (x >= 1 && x <= 1e22) {   /* the powers of ten come out whole, as in JavaScript */
    double p = 1;
    for (int i = 0; i <= 22; i++, p *= 10)
      if (x == p) return i;
  }
  return js_log(x) * 0.43429448190325182765;
}

double js_pow(double x, double y) {
  if (y == 0) return 1;
  if (x != x || y != y) return NAN_D;
  if (x == 1) return 1;
  if (y == 1) return x;
  bool whole = y == js_floor(y);
  if (x == 0) return y > 0 ? 0 : INF_D;
  if (x < 0) {
    if (!whole) return NAN_D;
    double r = js_pow(-x, y), h = y * 0.5;
    return h == js_floor(h) ? r : -r;
  }
  if (whole && fabs_d(y) <= 64) {
    unsigned n = (unsigned)fabs_d(y);
    double r = 1, b = x;
    while (n) {
      if (n & 1) r *= b;
      n >>= 1;
      if (n) b *= b;
    }
    return y < 0 ? 1 / r : r;
  }
  return js_exp(y * js_log(x));
}

/* sin and cos on [-pi/4, pi/4] */
static double ksin(double r) {
  double z = r * r, p = -1.0 / 121645100408832000.0;
  p = 1.0 / 355687428096000.0 + z * p;
  p = -1.0 / 1307674368000.0 + z * p;
  p = 1.0 / 6227020800.0 + z * p;
  p = -1.0 / 39916800.0 + z * p;
  p = 1.0 / 362880.0 + z * p;
  p = -1.0 / 5040.0 + z * p;
  p = 1.0 / 120.0 + z * p;
  p = -1.0 / 6.0 + z * p;
  return r + r * z * p;
}
static double kcos(double r) {
  double z = r * r, p = 1.0 / 6402373705728000.0;
  p = -1.0 / 20922789888000.0 + z * p;
  p = 1.0 / 87178291200.0 + z * p;
  p = -1.0 / 479001600.0 + z * p;
  p = 1.0 / 3628800.0 + z * p;
  p = -1.0 / 40320.0 + z * p;
  p = 1.0 / 720.0 + z * p;
  p = -1.0 / 24.0 + z * p;
  p = 0.5 + z * p;
  return 1.0 - z * p;
}

double js_sin(double x) {
  if (!js_finite(x)) return NAN_D;
  if (fabs_d(x) <= 0.78539816339744830962) return ksin(x);
  static const double INV_PIO2 = 6.36619772367581382433e-01, PIO2_1 = 1.57079632673412561417e+00,
                      PIO2_2 = 6.07710050630396597660e-11, PIO2_2T = 2.02226624879595063154e-21;
  double n = nearest(x * INV_PIO2);
  double t = x - n * PIO2_1, w = n * PIO2_2, r = t - w;
  w = n * PIO2_2T - ((t - r) - w);
  r = r - w;
  int q = (int)((int64_t)n & 3);
  switch (q) {
    case 0: return ksin(r);
    case 1: return kcos(r);
    case 2: return -ksin(r);
    default: return -kcos(r);
  }
}

/* ---------------------------------------------------------------- big numbers, for printing */
#define BIG_W 40
typedef struct {
  int n;
  uint32_t w[BIG_W];
} big_t;

static void big_set(big_t *b, uint64_t v) {
  b->n = 0;
  while (v) b->w[b->n++] = (uint32_t)v, v >>= 32;
}
static void big_mul(big_t *b, uint32_t m) {
  uint64_t c = 0;
  for (int i = 0; i < b->n; i++) {
    c += (uint64_t)b->w[i] * m;
    b->w[i] = (uint32_t)c;
    c >>= 32;
  }
  if (c && b->n < BIG_W) b->w[b->n++] = (uint32_t)c;
}
static void big_pow10(big_t *b, int k) {
  for (; k >= 9; k -= 9) big_mul(b, 1000000000u);
  static const uint32_t P[9] = {1, 10, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000};
  if (k > 0) big_mul(b, P[k]);
}
static void big_shl(big_t *b, int s) {
  if (!b->n) return;
  int words = s / 32, bitsh = s % 32;
  int n = b->n + words + 1;
  if (n > BIG_W) n = BIG_W;
  for (int i = n - 1; i >= 0; i--) {
    int j = i - words;
    uint32_t hi = j >= 0 && j < b->n ? b->w[j] : 0, lo = j - 1 >= 0 && j - 1 < b->n ? b->w[j - 1] : 0;
    b->w[i] = bitsh ? hi << bitsh | lo >> (32 - bitsh) : hi;
  }
  b->n = n;
  while (b->n && !b->w[b->n - 1]) b->n--;
}
static void big_shr(big_t *b, int s) {
  int words = s / 32, bitsh = s % 32;
  int n = b->n - words;
  if (n <= 0) {
    b->n = 0;
    return;
  }
  for (int i = 0; i < n; i++) {
    uint32_t lo = b->w[i + words], hi = i + words + 1 < b->n ? b->w[i + words + 1] : 0;
    b->w[i] = bitsh ? lo >> bitsh | hi << (32 - bitsh) : lo;
  }
  b->n = n;
  while (b->n && !b->w[b->n - 1]) b->n--;
}
static void big_add(big_t *a, const big_t *b) {
  uint64_t c = 0;
  int n = a->n > b->n ? a->n : b->n;
  for (int i = 0; i < n; i++) {
    c += (uint64_t)(i < a->n ? a->w[i] : 0) + (i < b->n ? b->w[i] : 0);
    a->w[i] = (uint32_t)c;
    c >>= 32;
  }
  a->n = n;
  if (c && a->n < BIG_W) a->w[a->n++] = (uint32_t)c;
}
static int big_cmp(const big_t *a, const big_t *b) {
  if (a->n != b->n) return a->n < b->n ? -1 : 1;
  for (int i = a->n - 1; i >= 0; i--)
    if (a->w[i] != b->w[i]) return a->w[i] < b->w[i] ? -1 : 1;
  return 0;
}
static void big_sub(big_t *a, const big_t *b) {   /* a >= b */
  int64_t c = 0;
  for (int i = 0; i < a->n; i++) {
    c += (int64_t)a->w[i] - (i < b->n ? b->w[i] : 0);
    a->w[i] = (uint32_t)c;
    c >>= 32;
  }
  while (a->n && !a->w[a->n - 1]) a->n--;
}
static uint32_t big_div(big_t *b, uint32_t d) {
  uint64_t r = 0;
  for (int i = b->n - 1; i >= 0; i--) {
    r = r << 32 | b->w[i];
    b->w[i] = (uint32_t)(r / d);
    r %= d;
  }
  while (b->n && !b->w[b->n - 1]) b->n--;
  return (uint32_t)r;
}

/* the decimal digits of a big number, into d (most significant first); returns how many */
static int big_digits(big_t *b, char *d, int room) {
  char t[400];
  int n = 0;
  while (b->n && n + 9 < (int)sizeof t) {
    uint32_t c = big_div(b, 1000000000u);
    for (int i = 0; i < 9; i++) t[n++] = (char)('0' + c % 10), c /= 10;
  }
  while (n > 1 && t[n - 1] == '0') n--;
  if (!n) t[n++] = '0';
  if (n > room) n = room;
  for (int i = 0; i < n; i++) d[i] = t[n - 1 - i];
  return n;
}

/* v = f * 2^e exactly (v > 0, finite) */
static void decompose(double v, uint64_t *f, int *e, bool *unequal) {
  uint64_t u = to_bits(v);
  int ex = (int)(u >> 52 & 0x7FF);
  *f = u & 0x000FFFFFFFFFFFFFull;
  if (ex) *f |= 1ull << 52, *e = ex - 1075;
  else *e = -1074;
  *unequal = ex > 1 && *f == 1ull << 52;
}

/* the shortest digits that read back as v (v > 0, finite): v = 0.d1d2...dn x 10^k; returns n */
static int shortest(double v, char *dig, int *kout) {
  uint64_t f;
  int e;
  bool unequal;
  decompose(v, &f, &e, &unequal);
  bool even = !(f & 1);
  big_t r, s, mp, mm, t;
  if (e >= 0) {
    big_set(&r, f), big_shl(&r, e + (unequal ? 2 : 1));
    big_set(&s, unequal ? 4 : 2);
    big_set(&mp, 1), big_shl(&mp, e + (unequal ? 1 : 0));
    big_set(&mm, 1), big_shl(&mm, e);
  } else {
    big_set(&r, f), big_shl(&r, unequal ? 2 : 1);
    big_set(&s, 1), big_shl(&s, (unequal ? 2 : 1) - e);
    big_set(&mp, unequal ? 2 : 1);
    big_set(&mm, 1);
  }
  int k = (int)js_ceil(js_log10(v) - 1e-10);
  if (k >= 0) big_pow10(&s, k);
  else big_pow10(&r, -k), big_pow10(&mp, -k), big_pow10(&mm, -k);
  for (;;) {   /* k too small */
    t = r, big_add(&t, &mp);
    int c = big_cmp(&t, &s);
    if (even ? c >= 0 : c > 0) big_mul(&s, 10), k++;
    else break;
  }
  for (;;) {   /* k too big */
    t = r, big_add(&t, &mp), big_mul(&t, 10);
    int c = big_cmp(&t, &s);
    if (even ? c < 0 : c <= 0) big_mul(&r, 10), big_mul(&mp, 10), big_mul(&mm, 10), k--;
    else break;
  }
  int n = 0;
  for (;;) {
    big_mul(&r, 10), big_mul(&mp, 10), big_mul(&mm, 10);
    int d = 0;
    while (big_cmp(&r, &s) >= 0) big_sub(&r, &s), d++;
    int c1 = big_cmp(&r, &mm);
    bool low = even ? c1 <= 0 : c1 < 0;
    t = r, big_add(&t, &mp);
    int c2 = big_cmp(&t, &s);
    bool high = even ? c2 >= 0 : c2 > 0;
    if (!low && !high && n < 25) {
      dig[n++] = (char)('0' + d);
      continue;
    }
    if (low && high) {
      t = r, big_mul(&t, 2);
      int c = big_cmp(&t, &s);
      if (c > 0 || (c == 0 && (d & 1))) d++;
    } else if (high) {
      d++;
    }
    dig[n++] = (char)('0' + d);
    break;
  }
  *kout = k;
  return n;
}

/* ---------------------------------------------------------------- printing */
typedef struct {
  char *p;
  int left;
} out_t;
static void put(out_t *o, char c) {
  if (o->left > 1) *o->p++ = c, o->left--;
  *o->p = 0;
}
static void puts_(out_t *o, const char *s) {
  while (*s) put(o, *s++);
}

char *js_tostring(double x, char *out, int n) {
  out_t o = {out, n};
  *out = 0;
  if (x != x) return puts_(&o, "NaN"), out;
  if (x == 0) return puts_(&o, "0"), out;
  if (x < 0) put(&o, '-'), x = -x;
  if (!js_finite(x)) return puts_(&o, "Infinity"), out;
  char d[32];
  int k, len = shortest(x, d, &k);
  if (len <= k && k <= 21) {
    for (int i = 0; i < len; i++) put(&o, d[i]);
    for (int i = len; i < k; i++) put(&o, '0');
  } else if (0 < k && k <= 21) {
    for (int i = 0; i < len; i++) {
      if (i == k) put(&o, '.');
      put(&o, d[i]);
    }
  } else if (-6 < k && k <= 0) {
    put(&o, '0'), put(&o, '.');
    for (int i = 0; i < -k; i++) put(&o, '0');
    for (int i = 0; i < len; i++) put(&o, d[i]);
  } else {
    put(&o, d[0]);
    if (len > 1) {
      put(&o, '.');
      for (int i = 1; i < len; i++) put(&o, d[i]);
    }
    put(&o, 'e');
    int ex = k - 1;
    put(&o, ex < 0 ? '-' : '+');
    if (ex < 0) ex = -ex;
    char t[8];
    int m = 0;
    do t[m++] = (char)('0' + ex % 10); while ((ex /= 10));
    while (m) put(&o, t[--m]);
  }
  return out;
}

char *js_tofixed(double x, int f, char *out, int n) {
  out_t o = {out, n};
  *out = 0;
  if (x != x) return puts_(&o, "NaN"), out;
  if (x >= 1e21 || x <= -1e21) return js_tostring(x, out, n);
  if (x < 0) put(&o, '-'), x = -x;
  big_t b;
  uint64_t m;
  int e;
  bool unequal;
  char d[400];
  int len;
  if (x == 0) {
    len = 1, d[0] = '0';
  } else {
    decompose(x, &m, &e, &unequal);
    big_set(&b, m);
    big_pow10(&b, f);
    if (e >= 0) {
      big_shl(&b, e);
    } else {
      big_t half;
      big_set(&half, 1), big_shl(&half, -e - 1);
      big_add(&b, &half);
      big_shr(&b, -e);
    }
    len = big_digits(&b, d, (int)sizeof d);
  }
  int whole = len - f;   /* digits before the point */
  if (whole <= 0) {
    put(&o, '0');
  } else {
    for (int i = 0; i < whole; i++) put(&o, d[i]);
  }
  if (f > 0) {
    put(&o, '.');
    for (int i = whole; i < len; i++)
      if (i < 0) put(&o, '0');
      else put(&o, d[i]);
  }
  return out;
}

/* commas between thousands in every run of digits (the game's /(\d)(?=(\d\d\d)+(?!\d))/g) */
static void commas_into(out_t *o, const char *s) {
  while (*s) {
    if (*s < '0' || *s > '9') {
      put(o, *s++);
      continue;
    }
    int run = 0;
    while (s[run] >= '0' && s[run] <= '9') run++;
    for (int i = 0; i < run; i++) {
      put(o, s[i]);
      if (i < run - 1 && (run - 1 - i) % 3 == 0) put(o, ',');
    }
    s += run;
  }
}

/* "1.5e+21" -> "15" and 20 zeros; "1e+21" stays "1e+21" and gets 21 zeros (as the game's code does) */
static void expand(const char *s, char *b, int n) {
  out_t o = {b, n};
  *b = 0;
  const char *e = strstr(s, "e+");
  if (!e) {
    puts_(&o, s);
    return;
  }
  int ex = 0;
  for (const char *p = e + 2; *p; p++) ex = ex * 10 + (*p - '0');
  const char *dot = strchr(s, '.');
  if (dot && dot < e) {
    for (const char *p = s; p < e; p++)
      if (*p != '.') put(&o, *p);
    ex -= (int)(e - dot - 1);
  } else {
    puts_(&o, s);
  }
  for (int i = 0; i < ex; i++) put(&o, '0');
}

char *js_commas(double num, int decimal, char *out, int n) {
  char s[64], base[400];
  js_tostring(num, s, sizeof s);
  expand(s, base, sizeof base);
  out_t o = {out, n};
  *out = 0;
  const char *dot = strchr(base, '.');
  if (decimal == 0 && strlen(base) <= 3 && !dot) return puts_(&o, base), out;
  char left[400];
  int ln = dot ? (int)(dot - base) : (int)strlen(base);
  memcpy(left, base, (size_t)ln), left[ln] = 0;
  if (num <= 999) puts_(&o, left);
  else commas_into(&o, left);
  if (decimal == 0) return out;
  put(&o, '.');
  int got = 0;
  if (dot)
    for (const char *p = dot + 1; *p && got < decimal; p++, got++) put(&o, *p);
  for (; got < decimal; got++) put(&o, '0');
  return out;
}

static const char *const PLACE[] = {"", "thousand", "million", "billion", "trillion", "quadrillion", "quintillion",
  "sextillion", "septillion", "octillion", "nonillion", "decillion", "undecillion", "duodecillion", "tredecillion",
  "quattuordecillion", "quindecillion", "sexdecillion", "septendecillion", "octodecillion", "novemdecillion",
  "vigintillion", "unvigintillion", "duovigintillion", "trevigintillion", "quattuorvigintillion", "quinvigintillion",
  "sexvigintillion", "septenvigintillion", "octovigintillion", "novemvigintillion", "trigintillion",
  "untrigintillion", "duotrigintillion", "tretrigintillion", "quattuortrigintillion", "quintrigintillion",
  "sextrigintillion", "septentrigintillion", "octotrigintillion", "novemtrigintillion", "quadragintillion",
  "unquadragintillion", "duoquadragintillion", "trequadragintillion", "quattuorquadragintillion",
  "quinquadragintillion", "sexquadragintillion", "septenquadragintillion", "octoquadragintillion",
  "novemquadragintillion", "quinquagintillion", "unquinquagintillion", "duoquinquagintillion",
  "trequinquagintillion", "quattuorquinquagintillion", "quinquinquagintillion", "sexquinquagintillion",
  "septenquinquagintillion", "octoquinquagintillion", "novemquinquagintillion", "sexagintillion",
  "unsexagintillion", "duosexagintillion", "tresexagintillion", "quattuorsexagintillion", "quinsexagintillion",
  "sexsexagintillion", "septsexagintillion", "octosexagintillion", "octosexagintillion", "septuagintillion",
  "unseptuagintillion", "duoseptuagintillion", "treseptuagintillion", "quinseptuagintillion", "sexseptuagintillion",
  "septseptuagintillion", "octoseptuagintillion", "novemseptuagintillion", "octogintillion", "unoctogintillion",
  "duooctogintillion", "treoctogintillion", "quattuoroctogintillion", "quinoctogintillion", "sexoctogintillion",
  "septoctogintillion", "octooctogintillion", "novemoctogintillion", "nonagintillion", "unnonagintillion",
  "duononagintillion", "trenonagintillion", "quattuornonagintillion", "quinnonagintillion", "sexnonagintillion",
  "septnonagintillion", "octononagintillion", "novemnonagintillion", "centillion"};

char *js_spellf(double x, char *out, int n) {
  out_t o = {out, n};
  *out = 0;
  if (x < 0) return js_tostring(x, out, n);
  char s[64], w[400];
  js_tostring(x, s, sizeof s);
  expand(s, w, sizeof w);
  if (!strstr(s, "e+")) {
    char *dot = strchr(w, '.');
    if (dot) *dot = 0;
  } else if (strstr(w, "e+")) {   /* "1e+21": its mantissa, then the zeros */
    char t[400];
    out_t q = {t, sizeof t};
    *t = 0;
    int ex = 0;
    const char *e = strstr(s, "e+");
    for (const char *p = e + 2; *p; p++) ex = ex * 10 + (*p - '0');
    for (const char *p = s; p < e; p++) put(&q, *p);
    for (int i = 0; i < ex; i++) put(&q, '0');
    strcpy(w, t);
  }
  int len = (int)strlen(w);
  if (len == 1 && w[0] == '0') return puts_(&o, "0"), out;
  if (len >= 303) return out;
  int lead = len % 3 ? len % 3 : 3, place = (len - 1) / 3;
  for (int i = 0; i < lead; i++) put(&o, w[i]);
  put(&o, '.');
  put(&o, lead < len ? w[lead] : '0');
  if (place) put(&o, ' '), puts_(&o, PLACE[place]);
  return out;
}

char *js_cruncher(double number, int decimals, char *out, int n) {
  static const struct {
    double over, by;
    const char *name;
  } STEP[] = {
    {999999999999999999999999999999999999999999999999999.0, 1e51, "sexdecillion"},
    {999999999999999999999999999999999999999999999999.0, 1e48, "quindecillion"},
    {999999999999999999999999999999999999999999999.0, 1e45, "quattuordecillion"},
    {999999999999999999999999999999999999999999.0, 1e42, "tredecillion"},
    {999999999999999999999999999999999999999.0, 1e39, "duodecillion"},
    {999999999999999999999999999999999999.0, 1e36, "undecillion"},
    {999999999999999999999999999999999.0, 1e33, "decillion"},
    {999999999999999999999999999999.0, 1e30, "nonillion"},
    {999999999999999999999999999.0, 1e27, "octillion"},
    {999999999999999999999999.0, 1e24, "septillion"},
    {999999999999999999999.0, 1e21, "sextillion"},
    {999999999999999999.0, 1e18, "quintillion"},
    {999999999999999.0, 1e15, "quadrillion"},
    {999999999999.0, 1e12, "trillion"},
    {999999999.0, 1e9, "billion"},
    {999999.0, 1e6, "million"},
    {999.0, 1e3, "thousand"},
  };
  const char *suffix = "";
  int precision = decimals;
  bool found = false;
  for (unsigned i = 0; i < sizeof STEP / sizeof STEP[0]; i++)
    if (number > STEP[i].over) {
      number = number / STEP[i].by, suffix = STEP[i].name, found = true;
      break;
    }
  if (!found && number < 1000) precision = 0;
  js_tofixed(number, precision, out, n);
  if (*suffix) {
    out_t o = {out + strlen(out), n - (int)strlen(out)};
    put(&o, ' ');
    puts_(&o, suffix);
  }
  return out;
}

char *js_locale(double x, char *out, int n) {
  char s[400];
  js_tofixed(x, 3, s, sizeof s);
  char *dot = strchr(s, '.');
  if (dot) {
    char *end = s + strlen(s);
    while (end > dot + 1 && end[-1] == '0') *--end = 0;
    if (end == dot + 1) *dot = 0;
  }
  out_t o = {out, n};
  *out = 0;
  if (!strcmp(s, "-0")) return puts_(&o, "0"), out;
  dot = strchr(s, '.');
  if (dot) {
    *dot = 0;
    commas_into(&o, s);
    put(&o, '.');
    puts_(&o, dot + 1);
  } else {
    commas_into(&o, s);
  }
  return out;
}

static void count_into(out_t *o, double v, const char *one, const char *many) {
  char t[32];
  js_tostring(v, t, sizeof t);
  puts_(o, t);
  puts_(o, v == 1 ? one : many);
}

char *js_time(double t, char *out, int n) {
  out_t o = {out, n};
  *out = 0;
  double x = t / 100;
  double h = js_floor(x / 3600);
  double r = x - js_floor(x / 3600) * 3600;   /* x % 3600, exact for positive x */
  double m = js_floor(r / 60), s = js_floor(r - js_floor(r / 60) * 60);
  if (h > 0) count_into(&o, h, " hour ", " hours ");
  if (m > 0) count_into(&o, m, " minute ", " minutes ");
  if (s > 0) count_into(&o, s, " second", " seconds");
  return out;
}
