/* Balatro's paint background on the TI-84 Plus CE. Its shaders (worked out as the NumWorks version does, src/bg.c)
 * give a picture of the paint's value, 16 levels (tools/mkart.py: BALBG0 in a run, BALBG1 on the title screen); the
 * 64 palette entries those levels use take the shader's colours, and the colours run through the levels with time:
 * the paint flows without a pixel being drawn. Four banks of the 16 levels, darker and darker, are the background
 * under the shaded panels and the cards' shadows. */
#include <string.h>
#include "bg.h"
#include "ce.h"

uint8_t ce_bg_ver;
static const uint8_t *field[2];
static int mode;
static float colours[3][3], target[3][3], contrast = 1, contrast_target = 1, spin, spin_target;
static int lut_dirty = 1;
static uint8_t lut[33][3]; /* the colour every 8 paint values */
static unsigned phase;     /* 0..511 in 1/256 units of the paint: where the colours are */
static uint32_t phase_frac;

int ce_bg_init(void) {
  size_t n;
  field[0] = ce_appvar("BALBG0", &n);
  field[1] = ce_appvar("BALBG1", &n);
  return field[0] && field[1];
}

static float fabsf_(float x) { return x < 0 ? -x : x; }
static float fmaxf_(float a, float b) { return a > b ? a : b; }
static float fminf_(float a, float b) { return a < b ? a : b; }

/* src/bg.c's colour of a paint value */
static void colour_at(int v, uint8_t out[3]) {
  float r = v / 127.5f, o[3];
  if (mode == 0) {
    float cm = 0.25f * contrast + 0.5f * spin + 1.2f;
    float c1p = fmaxf_(0.f, 1.f - cm * fabsf_(1.f - r));
    float c2p = fmaxf_(0.f, 1.f - cm * fabsf_(r));
    float c3p = 1.f - fminf_(1.f, c1p + c2p);
    float k = 0.3f / contrast;
    for (int j = 0; j < 3; j++)
      o[j] = k * colours[0][j] + (1 - k) * (colours[0][j] * c1p + colours[1][j] * c2p + colours[2][j] * c3p);
  } else {
    float s = r * 2.f - 2.f;
    float c1p = fmaxf_(0.f, 1.f - 2.f * fabsf_(1.f - s));
    float c2p = fmaxf_(0.f, 1.f - 2.f * s);
    float cb = 1.f - fminf_(1.f, c1p + c2p);
    static const float red[3] = {0xFE / 255.f, 0x5F / 255.f, 0x55 / 255.f};
    static const float blue[3] = {0x00, 0x9D / 255.f, 1.f};
    static const float black[3] = {0.6f * 79 / 255.f, 0.6f * 99 / 255.f, 0.6f * 103 / 255.f};
    float fl = fmaxf_(c1p, c2p) * 5.f - 4.4f;
    if (fl < 0) fl = 0;
    for (int j = 0; j < 3; j++) {
      float w = red[j] * c1p + blue[j] * c2p + black[j] * cb;
      o[j] = w * (1 - fl) + fl;
    }
  }
  for (int j = 0; j < 3; j++) out[j] = (uint8_t)(fminf_(1.f, fmaxf_(0.f, o[j])) * 255.f + .5f);
}

static void make_lut(void) {
  for (int i = 0; i <= 32; i++) colour_at(i * 8 > 255 ? 255 : i * 8, lut[i]);
  lut_dirty = 0;
}

void bg_mode(int m) {
  if (m != mode) {
    mode = m;
    lut_dirty = 1;
    ce_bg_ver++;
  }
}

static void set3(float d[3], uint32_t hex, float k) {
  d[0] = ((hex >> 16) & 255) / 255.f * k;
  d[1] = ((hex >> 8) & 255) / 255.f * k;
  d[2] = (hex & 255) / 255.f * k;
}

/* Balatro's ease_background_colour, as src/bg.c */
void bg_colours(uint32_t col, uint32_t special, uint32_t tertiary, float contr, float spin_amount, int instant) {
  if (special && tertiary) {
    set3(target[1], col, 1.f);
    set3(target[0], special, 1.f);
    set3(target[2], tertiary, 1.f);
  } else {
    set3(target[1], col, 1.3f);
    set3(target[2], col, special ? 0.4f : 0.7f);
    if (special) set3(target[0], special, 1.f);
    else set3(target[0], col, 0.9f);
  }
  contrast_target = contr;
  spin_target = spin_amount;
  if (instant) {
    memcpy(colours, target, sizeof colours);
    contrast = contr;
    spin = spin_amount;
  }
  lut_dirty = 1;
}

void bg_reset(void) { lut_dirty = 1; }

/* the colours: eased like the game, then run through the levels */
void bg_update(float dt, int rows) {
  (void)rows;
  float k = dt / 0.6f * 3.f;
  if (k > 1) k = 1;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++) {
      float d = target[i][j] - colours[i][j];
      if (d > 0.002f || d < -0.002f) lut_dirty = 1, colours[i][j] += d * k;
      else colours[i][j] = target[i][j];
    }
  float dc = contrast_target - contrast, ds = spin_target - spin;
  if (dc > 0.01f || dc < -0.01f) lut_dirty = 1, contrast += dc * k;
  else contrast = contrast_target;
  if (ds > 0.001f || ds < -0.001f) lut_dirty = 1, spin += ds * (dt * 2.f > 1 ? 1 : dt * 2.f);
  else spin = spin_target;
  if (lut_dirty) make_lut();
  /* a full turn of the colours every 8 s in a run, 4 s on the title (in 1/65536 of a paint value) */
  phase_frac += (uint32_t)(dt * (mode ? 128.f : 64.f) * 65536.f);
  phase = (phase + (unsigned)(phase_frac >> 16)) & 511;
  phase_frac &= 0xFFFF;
  static const uint8_t dark[CE_BANKS] = CE_BANK_DARK;
  uint16_t pal[CE_BANKS * CE_LEVELS];
  for (int l = 0; l < CE_LEVELS; l++) {
    unsigned p = (unsigned)(l * 16 + 8 + (int)phase) & 511;
    if (p > 255) p = 511 - p;
    int i = (int)(p >> 3), f = (int)(p & 7);
    int rgb[3];
    for (int j = 0; j < 3; j++) rgb[j] = (lut[i][j] * (8 - f) + lut[i + 1][j] * f) >> 3;
    for (int b = 0; b < CE_BANKS; b++) {
      int m = 32 - dark[b], r = rgb[0] * m >> 5, g = rgb[1] * m >> 5, bl = rgb[2] * m >> 5;
      pal[b * CE_LEVELS + l] = (uint16_t)((r & 248) << 7 | (g & 248) << 2 | bl >> 3);
    }
  }
  ce_set_palette(CE_SWIRL0, CE_BANKS * CE_LEVELS, pal);
}

void bg_row(C *dst, int y, int x0, int x1) { (void)dst, (void)y, (void)x0, (void)x1; }

void ce_bg_row(uint8_t *dst, int y, int x0) {
  if (mode == 0) {
    const uint8_t *row = field[0] + y * BG_RUN_W;
    int x = x0;
    for (; x < BG_RUN_X; x++) dst[x] = row[BG_RUN_X - x < BG_RUN_W ? BG_RUN_X - x : 0]; /* (mirrored: a sidebar covers it) */
    memcpy(dst + x, row + (x - BG_RUN_X), (size_t)(SCR_W - x));
  } else {
    const uint8_t *row = field[1] + (y >> 1) * BG_TITLE_W;
    for (int x = x0 & ~1; x < SCR_W; x += 2) dst[x] = dst[x + 1] = row[x >> 1];
  }
}
