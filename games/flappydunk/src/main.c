/* Flappy Dunk: flap the winged ball and drop it through every hoop.
 *
 * Voodoo's game as it plays on phones, endless mode: each press of OK flaps the wings once, the ball
 * falls between flaps, and it must go down through each hoop it meets. Miss one and the run is over.
 * Through without touching the rim is a swish, and swishes in a row are worth more and set the ball
 * on fire. The floor and the ceiling bounce it back. Its gravity, flap, speed, ball and hoop sizes and
 * how the rims and floor behave come from the game's Android build (its scene and physics settings).
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and saves
 * through its file functions; on its own (FlappyDunk.nwa) it brings them along.
 *
 * Made by Bobydeluxe and Mason Chen as part of NumPlay. */
#include <eadk.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"

#if NP_MODULE /* (np_file_read and np_file_write: NumPlay's, launcher/src/np.h) */
#define np_app_begin()
#define np_app_end() 0
#else
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
#ifndef HOST /* (a computer build of the tests has no app header) */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Flappy Dunk";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

#define KEY(k) (1ull << (k))

/* ------------------------------------------------------------ the original's numbers
   In its units (the camera shows 19.2 of them up and down) and seconds: Physics2D's gravity, the
   flap and scroll speed between its challenges' "Strong wings!", "Rollercoaster" and "Speed up!"
   settings, the ball's collider, the hoop prefab's rims and score box, the floor and ceiling boxes. */
#define G 40.0f       /* gravity */
#define FLAP 14.0f    /* a flap sets the ball rising at this speed */
#define SCROLL 4.0f   /* the ball's speed forward */
#define R 0.6f        /* the ball */
#define RIM_X 1.28f   /* the rims' centres, either side of the hoop's, times its width */
#define RIM_R 0.272f  /* and their radius */
#define BOX_W 0.8f    /* the score box under the rim: half its width (times the hoop's), half its height */
#define BOX_H 0.1f
#define MISS_X 3.5f   /* reaching the trigger 4 past a hoop (it is 1 wide) before scoring in it: missed */
#define TOP 9.5f      /* the ceiling and the floor, either side of the middle */
#define BOUNCE 0.7f   /* how much of its speed the ball keeps off them */
#define GAP 7.0f      /* from one hoop to the next */
#define DT 0.02f      /* Unity's physics step */

/* on screen: 12 pixels a unit, the middle of the field at y = 120, the ball at x = 72 */
#define PX 12.0f
#define BALL_SX 72
#define SY(y) (120.0f - (y) * PX)

#define NH 4 /* hoops alive at once */
enum { H_STATIC, H_VERTICAL, H_LATERAL };
typedef struct {
  float x, y, bx, by, w, c, s, v; /* its middle, where it moves about, its width, its tilt (cos, sin), its speed */
  uint8_t type, scored, touched, inside;
} hoop_t;

/* flappydunk.sav: the records, the ball chosen, and the run in progress (kept when you leave) */
static struct __attribute__((aligned(4))) { /* whole words, as the sum reads them (a TI-84 CE packs structs) */
  uint8_t magic, version, flags, skin; /* flags: 1 a run to continue, 2 the guide was read */
  uint32_t sum; /* of the words after it: a damaged file starts afresh */
  uint32_t best, swishes, games, best_streak;
  struct {
    float x, y, vy;
    uint32_t score, streak, swishes, rng, n; /* n: hoops made so far, for what comes next */
    hoop_t hoop[NH];
  } run;
} sv;
#define SAVE_NAME "flappydunk.sav"
#define RUN sv.run

static color_t strip[SCREEN_W * STRIP_H];
static uint32_t now, best0, pop_t, flap_t, net_t[NH], over_t;
static int pop_pts, pop_streak, ov, sel;
static float pop_x, pop_y, spin_c = 1, spin_s, spin, cam;
static float trail_x[8], trail_y[8];
static bool over, ready, contact;

enum { OV_NONE, OV_MENU, OV_BALLS, OV_HELP, OV_OVER };

static uint32_t rnd(void) {
  uint32_t x = RUN.rng;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return RUN.rng = x;
}
static float frand(float a, float b) { return a + (b - a) * (float)(rnd() & 0xFFFF) / 65535.0f; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float fabs_(float v) { return v < 0 ? -v : v; }
static float sqrt_(float v) { return __builtin_sqrtf(v); }
static void memset_(uint8_t *p, uint32_t n) {
  while (n--) *p++ = 0;
}

static char *num(char *o, uint32_t v) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *label(char *o, const char *s, uint32_t v) {
  char *e = o;
  while (*s) *e++ = *s++;
  num(e, v);
  return o;
}

/* ------------------------------------------------------------ the balls (skins)
   Unlocked with goals from the original's own list. */
#define NSKIN 6
static const char *const skin_names[NSKIN] = {T("Classic"), T("Watermelon"), T("Ocean"), T("Candy"), T("Gold"), T("Night")};
static const char *const skin_goals[NSKIN] = {"", T("Score 20 or more"), T("Score 50 or more"),
                                              T("Achieve 10 swishes in a row"), T("Score 125 or more"),
                                              T("Achieve 100 swishes in total")};
/* light, middle and dark, then the seams */
static const uint32_t skin_rgb[NSKIN][4] = {
  {0xFFC233, 0xFF8A00, 0xD9480F, 0x1A1A1A}, {0x8EE35A, 0x3DAA3A, 0x1E6E2A, 0xE84A5F},
  {0x7FD4FF, 0x2E8BEF, 0x1B4FB0, 0x0D1B3D}, {0xFFB3D9, 0xFF5FA8, 0xC92C78, 0x5A0F35},
  {0xFFF07A, 0xF2C200, 0xB98600, 0x5C3D00}, {0x6B6B80, 0x3A3A4A, 0x1E1E28, 0xF2C200}};
static bool unlocked(int k) {
  switch (k) {
    case 1: return sv.best >= 20;
    case 2: return sv.best >= 50;
    case 3: return sv.best_streak >= 10;
    case 4: return sv.best >= 125;
    case 5: return sv.swishes >= 100;
  }
  return true;
}

/* ------------------------------------------------------------ the rules */
static uint32_t checksum(void) {
  uint32_t h = 0x811C9DC5u;
  for (const uint32_t *w = &sv.sum + 1; w < (const uint32_t *)(&sv + 1); w++) h = (h ^ *w) * 0x01000193u;
  return h;
}
static void save(void) {
  sv.magic = 'F', sv.version = 1, sv.sum = checksum();
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}

/* the next hoop, GAP after the last one: higher or lower than it, and as the score climbs, tilted,
   moving and narrower, as the original's challenges make them */
static void spawn(hoop_t *h, float x, float last_y) {
  uint32_t n = RUN.n++;
  float y = clampf(last_y + frand(-4.0f, 4.0f), -5.5f, 5.5f);
  *h = (hoop_t){x, y, x, y, 1, 1, 0, 0, H_STATIC, 0, 0, 0};
  uint32_t r = rnd() % 100;
  if (n >= 10 && r < 35) { /* tilted 10 or 20 degrees, either way */
    static const float cs[2][2] = {{0.98481f, 0.17365f}, {0.93969f, 0.34202f}};
    int k = (int)(rnd() & 1);
    h->c = cs[k][0], h->s = cs[k][1] * (rnd() & 1 ? 1 : -1);
  }
  r = rnd() % 100;
  if (n >= 20 && r < 22) h->type = H_VERTICAL, h->v = frand(1.5f, 3.0f), h->y = h->by = clampf(y, -4.0f, 4.0f);
  else if (n >= 20 && r < 34) h->type = H_LATERAL, h->v = frand(1.5f, 2.5f);
  if (n >= 40 && rnd() % 100 < 30) h->w = rnd() & 1 ? 0.8f : 0.7f;
}

static void new_run(void) {
  RUN.x = 0, RUN.y = 0, RUN.vy = 0;
  RUN.score = RUN.streak = RUN.swishes = RUN.n = 0;
  RUN.rng = eadk_random() ^ now;
  if (!RUN.rng) RUN.rng = 1;
  float x = 10.0f, y = 0;
  for (int i = 0; i < NH; i++) {
    spawn(&RUN.hoop[i], x, y);
    if (!i) RUN.hoop[0].y = RUN.hoop[0].by = -1.5f; /* the first one just below the ball */
    y = RUN.hoop[i].by, x += GAP;
  }
  sv.flags |= 1, over = false, ready = true, best0 = sv.best, cam = RUN.x - BALL_SX / PX;
  for (int i = 0; i < 8; i++) trail_x[i] = BALL_SX, trail_y[i] = SY(0);
}

static void end_run(void) {
  over = true, over_t = now, sv.flags &= (uint8_t)~1;
  sv.games++;
  save();
}

static void flap(void) {
  RUN.vy = FLAP, flap_t = now;
  if (ready) ready = false;
}

/* a basket: 1, or for a swish (no rim touched) 2, 3, 4... for each swish in a row */
static void score(int i) {
  hoop_t *h = &RUN.hoop[i];
  h->scored = 1;
  if (h->touched) RUN.streak = 0, pop_pts = 1;
  else {
    RUN.streak++, RUN.swishes++, sv.swishes++;
    pop_pts = (int)RUN.streak + 1;
    if (RUN.streak > sv.best_streak) sv.best_streak = RUN.streak;
  }
  RUN.score += (uint32_t)pop_pts;
  if (RUN.score > sv.best) sv.best = RUN.score;
  pop_t = now, pop_streak = (int)RUN.streak, net_t[i] = now;
  pop_x = h->x, pop_y = h->y;
}

/* one rim (a fixed circle) against the ball: pushed out, no bounce, rolling along it */
static void rim(hoop_t *h, float rx, float ry, float *vx) {
  float dx = RUN.x - rx, dy = RUN.y - ry, rr = R + RIM_R, d2 = dx * dx + dy * dy;
  if (d2 >= rr * rr) return;
  float d = sqrt_(d2), nx = 0, ny = 1;
  if (d > 1e-4f) nx = dx / d, ny = dy / d;
  RUN.x += nx * (rr - d), RUN.y += ny * (rr - d);
  float vn = *vx * nx + RUN.vy * ny;
  if (vn < 0) *vx -= vn * nx, RUN.vy -= vn * ny;
  spin = (*vx * ny - RUN.vy * nx) / R; /* rolling over it */
  h->touched = 1, contact = true;
}

static void step(void) {
  float vx = SCROLL;
  RUN.vy -= G * DT;
  contact = false;
  for (int sub = 0; sub < 2; sub++) {
    RUN.x += vx * DT / 2, RUN.y += RUN.vy * DT / 2;
    for (int i = 0; i < NH; i++) {
      hoop_t *h = &RUN.hoop[i];
      if (h->type == H_VERTICAL) {
        h->y += h->v * DT / 2;
        if ((h->y > h->by + 2.5f && h->v > 0) || (h->y < h->by - 2.5f && h->v < 0)) h->v = -h->v;
      } else if (h->type == H_LATERAL) {
        h->x += h->v * DT / 2;
        if ((h->x > h->bx + 1.5f && h->v > 0) || (h->x < h->bx - 1.5f && h->v < 0)) h->v = -h->v;
      }
      if (h->x > RUN.x + 4 || h->x < RUN.x - 6) continue;
      float ax = RIM_X * h->w * h->c, ay = RIM_X * h->w * h->s;
      rim(h, h->x - ax, h->y - ay, &vx);
      rim(h, h->x + ax, h->y + ay, &vx);
      /* the score box, in the hoop's own frame: the ball going in downwards */
      float lx = (RUN.x - h->x) * h->c + (RUN.y - h->y) * h->s, ly = (RUN.y - h->y) * h->c - (RUN.x - h->x) * h->s;
      float ex = fabs_(lx) - BOX_W * h->w, ey = fabs_(ly) - BOX_H;
      float qx = ex > 0 ? ex : 0, qy = ey > 0 ? ey : 0;
      bool in = qx * qx + qy * qy < R * R;
      float lvy = RUN.vy * h->c - vx * h->s;
      if (in && !h->inside && !h->scored && lvy < 0) score(i);
      h->inside = in;
    }
    if (RUN.y < -TOP + R) RUN.y = -TOP + R, RUN.vy = RUN.vy < 0 ? -RUN.vy * BOUNCE : RUN.vy, contact = true;
    if (RUN.y > TOP - R) RUN.y = TOP - R, RUN.vy = RUN.vy > 0 ? -RUN.vy * BOUNCE : RUN.vy;
  }
  if (!contact) spin *= 1 - DT; /* the ball's angular damping */
  if (RUN.y <= -TOP + R + 0.01f) spin = -vx / R; /* rolling on the floor */
  for (int i = 0; i < NH; i++) {
    hoop_t *h = &RUN.hoop[i];
    if (!h->scored && RUN.x + R > h->x + MISS_X) {
      RUN.streak = 0;
      end_run();
      return;
    }
  }
  /* a hoop well behind makes way for the next one, GAP after the farthest */
  int far = 0;
  for (int i = 1; i < NH; i++)
    if (RUN.hoop[i].bx > RUN.hoop[far].bx) far = i;
  for (int i = 0; i < NH; i++) {
    hoop_t *h = &RUN.hoop[i];
    if (h->x < RUN.x - 8 && h->scored) spawn(h, RUN.hoop[far].bx + GAP, RUN.hoop[far].by), net_t[i] = 0, far = i;
  }
}

/* ------------------------------------------------------------ drawing */
static inline void px(int x, int y, color_t c) {
  if ((unsigned)x < SCREEN_W && y >= gfx_y0 && y < gfx_y1) {
#ifdef __TICE__
    if (gfx_listing) { /* (its strip is drawn every time) */
      gfx_list_rows(y, y + 1);
      return;
    }
#endif
    gfx_buf[(y - gfx_y0) * SCREEN_W + x] = c;
  }
}
static void line(float x0, float y0, float x1, float y1, color_t c) {
  float dx = x1 - x0, dy = y1 - y0, n = fabs_(dx) > fabs_(dy) ? fabs_(dx) : fabs_(dy);
  if ((y0 < gfx_y0 - 1 && y1 < gfx_y0 - 1) || (y0 > gfx_y1 && y1 > gfx_y1)) return;
#ifdef __TICE__ /* (the TI-84 Plus CE: the same points, stepping in 65536ths of a pixel) */
  {
    int32_t fx = (int32_t)(x0 * 65536), fy = (int32_t)(y0 * 65536), m = (int)n;
    int32_t sx = (int32_t)(dx / (n + 0.01f) * 65536), sy = (int32_t)(dy / (n + 0.01f) * 65536);
    for (int i = 0; i <= m; i++, fx += sx, fy += sy) px((int)(fx >> 16), (int)(fy >> 16), c);
    return;
  }
#endif
  for (int i = 0; i <= (int)n; i++) px((int)(x0 + dx * i / (n + 0.01f)), (int)(y0 + dy * i / (n + 0.01f)), c);
}

/* the white brick wall, scrolling at half the speed of the course */
static void wall(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, HEX(0xF7F7F7));
  static const uint8_t bricks[][2] = {{8, 14},  {30, 22}, {96, 9},   {120, 40}, {64, 58},  {14, 80},  {142, 72},
                                      {88, 96}, {40, 118}, {120, 126}, {6, 150}, {70, 148}, {100, 170}, {28, 190},
                                      {136, 196}, {60, 214}, {12, 228}, {110, 232}};
  int off = (int)(cam * PX * 0.5f) % 160;
  for (unsigned b = 0; b < sizeof bricks / sizeof bricks[0]; b++) {
    int y = bricks[b][1];
    if (y + 8 <= gfx_y0 || y >= gfx_y1) continue;
    for (int x = bricks[b][0] - off; x < SCREEN_W; x += 160) {
      if (x + 19 < 0) continue;
      color_t c = HEX(0xDCDCDC);
      gfx_fill(x + 1, y, 17, 1, c), gfx_fill(x + 1, y + 7, 17, 1, c);
      gfx_fill(x, y + 1, 1, 6, c), gfx_fill(x + 18, y + 1, 1, 6, c);
    }
  }
}

/* a hoop's ring: a tube along an ellipse, its back half (behind the ball) or its front half */
static void ring(const hoop_t *h, bool front) {
  float cx = BALL_SX + (h->x - RUN.x) * PX, cy = SY(h->y), a = RIM_X * h->w * PX, b = 0.42f * PX;
  if (cy + 20 < gfx_y0 || cy - 20 > gfx_y1 || cx < -30 || cx > SCREEN_W + 30) return;
  /* the points along the half, by a rotating unit vector (16 steps of 11.25 degrees) */
  float ux = front ? -1 : 1, uy = 0;
  const float k = 0.98079f, q = 0.19509f;
  float pts[17][2];
  for (int i = 0; i <= 16; i++) { /* (a cos t, b sin t) in the hoop's frame, turned by its tilt */
    float ex = a * ux, ey = b * uy;
    pts[i][0] = cx + ex * h->c - ey * h->s, pts[i][1] = cy - (ex * h->s + ey * h->c);
    float nx = ux * k - uy * q, ny = uy * k + ux * q;
    ux = nx, uy = ny;
  }
  color_t outline = HEX(0x1A1A1A);
  color_t body = front ? HEX(0xE8221B) : HEX(0xA8120E);
  for (int i = 0; i <= 16; i++) gfx_circle((int)pts[i][0], (int)pts[i][1], 3, outline, 256);
  for (int i = 0; i <= 16; i++) gfx_circle((int)pts[i][0], (int)pts[i][1], 2, body, 256);
  if (front)
    for (int i = 3; i <= 13; i++) px((int)pts[i][0], (int)pts[i][1] - 1, HEX(0xFF8A80));
}

/* the net under the rim, stretching when a ball goes through */
static void net(int i) {
  const hoop_t *h = &RUN.hoop[i];
  float cx = BALL_SX + (h->x - RUN.x) * PX, cy = SY(h->y), a = RIM_X * h->w * PX;
  if (cy - 6 > gfx_y1 || cy + 30 < gfx_y0 || cx < -30 || cx > SCREEN_W + 30) return;
  uint32_t t = net_t[i] ? now - net_t[i] : 999;
  float len = 17 + (t < 300 ? (float)(300 - t) * 0.03f : 0), bot = 0.55f;
  color_t c = HEX(0xB4B4B4);
  /* in the hoop's frame: x along the rim, d down from it */
#define NP_(lx, d) cx + (lx) * h->c + (d) * h->s, cy - (lx) * h->s + (d) * h->c
  for (int j = -2; j <= 2; j++) {
    float tx = a * 0.8f * j / 2, bx = a * bot * j / 2;
    line(NP_(tx, 3), NP_(bx, len), c);
  }
  for (int r = 1; r <= 2; r++) {
    float y = len * r / 3, w = a * 0.8f + (a * bot - a * 0.8f) * r / 3;
    line(NP_(-w, y), NP_(w, y), c);
  }
  line(NP_(-a * bot, len), NP_(a * bot, len), c);
#undef NP_
}

/* the ball, turning with its spin: the skin's orange (or other) with a light from the top left, the
   seams of a basketball, a black outline */
static void ball_at(int cx, int cy, float rad, int skin, float rc, float rs) {
  if (cy + rad + 2 < gfx_y0 || cy - rad - 2 >= gfx_y1) return;
  color_t col[5];
  for (int i = 0; i < 4; i++) col[i] = gfx_rgb(skin_rgb[skin][i]);
  col[4] = HEX(0x111111);
  int n = (int)rad + 2;
  float inv = 1.0f / rad;
#ifdef __TICE__ /* (the TI-84 Plus CE: in 32nds of a pixel, the same tests squared: no float, no square root) */
  {
    int RC = (int)(rc * 32), RS = (int)(rs * 32), r32 = (int)(rad * 32), w = 16; /* (w: half a pixel) */
    int in2 = (r32 - 32) * (r32 - 32), out2 = r32 * r32, cx1 = (int)(1.1f * r32), ar = (int)(0.78f * r32);
    int lo = (ar - w) * (ar - w), hi = (ar + w) * (ar + w), l1 = (int)(0.35f * rad * 160), l2 = (int)(-0.3f * rad * 160);
    for (int y = -n; y <= n; y++) {
      int sy = cy + y;
      if (sy < gfx_y0 || sy >= gfx_y1) continue;
      for (int x = -n; x <= n; x++) {
        int d2 = (x * x + y * y) * 1024;
        if (d2 > out2) continue;
        color_t c;
        if (d2 > in2) c = col[4];
        else {
          int u = x * RC + y * RS, v = y * RC - x * RS, v2 = v * v;
          int q1 = (u - cx1) * (u - cx1) + v2, q2 = (u + cx1) * (u + cx1) + v2;
          if ((u < w && u > -w) || (v < w && v > -w) || (q1 > lo && q1 < hi) || (q2 > lo && q2 < hi)) c = col[3];
          else {
            int l = -96 * x - 112 * y; /* (-0.6 and -0.7, times 160) */
            c = l > l1 ? col[0] : l > l2 ? col[1] : col[2];
          }
        }
        px(cx + x, sy, c);
      }
    }
    return;
  }
#endif
  for (int y = -n; y <= n; y++) {
    int sy = cy + y;
    if (sy < gfx_y0 || sy >= gfx_y1) continue;
    for (int x = -n; x <= n; x++) {
      float fx = x * inv, fy = y * inv, d2 = fx * fx + fy * fy;
      if (d2 > 1.0f) continue;
      color_t c;
      if (d2 > (1 - inv) * (1 - inv)) c = col[4];
      else {
        float u = fx * rc + fy * rs, v = fy * rc - fx * rs, w = 0.5f * inv;
        float a1 = sqrt_((u - 1.1f) * (u - 1.1f) + v * v) - 0.78f, a2 = sqrt_((u + 1.1f) * (u + 1.1f) + v * v) - 0.78f;
        if (fabs_(u) < w || fabs_(v) < w || fabs_(a1) < w || fabs_(a2) < w) c = col[3];
        else {
          float l = -0.6f * fx - 0.7f * fy;
          c = l > 0.35f ? col[0] : l > -0.3f ? col[1] : col[2];
        }
      }
      px(cx + x, sy, c);
    }
  }
}

/* a wing: three feathers swept back from the joint, the top one longest and over the others, white with
   a black outline, turned by (c, s) */
static void wing(int jx, int jy, float c, float s, bool back) {
  if (jy + 14 < gfx_y0 || jy - 14 >= gfx_y1) return;
  static const float fe[3][4] = {{6.2f, 2.0f, 6.6f, 2.2f}, {4.8f, -0.4f, 5.0f, 1.8f}, {3.4f, -2.5f, 3.6f, 1.5f}};
  color_t ink = HEX(0x111111), col[3] = {0xFFFF, HEX(0xEBEBEB), HEX(0xDEDEDE)};
  if (back) col[0] = col[1] = col[2] = HEX(0xC8C8C8);
#ifdef __TICE__ /* (the TI-84 Plus CE: each way the wing turns worked out once, its pixels' colours) */
  static struct {
    float c, s;
    uint8_t k[27 * 27]; /* 0 none, 1-3 a feather, 4 the ink */
  } shapes[6];
  static int nshapes;
  int i = 0;
  while (i < nshapes && (shapes[i].c != c || shapes[i].s != s)) i++;
  if (i == nshapes) {
    if (i == 6) i = 0, nshapes = 0;
    shapes[i].c = c, shapes[i].s = s, nshapes++;
    for (int y = -13, n = 0; y <= 13; y++)
      for (int x = -13; x <= 13; x++, n++) {
        float wx = -(x * c + y * s), wy = -(y * c - x * s);
        shapes[i].k[n] = 0;
        for (int k = 0; k < 3; k++) {
          float dx = (wx - fe[k][0]) / (fe[k][2] + 1), dy = (wy - fe[k][1]) / (fe[k][3] + 1), e = dx * dx + dy * dy;
          if (e >= 1) continue;
          dx = (wx - fe[k][0]) / fe[k][2], dy = (wy - fe[k][1]) / fe[k][3];
          shapes[i].k[n] = (uint8_t)(dx * dx + dy * dy < 1 ? k + 1 : 4);
          break;
        }
      }
  }
  const uint8_t *m = shapes[i].k;
  for (int y = -13; y <= 13; y++, m += 27) {
    int sy = jy + y;
    if (sy < gfx_y0 || sy >= gfx_y1) continue;
    for (int x = 0; x < 27; x++)
      if (m[x]) px(jx - 13 + x, sy, m[x] == 4 ? ink : col[m[x] - 1]);
  }
  return;
#endif
  for (int y = -13; y <= 13; y++) {
    int sy = jy + y;
    if (sy < gfx_y0 || sy >= gfx_y1) continue;
    for (int x = -13; x <= 13; x++) {
      float wx = -(x * c + y * s), wy = -(y * c - x * s); /* screen to wing: x back (to the left), y up */
      for (int k = 0; k < 3; k++) {
        float dx = (wx - fe[k][0]) / (fe[k][2] + 1), dy = (wy - fe[k][1]) / (fe[k][3] + 1), e = dx * dx + dy * dy;
        if (e >= 1) continue;
        dx = (wx - fe[k][0]) / fe[k][2], dy = (wy - fe[k][1]) / fe[k][3];
        px(jx + x, sy, dx * dx + dy * dy < 1 ? col[k] : ink);
        break;
      }
    }
  }
}

/* the ball with its wings (one behind, one in front), and its fire when swishing in a row */
static void dunky(int cx, int cy, int skin, float rc, float rs, uint32_t since_flap, bool fire) {
  /* the wings: raised 25 degrees as it glides; a flap beats them down to -35 and back */
  static const float wc[5] = {0.9063f, 0.9962f, 0.9397f, 0.8192f, 0.9848f}, ws[5] = {0.4226f, 0.0872f, -0.3420f, -0.5736f, -0.1736f};
  int f = since_flap < 50 ? 4 : since_flap < 110 ? 3 : since_flap < 170 ? 2 : since_flap < 230 ? 1 : 0;
  float c = wc[f], s = ws[f];
  if (fire)
    for (int i = 7; i >= 0; i--) {
      int r = 7 - i * 3 / 4, jx = (int)((now / 40 + (uint32_t)i * 7) % 3) - 1;
      gfx_circle((int)trail_x[i] + jx, (int)trail_y[i] - jx, r + 2, HEX(0xFF6A00), 150 - i * 12);
      gfx_circle((int)trail_x[i] - jx, (int)trail_y[i] + jx, r, HEX(0xFFD23A), 170 - i * 14);
    }
  wing(cx + 2, cy - 6, c, s, true);
  ball_at(cx, cy, 7.2f, skin, rc, rs);
  wing(cx - 3, cy - 5, c, s, false);
}

static void button(int x, int y, int w, const char *s, bool on) {
  gfx_rrect(x, y, w, 26, 13, on ? HEX(0xFF8A00) : HEX(0x3A3A3A), 256);
  gfx_text_center(&np_font_body, x + w / 2, y + 18, s, 0xFFFF, 256);
}

static const char *const menu_items[] = {T("Resume"), T("Balls"), T("How to play"), T("Quit")};
static const char *const over_items[] = {T("Play again"), T("Balls"), T("Quit")};

static void panel(int y, int h) {
  gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 110);
  gfx_rrect(50, y, 220, h, 14, 0xFFFF, 256);
  gfx_rrect_outline(50, y, 220, h, 14, 2, HEX(0x1A1A1A), 256);
}

static void overlay(void) {
  char s[24];
  if (ov == OV_MENU) {
    panel(28, 186);
    gfx_text_center(&np_font_title, 160, 58, T("Paused"), HEX(0x1A1A1A), 256);
    for (int i = 0; i < 4; i++) button(90, 72 + 34 * i, 140, menu_items[i], sel == i);
  } else if (ov == OV_OVER) {
    bool rec = RUN.score > best0;
    panel(24, 200);
    gfx_text_center(&np_font_title, 160, 54, rec ? T("New best!") : T("Game over"), HEX(0x1A1A1A), 256);
    num(s, RUN.score);
    gfx_text_center(&np_font_title, 160, 84, s, HEX(0xF26B00), 256);
    gfx_text_center(&np_font_body, 160, 104, label(s, T("Best "), sv.best), HEX(0x6A6A6A), 256);
    for (int i = 0; i < 3; i++) button(90, 114 + 34 * i, 140, over_items[i], sel == i);
  } else if (ov == OV_HELP) {
    panel(20, 204);
    gfx_text_center(&np_font_title, 160, 48, T("How to play"), HEX(0x1A1A1A), 256);
    gfx_paragraph(&np_font_body, 160, 70, 196, 15,
                  T("Press OK to flap. Drop the ball down through every hoop: miss one and the run is over. Go in without touching the rim for a swish. Swishes in a row are worth more."),
                  HEX(0x3A3A3A), 256);
    gfx_text_right(&np_font_body, 150, 164, T("OK"), HEX(0xD9480F), 256);
    gfx_text(&np_font_body, 160, 164, T("flap"), HEX(0x3A3A3A), 256);
    gfx_text_right(&np_font_body, 150, 181, T("Back"), HEX(0xD9480F), 256);
    gfx_text(&np_font_body, 160, 181, T("menu"), HEX(0x3A3A3A), 256);
    button(110, 190, 100, T("Play"), true);
  } else if (ov == OV_BALLS) {
    panel(8, 224);
    gfx_text_center(&np_font_title, 160, 34, T("Balls"), HEX(0x1A1A1A), 256);
    for (int k = 0; k < NSKIN; k++) {
      int x = 92 + (k % 3) * 68, y = 70 + (k / 3) * 66;
      bool on = k == sel, have = unlocked(k);
      if (k == sv.skin) gfx_rrect(x - 30, y - 24, 60, 60, 10, HEX(0xFFE2B8), 256);
      if (on) gfx_rrect_outline(x - 30, y - 24, 60, 60, 10, 3, HEX(0xFF8A00), 256);
      if (have) ball_at(x, y, 13.0f, k, 0.96f, 0.28f);
      else gfx_circle(x, y, 13, HEX(0xD0D0D0), 256), gfx_text_center(&np_font_title, x, y + 7, "?", 0xFFFF, 256);
      gfx_text_center(&np_font_small, x, y + 30, have ? skin_names[k] : T("Locked"), HEX(0x3A3A3A), 256);
    }
    const char *goal = unlocked(sel) ? (sel == sv.skin ? T("In play") : T("OK to play with it")) : skin_goals[sel];
    gfx_text_center(&np_font_body, 160, 220, goal, HEX(0xD9480F), 256);
  }
}

static void scene(void *ctx) {
  char s[16];
  wall();
  /* the ceiling and the floor */
  gfx_fill(0, 0, SCREEN_W, (int)SY(TOP), HEX(0xE9E9E9));
  gfx_fill(0, (int)SY(TOP) - 1, SCREEN_W, 2, HEX(0x1A1A1A));
  gfx_fill(0, (int)SY(-TOP), SCREEN_W, SCREEN_H - (int)SY(-TOP), HEX(0xE9E9E9));
  gfx_fill(0, (int)SY(-TOP), SCREEN_W, 2, HEX(0x1A1A1A));
  for (int i = 0; i < NH; i++) ring(&RUN.hoop[i], false);
  int bx = BALL_SX + (int)((RUN.x - cam - BALL_SX / PX) * PX), by = (int)SY(RUN.y);
  dunky(bx, by, sv.skin, spin_c, spin_s, now - flap_t, !over && RUN.streak >= 2);
  for (int i = 0; i < NH; i++) net(i);
  for (int i = 0; i < NH; i++) ring(&RUN.hoop[i], true);

  /* what the last basket was worth, rising from its hoop */
  uint32_t t = now - pop_t;
  if (t < 900 && pop_t) {
    int a = t < 600 ? 256 : (int)(900 - t) * 256 / 300, x = BALL_SX + (int)((pop_x - RUN.x) * PX), y = (int)SY(pop_y) - 18 - (int)t / 40;
    if (pop_streak) /* "Swish!", then "Swish x2", "Swish x3"... */
      gfx_text_center(&np_font_title, x, y - 16, pop_streak > 1 ? label(s, T("Swish x"), (uint32_t)pop_streak) : T("Swish!"),
                      HEX(0xF26B00), a);
    gfx_text_center(&np_font_body, x, y, label(s, "+", (uint32_t)pop_pts), HEX(0x2A2A2A), a);
  }
  if (ready && RUN.x == 0) {
    gfx_text_center(&np_font_title, 160, 50, "Flappy Dunk", HEX(0x1A1A1A), 256);
    gfx_text_center(&np_font_body, 160, 70, label(s, T("Best "), sv.best), HEX(0x6A6A6A), 256);
  } else {
    num(s, RUN.score);
    gfx_text_center(&np_font_title, 160, 36, s, HEX(0x2A2A2A), 256);
  }
  if (ready && !ov) gfx_text_center(&np_font_title, 160, 196, T("Press OK to flap!"), HEX(0xF26B00), 256);
  gfx_text_right(&np_font_small, 314, 20, T("Back  menu"), HEX(0x8A8A8A), 256);
  if (ov) overlay();
}

/* ------------------------------------------------------------ the game */
static void load(void) {
  uint32_t len = 0;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  uint8_t *v = (uint8_t *)&sv;
  bool ok = d && len == sizeof sv && d[0] == 'F' && d[1] == 1;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = ok ? d[i] : 0;
  if (sv.sum != checksum()) memset_(v, sizeof sv);
  if (sv.skin >= NSKIN || !unlocked(sv.skin)) sv.skin = 0;
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
#ifdef __TICE__
  gfx_pure = true; /* (its own pixels make their strips drawn every time) */
#endif
  now = (uint32_t)eadk_timing_millis();
  load();
  if (sv.flags & 1) ready = true, best0 = sv.best, cam = RUN.x - BALL_SX / PX;
  else new_run();
  for (int i = 0; i < 8; i++) trail_x[i] = BALL_SX, trail_y[i] = SY(RUN.y);
  if (!(sv.flags & 2)) ov = OV_HELP, sv.flags |= 2;
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the launcher wait to be let go */
  uint32_t tick = now, trail_at = now;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint64_t raw = eadk_keyboard_scan(), k = raw & ~(stale &= raw), hit = k & ~held;
    held = k;
    if (raw & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe));
    int dx = !!(hit & KEY(eadk_key_right)) - !!(hit & KEY(eadk_key_left));
    int dy = !!(hit & KEY(eadk_key_down)) - !!(hit & KEY(eadk_key_up));
    if (over && !ov && now - over_t > 900) ov = OV_OVER, sel = 0;
    if (ov == OV_BALLS) {
      sel = (sel + dx + NSKIN) % NSKIN;
      if (dy) sel = (sel + 3) % NSKIN;
      if (ok && unlocked(sel)) sv.skin = (uint8_t)sel, save();
      if (hit & KEY(eadk_key_back)) ov = over ? OV_OVER : OV_MENU, sel = 1;
    } else if (ov) {
      int n = ov == OV_MENU ? 4 : ov == OV_OVER ? 3 : 1;
      sel = (sel + dy + n) % n;
      if (hit & KEY(eadk_key_back) && ov != OV_OVER) ov = OV_NONE;
      if (ok) {
        int choice = ov == OV_OVER ? (sel == 0 ? 4 : sel == 1 ? 1 : 3) : ov == OV_MENU ? sel : 0;
        ov = OV_NONE;
        if (choice == 4) new_run();
        else if (choice == 1) ov = OV_BALLS, sel = sv.skin;
        else if (choice == 2) ov = OV_HELP;
        else if (choice == 3) break;
      }
      tick = now;
    } else if (hit & KEY(eadk_key_back)) {
      ov = OV_MENU, sel = 0;
      if (!over) ready = true; /* back from the menu, the next flap carries on */
      save();
    } else if (!over && (ok || (hit & KEY(eadk_key_up)))) {
      flap();
    }
    /* the physics at the original's 50 steps a second, catching up after a slow frame */
    if (ov || ready || over) tick = now;
    if (now - tick > 100) tick = now - 100;
    while (now - tick >= 20) {
      tick += 20;
      if (over) break;
      step();
      float a = spin * DT; /* turn the ball */
      float c = spin_c - spin_s * a, s = spin_s + spin_c * a, m = 1.0f / sqrt_(c * c + s * s);
      spin_c = c * m, spin_s = s * m;
    }
    if (ready && !ov && RUN.x == 0) { /* hovering, waiting for the first flap */
      float t = (float)(now % 1200) / 600.0f - 1.0f;
      RUN.vy = 0, RUN.y = 0.35f * (t < 0 ? -t : t) - 0.17f;
    }
    if (over) RUN.vy -= G * 0.03f, RUN.y = RUN.y + RUN.vy * 0.03f > -TOP + R ? RUN.y + RUN.vy * 0.03f : -TOP + R;
    cam = RUN.x - BALL_SX / PX;
    if (now - trail_at >= 30) {
      trail_at = now;
      for (int i = 7; i > 0; i--) trail_x[i] = trail_x[i - 1] - SCROLL * 0.03f * PX, trail_y[i] = trail_y[i - 1];
      trail_x[0] = BALL_SX, trail_y[0] = SY(RUN.y);
    }
    eadk_display_wait_for_vblank();
    gfx_render(scene, 0, 0, SCREEN_H);
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 20) eadk_timing_msleep(20 - spent);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
