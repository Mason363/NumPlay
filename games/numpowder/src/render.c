/* NumPowder: The Powder Toy's renderer (Renderer.cpp: render_parts, render_fire, DrawWalls, draw_air, draw_grav,
 * draw_other; Gradient.cpp; Pixel.h's blending), drawn in bands (render.h).
 *
 * A frame, as the original's RenderSimulation:
 * - the fire glow's cells first take the powered walls' glow and every particle's fire (render_parts' fire part,
 *   in the particles' order, so the glow is the original's);
 * - then for each band of 4 particle rows (one row of the glow's cells), into 24-bit pixels: the air (draw_air)
 *   or the persistent trails, the gravity field's lines (draw_grav), the streamlines, every particle whose
 *   drawing reaches the band (render_parts), the fire glow of the cells around (render_fire, which then lets the
 *   cells above fade), the EMP's flash (draw_other);
 * - each band shown 3 x 3 pixels a particle, the walls' patterns at the screen's resolution (DrawWalls), the
 *   stickmen, then the interface's overlay. */
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("O3")   /* (a hot path: a few KB of flash for speed) */
#endif
#include "render.h"
#include <string.h>

RenderSettings ren = {RENDER_BASC | RENDER_FIRE | RENDER_SPRK | RENDER_EFFE, 0, COLOUR_DEFAULT, false, true, false};

const RenderPreset render_presets[PRESET_COUNT] = {
  {"Alternative Velocity Display", RENDER_EFFE | RENDER_BASC, DISPLAY_AIRC, 0, false},
  {"Velocity Display", RENDER_EFFE | RENDER_BASC, DISPLAY_AIRV, 0, false},
  {"Pressure Display", RENDER_EFFE | RENDER_BASC, DISPLAY_AIRP, 0, false},
  {"Persistent Display", RENDER_EFFE | RENDER_BASC, DISPLAY_PERS, 0, false},
  {"Fire Display", RENDER_FIRE | RENDER_SPRK | RENDER_EFFE | RENDER_BASC, 0, 0, false},
  {"Blob Display", RENDER_FIRE | RENDER_SPRK | RENDER_EFFE | RENDER_BLOB, 0, 0, false},
  {"Heat Display", RENDER_BASC, DISPLAY_AIRH, COLOUR_HEAT, false},
  {"Fancy Display", RENDER_FIRE | RENDER_SPRK | RENDER_GLOW | RENDER_BLUR | RENDER_EFFE | RENDER_BASC, DISPLAY_WARP, 0, false},
  {"Nothing Display", RENDER_BASC, 0, 0, false},
  {"Heat Gradient Display", RENDER_BASC, 0, COLOUR_GRAD, false},
  {"Life Gradient Display", RENDER_BASC, 0, COLOUR_LIFE, false},
  {"Dynamic Heat Display", RENDER_BASC, DISPLAY_AIRH, COLOUR_HEAT, true},
  {"Vorticity Display", RENDER_EFFE | RENDER_BASC, DISPLAY_AIRW, 0, false},
  {"Destructibility Display", RENDER_BASC, 0, COLOUR_DEST, false},
};

uint32_t render_ms_parts, render_ms_total;
int render_found;

/* ---------------------------------------------------------------- the fire glow (fire_r, fire_g, fire_b) */
static uint8_t fire_r[YACELLS][XACELLS], fire_g[YACELLS][XACELLS], fire_b[YACELLS][XACELLS];
static uint32_t fire_nz;   /* the rows of cells that may glow (a bit each): the others are skipped */
static uint32_t frame_no;

/* prepare_alpha(CELL, 1.0f): each cell's glow over the 3 x 3 cells around it, the sum over its 4 x 4 particles of
 * expf(-0.1f * (i * i + j * j)) for i, j from -CELL to CELL - 1 (worked out once: it never changes) */
static const uint8_t fire_alpha[ACELL * 3][ACELL * 3] = {
  {0, 1, 4, 7, 9, 11, 11, 9, 6, 3, 1, 0},
  {1, 5, 12, 21, 28, 33, 33, 28, 19, 10, 3, 0},
  {4, 12, 26, 44, 60, 70, 70, 60, 40, 21, 8, 0},
  {7, 21, 44, 75, 103, 121, 121, 103, 68, 37, 14, 0},
  {9, 28, 60, 103, 141, 165, 165, 141, 94, 51, 19, 0},
  {11, 33, 70, 121, 165, 193, 193, 165, 109, 59, 22, 0},
  {11, 33, 70, 121, 165, 193, 193, 165, 109, 59, 22, 0},
  {9, 28, 60, 103, 141, 165, 165, 141, 94, 51, 19, 0},
  {6, 19, 40, 68, 94, 109, 109, 94, 62, 34, 12, 0},
  {3, 10, 21, 37, 51, 59, 59, 51, 34, 18, 6, 0},
  {1, 3, 8, 14, 19, 22, 22, 19, 12, 6, 2, 0},
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
};

void render_clear_accumulation(void) {
  memset(fire_r, 0, sizeof fire_r);
  memset(fire_g, 0, sizeof fire_g);
  memset(fire_b, 0, sizeof fire_b);
  fire_nz = 0;
}

void render_apply(uint32_t render_mode, uint32_t display_mode, uint32_t colour_mode) {
  if ((!(render_mode & FIREMODE) && (ren.render_mode & FIREMODE)) ||
      (!(display_mode & DISPLAY_PERS) && (ren.display_mode & DISPLAY_PERS)))
    render_clear_accumulation();
  ren.render_mode = render_mode, ren.display_mode = display_mode, ren.colour_mode = colour_mode;
}

int render_preset_now(void) {
  for (int i = 0; i < PRESET_COUNT; i++)
    if (render_presets[i].render_mode == ren.render_mode && render_presets[i].display_mode == ren.display_mode &&
        render_presets[i].colour_mode == ren.colour_mode && render_presets[i].hdisp_auto == ren.hdisp_auto)
      return i;
  return -1;
}
void render_apply_preset(int preset) {
  const RenderPreset *p = &render_presets[preset];
  render_apply(p->render_mode, p->display_mode, p->colour_mode);
  ren.hdisp_auto = p->hdisp_auto;
}

/* the heat display's range (RendererStats' hdispLimitMin and Max, Renderer::AdjustHdispLimit): MIN_TEMP to
 * MAX_TEMP, or in Dynamic Heat Display the coldest to the hottest shown, shrinking towards them slowly */
static float hdisp_min, hdisp_max = MAX_TEMP;
static void hdisp_adjust(void) {
  if (!ren.hdisp_auto) {
    hdisp_min = MIN_TEMP, hdisp_max = MAX_TEMP;
    return;
  }
  float lo = MAX_TEMP, hi = MIN_TEMP;
  if (ren.colour_mode & COLOUR_HEAT)
    for (int i = 0; i < parts_active; i++) {
      const Particle *p = &parts[i];
      int t = p->type;
      if (!t || !elements[t].HeatConduct || !InBounds(p_cx(p), p_cy(p))) continue;
      float v = p_temp(p);
      lo = fminf(lo, v), hi = fmaxf(hi, v);
    }
  if (aheat_enable && (ren.display_mode & DISPLAY_AIR) && (ren.display_mode & DISPLAY_AIRH))
    for (int y = 0; y < YACELLS; y++)
      for (int x = 0; x < XACELLS; x++) lo = fminf(lo, hv[y][x]), hi = fmaxf(hi, hv[y][x]);
  /* (they shrink towards the new range slowly, to prevent rapid flashes, but expand at once; a degree apart) */
  hi = fmaxf(hi, hdisp_max - (hdisp_max - hi) * 0.05f);
  lo = fminf(lo, hdisp_min + (lo - hdisp_min) * 0.05f);
  hi = fminf(MAX_TEMP, fmaxf(hi, lo + 1));
  lo = fmaxf(MIN_TEMP, fminf(lo, hi - 1));
  if (isnan(lo)) lo = MIN_TEMP;
  if (isnan(hi)) hi = MAX_TEMP;
  hdisp_max = fminf(fmaxf(hi, MIN_TEMP), MAX_TEMP);
  hdisp_min = fminf(fmaxf(lo, MIN_TEMP), hdisp_max);
}
static inline uint32_t heat_colour(float temp) {
  return heat_table_at((int)((temp - hdisp_min) / (hdisp_max - hdisp_min) * 1024));
}

void render_init(void) { render_clear_accumulation(); }

/* ---------------------------------------------------------------- the colour tables (Gradient, PopulateTables) */
typedef struct { uint32_t c; float p; } Stop;
static uint32_t blend888(uint32_t d, uint32_t c, int a) {
  if (a >= 255) return c & 0xFFFFFF;
  uint32_t r = (uint16_t)(a * (c >> 16 & 255) + (255 - a) * (d >> 16 & 255)) / 255;
  uint32_t g = (uint16_t)(a * (c >> 8 & 255) + (255 - a) * (d >> 8 & 255)) / 255;
  uint32_t b = (uint16_t)(a * (c & 255) + (255 - a) * (d & 255)) / 255;
  return r << 16 | g << 8 | b;
}
static uint32_t gradient(const Stop *s, int n, int res, int i) {
  if (i < 0) i = 0;
  if (i > res - 1) i = res - 1;
  float point = i / (float)res;
  int k = -1;
  while (k < n - 1 && s[k + 1].p <= point) k++;
  if (k < 0 || k >= n - 1) return 0;
  float f = (point - s[k].p) / (s[k + 1].p - s[k].p);
  return blend888(s[k].c, s[k + 1].c, (uint8_t)(f * 255));
}
static const Stop flame[] = {{0x000000, 0.00f}, {0x60300F, 0.50f}, {0xDFBF6F, 0.90f}, {0xAF9F0F, 1.00f}};
static const Stop plasma[] = {{0x000000, 0.00f}, {0x301040, 0.25f}, {0x301060, 0.50f}, {0xAFFFFF, 0.90f},
                              {0xAFFFFF, 1.00f}};
static const Stop heat[] = {{0x2B00FF, 0.00f}, {0x003CFF, 0.01f}, {0x00C0FF, 0.05f}, {0x00FFEB, 0.08f},
                            {0x00FF14, 0.19f}, {0x4BFF00, 0.25f}, {0xC8FF00, 0.37f}, {0xFFDC00, 0.45f},
                            {0xFF0000, 0.71f}, {0xFF00DC, 1.00f}};
static const Stop clfm[] = {{0x000000, 0.00f}, {0x0A0917, 0.10f}, {0x19163C, 0.20f}, {0x28285E, 0.30f},
                            {0x343E77, 0.40f}, {0x49769A, 0.60f}, {0x57A0B4, 0.80f}, {0x5EC4C6, 1.00f}};
static const Stop firw[] = {{0xFF00FF, 0.00f}, {0x0000FF, 0.20f}, {0x00FFFF, 0.40f}, {0x00FF00, 0.60f},
                            {0xFFFF00, 0.80f}, {0xFF0000, 1.00f}};
#define NSTOPS(a) ((int)(sizeof(a) / sizeof(a[0])))
uint32_t flame_table_at(int i) { return gradient(flame, NSTOPS(flame), 200, i); }
uint32_t plasma_table_at(int i) { return gradient(plasma, NSTOPS(plasma), 200, i); }
uint32_t heat_table_at(int i) { return gradient(heat, NSTOPS(heat), 1024, i); }
uint32_t clfm_table_at(int i) { return gradient(clfm, NSTOPS(clfm), 200, i); }
uint32_t firw_table_at(int i) { return gradient(firw, NSTOPS(firw), 200, i); }

/* HeatToColour, for the air's heat */
static inline uint32_t heat_colour(float temp);
static uint32_t heat_to_colour(float temp) {
  uint32_t c = heat_colour(temp);
  return (uint32_t)(uint8_t)((c >> 16 & 255) * 0.7f) << 16 | (uint32_t)(uint8_t)((c >> 8 & 255) * 0.7f) << 8 |
         (uint8_t)((c & 255) * 0.7f);
}

/* ---------------------------------------------------------------- the graphics functions' random numbers */
static uint32_t grng;
static void grng_seed(uint32_t i) {
  uint32_t x = i * 2654435761u ^ frame_no * 0x9E3779B9u;
  x ^= x >> 16, x *= 0x7FEB352Du, x ^= x >> 15, x *= 0x846CA68Bu, x ^= x >> 16;
  grng = x | 1;
}
static inline uint32_t grng_u32(void) {
  grng ^= grng << 13, grng ^= grng >> 17, grng ^= grng << 5;
  return grng;
}
uint32_t ren_rng_gen(void) { return grng_u32() >> 1; }
int ren_rng_between(int lower, int upper) { return lower + (int)(grng_u32() % (uint32_t)(upper - lower + 1)); }
bool ren_rng_chance(int numerator, unsigned denominator) {
  if (numerator <= 0) return false;
  return grng_u32() % denominator < (unsigned)numerator;
}

__attribute__((weak)) uint32_t part_dcolour(const Particle *p) { (void)p; return 0; }

/* SimulationData::DestructibilityScore */
static int destructibility(int t) {
  if (t <= 0 || t >= PT_NUM) return 0;
  if (t == PT_CLNE || t == PT_PCLN) return 100;   /* (nonzero Hardness, yet indestructible) */
  const Element *e = &elements[t];
  uint32_t prop = e->Properties;
  if (prop & TYPE_ENERGY) return 10;
  if (prop & TYPE_GAS) return 20;
  if (prop & TYPE_LIQUID) return 30;
  if (prop & TYPE_PART) return 40;
  if (e->Flammable > 0) return 50;
  if (e->HighPressureTransition != NT || e->LowPressureTransition != NT) return 60;
  if (e->HighTemperatureTransition != NT || e->LowTemperatureTransition != NT) return 70;
  if (e->Hardness > 0) return 80;
  if (t == PT_VIBR || t == PT_RSSS || t == PT_HEAC) return 90;
  return 100;
}

/* ---------------------------------------------------------------- a particle's look (render_parts) */
typedef struct { int pm, ca, cr, cg, cb, fa, fr, fg, fb; } Shade;

/* Element::defaultGraphics: radioactive elements glow, liquids blur, gases are a haze of fire */
int default_graphics(GRAPHICS_FUNC_ARGS) {
  uint32_t props = elements[cpart->type].Properties;
  if (props & PROP_RADIOACTIVE) *pixel_mode |= PMODE_GLOW;
  if (props & TYPE_LIQUID) *pixel_mode |= PMODE_BLUR;
  if (props & TYPE_GAS) {
    *pixel_mode &= ~PMODE;
    *pixel_mode |= FIRE_BLEND;
    *firer = *colr / 2;
    *fireg = *colg / 2;
    *fireb = *colb / 2;
    *firea = 125;
    *pixel_mode |= DECO_FIRE;
  }
  return 1;
}
/* the modes default_graphics gives an element (with PMODE_FLAT, which it starts with) */
static int default_modes(int t) {
  uint32_t props = elements[t].Properties;
  int pm = PMODE_FLAT;
  if (props & PROP_RADIOACTIVE) pm |= PMODE_GLOW;
  if (props & TYPE_LIQUID) pm |= PMODE_BLUR;
  if (props & TYPE_GAS) pm = (pm & ~PMODE) | FIRE_BLEND | DECO_FIRE;
  return pm;
}
/* render_parts' pixel modes as the render mode allows them */
static inline int allowed_modes(int pm, uint32_t rm) {
  if ((pm & FIRE_ADD) && !(rm & FIRE_ADD)) pm |= PMODE_GLOW;
  if ((pm & FIRE_BLEND) && !(rm & FIRE_BLEND)) pm |= PMODE_BLUR;
  if ((pm & PMODE_BLUR) && !(rm & PMODE_BLUR)) pm |= PMODE_FLAT;
  if ((pm & PMODE_GLOW) && !(rm & PMODE_GLOW)) pm |= PMODE_BLEND;
  if (rm & PMODE_BLOB) pm |= PMODE_BLOB;
  return pm & (int)rm;
}

static void shade(int i, const Particle *p, int t, int nx, int ny, Shade *s) {
  const Element *el = &elements[t];
  uint32_t colour = el->Colour;
  s->pm = PMODE_FLAT, s->ca = 255;
  s->cr = colour >> 16 & 255, s->cg = colour >> 8 & 255, s->cb = colour & 255;
  s->fa = s->fr = s->fg = s->fb = 0;
  uint32_t dc = ren.decorations ? part_dcolour(p) : 0;
  int deca = dc >> 24 & 255, decr = dc >> 16 & 255, decg = dc >> 8 & 255, decb = dc & 255;
  uint32_t rm = ren.render_mode, cm = ren.colour_mode;
  /* (an element without graphics of its own, but for gases: its colour with the default graphics' modes, what the
   * rest would come to) */
  if (!el->Graphics && !cm && !deca && !(el->Properties & (PROP_HOT_GLOW | TYPE_GAS))) {
    int base = PMODE_FLAT;
    if (t != PT_DUST && t != PT_SAWD && t != PT_DRAY) {   /* (element_graphics: default_graphics) */
      if (el->Properties & PROP_RADIOACTIVE) base |= PMODE_GLOW;
      if (el->Properties & TYPE_LIQUID) base |= PMODE_BLUR;
    }
    s->pm = allowed_modes(base, rm);
    return;
  }
  GraphicsFn g = element_graphics(t);
  if (!(cm & COLOUR_BASC) && g) {
    grng_seed((uint32_t)i);
    g(p, nx, ny, &s->pm, &s->ca, &s->cr, &s->cg, &s->cb, &s->fa, &s->fr, &s->fg, &s->fb);
  }
  if ((el->Properties & PROP_HOT_GLOW) && p_temp(p) > el->HighTemperature - 800.0f) {
    float ht = el->HighTemperature, temp = p_temp(p);
    float gradv = 3.14159265f / (2 * ht - (ht - 800.0f));
    int caddress = (int)(temp > ht ? ht - (ht - 800.0f) : temp - (ht - 800.0f));
    s->cr += (int)(sinf(gradv * caddress) * 226);
    s->cg += (int)(-sinf(gradv * caddress * 4.55f) * 34);
    s->cb += (int)(-sinf(gradv * caddress * 2.22f) * 64);
  }
  int pm = allowed_modes(s->pm, rm);
  if (cm & COLOUR_HEAT) {
    s->fa = 255;
    uint32_t c = heat_colour(p_temp(p));
    s->fr = s->cr = c >> 16 & 255, s->fg = s->cg = c >> 8 & 255, s->fb = s->cb = c & 255;
    s->ca = 255;
    if (pm & (FIREMODE | PMODE_GLOW)) pm = (pm & ~(FIREMODE | PMODE_GLOW)) | PMODE_BLUR;
    else if ((pm & (PMODE_BLEND | PMODE_ADD)) == (PMODE_BLEND | PMODE_ADD)) pm = (pm & ~(PMODE_BLEND | PMODE_ADD)) | PMODE_FLAT;
    else if (!pm) pm |= PMODE_FLAT;
  } else if (cm & COLOUR_LIFE) {
    int q = p->life < 5 ? p->life : (int)sqrtf((float)p->life);
    s->cr = s->cg = s->cb = (int)(sinf(0.4f * q) * 100 + 128);
    s->ca = 255;
    if (pm & (FIREMODE | PMODE_GLOW)) pm = (pm & ~(FIREMODE | PMODE_GLOW)) | PMODE_BLUR;
    else if ((pm & (PMODE_BLEND | PMODE_ADD)) == (PMODE_BLEND | PMODE_ADD)) pm = (pm & ~(PMODE_BLEND | PMODE_ADD)) | PMODE_FLAT;
    else if (!pm) pm |= PMODE_FLAT;
  } else if (cm & COLOUR_BASC) {
    s->cr = colour >> 16 & 255, s->cg = colour >> 8 & 255, s->cb = colour & 255;
    pm = PMODE_FLAT;
  } else if (cm & COLOUR_DEST) {
    int score = destructibility(t);
    s->cr = s->cg = s->cb = score < 100 ? (int)(score * 1.3f) : 255;
    pm = PMODE_FLAT;
  }
  if (!(cm & ~COLOUR_GRAD) && deca) {
    deca++;
    if (!(pm & NO_DECO)) {
      s->cr = (deca * decr + (256 - deca) * s->cr) >> 8;
      s->cg = (deca * decg + (256 - deca) * s->cg) >> 8;
      s->cb = (deca * decb + (256 - deca) * s->cb) >> 8;
    }
    if (pm & DECO_FIRE) {
      s->fr = (deca * decr + (256 - deca) * s->fr) >> 8;
      s->fg = (deca * decg + (256 - deca) * s->fg) >> 8;
      s->fb = (deca * decb + (256 - deca) * s->fb) >> 8;
    }
  }
  if (cm & COLOUR_GRAD) {
    int q = (int)(p_temp(p) - 40);
    float v = sinf(0.05f * q) * 16;
    s->cr = (int)(v + s->cr), s->cg = (int)(v + s->cg), s->cb = (int)(v + s->cb);
    if (pm & (FIREMODE | PMODE_GLOW)) pm = (pm & ~(FIREMODE | PMODE_GLOW)) | PMODE_BLUR;
  }
#define CLAMP255(v) ((v) = (v) > 255 ? 255 : (v) < 0 ? 0 : (v))
  CLAMP255(s->cr), CLAMP255(s->cg), CLAMP255(s->cb), CLAMP255(s->ca);
  CLAMP255(s->fr), CLAMP255(s->fg), CLAMP255(s->fb), CLAMP255(s->fa);
  s->pm = pm;
}

/* the modes a particle of each element may be drawn with this frame, as shade() changes them */
static int frame_modes(int t) {
  int pm = PMODE_FLAT | (int)el_graphics_modes[t];
  uint32_t rm = ren.render_mode, cm = ren.colour_mode;
  if (cm & (COLOUR_BASC | COLOUR_DEST)) return PMODE_FLAT;
  GraphicsFn g = element_graphics(t);
  if (g == NULL) pm = PMODE_FLAT;
  else if (g == default_graphics) pm = default_modes(t);
  pm = allowed_modes(pm, rm);
  if (cm & (COLOUR_HEAT | COLOUR_LIFE | COLOUR_GRAD))
    if (pm & (FIREMODE | PMODE_GLOW)) pm = (pm & ~(FIREMODE | PMODE_GLOW)) | PMODE_BLUR;
  return pm;
}

/* how many rows away from its own a particle's drawing reaches: REACH_DYN works it out for the particle */
enum { REACH_DYN = 126, REACH_ALL = 127, FIRES = 128 };
static uint8_t reach_of[PT_COUNT];   /* the reach, and FIRES if its fire may glow */
static void frame_tables(void) {
  for (int t = 0; t < PT_COUNT; t++) {
    int pm = frame_modes(t), r = 0;
    if (pm & PMODE_BLOB) r = 1;
    if (pm & PMODE_BLUR) r = 3;
    if (pm & PMODE_GLOW) r = 5;
    if (pm & (EFFECT_GRAVIN | EFFECT_GRAVOUT)) r = 16;
    if (pm & (PMODE_SPARK | PMODE_FLARE | EFFECT_LINES)) r = REACH_DYN;
    if (pm & PMODE_LFLARE) r = REACH_ALL;
    if (pm & PSPEC_STICKMAN) r = 0;
    reach_of[t] = (uint8_t)(r | ((pm & FIREMODE) ? FIRES : 0));
  }
}
static int dyn_reach(const Particle *p, int t) {
  int pm = frame_modes(t), r = (pm & PMODE_GLOW) ? 5 : (pm & PMODE_BLUR) ? 3 : 1;
  if (pm & PMODE_SPARK) {
    float g = 4.0f * p->life + 19;
    int n = 0;
    while (g > 0.5f && n < YRES) g /= 1.5f, n++;
    if (n > r) r = n;
  }
  if (pm & PMODE_FLARE) {
    float g = 19 + fabsf(p_vx(p)) * 17 + fabsf(p_vy(p)) * 17;
    if (g > 255) g = 255;
    int n = 0;
    while (g > 0.5f && n < YRES) g /= 1.2f, n++;
    if (n > r) r = n;
  }
  if ((pm & EFFECT_LINES) && t == PT_SOAP && (p->ctype & 3) == 3 && p->tmp >= 0 && p->tmp < npart_max) {
    float dx = p_x(&parts[p->tmp]) - p_cx(p), dy = p_y(&parts[p->tmp]) - p_cy(p);
    SOAP_neighbourLoop(&dx, &dy);
    int n = abs((int)(p_cy(p) + dy + 0.5f) - p_cy(p)) + 1;
    if (n > r) r = n;
  }
  return r;
}

/* how far a particle's drawing reaches this frame (REACH_ALL: everywhere) */
static int part_reach(const Particle *p, int t) {
  int r = reach_of[t] & ~FIRES;
  /* (PIPE and PPIP reach as far as what they carry) */
  if ((t == PT_PIPE || t == PT_PPIP) && TYP(p->ctype) > 0 && TYP(p->ctype) < PT_COUNT) {
    r = reach_of[TYP(p->ctype)] & ~FIRES;
    if (r == REACH_DYN) r = REACH_ALL;
  }
  return r == REACH_DYN ? dyn_reach(p, t) : r;
}

/* ---------------------------------------------------------------- the band's 24-bit pixels */
/* a band's particle rows: 8 (two rows of the glow's cells), 4 when the gravity lens needs three bands at once */
static int brows = 2 * ACELL;
#define BAND_ROWS brows
#define COVERED 0x1000000u   /* drawn over (PMODE_FLAT): the walls' patterns don't show */
static uint32_t (*pb)[XRES];
static int pb_y0;

static inline bool in_band(int x, int y) { return (unsigned)x < XRES && (unsigned)(y - pb_y0) < (unsigned)BAND_ROWS; }
static inline void pb_draw(int x, int y, uint32_t c) {
  if (in_band(x, y)) pb[y - pb_y0][x] = (c & 0xFFFFFF) | COVERED;
}
static inline void pb_blend(int x, int y, int r, int g, int b, int a) {
  if (!in_band(x, y)) return;
  uint32_t *d = &pb[y - pb_y0][x];
  a &= 255;
  if (a == 255) {
    *d = (uint32_t)r << 16 | g << 8 | b | COVERED;
    return;
  }
  uint32_t o = *d;
  uint32_t nr = (uint16_t)(a * r + (255 - a) * (o >> 16 & 255)) / 255;
  uint32_t ng = (uint16_t)(a * g + (255 - a) * (o >> 8 & 255)) / 255;
  uint32_t nb = (uint16_t)(a * b + (255 - a) * (o & 255)) / 255;
  *d = nr << 16 | ng << 8 | nb | (o & COVERED);
}
static inline void pb_add(int x, int y, int r, int g, int b, int a) {
  if (!in_band(x, y)) return;
  uint32_t *d = &pb[y - pb_y0][x], o = *d;
  a &= 255;
  uint32_t nr = (o >> 16 & 255) + (uint16_t)(a * r) / 255;
  uint32_t ng = (o >> 8 & 255) + (uint16_t)(a * g) / 255;
  uint32_t nb = (o & 255) + (uint16_t)(a * b) / 255;
  *d = (nr > 255 ? 255 : nr) << 16 | (ng > 255 ? 255 : ng) << 8 | (nb > 255 ? 255 : nb) | (o & COVERED);
}
/* RasterizeLine: a line's pixels, both ends included */
typedef void (*PixFn)(int x, int y, int r, int g, int b, int a);
static void pb_line(int x1, int y1, int x2, int y2, int r, int g, int b, int a, PixFn fn) {
  int dx = abs(x2 - x1), dy = abs(y2 - y1), sx = x1 < x2 ? 1 : -1, sy = y1 < y2 ? 1 : -1;
  /* (skipped when it can't cross the band) */
  if ((y1 < pb_y0 && y2 < pb_y0) || (y1 >= pb_y0 + BAND_ROWS && y2 >= pb_y0 + BAND_ROWS)) return;
  int err = dx - dy;
  for (int n = 0; n <= dx + dy + 1; n++) {
    fn(x1, y1, r, g, b, a);
    if (x1 == x2 && y1 == y2) break;
    int e2 = 2 * err;
    if (e2 > -dy) err -= dy, x1 += sx;
    if (e2 < dx) err += dx, y1 += sy;
  }
}

/* orbitalparts_get (orbitalparts.h): PRTI and PRTO's four orbiting dots */
static void orbitalparts_get(int block1, int block2, int resblock1[4], int resblock2[4]) {
  resblock1[0] = (block1 & 0x000000FF);
  resblock1[1] = (block1 & 0x0000FF00) >> 8;
  resblock1[2] = (block1 & 0x00FF0000) >> 16;
  resblock1[3] = (block1 & 0xFF000000) >> 24;
  resblock2[0] = (block2 & 0x000000FF);
  resblock2[1] = (block2 & 0x0000FF00) >> 8;
  resblock2[2] = (block2 & 0x00FF0000) >> 16;
  resblock2[3] = (block2 & 0xFF000000) >> 24;
}

/* render_parts' drawing of one particle, clipped to the band */
static int drawing_budget;
static void draw_part(int i, const Particle *p, int t, int nx, int ny, Shade *s) {
  int pm = s->pm, cr = s->cr, cg = s->cg, cb = s->cb, ca = s->ca;
  if (pm == PMODE_FLAT) {
    pb_draw(nx, ny, (uint32_t)cr << 16 | cg << 8 | cb);
    return;
  }
  if (pm & EFFECT_LINES) {
    if (t == PT_SOAP && (p->ctype & 3) == 3 && p->tmp >= 0 && p->tmp < npart_max) {
      const Particle *q = &parts[p->tmp];
      float dx = p_x(q) - nx, dy = p_y(q) - ny;
      SOAP_neighbourLoop(&dx, &dy);   /* (across the edges when they loop, the short way) */
      pb_line(nx, ny, (int)(nx + dx + 0.5f), (int)(ny + dy + 0.5f), cr, cg, cb, ca, pb_blend);
    }
  }
  if (pm & PMODE_FLAT) pb_draw(nx, ny, (uint32_t)cr << 16 | cg << 8 | cb);
  if (pm & PMODE_BLEND) pb_blend(nx, ny, cr, cg, cb, ca);
  if (pm & PMODE_ADD) pb_add(nx, ny, cr, cg, cb, ca);
  if (pm & PMODE_BLOB) {
    pb_draw(nx, ny, (uint32_t)cr << 16 | cg << 8 | cb);
    pb_blend(nx + 1, ny, cr, cg, cb, 223);
    pb_blend(nx - 1, ny, cr, cg, cb, 223);
    pb_blend(nx, ny + 1, cr, cg, cb, 223);
    pb_blend(nx, ny - 1, cr, cg, cb, 223);
    pb_blend(nx + 1, ny - 1, cr, cg, cb, 112);
    pb_blend(nx - 1, ny - 1, cr, cg, cb, 112);
    pb_blend(nx + 1, ny + 1, cr, cg, cb, 112);
    pb_blend(nx - 1, ny + 1, cr, cg, cb, 112);
  }
  if (pm & PMODE_GLOW) {
    int cola1 = (5 * ca) / 255;
    pb_add(nx, ny, cr, cg, cb, (192 * ca) / 255);
    pb_add(nx + 1, ny, cr, cg, cb, (96 * ca) / 255);
    pb_add(nx - 1, ny, cr, cg, cb, (96 * ca) / 255);
    pb_add(nx, ny + 1, cr, cg, cb, (96 * ca) / 255);
    pb_add(nx, ny - 1, cr, cg, cb, (96 * ca) / 255);
    for (int x = 1; x < 6; x++) {
      pb_add(nx, ny - x, cr, cg, cb, cola1);
      pb_add(nx, ny + x, cr, cg, cb, cola1);
      pb_add(nx - x, ny, cr, cg, cb, cola1);
      pb_add(nx + x, ny, cr, cg, cb, cola1);
      for (int y = 1; y < 6; y++) {
        if (x + y > 7) continue;
        pb_add(nx + x, ny - y, cr, cg, cb, cola1);
        pb_add(nx - x, ny + y, cr, cg, cb, cola1);
        pb_add(nx + x, ny + y, cr, cg, cb, cola1);
        pb_add(nx - x, ny - y, cr, cg, cb, cola1);
      }
    }
  }
  if (pm & PMODE_BLUR) {
    for (int x = -3; x < 4; x++)
      for (int y = -3; y < 4; y++) {
        int ax = abs(x), ay = abs(y);
        if (ax + ay < 2 && !(ax == 2 || ay == 2)) pb_blend(x + nx, y + ny, cr, cg, cb, 30);
        if (ax + ay <= 3 && ax + ay) pb_blend(x + nx, y + ny, cr, cg, cb, 20);
        if (ax + ay == 2) pb_blend(x + nx, y + ny, cr, cg, cb, 10);
      }
  }
  if (pm & PMODE_SPARK) {
    float flicker = (float)(grng_u32() % 20);
    float gradv = 4 * p->life + flicker;
    for (int x = 0; gradv > 0.5f && drawing_budget > 0; x++) {
      int r = cr * (int)gradv / 255, g = cg * (int)gradv / 255, b = cb * (int)gradv / 255;
      r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
      pb_add(nx + x, ny, r, g, b, 255);
      pb_add(nx - x, ny, r, g, b, 255);
      pb_add(nx, ny + x, r, g, b, 255);
      pb_add(nx, ny - x, r, g, b, 255);
      gradv = gradv / 1.5f;
      drawing_budget--;
    }
  }
  if (pm & (PMODE_FLARE | PMODE_LFLARE)) {
    for (int k = 0; k < 2; k++) {
      int mode = k ? PMODE_LFLARE : PMODE_FLARE;
      if (!(pm & mode)) continue;
      float flicker = (float)(grng_u32() % 20);
      float gradv = flicker + fabsf(p_vx(p)) * 17 + fabsf(p_vy(p)) * 17;
      int a4 = (int)(gradv * 4 > 255 ? 255 : gradv * 4), a2 = (int)(gradv * 2 > 255 ? 255 : gradv * 2);
      pb_blend(nx, ny, cr, cg, cb, a4);
      pb_blend(nx + 1, ny, cr, cg, cb, a2);
      pb_blend(nx - 1, ny, cr, cg, cb, a2);
      pb_blend(nx, ny + 1, cr, cg, cb, a2);
      pb_blend(nx, ny - 1, cr, cg, cb, a2);
      if (gradv > 255) gradv = 255;
      pb_blend(nx + 1, ny - 1, cr, cg, cb, (int)gradv);
      pb_blend(nx - 1, ny - 1, cr, cg, cb, (int)gradv);
      pb_blend(nx + 1, ny + 1, cr, cg, cb, (int)gradv);
      pb_blend(nx - 1, ny + 1, cr, cg, cb, (int)gradv);
      float div = k ? 1.01f : 1.2f;
      for (int x = 1; gradv > 0.5f && drawing_budget > 0; x++) {
        /* (a whole arm past the band in rows and columns is skipped, its remaining length counted) */
        pb_add(nx + x, ny, cr, cg, cb, (int)gradv);
        pb_add(nx - x, ny, cr, cg, cb, (int)gradv);
        pb_add(nx, ny + x, cr, cg, cb, (int)gradv);
        pb_add(nx, ny - x, cr, cg, cb, (int)gradv);
        gradv = gradv / div;
        drawing_budget--;
        if (x > XRES && x > YRES) {
          while (gradv > 0.5f && drawing_budget > 0) gradv /= div, drawing_budget--;
          break;
        }
      }
    }
  }
  if (pm & (EFFECT_GRAVIN | EFFECT_GRAVOUT)) {
    int orbd[4], orbl[4];
    /* (the distances: the original's 32-bit life is life and tmp2 here, el_special.c) */
    orbitalparts_get((int)((uint32_t)(uint16_t)p->life | (uint32_t)(uint16_t)p->tmp2 << 16), p->ctype, orbd, orbl);
    int other = (pm & EFFECT_GRAVIN) ? PT_PRTI : PT_PRTO;
    for (int r = 0; r < 4; r++) {
      float ddist = (float)orbd[r] / 16.0f;
      float drad = ((float)orbl[r] * 3.14159265f / 180.0f) * 1.41421356f;
      int nxo = (int)(ddist * cosf(drad)), nyo = (int)(ddist * sinf(drad));
      if (ny + nyo > 0 && ny + nyo < YRES && nx + nxo > 0 && nx + nxo < XRES && TYP(PMAPAT(nx + nxo, ny + nyo)) != other)
        pb_add(nx + nxo, ny + nyo, cr, cg, cb, 255 - orbd[r]);
    }
  }
  (void)i;
}

/* render_parts' fire: a particle's glow added to its cell (in the particles' order, before any band) */
static void fire_part(int nx, int ny, Shade *s) {
  int pm = s->pm, firea = s->fa;
  if (!firea) return;
  int cy = ny / ACELL, cx = nx / ACELL;
  fire_nz |= 1u << cy;
  uint8_t *r = &fire_r[cy][cx], *g = &fire_g[cy][cx], *b = &fire_b[cy][cx];
  if (pm & FIRE_BLEND) {
    firea /= 2;
    *r = (uint8_t)((firea * s->fr + (255 - firea) * *r) >> 8);
    *g = (uint8_t)((firea * s->fg + (255 - firea) * *g) >> 8);
    *b = (uint8_t)((firea * s->fb + (255 - firea) * *b) >> 8);
  }
  if (pm & FIRE_ADD) {
    firea /= 8;
    int fr = ((firea * s->fr) >> 8) + *r, fg = ((firea * s->fg) >> 8) + *g, fb = ((firea * s->fb) >> 8) + *b;
    *r = (uint8_t)(fr > 255 ? 255 : fr), *g = (uint8_t)(fg > 255 ? 255 : fg), *b = (uint8_t)(fb > 255 ? 255 : fb);
  }
  if (pm & FIRE_SPARK) {
    firea /= 4;
    *r = (uint8_t)((firea * s->fr + (255 - firea) * *r) >> 8);
    *g = (uint8_t)((firea * s->fg + (255 - firea) * *g) >> 8);
    *b = (uint8_t)((firea * s->fb + (255 - firea) * *b) >> 8);
  }
}

/* a particle the original's loop draws: on the grid, not under a photon (unless an energy particle or a stickman) */
static inline bool drawn_at(const Particle *p, int t, int *nx, int *ny) {
  *nx = p_cx(p), *ny = p_cy(p);
  if (*nx >= XRES || *nx < 0 || *ny >= YRES || *ny < 0) return false;
  if (photons_id[*ny][*nx] && !(elements[t].Properties & TYPE_ENERGY) && t != PT_STKM && t != PT_STKM2 && t != PT_FIGH)
    return false;
  return true;
}

/* the powered walls' glow (DrawWalls' end), then every particle's fire (render_parts) */
static void fire_pass(void) {
  for (int y = 0; y < YWCELLS; y++)
    for (int x = 0; x < XWCELLS; x++) {
      int wt = bmap[y][x];
      if (!wt || wt >= UI_WALLCOUNT || !emap[y][x] || !walltypes[wt].eglow) continue;
      uint32_t glow = walltypes[wt].eglow;
      /* (alpha 255: the cell's old glow doesn't count) */
      int cy = y * WCELL / ACELL, cx = x * WCELL / ACELL;
      fire_nz |= 1u << cy;
      fire_r[cy][cx] = (uint8_t)((255 * (glow >> 16 & 255)) >> 8);
      fire_g[cy][cx] = (uint8_t)((255 * (glow >> 8 & 255)) >> 8);
      fire_b[cy][cx] = (uint8_t)((255 * (glow & 255)) >> 8);
    }
  if (!(ren.render_mode & FIREMODE)) return;
  Shade s;
  for (int i = 0; i < parts_active; i++) {
    const Particle *p = &parts[i];
    int t = p->type, nx, ny;
    if (!t || !(reach_of[t] & FIRES) || !drawn_at(p, t, &nx, &ny)) continue;
    shade(i, p, t, nx, ny, &s);
    fire_part(nx, ny, &s);
  }
}

/* the bytes of a and b added, each at most 255 (UQADD8) */
static inline uint32_t sat_add8(uint32_t a, uint32_t b) {
#if defined(__ARM_FEATURE_SIMD32) && __ARM_FEATURE_SIMD32
  uint32_t r;
  __asm__("uqadd8 %0, %1, %2" : "=r"(r) : "r"(a), "r"(b));
  return r;
#else
  uint32_t r = 0;
  for (int k = 0; k < 32; k += 8) {
    uint32_t v = (a >> k & 255) + (b >> k & 255);
    r |= (v > 255 ? 255 : v) << k;
  }
  return r;
#endif
}

/* render_fire for the band: the glow of the cells around it */
static void fire_glow(int c0, int n) {
  for (int j = c0 - 1; j <= c0 + n; j++) {
    if (j < 0 || j >= YACELLS || !(fire_nz >> j & 1)) continue;
    /* (only the glow's rows in the band) */
    int y0 = pb_y0 - j * ACELL, y1 = pb_y0 + BAND_ROWS - j * ACELL;
    if (y0 < -ACELL) y0 = -ACELL;
    if (y1 > 2 * ACELL) y1 = 2 * ACELL;
    /* (the alphas' last row and column are 0: they add nothing) */
    if (y1 > 2 * ACELL - 1) y1 = 2 * ACELL - 1;
    for (int i = 0; i < XACELLS; i++) {
      int r = fire_r[j][i], g = fire_g[j][i], b = fire_b[j][i];
      if (!(r | g | b)) continue;
      int x0 = i ? -ACELL : 0, x1 = i < XACELLS - 1 ? 2 * ACELL - 1 : ACELL;
      for (int y = y0; y < y1; y++) {
        uint32_t *row = pb[j * ACELL + y - pb_y0] + i * ACELL;
        const uint8_t *al = fire_alpha[y + ACELL] + ACELL;
        for (int x = x0; x < x1; x++) {
          int a = al[x];
          /* each channel plus a / 255 of the glow's, at most 255 (COVERED kept) */
          row[x] = sat_add8(row[x], (uint32_t)((a * r) / 255) << 16 | (uint32_t)((a * g) / 255) << 8 | (uint32_t)((a * b) / 255));
        }
      }
    }
  }
}
/* render_fire's fading of a row of cells (in the original's order: the cells above already faded) */
static void fire_fade(int j) {
  /* (a row with no glow around stays dark) */
  if (!((fire_nz << 1) >> j & 7)) return;
  bool any = false;
  for (int i = 0; i < XACELLS; i++) {
    int r = fire_r[j][i] * 8, g = fire_g[j][i] * 8, b = fire_b[j][i] * 8;
    for (int y = -1; y < 2; y++)
      for (int x = -1; x < 2; x++)
        if ((x || y) && i + x >= 0 && j + y >= 0 && i + x < XACELLS && j + y < YACELLS)
          r += fire_r[j + y][i + x], g += fire_g[j + y][i + x], b += fire_b[j + y][i + x];
    r /= 16, g /= 16, b /= 16;
    fire_r[j][i] = (uint8_t)(r > 4 ? r - 4 : 0);
    fire_g[j][i] = (uint8_t)(g > 4 ? g - 4 : 0);
    fire_b[j][i] = (uint8_t)(b > 4 ? b - 4 : 0);
    any |= fire_r[j][i] | fire_g[j][i] | fire_b[j][i];
  }
  fire_nz = any ? fire_nz | 1u << j : fire_nz & ~(1u << j);
}

/* ---------------------------------------------------------------- the band's background */
/* draw_air: a cell's colour */
static uint32_t air_colour(int y, int x) {
  uint32_t dm = ren.display_mode;
  if (dm & DISPLAY_AIRP) {
    float pres = pv[y][x];
    return pres > 0.0f ? (uint32_t)clamp_flt(pres, 0.0f, 8.0f) << 16 : (uint32_t)clamp_flt(-pres, 0.0f, 8.0f);
  }
  if (dm & DISPLAY_AIRV)
    return (uint32_t)clamp_flt(fabsf(vx[y][x]), 0.0f, 8.0f) << 16 | (uint32_t)clamp_flt(pv[y][x], 0.0f, 8.0f) << 8 |
           (uint32_t)clamp_flt(fabsf(vy[y][x]), 0.0f, 8.0f);
  if (dm & DISPLAY_AIRH) return heat_to_colour(hv[y][x]);
  if (dm & DISPLAY_AIRC) {
    int r = (int)clamp_flt(fabsf(vx[y][x]), 0.0f, 24.0f) + (int)clamp_flt(fabsf(vy[y][x]), 0.0f, 20.0f);
    int g = (int)clamp_flt(fabsf(vx[y][x]), 0.0f, 20.0f) + (int)clamp_flt(fabsf(vy[y][x]), 0.0f, 24.0f);
    int b = (int)clamp_flt(fabsf(vx[y][x]), 0.0f, 24.0f) + (int)clamp_flt(fabsf(vy[y][x]), 0.0f, 20.0f);
    if (pv[y][x] > 0.0f) r += (int)clamp_flt(pv[y][x], 0.0f, 16.0f);
    else b += (int)clamp_flt(-pv[y][x], 0.0f, 16.0f);
    r = r > 255 ? 255 : r, g = g > 255 ? 255 : g, b = b > 255 ? 255 : b;
    /* (the original's RGB keeps the low 8 bits of what it's given) */
    return (uint32_t)(uint8_t)r << 16 | (uint32_t)(uint8_t)g << 8 | (uint8_t)b;
  }
  if (dm & DISPLAY_AIRW) {
    /* Air::vorticity: positive (clockwise) red, negative blue */
    float w = 0.0f;
    if (x > 1 && x < XACELLS - 2 && y > 1 && y < YACELLS - 2) w = 4 * ((vy[y][x + 1] - vy[y][x - 1]) - (vx[y + 1][x] - vx[y - 1][x])) * 0.5f;
    return w > 0.0f ? (uint32_t)clamp_flt(w, 0.0f, 8.0f) << 16 : (uint32_t)clamp_flt(-w, 0.0f, 8.0f);
  }
  return 0;
}

/* the persistent display's trails: the last frame's pixels (as the screen shows them), fading */
typedef struct { int16_t x0, y0, x1, y1; } SRect;
#define PERS_RECTS 12
static SRect pers_skip[PERS_RECTS];   /* the overlay's text and boxes last frame: not trails */
static int npers_skip;
static void pers_background(uint16_t *tmp) {
  for (int h = 0; h < BAND_ROWS / 2; h++) {
    int sy0 = pb_y0 * PX_SCALE + h * 6;
    plat_pull(SIM_X, sy0, SIM_W, 6, tmp);
    for (int r = 0; r < 2; r++) {
      int y = pb_y0 + h * 2 + r, sy = y * PX_SCALE + 1;
      for (int x = 0; x < XRES; x++) {
        int sx = SIM_X + x * PX_SCALE + 1;
        bool skip = false;
        for (int k = 0; k < npers_skip && !skip; k++)
          skip = sx >= pers_skip[k].x0 && sx < pers_skip[k].x1 && sy >= pers_skip[k].y0 && sy < pers_skip[k].y1;
        /* (not the overlay's last frame, nor the walls, which the original draws again over the trails) */
        uint16_t c = skip || bmap[y / WCELL][x / WCELL] ? 0 : tmp[(sy - sy0) * SIM_W + sx - SIM_X];
        /* Decay: each component down by one a frame, here by one step of the screen's 5 or 6 bits every 8 or 4
         * frames, spread over the pixels */
        int phase = (int)frame_no + x * 3 + y * 5;
        int rr = c >> 11, gg = (c >> 5) & 63, bb = c & 31;
        if (rr && !(phase & 7)) rr--;
        if (gg && !(phase & 3)) gg--;
        if (bb && !((phase + 4) & 7)) bb--;
        pb[y - pb_y0][x] = rgb888((uint16_t)(rr << 11 | gg << 5 | bb));
      }
    }
  }
}

/* draw_grav: the Newtonian gravity field's lines */
static void grav_lines(void) {
  for (int y = 0; y < YACELLS; y++)
    for (int x = 0; x < XACELLS; x++) {
      float gx = gravx[y][x], gy = gravy[y][x];
      float agx = fabsf(gx), agy = fabsf(gy);
      if (agx <= 0.001f && agy <= 0.001f) continue;
      float npx = (float)(x * ACELL), npy = (float)(y * ACELL), dist = agx + agy;
      int a = (uint8_t)(int)(dist * 20.0f);
      for (int i = 0; i < 4; ++i) {
        npx -= gx * 0.5f, npy -= gy * 0.5f;
        pb_add((int)(npx + 0.5f), (int)(npy + 0.5f), 255, 255, 255, a);
      }
    }
}

/* DrawWalls' streamlines: from each streamline wall, a line following the air */
static void streamlines(void) {
  for (int y = 0; y < YWCELLS; y++)
    for (int x = 0; x < XWCELLS; x++) {
      if (bmap[y][x] != WL_STREAM) continue;
      float xf = x * WCELL + WCELL * 0.5f, yf = y * WCELL + WCELL * 0.5f;
      int oldX = (int)(xf + 0.5f), oldY = (int)(yf + 0.5f);
      float xVel = vx[oldY / ACELL][oldX / ACELL] * 0.125f, yVel = vy[oldY / ACELL][oldX / ACELL] * 0.125f;
      if (!xVel && !yVel) {
        pb_add(oldX, oldY, 255, 255, 255, 255);
        continue;
      }
      bool changed = false;
      for (int t = 0; t < 1024; t++) {
        int newX = (int)(xf + 0.5f), newY = (int)(yf + 0.5f);
        if (newX != oldX || newY != oldY) changed = true, oldX = newX, oldY = newY;
        if (changed && (newX < 0 || newX >= XRES || newY < 0 || newY >= YRES)) break;
        pb_add(newX, newY, 255, 255, 255, 64);
        if (changed && newX >= 0 && newY >= 0 && newX < XRES && newY < YRES) {
          int wallX = newX / WCELL, wallY = newY / WCELL;
          xVel = vx[newY / ACELL][newX / ACELL] * 0.125f, yVel = vy[newY / ACELL][newX / ACELL] * 0.125f;
          if (wallX != x && wallY != y && bmap[wallY][wallX] == WL_STREAM) break;
        }
        xf += xVel, yf += yVel;
      }
    }
}

/* draw_other: the EMP's flash over everything */
static void emp_flash(void) {
  int emp = emp_decor;
  if (emp > 40) emp = 40;
  if (emp < 0) emp = 0;
  if (!(ren.render_mode & EFFECT) || emp <= 0) return;
  int r = (int)(emp * 2.5), g = (int)(100 + emp * 1.5), b = 255, a = (int)((1.0 * emp / 110) * 255);
  if (r > 255) r = 255;
  if (g > 255) g = 255;
  for (int y = pb_y0; y < pb_y0 + BAND_ROWS; y++)
    for (int x = 0; x < XRES; x++) pb_blend(x, y, r, g, b, a);
}

/* ---------------------------------------------------------------- walls, at the screen's resolution */
/* DrawWalls' patterns, with the original's 4-pixel cells' patterns kept at the screen's pixels: a wall's pixel at
 * (sx, sy), or -1 where the wall shows what's behind */
static int32_t wall_px(int wt, bool powered, int sx, int sy, int i, int j) {
  const WallType *w = &walltypes[wt];
  uint32_t pc = w->colour, gc = w->eglow;
  switch (w->drawstyle) {
    case 0:
      if (wt == WL_EWALL || wt == WL_STASIS) {
        bool on = (powered) ^ (wt == WL_STASIS);
        return (on ? (sx & sy & 1) : !(sx & sy & 1)) ? (int32_t)pc : -1;
      }
      if (wt == WL_WALLELEC) return !(sy & 1) && !(sx & 1) ? (int32_t)pc : 0x808080;
      if (wt == WL_EHOLE) {
        if (powered) return !(sy & 1) && !(sx & 1) ? 0x000000 : 0x242424;
        return !(sy & 1) && !(sx & 1) ? 0x242424 : -1;
      }
      return -1;
    case 1: return !(sy & 1) && (sx & 1) == ((sy >> 1) & 1) ? (int32_t)pc : -1;
    case 2: return !(sy & 1) && !(sx & 1) ? (int32_t)pc : -1;
    case 3: return (int32_t)pc;
    case 4: {
      int d = ((i - j) % (WCELL * PX_SCALE) + WCELL * PX_SCALE) % (WCELL * PX_SCALE);
      return d == 0 ? (int32_t)pc : d == 1 ? (int32_t)gc : 0x202020;
    }
  }
  return -1;
}

/* ---------------------------------------------------------------- stickmen, at the screen's resolution */
static inline int scr(float v) { return (int)v * PX_SCALE + 1; }
static void stickman_draw(Band *b, int i) {
  const Particle *p = &parts[i];
  int t = p->type, nx, ny;
  if (!drawn_at(p, t, &nx, &ny)) return;
  const playerst *cp;
  if (t == PT_STKM) cp = &player;
  else if (t == PT_STKM2) cp = &player2;
  else if (t == PT_FIGH && p->tmp >= 0 && p->tmp < MAX_FIGHTERS) cp = &fighters[(unsigned char)p->tmp];
  else return;
  uint32_t col, leg;
  if (ren.colour_mode == COLOUR_HEAT) {
    col = heat_colour(p_temp(p));
  } else if (cp->fan) {
    col = 0x8080FF;
  } else if (cp->elem < PT_NUM && cp->elem > 0) {
    col = elements[cp->elem].Colour;
  } else {
    col = 0x8080FF;
  }
  leg = ren.colour_mode == COLOUR_HEAT ? col : t == PT_STKM2 ? 0x6464FF : 0xFFFFFF;
  int x = SIM_X + scr((float)nx), y = scr((float)ny);
  if (t == PT_FIGH) {
    band_line(b, x, y + 6, x + 6, y, col, 255, false);
    band_line(b, x + 6, y, x, y - 6, col, 255, false);
    band_line(b, x, y - 6, x - 6, y, col, 255, false);
    band_line(b, x - 6, y, x, y + 6, col, 255, false);
  } else {
    band_line(b, x - 6, y + 6, x + 6, y + 6, col, 255, false);
    band_line(b, x - 6, y - 6, x + 6, y - 6, col, 255, false);
    band_line(b, x - 6, y - 6, x - 6, y + 6, col, 255, false);
    band_line(b, x + 6, y - 6, x + 6, y + 6, col, 255, false);
  }
  int hx = x, hy = scr((float)(ny + 3));
  band_line(b, hx, hy, SIM_X + scr(cp->legs[0]), scr(cp->legs[1]), leg, 255, false);
  band_line(b, SIM_X + scr(cp->legs[0]), scr(cp->legs[1]), SIM_X + scr(cp->legs[4]), scr(cp->legs[5]), leg, 255, false);
  band_line(b, hx, hy, SIM_X + scr(cp->legs[8]), scr(cp->legs[9]), leg, 255, false);
  band_line(b, SIM_X + scr(cp->legs[8]), scr(cp->legs[9]), SIM_X + scr(cp->legs[12]), scr(cp->legs[13]), leg, 255, false);
  if (cp->rocketBoots)
    for (int l = 0; l < 2; l++) {
      int fx = SIM_X + scr(cp->legs[l * 8 + 4]), fy = scr(cp->legs[l * 8 + 5]);
      bool on = (cp->comm & 0x04) || ((cp->comm & 0x01) && l == 0) || ((cp->comm & 0x02) && l == 1);
      band_fill(b, fx - 1, fy - 1, 3, 3, on ? 0x00FF00 : 0xFF0000, 255);
      band_fill(b, fx - 4, fy - 1, 3, 3, 0xFF00FF, 223);
      band_fill(b, fx + 2, fy - 1, 3, 3, 0xFF00FF, 223);
      band_fill(b, fx - 1, fy - 4, 3, 3, 0xFF00FF, 223);
      band_fill(b, fx - 1, fy + 2, 3, 3, 0xFF00FF, 223);
      band_fill(b, fx + 2, fy - 4, 3, 3, 0xFF00FF, 112);
      band_fill(b, fx - 4, fy - 4, 3, 3, 0xFF00FF, 112);
      band_fill(b, fx + 2, fy + 2, 3, 3, 0xFF00FF, 112);
      band_fill(b, fx - 4, fy + 2, 3, 3, 0xFF00FF, 112);
    }
}

/* ---------------------------------------------------------------- a frame */
#define SBAND 6   /* screen rows a band */
static bool any_stream, any_wall_row[YWCELLS], air_on;
static int16_t stkm_ids[2 + MAX_FIGHTERS];   /* the stickmen to draw this frame */
static int nstkm;
static uint32_t tparts;

/* the band each particle is drawn in, worked out once a frame rather than every particle tested in every band: a
 * nibble a particle (8 a word, in sim_scratch, which the simulation doesn't hold anything in between its frames),
 * the band when its drawing stays in one, BAND_MANY when it may reach others (tested in each band, as before),
 * BAND_NONE when it isn't drawn. Each band visits its particles in the same order as before. */
#define BAND_NONE 0xEu
#define BAND_MANY 0xFu
/* (NULL: every particle tested in every band, when there are more bands than nibbles have room for) */
static uint32_t *band_sort(int nbands) {
  int band_words = (parts_active + 7) / 8;
  if (nbands > (int)BAND_NONE || band_words * 4 > SCRATCH) return NULL;
  uint32_t *band_nib = (uint32_t *)(void *)sim_scratch;
  for (int w = 0; w < band_words; w++) {
    uint32_t word = 0;
    for (int k = 7; k >= 0; k--) {
      int i = w * 8 + k, t, nx, ny;
      uint32_t v = BAND_NONE;
      const Particle *p = &parts[i];
      if (i < parts_active && (t = p->type) && drawn_at(p, t, &nx, &ny)) {
        int r = part_reach(p, t);
        /* the bands whose rows [b * BAND_ROWS, (b + 1) * BAND_ROWS) come within r of the particle's row */
        v = r == 0 ? (uint32_t)(ny / BAND_ROWS) : (ny - r) / BAND_ROWS == (ny + r) / BAND_ROWS && ny >= r ? (uint32_t)(ny / BAND_ROWS) : BAND_MANY;
      }
      word = word << 4 | v;
    }
    band_nib[w] = word;
  }
  return band_nib;
}

/* a particle drawn by render_parts in the band */
static inline void band_part(int i, const Particle *p, int t, Shade *s) {
  int nx = p_cx(p), ny = p_cy(p);
  shade(i, p, t, nx, ny, s);
  draw_part(i, p, t, nx, ny, s);
  if (ny >= pb_y0 && ny < pb_y0 + BAND_ROWS) render_found++;
}

/* a band of particle rows into pb (from pb_y0): the background, the particles, the fire glow, the EMP's flash */
static void band_render(int cr, uint16_t *tmp, const uint32_t *band_nib) {
  pb_y0 = cr * BAND_ROWS;
  int c0 = pb_y0 / ACELL, nc = BAND_ROWS / ACELL;
  /* Clear and RenderBackground */
  if (ren.display_mode & DISPLAY_PERS) {
    pers_background(tmp);
  } else {
    /* (cleared a word at a time: the library's memset goes a byte at a time) */
    uint32_t *d = pb[0], *e = d + BAND_ROWS * XRES;
    while (d < e) d[0] = d[1] = d[2] = d[3] = 0, d += 4;
  }
  if (air_on)
    for (int k = 0; k < nc; k++)
      for (int x = 0; x < XACELLS; x++) {
        uint32_t c = air_colour(c0 + k, x);
        for (int y = 0; y < ACELL; y++)
          for (int i = 0; i < ACELL; i++) pb[k * ACELL + y][x * ACELL + i] = c;
      }
  if (ren.grav_field) grav_lines();
  if (any_stream) streamlines();
  /* render_parts */
  uint32_t tp = plat_millis();
  drawing_budget = 1000000;
  Shade s;
  if (band_nib) {
    /* the particles whose nibble is this band's or BAND_MANY (a nibble is 0 in x where it equals the band's,
     * 0 in ~w where it's BAND_MANY), in order */
    uint32_t mine = (uint32_t)cr * 0x11111111u;
    for (int w = 0, band_words = (parts_active + 7) / 8; w < band_words; w++) {
      uint32_t word = band_nib[w], x = word ^ mine, m = ~word;
      x = ~(((x & 0x77777777u) + 0x77777777u) | x) & 0x88888888u;
      m = ~(((m & 0x77777777u) + 0x77777777u) | m) & 0x88888888u;
      for (uint32_t hit = x | m; hit; hit &= hit - 1) {
        int k = __builtin_ctz(hit) >> 2, i = w * 8 + k;
        const Particle *p = &parts[i];
        int t = p->type;
        if (m >> (k * 4) & 8) {   /* BAND_MANY */
          int r = part_reach(p, t), ny = p_cy(p);
          if (ny < pb_y0 - r || ny >= pb_y0 + BAND_ROWS + r) continue;
        }
        band_part(i, p, t, &s);
      }
    }
  } else {
    for (int i = 0; i < parts_active; i++) {
      const Particle *p = &parts[i];
      int t = p->type, nx, ny;
      if (!t) continue;
      int r = part_reach(p, t);
      ny = p_cy(p);
      if (ny < pb_y0 - r || ny >= pb_y0 + BAND_ROWS + r) continue;
      if (!drawn_at(p, t, &nx, &ny)) continue;
      band_part(i, p, t, &s);
    }
  }
  tparts += plat_millis() - tp;
  /* render_fire */
  if (ren.render_mode & FIREMODE) {
    fire_glow(c0, nc);
    /* (the rows the next band's glow doesn't need, in order) */
    for (int j = c0 - 1; j < c0 + nc - 1; j++)
      if (j >= 0) fire_fade(j);
    if (c0 + nc == YACELLS) fire_fade(YACELLS - 1);
  }
  emp_flash();
}

/* render_gravlensing: the frame's pixels moved by Newtonian gravity, red the least and blue the most. Here a band
 * sees the band above and below it (the moves are cut to 4 particles) */
static uint32_t (*lens_ring[3])[XRES];
static int lens_band;
static inline uint32_t lens_src(int x, int y, int comp) {
  int b = y / BAND_ROWS;
  if (b < lens_band - 1) b = lens_band - 1, y = b * BAND_ROWS;
  if (b > lens_band + 1) b = lens_band + 1, y = b * BAND_ROWS + BAND_ROWS - 1;
  return lens_ring[b % 3][y - b * BAND_ROWS][x] >> comp & 255;
}
static uint32_t lens_px(int x, int y) {
  float fx = gravx[y / ACELL][x / ACELL], fy = gravy[y / ACELL][x / ACELL];
  int rx = (int)(x - fx * 0.75f + 0.5f), ry = (int)(y - fy * 0.75f + 0.5f);
  int gx = (int)(x - fx * 0.875f + 0.5f), gy = (int)(y - fy * 0.875f + 0.5f);
  int bx = (int)(x - fx + 0.5f), by = (int)(y - fy + 0.5f);
  if (!InBounds(rx, ry) || !InBounds(gx, gy) || !InBounds(bx, by)) return 0;
  return lens_src(rx, ry, 16) << 16 | lens_src(gx, gy, 8) << 8 | lens_src(bx, by, 0) |
         (lens_ring[lens_band % 3][y - lens_band * BAND_ROWS][x] & COVERED);
}

/* a band shown: 3 x 3 pixels a particle, the walls' patterns, the stickmen, the overlay */
/* a particle's 3 x 3 pixels at d (rows w apart): its colour, or a wall's pattern with the colour added (the
 * original draws the walls first) */
static void show_cell(uint16_t *d, int w, int x, int y, uint32_t c, int wt) {
  if (!wt || wt >= UI_WALLCOUNT || (c & COVERED) || walltypes[wt].drawstyle == 3) {
    /* (no pattern, or a wall all of its colour: the colour added, the same in all 9) */
    if (wt && wt < UI_WALLCOUNT && !(c & COVERED)) c = sat_add8(c & 0xFFFFFF, walltypes[wt].colour & 0xFFFFFF);
    uint16_t v = rgb565(c);
    d[0] = d[1] = d[2] = v;
    d[w] = d[w + 1] = d[w + 2] = v;
    d[2 * w] = d[2 * w + 1] = d[2 * w + 2] = v;
    return;
  }
  bool powered = emap[y / WCELL][x / WCELL] != 0;
  int cx0 = (x / WCELL) * WCELL * PX_SCALE, cy0 = (y / WCELL) * WCELL * PX_SCALE;
  for (int k = 0; k < PX_SCALE; k++)
    for (int m = 0; m < PX_SCALE; m++) {
      int sx = x * PX_SCALE + m, sy = y * PX_SCALE + k;
      int32_t wp = wall_px(wt, powered, sx, sy, sx - cx0, sy - cy0);
      uint32_t out = c;
      if (wp >= 0) {
        uint32_t rr = (wp >> 16 & 255) + (c >> 16 & 255), gg = (wp >> 8 & 255) + (c >> 8 & 255);
        uint32_t bb = (wp & 255) + (c & 255);
        out = (rr > 255 ? 255 : rr) << 16 | (gg > 255 ? 255 : gg) << 8 | (bb > 255 ? 255 : bb);
      }
      d[k * w + m] = rgb565(out);
    }
}

/* two pixels as one word, a left of b, in the CPU's byte order */
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define PX_PAIR(a, b) ((a) << 16 | (b))
#else
#define PX_PAIR(a, b) ((a) | (b) << 16)
#endif
static int push_w;   /* the sim's bands go to the screen up to here (an unchanging menu at the right isn't redrawn) */
static void band_show(int cr, uint32_t (*src)[XRES], bool lens, const Overlay *o, uint16_t *sbuf) {
  int y0 = cr * BAND_ROWS, w = SCREEN_W;
  _Static_assert(WCELL == 2 && XRES % 2 == 0, "two particles a wall cell");
  for (int h = 0; h < BAND_ROWS / 2; h++) {
    Band b = {sbuf, 0, y0 * PX_SCALE + h * SBAND, w, SBAND};
    for (int r = 0; r < 2; r++) {
      int y = y0 + h * 2 + r;
      uint16_t *row = sbuf + r * PX_SCALE * w + SIM_X;
      const uint8_t *walls = any_wall_row[y / WCELL] ? bmap[y / WCELL] : NULL;
      if (lens) {
        for (int x = 0; x < XRES; x++) show_cell(row + x * PX_SCALE, w, x, y, lens_px(x, y), walls ? walls[x / WCELL] : 0);
        continue;
      }
      /* two particles (a wall cell) at a time: without a wall pattern showing, three words a row */
      const uint32_t *sp = src[y - y0];
      uint32_t *d0 = (uint32_t *)(void *)row, *d1 = (uint32_t *)(void *)(row + w), *d2 = (uint32_t *)(void *)(row + 2 * w);
      for (int x = 0; x < XRES; x += 2, sp += 2, d0 += 3, d1 += 3, d2 += 3) {
        int wt = walls ? walls[x / WCELL] : 0;
        if (wt && wt < UI_WALLCOUNT && !(sp[0] & sp[1] & COVERED)) {
          show_cell(row + x * PX_SCALE, w, x, y, sp[0], wt);
          show_cell(row + (x + 1) * PX_SCALE, w, x + 1, y, sp[1], wt);
          continue;
        }
        uint32_t a = rgb565(sp[0]), b = rgb565(sp[1]);
        uint32_t w0 = a | a << 16, w1 = PX_PAIR(a, b), w2 = b | b << 16;
        d0[0] = w0, d0[1] = w1, d0[2] = w2;
        d1[0] = w0, d1[1] = w1, d1[2] = w2;
        d2[0] = w0, d2[1] = w1, d2[2] = w2;
      }
    }
    /* (the screen's sides by the sim: black, under the overlay) */
    for (int r = 0; r < SBAND; r++) {
      uint32_t *l = (uint32_t *)(void *)(sbuf + r * w), *rr = (uint32_t *)(void *)(sbuf + r * w + SIM_X + SIM_W);
      for (int k = 0; k < SIM_X / 2; k++) l[k] = 0;
      for (int k = 0; k < (SCREEN_W - SIM_X - SIM_W) / 2; k++) rr[k] = 0;
    }
    /* the streamline walls' mark (DrawWalls: glyph U+E00D) */
    if (any_stream)
      for (int y = 0; y < YWCELLS; y++)
        for (int x = 0; x < XWCELLS; x++)
          if (bmap[y][x] == WL_STREAM)
            font_draw_char(&b, SIM_X + x * WCELL * PX_SCALE, y * WCELL * PX_SCALE, FONT_ICON(0x0D), 0xFFFFFF, 128, FONT_BLEND);
    if (ren.render_mode & PSPEC_STICKMAN)
      for (int k = 0; k < nstkm; k++) stickman_draw(&b, stkm_ids[k]);
    if (o) ov_draw(o, &b);
    if (push_w < w)
      /* (the rows packed, a word at a time, forwards: each moves down) */
      for (int r = 1; r < SBAND; r++) {
        uint32_t *d = (uint32_t *)(void *)(sbuf + r * push_w), *sp = (uint32_t *)(void *)(sbuf + r * w);
        for (int k = 0; k < push_w / 2; k++) d[k] = sp[k];
      }
    plat_push(0, b.y0, push_w, SBAND, sbuf);
  }
}

void render_frame(const Overlay *o, bool panel, int sim_push_w) {
  uint32_t t0 = plat_millis();
  push_w = panel ? SCREEN_W : sim_push_w;
  frame_no++;
  /* the bands' pixels: one, or three when the gravity lens needs the bands around */
  bool lens = (ren.display_mode & DISPLAY_WARP) && ngrav_enable;
  int nring = lens ? 3 : 1;
  brows = lens ? ACELL : 2 * ACELL;
  int nbands = YRES / brows;
  uint32_t ring[nring * brows][XRES];
  uint16_t sbuf[SCREEN_W * SBAND] __attribute__((aligned(4)));
  frame_tables();
  hdisp_adjust();
  fire_pass();
  nstkm = 0;
  if (elementCount[PT_STKM] || elementCount[PT_STKM2] || elementCount[PT_FIGH])
    for (int i = 0; i < parts_active && nstkm < 2 + MAX_FIGHTERS; i++) {
      int t = parts[i].type;
      if (t == PT_STKM || t == PT_STKM2 || t == PT_FIGH) stkm_ids[nstkm++] = (int16_t)i;
    }
  any_stream = false;
  for (int y = 0; y < YWCELLS; y++) {
    any_wall_row[y] = false;
    for (int x = 0; x < XWCELLS; x++)
      if (bmap[y][x] && bmap[y][x] < UI_WALLCOUNT) any_wall_row[y] = true, any_stream |= bmap[y][x] == WL_STREAM;
  }
  air_on = (ren.display_mode & DISPLAY_AIR) && !(!aheat_enable && (ren.display_mode & DISPLAY_AIRH));
  render_found = 0;
  tparts = 0;
  const uint32_t *nib = band_sort(nbands);
  for (int k = 0; k < nring; k++) lens_ring[k] = ring + k * brows;
  for (int cr = 0; cr <= nbands; cr++) {
    if (cr < nbands) {
      pb = ring + (cr % nring) * brows;
      band_render(cr, sbuf, nib);
    } else if (lens) {
      memset(ring + (cr % nring) * brows, 0, sizeof(uint32_t) * XRES * brows);   /* (below the sim: nothing) */
    }
    int show = lens ? cr - 1 : cr;
    if (show < 0 || show >= nbands) continue;
    lens_band = show;
    band_show(show, ring + (show % nring) * brows, lens, o, sbuf);
  }
  /* the overlay's text and boxes, which the persistent display's trails leave out next frame */
  npers_skip = 0;
  for (int k = 0; o && k < o->n && npers_skip < PERS_RECTS; k++) {
    const OvOp *op = &o->op[k];
    if (op->kind == OV_FILL || (op->kind >= OV_TEXT && op->kind <= OV_TEXT_BIG)) {
      int x0 = op->x - 1, y0 = op->y - 3, x1 = op->x + op->w + 1, y1 = op->y + op->h + 1;
      if (x0 < SIM_X + SIM_W && y0 < SIM_H) pers_skip[npers_skip++] = (SRect){(int16_t)x0, (int16_t)y0, (int16_t)x1, (int16_t)y1};
    }
  }
  if (panel)
    for (int y = SIM_H; y < SCREEN_H; y += SBAND) {
      Band b = {sbuf, 0, y, SCREEN_W, SBAND};
      memset(sbuf, 0, sizeof sbuf);
      if (o) ov_draw(o, &b);
      plat_push(0, y, SCREEN_W, SBAND, sbuf);
    }
  render_ms_parts = tparts;
  render_ms_total = plat_millis() - t0;
}
