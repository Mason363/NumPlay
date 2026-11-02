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

static inline void plot(int x, int y, uint16_t c, int a) {
  if ((unsigned)x >= SCREEN_W || y < gfx_y0 || y >= gfx_y0 + BAND_H) return;
  uint16_t *p = gfx_band + (y - gfx_y0) * SCREEN_W + x;
  *p = a >= 32 ? c : gfx_mix(c, *p, a);
}

void gfx_ellipse(int cx, int cy, int rx, int ry, uint16_t c, int a) {
  if (rx <= 0 || ry <= 0 || !gfx_rows(cy - ry, 2 * ry + 1)) return;
  /* for each row the outline's two ends, joined to the next row's so it has no gaps */
  int prev = -1;
  for (int dy = -ry; dy <= ry; dy++) {
    int y = cy + dy;
    int64_t t = (int64_t)rx * rx * ((int64_t)ry * ry - (int64_t)dy * dy);
    int dx = 0;
    if (t > 0) {
      int64_t s = (int64_t)ry * ry;
      while ((int64_t)(dx + 1) * (dx + 1) * s <= t) dx++;
    }
    int from = prev < 0 ? dx : (prev < dx ? prev + 1 : dx);
    int to = prev < 0 ? dx : (prev > dx ? prev - 1 : dx);
    if (from > to) { int k = from; from = to; to = k; }
    if (y >= gfx_y0 && y < gfx_y0 + BAND_H)
      for (int k = from; k <= to; k++) {
        plot(cx - k, y, c, a);
        if (k) plot(cx + k, y, c, a);
      }
    prev = dx;
  }
}

void gfx_ellipse_fill(int cx, int cy, int rx, int ry, uint16_t c, int a) {
  if (rx <= 0 || ry <= 0 || !gfx_rows(cy - ry, 2 * ry + 1)) return;
  for (int y = cy - ry; y <= cy + ry; y++) {
    if (y < gfx_y0 || y >= gfx_y0 + BAND_H) continue;
    int dy = y - cy;
    int64_t t = (int64_t)rx * rx * ((int64_t)ry * ry - (int64_t)dy * dy), s = (int64_t)ry * ry;
    int dx = 0;
    while ((int64_t)(dx + 1) * (dx + 1) * s <= t) dx++;
    if (a >= 32) gfx_fill(cx - dx, y, 2 * dx + 1, 1, c);
    else gfx_blend(cx - dx, y, 2 * dx + 1, 1, c, a);
  }
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, e = dx + dy;
  for (int n = 0; n < 2000; n++) {
    plot(x0, y0, c, 32);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * e;
    if (e2 >= dy) e += dy, x0 += sx;
    if (e2 <= dx) e += dx, y0 += sy;
  }
}

void gfx_panel(int x, int y, int w, int h, uint16_t fill, uint16_t border) {
  gfx_fill(x + 1, y + 1, w - 2, h - 2, fill);
  gfx_fill(x + 2, y, w - 4, 1, border);
  gfx_fill(x + 2, y + h - 1, w - 4, 1, border);
  gfx_fill(x, y + 2, 1, h - 4, border);
  gfx_fill(x + w - 1, y + 2, 1, h - 4, border);
  gfx_fill(x + 1, y + 1, 1, 1, border);
  gfx_fill(x + w - 2, y + 1, 1, 1, border);
  gfx_fill(x + 1, y + h - 2, 1, 1, border);
  gfx_fill(x + w - 2, y + h - 2, 1, 1, border);
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

int gfx_text(int font, int x, int y, const char *s, uint16_t c) {
  const uint8_t *f = font_at(font);
  int x0 = x;
  bool here = y < gfx_y0 + BAND_H && y + f[0] > gfx_y0;
  for (; *s; s++) {
    int ch = (uint8_t)*s;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *g = f + 4 + 12 * (ch - 32);
    if (here && g[1]) alpha_rows(f + rd32(g + 8), g[1], g[2], x + (int8_t)g[3], y + (int8_t)g[4], c);
    x += g[0];
  }
  return x - x0;
}

int gfx_text_w(int font, const char *s) {
  const uint8_t *f = font_at(font);
  int w = 0;
  for (; *s; s++) {
    int ch = (uint8_t)*s;
    if (ch < 32 || ch > 126) ch = '?';
    w += f[4 + 12 * (ch - 32)];
  }
  return w;
}

void gfx_text_c(int font, int cx, int y, const char *s, uint16_t c) { gfx_text(font, cx - gfx_text_w(font, s) / 2, y, s, c); }
void gfx_text_r(int font, int rx, int y, const char *s, uint16_t c) { gfx_text(font, rx - gfx_text_w(font, s), y, s, c); }
void gfx_text_o(int font, int x, int y, const char *s, uint16_t c) {
  if (!gfx_rows(y - 1, gfx_line_h(font) + 2)) return;
  static const int8_t d[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
  for (int k = 0; k < 8; k++) gfx_text(font, x + d[k][0], y + d[k][1], s, RGB(0x2A1A0C));
  gfx_text(font, x, y, s, c);
}
void gfx_text_oc(int font, int cx, int y, const char *s, uint16_t c) { gfx_text_o(font, cx - gfx_text_w(font, s) / 2, y, s, c); }

int gfx_text_box(int font, int x, int y, int w, const char *s, uint16_t c) {
  int lh = gfx_line_h(font), yy = y;
  char line[96];
  while (*s) {
    /* as many words as fit */
    int n = 0, best = 0;
    for (;;) {
      int k = n;
      while (s[k] && s[k] != ' ' && s[k] != '\n') k++;
      if (k >= (int)sizeof line - 1) break;
      for (int i = 0; i < k; i++) line[i] = s[i];
      line[k] = 0;
      if (gfx_text_w(font, line) > w && best) break;
      best = k;
      if (!s[k] || s[k] == '\n') break;
      n = k + 1;
    }
    if (!best) best = 1;
    for (int i = 0; i < best; i++) line[i] = s[i];
    line[best] = 0;
    gfx_text(font, x, yy, line, c);
    yy += lh;
    s += best;
    if (*s == ' ' || *s == '\n') s++;
  }
  return yy - y;
}

/* ------------------------------------------------------------------ sprites */
static const uint16_t *spal;

static inline const uint8_t *frame_at(int slot) {
  const uint8_t *f = section(SEC_FRAMES);
  return f + rd32(f + 4 + 4 * slot);
}

void spr_box(int slot, int *w, int *h, int *ox, int *oy) {
  const uint8_t *p = frame_at(slot);
  *w = p[0], *h = p[1], *ox = rds16(p + 2), *oy = rds16(p + 4);
}

static void frame_draw(int slot, int x, int y, bool flip, bool tint, uint16_t tc, int ta) {
  if (!spal) spal = (const uint16_t *)(const void *)section(SEC_SPAL);
  const uint8_t *p = frame_at(slot);
  int w = p[0], h = p[1];
  int left = x - (flip ? w - 1 - rds16(p + 2) : rds16(p + 2)), top = y - rds16(p + 4);
  if (top >= gfx_y0 + BAND_H || top + h <= gfx_y0 || left >= SCREEN_W || left + w <= 0) return;
  p += 6;
  for (int r = 0; r < h; r++) {
    int n = *p++, sy = top + r;
    if (sy < gfx_y0) {   /* above the band: skip the row */
      for (int k = 0; k < n; k++) p += 2 + (p[1] & 0x7F);
      continue;
    }
    if (sy >= gfx_y0 + BAND_H) break;
    uint16_t *d = gfx_band + (sy - gfx_y0) * SCREEN_W;
    int cx = 0;
    for (int k = 0; k < n; k++) {
      cx += p[0];
      int len = p[1] & 0x7F;
      bool half = p[1] & 0x80;
      const uint8_t *c = p + 2;
      for (int i = 0; i < len; i++) {
        int px = flip ? left + w - 1 - (cx + i) : left + cx + i;
        if ((unsigned)px >= SCREEN_W) continue;
        uint16_t col = spal[c[i]];
        if (tint) col = gfx_mix(tc, col, ta);
        d[px] = half ? (uint16_t)((((d[px] ^ col) & 0xF7DEu) >> 1) + (d[px] & col)) : col;
      }
      cx += len;
      p += 2 + len;
    }
  }
}

void spr_frame(int slot, int x, int y, bool flip) { frame_draw(slot, x, y, flip, false, 0, 0); }
void spr_frame_tint(int slot, int x, int y, bool flip, uint16_t c, int a) { frame_draw(slot, x, y, flip, true, c, a); }

static inline const uint8_t *anim_at(int an) { return section(SEC_ANIMS) + 4 * an; }
int anim_frames(int an) { return anim_at(an)[2]; }
int anim_ticks(int an) { const uint8_t *a = anim_at(an); return a[2] * a[3]; }
int anim_slot(int an, int t, bool loop) {
  const uint8_t *a = anim_at(an);
  int n = a[2], k = t < 0 ? 0 : t / a[3];
  k = loop ? k % n : (k >= n ? n - 1 : k);
  return (int)rd16(a) + k;
}
