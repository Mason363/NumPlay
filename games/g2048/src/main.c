/* 2048: slide the tiles, join equal numbers, get to 2048.
 *
 * Gabriele Cirulli's game, looking like his: the beige page, the brown board,
 * warm tiles that slide, pop when they merge and zoom in when they appear,
 * and "+N" rising from the score. Only what changes is drawn, composed in RAM
 * a few rows at a time, and every letter comes from one small stroke font (a
 * handful of points and curves each) drawn with a round pen at any size. */
#include <eadk.h>
#ifdef __TICE__
#include <string.h>
#endif
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_text.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "2048";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

#define W 320
#define H 240
typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define PAGE RGB(0xFAF8EF)
#define BOARD RGB(0xBBADA0)
#define SLOT RGB(0xCDC1B4)
#define INK RGB(0x776E65)
#define LIGHT RGB(0xF9F6F2)
#define BROWN RGB(0x8F7A66)
#define LABEL RGB(0xEEE4DA)
#define KEY(k) (1ull << (k))
#define WT 2.4f /* the bold pen, in font units (a capital is 12 units tall) */

/* 2, 4, 8 ... 2048, and everything above */
static const color tile_bg[12] = {RGB(0xEEE4DA), RGB(0xEDE0C8), RGB(0xF2B179), RGB(0xF59563),
                                  RGB(0xF67C5F), RGB(0xF65E3B), RGB(0xEDCF72), RGB(0xEDCC61),
                                  RGB(0xEDC850), RGB(0xEDC53F), RGB(0xEDC22E), RGB(0x3C3A32)};

static uint32_t now, seed = 0x9E3779B9u, useed;
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static int fl(float v) { return (int)__builtin_floorf(v); }
static float ease(float t) {
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  return t * t * (3 - 2 * t);
}
/* fast, then slowing down: how pops and new tiles grow */
static float grow(float t) {
  t = 1 - t;
  return 1 - t * t * t;
}
static char *num(char *o, uint32_t v) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

/* ------------------------------------------------------------ drawing */
/* A rectangle of the screen is composed in buf, bx1 - bx0 pixels a row, then
   pushed: every pixel is written once, so nothing flickers. */
#define BUF (W * 16)
#define GH 48 /* at most this many rows at a time */
static color buf[BUF];
static int bx0, by0, bx1, by1;

/* Colours spread out as 0x0GGRRBB-ish words, so one multiply blends all three */
#define SPREAD(c) (((c) | (uint32_t)(c) << 16) & 0x07E0F81F)
#ifdef __TICE__ /* the TI-84 Plus CE: its own mixing (the same colours), fills by the C library */
#include <wchar.h>
#include "../../../platform/ti84ce/ce_draw.h"
__attribute__((naked)) static color ce_mix(color a, color b, unsigned t) { __asm__(CE_MIX_ASM); }
__attribute__((naked)) static void ce_blend(color *p, unsigned n, const uint8_t *t) { __asm__(CE_BLEND_ASM); }
static uint8_t tab_mem[CE_TABLES + 255];
static void put(color *p, color c, int v) {
  if (v >= 32) *p = c;
  else if (v > 0) *p = ce_mix(*p, c, (unsigned)v);
}
static void span(color *p, int n, color c) {
  if (n > 0) wmemset((wchar_t *)p, (wchar_t)c, (size_t)n);
}
static void blend(color *p, int n, color c, int v) {
  static color tc;
  static int tv = -1;
  uint8_t *m = (uint8_t *)(((unsigned)tab_mem + 255) & ~255u);
  if (tc != c || tv != v) ce_tables(m, c, (unsigned)v), tc = c, tv = v;
  for (; n > 0; n -= 255, p += 255) ce_blend(p, (unsigned)(n > 255 ? 255 : n), m);
}
#else
/* c over the pixel at p; v from 0 (none) to 32 (all c). Inlined: it runs for
   every smooth or see-through pixel. */
static inline __attribute__((always_inline)) void put(color *p, color c, int v) {
  if (v >= 32) *p = c;
  else if (v > 0) {
    uint32_t y = SPREAD(*p);
    y = (y * (uint32_t)(32 - v) + SPREAD(c) * (uint32_t)v) >> 5 & 0x07E0F81F;
    *p = (color)(y | y >> 16);
  }
}
/* n pixels of c, two at a time */
static void span(color *p, int n, color c) {
  if (n <= 0) return;
  if ((uintptr_t)p & 2) *p++ = c, n--;
  uint32_t cc = c | (uint32_t)c << 16, *q = (uint32_t *)(void *)p;
  for (; n >= 2; n -= 2) *q++ = cc;
  if (n) *(color *)q = c;
}
/* n pixels of c at opacity v over what is there (the washes over the board) */
static void blend(color *p, int n, color c, int v) {
  uint32_t f = SPREAD(c) * (uint32_t)v, k = 32 - (uint32_t)v;
  for (; n > 0; n--, p++) {
    uint32_t y = (SPREAD(*p) * k + f) >> 5 & 0x07E0F81F;
    *p = (color)(y | y >> 16);
  }
}
#endif
static bool hits(float x, float y, float w, float h) { return x < bx1 && x + w > bx0 && y < by1 && y + h > by0; }
static color *row(int y) { return buf + (y - by0) * (bx1 - bx0) - bx0; }

/* coverage of a pixel whose centre is d pixels outside an edge that fades
   over soft pixels (1 for a sharp edge) */
static float soft = 1;
static int cover(float d) {
  int v = (int)((0.5f - d / soft) * 32);
  return v < 0 ? 0 : v > 32 ? 32 : v;
}

/* A rounded box with smooth edges, at any position and size; a: 0..32 */
static void rbox(float x, float y, float w, float h, float r, color c, int a) {
  float o = soft * 0.5f - 0.5f; /* how far a soft edge fades out beyond the box */
  if (w <= 0 || h <= 0 || a <= 0 || !hits(x - o, y - o, w + 2 * o, h + 2 * o)) return;
  float hx = w * 0.5f, hy = h * 0.5f, mx = x + hx, my = y + hy;
  if (r > hx) r = hx;
  if (r > hy) r = hy;
  float ix = hx - r, iy = hy - r;
  int x0 = fl(x - o), x1 = -fl(o - x - w), y0 = fl(y - o), y1 = -fl(o - y - h);
  if (x0 < bx0) x0 = bx0;
  if (x1 > bx1) x1 = bx1;
  if (y0 < by0) y0 = by0;
  if (y1 > by1) y1 = by1;
  for (int j = y0; j < y1; j++) {
    float qy = __builtin_fabsf(j + 0.5f - my) - iy;
    color *p = row(j);
    /* the middle of the row is all alike: all covered between the straight
       sides, or as much as the row between the corners */
    float e = qy <= 0 && r >= 0.5f * soft ? hx - 0.5f * soft : ix;
    int s0 = -fl(e + 0.5f - mx), s1 = fl(mx + e - 0.5f) + 1, rv = cover((qy > -1 ? qy : -1) - r) * a >> 5;
    if (s0 < x0) s0 = x0;
    if (s1 > x1) s1 = x1;
    if (s1 <= s0) s0 = s1 = x1;
    if (rv >= 32) span(p + s0, s1 - s0, c);
    else if (rv > 0) blend(p + s0, s1 - s0, c, rv);
    for (int i = x0; i < x1; i++) { /* the edges */
      if (i == s0) i = s1;
      if (i >= x1) break;
      float qx = __builtin_fabsf(i + 0.5f - mx) - ix, d;
      if (qx > 0 && qy > 0) d = __builtin_sqrtf(qx * qx + qy * qy) - r;
      else d = (qx > qy ? qx : qy) - r;
      put(p + i, c, cover(d) * a >> 5);
    }
  }
}

/* ------------------------------------------------------------ the font */
/* Each letter is a few strokes through points on a grid of half units: x
   from 0, y from 0 (capital height) through 6 (x-height) to 24 (baseline)
   and 30 (descenders). Points are on the stroke, or C: the control point of
   a curve (two in a row imply an on-point halfway, as in TrueType fonts). E
   ends a stroke, Z ends it by closing it, G ends the letter. */
#define P(x, y) ((x) | (y) << 5)
#define C(x, y) (P(x, y) | 1 << 10)
#define E (1 << 11)
#define Z (3 << 11)
#define G (1 << 13 | E)
#define BOWL P(6, 6), C(12, 6), P(12, 15), C(12, 24), P(6, 24), C(0, 24), P(0, 15), C(0, 6) | Z
static const char font_chars[] = "0123456789BCEGJKMNOPQRSTUYabdeghilmnoprstuvwy!?+-'*<>\x7f~"
#if NP_TEXT_EXTRA /* letters other languages need too, then the accents (np_latin's order) and a dotless i */
                                 "ADFHILVWXZcfjkqxz.,:\1\2\3\4\5\6"
#endif
    ;
static const uint16_t font[] = {
  /* 0 */ P(7, 0), C(14, 0), P(14, 8), P(14, 16), C(14, 24), P(7, 24), C(0, 24), P(0, 16), P(0, 8), C(0, 0) | Z | G,
  /* 1 */ P(3, 5), P(9, 0), P(9, 24) | G,
  /* 2 */ P(1, 5), C(1, 0), P(7, 0), C(14, 0), P(14, 7), C(14, 11), P(11, 14), P(0, 24), P(14, 24) | G,
  /* 3 */ P(1, 4), C(2, 0), P(7, 0), C(13, 0), P(13, 6), C(13, 11), P(6, 11), C(14, 11), P(14, 17), C(14, 24),
  P(7, 24), C(1, 24), P(0, 20) | G,
  /* 4 */ P(10, 24), P(10, 0), P(0, 16), P(14, 16) | G,
  /* 5 */ P(13, 0), P(2, 0), P(1, 11), C(3, 8), P(8, 8), C(14, 8), P(14, 16), C(14, 24), P(7, 24), C(1, 24),
  P(0, 20) | G,
  /* 6 */ P(13, 3), C(11, 0), P(7, 0), C(0, 0), P(0, 9), P(0, 16), C(0, 24), P(7, 24), C(14, 24), P(14, 16),
  C(14, 9), P(7, 9), C(0, 9), P(0, 15) | G,
  /* 7 */ P(0, 0), P(14, 0), P(4, 24) | G,
  /* 8 */ P(7, 0), C(13, 0), P(13, 6), C(13, 11), P(7, 11), C(1, 11), P(1, 6), C(1, 0) | Z, P(7, 11), C(14, 11),
  P(14, 18), C(14, 24), P(7, 24), C(0, 24), P(0, 18), C(0, 11) | Z | G,
  /* 9 */ P(1, 21), C(3, 24), P(7, 24), C(14, 24), P(14, 15), P(14, 8), C(14, 0), P(7, 0), C(0, 0), P(0, 8),
  C(0, 15), P(7, 15), C(14, 15), P(14, 9) | G,
  /* B */ P(0, 12), P(8, 12), C(14, 12), P(14, 18), C(14, 24), P(8, 24), P(0, 24), P(0, 0), P(7, 0), C(13, 0),
  P(13, 6), C(13, 12), P(7, 12) | G,
  /* C */ P(15, 4), C(13, 0), P(8, 0), C(0, 0), P(0, 8), P(0, 16), C(0, 24), P(8, 24), C(13, 24), P(15, 20) | G,
  /* E */ P(13, 0), P(0, 0), P(0, 24), P(13, 24) | E, P(0, 12), P(11, 12) | G,
  /* G */ P(15, 4), C(13, 0), P(8, 0), C(0, 0), P(0, 8), P(0, 16), C(0, 24), P(8, 24), C(15, 24), P(15, 16),
  P(15, 13), P(9, 13) | G,
  /* J */ P(10, 0), P(10, 17), C(10, 24), P(5, 24), C(0, 24), P(0, 19) | G,
  /* K */ P(0, 0), P(0, 24) | E, P(13, 0), P(1, 14) | E, P(5, 10), P(14, 24) | G,
  /* M */ P(0, 24), P(0, 0), P(9, 16), P(18, 0), P(18, 24) | G,
  /* N */ P(0, 24), P(0, 0), P(14, 24), P(14, 0) | G,
  /* O */ P(8, 0), C(16, 0), P(16, 8), P(16, 16), C(16, 24), P(8, 24), C(0, 24), P(0, 16), P(0, 8), C(0, 0) | Z | G,
  /* P */ P(0, 24), P(0, 0), P(7, 0), C(14, 0), P(14, 7), C(14, 14), P(7, 14), P(0, 14) | G,
  /* Q */ P(8, 0), C(16, 0), P(16, 8), P(16, 16), C(16, 24), P(8, 24), C(0, 24), P(0, 16), P(0, 8), C(0, 0) | Z,
  P(10, 18), P(16, 25) | G,
  /* R */ P(0, 24), P(0, 0), P(7, 0), C(14, 0), P(14, 7), C(14, 13), P(7, 13), P(0, 13) | E, P(7, 13),
  P(14, 24) | G,
  /* S */ P(14, 4), C(12, 0), P(7, 0), C(0, 0), P(0, 6), C(0, 11), P(7, 12), C(14, 13), P(14, 18), C(14, 24),
  P(7, 24), C(2, 24), P(0, 20) | G,
  /* T */ P(0, 0), P(14, 0) | E, P(7, 0), P(7, 24) | G,
  /* U */ P(0, 0), P(0, 16), C(0, 24), P(7, 24), C(14, 24), P(14, 16), P(14, 0) | G,
  /* Y */ P(0, 0), P(7, 12), P(14, 0) | E, P(7, 12), P(7, 24) | G,
  /* a */ P(1, 9), C(3, 6), P(7, 6), C(12, 6), P(12, 11), P(12, 24) | E, P(12, 15), P(6, 15), C(0, 15), P(0, 20),
  C(0, 24), P(5, 24), C(10, 24), P(12, 21) | G,
  /* b */ P(0, 0), P(0, 24) | E, BOWL | G,
  /* d */ BOWL, P(12, 0), P(12, 24) | G,
  /* e */ P(0, 15), P(12, 15), C(12, 6), P(6, 6), C(0, 6), P(0, 15), C(0, 24), P(6, 24), C(10, 24), P(12, 21) | G,
  /* g */ BOWL, P(12, 6), P(12, 25), C(12, 30), P(6, 30), C(2, 30), P(1, 28) | G,
  /* h */ P(0, 0), P(0, 24) | E, P(0, 13), C(0, 6), P(6, 6), C(12, 6), P(12, 12), P(12, 24) | G,
  /* i */ P(0, 0), P(0, 0) | E, P(0, 7), P(0, 24) | G,
  /* l */ P(0, 0), P(0, 24) | G,
  /* m */ P(0, 6), P(0, 24) | E, P(0, 12), C(0, 6), P(5, 6), C(10, 6), P(10, 12), P(10, 24) | E, P(10, 12),
  C(10, 6), P(15, 6), C(20, 6), P(20, 12), P(20, 24) | G,
  /* n */ P(0, 6), P(0, 24) | E, P(0, 12), C(0, 6), P(6, 6), C(12, 6), P(12, 12), P(12, 24) | G,
  /* o */ BOWL | G,
  /* p */ BOWL, P(0, 6), P(0, 30) | G,
  /* r */ P(0, 6), P(0, 24) | E, P(0, 14), C(0, 6), P(8, 6) | G,
  /* s */ P(11, 8), C(10, 6), P(6, 6), C(1, 6), P(1, 10), C(1, 14), P(6, 15), C(12, 16), P(12, 20), C(12, 24),
  P(6, 24), C(1, 24), P(0, 22) | G,
  /* t */ P(3, 1), P(3, 20), C(3, 24), P(8, 24) | E, P(0, 6), P(9, 6) | G,
  /* u */ P(0, 6), P(0, 18), C(0, 24), P(6, 24), C(12, 24), P(12, 18) | E, P(12, 6), P(12, 24) | G,
  /* v */ P(0, 6), P(6, 24), P(12, 6) | G,
  /* w */ P(0, 6), P(4, 24), P(9, 10), P(14, 24), P(18, 6) | G,
  /* y */ P(0, 6), P(6, 24) | E, P(12, 6), P(5, 27), C(4, 30), P(1, 30) | G,
  /* ! */ P(0, 0), P(0, 16) | E, P(0, 24), P(0, 24) | G,
  /* ? */ P(0, 5), C(1, 0), P(6, 0), C(12, 0), P(12, 6), C(12, 11), P(6, 12), P(6, 16) | E, P(6, 24), P(6, 24) | G,
  /* + */ P(0, 12), P(12, 12) | E, P(6, 6), P(6, 18) | G,
  /* - */ P(0, 14), P(8, 14) | G,
  /* ' */ P(0, 0), P(0, 6) | G,
  /* * (times) */ P(0, 7), P(10, 17) | E, P(10, 7), P(0, 17) | G,
  /* < */ P(8, 4), P(0, 12), P(8, 20) | G,
  /* > */ P(0, 4), P(8, 12), P(0, 20) | G,
  /* the Backspace key */ P(6, 4), P(24, 4), P(24, 20), P(6, 20), P(0, 12) | Z, P(11, 8), P(18, 16) | E, P(18, 8),
  P(11, 16) | G,
  /* the Back key */ P(10, 4), C(17, 4), P(17, 9), C(17, 14), P(10, 14), P(1, 14) | E, P(5, 10), P(1, 14),
  P(5, 18) | G,
#if NP_TEXT_EXTRA
  /* A */ P(0, 24), P(7, 0), P(14, 24) | E, P(2, 17), P(12, 17) | G,
  /* D */ P(0, 0), P(0, 24), P(7, 24), C(15, 24), P(15, 16), P(15, 8), C(15, 0), P(7, 0) | Z | G,
  /* F */ P(13, 0), P(0, 0), P(0, 24) | E, P(0, 12), P(11, 12) | G,
  /* H */ P(0, 0), P(0, 24) | E, P(14, 0), P(14, 24) | E, P(0, 12), P(14, 12) | G,
  /* I */ P(0, 0), P(0, 24) | G,
  /* L */ P(0, 0), P(0, 24), P(12, 24) | G,
  /* V */ P(0, 0), P(7, 24), P(14, 0) | G,
  /* W */ P(0, 0), P(5, 24), P(10, 5), P(15, 24), P(20, 0) | G,
  /* X */ P(0, 0), P(14, 24) | E, P(14, 0), P(0, 24) | G,
  /* Z */ P(0, 0), P(14, 0), P(0, 24), P(14, 24) | G,
  /* c */ P(12, 9), C(10, 6), P(6, 6), C(0, 6), P(0, 15), C(0, 24), P(6, 24), C(10, 24), P(12, 21) | G,
  /* f */ P(10, 1), C(9, 0), P(7, 0), C(3, 0), P(3, 5), P(3, 24) | E, P(0, 6), P(9, 6) | G,
  /* j */ P(4, 0), P(4, 0) | E, P(4, 7), P(4, 27), C(4, 30), P(1, 30) | G,
  /* k */ P(0, 0), P(0, 24) | E, P(11, 6), P(1, 17) | E, P(5, 13), P(12, 24) | G,
  /* q */ BOWL, P(12, 6), P(12, 30) | G,
  /* x */ P(0, 6), P(12, 24) | E, P(12, 6), P(0, 24) | G,
  /* z */ P(0, 6), P(12, 6), P(0, 24), P(12, 24) | G,
  /* . */ P(0, 24), P(0, 24) | G,
  /* , */ P(1, 24), P(0, 28) | G,
  /* : */ P(0, 10), P(0, 10) | E, P(0, 24), P(0, 24) | G,
  /* the accents, over a letter 12 wide (raised over it), and the cedilla under it */
  /* acute */ P(8, 0), P(4, 4) | G,
  /* grave */ P(4, 0), P(8, 4) | G,
  /* circumflex */ P(2, 4), P(6, 0), P(10, 4) | G,
  /* diaeresis */ P(3, 2), P(3, 2) | E, P(9, 2), P(9, 2) | G,
  /* cedilla */ P(6, 24), P(7, 27), C(8, 30), P(4, 30) | G,
  /* dotless i */ P(0, 7), P(0, 24) | G,
#endif
};
#define NCH (int)(sizeof font_chars - 1)
static uint16_t font_at[NCH];
static uint8_t font_w[NCH]; /* in half units; digits all get the same width */
#define TRACK 1.0f          /* space between letters, in units */

static int8_t font_of[96]; /* the letter for each character from ' ', or -1 */
static int gi(char c) { return (unsigned)(c - 32) < 96 ? font_of[c - 32] : -1; }
static float advance(char c, float wt) {
  int i = gi(c);
  return i < 0 ? 3.6f : font_w[i] * 0.5f + wt + TRACK;
}
/* the width of s, in units */
static float tw(const char *s, float wt) {
  float w = -TRACK;
  for (; *s; s++) w += advance(*s, wt);
  return w;
}

#if NP_TEXT_EXTRA
/* Other languages: a letter beyond ASCII is one of the font's with an accent (g, acc; a ligature's
   second letter g2), or a Chinese one (cp) from the 12-pixel font, at a whole scale near the text's size. */
typedef struct {
  int g, g2, acc, cap;
  uint32_t cp;
} letter_t;
static letter_t next_letter(const char **s) {
  letter_t l = {-1, -1, 0, 0, np_utf8(s)};
  char b = (char)l.cp, b2 = 0;
  if (l.cp >= 0x80) {
    l.acc = np_latin(l.cp, &b, &b2);
    if (!b) return l;
  }
  l.g = gi(b), l.g2 = b2 ? gi(b2) : -1, l.cap = b >= 'A' && b <= 'Z', l.cp = (uint8_t)b;
  if (b == 'i' && l.acc) l.g = NCH - 1; /* no dot under the accent */
  return l;
}
static int xscale(float u, float wt) {
  int k = (int)((12 + wt) * u / 12 + 0.5f);
  return k < 1 ? 1 : k;
}
static float l_adv(letter_t l, float u, float wt) {
  if (l.cp >= 0x80) return np_xadvance(l.cp, xscale(u, wt)) / u;
  float a = l.g < 0 ? 3.6f : font_w[l.g] * 0.5f + wt + TRACK;
  return l.g2 < 0 ? a : a + font_w[l.g2] * 0.5f + wt + TRACK;
}
static float tw_u(const char *s, float u, float wt) {
  float w = -TRACK;
  while (*s) w += l_adv(next_letter(&s), u, wt);
  return w;
}
#else
#define tw_u(s, u, wt) tw(s, wt)
#endif

static void font_init(void) {
  for (int i = 0, k = 0; i < NCH; i++) { /* where each letter starts, and its width */
    font_at[i] = (uint16_t)k;
    int m = 0;
    do
      if ((font[k] & 31) > m) m = font[k] & 31;
    while (!(font[k++] & 1 << 13));
    font_w[i] = (uint8_t)(i < 10 ? 14 : m);
  }
  for (int c = 0; c < 96; c++) {
    font_of[c] = -1;
    for (int i = 0; i < NCH; i++)
      if (font_chars[i] == c + 32) font_of[c] = (int8_t)i;
  }
}

/* The pen draws into a coverage mask (0..32), mw x mh pixels at (mx0, my0);
   each round-ended segment keeps the higher coverage, which is exactly the
   coverage of the whole stroke. */
static uint8_t gm[NP_TEXT_EXTRA ? BUF : 4096], *mk;
static int mx0, my0, mw, mh, nseg;
static float pen;
#if NP_TEXT_EXTRA
static void mplot(int x, int y, void *ctx) { /* a pixel of a Chinese letter */
  if (x >= mx0 && x < mx0 + mw && y >= my0 && y < my0 + mh) mk[(y - my0) * mw + x - mx0] = 32;
}
#endif

#ifdef __TICE__
/* the TI-84 Plus CE: a stroke in whole numbers, its distances growing by the same steps from pixel to pixel (in
   256ths of a pixel; t, how far along, in 65536ths): float only once a stroke */
static int isqrt(int v) { /* (rounded down) */
  int r = 0, b = 1 << 22;
  while (b > v) b >>= 2;
  for (; b; b >>= 2)
    if (v >= r + b) v -= r + b, r = (r >> 1) + b;
    else r >>= 1;
  return r;
}
static void seg_ce(float ax, float ay, float bx, float by, int x0, int x1, int y0, int y1) {
  float ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey;
  if (l2 < 1e-4f) ex = 1, ey = 0, l2 = 1e9f; /* (a dot: its end caps) */
  float il = 1 / __builtin_sqrtf(l2), qx = x0 + 0.5f - ax, qy = y0 + 0.5f - ay;
  int ro = (int)((pen + 0.5f) * 256), ri = (int)((pen - 0.5f) * 256);
  int dp = (int)((qx * ey - qy * ex) * il * 256), dpi = (int)(ey * il * 256), dpj = (int)(-ex * il * 256);
  int tt = (int)((qx * ex + qy * ey) / l2 * 65536), tti = (int)(ex / l2 * 65536), ttj = (int)(ey / l2 * 65536);
  int ax8 = (int)(ax * 256), ay8 = (int)(ay * 256), bx8 = (int)(bx * 256), by8 = (int)(by * 256);
  int ro2 = ro >> 4, ri2 = ri > 0 ? ri >> 4 : -1;
  ro2 *= ro2, ri2 = ri2 > 0 ? ri2 * ri2 : -1; /* (squares in 16ths) */
  for (int j = y0; j < y1; j++, dp += dpj, tt += ttj) {
    uint8_t *m = mk + (j - my0) * mw - mx0;
    int d = dp, t = tt, py = j * 256 + 128, i0 = x0, i1 = x1;
    if (dpi) { /* only the pixels less than ro across from its line */
      int a = (-ro - dp) / dpi, b = (ro - dp) / dpi;
      if (a > b) {
        int c = a;
        a = b, b = c;
      }
      if (x0 + a - 1 > i0) i0 = x0 + a - 1;
      if (x0 + b + 2 < i1) i1 = x0 + b + 2;
      d += dpi * (i0 - x0), t += tti * (i0 - x0);
    } else if (dp >= ro || dp <= -ro) {
      continue;
    }
    for (int i = i0; i < i1; i++, d += dpi, t += tti) {
      int v;
      if (t > 0 && t < 65536) { /* beside it */
        int a = d < 0 ? -d : d;
        if (a >= ro) continue;
        v = a <= ri ? 32 : (ro - a) >> 3;
      } else { /* round, past an end */
        int ux = (i * 256 + 128 - (t <= 0 ? ax8 : bx8)) >> 4, uy = (py - (t <= 0 ? ay8 : by8)) >> 4, q = ux * ux + uy * uy;
        if (q >= ro2) continue;
        v = q <= ri2 ? 32 : (ro - isqrt(q) * 16) >> 3;
      }
      if (v > m[i]) m[i] = (uint8_t)v;
    }
  }
}
#endif
static void seg(float ax, float ay, float bx, float by) {
  float ro = pen + 0.5f, ri = pen - 0.5f, ro2 = ro * ro, ri2 = ri > 0 ? ri * ri : -1;
  int x0 = fl((ax < bx ? ax : bx) - ro), x1 = fl((ax > bx ? ax : bx) + ro) + 1;
  int y0 = fl((ay < by ? ay : by) - ro), y1 = fl((ay > by ? ay : by) + ro) + 1;
  if (x0 < mx0) x0 = mx0;
  if (x1 > mx0 + mw) x1 = mx0 + mw;
  if (y0 < my0) y0 = my0;
  if (y1 > my0 + mh) y1 = my0 + mh;
#ifdef __TICE__
  if (x1 > x0 && y1 > y0) seg_ce(ax, ay, bx, by, x0, x1, y0, y1);
  return;
#endif
  float ex = bx - ax, ey = by - ay, l2 = ex * ex + ey * ey, inv = l2 > 1e-4f ? 1 / l2 : 0;
  for (int j = y0; j < y1; j++) {
    uint8_t *m = mk + (j - my0) * mw - mx0;
    for (int i = x0; i < x1; i++) {
      float dx = i + 0.5f - ax, dy = j + 0.5f - ay, t = (dx * ex + dy * ey) * inv;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      dx -= t * ex, dy -= t * ey;
      float d2 = dx * dx + dy * dy;
      if (d2 >= ro2) continue;
      int v = d2 <= ri2 ? 32 : (int)((ro - __builtin_sqrtf(d2)) * 32);
      if (v > m[i]) m[i] = (uint8_t)v;
    }
  }
}

/* the strokes of letter g, its grid's origin at (ox, oy), h pixels a half unit */
static void strokes(const uint16_t *g, float ox, float oy, float h) {
#define PX(p) (ox + ((p) & 31) * h)
#define PY(p) (oy + ((p) >> 5 & 31) * h)
  for (;;) {
    const uint16_t *s = g;
    while (!(*g & E)) g++;
    int n = (int)(g - s) + 1, closed = (*g >> 12) & 1;
    float px = PX(s[0]), py = PY(s[0]);
    for (int i = 1; i < n + closed; i++) {
      uint16_t p = s[i % n];
      float x = PX(p), y = PY(p);
      if (!(p & 1 << 10)) {
        seg(px, py, x, y);
      } else { /* a curve through control point p, flattened */
        uint16_t q = s[(i + 1) % n];
        float ex = PX(q), ey = PY(q);
        if (q & 1 << 10) ex = (ex + x) * 0.5f, ey = (ey + y) * 0.5f;
        else i++;
        for (int k = 1; k <= nseg; k++) {
          float t = (float)k / nseg, u = 1 - t;
          float cx = u * u * px + 2 * u * t * x + t * t * ex, cy = u * u * py + 2 * u * t * y + t * t * ey;
          seg(px, py, cx, cy);
          px = cx, py = cy;
        }
        x = ex, y = ey;
      }
      px = x, py = y;
    }
    if (*g++ & 1 << 13) return;
  }
}

/* s into the mask (cleared first), its left edge at x and the top of its
   capitals at y; u pixels a unit, wt the pen's width in units */
static void raster(const char *s, float x, float y, float u, float wt) {
  for (int i = 0; i < mw * mh; i++) mk[i] = 0;
  pen = wt * u * 0.5f;
  nseg = 2 + (int)(u * 1.5f);
  if (nseg > 8) nseg = 8;
#if NP_TEXT_EXTRA
  for (float h = u * 0.5f; *s;) {
    letter_t l = next_letter(&s);
    if (l.cp >= 0x80) { /* Chinese, in the middle of the capitals' height */
      int k = xscale(u, wt);
      np_xdraw(l.cp, fl(x + 0.5f), fl(y + ((12 + wt) * u - 12 * k) * 0.5f + 0.5f), k, mplot, 0);
    } else if (l.g >= 0) {
      strokes(font + font_at[l.g], x + pen, y + pen, h);
      if (l.acc >= NP_ACC_ACUTE && l.acc <= NP_ACC_CEDIL) /* over a lowercase letter, higher over a capital */
        strokes(font + font_at[NCH - 7 + l.acc], x + pen + (font_w[l.g] - 12) * 0.5f * h,
                y + pen - (l.acc == NP_ACC_CEDIL ? 0 : l.cap ? 10 : 6) * h, h);
      if (l.g2 >= 0) strokes(font + font_at[l.g2], x + pen + (font_w[l.g] * 0.5f + wt + TRACK) * u, y + pen, h);
    }
    x += l_adv(l, u, wt) * u;
  }
#else
  for (; *s; s++) {
    int i = gi(*s);
    if (i >= 0) strokes(font + font_at[i], x + pen, y + pen, u * 0.5f);
    x += advance(*s, wt) * u;
  }
#endif
}

/* a w x h mask with its corner at (x, y), in colour c, opacity a (0..32) */
static void blit(const uint8_t *m, int x, int y, int w, int h, color c, int a) {
  for (int j = y < by0 ? by0 : y; j < y + h && j < by1; j++) {
    color *p = row(j);
    const uint8_t *q = m + (j - y) * w - x;
    for (int i = x < bx0 ? bx0 : x; i < x + w && i < bx1; i++)
      if (q[i]) put(p + i, c, q[i] * a >> 5);
  }
}

/* s drawn as for raster, in colour c, opacity a (0..32) */
static void text(const char *s, float x, float y, float u, float wt, color c, int a) {
  float w = tw_u(s, u, wt) * u, h = (15 + wt) * u, t = y;
  if (NP_TEXT_EXTRA) t -= 7 * u + 2, h += 7 * u + 4; /* accents over capitals, Chinese letters */
  if (a <= 0 || !hits(x, t, w, h)) return;
#ifdef __TICE__ /* (the TI-84 Plus CE: each text's whole mask made once, its strokes being float sums a pixel) */
  static uint8_t pool[16384];
  static struct {
    float x, y, u, wt;
    char s[24];
    uint16_t at;
    int16_t x0, y0, w, h;
  } kept[32];
  static int used, nkept;
  int n = 0, X = fl(x), Y = fl(y);
  while (s[n] && n < 24) n++;
  x = X + fl((x - X) * 8 + 0.5f) * 0.125f, y = Y + fl((y - Y) * 8 + 0.5f) * 0.125f, t = y; /* (to an eighth of a pixel) */
  X = fl(x), Y = fl(y);
  float fx = x - X, fy = y - Y;
  for (int i = 0; i < nkept; i++)
    if (kept[i].x == fx && kept[i].y == fy && kept[i].u == u && kept[i].wt == wt && !memcmp(kept[i].s, s, (size_t)n) &&
        (n == 24 || !kept[i].s[n])) {
      blit(pool + kept[i].at, X + kept[i].x0, Y + kept[i].y0, kept[i].w, kept[i].h, c, a);
      return;
    }
  int x0 = fl(x), y0 = fl(t), mw_ = fl(x + w) + 1 - x0, mh_ = fl(t + h) + 1 - y0;
  if (n < 24 && mw_ * mh_ <= (int)sizeof pool) {
    if (used + mw_ * mh_ > (int)sizeof pool || nkept == 32) used = nkept = 0;
    mx0 = x0, my0 = y0, mw = mw_, mh = mh_, mk = pool + used;
    raster(s, x, y, u, wt);
    kept[nkept].x = fx, kept[nkept].y = fy, kept[nkept].u = u, kept[nkept].wt = wt;
    memset(kept[nkept].s, 0, sizeof kept[nkept].s), memcpy(kept[nkept].s, s, (size_t)n);
    kept[nkept].at = (uint16_t)used, kept[nkept].x0 = (int16_t)(x0 - X), kept[nkept].y0 = (int16_t)(y0 - Y);
    kept[nkept].w = (int16_t)mw_, kept[nkept].h = (int16_t)mh_;
    nkept++, used += mw_ * mh_;
    blit(mk, x0, y0, mw_, mh_, c, a);
    return;
  }
#endif
  mx0 = fl(x), my0 = fl(t), mw = fl(x + w) + 1, mh = fl(t + h) + 1;
  if (mx0 < bx0) mx0 = bx0;
  if (my0 < by0) my0 = by0;
  if (mw > bx1) mw = bx1;
  if (mh > by1) mh = by1;
  mw -= mx0, mh -= my0;
  if (mw * mh > (int)sizeof gm) mh = (int)sizeof gm / mw; /* never with this game's text */
  mk = gm;
  raster(s, x, y, u, wt);
  blit(gm, mx0, my0, mw, mh, c, a);
}
/* centred on (cx, cy), the middle of the capitals */
static void ctext(const char *s, float cx, float cy, float u, float wt, color c, int a) {
  text(s, cx - tw_u(s, u, wt) * u * 0.5f, cy - (12 + wt) * u * 0.5f, u, wt, c, a);
}

/* ------------------------------------------------------------ the game */
static int n = 4;                    /* the board is n x n */
static uint8_t cell[36], ucell[36];  /* 2 to the power of each, 0 when empty */
static uint32_t score, uscore, moves, umoves;
static bool can_undo, won;

/* the last move, for its animation: every tile's slide, then what merged (1)
   or appeared (2) where */
static uint8_t sl_from[36], sl_to[36], sl_e[36], fx[36];
static int nsl, lines;       /* lines: the rows or columns that moved */
static bool anim, slid, dir_rows; /* slid: the tiles reached their cells; dir_rows: left or right */
static float slide_t;             /* how far the tiles were drawn in their slide, 0..1 */
static uint32_t t_move, t_add, added;
#define SLIDE 100
#define POP 200
#define RISE 600

static int at(int dir, int l, int k) { /* the k-th cell of line l, from the side the tiles go to */
  switch (dir) {
    case 0: return l * n + k;
    case 3: return l * n + n - 1 - k;
    case 1: return k * n + l;
    default: return (n - 1 - k) * n + l;
  }
}

static void spawn(void) {
  int empty = 0;
  for (int i = 0; i < n * n; i++) empty += !cell[i];
  if (!empty) return;
  int k = (int)(rnd() % (uint32_t)empty);
  for (int i = 0; i < n * n; i++)
    if (!cell[i] && !k--) {
      cell[i] = rnd() % 10 ? 1 : 2; /* a 4 one time in ten */
      fx[i] = 2;
      return;
    }
}

static bool can_move(void) {
  for (int r = 0; r < n; r++)
    for (int c = 0; c < n; c++) {
      int e = cell[r * n + c]; /* 2^30 is as far as tiles go: two of them stay apart */
      if (!e || (e < 30 && ((c + 1 < n && cell[r * n + c + 1] == e) || (r + 1 < n && cell[(r + 1) * n + c] == e))))
        return true;
    }
  return false;
}

/* slides everything towards dir (0 left, 1 up, 2 down, 3 right); false if
   nothing moved. Returns the merged tiles' highest power in *top. */
static bool move(int dir, int *top) {
  uint8_t nc[36] = {0}, nfx[36] = {0}, from[36], to[36], es[36]; /* kept only if something moves */
  uint32_t gain = 0;
  int ns = 0, ls = 0;
  *top = 0;
  for (int l = 0; l < n; l++) {
    int k = 0, last = -1; /* last: where the previous tile went, while it can still merge */
    for (int j = 0; j < n; j++) {
      int s = at(dir, l, j), e = cell[s], d;
      if (!e) continue;
      if (last >= 0 && nc[last] == e && e < 30) {
        d = last, nc[d] = (uint8_t)(e + 1), nfx[d] = 1, gain += 2u << e, last = -1;
        if (e + 1 > *top) *top = e + 1;
      } else {
        d = at(dir, l, k++), nc[d] = (uint8_t)e, last = d;
      }
      from[ns] = (uint8_t)s, to[ns] = (uint8_t)d, es[ns++] = (uint8_t)e;
      if (s != d) ls |= 1 << l;
    }
  }
  if (!ls) return false;
  for (int i = 0; i < 36; i++) ucell[i] = cell[i], cell[i] = nc[i], fx[i] = nfx[i];
  for (int i = 0; i < ns; i++) sl_from[i] = from[i], sl_to[i] = to[i], sl_e[i] = es[i];
  uscore = score, umoves = moves, can_undo = true;
  score = score + gain < score ? 0xFFFFFFFFu : score + gain;
  moves++;
  nsl = ns, lines = ls, dir_rows = dir == 0 || dir == 3;
  if (gain) added = gain, t_add = now;
  useed = seed; /* Undo puts it back: the same move then brings the same tile */
  spawn();
  /* one frame in already: the frame of the key press shows the tiles moving */
  anim = true, slid = false, slide_t = 0, t_move = now - 16;
  return true;
}

/* ------------------------------------------------------------ saving */
/* g2048.sav: the size last played, and for each size the best score and the
   game in progress */
static struct {
  uint8_t magic, version, size, flags[4], pad; /* flags: 1 a game to continue, 2 won already */
  uint32_t best[4], score[4], moves[4];
  uint8_t cells[86];
} sv;
static const uint8_t cells_at[4] = {0, 9, 25, 50};
#define SAVE_NAME "g2048.sav"

static void check_save(void);
static void load(void) {
  uint32_t len = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &len);
  uint8_t *v = (uint8_t *)&sv;
  bool ok = d && len == sizeof sv && d[0] == '2' && d[1] == 1;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = ok ? d[i] : 0;
  if (!ok) sv.size = 1; /* 4 x 4 */
  check_save();
}

/* whatever sv holds, makes it a save this game could have written */
static void check_save(void) {
  sv.size &= 3;
  for (int s = 0; s < 4; s++) {
    int tiles = 0, k = s + 3;
    sv.flags[s] &= 3;
    for (int i = 0; i < k * k; i++) {
      uint8_t *e = &sv.cells[cells_at[s] + i];
      if (*e > 30) sv.flags[s] = 0; /* not a board this game made */
      tiles += *e != 0;
    }
    if (!tiles) sv.flags[s] &= 2;
    if (!(sv.flags[s] & 1))
      for (int i = 0; i < k * k; i++) sv.cells[cells_at[s] + i] = 0;
    if (sv.best[s] < sv.score[s]) sv.best[s] = sv.score[s];
  }
}

static void save(void) {
  sv.magic = '2', sv.version = 1;
  ef_write(SAVE_NAME, &sv, sizeof sv);
}

/* ------------------------------------------------------------ screens */
enum { O_NONE, O_WIN, O_OVER, O_PAUSE, O_QUIT };
static int scene;          /* 0 the title, 1 the game */
static int ov, ov_under;   /* the message over the board, and the one under the pause menu */
static int sel, tsel;      /* the button chosen in the message, on the title */
static uint32_t ov_t;      /* when the message starts to show */
static bool over;

typedef struct {
  int x, y, n, cell, gap, size;
  float r, tr; /* the board's and the tiles' corners */
} geo_t;
static geo_t gb;

static geo_t board_geo(int k, int cx, int cy, int room) {
  geo_t g;
  g.n = k;
  g.gap = room > 150 ? (k <= 4 ? 8 : 6) : (k <= 4 ? 4 : 3);
  g.cell = (room - (k + 1) * g.gap) / k;
  g.size = k * g.cell + (k + 1) * g.gap;
  g.x = cx - g.size / 2, g.y = cy - g.size / 2;
  g.r = g.gap * 0.4f + 1, g.tr = g.cell / 32.0f + 1; /* crisp, nearly square, like Cirulli's */
  return g;
}
static float cell_x(const geo_t *g, int i) { return (float)(g->x + g->gap + (i % g->n) * (g->cell + g->gap)); }
static float cell_y(const geo_t *g, int i) { return (float)(g->y + g->gap + (i / g->n) * (g->cell + g->gap)); }

/* The numbers on the game's tiles are drawn once each (for the board's
   size) and kept as masks: tiles at rest or sliding are always on whole
   pixels, so the masks are exact. */
static uint8_t tcm[12288];
static uint16_t tc_at[32]; /* where 2^e's mask starts in tcm, plus 1 (0: not drawn yet) */
static uint8_t tc_x[32], tc_y[32], tc_w[32], tc_h[32];
static int tc_used;

/* the tile 2^e in the cell at (x, y), scaled by s around its middle */
static void tile(const geo_t *g, int e, float x, float y, float s, int a) {
  float c = (float)g->cell, h = c * s, o = (c - h) * 0.5f;
  if (e <= 0 || h < 1 || !hits(x + o - g->gap, y + o - g->gap, h + 2 * g->gap, h + 2 * g->gap)) return;
  if (e >= 7 && e <= 11) { /* from 128 to 2048, a golden glow that fades out halfway to the next tile */
    float m = g->gap * 0.25f * s;
    soft = 2 * m;
    rbox(x + o - m, y + o - m, h + 2 * m, h + 2 * m, g->tr * s + m, RGB(0xF3D774), (e - 3) * 2 * a >> 5);
    soft = 1;
  }
  rbox(x + o, y + o, h, h, g->tr * s, tile_bg[e > 12 ? 11 : e - 1], a);
  if (g->cell < 16 || s < 0.5f) return; /* a tile still too small for its number */
  color col = e <= 2 ? INK : LIGHT;
  bool keep = s == 1 && g == &gb;
#ifdef __TICE__ /* (the TI-84 Plus CE: growing or popping, the number's kept mask scaled, not drawn again) */
  if (!keep && g == &gb && tc_at[e]) {
    int w = (int)(tc_w[e] * s + 0.5f), hh = (int)(tc_h[e] * s + 0.5f);
    float mx = tc_x[e] + tc_w[e] * 0.5f - c * 0.5f, my = tc_y[e] + tc_h[e] * 0.5f - c * 0.5f;
    int x0 = fl(x + c * 0.5f + mx * s - w * 0.5f + 0.5f), y0 = fl(y + c * 0.5f + my * s - hh * 0.5f + 0.5f);
    if (w < 1 || hh < 1 || w > 255) return;
    uint8_t col_of[256];
    for (int i = 0; i < w; i++) col_of[i] = (uint8_t)(i * tc_w[e] / w);
    const uint8_t *m = tcm + tc_at[e] - 1;
    for (int j = y0 < by0 ? by0 : y0; j < y0 + hh && j < by1; j++) {
      color *p = row(j);
      const uint8_t *q = m + (j - y0) * tc_h[e] / hh * tc_w[e];
      for (int i = x0 < bx0 ? bx0 : x0; i < x0 + w && i < bx1; i++)
        if (q[col_of[i - x0]]) put(p + i, col, q[col_of[i - x0]] * a >> 5);
    }
    return;
  }
#endif
  if (!(keep && tc_at[e])) {
    /* Cirulli's sizes for 1, 2, 3, 4... digits; small tiles have no room to
       spare, so there every number is as big as it fits */
    static const float big[] = {0.368f, 0.368f, 0.368f, 0.301f, 0.234f, 0.2f, 0.18f, 0.16f, 0.14f, 0.13f, 0.12f};
    char str[12];
    int len = (int)(num(str, 1u << e) - str);
    float w = tw(str, WT), u = big[g->cell < 40 ? 2 : len] * h / (12 + WT), fit = h * 0.86f / w;
    if (u > fit) u = fit;
    float tx = (c - w * u) * 0.5f, ty = (c - (12 + WT) * u) * 0.5f; /* from the cell's corner */
    mx0 = fl(tx), my0 = fl(ty), mw = fl(tx + w * u) + 1 - mx0, mh = fl(ty + (12 + WT) * u) + 1 - my0;
    if (!keep || tc_used + mw * mh > (int)sizeof tcm) {
      text(str, x + tx, y + ty, u, WT, col, a);
      return;
    }
    mk = tcm + tc_used;
    raster(str, tx, ty, u, WT);
    tc_at[e] = (uint16_t)(tc_used + 1), tc_used += mw * mh;
    tc_x[e] = (uint8_t)mx0, tc_y[e] = (uint8_t)my0, tc_w[e] = (uint8_t)mw, tc_h[e] = (uint8_t)mh;
  }
  blit(tcm + tc_at[e] - 1, (int)x + tc_x[e], (int)y + tc_y[e], tc_w[e], tc_h[e], col, a);
}

/* a board: cells, with the last move's animation when it is the game's */
static void board(const geo_t *g, const uint8_t *cells, bool live) {
  if (!hits((float)g->x, (float)g->y, (float)g->size, (float)g->size)) return;
  rbox((float)g->x, (float)g->y, (float)g->size, (float)g->size, g->r, BOARD, 32);
  int k = g->n * g->n;
  uint32_t dt = now - t_move;
  bool sliding = live && anim && dt < SLIDE;
  uint8_t hide[36]; /* the cells under a tile at rest hide theirs */
  for (int i = 0; i < k; i++) hide[i] = !sliding && cells[i] && !(live && anim && fx[i]);
  for (int j = 0; sliding && j < nsl; j++) hide[sl_from[j]] |= sl_from[j] == sl_to[j];
  for (int i = 0; i < k; i++)
    if (!hide[i]) rbox(cell_x(g, i), cell_y(g, i), (float)g->cell, (float)g->cell, g->tr, SLOT, 32);
  if (sliding) {
    float t = ease((float)dt / SLIDE);
    for (int j = 0; j < nsl; j++) {
      float x0 = cell_x(g, sl_from[j]), y0 = cell_y(g, sl_from[j]); /* on whole pixels */
      tile(g, sl_e[j], (float)fl(x0 + (cell_x(g, sl_to[j]) - x0) * t + 0.5f),
           (float)fl(y0 + (cell_y(g, sl_to[j]) - y0) * t + 0.5f), 1, 32);
    }
    return;
  }
  float t = live && anim ? (float)(dt - SLIDE) / POP : 1;
  for (int i = 0; i < k; i++) {
    float x = cell_x(g, i), y = cell_y(g, i);
    if (t >= 1 || !fx[i]) {
      tile(g, cells[i], x, y, 1, 32);
    } else if (fx[i] == 1) { /* the two tiles that merged, then the new one pops out of them */
      tile(g, cells[i] - 1, x, y, 1, 32);
      tile(g, cells[i], x, y, t < 0.5f ? 1.2f * grow(t * 2) : 1.2f - 0.2f * ease(t * 2 - 1), 32);
    } else {
      float s = grow(t);
      tile(g, cells[i], x, y, s, (int)(s * 32));
    }
  }
}

static void score_box(float x, float y, float w, float h, const char *label, uint32_t v) {
  char s[12];
  rbox(x, y, w, h, 3, BOARD, 32);
  ctext(label, x + w * 0.5f, y + 10, 0.6f, 2.8f, LABEL, 32);
  num(s, v);
  float u = 1.05f, fit = (w - 10) / tw(s, WT);
  ctext(s, x + w * 0.5f, y + h - 12, u < fit ? u : fit, WT, LIGHT, 32);
}

static void button(float cx, float y, float w, float h, const char *s, bool on, int a) {
  rbox(cx - w * 0.5f, y, w, h, 3, on ? BROWN : RGB(0xDDD3C8), a);
  ctext(s, cx, y + h * 0.5f, 0.85f, 2.1f, on ? LIGHT : INK, a);
}

/* game screen: the left column */
#define PX0 6
#define PW 76
#define SCORE_Y 62
#define BEST_Y 108
#define UNDO_Y 172
#define MENU_Y 204

/* how far the message has faded in, 0..32: quickly for the menus, slowly
   for You win! and Game over!, as in the original */
static int fade(void) {
  int32_t d = (int32_t)(now - ov_t), len = ov >= O_PAUSE ? 120 : 500;
  return d <= 0 ? 0 : d >= len ? 32 : d * 32 / len;
}
static int nbuttons(void) { return ov == O_PAUSE ? 3 : ov == O_OVER && !can_undo ? 1 : 2; }
/* the middle of a gap between two rows of the board (k rows below the middle
   one): there the message's title and buttons cover no tile's number */
static float gap_y(int k) { return gb.y + (n / 2 + k) * (gb.cell + gb.gap) + gb.gap * 0.5f; }

static void message(void) {
  static const char *const titles[] = {"", T("You win!"), T("Game over!"), T("Paused"), T("Quit game?")};
  static const char *const labels[][3] = {{0}, {T("Keep going"), T("Try again")}, {T("Try again"), T("Undo")},
                                          {T("Resume"), T("New game"), T("Quit game")}, {T("No"), T("Yes")}};
  int f = fade(), nb = nbuttons();
  if (!ov || !f) return;
  float x = (float)gb.x, y = (float)gb.y, b = (float)gb.size, cx = x + b * 0.5f;
  rbox(x, y, b, b, gb.r, ov == O_WIN ? RGB(0xEDC22E) : LABEL, (ov == O_WIN ? 20 : ov == O_OVER ? 27 : 29) * f >> 5);
  ctext(titles[ov], cx, ov == O_PAUSE ? y + b * 0.2f : gap_y(0), b / 124, WT, ov == O_WIN ? LIGHT : INK, f);
  for (int i = 0; i < nb; i++) {
    if (ov == O_PAUSE) button(cx, y + b * 0.36f + i * 36, 132, 28, labels[ov][i], sel == i, f);
    else button(cx + (nb == 1 ? 0 : (i * 2 - 1) * 50), gap_y(1) - 14, 92, 28, labels[ov][i], sel == i || nb == 1, f);
  }
}

static uint32_t best0; /* the best score when this game started */
static bool nbest;     /* "New best!" is up */

static void game_screen(void) {
  char s[12];
  text("2048", PX0 + 3, 8, 1.72f, WT, INK, 32);
  score_box(PX0, SCORE_Y, PW, 40, T("SCORE"), score);
  score_box(PX0, BEST_Y, PW, 40, T("BEST"), sv.best[n - 3]);
  if (nbest) ctext(T("New best!"), PX0 + PW * 0.5f, BEST_Y + 52, 0.62f, 2.6f, RGB(0xF65E3B), 32);
  button(PX0 + PW * 0.5f, UNDO_Y, PW, 26, T("\x7f Undo"), can_undo, 32);
  button(PX0 + PW * 0.5f, MENU_Y, PW, 26, T("~ Menu"), true, 32);
  board(&gb, cell, true);
  uint32_t dt = now - t_add;
  if (added && dt < RISE) { /* the points of the last move rise out of the score box */
    float t = (float)dt / RISE, u;
    t *= t; /* slow, then faster, as it fades */
    s[0] = '+';
    num(s + 1, added);
    u = (PW - 2) / tw(s, WT);
    ctext(s, PX0 + PW * 0.5f, SCORE_Y - 7 - 16 * t, u < 0.8f ? u : 0.8f, WT, INK, (int)((1 - t) * 29));
  }
  message();
}

/* the title: size, best, and the game to continue */
static const uint8_t demo[] = {11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1};
static void title_screen(void) {
  int s = sv.size, k = s + 3;
  char str[8] = {(char)('0' + k), '*', (char)('0' + k), 0};
  uint8_t cells[36];
  geo_t g = board_geo(k, 82, 150, 114);
  for (int i = 0; i < k * k; i++) { /* the game to continue, or a snake of tiles */
    int r = i / k, c = r & 1 ? k - 1 - i % k : i % k, j = r * k + c;
    cells[i] = sv.flags[s] & 1 ? sv.cells[cells_at[s] + i] : 0;
    if (!(sv.flags[s] & 1) && j < 11 - (k == 3) * 2) cells[i] = demo[j + (k == 3) * 2];
  }
  text("2048", 20, 14, 3.0f, WT, INK, 32);
  score_box(214, 16, 90, 42, T("BEST"), sv.best[s]);
  const char *join = T("Join the numbers and get to the ");
  text(join, 20, 68, 0.62f, 2.0f, INK, 32);
  /* then the goal, in bold */
  text(k == 3 ? T("512 tile!") : T("2048 tile!"), 20 + (tw_u(join, 0.62f, 2.0f) + 0.4f) * 0.62f, 68, 0.62f, 2.6f, INK, 32);
  board(&g, cells, false);
  text("<", 166, 104, 1.3f, WT, s ? INK : SLOT, 32);
  text(">", 292, 104, 1.3f, WT, s < 3 ? INK : SLOT, 32);
  ctext(str, 236, 114, 1.3f, WT, INK, 32);
  if (sv.flags[s] & 1) button(236, 140, 132, 28, T("Continue"), tsel == 0, 32);
  button(236, sv.flags[s] & 1 ? 176 : 150, 132, 28, T("New game"), tsel == 1 || !(sv.flags[s] & 1), 32);
  ctext(T("Game by Gabriele Cirulli  -  based on Tatone26's version"), 160, 226, 0.5f, 1.9f, BROWN, 32);
}

/* paints a rectangle of the screen, a few rows at a time */
static void paint(int x, int y, int w, int h) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > W) w = W - x;
  if (y + h > H) h = H - y;
  if (w <= 0 || h <= 0) return;
  int rows = BUF / w > GH ? GH : BUF / w;
  for (int t = y; t < y + h; t += rows) {
    bx0 = x, bx1 = x + w, by0 = t, by1 = t + rows < y + h ? t + rows : y + h;
    span(buf, w * (by1 - by0), PAGE);
    if (scene) game_screen();
    else title_screen();
    eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)t, (uint16_t)w, (uint16_t)(by1 - by0)}, buf);
  }
}

/* what to paint this frame */
static int16_t dirty[40][4];
static int nd;
static void mark(float x, float y, float w, float h) {
  int x0 = fl(x), y0 = fl(y), x1 = fl(x + w) + 1, y1 = fl(y + h) + 1;
  for (int i = 0; i < nd; i++) { /* one with any rectangle that costs no more together than apart */
    int16_t *d = dirty[i];
    int u0 = x0 < d[0] ? x0 : d[0], v0 = y0 < d[1] ? y0 : d[1];
    int u1 = x1 > d[0] + d[2] ? x1 : d[0] + d[2], v1 = y1 > d[1] + d[3] ? y1 : d[1] + d[3];
    if ((u1 - u0) * (v1 - v0) <= (x1 - x0) * (y1 - y0) + d[2] * d[3]) {
      x0 = u0, y0 = v0, x1 = u1, y1 = v1, nd--, i = -1; /* and the union may take in more */
      for (int k = 0; k < 4; k++) d[k] = dirty[nd][k];
    }
  }
  if (nd == 40) nd = 0, x0 = y0 = 0, x1 = W, y1 = H;
  int16_t *d = dirty[nd++];
  d[0] = (int16_t)x0, d[1] = (int16_t)y0, d[2] = (int16_t)(x1 - x0), d[3] = (int16_t)(y1 - y0);
}
static void mark_all(void) { nd = 0, mark(0, 0, W, H); }
static void mark_board(void) { mark((float)gb.x, (float)gb.y, (float)gb.size, (float)gb.size); }
static void mark_pops(void) { /* the cells that pop or appear, and room to grow */
  for (int i = 0; i < n * n; i++)
    if (fx[i]) {
      float m = gb.cell * 0.1f + gb.gap * 0.6f + 1;
      mark(cell_x(&gb, i) - m, cell_y(&gb, i) - m, gb.cell + 2 * m, gb.cell + 2 * m);
    }
}
/* in each line that moves, what the moving tiles sweep from slide_t to t1 */
static void mark_slide(float t1) {
  float m = gb.gap * 0.5f + 1, pitch = (float)(gb.cell + gb.gap);
  for (int l = 0; l < n; l++)
    if (lines >> l & 1) {
      float lo = 999, hi = -999;
      for (int j = 0; j < nsl; j++) {
        int f = sl_from[j], d = sl_to[j];
        if (f == d || (dir_rows ? f / n : f % n) != l) continue;
        float a = dir_rows ? cell_x(&gb, f) : cell_y(&gb, f), b = (dir_rows ? cell_x(&gb, d) : cell_y(&gb, d)) - a;
        float p0 = a + b * slide_t, p1 = a + b * t1;
        if (p0 > p1) a = p0, p0 = p1, p1 = a;
        if (p0 < lo) lo = p0;
        if (p1 > hi) hi = p1;
      }
      if (lo > hi) continue;
      float q = (dir_rows ? cell_y(&gb, l * n) : cell_x(&gb, l)) - gb.gap * 0.5f, len = hi - lo + gb.cell + 2 * m;
      if (dir_rows) mark(lo - m, q, len, pitch);
      else mark(q, lo - m, pitch, len);
    }
  slide_t = t1;
}
static void mark_score(void) { mark(PX0, SCORE_Y, PW, 40); }
static void mark_buttons(void) { /* the message's buttons, when the choice changes */
  if (ov == O_PAUSE) mark((float)gb.x, gb.y + gb.size * 0.36f, (float)gb.size, 100);
  else mark((float)gb.x, gap_y(1) - 14, (float)gb.size, 28);
}

static void start(bool fresh) {
  n = sv.size + 3;
  gb = board_geo(n, 201, 120, 226);
  for (int i = 0; i < 32; i++) tc_at[i] = 0; /* the numbers change size */
  tc_used = 0;
  for (int i = 0; i < 36; i++) cell[i] = 0, fx[i] = 0;
  score = moves = 0, won = over = can_undo = false, added = 0, ov = O_NONE, best0 = sv.best[sv.size];
  if (!fresh && sv.flags[sv.size] & 1) {
    for (int i = 0; i < n * n; i++) cell[i] = sv.cells[cells_at[sv.size] + i];
    score = sv.score[sv.size], moves = sv.moves[sv.size], won = sv.flags[sv.size] & 2;
    anim = false;
  } else {
    spawn();
    spawn();
    anim = slid = true, t_move = now - SLIDE, nsl = 0;
  }
  over = !can_move();
  if (over) ov = O_OVER, ov_t = now, sel = 0;
  scene = 1;
  mark_all();
}

/* keeps the game in sv (a finished game is not kept) */
static void store(void) {
  int s = sv.size;
  for (int i = 0; i < n * n; i++) sv.cells[cells_at[s] + i] = over ? 0 : cell[i];
  sv.flags[s] = (uint8_t)((over ? 0 : 1) | (won ? 2 : 0));
  sv.score[s] = score, sv.moves[s] = moves;
  if (sv.best[s] < score) sv.best[s] = score;
}

/* Quit game (and Back on the title): back to NumPlay's games, or the
   calculator's home screen; main keeps the game on the way out */
static bool leave;
static void quit_game(void) { leave = true; }

static void new_game(void) {
  over = true; /* the old one is dropped */
  store();
  save();
  start(true);
}

/* the message o, after delay ms (a negative delay: already faded in) */
static void show(int o, int32_t delay) {
  ov = o, sel = 0, ov_t = now + (uint32_t)delay;
}

static void undo(void) {
  if (anim) mark_pops(); /* a pop reaches past the board's edge */
  for (int i = 0; i < 36; i++) cell[i] = ucell[i], fx[i] = 0;
  score = uscore, moves = umoves, seed = useed, can_undo = over = anim = false, added = 0, ov = O_NONE;
  mark_board();
  mark(PX0, SCORE_Y - 30, PW, 70); /* the score, and the points still rising */
  mark(PX0, UNDO_Y, PW, 26);
}

/* the keys, in the game */
static void game_keys(uint64_t hit) {
  bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
  bool bksp = hit & KEY(eadk_key_backspace);
  int lr = (hit & KEY(eadk_key_right) ? 1 : 0) - (hit & KEY(eadk_key_left) ? 1 : 0);
  int ud = (hit & KEY(eadk_key_down) ? 1 : 0) - (hit & KEY(eadk_key_up) ? 1 : 0);
  if (back) { /* one level back: the pause menu, or out of it (menu to menu without fading) */
    if (ov == O_QUIT) show(O_PAUSE, -999), sel = 2;
    else if (ov == O_PAUSE) show(ov_under, -999);
    else ov_under = ov, show(O_PAUSE, 0);
    mark_board();
    return;
  }
  if (bksp && can_undo && (!ov || ov == O_OVER)) {
    undo();
    return;
  }
  if (ov) {
    int nb = nbuttons(), step = ov == O_PAUSE ? ud : lr + ud;
    if ((int32_t)(now - ov_t) < 0) return; /* not shown yet */
    if (step && nb > 1) sel = (sel + step + nb) % nb, mark_buttons();
    if (!ok) return;
    mark_board();
    if (ov == O_WIN) {
      if (sel) new_game();
      else ov = O_NONE; /* keep going */
    } else if (ov == O_OVER) {
      if (sel) undo();
      else new_game();
    } else if (ov == O_PAUSE) {
      if (sel == 0) show(ov_under, -999);
      else if (sel == 1) new_game();
      else show(O_QUIT, -999);
    } else {
      if (sel) quit_game();
      else show(O_PAUSE, -999), sel = 2;
    }
    return;
  }
  int dir = hit & (KEY(eadk_key_left) | KEY(eadk_key_four))   ? 0
            : hit & (KEY(eadk_key_up) | KEY(eadk_key_eight))  ? 1
            : hit & (KEY(eadk_key_down) | KEY(eadk_key_two))  ? 2
            : hit & (KEY(eadk_key_right) | KEY(eadk_key_six)) ? 3
                                                              : -1;
  if (dir < 0) return;
  if (anim) { /* the last move's tiles may still be sliding or growing */
    if (!slid) mark_slide(1);
    mark_pops();
  }
  bool was = can_undo;
  int top;
  if (!move(dir, &top)) return;
  if (added && now == t_add) mark_score();
  if (sv.best[n - 3] < score) sv.best[n - 3] = score, mark(PX0, BEST_Y, PW, 40);
  if (!was) mark(PX0, UNDO_Y, PW, 26);
  /* the messages wait for the pop to land, then fade in slowly */
  if (!won && top >= (n == 3 ? 9 : 11)) won = true, show(O_WIN, SLIDE + POP + 700);
  if (!can_move()) over = true, show(O_OVER, SLIDE + POP + 700), store(), save();
}

/* the keys, on the title */
static void title_keys(uint64_t hit) {
  int lr = (hit & KEY(eadk_key_right) ? 1 : 0) - (hit & KEY(eadk_key_left) ? 1 : 0);
  int ud = (hit & KEY(eadk_key_down) ? 1 : 0) - (hit & KEY(eadk_key_up) ? 1 : 0);
  if (lr && sv.size + lr >= 0 && sv.size + lr <= 3) {
    sv.size = (uint8_t)(sv.size + lr), tsel = sv.flags[sv.size] & 1 ? 0 : 1;
    mark(214, 16, 90, 42); /* what the size changes: the best, the goal, the board, the choices */
    mark(16, 66, 300, 16);
    mark(24, 92, 290, 118);
  }
  if (ud && sv.flags[sv.size] & 1) tsel = !tsel, mark(160, 130, 160, 80);
  if (hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe))) start(tsel == 1);
  else if (hit & KEY(eadk_key_back)) leave = true;
}

int main(void) {
  np_app_begin();
  font_init();
  load();
  now = (uint32_t)eadk_timing_millis();
  seed ^= eadk_random() ^ now;
  if (!seed) seed = 1;
  tsel = sv.flags[sv.size] & 1 ? 0 : 1;
  mark_all();
  /* keys still down from the launcher do nothing until let go, nor repeat */
  uint64_t held = eadk_keyboard_scan(), stale = held;
  uint32_t repeat_at = 0;
  int ov_f = 0; /* how far the message was faded in when last painted */
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint64_t k = eadk_keyboard_scan(), hit = k & ~held;
    const uint64_t rep = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down);
    stale &= k;
    if (hit & rep) repeat_at = now + 400;
    else if ((k & rep & ~stale) && (int32_t)(now - repeat_at) >= 0 && (!scene || ov))
      hit |= k & rep & ~stale, repeat_at = now + 110;
    held = k;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    if (scene) game_keys(hit);
    else title_keys(hit);
    if (leave) break;
    if (scene && anim) {
      uint32_t dt = now - t_move;
      if (!slid) mark_slide(ease((float)dt / SLIDE)), slid = dt >= SLIDE;
      if (dt >= SLIDE) mark_pops();
      if (dt >= SLIDE + POP) anim = false;
    }
    if (scene && added && now - t_add < RISE + 40) mark(PX0, SCORE_Y - 30, PW, 30); /* above the score box */
    bool nb = scene && best0 && score > best0;
    if (nb != nbest) nbest = nb, mark(PX0, BEST_Y + 44, PW, 16);
    if (scene && ov && fade() != ov_f) ov_f = fade(), mark_board(); /* only while it fades */
    /* many rectangles over each other (keys pressed fast): one around them
       all may be less to paint */
    int area = 0, x0 = W, y0 = H, x1 = 0, y1 = 0;
    for (int i = 0; i < nd; i++) {
      int16_t *d = dirty[i];
      area += d[2] * d[3];
      if (d[0] < x0) x0 = d[0];
      if (d[1] < y0) y0 = d[1];
      if (d[0] + d[2] > x1) x1 = d[0] + d[2];
      if (d[1] + d[3] > y1) y1 = d[1] + d[3];
    }
    if (nd > 1 && (x1 - x0) * (y1 - y0) <= area) nd = 0, mark(x0, y0, x1 - x0 - 1, y1 - y0 - 1);
    eadk_display_wait_for_vblank();
    for (int i = 0; i < nd; i++) paint(dirty[i][0], dirty[i][1], dirty[i][2], dirty[i][3]);
    nd = 0;
    uint32_t spent = (uint32_t)eadk_timing_millis() - now; /* about 60 frames a second */
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
  if (scene) store();
  save();
  return np_app_end();
}
