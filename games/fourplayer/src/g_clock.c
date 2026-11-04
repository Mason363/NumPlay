/* Stop the Clock: a stopwatch runs, then hides after two seconds. Tap your key when you think it shows the
 * time asked: the closest takes the round. */
#include "fp.h"

#define SHOW 2000 /* ms the stopwatch shows */
#define CX 160.0f
#define CY 124.0f
static int32_t target, stopped[NMAX], reveal_t;

static void start(void) {
  target = 5000 + rn(6) * 1000, reveal_t = -1;
  for (int p = 0; p < NMAX; p++) stopped[p] = -1;
}
static int32_t off(int p) { return stopped[p] > target ? stopped[p] - target : target - stopped[p]; }
static void step(void) {
  if (reveal_t >= 0) return;
  int waiting = 0;
  for (int p = 0; p < np; p++) {
    if (tap[p] && stopped[p] < 0) stopped[p] = rt;
    waiting += stopped[p] < 0;
  }
  if (waiting && rt < target + 4000) return;
  reveal_t = rt;
  int best = -1, n = 0;
  for (int p = 0; p < np; p++) {
    if (stopped[p] < 0) continue;
    if (best < 0 || off(p) < off(best)) best = p, n = 1;
    else if (off(p) == off(best) && side(p) != side(best)) n++;
  }
  if (best >= 0 && n == 1) win(best);
  else tie();
}

static void time_text(char *s, int32_t ms) { /* 6.87 */
  char *e = num(s, (uint32_t)(ms / 1000));
  int c = (int)(ms % 1000 / 10);
  e[0] = '.', e[1] = (char)('0' + c / 10), e[2] = (char)('0' + c % 10), e[3] = 0;
}
static void draw(void) {
  char s[24];
  gfx_vgrad(0, TOP, SCREEN_W, SCREEN_H - TOP, 0x3A2F6E, 0x1A1538);
  int32_t now = rt < 0 ? 0 : rt;
  bool shown = now < SHOW || reveal_t >= 0;
  char *e = s;
  for (const char *q = "Stop at "; *q; q++) *e++ = *q;
  time_text(e, target);
  say(&np_font_body, 160, TOP + 20, s, GOLD);
  disc(CX, CY - 44, 6, HEX(0xC9C3E8)); /* the stopwatch */
  disc(CX + 2, CY + 3, 44, HEX(0x0F0C22));
  disc(CX, CY, 44, HEX(0xC9C3E8));
  disc(CX, CY, 39, WHITE);
  for (int k = 0; k < 12; k++) {
    float a = k * PI / 6;
    line(CX + fcos(a) * 33, CY + fsin(a) * 33, CX + fcos(a) * 37, CY + fsin(a) * 37, k % 3 ? 1.2f : 2.2f, HEX(0x5A5378));
  }
  if (shown) {
    int32_t t = reveal_t >= 0 ? target : now;
    float a = t * (2 * PI / 10000) - PI / 2;
    line(CX, CY, CX + fcos(a) * 30, CY + fsin(a) * 30, 2.2f, HEX(0xE8433E));
    time_text(s, reveal_t >= 0 ? target : now);
  } else {
    disc(CX, CY, 33, HEX(0xE6E2F5));
    s[0] = '?', s[1] = '.', s[2] = '?', s[3] = '?', s[4] = 0;
  }
  disc(CX, CY, 3, HEX(0x2A2440));
  gfx_text_center(&np_font_title, (int)CX, (int)CY + 26, s, HEX(0x2A2440), 256);
  for (int p = 0; p < np; p++) {
    float a = seat(p), x = CX + fcos(a) * 112, y = CY + 4 + fsin(a) * 66;
    if (y > 214) y = 214;
    blob(x, y, 11, p, fatan2(CY - y, CX - x));
    tag_at(p, x, y - 11);
    int by = (int)y + 24;
    if (reveal_t >= 0 && stopped[p] >= 0) {
      time_text(s, stopped[p]);
      say(&np_font_body, (int)x, by, s, lite[p]);
      char d[12];
      d[0] = stopped[p] >= target ? '+' : '-';
      time_text(d + 1, off(p));
      gfx_text_center(&np_font_small, (int)x, by + 12, d, WHITE, 256);
    } else if (reveal_t >= 0) {
      say(&np_font_small, (int)x, by, "No time", WHITE);
    } else if (stopped[p] >= 0) {
      box((int)x - 20, by - 11, 40, 14, 7, col[p]);
      gfx_text_center(&np_font_small, (int)x, by, "Stop", WHITE, 256);
    }
  }
}

static void icon(int x, int y) {
  box(x + 2, y + 1, 52, 32, 7, HEX(0x3A2F6E));
  disc(x + 28, y + 18, 13, HEX(0xC9C3E8));
  disc(x + 28, y + 18, 11, WHITE);
  disc(x + 28, y + 4, 3, HEX(0xC9C3E8));
  line(x + 28, y + 18, x + 34, y + 12, 2, HEX(0xE8433E));
  disc(x + 10, y + 26, 4, col[0]), disc(x + 46, y + 26, 4, col[1]);
}

#ifdef HOST
static int bot(int p) {
  int32_t when = target + (int32_t)((rn(1000) + rn(1000) - 1000) * 0.5f) + p * 7;
  return bot_tap(p, stopped[p] < 0 && rt >= when - 200 && rt <= when + 200 && rn(4) == 0);
}
#endif

const game_t g_clock = {"Stop the Clock", "Clock", "Tap  stop",
                        "The stopwatch hides after two seconds. Tap your key when you think it shows the time asked. "
                        "The closest takes the round.",
                        0, start, step, draw, icon BOT(bot)};
