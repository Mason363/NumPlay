/* Among Us: the Skeld's walls, light walls, rooms and the vision polygon (see ship.h). */
#include <math.h>
#include <string.h>
#include "ship.h"
#include "world.h"

const char *const ROOM_NAMES[N_ROOMS] = {"Hallway",   "Storage", "Cafeteria", "Reactor",  "Upper Engine",
                                         "Navigation", "Admin",  "Electrical", "O2",      "Shields",
                                         "MedBay",    "Security", "Weapons",  "Lower Engine", "Communications"};

/* segments already looked at in this query (a query number per segment) */
static uint16_t stamp_w[N_WALL], stamp_s[N_SHADOW];
static uint16_t query;

void ship_init(void) {
  memset(stamp_w, 0, sizeof stamp_w);
  memset(stamp_s, 0, sizeof stamp_s);
  query = 0;
}

static void next_query(void) {
  if (++query == 0) {
    memset(stamp_w, 0, sizeof stamp_w);
    memset(stamp_s, 0, sizeof stamp_s);
    query = 1;
  }
}

static inline int cell_of(float x, float y) {
  int cx = (int)floorf(x - GRID_X0), cy = (int)floorf(y - GRID_Y0);
  if (cx < 0 || cy < 0 || cx >= GRID_W || cy >= GRID_H) return -1;
  return cy * GRID_W + cx;
}

static V2 closest(V2 p, const Seg *s) {
  float ax = s->x0, ay = s->y0, bx = s->x1 - ax, by = s->y1 - ay;
  float l = bx * bx + by * by, t = l > 0 ? ((p.x - ax) * bx + (p.y - ay) * by) / l : 0;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return (V2){ax + bx * t, ay + by * t};
}

static void push_out(V2 *p, const Seg *s, float r) {
  V2 q = closest(*p, s);
  float dx = p->x - q.x, dy = p->y - q.y, d2 = dx * dx + dy * dy;
  if (d2 >= r * r) return;
  float d = sqrtf(d2);
  if (d < 1e-5f) {
    /* on the line: out along its normal */
    float nx = -(s->y1 - s->y0), ny = s->x1 - s->x0, l = sqrtf(nx * nx + ny * ny);
    if (l < 1e-6f) return;
    p->x += nx / l * r, p->y += ny / l * r;
    return;
  }
  p->x += dx / d * (r - d), p->y += dy / d * (r - d);
}

V2 ship_move(V2 p, V2 d, float r) {
  float len = sqrtf(d.x * d.x + d.y * d.y);
  int steps = (int)(len / (r * 0.5f)) + 1;
  for (int k = 0; k < steps; k++) {
    p.x += d.x / steps, p.y += d.y / steps;
    for (int it = 0; it < 3; it++) {
      int c = cell_of(p.x, p.y);
      if (c >= 0)
        for (int i = WALL_START[c]; i < WALL_START[c + 1]; i++) push_out(&p, &WALL_SEGS[WALL_IDX[i]], r);
      for (int dd = 0; dd < N_DOORS; dd++) {
        if (!door_closed(dd)) continue;
        const DoorRec *D = &DOORS[dd];
        if (fabsf(p.x - D->x) > 2 || fabsf(p.y - D->y) > 2) continue;
        for (int s = 0; s < 4; s++) push_out(&p, &D->seg[s], r);
      }
    }
  }
  return p;
}

static bool cross(V2 a, V2 b, const Seg *s) {
  float d1 = (s->x1 - s->x0) * (a.y - s->y0) - (s->y1 - s->y0) * (a.x - s->x0);
  float d2 = (s->x1 - s->x0) * (b.y - s->y0) - (s->y1 - s->y0) * (b.x - s->x0);
  if ((d1 > 0) == (d2 > 0)) return false;
  float d3 = (b.x - a.x) * (s->y0 - a.y) - (b.y - a.y) * (s->x0 - a.x);
  float d4 = (b.x - a.x) * (s->y1 - a.y) - (b.y - a.y) * (s->x1 - a.x);
  return (d3 > 0) != (d4 > 0);
}

static bool doors_between(V2 a, V2 b) {
  for (int d = 0; d < N_DOORS; d++) {
    if (!door_closed(d)) continue;
    for (int s = 0; s < 4; s++)
      if (cross(a, b, &DOORS[d].seg[s])) return true;
  }
  return false;
}

static bool any_between(V2 a, V2 b, const Seg *segs, const uint16_t *start, const uint16_t *idx, uint16_t *stamp) {
  next_query();
  int x0 = (int)floorf(fminf(a.x, b.x) - GRID_X0), x1 = (int)floorf(fmaxf(a.x, b.x) - GRID_X0);
  int y0 = (int)floorf(fminf(a.y, b.y) - GRID_Y0), y1 = (int)floorf(fmaxf(a.y, b.y) - GRID_Y0);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= GRID_W) x1 = GRID_W - 1;
  if (y1 >= GRID_H) y1 = GRID_H - 1;
  /* the cells the segment's line passes near */
  float dx = b.x - a.x, dy = b.y - a.y, l = sqrtf(dx * dx + dy * dy) + 1e-6f;
  for (int cy = y0; cy <= y1; cy++)
    for (int cx = x0; cx <= x1; cx++) {
      float mx = GRID_X0 + cx + 0.5f - a.x, my = GRID_Y0 + cy + 0.5f - a.y;
      if (fabsf(mx * dy - my * dx) / l > 0.75f) continue;
      int c = cy * GRID_W + cx;
      for (int i = start[c]; i < start[c + 1]; i++) {
        int s = idx[i];
        if (stamp[s] == query) continue;
        stamp[s] = query;
        if (cross(a, b, &segs[s])) return true;
      }
    }
  return false;
}

bool ship_wall_between(V2 a, V2 b) {
  return any_between(a, b, WALL_SEGS, WALL_START, WALL_IDX, stamp_w) || doors_between(a, b);
}

bool ship_light_between(V2 a, V2 b) {
  return any_between(a, b, SHADOW_SEGS, SHADOW_START, SHADOW_IDX, stamp_s) || doors_between(a, b);
}

int ship_room_at(V2 p) {
  for (int i = 0; i < N_AREAS; i++) {
    const AreaRec *A = &AREAS[i];
    const V2 *v = AREA_PTS + A->first;
    bool in = false;
    for (int k = 0, j = A->n - 1; k < A->n; j = k++)
      if ((v[k].y > p.y) != (v[j].y > p.y) && p.x < v[j].x + (p.y - v[j].y) * (v[k].x - v[j].x) / (v[k].y - v[j].y))
        in = !in;
    if (in && A->room) return A->room;
  }
  return R_HALLWAY;
}

/* ---- the vision polygon: rays to the light walls' ends and around the circle ---- */

#define MAX_NEAR 160
static const Seg *near_segs[MAX_NEAR];
static int nnear;

static float ray(V2 o, float dx, float dy, float r) {
  float best = r;
  for (int i = 0; i < nnear; i++) {
    const Seg *s = near_segs[i];
    float ex = s->x1 - s->x0, ey = s->y1 - s->y0;
    float den = dx * ey - dy * ex;
    if (den > -1e-9f && den < 1e-9f) continue;
    float ax = s->x0 - o.x, ay = s->y0 - o.y;
    float t = (ax * ey - ay * ex) / den, u = (ax * dy - ay * dx) / den;
    if (t > 0 && t < best && u >= 0 && u <= 1) best = t;
  }
  return best;
}

static void add_near(const Seg *s, V2 p, float r) {
  if (nnear >= MAX_NEAR) return;
  /* (only segments that reach the circle) */
  V2 q = closest(p, s);
  float dx = q.x - p.x, dy = q.y - p.y;
  if (dx * dx + dy * dy > r * r) return;
  near_segs[nnear++] = s;
}

#define MAX_ANG 720
static float angs[MAX_ANG];

static void sort_floats(float *a, int n) {
  /* (a heap sort: no recursion, no buffer) */
  for (int start = n / 2 - 1; start >= -0; start--) {
    for (int root = start;;) {
      int ch = 2 * root + 1;
      if (ch >= n) break;
      if (ch + 1 < n && a[ch + 1] > a[ch]) ch++;
      if (a[root] >= a[ch]) break;
      float t = a[root];
      a[root] = a[ch], a[ch] = t, root = ch;
    }
    if (start == 0) break;
  }
  for (int end = n - 1; end > 0; end--) {
    float t = a[0];
    a[0] = a[end], a[end] = t;
    for (int root = 0;;) {
      int ch = 2 * root + 1;
      if (ch >= end) break;
      if (ch + 1 < end && a[ch + 1] > a[ch]) ch++;
      if (a[root] >= a[ch]) break;
      float u = a[root];
      a[root] = a[ch], a[ch] = u, root = ch;
    }
  }
}

int ship_vision(V2 p, float r, float *out, int max) {
  nnear = 0;
  next_query();
  int x0 = (int)floorf(p.x - r - GRID_X0), x1 = (int)floorf(p.x + r - GRID_X0);
  int y0 = (int)floorf(p.y - r - GRID_Y0), y1 = (int)floorf(p.y + r - GRID_Y0);
  if (x0 < 0) x0 = 0;
  if (y0 < 0) y0 = 0;
  if (x1 >= GRID_W) x1 = GRID_W - 1;
  if (y1 >= GRID_H) y1 = GRID_H - 1;
  for (int cy = y0; cy <= y1; cy++)
    for (int cx = x0; cx <= x1; cx++) {
      int c = cy * GRID_W + cx;
      for (int i = SHADOW_START[c]; i < SHADOW_START[c + 1]; i++) {
        int s = SHADOW_IDX[i];
        if (stamp_s[s] == query) continue;
        stamp_s[s] = query;
        add_near(&SHADOW_SEGS[s], p, r);
      }
    }
  for (int d = 0; d < N_DOORS; d++)
    if (door_closed(d))
      for (int s = 0; s < 4; s++) add_near(&DOORS[d].seg[s], p, r);
  int na = 0;
  const int CIRCLE = 72;
  for (int k = 0; k < CIRCLE; k++) angs[na++] = -3.14159265f + 6.2831853f * k / CIRCLE;
  for (int i = 0; i < nnear && na + 6 <= MAX_ANG; i++) {
    const Seg *s = near_segs[i];
    for (int e = 0; e < 2; e++) {
      float ex = (e ? s->x1 : s->x0) - p.x, ey = (e ? s->y1 : s->y0) - p.y;
      if (ex * ex + ey * ey > r * r) continue;
      float a = atan2f(ey, ex);
      angs[na++] = a - 0.0005f, angs[na++] = a, angs[na++] = a + 0.0005f;
    }
  }
  sort_floats(angs, na);
  int n = 0;
  float last = -10;
  for (int k = 0; k < na && n < max; k++) {
    if (angs[k] - last < 0.0001f) continue;
    last = angs[k];
    float dx = cosf(angs[k]), dy = sinf(angs[k]);
    float t = ray(p, dx, dy, r);
    out[2 * n] = p.x + dx * t, out[2 * n + 1] = p.y + dy * t;
    n++;
  }
  return n;
}
