/* Moto X3M: drawing (see gfx.h). */
#include <math.h>
#include <string.h>
#include "gfx.h"

uint16_t *gfx_band;
int gfx_y0;

void gfx_init(uint16_t *band) { gfx_band = band; }

void gfx_frame(void (*draw)(void)) {
  for (int y = 0; y < SCREEN_H; y += BAND) {
    gfx_y0 = y;
    draw();
    plat_push(0, y, SCREEN_W, BAND, gfx_band);
  }
}

static inline uint16_t blend(uint16_t d, uint16_t s, int a) {
  /* a: 0-256 */
  uint32_t dd = (d | ((uint32_t)d << 16)) & 0x07E0F81F, ss = (s | ((uint32_t)s << 16)) & 0x07E0F81F;
  uint32_t r = (dd + (((ss - dd) * (uint32_t)(a >> 3)) >> 5)) & 0x07E0F81F;
  return (uint16_t)(r | (r >> 16));
}

/* A polygon's edges that reach the band, for its even-odd spans: edges wholly right of the screen are left out
 * (they cannot change what is on it), edges wholly left of it only count for the parity. */
#define MAX_EDGES 160
#define MAX_LEFT 96
typedef struct {
  int ne, nl;
  float e[4 * MAX_EDGES];
  float l[2 * MAX_LEFT];
} Edges;

static void band_edges(const float *xy, int n, Edges *E) {
  E->ne = E->nl = 0;
  float b0 = (float)gfx_y0, b1 = (float)(gfx_y0 + BAND);
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float yi = xy[2 * i + 1], yj = xy[2 * j + 1];
    float lo = yi < yj ? yi : yj, hi = yi < yj ? yj : yi;
    if (hi < b0 || lo > b1 || yi == yj) continue;
    float xi = xy[2 * i], xj = xy[2 * j];
    if (xi >= SCREEN_W && xj >= SCREEN_W) continue;
    if (xi < 0 && xj < 0) {
      if (E->nl < MAX_LEFT) E->l[2 * E->nl] = yj, E->l[2 * E->nl + 1] = yi, E->nl++;
      continue;
    }
    if (E->ne >= MAX_EDGES) continue;
    float *e = E->e + 4 * E->ne++;
    e[0] = xj, e[1] = yj, e[2] = xi, e[3] = yi;
  }
}

/* the row's spans (pairs of x), clamped to the screen */
static int spans(const Edges *E, float yc, int16_t *out) {
  float xs[48];
  int k = 0;
  bool inside = false;
  for (int i = 0; i < E->nl; i++)
    if ((E->l[2 * i] > yc) != (E->l[2 * i + 1] > yc)) inside = !inside;
  for (int i = 0; i < E->ne; i++) {
    const float *e = E->e + 4 * i;
    if ((e[3] > yc) != (e[1] > yc)) {
      float x = e[0] + (yc - e[1]) * (e[2] - e[0]) / (e[3] - e[1]);
      if (x < 0) inside = !inside;
      else if (k < 48) xs[k++] = x;
    }
  }
  for (int a = 1; a < k; a++) {
    float v = xs[a];
    int b = a - 1;
    while (b >= 0 && xs[b] > v) xs[b + 1] = xs[b], b--;
    xs[b + 1] = v;
  }
  int ns = 0;
  float start = 0;
  for (int i = 0; i < k; i++) {
    if (inside) {
      int xa = (int)ceilf(start - 0.5f), xb = (int)ceilf(xs[i] - 0.5f);
      if (xa < 0) xa = 0;
      if (xb > SCREEN_W) xb = SCREEN_W;
      if (xa < xb) out[2 * ns] = (int16_t)xa, out[2 * ns + 1] = (int16_t)xb, ns++;
    } else start = xs[i];
    inside = !inside;
  }
  if (inside) {
    int xa = (int)ceilf(start - 0.5f);
    if (xa < 0) xa = 0;
    if (xa < SCREEN_W) out[2 * ns] = (int16_t)xa, out[2 * ns + 1] = SCREEN_W, ns++;
  }
  return ns;
}


void gfx_poly_pic(const float *xy, int n, const Pic *p, const Map *m, bool wrap, int alpha) {
  Edges E;
  int16_t sp[48];
  band_edges(xy, n, &E);
  if (!E.ne && !E.nl) return;
  const uint16_t *pal = pic_pal(p);
  const uint8_t *al = pic_alpha(p), *px = pic_px(p);
  bool four = p->flags & IMG_4BIT, opaque = (p->flags & IMG_OPAQUE) && alpha >= 256;
  int w = p->w, h = p->h, stride = pic_stride(p);
  int32_t du = (int32_t)(m->ux * 65536), dv = (int32_t)(m->vx * 65536);
  for (int r = 0; r < BAND; r++) {
    int y = gfx_y0 + r;
    float yc = y + 0.5f;
    int k = spans(&E, yc, sp);
    uint16_t *row = gfx_band + r * SCREEN_W;
    for (int s = 0; s < k; s++) {
      int xa = sp[2 * s], xb = sp[2 * s + 1];
      float fx = xa + 0.5f;
      int32_t u = (int32_t)((m->ux * fx + m->uy * yc + m->u0) * 65536);
      int32_t v = (int32_t)((m->vx * fx + m->vy * yc + m->v0) * 65536);
      if (wrap) {
        unsigned wm = (unsigned)w - 1, hm = (unsigned)h - 1;
        if (four) {
          for (int x = xa; x < xb; x++, u += du, v += dv) {
            unsigned tx = (unsigned)(u >> 16) & wm, ty = (unsigned)(v >> 16) & hm;
            uint8_t b = px[ty * (unsigned)stride + (tx >> 1)];
            unsigned c = tx & 1 ? b & 15 : b >> 4;
            if (opaque) row[x] = pal[c];
            else if (al[c]) row[x] = al[c] == 255 && alpha >= 256 ? pal[c] : blend(row[x], pal[c], al[c] * alpha >> 8);
          }
        } else {
          for (int x = xa; x < xb; x++, u += du, v += dv) {
            unsigned tx = (unsigned)(u >> 16) & wm, ty = (unsigned)(v >> 16) & hm;
            unsigned c = px[ty * (unsigned)stride + tx];
            if (opaque) row[x] = pal[c];
            else if (al[c]) row[x] = al[c] == 255 && alpha >= 256 ? pal[c] : blend(row[x], pal[c], al[c] * alpha >> 8);
          }
        }
      } else {
        for (int x = xa; x < xb; x++, u += du, v += dv) {
          int tx = u >> 16, ty = v >> 16;
          if ((unsigned)tx >= (unsigned)w || (unsigned)ty >= (unsigned)h) continue;
          unsigned c;
          if (four) {
            uint8_t b = px[ty * stride + (tx >> 1)];
            c = tx & 1 ? b & 15 : b >> 4;
          } else c = px[ty * stride + tx];
          if (!al[c]) continue;
          row[x] = al[c] == 255 && alpha >= 256 ? pal[c] : blend(row[x], pal[c], al[c] * alpha >> 8);
        }
      }
    }
  }
}

void gfx_poly_color(const float *xy, int n, uint16_t c, int alpha) {
  Edges E;
  int16_t sp[48];
  band_edges(xy, n, &E);
  for (int r = 0; r < BAND && (E.ne || E.nl); r++) {
    float yc = gfx_y0 + r + 0.5f;
    int k = spans(&E, yc, sp);
    uint16_t *row = gfx_band + r * SCREEN_W;
    for (int s = 0; s < k; s++) {
      int xa = sp[2 * s], xb = sp[2 * s + 1];
      if (alpha >= 256)
        for (int x = xa; x < xb; x++) row[x] = c;
      else
        for (int x = xa; x < xb; x++) row[x] = blend(row[x], c, alpha);
    }
  }
}

void gfx_pic(int id, float x, float y, float sx, float sy, float angle, int alpha) {
  const Pic *p = pic(id);
  if (!p || sx == 0 || sy == 0) return;
  float c = cosf(angle), s = sinf(angle);
  float corners[8];
  float lx[4] = {0, (float)p->w, (float)p->w, 0}, ly[4] = {0, 0, (float)p->h, (float)p->h};
  float miny = 1e9f, maxy = -1e9f;
  for (int i = 0; i < 4; i++) {
    float dx = (lx[i] - p->px) * sx, dy = (ly[i] - p->py) * sy;
    corners[2 * i] = x + dx * c - dy * s;
    corners[2 * i + 1] = y + dx * s + dy * c;
    if (corners[2 * i + 1] < miny) miny = corners[2 * i + 1];
    if (corners[2 * i + 1] > maxy) maxy = corners[2 * i + 1];
  }
  if (!gfx_rows(miny, maxy)) return;
  /* screen to picture: undo the turn, then the scale */
  Map m;
  m.ux = c / sx, m.uy = s / sx, m.u0 = p->px - (x * c + y * s) / sx;
  m.vx = -s / sy, m.vy = c / sy, m.v0 = p->py - (-x * s + y * c) / sy;
  gfx_poly_pic(corners, 4, p, &m, false, alpha);
}

void gfx_pic_at(int id, float x, float y, float scale, int alpha) {
  const Pic *p = pic(id);
  if (!p) return;
  if (!gfx_rows(y, y + p->h * scale)) return;
  if (scale == 1 && x == (int)x && y == (int)y) {
    /* straight copy */
    const uint16_t *pal = pic_pal(p);
    const uint8_t *al = pic_alpha(p), *px = pic_px(p);
    int ix = (int)x, iy = (int)y;
    int r0 = gfx_y0 > iy ? gfx_y0 - iy : 0, r1 = p->h;
    if (iy + r1 > gfx_y0 + BAND) r1 = gfx_y0 + BAND - iy;
    int c0 = ix < 0 ? -ix : 0, c1 = p->w;
    if (ix + c1 > SCREEN_W) c1 = SCREEN_W - ix;
    bool opaque = (p->flags & IMG_OPAQUE) && alpha >= 256;
    if (opaque) {
      /* a straight copy of opaque rows (the backgrounds) */
      for (int r = r0; r < r1; r++) {
        uint16_t *row = gfx_band + (iy + r - gfx_y0) * SCREEN_W + ix;
        if (p->flags & IMG_4BIT) {
          const uint8_t *src = px + r * ((p->w + 1) >> 1);
          int cx = c0;
          if (cx & 1) row[cx] = pal[src[cx >> 1] & 15], cx++;
          for (; cx + 1 < c1; cx += 2) {
            uint8_t b = src[cx >> 1];
            row[cx] = pal[b >> 4], row[cx + 1] = pal[b & 15];
          }
          if (cx < c1) row[cx] = pal[src[cx >> 1] >> 4];
        } else {
          const uint8_t *src = px + r * p->w;
          for (int cx = c0; cx < c1; cx++) row[cx] = pal[src[cx]];
        }
      }
      return;
    }
    for (int r = r0; r < r1; r++) {
      uint16_t *row = gfx_band + (iy + r - gfx_y0) * SCREEN_W + ix;
      for (int cx = c0; cx < c1; cx++) {
        unsigned c = pic_at(p, px, cx, r);
        if (opaque) row[cx] = pal[c];
        else if (al[c]) row[cx] = al[c] == 255 && alpha >= 256 ? pal[c] : blend(row[cx], pal[c], al[c] * alpha >> 8);
      }
    }
    return;
  }
  float corners[8] = {x, y, x + p->w * scale, y, x + p->w * scale, y + p->h * scale, x, y + p->h * scale};
  Map m = {1 / scale, 0, -x / scale, 0, 1 / scale, -y / scale};
  gfx_poly_pic(corners, 4, p, &m, false, alpha);
}

void gfx_fill(int x, int y, int w, int h, uint16_t c, int alpha) {
  int y0 = y > gfx_y0 ? y : gfx_y0, y1 = y + h < gfx_y0 + BAND ? y + h : gfx_y0 + BAND;
  if (x < 0) w += x, x = 0;
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (w <= 0) return;
  for (int yy = y0; yy < y1; yy++) {
    uint16_t *row = gfx_band + (yy - gfx_y0) * SCREEN_W + x;
    if (alpha >= 256)
      for (int i = 0; i < w; i++) row[i] = c;
    else
      for (int i = 0; i < w; i++) row[i] = blend(row[i], c, alpha);
  }
}

void gfx_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int alpha) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  if (l <= 0) return;
  float nx = -dy / l * w / 2, ny = dx / l * w / 2;
  float q[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
  float lo = fminf(fminf(q[1], q[3]), fminf(q[5], q[7])), hi = fmaxf(fmaxf(q[1], q[3]), fmaxf(q[5], q[7]));
  if (!gfx_rows(lo, hi)) return;
  gfx_poly_color(q, 4, c, alpha);
}

/* ---- text ---- */

static void glyph(const Font *f, const Glyph *g, int x, int y, uint16_t color, int alpha) {
  const uint8_t *bits = font_bits(f) + g->off;
  int gx = x + g->x, gy = y + g->y;
  for (int r = 0; r < g->h; r++) {
    int yy = gy + r;
    if (yy < gfx_y0 || yy >= gfx_y0 + BAND) continue;
    uint16_t *row = gfx_band + (yy - gfx_y0) * SCREEN_W;
    for (int c = 0; c < g->w; c++) {
      int xx = gx + c;
      if ((unsigned)xx >= SCREEN_W) continue;
      int i = r * g->w + c;
      int v = i & 1 ? bits[i >> 1] & 15 : bits[i >> 1] >> 4;
      if (!v) continue;
      int a = (v * 17 + 1) * alpha >> 8;
      row[xx] = a >= 255 ? color : blend(row[xx], color, a);
    }
  }
}

int gfx_text_width(int fid, const char *s) {
  const Font *f = font(fid);
  const Glyph *gs = font_glyphs(f);
  int w = 0;
  for (; *s; s++) {
    unsigned ch = (uint8_t)*s;
    if (ch < f->first || ch >= f->first + f->count) continue;
    w += gs[ch - f->first].adv;
  }
  return w;
}

int gfx_text(int fid, float fx, float fy, const char *s, uint16_t color, uint16_t outline, int thick, int align) {
  const Font *f = font(fid);
  const Glyph *gs = font_glyphs(f);
  int w = gfx_text_width(fid, s);
  int x = (int)(fx + 0.5f) - (align == 1 ? w / 2 : align == 2 ? w : 0), y = (int)(fy + 0.5f);
  if (y + thick + 8 < gfx_y0 || y - f->ascent - thick > gfx_y0 + BAND) return w;
  for (int pass = thick ? 0 : 1; pass < 2; pass++) {
    int px = x;
    for (const char *p = s; *p; p++) {
      unsigned ch = (uint8_t)*p;
      if (ch < f->first || ch >= f->first + f->count) continue;
      const Glyph *g = &gs[ch - f->first];
      if (g->w) {
        if (pass == 0) {
          for (int oy = -thick; oy <= thick; oy++)
            for (int ox = -thick; ox <= thick; ox++)
              if ((ox || oy) && ox * ox + oy * oy <= thick * thick + 1) glyph(f, g, px + ox, y + oy, outline, 256);
        } else glyph(f, g, px, y, color, 256);
      }
      px += g->adv;
    }
  }
  return w;
}
