/* Connect 4: drop discs into the blue grid, four in a row wins. Against a
 * friend or the computer (three levels), with two or three players.
 *
 * Nothing is a bitmap. Each pixel of a changed rectangle is worked out from
 * the board, the discs and the moving disc (a falling disc is behind the
 * plastic, so it only shows through the holes, like the real one), text is
 * laid on top, and the rectangle goes to the screen in bands of a small
 * buffer. The computer searches with bitboards (alpha-beta, a transposition
 * table, threats and row parity), and gives up thinking after ~0.8 s. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_text.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Connect 4";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
#define NAVY RGB(0x0B1B42)
#define KEY(k) (1ull << (k))
#define SAVE_NAME "connect4.sav"

/* base, shade, light */
static const color DISC[3][3] = {
  {RGB(0xE0252B), RGB(0x86101A), RGB(0xFF8672)},
  {RGB(0xF8CB1C), RGB(0xB27A00), RGB(0xFFF5A8)},
  {RGB(0x1FAE4F), RGB(0x0A5E2A), RGB(0x8AF0A6)},
};
static const color BLUE[3] = {RGB(0x1E5BC6), RGB(0x0B2F7A), RGB(0x78A6FF)};
/* background top and bottom, text, soft text */
static const color THEME[2][4] = {
  {RGB(0xF2F6FC), RGB(0xC4D4EA), RGB(0x17264A), RGB(0x5A6C8E)},
  {RGB(0x1F2740), RGB(0x080B14), RGB(0xE9EEF8), RGB(0x8C98B4)},
};

static color mix(color f, color b, int a) { /* f over b, a from 0 to 32 */
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color)(y | y >> 16);
}
static int clamp(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int isin(int a) { /* a parabola sine: period 256, -256..256 */
  int x = a & 127, v = x * (128 - x) >> 4;
  return a & 128 ? -v : v;
}
static uint32_t seed = 0x2545F491;
static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}

/* ------------------------------------------------------------------ saved */
static struct {
  uint8_t magic, version;
  uint8_t cpu, players, level, dark; /* the options */
  uint8_t tcfg;                      /* the options the tally below belongs to */
  uint8_t starter;                   /* who starts the next round */
  uint16_t wins[4];                  /* and draws */
} V, saved;

static void save(void) {
  uint8_t *a = (uint8_t *)&V, *b = (uint8_t *)&saved;
  bool same = true;
  for (unsigned i = 0; i < sizeof V; i++) same &= a[i] == b[i];
  if (!same && ef_write(SAVE_NAME, &V, sizeof V)) saved = V;
}

static void load(void) {
  V.magic = 'C', V.version = 1, V.cpu = 1, V.players = 2, V.level = 1, V.tcfg = 0xFF;
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  if (d && n == sizeof V && d[0] == 'C' && d[1] == 1) {
    __typeof__(V) t;
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&t)[i] = d[i];
    /* (any tcfg or starter is harmless) */
    bool ok = t.cpu < 2 && t.players - 2u < 2 && t.level < 3 && t.dark < 2;
    for (int i = 0; i < 4; i++) ok &= t.wins[i] < 1000;
    if (ok) V = saved = t;
  }
}

/* ------------------------------------------------------------------ game state */
enum { S_TITLE, S_TURN, S_CPU, S_AIM, S_DROP, S_OVER, S_DRAIN, S_PAUSE, S_CONFIRM };
static int st, under;         /* the state, and the one under the pause menu */
static uint32_t now, t_state; /* milliseconds; when the state began */
static uint64_t held = ~0ull, hit;
static bool quit, at_title; /* at_title: "Quit game?" asked from the title */

typedef uint64_t bb;
/* The board as bitboards: bit c * 7 + r is column c, row r (from the
   bottom); the 7th bit of each column stays empty so that lines never wrap. */
static bb disc[4]; /* each player's discs, then all of them (the search plays on them too) */
static uint8_t height[8];
__attribute__((noinline)) static bb bit(int i) { return (bb)1 << i; }
__attribute__((noinline)) static int get(bb x, int i) { return (int)(x >> i) & 1; }
static uint8_t hist[48];   /* the columns played */
static int nh, first, cur, winner, hov[3];
static int np = 2, NC = 7, C = 30, R = 12, rr = 24, BX, BY, HY; /* layout */
#define F 6 /* the frame around the holes */
static bool mv;                   /* the current player's disc is shown */
static int mcol = -1, mx2, my2;   /* the column it falls in, its centre (half pixels) */
static int fy, vy, ty, dfall, dv; /* falls, in 1/16 px */
static int drain;                 /* how far the discs have dropped out, in px */
static bool pend_pause;
static bb wmask;                  /* the winning discs */
static int glow;
static int msel, psel, nitems, qsel;
static uint8_t items[4];
enum { I_RESUME, I_UNDO, I_RESTART, I_QUIT };
static const char *const ITEM[] = {T("Resume"), T("Undo move"), T("Restart"), T("Quit game")};

static int ccx2(int c) { return 2 * (BX + c * C) + C; }        /* a column's centre, in half pixels */
static int cy2(int r) { return 2 * (BY + (5 - r) * C) + C; } /* a row's */
static int hover_y2(void) { return 2 * HY + 1; }
static bool human(int p) { return !V.cpu || p == 0; }

/* for each screen column and row: the board's column and row (from the top,
   -1 off the grid), and the pixel's place in its cell */
static int8_t gcol[320], gdx[320], grow[240], gdy[240];

static void layout(void) {
  np = V.players, NC = np + 5, C = np == 3 ? 28 : 30, R = C * 2 / 5, rr = 2 * R;
  BX = (320 - NC * C) / 2, BY = 240 - 10 - F - 6 * C, HY = BY - F - R - 3;
  for (int i = 0; i < 320; i++) {
    int g = i - BX, h = i - BY;
    gcol[i] = (int8_t)(g >= 0 && g < NC * C ? g / C : -1), gdx[i] = (int8_t)(g % C);
    if (i < 240) grow[i] = (int8_t)(h >= 0 && h < 6 * C ? h / C : -1), gdy[i] = (int8_t)(h % C);
  }
}

/* ------------------------------------------------------------------ dirty rectangles */
static int16_t dr[12][4];
static int nd;
static void flush(void);
static void dirty(int x, int y, int w, int h) {
  int x1 = clamp(x + w, 0, 320), y1 = clamp(y + h, 0, 240);
  x = clamp(x, 0, 320), y = clamp(y, 0, 240);
  if (x >= x1 || y >= y1) return;
  for (int i = 0; i < nd; i++) {
    int16_t *d = dr[i];
    if (d[0] <= x && d[1] <= y && d[2] >= x1 && d[3] >= y1) return; /* already in */
    if (d[0] >= x && d[1] >= y && d[2] <= x1 && d[3] <= y1) d[2] = d[0];  /* inside the new one: drop it */
  }
  if (nd == 12) flush(); /* full: draw what is there now */
  dr[nd][0] = (int16_t)x, dr[nd][1] = (int16_t)y, dr[nd][2] = (int16_t)x1, dr[nd][3] = (int16_t)y1, nd++;
}
static void dirty_all(void) { dirty(0, 0, 320, 240); }
static void dirty_top(void) { dirty(0, 0, 320, BY - F); }
static void dirty_board(void) { dirty(BX, BY, NC * C, 6 * C); }
static void dirty_cell(int c, int r) { dirty(BX + c * C, BY + (5 - r) * C, C, C); }
static void dirty_panels(void) {
  dirty(0, 0, BX - F - 6, 240);
  dirty(320 - (BX - F - 6), 0, BX - F - 6, 240);
}

/* ------------------------------------------------------------------ pixels */
static color buf[320 * 20];
static int cx0, cy0, cx1, cy1, bw; /* the band being drawn */
static color rb[2]; /* the background on this row, dithered */

static struct { /* the rings of a disc of radius r, worked out once */
  int r, rim, band, step, inv, ho, hr, hk;
} dk;
/* A disc of radius r (half pixels) at offset (dx, dy) from its centre: a
   raised rim, a step down to a flat middle, lit from the top left. */
static color shade(int dx, int dy, int r, int p) {
  int d2 = dx * dx + dy * dy;
  if (dk.r != r) {
    int b = r * 7 / 10, m = r * 3 / 5;
    dk.r = r, dk.rim = (r - 3) * (r - 3), dk.band = b * b, dk.step = m * m, dk.inv = 65536 / r;
    dk.ho = r * 11 / 20, dk.hr = r * r / 30, dk.hk = 20 * 65536 / dk.hr;
  }
  const color *k = DISC[p];
  int s = (dx + dy) * dk.inv, a;
  if (d2 > dk.rim) a = -s * 18 >> 16;          /* the rim's edge, facing out */
  else if (d2 > dk.band) a = (-s * 6 >> 16) + 2; /* the rim */
  else if (d2 > dk.step) a = s * 22 >> 16;       /* the step, facing in */
  else a = (-s * 3 >> 16) - 7;                   /* the middle */
  color c = a > 0 ? mix(k[2], k[0], a > 32 ? 32 : a) : mix(k[1], k[0], a < -32 ? 32 : -a);
  int hx = dx + dk.ho, hy = dy + dk.ho, h2 = dk.hr - hx * hx - hy * hy;
  return h2 > 0 ? mix(WHITE, c, h2 * dk.hk >> 16) : c; /* a shine on the rim */
}
/* the winners' rims glow, the others fade */
static color light(color c, color bg, int lit, bool rim) {
  return !lit ? c : lit < 0 ? mix(c, bg, 13) : rim ? mix(WHITE, c, 8 + lit * 2) : mix(WHITE, c, lit / 3);
}
static color over_disc(color bg, int dx, int dy, int r, int p, int lit) {
  int d2 = dx * dx + dy * dy, e = r * r - d2;
  if (e <= -2 * r) return bg;
  color c = light(shade(dx, dy, r, p), bg, lit, d2 > (r - 5) * (r - 5));
  return e < 2 * r ? mix(c, bg, 8 * e / r + 16) : c;
}

/* One cell of the board, worked out once: for each pixel, the plastic's
   shade, how much of the hole it shows (0..32), the plastic's shadow (64),
   the edge of a disc (128); and each player's disc, lined up with it. */
static int8_t cshade[30 * 30];
static uint8_t chole[30 * 30];
static color cdisc[3][30 * 30];
static void cell_setup(void) {
  int o2 = (rr + 7) * (rr + 7), rr2 = rr * rr;
  for (int k = 0; k < C * C; k++) {
    int dx = 2 * (k % C) + 1 - C, dy = 2 * (k / C) + 1 - C, d2 = dx * dx + dy * dy, e = rr2 - d2, sx = dx - 5, sy = dy - 5;
    cshade[k] = (int8_t)clamp(d2 < o2 ? (dx + dy) * (o2 - d2) * 14 / rr / (o2 - rr2) : 0, -32, 32); /* the hole's bevel */
    chole[k] = (uint8_t)(clamp(8 * e / rr + 16, 0, 32) | (sx * sx + sy * sy > rr2) << 6 | (d2 > (rr - 5) * (rr - 5)) << 7);
    for (int p = 0; p < 3; p++) cdisc[p][k] = shade(dx, dy, rr, p);
  }
}

/* The blue plastic, shade from -32 (dark) to 32 (light). */
static color pl[65];
static color plastic(int s) { return pl[clamp(s, -32, 32) + 32]; }

/* A row of the game screen, from x0 to x1. What only depends on the row is
   worked out first: the frame's edges, which disc shows through each hole
   (the drain moves them all down), whether the moving disc crosses it. */
static void row_game(color *p, int y, int x0, int x1) {
  int fx0 = BX - F, fx1 = BX + NC * C + F, fy0 = BY - F, fy1 = BY + 6 * C + F;
  int ey = y < fy0 + 8 ? fy0 + 8 - y : y > fy1 - 9 ? y - (fy1 - 9) : 0, ey2 = y >= fy0 && y < fy1 ? ey * ey : 999;
  int sy = y < fy0 + 2 ? 16 : y >= fy1 - 2 ? -18 : 0, rt = grow[y], g = y - drain, gr = g >= 0 ? grow[g] : -1;
  int md = 2 * y + 1 - my2, ky = gdy[y] * C, kd = gr >= 0 ? gdy[g] * C : 0;
  bool mrow = mv && md > -rr - 2 && md < rr + 2;
  int8_t who[8], lit[8];
  for (int c = 0; c < NC; c++) {
    int i = c * 7 + 5 - gr;
    who[c] = -1, lit[c] = 0;
    for (int q = 0; q < np && gr >= 0; q++)
      if (get(disc[q], i)) who[c] = (int8_t)q, lit[c] = (int8_t)(!wmask ? 0 : get(wmask, i) ? glow : -1);
  }
  for (int x = x0; x < x1; x++) {
    color c = rb[(x ^ y) & 1];
    int ex = x < fx0 + 8 ? fx0 + 8 - x : x > fx1 - 9 ? x - (fx1 - 9) : 0;
    if (x < fx0 || x >= fx1 || ex * ex + ey2 > 70) {
      if ((x >= fx1 || y >= fy1) && x >= fx0 + 6 && x < fx1 + 6 && y >= fy0 + 6 && y < fy1 + 4) c = mix(0, c, 3); /* the board's shadow */
      if (mrow) c = over_disc(c, 2 * x + 1 - mx2, md, rr, cur, 0);
    } else {
      int s = sy + (x < fx0 + 2 ? 10 : x >= fx1 - 2 ? -12 : 0), col = gcol[x];
      if (col >= 0 && rt >= 0) {
        int kx = gdx[x], k = ky + kx, a = chole[k] & 63;
        s = cshade[k];
        if (a) { /* through the hole: the wall, a disc, the falling disc */
          if (who[col] >= 0) {
            int q = kd + kx, h = chole[q];
            c = mix(light(cdisc[who[col]][q], c, lit[col], h >> 7), c, h & 63);
          }
          if (mrow && col == mcol) c = over_disc(c, 2 * kx + 1 - C, md, rr, cur, 0);
          if (chole[k] & 64) c = mix(0, c, 9); /* the plastic's shadow */
          *p++ = a < 32 ? mix(c, plastic(s), a) : c;
          continue;
        }
      }
      c = plastic(s);
    }
    *p++ = c;
  }
}

/* The title's background: a close-up of the board, some discs in it. */
static color pix_title(int x, int y) {
  const int P = 48, r = 38;
  int gx = x + 16, gy = y + 14, col = gx / P, rt = gy / P;
  int dx = 2 * (gx - col * P) + 1 - P, dy = 2 * (gy - rt * P) + 1 - P, d2 = dx * dx + dy * dy, e = r * r - d2;
  static const char pat[] = "RY  YR" "  Y  Y" "     R" "Y    Y" "RR  RY" "YRY YR" "RYRYRR"; /* by columns */
  int s = (dx + dy) * 16 / r - 2;
  int o2 = (r + 9) * (r + 9);
  s = d2 < o2 ? s * (o2 - d2) / (o2 - r * r) : -2;
  color h = RGB(0x0A1430), c;
  char q = pat[(col % 7) * 6 + rt % 6];
  if (e <= -2 * r) {
    c = plastic(s);
  } else {
    if (q != ' ') h = over_disc(h, dx, dy, r, q == 'Y', 0);
    int sx = dx - 7, sy = dy - 7;
    if (sx * sx + sy * sy > r * r) h = mix(0, h, 10);
    c = e >= 2 * r ? h : mix(h, plastic(s), 8 * e / r + 16);
  }
  return mix(NAVY, c, y < 80 ? 6 : y < 216 ? 12 : 20); /* dimmed, so the words stand out */
}

/* ------------------------------------------------------------------ painting on the band */
static void fill(int x, int y, int w, int h, color c, int a) {
  int x0 = x < cx0 ? cx0 : x, x1 = x + w > cx1 ? cx1 : x + w, y0 = y < cy0 ? cy0 : y, y1 = y + h > cy1 ? cy1 : y + h;
  for (int j = y0; j < y1; j++)
    for (color *p = buf + (j - cy0) * bw + x0 - cx0, *e = p + x1 - x0; p < e; p++) *p = a >= 32 ? c : mix(c, *p, a);
}
static void rrect(int x, int y, int w, int h, int r, color c, int a) {
  for (int j = clamp(cy0 - y, 0, h), je = clamp(cy1 - y, 0, h); j < je; j++) {
    int e = j < r ? r - j : j >= h - r ? j - (h - 1 - r) : 0, in = 0;
    while (e && (r - in) * (r - in) + e * e > r * r + r) in++;
    fill(x + in, y + j, w - 2 * in, 1, c, a);
  }
}
/* a disc of radius r (half pixels) centred on (cx2, cy2) in half pixels */
static void disc_at(int cx2, int cy2, int r, int p, int lit) {
  int x0 = clamp((cx2 - r) / 2 - 1, cx0, cx1), x1 = clamp((cx2 + r) / 2 + 2, cx0, cx1);
  int y0 = clamp((cy2 - r) / 2 - 1, cy0, cy1), y1 = clamp((cy2 + r) / 2 + 2, cy0, cy1);
  for (int y = y0; y < y1; y++)
    for (int x = x0; x < x1; x++) {
      color *q = buf + (y - cy0) * bw + x - cx0;
      *q = over_disc(*q, 2 * x + 1 - cx2, 2 * y + 1 - cy2, r, p, lit);
    }
}

/* The characters the game writes, and their 5 columns of 8 bits (top row
   first, row 7 for descenders). Add any new one to both. */
static const char GLYPHS[] = " !'0123456789:<>?ABCDEFGIKLNOPQRSTUWYabdeghiklmnoprstuvxy"
#if NP_TEXT_EXTRA /* letters the translations need too (any other comes from the 12-pixel font) */
                             "HJMVcj"
#endif
    ;
static const uint8_t font[][5] = {
  {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x05,0x03,0x00,0x00},{0x3E,0x41,0x41,0x41,0x3E},{0x00,0x42,0x7F,0x40,0x00},
  {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},{0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
  {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
  {0x00,0x36,0x36,0x00,0x00},{0x08,0x14,0x22,0x41,0x00},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
  {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},
  {0x7F,0x09,0x09,0x09,0x01},{0x3E,0x41,0x49,0x49,0x7A},{0x00,0x41,0x7F,0x41,0x00},{0x7F,0x08,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
  {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
  {0x3F,0x40,0x40,0x40,0x3F},{0x3F,0x40,0x38,0x40,0x3F},{0x07,0x08,0x70,0x08,0x07},{0x20,0x54,0x54,0x54,0x78},
  {0x7F,0x48,0x44,0x44,0x38},{0x38,0x44,0x44,0x48,0x7F},{0x38,0x54,0x54,0x54,0x18},{0x18,0xA4,0xA4,0xA4,0x7C},
  {0x7F,0x08,0x04,0x04,0x78},{0x00,0x44,0x7D,0x40,0x00},{0x7F,0x10,0x28,0x44,0x00},{0x00,0x41,0x7F,0x40,0x00},
  {0x7C,0x04,0x18,0x04,0x78},{0x7C,0x08,0x04,0x04,0x78},{0x38,0x44,0x44,0x44,0x38},{0xFC,0x24,0x24,0x24,0x18},
  {0x7C,0x08,0x04,0x04,0x08},{0x48,0x54,0x54,0x54,0x20},{0x04,0x3F,0x44,0x40,0x20},{0x3C,0x40,0x40,0x20,0x7C},
  {0x1C,0x20,0x40,0x20,0x1C},{0x44,0x28,0x10,0x28,0x44},{0x0C,0x90,0x90,0x90,0x7C},
#if NP_TEXT_EXTRA
  {0x7F,0x08,0x08,0x08,0x7F},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x02,0x0C,0x02,0x7F},{0x1F,0x20,0x40,0x20,0x1F},
  {0x38,0x44,0x44,0x44,0x20},{0x40,0x80,0x84,0x7D,0x00},
#endif
};
#if NP_TEXT_EXTRA
/* Other languages: an accented letter is the font's own with np_accent() over it (a cedilla under it);
   a Chinese one, or one the font lacks, comes from the 12-pixel font at a whole scale near the text's
   size. dot() draws a font pixel: a square pen_k wide from (pen_x, pen_y), grown by pen_g, in pen_c at
   opacity pen_a. */
static int pen_x, pen_y, pen_k, pen_g, pen_a;
static color pen_c;
static void dot(int i, int j, void *ctx) {
  fill(pen_x + i * pen_k - pen_g, pen_y + j * pen_k - pen_g, pen_k + 2 * pen_g, pen_k + 2 * pen_g, pen_c, pen_a);
}
static int glyph_at(char b) {
  int ch = 0;
  while (GLYPHS[ch] && GLYPHS[ch] != b) ch++;
  return b && GLYPHS[ch] ? ch : -1;
}
/* draws (or only measures) the next letter of *s at x; returns its advance */
static int letter(const char **s, int x, int y, int k, bool draw) {
  uint32_t cp = np_utf8(s);
  char b = (char)cp, b2 = 0;
  int acc = cp >= 0x80 ? np_latin(cp, &b, &b2) : 0, ch = glyph_at(b), xs = (7 * k + 6) / 12;
  if (ch < 0) { /* from the 12-pixel font, in the middle of the capitals' height (an outline round its
                   thin strokes would fill them in: a shadow under them instead) */
    int g = pen_g, o = xs == 1 ? g : 0;
    if (draw) pen_x = x + o, pen_y = y + (7 * k - 12 * xs) / 2 + o, pen_k = xs, pen_g -= o, np_xdraw(cp, 0, 0, 1, dot, 0);
    pen_g = g;
    return np_xadvance(cp, xs);
  }
  if (draw) {
    pen_x = x, pen_y = y, pen_k = k;
    for (int n = 0; n < 2; n++, pen_x += 6 * k, ch = glyph_at(b2))
      for (int i = 0; i < 5 && ch >= 0; i++)
        for (int j = 0, f = font[ch][i]; f; j++, f >>= 1)
          if (f & 1) dot(i, j, 0);
    /* over a capital, over a small letter (its top is row 2), or the cedilla under it */
    pen_x = x, np_accent(acc, 0, acc == NP_ACC_CEDIL ? 7 : b < 'a' ? -3 : -1, 1, dot, 0);
  }
  return (b2 ? 12 : 6) * k;
}
static int tw(const char *s, int k) {
  int w = -k;
  while (*s) w += letter(&s, 0, 0, k, false);
  return w;
}
static void text(const char *s, int x, int y, int k, int g, color c, int a) {
  if (y - 3 * k - 3 - g >= cy1 || y + 8 * k + 4 + g <= cy0) return;
  pen_g = g, pen_c = c, pen_a = a;
  while (*s) x += letter(&s, x, y, k, true);
}
#else
static int slen(const char *s) {
  int n = 0;
  while (s[n]) n++;
  return n;
}
static int tw(const char *s, int k) { return slen(s) * 6 * k - k; }
/* text from x; each dot a k x k square grown by g on every side */
static void text(const char *s, int x, int y, int k, int g, color c, int a) {
  if (y - g >= cy1 || y + 8 * k + g <= cy0) return;
  for (; *s; s++, x += 6 * k) {
    int ch = 0;
    while (GLYPHS[ch] && GLYPHS[ch] != *s) ch++;
    for (int i = 0; i < 5 && GLYPHS[ch]; i++)
      for (int j = 0, b = font[ch][i]; b; j++, b >>= 1)
        if (b & 1) fill(x + i * k - g, y + j * k - g, k + 2 * g, k + 2 * g, c, a);
  }
}
#endif
static void ctext(const char *s, int cx, int y, int k, color c, int a) { text(s, cx - tw(s, k) / 2, y, k, 0, c, a); }
/* white (or c) with a dark outline */
static void otext(const char *s, int cx, int y, int k, color c) {
  int x = cx - tw(s, k) / 2;
  text(s, x, y + 1, k, 1, 0, 14);
  text(s, x, y, k, 1, NAVY, 32);
  text(s, x, y, k, 0, c, 32);
}
static char *num(char *o, unsigned v) {
  char t[6];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

/* ------------------------------------------------------------------ what goes on top */
/* the players' names, and what their wins are called */
static const char *const NAME[2][8] = {
  {T("P1"), T("P2"), T("P3"), "", T("RED WINS!"), T("YELLOW WINS!"), T("GREEN WINS!"), T("DRAW!")},
  {T("YOU"), T("CPU 1"), T("CPU 2"), T("CPU"), T("YOU WIN!"), T("CPU 1 WINS"), T("CPU 2 WINS"), T("CPU WINS")},
};
static const char *name(int p) { return NAME[V.cpu][p == 1 && np == 2 && V.cpu ? 3 : p]; }
static int panel_w(void) { return BX - F - 6; }

static void panels(void) {
  const color *t = THEME[V.dark];
  int w = panel_w();
  if (cx0 >= w && cx1 <= 320 - w) return;
  for (int p = 0; p < np; p++) {
    int cx = p == 1 ? 320 - w / 2 : w / 2, y = np == 2 ? 88 : p == 2 ? 146 : 66;
    int s = st >= S_PAUSE ? under : st;
    bool on = p == cur && s >= S_TURN && s <= S_DROP;
    if (on) rrect(cx - w / 2 + 1, y - 9, w - 2, 70, 10, DISC[p][0], 32);
    rrect(cx - w / 2 + 3, y - 7, w - 6, 66, 8, on ? mix(DISC[p][0], rb[0], 8) : mix(t[2], rb[0], 3), 32);
    ctext(name(p), cx, y, 1, t[2], 32);
    disc_at(2 * cx, 2 * (y + 23), 22, p, 0);
    char n[6];
    num(n, V.wins[p]);
    ctext(n, cx, y + 40, 2, t[2], 32);
  }
}

static void top_message(void) {
  const color *t = THEME[V.dark];
  const char *m = winner < 0 ? NAME[0][7] : NAME[V.cpu][winner == 1 && np == 2 && V.cpu ? 7 : 4 + winner];
  int y = np == 2 ? 3 : 8;
  otext(m, 160, y, 2, winner < 0 ? WHITE : DISC[winner][0]);
  ctext(T("OK: next round"), 160, y + 20, 1, t[3], 32);
}

static void card(int h, const char *title) {
  int y = (240 - h) / 2;
  rrect(80, y + 3, 160, h, 12, 0, 10);
  rrect(80, y, 160, h, 12, NAVY, 30);
  ctext(title, 160, y + 10, 2, WHITE, 32);
}

static void overlay(void) {
  for (color *p = buf, *e = buf + bw * (cy1 - cy0); p < e; p++) *p = mix(0, *p, 13);
  if (st == S_PAUSE) {
    int h = 40 + nitems * 24, y = (240 - h) / 2 + 34;
    card(h, T("PAUSED"));
    for (int i = 0; i < nitems; i++) {
      if (i == psel) rrect(96, y + i * 24 - 5, 128, 22, 8, DISC[1][0], 32);
      ctext(ITEM[items[i]], 160, y + i * 24, 2, i == psel ? NAVY : WHITE, 32);
    }
  } else {
    card(76, T("Quit game?"));
    for (int i = 0; i < 2; i++) {
      bool on = i == qsel;
      rrect(98 + i * 66, 128, 58, 24, 8, on ? DISC[1][0] : WHITE, on ? 32 : 6);
      ctext(i ? T("Yes") : T("No"), 127 + i * 66, 133, 2, on ? NAVY : WHITE, 32);
    }
  }
}

static const char *const OPT_NAME[] = {T("Opponent"), T("Players"), T("Level"), T("Theme")};
static const char *const OPT_VAL[] = {T("Friend"), T("Computer"), "2", "3", T("Weak"), T("Normal"), T("Strong"), T("Light"), T("Dark")};
static int opt(int i) { return i == 0 ? V.cpu : i == 1 ? V.players : i == 2 ? 4 + V.level : 7 + V.dark; }

static void title_ui(void) {
  /* CONNECT and a red disc with a 4 */
  if (cy0 < 80) {
#if NP_TEXT_EXTRA /* (another language's name: smaller if it would reach the disc) */
    const char *nm = T("CONNECT");
    int k = tw(nm, 5) > 210 ? 4 : 5, y = 25 + (5 - k) * 7 / 2;
    text(nm, 22, y + 4, k, 3, 0, 12);
    text(nm, 20, y, k, 2, NAVY, 32);
    text(nm, 20, y, k, 0, WHITE, 32);
#else
    text("CONNECT", 22, 29, 5, 3, 0, 12);
    text("CONNECT", 20, 25, 5, 2, NAVY, 32);
    text("CONNECT", 20, 25, 5, 0, WHITE, 32);
#endif
    disc_at(2 * 268, 2 * 42, 62, 0, 0);
    text("4", 256, 26, 5, 2, NAVY, 32);
    text("4", 256, 26, 5, 0, WHITE, 32);
  }
  rrect(34, 92, 252, 124, 14, 0, 10);
  rrect(32, 88, 256, 124, 14, NAVY, 32);
  bool on = msel == 0;
  rrect(100, 98, 120, 26, 10, on ? DISC[1][0] : WHITE, on ? 32 : 5);
  ctext(T("PLAY"), 160, 104, 2, on ? NAVY : WHITE, 32);
  for (int i = 0; i < 4; i++) {
    int y = 132 + i * 19, a = i == 2 && !V.cpu ? 12 : 32;
    on = msel == i + 1;
    if (on) rrect(40, y - 4, 240, 18, 7, WHITE, 5);
    text(OPT_NAME[i], 50, y + 4, 1, 0, RGB(0xA9BCE0), a);
    ctext(OPT_VAL[opt(i)], 220, y, 2, on ? DISC[1][0] : WHITE, a);
    if (on) {
      text("<", 152, y, 2, 0, DISC[1][0], 32);
      text(">", 278, y, 2, 0, DISC[1][0], 32);
    }
  }
  ctext(T("Based on Tatone26's version"), 160, 226, 1, WHITE, 26);
}

/* ------------------------------------------------------------------ to the screen */
static void paint(int x, int y, int w, int h) {
  int rows = (int)(sizeof buf / sizeof buf[0]) / w;
  bool title = st == S_TITLE || (st == S_CONFIRM && at_title);
  for (int y0 = y; y0 < y + h; y0 += rows) {
    cx0 = x, cx1 = x + w, cy0 = y0, cy1 = y0 + rows < y + h ? y0 + rows : y + h, bw = w;
    color *p = buf;
    for (int j = cy0; j < cy1; j++, p += w) {
      int g = j * 64 / 240;
      rb[0] = mix(THEME[V.dark][1], THEME[V.dark][0], g / 2), rb[1] = mix(THEME[V.dark][1], THEME[V.dark][0], (g + 1) / 2);
      if (!title) row_game(p, j, cx0, cx1);
      else
        for (int i = cx0; i < cx1; i++) /* (under the menu's card: nothing to work out) */
          p[i - cx0] = (i >= 46 && i < 274 && j >= 88 && j < 212) || (i >= 32 && i < 288 && j >= 102 && j < 198) ? 0 : pix_title(i, j);
    }
    if (title) {
      title_ui();
      if (st == S_CONFIRM) overlay();
    } else {
      panels();
      if (st == S_OVER || ((st == S_PAUSE || st == S_CONFIRM) && under == S_OVER)) top_message();
      if (st >= S_PAUSE) overlay();
    }
    eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)cy0, (uint16_t)w, (uint16_t)(cy1 - cy0)}, buf);
  }
}
static void flush(void) {
  for (int i = 0; i < nd; i++)
    if (dr[i][2] > dr[i][0]) paint(dr[i][0], dr[i][1], dr[i][2] - dr[i][0], dr[i][3] - dr[i][1]);
  nd = 0;
}

static void scan(void) {
  uint64_t k = eadk_keyboard_scan();
  hit = k & ~held, held = k;
  if (hit & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) quit = true;
}

/* ------------------------------------------------------------------ the computer */
/* every cell, the bottom row, the rows that suit each player */
static bb full, bot, good[3];
static int me, ply, noise, abort_s, psum[3], root;
static uint8_t order[8], cw[56]; /* centre-out columns, fours through each cell */
static uint32_t nodes, t0, t_anim, t_hard, gseed;
typedef struct {
  uint32_t k;
  int16_t v;
  uint8_t d, f; /* depth; bound (2 bits) and best column */
} tte;
#define TTN 4096
static tte tt[TTN];
#define WIN 10000
#define INF 30000

static int pc32(uint32_t v) {
  v = v - ((v >> 1) & 0x55555555);
  v = (v & 0x33333333) + ((v >> 2) & 0x33333333);
  return (int)((((v + (v >> 4)) & 0x0F0F0F0F) * 0x01010101) >> 24);
}
static int pc(bb x) { return pc32((uint32_t)x) + pc32((uint32_t)(x >> 32)); }
static bb cm[8]; /* each column */
static const int8_t DIR[4][2] = {{1, 0}, {0, 1}, {1, 1}, {1, -1}};
static bool on(int c, int r) { return (unsigned)c < (unsigned)NC && (unsigned)r < 6; }

/* the empty cells that would make four for p */
static bb wins(bb p, bb o) {
  bb r = (p << 1) & (p << 2) & (p << 3);
  for (int s = 6; s <= 8; s++) {
    bb a = (p << s) & (p << 2 * s), b = (p >> s) & (p >> 2 * s);
    r |= (a & ((p << 3 * s) | (p >> s))) | (b & ((p << s) | (p >> 3 * s)));
  }
  return r & full & ~o;
}

/* the discs of p that make fours */
static bb four(bb p, int d) {
  bb m = p & p >> d;
  m &= m >> 2 * d; /* where fours start */
  return m | m << d | m << 2 * d | m << 3 * d;
}
static bb fours(bb p) { return four(p, 1) | four(p, 6) | four(p, 7) | four(p, 8); } /* the four directions */

static void ai_setup(void) {
  full = bot = 0;
  for (int c = 0; c < NC; c++) cm[c] = bit(c * 7) * 0x3F, full |= cm[c], bot |= bit(c * 7);
  for (int i = 0; i < NC; i++) order[i] = (uint8_t)((NC - 1) / 2 + (i & 1 ? 1 : -1) * ((i + 1) / 2));
  for (int c = 0; c < NC; c++)
    for (int r = 0; r < 6; r++) {
      int n = 0;
      for (int d = 0; d < 4; d++)
        for (int k = 0; k < 4; k++) { /* the window starting k cells back */
          int c0 = c - k * DIR[d][0], r0 = r - k * DIR[d][1];
          n += on(c0, r0) && on(c0 + 3 * DIR[d][0], r0 + 3 * DIR[d][1]);
        }
      cw[c * 7 + r] = (uint8_t)n;
    }
}

/* t: each player's threats. A threat above another player's threat in the
   same column rarely matters: the lower one decides the column. Threats on
   the rows that suit the player (odd rows for whoever started, in
   two-player games) are what win endgames: if the second player answers
   every move in the same column, the first gets the odd rows and the second
   the even ones, so fill the board that way and see who has a four. Discs
   near the middle take part in more fours (psum). */
static int eval(const bb *t) {
  int s = 0;
  if (np == 2) {
    bb e = full & ~disc[3], o = e & good[first];
    int v = ((fours(disc[first] | o) != 0) - (fours(disc[!first] | (e & ~o)) != 0)) * 150;
    s = first == me ? v : -v;
  }
  bb all = t[0] | t[1] | t[2];
  for (int p = 0; p < np; p++) {
    bb a = (all & ~t[p]) << 1 & full; /* the cells above the others' threats */
    for (int i = 0; i < 4; i++) a |= a << 1 & full;
    bb u = t[p] & ~a;
    int v = pc(t[p]) * 4 + pc(u) * 12 + pc(u & good[p]) * 16 + psum[p];
    s += p == me ? v : -v;
  }
  if (noise) {
    uint32_t h = (uint32_t)(((disc[3] ^ disc[0]) * 0x9E3779B97F4A7C15ull) >> 32) ^ gseed;
    h ^= h >> 15, h *= 0x2C1B3C6Du, h ^= h >> 12;
    s += (int)(h % (2u * noise + 1)) - noise;
  }
  return s;
}

static void think_anim(void);
static void hook(void) {
  uint32_t t = (uint32_t)eadk_timing_millis();
  if (t >= t_hard) abort_s = 1;
  if (t - t_anim >= 30) {
    t_anim = t;
    think_anim();
  }
}

static int search(int side, int depth, int alpha, int beta, const bb *t);
/* plays column c for side, searches, takes it back */
static int try_move(int side, int c, int depth, int alpha, int beta, const bb *t, bb w) {
  int i = c * 7 + height[c], nx = side + 1 == np ? 0 : side + 1;
  bb m = bit(i), ct[3] = {t[0] & ~m, t[1] & ~m, t[2] & ~m};
  ct[side] = w; /* the mover's threats, already worked out */
  disc[side] |= m, disc[3] |= m, height[c]++, ply++, psum[side] += cw[i];
  int v = depth < 0 ? eval(ct) : search(nx, depth, alpha, beta, ct);
  disc[side] ^= m, disc[3] ^= m, height[c]--, ply--, psum[side] -= cw[i];
  return v;
}

/* Paranoid minimax: the computer (me) against everyone else. Scores are
   from me's side; a win sooner is worth more. */
static int search(int side, int depth, int alpha, int beta, const bb *t) {
  if (!(++nodes & 255)) hook();
  if (abort_s) return 0;
  bb poss = (disc[3] + bot) & full;
  if (!poss) return 0;
  int win = WIN - ply, nx = side + 1 == np ? 0 : side + 1;
  bool mx = side == me;
  if (t[side] & poss) return mx ? win : -win;
  if (!depth) return eval(t);
  if (mx != (nx == me)) { /* the next player is against us: block, and don't help */
    bb ow = t[nx], forced = poss & ow;
    int lose = nx == me ? win - 1 : 1 - win;
    if (forced) {
      if (forced & (forced - 1)) return lose;
      poss = forced;
    }
    poss &= ~(ow >> 1);
    if (!poss) return lose;
  }
  bb h = disc[0] * 0x9E3779B97F4A7C15ull ^ disc[1] * 0xC2B2AE3D27D4EB4Full ^ disc[2] * 0x165667B19E3779F9ull;
  tte *e = &tt[(uint32_t)h >> 20];
  uint32_t k = (uint32_t)(h >> 32);
  int tm = -1;
  if (e->k == k) {
    tm = e->f >> 2;
    if (e->d >= depth) {
      int v = e->v, f = e->f & 3;
      if (f == 0 || (f == 1 && v >= beta) || (f == 2 && v <= alpha)) return v;
    }
  }
  /* moves: the table's first, then those making the most threats, centre first */
  int8_t mv_[8];
  int16_t sc[8];
  bb ws[8];
  int n = 0;
  for (int i = 0; i < NC; i++) {
    int c = order[i];
    bb m = poss & cm[c];
    if (!m) continue;
    bb w = wins(disc[side] | m, disc[3] | m);
    int s = c == tm ? 1000 : pc(w) * 4 - i, j = n++;
    while (j && sc[j - 1] < s) sc[j] = sc[j - 1], mv_[j] = mv_[j - 1], ws[j] = ws[j - 1], j--;
    sc[j] = (int16_t)s, mv_[j] = (int8_t)c, ws[j] = w;
  }
  int best = mx ? -INF : INF, bc = mv_[0], a0 = alpha, b0 = beta;
  for (int i = 0; i < n; i++) {
    int v = try_move(side, mv_[i], depth - 1, alpha, beta, t, ws[i]);
    if (abort_s) break;
    if (mx ? v > best : v < best) {
      best = v, bc = mv_[i];
      if (!ply) root = bc; /* the computer's move, so far */
    }
    if (mx) alpha = best > alpha ? best : alpha;
    else beta = best < beta ? best : beta;
    if (alpha >= beta) break;
  }
  if (!abort_s) e->k = k, e->v = (int16_t)best, e->d = (uint8_t)depth, e->f = (uint8_t)((best <= a0 ? 2 : best >= b0 ? 1 : 0) | bc << 2);
  return best;
}

/* The computer's column for player cur. */
static int think(void) {
  for (int p = 0; p < 3; p++) {
    psum[p] = 0;
    for (int i = 0; i < 56; i++) psum[p] += get(disc[p], i) * cw[i];
  }
  me = cur, ply = 0, abort_s = 0, nodes = 0;
  t0 = t_anim = (uint32_t)eadk_timing_millis();
  t_hard = t0 + (V.level == 2 ? 850 : 4000);
  gseed = rnd();
  bb odd = full & 0x2A54A952A54A95ull, t[3]; /* rows 1, 3 and 5 */
  for (int p = 0; p < 3; p++) good[p] = 0, t[p] = wins(disc[p], disc[3]);
  if (np == 2) good[first] = odd, good[!first] = full & ~odd;
  for (unsigned i = 0; i < sizeof tt / sizeof tt[0]; i++) tt[i].k = 0;
  bb poss = (disc[3] + bot) & full;
  for (int i = NC; i--;)
    if (poss & cm[order[i]]) {
      root = order[i];
      if (t[me] & poss & cm[root]) return root; /* a four: always seen, at once */
    }
  /* deeper paranoid searches only get gloomier with three players */
  int lv = V.level, maxd = lv == 0 ? 1 : lv == 1 ? 9 - np * 2 : np == 2 ? 42 - nh : 4, best = root;
  noise = lv == 0 ? 50 : lv == 1 ? 6 : 1;
  for (int d = lv == 2 ? 1 : maxd; d <= maxd; d++) { /* deeper while there is time */
    int v = search(me, d, -INF, INF, t);
    /* every move loses against perfect play: keep the last answer, the
       opponent may not find the win */
    if (v <= 64 - WIN && d > 1) break;
    best = root; /* (even out of time: the moves searched in time are fine) */
    if (abort_s || v >= WIN - 64 || v <= 64 - WIN || (uint32_t)eadk_timing_millis() - t0 > 300) break;
  }
  while (!abort_s && (uint32_t)eadk_timing_millis() - t0 < 500) /* a quick answer still looks thought about */
    eadk_timing_msleep(15), hook();
  return best;
}

/* ------------------------------------------------------------------ playing */
static void set_state(int s) { st = s, t_state = now; }

static void show_turn(void) { /* the current player's disc above the board */
  mv = true, mcol = -1, mx2 = ccx2(hov[cur]), my2 = hover_y2();
  set_state(human(cur) ? S_TURN : S_CPU);
  dirty_top();
  dirty_panels();
}

static void new_round(void) {
  disc[0] = disc[1] = disc[2] = disc[3] = 0;
  for (int c = 0; c < 8; c++) height[c] = 0;
  nh = 0, wmask = 0, drain = 0, pend_pause = false;
  cur = first = V.starter % np;
  V.starter = (uint8_t)((first + 1) % np);
  for (int p = 0; p < 3; p++) hov[p] = (NC - 1) / 2;
  dirty_all();
  show_turn();
}

static void start_game(void) {
  int cfg = V.cpu | (V.players - 2) << 1 | V.level << 2;
  if (cfg != V.tcfg) {
    V.tcfg = (uint8_t)cfg, V.starter = 0;
    V.wins[0] = V.wins[1] = V.wins[2] = V.wins[3] = 0;
  }
  layout();
  cell_setup();
  ai_setup();
  new_round();
  save();
}

static void drop(int c) {
  if (height[c] >= 6) return;
  hov[cur] = c, mcol = c, mx2 = ccx2(c);
  fy = my2 * 8, vy = 0, ty = cy2(height[c]) * 8;
  dirty_top();
  set_state(S_DROP);
}

static void open_pause(void);
/* the disc has landed: four in a row, a full board, or the next player */
static void landed(void) {
  int c = mcol;
  bb m = bit(c * 7 + height[c]++);
  disc[cur] |= m, disc[3] |= m;
  hist[nh++] = (uint8_t)c;
  mv = false, mcol = -1;
  wmask = fours(disc[cur]);
  if (wmask || nh == NC * 6) {
    winner = wmask ? cur : -1;
    int w = wmask ? cur : 3;
    V.wins[w] += V.wins[w] < 999;
    save();
    set_state(S_OVER);
    dirty_top();
    dirty_panels();
    dirty_board();
  } else {
    cur = (cur + 1) % np;
    show_turn();
  }
  if (pend_pause) pend_pause = false, open_pause();
}

static bool can_undo(void) { /* back to a human turn */
  return nh > (V.cpu ? (np - first) % np : 0);
}
static void undo(void) {
  while (nh) {
    int c = hist[--nh];
    bb m = ~bit(c * 7 + --height[c]);
    for (int p = 0; p < 4; p++) disc[p] &= m;
    dirty_cell(c, height[c]);
    cur = (first + nh) % np;
    if (human(cur)) break;
  }
  show_turn();
}

static void open_pause(void) {
  under = st;
  nitems = 0, psel = 0;
  items[nitems++] = I_RESUME;
  if (under != S_OVER && can_undo()) items[nitems++] = I_UNDO;
  items[nitems++] = I_RESTART;
  items[nitems++] = I_QUIT;
  set_state(S_PAUSE);
  dirty_all();
}
static void close_pause(void) {
  st = under;
  if (st == S_TURN) show_turn();
  dirty_all();
}

static void dirty_hover(int old) { /* where the hovering disc was and is */
  dirty((old < mx2 ? old : mx2) / 2 - R - 2, HY - R - 2, iabs(old - mx2) / 2 + 2 * R + 6, 2 * R + 5);
}

/* while thinking: the disc sways over the board, and keys still work */
static bool want_pause;
static void think_anim(void) {
  scan();
  if (quit) abort_s = 1;
  if (hit & KEY(eadk_key_back)) want_pause = true, abort_s = 1;
  int old = mx2;
  mx2 = clamp(ccx2(hov[cur]) + isin((int)(t_anim - t0) / 5) * C * 3 / 256, ccx2(0), ccx2(NC - 1));
  dirty_hover(old);
  flush(); /* no wait for the refresh: thinking time is precious */
}

static void slide(void) { /* the hovering disc glides to its column */
  int tx = ccx2(hov[cur]), old = mx2, d = (tx - mx2) / 3;
  mx2 += d ? d : tx > mx2 ? 1 : tx < mx2 ? -1 : 0;
  if (mx2 != old) dirty_hover(old);
}

static uint32_t rep_at;
static void step(int ticks) {
  bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back);
  const uint64_t rep = KEY(eadk_key_left) | KEY(eadk_key_right) | KEY(eadk_key_up) | KEY(eadk_key_down);
  uint64_t h = hit;
  if (hit & rep) rep_at = now + 300;
  else if ((held & rep) && (int32_t)(now - rep_at) >= 0) h |= held & rep, rep_at = now + 90;
  int lr = (h & KEY(eadk_key_right) ? 1 : 0) - (h & KEY(eadk_key_left) ? 1 : 0);
  int ud = (h & KEY(eadk_key_down) ? 1 : 0) - (h & KEY(eadk_key_up) ? 1 : 0);
  switch (st) {
    case S_TITLE:
      if (back) {
        qsel = 0, at_title = true, set_state(S_CONFIRM), dirty_all();
        break;
      }
      if (ud) {
        do msel = (msel + ud + 5) % 5; while (msel == 3 && !V.cpu);
        dirty(32, 88, 256, 124);
      }
      if ((lr || ok) && msel) {
        int d = lr ? lr : 1;
        if (msel == 1) V.cpu ^= 1;
        if (msel == 2) V.players ^= 1;
        if (msel == 3) V.level = (uint8_t)((V.level + d + 3) % 3);
        if (msel == 4) V.dark ^= 1;
        dirty(32, 88, 256, 124);
      } else if (ok) {
        start_game();
      }
      break;
    case S_TURN: {
      static const uint8_t digit[8] = {42, 43, 44, 36, 37, 38, 30, 31};
      for (int i = 0; i < NC; i++)
        if (hit & KEY(digit[i])) hov[cur] = i, ok = true;
      if (lr) hov[cur] = clamp(hov[cur] + lr, 0, NC - 1);
      if (ok || (hit & KEY(eadk_key_down))) drop(hov[cur]);
      else if ((hit & KEY(eadk_key_backspace)) && can_undo()) undo();
      else if (back) open_pause();
      slide();
      break;
    }
    case S_CPU: {
      want_pause = false;
      int c = think();
      if (quit) return;
      if (want_pause) {
        mx2 = ccx2(hov[cur]);
        open_pause();
        return;
      }
      hov[cur] = c;
      set_state(S_AIM);
      break;
    }
    case S_AIM:
      slide();
      if (back) open_pause();
      else if (mx2 == ccx2(hov[cur]) && now - t_state > 200) drop(hov[cur]);
      break;
    case S_DROP: {
      int old = my2;
      while (ticks--) {
        vy += 20, fy += vy;
        if (fy >= ty) {
          fy = ty;
          if (vy < 90) {
            vy = 0;
            break;
          }
          vy = -vy / 4;
        }
      }
      my2 = fy / 8;
      int y0 = (old < my2 ? old : my2) / 2 - R - 2, y1 = (old > my2 ? old : my2) / 2 + R + 3;
      dirty(BX + mcol * C, y0, C, y1 - y0);
      if (back) pend_pause = true;
      if (fy == ty && !vy) landed();
      break;
    }
    case S_OVER:
      glow = (isin((int)(now - t_state) / 4) + 256) * 12 / 512;
      for (int b = 0; b < 56; b++)
        if (get(wmask, b)) dirty_cell(b / 7, b % 7);
      if (ok) {
        wmask = 0, dfall = dv = 0;
        set_state(S_DRAIN);
        dirty_all();
      } else if (back) {
        open_pause();
      }
      break;
    case S_DRAIN: { /* the discs drop out of the bottom: from the top one down */
      int top = 0, y;
      for (int c = 0; c < NC; c++) top = height[c] > top ? height[c] : top;
      y = BY + (6 - top) * C + drain;
      while (ticks--) dv += 10, dfall += dv;
      drain = dfall / 16;
      if (drain > top * C) new_round();
      else dirty(BX, y, NC * C, BY + 6 * C - y);
      break;
    }
    case S_PAUSE:
      if (ud) psel = (psel + ud + nitems) % nitems, dirty(80, 40, 160, 160);
      if (back) close_pause();
      else if (ok) {
        int it = items[psel];
        if (it == I_RESUME) close_pause();
        if (it == I_UNDO) st = S_TURN, undo(), dirty_all();
        if (it == I_RESTART) V.starter = (uint8_t)first, new_round();
        if (it == I_QUIT) qsel = 0, at_title = false, set_state(S_CONFIRM), dirty_all();
      }
      break;
    case S_CONFIRM:
      if (lr) qsel ^= 1, dirty(80, 80, 160, 80);
      if (back || (ok && !qsel)) set_state(at_title ? S_TITLE : S_PAUSE), dirty_all();
      else if (ok) quit = true; /* back to NumPlay (or the calculator); main saves */
      break;
  }
}

int main(void) {
  np_app_begin();
  for (int i = 0; i < 65; i++) /* the plastic's shades */
    pl[i] = i > 32 ? mix(BLUE[2], BLUE[0], i - 32) : mix(BLUE[1], BLUE[0], 32 - i);
  load();
  now = (uint32_t)eadk_timing_millis();
  seed ^= now ^ eadk_random();
  layout();
  set_state(S_TITLE);
  dirty_all();
  uint32_t last = now;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    int ticks = (int)(now - last) / 16; /* 60 Hz steps for the falling discs */
    if (ticks > 4) ticks = 4, last = now;
    else last += (uint32_t)ticks * 16;
    scan();
    if (quit) break;
    step(ticks);
    if (quit) break;
    eadk_display_wait_for_vblank();
    flush();
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
  save();
  return np_app_end();
}
