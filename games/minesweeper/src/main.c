/* Minesweeper, like the one that came with Windows: the gray field, the red
 * LED counters and the smiley, on the teal desktop. On top of the classic
 * rules it has a "no guessing" option that only deals fields you can clear by
 * logic alone, a cursor that wraps around, and it keeps your game when you
 * leave.
 *
 * Drawing uses dirty rectangles: a cell is composed in a tiny buffer and
 * pushed only when it changes. Cells that change together (an opening, the
 * mines after a loss) go through a queue a ring at a time, so an opening
 * spreads out like a wave. Text uses the calculator's own font. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_text.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Minesweeper";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define GRAY RGB(0xC0C0C0)
#define DARK RGB(0x808080)
#define WHITE 0xFFFF
#define BLACK 0
#define NAVY RGB(0x000080)
#define TEAL RGB(0x008080)
#define RED RGB(0xFF0000)
#define YELLOW RGB(0xFFFF00)

/* what the keys do: the arrows (or 8 4 6 2), open (OK, EXE, 5), flag (Backspace, Shift, 0) */
enum { K_UP = 1, K_DOWN = 2, K_LEFT = 4, K_RIGHT = 8, K_OK = 16, K_FLAG = 32, K_BACK = 64, K_HOME = 128, K_ANY = 256 };
static const uint8_t keymap[][2] = {
  {eadk_key_up, K_UP}, {eadk_key_eight, K_UP}, {eadk_key_down, K_DOWN}, {eadk_key_two, K_DOWN},
  {eadk_key_left, K_LEFT}, {eadk_key_four, K_LEFT}, {eadk_key_right, K_RIGHT}, {eadk_key_six, K_RIGHT},
  {eadk_key_ok, K_OK}, {eadk_key_exe, K_OK}, {eadk_key_five, K_OK}, {eadk_key_backspace, K_FLAG},
  {eadk_key_shift, K_FLAG}, {eadk_key_zero, K_FLAG}, {eadk_key_back, K_BACK}, {eadk_key_home, K_HOME},
  {eadk_key_on_off, K_HOME},
};
static int keys(void) {
  uint64_t k = eadk_keyboard_scan();
  int a = k ? K_ANY : 0;
  for (unsigned i = 0; i < sizeof keymap / 2; i++)
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
static int iabs(int v) { return v < 0 ? -v : v; }
static int imin(int a, int b) { return a < b ? a : b; }

/* ------------------------------------------------------------------ drawing */
static void fill(int x, int y, int w, int h, color c) {
  if (w > 0 && h > 0) eadk_display_push_rect_uniform((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, c);
}
/* pix: a small picture composed in RAM, pw pixels wide, then pushed at once */
static color pix[34 * 34];
static int pw;
static void pfill(int x, int y, int w, int h, color c) {
  for (int j = y; j < y + h; j++)
    for (int i = x; i < x + w; i++) pix[j * pw + i] = c;
}
static void push(int x, int y, int h) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)pw, (uint16_t)h}, pix);
}
typedef void (*filler)(int, int, int, int, color);
/* a 3D edge t pixels thick: lt on the top and left, dk on the bottom and right */
static void edge(filler f, int x, int y, int w, int h, int t, color lt, color dk) {
  for (; t--; x++, y++, w -= 2, h -= 2) {
    f(x, y, w - 1, 1, lt);
    f(x, y + 1, 1, h - 2, lt);
    f(x, y + h - 1, w, 1, dk);
    f(x + w - 1, y, 1, h - 1, dk);
  }
}
/* Windows 95's raised edge (buttons, windows, menus) and sunken one (fields) */
static void raised(int x, int y, int w, int h) {
  edge(fill, x, y, w, h, 1, WHITE, BLACK);
  edge(fill, x + 1, y + 1, w - 2, h - 2, 1, GRAY, DARK);
}
static void sunken(int x, int y, int w, int h) {
  edge(fill, x, y, w, h, 1, DARK, WHITE);
  edge(fill, x + 1, y + 1, w - 2, h - 2, 1, BLACK, GRAY);
}
/* fills (x, y, w, h) except the rectangle (X, Y, W, H) inside it */
static void around(int x, int y, int w, int h, int X, int Y, int W, int H, color c) {
  fill(x, y, w, Y - y, c);
  fill(x, Y + H, w, y + h - Y - H, c);
  fill(x, Y, X - x, H, c);
  fill(X + W, Y, x + w - X - W, H, c);
}

static void text(const char *s, int x, int y, color fg, color bg) {
  eadk_display_draw_string(s, (eadk_point_t){(uint16_t)x, (uint16_t)y}, false, fg, bg);
}
static int slen(const char *s) { return np_text_cells(s); } /* (Chinese letters take two cells) */
static void centered(const char *s, int cx, int y, color fg, color bg) { text(s, cx - slen(s) * 7 / 2, y, fg, bg); }
static char *num(char *o, int v) {
  char t[8];
  int n = 0;
  if (v < 0) *o++ = '-', v = -v;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}
/* "12.3 s" from tenths of a second */
static char *secs(char *o, int t) {
  o = num(o, t / 10);
  *o++ = '.';
  o = num(o, t % 10);
  return cat(o, T(" s"));
}

/* ------------------------------------------------------------------ pictures */
/* 1 to 8 and ?, bold like Windows' */
static const uint8_t glyphs[9][7] = {
  {0x0C, 0x1C, 0x3C, 0x0C, 0x0C, 0x0C, 0x3F}, {0x1E, 0x33, 0x03, 0x0E, 0x18, 0x30, 0x3F},
  {0x1E, 0x33, 0x03, 0x0E, 0x03, 0x33, 0x1E}, {0x06, 0x0E, 0x1E, 0x36, 0x3F, 0x06, 0x06},
  {0x3F, 0x30, 0x3E, 0x03, 0x03, 0x33, 0x1E}, {0x1E, 0x30, 0x3E, 0x33, 0x33, 0x33, 0x1E},
  {0x3F, 0x03, 0x06, 0x06, 0x0C, 0x0C, 0x0C}, {0x1E, 0x33, 0x33, 0x1E, 0x33, 0x33, 0x1E},
  {0x1E, 0x33, 0x03, 0x0E, 0x0C, 0x00, 0x0C},
};
static const color ink[9] = {RGB(0x0000FF), RGB(0x008000), RGB(0xFF0000), RGB(0x000080), RGB(0x800000),
                             RGB(0x008080), BLACK, DARK, BLACK};
/* glyph g (0 is "1") at q/2 times its size (q 2 to 4, so 1.5 too), centred in
   the s x s square at (x, y); a glyph pixel i starts at (i * q + 1) / 2 */
static void glyph(int g, int x, int y, int s, int q) {
  x += (s - (6 * q + 1) / 2 + 1) / 2, y += (s - (7 * q + 1) / 2 + 1) / 2;
  for (int j = 0; j < 7; j++)
    for (int i = 0; i < 6; i++) {
      int x0 = (i * q + 1) / 2, y0 = (j * q + 1) / 2;
      if (glyphs[g][j] >> (5 - i) & 1) pfill(x + x0, y + y0, (i * q + q + 1) / 2 - x0, (j * q + q + 1) / 2 - y0, ink[g]);
    }
}
/* a mine m pixels across (m odd): a ball with spikes and a glint */
static void mine(int x, int y, int m) {
  int c = m / 2, r = (c - 2) * (c - 1), g = c / 3;
  for (int j = 0; j < m; j++)
    for (int i = 0; i < m; i++) {
      int dx = i - c, dy = j - c;
      if (dx * dx + dy * dy <= r || !dx || !dy || (iabs(dx) == iabs(dy) && iabs(dx) == c - 2))
        pix[(y + j) * pw + x + i] = dx < 0 && dy < 0 && dx >= -g && dy >= -g ? WHITE : BLACK;
    }
}
/* the red flag on its pole and stand, for an s x s square (bolder in small ones) */
static void flag(int s) {
  int t = s < 14, p = s / 2, y = t ? 1 : s / 5, h = t ? 4 : s / 3 | 1, b = s * 2 / 3 + t, a = s / 6 + t, c = s / 4 + t;
  for (int r = 0; r < h; r++) {
    int w = imin(2 + 2 * imin(r, h - 1 - r), p - 2 + t); /* clear of the bevel */
    pfill(p - w, y + r, w, 1, RED);
  }
  pfill(p, y, 1, b - y, BLACK);
  pfill(p - a, b, a * 2 + 1, 1, BLACK);
  pfill(p - c, b + 1, c * 2 + 1, s / 8, BLACK);
}

/* the smiley's eyes and mouth, rows 5 to 13 of 17, left half (the other is mirrored) */
enum { F_SMILE, F_OH, F_COOL, F_DEAD };
static const uint16_t faces[4][9] = {
  {0, 0x60, 0x60, 0, 0, 0x10, 0x20, 0x1C0, 0},
  {0x60, 0x60, 0x60, 0, 0, 0x180, 0x40, 0x40, 0x180},
  {0x1FE, 0xF8, 0x70, 0, 0, 0x10, 0x20, 0x1C0, 0},
  {0x50, 0x20, 0x50, 0, 0, 0x1C0, 0x20, 0x10, 0},
};
static void face(int x, int y, int k, int f) {
  for (int j = 0; j < 17; j++)
    for (int i = 0; i < 17; i++) {
      int d = (i - 8) * (i - 8) + (j - 8) * (j - 8);
      if (d > 72) continue;
      color c = d > 56 ? BLACK : YELLOW;
      if (j >= 5 && j < 14 && faces[f][j - 5] >> (i < 9 ? i : 16 - i) & 1) c = BLACK;
      pfill(x + i * k, y + j * k, k, k, c);
    }
}

/* the LED counters: seven segments, lit ones red */
static const uint8_t segs[11] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F, 0x40};
/* the segment (a to g, 0 to 6) at (x, y) of an 11x21 digit, or -1 */
static int seg(int x, int y) {
  if ((unsigned)x > 10 || (unsigned)y > 20) return -1;
  int m = iabs(y - 10);
  if (y < 3 && x > y && x < 10 - y) return 0;
  if (y > 17 && x > 20 - y && x < y - 10) return 3;
  if (m < 2 && x > m && x < 10 - m) return 6;
  int d = x < 3 ? x : x > 7 ? 10 - x : -1;
  if (d < 0) return -1;
  if (y > d && y < 10 - d) return x < 3 ? 5 : 1;
  if (y > 10 + d && y < 20 - d) return x < 3 ? 4 : 2;
  return -1;
}
/* three digits (a minus sign below zero) in a sunken box 41x25 */
static void led(int x, int y, int v) {
  v = v > 999 ? 999 : v < -99 ? -99 : v;
  int d[3] = {v < 0 ? 10 : v / 100, iabs(v) / 10 % 10, iabs(v) % 10};
  edge(fill, x, y, 41, 25, 1, DARK, WHITE);
  pw = 13;
  for (int k = 0; k < 3; k++) {
    for (int j = 0; j < 23; j++)
      for (int i = 0; i < 13; i++) {
        int s = seg(i - 1, j - 1);
        pix[j * 13 + i] = s < 0 ? BLACK : segs[d[k]] >> s & 1 ? RED : RGB(0x600000);
      }
    push(x + 1 + 13 * k, y + 1, 23);
  }
}

/* ------------------------------------------------------------------ windows */
/* rows of 8 pixels, the leftmost the top bit */
static void bitmap(const uint8_t *rows, int n, int x, int y, color c) {
  for (int j = 0; j < n; j++)
    for (int i = 0; i < 8; i++)
      if (rows[j] << i & 0x80) fill(x + i, y + j, 1, 1, c);
}
/* the caption buttons' close, maximize and minimize, Windows' check mark, restore */
static const uint8_t glyph8[5][7] = {
  {0xC3, 0x66, 0x3C, 0x18, 0x3C, 0x66, 0xC3}, {0xFF, 0xFF, 0x81, 0x81, 0x81, 0x81, 0xFF},
  {0, 0, 0, 0, 0, 0xFC, 0xFC}, {0x02, 0x06, 0x8E, 0xDC, 0xF8, 0x70, 0x20},
  {0x3F, 0x3F, 0xFD, 0xFD, 0x87, 0x84, 0xFC},
};
/* a raised button with a label; the focused one has a black frame and a dotted rectangle */
static void button(int x, int y, int w, int h, const char *s, bool focus) {
  if (focus) edge(fill, x, y, w, h, 1, BLACK, BLACK), x++, y++, w -= 2, h -= 2;
  raised(x, y, w, h);
  fill(x + 2, y + 2, w - 4, h - 4, GRAY);
  centered(s, x + w / 2, y + (h - 14) / 2, BLACK, GRAY);
  if (focus) /* Windows' dotted focus rectangle */
    for (int i = 3; i < w - 3 || i < h - 3; i += 2) {
      if (i < w - 3) fill(x + i, y + 3, 1, 1, BLACK), fill(x + i, y + h - 4, 1, 1, BLACK);
      if (i < h - 3) fill(x + 3, y + i, 1, 1, BLACK), fill(x + w - 4, y + i, 1, 1, BLACK);
    }
}
/* the navy title bar: the app's icon (not on dialogs), the title and the buttons */
static void caption(int x, int y, int w, const char *s, int buttons) {
  fill(x, y, w, 18, NAVY);
  if (buttons > 1) {
    pw = 14;
    pfill(0, 0, 14, 14, GRAY);
    edge(pfill, 0, 0, 14, 14, 1, WHITE, DARK);
    mine(2, 2, 11);
    push(x + 2, y + 2, 14);
  }
  text(s, x + (buttons > 1 ? 20 : 4), y + 2, WHITE, NAVY);
  for (int b = 0; b < buttons; b++) {
    int bx = x + w - 18 - (b ? 2 + 16 * b : 0);
    /* Minesweeper's window can't be resized: maximize (restore when it fills the screen) is grayed out */
    const uint8_t *g = glyph8[b == 1 && !x ? 4 : b];
    raised(bx, y + 2, 16, 14);
    fill(bx + 2, y + 4, 12, 10, GRAY);
    if (b == 1) bitmap(g, 7, bx + 5, y + 6, WHITE);
    bitmap(g, 7, bx + 4, y + 5, b == 1 ? DARK : BLACK);
  }
}
/* a window: frame, title bar and a gray inside; returns where the inside starts */
static int wx0, wy0;
static void window(int x, int y, int w, int h, const char *s, int buttons) {
  edge(fill, x, y, w, h, 1, GRAY, BLACK);
  edge(fill, x + 1, y + 1, w - 2, h - 2, 1, WHITE, DARK);
  edge(fill, x + 2, y + 2, w - 4, h - 4, 2, GRAY, GRAY);
  caption(x + 4, y + 4, w - 8, s, buttons);
  wx0 = x + 4, wy0 = y + 22;
}
/* a dialog in the middle of the screen */
static void dialog(int w, int h, const char *s) {
  int x = (320 - w) / 2, y = (240 - h) / 2;
  window(x, y, w, h, s, 1);
  fill(wx0, wy0, w - 8, h - 26, GRAY);
}
/* a menu row: a check mark, a label and two notes, white on navy when selected */
static void row(int x, int y, int w, const char *s, const char *mid, const char *note, bool sel, bool check) {
  color bg = sel ? NAVY : GRAY, fg = sel ? WHITE : BLACK;
  fill(x, y, w, 17, bg);
  if (check) bitmap(glyph8[3], 7, x + 4, y + 5, fg);
  text(s, x + 18, y + 2, fg, bg);
  if (mid) text(mid, x + 124, y + 2, fg, bg);
  if (note) text(note, x + w - 6 - slen(note) * 7, y + 2, fg, bg);
}
/* the etched line between groups of a menu */
static void rule(int x, int y, int w) {
  fill(x, y, w, 5, GRAY);
  fill(x + 1, y + 2, w - 2, 1, DARK);
  fill(x + 1, y + 3, w - 2, 1, WHITE);
}

/* ------------------------------------------------------------------ the field */
#define MW 30
#define MH 16
#define MINE 0x10
#define DIRTY 0x80
enum { HID, OPEN, FLAG, ASK };
enum { G_NEW, G_PLAY, G_WON, G_LOST };
static uint8_t cell[MW * MH], kn[MW * MH], fr[MW * MH]; /* cell: count, mine, state << 5, dirty */
static uint16_t bfs[MW * MH], fifo[1024];
static unsigned qh, qt;
static int bw = 9, bh = 9, nm = 10, cur, gs, flags, left;
static uint32_t elapsed; /* ms */
static const uint8_t levels[3][3] = {{9, 9, 10}, {16, 16, 40}, {30, 16, 99}};

static int state(int i) { return cell[i] >> 5 & 3; }
static void set_state(int i, int s) { cell[i] = (uint8_t)((cell[i] & 0x9F) | s << 5); }
static int nbrs(int i, int *o) {
  int x = i % bw, y = i / bw, n = 0;
  for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++)
      if ((dx || dy) && (unsigned)(x + dx) < (unsigned)bw && (unsigned)(y + dy) < (unsigned)bh) o[n++] = i + dy * bw + dx;
  return n;
}
static void count(void) {
  for (int i = 0, o[8]; i < bw * bh; i++) {
    int n = 0;
    for (int k = nbrs(i, o); k--;) n += cell[o[k]] >> 4 & 1;
    cell[i] = (uint8_t)((cell[i] & 0xF0) | n);
  }
}
/* the level being played (3 when custom) */
static int level(void) {
  for (int d = 0; d < 3; d++)
    if (bw == levels[d][0] && bh == levels[d][1] && nm == levels[d][2]) return d;
  return 3;
}

/* the drawing queue: cells to redraw, with a mark (0xFFFF) where a frame ends */
static void dirty(int i) {
  if (cell[i] & DIRTY) return;
  cell[i] |= DIRTY;
  fifo[qt++ & 1023] = (uint16_t)i;
}
static void wave(void) {
  if (qt != qh && fifo[(qt - 1) & 1023] != 0xFFFF) fifo[qt++ & 1023] = 0xFFFF;
}

/* ------------------------------------------------------------------ no guessing */
/* A small solver plays the field from the first click with what a person can
 * see: a number whose mines are all flagged frees its other neighbors, one
 * with as many hidden neighbors as missing mines flags them all, and two
 * numbers close together are compared (the 1-2 patterns): if B needs as many
 * more mines than A as it has cells A does not touch, those cells are mines
 * and the cells only A touches are safe. When the solver gets stuck, a mine
 * where it stopped is moved away and it tries again. kn: 0 unknown, 1 open,
 * 2 mine; fr: open cells next to unknown ones. */
static int todo, found;
static void s_open(int i) {
  int n = 1, o[8];
  bfs[0] = (uint16_t)i, kn[i] = 1;
  while (n) {
    int j = bfs[--n];
    todo--;
    if (!(cell[j] & 15))
      for (int k = nbrs(j, o); k--;)
        if (!kn[o[k]]) kn[o[k]] = 1, bfs[n++] = (uint16_t)o[k];
  }
}
/* the unknown neighbors of i as bits of the 5x5 square at (x0, y0); *r: its mines not found yet */
static uint32_t unknown(int i, int x0, int y0, int *r) {
  int o[8], n = nbrs(i, o);
  uint32_t m = 0;
  *r = cell[i] & 15;
  while (n--) {
    int j = o[n];
    if (kn[j] == 2) --*r;
    else if (!kn[j]) m |= 1u << ((j / bw - y0) * 5 + j % bw - x0);
  }
  return m;
}
static int bits(uint32_t m) {
  int n = 0;
  for (; m; m &= m - 1) n++;
  return n;
}
/* the squares of m are all mines, or all safe */
static void settle(uint32_t m, int x0, int y0, bool mines) {
  for (int q = 0; m; q++, m >>= 1) {
    int j = (y0 + q / 5) * bw + x0 + q % 5;
    if ((m & 1) && !kn[j]) {
      if (mines) kn[j] = 2, found++;
      else s_open(j);
    }
  }
}
static bool solvable(int first) {
  int n = bw * bh;
  for (int i = 0; i < n; i++) kn[i] = 0;
  todo = n - nm, found = 0;
  s_open(first);
  for (bool more = true; more && todo;) {
    more = false;
    for (int i = 0; i < n; i++) {
      int r, x0 = i % bw - 2, y0 = i / bw - 2;
      fr[i] = 0;
      if (kn[i] != 1) continue;
      uint32_t m = unknown(i, x0, y0, &r);
      int u = bits(m);
      if (!u) kn[i] = 3; /* nothing more to learn from it */
      else if (!r || r == u) settle(m, x0, y0, r), more = true;
      else fr[i] = 1;
    }
    if (more) continue;
    for (int a = 0; a < n; a++) {
      if (!fr[a]) continue;
      int ax = a % bw, ay = a / bw, ra, rb;
      for (int oy = -2; oy <= 2; oy++)
        for (int ox = -2; ox <= 2; ox++) {
          int x = ax + ox, y = ay + oy;
          if ((!ox && !oy) || (unsigned)x >= (unsigned)bw || (unsigned)y >= (unsigned)bh || !fr[y * bw + x]) continue;
          int x0 = ax + (ox < 0 ? ox : 0) - 1, y0 = ay + (oy < 0 ? oy : 0) - 1;
          uint32_t ma = unknown(a, x0, y0, &ra), mb = unknown(y * bw + x, x0, y0, &rb), d = mb & ~ma, e = ma & ~mb;
          if (!ma || !mb || !(d | e) || rb - ra != bits(d)) continue;
          settle(d, x0, y0, true);
          settle(e, x0, y0, false);
          more = true;
        }
    }
    if (!more && found == nm) /* every mine found: the rest is safe */
      for (int i = 0; i < n; i++)
        if (!kn[i]) s_open(i);
  }
  return !todo;
}
/* mines go in anywhere but the first square opened, and (area) around it;
   no guessing needs the area */
static void deal(int first, bool sure, bool area) {
  int n = bw * bh, fx = first % bw, fy = first / bw, around = sure || area;
  uint32_t t0 = eadk_timing_millis();
  seed ^= t0 * 2654435761u;
  for (bool again = true;;) {
    if (again) {
      for (int i = 0; i < n; i++) cell[i] &= 0xE0;
      for (int k = 0; k < nm;) {
        int i = (int)(rnd() % (uint32_t)n), dx = i % bw - fx, dy = i / bw - fy;
        if (cell[i] & MINE || (around ? dx * dx <= 1 && dy * dy <= 1 : i == first)) continue;
        cell[i] |= MINE, k++;
      }
    }
    count();
    /* a second at most (dense custom fields), and Home still quits */
    if (!sure || solvable(first) || eadk_timing_millis() - t0 > 1000 || keys() & K_HOME) break;
    /* stuck: a mine next to what is open trades places with a safe square
       away from it (or deal again) */
    int f = -1, g = -1, nf = 0, ng = 0;
    for (int i = 0, o[8]; i < n; i++) {
      if (kn[i]) continue;
      bool front = false;
      for (int k = nbrs(i, o); k--;) front |= kn[o[k]] & 1;
      if (front && (cell[i] & MINE) && !(rnd() % (uint32_t)++nf)) f = i;
      if (!front && !(cell[i] & MINE) && !(rnd() % (uint32_t)++ng)) g = i;
    }
    again = f < 0 || g < 0;
    if (!again) cell[f] &= ~MINE, cell[g] |= MINE;
  }
  left = n - nm;
}

/* ------------------------------------------------------------------ the game */
static struct {
  uint8_t magic, version, diff, opts;  /* diff: 0-2, 3 custom; opts: O_ bits */
  uint32_t elapsed;                    /* the game in progress (ms) */
  uint16_t best[3], played[3], won[3]; /* best: tenths of a second, 0 for none */
  uint16_t cmines, gmines;             /* custom mines; the game's mines */
  uint8_t cw, ch, gw, gh, gx, gy;      /* custom size; the game's size (gw 0: none) and cursor */
  uint8_t board[MW * MH / 2];          /* the game, a nibble a cell: mine | state << 1 */
} V;
#define SAVE_NAME "mines.sav"
#define O_MARKS 1 /* right-clicks go flag, ?, hidden */
#define O_SURE 2  /* no guessing */
#define O_BARE 4  /* the first square is only safe, like Windows: no area around it */
#define O_EDGE 8  /* the cursor stops at the edges */

static void save(void) {
  V.magic = 'M', V.version = 1;
  if (gs != G_PLAY) {
    if (gs) V.gw = 0; /* over; a new field keeps the saved game until its first square */
  } else {
    V.gw = (uint8_t)bw, V.gh = (uint8_t)bh, V.gmines = (uint16_t)nm;
    V.gx = (uint8_t)(cur % bw), V.gy = (uint8_t)(cur / bw), V.elapsed = elapsed > 999000 ? 999000 : elapsed;
    for (int i = 0; i < MW * MH; i++) {
      int v = i < bw * bh ? (cell[i] >> 4 & 1) | state(i) << 1 : 0;
      V.board[i / 2] = (uint8_t)(i & 1 ? V.board[i / 2] | v << 4 : v);
    }
  }
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  if (d && n == sizeof V) {
    uint32_t i = 0;
    while (i < n && d[i] == ((uint8_t *)&V)[i]) i++;
    if (i == n) return; /* nothing new */
  }
  ef_write(SAVE_NAME, &V, sizeof V);
}
/* unpacks the saved game into the field; false if it makes no sense */
static bool unpack(void) {
  int mines = 0;
  bw = V.gw, bh = V.gh, nm = V.gmines, flags = 0, left = bw * bh - nm;
  if (bw < 8 || bw > MW || bh < 8 || bh > MH || V.gx >= bw || V.gy >= bh || V.elapsed > 999000u) return false;
  for (int i = 0; i < bw * bh; i++) {
    int v = V.board[i / 2] >> (i & 1) * 4, st = v >> 1 & 3;
    cell[i] = (uint8_t)((v & 1) << 4 | st << 5);
    mines += v & 1, flags += st == FLAG, left -= st == OPEN;
    if ((v & 1) && st == OPEN) return false;
  }
  count();
  cur = V.gy * bw + V.gx, elapsed = V.elapsed;
  return mines == nm && nm <= (bw - 1) * (bh - 1) && left > 0 && left < bw * bh - nm;
}
static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  bool ok = d && n == sizeof V && d[0] == 'M' && d[1] == 1;
  for (uint32_t i = 0; i < sizeof V; i++) ((uint8_t *)&V)[i] = ok ? d[i] : 0;
  if (V.diff > 3) V.diff = 0;
  V.opts &= 15;
  if (V.cw < 8 || V.cw > MW || V.ch < 8 || V.ch > MH) V.cw = 16, V.ch = 12;
  if (!V.cmines || V.cmines > (V.cw - 1) * (V.ch - 1)) V.cmines = (uint16_t)(V.cw * V.ch / 6);
  for (int k = 0; k < 3; k++) {
    if (V.best[k] > 9999) V.best[k] = 0;
    if (V.won[k] > V.played[k]) V.won[k] = V.played[k] = 0;
  }
}

/* where things are on the screen */
#define HH 33 /* the counters' panel */
static int cs, bx, by, hx, hy, hw, wx, wy, ww, wh;
static bool framed;
static void layout(void) {
  /* cells as big as fit: maximized, or in a window on the desktop (at most 20 pixels, as bh >= 8) */
  int m = imin(308 / bw, (218 - HH - 9) / bh), f = imin(292 / bw, (240 - 26 - 25 - HH) / bh);
  framed = f >= 16 || f == m;
  cs = framed ? f : m;
  int cw = bw * cs + 20 < 150 ? 150 : bw * cs + 20, ch = bh * cs + 25 + HH;
  if (framed) {
    ww = cw + 8, wh = ch + 26, wx = (320 - ww) / 2, wy = (240 - wh) / 2;
    hy = wy + 29, by = hy + HH + 8;
  } else { /* maximized: the rows to spare go above, between and below */
    int e = 218 - HH - 6 - bh * cs;
    wx = wy = 0, ww = 320, wh = 240;
    hy = 20 + e / 3, by = hy + HH + 3 + (e + 1) / 3;
  }
  hx = wx + (ww - cw) / 2 + 7, hw = cw - 14;
  bx = (320 - bw * cs) / 2;
}

/* ------------------------------------------------------------------ drawing the game */
static int shown[3] = {-1000, -1000, -1}; /* on the counters and the face */
static bool cur_on = true; /* no cursor after a game (like Windows) until an arrow is pressed */
static void draw_cell(int i) {
  int s = cs, n = s - 1, st = state(i), m = cell[i] & MINE, q = s < 14 ? 2 : s < 19 ? 3 : 4;
  /* open, or shown after a loss: the mines missed and the wrong flags */
  bool seen = st == OPEN || (gs == G_LOST && (m ? st != FLAG : st == FLAG));
  bool flat = seen || (st == HID && kn[i] == 4); /* pressed (see press) */
  cell[i] &= ~DIRTY;
  pw = s;
  pfill(0, 0, s, s, st == OPEN && m ? RED : GRAY);
  if (flat) {
    pfill(0, 0, s, 1, DARK);
    pfill(0, 0, 1, s, DARK);
  } else {
    edge(pfill, 0, 0, s, s, s < 15 ? 1 : 2, WHITE, DARK);
  }
  if (i == cur && cur_on) { /* under the number or flag, so small cells stay readable */
    edge(pfill, 0, 0, s, s, 1, BLACK, BLACK);
    edge(pfill, 1, 1, s - 2, s - 2, 1, YELLOW, YELLOW);
  }
  if (seen) {
    if (m || st == FLAG) {
      int k = s < 14 ? 9 : (s - 3) | 1;
      mine(1 + (n - k) / 2, 1 + (n - k) / 2, k);
      if (st == FLAG) /* a flag where there was no mine */
        for (int j = 2; j < n - 1; j++) pfill(j, j, 2, 1, RED), pfill(n - j, j, 2, 1, RED);
    } else if (cell[i] & 15) {
      glyph((cell[i] & 15) - 1, 1, 1, n, q);
    }
  } else if (!flat) {
    if (st == FLAG) flag(s);
    else if (st == ASK) glyph(8, 0, 0, s, q);
  }
  push(bx + i % bw * s, by + i / bw * s, s);
}
/* the counters' panel: two LEDs and the face (drawn by update) */
static void panel(void) {
  edge(fill, hx, hy, hw, HH, 2, DARK, WHITE);
  int ix = hx + 2, iw = hw - 4, y = hy + 4, l = ix + 5, f = hx + hw / 2 - 12, r = ix + iw - 46;
  fill(ix, hy + 2, iw, 2, GRAY);
  fill(ix, hy + 29, iw, 2, GRAY);
  fill(ix, y, 5, 25, GRAY);
  fill(l + 41, y, f - l - 41, 25, GRAY);
  fill(f + 25, y, r - f - 25, 25, GRAY);
  fill(r + 41, y, 5, 25, GRAY);
  shown[0] = shown[1] = -1000, shown[2] = -1;
}
/* puts new numbers on the LEDs and the face */
static void update(int l, int r, int f) {
  if (l != shown[0]) led(hx + 7, hy + 4, shown[0] = l);
  if (r != shown[1]) led(hx + hw - 48, hy + 4, shown[1] = r);
  if (f != shown[2]) {
    int p = f >> 2; /* pressed */
    shown[2] = f;
    pw = 25;
    pfill(0, 0, 25, 25, GRAY);
    edge(pfill, 0, 0, 25, 25, 1, DARK, DARK);
    if (!p) edge(pfill, 1, 1, 23, 23, 2, WHITE, DARK);
    face(4 + p, 4 + p, 1, f & 3);
    push(hx + hw / 2 - 12, hy + 4, 25);
  }
}
static void draw_game(void) {
  if (framed) {
    around(0, 0, 320, 240, wx, wy, ww, wh, TEAL);
    window(wx, wy, ww, wh, T("Minesweeper"), 3);
  } else {
    caption(0, 0, 320, T("Minesweeper"), 3);
  }
  int cx = framed ? wx + 4 : 0, cy = framed ? wy + 22 : 18, cw = framed ? ww - 8 : 320, ch = framed ? wh - 26 : 222;
  int split = hy + HH, fw = bw * cs + 6, fh = bh * cs + 6;
  edge(fill, cx, cy, cw, ch, 2, WHITE, DARK);
  around(cx + 2, cy + 2, cw - 4, split - cy - 2, hx, hy, hw, HH, GRAY);
  around(cx + 2, split, cw - 4, cy + ch - 2 - split, bx - 3, by - 3, fw, fh, GRAY);
  panel();
  edge(fill, bx - 3, by - 3, fw, fh, 3, DARK, WHITE);
  /* everything is drawn now: what the queue held (maybe for a bigger field) is done */
  qh = qt;
  for (int i = 0; i < MW * MH; i++)
    if (i < bw * bh) draw_cell(i);
    else cell[i] &= ~DIRTY;
}
static void game_counters(int f) {
  /* a win shows whole seconds, like the time in tenths it gets */
  update(nm - flags, gs == G_NEW ? 0 : (int)(elapsed / 1000) + (gs != G_WON), gs >= G_WON ? gs : f); /* F_COOL, F_DEAD */
}

/* ------------------------------------------------------------------ playing */
static int32_t pressed_until, win_at; /* win_at: when the game that ended takes OK again (a win shows its box) */
static uint32_t last; /* the last frame's time */
static void new_game(void) {
  gs = G_NEW, elapsed = 0, flags = 0, win_at = 0, cur_on = true;
  for (int i = 0; i < bw * bh; i++) cell[i] &= DIRTY;
  /* a quick wipe from the top left corner */
  for (int d = 0; d < bw + bh - 1; d++) {
    for (int x = 0; x < bw; x++)
      if ((unsigned)(d - x) < (unsigned)bh) dirty((d - x) * bw + x);
    if (d & 1) wave();
  }
  wave();
}
/* opens bfs[0..n) and the empty areas they touch, a ring per frame */
static void open_cells(int n) {
  for (int k = 0; k < n; k++) set_state(bfs[k], OPEN), dirty(bfs[k]);
  wave();
  for (int h = 0, end = n, o[8]; h < n;) {
    int i = bfs[h++];
    left--;
    if (!(cell[i] & 15))
      for (int k = nbrs(i, o); k--;) {
        int j = o[k];
        if (state(j) != OPEN && state(j) != FLAG) set_state(j, OPEN), dirty(j), bfs[n++] = (uint16_t)j;
      }
    if (h == end) wave(), end = n;
  }
}
/* after the game: the mines (or flags) show up ring by ring around c */
static void ripple(int c) {
  for (int d = 0; d < MW; d++) {
    for (int i = 0; i < bw * bh; i++) {
      int dx = iabs(i % bw - c % bw), dy = iabs(i / bw - c / bw);
      if ((dx > dy ? dx : dy) == d && (cell[i] & MINE || state(i) == FLAG)) dirty(i);
    }
    wave();
  }
}
static int last_time, new_best;
static void ended(int c) {
  cur_on = false, dirty(cur);
  ripple(c);
  win_at = (int32_t)now + 700;
  save();
}
static void won(void) {
  int lv = level(), t = (int)(elapsed / 100);
  gs = G_WON, flags = nm;
  for (int i = 0; i < bw * bh; i++)
    if (cell[i] & MINE) set_state(i, FLAG);
  last_time = t > 9999 ? 9999 : t, new_best = 0;
  if (lv < 3) {
    V.won[lv]++;
    if (!V.best[lv] || last_time < V.best[lv]) V.best[lv] = (uint16_t)(last_time ? last_time : 1), new_best = 1;
  }
  ended(cur);
}
static void lost(int i) {
  gs = G_LOST;
  set_state(i, OPEN);
  ended(i);
}
static void play(int i) {
  int st = state(i), o[8], n = 0, boom = -1;
  if (st == FLAG) return;
  if (gs == G_NEW) {
    update(shown[0], shown[1], F_OH); /* dealing a no-guessing field can take a moment */
    deal(i, V.opts & O_SURE, !(V.opts & O_BARE));
    gs = G_PLAY, last = (uint32_t)eadk_timing_millis(), V.gw = 0; /* this game replaces the saved one */
    int lv = level();
    if (lv < 3) V.played[lv]++;
  }
  if (st == OPEN) { /* a number with all its flags: open the rest around it */
    int f = 0, k = nbrs(i, o);
    for (int j = 0; j < k; j++) f += state(o[j]) == FLAG;
    if (!(cell[i] & 15) || f != (cell[i] & 15)) return;
    for (int j = 0; j < k; j++)
      if (state(o[j]) == HID || state(o[j]) == ASK) {
        if (cell[o[j]] & MINE) boom = o[j];
        else bfs[n++] = (uint16_t)o[j];
      }
  } else if (cell[i] & MINE) {
    boom = i;
  } else {
    bfs[n++] = (uint16_t)i;
  }
  if (n) open_cells(n);
  if (boom >= 0) lost(boom);
  else if (!left) won();
}
static void mark(int i) {
  int st = state(i), to = st == HID ? FLAG : st == FLAG && (V.opts & O_MARKS) ? ASK : HID;
  if (st == OPEN) return;
  flags += (to == FLAG) - (st == FLAG);
  set_state(i, to);
  dirty(i);
}
/* OK held on a number shows its hidden neighbors pressed, like Windows' two
   buttons; kn (the solver's, free while playing) marks them with a 4 */
static int held_on = -1;
static void press(int c, int on) {
  int o[8];
  for (int j = nbrs(c, o); j--;) kn[o[j]] = (uint8_t)on, dirty(o[j]);
}
static void move_to(int i) {
  int was = cur;
  cur = i, cur_on = true;
  if (was != i) draw_cell(was);
  draw_cell(i);
}

/* ------------------------------------------------------------------ screens */
enum { S_TITLE, S_GAME };
enum { D_NONE, D_PAUSE, D_QUIT, D_WIN, D_HELP, D_OPTIONS, D_CUSTOM };
static int scr, dlg, sel, stack[3][2], depth; /* the dialogs under the open one */
static const char *const names[] = {T("Continue"), T("Beginner"), T("Intermediate"), T("Expert"), T("Custom..."), T("Options..."),
                                    T("Help")};
static int first, nitems; /* the title's rows are names[first..6]: Continue only with a game */

/* the title: a window on the desktop, its menu under the counters */
#define TX 16
#define TY 4
#define TW 288
/* rows of 17 pixels, with a rule after Continue and before Options */
static int title_y(int k) { return TY + 67 + k * 17 + (!first && k > 0 ? 5 : 0) + (k >= nitems - 2 ? 5 : 0); }
static void size_note(char *o, int w, int h) {
  o = num(o, w);
  o = cat(o, " x ");
  num(o, h);
}
/* the size of each field, then the time played, the best time or the mines */
static void title_row(int k) {
  int id = k + first, w = V.cw, h = V.ch;
  char size[12] = "", note[12] = "";
  if (id == 0) w = V.gw, h = V.gh, cat(num(note, (int)(elapsed / 1000) + 1), T(" s"));
  else if (id < 4) w = levels[id - 1][0], h = levels[id - 1][1], V.best[id - 1] && secs(note, V.best[id - 1]);
  else if (id == 4) cat(num(note, V.cmines), T(" mines"));
  if (id < 5) size_note(size, w, h);
  row(TX + 8, title_y(k), TW - 16, names[id], size, note, k == sel, id && id - 1 == V.diff && id < 5);
}
/* what the counters show for the selected line: mines and best time, or the saved game */
static void title_counters(int f) {
  int id = sel + first, l, r = 0;
  if (id > 4) id = V.gw ? 0 : V.diff + 1; /* Options and Help: the game or level at hand */
  if (!id) l = nm - flags, r = (int)(elapsed / 1000) + 1;
  else if (id < 4) l = levels[id - 1][2], r = (V.best[id - 1] + 9) / 10;
  else l = V.cmines;
  update(l, r, f);
}
static void draw_title(void) {
  first = !V.gw, nitems = 7 - first;
  int th = title_y(nitems) + 10 - TY;
  around(0, 0, 320, 216, TX, TY, TW, th, TEAL);
  window(TX, TY, TW, th, T("Minesweeper"), 3);
  hx = TX + 12, hy = TY + 28, hw = TW - 24;
  around(wx0, wy0, TW - 8, th - 26, hx, hy, hw, HH, GRAY);
  panel();
  for (int k = 0; k < nitems; k++) {
    title_row(k);
    if (k == nitems - 3 || (k == 0 && !first)) rule(TX + 8, title_y(k) + 17, TW - 16);
  }
  /* the taskbar */
  fill(0, 216, 320, 1, GRAY);
  fill(0, 217, 320, 1, WHITE);
  fill(0, 218, 320, 22, GRAY);
  int sw = NP_TEXT_EXTRA ? 26 + slen(T("Start")) * 7 : 56; /* (the Start button fits its label) */
  if (sw < 56) sw = 56;
  raised(2, 220, sw, 18);
  fill(4, 222, sw - 4, 14, GRAY);
  static const color logo[4] = {RGB(0xFF0000), RGB(0x00A000), RGB(0x0000FF), RGB(0xFFD000)};
  for (int q = 0; q < 4; q++) fill(7 + (q & 1) * 6, 223 + (q >> 1) * 6, 5, 5, logo[q]);
  text(T("Start"), 21, 222, BLACK, GRAY);
  int tx = sw + 6; /* the window's button, after it */
  edge(fill, tx, 220, 192 - tx, 18, 1, BLACK, WHITE);
  edge(fill, tx + 1, 221, 190 - tx, 16, 1, DARK, GRAY);
  fill(tx + 2, 222, 188 - tx, 14, RGB(0xD8D8D8));
  text(T("Minesweeper"), tx + 8, 222, BLACK, RGB(0xD8D8D8));
  title_counters(F_SMILE);
}

/* the dialogs; after a key only what changes is redrawn (whole is false) */
static bool whole;
static void draw_pause(void) {
  static const char *const p[4] = {T("Resume"), T("New game"), T("Help"), T("Quit game")};
  int x = wx + (framed ? 4 : 0) + 2, y = wy + (framed ? 22 : 18), w = 118;
  for (int k = 0; NP_TEXT_EXTRA && k < 4; k++) /* (wider for longer words) */
    if (slen(p[k]) * 7 + 30 > w) w = slen(p[k]) * 7 + 30;
  raised(x, y, w, 4 * 17 + 5 + 6);
  edge(fill, x + 2, y + 2, w - 4, 4 * 17 + 5 + 2, 1, GRAY, GRAY);
  for (int k = 0; k < 4; k++) row(x + 3, y + 3 + k * 17 + (k > 2) * 5, w - 6, p[k], 0, 0, k == sel, false);
  rule(x + 3, y + 3 + 3 * 17, w - 6);
}
static void draw_ask(const char *q) {
  if (whole) {
    dialog(200, 100, T("Minesweeper"));
    pw = 26;
    pfill(0, 0, 26, 26, GRAY);
    for (int j = 0; j < 26; j++)
      for (int i = 0; i < 26; i++) {
        int d = (i - 13) * (i - 13) + (j - 12) * (j - 12);
        if (d < 150) pix[j * 26 + i] = d < 110 ? WHITE : BLACK;
      }
    push(wx0 + 12, wy0 + 10, 26);
    text("?", wx0 + 22, wy0 + 15, RGB(0x0000FF), WHITE);
    text(q, wx0 + 50, wy0 + 16, BLACK, GRAY);
  }
  button(wx0 + 34, wy0 + 46, 56, 22, T("Yes"), sel == 0);
  button(wx0 + 102, wy0 + 46, 56, 22, T("No"), sel == 1);
}
static void draw_win(void) {
  char s[40], *o;
  dialog(230, 118, T("Minesweeper"));
  pw = 34;
  pfill(0, 0, 34, 34, GRAY);
  face(0, 0, 2, F_COOL);
  push(wx0 + 10, wy0 + 12, 34);
  text(new_best ? T("New best time!") : T("You win!"), wx0 + 56, wy0 + 8, BLACK, GRAY);
  o = cat(s, T("Time "));
  secs(o, last_time);
  text(s, wx0 + 56, wy0 + 26, BLACK, GRAY);
  int lv = level();
  if (lv < 3) {
    o = cat(s, T("Won "));
    o = num(o, V.won[lv]);
    o = cat(o, T(" of "));
    num(o, V.played[lv]);
    text(s, wx0 + 56, wy0 + 44, BLACK, GRAY);
  }
  button(wx0 + 77, wy0 + 66, 68, 22, "OK", true);
}
static void draw_help(void) {
  static const char *const lines[12] = {
    T("Open every square without a mine."), T("A number counts the mines around it."),
    T("Arrows, 8 4 6 2"),  T("Move (wraps around)"),
    T("OK, EXE, 5"),       T("Open"),
    T("OK on a number"),   T("Open all around it"),
    T("Backspace, Shift, 0"), T("Flag"),
    T("Back"),             T("Pause"),
  };
  dialog(304, 224, T("Help"));
  text(lines[0], wx0 + 8, wy0 + 6, BLACK, GRAY);
  text(lines[1], wx0 + 8, wy0 + 22, BLACK, GRAY);
  for (int k = 1; k < 6; k++) {
    text(lines[2 * k], wx0 + 8, wy0 + 28 + k * 18, BLACK, GRAY);
    text(lines[2 * k + 1], wx0 + 150, wy0 + 28 + k * 18, NAVY, GRAY);
  }
  text(T("Original by Robert Donner & Curt Johnson"), wx0 + 8, wy0 + 142, BLACK, GRAY);
  button(wx0 + 114, wy0 + 170, 68, 22, T("OK"), true);
}
static const char *const OPTS[4] = {T("Marks (?)"), T("No guessing"), T("Open an area first"), T("Cursor wraps around")};
static const char *const HINTS[4][2] = {
  {T("Flag, then ?, then nothing:"), T("for squares you are unsure of.")},
  {T("Every field can be cleared"), T("by logic alone.")},
  {T("The first square opens an area."), T("Off: only safe, like Windows.")},
  {T("Off: the cursor stops at"), T("the edges of the field.")},
};
/* the options on, as the rows show them (the last two are kept inverted) */
static bool opt_on(int k) { return (V.opts >> k & 1) ^ (k >= 2); }
static void draw_options(void) {
  if (whole) dialog(252, 172, T("Options"));
  for (int k = 0; k < 4; k++) row(wx0 + 8, wy0 + 8 + k * 18, 228, OPTS[k], 0, 0, k == sel, opt_on(k));
  bool forced = sel == 2 && (V.opts & O_SURE);
  fill(wx0 + 8, wy0 + 88, 228, 32, GRAY);
  text(HINTS[sel][0], wx0 + 12, wy0 + 88, BLACK, GRAY);
  text(forced ? T("Always on with no guessing.") : HINTS[sel][1], wx0 + 12, wy0 + 104, forced ? NAVY : BLACK, GRAY);
  if (whole) text(T("OK: on/off     Back: done"), wx0 + 12, wy0 + 126, BLACK, GRAY);
}
static int cf[3]; /* the custom field: height, width, mines */
static void draw_custom(void) {
  static const char *const f[3] = {T("Height:"), T("Width:"), T("Mines:")};
  int bx = 66; /* the boxes, after the longest label */
  for (int k = 0; NP_TEXT_EXTRA && k < 3; k++)
    if (12 + slen(f[k]) * 7 + 4 > bx) bx = 12 + slen(f[k]) * 7 + 4;
  if (whole) {
    dialog(220, 136, T("Custom Field"));
    for (int k = 0; k < 3; k++) text(f[k], wx0 + 12, wy0 + 12 + k * 26, BLACK, GRAY);
    button(wx0 + 136, wy0 + 10, 64, 22, T("OK"), true);
    button(wx0 + 136, wy0 + 40, 64, 22, T("Cancel"), false);
    text(T("Arrows: change"), wx0 + 12, wy0 + 88, BLACK, GRAY);
  }
  for (int k = 0; k < 3; k++) {
    int x = wx0 + bx, y = wy0 + 9 + k * 26;
    char s[6];
    bool on = k == sel;
    sunken(x, y, 50, 20);
    fill(x + 2, y + 2, 46, 16, WHITE);
    num(s, cf[k]);
    fill(x + 4, y + 3, slen(s) * 7 + 2, 14, on ? NAVY : WHITE);
    text(s, x + 5, y + 3, on ? WHITE : BLACK, on ? NAVY : WHITE);
  }
}
static void draw_dialog(bool all) {
  whole = all;
  switch (dlg) {
    case D_PAUSE: draw_pause(); break;
    case D_QUIT: draw_ask(T("Quit game?")); break;
    case D_WIN: draw_win(); break;
    case D_HELP: draw_help(); break;
    case D_OPTIONS: draw_options(); break;
    case D_CUSTOM: draw_custom(); break;
  }
}
static void draw_all(void) {
  if (scr == S_GAME) layout(), draw_game(), game_counters(F_SMILE);
  else draw_title();
  draw_dialog(true);
}
static void open_dialog(int d, int s) {
  stack[depth][0] = dlg, stack[depth][1] = sel, depth++;
  dlg = d, sel = s;
  draw_dialog(true);
}
/* back to what was under the dialog */
static void close_dialog(void) {
  depth--;
  dlg = stack[depth][0], sel = stack[depth][1];
  draw_all();
}

/* a new game; the same field again is only wiped clean */
static void start(int d) {
  int w = bw, h = bh, again = scr == S_GAME && !dlg;
  V.diff = (uint8_t)d;
  if (d < 3) bw = levels[d][0], bh = levels[d][1], nm = levels[d][2];
  else bw = V.cw, bh = V.ch, nm = V.cmines;
  new_game();
  scr = S_GAME, dlg = D_NONE, depth = 0;
  save();
  if (again && w == bw && h == bh) return;
  cur = bh / 2 * bw + bw / 2;
  draw_all();
}
static bool leave; /* Quit game: back to NumPlay (or the calculator); main saves */
static void to_title(bool keep) {
  if (keep) save();
  /* the saved game, ready to continue */
  if (gs != G_PLAY && V.gw) {
    if (unpack()) gs = G_PLAY;
    else V.gw = 0;
  }
  scr = S_TITLE, dlg = D_NONE, depth = 0;
  sel = V.gw ? 0 : V.diff; /* Continue, or the level last played */
  draw_all();
}

int main(void) {
  np_app_begin();
  load();
  now = (uint32_t)eadk_timing_millis();
  seed ^= now;
  to_title(false);
  int held = 0, repeats = 0, arrows = K_UP | K_DOWN | K_LEFT | K_RIGHT, stuck = K_HOME, grip = -1;
  bool armed = false; /* once every key held from before is let go */
  uint32_t repeat_at = 0;
  last = now;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint32_t dt = now - last;
    last = now;
    int k = keys(), hit = armed ? k & ~held : 0, fresh = hit;
    stuck &= k; /* Home quits unless held since the start, whatever else is held */
    if (k & ~stuck & K_HOME) break;
    if (leave) break;
    if (!armed) armed = !k, k = 0;
    if (hit & arrows) repeat_at = now + 280, repeats = 0;
    else if ((k & arrows) && (int32_t)(now - repeat_at) >= 0) hit |= k & arrows, repeat_at = now + 55, repeats++;
    held = k;
    /* what the keys change is drawn right after the refresh, so it never tears */
    eadk_display_wait_for_vblank();
    bool ok = hit & K_OK, back = hit & K_BACK;
    int ud = !!(hit & K_DOWN) - !!(hit & K_UP), lr = !!(hit & K_RIGHT) - !!(hit & K_LEFT);
    int face_now = k & K_OK ? F_OH : F_SMILE;

    if (dlg == D_NONE && scr == S_TITLE) {
      if (ud) {
        int was = sel;
        sel = (sel + ud + nitems) % nitems;
        title_row(was), title_row(sel);
      }
      if (back) open_dialog(D_QUIT, 1); /* "Quit game?" from the title, on No */
      else if (ok) {
        int id = sel + first;
        if (id == 0) {
          V.diff = (uint8_t)level(), scr = S_GAME;
          draw_all();
        } else if (id < 4) {
          start(id - 1);
        } else if (id == 4) {
          cf[0] = V.ch, cf[1] = V.cw, cf[2] = V.cmines;
          open_dialog(D_CUSTOM, 0);
        } else {
          open_dialog(id == 5 ? D_OPTIONS : D_HELP, 0);
        }
      }
    } else if (dlg == D_NONE) {
      if (hit & arrows) {
        int x = cur % bw + lr, y = cur / bw + ud;
        /* a new press wraps around the edge, a held key stops there */
        if ((unsigned)x < (unsigned)bw && (unsigned)y < (unsigned)bh) move_to(y * bw + x);
        else if ((fresh & arrows) && !(V.opts & O_EDGE)) move_to((y + bh) % bh * bw + (x + bw) % bw);
      }
      if (ok) {
        if (gs < G_WON) grip = state(cur) == OPEN ? cur : -1, play(cur);
        else if (!win_at) pressed_until = (int32_t)now + 120, start(V.diff); /* like clicking the face */
      }
      if ((hit & K_FLAG) && gs < G_WON) mark(cur);
      if (back) open_dialog(D_PAUSE, 0);
      else if (win_at && (int32_t)(now - win_at) >= 0) {
        win_at = 0;
        if (gs == G_WON) open_dialog(D_WIN, 0);
      }
    } else if (dlg == D_PAUSE) {
      if (ud) sel = (sel + ud + 4) % 4, draw_dialog(false);
      if (back || (ok && sel == 0)) close_dialog();
      else if (ok && sel == 1) {
        if (gs) V.gw = 0; /* given up: no Continue for it */
        dlg = D_NONE, depth = 0, new_game(), draw_all();
      }
      else if (ok && sel == 2) open_dialog(D_HELP, 0);
      else if (ok) open_dialog(D_QUIT, 0);
    } else if (dlg == D_QUIT) {
      if (lr) sel = !sel, draw_dialog(false);
      if (back || (ok && sel)) close_dialog();
      else if (ok) leave = true;
    } else if (dlg == D_OPTIONS) {
      if (ud) sel = (sel + ud + 4) % 4;
      if (ok) V.opts ^= (uint8_t)(1 << sel);
      if (ud || ok) draw_dialog(false);
      if (back) save(), close_dialog();
    } else if (dlg == D_CUSTOM) {
      if (ud) sel = (sel + ud + 3) % 3;
      if (lr) { /* held, the mines go by fives */
        static const uint8_t hi[2] = {MH, MW};
        int v = cf[sel] + lr * (repeats > 12 && sel == 2 ? 5 : 1), top = sel < 2 ? hi[sel] : (cf[1] - 1) * (cf[0] - 1), lo = sel < 2 ? 8 : 1;
        cf[sel] = v < lo ? lo : v > top ? top : v;
        top = (cf[1] - 1) * (cf[0] - 1);
        if (cf[2] > top) cf[2] = top;
      }
      if (ud || lr) draw_dialog(false);
      if (ok) V.ch = (uint8_t)cf[0], V.cw = (uint8_t)cf[1], V.cmines = (uint16_t)cf[2], start(3);
      else if (back) close_dialog();
    } else { /* help, the win */
      if (ok || back) close_dialog();
    }

    if (scr == S_GAME && gs == G_PLAY && dlg == D_NONE) elapsed += dt;
    /* OK held on a number where it was pressed: its hidden neighbors go down */
    int h = scr == S_GAME && !dlg && gs == G_PLAY && (k & K_OK) && cur == grip ? cur : -1;
    if (h != held_on) {
      if (held_on >= 0) press(held_on, 0);
      if ((held_on = h) >= 0) press(h, 4);
    }
    if (dlg == D_NONE) {
      if (scr == S_GAME) {
        for (int drawn = 0; qh != qt;) {
          uint16_t i = fifo[qh++ & 1023];
          if (i == 0xFFFF) {
            if (drawn) break;
          } else if (cell[i] & DIRTY) {
            draw_cell(i), drawn++;
          }
        }
        game_counters((int32_t)(now - pressed_until) < 0 ? F_SMILE | 4 : face_now);
      } else {
        title_counters(face_now);
      }
    }
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
  save();
  return np_app_end();
}
