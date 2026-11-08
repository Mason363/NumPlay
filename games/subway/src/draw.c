#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* Flat drawing over a band (draw.h). */
#include "draw.h"

/* (what is drawn every frame, the HUD's boxes, letters and pictures: fast rather than small) */
#if defined(__GNUC__) && !defined(__clang__)
#define HOT __attribute__((optimize("O2")))
#else
#define HOT
#endif

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

HOT void d_rrect(int x, int y, int w, int h, int r, uint16_t c, int a) {
  if (r * 2 > w) r = w / 2;
  if (r * 2 > h) r = h / 2;
  int y0 = imax(y, r_y0), y1 = imin(y + h, r_y1), x0 = imax(x, 0), x1 = imin(x + w, SCREEN_W);
  /* (inside, a see-through box's colour spread as r_mix565 spreads it, times its share: a pixel is a multiply and a
   * fold) */
  int ka = 32 * a >> 8;
  uint32_t cs = ((c | (uint32_t)c << 16) & 0x07E0F81F) * (uint32_t)ka, keep = (uint32_t)(32 - ka);
  for (int j = y0; j < y1; j++) {
    uint16_t *p = row(j);
    int i0 = x0, i1 = x1;
    if (j < y + r || j >= y + h - r) {
      /* the rounded corners' rows: their ends pixel by pixel */
      i0 = imin(imax(x + r, x0), x1), i1 = imax(imin(x + w - r, x1), i0);
      for (int side = 0; side < 2; side++)
        for (int i = side ? i1 : x0, e = side ? x1 : i0; i < e; i++) {
          int k = rr_cover(i, j, x, y, w, h, r) * a >> 8;
          if (k) p[i] = k >= 32 ? c : r_mix565(p[i], c, k);
        }
    }
    if (ka >= 32)
      for (int i = i0; i < i1; i++) p[i] = c;
    else
      for (int i = i0; i < i1; i++) {
        uint32_t q = (p[i] | (uint32_t)p[i] << 16) & 0x07E0F81F;
        q = ((q * keep + cs) >> 5) & 0x07E0F81F;
        p[i] = (uint16_t)(q | q >> 16);
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

HOT static void glyph(const uint8_t *data, const Glyph *g, int x, int baseline, uint16_t c, int a) {
  int top = baseline + g->y, left = x + g->x;
  if (top >= r_y1 || top + g->h <= r_y0) return;
  const uint8_t *d = data + g->offset;
  /* (the rows and columns in the band and on the screen; each of its 16 levels as r_mix565 takes it) */
  int j0 = imax(r_y0 - top, 0), j1 = imin(r_y1 - top, g->h), i0 = imax(-left, 0), i1 = imin(SCREEN_W - left, g->w);
  uint8_t tv[16];
  for (int v = 0; v < 16; v++) tv[v] = (uint8_t)((v * 2 + (v >> 3)) * a >> 8);   /* 0..15 to 0..32 */
  for (int j = j0; j < j1; j++) {
    uint16_t *p = row(top + j) + left;
    for (int i = i0, k = j * g->w + i0; i < i1; i++, k++) {
      int v = d[k >> 1] >> ((k & 1) * 4) & 15;
      if (!v) continue;
      int t = tv[v];
      p[i] = t >= 32 ? c : r_mix565(p[i], c, t);
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

HOT void d_image(int x, int y, const Image *im) {
  int y0 = imax(y, r_y0), y1 = imin(y + im->h, r_y1);
  for (int j = y0; j < y1; j++) {
    const uint8_t *r = im->runs + im->rows[j - y], *e = im->runs + im->rows[j - y + 1];
    uint16_t *p = row(j);
    int i = x;
    for (; r < e; r += 2) {
      int n = r[0], v = r[1], a = v >> 5;
      if (a) {
        uint16_t c = im->pal[v & 31];
        int t = a * 32 / 7;
        for (int k = 0; k < n; k++)
          if ((unsigned)(i + k) < SCREEN_W) p[i + k] = t >= 32 ? c : r_mix565(p[i + k], c, t);
      }
      i += n;
    }
  }
}

void d_slice(const Slice *s, int x, int y, int w, int h, int a256) {
  int y0 = imax(y, r_y0), y1 = imin(y + h, r_y1);
  if (y0 >= y1) return;
  int mw = w - 2 * s->bx, mh = h - 2 * s->by, sw = s->w - 2 * s->bx, sh = s->h - 2 * s->by;
  if (mw < 1) mw = 1;
  if (mh < 1) mh = 1;
  for (int j = y0; j < y1; j++) {
    int dy = j - y, sy = dy < s->by ? dy : dy >= h - s->by ? s->h - (h - dy) : s->by + (dy - s->by) * sh / mh;
    const uint16_t *src = s->px + sy * s->w;
    const uint8_t *sa = s->a + sy * s->w;
    uint16_t *p = row(j);
    for (int i = imax(x, 0); i < imin(x + w, SCREEN_W); i++) {
      int dx = i - x, sx = dx < s->bx ? dx : dx >= w - s->bx ? s->w - (w - dx) : s->bx + (dx - s->bx) * sw / mw;
      int al = sa[sx] * a256 >> 8;
      if (al < 8) continue;
      p[i] = al > 247 ? src[sx] : r_mix565(p[i], src[sx], (al + 4) >> 3);
    }
  }
}
