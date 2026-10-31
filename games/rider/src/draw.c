/* The picture, drawn the way the game looks: dark ground under a dark sky, the track as glowing lines (a light core,
 * its colour, a soft edge), red saws and spikes, white gems, and the bike as a coloured shape with a white edge
 * on two white wheels, a trail behind it. Everything is drawn in a 320x240 picture of colour numbers first, then
 * sent to the screen in strips with the texts over it (launcher/src/gfx.c). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "rider.h"
#include "data.h"

uint8_t fb[240][320];
uint16_t pal[256];
#define ZOOM 0.5f

int obj_poly(const rd_run *r, int i, int k, float *xy);

void fb_fill(int x, int y, int w, int h, uint8_t c) {
  int x1 = x + w, y1 = y + h;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x1 > 320) x1 = 320;
  if (y1 > 240) y1 = 240;
  for (int j = y; j < y1; j++) memset(&fb[j][x], c, (size_t)(x1 > x ? x1 - x : 0));
}

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

/* ---------------------------------------------------------------- filling polygons */
static void fill_convex(const float *xy, int n, uint8_t col) {
  float y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < n; i++) y0 = fminf(y0, xy[2 * i + 1]), y1 = fmaxf(y1, xy[2 * i + 1]);
  int r0 = (int)ceilf(y0 - 0.5f), r1 = (int)floorf(y1 - 0.5f);
  if (r0 < 0) r0 = 0;
  if (r1 > 239) r1 = 239;
  for (int y = r0; y <= r1; y++) {
    float yc = y + 0.5f, xl = 1e9f, xr = -1e9f;
    for (int i = 0; i < n; i++) {
      float ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
      if ((ay <= yc) == (by <= yc)) continue;
      float x = ax + (yc - ay) * (bx - ax) / (by - ay);
      xl = fminf(xl, x), xr = fmaxf(xr, x);
    }
    int a = (int)ceilf(xl - 0.5f), b = (int)floorf(xr - 0.5f);
    if (a < 0) a = 0;
    if (b > 319) b = 319;
    if (b >= a) memset(&fb[y][a], col, (size_t)(b - a + 1));
  }
}
/* any polygon, even-odd */
static void fill_poly(const float *xy, int n, uint8_t col) {
  float y0 = 1e9f, y1 = -1e9f;
  for (int i = 0; i < n; i++) y0 = fminf(y0, xy[2 * i + 1]), y1 = fmaxf(y1, xy[2 * i + 1]);
  int r0 = (int)ceilf(y0 - 0.5f), r1 = (int)floorf(y1 - 0.5f);
  if (r0 < 0) r0 = 0;
  if (r1 > 239) r1 = 239;
  for (int y = r0; y <= r1; y++) {
    float yc = y + 0.5f, xs[48];
    int m = 0;
    for (int i = 0; i < n && m < 48; i++) {
      float ax = xy[2 * i], ay = xy[2 * i + 1], bx = xy[2 * ((i + 1) % n)], by = xy[2 * ((i + 1) % n) + 1];
      if ((ay <= yc) == (by <= yc)) continue;
      xs[m++] = ax + (yc - ay) * (bx - ax) / (by - ay);
    }
    for (int i = 1; i < m; i++)
      for (int j = i; j > 0 && xs[j - 1] > xs[j]; j--) { float t = xs[j]; xs[j] = xs[j - 1], xs[j - 1] = t; }
    for (int i = 0; i + 1 < m; i += 2) {
      int a = (int)ceilf(xs[i] - 0.5f), b = (int)floorf(xs[i + 1] - 0.5f);
      if (a < 0) a = 0;
      if (b > 319) b = 319;
      if (b >= a) memset(&fb[y][a], col, (size_t)(b - a + 1));
    }
  }
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

static void dot(int x, int y, uint8_t c) {
  if (x >= 0 && x < 320 && y >= 0 && y < 240) fb[y][x] = c;
}
static void line(float x0, float y0, float x1, float y1, uint8_t c, int thick) {
  float dx = x1 - x0, dy = y1 - y0;
  int n = (int)(fmaxf(fabsf(dx), fabsf(dy)) + 1);
  for (int i = 0; i <= n; i++) {
    int x = (int)(x0 + dx * i / n), y = (int)(y0 + dy * i / n);
    dot(x, y, c);
    if (thick > 1) dot(x + 1, y, c), dot(x, y + 1, c), dot(x + 1, y + 1, c);
  }
}
static void disc(float cx, float cy, float rad, uint8_t c) {
  int y0 = (int)floorf(cy - rad), y1 = (int)ceilf(cy + rad);
  for (int y = y0; y <= y1; y++) {
    if (y < 0 || y > 239) continue;
    float dy = y + 0.5f - cy, w2 = rad * rad - dy * dy;
    if (w2 < 0) continue;
    float w = sqrtf(w2);
    int a = (int)ceilf(cx - w - 0.5f), b = (int)floorf(cx + w - 0.5f);
    if (a < 0) a = 0;
    if (b > 319) b = 319;
    if (b >= a) memset(&fb[y][a], c, (size_t)(b - a + 1));
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
void draw_bike_at(int bike, float x, float y, float a, float z, int glow) {
  const rd_bike *bk = &rd_bikes[bike];
  float c = cosf(a), s = sinf(a), xy[96], g[96];
  int n = bk->on > 48 ? 48 : bk->on;
  for (int i = 0; i < n; i++) {
    float lx = bike_xy[2 * (bk->o0 + i)] * 0.5f * z, ly = bike_xy[2 * (bk->o0 + i) + 1] * 0.5f * z;
    xy[2 * i] = x + c * lx - s * ly, xy[2 * i + 1] = y + s * lx + c * ly;
  }
  float wx[2], wy[2], wr = 8.4f * z;
  for (int k = 0; k < 2; k++) {
    float lx = (k ? 38 : -34) * z, ly = 13.3f * z;
    wx[k] = x + c * lx - s * ly, wy[k] = y + s * lx + c * ly;
  }
  if (!bk->front)
    for (int k = 0; k < 2; k++) disc(wx[k], wy[k], wr, C_WHITE), disc(wx[k], wy[k], wr * 0.35f, C_WHEEL_HUB);
  if (glow) { /* the white edge: the shape drawn one pixel bigger in white first */
    for (int d = 0; d < 4; d++) {
      float ox = d == 0 ? 1 : d == 1 ? -1 : 0, oy = d == 2 ? 1 : d == 3 ? -1 : 0;
      for (int i = 0; i < n; i++) g[2 * i] = xy[2 * i] + ox, g[2 * i + 1] = xy[2 * i + 1] + oy;
      fill_poly(g, n, C_WHITE);
    }
  }
  fill_poly(xy, n, C_BODY);
  if (bk->front)
    for (int k = 0; k < 2; k++) disc(wx[k], wy[k], wr, C_WHITE), disc(wx[k], wy[k], wr * 0.35f, C_WHEEL_HUB);
}

/* ---------------------------------------------------------------- the run */
void draw_run(const rd_run *r) {
  float sh = r->ended == END_CRASH && r->shake > 0 ? r->shake * 20 : 0;
  float cx = r->camx + (sh ? (float)((int)(r->end_ms * 7) % 5 - 2) * sh * 0.3f : 0), cy = r->camy;
  memset(fb, C_BG, sizeof fb);
  float wx0 = cx - 8, wx1 = cx + 320 / ZOOM + 8, wy0 = cy - 8, wy1 = cy + 240 / ZOOM + 8;
  float xy[64], o[64];
  /* three passes: the ground, the glow round the lines, the lines and the rest */
  for (int pass = 0; pass < 3; pass++) {
    for (int i = 0; i < r->nlive; i++) {
      const rd_live *l = &r->live[i];
      if (l->x1 < wx0 || l->x0 > wx1 || l->y1 < wy0 || l->y0 > wy1 || (l->st & ST_GONE)) continue;
      const rd_obj *ob = &rd_objs[l->obj];
      int kind = ob->kind;
      if ((pass == 0) != (kind == K_GROUND)) continue;
      if (kind == K_GEM) {
        if (pass != 2) continue;
        float sx = (l->cx - cx) * ZOOM, sy = (l->cy - cy) * ZOOM, w = ob->sx * 0.14f * ZOOM, h = ob->sy * 0.18f * ZOOM;
        float d[8] = {sx, sy - h, sx + w, sy, sx, sy + h, sx - w, sy};
        offset_poly(d, 4, 2, false, o);
        fill_convex(o, 4, C_GEM_HALO);
        fill_convex(d, 4, C_GEM);
        continue;
      }
      if (kind == K_FINISH) {
        if (pass != 2) continue;
        float sx = (l->cx - cx) * ZOOM, top = (l->cy - ob->sy / 2 - cy) * ZOOM, bot = (l->cy + ob->sy / 2 - cy) * ZOOM;
        for (int y = (int)top; y < (int)bot; y++) {
          bool lit = ((y - (int)top) / 6) & 1;
          for (int k = -3; k <= 3; k++) dot((int)sx + k, y, abs(k) == 3 ? C_HALO : lit ? C_WHITE : C_FINISH);
        }
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
        float c = cosf(l->rot), s = sinf(l->rot), ph = fmodf(r->ms / 8, 24);
        for (int k = -2; k <= 1; k++) {
          float ax = (k * 24 + ph) * ZOOM, bx = (l->cx - cx) * ZOOM, by = (l->cy - cy) * ZOOM - 4;
          float px = bx + c * ax, py = by + s * ax;
          line(px - c * 3 - s * 3, py - s * 3 + c * 3, px, py, C_WHITE, 1);
          line(px - c * 3 + s * 3, py - s * 3 - c * 3, px, py, C_WHITE, 1);
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
    draw_bike_at(r->bike, (b->x - cx) * ZOOM, (b->y - cy) * ZOOM, b->a, ZOOM, 1);
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
        dot((int)x, (int)y, p->c), dot((int)x + 1, (int)y, p->c);
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

/* ---------------------------------------------------------------- to the screen */
static void (*hud_fn)(void);
static void scene(void *ctx) {
  (void)ctx;
  for (int y = gfx_y0; y < gfx_y1; y++) {
    color_t *d = gfx_buf + (y - gfx_y0) * SCREEN_W;
    const uint8_t *s = fb[y];
    for (int x = 0; x < 320; x++) d[x] = pal[s[x]];
  }
  if (hud_fn) hud_fn();
}
void draw_flush(void (*hud)(void)) {
  hud_fn = hud;
  gfx_render(scene, 0, 0, SCREEN_H);
}
