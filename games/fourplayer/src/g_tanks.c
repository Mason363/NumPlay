/* Tanks: each tank turns on the spot. Holding the key drives it straight; letting go fires. Shells bounce off
 * the walls three times, and one hit is enough. The last tank left takes the round. In teams, shells pass
 * through your own team. */
#include "fp.h"

#define TR 8.0f     /* a tank */
#define TURN 2.5f   /* radians a second, clockwise */
#define DRIVE 62.0f /* px/s */
#define SHOT 150.0f
#define NSH 18
#define WALL 5
typedef struct {
  int16_t x, y, w, h;
} rect_t;
static const rect_t LAYOUT[3][7] = {
  {{152, 86, 16, 90}, {115, 123, 90, 16}, {70, 72, 26, 14}, {224, 72, 26, 14}, {70, 190, 26, 14}, {224, 190, 26, 14}},
  {{100, 62, 14, 48}, {206, 62, 14, 48}, {100, 154, 14, 48}, {206, 154, 14, 48}, {145, 116, 30, 30}},
  {{80, 70, 18, 18}, {151, 70, 18, 18}, {222, 70, 18, 18}, {80, 174, 18, 18}, {151, 174, 18, 18}, {222, 174, 18, 18},
   {120, 122, 80, 18}},
};
static const rect_t *map;
static int nmap;
static struct {
  body_t b;
  float a;
  int32_t shot_t, dead_t;
} t[NMAX];
static struct {
  float x, y, vx, vy;
  int8_t own, hits;
  int32_t born;
} sh[NSH];

static void start(void) {
  int k = rn(3);
  map = LAYOUT[k], nmap = 0;
  while (nmap < 7 && map[nmap].w) nmap++;
  for (int p = 0; p < np; p++) {
    float s = seat(p);
    t[p].b = (body_t){160 + fcos(s) * 122, 131 + fsin(s) * 82, 0, 0};
    if (np == 3 && p == 2) t[p].b.y = 214;
    t[p].a = fatan2(131 - t[p].b.y, 160 - t[p].b.x), t[p].shot_t = -10000, t[p].dead_t = -1;
  }
  for (int i = 0; i < NSH; i++) sh[i].own = -1;
}

static bool solid(float x, float y) { /* a wall at (x, y) */
  if (x < WALL || x >= SCREEN_W - WALL || y < TOP + WALL || y >= SCREEN_H - WALL) return true;
  for (int i = 0; i < nmap; i++)
    if (x >= map[i].x && x < map[i].x + map[i].w && y >= map[i].y && y < map[i].y + map[i].h) return true;
  return false;
}
/* a tank out of the walls */
static void push_out(body_t *b) {
  b->x = clampf(b->x, WALL + TR, SCREEN_W - WALL - TR), b->y = clampf(b->y, TOP + WALL + TR, SCREEN_H - WALL - TR);
  for (int i = 0; i < nmap; i++) {
    float cx = clampf(b->x, map[i].x, map[i].x + map[i].w), cy = clampf(b->y, map[i].y, map[i].y + map[i].h);
    float dx = b->x - cx, dy = b->y - cy, d2 = dx * dx + dy * dy;
    if (d2 >= TR * TR) continue;
    if (d2 < 0.0001f) { /* (its middle inside: out the nearest side) */
      float l = b->x - map[i].x, r = map[i].x + map[i].w - b->x, u = b->y - map[i].y, d = map[i].y + map[i].h - b->y;
      float m = l < r ? l : r, n = u < d ? u : d;
      if (m < n) b->x = l < r ? map[i].x - TR : map[i].x + map[i].w + TR;
      else b->y = u < d ? map[i].y - TR : map[i].y + map[i].h + TR;
      continue;
    }
    float d = fsqrt(d2);
    b->x = cx + dx / d * TR, b->y = cy + dy / d * TR;
  }
}

static void fire(int p) {
  int live = 0, free_ = -1;
  for (int i = 0; i < NSH; i++)
    if (sh[i].own == p) live++;
    else if (sh[i].own < 0 && free_ < 0) free_ = i;
  bool late = rt > 30000; /* (after 30 seconds: shells come faster) */
  if (live >= (late ? 4 : 3) || free_ < 0 || rt - t[p].shot_t < (late ? 220 : 400)) return;
  float c = fcos(t[p].a), s = fsin(t[p].a), x = t[p].b.x + c * 12, y = t[p].b.y + s * 12;
  if (solid(x, y)) return; /* (the barrel against a wall) */
  sh[free_].x = x, sh[free_].y = y, sh[free_].vx = c * SHOT, sh[free_].vy = s * SHOT;
  sh[free_].own = (int8_t)p, sh[free_].hits = 0, sh[free_].born = rt;
  t[p].shot_t = rt;
}

static void step(void) {
  for (int p = 0; p < np; p++) {
    if (gone[p]) continue;
    if (hold[p]) t[p].b.x += fcos(t[p].a) * DRIVE * DT, t[p].b.y += fsin(t[p].a) * DRIVE * DT;
    else t[p].a = wrap_pi(t[p].a + TURN * DT);
    if (rel[p]) fire(p);
  }
  for (int p = 0; p < np; p++)
    for (int q = p + 1; q < np; q++)
      if (!gone[p] && !gone[q]) {
        t[p].b.vx = t[p].b.vy = t[q].b.vx = t[q].b.vy = 0;
        collide(&t[p].b, &t[q].b, 2 * TR, 1, 1, 0);
      }
  for (int p = 0; p < np; p++)
    if (!gone[p]) push_out(&t[p].b);
  for (int i = 0; i < NSH; i++) {
    if (sh[i].own < 0) continue;
    float ox = sh[i].x, oy = sh[i].y;
    sh[i].x += sh[i].vx * DT, sh[i].y += sh[i].vy * DT;
    if (solid(sh[i].x, sh[i].y)) { /* a bounce: off the side it came through */
      bool fx = !solid(ox, sh[i].y), fy = !solid(sh[i].x, oy);
      if (fx || !fy) sh[i].vx = -sh[i].vx;
      if (fy || !fx) sh[i].vy = -sh[i].vy;
      sh[i].x = ox, sh[i].y = oy;
      if (++sh[i].hits > 3) sh[i].own = -1;
    }
    if (rt - sh[i].born > 6000) sh[i].own = -1;
    for (int p = 0; p < np && sh[i].own >= 0; p++) {
      if (gone[p] || (teams && side(p) == side(sh[i].own)) || (p == sh[i].own && rt - sh[i].born < 200)) continue;
      float dx = sh[i].x - t[p].b.x, dy = sh[i].y - t[p].b.y;
      if (dx * dx + dy * dy < (TR + 1.5f) * (TR + 1.5f)) out(p), t[p].dead_t = rt, sh[i].own = -1;
    }
  }
  if (rt > 75000) tie();
}

static void tank(int p) {
  float x = t[p].b.x, y = t[p].b.y, a = t[p].a, c = fcos(a), s = fsin(a);
  quad(x + 1.5f, y + 2.5f, a, -9, 9, 8, HEX(0xB7AE95)); /* shadow */
  for (int k = -1; k <= 1; k += 2) quad(x - s * k * 6, y + c * k * 6, a, -9, 9, 2.6f, HEX(0x26232E));
  quad(x, y, a, -7.5f, 7.5f, 5, col[p]);
  quad(x, y, a, 2, 13, 1.7f, dark[p]);
  disc(x, y, 4, dark[p]);
  disc(x - 1, y - 1, 2, lite[p]);
}
static void draw(void) {
  for (int y = gfx_y0 < TOP ? TOP : gfx_y0; y < gfx_y1; y++) gfx_fill(0, y, SCREEN_W, 1, (y - TOP) % 20 ? HEX(0xDCD5C1) : HEX(0xCFC7B1));
  for (int x = 20; x < SCREEN_W; x += 20) gfx_fill(x, TOP, 1, SCREEN_H - TOP, HEX(0xCFC7B1));
  color_t wc = HEX(0x3B3F4E), hc = HEX(0x5A5F72);
  gfx_fill(0, TOP, SCREEN_W, WALL, wc), gfx_fill(0, SCREEN_H - WALL, SCREEN_W, WALL, wc);
  gfx_fill(0, TOP, WALL, SCREEN_H - TOP, wc), gfx_fill(SCREEN_W - WALL, TOP, WALL, SCREEN_H - TOP, wc);
  for (int i = 0; i < nmap; i++) {
    gfx_fill(map[i].x + 2, map[i].y + 3, map[i].w, map[i].h, HEX(0xB7AE95));
    gfx_fill(map[i].x, map[i].y, map[i].w, map[i].h, wc);
    gfx_fill(map[i].x, map[i].y, map[i].w, 2, hc);
  }
  for (int p = 0; p < np; p++) {
    if (t[p].dead_t < 0) continue;
    int32_t k = rt - t[p].dead_t;
    disc_a(t[p].b.x, t[p].b.y, 11, HEX(0x4A4438), 160);
    if (k < 600) disc_a(t[p].b.x, t[p].b.y, 6 + k / 30.0f, HEX(0xFF8A2A), 256 - k * 256 / 600),
        disc_a(t[p].b.x, t[p].b.y, 3 + k / 50.0f, HEX(0xFFE066), 256 - k * 256 / 600);
  }
  for (int p = 0; p < np; p++)
    if (t[p].dead_t < 0) tank(p), tag_at(p, t[p].b.x, t[p].b.y - 9);
  for (int i = 0; i < NSH; i++)
    if (sh[i].own >= 0) disc(sh[i].x, sh[i].y, 2.5f, HEX(0x1E1F24)), disc_a(sh[i].x - 1, sh[i].y - 1, 1, WHITE, 160);
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0xDCD5C1));
  gfx_fill(x + 25, y + 6, 6, 22, HEX(0x3B3F4E));
  for (int i = 0; i < 2; i++) {
    int cx = x + 13 + i * 30, cy = y + 17;
    gfx_fill(cx - 7, cy - 6, 14, 3, HEX(0x26232E)), gfx_fill(cx - 7, cy + 3, 14, 3, HEX(0x26232E));
    box(cx - 6, cy - 4, 12, 8, 2, col[i]);
    gfx_fill(i ? cx - 11 : cx, cy - 1, 11, 3, dark[i]);
  }
}

#ifdef HOST
static bool clear_shot(float x, float y, float tx, float ty) {
  for (int i = 1; i < 24; i++)
    if (solid(x + (tx - x) * i / 24, y + (ty - y) * i / 24)) return false;
  return true;
}
static int bot(int p) {
  static int8_t driving[NMAX];
  static int32_t until[NMAX], seen[NMAX];
  if (bot_fresh(p)) driving[p] = 0, seen[p] = rt;
  float x = t[p].b.x, y = t[p].b.y, best = 1e9f, want = 0, bx = 0, by = 0;
  for (int q = 0; q < np; q++) {
    float d = (t[q].b.x - x) * (t[q].b.x - x) + (t[q].b.y - y) * (t[q].b.y - y);
    if (q != p && !gone[q] && side(q) != side(p) && d < best) best = d, bx = t[q].b.x, by = t[q].b.y;
  }
  if (best > 1e8f) return 0;
  want = fatan2(by - y, bx - x);
  float off = fabs_(wrap_pi(t[p].a - want)), ax = x + fcos(t[p].a) * 18, ay = y + fsin(t[p].a) * 18;
  bool clear = clear_shot(x, y, bx, by);
  if (clear) seen[p] = rt;
  if (driving[p]) { /* on the move: until it's time, or a wall ahead */
    if (rt > until[p] || solid(ax, ay)) driving[p] = 0;
    return driving[p];
  }
  if (off < 0.14f && clear && rt - t[p].shot_t > 400) return bot_tap(p, true);
  bool lost = rt - seen[p] > 1500 && !solid(ax, ay) && rn(30) == 0; /* no shot for a while: somewhere else */
  if ((off < 0.2f && best > 140 * 140 && rn(4) == 0) || lost) driving[p] = 1, until[p] = rt + 500 + rn(800);
  return driving[p];
}
#endif

const game_t g_tanks = {"Tanks", "Tanks",
                        "Your tank turns on the spot. Hold your key to drive, let go to fire. Shells bounce off the "
                        "walls. One hit and you're out: the last tank left takes the round.",
                        1, start, step, draw, icon BOT(bot)};
