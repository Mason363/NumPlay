/* Drawing on the CE's screen (gfx.h). */
#include "gfx.h"
#include <string.h>

void zx0_Decompress(void *dst, const void *src);

static Assets A;

void gfx_init(const Assets *a, int tables) {
  A = *a;
  pic_init(asset(tables));
}

const uint8_t *asset(int id) {
  uint32_t at = A.at[id];
  return A.vars[at >> 16] + (at & 0xFFFF);
}

/* ------------------------------------------------------------------ maps: two places, the older one reused */
static uint8_t maps[2][(MAP_BYTES + 3) & ~3] __attribute__((aligned(4)));
static int map_group[2] = {-1, -1}, map_age[2];
static int age;
static PicMap picmap[2];
static uint8_t phase_bytes[256];

static int slot_of(int group) {
  for (int s = 0; s < 2; s++)
    if (map_group[s] == group) {
      map_age[s] = ++age;
      return s;
    }
  int s = map_age[0] <= map_age[1] ? 0 : 1;
  zx0_Decompress(maps[s], asset(A.map_first + group));
  map_group[s] = group;
  map_age[s] = ++age;
  const uint8_t *m = maps[s] + 512;
  /* a group with phases has its colours four times over: the static's phase goes in the low two bits */
  bool phases = maps[s][MAP_BYTES - 1] == 4;
  if (!phase_bytes[0] && !phase_bytes[1]) {   /* (once: random bytes) */
    uint32_t r = 0x9E3779B9u;
    for (int i = 0; i < 256; i++) r ^= r << 13, r ^= r >> 17, r ^= r << 5, phase_bytes[i] = (uint8_t)(r >> 11);
  }
  picmap[s] = (PicMap){m, m + (YQ_HI - YQ_LO), m + (YQ_HI - YQ_LO) + (CQ_HI - CQ_LO),
                       m + (YQ_HI - YQ_LO) + 2 * (CQ_HI - CQ_LO), phases ? phase_bytes : NULL};
  return s;
}

const uint8_t *map_of(int group) { return maps[slot_of(group)]; }
const uint16_t *map_palette(int group) { return (const uint16_t *)(const void *)maps[slot_of(group)]; }
const uint8_t *map_remap(int group, int which) { return maps[slot_of(group)] + MAP_BYTES - 513 + 256 * which; }

/* ------------------------------------------------------------------ pictures */
int pic_group_of(int pic) { return A.pic_group[pic]; }
int pic_w(int pic) { return pic_width(asset(A.pic_first + pic)); }
int pic_h(int pic) { return pic_height(asset(A.pic_first + pic)); }

void pic_into(int pic, uint8_t *dst, int stride) {
  int s = slot_of(A.pic_group[pic]);
  pic_decode(asset(A.pic_first + pic), dst, stride, &picmap[s]);
}

void pic_full(int pic, uint8_t *dst, int stride) {
  int base = pic_base(asset(A.pic_first + pic));
  if (base >= 0) pic_full(base, dst, stride);
  pic_into(pic, dst, stride);
}

/* ------------------------------------------------------------------ drawing */
static bool clip(int *x, int *y, int *w, int *h, int *sx, int *sy) {
  if (*x < 0) *sx -= *x, *w += *x, *x = 0;
  if (*y < 0) *sy -= *y, *h += *y, *y = 0;
  if (*x + *w > SCR_W) *w = SCR_W - *x;
  if (*y + *h > SCR_H) *h = SCR_H - *y;
  return *w > 0 && *h > 0;
}

void d_fill(int x, int y, int w, int h, uint8_t c) {
  int sx = 0, sy = 0;
  if (!clip(&x, &y, &w, &h, &sx, &sy)) return;
  for (uint8_t *p = scr + y * SCR_W + x; h--; p += SCR_W) memset(p, c, (size_t)w);
}

void d_frame(int x, int y, int w, int h, uint8_t c) {
  d_fill(x, y, w, 1, c);
  d_fill(x, y + h - 1, w, 1, c);
  d_fill(x, y, 1, h, c);
  d_fill(x + w - 1, y, 1, h, c);
}

void d_remap(int x, int y, int w, int h, const uint8_t *lut) {
  int sx = 0, sy = 0;
  if (!clip(&x, &y, &w, &h, &sx, &sy)) return;
  for (uint8_t *row = scr + y * SCR_W + x; h--; row += SCR_W)
    for (uint8_t *p = row, *e = row + w; p < e; p++) *p = lut[*p];
}

void d_blit(const uint8_t *src, int stride, int sx, int sy, int w, int h, int x, int y, bool dbl) {
  if (!dbl) {
    if (!clip(&x, &y, &w, &h, &sx, &sy)) return;
    const uint8_t *s = src + sy * stride + sx;
    for (uint8_t *p = scr + y * SCR_W + x; h--; p += SCR_W, s += stride) memcpy(p, s, (size_t)w);
    return;
  }
  /* rows doubled: source row r at screen rows y + 2r and y + 2r + 1 */
  if (x < 0) sx -= x, w += x, x = 0;
  if (x + w > SCR_W) w = SCR_W - x;
  if (w <= 0) return;
  for (int r = 0; r < h; r++) {
    int yy = y + 2 * r;
    if (yy + 1 < 0 || yy >= SCR_H) continue;
    const uint8_t *s = src + (sy + r) * stride + sx;
    if (yy >= 0) memcpy(scr + yy * SCR_W + x, s, (size_t)w);
    if (yy + 1 < SCR_H) memcpy(scr + (yy + 1) * SCR_W + x, s, (size_t)w);
  }
}

void d_blit_key(const uint8_t *src, int stride, int w, int h, int x, int y, uint8_t key) {
  int sx = 0, sy = 0;
  if (!clip(&x, &y, &w, &h, &sx, &sy)) return;
  const uint8_t *s = src + sy * stride + sx;
  for (uint8_t *p = scr + y * SCR_W + x; h--; p += SCR_W, s += stride)
    for (int i = 0; i < w; i++)
      if (s[i] != key) p[i] = s[i];
}

void d_blit2(const uint8_t *src, int stride, int dx, uint8_t fill) {
  int x0 = dx > 0 ? dx : 0, x1 = dx < 0 ? SCR_W + dx : SCR_W;
  for (int r = 0; r < SCR_H / 2; r++) {
    uint8_t *d = scr + 2 * r * SCR_W;
    const uint8_t *s = src + r * stride;
    if (dx > 0) memset(d, fill, (size_t)dx);
    if (dx < 0) memset(d + x1, fill, (size_t)-dx);
    uint8_t *o = d + x0;
    const uint8_t *i = s + ((x0 - dx) >> 1);
    int n = x1 - x0;
    if ((x0 - dx) & 1) *o++ = *i++, n--;
    for (; n >= 2; n -= 2, i++) o[0] = o[1] = *i, o += 2;
    if (n) *o = *i;
    memcpy(d + SCR_W, d, SCR_W);
  }
}

void d_noise(int x, int y, int w, int h, uint32_t seed, const uint8_t *greys) {
  int sx = 0, sy = 0;
  if (!clip(&x, &y, &w, &h, &sx, &sy)) return;
  uint32_t r = seed | 1;
  for (uint8_t *row = scr + y * SCR_W + x; h--; row += SCR_W) {
    uint8_t *p = row, *e = row + w;
    while (p < e) {
      r ^= r << 13, r ^= r >> 17, r ^= r << 5;
      uint32_t b = r;
      for (int k = 0; k < 8 && p < e; k++, b >>= 3) *p++ = greys[b & 7];
    }
  }
}

/* ------------------------------------------------------------------ text */
static const uint8_t *font_at(int font) { return asset(A.font_first + font); }
int d_line_h(int font) { return font_at(font)[0]; }

static void glyph(const uint8_t *f, const uint8_t *gl, int x, int y, Ink ink) {
  int w = gl[1], h = gl[2];
  if (!w) return;
  const uint8_t *rows = f + (gl[6] | gl[7] << 8);
  int stride = (w + 3) >> 2;
  x += (int8_t)gl[3], y += (int8_t)gl[4];
  for (int r = 0; r < h; r++, rows += stride) {
    int yy = y + r;
    if (yy < 0 || yy >= SCR_H) continue;
    uint8_t *d = scr + yy * SCR_W;
    for (int c = 0; c < w; c++) {
      int a = rows[c >> 2] >> (6 - 2 * (c & 3)) & 3, xx = x + c;
      if (a && xx >= 0 && xx < SCR_W) d[xx] = ink[a];
    }
  }
}

int d_text_n(int font, int x, int y, const char *s, int n, Ink ink) {
  const uint8_t *f = font_at(font);
  int x0 = x;
  for (; *s && n-- > 0; s++) {
    int ch = (uint8_t)*s;
    if (ch < 32 || ch > 126) ch = '?';
    const uint8_t *gl = f + 2 + 8 * (ch - 32);
    glyph(f, gl, x, y, ink);
    x += gl[0];
  }
  return x - x0;
}

int d_text(int font, int x, int y, const char *s, Ink ink) { return d_text_n(font, x, y, s, 1 << 20, ink); }

int d_text_wn(int font, const char *s, int n) {
  const uint8_t *f = font_at(font);
  int w = 0;
  for (; *s && n-- > 0; s++) {
    int ch = (uint8_t)*s;
    if (ch < 32 || ch > 126) ch = '?';
    w += f[2 + 8 * (ch - 32)];
  }
  return w;
}
int d_text_w(int font, const char *s) { return d_text_wn(font, s, 1 << 20); }
void d_text_c(int font, int cx, int y, const char *s, Ink ink) { d_text(font, cx - d_text_w(font, s) / 2, y, s, ink); }
void d_text_r(int font, int rx, int y, const char *s, Ink ink) { d_text(font, rx - d_text_w(font, s), y, s, ink); }

void d_img(const uint8_t *img, int x, int y, Ink ink) {
  int w = img[0] | img[1] << 8, h = img[2] | img[3] << 8, stride = (w + 3) >> 2;
  const uint8_t *rows = img + 4;
  for (int r = 0; r < h; r++, rows += stride) {
    int yy = y + r;
    if (yy < 0 || yy >= SCR_H) continue;
    uint8_t *d = scr + yy * SCR_W;
    for (int c = 0; c < w; c++) {
      int a = rows[c >> 2] >> (6 - 2 * (c & 3)) & 3, xx = x + c;
      if (a && xx >= 0 && xx < SCR_W) d[xx] = ink[a];
    }
  }
}

/* ------------------------------------------------------------------ the palette */
static uint16_t mix(uint16_t c, int r2, int g2, int b2, int a) {   /* c toward (r2, g2, b2) by a / 32 */
  int r = c >> 10 & 31, g = (c >> 5 & 31) << 1 | c >> 15, b = c & 31;
  r += (r2 - r) * a / 32, g += (g2 - g) * a / 32, b += (b2 - b) * a / 32;
  return (uint16_t)((g & 1) << 15 | r << 10 | (g >> 1) << 5 | b);
}

static uint16_t scale(uint16_t c, int k) {   /* each channel times k / 32 */
  int r = (c >> 10 & 31) * k / 32, g = ((c >> 5 & 31) << 1 | c >> 15) * k / 32, b = (c & 31) * k / 32;
  r = r > 31 ? 31 : r, g = g > 63 ? 63 : g, b = b > 31 ? 31 : b;
  return (uint16_t)((g & 1) << 15 | r << 10 | (g >> 1) << 5 | b);
}

static struct {
  int group, dim, white;
} last = {-2, 0, 0};

void pal_set(int group, int dim, const uint8_t *phase, int white) {
  if (!phase && group == last.group && dim == last.dim && white == last.white) return;
  last.group = group, last.dim = dim, last.white = white;
  if (phase) last.group = -2;
  if (group < 0) {
    memset(ce_pal, 0, 2 * PICS_COLORS);
    if (map_group[0] < 0 && map_group[1] < 0) slot_of(0);
    memcpy(ce_pal + PICS_COLORS, maps[map_group[0] >= 0 ? 0 : 1] + 2 * PICS_COLORS, 2 * (256 - PICS_COLORS));
    ce_pal_dirty = true;
    return;
  }
  const uint16_t *p = map_palette(group);
  for (int i = 0; i < PICS_COLORS; i++) {
    uint16_t c = p[i];
    if (phase && phase[i & 3]) {   /* the static: each phase toward its grey */
      int k = phase[4 + (i & 3)];
      c = mix(c, k, 2 * k, k, phase[i & 3]);
    }
    if (white) c = mix(c, 31, 63, 31, white);
    if (dim != 32) c = scale(c, dim);
    ce_pal[i] = c;
  }
  memcpy(ce_pal + PICS_COLORS, p + PICS_COLORS, 2 * (256 - PICS_COLORS));
  ce_pal_dirty = true;
}

void pal_ui(int dim) {
  for (int i = PICS_COLORS; i < 256; i++) ce_pal[i] = scale(ce_pal[i], dim);
  last.group = -2;
  ce_pal_dirty = true;
}
