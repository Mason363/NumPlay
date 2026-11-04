#include "gfx.h"
#include <string.h>
#include "../../games/common/np_text.h"

color_t *gfx_buf;
int gfx_y0, gfx_y1;
int gfx_clip_x0 = 0, gfx_clip_x1 = SCREEN_W;

void gfx_init(void) { gfx_buf = np_alloc(SCREEN_W * STRIP_H * sizeof(color_t)); }

#ifndef __TICE__
void gfx_render(gfx_scene_t scene, void *ctx, int y0, int y1) {
  for (int y = y0; y < y1; y += STRIP_H) {
    gfx_y0 = y;
    gfx_y1 = NP_MIN(y + STRIP_H, y1);
    scene(ctx);
    np_push(0, y, SCREEN_W, gfx_y1 - y, gfx_buf);
  }
}

static inline color_t *px_at(int x, int y) { return gfx_buf + (y - gfx_y0) * SCREEN_W + x; }

color_t gfx_mix(color_t a, color_t b, int t) {
  uint32_t A = (a | (uint32_t)a << 16) & 0x07E0F81F, B = (b | (uint32_t)b << 16) & 0x07E0F81F;
  uint32_t M = ((A * (uint32_t)(32 - t) + B * (uint32_t)t) >> 5) & 0x07E0F81F;
  return (color_t)(M | M >> 16);
}

#endif

color_t gfx_rgb(uint32_t c) { return RGB((c >> 16) & 0xFF, (c >> 8) & 0xFF, c & 0xFF); }

uint32_t gfx_lerp888(uint32_t a, uint32_t b, int t) {
  uint32_t out = 0;
  for (int s = 0; s < 24; s += 8) {
    int ca = (a >> s) & 0xFF, cb = (b >> s) & 0xFF;
    out |= (uint32_t)((ca * (256 - t) + cb * t) >> 8) << s;
  }
  return out;
}

#ifdef __TICE__
#include "../../platform/ti84ce/gfx_ce.c" /* the TI-84 Plus CE's own */
#else
static bool clip(int *x, int *y, int *w, int *h) {
  int x0 = NP_MAX(*x, 0), y0 = NP_MAX(*y, gfx_y0);
  int x1 = NP_MIN(*x + *w, SCREEN_W), y1 = NP_MIN(*y + *h, gfx_y1);
  if (x1 <= x0 || y1 <= y0) return false;
  *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
  return true;
}

static void span(color_t *p, int n, color_t c) {
  if (n > 0 && ((uintptr_t)p & 2)) *p++ = c, n--;
  uint32_t two = c | (uint32_t)c << 16, *q = (uint32_t *)p;
  for (int i = 0; i < n / 2; i++) q[i] = two;
  if (n & 1) p[n - 1] = c;
}

void gfx_fill(int x, int y, int w, int h, color_t c) {
  if (!clip(&x, &y, &w, &h)) return;
  for (int j = 0; j < h; j++) span(px_at(x, y + j), w, c);
}

static void span_alpha(color_t *p, int n, color_t c, int t) {
  if (t >= 32) {
    span(p, n, c);
    return;
  }
  if (t <= 0) return;
  uint32_t C = ((c | (uint32_t)c << 16) & 0x07E0F81F) * (uint32_t)t;
  for (int i = 0; i < n; i++) {
    uint32_t A = (p[i] | (uint32_t)p[i] << 16) & 0x07E0F81F;
    uint32_t M = ((A * (uint32_t)(32 - t) + C) >> 5) & 0x07E0F81F;
    p[i] = (color_t)(M | M >> 16);
  }
}

void gfx_fill_alpha(int x, int y, int w, int h, color_t c, int a) {
  if (!clip(&x, &y, &w, &h)) return;
  int t = (a + 4) >> 3;
  for (int j = 0; j < h; j++) span_alpha(px_at(x, y + j), w, c, t);
}

/* Ordered dithering hides the banding of 16-bit colour in soft gradients. */
static const uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
  int x0 = x, y0 = y, h0 = h;
  if (!clip(&x, &y, &w, &h)) return;
  (void)x0;
  for (int j = 0; j < h; j++) {
    int row = y + j;
    int t = h0 > 1 ? (row - y0) * 256 / (h0 - 1) : 0;
    uint32_t c = gfx_lerp888(top, bottom, t);
    int r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
    color_t pat[4];
    for (int i = 0; i < 4; i++) {
      int d = bayer[row & 3][i];
      int r5 = NP_MIN(31, (r + (d >> 1)) >> 3), g6 = NP_MIN(63, (g + (d >> 2)) >> 2), b5 = NP_MIN(31, (b + (d >> 1)) >> 3);
      pat[i] = (color_t)(r5 << 11 | g6 << 5 | b5);
    }
    color_t *p = px_at(x, row);
    for (int i = 0; i < w; i++) p[i] = pat[(x + i) & 3];
  }
}

static inline float clampf(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

/* Coverage of pixel column i (0 = leftmost) of a rounded corner of radius r,
 * dy = vertical distance from the corner's centre row. */
static int corner_cov(int i, int r, float dy) {
  float dx = r - i - 0.5f;
  float d = __builtin_sqrtf(dx * dx + dy * dy) - r;
  return (int)(32 * clampf(0.5f - d) + 0.5f);
}

static float row_dy(int j, int h, int r) {
  if (j < r) return r - j - 0.5f;
  if (j >= h - r) return j + 0.5f - (h - r);
  return 0;
}

void gfx_rrect(int x, int y, int w, int h, int r, color_t c, int a) {
  if (w <= 0 || h <= 0) return;
  if (2 * r > w) r = w / 2;
  if (2 * r > h) r = h / 2;
  int ya = NP_MAX(y, gfx_y0), yb = NP_MIN(y + h, gfx_y1);
  int t = (a + 4) >> 3;
  for (int row = ya; row < yb; row++) {
    float dy = row_dy(row - y, h, r);
    int solid0 = x, solid1 = x + w;
    if (dy > 0) {
      for (int i = 0; i < r; i++) {
        int cov = corner_cov(i, r, dy);
        if (cov >= 32) break;
        int k = cov * t >> 5;
        if (k > 0) {
          if (x + i >= 0 && x + i < SCREEN_W) span_alpha(px_at(x + i, row), 1, c, k);
          int xr = x + w - 1 - i;
          if (xr >= 0 && xr < SCREEN_W) span_alpha(px_at(xr, row), 1, c, k);
        }
        solid0 = x + i + 1;
        solid1 = x + w - i - 1;
      }
    }
    int s0 = NP_MAX(solid0, 0), s1 = NP_MIN(solid1, SCREEN_W);
    if (s1 > s0) span_alpha(px_at(s0, row), s1 - s0, c, t);
  }
}

/* Signed distance from a pixel centre to a rounded box (negative inside). */
static float box_dist(float px, float py, float hw, float hh, float r) {
  float qx = __builtin_fabsf(px) - (hw - r), qy = __builtin_fabsf(py) - (hh - r);
  float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0;
  float in = qx > qy ? qx : qy;
  return __builtin_sqrtf(mx * mx + my * my) + (in < 0 ? in : 0) - r;
}

void gfx_rrect_outline(int x, int y, int w, int h, int r, int th, color_t c, int a) {
  if (w <= 0 || h <= 0) return;
  int ya = NP_MAX(y, gfx_y0), yb = NP_MIN(y + h, gfx_y1);
  int t = (a + 4) >> 3;
  float hw = w * 0.5f, hh = h * 0.5f, ri = r > th ? r - th : 0;
  for (int row = ya; row < yb; row++) {
    float py = row + 0.5f - (y + hh);
    for (int i = 0; i < w; i++) {
      float px = i + 0.5f - hw;
      float d_in = box_dist(px, py, hw - th, hh - th, ri);
      if (d_in < -0.5f) {
        if (i < w / 2) i = w - 1 - i - 1;  /* the rest up to the mirror pixel is inside */
        continue;
      }
      float cov = clampf(0.5f - box_dist(px, py, hw, hh, r)) - clampf(0.5f - d_in);
      int k = (int)(cov * t + 0.5f);
      int sx = x + i;
      if (k > 0 && sx >= 0 && sx < SCREEN_W) span_alpha(px_at(sx, row), 1, c, k);
    }
  }
}

void gfx_shadow(int x, int y, int w, int h, int r, int blur, int a) {
  int ya = NP_MAX(y - blur, gfx_y0), yb = NP_MIN(y + h + blur, gfx_y1);
  float hw = w * 0.5f, hh = h * 0.5f, cx = x + hw, cy = y + hh;
  int xa = x - blur, xb = x + w + blur;
  for (int row = ya; row < yb; row++) {
    float py = row + 0.5f - cy;
    for (int sx = xa; sx < xb; sx++) {
      float d = box_dist(sx + 0.5f - cx, py, hw, hh, r);
      if (d <= 0) {
        int mirror = (int)(2 * cx) - 1 - sx;  /* inside: the card will cover it */
        if (mirror > sx) sx = mirror - 1;
        continue;
      }
      if (d >= blur || sx < 0 || sx >= SCREEN_W) continue;
      float f = 1 - d / blur;
      int k = (int)(f * f * a) >> 3;
      if (k > 0) span_alpha(px_at(sx, row), 1, 0, k);
    }
  }
}

void gfx_circle(int cx, int cy, int r, color_t c, int a) {
  gfx_rrect(cx - r, cy - r, 2 * r, 2 * r, r, c, a);
}

void gfx_image(const uint8_t *idx, const color_t *pal, int ncolors, int sw, int sh, int dx, int dy, int dw, int dh,
               int radius, int dim) {
  if (dw <= 0 || dh <= 0) return;
  int ya = NP_MAX(dy, gfx_y0), yb = NP_MIN(dy + dh, gfx_y1);
  if (ya >= yb || dx >= SCREEN_W || dx + dw <= 0) return;
  color_t dimmed[256];
  if (dim > 0) {
    int t = (dim + 4) >> 3;
    for (int i = 0; i < ncolors && i < 256; i++) dimmed[i] = gfx_mix(pal[i], 0, t);
    pal = dimmed;
  }
  if (2 * radius > dw) radius = dw / 2;
  if (2 * radius > dh) radius = dh / 2;
  uint32_t step = ((uint32_t)sw << 16) / (uint32_t)dw;
  int x0 = NP_MAX(dx, gfx_clip_x0), x1 = NP_MIN(dx + dw, gfx_clip_x1);
  for (int row = ya; row < yb; row++) {
    int j = row - dy;
    const uint8_t *src = idx + (uint32_t)(j * sh / dh) * sw;
    float fdy = row_dy(j, dh, radius);
    int in0 = 0, in1 = dw;   /* fully covered columns (relative) */
    color_t *p = px_at(0, row);
    if (fdy > 0) {
      for (int i = 0; i < radius; i++) {
        int cov = corner_cov(i, radius, fdy);
        if (cov >= 32) break;
        in0 = i + 1;
        in1 = dw - i - 1;
        if (cov <= 0) continue;
        int xl = dx + i, xr = dx + dw - 1 - i;
        if (xl >= x0 && xl < x1) p[xl] = gfx_mix(p[xl], pal[src[(uint32_t)i * step >> 16]], cov);
        if (xr >= x0 && xr < x1) p[xr] = gfx_mix(p[xr], pal[src[(uint32_t)(dw - 1 - i) * step >> 16]], cov);
      }
    }
    int a0 = NP_MAX(x0, dx + in0), a1 = NP_MIN(x1, dx + in1);
    uint32_t fx = (uint32_t)(a0 - dx) * step;
    for (int x = a0; x < a1; x++, fx += step) p[x] = pal[src[fx >> 16]];
  }
}

void gfx_mask(const uint8_t *mask, int w, int h, int x, int y, color_t c, int a) {
  int ya = NP_MAX(y, gfx_y0), yb = NP_MIN(y + h, gfx_y1);
  for (int row = ya; row < yb; row++) {
    int j = row - y;
    for (int i = 0; i < w; i++) {
      int px = x + i;
      if (px < 0 || px >= SCREEN_W) continue;
      int n = j * w + i;
      int cov = (mask[n >> 1] >> ((n & 1) * 4)) & 15;
      if (!cov) continue;
      int k = (cov * a + 60) / 120;  /* 0..32 */
      color_t *p = px_at(px, row);
      *p = gfx_mix(*p, c, NP_MIN(k, 32));
    }
  }
}

void gfx_corners(int x, int y, int w, int h, int r, color_t c) {
  if (2 * r > w) r = w / 2;
  if (2 * r > h) r = h / 2;
  int ya = NP_MAX(y, gfx_y0), yb = NP_MIN(y + h, gfx_y1);
  for (int row = ya; row < yb; row++) {
    float dy = row_dy(row - y, h, r);
    if (dy <= 0) continue;
    for (int i = 0; i < r; i++) {
      int cov = corner_cov(i, r, dy);
      if (cov >= 32) break;
      int xl = x + i, xr = x + w - 1 - i;
      if (xl >= 0 && xl < SCREEN_W) span_alpha(px_at(xl, row), 1, c, 32 - cov);
      if (xr >= 0 && xr < SCREEN_W) span_alpha(px_at(xr, row), 1, c, 32 - cov);
    }
  }
}

#endif

void gfx_icon(const np_icon_t *icon, int x, int y, color_t c, int a) { gfx_mask(icon->data, icon->w, icon->h, x, y, c, a); }

static const np_glyph_t *glyph(const np_font_t *f, char ch) {
  unsigned i = (unsigned char)ch - f->first;
  return i < f->count ? &f->glyphs[i] : &f->glyphs['?' - f->first];
}

/* Language builds (games/common/np_text.h): the letter at *s, *s moved past it; its glyph, or NULL
 * for one of the 12-pixel font (Chinese) */
static const np_glyph_t *glyph_at(const np_font_t *f, const char **s, uint32_t *cp) {
  if (!NP_TEXT_EXTRA || (uint8_t)**s < 0x80) {
    *cp = (uint8_t)**s;
    return glyph(f, *(*s)++);
  }
  *cp = np_utf8(s);
  for (int k = 0; k < np_font_extra_count; k++)
    if (np_font_extra[k] == *cp) return &f->glyphs[95 + k];
  return np_xglyph(*cp) ? NULL : &f->glyphs['?' - f->first];
}
/* the 12-pixel font's pixels: twice as big for the title font */
static int xscale(const np_font_t *f) { return f->ascent >= 16 ? 2 : 1; }
typedef struct {
  color_t c;
  int a;
} xplot_t;
static void xplot(int x, int y, void *ctx) {
  const xplot_t *p = ctx;
  if (x < 0 || x >= SCREEN_W || y < gfx_y0 || y >= gfx_y1) return;
  color_t *q = px_at(x, y);
  *q = gfx_mix(*q, p->c, NP_MIN((p->a * 32 + 60) / 120, 32));
}

int gfx_text(const np_font_t *f, int x, int baseline, const char *s, color_t c, int a) {
  int top = baseline - f->ascent, bottom = baseline + f->descent;
  bool visible = bottom > gfx_y0 && top < gfx_y1;
#ifdef __TICE__
  if (gfx_listing) { /* in the strips' lists, then only its width */
    gfx_ce_text(L_TEXT, f, x, baseline, s, c, a);
    return x + gfx_text_width(f, s);
  }
#endif
  while (*s) {
    uint32_t cp;
    const np_glyph_t *g = glyph_at(f, &s, &cp);
    if (!g) { /* (NP_TEXT_EXTRA only) */
      xplot_t p = {c, a};
      int sc = xscale(f);
      x += visible ? np_xdraw(cp, x, baseline - 10 * sc, sc, xplot, &p) : np_xadvance(cp, sc);
      continue;
    }
    if (visible && g->w) gfx_mask(f->data + g->offset, g->w, g->h, x + g->x, baseline + g->y, c, a);
    x += g->advance;
  }
  return x;
}

static int advance_at(const np_font_t *f, const char **s) {
  uint32_t cp;
  const np_glyph_t *g = glyph_at(f, s, &cp);
  return g ? g->advance : np_xadvance(cp, xscale(f));
}

int gfx_text_width(const np_font_t *f, const char *s) {
  int w = 0;
  while (*s) w += advance_at(f, &s);
  return w;
}

void gfx_text_center(const np_font_t *f, int cx, int baseline, const char *s, color_t c, int a) {
#ifdef __TICE__
  if (gfx_listing) {
    gfx_ce_text(L_TEXTC, f, cx, baseline, s, c, a);
    return;
  }
#endif
  gfx_text(f, cx - gfx_text_width(f, s) / 2, baseline, s, c, a);
}

void gfx_text_right(const np_font_t *f, int rx, int baseline, const char *s, color_t c, int a) {
#ifdef __TICE__
  if (gfx_listing) {
    gfx_ce_text(L_TEXTR, f, rx, baseline, s, c, a);
    return;
  }
#endif
  gfx_text(f, rx - gfx_text_width(f, s), baseline, s, c, a);
}

int gfx_paragraph(const np_font_t *f, int cx, int baseline, int width, int line, const char *s, color_t c, int a) {
  char buf[96];
  while (*s) {
    /* take as many words as fit (in Chinese, as many letters: a line may end after any of them) */
    int n = 0, last_space = -1, wpx = 0;
    while (s[n] && s[n] != '\n' && n < (int)sizeof(buf) - 4) {
      const char *p = s + n;
      uint32_t first = (uint8_t)*p;
      wpx += advance_at(f, &p);
      int len = (int)(p - (s + n));
      if (first == ' ') last_space = n;
      if (wpx > width && last_space > 0) {
        n = last_space;
        break;
      }
      if (NP_TEXT_EXTRA && first >= 0xE0 && wpx <= width) last_space = n + len; /* (after a Chinese letter) */
      n += len;
    }
    memcpy(buf, s, (size_t)n);
    buf[n] = 0;
    gfx_text_center(f, cx, baseline, buf, c, a);
    baseline += line;
    s += n;
    while (*s == ' ' || *s == '\n') s++;
  }
  return baseline;
}
