/* Frog Jump: lily pads across a river, the same for everyone, each in their own lane. Hold your key to gather
 * a jump (a ring shows where you'd land), let go to jump. Miss the pads and you swim back. The first frog
 * across takes the round. */
#include "fp.h"

#define NPAD 14
#define MAP 18      /* the map of the race, at the top */
#define FULL 700    /* ms to a full jump */
#define FLY 360
#define BANK 36.0f  /* the start's bank */
static float pad[NPAD], finish;
static struct {
  float x, from, to, cam;
  int st;                            /* on a pad or a bank, in the air, swimming back */
  int32_t charge_t, fly_t, splash_t; /* charge_t: -1 not gathering */
} f[NMAX];
enum { SIT, AIR, SWIM };

static int lane_h(void) { return (SCREEN_H - TOP - MAP) / np; }
static int lane_y(int p) {
  static const uint8_t ORDER[4][NMAX] = {{0, 1}, {0, 1, 2}, {0, 1, 2, 3}, {0, 4, 2, 1, 5, 3}}; /* the teams together */
  const uint8_t *o = ORDER[np == 2 ? 0 : np == 3 ? 1 : np == 4 ? 2 : 3];
  int i = 0;
  while (o[i] != p) i++;
  return TOP + MAP + i * lane_h();
}
static float jump_len(int32_t held) { return 16 + 114 * clampf(held / (float)FULL, 0, 1); }

static void start(void) {
  float x = BANK + rr(20, 40);
  for (int k = 0; k < NPAD; k++) pad[k] = x, x += rr(36, 78);
  finish = x - 10;
  for (int p = 0; p < np; p++) f[p].x = 22, f[p].cam = 0, f[p].st = SIT, f[p].charge_t = -1, f[p].fly_t = f[p].splash_t = -10000;
}
static bool flying(int p) { return f[p].st == AIR; }
static void step(void) {
  int first = -1;
  for (int p = 0; p < np; p++) {
    if (f[p].st == AIR && rt - f[p].fly_t >= FLY) { /* landing */
      float x = f[p].to;
      bool ok = x >= finish - 6;
      for (int k = 0; k < NPAD; k++) ok |= fabs_(x - pad[k]) <= 11;
      f[p].x = x, f[p].st = ok ? SIT : SWIM, f[p].splash_t = ok ? f[p].splash_t : rt;
      if (x >= finish - 6 && (first < 0 || x > f[first].x)) first = p;
    }
    if (f[p].st == SWIM && rt - f[p].splash_t >= 700) f[p].x = f[p].from, f[p].st = SIT; /* swum back */
    if (f[p].st == SIT) {
      if (tap[p]) f[p].charge_t = rt;
      if (rel[p] && f[p].charge_t >= 0)
        f[p].from = f[p].x, f[p].to = f[p].x + jump_len(rt - f[p].charge_t), f[p].fly_t = rt, f[p].st = AIR;
    }
    if (!hold[p] || f[p].st != SIT) f[p].charge_t = -1;
    float want = clampf(f[p].x - 64, 0, finish + 40 - SCREEN_W);
    f[p].cam += (want - f[p].cam) * 0.12f;
  }
  if (first >= 0) win(first);
}

static float frog_x(int p) {
  if (!flying(p)) return f[p].x;
  return f[p].from + (f[p].to - f[p].from) * (rt - f[p].fly_t) / FLY;
}
static void draw(void) {
  int h = lane_h();
  gfx_fill(0, TOP, SCREEN_W, MAP, HEX(0x1E3A5A)); /* the race's map */
  gfx_fill(16, TOP + 9, 288, 2, HEX(0x5E86AE));
  gfx_fill(300, TOP + 3, 3, 14, WHITE);
  for (int p = 0; p < np; p++) disc(16 + clampf(frog_x(p) / finish, 0, 1) * 284, TOP + 10, 4, col[p]);
  for (int p = 0; p < np; p++) {
    int y0 = lane_y(p);
    if (y0 + h <= gfx_y0 || y0 >= gfx_y1) continue;
    float cam = f[p].cam, mid = y0 + h / 2.0f, pr = h * 0.3f > 11 ? 11 : h * 0.3f;
    gfx_fill(0, y0, SCREEN_W, h, HEX(0x2F86C8));
    for (int k = 0; k < 9; k++) gfx_fill((int)(k * 40 - ((int)(cam + rt * 0.01f) % 40)), y0 + h / 3 + (k & 1) * h / 3, 14, 1, HEX(0x5AA6DE));
    gfx_fill(0, y0, SCREEN_W, 1, HEX(0x1E3A5A));
    if (BANK - cam > 0) gfx_fill(0, y0 + 1, (int)(BANK - cam), h - 1, HEX(0x5DAA4A));
    int fx = (int)(finish - cam);
    if (fx < SCREEN_W) {
      gfx_fill(fx, y0 + 1, SCREEN_W - fx, h - 1, HEX(0x5DAA4A));
      for (int k = 0; k < (h - 1) / 4; k++) gfx_fill(fx + 2 + (k & 1) * 4, y0 + 1 + k * 4, 4, 4, WHITE);
    }
    for (int k = 0; k < NPAD; k++) {
      float x = pad[k] - cam;
      if (x < -20 || x > SCREEN_W + 20) continue;
      disc(x, mid + 2, pr, HEX(0x2E7A3C));
      disc(x, mid, pr, HEX(0x58B04A));
      float n[6] = {x, mid, x + pr, mid - pr * 0.3f, x + pr, mid + pr * 0.3f};
      poly(n, 3, HEX(0x2F86C8));
    }
    float x = frog_x(p) - cam, y = mid - 3;
    if (flying(p)) {
      float t = (rt - f[p].fly_t) / (float)FLY;
      y -= 4 * h * 0.28f * t * (1 - t);
    }
    if (f[p].charge_t >= 0) { /* where it would land */
      float lx = f[p].x + jump_len(rt - f[p].charge_t) - cam;
      disc(lx, mid, 5, WHITE), disc(lx, mid, 3, HEX(0x2F86C8));
      line(x, mid, lx, mid, 1, lite[p]);
    }
    int32_t s = rt - f[p].splash_t;
    if (f[p].st == SWIM) {
      disc_a(x, mid, 4 + s / 40.0f, WHITE, 220 - s / 4);
      disc_a(x, mid, 4, col[p], 200 - s / 4);
    } else {
      float r = h > 40 ? 9 : 7;
      disc(x - r * 0.45f, y - r * 0.8f, r * 0.42f, col[p]), disc(x + r * 0.45f, y - r * 0.8f, r * 0.42f, col[p]);
      blob(x, y, r, p, 0);
      tag_at(p, x, y - r);
    }
  }
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x2F86C8));
  disc(x + 14, y + 22, 6, HEX(0x58B04A)), disc(x + 34, y + 20, 6, HEX(0x58B04A));
  disc(x + 24, y + 12, 5, col[0]);
  disc(x + 21, y + 8, 2, col[0]), disc(x + 27, y + 8, 2, col[0]);
  disc(x + 47, y + 17, 3, WHITE);
}

#ifdef HOST
static int bot(int p) {
  if (f[p].st != SIT) return 0;
  if (f[p].charge_t < 0) return 1;
  float target = finish + 4;
  for (int k = NPAD - 1; k >= 0; k--)
    if (pad[k] > f[p].x + 20 && pad[k] - f[p].x <= 128) target = pad[k];
  if (finish - f[p].x <= 126) target = finish + 2;
  float err = (p * 13 % 7 - 3) * 1.8f;
  return f[p].x + jump_len(rt - f[p].charge_t) < target + err;
}
#endif

const game_t g_frog = {"Frog Jump", "Frogs",
                       "Hold your key to gather a jump: the ring shows where you'll land. Let go to jump. Miss the "
                       "lily pads and you swim back. The first frog across takes the round.",
                       0, start, step, draw, icon BOT(bot)};
