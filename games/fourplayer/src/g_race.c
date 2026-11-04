/* Racing: little cars on a loop, driven anticlockwise. They go on their own; holding the key turns left, and
 * every bend turns left. Grass slows, the island's barrier pushes you along it, oil takes your steering away
 * a moment. Three laps: the first one home takes the round. */
#include "fp.h"

#define OX 6 /* the track: between the outer rounded rectangle and the island's */
#define OY 26
#define OW 308
#define OH 210
#define OR 74
#define IX 54
#define IY 74
#define IW 212
#define IH 114
#define IR 28
#define MX 160.0f /* its middle: laps are counted round it */
#define MY 131.0f
#define LAPS 3
#define CR 5.5f   /* a car */
#define TURN 3.0f /* radians a second */
#define FAST 125.0f
#define SLOW 50.0f
static struct {
  float x, y, h, va, s, prog, last_a; /* h: where it points, va: where it goes, prog: radians driven round */
  int32_t oil_t, bump_t;
} c[NMAX];
static float oil[2][2];

/* how far (x, y) is outside a rounded rectangle (negative inside) */
static float rbox(float x, float y, float bx, float by, float bw, float bh, float r) {
  float qx = fabs_(x - (bx + bw / 2)) - (bw / 2 - r), qy = fabs_(y - (by + bh / 2)) - (bh / 2 - r);
  float mx = qx > 0 ? qx : 0, my = qy > 0 ? qy : 0, in = qx > qy ? qx : qy;
  return fsqrt(mx * mx + my * my) + (in < 0 ? in : 0) - r;
}
static float island(float x, float y) { return rbox(x, y, IX, IY, IW, IH, IR); }

static void start(void) {
  static const float SPOT[3][2] = {{160, 46}, {28, 131}, {292, 131}}; /* oil: on the top straight or a side */
  int skip = rn(3);
  for (int i = 0, k = 0; i < 3; i++)
    if (i != skip) oil[k][0] = SPOT[i][0] + rr(-30, 30) * (i == 0), oil[k][1] = SPOT[i][1] + rr(-22, 22) * (i > 0), k++;
  for (int p = 0; p < np; p++) {
    int row = p / 3, lane = p % 3, lanes = np < 3 ? np : 3;
    c[p].x = 148 - row * 22 - lane * 3, c[p].y = 212 + (lane - (lanes - 1) / 2.0f) * 14;
    c[p].h = c[p].va = 0, c[p].s = 0, c[p].oil_t = -10000, c[p].bump_t = -10000;
    c[p].last_a = fatan2(c[p].y - MY, c[p].x - MX), c[p].prog = PI / 2 - c[p].last_a;
  }
}

/* against a wall whose way out is (nx, ny): along it, a little slower */
static void slide(int p, float nx, float ny, float push) {
  c[p].x += nx * push, c[p].y += ny * push;
  float vx = fcos(c[p].va), vy = fsin(c[p].va), vn = vx * nx + vy * ny;
  if (vn >= 0) return;
  vx -= vn * nx, vy -= vn * ny;
  if (vx * vx + vy * vy < 0.01f) vx = -ny, vy = nx;
  c[p].va = c[p].h = fatan2(vy, vx);
  if (rt - c[p].bump_t > 200) c[p].s *= 0.7f;
  c[p].bump_t = rt;
}

static void step(void) {
  for (int p = 0; p < np; p++) {
    bool oily = rt - c[p].oil_t < 550, grass = rbox(c[p].x, c[p].y, OX, OY, OW, OH, OR) > 0;
    if (hold[p] && !oily) c[p].h = wrap_pi(c[p].h - TURN * DT);
    float top = grass ? SLOW : hold[p] ? FAST * 0.94f : FAST;
    if (over() && rt > 0) top = c[p].s * 0.98f; /* (the round decided: they roll to a stop) */
    c[p].s += c[p].s < top ? 150 * DT : -260 * DT;
    if (c[p].s < 0) c[p].s = 0;
    if (!oily) /* the tyres grip: where it goes follows where it points, a little late */
      c[p].va = wrap_pi(c[p].va + wrap_pi(c[p].h - c[p].va) * (grass ? 0.05f : 0.14f));
    c[p].x += fcos(c[p].va) * c[p].s * DT, c[p].y += fsin(c[p].va) * c[p].s * DT;
    for (int k = 0; k < 2; k++) {
      float dx = c[p].x - oil[k][0], dy = c[p].y - oil[k][1];
      if (dx * dx + dy * dy < 81 && !oily) c[p].oil_t = rt;
    }
  }
  for (int p = 0; p < np; p++)
    for (int q = p + 1; q < np; q++) {
      body_t a = {c[p].x, c[p].y, fcos(c[p].va) * c[p].s, fsin(c[p].va) * c[p].s};
      body_t b = {c[q].x, c[q].y, fcos(c[q].va) * c[q].s, fsin(c[q].va) * c[q].s};
      if (!collide(&a, &b, 2 * CR, 1, 1, 0.4f)) continue;
      c[p].x = a.x, c[p].y = a.y, c[p].s = fsqrt(a.vx * a.vx + a.vy * a.vy), c[p].va = fatan2(a.vy, a.vx);
      c[q].x = b.x, c[q].y = b.y, c[q].s = fsqrt(b.vx * b.vx + b.vy * b.vy), c[q].va = fatan2(b.vy, b.vx);
    }
  for (int p = 0; p < np; p++) {
    float d = island(c[p].x, c[p].y);
    if (d < CR) { /* the island's barrier */
      float e = 0.5f, gx = island(c[p].x + e, c[p].y) - island(c[p].x - e, c[p].y);
      float gy = island(c[p].x, c[p].y + e) - island(c[p].x, c[p].y - e), l = fsqrt(gx * gx + gy * gy);
      if (l > 0.0001f) slide(p, gx / l, gy / l, CR - d);
    }
    if (c[p].x < CR) slide(p, 1, 0, CR - c[p].x);
    if (c[p].x > SCREEN_W - CR) slide(p, -1, 0, c[p].x - (SCREEN_W - CR));
    if (c[p].y < TOP + CR) slide(p, 0, 1, TOP + CR - c[p].y);
    if (c[p].y > SCREEN_H - CR) slide(p, 0, -1, c[p].y - (SCREEN_H - CR));
    float a = fatan2(c[p].y - MY, c[p].x - MX);
    c[p].prog -= wrap_pi(a - c[p].last_a), c[p].last_a = a;
    if (c[p].prog >= LAPS * 2 * PI) win(p);
  }
}

static int lap(int p) { return clampi((int)(c[p].prog / (2 * PI)) + 1, 1, LAPS); }

static void car(int p, float x, float y, float h) {
  quad(x + 1.5f, y + 2.5f, h, -7, 7, 5, HEX(0x2C3A2F)); /* shadow */
  for (int i = 0; i < 4; i++) {
    float l = i < 2 ? 3.5f : -4.5f, e = i & 1 ? 4.2f : -4.2f;
    quad(x + fcos(h) * l - fsin(h) * e, y + fsin(h) * l + fcos(h) * e, h, -2, 2, 1.4f, HEX(0x15131C));
  }
  quad(x, y, h, -7, 7, 3.6f, col[p]);
  quad(x, y, h, -1, 3.5f, 2.6f, HEX(0x1E2A3A)); /* the windscreen */
  quad(x, y, h, -6.5f, -4.5f, 3.2f, dark[p]);
  quad(x, y, h, 5.5f, 7, 3.2f, lite[p]);
}

static void draw(void) {
  for (int y = gfx_y0 < TOP ? TOP : gfx_y0; y < gfx_y1; y++) gfx_fill(0, y, SCREEN_W, 1, (y >> 3) & 1 ? HEX(0x3F9A48) : HEX(0x46A250));
  gfx_rrect(OX - 3, OY - 3, OW + 6, OH + 6, OR + 3, HEX(0xF2F2F2), 256);
  gfx_rrect(OX, OY, OW, OH, OR, HEX(0x52555F), 256);
  for (int i = 0; i < 12; i++) { /* the start line, chequered */
    int y = 188 + i * 4;
    gfx_fill(156, y, 4, 4, i & 1 ? HEX(0x1A1A1A) : WHITE);
    gfx_fill(160, y, 4, 4, i & 1 ? WHITE : HEX(0x1A1A1A));
  }
  gfx_rrect(IX - 3, IY - 3, IW + 6, IH + 6, IR + 3, HEX(0xE84A3F), 256);
  gfx_rrect(IX, IY, IW, IH, IR, HEX(0x2F7F3A), 256);
  for (int k = 0; k < 2; k++) {
    float ox = oil[k][0], oy = oil[k][1];
    disc_a(ox - 4, oy + 1, 6, HEX(0x1C1626), 235), disc_a(ox + 3, oy - 2, 7, HEX(0x1C1626), 235);
    disc_a(ox + 1, oy + 4, 5, HEX(0x1C1626), 235);
    disc_a(ox + 3, oy - 4, 2, HEX(0x8C7BD8), 170), disc_a(ox - 4, oy + 2, 1.5f, HEX(0x5FC0C8), 150);
  }
  /* the laps, on the island */
  gfx_text_center(&np_font_small, 160, 96, "LAP", HEX(0xBFE5C4), 256);
  for (int p = 0; p < np; p++) {
    char s[6], *e = num(s, (uint32_t)lap(p));
    e[0] = '/', e[1] = (char)('0' + LAPS), e[2] = 0;
    int x = 160 + (p % 3 - (np < 3 ? np - 1 : 2) / 2.0f) * 56, y = 120 + p / 3 * 22;
    disc(x - 14, y - 4, 5, col[p]);
    gfx_text(&np_font_body, x - 6, y, s, WHITE, 256);
  }
  for (int p = 0; p < np; p++) {
    float h = c[p].h;
    if (rt - c[p].oil_t < 550) h += (rt - c[p].oil_t) * 0.012f; /* a spin on the oil */
    car(p, c[p].x, c[p].y, h);
    tag_at(p, c[p].x, c[p].y - 6);
  }
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x3F9A48));
  gfx_rrect(x + 5, y + 4, 46, 26, 12, HEX(0x52555F), 256);
  gfx_rrect(x + 15, y + 12, 26, 10, 5, HEX(0x2F7F3A), 256);
  box(x + 20, y + 24, 9, 5, 2, col[0]);
  box(x + 32, y + 24, 9, 5, 2, col[1]);
}

#ifdef HOST
static int bot(int p) {
  float a = fatan2(c[p].y - MY, c[p].x - MX) - 0.55f, tx = MX + fcos(a) * 134, ty = MY + fsin(a) * 86;
  float want = fatan2(ty - c[p].y, tx - c[p].x);
  return wrap_pi(want - c[p].h) < -0.06f;
}
#endif

const game_t g_race = {"Racing", "Racing",
                       "Your car drives itself. Hold your key to turn left: every bend turns left. Grass slows you, oil "
                       "spins you. Three laps: the first one home takes the round.",
                       0, start, step, draw, icon BOT(bot)};
