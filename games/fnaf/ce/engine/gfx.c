/* Drawing on the CE's screen (gfx.h). */
#include "gfx.h"
#include <string.h>

void zx0_Decompress(void *dst, const void *src);
void ce_double(const uint8_t *src, uint8_t *dst, int n);   /* (draw_ez80.s, or below) */
void ce_remap(uint8_t *p, int n, const uint8_t *lut);
void ce_alpha(const uint8_t *rows, int stride, int h, uint8_t *dst, const uint8_t *ink);

#ifndef CE_ASM
void ce_double(const uint8_t *src, uint8_t *dst, int n) {
  while (n--) dst[0] = dst[1] = *src++, dst += 2;
}
void ce_remap(uint8_t *p, int n, const uint8_t *lut) {
  while (n--) *p = lut[*p], p++;
}
void ce_alpha(const uint8_t *rows, int stride, int h, uint8_t *dst, const uint8_t *ink) {
  for (; h; h--, dst += SCR_W)
    for (int n = 0; n < stride; n++) {
      uint8_t b = *rows++;
      for (int k = 0; k < 4; k++, b = (uint8_t)(b << 2))
        if (b >> 6) dst[4 * n + k] = ink[b >> 6];
    }
}
#endif

static Assets A;
static uint8_t *temp;
static size_t temp_len;
int gfx_temp_uses;

void gfx_init(const Assets *a, uint8_t *t, size_t len) {
  A = *a;
  temp = t, temp_len = len;
}

const uint8_t *asset(int id) {
  uint32_t at = A.at[id];
  return A.vars[at >> 16] + (at & 0xFFFF);
}

/* ------------------------------------------------------------------ maps: two places, the older one reused */
static uint8_t maps[2][PIC_MAP_SIZE] __attribute__((aligned(1024)));
static int map_group[2] = {-1, -1}, map_age[2];
static int age;

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
  return s;
}

const uint8_t *map_of(int group) { return maps[slot_of(group)]; }
const uint16_t *map_palette(int group) { return (const uint16_t *)(const void *)(maps[slot_of(group)] + MAP_PALETTE); }
const uint8_t *map_remap(int group, int which) { return maps[slot_of(group)] + MAP_REMAP + 256 * which; }

/* ------------------------------------------------------------------ pictures */
int pic_group_of(int pic) { return A.pic_group[pic]; }
int pic_w(int pic) { return pic_width(asset(A.pic_first + pic)); }
int pic_h(int pic) { return pic_height(asset(A.pic_first + pic)); }

void pic_into(int pic, uint8_t *dst, int stride) {
  const uint8_t *p = asset(A.pic_first + pic);
  bool in_place = !pic_keeps(p) && stride == pic_width(p);
  if (!in_place) gfx_temp_uses++;
  pic_decode(p, dst, stride, maps[slot_of(A.pic_group[pic])], in_place ? NULL : temp);
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
  for (uint8_t *row = scr + y * SCR_W + x; h--; row += SCR_W) ce_remap(row, w, lut);
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
  static uint8_t row[SCR_W];
  for (int r = 0; r < SCR_H / 2; r++) {
    uint8_t *d = scr + 2 * r * SCR_W;
    const uint8_t *s = src + r * stride;
    if (!dx) ce_double(s, d, SCR_W / 2);
    else {   /* (shaken: dx to the side) */
      ce_double(s, row, SCR_W / 2);
      if (dx > 0) memset(d, fill, (size_t)dx), memcpy(d + dx, row, (size_t)(SCR_W - dx));
      else memcpy(d, row - dx, (size_t)(SCR_W + dx)), memset(d + SCR_W + dx, fill, (size_t)-dx);
    }
    memcpy(d + SCR_W, d, SCR_W);
  }
}

void d_noise(int x, int y, int w, int h, uint32_t seed, const uint8_t *greys) {
  static uint8_t strip[2 * SCR_W];
  static uint8_t rnd[256];
  static const uint8_t *made;
  int sx = 0, sy = 0;
  if (!clip(&x, &y, &w, &h, &sx, &sy)) return;
  if (!rnd[0] && !rnd[1]) {   /* (once: random bytes) */
    uint32_t r = 0x2545F491u;
    for (int i = 0; i < 256; i++) r ^= r << 13, r ^= r >> 17, r ^= r << 5, rnd[i] = (uint8_t)(r >> 13);
  }
  if (made != greys) {
    for (int i = 0; i < 2 * SCR_W; i++) strip[i] = greys[rnd[(uint8_t)(i * 7 + (i >> 8))] & 7];
    made = greys;
  }
  uint8_t k = (uint8_t)seed, step = (uint8_t)(seed >> 8) | 1;
  for (uint8_t *row = scr + y * SCR_W + x; h--; row += SCR_W, k += step) memcpy(row, strip + rnd[k], (size_t)w);
}

void d_snow(int n, uint32_t seed, const uint8_t *greys) {
  static unsigned at[1024];   /* (random places on the screen, made once) */
  if (!at[0] && !at[1]) {
    uint32_t r = 0x6F4A7C15u;
    for (int i = 0; i < 1024; i++) r ^= r << 13, r ^= r >> 17, r ^= r << 5, at[i] = (unsigned)(r % (SCR_W * SCR_H));
  }
  unsigned k = (unsigned)seed & 1023, step = ((unsigned)(seed >> 10) & 510) | 1;
  uint8_t c = (uint8_t)(seed >> 20);
  while (n--) scr[at[k]] = greys[c++ & 7], k = (k + step) & 1023;
}

/* ------------------------------------------------------------------ text */
static const uint8_t *font_at(int font) { return asset(A.font_first + font); }
int d_line_h(int font) { return font_at(font)[0]; }

/* 2-bit alpha rows (4 pixels a byte, the first in the top bits) at (x, y) in an ink; clipped */
static void alpha(const uint8_t *rows, int w, int h, int x, int y, Ink ink) {
  int stride = (w + 3) >> 2;
  if (x >= 0 && y >= 0 && x + 4 * stride <= SCR_W && y + h <= SCR_H) {   /* (all of it on the screen: quick) */
    ce_alpha(rows, stride, h, scr + y * SCR_W + x, ink);
    return;
  }
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

static void glyph(const uint8_t *f, const uint8_t *gl, int x, int y, Ink ink) {
  if (gl[1]) alpha(f + (gl[6] | gl[7] << 8), gl[1], gl[2], x + (int8_t)gl[3], y + (int8_t)gl[4], ink);
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
  alpha(img + 4, img[0] | img[1] << 8, img[2] | img[3] << 8, x, y, ink);
}

/* ------------------------------------------------------------------ the palette */
/* the group shown: its colours' channels (red and blue 0..31, green 0..63); the channels as they come out, already
 * in their place in a palette word */
static uint8_t chan_r[PICS_COLORS], chan_g[PICS_COLORS], chan_b[PICS_COLORS];
static int chan_group = -1;
static uint16_t out_r[32], out_g[64], out_b[32];

/* a channel v (0..max) toward max by white / 32, then times dim / 32 */
static int chan(int v, int max, int white, int dim) {
  v = (v * (32 - white) + max * white) >> 5;
  v = (v * dim) >> 5;
  return v > max ? max : v;
}

static struct {
  int group, dim, white;
} last = {-2, 0, 0};

void pal_set(int group, int dim, int white) {
  if (group == last.group && dim == last.dim && white == last.white) return;
  last.group = group, last.dim = dim, last.white = white;
  if (group < 0) {
    memset(ce_pal, 0, 2 * PICS_COLORS);
    if (map_group[0] < 0 && map_group[1] < 0) slot_of(0);
    memcpy(ce_pal + PICS_COLORS, maps[map_group[0] >= 0 ? 0 : 1] + MAP_PALETTE + 2 * PICS_COLORS, 2 * (256 - PICS_COLORS));
    ce_pal_dirty = true;
    return;
  }
  const uint16_t *p = map_palette(group);
  if (chan_group != group) {
    for (int i = 0; i < PICS_COLORS; i++) {
      uint16_t c = p[i];
      chan_r[i] = (uint8_t)(c >> 10 & 31), chan_g[i] = (uint8_t)((c >> 5 & 31) << 1 | c >> 15);
      chan_b[i] = (uint8_t)(c & 31);
    }
    chan_group = group;
  }
  for (int v = 0; v < 32; v++) {
    int c = chan(v, 31, white, dim);
    out_r[v] = (uint16_t)(c << 10), out_b[v] = (uint16_t)c;
  }
  for (int v = 0; v < 64; v++) {
    int g = chan(v, 63, white, dim);
    out_g[v] = (uint16_t)((g & 1) << 15 | (g >> 1) << 5);
  }
  for (int i = 0; i < PICS_COLORS; i++) ce_pal[i] = out_r[chan_r[i]] | out_g[chan_g[i]] | out_b[chan_b[i]];
  memcpy(ce_pal + PICS_COLORS, p + PICS_COLORS, 2 * (256 - PICS_COLORS));
  ce_pal_dirty = true;
}

void pal_ui(int dim) {
  for (int i = PICS_COLORS; i < 256; i++) {
    uint16_t c = ce_pal[i];
    int r = chan(c >> 10 & 31, 31, 0, dim), g = chan((c >> 5 & 31) << 1 | c >> 15, 63, 0, dim);
    int b = chan(c & 31, 31, 0, dim);
    ce_pal[i] = (uint16_t)((g & 1) << 15 | r << 10 | (g >> 1) << 5 | b);
  }
  last.group = -2;
  ce_pal_dirty = true;
}
