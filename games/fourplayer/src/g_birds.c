/* Birds: every tap is a flap. Fly through the gaps between the columns: touch one, or the ground, and you're
 * out. The columns come faster and the gaps get smaller. The last bird flying takes the round. */
#include "fp.h"

#define GROUND 226
#define BR 7.0f
#define NCOL 4
#define CW 26 /* a column's width */
static float y[NMAX], vy[NMAX], scroll, speed;
static int32_t dead_t[NMAX], flap_t[NMAX];
static struct {
  float x, gap, mid;
} c[NCOL];

static float bx(int p) { return 60 + p * 13; }
static void column(int i, float x) {
  float gap = 78 - (rt > 0 ? rt : 0) * 0.0006f;
  if (gap < 58) gap = 58;
  c[i].x = x, c[i].gap = gap, c[i].mid = rr(TOP + 22 + gap / 2, GROUND - 16 - gap / 2);
}
static void start(void) {
  for (int p = 0; p < np; p++) y[p] = 66 + p * (np > 4 ? 24 : 34), vy[p] = -120, dead_t[p] = -1, flap_t[p] = -10000;
  for (int i = 0; i < NCOL; i++) column(i, 360 + i * 150);
  scroll = 0, speed = 80;
}
static void step(void) {
  speed = 80 + (rt < 40000 ? rt : 40000) * 0.001f;
  for (int i = 0; i < NCOL; i++) {
    c[i].x -= speed * DT;
    if (c[i].x < -CW) column(i, c[(i + NCOL - 1) % NCOL].x + 150);
  }
  scroll += speed * DT;
  for (int p = 0; p < np; p++) {
    if (dead_t[p] >= 0 && y[p] >= GROUND - BR) continue;
    if (tap[p] && dead_t[p] < 0) vy[p] = -235, flap_t[p] = rt;
    vy[p] += 760 * DT;
    if (vy[p] > 340) vy[p] = 340;
    y[p] += vy[p] * DT;
    if (y[p] < TOP + BR) y[p] = TOP + BR, vy[p] = 0;
    if (y[p] > GROUND - BR) y[p] = GROUND - BR;
    if (dead_t[p] >= 0) continue;
    bool hit = y[p] >= GROUND - BR;
    for (int i = 0; i < NCOL && !hit; i++) {
      float dx = bx(p) - clampf(bx(p), c[i].x, c[i].x + CW), top = c[i].mid - c[i].gap / 2, bot = c[i].mid + c[i].gap / 2;
      if (fabs_(dx) < BR - 1 && (y[p] - BR + 1.5f < top || y[p] + BR - 1.5f > bot)) hit = true;
      else if (fabs_(dx) < BR - 1) continue;
      else { /* the corners */
        float dt_ = y[p] - top, db = y[p] - bot;
        if ((dt_ < 0 && dx * dx + dt_ * dt_ < (BR - 1.5f) * (BR - 1.5f)) || (db > 0 && dx * dx + db * db < (BR - 1.5f) * (BR - 1.5f))) hit = true;
      }
    }
    if (hit) dead_t[p] = rt, out(p), vy[p] = vy[p] < 0 ? 0 : vy[p];
  }
}

static void bird(int p, float x, float yy) {
  float tilt = clampf(vy[p] * 0.004f, -0.5f, 1.2f);
  int32_t k = rt - flap_t[p];
  float beak[6] = {x + fcos(tilt) * 6, yy + fsin(tilt) * 6 - 1, x + fcos(tilt) * 12, yy + fsin(tilt) * 12 + 1, x + fcos(tilt + 0.5f) * 6, yy + fsin(tilt + 0.5f) * 6 + 2};
  disc_a(x + 1, yy + 2, BR, 0, 60);
  disc(x, yy, BR, col[p]);
  poly(beak, 3, HEX(0xFF9F1C));
  eyes(x + 1, yy - 1, 6, tilt);
  quad(x - 3, yy + 2, k < 120 ? -2.2f : 2.6f, 0, 6, 2.2f, dark[p]); /* the wing */
}
static void draw(void) {
  gfx_vgrad(0, TOP, SCREEN_W, GROUND - TOP, 0x5BC0EB, 0xD7F2FC);
  for (int i = 0; i < 5; i++) { /* far hills */
    float x = i * 96 - ((int)(scroll * 0.25f) % 96) + 30;
    disc(x, GROUND + 20, 56, HEX(0xA8D9A0));
  }
  for (int i = 0; i < NCOL; i++) {
    int x = (int)c[i].x, top = (int)(c[i].mid - c[i].gap / 2), bot = (int)(c[i].mid + c[i].gap / 2);
    color_t body = HEX(0x8E9BB8), edge = HEX(0x5F6A88), lit = HEX(0xB4BED6);
    gfx_fill(x, TOP, CW, top - TOP, body), gfx_fill(x, bot, CW, GROUND - bot, body);
    gfx_fill(x + 4, TOP, 4, top - TOP, lit), gfx_fill(x + 4, bot, 4, GROUND - bot, lit);
    gfx_fill(x + CW - 5, TOP, 5, top - TOP, edge), gfx_fill(x + CW - 5, bot, 5, GROUND - bot, edge);
    box(x - 3, top - 10, CW + 6, 10, 2, edge), box(x - 3, bot, CW + 6, 10, 2, edge);
  }
  gfx_fill(0, GROUND, SCREEN_W, SCREEN_H - GROUND, HEX(0xD8C27A));
  for (int k = 0; k < 22; k++) gfx_fill(k * 16 - ((int)scroll % 16), GROUND, 8, 3, HEX(0x8CC152));
  gfx_fill(0, GROUND + 3, SCREEN_W, 2, HEX(0x6EA03E));
  for (int p = np - 1; p >= 0; p--) {
    bird(p, bx(p), y[p]);
    if (dead_t[p] < 0) tag_at(p, bx(p), y[p] - BR);
  }
}

static void icon(int x0, int y0) {
  box(x0 + 2, y0 + 1, 52, 32, 7, HEX(0x5BC0EB));
  gfx_fill(x0 + 36, y0 + 1, 10, 9, HEX(0x8E9BB8)), gfx_fill(x0 + 36, y0 + 24, 10, 9, HEX(0x8E9BB8));
  disc(x0 + 16, y0 + 13, 5, col[0]), disc(x0 + 24, y0 + 21, 5, col[1]);
  disc(x0 + 21, y0 + 13, 1.5f, HEX(0xFF9F1C)), disc(x0 + 29, y0 + 21, 1.5f, HEX(0xFF9F1C));
}

#ifdef HOST
static int bot(int p) {
  float target = 131, best = 1e9f; /* the middle of the nearest gap ahead */
  for (int i = 0; i < NCOL; i++)
    if (c[i].x + CW > bx(p) - BR && c[i].x < best) best = c[i].x, target = c[i].mid + 6;
  float noise = (p * 7 % 5) * 1.5f;
  return bot_tap(p, y[p] > target + noise && vy[p] > -40);
}
#endif

const game_t g_birds = {"Birds", "Birds", "Tap  flap",
                        "Every tap of your key is a flap. Fly through the gaps: touch a column or the ground and "
                        "you're out. The last bird flying takes the round.",
                        1, start, step, draw, icon BOT(bot)};
