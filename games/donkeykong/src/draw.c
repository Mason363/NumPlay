/* Drawing: the arcade's tile map and sprite table with this port's pictures (art.h), in the launcher's strips.
 *
 * The play area is the arcade's, pixel for pixel: its 224 columns at x 96..319 and its rows 16 to 255 (the
 * two rows above hold only 1UP and the scores). The panel on the left reads what the ROM's own display tasks
 * wrote in the tile map's top rows (score, high score, lives, L=) and in its BONUS box. A strip is sent to the
 * screen only where it changed. */
#include <string.h>
#include "../../../launcher/src/gfx.h"
#include "draw.h"
#include "art.h"
#include "rom.h"

#define PX 96 /* the play area's left edge */
#define WHITE HEX(0xFFFFFF)
#define INK HEX(0x000000)
#define MUTED HEX(0x9AA3B5)
#define FOCUS HEX(0xFFC21A)

static const dk_state *G;
static const dk_view *W;

typedef struct {
  uint32_t girder[3], ladder, beam[3], belt[2], rivet[2], line, cross, text, box, bonus, level;
} bank_t;
static const bank_t banks[4] = {
    /* 0: the title */
    {{0xFF2155, 0x970000, 0xFF2155}, 0x00FFFF, {0xFF2155, 0xFF2155, 0x970000}, {0, 0}, {0, 0}, 0x970000, 0, 0xFFFFFF,
     0x00FFFF, 0xFF47AA, 0x2020FF},
    /* 1: 50m (and HOW HIGH CAN YOU GET?) */
    {{0xFB7100, 0xFBBA99, 0xFB7100}, 0xFCFCFF, {0x00FFFF, 0x0000FF, 0}, {0xFB7100, 0xFBBA99}, {0xFFB600, 0x00FFFF},
     0xB70000, 0xFB7100, 0xFFFFFF, 0xFB7100, 0xFB7100, 0x2020FF},
    /* 2: 25m and 75m */
    {{0xFF2155, 0x970000, 0xFF2155}, 0x00FFFF, {0x00FFFF, 0x0000FF, 0}, {0xFB7100, 0xFBBA99}, {0xFFB600, 0x00FFFF},
     0xA40000, 0xFB7100, 0xFFFFFF, 0x00FFFF, 0xFF47AA, 0x2020FF},
    /* 3: 100m */
    {{0x0000FF, 0x00FFFF, 0x0000FF}, 0xFFB600, {0x00FFFF, 0x0000FF, 0}, {0xFB7100, 0xFBBA99}, {0xFFB600, 0x00FFFF},
     0x00FFFF, 0xFB7100, 0xFFFFFF, 0xFFB600, 0x2020FF, 0x2020FF},
};

static inline color_t *row_at(int y) { return gfx_buf + (y - gfx_y0) * SCREEN_W; }

/* ---------------------------------------------------------------- letters in the arcade's style (8 px cells) */

static void glyph_at(int x, int y, int index, color_t c, int scale) {
  const uint8_t *g = dk_font[index];
  for (int r = 0; r < 8; r++) {
    for (int sy = 0; sy < scale; sy++) {
      int yy = y + r * scale + sy;
      if (yy < gfx_y0 || yy >= gfx_y1) continue;
      color_t *row = row_at(yy);
      for (int b = 0; b < 8; b++)
        if (g[r] & 0x80 >> b)
          for (int sx = 0; sx < scale; sx++) {
            int xx = x + b * scale + sx;
            if ((unsigned)xx < SCREEN_W) row[xx] = c;
          }
    }
  }
}

static void atext(int x, int y, const char *s, color_t c, int scale) {
  if (y >= gfx_y1 || y + 8 * scale <= gfx_y0) return;
  for (; *s; s++, x += 8 * scale) {
    const char *p = strchr(dk_font_chars, *s);
    if (*s != ' ' && p) glyph_at(x, y, (int)(p - dk_font_chars), c, scale);
  }
}

/* ---------------------------------------------------------------- tiles */

static int bank(void) { return G ? G->palette & 3 : 0; }

static void tile_rows(int x, int y, const uint8_t *pat, int row0, int row1, int shift, const color_t col[3]) {
  /* pattern rows row0..row1-1 of an 8x8 2-bit pattern, drawn at tile rows row0+shift.. */
  for (int r = row0; r < row1; r++) {
    int yy = y + r + shift;
    if (yy < gfx_y0 || yy >= gfx_y1) continue;
    color_t *row = row_at(yy) + x;
    uint8_t a = pat[r * 2], b = pat[r * 2 + 1];
    uint16_t bits = (uint16_t)(a << 8 | b);
    for (int k = 0; k < 8; k++) {
      int v = bits >> (14 - 2 * k) & 3;
      if (v) row[k] = col[v - 1];
    }
  }
}

static void tile(int x, int y, uint8_t code, int row) {
  const bank_t *bk = &banks[bank()];
  color_t g3[3] = {HEX(bk->girder[0]), HEX(bk->girder[1]), HEX(bk->girder[2])};
  color_t lad[3] = {HEX(bk->ladder), HEX(bk->ladder), HEX(bk->ladder)};
  if (code >= 0x50 && code <= 0x67) { /* the Kongs of HOW HIGH CAN YOU GET? */
    int k = code - 0x50, cx = (5 - k / 4) * 8, cy = (3 - k % 4) * 8;
    color_t kp[3] = {HEX(0xB81000), HEX(0xFFB855), HEX(0xFFFFFF)};
    for (int r = 0; r < 8; r++) {
      int yy = y + r;
      if (yy < gfx_y0 || yy >= gfx_y1) continue;
      color_t *out = row_at(yy) + x;
      for (int c = 0; c < 8; c++) {
        int px = cx + c, v = dk_goofy[(cy + r) * 12 + px / 4] >> (6 - 2 * (px & 3)) & 3;
        if (v) out[c] = kp[v - 1];
      }
    }
    return;
  }
  if (dk_tile_char[code]) {
    color_t c = HEX(bk->text);
    if (code >= 0x70 && code <= 0x79) c = HEX(0xFF2000);
    glyph_at(x, y, dk_tile_char[code] - 1, c, 1);
    return;
  }
  uint8_t hi = code & 0xF0, k = code & 0x0F;
  if (k < 8 && (hi == 0xF0 || hi == 0xE0 || hi == 0xC0 || hi == 0xD0)) {
    if (hi == 0xF0) tile_rows(x, y, dk_tile_girder, 0, 8 - k, k, g3);
    else if (hi == 0xE0) tile_rows(x, y, dk_tile_girder, 8 - k, 8, k - 8, g3);
    else if (hi == 0xC0) {
      tile_rows(x, y, dk_tile_ladder, k, 8, 0, lad);
      if (k) tile_rows(x, y, dk_tile_girder, 8 - k, 8, k - 8, g3);
    } else {
      tile_rows(x, y, dk_tile_ladder, 0, k, 0, lad);
      tile_rows(x, y, dk_tile_girder, 0, 8 - k, k, g3);
    }
    return;
  }
  switch (code) {
  case 0xB0: {
    color_t b3[3] = {HEX(bk->beam[0]), HEX(bk->beam[1]), HEX(bk->beam[2])};
    tile_rows(x, y, dk_tile_rivet_girder, 0, 8, 0, b3);
    break;
  }
  case 0xB1:
  case 0xB2:
  case 0xB3: {
    color_t b3[3] = {HEX(bk->belt[0]), HEX(bk->belt[1]), HEX(bk->belt[1])};
    tile_rows(x, y, dk_tile_belt, 0, 8, 0, b3);
    break;
  }
  case 0xB7:
  case 0xB8: {
    color_t r3[3] = {HEX(bk->rivet[0]), HEX(bk->rivet[1]), HEX(bk->beam[0])};
    color_t b3[3] = {HEX(bk->beam[0]), HEX(bk->beam[1]), HEX(bk->beam[2])};
    if (code == 0xB7) {
      tile_rows(x, y, dk_tile_rivet_girder, 0, 8, 0, b3);
      tile_rows(x, y, dk_tile_rivet_low, 0, 8, 0, r3);
    } else {
      tile_rows(x, y, dk_tile_rivet_top, 0, 8, 0, r3);
    }
    break;
  }
  case 0xFC:
  case 0xFD: { /* a cable, or the bars by Kong on 100m: a line at the tile's edge */
    int xx = code == 0xFC ? x + 7 : x;
    if (row >= 0 && (unsigned)xx < SCREEN_W)
      for (int r = 0; r < 8; r++)
        if (y + r >= gfx_y0 && y + r < gfx_y1) row_at(y + r)[xx] = HEX(bk->line);
    break;
  }
  case 0xFE: {
    color_t c3[3] = {HEX(bk->cross), HEX(bk->cross), HEX(bk->cross)};
    tile_rows(x, y, dk_tile_cross, 0, 8, 0, c3);
    break;
  }
  case 0xDD:
  case 0xDE:
  case 0xDF:
  case 0xED:
  case 0xEE:
  case 0xEF: {
    const uint8_t *h = dk_help[(code & 0x0F) - 0x0D];
    for (int r = 0; r < 8; r++) {
      if (y + r < gfx_y0 || y + r >= gfx_y1) continue;
      color_t *out = row_at(y + r) + x;
      for (int b = 0; b < 8; b++)
        if (h[r] & 0x80 >> b) out[b] = HEX(bk->text);
    }
    break;
  }
  }
}

/* the cells of the top rows and of the BONUS box: shown in the panel instead */
static bool hud_cell(int col, int row) {
  if (row <= 3) return row < 2 || (row == 3 && ((col >= 23 && col <= 28) || (col >= 5 && col <= 8)));
  return row >= 5 && row <= 7 && col >= 3 && col <= 8;
}

static void tiles(const uint8_t *vram) {
  int r0 = (gfx_y0 + 16) >> 3, r1 = (gfx_y1 + 16 + 7) >> 3;
  if (r1 > 32) r1 = 32;
  for (int r = r0; r < r1; r++)
    for (int sc = 0; sc < 28; sc++) {
      int col = 29 - sc;
      uint8_t code = vram[col * 32 + r];
      if (code == 0x10 || hud_cell(col, r)) continue;
      tile(PX + sc * 8, r * 8 - 16, code, r);
    }
}

/* ---------------------------------------------------------------- sprites */

static void sprite(uint8_t x, uint8_t code, uint8_t color, uint8_t y) {
  int sx = x + PX - 23, sy = y - 24;
  if (sy >= gfx_y1 || sy + 16 <= gfx_y0) return;
  uint8_t idx = dk_spr_of[code & 0x7F];
  if (idx == 255) return;
  int pal = dk_spr_pal[idx], cc = color & 0x0F;
  uint8_t c7 = code & 0x7F;
  if ((c7 == 0x3D || c7 == 0x3E || c7 == 0x4D || c7 == 0x4E) && cc == 0) pal = PAL_FIRE_BLUE;
  if ((c7 == 0x1E || c7 == 0x1F) && cc == 1) pal = PAL_HAMMER_FLASH;
  if (c7 == 0x46 && bank() != 1) pal = PAL_POINTS; /* (only on 50m) */
  if (c7 == 0x46 && bank() == 1) pal = PAL_LADDER_WHITE;
  const uint16_t *p = dk_pal[pal];
  bool fx = code & 0x80, fy = color & 0x80;
  const uint8_t *img = dk_spr[idx];
  for (int r = 0; r < 16; r++) {
    int yy = sy + r;
    if (yy < gfx_y0 || yy >= gfx_y1) continue;
    const uint8_t *src = img + (fy ? 15 - r : r) * 4;
    color_t *out = row_at(yy);
    for (int c = 0; c < 16; c++) {
      int xx = sx + c;
      if (xx < PX || xx >= SCREEN_W) continue;
      int cx = fx ? 15 - c : c, v = src[cx >> 2] >> (6 - 2 * (cx & 3)) & 3;
      if (v) out[xx] = p[v - 1];
    }
  }
}

static void sprites(const uint8_t *table) {
  for (int i = 0; i < 0x60; i++) {
    const uint8_t *s = table + i * 4;
    if (s[0]) sprite(s[0], s[1], s[2], s[3]);
  }
}

/* ---------------------------------------------------------------- the panel */

static void digits_from(int x, int y, const uint16_t *cells, int n, int scale, color_t c) {
  char buf[8];
  for (int i = 0; i < n; i++) {
    uint8_t v = DK_VRAM(G, cells[i]);
    buf[i] = v <= 9 ? (char)('0' + v) : v >= 0x70 && v <= 0x79 ? (char)('0' + v - 0x70) : ' ';
  }
  buf[n] = 0;
  atext(x, y, buf, c, scale);
}

static void keyline(int y) {
  static const char *const keys[3][2] = {{"OK", "jump"}, {"arrows", "move"}, {"Back", "pause"}};
  for (int i = 0; i < 3; i++) {
    gfx_text(&np_font_small, 8, y + i * 15, keys[i][0], WHITE, 256);
    gfx_text(&np_font_small, 8 + gfx_text_width(&np_font_small, keys[i][0]) + 5, y + i * 15, keys[i][1], MUTED, 256);
  }
}

static void panel(void) {
  if (gfx_y0 >= 240) return;
  gfx_fill(0, gfx_y0, PX, gfx_y1 - gfx_y0, INK);
  const bank_t *bk = &banks[bank()];
  static const uint16_t score[6] = {0x7781, 0x7761, 0x7741, 0x7721, 0x7701, 0x76E1};
  static const uint16_t high[6] = {0x7641, 0x7621, 0x7601, 0x75E1, 0x75C1, 0x75A1};
  if (DK_VRAM(G, 0x7740) != 0x10) atext(8, 8, "1UP", HEX(0xFF2000), 1);
  digits_from(8, 20, score, 6, 1, WHITE);
  atext(8, 38, "HIGH", HEX(0xFF2000), 1);
  atext(8, 48, "SCORE", HEX(0xFF2000), 1);
  digits_from(8, 60, high, 6, 1, WHITE);
  if (DK_VRAM(G, 0x7465) == 0x8D) { /* the BONUS box, as the board draws it */
    color_t box = HEX(bk->box);
    gfx_fill(6, 80, 84, 2, box);
    gfx_fill(6, 116, 84, 2, box);
    gfx_fill(6, 80, 2, 38, box);
    gfx_fill(88, 80, 2, 38, box);
    atext(28, 86, "BONUS", HEX(bk->bonus), 1);
    static const uint16_t bonus[4] = {0x74E6, 0x74C6, 0x74A6, 0x7486};
    bool red = DK_VRAM(G, 0x74C6) >= 0x70;
    digits_from(16, 98, bonus, 4, 2, red ? HEX(0xFF2000) : WHITE);
  }
  if (DK_VRAM(G, 0x7503) == 0x1C) {
    static const uint16_t lv[2] = {0x74C3, 0x74A3};
    atext(8, 128, "L=", HEX(bk->level), 1);
    digits_from(24, 128, lv, 2, 1, HEX(bk->level));
  }
  for (int i = 0; i < 6; i++) /* a little Mario for each life left */
    if (DK_VRAM(G, 0x7783 - i * 0x20) == 0xFF) {
      color_t lp[3] = {HEX(0xFF2010), HEX(0x2830FF), HEX(0xFFB855)};
      int x = 8 + i * 12, y = 144;
      for (int r = 0; r < 8; r++) {
        if (y + r < gfx_y0 || y + r >= gfx_y1) continue;
        color_t *out = row_at(y + r) + x;
        for (int c = 0; c < 8; c++) {
          int v = dk_life[r * 2 + c / 4] >> (6 - 2 * (c & 3)) & 3;
          if (v) out[c] = lp[v - 1];
        }
      }
    }
  keyline(196);
}

/* ---------------------------------------------------------------- the title: the arcade's logo, Kong beating his chest */

static uint8_t title_vram[0x400];
static uint8_t title_spr[0x28];

static void title_build(uint32_t clock) {
  static bool built;
  if (!built) {
    memset(title_vram, 0x10, sizeof title_vram);
    uint16_t hl = 0x3D08; /* 0x07F7: B0 tiles, a run down each column */
    for (int guard = 0; guard < 64; guard++) {
      uint8_t n = ROM(hl);
      if (!n) break;
      uint16_t de = ROM(hl + 1) | ROM(hl + 2) << 8;
      for (int i = 0; i < n; i++) title_vram[(de + i - 0x7400) & 0x3FF] = 0xB0;
      hl += 3;
    }
    built = true;
  }
  uint16_t pose = (clock / 16) & 1 ? 0x39CF : 0x39F7;
  for (int i = 0; i < 0x28; i++) title_spr[i] = ROM(pose + i);
  for (int i = 0; i < 10; i++) {
    if (title_spr[i * 4]) title_spr[i * 4] = (uint8_t)(title_spr[i * 4] + 0x44);
    title_spr[i * 4 + 3] = (uint8_t)(title_spr[i * 4 + 3] + 0x78);
  }
}

static void title(void) {
  gfx_fill(PX, gfx_y0, SCREEN_W - PX, gfx_y1 - gfx_y0, INK);
  const dk_state *keep = G;
  G = NULL; /* bank 0 */
  tiles(title_vram);
  for (int i = 0; i < 10; i++) {
    const uint8_t *s = title_spr + i * 4;
    if (s[0]) sprite(s[0], s[1], s[2], s[3]);
  }
  G = keep;
  gfx_fill(0, gfx_y0, PX, gfx_y1 - gfx_y0, INK);
  atext(8, 8, "HIGH", HEX(0xFF2000), 1);
  atext(8, 18, "SCORE", HEX(0xFF2000), 1);
  char b[12];
  uint32_t v = W->best < 7650 ? 7650 : W->best;
  for (int i = 5; i >= 0; i--) b[i] = (char)('0' + v % 10), v /= 10;
  b[6] = 0;
  atext(8, 30, b, WHITE, 1);
  if (W->best_level) {
    atext(8, 50, "BEST L=", HEX(0x2020FF), 1);
    b[0] = (char)('0' + W->best_level / 10 % 10), b[1] = (char)('0' + W->best_level % 10), b[2] = 0;
    atext(64, 50, b, HEX(0x2020FF), 1);
  }
  if ((W->clock / 32) & 1) atext(PX + 72, 200, "PRESS OK", WHITE, 1);
  gfx_text(&np_font_small, 8, 196, "OK", WHITE, 256);
  gfx_text(&np_font_small, 30, 196, "play", MUTED, 256);
  gfx_text(&np_font_small, 8, 211, "Back", WHITE, 256);
  gfx_text(&np_font_small, 39, 211, "quit", MUTED, 256);
}

/* ---------------------------------------------------------------- menus */

static void card(int y, int h) {
  gfx_fill(PX + 16, y, 192, h, HEX(0x0C1220));
  gfx_fill(PX + 16, y, 192, 2, FOCUS);
  gfx_fill(PX + 16, y + h - 2, 192, 2, FOCUS);
  gfx_fill(PX + 16, y, 2, h, FOCUS);
  gfx_fill(PX + 206, y, 2, h, FOCUS);
}

static void choice(int y, const char *s, bool on) {
  if (on) gfx_fill(PX + 28, y - 13, 168, 18, FOCUS);
  gfx_text_center(&np_font_body, PX + 112, y, s, on ? INK : WHITE, 256);
}

static void pause_menu(void) {
  card(56, 128);
  gfx_text_center(&np_font_title, PX + 112, 84, "PAUSED", WHITE, 256);
  static const char *const items[4] = {"Resume", "New game", "How to play", "Quit game"};
  for (int i = 0; i < 4; i++) choice(110 + i * 20, items[i], W->sel == i);
}

static void confirm_menu(void) {
  card(80, 84);
  gfx_text_center(&np_font_body, PX + 112, 106, W->confirm ? "Quit this game?" : "Start a new game?", WHITE, 256);
  choice(132, "No", W->sel == 0);
  choice(150, "Yes", W->sel == 1);
}

static void over_menu(void) {
  card(52, 136);
  gfx_text_center(&np_font_title, PX + 112, 80, "GAME OVER", WHITE, 256);
  char b[16];
  uint32_t v = dk_score(G);
  int n = 0;
  do b[n++] = (char)('0' + v % 10), v /= 10;
  while (v);
  for (int i = 0; i < n / 2; i++) {
    char t = b[i];
    b[i] = b[n - 1 - i], b[n - 1 - i] = t;
  }
  b[n] = 0;
  gfx_text_center(&np_font_title, PX + 112, 110, b, FOCUS, 256);
  if (W->new_best) gfx_text_center(&np_font_body, PX + 112, 128, "New best!", WHITE, 256);
  choice(152, "Play again", W->sel == 0);
  choice(172, "Title", W->sel == 1);
}

static void guide(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, INK);
  gfx_text_center(&np_font_title, SCREEN_W / 2, 30, "How to play", WHITE, 256);
  static const char *const lines[] = {
      "Climb to Pauline. Donkey Kong throws barrels,",
      "fires chase you, pies ride the belts,",
      "springs bounce over the elevators.",
      "On 100m, walk over all eight rivets.",
      "Jump over a barrel or a fire for points,",
      "grab a hammer to smash them (no jumping then).",
      "Falling too far, or the bonus running out, costs a life.",
  };
  for (int i = 0; i < 7; i++) gfx_text_center(&np_font_small, SCREEN_W / 2, 58 + i * 16, lines[i], HEX(0xE6EBF2), 256);
  static const char *const keys[4][2] = {
      {"arrows", "walk, climb ladders"}, {"OK, EXE", "jump"}, {"Back", "pause"}, {"Home", "save and quit"}};
  for (int i = 0; i < 4; i++) {
    gfx_text_right(&np_font_body, 140, 184 + i * 14, keys[i][0], WHITE, 256);
    gfx_text(&np_font_small, 152, 184 + i * 14, keys[i][1], MUTED, 256);
  }
  gfx_text_center(&np_font_small, SCREEN_W / 2, 236, "OK to go on", FOCUS, 256);
}

/* ---------------------------------------------------------------- the frame */

static void scene(void *ctx) {
  (void)ctx;
  if (W->screen == DK_SCREEN_GUIDE) {
    guide();
    return;
  }
  if (W->screen == DK_SCREEN_TITLE) {
    title();
    return;
  }
  gfx_fill(PX, gfx_y0, SCREEN_W - PX, gfx_y1 - gfx_y0, INK);
  tiles(G->vram);
  sprites(&DK_RAM(G, 0x6900));
  panel();
  if (W->screen == DK_SCREEN_PAUSE) pause_menu();
  if (W->screen == DK_SCREEN_CONFIRM) confirm_menu();
  if (W->screen == DK_SCREEN_OVER) over_menu();
}

static uint32_t hashes[SCREEN_H / STRIP_H][10];
static bool hashes_ok;
static color_t span[SCREEN_W * STRIP_H];
void dk_draw_invalidate(void) { hashes_ok = false; }

void dk_draw(const dk_state *s, const dk_view *v) {
  G = s, W = v;
  if (v->screen == DK_SCREEN_TITLE) title_build(v->clock);
  for (int strip = 0; strip < SCREEN_H / STRIP_H; strip++) {
    gfx_y0 = strip * STRIP_H;
    gfx_y1 = gfx_y0 + STRIP_H;
    scene(0);
    int first = -1, last = -1;
    for (int seg = 0; seg < 10; seg++) {
      uint32_t h = 2166136261u;
      for (int y = 0; y < STRIP_H; y++) {
        const color_t *p = gfx_buf + y * SCREEN_W + seg * 32;
        for (int x = 0; x < 32; x++) h = (h ^ p[x]) * 16777619u;
      }
      if (!hashes_ok || hashes[strip][seg] != h) {
        if (first < 0) first = seg;
        last = seg;
      }
      hashes[strip][seg] = h;
    }
    if (first < 0) continue;
    int x0 = first * 32, w = (last - first + 1) * 32;
    for (int y = 0; y < STRIP_H; y++) memcpy(span + y * w, gfx_buf + y * SCREEN_W + x0, (size_t)w * sizeof(color_t));
    np_push(x0, gfx_y0, w, STRIP_H, span);
  }
  hashes_ok = true;
}
