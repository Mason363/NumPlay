/* Paint: rollers that never stop, always curving. A tap makes yours curve the other way. Paint the floor your
 * colour, over the others' too: after 30 seconds, the most floor takes the round. Teams paint together. */
#include "fp.h"

#define CS 4 /* a cell of floor */
#define GW 80
#define GH 54
#define Y0 (TOP + 1)
#define SPEED 72.0f
#define CURVE 2.1f
#define BRUSH 7.0f
#define TIME 30000
static uint8_t floor_[GH][GW]; /* 0 bare, else the side + 1 */
static int count[NMAX];
static color_t paint[NMAX];
static struct {
  float x, y, a;
  int sgn;
} r[NMAX];

static void start(void) {
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) floor_[y][x] = 0;
  for (int s = 0; s < NMAX; s++) count[s] = 0, paint[s] = gfx_mix(col[s], WHITE, 9);
  for (int p = 0; p < np; p++) {
    float s = seat(p);
    r[p].x = 160 + fcos(s) * 110, r[p].y = 131 + fsin(s) * 70, r[p].a = s + PI + 0.6f, r[p].sgn = p & 1 ? 1 : -1;
  }
}
static void roll(int p) { /* paints under p's roller */
  int s = side(p) + 1, cx = (int)(r[p].x / CS), cy = (int)((r[p].y - Y0) / CS);
  for (int y = cy - 2; y <= cy + 2; y++)
    for (int x = cx - 2; x <= cx + 2; x++) {
      if (x < 0 || y < 0 || x >= GW || y >= GH || floor_[y][x] == s) continue;
      float dx = x * CS + 2 - r[p].x, dy = Y0 + y * CS + 2 - r[p].y;
      if (dx * dx + dy * dy > BRUSH * BRUSH) continue;
      if (floor_[y][x]) count[floor_[y][x] - 1]--;
      floor_[y][x] = (uint8_t)s, count[s - 1]++;
    }
}
static void step(void) {
  for (int p = 0; p < np; p++) {
    if (tap[p]) r[p].sgn = -r[p].sgn;
    r[p].a = wrap_pi(r[p].a + r[p].sgn * CURVE * DT);
    r[p].x += fcos(r[p].a) * SPEED * DT, r[p].y += fsin(r[p].a) * SPEED * DT;
    if (r[p].x < 6 || r[p].x > SCREEN_W - 6) r[p].a = wrap_pi(PI - r[p].a), r[p].x = clampf(r[p].x, 6, SCREEN_W - 6);
    if (r[p].y < TOP + 6 || r[p].y > SCREEN_H - 6) r[p].a = -r[p].a, r[p].y = clampf(r[p].y, TOP + 6, SCREEN_H - 6);
  }
  for (int p = 0; p < np; p++)
    for (int q = p + 1; q < np; q++) {
      body_t a = {r[p].x, r[p].y, fcos(r[p].a) * SPEED, fsin(r[p].a) * SPEED};
      body_t b = {r[q].x, r[q].y, fcos(r[q].a) * SPEED, fsin(r[q].a) * SPEED};
      if (!collide(&a, &b, 14, 1, 1, 1)) continue;
      r[p].x = a.x, r[p].y = a.y, r[p].a = fatan2(a.vy, a.vx);
      r[q].x = b.x, r[q].y = b.y, r[q].a = fatan2(b.vy, b.vx);
    }
  if (!over())
    for (int p = 0; p < np; p++) roll(p);
  if (rt >= TIME && !over()) {
    int best = -1, n = 0, who = 0; /* (side s is player s's: in teams, players 0 and 1 lead them) */
    for (int s = 0; s < (teams ? 2 : np); s++)
      if (count[s] > best) best = count[s], n = 1, who = s;
      else if (count[s] == best) n++;
    if (n == 1) win(who);
    else tie();
  }
}

static void draw(void) {
  int r0 = clampi((gfx_y0 - Y0) / CS, 0, GH), r1 = clampi((gfx_y1 - Y0 + CS - 1) / CS, 0, GH);
  color_t bare = HEX(0xF1ECE1);
  gfx_fill(0, TOP, SCREEN_W, SCREEN_H - TOP, HEX(0xD9D1C0));
  for (int row = r0; row < r1; row++) /* the floor, straight into the strip */
    for (int yy = 0; yy < CS; yy++) {
      int y = Y0 + row * CS + yy;
      if (y < gfx_y0 || y >= gfx_y1) continue;
      color_t *px = gfx_buf + (y - gfx_y0) * SCREEN_W;
      for (int c = 0; c < GW; c++) {
        int v = floor_[row][c];
        color_t k = v ? paint[v - 1] : bare;
        px[c * 4] = px[c * 4 + 1] = px[c * 4 + 2] = px[c * 4 + 3] = k;
      }
    }
  for (int p = 0; p < np; p++) {
    float c = fcos(r[p].a), s = fsin(r[p].a);
    quad(r[p].x + c * 8, r[p].y + s * 8, r[p].a + PI / 2, -7, 7, 2.5f, dark[p]); /* the roller */
    line(r[p].x, r[p].y, r[p].x + c * 8, r[p].y + s * 8, 2, HEX(0x5A5466));
    blob(r[p].x, r[p].y, 7, p, r[p].a);
    tag_at(p, r[p].x, r[p].y - 7);
  }
  int total = GW * GH, x = 0; /* the shares, along the bottom */
  for (int s = 0; s < (teams ? 2 : np); s++) {
    int w = count[s] * SCREEN_W / total;
    gfx_fill(x, SCREEN_H - 4, w, 4, col[s]);
    x += w;
  }
  char t[4];
  num(t, (uint32_t)(rt < TIME ? (TIME - rt + 999) / 1000 : 0));
  if (rt > 0) say(&np_font_title, 160, TOP + 24, t, INK);
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0xF1ECE1));
  gfx_rrect(x + 4, y + 4, 26, 18, 8, gfx_mix(col[0], WHITE, 9), 256);
  gfx_rrect(x + 26, y + 14, 26, 17, 8, gfx_mix(col[1], WHITE, 9), 256);
  disc(x + 26, y + 12, 5, col[0]), disc(x + 30, y + 24, 5, col[1]);
}

#ifdef HOST
static int bot(int p) {
  static int32_t next[NMAX];
  if (bot_fresh(p)) next[p] = 0;
  float ax = r[p].x + fcos(r[p].a) * 24, ay = r[p].y + fsin(r[p].a) * 24;
  bool wall = ax < 0 || ax > SCREEN_W || ay < TOP || ay > SCREEN_H;
  if (rt < next[p] && !wall) return bot_tap(p, false);
  next[p] = rt + 400 + rn(1200);
  return bot_tap(p, true);
}
#endif

const game_t g_paint = {"Paint", "Paint", "Tap  curve the other way",
                        "Your roller never stops and always curves. Tap your key to curve the other way. After 30 "
                        "seconds, the most floor in your colour takes the round.",
                        0, start, step, draw, icon BOT(bot)};
