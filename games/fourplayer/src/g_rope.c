/* Jump Rope: a bar sweeps round the platform. Tap to jump over it as it comes; caught on the ground, you're
 * thrown off. It speeds up, turns back now and then, and after a while a second bar joins it. The last one on
 * the platform takes the round. */
#include "fp.h"

#define CX 160.0f
#define CY 131.0f
#define PLAT 98.0f  /* the platform */
#define STAND 70.0f /* where the players stand */
#define AIR 520     /* ms in the air */
static float beam, w;
static int dir;
static int32_t flip_at, jump_t[NMAX], fall_t[NMAX];
static bool two;

static void start(void) {
  beam = seat(0) + PI + 0.3f, w = 0, dir = rn(2) ? 1 : -1, two = false;
  flip_at = 6000 + rn(5000);
  for (int p = 0; p < np; p++) jump_t[p] = fall_t[p] = -10000;
}
static float height(int p) {
  int32_t k = rt - jump_t[p];
  if (k >= AIR) return 0;
  float f = k / (float)AIR;
  return 4 * 22 * f * (1 - f);
}
static float speed(void) { return 1.7f + (rt < 40000 ? rt : 40000) * 0.000085f; }

static void step(void) {
  if (rt >= flip_at) dir = -dir, flip_at = rt + 5000 + rn(6000);
  if (rt > 22000) two = true;
  float want = dir * speed();
  w += clampf(want - w, -6 * DT, 6 * DT);
  float old = beam;
  beam = wrap_pi(beam + w * DT);
  for (int p = 0; p < np; p++) {
    if (gone[p]) continue;
    if (tap[p] && rt - jump_t[p] >= AIR + 120) jump_t[p] = rt;
    if (height(p) > 6) continue;
    float a = seat(p);
    for (int k = 0; k < 1 + two; k++) {
      float d0 = wrap_pi(a - old - k * PI), d1 = wrap_pi(a - beam - k * PI);
      if ((d0 < 0) != (d1 < 0) && fabs_(d0) < 1 && fabs_(d1) < 1) out(p), fall_t[p] = rt;
    }
  }
}

static void bar(float a) {
  float c = fcos(a), s = fsin(a);
  quad(CX + 3, CY + 5, a, 0, PLAT + 4, 4, HEX(0x8E6A38)); /* its shadow */
  quad(CX, CY, a, 0, PLAT + 4, 4.5f, HEX(0xE8433E));
  quad(CX, CY, a, 0, PLAT + 4, 1.6f, HEX(0xFF9A8A));
  disc(CX + c * (PLAT + 4), CY + s * (PLAT + 4), 6, HEX(0x3A2A4A));
}
static void draw(void) {
  for (int y = gfx_y0 < TOP ? TOP : gfx_y0; y < gfx_y1; y++) gfx_fill(0, y, SCREEN_W, 1, (y / 6) & 1 ? HEX(0x4FA65A) : HEX(0x4A9E55));
  disc(CX + 2, CY + 5, PLAT + 4, HEX(0x3F7F44));
  disc(CX, CY, PLAT + 4, HEX(0xB98446));
  disc(CX, CY, PLAT, HEX(0xDDAA66));
  disc(CX, CY, STAND + 12, HEX(0xD09A55));
  disc(CX, CY, STAND - 12, HEX(0xDDAA66));
  for (int p = 0; p < np; p++) /* the shadows, under everything */
    if (!gone[p]) {
      float a = seat(p), h = height(p);
      disc_a(CX + fcos(a) * STAND + 2, CY + fsin(a) * STAND + 4, 9 - h / 6, 0, 80);
    }
  for (int p = 0; p < np; p++) /* the ones on the ground go under the bar */
    if (!gone[p] && height(p) <= 6) {
      float a = seat(p);
      blob(CX + fcos(a) * STAND, CY + fsin(a) * STAND, 10, p, a + PI);
    }
  bar(beam);
  if (two) bar(beam + PI);
  disc(CX, CY, 9, HEX(0x3A2A4A));
  disc(CX - 2, CY - 2, 3, HEX(0x6A5A7A));
  for (int p = 0; p < np; p++) {
    float a = seat(p), x = CX + fcos(a) * STAND, y = CY + fsin(a) * STAND;
    if (!gone[p] && height(p) > 6) blob(x, y - height(p), 10, p, a + PI);
    if (!gone[p]) tag_at(p, x, y - 10);
    int32_t k = rt - fall_t[p];
    if (gone[p] && k < 800) { /* thrown off */
      float d = STAND + k * 0.16f, b = a + w * 0.1f * k / 800;
      blob(CX + fcos(b) * d, CY + fsin(b) * d - k * 0.03f, 10 * (1 - k / 1100.0f), p, a + k * 0.02f);
    }
  }
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x4FA65A));
  disc(x + 28, y + 17, 15, HEX(0xDDAA66));
  quad(x + 28, y + 17, -0.5f, 0, 16, 2.5f, HEX(0xE8433E));
  disc(x + 28, y + 17, 3, HEX(0x3A2A4A));
  disc(x + 16, y + 25, 4, col[0]), disc(x + 40, y + 10, 4, col[1]);
}

#ifdef HOST
static int bot(int p) {
  if (height(p) > 0 || fabs_(w) < 0.3f) return bot_tap(p, false);
  float a = seat(p), soon = 99;
  for (int k = 0; k < 1 + two; k++) {
    float d = wrap_pi(a - beam - k * PI), dist = w > 0 ? (d >= 0 ? d : d + 2 * PI) : (d <= 0 ? -d : 2 * PI - d);
    float t = dist / fabs_(w);
    soon = t < soon ? t : soon;
  }
  float when = 0.12f + (p * 37 % 10) * 0.012f + (rn(60) == 0 ? 0.3f : 0);
  return bot_tap(p, soon < when);
}
#endif

const game_t g_rope = {"Jump Rope", "Rope",
                       "A bar sweeps round the platform. Tap your key to jump over it. Caught on the ground, you're "
                       "thrown off. The last one standing takes the round.",
                       1, start, step, draw, icon BOT(bot)};
