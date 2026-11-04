/* Space Invaders: Taito's arcade game of 1978, as it plays on the Midway cabinet.
 *
 * The arcade draws in a picture of one bit a pixel, 224 wide and 256 high, white on black, seen
 * through strips of coloured film: red over the saucer, green over the bunkers, the cannon and the
 * cannons left. Here it is the same picture, pixel for pixel, in the middle of the calculator's screen;
 * only its empty margins at the top and the bottom are left out. It works the arcade's way:
 * what moves is wiped and drawn again in the picture, a shot hits whatever it finds lit there, and
 * a burst leaves its shape cut out when it goes, so the bunkers and the ground wear away. The rules
 * are the arcade's:
 * - the invaders move one at a time, one each frame, so the rack speeds up as it thins out; it
 *   drops 8 pixels at each edge, wipes out the bunkers it reaches, and each wave starts lower;
 * - their three shots: the rolling one, from the column above the cannon, and the plunger and the
 *   squiggly one, from columns in a fixed order; a shot waits until the others have gone far
 *   enough, and less far as the score grows;
 * - the mystery saucer is worth 50 to 300 points, from how many shots were fired: the 23rd shot,
 *   then every 15th, gets 300; a new cannon at 1500 points.
 *
 * Only the rows that change are sent to the screen, in the launcher's strips (launcher/src/gfx.c).
 * Inside NumPlay it uses the launcher's shapes, fonts and files; on its own it brings them along. */
#include <eadk.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"

#if NP_MODULE /* (np_file_read and np_file_write: NumPlay's, launcher/src/np.h) */
#define np_app_begin()
#define np_app_end() 0
#else
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
#ifndef HOST /* (a computer build of the tests has no app header) */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Space Invaders";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

/* the keys used, in 32 bits: 4, 5, 6, 2 and EXE come down from the high word */
#define KEY(k) (1u << ((k) - ((k) >= 32) * 24))
#define X0 48   /* the arcade's 224 columns, in the middle of the screen */
#define OFF 7   /* and its rows from row 7 on */
#define TOPY 32 /* the cannon's shots burst here, at the top of the red film */
#define UY 40   /* the saucer's row */
#define SHY 192 /* the bunkers' top row */
#define PY 216  /* the cannon's */
#define GY 239  /* the ground */
#define LY 240  /* the lives and the credits */
#define ALL 0x3FFFFFFFu

#define WHITE 0xFFFF
#define RED HEX(0xFF4848)
#define GREEN HEX(0x3CF03C)

/* the arcade's pictures, from its ROM, a row of 16 pixels each, the top bit the leftmost */
static const uint16_t art[][8] = {
  {0x03C0, 0x1FF8, 0x3FFC, 0x399C, 0x3FFC, 0x0660, 0x0DB0, 0x300C}, /* the octopus, 10 points */
  {0x03C0, 0x1FF8, 0x3FFC, 0x399C, 0x3FFC, 0x0E70, 0x1998, 0x0C30},
  {0x0410, 0x0220, 0x07F0, 0x0DD8, 0x1FFC, 0x17F4, 0x1414, 0x0360}, /* the crab, 20 */
  {0x0410, 0x1224, 0x17F4, 0x1DDC, 0x1FFC, 0x0FF8, 0x0410, 0x0808},
  {0x0180, 0x03C0, 0x07E0, 0x0DB0, 0x0FF0, 0x0240, 0x05A0, 0x0A50}, /* the squid, 30 */
  {0x0180, 0x03C0, 0x07E0, 0x0DB0, 0x0FF0, 0x05A0, 0x0810, 0x0420},
  {0x0440, 0x2108, 0x1010, 0x0820, 0x600C, 0x0820, 0x1110, 0x2448}, /* an invader hit */
  {0x0080, 0x01C0, 0x01C0, 0x1FFC, 0x3FFE, 0x3FFE, 0x3FFE, 0x3FFE}, /* the cannon */
  {0x0200, 0x0010, 0x02A0, 0x1200, 0x01B0, 0x45A8, 0x1FE4, 0x3FF5}, /* the cannon hit, two pictures */
  {0x1004, 0x8219, 0x10C0, 0x0202, 0x4B31, 0x21C4, 0x1FF0, 0x37F2},
  {0x0000, 0x07E0, 0x1FF8, 0x3FFC, 0x6DB6, 0xFFFF, 0x399C, 0x1008}, /* the saucer */
  {0x1281, 0x0806, 0x51E3, 0x03F9, 0x0754, 0x11F1, 0x40A3, 0x1110}, /* the saucer hit: 24 columns, 4 */
  {0x4800, 0x1000, 0x0000, 0xC800, 0xE400, 0x8000, 0x1000, 0x8000}, /* to the saucer's left */
  {0x8900, 0x2200, 0x7E00, 0xFF00, 0xFF00, 0x7E00, 0x2400, 0x9100}, /* the cannon's shot bursting */
  {0x2000, 0x8800, 0x3400, 0x7800, 0xB800, 0x7C00, 0xB800, 0x5400}, /* an invader's shot bursting */
  {0x8000, 0x8000, 0x8000, 0x8000},                                 /* the cannon's shot */
  {0x0FFF, 0x1FFF, 0x3FFF, 0x7FFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, /* a bunker, in four */
  {0xC000, 0xE000, 0xF000, 0xF800, 0xFC00, 0xFC00, 0xFC00, 0xFC00},
  {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFE01, 0xFC00, 0xF800, 0xF800},
  {0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0xFC00, 0x7C00, 0x7C00},
  {0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF}, /* a whole cell, to wipe it */
  {0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00, 0xFF00}, /* a letter's */
};
enum { A_BOOM = 6, A_CANNON, A_WRECK, A_SAUCER = 10, A_SBOOM, A_SBOOM2, A_PBOOM, A_ABOOM, A_SHOT, A_BUNKER, A_CELL = 20, A_LETTER };
/* the arcade's letters, 5 columns of 7 pixels (the lowest bit on top) */
static const char glyphs[] = "0123456789ABCDEGHILMNOPRSTVY<>-*=?UQJF";
static const uint8_t font[][5] = {
  {0x3E, 0x51, 0x49, 0x45, 0x3E}, {0x00, 0x42, 0x7F, 0x40, 0x00}, {0x62, 0x51, 0x49, 0x49, 0x46},
  {0x22, 0x41, 0x49, 0x49, 0x36}, {0x18, 0x14, 0x12, 0x7F, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
  {0x3C, 0x4A, 0x49, 0x49, 0x31}, {0x01, 0x71, 0x09, 0x05, 0x03}, {0x36, 0x49, 0x49, 0x49, 0x36},
  {0x46, 0x49, 0x49, 0x29, 0x1E}, {0x7E, 0x11, 0x11, 0x11, 0x7E}, {0x7F, 0x49, 0x49, 0x49, 0x36},
  {0x3E, 0x41, 0x41, 0x41, 0x22}, {0x7F, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x49, 0x49, 0x49, 0x41},
  {0x3E, 0x41, 0x49, 0x49, 0x7A}, {0x7F, 0x08, 0x08, 0x08, 0x7F}, {0x00, 0x41, 0x7F, 0x41, 0x00},
  {0x7F, 0x40, 0x40, 0x40, 0x40}, {0x7F, 0x02, 0x0C, 0x02, 0x7F}, {0x7F, 0x04, 0x08, 0x10, 0x7F},
  {0x3E, 0x41, 0x41, 0x41, 0x3E}, {0x7F, 0x09, 0x09, 0x09, 0x06}, {0x7F, 0x09, 0x19, 0x29, 0x46},
  {0x26, 0x49, 0x49, 0x49, 0x32}, {0x01, 0x01, 0x7F, 0x01, 0x01}, {0x1F, 0x20, 0x40, 0x20, 0x1F},
  {0x07, 0x08, 0x70, 0x08, 0x07}, {0x08, 0x14, 0x22, 0x41, 0x00}, {0x00, 0x41, 0x22, 0x14, 0x08},
  {0x08, 0x08, 0x08, 0x08, 0x08}, {0x22, 0x14, 0x7F, 0x14, 0x22}, {0x14, 0x14, 0x14, 0x14, 0x14},
  {0x02, 0x01, 0x51, 0x09, 0x06}, {0x3F, 0x40, 0x40, 0x40, 0x3F}, {0x3E, 0x41, 0x51, 0x21, 0x5E},
  {0x20, 0x40, 0x41, 0x3F, 0x01}, {0x7F, 0x09, 0x09, 0x09, 0x01}, /* (U Q J F: the pause menu's) */
};
/* the attract screen, typed out a letter at a time, and where its lines go */
static const char attract[] = "PLAY\0SPACE INVADERS\0*SCORE ADVANCE TABLE*\0=? MYSTERY\0=30 POINTS\0=20 POINTS\0=10 POINTS";
static const uint8_t attract_at[7][2] = {{96, 64}, {56, 88}, {28, 112}, {80, 128}, {80, 144}, {80, 160}, {80, 176}};
#define TYPED (int)(sizeof attract - 1)
/* the saucer's points, tens of them, by how many shots were fired */
static const uint8_t ufo[15] = {10, 5, 5, 10, 15, 10, 10, 5, 30, 10, 10, 10, 5, 15, 10};
/* the columns the plunger shot (16) and the squiggly one (15) come from, in turn */
static const uint8_t cols[31] = {1, 7, 1, 1, 1, 4, 11, 1, 6, 3, 1, 1, 11, 9, 2, 8,
                                 11, 1, 6, 3, 1, 1, 11, 9, 2, 8, 2, 11, 4, 7, 10};
/* how high the rack starts, wave after wave (the arcade's numbers: the bottom row's top is 248 less it) */
static const uint8_t starts[9] = {0x78, 0x60, 0x50, 0x48, 0x48, 0x48, 0x40, 0x40, 0x40};

/* invaders.sav */
static struct {
  uint8_t magic, version, pad[2];
  uint32_t hi;
} sv;
#define SAVE_NAME "invaders.sav"

static color_t strip[SCREEN_W * STRIP_H];
static uint8_t vram[240][30]; /* the picture from the arcade's row 7, a bit a pixel, the top bit the leftmost */
static uint16_t pic[8];       /* a picture made on the spot: a letter, an invader's shot */
enum { S_TITLE, S_PLAY, S_OVER };
enum { OV_NONE, OV_PAUSE, OV_QUIT };
static uint32_t dirty, frames;
static int state, ov, sel, typed, line, col, t_over; /* typed: the letters shown, of a line, col */
static int score, lives, wave, shots, bonus, clear_t;
/* the cannon (the left of its 16 columns), its shot and the shot's burst */
static int px, die, fire_x, fire_y, fire_held, pb_x, pb_y, pb_t;
/* the rack: each invader's 16 x 8 cell, and whether it is alive (1) and which picture it shows (2) */
static uint8_t ax[55], ay[55], st[55];
static int live, spawn, cur, dir, rdx, rdy, edge, ref_x, boom_i, boom_t;
/* the invaders' shots: rolling, plunger, squiggly */
typedef struct {
  uint8_t on, x, y, f, steps, t, bx, by; /* t: the frames its burst has left, at bx, by */
} shot_t;
static shot_t as[3];
static int next[3]; /* where the plunger and squiggly shots are in cols[] */
/* the saucer: where, which way, the frames until the next one; hit: its burst, then its points */
static int ux, udx, u_wait, u_hit, u_pts;

/* ------------------------------------------------------------ the picture */
/* a picture at x, y (the arcade's): lit, wiped, or only looked at; whether it met anything lit */
enum { SET, CLEAR, TEST };
static int blit(const uint16_t *a, int x, int y, int mode) {
  int hit = 0;
  for (int r = 0; r < 8; r++) {
    unsigned row = (unsigned)(y + r - OFF);
    uint32_t v = (uint32_t)a[r] << 16 >> (x & 7);
    for (int k = 0; row < 240 && k < 3; k++, v <<= 8) {
      uint8_t *p = &vram[row][(x >> 3) + k], b = (uint8_t)(v >> 24);
      hit |= *p & b;
      if (mode == SET) *p |= b;
      if (mode == CLEAR) *p &= (uint8_t)~b;
    }
  }
  if (mode != TEST) {
    int a0 = (y - OFF) >> 3, a1 = (y + 7 - OFF) >> 3;
    for (a0 = a0 < 0 ? 0 : a0; a0 <= a1 && a0 < 30; a0++) dirty |= 1u << a0;
  }
  return hit;
}
static void put(int a, int x, int y) { blit(art[a], x, y, SET); }
static void wipe(int x, int y) { blit(art[A_CELL], x, y, CLEAR); }

static void text(int x, int y, const char *s, int n) {
  for (; *s && n-- > 0; s++, x += 8) {
    int g = 0;
    while (glyphs[g] && glyphs[g] != *s) g++;
    for (int r = 0; r < 8; r++) {
      pic[r] = 0;
      for (int c = 0; glyphs[g] && c < 5; c++) pic[r] |= (uint16_t)((font[g][c] >> r & 1) << (15 - c));
    }
    blit(art[A_LETTER], x, y, CLEAR);
    blit(pic, x, y, SET);
  }
}
static void digits(int x, int y, int v, int n) {
  char s[5] = {0};
  while (n--) s[n] = (char)('0' + v % 10), v /= 10;
  text(x, y, s, 4);
}
static void hud(void) {
  digits(24, 24, score, 4), digits(96, 24, (int)sv.hi, 4);
  if (state == S_TITLE) return;
  digits(8, LY, lives, 1);
  for (int k = 1; k < 7; k++) {
    wipe(8 + 16 * k, LY);
    if (k < lives) put(A_CANNON, 8 + 16 * k, LY);
  }
}
static void screen(void) { /* a clean picture: the scores and the credits */
  for (uint8_t *p = vram[0]; p < vram[240]; p++) *p = 0;
  text(8, 8, "SCORE<1> HI-SCORE SCORE<2>", 26), text(168, 24, "0000", 4), text(136, LY, "CREDIT 00", 9);
  dirty = ALL;
  hud();
}

/* the invaders' shots, 3 pixels by 8 (the top row in the high bits) in four pictures: the rolling
   one twists, the plunger's bar slides down it, the squiggly one wriggles */
static const uint32_t shot_art[3][4] = {{0x492490, 0x4B3598, 0x492490, 0x793C90},
                                        {0x4925C0, 0x497480, 0x4BA480, 0xE92480},
                                        {0x511510, 0x88A888, 0x454450, 0x2A22A0}};
static const uint16_t *shot_pic(int k, int f) {
  for (int r = 0; r < 8; r++) pic[r] = (uint16_t)((shot_art[k][f] >> (21 - 3 * r) & 7) << 13);
  return pic;
}

static uint32_t keys(void) {
  uint64_t s = eadk_keyboard_scan();
  return ((uint32_t)s & 0x800001FFu) | ((uint32_t)(s >> 32) & 0x00100870u) << 8;
}

static const uint16_t *look(int i) { return art[i / 22 * 2 + (st[i] >> 1)]; }

/* ------------------------------------------------------------ the screen */
static const char *const items[2][2] = {{T("Resume"), T("Quit game")}, {T("No"), T("Yes")}};
/* the pause menu in the arcade's own letters, white on black, the squid pointing at the choice in the
   film's green (in the arcade's letters when they have every letter of a text, else NumPlay's) */
static int glyph(int ch) {
  ch = ch >= 'a' && ch <= 'z' ? ch - 32 : ch;
  for (int g = 0; glyphs[g]; g++)
    if (glyphs[g] == ch) return g;
  return ch == ' ' ? -1 : -2;
}
static int atext(int cx, int y, const char *s, int k, color_t c) { /* returns where it begins */
  int n = 0;
  for (const char *t = s; *t; t++) n += glyph((uint8_t)*t) > -2 ? 1 : 1000;
  if (n >= 1000) { /* a letter the arcade didn't have */
    gfx_text_center(&np_font_body, cx, y + 7 * k, s, c, 256);
    return cx - gfx_text_width(&np_font_body, s) / 2;
  }
  int x0 = cx - (n * 8 - 3) * k / 2;
  for (int x = x0; *s; s++, x += 8 * k) {
    int g = glyph((uint8_t)*s);
    for (int col = 0; g >= 0 && col < 5; col++)
      for (int r = 0; r < 7; r++)
        if (font[g][col] >> r & 1) gfx_fill(x + col * k, y + r * k, k, k, c);
  }
  return x0;
}
static void overlay(void) {
  gfx_fill(64, 72, 192, 96, 0);
  atext(160, 84, ov == OV_PAUSE ? T("Paused") : T("Quit game?"), 2, WHITE);
  for (int i = 0; i < 2; i++) {
    int y = 120 + 20 * i;
    int x = atext(160, y, items[ov - 1][i], 1, sel == i ? GREEN : WHITE) - 22;
    for (int r = 0; sel == i && r < 8; r++)
      for (int b = 0; b < 16; b++)
        if (art[4][r] << b & 0x8000) gfx_fill(x + b, y + r - 1, 1, 1, GREEN);
  }
}

/* the picture's rows in the strip, through the cabinet's film */
static void scene(void *ctx) {
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, 0);
  for (int y = gfx_y0; y < gfx_y1; y++) {
    int py = y + OFF;
    color_t c = py >= TOPY && py < 64 ? RED : py >= 184 ? GREEN : WHITE, *o = gfx_buf + (y - gfx_y0) * SCREEN_W + X0;
    for (int x = 0; x < 224; x++)
      if (vram[y][x >> 3] << (x & 7) & 0x80) o[x] = py >= LY && (x < 16 || x >= 134) ? WHITE : c;
  }
  if (state == S_TITLE && typed >= TYPED) { /* the keys */
    gfx_text_center(&np_font_body, 160, 204, T("Press OK to play"), WHITE, 256);
    gfx_text_center(&np_font_small, 160, 222, T("Left Right: move    OK, EXE, Up: fire    Back: pause"), GREEN, 256);
  }
  if (ov) overlay();
}

static void present(void) {
  for (int b = 0, e; b < 30; b = e) {
    for (e = b; e < 30 && dirty >> e & 1; e++) {}
    if (e > b) gfx_render(scene, 0, b * 8, e * 8);
    else e++;
  }
  dirty = 0;
}

/* ------------------------------------------------------------ the rules */
static void save(void) {
  sv.magic = 'I', sv.version = 1;
  if ((uint32_t)score > sv.hi) sv.hi = (uint32_t)score;
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}

static void add(int pts) {
  if ((score += pts) > 9999) score -= 10000; /* (the arcade's 4 digits roll over) */
  if (!bonus && score >= 1500) bonus = 1, lives++;
  hud();
}

static void kill_cannon(void) {
  die = 1, lives--;
  if (fire_y) blit(art[A_SHOT], fire_x, fire_y, CLEAR), fire_y = 0;
  for (int k = 0; k < 3; k++)
    if (as[k].on) blit(shot_pic(k, as[k].f), as[k].x, as[k].y, CLEAR), as[k].on = 0;
  hud();
}

/* the cannon's shot: 4 pixels up a frame; what it meets, found from where it is */
static void shoot(void) {
  if (pb_t && !--pb_t) blit(art[A_PBOOM], pb_x, pb_y, CLEAR); /* the burst goes, its shape cut out */
  if (!fire_y) return;
  int x = fire_x, y = fire_y -= 4, k = 0, i = 0;
  blit(art[A_SHOT], x, y + 4, CLEAR);
  if (y >= TOPY && !blit(art[A_SHOT], x, y, TEST)) {
    put(A_SHOT, x, y);
    return;
  }
  fire_y = 0;
  while (k < 3 && !(as[k].on && (unsigned)(x - as[k].x) < 3 && (unsigned)(y + 3 - as[k].y) < 11)) k++;
  while (i < 55 && !(st[i] & 1 && (unsigned)(x - ax[i]) < 16 && (unsigned)(y + 3 - ay[i]) < 11)) i++;
  if (k < 3) { /* two shots meet */
    blit(shot_pic(k, as[k].f), as[k].x, as[k].y, CLEAR);
    as[k].on = 0, as[k].t = 16, as[k].bx = (uint8_t)(as[k].x - 1), as[k].by = as[k].y;
    put(A_ABOOM, as[k].bx, as[k].by);
  } else if (y >= TOPY && i < 55) { /* an invader: it bursts, and the rack waits for it */
    st[i] = 0, live--, boom_i = i, boom_t = 16;
    wipe(ax[i], ay[i]), put(A_BOOM, ax[i], ay[i]);
    add(10 * (i / 22 + 1));
    return;
  } else if (y >= TOPY && udx && !u_hit && y < UY + 8) { /* the saucer */
    u_hit = 120, u_pts = 10 * ufo[shots % 15];
    wipe(ux, UY), put(A_SBOOM, ux - 4, UY), put(A_SBOOM2, ux + 12, UY);
    add(u_pts);
    return;
  }
  pb_x = x - 3, pb_y = y < TOPY ? TOPY : y - 4, pb_t = 16;
  put(A_PBOOM, pb_x, pb_y);
}

/* one invader takes the rack's step; after the last one, the rack decides the next step */
static void march(void) {
  int i = cur;
  wipe(ax[i], ay[i]);
  ax[i] = (uint8_t)(ax[i] + rdx), ay[i] = (uint8_t)(ay[i] + rdy), st[i] ^= 2;
  wipe(ax[i], ay[i]); /* (and what was there: the bunkers it reaches) */
  blit(look(i), ax[i], ay[i], SET);
  if ((rdx > 0 && ax[i] >= 200) || (rdx < 0 && ax[i] <= 8)) edge = 1;
  if (ay[i] + 8 > PY && !die) lives = 1, kill_cannon(); /* landed */
  for (;;) {
    if (++i == 55) {
      i = 0, ref_x += rdx;
      if (rdy) rdy = 0;
      else if (edge) edge = 0, dir = -dir, rdy = 8;
      rdx = rdy ? 0 : dir > 0 && live == 1 ? 3 : 2 * dir;
    }
    if (st[i] & 1) break;
  }
  cur = i;
}

/* an invader's shot (each one moves every third frame): it bursts, moves, or may be fired */
static void alien_shot(int k) {
  shot_t *s = &as[k];
  int x = s->x, y = s->y;
  if (s->t) {
    if (!--s->t) blit(art[A_ABOOM], s->bx, s->by, CLEAR);
    return;
  }
  if (!s->on) {
    int rate = score < 200 ? 48 : score < 1000 ? 16 : score < 2000 ? 11 : score < 3000 ? 8 : 7, c;
    for (int j = 0; j < 3; j++)
      if (j != k && as[j].on && as[j].steps < rate) return;
    if ((k == 1 && live == 1) || (k == 2 && udx) || die || !live) return;
    if (k) {
      c = cols[next[k]] - 1;
      if (++next[k] == (k == 1 ? 16 : 31)) next[k] = k == 1 ? 0 : 16;
    } else {
      c = (px + 8 - ref_x) >> 4; /* the column above the cannon */
    }
    for (int i = c; (unsigned)c < 11 && i < 55; i += 11)
      if (st[i] & 1) {
        s->on = 1, s->x = (uint8_t)(ax[i] + 6), s->y = (uint8_t)(ay[i] + 8), s->steps = 0;
        blit(shot_pic(k, s->f), s->x, s->y, SET);
        return;
      }
    return;
  }
  blit(shot_pic(k, s->f), x, y, CLEAR);
  s->y = (uint8_t)(y += live > 8 ? 4 : 5), s->f = (uint8_t)((s->f + 1) & 3), s->steps++;
  if (y + 7 < GY && !blit(shot_pic(k, s->f), x, y, TEST)) {
    blit(pic, x, y, SET);
    return;
  }
  s->on = 0;
  if (!die && y + 8 > PY && y < PY + 8 && (unsigned)(x + 2 - px) < 18) { /* the cannon */
    kill_cannon();
    return;
  }
  if (fire_y && (unsigned)(fire_x - x) < 3 && (unsigned)(fire_y + 3 - y) < 11) { /* the cannon's shot */
    blit(art[A_SHOT], fire_x, fire_y, CLEAR);
    pb_x = fire_x - 3, pb_y = fire_y - 4, pb_t = 16, fire_y = 0;
    put(A_PBOOM, pb_x, pb_y);
  }
  s->t = 16, s->bx = (uint8_t)(x - 1), s->by = (uint8_t)(y + 7 >= GY ? GY - 7 : y + 3);
  put(A_ABOOM, s->bx, s->by);
}

static void saucer(void) {
  if (u_hit) { /* its burst, then its points, then nothing */
    if (--u_hit == 60) {
      wipe(ux - 4, UY), wipe(ux + 12, UY);
      digits(ux, UY, u_pts, u_pts < 100 ? 2 : 3);
    }
    if (!u_hit) udx = 0, text(ux, UY, "   ", 3);
    return;
  }
  if (!udx) {
    if (--u_wait <= 0 && live >= 8 && !as[2].on)
      udx = shots & 1 ? -1 : 1, ux = udx > 0 ? 8 : 200, u_wait = 0x600;
    return;
  }
  wipe(ux, UY);
  ux += udx;
  if (ux < 8 || ux > 200) udx = 0;
  else put(A_SAUCER, ux, UY);
}

static void new_wave(void) {
  int base = 248 - starts[wave ? (wave - 1) % 8 + 1 : 0];
  for (int i = 0; i < 55; i++) ax[i] = (uint8_t)(24 + i % 11 * 16), ay[i] = (uint8_t)(base - i / 11 * 16);
  screen();
  for (int k = 0; k < 4; k++)
    for (int q = 0; q < 4; q++) put(A_BUNKER + q, 32 + 45 * k + (q & 1) * 16, SHY + (q & 2) * 4);
  for (int k = 0; k < 28; k++) vram[GY - OFF][k] = 0xFF;
  put(A_CANNON, px, PY);
  for (int k = 0; k < 3; k++) as[k].on = as[k].t = 0;
  for (int i = 0; i < 55; i++) st[i] = 0;
  live = spawn = cur = rdy = edge = boom_t = clear_t = 0;
  dir = 1, rdx = 2, ref_x = 24, udx = u_hit = 0, u_wait = 0x600, fire_y = pb_t = 0;
  wave++;
}

static void title(void) {
  state = S_TITLE, typed = line = col = 0;
  screen();
}

static void start(void) {
  state = S_PLAY, score = wave = shots = bonus = die = 0, lives = 3, px = 16;
  next[1] = 0, next[2] = 16;
  new_wave();
}

/* a frame of the game, 60 a second */
static void tick(uint32_t keys) {
  frames++;
  if (state == S_TITLE) {
    if (typed < TYPED && frames % 4 == 0) { /* the next letter; the table's pictures come with its title */
      if (!attract[typed]) typed++, line++, col = 0;
      text(attract_at[line][0] + 8 * col++, attract_at[line][1], attract + typed++, 1);
      if (typed == TYPED) dirty = ALL; /* (the keys come under it) */
      if (line == 3 && col == 1)
        for (int k = 0; k < 4; k++) put(k ? 2 * (3 - k) : A_SAUCER, 60, 128 + 16 * k);
    }
    return;
  }
  if (state == S_OVER) {
    if (typed < 9 && frames % 6 == 0) typed++, text(72, 48, "GAME OVER", typed);
    if (++t_over > 300) title();
    return;
  }
  if (spawn < 55) { /* a new rack, drawn an invader a frame */
    st[spawn] = 1, blit(look(spawn), ax[spawn], ay[spawn], SET);
    if (++spawn == 55) live = 55;
    return;
  }
  if (die) { /* the cannon blows up, then the next one comes, or the game is over */
    if (die % 5 == 0 && die <= 60) {
      wipe(px, PY);
      if (die < 60) put(A_WRECK + (die / 5 & 1), px, PY);
    }
    if (++die < 120) return;
    die = 0, px = 16;
    if (!lives) {
      state = S_OVER, typed = t_over = 0;
      save();
      return;
    }
    put(A_CANNON, px, PY);
    return;
  }
  int dx = !!(keys & (KEY(eadk_key_right) | KEY(eadk_key_six))) - !!(keys & (KEY(eadk_key_left) | KEY(eadk_key_four)));
  if (dx && px + dx >= 16 && px + dx <= 192) wipe(px, PY), px += dx, put(A_CANNON, px, PY);
  int fire = !!(keys & (KEY(eadk_key_ok) | KEY(eadk_key_exe) | KEY(eadk_key_up) | KEY(eadk_key_eight)));
  if (fire && !fire_held && !fire_y && !pb_t) fire_x = px + 8, fire_y = PY, shots++; /* a new press each shot */
  fire_held = fire;
  shoot();
  if (boom_t) {
    if (!--boom_t) wipe(ax[boom_i], ay[boom_i]);
  } else if (live) {
    march();
  } else if (++clear_t > 60) {
    new_wave();
    return;
  }
  alien_shot((int)(frames % 3));
  saucer();
}

static void load(void) {
  uint32_t len = 0;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  if (d && len == sizeof sv && d[0] == 'I' && d[1] == 1)
    for (uint32_t i = 0; i < sizeof sv; i++) ((uint8_t *)&sv)[i] = d[i];
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  load();
  title();
  const uint32_t ok = KEY(eadk_key_ok) | KEY(eadk_key_exe) | KEY(eadk_key_five), back = KEY(eadk_key_back);
  const uint32_t up = KEY(eadk_key_up) | KEY(eadk_key_eight), down = KEY(eadk_key_down) | KEY(eadk_key_two);
  uint32_t held = keys(), stale = held; /* keys still down from the launcher wait to be let go */
  uint32_t last = (uint32_t)eadk_timing_millis(), acc = 0;
  dirty = ALL;
  for (;;) {
    uint32_t now = (uint32_t)eadk_timing_millis();
    uint32_t k = keys(), hit = k & ~held;
    stale &= k, held = k, k &= ~stale;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    if (ov) {
      if (hit & (up | down)) sel ^= 1;
      if (hit & (ok | back)) {
        if (hit & ok && sel && ov == OV_QUIT) break;
        ov = hit & ok && sel ? OV_QUIT : OV_NONE, sel = 0;
      }
      if (hit) dirty = ALL;
    } else if (hit & back) {
      if (state == S_TITLE) break;
      ov = OV_PAUSE, sel = 0, dirty = ALL;
    } else if (state == S_TITLE && hit & ok) {
      start();
      fire_held = 1;
    }
    acc = ov ? 0 : acc + (now - last) * 3, last = now;
    for (int n = 0; acc >= 50 && n < 4; acc -= 50, n++) tick(k);
    if (acc >= 50) acc = 0; /* (too far behind: let it go) */
    if (dirty) {
      eadk_display_wait_for_vblank();
      present();
    } else {
      eadk_timing_msleep(4);
    }
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
