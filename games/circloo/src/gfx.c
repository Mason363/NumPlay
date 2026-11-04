/* circloO: drawing into a band of the screen (see gfx.h). Shapes are filled a row at a time from two sub-rows,
 * with their edges' coverage blended, which smooths circles about as the game's own drawing does. */
#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <stdint.h>
#include "font.h"
#include "gfx.h"
#include "plat.h"

static uint16_t *fb;
static int by0;
static const Font *const fonts[4] = {&font_small, &font_medium, &font_large, &font_big};

void gfx_init(uint16_t *band) { fb = band; }
void gfx_band(int y0) { by0 = y0; }
void gfx_push(void) {
  int h = SCREEN_H - by0 < BAND ? SCREEN_H - by0 : BAND;
  plat_push(0, by0, SCREEN_W, h, fb);
}

uint16_t rgb(int r, int g, int b) {
  r = r < 0 ? 0 : r > 255 ? 255 : r, g = g < 0 ? 0 : g > 255 ? 255 : g, b = b < 0 ? 0 : b > 255 ? 255 : b;
  return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}

uint16_t mix(uint16_t a, uint16_t b, int t) {
  if (t <= 0) return a;
  if (t >= 256) return b;
  int r = ((a >> 11) * (256 - t) + (b >> 11) * t) >> 8;
  int g = (((a >> 5) & 63) * (256 - t) + ((b >> 5) & 63) * t) >> 8;
  int bl = ((a & 31) * (256 - t) + (b & 31) * t) >> 8;
  return (uint16_t)(r << 11 | g << 5 | bl);
}

void gfx_fill(uint16_t c) {
  int n = SCREEN_W * BAND;
  for (int i = 0; i < n; i++) fb[i] = c;
}

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

/* one row from its two sub-rows' spans (a > b: empty) */
static void row(int y, float a0, float b0, float a1, float b1, uint16_t c, int alpha) {
  bool e0 = a0 >= b0, e1 = a1 >= b1;
  if (e0 && e1) return;
  float lo = e0 ? a1 : e1 ? a0 : fminf(a0, a1), hi = e0 ? b1 : e1 ? b0 : fmaxf(b0, b1);
  float in_lo = e0 || e1 ? 1e9f : fmaxf(a0, a1), in_hi = e0 || e1 ? -1e9f : fminf(b0, b1);
  int x0 = (int)floorf(lo), x1 = (int)ceilf(hi);
  if (x0 < 0) x0 = 0;
  if (x1 > SCREEN_W) x1 = SCREEN_W;
  uint16_t *p = fb + (y - by0) * SCREEN_W;
  int f0 = (int)ceilf(in_lo), f1 = (int)floorf(in_hi);   /* fully covered pixels: [f0, f1) */
  for (int x = x0; x < x1; x++) {
    int cov;
    if (x >= f0 && x < f1) cov = 256;
    else {
      float c0 = e0 ? 0 : clampf(fminf(b0, x + 1.0f) - fmaxf(a0, (float)x), 0, 1);
      float c1 = e1 ? 0 : clampf(fminf(b1, x + 1.0f) - fmaxf(a1, (float)x), 0, 1);
      cov = (int)((c0 + c1) * 128);
    }
    if (alpha < 256) cov = cov * alpha >> 8;
    if (cov >= 256) p[x] = c;
    else if (cov > 0) p[x] = mix(p[x], c, cov);
  }
}

static bool rows(float top, float bot, int *y0, int *y1) {
  *y0 = (int)floorf(top), *y1 = (int)ceilf(bot);
  if (*y0 < by0) *y0 = by0;
  if (*y1 > by0 + BAND) *y1 = by0 + BAND;
  if (*y1 > SCREEN_H) *y1 = SCREEN_H;
  return *y0 < *y1;
}

void gfx_disc(float cx, float cy, float r, uint16_t c, int alpha) {
  int y0, y1;
  if (r <= 0 || alpha <= 0 || cx + r < 0 || cx - r > SCREEN_W || !rows(cy - r, cy + r, &y0, &y1)) return;
  float r2 = r * r;
  for (int y = y0; y < y1; y++) {
    float s[4];
    for (int k = 0; k < 2; k++) {
      float dy = y + 0.25f + 0.5f * k - cy, h2 = r2 - dy * dy;
      if (h2 <= 0) s[2 * k] = 1, s[2 * k + 1] = 0;
      else {
        float h = sqrtf(h2);
        s[2 * k] = cx - h, s[2 * k + 1] = cx + h;
      }
    }
    row(y, s[0], s[1], s[2], s[3], c, alpha);
  }
}

void gfx_poly(const float *x, const float *y, int n, uint16_t c, int alpha) {
  float top = y[0], bot = y[0], l = x[0], r = x[0];
  for (int i = 1; i < n; i++) top = fminf(top, y[i]), bot = fmaxf(bot, y[i]), l = fminf(l, x[i]), r = fmaxf(r, x[i]);
  int y0, y1;
  if (alpha <= 0 || r < 0 || l > SCREEN_W || !rows(top, bot, &y0, &y1)) return;
  for (int yy = y0; yy < y1; yy++) {
    float s[4];
    for (int k = 0; k < 2; k++) {
      float sy = yy + 0.25f + 0.5f * k, a = 1e9f, b = -1e9f;
      for (int i = 0; i < n; i++) {
        int j = (i + 1) % n;
        float ya = y[i], yb = y[j];
        if ((sy >= ya && sy < yb) || (sy >= yb && sy < ya)) {
          float t = (sy - ya) / (yb - ya), xx = x[i] + t * (x[j] - x[i]);
          a = fminf(a, xx), b = fmaxf(b, xx);
        }
      }
      s[2 * k] = a, s[2 * k + 1] = b;
    }
    row(yy, s[0], s[1], s[2], s[3], c, alpha);
  }
}

void gfx_rect(float x0, float y0, float x1, float y1, uint16_t c, int alpha) {
  float x[4] = {x0, x1, x1, x0}, y[4] = {y0, y0, y1, y1};
  gfx_poly(x, y, 4, c, alpha);
}

void gfx_line(float x1, float y1, float x2, float y2, float w, uint16_t c, int alpha) {
  float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy);
  if (l < 1e-6f) return;
  float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
  float x[4] = {x1 + nx, x2 + nx, x2 - nx, x1 - nx}, y[4] = {y1 + ny, y2 + ny, y2 - ny, y1 - ny};
  gfx_poly(x, y, 4, c, alpha);
}

/* ---- text ---- */

int gfx_font_h(int font) { return fonts[font]->height; }

int gfx_text_w(int font, const char *s, int n) {
  const Font *f = fonts[font];
  int w = 0;
  for (int i = 0; i < n && s[i]; i++) {
    unsigned ch = (unsigned char)s[i];
    if (ch >= 32 && ch <= 126) w += f->g[ch - 32].adv;
  }
  return w;
}

void gfx_text(int font, float x, float y, const char *s, int n, uint16_t c, int alpha, int align) {
  const Font *f = fonts[font];
  int px = (int)lroundf(x), py = (int)lroundf(y);
  if (py > by0 + BAND || py + f->height < by0 || alpha <= 0) return;
  if (align == AL_CENTER) px -= gfx_text_w(font, s, n) / 2;
  else if (align == AL_RIGHT) px -= gfx_text_w(font, s, n);
  for (int i = 0; i < n && s[i]; i++) {
    unsigned ch = (unsigned char)s[i];
    if (ch < 32 || ch > 126) continue;
    const Glyph *g = &f->g[ch - 32];
    for (int gy = 0; gy < g->h; gy++) {
      int yy = py + g->y + gy;
      if (yy < by0 || yy >= by0 + BAND || yy >= SCREEN_H) continue;
      uint16_t *p = fb + (yy - by0) * SCREEN_W;
      for (int gx = 0; gx < g->w; gx++) {
        int xx = px + g->x + gx;
        if (xx < 0 || xx >= SCREEN_W) continue;
        int k = gy * g->w + gx, v = f->bits[g->off + (k >> 1)];
        v = k & 1 ? v >> 4 : v & 15;
        if (!v) continue;
        int a = v * 17 + (v >> 1);
        if (alpha < 256) a = a * alpha >> 8;
        p[xx] = mix(p[xx], c, a);
      }
    }
    px += g->adv;
  }
}

int gfx_wrap(int font, float x, float y, const char *s, int w, uint16_t c, int alpha, int align, int draw) {
  int lines = 0, h = fonts[font]->height;
  while (*s) {
    int n = 0, last = -1;
    while (s[n] && s[n] != '#') {
      if (s[n] == ' ') last = n;
      if (gfx_text_w(font, s, n + 1) > w && last > 0) break;
      n++;
    }
    int len = s[n] && s[n] != '#' ? last : n;
    if (draw) gfx_text(font, x, y + lines * h, s, len, c, alpha, align);
    lines++;
    s += len;
    if (*s == ' ' || *s == '#') s++;
  }
  return lines;
}
