/* 2 Players: Pong and more games for two people on one calculator.
 *
 * Player 1 holds the left of the keyboard (the arrows, and OK to act), player 2 the right (the keys
 * around +, and EXE to act), both at once; or player 2 is the computer, easy, normal or hard. Pong
 * plays as the 1972 arcade game does; then Air Hockey, Tanks in a maze with shells that bounce off
 * the walls, Light Cycles, and Bobydeluxe's party games. A match is first to a few points, and the
 * matches each side has won are kept for every game.
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and
 * saves through its file functions, like Block Blast; on its own (TwoPlayers.nwa) it brings them
 * along. Positions are in 256ths of a pixel and time in milliseconds, but for the round things
 * of Air Hockey and the party games, in pixels and seconds. */
#include <eadk.h>
#include <limits.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"
#ifdef __TICE__
#include <string.h>
#include <wchar.h>
#endif

#if NP_MODULE /* (np_file_read and np_file_write: NumPlay's, launcher/src/np.h) */
#define np_app_begin()
#define np_app_end() 0
#else
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "2 Players";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

#define WHITE 0xFFFF
#define INK HEX(0x161A33) /* the menus' night blue */
#define GOLD HEX(0xFFC93C)

/* (in the menu's order, and the saves': a new game goes last) */
enum { PONG, HOCKEY, TANKS, CYCLES, QUICK, TUG, NG };
static const char *const NAME[NG] = {T("Pong"), T("Air Hockey"), T("Tanks"), T("Light Cycles"), T("Quick Draw"),
                                     T("Tug of War")};
static const uint8_t TARGET[NG] = {11, 7, 5, 5, 5, 5}; /* first to */
/* the keys each game uses, for its keyboard: left 1, up 2, down 4, right 8, the action 16 */
static const uint8_t KEYS_USED[NG] = {6, 15, 31, 15, 16, 16};
static const char *const RULES[NG] = {
  T("Return the ball with your paddle: its middle sends the ball back straight, its ends at a steep angle. The longer a rally lasts, the faster the ball."),
  T("Knock the puck into the other goal. Your mallet stays in your half; hit the puck on the move to send it flying."),
  T("Drive through the maze and hit the other tank. Shells bounce off the walls: watch out for your own!"),
  T("Steer your light cycle and make the other one crash into a wall or a trail. It never stops and never turns back."),
  T("Wait for DRAW!, then press your key: the first one wins. Too soon loses the round."),
  T("Tap your key fast to pull the rope. Pull the flag past your line."),
};

/* twoplayer.sav: the game last chosen, who player 2 is, and the matches won (version 1 had four
   games: a shorter file of the same layout, read as it is) */
static struct {
  uint8_t magic, version, game, cpu; /* cpu: 0 a second player, 1 to 3 the computer, easy to hard */
  uint16_t wins[NG][2][2];           /* [game][against the computer][player 1, player 2] */
} sv;
#define SAVE_NAME "twoplayer.sav"

static color_t strip[SCREEN_W * STRIP_H];
static color_t team[2], light[2], dark[2]; /* player 1 red, player 2 blue */
static uint32_t now, seed, phase_t;        /* now: the game's clock, stopped while paused */
static int ms;                             /* this frame's milliseconds */
static float dt;                           /* ...in seconds */
static int game, cpu, score[2], phase, winner; /* phase: 0 playing, 1 a point just won, 2 the match over */
static int pad[2], tap[2];                 /* each player's keys held, and just pressed */
enum { B_LEFT = 1, B_UP = 2, B_DOWN = 4, B_RIGHT = 8, B_FIRE = 16 };
/* the keys: left, up, down, right, action */
static const uint8_t KEYS[2][5] = {
  {eadk_key_left, eadk_key_up, eadk_key_down, eadk_key_right, eadk_key_ok},
  {eadk_key_three, eadk_key_multiplication, eadk_key_ans, eadk_key_minus, eadk_key_exe}};

static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  return seed ^= seed << 5;
}
static int rn(int n) { return (int)(rnd() % (uint32_t)n); }
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float len(float x, float y) { return __builtin_sqrtf(x * x + y * y); }
static uint32_t ago(uint32_t t) { return now - t; }
static bool due(uint32_t t) { return (int32_t)(now - t) >= 0; }
static bool down(uint64_t keys, int k) { return (uint32_t)(keys >> (k & 32)) >> (k & 31) & 1; }
static bool is_cpu(int p) { return p && cpu; }
static int dirx(int p) { return !!(pad[p] & B_RIGHT) - !!(pad[p] & B_LEFT); }
static int diry(int p) { return !!(pad[p] & B_DOWN) - !!(pad[p] & B_UP); }
static int acted(int p) { return tap[p] >> 4 & 1; } /* the action key just pressed */
/* sine of a 64th of a turn, times 127 */
static const int8_t SINQ[17] = {0, 12, 25, 37, 49, 60, 71, 81, 90, 98, 106, 112, 117, 122, 125, 126, 127};
static int sin64(int a) {
  int q = a & 15, v = a & 16 ? SINQ[16 - q] : SINQ[q];
  return a & 32 ? -v : v;
}
static int cos64(int a) { return sin64(a + 16); }
/* The computer sees and thinks at a human's pace: it decides again every so often, not every
   frame, and holds its keys in between. */
static struct {
  uint32_t next;
  float x, y;
} bot;
static bool bot_due(void) {
  static const uint16_t EVERY[4] = {0, 340, 240, 160};
  if (!due(bot.next)) return false;
  bot.next = now + EVERY[cpu] + rn(EVERY[cpu] / 2);
  return true;
}

static char *num(char *o, uint32_t v) { /* v in o, then the end of it */
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
/* text with a shadow, to read over anything */
static void say(const np_font_t *f, int cx, int base, const char *s, color_t c) {
  gfx_text_center(f, cx + 1, base + 2, s, 0, 150);
  gfx_text_center(f, cx, base, s, c, 256);
}
static const char *who(int p) {
  return p ? (cpu ? T("Computer") : T("Player 2")) : (cpu ? T("You") : T("Player 1"));
}
static void box(int x, int y, int w, int h, int r, color_t c) { gfx_rrect(x, y, w, h, r, c, 256); }
static void disc(int x, int y, int r, color_t c) { gfx_circle(x, y, r, c, 256); }
#ifdef __TICE__
/* Something that moves about, drawn by fn(a) in the box x, y, w, h (on: there at all), on the TI-84 Plus CE: one
   call in the list of every strip from row y0 to y1, there or not, so the strips' lists keep their length as it
   goes from one to another and only the columns it left and came to are drawn again (a list changing length is
   drawn again whole) */
static void moving(void (*fn)(const int *a), const int *a, int n, int x, int y, int w, int h, int y0, int y1, bool on) {
  static const int none[8];
  if (!gfx_listing) {
    if (on) fn(a);
    return;
  }
  for (int s = y0 & ~15; s < y1; s += 16) {
    bool in = on && y < s + 16 && y + h > s;
    gfx_call(fn, in ? a : none, n, in ? x : 0, s, in ? w : 0, 16);
  }
}
#endif

/* A match: points, then the next point or round once the last one has been seen. */
static void point(int w) {
  if (phase) return;
  winner = w, phase = 1, phase_t = now;
  if (w < 2) score[w]++;
}

/* ------------------------------------------------------------ Pong
   The 1972 arcade game: a paddle has eight segments, the two in the middle send the ball back
   straight, the others at steeper and steeper angles; the ball goes 25% faster after 4 hits,
   50% after 12, and a little faster with every hit after that, until a point is won. */
#define PH 30  /* a paddle */
#define PW 6
#define BS 6   /* the ball, square */
#define PTOP 8 /* the walls */
#define PBOT 232
static const int PX[2] = {24, SCREEN_W - 24 - PW};
static const int8_t SEG[8] = {-10, -7, -4, 0, 0, 4, 7, 10}; /* tenths of the speed, up or down */
static int py[2], bx, by, bvx, bvy, aim, hits;
static uint32_t serve_at; /* the ball waits in the middle until then (0: in play) */

/* the ball's speed across, in 256ths of a pixel a millisecond (38: 150 pixels a second) */
static int pong_speed(void) { return hits >= 12 ? clampi(46 + hits, 58, 84) : hits >= 4 ? 48 : 38; }
static void pong_start(void) { /* the ball is served towards the one who lost the point */
  int to = winner < 2 ? 1 - winner : rn(2);
  hits = aim = 0;
  bx = (160 - BS / 2) << 8, by = (70 + rn(100)) << 8, bvx = to ? 1 : -1, bvy = rn(9) - 4;
  serve_at = now + 900, bot.y = 120 << 8;
  if (!score[0] && !score[1]) py[0] = py[1] = (120 - PH / 2) << 8;
}
/* the computer's paddle (player 2): to where it sees the ball will come, off centre by its aim
   and by how much it misjudges, slower than a player's */
static int pong_cpu(void) {
  static const uint8_t SPEED[4] = {0, 20, 30, 40}, MISS[4] = {0, 20, 12, 7};
  static const int16_t SEES[4] = {0, 190, 145, 90}; /* where the ball must be before it looks */
  if (bot_due()) {
    int to = 120 << 8;
    if (bvx > 0 && !serve_at && bx > SEES[cpu] << 8) {
      int span = (PBOT - BS - PTOP) << 8, y = by - (PTOP << 8) + bvy * (((PX[1] - BS) << 8) - bx) / bvx;
      for (int i = 0; i < 8 && (y < 0 || y > span); i++) y = y < 0 ? -y : 2 * span - y;
      to = y + ((PTOP + BS / 2) << 8) - aim;
    }
    bot.y = (float)(to + (rn(2 * MISS[cpu] + 1) - MISS[cpu]) * 256);
  }
  return clampi(((int)bot.y - py[1] - (PH << 7)) / 40, -SPEED[cpu], SPEED[cpu]);
}
static void pong_step(void) {
  for (int p = 0; p < 2; p++)
    py[p] = clampi(py[p] + (is_cpu(p) ? pong_cpu() : diry(p) * 64) * ms, PTOP << 8, (PBOT - PH) << 8);
  if (phase || (int32_t)(now - serve_at) < 0) return;
  if (serve_at) serve_at = 0, bvx *= pong_speed(), bvy = bvy * pong_speed() / 10;
  for (int i = 0; i < ms; i++) {
    bx += bvx, by += bvy;
    if (by < PTOP << 8 || by > (PBOT - BS) << 8) by -= 2 * bvy, bvy = -bvy;
    int p = bvx > 0, x = bx >> 8, y = by >> 8, top = py[p] >> 8;
    if (x + BS > PX[p] && x < PX[p] + PW && y + BS > top && y < top + PH) {
      int s = (++hits, pong_speed());
      bvx = p ? -s : s, bvy = SEG[clampi((y + BS - top) * 8 / (PH + BS), 0, 7)] * s / 10;
      bx = (p ? PX[p] - BS : PX[p] + PW) << 8;
      /* where the computer means to take the next one: off centre, by how much it misjudges */
      if (!p) {
        aim = rn(1536) - 768;
        aim = cpu == 3 ? aim + (rn(2) ? 2816 : -2816) : aim / 3 * (cpu == 1 ? 25 : 14);
      }
    }
    if (x < -BS - 10 || x > SCREEN_W + 10) {
      point(x < 0);
      return;
    }
  }
}
/* the arcade game's digits: three by five blocks */
#define D(a, b, c, d, e) ((a) << 12 | (b) << 9 | (c) << 6 | (d) << 3 | (e))
static const uint16_t DIGIT[10] = {D(7, 5, 5, 5, 7), D(1, 1, 1, 1, 1), D(7, 1, 7, 4, 7), D(7, 1, 7, 1, 7),
                                   D(5, 5, 7, 1, 1), D(7, 4, 7, 1, 7), D(7, 4, 7, 5, 7), D(7, 1, 1, 1, 1),
                                   D(7, 5, 7, 5, 7), D(7, 5, 7, 1, 7)};
static void pong_paint(void) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, 0);
  gfx_fill(0, PTOP - 4, SCREEN_W, 4, WHITE);
  gfx_fill(0, PBOT, SCREEN_W, 4, WHITE);
  for (int y = PTOP + 4; y < PBOT; y += 16) gfx_fill(158, y, 4, 8, WHITE);
  gfx_text_center(&np_font_small, 80, 227, T("Back  pause"), HEX(0x6E6E6E), 256); /* (under the paddles and the ball) */
  for (int p = 0; p < 2; p++) {
    gfx_fill(PX[p], py[p] >> 8, PW, PH, WHITE);
    for (int v = score[p], n = v > 9, d = n; d >= 0; d--, v /= 10)
      for (int b = 0; b < 15; b++)
        if (DIGIT[v % 10] >> (14 - b) & 1) gfx_fill(p * 100 + 103 - n * 10 + d * 20 + b % 3 * 5, 18 + b / 3 * 5, 5, 5, WHITE);
  }
  if (!phase && (!serve_at || ago(serve_at) % 300 < 200)) gfx_fill(bx >> 8, by >> 8, BS, BS, WHITE);
}

/* ------------------------------------------------------------ Air Hockey */
#define RX0 10 /* the table */
#define RX1 310
#define RY0 30
#define RY1 232
#define RYM 131
#define GY0 103 /* the goals */
#define GY1 159
#define MR 13 /* a mallet */
#define QR 9  /* the puck */
static float mx[2], my[2], mvx[2], mvy[2], qx, qy, qvx, qvy, qaim;

#ifdef __TICE__
static void hk_keep(void);
#endif
static void hockey_start(void) { /* the puck to the one who was scored on */
  int to = winner < 2 ? 1 - winner : rn(2);
  for (int p = 0; p < 2; p++) mx[p] = p ? RX1 - 40 : RX0 + 40, my[p] = RYM, mvx[p] = mvy[p] = 0;
  qx = to ? 220 : 100, qy = RYM, qvx = qvy = 0, qaim = (float)(rn(44) - 22); /* where the computer aims */
#ifdef __TICE__
  hk_keep();
#endif
  bot.x = RX1 - 40, bot.y = RYM;
}
/* the puck off something round at (x, y) moving at (vx, vy) */
static void knock(float x, float y, float vx, float vy, float r) {
#if NP_SOFT_FLOAT /* (float done in software: no square root while they're apart) */
  float ex = qx - x, ey = qy - y;
  if (ex * ex + ey * ey >= (r + QR) * (r + QR)) return;
#endif
  float dx = qx - x, dy = qy - y, d = len(dx, dy), R = r + QR;
  if (d >= R || d < 0.01f) return;
  dx /= d, dy /= d;
  qx = x + dx * R, qy = y + dy * R;
  float vn = (qvx - vx) * dx + (qvy - vy) * dy;
  if (vn < 0) qvx -= 1.85f * vn * dx, qvy -= 1.85f * vn * dy;
}
/* the computer's mallet (player 2): behind the puck to shoot it at the other goal, or guarding
   its own, where it saw them last; its speed (tx, ty) */
static void hockey_cpu(float *tx, float *ty) {
  static const float V[4] = {0, 90, 130, 175};
  static const uint8_t MISS[4] = {0, 18, 11, 6};
  if (bot_due()) {
    float x = RX1 - 36, y = RYM + (qy - RYM) / 2;
    if (qx > 160 - QR && !phase) {
      float ax = RX0 - qx, ay = RYM + qaim - qy, l = len(ax, ay);
      ax /= l, ay /= l;
      x = qx - ax * (MR + QR + 3), y = qy - ay * (MR + QR + 3);
      if (mx[1] < qx + 2) x = qx + MR + QR + 10, y = qy + (qy > RYM ? -28 : 28); /* in front of it: go round */
      else if (len(x - mx[1], y - my[1]) < 7) x = qx + ax * 24, y = qy + ay * 24; /* strike */
      if (x > RX1 - MR || y < RY0 + MR || y > RY1 - MR) x = qx, y = qy; /* it can't get behind it: at it */
    }
    bot.x = x, bot.y = y + (float)(rn(2 * MISS[cpu] + 1) - MISS[cpu]);
  }
  float x = bot.x - mx[1], y = bot.y - my[1], l = len(x, y) + 0.01f, v = V[cpu] * (l < 16 ? l / 16 : 1) / l;
  *tx = x * v, *ty = y * v;
}
#if NP_SOFT_FLOAT
/* The puck's millisecond steps where float is done in software (the TI-84 Plus CE, a few hundred cycles a sum):
   its place and speed in 4096ths of a pixel, the floats' steps only within reach of a post or a mallet, at a
   board or past a goal line. False: a point was just won. */
#define FX 4096
static int fx(float v) { return (int)(v * FX + (v < 0 ? -.5f : .5f)); }
static bool in_reach(int d, int r) { return d < r && d > -r; } /* |d| < r */
static bool puck_steps(void) {
  int x = fx(qx), y = fx(qy), vx = fx(qvx / 1000), vy = fx(qvy / 1000), bx[2], by[2];
  for (int p = 0; p < 2; p++) bx[p] = fx(mx[p]), by[p] = fx(my[p]);
  for (int i = 0; i < ms; i++) {
    x += vx, y += vy;
    /* (within reach of a post, then of a mallet: the far ones first, the CE's compiler making slow work of the rest) */
    bool near = (in_reach(x - RX0 * FX, QR * FX) || in_reach(x - RX1 * FX, QR * FX)) &&
                (in_reach(y - GY0 * FX, QR * FX) || in_reach(y - GY1 * FX, QR * FX));
    for (int p = 0; p < 2 && !near; p++) near = in_reach(x - bx[p], (MR + QR) * FX) && in_reach(y - by[p], (MR + QR) * FX);
    if (near) { /* (as the float steps: the posts, the mallets, and a mallet pressing it into a corner giving way) */
      qx = (float)x / FX, qy = (float)y / FX;
      for (int k = 0; k < 4; k++) knock(k & 1 ? RX1 : RX0, k & 2 ? GY1 : GY0, 0, 0, 0);
      for (int p = 0; p < 2; p++) knock(mx[p], my[p], mvx[p], mvy[p], MR);
      x = fx(qx), y = fx(qy), vx = fx(qvx / 1000), vy = fx(qvy / 1000);
    }
    if (y < (RY0 + QR) * FX) y = (RY0 + QR) * FX, qvy = qvy < 0 ? -qvy * 0.9f : qvy, vy = fx(qvy / 1000);
    else if (y > (RY1 - QR) * FX) y = (RY1 - QR) * FX, qvy = qvy > 0 ? -qvy * 0.9f : qvy, vy = fx(qvy / 1000);
    if (y < GY0 * FX || y > GY1 * FX) {
      if (x < (RX0 + QR) * FX) x = (RX0 + QR) * FX, qvx = qvx < 0 ? -qvx * 0.9f : qvx, vx = fx(qvx / 1000);
      else if (x > (RX1 - QR) * FX) x = (RX1 - QR) * FX, qvx = qvx > 0 ? -qvx * 0.9f : qvx, vx = fx(qvx / 1000);
    }
    if (x < (RX0 - QR) * FX || x > (RX1 + QR) * FX) {
      qx = (float)x / FX, qy = (float)y / FX;
      point(x < 160 * FX);
      return false;
    }
    for (int p = 0; p < 2; p++)
      if (in_reach(x - bx[p], (MR + QR) * FX) && in_reach(y - by[p], (MR + QR) * FX)) {
        qx = (float)x / FX, qy = (float)y / FX;
        float dx = mx[p] - qx, dy = my[p] - qy, d = len(dx, dy) + .01f;
        if (d < MR + QR) mx[p] = qx + dx / d * (MR + QR), my[p] = qy + dy / d * (MR + QR), bx[p] = fx(mx[p]), by[p] = fx(my[p]);
      }
  }
  qx = (float)x / FX, qy = (float)y / FX;
  return true;
}
#endif
static void hockey_step(void) {
  for (int p = 0; p < 2; p++) {
    float tx, ty, k = clampf(ms * 0.014f, 0, 1);
    if (is_cpu(p)) hockey_cpu(&tx, &ty);
    else {
      int x = dirx(p), y = diry(p);
      float s = x && y ? 175 : 245;
      tx = x * s, ty = y * s;
    }
    mvx[p] += (tx - mvx[p]) * k, mvy[p] += (ty - mvy[p]) * k;
    mx[p] = clampf(mx[p] + mvx[p] * ms / 1000, p ? 160 + MR : RX0 + MR, p ? RX1 - MR : 160 - MR);
    my[p] = clampf(my[p] + mvy[p] * ms / 1000, RY0 + MR, RY1 - MR);
  }
  if (phase) return;
#if NP_SOFT_FLOAT
  if (!puck_steps()) return;
#else
  for (int i = 0; i < ms; i++) {
    qx += qvx / 1000, qy += qvy / 1000;
    for (int k = 0; k < 4; k++) knock(k & 1 ? RX1 : RX0, k & 2 ? GY1 : GY0, 0, 0, 0); /* the goal posts */
    for (int p = 0; p < 2; p++) knock(mx[p], my[p], mvx[p], mvy[p], MR);
    /* the boards: back on their edge, and off them only when moving into them (a mallet can push the
       puck into a board, and a slow puck flipped back and forth there stayed in it) */
    if (qy < RY0 + QR) qy = RY0 + QR, qvy = qvy < 0 ? -qvy * 0.9f : qvy;
    else if (qy > RY1 - QR) qy = RY1 - QR, qvy = qvy > 0 ? -qvy * 0.9f : qvy;
    if (qy < GY0 || qy > GY1) {
      if (qx < RX0 + QR) qx = RX0 + QR, qvx = qvx < 0 ? -qvx * 0.9f : qvx;
      else if (qx > RX1 - QR) qx = RX1 - QR, qvx = qvx > 0 ? -qvx * 0.9f : qvx;
    }
    if (qx < RX0 - QR || qx > RX1 + QR) {
      point(qx < 160);
      return;
    }
    for (int p = 0; p < 2; p++) { /* and a mallet pressing it into a corner gives way */
      float dx = mx[p] - qx, dy = my[p] - qy;
      float d = len(dx, dy) + .01f;
      if (d < MR + QR) mx[p] = qx + dx / d * (MR + QR), my[p] = qy + dy / d * (MR + QR);
    }
  }
#endif
  float s = len(qvx, qvy), f = 1 - 0.00035f * ms;
  if (s * f > 560) f = 560 / s;
  qvx *= f, qvy *= f;
}
#define ICE HEX(0xEDF4FA)
#ifdef __TICE__
/* ---- Air Hockey on the TI-84 Plus CE, where a strip a mallet or the puck crosses is drawn again every frame: the
   rink and the pieces drawn from how they look, kept, rather than from their dozen calls and discs each time. The same
   pixels: what's kept is what those calls drew. */
static void rink_calls(void) { /* (the rink as hockey_paint draws it elsewhere) */
  const color_t RED = HEX(0xE25555), BLUE = HEX(0x5C8FE6), RIM = HEX(0x8D9BB0);
  gfx_fill(0, 22, SCREEN_W, SCREEN_H - 22, INK);
  box(RX0 - 5, RY0 - 5, RX1 - RX0 + 10, RY1 - RY0 + 10, 14, RIM);
  box(RX0, RY0, RX1 - RX0, RY1 - RY0, 10, ICE);
  gfx_fill(159, RY0, 3, RY1 - RY0, RED);
  for (int k = 0; k < 3; k++) {
    int c = k == 1, d = 80 - 20 * c;
    gfx_rrect_outline(k * 150 - 30 + 10 * c, RYM - d / 2, d, d, d / 2, 2, c ? RED : BLUE, 220);
  }
  for (int k = 0; k < 2; k++) {
    gfx_fill(k ? RX1 + 5 : 0, RY0, k ? SCREEN_W - RX1 - 5 : RX0 - 5, RY1 - RY0, INK);
    gfx_fill(k ? RX1 : RX0 - 5, RY0, 5, RY1 - RY0, RIM);
    gfx_fill(k ? RX1 : RX0 - 6, GY0, 6, GY1 - GY0, HEX(0x0B0E1C));
  }
}
/* The rink from row RK0 to RK1 (the rest, by its corners, by its calls): a row without the rings or the goals, and
   each row's runs of other pixels (a run: its column in 2 bytes, low then high + 2 * its length, then each pixel's
   colour's place in pal). A puck or a mallet: its shadow (radius R, 2 right and 3 down), its dark disc (R) and its
   middle inside the dark one; each row (from R up to R + 2 down): the pixels the dark disc covers whole, from a to b,
   as all the discs leave them, and the n others the shadow or the dark disc touch, mixed with what's under them
   (each: its column from the left of the piece, the shadow's level, the dark disc's) */
#define RK0 48
#define RK1 208
#define RUNS 2000 /* (1814 used) */
#define MIXES 400 /* (276 used) */
#define WHOLES 1240 /* (1216 used) */
typedef struct {
  int8_t a, b;
  uint8_t n;
  uint16_t mix, whole;
} piece_row_t;
typedef struct {
  color_t row[SCREEN_W], pal[64];
  uint16_t at[RK1 - RK0 + 1];
  uint8_t runs[RUNS];
  piece_row_t piece[2][2 * MR + 3]; /* the puck's, a mallet's */
  uint8_t mix[MIXES][3];
  color_t whole[WHOLES];
  uint16_t whole_at[3]; /* the puck's, player 1's mallet's, player 2's */
} hk_t;
/* (the room Light Cycles' grid takes, Air Hockey's things in it: one game at a time) */
static union {
  hk_t hk;
  struct { /* Light Cycles' grid, and the columns of the cells its computer reaches (room) */
    uint8_t cell[54][80], x[256];
  } lc;
  struct { /* Tanks' rectangles at every angle (quad_rows) */
    uint16_t at[64][3], used;
    int8_t rows[7600];
  } tq;
} shared;
static enum { BY_NONE, BY_HOCKEY, BY_CYCLES, BY_TANKS } shared_by;
static bool hk_ok;
#define GOAL HEX(0x0B0E1C)
static bool rink_ok;
static int rink_next, rink_pal; /* (the next row to keep, the colours kept) */
static bool goal_col(int x) { return (x >= RX0 - 6 && x < RX0) || (x >= RX1 && x < RX1 + 6); }
/* the rows of the strip just drawn by rink_calls, kept: the first strip from RK0 gives the plain row (its row 60, with
   no ring and no goal) */
static void rink_keep(void) {
  hk_t *h = &shared.hk;
  unsigned used = rink_next == RK0 ? 0 : h->at[rink_next - RK0];
  if (rink_next == RK0) memcpy(h->row, gfx_buf + (60 - RK0) * SCREEN_W, sizeof h->row), rink_pal = 0;
  for (int r = 0; r < STRIP_H; r++) {
    int y = rink_next + r;
    bool goal = y >= GY0 && y < GY1;
    const color_t *p = gfx_buf + r * SCREEN_W;
    h->at[y - RK0] = (uint16_t)used;
    if (!goal && !memcmp(p, h->row, sizeof h->row)) continue;
    for (int x = 0; x < SCREEN_W;) {
      if (p[x] == (goal && goal_col(x) ? GOAL : h->row[x])) {
        x++;
        continue;
      }
      int x0 = x;
      while (x < SCREEN_W && x - x0 < 127 && p[x] != (goal && goal_col(x) ? GOAL : h->row[x])) x++;
      if (used + 2 + (unsigned)(x - x0) > RUNS) {
        rink_next = RK1 + 1; /* (no room: drawn by its calls) */
        return;
      }
      h->runs[used++] = (uint8_t)x0, h->runs[used++] = (uint8_t)(x0 >> 8 | (x - x0) << 1);
      for (int i = x0; i < x; i++) {
        int k = 0;
        while (k < rink_pal && h->pal[k] != p[i]) k++;
        if (k == rink_pal && rink_pal++ == 64) {
          rink_next = RK1 + 1;
          return;
        }
        h->pal[k] = p[i], h->runs[used++] = (uint8_t)k;
      }
    }
  }
  h->at[rink_next + STRIP_H - RK0] = (uint16_t)used;
  if ((rink_next += STRIP_H) == RK1) rink_ok = true;
}
static void rink_at(const int *a) { /* the rink over the rows being drawn, in the columns being drawn */
  hk_t *h = &shared.hk;
  if (!rink_ok || gfx_y0 < RK0 || gfx_y1 > RK1) {
    rink_calls();
    if (!rink_ok && gfx_y0 == rink_next && gfx_x0 == 0 && gfx_x1 == SCREEN_W) rink_keep();
    return;
  }
  int x0 = gfx_x0, x1 = gfx_x1;
  color_t *p = gfx_buf;
  for (int y = gfx_y0; y < gfx_y1; y++, p += SCREEN_W) {
    memcpy(p + x0, h->row + x0, (size_t)(x1 - x0) * sizeof(color_t));
    if (y >= GY0 && y < GY1) /* (the goals) */
      for (int k = 0; k < 2; k++) {
        int g0 = k ? RX1 : RX0 - 6, g1 = g0 + 6;
        for (int x = x0 > g0 ? x0 : g0, e = x1 < g1 ? x1 : g1; x < e; x++) p[x] = GOAL;
      }
    for (const uint8_t *e = h->runs + h->at[y - RK0], *end = h->runs + h->at[y - RK0 + 1]; e < end;) {
      int x = e[0] | (e[1] & 1) << 8, n = e[1] >> 1;
      if (x >= x1) break; /* (the runs from left to right) */
      if (x + n > x0)
        for (int i = x < x0 ? x0 - x : 0, i1 = x + n > x1 ? x1 - x : n; i < i1; i++) p[x + i] = h->pal[e[2 + i]];
      e += 2 + n;
    }
  }
}
/* the puck's (kind 0) or a mallet's (kind 1, player p's) discs inside the dark one, at (x, y) */
static void piece_middle(int kind, int p, int x, int y) {
  if (!kind) {
    disc(x, y, QR - 3, HEX(0x3B3F4C));
    return;
  }
  disc(x, y, MR - 3, team[p]);
  disc(x, y, 6, dark[p]);
  disc(x - 1, y - 1, 4, light[p]);
}
static color_t piece_dark(int kind, int p) { return kind ? dark[p] : HEX(0x22252E); }
static bool piece_keep(int kind, unsigned *mixes, unsigned *wholes) {
  hk_t *h = &shared.hk;
  int R = kind ? MR : QR, W = 2 * R + 2, sets = kind ? 2 : 1;
  static uint8_t level[64], l0[STRIP_H][2 * MR + 2], l1[STRIP_H][2 * MR + 2]; /* (a level from the green on black) */
  for (int t = 0; t <= 32; t++) level[gfx_mix(0, 0x07E0, t) >> 5 & 63] = (uint8_t)t;
  gfx_y0 = 0, gfx_y1 = STRIP_H;
  unsigned width = 0; /* (its whole pixels, for one set of colours) */
  for (int top = -R; top < R + 3; top += STRIP_H) { /* its rows top to top + 15 in the strip's: their shape */
    for (int pass = 0; pass < 2; pass++) { /* the shadow's levels, the dark disc's (the middle stays inside it) */
      gfx_fill(0, 0, W, STRIP_H, 0);
      disc(R + 2 * !pass, -top + 3 * !pass, R, 0x07E0);
      for (int r = 0; r < STRIP_H; r++)
        for (int i = 0; i < W; i++) (pass ? l1 : l0)[r][i] = level[gfx_buf[r * SCREEN_W + i] >> 5 & 63];
    }
    for (int r = 0; r < STRIP_H && top + r < R + 3; r++) {
      piece_row_t *pr = &h->piece[kind][top + r + R];
      int a = W, b = -1;
      pr->mix = (uint16_t)*mixes, pr->n = 0;
      for (int i = 0; i < W; i++)
        if (l1[r][i] >= 32) {
          a = a < i ? a : i, b = i;
        } else if (l0[r][i] || l1[r][i]) {
          if (*mixes == MIXES) return false;
          h->mix[*mixes][0] = (uint8_t)i, h->mix[*mixes][1] = l0[r][i], h->mix[(*mixes)++][2] = l1[r][i], pr->n++;
        }
      pr->a = (int8_t)a, pr->b = (int8_t)b, pr->whole = (uint16_t)width;
      if (a <= b) width += (unsigned)(b - a + 1);
    }
  }
  if (*wholes + width * (unsigned)sets > WHOLES) return false;
  for (int set = 0; set < sets; set++) h->whole_at[kind + set] = (uint16_t)(*wholes + width * (unsigned)set);
  *wholes += width * (unsigned)sets;
  for (int top = -R; top < R + 3; top += STRIP_H) /* its pixels covered whole, as the discs leave them */
    for (int set = 0; set < sets; set++) {
      gfx_fill(0, 0, W, STRIP_H, 0);
      disc(R + 2, -top + 3, R, gfx_mix(ICE, 0, 10)), disc(R, -top, R, piece_dark(kind, set));
      piece_middle(kind, set, R, -top);
      for (int r = 0; r < STRIP_H && top + r < R + 3; r++) {
        const piece_row_t *pr = &h->piece[kind][top + r + R];
        for (int i = pr->a; i <= pr->b; i++) h->whole[h->whole_at[kind + set] + pr->whole + (i - pr->a)] = gfx_buf[r * SCREEN_W + i];
      }
    }
  return true;
}
static void hk_keep(void) { /* (between frames, in the strip) */
  if (shared_by == BY_HOCKEY) return;
  shared_by = BY_HOCKEY;
  int y0 = gfx_y0, y1 = gfx_y1;
  unsigned mixes = 0, wholes = 0;
  hk_ok = piece_keep(0, &mixes, &wholes) && piece_keep(1, &mixes, &wholes);
  rink_ok = false, rink_next = RK0; /* (kept as the next frame draws it whole) */
  gfx_y0 = y0, gfx_y1 = y1;
}
/* piece kind (0 the puck, 1 player p's mallet) at (x, y) over the rows being drawn; false: not there (off the screen's
   sides) */
color_t gfx_ce_mix(color_t a, color_t b, unsigned t); /* (gfx_mix's own: platform/ti84ce/gfx_ce.c) */
static bool piece_at(int kind, int p, int x, int y) {
  hk_t *h = &shared.hk;
  int R = kind ? MR : QR;
  if (!hk_ok || x - R < 0 || x + R + 2 > SCREEN_W) return false;
  color_t S = gfx_mix(ICE, 0, 10), D = piece_dark(kind, p);
  const color_t *whole = h->whole + h->whole_at[kind + p];
  int k = gfx_y0 - (y - R), k1 = gfx_y1 - (y - R);
  k = k < 0 ? 0 : k, k1 = k1 > 2 * R + 3 ? 2 * R + 3 : k1;
  for (color_t *line = gfx_buf + (y - R + k - gfx_y0) * SCREEN_W + x - R; k < k1; k++, line += SCREEN_W) {
    const piece_row_t *pr = &h->piece[kind][k];
    if (pr->a <= pr->b) memcpy(line + pr->a, whole + pr->whole, (size_t)(pr->b - pr->a + 1) * sizeof(color_t));
    for (const uint8_t(*m)[3] = h->mix + pr->mix, (*e)[3] = m + pr->n; m < e; m++) {
      color_t q = line[(*m)[0]];
      if ((*m)[1]) q = (*m)[1] >= 32 ? S : gfx_ce_mix(q, S, (*m)[1]);
      if ((*m)[2]) q = gfx_ce_mix(q, D, (*m)[2]);
      line[(*m)[0]] = q;
    }
  }
  return true;
}
static void puck(const int *a) { /* at a[0], a[1] */
  if (!piece_at(0, 0, a[0], a[1]))
    disc(a[0] + 2, a[1] + 3, QR, gfx_mix(ICE, 0, 10)), disc(a[0], a[1], QR, HEX(0x22252E)), disc(a[0], a[1], QR - 3, HEX(0x3B3F4C));
}
static void mallet(const int *a) { /* at a[0], a[1], player a[2]'s */
  int x = a[0], y = a[1], p = a[2];
  if (piece_at(1, p, x, y)) return;
  disc(x + 2, y + 3, MR, gfx_mix(ICE, 0, 10));
  disc(x, y, MR, dark[p]);
  piece_middle(1, p, x, y);
}
#endif
static void hockey_paint(void) {
#ifdef __TICE__
  static const int none[1];
  gfx_call(rink_at, none, 0, 0, 22, SCREEN_W, SCREEN_H - 22); /* (one call a strip) */
#else
  const color_t RED = HEX(0xE25555), BLUE = HEX(0x5C8FE6), RIM = HEX(0x8D9BB0);
  gfx_fill(0, 22, SCREEN_W, SCREEN_H - 22, INK);
  box(RX0 - 5, RY0 - 5, RX1 - RX0 + 10, RY1 - RY0 + 10, 14, RIM);
  box(RX0, RY0, RX1 - RX0, RY1 - RY0, 10, ICE);
  gfx_fill(159, RY0, 3, RY1 - RY0, RED);
  for (int k = 0; k < 3; k++) { /* the creases round the goals, and the circle in the middle */
    int c = k == 1, d = 80 - 20 * c;
    gfx_rrect_outline(k * 150 - 30 + 10 * c, RYM - d / 2, d, d, d / 2, 2, c ? RED : BLUE, 220);
  }
  for (int k = 0; k < 2; k++) { /* the rim over the creases, and the goals in it */
    gfx_fill(k ? RX1 + 5 : 0, RY0, k ? SCREEN_W - RX1 - 5 : RX0 - 5, RY1 - RY0, INK);
    gfx_fill(k ? RX1 : RX0 - 5, RY0, 5, RY1 - RY0, RIM);
    gfx_fill(k ? RX1 : RX0 - 6, GY0, 6, GY1 - GY0, HEX(0x0B0E1C));
  }
#endif
#ifdef __TICE__
  int q[2] = {(int)qx, (int)qy};
  moving(puck, q, 2, q[0] - QR, q[1] - QR, 2 * QR + 2, 2 * QR + 3, RY0, RY1 + 3, !phase);
  for (int p = 0; p < 2; p++) {
    int m[3] = {(int)mx[p], (int)my[p], p};
    moving(mallet, m, 3, m[0] - MR, m[1] - MR, 2 * MR + 2, 2 * MR + 3, RY0, RY1 + 3, true);
  }
#else
  if (!phase) disc((int)qx + 2, (int)qy + 3, QR, gfx_mix(ICE, 0, 10)), disc((int)qx, (int)qy, QR, HEX(0x22252E)),
              disc((int)qx, (int)qy, QR - 3, HEX(0x3B3F4C));
  for (int p = 0; p < 2; p++) {
    int x = (int)mx[p], y = (int)my[p];
    disc(x + 2, y + 3, MR, gfx_mix(ICE, 0, 10));
    disc(x, y, MR, dark[p]);
    disc(x, y, MR - 3, team[p]);
    disc(x, y, 6, dark[p]);
    disc(x - 1, y - 1, 4, light[p]);
  }
#endif
}

/* ------------------------------------------------------------ Tanks
   A maze of 8 x 6 cells: a random one with every cell reached, then a third of its walls left
   knocked down so there are ways round. Walls are 4 pixels thick, on the cells' edges. A tank
   faces one of 64 ways (ta: 256ths of them); shells fly at 124 pixels a second. */
#define TCS 35 /* a cell */
#define TX0 20
#define TY0 26
#define TCOLS 8
#define TROWS 6
#define TR 10 /* a tank, for walls and shells */
#define SHELLS 10
static uint8_t hwall[TROWS + 1][TCOLS], vwall[TROWS][TCOLS + 1];
static int tx_[2], ty_[2], ta[2], ai_aim;
static bool dead[2];
static uint32_t dead_t[2], ai_t, ai_shot;
static struct { int x, y, vx, vy, life, own; } sh[SHELLS]; /* own -1: none */
#ifdef __TICE__
static int maze; /* (a new one each round) */
#endif

static bool H(int r, int c) { return r >= 0 && r <= TROWS && c >= 0 && c < TCOLS && hwall[r][c]; }
static bool V(int r, int c) { return r >= 0 && r < TROWS && c >= 0 && c <= TCOLS && vwall[r][c]; }
static int tcell(int x, int y) { /* the cell at (x, y), in 256ths of a pixel */
  return clampi(((y >> 8) - TY0) / TCS, 0, TROWS - 1) * TCOLS + clampi(((x >> 8) - TX0) / TCS, 0, TCOLS - 1);
}
/* inside a wall (or out of the maze)? */
#if !NP_SOFT_FLOAT
static bool solid(int x, int y) {
  int k = tcell(x, y), r = k / TCOLS, c = k % TCOLS, fx = (x >> 8) - TX0 - c * TCS, fy = (y >> 8) - TY0 - r * TCS;
  bool L = fx < 2, R = fx >= TCS - 2, T = fy < 2, B = fy >= TCS - 2;
  if ((T && H(r, c)) || (B && H(r + 1, c)) || (L && V(r, c)) || (R && V(r, c + 1))) return true;
  /* the corners, where any wall meeting there fills them */
  int gr = r + B, gc = c + R;
  return (L || R) && (T || B) && (H(gr, gc - 1) || H(gr, gc) || V(gr - 1, gc) || V(gr, gc));
}
#else
/* Where division is slow (the TI-84 Plus CE, a shell's or the computer's look at the walls hundreds of times a
   frame), the same answer looked up: a column's cell and part of it (bits 4 and up: its left wall 0, the middle 1,
   its right wall 2), a row's (its top 0, the middle 3, its bottom 6), and each cell's nine parts, in a wall or not */
static uint8_t xzone[SCREEN_W], yzone[SCREEN_H], ways[TROWS * TCOLS]; /* ways: each cell's open sides, open_way's */
static uint16_t zones[TROWS * TCOLS];
static bool open_way(int k, int d);
static bool solid_px(unsigned px, unsigned py) { /* (the pixel's, on the screen) */
  unsigned a = xzone[px], b = yzone[py];
  return zones[(b & 15) * TCOLS + (a & 15)] >> ((a >> 4) + (b >> 4)) & 1;
}
static bool solid(int x, int y) { /* (off the screen: the part of the cell at its edge, the same) */
  int px = x >> 8, py = y >> 8;
  return solid_px((unsigned)(px < 0 ? 0 : px >= SCREEN_W ? SCREEN_W - 1 : px), (unsigned)(py < 0 ? 0 : py >= SCREEN_H ? SCREEN_H - 1 : py));
}
static int maze_n; /* (a new maze each round) */
static void zones_make(void) { /* (for a new maze) */
  maze_n++;
  for (int i = 0; i < SCREEN_W; i++) {
    int c = clampi((i - TX0) / TCS, 0, TCOLS - 1), f = i - TX0 - c * TCS;
    xzone[i] = (uint8_t)(c | (f < 2 ? 0 : f >= TCS - 2 ? 2 : 1) << 4);
  }
  for (int i = 0; i < SCREEN_H; i++) {
    int r = clampi((i - TY0) / TCS, 0, TROWS - 1), f = i - TY0 - r * TCS;
    yzone[i] = (uint8_t)(r | (f < 2 ? 0 : f >= TCS - 2 ? 6 : 3) << 4);
  }
  for (int k = 0; k < TROWS * TCOLS; k++) {
    ways[k] = 0;
    for (int d = 0; d < 4; d++) ways[k] |= (uint8_t)(open_way(k, d) << d);
    zones[k] = 0;
    for (int z = 0, r = k / TCOLS, c = k % TCOLS; z < 9; z++) { /* (as solid finds it in that part of the cell) */
      bool L = z % 3 == 0, R = z % 3 == 2, T = z < 3, B = z >= 6;
      int gr = r + B, gc = c + R;
      bool w = (T && H(r, c)) || (B && H(r + 1, c)) || (L && V(r, c)) || (R && V(r, c + 1)) ||
               ((L || R) && (T || B) && (H(gr, gc - 1) || H(gr, gc) || V(gr - 1, gc) || V(gr, gc)));
      zones[k] |= (uint16_t)(w << z);
    }
  }
}
#endif
static bool open_way(int k, int d) { /* from cell k to the next one in direction d (right, down, left, up) */
  int r = k / TCOLS, c = k % TCOLS;
  return d == 0 ? !V(r, c + 1) : d == 1 ? !H(r + 1, c) : d == 2 ? !V(r, c) : !H(r, c);
}
#if NP_SOFT_FLOAT
#define OPEN(k, d) (ways[k] >> (d) & 1)
#else
#define OPEN(k, d) open_way(k, d)
#endif
static const int8_t STEP[4] = {1, TCOLS, -1, -TCOLS};
static bool near(int x, int y, int p, int r) {
#if NP_SOFT_FLOAT /* (the far ones first, without the multiplications) */
  int R = r << 8;
  x -= tx_[p], y -= ty_[p];
  if (x >= R || x <= -R || y >= R || y <= -R) return false;
  x >>= 8, y >>= 8;
#else
  x = (x - tx_[p]) >> 8, y = (y - ty_[p]) >> 8;
#endif
  return x * x + y * y < r * r;
}
static void tanks_start(void) {
  uint8_t seen[TROWS * TCOLS] = {0};
  for (uint8_t *w = &hwall[0][0]; w < &hwall[0][0] + sizeof hwall; w++) *w = 1;
  for (uint8_t *w = &vwall[0][0]; w < &vwall[0][0] + sizeof vwall; w++) *w = 1;
  /* a random walk, knocking down the wall into every cell it reaches first: any maze as likely as
     any other */
  int k = rn(TROWS * TCOLS), left = TROWS * TCOLS - 1;
  for (seen[k] = 1; left;) {
    int d = rn(4), j = k + STEP[d], r = k / TCOLS, c = k % TCOLS;
    if ((d == 0 && c == TCOLS - 1) || (d == 2 && !c) || j < 0 || j >= TROWS * TCOLS) continue;
    if (!seen[j]) *(d & 1 ? &hwall[r + (d == 1)][c] : &vwall[r][c + (d == 0)]) = 0, seen[j] = 1, left--;
    k = j;
  }
  for (int r = 1; r < TROWS; r++)
    for (int c = 1; c < TCOLS; c++) {
      if (!rn(3)) hwall[r][c] = 0;
      if (!rn(3)) vwall[r][c] = 0;
    }
#if NP_SOFT_FLOAT
  zones_make();
#endif
#ifdef __TICE__
  maze++;
  if (shared_by != BY_TANKS) /* (the rectangles' rows, kept from here on) */
    shared_by = BY_TANKS, memset(shared.tq.at, 0, sizeof shared.tq.at), shared.tq.used = 0;
#endif
  for (int p = 0; p < 2; p++) {
    int c = p ? TCOLS - 1 - rn(3) : rn(3);
    tx_[p] = (TX0 + c * TCS + TCS / 2) << 8, ty_[p] = (TY0 + rn(TROWS) * TCS + TCS / 2) << 8;
    ta[p] = p ? 32 << 8 : 0, dead[p] = false;
  }
  for (int i = 0; i < SHELLS; i++) sh[i].own = -1;
  ai_t = ai_shot = now;
}
static bool tank_stuck(int p, int x, int y) {
  if (!dead[1 - p] && near(x, y, 1 - p, 2 * TR)) return true;
#if NP_SOFT_FLOAT
  /* (the same 16 points round it: in whole pixels and 256ths, added to the tank's without shifting each) */
  static int8_t whole[16][2];
  static uint8_t part[16][2];
  if (!whole[0][0])
    for (int i = 0; i < 16; i++)
      for (int k = 0; k < 2; k++) {
        int v = (k ? sin64(4 * i) : cos64(4 * i)) * TR * 2;
        whole[i][k] = (int8_t)(v >> 8), part[i][k] = (uint8_t)(v & 255);
      }
  int px = x >> 8, py = y >> 8, fx = x & 255, fy = y & 255;
  for (int i = 0; i < 16; i++) {
    int qx = px + whole[i][0] + (fx + part[i][0] > 255), qy = py + whole[i][1] + (fy + part[i][1] > 255);
    if (solid_px((unsigned)(qx < 0 ? 0 : qx >= SCREEN_W ? SCREEN_W - 1 : qx), (unsigned)(qy < 0 ? 0 : qy >= SCREEN_H ? SCREEN_H - 1 : qy)))
      return true; /* (off the screen: as solid does) */
  }
#else
  for (int i = 0; i < 64; i += 4)
    if (solid(x + cos64(i) * TR * 2, y + sin64(i) * TR * 2)) return true;
#endif
  return false;
}
static void fire(int p) {
  int n = 0, k = -1, a = ta[p] >> 8;
  for (int i = 0; i < SHELLS; i++)
    if (sh[i].own == p) n++;
    else if (sh[i].own < 0) k = i;
  if (n >= 5 || k < 0) return;
  sh[k].x = tx_[p] + cos64(a) * (TR + 3) * 2, sh[k].y = ty_[p] + sin64(a) * (TR + 3) * 2;
  sh[k].vx = cos64(a), sh[k].vy = sin64(a), sh[k].life = 9000, sh[k].own = p;
}
/* a shell's way for h ms, off the walls it meets */
static void roll(int *x, int *y, int *vx, int *vy, int h) {
#if NP_SOFT_FLOAT /* (no multiplication or division for the usual steps) */
  int nx = *x + (h == 16 ? *vx * 4 : h == 8 ? *vx * 2 : *vx * h / 4);
  int ny = *y + (h == 16 ? *vy * 4 : h == 8 ? *vy * 2 : *vy * h / 4);
#else
  int nx = *x + *vx * h / 4, ny = *y + *vy * h / 4;
#endif
  if (!solid(nx, ny)) {
    *x = nx, *y = ny;
    return;
  }
  bool a = solid(nx, *y), b = solid(*x, ny);
  if (a == b || a) *vx = -*vx;
  if (a == b || b) *vy = -*vy;
}
/* Would a shell fired at a from the computer's tank find the player's, bouncing at most b
   times, before its own? */
#if NP_SOFT_FLOAT
/* the player's tank well off the line from (x, y) the way (vx, vy) goes: no point of it near the tank (its distance
   from the line, or from (x, y) when behind it, past TR by more than the rounding of the positions to pixels) */
static bool line_far(int x, int y, int vx, int vy) {
  int dx = (tx_[0] - x) >> 8, dy = (ty_[0] - y) >> 8, cross = dx * vy - dy * vx, m = (TR + 4) * 128;
  return dx * vx + dy * vy < 0 ? dx * dx + dy * dy >= (TR + 4) * (TR + 4) : cross >= m || cross <= -m;
}
/* how many 16 ms steps from (x, y) at (vx, vy) stay in the middle of its cell (2 pixels or more from every side) */
static int steps_open(int x, int y, int vx, int vy) {
  unsigned px = (unsigned)(x >> 8), py = (unsigned)(y >> 8);
  if (px >= SCREEN_W || py >= SCREEN_H || (xzone[px] >> 4) != 1 || (yzone[py] >> 4) != 3) return 0;
  int x0 = (TX0 + (xzone[px] & 15) * TCS + 2) << 8, y0 = (TY0 + (yzone[py] & 15) * TCS + 2) << 8, k = 400, n;
  int x1 = x0 + ((TCS - 4) << 8), y1 = y0 + ((TCS - 4) << 8); /* (its middle: x0 to x1, y0 to y1) */
  if (vx > 0 && (n = (x1 - 1 - x) / (4 * vx)) < k) k = n;
  if (vx < 0 && (n = (x - x0) / (-4 * vx)) < k) k = n;
  if (vy > 0 && (n = (y1 - 1 - y) / (4 * vy)) < k) k = n;
  if (vy < 0 && (n = (y - y0) / (-4 * vy)) < k) k = n;
  return k;
}
__attribute__((noinline))
#endif
static bool shot_finds(int a, int b) {
  int x = tx_[1], y = ty_[1], vx = cos64(a), vy = sin64(a);
#if NP_SOFT_FLOAT
  /* Without following it step by step where it can't find anything: a shell going straight from (x, y) passes near
     the player's tank only when the tank is near its line. Its first stretch, going away from the computer's tank,
     never comes near that one either; and after the last bounce allowed, a stretch well off the player's ends it. */
  bool far = line_far(x, y, vx, vy), first = true;
  if (far && !b) return false;
  for (int i = 0; i < 400; i++) {
    if (first && far) { /* (and the steps that stay in the open middle of a cell, where it can't hit a wall: at once) */
      int k = steps_open(x, y, vx, vy);
      if (k >= 400 - i) return false;
      x += 4 * vx * k, y += 4 * vy * k, i += k;
    }
    int ox = vx, oy = vy;
    roll(&x, &y, &vx, &vy, 16);
    if (ox != vx || oy != vy) {
      if (--b < 0) return false;
      if (first) {
        first = false;
        if (line_far(x, y, vx, vy) && !b) return false;
      }
    }
    if (first && far) continue;
    if (near(x, y, 0, TR)) return true;
    if (i > 8 && near(x, y, 1, TR + 3)) return false;
  }
  return false;
#else
  for (int i = 0; i < 400; i++) {
    int ox = vx, oy = vy;
    roll(&x, &y, &vx, &vy, 16);
    if ((ox != vx || oy != vy) && --b < 0) return false;
    if (near(x, y, 0, TR)) return true;
    if (i > 8 && near(x, y, 1, TR + 3)) return false;
  }
  return false;
#endif
}
/* the computer's tank: a shot when it sees one, a little off, else the way to the player, cell
   by cell through the maze (found breadth first from the player) */
#if NP_SOFT_FLOAT
/* The next cell on the way from cell me to cell you, found breadth first from you (as tanks_cpu does it elsewhere):
   on its own, its tables outside the stack, which the CE's compiler reaches slowly in a big function */
static uint8_t way_dist[TROWS * TCOLS], way_q[TROWS * TCOLS];
__attribute__((noinline)) static int way_on(int me, int you) {
  static const uint8_t BIT[4] = {1, 2, 4, 8};
  static int was_me = -1, was_you, was_maze, was_next; /* (the last answer: the tanks stay in a cell a while) */
  if (me == was_me && you == was_you && maze_n == was_maze) return was_next;
  was_me = me, was_you = you, was_maze = maze_n;
  int n = 1, next = you;
  for (int i = 0; i < TROWS * TCOLS; i++) way_dist[i] = 255;
  way_dist[you] = 0, way_q[0] = (uint8_t)you;
  for (int h = 0; h < n; h++) {
    int k = way_q[h], w = ways[k];
    for (int d = 0; d < 4; d++) {
      int j = k + STEP[d];
      if (w & BIT[d] && way_dist[j] == 255) way_dist[j] = (uint8_t)(way_dist[k] + 1), way_q[n++] = (uint8_t)j;
    }
  }
  for (int d = 0; d < 4; d++)
    if (me != you && ways[me] & BIT[d] && way_dist[me + STEP[d]] < way_dist[me]) next = me + STEP[d];
  return was_next = next;
}
#define CE_APART __attribute__((noinline)) /* (the same: a function the CE's compiler handles faster on its own) */
#else
#define CE_APART
#endif
CE_APART static void tanks_cpu(int *v, int *turn) {
  static const int16_t RELOAD[4] = {0, 2300, 1700, 1200}, LOOK[4] = {0, 700, 500, 350};
  static const int8_t BOUNCES[4] = {0, 0, 0, 1};
  int dir = ta[1] >> 8, want = dir;
  if (ago(ai_t) > (uint32_t)LOOK[cpu]) {
    ai_t = now, ai_aim = -1;
    for (int i = 0; i < 64 && ai_aim < 0; i++) { /* the nearest to where it faces first */
      int a = (dir + (i & 1 ? 1 : -1) * ((i + 1) / 2)) & 63;
      if (shot_finds(a, BOUNCES[cpu])) ai_aim = (a + rn(9 - 2 * cpu) - (4 - cpu)) & 63;
    }
  }
  if (ai_aim >= 0) {
    want = ai_aim;
    if (want == dir && ago(ai_shot) > (uint32_t)RELOAD[cpu]) fire(1), ai_shot = now, ai_t = now - 200;
  } else {
#if NP_SOFT_FLOAT
    int you = tcell(tx_[0], ty_[0]), next = way_on(tcell(tx_[1], ty_[1]), you);
#else
    uint8_t dist[TROWS * TCOLS], q[TROWS * TCOLS];
    int me = tcell(tx_[1], ty_[1]), you = tcell(tx_[0], ty_[0]), n = 1, next = you;
    for (int i = 0; i < TROWS * TCOLS; i++) dist[i] = 255;
    dist[you] = 0, q[0] = (uint8_t)you;
    for (int h = 0; h < n; h++)
      for (int d = 0; d < 4; d++)
        if (OPEN(q[h], d) && dist[q[h] + STEP[d]] == 255) dist[q[h] + STEP[d]] = dist[q[h]] + 1, q[n++] = (uint8_t)(q[h] + STEP[d]);
    for (int d = 0; d < 4; d++)
      if (me != you && OPEN(me, d) && dist[me + STEP[d]] < dist[me]) next = me + STEP[d];
#endif
    int gx = ((TX0 + next % TCOLS * TCS + TCS / 2) << 8) - tx_[1], gy = ((TY0 + next / TCOLS * TCS + TCS / 2) << 8) - ty_[1];
    if (next == you) gx = tx_[0] - tx_[1], gy = ty_[0] - ty_[1];
#if NP_SOFT_FLOAT
    static int8_t SIN[80]; /* (sin64 from a table, cos64 16 on) */
    if (!SIN[4])
      for (int a = 0; a < 80; a++) SIN[a] = (int8_t)sin64(a);
    static int was_x = INT_MIN, was_y, was_want; /* (the last answer: the same pixels apart, the same way) */
    int d[32], x = gx >> 8, y = gy >> 8; /* (half a turn on, the opposite: sin64(a + 32) is -sin64(a)) */
    if (x == was_x && y == was_y) {
      want = was_want;
    } else {
      for (int a = 0; a < 32; a++) d[a] = x * SIN[a + 16] + y * SIN[a];
      for (int a = 0, best = INT_MIN / 2; a < 64; a++) {
        int v = a < 32 ? d[a] : -d[a - 32];
        if (v > best) best = v, want = a;
      }
      was_x = x, was_y = y, was_want = want;
    }
#else
    for (int a = 0, best = INT_MIN / 2; a < 64; a++) { /* the way of the 64 nearest to it */
      int d = (gx >> 8) * cos64(a) + (gy >> 8) * sin64(a);
      if (d > best) best = d, want = a;
    }
#endif
    *v = clampi(6 - (((want - dir + 32) & 63) - 32) * (((want - dir + 32) & 63) - 32), 0, 1) * (36 + 9 * cpu);
  }
  int d = ((want - dir + 32) & 63) - 32;
  *turn = d > 0 ? 4 + cpu : d < 0 ? -4 - cpu : 0;
}
static void tanks_step(void) {
  for (int p = 0; p < 2; p++) {
    if (dead[p]) continue;
    int v = 0, turn = 0;
    if (is_cpu(p)) {
      tanks_cpu(&v, &turn);
      ta[p] = (ta[p] + turn * ms) & 0x3FFF; /* 8 a ms: half a turn a second */
    } else {
      /* the arrows point where to go (diagonals too): the tank turns that way, at half a turn a second, and drives
         once it faces within an eighth of a turn of it */
      static const int8_t WANT[9] = {40, 48, 56, 32, -1, 0, 24, 16, 8}; /* in 64ths of a turn, y down */
      int w = WANT[(diry(p) + 1) * 3 + dirx(p) + 1];
      if (w >= 0) {
        int d = (((w << 8) - ta[p] + 0x2000) & 0x3FFF) - 0x2000, step = 8 * ms;
        ta[p] = (ta[p] + (d > step ? step : d < -step ? -step : d)) & 0x3FFF;
        v = (d < 0 ? -d : d) <= 0x800 ? 70 : 0;
      }
      if (acted(p)) fire(p);
    }
    int x = tx_[p], y = ty_[p], dx = cos64(ta[p] >> 8) * v * ms / 496, dy = sin64(ta[p] >> 8) * v * ms / 496;
    if (!tank_stuck(p, x + dx, y + dy)) tx_[p] += dx, ty_[p] += dy;
    else if (!tank_stuck(p, x + dx, y)) tx_[p] += dx;
    else if (!tank_stuck(p, x, y + dy)) ty_[p] += dy;
  }
  for (int i = 0; i < SHELLS; i++)
    for (int j = 0; j < ms && sh[i].own >= 0; j += 8) {
      roll(&sh[i].x, &sh[i].y, &sh[i].vx, &sh[i].vy, ms - j < 8 ? ms - j : 8);
      if ((sh[i].life -= 8) <= 0) sh[i].own = -1;
      for (int p = 0; p < 2; p++)
        if (!dead[p] && near(sh[i].x, sh[i].y, p, TR - 1)) dead[p] = true, dead_t[p] = now, sh[i].own = -1;
    }
  /* a round ends a moment after a tank is hit: the other may be hit too, a draw */
  int k = dead[0] && (!dead[1] || dead_t[0] < dead_t[1]) ? 0 : 1; /* the first one hit */
  if (dead[k] && ago(dead_t[k]) > 1200) point(dead[0] && dead[1] ? 2 : !dead[0] ? 0 : 1);
}
#if NP_SOFT_FLOAT
/* Where multiplication and division are slow (the TI-84 Plus CE), the rectangles' rows, kept: the same for any x and
   y (quad below at 0, 0), from row q[0], q[1] of them, each from q[2 + 2i] to q[3 + 2i] (none: before it). On the CE,
   during Tanks, every angle's in the room Air Hockey's drawings take; else (or no room) each tank (quad_tank) keeps
   the last of each of its three. */
#define QUAD_ROWS 32
static int8_t quad_slot[6][2 + 2 * QUAD_ROWS], quad_key[6][4];
static int quad_tank;
static bool quad_make(int a, int l0, int l1, int w, int8_t *q) { /* false: more rows than QUAD_ROWS */
  int c = cos64(a), s = sin64(a), vx[4], vy[4];
  for (int i = 0; i < 4; i++) {
    int l = i == 1 || i == 2 ? l1 : l0, e = i < 2 ? -w : w;
    vx[i] = (c * l + s * e) / 127, vy[i] = (s * l - c * e) / 127;
  }
  int y0 = vy[0], y1 = vy[0];
  for (int i = 1; i < 4; i++) y0 = vy[i] < y0 ? vy[i] : y0, y1 = vy[i] > y1 ? vy[i] : y1;
  y0 = (y0 >> 2) - 1, y1 = (y1 >> 2) + 2;
  if (y1 - y0 > QUAD_ROWS) return false;
  q[0] = (int8_t)y0, q[1] = (int8_t)(y1 - y0), q += 2;
  for (int row = y0; row < y1; row++, q += 2) {
    int yc = 4 * row + 2, lo = 1 << 20, hi = -lo;
    for (int i = 0; i < 4; i++) {
      int j = (i + 1) & 3;
      if ((vy[i] <= yc) != (vy[j] <= yc)) {
        int xx = vx[i] + (yc - vy[i]) * (vx[j] - vx[i]) / (vy[j] - vy[i]);
        lo = xx < lo ? xx : lo, hi = xx > hi ? xx : hi;
      }
    }
    q[0] = (int8_t)(hi > lo ? (lo + 2) >> 2 : 1), q[1] = (int8_t)(hi > lo ? (hi + 2) >> 2 : 0);
  }
  return true;
}
static const int8_t *quad_rows(int a, int l0, int l1, int w) { /* null: too many rows */
  int kind = l1 == 24 ? 1 : l1 == 52 ? 2 : 0, k = quad_tank * 3 + kind;
#ifdef __TICE__
  static const int8_t KIND[3][3] = {{-32, 32, 28}, {-24, 24, 18}, {0, 52, 6}};
  if (shared_by == BY_TANKS && l0 == KIND[kind][0] && l1 == KIND[kind][1] && w == KIND[kind][2]) {
    uint16_t *at = &shared.tq.at[a & 63][kind];
    if (!*at && shared.tq.used + 2 + 2 * QUAD_ROWS <= sizeof shared.tq.rows && quad_make(a, l0, l1, w, shared.tq.rows + shared.tq.used))
      *at = (uint16_t)(shared.tq.used + 1), shared.tq.used += 2 + 2 * (uint8_t)shared.tq.rows[shared.tq.used + 1];
    if (*at) return shared.tq.rows + *at - 1;
  }
#endif
  if (quad_slot[k][1] && quad_key[k][0] == a && quad_key[k][1] == l0 && quad_key[k][2] == l1 && quad_key[k][3] == w)
    return quad_slot[k];
  quad_key[k][0] = (int8_t)a, quad_key[k][1] = (int8_t)l0, quad_key[k][2] = (int8_t)l1, quad_key[k][3] = (int8_t)w;
  if (quad_make(a, l0, l1, w, quad_slot[k])) return quad_slot[k];
  quad_slot[k][1] = 0;
  return 0;
}
#endif
#ifdef __TICE__
/* a row's pixels x0 to x1 (as far as the columns being drawn go) in colour c */
static void row_fill(color_t *row, int x0, int x1, color_t c) {
  x0 = x0 > gfx_x0 ? x0 : gfx_x0, x1 = x1 < gfx_x1 ? x1 : gfx_x1;
  if (x1 > x0) wmemset((wchar_t *)row + x0, (wchar_t)c, (size_t)(x1 - x0));
}
#endif
/* a rectangle from l0 to l1 ahead of (x, y) the way a faces, w to either side (quarter pixels) */
static void quad(int x, int y, int a, int l0, int l1, int w, color_t col) {
#if NP_SOFT_FLOAT
  const int8_t *q = quad_rows(a, l0, l1, w);
  if (q) { /* (its rows in the strip only) */
    int top = y + q[0], i = gfx_y0 - top, n = gfx_y1 - top;
    i = i < 0 ? 0 : i, n = n > q[1] ? q[1] : n;
#ifdef __TICE__
    color_t *line = gfx_buf + (top + i - gfx_y0) * SCREEN_W;
#endif
    for (const int8_t *e = q + 2 + 2 * i; i < n; i++, e += 2) {
      int lo = e[0], hi = e[1];
      if (hi >= lo) {
#ifdef __TICE__ /* (a tank being drawn: its rows straight into the strip) */
        if (!gfx_listing) row_fill(line, x + lo, x + hi, col);
        else
#endif
          gfx_fill(x + lo, top + i, hi - lo, 1, col);
      }
#ifdef __TICE__
      line += SCREEN_W;
#endif
    }
    return;
  }
#else /* (where they're kept, a tank's rectangles always fit: 24 rows at most) */
  int c = cos64(a), s = sin64(a), vx[4], vy[4];
  for (int i = 0; i < 4; i++) {
    int l = i == 1 || i == 2 ? l1 : l0, e = i < 2 ? -w : w;
    vx[i] = 4 * x + (c * l + s * e) / 127, vy[i] = 4 * y + (s * l - c * e) / 127;
  }
  int y0 = vy[0], y1 = vy[0]; /* (only its own rows) */
  for (int i = 1; i < 4; i++) y0 = vy[i] < y0 ? vy[i] : y0, y1 = vy[i] > y1 ? vy[i] : y1;
  y0 = (y0 >> 2) - 1, y1 = (y1 >> 2) + 2;
  for (int row = y0 > gfx_y0 ? y0 : gfx_y0, end = y1 < gfx_y1 ? y1 : gfx_y1; row < end; row++) {
    int yc = 4 * row + 2, lo = 1 << 20, hi = -lo;
    for (int i = 0; i < 4; i++) {
      int j = (i + 1) & 3;
      if ((vy[i] <= yc) != (vy[j] <= yc)) {
        int xx = vx[i] + (yc - vy[i]) * (vx[j] - vx[i]) / (vy[j] - vy[i]);
        lo = xx < lo ? xx : lo, hi = xx > hi ? xx : hi;
      }
    }
    if (hi > lo) gfx_fill((lo + 2) >> 2, row, ((hi + 2) >> 2) - ((lo + 2) >> 2), 1, col);
  }
#endif
}
static void tank(int x, int y, int a, int p) {
#if NP_SOFT_FLOAT
  quad_tank = p;
#endif
  quad(x + 1, y + 2, a, -32, 32, 28, HEX(0xB9BBB6)); /* its shadow */
  quad(x, y, a, -32, 32, 28, dark[p]);
  quad(x, y, a, -24, 24, 18, team[p]);
  quad(x, y, a, 0, 52, 6, dark[p]);
  disc(x, y, 4, dark[p]);
  disc(x, y, 2, light[p]);
}
#ifdef __TICE__
/* the floor and the walls over the rows being drawn, in the columns being drawn (as their gfx_fill calls would draw
   them): a row of each kind (a wall row's first 4 rows, the rest of its cell), the next ones of that kind copied */
static void maze_at(const int *a) {
  int c0 = (gfx_x0 - TX0 - TCS - 2) / TCS, c1 = (gfx_x1 - TX0 + 2) / TCS + 1; /* (the walls' columns there, or more) */
  c0 = c0 < 0 ? 0 : c0, c1 = c1 > TCOLS ? TCOLS : c1;
  int top = gfx_y0 < 22 ? 22 : gfx_y0; /* (from under the score bar, as the floor's fill) */
  int t = top - (TY0 - 2), r = t < 0 ? -1 : t / TCS, f = t < 0 ? t : t - r * TCS; /* the wall row, and the row in it */
  color_t *row = gfx_buf + (top - gfx_y0) * SCREEN_W, *same = 0;
  for (int y = top, kind = -2; y < gfx_y1; y++, row += SCREEN_W) {
    int k = r >= 0 && r <= TROWS ? 2 * r + (f >= 4) : -1;
    if (k == kind) {
      memcpy(row + gfx_x0, same + gfx_x0, (size_t)(gfx_x1 - gfx_x0) * sizeof(color_t));
    } else {
      kind = k, same = row;
      row_fill(row, 0, SCREEN_W, HEX(0xE6E6E1));
      if (k >= 0) /* (a wall's row across: from TY0 + r * TCS - 2, 4 rows) */
        for (int c = c0, x = TX0 + c0 * TCS - 2; c <= c1; c++, x += TCS) {
          if (f < 4 && c < TCOLS && hwall[r][c]) row_fill(row, x, x + TCS + 4, HEX(0x3A3F4B));
          if ((r < TROWS && vwall[r][c]) || (f < 4 && r && vwall[r - 1][c])) row_fill(row, x, x + 4, HEX(0x3A3F4B));
        }
    }
    if (++f == TCS) f = 0, r++;
    else if (!f) r = 0; /* (from above the maze into its first wall's rows) */
  }
}
static void tank_at(const int *a) { tank(a[0], a[1], a[2], a[3]); }
static void shells_at(const int *a) { /* (over the rows being drawn) */
  for (int i = 0; i < SHELLS; i++)
    if (sh[i].own >= 0) disc(sh[i].x >> 8, sh[i].y >> 8, 2, HEX(0x1E1F24));
}
#endif
static void tanks_paint(void) {
#ifdef __TICE__
  gfx_call(maze_at, &maze, 1, 0, 22, SCREEN_W, SCREEN_H - 22); /* (one call a strip) */
#else
  gfx_fill(0, 22, SCREEN_W, SCREEN_H - 22, HEX(0xE6E6E1));
  for (int r = 0; r <= TROWS; r++)
    for (int c = 0; c <= TCOLS; c++) {
      if (H(r, c)) gfx_fill(TX0 + c * TCS - 2, TY0 + r * TCS - 2, TCS + 4, 4, HEX(0x3A3F4B));
      if (V(r, c)) gfx_fill(TX0 + c * TCS - 2, TY0 + r * TCS - 2, 4, TCS + 4, HEX(0x3A3F4B));
    }
#endif
#ifdef __TICE__
  if (gfx_listing) { /* (the shells: in each strip, one call saying where those in it are) */
    int sx[SHELLS], sy[SHELLS];
    for (int i = 0; i < SHELLS; i++) sx[i] = sh[i].x >> 8, sy[i] = sh[i].own < 0 ? -99 : sh[i].y >> 8;
    for (int s = TY0 & ~15; s < TY0 + TROWS * TCS; s += 16) {
      int a[8] = {0}, n = 0, x0 = SCREEN_W, x1 = 0;
      for (int i = 0; i < SHELLS; i++) {
        int x = sx[i], y = sy[i];
        if (y + 2 <= s || y - 2 >= s + 16) continue;
        a[n < 7 ? n : 7] += x | y << 9 | i << 18, n++;
        x0 = x - 2 < x0 ? x - 2 : x0, x1 = x + 2 > x1 ? x + 2 : x1;
      }
      gfx_call(shells_at, a, 8, n ? x0 : 0, s, n ? x1 - x0 : 0, 16);
    }
  } else {
    shells_at(0);
  }
#else
  for (int i = 0; i < SHELLS; i++)
    if (sh[i].own >= 0) disc(sh[i].x >> 8, sh[i].y >> 8, 2, HEX(0x1E1F24));
#endif
  for (int p = 0; p < 2; p++) {
    int x = tx_[p] >> 8, y = ty_[p] >> 8;
    int32_t t = (int32_t)ago(dead_t[p]);
#ifdef __TICE__
    int a[4] = {x, y, ta[p] >> 8, p};
    moving(tank_at, a, 4, x - 15, y - 15, 31, 32, TY0 - 4, TY0 + TROWS * TCS + 4, !dead[p]);
    if (!dead[p]) continue;
#else
    if (!dead[p]) {
      tank(x, y, ta[p] >> 8, p);
      continue;
    }
#endif
    gfx_circle(x, y, 9, HEX(0x55524C), 150); /* what is left of it */
    if (t < 700) gfx_circle(x, y, 6 + t / 30, HEX(0xFF8A2A), 256 - t * 256 / 700), gfx_circle(x, y, 3 + t / 50, HEX(0xFFE066), 256 - t * 256 / 700);
  }
}

/* ------------------------------------------------------------ Light Cycles */
#define CW 80 /* the grid, 4 pixels a cell */
#define CH 54
#define CY0 24
#ifdef __TICE__ /* (little RAM: room marks the cells it reaches in cell itself, and reaches 223 at most) */
#define QMAX 256
#define cell shared.lc.cell /* (in the room Air Hockey's drawings take) */
_Static_assert(sizeof cell == CH * CW, "Light Cycles' grid");
/* each strip's changes: a count, and the columns its cells changed in since its list last said so (said) */
static unsigned changes[15];
static int changed_x0[15], changed_x1[15];
static bool said[15];
static void cycles_changed(int x0, int y0, int x1, int y1) { /* pixels x0..x1, y0..y1 (cells, or all) */
  for (int s = y0 > 0 ? y0 >> 4 : 0; s < 15 && s <= (y1 - 1) >> 4; s++) {
    if (said[s] || changed_x1[s] <= changed_x0[s]) changed_x0[s] = x0, changed_x1[s] = x1, said[s] = false;
    else changed_x0[s] = x0 < changed_x0[s] ? x0 : changed_x0[s], changed_x1[s] = x1 > changed_x1[s] ? x1 : changed_x1[s];
    changes[s]++;
  }
}
static void cell_changed(int x, int y) { /* (what it and its neighbours draw) */
  cycles_changed(x * 4 - 4, CY0 + y * 4 - 4, x * 4 + 8, CY0 + y * 4 + 8);
}
#else
#define QMAX 1024
static uint8_t cell[CH][CW], mark[CH][CW], gen;
#endif
static uint16_t queue[QMAX];
static int hx[2], hy[2], hd[2], turns[2][2], nturns[2];
static bool crashed[2];
static uint32_t tick_t, round_t;
static const int8_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1}; /* right, down, left, up */

static void cycles_start(void) { /* side by side, each on its own lane, the other's way */
  for (uint8_t *c = &cell[0][0]; c < &cell[0][0] + CW * CH; c++) *c = 0;
  for (int p = 0; p < 2; p++) {
    hx[p] = p ? CW - 17 : 16, hy[p] = p ? CH / 2 + 8 : CH / 2 - 8, hd[p] = p ? 2 : 0, nturns[p] = 0, crashed[p] = false;
    cell[hy[p]][hx[p]] = (uint8_t)(p + 1);
  }
  tick_t = round_t = now;
#ifdef __TICE__
  shared_by = BY_CYCLES;
  cycles_changed(0, CY0, SCREEN_W, CY0 + CH * 4);
#endif
}
static bool free_at(int x, int y) { return x >= 0 && y >= 0 && x < CW && y < CH && !cell[y][x]; }
/* the free cells the computer could still reach from (x, y), up to cap */
static int room(int x, int y, int cap) {
  if (!free_at(x, y)) return 0;
#ifdef __TICE__ /* (each cell by its place in the grid and its column: no division) */
  uint8_t *grid = &cell[0][0], *qx = shared.lc.x;
  int n = 1;
  queue[0] = (uint16_t)(y * CW + x), qx[0] = (uint8_t)x, grid[queue[0]] = 3;
  for (int h = 0; h < n && n < cap; h++) {
    int i = queue[h], c = qx[h];
    for (int d = 0; d < 4; d++) { /* (right, down, left, up) */
      int j = d == 0 ? (c + 1 < CW ? i + 1 : -1) : d == 1 ? (i + CW < CW * CH ? i + CW : -1)
            : d == 2 ? (c ? i - 1 : -1) : (i >= CW ? i - CW : -1);
      if (j >= 0 && !grid[j] && n < QMAX) grid[j] = 3, qx[n] = (uint8_t)(c + (d == 0) - (d == 2)), queue[n++] = (uint16_t)j;
    }
  }
  for (int i = 0; i < n; i++) grid[queue[i]] = 0;
#else
  if (!++gen)
    for (uint8_t *m = &mark[0][0]; m < &mark[0][0] + CW * CH; m++) *m = 0;
  int n = 1;
  queue[0] = (uint16_t)(y * CW + x), mark[y][x] = gen;
  for (int h = 0; h < n && n < cap; h++)
    for (int d = 0; d < 4; d++) {
      int nx = queue[h] % CW + DX[d], ny = queue[h] / CW + DY[d];
      if (free_at(nx, ny) && mark[ny][nx] != gen && n < QMAX) mark[ny][nx] = gen, queue[n++] = (uint16_t)(ny * CW + nx);
    }
#endif
  return n;
}
/* the computer's way, at a human's pace: the turn that leaves it the most room and the longest
   way ahead, without guessing the player's next move */
static int cycles_cpu(void) {
  static const int16_t CAP[4] = {0, 12, 70, 220};
  if (!bot_due()) return hd[1];
  int best = hd[1], bv = INT_MIN / 2;
  for (int t = 0; t < 3; t++) {
    int d = (hd[1] + t + (t == 2)) & 3, x = hx[1] + DX[d], y = hy[1] + DY[d], r = room(x, y, CAP[cpu]), a = 0;
    while (a < 7 && free_at(x + a * DX[d], y + a * DY[d])) a++;
    int v = r * 8 + a * 48 + (t ? 0 : 6) + rn(cpu == 1 ? 120 : 6);
    if (!r) v = INT_MIN / 4;
    if (v > bv) bv = v, best = d;
  }
  return best;
}
static void cycles_step(void) {
  static const uint8_t BIT[4] = {B_RIGHT, B_DOWN, B_LEFT, B_UP};
  for (int p = 0; p < 2; p++)
    for (int d = 0; d < 4; d++) { /* a turn waits its turn: two can wait */
      int last = nturns[p] ? turns[p][nturns[p] - 1] : hd[p];
      if (tap[p] & BIT[d] && d != last && (d ^ last) != 2 && nturns[p] < 2) turns[p][nturns[p]++] = d;
    }
  /* a step every 70 ms, down to 42 as the round goes on */
  uint32_t period = 70 - (ago(round_t) < 28000 ? ago(round_t) / 1000 : 28);
  for (int n = 0; n < 3 && !phase && ago(tick_t) >= period; n++) { /* (the steps owed: a slow frame catches up) */
    tick_t = ago(tick_t) > 3 * period ? now : tick_t + period;
    int nx[2], ny[2];
    for (int p = 0; p < 2; p++) {
      if (is_cpu(p)) hd[p] = cycles_cpu();
      else if (nturns[p]) hd[p] = turns[p][0], turns[p][0] = turns[p][1], nturns[p]--;
      nx[p] = hx[p] + DX[hd[p]], ny[p] = hy[p] + DY[hd[p]], crashed[p] = !free_at(nx[p], ny[p]);
    }
    if (nx[0] == nx[1] && ny[0] == ny[1]) crashed[0] = crashed[1] = true;
    for (int p = 0; p < 2; p++)
      if (!crashed[p]) {
        hx[p] = nx[p], hy[p] = ny[p], cell[hy[p]][hx[p]] = (uint8_t)(p + 1);
#ifdef __TICE__
        cell_changed(hx[p], hy[p]);
#endif
      }
    if (crashed[0] || crashed[1]) point(crashed[0] && crashed[1] ? 2 : crashed[0]);
  }
}
static void cycles_cells(const int *a) { /* the trails' cells over the rows being drawn */
#ifdef __TICE__ /* (and the columns being drawn: a cell draws from a pixel left of its own to its right side) */
  int c0 = gfx_x0 < 4 ? 0 : (gfx_x0 - 4) >> 2, c1 = (gfx_x1 >> 2) + 1;
  c1 = c1 > CW ? CW : c1;
#endif
  for (int r = clampi((gfx_y0 - CY0) / 4, 0, CH); r < clampi((gfx_y1 - CY0) / 4 + 1, 0, CH); r++)
#ifdef __TICE__
    for (int c = c0; c < c1; c++) {
#else
    for (int c = 0; c < CW; c++) {
#endif
      int v = cell[r][c];
      if (!v) continue;
      int l = c && cell[r][c - 1] == v, u = r && cell[r - 1][c] == v; /* its light, joined to the trail before */
      gfx_fill(c * 4, CY0 + r * 4, 4, 4, team[v - 1]);
      gfx_fill(c * 4 + 1 - 2 * l, CY0 + r * 4 + 1 - 2 * u, 2 + 2 * l, 2 + 2 * u, light[v - 1]);
    }
}
static void cycles_grid(const int *a) { /* the night and its grid */
  gfx_fill(0, 22, SCREEN_W, SCREEN_H - 22, HEX(0x060A18));
  for (int i = 0; i < SCREEN_W; i += 16) gfx_fill(i, CY0, 1, SCREEN_H, HEX(0x111B36)), gfx_fill(0, CY0 + i, SCREEN_W, 1, HEX(0x111B36));
}
#ifdef __TICE__
static void nothing(const int *a) {}
static void head_at(const int *a) { /* a light cycle's head at a[0], a[1], player a[2]'s */
  gfx_circle(a[0], a[1], 6, light[a[2]], 90);
  gfx_fill(a[0] - 2, a[1] - 2, 4, 4, WHITE);
}
#endif
static void cycles_paint(void) {
#ifdef __TICE__
  static const int none[1];
  gfx_call(cycles_grid, none, 0, 0, 22, SCREEN_W, SCREEN_H - 22); /* (one call in each strip's list) */
#else
  cycles_grid(0);
#endif
#ifdef __TICE__ /* (the TI-84 Plus CE: each strip's list says how many times its cells changed, where they last did,
                   and holds one call drawing them, not two fills a cell: only the columns where they changed are drawn
                   again) */
  if (gfx_listing) {
    for (int s = CY0 >> 4; s < 15; s++) {
      int a = (int)changes[s];
      gfx_call(nothing, &a, 1, changed_x0[s], s * 16, changed_x1[s] - changed_x0[s], 16);
      said[s] = true;
    }
    gfx_call(cycles_cells, none, 0, 0, CY0, SCREEN_W, CH * 4);
  } else {
    cycles_cells(0);
  }
#else
  cycles_cells(0);
#endif
  for (int p = 0; p < 2; p++) {
    int x = hx[p] * 4 + 2, y = CY0 + hy[p] * 4 + 2;
    int32_t t = (int32_t)ago(phase_t);
#ifdef __TICE__
    int a[3] = {x, y, p};
    moving(head_at, a, 3, x - 6, y - 6, 12, 12, CY0, CY0 + CH * 4, true);
#else
    gfx_circle(x, y, 6, light[p], 90);
    gfx_fill(x - 2, y - 2, 4, 4, WHITE);
#endif
    if (crashed[p] && phase) { /* a flash where it hit */
      x += DX[hd[p]] * 3, y += DY[hd[p]] * 3;
      if (t < 800) gfx_circle(x, y, 4 + t / 25, WHITE, 230 - t * 230 / 800);
      disc(x, y, 3, light[p]);
    }
  }
}

/* ------------------------------------------------------------ the party games
   Bobydeluxe's: short rounds, a key each. */
static struct {
  float rope;            /* Tug of War: -100 (player 1's side) to 100 */
  uint32_t t0, at, next; /* the round's start, the signal (Quick Draw), the computer's next press */
} pg;
static void party(void) { /* a new round */
  pg.rope = 0, pg.t0 = now, pg.next = now + 600;
}
static uint32_t round_ms(void) { return now - pg.t0; }
static void arena(uint32_t bg) {
  gfx_fill(0, 22, SCREEN_W, SCREEN_H - 22, INK);
  box(6, 28, 308, 206, 12, gfx_rgb(bg));
}
static void person(int x, int feet, int p) { /* a player seen from the side, its key under it */
  disc(x, feet, 9, HEX(0x232944));
  box(x - 7, feet - 19, 14, 17, 5, team[p]);
  disc(x, feet - 25, 7, light[p]);
  disc(x + (p ? -3 : 3), feet - 26, 1, INK);
  if (!is_cpu(p)) gfx_text_center(&np_font_body, clampi(x, 30, 290), 222, p ? "EXE" : "OK", light[p], 256);
}

/* Quick Draw: the first to press after the signal wins; before it, loses */
static void quick_start(void) {
  static const uint16_t REACT[4] = {0, 360, 280, 210};
  party();
  pg.at = now + 1500 + rn(2500);
  if (cpu) pg.next = pg.at + REACT[cpu] + rn(REACT[cpu] / 2);
}
static void quick_step(void) {
  if (phase) return;
  if (cpu && due(pg.next)) tap[1] = B_FIRE;
  int a = acted(0), b = acted(1);
  if (a | b) point(a & b ? 2 : due(pg.at) ? b : a);
}
static void quick_paint(void) {
  bool go = due(pg.at);
  arena(go ? 0x3A2F1C : 0x262D52);
  for (int p = 0; p < 2; p++) person(80 + 160 * p, 196, p);
  say(go ? &np_font_title : &np_font_body, 160, 130, go ? T("DRAW!") : T("Wait..."), go ? GOLD : WHITE);
}

/* Tug of War: every tap pulls; the rope slides back to the middle a little */
static void tug_step(void) {
  static const uint16_t EVERY[4] = {0, 300, 200, 150};
  if (phase) return;
  if (cpu && due(pg.next)) tap[1] = B_FIRE, pg.next = now + EVERY[cpu] + rn(EVERY[cpu] / 5);
  float r = pg.rope + (acted(1) - acted(0)) * 7.5f, d = 3 * dt;
  pg.rope = r > d ? r - d : r < -d ? r + d : 0;
  if (pg.rope < -67 || pg.rope > 67 || round_ms() > 30000) point(pg.rope < -1 ? 0 : pg.rope > 1 ? 1 : 2);
}
static void tug_paint(void) {
  int x = 160 + (int)pg.rope;
  char s[8], *e = num(s, round_ms() < 30000 ? (30999 - round_ms()) / 1000 : 0); /* the seconds left */
  arena(0x262D52);
  e[0] = 's', e[1] = 0;
  say(&np_font_body, 160, 56, s, GOLD);
  for (int p = 0; p < 2; p++) gfx_fill(p ? 227 : 91, 96, 2, 100, team[p]);
  gfx_fill(x - 86, 150, 172, 4, HEX(0xC5CBE4));
  for (int p = 0; p < 2; p++) person(x + (p ? 86 : -86), 186, p);
  box(x - 6, 140, 12, 20, 3, GOLD);
}

static void (*const START[NG])(void) = {pong_start, hockey_start, tanks_start, cycles_start, quick_start, party};
static void (*const STEP_[NG])(void) = {pong_step, hockey_step, tanks_step, cycles_step, quick_step, tug_step};
static void (*const PAINT[NG])(void) = {pong_paint, hockey_paint, tanks_paint, cycles_paint, quick_paint, tug_paint};

/* ------------------------------------------------------------ screens */
enum { S_MENU, S_RULES, S_PLAY, S_PAUSE, S_ASK, S_OVER };
static int screen, sel, back_to, top; /* top: the first game the menu shows */
static uint32_t shown;                /* when the screen came (the clock's time) */

static void save(void) {
  sv.magic = 'P', sv.version = 1, sv.game = (uint8_t)game, sv.cpu = (uint8_t)cpu;
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}
static void new_match(void) {
  score[0] = score[1] = 0, phase = 0, winner = 2, screen = S_PLAY, bot.next = now + 450;
  START[game]();
}
static void play(void) { /* a frame of the match */
  STEP_[game]();
  if (phase == 1 && ago(phase_t) > 1500) {
    phase = 0;
    if (score[0] < TARGET[game] && score[1] < TARGET[game]) bot.next = now + 450, START[game]();
    else phase = 2, screen = S_OVER, sel = 0, sv.wins[game][cpu > 0][score[1] > score[0]]++, save();
  }
}
static const char *opponent(void) {
  static const char *const O[4] = {T("Player 2"), T("Computer: easy"), T("Computer: normal"), T("Computer: hard")};
  return O[cpu];
}

static void tri(int x, int y, int d, color_t c) { /* a little arrow, pointing right, down, left or up */
  for (int i = 0; i < 4; i++) {
    int a = d < 2 ? 3 - i : i;
    if (d & 1) gfx_fill(x - a, y - 2 + i, 2 * a + 1, 1, c);
    else gfx_fill(x - 2 + i, y - a, 1, 2 * a + 1, c);
  }
}
/* The calculator's keyboard, the keys of this game in each player's colour: arrows on the keys
   that move, a dot on the one that acts. */
static void keyboard(int x0, int y0) {
  static const int8_t ARROW[4][2] = {{0, 9}, {13, 0}, {13, 18}, {26, 9}}, DIR[4] = {2, 3, 1, 0};
  box(x0 - 4, y0 - 4, 132, 136, 10, HEX(0x0E1124));
  disc(x0 + 62, y0 + 19, 6, HEX(0x3A4268)); /* Home */
  for (int k = 0; k < 53; k++) {
    int x = x0 + 4, y = y0, w = 12, h = 8, p = -1, b = 0;
    if (k < 4) x = x0 + 6 + ARROW[k][0], y = y0 + 6 + ARROW[k][1];
    else if (k < 6) x += 82 + (k - 4) * 16, y += 15;
    else if (k < 12 || (k >= 30 && (k - 30) % 6 == 5)) continue;
    else if (k < 30) x += (k - 12) % 6 * 20, y += 38 + (k - 12) / 6 * 12, w = 16;
    else x += (k - 30) % 6 * 24, y += 74 + (k - 30) / 6 * 14, w = 20, h = 10;
    for (int q = 0; q < 2; q++)
      for (int i = 0; i < 5; i++)
        if (KEYS[q][i] == k && KEYS_USED[game] >> i & 1 && !is_cpu(q)) p = q, b = i;
    box(x, y, w, h, 3, p < 0 ? HEX(0x3A4268) : team[p]);
    if (p >= 0 && b == 4) disc(x + w / 2, y + h / 2, 2, WHITE);
    else if (p >= 0) tri(x + w / 2, y + h / 2, DIR[b], WHITE);
  }
}

static void button(int x, int y, int w, const char *s, bool on) {
  box(x, y, w, 26, 13, on ? GOLD : HEX(0x3A4372));
  gfx_text_center(&np_font_body, x + w / 2, y + 18, s, on ? INK : WHITE, 256);
}
static void keys_line(int y, const char *s) { say(&np_font_small, 160, y, s, HEX(0xD6DCF5)); }
/* a card over the game: its title, then its buttons from by down, and its keys under it. Over
   the first four games it is drawn in that game's look: Pong's black court and white walls with
   its square ball, the air hockey rink with a mallet, the tanks' grey maze, the light cycles' blue
   grid; over the menu and the party games, in the menu's */
static void card(int y, int h, const char *title, const char *const *items, int n, int by, const char *keys) {
  gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 130);
  keys_line(232, keys);
  if (screen <= S_RULES || (screen == S_ASK && back_to == S_MENU) || game > CYCLES) {
    box(60, y, 200, h, 14, HEX(0x262D52));
    gfx_rrect_outline(60, y, 200, h, 14, 2, HEX(0x56619A), 256);
    say(&np_font_title, 160, y + 30, title, WHITE);
    for (int i = 0; i < n; i++) button(84, by + 32 * i, 152, items[i], sel == i);
    return;
  }
  static const uint32_t LOOK[4][3] = {{0x000000, 0xFFFFFF, 0xFFFFFF}, {0xF4F6FA, 0x9AA3B8, 0x161A33},
                                      {0xE6E6EA, 0x3A3D4F, 0x3A3D4F}, {0x0B1230, 0x3E8BFF, 0xFFFFFF}};
  color_t bg = gfx_rgb(LOOK[game][0]), edge = gfx_rgb(LOOK[game][1]), ink = gfx_rgb(LOOK[game][2]);
  gfx_fill(57, y - 3, 206, h + 6, edge);
  gfx_fill(60, y, 200, h, bg);
  if (game == CYCLES)
    for (int k = 16; k < 200; k += 16) gfx_fill(60 + k, y, 1, h, HEX(0x1C2A55)), k < h ? gfx_fill(60, y + k, 200, 1, HEX(0x1C2A55)) : (void)0;
  say(&np_font_title, 160, y + 30, title, ink);
  for (int i = 0; i < n; i++) {
    int ty = by + 32 * i + 18, x = 160 - gfx_text_width(&np_font_body, items[i]) / 2 - 16;
    gfx_text_center(&np_font_body, 160, ty, items[i], sel == i && game ? team[0] : ink, 256);
    if (sel != i) continue;
    if (game == PONG) gfx_fill(x, ty - 9, 6, 6, WHITE); /* the ball */
    else if (game == HOCKEY) gfx_circle(x + 3, ty - 6, 6, team[0], 256), gfx_circle(x + 3, ty - 6, 3, light[0], 256);
    else gfx_fill(x - 6, ty - 7, 12, 3, team[0]); /* a shot, a trail */
  }
}

/* the little picture of each game in the menu: x, y, w, h, roundness, colour (red, blue, white, gold) */
static const int8_t ICON[][6] = {
  {4, 6, 3, 10, 0, 0}, {19, 10, 3, 10, 0, 1}, {12, 11, 3, 3, 0, 2},                       /* Pong */
  {3, 8, 10, 10, 5, 0}, {14, 6, 10, 10, 5, 1}, {10, 16, 6, 6, 3, 2},                      /* Air Hockey */
  {0, 0, 0, 0, 0, 0},                                                                     /* (a tank) */
  {4, 7, 12, 3, 0, 0}, {13, 7, 3, 12, 0, 0}, {10, 19, 12, 3, 0, 1}, {10, 12, 3, 9, 0, 1}, /* Light Cycles */
  {3, 8, 10, 10, 5, 0}, {13, 8, 10, 10, 5, 1}, {11, 5, 4, 16, 2, 3},                      /* Quick Draw */
  {3, 12, 20, 3, 0, 2}, {11, 8, 5, 11, 2, 3},                                             /* Tug of War */
};
static const uint8_t ICON_AT[NG + 1] = {0, 3, 6, 7, 11, 14, 16};
static void icon(int g, int x, int y) {
  const color_t C[4] = {team[0], team[1], WHITE, GOLD};
  box(x, y, 26, 26, 6, INK);
  if (g == TANKS) tank(x + 13, y + 14, 54, 0);
  else
    for (int i = ICON_AT[g]; i < ICON_AT[g + 1]; i++)
      box(x + ICON[i][0], y + ICON[i][1], ICON[i][2], ICON[i][3], ICON[i][4], C[ICON[i][5]]);
}

#define ROWS 5 /* the games the menu shows at once */
static void scene(void *ctx) {
  char s[24];
  if (screen <= S_RULES || (screen == S_ASK && back_to == S_MENU)) {
    gfx_vgrad(0, 0, 160, SCREEN_H, 0xB8323A, 0x3A0F24); /* the two players' colours */
    gfx_vgrad(160, 0, 160, SCREEN_H, 0x2A5FD0, 0x0F1A44);
    say(&np_font_title, 160, 28, T("2 Players"), WHITE);
    for (int i = 0; i < ROWS; i++) {
      int g = top + i, y = 38 + 33 * i, on = g == game;
      gfx_rrect(24, y, 272, 30, 10, on ? WHITE : INK, on ? 256 : 170);
      icon(g, 26, y + 2);
      gfx_text(&np_font_body, 62, y + 20, NAME[g], on ? INK : WHITE, 256);
      for (int p = 0; p < 2; p++) { /* the matches each side has won */
        num(s, sv.wins[g][cpu > 0][p]);
        gfx_text_center(&np_font_body, 248 + 30 * p, y + 20, s, team[p], 256);
      }
      gfx_text_center(&np_font_body, 263, y + 20, "-", on ? INK : WHITE, 256);
    }
    if (top) tri(306, 44, 3, WHITE); /* more above, more below */
    if (top < NG - ROWS) tri(306, 194, 1, WHITE);
    keys_line(207, T("OK  play   Back  quit"));
    gfx_rrect(40, 212, 240, 24, 12, INK, 200);
    tri(52, 224, 2, WHITE), tri(268, 224, 0, WHITE);
    gfx_text_center(&np_font_body, 160, 229, opponent(), WHITE, 256);
    if (screen == S_RULES) {
      static const char *const KEY[2][5] = {{"", "", "", "", "OK"}, {"3", "x", "Ans", "-", "EXE"}};
      gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 140);
      box(8, 8, 304, 232, 14, HEX(0x262D52));
      say(&np_font_title, 160, 34, NAME[game], WHITE);
      gfx_paragraph(&np_font_small, 160, 54, 284, 12, RULES[game], HEX(0xD6DCF5), 256);
      keyboard(26, 100);
      for (int p = 0; p < 2; p++) { /* who plays, and with which keys */
        int y = 100 + 36 * p, u = KEYS_USED[game], x = 188;
        box(166, y, 14, 14, 4, team[p]);
        gfx_text(&np_font_body, 188, y + 12, p ? opponent() : who(0), WHITE, 256);
        if (!p && u & 15) x = gfx_text(&np_font_small, x, y + 27, T("arrows"), light[p], 256) + 6;
        for (int i = 0; i < 5; i++)
          if (u >> i & 1 && !is_cpu(p)) x = gfx_text(&np_font_small, x, y + 27, KEY[p][i], i < 4 ? light[p] : GOLD, 256) + 6;
      }
      char *e = s;
      for (const char *q = T("First to "); *q; q++) *e++ = *q;
      num(e, TARGET[game]);
      gfx_text_center(&np_font_body, 231, 182, s, GOLD, 256);
      button(166, 190, 130, T("Play"), true);
      say(&np_font_small, 231, 233, T("Back  games"), HEX(0xD6DCF5));
    }
  } else {
    PAINT[game]();
    if (game != PONG) { /* the score bar: each side's name and points, and the pause key */
      gfx_fill(0, 0, SCREEN_W, 22, INK);
      for (int p = 0; p < 2; p++) {
        box(p ? 214 : 4, 3, 102, 17, 8, team[p]);
        gfx_text(&np_font_small, p ? 222 : 12, 15, who(p), WHITE, 256);
        num(s, (uint32_t)score[p]);
        gfx_text_right(&np_font_body, p ? 308 : 98, 16, s, WHITE, 256);
      }
    }
    if (game) gfx_text_center(&np_font_small, 160, 15, T("Back  pause"), HEX(0xD6DCF5), 256);
    if (phase == 1 && screen == S_PLAY) {
      int32_t t = (int32_t)ago(phase_t);
      int y = 120 - (t < 200 ? (200 - t) / 10 : 0);
      gfx_rrect(70, y - 26, 180, 62, 14, INK, 200);
      if (winner < 2) say(&np_font_title, 160, y, who(winner), light[winner]);
      say(&np_font_title, 160, y + 26, winner < 2 ? "+1" : T("Draw!"), WHITE);
    }
    if (screen == S_PAUSE) {
      static const char *const ITEM[4] = {T("Resume"), T("Restart"), T("Other games"), T("Quit game")};
      card(40, 172, T("Paused"), ITEM, 4, 82, T("OK  choose   Back  resume"));
    } else if (screen == S_OVER) {
      static const char *const WON[2][2] = {{T("Player 1 wins!"), T("Player 2 wins!")}, {T("You win!"), T("The computer wins")}};
      static const char *const ITEM[2] = {T("Rematch"), T("Other games")};
      card(46, 158, WON[cpu > 0][score[1] > score[0]], ITEM, 2, 140, T("OK  choose   Back  games"));
      for (int p = 0; p < 2; p++) {
        num(s, (uint32_t)score[p]);
        say(&np_font_title, 124 + 72 * p, 118, s, light[p]);
      }
      say(&np_font_title, 160, 118, "-", WHITE);
    }
  }
  if (screen == S_ASK) {
    static const char *const ITEM[2] = {T("No"), T("Yes")};
    card(60, 120, T("Quit game?"), ITEM, 2, 104, T("OK  choose   Back  no"));
  }
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
#ifdef __TICE__
  gfx_pure = true; /* (draws only with gfx_*) */
#endif
  static const uint32_t TEAM[2] = {0xEF4136, 0x2E7CF6};
  for (int p = 0; p < 2; p++)
    team[p] = gfx_rgb(TEAM[p]), light[p] = gfx_rgb(gfx_lerp888(TEAM[p], 0xFFFFFF, 120)), dark[p] = gfx_rgb(gfx_lerp888(TEAM[p], 0, 110));
  uint32_t last = (uint32_t)eadk_timing_millis(), n = 0;
  seed = eadk_random() ^ last;
  if (!seed) seed = 1;
  const uint8_t *d = np_file_read(SAVE_NAME, &n);
  if (d && n >= 4 && n <= sizeof sv && d[0] == 'P' && d[1] == 1) /* (a save with fewer games: the first ones) */
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&sv)[i] = d[i];
  game = sv.game % NG, cpu = sv.cpu & 3;
  top = clampi(game - 2, 0, NG - ROWS);
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the launcher wait to be let go */
  for (bool quit = false; !quit;) {
    uint32_t t = (uint32_t)eadk_timing_millis();
    int owed = t - last < 120 ? (int)(t - last) - 40 : 80; /* (a slow frame's time past 40 ms: steps of its own) */
    ms = t - last < 40 ? (int)(t - last) : 40, last = t, dt = ms * .001f;
    if (screen == S_PLAY) now += (uint32_t)ms;
    uint64_t k = eadk_keyboard_scan();
    if (cpu) k |= (uint64_t)down(k, eadk_key_exe) << eadk_key_ok; /* alone, EXE is OK: one key, either */
    stale &= k, k &= ~stale;
    uint64_t hit = k & ~held;
    held = k;
    if (down(k, eadk_key_home) || down(k, eadk_key_on_off)) break;
    bool ok = down(hit, eadk_key_ok) || down(hit, eadk_key_exe), back = down(hit, eadk_key_back);
    pad[0] = pad[1] = tap[0] = tap[1] = 0;
    for (int i = 0; i < 10; i++)
      pad[i / 5] |= down(k, KEYS[i / 5][i % 5]) << i % 5, tap[i / 5] |= down(hit, KEYS[i / 5][i % 5]) << i % 5;
    if (cpu) pad[1] = tap[1] = 0; /* (player 2's keys are the computer's) */
    int dx = down(hit, eadk_key_right) - down(hit, eadk_key_left), dy = down(hit, eadk_key_down) - down(hit, eadk_key_up);
    int was = screen;
    switch (screen) {
      case S_MENU:
      case S_RULES:
        if (dx) cpu = (cpu + dx) & 3, save();
        if (screen == S_RULES) {
          if (ok) new_match();
          else if (back) screen = S_MENU;
        } else if (dy) {
          game = (game + dy + NG) % NG, top = clampi(top, game - ROWS + 1, game);
        } else if (ok) screen = S_RULES, save();
        else if (back) back_to = S_MENU, screen = S_ASK, sel = 0;
        break;
      case S_PLAY:
        if (back) {
          screen = S_PAUSE, sel = 0;
          break;
        }
        play();
        for (; owed > 0 && screen == S_PLAY; owed -= 40) { /* the game keeps its pace however slow the frames */
          ms = owed < 40 ? owed : 40, dt = ms * .001f, now += (uint32_t)ms;
          tap[0] = tap[1] = 0;
          play();
        }
        break;
      default: /* the cards: Paused, the match over, Quit game? */
        sel = (sel + dy) & (screen == S_PAUSE ? 3 : 1);
        if (screen == S_OVER && t - shown < 700) break; /* (a key still being mashed doesn't choose) */
        if (screen == S_ASK) {
          if (back || (ok && !sel)) screen = back_to;
          else quit = ok;
        } else if (back) {
          screen = screen == S_PAUSE ? S_PLAY : S_MENU;
        } else if (ok) { /* Resume, Restart, Other games, Quit game; Rematch, Other games */
          int a = screen == S_PAUSE ? sel : sel ? 2 : 1;
          if (a == 1) new_match();
          else screen = a ? a == 2 ? S_MENU : S_ASK : S_PLAY, back_to = S_PAUSE, sel = 0;
        }
    }
    if (screen != was) shown = t;
    eadk_display_wait_for_vblank();
    gfx_render(scene, 0, 0, SCREEN_H);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
