/* Gravity Run: everyone runs on their own, on the floor or on the ceiling. A tap flips your gravity (from the
 * floor or the ceiling, not in the air). Fall through a hole and you're out: the last runner left takes the
 * round. It gets faster and faster. */
#include "fp.h"

#define CEIL (TOP + 22) /* the ceiling's underside */
#define FLOOR 216       /* the floor's top */
#define R 7.0f
#define NGAP 16
static struct {
  float x0, x1;
  uint8_t up; /* in the ceiling */
} gap[NGAP];
static int ngap;
static float world, speed, next_x; /* world: the screen's left edge, in the run's pixels */
static struct {
  float y, vy;
  int g; /* 1 down, -1 up */
  bool on;
  int32_t dead_t;
} r[NMAX];

static float rx(int p) { return 50 + p * 24; }
static void add_gap(void) {
  float later = (rt < 60000 ? (rt > 0 ? rt : 0) : 60000) / 60000.0f; /* (holes get wider, and come sooner) */
  float w = rr(30, 56) + later * 40;
  int k = ngap < NGAP ? ngap++ : 0;
  if (ngap == NGAP) /* (the oldest one goes: it's long gone) */
    for (int i = 1; i < NGAP; i++)
      if (gap[i].x1 < gap[k].x1) k = i;
  gap[k].x0 = next_x, gap[k].x1 = next_x + w, gap[k].up = (uint8_t)rn(2);
  next_x += w + speed * (0.9f - later * 0.2f) + rr(0, 120 - later * 60);
}
static bool solid(float x, int up) {
  for (int i = 0; i < ngap; i++)
    if (gap[i].up == up && x > gap[i].x0 && x < gap[i].x1) return false;
  return true;
}
static void start(void) {
  world = 0, speed = 105, ngap = 0, next_x = 420;
  for (int p = 0; p < np; p++) r[p].g = 1, r[p].y = FLOOR - R, r[p].vy = 0, r[p].on = true, r[p].dead_t = -1;
  for (int p = 1; p < np; p += 2) r[p].g = -1, r[p].y = CEIL + R; /* (half of them start on the ceiling) */
  while (next_x < world + 700) add_gap();
}
static void step(void) {
  speed = 105 + (rt < 45000 ? rt : 45000) * 0.002f;
  world += speed * DT;
  while (next_x < world + 700) add_gap();
  for (int p = 0; p < np; p++) {
    if (gone[p] && r[p].dead_t >= 0 && rt - r[p].dead_t > 1500) continue;
    if (tap[p] && r[p].on) r[p].g = -r[p].g, r[p].on = false, r[p].vy = r[p].g * 60.0f;
    float x = world + rx(p);
    bool under = r[p].g > 0 ? solid(x - R * 0.6f, 0) || solid(x + R * 0.6f, 0) : solid(x - R * 0.6f, 1) || solid(x + R * 0.6f, 1);
    if (r[p].on && !under) r[p].on = false; /* the ground ends */
    if (!r[p].on) {
      r[p].vy = clampf(r[p].vy + 900 * r[p].g * DT, -420, 420);
      float ny = r[p].y + r[p].vy * DT;
      if (r[p].g > 0 && ny + R >= FLOOR && r[p].y + R <= FLOOR + 6 && under) ny = FLOOR - R, r[p].on = true, r[p].vy = 0;
      if (r[p].g < 0 && ny - R <= CEIL && r[p].y - R >= CEIL - 6 && under) ny = CEIL + R, r[p].on = true, r[p].vy = 0;
      r[p].y = ny;
    }
    if (!gone[p] && (r[p].y > SCREEN_H + R || r[p].y < TOP - R)) out(p), r[p].dead_t = rt;
  }
}

static void band(int y0, int y1, int up) {
  int h = y1 - y0;
  if (y1 <= gfx_y0 || y0 >= gfx_y1) return;
  for (int x = 0; x < SCREEN_W;) { /* the solid stretches, then the holes */
    float wx = world + x;
    bool s = solid(wx + 0.5f, up);
    int e = x + 1;
    while (e < SCREEN_W && solid(world + e + 0.5f, up) == s) e++;
    if (s) {
      gfx_fill(x, y0, e - x, h, HEX(0x4F5BC4));
      gfx_fill(x, up ? y1 - 3 : y0, e - x, 3, HEX(0x8D9BFF));
      for (int k = (int)((x + world) / 16) * 16 + 16; k < world + e; k += 16) gfx_fill((int)(k - world), y0 + (up ? 0 : 3), 1, h - 3, HEX(0x3E489E));
    }
    x = e;
  }
}
static void draw(void) {
  gfx_vgrad(0, TOP, SCREEN_W, SCREEN_H - TOP, 0x1A1E3A, 0x2E2A5C);
  for (int i = 0; i < 40; i++) { /* stars, slower than the run */
    int sx = (int)((i * 97 + 13) % 340 - (int)(world * 0.2f) % 340), sy = TOP + 26 + (i * 53) % 168;
    if (sx < 0) sx += 340;
    gfx_fill(sx, sy, 1 + (i % 3 == 0), 1 + (i % 3 == 0), i & 1 ? HEX(0x8A86C8) : HEX(0xD7D4FF));
  }
  band(TOP, CEIL, 1);
  band(FLOOR, SCREEN_H, 0);
  for (int p = 0; p < np; p++) {
    if (r[p].dead_t >= 0 && rt - r[p].dead_t > 1500) continue;
    float x = rx(p), y = r[p].y, f = r[p].g;
    if (r[p].on) { /* running feet */
      int k = (int)(rt / 90 + p) & 1;
      disc(x - 3 + k * 2, y + f * (R - 1), 2.5f, dark[p]);
      disc(x + 3 - k * 2, y + f * (R - 1), 2.5f, dark[p]);
    }
    blob(x, y, R, p, f > 0 ? 0 : 0.6f);
    float a[6] = {x - 3, y - f * (R + 3), x + 3, y - f * (R + 3), x, y - f * (R + 7)}; /* which way is down */
    if (!r[p].on) poly(a, 3, lite[p]);
    tag_at(p, x, f > 0 ? y - R : y + R + 26);
  }
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x1A1E3A));
  gfx_fill(x + 2, y + 1, 30, 6, HEX(0x4F5BC4)), gfx_fill(x + 40, y + 1, 14, 6, HEX(0x4F5BC4));
  gfx_fill(x + 2, y + 27, 18, 6, HEX(0x4F5BC4)), gfx_fill(x + 30, y + 27, 24, 6, HEX(0x4F5BC4));
  disc(x + 16, y + 22, 4, col[0]), disc(x + 26, y + 12, 4, col[1]);
}

#ifdef HOST
static int bot(int p) {
  static float skip[NMAX]; /* a hole it didn't see coming, until it's past */
  if (bot_fresh(p)) skip[p] = 0;
  float x = world + rx(p), d = speed * (0.12f + (p * 3 % 5) * 0.02f);
  if (!r[p].on || x < skip[p]) return bot_tap(p, false);
  int here = r[p].g < 0;
  bool hole = !solid(x + d, here), other = solid(x + speed * 0.7f, !here) && solid(x + speed * 0.5f, !here);
  if (hole && rn(100) < 3 + rt / 8000) {
    for (int i = 0; i < ngap; i++)
      if (gap[i].up == here && x + d > gap[i].x0 && x + d < gap[i].x1) skip[p] = gap[i].x1;
    return bot_tap(p, false);
  }
  return bot_tap(p, hole && (other || rn(3) == 0));
}
#endif

const game_t g_gravity = {"Gravity Run", "Gravity", "Tap  flip",
                          "You run on your own. Tap your key to flip your gravity, from the floor to the ceiling and "
                          "back. Fall through a hole and you're out: the last runner left takes the round.",
                          1, start, step, draw, icon BOT(bot)};
