/* NumTycoon: build a business empire, one lemonade stand at a time.
 *
 * Ten businesses grow into a living pixel city: sunrise and sunset, rain,
 * cars, walkers, smoking chimneys, a rocket that takes off with every
 * Space Port sale. Buy them, run them (EXE), hire managers, buy upgrades,
 * catch the golden truck (Shift), then Franchise for Stars that make every
 * next empire richer. 20 achievements, a Star shop, saved in the background.
 *
 * Drawing: the city is composed 12 rows at a time in a small buffer and
 * pushed as a band (30 frames a second); the interface below it uses dirty
 * rows: a row is composed in RAM and pushed only when it changes, and a
 * progress bar only repaints the pixels that moved. Text uses the
 * calculator's own font. Money is a double, shown as 1.23K, 4.5M, 6.7B... */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "NumTycoon";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif

typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
#define BLACK 0

/* what the keys do */
enum {
  K_UP = 1, K_DOWN = 2, K_LEFT = 4, K_RIGHT = 8, K_OK = 16, K_RUN = 32, K_QTY = 64, K_BACK = 128, K_HOME = 256,
  K_CATCH = 512, K_T1 = 1024, K_T2 = 2048, K_T3 = 4096, K_T4 = 8192, K_T5 = 16384, K_ANY = 32768
};
static const uint16_t keymap[][2] = {
  {eadk_key_up, K_UP}, {eadk_key_down, K_DOWN}, {eadk_key_left, K_LEFT}, {eadk_key_right, K_RIGHT},
  {eadk_key_ok, K_OK}, {eadk_key_exe, K_RUN}, {eadk_key_six, K_RUN}, {eadk_key_backspace, K_QTY},
  {eadk_key_alpha, K_QTY}, {eadk_key_back, K_BACK}, {eadk_key_home, K_HOME}, {eadk_key_on_off, K_HOME},
  {eadk_key_shift, K_CATCH}, {eadk_key_zero, K_CATCH}, {eadk_key_one, K_T1}, {eadk_key_two, K_T2},
  {eadk_key_three, K_T3}, {eadk_key_four, K_T4}, {eadk_key_five, K_T5},
};
static int keys(void) {
  uint64_t k = eadk_keyboard_scan();
  int a = k ? K_ANY : 0;
  for (unsigned i = 0; i < sizeof keymap / sizeof keymap[0]; i++)
    if (k >> keymap[i][0] & 1) a |= keymap[i][1];
  return a;
}

static uint32_t now, seed = 0x9E3779B9u;
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static int rr(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static int isq(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}
static double dsqrt(double x) {
  if (x <= 0) return 0;
  double g = x > 1 ? x / 2 : 1;
  for (int i = 0; i < 64; i++) {
    double n = 0.5 * (g + x / g);
    if (n == g) break;
    g = n;
  }
  return g;
}
static double dpow(double b, uint32_t n) {
  double r = 1;
  while (n) {
    if (n & 1) r *= b;
    b *= b;
    n >>= 1;
  }
  return r;
}

/* colors: t goes from 0 (a) to 256 (b) */
static color mix(color a, color b, int t) {
  int ar = a >> 11, ag = a >> 5 & 63, ab = a & 31, br = b >> 11, bg = b >> 5 & 63, bb = b & 31;
  return (color)(((ar + ((br - ar) * t >> 8)) << 11) | ((ag + ((bg - ag) * t >> 8)) << 5) | (ab + ((bb - ab) * t >> 8)));
}

/* ------------------------------------------------------------------ text */
static void text(const char *s, int x, int y, color fg, color bg) {
  eadk_display_draw_string(s, (eadk_point_t){(uint16_t)x, (uint16_t)y}, false, fg, bg);
}
static void bigtext(const char *s, int x, int y, color fg, color bg) {
  eadk_display_draw_string(s, (eadk_point_t){(uint16_t)x, (uint16_t)y}, true, fg, bg);
}
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}
static char *num(char *o, uint32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
/* 1.23K, 45.6M, 789B ... (no dollar sign) */
static char *fmt(char *o, double v) {
  static const char *const suf[] = {"", "K", "M", "B", "T", "Qa", "Qi", "Sx", "Sp", "Oc", "No", "Dc"};
  if (!(v > 0)) return cat(o, "0");
  if (v < 1000) {
    if (v < 10) {
      int t = (int)(v * 10);
      o = num(o, (uint32_t)(t / 10));
      *o++ = '.';
      *o++ = (char)('0' + t % 10);
      *o = 0;
      return o;
    }
    return num(o, (uint32_t)v);
  }
  int e = 0;
  while (v >= 999.5 && e < 11) v /= 1000, e++;
  if (!(v < 999999)) v = 999999; /* beyond the last suffix (or not a number) */
  if (v < 10) {
    int t = (int)(v * 100);
    o = num(o, (uint32_t)(t / 100));
    *o++ = '.';
    *o++ = (char)('0' + t / 10 % 10);
    *o++ = (char)('0' + t % 10);
    *o = 0;
  } else if (v < 100) {
    int t = (int)(v * 10);
    o = num(o, (uint32_t)(t / 10));
    *o++ = '.';
    *o++ = (char)('0' + t % 10);
    *o = 0;
  } else {
    o = num(o, (uint32_t)v);
  }
  return cat(o, suf[e]);
}
static char *dollars(char *o, double v) {
  *o++ = '$';
  return fmt(o, v);
}
/* "12s", "3m05s", "2h05m" */
static char *hms(char *o, uint32_t s) {
  if (s >= 3600) {
    o = num(o, s / 3600);
    o = cat(o, "h");
    if (s / 60 % 60 < 10) *o++ = '0';
    o = num(o, s / 60 % 60);
    return cat(o, "m");
  }
  if (s >= 60) {
    o = num(o, s / 60);
    o = cat(o, "m");
    if (s % 60 < 10) *o++ = '0';
    o = num(o, s % 60);
    return cat(o, "s");
  }
  o = num(o, s);
  return cat(o, "s");
}

/* ------------------------------------------------------------------ the businesses */
#define NB 10
typedef struct {
  const char *name, *boss;
  double cost, grow, rev, time, mgr; /* first price, price growth, sale per unit, seconds, manager's price */
  color c1, c2;                      /* walls, accent */
  uint8_t h;                         /* height of the building in the city */
} biz_t;
static const biz_t BIZ[NB] = {
  {"Lemonade", "Mia", 4, 1.07, 1, 1, 400, RGB(0xF4D03F), RGB(0xE74C3C), 14},
  {"Newsstand", "Noah", 60, 1.15, 60, 3, 5000, RGB(0x3FA569), RGB(0xF2F2F2), 20},
  {"Bakery", "Rosa", 720, 1.14, 540, 6, 60000, RGB(0xD98C4A), RGB(0xFFF1D0), 26},
  {"Cafe", "Leo", 8640, 1.13, 4320, 12, 700000, RGB(0x4DB0C2), RGB(0xB5651D), 28},
  {"Restaurant", "Chef Anna", 103680, 1.12, 51840, 24, 8000000, RGB(0xC8453B), RGB(0xFFD35C), 34},
  {"Factory", "Gus", 1244160, 1.11, 622080, 48, 100000000, RGB(0x7C8591), RGB(0x3B4048), 34},
  {"Bank", "Vera", 14929920, 1.10, 7464960, 96, 1200000000.0, RGB(0xDAD3BC), RGB(0xE0B53A), 42},
  {"Hotel", "Sir Hugo", 179159040, 1.09, 89579520, 192, 15000000000.0, RGB(0x6F66BF), RGB(0xFFD35C), 58},
  {"Tech Lab", "Dr. Kim", 2149908480.0, 1.08, 1074954240.0, 384, 200000000000.0, RGB(0x2F5B8A), RGB(0x6FE3FF), 66},
  {"Space Port", "Cmdr Zed", 25798901760.0, 1.07, 12899450880.0, 768, 3000000000000.0, RGB(0xE8ECF2), RGB(0xE23D3D), 72},
};
/* every milestone doubles a business's profit */
static const uint16_t MS[] = {25, 50, 100, 200, 300, 400, 500, 600, 700, 800, 900, 1000};
#define NMS ((int)(sizeof MS / sizeof MS[0]))

/* upgrades: 3 tiers of x3 per business (level needed, price in first prices), then global ones */
static const uint16_t UREQ[3] = {10, 50, 150};
static const double UCOST[3] = {300, 30000, 3000000};
static const char *const UTXT[3] = {"Better ads", "Pro staff", "Mega deal"};
typedef struct {
  const char *name;
  double cost, mult;
} gup_t;
#define NG 8
static const gup_t GUP[NG] = {
  {"Marketing", 5e4, 2}, {"City Ads", 5e6, 2}, {"Radio Spots", 5e8, 2}, {"TV Campaign", 5e10, 3},
  {"Global Brand", 5e13, 3}, {"Mega Corp", 5e16, 4}, {"Monopoly", 5e19, 5}, {"World Peace", 5e23, 10},
};
#define NU (NB * 3 + NG)

/* stars: permanent upgrades */
typedef struct {
  const char *name, *desc;
  uint8_t max;
  uint16_t cost[6];
} sup_t;
#define NS 5
static const sup_t SUP[NS] = {
  {"Seed Money", "Start with more cash", 5, {2, 5, 12, 30, 80}},
  {"Bulk Deals", "Prices -4% per level", 6, {3, 6, 12, 25, 50, 100}},
  {"Fast Trucks", "Golden trucks come sooner", 4, {4, 10, 25, 60}},
  {"Super Profit", "Profit +50% per level", 5, {10, 40, 150, 600, 2500}},
  {"Overtime", "Sales 8% faster per level", 5, {8, 30, 100, 350, 1200}},
};
static const double SEED_CASH[6] = {4, 1e3, 1e5, 1e7, 1e9, 1e11};

#define NA 20
static const char *const ACH_NAME[NA] = {
  "First Sale", "Small Biz", "Millionaire", "Billionaire", "Trillionaire", "Quadrillion", "Diversified",
  "Big Shop", "Sky High", "Delegator", "The Boss", "Upgrader", "Optimizer", "Franchisor", "Star Power",
  "Lucky Day", "Truck Fan", "Hustler", "Cash Flow", "Dedicated",
};
static const char *const ACH_DESC[NA] = {
  "Earn your first dollar", "Earn $1,000 in total", "Earn $1M in total", "Earn $1B in total",
  "Earn $1T in total", "Earn $1Qa in total", "Own all 10 businesses", "Own 100 of one business",
  "Own 300 of one business", "Hire your first manager", "Hire all 10 managers", "Buy 10 upgrades",
  "Buy 25 upgrades", "Franchise for the first time", "Collect 25 stars in total", "Catch a golden truck",
  "Catch 10 golden trucks", "Run businesses by hand 100x", "Make $1M every second", "Play for one hour",
};

/* ------------------------------------------------------------------ the saved game */
#define SAVE_NAME "tycoon.sav"
#define O_PART 1 /* particles */
#define O_RAIN 2 /* rain showers */
#define O_DAY 4  /* the day goes round (else always day) */
static struct {
  uint8_t magic, version, opts, qty; /* qty: 0 x1, 1 x10, 2 x100, 3 max */
  uint8_t tab, gu, spd, started;     /* spd: 0 x1, 1 x2, 2 x5 */
  double money, run, life;           /* cash; earned this run; earned ever */
  uint32_t owned[NB];
  float prog[NB]; /* how far each sale is */
  uint32_t stars, stars_total, franchises, ach, taps, trucks, ups, play_s, phase;
  uint16_t mgr; /* hired managers, a bit each */
  uint8_t bu[NB], su[NS];
} V;
static const int QTY[4] = {1, 10, 100, 0};

static void fresh(void) {
  for (uint32_t i = 0; i < sizeof V; i++) ((uint8_t *)&V)[i] = 0;
  V.magic = 'T', V.version = 1, V.opts = O_PART | O_RAIN | O_DAY, V.money = 4;
}
static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  bool ok = d && n == sizeof V && d[0] == 'T' && d[1] == 1;
  if (!ok) return fresh();
  for (uint32_t i = 0; i < sizeof V; i++) ((uint8_t *)&V)[i] = d[i];
  if (!(V.money >= 0) || V.money > 1e300) V.money = 0;
  if (!(V.run >= 0) || V.run > 1e300) V.run = 0;
  if (!(V.life >= V.run) || V.life > 1e300) V.life = V.run;
  V.opts &= 7, V.qty &= 3, V.tab %= 5, V.spd %= 3, V.phase %= 240000u;
  for (int i = 0; i < NB; i++) {
    if (V.owned[i] > 100000u) V.owned[i] = 100000u;
    if (!(V.prog[i] >= 0 && V.prog[i] < 1)) V.prog[i] = 0;
    V.bu[i] &= 7;
  }
  for (int i = 0; i < NS; i++)
    if (V.su[i] > SUP[i].max) V.su[i] = SUP[i].max;
  V.mgr &= 0x3FF;
}
static void save(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  if (d && n == sizeof V) {
    uint32_t i = 0;
    while (i < n && d[i] == ((uint8_t *)&V)[i]) i++;
    if (i == n) return; /* nothing new */
  }
  ef_write(SAVE_NAME, &V, sizeof V);
}

/* ------------------------------------------------------------------ the economy */
static double inc;       /* sales per second, managed businesses */
static int boost_ms;     /* truck bonus: all profit x7 */
static int rush_ms;      /* truck bonus: all sales 3x faster */
static bool running[NB]; /* a sale started by hand */

static double discount(void) { return 1.0 - 0.04 * V.su[1]; }
static double unit_cost(int i) { return BIZ[i].cost * dpow(BIZ[i].grow, V.owned[i]) * discount(); }
/* price of n more of business i */
static double cost_n(int i, int n) {
  double g = BIZ[i].grow;
  return unit_cost(i) * (dpow(g, (uint32_t)n) - 1) / (g - 1);
}
/* how many can be bought with the money in hand */
static int max_n(int i) {
  double c = unit_cost(i), m = V.money, g = BIZ[i].grow;
  int n = 0;
  while (n < 2000 && m >= c) m -= c, c *= g, n++;
  return n;
}
static int ach_count(void) {
  int n = 0;
  for (int k = 0; k < NA; k++) n += V.ach >> k & 1;
  return n;
}
static double mult_all(void) {
  double m = (1 + 0.02 * V.stars_total) * (1 + 0.5 * V.su[3]) * (1 + 0.01 * ach_count());
  for (int g = 0; g < NG; g++)
    if (V.gu >> g & 1) m *= GUP[g].mult;
  if (boost_ms > 0) m *= 7;
  return m;
}
static double mult_biz(int i) {
  double m = 1;
  for (int t = 0; t < 3; t++)
    if (V.bu[i] >> t & 1) m *= 3;
  for (int k = 0; k < NMS; k++)
    if (V.owned[i] >= MS[k]) m *= 2;
  return m;
}
static double rev_of(int i) { return BIZ[i].rev * V.owned[i] * mult_biz(i) * mult_all(); }
static double time_of(int i) {
  double t = BIZ[i].time;
  for (int k = 0; k < V.su[4]; k++) t *= 0.92;
  return rush_ms > 0 ? t / 3 : t;
}
static void earn(double v) { V.money += v, V.run += v, V.life += v; }
static uint32_t stars_pot(void) {
  double x = V.life / 1e12;
  return x < 1 ? 0 : (uint32_t)dsqrt(x);
}
static uint32_t stars_gain(void) {
  uint32_t p = stars_pot();
  return p > V.stars_total ? p - V.stars_total : 0;
}
static double seed_cash(void) { return SEED_CASH[V.su[0]]; }

/* upgrades: ids 0..29 are business tiers (i * 3 + t), 30.. the global ones */
static bool up_bought(int u) { return u < NB * 3 ? (V.bu[u / 3] >> (u % 3) & 1) : (V.gu >> (u - NB * 3) & 1); }
static double up_cost(int u) { return u < NB * 3 ? BIZ[u / 3].cost * UCOST[u % 3] : GUP[u - NB * 3].cost; }
static bool up_visible(int u) {
  if (up_bought(u)) return false;
  if (u < NB * 3) {
    int i = u / 3, t = u % 3;
    return V.owned[i] >= UREQ[t] && (t == 0 || (V.bu[i] >> (t - 1) & 1));
  }
  int g = u - NB * 3;
  return g == 0 || (V.gu >> (g - 1) & 1);
}
static uint8_t ulist[NU];
static int ucount;
static void build_ulist(void) {
  ucount = 0;
  for (int u = 0; u < NU; u++)
    if (up_visible(u)) {
      int j = ucount++;
      while (j > 0 && up_cost(ulist[j - 1]) > up_cost(u)) ulist[j] = ulist[j - 1], j--;
      ulist[j] = (uint8_t)u;
    }
}
/* managers still to hire, then the hired ones, for the businesses you own */
static uint8_t mlist[NB];
static int mcount;
static void build_mlist(void) {
  mcount = 0;
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < NB; i++)
      if (V.owned[i] && (V.mgr >> i & 1) == pass) mlist[mcount++] = (uint8_t)i;
}

static bool ach_ok(int k) {
  switch (k) {
    case 0: return V.life >= 1;
    case 1: return V.life >= 1e3;
    case 2: return V.life >= 1e6;
    case 3: return V.life >= 1e9;
    case 4: return V.life >= 1e12;
    case 5: return V.life >= 1e15;
    case 6:
      for (int i = 0; i < NB; i++)
        if (!V.owned[i]) return false;
      return true;
    case 7:
      for (int i = 0; i < NB; i++)
        if (V.owned[i] >= 100) return true;
      return false;
    case 8:
      for (int i = 0; i < NB; i++)
        if (V.owned[i] >= 300) return true;
      return false;
    case 9: return V.mgr != 0;
    case 10: return V.mgr == 0x3FF;
    case 11: return V.ups >= 10;
    case 12: return V.ups >= 25;
    case 13: return V.franchises >= 1;
    case 14: return V.stars_total >= 25;
    case 15: return V.trucks >= 1;
    case 16: return V.trucks >= 10;
    case 17: return V.taps >= 100;
    case 18: return inc >= 1e6;
    default: return V.play_s >= 3600;
  }
}

/* ------------------------------------------------------------------ canvas */
/* Everything is drawn through R() onto a canvas: a band of the city, or the picture of a row. */
#define SW 320
#define SH 96
#define BH 12
#define GY 80 /* the ground the buildings stand on */
static color band[SW * BH];
static color *cv = band;
static int cvw = SW, cvh = BH, cox, coy; /* pixel (x, y) lands at (x - cox, y - coy) */
static void R(int x, int y, int w, int h, color c) {
  x -= cox, y -= coy;
  int x1 = x + w, y1 = y + h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x1 > cvw) x1 = cvw;
  if (y1 > cvh) y1 = cvh;
  if (x >= x1 || y >= y1) return;
  for (; y < y1; y++) {
    color *p = cv + y * cvw;
    for (int i = x; i < x1; i++) p[i] = c;
  }
}
static void disc(int cx, int cy, int r, color c) {
  for (int dy = -r; dy <= r; dy++) {
    int w = isq(r * r - dy * dy);
    R(cx - w, cy + dy, 2 * w + 1, 1, c);
  }
}
static void fill(int x, int y, int w, int h, color c) {
  if (w > 0 && h > 0) eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, c);
}

/* ------------------------------------------------------------------ the sky */
typedef struct {
  uint16_t at, night;
  color top, bot;
} kf_t;
#define DAY_TOP RGB(0x4FA6F0)
#define DAY_BOT RGB(0xBFE3FF)
#define NIGHT_TOP RGB(0x070B24)
#define NIGHT_BOT RGB(0x1B2A5A)
static const kf_t KF[] = {
  {0, 0, DAY_TOP, DAY_BOT},
  {90, 0, DAY_TOP, DAY_BOT},
  {128, 128, RGB(0x4A3C94), RGB(0xFF9A5A)},
  {158, 256, NIGHT_TOP, NIGHT_BOT},
  {226, 256, NIGHT_TOP, NIGHT_BOT},
  {246, 128, RGB(0x5A5FA8), RGB(0xFFB38A)},
  {256, 0, DAY_TOP, DAY_BOT},
};
static color sky_top, sky_bot;
static int nf, amb, ph;  /* night 0..256; how much everything is dimmed; the hour 0..255 */
static int tick_ms;      /* free-running clock for the animations */
static int rain_ms, rain_wait = 60000;
static bool demo;        /* the title shows a ready-made city */
static color A(color c) { return amb ? mix(c, RGB(0x0E1638), amb) : c; }

static void sky_update(void) {
  ph = (int)(V.phase * 256u / 240000u);
  if (!(V.opts & O_DAY)) ph = 20;
  int k = 0;
  while (k < 5 && ph >= KF[k + 1].at) k++;
  int t = (ph - KF[k].at) * 256 / (KF[k + 1].at - KF[k].at);
  sky_top = mix(KF[k].top, KF[k + 1].top, t), sky_bot = mix(KF[k].bot, KF[k + 1].bot, t);
  nf = KF[k].night + ((KF[k + 1].night - KF[k].night) * t >> 8);
  amb = nf * 150 >> 8;
  if (rain_ms > 0) {
    amb = imin(200, amb + 36);
    sky_top = mix(sky_top, RGB(0x55606E), 120), sky_bot = mix(sky_bot, RGB(0x8E98A6), 120);
  }
}

/* ------------------------------------------------------------------ things that move */
typedef struct {
  float x, y, vx, vy;
  int16_t life;
  uint8_t kind; /* 0 coin, 1 confetti */
  color c;
} part_t;
#define NP 48
static part_t P[NP];
static void spawn(float x, float y, float vx, float vy, int life, int kind, color c) {
  if (!(V.opts & O_PART)) return;
  for (int i = 0; i < NP; i++)
    if (P[i].life <= 0) {
      P[i] = (part_t){x, y, vx, vy, (int16_t)life, (uint8_t)kind, c};
      return;
    }
}
static void burst(float x, float y, int n, int kind) {
  static const color pal[] = {RGB(0xFFD23F), RGB(0xFF5D73), RGB(0x4DD0E1), RGB(0x9CFF57), RGB(0xFFFFFF), RGB(0xC77DFF)};
  for (int i = 0; i < n; i++)
    spawn(x, y, (float)rr(-70, 70), (float)rr(-120, -20), rr(700, 1400), kind, pal[rnd() % 6]);
}
static float anim_t[NB]; /* ms since the last sale, per business */

typedef struct {
  float x, v;
  color c;
} car_t;
static car_t car[4] = {{20, 24, 0}, {190, 33, 0}, {260, -27, 0}, {90, -19, 0}};
typedef struct {
  float x, v;
  color shirt, skin;
} walker_t;
static walker_t wk[6];
static bool tr_on;
static float tr_x, tr_v;
static int tr_wait = 40000;
static uint8_t far_h[40];

static void scene_init(void) {
  static const color cc[] = {RGB(0xD64545), RGB(0x3D7BE0), RGB(0xF2F2F2), RGB(0x38A169), RGB(0xE9A23B), RGB(0x7B5CC4)};
  for (int i = 0; i < 4; i++) car[i].c = cc[rnd() % 6];
  static const color sh[] = {RGB(0xE74C3C), RGB(0x3498DB), RGB(0xF1C40F), RGB(0x2ECC71), RGB(0x9B59B6), RGB(0xECF0F1)};
  for (int i = 0; i < 6; i++)
    wk[i] = (walker_t){(float)rr(0, 319), (float)(rr(7, 13) * (i & 1 ? -1 : 1)), sh[i], rnd() & 1 ? RGB(0xF1C27D) : RGB(0xA9714B)};
  for (int i = 0; i < 40; i++) far_h[i] = (uint8_t)rr(14, 40);
}
static void scene_update(int dt) {
  float s = (float)dt * 0.001f;
  tick_ms += dt;
  if (V.opts & O_DAY) V.phase = (V.phase + (uint32_t)dt) % 240000u;
  for (int i = 0; i < NB; i++) anim_t[i] = anim_t[i] < 60000 ? anim_t[i] + (float)dt : 60000;
  for (int i = 0; i < 4; i++) {
    car[i].x += car[i].v * s;
    if (car[i].x > 340) car[i].x = -20;
    if (car[i].x < -20) car[i].x = 340;
  }
  for (int i = 0; i < 6; i++) {
    wk[i].x += wk[i].v * s;
    if (wk[i].x > 330) wk[i].x = -10;
    if (wk[i].x < -10) wk[i].x = 330;
  }
  for (int i = 0; i < NP; i++)
    if (P[i].life > 0) {
      P[i].life = (int16_t)(P[i].life - dt);
      P[i].x += P[i].vx * s, P[i].y += P[i].vy * s;
      if (P[i].kind == 1) P[i].vy += 150 * s;
      if (P[i].kind == 0) P[i].vy *= 1 - 1.2f * s;
    }
  if (tr_on) {
    tr_x += tr_v * s;
    if (tr_x > 350 || tr_x < -40) tr_on = false, tr_wait = 40000;
  }
  if (V.opts & O_RAIN) {
    if (rain_ms > 0) rain_ms -= dt;
    else if ((rain_wait -= dt) < 0) rain_ms = rr(12000, 30000), rain_wait = rr(90000, 240000);
  } else {
    rain_ms = 0;
  }
}

/* ------------------------------------------------------------------ buildings */
static void wins(int x, int y, int cols, int rows, int dx, int dy, int w, int h, int sd) {
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < cols; c++) {
      bool lit = nf > 90 && ((sd + r * 5 + c * 3 + (r ^ c)) & 3) != 0;
      R(x + c * dx, y + r * dy, w, h, lit ? RGB(0xFFD66B) : A(RGB(0x8FBEE6)));
    }
}
static void awning(int x, int y, int w, color a, color b) {
  for (int k = 0; k * 4 < w; k++) R(x + k * 4, y, imin(4, w - k * 4), 4, A(k & 1 ? b : a));
  R(x, y + 4, w, 1, A(RGB(0x30343C)));
}
static void smoke(int x, int y, int sd) {
  for (int k = 0; k < 5; k++) {
    int t = (tick_ms / 40 + sd * 17 + k * 13) % 70;
    int py = y - t / 2, px = x + (k & 1 ? 1 : -1) * (t / 9) + (t / 12);
    color c = mix(rain_ms > 0 ? RGB(0x8A9098) : RGB(0xC4CAD2), sky_bot, 70 + t * 2);
    disc(px, py, 1 + t / 32, c);
  }
}
static void flame_bit(int x, int y, int w) {
  R(x, y, w, 4, RGB(0xFFE27A));
  R(x + 1, y + 4, w - 2, 4, RGB(0xFF9A2E));
  R(x + 2, y + 8, w - 4, 3 + (tick_ms / 50 & 3), RGB(0xE5532D));
}

static int tier_of(int lv) { return lv >= 200 ? 3 : lv >= 100 ? 2 : lv >= 25 ? 1 : 0; }

static void lot(int x, bool next) {
  color wood = A(RGB(0x8B6B3E));
  R(x + 2, 72, 24, 2, wood);
  for (int k = 0; k < 4; k++) R(x + 2 + k * 7, 68, 2, 12, wood);
  R(x + 3, 78, 22, 2, A(RGB(0x7C6A4A)));
  R(x + 9, 58, 11, 8, A(WHITE));
  R(x + 10, 59, 9, 1, A(RGB(0xD64545)));
  R(x + 13, 66, 2, 8, wood);
  if (next) { /* a crane waits for the next one */
    R(x + 22, 12, 3, 58, A(RGB(0xE5A82E)));
    R(x + 2, 12, 26, 2, A(RGB(0xE5A82E)));
    R(x + 5 + (tick_ms / 90 % 6), 14, 1, 14 + (tick_ms / 400 & 3), A(RGB(0x30343C)));
    R(x + 3 + (tick_ms / 90 % 6), 28 + (tick_ms / 400 & 3), 5, 3, A(RGB(0xD64545)));
  }
}

static void building(int i, int lv, int x) {
  const biz_t *b = &BIZ[i];
  int tier = tier_of(lv), H = b->h + (i >= 1 && i <= 8 ? tier * 3 : 0), top = GY - H;
  color c1 = A(b->c1), c2 = A(b->c2), trim = tier >= 1 ? A(RGB(0xE0B53A)) : c2, dark = A(RGB(0x2B2F38));
  switch (i) {
    case 0: /* the stand */
      R(x + 3, 72, 22, 8, A(RGB(0xB5762F)));
      R(x + 2, 71, 24, 2, A(RGB(0xDDA25A)));
      R(x + 2, 62, 1, 10, dark), R(x + 25, 62, 1, 10, dark);
      awning(x + 1, 60, 26, A(b->c2), A(WHITE));
      R(x + 6, 67, 5, 4, A(RGB(0xF7DC3A))), R(x + 8, 66, 2, 1, A(RGB(0x3FA569)));
      R(x + 11, 73, 8, 5, A(WHITE)), R(x + 13, 75, 4, 1, c2);
      R(x + 19, 66, 4, 5, A(RGB(0xFFF4B0)));
      if (tier >= 1) R(x + 14, 48, 1, 12, dark), R(x + 15, 48, 7, 4, trim);
      if (tier >= 2) disc(x + 14, 45, 2, A(RGB(0xF7DC3A)));
      break;
    case 1: /* newsstand */
      R(x + 3, top, 22, H, c1);
      R(x + 2, top - 2, 24, 3, trim);
      awning(x + 3, top + 6, 22, c1, A(WHITE));
      R(x + 5, top + 12, 18, 8, dark);
      R(x + 6, top + 14, 4, 5, A(WHITE)), R(x + 11, top + 14, 4, 5, A(RGB(0xFFD35C))), R(x + 16, top + 14, 5, 5, A(RGB(0xE8EEF5)));
      R(x + 8, top + 1, 12, 4, A(WHITE)), R(x + 9, top + 2, 10, 1, c1);
      break;
    case 2: /* bakery */
      R(x + 2, top, 24, H, c1);
      R(x + 1, top - 2, 26, 3, trim);
      R(x + 19, top - 8, 4, 8, A(RGB(0x9A4F3A)));
      smoke(x + 21, top - 9, i);
      awning(x + 2, top + 8, 24, c1, c2);
      wins(x + 5, top + 15, 2, 1, 11, 0, 5, 6, i);
      R(x + 18, GY - 10, 5, 10, dark);
      R(x + 6, top + 2, 12, 5, A(RGB(0xF3C987))), R(x + 8, top + 3, 8, 3, A(RGB(0xC9833A)));
      break;
    case 3: /* cafe with a parasol */
      R(x + 2, top, 19, H, c1);
      R(x + 1, top - 2, 21, 3, trim);
      wins(x + 4, top + 5, 2, 2, 8, 8, 5, 6, i);
      R(x + 9, GY - 9, 5, 9, dark);
      R(x + 7, top + 14, 9, 2, c2);
      R(x + 25, 66, 1, 14, dark);
      R(x + 22, 64, 8, 2, A(RGB(0xE8503A))), R(x + 23, 62, 6, 2, A(RGB(0xE8503A))), R(x + 24, 60, 4, 2, A(WHITE));
      R(x + 22, 76, 8, 1, A(RGB(0x6B4A2B))), R(x + 25, 77, 1, 3, A(RGB(0x6B4A2B)));
      break;
    case 4: /* restaurant with its neon sign */
      R(x + 2, top, 24, H, c1);
      R(x + 1, top - 2, 26, 3, trim);
      wins(x + 5, top + 12, 3, 1, 7, 0, 5, 6, i);
      wins(x + 5, top + 21, 2, 1, 7, 0, 5, 6, i);
      R(x + 19, GY - 10, 5, 10, dark);
      R(x + 5, top + 3, 16, 6, dark);
      R(x + 6, top + 4, 14, 4, (nf > 90 && (tick_ms / 500 & 1)) ? RGB(0xFF4D6D) : (nf > 90 ? RGB(0xFFD35C) : A(RGB(0xE8C468))));
      break;
    case 5: /* factory with chimneys */
      R(x + 1, top + 12, 26, H - 12, c1);
      for (int k = 0; k < 3; k++) R(x + 1 + k * 9, top + 6, 8, 6, c1), R(x + 1 + k * 9, top + 5, 8, 1, trim);
      R(x + 3, top - 12, 4, 17, A(RGB(0xA0524A))), R(x + 3, top - 12, 4, 2, c2);
      R(x + 11, top - 6, 4, 11, A(RGB(0xA0524A))), R(x + 11, top - 6, 4, 2, c2);
      smoke(x + 5, top - 13, i), smoke(x + 13, top - 7, i + 3);
      wins(x + 4, top + 16, 4, 1, 6, 0, 4, 5, i);
      R(x + 17, GY - 12, 8, 12, dark);
      break;
    case 6: /* bank */
      for (int k = 0; k < 8; k++) R(x + 1 + k, top + 7 - k, 26 - 2 * k, 1, trim);
      R(x + 2, top + 8, 24, 3, c1);
      for (int k = 0; k < 4; k++) R(x + 4 + k * 6, top + 11, 3, H - 14, A(WHITE));
      R(x + 1, GY - 3, 26, 3, c1), R(x + 2, GY - 6, 24, 1, c1);
      R(x + 11, GY - 12, 6, 12, dark);
      R(x + 12, top + 2, 4, 3, A(RGB(0xFFF0A0)));
      if (tier >= 2) R(x + 13, top - 8, 1, 8, dark), R(x + 14, top - 8, 5, 3, c2);
      break;
    case 7: /* hotel */
      R(x + 3, top, 22, H, c1);
      R(x + 2, top - 2, 24, 3, trim);
      wins(x + 6, top + 6, 4, 6, 5, 8, 3, 5, i);
      R(x + 11, GY - 8, 6, 8, dark);
      R(x + 4, GY - 14, 20, 2, c2);
      R(x + 9, top - 12, 2, 10, A(RGB(0xD64545))), R(x + 15, top - 12, 2, 10, A(RGB(0xD64545))), R(x + 9, top - 8, 8, 2, A(RGB(0xD64545)));
      break;
    case 8: /* tech tower */
      R(x + 4, top, 20, H, c1);
      R(x + 3, top - 2, 22, 3, trim);
      for (int k = 0; k < H / 5; k++) R(x + 5, top + 3 + k * 5, 18, 1, A(RGB(0x4B86C4)));
      for (int k = 0; k < 4; k++) R(x + 8 + k * 4, top + 2, 1, H - 6, A(RGB(0x3C73B0)));
      wins(x + 6, top + 8, 4, 8, 4, 5, 2, 3, i);
      R(x + 13, top - 14, 2, 13, dark);
      R(x + 12, top - 16, 4, 3, (tick_ms / 400 & 1) ? RGB(0xFF3B3B) : A(RGB(0x7A2323)));
      if (tick_ms / 200 % 8 < 3) R(x + 9, top - 12, 1, 3, A(c2)), R(x + 18, top - 12, 1, 3, A(c2));
      R(x + 11, GY - 7, 6, 7, dark);
      break;
    default: { /* space port */
      float a = anim_t[i];
      int lift = a < 2600 ? (int)(a * a / 45000.0f) : a < 3400 ? 999 : 0; /* takes off, then a new one rolls out */
      R(x + 2, top + 12, 3, H - 12, A(RGB(0x8E98A6)));
      for (int k = 0; k < (H - 12) / 6; k++) R(x + 2, top + 14 + k * 6, 3, 1, dark);
      R(x + 2, top + 12, 12, 2, A(RGB(0x8E98A6)));
      R(x + 6, GY - 3, 20, 3, A(RGB(0x5C6370)));
      if (lift < 100) {
        int ry = GY - 4 - lift, rx = x + 14;
        R(rx, ry - 30, 7, 28, A(WHITE)), R(rx + 5, ry - 30, 2, 28, A(RGB(0xC9D1DB)));
        R(rx + 1, ry - 34, 5, 4, c2), R(rx + 2, ry - 37, 3, 3, c2);
        R(rx + 2, ry - 22, 3, 3, A(RGB(0x6FE3FF)));
        R(rx - 3, ry - 10, 3, 10, c2), R(rx + 7, ry - 10, 3, 10, c2);
        if (a < 2600) flame_bit(rx + 1, ry - 2, 5);
      }
      break;
    }
  }
  if (tier >= 3 && i >= 1) disc(x + 14, top - 6, 2, A(RGB(0xFFD23F)));
}

static int shown_level(int i) { return demo ? 30 + i * 35 : (int)V.owned[i]; }
static void city(void) {
  int next = -1;
  if (!demo)
    for (int i = 0; i < NB; i++)
      if (!V.owned[i]) {
        next = i;
        break;
      }
  for (int i = 0; i < NB; i++) {
    int x = i * 32 + 2, lv = shown_level(i);
    if (lv > 0) building(i, lv, x);
    else lot(x, i == next);
    if (i < NB - 1 && !(i & 1)) { /* a bush between neighbors */
      R(x + 29, 77, 4, 3, A(RGB(0x2F8F46))), R(x + 30, 75, 2, 2, A(RGB(0x3FB35B)));
    }
  }
}

/* ------------------------------------------------------------------ the whole scene */
static void scene_pass(void) {
  int y0 = coy, y1 = coy + cvh;
  for (int y = y0; y < y1 && y < GY; y++) R(0, y, SW, 1, mix(sky_top, sky_bot, y * 256 / GY));
  if (nf > 100 && rain_ms <= 0)
    for (int k = 0; k < 40; k++) {
      int sx = (k * 97 + 13) % SW, sy = (k * 53 + 7) % 56;
      if (sy >= y1 || sy + 1 < y0) continue;
      if (((tick_ms >> 8) + k) % 6) R(sx, sy, 1, 1, mix(sky_top, WHITE, imin(256, nf)));
      else R(sx, sy, 2, 1, WHITE), R(sx, sy - 1, 1, 3, WHITE);
    }
  /* sun and moon */
  if (ph < 140) {
    int u = ph * 256 / 140, sx = 16 + u * 288 / 256, sy = 76 - (u * (256 - u) >> 8);
    disc(sx, sy, 11, mix(sky_top, RGB(0xFFF1A8), 70)), disc(sx, sy, 8, RGB(0xFFD43B)), disc(sx, sy, 6, RGB(0xFFE66D));
  } else if (ph >= 150) {
    int v = (ph - 150) * 256 / 106, mx = 20 + v * 280 / 256, my = 76 - (v * (256 - v) >> 8);
    disc(mx, my, 7, RGB(0xF4F1DE)), disc(mx + 3, my - 1, 6, mix(sky_top, sky_bot, 80));
    disc(mx - 1, my + 1, 1, RGB(0xD5D2BF));
  }
  /* clouds */
  color cl = mix(WHITE, sky_top, imin(256, nf));
  for (int k = 0; k < 4; k++) {
    int x = (tick_ms / (90 + k * 40) + k * 101) % 420 - 60, y = 8 + k * 11 + (k & 1) * 6;
    R(x + 4, y + 4, 34, 7, cl), R(x + 10, y, 18, 11, cl), R(x + 22, y + 2, 14, 9, cl);
  }
  /* far skyline */
  color far = mix(sky_bot, RGB(0x2B3A67), 90 + nf / 3);
  for (int k = 0; k < 40; k++)
    if (GY - far_h[k] < y1) R(k * 8, GY - far_h[k], 8, far_h[k], far), R(k * 8 + 2, GY - far_h[k] - 2, 4, 2, far);
  city();
  /* the street */
  R(0, GY, SW, 5, A(RGB(0xB8B8C2))), R(0, GY + 4, SW, 1, A(RGB(0x8E8E9A)));
  R(0, GY + 5, SW, 11, A(RGB(0x3B3F4A)));
  for (int x = 0; x < SW; x += 16) R(x, GY + 10, 8, 1, A(RGB(0xE3C94F)));
  bool lights = nf > 90;
  for (int i = 0; i < 4; i++) {
    int cx = (int)car[i].x, cy = car[i].v > 0 ? GY + 6 : GY + 11;
    R(cx, cy + 2, 14, 3, A(car[i].c)), R(cx + 3, cy, 8, 3, A(car[i].c)), R(cx + 4, cy + 1, 6, 1, A(RGB(0xBFE3FF)));
    R(cx + 2, cy + 4, 3, 2, BLACK), R(cx + 9, cy + 4, 3, 2, BLACK);
    if (lights) {
      if (car[i].v > 0) R(cx + 14, cy + 2, 7, 2, mix(RGB(0xFFF3A0), A(RGB(0x3B3F4A)), 90));
      else R(cx - 7, cy + 2, 7, 2, mix(RGB(0xFFF3A0), A(RGB(0x3B3F4A)), 90));
      R(car[i].v > 0 ? cx + 13 : cx, cy + 2, 1, 2, RGB(0xFFF3A0));
    }
  }
  for (int i = 0; i < 6; i++) {
    int wx = (int)wk[i].x, step = (tick_ms / 160 + i) & 1;
    R(wx, GY - 3, 2, 2, A(wk[i].skin)), R(wx, GY - 1, 2, 3, A(wk[i].shirt)), R(wx, GY + 2, 1, 1 + step, A(RGB(0x30343C))), R(wx + 1, GY + 2, 1, 2 - step, A(RGB(0x30343C)));
  }
  if (tr_on) { /* the golden truck */
    int tx = (int)tr_x, dir = tr_v > 0 ? 1 : -1, bx = dir > 0 ? tx : tx + 8, cx = dir > 0 ? tx + 15 : tx;
    R(bx, GY + 1, 15, 9, RGB(0xFFC933)), R(bx, GY + 1, 15, 2, RGB(0xFFE680)), R(bx + 3, GY + 4, 9, 3, RGB(0xE08E0B));
    R(cx, GY + 3, 8, 7, RGB(0xF2A413)), R(cx + (dir > 0 ? 3 : 1), GY + 4, 4, 3, RGB(0xBFE3FF));
    R(tx + 3, GY + 9, 4, 3, BLACK), R(tx + 16, GY + 9, 4, 3, BLACK);
    if ((tick_ms >> 7) & 1) R(tx + 7, GY - 6, 1, 5, WHITE), R(tx + 5, GY - 4, 5, 1, WHITE);
    else R(tx + 17, GY - 5, 1, 4, WHITE), R(tx + 15, GY - 3, 5, 1, WHITE);
  }
  for (int i = 0; i < NP; i++)
    if (P[i].life > 0) {
      int px = (int)P[i].x, py = (int)P[i].y;
      if (P[i].kind == 0) {
        if (P[i].life < 300 && (P[i].life >> 5 & 1)) continue;
        R(px, py, 3, 3, RGB(0xFFC933)), R(px, py, 2, 1, RGB(0xFFF3A0));
      } else {
        R(px, py, 2, 2, P[i].c);
      }
    }
  if (rain_ms > 0)
    for (int k = 0; k < 36; k++) {
      int ry = (k * 31 + tick_ms / 3) % SH, rx = (k * 53 + 640 - ry / 3) % SW;
      R(rx, ry, 1, 3, mix(RGB(0xDDE8F5), sky_bot, 110));
    }
}
static void scene_render(void) {
  sky_update();
  cv = band, cvw = SW, cvh = BH, cox = 0;
  for (int b = 0; b < SH / BH; b++) {
    coy = b * BH;
    scene_pass();
    eadk_display_push_rect((eadk_rect_t){0, (uint16_t)coy, SW, BH}, band);
  }
}

/* ------------------------------------------------------------------ the game */
static char toast[48];
static int toast_ms;
static color toast_col;
static bool hud_dirty = true;
static bool scr_game_active;
static void say(const char *s, color c) {
  char *o = toast;
  while (*s && o < toast + sizeof toast - 1) *o++ = *s++;
  *o = 0;
  toast_ms = 3600, toast_col = c, hud_dirty = true;
}
static const int SPD[3] = {1, 2, 5};
#define GOLD RGB(0xFFC933)

static void sale_fx(int i) {
  anim_t[i] = 0;
  int top = GY - BIZ[i].h - 6;
  for (int k = rr(1, 3); k > 0; k--) spawn((float)(i * 32 + rr(8, 22)), (float)top, (float)rr(-8, 8), (float)rr(-46, -26), 900, 0, GOLD);
}

static bool unlocked(int i) { return i == 0 || V.owned[i - 1] || V.owned[i]; }
static void buy_biz(int i) {
  if (!unlocked(i)) return;
  int q = QTY[V.qty], n = q ? q : max_n(i);
  if (n < 1) n = 1;
  double c = cost_n(i, n);
  if (V.money < c) return;
  bool first = V.owned[i] == 0;
  V.money -= c;
  V.owned[i] += (uint32_t)n;
  if (V.owned[i] > 100000u) V.owned[i] = 100000u;
  if (first) {
    char b[48], *o = cat(b, "New business: ");
    cat(o, BIZ[i].name);
    say(b, RGB(0x7CFFB2));
    burst((float)(i * 32 + 16), (float)(GY - BIZ[i].h), 14, 1);
  }
  for (int k = 0; k < NMS; k++)
    if (V.owned[i] >= MS[k] && V.owned[i] - (uint32_t)n < MS[k]) {
      char b[48], *o = cat(b, BIZ[i].name);
      cat(o, " profit x2!");
      say(b, GOLD);
      burst((float)(i * 32 + 16), (float)(GY - BIZ[i].h), 24, 1);
    }
}
static void run_biz(int i) {
  if (V.owned[i] && !(V.mgr >> i & 1) && !running[i]) running[i] = true, V.taps++;
}
static void hire(int i) {
  if (!V.owned[i] || (V.mgr >> i & 1) || V.money < BIZ[i].mgr) return;
  V.money -= BIZ[i].mgr;
  V.mgr |= (uint16_t)(1 << i);
  char b[48], *o = cat(b, "Hired ");
  cat(o, BIZ[i].boss);
  say(b, RGB(0x7CFFB2));
}
static void buy_up(int u) {
  if (!up_visible(u) || V.money < up_cost(u)) return;
  V.money -= up_cost(u);
  if (u < NB * 3) V.bu[u / 3] |= (uint8_t)(1 << (u % 3));
  else V.gu |= (uint8_t)(1 << (u - NB * 3));
  V.ups++;
  say("Upgrade bought!", RGB(0x7CFFB2));
}
static void buy_star(int s) {
  if (V.su[s] >= SUP[s].max || V.stars < SUP[s].cost[V.su[s]]) return;
  V.stars -= SUP[s].cost[V.su[s]];
  V.su[s]++;
  say("Star upgrade!", GOLD);
}
static void franchise(void) {
  uint32_t g = stars_gain();
  if (!g) return;
  V.stars += g, V.stars_total += g, V.franchises++;
  V.run = 0, V.mgr = 0, V.gu = 0;
  for (int i = 0; i < NB; i++) V.owned[i] = 0, V.prog[i] = 0, V.bu[i] = 0, running[i] = false;
  V.money = seed_cash();
  boost_ms = rush_ms = 0;
  char b[48], *o = num(b, g);
  cat(o, " stars! New empire.");
  say(b, GOLD);
  for (int k = 0; k < 4; k++) burst((float)rr(40, 280), (float)rr(20, 60), 12, 1);
}

static void catch_truck(void) {
  if (!tr_on) return;
  tr_on = false;
  tr_wait = rr(45000, 90000);
  for (int k = 0; k < V.su[2]; k++) tr_wait = tr_wait * 4 / 5;
  V.trucks++;
  burst(tr_x + 10, (float)GY, 22, 1);
  int r = (int)(rnd() % 100);
  char b[48], *o;
  if (r < 55) {
    double v = inc * rr(60, 150);
    if (v < 10) v = 10;
    earn(v);
    o = cat(b, "Delivery: +");
    dollars(o, v);
    say(b, GOLD);
  } else if (r < 80) {
    boost_ms = 30000;
    say("Profit x7 for 30 s!", GOLD);
  } else if (r < 95) {
    rush_ms = 30000;
    say("Sales 3x faster for 30 s!", GOLD);
  } else if (V.stars_total) {
    uint32_t n = (uint32_t)rr(1, 3);
    V.stars += n;
    o = cat(b, "Lucky! +");
    o = num(o, n);
    cat(o, " stars");
    say(b, GOLD);
  } else {
    earn(inc * 200 + 50);
    say("Big delivery!", GOLD);
  }
}

static int acc_ms;
static void sim(int dt) {
  int g = dt * SPD[V.spd];
  if (boost_ms > 0) boost_ms -= dt;
  if (rush_ms > 0) rush_ms -= dt;
  if (toast_ms > 0 && (toast_ms -= dt) <= 0) hud_dirty = true;
  for (int i = 0; i < NB; i++) {
    if (!V.owned[i]) {
      V.prog[i] = 0, running[i] = false;
      continue;
    }
    bool m = V.mgr >> i & 1;
    if (!m && !running[i]) {
      V.prog[i] = 0;
      continue;
    }
    double p = V.prog[i] + (double)g / 1000.0 / time_of(i);
    if (p >= 1) {
      int n = m ? (int)p : 1;
      if (n > 100000) n = 100000;
      earn(rev_of(i) * n);
      p = m ? p - n : 0;
      if (!m) running[i] = false;
      sale_fx(i);
    }
    V.prog[i] = (float)p;
  }
  inc = 0;
  for (int i = 0; i < NB; i++)
    if (V.owned[i] && (V.mgr >> i & 1)) inc += rev_of(i) / time_of(i);
  /* the golden truck */
  if (!tr_on && scr_game_active && (tr_wait -= dt) <= 0) {
    tr_on = true;
    int dir = rnd() & 1 ? 1 : -1;
    tr_x = dir > 0 ? -30.0f : 340.0f, tr_v = (float)(dir * rr(36, 48));
    say("Golden truck! Press SHIFT", GOLD);
  }
  acc_ms += dt;
  while (acc_ms >= 1000) {
    acc_ms -= 1000;
    V.play_s++;
    for (int k = 0; k < NA; k++)
      if (!(V.ach >> k & 1) && ach_ok(k)) {
        V.ach |= 1u << k;
        char b[48], *o = cat(b, "Achievement: ");
        cat(o, ACH_NAME[k]);
        say(b, RGB(0xFF9F43));
        burst(160, 40, 18, 1);
      }
  }
}

/* ------------------------------------------------------------------ interface layout */
#define HUD_Y 96
#define HUD_H 18
#define LST_Y 114
#define ROW_H 26
#define ROWS 4
#define TAB_Y 218
#define TAB_H 22
#define ROW_W 316
#define UI_BG RGB(0x131822)
#define ROW_BG RGB(0x232B3A)
#define ROW_SEL RGB(0x354463)
#define BAR_BG RGB(0x0F131B)
#define GRAY RGB(0x8A93A6)
#define GREEN RGB(0x2FAE60)
#define MINT RGB(0x7CFFB2)

enum { S_TITLE, S_GAME };
enum { D_NONE, D_PAUSE, D_OPTS, D_HELP, D_FRAN, D_ERASE };
static int scr = S_TITLE, dlg = D_NONE, dsel, dpage;
static int tab, sel[5], top[5], ach_cur;
static color rowbuf[ROW_W * ROW_H];

/* ------------------------------------------------------------------ icons: 24 x 24, drawn on the canvas */
static bool dim;
static color D(color c) { return dim ? mix(c, ROW_BG, 150) : c; }
static void icon(int id) {
  const biz_t *b = id < NB ? &BIZ[id] : &BIZ[0];
  color c1 = D(b->c1), c2 = D(b->c2), W_ = D(WHITE), K = D(RGB(0x2B2F38));
  switch (id) {
    case 0:
      R(3, 21, 18, 2, D(RGB(0xB5762F)));
      R(7, 6, 10, 15, D(RGB(0xE8F4FA))), R(8, 10, 8, 11, c1), R(14, 2, 2, 9, c2);
      disc(6, 18, 3, D(RGB(0xF7DC3A))), disc(6, 18, 1, D(RGB(0xFFF3A0)));
      break;
    case 1:
      R(4, 9, 16, 13, c1);
      for (int k = 0; k < 4; k++) R(3 + k * 5, 4, 5, 5, k & 1 ? W_ : c1);
      R(7, 13, 5, 7, W_), R(13, 13, 5, 7, D(RGB(0xFFD35C))), R(8, 15, 3, 1, K), R(14, 15, 3, 1, K);
      break;
    case 2:
      R(4, 12, 16, 8, c1), R(6, 9, 12, 4, c1), R(8, 7, 8, 3, c1);
      R(8, 11, 2, 4, c2), R(12, 10, 2, 4, c2), R(16, 12, 2, 4, c2);
      R(3, 20, 18, 2, D(RGB(0x7A4A24)));
      break;
    case 3:
      R(5, 9, 12, 12, W_), R(17, 11, 4, 1, W_), R(20, 11, 1, 6, W_), R(17, 16, 4, 1, W_);
      R(7, 10, 8, 3, c2);
      R(8, 2, 1, 5, D(GRAY)), R(11, 1, 1, 6, D(GRAY)), R(14, 2, 1, 5, D(GRAY));
      R(3, 21, 18, 2, c2);
      break;
    case 4:
      disc(12, 14, 8, W_), disc(12, 14, 5, c1), disc(12, 14, 3, D(RGB(0xE8A090)));
      R(1, 6, 1, 14, D(GRAY)), R(22, 6, 1, 14, D(GRAY)), R(0, 6, 3, 1, D(GRAY));
      break;
    case 5:
      R(3, 12, 18, 10, c1);
      R(4, 8, 6, 4, c1), R(12, 8, 6, 4, c1);
      R(5, 3, 3, 9, c2), R(13, 5, 3, 7, c2);
      disc(8, 3, 2, D(RGB(0xD7DCE2))), disc(17, 3, 1, D(RGB(0xD7DCE2)));
      R(5, 15, 3, 3, D(RGB(0xFFD66B))), R(10, 15, 3, 3, D(RGB(0xFFD66B))), R(15, 15, 3, 3, D(RGB(0xFFD66B)));
      break;
    case 6:
      for (int k = 0; k < 5; k++) R(2 + k, 5 + k, 20 - 2 * k, 1, c2);
      R(3, 7, 18, 3, c1);
      for (int k = 0; k < 4; k++) R(4 + k * 5, 10, 3, 9, W_);
      R(2, 19, 20, 3, c1);
      R(10, 3, 4, 3, D(RGB(0xFFF0A0)));
      break;
    case 7:
      R(6, 2, 12, 20, c1), R(5, 1, 14, 2, c2);
      for (int r = 0; r < 4; r++)
        for (int c = 0; c < 2; c++) R(8 + c * 5, 5 + r * 4, 3, 2, D(RGB(0xFFD66B)));
      R(10, 18, 4, 4, K);
      break;
    case 8:
      R(5, 5, 14, 14, c1), R(7, 7, 10, 10, D(RGB(0x1E3F66)));
      for (int k = 0; k < 4; k++) R(7 + k * 3, 2, 1, 3, c2), R(7 + k * 3, 19, 1, 3, c2), R(2, 7 + k * 3, 3, 1, c2), R(19, 7 + k * 3, 3, 1, c2);
      R(9, 11, 6, 2, c2), R(11, 9, 2, 6, c2);
      break;
    case 9:
      R(10, 4, 5, 14, W_), R(11, 1, 3, 3, c2), R(8, 13, 2, 6, c2), R(15, 13, 2, 6, c2);
      R(11, 8, 3, 3, D(RGB(0x6FE3FF)));
      R(10, 19, 5, 3, D(RGB(0xFF9A2E))), R(11, 22, 3, 1, D(RGB(0xFFE27A)));
      break;
    case 10: /* up arrow */
      for (int k = 0; k < 8; k++) R(12 - k, 3 + k, 2 * k + 1, 1, D(GREEN));
      R(9, 11, 6, 10, D(GREEN));
      break;
    case 11: /* a manager */
      disc(12, 7, 4, D(RGB(0xF1C27D)));
      R(7, 12, 10, 10, D(RGB(0x3B4A6B))), R(11, 12, 2, 8, W_), R(12, 13, 1, 6, D(RGB(0xD64545)));
      break;
    case 12: /* star */
      R(10, 1, 4, 22, D(GOLD)), R(1, 9, 22, 5, D(GOLD)), R(5, 5, 14, 13, D(GOLD)), R(9, 9, 6, 5, D(RGB(0xFFF3A0)));
      break;
    default: /* padlock */
      R(6, 11, 12, 10, D(GRAY)), R(8, 5, 2, 7, D(GRAY)), R(14, 5, 2, 7, D(GRAY)), R(8, 4, 8, 2, D(GRAY)), R(11, 15, 2, 3, K);
  }
}

/* ------------------------------------------------------------------ the rows */
typedef struct {
  char name[16], cnt[8], sub[30], hint[12], i1[10], i2[10], b1[14], b2[14];
  uint8_t icon, state; /* state: 0 cannot afford, 1 can, 2 done */
  color acc;
  int bar; /* 0..120, or -1: kept last so a row that only moved its bar is not redrawn */
} item_t;
#define ITEM_KEY ((int)__builtin_offsetof(item_t, bar))
static item_t shown[ROWS];
static bool shown_sel[ROWS], shown_valid[ROWS];
static int shown_top = -1, shown_tab = -1;

static void zero_item(item_t *it) {
  for (unsigned i = 0; i < sizeof *it; i++) ((uint8_t *)it)[i] = 0;
  it->bar = -1, it->acc = GOLD;
}
static void put(char *dst, const char *src, int max) {
  int n = 0;
  while (src[n] && n < max - 1) dst[n] = src[n], n++;
  dst[n] = 0;
}
static int list_len(int t) {
  switch (t) {
    case 0: return NB;
    case 1: return imax(1, ucount);
    case 2: return imax(1, mcount);
    case 3: return 1 + NS;
    default: return 0;
  }
}
static void get_item(int t, int idx, item_t *it) {
  char b[40], *o;
  zero_item(it);
  if (t == 0) {
    int i = idx;
    it->icon = (uint8_t)i, it->acc = BIZ[i].c2;
    if (!unlocked(i)) {
      it->icon = 13, it->state = 0;
      put(it->name, "???", 16);
      o = cat(b, "Own a ");
      cat(o, BIZ[i - 1].name);
      put(it->sub, b, 30);
      put(it->b1, "Locked", 14);
      return;
    }
    put(it->name, BIZ[i].name, 16);
    if (V.owned[i]) o = cat(b, "x"), num(o, V.owned[i]), put(it->cnt, b, 8);
    int q = QTY[V.qty], n = q ? q : max_n(i);
    if (n < 1) n = 1;
    o = cat(b, "Buy x");
    num(o, (uint32_t)n);
    put(it->b1, b, 14);
    dollars(b, cost_n(i, n));
    put(it->b2, b, 14);
    it->state = V.money >= cost_n(i, n);
    double r = V.owned[i] ? rev_of(i) : BIZ[i].rev * mult_all();
    dollars(b, r);
    put(it->i1, b, 10);
    o = fmt(b, time_of(i)), cat(o, "s");
    put(it->i2, b, 10);
    if (V.owned[i]) {
      bool m = V.mgr >> i & 1;
      if (!m && !running[i]) put(it->hint, "EXE: run", 12), it->bar = 0;
      else it->bar = clampi((int)(V.prog[i] * 120), 0, 120);
    } else {
      it->bar = 0;
    }
    return;
  }
  if (t == 1) {
    if (!ucount) {
      it->icon = 10, put(it->name, "No upgrades yet", 16), put(it->sub, "Grow your businesses", 30);
      it->state = 2;
      return;
    }
    int u = ulist[idx];
    if (u < NB * 3) {
      int i = u / 3;
      it->icon = (uint8_t)i, it->acc = BIZ[i].c2;
      put(it->name, BIZ[i].name, 16);
      put(it->cnt, "x3", 8);
      o = cat(b, UTXT[u % 3]);
      cat(o, ": profit x3");
      put(it->sub, b, 30);
    } else {
      const gup_t *g = &GUP[u - NB * 3];
      it->icon = 10;
      put(it->name, g->name, 16);
      o = cat(b, "All profit x");
      num(o, (uint32_t)g->mult);
      put(it->sub, b, 30);
    }
    put(it->b1, "Buy", 14);
    dollars(b, up_cost(u));
    put(it->b2, b, 14);
    it->state = V.money >= up_cost(u);
    return;
  }
  if (t == 2) {
    if (!mcount) {
      it->icon = 11, put(it->name, "No managers yet", 16), put(it->sub, "Buy a business first", 30);
      it->state = 2;
      return;
    }
    int i = mlist[idx];
    it->icon = 11, it->acc = BIZ[i].c2;
    put(it->name, BIZ[i].boss, 16);
    o = cat(b, "Runs your ");
    cat(o, BIZ[i].name);
    put(it->sub, b, 30);
    if (V.mgr >> i & 1) {
      put(it->b1, "Hired", 14), it->state = 2;
    } else {
      put(it->b1, "Hire", 14);
      dollars(b, BIZ[i].mgr);
      put(it->b2, b, 14);
      it->state = V.money >= BIZ[i].mgr;
    }
    return;
  }
  /* stars */
  it->icon = 12;
  if (idx == 0) {
    uint32_t g = stars_gain();
    put(it->name, "Franchise", 16);
    o = cat(b, "Reset: +");
    o = num(o, g);
    cat(o, g == 1 ? " star" : " stars");
    put(it->sub, g ? b : "Earn $1T to start", 30);
    put(it->b1, g ? "Go!" : "Not yet", 14);
    it->state = g > 0;
    return;
  }
  int s = idx - 1;
  put(it->name, SUP[s].name, 16);
  o = cat(b, "Lv "), o = num(o, V.su[s]), o = cat(o, "/"), num(o, SUP[s].max), put(it->cnt, b, 8);
  put(it->sub, SUP[s].desc, 30);
  if (V.su[s] >= SUP[s].max) {
    put(it->b1, "Maxed", 14), it->state = 2;
  } else {
    uint32_t c = SUP[s].cost[V.su[s]];
    put(it->b1, "Buy", 14);
    o = num(b, c), cat(o, " stars");
    put(it->b2, b, 14);
    it->state = V.stars >= c;
  }
}

static color btn_col(const item_t *it) { return it->state == 1 ? GREEN : it->state == 2 ? RGB(0x3A4150) : RGB(0x4A3A42); }
static int ry(int slot) { return LST_Y + slot * ROW_H; }
static void bar_fill(int slot, int from, int to, color c) {
  if (to > from) fill(2 + 29 + from, ry(slot) + 16, to - from, 8, c);
}
static void row_text(int slot, const item_t *it, bool sel_) {
  color bg = sel_ ? ROW_SEL : ROW_BG, bc = btn_col(it);
  int y = ry(slot);
  text(it->name, 2 + 28, y + 1, WHITE, bg);
  if (it->cnt[0]) text(it->cnt, 2 + 28 + slen(it->name) * 7 + 6, y + 1, GOLD, bg);
  if (it->bar < 0 && it->sub[0]) text(it->sub, 2 + 28, y + 13, GRAY, bg);
  if (it->hint[0]) text(it->hint, 2 + 33, y + 13, GRAY, BAR_BG);
  if (it->i1[0]) text(it->i1, 2 + 154, y + 1, MINT, bg), text(it->i2, 2 + 154, y + 13, GRAY, bg);
  color fg = it->state == 0 ? RGB(0xC7B0B8) : WHITE;
  if (it->b1[0]) text(it->b1, 2 + 212 + (102 - slen(it->b1) * 7) / 2, y + (it->b2[0] ? 1 : 7), fg, bc);
  if (it->b2[0]) text(it->b2, 2 + 212 + (102 - slen(it->b2) * 7) / 2, y + 13, fg, bc);
}
static void draw_row(int slot, const item_t *it, bool sel_) {
  color bg = sel_ ? ROW_SEL : ROW_BG, bc = btn_col(it);
  cv = rowbuf, cvw = ROW_W, cvh = ROW_H, cox = coy = 0;
  R(0, 0, ROW_W, ROW_H, bg);
  if (sel_) R(0, 0, ROW_W, 1, GOLD), R(0, ROW_H - 1, ROW_W, 1, GOLD), R(0, 0, 1, ROW_H, GOLD), R(ROW_W - 1, 0, 1, ROW_H, GOLD);
  cox = -2, coy = -1, dim = it->icon == 13;
  icon(it->icon);
  cox = coy = 0, dim = false;
  if (it->bar >= 0) {
    R(28, 15, 122, 10, RGB(0x4B566E)), R(29, 16, 120, 8, BAR_BG);
    if (!it->hint[0]) R(29, 16, it->bar, 8, it->acc), R(29, 16, it->bar, 1, mix(it->acc, WHITE, 90));
  }
  if (it->b1[0]) {
    color edge = mix(bc, WHITE, 70), shade = mix(bc, BLACK, 90);
    R(212, 1, 102, 24, bc), R(212, 1, 102, 1, edge), R(212, 24, 102, 1, shade), R(212, 1, 1, 24, edge), R(313, 1, 1, 24, shade);
  }
  eadk_display_push_rect((eadk_rect_t){2, (uint16_t)ry(slot), ROW_W, ROW_H}, rowbuf);
  cv = band, cvw = SW, cvh = BH; /* back to the city canvas */
  row_text(slot, it, sel_);
}
static bool same_item(const item_t *a, const item_t *b) {
  const uint8_t *p = (const uint8_t *)a, *q = (const uint8_t *)b;
  for (int i = 0; i < ITEM_KEY; i++)
    if (p[i] != q[i]) return false;
  return true;
}
static void refresh_rows(bool force) {
  if (tab == 1) build_ulist();
  if (tab == 2) build_mlist();
  int len = list_len(tab);
  sel[tab] = clampi(sel[tab], 0, imax(0, len - 1));
  if (sel[tab] < top[tab]) top[tab] = sel[tab];
  if (sel[tab] >= top[tab] + ROWS) top[tab] = sel[tab] - ROWS + 1;
  bool all = force || shown_tab != tab || shown_top != top[tab];
  for (int s = 0; s < ROWS; s++) {
    int idx = top[tab] + s;
    if (idx >= len) {
      if (all || shown_valid[s]) fill(0, ry(s), 320, ROW_H, UI_BG);
      shown_valid[s] = false;
      continue;
    }
    item_t it;
    get_item(tab, idx, &it);
    bool sl = idx == sel[tab];
    if (all || !shown_valid[s] || sl != shown_sel[s] || !same_item(&it, &shown[s])) {
      draw_row(s, &it, sl);
    } else if (it.bar >= 0 && !it.hint[0] && it.bar != shown[s].bar) {
      int a = shown[s].bar, b = it.bar;
      if (b > a) bar_fill(s, a, b, it.acc);
      else bar_fill(s, 0, 120, BAR_BG), bar_fill(s, 0, b, it.acc);
    }
    shown[s] = it, shown_sel[s] = sl, shown_valid[s] = true;
  }
  shown_tab = tab, shown_top = top[tab];
}

/* ------------------------------------------------------------------ the money bar */
static char hud_m[20], hud_c[24], hud_i[24], hud_t[48];
static int hud_mode = -1;
static void draw_hud(bool force) {
  char m[20] = {0}, *o = dollars(m, V.money);
  while (o < m + 14) *o++ = ' ';
  *o = 0;
  bool cm = force;
  for (int i = 0; i < 16 && !cm; i++) cm = m[i] != hud_m[i];
  if (cm) {
    bigtext(m, 4, HUD_Y, GOLD, UI_BG);
    for (int i = 0; i < 16; i++) hud_m[i] = m[i];
  }
  char c[24] = {0}, ic[24] = {0};
  int mode = 0;
  if (toast_ms > 0) mode = 1;
  else if (tr_on) mode = 2 + (tick_ms / 300 & 1);
  else {
    o = c;
    if (boost_ms > 0) o = cat(o, "x7 "), o = num(o, (uint32_t)(boost_ms / 1000 + 1)), o = cat(o, "s ");
    if (rush_ms > 0) o = cat(o, "3x "), o = num(o, (uint32_t)(rush_ms / 1000 + 1)), o = cat(o, "s ");
    if (V.stars_total + V.stars) o = cat(o, "*"), o = num(o, V.stars);
    o = dollars(ic, inc), cat(o, "/s");
  }
  bool ch = force || mode != hud_mode;
  for (int i = 0; i < 24 && !ch; i++) ch = c[i] != hud_c[i] || ic[i] != hud_i[i];
  if (mode == 1)
    for (int i = 0; i < 48 && !ch; i++) ch = toast[i] != hud_t[i];
  if (!ch) return;
  fill(150, HUD_Y, 170, HUD_H, UI_BG);
  if (mode == 1) {
    text(toast, imax(154, 318 - slen(toast) * 7), HUD_Y + 3, toast_col, UI_BG);
    for (int i = 0; i < 48; i++) hud_t[i] = toast[i];
  } else if (mode >= 2) {
    const char *s = "TRUCK! Press SHIFT";
    text(s, 318 - slen(s) * 7, HUD_Y + 3, mode == 2 ? GOLD : WHITE, UI_BG);
  } else {
    if (c[0]) text(c, 154, HUD_Y + 3, RGB(0xFF9F43), UI_BG);
    text(ic, 318 - slen(ic) * 7, HUD_Y + 3, MINT, UI_BG);
  }
  hud_mode = mode;
  for (int i = 0; i < 24; i++) hud_c[i] = c[i], hud_i[i] = ic[i];
}

/* ------------------------------------------------------------------ tabs */
static const char *const TABN[5] = {"Biz", "Upgrades", "Managers", "Stars", "Stats"};
static int tab_flags(void) {
  int f = 0;
  for (int i = 0; i < NB; i++)
    if (unlocked(i) && V.money >= unit_cost(i)) f |= 1;
  build_ulist();
  for (int k = 0; k < ucount; k++)
    if (V.money >= up_cost(ulist[k])) f |= 2;
  for (int i = 0; i < NB; i++)
    if (V.owned[i] && !(V.mgr >> i & 1) && V.money >= BIZ[i].mgr) f |= 4;
  if (stars_gain()) f |= 8;
  for (int s = 0; s < NS; s++)
    if (V.su[s] < SUP[s].max && V.stars >= SUP[s].cost[V.su[s]]) f |= 8;
  return f;
}
static int tabs_shown = -1;
static void draw_tabs(bool force) {
  int f = tab_flags(), sig = f | tab << 8;
  if (!force && sig == tabs_shown) return;
  tabs_shown = sig;
  for (int t = 0; t < 5; t++) {
    color bg = t == tab ? RGB(0x354463) : RGB(0x1B2230);
    fill(t * 64, TAB_Y, 64, TAB_H, bg);
    if (t == tab) fill(t * 64, TAB_Y, 64, 2, GOLD);
    text(TABN[t], t * 64 + (64 - slen(TABN[t]) * 7) / 2, TAB_Y + 5, t == tab ? WHITE : GRAY, bg);
    if (f >> t & 1) fill(t * 64 + 54, TAB_Y + 4, 6, 6, GOLD);
    if (t) fill(t * 64, TAB_Y + 2, 1, TAB_H - 2, UI_BG);
  }
}

/* ------------------------------------------------------------------ the stats tab */
static void draw_stats(void) {
  fill(0, LST_Y, 320, 104, UI_BG);
  char b[48], *o;
  static const char *const lab[5] = {"Earned this run", "Earned in total", "Play time", "Stars", "Achievements"};
  for (int k = 0; k < 5; k++) {
    int y = LST_Y + 2 + k * 14;
    text(lab[k], 10, y, GRAY, UI_BG);
    if (k == 0) dollars(b, V.run);
    else if (k == 1) dollars(b, V.life);
    else if (k == 2) o = hms(b, V.play_s), o = cat(o, "  "), o = num(o, V.franchises), cat(o, " franchises");
    else if (k == 3) o = num(b, V.stars), o = cat(o, " / "), o = num(o, V.stars_total), cat(o, " earned");
    else o = num(b, (uint32_t)ach_count()), cat(o, " / 20");
    text(b, 130, y, WHITE, UI_BG);
  }
  for (int k = 0; k < NA; k++) {
    int x = 10 + k * 15, y = LST_Y + 76;
    bool got = V.ach >> k & 1;
    fill(x, y, 13, 13, k == ach_cur ? WHITE : UI_BG);
    fill(x + 1, y + 1, 11, 11, got ? GOLD : RGB(0x2B3345));
    if (got) fill(x + 3, y + 3, 7, 4, RGB(0xFFF3A0)), fill(x + 5, y + 7, 3, 3, RGB(0xE0A020));
    else fill(x + 5, y + 4, 3, 5, RGB(0x4B566E));
  }
  o = cat(b, ACH_NAME[ach_cur]);
  o = cat(o, ": ");
  cat(o, ACH_DESC[ach_cur]);
  text(b, 4, LST_Y + 92, V.ach >> ach_cur & 1 ? GOLD : GRAY, UI_BG);
}

/* ------------------------------------------------------------------ menus and dialogs */
#define PANEL RGB(0x1B2230)
#define DX 24
#define DW 272
#define DY 98
#define DH 118
static const char *dtitle;
static char dline[5][44], ditem[6][30];
static int dnl, dni;
static void dset(int i, const char *s) { put(ditem[i], s, 30); }
static void dlg_prepare(void) {
  char b[44], *o;
  dnl = dni = 0;
  switch (dlg) {
    case D_PAUSE:
      dtitle = "Paused";
      dset(0, "Resume"), dset(1, "Options"), dset(2, "How to play"), dset(3, "Save & quit"), dni = 4;
      break;
    case D_OPTS:
      dtitle = "Options";
      o = cat(b, "Particles: "), cat(o, V.opts & O_PART ? "On" : "Off"), dset(0, b);
      o = cat(b, "Rain: "), cat(o, V.opts & O_RAIN ? "On" : "Off"), dset(1, b);
      o = cat(b, "Day & night: "), cat(o, V.opts & O_DAY ? "On" : "Off"), dset(2, b);
      o = cat(b, "Game speed: x"), num(o, (uint32_t)SPD[V.spd]), dset(3, b);
      dset(4, "Erase everything..."), dset(5, "Back");
      dni = 6;
      break;
    case D_FRAN: {
      uint32_t g = stars_gain();
      dtitle = "Franchise?";
      o = cat(b, "Reset your empire for +");
      o = num(o, g), cat(o, g == 1 ? " star" : " stars");
      put(dline[0], b, 44);
      put(dline[1], "Every star adds +2% profit for good.", 44);
      put(dline[2], "Stars, achievements and records stay.", 44);
      dnl = 3;
      dset(0, "Yes, franchise!"), dset(1, "Not yet"), dni = 2;
      break;
    }
    case D_ERASE:
      dtitle = "Erase everything?";
      put(dline[0], "Your empire, stars and achievements", 44);
      put(dline[1], "will be gone for good.", 44);
      dnl = 2;
      dset(0, "Cancel"), dset(1, "Erase"), dni = 2;
      break;
    case D_HELP: {
      static const char *const pg[3][5] = {
        {"Buy businesses with OK, run them", "with EXE: each sale pays you.", "Hire managers to run them for you.", "Upgrades & milestones boost profit.", "Catch the golden truck with SHIFT!"},
        {"Up / Down   pick a line", "Left / Right or 1-5   change tab", "OK  buy       EXE  run a sale", "Backspace   x1 / x10 / x100 / Max", "Shift  catch truck     Back  menu"},
        {"Earn $1T in total, then Franchise:", "your empire resets, you get Stars.", "Each star gives +2% profit forever,", "and the Star tab sells big upgrades.", "Every run goes faster than the last."},
      };
      dtitle = dpage == 0 ? "How to play  1/3" : dpage == 1 ? "Keys  2/3" : "Franchise  3/3";
      for (int k = 0; k < 5; k++) put(dline[k], pg[dpage][k], 44);
      dnl = 5;
      dset(0, dpage == 2 ? "Close" : "Next"), dni = 1;
      break;
    }
    default: /* title */
      dtitle = "";
      dset(0, V.started ? "Continue" : "Play"), dset(1, "How to play"), dset(2, "Options"), dset(3, "Quit game"), dni = 4;
  }
}
static void draw_dialog(void) {
  dlg_prepare();
  bool title = scr == S_TITLE && dlg == D_NONE;
  fill(DX, DY, DW, DH, GOLD), fill(DX + 2, DY + 2, DW - 4, DH - 4, PANEL);
  int y = DY + 5;
  if (title) {
    bigtext("NumTycoon", 160 - 45 + 1, y + 1, RGB(0x6B4A00), PANEL);
    bigtext("NumTycoon", 160 - 45, y, GOLD, PANEL);
    y += 22;
    const char *sub = "Build your business empire";
    text(sub, 160 - slen(sub) * 7 / 2, y, GRAY, PANEL);
    y += 14;
  } else {
    text(dtitle, 160 - slen(dtitle) * 7 / 2, y, GOLD, PANEL);
    y += 17;
  }
  for (int k = 0; k < dnl; k++, y += 13) text(dline[k], 160 - slen(dline[k]) * 7 / 2, y, WHITE, PANEL);
  y += 2;
  int ih = dni > 4 ? 15 : 17;
  for (int k = 0; k < dni; k++, y += ih) {
    bool s = k == dsel;
    color bg = s ? ROW_SEL : PANEL;
    fill(DX + 14, y, DW - 28, ih - 1, bg);
    text(ditem[k], 160 - slen(ditem[k]) * 7 / 2, y + (ih - 14) / 2, s ? GOLD : WHITE, bg);
  }
}

/* ------------------------------------------------------------------ screens and input */
static bool leave;
static int dstack[4], dsp;
static void ui_all(void) {
  fill(0, HUD_Y, 320, 144, UI_BG);
  hud_mode = -1, shown_tab = -1, tabs_shown = -1;
  draw_hud(true);
  if (tab == 4) draw_stats();
  else refresh_rows(true);
  draw_tabs(true);
}
static void show_title(void) {
  scr = S_TITLE, dlg = D_NONE, dsp = 0, dsel = 0, demo = true, scr_game_active = false;
  fill(0, HUD_Y, 320, 144, UI_BG);
  draw_dialog();
}
static void open_dlg(int d) {
  if (dsp < 4) dstack[dsp++] = dlg;
  dlg = d, dsel = 0, dpage = 0;
  fill(0, HUD_Y, 320, 144, UI_BG);
  draw_dialog();
}
static void close_dlg(void) {
  dlg = dsp ? dstack[--dsp] : D_NONE;
  dsel = 0;
  if (scr == S_TITLE) {
    if (dlg == D_NONE) fill(0, HUD_Y, 320, 144, UI_BG);
    draw_dialog();
  } else if (dlg == D_NONE) {
    ui_all();
  } else {
    fill(0, HUD_Y, 320, 144, UI_BG);
    draw_dialog();
  }
}
static void start_game(void) {
  scr = S_GAME, dlg = D_NONE, dsp = 0, demo = false, scr_game_active = true;
  if (!V.started) V.started = 1, say("OK buys, EXE runs a sale!", MINT);
  ui_all();
}
static void erase_all(void) {
  fresh();
  for (int i = 0; i < NB; i++) running[i] = false, anim_t[i] = 60000;
  boost_ms = rush_ms = 0, tr_on = false, tab = 0;
  for (int i = 0; i < 5; i++) sel[i] = top[i] = 0;
  save();
  show_title();
}
static void goto_tab(int t) {
  t = (t + 5) % 5;
  if (t == tab) return;
  tab = t;
  V.tab = (uint8_t)tab;
  if (tab == 4) fill(0, LST_Y, 320, 104, UI_BG), draw_stats();
  else refresh_rows(true);
  draw_tabs(false);
}

/* a key press in a dialog or menu */
static void dialog_input(int hit, int ud, int lr) {
  bool ok = hit & K_OK, back = hit & K_BACK;
  bool title = scr == S_TITLE && dlg == D_NONE;
  if (dlg == D_HELP) {
    if (lr < 0 && dpage > 0) dpage--, draw_dialog();
    else if ((ok || lr > 0) && dpage < 2) dpage++, draw_dialog();
    else if (back || ok) close_dlg();
    return;
  }
  dlg_prepare();
  if (ud) {
    dsel = (dsel + ud + dni) % dni;
    draw_dialog();
  }
  if (dlg == D_OPTS && lr && dsel == 3) V.spd = (uint8_t)((V.spd + lr + 3) % 3), draw_dialog();
  if (back && !title) {
    close_dlg();
    return;
  }
  if (!ok) return;
  if (title) {
    if (dsel == 0) start_game();
    else if (dsel == 1) open_dlg(D_HELP);
    else if (dsel == 2) open_dlg(D_OPTS);
    else leave = true;
  } else if (dlg == D_PAUSE) {
    if (dsel == 0) close_dlg();
    else if (dsel == 1) open_dlg(D_OPTS);
    else if (dsel == 2) open_dlg(D_HELP);
    else save(), leave = true;
  } else if (dlg == D_OPTS) {
    if (dsel < 3) V.opts ^= (uint8_t)(1 << dsel), draw_dialog();
    else if (dsel == 3) V.spd = (uint8_t)((V.spd + 1) % 3), draw_dialog();
    else if (dsel == 4) open_dlg(D_ERASE);
    else close_dlg();
  } else if (dlg == D_FRAN) {
    if (dsel == 0) franchise();
    close_dlg();
  } else if (dlg == D_ERASE) {
    if (dsel == 1) erase_all();
    else close_dlg();
  }
}

static void game_input(int hit, int ud, int lr) {
  if (hit & K_BACK) return open_dlg(D_PAUSE);
  if (hit & K_CATCH) catch_truck();
  for (int t = 0; t < 5; t++)
    if (hit & (K_T1 << t)) goto_tab(t);
  if (lr) goto_tab(tab + lr);
  if (tab == 4) {
    if (ud) ach_cur = (ach_cur + ud + NA) % NA, draw_stats();
    if (hit & K_OK) ach_cur = (ach_cur + 1) % NA, draw_stats();
    return;
  }
  int len = list_len(tab);
  if (ud) sel[tab] = clampi(sel[tab] + ud, 0, imax(0, len - 1));
  if (hit & K_QTY) V.qty = (uint8_t)((V.qty + 1) & 3);
  int s = sel[tab];
  if ((hit & K_RUN) && tab == 0) run_biz(s);
  if (hit & K_OK) {
    if (tab == 0) buy_biz(s);
    else if (tab == 1 && ucount) buy_up(ulist[s]);
    else if (tab == 2 && mcount) hire(mlist[s]);
    else if (tab == 3) {
      if (s == 0) {
        if (stars_gain()) open_dlg(D_FRAN);
      } else {
        buy_star(s - 1);
      }
    }
  }
}

int main(void) {
  np_app_begin();
  load();
  now = (uint32_t)eadk_timing_millis();
  seed ^= now;
  scene_init();
  tab = V.tab % 5;
  for (int i = 0; i < NB; i++) {
    anim_t[i] = 60000;
    running[i] = V.prog[i] > 0 && !(V.mgr >> i & 1);
  }
  show_title();
  int held = 0, repeats = 0, arrows = K_UP | K_DOWN | K_OK, stuck = K_HOME;
  bool armed = false; /* once every key held from before is let go */
  uint32_t repeat_at = 0, last = now, saved_at = now, frame = 0, stat_at = 0;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    int dt = (int)(now - last);
    last = now;
    if (dt > 100) dt = 100;
    int k = keys(), hit = armed ? k & ~held : 0;
    stuck &= k; /* Home quits unless held since the start, whatever else is held */
    if (k & ~stuck & K_HOME) break;
    if (leave) break;
    if (!armed) armed = !k, k = 0;
    bool can_repeat = scr == S_GAME && dlg == D_NONE;
    if (hit & arrows) repeat_at = now + 320, repeats = 0;
    else if (can_repeat && (k & arrows) && (int32_t)(now - repeat_at) >= 0) hit |= k & arrows, repeats++, repeat_at = now + (repeats > 8 ? 45 : 90);
    held = k;
    int ud = !!(hit & K_DOWN) - !!(hit & K_UP), lr = !!(hit & K_RIGHT) - !!(hit & K_LEFT);
    if (scr == S_TITLE || dlg != D_NONE) dialog_input(hit, ud, lr);
    else game_input(hit, ud, lr);

    if (scr == S_GAME) sim(dt);
    scene_update(dt);
    eadk_display_wait_for_vblank();
    if (!(++frame & 1)) scene_render();
    if (scr == S_GAME && dlg == D_NONE) {
      draw_hud(hud_dirty);
      hud_dirty = false;
      if (tab == 4) {
        if (now - stat_at > 1000) stat_at = now, draw_stats();
      } else {
        refresh_rows(false);
      }
      draw_tabs(false);
    }
    if (scr == S_GAME && now - saved_at > 15000) saved_at = now, save();
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
  save();
  return np_app_end();
}
