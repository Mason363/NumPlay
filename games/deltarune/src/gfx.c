/* The renderer: a draw list rasterised in bands of GFX_BAND rows.
 *
 * Each command keeps its screen bounds (clipped); a band walks the commands
 * that cross it, in order. Sprites are row RLE (res.h): a row is runs of
 * (skip, length, pixels), so a band only reads the rows it shows. Alpha has
 * 33 steps (0..32), applied with the usual RGB565 trick. */
#include <string.h>
#include "blk.h"
#include "gfx.h"
#include "res.h"
#include "world.h"

enum { I_RECT, I_SPRITE, I_TILES, I_TEXT, I_LINE, I_CIRCLE, I_SPRITE_EX };

typedef struct {
  uint8_t kind, flags, alpha, font;
  int16_t x0, y0, x1, y1;      /* screen bounds, clipped: [x0, x1) x [y0, y1) */
  int16_t cx0, cx1;            /* the clip's columns */
  int16_t x, y;                /* sprite: its top-left; text: its top-left; line: start */
  int16_t a, b;                /* line: end; circle: radius; sprite_ex: scale x, scale y; text: shake, spacing */
  uint16_t ref, sub;           /* sprite, frame / text: pool offset, length */
  uint16_t color;              /* sprite: 0 its own colours, else 1 + RGB565 (in `extra`) */
  uint16_t extra;
  uint32_t seed;
} Item;

#ifdef HOST
static uint16_t host_band[SCREEN_W * GFX_BAND];
static uint16_t *band = host_band;
#else
static uint16_t *band;
#endif
void gfx_set_band_memory(uint16_t *px) {
#ifdef HOST
  band = px ? px : host_band;
#else
  band = px;
#endif
}

static Item items[GFX_MAX_ITEMS];
static int nitems;
static char pool[GFX_TEXT_POOL];
static int npool;
static int clip_x0, clip_y0, clip_x1 = SCREEN_W, clip_y1 = SCREEN_H;
int gfx_dropped;
static int dropped_now;

void gfx_begin(void) {
  blk_frame();
  nitems = 0, npool = 0, dropped_now = 0;
  gfx_clip_all();
}

void gfx_clip(int x, int y, int w, int h) {
  clip_x0 = x < 0 ? 0 : x, clip_y0 = y < 0 ? 0 : y;
  clip_x1 = x + w > SCREEN_W ? SCREEN_W : x + w, clip_y1 = y + h > SCREEN_H ? SCREEN_H : y + h;
}
void gfx_clip_all(void) { clip_x0 = 0, clip_y0 = 0, clip_x1 = SCREEN_W, clip_y1 = SCREEN_H; }

/* a new command over [x0, x1) x [y0, y1), clipped; NULL if it shows nothing */
static Item *add(int kind, int x0, int y0, int x1, int y1) {
  if (x0 < clip_x0) x0 = clip_x0;
  if (y0 < clip_y0) y0 = clip_y0;
  if (x1 > clip_x1) x1 = clip_x1;
  if (y1 > clip_y1) y1 = clip_y1;
  if (x0 >= x1 || y0 >= y1) return NULL;
  if (nitems >= GFX_MAX_ITEMS) {
    dropped_now++;
    return NULL;
  }
  Item *it = &items[nitems++];
  memset(it, 0, sizeof *it);
  it->kind = (uint8_t)kind;
  it->x0 = (int16_t)x0, it->y0 = (int16_t)y0, it->x1 = (int16_t)x1, it->y1 = (int16_t)y1;
  it->cx0 = (int16_t)clip_x0, it->cx1 = (int16_t)clip_x1;
  it->alpha = 255;
  return it;
}

void gfx_rect(int x, int y, int w, int h, uint16_t color, uint8_t alpha) {
  if (!alpha) return;
  Item *it = add(I_RECT, x, y, x + w, y + h);
  if (it) it->color = color, it->alpha = alpha;
}

void gfx_frame_rect(int x, int y, int w, int h, int t, uint16_t color) {
  gfx_rect(x, y, w, t, color, 255);
  gfx_rect(x, y + h - t, w, t, color, 255);
  gfx_rect(x, y + t, t, h - 2 * t, color, 255);
  gfx_rect(x + w - t, y + t, t, h - 2 * t, color, 255);
}

static void sprite_item(uint16_t s, int i, int x, int y, uint8_t flags, uint16_t color, bool tint, uint8_t alpha) {
  Frame f;
  if (!alpha || !frame_get(s, i, &f)) return;
  int tx = x - ((flags & GFX_FLIPX) ? f.w - 1 - f.ox : f.ox), ty = y - ((flags & GFX_FLIPY) ? f.h - 1 - f.oy : f.oy);
  Item *it = add(I_SPRITE, tx, ty, tx + f.w, ty + f.h);
  if (!it) return;
  it->x = (int16_t)tx, it->y = (int16_t)ty;
  it->ref = s, it->sub = (uint16_t)i, it->flags = flags, it->alpha = alpha;
  it->color = tint, it->extra = color;
}
void gfx_sprite(uint16_t s, int i, int x, int y, uint8_t flags, uint8_t alpha) { sprite_item(s, i, x, y, flags, 0, false, alpha); }
void gfx_sprite_color(uint16_t s, int i, int x, int y, uint8_t flags, uint16_t color, uint8_t alpha) {
  sprite_item(s, i, x, y, flags, color, true, alpha);
}

/* scaled and turned: the bounds of the turned rectangle (sin and cos in 1/256 steps of a turn) */
static const int16_t sin_q[65] = {
  0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369, 392, 415, 438, 460, 483, 505, 526, 548,
  569, 590, 610, 630, 650, 669, 688, 706, 724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878, 891, 903, 915, 926,
  936, 946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023, 1024, 1024};
int gfx_sin(int a) {   /* a in 1/256 turn, result in 1/1024 */
  a &= 255;
  if (a < 64) return sin_q[a];
  if (a < 128) return sin_q[128 - a];
  if (a < 192) return -sin_q[a - 128];
  return -sin_q[256 - a];
}
int gfx_cos(int a) { return gfx_sin(a + 64); }

void gfx_sprite_ex(uint16_t s, int i, int x, int y, int scale_x, int scale_y, int angle, uint16_t color, uint8_t alpha) {
  Frame f;
  if (!alpha || !frame_get(s, i, &f) || !scale_x || !scale_y) return;
  if (!(angle & 255) && scale_x == 256 && scale_y == 256) {
    sprite_item(s, i, x, y, 0, color, color != 0, alpha);
    return;
  }
  /* the corners, relative to the origin, scaled then turned */
  int c = gfx_cos(angle), sn = gfx_sin(angle);
  int xs[4] = {-f.ox, f.w - f.ox, -f.ox, f.w - f.ox}, ys[4] = {-f.oy, -f.oy, f.h - f.oy, f.h - f.oy};
  int minx = 1 << 30, maxx = -(1 << 30), miny = 1 << 30, maxy = -(1 << 30);
  for (int k = 0; k < 4; k++) {
    int px = xs[k] * scale_x / 256, py = ys[k] * scale_y / 256;
    int rx = (px * c - py * sn) / 1024, ry = (px * sn + py * c) / 1024;
    if (rx < minx) minx = rx;
    if (rx > maxx) maxx = rx;
    if (ry < miny) miny = ry;
    if (ry > maxy) maxy = ry;
  }
  Item *it = add(I_SPRITE_EX, x + minx - 1, y + miny - 1, x + maxx + 2, y + maxy + 2);
  if (!it) return;
  it->x = (int16_t)x, it->y = (int16_t)y;
  it->a = (int16_t)scale_x, it->b = (int16_t)scale_y;
  it->ref = s, it->sub = (uint16_t)i, it->alpha = alpha;
  it->color = color != 0, it->extra = color;
  it->seed = (uint32_t)angle;
}

void gfx_tiles(int wx, int wy, bool front) {
  Item *it = add(I_TILES, 0, 0, SCREEN_W, SCREEN_H);
  if (it) it->x = (int16_t)wx, it->y = (int16_t)wy, it->flags = front;
}

void gfx_line(int x0, int y0, int x1, int y1, uint16_t color) {
  Item *it = add(I_LINE, (x0 < x1 ? x0 : x1), (y0 < y1 ? y0 : y1), (x0 > x1 ? x0 : x1) + 1, (y0 > y1 ? y0 : y1) + 1);
  if (!it) return;
  it->x = (int16_t)x0, it->y = (int16_t)y0, it->a = (int16_t)x1, it->b = (int16_t)y1, it->color = color;
}

void gfx_circle(int cx, int cy, int r, uint16_t color, uint8_t alpha) {
  Item *it = add(I_CIRCLE, cx - r, cy - r, cx + r + 1, cy + r + 1);
  if (!it) return;
  it->x = (int16_t)cx, it->y = (int16_t)cy, it->a = (int16_t)r, it->color = color, it->alpha = alpha;
}

/* ---------------------------------------------------------------- fonts */
typedef struct {
  uint8_t height, first, count, flags;
  const uint8_t *glyphs;    /* 8 bytes each: advance, w, h, ox, oy, pad, bitmap offset (u16) */
  const uint8_t *bits;
} Font;
static void font_get(int font, Font *f) {
  const uint8_t *s = section(SEC_FONTS);
  const uint8_t *p = s + rd32(s + 4 + 4 * font);
  f->height = p[0], f->first = p[1], f->count = p[2], f->flags = p[3];
  f->glyphs = p + 4;
  f->bits = p + 4 + 8 * f->count;
}
static const uint8_t *glyph(const Font *f, uint8_t c) {
  if (c < f->first || c >= f->first + f->count) c = '?';
  if (c < f->first || c >= f->first + f->count) return NULL;
  return f->glyphs + 8 * (c - f->first);
}
int font_height(int font) {
  Font f;
  font_get(font, &f);
  return f.height;
}
int gfx_text_width(int font, const char *s, int n) {
  Font f;
  font_get(font, &f);
  int w = 0, best = 0;
  for (int i = 0; i < n && s[i]; i++) {
    if (s[i] == '\n') {
      if (w > best) best = w;
      w = 0;
      continue;
    }
    const uint8_t *g = glyph(&f, (uint8_t)s[i]);
    if (g) w += g[0];
  }
  return w > best ? w : best;
}

int gfx_text_ex(int font, int x, int y, const char *s, int n, uint16_t color, int shake, uint32_t seed, int spacing) {
  int len = 0;
  while (len < n && s[len]) len++;
  if (!len) return 0;
  Font f;
  font_get(font, &f);
  int lines = 1;
  for (int i = 0; i < len; i++) lines += s[i] == '\n';
  int w = spacing >= GFX_FIXED ? len * (spacing - GFX_FIXED) + 8 : gfx_text_width(font, s, len) + len * spacing;
  if (npool + len > GFX_TEXT_POOL) {
    dropped_now++;
    return w;
  }
  Item *it = add(I_TEXT, x - shake - 2, y - shake - 4, x + w + shake + 4, y + lines * (f.height + 2) + shake + 4);
  if (!it) return w;
  memcpy(pool + npool, s, (size_t)len);
  it->ref = (uint16_t)npool, it->sub = (uint16_t)len;
  npool += len;
  it->x = (int16_t)x, it->y = (int16_t)y, it->font = (uint8_t)font, it->color = color;
  it->a = (int16_t)shake, it->b = (int16_t)spacing, it->seed = seed;
  return w;
}
int gfx_text(int font, int x, int y, const char *s, uint16_t color, uint8_t alpha) {
  (void)alpha;
  return gfx_text_ex(font, x, y, s, 0x7FFF, color, 0, 0, 0);
}

/* ---------------------------------------------------------------- rasterising */
static inline uint16_t blend(uint16_t d, uint16_t s, uint32_t a) {   /* a: 0..32 */
  uint32_t dd = (d | (uint32_t)d << 16) & 0x07E0F81Fu;
  uint32_t ss = (s | (uint32_t)s << 16) & 0x07E0F81Fu;
  uint32_t r = (dd + (((ss - dd) * a) >> 5)) & 0x07E0F81Fu;
  return (uint16_t)(r | r >> 16);
}
static inline uint32_t a32(uint8_t a) { return (a * 32u + 127) / 255; }

static void rast_rect(const Item *it, int by0, int by1) {
  int y0 = it->y0 > by0 ? it->y0 : by0, y1 = it->y1 < by1 ? it->y1 : by1;
  uint32_t a = a32(it->alpha);
  for (int y = y0; y < y1; y++) {
    uint16_t *row = band + (y - by0) * SCREEN_W;
    if (a >= 32)
      for (int x = it->x0; x < it->x1; x++) row[x] = it->color;
    else
      for (int x = it->x0; x < it->x1; x++) row[x] = blend(row[x], it->color, a);
  }
}

static inline uint32_t px_at(const uint8_t *p, int k, int bpp) {
  switch (bpp) {
    case 8: return p[k];
    case 4: return (p[k >> 1] >> ((k & 1) * 4)) & 15;
    case 2: return (p[k >> 2] >> ((k & 3) * 2)) & 3;
    case 1: return (p[k >> 3] >> (k & 7)) & 1;
    default: return 0;
  }
}

static void rast_sprite(const Item *it, int by0, int by1) {
  Frame f;
  if (!frame_get(it->ref, it->sub, &f)) return;
  int y0 = it->y0 > by0 ? it->y0 : by0, y1 = it->y1 < by1 ? it->y1 : by1;
  uint32_t a = a32(it->alpha);
  bool fx = it->flags & GFX_FLIPX, fy = it->flags & GFX_FLIPY;
  uint16_t pal[256];
  for (int k = 0; k < f.ncol; k++) pal[k] = it->color ? it->extra : rd16(f.pal + 2 * k);
  int cx0 = it->x0, cx1 = it->x1;
  for (int y = y0; y < y1; y++) {
    int r = y - it->y;
    if (fy) r = f.h - 1 - r;
    const uint8_t *p = f.data + rd16(f.rows + 2 * r);
    uint16_t *row = band + (y - by0) * SCREEN_W;
    int x = 0;
    while (x < f.w) {
      int skip = p[0], len = p[1];
      p += 2;
      x += skip;
      for (int k = 0; k < len; k++) {
        int sx = x + k;
        int dx = it->x + (fx ? f.w - 1 - sx : sx);
        if (dx < cx0 || dx >= cx1) continue;
        uint16_t c = pal[px_at(p, k, f.bpp)];
        row[dx] = a >= 32 ? c : blend(row[dx], c, a);
      }
      p += (len * f.bpp + 7) >> 3;
      x += len;
    }
  }
}

/* one row of a frame, unpacked: index + 1 for each pixel, 0 where clear */
static void frame_row(const Frame *f, int r, uint8_t *out) {
  memset(out, 0, f->w);
  const uint8_t *p = f->data + rd16(f->rows + 2 * r);
  int x = 0;
  while (x < f->w) {
    int skip = p[0], len = p[1];
    p += 2;
    x += skip;
    for (int k = 0; k < len && x + k < f->w; k++) out[x + k] = (uint8_t)(px_at(p, k, f->bpp) + 1);
    p += (len * f->bpp + 7) >> 3;
    x += len;
  }
}

static void rast_sprite_ex(const Item *it, int by0, int by1) {
  Frame f;
  if (!frame_get(it->ref, it->sub, &f) || f.w > 640) return;
  int y0 = it->y0 > by0 ? it->y0 : by0, y1 = it->y1 < by1 ? it->y1 : by1;
  uint32_t a = a32(it->alpha);
  uint16_t pal[256];
  for (int k = 0; k < f.ncol; k++) pal[k] = it->color ? it->extra : rd16(f.pal + 2 * k);
  int c = gfx_cos((int)it->seed), s = gfx_sin((int)it->seed);
  /* inverse: source = R(-angle) (screen - origin) / scale, in 1/1024 */
  static uint8_t rowbuf[640];
  int cached = -1;
  for (int y = y0; y < y1; y++) {
    uint16_t *row = band + (y - by0) * SCREEN_W;
    int dy = y - it->y;
    for (int x = it->x0; x < it->x1; x++) {
      int dx = x - it->x;
      int ux = (dx * c + dy * s), uy = (-dx * s + dy * c);   /* x 1024 */
      int sx = (int)((int64_t)ux * 256 / ((int64_t)it->a * 1024)) + f.ox;
      int sy = (int)((int64_t)uy * 256 / ((int64_t)it->b * 1024)) + f.oy;
      if (ux < 0 && (int64_t)ux * 256 % ((int64_t)it->a * 1024)) sx--;
      if (uy < 0 && (int64_t)uy * 256 % ((int64_t)it->b * 1024)) sy--;
      if (sx < 0 || sy < 0 || sx >= f.w || sy >= f.h) continue;
      if (sy != cached) frame_row(&f, sy, rowbuf), cached = sy;
      uint8_t v = rowbuf[sx];
      if (!v) continue;
      uint16_t col = pal[v - 1];
      row[x] = a >= 32 ? col : blend(row[x], col, a);
    }
  }
}

static void rast_tiles(const Item *it, int by0, int by1) {
  const RoomInfo *r = world_room_info();
  if (!r) return;
  const uint8_t *grid = it->flags ? r->fgrid : r->grid;
  if (!grid) return;
  int y0 = it->y0 > by0 ? it->y0 : by0, y1 = it->y1 < by1 ? it->y1 : by1;
  int gw = r->gcols * 20, gh = r->grows * 20;
  for (int y = y0; y < y1; y++) {
    int gy = it->y + y;
    if (gy < 0 || gy >= gh) continue;
    int trow = gy / 20, ty = gy % 20;
    uint16_t *row = band + (y - by0) * SCREEN_W;
    int x = it->x0;
    while (x < it->x1) {
      int gx = it->x + x;
      if (gx < 0) {
        x += -gx;
        continue;
      }
      if (gx >= gw) break;
      int tcol = gx / 20, tx = gx % 20;
      int n = 20 - tx;
      if (x + n > it->x1) n = it->x1 - x;
      uint16_t id = rd16(grid + 2 * (trow * r->gcols + tcol));
      if (id != 0xFFFF) {
        Tile t;
        tile_get(id, &t);
        int base = ty * 20 + tx;
        if (t.bpp == 0) {
          uint16_t c = rd16(t.pal);
          for (int k = 0; k < n; k++) row[x + k] = c;
        } else if (t.clear) {
          for (int k = 0; k < n; k++) {
            uint32_t v = px_at(t.bits, base + k, t.bpp);
            if (v) row[x + k] = rd16(t.pal + 2 * v);
          }
        } else if (t.bpp == 8) {
          for (int k = 0; k < n; k++) row[x + k] = rd16(t.pal + 2 * t.bits[base + k]);
        } else {
          uint16_t pal[16];
          int nc = t.ncol > 16 ? 16 : t.ncol;
          for (int k = 0; k < nc; k++) pal[k] = rd16(t.pal + 2 * k);
          for (int k = 0; k < n; k++) row[x + k] = pal[px_at(t.bits, base + k, t.bpp)];
        }
      }
      x += n;
    }
  }
}

/* a pseudo-random shake for letter i of a text */
static int shake_of(uint32_t seed, int i, int axis, int amount) {
  uint32_t h = seed * 2654435761u ^ (uint32_t)(i * 2 + axis) * 40503u;
  h ^= h >> 13, h *= 0x5bd1e995u, h ^= h >> 15;
  return (int)(h % (uint32_t)(2 * amount + 1)) - amount;
}

static void rast_text(const Item *it, int by0, int by1) {
  Font f;
  font_get(it->font, &f);
  const char *s = pool + it->ref;
  int x = it->x, y = it->y;
  for (int i = 0; i < it->sub; i++) {
    uint8_t ch = (uint8_t)s[i];
    if (ch == '\n') {
      x = it->x, y += f.height + 2;
      continue;
    }
    const uint8_t *g = glyph(&f, ch);
    if (!g) continue;
    int gw = g[1], gh = g[2], gx = x + (int8_t)g[3], gy = y + (int8_t)g[4];
    if (it->a) gx += shake_of(it->seed, i, 0, it->a), gy += shake_of(it->seed, i, 1, it->a);
    x += it->b >= GFX_FIXED ? it->b - GFX_FIXED : g[0] + it->b;
    if (gy >= by1 || gy + gh <= by0) continue;
    const uint8_t *bits = f.bits + rd16(g + 6);
    int stride = (gw + 7) >> 3;
    for (int r = 0; r < gh; r++) {
      int yy = gy + r;
      if (yy < by0 || yy >= by1 || yy < it->y0 || yy >= it->y1) continue;
      uint16_t *row = band + (yy - by0) * SCREEN_W;
      const uint8_t *b = bits + r * stride;
      for (int c = 0; c < gw; c++)
        if (b[c >> 3] & (0x80 >> (c & 7))) {
          int xx = gx + c;
          if (xx >= it->cx0 && xx < it->cx1 && xx >= 0 && xx < SCREEN_W) row[xx] = it->color;
        }
    }
  }
}

static void rast_line(const Item *it, int by0, int by1) {
  int x0 = it->x, y0 = it->y, x1 = it->a, y1 = it->b;
  int dx = x1 > x0 ? x1 - x0 : x0 - x1, dy = y1 > y0 ? y0 - y1 : y1 - y0;
  int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  for (;;) {
    if (y0 >= by0 && y0 < by1 && x0 >= it->x0 && x0 < it->x1 && y0 >= it->y0 && y0 < it->y1) band[(y0 - by0) * SCREEN_W + x0] = it->color;
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}

static void rast_circle(const Item *it, int by0, int by1) {
  int y0 = it->y0 > by0 ? it->y0 : by0, y1 = it->y1 < by1 ? it->y1 : by1;
  int r = it->a;
  uint32_t a = a32(it->alpha);
  for (int y = y0; y < y1; y++) {
    int dy = y - it->y, w = 0;
    while ((w + 1) * (w + 1) + dy * dy <= r * r + r) w++;
    if (dy * dy > r * r + r) continue;
    int xa = it->x - w, xb = it->x + w + 1;
    if (xa < it->x0) xa = it->x0;
    if (xb > it->x1) xb = it->x1;
    uint16_t *row = band + (y - by0) * SCREEN_W;
    for (int x = xa; x < xb; x++) row[x] = a >= 32 ? it->color : blend(row[x], it->color, a);
  }
}

void gfx_end(void) {
  gfx_dropped = dropped_now;
  if (!band) return;
  for (int by0 = 0; by0 < SCREEN_H; by0 += GFX_BAND) {
    int by1 = by0 + GFX_BAND;
    memset(band, 0, sizeof(uint16_t) * SCREEN_W * GFX_BAND);
    for (int i = 0; i < nitems; i++) {
      const Item *it = &items[i];
      if (it->y1 <= by0 || it->y0 >= by1) continue;
      switch (it->kind) {
        case I_RECT: rast_rect(it, by0, by1); break;
        case I_SPRITE: rast_sprite(it, by0, by1); break;
        case I_SPRITE_EX: rast_sprite_ex(it, by0, by1); break;
        case I_TILES: rast_tiles(it, by0, by1); break;
        case I_TEXT: rast_text(it, by0, by1); break;
        case I_LINE: rast_line(it, by0, by1); break;
        case I_CIRCLE: rast_circle(it, by0, by1); break;
      }
    }
    plat_push(0, by0, SCREEN_W, GFX_BAND, band);
  }
}
