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

void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t c) {
  gfx_fill(x + r, y, w - 2 * r, h, c);
  for (int i = 0; i < r; i++) {
    int dy = r - i, dx = 0;
    while ((dx + 1) * (dx + 1) + dy * dy <= r * r + r) dx++;
    int inset = r - dx;
    gfx_fill(x + inset, y + i, r - inset, 1, c);
    gfx_fill(x + w - r, y + i, r - inset, 1, c);
    gfx_fill(x + inset, y + h - 1 - i, r - inset, 1, c);
    gfx_fill(x + w - r, y + h - 1 - i, r - inset, 1, c);
  }
  gfx_fill(x, y + r, r, h - 2 * r, c);
  gfx_fill(x + w - r, y + r, r, h - 2 * r, c);
}

void gfx_ellipse(int cx, int cy, int rx, int ry, uint16_t c, int a) {
  if (rx <= 0 || ry <= 0 || !gfx_rows(cy - ry, 2 * ry + 1)) return;
  int y0 = cy - ry > gfx_y0 ? cy - ry : gfx_y0, y1 = cy + ry < gfx_y0 + BAND_H - 1 ? cy + ry : gfx_y0 + BAND_H - 1;
  for (int y = y0; y <= y1; y++) {
    int dy = y - cy;
    /* half width at this row: rx * sqrt(1 - dy^2/ry^2) */
    int32_t q = (int32_t)rx * rx * (ry * ry - dy * dy) / (ry * ry);
    int hw = 0;
    while ((hw + 1) * (hw + 1) <= q) hw++;
    if (a >= 32)
      gfx_fill(cx - hw, y, 2 * hw + 1, 1, c);
    else
      gfx_blend(cx - hw, y, 2 * hw + 1, 1, c, a);
  }
}

void gfx_ring(int cx, int cy, int rx, int ry, int t, uint16_t c, int a) {
  if (rx <= 0 || ry <= 0 || !gfx_rows(cy - ry, 2 * ry + 1)) return;
  int y0 = cy - ry > gfx_y0 ? cy - ry : gfx_y0, y1 = cy + ry < gfx_y0 + BAND_H - 1 ? cy + ry : gfx_y0 + BAND_H - 1;
  int irx = rx - t, iry = ry - t;
  for (int y = y0; y <= y1; y++) {
    int dy = y - cy;
    int32_t q = (int32_t)rx * rx * (ry * ry - dy * dy) / (ry * ry);
    int hw = 0, iw = -1;
    while ((hw + 1) * (hw + 1) <= q) hw++;
    if (irx > 0 && iry > 0 && dy * dy < iry * iry) {
      int32_t qi = (int32_t)irx * irx * (iry * iry - dy * dy) / (iry * iry);
      iw = 0;
      while ((iw + 1) * (iw + 1) <= qi) iw++;
    }
    if (iw < 0) {
      gfx_blend(cx - hw, y, 2 * hw + 1, 1, c, a);
    } else {
      gfx_blend(cx - hw, y, hw - iw, 1, c, a);
      gfx_blend(cx + iw + 1, y, hw - iw, 1, c, a);
    }
  }
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y1 - y0 : y0 - y1;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx - dy;
  for (int n = 0; n < 400; n++) {
    if (y0 >= gfx_y0 && y0 < gfx_y0 + BAND_H && (unsigned)x0 < SCREEN_W) gfx_band[(y0 - gfx_y0) * SCREEN_W + x0] = c;
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 > -dy) err -= dy, x0 += sx;
    if (e2 < dx) err += dx, y0 += sy;
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

int gfx_text(int font, int x, int y, const char *s, uint16_t c) {
  const uint8_t *f = font_at(font);
  int x0 = x;
  bool here = gfx_rows(y, f[0] + 2);
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
  if (!gfx_rows(y - 1, gfx_line_h(font) + 4)) return;
  static const int8_t d[8][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-1, -1}, {1, -1}, {-1, 1}, {1, 1}};
  for (int i = 0; i < 8; i++) gfx_text(font, x + d[i][0], y + d[i][1], s, RGB(0x1A1420));
  gfx_text(font, x, y + 2, s, RGB(0x1A1420));
  gfx_text(font, x, y, s, c);
}
void gfx_text_oc(int font, int cx, int y, const char *s, uint16_t c) { gfx_text_o(font, cx - gfx_text_w(font, s) / 2, y, s, c); }

int gfx_wrap(int font, int x, int y, int w, const char *s, uint16_t c) {
  int lh = gfx_line_h(font), y0 = y;
  char line[96];
  while (*s) {
    int n = 0, last_space = -1;
    while (s[n] && s[n] != '\n') {
      if (s[n] == ' ') last_space = n;
      if (n < (int)sizeof line - 1) line[n] = s[n];
      line[n + 1 < (int)sizeof line ? n + 1 : (int)sizeof line - 1] = 0;
      if (gfx_text_w(font, line) > w && last_space > 0) {
        n = last_space;
        break;
      }
      n++;
    }
    int m = n < (int)sizeof line - 1 ? n : (int)sizeof line - 1;
    for (int i = 0; i < m; i++) line[i] = s[i];
    line[m] = 0;
    gfx_text(font, x, y, line, c);
    y += lh;
    s += n;
    while (*s == ' ' || *s == '\n') s++;
  }
  return y - y0;
}

/* ------------------------------------------------------------------ pictures */
static const uint8_t *img_at(int id) {
  const uint8_t *s = section(SEC_IMGS);
  return s + rd32(s + 4 + 4 * id);
}
int gfx_img_w(int id) { return (int)rd16(img_at(id)); }
int gfx_img_h(int id) { return (int)rd16(img_at(id) + 2); }

void gfx_img_box(int id, bool mirror, int *x0, int *y0, int *w, int *h) {
  const uint8_t *p = img_at(id);
  *w = (int)rd16(p), *h = (int)rd16(p + 2);
  int ax = rds16(p + 4), ay = rds16(p + 6);
  *x0 = mirror ? ax - *w + 1 : -ax;
  *y0 = -ay;
}

/* mode 0: opaque colours; mode 1: 6-bit colours with 2 bits of alpha (a quarter to all) */
static void img_draw(int id, int x, int y, int team, bool mirror, int dim, uint16_t tint, int ta) {
  const uint8_t *p = img_at(id);
  int h = (int)rd16(p + 2), ax = rds16(p + 4), ay = rds16(p + 6), mode = p[8];
  int top = y - ay;
  if (!gfx_rows(top, h)) return;
  const uint8_t *pal = cr_bin + rd32(p + (team ? 16 : 12)) + 2;
  int r0 = gfx_y0 > top ? gfx_y0 - top : 0, r1 = top + h < gfx_y0 + BAND_H ? h : gfx_y0 + BAND_H - top;
  int left = mirror ? x + ax : x - ax;   /* mirrored: column c lands at left - c */
  for (int r = r0; r < r1; r++) {
    const uint8_t *q = p + rd16(p + 20 + 2 * r);
    uint16_t *d = gfx_band + (top + r - gfx_y0) * SCREEN_W;
    int runs = *q++;
    while (runs--) {
      int cx = q[0], n = q[1];
      q += 2;
      for (int i = 0; i < n; i++) {
        int v = q[i];
        int sx = mirror ? left - (cx + i) : left + cx + i;
        if ((unsigned)sx >= SCREEN_W) continue;
        uint16_t c;
        int a = 32;
        if (mode) {
          c = (uint16_t)rd16(pal + 2 * (v & 63));
          a = ((v >> 6) + 1) * 8;
        } else {
          c = (uint16_t)rd16(pal + 2 * v);
        }
        if (dim) c = gfx_mix(BLACK, c, dim);
        if (ta) c = gfx_mix(tint, c, ta);
        d[sx] = a >= 32 ? c : gfx_mix(c, d[sx], a);
      }
      q += n;
    }
  }
}

void gfx_img(int id, int x, int y, int team, bool mirror) { img_draw(id, x, y, team, mirror, 0, 0, 0); }
void gfx_img_dim(int id, int x, int y, int team, bool mirror, int dim) { img_draw(id, x, y, team, mirror, dim, 0, 0); }
void gfx_img_tint(int id, int x, int y, int team, bool mirror, uint16_t c, int a) { img_draw(id, x, y, team, mirror, 0, c, a); }

void gfx_arena(const uint8_t *idx, const uint8_t *pal, int y0, int rows) {
  int a = gfx_y0 > y0 ? gfx_y0 : y0, b = gfx_y0 + BAND_H < y0 + rows ? gfx_y0 + BAND_H : y0 + rows;
  for (int y = a; y < b; y++) {
    const uint8_t *s = idx + (y - y0) * SCREEN_W;
    uint16_t *d = gfx_band + (y - gfx_y0) * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) d[x] = (uint16_t)(pal[2 * s[x]] | pal[2 * s[x] + 1] << 8);
  }
}
