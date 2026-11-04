/* Skate Race: every tap is a kick. Tap as fast as you can and roll down your lane: the first over the line takes
 * the round. */
#include "fp.h"

#define LEN 1100.0f /* px to the line */
#define LANE 26
static float x[NMAX], v[NMAX], cam;
static int32_t kick_t[NMAX];

static int lanes_top(void) { return SCREEN_H - 6 - np * LANE; }
static void start(void) {
  for (int p = 0; p < np; p++) x[p] = 30, v[p] = 0, kick_t[p] = -10000;
  cam = 0;
}
static void step(void) {
  int first = -1;
  for (int p = 0; p < np; p++) {
    if (tap[p]) v[p] = v[p] + 24 > 300 ? 300 : v[p] + 24, kick_t[p] = rt;
    v[p] *= over() ? 0.97f : 0.986f;
    x[p] += v[p] * DT;
    if (x[p] >= LEN && (first < 0 || x[p] > x[first])) first = p;
  }
  if (first >= 0) win(first);
  float lead = 0;
  for (int p = 0; p < np; p++) lead = x[p] > lead ? x[p] : lead;
  float want = clampf(lead - 190, 0, LEN - 240);
  cam += (want - cam) * 0.15f;
}

static void draw(void) {
  int top = lanes_top();
  gfx_vgrad(0, TOP, SCREEN_W, top - TOP, 0x7EC8F2, 0xFCD9B4);
  for (int i = 0; i < 14; i++) { /* the town, far off: it moves slower */
    int w = 26 + (i * 37) % 22, h = 30 + (i * 53) % 48, bx = (int)(i * 52 - cam * 0.3f) % 728;
    if (bx < -60) bx += 728;
    gfx_fill(bx, top - 12 - h, w, h + 12, i & 1 ? HEX(0x8B7FB5) : HEX(0x9A8FC2));
    for (int k = 0; k < h / 12; k++) gfx_fill(bx + 5, top - 4 - h + k * 12, w - 10, 3, HEX(0xFFE7A8));
  }
  gfx_fill(0, top - 12, SCREEN_W, 12, HEX(0xBDB6A8)); /* the pavement */
  for (int m = 100; m <= (int)LEN; m += 100) {
    int sx = (int)(m - cam + 20);
    if (sx < -20 || sx > 340) continue;
    char s[6];
    num(s, (uint32_t)m);
    gfx_text_center(&np_font_small, sx, top - 2, m == (int)LEN ? "FINISH" : s, HEX(0x4A4458), 256);
  }
  for (int p = 0; p < np; p++) {
    int y = top + p * LANE;
    gfx_fill(0, y, SCREEN_W, LANE, p & 1 ? HEX(0x5C5966) : HEX(0x64616F));
    for (int k = 0; k < 12; k++) gfx_fill((int)(k * 32 - ((int)cam % 32)), y + LANE - 1, 16, 1, HEX(0x8C8898));
  }
  gfx_fill(0, SCREEN_H - 6, SCREEN_W, 6, HEX(0xBDB6A8));
  int fx = (int)(LEN - cam + 20); /* the line */
  for (int r = 0; r < np * LANE / 4; r++) gfx_fill(fx, top + r * 4, 4, 4, r & 1 ? HEX(0x1A1A1A) : WHITE), gfx_fill(fx + 4, top + r * 4, 4, 4, r & 1 ? WHITE : HEX(0x1A1A1A));
  for (int p = 0; p < np; p++) {
    float sx = x[p] - cam + 20, y = top + p * LANE + 12;
    bool behind = sx < 10;
    if (behind) sx = 10;
    if (rt - kick_t[p] < 160) line(sx - 3, y + 2, sx - 13, y + 8, 3, dark[p]); /* the kicking leg */
    box((int)sx - 10, (int)y + 6, 20, 3, 1, HEX(0x2A2433));
    disc(sx - 6, y + 10, 2, HEX(0xEDE7D6)), disc(sx + 6, y + 10, 2, HEX(0xEDE7D6));
    blob(sx, y - 1, 7, p, 0);
    if (behind) disc_a(sx, y - 1, 8, INK, 120);
    tag_at(p, sx, y - 8);
  }
}

static void icon(int x0, int y0) {
  box(x0 + 2, y0 + 1, 52, 32, 7, HEX(0x7EC8F2));
  gfx_fill(x0 + 2, y0 + 15, 52, 18, HEX(0x5C5966));
  for (int i = 0; i < 2; i++) {
    int y = y0 + 20 + i * 8;
    box(x0 + 14 + i * 10, y + 3, 12, 2, 1, HEX(0x2A2433));
    disc(x0 + 20 + i * 10, y, 4, col[i]);
  }
  gfx_fill(x0 + 44, y0 + 15, 3, 18, WHITE);
}

#ifdef HOST
static int bot(int p) {
  static int32_t next[NMAX], every[NMAX];
  if (bot_fresh(p)) next[p] = 0, every[p] = 100 + rn(40);
  if (rt < next[p]) return bot_tap(p, false);
  next[p] = rt + every[p] + rn(50);
  return bot_tap(p, true);
}
#endif

const game_t g_skate = {"Skate Race", "Skate",
                        "Every tap of your key is a kick. Tap as fast as you can: the first over the line takes the "
                        "round.",
                        0, start, step, draw, icon BOT(bot)};
