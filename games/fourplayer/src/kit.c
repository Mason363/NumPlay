/* 2 3 4 Players: chance, numbers and the shapes every minigame draws with. */
#include "fp.h"

/* ---- chance (app.c seeds it, and every key pressed stirs it) */
uint32_t fp_seed = 1;
uint32_t rnd(void) {
  fp_seed ^= fp_seed << 13;
  fp_seed ^= fp_seed >> 17;
  return fp_seed ^= fp_seed << 5;
}
int rn(int n) { return n > 0 ? (int)(rnd() % (uint32_t)n) : 0; }
float rf(void) { return (float)(rnd() >> 8) * (1.0f / 16777216.0f); }
float rr(float a, float b) { return a + (b - a) * rf(); }

/* ---- numbers */
float fabs_(float v) { return v < 0 ? -v : v; }
float fsqrt(float v) { return v > 0 ? __builtin_sqrtf(v) : 0; }
float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
float wrap_pi(float a) {
  while (a > PI) a -= 2 * PI;
  while (a < -PI) a += 2 * PI;
  return a;
}
/* a parabola, refined: within a thousandth */
float fsin(float a) {
  a = wrap_pi(a);
  float y = 1.2732395f * a - 0.4052847f * a * fabs_(a);
  return 0.225f * (y * fabs_(y) - y) + y;
}
float fcos(float a) { return fsin(a + PI / 2); }
float fatan2(float y, float x) {
  float ax = fabs_(x), ay = fabs_(y);
  if (ax < 1e-9f && ay < 1e-9f) return 0;
  float a = ax > ay ? ay / ax : ax / ay, s = a * a;
  float r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;
  if (ay > ax) r = PI / 2 - r;
  if (x < 0) r = PI - r;
  return y < 0 ? -r : r;
}
bool collide(body_t *a, body_t *b, float r, float ma, float mb, float e) {
  float dx = b->x - a->x, dy = b->y - a->y, d2 = dx * dx + dy * dy;
  if (d2 >= r * r) return false;
  float d = fsqrt(d2), nx = 1, ny = 0;
  if (d > 0.001f) nx = dx / d, ny = dy / d;
  float over = r - d, ia = mb / (ma + mb), ib = ma / (ma + mb);
  a->x -= nx * over * ia, a->y -= ny * over * ia, b->x += nx * over * ib, b->y += ny * over * ib;
  float vn = (b->vx - a->vx) * nx + (b->vy - a->vy) * ny;
  if (vn < 0) {
    float j = -(1 + e) * vn / (1 / ma + 1 / mb);
    a->vx -= j * nx / ma, a->vy -= j * ny / ma, b->vx += j * nx / mb, b->vy += j * ny / mb;
  }
  return true;
}
#ifdef HOST
int fp_round_serial; /* (app.c counts the rounds) */
bool bot_fresh(int p) {
  static int seen[NMAX];
  if (seen[p] == fp_round_serial) return false;
  seen[p] = fp_round_serial;
  return true;
}
int bot_tap(int p, bool want) {
  static uint8_t was[NMAX];
  int r = want && !was[p];
  was[p] = (uint8_t)r;
  return r;
}
#endif

char *num(char *o, uint32_t v) { /* v in o, then the end of it */
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

/* ---- shapes */
static int iround(float v) { return (int)(v < 0 ? v - 0.5f : v + 0.5f); }
void disc(float x, float y, float r, color_t c) {
  int R = iround(r);
  if (R > 0) gfx_circle(iround(x), iround(y), R, c, 256);
}
void disc_a(float x, float y, float r, color_t c, int a) {
  int R = iround(r);
  if (R > 0 && a > 0) gfx_circle(iround(x), iround(y), R, c, a > 256 ? 256 : a);
}
void box(int x, int y, int w, int h, int r, color_t c) { gfx_rrect(x, y, w, h, r, c, 256); }

/* a convex polygon, its left and right edges softened */
void poly(const float *xy, int n, color_t c) {
  float top = xy[1], bot = xy[1];
  for (int i = 1; i < n; i++) top = xy[2 * i + 1] < top ? xy[2 * i + 1] : top, bot = xy[2 * i + 1] > bot ? xy[2 * i + 1] : bot;
  int r0 = (int)top, r1 = (int)bot + 1;
  if (r0 < gfx_y0) r0 = gfx_y0;
  if (r1 > gfx_y1) r1 = gfx_y1;
  for (int row = r0; row < r1; row++) {
    float yc = row + 0.5f, lo = 1e9f, hi = -1e9f;
    for (int i = 0; i < n; i++) {
      int j = i + 1 < n ? i + 1 : 0;
      float ya = xy[2 * i + 1], yb = xy[2 * j + 1];
      if ((ya <= yc) != (yb <= yc)) {
        float x = xy[2 * i] + (yc - ya) * (xy[2 * j] - xy[2 * i]) / (yb - ya);
        lo = x < lo ? x : lo, hi = x > hi ? x : hi;
      }
    }
    if (hi <= lo) continue;
    if (lo < 0) lo = 0;
    if (hi > SCREEN_W) hi = SCREEN_W;
    int a = (int)lo, b = (int)hi;
    if (a == b) {
      gfx_fill_alpha(a, row, 1, 1, c, (int)((hi - lo) * 256));
      continue;
    }
    gfx_fill_alpha(a, row, 1, 1, c, (int)((a + 1 - lo) * 256));
    if (b > a + 1) gfx_fill(a + 1, row, b - a - 1, 1, c);
    if (b < SCREEN_W) gfx_fill_alpha(b, row, 1, 1, c, (int)((hi - b) * 256));
  }
}

void quad(float x, float y, float a, float l0, float l1, float w, color_t c) {
  float cs = fcos(a), sn = fsin(a), p[8];
  for (int i = 0; i < 4; i++) {
    float l = i == 1 || i == 2 ? l1 : l0, e = i < 2 ? -w : w;
    p[2 * i] = x + cs * l - sn * e, p[2 * i + 1] = y + sn * l + cs * e;
  }
  if (y + (l0 > l1 ? l0 : l1) + w + 2 < gfx_y0 - 64 || y - 200 > gfx_y1) return;
  poly(p, 4, c);
}

void line(float x0, float y0, float x1, float y1, float w, color_t c) {
  float dx = x1 - x0, dy = y1 - y0, l = fsqrt(dx * dx + dy * dy);
  if (l < 0.01f) return;
  float nx = -dy / l * w / 2, ny = dx / l * w / 2;
  float p[8] = {x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny};
  poly(p, 4, c);
}

void say(const np_font_t *f, int cx, int base, const char *s, color_t c) {
  gfx_text_center(f, cx + 1, base + 2, s, 0, 150);
  gfx_text_center(f, cx, base, s, c, 256);
}

void eyes(float x, float y, float r, float look) {
  float cs = fcos(look), sn = fsin(look);
  float ex = x + cs * r * 0.32f, ey = y + sn * r * 0.32f - r * 0.12f, s = r * 0.33f, e = r * 0.27f;
  if (e < 1.6f) e = 1.6f;
  for (int i = -1; i <= 1; i += 2) {
    disc(ex + i * s, ey, e, WHITE);
    disc(ex + i * s + cs * e * 0.45f, ey + sn * e * 0.45f, e * 0.55f, HEX(0x15122A));
  }
}

void blob(float x, float y, float r, int p, float look) {
  disc_a(x + 1.5f, y + 2.5f, r, 0, 70); /* its shadow */
  disc(x, y, r, dark[p]);
  disc(x - r * 0.06f, y - r * 0.1f, r * 0.9f, col[p]);
  disc_a(x - r * 0.35f, y - r * 0.4f, r * 0.32f, lite[p], 170);
  eyes(x, y, r, look);
}

/* the backspace key's sign: an arrow head with a cross in it */
static void erase_sign(int x, int mid, color_t c) {
  float p[10] = {x, mid, x + 5, mid - 5, x + 15, mid - 5, x + 15, mid + 5, x + 5, mid + 5};
  poly(p, 5, c);
  line(x + 7, mid - 2.5f, x + 12, mid + 2.5f, 1.4f, INK);
  line(x + 7, mid + 2.5f, x + 12, mid - 2.5f, 1.4f, INK);
}
int keytag_w(int p) { return *keyname(p) ? gfx_text_width(&np_font_body, keyname(p)) : 15; }
void keytag(int cx, int base, int p, color_t c) {
  if (*keyname(p)) gfx_text_center(&np_font_body, cx, base, keyname(p), c, 256);
  else erase_sign(cx - 8, base - 4, c);
}
