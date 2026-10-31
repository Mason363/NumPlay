/* Drawing: the draw list, rasterised in bands (gfx.h). */
#include <stdarg.h>
#include <string.h>
#include "../../../launcher/src/inflate.h"
#include "gfx.h"
#include "res.h"

uint8_t gfx_bg[SCREEN_W * SCREEN_H];
uint16_t gfx_bg_pal[256];
bool gfx_bg_on;
int gfx_dropped;

enum { IT_ANIM, IT_RECT, IT_RRECT, IT_ELLIPSE, IT_TEXT, IT_CUSTOM };
typedef struct {
  uint8_t type, flags, alpha, font;
  int16_t x, y, w, h;           /* what it covers, for skipping bands */
  int16_t cx0, cy0, cx1, cy1;   /* its clip */
  union {
    struct {
      uint16_t frame, base_frame;   /* base: 0xFFFF none */
      uint16_t sx, sy;              /* scale, 256 = 1 */
      const uint16_t *pal, *base_pal;
      int16_t ax, ay;               /* the anchor */
    } a;
    struct { uint16_t color; int16_t r; } r;
    struct { const char *s; uint16_t color; } t;
    struct { gfx_fn fn; void *ctx; } c;
  } u;
} Item;

static Item items[GFX_MAX_ITEMS];
static int nitems;
static uint16_t *band;
static int clip_x0, clip_y0, clip_x1 = SCREEN_W, clip_y1 = SCREEN_H;
static char text_pool[GFX_TEXT_POOL];
static int text_used;

/* palettes changed for a tint, this frame */
#define TINT_SLOTS 24
#define TINT_COLORS 64
static struct { uint16_t pal; uint8_t tint, used; uint16_t col[TINT_COLORS]; } tints[TINT_SLOTS];
static int ntints;

void gfx_set_band_memory(uint16_t *px) { band = px; }

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

void gfx_clip(int x, int y, int w, int h) {
  clip_x0 = imax(0, x), clip_y0 = imax(0, y), clip_x1 = imin(SCREEN_W, x + w), clip_y1 = imin(SCREEN_H, y + h);
}
void gfx_clip_all(void) { clip_x0 = clip_y0 = 0, clip_x1 = SCREEN_W, clip_y1 = SCREEN_H; }

void gfx_begin(void) {
  nitems = 0, text_used = 0, ntints = 0, gfx_dropped = 0;
  gfx_clip_all();
}

static Item *add(int type, int x, int y, int w, int h) {
  if (nitems >= GFX_MAX_ITEMS) {
    gfx_dropped++;
    return NULL;
  }
  if (x >= clip_x1 || y >= clip_y1 || x + w <= clip_x0 || y + h <= clip_y0 || w <= 0 || h <= 0) return NULL;
  Item *it = &items[nitems++];
  it->type = (uint8_t)type, it->flags = 0, it->alpha = 32;
  it->x = (int16_t)x, it->y = (int16_t)y, it->w = (int16_t)w, it->h = (int16_t)h;
  it->cx0 = (int16_t)clip_x0, it->cy0 = (int16_t)clip_y0, it->cx1 = (int16_t)clip_x1, it->cy1 = (int16_t)clip_y1;
  return it;
}

/* ---------------------------------------------------------------- colours */

static uint16_t tint_color(uint16_t c, int tint) {
  int r = c >> 11, g = (c >> 5) & 63, b = c & 31;
  switch (tint) {
  case TINT_HIT:     /* a flash: brighter */
    r = imin(31, r + 6), g = imin(63, g + 12), b = imin(31, b + 6);
    break;
  case TINT_CHILL: { /* the chill of a snow pea: blue */
    r = r * 9 / 16, g = g * 13 / 16 + 6, b = imin(31, b * 3 / 4 + 11);
    break;
  }
  case TINT_FROZEN: {
    int l = (r * 2 + g + b * 2) / 5;
    r = imin(31, l / 2 + 10), g = imin(63, l + 26), b = 31;
    break;
  }
  case TINT_HYPNO:   /* charmed by a hypno-shroom: pinkish */
    r = imin(31, (r + 31) / 2), g = g * 2 / 3, b = imin(31, (b + 28) / 2);
    break;
  case TINT_SLEEP:   /* a mushroom asleep in the day */
    r = r * 5 / 8, g = g * 5 / 8, b = imin(31, b * 5 / 8 + 3);
    break;
  case TINT_DARK:
    r /= 2, g /= 2, b /= 2;
    break;
  case TINT_WHITE:
    r = 31, g = 63, b = 31;
    break;
  case TINT_BLACK:
    r = g = b = 0;
    break;
  case TINT_RED:
    r = imin(31, r + 10), g = g * 2 / 3, b = b * 2 / 3;
    break;
  case TINT_GRAY: {
    int l = (r * 2 + g + b * 2) / 5;
    r = l / 2, g = l, b = l / 2;
    break;
  }
  }
  return (uint16_t)(r << 11 | g << 5 | b);
}

static const uint16_t *palette(int pal, int tint) {
  int n;
  const uint16_t *c = pal_colors(pal, &n);
  if (tint == TINT_NONE || n > TINT_COLORS) return c;
  for (int i = 0; i < ntints; i++)
    if (tints[i].pal == pal && tints[i].tint == tint) return tints[i].col;
  if (ntints == TINT_SLOTS) return c;
  tints[ntints].pal = (uint16_t)pal, tints[ntints].tint = (uint8_t)tint;
  for (int i = 0; i < n; i++) tints[ntints].col[i] = tint_color(c[i], tint);
  return tints[ntints++].col;
}

/* ---------------------------------------------------------------- the background */

uint32_t gfx_scratch[640 * 16 / 4];

bool gfx_load_back_rows(int b, int src_x, int y0, int y1, int x0, int x1) {
  Back bk = back_info(b);
  if (y1 <= y0) y0 = 0, y1 = bk.h;
  if (x1 <= x0) x0 = 0, x1 = SCREEN_W;
  int n;
  const uint16_t *pal = pal_colors(bk.pal, &n);
  for (int i = 0; i < n && i < 256; i++) gfx_bg_pal[i] = pal[i];
  uint8_t *chunk = (uint8_t *)gfx_scratch;
  uint8_t tables[3600];
  if (np_inflate_tables_size() > sizeof tables || bk.w * bk.rows > (int)sizeof gfx_scratch) return false;
  for (int c = 0; c < bk.nchunks; c++) {
    int ry = c * bk.rows;
    if (ry + bk.rows <= y0 || ry >= y1) continue;
    uint32_t a = rd32(bk.chunks + 4 * c), e = rd32(bk.chunks + 4 * c + 4);
    int got = np_inflate(chunk, sizeof gfx_scratch, pvz_bin + a, e - a, (np_inflate_tables_t *)(void *)tables);
    if (got < bk.w) return false;
    for (int r = 0; r < bk.rows && ry + r < SCREEN_H; r++) {
      int y = ry + r;
      if (y < y0 || y >= y1 || (r + 1) * bk.w > got) continue;
      for (int x = x0; x < x1; x++) {
        int sx = src_x + x;
        gfx_bg[y * SCREEN_W + x] = chunk[r * bk.w + (sx < 0 ? 0 : sx >= bk.w ? bk.w - 1 : sx)];
      }
    }
  }
  gfx_bg_on = true;
  return true;
}
bool gfx_load_back(int b, int src_x) { return gfx_load_back_rows(b, src_x, 0, 0, 0, 0); }

/* ---------------------------------------------------------------- sprites */

void anim_box(int a, int f, int *x, int *y, int *w, int *h) {
  Anim an = anim_info(a);
  Frame fr = frame_info(an.first + (f % an.frames));
  *x = fr.x, *y = fr.y, *w = fr.w, *h = fr.h;
}

int anim_frame_at(int a, uint32_t t) {
  Anim an = anim_info(a);
  if (an.frames <= 1 || !an.cycle) return 0;
  return (int)((t % an.cycle) * an.frames / an.cycle);
}

static void add_anim(int a, int f, int x, int y, int sx, int sy, int flags, int tint, int alpha) {
  Anim an = anim_info(a);
  if (!an.frames) return;
  f = ((f % an.frames) + an.frames) % an.frames;
  Frame fr = frame_info(an.first + f);
  int bx = fr.x, by = fr.y, bw = fr.w, bh = fr.h, base = 0xFFFF;
  const uint16_t *bpal = NULL;
  if (an.base != 0xFFFF) {
    Anim bn = anim_info(an.base);
    base = bn.first + f % bn.frames;
    Frame b = frame_info(base);
    int x1 = imax(bx + bw, b.x + b.w), y1 = imax(by + bh, b.y + b.h);
    bx = imin(bx, b.x), by = imin(by, b.y), bw = x1 - bx, bh = y1 - by;
    bpal = palette(bn.pal, tint);
  }
  int x0, y0, w, h;
  if (flags & GFX_FLIP) bx = -(bx + bw);
  x0 = x + bx * sx / 256, y0 = y + by * sy / 256, w = (bw * sx + 255) / 256 + 1, h = (bh * sy + 255) / 256 + 1;
  Item *it = add(IT_ANIM, x0, y0, w, h);
  if (!it) return;
  it->flags = (uint8_t)flags, it->alpha = (uint8_t)alpha;
  it->u.a.frame = (uint16_t)(an.first + f), it->u.a.base_frame = (uint16_t)base;
  it->u.a.sx = (uint16_t)sx, it->u.a.sy = (uint16_t)sy;
  it->u.a.pal = palette(an.pal, tint), it->u.a.base_pal = bpal;
  it->u.a.ax = (int16_t)x, it->u.a.ay = (int16_t)y;
  it->font = an.bpp;
  if (an.base != 0xFFFF && anim_info(an.base).bpp != an.bpp) it->font |= 0x80;   /* (never: overlays share 4 bits) */
}

void gfx_anim(int a, int f, int x, int y, int flags, int tint, int alpha) {
  if (alpha <= 0) return;
  add_anim(a, f, x, y, 256, 256, flags, tint, alpha);
}
void gfx_anim_scaled(int a, int f, int x, int y, int sx, int sy, int flags, int tint, int alpha) {
  if (alpha <= 0 || sx <= 0 || sy <= 0) return;
  add_anim(a, f, x, y, sx, sy, flags, tint, alpha);
}

static inline uint16_t half(uint16_t a, uint16_t b) { return (uint16_t)(((a & 0xF7DE) >> 1) + ((b & 0xF7DE) >> 1)); }

/* one row of a frame into idx / vis (0 none, 1 half, 2 opaque); returns the pointer past it */
static void decode_row(const uint8_t *p, const uint8_t *e, int bpp, uint8_t *idx, uint8_t *vis, int w) {
  int x = 0;
  memset(vis, 0, (size_t)w);
  while (p < e && x < w) {
    int t = *p++;
    if (t < 0x40) {
      x += t + 1;
    } else if (t < 0xC0) {
      int n = (t & 0x3F) + 1, v = t < 0x80 ? 2 : 1;
      if (bpp == 4) {
        for (int i = 0; i < n; i += 2) {
          int b = *p++;
          if (x < w) idx[x] = (uint8_t)(b & 15), vis[x] = (uint8_t)v;
          x++;
          if (i + 1 < n) {
            if (x < w) idx[x] = (uint8_t)(b >> 4), vis[x] = (uint8_t)v;
            x++;
          }
        }
      } else {
        for (int i = 0; i < n; i++, x++)
          if (x < w) idx[x] = p[i], vis[x] = (uint8_t)v;
        p += n;
      }
    } else {
      int n, c;
      if (bpp == 4) n = 2 + ((t >> 4) & 3), c = t & 15;
      else n = t - 0xBE, c = *p++;
      for (int i = 0; i < n; i++, x++)
        if (x < w) idx[x] = (uint8_t)c, vis[x] = 2;
    }
  }
}

/* PVZ_SHARED_RLE_ROWS_V1: a marked length 255 carries a 4-byte descriptor.
 * Bits 0..23: absolute offset of an earlier encoded row; bits 24..31: size.
 * Legacy 4-bit frames and all 8-bit frames retain their previous format. */
static const uint8_t *row_at(const Frame *f, int bpp, int r, const uint8_t **end) {
  if (bpp == 4) {
    const uint8_t *p = f->rows + f->h;
    if (f->shared_rows) {
      for (int i = 0; i < r; i++) p += f->rows[i] == 255 ? 4 : f->rows[i];
      if (f->rows[r] == 255) {
        uint32_t ref = rd32(p);
        const uint8_t *shared = pvz_bin + (ref & 0x00ffffffu);
        *end = shared + (ref >> 24);
        return shared;
      }
    } else {
      for (int i = 0; i < r; i++) p += f->rows[i];
    }
    *end = p + f->rows[r];
    return p;
  }
  const uint8_t *base = f->rows;
  *end = r + 1 < f->h ? base + rd16(base + 2 * r + 2) : NULL;
  return base + rd16(base + 2 * r);
}

static void blit_frame(const Item *it, int fi, const uint16_t *pal, int bpp, int y0, int y1) {
  Frame f = frame_info(fi);
  int flip = it->flags & GFX_FLIP, alpha = it->alpha;
  int cx0 = it->cx0, cx1 = it->cx1;
  y0 = imax(y0, it->cy0), y1 = imin(y1, it->cy1);
  if (it->u.a.sx != 256 || it->u.a.sy != 256) {
    /* scaled: each screen pixel from the frame's pixel under it */
    int sx = it->u.a.sx, sy = it->u.a.sy;
    int fx = flip ? -(f.x + f.w) : f.x;
    int ox = it->u.a.ax + fx * sx / 256, oy = it->u.a.ay + f.y * sy / 256;
    int dw = f.w * sx / 256, dh = f.h * sy / 256;
    uint8_t idx[320], vis[320];
    int last = -1;
    for (int y = imax(y0, oy); y < imin(y1, oy + dh); y++) {
      int r = (y - oy) * 256 / sy;
      if (r >= f.h) break;
      if (r != last) {
        const uint8_t *e, *p = row_at(&f, bpp, r, &e);
        if (!e) e = f.end;   /* (the last row: up to the next frame) */
        decode_row(p, e, bpp, idx, vis, imin(f.w, 320));
        last = r;
      }
      uint16_t *out = band + (y % GFX_BAND) * SCREEN_W;
      for (int x = imax(cx0, ox); x < imin(cx1, ox + dw); x++) {
        int c = (x - ox) * 256 / sx;
        if (c >= f.w) break;
        if (flip) c = f.w - 1 - c;
        int v = vis[c];
        if (!v) continue;
        uint16_t col = pal[idx[c]];
        if (v == 2) out[x] = alpha >= 32 ? col : mix565(col, out[x], alpha);
        else out[x] = alpha >= 32 ? half(col, out[x]) : mix565(col, out[x], alpha / 2);
      }
    }
    return;
  }
  int fx = flip ? -(f.x + f.w) : f.x;
  int ox = it->u.a.ax + fx, oy = it->u.a.ay + f.y;
  int ya = imax(y0, oy), yb = imin(y1, oy + f.h);
  if (ya >= yb) return;
  const uint8_t *e, *p = row_at(&f, bpp, ya - oy, &e);
  /* PVZ_SHARED_RLE_ROWS_V1: streaming cursor avoids quadratic row rescans. */
  const uint8_t *cursor = NULL;
  if (bpp == 4 && f.shared_rows) {
    cursor = f.rows + f.h;
    for (int r = 0; r < ya - oy; r++) cursor += f.rows[r] == 255 ? 4 : f.rows[r];
  }
  for (int y = ya; y < yb; y++) {
    int r = y - oy;
    if (bpp == 4) {
      if (f.shared_rows) {
        int length = f.rows[r];
        if (length == 255) {
          uint32_t ref = rd32(cursor);
          p = pvz_bin + (ref & 0x00ffffffu);
          e = p + (ref >> 24);
          cursor += 4;
        } else {
          p = cursor;
          e = p + length;
          cursor += length;
        }
      } else e = p + f.rows[r];
    } else {
      p = f.rows + rd16(f.rows + 2 * r);
      e = r + 1 < f.h ? f.rows + rd16(f.rows + 2 * r + 2) : f.end;   /* (the last row: up to the next frame) */
    }
    uint16_t *out = band + (y % GFX_BAND) * SCREEN_W;
    int x = 0, w = f.w;
    const uint8_t *q = p;
    while (q < e && x < w) {
      int t = *q++;
      if (t < 0x40) {
        x += t + 1;
        continue;
      }
      if (t < 0xC0) {
        int n = (t & 0x3F) + 1, opaque = t < 0x80;
        for (int i = 0; i < n; i++, x++) {
          int c = bpp == 4 ? (i & 1 ? q[i >> 1] >> 4 : q[i >> 1] & 15) : q[i];
          int dx = flip ? ox + w - 1 - x : ox + x;
          if (dx < cx0 || dx >= cx1) continue;
          uint16_t col = pal[c];
          if (opaque) out[dx] = alpha >= 32 ? col : mix565(col, out[dx], alpha);
          else out[dx] = alpha >= 32 ? half(col, out[dx]) : mix565(col, out[dx], alpha / 2);
        }
        q += bpp == 4 ? (n + 1) >> 1 : n;
        continue;
      }
      int n, c;
      if (bpp == 4) n = 2 + ((t >> 4) & 3), c = t & 15;
      else n = t - 0xBE, c = *q++;
      uint16_t col = pal[c];
      for (int i = 0; i < n; i++, x++) {
        int dx = flip ? ox + w - 1 - x : ox + x;
        if (dx < cx0 || dx >= cx1) continue;
        out[dx] = alpha >= 32 ? col : mix565(col, out[dx], alpha);
      }
    }
    if (bpp == 4 && !f.shared_rows) p = e;
  }
}

/* ---------------------------------------------------------------- shapes */

void gfx_rect(int x, int y, int w, int h, uint16_t color, int alpha) {
  if (alpha <= 0) return;
  Item *it = add(IT_RECT, x, y, w, h);
  if (it) it->alpha = (uint8_t)imin(alpha, 32), it->u.r.color = color, it->u.r.r = 0;
}
void gfx_frame_rect(int x, int y, int w, int h, int t, uint16_t color, int alpha) {
  gfx_rect(x, y, w, t, color, alpha);
  gfx_rect(x, y + h - t, w, t, color, alpha);
  gfx_rect(x, y + t, t, h - 2 * t, color, alpha);
  gfx_rect(x + w - t, y + t, t, h - 2 * t, color, alpha);
}
void gfx_round_rect(int x, int y, int w, int h, int r, uint16_t color, int alpha) {
  if (alpha <= 0) return;
  Item *it = add(IT_RRECT, x, y, w, h);
  if (it) it->alpha = (uint8_t)imin(alpha, 32), it->u.r.color = color, it->u.r.r = (int16_t)imin(r, imin(w, h) / 2);
}
void gfx_ellipse(int cx, int cy, int rx, int ry, uint16_t color, int alpha) {
  if (alpha <= 0 || rx <= 0 || ry <= 0) return;
  Item *it = add(IT_ELLIPSE, cx - rx, cy - ry, 2 * rx + 1, 2 * ry + 1);
  if (it) it->alpha = (uint8_t)imin(alpha, 32), it->u.r.color = color, it->u.r.r = 0;
}
void gfx_custom(gfx_fn fn, void *ctx) {
  Item *it = add(IT_CUSTOM, 0, 0, SCREEN_W, SCREEN_H);
  if (it) it->u.c.fn = fn, it->u.c.ctx = ctx;
}

static int isqrt(int v) {
  int r = 0;
  while ((r + 1) * (r + 1) <= v) r++;
  return r;
}

static void span(uint16_t *out, int a, int b, const Item *it) {
  a = imax(a, it->cx0), b = imin(b, it->cx1);
  uint16_t c = it->u.r.color;
  if (it->alpha >= 32)
    for (int x = a; x < b; x++) out[x] = c;
  else if (it->alpha == 16)
    for (int x = a; x < b; x++) out[x] = half(c, out[x]);
  else
    for (int x = a; x < b; x++) out[x] = mix565(c, out[x], it->alpha);
}

/* ---------------------------------------------------------------- text */

static const uint8_t *glyph(const Font *f, int ch) {
  if (ch < f->first || ch >= f->first + f->count) ch = '?';
  return f->glyphs + 8 * (ch - f->first);
}

int font_line(int font) {
  Font f = font_info(font);
  return f.ascent + f.descent + 1;
}

static int line_width(const Font *f, const char *s) {
  int w = 0;
  for (; *s && *s != '\n'; s++) w += glyph(f, (uint8_t)*s)[4];
  return w;
}

int gfx_text_width(int font, const char *s) {
  Font f = font_info(font);
  int w = 0;
  while (*s) {
    w = imax(w, line_width(&f, s));
    while (*s && *s != '\n') s++;
    if (*s) s++;
  }
  return w;
}

int gfx_text(int font, int x, int y, const char *s, uint16_t color, int flags) {
  Font f = font_info(font);
  int w = gfx_text_width(font, s), lines = 1;
  for (const char *p = s; *p; p++) lines += *p == '\n';
  int lh = font_line(font);
  int x0 = flags & GFX_CENTER ? x - w / 2 : flags & GFX_RIGHT ? x - w : x;
  Item *it = add(IT_TEXT, x0 - 2, y - f.ascent - 2, w + 4, lh * lines + 4);
  if (!it) return w;
  it->flags = (uint8_t)flags, it->font = (uint8_t)font, it->u.t.s = s, it->u.t.color = color;
  return w;
}

static void draw_text(const Item *it, int y0, int y1, int dx, int dy, uint16_t color) {
  Font f = font_info(it->font);
  int lh = font_line(it->font);
  int base = it->y + 2 + f.ascent + dy;
  const char *s = it->u.t.s;
  y0 = imax(y0, it->cy0), y1 = imin(y1, it->cy1);
  while (*s) {
    int w = line_width(&f, s);
    int x = it->flags & GFX_CENTER ? it->x + 2 + (it->w - 4) / 2 - w / 2 : it->flags & GFX_RIGHT ? it->x + it->w - 2 - w
                                                                                              : it->x + 2;
    x += dx;
    for (; *s && *s != '\n'; s++) {
      const uint8_t *g = glyph(&f, (uint8_t)*s);
      int gw = g[0], gh = g[1], gx = x + (int8_t)g[2], gy = base + (int8_t)g[3];
      const uint8_t *bits = f.data + rd16(g + 6);
      for (int r = imax(0, y0 - gy); r < gh && gy + r < y1; r++) {
        uint16_t *out = band + ((gy + r) % GFX_BAND) * SCREEN_W;
        for (int c = 0; c < gw; c++) {
          int px = gx + c;
          if (px < it->cx0 || px >= it->cx1) continue;
          int i = r * gw + c, v = (bits[i >> 1] >> ((i & 1) * 4)) & 15;
          if (!v) continue;
          out[px] = v == 15 ? color : mix565(color, out[px], (v * 32 + 7) / 15);
        }
      }
      x += g[4];
    }
    if (*s) s++;
    base += lh;
  }
}

int gfx_paragraph(int font, int x, int y, int width, const char *s, uint16_t color, int flags) {
  Font f = font_info(font);
  int lh = font_line(font) - (flags & GFX_TIGHT ? 1 : 0);
  char line[160];
  bool draw = !(flags & GFX_MEASURE);
  flags &= ~(GFX_MEASURE | GFX_TIGHT);
  while (*s) {
    /* as many words as fit */
    int n = 0, best = 0, w = 0;
    while (s[n] && s[n] != '\n') {
      w += glyph(&f, (uint8_t)s[n])[4];
      if (w > width && best) break;
      n++;
      if (s[n] == ' ' || !s[n] || s[n] == '\n') best = n;
    }
    if (!best) best = n;
    if (best > (int)sizeof line - 1) best = (int)sizeof line - 1;
    memcpy(line, s, (size_t)best);
    line[best] = 0;
    if (draw) gfx_text(font, x, y, gfx_fmt("%s", line), color, flags | GFX_CENTER);
    s += best;
    while (*s == ' ') s++;
    if (*s == '\n') s++;
    y += lh;
  }
  return y;
}

/* a printf of %d, %u, %s and %c, with a width (%3d, %-3s, %03d) */
const char *gfx_fmt(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char *out = text_pool + text_used, *end = text_pool + GFX_TEXT_POOL - 1;
  char *o = out;
  for (const char *p = fmt; *p && o < end; p++) {
    if (*p != '%') {
      *o++ = *p;
      continue;
    }
    p++;
    bool left = false, zero = false;
    int width = 0;
    if (*p == '-') left = true, p++;
    if (*p == '0') zero = true, p++;
    while (*p >= '0' && *p <= '9') width = width * 10 + (*p++ - '0');
    char tmp[16];
    const char *src = tmp;
    int n = 0;
    if (*p == 'd' || *p == 'u') {
      long v = *p == 'd' ? (long)va_arg(ap, int) : (long)va_arg(ap, unsigned);
      bool neg = v < 0;
      unsigned long u = neg ? (unsigned long)-v : (unsigned long)v;
      char rev[16];
      int k = 0;
      do rev[k++] = (char)('0' + u % 10), u /= 10;
      while (u && k < 15);
      if (neg) tmp[n++] = '-';
      while (k) tmp[n++] = rev[--k];
    } else if (*p == 's') {
      src = va_arg(ap, const char *);
      n = (int)strlen(src);
    } else if (*p == 'c') {
      tmp[n++] = (char)va_arg(ap, int);
    } else {
      tmp[n++] = *p;
    }
    int pad = width > n ? width - n : 0;
    if (!left)
      while (pad-- > 0 && o < end) *o++ = zero ? '0' : ' ';
    for (int i = 0; i < n && o < end; i++) *o++ = src[i];
    if (left)
      while (pad-- > 0 && o < end) *o++ = ' ';
  }
  *o++ = 0;
  va_end(ap);
  if (o > end) return "";
  text_used = (int)(o - text_pool);
  return out;
}

/* ---------------------------------------------------------------- a frame */

static void raster(int y0, int y1) {
  /* the background */
  for (int y = y0; y < y1; y++) {
    uint16_t *out = band + (y % GFX_BAND) * SCREEN_W;
    if (!gfx_bg_on) {
      memset(out, 0, SCREEN_W * 2);
      continue;
    }
    const uint8_t *s = gfx_bg + y * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x += 4) {
      out[x] = gfx_bg_pal[s[x]], out[x + 1] = gfx_bg_pal[s[x + 1]];
      out[x + 2] = gfx_bg_pal[s[x + 2]], out[x + 3] = gfx_bg_pal[s[x + 3]];
    }
  }
  for (int i = 0; i < nitems; i++) {
    const Item *it = &items[i];
    if (it->y >= y1 || it->y + it->h <= y0 || it->cy0 >= y1 || it->cy1 <= y0) continue;
    int ya = imax(y0, imax(it->y, it->cy0)), yb = imin(y1, imin(it->y + it->h, it->cy1));
    switch (it->type) {
    case IT_ANIM:
      if (it->u.a.base_frame != 0xFFFF) blit_frame(it, it->u.a.base_frame, it->u.a.base_pal, it->font & 0x7F, y0, y1);
      blit_frame(it, it->u.a.frame, it->u.a.pal, it->font & 0x7F, y0, y1);
      break;
    case IT_RECT:
      for (int y = ya; y < yb; y++) span(band + (y % GFX_BAND) * SCREEN_W, it->x, it->x + it->w, it);
      break;
    case IT_RRECT: {
      int r = it->u.r.r;
      for (int y = ya; y < yb; y++) {
        /* (twice the distance from the corners' centre row, for half-pixel steps) */
        int d2 = y < it->y + r ? 2 * (it->y + r - y) - 1 : y >= it->y + it->h - r ? 2 * (y - (it->y + it->h - r)) + 1 : 0;
        int in = d2 > 0 ? r - isqrt(imax(0, r * r - d2 * d2 / 4)) : 0;
        span(band + (y % GFX_BAND) * SCREEN_W, it->x + in, it->x + it->w - in, it);
      }
      break;
    }
    case IT_ELLIPSE: {
      int rx = (it->w - 1) / 2, ry = (it->h - 1) / 2, cx = it->x + rx, cy = it->y + ry;
      for (int y = ya; y < yb; y++) {
        int dy = y - cy;
        int hw = (int)((long)rx * isqrt(imax(0, ry * ry - dy * dy)) / ry);
        span(band + (y % GFX_BAND) * SCREEN_W, cx - hw, cx + hw + 1, it);
      }
      break;
    }
    case IT_TEXT:
      if (it->flags & (GFX_OUTLINE | GFX_SHADOW)) {
        uint16_t dark = it->flags & GFX_OUTLINE ? 0x0000 : mix565(0, it->u.t.color, 8);
        if (it->flags & GFX_OUTLINE) {
          draw_text(it, y0, y1, -1, 0, dark), draw_text(it, y0, y1, 1, 0, dark);
          draw_text(it, y0, y1, 0, -1, dark), draw_text(it, y0, y1, 0, 1, dark);
        } else {
          draw_text(it, y0, y1, 1, 1, dark);
        }
      }
      draw_text(it, y0, y1, 0, 0, it->u.t.color);
      break;
    case IT_CUSTOM:
      it->u.c.fn(band, y0, y1, it->u.c.ctx);
      break;
    }
  }
}

void gfx_end(void) {
  for (int y = 0; y < SCREEN_H; y += GFX_BAND) {
    raster(y, y + GFX_BAND);
    plat_push(0, y, SCREEN_W, GFX_BAND, band);
  }
}
