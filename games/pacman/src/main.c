/* Pac-Man for the NumWorks calculator: Namco's 1980 arcade game.
 *
 * The rules follow the arcade: four ghosts that hunt each in their own way,
 * scatter and chase waves, the speed and fright tables of every level,
 * Cruise Elroy, the ghost house counters, cornering, the fruit and the extra
 * life. The options and the saved game come from Tatone26's version in All
 * the Apps.
 *
 * The 28x31 maze uses 7-pixel tiles so it fits the 240-pixel screen, with the
 * scores at the sides. Its walls are traced once from a bit map of the maze
 * (a distance field gives the arcade's rounded double lines); after that only
 * the few tiles under the sprites are redrawn, 60 times a second. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_text.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Pac-Man";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define KEY(k) (1ull << (k))

/* the arcade's colours */
#define BLACK 0
#define WALL RGB(0x2121DE)
#define PEACH RGB(0xFFB8AE)
#define YELLOW RGB(0xFFFF00)
#define WHITE RGB(0xDEDEFF)
#define BLUE RGB(0x2121FF)
#define RED RGB(0xFF0000)
#define PINK RGB(0xFFB8FF)
#define CYAN RGB(0x00FFFF)
#define ORANGE RGB(0xFFB852)
#define GREY RGB(0x7C7C9C)

/* the maze: 28x31 tiles of 7 pixels, in the middle of the screen */
#define MX 62
#define MY 11
#define MW 196
#define MH 217

enum { UP, LEFT, DOWN, RIGHT, NONE }; /* ghosts break ties in this order */
static const int8_t DX[5] = {0, -1, 0, 1, 0}, DY[5] = {-1, 0, 1, 0, 0};

static uint32_t seed = 0x9E3779B9;
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

/* ------------------------------------------------------------------ drawing */
/* Everything is composed in this buffer, a rectangle of the screen at a time,
   then pushed: each pixel is written once, so nothing flickers. */
#define BUFPX (320 * 16)
static color buf[BUFPX];
static int bx, by, bw, cx0, cy0, cx1, cy1; /* the buffer's place, and the clip */
static void (*scene)(void);                /* draws everything in the clip */

static void fill(int x, int y, int w, int h, color c) {
  int x0 = x > cx0 ? x : cx0, x1 = x + w < cx1 ? x + w : cx1;
  int y0 = y > cy0 ? y : cy0, y1 = y + h < cy1 ? y + h : cy1;
  for (; y0 < y1; y0++)
    for (color *p = buf + (y0 - by) * bw + x0 - bx, *e = p + (x1 - x0); p < e; p++) *p = c;
}
static void pset(int x, int y, color c) {
  if (x >= cx0 && x < cx1 && y >= cy0 && y < cy1) buf[(y - by) * bw + x - bx] = c;
}
static bool hits(int x, int y, int w, int h) { return x < cx1 && y < cy1 && x + w > cx0 && y + h > cy0; }

/* redraws a rectangle of the screen, in bands that fit the buffer */
static void refresh(int x, int y, int w, int h) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > 320) w = 320 - x;
  if (y + h > 240) h = 240 - y;
  if (w <= 0 || h <= 0) return;
  for (int band = BUFPX / w, j = y; j < y + h; j += band) {
    int n = y + h - j < band ? y + h - j : band;
    bx = cx0 = x, by = cy0 = j, bw = w, cx1 = x + w, cy1 = j + n;
    scene();
    eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)j, (uint16_t)w, (uint16_t)n}, buf);
  }
}
/* a thin frame with its corners cut, like the maze's walls */
static void frame(int x, int y, int w, int h, color c) {
  fill(x + 1, y, w - 2, 1, c);
  fill(x + 1, y + h - 1, w - 2, 1, c);
  fill(x, y + 1, 1, h - 2, c);
  fill(x + w - 1, y + 1, 1, h - 2, c);
}

/* ------------------------------------------------------------------ text */
/* ASCII 32..90, columns of 7 bits, top bit first. Each column is drawn two
   pixels wide, which gives the arcade's font: thick uprights, thin bars, one
   character per 7-pixel tile. */
static const uint8_t font[59][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x07,0x00,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},{0x24,0x2A,0x7F,0x2A,0x12},
  {0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},{0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},
  {0x14,0x08,0x3E,0x08,0x14},{0x08,0x08,0x3E,0x08,0x08},{0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},
  {0x20,0x10,0x08,0x04,0x02},{0x3E,0x41,0x41,0x41,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
  {0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},
  {0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},{0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
  {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
  {0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},
  {0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
  {0x7F,0x20,0x10,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},{0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43},
};

static void glyph(int ch, int x, int y, int k, color c) {
  if (!hits(x, y, 6 * k, 7 * k)) return;
  unsigned g = (unsigned)ch - 32u;
  if (g > 58) g = '?' - 32;
  for (int i = 0; i < 5; i++)
    for (int j = 0, b = font[g][i]; b; j++, b >>= 1)
      if (b & 1) fill(x + i * k, y + j * k, 2 * k, k, c);
}
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
/* Language builds: a letter of a translation at x (draw 0: only its advance). An accented one is
   the plain capital with its accent (np_text.h) just over it, in the font's two-pixel columns
   (under it, a cedilla); others (Chinese) come from the 12-pixel font, as tall as the capitals. */
typedef struct {
  int x, y, k;
  color c;
} xpen_t;
static void xcol(int i, int j, void *ctx) {
  const xpen_t *p = ctx;
  fill(p->x + i * p->k, p->y + j * p->k, 2 * p->k, p->k, p->c);
}
static void xpix(int i, int j, void *ctx) {
  const xpen_t *p = ctx;
  fill(p->x + i * p->k, p->y + j * p->k, p->k, p->k, p->c);
}
static int xletter(uint32_t cp, int x, int y, int k, color c, int draw) {
  char b = (char)cp, b2 = 0;
  int acc = cp >= 0x80 ? np_latin(cp, &b, &b2) : 0, w = 0;
  if (cp >= 0x80 && !b) {
    int s = (7 * k + 6) / 12, top = y + (7 * k - 12 * s) / 2;
    xpen_t p = {x, top, s, c};
    if (draw && hits(x, top, 12 * s, 12 * s)) np_xdraw(cp, 0, 0, 1, xpix, &p);
    return np_xadvance(cp, s);
  }
  for (; b; b = b2, b2 = 0, acc = 0, x += 7 * k, w += 7 * k) {
    if (b >= 'a' && b <= 'z') b -= 32; /* (capitals only) */
    if (!draw) continue;
    glyph(b, x, y, k, c);
    xpen_t p = {x, acc == NP_ACC_CEDIL ? y + 7 * k : y - 3 * k, k, c};
    np_accent(acc, 0, 0, 1, xcol, &p);
  }
  return w;
}
static int tw(const char *s, int k) {
  if (NP_TEXT_EXTRA) {
    int w = 0;
    while (*s) w += xletter(np_utf8(&s), 0, 0, k, 0, 0);
    return w - k;
  }
  return slen(s) * 7 * k - k;
}
static void text(const char *s, int x, int y, int k, color c) {
  if (NP_TEXT_EXTRA) {
    while (*s) x += xletter(np_utf8(&s), x, y, k, c, 1);
    return;
  }
  for (; *s; s++, x += 7 * k) glyph(*s, x, y, k, c);
}
static void ctext(const char *s, int cx, int y, int k, color c) { text(s, cx - tw(s, k) / 2, y, k, c); }
static void rtext(const char *s, int rx, int y, int k, color c) { text(s, rx - tw(s, k), y, k, c); }

static char nb[12];
static char *num(uint32_t v) {
  char *o = nb + 11;
  do *--o = (char)('0' + v % 10); while (v /= 10);
  return o;
}

/* the small digits of the points shown in the maze, 3x5 */
static const uint16_t mini_font[10] = {0x7B6F, 0x2C97, 0x73E7, 0x73CF, 0x5BC9, 0x79CF, 0x79EF, 0x7249, 0x7BEF, 0x7BCF};
static void mini(uint32_t v, int cx, int y, color c) {
  char *s = num(v);
  for (int x = cx - (slen(s) * 4 - 1) / 2; *s; s++, x += 4)
    for (int b = 0; b < 15; b++)
      if (mini_font[*s - '0'] >> b & 1) pset(x + 2 - b % 3, y + 4 - b / 3, c);
}

/* ------------------------------------------------------------------ sprites */
/* Pac-Man centred on (x, y), r pixels round (6 in the maze), facing d, the
   mouth open from 0 (closed) to 4 (wide); with die > 0 it is his death: a
   mouth opening from the top until nothing is left (die up to 32) */
static void pac_r(int x, int y, int d, int open, int die, int r) {
  if (!hits(x - r, y - r, 2 * r + 1, 2 * r + 1)) return;
  for (int j = -r; j <= r; j++)
    for (int i = -r; i <= r; i++) {
      if (i * i + j * j > r * r + r) continue;
      if (die) {
        int p = i < 0 ? -i : i, s = p + (j < 0 ? -j : j);
        if ((j <= 0 ? p : s + j) * 16 < die * s) continue;
      } else {
        int a = i * DX[d] + j * DY[d], p = i * DY[d] - j * DX[d];
        if (a > 0 && 4 * (p < 0 ? -p : p) < open * a) continue;
      }
      pset(x + i, y + j, YELLOW);
    }
}
static void pac_draw(int x, int y, int d, int open, int die) { pac_r(x, y, d, open, die, 6); }

/* the ghost, 13x13: the body's rows (bit i is column i), then two frames of the skirt */
static const uint16_t ghost_body[11] = {0x01F0, 0x07FC, 0x0FFE, 0x0FFE, 0x0FFE, 0x1FFF,
                                        0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF, 0x1FFF};
static const uint16_t ghost_skirt[4] = {0x1BBB, 0x11B1, 0x1FFF, 0x0CE6};
static const color ghost_col[4] = {RED, PINK, CYAN, ORANGE};
enum { LOOK_NORMAL, LOOK_BLUE, LOOK_FLASH, LOOK_EYES };

/* a ghost centred on (x, y), looking towards d */
static void ghost_draw(int x, int y, color body, int d, int look, int fr) {
  x -= 6, y -= 6;
  if (!hits(x, y, 13, 13)) return;
  if (look != LOOK_EYES) {
    if (look) body = look == LOOK_FLASH ? WHITE : BLUE;
    for (int r = 0; r < 13; r++)
      for (unsigned c = 0, b = r < 11 ? ghost_body[r] : ghost_skirt[fr * 2 + r - 11]; b; c++, b >>= 1)
        if (b & 1) pset(x + (int)c, y + r, body);
  }
  if (look == LOOK_BLUE || look == LOOK_FLASH) { /* frightened: little eyes and a wavy mouth */
    color f = look == LOOK_FLASH ? RED : PEACH;
    fill(x + 3, y + 4, 2, 2, f);
    fill(x + 8, y + 4, 2, 2, f);
    for (int c = 1; c < 12; c++) pset(x + c, y + 8 + (c >> 1 & 1), f);
    return;
  }
  for (int e = 2; e < 9; e += 5) { /* the eyes look where the ghost goes */
    int ox = x + e + DX[d], oy = y + 2 + DY[d];
    fill(ox + 1, oy, 2, 5, WHITE);
    fill(ox, oy + 1, 4, 3, WHITE);
    fill(ox + 1 + DX[d], oy + 2 + 2 * DY[d] - (DY[d] > 0), 2, 2, BLUE);
  }
}

/* cherry, strawberry, orange, apple, melon, Galaxian, bell, key: 12x12, 2 bits a pixel */
static const uint8_t fruit_bits[8][36] = {
  {0x00,0x00,0xA0,0x00,0x00,0x2A,0x00,0x80,0x20,0x00,0x20,0x20,0x00,0x08,0x08,0x50,0x09,0x02,0x54,0x45,0x16,0x55,0x55,0x55,0x5D,0x55,0x55,0x75,0xD5,0x55,0x54,0x51,0x5D,0x50,0x40,0x15},
  {0x00,0x28,0x00,0xA0,0xAA,0x0A,0x94,0x96,0x16,0x75,0x55,0x5D,0x55,0x5D,0x55,0x5D,0x55,0x75,0x55,0x57,0x55,0x54,0xD5,0x55,0x74,0x55,0x15,0x50,0x75,0x01,0x40,0x55,0x01,0x00,0x55,0x00},
  {0x00,0x8C,0x02,0x00,0xAC,0x0A,0x40,0x5D,0x01,0x50,0x55,0x05,0x54,0x55,0x15,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x54,0x55,0x15,0x50,0x55,0x05,0x40,0x55,0x01},
  {0x00,0x08,0x00,0x40,0x59,0x00,0x54,0x59,0x15,0x55,0x55,0x55,0x7D,0x55,0x55,0x5D,0x55,0x55,0x5D,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x55,0x54,0x55,0x15,0x50,0x55,0x05,0x40,0x41,0x01},
  {0x00,0x28,0x00,0x00,0x20,0x00,0x40,0x55,0x01,0xD0,0xD5,0x05,0x54,0x77,0x15,0x74,0x5D,0x1D,0xD5,0xD5,0x55,0x5D,0x5D,0x5D,0xD4,0xD5,0x15,0x74,0x5D,0x1D,0x50,0x75,0x05,0x40,0x55,0x01},
  {0x00,0x14,0x00,0x00,0x55,0x00,0x02,0xFF,0x80,0xCA,0xFF,0xA3,0x2A,0xFF,0xA8,0xAA,0x14,0xAA,0x8A,0x55,0xA2,0x02,0x96,0x80,0x00,0x14,0x00,0x00,0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00},
  {0x00,0x14,0x00,0x00,0x55,0x00,0x40,0x55,0x01,0xD0,0x55,0x05,0xD0,0x55,0x05,0xD0,0x55,0x05,0x74,0x55,0x15,0x74,0x55,0x15,0x55,0x55,0x55,0x55,0x55,0x55,0x00,0xAA,0x00,0x00,0x28,0x00},
  {0x40,0x55,0x01,0x50,0xFF,0x05,0x50,0x55,0x05,0x00,0x28,0x00,0x00,0xA8,0x00,0x00,0x28,0x00,0x00,0xA8,0x00,0x00,0x28,0x00,0x00,0xA8,0x00,0x00,0x28,0x00,0x00,0x28,0x00,0x00,0x20,0x00},
};
#define BROWN RGB(0xDE9751)
#define GREEN RGB(0x00DE00)
static const color fruit_pal[8][3] = {
  {RED, BROWN, WHITE}, {RED, GREEN, WHITE}, {ORANGE, GREEN, BROWN}, {RED, BROWN, WHITE},
  {GREEN, BROWN, WHITE}, {YELLOW, BLUE, RED}, {YELLOW, CYAN, WHITE}, {CYAN, WHITE, BLACK},
};
static const uint16_t fruit_pts[8] = {100, 300, 500, 700, 1000, 2000, 3000, 5000};

static void fruit_draw(int x, int y, int f) {
  if (!hits(x, y, 12, 12)) return;
  const uint8_t *b = fruit_bits[f];
  for (int r = 0; r < 12; r++, b += 3)
    for (uint32_t c = 0, v = b[0] | b[1] << 8 | (uint32_t)b[2] << 16; c < 12; c++, v >>= 2)
      if (v & 3) pset(x + (int)c, y + r, fruit_pal[f][(v & 3) - 1]);
}
static int fruit_of(int level) { return level < 3 ? level - 1 : level > 12 ? 7 : (level + 1) / 2; }

/* ------------------------------------------------------------------ the maze */
/* the left half of each row (the right half mirrors it): a bit for each tile
   that is not a corridor (walls, the ghost house, the outside) */
static const uint16_t maze_rows[31] = {
  0x3FFF, 0x2001, 0x2FBD, 0x2FBD, 0x2FBD, 0x0001, 0x3DBD, 0x3DBD, 0x2181, 0x2FBF, 0x2FBF,
  0x01BF, 0x3DBF, 0x3DBF, 0x3C00, 0x3DBF, 0x3DBF, 0x01BF, 0x3DBF, 0x3DBF, 0x2001, 0x2FBD,
  0x2FBD, 0x0031, 0x3DB7, 0x3DB7, 0x2181, 0x2FFD, 0x2FFD, 0x0001, 0x3FFF,
};
static bool solid(int tx, int ty) {
  if ((unsigned)ty > 30) return true;
  if ((unsigned)tx > 27) return ty != 14; /* the tunnel goes on off screen */
  return maze_rows[ty] >> (tx < 14 ? tx : 27 - tx) & 1;
}
static int tile(int v) { return (v + 21) / 7 - 3; } /* floor(v / 7), for v >= -21 */

static uint64_t wpix[31][28]; /* the wall pixels of each tile */
static float hyp(float a, float b) { return __builtin_sqrtf(a * a + b * b); }

/* The walls run half a tile inside the wall tiles: a pixel is on a line when
   its distance to the nearest corridor is 3 to 4 pixels (and 5 to 6 for the
   second line of the outer wall and the ghost house). Concave corners come
   round by themselves; convex ones are rounded around a point inside. */
static void build_walls(void) {
  const float R = 7;
  for (int ty = 0; ty < 31; ty++)
    for (int tx = 0; tx < 28; tx++) {
      int c = tx < 14 ? tx : 27 - tx;
      uint64_t m = 0;
      bool out = ((ty >= 10 && ty <= 12) || (ty >= 16 && ty <= 18)) && c < 5;
      bool house = ty >= 12 && ty <= 16 && c >= 10, inside = house && ty != 12 && ty != 16 && c > 10;
      bool dbl = !c || !ty || ty == 30 || (ty >= 9 && ty <= 19 && c <= 5) || (ty >= 24 && ty <= 25 && c <= 2) ||
                 (c == 13 && ty <= 4) || house;
      if (solid(tx, ty) && !out && !inside && !(ty == 12 && c == 13)) {
        bool o[9];
        for (int k = 0; k < 9; k++) o[k] = !solid(tx + k % 3 - 1, ty + k / 3 - 1);
        for (int py = 0; py < 7; py++)
          for (int px = 0; px < 7; px++) {
            float u = px + .5f, v = py + .5f, U = 7 - u, V = 7 - v, d = 99;
            if (o[3] && u < d) d = u;
            if (o[5] && U < d) d = U;
            if (o[1] && v < d) d = v;
            if (o[7] && V < d) d = V;
            if (o[0] && hyp(u, v) < d) d = hyp(u, v);
            if (o[2] && hyp(U, v) < d) d = hyp(U, v);
            if (o[6] && hyp(u, V) < d) d = hyp(u, V);
            if (o[8] && hyp(U, V) < d) d = hyp(U, V);
            float a = o[3] ? u : o[5] ? U : R, b = o[1] ? v : o[7] ? V : R;
            if (a < R && b < R) d = R - hyp(R - a, R - b);
            if ((d >= 3 && d < 4) || (dbl && d >= 5 && d < 6)) m |= 1ull << (py * 7 + px);
          }
      }
      wpix[ty][tx] = m;
    }
  /* the pink door, and the ends of the walls beside it */
  wpix[12][13] = wpix[12][14] = 0x3FFFull << 28;
  wpix[12][12] |= 0x4081ull << 27;
  wpix[12][15] |= 0x4081ull << 21;
}

/* ------------------------------------------------------------------ the game's state */
enum { PH_START, PH_READY, PH_PLAY, PH_EAT, PH_DIE, PH_CLEAR, PH_OVER, PH_CUT };
enum { G_HOUSE, G_LEAVE, G_OUT, G_EYES, G_ENTER };

typedef struct {
  int16_t x, y; /* in maze pixels, the centre */
  uint16_t acc; /* sub-pixel progress, in 1/256 */
  uint8_t dir, st, fright, rev, dots;
} ghost_t;
static ghost_t G[4];
static struct {
  int16_t x, y;
  uint16_t acc;
  uint8_t dir, want, wait, stall, anim;
} P;
static uint8_t cell[31][28]; /* 1 a dot, 2 an energizer */
static uint8_t level, lives, phase, wave, combo, global_on, global_dots, extra, cheat, eaten_g, died;
static uint16_t phase_t, wave_t, fright_t, fruit_t, pop_t, idle_t, dots_left, ticks;
static uint32_t score, pop_v;
static int16_t pop_x, pop_y;
static color pop_c;

/* the options and the saved game (pacman.sav) */
static struct {
  uint8_t magic, version, speed, lives, buffer, start, bonus, cheat;
  uint32_t best;
  uint8_t has, level, glives, flags; /* a game to continue: flags 1 extra life given, 2 no collisions */
  uint32_t score;
  uint8_t dots[32]; /* the dots and energizers left, in reading order */
} S, saved;
#define SAVE_NAME "pacman.sav"
static const char *const speed_names[3] = {T("SLOW"), T("NORMAL"), T("FAST")};
static const uint8_t speed_mul[3] = {16, 20, 25};
static const uint8_t lives_opt[4] = {1, 2, 3, 5};
static const char *const buffer_names[5] = {T("STRICT"), T("SNAPPY"), T("NORMAL"), T("FORGIVING"), T("INFINITE")};
static const uint8_t buffer_ticks[5] = {0, 6, 15, 25, 255};
static const uint16_t bonus_at[4] = {10000, 15000, 20000, 0};

/* dots of a fresh maze: every corridor, except in the middle band and next
   to Pac-Man's start */
static int dot_at(int tx, int ty) {
  int c = tx < 14 ? tx : 27 - tx;
  if (solid(tx, ty) || !(((ty < 9 || ty > 19) && !(ty == 23 && c == 13)) || c == 6)) return 0;
  return c == 1 && (ty == 3 || ty == 23) ? 2 : 1;
}

/* ------------------------------------------------------------------ rules */
static int grp(void) { return level == 1 ? 0 : level < 5 ? 1 : level < 21 ? 2 : 3; }
/* speeds in % of the arcade's top speed: Pac-Man, Pac-Man frightening, ghosts, frightened, in the tunnel */
static const uint8_t speeds[4][5] = {
  {80, 90, 75, 50, 40}, {90, 95, 85, 55, 45}, {100, 100, 95, 60, 50}, {90, 90, 95, 60, 50}};
/* 100 % is 75.76 arcade pixels a second: 9.47 tiles, 1.105 of our pixels a tick */
static int per_tick(int pct) { return pct * 283 * speed_mul[S.speed] / 2000; }

static const uint8_t fright_secs[18] = {6, 5, 4, 3, 2, 5, 2, 2, 1, 5, 2, 1, 1, 3, 1, 1, 0, 1};
static int flash_ticks(void) {
  int l = level;
  return l == 9 || l == 12 || l == 13 || l == 15 || l == 16 || l == 18 ? 48 : 80;
}
/* scatter, chase, scatter... in ticks; the last chase lasts forever */
static const uint16_t waves[3][7] = {
  {420, 1200, 420, 1200, 300, 1200, 300}, {420, 1200, 420, 1200, 300, 61980, 1}, {300, 1200, 300, 1200, 300, 62220, 1}};
/* Cruise Elroy: Blinky speeds up when this few dots are left, and again at half */
static const uint8_t elroy_dots[19] = {20, 30, 40, 40, 40, 50, 50, 50, 60, 60, 60, 80, 80, 80, 100, 100, 100, 100, 120};
static int elroy(void) {
  if (died && G[3].st == G_HOUSE) return 0; /* after a death, not before Clyde is out */
  int n = elroy_dots[level < 19 ? level - 1 : 18];
  return dots_left <= n / 2 ? 2 : dots_left <= n;
}

#define MAX_SCORE 9999999 /* 7 digits fit the side panels */
static void add_score(uint32_t v) {
  uint32_t b = bonus_at[S.bonus];
  if (b && !extra && score + v >= b) extra = 1, lives += lives < 9;
  score = score + v < MAX_SCORE ? score + v : MAX_SCORE;
  if (!cheat && score > S.best) S.best = score;
}

static void wrap(int16_t *x) {
  if (*x < -14) *x += 224;
  else if (*x >= 210) *x -= 224;
}

/* the tile a ghost heads for */
static void target(int i, int *x, int *y) {
  static const int8_t corner[4][2] = {{25, -3}, {2, -3}, {27, 32}, {0, 32}};
  int px = tile(P.x), py = tile(P.y), d = P.dir, n = i == 1 ? 4 : 2;
  bool scatter = !(wave & 1) && !(i == 0 && elroy());
  if (G[i].st == G_EYES) {
    *x = 13, *y = 11;
    return;
  }
  *x = px, *y = py;
  if (i == 1 || i == 2) { /* ahead of Pac-Man (and left of it when he faces up, as in the arcade) */
    *x += n * DX[d] - (d == UP ? n : 0);
    *y += n * DY[d];
  }
  if (i == 2) { /* Inky: twice the way from Blinky to there */
    *x = 2 * *x - tile(G[0].x);
    *y = 2 * *y - tile(G[0].y);
  }
  if (i == 3) { /* Clyde: shy within 8 tiles */
    int dx = tile(G[3].x) - px, dy = tile(G[3].y) - py;
    if (dx * dx + dy * dy < 64) scatter = true;
  }
  if (scatter) *x = corner[i][0], *y = corner[i][1];
}

/* at the centre of a tile: the way closest to the target, never back */
static void decide(ghost_t *g, int i) {
  int tx = tile(g->x), ty = tile(g->y), gx, gy, best = -1, start = g->fright ? (int)(rnd() & 3) : 0;
  int32_t bd = 0x7FFFFFFF;
  target(i, &gx, &gy);
  for (int k = 0; k < 4; k++) {
    int d = (start + k) & 3, nx = tx + DX[d], ny = ty + DY[d];
    if (d == (g->dir ^ 2) || solid(nx, ny)) continue;
    if (g->fright) { /* frightened: a random way */
      best = d;
      break;
    }
    /* no turning up just above the house and above Pac-Man's start */
    if (d == UP && g->st == G_OUT && (ty == 11 || ty == 23) && (tx == 12 || tx == 15)) continue;
    int32_t dd = (nx - gx) * (nx - gx) + (ny - gy) * (ny - gy);
    if (dd < bd) bd = dd, best = d;
  }
  g->dir = (uint8_t)(best < 0 ? g->dir ^ 2 : best);
}

#define DOOR_X 97
#define OUT_Y 80
#define HOUSE_Y 101
static const int16_t home_x[4] = {97, 97, 83, 111};

static void ghost_px(ghost_t *g, int i) {
  int tx = tile(g->x), ty = tile(g->y);
  switch (g->st) {
    case G_HOUSE: /* bobbing */
      if (g->y <= HOUSE_Y - 3) g->dir = DOWN;
      else if (g->y >= HOUSE_Y + 3) g->dir = UP;
      break;
    case G_LEAVE:
      if (g->x != DOOR_X) {
        g->dir = g->x < DOOR_X ? RIGHT : LEFT;
      } else if (g->y > OUT_Y) {
        g->dir = UP;
      } else {
        g->st = G_OUT, g->dir = LEFT;
        return;
      }
      break;
    case G_ENTER: /* eyes: down through the door, then to their place */
      if (g->y < HOUSE_Y) g->dir = g->x != DOOR_X ? (g->x < DOOR_X ? RIGHT : LEFT) : DOWN;
      else if (g->x != home_x[i]) g->dir = g->x < home_x[i] ? RIGHT : LEFT;
      else {
        g->st = G_LEAVE;
        return;
      }
      break;
    default: {
      bool at = g->x == tx * 7 + 3 && g->y == ty * 7 + 3;
      if (g->st == G_EYES && g->y == OUT_Y && g->x >= 94 && g->x <= 101) {
        g->st = G_ENTER;
        return;
      }
      if (g->rev) {
        g->rev = 0, g->dir ^= 2;
        if (at && solid(tx + DX[g->dir], ty + DY[g->dir])) decide(g, i);
      } else if (at) {
        decide(g, i);
      }
    }
  }
  g->x += DX[g->dir], g->y += DY[g->dir];
  wrap(&g->x);
}

static int ghost_speed(int i) {
  const ghost_t *g = &G[i];
  const uint8_t *s = speeds[grp()];
  int tx = tile(g->x), ty = tile(g->y);
  if (g->st >= G_EYES) return per_tick(160);
  if (g->st != G_OUT) return per_tick(50);
  if (ty == 14 && (tx <= 5 || tx >= 22)) return per_tick(s[4]);
  if (g->fright) return per_tick(s[3]);
  return per_tick(s[2] + (i ? 0 : 5 * elroy()));
}

static void frighten(void) {
  combo = 0;
  fright_t = level <= 18 ? fright_secs[level - 1] * 60 : 0;
  for (int i = 0; i < 4; i++) {
    if (G[i].st == G_OUT) G[i].rev = 1;
    if (fright_t && G[i].st <= G_OUT) G[i].fright = 1;
  }
}

/* the first ghost still at home, or 4 */
static int in_house(void) {
  int i = 1;
  while (i < 4 && G[i].st != G_HOUSE) i++;
  return i;
}

static void eat(void) {
  int tx = tile(P.x), ty = tile(P.y), k;
  if ((unsigned)tx > 27 || !(k = cell[ty][tx])) return;
  cell[ty][tx] = 0;
  dots_left--, idle_t = 0;
  P.stall = k == 2 ? 3 : 1; /* Pac-Man stops a frame for each dot, three for an energizer */
  add_score(k == 2 ? 50 : 10);
  int h = in_house();
  if (global_on) global_dots++;
  else if (h < 4) G[h].dots++;
  if (k == 2) frighten();
  if (dots_left == 244 - 70 || dots_left == 244 - 170) fruit_t = (uint16_t)(560 + rnd() % 40);
  if (!dots_left) phase = PH_CLEAR, phase_t = 0;
}

static void pac_px(void) {
  int tx = tile(P.x), ty = tile(P.y), ox = P.x - tx * 7 - 3, oy = P.y - ty * 7 - 3, w = P.want, d;
  /* turn back at once; turn sideways anywhere in the tile (cornering) */
  if (w != NONE && w != P.dir && (w == (P.dir ^ 2) || !solid(tx + DX[w], ty + DY[w]))) P.dir = (uint8_t)w;
  d = P.dir;
  if (DX[d] * ox + DY[d] * oy >= 0 && solid(tx + DX[d], ty + DY[d])) return; /* against a wall */
  P.x += DX[d], P.y += DY[d];
  if (DX[d]) P.y -= (oy > 0) - (oy < 0); /* back onto the middle of the corridor */
  else P.x -= (ox > 0) - (ox < 0);
  wrap(&P.x);
  P.anim++;
  eat();
}

static void reset_actors(void) {
  static const int16_t gx[4] = {97, 97, 83, 111};
  for (int i = 0; i < 4; i++) {
    ghost_t *g = &G[i];
    g->x = gx[i], g->y = i ? HOUSE_Y : OUT_Y, g->acc = 0;
    g->st = i ? G_HOUSE : G_OUT, g->dir = i == 0 ? LEFT : i == 1 ? DOWN : UP;
    g->fright = g->rev = 0;
  }
  P.x = 97, P.y = 23 * 7 + 3, P.dir = LEFT, P.want = NONE, P.wait = 0, P.acc = 0, P.stall = 0, P.anim = 0;
  wave = 0, wave_t = 0, fright_t = 0, fruit_t = 0, pop_t = 0, idle_t = 0;
}

static void new_level(void) {
  dots_left = 0;
  for (int ty = 0; ty < 31; ty++)
    for (int tx = 0; tx < 28; tx++) dots_left += (cell[ty][tx] = (uint8_t)dot_at(tx, ty)) != 0;
  for (int i = 0; i < 4; i++) G[i].dots = 0;
  global_on = global_dots = 0, died = 0;
  reset_actors();
}

static void play_tick(void) {
  if (fright_t) {
    if (!--fright_t)
      for (int i = 0; i < 4; i++) G[i].fright = 0;
  } else if (wave < 7 && ++wave_t >= waves[level == 1 ? 0 : level < 5 ? 1 : 2][wave]) {
    wave++, wave_t = 0; /* scatter <-> chase: the ghosts turn around */
    for (int i = 0; i < 4; i++) G[i].rev |= G[i].st == G_OUT;
  }
  if (fruit_t) fruit_t--;
  if (pop_t) pop_t--;
  /* the ghost house: the first ghost at home leaves when its dot counter (or,
     after a death, the shared one) says so, or when no dot was eaten for a while */
  int h = in_house();
  if (h < 4) {
    bool go;
    if (global_on) {
      go = global_dots >= (h == 1 ? 7 : h == 2 ? 17 : 32);
      if (go && h == 3) global_on = 0;
    } else {
      go = G[h].dots >= (level == 1 ? (h == 2 ? 30 : h == 3 ? 60 : 0) : level == 2 && h == 3 ? 50 : 0);
    }
    if (++idle_t >= (level < 5 ? 240 : 180)) idle_t = 0, go = true;
    if (go) G[h].st = G_LEAVE;
  }
  if (P.stall) {
    P.stall--;
  } else {
    P.acc += per_tick(speeds[grp()][fright_t ? 1 : 0]);
    for (; P.acc >= 256; P.acc -= 256) {
      pac_px();
      if (phase != PH_PLAY) return;
    }
  }
  if (fruit_t && P.y == 17 * 7 + 3 && P.x >= 94 && P.x <= 101) {
    fruit_t = 0;
    add_score(pop_v = fruit_pts[fruit_of(level)]);
    pop_x = 98, pop_y = 122, pop_c = PINK, pop_t = 120;
  }
  for (int i = 0; i < 4; i++)
    for (G[i].acc += ghost_speed(i); G[i].acc >= 256; G[i].acc -= 256) ghost_px(&G[i], i);
  for (int i = 0; i < 4; i++) {
    ghost_t *g = &G[i];
    int dx = g->x - P.x, dy = g->y - P.y;
    dx += dx < -112 ? 224 : dx > 112 ? -224 : 0; /* the tunnel's two ends are neighbours */
    if (g->st != G_OUT || dx < -4 || dx > 4 || dy < -4 || dy > 4) continue;
    if (g->fright) { /* eaten: 200, 400, 800, 1600, and a pause to see it */
      g->fright = 0, g->st = G_EYES;
      add_score(pop_v = 200u << combo);
      combo += combo < 3;
      pop_x = g->x, pop_y = g->y, pop_c = CYAN, pop_t = 60;
      eaten_g = (uint8_t)i, phase = PH_EAT, phase_t = 0;
      return;
    }
    if (!cheat) {
      phase = PH_DIE, phase_t = 0;
      return;
    }
  }
}

/* ------------------------------------------------------------------ the game screen */
static bool paused;
static const char *const *menu_items;
static const char *menu_title;
static int menu_n, menu_sel;
static bool ready_shown(void) { return phase <= PH_READY; }
static bool ghosts_shown(void) {
  return phase != PH_START && phase < PH_OVER && !((phase == PH_DIE || phase == PH_CLEAR) && phase_t >= 60);
}
static bool pac_shown(void) {
  return phase != PH_START && phase != PH_EAT && phase < PH_OVER && !(phase == PH_DIE && phase_t >= 150);
}
static int lives_shown(void) { return lives - (phase != PH_START); }

/* where sprite i is drawn (the ghosts, Pac-Man, the fruit, points); w = 0 when hidden */
static void obj_rect(int i, int16_t *r) {
  int x = 98, y = 122, w = 12, h = 12;
  bool on = fruit_t && phase <= PH_DIE && (phase != PH_DIE || phase_t < 60);
  if (i < 4) x = G[i].x, y = G[i].y, w = h = 13, on = ghosts_shown() && !(phase == PH_EAT && i == eaten_g);
  if (i == 4) x = P.x, y = P.y, w = h = 13, on = pac_shown();
  if (i == 6) x = pop_x, y = pop_y, w = 17, h = 7, on = pop_t;
  r[0] = (int16_t)(MX + x - w / 2), r[1] = (int16_t)(MY + y - h / 2), r[2] = (int16_t)(on ? w : 0), r[3] = (int16_t)h;
}

static color wallc;
static bool energizers_on;

static void maze_draw(void) {
  int x0 = cx0 > MX ? cx0 : MX, x1 = cx1 < MX + MW ? cx1 : MX + MW;
  int y0 = cy0 > MY ? cy0 : MY, y1 = cy1 < MY + MH ? cy1 : MY + MH;
  for (int y = y0; y < y1; y++) {
    int my = y - MY, ty = my / 7, py = my - ty * 7, mx = x0 - MX, tx = mx / 7, px = mx - tx * 7;
    color *p = buf + (y - by) * bw + x0 - bx;
    for (int x = x0; x < x1; x++, p++) {
      color c = BLACK;
      int k = cell[ty][tx];
      if (wpix[ty][tx] >> (py * 7 + px) & 1) {
        c = ty == 12 && (tx == 13 || tx == 14) ? PINK : wallc;
      } else if (k == 1) { /* a dot, mirrored in the right half */
        if ((py == 3 || py == 4) && (px == 3 || px == (tx < 14 ? 4 : 2))) c = PEACH;
      } else if (k == 2 && energizers_on) {
        int a = px - 3, b = py - 3;
        if (a * a + b * b <= 10) c = PEACH;
      }
      *p = c;
      if (++px == 7) px = 0, tx++;
    }
  }
}

/* The intermissions, as in the arcade. 0: Blinky chases Pac-Man away, and a
   giant Pac-Man comes back after him. 1: Blinky's cloak catches on a nail and
   tears. 2: Blinky, his cloak patched, chases Pac-Man, then runs back
   without it, dragging it along. */
#define CUT_Y (MY + 108)
#define NAIL_X (MX + 98)
static uint8_t cut;
static void cut_draw(void) {
  int t = phase_t, f = t >> 1 & 3, fr = t >> 3 & 1, x = MX + MW + 10 - t * 6 / 5, g = x + 28 - t / 10;
  f = f == 3 ? 2 : f * 2;
  if (cut == 1) {
    int s = t - 114; /* ticks since the cloak caught */
    fill(NAIL_X, CUT_Y + 3, 1, 4, PEACH);
    fill(NAIL_X - 1, CUT_Y + 7, 3, 1, PEACH);
    pac_draw(x, CUT_Y, LEFT, f, 0);
    g = MX + MW + 38 - t * 5 / 4;
    if (s > 0) g = NAIL_X - 6 - (s < 64 ? s : 64) / 8; /* straining: the cloak stretches */
    if (s > 0 && s < 64) fill(g + 6, CUT_Y + 4, NAIL_X - g - 6, 3, RED);
    ghost_draw(g, CUT_Y, RED, s < 64 ? LEFT : s < 150 ? RIGHT : DOWN, LOOK_NORMAL, s > 0 || fr);
    if (s >= 64) { /* torn: a rag on the nail, a leg showing */
      fill(g + 2, CUT_Y + 3, 5, 4, BLACK);
      fill(g + 3, CUT_Y + 3, 2, 4, PEACH);
      fill(NAIL_X - 2, CUT_Y + 4, 2, 3, RED);
    }
  } else if (t < 200) {
    pac_draw(x, CUT_Y, LEFT, f, 0);
    ghost_draw(g, CUT_Y, RED, LEFT, LOOK_NORMAL, fr);
    if (cut) fill(g + 2, CUT_Y + 1, 3, 3, PEACH); /* the patch */
  } else if (t >= 240) {
    x = MX - 10 + (t - 240);
    if (cut) { /* no cloak: a little red body on two legs, the cloak dragged behind */
      fill(x - 4, CUT_Y - 1, 9, 5, RED);
      fill(x - 3 + fr, CUT_Y + 4, 1, 2, RED);
      fill(x + 2 - fr, CUT_Y + 4, 1, 2, RED);
      fill(x - 10, CUT_Y + 2, 6, 1, RED);
      for (int r = 0; r < 4; r++)
        for (int c = 0, b = r < 2 ? 0x1FFF : ghost_skirt[fr * 2 + r - 2]; b; c++, b >>= 1)
          if (b & 1) pset(x - 23 + c, CUT_Y + 2 + r, RED);
      ghost_draw(x, CUT_Y + 1, RED, RIGHT, LOOK_EYES, 0);
    } else {
      ghost_draw(x, CUT_Y, RED, RIGHT, LOOK_BLUE, fr);
      pac_r(x - 44 + (t - 240) / 12, CUT_Y, RIGHT, f, 0, 13);
    }
  }
}

static void box_draw(void) {
  int h = 36 + menu_n * 20, y = MY + MH / 2 - h / 2;
  fill(70, y, 180, h, BLACK);
  frame(70, y, 180, h, WALL);
  frame(73, y + 3, 174, h - 6, WALL);
  ctext(menu_title, 160, y + 11, 2, YELLOW);
  for (int i = 0; i < menu_n; i++) {
    int iy = y + 34 + i * 20;
    text(menu_items[i], 110, iy, 2, i == menu_sel ? WHITE : GREY);
    if (i == menu_sel) pac_draw(96, iy + 7, RIGHT, (ticks >> 2 & 3) == 3 ? 2 : (ticks >> 2 & 3) * 2, 0);
  }
}

static void game_scene(void) {
  fill(cx0, cy0, cx1 - cx0, cy1 - cy0, BLACK);
  if (cx0 < MX || cx1 > MX + MW) { /* the sides: scores, lives, fruit (the latest at the right) */
    if (phase != PH_PLAY || ticks & 16) text("1UP", 21, 12, 1, WHITE);
    rtext(score ? num(score) : "00", 48, 21, 1, WHITE);
    ctext(T("HIGH"), 289, 12, 1, WHITE);
    ctext(T("SCORE"), 289, 21, 1, WHITE);
    if (S.best) rtext(num(S.best), 306, 30, 1, WHITE);
    if (cheat) ctext(T("CHEAT"), 289, 46, 1, RED);
    for (int i = 0, n = lives_shown(); i < n && i < 8; i++) pac_draw(10 + i % 4 * 14, 222 - i / 4 * 14, LEFT, 3, 0);
    for (int i = 0, l = level; l > 0 && i < 7; i++, l--) fruit_draw(303 - i % 4 * 14, 216 - i / 4 * 14, fruit_of(l));
  }
  int s0 = cx0, s1 = cx1; /* sprites stay in the maze (the tunnel) */
  if (cx0 < MX) cx0 = MX;
  if (cx1 > MX + MW) cx1 = MX + MW;
  if (phase == PH_CUT) cut_draw();
  else maze_draw();
  int16_t r[4];
  obj_rect(5, r);
  if (r[2]) fruit_draw(MX + 92, MY + 116, fruit_of(level));
  obj_rect(4, r);
  if (r[2]) {
    int f = P.anim >> 1 & 3;
    if (phase == PH_DIE && phase_t >= 60) {
      int t = phase_t - 60;
      if (t < 80) pac_draw(MX + P.x, MY + P.y, UP, 0, t * 2 / 5 + 1);
      else /* a little flash where he was */
        for (int k = 2; k < 5; k++) {
          int x = MX + P.x, y = MY + P.y;
          pset(x + k + 1, y, YELLOW), pset(x - k - 1, y, YELLOW), pset(x, y + k + 1, YELLOW), pset(x, y - k - 1, YELLOW);
          pset(x + k, y + k, YELLOW), pset(x - k, y + k, YELLOW), pset(x + k, y - k, YELLOW), pset(x - k, y - k, YELLOW);
        }
    } else {
      pac_draw(MX + P.x, MY + P.y, P.dir, phase < PH_PLAY || phase == PH_CLEAR ? 0 : f == 3 ? 2 : f * 2, 0);
    }
  }
  for (int i = 3; i >= 0; i--) {
    obj_rect(i, r);
    if (!r[2]) continue;
    ghost_t *g = &G[i];
    int look = g->st >= G_EYES ? LOOK_EYES : !g->fright ? LOOK_NORMAL
             : fright_t <= flash_ticks() && fright_t & 8 ? LOOK_FLASH : LOOK_BLUE;
    ghost_draw(MX + g->x, MY + g->y, ghost_col[i], g->dir, look, ticks >> 3 & 1);
  }
  if (pop_t) mini(pop_v, MX + pop_x, MY + pop_y - 2, pop_c);
  cx0 = s0, cx1 = s1;
  if (ready_shown()) ctext(T("READY!"), MX + 98, MY + 119, 1, YELLOW);
  if (phase == PH_START) ctext(T("PLAYER ONE"), MX + 98, MY + 77, 1, CYAN);
  if (phase == PH_OVER) ctext(T("GAME  OVER"), MX + 98, MY + 119, 1, RED);
  if (paused) box_draw();
}

/* ------------------------------------------------------------------ keys and frames */
static uint64_t held, hit, fresh; /* fresh: pressed since last time, without repeats */
static uint64_t stale;            /* held into this screen: ignored until released */
static bool quit;                 /* Home or On/Off: leave the app */
static uint32_t frame_t, repeat_at;
static const uint64_t K_UP = KEY(eadk_key_up) | KEY(eadk_key_eight), K_DOWN = KEY(eadk_key_down) | KEY(eadk_key_two);
static const uint64_t K_LEFT = KEY(eadk_key_left) | KEY(eadk_key_four), K_RIGHT = KEY(eadk_key_right) | KEY(eadk_key_six);
static const uint64_t K_OK = KEY(eadk_key_ok) | KEY(eadk_key_exe), K_BACK = KEY(eadk_key_back);

/* reads the keyboard: `hit` has the keys pressed since last time, arrows repeating */
static void keys(void) {
  uint64_t k = eadk_keyboard_scan(), arrows = K_UP | K_DOWN | K_LEFT | K_RIGHT;
  uint32_t now = eadk_timing_millis();
  if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) quit = true;
  k &= ~(stale &= k);
  hit = fresh = k & ~held;
  if (hit & arrows) repeat_at = now + 350;
  else if ((k & arrows) && (int32_t)(now - repeat_at) >= 0) hit |= k & arrows, repeat_at = now + 90;
  held = k;
}
/* a new screen: the keys down now count only once pressed again */
static void settle(void) { stale = eadk_keyboard_scan(); }
/* ends a frame: at most 60 a second */
static void frame_end(void) {
  uint32_t spent = (uint32_t)eadk_timing_millis() - frame_t;
  if (spent < 16) eadk_timing_msleep(16 - spent);
  frame_t = eadk_timing_millis();
}

/* ------------------------------------------------------------------ saving */
static void save(void) {
  const uint8_t *a = (const uint8_t *)&S;
  uint8_t *b = (uint8_t *)&saved;
  for (unsigned i = 0; i < sizeof S; i++)
    if (a[i] != b[i]) { /* changed: write it (and try again next time if that failed) */
      if (ef_write(SAVE_NAME, &S, sizeof S))
        for (i = 0; i < sizeof S; i++) b[i] = a[i];
      return;
    }
}
/* the game in progress, to continue later */
static void keep_game(void) {
  int n = 0, left = lives - (phase == PH_DIE), next = !dots_left && level < 255; /* cleared: the next level */
  S.has = phase != PH_OVER && left > 0;
  S.level = (uint8_t)(level + next), S.glives = (uint8_t)left, S.flags = (uint8_t)(extra | cheat << 1), S.score = score;
  for (int i = 0; i < 32; i++) S.dots[i] = 0;
  for (int ty = 0; ty < 31; ty++)
    for (int tx = 0; tx < 28; tx++)
      if (dot_at(tx, ty)) S.dots[n >> 3] |= (uint8_t)((next || cell[ty][tx]) << (n & 7)), n++;
}
static void load(void) {
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  uint8_t *s = (uint8_t *)&S;
  if (d && n == sizeof S && d[0] == 'P' && d[1] == 1)
    for (unsigned i = 0; i < sizeof S; i++) s[i] = d[i];
  else
    for (unsigned i = 0; i < sizeof S; i++) s[i] = 0;
  /* anything out of range gets its default */
  if (S.magic != 'P') S.speed = 1, S.lives = 2, S.buffer = 2;
  S.magic = 'P', S.version = 1;
  if (S.speed > 2) S.speed = 1;
  if (S.lives > 3) S.lives = 2;
  if (S.buffer > 4) S.buffer = 2;
  if (S.start < 1 || S.start > 21) S.start = 1;
  if (S.bonus > 3) S.bonus = 0;
  if (S.cheat > 1) S.cheat = 0;
  if (S.best > MAX_SCORE) S.best = 0;
  int left = 0;
  for (int i = 0; i < 244; i++) left += S.dots[i >> 3] >> (i & 7) & 1;
  if (S.has != 1 || !S.level || !S.glives || S.glives > 9 || S.flags > 3 || !left || S.score > MAX_SCORE) S.has = 0;
  for (unsigned i = 0; i < sizeof S; i++) ((uint8_t *)&saved)[i] = s[i];
}

/* ------------------------------------------------------------------ menus */
static int menu_box(const char *title, const char *const *items, int n, int sel) {
  int h = 36 + n * 20, y = MY + MH / 2 - h / 2;
  menu_title = title, menu_items = items, menu_n = n, menu_sel = sel, paused = true;
  settle();
  refresh(70, MY + MH / 2 - 48, 180, 96); /* also clears a bigger box shown before */
  for (;;) {
    keys();
    if (quit) break;
    if (hit & (K_UP | K_DOWN)) menu_sel = (menu_sel + (hit & K_UP ? n - 1 : 1)) % n;
    if (hit & (K_OK | K_BACK)) break;
    ticks++;
    eadk_display_wait_for_vblank();
    refresh(70, y + 30, 180, n * 20);
    frame_end();
  }
  paused = false;
  return hit & K_OK ? menu_sel : -1;
}

/* ------------------------------------------------------------------ playing */
enum { END_TITLE, END_RESTART };

/* one tick of the game (1/60 s); true when it is over */
static bool redraw_all;
static bool step(void) {
  ticks++, phase_t++;
  switch (phase) {
    case PH_START:
    case PH_READY:
      if (phase_t >= 120) phase++, phase_t = 0;
      break;
    case PH_PLAY: play_tick(); break;
    case PH_EAT:
      if (phase_t >= 60) phase = PH_PLAY, pop_t = 0;
      break;
    case PH_DIE:
      if (phase_t >= 190) {
        phase_t = 0;
        if (!--lives) {
          phase = PH_OVER;
          break;
        }
        reset_actors();
        died = 1, global_on = 1, global_dots = 0;
        phase = PH_READY;
      }
      break;
    case PH_CLEAR:
      if (phase_t >= 60 + 8 * 12) {
        int l = level; /* intermissions after levels 2, 5, 9, 13 and 17, like the arcade */
        if (level < 255) level++;
        new_level();
        phase = l == 2 || l == 5 || l == 9 || l == 13 || l == 17 ? PH_CUT : PH_READY;
        cut = l == 2 ? 0 : l == 5 ? 1 : 2;
        phase_t = 0, redraw_all = true;
      }
      break;
    case PH_CUT:
      if (phase_t >= (cut == 1 ? 360 : 500) || (fresh & K_OK)) phase = PH_READY, phase_t = 0, redraw_all = true;
      break;
    case PH_OVER: return phase_t >= 240 || (hit & (K_OK | K_BACK));
  }
  return false;
}

static const char *const yes_no[2] = {T("NO"), T("YES")};
static int play(void) {
  static const char *const pause_items[3] = {T("RESUME"), T("RESTART"), T("QUIT GAME")};
  static int16_t orect[7][4];
  static uint32_t sig[8];
  uint32_t last = eadk_timing_millis(), acc = 0;
  redraw_all = true;
  scene = game_scene;
  for (;;) {
    keys();
    if (quit) {
      keep_game();
      return END_TITLE;
    }
    if ((hit & K_BACK) && phase != PH_OVER) {
      int c = 0;
      do /* "Quit game?": No goes back to the pause menu */
        c = menu_box(T("PAUSED"), pause_items, 3, c);
      while (c == 2 && !quit && menu_box(T("QUIT GAME?"), yes_no, 2, 0) != 1);
      if (quit || c == 2) { /* Quit game: back to NumPlay (or the calculator) */
        keep_game();
        quit = true;
        return END_TITLE;
      }
      if (c == 1) return END_RESTART;
      redraw_all = true, last = eadk_timing_millis(), acc = 0;
      held = eadk_keyboard_scan(), stale = 0; /* an arrow still held steers */
    }
    /* the wanted direction: the arrow pressed last, while it is held and for
       the input buffer's time after (even if another arrow is still held) */
    int d = NONE;
    static const uint64_t dk[4] = {KEY(eadk_key_up) | KEY(eadk_key_eight), KEY(eadk_key_left) | KEY(eadk_key_four),
                                   KEY(eadk_key_down) | KEY(eadk_key_two), KEY(eadk_key_right) | KEY(eadk_key_six)};
    for (int i = 0; i < 4; i++)
      if (fresh & dk[i]) d = i;
    if (d == NONE && P.want != NONE && (held & dk[P.want])) d = P.want;
    if (d == NONE && P.wait != 255 && !(P.wait && --P.wait))
      for (int i = 0; i < 4 && d == NONE; i++) /* the turn is forgotten: back to an arrow still held */
        if (held & dk[i]) d = i;
    if (d != NONE) P.want = (uint8_t)d, P.wait = buffer_ticks[S.buffer];
    else if (P.wait != 255 && !P.wait) P.want = NONE;

    /* the rules run 60 times a second: one tick a frame while the frames come
       at about that rate (the arcade too runs at its screen's rate), else as
       many as the time says, 4 at most */
    uint32_t now = eadk_timing_millis(), dt = now - last, todo;
    last = now;
    if (dt >= 15 && dt <= 18) todo = 1, acc = 0;
    else acc += dt * 3, todo = acc / 50, acc %= 50;
    for (todo = todo < 4 ? todo : 4; todo; todo--)
      if (step()) {
        keep_game();
        return END_TITLE;
      }

    /* what the screen shows, to redraw only what changed */
    energizers_on = phase < PH_PLAY || phase == PH_CLEAR || ticks & 8;
    wallc = phase == PH_CLEAR && phase_t >= 60 && (phase_t - 60) / 12 % 2 ? WHITE : WALL;
    uint32_t s[8] = {score, S.best, (uint32_t)lives_shown(), phase == PH_PLAY && !(ticks & 16), energizers_on, wallc,
                     (uint32_t)(ready_shown() | (phase == PH_START) << 1 | (phase == PH_OVER) << 2), level};
    eadk_display_wait_for_vblank();
    bool full = redraw_all || s[7] != sig[7];
    if (full) {
      refresh(0, 0, 320, 240);
      redraw_all = false;
    } else {
      if (s[0] != sig[0]) refresh(0, 21, 62, 7);
      if (s[1] != sig[1]) refresh(258, 30, 62, 7);
      if (s[2] != sig[2]) refresh(0, 200, 62, 30);
      if (s[3] != sig[3]) refresh(0, 12, 62, 7);
      if (s[4] != sig[4])
        for (int i = 0; i < 4; i++) refresh(MX + (i & 1 ? 26 : 1) * 7, MY + (i & 2 ? 23 : 3) * 7, 7, 7);
      if (s[5] != sig[5]) refresh(MX, MY, MW, MH);
      if (s[6] != sig[6]) /* (wider and taller for translations: their accents, Chinese) */
        refresh(MX + 60 - 20 * NP_TEXT_EXTRA, MY + 77 - 3 * NP_TEXT_EXTRA, 76 + 40 * NP_TEXT_EXTRA, 49 + 6 * NP_TEXT_EXTRA);
      if (phase == PH_CUT) refresh(MX, CUT_Y - 14, MW, 28);
    }
    for (int i = 0; i < 8; i++) sig[i] = s[i];
    for (int i = 0; i < 7; i++) { /* the sprites: where they were and where they are */
      int16_t n[4], *o = orect[i];
      obj_rect(i, n);
      int dx = n[0] - o[0], dy = n[1] - o[1];
      if (full) {
      } else if (n[2] && o[2] && dx > -20 && dx < 20 && dy > -20 && dy < 20) {
        int x = dx < 0 ? n[0] : o[0], y = dy < 0 ? n[1] : o[1];
        refresh(x, y, (dx < 0 ? -dx : dx) + (n[2] > o[2] ? n[2] : o[2]), (dy < 0 ? -dy : dy) + n[3]);
      } else {
        if (o[2]) refresh(o[0], o[1], o[2], o[3]);
        if (n[2]) refresh(n[0], n[1], n[2], n[3]);
      }
      for (int k = 0; k < 4; k++) o[k] = n[k];
    }
    frame_end();
  }
}

static int game(bool resume) {
  int r;
  do {
    level = S.start, lives = lives_opt[S.lives], score = 0, extra = 0, cheat = S.cheat, phase = PH_START;
    if (resume) /* the saved game: its level, score, lives and dots */
      level = S.level, lives = S.glives, score = S.score, extra = S.flags & 1, cheat = S.flags >> 1, phase = PH_READY;
    new_level();
    if (resume) {
      dots_left = 0;
      for (int ty = 0, n = 0; ty < 31; ty++)
        for (int tx = 0; tx < 28; tx++)
          if (cell[ty][tx]) {
            if (!(S.dots[n >> 3] >> (n & 7) & 1)) cell[ty][tx] = 0;
            dots_left += cell[ty][tx] != 0, n++;
          }
    }
    S.has = 0;
    phase_t = 0, resume = false;
    r = play();
    save();
  } while (r == END_RESTART && !quit);
  return r;
}

/* ------------------------------------------------------------------ title and options */
static int sel, osel;
static uint16_t tt; /* the title's clock, in ticks */

static const char *const title_items[3] = {T("PLAY"), T("CONTINUE"), T("OPTIONS")};

/* the attract chase: Pac-Man runs from the ghosts, eats the energizer and
   turns the tables; each ghost he catches stops the chase for a moment, its
   points in its place */
#define LANE_Y 197
static void lane_draw(void) {
  int t = tt % 640, px, gx0, eat = -1;
  bool back = t >= 250;
  if (!back) {
    px = 335 - t * 6 / 5, gx0 = px + 24;
  } else { /* he gains a pixel a tick: ghost i is caught 24 + 16 i ticks in */
    int d = t - 250;
    for (int i = 0; i < 4; i++) {
      int c = 24 + 16 * i;
      if (d < c) break;
      if (d < c + 40) {
        eat = i, d = c;
        break;
      }
      d -= 40;
    }
    px = 35 + d * 3 / 2, gx0 = 59 + d / 2;
  }
  for (int x = 45; x < 310; x += 10)
    if (!back && x < px - 5) fill(x, LANE_Y - 1, 2, 2, PEACH);
  if (!back && tt & 8) {
    fill(33, LANE_Y - 3, 5, 7, PEACH);
    fill(32, LANE_Y - 2, 7, 5, PEACH);
  }
  for (int i = 0; i < 4; i++) {
    int gx = gx0 + 16 * i;
    if (i == eat) mini(200u << i, gx, LANE_Y - 2, CYAN);
    else if (!back || px < gx)
      ghost_draw(gx, LANE_Y, ghost_col[i], back ? RIGHT : LEFT, back ? LOOK_BLUE : LOOK_NORMAL, tt >> 3 & 1);
  }
  int f = tt >> 1 & 3;
  if (eat < 0) pac_draw(px, LANE_Y, back ? RIGHT : LEFT, f == 3 ? 2 : f * 2, 0);
}

/* The logo: fat letters from the font (5-pixel blocks, rounded where two
   sides are open), Pac-Man as the C, chomping, all ringed in orange. */
#define LOGO_X 45
#define LOGO_Y 10
static bool logo_blk(const uint8_t *g, int w, int bx, int by) {
  if (bx < 0 || bx >= w || by < 0 || by > 6) return false;
  return ((bx < 5 ? g[bx] : 0) | (bx ? g[bx - 1] : 0)) >> by & 1;
}
static bool logo_in(int x, int y) {
  static const uint8_t at[6] = {0, 35, 110, 130, 165, 200};
  x -= LOGO_X, y -= LOGO_Y;
  if (y < 0 || y >= 35) return false;
  int dx = x - 87, dy = y - 17, f = tt >> 3 & 3, open = f == 3 ? 2 : f * 2;
  if (dx * dx + dy * dy <= 17 * 18) return !(dx > 0 && 4 * (dy < 0 ? -dy : dy) < open * dx);
  for (int i = 0; i < 6; i++) {
    int u = x - at[i], w = i == 2 ? 3 : 6; /* the hyphen is short */
    if (u < 0 || u >= w * 5) continue;
    const uint8_t *g = font["PA-MAN"[i] - 32];
    int bx = u / 5, by = y / 5, px = u % 5 - 2, py = y % 5 - 2;
    if (!logo_blk(g, w, bx, by)) return false;
    /* round the outer corners: a quarter circle as big as the block, around its far corner */
    for (int q = 0; q < 4; q++) {
      int qx = q & 1 ? 1 : -1, qy = q & 2 ? 1 : -1, ax = 2 * px + 5 * qx, ay = 2 * py + 5 * qy;
      if (px * qx >= 0 && py * qy >= 0 && !logo_blk(g, w, bx + qx, by) && !logo_blk(g, w, bx, by + qy) &&
          ax * ax + ay * ay > 100)
        return false;
    }
    return true;
  }
  return false;
}
static void logo_draw(void) {
  static const int8_t ring[12][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}, {2, 0}, {-2, 0},
                                     {0, 2}, {0, -2}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
  if (!hits(LOGO_X - 2, LOGO_Y - 2, 236, 39)) return;
  for (int y = LOGO_Y - 2; y < LOGO_Y + 37; y++)
    for (int x = LOGO_X - 2; x < LOGO_X + 234; x++) {
      if (x < cx0 || x >= cx1 || y < cy0 || y >= cy1) continue;
      color c = YELLOW;
      if (!logo_in(x, y)) {
        int k = 0;
        while (k < 12 && !logo_in(x + ring[k][0], y + ring[k][1])) k++;
        if (k == 12) continue;
        c = RGB(0xDE5A00);
      }
      buf[(y - by) * bw + x - bx] = c;
    }
}

static void title_scene(void) {
  static const char *const names[4] = {T("-SHADOW    \"BLINKY\""), T("-SPEEDY    \"PINKY\""), T("-BASHFUL   \"INKY\""),
                                       T("-POKEY     \"CLYDE\"")};
  fill(cx0, cy0, cx1 - cx0, cy1 - cy0, BLACK);
  logo_draw();
  ctext(T("CHARACTER / NICKNAME"), 160, 58, 1, WHITE);
  for (int i = 0; i < 4; i++) {
    ghost_draw(88, 76 + i * 14, ghost_col[i], RIGHT, LOOK_NORMAL, 0);
    text(names[i], 102, 73 + i * 14, 1, ghost_col[i]);
  }
  for (int i = 0; i < 3; i++) {
    bool off = i == 1 && !S.has;
    text(title_items[i], 118, 132 + i * 18, 2, off ? RGB(0x3A3A4A) : i == sel ? WHITE : GREY);
    if (i == sel) pac_draw(104, 139 + i * 18, RIGHT, (tt >> 2 & 3) == 3 ? 2 : (tt >> 2 & 3) * 2, 0);
  }
  lane_draw();
  if (S.best) { /* HIGH SCORE and the score, centred together */
    char *n = num(S.best);
    int w = NP_TEXT_EXTRA ? tw(T("HIGH SCORE"), 1) + 8 : 77, x = 160 - (w + slen(n) * 7 - 1) / 2;
    text(T("HIGH SCORE"), x, 214, 1, WHITE);
    text(n, x + w, 214, 1, WHITE);
  }
  ctext(T("BASED ON TATONE26'S VERSION"), 160, 228, 1, GREY);
  if (paused) box_draw(); /* "Quit game?" */
}

static const char *const opt_names[6] = {T("GAME SPEED"), T("LIVES"), T("BUFFER"), T("START LEVEL"), T("BONUS LIFE"),
                                         T("NO COLLISIONS")};
static const char *opt_value(int i) {
  switch (i) {
    case 0: return speed_names[S.speed];
    case 1: return num(lives_opt[S.lives]);
    case 2: return buffer_names[S.buffer];
    case 3: return num(S.start);
    case 4: return S.bonus == 3 ? T("NONE") : num(bonus_at[S.bonus]);
    default: return S.cheat ? T("ON") : T("OFF");
  }
}
static void options_scene(void) {
  fill(cx0, cy0, cx1 - cx0, cy1 - cy0, BLACK);
  ctext(T("OPTIONS"), 160, 10, 2, YELLOW);
  for (int i = 0; i < 6; i++) {
    int y = 38 + i * 26;
    bool on = i == osel;
    const char *v = opt_value(i);
    color c = i == 5 && S.cheat ? RED : on ? YELLOW : CYAN;
    if (on) {
      pac_draw(13, y + 7, RIGHT, (tt >> 2 & 3) == 3 ? 2 : (tt >> 2 & 3) * 2, 0);
      text("<", 292 - tw(v, 2) - 18, y, 2, c);
      text(">", 300, y, 2, c);
    }
    text(opt_names[i], 26, y, 2, on ? WHITE : GREY);
    rtext(v, 292, y, 2, c);
    if (i == 3) fruit_draw(292 - tw(v, 2) - (on ? 34 : 16), y + 1, fruit_of(S.start));
  }
  static const char *const help[6] = {T("HOW FAST EVERYTHING MOVES"), T("PAC-MEN TO START WITH"),
                                      T("HOW LONG A TURN WAITS FOR A GAP"), T("LATER LEVELS ARE FASTER"),
                                      T("THE SCORE FOR AN EXTRA PAC-MAN"), T("GHOSTS CANNOT CATCH YOU: NO HIGH SCORE")};
  ctext(help[osel], 160, 200, 1, osel == 5 ? RED : WHITE);
  ctext(T("LEFT/RIGHT: CHANGE    BACK: DONE"), 160, 222, 1, GREY);
}

static void options(void) {
  static const uint8_t count[6] = {3, 4, 5, 21, 4, 2};
  osel = 0;
  scene = options_scene;
  settle();
  refresh(0, 0, 320, 240);
  for (;;) {
    keys();
    if (quit || (hit & K_BACK)) break;
    int was = osel;
    if (hit & (K_UP | K_DOWN)) osel = (osel + (hit & K_UP ? 5 : 1)) % 6;
    int lr = (hit & K_RIGHT || hit & K_OK ? 1 : 0) - (hit & K_LEFT ? 1 : 0);
    if (lr) {
      uint8_t *v = osel == 0 ? &S.speed : osel == 1 ? &S.lives : osel == 2 ? &S.buffer : osel == 3 ? &S.start
                 : osel == 4 ? &S.bonus : &S.cheat;
      int base = osel == 3, n = count[osel];
      *v = (uint8_t)((*v - base + lr + n) % n + base);
    }
    tt++;
    eadk_display_wait_for_vblank();
    if (was != osel) /* (a bit higher for translations: their accents) */
      refresh(0, 36 - 4 * NP_TEXT_EXTRA + was * 26, 320, 18 + 4 * NP_TEXT_EXTRA),
          refresh(0, 198 - 2 * NP_TEXT_EXTRA, 320, 11 + 4 * NP_TEXT_EXTRA); /* the help line */
    if (was != osel || lr) refresh(0, 36 - 4 * NP_TEXT_EXTRA + osel * 26, 320, 18 + 4 * NP_TEXT_EXTRA);
    else refresh(6, 39 + osel * 26, 14, 14); /* the cursor's mouth */
    frame_end();
  }
  save();
}

int main(void) {
  np_app_begin();
  quit = false, paused = false, tt = 0, ticks = 0;
  seed ^= eadk_random() | 1;
  build_walls();
  load();
  settle();
  frame_t = eadk_timing_millis();
  sel = S.has ? 1 : 0;
  bool full = true;
  while (!quit) {
    scene = title_scene;
    keys();
    if (quit) break;
    if (hit & (K_UP | K_DOWN)) {
      do sel = (sel + (hit & K_UP ? 2 : 1)) % 3; while (sel == 1 && !S.has);
      refresh(96, 130, 136 + 88 * NP_TEXT_EXTRA, 54); /* (longer words in translations) */
    }
    if (hit & K_BACK) { /* "Quit game?" from the title */
      if (menu_box(T("QUIT GAME?"), yes_no, 2, 0) == 1) break;
      full = true;
      continue;
    }
    if (hit & K_OK) {
      if (sel == 2) options();
      else game(sel == 1);
      sel = S.has ? 1 : sel == 1 ? 0 : sel;
      full = true;
      settle();
      continue;
    }
    tt++;
    eadk_display_wait_for_vblank();
    if (full) {
      refresh(0, 0, 320, 240);
      full = false;
    } else { /* what moves: the chase, and the mouths of the cursor and the logo */
      refresh(0, LANE_Y - 8, 320, 16);
      if (!(tt & 3)) refresh(96, 132 + sel * 18, 16, 14);
      if (!(tt & 7)) refresh(LOGO_X + 68, LOGO_Y - 2, 39, 39);
    }
    frame_end();
  }
  save();
  return np_app_end();
}
