#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* Flat drawing over a band (draw.h). */
#include "draw.h"

static int imax(int a, int b) { return a > b ? a : b; }
static int imin(int a, int b) { return a < b ? a : b; }
static uint16_t *row(int y) { return r_col + (y - r_y0) * SCREEN_W; }

void d_fill(int x, int y, int w, int h, uint16_t c) {
  int x0 = imax(x, 0), x1 = imin(x + w, SCREEN_W), y0 = imax(y, r_y0), y1 = imin(y + h, r_y1);
  for (int j = y0; j < y1; j++) {
    uint16_t *p = row(j);
    for (int i = x0; i < x1; i++) p[i] = c;
  }
}

void d_fill_a(int x, int y, int w, int h, uint16_t c, int a) {
  if (a >= 255) {
    d_fill(x, y, w, h, c);
    return;
  }
  int x0 = imax(x, 0), x1 = imin(x + w, SCREEN_W), y0 = imax(y, r_y0), y1 = imin(y + h, r_y1), t = a >> 3;
  for (int j = y0; j < y1; j++) {
    uint16_t *p = row(j);
    for (int i = x0; i < x1; i++) p[i] = r_mix565(p[i], c, t);
  }
}

void d_vgrad(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
  int y0 = imax(y, r_y0), y1 = imin(y + h, r_y1);
  for (int j = y0; j < y1; j++) d_fill(x, j, w, 1, r_565(r_mix888(top, bottom, h > 1 ? (j - y) * 256 / (h - 1) : 0)));
}

/* coverage of a pixel by a round box, in 1/32: from the distance to the nearest rounded corner */
static int rr_cover(int i, int j, int x, int y, int w, int h, int r) {
  float px = (float)i + 0.5f, py = (float)j + 0.5f, cx = px, cy = py;
  if (px < x + r) cx = (float)(x + r);
  else if (px > x + w - r) cx = (float)(x + w - r);
  if (py < y + r) cy = (float)(y + r);
  else if (py > y + h - r) cy = (float)(y + h - r);
  float dx = px - cx, dy = py - cy, d2 = dx * dx + dy * dy;
  if (d2 <= (r - 1) * (r - 1)) return 32;
  if (d2 >= (r + 1) * (r + 1)) return 0;
  float e = (float)r - __builtin_sqrtf(d2) + 0.5f;
  return e >= 1 ? 32 : e <= 0 ? 0 : (int)(e * 32);
}

void d_rrect(int x, int y, int w, int h, int r, uint16_t c, int a) {
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  int y0 = imax(y, r_y0), y1 = imin(y + h, r_y1);
  for (int j = y0; j < y1; j++) {
    uint16_t *p = row(j);
    bool edge = j < y + r || j >= y + h - r;
    int x0 = imax(x, 0), x1 = imin(x + w, SCREEN_W);
    for (int i = x0; i < x1; i++) {
      int k = 32;
      if (edge && (i < x + r || i >= x + w - r)) k = rr_cover(i, j, x, y, w, h, r);
      if (!k) continue;
      k = k * a >> 8;
      p[i] = k >= 32 ? c : r_mix565(p[i], c, k);
    }
  }
}

void d_panel(int x, int y, int w, int h, int r, int t, uint32_t border, uint32_t top, uint32_t bottom) {
  d_rrect(x, y, w, h, r, r_565(border), 255);
  int iy = imax(y + t, r_y0), iy1 = imin(y + h - t, r_y1), ih = h - 2 * t;
  for (int j = iy; j < iy1; j++) {
    uint16_t c = r_565(r_mix888(top, bottom, ih > 1 ? (j - y - t) * 256 / (ih - 1) : 0));
    uint16_t *p = row(j);
    int ri = imax(r - t, 1);
    bool edge = j < y + t + ri || j >= y + h - t - ri;
    for (int i = imax(x + t, 0), i1 = imin(x + w - t, SCREEN_W); i < i1; i++) {
      int k = 32;
      if (edge && (i < x + t + ri || i >= x + w - t - ri)) k = rr_cover(i, j, x + t, y + t, w - 2 * t, h - 2 * t, ri);
      if (k) p[i] = k >= 32 ? c : r_mix565(p[i], c, k);
    }
  }
}

void d_disc(float cx, float cy, float r, uint16_t c, int a) {
  int y0 = imax((int)(cy - r - 1), r_y0), y1 = imin((int)(cy + r + 2), r_y1);
  for (int j = y0; j < y1; j++) {
    float dy = (float)j + 0.5f - cy;
    if (dy * dy >= (r + 1) * (r + 1)) continue;
    uint16_t *p = row(j);
    int x0 = imax((int)(cx - r - 1), 0), x1 = imin((int)(cx + r + 2), SCREEN_W);
    for (int i = x0; i < x1; i++) {
      float dx = (float)i + 0.5f - cx, d2 = dx * dx + dy * dy;
      if (d2 >= (r + 0.5f) * (r + 0.5f)) continue;
      float e = r + 0.5f - __builtin_sqrtf(d2);
      int k = e >= 1 ? 32 : (int)(e * 32);
      k = k * a >> 8;
      p[i] = k >= 32 ? c : r_mix565(p[i], c, k);
    }
  }
}

void d_ring(float cx, float cy, float r, float t, uint16_t c, int a) {
  int y0 = imax((int)(cy - r - 1), r_y0), y1 = imin((int)(cy + r + 2), r_y1);
  for (int j = y0; j < y1; j++) {
    float dy = (float)j + 0.5f - cy;
    uint16_t *p = row(j);
    for (int i = imax((int)(cx - r - 1), 0), i1 = imin((int)(cx + r + 2), SCREEN_W); i < i1; i++) {
      float dx = (float)i + 0.5f - cx, d = __builtin_sqrtf(dx * dx + dy * dy);
      float e = t / 2 + 0.5f - (d > r - t / 2 ? d - (r - t / 2) : (r - t / 2) - d);
      if (e <= 0) continue;
      int k = e >= 1 ? 32 : (int)(e * 32);
      k = k * a >> 8;
      p[i] = k >= 32 ? c : r_mix565(p[i], c, k);
    }
  }
}

void d_poly(const float *xy, int n, uint16_t c, int a) {
  float ymin = 1e9f, ymax = -1e9f;
  for (int i = 0; i < n; i++) {
    if (xy[2 * i + 1] < ymin) ymin = xy[2 * i + 1];
    if (xy[2 * i + 1] > ymax) ymax = xy[2 * i + 1];
  }
  int y0 = imax((int)(ymin + 0.5f), r_y0), y1 = imin((int)(ymax + 0.5f), r_y1), t = a >> 3;
  for (int j = y0; j < y1; j++) {
    float yc = (float)j + 0.5f, xl = 1e9f, xr = -1e9f;
    for (int i = 0, k = n - 1; i < n; k = i++) {
      float ya = xy[2 * k + 1], yb = xy[2 * i + 1];
      if ((ya <= yc) == (yb <= yc)) continue;
      float x = xy[2 * k] + (yc - ya) * (xy[2 * i] - xy[2 * k]) / (yb - ya);
      if (x < xl) xl = x;
      if (x > xr) xr = x;
    }
    if (xl > xr) continue;
    int x0 = imax((int)(xl + 0.5f), 0), x1 = imin((int)(xr + 0.5f), SCREEN_W);
    uint16_t *p = row(j);
    for (int i = x0; i < x1; i++) p[i] = t >= 31 ? c : r_mix565(p[i], c, t);
  }
}

void d_line(float x0, float y0, float x1, float y1, float t, uint16_t c, int a) {
  float dx = x1 - x0, dy = y1 - y0, l = __builtin_sqrtf(dx * dx + dy * dy);
  if (l < 0.01f) return;
  float nx = -dy / l * t / 2, ny = dx / l * t / 2;
  float q[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
  d_poly(q, 4, c, a);
}

void d_dim(int a) { d_fill_a(0, r_y0, SCREEN_W, BAND_H, 0, a); }

/* ---------------------------------------------------------------- text */
static const Glyph *glyph_of(const Font *f, char ch) {
  unsigned c = (unsigned char)ch;
  if (c < f->first || c >= (unsigned)f->first + f->count) c = '?';
  return &f->glyphs[c - f->first];
}

static void glyph(const uint8_t *data, const Glyph *g, int x, int baseline, uint16_t c, int a) {
  int top = baseline + g->y, left = x + g->x;
  if (top >= r_y1 || top + g->h <= r_y0) return;
  const uint8_t *d = data + g->offset;
  for (int j = 0; j < g->h; j++) {
    int y = top + j;
    if (y < r_y0 || y >= r_y1) continue;
    uint16_t *p = row(y);
    for (int i = 0; i < g->w; i++) {
      int k = j * g->w + i, v = d[k >> 1] >> ((k & 1) * 4) & 15, xx = left + i;
      if (!v || xx < 0 || xx >= SCREEN_W) continue;
      int t = (v * 2 + (v >> 3)) * a >> 8;   /* 0..15 to 0..32 */
      p[xx] = t >= 32 ? c : r_mix565(p[xx], c, t);
    }
  }
}

int d_text(const Font *f, int x, int baseline, const char *s, uint16_t c, int a) {
  for (; *s; s++) {
    const Glyph *g = glyph_of(f, *s);
    if (g->w) glyph(f->data, g, x, baseline, c, a);
    x += g->advance;
  }
  return x;
}

/* the text's outline (and shadow): the letters grown by t pixels, from the font's tables */
static void outline(const Font *f, int t, int x, int baseline, const char *s, uint16_t c) {
  const Glyph *table = f->outline[t - 1];
  for (; *s; s++) {
    unsigned ch = (unsigned char)*s;
    if (ch < f->first || ch >= (unsigned)f->first + f->count) ch = '?';
    const Glyph *g = &table[ch - f->first];
    if (g->w) glyph(f->outline_data[t - 1], g, x, baseline, c, 255);
    x += glyph_of(f, *s)->advance;
  }
}

int d_text_width(const Font *f, const char *s) {
  int w = 0;
  for (; *s; s++) w += glyph_of(f, *s)->advance;
  return w;
}

void d_label(const Font *f, int x, int baseline, const char *s, uint16_t fill, uint16_t line, int t, int align) {
  int w = d_text_width(f, s);
  if (align == 0) x -= w / 2;
  else if (align > 0) x -= w;
  if (baseline - f->ascent - t >= r_y1 || baseline + f->descent + t + 2 < r_y0) return;
  if (t > 0) outline(f, t > 2 ? 2 : t, x, baseline, s, line);
  d_text(f, x, baseline, s, fill, 255);
}

/* ---------------------------------------------------------------- the game's pictures */
static void blit(const HudPic *p, int x, int y, int a256, int flip, int from_row) {
  int y0 = imax(y + from_row, r_y0), y1 = imin(y + p->h, r_y1);
  int x0 = imax(x, 0), x1 = imin(x + p->w, SCREEN_W);
  if (y0 >= y1 || x0 >= x1) return;
  for (int j = y0; j < y1; j++) {
    const uint8_t *src = hud_px + p->off + (j - y) * p->w;
    uint16_t *d = row(j);
    /* (the picture's pixel for screen x is src[x - x], or mirrored src[w - 1 - (x - x)]: a step of 1 or -1) */
    int base = flip ? p->w - 1 + x : -x, ds = flip ? -1 : 1;
    if (a256 == 256)
      for (int i = x0; i < x1; i++) {
        uint8_t k = src[base + ds * i];
        if (!k) continue;
        int a = hud_alpha[k];
        d[i] = a >= 248 ? hud_pal[k] : r_mix565(d[i], hud_pal[k], a >> 3);
      }
    else
      for (int i = x0; i < x1; i++) {
        uint8_t k = src[base + ds * i];
        if (!k) continue;
        int a = hud_alpha[k] * a256 >> 8;
        d[i] = a >= 248 ? hud_pal[k] : r_mix565(d[i], hud_pal[k], a >> 3);
      }
  }
}

void d_pic(int pic, int x, int y, int a256, int flip, int from_row) { blit(&hud_pics[pic], x, y, a256, flip, from_row); }

static const HudPic *gold_of(const HudFont *f, char ch) {
  int c = (uint8_t)ch - f->first;
  return &hud_pics[f->at + (c >= 0 && c < f->count ? c : 0)];
}

int d_gold_width(const HudFont *f, const char *s) {
  int w = 0;
  for (; *s; s++) w += gold_of(f, *s)->advance;
  return w;
}

int d_gold(const HudFont *f, int x, int y, const char *s, int align, int a256) {
  int w = d_gold_width(f, s);
  x -= align < 0 ? 0 : align == 0 ? w / 2 : w;
  if (y >= r_y1 || y + f->line < r_y0) return w;
  for (; *s; s++) {
    const HudPic *g = gold_of(f, *s);
    if (g->w > 1) blit(g, x + g->ox, y + g->oy, a256, 0, 0);
    x += g->advance;
  }
  return w;
}
