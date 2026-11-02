/* Cat Ninja's drawing, in the launcher's strips of 16 rows (launcher/src/gfx.c).
 *
 * The original draws an 800 x 600 stage: Main.world turned and sized about the middle (gravity changers turn
 * it, the hallucination pill wobbles it), and in it Main.screen, the camera, with the map's two layers (the
 * grid of tiles behind, untinted, and the tiles themselves, tinted by the map's ColourChanger), the
 * particles between the two, then the things, the trails and the cat. Here the world is drawn at half size
 * (its 20 pixel tiles 10 wide: 640 x 480 of it shows) with every pixel found back in the map, so any turn
 * works; the stage's texts (the title cards, the messages, the counts) at 0.4 (320 / 800). */
#include <math.h>
#include <string.h>
#include "cn.h"
#include "gfx.h"

static color_t pal_plain[256], pal_map[256], pal_cat[256];
static uint8_t pal_a[256];
static uint8_t grid_tex[92];
int cat_parts[5][4]; /* PixelModifier.breakData(characteridle, 5): five pieces of the cat (x, y, w, h) */
static double M00, M01, M10, M11, I00, I01, I10, I11;
#define CX 160
#define CY 120
#define ZOOM 0.5
static uint8_t fgmask[SCREEN_W * STRIP_H];
extern double boss_csx_get(void), boss_width(void), boss_height(void);
extern int env_dirts(const uint8_t **o), env_crates(const uint8_t **o), env_falls(const uint8_t **o),
    env_buttons(const uint8_t **o), env_spheres(const uint8_t **o), env_bombs(const bomb_t **b);

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

void draw_init(void) {
  for (int i = 0; i < 256; i++) {
    uint32_t c = cn_pal[i];
    pal_plain[i] = gfx_rgb(c & 0xFFFFFF), pal_a[i] = (uint8_t)(c >> 24);
  }
  for (int t = 0; t < 92; t++)
    grid_tex[t] = cn_grid_tile[t] != 255 ? cn_grid_tile[t] : t == 1 || t == 3 || t == 4 ? 255 : cn_grid_tile[0];
  for (int k = 0; k < 5; k++) { /* (in the original's pixels: the cat is 23 x 20) */
    int x = (int)(rnd01() * (23 / 1.5)), y = (int)(rnd01() * (20 / 1.5));
    int w = 5 + (int)(rnd01() * 23 / 1.5), h = 5 + (int)(rnd01() * 20 / 1.5);
    cat_parts[k][0] = x, cat_parts[k][1] = y, cat_parts[k][2] = w > 23 ? 23 : w, cat_parts[k][3] = h > 20 ? 20 : h;
  }
}
static void tint_pal(color_t *out, double r, double g, double b) {
  int mr = (int)(r * 256), mg = (int)(g * 256), mb = (int)(b * 256);
  for (int i = 1; i < 256; i++) {
    uint32_t c = cn_pal[i];
    int cr = (int)((c >> 16 & 255) * mr >> 8), cg = (int)((c >> 8 & 255) * mg >> 8), cb = (int)((c & 255) * mb >> 8);
    out[i] = RGB(cr > 255 ? 255 : cr, cg > 255 ? 255 : cg, cb > 255 ? 255 : cb);
  }
}
static uint32_t tint888(uint32_t c, double r, double g, double b) {
  int cr = (int)((c >> 16 & 255) * r), cg = (int)((c >> 8 & 255) * g), cb = (int)((c & 255) * b);
  return (uint32_t)(imin(cr, 255) << 16 | imin(cg, 255) << 8 | imin(cb, 255));
}

/* the frame's transform: screen = (160, 120) + M (p + camera) */
void draw_frame(void) {
  double r = world_rot * M_PI / 180, c = cos(r), s = sin(r);
  M00 = ZOOM * world_sx * c, M01 = -ZOOM * world_sy * s, M10 = ZOOM * world_sx * s, M11 = ZOOM * world_sy * c;
  double det = M00 * M11 - M01 * M10;
  I00 = M11 / det, I01 = -M01 / det, I10 = -M10 / det, I11 = M00 / det;
  tint_pal(pal_map, map_tint.r, map_tint.g, map_tint.b);
  tint_pal(pal_cat, P.tint.r, P.tint.g, P.tint.b);
}
static void to_screen(double x, double y, double *sx, double *sy) {
  x += screen_x, y += screen_y;
  *sx = CX + M00 * x + M01 * y, *sy = CY + M10 * x + M11 * y;
}

/* ---------------------------------------------------------------- the map's two layers */
static inline color_t tile_px(int i, int k, uint8_t *mask) {
  int t = grid[i] & 0x7F;
  int f = cn_fg_tile[t];
  if (t >= 2 && t <= 7 && (blood[i >> 3] >> (i & 7) & 1) && cn_blood_tile[t] != 255) f = cn_blood_tile[t];
  int g = grid_tex[t];
  color_t back = g != 255 ? pal_plain[cn_tiles[g * CN_TILE_PX + k]] : 0;
  *mask = 0;
  if (f != 255) {
    int idx = cn_tiles[f * CN_TILE_PX + k];
    if (idx) {
      int a = pal_a[idx];
      if (a >= CN_ALPHA) {
        *mask = 1;
        return pal_map[idx];
      }
      *mask = a >= CN_ALPHA / 2;
      return gfx_mix(back, pal_map[idx], a * 32 / CN_ALPHA);
    }
  }
  return back;
}
/* n pixels of one tile's row, from texel k0 */
static void tile_span(int i, int k0, int n, color_t *o, uint8_t *m) {
  int t = grid[i] & 0x7F, f = cn_fg_tile[t], g = grid_tex[t];
  if (t >= 2 && t <= 7 && (blood[i >> 3] >> (i & 7) & 1) && cn_blood_tile[t] != 255) f = cn_blood_tile[t];
  const uint8_t *gp = g != 255 ? cn_tiles + g * CN_TILE_PX + k0 : 0;
  if (f == 255) {
    if (gp)
      for (int k = 0; k < n; k++) o[k] = pal_plain[gp[k]];
    else
      memset(o, 0, (size_t)n * sizeof *o);
    memset(m, 0, (size_t)n);
    return;
  }
  const uint8_t *fp = cn_tiles + f * CN_TILE_PX + k0;
  for (int k = 0; k < n; k++) {
    int idx = fp[k];
    if (!idx) {
      o[k] = gp ? pal_plain[gp[k]] : 0, m[k] = 0;
      continue;
    }
    int a = pal_a[idx];
    if (a >= CN_ALPHA) {
      o[k] = pal_map[idx], m[k] = 1;
      continue;
    }
    o[k] = gfx_mix(gp ? pal_plain[gp[k]] : 0, pal_map[idx], a * 32 / CN_ALPHA), m[k] = a >= CN_ALPHA / 2;
  }
}
static void draw_tiles(void) {
  int W = gw * 10, H = gh * 10;
  bool flat = world_rot == 0 && world_sx == 1 && world_sy == 1;
  for (int Y = gfx_y0; Y < gfx_y1; Y++) {
    color_t *o = gfx_buf + (Y - gfx_y0) * SCREEN_W;
    uint8_t *m = fgmask + (Y - gfx_y0) * SCREEN_W;
    double dx = 0.5 - CX, dy = Y + 0.5 - CY;
    double wx = I00 * dx + I01 * dy - screen_x, wy = I10 * dx + I11 * dy - screen_y; /* (the original's pixels) */
    if (flat) {
      int qy = (int)floor(wy / 2), qx = (int)floor(wx / 2), X = 0;
      if (qy < 0 || qy >= H) {
        memset(o, 0, SCREEN_W * sizeof *o), memset(m, 0, SCREEN_W);
        continue;
      }
      int ty = qy / 10, fy = qy - ty * 10, row = ty * gw;
      while (X < SCREEN_W) {
        if (qx < 0 || qx >= W) {
          o[X] = 0, m[X] = 0, X++, qx++;
          continue;
        }
        int tx = qx / 10, fx = qx - tx * 10, n = imin(10 - fx, SCREEN_W - X);
        tile_span(row + tx, fy * 10 + fx, n, o + X, m + X);
        X += n, qx += n;
      }
      continue;
    }
    int32_t u = (int32_t)floor(wx / 2 * 65536), v = (int32_t)floor(wy / 2 * 65536);
    int32_t du = (int32_t)(I00 / 2 * 65536), dv = (int32_t)(I10 / 2 * 65536);
    for (int X = 0; X < SCREEN_W; X++, u += du, v += dv) {
      int qx = u >> 16, qy = v >> 16;
      if ((unsigned)qx >= (unsigned)W || (unsigned)qy >= (unsigned)H) {
        o[X] = 0, m[X] = 0;
        continue;
      }
      int tx = qx / 10, ty = qy / 10;
      o[X] = tile_px(ty * gw + tx, (qy - ty * 10) * 10 + qx - tx * 10, &m[X]);
    }
  }
}

/* ---------------------------------------------------------------- pictures: any place, turn and size */
enum { PAL_PLAIN, PAL_MAP, PAL_CAT, PAL_RGB };
typedef struct {
  int pic, sx0, sy0, sw, sh; /* the picture, and the part of it (texels) */
  double tsx, tsy;           /* a texel's size, in the original's pixels */
  double ax, ay;             /* the picture's corner from its origin (the original's pixels) */
  double lsx, lsy, lrot;     /* its own size and turn about its origin */
  int pal, alpha32;          /* palette, see-through (32 solid) */
  double tr, tg, tb;         /* PAL_RGB: its colours' multipliers */
  bool under;                /* under the map's tiles (particles) */
} blit_t;
static void blit(const blit_t *b, double ox, double oy) {
  const cn_sprite_t *sp = &cn_sprites[b->pic];
  double r = b->lrot * M_PI / 180, c = cos(r), s = sin(r);
  double L00 = c * b->lsx, L01 = -s * b->lsy, L10 = s * b->lsx, L11 = c * b->lsy;
  double A00 = M00 * L00 + M01 * L10, A01 = M00 * L01 + M01 * L11, A10 = M10 * L00 + M11 * L10, A11 = M10 * L01 + M11 * L11;
  double sx0, sy0;
  to_screen(ox, oy, &sx0, &sy0);
  double t0x = sx0 + A00 * b->ax + A01 * b->ay, t0y = sy0 + A10 * b->ax + A11 * b->ay;
  double T00 = A00 * b->tsx, T01 = A01 * b->tsy, T10 = A10 * b->tsx, T11 = A11 * b->tsy;
  double det = T00 * T11 - T01 * T10;
  if (fabs(det) < 1e-9) return;
  double J00 = T11 / det, J01 = -T01 / det, J10 = -T10 / det, J11 = T00 / det;
  double xs[4] = {0, b->sw, 0, b->sw}, ys[4] = {0, 0, b->sh, b->sh}, x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
  for (int k = 0; k < 4; k++) {
    double X = t0x + T00 * xs[k] + T01 * ys[k], Y = t0y + T10 * xs[k] + T11 * ys[k];
    x0 = fmin(x0, X), x1 = fmax(x1, X), y0 = fmin(y0, Y), y1 = fmax(y1, Y);
  }
  int X0 = imax((int)floor(x0), 0), X1 = imin((int)ceil(x1), SCREEN_W), Y0 = imax((int)floor(y0), gfx_y0),
      Y1 = imin((int)ceil(y1), gfx_y1);
  if (X0 >= X1 || Y0 >= Y1) return;
  const uint8_t *px = cn_sprite_px + sp->at;
  const color_t *pal = b->pal == PAL_MAP ? pal_map : b->pal == PAL_CAT ? pal_cat : pal_plain;
  int32_t di = (int32_t)(J00 * 65536), dj = (int32_t)(J10 * 65536), wmax = b->sw << 16, hmax = b->sh << 16;
  for (int Y = Y0; Y < Y1; Y++) {
    double py = Y + 0.5 - t0y, px0 = X0 + 0.5 - t0x;
    int32_t fi = (int32_t)floor((J00 * px0 + J01 * py) * 65536), fj = (int32_t)floor((J10 * px0 + J11 * py) * 65536);
    color_t *o = gfx_buf + (Y - gfx_y0) * SCREEN_W;
    const uint8_t *m = fgmask + (Y - gfx_y0) * SCREEN_W;
    for (int X = X0; X < X1; X++, fi += di, fj += dj) {
      if (fi < 0 || fj < 0 || fi >= wmax || fj >= hmax) continue;
      if (b->under && m[X]) continue;
      int i = (fi >> 16) + b->sx0, j = (fj >> 16) + b->sy0;
      if (i >= sp->w || j >= sp->h) continue;
      int idx = px[j * sp->w + i];
      if (!idx) continue;
      int a = pal_a[idx] * b->alpha32 / CN_ALPHA;
      color_t c2;
      if (b->pal == PAL_RGB) c2 = gfx_rgb(tint888(cn_pal[idx] & 0xFFFFFF, b->tr, b->tg, b->tb));
      else c2 = pal[idx];
      o[X] = a >= 32 ? c2 : gfx_mix(o[X], c2, a);
    }
  }
}
static blit_t pic_at(int pic, double ax, double ay) {
  const cn_sprite_t *sp = &cn_sprites[pic];
  return (blit_t){pic, 0, 0, sp->w, sp->h, 2, 2, ax, ay, 1, 1, 0, PAL_PLAIN, 32, 1, 1, 1, false};
}

/* ---------------------------------------------------------------- shapes */
static void fill_circle(double cx, double cy, double r, color_t c, int a32) {
  int y0 = imax((int)floor(cy - r), gfx_y0), y1 = imin((int)ceil(cy + r), gfx_y1);
  for (int Y = y0; Y < y1; Y++) {
    double d = Y + 0.5 - cy, w = r * r - d * d;
    if (w <= 0) continue;
    w = sqrt(w);
    int x0 = imax((int)floor(cx - w + 0.5), 0), x1 = imin((int)floor(cx + w + 0.5), SCREEN_W);
    color_t *o = gfx_buf + (Y - gfx_y0) * SCREEN_W;
    for (int X = x0; X < x1; X++) o[X] = a32 >= 32 ? c : gfx_mix(o[X], c, a32);
  }
}
/* a polygon (n points, screen pixels), filled by the even-odd rule */
static void fill_poly(const double *xy, int n, color_t c, int a32, bool under) {
  double ymin = 1e9, ymax = -1e9;
  for (int k = 0; k < n; k++) ymin = fmin(ymin, xy[k * 2 + 1]), ymax = fmax(ymax, xy[k * 2 + 1]);
  int y0 = imax((int)floor(ymin), gfx_y0), y1 = imin((int)ceil(ymax), gfx_y1);
  for (int Y = y0; Y < y1; Y++) {
    double sy = Y + 0.5, xs[16];
    int nx = 0;
    for (int k = 0; k < n && nx < 16; k++) {
      double ax = xy[k * 2], ay = xy[k * 2 + 1], bx = xy[(k + 1) % n * 2], by = xy[(k + 1) % n * 2 + 1];
      if ((ay <= sy && by > sy) || (by <= sy && ay > sy)) xs[nx++] = ax + (sy - ay) * (bx - ax) / (by - ay);
    }
    for (int i = 1; i < nx; i++)
      for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) {
        double t = xs[j];
        xs[j] = xs[j - 1], xs[j - 1] = t;
      }
    color_t *o = gfx_buf + (Y - gfx_y0) * SCREEN_W;
    const uint8_t *m = fgmask + (Y - gfx_y0) * SCREEN_W;
    for (int i = 0; i + 1 < nx; i += 2) {
      int x0 = imax((int)floor(xs[i] + 0.5), 0), x1 = imin((int)floor(xs[i + 1] + 0.5), SCREEN_W);
      for (int X = x0; X < x1; X++)
        if (!under || !m[X]) o[X] = a32 >= 32 ? c : gfx_mix(o[X], c, a32);
    }
  }
}
static void line(double ax, double ay, double bx, double by, color_t c, int a32) {
  double dx = bx - ax, dy = by - ay;
  int n = (int)fmax(fabs(dx), fabs(dy)) + 1;
  for (int k = 0; k <= n; k++) {
    int X = (int)floor(ax + dx * k / n), Y = (int)floor(ay + dy * k / n);
    if (X >= 0 && X < SCREEN_W && Y >= gfx_y0 && Y < gfx_y1) {
      color_t *o = gfx_buf + (Y - gfx_y0) * SCREEN_W + X;
      *o = a32 >= 32 ? c : gfx_mix(*o, c, a32);
    }
  }
}

/* ---------------------------------------------------------------- texts: the game's fonts, in runs */
/* a glyph, turned a quarter turn `rot` times about the pen */
static void glyph(const np_font_t *f, const np_glyph_t *g, int px, int py, int rot, color_t c, int a32) {
  const uint8_t *d = f->data + g->offset;
  int r = rot & 3;
  /* the glyph's rows on the screen: skip it when it misses this strip */
  int top, bottom;
  if (r == 0) top = py + g->y, bottom = top + g->h;
  else if (r == 2) bottom = py - g->y, top = bottom - g->h;
  else if (r == 1) top = py + g->x, bottom = top + g->w;
  else bottom = py - g->x, top = bottom - g->w;
  if (bottom <= gfx_y0 || top >= gfx_y1) return;
  for (int j = 0; j < g->h; j++)
    for (int i = 0; i < g->w; i++) {
      int k = j * g->w + i, v = (d[k >> 1] >> ((k & 1) * 4)) & 15;
      if (!v) continue;
      int lx = g->x + i, ly = g->y + j, X, Y;
      switch (r) {
      case 0: X = px + lx, Y = py + ly; break;
      case 1: X = px - ly - 1, Y = py + lx; break;
      case 2: X = px - lx - 1, Y = py - ly - 1; break;
      default: X = px + ly, Y = py - lx - 1; break;
      }
      if (X < 0 || X >= SCREEN_W || Y < gfx_y0 || Y >= gfx_y1) continue;
      color_t *o = gfx_buf + (Y - gfx_y0) * SCREEN_W + X;
      int a = v * 2 * a32 / 30;
      *o = a >= 32 ? c : gfx_mix(*o, c, a);
    }
}
static const np_glyph_t *glyph_of(const np_font_t *f, unsigned char ch) {
  if (ch < f->first || ch >= f->first + f->count) return 0;
  return &f->glyphs[ch - f->first];
}
typedef struct {
  int at, end, w, asc, desc, font, colour;
} tline_t;
/* the text's lines, wrapped at `width` pixels (0: no wrapping) */
static int text_lines(const char *s, int width, tline_t *lines, int max) {
  int n = 0, font = 0, colour = 0, i = 0;
  while (s[i] && n < max) {
    tline_t *l = &lines[n];
    l->at = i, l->w = 0, l->asc = 0, l->desc = 0, l->font = font, l->colour = colour;
    int brk = -1, brk_w = 0, brk_font = font, brk_colour = colour, w = 0;
    for (;;) {
      unsigned char ch = (unsigned char)s[i];
      if (!ch || ch == '\n') {
        l->end = i, l->w = w;
        if (ch) i++;
        break;
      }
      if (ch == 1) {
        font = (unsigned char)s[i + 1], i += 2;
        continue;
      }
      if (ch == 2) {
        colour = (unsigned char)s[i + 1], i += 2;
        continue;
      }
      const np_font_t *f = &cn_fonts[font];
      l->asc = imax(l->asc, f->ascent), l->desc = imax(l->desc, f->descent);
      const np_glyph_t *g = glyph_of(f, ch >= 'a' && ch <= 'z' && !glyph_of(f, ch) ? ch - 32 : ch);
      int adv = g ? g->advance : 0;
      if (width && w + adv > width && brk > l->at && ch != ' ') { /* back to the last space */
        l->end = brk, l->w = brk_w;
        i = brk, font = brk_font, colour = brk_colour;
        while (s[i] == ' ') i++;
        break;
      }
      w += adv, i++;
      if (ch == ' ') brk = i, brk_w = w - adv, brk_font = font, brk_colour = colour;
    }
    if (!l->asc) l->asc = cn_fonts[font].ascent, l->desc = cn_fonts[font].descent;
    n++;
  }
  return n;
}
/* the text centred on (cx, cy) or with its top at y0 (cy < 0: then y0 is its top), turned rot quarter turns */
static void text_draw(int text, double cx, double top, int width, int rot, int a32, bool outline, bool centre_v,
                      int dir_x) {
  static tline_t lines[12];
  const char *s = cn_text_data + cn_text_at[text];
  int n = text_lines(s, width, lines, 12), h = 0;
  for (int k = 0; k < n; k++) h += lines[k].asc + lines[k].desc;
  double y = centre_v ? -h / 2.0 : 0;
  static const int8_t ux[4] = {1, 0, -1, 0}, uy[4] = {0, 1, 0, -1};
  int r = rot & 3;
  /* (a quick look: which rows the text could cover) */
  double reach = fmax(h, width ? width : 320) / 2.0 + 30;
  if (r == 0 && !centre_v && (top + h + 4 < gfx_y0 || top - 4 > gfx_y1)) return;
  if (centre_v && (top + reach < gfx_y0 || top - reach > gfx_y1)) return;
  int icx = (int)floor(cx), itop = (int)floor(top);
  for (int k = 0; k < n; k++) {
    tline_t *l = &lines[k];
    int lx = dir_x < 0 ? 0 : -l->w / 2, ly = (int)floor(y) + l->asc; /* the pen, in the text's own frame */
    int font = l->font, colour = l->colour;
    if (r == 0 && (itop + ly + l->desc + 2 <= gfx_y0 || itop + ly - l->asc - 2 >= gfx_y1)) {
      y += l->asc + l->desc;
      continue;
    }
    for (int i = l->at; i < l->end; i++) {
      unsigned char ch = (unsigned char)s[i];
      if (ch == 1) {
        font = (unsigned char)s[++i];
        continue;
      }
      if (ch == 2) {
        colour = (unsigned char)s[++i];
        continue;
      }
      const np_font_t *f = &cn_fonts[font];
      const np_glyph_t *g = glyph_of(f, ch);
      if (!g || (!g->w && !g->advance)) g = glyph_of(f, ch >= 'a' && ch <= 'z' ? ch - 32 : ch);
      if (!g) continue;
      int px = icx + lx * ux[r] - ly * uy[r], py = itop + lx * uy[r] + ly * ux[r];
      if (g->w) {
        color_t c = gfx_rgb(cn_colours[colour]);
        if (outline)
          for (int d = 0; d < 8; d++) {
            static const int8_t ox[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, oy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
            glyph(f, g, px + ox[d], py + oy[d], r, 0, a32);
          }
        glyph(f, g, px, py, r, c, a32);
      }
      lx += g->advance;
    }
    y += l->asc + l->desc;
  }
}

/* ---------------------------------------------------------------- the boss (drawboss: its shape, wobbling) */
static void draw_boss(void) {
  static int pts[BOSS_POINTS * 2];
  static double sxy[BOSS_POINTS * 2 + 2];
  boss_draw_points(pts);
  double w = boss_width(), h = boss_height(), csx = boss_csx_get();
  for (int k = 0; k <= BOSS_POINTS; k++) {
    double px = k < BOSS_POINTS ? pts[k * 2] : 0, py = k < BOSS_POINTS ? pts[k * 2 + 1] : 0;
    /* base.x = -width / 2 (its left, x0, ends there), then the container's mirror, then the boss's size */
    double bx = (px - w / 2) * csx * boss.sx, by = (py - h / 2) * boss.sy;
    to_screen(boss.x + bx, boss.y + by, &sxy[k * 2], &sxy[k * 2 + 1]);
  }
  static const uint32_t colours[4] = {0x1E9ADD, 0xD67A1F, 0x8C0003, 0x000000};
  int a = (int)(boss.alpha * 32);
  if (a <= 0) return;
  color_t body = gfx_rgb(colours[boss.id & 3]), red = HEX(0xFF0000), dark = HEX(0xA80004), black = 0;
  double poly[10];
  /* the first shape: from the pen's start (0, 0) through points 0 to 3 */
  poly[0] = sxy[BOSS_POINTS * 2], poly[1] = sxy[BOSS_POINTS * 2 + 1];
  for (int k = 0; k < 4; k++) poly[2 + k * 2] = sxy[k * 2], poly[3 + k * 2] = sxy[k * 2 + 1];
  fill_poly(poly, 5, body, a, false);
  for (int k = 0; k < 5; k++) line(poly[k * 2], poly[k * 2 + 1], poly[(k + 1) % 5 * 2], poly[(k + 1) % 5 * 2 + 1], black, a);
  for (int m = 1; m < BOSS_POINTS / 4; m++) {
    const double *q = sxy + m * 8;
    if (m <= 10 || m >= 22) {
      fill_poly(q, 4, m <= 10 ? body : dark, a, false);
      for (int k = 0; k < 4; k++) line(q[k * 2], q[k * 2 + 1], q[(k + 1) % 4 * 2], q[(k + 1) % 4 * 2 + 1], m <= 10 ? black : red, a);
    } else {
      for (int k = 0; k < 3; k++) line(q[k * 2], q[k * 2 + 1], q[k * 2 + 2], q[k * 2 + 3], red, a);
    }
  }
}

/* ---------------------------------------------------------------- particles (effects/Particle: under the tiles) */
static void draw_particles(void) {
  static const uint32_t cols[7][2] = {{0, 0},           {0xFF0000, 0x940802}, {0x006BD7, 0x58A6F4}, {0x7028B0, 0x9855D3},
                                      {0xAC590D, 0x6E3A0D}, {0x808080, 0xD9D9D9}, {0xED7DFA, 0xCC24F7}};
  double z = ZOOM * world_sx;
  for (int i = 0; i < n_particles; i++) {
    particle_t *p = &particles[i];
    int a = (int)(p->alpha * 32);
    if (a <= 0) continue;
    if (p->type == 0) {
      blit_t b = pic_at(SPR_CAT_IDLE, -p->pw / 2.0, -p->ph / 2.0);
      b.sx0 = p->px0 / 2, b.sy0 = p->py0 / 2, b.sw = (p->pw + 1) / 2, b.sh = (p->ph + 1) / 2;
      b.lrot = p->rot, b.pal = PAL_CAT, b.alpha32 = a, b.under = true;
      blit(&b, p->x, p->y);
      continue;
    }
    double sx, sy, s = p->size * z;
    to_screen(p->x, p->y, &sx, &sy);
    if (sy + s < gfx_y0 - 2 || sy - s > gfx_y1 + 2 || sx < -10 || sx > SCREEN_W + 10) continue;
    color_t c1 = gfx_rgb(cols[p->type][0]), c2 = gfx_rgb(cols[p->type][1]);
    if (p->type == 5) { /* a shard: a triangle up from its place */
      double e = (2 + p->size) * z, q[6] = {sx, sy, sx + e, sy + e, sx - e, sy + e};
      fill_poly(q, 3, gfx_mix(c1, c2, 16), a, true);
      continue;
    }
    double q[8] = {sx, sy, sx + s, sy, sx + s, sy + s, sx, sy + s};
    if (s < 1.5) q[2] = q[4] = sx + 1.5, q[5] = q[7] = sy + 1.5;
    fill_poly(q, 4, s >= 3 ? c1 : gfx_mix(c1, c2, 16), a, true);
    if (s >= 3) {
      double r2[8] = {sx + s / 2, sy, sx + s, sy, sx + s, sy + s, sx + s / 2, sy + s};
      fill_poly(r2, 4, c2, a, true);
    }
  }
}

/* ---------------------------------------------------------------- the world */
static int snap_rot(double r) { /* a turn to the nearest quarter (texts stay readable) */
  int q = (int)floor((r + 45) / 90);
  return ((q % 4) + 4) % 4;
}
static void draw_screens(void) {
  for (int i = 0; i < n_screens; i++) {
    screen_t *s = &screens[i];
    blit_t b = pic_at(SPR_TV, -96, -48);
    b.pal = PAL_MAP;
    blit(&b, s->x + 60, s->y + 30);
    if (s->text >= 0) {
      double cx, cy;
      to_screen(s->x + 60, s->y + 30, &cx, &cy);
      text_draw(s->text, cx, cy, 88, snap_rot(world_rot + s->rot), 32, false, true, 0);
    }
  }
}
static void draw_things(void) {
  const uint8_t *o;
  int n;
  for (int i = 0; i < n_mines; i++) {
    blit_t b = pic_at(mines[i].pic, 0, 0);
    blit(&b, mines[i].x, mines[i].y);
  }
  n = env_buttons(&o);
  for (int i = 0; i < n; i++) {
    button_t *t = &buttons[o[i]];
    blit_t b = pic_at(t->pic, 0, 0);
    b.lsx = t->sx, b.lsy = t->sy;
    blit(&b, t->x, t->y);
  }
  n = env_dirts(&o);
  for (int i = 0; i < n; i++) {
    dirt_t *d = &dirts[o[i]];
    blit_t b = pic_at(SPR_DIRT_0 + (d->playing ? 1 + d->frame : 0), 0, 0);
    blit(&b, d->x, d->y);
  }
  n = env_crates(&o);
  for (int i = 0; i < n; i++) {
    blit_t b = pic_at(SPR_CRATE, 0, 0);
    blit(&b, crates[o[i]].x, crates[o[i]].y);
  }
  for (int i = 0; i < n_shooters; i++) {
    shooter_t *s = &shooters[i];
    blit_t b = pic_at((s->homing ? SPR_HOMING_0_0 : SPR_SHIELD_0_0) + s->orient * 4 + s->frame, 0, 0);
    blit(&b, s->x, s->y);
  }
  n = env_spheres(&o);
  for (int i = 0; i < n; i++) {
    sphere_t *s = &spheres[o[i]];
    blit_t b = pic_at(SPR_SPHERE_0 + s->frame, 0, 0);
    blit(&b, s->x, s->y);
  }
  n = env_falls(&o);
  for (int i = 0; i < n; i++) {
    fall_t *f = &falls[o[i]];
    blit_t b = pic_at(SPR_FALLSPIKE_0 + f->dir, 0, 0);
    b.pal = PAL_MAP;
    blit(&b, f->x, f->y);
  }
  for (int i = 0; i < n_suns; i++) {
    blit_t b = pic_at(SPR_SUNSPIKE, 0, 0);
    blit(&b, suns[i].x, suns[i].y);
  }
  if (big.on) {
    blit_t b = pic_at(SPR_BIGSPHERE_0 + big.frame, -83.5, -163);
    b.tsx = 167 / 42.0, b.tsy = 163 / 41.0, b.lsx = b.lsy = big.scale;
    blit(&b, big.x, big.y);
  }
}
static void draw_trails(void) {
  for (int i = 0; i < n_trails; i++) {
    trail_t *t = &trails[i];
    int a = (int)(t->alpha * 32 + 0.5);
    if (a <= 0) continue;
    if (t->kind == TR_PIC) {
      const cn_sprite_t *sp = &cn_sprites[t->pic];
      blit_t b = pic_at(t->pic, -sp->w, -sp->h);
      if (t->pic <= SPR_CAT_ROLL) b.ax = -11.5, b.ay = -10;
      b.lrot = t->rot, b.alpha32 = a;
      if (t->tint == 2) b.pal = PAL_RGB, b.tr = t->tr / 32.0, b.tg = t->tg / 32.0, b.tb = t->tb / 32.0;
      blit(&b, t->x, t->y);
      continue;
    }
    double sx, sy, r = t->r * t->scale * ZOOM * world_sx;
    if (r <= 0) continue;
    to_screen(t->x, t->y, &sx, &sy);
    if (sy + r < gfx_y0 || sy - r > gfx_y1) continue;
    color_t c = t->kind == TR_CIRCLE_RED ? HEX(0xFF0000)
                                         : gfx_rgb(tint888((uint32_t)t->color & 0xFFFFFF, map_tint.r, map_tint.g, map_tint.b));
    fill_circle(sx, sy, r < 0.7 ? 0.7 : r, c, a);
  }
}
static void draw_cat(void) {
  if (!P.on_stage) return;
  if (P.hasJetPack && P.jet_visible) {
    blit_t b = pic_at(SPR_JETPACKADDON, -15, P.jet_half ? 0 : -10);
    if (P.jet_half) b.tsy = 1;
    b.lsx = P.scaleX, b.lrot = P.rotation, b.pal = PAL_CAT;
    blit(&b, P.x, P.y);
  }
  blit_t b = pic_at(P.pic, -OFFX, -OFFY);
  b.lsx = P.scaleX, b.lrot = P.rotation, b.pal = PAL_CAT;
  blit(&b, P.x, P.y);
}
static void draw_bombs(void) {
  const bomb_t *bs;
  int n = env_bombs(&bs);
  for (int i = 0; i < n; i++) {
    blit_t b = pic_at(SPR_BOMB, -10, -10);
    b.lrot = bs[i].rot;
    blit(&b, bs[i].x, bs[i].y);
  }
}

static void hud_draw(void);
/* the stage's texts and counts (0.4 of its 800 x 600) */
static void draw_stage(void) {
  for (int i = 0; i < n_texts; i++) {
    dtext_t *t = &texts[i];
    if (!t->visible) continue;
    int a = (int)(t->alpha * 32 + 0.5);
    if (a <= 0) continue;
    double x = t->x * 0.4, top = (t->y + 22) * 0.4;
    text_draw(t->text, x + 160, top, 316, 0, a, true, false, 0);
    if (t->logo) { /* spec3: the TrackMill logo under the words */
      blit_t b = pic_at(SPR_TRACKMILL, 0, 0);
      const cn_sprite_t *sp = &cn_sprites[SPR_TRACKMILL];
      int x0 = (int)x + 160 - sp->w / 2, y0 = (int)((t->y + 80) * 0.4);
      (void)b;
      for (int Y = imax(y0, gfx_y0); Y < imin(y0 + sp->h, gfx_y1); Y++)
        for (int X = imax(x0, 0); X < imin(x0 + sp->w, SCREEN_W); X++) {
          int idx = cn_sprite_px[sp->at + (Y - y0) * sp->w + (X - x0)];
          if (!idx) continue;
          int al = pal_a[idx] * a / CN_ALPHA;
          color_t *o = gfx_buf + (Y - gfx_y0) * SCREEN_W + X;
          *o = al >= 32 ? pal_plain[idx] : gfx_mix(*o, pal_plain[idx], al);
        }
    }
  }
  hud_draw();
}
static void hud_draw_line(const char *s, int x, int base) {
  const np_font_t *f = &cn_fonts[FONT_HUD];
  color_t c = HEX(0xFFFFFF);
  for (; *s; s++) {
    const np_glyph_t *g = glyph_of(f, (unsigned char)*s);
    if (!g) g = glyph_of(f, (unsigned char)(*s >= 'a' && *s <= 'z' ? *s - 32 : *s));
    if (!g) continue;
    if (g->w) {
      for (int dd = 0; dd < 8; dd++) {
        static const int8_t ox[8] = {-1, 0, 1, -1, 1, -1, 0, 1}, oy[8] = {-1, -1, -1, 0, 0, 1, 1, 1};
        glyph(f, g, x + ox[dd], base + oy[dd], 0, 0, 32);
      }
      glyph(f, g, x, base, 0, c, 32);
    }
    x += g->advance;
  }
}
static void hud_number(char *o, const char *label, int v) {
  while (*label) *o++ = *label++;
  char d[10];
  int k = 0;
  do d[k++] = (char)('0' + v % 10); while ((v /= 10) && k < 9);
  while (k) *o++ = d[--k];
  *o = 0;
}
static void hud_draw(void) { /* tools/text/Info: the deaths and the crystals, at the top left */
  const np_font_t *f = &cn_fonts[FONT_HUD];
  if (gfx_y0 > 4 + 2 * (f->ascent + f->descent)) return;
  char b[40];
  hud_number(b, "Deaths: ", hud_deaths);
  hud_draw_line(b, 3, 2 + f->ascent);
  hud_number(b, "Crystals Collected: ", hud_collected);
  hud_draw_line(b, 3, 3 + f->ascent * 2 + f->descent);
}

/* the whole world, in this strip */
void draw_world(void) {
  draw_tiles();
  draw_particles();
  draw_screens();
  draw_things();
  if (boss.created) draw_boss();
  draw_bombs();
  draw_trails();
  draw_cat();
  draw_stage();
  if (fade_on && fade_alpha > 0)
    gfx_fill_alpha(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, 0, (int)(fade_alpha * 256 > 256 ? 256 : fade_alpha * 256));
}
