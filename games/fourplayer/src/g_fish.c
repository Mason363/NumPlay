/* Grab the Fish: cats round a pond. Things jump out of it: tap to swipe, and the first paw on a goldfish keeps
 * it. Swipe at a boot, a can or a puffer fish, or at nothing, and you're dizzy a moment. Three goldfish take the
 * round (in teams, three between you). */
#include "fp.h"

#define PX 160.0f /* the pond */
#define PY 134.0f
#define UP 1100   /* ms a thing is out of the water */
#define GOAL 3
enum { FISH, BOOT, CAN, PUFFER };
static int item, stage, grabber; /* stage: 0 under water, 1 out; grabber: who caught the last one (-1: it fell back) */
static int32_t item_t, done_t, react[NMAX];
static int32_t paw_t[NMAX], dizzy_t[NMAX], dizzy_ms[NMAX];
static uint8_t caught[NMAX], tally[NMAX];
static float cx[NMAX], cy[NMAX];

static void next_item(int32_t after) {
  item = rn(100) < 55 ? FISH : 1 + rn(3), stage = 0, item_t = rt + after;
  for (int p = 0; p < np; p++) react[p] = 230 + rn(260);
}
static void start(void) {
  for (int p = 0; p < np; p++) {
    float s = seat(p);
    cx[p] = PX + fcos(s) * 124, cy[p] = PY + fsin(s) * 80;
    if (cy[p] > 222) cy[p] = 222;
    paw_t[p] = dizzy_t[p] = -10000, dizzy_ms[p] = 0, caught[p] = tally[p] = 0;
  }
  done_t = -10000, grabber = -1;
  next_item(1200);
}
static bool dizzy(int p) { return rt - dizzy_t[p] < dizzy_ms[p]; }

static void step(void) {
  if (stage == 0 && rt >= item_t) stage = 1;
  if (stage == 1 && rt - item_t >= UP) grabber = -1, done_t = rt, next_item(900 + rn(1700));
  int first = -1, n = 0;
  for (int p = 0; p < np; p++) {
    if (!tap[p] || dizzy(p)) continue;
    paw_t[p] = rt;
    bool up = stage == 1 && rt - item_t > 80;
    if (up && item == FISH) {
      if (rn(++n) == 0) first = p; /* (two paws at once: either) */
    } else {
      dizzy_t[p] = rt, dizzy_ms[p] = up ? 1800 : 900;
    }
  }
  if (first >= 0) {
    caught[first]++, tally[side(first)]++;
    grabber = first, done_t = rt;
    if (tally[side(first)] >= GOAL) win(first);
    else next_item(1000 + rn(1600));
  }
}

static void fish(float x, float y, float s, float a, color_t c) { /* a goldfish, s its size */
  float ca = fcos(a), sa = fsin(a);
  float tail[6] = {x - ca * s * 0.8f, y - sa * s * 0.8f, x - ca * s * 1.6f - sa * s * 0.6f, y - sa * s * 1.6f + ca * s * 0.6f,
                   x - ca * s * 1.6f + sa * s * 0.6f, y - sa * s * 1.6f - ca * s * 0.6f};
  poly(tail, 3, HEX(0xF07A1A));
  disc(x, y, s, c);
  disc(x + ca * s * 0.45f, y + sa * s * 0.45f, s * 0.75f, c);
  disc(x + ca * s * 0.6f - sa * s * 0.25f, y + sa * s * 0.6f - s * 0.25f, s * 0.22f > 1 ? s * 0.22f : 1, HEX(0x1A1A1A));
}
static void thing(float x, float y, float a) {
  switch (item) {
    case FISH: fish(x, y, 7, a - PI / 2 + 0.3f, HEX(0xFFB51E)); break;
    case BOOT:
      quad(x, y, a, -9, 2, 3.5f, HEX(0x7A4E2A));
      quad(x + fcos(a) * 3 + fsin(a) * 4, y + fsin(a) * 3 - fcos(a) * 4, a + PI / 2, -3, 6, 4, HEX(0x7A4E2A));
      quad(x, y, a, -9, -7, 3.6f, HEX(0x4E3018));
      break;
    case CAN:
      quad(x, y, a, -7, 7, 4.5f, HEX(0xA8B0BC));
      quad(x, y, a, -2, 2, 4.6f, HEX(0xD8463E));
      break;
    default:
      for (int i = 0; i < 8; i++) {
        float b = a + i * PI / 4, t[6] = {x + fcos(b) * 10, y + fsin(b) * 10, x + fcos(b + 0.4f) * 6, y + fsin(b + 0.4f) * 6,
                                          x + fcos(b - 0.4f) * 6, y + fsin(b - 0.4f) * 6};
        poly(t, 3, HEX(0x6E8F2A));
      }
      disc(x, y, 7, HEX(0x9CC23F));
      eyes(x, y, 7, PI / 2);
  }
}
static void cat(int p) {
  float x = cx[p], y = cy[p], look = fatan2(PY - y, PX - x);
  for (int k = -1; k <= 1; k += 2) { /* the ears */
    float e[6] = {x + k * 12, y - 6, x + k * 4, y - 12, x + k * 13, y - 19};
    poly(e, 3, dark[p]);
  }
  blob(x, y, 13, p, look);
  disc(x, y + 4, 2, HEX(0xF4A0B0));
  for (int k = -1; k <= 1; k += 2) line(x + k * 5, y + 4, x + k * 15, y + 2 + k * 0, 1, lite[p]);
  if (dizzy(p))
    for (int i = 0; i < 3; i++) {
      float b = rt * 0.008f + i * 2.1f;
      disc(x + fcos(b) * 12, y - 16 + fsin(b) * 4, 2, GOLD);
    }
  int32_t k = rt - paw_t[p];
  if (k < 260) { /* the swipe */
    float f = k < 110 ? k / 110.0f : (260 - k) / 150.0f, px = x + (PX - x) * 0.8f * f, py = y + (PY - 10 - y) * 0.8f * f;
    line(x, y, px, py, 7, col[p]);
    disc(px, py, 5.5f, lite[p]);
  }
  for (int i = 0; i < GOAL; i++) { /* its goldfish */
    float fx = x - 16 + i * 14, fy = y + 22;
    if (fy > 234) fy = y - 30;
    if (i < caught[p]) fish(fx, fy, 4, 0, HEX(0xFFB51E));
    else disc_a(fx, fy, 3, 0, 60);
  }
  tag_at(p, x, y - 18);
}
static void draw(void) {
  for (int y = gfx_y0 < TOP ? TOP : gfx_y0; y < gfx_y1; y++) gfx_fill(0, y, SCREEN_W, 1, (y / 5) & 1 ? HEX(0x5DAA4A) : HEX(0x58A346));
  gfx_rrect(PX - 78, PY - 52, 156, 104, 52, HEX(0xE6D29A), 256);
  gfx_rrect(PX - 72, PY - 46, 144, 92, 46, HEX(0x2D7FC4), 256);
  gfx_rrect(PX - 64, PY - 38, 128, 76, 38, HEX(0x3590D6), 256);
  if (stage == 0 && item_t - rt < 700) { /* something under the water */
    float r = 8 + (rt % 600) / 40.0f;
    disc_a(PX, PY, r, HEX(0x7CC0EE), 140 - (int)r * 4);
  }
  if (stage == 1) {
    float f = (rt - item_t) / (float)UP, h = 52 * 4 * f * (1 - f);
    disc_a(PX, PY + 2, 10 * (1 - f) + 4, HEX(0x1F5F9A), 140);
    thing(PX, PY - h, (f - 0.5f) * 1.6f);
  }
  if (rt - done_t < 500) {
    float f = (rt - done_t) / 500.0f;
    if (grabber >= 0) fish(PX + (cx[grabber] - PX) * f, PY - 30 + (cy[grabber] + 22 - PY + 30) * f, 7 - 3 * f, 0, HEX(0xFFB51E));
    else disc_a(PX, PY, 6 + f * 14, WHITE, (int)(200 * (1 - f)));
  }
  for (int p = 0; p < np; p++) cat(p);
  if (teams)
    for (int s = 0; s < 2; s++)
      for (int i = 0; i < GOAL; i++) {
        float x = s ? 214 + i * 14 : 78 + i * 14;
        if (i < tally[s]) fish(x, TOP + 12, 4, 0, HEX(0xFFB51E));
        else disc_a(x, TOP + 12, 3, 0, 60);
        if (!i) disc(x - 14, TOP + 12, 4, col[s]);
      }
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x5DAA4A));
  gfx_rrect(x + 11, y + 16, 34, 15, 7, HEX(0x3590D6), 256);
  fish(x + 30, y + 10, 5, -0.3f, HEX(0xFFB51E));
  disc(x + 8, y + 25, 5, col[0]), disc(x + 48, y + 25, 5, col[1]);
}

#ifdef HOST
static int bot(int p) {
  bool up = stage == 1 && rt - item_t > react[p];
  bool want = up && (item == FISH || (react[p] % 17 == 0 && rt - item_t > 300));
  return bot_tap(p, want && !dizzy(p));
}
#endif

const game_t g_fish = {"Grab the Fish", "Fish",
                       "Things jump out of the pond. Tap your key to swipe: the first paw on a goldfish keeps it. Swipe "
                       "at anything else and you're dizzy. Three goldfish take the round.",
                       0, start, step, draw, icon BOT(bot)};
