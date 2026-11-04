/* Among Us: drawing (see gfx.h). */
#include <math.h>
#include <string.h>
#include "gfx.h"

uint16_t *gfx_band;
int gfx_y0;
const uint8_t *gfx_mix_alpha;

void gfx_init(uint16_t *band) {
  gfx_band = band;
  gfx_mix_alpha = (const uint8_t *)blob(BLOB_MIX) + 3;
}

void gfx_frame(void (*draw)(void)) {
  for (int y = 0; y < SCREEN_H; y += BAND) {
    gfx_y0 = y;
    draw();
    plat_push(0, y, SCREEN_W, BAND, gfx_band);
  }
}

/* the game's player shader: red is the body, green the visor, blue the shadow */
void gfx_mix_palette(uint16_t *pal, uint32_t body, uint32_t shadow, uint32_t visor) {
  const uint8_t *m = (const uint8_t *)blob(BLOB_MIX);
  for (int i = 0; i < N_MIX; i++, m += 4) {
    int c[3];
    for (int k = 0; k < 3; k++) {
      int sh = 16 - 8 * k;
      int v = (m[0] * (int)(body >> sh & 255) + m[1] * (int)(visor >> sh & 255) + m[2] * (int)(shadow >> sh & 255)) / 255;
      c[k] = v > 255 ? 255 : v;
    }
    pal[i] = rgb565((uint32_t)c[0] << 16 | (uint32_t)c[1] << 8 | (uint32_t)c[2]);
  }
}

static inline unsigned px_at(const Pic *p, const uint8_t *px, int x, int y) {
  if (p->flags & P_4BIT) {
    uint8_t b = px[y * ((p->w + 1) >> 1) + (x >> 1)];
    return x & 1 ? b & 15 : b >> 4;
  }
  return px[y * p->w + x];
}

void gfx_spr_clip(const Pic *p, int x, int y, bool flip, const uint16_t *pal, int alpha, int cx0, int cy0, int cx1,
                  int cy1) {
  if (!p) return;
  int x0 = x - (flip ? p->w - p->ox : p->ox), y0 = y - p->oy;
  if (cy0 < gfx_y0) cy0 = gfx_y0;
  if (cy1 > gfx_y0 + BAND) cy1 = gfx_y0 + BAND;
  if (cx0 < 0) cx0 = 0;
  if (cx1 > SCREEN_W) cx1 = SCREEN_W;
  int r0 = cy0 - y0, r1 = cy1 - y0, c0 = cx0 - x0, c1 = cx1 - x0;
  if (r0 < 0) r0 = 0;
  if (r1 > p->h) r1 = p->h;
  if (c0 < 0) c0 = 0;
  if (c1 > p->w) c1 = p->w;
  if (r0 >= r1 || c0 >= c1) return;
  const uint8_t *al = p->flags & P_MIX ? gfx_mix_alpha : pic_alpha(p);
  const uint16_t *pl = pal ? pal : pic_pal(p);
  const uint8_t *px = pic_px(p);
  bool four = p->flags & P_4BIT;
  int astep = (p->flags & P_MIX) ? 4 : 1;
  for (int r = r0; r < r1; r++) {
    uint16_t *row = gfx_band + (y0 + r - gfx_y0) * SCREEN_W + x0;
    const uint8_t *src = px + r * (four ? (p->w + 1) >> 1 : p->w);
    for (int c = c0; c < c1; c++) {
      int sc = flip ? p->w - 1 - c : c;
      unsigned i = four ? (sc & 1 ? src[sc >> 1] & 15 : src[sc >> 1] >> 4) : src[sc];
      unsigned a = al[i * astep];
      if (!a) continue;
      if (a >= 250 && alpha >= 256) row[c] = pl[i];
      else row[c] = blend(row[c], pl[i], (int)(a * (unsigned)alpha >> 8));
    }
  }
}

void gfx_spr(const Pic *p, int x, int y, bool flip, const uint16_t *pal, int alpha) {
  gfx_spr_clip(p, x, y, flip, pal, alpha, 0, 0, SCREEN_W, SCREEN_H);
}

void gfx_spr_tint(const Pic *p, int x, int y, uint16_t col, int alpha) {
  if (!p) return;
  int x0 = x - p->ox, y0 = y - p->oy;
  if (!gfx_rows(y0, y0 + p->h)) return;
  const uint8_t *al = p->flags & P_MIX ? gfx_mix_alpha : pic_alpha(p);
  int astep = (p->flags & P_MIX) ? 4 : 1;
  const uint8_t *px = pic_px(p);
  for (int r = 0; r < p->h; r++) {
    int yy = y0 + r;
    if (yy < gfx_y0 || yy >= gfx_y0 + BAND) continue;
    uint16_t *row = gfx_band + (yy - gfx_y0) * SCREEN_W;
    for (int c = 0; c < p->w; c++) {
      int xx = x0 + c;
      if ((unsigned)xx >= SCREEN_W) continue;
      unsigned a = al[px_at(p, px, c, r) * astep];
      if (a > 128) row[xx] = blend(row[xx], col, alpha);
    }
  }
}

void gfx_pic(const Pic *p, float x, float y, float sx, float sy, float angle, const uint16_t *pal, int alpha) {
  if (!p || sx == 0 || sy == 0) return;
  float c = cosf(angle), s = sinf(angle);
  float miny = 1e9f, maxy = -1e9f, minx = 1e9f, maxx = -1e9f;
  float lx[4] = {0, (float)p->w, (float)p->w, 0}, ly[4] = {0, 0, (float)p->h, (float)p->h};
  for (int i = 0; i < 4; i++) {
    float dx = (lx[i] - p->ox) * sx, dy = (ly[i] - p->oy) * sy;
    float X = x + dx * c - dy * s, Y = y + dx * s + dy * c;
    if (Y < miny) miny = Y;
    if (Y > maxy) maxy = Y;
    if (X < minx) minx = X;
    if (X > maxx) maxx = X;
  }
  int ya = (int)floorf(miny), yb = (int)ceilf(maxy);
  if (!gfx_rows(ya, yb)) return;
  if (ya < gfx_y0) ya = gfx_y0;
  if (yb > gfx_y0 + BAND) yb = gfx_y0 + BAND;
  int xa = (int)floorf(minx), xb = (int)ceilf(maxx);
  if (xa < 0) xa = 0;
  if (xb > SCREEN_W) xb = SCREEN_W;
  const uint8_t *al = p->flags & P_MIX ? gfx_mix_alpha : pic_alpha(p);
  int astep = (p->flags & P_MIX) ? 4 : 1;
  const uint16_t *pl = pal ? pal : pic_pal(p);
  const uint8_t *px = pic_px(p);
  /* screen to picture: u = ((X-x) c + (Y-y) s) / sx + ox */
  for (int yy = ya; yy < yb; yy++) {
    uint16_t *row = gfx_band + (yy - gfx_y0) * SCREEN_W;
    float fy = yy + 0.5f - y;
    for (int xx = xa; xx < xb; xx++) {
      float fx = xx + 0.5f - x;
      int u = (int)floorf((fx * c + fy * s) / sx + p->ox), v = (int)floorf((-fx * s + fy * c) / sy + p->oy);
      if ((unsigned)u >= p->w || (unsigned)v >= p->h) continue;
      unsigned i = px_at(p, px, u, v);
      unsigned a = al[i * astep];
      if (!a) continue;
      if (a >= 250 && alpha >= 256) row[xx] = pl[i];
      else row[xx] = blend(row[xx], pl[i], (int)(a * (unsigned)alpha >> 8));
    }
  }
}

void gfx_tiles(const TilePic *s, int x, int y) {
  const uint16_t *map = tile_map(s);
  x -= s->ox, y -= s->oy;
  int y0 = gfx_y0 - y, y1 = y0 + BAND;   /* the band's rows in the picture */
  if (y1 <= 0 || y0 >= s->th * 16) return;
  int ty0 = y0 < 0 ? 0 : y0 >> 4, ty1 = (y1 - 1) >> 4;
  if (ty1 >= s->th) ty1 = s->th - 1;
  int tx0 = x < 0 ? (-x) >> 4 : 0, tx1 = (SCREEN_W - 1 - x) >> 4;
  if (tx1 >= s->tw) tx1 = s->tw - 1;
  for (int ty = ty0; ty <= ty1; ty++) {
    int r0 = ty * 16 < y0 ? y0 - ty * 16 : 0, r1 = ty * 16 + 16 > y1 ? y1 - ty * 16 : 16;
    for (int tx = tx0; tx <= tx1; tx++) {
      unsigned t = map[ty * s->tw + tx];
      if (t == T_EMPTY) continue;
      int sx = x + tx * 16;
      int c0 = sx < 0 ? -sx : 0, c1 = sx + 16 > SCREEN_W ? SCREEN_W - sx : 16;
      const uint8_t *d = tile_data(s, t);
      const uint16_t *pal = (const uint16_t *)(const void *)d;
      bool clear = t & T_CLEAR;
      for (int r = r0; r < r1; r++) {
        uint16_t *row = gfx_band + (y + ty * 16 + r - gfx_y0) * SCREEN_W + sx;
        const uint8_t *src = d + 32 + r * 8;
        if (!clear) {
          int c = c0;
          if (c & 1) row[c] = pal[src[c >> 1] & 15], c++;
          for (; c + 1 < c1; c += 2) {
            uint8_t b = src[c >> 1];
            row[c] = pal[b >> 4], row[c + 1] = pal[b & 15];
          }
          if (c < c1) row[c] = pal[src[c >> 1] >> 4];
        } else
          for (int c = c0; c < c1; c++) {
            unsigned k = c & 1 ? src[c >> 1] & 15 : src[c >> 1] >> 4;
            if (k != 15) row[c] = pal[k];
          }
      }
    }
  }
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

void gfx_rect(int x, int y, int w, int h, int t, uint16_t c) {
  gfx_fill(x, y, w, t, c, 256);
  gfx_fill(x, y + h - t, w, t, c, 256);
  gfx_fill(x, y + t, t, h - 2 * t, c, 256);
  gfx_fill(x + w - t, y + t, t, h - 2 * t, c, 256);
}

int gfx_poly_spans(const float *xy, int n, int y, int16_t *out, int max) {
  float yc = y + 0.5f, xs[32];
  int k = 0;
  for (int i = 0, j = n - 1; i < n; j = i++) {
    float yi = xy[2 * i + 1], yj = xy[2 * j + 1];
    if ((yi > yc) != (yj > yc) && k < 32) xs[k++] = xy[2 * j] + (yc - yj) * (xy[2 * i] - xy[2 * j]) / (yi - yj);
  }
  for (int a = 1; a < k; a++) {
    float v = xs[a];
    int b = a - 1;
    while (b >= 0 && xs[b] > v) xs[b + 1] = xs[b], b--;
    xs[b + 1] = v;
  }
  int ns = 0;
  for (int i = 0; i + 1 < k && ns < max; i += 2) {
    int xa = (int)ceilf(xs[i] - 0.5f), xb = (int)ceilf(xs[i + 1] - 0.5f);
    if (xa < 0) xa = 0;
    if (xb > SCREEN_W) xb = SCREEN_W;
    if (xa < xb) out[2 * ns] = (int16_t)xa, out[2 * ns + 1] = (int16_t)xb, ns++;
  }
  return ns;
}

void gfx_poly(const float *xy, int n, uint16_t c, int alpha) {
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < n; i++) {
    if (xy[2 * i + 1] < lo) lo = xy[2 * i + 1];
    if (xy[2 * i + 1] > hi) hi = xy[2 * i + 1];
  }
  if (!gfx_rows((int)floorf(lo), (int)ceilf(hi) + 1)) return;
  int16_t sp[16];
  for (int r = 0; r < BAND; r++) {
    int y = gfx_y0 + r;
    if (y + 1 < lo || y > hi) continue;
    int k = gfx_poly_spans(xy, n, y, sp, 8);
    uint16_t *row = gfx_band + r * SCREEN_W;
    for (int s = 0; s < k; s++)
      for (int x = sp[2 * s]; x < sp[2 * s + 1]; x++) row[x] = alpha >= 256 ? c : blend(row[x], c, alpha);
  }
}

void gfx_line(float x0, float y0, float x1, float y1, float w, uint16_t c, int alpha) {
  float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
  if (l <= 0) return;
  float nx = -dy / l * w / 2, ny = dx / l * w / 2;
  float q[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
  gfx_poly(q, 4, c, alpha);
}

void gfx_disc(float cx, float cy, float r, uint16_t c, int alpha) {
  int ya = (int)floorf(cy - r), yb = (int)ceilf(cy + r);
  if (!gfx_rows(ya, yb)) return;
  for (int y = ya; y < yb; y++) {
    if (y < gfx_y0 || y >= gfx_y0 + BAND) continue;
    float dy = y + 0.5f - cy, w2 = r * r - dy * dy;
    if (w2 <= 0) continue;
    float w = sqrtf(w2);
    int xa = (int)ceilf(cx - w - 0.5f), xb = (int)ceilf(cx + w - 0.5f);
    gfx_fill(xa, y, xb - xa, 1, c, alpha);
  }
}

/* ---- text ---- */

static void glyph(const Font *f, const Glyph *g, int x, int y, uint16_t color) {
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
      row[xx] = v == 15 ? color : blend(row[xx], color, v * 17 + 1);
    }
  }
}

int gfx_text_width(int fid, const char *s) {
  const Font *f = font(fid);
  const Glyph *gs = font_glyphs(f);
  int w = 0;
  for (; *s && *s != '\n'; s++) {
    unsigned ch = (uint8_t)*s;
    if (ch < f->first || ch >= f->first + f->count) continue;
    w += gs[ch - f->first].adv;
  }
  return w;
}

int gfx_text(int fid, int x, int y, const char *s, uint16_t color, uint16_t outline, int thick, int align) {
  const Font *f = font(fid);
  const Glyph *gs = font_glyphs(f);
  int w = gfx_text_width(fid, s);
  x -= align == 1 ? w / 2 : align == 2 ? w : 0;
  if (y + thick + 8 < gfx_y0 || y - f->ascent - thick > gfx_y0 + BAND) return w;
  for (int pass = thick ? 0 : 1; pass < 2; pass++) {
    int px = x;
    for (const char *p = s; *p && *p != '\n'; p++) {
      unsigned ch = (uint8_t)*p;
      if (ch < f->first || ch >= f->first + f->count) continue;
      const Glyph *g = &gs[ch - f->first];
      if (g->w) {
        if (pass == 0) {
          for (int oy = -thick; oy <= thick; oy++)
            for (int ox = -thick; ox <= thick; ox++)
              if ((ox || oy) && ox * ox + oy * oy <= thick * thick + 1) glyph(f, g, px + ox, y + oy, outline);
        } else glyph(f, g, px, y, color);
      }
      px += g->adv;
    }
  }
  return w;
}

int gfx_text_wrap(int fid, int x, int y, int w, int lh, const char *s, uint16_t color, uint16_t outline, int thick,
                  int align) {
  char line[96];
  int n = 0;
  while (*s) {
    /* the longest run of words that fits */
    int len = 0, fit = 0;
    for (;;) {
      while (s[len] && s[len] != ' ' && s[len] != '\n') len++;
      if (len >= (int)sizeof line) break;
      memcpy(line, s, (size_t)len);
      line[len] = 0;
      if (gfx_text_width(fid, line) > w && fit) break;
      fit = len;
      if (!s[len] || s[len] == '\n') break;
      len++;
    }
    if (!fit) fit = len < (int)sizeof line - 1 ? len : (int)sizeof line - 1;
    memcpy(line, s, (size_t)fit);
    line[fit] = 0;
    gfx_text(fid, x, y + n * lh, line, color, outline, thick, align);
    n++;
    s += fit;
    while (*s == ' ' || *s == '\n') s++;
  }
  return n;
}
