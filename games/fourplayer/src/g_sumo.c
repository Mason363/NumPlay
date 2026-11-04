/* Sumo: wrestlers on a ring of ice in the sea. Each one's arrow spins; a tap dashes where it points and turns
 * the spin the other way. Off the ice is out, and the last one on it takes the round. After 20 seconds the
 * ice melts, a little more every second. */
#include "fp.h"

#define CX 160.0f
#define CY 131.0f
#define RING 100.0f /* the ice */
#define R 11.0f     /* a wrestler */
#define SPIN 3.4f   /* the arrow, radians a second */
#define DASH 215.0f /* px/s a dash adds */
#define FAST 330.0f
#define COOL 300    /* ms between dashes */
static struct {
  body_t b;
  float a, spin;
  int32_t dash_t, fall_t;
} w[NMAX];
static float ring;

static void start(void) {
  ring = RING;
  for (int p = 0; p < np; p++) {
    float s = seat(p);
    w[p].b = (body_t){CX + fcos(s) * 62, CY + fsin(s) * 62, 0, 0};
    w[p].a = s + PI, w[p].spin = 1, w[p].dash_t = -10000, w[p].fall_t = -1;
  }
}

static void step(void) {
  if (rt > 20000 && ring > 0) ring -= 3.0f * DT; /* the ice melts */
  for (int p = 0; p < np; p++) {
    body_t *b = &w[p].b;
    if (w[p].fall_t >= 0) { /* sinking */
      b->x += b->vx * DT * 0.4f, b->y += b->vy * DT * 0.4f;
      continue;
    }
    w[p].a = wrap_pi(w[p].a + w[p].spin * SPIN * DT);
    if (tap[p] && rt - w[p].dash_t >= COOL) {
      b->vx += fcos(w[p].a) * DASH, b->vy += fsin(w[p].a) * DASH;
      float v = fsqrt(b->vx * b->vx + b->vy * b->vy);
      if (v > FAST) b->vx *= FAST / v, b->vy *= FAST / v;
      w[p].spin = -w[p].spin, w[p].dash_t = rt;
    }
    b->vx *= 0.988f, b->vy *= 0.988f; /* ice: barely slows */
    b->x += b->vx * DT, b->y += b->vy * DT;
  }
  for (int p = 0; p < np; p++)
    for (int q = p + 1; q < np; q++)
      if (w[p].fall_t < 0 && w[q].fall_t < 0) collide(&w[p].b, &w[q].b, 2 * R, 1, 1, 0.95f);
  for (int p = 0; p < np; p++) {
    float dx = w[p].b.x - CX, dy = w[p].b.y - CY;
    if (w[p].fall_t < 0 && dx * dx + dy * dy > ring * ring) w[p].fall_t = rt, out(p);
  }
}

static void sea(void) {
  for (int y = gfx_y0 < TOP ? TOP : gfx_y0; y < gfx_y1; y++) {
    int k = ((y + rt / 90) / 7) & 3;
    gfx_fill(0, y, SCREEN_W, 1, k == 0 ? HEX(0x1F5FA0) : HEX(0x1B5291));
  }
}
static void wrestler(int p, float x, float y, float r) {
  blob(x, y, r, p, w[p].a);
  disc(x, y - r * 0.92f, r * 0.3f, HEX(0x221B33)); /* the topknot */
}
static void draw(void) {
  sea();
  if (ring > 1) {
    disc(CX, CY + 3, ring + 4, HEX(0x174780)); /* the ice's side, under water */
    disc(CX, CY, ring + 3, HEX(0x9FD3EE));
    disc(CX, CY, ring, HEX(0xE4F5FF));
    if (ring > 30) {
      disc(CX, CY, ring - 14, HEX(0xC4E4F6));
      disc(CX, CY, ring - 16, HEX(0xE4F5FF));
    }
    gfx_fill(CX - 18, CY - 8, 12, 3, HEX(0xC4E4F6));
    gfx_fill(CX + 6, CY + 5, 12, 3, HEX(0xC4E4F6));
  }
  for (int p = 0; p < np; p++) {
    float x = w[p].b.x, y = w[p].b.y;
    if (w[p].fall_t >= 0) {
      int32_t k = rt - w[p].fall_t;
      if (k < 700) {
        disc_a(x, y, R + k / 25, WHITE, 180 - k / 4);
        wrestler(p, x, y, R * (1 - k / 800.0f));
      }
      continue;
    }
    if (rt - w[p].dash_t < 160) disc_a(x - w[p].b.vx * 0.05f, y - w[p].b.vy * 0.05f, R * 0.8f, WHITE, 110);
    float a = w[p].a, c = fcos(a), s = fsin(a), d = R + 4;
    float t[6] = {x + c * (d + 8), y + s * (d + 8), x + c * d - s * 5, y + s * d + c * 5, x + c * d + s * 5, y + s * d - c * 5};
    poly(t, 3, dark[p]);
    wrestler(p, x, y, R);
    tag_at(p, x, y - R);
  }
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x1F5FA0));
  disc(x + 28, y + 17, 15, HEX(0x9FD3EE));
  disc(x + 28, y + 17, 13, HEX(0xE4F5FF));
  disc(x + 21, y + 17, 6, col[0]);
  disc(x + 35, y + 17, 6, col[1]);
  disc(x + 21, y + 11, 2, HEX(0x221B33));
  disc(x + 35, y + 11, 2, HEX(0x221B33));
}

#ifdef HOST
static int bot(int p) {
  float x = w[p].b.x, y = w[p].b.y, want = 99, best = 1e9f;
  if ((x - CX) * (x - CX) + (y - CY) * (y - CY) > (ring - 30) * (ring - 30)) want = fatan2(CY - y, CX - x);
  else
    for (int q = 0; q < np; q++) {
      float d = (w[q].b.x - x) * (w[q].b.x - x) + (w[q].b.y - y) * (w[q].b.y - y);
      if (q != p && !gone[q] && side(q) != side(p) && d < best) best = d, want = fatan2(w[q].b.y - y, w[q].b.x - x);
    }
  bool aim = want < 50 && fabs_(wrap_pi(w[p].a - want)) < 0.3f && rt - w[p].dash_t >= COOL && rn(3) == 0;
  return bot_tap(p, aim);
}
#endif

const game_t g_sumo = {"Sumo", "Sumo",
                       "Your arrow spins. Tap your key to dash where it points: the spin turns the other way. Push the "
                       "others into the sea. The last one on the ice takes the round.",
                       1, start, step, draw, icon BOT(bot)};
