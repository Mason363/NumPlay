/* Block Breaker: Google's brick breaker (the game in Search) for the NumWorks
 * calculator. The dark pixel-art field, seven bricks across in Google's blue,
 * red, yellow and green, bricks that hold a power-up (TNT, an extra ball,
 * three balls, a wider paddle, a fireball, a laser), the big white ball and
 * paddle, the lives as circles and the score in segments; wall after wall,
 * each a little faster.
 *
 * Nothing is drawn straight to the screen: whatever moves or changes marks
 * the rectangle it covers, and each marked rectangle is composed in a small
 * buffer (background, bricks, paddle, balls, sparks, text) and pushed once.
 * A frame costs a few thousand pixels and nothing flickers. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_text.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Block Breaker";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

#define W 320
#define H 240
typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
#define BG RGB(0x1C181F)   /* Google's night */
#define GREY RGB(0x8E8A92) /* the score */
#define DIM RGB(0x555257)
#define PALE RGB(0xC9C6CC)
#define KEY(k) (1ull << (k))

/* the playfield: seven bricks of 30 x 16 across, 2 pixels apart */
#define TOP 24  /* the ball bounces here, under the score */
#define COLS 7
#define ROWS 7
#define BW 30
#define BH 16
#define GAP 2
#define BX0 49  /* the first brick */
#define BY0 31
#define PY 222  /* the top of the paddle */
#define PH 6
#define BS 12   /* the ball's size */
#define ONE 256 /* positions and speeds are in 256ths of a pixel */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int v) { return v < 0 ? -v : v; }

static uint32_t seed = 0x2545F491;
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static int rndi(int n) { return (int)(rnd() % (uint32_t)n); }

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

/* ------------------------------------------------------------------ bricks */
/* Google's four colors: the face, the rim, and the icon of a power-up */
static const color BRICK[4][3] = {
  {RGB(0x5784E6), RGB(0x4B70C3), RGB(0x375491)}, /* blue */
  {RGB(0xCA423E), RGB(0xAB3934), RGB(0x7E2622)}, /* red */
  {RGB(0xECC444), RGB(0xC7A63A), RGB(0x8E7420)}, /* yellow */
  {RGB(0x3B854B), RGB(0x326F3F), RGB(0x1F4A2A)}, /* green */
};
enum { U_NONE, U_TNT, U_LIFE, U_MULTI, U_WIDE, U_FIRE, U_LASER, NU };

/* The walls, 7 x 7 at most: '.' nothing, 'b' 'r' 'y' 'g' a brick of that
   color, '#' a brick in the row's color (blue, red, yellow, green, then
   again), and a power-up in the row's color: T TNT, + a ball, o three balls,
   w a wider paddle, f a fireball, l a laser. The first is Google's. */
static const char *const WALLS[][ROWS] = {
  {"#T####+", "##+##wT", "#####o#", "###o###"},
  {"..#w#..", ".##T##.", "###+###", "o#####o", ".#####.", "..###.."},
  {"#.#.#.#", "T#o#l#T", "#.#.#.#", ".#.#.#.", "#.#f#.#"},
  {"bbbbbbb", "r.....r", "r.yTy.r", "r.ywy.r", "r.....r", "ggg+ggg"},
  {"#######", "#T###T#", "..#o#..", "#######", "..#l#..", "#w###f#"},
  {"...#...", "..#T#..", ".#+#o#.", "#w#f#l#", ".#####.", "..###..", "...#..."},
  {"b.r.y.g", ".T.o.T.", "g.y.r.b", ".w.f.l.", "b.r.y.g", ".......", "#+###+#"},
  {"#######", "#.....#", "#.#T#.#", "#.#o#.#", "#.....#", "###w###"},
  {"TTT.TTT", "#######", "##f#l##", "#######", "+#####+"},
  {"#.#.#.#", "#.#.#.#", "#o#T#w#", "#.#.#.#", "#f#.#l#", "#######"},
  {"yyyTyyy", "gggwggg", "bbbobbb", "rrrfrrr", "yyylyyy", "ggg+ggg", "bbbbbbb"},
  {"T#####T", "#o#l#f#", "#######", "#w#+#w#", "#######", "T#####T"},
};
#define NWALLS (int)(sizeof WALLS / sizeof WALLS[0])

/* a brick: its color + 1 (3 bits), its power-up (3 bits); 0 none */
static uint8_t brick[ROWS][COLS];
static uint8_t flash[ROWS][COLS]; /* white for a moment when it breaks next door (TNT) */

/* ------------------------------------------------------------------ the game */
enum { S_TITLE, S_SET, S_READY, S_PLAY, S_LOST, S_CLEAR, S_PAUSE, S_QUIT, S_OVER };

typedef struct {
  int32_t x, y, vx, vy;
  int16_t off; /* where it sits on the paddle before the serve, in pixels */
  int16_t tx[4], ty[4]; /* the trail of a fireball */
  uint8_t on, stuck;
} ball_t;
typedef struct {
  int16_t x, y; /* in 16ths of a pixel */
  int8_t vx, vy;
  color c;
  uint8_t life, max, size;
} part_t;
typedef struct {
  int16_t x, y;
  uint8_t on;
} shot_t;

#define NBALL 5
#define NPART 64
#define NSHOT 6
static ball_t ball[NBALL];
static part_t part[NPART];
static shot_t shot[NSHOT];
static int state, resume_state, st_t, tk, sel, tsel, level, bricks_left, start_lv = 1;
static int32_t score;
static int lives, laser_cd, wide_t, fire_t, laser_t;
static bool demo, fresh_best, full, hinted;
static int px, pv, spd, spd0; /* the paddle's centre and speed, the balls' speed (in 256ths) */
static int pw, pw_to;         /* the paddle's width, and the width it is going to */
static int demo_aim;          /* where the self-playing paddle aims */
static int shake_t, shake_a, sx, sy; /* the shake: time left, strength, and the bricks' offset */

/* ------------------------------------------------------------------ save */
static struct {
  uint8_t magic, version, reached, speed, shake, pad[3];
  uint32_t best;
} sv, saved;
#define SAVE_NAME "breakout.sav"

static void save(void) {
  const uint8_t *a = (const uint8_t *)&sv, *b = (const uint8_t *)&saved;
  unsigned i = 0;
  while (i < sizeof sv && a[i] == b[i]) i++;
  if (i < sizeof sv && ef_write(SAVE_NAME, &sv, sizeof sv)) saved = sv;
}
static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  sv.magic = 'B', sv.version = 2, sv.reached = 1, sv.speed = 1, sv.shake = 1;
  if (d && n == sizeof sv && d[0] == 'B' && d[1] == 2 && d[2] >= 1 && d[2] <= 99 && d[3] < 3 && d[4] < 2) {
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&sv)[i] = d[i];
    if (sv.best > 99999) sv.best = 0;
    saved = sv;
  }
}

/* ------------------------------------------------------------------ dirty rectangles */
typedef struct {
  int16_t x0, y0, x1, y1;
} rect_t;
#define NDIRTY 40
static rect_t dirty[NDIRTY];
static int ndirty;

/* marks an area to redraw, merged with another when that wastes little
   (overlapping areas are simply drawn twice) */
static void mark(int x0, int y0, int x1, int y1) {
  x0 = imax(x0, 0), y0 = imax(y0, 0), x1 = imin(x1, W), y1 = imin(y1, H);
  if (x0 >= x1 || y0 >= y1) return;
  for (int i = 0; i < ndirty;) {
    rect_t *d = &dirty[i];
    int u0 = imin(x0, d->x0), v0 = imin(y0, d->y0), u1 = imax(x1, d->x1), v1 = imax(y1, d->y1);
    if ((u1 - u0) * (v1 - v0) <= (x1 - x0) * (y1 - y0) + (d->x1 - d->x0) * (d->y1 - d->y0) + 256) {
      x0 = u0, y0 = v0, x1 = u1, y1 = v1;
      *d = dirty[--ndirty];
      i = 0;
    } else {
      i++;
    }
  }
  if (ndirty == NDIRTY) { /* too many: the last one takes this one in */
    rect_t *d = &dirty[NDIRTY - 1];
    x0 = imin(x0, d->x0), y0 = imin(y0, d->y0), x1 = imax(x1, d->x1), y1 = imax(y1, d->y1);
    ndirty--;
  }
  dirty[ndirty++] = (rect_t){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
}
static void mark_at(int x, int y, int w, int h) { mark(x - 2, y - 2, x + w + 2, y + h + 2); }
static int brick_x(int c) { return BX0 + c * (BW + GAP); }
static int brick_y(int r) { return BY0 + r * (BH + GAP); }
static void mark_brick(int r, int c) { mark_at(brick_x(c) + sx, brick_y(r) + sy, BW, BH); } /* bricks shake */

/* ------------------------------------------------------------------ drawing */
#define BUFN (W * 16)
static color buf[BUFN];
static int rx0, ry0, rx1, ry1, rw; /* the area buf holds */

static void rect(int x, int y, int w, int h, color c, int a) {
  int x0 = imax(x, rx0), x1 = imin(x + w, rx1), y0 = imax(y, ry0), y1 = imin(y + h, ry1);
  for (int j = y0; j < y1; j++)
    for (color *p = buf + (j - ry0) * rw + x0 - rx0, *e = p + x1 - x0; p < e; p++) *p = a >= 32 ? c : mix(c, *p, a);
}
static __attribute__((noinline)) void fill(int x, int y, int w, int h, color c) { rect(x, y, w, h, c, 32); }
static bool seen(int x, int y, int w, int h) { return x < rx1 && x + w > rx0 && y < ry1 && y + h > ry0; }
/* a pixel-art circle, d pixels across */
static void round_(int x, int y, int d, color c) {
  for (int j = 0; j < d; j++) {
    int t = 2 * j + 1 - d, e = 0;
    while ((2 * e + 1 - d) * (2 * e + 1 - d) + t * t > d * d) e++;
    fill(x + e, y + j, d - 2 * e, 1, c);
  }
}
static void ring_(int x, int y, int d, color c) {
  for (int j = 0; j < d; j++) {
    int t = 2 * j + 1 - d, e = 0;
    while ((2 * e + 1 - d) * (2 * e + 1 - d) + t * t > d * d) e++;
    if (j == 0 || j == d - 1) fill(x + e, y + j, d - 2 * e, 1, c);
    else fill(x + e, y + j, 1, 1, c), fill(x + d - 1 - e, y + j, 1, 1, c);
  }
}

/* ASCII 32..90 (lowercase is drawn as capitals), columns of 7 bits, top bit first */
static const uint8_t font[59][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
  {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},
  {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
  {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
  {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
  {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
};
static int glyph_of(char ch) {
  if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
  return ch < 32 || ch > 'Z' ? '?' - 32 : ch - 32;
}
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
#if NP_TEXT_EXTRA
/* Other languages: an accented letter is the font's capital with np_accent() over it (a cedilla
   under it); a Chinese one comes from the 12-pixel font, 12 pixels tall up to size 2, then 24. */
static color pen_c;
static void pen_px(int x, int y, void *ctx) { fill(x, y, 1, 1, pen_c); }
static int xk(int k) { return (7 * k + 6) / 12; }
static void glyph(char ch, int x, int y, int k) {
  const uint8_t *g = font[glyph_of(ch)];
  for (int i = 0; i < 5; i++)
    for (int j = 0, b = g[i]; b; j++, b >>= 1)
      if (b & 1) fill(x + i * k, y + j * k, k, k, pen_c);
}
/* draws (or only measures) the next letter of *s at x; returns its advance */
static int letter(const char **s, int x, int y, int k, bool draw) {
  uint32_t cp = np_utf8(s);
  char b = (char)cp, b2 = 0;
  int acc = 0;
  if (cp >= 0x80 && (acc = np_latin(cp, &b, &b2), !b)) {
    if (draw) np_xdraw(cp, x, y + (7 * k - 12 * xk(k)) / 2, xk(k), pen_px, 0);
    return np_xadvance(cp, xk(k));
  }
  if (draw) {
    glyph(b, x, y, k);
    np_accent(acc, x, acc == NP_ACC_CEDIL ? y + 7 * k : y - 2 * k - 1, k, pen_px, 0);
    if (b2) glyph(b2, x + 6 * k, y, k);
  }
  return (b2 ? 12 : 6) * k;
}
static int tw(const char *s, int k) {
  int w = -k;
  while (*s) w += letter(&s, 0, 0, k, false);
  return w;
}
static void text(const char *s, int x, int y, int k, color c) {
  if (!seen(x, y - 3 * k - 3, tw(s, k) + k, 11 * k + 6)) return;
  pen_c = c;
  while (*s) x += letter(&s, x, y, k, true);
}
#else
static int tw(const char *s, int k) { return slen(s) * 6 * k - k; }
static void text(const char *s, int x, int y, int k, color c) {
  if (!seen(x, y, tw(s, k) + k, 8 * k)) return;
  for (int n = 0; s[n]; n++) {
    const uint8_t *g = font[glyph_of(s[n])];
    for (int i = 0; i < 5; i++)
      for (int j = 0, b = g[i]; b; j++, b >>= 1)
        if (b & 1) fill(x + n * 6 * k + i * k, y + j * k, k, k, c);
  }
}
#endif
static void ctext(const char *s, int y, int k, color c) { text(s, 160 - tw(s, k) / 2, y, k, c); } /* centred */
static char *cat(char *o, const char *t) {
  while ((*o = *t++)) o++;
  return o;
}
static char *itoa_(char *o, int32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

/* Google's score: figures of seven segments, 9 x 15 */
static const uint8_t SEG[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
static void segments(int d, int x, int y, color c) {
  int m = SEG[d];
  if (m & 1) fill(x + 2, y, 5, 2, c);
  if (m & 2) fill(x + 7, y + 2, 2, 4, c);
  if (m & 4) fill(x + 7, y + 9, 2, 4, c);
  if (m & 8) fill(x + 2, y + 13, 5, 2, c);
  if (m & 16) fill(x, y + 9, 2, 4, c);
  if (m & 32) fill(x, y + 2, 2, 4, c);
  if (m & 64) fill(x + 2, y + 6, 5, 3, c);
}
static void score_at(int32_t v, int x, int y, color c) {
  for (int i = 4; i >= 0; i--, v /= 10) segments((int)(v % 10), x + i * 12, y, c);
}

/* ------------------------------------------------------------------ the scene */
/* a power-up's icon on a brick, in the brick's darkest shade */
static void icon(int u, int x, int y, color c) {
  int cx = x + BW / 2, cy = y + BH / 2;
  switch (u) {
    case U_TNT: /* TNT in tiny letters */
      for (int i = 0; i < 3; i++) {
        int lx = cx - 8 + i * 6;
        if (i != 1) fill(lx, cy - 3, 5, 1, c), fill(lx + 2, cy - 2, 1, 5, c);
        else fill(lx, cy - 3, 1, 6, c), fill(lx + 4, cy - 3, 1, 6, c), fill(lx + 1, cy - 2, 1, 1, c), fill(lx + 2, cy - 1, 1, 1, c), fill(lx + 3, cy, 1, 1, c);
      }
      break;
    case U_LIFE: fill(cx - 1, cy - 4, 3, 9, c), fill(cx - 4, cy - 1, 9, 3, c); break;
    case U_MULTI: ring_(cx - 4, cy - 4, 9, c), ring_(cx - 3, cy - 3, 7, c); break;
    case U_WIDE: /* <-> with dots */
      fill(cx - 7, cy, 15, 1, c);
      for (int i = 1; i < 4; i++) fill(cx - 7 + i, cy - i, 1, 2 * i + 1, c), fill(cx + 7 - i, cy - i, 1, 2 * i + 1, c);
      fill(cx - 12, cy - 1, 2, 2, c), fill(cx + 11, cy - 1, 2, 2, c);
      break;
    case U_FIRE: /* a flame */
      for (int j = 0; j < 9; j++) {
        int w = j < 4 ? j / 2 : j < 7 ? 2 + (j - 4) / 2 : 3 - (j - 7);
        fill(cx - w, cy - 4 + j, 2 * w + 1, 1, c);
      }
      break;
    case U_LASER: /* two beams */
      fill(cx - 5, cy - 4, 2, 9, c), fill(cx + 4, cy - 4, 2, 9, c), fill(cx - 1, cy - 1, 3, 3, c);
      break;
  }
}
static void draw_brick(int r, int c) {
  int v = brick[r][c], x = brick_x(c) + sx, y = brick_y(r) + sy;
  if (!v || !seen(x, y, BW, BH)) return;
  const color *k = BRICK[(v & 7) - 1];
  color face = flash[r][c] ? mix(WHITE, k[0], flash[r][c] * 4) : k[0];
  fill(x + 1, y, BW - 2, BH, k[1]); /* the rim, corners cut */
  fill(x, y + 1, BW, BH - 2, k[1]);
  fill(x + 2, y + 2, BW - 4, BH - 4, face);
  if (v >> 3) icon(v >> 3, x, y, k[2]);
}
static void bricks(void) {
  for (int r = 0; r < ROWS; r++)
    if (seen(0, brick_y(r) + sy - 2, W, BH + 4))
      for (int c = 0; c < COLS; c++) draw_brick(r, c);
}

static bool paddle_shown(void) { return state != S_LOST && state != S_OVER; }
static void paddle(void) {
  if (!paddle_shown()) return;
  int x = ((px + ONE / 2) >> 8) - pw / 2, y = PY;
  if (!seen(x - 4, y - 4, pw + 8, PH + 4)) return;
  fill(x + 1, y, pw - 2, PH, WHITE);
  fill(x, y + 1, pw, PH - 2, WHITE);
  if (laser_t) { /* two cannons, red */
    fill(x + 3, y - 4, 4, 4, RGB(0xE8453C));
    fill(x + pw - 7, y - 4, 4, 4, RGB(0xE8453C));
  }
}
static color ball_color(void) { return fire_t ? RGB(0xFF8A3D) : WHITE; }
static void balls(void) {
  for (int i = 0; i < NBALL; i++) {
    ball_t *b = &ball[i];
    if (!b->on) continue;
    if (fire_t && !b->stuck)
      for (int t = 3; t >= 0; t--) { /* the fireball's trail */
        int s = BS - 2 - t * 2;
        round_(b->tx[t] + (BS - s) / 2, b->ty[t] + (BS - s) / 2, s, mix(RGB(0xFFD54F), BG, 20 - t * 4));
      }
    round_(b->x >> 8, b->y >> 8, BS, ball_color());
  }
}

static void things(void) {
  for (int i = 0; i < NSHOT; i++)
    if (shot[i].on) fill(shot[i].x, shot[i].y, 2, 8, RGB(0xFF6E5A)), fill(shot[i].x, shot[i].y, 2, 3, WHITE);
  for (int i = 0; i < NPART; i++) {
    part_t *p = &part[i];
    if (p->life) rect(p->x >> 4, p->y >> 4, p->size, p->size, p->c, 8 + 24 * p->life / p->max);
  }
}

/* the lives as circles, the score in segments, the wall's number */
static void hud(void) {
  if (ry0 >= TOP) return;
  int n = demo ? 3 : imax(lives, 0);
  for (int i = 0; i < imax(3, n); i++) {
    if (i < n) round_(8 + i * 15, 6, 11, PALE);
    else ring_(8 + i * 15, 6, 11, DIM);
  }
  score_at(demo ? (int32_t)sv.best : score, 130, 5, GREY);
  char s[NP_TEXT_EXTRA ? 16 : 8];
  itoa_(cat(s, T("LV ")), demo ? sv.reached : level + 1);
  text(s, 312 - tw(s, 1), 9, 1, DIM);
}

/* the floor: a darker band at the bottom */
static void background(void) {
  for (int y = ry0; y < ry1; y++) {
    color c = y < 228 ? BG : y < 232 ? RGB(0x18151B) : y < 235 ? RGB(0x151217) : y < 238 ? RGB(0x121014) : RGB(0x100E12);
    for (color *p = buf + (y - ry0) * rw, *e = p + rw; p < e; p++) *p = c;
  }
}

/* ------------------------------------------------------------------ overlays */
static const char *const speed_names[3] = {T("SLOW"), T("NORMAL"), T("FAST")};

/* where each screen's text is, so a change redraws only that */
static void ui_area(int s) {
  if (s <= S_SET) mark(0, 30, W, 232);
  else if (s < S_PAUSE) mark(40, 120, 280, 200);
  else full = true; /* a menu over the dimmed game */
}

/* a pixel-art panel: dark, with a white rim and corners cut */
static void panel(int x, int y, int w, int h) {
  fill(x + 2, y, w - 4, h, WHITE);
  fill(x, y + 2, w, h - 4, WHITE);
  fill(x + 1, y + 1, w - 2, h - 2, WHITE);
  fill(x + 2, y + 2, w - 4, h - 4, RGB(0x26222B));
}
static void item(const char *s, int cy, bool on) {
  if (on) fill(92, cy - 5, 136, 24, WHITE), fill(90, cy - 3, 140, 20, WHITE);
  ctext(s, cy, 2, on ? BG : PALE);
}
/* the keys, like Google's hint: left and right, and OK above */
static void keys_hint(int y) {
  panel(128, y, 64, 64);
  for (int i = 0; i < 3; i++) {
    int kx = 136 + i * 16, ky = y + (i == 1 ? 12 : 30);
    if (i == 1) fill(kx, ky - 1, 16, 16, WHITE), fill(kx + 2, ky + 1, 12, 12, RGB(0x26222B));
    else fill(kx, ky + 17, 16, 14, WHITE);
    if (i != 1) /* an arrow */
      for (int t = 0; t < 4; t++) fill(kx + (i ? 10 - t : 5 + t), ky + 24 - t, 1, 2 * t + 1, RGB(0x26222B));
  }
  fill(152, y + 47, 16, 14, WHITE), fill(154, y + 49, 12, 10, RGB(0x26222B));
}

static void logo(int y) {
  /* BLOCK BREAKER in bricks of Google's four colors */
#if NP_TEXT_EXTRA /* (another language's name: its letters, a space skips a color as there) */
  const char *name = T("BLOCK BREAKER");
  int w = -3, x, c = 0;
  for (const char *s = name; *s;) w += np_utf8(&s) >= 0x2E80 ? 38 : 18;
  x = 160 - w / 2;
  if (!seen(x - 2, y - 10, w + 4, 40)) return;
  for (const char *s = name; *s; c++) {
    uint32_t cp = np_utf8(&s);
    char b = (char)cp, b2;
    pen_c = BRICK[c & 3][0];
    if (cp >= 0x2E80) { /* a Chinese letter, its pixels 3 x 3 too */
      np_xdraw(cp, x, y - 8, 3, pen_px, 0);
      x += 38;
      continue;
    }
    if (cp >= 0x80) np_latin(cp, &b, &b2);
    if (b == ' ') c++;
    else glyph(b, x, y, 3);
    x += 18;
  }
#else
  static const char name[] = "BLOCK BREAKER";
  int x0 = 160 - 13 * 18 / 2 + 2;
  if (!seen(x0 - 2, y - 2, 13 * 18 + 4, 28)) return;
  for (int n = 0; n < 13; n++) {
    const uint8_t *g = font[glyph_of(name[n])];
    for (int i = 0; i < 5; i++)
      for (int j = 0; j < 7; j++)
        if (g[i] >> j & 1) fill(x0 + n * 18 + i * 3, y + j * 3, 3, 3, BRICK[(n + (n > 5)) & 3][0]);
  }
#endif
}

static void overlay(void) {
  char s[24];
  if (state == S_TITLE || state == S_SET) {
    logo(112);
    if (state == S_TITLE) {
      static const char *const items[2] = {T("PLAY"), T("SETTINGS")};
      for (int i = 0; i < 2; i++) item(items[i], 150 + i * 28, tsel == i);
      if (tsel == 0 && sv.reached > 1) {
        cat(itoa_(cat(s, T("< LEVEL ")), (int32_t)start_lv), T(" >"));
        ctext(s, 202, 1, PALE);
      }
      itoa_(cat(s, T("BEST ")), (int32_t)sv.best);
      ctext(s, 214, 1, DIM);
    } else {
      static const char *const names[2] = {T("SPEED"), T("SHAKE")};
      for (int i = 0; i < 2; i++) {
        bool on = sel == i;
        if (on) fill(40, 145 + i * 28, 240, 24, WHITE);
        text(names[i], 50, 150 + i * 28, 2, on ? BG : PALE);
        const char *v = i == 0 ? speed_names[sv.speed] : sv.shake ? T("ON") : T("OFF");
        text(v, 270 - tw(v, 2), 150 + i * 28, 2, on ? BG : PALE);
      }
      ctext(T("LEFT/RIGHT: CHANGE   BACK: DONE"), 210, 1, DIM);
    }
    return;
  }
  if (state == S_CLEAR) {
    itoa_(cat(s, T("LEVEL ")), level + 1);
    cat(s + slen(s), T(" CLEAR"));
    ctext(s, 146, 2, WHITE);
  }
  if (state == S_READY && !demo) {
    if (!hinted) keys_hint(104);
    else if (st_t > 20) ctext(T("OK: LAUNCH"), 160, 1, PALE);
  }
  if (state == S_PAUSE) {
    static const char *const items[3] = {T("RESUME"), T("RESTART"), T("QUIT GAME")};
    panel(80, 58, 160, 124);
    ctext(T("PAUSED"), 70, 2, WHITE);
    for (int i = 0; i < 3; i++) item(items[i], 104 + i * 26, sel == i);
  } else if (state == S_QUIT) {
    panel(80, 74, 160, 92);
    ctext(T("QUIT GAME?"), 88, 2, WHITE);
    for (int i = 0; i < 2; i++) {
      int x = 98 + i * 66;
      if (sel == i) fill(x, 123, 58, 24, WHITE);
      const char *v = i ? T("YES") : T("NO");
      text(v, NP_TEXT_EXTRA ? x + 29 - tw(v, 2) / 2 : x + 17 - i * 6, 128, 2, sel == i ? BG : PALE);
    }
  } else if (state == S_OVER) {
    panel(60, 50, 200, 140);
    ctext(T("GAME OVER"), 64, 3, WHITE);
    itoa_(cat(s, T("SCORE ")), score);
    ctext(s, 100, 2, PALE);
    itoa_(cat(s, T("BEST ")), (int32_t)sv.best);
    ctext(s, 124, 2, fresh_best && (st_t & 16) ? BRICK[2][0] : DIM);
    if (fresh_best) ctext(T("NEW BEST!"), 146, 1, BRICK[2][0]);
    ctext(T("OK: PLAY AGAIN   BACK: MENU"), 170, 1, DIM);
  }
}

/* one area of the screen, in layers */
static void render(void) {
  background();
  bricks();
  things();
  paddle();
  balls();
  hud();
  bool dimmed = state >= S_PAUSE || state <= S_SET || (state == S_READY && !hinted && !demo);
  if (dimmed) /* dimmed under a menu */
    for (color *p = buf, *e = buf + rw * (ry1 - ry0); p < e; p++) *p = mix(0, *p, 16);
  overlay();
}

static void flush(void) {
  if (full) ndirty = 0, mark(0, 0, W, H), full = false;
  for (int i = 0; i < ndirty; i++) {
    rect_t r = dirty[i];
    int w = r.x1 - r.x0, band = BUFN / w;
    for (int y = r.y0; y < r.y1; y += band) {
      rx0 = r.x0, rx1 = r.x1, ry0 = y, ry1 = imin(y + band, r.y1), rw = w;
      render();
      eadk_display_push_rect((eadk_rect_t){(uint16_t)rx0, (uint16_t)ry0, (uint16_t)w, (uint16_t)(ry1 - ry0)}, buf);
    }
  }
  ndirty = 0;
}

/* everything that moves, where it is now (called before and after moving) */
static int paddle_key, paddle_mx;
static void mark_moving(void) {
  for (int i = 0; i < NBALL; i++) {
    ball_t *b = &ball[i];
    if (!b->on) continue;
    int x0 = b->x >> 8, y0 = b->y >> 8, x1 = x0 + BS, y1 = y0 + BS;
    if (fire_t)
      for (int t = 0; t < 4; t++)
        x0 = imin(x0, b->tx[t]), y0 = imin(y0, b->ty[t]), x1 = imax(x1, b->tx[t] + BS), y1 = imax(y1, b->ty[t] + BS);
    mark_at(x0, y0, x1 - x0, y1 - y0);
  }
  for (int i = 0; i < NSHOT; i++)
    if (shot[i].on) mark_at(shot[i].x, shot[i].y, 2, 8);
  for (int i = 0; i < NPART; i++)
    if (part[i].life) mark_at(part[i].x >> 4, part[i].y >> 4, part[i].size, part[i].size);
  /* the paddle, only when it changed: where it was drawn, and where it is */
  int x = (px + ONE / 2) >> 8, key = x | pw << 9 | !!laser_t << 17 | paddle_shown() << 18;
  if (key != paddle_key) {
    x -= pw / 2;
    mark(paddle_mx, PY - 6, paddle_mx + (paddle_key >> 9 & 255) + 12, PY + PH + 6);
    mark_at(x, PY - 4, pw, PH + 4);
    paddle_key = key, paddle_mx = x - 6;
  }
}

/* ------------------------------------------------------------------ effects */
/* n bits flying out of an area; f: how hard, in 16ths of a pixel a tick */
static void burst(int x, int y, int w, int h, color c, int n, int f) {
  static int next;
  while (n--) {
    part_t *p = &part[next];
    next = (next + 1) % NPART;
    if (p->life) mark_at(p->x >> 4, p->y >> 4, p->size, p->size);
    p->x = (int16_t)((x + rndi(w)) * 16), p->y = (int16_t)((y + rndi(h)) * 16);
    p->vx = (int8_t)(rndi(2 * f + 1) - f), p->vy = (int8_t)(rndi(2 * f + 1) - f * 13 / 10);
    p->c = c, p->size = (uint8_t)(2 + rndi(3));
    p->max = p->life = (uint8_t)(16 + rndi(20));
  }
}
static void shake(int a, int t) {
  if (!sv.shake) return;
  shake_a = imax(shake_a, a), shake_t = imax(shake_t, t);
}

/* ------------------------------------------------------------------ rules */
static int isqrt(int v) { return (int)__builtin_sqrtf((float)v); }
static void set_speed(int s) {
  spd = s;
  for (int i = 0; i < NBALL; i++) {
    ball_t *b = &ball[i];
    int l = isqrt(b->vx * b->vx + b->vy * b->vy);
    if (l) b->vx = b->vx * s / l, b->vy = b->vy * s / l;
  }
}

static void load_wall(void) {
  bricks_left = 0;
  const char *const *w = WALLS[level % NWALLS];
  for (int r = 0; r < ROWS; r++)
    for (int c = 0; c < COLS; c++) {
      char ch = w[r] ? w[r][c] : '.';
      int k = r % 4 + 1, u = U_NONE;
      static const char U[] = " T+owfl";
      if (ch == '.') k = 0;
      else if (ch == 'b') k = 1;
      else if (ch == 'r') k = 2;
      else if (ch == 'y') k = 3;
      else if (ch == 'g') k = 4;
      for (int i = 1; i < NU; i++)
        if (ch == U[i]) u = i;
      if (level >= NWALLS && u == U_NONE && k && !rndi(12)) u = 1 + rndi(NU - 1); /* again: new surprises */
      brick[r][c] = (uint8_t)(k ? k | u << 3 : 0), flash[r][c] = 0;
      bricks_left += !!k;
    }
}

static int base_width(void) { return 60; }
static void ball_tick(ball_t *b);
static void serve(void) { /* a new ball, on the paddle */
  for (int i = 0; i < NBALL; i++) ball[i].on = 0;
  ball_t *b = &ball[0];
  b->on = b->stuck = 1;
  b->off = (int16_t)(demo ? 0 : rndi(9) - 4);
  b->vx = 154, b->vy = -ONE;
  pw = pw_to = base_width(), demo_aim = 0;
  wide_t = fire_t = laser_t = 0;
  for (int i = 0; i < NSHOT; i++) shot[i].on = 0;
  static const int SPEED[3] = {500, 610, 740}; /* 2 to 3 pixels a tick, a little faster each wall */
  spd0 = SPEED[sv.speed] * (100 + imin(level, 16) * 5) / 100;
  set_speed(spd0);
  ball_tick(b);
}

static void set_state(int s) {
  ui_area(state);
  state = s, st_t = 0, sel = 0;
  ui_area(state);
}

static void start_wall(void) {
  load_wall();
  serve();
  set_state(S_READY);
  full = true;
  if (!demo && level + 1 > sv.reached && level + 1 <= 99) sv.reached = (uint8_t)(level + 1), save();
}

static void new_game(void) {
  demo = false;
  score = 0, lives = 3, fresh_best = false, hinted = false;
  level = start_lv - 1;
  for (int i = 0; i < NPART; i++) part[i].life = 0;
  px = W / 2 * ONE;
  start_wall();
}

static void title(void) {
  demo = true, level = 0;
  load_wall();
  serve();
  set_state(S_TITLE);
  full = true;
}

/* keeps the best score, for a finished or abandoned game */
static void keep_best(void) {
  if (!demo && (uint32_t)score > sv.best) sv.best = (uint32_t)score, fresh_best = true;
  save();
}

static void add_score(int n) {
  score += n;
  if (score > 99999) score = 99999;
  mark(0, 0, W, TOP);
}

static void power_up(int u, int x, int y);
/* a brick breaks: bits fly, its power-up goes off */
static void break_brick(int r, int c) {
  int v = brick[r][c];
  if (!v) return;
  brick[r][c] = 0, flash[r][c] = 0;
  bricks_left--;
  mark_brick(r, c);
  int x = brick_x(c), y = brick_y(r);
  const color *k = BRICK[(v & 7) - 1];
  burst(x, y, BW, BH, k[0], 7, 20);
  burst(x, y, BW, BH, k[1], 4, 14);
  shake(1, 5);
  if (demo) return;
  add_score(v >> 3 ? 50 : 10);
  if (v >> 3) power_up(v >> 3, x, y), (void)r;
  if (v >> 3 == U_TNT) { /* it goes off: the bricks around break too */
    shake(4, 12);
    burst(x - 8, y - 6, BW + 16, BH + 12, RGB(0xFFB040), 14, 30);
    for (int dr = -1; dr <= 1; dr++)
      for (int dc = -1; dc <= 1; dc++) {
        int rr = r + dr, cc = c + dc;
        if ((dr || dc) && rr >= 0 && rr < ROWS && cc >= 0 && cc < COLS && brick[rr][cc]) break_brick(rr, cc);
      }
  }
}

/* the brick the ball is over, if any (a gap between bricks is empty) */
static bool touch_bricks(const ball_t *b, int *hr, int *hc) {
  int x0 = (b->x >> 8) - BX0, y0 = (b->y >> 8) - BY0, x1 = x0 + BS - 1, y1 = y0 + BS - 1;
  const int sw = BW + GAP, shh = BH + GAP;
  if (y1 < 0 || y0 >= ROWS * shh || x1 < 0 || x0 >= COLS * sw) return false;
  for (int r = imax(y0, 0) / shh; r <= imin(y1 / shh, ROWS - 1); r++)
    for (int c = imax(x0, 0) / sw; c <= imin(x1 / sw, COLS - 1); c++) {
      int bx = c * sw, by = r * shh;
      if (brick[r][c] && x1 >= bx && x0 < bx + BW && y1 >= by && y0 < by + BH) {
        *hr = r, *hc = c;
        return true;
      }
    }
  return false;
}

/* a: the angle from straight up, in 256ths of a radian (a sine good to 1 %) */
static void aim(ball_t *b, int a) {
  int a2 = a * a / 256, s = a * (256 - a2 * (256 - a2 / 20) / 1536) / 256;
  b->vx = spd * s / 256, b->vy = -spd * isqrt(65536 - s * s) / 256;
}
static int untouched; /* ticks since a ball last met the paddle */
/* Long without the paddle, a bounce turns the ball a little, so it can't
   loop forever. */
static void nudge(ball_t *b) {
  if (untouched < 600) return;
  untouched = 420;
  for (int k = 0, t = rndi(2) ? 25 : -25; k < 2; k++, t = -t) {
    int vx = (b->vx * 255 - b->vy * t) / 256, vy = (b->vy * 255 + b->vx * t) / 256;
    if (iabs(vx) * 256 <= spd * 228 && iabs(vy) * 256 >= spd * 60) {
      b->vx = vx, b->vy = vy;
      return;
    }
  }
}
static void launch(ball_t *b) {
  untouched = 0;
  b->stuck = 0;
  int o = b->off * 256 / (pw / 2);
  aim(b, (o < 0 ? -90 : 90) + o / 2);
}

static void power_up(int u, int x, int y) {
  color c = RGB(0xFFFFFF);
  if (u == U_LIFE && lives < 5) lives++, mark(0, 0, W, TOP);
  if (u == U_MULTI) { /* two more balls from the last one */
    ball_t *b = 0;
    for (int i = 0; i < NBALL; i++)
      if (ball[i].on && !ball[i].stuck) b = &ball[i];
    for (int i = 0, n = 0; b && i < NBALL && n < 2; i++)
      if (!ball[i].on) {
        ball[i] = *b;
        int a = b->vx * 256 / (spd ? spd : 1) + (n++ ? 100 : -100);
        a = imax(-282, imin(282, a));
        if (a > -56 && a < 56) a = a < 0 ? -56 : 56;
        aim(&ball[i], a);
        if (b->vy > 0) ball[i].vy = -ball[i].vy;
      }
  }
  if (u == U_WIDE) wide_t = 60 * 12, pw_to = 90;
  if (u == U_FIRE) fire_t = 60 * 8;
  if (u == U_LASER) laser_t = 60 * 8;
  burst(x, y, BW, BH, c, 6, 24);
}

static void lose_life(void) {
  shake(4, 14);
  burst((px >> 8) - pw / 2, PY, pw, PH, WHITE, 16, 35);
  set_state(S_LOST);
}

static void ball_tick(ball_t *b) {
  for (int t = 3; t > 0; t--) b->tx[t] = b->tx[t - 1], b->ty[t] = b->ty[t - 1];
  b->tx[0] = (int16_t)(b->x >> 8), b->ty[0] = (int16_t)(b->y >> 8);
  if (b->stuck) {
    int x = (px >> 8) + b->off - BS / 2;
    x = x < 0 ? 0 : x > W - BS ? W - BS : x;
    b->x = x * ONE, b->y = (PY - BS) * ONE;
    for (int t = 0; t < 4; t++) b->tx[t] = (int16_t)x, b->ty[t] = PY - BS;
    return;
  }
  int n = imax(iabs(b->vx), iabs(b->vy)) / ONE + 1, r, c; /* steps of a pixel at most */
  for (int i = 0; i < n; i++) {
    int dx = b->vx / n, dy = b->vy / n;
    b->x += dx;
    if (b->x < 0) b->x = 0, b->vx = iabs(b->vx), nudge(b);
    else if (b->x > (W - BS) * ONE) b->x = (W - BS) * ONE, b->vx = -iabs(b->vx), nudge(b);
    else if (touch_bricks(b, &r, &c)) {
      break_brick(r, c);
      if (!fire_t) b->x -= dx, b->vx = -b->vx, nudge(b);
    }
    b->y += dy;
    if (b->y < TOP * ONE) {
      b->y = TOP * ONE, b->vy = iabs(b->vy), nudge(b);
    } else if (touch_bricks(b, &r, &c)) {
      break_brick(r, c);
      if (!fire_t) b->y -= dy, b->vy = -b->vy, nudge(b);
    }
    int half = pw * ONE / 2, bot = b->y + BS * ONE, mid = b->x + BS * ONE / 2 - px;
    if (b->vy > 0 && bot >= PY * ONE && bot - dy <= (PY + 1) * ONE && mid > -half - BS * ONE / 2 &&
        mid < half + BS * ONE / 2) { /* the paddle: where it lands decides where it goes */
      b->y = (PY - BS) * ONE, untouched = 0;
      int a = mid * 282 / (half + BS * ONE / 2); /* up to 1.1 radians off vertical, and never too steep */
      if (a > -56 && a < 56) a = b->vx < 0 ? -56 : 56;
      aim(b, a);
      demo_aim = rndi(pw * 4 / 5 + 1) - pw * 2 / 5;
      return;
    }
    if (b->y > H * ONE) {
      b->on = 0;
      return;
    }
  }
}

static void clear_wall(void) {
  for (int i = 0; i < NBALL; i++) ball[i].on = 0;
  for (int i = 0; i < NSHOT; i++) shot[i].on = 0;
  add_score(100 * (level + 1));
  set_state(S_CLEAR);
}

/* one 60th of a second of play; d is the paddle's direction, fire is OK */
static void play_tick(int d, bool fire) {
  /* the paddle: a tap nudges it, holding speeds it up */
  if (!d) pv = 0;
  else if (pv * d <= 0) pv = d * 300;
  else if ((pv += d * 120) * d > 7 * ONE) pv = d * 7 * ONE;
  px += pv;
  if (pw != pw_to) pw += pw < pw_to ? 1 : -1;
  int lo = pw * ONE / 2, hi = (W * 2 - pw) * ONE / 2;
  if (px < lo) px = lo, pv = 0;
  if (px > hi) px = hi, pv = 0;
  if (demo ? ball[0].stuck : state == S_READY) {
    ball_tick(&ball[0]);
    if (fire) {
      launch(&ball[0]);
      if (!demo) {
        if (!hinted) hinted = true, full = true;
        set_state(S_PLAY);
      }
    }
    return;
  }
  if (!demo && state != S_PLAY) return;
  int alive = 0;
  for (int i = 0; i < NBALL; i++)
    if (ball[i].on) {
      ball_tick(&ball[i]);
      alive += ball[i].on;
    }
  if (!alive) {
    if (demo) serve();
    else lose_life();
    return;
  }
  if (wide_t && !--wide_t) pw_to = base_width();
  if (fire_t) fire_t--;
  if (laser_t) { /* the laser fires by itself */
    laser_t--;
    if (--laser_cd <= 0) {
      int x = (px >> 8) - pw / 2;
      for (int k = 0, i = 0; k < 2 && i < NSHOT; i++)
        if (!shot[i].on) shot[i] = (shot_t){(int16_t)(k++ ? x + pw - 6 : x + 4), PY - 12, 1};
      laser_cd = 24;
    }
  }
  for (int i = 0; i < NSHOT; i++) {
    shot_t *s = &shot[i];
    if (!s->on) continue;
    s->y -= 6;
    if (s->y < TOP) {
      s->on = 0;
      continue;
    }
    int r = (s->y - BY0) / (BH + GAP), c = (s->x - BX0) / (BW + GAP);
    if (s->y >= BY0 && r < ROWS && s->x >= BX0 && c < COLS && brick[r][c] && (s->x - BX0) % (BW + GAP) < BW) break_brick(r, c), s->on = 0;
  }
  if (!bricks_left && !demo) {
    shake(3, 10);
    clear_wall();
  }
  if (demo && !bricks_left) load_wall();
}

/* the title screen's paddle plays by itself, after the lowest ball */
static int demo_dir(void) {
  ball_t *b = &ball[0];
  for (int i = 1; i < NBALL; i++)
    if (ball[i].on && (!b->on || ball[i].y > b->y)) b = &ball[i];
  int dd = (b->x >> 8) + BS / 2 - demo_aim - (px >> 8);
  return dd > 3 ? 1 : dd < -3 ? -1 : 0;
}

static void tick(int d, bool fire) {
  st_t++, tk++, untouched++;
  if (shake_t) {
    shake_t--;
    int a = imin(shake_a, shake_t / 2 + 1);
    sx = shake_t ? rndi(2 * a + 1) - a : 0, sy = shake_t ? rndi(2 * a + 1) - a : 0;
    if (!shake_t) shake_a = 0;
  }
  for (int i = 0; i < NPART; i++) {
    part_t *p = &part[i];
    if (!p->life) continue;
    p->x += p->vx, p->y += p->vy, p->vy += 2, p->vx -= p->vx / 16;
    p->life--;
  }
  if (demo) d = demo_dir(), fire = !(st_t & 63);
#ifdef AUTOPLAY /* a test build that plays by itself */
  if (state == S_READY || state == S_PLAY) d = demo_dir(), fire = fire || !(st_t & 31);
#endif
  if (state == S_READY && st_t == 21) ui_area(S_READY); /* the hint shows up */
  if (demo || state == S_READY || state == S_PLAY) play_tick(d, fire);
  if (state == S_LOST && st_t > 70) {
    if (--lives > 0) serve(), set_state(S_READY), mark(0, 0, W, TOP);
    else keep_best(), set_state(S_OVER);
  }
  if (state == S_CLEAR && st_t > 100) level++, start_wall();
}

/* n ticks of play, and the areas they change */
static void frame(int n, int d, bool fire) {
  mark_moving();
  int psx = sx, psy = sy;
  while (n-- > 0) {
    tick(d, fire);
    fire = false;
  }
  mark_moving();
  if (sx != psx || sy != psy) { /* the bricks shake: where there are some */
    int r1 = 0;
    for (int r = 0; r < ROWS; r++)
      for (int c = 0; c < COLS; c++)
        if (brick[r][c]) r1 = r + 1;
    if (r1) mark(BX0 - 8, BY0 - 8, BX0 + COLS * (BW + GAP) + 8, brick_y(r1) + 8);
  }
}

/* ------------------------------------------------------------------ menus */
static bool leave, ask_title; /* Quit game: back to NumPlay (or the calculator) */
static void menu_input(uint64_t hit) {
  bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
  int ud = (hit & KEY(eadk_key_down) ? 1 : 0) - (hit & KEY(eadk_key_up) ? 1 : 0);
  int lr = (hit & KEY(eadk_key_right) ? 1 : 0) - (hit & KEY(eadk_key_left) ? 1 : 0);
  int before = sel + tsel * 4 + start_lv * 16;
  switch (state) {
    case S_TITLE:
      tsel = (tsel + ud + 2) % 2;
      if (tsel == 0 && lr) start_lv = (start_lv + lr + sv.reached - 1) % sv.reached + 1;
      if (back) {
        ask_title = true, sel = 0;
        set_state(S_QUIT);
      } else if (ok) {
        if (tsel == 1) set_state(S_SET);
        else new_game();
      }
      break;
    case S_SET:
      sel = (sel + ud + 2) % 2;
      if (lr || ok) {
        if (sel == 0) sv.speed = (uint8_t)((sv.speed + (lr ? lr : 1) + 3) % 3);
        else sv.shake ^= 1;
        ui_area(S_SET);
      }
      if (back) {
        save();
        set_state(S_TITLE);
      }
      break;
    case S_READY:
    case S_PLAY:
      if (back) resume_state = state, set_state(S_PAUSE);
      break;
    case S_PAUSE:
      sel = (sel + ud + 3) % 3;
      if (back || (ok && sel == 0)) {
        state = resume_state, st_t = 30, full = true;
      } else if (ok && sel == 1) {
        keep_best();
        new_game();
      } else if (ok) {
        set_state(S_QUIT);
      }
      break;
    case S_QUIT:
      if (lr) sel ^= 1;
      if ((back || (ok && !sel)) && ask_title) ask_title = false, title();
      else if (back || (ok && !sel)) set_state(S_PAUSE), sel = 2;
      else if (ok) {
        keep_best();
        leave = true;
      }
      break;
    case S_OVER:
      if (ok) new_game();
      else if (back) title();
      break;
  }
  if (before != sel + tsel * 4 + start_lv * 16) ui_area(state);
}

int main(void) {
  np_app_begin();
  load();
  start_lv = sv.reached;
  seed ^= (uint32_t)eadk_timing_millis() * 2654435761u;
  title();
  uint64_t ignore = eadk_keyboard_scan(), held = 0;
  const uint64_t rep = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down);
  uint32_t t0 = (uint32_t)eadk_timing_millis(), ticks = 0, frames = 0, repeat_at = 0;
  for (;;) {
    uint32_t now = (uint32_t)eadk_timing_millis();
    uint64_t k = eadk_keyboard_scan();
    ignore &= k; /* keys held from before start count once released */
    k &= ~ignore;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    uint64_t hit = k & ~held;
    held = k;
    bool in_menu = state == S_TITLE || state == S_SET || state >= S_PAUSE;
    if (hit & rep) repeat_at = now + 350;
    else if (in_menu && (k & rep) && (int32_t)(now - repeat_at) >= 0) hit |= k & rep, repeat_at = now + 90;
    int s = state;
    menu_input(hit);
    if (leave) break;
    bool fire = s == state && (s == S_READY || s == S_PLAY) && (hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe) | KEY(eadk_key_up)));
    int d = (k & (KEY(eadk_key_right) | KEY(eadk_key_six)) ? 1 : 0) - (k & (KEY(eadk_key_left) | KEY(eadk_key_four)) ? 1 : 0);
    /* the game runs 60 ticks a second, whatever the frame rate */
    uint32_t due = (now - t0) * 3 / 50;
    static bool fire_due; /* a press waits for a tick to use it */
    if ((int32_t)(due - ticks) > 4) ticks = due - 4;
    if (state < S_PAUSE) {
      /* the ticks owed, or one ahead: every frame moves, and none twice as much */
      int n = (int32_t)(due - ticks) > 0 ? (int)(due - ticks) : (int32_t)(ticks - due) < 1;
      fire_due |= fire;
      frame(n, d, fire_due);
      if (n) fire_due = false;
      ticks += (uint32_t)n;
    } else {
      ticks = due, fire_due = false;
      st_t++;
      if (state == S_OVER && !(st_t & 15) && fresh_best) mark(60, 120, 260, 156);
    }
    eadk_display_wait_for_vblank();
    flush();
    /* at most 60 frames a second */
    frames++;
    uint32_t next = t0 + frames * 50 / 3, after = (uint32_t)eadk_timing_millis();
    if ((int32_t)(next - after) > 0) eadk_timing_msleep(next - after);
    else if (after - next > 100) frames = (after - t0) * 3 / 50;
  }
  keep_best();
  return np_app_end();
}
