#include <eadk.h>
#include <string.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/jump.h"
#include "../../common/np_text.h"
#include "chess.h"
#include "sprites.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Chess";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

typedef uint16_t C;
#define RGB(r, g, b) ((C)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))

#define BG RGB(0x26, 0x25, 0x22)
#define CARD RGB(0x3A, 0x38, 0x35)
#define GREEN RGB(0x81, 0xB6, 0x4C)
#define RED RGB(0xE0, 0x43, 0x3B)
#define WHITE 0xFFFF
#define DIM RGB(0x98, 0x97, 0x95)
#define INK RGB(0x26, 0x25, 0x22)
#define SQ_L RGB(0xEB, 0xEC, 0xD0)
#define SQ_D RGB(0x77, 0x95, 0x56)
#define HL_L RGB(0xF5, 0xF6, 0x82)
#define HL_D RGB(0xB9, 0xCA, 0x43)
#define HINT RGB(0x5B, 0x9B, 0xE6)

enum { K_LEFT, K_UP, K_DOWN, K_RIGHT, K_OK, K_BACK, K_UNDO = 17, K_EXE = 52 };
enum { M_BOT, M_2P, M_PUZ };

static C buf[900];

/* ------------------------------------------------------------ Drawing */

#ifdef __TICE__ /* the TI-84 Plus CE: its own mixing (the same colours, without a 24-bit int's overflow) */
#include "../../../platform/ti84ce/ce_draw.h"
__attribute__((naked)) static C ce_mix(C a, C b, unsigned t) { __asm__(CE_MIX_ASM); }
static C mix(C a, C b, int t) { return ce_mix(a, b, (unsigned)t); }
#else
static C mix(C a, C b, int t) {
  uint32_t A = (a | a << 16) & 0x07E0F81F, B = (b | b << 16) & 0x07E0F81F;
  uint32_t M = ((A * (32 - t) + B * t) >> 5) & 0x07E0F81F;
  return M | M >> 16;
}
#endif

static void fill(int x, int y, int w, int h, C c) {
  eadk_display_push_rect_uniform((eadk_rect_t){x, y, w, h}, c);
}

/* The background everywhere except in the rectangles {x, y, w, h}, which are drawn over next:
 * a new screen goes up without being cleared first, every pixel changing once (a cleared
 * screen shows on the calculator for a moment: a flicker). */
static void clear_around(const int16_t (*r)[4], int n, C c) {
  int run0 = 0, prev[16], np = -1;
  for (int y = 0; y <= 240; y++) {
    int cov[16], nc = 0;
    if (y < 240)
      for (int k = 0; k < n && nc < 16; k++)
        if (y >= r[k][1] && y < r[k][1] + r[k][3]) {
          /* (kept sorted by start) */
          int j = nc++;
          for (; j > 0 && r[cov[j - 1]][0] > r[k][0]; j--) cov[j] = cov[j - 1];
          cov[j] = k;
        }
    bool same = nc == np;
    for (int k = 0; same && k < nc; k++) same = cov[k] == prev[k];
    if (same && y < 240) continue;
    /* rows run0..y-1 had the rectangles prev: the gaps between them */
    if (np >= 0 && y > run0) {
      int x = 0;
      for (int k = 0; k <= np; k++) {
        int x1 = k < np ? r[prev[k]][0] : 320;
        if (x1 > x) fill(x, run0, x1 - x, y - run0, c);
        if (k < np && r[prev[k]][0] + r[prev[k]][2] > x) x = r[prev[k]][0] + r[prev[k]][2];
      }
    }
    run0 = y, np = nc;
    for (int k = 0; k < nc; k++) prev[k] = cov[k];
  }
}

static void text(const char *s, int x, int y, int big, C fg, C bg) {
  eadk_display_draw_string(s, (eadk_point_t){x, y}, big, fg, bg);
}

/* the cells a text takes: a letter each (two for a Chinese one, in a language build) */
static int cells(const char *s) {
  if (NP_TEXT_EXTRA) return np_text_cells(s);
  int n = 0;
  for (const char *c = s; *c; c++) n += (*c & 0xC0) != 0x80;
  return n;
}

static void ctext(const char *s, int cx, int y, int big, C fg, C bg) {
  text(s, cx - cells(s) * (big ? 10 : 7) / 2, y, big, fg, bg);
}

static char *itoa(int v, char *o) {
  char t[8];
  int n = 0;
  if (v < 0) *o++ = '-', v = -v;
  do t[n++] = '0' + v % 10; while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

static float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* Anti-aliased rounded rectangle blended over what is on screen. */
static void rrect(int x, int y, int w, int h, int r, C c) {
  for (int j = 0; j < h; j++) {
    int e = j < r || j >= h - r;
    if (!e) {
      fill(x, y + j, w, h - 2 * r, c);
      j = h - r - 1;
      continue;
    }
    eadk_rect_t R = {x, y + j, w, 1};
    eadk_display_pull_rect(R, buf);
    for (int i = 0; i < w; i++) {
      float dx = i < r ? r - i - 0.5f : i >= w - r ? i + 0.5f - (w - r) : 0;
      float dy = j < r ? r - j - 0.5f : j + 0.5f - (h - r);
      float d = __builtin_sqrtf(dx * dx + dy * dy) - r;
      buf[i] = mix(buf[i], c, (int)(32 * clampf(0.5f - d)));
    }
    eadk_display_push_rect(R, buf);
  }
}

static void dim(int x, int y, int w, int h) {
  for (int j = 0; j < h; j++) {
    eadk_rect_t R = {x, y + j, w, 1};
    eadk_display_pull_rect(R, buf);
    for (int i = 0; i < w; i++) buf[i] = mix(buf[i], 0, 15);
    eadk_display_push_rect(R, buf);
  }
}

/* ------------------------------------------------------------ Sprites */

static const C PCOL[2][3] = {
  {RGB(0xF9, 0xF9, 0xF7), RGB(0x3A, 0x3A, 0x3A), RGB(0x3A, 0x3A, 0x3A)},
  {RGB(0x52, 0x4F, 0x4D), RGB(0x16, 0x15, 0x14), RGB(0xA8, 0xA5, 0xA2)},
};
/* class -> (color a, color b, weight of b) with colors: 0 bg, 1 body, 2 ink, 3 detail */
static const uint8_t SHADE[8][3] = {{0, 0, 0}, {1, 1, 0}, {2, 2, 0}, {0, 2, 19},
                                    {0, 2, 10}, {3, 3, 0}, {1, 2, 16}, {1, 3, 16}};

static int spx(int t, int x, int y) {
  const uint8_t *m = &SPR[t - 1].x;
  int w = m[2] & 0x7F;
  y -= m[1];
  if ((unsigned)y >= m[3]) return 0;
  if (m[2] & 0x80 && x > 14) x = 29 - x;
  x -= m[0];
  if ((unsigned)x >= (unsigned)w) return 0;
  unsigned i = (SPR[t - 1].off + y * w + x) * 3;
  return ((SPRD[i >> 3] | SPRD[(i >> 3) + 1] << 8) >> (i & 7)) & 7;
}

static C shade(int pc, int x, int y, C bg) {
  int k = spx(TYPE(pc), x, y);
  if (!k) return bg;
  const C *p = PCOL[COLOR(pc) > 1 ? x > 14 : COLOR(pc)];
  C c[4] = {bg, p[0], p[1], p[2]};
  return mix(c[SHADE[k][0]], c[SHADE[k][1]], SHADE[k][2]);
}

/* Draw a piece (30px, or 15px when small) over the screen content.
 * COLOR 2 draws a half white / half black piece. */
static void sprite(int pc, int X, int Y, int small) {
  int s = small ? 15 : 30;
  eadk_rect_t R = {X, Y, s, s};
  eadk_display_pull_rect(R, buf);
  for (int y = 0; y < s; y++)
    for (int x = 0; x < s; x++) {
      C *o = &buf[y * s + x];
      if (small) {
        C a = mix(shade(pc, 2 * x, 2 * y, *o), shade(pc, 2 * x + 1, 2 * y, *o), 16);
        C b = mix(shade(pc, 2 * x, 2 * y + 1, *o), shade(pc, 2 * x + 1, 2 * y + 1, *o), 16);
        *o = mix(a, b, 16);
      } else {
        *o = shade(pc, x, y, *o);
      }
    }
  eadk_display_push_rect(R, buf);
}

/* ------------------------------------------------------------ Widgets */

/* Menus, dialogs and the side panel are drawn into memory, then sent to the
 * screen in one go, so nothing on screen is cleared first. Their texts are left
 * out of that piece and drawn by the calculator right after: its text routine
 * paints its own background, so every pixel changes once and nothing flickers. */
#define WPIX (200 * 56)
#ifdef __TICE__ /* past the TI-84 CE's 60 KB for static data: it comes with the program */
__attribute__((section(".data")))
#endif
static C wbuf[WPIX];
static struct {
  int x, y, w, h, nt;
  struct {
    int16_t x, y;
    uint8_t big, n;
    C fg, bg;
    char s[NP_TEXT_EXTRA ? 40 : 16];
  } t[8];
} W;

static void w_begin(int x, int y, int w, int h, C bg) {
  if (w * h > WPIX) h = WPIX / w;
  W.x = x, W.y = y, W.w = w, W.h = h, W.nt = 0;
  for (int i = 0; i < w * h; i++) wbuf[i] = bg;
}

/* the same, over what the screen shows there */
static void w_grab(int x, int y, int w, int h) {
  w_begin(x, y, w, h, 0);
  eadk_display_pull_rect((eadk_rect_t){x, y, w, W.h}, wbuf);
}

/* Anti-aliased rounded rectangle over what is drawn so far (screen coordinates). */
static void w_rrect(int x, int y, int w, int h, int r, C c) {
  for (int j = 0; j < h; j++) {
    int yy = y + j - W.y;
    if (yy < 0 || yy >= W.h) continue;
    float dy = j < r ? r - j - 0.5f : j >= h - r ? j + 0.5f - (h - r) : 0;
    C *row = wbuf + yy * W.w;
    for (int i = 0; i < w; i++) {
      int xx = x + i - W.x;
      if (xx < 0 || xx >= W.w) continue;
      float dx = i < r ? r - i - 0.5f : i >= w - r ? i + 0.5f - (w - r) : 0;
      if (dx == 0 || dy == 0) {
        row[xx] = c;
        continue;
      }
      float d = __builtin_sqrtf(dx * dx + dy * dy) - r;
      row[xx] = mix(row[xx], c, (int)(32 * clampf(0.5f - d)));
    }
  }
}

static void w_disc(int cx, int cy, int r, C c) { w_rrect(cx - r, cy - r, 2 * r, 2 * r, r, c); }

/* Thick anti-aliased line. */
static void w_stroke(float x0, float y0, float x1, float y1, float w, C c) {
  float ex = x1 - x0, ey = y1 - y0, l2 = ex * ex + ey * ey;
  for (int yy = 0; yy < W.h; yy++)
    for (int xx = 0; xx < W.w; xx++) {
      float px = W.x + xx + 0.5f - x0, py = W.y + yy + 0.5f - y0, t = clampf((px * ex + py * ey) / l2);
      px -= ex * t, py -= ey * t;
      float a = clampf(w / 2 + 0.5f - __builtin_sqrtf(px * px + py * py));
      if (a > 0) wbuf[yy * W.w + xx] = mix(wbuf[yy * W.w + xx], c, (int)(32 * a));
    }
}

static void w_sprite(int pc, int X, int Y, int small) {
  int s = small ? 15 : 30;
  for (int y = 0; y < s; y++) {
    int yy = Y + y - W.y;
    if (yy < 0 || yy >= W.h) continue;
    for (int x = 0; x < s; x++) {
      int xx = X + x - W.x;
      if (xx < 0 || xx >= W.w) continue;
      C *o = &wbuf[yy * W.w + xx];
      if (small) {
        C a = mix(shade(pc, 2 * x, 2 * y, *o), shade(pc, 2 * x + 1, 2 * y, *o), 16);
        C b = mix(shade(pc, 2 * x, 2 * y + 1, *o), shade(pc, 2 * x + 1, 2 * y + 1, *o), 16);
        *o = mix(a, b, 16);
      } else {
        *o = shade(pc, x, y, *o);
      }
    }
  }
}

static void w_text(const char *s, int x, int y, int big, C fg, C bg) {
  if (W.nt >= 8) return;
  int k = W.nt++, n;
  strncpy(W.t[k].s, s, sizeof W.t[k].s - 1);
  W.t[k].s[sizeof W.t[k].s - 1] = 0;
  n = cells(W.t[k].s);
  W.t[k].x = (int16_t)x, W.t[k].y = (int16_t)y, W.t[k].big = (uint8_t)big, W.t[k].n = (uint8_t)n;
  W.t[k].fg = fg, W.t[k].bg = bg;
}

static void w_ctext(const char *s, int cx, int y, int big, C fg, C bg) {
  w_text(s, cx - cells(s) * (big ? 10 : 7) / 2, y, big, fg, bg);
}

/* Sends the piece around its texts (rows without text in blocks), then the texts. */
static void w_end(void) {
  int b0 = -1;
  for (int j = 0; j <= W.h; j++) {
    int sy = W.y + j, cut = 0;
    for (int k = 0; j < W.h && k < W.nt && !cut; k++) cut = sy >= W.t[k].y && sy < W.t[k].y + (W.t[k].big ? 18 : 14);
    if (j < W.h && !cut) {
      if (b0 < 0) b0 = j;
      continue;
    }
    if (b0 >= 0) eadk_display_push_rect((eadk_rect_t){W.x, W.y + b0, W.w, j - b0}, wbuf + b0 * W.w);
    b0 = -1;
    if (j == W.h) break;
    for (int x = W.x; x < W.x + W.w;) {
      int next = W.x + W.w, over = x;
      for (int k = 0; k < W.nt; k++) {
        if (sy < W.t[k].y || sy >= W.t[k].y + (W.t[k].big ? 18 : 14)) continue;
        int t0 = W.t[k].x, t1 = t0 + W.t[k].n * (W.t[k].big ? 10 : 7);
        if (t0 <= x && x < t1 && t1 > over) over = t1;
        else if (t0 > x && t0 < next) next = t0;
      }
      if (over > x) {
        x = over;
        continue;
      }
      eadk_display_push_rect((eadk_rect_t){x, sy, next - x, 1}, wbuf + j * W.w + (x - W.x));
      x = next;
    }
  }
  for (int k = 0; k < W.nt; k++) text(W.t[k].s, W.t[k].x, W.t[k].y, W.t[k].big, W.t[k].fg, W.t[k].bg);
}

/* -------------------------------------------------------------- Board */

static int mode, flip, cur = 0x14, sel = -1, hintsq = -1, badsq = -1, ntg, incheck;
static int anim_pc, anim_x, anim_y, anim_hide = -1;
static Move tg[256];
static char lastsan[10];

static int sqx(int s) { return 30 * (flip ? 7 - (s & 7) : s & 7); }
static int sqy(int s) { return 30 * (flip ? s >> 4 : 7 - (s >> 4)); }
static int at(int vx, int vy) { return flip ? vy * 16 + 7 - vx : (7 - vy) * 16 + vx; }

static void draw_sq(int s) {
  int X = sqx(s), Y = sqy(s), light = ((s >> 4) + s) & 1, mark = 0;
  Move lm = hp ? H[hp - 1].m : 0;
  C base = light ? SQ_L : SQ_D;
  if (s == sel || (hp && (s == MFROM(lm) || s == MTO(lm)))) base = light ? HL_L : HL_D;
  if (s == hintsq) base = mix(base, HINT, 18);
  if (s == badsq) base = mix(base, RED, 20);
  int pc = s == anim_hide ? 0 : P.b[s];
  if (sel >= 0)
    for (int i = 0; i < ntg; i++)
      if (MFROM(tg[i]) == sel && MTO(tg[i]) == s) mark = pc ? 2 : 1;
  int chk = incheck && s == P.king[P.side];
  for (int y = 0; y < 30; y++)
    for (int x = 0; x < 30; x++) {
      C c = base;
      int dx = 2 * x - 29, dy = 2 * y - 29, d2 = dx * dx + dy * dy;
      if (chk && d2 < 1300) c = mix(c, RED, d2 < 500 ? 30 : (1300 - d2) * 30 / 800);
      if (mark == 2 && d2 > 600 && d2 < 900) c = mix(c, 0, 8);
      if (pc) c = shade(pc, x, y, c);
      if (mark == 1 && d2 < 150) c = mix(c, 0, d2 < 118 ? 8 : 4);
      if (anim_pc) {
        int u = X + x - anim_x, v = Y + y - anim_y;
        if ((unsigned)u < 30 && (unsigned)v < 30) c = shade(anim_pc, u, v, c);
      }
      if (s == cur && sel != -2 && (x < 3 || x > 26 || y < 3 || y > 26)) c = light ? mix(c, INK, 13) : mix(c, WHITE, 22);
      buf[y * 30 + x] = c;
    }
  eadk_display_push_rect((eadk_rect_t){X, Y, 30, 30}, buf);
}

static void draw_board(void) {
  for (int s = 0; s < 120; s++)
    if (!(s & 0x88)) draw_sq(s);
}

static void draw_rect_squares(int x, int y) {
  for (int j = 0; j < 2; j++)
    for (int i = 0; i < 2; i++) {
      int vx = (x + i * 29) / 30, vy = (y + j * 29) / 30;
      if (vx >= 0 && vx < 8 && vy >= 0 && vy < 8) draw_sq(at(vx, vy));
    }
}

static void animate(Move m) {
  int f = MFROM(m), t = MTO(m), x0 = sqx(f), y0 = sqy(f), x1 = sqx(t), y1 = sqy(t);
  anim_pc = P.b[f], anim_hide = f, anim_x = x0, anim_y = y0;
  int n = 7;
  for (int i = 1; i <= n; i++) {
    int ox = anim_x, oy = anim_y, k = n - i;
    int e = 1024 - k * k * k * 1024 / (n * n * n); /* ease-out cubic */
    anim_x = x0 + (x1 - x0) * e / 1024, anim_y = y0 + (y1 - y0) * e / 1024;
    draw_rect_squares(ox, oy);
    draw_rect_squares(anim_x, anim_y);
    eadk_timing_msleep(14);
  }
  anim_pc = 0, anim_hide = -1;
}

/* ---------------------------------------------------------------- Bots */

static const struct {
  char name[6];
  uint8_t pc, depth, noise, blunder;
  uint16_t elo, ms;
  C col;
} BOTS[] = {
  {"Pip", PAWN, 1, 250, 45, 150, 0, RGB(0x8B, 0xC3, 0x4A)},
  {"Milo", PAWN, 1, 200, 28, 300, 0, RGB(0x4C, 0xAF, 0x50)},
  {"Luna", KNIGHT, 1, 130, 14, 450, 0, RGB(0x26, 0xA6, 0x9A)},
  {"Otto", KNIGHT, 2, 110, 10, 600, 0, RGB(0x29, 0xB6, 0xF6)},
  {"Ivy", BISHOP, 2, 85, 7, 750, 0, RGB(0x42, 0x8D, 0xF5)},
  {"Finn", BISHOP, 2, 65, 5, 900, 0, RGB(0x5C, 0x6B, 0xC0)},
  {"Nova", ROOK, 3, 55, 3, 1050, 0, RGB(0x7E, 0x57, 0xC2)},
  {"Hugo", ROOK, 4, 40, 2, 1200, 0, RGB(0xAB, 0x47, 0xBC)},
  {"Vera", QUEEN, 4, 20, 1, 1400, 0, RGB(0xEC, 0x40, 0x7A)},
  {"Rex", QUEEN, 5, 10, 0, 1600, 800, RGB(0xEF, 0x53, 0x50)},
  {"Zara", KING, 7, 0, 0, 1850, 900, RGB(0xFF, 0x70, 0x43)},
  {"Atlas", KING, 40, 0, 0, 2100, 2500, RGB(0xFF, 0xB3, 0x00)},
};
#define NBOTS 12

static const struct { uint8_t min, inc; } TC[] = {{1, 0}, {3, 0}, {3, 2}, {5, 0}, {10, 0}, {15, 10}, {30, 0}, {0, 0}};

static int bot = 4, pside = 0, pcol, tc = 3, autoflip = 1;
static int32_t tm[2];
static int prating = 800, pstreak, pdelta, spin_mode, spin_y, spin;

/* -------------------------------------------------------------- Input */

/* Home and On/Off leave the app from anywhere: main() set this jump. */
static np_jump_t leave;

static int key(int ms) {
  int32_t t = ms;
  int e = eadk_event_get(&t);
  if (e == 6 || e == 8) np_jump(leave); /* Home, On/Off */
  return e == K_EXE ? K_OK : e;
}

static int arrow(int e, int *i, int n, int horiz) {
  if (e == (horiz ? K_LEFT : K_UP) && *i > 0) return --*i, 1;
  if (e == (horiz ? K_RIGHT : K_DOWN) && *i < n - 1) return ++*i, 1;
  return 0;
}

/* ---------------------------------------------------------------- Menus */

static void button(int x, int y, int w, int h, const char *s, int on, int icon) {
  C c = on ? GREEN : CARD;
  w_begin(x, y, w, h, BG);
  w_rrect(x, y, w, h, 8, c);
  if (icon) {
    int ty = y + (h - 40) / 2;
    w_rrect(x + 8, ty, 40, 40, 9, SQ_L);
    if (icon == KING) {
      w_sprite(KING, x + 6, ty + 5, 0);
      w_sprite(KING | BLACK, x + 20, ty + 5, 0);
    } else {
      w_sprite(icon, x + 13, ty + 5, 0);
    }
    w_text(s, x + 60, y + (h - 18) / 2, 1, WHITE, c);
  } else {
    w_ctext(s, x + w / 2, y + (h - 18) / 2, 1, on ? WHITE : RGB(0xDD, 0xDC, 0xDA), c);
  }
  w_end();
}

/* Vertical list of buttons. Returns chosen index or -1. A move redraws only
   the two buttons it changes. */
static int list(const char *const *items, const uint8_t *icons, int n, int x, int w, int h, int gap, int *i) {
  int y0 = (240 - n * h - (n - 1) * gap) / 2;
  int16_t r[8][4];
  for (int k = 0; k < n && k < 8; k++) r[k][0] = x, r[k][1] = y0 + k * (h + gap), r[k][2] = w, r[k][3] = h;
  clear_around(r, n < 8 ? n : 8, BG);
  for (int k = 0; k < n; k++) button(x, y0 + k * (h + gap), w, h, items[k], k == *i, icons ? icons[k] : 0);
  for (;;) {
    int e = key(100000), o = *i;
    if (arrow(e, i, n, 0)) {
      button(x, y0 + o * (h + gap), w, h, items[o], 0, icons ? icons[o] : 0);
      button(x, y0 + *i * (h + gap), w, h, items[*i], 1, icons ? icons[*i] : 0);
    }
    if (e == K_OK) return *i;
    if (e == K_BACK) return -1;
  }
}

/* "Quit game?" over whatever is on screen: 1 to quit. Back sits right next to
   OK, so leaving the game always asks first. */
static void quit_buttons(int yes, int first) {
  w_grab(40, 116, 160, 60);
  if (first) w_rrect(40, 64, 160, 112, 12, BG);
  else w_rrect(46, 120, 148, 44, 0, BG);
  for (int k = 0; k < 2; k++) {
    C c = k == yes ? GREEN : CARD;
    w_rrect(50 + k * 74, 124, 66, 36, 8, c);
    w_ctext(k ? T("Yes") : T("No"), 83 + k * 74, 133, 1, WHITE, c);
  }
  w_end();
}

static int confirm_quit(void) {
  w_grab(40, 64, 160, 52);
  w_rrect(40, 64, 160, 112, 12, BG);
  w_ctext(T("Quit game?"), 120, 84, 1, WHITE, BG);
  w_end();
  int yes = 0;
  quit_buttons(yes, 1);
  for (;;) {
    int e = key(100000);
    if (e == K_LEFT || e == K_RIGHT) quit_buttons(yes = !yes, 0);
    if (e == K_OK) return yes;
    if (e == K_BACK) return 0;
  }
}

static int main_menu(int *i) {
  static const char *const items[] = {T("Play"), T("Puzzles"), T("2 Players")};
  static const uint8_t icons[] = {KNIGHT | BLACK, QUEEN | BLACK, KING};
  for (;;) {
    int k = list(items, icons, 3, 60, 200, 56, 14, i);
    if (k >= 0 || confirm_quit()) return k;
  }
}

static void w_avatar(int b, int cx, int cy, int r) {
  w_disc(cx, cy, r, BOTS[b].col);
  w_sprite(BOTS[b].pc, cx - 15, cy - 16, 0);
}

static void bot_row(int k) {
  C c = k == bot ? CARD : BG;
  w_begin(6, k * 20, 148, 20, BG);
  w_rrect(6, k * 20, 148, 20, 6, c);
  w_disc(20, k * 20 + 10, 5, BOTS[k].col);
  w_text(BOTS[k].name, 32, k * 20 + 3, 0, k == bot ? WHITE : RGB(0xC8, 0xC7, 0xC5), c);
  char e[6];
  itoa(BOTS[k].elo, e);
  w_text(e, 146 - 7 * (int)strlen(e), k * 20 + 3, 0, DIM, c);
  w_end();
}

/* the chosen bot on the right: its avatar, then its name and Elo, whose old
   text is covered by the new one and the margins around it */
static void bot_info(void) {
  w_begin(200, 22, 80, 80, BG);
  w_avatar(bot, 240, 62, 40);
  w_end();
  w_begin(170, 110, 140, 44, BG);
  w_ctext(BOTS[bot].name, 240, 114, 1, WHITE, BG);
  char e[6];
  itoa(BOTS[bot].elo, e);
  w_ctext(e, 240, 136, 0, DIM, BG);
  w_end();
}

static void side_button(int k) {
  int x = 184 + k * 40;
  w_begin(x, 172, 32, 32, BG);
  w_rrect(x, 172, 32, 32, 8, k == pside ? GREEN : CARD);
  w_sprite(KING | (k == 1 ? 16 : k == 2 ? BLACK : 0), x + 1, 173, 0);
  w_end();
}

static int bot_select(void) {
  int16_t r[NBOTS + 5][4];
  for (int k = 0; k < NBOTS; k++) r[k][0] = 6, r[k][1] = (int16_t)(k * 20), r[k][2] = 148, r[k][3] = 20;
  static const int16_t info[5][4] = {{200, 22, 80, 80}, {170, 110, 140, 44}, {184, 172, 32, 32}, {224, 172, 32, 32},
                                     {264, 172, 32, 32}};
  memcpy(r + NBOTS, info, sizeof info);
  clear_around(r, NBOTS + 5, BG);
  for (int k = 0; k < NBOTS; k++) bot_row(k);
  bot_info();
  for (int k = 0; k < 3; k++) side_button(k);
  for (;;) {
    int e = key(100000), o = bot, os = pside;
    arrow(e, &bot, NBOTS, 0);
    arrow(e, &pside, 3, 1);
    if (bot != o) {
      bot_row(o);
      bot_row(bot);
      bot_info();
    }
    if (pside != os) {
      side_button(os);
      side_button(pside);
    }
    if (e == K_OK) return 1;
    if (e == K_BACK) return 0;
  }
}

static void tc_card(int k) {
  int x = 12 + (k & 3) * 76, y = 64 + (k >> 2) * 64;
  char s[8], *o = s;
  if (TC[k].min) {
    o = itoa(TC[k].min, o);
    *o++ = '+';
    itoa(TC[k].inc, o);
  } else {
    strcpy(s, "\xE2\x88\x9E");
  }
  C c = k == tc ? GREEN : CARD;
  w_begin(x, y, 68, 52, BG);
  w_rrect(x, y, 68, 52, 8, c);
  w_ctext(s, x + 34, y + 17, 1, WHITE, c);
  w_end();
}

static int time_select(void) {
  int16_t r[8][4];
  for (int k = 0; k < 8; k++)
    r[k][0] = (int16_t)(12 + (k & 3) * 76), r[k][1] = (int16_t)(64 + (k >> 2) * 64), r[k][2] = 68, r[k][3] = 52;
  clear_around(r, 8, BG);
  for (int k = 0; k < 8; k++) tc_card(k);
  for (;;) {
    int e = key(100000), o = tc;
    if (e == K_LEFT && tc & 3) tc--;
    if (e == K_RIGHT && (tc & 3) < 3) tc++;
    if (e == K_UP && tc > 3) tc -= 4;
    if (e == K_DOWN && tc < 4) tc += 4;
    if (tc != o) {
      tc_card(o);
      tc_card(tc);
    }
    if (e == K_OK) return 1;
    if (e == K_BACK) return 0;
  }
}

/* The pause menu: a list drawn over the dimmed board, with Quit game under it,
   small. Returns the chosen index, or -1 for Back. */
static void overlay_item(const char *const *items, int n, int y, int it, int on) {
  C c = on ? GREEN : it == n ? CARD : BG;
  if (it < n) {
    w_begin(46, y + 6 + it * 36, 148, 36, BG);
    w_rrect(46, y + 6 + it * 36, 148, 36, 8, c);
    w_ctext(items[it], 120, y + 15 + it * 36, 1, WHITE, c);
  } else {
    int qy = y + n * 36 + 12;
    w_begin(70, qy, 100, 20, BG);
    w_rrect(70, qy, 100, 20, 7, c);
    w_ctext(T("Quit game"), 120, qy + 3, 0, on ? WHITE : RGB(0xDD, 0xDC, 0xDA), c);
  }
  w_end();
}

static int overlay(const char *const *items, int n) {
  int h = n * 36 + 38, y = (240 - h) / 2, i = 0;
  for (;;) {
    dim(0, 0, 240, 240);
    rrect(40, y, 160, h, 12, BG);
    for (int k = 0; k <= n; k++) overlay_item(items, n, y, k, k == i);
    for (;;) {
      int e = key(100000), o = i;
      if (arrow(e, &i, n + 1, 0)) {
        overlay_item(items, n, y, o, 0);
        overlay_item(items, n, y, i, 1);
      }
      if (e == K_BACK) return -1;
      if (e != K_OK) continue;
      if (i < n) return i;
      if (confirm_quit()) np_jump(leave);
      break;
    }
    draw_board(); /* not quitting: the board again, and the menu over it */
  }
}

/* --------------------------------------------------------------- Panel */

/* the kinds of puzzles: their menu, and the label over the panel */
static const char *const PUZ_KINDS[5] = {T("Mix"), T("Mate in 1"), T("Mate in 2"), T("Mate in 3"), T("Best move")};

static const uint8_t START[7] = {0, 8, 2, 2, 2, 1, 0};
static const uint8_t WORTH[7] = {0, 1, 3, 3, 5, 9, 0};

static int material(int c) {
  int m = 0;
  for (int t = PAWN; t < KING; t++) m += WORTH[t] * P.cnt[c << 3 | t];
  return m;
}

/* Pieces captured by side c, drawn as small icons from (x, y). */
static void captures(int c, int y) {
  int x = 244;
  for (int t = QUEEN; t >= PAWN; t--) {
    int n = START[t] - P.cnt[(!c) << 3 | t];
    for (int k = 0; k < n; k++, x += 7) {
      if (x > 302) x = 244, y += 15;
      w_sprite(t | (!c) << 3, x, y, 1);
    }
    if (n > 0) x += 6;
  }
  int d = material(c) - material(!c);
  if (d > 0) {
    char s[6] = "+";
    itoa(d, s + 1);
    w_text(s, x > 290 ? 244 : x + 4, x > 290 ? y + 15 : y, 0, DIM, BG);
  }
}

static void fmt_clock(int32_t ms, char *o) {
  if (ms < 0) ms = 0;
  if (ms < 10000) {
    o = itoa(ms / 1000, o);
    *o++ = '.', *o++ = '0' + ms / 100 % 10, *o = 0;
  } else {
    int s = (ms + 999) / 1000;
    o = itoa(s / 60, o);
    *o++ = ':', *o++ = '0' + s % 60 / 10, *o++ = '0' + s % 10, *o = 0;
  }
}

static void pill(int c, int y) {
  int on = P.side == c;
  C bg = on ? (tm[c] < 10000 && TC[tc].min ? RED : WHITE) : CARD, fg = on ? (bg == RED ? WHITE : INK) : DIM;
  w_rrect(244, y, 72, 34, 8, bg);
  if (TC[tc].min) {
    char s[8];
    fmt_clock(tm[c], s);
    w_ctext(s, 280, y + 8, 1, fg, bg);
  } else {
    w_sprite(KING | c << 3, 265, y + 2, 0);
  }
}

/* one clock, ticking: redrawn in one piece */
static void clock_pill(int c, int y) {
  w_begin(244, y, 72, 34, BG);
  pill(c, y);
  w_end();
}

static void feedback(int ok) {
  w_begin(244, 84, 72, 76, BG);
  if (ok >= 0) {
    w_disc(280, 120, 26, ok ? GREEN : RED);
    if (ok) {
      w_stroke(268, 121, 276, 129, 5, WHITE);
      w_stroke(276, 129, 292, 112, 5, WHITE);
    } else {
      w_stroke(270, 110, 290, 130, 5, WHITE);
      w_stroke(290, 110, 270, 130, 5, WHITE);
    }
  }
  w_end();
}

static void rating_texts(void) {
  char s[8];
  itoa(prating, s);
  w_ctext(s, 280, 186, 1, WHITE, BG);
  if (pdelta) {
    char *o = s;
    if (pdelta > 0) *o++ = '+';
    itoa(pdelta, o);
    w_ctext(s, 280, 208, 0, pdelta > 0 ? GREEN : RED, BG);
  }
}

static void puzzle_rating(void) {
  w_begin(240, 176, 80, 64, BG);
  rating_texts();
  w_end();
}

/* everything in the panel, into the piece being drawn (the panel is drawn in
   two halves, each clipping what it does not cover) */
static void panel_items(void) {
  int top = !flip;
  if (mode == M_PUZ) {
    int s = !PZ.start.side;
    C bg = s ? RGB(0x10, 0x10, 0x10) : WHITE;
    w_rrect(244, 8, 72, 34, 8, bg);
    w_ctext(PUZ_KINDS[PZ.mate ? PZ.mate : 4], 280, 18, 0, s ? WHITE : INK, bg);
    if (pstreak > 1) {
      char k[NP_TEXT_EXTRA ? 32 : 12];
      strcpy(itoa(pstreak, k), T(" in a row"));
      w_ctext(k, 280, 52, 0, DIM, BG);
    }
    rating_texts();
    return;
  }
  if (mode == M_BOT) {
    w_avatar(bot, 280, 24, 18);
    w_ctext(BOTS[bot].name, 280, 46, 1, WHITE, BG);
    char e[6];
    itoa(BOTS[bot].elo, e);
    w_ctext(e, 280, 66, 0, DIM, BG);
    captures(top, 88);
  } else {
    pill(top, 6);
    pill(!top, 200);
    captures(top, 46);
  }
  captures(!top, mode == M_BOT ? 200 : 164);
  if (hp) {
    char n[8];
    strcpy(itoa((hp + 1) / 2, n), P.side ? "." : "...");
    w_ctext(n, 280, 112 - (mode == M_2P) * 8, 0, DIM, BG);
    w_ctext(lastsan, 280, 128 - (mode == M_2P) * 8, 1, WHITE, BG);
  }
}

static void panel(void) {
  for (int half = 0; half < 2; half++) {
    w_begin(240, half * 120, 80, 120, BG);
    panel_items();
    w_end();
  }
}

/* ------------------------------------------------------------ Moving */

static void refresh(void) {
  sel = -1;
  ntg = legal(tg);
  incheck = in_check();
}

static void set_lastsan(void) {
  lastsan[0] = 0;
  if (!hp) return;
  Move m = H[hp - 1].m;
  unmake();
  san(m, lastsan);
  make(m);
}

static void do_move(Move m) {
  san(m, lastsan);
  animate(m);
  make(m);
  refresh();
  draw_board();
  panel();
}

static int promo_pick(int to) {
  static const uint8_t order[4] = {3, 0, 2, 1};
  int X = sqx(to), Y = sqy(to), dir = Y ? -30 : 30, i = 0, c = P.side << 3;
  for (;;) {
    for (int k = 0; k < 4; k++) {
      fill(X, Y + k * dir, 30, 30, k == i ? GREEN : WHITE);
      sprite(c | (order[k] + KNIGHT), X, Y + k * dir, 0);
    }
    int e = key(100000);
    if (e == K_UP || e == K_DOWN) i = (i + ((e == K_DOWN) == (dir > 0) ? 1 : 3)) & 3;
    if (e == K_OK || e == K_BACK) {
      draw_board();
      return e == K_OK ? order[i] : -1;
    }
  }
}

/* Cursor handling. Returns a move, 0, or -1 when back is pressed with nothing selected. */
static int human(int e) {
  if (e <= K_RIGHT) {
    int f = cur & 7, r = cur >> 4, d = flip ? -1 : 1, o = cur;
    if (e == K_LEFT) f = (f - d) & 7;
    if (e == K_RIGHT) f = (f + d) & 7;
    if (e == K_UP) r = (r + d) & 7;
    if (e == K_DOWN) r = (r - d) & 7;
    cur = r * 16 + f;
    draw_sq(o);
    draw_sq(cur);
  } else if (e == K_OK) {
    if (sel >= 0)
      for (int i = 0; i < ntg; i++)
        if (MFROM(tg[i]) == sel && MTO(tg[i]) == cur) {
          Move m = tg[i];
          if (TYPE(P.b[sel]) == PAWN && (cur >> 4 == 0 || cur >> 4 == 7)) {
            int p = promo_pick(cur);
            if (p < 0) return 0;
            m = MOVE(sel, cur, p);
          }
          return m;
        }
    int p = P.b[cur];
    sel = p && COLOR(p) == P.side && sel != cur ? cur : -1;
    draw_board();
  } else if (e == K_BACK) {
    if (sel < 0) return -1;
    sel = -1;
    draw_board();
  } else if (e == K_UNDO) {
    return -2;
  }
  return 0;
}

/* ---------------------------------------------------------------- Game */

static void over_button(int k, int on) {
  static const char *const b[2] = {T("Rematch"), T("Menu")};
  C c = on ? GREEN : CARD;
  w_begin(30 + k * 94, 130, 86, 38, BG);
  w_rrect(30 + k * 94, 130, 86, 38, 8, c);
  w_ctext(b[k], 73 + k * 94, 140, 1, WHITE, c);
  w_end();
}

static int game_over(const char *title, const char *why) {
  eadk_timing_msleep(400);
  dim(0, 0, 240, 240);
  rrect(20, 58, 200, 124, 12, BG);
  ctext(title, 120, 74, 1, WHITE, BG);
  ctext(why, 120, 98, 0, DIM, BG);
  int i = 0;
  for (int k = 0; k < 2; k++) over_button(k, k == i);
  for (;;) {
    int e = key(100000), o = i;
    if (arrow(e, &i, 2, 1)) {
      over_button(o, 0);
      over_button(i, 1);
    }
    if (e == K_OK) return !i;
    if (e == K_BACK) return 0;
  }
}

static void undo(int n) {
  while (n-- && hp) unmake();
  if (mode == M_2P && autoflip) flip = P.side;
  set_lastsan();
  refresh();
  draw_board();
  panel();
}

static void game(int md) {
  static const char *const why[] = {"", T("Checkmate"), T("Stalemate"), T("50-move rule"), T("Repetition"),
                                     T("Insufficient material")};
again:
  mode = md;
  ch_reset();
  refresh();
  lastsan[0] = 0;
  pcol = pside == 1 ? eadk_random() & 1 : pside >> 1;
  flip = md == M_BOT && pcol;
  int curs[2] = {0x14, 0x64};
  cur = curs[flip];
  tm[0] = tm[1] = TC[tc].min * 60000;
  uint64_t last = eadk_timing_millis();
  draw_board();
  panel();
  for (;;) {
    const char *title = 0, *reason = 0;
    int st = status(), winner = -1;
    if (st) {
      reason = why[st];
      if (st == CHECKMATE) winner = !P.side;
    }
    if (md == M_2P && TC[tc].min && hp) {
      uint64_t now = eadk_timing_millis();
      int32_t before = tm[P.side];
      tm[P.side] -= now - last;
      last = now;
      if (tm[P.side] <= 0) winner = !P.side, reason = T("Timeout");
      if (before / 100 != tm[P.side] / 100 && (tm[P.side] < 10000 || before / 1000 != tm[P.side] / 1000))
        clock_pill(P.side, (P.side == !flip) ? 6 : 200);
    } else {
      last = eadk_timing_millis();
    }
    if (reason) {
      if (md == M_BOT) title = winner < 0 ? T("Draw") : winner == pcol ? T("You won") : BOTS[bot].name;
      else title = winner < 0 ? T("Draw") : winner ? T("Black won") : T("White won");
      static char t[NP_TEXT_EXTRA ? 40 : 12];
      if (md == M_BOT && winner >= 0 && winner != pcol) strcpy(strcpy(t, title) + strlen(title), T(" won")), title = t;
      if (game_over(title, reason)) goto again;
      return;
    }
    if (md == M_BOT && P.side != pcol) {
      uint64_t t0 = eadk_timing_millis();
      spin_mode = 2, spin_y = 80;
      Move m = bot_move(BOTS[bot].depth, BOTS[bot].noise, BOTS[bot].blunder, BOTS[bot].ms);
      spin_mode = 0;
      int32_t wait = 450 - (int32_t)(eadk_timing_millis() - t0);
      if (wait > 0) eadk_timing_msleep(wait);
      do_move(m);
      continue;
    }
    int e = key(md == M_2P && TC[tc].min && hp ? 50 : 100000);
    int m = human(e);
    if (m == -2) {
      undo(md == M_BOT ? (hp >= 2) * 2 : 1);
      continue;
    }
    if (m == -1) {
      static const char *const pb[] = {T("Undo"), T("Flip"), T("Resign"), T("Menu")};
      static const char *const p2[] = {T("Undo"), T("Flip"), T("Draw"), T("Resign"), T("Menu")};
      int k = md == M_BOT ? overlay(pb, 4) : overlay(p2, 5);
      if (md == M_BOT && k >= 2) k++;
      if (k == 0) {
        undo(md == M_BOT ? (hp >= 2) * 2 : 1);
        continue;
      }
      if (k == 1) {
        flip ^= 1;
        autoflip = flip == P.side;
      }
      if (k == 2 || k == 3) {
        title = k == 2 ? T("Draw") : P.side ? T("White won") : T("Black won");
        if (md == M_BOT && k == 3) {
          static char t[NP_TEXT_EXTRA ? 40 : 12];
          strcpy(strcpy(t, BOTS[bot].name) + strlen(BOTS[bot].name), T(" won"));
          title = t;
        }
        if (game_over(title, k == 2 ? T("Agreement") : T("Resignation"))) goto again;
        return;
      }
      if (k == 4) return;
      draw_board();
      panel();
      last = eadk_timing_millis();
      continue;
    }
    if (m > 0) {
      int mover = P.side;
      curs[mover] = cur;
      do_move(m);
      if (md == M_2P) {
        tm[mover] += TC[tc].inc * 1000;
        if (autoflip && flip != P.side) {
          eadk_timing_msleep(250);
          flip = P.side;
          cur = curs[P.side];
          draw_board();
        }
        panel();
      }
    }
  }
}

/* ------------------------------------------------------------- Puzzles */

/* Progress animation while the engine works: 0 off, 1 ring (puzzles), 2 dots (bot). */
void ui_tick(void) {
  static uint64_t t;
  uint64_t now = eadk_timing_millis();
  if (!spin_mode || now - t < 70) return;
  t = now;
  spin++;
  for (int k = 0; k < 8; k++) {
    static const int8_t dx[8] = {0, 11, 16, 11, 0, -11, -16, -11}, dy[8] = {-16, -11, 0, 11, 16, 11, 0, -11};
    if (spin_mode == 1) fill(278 + dx[(spin - k) & 7], 118 + dy[(spin - k) & 7], 5, 5, mix(BG, WHITE, 32 - k * 4));
    else if (k < 3) fill(271 + k * 8, spin_y, 3, 3, (spin % 3) == k ? WHITE : CARD);
  }
}

/* -------------------------------------------------------------- Saving */

/* The puzzle rating and streak, and the last bot, side and clock. */
static const char SAVE_NAME[] = "numchess.sav";

static void save(void) {
  uint8_t s[8] = {'C', 1, (uint8_t)prating, (uint8_t)(prating >> 8), (uint8_t)pstreak, (uint8_t)(pstreak >> 8),
                  (uint8_t)bot, (uint8_t)(pside | tc << 2)};
  ef_write(SAVE_NAME, s, sizeof s);
}

static void load(void) {
  uint32_t n;
  const uint8_t *s = ef_read(SAVE_NAME, &n);
  if (!s || n != 8 || s[0] != 'C' || s[1] != 1) return;
  int r = s[2] | s[3] << 8;
  if (r >= 100 && r <= 4000) prating = r, pstreak = s[4] | s[5] << 8;
  if (s[6] < NBOTS) bot = s[6];
  if ((s[7] & 3) < 3) pside = s[7] & 3;
  tc = s[7] >> 2 & 7;
}

static int quit(void) {
  save();
  return np_app_end();
}

static void rate(int won) {
  int e = 16 + (prating - PZ.rating) / 25;
  e = e < 2 ? 2 : e > 30 ? 30 : e;
  pdelta = won ? 32 - e : -e;
  prating += pdelta;
  if (prating < 100) prating = 100;
  pstreak = won ? pstreak + 1 : 0;
  save();
}

/* Generate the next puzzle while the solved one stays on screen. */
static Puzzle next;
static int have_next;
static void pregenerate(int kind) {
  static Undo h[16];
  Puzzle cur = PZ;
  Pos p = P;
  int n = hp;
  memcpy(h, H, sizeof h);
  spin_mode = 2, spin_y = 164;
  puzzle_gen(kind, prating);
  spin_mode = 0;
  next = PZ, have_next = 1;
  PZ = cur, P = p, hp = n;
  memcpy(H, h, sizeof h);
  fill(270, 164, 24, 3, BG);
}

static void puzzles(int kind) {
  mode = M_PUZ;
  have_next = 0;
  for (;;) {
    if (have_next) {
      PZ = next, have_next = 0;
    } else {
      fill(240, 0, 80, 240, BG);
      sel = -2;
      draw_board();
      dim(0, 0, 240, 240);
      spin_mode = 1;
      puzzle_gen(kind, prating);
      spin_mode = 0;
    }
    P = PZ.start;
    hp = 0;
    flip = !P.side;
    cur = flip ? 0x74 : 0x04;
    refresh();
    sel = -2;
    lastsan[0] = 0;
    pdelta = 0;
    draw_board();
    panel();
    eadk_timing_msleep(500);
    do_move(PZ.pre);
    int step = 0, failed = 0, done = 0, rem = PZ.mate;
    for (;;) {
      int e = key(100000), m = 0;
      if (done) {
        if (e == K_OK) break;
        if (e != K_BACK) continue;
        m = -1;
      } else {
        m = human(e);
      }
      if (m == -1) {
        static const char *const it[] = {T("Hint"), T("Solution"), T("Next"), T("Menu")};
        int k = overlay(it, 4);
        draw_board();
        if (k == 3) return;
        if (k == 2) {
          if (!done && !failed) rate(0);
          break;
        }
        if (k < 0 || done) continue;
        Move sm = PZ.mate ? (step ? mate_move(rem) : PZ.sol[0]) : PZ.sol[step];
        if (k == 0) {
          hintsq = MFROM(sm);
          draw_sq(hintsq);
          continue;
        }
        if (!failed) failed = 1, rate(0);
        m = sm;
      }
      if (m <= 0) continue;
      hintsq = -1;
      make(m);
      int ok = (!step && m == PZ.sol[0]) || status() == CHECKMATE ||
               (PZ.mate ? mated_within(rem - 1) : m == PZ.sol[step]);
      unmake();
      if (!ok) {
        do_move(m);
        badsq = MTO(m);
        draw_sq(badsq);
        if (!failed) failed = 1, rate(0);
        feedback(0);
        puzzle_rating();
        eadk_timing_msleep(700);
        badsq = -1;
        unmake();
        set_lastsan();
        refresh();
        draw_board();
        continue;
      }
      do_move(m);
      step++;
      if (status() == CHECKMATE || (!PZ.mate && step >= PZ.nsol)) {
        if (!failed) rate(1);
        done = 1;
        sel = -2;
        draw_board();
        panel();
        feedback(1);
        pregenerate(kind);
        continue;
      }
      feedback(1);
      eadk_timing_msleep(350);
      feedback(-1);
      do_move(PZ.mate ? defend(--rem) : PZ.sol[step]);
      step++;
    }
    hintsq = -1;
  }
}

static int puzzle_menu(int *i) {
  return list(PUZ_KINDS, 0, 5, 70, 180, 38, 8, i);
}

int main(void) {
  np_app_begin();
  if (np_save_jump(leave)) return quit();
  ch_init();
  load();
  int m = 0, pk = 0;
  for (;;) {
    if (main_menu(&m) < 0) return quit();
    if (m == 0 && bot_select()) game(M_BOT);
    if (m == 1 && puzzle_menu(&pk) >= 0) puzzles(pk);
    if (m == 2 && time_select()) game(M_2P);
  }
}
