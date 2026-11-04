/* Solitaire: Klondike the way Google Solitaire (the game in Search) plays
 * and looks, for the NumWorks calculator: the deck and the waste on the
 * left, the foundations on the right, the dark bar with the time, score and
 * moves, flat cards with two-tone suits and blue sunburst backs, and "Choose
 * your difficulty" to start. Rewritten in C for NumPlay from Tatone26's
 * Solitaire in All the Apps, which is in the public domain.
 *
 * The table is drawn only where something changed: each changed rectangle is
 * composed in a small buffer, strip by strip, and pushed once, so nothing
 * flickers. Suits are tiny bitmaps; the court figures are drawn from shapes. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/jump.h"
#include "../../common/np_text.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Solitaire";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
#define BLACK 0
/* Google Solitaire's colors */
#define FELT RGB(0x34A249)
#define SIDE RGB(0x2B7B3B)  /* the columns of the deck and the foundations */
#define BAR RGB(0x313131)   /* the bar on top */
#define SLOT RGB(0x3C8D4B)  /* an empty place in a side column */
#define SLOT2 RGB(0x57A566) /* the suit on it */
#define EDGE RGB(0xC9CDD1)  /* a card's outline */
#define INK RGB(0x202124)   /* text on white */
#define GREY RGB(0x5F6368)
#define PALE RGB(0xE8EAED)
#define BLUE RGB(0x1A73E8)
#define GOLD RGB(0xFDD835) /* the cursor */
#define KEY(k) (1ull << (k))

/* suits: spades, hearts, clubs, diamonds; each a light and a dark shade */
static const color SUIT_C[2][2] = {{RGB(0x2F3A58), RGB(0x141624)}, {RGB(0xEF5050), RGB(0xBB2026)}};

/* ------------------------------------------------------------------ art */
/* suits: spades, hearts, clubs, diamonds (left half and middle column of each row) */
static const uint8_t pip7[4][7] = {{0x01,0x03,0x07,0x0F,0x06,0x01,0x03}, {0x06,0x0F,0x0F,0x0F,0x07,0x03,0x01}, {0x03,0x03,0x06,0x0F,0x06,0x01,0x03}, {0x01,0x03,0x07,0x0F,0x07,0x03,0x01}};
static const uint8_t pip13[4][15] = {
  {0x01,0x03,0x07,0x0F,0x1F,0x3F,0x7F,0x7F,0x7F,0x3D,0x19,0x01,0x03,0x0F,0x00},
  {0x00,0x38,0x7C,0x7E,0x7F,0x7F,0x7F,0x7F,0x3F,0x1F,0x0F,0x07,0x03,0x01,0x00},
  {0x00,0x03,0x07,0x07,0x07,0x33,0x79,0x7F,0x7F,0x79,0x31,0x01,0x03,0x0F,0x00},
  {0x01,0x03,0x07,0x07,0x0F,0x1F,0x3F,0x7F,0x3F,0x1F,0x0F,0x07,0x07,0x03,0x01}};

/* ASCII 32..122 (space to z), columns of 7 bits, top bit first */
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
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
  {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
  {0x00,0x01,0x02,0x04,0x00},{0x20,0x54,0x54,0x54,0x78},{0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x44,0x20},
  {0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x08,0x7E,0x09,0x01,0x02},{0x0C,0x52,0x52,0x52,0x3E},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x20,0x40,0x44,0x3D,0x00},{0x7F,0x10,0x28,0x44,0x00},
  {0x00,0x41,0x7F,0x40,0x00},{0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},
  {0x7C,0x14,0x14,0x14,0x08},{0x08,0x14,0x14,0x18,0x7C},{0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},
  {0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},{0x1C,0x20,0x40,0x20,0x1C},{0x3C,0x40,0x30,0x40,0x3C},
  {0x44,0x28,0x10,0x28,0x44},{0x0C,0x50,0x50,0x50,0x3C},{0x44,0x64,0x54,0x4C,0x44},
};

/* ------------------------------------------------------------------ drawing */
/* Everything is composed in buf, one region of the screen at a time: the
   region's pixels, row after row (rw wide). Drawing outside it is clipped. */
#define BUFPX (320 * 24)
static color buf[BUFPX];
static int rx, ry, rw, rh;

static int min(int a, int b) { return a < b ? a : b; }
static int max(int a, int b) { return a > b ? a : b; }

/* f over b; a from 0 (all b) to 32 (all f) */
#ifdef __TICE__ /* the TI-84 Plus CE: its own mixing (the same colours) */
#include "../../../platform/ti84ce/ce_draw.h"
#include <wchar.h>
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
static void fill(int x, int y, int w, int h, color c) {
  int x0 = max(x, rx), x1 = min(x + w, rx + rw), y0 = max(y, ry), y1 = min(y + h, ry + rh);
  if (x0 >= x1) return;
#ifdef __TICE__ /* (the TI-84 Plus CE: the C library's fill) */
  for (int j = y0; j < y1; j++) wmemset((wchar_t *)buf + (j - ry) * rw + (x0 - rx), (wchar_t)c, (size_t)(x1 - x0));
  return;
#endif
  for (int j = y0; j < y1; j++)
    for (color *p = buf + (j - ry) * rw + (x0 - rx), *e = p + (x1 - x0); p < e; p++) *p = c;
}
/* darkens a rectangle, a from 0 (not at all) to 32 (black) */
static void shadow(int x, int y, int w, int h, int a) {
  int x0 = max(x, rx), x1 = min(x + w, rx + rw), y0 = max(y, ry), y1 = min(y + h, ry + rh);
  for (int j = y0; j < y1; j++)
    for (color *p = buf + (j - ry) * rw + (x0 - rx), *e = p + max(x1 - x0, 0); p < e; p++) *p = mix(0, *p, a);
}
static void px(int x, int y, color c) {
  if ((unsigned)(x - rx) < (unsigned)rw && (unsigned)(y - ry) < (unsigned)rh) buf[(y - ry) * rw + x - rx] = c;
}
/* a rounded rectangle, corners of radius r (up to 8), filled with c */
static void rbox(int x, int y, int w, int h, int r, color c) {
  static const uint8_t cut[8][8] = {{0}, {1}, {1, 0}, {2, 1, 0}, {2, 1, 0, 0}, {3, 2, 1, 1, 0}, {3, 2, 1, 1, 0, 0}, {4, 3, 2, 1, 1, 0, 0}};
  r = min(r, 7);
  for (int j = 0; j < h; j++) {
    int e = j < r ? cut[r][j] : j >= h - r ? cut[r][h - 1 - j] : 0;
    fill(x + e, y + j, w - 2 * e, 1, c);
  }
}
/* a disc of radius r (a ring from r0) */
static void ring(int cx, int cy, int r0, int r1, color c) {
  for (int dy = -r1; dy <= r1; dy++)
    for (int dx = -r1; dx <= r1; dx++) {
      int d = dx * dx + dy * dy;
      if (d >= r0 * r0 && d <= r1 * r1 + r1) px(cx + dx, cy + dy, c);
    }
}
/* A left-right symmetric bitmap: each row holds the left half and the middle
   column, first column in the top bit. Two-tone like Google's suits: the left
   half in a, the rest in b. */
static void sym(const uint8_t *rows, int n, int half, int x, int y, color a, color b) {
  for (int j = 0; j < n; j++)
    for (int i = 0; i < half; i++)
      if (rows[j] >> (half - 1 - i) & 1) px(x + i, y + j, i < half - 1 ? a : b), px(x + 2 * half - 2 - i, y + j, b);
}
/* a character of the 5x7 font, k times bigger, bold (one more column) when b */
static void glyph(int ch, int x, int y, int k, color c, int b) {
  const uint8_t *g = font[(unsigned)(ch - 32) < 91 ? ch - 32 : '?' - 32];
  for (int i = 0; i < 5; i++)
    for (int j = 0; j < 7; j++)
      if (g[i] >> j & 1) fill(x + i * k, y + j * k, k + b, k, c);
}
static int slen(const char *s) { return np_text_cells(s); } /* (Chinese letters take two cells) */
/* the firmware's fonts, straight to the screen: 7x14 or 10x18 cells */
static void str(const char *s, int x, int y, int large, color fg, color bg) {
  eadk_display_draw_string(s, (eadk_point_t){(uint16_t)x, (uint16_t)y}, large, fg, bg);
}

static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}
static char *num(char *o, int32_t v) {
  char t[11];
  int n = 0;
  uint32_t u = v < 0 ? 0u - (uint32_t)v : (uint32_t)v;
  if (v < 0) *o++ = '-';
  do t[n++] = (char)('0' + u % 10); while (u /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *two(char *o, int v) { /* 00 to 59 */
  *o++ = (char)('0' + v / 10), *o++ = (char)('0' + v % 10), *o = 0;
  return o;
}
/* Vegas dollars: -$52 */
static char *money(char *o, int32_t v) {
  char *s = o;
  o = num(cat(o, "$"), v);
  if (v < 0) s[0] = '-', s[1] = '$';
  return o;
}

/* ------------------------------------------------------------------ cards */
enum { STOCK, WASTE, F0, T0 = 6, NP = 13 }; /* the piles */
enum { B_UNDO = -2, B_NEW = -3 };           /* the buttons under the deck, for the cursor */

/* everything kept between visits (solitaire.sav): options, statistics and
   the game in progress */
static struct {
  uint8_t magic, version, draw, scoring; /* draw: 1 Easy, 3 Hard; scoring: 0 Standard, 1 Vegas, 2 none */
  uint8_t timed, keep, live, started;    /* live: a game to continue */
  uint8_t gdraw, gscoring, gtimed, passes; /* the game's own options; recycled decks */
  uint8_t wfan, back, pad[2];            /* cards spread on the waste; the card back */
  uint16_t played, won, streak, best_streak, best_time, moves;
  int32_t best_score, bank, score; /* bank: all Vegas games added up */
  uint32_t seed, ms;
  uint8_t n[NP], cards[52], pad2[3];
} V;
#define SAVE_NAME "solitaire.sav"

/* A card is rank * 4 + suit (suits: spades, hearts, clubs, diamonds, so bit 0
   is red), plus UP when it lies face up. */
#define UP 0x80
#define RANK(c) (((c) & 63) >> 2)
#define SUIT(c) ((c) & 3)
#define CW 31
#define CH 43
#define BARH 18                 /* the bar on top */
#define SIDEW 38                /* the side columns */
#define COLX(i) (42 + (i) * 34) /* the columns of the table */
#define TABY 24
#define LX 4      /* the deck, the waste and the buttons */
#define RX 285    /* the foundations */
#define WASTEY 72
#define FAN 12    /* the waste's spread, drawing 2 or 3, downwards */
#define FY(i) (24 + (i) * 48)
#define UNDOY 172 /* the buttons */
#define NEWY 204

/* the backs: Google's blue sunburst, and red, green and purple ones */
static const uint32_t BACKS[4][2] = {{0x3F9CFB, 0x40B7FE}, {0xE0463C, 0xF77B6E}, {0x2E9D57, 0x55C27D}, {0x7D52D9, 0xA07CF0}};

/* a card's shape: round corners, an outline o, filled with f */
static void cardshape(int x, int y, color o, color f) {
  fill(x + 3, y, CW - 6, CH, o), fill(x + 1, y + 1, CW - 2, CH - 2, o), fill(x, y + 3, CW, CH - 6, o);
  fill(x + 3, y + 1, CW - 6, CH - 2, f), fill(x + 2, y + 2, CW - 4, CH - 4, f), fill(x + 1, y + 3, CW - 2, CH - 6, f);
}


/* the back: a white border, rays of two blues from the middle, a white disc
   with a four-color ring (shows only the rows in view: covered, a strip) */
static void back(int x, int y, int style) {
  cardshape(x, y, EDGE, WHITE);
  color c0 = RGB(BACKS[style][0]), c1 = RGB(BACKS[style][1]);
  int cx = x + CW / 2, cy = y + CH / 2, x0 = max(x + 3, rx), x1 = min(x + CW - 3, rx + rw), y0 = max(y + 3, ry), y1 = min(y + CH - 3, ry + rh);
#ifdef __TICE__ /* (the TI-84 Plus CE: the pattern worked out once for the style, then copied) */
  static color pat[(CH - 6) * (CW - 6)];
  static int pat_style = -1;
  if (pat_style != style) {
    pat_style = style;
    for (int j = y + 3, n = 0; j < y + CH - 3; j++)
      for (int i = x + 3; i < x + CW - 3; i++, n++) {
        int dx = i - cx, dy = j - cy, a = dx < 0 ? -dx : dx, b = dy < 0 ? -dy : dy, m = min(a, b), M = max(a, b), d = dx * dx + dy * dy;
        int w = a >= b ? m * 1000 >= M * 414 : 2 + (m * 1000 < M * 414);
        color c = w & 1 ? c0 : c1;
        if (d <= 30) {
          static const uint32_t Q[4] = {0xEA4335, 0x4285F4, 0xFBBC05, 0x34A853};
          c = d >= 12 && d <= 20 ? RGB(Q[(dx < 0) + 2 * (dy >= 0)]) : WHITE;
        }
        pat[n] = c;
      }
  }
  if (x0 < x1)
    for (int j = y0; j < y1; j++) memcpy(buf + (j - ry) * rw + x0 - rx, pat + (j - y - 3) * (CW - 6) + x0 - x - 3, (size_t)(x1 - x0) * 2);
  return;
#endif
  for (int j = y0; j < y1; j++)
    for (int i = x0; i < x1; i++) {
      int dx = i - cx, dy = j - cy, a = dx < 0 ? -dx : dx, b = dy < 0 ? -dy : dy, m = min(a, b), M = max(a, b), d = dx * dx + dy * dy;
      int w = a >= b ? m * 1000 >= M * 414 : 2 + (m * 1000 < M * 414); /* which of 4 rays in a quarter */
      color c = w & 1 ? c0 : c1;
      if (d <= 30) {
        static const uint32_t Q[4] = {0xEA4335, 0x4285F4, 0xFBBC05, 0x34A853};
        c = d >= 12 && d <= 20 ? RGB(Q[(dx < 0) + 2 * (dy >= 0)]) : WHITE;
      }
      buf[(j - ry) * rw + i - rx] = c;
    }
}

/* A court figure, flat and two-tone like Google's: a bust in a robe of the
   suit's colors (a, b), 24 x 28 units of s pixels. r: 10 jack, 11 queen, 12 king. */
#define SKIN RGB(0xF6C4A0)
#define SKIN2 RGB(0xE0A47E)
#define CROWN RGB(0xF9C22E)
static void portrait(int r, int x, int y, int s, color a, color b) {
#define P(u, v, w, h, c) fill(x + (u) * s, y + (v) * s, (w) * s, (h) * s, c)
  color hair = r == 11 ? RGB(0x2B2B3A) : r == 10 ? RGB(0x8A5230) : RGB(0xB0B0B0);
  if (r == 11) P(6, 7, 12, 15, hair), P(7, 5, 10, 2, hair); /* the queen's long hair */
  for (int t = 0; t < 9; t++) { /* the robe, wider at the bottom */
    int hw = 6 + t / 2;
    P(12 - hw, 19 + t, hw, 1, a), P(12, 19 + t, hw, 1, b);
  }
  P(10, 15, 4, 4, SKIN2); /* the neck */
  P(9, 19, 6, 2, r == 10 ? WHITE : CROWN), P(11, 21, 2, 3, r == 10 ? WHITE : CROWN); /* the collar */
  ring(x + 12 * s, y + 11 * s, 0, 5 * s, SKIN);
  P(12, 7, 5, 9, SKIN), P(7, 7, 5, 9, SKIN), fill(x + 17 * s - 1, y + 9 * s, 1, 4 * s, SKIN2);
  P(9, 11, 1, 1, INK), P(14, 11, 1, 1, INK); /* the eyes */
  P(11, 14, 2, 1, RGB(0xC0504D));            /* the mouth */
  if (r == 12) { /* the king's beard and moustache */
    P(8, 14, 8, 2, RGB(0xE9E9E9)), P(9, 16, 6, 1, RGB(0xE9E9E9)), P(10, 17, 4, 1, RGB(0xD5D5D5));
    P(9, 13, 6, 1, RGB(0xBDBDBD));
  }
  if (r != 11) P(7, 6, 10, 2, hair), P(7, 8, 1, 3, hair), P(16, 8, 1, 3, hair);
  if (r >= 11) { /* a crown with three points */
    int w = r == 12 ? 10 : 8, u = 12 - w / 2;
    P(u, 3, w, 3, CROWN), P(u, 1, 2, 2, CROWN), P(11, 0, 2, 3, CROWN), P(u + w - 2, 1, 2, 2, CROWN);
    P(11, 4, 2, 1, b);
  } else { /* the jack's cap, and a feather */
    P(6, 4, 12, 3, a), P(12, 4, 6, 3, b), P(8, 2, 8, 2, a), P(16, 0, 2, 4, CROWN);
  }
#undef P
}

/* a card at (x, y): the rank in bold and a small suit at the top, a big suit
   (or a figure) below */
static void card(int c, int x, int y) {
  if (x >= rx + rw || x + CW <= rx || y >= ry + rh || y + CH <= ry) return;
  if (!(c & UP)) {
    back(x, y, V.back & 3);
    return;
  }
  int s = SUIT(c), r = RANK(c), red = s & 1;
  color a = SUIT_C[red][0], b = SUIT_C[red][1], ink = red ? b : a;
  cardshape(x, y, EDGE, WHITE);
  if (r == 9) glyph('1', x + 1, y + 3, 1, ink, 1), glyph('0', x + 7, y + 3, 1, ink, 1);
  else glyph("A23456789?JQK"[r], x + 3, y + 3, 1, ink, 1);
  sym(pip7[s], 7, 4, x + CW - 11, y + 3, a, b);
  if (r < 10) sym(pip13[s], 15, 7, x + 9, y + 19, a, b);
  else portrait(r, x + 4, y + 13, 1, a, b);
}

/* the suit each foundation takes, top to bottom, as Google marks them */
static const uint8_t FSUIT[4] = {1, 3, 2, 0};
/* an empty place: in a side column, a darker card with a suit (-1: none);
   on the felt, an outline */
static void slot(int x, int y, int s) {
  if (x < SIDEW || x > 320 - SIDEW) {
    rbox(x, y, CW, CH, 3, SLOT);
    if (s >= 0) sym(pip13[s], 15, 7, x + 9, y + 14, SLOT2, SLOT2);
  } else {
    rbox(x, y, CW, CH, 3, RGB(0x4DB361));
    rbox(x + 1, y + 1, CW - 2, CH - 2, 3, FELT);
  }
}
/* a 2-pixel frame with round corners */
static void rframe(int x, int y, int w, int h, color c) {
  fill(x + 2, y, w - 4, 2, c), fill(x + 2, y + h - 2, w - 4, 2, c), fill(x, y + 2, 2, h - 4, c), fill(x + w - 2, y + 2, 2, h - 4, c);
  px(x + 1, y + 1, c), px(x + w - 2, y + 1, c), px(x + 1, y + h - 2, c), px(x + w - 2, y + h - 2, c);
}
static void text(const char *s, int x, int y, color c) {
  for (; *s; s++, x += 6) glyph(*s, x, y, 1, c, 0);
}
/* Language builds: a text in the 5x7 font with letters beyond ASCII. An accented letter is the
   plain one with its accent (np_text.h) over it (in the rows over a small letter, just over a
   capital, under it for a cedilla); others (Chinese) come from the 12-pixel font. Returns the
   width (draw false: only measures). */
static color xcol;
static void xplot(int x, int y, void *ctx) { px(x, y, xcol); }
static int xtext(const char *s, int x, int y, color c, int draw) {
  int x0 = x;
  xcol = c;
  while (*s) {
    uint32_t cp = np_utf8(&s);
    char b = (char)cp, b2 = 0;
    int acc = cp >= 0x80 ? np_latin(cp, &b, &b2) : 0;
    if (cp >= 0x80 && !b) {
      x += draw ? np_xdraw(cp, x, y - 3, 1, xplot, 0) : np_xadvance(cp, 1);
      continue;
    }
    for (; b; b = b2, b2 = 0, acc = 0, x += 6)
      if (draw) {
        glyph(b, x, y, 1, c, 0);
        np_accent(acc, x, acc == NP_ACC_CEDIL ? y + 7 : b >= 'a' ? y : y - 3, 1, xplot, 0);
      }
  }
  return x - x0 - 1;
}

/* ------------------------------------------------------------------ state */
static uint8_t pl[NP][52], pn[NP]; /* the piles, bottom card first */
static uint8_t fd[7], od[7], ou[7]; /* per column: face-down cards, their spread, the face-up spread */

static uint32_t now, seed = 0x9E3779B9;
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

/* the waste shows the last cards drawn, spread (none left in the spread: the
   top card alone) */
static int fan(void) { return min(min(max(V.wfan, 1), V.gdraw), pn[WASTE]); }

/* each column's spread, squeezed until it fits the screen */
static void relayout(void) {
  for (int i = 0; i < 7; i++) {
    int n = pn[T0 + i], d = 0, a = 5, b = 13;
    while (d < n && !(pl[T0 + i][d] & UP)) d++;
    /* face up to 11 px, face down to 2, face up to 10 (the rank still shows
       whole), then whatever it takes */
    while (d * a + max(n - d - 1, 0) * b > 240 - 2 - TABY - CH) {
      if (b > 11 || (a < 3 && b > 10)) b--;
      else if (a > 2) a--;
      else if (b > 1) b--;
      else break;
    }
    fd[i] = (uint8_t)d, od[i] = (uint8_t)a, ou[i] = (uint8_t)b;
  }
}

/* where card k of pile p lies (k may be the free place above the top) */
static void pos(int p, int k, int *x, int *y) {
  if (p == STOCK) {
    *x = LX, *y = TABY;
  } else if (p == WASTE) {
    *x = LX, *y = WASTEY + max(k - (pn[WASTE] - fan()), 0) * FAN;
  } else if (p < T0) {
    *x = RX, *y = FY(p - F0);
  } else {
    int i = p - T0, d = fd[i];
    *x = COLX(i);
    *y = TABY + (k < d ? k * od[i] : d * od[i] + (k - d) * ou[i]);
  }
}

/* how many cards from the top can be picked up together */
static int run(int p) {
  if (p < 0) return 0;
  int n = pn[p], k = 1;
  if (!n || !(pl[p][n - 1] & UP)) return 0;
  if (p < T0) return 1;
  for (; k < n; k++) {
    int a = pl[p][n - k - 1], b = pl[p][n - k];
    if (!(a & UP) || RANK(a) != RANK(b) + 1 || !((a ^ b) & 1)) break;
  }
  return k;
}

/* can the top n cards of f go on t? */
static int legal(int f, int n, int t) {
  if (f == t || t < F0 || (f >= F0 && f < T0 && t < T0) || n < 1 || n > run(f)) return 0;
  int c = pl[f][pn[f] - n], top = pn[t] ? pl[t][pn[t] - 1] : -1;
  if (t < T0) return n == 1 && SUIT(c) == FSUIT[t - F0] && (top < 0 ? RANK(c) == 0 : RANK(top) + 1 == RANK(c));
  return top < 0 ? RANK(c) == 12 : RANK(top) == RANK(c) + 1 && ((top ^ c) & 1);
}

/* ------------------------------------------------------------------ screen */
static np_jump_t leave; /* Home and On/Off leave from anywhere through it */
static uint64_t held, pend, rep; /* keys down, presses not handled yet, arrows that repeat */
static uint32_t repeat_at, last_frame;
static int layer;   /* what the screen shows: 0 the table, 1 the table under the start, 2 one widget */
static int ticking; /* the game's clock runs (not in dialogs) */
static const char *msg;
static void scene(void);
static void over_text(int x, int y, int w, int h);

/* draws the scene over a rectangle of the screen and pushes it */
static void paint(int x, int y, int w, int h) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  w = min(w, 320 - x), h = min(h, 240 - y);
  if (w <= 0 || h <= 0) return;
  for (int y0 = y, sh = BUFPX / w; y0 < y + h; y0 += sh) {
    rx = x, ry = y0, rw = w, rh = min(sh, y + h - y0);
    scene();
    eadk_display_push_rect((eadk_rect_t){(uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh}, buf);
  }
  if (!layer) over_text(x, y, w, h);
}

/* rectangles to repaint at the next frame, merged when they overlap */
static int16_t dr[12][4];
static int ndr;
static void dirty(int x, int y, int w, int h) {
  int x1 = x + w, y1 = y + h;
  for (int i = 0; i < ndr; i++) {
    int16_t *d = dr[i];
    if (x < d[2] && d[0] < x1 && y < d[3] && d[1] < y1) {
      d[0] = (int16_t)min(d[0], x), d[1] = (int16_t)min(d[1], y), d[2] = (int16_t)max(d[2], x1), d[3] = (int16_t)max(d[3], y1);
      return;
    }
  }
  if (ndr == 12) {
    int16_t *d = dr[11];
    d[0] = (int16_t)min(d[0], x), d[1] = (int16_t)min(d[1], y), d[2] = (int16_t)max(d[2], x1), d[3] = (int16_t)max(d[3], y1);
    return;
  }
  dr[ndr][0] = (int16_t)x, dr[ndr][1] = (int16_t)y, dr[ndr][2] = (int16_t)x1, dr[ndr][3] = (int16_t)y1;
  ndr++;
}
static void dirty_pile(int p) {
  if (p == STOCK) dirty(LX - 2, TABY - 2, CW + 6, CH + 6);
  else if (p == WASTE) dirty(LX - 2, WASTEY - 2, CW + 4, CH + 4 + 2 * FAN);
  else if (p < T0) dirty(RX - 2, FY(p - F0) - 2, CW + 4, CH + 4);
  else dirty(COLX(p - T0) - 2, TABY - 2, CW + 4, 240 - TABY + 2);
}
static void dirty_bar(void) { dirty(0, 0, 320, BARH); }
#define TOAST_Y 216 /* a message at the bottom of the table */
static void dirty_toast(void) { dirty(SIDEW, TOAST_Y, 320 - 2 * SIDEW, 20); }

/* keys: presses since the last look, arrows repeating while held */
static void poll(void) {
  uint64_t k = eadk_keyboard_scan(), e = k & ~held;
  const uint64_t arrows = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down);
  if (e & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) np_jump(leave);
  rep &= k; /* only arrows pressed here repeat, not one still held from the launcher */
  if (e & arrows) rep |= e & arrows, repeat_at = now + 320;
  else if (rep && (int32_t)(now - repeat_at) >= 0) e |= rep, repeat_at = now + 80;
  held = k;
  pend |= e;
}

static void tick(uint32_t dt);
/* one frame: repaint what changed, at most 60 times a second. Timed from the
   refresh, so a long repaint does not make the next frame miss one. */
static void next_frame(void) {
  uint32_t t = (uint32_t)eadk_timing_millis();
  if (t - last_frame < 14) eadk_timing_msleep(14 - (t - last_frame));
  eadk_display_wait_for_vblank();
  last_frame = (uint32_t)eadk_timing_millis();
  for (int i = 0; i < ndr; i++) paint(dr[i][0], dr[i][1], dr[i][2] - dr[i][0], dr[i][3] - dr[i][1]);
  ndr = 0;
  t = (uint32_t)eadk_timing_millis();
  if (ticking) tick((uint32_t)min((int)(t - now), 100)); /* animations count too */
  now = t;
  poll();
}
static uint64_t take(void) {
  uint64_t e = pend;
  pend = 0;
  return e;
}
#define OK_KEYS (KEY(eadk_key_ok) | KEY(eadk_key_exe))

/* ------------------------------------------------------------------ widgets */
/* Google's dialogs: white cards with round corners, pill buttons (blue when
   in focus), round radio buttons and check boxes. Widgets are painted one at
   a time (layer 2); their text is the firmware's font, drawn over them
   afterwards. */
enum { W_WIN, W_BTN, W_RADIO, W_CHECK, W_GROUP, W_BACK };
#define F_ON 1 /* a radio button or a box that is on */
#define F_FOCUS 2
#define F_OFF 4 /* greyed out */
static int wt, wx, wy, ww, wh, wf;
static const char *wl;

static void widget_draw(void) {
  int x = wx, y = wy, f = wf & F_FOCUS;
  if (wt == W_WIN) {
    rbox(x, y, ww, wh, 7, WHITE);
    if (wf) { /* the question mark */
      ring(x + 30, y + 50, 0, 14, BLUE);
      glyph('?', x + 25, y + 43, 2, WHITE, 0);
    }
    return;
  }
  fill(x, y, ww, wh, WHITE);
  if (wt == W_BTN) {
    rbox(x, y, ww, wh, 7, f ? BLUE : PALE);
  } else if (wt == W_BACK) { /* a card back to pick, style in the high bits */
    if (f) rbox(x, y, ww, wh, 5, PALE);
    if (wf & F_ON) rframe(x + 1, y + 1, ww - 2, wh - 2, BLUE);
    back(x + 4, y + 4, wf >> 4);
  } else if (wt != W_GROUP) {
    if (f) rbox(x + 14, y - 1, slen(wl) * 7 + 10, 18, 5, PALE);
    color c = wf & F_OFF ? PALE : wf & F_ON ? BLUE : GREY;
    if (wt == W_RADIO) {
      ring(x + 6, y + 8, 0, 6, c);
      ring(x + 6, y + 8, 0, 4, WHITE);
      if (wf & F_ON) ring(x + 6, y + 8, 0, 2, c);
    } else {
      rbox(x, y + 2, 13, 13, 2, c);
      if (wf & F_ON)
        for (int i = 0; i < 7; i++) fill(x + 3 + i, y + 6 + (i <= 2 ? i + 2 : 6 - i), 1, 2, WHITE);
      else rbox(x + 2, y + 4, 9, 9, 1, WHITE);
    }
  }
}
/* paints a widget, then its label */
static void widget(int t, int x, int y, int w, int h, const char *label, int f) {
  int l = layer;
  wt = t, wx = x, wy = y, ww = w, wh = h, wl = label, wf = f, layer = 2;
  paint(x, y, w, h);
  layer = l;
  if (!label) return;
  color bg = t == W_BTN ? (f & F_FOCUS ? BLUE : PALE) : f & F_FOCUS ? PALE : WHITE;
  color fg = t == W_BTN && (f & F_FOCUS) ? WHITE : t == W_GROUP ? BLUE : f & F_OFF ? GREY : INK;
  if (t == W_BTN) x += (w - slen(label) * 7) / 2 - 18, y += (h - 14) / 2 - 1;
  else if (t == W_GROUP) x -= 18, bg = WHITE;
  str(label, x + 18, y + 1, 0, fg, bg);
}
static void win(int x, int y, int w, int h, const char *title, int icon) {
  widget(W_WIN, x, y, w, h, 0, icon);
  str(title, x + 16, y + 9, 1, INK, WHITE);
}

/* the cells of the first n bytes of s (language builds: Chinese letters take two) */
static int cells(const char *s, int n) {
  char t[120];
  int k = 0;
  for (; k < n && k < (int)sizeof t - 1; k++) t[k] = s[k];
  t[k] = 0;
  return slen(t);
}

/* A dialog box: text lines, then buttons (in a column when vert). Returns the
   button chosen, or -1 for Back. box keeps where it was. */
static int16_t box[4];
static int dialog(const char *title, const char *body, const char *const *btn, int nb, int vert, int icon) {
  int lines = 0, tw = 0, bw = 76, ix = icon ? 48 : 0;
  for (const char *s = body; s && *s; lines++) {
    int n = 0;
    while (s[n] && s[n] != '\n') n++;
    tw = max(tw, (NP_TEXT_EXTRA ? cells(s, n) : n) * 7);
    s += n + (s[n] == '\n');
  }
  for (int i = 0; i < nb; i++) bw = max(bw, slen(btn[i]) * 7 + 28);
  int bb = vert ? bw : nb * bw + (nb - 1) * 10, w = max(max(tw + ix, bb) + 36, slen(title) * 10 + 36);
  int th = max(lines * 16, icon ? 36 : 0), h = 38 + th + (th ? 12 : 0) + (vert ? nb * 30 - 6 : 24) + 14;
  int x = (320 - w) / 2, y = (240 - h) / 2, by = y + 38 + th + (th ? 12 : 0), f = 0;
  box[0] = (int16_t)x, box[1] = (int16_t)y, box[2] = (int16_t)w, box[3] = (int16_t)h;
  ndr = 0, ticking = 0; /* nothing of the table shows through, and the clock stops */
  win(x, y, w, h, title, icon);
  char ln[NP_TEXT_EXTRA ? 120 : 48]; /* (a Chinese letter takes three bytes) */
  for (int i = 0, n; body && *body; i++, body += n + (body[n] == '\n')) {
    for (n = 0; body[n] && body[n] != '\n' && n < (int)sizeof ln - 1; n++) ln[n] = body[n];
    ln[n] = 0;
    str(ln, x + 18 + ix, y + 38 + (th - lines * 16) / 2 + i * 16, 0, GREY, WHITE);
  }
  for (uint64_t e = 0;; e = take()) {
    int d = (e & KEY(vert ? eadk_key_down : eadk_key_right) ? 1 : 0) - (e & KEY(vert ? eadk_key_up : eadk_key_left) ? 1 : 0);
    for (int i = 0; i < nb; i++)
      if (!e || (d && (i == f || i == (f + d + nb) % nb)))
        widget(W_BTN, vert ? x + (w - bw) / 2 : x + (w - bb) / 2 + i * (bw + 10), vert ? by + i * 30 : by, bw, 24, btn[i],
               (i == (f + d + nb) % nb) * F_FOCUS);
    f = (f + d + nb) % nb;
    if (e & OK_KEYS) return f;
    if (e & KEY(eadk_key_back)) return -1;
    next_frame();
  }
}

/* ------------------------------------------------------------------ the table */
static int cp = T0 + 3, cd = 1;  /* the cursor: pile (or button), and cards picked with Up in a column */
static int lastl = WASTE, lastr = F0; /* where the cursor was in the side columns */
static int sp = -1, sn;          /* the cards picked up: pile and count */
static int hint_p = -1;          /* the pile a hint points to */
static int nocur;                /* hides the cursor: dealing, winning */
static uint32_t hint_until, msg_until;

/* cards in flight: they are hidden in their pile until they land */
typedef struct {
  uint8_t c;
  int8_t t; /* frames flown; waits while negative */
  int16_t x0, y0, x1, y1;
} fly_t;
#define NFLY 28
static fly_t fl[NFLY];
static int nfl, dur = 9;
static uint64_t hid;

static void fly_pos(const fly_t *f, int *x, int *y) {
  int t = f->t * (2 * dur - f->t), d = dur * dur; /* easing out */
  *x = f->x0 + (f->x1 - f->x0) * t / d, *y = f->y0 + (f->y1 - f->y0) * t / d;
}
static int shown(int c) { return !(hid >> (c & 63) & 1); }

/* the cursor's rectangle: the cards (or the button) it points at */
static void cur_rect(int *x, int *y, int *h) {
  if (cp < 0) {
    *x = LX, *y = cp == B_UNDO ? UNDOY - 3 : NEWY - 3, *h = 30;
    return;
  }
  int n = pn[cp], y1;
  pos(cp, max(n - (cp >= T0 ? cd : 1), 0), x, y);
  pos(cp, max(n - 1, 0), x, &y1);
  *h = y1 - *y + CH;
}
static void dirty_cursor(void) {
  int x, y, h;
  cur_rect(&x, &y, &h);
  dirty(x - 3, y - 3, CW + 6, h + 6);
}
static void set_cursor(int p, int d) {
  dirty_cursor();
  cp = p, cd = p >= T0 ? max(min(d, run(p)), 1) : 1;
  if (p < 0 || p <= WASTE) lastl = p;
  else if (p < T0) lastr = p;
  dirty_cursor();
}
static void say(const char *s) {
  msg = s, msg_until = now + 1800;
  dirty_toast();
}

/* with Keep score, Vegas shows the bank, which already holds a finished game */
static int32_t shown_score(void) { return V.gscoring == 1 && V.keep ? V.bank + (V.live ? V.score : 0) : V.score; }
static int can_recycle(void) { return V.gscoring != 1 || V.passes < V.gdraw - 1; }

/* the text over the table: the bar's figures, and a message */
static void over_text(int x, int y, int w, int h) {
  char s[16], *o;
  if (y < BARH) {
    uint32_t t = V.live || V.ms ? V.ms / 1000 : 0;
    o = num(s, (int32_t)(t / 3600));
    o = two(cat(o, ":"), (int)(t / 60 % 60));
    two(cat(o, ":"), (int)(t % 60));
    str(s, 96, 2, 0, WHITE, BAR);
    if (V.gscoring < 2) {
      str(T("Score"), 162, 2, 0, RGB(0x9AA0A6), BAR);
      (V.gscoring ? money : num)(s, shown_score());
      str(s, 204, 2, 0, WHITE, BAR);
    }
    str(T("Moves"), 250, 2, 0, RGB(0x9AA0A6), BAR);
    num(s, V.moves);
    str(s, 292, 2, 0, WHITE, BAR);
  }
  if (msg && y + h > TOAST_Y && x < 320 - SIDEW && x + w > SIDEW) str(msg, 160 - slen(msg) * 7 / 2, TOAST_Y + 3, 0, WHITE, RGB(0x323232));
}

static void game_scene(void) {
  fill(rx, ry, rw, rh, FELT);
  if (ry < BARH) { /* the bar, and a king for the difficulty: red Easy, blue Hard */
    fill(0, 0, 320, BARH, BAR);
    int h = V.gdraw == 1 || !V.live ? 0 : 1;
    portrait(12, 6, 1, 1, h ? RGB(0x5B7FD6) : RGB(0xEF5050), h ? RGB(0x2F4F9A) : RGB(0xBB2026));
  }
  fill(0, BARH, SIDEW, 240, SIDE);
  fill(320 - SIDEW, BARH, SIDEW, 240, SIDE);
  int k = pn[STOCK] - 1, x, y;
  while (k >= 0 && !shown(pl[STOCK][k])) k--;
  if (k >= 0) { /* the deck, a little thicker when it holds more */
    for (int i = min(k / 8, 2); i; i--) fill(LX + 2, TABY + CH - 2 + i, CW - 4, 1, RGB(0x1E5A2A)), fill(LX + 1, TABY + CH - 3 + i, 1, 1, RGB(0x1E5A2A));
    card(pl[STOCK][k], LX, TABY);
  } else { /* the empty deck: a circle arrow to deal again */
    slot(LX, TABY, -1);
    if (V.live && can_recycle()) {
      ring(LX + 15, TABY + 21, 6, 8, WHITE);
      fill(LX + 15, TABY + 12, 9, 7, SLOT);
      for (int i = 0; i < 5; i++) fill(LX + 15 + i, TABY + 11 + i, 1, 10 - 2 * i, WHITE);
    }
  }
  for (k = max(pn[WASTE] - fan() - 1, 0); k < pn[WASTE]; k++)
    if (shown(pl[WASTE][k])) pos(WASTE, k, &x, &y), card(pl[WASTE][k], x, y);
  for (int p = F0; p < T0; p++) {
    k = pn[p] - 1;
    while (k >= 0 && !shown(pl[p][k])) k--;
    if (k >= 0) card(pl[p][k], RX, FY(p - F0));
    else slot(RX, FY(p - F0), FSUIT[p - F0]);
  }
  /* Undo and New, under the deck */
  for (int i = 0; i < 2; i++) {
    int by = i ? NEWY : UNDOY, cx = LX + CW / 2;
    if (!i) { /* a round arrow */
      ring(cx + 1, by + 6, 3, 5, WHITE);
      fill(cx - 5, by + 1, 7, 6, SIDE);
      for (int j = 0; j < 4; j++) fill(cx - 5 + j, by + 2 - j + 3, 1, 1 + 2 * j, WHITE);
    } else { /* a star */
      for (int j = 0; j < 9; j++) {
        int w = j < 3 ? j / 2 : j < 5 ? 5 - (j - 3) : 2 + (j - 5) / 2;
        fill(cx - w, by + j, 2 * w + 1, 1, WHITE);
      }
      fill(cx - 4, by + 8, 3, 2, WHITE), fill(cx + 2, by + 8, 3, 2, WHITE), fill(cx - 1, by + 8, 3, 1, SIDE);
    }
    if (NP_TEXT_EXTRA) xtext(i ? T("NEW") : T("UNDO"), cx - xtext(i ? T("NEW") : T("UNDO"), 0, 0, 0, 0) / 2, by + 13, WHITE, 1);
    else text(i ? T("NEW") : T("UNDO"), cx - (i ? 9 : 12), by + 13, WHITE);
  }
  for (int p = T0; p < NP; p++) {
    if (!pn[p] || !shown(pl[p][0])) slot(COLX(p - T0), TABY, -1);
    for (k = 0; k < pn[p]; k++)
      if (shown(pl[p][k])) pos(p, k, &x, &y), card(pl[p][k], x, y);
  }
  if (sp >= 0) { /* the cards picked up, in Google blue */
    int y1;
    pos(sp, pn[sp] - sn, &x, &y);
    pos(sp, pn[sp] - 1, &x, &y1);
    rframe(x - 2, y - 2, CW + 4, y1 - y + CH + 4, BLUE);
  }
  if (hint_p >= 0 && (now / 200 & 1)) {
    pos(hint_p, max(pn[hint_p] - 1, 0), &x, &y);
    rframe(x - 2, y - 2, CW + 4, CH + 4, RGB(0x40E8FF));
  }
  if (!nocur) {
    int h;
    cur_rect(&x, &y, &h);
    rframe(x - 3, y - 3, CW + 6, h + 6, GOLD);
  }
  for (int i = 0; i < nfl; i++)
    if (fl[i].t >= 0) fly_pos(&fl[i], &x, &y), card(fl[i].c, x, y);
  if (msg) rbox(160 - slen(msg) * 7 / 2 - 8, TOAST_Y, slen(msg) * 7 + 16, 20, 6, RGB(0x323232));
}

/* ------------------------------------------------------------------ moves */
/* flies every card in flight to its place; skip lets a key land them all */
static void animate(int skip) {
  while (nfl) {
    int j = 0, x, y;
    for (int i = 0; i < nfl; i++) {
      fly_t *f = &fl[i];
      if (f->t >= 0) fly_pos(f, &x, &y), dirty(x, y, CW, CH);
      if (++f->t >= dur || (skip && pend)) {
        hid &= ~(1ull << (f->c & 63));
        dirty(f->x1, f->y1, CW, CH);
        continue;
      }
      if (f->t >= 0) fly_pos(f, &x, &y), dirty(x, y, CW, CH);
      fl[j++] = *f;
    }
    nfl = j;
    if (skip && pend) pend = 0;
    next_frame();
  }
}
static void launch(int c, int x0, int y0, int x1, int y1, int t) {
  if (nfl == NFLY) return;
  fl[nfl++] = (fly_t){(uint8_t)c, (int8_t)t, (int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
  hid |= 1ull << (c & 63);
}

/* Moving cards: begin() notes where they are and moves them in the state,
   fly() sends them from there to their new places. turn: one at a time,
   turned over (the stock and the waste); else as a block. */
static int16_t mx[NFLY], my[NFLY];
static int mn, mt, mturn;
static void begin(int f, int t, int n, int turn) {
  for (int k = 0; k < n && k < NFLY; k++) {
    int x, y;
    pos(f, turn ? pn[f] - 1 - k : pn[f] - n + k, &x, &y);
    mx[k] = (int16_t)x, my[k] = (int16_t)y;
  }
  dirty_pile(f), dirty_pile(t);
  for (int k = 0; k < n; k++) {
    int c = pl[f][turn ? pn[f] - 1 - k : pn[f] - n + k];
    pl[t][pn[t] + k] = (uint8_t)(turn ? (c & 63) | (t == WASTE ? UP : 0) : c);
  }
  pn[t] = (uint8_t)(pn[t] + n), pn[f] = (uint8_t)(pn[f] - n);
  mn = n, mt = t, mturn = turn;
}
static void fly(int frames, int stagger) {
  relayout();
  dur = frames;
  for (int k = 0; k < mn && k < NFLY; k++) {
    int i = pn[mt] - mn + k, x, y;
    pos(mt, i, &x, &y);
    launch(pl[mt][i], mx[k], my[k], x, y, -k * stagger);
  }
  animate(0);
  dirty_pile(mt);
}

/* the undo history: each move, and what it changed */
typedef struct {
  uint8_t f, t, n, fl; /* fl: 1 turned one at a time, 2 turned a card up after; the waste's spread << 4 */
  int16_t ds;          /* the score it gave */
} undo_t;
static undo_t U[256];
static uint8_t ut;
static int nu;
static void push_undo(int f, int t, int n, int flags, int ds) {
  U[ut++] = (undo_t){(uint8_t)f, (uint8_t)t, (uint8_t)n, (uint8_t)(flags | V.wfan << 4), (int16_t)ds};
  nu = min(nu + 1, 256);
}

static int add_score(int d) { /* what actually changed: Standard stops at 0 */
  int32_t s = V.score + d;
  if (!V.gscoring && s < 0) s = 0;
  d = (int)(s - V.score);
  V.score = s;
  dirty_bar();
  return d;
}
/* the clock, while a game is on; Standard loses 2 points every 10 seconds */
static void tick(uint32_t dt) {
  if (!V.live || !V.started) return;
  uint32_t t0 = V.ms;
  V.ms += dt;
  if (V.ms / 10000 != t0 / 10000 && !V.gscoring && V.gtimed) add_score(-2);
  if (V.ms / 1000 != t0 / 1000) dirty(90, 0, 60, BARH);
}
static void counted(void) {
  if (!V.started) V.started = 1, V.played++;
  V.moves++;
  hint_p = -1;
  dirty_bar();
}
static void deselect(void) {
  if (sp >= 0) dirty_pile(sp);
  sp = -1;
}

static void move(int f, int n, int t, int frames) {
  push_undo(f, t, n, 0, 0);
  undo_t *u = &U[(uint8_t)(ut - 1)];
  begin(f, t, n, 0);
  if (f == WASTE && V.wfan > 1) V.wfan--;
  u->ds = (int16_t)add_score(!V.gscoring ? (t < T0 ? 10 : f == WASTE ? 5 : f < T0 ? -15 : 0) :
                             V.gscoring == 1 ? (t < T0 ? 5 : f >= F0 && f < T0 ? -5 : 0) : 0);
  if (f >= T0 && pn[f] && !(pl[f][pn[f] - 1] & UP)) { /* turn the card underneath */
    pl[f][pn[f] - 1] |= UP;
    u->fl |= 2;
    if (!V.gscoring) u->ds = (int16_t)(u->ds + add_score(5));
  }
  counted();
  fly(frames, 0);
}

static void draw_cards(void) {
  if (sp == WASTE) deselect();
  if (pn[STOCK]) {
    int n = min(V.gdraw, pn[STOCK]);
    push_undo(STOCK, WASTE, n, 1, 0);
    begin(STOCK, WASTE, n, 1);
    V.wfan = (uint8_t)n;
  } else if (!pn[WASTE]) {
    return;
  } else if (!can_recycle()) {
    say(T("No more passes"));
    return;
  } else { /* the waste goes back to the stock */
    int d = V.gscoring ? 0 : V.gdraw == 1 ? -100 : V.passes >= 2 ? -20 : 0;
    push_undo(WASTE, STOCK, pn[WASTE], 1, 0);
    begin(WASTE, STOCK, pn[WASTE], 1);
    U[(uint8_t)(ut - 1)].ds = (int16_t)add_score(d);
    V.passes++, V.wfan = 0;
    counted();
    fly(8, 0);
    return;
  }
  counted();
  fly(8, 3);
}

static void undo(void) {
  if (!nu) {
    say(T("Nothing to undo"));
    return;
  }
  deselect();
  nu--;
  undo_t u = U[--ut];
  if (u.fl & 2) pl[u.f][pn[u.f] - 1] &= (uint8_t)~UP;
  begin(u.t, u.f, u.n, u.fl & 1);
  V.wfan = u.fl >> 4;
  add_score(-u.ds);
  if (u.f == WASTE && u.t == STOCK) V.passes--;
  if (V.moves) V.moves--;
  hint_p = -1;
  fly(8, 0);
}

/* the lowest card that can go to a foundation, or 0 */
static int to_foundation(int frames) {
  int best = 99, bf = 0, bt = 0;
  for (int f = WASTE; f < NP; f++) {
    if (f >= F0 && f < T0) continue;
    for (int t = F0; t < T0; t++)
      if (legal(f, 1, t) && RANK(pl[f][pn[f] - 1]) < best) best = RANK(pl[f][pn[f] - 1]), bf = f, bt = t;
  }
  if (best == 99) return 0;
  move(bf, 1, bt, frames);
  return 1;
}

static void hint(void) {
  int f, t, n = 1;
  for (f = WASTE; f < NP; f++)
    for (t = F0; t < T0; t++)
      if ((f < F0 || f >= T0) && legal(f, 1, t)) goto found;
  for (f = T0; f < NP; f++) { /* a run that turns a card up or empties its column */
    n = run(f);
    int base = pn[f] - n;
    if (n && (base ? !(pl[f][base - 1] & UP) : 1))
      for (t = T0; t < NP; t++)
        if (legal(f, n, t) && (base || pn[t])) goto found;
  }
  for (f = WASTE, n = 1, t = T0; t < NP; t++)
    if (legal(WASTE, 1, t)) goto found;
  if (pn[STOCK] || (pn[WASTE] && can_recycle())) {
    set_cursor(STOCK, 1);
    say(T("Draw a card"));
  } else {
    say(T("No more moves"));
  }
  return;
found:
  deselect();
  set_cursor(f, n);
  hint_p = t, hint_until = now + 1600;
  say(T("Hint"));
}

static int difficulty(int full);
static void new_deal(uint32_t s);
/* OK: pick up, put down, draw, or a button */
static void act(void) {
  if (cp == B_UNDO) {
    undo();
    return;
  }
  if (cp == B_NEW) {
    int d = difficulty(0);
    dirty(0, 0, 320, 240);
    if (d >= 0) V.draw = (uint8_t)(d ? 3 : 1), new_deal(rnd());
    return;
  }
  if (cp == STOCK) {
    draw_cards();
    return;
  }
  int n = cp >= T0 ? cd : 1;
  if (sp < 0) {
    if (n <= run(cp)) sp = cp, sn = n, dirty_pile(cp);
    return;
  }
  if (cp == sp) { /* a second OK sends the card up, like a double click */
    int f = sp;
    deselect();
    for (int t = F0; t < T0 && sn == 1; t++)
      if (legal(f, 1, t)) {
        move(f, 1, t, 9);
        return;
      }
    return;
  }
  for (n = 1; n <= run(sp) && !legal(sp, n, cp); n++) {} /* only one count can fit */
  if (n > run(sp)) {
    say(T("Not there"));
    return;
  }
  int f = sp;
  deselect();
  move(f, n, cp, 9);
}

/* ------------------------------------------------------------------ games */
static void deal(uint32_t s) {
  uint8_t d[52];
  for (int i = 0; i < 52; i++) d[i] = (uint8_t)i;
  V.seed = s;
  s |= 1;
  for (int i = 51; i > 0; i--) {
    s ^= s << 13, s ^= s >> 17, s ^= s << 5;
    int j = (int)(s % (uint32_t)(i + 1));
    uint8_t c = d[i];
    d[i] = d[j], d[j] = c;
  }
  for (int p = 0; p < NP; p++) pn[p] = 0;
  int k = 0;
  for (int r = 0; r < 7; r++)
    for (int c = r; c < 7; c++) pl[T0 + c][pn[T0 + c]++] = (uint8_t)(d[k++] | (c == r ? UP : 0));
  while (k < 52) pl[STOCK][pn[STOCK]++] = d[k++];
  V.live = 1, V.started = 0, V.moves = 0, V.ms = 0, V.passes = 0, V.wfan = 0;
  V.gdraw = V.draw, V.gscoring = V.scoring, V.gtimed = V.timed;
  V.score = V.gscoring == 1 ? -52 : 0;
  nu = 0, sp = -1, hint_p = -1, cd = 1;
  relayout();
}
/* the cards fly from the stock to the columns, row by row */
static void deal_fly(void) {
  for (int r = 0, k = 0; r < 7; r++)
    for (int c = r; c < 7; c++, k++) {
      int x, y;
      pos(T0 + c, r, &x, &y);
      launch(pl[T0 + c][r], LX, TABY, x, y, -k * 2);
    }
  dirty(0, 0, 320, 240);
  dur = 10;
  animate(1);
}

static void save(void) {
  V.magic = 'S', V.version = 1;
  for (int p = 0, k = 0; p < NP; p++) {
    V.n[p] = pn[p];
    for (int i = 0; i < pn[p] && k < 52; i++) V.cards[k++] = pl[p][i];
  }
  ef_write(SAVE_NAME, &V, sizeof V);
}

/* reads the save, keeping only what makes sense */
static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  V.draw = 1, V.timed = 1;
  if (!d || n != sizeof V || d[0] != 'S' || d[1] != 1) return;
  for (uint32_t i = 0; i < n; i++) ((uint8_t *)&V)[i] = d[i];
  if (V.draw != 1 && V.draw != 3) V.draw = V.draw == 2 ? 3 : 1;
  V.scoring %= 3, V.timed &= 1, V.keep &= 1, V.back &= 3;
  uint64_t seen = 0;
  int k = 0, ok = V.live == 1 && V.gdraw >= 1 && V.gdraw <= 3 && V.gscoring < 3 && V.gtimed < 2;
  for (int p = 0; p < NP && ok; p++) {
    if (V.n[p] > 52 - k) ok = 0;
    for (int i = 0; ok && i < V.n[p]; i++, k++) {
      int c = V.cards[k] & 63, up = p == WASTE || (p >= F0 && p < T0) || (p >= T0 && (V.cards[k] & UP || i == V.n[p] - 1 || (i && pl[p][i - 1] & UP)));
      if (c >= 52 || seen >> c & 1) ok = 0;
      if (p >= F0 && p < T0 && (RANK(c) != i || SUIT(c) != FSUIT[p - F0])) ok = 0;
      seen |= 1ull << c;
      pl[p][i] = (uint8_t)(c | (up ? UP : 0));
    }
    pn[p] = V.n[p];
  }
  if (!ok || k != 52) {
    V.live = 0;
    for (int p = 0; p < NP; p++) pn[p] = 0;
  }
  V.started &= 1;
}

static void abandon(void) {
  if (V.live && V.started) {
    V.streak = 0;
    if (V.gscoring == 1) V.bank += V.score;
  }
  V.live = 0;
}

/* ------------------------------------------------------------------ the win */
/* one card straight onto the screen, over whatever is there (c < 0: the
   empty foundation of suit -1 - c) */
static void stamp(int c, int x, int y) {
  rx = max(x, 0), ry = max(y, 0), rw = min(x + CW, 320) - rx, rh = min(y + CH, 240) - ry;
  if (rw <= 0 || rh <= 0) return;
  eadk_rect_t r = {(uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh};
  eadk_display_pull_rect(r, buf);
  if (c < 0) fill(x, y, CW, CH, SIDE), slot(x, y, -1 - c);
  else card(c, x, y);
  eadk_display_push_rect(r, buf);
}

/* The famous ending: the cards leave the foundations one by one, kings
   first, bounce along the bottom of the screen and leave a trail. */
static void bounce(void) {
  ndr = 0;
  take();
  for (int r = 12; r >= 0; r--)
    for (int p = F0; p < T0; p++) {
      if (!pn[p]) continue;
      int c = pl[p][--pn[p]], x = RX * 16, y = FY(p - F0) * 16;
      int vx = -(int)(rnd() % 40 + 20), vy = -(int)(rnd() % 90);
      stamp(pn[p] ? pl[p][pn[p] - 1] : -1 - FSUIT[p - F0], RX, FY(p - F0));
      while (x > -CW * 16 && x < 320 * 16) {
        for (int s = 0; s < 5; s++) {
          x += vx, vy += 9, y += vy;
          if (y > (240 - CH) * 16) y = (240 - CH) * 16, vy = -vy * 13 / 16;
          stamp(c, x >> 4, y >> 4);
        }
        next_frame();
        if (take()) return;
      }
    }
}

static int won(void) { return pn[F0] + pn[F0 + 1] + pn[F0 + 2] + pn[F0 + 3] == 52; }

/* after every move: finish by itself once every card is face up */
static void settle(void) {
  if (!pn[STOCK] && !pn[WASTE] && !won()) {
    for (int i = 0; i < 7; i++)
      if (fd[i]) return;
    deselect();
    while (to_foundation(6)) {}
  }
}

static void finish(void) {
  uint32_t secs = V.ms / 1000;
  V.live = 0, V.won++, V.streak++;
  if (V.streak > V.best_streak) V.best_streak = V.streak;
  if (!V.gscoring && V.gtimed && secs >= 30) V.score += (int32_t)(700000 / secs); /* the bonus for speed */
  if (V.gscoring == 1) V.bank += V.score;
  if (!V.best_time || secs < V.best_time) V.best_time = (uint16_t)min((int)secs, 65535);
  if (!V.gscoring && V.score > V.best_score) V.best_score = V.score;
  save();
}

/* ------------------------------------------------------------------ play */
static const char *const yes_no[] = {T("Yes"), T("No")}, *const ok_btn[] = {T("OK")};

static void new_deal(uint32_t s) {
  abandon();
  deal(s);
  layer = 0;
  set_cursor(T0 + 3, 1);
  deal_fly();
}

/* the side column a pile is in: -1 the deck's, 7 the foundations', else a column */
static int col(int p) { return p < 0 || p <= WASTE ? -1 : p < T0 ? 7 : p - T0; }

/* the game, until the player leaves it */
static void play(void) {
  static const char *const pause_items[] = {T("Resume"), T("Restart"), T("New game"), T("Quit game")};
  static const int8_t LEFT[4] = {STOCK, WASTE, B_UNDO, B_NEW};
  layer = 0, sp = -1, hint_p = -1, msg = 0, ticking = 1;
  relayout();
  set_cursor(cp, cd);
  dirty(0, 0, 320, 240);
  for (;;) {
    next_frame();
    ticking = 1;
    uint64_t e = take();
    if (msg && (int32_t)(now - msg_until) > 0) msg = 0, dirty_toast();
    if (hint_p >= 0) {
      if ((int32_t)(now - hint_until) > 0) dirty_pile(hint_p), hint_p = -1;
      else if (now / 200 != (now - 16) / 200) dirty_pile(hint_p);
    }
    int lr = (e & KEY(eadk_key_right) ? 1 : 0) - (e & KEY(eadk_key_left) ? 1 : 0);
    int ud = (e & KEY(eadk_key_down) ? 1 : 0) - (e & KEY(eadk_key_up) ? 1 : 0);
    if (lr) { /* across: the deck's column, the seven columns, the foundations' */
      int c = col(cp) + lr;
      c = c < -1 ? 7 : c > 7 ? -1 : c;
      set_cursor(c == -1 ? lastl : c == 7 ? lastr : T0 + c, 1);
    } else if (ud && col(cp) == -1) { /* up and down a side column */
      int i = 0;
      while (LEFT[i] != cp) i++;
      set_cursor(LEFT[max(0, min(3, i + ud))], 1);
    } else if (ud && col(cp) == 7) {
      set_cursor(F0 + max(0, min(3, cp - F0 + ud)), 1);
    } else if (ud < 0 && sp < 0 && cd < run(cp)) { /* in a column: pick more cards, or fewer */
      set_cursor(cp, cd + 1);
    } else if (ud > 0) {
      set_cursor(cp, cd - 1);
    }
    if (e & KEY(eadk_key_ok)) act();
    if (e & KEY(eadk_key_exe)) draw_cards();
    if (e & KEY(eadk_key_toolbox)) {
      deselect();
      if (!to_foundation(7)) say(T("Nothing to play"));
      while (to_foundation(7)) {}
    }
    if (e & KEY(eadk_key_backspace)) undo();
    if (e & KEY(eadk_key_shift)) hint();
    if (e & KEY(eadk_key_back)) {
      if (sp >= 0) {
        deselect();
      } else {
        for (;;) {
          layer = 1, paint(0, 0, 320, 240), layer = 0; /* the table, dimmed under the dialog */
          int r = dialog(T("Paused"), 0, pause_items, 4, 1, 0);
          if (r == 3) {
            if (!dialog(T("Solitaire"), T("Quit game?"), yes_no, 2, 0, 1)) np_jump(leave); /* saves, then leaves */
            continue;
          }
          if (r == 2) {
            int d = difficulty(0);
            if (d < 0) continue;
            V.draw = (uint8_t)(d ? 3 : 1);
          }
          dirty(0, 0, 320, 240);
          if (r == 1 || r == 2) new_deal(r == 1 ? V.seed : rnd());
          break;
        }
      }
    }
    if (cp >= T0 && cd > max(run(cp), 1)) set_cursor(cp, cd); /* the column changed */
    settle();
    if (won() && V.live) {
      char s[96], *o = s;
      finish();
      o = num(cat(o, T("Time: ")), (int32_t)(V.ms / 1000));
      o = num(cat(o, T(" seconds\nMoves: ")), V.moves);
      if (V.gscoring < 2) o = (V.gscoring ? money : num)(cat(o, T("\nScore: ")), shown_score());
      cat(o, T("\n\nPlay again?"));
      next_frame();
      nocur = 1, bounce(), nocur = 0;
      layer = 1, paint(0, 0, 320, 240), layer = 0;
      if (dialog(T("You win!"), s, yes_no, 2, 0, 0)) return;
      new_deal(rnd());
    }
  }
}

/* ------------------------------------------------------------------ the start */
/* Google's "Choose your difficulty", over the dimmed table: a red king for
   Easy (draw 1), a blue one for Hard (draw 3). full adds Continue (with a
   game to continue) and Options, Statistics and How to play. Returns 0 Easy,
   1 Hard, 2 Continue, 3 Options, 4 Statistics, 5 How to play, or -1 (Back). */
static int drow, dsel;
static void difficulty_draw(int full, int all) {
  static const char *const LINKS[3] = {T("Options"), T("Stats"), T("Help")};
  int live = full && V.live, h = full ? (live ? 176 : 148) : 120, x = 48, y = (240 - h) / 2;
  if (all) {
    layer = 1, paint(0, 0, 320, 240), layer = 0;
    widget(W_WIN, x, y, 224, h, 0, 0);
    str(T("Choose your difficulty"), 160 - (NP_TEXT_EXTRA ? slen(T("Choose your difficulty")) : 22) * 7 / 2, y + 8, 0, GREY, WHITE);
  }
  for (int i = 0; i < 2; i++) { /* the kings */
    bool on = drow == 0 && dsel == i;
    int kx = i ? 184 : 88, l = layer;
    const char *lb = i ? T("HARD") : T("EASY");
    int big = 1, lw = 0;
    if (NP_TEXT_EXTRA) big = slen(lb) * 10 <= 56, lw = slen(lb) * (big ? 10 : 7); /* (a long word: small) */
    wt = W_WIN, wf = 0, layer = 3;
    rx = kx - 4, ry = y + 26, rw = 56, rh = 86;
    if (NP_TEXT_EXTRA && lw + 6 > rw) rx -= (lw + 7 - rw) / 2, rw = lw + 6;
    fill(rx, ry, rw, rh, WHITE);
    if (on) rbox(rx, ry, rw, rh, 7, PALE);
    portrait(12, kx, y + 30, 2, i ? RGB(0x5B7FD6) : RGB(0xEF5050), i ? RGB(0x2F4F9A) : RGB(0xBB2026));
    eadk_display_push_rect((eadk_rect_t){(uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh}, buf);
    layer = l;
    if (NP_TEXT_EXTRA) str(lb, kx + 24 - lw / 2, y + (big ? 90 : 93), big, INK, on ? PALE : WHITE);
    else str(lb, kx + 4, y + 90, 1, INK, on ? PALE : WHITE);
  }
  if (live) widget(W_BTN, 100, y + 118, 120, 22, T("Continue"), drow == 1 ? F_FOCUS : 0);
  int links = NP_TEXT_EXTRA ? slen(LINKS[0]) + slen(LINKS[1]) + slen(LINKS[2]) : 16;
  if (full)
    for (int i = 0, lx = 160 - (7 * links + 3 * 16 + 2 * 8) / 2; i < 3; lx += slen(LINKS[i]) * 7 + 24, i++) {
      bool on = drow == 2 && dsel == i;
      int w = slen(LINKS[i]) * 7 + 16, ly = y + h - 30, l = layer;
      layer = 3, rx = lx, ry = ly, rw = w, rh = 22;
      fill(rx, ry, rw, rh, WHITE);
      if (on) rbox(rx, ry, rw, rh, 7, BLUE);
      eadk_display_push_rect((eadk_rect_t){(uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh}, buf);
      layer = l;
      str(LINKS[i], lx + 8, ly + 4, 0, on ? WHITE : BLUE, on ? BLUE : WHITE);
    }
}
static int difficulty(int full) {
  int live = full && V.live;
  drow = live ? 1 : 0, dsel = V.draw == 3;
  ndr = 0, ticking = 0;
  difficulty_draw(full, 1);
  for (uint64_t e = 0;; e = take()) {
    int lr = (e & KEY(eadk_key_right) ? 1 : 0) - (e & KEY(eadk_key_left) ? 1 : 0);
    int ud = (e & KEY(eadk_key_down) ? 1 : 0) - (e & KEY(eadk_key_up) ? 1 : 0);
    if (ud && full) {
      drow = max(0, min(2, drow + ud));
      if (drow == 1 && !live) drow += ud;
      drow = max(0, min(2, drow)), dsel = drow == 0 ? V.draw == 3 : 0;
    }
    if (lr) dsel = drow == 0 ? !dsel : drow == 2 ? (dsel + lr + 3) % 3 : 0;
    if (lr || ud) difficulty_draw(full, 0);
    if (e & OK_KEYS) return drow == 0 ? dsel : drow == 1 ? 2 : 3 + dsel;
    if (e & KEY(eadk_key_back)) return -1;
    next_frame();
  }
}

static void scene(void) {
  if (layer == 2) {
    widget_draw();
  } else {
    game_scene();
    if (layer == 1) shadow(rx, ry, rw, rh, 20); /* dimmed under a dialog */
  }
}

static void options(void) {
  static const char *const lbl[5] = {T("Standard"), T("Vegas"), T("None"), T("Timed game"), T("Keep score")};
  int x = 22, y = 9, f = 0;
  ndr = 0;
  layer = 1, paint(0, 0, 320, 240), layer = 0;
  win(x, y, 276, 222, T("Options"), 0);
  widget(W_GROUP, x + 16, y + 36, 110, 16, T("Scoring"), 0);
  widget(W_GROUP, x + 146, y + 36, 110, 16, T("Game"), 0);
  widget(W_GROUP, x + 16, y + 120, 110, 16, T("Card back"), 0);
  for (uint64_t e = 0;; e = take()) {
    int o = f;
    if (e & (KEY(eadk_key_up) | KEY(eadk_key_left))) f = (f + 10) % 11;
    if (e & (KEY(eadk_key_down) | KEY(eadk_key_right))) f = (f + 1) % 11;
    if (e & OK_KEYS) {
      if (f == 10) break;
      if (f < 3) V.scoring = (uint8_t)f;
      else if (f == 3) V.timed ^= 1;
      else if (f == 4) V.keep ^= 1;
      else if (f < 9) V.back = (uint8_t)(f - 5);
      else f = 10;
    }
    if (e & KEY(eadk_key_back)) break;
    for (int i = 0; i < 11; i++) {
      if (e && i != f && i != o && !(e & OK_KEYS)) continue;
      if (i == 10) {
        widget(W_BTN, x + 196, y + 188, 64, 24, T("OK"), f == 10 ? F_FOCUS : 0);
      } else if (i >= 5 && i < 9) {
        for (int j = 5; j < 9 && (e & OK_KEYS); j++) /* the one on changed */
          if (j != i) widget(W_BACK, x + 12 + (j - 5) * 42, y + 138, CW + 8, CH + 8, 0, (j == f) * F_FOCUS | (V.back == j - 5) * F_ON | (j - 5) << 4);
        widget(W_BACK, x + 12 + (i - 5) * 42, y + 138, CW + 8, CH + 8, 0, (i == f) * F_FOCUS | (V.back == i - 5) * F_ON | (i - 5) << 4);
      } else if (i < 9) {
        int on = i == 3 ? V.timed : i == 4 ? V.keep : V.scoring == i;
        widget(i < 3 ? W_RADIO : W_CHECK, x + (i < 3 ? 16 : 146), y + 56 + (i < 3 ? i : i - 3) * 20, 124, 16, lbl[i], on | (i == f) << 1);
      }
    }
    next_frame();
  }
  save();
}

static void stats(void) {
  char s[200], *o = s;
  o = num(cat(o, T("Games played: ")), V.played);
  o = num(cat(o, T("\nGames won: ")), V.won);
  if (V.played) o = cat(num(cat(o, " ("), V.won * 100 / V.played), T("%)"));
  o = num(cat(o, T("\nStreak: ")), V.streak);
  o = num(cat(o, T("   Best: ")), V.best_streak);
  o = cat(o, T("\nBest time: "));
  if (V.best_time) o = two(cat(num(o, V.best_time / 60), ":"), V.best_time % 60);
  else o = cat(o, "-");
  o = num(cat(o, T("\nBest score: ")), V.best_score);
  money(cat(o, T("\nVegas bank: ")), V.bank);
  layer = 1, paint(0, 0, 320, 240), layer = 0;
  dialog(T("Statistics"), s, ok_btn, 1, 0, 0);
}

static void help(void) {
  layer = 1, paint(0, 0, 320, 240), layer = 0;
  /* (one text, so that it is translated as one: the keys line up in the font's cells) */
  dialog(T("How to play"),
         T("Build the four suits up, ace to\nking, on the right. In the columns,\ngo down, alternating red and black.\nOK         Pick up, put down\nOK twice   Send to its foundation\nUp, Down   Pick more or fewer\nEXE        Draw from the deck\nToolbox    Play all you can up\nBackspace  Undo      Shift  Hint"),
         ok_btn, 1, 0, 0);
}

int main(void) {
  np_app_begin();
  if (np_save_jump(leave)) {
    save();
    return np_app_end();
  }
  now = last_frame = (uint32_t)eadk_timing_millis();
  seed ^= eadk_random() ^ now;
  load();
  held = eadk_keyboard_scan();
  nocur = 1;
  for (;;) {
    relayout();
    int a = difficulty(1);
    if (a == 2) {
      nocur = 0, play();
    } else if (a == 0 || a == 1) {
      V.draw = (uint8_t)(a ? 3 : 1), nocur = 0;
      new_deal(rnd());
      play();
    } else if (a == 3) {
      options();
    } else if (a == 4) {
      stats();
    } else if (a == 5) {
      help();
    } else {
      layer = 1, paint(0, 0, 320, 240), layer = 0;
      if (!dialog(T("Solitaire"), T("Quit game?"), yes_no, 2, 0, 1)) np_jump(leave);
    }
    nocur = 1;
  }
}
