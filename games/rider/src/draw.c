/* The picture, drawn the way the game looks: dark ground under a dark sky, the track as glowing lines (a light core,
 * its colour, a soft edge), red saws and spikes, white gems, and the bike as a coloured shape with a white edge
 * on two white wheels, a trail behind it. Everything is first written down as a list of shapes in colour numbers,
 * then filled strip by strip into the screen with the texts over it (launcher/src/gfx.c): no picture of the whole
 * screen is kept, and the list itself sits in the physics' memory left free between two steps. */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "rider.h"
#include "data.h"

uint16_t pal[256];
#define ZOOM 0.5f

/* ---------------------------------------------------------------- the list of shapes */
enum { D_CONVEX, D_POLY, D_DISC, D_LINE, D_RECT };
typedef struct {
  uint8_t type, col;
  uint16_t n, at; /* points (or the line's thickness), where they start in the pool */
  int16_t y0, y1; /* the rows it covers */
} dcmd;
static dcmd *cmds;   /* (in the physics' free memory: a third shapes, the rest points) */
static int16_t *pts; /* eighths of a pixel */
static int ncmd, npts, NCMD, NPTS;
static uint8_t bg_col;

void draw_clear(uint8_t c) {
  size_t n;
  uint8_t *m = ph_scratch(&n);
  NCMD = (int)(n / 3 / sizeof(dcmd)), cmds = (dcmd *)m;
  NPTS = (int)((n - NCMD * sizeof(dcmd)) / 4), pts = (int16_t *)(m + NCMD * sizeof(dcmd));
  if (NCMD > 900) NCMD = 900;
  if (NPTS > 4000) NPTS = 4000;
  ncmd = npts = 0, bg_col = c;
}

static inline int16_t q8(float v) {
  v *= 8;
  if (v > 32000) v = 32000;
  if (v < -32000) v = -32000;
  return (int16_t)lrintf(v);
}
static void push_shape(int type, const float *xy, int n, uint8_t col) {
  if (ncmd == NCMD || npts + n > NPTS || n < 1) return;
  float y0 = 1e9f, y1 = -1e9f, x0 = 1e9f, x1 = -1e9f;
  for (int i = 0; i < n; i++) {
    y0 = fminf(y0, xy[2 * i + 1]), y1 = fmaxf(y1, xy[2 * i + 1]);
    x0 = fminf(x0, xy[2 * i]), x1 = fmaxf(x1, xy[2 * i]);
  }
  if (y1 < 0 || y0 > 240 || x1 < 0 || x0 > 320) return;
  dcmd *c = &cmds[ncmd++];
  c->type = (uint8_t)type, c->col = col, c->n = (uint16_t)n, c->at = (uint16_t)npts;
  c->y0 = (int16_t)floorf(fmaxf(y0, -1)), c->y1 = (int16_t)ceilf(fminf(y1, 241));
  for (int i = 0; i < n; i++) pts[2 * npts] = q8(xy[2 * i]), pts[2 * npts + 1] = q8(xy[2 * i + 1]), npts++;
}
static void fill_convex(const float *xy, int n, uint8_t col) { push_shape(D_CONVEX, xy, n, col); }
static void fill_poly(const float *xy, int n, uint8_t col) { push_shape(D_POLY, xy, n, col); }
static void disc(float cx, float cy, float rad, uint8_t c) {
  float q[4] = {cx - rad, cy - rad, cx + rad, cy + rad};
  int k = ncmd;
  push_shape(D_DISC, q, 2, c);
  if (ncmd > k) cmds[k].y0 = (int16_t)floorf(cy - rad), cmds[k].y1 = (int16_t)ceilf(cy + rad);
}
static void line(float x0, float y0, float x1, float y1, uint8_t c, int thick) {
  float q[4] = {x0, y0, x1, y1};
  int k = ncmd;
  push_shape(D_LINE, q, 2, c);
  if (ncmd > k) cmds[k].type = D_LINE, cmds[k].y1 = (int16_t)(cmds[k].y1 + 1), cmds[k].n = 2 | (uint16_t)(thick << 8);
}
void fb_fill(int x, int y, int w, int h, uint8_t c) {
  float q[4] = {(float)x, (float)y, (float)(x + w), (float)(y + h)};
  push_shape(D_RECT, q, 2, c);
}

/* ---------------------------------------------------------------- filling a strip */
static color_t *strip;
static int sy0, sy1;
static inline void span(int y, int a, int b, color_t c) {
  if (a < 0) a = 0;
  if (b > 319) b = 319;
  color_t *p = strip + (y - sy0) * SCREEN_W;
  for (int x = a; x <= b; x++) p[x] = c;
}
static void raster(const dcmd *c) {
  const int16_t *p = pts + 2 * c->at;
  color_t col = pal[c->col];
  int r0 = c->y0 > sy0 ? c->y0 : sy0, r1 = c->y1 < sy1 - 1 ? c->y1 : sy1 - 1;
  switch (c->type) {
  case D_CONVEX:
  case D_POLY: {
    int n = c->n;
    for (int y = r0; y <= r1; y++) {
      float yc = y + 0.5f, xs[48];
      int m = 0;
      for (int i = 0; i < n && m < 48; i++) {
        int j = i + 1 == n ? 0 : i + 1;
        float ax = p[2 * i] * 0.125f, ay = p[2 * i + 1] * 0.125f, bx = p[2 * j] * 0.125f, by = p[2 * j + 1] * 0.125f;
        if ((ay <= yc) == (by <= yc)) continue;
        xs[m++] = ax + (yc - ay) * (bx - ax) / (by - ay);
      }
      if (c->type == D_CONVEX) {
        if (m < 2) continue;
        float xl = xs[0], xr = xs[0];
        for (int i = 1; i < m; i++) xl = fminf(xl, xs[i]), xr = fmaxf(xr, xs[i]);
        span(y, (int)ceilf(xl - 0.5f), (int)floorf(xr - 0.5f), col);
        continue;
      }
      for (int i = 1; i < m; i++)
        for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { float t = xs[j]; xs[j] = xs[j - 1], xs[j - 1] = t; }
      for (int i = 0; i + 1 < m; i += 2) span(y, (int)ceilf(xs[i] - 0.5f), (int)floorf(xs[i + 1] - 0.5f), col);
    }
    break;
  }
  case D_DISC: {
    float cx = (p[0] + p[2]) * 0.0625f, cy = (p[1] + p[3]) * 0.0625f, rad = (p[2] - p[0]) * 0.0625f;
    for (int y = r0; y <= r1; y++) {
      float dy = y + 0.5f - cy, w2 = rad * rad - dy * dy;
      if (w2 < 0) continue;
      float w = sqrtf(w2);
      span(y, (int)ceilf(cx - w - 0.5f), (int)floorf(cx + w - 0.5f), col);
    }
    break;
  }
  case D_LINE: {
    float x0 = p[0] * 0.125f, y0 = p[1] * 0.125f, dx = p[2] * 0.125f - x0, dy = p[3] * 0.125f - y0;
    int thick = c->n >> 8, n = (int)(fmaxf(fabsf(dx), fabsf(dy)) + 1);
    for (int i = 0; i <= n; i++) {
      int x = (int)(x0 + dx * i / n), y = (int)(y0 + dy * i / n);
      for (int k = 0; k < (thick > 1 ? 4 : 1); k++) {
        int xx = x + (k & 1), yy = y + (k >> 1);
        if (xx >= 0 && xx < 320 && yy >= sy0 && yy < sy1) strip[(yy - sy0) * SCREEN_W + xx] = col;
      }
    }
    break;
  }
  case D_RECT: {
    int x0 = p[0] >> 3, x1 = (p[2] >> 3) - 1, ya = (p[1] >> 3) > r0 ? (p[1] >> 3) : r0, yb = (p[3] >> 3) - 1 < r1 ? (p[3] >> 3) - 1 : r1;
    for (int y = ya; y <= yb; y++) span(y, x0, x1, col);
    break;
  }
  }
}

static void (*hud_fn)(void);
static void scene(void *ctx) {
  (void)ctx;
  strip = gfx_buf, sy0 = gfx_y0, sy1 = gfx_y1;
  color_t bg = pal[bg_col];
  for (int i = 0; i < SCREEN_W * (sy1 - sy0); i++) strip[i] = bg;
  for (int i = 0; i < ncmd; i++)
    if (cmds[i].y1 >= sy0 && cmds[i].y0 < sy1) raster(&cmds[i]);
  if (hud_fn) hud_fn();
}
#ifdef HOST
int draw_most_cmds, draw_most_pts;
#endif
void draw_flush(void (*hud)(void)) {
  hud_fn = hud;
#ifdef HOST
  if (ncmd > draw_most_cmds) draw_most_cmds = ncmd;
  if (npts > draw_most_pts) draw_most_pts = npts;
#endif
  gfx_render(scene, 0, 0, SCREEN_H);
}

/* ---------------------------------------------------------------- colours */
static uint32_t mix888(uint32_t a, uint32_t b, float t) {
  uint32_t o = 0;
  for (int s = 0; s < 24; s += 8) {
    float ca = (a >> s) & 255, cb = (b >> s) & 255;
    o |= (uint32_t)(ca + (cb - ca) * t + 0.5f) << s;
  }
  return o;
}
static uint32_t scale888(uint32_t a, float t) { return mix888(0, a, t); }
static color_t c565(uint32_t c) { return RGB((c >> 16) & 255, (c >> 8) & 255, c & 255); }

void draw_theme(int theme) {
  const uint32_t *t = themes[theme];
  uint32_t bg = scale888(t[3], 0.31f), ground = scale888(t[3], 0.49f), red = 0xFF5959;
  uint32_t c[C_TRAIL + 8] = {0};
  c[C_BG] = bg, c[C_GROUND] = ground;
  c[C_HALO] = mix888(bg, t[2], 0.4f), c[C_GLOW] = t[2], c[C_CORE] = t[1];
  c[C_EN_HALO] = mix888(bg, 0xFF0000, 0.4f), c[C_EN] = red;
  c[C_BODY] = t[0], c[C_BODY_DARK] = scale888(t[0], 0.55f), c[C_WHITE] = 0xFFFFFF, c[C_WHEEL_HUB] = mix888(bg, 0, 0.3f);
  c[C_GEM_HALO] = mix888(bg, t[6], 0.45f), c[C_GEM] = t[5];
  c[C_BOOST] = 0x2E7BFF, c[C_BOOST_DIM] = mix888(bg, 0x2E7BFF, 0.45f), c[C_FINISH] = mix888(t[2], 0xFFFFFF, 0.5f);
  c[C_SPARK] = 0xFFD46A, c[C_SHADE] = 0x000000;
  for (int i = 0; i < 8; i++) c[C_TRAIL + i] = mix888(t[4], bg, i / 8.0f);
  for (int i = 0; i < C_TRAIL + 8; i++) pal[i] = c565(c[i]);
}

/* the polygon grown (d > 0) or shrunk (d < 0) by d pixels; with long_only, only its long sides move (a strip's
 * pieces keep meeting end to end) */
static void offset_poly(const float *xy, int n, float d, bool long_only, float *out) {
  float area = 0;
  for (int i = 0; i < n; i++) area += xy[2 * i] * xy[2 * ((i + 1) % n) + 1] - xy[2 * ((i + 1) % n)] * xy[2 * i + 1];
  float sgn = area > 0 ? 1 : -1;
  float nx[24], ny[24], dd[24], ln[24];
  for (int i = 0; i < n; i++) {
    float ex = xy[2 * ((i + 1) % n)] - xy[2 * i], ey = xy[2 * ((i + 1) % n) + 1] - xy[2 * i + 1];
    float len = sqrtf(ex * ex + ey * ey);
    if (len < 1e-4f) len = 1e-4f;
    nx[i] = sgn * ey / len, ny[i] = -sgn * ex / len, ln[i] = len;
    dd[i] = long_only && len < 9 ? 0 : d;
  }
  if (long_only && n == 4) { /* a strip's piece: its two short sides are where it meets the next */
    int a = ln[0] + ln[2] < ln[1] + ln[3] ? 0 : 1;
    dd[a] = dd[a + 2] = 0, dd[a + 1] = dd[(a + 3) % 4] = d;
  }
  for (int i = 0; i < n; i++) {
    int p = (i + n - 1) % n;
    float det = nx[p] * ny[i] - ny[p] * nx[i];
    float ox, oy;
    if (fabsf(det) < 0.05f) ox = nx[i] * dd[i], oy = ny[i] * dd[i];
    else ox = (dd[p] * ny[i] - dd[i] * ny[p]) / det, oy = (nx[p] * dd[i] - nx[i] * dd[p]) / det;
    float m = ox * ox + oy * oy, lim = 4 * d * d;
    if (m > lim) { float k = sqrtf(lim / m); ox *= k, oy *= k; }
    out[2 * i] = xy[2 * i] + ox, out[2 * i + 1] = xy[2 * i + 1] + oy;
  }
}

/* the light core of a line's piece: the piece squeezed across its length (so pieces still meet end to end), or
 * for a wide piece its inside */
static int core_poly(const float *xy, int n, float *out) {
  float cx = 0, cy = 0;
  for (int i = 0; i < n; i++) cx += xy[2 * i], cy += xy[2 * i + 1];
  cx /= n, cy /= n;
  float sxx = 0, syy = 0, sxy = 0;
  for (int i = 0; i < n; i++) {
    float dx = xy[2 * i] - cx, dy = xy[2 * i + 1] - cy;
    sxx += dx * dx, syy += dy * dy, sxy += dx * dy;
  }
  float th = 0.5f * atan2f(2 * sxy, sxx - syy), ux = cosf(th), uy = sinf(th);
  float lo = 1e9f, hi = -1e9f;
  for (int i = 0; i < n; i++) {
    float p = -(xy[2 * i] - cx) * uy + (xy[2 * i + 1] - cy) * ux;
    lo = fminf(lo, p), hi = fmaxf(hi, p);
  }
  float thick = hi - lo;
  if (thick > 9) { offset_poly(xy, n, -2.5f, false, out); return -n; } /* (drawn dark inside: an outline) */
  if (thick < 2.5f) return 0;
  float k = 1 - 2.4f / thick, mid = (hi + lo) / 2;
  for (int i = 0; i < n; i++) {
    float dx = xy[2 * i] - cx, dy = xy[2 * i + 1] - cy, a = dx * ux + dy * uy, p = -dx * uy + dy * ux;
    p = mid + (p - mid) * k;
    out[2 * i] = cx + a * ux - p * uy, out[2 * i + 1] = cy + a * uy + p * ux;
  }
  return n;
}

/* ---------------------------------------------------------------- the bike */
static int bike_shape(int bike, float x, float y, float a, float z, float *xy) {
  const rd_bike *bk = &rd_bikes[bike];
  float c = cosf(a), s = sinf(a);
  int n = bk->on > 48 ? 48 : bk->on;
  for (int i = 0; i < n; i++) {
    float lx = bike_xy[2 * (bk->o0 + i)] * 0.5f * z, ly = bike_xy[2 * (bk->o0 + i) + 1] * 0.5f * z;
    xy[2 * i] = x + c * lx - s * ly, xy[2 * i + 1] = y + s * lx + c * ly;
  }
  return n;
}
#define WHEEL_R 8.4f
void draw_wheels_at(float x0, float y0, float x1, float y1, float z, int front) {
  (void)front;
  disc(x0, y0, WHEEL_R * z, C_WHITE), disc(x0, y0, WHEEL_R * z * 0.35f, C_WHEEL_HUB);
  disc(x1, y1, WHEEL_R * z, C_WHITE), disc(x1, y1, WHEEL_R * z * 0.35f, C_WHEEL_HUB);
}
static void bike_body_at(int bike, float x, float y, float a, float z, int glow) {
  float xy[96], g[96];
  int n = bike_shape(bike, x, y, a, z, xy);
  if (glow) { /* the white edge: the shape drawn one pixel bigger in white first */
    for (int d = 0; d < 4; d++) {
      float ox = d == 0 ? 1 : d == 1 ? -1 : 0, oy = d == 2 ? 1 : d == 3 ? -1 : 0;
      for (int i = 0; i < n; i++) g[2 * i] = xy[2 * i] + ox, g[2 * i + 1] = xy[2 * i + 1] + oy;
      fill_poly(g, n, C_WHITE);
    }
  }
  fill_poly(xy, n, C_BODY);
}
/* the bike as it stands (menus): wheels where GameCharacter puts them, x0.7 (x0.49 down) from the body's centre */
static void stand_wheels(float x, float y, float a, float z, float *w) {
  float c = cosf(a), s = sinf(a);
  for (int k = 0; k < 2; k++) {
    float lx = (k ? 38 : -34) * 0.7f * z, ly = (37 - 18) * 0.49f * z;
    w[2 * k] = x + c * lx - s * ly, w[2 * k + 1] = y + s * lx + c * ly;
  }
}
void draw_bike_at(int bike, float x, float y, float a, float z, int glow) {
  float w[4];
  stand_wheels(x, y, a, z, w);
  bool front = rd_bikes[bike].front;
  if (!front) draw_wheels_at(w[0], w[1], w[2], w[3], z, 0);
  bike_body_at(bike, x, y, a, z, glow);
  if (front) draw_wheels_at(w[0], w[1], w[2], w[3], z, 1);
}
void draw_bike_shape(int bike, float x, float y, float a, float z, uint8_t c) {
  float w[4], xy[96];
  stand_wheels(x, y, a, z, w);
  disc(w[0], w[1], WHEEL_R * z, c), disc(w[2], w[3], WHEEL_R * z, c);
  fill_poly(xy, bike_shape(bike, x, y, a, z, xy), c);
}

/* ---------------------------------------------------------------- the run */
void draw_run(const rd_run *r) {
  float sh = r->ended == END_CRASH && r->shake > 0 ? r->shake * 20 : 0;
  float cx = r->camx + (sh ? (float)((int)(r->end_ms * 7) % 5 - 2) * sh * 0.3f : 0), cy = r->camy;
  draw_clear(C_BG);
  float wx0 = cx - 8, wx1 = cx + 320 / ZOOM + 8, wy0 = cy - 8, wy1 = cy + 240 / ZOOM + 8;
  float xy[64], o[64];
  /* three passes: the ground, the glow round the lines, the lines and the rest */
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < r->nlive; i++) {
      const rd_live *l = &r->live[i];
      if (!(l->st & ST_USED) || (l->st & ST_GONE)) continue;
      float bx0, by0, bx1, by1;
      obj_box(r, i, &bx0, &by0, &bx1, &by1);
      if (bx1 < wx0 || bx0 > wx1 || by1 < wy0 || by0 > wy1) continue;
      const rd_obj *ob = &rd_objs[l->obj];
      int kind = ob->kind;
      if ((pass == 0) != (kind == K_GROUND)) continue;
      float px, py, pa;
      obj_pose(r, i, &px, &py, &pa);
      if (kind == K_GEM) {
        if (pass != 2) continue;
        float sx = (px - cx) * ZOOM, sy = (py - cy) * ZOOM, w = fabsf(ob->sx) * 0.14f * ZOOM, h = fabsf(ob->sy) * 0.18f * ZOOM;
        float d[8] = {sx, sy - h, sx + w, sy, sx, sy + h, sx - w, sy};
        offset_poly(d, 4, 2, false, o);
        fill_convex(o, 4, C_GEM_HALO);
        fill_convex(d, 4, C_GEM);
        continue;
      }
      if (kind == K_FINISH) {
        if (pass != 2) continue;
        int sx = (int)((px - cx) * ZOOM), top = (int)((py - fabsf(ob->sy) / 2 - cy) * ZOOM), bot = (int)((py + fabsf(ob->sy) / 2 - cy) * ZOOM);
        fb_fill(sx - 3, top, 1, bot - top, C_HALO), fb_fill(sx + 3, top, 1, bot - top, C_HALO);
        for (int y = top; y < bot; y += 6) fb_fill(sx - 2, y, 5, bot - y < 6 ? bot - y : 6, ((y - top) / 6) & 1 ? C_WHITE : C_FINISH);
        continue;
      }
      for (int k = 0;; k++) {
        int n = obj_poly(r, i, k, xy);
        if (!n) break;
        if (n > 24) n = 24;
        for (int j = 0; j < n; j++) xy[2 * j] = (xy[2 * j] - cx) * ZOOM, xy[2 * j + 1] = (xy[2 * j + 1] - cy) * ZOOM;
        if (pass == 0) { fill_convex(xy, n, C_GROUND); continue; }
        if (kind == K_BOOST) {
          if (pass == 1) offset_poly(xy, n, 1.5f, false, o), fill_convex(o, n, C_BOOST_DIM);
          else fill_convex(xy, n, C_BOOST);
          continue;
        }
        bool en = kind == K_ENEMY;
        if (pass == 1) {
          offset_poly(xy, n, 2, false, o);
          fill_convex(o, n, en ? C_EN_HALO : C_HALO);
        } else {
          fill_convex(xy, n, en ? C_EN : C_GLOW);
          int m = en ? 0 : core_poly(xy, n, o);
          if (m) fill_convex(o, m > 0 ? m : -m, m > 0 ? C_CORE : C_HALO);
        }
      }
      /* speed pads: arrows running along */
      if (kind == K_BOOST && pass == 2) {
        float c = cosf(pa), s = sinf(pa), ph = fmodf(r->ms / 8, 24);
        for (int k = -2; k <= 1; k++) {
          float ax = (k * 24 + ph) * ZOOM, bx = (px - cx) * ZOOM, by = (py - cy) * ZOOM - 4;
          float qx = bx + c * ax, qy = by + s * ax;
          line(qx - c * 3 - s * 3, qy - s * 3 + c * 3, qx, qy, C_WHITE, 1);
          line(qx - c * 3 + s * 3, qy - s * 3 - c * 3, qx, qy, C_WHITE, 1);
        }
      }
    }
  }
  /* the trail */
  const rd_bikestate *b = &r->b;
  for (int i = 0; i + 1 < b->trail_n && !r->ended; i++) {
    float x0 = (b->trail_x[i] - cx) * ZOOM, y0 = (b->trail_y[i] - cy) * ZOOM;
    float x1 = (b->trail_x[i + 1] - cx) * ZOOM, y1 = (b->trail_y[i + 1] - cy) * ZOOM;
    line(x0, y0, x1, y1, (uint8_t)(C_TRAIL + i * 8 / TRAIL), i < TRAIL / 2 ? 2 : 1);
  }
  if (r->ended != END_CRASH) {
    float w0x = (b->wx[0] - cx) * ZOOM, w0y = (b->wy[0] - cy) * ZOOM, w1x = (b->wx[1] - cx) * ZOOM, w1y = (b->wy[1] - cy) * ZOOM;
    bool front = rd_bikes[r->bike].front;
    if (!front) draw_wheels_at(w0x, w0y, w1x, w1y, ZOOM, 0);
    bike_body_at(r->bike, (b->x - cx) * ZOOM, (b->y - cy) * ZOOM, b->a, ZOOM, 1);
    if (front) draw_wheels_at(w0x, w0y, w1x, w1y, ZOOM, 1);
  } else {
    for (int i = 0; i < r->nbits; i++) {
      const rd_bit *p = &r->bits[i];
      float x = (p->x - cx) * ZOOM, y = (p->y - cy) * ZOOM;
      if (p->size > 4) {
        float c = cosf(p->a) * p->size * ZOOM, s = sinf(p->a) * p->size * ZOOM;
        float q[8] = {x - c - s * 0.35f, y - s + c * 0.35f, x + c - s * 0.35f, y + s + c * 0.35f,
                      x + c + s * 0.35f, y + s - c * 0.35f, x - c + s * 0.35f, y - s - c * 0.35f};
        offset_poly(q, 4, 1, false, o), fill_convex(o, 4, C_WHITE), fill_convex(q, 4, C_BODY);
      } else if (r->end_ms < 900) {
        fb_fill((int)x, (int)y, 2, 1, p->c);
      }
    }
  }
}

/* little pictures for the texts over it */
void draw_gem_icon(float x, float y, float h) {
  float d[8] = {x, y - h, x + h * 0.75f, y, x, y + h, x - h * 0.75f, y}, o[8];
  offset_poly(d, 4, 1.5f, false, o);
  fill_convex(o, 4, C_GEM_HALO), fill_convex(d, 4, C_GEM);
}
void draw_star(float x, float y, float rad, uint8_t c) {
  float p[20];
  for (int i = 0; i < 10; i++) {
    float a = -1.5708f + i * 0.6283f, rr = i & 1 ? rad * 0.45f : rad;
    p[2 * i] = x + cosf(a) * rr, p[2 * i + 1] = y + sinf(a) * rr;
  }
  fill_poly(p, 10, c);
}
void draw_line(float x0, float y0, float x1, float y1, uint8_t c) { line(x0, y0, x1, y1, c, 1); }
void draw_disc(float x, float y, float rad, uint8_t c) { disc(x, y, rad, c); }
