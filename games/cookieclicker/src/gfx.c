/* Drawing (gfx.h): RGB565 bands, pictures with palettes and opacity, scaled, rotated and tiled
 * with bilinear sampling, 4-bit fonts and the game's markup. */
#include <math.h>
#include <string.h>
#include "gfx.h"
#include "res.h"

static uint16_t *band_mem;
static uint16_t *band;          /* the band: g_bw x (g_by1 - g_by0) */
static int g_bx, g_by, g_bw;    /* its top-left on screen and width */
int g_x0, g_y0, g_x1, g_y1;
#define CLIP_DEPTH 8
static int clip_save[CLIP_DEPTH][4], clip_depth;
static int blend_mode;

void gfx_set_band_memory(uint16_t *m) { band_mem = m; }

void gfx_render(int x, int y, int w, int h, void (*draw)(void)) {
  if (x < 0) w += x, x = 0;
  if (y < 0) h += y, y = 0;
  if (x + w > SCREEN_W) w = SCREEN_W - x;
  if (y + h > SCREEN_H) h = SCREEN_H - y;
  if (w <= 0 || h <= 0) return;
  int rows = SCREEN_W * GFX_BAND / w;
  if (rows > h) rows = h;
  for (int by = y; by < y + h; by += rows) {
    int bh = by + rows > y + h ? y + h - by : rows;
    band = band_mem;
    g_bx = x, g_by = by, g_bw = w;
    g_x0 = x, g_x1 = x + w, g_y0 = by, g_y1 = by + bh;
    blend_mode = BLEND_NORMAL;
    clip_depth = 0;
    draw();
    plat_push(x, by, w, bh, band);
  }
}

void gfx_clip(int x, int y, int w, int h) {
  int *c = clip_save[clip_depth < CLIP_DEPTH ? clip_depth++ : CLIP_DEPTH - 1];
  c[0] = g_x0, c[1] = g_y0, c[2] = g_x1, c[3] = g_y1;
  if (x > g_x0) g_x0 = x;
  if (y > g_y0) g_y0 = y;
  if (x + w < g_x1) g_x1 = x + w;
  if (y + h < g_y1) g_y1 = y + h;
  if (g_x1 < g_x0) g_x1 = g_x0;
  if (g_y1 < g_y0) g_y1 = g_y0;
}
void gfx_unclip(void) {
  if (clip_depth > 0) clip_depth--;
  int *c = clip_save[clip_depth];
  g_x0 = c[0], g_y0 = c[1], g_x1 = c[2], g_y1 = c[3];
}
void gfx_blend(int mode) { blend_mode = mode; }

static inline uint16_t *at(int x, int y) { return band + (y - g_by) * g_bw + (x - g_bx); }

/* a over d with opacity a (0..256) */
static inline uint16_t blend(uint16_t d, uint16_t s, int a) {
  if (a >= 256) return s;
  uint32_t dd = (d | (uint32_t)d << 16) & 0x07E0F81Fu;
  uint32_t ss = (s | (uint32_t)s << 16) & 0x07E0F81Fu;
  uint32_t r = ((((ss - dd) * (uint32_t)(a >> 3)) >> 5) + dd) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}
static inline uint16_t add(uint16_t d, uint16_t s, int a) {
  int r = (d >> 11) + (((s >> 11) * a) >> 8), g = ((d >> 5) & 63) + ((((s >> 5) & 63) * a) >> 8);
  int b = (d & 31) + (((s & 31) * a) >> 8);
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  return (uint16_t)(r << 11 | g << 5 | b);
}
static inline void put(uint16_t *p, uint16_t c, int a) {
  if (a <= 0) return;
  *p = blend_mode == BLEND_ADD ? add(*p, c, a) : blend(*p, c, a);
}
uint16_t gfx_mix(uint16_t a, uint16_t b, int t) { return blend(a, b, t); }

/* ------------------------------------------------------------------ shapes */
static bool clipr(int *x, int *y, int *w, int *h) {
  int x0 = *x < g_x0 ? g_x0 : *x, y0 = *y < g_y0 ? g_y0 : *y;
  int x1 = *x + *w > g_x1 ? g_x1 : *x + *w, y1 = *y + *h > g_y1 ? g_y1 : *y + *h;
  if (x1 <= x0 || y1 <= y0) return false;
  *x = x0, *y = y0, *w = x1 - x0, *h = y1 - y0;
  return true;
}
void gfx_fill(int x, int y, int w, int h, uint16_t c) {
  if (!clipr(&x, &y, &w, &h)) return;
  for (int r = 0; r < h; r++) {
    uint16_t *p = at(x, y + r);
    for (int i = 0; i < w; i++) p[i] = c;
  }
}
void gfx_fill_alpha(int x, int y, int w, int h, uint16_t c, int alpha) {
  if (alpha >= 256 && blend_mode == BLEND_NORMAL) {
    gfx_fill(x, y, w, h, c);
    return;
  }
  if (alpha <= 0 || !clipr(&x, &y, &w, &h)) return;
  for (int r = 0; r < h; r++) {
    uint16_t *p = at(x, y + r);
    for (int i = 0; i < w; i++) put(p + i, c, alpha);
  }
}
void gfx_hgrad(int x, int y, int w, int h, uint16_t c, int a0, int a1) {
  int ox = x, ow = w;
  if (ow <= 0 || !clipr(&x, &y, &w, &h)) return;
  for (int i = 0; i < w; i++) {
    int a = a0 + (a1 - a0) * (x + i - ox) / (ow > 1 ? ow - 1 : 1);
    for (int r = 0; r < h; r++) put(at(x + i, y + r), c, a);
  }
}
void gfx_vgrad(int x, int y, int w, int h, uint16_t c, int a0, int a1) {
  int oy = y, oh = h;
  if (oh <= 0 || !clipr(&x, &y, &w, &h)) return;
  for (int r = 0; r < h; r++) {
    int a = a0 + (a1 - a0) * (y + r - oy) / (oh > 1 ? oh - 1 : 1);
    uint16_t *p = at(x, y + r);
    for (int i = 0; i < w; i++) put(p + i, c, a);
  }
}
void gfx_rect(int x, int y, int w, int h, uint16_t c, int alpha) {
  gfx_fill_alpha(x, y, w, 1, c, alpha);
  gfx_fill_alpha(x, y + h - 1, w, 1, c, alpha);
  gfx_fill_alpha(x, y + 1, 1, h - 2, c, alpha);
  gfx_fill_alpha(x + w - 1, y + 1, 1, h - 2, c, alpha);
}
void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t c, int alpha) {
  if (!gfx_visible(x, y, w, h)) return;
  for (int row = 0; row < h; row++) {
    int yy = y + row;
    if (yy < g_y0 || yy >= g_y1) continue;
    int d = 0;
    if (row < r || row >= h - r) {
      float dy = row < r ? r - row - 0.5f : row - (h - r) + 0.5f;
      float dx = r - sqrtf((float)(r * r) - dy * dy);
      d = (int)(dx + 0.5f);
    }
    gfx_fill_alpha(x + d, yy, w - 2 * d, 1, c, alpha);
  }
}
void gfx_line(float x0, float y0, float x1, float y1, uint16_t c, int alpha) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)(fabsf(dx) > fabsf(dy) ? fabsf(dx) : fabsf(dy)) + 1;
  for (int i = 0; i <= n; i++) {
    int x = (int)(x0 + dx * i / n + 0.5f), y = (int)(y0 + dy * i / n + 0.5f);
    if (x >= g_x0 && x < g_x1 && y >= g_y0 && y < g_y1) put(at(x, y), c, alpha);
  }
}
void gfx_disc(float cx, float cy, float r, uint16_t c, int alpha) {
  int y0 = (int)floorf(cy - r), y1 = (int)ceilf(cy + r);
  if (y0 < g_y0) y0 = g_y0;
  if (y1 > g_y1) y1 = g_y1;
  for (int y = y0; y < y1; y++) {
    float dy = y + 0.5f - cy;
    float h2 = r * r - dy * dy;
    if (h2 <= 0) continue;
    float hw = sqrtf(h2);
    float xa = cx - hw, xb = cx + hw;
    int x0 = (int)floorf(xa), x1 = (int)ceilf(xb);
    if (x0 < g_x0) x0 = g_x0;
    if (x1 > g_x1) x1 = g_x1;
    for (int x = x0; x < x1; x++) {
      float cov = 1;
      if (x < xa) cov -= xa - x;
      if (x + 1 > xb) cov -= x + 1 - xb;
      if (cov > 0) put(at(x, y), c, (int)(alpha * cov));
    }
  }
}

/* ------------------------------------------------------------------ pictures */
typedef struct {
  const uint8_t *px;
  const uint16_t *col;
  const uint8_t *opa;
  int w, h;
  int x, y, cw, ch, bits, stride;   /* the part kept: outside it, clear */
} Img;

static const uint8_t *unpacked(int im);
static Img img(int im) {
  const ImageRec *r = image_rec(im);
  Img m;
  m.w = r->w, m.h = r->h, m.x = r->x, m.y = r->y, m.cw = r->cw, m.ch = r->ch, m.bits = r->bits & 15;
  m.stride = (m.cw * m.bits + 7) >> 3;
  m.px = r->bits & IM_PACKED ? unpacked(im) : cc_data + r->px;
  m.col = (const uint16_t *)(const void *)(cc_data + r->pal);
  m.opa = cc_data + r->pal + 2 * (r->ncolors_1 + 1);
  return m;
}
int gfx_image_w(int im) { return image_rec(im)->w; }
int gfx_image_h(int im) { return image_rec(im)->h; }

/* the palette index of pixel (x, y) of the picture, -1 if it is outside the part kept (clear) */
static inline int index_at(const Img *m, int x, int y) {
  x -= m->x, y -= m->y;
  if ((unsigned)x >= (unsigned)m->cw || (unsigned)y >= (unsigned)m->ch) return -1;
  if (m->bits == FMT_P8) return m->px[y * m->stride + x];
  if (m->bits == FMT_P0) return 0;
  uint8_t b = m->px[y * m->stride + (x >> 1)];
  return x & 1 ? b >> 4 : b & 15;
}

/* the colour and opacity (0..255) of pixel (x, y) */
static inline uint16_t texel(const Img *m, int x, int y, int *a) {
  int i = index_at(m, x, y);
  if (i < 0) {
    *a = 0;
    return 0;
  }
  *a = m->opa[i];
  return m->col[i];
}

/* ------------------------------------------------------------------ packed pictures */
/* Most pictures are packed (tools/pictures.py: its model() is unpack()'s): when one is drawn, it is unpacked into
 * the cache, which keeps the ones drawn latest (40 KB holds what any screen of the game draws). */
#define CACHE_BYTES 40960
#define CACHE_SLOTS 254
static uint8_t cache[CACHE_BYTES];
static struct {
  uint16_t im, len;
  uint32_t off, used;
} slot[CACHE_SLOTS];               /* in the order they lie in the cache */
static int nslots;
static uint32_t cache_end, cache_used, cache_clock;
static uint8_t where[N_IMAGES];    /* a picture's slot + 1, 0 if it isn't in the cache */

static void cache_drop(int s) {
  where[slot[s].im] = 0;
  cache_used -= slot[s].len;
  for (int t = s; t + 1 < nslots; t++) slot[t] = slot[t + 1], where[slot[t].im] = (uint8_t)(t + 1);
  nslots--;
}
/* room for len bytes at the cache's end, the pictures drawn longest ago dropped (but keep), the others moved
 * together if need be */
static uint8_t *cache_room(int im, uint32_t len, int keep) {
  while (cache_used + len > CACHE_BYTES || nslots == CACHE_SLOTS) {
    int old = -1;
    for (int s = 0; s < nslots; s++)
      if (slot[s].im != keep && (old < 0 || slot[s].used < slot[old].used)) old = s;
    cache_drop(old);
  }
  if (cache_end + len > CACHE_BYTES) {
    cache_end = 0;
    for (int s = 0; s < nslots; s++) {
      if (slot[s].off != cache_end) memmove(cache + cache_end, cache + slot[s].off, slot[s].len);
      slot[s].off = cache_end, cache_end += slot[s].len;
    }
  }
  slot[nslots].im = (uint16_t)im, slot[nslots].len = (uint16_t)len, slot[nslots].off = cache_end;
  slot[nslots].used = ++cache_clock;
  where[im] = (uint8_t)++nslots;
  cache_used += len;
  cache_end += len;
  return cache + cache_end - len;
}

/* LZMA's range decoder, chances of a 0 out of 2048 */
typedef struct {
  const uint8_t *in;
  uint32_t range, code;
} Rc;
static inline int rc_bit(Rc *rc, uint16_t *p) {
  uint32_t bound = (rc->range >> 11) * *p;
  int b;
  if (rc->code < bound) rc->range = bound, *p += (2048 - *p) >> 4, b = 0;
  else rc->code -= bound, rc->range -= bound, *p -= *p >> 4, b = 1;
  if (rc->range < 1u << 24) rc->range <<= 8, rc->code = rc->code << 8 | *rc->in++;
  return b;
}

/* the contexts (pictures.py's) */
enum { GUESS = 0, NEW = 400, UNARY = 405, FIRST = 413, OTHER = 422, CONTEXTS = 423 };

/* picture r's pixels, from its packed data in, into out (as a raw picture's); ref: its reference (map: its colours
 * to this one's, the last for where it is clear outside its part), or NULL */
static void unpack(const ImageRec *r, const uint8_t *in, const Img *ref, const uint8_t *map, int nref, uint8_t *out) {
  int cw = r->cw, ch = r->ch, n = r->ncolors_1 + 1, bits = r->bits & 15, stride = (cw * bits + 7) >> 3;
  uint16_t p[CONTEXTS];
  uint8_t rows[2][256], mtf[256], cands[5];
  memcpy(p, cc_data + OFF_PRIORS, sizeof p);
  for (int i = 0; i < n; i++) mtf[i] = (uint8_t)i;
  int seen = 0;
  Rc rc = {in + 4, 0xFFFFFFFFu, (uint32_t)in[0] << 24 | (uint32_t)in[1] << 16 | (uint32_t)in[2] << 8 | in[3]};
  for (int y = 0; y < ch; y++) {
    uint8_t *row = rows[y & 1], *up = rows[(y & 1) ^ 1], *o = out + y * stride;
    for (int x = 0; x < cw; x++) {
      int L = x ? row[x - 1] : -1, U = y ? up[x] : -1;
      int UL = x && y ? up[x - 1] : -1, UR = y && x + 1 < cw ? up[x + 1] : -1;
      int shape = (L >= 0 && L == U) | (U >= 0 && U == UL) << 1 | (L >= 0 && L == UL) << 2 | (U >= 0 && U == UR) << 3;
      int g[5] = {-1, L, U, UL, UR}, j = 0, got = -1;
      if (ref) {
        int v = index_at(ref, r->x + x, r->y + y), m = map[v < 0 ? nref : v];
        g[0] = m == 255 ? -1 : m;
      }
      for (int s = 0; s < 5 && got < 0; s++) {   /* (the neighbours, each once: is it this one?) */
        int c = g[s], k = 0;
        if (c < 0) continue;
        while (k < j && cands[k] != c) k++;
        if (k < j) continue;
        if (!rc_bit(&rc, &p[GUESS + (s * 5 + j) * 16 + shape])) got = c;
        else cands[j++] = (uint8_t)c;
      }
      if (got < 0) {
        if (seen < n && !rc_bit(&rc, &p[NEW + (j < 4 ? j : 4)])) got = seen++;
        else {   /* (a colour seen before: its rank among the others, the latest first) */
          int k = 0, q = 1;
          while (k < 8 && rc_bit(&rc, &p[UNARY + k])) k++;
          for (int t = k - 1; t >= 0; t--) q = q << 1 | rc_bit(&rc, &p[t == k - 1 ? FIRST + k : OTHER]);
          for (int i = 0; got < 0; i++) {
            int c = mtf[i], kk = 0;
            while (kk < j && cands[kk] != c) kk++;
            if (kk == j && --q == 0) got = c;
          }
        }
      }
      row[x] = (uint8_t)got;
      if (mtf[0] != got) {
        int i = 1;
        while (mtf[i] != got) i++;
        memmove(mtf + 1, mtf, (size_t)i);
        mtf[0] = (uint8_t)got;
      }
      if (bits == 8) o[x] = (uint8_t)got;
      else if (x & 1) o[x >> 1] |= (uint8_t)(got << 4);
      else o[x >> 1] = (uint8_t)got;
    }
  }
}

/* a packed picture's pixels, unpacked (its reference first) */
static const uint8_t *unpacked(int im) {
  if (where[im]) {
    slot[where[im] - 1].used = ++cache_clock;
    return cache + slot[where[im] - 1].off;
  }
  const ImageRec *r = image_rec(im);
  const uint8_t *in = cc_data + r->px;
  int ref = -1;
  if (r->bits & IM_REF) ref = in[0] | in[1] << 8, unpacked(ref);
  uint8_t *out = cache_room(im, (uint32_t)(((r->cw * (r->bits & 15) + 7) >> 3) * r->ch), ref);
  if (ref < 0) {
    unpack(r, in, NULL, NULL, 0, out);
  } else {
    Img m = img(ref);   /* (where it is now) */
    int nref = image_rec(ref)->ncolors_1 + 1;
    unpack(r, in + 3 + nref, &m, in + 2, nref, out);
  }
  return out;
}

/* a row of n pixels of the picture from (sx, sy), on p: each format its own loop (most of what is drawn) */
static void row(const Img *m, int sx, int sy, int n, uint16_t *p, int alpha) {
  sx -= m->x, sy -= m->y;
  if ((unsigned)sy >= (unsigned)m->ch) return;
  if (sx < 0) p -= sx, n += sx, sx = 0;
  if (sx + n > m->cw) n = m->cw - sx;
  if (n <= 0) return;
  bool plain = alpha >= 256 && blend_mode == BLEND_NORMAL;
  const uint8_t *src = m->px + sy * m->stride;
  if (m->bits == FMT_P8) {
    src += sx;
    for (int i = 0; i < n; i++) {
      int k = src[i], a = m->opa[k];
      if (!a) continue;
      if (a == 255 && plain) p[i] = m->col[k];
      else put(p + i, m->col[k], (a + (a >> 7)) * alpha >> 8);
    }
  } else if (m->bits == FMT_P4) {
    for (int i = 0; i < n; i++) {
      int x = sx + i, k = x & 1 ? src[x >> 1] >> 4 : src[x >> 1] & 15, a = m->opa[k];
      if (!a) continue;
      if (a == 255 && plain) p[i] = m->col[k];
      else put(p + i, m->col[k], (a + (a >> 7)) * alpha >> 8);
    }
  } else {   /* (one colour) */
    int a = m->opa[0];
    uint16_t c = m->col[0];
    if (!a) return;
    if (a == 255 && plain)
      for (int i = 0; i < n; i++) p[i] = c;
    else
      for (int i = 0; i < n; i++) put(p + i, c, (a + (a >> 7)) * alpha >> 8);
  }
}

void gfx_image_part(int im, int sx, int sy, int sw, int sh, int x, int y, int alpha) {
  Img m = img(im);
  int dx = x, dy = y, w = sw, h = sh;
  if (alpha <= 0 || !clipr(&dx, &dy, &w, &h)) return;
  sx += dx - x, sy += dy - y;
  for (int r = 0; r < h; r++) row(&m, sx, sy + r, w, at(dx, dy + r), alpha);
}
void gfx_image(int im, int x, int y, int alpha) { gfx_image_part(im, 0, 0, gfx_image_w(im), gfx_image_h(im), x, y, alpha); }

void gfx_image_dark(int im, int x, int y, int alpha) {
  Img m = img(im);
  int dx = x, dy = y, w = m.w, h = m.h;
  if (alpha <= 0 || !clipr(&dx, &dy, &w, &h)) return;
  for (int r = 0; r < h; r++) {
    uint16_t *p = at(dx, dy + r);
    for (int i = 0; i < w; i++) {
      int a;
      texel(&m, dx - x + i, dy - y + r, &a);
      if (a) put(p + i, BLACK, (a + (a >> 7)) * alpha >> 8);
    }
  }
}

/* the colour at (u, v), in 1/256ths of a pixel of the picture, between its four nearest pixels;
 * opacity 0..255. wrap: tiled, else clear outside. (Flat areas and opaque ones take shortcuts: this
 * runs for most pixels drawn.) */
static uint16_t bilinear(const Img *m, int u, int v, bool wrap, int *a_out) {
  u -= 128, v -= 128;
  int x0 = u >> 8, y0 = v >> 8, fx = (u & 255) >> 4, fy = (v & 255) >> 4;
  int x1 = x0 + 1, y1 = y0 + 1;
  if (wrap) {
    if ((unsigned)x0 >= (unsigned)m->w) x0 %= m->w, x0 += x0 < 0 ? m->w : 0;
    if ((unsigned)y0 >= (unsigned)m->h) y0 %= m->h, y0 += y0 < 0 ? m->h : 0;
    x1 = x0 + 1 == m->w ? 0 : x0 + 1, y1 = y0 + 1 == m->h ? 0 : y0 + 1;
  }
  uint16_t c[4];
  int a[4];
  int i0 = index_at(m, x0, y0), i1 = index_at(m, x1, y0), i2 = index_at(m, x0, y1), i3 = index_at(m, x1, y1);
  if (i0 == i1 && i0 == i2 && i0 == i3) {   /* (a flat area) */
    *a_out = i0 < 0 ? 0 : m->opa[i0];
    return i0 < 0 ? 0 : m->col[i0];
  }
  c[0] = i0 < 0 ? 0 : m->col[i0], a[0] = i0 < 0 ? 0 : m->opa[i0];
  c[1] = i1 < 0 ? 0 : m->col[i1], a[1] = i1 < 0 ? 0 : m->opa[i1];
  c[2] = i2 < 0 ? 0 : m->col[i2], a[2] = i2 < 0 ? 0 : m->opa[i2];
  c[3] = i3 < 0 ? 0 : m->col[i3], a[3] = i3 < 0 ? 0 : m->opa[i3];
  int w[4] = {(16 - fx) * (16 - fy), fx * (16 - fy), (16 - fx) * fy, fx * fy};   /* (sum 256) */
  if ((a[0] & a[1] & a[2] & a[3]) == 255) {   /* (opaque: no weighting by opacity) */
    uint32_t R = 0, G = 0, B = 0;
    for (int k = 0; k < 4; k++) R += (c[k] >> 11) * w[k], G += ((c[k] >> 5) & 63) * w[k], B += (c[k] & 31) * w[k];
    *a_out = 255;
    return (uint16_t)((R >> 8) << 11 | (G >> 8) << 5 | (B >> 8));
  }
  uint32_t R = 0, G = 0, B = 0, A = 0;
  for (int k = 0; k < 4; k++) {
    uint32_t wa = (uint32_t)(w[k] * a[k]);   /* 0..65280 */
    R += (c[k] >> 11) * wa, G += ((c[k] >> 5) & 63) * wa, B += (c[k] & 31) * wa, A += wa;
  }
  *a_out = (int)(A >> 8);
  if (!A) return 0;
  return (uint16_t)((R / A) << 11 | (G / A) << 5 | (B / A));
}

/* scaled: the picture's pixels in steps of su, sv (1/256ths of its pixels per screen pixel); nearest
 * picks the nearest pixel (smooth overlays), lo samples every other pixel in both directions (soft
 * pictures: the flares) */
static void scaled(int im, float x, float y, float w, float h, int alpha, bool nearest) {
  if (w < 0.5f || h < 0.5f || alpha <= 0) return;
  int x0 = (int)floorf(x), y0 = (int)floorf(y), x1 = (int)ceilf(x + w), y1 = (int)ceilf(y + h);
  if (x0 < g_x0) x0 = g_x0;
  if (y0 < g_y0) y0 = g_y0;
  if (x1 > g_x1) x1 = g_x1;
  if (y1 > g_y1) y1 = g_y1;
  if (x1 <= x0 || y1 <= y0) return;
  Img m = img(im);
  int su = (int)(m.w / w * 256);
  int u0 = (int)((x0 + 0.5f - x) * m.w / w * 256);
  for (int py = y0; py < y1; py++) {
    int v = (int)((py + 0.5f - y) * m.h / h * 256);
    uint16_t *p = at(x0, py);
    int u = u0;
    for (int px = x0; px < x1; px++, p++, u += su) {
      int a;
      uint16_t c;
      if (nearest) {
        int tx = u >> 8, ty = v >> 8;
        if ((unsigned)tx >= (unsigned)m.w || (unsigned)ty >= (unsigned)m.h) continue;
        c = texel(&m, tx, ty, &a);
      } else
        c = bilinear(&m, u, v, false, &a);
      if (a) put(p, c, (a + (a >> 7)) * alpha >> 8);
    }
  }
}

void gfx_image_scaled(int im, float x, float y, float w, float h, int alpha) {
  const ImageRec *r = image_rec(im);
  if (fabsf(w - r->w) < 0.05f && fabsf(h - r->h) < 0.05f) {   /* (its own size: as it is) */
    gfx_image(im, (int)floorf(x + 0.5f), (int)floorf(y + 0.5f), alpha);
    return;
  }
  scaled(im, x, y, w, h, alpha, false);
}
void gfx_image_scaled_fast(int im, float x, float y, float w, float h, int alpha) { scaled(im, x, y, w, h, alpha, true); }

static void rotated(int im, float cx, float cy, float w, float h, float angle, int alpha, int step) {
  if (w < 0.5f || h < 0.5f || alpha <= 0) return;
  float c = cosf(angle), s = sinf(angle);
  float ex = (fabsf(c) * w + fabsf(s) * h) / 2, ey = (fabsf(s) * w + fabsf(c) * h) / 2;
  int x0 = (int)floorf(cx - ex), y0 = (int)floorf(cy - ey), x1 = (int)ceilf(cx + ex), y1 = (int)ceilf(cy + ey);
  if (x0 < g_x0) x0 = g_x0;
  if (y0 < g_y0) y0 = g_y0;
  if (x1 > g_x1) x1 = g_x1;
  if (y1 > g_y1) y1 = g_y1;
  if (x1 <= x0 || y1 <= y0) return;
  Img m = img(im);
  float ku = m.w / w * 256, kv = m.h / h * 256;
  /* screen (dx, dy) from the centre -> picture: turned back by angle */
  int uxs = (int)(c * ku), uys = (int)(s * ku), vxs = (int)(-s * kv), vys = (int)(c * kv);
  int hu = m.w * 128, hv = m.h * 128, umax = (m.w + 1) * 256, vmax = (m.h + 1) * 256;
  if (step > 1) {   /* (every other pixel, both ways: on even screen coordinates, so that bands agree) */
    y0 &= ~1, x0 &= ~1;
    for (int py = y0; py < y1; py += 2) {
      float dy = py + 1 - cy, dx = x0 + 1 - cx;
      int u = hu + (int)(dx * uxs + dy * uys), v = hv + (int)(dx * vxs + dy * vys);
      for (int px = x0; px < x1; px += 2, u += 2 * uxs, v += 2 * vxs) {
        if (u < -256 || v < -256 || u >= umax || v >= vmax) continue;
        int a;
        uint16_t col = bilinear(&m, u, v, false, &a);
        if (!a) continue;
        a = (a + (a >> 7)) * alpha >> 8;
        for (int yy = py; yy < py + 2; yy++) {
          if (yy < g_y0 || yy >= g_y1) continue;
          uint16_t *p = at(px, yy);
          if (px >= g_x0 && px < g_x1) put(p, col, a);
          if (px + 1 >= g_x0 && px + 1 < g_x1) put(p + 1, col, a);
        }
      }
    }
    return;
  }
  for (int py = y0; py < y1; py++) {
    float dy = py + 0.5f - cy, dx = x0 + 0.5f - cx;
    int u = hu + (int)(dx * uxs + dy * uys), v = hv + (int)(dx * vxs + dy * vys);
    uint16_t *p = at(x0, py);
    for (int px = x0; px < x1; px++, p++, u += uxs, v += vxs) {
      if (u < -256 || v < -256 || u >= umax || v >= vmax) continue;
      int a;
      uint16_t col = bilinear(&m, u, v, false, &a);
      if (a) put(p, col, (a + (a >> 7)) * alpha >> 8);
    }
  }
}
void gfx_image_rotated(int im, float cx, float cy, float w, float h, float angle, int alpha) {
  rotated(im, cx, cy, w, h, angle, alpha, 1);
}
void gfx_image_rotated_soft(int im, float cx, float cy, float w, float h, float angle, int alpha) {
  rotated(im, cx, cy, w, h, angle, alpha, 2);
}

void gfx_pattern(int im, int x, int y, int w, int h, float tw, float th, float ox, float oy, int alpha) {
  if (alpha <= 0 || !clipr(&x, &y, &w, &h)) return;
  Img m = img(im);
  bool same = fabsf(tw - m.w) < 0.01f && fabsf(th - m.h) < 0.01f;
  int su = (int)(m.w / tw * 256), sv = (int)(m.h / th * 256);
  for (int r = 0; r < h; r++) {
    uint16_t *p = at(x, y + r);
    float fy = y + r + 0.5f - oy;
    if (same) {   /* (the tiles as they are: in runs of a row of the picture) */
      int ty = (int)floorf(fy) % m.h;
      if (ty < 0) ty += m.h;
      int tx = (int)floorf(x + 0.5f - ox) % m.w;
      if (tx < 0) tx += m.w;
      for (int i = 0; i < w;) {
        int n = m.w - tx < w - i ? m.w - tx : w - i;
        row(&m, tx, ty, n, p + i, alpha);
        i += n, tx = 0;
      }
      continue;
    }
    int v = (int)(fy * m.h / th * 256);
    int u = (int)((x + 0.5f - ox) * m.w / tw * 256);
    for (int i = 0; i < w; i++, p++, u += su) {
      int a;
      uint16_t c = bilinear(&m, u, v, true, &a);
      if (a) put(p, c, (a + (a >> 7)) * alpha >> 8);
    }
    (void)sv;
  }
}

void gfx_icon(int index, int x, int y, int alpha) {
  int im = icon_index_image(index);
  if (im >= 0) gfx_image(im, x, y, alpha);
}
void gfx_icon_scaled(int index, float x, float y, float size, int alpha) {
  int im = icon_index_image(index);
  if (im < 0) return;
  if (fabsf(size - 24) < 0.01f) gfx_image(im, (int)x, (int)y, alpha);
  else gfx_image_scaled(im, x, y, size, size, alpha);
}

/* ------------------------------------------------------------------ text */
typedef struct {
  uint8_t first, count, ascent, line;
} FontHead;
typedef struct {
  uint8_t w, h;
  int8_t x, y;
  uint8_t adv, pad;
  uint16_t off;
} Glyph;

static const uint8_t *font_at(int f) { return cc_data + RES(uint32_t, OFF_FONTS)[f]; }
static const Glyph *glyph(int f, unsigned char ch, const uint8_t **data) {
  const uint8_t *p = font_at(f);
  const FontHead *hd = (const FontHead *)(const void *)p;
  int i = ch < 127 ? ch - 32 : ch >= 0x80 ? ch - 0x80 + 96 : -1;
  if (i < 0 || i >= hd->count) return NULL;
  const Glyph *g = (const Glyph *)(const void *)(p + 4) + i;
  *data = p + 4 + 8 * hd->count + g->off;
  return g;
}
int gfx_font_ascent(int f) { return ((const FontHead *)(const void *)font_at(f))->ascent; }
int gfx_font_line(int f) { return ((const FontHead *)(const void *)font_at(f))->line; }

static int draw_char(int f, int x, int y, unsigned char ch, uint16_t c, int alpha) {
  const uint8_t *d;
  const Glyph *g = glyph(f, ch, &d);
  if (!g) return x;
  int gx = x + g->x, gy = y + g->y;
  if (g->w && gfx_visible(gx, gy, g->w, g->h)) {
    int stride = (g->w + 1) >> 1;
    for (int r = 0; r < g->h; r++) {
      int yy = gy + r;
      if (yy < g_y0 || yy >= g_y1) continue;
      for (int i = 0; i < g->w; i++) {
        int xx = gx + i;
        if (xx < g_x0 || xx >= g_x1) continue;
        uint8_t b = d[r * stride + (i >> 1)];
        int cov = i & 1 ? b >> 4 : b & 15;
        if (cov) put(at(xx, yy), c, cov == 15 ? alpha : cov * alpha / 15);
      }
    }
  }
  return x + g->adv;
}
static int char_width(int f, unsigned char ch) {
  const uint8_t *d;
  const Glyph *g = glyph(f, ch, &d);
  return g ? g->adv : 0;
}
int gfx_text(int f, int x, int y, const char *s, uint16_t c, int alpha) {
  for (; *s; s++)
    if ((unsigned char)*s >= 32) x = draw_char(f, x, y, (unsigned char)*s, c, alpha);
  return x;
}
int gfx_text_n_width(int f, const char *s, int n) {
  int w = 0;
  for (int i = 0; i < n && s[i]; i++)
    if ((unsigned char)s[i] >= 32) w += char_width(f, (unsigned char)s[i]);
  return w;
}
int gfx_text_width(int f, const char *s) { return gfx_text_n_width(f, s, 1 << 30); }
int gfx_text_center(int f, int cx, int y, const char *s, uint16_t c, int alpha) {
  return gfx_text(f, cx - gfx_text_width(f, s) / 2, y, s, c, alpha);
}
int gfx_text_right(int f, int rx, int y, const char *s, uint16_t c, int alpha) {
  return gfx_text(f, rx - gfx_text_width(f, s), y, s, c, alpha);
}
/* a character's black outline (the pixels next to it, and a shadow below): made in a little mask, then
 * blended in one go */
static int draw_char_outline(int f, int x, int y, unsigned char ch, int alpha) {
  const uint8_t *d;
  const Glyph *g = glyph(f, ch, &d);
  if (!g) return x;
  int gx = x + g->x - 1, gy = y + g->y - 1, W = g->w + 2, H = g->h + 4;
  if (g->w && W <= 40 && H <= 44 && gfx_visible(gx, gy, W, H)) {
    uint8_t m[40 * 44];
    memset(m, 0, (size_t)(W * H));
    int stride = (g->w + 1) >> 1;
    for (int r = 0; r < g->h; r++)
      for (int i = 0; i < g->w; i++) {
        uint8_t b = d[r * stride + (i >> 1)];
        int cov = (i & 1 ? b >> 4 : b & 15) * 17, third = cov / 3;
        if (!cov) continue;
        uint8_t *o = m + (r + 1) * W + (i + 1);
#define MAX(p, v) (*(p) < (v) ? *(p) = (uint8_t)(v) : 0)
        MAX(o - 1, cov), MAX(o + 1, cov), MAX(o - W, cov), MAX(o + W, cov);
        MAX(o - W - 1, third), MAX(o - W + 1, third), MAX(o + W - 1, third), MAX(o + W + 1, third);
        MAX(o + 2 * W, third);
#undef MAX
      }
    for (int r = 0; r < H; r++) {
      int yy = gy + r;
      if (yy < g_y0 || yy >= g_y1) continue;
      for (int i = 0; i < W; i++) {
        int xx = gx + i, a = m[r * W + i];
        if (a && xx >= g_x0 && xx < g_x1) put(at(xx, yy), BLACK, (a + (a >> 7)) * alpha >> 8);
      }
    }
  }
  return x + g->adv;
}

/* (the outlines first, then the letters over them) */
void gfx_text_outline(int f, int x, int y, const char *s, uint16_t c, int alpha) {
  int x0 = x;
  for (const char *p = s; *p; p++)
    if ((unsigned char)*p >= 32) x = draw_char_outline(f, x, y, (unsigned char)*p, alpha);
  gfx_text(f, x0, y, s, c, alpha);
}
void gfx_text_outline_center(int f, int cx, int y, const char *s, uint16_t c, int alpha) {
  gfx_text_outline(f, cx - gfx_text_width(f, s) / 2, y, s, c, alpha);
}

/* ------------------------------------------------------------------ paragraphs */
typedef struct {
  const ParaStyle *st;
  bool bold, italic;
} Pen;
static int pen_font(const Pen *p) { return p->bold ? p->st->bold : p->italic ? p->st->italic : p->st->font; }
static uint16_t pen_color(const Pen *p) {
  return p->bold ? p->st->bold_color : p->italic ? p->st->italic_color : p->st->color;
}
static void pen_code(Pen *p, unsigned char c) {
  if (c == 1) p->bold = true;
  if (c == 2) p->bold = false;
  if (c == 3) p->italic = true;
  if (c == 4) p->italic = false;
}

int gfx_para(const ParaStyle *st, int x, int y, int w, const char *s, bool draw) {
  Pen pen = {st, false, false};
  int top = y, base = y + gfx_font_ascent(st->font);
  const unsigned char *p = (const unsigned char *)s;
  while (*p) {
    if (*p == 5) {   /* a line across */
      if (draw) {
        gfx_hgrad(x, base - gfx_font_ascent(st->font) + 3, w / 2, 1, BLACK, 0, 120);
        gfx_hgrad(x + w / 2, base - gfx_font_ascent(st->font) + 3, w - w / 2, 1, BLACK, 120, 0);
        gfx_hgrad(x, base - gfx_font_ascent(st->font) + 4, w / 2, 1, WHITE, 0, 40);
        gfx_hgrad(x + w / 2, base - gfx_font_ascent(st->font) + 4, w - w / 2, 1, WHITE, 40, 0);
      }
      base += 7, p++;
      if (*p == '\n') p++;
      continue;
    }
    /* the words that fit on this line */
    const unsigned char *start = p, *end = p, *q = p;
    Pen at_start = pen, scan = pen;
    int width = 0, fit_width = 0;
    while (*q && *q != '\n' && *q != 5) {
      int ww = 0;
      const unsigned char *word = q;
      Pen wp = scan;
      while (*q == ' ') ww += char_width(pen_font(&wp), ' '), q++;
      while (*q && *q != ' ' && *q != '\n' && *q != 5) {
        if (*q < 32) pen_code(&wp, *q);
        else ww += char_width(pen_font(&wp), *q);
        q++;
      }
      if (width + ww > w && end != start) {
        q = word;
        break;
      }
      width += ww, scan = wp, end = q, fit_width = width;
    }
    if (end == start && *q && *q != '\n' && *q != 5) end = q;   /* (a word longer than the line) */
    if (draw) {
      int xx = x + (st->align ? (w - fit_width) / 2 : 0);
      Pen dp = at_start;
      for (const unsigned char *c = start; c < end; c++) {
        if (*c < 32) pen_code(&dp, *c);
        else if (base - gfx_font_ascent(st->font) < g_y1 && base + 4 > g_y0)
          xx = draw_char(pen_font(&dp), xx, base, *c, pen_color(&dp), 256);
        else
          xx += char_width(pen_font(&dp), *c);
      }
    }
    for (const unsigned char *c = start; c < end; c++)
      if (*c < 32) pen_code(&pen, *c);
    p = end;
    while (*p == ' ') p++;
    if (*p == '\n') p++;
    base += st->line;
  }
  return base - gfx_font_ascent(st->font) - top;
}
