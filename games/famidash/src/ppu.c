/* The NES's picture chip, as much of it as Famidash uses: tiles from 1 KB CHR banks, 16x16 metatiles
 * with their palettes, the parallax (one 48x72 picture moved a pixel per bank, the way the game's 144
 * parallax banks are), 8x16 sprites with flips and the behind-the-background bit, 32 palette entries,
 * brightness (fades) and the grey and tint bits of the PPU mask (pause, gameboy palettes).
 *
 * The level's background is drawn from the decoded columns (game.c), 320 pixels wide: the NES's 256 in
 * the middle and 32 more of the level on each side. Menus are drawn from nametables. */
#include "fd.h"

spr_t oam[MAX_SPR];
int oam_n;
uint8_t pal_bright_level = 4, emphasis;
uint8_t chr_bg[4], chr_spr[2];
uint8_t bg_mode;
uint8_t nametable[2][1024];
int nt_scroll_x, nt_scroll_y;
int nt_split_y0 = -1, nt_split_y1 = -1, nt_split_x;
uint8_t nt_fill_tile, nt_wrap_split;
/* the "ATTEMPT 1" line: tiles put into the level's first screen */
int32_t text_x, text_y;
uint8_t text_tiles[16], text_n;

/* the NES's 64 colours (2C02), as RGB565 */
static const uint32_t nes_rgb[64] = {
    0x545454, 0x001E74, 0x081090, 0x300088, 0x440064, 0x5C0030, 0x540400, 0x3C1800, 0x202A00, 0x083A00, 0x004000,
    0x003C00, 0x00323C, 0x000000, 0x000000, 0x000000, 0x989698, 0x084CC4, 0x3032EC, 0x5C1EE4, 0x8814B0, 0xA01464,
    0x982220, 0x783C00, 0x545A00, 0x287200, 0x087C00, 0x007628, 0x006678, 0x000000, 0x000000, 0x000000, 0xECEEEC,
    0x4C9AEC, 0x787CEC, 0xB062EC, 0xE454EC, 0xEC58B4, 0xEC6A64, 0xD48820, 0xA0AA00, 0x74C400, 0x4CD020, 0x38CC6C,
    0x38B4CC, 0x3C3C3C, 0x000000, 0x000000, 0xECEEEC, 0xA8CCEC, 0xBCBCEC, 0xD4B2EC, 0xECAEEC, 0xECAED4, 0xECB4B0,
    0xE4C490, 0xCCD278, 0xB4DE78, 0xA8E290, 0x98E2B4, 0xA0D6E4, 0xA0A2A0, 0x000000, 0x000000};

static uint16_t pal565[32];
static const uint8_t *bgp[4];      /* 1 KB of tiles for each quarter of the background's table */
static const uint8_t *sprp[4];     /* 1 KB each for the sprites' table ($1000-$1FFF) */
static bool par_on;                /* bank 2 is the parallax */
static uint8_t par_shift;          /* which of its 144 banks */
static uint8_t par_img_s[72][48];  /* the picture, moved for that bank */
static uint8_t par_img_shift = 0xFF;

static const uint8_t *bank_ptr(uint8_t b) {
  if (b >= 112) return chr_data[(b & 1) ? PAR_BANK1 : PAR_BANK0];
  return chr_data[chr_bank_map[b]];
}

static uint32_t spread4[16];
void ppu_init(void) {
  for (int n = 0; n < 16; n++)
    spread4[n] = (uint32_t)((n >> 3) & 1) | (uint32_t)((n >> 2) & 1) << 8 | (uint32_t)((n >> 1) & 1) << 16 | (uint32_t)(n & 1) << 24;
}

static uint16_t rgb(uint32_t c, uint8_t emp) {
  int r = c >> 16 & 0xFF, g = c >> 8 & 0xFF, b = c & 0xFF;
  if (emp & 0xE0) { /* each tint bit dims the other two colours */
    int kr = 4, kg = 4, kb = 4;
    if (emp & 0x20) kg--, kb--;
    if (emp & 0x40) kr--, kb--;
    if (emp & 0x80) kr--, kg--;
    if (kr < 2) kr = 2;
    if (kg < 2) kg = 2;
    if (kb < 2) kb = 2;
    r = r * (kr + 8) / 12, g = g * (kg + 8) / 12, b = b * (kb + 8) / 12;
  }
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

/* palette, banks and the parallax picture for this frame */
static void frame_setup(void) {
  for (int i = 0; i < 32; i++) {
    uint8_t c = colBrightness(PAL_BUF_RAW[i], pal_bright_level);
    if (emphasis & 1) c &= 0x30;
    pal565[i] = rgb(nes_rgb[c & 0x3F], emphasis);
  }
  for (int i = 0; i < 4; i++) bgp[i] = bank_ptr(chr_bg[i]);
  par_on = chr_bg[2] >= 112;
  par_shift = (uint8_t)(chr_bg[2] - 112);
  if (par_on && par_shift != par_img_shift) {
    par_img_shift = par_shift;
    for (int y = 0; y < 72; y++)
      for (int x = 0; x < 48; x++) {
        int sx = x - par_shift, sy = y;
        while (sx < 0) sx += 48, sy -= 24;
        sy %= 72;
        if (sy < 0) sy += 72;
        par_img_s[y][x] = parallax_img[sy * 48 + sx];
      }
  }
  sprp[0] = bank_ptr(chr_spr[0] & 0xFE);
  sprp[1] = bank_ptr((chr_spr[0] & 0xFE) + 1);
  sprp[2] = bank_ptr(chr_spr[1] & 0xFE);
  sprp[3] = bank_ptr((chr_spr[1] & 0xFE) + 1);
}

/* 8 pixels of a tile row as palette indices (0 when transparent) */
static inline void tile_row(uint8_t *dst, const uint8_t *t, int py, uint8_t palbase) {
  uint8_t p0 = t[py], p1 = t[py + 8];
  uint32_t a = spread4[p0 >> 4] | spread4[p1 >> 4] << 1, b = spread4[p0 & 15] | spread4[p1 & 15] << 1;
  uint32_t ma = (a | a >> 1) & 0x01010101u, mb = (b | b >> 1) & 0x01010101u;
  a += ma * palbase, b += mb * palbase;
  memcpy(dst, &a, 4), memcpy(dst + 4, &b, 4);
}


/* one row of the level's background, 336 pixels from tile column t0 */
static void level_row(uint8_t *buf, int32_t t0, int wy) {
  int mrow = wy >> 4, half = (wy >> 3) & 1, py = wy & 7;
  int trow = (wy >> 3) - 60; /* the parallax counts its rows from the top of the level's usual view */
  int pr0 = trow % 9;
  if (pr0 < 0) pr0 += 9;
  const uint8_t *ltab = half ? mt_bl : mt_tl, *rtab = half ? mt_br : mt_tr;
  int32_t mc_prev = 0x7FFFFFFF;
  uint8_t mt = 0;
  for (int i = 0; i < 42; i++) {
    int32_t T = t0 + i, mc = T >> 1;
    if (mc != mc_prev) mt = level_metatile(mc, mrow), mc_prev = mc;
    uint8_t tile = (T & 1) ? rtab[mt] : ltab[mt];
    if (invisblocks && mrow < 57) tile = 0;
    uint8_t *d = buf + i * 8;
    if (text_n && mrow < 60 && (wy >> 3) == (text_y >> 3) && T >= (text_x >> 3) && T < (text_x >> 3) + text_n &&
        text_tiles[T - (text_x >> 3)])
      tile = text_tiles[T - (text_x >> 3)];
    if (tile == 0 && par_on && !no_parallax) {
      int32_t tt = T < 0 ? 0 : T;
      int pc = (int)(tt % 6), pstart = (int)((3 * (tt / 6)) % 9);
      int prow = (pstart + pr0) % 9;
      const uint8_t *s = &par_img_s[prow * 8 + py][pc * 8];
      uint8_t pb = (uint8_t)(mt_pal[mt] * 4);
      for (int k = 0; k < 8; k++) d[k] = s[k] ? (uint8_t)(s[k] + pb) : 0;
      continue;
    }
    tile_row(d, bgp[tile >> 6] + (tile & 63) * 16, py, (uint8_t)(mt_pal[mt] * 4));
  }
}

/* one row from the nametables (menus) */
static void nt_row(uint8_t *buf, int sx, int sy) {
  int ty = sy % 480;
  if (ty < 0) ty += 480;
  bool lower = ty >= 240;
  if (lower) ty -= 240;
  int row = ty >> 3, py = ty & 7;
  int x0 = sx - NES_X0;
  int t0 = x0 >> 3;
  for (int i = 0; i < 42; i++) {
    int T = t0 + i, ntx = T & 63, n = ntx >> 5, col = ntx & 31;
    uint8_t tile, pal;
    if (lower) tile = nt_fill_tile, pal = 0;
    else {
      const uint8_t *nt = nametable[n];
      tile = nt[row * 32 + col];
      uint8_t at = nt[960 + (row >> 2) * 8 + (col >> 2)];
      pal = (at >> (((row >> 1) & 1) * 4 + ((col >> 1) & 1) * 2)) & 3;
    }
    tile_row(buf + i * 8, bgp[tile >> 6] + (tile & 63) * 16, py, (uint8_t)(pal * 4));
  }
}

/* the sprites on one row: the first opaque one wins, then the background bit decides */
static uint8_t spr_line[336];
static void sprites_row(uint8_t *line, int y) {
  bool any = false;
  for (int i = 0; i < oam_n; i++) {
    const spr_t *s = &oam[i];
    int r = y - (s->y + 1);
    if ((unsigned)r >= 16) continue;
    if (!any) memset(spr_line, 0, sizeof spr_line), any = true;
    if (s->attr & 0x80) r = 15 - r;
    uint8_t t = s->tile;
    int tile = (t & 0xFE) + (r >> 3);
    const uint8_t *src;
    if (t & 1) src = sprp[(tile >> 6) & 3] + (tile & 63) * 16;
    else src = bgp[(tile >> 6) & 3] + (tile & 63) * 16;
    uint8_t px[8];
    tile_row(px, src, r & 7, 0);
    uint8_t base = (uint8_t)(16 + (s->attr & 3) * 4), behind = s->attr & 0x20 ? 0x80 : 0;
    int x0 = s->x + NES_X0;
    for (int k = 0; k < 8; k++) {
      int x = x0 + ((s->attr & 0x40) ? 7 - k : k);
      if ((unsigned)x >= 320) continue;
      uint8_t c = px[k];
      if (c && !spr_line[x]) spr_line[x] = (uint8_t)((base + c) | behind | 0x40);
    }
  }
  if (!any) return;
  for (int x = 0; x < 320; x++) {
    uint8_t s = spr_line[x];
    if (!s) continue;
    if ((s & 0x80) && line[x]) continue;
    line[x] = s & 0x1F;
  }
}

void ppu_set_palette(const uint8_t *bg16, const uint8_t *spr16) {
  if (bg16) memcpy(PAL_BUF_RAW, bg16, 16);
  if (spr16) memcpy(PAL_BUF_RAW + 16, spr16, 16);
}

static int frame_y0 = -1;
void ppu_strip(uint16_t *out, int y0, int y1) {
  if (y0 == 0 || frame_y0 < 0) frame_setup();
  frame_y0 = y0;
  static uint8_t buf[42 * 8];
  for (int y = y0; y < y1; y++) {
    uint8_t *line;
    if (bg_mode == BG_LEVEL) {
      int32_t wx = (int32_t)scroll_x - NES_X0;
      int32_t t0 = wx >> 3;
      level_row(buf, t0, (int)scroll_y + y);
      line = buf + (wx & 7);
    } else {
      int sx = nt_scroll_x;
      bool split = y >= nt_split_y0 && y < nt_split_y1;
      if (split) sx = nt_split_x;
      int x0 = sx - NES_X0;
      nt_row(buf, sx, nt_scroll_y + y);
      line = buf + (x0 & 7);
      if (!(split && nt_wrap_split)) /* the menus are 256 wide: their edges go on into the sides */
        for (int x = 0; x < NES_X0; x++) line[x] = line[NES_X0 + (x & 7)], line[319 - x] = line[287 - (x & 7)];
    }
    sprites_row(line, y);
    uint16_t *o = out + (y - y0) * 320;
    for (int x = 0; x < 320; x++) o[x] = pal565[line[x]];
  }
}

/* ---------------------------------------------------------------- the sprite list (neslib's OAM) */
void oam_clear(void) { oam_n = 0; }
void oam_spr(int x, int y, uint8_t tile, uint8_t attr) {
  if (oam_n >= MAX_SPR) return;
  y &= 0xFF;
  if (y >= 0xEF) return;
  oam[oam_n++] = (spr_t){(int16_t)x, (int16_t)y, tile, attr};
}
void oam_meta_spr(int x, int y, const uint8_t *d) {
  for (; d[0] != 0x80; d += 4) {
    int sy = y + (int8_t)d[1];
    if (sy < 0 || sy >= 0xEF) continue;
    if (oam_n >= MAX_SPR) return;
    oam[oam_n++] = (spr_t){(int16_t)(x + (int8_t)d[0]), (int16_t)sy, d[2], d[3]};
  }
}
static uint32_t disco_rng = 1;
void oam_meta_spr_disco(int x, int y, const uint8_t *d) {
  for (; d[0] != 0x80; d += 4) {
    int sy = y + (int8_t)d[1];
    if (sy < 0 || sy >= 0xEF) continue;
    if (oam_n >= MAX_SPR) return;
    oam[oam_n++] = (spr_t){(int16_t)(x + (int8_t)d[0]), (int16_t)sy, d[2], (uint8_t)((d[3] & ~3) | ((disco_rng = disco_rng * 1103515245u + 12345u) >> 16 & 3))};
  }
}
void oam_meta_spr_flipped(uint8_t flip, int x, int y, const uint8_t *d) {
  for (; d[0] != 0x80; d += 4) {
    int dx = (int8_t)d[0], dy = (int8_t)d[1];
    int sx = (flip & 0x40) ? x - dx - 8 : x + dx;
    int sy = (flip & 0x80) ? y - dy : y + dy;
    if (oam_n >= MAX_SPR) return;
    oam[oam_n++] = (spr_t){(int16_t)sx, (int16_t)(sy & 0xFF), d[2], (uint8_t)(d[3] ^ flip)};
  }
}
