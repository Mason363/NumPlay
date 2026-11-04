/* Balloons: every tap pumps your balloon bigger, but each one bursts at its own size, which nobody knows. After
 * 10 seconds the biggest balloon still whole takes the round (in teams, the most air between you). */
#include "fp.h"

#define TIME 10000
static int pumps[NMAX], burst_at[NMAX];
static int32_t pump_t[NMAX], burst_t[NMAX], last_t[NMAX];

static const uint8_t ORDER[4][NMAX] = {{0, 1}, {0, 2, 1}, {0, 2, 3, 1}, {0, 4, 2, 3, 5, 1}}; /* left to right */
static float column(int p) {
  const uint8_t *o = ORDER[np == 2 ? 0 : np == 3 ? 1 : np == 4 ? 2 : 3];
  int i = 0;
  while (o[i] != p) i++;
  return (i + 0.5f) * SCREEN_W / np;
}
static float radius(int p) {
  float big = SCREEN_W / np / 2.0f - 5;
  if (big > 34) big = 34;
  return 7 + pumps[p] * (big - 7) / 40;
}
static void start(void) {
  for (int p = 0; p < np; p++) pumps[p] = 0, burst_at[p] = 18 + rn(19), pump_t[p] = burst_t[p] = last_t[p] = -10000;
}
static void decide(void) {
  int ns = teams ? 2 : np, air[NMAX] = {0}, whole = 0, best = -1, n = 0, who = -1;
  for (int p = 0; p < np; p++)
    if (burst_t[p] < 0) whole |= 1 << side(p), air[side(p)] = teams ? air[side(p)] + pumps[p] : pumps[p];
  if (!whole) {
    tie();
    return;
  }
  if (rt < TIME && (whole & (whole - 1))) return; /* (two sides or more still have a whole balloon) */
  for (int s = 0; s < ns; s++) {
    if (!(whole >> s & 1)) continue;
    if (air[s] > best || (!teams && air[s] == best && last_t[s] < last_t[who])) best = air[s], n = 1, who = s;
    else if (air[s] == best && (teams || last_t[s] == last_t[who])) n++;
  }
  if (n == 1) win(who);
  else tie();
}
static void step(void) {
  for (int p = 0; p < np; p++) {
    if (!tap[p] || burst_t[p] >= 0 || rt >= TIME) continue;
    pumps[p]++, pump_t[p] = last_t[p] = rt;
    if (pumps[p] >= burst_at[p]) burst_t[p] = rt;
  }
  if (!over()) decide();
}

static void balloon(int p, float x, float y, float r) {
  disc(x, y + r * 0.08f, r, dark[p]);
  disc(x, y - r * 0.05f, r * 0.96f, col[p]);
  disc_a(x - r * 0.38f, y - r * 0.42f, r * 0.26f, WHITE, 150);
  float k[6] = {x, y + r - 1, x - 3, y + r + 4, x + 3, y + r + 4};
  poly(k, 3, dark[p]);
}
static void draw(void) {
  gfx_vgrad(0, TOP, SCREEN_W, SCREEN_H - TOP, 0x9FD8F7, 0xE9F7FF);
  for (int i = 0; i < 3; i++) { /* clouds */
    float cx = 50 + i * 115 + fsin(rt * 0.0002f + i) * 8, cy = 50 + (i & 1) * 18;
    disc_a(cx, cy, 13, WHITE, 200), disc_a(cx + 14, cy + 3, 10, WHITE, 200), disc_a(cx - 14, cy + 4, 9, WHITE, 200);
  }
  gfx_fill(0, 222, SCREEN_W, SCREEN_H - 222, HEX(0x7BBF5A));
  char t[6];
  for (int p = 0; p < np; p++) {
    float x = column(p), r = radius(p), shake = pumps[p] > 14 ? fsin(rt * 0.06f + p * 1.7f) * (pumps[p] - 14) * 0.07f : 0;
    float by = 168 - r;
    int32_t k = rt - pump_t[p];
    int down = k < 140 ? (int)(8 - k / 18) : 0;
    line(x, 214, x, 182, 2, HEX(0x5A5466)); /* the tube */
    if (burst_t[p] < 0) balloon(p, x + shake, by, r);
    else {
      int32_t b = rt - burst_t[p];
      for (int i = 0; i < 7 && b < 600; i++) {
        float a = i * 0.9f, d = r * 0.6f + b * 0.08f, px = x + fcos(a) * d, py = by + fsin(a) * d + b * b * 0.00006f;
        float s[6] = {px, py, px + 4, py + 2, px + 1, py + 5};
        poly(s, 3, col[p]);
      }
      if (b < 700) say(&np_font_title, (int)x, (int)by + 7, "POP", WHITE);
      quad(x, 180, PI / 2 + 0.4f, -2, 8, 2.5f, dark[p]); /* what's left */
    }
    gfx_fill((int)x - 2, 196 + down - 18, 4, 18, HEX(0x8A8494)); /* the pump: its handle, its body */
    box((int)x - 9, 192 + down - 18, 18, 4, 2, HEX(0x3D3846));
    box((int)x - 8, 196, 16, 26, 4, col[p]);
    gfx_fill((int)x - 8, 200, 16, 3, dark[p]);
    num(t, (uint32_t)pumps[p]);
    gfx_text_center(&np_font_body, (int)x, 236, t, INK, 256);
    tag_at(p, x, by - r);
  }
  num(t, (uint32_t)(rt < TIME ? (TIME - rt + 999) / 1000 : 0));
  if (rt > 0) say(&np_font_title, 160, TOP + 24, t, WHITE);
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x9FD8F7));
  disc(x + 18, y + 13, 9, col[0]), disc(x + 40, y + 16, 6, col[1]);
  disc_a(x + 15, y + 10, 3, WHITE, 150);
  gfx_fill(x + 17, y + 22, 2, 9, HEX(0x5A5466)), gfx_fill(x + 39, y + 22, 2, 9, HEX(0x5A5466));
}

#ifdef HOST
static int bot(int p) {
  static int32_t next[NMAX];
  static int goal[NMAX];
  if (bot_fresh(p)) next[p] = 0, goal[p] = 16 + rn(14);
  if (pumps[p] >= goal[p] || rt < next[p]) return bot_tap(p, false);
  next[p] = rt + 120 + rn(80);
  return bot_tap(p, true);
}
#endif

const game_t g_balloon = {"Balloons", "Balloons", "Tap  pump",
                          "Tap your key to pump. Each balloon bursts at its own size, and nobody knows which. After 10 "
                          "seconds the biggest balloon still whole takes the round.",
                          0, start, step, draw, icon BOT(bot)};
