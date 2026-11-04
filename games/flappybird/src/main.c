/* Flappy Bird: Dong Nguyen's game, with the settings and random events of
 * Tatone26's version in All the Apps (wind, gravity changes, moving pipes,
 * narrow and wide gaps...).
 *
 * The background never moves, so a frame only repaints what changed: the
 * pipe columns whose colour changed, the bird's box, the ground's stripes, a
 * few streaks. Each region is composed in a small buffer from the whole scene
 * (the skyline comes from a few heights per column, then the pipes, the bird
 * and the text on top) and pushed once, so nothing flickers. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_text.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Flappy Bird";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

#define W 320
#define H 240
#define GY 200 /* the ground's top */
#define BX 96  /* the bird's centre, across */
#define LW 38  /* a pipe's cap (the shaft is 2 pixels narrower each side) */
#define LH 16
#define NP 8
#define NS 24 /* wind streaks */
#define BUFN (W * 24)
#define OFF (-999)
typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define KEY(k) (1ull << (k))
#define WHITE 0xFFFF
#define DARK RGB(0x543847)
#define ORANGE RGB(0xE86101)
#define BEIGE RGB(0xDED895)
#define ADV(k) (5 * (k) + ((k) + 1) / 2) /* letter advance */

static color buf[BUFN];
static int rx, ry, rw, rh; /* the region being composed */

static uint32_t seed = 0x2545F491;
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static int rr(int a, int b) { return a + (int)(rnd() % (uint32_t)(b - a + 1)); } /* a..b */
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int clamp(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int16_t sn[256]; /* sine, -256..256 */
#define SIN(a) sn[(a) & 255]
#define COS(a) sn[((a) + 64) & 255]

/* f over b; a from 0 (all b) to 32 (all f) */
#ifdef __TICE__ /* the TI-84 Plus CE: its own mixing (the same colours) */
#include "../../../platform/ti84ce/ce_draw.h"
__attribute__((naked)) static color ce_mix(color a, color b, unsigned t) { __asm__(CE_MIX_ASM); }
#endif
static color mix(color f, color b, int a) {
#ifdef __TICE__
  return ce_mix(b, f, (unsigned)a);
#else
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color)(y | y >> 16);
#endif
}

/* ------------------------------------------------------------------ drawing */
static void shade(int x, int y, int w, int h, color c, int a) {
  int x0 = imax(x, rx), x1 = imin(x + w, rx + rw), y0 = imax(y, ry), y1 = imin(y + h, ry + rh);
  if (x0 >= x1) return;
  for (int j = y0; j < y1; j++)
    for (color *p = buf + (j - ry) * rw + (x0 - rx), *e = p + (x1 - x0); p < e; p++) *p = a >= 32 ? c : mix(c, *p, a);
}
static int ga = 32; /* how opaque fill() paints, for things fading out */
static void fill(int x, int y, int w, int h, color c) { shade(x, y, w, h, c, ga); }
static void rrect(int x, int y, int w, int h, color c) { /* corners cut */
  fill(x + 1, y, w - 2, h, c);
  fill(x, y + 1, w, h - 2, c);
}
/* the beige board of the original: dark outline, light rim, a shadow inside */
static void board(int x, int y, int w, int h) {
  rrect(x, y, w, h, DARK);
  rrect(x + 1, y + 1, w - 2, h - 2, RGB(0xFCF8CF));
  fill(x + 3, y + 3, w - 6, h - 6, RGB(0xCFC286));
  fill(x + 3, y + 3, w - 6, h - 8, BEIGE);
}

/* ASCII 32..122, columns of 8 bits, top first; the last row is for g, j, p, q, y */
static const uint8_t font[91][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x41,0x41,0x41,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x41,0x49,0x3A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x18,0xA4,0xA4,0xA4,0x7C},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x40,0x80,0x84,0x7D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0xFC,0x24,0x24,0x24,0x18},{0x18,0x24,0x24,0x18,0xFC},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x1C,0xA0,0xA0,0xA0,0x7C},{0x44,0x64,0x54,0x4C,0x44},
};
static bool fpix(char ch, int i, int j) { /* is pixel (i, j) of a letter set */
  unsigned g = (uint8_t)ch - 32u;
  return g < 91 && (unsigned)i < 5 && (font[g][i] >> j & 1);
}
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
#if NP_TEXT_EXTRA
/* Other languages: an accented letter is the font's letter with np_accent() in the two rows over
   lowercase letters (an i without its dot; a capital before a lowercase letter as a small
   capital, its rows 0, 2, 3, 4 and 6; among capitals, above it), a cedilla under it. Chinese
   comes from the 12-pixel font, 12 pixels tall up to size 2, then 24. */
static color pen_c;
static void pen_px(int x, int y, void *ctx) { fill(x, y, 1, 1, pen_c); }
static int xk(int k) { return (7 * k + 6) / 12; }
static char plain(uint32_t cp) { /* the letter without its accent (0: none such) */
  char b = (char)cp, b2;
  if (cp >= 0x80) np_latin(cp, &b, &b2);
  return b;
}
static void glyph(char b, int x, int y, int k, int j0, bool small, void (*plot)(int x, int y, void *ctx)) {
  for (int i = 0; i < 5; i++)
    for (int j = j0; j < 8; j++)
      if (fpix(b, i, small ? "\0\0\0\2\3\4\6\7"[j] : j))
        for (int a = 0; a < k * k; a++) plot(x + i * k + a % k, y + j * k + a / k, 0);
}
/* draws (with plot, or only measures) the next letter of *s at x; returns its advance */
static int letter(const char **s, int x, int y, int k, void (*plot)(int x, int y, void *ctx)) {
  uint32_t cp = np_utf8(s);
  char b = (char)cp, b2 = 0;
  int acc = 0;
  if (cp >= 0x80 && (acc = np_latin(cp, &b, &b2), !b)) {
    if (plot) np_xdraw(cp, x, y + (7 * k - 12 * xk(k)) / 2, xk(k), plot, 0);
    return np_xadvance(cp, xk(k));
  }
  if (plot) {
    const char *n = *s;
    char nb = *n ? plain(np_utf8(&n)) : 0;
    bool over = acc && acc != NP_ACC_CEDIL, cap = b >= 'A' && b <= 'Z', small = over && cap && nb >= 'a' && nb <= 'z';
    glyph(b, x, y, k, over && (!cap || small) ? 2 : 0, small, plot);
    if (over && (!cap || small) && plot != pen_px && k > 1) /* (in big lettering: a size smaller, 2 pixels off the letter) */
      np_accent(acc, x + k / 2 + 1, y + 2 * k - 2 * (k - 1) - 2, k - 1, plot, 0);
    else
      np_accent(acc, x, acc == NP_ACC_CEDIL ? y + 7 * k : over && cap && !small ? y - 3 * k : y, k, plot, 0);
    if (b2) glyph(b2, x + ADV(k), y, k, 0, false, plot);
  }
  return (b2 ? 2 : 1) * ADV(k);
}
static int tw(const char *s, int k) {
  int w = 5 * k - ADV(k);
  while (*s) w += letter(&s, 0, 0, k, 0);
  return w;
}
static void text(const char *s, int x, int y, int k, color c) {
  if (y - 3 * k >= ry + rh || y + 10 * k <= ry) return;
  pen_c = c;
  while (*s) x += letter(&s, x, y, k, pen_px);
}
#else
static int tw(const char *s, int k) { return slen(s) * ADV(k) - ADV(k) + 5 * k; }
static void text(const char *s, int x, int y, int k, color c) {
  if (y >= ry + rh || y + 8 * k <= ry) return;
  for (; *s; s++, x += ADV(k))
    for (int i = 0; i < 5; i++)
      for (int j = 0; j < 8; j++)
        if (fpix(*s, i, j)) fill(x + i * k, y + j * k, k, k, c);
}
#endif
static void ctext(const char *s, int cx, int y, int k, color c) { text(s, cx - tw(s, k) / 2, y, k, c); }
static void otext(const char *s, int x, int y, int k, color c) { /* with a dark outline */
  for (int d = 0; d < 9; d++) text(s, x + d % 3 - 1, y + d / 3 - 1, k, DARK);
  text(s, x, y, k, c);
}
static char *num(char *o, int v) {
  char t[8];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

/* the score's bold digits, 5x7 ("1" is 3 wide), a bit per column */
static const uint8_t digs[10][7] = {
  {0x0E,0x1B,0x1B,0x1B,0x1B,0x1B,0x0E},{0x06,0x07,0x06,0x06,0x06,0x06,0x06},{0x0E,0x1B,0x18,0x0C,0x06,0x03,0x1F},
  {0x0E,0x1B,0x18,0x0C,0x18,0x1B,0x0E},{0x1B,0x1B,0x1B,0x1F,0x18,0x18,0x18},{0x1F,0x03,0x0F,0x18,0x18,0x1B,0x0E},
  {0x0E,0x1B,0x03,0x0F,0x1B,0x1B,0x0E},{0x1F,0x18,0x0C,0x0C,0x06,0x06,0x06},{0x0E,0x1B,0x1B,0x0E,0x1B,0x1B,0x0E},
  {0x0E,0x1B,0x1B,0x1E,0x18,0x1B,0x0E},
};
static int bigw(const char *s, int k) {
  int w = -k;
  for (; *s; s++) w += (*s == '1' ? 4 : 6) * k;
  return w;
}
/* white digits with a black outline, from x (or centred on x when c) */
static void big(int n, int x, int y, int k, bool c) {
  char s[8];
  num(s, n);
  int o = k > 2 ? 2 : 1;
  if (c) x -= bigw(s, k) / 2;
  if (y - o >= ry + rh || y + 7 * k + o <= ry) return;
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0, px = x; s[i]; px += (s[i++] == '1' ? 4 : 6) * k)
      for (int r = 0; r < 7; r++)
        for (int b = 0; b < 5; b++)
          if (digs[s[i] - '0'][r] >> b & 1) {
            if (pass) fill(px + b * k, y + r * k, k, k, WHITE);
            else fill(px + b * k - o, y + r * k - o, k + 2 * o, k + 2 * o, 0);
          }
}

/* big lettering ("Get Ready!", "Game Over"...): the letters scaled up, a white
   ring and a dark ring around them, taller below; made once, 2 bits a pixel */
#define LMW 232
#define LMH 42
static uint8_t lmap[LMW * LMH / 4];
static int lw, lh;
static color lfill[LMH];
#if NP_TEXT_EXTRA
/* (other languages: the letters drawn first into a mask, a bit a pixel, after the distances) */
static uint8_t *lmask;
static int lwide;
static void mask_px(int x, int y, void *ctx) {
  for (int i = 0; i < lwide; i++, x++) /* strokes widened to the right (not a Chinese letter's) */
    if ((unsigned)x < (unsigned)lw && (unsigned)y < (unsigned)lh) lmask[(y * lw + x) >> 3] |= (uint8_t)(1 << ((y * lw + x) & 7));
}
#endif
static void letters(const char *s, int k, int drop, color top, color bot) {
#if NP_TEXT_EXTRA
  const int p = 3, R = p + drop + 1, a = ADV(k) + 1;
  lw = 5 * k + 1 + 2 * p - a, lh = 8 * k + 2 * p + drop;
  for (const char *t = s; *t;) { /* (a Chinese letter a pixel apart too) */
    int d = letter(&t, 0, 0, k, 0);
    lw += d % ADV(k) ? d + 1 : d / ADV(k) * a;
  }
  lw = imin(lw, LMW);
  lmask = (uint8_t *)buf + LMW * LMH;
  for (int i = 0; i < (lw * lh + 7) >> 3; i++) lmask[i] = 0;
  for (int x = p; *s;) {
    const char *t = s;
    uint32_t cp = np_utf8(&t);
    lwide = cp < 0x80 || plain(cp) ? 2 : 1;
    int d = letter(&s, x, p, k, mask_px);
    x += d % ADV(k) ? d + 1 : d / ADV(k) * a;
  }
#else
  const int p = 3, R = p + drop + 1, n = slen(s), a = ADV(k) + 1; /* strokes a pixel bolder */
  lw = n * a - a + 5 * k + 1 + 2 * p, lh = 8 * k + 2 * p + drop;
#endif
  uint8_t *hd = (uint8_t *)buf; /* distance to the nearest letter pixel in the row */
  for (int y = 0; y < lh; y++) {
    uint8_t *h = hd + y * lw;
    int d = R;
    for (int x = 0; x < lw; x++) {
#if NP_TEXT_EXTRA
      bool on = lmask[(y * lw + x) >> 3] >> ((y * lw + x) & 7) & 1;
#else
      int u = x - p, v = y - p, c = u % a; /* strokes widened to the right */
      bool on = u >= 0 && v >= 0 && v < 8 * k && (fpix(s[u / a], c / k, v / k) || (c && fpix(s[u / a], (c - 1) / k, v / k)));
#endif
      d = on ? 0 : imin(d + 1, R);
      h[x] = (uint8_t)d;
    }
    for (int x = lw - 2; x >= 0; x--) h[x] = (uint8_t)imin(h[x], h[x + 1] + 1);
  }
  for (int i = 0; i < LMW * LMH / 4; i++) lmap[i] = 0;
  for (int y = 0; y < lh; y++)
    for (int x = 0; x < lw; x++) {
      int da = 99, db = 99; /* squared distances: plain, and stretched down */
      for (int dy = -R; dy <= R; dy++) {
        int yy = y + dy;
        if (yy < 0 || yy >= lh) continue;
        int e = hd[yy * lw + x], v = dy < 0 ? imax(-dy - drop, 0) : dy;
        da = imin(da, e * e + dy * dy);
        db = imin(db, e * e + v * v);
      }
      int i = y * lw + x, c = !da ? 3 : da <= 2 ? 2 : db <= 12 ? 1 : 0;
      lmap[i >> 2] |= (uint8_t)(c << (i & 3) * 2);
    }
  for (int y = 0; y < lh; y++) {
    int t = clamp((y - p) * 32 / (7 * k), 0, 32);
    lfill[y] = mix(bot, top, t);
  }
}
static void lettering(int x0, int y0, int a) {
  int xa = imax(x0, rx), xb = imin(x0 + lw, rx + rw);
  if (xa >= xb) return;
  for (int y = imax(y0, ry); y < imin(y0 + lh, ry + rh); y++) {
    color *d = buf + (y - ry) * rw + xa - rx;
    for (int x = xa, i = (y - y0) * lw + xa - x0; x < xb; x++, i++, d++) {
      int v = lmap[i >> 2] >> (i & 3) * 2 & 3;
      if (!v) continue;
      color c = v == 3 ? lfill[y - y0] : v == 2 ? WHITE : DARK;
      *d = a >= 32 ? c : mix(c, *d, a);
    }
  }
}

/* ------------------------------------------------------------------ scene */
/* sky, cloud, cloud top, building, building edge, window, lit window, bush,
   bush top; day then night */
static const color themes[2][9] = {
  {RGB(0x4EC0CA), RGB(0xE6F9DC), RGB(0xD2F2E6), RGB(0xCDECD4), RGB(0xA8DCBC), RGB(0xB6E3C6), RGB(0xB6E3C6), RGB(0x5EE270), RGB(0x4AC45E)},
  {RGB(0x008793), RGB(0x1CA3AE), RGB(0x2DB2BC), RGB(0x0B5F66), RGB(0x084A50), RGB(0x0F6C73), RGB(0xF7E27A), RGB(0x1E8A4C), RGB(0x16703D)},
};
static const color *th;
static uint8_t cl[W], ct[W], bt[W], bf[W]; /* cloud, city and bush tops; building columns */
static bool night;
static int32_t gx;          /* how far the ground went, 1/256 pixel */
static int gph, gph_drawn; /* ...and its stripes' phase */

static int hnext(uint32_t *h, int n) {
  *h = *h * 1103515245u + 12345u;
  return (int)((*h >> 16) % (uint32_t)n);
}
/* a band of round puffs, without deep notches between them */
static void bumps(uint8_t *a, int base, int r0, int r1, uint32_t *h) {
  for (int x = 0; x < W; x++) a[x] = (uint8_t)(base - r0 / 2);
  for (int cx = -6; cx < W + 6; cx += r0 + hnext(h, r1)) {
    int r = r0 + hnext(h, r1 - r0 + 1);
    for (int dx = -r; dx <= r; dx++) {
      int x = cx + dx, y = base - (int)__builtin_sqrtf((float)(r * r - dx * dx));
      if (x >= 0 && x < W && y < a[x]) a[x] = (uint8_t)y;
    }
  }
  for (int x = 1; x < W; x++) a[x] = (uint8_t)imin(a[x], a[x - 1] + 2);
  for (int x = W - 2; x >= 0; x--) a[x] = (uint8_t)imin(a[x], a[x + 1] + 2);
}
static void make_scene(void) {
  th = themes[night];
  uint32_t h = 11; /* the same skyline every time */
  bumps(cl, 158, 9, 19, &h);
  for (int x = 0, i = 0, w = 0, top = 0; x < W; x++, i++) {
    if (i >= w) w = 10 + 4 * hnext(&h, 4), top = 176 - hnext(&h, 26), i = 0;
    ct[x] = (uint8_t)top;
    bf[x] = !i ? 1 : (i & 3) >= 2 && i < w - 2 ? 2 : 0;
  }
  bumps(bt, 196, 5, 11, &h);
}

static void ground_row(color *d, int y) {
  int t = y - GY;
  if (t >= 2 && t < 12) { /* the green stripes, leaning, moving with the pipes */
    for (int x = 0, k = (rx + gph + 140 - t) % 14; x < rw; x++, k = k == 13 ? 0 : k + 1)
      d[x] = k < 7 ? RGB(0x9CE659) : RGB(0x73BF2E);
    return;
  }
  color c = t == 0 ? DARK : t == 1 ? RGB(0xE4FD8B) : t == 12 ? RGB(0x558022) : t == 13 ? RGB(0xD7A84C) : BEIGE;
  for (int x = 0; x < rw; x++) d[x] = c;
}
static void background(void) {
  for (int y = ry; y < ry + rh; y++) {
    color *d = buf + (y - ry) * rw;
    if (y >= GY) {
      ground_row(d, y);
      continue;
    }
    for (int x = rx; x < rx + rw; x++, d++) {
      int i;
      if (y >= bt[x]) i = y == bt[x] ? 8 : 7;
      else if (y >= ct[x]) {
        int t = y - ct[x];
        i = 3;
        if (!t || (bf[x] & 1)) i = 4;
        else if ((bf[x] & 2) && (t & 3) >= 2) i = night && ((x >> 2) * 7 + (t >> 2) * 5 + ct[x]) % 4 == 0 ? 6 : 5;
      } else if (y >= cl[x]) i = y == cl[x] ? 2 : 1;
      else i = 0;
      *d = th[i];
    }
  }
}

/* ------------------------------------------------------------------ pipes */
typedef struct {
  int32_t x;           /* left edge, 1/256 pixel */
  int16_t c, g, o, v;  /* gap centre; gap and wobble (1/256 pixel); wobble speed */
  int8_t lo, hi;       /* wobble range */
  uint8_t on, scored, osc, dense;
  int16_t dx, dt, db;  /* what the screen shows: left, gap top, gap bottom */
} pipe_t;
static pipe_t P[NP];
static color pcol[2][LW]; /* shaft, cap colour by column */
static uint8_t psig[LW];
static int gap_top(const pipe_t *p) { return p->c + (p->o >> 8) - (p->g >> 9); }
static int gap_bot(const pipe_t *p) { return gap_top(p) + (p->g >> 8); }

static void make_pipes(void) {
  static const char shaft[] = "  OLLHHHLLMMMMMMMMMMMMMMMDMDDDDDDDEO  ", cap[] = "OLLHHHHLLMMMMMMMMMMMMMMMMMDMDDDDDDDDEO";
  static const char key[] = " OLHMDE";
  static const color cols[] = {0, DARK, RGB(0x9CE659), RGB(0xE4FD8B), RGB(0x73BF2E), RGB(0x558022), RGB(0x4A7320)};
  for (int u = 0; u < LW; u++) {
    int a = 0, b = 0;
    while (key[a] != shaft[u]) a++;
    while (key[b] != cap[u]) b++;
    pcol[0][u] = cols[a], pcol[1][u] = cols[b];
    psig[u] = (uint8_t)(a | b << 3);
  }
}
static void pipes_draw(void) {
  for (int i = 0; i < NP; i++) {
    const pipe_t *p = &P[i];
    int px = p->x >> 8, x0 = imax(px, rx), x1 = imin(px + LW, rx + rw);
    if (!p->on || x0 >= x1) continue;
    int gt = gap_top(p), gb = gap_bot(p);
    for (int y = ry; y < imin(ry + rh, GY); y++) {
      int r = y < gt - LH ? -1 : y < gt ? y - gt + LH : y < gb ? -2 : y < gb + LH ? y - gb : -1;
      if (r == -2) continue;
      color *d = buf + (y - ry) * rw;
      if (r == 0 || r == LH - 1) { /* the cap's outline */
        for (int x = x0; x < x1; x++) d[x - rx] = DARK;
        continue;
      }
      const color *c = pcol[r > 0];
      for (int x = imax(x0, r < 0 ? px + 2 : px); x < imin(x1, r < 0 ? px + LW - 2 : px + LW); x++) d[x - rx] = c[x - px];
    }
  }
}

/* ------------------------------------------------------------------ the bird */
/* 24x17, 3 bits a pixel: none, outline, white, body, light, belly, lips (2) */
static const uint8_t bird_body[154] = {
  0x00,0x00,0x00,0x48,0x92,0x04,0x00,0x00,0x00,0x00,0x00,0x20,0x21,0x49,0x26,0x09,0x00,0x00,0x00,0x00,0x84,0x24,
  0xC9,0x48,0x52,0x00,0x00,0x00,0x80,0x90,0xDB,0x16,0x49,0x92,0x02,0x00,0x00,0x90,0x6D,0xDB,0x22,0x49,0x92,0x14,
  0x00,0x00,0xB2,0x6D,0xDB,0x22,0x49,0x4A,0x14,0x00,0x40,0xB6,0x6D,0xDB,0x22,0x49,0x4A,0x14,0x00,0xC8,0xB6,0x6D,
  0xDB,0x22,0x49,0x4A,0x14,0x00,0xC8,0xB6,0x6D,0xDB,0x16,0x49,0x92,0x02,0x00,0xD9,0xB6,0x6D,0xDB,0xB6,0x24,0x49,
  0x92,0x00,0xD9,0xB6,0x6D,0xDB,0x16,0xDB,0xB6,0x6D,0x07,0xD9,0xB6,0x6D,0xDB,0x62,0x27,0x49,0x92,0x00,0x48,0xBB,
  0x6D,0xDB,0xE2,0xFF,0xFF,0xFF,0x00,0x40,0xDA,0xB6,0x6D,0x9B,0x24,0x49,0x12,0x00,0x00,0x92,0xB6,0x6D,0xDB,0x36,
  0x00,0x00,0x00,0x00,0x80,0x24,0x6D,0xDB,0x04,0x00,0x00,0x00,0x00,0x00,0x00,0x49,0x12,0x00,0x00,0x00,0x00,0x00,
};
/* the wing, up, level and down: 8x7, 2 bits (none, outline, white, light) */
static const uint8_t bird_wing[3][7][2] = {
  {{0x50,0x01},{0xA4,0x06},{0xA9,0x06},{0xA9,0x1A},{0xAD,0x1A},{0xF4,0x1B},{0x50,0x05}},
  {{0x54,0x05},{0xA9,0x1A},{0xAD,0x6A},{0xF4,0x1F},{0x50,0x05}},
  {{0x54,0x05},{0xA9,0x1A},{0xAD,0x1A},{0xB4,0x1A},{0xD0,0x07},{0x40,0x01}},
};
static const uint8_t wing_y[3] = {3, 6, 7};
/* body, light, belly: yellow, blue, red */
static const color bird_cols[3][3] = {{RGB(0xF8C136), RGB(0xFCE88A), RGB(0xF39A22)},
                                     {RGB(0x55B8E8), RGB(0xA6E2F8), RGB(0x2F86C8)},
                                     {RGB(0xF0553A), RGB(0xFBA28A), RGB(0xC53A2A)}};
static color bpal[8];
static int bird_x, bird_y, bird_a, bird_f, bird_on, bird_s; /* what is drawn: centre, angle (256 a turn), wing, 2x */
static int bd_x, bd_y, bd_a, bd_f;                            /* ...and what the screen shows */

static void bird_colors(int c) {
  static const color fixed[8] = {0, DARK, WHITE, 0, 0, 0, RGB(0xF76A2C), RGB(0xDB4A26)};
  for (int i = 0; i < 8; i++) bpal[i] = i >= 3 && i <= 5 ? bird_cols[c][i - 3] : fixed[i];
}
static int bird_pix(int i, int j) {
  int wj = j - wing_y[bird_f];
  if ((unsigned)wj < 7 && i < 8) {
    int v = bird_wing[bird_f][wj][i >> 2] >> (i & 3) * 2 & 3;
    if (v) return "\1\2\4"[v - 1];
  }
  int b = (j * 24 + i) * 3;
  return (bird_body[b >> 3] | bird_body[(b >> 3) + 1] << 8) >> (b & 7) & 7;
}
static void bird_draw(void) {
  int r = 15 << bird_s, x0 = imax(bird_x - r, rx), x1 = imin(bird_x + r, rx + rw), y1 = imin(bird_y + r, ry + rh);
  if (!bird_on || x0 >= x1) return;
  int a = iabs(bird_a) < 6 ? 0 : bird_a, c = COS(a), s = SIN(a), sh = 8 + bird_s; /* level: the plain sprite */
  for (int y = imax(bird_y - r, ry); y < y1; y++) {
    color *d = buf + (y - ry) * rw + (x0 - rx);
    int dy = 2 * (y - bird_y) + 1;
    for (int x = x0; x < x1; x++, d++) { /* turn back to the sprite, in half pixels */
      int dx = 2 * (x - bird_x) + 1, u = ((dx * c + dy * s) >> sh) + 24, v = ((dy * c - dx * s) >> sh) + 17;
      if (u < 0 || v < 0 || u >= 48 || v >= 34) continue;
      int i = bird_pix(u >> 1, v >> 1);
      if (i) *d = bpal[i];
    }
  }
}

/* ------------------------------------------------------------------ dirty regions */
#define ND 128
static int16_t dq[ND][4];
static int nd;
static bool full;
static void add(int x, int y, int w, int h) {
  int x1 = imin(x + w, W), y1 = imin(y + h, H);
  x = imax(x, 0), y = imax(y, 0);
  if (x >= x1 || y >= y1) return;
  if (nd == ND) {
    full = true;
    return;
  }
  dq[nd][0] = (int16_t)x, dq[nd][1] = (int16_t)y, dq[nd][2] = (int16_t)(x1 - x), dq[nd][3] = (int16_t)(y1 - y);
  nd++;
}
/* both boxes, as one when they overlap */
static void add2(int x0, int y0, int x1, int y1, int w, int h) {
  if (iabs(x1 - x0) < w && iabs(y1 - y0) < h) add(imin(x0, x1), imin(y0, y1), w + iabs(x1 - x0), h + iabs(y1 - y0));
  else add(x0, y0, w, h), add(x1, y1, w, h);
}

static void pipe_dirty(pipe_t *p) {
  int nx = p->on ? p->x >> 8 : OFF, nt = p->on ? gap_top(p) : 0, nb = p->on ? gap_bot(p) : 0;
  int ox = p->dx, ot = p->dt, ob = p->db;
  if (nx == ox && nt == ot && nb == ob) return;
  p->dx = (int16_t)nx, p->dt = (int16_t)nt, p->db = (int16_t)nb;
  if (nx == OFF || ox == OFF || iabs(nx - ox) >= LW) {
    if (ox != OFF) add(ox, 0, LW, GY);
    if (nx != OFF) add(nx, 0, LW, GY);
    return;
  }
  /* the columns whose colours changed, above and below the gap */
  int lo = imin(ox, nx), hi = imax(ox, nx) + LW, top = imax(ot, nt), bot = imin(ob, nb), run = -1, end = 0;
  for (int x = lo; x <= hi; x++) { /* (runs a few columns apart go as one) */
    int a = x - ox, b = x - nx;
    if (x < hi && ((unsigned)a < LW ? psig[a] : 255) != ((unsigned)b < LW ? psig[b] : 255)) {
      if (run < 0) run = x;
      end = x + 1;
    } else if (run >= 0 && (x - end >= 3 || x == hi)) {
      add(run, 0, end - run, top), add(run, bot, end - run, GY - bot), run = -1;
    }
  }
  if (nt != ot) add(lo, imin(ot, nt) - LH, hi - lo, iabs(nt - ot) + LH);
  if (nb != ob) add(lo, imin(ob, nb), hi - lo, iabs(nb - ob) + LH);
}

/* ------------------------------------------------------------------ settings, save */
enum { O_SPEED, O_SPEEDUP, O_PIPES, O_GAP, O_MOVING, O_JUMP, O_EVENTS, O_SURGE, O_WIND, O_NARROW, O_WIDE, O_DENSE,
       O_GRAV, O_STACK, O_NOCOLL, O_BIRD, O_SKY, O_RESET, NOPT };
static const char *const opt_names = /* one after the other, see nth() */
  T("Speed\0Speed up\0Pipes\0Gap\0Moving pipes\0Jump\0Events\0- Pipe surge\0- Wind\0- Narrow gaps\0- Wide gaps\0- Dense pipes\0- Gravity\0Stack events\0No collisions\0Bird\0Sky\0Reset all");
/* the values of the options that are not Off/On, one list after the other */
static const char *const opt_vals =
  T("Very slow\0Slow\0Normal\0Fast\0Insane\0Impossible\0Never\0Every 15\0Every 10\0Every 5\0Each pipe\0Sparse\0Normal\0Dense\0Extreme\0Easy\0Normal\0Hard\0Off\0Rarely\0Sometimes\0Often\0Slow\0Fast\0Floaty\0Bouncy\0Normal\0Snappy\0Heavy\0Never\0Rare\0Normal\0Frequent\0Random\0Yellow\0Blue\0Red\0Random\0Day\0Night\0OK");
static const uint8_t opt_count[NOPT] = {6, 5, 4, 3, 6, 5, 4, 2, 2, 2, 2, 2, 2, 2, 2, 4, 3, 1};
static const uint8_t opt_default[NOPT] = {2, 2, 1, 1, 0, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
/* Classic: the real game (the bird and sky settings still apply) */
static const uint8_t classic[NOPT] = {2, 0, 1, 1, 0, 2, 0};

static struct {
  uint8_t magic, version, mode, pad;
  uint16_t best[2]; /* Classic, Custom */
  uint8_t opt[NOPT];
} sv;
#define SAVE_NAME "flappy.sav"
static bool save_dirty;

static const char *nth(const char *s, int i) {
  while (i--) s += slen(s) + 1;
  return s;
}
static const char *opt_text(int r) {
  int k = sv.opt[r];
  if (opt_count[r] == 2) return nth(T("Off\0On"), k);
  for (int i = 0; i < r; i++) k += opt_count[i] == 2 ? 0 : opt_count[i];
  return nth(opt_vals, k);
}
static void save(void) {
  if (!save_dirty) return;
  sv.magic = 'F', sv.version = 1, sv.pad = 0;
  if (ef_write(SAVE_NAME, &sv, sizeof sv)) save_dirty = false;
}
static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  bool ok = d && n == sizeof sv && d[0] == 'F' && d[1] == 1;
  if (ok)
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&sv)[i] = d[i];
  if (!ok || sv.mode > 1) sv.mode = 0;
  for (int i = 0; i < 2; i++)
    if (!ok || sv.best[i] > 9999) sv.best[i] = 0;
  for (int i = 0; i < NOPT; i++)
    if (!ok || sv.opt[i] >= opt_count[i]) sv.opt[i] = opt_default[i];
}

/* ------------------------------------------------------------------ the game */
enum { S_TITLE, S_SETTINGS, S_READY, S_PLAY, S_DEAD, S_OVER };
enum { EV_NONE, EV_SURGE, EV_TAIL, EV_HEAD, EV_NARROW, EV_WIDE, EV_DENSE, EV_LOW, EV_HIGH };
static const char *const ev_names =
  T("\0Pipe surge!\0Tailwind!\0Headwind!\0Narrow gaps!\0Wide gaps!\0Dense pipes!\0Low gravity!\0High gravity!");
static int state, tick, sel, top_row, paused, psel; /* paused: 1 the menu, 2 "Quit game?" */
static bool leave; /* Quit game: back to NumPlay (or the calculator) */
static int32_t by, vy;                             /* the bird's height and speed, 1/256 pixel */
static int score, shown, done_t, countdown, flash, flashed, landed, newbest, sparkle;
static int spd, spd0, space, gap0, diff, moving, up_every, grav, jump, maxfall, nocoll;
static int apex, gmin, last_c, last_w; /* a flap's height; the narrowest gap; the last pipe's centre and wobble */
static const uint8_t *opts;
/* events, as in All the Apps: a pipe event and a weather event can overlap */
static uint8_t ev_list[8], ev_n, ev_freq, ev_stack, pev, fev, ev_last, ban_e;
static int pwarm, ptime, fwarm, ftime, fdown, cool, ban_t;
static struct {
  int16_t x, y, b[4]; /* the head; the box on the screen */
  int8_t vx, vy;
  uint8_t len, life, drawn;
} st[NS];

static int ev_cooldown(void) {
  static const int16_t lo[4] = {1000, 420, 180, 40}, span[4] = {1, 240, 120, 40};
  return rr(lo[ev_freq], lo[ev_freq] + span[ev_freq]);
}
static bool try_event(int e, bool steep) {
  if (e == EV_SURGE || e == EV_NARROW || e == EV_WIDE || e == EV_DENSE) {
    /* low gravity with narrow or surging gaps can make a pipe impossible */
    if (pev || (e == EV_DENSE && fev) || ((e == EV_NARROW || e == EV_SURGE) && fev == EV_LOW)) return false;
    pev = (uint8_t)e, pwarm = 48, ptime = e == EV_SURGE ? rr(280, 360) : e == EV_DENSE ? rr(240, 310) : rr(260, 340);
  } else {
    if (fev || pev == EV_DENSE || (steep && (e == EV_TAIL || e == EV_HIGH)) ||
        (e == EV_LOW && (pev == EV_NARROW || pev == EV_SURGE)))
      return false;
    bool wind = e == EV_TAIL || e == EV_HEAD;
    fev = (uint8_t)e, fwarm = wind ? 42 : 32, ftime = wind ? rr(220, 290) : rr(200, 260), fdown = 0;
  }
  ban_e = (uint8_t)e, ban_t = 0;
  return true;
}
static void events_step(bool dense_left, bool steep) {
  if (!ev_freq) return;
  if (pwarm) pwarm--;
  else if (ptime && !--ptime && pev != EV_DENSE) pev = 0, cool = fev ? cool : ev_cooldown();
  else if (!ptime && pev == EV_DENSE && !dense_left) pev = 0, cool = fev ? cool : ev_cooldown();
  if (fwarm) fwarm--;
  else if (ftime) {
    if (!--ftime) fdown = fev == EV_TAIL || fev == EV_HEAD ? 28 : 22;
  } else if (fdown && !--fdown) fev = 0, cool = pev ? cool : ev_cooldown();
  bool active = pev || fev;
  if (cool) {
    if (ev_stack || !active) cool--;
  } else if (ev_n && (!active || (ev_stack && rr(0, 99) < (ev_freq == 3 ? 20 : ev_freq == 2 ? 12 : 6)))) {
    int s = rr(0, ev_n - 1);
    cool = 24;
    for (int i = 0; i < ev_n; i++) {
      int e = ev_list[(s + i) % ev_n];
      if ((ev_n > 1 && e == ev_last) || !try_event(e, steep)) continue;
      ev_last = (uint8_t)e, cool = ev_stack ? ev_cooldown() : 0;
      break;
    }
  }
}
static int weather(void) { return fev && !fwarm ? fev : 0; } /* in effect */
/* gravity events change the pull and the flap together, so a flap still lifts
   the bird as high: it only hangs longer or shorter in the air */
static int pull(bool up) {
  int w = weather();
  return w == EV_LOW ? (up ? 205 : 164) : w == EV_HIGH ? (up ? 297 : 343) : 256;
}
static int gap_target(void) { /* always room for a flap and some */
  return imax(pwarm ? gap0 << 8 : pev == EV_NARROW ? gap0 * 184 : pev == EV_WIDE ? gap0 * 307 : gap0 << 8, (apex + 28) << 8);
}
static bool dense_now(void) { return pev == EV_DENSE && !pwarm && ptime; }

/* where the next gap goes: never further than the bird can climb or dive
   between two pipes (the whole range in the classic game). The bird can use
   the room a gap leaves beside a flap, then climb with flaps or fall from rest
   in the frames between the pipes; wobbling pipes take some of that away. */
static int next_centre(int prev, int wob) {
  static const uint8_t fac[3] = {100, 154, 218}, most[3] = {36, 90, 130};
  int g = gap_target() >> 8, lo = 24 + g / 2, hi = GY - 24 - g / 2;
  int sp = dense_now() ? space * 174 >> 8 : space, f = imax(((sp - LW - 18) << 8) / imax(spd, spd0), 0);
  int room = imax(imin(g, gmin) - 12 - apex, 0) << 8, fall = room;
  for (int t = 1; t <= f; t++) fall += imin(grav * t, maxfall);
  int up = imin((room + f * (jump - 5 * grav)) * fac[diff] >> 16, most[diff]) - wob;
  int dn = imin(fall * fac[diff] >> 16, most[diff]) - wob;
  int a = imax(lo, prev - imax(up, 4)), b = imin(hi, prev + imax(dn, 4));
  if (a > b) return clamp(prev, lo, hi);
  int c = rr(a, b);
  if (diff == 2 && iabs(c - prev) < 16) c = c < prev ? imax(a, prev - 16) : imin(b, prev + 16);
  return c;
}
static int reach(int g) { return clamp((g >> 8) - 12 - apex, 6, 20); } /* how far a pipe may wobble */
static void wobble(pipe_t *p, int v, int reach) {
  int g = p->g >> 9;
  p->v = (int16_t)(rnd() & 1 ? v : -v);
  p->lo = (int8_t)-imin(reach, imax(p->c - g - 24, 0)), p->hi = (int8_t)imin(reach, imax(GY - 24 - g - p->c, 0));
  if (p->lo >= p->hi) p->v = 0;
}
static void spawn(pipe_t *p, int32_t x) {
  static const uint8_t how_often[6] = {0, 25, 50, 75, 100, 100}; /* moving pipes, % */
  p->on = 1, p->scored = 0, p->x = x, p->o = 0, p->v = 0;
  p->g = (int16_t)gap_target();
  p->dense = dense_now();
  int r = rr(0, 99), w = 0;
  p->osc = r < how_often[moving];
  bool fast = moving == 5 || (moving == 3 && r < 22);
  if (p->osc || (pev == EV_SURGE && !pwarm)) w = reach(p->g);
  p->c = (int16_t)(last_c = next_centre(last_c, last_w + w));
  last_w = w;
  if (w) wobble(p, w < 12 ? 51 : fast ? 128 : 82, w);
}

static void flap(void) {
  vy = -(jump * pull(true) >> 8);
  bird_a = -18; /* the beak goes up at once, as in the original */
}

static void new_game(void) {
  opts = sv.mode ? sv.opt : classic;
  /* pixels a frame, in 1/256: pipe speeds; gravity, flap and fall speed */
  static const uint16_t speeds[6] = {197, 269, 358, 466, 591, 752}, jumps[5][3] = {
    {36, 740, 900}, {46, 912, 1152}, {51, 870, 1152}, {72, 1000, 1400}, {72, 820, 1536}};
  static const uint8_t spaces[4] = {150, 120, 96, 76}, gaps[3] = {80, 70, 60}, ups[5] = {0, 15, 10, 5, 1};
  spd = spd0 = speeds[opts[O_SPEED]], space = spaces[opts[O_PIPES]], diff = opts[O_GAP], gap0 = gaps[diff];
  moving = opts[O_MOVING], up_every = ups[opts[O_SPEEDUP]], nocoll = sv.mode && sv.opt[O_NOCOLL];
  grav = jumps[opts[O_JUMP]][0], jump = jumps[opts[O_JUMP]][1], maxfall = jumps[opts[O_JUMP]][2];
  apex = jump * jump / (grav * 512);
  ev_freq = opts[O_EVENTS], ev_stack = sv.mode && sv.opt[O_STACK], ev_n = 0;
  for (int e = EV_SURGE; e <= EV_HIGH; e++) /* both winds share a setting, and both gravities */
    if (sv.opt[O_SURGE + e - EV_SURGE - (e > EV_TAIL) - (e > EV_LOW)]) ev_list[ev_n++] = (uint8_t)e;
  pev = fev = ev_last = ban_e = 0, pwarm = ptime = fwarm = ftime = fdown = 0, cool = ev_freq ? ev_cooldown() : 0;
  night = sv.opt[O_SKY] ? sv.opt[O_SKY] == 2 : rnd() & 1;
  bird_colors(sv.opt[O_BIRD] ? sv.opt[O_BIRD] - 1 : (int)(rnd() % 3));
  make_scene();
  for (int i = 0; i < NP; i++) P[i].on = 0;
  for (int i = 0; i < NS; i++) st[i].life = 0;
  gmin = imax(ev_freq && sv.opt[O_NARROW] ? gap0 * 184 >> 8 : gap0, apex + 28); /* the gap's smallest size */
  last_c = GY / 2, last_w = 0, by = 100 << 8, vy = 0, score = 0, bird_a = 0, countdown = 0, flash = 0, landed = 0;
  letters(T("Get Ready!"), 3, 2, RGB(0xB8F07A), RGB(0x4DB33D));
  state = S_READY, tick = 0, paused = 0, full = true;
}
static void to_title(void) {
  night = 0;
  bird_colors(0);
  make_scene();
  for (int i = 0; i < NP; i++) P[i].on = 0;
  for (int i = 0; i < NS; i++) st[i].life = 0;
  letters(T("FlappyBird"), 3, 3, RGB(0xFFF3B8), RGB(0xF7A93A));
  state = S_TITLE, tick = 0, sel = sv.mode, paused = 0, bird_a = 0, full = true;
}
static void to_settings(void) {
  letters(T("Settings"), 3, 2, RGB(0xB8F07A), RGB(0x4DB33D));
  state = S_SETTINGS, sel = 0, top_row = 0, full = true;
}
static void die(void) {
  state = S_DEAD, tick = 0, flash = 32;
  if (vy < 0) vy = 0;
  for (int i = 0; i < NS; i++) st[i].life = 0;
  ban_e = 0;
}
#define PY 52  /* the score board's place, */
#define BTY 148 /* and the buttons' */
static int panel_y(int t) { /* the board slides up, slowing down */
  int u = clamp(t - 16, 0, 20);
  return PY + (20 - u) * (20 - u) * 188 / 400;
}
static bool keep_best(void) { /* a game ended or was left: is it a new best? */
  if (nocoll || score <= sv.best[sv.mode]) return false;
  sv.best[sv.mode] = (uint16_t)imin(score, 9999), save_dirty = true;
  return true;
}
static void game_over(void) {
  state = S_OVER, tick = 0, sel = 0, shown = 0, done_t = 9999, sparkle = 0;
  newbest = keep_best();
  save();
  letters(T("Game Over"), 3, 2, RGB(0xFFD27A), RGB(0xE86101));
  full = true;
}

/* a bird that flaps its wings, with a bob */
static int bob(int period, int amp) { return SIN(tick * 256 / period) * amp / 256; }
static void flutter(int every) {
  static const uint8_t cyc[4] = {0, 1, 2, 1};
  bird_f = cyc[tick / every & 3];
}

static bool hits(void) {
  int y = by >> 8, t = y - 6, b = y + 6, l = BX - 9, r = BX + 9;
  if (b >= GY) return true;
  for (int i = 0; i < NP; i++) {
    const pipe_t *p = &P[i];
    int px = p->x >> 8, gt = gap_top(p), gb = gap_bot(p);
    if (!p->on || r <= px || l >= px + LW) continue;
    bool shaft = r > px + 2 && l < px + LW - 2;
    if ((t < gt && (b > gt - LH || shaft)) || (b > gb && (t < gb + LH || shaft))) return true;
  }
  return false;
}

static void streaks_step(void) {
  int e = fwarm || ftime ? fev : 0;
  if (e == EV_TAIL || e == EV_HEAD || e == EV_LOW || e == EV_HIGH)
    for (int i = 0; i < NS && tick % 2 == 0; i++)
      if (!st[i].life) {
        bool side = e == EV_TAIL || e == EV_HEAD;
        int v = rr(6, 9) * (e == EV_TAIL || e == EV_HIGH ? 1 : -1);
        st[i].x = (int16_t)(side ? (v > 0 ? rr(-30, -10) : rr(W + 10, W + 30)) : rr(20, W - 20));
        st[i].y = (int16_t)(side ? rr(20, GY - 20) : v > 0 ? rr(-30, -10) : rr(GY, GY + 20));
        st[i].vx = (int8_t)(side ? v : 0), st[i].vy = (int8_t)(side ? 0 : v);
        st[i].len = (uint8_t)rr(8, 18), st[i].life = 60;
        break;
      }
  for (int i = 0; i < NS; i++)
    if (st[i].life) {
      st[i].x = (int16_t)(st[i].x + st[i].vx), st[i].y = (int16_t)(st[i].y + st[i].vy), st[i].life--;
      if (st[i].x < -40 || st[i].x > W + 40 || st[i].y < -40 || st[i].y > GY + 30) st[i].life = 0;
    }
}
/* a streak's box, trailing behind its head */
static void streak_box(int x, int y, int vx, int vy, int len, int *b) {
  b[0] = vx > 0 ? x - len : x, b[1] = vy > 0 ? y - len : y, b[2] = vx ? len : 2, b[3] = vx ? 2 : len;
}
static void streaks_draw(void) {
  for (int i = 0; i < NS; i++)
    if (st[i].life) {
      int b[4];
      streak_box(st[i].x, st[i].y, st[i].vx, st[i].vy, st[i].len, b);
      /* wind is white; light air rises pale blue, heavy air falls warm */
      shade(b[0], b[1], b[2], imin(b[3], GY - b[1]), st[i].vx ? WHITE : st[i].vy < 0 ? RGB(0xCFF4FF) : RGB(0xFFE0A0), 26);
    }
}

static void play_step(void) {
  if (tick <= 8) add(W / 2 - lw / 2, 48, lw, lh), add(NP_TEXT_EXTRA ? 0 : 138, 94, NP_TEXT_EXTRA ? W : 96, 56); /* Get Ready fades out */
  if (countdown) {
    if (!(--countdown % 50)) add(120, 82, 80, 56);
    return;
  }
  bool dense_left = false;
  int n0 = -1, n1 = -1; /* the next two pipes */
  for (int i = 0; i < NP; i++)
    if (P[i].on && (P[i].x >> 8) + LW >= BX) {
      dense_left |= P[i].dense;
      if (n0 < 0 || P[i].x < P[n0].x) n1 = n0, n0 = i;
      else if (n1 < 0 || P[i].x < P[n1].x) n1 = i;
    }
  bool steep = n1 >= 0 && iabs(P[n0].c - P[n1].c) >= 54;
  int was = pev == EV_SURGE && !pwarm;
  events_step(dense_left, steep);
  int surge = pev == EV_SURGE && !pwarm, w = weather();
  /* the pipes' speed follows the wind, smoothly */
  int target = spd0 * (w == EV_TAIL ? 422 : w == EV_HEAD ? 154 : 256) >> 8;
  spd += (target - spd) / 12 + (target > spd) - (target < spd);
  gx += spd, gph = (int)(gx >> 8) % 14;
  int32_t right = -((int32_t)1 << 30);
  int gt = gap_target();
  for (int i = 0; i < NP; i++) {
    pipe_t *p = &P[i];
    if (!p->on) continue;
    p->x -= spd;
    if (surge != was) {
      if (surge && !p->v) wobble(p, 77 + rr(0, 2) * 20, 16);
      else if (!surge && !p->osc) p->v = 0;
    }
    if (p->v) {
      p->o = (int16_t)(p->o + p->v);
      if ((p->v < 0 && p->o <= p->lo * 256) || (p->v > 0 && p->o >= p->hi * 256)) p->v = (int16_t)-p->v;
    }
    if (iabs(p->g - gt) > 64) p->g = (int16_t)(p->g + (gt - p->g) * 20 / 256);
    if (!p->scored && (p->x >> 8) + LW / 2 < BX) {
      p->scored = 1, score++;
      add(100, 8, 120, 34);
      if (up_every && score % up_every == 0) spd0 = imin(spd0 * 294 >> 8, imax(1024, spd0));
    }
    if ((p->x >> 8) < -LW - 4) p->on = 0;
    else if (p->x > right) right = p->x;
  }
  if (right < (W << 8)) {
    int sp = dense_now() ? space * 174 >> 8 : space;
    for (int i = 0; i < NP; i++)
      if (!P[i].on) {
        spawn(&P[i], right < -((int32_t)1 << 29) ? W << 8 : right + (sp << 8));
        break;
      }
  }
  /* the bird */
  vy = imin(vy + (grav * pull(false) >> 8), maxfall);
  by += vy;
  if (by < 0) by = 0, vy = imax(vy, 0);
  bird_a = vy < 300 ? imax(bird_a - 6, -18) : imin(bird_a + 3, 64);
  if (bird_a > 40) bird_f = 1;
  else flutter(4);
  if (nocoll) {
    if ((by >> 8) + 8 > GY) by = (GY - 8) << 8, vy = imin(vy, 0);
  } else if (hits()) {
    die();
  }
  streaks_step();
  if (ban_e && ++ban_t > 170) ban_e = 0, add(0, 210, NP_TEXT_EXTRA ? W : 190, 30);
  if (ban_e && (ban_t < 50 || ban_t > 158)) add(0, 210, NP_TEXT_EXTRA ? W : 190, 30);
}

static void (*fade_next)(void);
static int fade_t; /* a quick fade through black between screens, as in the original */
static void go(void (*f)(void)) { fade_next = f, fade_t = 1, paused = 0, full = true; }

static void step(void) {
  if (fade_t) {
    full = true;
    if (++fade_t == 7) fade_next();
    if (fade_t == 13) fade_t = 0;
    if (fade_t && fade_t < 7) return;
  }
  tick++;
  switch (state) {
    case S_TITLE:
    case S_SETTINGS:
    case S_READY:
      gx += 358, gph = (int)(gx >> 8) % 14; /* the ground keeps running */
      flutter(6);
      break;
    case S_PLAY:
      play_step();
      break;
    case S_DEAD:
      if (flash) flash = imax(flash - 4, 0);
      if (!landed) {
        vy = imin(vy + grav, maxfall);
        by += vy;
        bird_a = imin(bird_a + 6, 64), bird_f = 1;
        if ((by >> 8) + 8 >= GY) by = (GY - 8) << 8, landed = tick;
      } else if (tick - landed > 30) {
        game_over();
      }
      break;
    case S_OVER:
      if (tick <= 9) add(W / 2 - lw / 2, 4, lw, lh + 10);
      if (tick >= 16 && tick <= 36) add(75, panel_y(tick), 170, panel_y(tick - 1) - panel_y(tick) + 90);
      if (tick >= 40 && shown < score && tick % 2 == 0) shown = imin(score, shown + imax(1, score / 24)), add(150, PY + 20, 84, 20);
      if (tick >= 40 && shown == score && done_t == 9999) done_t = tick, add(75, PY, 170, 88);
      if (tick == done_t + 6) add(76, BTY - 4, 168, 36);
      if (tick > done_t && score >= 10 && tick % 6 == 0) sparkle = (int)(rnd() & 0xFFFF), add(95, PY + 30, 32, 32);
      break;
  }
}

/* ------------------------------------------------------------------ screens */
static void play_icon(int x, int y) { /* the green arrow of the original's Play button */
  for (int i = -1; i < 12; i++) fill(x - 1, y + i, imin(i + 1, 11 - i) + 2, 1, DARK);
  for (int i = 0; i < 11; i++) fill(x, y + i, imin(i, 10 - i) + 1, 1, RGB(0x5AC54B));
}
/* the original's buttons; the chosen one is lifted, in an orange ring */
static void button(int x, int y, int w, int h, const char *s, bool on, bool icon, int k) {
  if (on) y -= 2, rrect(x - 2, y - 2, w + 4, h + 6, ORANGE);
  rrect(x, y + 2, w, h, mix(DARK, BEIGE, 12)); /* a soft shadow */
  rrect(x, y, w, h, DARK);
  rrect(x + 1, y + 1, w - 2, h - 2, WHITE);
  fill(x + 3, y + 3, w - 6, h - 6, RGB(0xF3EBCF));
  int tx = x + (w - tw(s, k) - (icon ? 16 : 0)) / 2;
  if (icon) play_icon(tx, y + h / 2 - 5), tx += 16;
  text(s, tx, y + (h - 7 * k) / 2, k, DARK);
}

static void disc(int cx, int cy, int r, color c) {
  for (int dy = -r; dy <= r; dy++) {
    int h = (int)__builtin_sqrtf((float)(r * r + r - dy * dy));
    fill(cx - h, cy + dy, 2 * h + 1, 1, c);
  }
}
static void medal(int cx, int cy, int tier) {
  static const color cols[4][3] = {{RGB(0xE8A36A), RGB(0xC77A3C), RGB(0xF6CFA0)}, {RGB(0xE3E3E3), RGB(0xAFAFAF), WHITE},
                                   {RGB(0xF8D23A), RGB(0xD39A14), RGB(0xFFF1A8)}, {RGB(0xE9F6F8), RGB(0x9CC8D4), WHITE}};
  color base = cols[tier][0], rim = cols[tier][1];
  disc(cx, cy, 13, rim);
  disc(cx - 1, cy - 1, 11, cols[tier][2]); /* a shine, top left */
  disc(cx + 1, cy + 1, 10, base);
  disc(cx, cy, 7, rim); /* the embossed ring */
  disc(cx, cy, 5, base);
  int s = sparkle, sx = cx - 9 + (s & 15) * 18 / 16, sy = cy - 9 + (s >> 4 & 15) * 18 / 16, k = s >> 8 & 1;
  fill(sx - 1 - k, sy, 3 + 2 * k, 1, WHITE), fill(sx, sy - 1 - k, 1, 3 + 2 * k, WHITE);
}

static void panel(int y) { /* the score board */
  const int x = 75, w = 170;
  char n[8];
  board(x, y, w, 88);
  const char *sc = T("SCORE"), *be = T("BEST");
  text(T("MEDAL"), x + 16, y + 10, 1, ORANGE);
  text(sc, x + w - 14 - tw(sc, 1), y + 10, 1, ORANGE);
  text(be, x + w - 14 - tw(be, 1), y + 46, 1, ORANGE);
  disc(x + 36, y + 46, 16, RGB(0xCFC285)); /* the medal's hollow */
  num(n, shown);
  big(shown, x + w - 14 - bigw(n, 2), y + 22, 2, false);
  int best = sv.best[sv.mode];
  num(n, best);
  big(best, x + w - 14 - bigw(n, 2), y + 58, 2, false);
  if (shown == score && tick >= done_t) {
    if (score >= 10) medal(x + 36, y + 46, score >= 40 ? 3 : score >= 30 ? 2 : score >= 20 ? 1 : 0);
    if (newbest) {
#if NP_TEXT_EXTRA /* (as wide as its text, and a Chinese letter's height) */
      const char *nw = T("NEW");
      int bw = tw(nw, 1) + 7, bx = x + w - 14 - bigw(n, 2) - 6 - bw;
      rrect(bx, y + 59, bw, 13, RGB(0xFC3800));
      text(nw, bx + 4, y + 62, 1, WHITE);
#else
      int bx = x + w - 14 - bigw(n, 2) - 30;
      rrect(bx, y + 60, 24, 11, RGB(0xFC3800));
      text("NEW", bx + 3, y + 62, 1, WHITE);
#endif
    }
  }
}

static void tap_hint(int cx, int y) { /* the original's tutorial, with the OK key */
  for (int i = 0; i < 10; i++) fill(cx - i - 1, y + i, 2 * i + 2, 1, DARK), fill(cx - i, y + i + 1, 2 * i, 1, WHITE);
  fill(cx - 4, y + 10, 8, 10, DARK), fill(cx - 3, y + 10, 6, 9, WHITE);
  rrect(cx - 20, y + 26, 40, 24, DARK);
  rrect(cx - 19, y + 27, 38, 22, WHITE);
  fill(cx - 17, y + 45, 34, 2, RGB(0xD0D0D0));
  ctext("OK", cx, y + 31, 2, DARK); /* (the key) */
  const char *tap = T("TAP");
  text(tap, NP_TEXT_EXTRA ? cx - 29 - tw(tap, 1) : cx - 46, y + 34, 1, RGB(0xF4561D));
  text(tap, cx + 28, y + 34, 1, RGB(0xF4561D));
}

static void settings_draw(void) {
  lettering(160 - lw / 2, 4, 32);
  board(8, 40, 304, 158);
  for (int i = 0; i < 8 && top_row + i < NOPT; i++) {
    int r = top_row + i, y = 50 + i * 18;
    bool on = r == sel;
    const char *v = opt_text(r);
    if (on) rrect(14, y - 3, 292, 20, ORANGE);
    color c = on ? WHITE : DARK;
    text(nth(opt_names, r), 22, y, 2, c);
    bool arrows = on && opt_count[r] > 1;
    int vx = 298 - tw(v, 2) - (arrows ? 12 : 0);
    if (on || r != O_RESET) text(v, vx, y, 2, on ? WHITE : ORANGE);
    if (arrows) text("<", vx - 12, y, 2, c), text(">", 294, y, 2, c);
  }
  fill(306, 44 + top_row * 146 / NOPT, 3, 8 * 146 / NOPT, mix(DARK, BEIGE, 14)); /* where we are */
  ctext(nth(T("For Custom games\0For both games\0Back to the defaults"), sel < O_BIRD ? 0 : sel < O_RESET ? 1 : 2), 160, 216, 1, DARK);
  ctext(T("Left/Right: change   Back: done"), 160, 228, 1, RGB(0x9A7F4E));
}

static void pause_draw(void) {
  shade(0, 0, W, H, 0, 12);
  board(80, 58, 160, 118);
  if (paused == 2) {
    ctext(T("Quit game?"), 160, 76, 2, DARK);
    button(94, 120, 60, 28, T("Yes"), psel == 1, false, 2);
    button(166, 120, 60, 28, T("No"), psel == 0, false, 2);
    return;
  }
  ctext(T("Paused"), 160, 68, 2, ORANGE);
  for (int i = 0; i < 3; i++) {
    int y = 92 + i * 26;
    if (psel == i) rrect(92, y - 4, 136, 22, ORANGE);
    ctext(nth(T("Resume\0Restart\0Quit game"), i), 160, y, 2, psel == i ? WHITE : DARK);
  }
}

static void ui(void) {
  switch (state) {
    case S_TITLE: {
      int b = bob(90, 4), lx = (W - lw - 56) / 2;
      lettering(lx, 28 + b, 32);
      const char *m0 = T("Classic"), *m1 = T("Custom");
      bool arrow = !NP_TEXT_EXTRA || imax(tw(m0, 2), tw(m1, 2)) + 16 <= 104; /* (both with it, or neither) */
      button(44, 112, 110, 38, m0, sel == 0, arrow, 2);
      button(166, 112, 110, 38, m1, sel == 1, arrow, 2);
      for (int i = 0; i < 2; i++) {
        char t[NP_TEXT_EXTRA ? 32 : 16], *e = t;
        for (const char *b = T("Best "); *b;) *e++ = *b++;
        num(e, sv.best[i]);
        otext(t, 99 + 122 * i - tw(t, 1) / 2, 158, 1, WHITE);
      }
      button(116, 172, 88, 20, T("Settings"), sel == 2, false, 1); /* small, like the original's Rate */
      ctext(T("Based on Tatone26's version"), 160, 222, 1, RGB(0x9A7F4E));
      break;
    }
    case S_SETTINGS:
      settings_draw();
      break;
    case S_READY:
      big(0, W / 2, 14, 3, true);
      lettering(W / 2 - lw / 2, 48, 32);
      tap_hint(186, 96);
      break;
    case S_PLAY:
    case S_DEAD:
      if (tick < 8 && state == S_PLAY) { /* Get Ready fades out */
        ga = 32 - tick * 4;
        lettering(W / 2 - lw / 2, 48, ga);
        tap_hint(186, 96);
        ga = 32;
      }
      big(score, W / 2, 14, 3, true);
      if (countdown) big((countdown + 49) / 50, W / 2, 88, 6, true);
      if (ban_e) { /* the event's name, on the ground */
        const char *s = nth(ev_names, ban_e);
        int w = tw(s, 2) + 16, t = ban_t, out = t < 10 ? 10 - t : t > 160 ? t - 160 : 0, x = 8 - (w + 8) * out / 10;
        if (!(pwarm || fwarm) || (t & 8)) {
          rrect(x, 213, w, 22, DARK);
          rrect(x + 1, 214, w - 2, 20, ORANGE);
          text(s, x + 8, 217, 2, WHITE);
        }
      }
      break;
    case S_OVER: {
      int t = tick;
      lettering(W / 2 - lw / 2, 14 - imax(0, 8 - t), imin(32, t * 4));
      if (t >= 16) panel(panel_y(t));
      if (t >= done_t + 6) {
        button(80, BTY, 76, 26, T("Play"), sel == 0, true, 2);
        button(164, BTY, 76, 26, T("Menu"), sel == 1, false, 2);
      }
      break;
    }
  }
  if (nocoll && state >= S_READY && state <= S_DEAD) { /* scores don't count */
    const char *s = T("PRACTICE");
    otext(s, NP_TEXT_EXTRA ? W / 2 - tw(s, 1) / 2 : W / 2 - 23, 42, 1, WHITE);
  }
  if (paused) pause_draw();
  if (flash) shade(0, 0, W, H, WHITE, flash);
  if (fade_t) shade(0, 0, W, H, 0, (fade_t < 7 ? fade_t : 13 - fade_t) * 5);
}

static void compose(void) {
  background();
  pipes_draw();
  streaks_draw();
  bird_draw();
  ui();
}
static void paint(int x, int y, int w, int h) {
  for (int band = BUFN / w, y1 = y + h; y < y1; y += band) {
    rx = x, ry = y, rw = w, rh = imin(band, y1 - y);
    compose();
    eadk_display_push_rect((eadk_rect_t){(uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh}, buf);
  }
}

/* what moved since the last frame goes to the screen */
static int logo_y;
static void flush(void) {
  bird_on = state != S_SETTINGS, bird_s = state == S_TITLE;
  switch (state) {
    case S_TITLE: bird_x = (W + lw + 56) / 2 - 24, bird_y = 30 + lh / 2 + bob(90, 4); break;
    case S_READY: bird_x = BX, bird_y = 100 + bob(60, 4); break;
    default: bird_x = BX, bird_y = by >> 8;
  }
  if (state == S_TITLE && bob(90, 4) != logo_y) {
    int b = bob(90, 4);
    add((W - lw - 56) / 2, 28 + imin(b, logo_y), lw, lh + iabs(b - logo_y));
    logo_y = b;
  }
  for (int i = 0; i < NP; i++) pipe_dirty(&P[i]);
  /* (the bird only appears, goes or changes size with a new screen, drawn whole) */
  int h = 15 << bird_s;
  if (bird_x != bd_x || bird_y != bd_y || bird_a != bd_a || bird_f != bd_f) {
    add2(bd_x - h, bd_y - h, bird_x - h, bird_y - h, 2 * h, 2 * h);
    bd_x = bird_x, bd_y = bird_y, bd_a = bird_a, bd_f = bird_f;
  }
  for (int i = 0; i < NS; i++) { /* a streak that moved: where it was and where it is, as one box */
    int b[4] = {0, 0, 0, 0};
    int16_t *o = st[i].b;
    if (st[i].life) streak_box(st[i].x, st[i].y, st[i].vx, st[i].vy, st[i].len, b);
    if (st[i].drawn && st[i].life && o[2] == b[2] && o[3] == b[3]) add2(o[0], o[1], b[0], b[1], b[2], b[3]);
    else {
      if (st[i].drawn) add(o[0], o[1], o[2], o[3]);
      add(b[0], b[1], b[2], b[3]);
    }
    for (int j = 0; j < 4; j++) o[j] = (int16_t)b[j];
    st[i].drawn = st[i].life != 0;
  }
  if (gph != gph_drawn) add(0, GY + 2, W, 10), gph_drawn = gph;
  if (flash || flashed) full = true;
  flashed = flash;
  eadk_display_wait_for_vblank();
  if (full)
    for (int y = 0; y < H; y += BUFN / W) paint(0, y, W, BUFN / W);
  else
    for (int i = 0; i < nd; i++) paint(dq[i][0], dq[i][1], dq[i][2], dq[i][3]);
  nd = 0, full = false;
}

/* ------------------------------------------------------------------ input */
#define FLAP (KEY(eadk_key_ok) | KEY(eadk_key_exe) | KEY(eadk_key_up) | KEY(eadk_key_eight) | KEY(eadk_key_five))

static void input(uint64_t hit) {
  bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
  int lr = !!(hit & (KEY(eadk_key_right) | KEY(eadk_key_six))) - !!(hit & (KEY(eadk_key_left) | KEY(eadk_key_four)));
  int ud = !!(hit & (KEY(eadk_key_down) | KEY(eadk_key_two))) - !!(hit & (KEY(eadk_key_up) | KEY(eadk_key_eight)));
  if (paused) {
    int was = paused * 4 + psel;
    if (paused == 2) {
      if (lr || ud) psel ^= 1;
      if (ok && psel) keep_best(), leave = true;
      else if ((back || ok) && state == S_TITLE) paused = 0; /* asked from the title */
      else if (back || ok) paused = 1, psel = 2;
    } else {
      psel = clamp(psel + ud, 0, 2);
      if (back || (ok && psel == 0)) {
        paused = 0;
        if (state == S_PLAY) countdown = 150;
      } else if (ok && psel == 1) {
        keep_best();
        go(new_game);
      } else if (ok) {
        paused = 2, psel = 0;
      }
    }
    if (paused * 4 + psel != was || !paused) full = true;
    return;
  }
  switch (state) {
    case S_TITLE: {
      int was = sel;
      if (sel < 2 && lr) sel ^= 1;
      if (ud > 0) sel = 2;
      if (ud < 0 && sel == 2) sel = sv.mode;
      if (sel != was) add(40, 106, 240, 90);
      if (back) paused = 2, psel = 0, full = true;
      else if (ok && sel == 2) to_settings();
      else if (ok) {
        if (sv.mode != sel) sv.mode = (uint8_t)sel, save_dirty = true;
        go(new_game);
      }
      break;
    }
    case S_SETTINGS:
      if (back) {
        save();
        to_title();
        sel = 2;
        break;
      }
      if (ud || lr || ok) add(8, 40, 304, 158), add(0, 214, W, 12);
      sel = clamp(sel + ud, 0, NOPT - 1);
      top_row = clamp(top_row, sel - 7, sel);
      if (sel == O_RESET) {
        if (ok)
          for (int i = 0; i < NOPT; i++) sv.opt[i] = opt_default[i], save_dirty = true;
      } else if (lr || ok) {
        int n = opt_count[sel];
        sv.opt[sel] = (uint8_t)((sv.opt[sel] + (lr ? lr : 1) + n) % n), save_dirty = true;
      }
      break;
    case S_READY:
      if (back) paused = 1, psel = 0, full = true;
      else if (hit & FLAP) by = (100 + bob(60, 4)) << 8, state = S_PLAY, tick = 0, flap();
      break;
    case S_PLAY:
      if (back) paused = 1, psel = 0, full = true;
      else if ((hit & FLAP) && !countdown) flap();
      break;
    case S_OVER:
      if (tick < done_t + 6) break;
      if (lr) sel ^= 1, add(76, BTY - 4, 168, 36);
      if (back || (ok && sel)) go(to_title);
      else if (ok) go(new_game);
      break;
  }
}

int main(void) {
  np_app_begin();
  for (int i = 0; i < 128; i++) { /* Bhaskara's sine */
    float x = i * (3.14159265f / 128), q = x * (3.14159265f - x);
    sn[i] = (int16_t)(16 * q / (49.348022f - 4 * q) * 256 + 0.5f);
    sn[i + 128] = (int16_t)-sn[i];
  }
  uint32_t last = (uint32_t)eadk_timing_millis();
  seed ^= last ^ eadk_random();
  make_pipes();
  for (int i = 0; i < NP; i++) P[i].dx = OFF;
  load();
  to_title();
  uint64_t held = ~0ull; /* keys still down from the launcher count only once released */
  uint32_t repeat_at = 0;
  int acc = 0;
  for (;;) {
    uint32_t now = (uint32_t)eadk_timing_millis();
    acc = imin(acc + (int)(now - last) * 3, 200), last = now;
    uint64_t k = eadk_keyboard_scan(), hit = k & ~held;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) {
      keep_best();
      break;
    }
    const uint64_t rep = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down) |
                         KEY(eadk_key_two) | KEY(eadk_key_four) | KEY(eadk_key_six) | KEY(eadk_key_eight);
    if (hit & rep) repeat_at = now + 350;
    else if ((k & rep) && (int32_t)(now - repeat_at) >= 0 && (state == S_SETTINGS || paused))
      hit |= k & rep, repeat_at = now + 90;
    held = k;
    if (!fade_t) input(hit);
    if (leave) break;
    for (; acc >= 25; acc -= 50) /* 60 steps a second, whatever the frame rate */
      if (!paused || fade_t) step();
    flush();
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 15) eadk_timing_msleep(15 - spent);
  }
  save();
  return np_app_end();
}
