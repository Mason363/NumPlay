/* Soccer: each player's arrow spins, a tap dashes where it points and turns the spin the other way. Two sides
 * play on a pitch with a goal at each end: the first goal takes the round. Three or four players get a goal
 * each, in the corners: let one in and you're out, the last one left takes the round. */
#include "fp.h"

#define PR 9.0f  /* a player */
#define BR 6.0f  /* the ball */
#define SPIN 3.6f
#define DASH 200.0f
#define FAST 300.0f
#define COOL 280
#define CX 160.0f
#define CY 131.0f
typedef struct {
  float x0, y0, x1, y1;
  int8_t goal; /* the side whose goal it is, or -1: a wall */
} seg_t;
static seg_t seg[12];
static int nseg;
static bool corners; /* the corner goals of 3 and 4 players */
static struct {
  body_t b;
  float a, spin;
  int32_t dash_t;
} pl[NMAX];
static body_t ball;
static int32_t goal_t, kick_t; /* the last goal, the next kick-off */
static int last_goal;          /* whose */

static void add(float x0, float y0, float x1, float y1, int goal) {
  seg[nseg++] = (seg_t){x0, y0, x1, y1, (int8_t)goal};
}
static void kick_off(void) { ball = (body_t){CX, CY, 0, 0}; }

static void start(void) {
  nseg = 0, corners = !teams && np > 2;
  if (corners) { /* an octagon: its four corners are goals (bottom right a wall for 3 players) */
    float L = 8, R = 312, T = 26, B = 236, c = 46;
    add(L + c, T, R - c, T, -1), add(R - c, T, R, T + c, 1), add(R, T + c, R, B - c, -1), add(R, B - c, R - c, B, np > 3 ? 3 : -1);
    add(R - c, B, L + c, B, -1), add(L + c, B, L, B - c, 2), add(L, B - c, L, T + c, -1), add(L, T + c, L + c, T, 0);
  } else { /* a pitch: the left goal the left side's (player 1, or the red team) */
    float L = 14, R = 306, T = 26, B = 236;
    add(L, T, R, T, -1), add(R, T, R, CY - 30, -1), add(R, CY - 30, R, CY + 30, 1), add(R, CY + 30, R, B, -1);
    add(R, B, L, B, -1), add(L, B, L, CY + 30, -1), add(L, CY + 30, L, CY - 30, 0), add(L, CY - 30, L, T, -1);
  }
  static const float TEAM[3][3][2] = {{{60, 131}}, {{85, 95}, {85, 167}}, {{95, 70}, {55, 131}, {95, 192}}};
  for (int p = 0; p < np; p++) {
    float x, y;
    if (corners) x = CX + fcos(seat(p)) * 100, y = CY + fsin(seat(p)) * 64;
    else {
      const float *t = TEAM[np / 2 - 1][p >> 1];
      x = p & 1 ? 320 - t[0] : t[0], y = t[1];
    }
    pl[p].b = (body_t){x, y, 0, 0}, pl[p].a = fatan2(CY - y, CX - x), pl[p].spin = 1, pl[p].dash_t = -10000;
  }
  kick_off();
  goal_t = -10000, kick_t = -1, last_goal = -1;
}

/* a disc against a segment (a wall, or a goal for the players): pushed out, bounced (e) */
static void against(body_t *b, const seg_t *s, float r, float e) {
  float dx = s->x1 - s->x0, dy = s->y1 - s->y0, l2 = dx * dx + dy * dy;
  float t = clampf(((b->x - s->x0) * dx + (b->y - s->y0) * dy) / l2, 0, 1);
  float cx = s->x0 + dx * t, cy = s->y0 + dy * t, ox = b->x - cx, oy = b->y - cy, d2 = ox * ox + oy * oy;
  if (d2 >= r * r) return;
  float d = fsqrt(d2), nx, ny;
  if (d > 0.001f) nx = ox / d, ny = oy / d;
  else nx = dy / fsqrt(l2), ny = -dx / fsqrt(l2);
  b->x = cx + nx * r, b->y = cy + ny * r;
  float vn = b->vx * nx + b->vy * ny;
  if (vn < 0) b->vx -= (1 + e) * vn * nx, b->vy -= (1 + e) * vn * ny;
}
/* how far past a goal's line the ball is (the walls go clockwise round the field: outside is on their left) */
static float past(const seg_t *s, float x, float y) {
  float dx = s->x1 - s->x0, dy = s->y1 - s->y0, l = fsqrt(dx * dx + dy * dy);
  float t = ((x - s->x0) * dx + (y - s->y0) * dy) / (l * l);
  if (t < 0 || t > 1) return -1;
  return ((x - s->x0) * dy - (y - s->y0) * dx) / l;
}
static bool open_goal(const seg_t *s) { return s->goal >= 0 && (teams || !gone[s->goal]); }

static void step(void) {
  for (int p = 0; p < np; p++) {
    if (gone[p]) continue;
    body_t *b = &pl[p].b;
    pl[p].a = wrap_pi(pl[p].a + pl[p].spin * SPIN * DT);
    if (tap[p] && rt - pl[p].dash_t >= COOL) {
      b->vx += fcos(pl[p].a) * DASH, b->vy += fsin(pl[p].a) * DASH;
      float v = fsqrt(b->vx * b->vx + b->vy * b->vy);
      if (v > FAST) b->vx *= FAST / v, b->vy *= FAST / v;
      pl[p].spin = -pl[p].spin, pl[p].dash_t = rt;
    }
    b->vx *= 0.975f, b->vy *= 0.975f;
    b->x += b->vx * DT, b->y += b->vy * DT;
  }
  bool in_play = kick_t < 0;
  if (!in_play && rt >= kick_t) kick_off(), kick_t = -1, in_play = true;
  ball.vx *= in_play ? 0.992f : 0.8f, ball.vy *= in_play ? 0.992f : 0.8f; /* (in the net: it stops) */
  float v = fsqrt(ball.vx * ball.vx + ball.vy * ball.vy);
  if (v > 420) ball.vx *= 420 / v, ball.vy *= 420 / v;
  ball.x += ball.vx * DT, ball.y += ball.vy * DT;
  for (int p = 0; p < np; p++) {
    if (gone[p]) continue;
    for (int q = p + 1; q < np; q++)
      if (!gone[q]) collide(&pl[p].b, &pl[q].b, 2 * PR, 1, 1, 0.7f);
    if (in_play) collide(&pl[p].b, &ball, PR + BR, 3, 1, 0.9f);
  }
  for (int i = 0; i < nseg; i++) {
    for (int p = 0; p < np; p++)
      if (!gone[p]) against(&pl[p].b, &seg[i], PR, 0.3f);
    if (!open_goal(&seg[i])) against(&ball, &seg[i], BR, 0.8f);
    else { /* the posts */
      body_t *b = &ball;
      for (int k = 0; k < 2; k++) {
        seg_t post = {k ? seg[i].x1 : seg[i].x0, k ? seg[i].y1 : seg[i].y0, k ? seg[i].x1 : seg[i].x0, (k ? seg[i].y1 : seg[i].y0) + 0.01f, -1};
        against(b, &post, BR + 1, 0.8f);
      }
      if (in_play && past(&seg[i], ball.x, ball.y) > BR) { /* a goal */
        int s = seg[i].goal;
        goal_t = rt, last_goal = s, kick_t = rt + 1100;
        if (teams) win(1 - s);
        else out(s);
        in_play = false;
      }
    }
  }
  if (rt > 90000) tie();
}

static void lines(void) {
  color_t w = HEX(0xEAF5EA);
  for (int i = 0; i < nseg; i++)
    if (!open_goal(&seg[i])) line(seg[i].x0, seg[i].y0, seg[i].x1, seg[i].y1, 3, w);
  disc(CX, CY, 30, w), disc(CX, CY, 28, HEX(0x40A050)), disc(CX, CY, 3, w);
  if (!corners) gfx_fill(159, 26, 2, 210, w);
}
static void net(const seg_t *s) { /* the goal behind its line, in its side's colour */
  float dx = s->x1 - s->x0, dy = s->y1 - s->y0, l = fsqrt(dx * dx + dy * dy), nx = dy / l * 13, ny = -dx / l * 13;
  float p[8] = {s->x0, s->y0, s->x1, s->y1, s->x1 + nx * 0.8f, s->y1 + ny * 0.8f, s->x0 + nx * 0.8f, s->y0 + ny * 0.8f};
  poly(p, 4, dark[s->goal]);
  for (int k = 1; k < 6; k++) {
    float t = k / 6.0f;
    line(s->x0 + dx * t, s->y0 + dy * t, s->x0 + dx * t + nx * 0.8f, s->y0 + dy * t + ny * 0.8f, 1, lite[s->goal]);
  }
  for (int k = 1; k < 3; k++) line(s->x0 + nx * 0.27f * k, s->y0 + ny * 0.27f * k, s->x1 + nx * 0.27f * k, s->y1 + ny * 0.27f * k, 1, lite[s->goal]);
  disc(s->x0, s->y0, 3, col[s->goal]), disc(s->x1, s->y1, 3, col[s->goal]);
}
static void draw(void) {
  gfx_fill(0, TOP, SCREEN_W, SCREEN_H - TOP, HEX(0x255E33));
  if (corners) {
    float P[16] = {54, 26, 266, 26, 312, 72, 312, 190, 266, 236, 54, 236, 8, 190, 8, 72};
    poly(P, 8, HEX(0x40A050));
  } else {
    gfx_fill(14, 26, 292, 210, HEX(0x40A050));
  }
  for (int x = 14; x < 306; x += 40) gfx_fill_alpha(x, 26, 20, 210, HEX(0x4DB25D), 120);
  if (corners) { /* (the stripes trimmed to the octagon) */
    for (int k = 0; k < 4; k++) {
      float cx = k & 1 ? 312 : 8, cy = k & 2 ? 236 : 26, ex = k & 1 ? -46 : 46, ey = k & 2 ? -46 : 46;
      float tr[6] = {cx, cy, cx + ex, cy, cx, cy + ey};
      poly(tr, 3, HEX(0x255E33));
    }
  }
  lines();
  for (int i = 0; i < nseg; i++)
    if (open_goal(&seg[i])) net(&seg[i]);
  disc_a(ball.x + 1.5f, ball.y + 2.5f, BR, 0, 80);
  disc(ball.x, ball.y, BR, WHITE);
  disc(ball.x - 1, ball.y - 1, 2, HEX(0x2A2A2A)), disc(ball.x + 2.5f, ball.y + 2, 1.5f, HEX(0x2A2A2A));
  for (int p = 0; p < np; p++) {
    if (gone[p]) continue;
    float x = pl[p].b.x, y = pl[p].b.y, a = pl[p].a, c = fcos(a), s = fsin(a), d = PR + 3;
    float t[6] = {x + c * (d + 7), y + s * (d + 7), x + c * d - s * 4.5f, y + s * d + c * 4.5f, x + c * d + s * 4.5f, y + s * d - c * 4.5f};
    poly(t, 3, WHITE);
    blob(x, y, PR, p, a);
    tag_at(p, x, y - PR);
  }
  if (rt - goal_t < 1100 && last_goal >= 0) say(&np_font_title, 160, 80, "GOAL!", WHITE);
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x40A050));
  gfx_fill(x + 27, y + 2, 2, 30, HEX(0xEAF5EA));
  gfx_fill(x + 2, y + 11, 6, 12, dark[0]), gfx_fill(x + 48, y + 11, 6, 12, dark[1]);
  disc(x + 17, y + 17, 5, col[0]), disc(x + 39, y + 17, 5, col[1]);
  disc(x + 28, y + 9, 3, WHITE);
}

#ifdef HOST
static int bot(int p) {
  float want = fatan2(ball.y - pl[p].b.y, ball.x - pl[p].b.x);
  return bot_tap(p, fabs_(wrap_pi(pl[p].a - want)) < 0.25f && rt - pl[p].dash_t >= COOL && rn(2) == 0);
}
#endif

const game_t g_soccer = {"Soccer", "Soccer",
                         "Your arrow spins. Tap your key to dash where it points and hit the ball. Two sides: the first "
                         "goal takes the round. With three or four, let one in and you're out.",
                         1, start, step, draw, icon BOT(bot)};
