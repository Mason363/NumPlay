/* Balatro's paint background on the TI-84 Plus CE. Its shaders (worked out as the NumWorks version does, src/bg.c)
 * give a picture of the paint's value, 16 levels (tools/mkart.py: BALBG0 in a run, BALBG1 and 2 on the title screen); the
 * 64 palette entries those levels use take the shader's colours, and the colours run through the levels with time:
 * the paint flows without a pixel being drawn. Four banks of the 16 levels, darker and darker, are the background
 * under the shaded panels and the cards' shadows. */
#include <string.h>
#include "bg.h"
#include "ce.h"

uint8_t ce_bg_ver;
static const uint8_t *field[3]; /* the run's (right of the sidebar), the title's (its halves): palette entries */
static int mode;
/* in 1/256: the colours (0..255 a channel, times 256), the contrast and the spin */
static int colours[3][3], target[3][3], contrast = 256, contrast_target = 256, spin, spin_target;
static int lut_dirty = 1;
static uint8_t lut[33][3]; /* the colour every 8 paint values */
static unsigned phase;     /* 0..511 in 1/256 units of the paint: where the colours are */
static unsigned phase_frac;
static unsigned shown_phase = 1024; /* the palette's */
static uint8_t lut_made;

/* the banks' darkness as factors in 1/256 (0: 256) */
static uint8_t keep[CE_BANKS];

#ifdef NP_HOST
/* (blit.S on the calculator) */
void ce_bg_pal(uint8_t *out, const uint8_t *lt, unsigned ph, const uint8_t *kp) {
  for (int l = 0; l < CE_LEVELS; l++) {
    unsigned q = ((unsigned)l * 16 + 8 + ph) & 511;
    if (q > 255) q = 511 - q;
    int p = (int)q, f = p & 7, rgb[3];
    const uint8_t *a = lt + (p >> 3) * 3;
    for (int j = 0; j < 3; j++) rgb[j] = f ? (a[j] * (256 - f * 32) + a[j + 3] * f * 32) >> 8 : a[j];
    for (int b = 0; b < CE_BANKS; b++) {
      int r = rgb[0], g = rgb[1], bl = rgb[2], k = kp[b];
      if (k) r = r * k >> 8, g = g * k >> 8, bl = bl * k >> 8;
      uint8_t *e = out + (b * CE_LEVELS + l) * 2;
      e[0] = (uint8_t)((g << 2 & 0xE0) | bl >> 3);
      e[1] = (uint8_t)((r >> 1 & 0x7C) | g >> 6);
    }
  }
}
#endif

int ce_bg_init(void) {
  static const uint8_t dark[CE_BANKS] = CE_BANK_DARK;
  for (int b = 0; b < CE_BANKS; b++) keep[b] = (uint8_t)((32 - dark[b]) * 8);
  size_t n;
  field[0] = ce_appvar("BALBG0", &n);
  field[1] = ce_appvar("BALBG1", &n);
  field[2] = ce_appvar("BALBG2", &n);
  return field[0] && field[1] && field[2];
}

static int iabs(int x) { return x < 0 ? -x : x; }
static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }

/* src/bg.c's colour of a paint value v (0..255), in integers: 256 is 1 */
static void colour_at(int v, uint8_t out[3]) {
  int r = v * 2, o[3]; /* 0..2 */
  if (mode == 0) {
    int cm = contrast / 4 + spin / 2 + 307; /* 0.25 contrast + 0.5 spin + 1.2 */
    int c1p = imax(0, 256 - cm * iabs(256 - r) / 256), c2p = imax(0, 256 - cm * r / 256);
    int c3p = 256 - imin(256, c1p + c2p), k = imin(256, 19661 / contrast); /* 0.3 / contrast */
    for (int j = 0; j < 3; j++) {
      int mixed = (colours[0][j] / 256 * c1p + colours[1][j] / 256 * c2p + colours[2][j] / 256 * c3p) / 256;
      o[j] = (k * (colours[0][j] / 256) + (256 - k) * mixed) / 256;
    }
  } else {
    int s = r * 2 - 512; /* -2..2 */
    int c1p = imax(0, 256 - 2 * iabs(256 - s)), c2p = imax(0, 256 - 2 * s), cb = 256 - imin(256, c1p + c2p);
    static const int16_t red[3] = {0xFE, 0x5F, 0x55}, blue[3] = {0x00, 0x9D, 0xFF}, black[3] = {47, 59, 62};
    int fl = imin(1024, imax(0, imax(c1p, c2p) * 5 - 1126)); /* max * 5 - 4.4 (past 4: white all the same) */
    for (int j = 0; j < 3; j++) {
      int w = (red[j] * c1p + blue[j] * c2p + black[j] * cb) / 256;
      o[j] = (w * (256 - fl) + 255 * fl) / 256;
    }
  }
  for (int j = 0; j < 3; j++) out[j] = (uint8_t)imax(0, imin(255, o[j]));
}

static void make_lut(void) {
  for (int i = 0; i <= 32; i++) colour_at(i * 8 > 255 ? 255 : i * 8, lut[i]);
  lut_dirty = 0, lut_made = 1;
}

void bg_mode(int m) {
  if (m != mode) {
    mode = m;
    lut_dirty = 1;
    ce_bg_ver++;
  }
}

/* a channel times k (in 1/10), times 256 */
static void set3(int d[3], uint32_t hex, int k10) {
  d[0] = (int)((hex >> 16) & 255) * k10 * 256 / 10;
  d[1] = (int)((hex >> 8) & 255) * k10 * 256 / 10;
  d[2] = (int)(hex & 255) * k10 * 256 / 10;
}

/* Balatro's ease_background_colour, as src/bg.c */
void bg_colours(uint32_t col, uint32_t special, uint32_t tertiary, float contr, float spin_amount, int instant) {
  if (special && tertiary) {
    set3(target[1], col, 10);
    set3(target[0], special, 10);
    set3(target[2], tertiary, 10);
  } else {
    set3(target[1], col, 13);
    set3(target[2], col, special ? 4 : 7);
    if (special) set3(target[0], special, 10);
    else set3(target[0], col, 9);
  }
  contrast_target = (int)(contr * 256);
  spin_target = (int)(spin_amount * 256);
  if (contrast_target < 64) contrast_target = 64;
  if (instant) {
    memcpy(colours, target, sizeof colours);
    contrast = contrast_target;
    spin = spin_target;
  }
  lut_dirty = 1;
}

void bg_reset(void) { lut_dirty = 1; }

/* the colours: eased like the game (over 0.6 s), then run through the levels */
void bg_update(float dt, int rows) {
  (void)rows;
  int ms = (int)(dt * 1000.f);
  if (ms > 200) ms = 200;
  int k = ms * 1280 / 1000; /* dt / 0.6 * 3, in 1/256 */
  if (k > 256) k = 256;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      int d = target[i][j] - colours[i][j];
      if (iabs(d) > 128) lut_dirty = 1, colours[i][j] += d / 256 * k + (d % 256) * k / 256;
      else colours[i][j] = target[i][j];
    }
  int dc = contrast_target - contrast, ds = spin_target - spin;
  if (iabs(dc) > 3) lut_dirty = 1, contrast += dc * k / 256;
  else contrast = contrast_target;
  if (iabs(ds) > 1) lut_dirty = 1, spin += ds * imin(256, ms * 512 / 1000) / 256;
  else spin = spin_target;
  if (lut_dirty) make_lut();
  /* the colours sway along the paint, two slow waves: its bands move as the paint flowing, and every colour stays
     where the game has it (a sine table, a quarter in 16 steps) */
  static const int8_t sine[16] = {0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 127};
  phase_frac = (phase_frac + (unsigned)ms) % 42000u; /* (a common period of both waves) */
  int a = (int)(phase_frac % 7000u * 64 / 7000), b2 = (int)(phase_frac % 3000u * 64 / 3000);
#define SINE(t) ((t) < 16 ? sine[t] : (t) < 32 ? sine[31 - (t)] : (t) < 48 ? -sine[(t) - 32] : -sine[63 - (t)])
  phase = (unsigned)(512 + (SINE(a) * (mode ? 40 : 32) + SINE(b2) * (mode ? 18 : 14)) / 127) & 511;
#undef SINE
  if (phase == shown_phase && !lut_made) return;
  shown_phase = phase, lut_made = 0;
  uint16_t pal[CE_BANKS * CE_LEVELS];
  ce_bg_pal((uint8_t *)pal, &lut[0][0], phase, keep);
  ce_set_palette(CE_SWIRL0, CE_BANKS * CE_LEVELS, pal);
}

void bg_row(C *dst, int y, int x0, int x1) { (void)dst, (void)y, (void)x0, (void)x1; }

void ce_bg_row(uint8_t *dst, int y, int x0) {
  if (mode == 0) {
    const uint8_t *row = field[0] + y * BG_RUN_W;
    if (x0 < BG_RUN_X) { /* (mirrored: the sidebar covers it) */
      for (int x = x0; x < BG_RUN_X; x++) dst[x] = row[BG_RUN_X - 1 - x];
      x0 = BG_RUN_X;
    }
    memcpy(dst + x0, row + (x0 - BG_RUN_X), (size_t)(SCR_W - x0));
  } else {
    const uint8_t *row = y < SCR_H / 2 ? field[1] + y * SCR_W : field[2] + (y - SCR_H / 2) * SCR_W;
    memcpy(dst + x0, row + x0, (size_t)(SCR_W - x0));
  }
}
