/* Drawing (gfx.h): the frame's commands, then each band of rows painted with every command that reaches it. */
#include <math.h>
#include <string.h>
#include "gfx.h"

enum { C_FILL, C_RRECT, C_VGRAD, C_ROWS, C_IMAGE, C_BLIT, C_TEXT, C_CIRCLE, C_RING, C_TUBE, C_GROUND, C_LIQUID };

typedef struct {
  uint8_t type, alpha;
  int16_t x0, y0, x1, y1;          /* the pixels it may touch: [x0, x1) x [y0, y1) */
  union {
    struct { uint16_t c; } fill;
    struct { float x, y, w, h; uint16_t c; int16_t r; } rr;
    struct { uint32_t top, bottom; int16_t y, h; } grad;
    struct { const uint16_t *rows; } rows;
    struct { const Img *im; int32_t ua, ub, va, vb, u0, v0; } image;   /* u = u0 + ua x + ub y (16.16) */
    struct { const Img *im; int16_t x, y; } blit;                        /* unscaled, its top-left */
    struct { const char *s; uint8_t font; uint16_t c, oc; int16_t x, y; } text;
    struct { float x, y, r, t; uint16_t c; } circle;
    struct { float ax, ay, bx, by, r; uint16_t c; } tube;
  };
} Cmd;

static Cmd cmds[GFX_MAX_CMDS];
static int ncmds;
static char pool[GFX_TEXT_POOL];
static int npool;
static uint16_t *band;
static int by0, by1;   /* the band being painted */
Ground gfx_ground;

void gfx_init(uint16_t *b) { band = b; }
void gfx_begin(void) { ncmds = npool = 0; }

static Cmd *add(int type, int x0, int y0, int x1, int y1, int alpha) {
  if (ncmds >= GFX_MAX_CMDS || alpha <= 0) return NULL;
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 > SCREEN_W) x1 = SCREEN_W;
  if (y1 > SCREEN_H) y1 = SCREEN_H;
  if (x0 >= x1 || y0 >= y1) return NULL;
  Cmd *c = &cmds[ncmds++];
  c->type = (uint8_t)type, c->alpha = (uint8_t)(alpha > 255 ? 255 : alpha);
  c->x0 = (int16_t)x0, c->y0 = (int16_t)y0, c->x1 = (int16_t)x1, c->y1 = (int16_t)y1;
  return c;
}

/* ------------------------------------------------------------------ pixels */
static inline uint16_t pack(uint32_t r, uint32_t g, uint32_t b) {
  return (uint16_t)((r >> 3) << 11 | (g >> 2) << 5 | b >> 3);
}
/* premultiplied (r, g, b) with coverage a (0-255) over the pixel */
static inline void over(uint16_t *p, uint32_t r, uint32_t g, uint32_t b, uint32_t a) {
  if (a >= 252) {
    *p = pack(r, g, b);
    return;
  }
  uint32_t d = *p, k = 256 - a;
  uint32_t dr = (d >> 11) << 3 | d >> 13, dg = ((d >> 5) & 63) << 2 | ((d >> 9) & 3), db = (d & 31) << 3 | ((d >> 2) & 7);
  r += dr * k >> 8, g += dg * k >> 8, b += db * k >> 8;
  *p = pack(r > 255 ? 255 : r, g > 255 ? 255 : g, b > 255 ? 255 : b);
}
/* colour c at coverage a (0-255) over the pixel */
static inline void blend(uint16_t *p, uint16_t c, uint32_t a) {
  if (a >= 252) {
    *p = c;
    return;
  }
  uint32_t x = (c | (uint32_t)c << 16) & 0x07E0F81Fu, y = (*p | (uint32_t)*p << 16) & 0x07E0F81Fu;
  y = (y + ((x - y) * (a >> 3) >> 5)) & 0x07E0F81Fu;
  *p = (uint16_t)(y | y >> 16);
}

/* ------------------------------------------------------------------ commands */
void gfx_fill(int x, int y, int w, int h, uint16_t c, int alpha) {
  Cmd *k = add(C_FILL, x, y, x + w, y + h, alpha);
  if (k) k->fill.c = c;
}

void gfx_rrect(int x, int y, int w, int h, int r, uint16_t c, int alpha) {
  Cmd *k = add(C_RRECT, x, y, x + w, y + h, alpha);
  if (!k) return;
  /* (the corners are measured from the rectangle itself, not its clipped part) */
  k->rr.x = (float)x, k->rr.y = (float)y, k->rr.w = (float)w, k->rr.h = (float)h, k->rr.c = c, k->rr.r = (int16_t)r;
}

void gfx_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
  Cmd *k = add(C_VGRAD, x, y, x + w, y + h, 255);
  if (!k) return;
  k->grad.top = top, k->grad.bottom = bottom, k->grad.y = (int16_t)y, k->grad.h = (int16_t)h;
}

void gfx_rows(const uint16_t *rows, int y0, int y1) {
  Cmd *k = add(C_ROWS, 0, y0, SCREEN_W, y1, 255);
  if (k) k->rows.rows = rows - y0;
}

static void corners(float x, float y, float scale, float ca, float sa, float u, float v, float *sx, float *sy) {
  *sx = x + scale * (ca * u + sa * v);
  *sy = y + scale * (-sa * u + ca * v);
}

void gfx_image_point(int id, float x, float y, float scale, float angle, float u, float v, float *sx, float *sy) {
  const Img *im = img(id);
  corners(x, y, scale, cosf(angle), sinf(angle), u - im->px4 / 4.0f, v - im->py4 / 4.0f, sx, sy);
}

/* a picture at its own size, its top-left on a pixel: copied, not filtered */
static void blit(const Img *im, int x, int y, int alpha) {
  Cmd *k = add(C_BLIT, x, y, x + im->w, y + im->h, alpha);
  if (k) k->blit.im = im, k->blit.x = (int16_t)x, k->blit.y = (int16_t)y;
}

void gfx_image(int id, float x, float y, float scale, float angle, int alpha) {
  const Img *im = img(id);
  if (scale <= 0.01f) return;
  /* (a wheel's angle grows with the distance, and sinf and cosf get slow past 200 or so) */
  if (angle > 6.2831853f || angle < -6.2831853f) angle = fmodf(angle, 6.2831853f);
  if (angle == 0 && scale > 0.999f && scale < 1.001f) {
    blit(im, (int)floorf(x - im->px4 / 4.0f + 0.5f), (int)floorf(y - im->py4 / 4.0f + 0.5f), alpha);
    return;
  }
  float ca = cosf(angle), sa = sinf(angle), px = im->px4 / 4.0f, py = im->py4 / 4.0f;
  float xs[4], ys[4];
  corners(x, y, scale, ca, sa, -px, -py, &xs[0], &ys[0]);
  corners(x, y, scale, ca, sa, im->w - px, -py, &xs[1], &ys[1]);
  corners(x, y, scale, ca, sa, -px, im->h - py, &xs[2], &ys[2]);
  corners(x, y, scale, ca, sa, im->w - px, im->h - py, &xs[3], &ys[3]);
  float mx0 = xs[0], mx1 = xs[0], my0 = ys[0], my1 = ys[0];
  for (int i = 1; i < 4; i++) {
    if (xs[i] < mx0) mx0 = xs[i];
    if (xs[i] > mx1) mx1 = xs[i];
    if (ys[i] < my0) my0 = ys[i];
    if (ys[i] > my1) my1 = ys[i];
  }
  if (mx1 < 0 || my1 < 0 || mx0 > SCREEN_W || my0 > SCREEN_H) return;
  Cmd *k = add(C_IMAGE, (int)floorf(mx0) - 1, (int)floorf(my0) - 1, (int)ceilf(mx1) + 1, (int)ceilf(my1) + 1, alpha);
  if (!k) return;
  float is = 1.0f / scale;
  float ua = ca * is, ub = -sa * is, va = sa * is, vb = ca * is;
  /* the texel coordinates of pixel (0, 0)'s centre, half a texel back for the filter */
  float u0 = px - 0.5f + ua * (0.5f - x) + ub * (0.5f - y);
  float v0 = py - 0.5f + va * (0.5f - x) + vb * (0.5f - y);
  k->image.im = im;
  k->image.ua = (int32_t)(ua * 65536), k->image.ub = (int32_t)(ub * 65536);
  k->image.va = (int32_t)(va * 65536), k->image.vb = (int32_t)(vb * 65536);
  k->image.u0 = (int32_t)(u0 * 65536), k->image.v0 = (int32_t)(v0 * 65536);
}

void gfx_image_box(int id, int x, int y, int w, int h, int alpha) {
  const Img *im = img(id);
  if (w <= 0 || h <= 0) return;
  if (w == im->w && h == im->h) {
    blit(im, x, y, alpha);
    return;
  }
  Cmd *k = add(C_IMAGE, x, y, x + w, y + h, alpha);
  if (!k) return;
  float sx = (float)im->w / w, sy = (float)im->h / h;
  k->image.im = im;
  k->image.ua = (int32_t)(sx * 65536), k->image.ub = 0, k->image.va = 0, k->image.vb = (int32_t)(sy * 65536);
  k->image.u0 = (int32_t)((sx * (0.5f - x) - 0.5f) * 65536);
  k->image.v0 = (int32_t)((sy * (0.5f - y) - 0.5f) * 65536);
}

int gfx_text_width(int fid, const char *s) {
  const Font *f = font(fid);
  const Glyph *g = font_glyphs(f);
  int w = 0;
  for (; *s; s++) {
    int c = (uint8_t)*s - f->first;
    if (c >= 0 && c < f->count) w += g[c].adv;
  }
  return w;
}

int gfx_text(int fid, int x, int y, const char *s, uint16_t color, uint16_t outline, int align) {
  const Font *f = font(fid);
  int n = (int)strlen(s), w = gfx_text_width(fid, s);
  if (align == 1) x -= w / 2;
  else if (align == 2) x -= w;
  if (npool + n + 1 > GFX_TEXT_POOL) return w;
  Cmd *k = add(C_TEXT, x - 4, y - f->ascent - 4, x + w + 6, y + f->height - f->ascent + 4, 255);
  if (!k) return w;
  memcpy(pool + npool, s, (size_t)n + 1);
  k->text.s = pool + npool;
  npool += n + 1;
  k->text.font = (uint8_t)fid, k->text.c = color, k->text.oc = outline, k->text.x = (int16_t)x, k->text.y = (int16_t)y;
  return w;
}

void gfx_circle(float x, float y, float r, uint16_t c, int alpha) {
  Cmd *k = add(C_CIRCLE, (int)(x - r) - 1, (int)(y - r) - 1, (int)(x + r) + 2, (int)(y + r) + 2, alpha);
  if (k) k->circle.x = x, k->circle.y = y, k->circle.r = r, k->circle.c = c;
}

void gfx_ring(float x, float y, float r, float t, uint16_t c, int alpha) {
  Cmd *k = add(C_RING, (int)(x - r) - 1, (int)(y - r) - 1, (int)(x + r) + 2, (int)(y + r) + 2, alpha);
  if (k) k->circle.x = x, k->circle.y = y, k->circle.r = r, k->circle.t = t, k->circle.c = c;
}

void gfx_tube(float x0, float y0, float x1, float y1, float r, uint16_t c, int alpha) {
  float lx = x0 < x1 ? x0 : x1, hx = x0 < x1 ? x1 : x0, ly = y0 < y1 ? y0 : y1, hy = y0 < y1 ? y1 : y0;
  Cmd *k = add(C_TUBE, (int)(lx - r) - 1, (int)(ly - r) - 1, (int)(hx + r) + 2, (int)(hy + r) + 2, alpha);
  if (k) k->tube.ax = x0, k->tube.ay = y0, k->tube.bx = x1, k->tube.by = y1, k->tube.r = r, k->tube.c = c;
}

void gfx_ground_draw(void) { add(C_GROUND, 0, 0, SCREEN_W, SCREEN_H, 255); }
void gfx_liquid_draw(void) { add(C_LIQUID, 0, 0, SCREEN_W, SCREEN_H, 255); }

/* ------------------------------------------------------------------ painting a band */
__attribute__((noinline)) static void paint_fill(const Cmd *k, int y0, int y1) {
  for (int y = y0; y < y1; y++) {
    uint16_t *p = band + (y - by0) * SCREEN_W;
    if (k->alpha >= 252)
      for (int x = k->x0; x < k->x1; x++) p[x] = k->fill.c;
    else
      for (int x = k->x0; x < k->x1; x++) blend(p + x, k->fill.c, k->alpha);
  }
}

__attribute__((noinline)) static void paint_rrect(const Cmd *k, int y0, int y1) {
  float rx = k->rr.x, ry = k->rr.y, rw = k->rr.w, rh = k->rr.h, r = k->rr.r;
  for (int y = y0; y < y1; y++) {
    uint16_t *p = band + (y - by0) * SCREEN_W;
    float cy = y + 0.5f, dy = 0;
    if (cy < ry + r) dy = ry + r - cy;
    else if (cy > ry + rh - r) dy = cy - (ry + rh - r);
    for (int x = k->x0; x < k->x1; x++) {
      float cx = x + 0.5f, dx = 0;
      if (cx < rx + r) dx = rx + r - cx;
      else if (cx > rx + rw - r) dx = cx - (rx + rw - r);
      int a = k->alpha;
      if (dx > 0 && dy > 0) {
        float d = sqrtf(dx * dx + dy * dy), cov = r + 0.5f - d;
        if (cov <= 0) continue;
        if (cov < 1) a = (int)(a * cov);
      }
      blend(p + x, k->rr.c, (uint32_t)a);
    }
  }
}

static uint32_t lerp888(uint32_t a, uint32_t b, int t, int n) {
  uint32_t r = ((a >> 16 & 255) * (n - t) + (b >> 16 & 255) * t) / n;
  uint32_t g = ((a >> 8 & 255) * (n - t) + (b >> 8 & 255) * t) / n;
  uint32_t bl = ((a & 255) * (n - t) + (b & 255) * t) / n;
  return r << 16 | g << 8 | bl;
}

__attribute__((noinline)) static void paint_vgrad(const Cmd *k, int y0, int y1) {
  int top = k->grad.y, h = k->grad.h;
  for (int y = y0; y < y1; y++) {
    uint16_t c = rgb565(lerp888(k->grad.top, k->grad.bottom, y - top, h > 1 ? h - 1 : 1));
    uint16_t *p = band + (y - by0) * SCREEN_W;
    for (int x = k->x0; x < k->x1; x++) p[x] = c;
  }
}

__attribute__((noinline)) static void paint_rows(const Cmd *k, int y0, int y1) {
  for (int y = y0; y < y1; y++) {
    uint16_t c = k->rows.rows[y], *p = band + (y - by0) * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) p[x] = c;
  }
}

/* the pixels [*x0, *x1) of a row where t = t0 + dt x stays in [lo, hi] */
static void span(int32_t t0, int32_t dt, int32_t lo, int32_t hi, int *x0, int *x1) {
  if (dt == 0) {
    if (t0 < lo || t0 > hi) *x1 = *x0;
    return;
  }
  /* x where t crosses lo and hi */
  float a = (float)(lo - t0) / dt, b = (float)(hi - t0) / dt;
  if (a > b) {
    float s = a;
    a = b, b = s;
  }
  int lo_x = (int)ceilf(a), hi_x = (int)floorf(b) + 1;
  if (lo_x > *x0) *x0 = lo_x;
  if (hi_x < *x1) *x1 = hi_x;
}

__attribute__((noinline)) static void paint_image(const Cmd *k, int y0, int y1) {
  const Img *im = k->image.im;
  const uint32_t *pal = img_pal(im);
  const uint8_t *px = img_px(im);
  int w = im->w, h = im->h;
  int32_t umax = (w - 1) << 16, vmax = (h - 1) << 16;
  uint32_t ga = k->alpha;
  for (int y = y0; y < y1; y++) {
    int32_t ur = k->image.u0 + k->image.ub * y, vr = k->image.v0 + k->image.vb * y;
    int x0 = k->x0, x1 = k->x1;
    span(ur, k->image.ua, 0, umax, &x0, &x1);
    span(vr, k->image.va, 0, vmax, &x0, &x1);
    if (x0 >= x1) continue;
    int32_t u = ur + k->image.ua * x0, v = vr + k->image.va * x0;
    uint16_t *p = band + (y - by0) * SCREEN_W + x0;
    for (int x = x0; x < x1; x++, p++, u += k->image.ua, v += k->image.va) {
      int iu = u >> 16, iv = v >> 16;
      if ((unsigned)iu > (unsigned)(w - 1) || (unsigned)iv > (unsigned)(h - 1)) continue;   /* (rounding at the ends) */
      int iu1 = iu < w - 1 ? iu + 1 : iu, iv1 = iv < h - 1 ? w : 0;
      const uint8_t *r0 = px + iv * w;
      uint32_t c00 = pal[r0[iu]], c01 = pal[r0[iu1]], c10 = pal[r0[iu + iv1]], c11 = pal[r0[iu1 + iv1]];
      if (!((c00 | c01 | c10 | c11) >> 24)) continue;   /* (all four see-through) */
      uint32_t fu = (u >> 12) & 15, fv = (v >> 12) & 15;
      uint32_t w11 = fu * fv, w01 = fu * (16 - fv), w10 = (16 - fu) * fv, w00 = 256 - w11 - w01 - w10;
      uint32_t rb = ((c00 & 0xFF00FF) * w00 + (c01 & 0xFF00FF) * w01 + (c10 & 0xFF00FF) * w10 + (c11 & 0xFF00FF) * w11) >> 8;
      uint32_t ag = (((c00 >> 8) & 0xFF00FF) * w00 + ((c01 >> 8) & 0xFF00FF) * w01 + ((c10 >> 8) & 0xFF00FF) * w10 +
                     ((c11 >> 8) & 0xFF00FF) * w11) >> 8;
      if (ga < 255) rb = (rb & 0xFF00FF) * ga >> 8, ag = (ag & 0xFF00FF) * ga >> 8;
      uint32_t a = (ag >> 16) & 255;
      if (a < 3) continue;
      over(p, rb & 255, ag & 255, (rb >> 16) & 255, a);
    }
  }
}

/* HC_LOSSLESS_CROP_V1: separate pixel path for cropped images. */
__attribute__((noinline)) static void paint_image_cropped(const Cmd *k, int y0, int y1) {
  const Img *im = k->image.im;
  const uint32_t *pal = img_pal(im);
  const uint8_t *px = img_px(im);
  int w = im->w, h = im->h;
  int32_t umax = (w - 1) << 16, vmax = (h - 1) << 16;
  uint32_t ga = k->alpha;
  for (int y = y0; y < y1; y++) {
    int32_t ur = k->image.u0 + k->image.ub * y, vr = k->image.v0 + k->image.vb * y;
    int x0 = k->x0, x1 = k->x1;
    span(ur, k->image.ua, 0, umax, &x0, &x1);
    span(vr, k->image.va, 0, vmax, &x0, &x1);
    if (x0 >= x1) continue;
    int32_t u = ur + k->image.ua * x0, v = vr + k->image.va * x0;
    uint16_t *p = band + (y - by0) * SCREEN_W + x0;
    for (int x = x0; x < x1; x++, p++, u += k->image.ua, v += k->image.va) {
      int iu = u >> 16, iv = v >> 16;
      if ((unsigned)iu > (unsigned)(w - 1) || (unsigned)iv > (unsigned)(h - 1)) continue;   /* (rounding at the ends) */
      int iu1 = iu < w - 1 ? iu + 1 : iu, iv1 = iv < h - 1 ? w : 0;
      uint32_t c00 = pal[img_crop_index(im, px, iu, iv)];
      uint32_t c01 = pal[img_crop_index(im, px, iu1, iv)];
      uint32_t c10 = pal[img_crop_index(im, px, iu, iv + (iv1 ? 1 : 0))];
      uint32_t c11 = pal[img_crop_index(im, px, iu1, iv + (iv1 ? 1 : 0))];
      if (!((c00 | c01 | c10 | c11) >> 24)) continue;   /* (all four see-through) */
      uint32_t fu = (u >> 12) & 15, fv = (v >> 12) & 15;
      uint32_t w11 = fu * fv, w01 = fu * (16 - fv), w10 = (16 - fu) * fv, w00 = 256 - w11 - w01 - w10;
      uint32_t rb = ((c00 & 0xFF00FF) * w00 + (c01 & 0xFF00FF) * w01 + (c10 & 0xFF00FF) * w10 + (c11 & 0xFF00FF) * w11) >> 8;
      uint32_t ag = (((c00 >> 8) & 0xFF00FF) * w00 + ((c01 >> 8) & 0xFF00FF) * w01 + ((c10 >> 8) & 0xFF00FF) * w10 +
                     ((c11 >> 8) & 0xFF00FF) * w11) >> 8;
      if (ga < 255) rb = (rb & 0xFF00FF) * ga >> 8, ag = (ag & 0xFF00FF) * ga >> 8;
      uint32_t a = (ag >> 16) & 255;
      if (a < 3) continue;
      over(p, rb & 255, ag & 255, (rb >> 16) & 255, a);
    }
  }
}

__attribute__((noinline)) static void paint_blit_cropped(const Cmd *k, int y0, int y1) {
  const Img *im = k->blit.im;
  const uint32_t *pal = img_pal(im);
  const uint8_t *px = img_px(im);
  uint32_t ga = k->alpha;
  int x0 = k->x0, x1 = k->x1;
  /* HC_LOSSLESS_CROP_V1: transparent margins need no blit work. */
  const uint8_t *crop = img_crop(im);
  if (x0 < k->blit.x + crop[0]) x0 = k->blit.x + crop[0];
  if (x1 > k->blit.x + crop[0] + crop[2]) x1 = k->blit.x + crop[0] + crop[2];
  if (y0 < k->blit.y + crop[1]) y0 = k->blit.y + crop[1];
  if (y1 > k->blit.y + crop[1] + crop[3]) y1 = k->blit.y + crop[1] + crop[3];
  if (x0 >= x1 || y0 >= y1) return;
  bool solid = im->flags & IMG_OPAQUE && ga == 255;
  const uint16_t *pal565 = solid ? img_pal565(im) : NULL;
  for (int y = y0; y < y1; y++) {
    const uint8_t *src = px + (y - k->blit.y - crop[1]) * crop[2];
    uint16_t *p = band + (y - by0) * SCREEN_W;
    if (solid) {
      for (int x = x0; x < x1; x++) p[x] = pal565[src[x - k->blit.x - crop[0]]];
      continue;
    }
    for (int x = x0; x < x1; x++) {
      uint32_t c = pal[src[x - k->blit.x - crop[0]]], a = c >> 24;
      if (!a) continue;
      if (ga < 255) {
        uint32_t rb = (c & 0xFF00FF) * ga >> 8, g = ((c >> 8) & 0xFF00FF) * ga >> 8;
        c = (rb & 0xFF00FF) | (g & 0xFF00FF) << 8, a = c >> 24;
      }
      over(p + x, c & 255, (c >> 8) & 255, (c >> 16) & 255, a);
    }
  }
}

__attribute__((noinline)) static void paint_blit(const Cmd *k, int y0, int y1) {
  const Img *im = k->blit.im;
  const uint32_t *pal = img_pal(im);
  const uint8_t *px = img_px(im);
  uint32_t ga = k->alpha;
  int x0 = k->x0, x1 = k->x1;
  bool solid = im->flags & IMG_OPAQUE && ga == 255;
  const uint16_t *pal565 = solid ? img_pal565(im) : NULL;
  for (int y = y0; y < y1; y++) {
    const uint8_t *src = px + (y - k->blit.y) * im->w - k->blit.x;
    uint16_t *p = band + (y - by0) * SCREEN_W;
    if (solid) {
      for (int x = x0; x < x1; x++) p[x] = pal565[src[x]];
      continue;
    }
    for (int x = x0; x < x1; x++) {
      uint32_t c = pal[src[x]], a = c >> 24;
      if (!a) continue;
      if (ga < 255) {
        uint32_t rb = (c & 0xFF00FF) * ga >> 8, g = ((c >> 8) & 0xFF00FF) * ga >> 8;
        c = (rb & 0xFF00FF) | (g & 0xFF00FF) << 8, a = c >> 24;
      }
      over(p + x, c & 255, (c >> 8) & 255, (c >> 16) & 255, a);
    }
  }
}

/* HC_FONT_ROW_RLE_V1: decode only a requested glyph row; no frame cache or heap. */
__attribute__((noinline)) static void paint_font_rle_row(const Glyph *g, const uint8_t *bitmap,
                                                      unsigned ry, int gx, uint16_t *p,
                                                      uint16_t oc, uint16_t color) {
  const uint8_t *table = bitmap + g->off;
  unsigned start = (unsigned)table[2 * ry] | (unsigned)table[2 * ry + 1] << 8;
  unsigned stop = (unsigned)table[2 * ry + 2] | (unsigned)table[2 * ry + 3] << 8;
  const uint8_t *q = table + 2 * ((unsigned)g->h + 1) + start;
  const uint8_t *end = table + 2 * ((unsigned)g->h + 1) + stop;
  unsigned x = 0;
  while (q < end && x < g->w) {
    unsigned cmd = *q++;
    unsigned n = (cmd & 127u) + 1u;
    if (x + n > g->w) return; /* unexpected/corrupt glyph */
    if (cmd & 128u) {
      if (q >= end) return;
      uint8_t b = *q++;
      if (b) {
        for (unsigned j = 0; j < n; j++) {
          int sx = gx + (int)x + (int)j;
          if ((unsigned)sx >= SCREEN_W) continue;
          if (b & 15) blend(p + sx, oc, (b & 15) * 17u);
          if (b >> 4) blend(p + sx, color, (b >> 4) * 17u);
        }
      }
    } else {
      if ((unsigned)(end - q) < n) return;
      for (unsigned j = 0; j < n; j++) {
        uint8_t b = q[j];
        if (!b) continue;
        int sx = gx + (int)x + (int)j;
        if ((unsigned)sx >= SCREEN_W) continue;
        if (b & 15) blend(p + sx, oc, (b & 15) * 17u);
        if (b >> 4) blend(p + sx, color, (b >> 4) * 17u);
      }
      q += n;
    }
    x += n;
  }
}

/* HC_MENU_TILES_EXPERIMENT_V1: bilinear sampling uses the same math and palettes. */
__attribute__((noinline)) static void paint_image_tiled(const Cmd *k, int y0, int y1) {
  const Img *im = k->image.im;
  const uint32_t *pal = img_pal(im);
  const uint8_t *px = img_px(im);
  int w = im->w, h = im->h;
  int32_t umax = (w - 1) << 16, vmax = (h - 1) << 16;
  uint32_t ga = k->alpha;
  for (int y = y0; y < y1; y++) {
    int32_t ur = k->image.u0 + k->image.ub * y, vr = k->image.v0 + k->image.vb * y;
    int x0 = k->x0, x1 = k->x1;
    span(ur, k->image.ua, 0, umax, &x0, &x1);
    span(vr, k->image.va, 0, vmax, &x0, &x1);
    if (x0 >= x1) continue;
    int32_t u = ur + k->image.ua * x0, v = vr + k->image.va * x0;
    uint16_t *p = band + (y - by0) * SCREEN_W + x0;
    for (int x = x0; x < x1; x++, p++, u += k->image.ua, v += k->image.va) {
      int iu = u >> 16, iv = v >> 16;
      if ((unsigned)iu > (unsigned)(w - 1) || (unsigned)iv > (unsigned)(h - 1)) continue;   /* (rounding at the ends) */
      int iu1 = iu < w - 1 ? iu + 1 : iu, iv1 = iv < h - 1 ? w : 0;
      uint32_t c00 = pal[img_tile_index(im, px, iu, iv)];
      uint32_t c01 = pal[img_tile_index(im, px, iu1, iv)];
      uint32_t c10 = pal[img_tile_index(im, px, iu, iv + (iv1 ? 1 : 0))];
      uint32_t c11 = pal[img_tile_index(im, px, iu1, iv + (iv1 ? 1 : 0))];
      if (!((c00 | c01 | c10 | c11) >> 24)) continue;   /* (all four see-through) */
      uint32_t fu = (u >> 12) & 15, fv = (v >> 12) & 15;
      uint32_t w11 = fu * fv, w01 = fu * (16 - fv), w10 = (16 - fu) * fv, w00 = 256 - w11 - w01 - w10;
      uint32_t rb = ((c00 & 0xFF00FF) * w00 + (c01 & 0xFF00FF) * w01 + (c10 & 0xFF00FF) * w10 + (c11 & 0xFF00FF) * w11) >> 8;
      uint32_t ag = (((c00 >> 8) & 0xFF00FF) * w00 + ((c01 >> 8) & 0xFF00FF) * w01 + ((c10 >> 8) & 0xFF00FF) * w10 +
                     ((c11 >> 8) & 0xFF00FF) * w11) >> 8;
      if (ga < 255) rb = (rb & 0xFF00FF) * ga >> 8, ag = (ag & 0xFF00FF) * ga >> 8;
      uint32_t a = (ag >> 16) & 255;
      if (a < 3) continue;
      over(p, rb & 255, ag & 255, (rb >> 16) & 255, a);
    }
  }
}

/* HC_MENU_TILES_EXPERIMENT_V1: menu-only tile blitting; original hot blit unchanged. */
__attribute__((noinline)) static void paint_blit_tiled(const Cmd *k, int y0, int y1) {
  const Img *im = k->blit.im;
  const uint32_t *pal = img_pal(im);
  const uint8_t *px = img_px(im);
  int x0 = k->x0, x1 = k->x1;
  uint32_t ga = k->alpha;
  if (im->flags & IMG_CROPPED) {
    const uint8_t *c = img_crop(im);
    if (x0 < k->blit.x + c[0]) x0 = k->blit.x + c[0];
    if (x1 > k->blit.x + c[0] + c[2]) x1 = k->blit.x + c[0] + c[2];
    if (y0 < k->blit.y + c[1]) y0 = k->blit.y + c[1];
    if (y1 > k->blit.y + c[1] + c[3]) y1 = k->blit.y + c[1] + c[3];
  }
  if (x0 >= x1 || y0 >= y1) return;
  bool solid = (im->flags & IMG_OPAQUE) && ga == 255;
  const uint16_t *pal565 = solid ? img_pal565(im) : NULL;
  for (int y = y0; y < y1; y++) {
    uint16_t *p = band + (y - by0) * SCREEN_W;
    for (int x = x0; x < x1; x++) {
      uint8_t index = img_tile_index(im, px, x - k->blit.x, y - k->blit.y);
      if (solid) { p[x] = pal565[index]; continue; }
      uint32_t c = pal[index], a = c >> 24;
      if (!a) continue;
      if (ga < 255) {
        uint32_t rb = (c & 0xFF00FF) * ga >> 8, g = ((c >> 8) & 0xFF00FF) * ga >> 8;
        c = (rb & 0xFF00FF) | (g & 0xFF00FF) << 8, a = c >> 24;
      }
      over(p + x, c & 255, (c >> 8) & 255, (c >> 16) & 255, a);
    }
  }
}

__attribute__((noinline)) static void paint_text(const Cmd *k, int y0, int y1) {
  const Font *f = font(k->text.font);
  const Glyph *gl = font_glyphs(f);
  const uint8_t *bits = font_bits(f);
  int pen = k->text.x;
  for (const char *s = k->text.s; *s; s++) {
    int c = (uint8_t)*s - f->first;
    if (c < 0 || c >= f->count) continue;
    const Glyph *g = &gl[c];
    int gx = pen + g->x, gy = k->text.y + g->y;
    pen += g->adv;
    int ya = gy > y0 ? gy : y0, yb = gy + g->h < y1 ? gy + g->h : y1;
    for (int y = ya; y < yb; y++) {
      /* HC_FONT_ROW_RLE_V1: preserve the original hot path for raw glyphs. */
      if (g->pad[0] == 1) {
        paint_font_rle_row(g, bits, (unsigned)(y - gy), gx,
                           band + (y - by0) * SCREEN_W, k->text.oc, k->text.c);
        continue;
      }
      const uint8_t *row = bits + g->off + (y - gy) * g->w;
      uint16_t *p = band + (y - by0) * SCREEN_W;
      for (int i = 0; i < g->w; i++) {
        int x = gx + i;
        if ((unsigned)x >= SCREEN_W) continue;
        uint8_t b = row[i];
        if (!b) continue;
        if (b & 15) blend(p + x, k->text.oc, (b & 15) * 17u);
        if (b >> 4) blend(p + x, k->text.c, (b >> 4) * 17u);
      }
    }
  }
}

__attribute__((noinline)) static void paint_circle(const Cmd *k, int y0, int y1, bool ring) {
  float cx = k->circle.x, cy = k->circle.y, r = k->circle.r, t = k->circle.t;
  for (int y = y0; y < y1; y++) {
    uint16_t *p = band + (y - by0) * SCREEN_W;
    float dy = y + 0.5f - cy;
    for (int x = k->x0; x < k->x1; x++) {
      float dx = x + 0.5f - cx, d = sqrtf(dx * dx + dy * dy), cov = r + 0.5f - d;
      if (cov <= 0) continue;
      if (ring) {
        float in = d - (r - t) + 0.5f;
        if (in <= 0) continue;
        if (in < cov) cov = in;
      }
      blend(p + x, k->circle.c, (uint32_t)(k->alpha * (cov > 1 ? 1 : cov)));
    }
  }
}

__attribute__((noinline)) static void paint_tube(const Cmd *k, int y0, int y1) {
  float ax = k->tube.ax, ay = k->tube.ay, dx = k->tube.bx - ax, dy = k->tube.by - ay, r = k->tube.r;
  float l = dx * dx + dy * dy, inv = l > 0 ? 1 / l : 0;
  for (int y = y0; y < y1; y++) {
    uint16_t *p = band + (y - by0) * SCREEN_W;
    float qy = y + 0.5f - ay;
    for (int x = k->x0; x < k->x1; x++) {
      float qx = x + 0.5f - ax, t = (qx * dx + qy * dy) * inv;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      float ex = qx - t * dx, ey = qy - t * dy, d = ex * ex + ey * ey, e = r + 0.5f;
      if (d >= e * e) continue;
      float cov = e - sqrtf(d);
      blend(p + x, k->tube.c, (uint32_t)(k->alpha * (cov > 1 ? 1 : cov)));
    }
  }
}

/* The ground: rock or soil under the surface line (and over the ceiling's), then the surface strip along it. */
__attribute__((noinline)) static void paint_ground(int y0, int y1) {
  const Ground *g = &gfx_ground;
  const Img *gi = img(g->ground), *ci = img(g->cground);
  const uint16_t *gpal = img_pal565(gi), *cpal = img_pal565(ci);
  const uint8_t *gpx = img_px(gi), *cpx = img_px(ci);
  /* each row's line of the ground's picture, then column by column only the rows under the line (over the
   * ceiling's) */
  const uint8_t *grow[GFX_BAND], *crow[GFX_BAND];
  for (int y = y0; y < y1; y++) {
    int32_t v = g->gv0 + g->gdv * y;
    grow[y - y0] = gpx + ((v >> 16) & 127) * 128, crow[y - y0] = cpx + ((v >> 16) & 127) * 128;
  }
  for (int x = 0; x < SCREEN_W; x++) {
    int u = g->gu[x];
    int ys = (g->floor[x] + 3) >> 3;   /* the first row whose centre is under the line */
    if (ys < y0) ys = y0;
    uint16_t *p = band + (ys - by0) * SCREEN_W + x;
    for (int y = ys; y < y1; y++, p += SCREEN_W) *p = gpal[grow[y - y0][u]];
    if (g->ceiling) {
      int ye = ((g->ceil[x] - 4) >> 3) + 1;   /* past the last row whose centre is over the ceiling's line */
      if (ye > y1) ye = y1;
      p = band + (y0 - by0) * SCREEN_W + x;
      for (int y = y0; y < ye; y++, p += SCREEN_W) *p = cpal[crow[y - y0][u]];
    }
  }
  /* the strips: 16 rows of picture over above + below rows of the screen */
  const Img *si = img(g->surf), *cs = img(g->csurf);
  const uint32_t *spal = img_pal(si), *cspal = img_pal(cs);
  const uint8_t *spx = img_px(si), *cspx = img_px(cs);
  int span8 = (g->above + g->below) * 8, sh = si->h;
  if (span8 <= 0) return;
  for (int x = 0; x < SCREEN_W; x++) {
    for (int pass = 0; pass < 1 + g->ceiling; pass++) {
      int line = pass ? g->ceil[x] : g->floor[x];
      int top8 = pass ? line - g->below * 8 : line - g->above * 8;   /* (the ceiling's strip hangs flipped) */
      int ya = (top8 >> 3) - 1, yb = ((top8 + span8) >> 3) + 1;
      if (ya < y0) ya = y0;
      if (yb > y1) yb = y1;
      for (int y = ya; y < yb; y++) {
        int t = (y * 8 + 4 - top8) * sh * 16 / span8;   /* 1/16 of a strip row */
        if (t < 0 || t >= sh << 4) continue;
        int row = t >> 4;
        if (pass) row = sh - 1 - row;
        uint32_t c = pass ? cspal[cspx[row * cs->w + g->su[x]]] : spal[spx[row * si->w + g->su[x]]];
        uint32_t a = c >> 24;
        if (a < 3) continue;
        over(band + (y - by0) * SCREEN_W + x, c & 255, (c >> 8) & 255, (c >> 16) & 255, a);
      }
    }
  }
}

/* A pit's mud or lava: from its surface's line down to the ground's, the pit's picture's rows by the depth */
__attribute__((noinline)) static void paint_liquid(int y0, int y1) {
  const Ground *g = &gfx_ground;
  const Img *li = img(g->lpic);
  const uint16_t *pal = img_pal565(li);
  const uint8_t *px = img_px(li);
  int h = li->h, w = li->w;
  for (int x = 0; x < SCREEN_W; x++) {
    int top8 = g->liquid[x];
    if (top8 >= g->floor[x]) continue;
    int ya = (top8 + 3) >> 3, yb = (g->floor[x] + 3) >> 3;
    if (ya < y0) ya = y0;
    if (yb > y1) yb = y1;
    int u = (g->gu[x] * w) >> 7;
    for (int y = ya; y < yb; y++) {
      int32_t v = (int32_t)((int64_t)(y * 8 + 4 - top8) * g->ldv >> 3) >> 16;
      if (v >= h) v = h - 1;
      if (v < 0) v = 0;
      band[(y - by0) * SCREEN_W + x] = pal[px[v * w + u]];
    }
  }
}

void gfx_end(void) {
  for (by0 = 0; by0 < SCREEN_H; by0 = by1) {
    by1 = by0 + GFX_BAND < SCREEN_H ? by0 + GFX_BAND : SCREEN_H;
    for (int i = 0; i < ncmds; i++) {
      const Cmd *k = &cmds[i];
      int y0 = k->y0 > by0 ? k->y0 : by0, y1 = k->y1 < by1 ? k->y1 : by1;
      if (y0 >= y1) continue;
      switch (k->type) {
        case C_FILL: paint_fill(k, y0, y1); break;
        case C_RRECT: paint_rrect(k, y0, y1); break;
        case C_VGRAD: paint_vgrad(k, y0, y1); break;
        case C_ROWS: paint_rows(k, y0, y1); break;
        case C_IMAGE: if (k->image.im->flags & IMG_TILED) paint_image_tiled(k, y0, y1);
                      else if (k->image.im->flags & IMG_CROPPED) paint_image_cropped(k, y0, y1);
                      else paint_image(k, y0, y1); break;
        case C_BLIT: if (k->blit.im->flags & IMG_TILED) paint_blit_tiled(k, y0, y1);
                     else if (k->blit.im->flags & IMG_CROPPED) paint_blit_cropped(k, y0, y1);
                     else paint_blit(k, y0, y1); break;
        case C_TEXT: paint_text(k, y0, y1); break;
        case C_CIRCLE: paint_circle(k, y0, y1, false); break;
        case C_RING: paint_circle(k, y0, y1, true); break;
        case C_TUBE: paint_tube(k, y0, y1); break;
        case C_GROUND: paint_ground(y0, y1); break;
        case C_LIQUID: paint_liquid(y0, y1); break;
      }
    }
    plat_push(0, by0, SCREEN_W, by1 - by0, band);
  }
}
