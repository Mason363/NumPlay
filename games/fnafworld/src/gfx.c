/* Drawing (gfx.h): a band of 16 rows at a time, then pushed to the screen. */
#include "gfx.h"
#include "res.h"

uint16_t *gfx_band;
int gfx_y0;

void gfx_set_band_memory(uint16_t *m) { gfx_band = m; }

void gfx_frame(void (*draw)(void)) {
  for (gfx_y0 = 0; gfx_y0 < SCREEN_H; gfx_y0 += BAND_H) {
    draw();
    plat_push(0, gfx_y0, SCREEN_W, BAND_H, gfx_band);
  }
  gfx_y0 = 0;
}

uint16_t gfx_mix(uint16_t f, uint16_t b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (uint16_t)(y | y >> 16);
}

/* the rectangle's part in this band: rows r0 to r1 of the band, columns x0 to x1 */
static bool clip(int x, int y, int w, int h, int *x0, int *x1, int *r0, int *r1) {
  *x0 = x > 0 ? x : 0, *x1 = x + w < SCREEN_W ? x + w : SCREEN_W;
  int y0 = y > gfx_y0 ? y : gfx_y0, y1 = y + h < gfx_y0 + BAND_H ? y + h : gfx_y0 + BAND_H;
  *r0 = y0 - gfx_y0, *r1 = y1 - gfx_y0;
  return *x0 < *x1 && *r0 < *r1;
}

void gfx_fill(int x, int y, int w, int h, uint16_t c) {
  int x0, x1, r0, r1;
  if (!clip(x, y, w, h, &x0, &x1, &r0, &r1)) return;
  for (int r = r0; r < r1; r++)
    for (uint16_t *p = gfx_band + r * SCREEN_W + x0, *e = gfx_band + r * SCREEN_W + x1; p < e; p++) *p = c;
}

void gfx_blend(int x, int y, int w, int h, uint16_t c, int a) {
  int x0, x1, r0, r1;
  if (!clip(x, y, w, h, &x0, &x1, &r0, &r1)) return;
  for (int r = r0; r < r1; r++)
    for (uint16_t *p = gfx_band + r * SCREEN_W + x0, *e = gfx_band + r * SCREEN_W + x1; p < e; p++)
      *p = gfx_mix(c, *p, a);
}

void gfx_frame_rect(int x, int y, int w, int h, uint16_t c) {
  gfx_fill(x, y, w, 1, c);
  gfx_fill(x, y + h - 1, w, 1, c);
  gfx_fill(x, y, 1, h, c);
  gfx_fill(x + w - 1, y, 1, h, c);
}

/* static: grey noise, spread out for gfx_mix (made once) */
static uint32_t noise[2048];

void gfx_static(int x, int y, int w, int h, int a, uint32_t seed) {
  int x0, x1, r0, r1;
  if (!clip(x, y, w, h, &x0, &x1, &r0, &r1) || a <= 0) return;
  if (!noise[0]) {
    uint32_t s = 0x2545F491u;
    for (int i = 0; i < 2048; i++) {
      s ^= s << 13, s ^= s >> 17, s ^= s << 5;
      uint32_t g = (s >> 24) * 3 / 4;
      uint32_t c = (g & 0xF8) << 8 | (g & 0xFC) << 3 | g >> 3;
      noise[i] = ((c | c << 16) & 0x07E0F81F) | 1u << 31;   /* (the top bit: made) */
    }
  }
  for (int r = r0; r < r1; r++) {
    uint32_t s = seed ^ (uint32_t)(gfx_y0 + r) * 0x9E3779B1u;
    s ^= s >> 15, s *= 0x2C1B3C6Du, s ^= s >> 12;
    /* a line now and then is brighter, as the game's static has */
    uint32_t la = (s & 31) == 0 ? (uint32_t)a + 6 : (uint32_t)a;
    if (la > 32) la = 32;
    const uint32_t *nz = noise + (s >> 16 & 1023);
    for (uint16_t *p = gfx_band + r * SCREEN_W + x0, *e = gfx_band + r * SCREEN_W + x1; p < e; p++) {
      uint32_t n = *nz++ & 0x07E0F81F, b = (*p | (uint32_t)*p << 16) & 0x07E0F81F;
      b = (b + ((n - b) * la >> 5)) & 0x07E0F81F;
      *p = (uint16_t)(b | b >> 16);
    }
  }
}

void gfx_dim(int x, int y, int w, int h, int a) {
  int x0, x1, r0, r1;
  if (!clip(x, y, w, h, &x0, &x1, &r0, &r1)) return;
  for (int r = r0; r < r1; r++)
    for (uint16_t *p = gfx_band + r * SCREEN_W + x0, *e = gfx_band + r * SCREEN_W + x1; p < e; p++) {
      if (a <= 32) {
        *p = gfx_mix(*p, BLACK, a);
      } else {   /* brighter: each channel times a / 32, up to 2 */
        uint32_t v = *p, R = (v >> 11) * (uint32_t)a >> 5, G = (v >> 5 & 63) * (uint32_t)a >> 5, B = (v & 31) * (uint32_t)a >> 5;
        *p = (uint16_t)((R > 31 ? 31 : R) << 11 | (G > 63 ? 63 : G) << 5 | (B > 31 ? 31 : B));
      }
    }
}

/* ------------------------------------------------------------------ text */
static const uint8_t *font_at(int font) {
  const uint8_t *s = section(SEC_FONTS);
  return s + rd32(s + 4 + 4 * font);
}

int gfx_line_h(int font) { return font_at(font)[0]; }

/* 4-bit alpha rows (the first pixel in the low half of a byte) in colour c */
static void alpha_rows(const uint8_t *rows, int w, int h, int x, int y, uint16_t c) {
  int x0, x1, r0, r1;
  if (!clip(x, y, w, h, &x0, &x1, &r0, &r1)) return;
  int stride = (w + 1) >> 1;
  for (int r = r0; r < r1; r++) {
    const uint8_t *src = rows + (gfx_y0 + r - y) * stride;
    uint16_t *d = gfx_band + r * SCREEN_W;
    for (int px = x0; px < x1; px++) {
      int i = px - x, a = src[i >> 1] >> ((i & 1) * 4) & 15;
      if (a) d[px] = a == 15 ? c : gfx_mix(c, d[px], a * 2 + (a >> 3));
    }
  }
}

int gfx_text_n(int font, int x, int y, const char *s, int n, uint16_t c) {
  const uint8_t *f = font_at(font);
  int x0 = x;
  bool here = y < gfx_y0 + BAND_H && y + f[0] > gfx_y0;
  for (; *s && n-- > 0; s++) {
    int ch = (uint8_t)*s;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *g = f + 4 + 12 * (ch - 32);
    if (here && g[1]) alpha_rows(f + rd32(g + 8), g[1], g[2], x + (int8_t)g[3], y + (int8_t)g[4], c);
    x += g[0];
  }
  return x - x0;
}

int gfx_text(int font, int x, int y, const char *s, uint16_t c) { return gfx_text_n(font, x, y, s, 1 << 30, c); }

int gfx_text_wn(int font, const char *s, int n) {
  const uint8_t *f = font_at(font);
  int w = 0;
  for (; *s && n-- > 0; s++) {
    int ch = (uint8_t)*s;
    if (ch < 32 || ch > 126) ch = '?';
    w += f[4 + 12 * (ch - 32)];
  }
  return w;
}
int gfx_text_w(int font, const char *s) { return gfx_text_wn(font, s, 1 << 30); }

void gfx_text_c(int font, int cx, int y, const char *s, uint16_t c) { gfx_text(font, cx - gfx_text_w(font, s) / 2, y, s, c); }
void gfx_text_r(int font, int rx, int y, const char *s, uint16_t c) { gfx_text(font, rx - gfx_text_w(font, s), y, s, c); }
void gfx_text_s(int font, int x, int y, const char *s, uint16_t c) {
  gfx_text(font, x + 1, y + 1, s, BLACK);
  gfx_text(font, x, y, s, c);
}

/* ------------------------------------------------------------------ small pictures */
static const uint8_t *img_at(int id) {
  const uint8_t *s = section(SEC_IMGS);
  return s + rd32(s + 4 + 4 * id);
}
int gfx_img_w(int id) { return (int)rd16(img_at(id)); }
int gfx_img_h(int id) { return (int)rd16(img_at(id) + 2); }

void gfx_img(int id, int x, int y, uint16_t c) {
  const uint8_t *p = img_at(id);
  alpha_rows(p + 8, (int)rd16(p), (int)rd16(p + 2), x, y, c);
}

/* ------------------------------------------------------------------ the map's pictures (SPRS: tools/world.py) */
static const uint8_t *spr_at(int id) {
  const uint8_t *s = section(SEC_SPRS);
  return s + rd32(s + 4 + 4 * id);
}
int gfx_spr_w(int id) { return (int)rd16(spr_at(id)); }
int gfx_spr_h(int id) { return (int)rd16(spr_at(id) + 2); }
int gfx_spr_hx(int id) { return (int)rds16(spr_at(id) + 4); }
int gfx_spr_hy(int id) { return (int)rds16(spr_at(id) + 6); }

/* sprite id with its top left corner at (x, y) */
void gfx_spr(int id, int x, int y) {
  const uint8_t *p = spr_at(id);
  int w = (int)rd16(p), h = (int)rd16(p + 2), opaque = p[8] & 1;
  int r0 = gfx_y0 - y, r1 = gfx_y0 + BAND_H - y;
  if (r0 < 0) r0 = 0;
  if (r1 > h) r1 = h;
  int c0 = x < 0 ? -x : 0, c1 = x + w > SCREEN_W ? SCREEN_W - x : w;
  if (r0 >= r1 || c0 >= c1) return;
  const uint16_t *px = (const uint16_t *)(const void *)(p + 12);
  const uint8_t *al = p + 12 + 2 * w * h;
  int stride = (w + 1) >> 1;
  for (int r = r0; r < r1; r++) {
    const uint16_t *s = px + r * w;
    uint16_t *d = gfx_band + (y + r - gfx_y0) * SCREEN_W + x;
    if (opaque) {
      for (int c = c0; c < c1; c++) d[c] = s[c];
      continue;
    }
    const uint8_t *a = al + r * stride;
    for (int c = c0; c < c1; c++) {
      int v = a[c >> 1] >> ((c & 1) * 4) & 15;
      if (v == 15) d[c] = s[c];
      else if (v) d[c] = gfx_mix(s[c], d[c], v * 2 + (v >> 3));
    }
  }
}

/* an opaque sprite tiled over the rectangle (x, y, w, h), the tiles from (tx, ty) */
void gfx_spr_tile(int id, int x, int y, int w, int h, int tx, int ty) {
  const uint8_t *p = spr_at(id);
  int sw = (int)rd16(p), sh = (int)rd16(p + 2);
  int y0 = y > gfx_y0 ? y : gfx_y0, y1 = y + h < gfx_y0 + BAND_H ? y + h : gfx_y0 + BAND_H;
  int x0 = x > 0 ? x : 0, x1 = x + w < SCREEN_W ? x + w : SCREEN_W;
  if (y0 >= y1 || x0 >= x1) return;
  const uint16_t *px = (const uint16_t *)(const void *)(p + 12);
  for (int r = y0; r < y1; r++) {
    int sr = (r - ty) % sh;
    if (sr < 0) sr += sh;
    const uint16_t *s = px + sr * sw;
    uint16_t *d = gfx_band + (r - gfx_y0) * SCREEN_W;
    int sc = (x0 - tx) % sw;
    if (sc < 0) sc += sw;
    for (int c = x0; c < x1; c++) {
      d[c] = s[sc];
      if (++sc == sw) sc = 0;
    }
  }
}
