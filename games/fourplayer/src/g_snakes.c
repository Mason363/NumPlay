/* Snakes: they never stop. A tap turns yours right (two taps can wait their turn). Stars make you longer. Hit a
 * wall or a snake, your own too, and you're out: the last snake left takes the round. They speed up as the
 * round goes on. */
#include "fp.h"

#define CS 8 /* a cell */
#define GW 40
#define GH 27
#define X0 0
#define Y0 (TOP + 1)
#define MAXLEN 256
#define NSTAR 3
static const int8_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1}; /* right, down, left, up */
static uint8_t grid[GH][GW]; /* 0 free, else the snake's player + 1 */
static struct {
  uint16_t cell[MAXLEN]; /* a ring: the head at hd, the tail len - 1 before it */
  int hd, len, grow, dir, turns;
  int32_t dead_t;
} s[NMAX];
static int16_t star[NSTAR];
static int32_t next_t;

static int at(int x, int y) { return y * GW + x; }
static void place_star(int k) {
  for (int tries = 0; tries < 200; tries++) {
    int x = 2 + rn(GW - 4), y = 2 + rn(GH - 4), ok = !grid[y][x];
    for (int j = 0; j < NSTAR; j++) ok &= j == k || star[j] != at(x, y);
    for (int p = 0; p < np && ok; p++) {
      int h = s[p].cell[s[p].hd], d = (h % GW - x) * (h % GW - x) + (h / GW - y) * (h / GW - y);
      ok = d > 9;
    }
    if (ok) {
      star[k] = (int16_t)at(x, y);
      return;
    }
  }
  star[k] = -1;
}

static void start(void) {
  static const int8_t SPAWN[4][NMAX][3] = {
    {{9, 18, 3}, {30, 8, 1}},
    {{6, 18, 3}, {33, 8, 1}, {24, 22, 2}},
    {{9, 6, 0}, {33, 7, 1}, {7, 20, 3}, {31, 21, 2}},
    {{9, 5, 0}, {30, 5, 1}, {9, 22, 3}, {30, 22, 2}, {15, 13, 1}, {24, 14, 3}}};
  const int8_t(*sp)[3] = SPAWN[np == 2 ? 0 : np == 3 ? 1 : np == 4 ? 2 : 3];
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) grid[y][x] = 0;
  for (int p = 0; p < np; p++) {
    s[p].dir = sp[p][2], s[p].len = 4, s[p].hd = 3, s[p].grow = 0, s[p].turns = 0, s[p].dead_t = -1;
    for (int i = 0; i < 4; i++) {
      int x = sp[p][0] - DX[s[p].dir] * (3 - i), y = sp[p][1] - DY[s[p].dir] * (3 - i);
      s[p].cell[i] = (uint16_t)at(x, y), grid[y][x] = (uint8_t)(p + 1);
    }
  }
  for (int k = 0; k < NSTAR; k++) star[k] = -1;
  for (int k = 0; k < NSTAR; k++) place_star(k);
  next_t = 0;
}

static int tail_of(int p) { return (s[p].hd - s[p].len + 1 + MAXLEN) % MAXLEN; }
static void clear_snake(int p) {
  for (int i = 0; i < s[p].len; i++) {
    int c = s[p].cell[(tail_of(p) + i) % MAXLEN];
    if (grid[c / GW][c % GW] == p + 1) grid[c / GW][c % GW] = 0;
  }
  s[p].len = 0;
}

static void move(void) {
  int nx[NMAX], ny[NMAX];
  bool dies[NMAX] = {0};
  for (int p = 0; p < np; p++) {
    if (gone[p]) continue;
    if (s[p].turns) s[p].dir = (s[p].dir + 1) & 3, s[p].turns--;
    int h = s[p].cell[s[p].hd];
    nx[p] = h % GW + DX[s[p].dir], ny[p] = h / GW + DY[s[p].dir];
    bool grows = s[p].grow > 0 && s[p].len < MAXLEN - 1;
    if (s[p].grow) s[p].grow--;
    if (grows) s[p].len++;
    else { /* the tail moves on first */
      int t = s[p].cell[tail_of(p)];
      if (grid[t / GW][t % GW] == p + 1) grid[t / GW][t % GW] = 0;
    }
  }
  for (int p = 0; p < np; p++) {
    if (gone[p]) continue;
    dies[p] = nx[p] < 0 || ny[p] < 0 || nx[p] >= GW || ny[p] >= GH || grid[ny[p]][nx[p]];
    for (int q = 0; q < np; q++)
      if (q != p && !gone[q] && nx[q] == nx[p] && ny[q] == ny[p]) dies[p] = true; /* head to head */
  }
  for (int p = 0; p < np; p++) {
    if (gone[p]) continue;
    if (dies[p]) {
      out(p), s[p].dead_t = rt;
      s[p].len--; /* (its tail had moved on, or it hadn't grown a head) */
      continue;
    }
    s[p].hd = (s[p].hd + 1) % MAXLEN, s[p].cell[s[p].hd] = (uint16_t)at(nx[p], ny[p]), grid[ny[p]][nx[p]] = (uint8_t)(p + 1);
    for (int k = 0; k < NSTAR; k++)
      if (star[k] == at(nx[p], ny[p])) s[p].grow += 3, place_star(k);
  }
}

static void step(void) {
  for (int p = 0; p < np; p++) {
    if (tap[p] && s[p].turns < 2) s[p].turns++;
    if (s[p].dead_t >= 0 && s[p].len && rt - s[p].dead_t > 900) clear_snake(p);
  }
  if (rt >= next_t) {
    int period = 115 - (rt < 55000 ? rt / 1000 : 55);
    next_t = rt + period;
    if (!over()) move();
  }
}

static void draw(void) {
  int r0 = clampi((gfx_y0 - Y0) / CS, 0, GH), r1 = clampi((gfx_y1 - Y0) / CS + 1, 0, GH);
  gfx_fill(0, TOP, SCREEN_W, SCREEN_H - TOP, HEX(0x15382A));
  for (int r = r0; r < r1; r++)
    for (int c = (r & 1); c < GW; c += 2) gfx_fill(X0 + c * CS, Y0 + r * CS, CS, CS, HEX(0x184031));
  for (int k = 0; k < NSTAR; k++) {
    if (star[k] < 0) continue;
    float x = X0 + star[k] % GW * CS + 4, y = Y0 + star[k] / GW * CS + 4, a = rt * 0.002f;
    for (int i = 0; i < 5; i++) {
      float b = a + i * 2 * PI / 5, p[6] = {x + fcos(b) * 7.5f, y + fsin(b) * 7.5f, x + fcos(b + 2.2f) * 3, y + fsin(b + 2.2f) * 3,
                                            x + fcos(b - 2.2f) * 3, y + fsin(b - 2.2f) * 3};
      poly(p, 3, GOLD);
    }
    disc(x, y, 3, HEX(0xFFE89A));
  }
  for (int r = r0; r < r1; r++)
    for (int c = 0; c < GW; c++) {
      int v = grid[r][c];
      if (!v) continue;
      int p = v - 1, x = X0 + c * CS, y = Y0 + r * CS;
      if (s[p].dead_t >= 0 && ((rt - s[p].dead_t) / 120 & 1)) continue; /* blinks out */
      bool rgt = c + 1 < GW && grid[r][c + 1] == v, dn = r + 1 < GH && grid[r + 1][c] == v;
      gfx_fill(x + 1, y + 1, 6 + 2 * rgt, 6 + 2 * dn, col[p]);
      gfx_fill(x + 2, y + 2, 3, 3, lite[p]);
    }
  for (int p = 0; p < np; p++) {
    if (!s[p].len) continue;
    int h = s[p].cell[s[p].hd];
    float x = X0 + h % GW * CS + 4, y = Y0 + h / GW * CS + 4, a = s[p].dir * PI / 2;
    disc(x, y, 6, col[p]);
    eyes(x, y, 6, a);
    tag_at(p, x, y - 5);
  }
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x15382A));
  gfx_fill(x + 8, y + 8, 22, 5, col[0]), gfx_fill(x + 25, y + 8, 5, 14, col[0]);
  gfx_fill(x + 26, y + 25, 22, 5, col[1]), gfx_fill(x + 43, y + 14, 5, 16, col[1]);
  disc(x + 45, y + 13, 4, col[1]), disc(x + 28, y + 23, 4, col[0]);
  disc(x + 12, y + 25, 3, GOLD);
}

#ifdef HOST
static bool free_cell(int x, int y) { return x >= 0 && y >= 0 && x < GW && y < GH && !grid[y][x]; }
static int bot(int p) {
  if (s[p].turns) return bot_tap(p, false);
  int h = s[p].cell[s[p].hd], x = h % GW, y = h / GW, d = s[p].dir, r = (d + 1) & 3;
  bool ahead = free_cell(x + DX[d], y + DY[d]) && free_cell(x + 2 * DX[d], y + 2 * DY[d]);
  bool right = free_cell(x + DX[r], y + DY[r]) && free_cell(x + 2 * DX[r], y + 2 * DY[r]);
  bool want = (!ahead && right) || (right && rn(14) == 0);
  for (int k = 0; k < NSTAR && !want; k++) /* a star on its right */
    if (star[k] >= 0 && right) {
      int sx = star[k] % GW - x, sy = star[k] / GW - y;
      want = (DX[r] && sx * DX[r] > 0 && sy == 0) || (DY[r] && sy * DY[r] > 0 && sx == 0);
    }
  return bot_tap(p, want);
}
#endif

const game_t g_snakes = {"Snakes", "Snakes", "Tap  turn right",
                         "Your snake never stops. Tap your key to turn right. Stars make you longer. Hit a wall or a "
                         "snake, yours too, and you're out: the last snake left takes the round.",
                         1, start, step, draw, icon BOT(bot)};
