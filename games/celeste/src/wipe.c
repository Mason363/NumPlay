#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Screen wipes (ScreenWipe and the chapters' own): black shapes over the
 * screen, drawn like the game does, as triangles in its 1920x1080 interface. */
#include "wipe.h"
#include "level.h"

Wipe g_wipe;

/* ---------------------------------------------------------------- triangles over the strips */
#define MAX_TRIS 136
typedef struct { int16_t x[3], y[3]; } Tri;   /* quarter screen pixels */
static Tri tris[MAX_TRIS];
static int ntris;
static uint8_t fade_alpha;   /* FadeWipe: the whole screen at this alpha */

static void tri(float x0, float y0, float x1, float y1, float x2, float y2) {
  if (ntris >= MAX_TRIS) return;
  Tri *t = &tris[ntris++];
  /* interface pixels to quarter screen pixels (clamped: far points only need to be off screen) */
#define Q(v) (int16_t)fmaxf(-8000, fminf(8000, (v) * 4 / 6))
  t->x[0] = Q(x0), t->y[0] = Q(y0), t->x[1] = Q(x1), t->y[1] = Q(y1), t->x[2] = Q(x2), t->y[2] = Q(y2);
#undef Q
}
static void quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3) {
  tri(x0, y0, x1, y1, x2, y2);
  tri(x0, y0, x2, y2, x3, y3);
}

#if NP_SOFT_FLOAT
/* No FPU (the TI-Nspire): the same in quarter pixels, each edge's crossing stepped down the rows in 2^32ths */
static void fill_tri(uint16_t *strip, int sy0, int sy1, const Tri *q) {
  int ymin = q->y[0], ymax = q->y[0];
  for (int i = 1; i < 3; i++) ymin = q->y[i] < ymin ? q->y[i] : ymin, ymax = q->y[i] > ymax ? q->y[i] : ymax;
  /* (rows whose centre y + 1/2 is in [ymin, ymax): ceil(v / 4 - 1/2)) */
  int r0 = (ymin - 2 + 3) >> 2, r1 = (ymax - 2 + 3) >> 2;
  if (r0 < sy0) r0 = sy0;
  if (r1 > sy1) r1 = sy1;
  if (r0 >= r1) return;
  int64_t slope[3];
  for (int i = 0; i < 3; i++) {
    int j = (i + 1) % 3, dy = q->y[j] - q->y[i];
    slope[i] = dy ? (int64_t)(q->x[j] - q->x[i]) * 4294967296 / dy : 0;
  }
  for (int y = r0; y < r1; y++) {
    int py = 4 * y + 2;
    int64_t xl = INT64_MAX, xr = INT64_MIN;
    for (int i = 0; i < 3; i++) {   /* the row's crossing with each edge */
      int ay = q->y[i], by = q->y[(i + 1) % 3];
      if ((ay <= py && by > py) || (by <= py && ay > py)) {
        int64_t x = (int64_t)q->x[i] * 4294967296 + (py - ay) * slope[i];
        xl = x < xl ? x : xl, xr = x > xr ? x : xr;
      }
    }
    if (xl > xr) continue;
    /* (ceil(x / 4 - 1/2), x in quarter pixels and 2^32ths) */
    int64_t a = (xl + ((int64_t)2 << 32) - 1) >> 34, b = (xr + ((int64_t)2 << 32) - 1) >> 34;
    if (a < 0) a = 0;
    if (b > VIEW_W) b = VIEW_W;
    uint16_t *row = strip + (y - sy0) * VIEW_W;
    for (int x = (int)a; x < b; x++) row[x] = 0;
  }
}
#else
static void fill_tri(uint16_t *strip, int sy0, int sy1, const Tri *q) {
  struct { float x[3], y[3]; } tt, *t = &tt;
  for (int i = 0; i < 3; i++) t->x[i] = q->x[i] / 4.f, t->y[i] = q->y[i] / 4.f;
  float ymin = fminf(t->y[0], fminf(t->y[1], t->y[2])), ymax = fmaxf(t->y[0], fmaxf(t->y[1], t->y[2]));
  int r0 = (int)ceilf(ymin - 0.5f), r1 = (int)ceilf(ymax - 0.5f);
  if (r0 < sy0) r0 = sy0;
  if (r1 > sy1) r1 = sy1;
  for (int y = r0; y < r1; y++) {
    float py = y + 0.5f, xl = 1e9f, xr = -1e9f;
    for (int i = 0; i < 3; i++) {   /* the row's crossing with each edge */
      float ax = t->x[i], ay = t->y[i], bx = t->x[(i + 1) % 3], by = t->y[(i + 1) % 3];
      if ((ay <= py && by > py) || (by <= py && ay > py)) {
        float x = ax + (py - ay) * (bx - ax) / (by - ay);
        xl = fminf(xl, x), xr = fmaxf(xr, x);
      }
    }
    int a = (int)ceilf(xl - 0.5f), b = (int)ceilf(xr - 0.5f);
    if (a < 0) a = 0;
    if (b > VIEW_W) b = VIEW_W;
    uint16_t *row = strip + (y - sy0) * VIEW_W;
    for (int x = a; x < b; x++) row[x] = 0;
  }
}
#endif

static void wipe_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  if (fade_alpha) {
    uint16_t *p = strip, *end = strip + (sy1 - sy0) * VIEW_W;
    int k = fade_alpha + (fade_alpha >> 7);   /* black over it: the picture * (1 - alpha) */
    for (; p < end; p++) *p = blend565(*p, 0, k);
  }
  for (int i = 0; i < ntris; i++) fill_tri(strip, sy0, sy1, &tris[i]);
}

/* ---------------------------------------------------------------- the wipes' shapes */
static void fade_shape(float p, bool in) {
  float a = in ? 1 - ease_cube_in(p) : ease_cube_out(p);
  fade_alpha = (uint8_t)(a * 255 + 0.5f);
}

static void angled_shape(float p, bool in) {
  const float h = 183.33333f, x0 = -64, w = 1984;
  for (int i = 0; i < 6; i++) {
    float y = -10 + i * h, k = 0, start = (in ? 1 - i / 6.f : i / 6.f) * 0.3f;
    if (p > start) k = fminf(1, (p - start) / 0.7f);
    if (in) k = 1 - k;
    float len = w * k;
    float v[6][2] = {{x0, y}, {x0 + len, y}, {x0, y + h}, {x0 + len, y}, {x0 + len + 64, y + h}, {x0, y + h}};
    if (in)
      for (int j = 0; j < 6; j++) v[j][0] = 1920 - v[j][0], v[j][1] = 1080 - v[j][1];
    tri(v[0][0], v[0][1], v[1][0], v[1][1], v[2][0], v[2][1]);
    tri(v[3][0], v[3][1], v[4][0], v[4][1], v[5][0], v[5][1]);
  }
}

static V2 curve(V2 a, V2 b, V2 c, float t) {   /* SimpleCurve: begin a, end b, control c */
  float u = 1 - t;
  return v2(u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y);
}
static void curtain_shape(float p, bool in) {
  float num = ease_cube_inout(in ? 1 - p : p);
  float n2 = fminf(1, num / 0.3f), n3 = fmaxf(0, fminf(1, (num - 0.1f) / 0.9f / 0.9f));
  V2 a = v2(0, 540 * n2), b = v2(960, 796), ctl = v2((a.x + b.x) / 2, (a.y + b.y) / 2 + 270);
  V2 top = v2(896 + 200 * num, -350 + 256 * n2);
  V2 pt = curve(a, b, ctl, n3);
  V2 bot = v2(pt.x + 64 * num, 1080);
  for (int side = 0; side < 2; side++) {
#define X(v) (side ? 1920 - (v) : (v))
    tri(X(-10), -10, X(top.x), -10, X(top.x), top.y);
    tri(X(-10), -10, X(-10), pt.y, X(pt.x), pt.y);
    tri(X(pt.x), pt.y, X(-10), pt.y, X(-10), 1090);
    tri(X(pt.x), pt.y, X(-10), 1090, X(bot.x), bot.y + 10);
    /* the curtain's edge: a fan from the corner along a curve */
    V2 prev = top, c2 = v2((top.x + pt.x) / 2, (top.y + pt.y) / 2 + 384 * n3);
    const int n = 30;
    for (int i = 1; i <= n; i++) {
      V2 q = curve(top, pt, c2, (float)i / n);
      tri(X(-10), -10, X(prev.x), prev.y, X(q.x), q.y);
      prev = q;
    }
#undef X
  }
}

/* SpotlightWipe: all but a circle around the focus */
static void spotlight_shape(float p, bool in) {
  float num = in ? p : 1 - p;
  float small = 288 + g_wipe.modifier, r;
  if (g_wipe.linear) r = ease_cube_inout(num) * 1920;
  else if (num < 0.2f) r = ease_cube_inout(num / 0.2f) * small;
  else if (num < 0.8f) r = small;
  else r = small + (num - 0.8f) / 0.2f * (1920 - small);
  V2 f = v2(g_wipe.focus.x * 6, g_wipe.focus.y * 6);
  const int n = 32;
  V2 a = v2(1, 0);
  for (int i = 1; i <= n; i++) {
    V2 b = angle_vec((float)i / n * PI_F * 2, 1);
    quad(f.x + a.x * 5000, f.y + a.y * 5000, f.x + a.x * r, f.y + a.y * r, f.x + b.x * r, f.y + b.y * r, f.x + b.x * 5000,
         f.y + b.y * 5000);
    a = b;
  }
}

/* ---------------------------------------------------------------- the wipe */
void wipe_start(int type, bool in, WipeDone done) {
  memset(&g_wipe, 0, sizeof g_wipe);
  g_wipe.type = (uint8_t)type;
  g_wipe.in = in;
  g_wipe.active = true;
  g_wipe.duration = type == WIPE_SPOTLIGHT ? 1.8f : 0.5f;
  g_wipe.done = done;
}
/* the area's own (AreaData.Wipe) */
void wipe_area(bool in, WipeDone done) {
  static const uint8_t types[AREAS] = {WIPE_CURTAIN, WIPE_ANGLED, WIPE_DREAM, WIPE_KEYDOOR, WIPE_WIND, WIPE_DROP,
                                       WIPE_FALL, WIPE_MOUNTAIN, WIPE_CURTAIN, WIPE_HEART, WIPE_STARFIELD};
  wipe_start(types[g_session.area < AREAS ? g_session.area : 1], in, done);
}

void wipe_update(void) {
  Wipe *w = &g_wipe;
  if (!w->active) return;
  if (!w->completed) {
    if (w->percent < 1) w->percent = approach(w->percent, 1, RAW_DT / w->duration);
    else if (w->end_timer > 0) w->end_timer -= RAW_DT;
    else w->completed = true;
  } else {
    w->active = false;
    if (w->done) w->done();
  }
}

void wipe_render(void) {
  Wipe *w = &g_wipe;
  if (!w->active) return;
  ntris = 0;
  fade_alpha = 0;
  switch (w->type) {
    case WIPE_FADE: fade_shape(w->percent, w->in); break;
    case WIPE_SPOTLIGHT: spotlight_shape(w->percent, w->in); break;
    case WIPE_CURTAIN: curtain_shape(w->percent, w->in); break;
    default: angled_shape(w->percent, w->in); break;
  }
  gfx_hud(true);   /* over everything, not zoomed */
  gfx_custom(wipe_strip, NULL, 0, VIEW_H);
  gfx_hud(false);
}
