/* 2 3 4 Players: the screens (players, games, rules, the rounds, the scores, pause), the matches and the save.
 *
 * Every player has one key, spread around the keypad: shift and backspace at the top corners, 0 and EXE at the
 * bottom ones, 7 and ) on the sides for 3 v 3. Every minigame is played with that key alone. */
#include <string.h>
#include "app.h"
#include "fp.h"
#include "plat.h"

const game_t *const GAMES[NG] = {&g_sumo,  &g_race,    &g_tanks,   &g_snakes, &g_soccer, &g_fish,  &g_skate,
                                 &g_rope,  &g_paint,   &g_balloon, &g_birds,  &g_gravity, &g_frog, &g_clock};
#define MIX NG /* (the menu's first tile: another game every round) */

/* ---- the players */
int np = 2;
bool teams;
uint8_t hold[NMAX], tap[NMAX], rel[NMAX], gone[NMAX];
int32_t rt;
color_t col[NMAX], lite[NMAX], dark[NMAX];

static const uint8_t PKEY[NMAX] = {FK_SHIFT, FK_BACKSPACE, FK_ZERO, FK_EXE, FK_SEVEN, FK_RPAREN};
static const char *const KEYNAME[NMAX] = {"shift", "", "0", "EXE", "7", ")"};
static const uint32_t FFA_RGB[4] = {0xF2453D, 0x2F7FF0, 0x2DB653, 0xF5B400};
static const uint32_t TEAM_RGB[NMAX] = {0xF2453D, 0x2F7FF0, 0xFF8A1F, 0x1FC2D6, 0xF25FA8, 0x8E5CF0};
static const char *const FFA_NAME[4] = {"Red", "Blue", "Green", "Yellow"};
static const char *const TEAM_NAME[2] = {"Red team", "Blue team"};
#define NMODES 5
static const uint8_t MODE_N[NMODES] = {2, 3, 4, 4, 6};
static const char *const MODE_NAME[NMODES] = {"2 players", "3 players", "4 players", "2 v 2", "3 v 3"};

const char *keyname(int p) { return KEYNAME[p]; }
int side(int p) { return teams ? p & 1 : p; }
float seat(int p) {
  static const int16_t DEG[4][NMAX] = {{180, 0}, {210, 330, 90}, {225, 315, 135, 45}, {240, 300, 120, 60, 180, 0}};
  return DEG[np == 2 ? 0 : np == 3 ? 1 : np == 4 ? 2 : 3][p] * (PI / 180);
}

/* ---- the save: the mode, the game, the target, and the matches each colour won, each game and Mix */
#define SAVE_NAME "fourplayer.sav"
#define COPY_NAME "fourplayer_saves.py"
#define VERSION 1
typedef struct {
  char magic[4];                       /* "FOUR" */
  uint8_t version, mode, game, target; /* game: the tile last chosen (NG: Mix); target: first to */
  uint16_t wins[NG + 1][6];            /* red, blue, green, yellow (2 to 4 players), the red and blue teams */
} save_t;
static save_t sv;

static const char copy_head[] = "# 2 3 4 Players' saves: keep this file\n#>" SAVE_NAME ":";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#define COPY_SIZE (1 + sizeof copy_head - 1 + (sizeof(save_t) + 2) / 3 * 4 + 2)

/* the save, and its copy in a Python script: installing apps from the NumWorks website keeps only scripts */
static void save(void) {
  static uint8_t copy[COPY_SIZE];
  memcpy(sv.magic, "FOUR", 4), sv.version = VERSION;
  plat_save(SAVE_NAME, &sv, sizeof sv);
  const uint8_t *d = (const uint8_t *)&sv;
  uint8_t *p = copy;
  *p++ = 0; /* (a script's first byte: not imported) */
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  for (uint32_t i = 0; i < sizeof sv; i += 3, p += 4) {
    uint32_t left = (uint32_t)sizeof sv - i;
    uint32_t v = (uint32_t)d[i] << 16 | (uint32_t)(left > 1 ? d[i + 1] : 0) << 8 | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63], p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  *p++ = 0;
  plat_save(COPY_NAME, copy, (uint32_t)(p - copy));
}
static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}
static bool usable(const uint8_t *d, uint32_t n) { return d && n == sizeof sv && !memcmp(d, "FOUR", 4) && d[4] == VERSION; }
static void load(void) {
  uint32_t n;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  static save_t from_copy;
  if (!usable(d, n)) { /* the copy, then */
    d = NULL;
    const uint8_t *c = plat_load(COPY_NAME, &n);
    uint32_t head = 1 + sizeof copy_head - 1, len = (sizeof(save_t) + 2) / 3 * 4;
    if (c && n >= head + len && !memcmp(c + 1, copy_head, sizeof copy_head - 1)) {
      uint8_t *o = (uint8_t *)&from_copy;
      uint32_t k = 0;
      bool ok = true;
      for (uint32_t i = 0; i < len && ok; i += 4) {
        int v[4];
        for (int j = 0; j < 4; j++) {
          uint8_t q = c[head + i + j];
          v[j] = q == '=' ? 0 : b64_value(q);
          ok &= v[j] >= 0;
        }
        uint32_t w = (uint32_t)(v[0] & 63) << 18 | (uint32_t)(v[1] & 63) << 12 | (uint32_t)(v[2] & 63) << 6 | (uint32_t)(v[3] & 63);
        for (int j = 0; j < 3 && k < sizeof(save_t); j++) o[k++] = (uint8_t)(w >> (16 - 8 * j));
      }
      if (ok && usable((const uint8_t *)&from_copy, sizeof from_copy)) d = (const uint8_t *)&from_copy;
    }
  }
  if (d) memcpy(&sv, d, sizeof sv);
  else memset(&sv, 0, sizeof sv), sv.mode = 2, sv.target = 3, sv.game = 0;
  /* whatever the file held: settings this game could have written */
  if (sv.mode >= NMODES) sv.mode = 2;
  if (sv.game > NG) sv.game = 0;
  if (sv.target < 1 || sv.target > 9) sv.target = 3;
}

/* ---- the app */
#ifndef HOST /* (app.h names them for the tests) */
enum { S_MODE, S_GAMES, S_RULES, S_PLAY, S_SCORE, S_OVER, S_PAUSE, S_ASK };
#endif
#define COUNTDOWN 2000 /* ms of 3, 2, 1 before a round */
#define ENDING 1300    /* ms the game plays on once a round is decided */
#define BANNER 1100    /* ms its winner shows */
#define SCORE_MS 3000  /* the scores between rounds */
static struct {
  int screen, sel, under;   /* under: the screen under Paused or Quit game? */
  int cur;                  /* the game playing */
  int nsides, score[4], round, winner, matches; /* winner: the round's side, -1 a draw */
  bool decided, started, dirty;
  int32_t end_t, freeze;    /* freeze: ms of countdown left after Resume */
  uint32_t now, shown, acc;
  uint8_t ready, prev_h[NMAX];
  uint64_t held, stale;
  float tag_x[NMAX], tag_y[NMAX];
  int8_t tag_side[NMAX];
} u;

static int nsides(void) { return teams ? 2 : np; }
static int slot(int s) { return teams ? 4 + s : s; } /* a side's place in the wins */
static const char *side_name(int s) { return teams ? TEAM_NAME[s] : FFA_NAME[s]; }
static const game_t *G(void) { return GAMES[u.cur]; }

static void set_mode(int m) {
  np = MODE_N[m], teams = m >= 3;
  for (int p = 0; p < NMAX; p++) {
    uint32_t c = teams ? TEAM_RGB[p] : FFA_RGB[p & 3];
    col[p] = gfx_rgb(c), lite[p] = gfx_rgb(gfx_lerp888(c, 0xFFFFFF, 120)), dark[p] = gfx_rgb(gfx_lerp888(c, 0, 80));
  }
  u.nsides = nsides();
}
static void open(int s) {
  u.screen = s, u.shown = u.now, u.dirty = true;
  if (s == S_PAUSE || s == S_ASK || s == S_OVER) u.sel = 0;
}

/* ---- the round */
void tag_by(int p, float x, float y, int side) { u.tag_x[p] = x, u.tag_y[p] = y, u.tag_side[p] = (int8_t)side; }
void tag_at(int p, float x, float y) { tag_by(p, x, y, 0); }
void out(int p) { gone[p] = 1; }
int alive(void) {
  int n = 0;
  for (int p = 0; p < np; p++) n += !gone[p];
  return n;
}
static void decide(int s) {
  if (u.decided) return;
  u.decided = true, u.winner = s, u.end_t = rt;
  if (s >= 0) u.score[s]++;
}
void win(int p) { decide(side(p)); }
void tie(void) { decide(-1); }
bool over(void) { return u.decided; }

static void begin_round(void) {
  memset(gone, 0, sizeof gone);
  memset(hold, 0, sizeof hold), memset(tap, 0, sizeof tap), memset(rel, 0, sizeof rel);
  for (int p = 0; p < NMAX; p++) u.tag_x[p] = -1000;
  u.decided = false, u.winner = -1, u.freeze = 0, u.acc = 0;
  rt = -COUNTDOWN;
  G()->start();
  open(S_PLAY);
#ifdef HOST
  extern int fp_round_serial;
  fp_round_serial++;
#endif
}
static int mix_pick(void) {
  int g;
  do g = rn(NG); while (g == u.cur && NG > 1);
  return g;
}
static void new_match(void) {
  memset(u.score, 0, sizeof u.score);
  u.round = 0, u.started = false, u.ready = 0;
  u.cur = sv.game == MIX ? mix_pick() : sv.game;
  open(S_RULES);
}
/* the round's winner seen: the scores, or the match is over */
static void after_banner(void) {
  int s = u.winner;
  if (s >= 0 && u.score[s] >= sv.target) {
    if (sv.wins[sv.game][slot(s)] < 0xFFFF) sv.wins[sv.game][slot(s)]++;
    u.matches++;
    save();
    open(S_OVER);
  } else {
    open(S_SCORE);
  }
}
static void next_round(void) {
  u.round++;
  if (sv.game == MIX && u.winner >= 0) { /* (a draw is played again) */
    u.cur = mix_pick(), u.ready = 0;
    open(S_RULES);
  } else {
    begin_round();
  }
}

/* a step of the game, with each player's key held, just pressed, just let go */
static void game_step(const uint8_t *h, const uint8_t *t, const uint8_t *r) {
  for (int p = 0; p < np; p++) {
    bool on = !u.decided && !gone[p];
    hold[p] = on && h[p], tap[p] = on && t[p], rel[p] = on && r[p];
  }
  G()->step();
  if (G()->last && !u.decided) { /* the last side standing */
    int left = -1;
    bool one = true;
    for (int p = 0; p < np; p++)
      if (!gone[p]) {
        if (left < 0) left = side(p);
        else one &= side(p) == left;
      }
    if (left < 0) tie();
    else if (one) decide(left);
  }
}

/* a frame of play: the game moved on in steps of TICK ms, the keys read once a frame */
static void play(uint32_t ms) {
  uint8_t now_h[NMAX], t_[NMAX], r_[NMAX];
  for (int p = 0; p < np; p++) {
    now_h[p] = (u.held & KEY(PKEY[p])) != 0;
    t_[p] = now_h[p] && !u.prev_h[p], r_[p] = !now_h[p] && u.prev_h[p];
    u.prev_h[p] = now_h[p];
  }
  u.acc += ms > 100 ? 100 : ms;
  bool first = true;
  while (u.acc >= TICK) {
    u.acc -= TICK;
    if (u.freeze > 0) {
      u.freeze -= TICK;
      continue;
    }
    if (u.decided && rt - u.end_t >= ENDING + BANNER) {
      after_banner();
      return;
    }
    rt += TICK;
    if (rt <= 0 || (u.decided && rt - u.end_t >= ENDING)) continue; /* the countdown; the winner showing */
    static const uint8_t none[NMAX];
    game_step(now_h, first ? t_ : none, first ? r_ : none);
    first = false;
  }
}

/* ---- drawing */
#define BG_TOP 0x2B2452
#define BG_BOT 0x100D22
#define CARD HEX(0x2A2350)
#define EDGE HEX(0x5A4F96)
#define DIM HEX(0x3A3363)
#define SOFT HEX(0xCFC9F2)

static void backdrop(void) { gfx_vgrad(0, 0, SCREEN_W, SCREEN_H, BG_TOP, BG_BOT); }
static void keys_line(int y, const char *s) { say(&np_font_small, 160, y, s, SOFT); }
static void title(int cx, int base) { /* 2 3 4 in three players' colours */
  static const char *const W[4] = {"2", "3", "4", "Players"};
  int w = 0, sp = 7;
  for (int i = 0; i < 4; i++) w += gfx_text_width(&np_font_title, W[i]) + (i < 3 ? sp : 0);
  int x = cx - w / 2;
  for (int i = 0; i < 4; i++) {
    gfx_text(&np_font_title, x + 1, base + 2, W[i], 0, 140);
    x = gfx_text(&np_font_title, x, base, W[i], i < 3 ? gfx_rgb(FFA_RGB[i]) : WHITE, 256) + sp;
  }
}
static void button(int x, int y, int w, const char *s, bool on) {
  box(x, y, w, 26, 13, on ? GOLD : HEX(0x3D3570));
  gfx_text_center(&np_font_body, x + w / 2, y + 18, s, on ? INK : WHITE, 256);
}
static void card(int y, int h) {
  gfx_shadow(40, y, 240, h, 16, 10, 200);
  box(40, y, 240, h, 16, CARD);
  gfx_rrect_outline(40, y, 240, h, 16, 2, EDGE, 256);
}
static void tri(int x, int y, int d, color_t c) { /* a little arrow: right, down, left, up */
  for (int i = 0; i < 4; i++) {
    int a = d < 2 ? 3 - i : i;
    if (d & 1) gfx_fill(x - a, y - 2 + i, 2 * a + 1, 1, c);
    else gfx_fill(x - 2 + i, y - a, 1, 2 * a + 1, c);
  }
}
static void tick_sign(int x, int y, color_t c) {
  line(x - 4, y, x - 1, y + 3, 2.2f, c);
  line(x - 1, y + 3, x + 5, y - 4, 2.2f, c);
}

/* the calculator's keypad, 112 x 136, with each player's key in their colour (lit: those to show) */
static void key_at(int p, int x0, int y0, int *x, int *y, int *w, int *h) {
  static const uint8_t ROW[NMAX] = {0, 0, 6, 6, 3, 3}, COLM[NMAX] = {0, 5, 0, 4, 0, 4};
  int r = ROW[p], c = COLM[p];
  if (r < 3) *x = x0 + 5 + c * 172 / 10, *y = y0 + 34 + r * 11, *w = 15, *h = 8;
  else *x = x0 + 5 + c * 206 / 10, *y = y0 + 69 + (r - 3) * 16, *w = 18, *h = 12;
}
static void keypad(int x0, int y0, int n, uint8_t ready) {
  gfx_shadow(x0, y0, 112, 136, 12, 8, 160);
  box(x0, y0, 112, 136, 12, HEX(0x0D0B1C));
  disc(x0 + 21, y0 + 17, 12, DIM); /* the arrows */
  disc(x0 + 21, y0 + 17, 5, HEX(0x0D0B1C));
  disc(x0 + 56, y0 + 11, 5, DIM);  /* Home */
  disc(x0 + 86, y0 + 12, 6, DIM);  /* OK, Back */
  disc(x0 + 99, y0 + 24, 6, DIM);
  for (int r = 0; r < 7; r++)
    for (int c = 0; c < (r < 3 ? 6 : 5); c++) {
      int x = r < 3 ? x0 + 5 + c * 172 / 10 : x0 + 5 + c * 206 / 10, y = r < 3 ? y0 + 34 + r * 11 : y0 + 69 + (r - 3) * 16;
      box(x, y, r < 3 ? 15 : 18, r < 3 ? 8 : 12, 3, DIM);
    }
  for (int p = 0; p < n; p++) {
    int x, y, w, h;
    key_at(p, x0, y0, &x, &y, &w, &h);
    if (ready >> p & 1) gfx_rrect(x - 2, y - 2, w + 4, h + 4, 5, WHITE, 256);
    box(x, y, w, h, 3, col[p]);
    gfx_fill_alpha(x + 2, y + 1, w - 4, 2, WHITE, 90);
  }
}
/* a side's chip in the score bar */
static void chip(int x, int w, int s) {
  char t[6];
  box(x, 3, w, 17, 8, col[s]);
  if (teams) gfx_text(&np_font_small, x + 8, 15, TEAM_NAME[s], WHITE, 256);
  else keytag(x + 8 + keytag_w(s) / 2, 16, s, WHITE);
  num(t, (uint32_t)u.score[s]);
  gfx_text_right(&np_font_body, x + w - 7, 16, t, WHITE, 256);
}
static void hud(void) {
  if (gfx_y0 >= TOP) return;
  gfx_fill(0, 0, SCREEN_W, TOP, INK);
  int ns = u.nsides, w = ns <= 2 ? 104 : ns == 3 ? 66 : 56, l = 0, r = 0;
  for (int s = 0; s < ns; s++) /* keys on the left of the keypad on the left, the others on the right */
    if (!(s & 1)) chip(3 + (w + 3) * l++, w, s);
    else chip(SCREEN_W - 3 - w - (w + 3) * r++, w, s);
  gfx_text_center(&np_font_small, (3 + (w + 3) * l + SCREEN_W - 3 - (w + 3) * r) / 2, 15, "Back  pause", SOFT, 256);
}
/* the players' keys by them during the countdown: over them, or to their left or right */
static void key_tags(void) {
  for (int p = 0; p < np; p++) {
    if (u.tag_x[p] < -500) continue;
    int w = keytag_w(p) + 10, side = u.tag_side[p], tx = (int)u.tag_x[p], ty = (int)u.tag_y[p];
    int x = side ? (side < 0 ? tx - 10 - w : tx + 10) : tx - w / 2, y = side ? ty - 8 : ty - 22;
    x = clampi(x, 2, SCREEN_W - 2 - w), y = clampi(y, TOP + 2, 220);
    box(x, y, w, 17, 6, col[p]);
    if (side) gfx_fill(side < 0 ? x + w : x - 2, y + 6, 2, 5, col[p]);
    else gfx_fill(x + w / 2 - 3, y + 17, 6, 2, col[p]);
    keytag(x + w / 2, y + 13, p, WHITE);
  }
}
static void countdown(int32_t left) { /* 3, 2, 1 */
  char s[2] = {(char)('1' + left / 667), 0};
  int k = (int)(left % 667);
  disc_a(160, 128, 34, INK, 200);
  say(&np_font_title, 160, 136, s, k > 560 ? GOLD : WHITE);
  int w = gfx_text_width(&np_font_body, G()->hint) + 24;
  gfx_rrect(160 - w / 2, 170, w, 22, 11, INK, 210);
  gfx_text_center(&np_font_body, 160, 185, G()->hint, WHITE, 256);
}

static void pips(int x, int y, int s, bool pop) { /* a side's rounds: won ones filled */
  int n = sv.target, gap = n > 7 ? 13 : 17;
  for (int i = 0; i < n; i++) {
    int on = i < u.score[s], big = pop && i == u.score[s] - 1;
    disc(x + i * gap, y, big ? 7 : 6, on ? col[s] : HEX(0x1A1636));
    if (on) disc_a(x + i * gap - 2, y - 2, 2, WHITE, 140);
  }
}
static void scores(int y0, bool pop) {
  for (int s = 0; s < u.nsides; s++) {
    int y = y0 + s * (u.nsides > 3 ? 22 : 26);
    blob(66, y, 8, s, 0);
    int e = gfx_text(&np_font_body, 82, y + 5, side_name(s), s == u.winner ? GOLD : WHITE, 256);
    if (!teams) keytag(e + 8 + keytag_w(s) / 2, y + 5, s, SOFT);
    pips(176, y, s, pop && s == u.winner);
  }
}

/* the menu's tiles: Mix, then the games */
#define TW 58
#define TH 52
static void tile_xy(int i, int *x, int *y) { *x = 7 + i % 5 * (TW + 4), *y = 36 + i / 5 * (TH + 4); }
static void mix_icon(int x, int y) {
  static const int8_t D[4][2] = {{14, 10}, {42, 10}, {14, 26}, {42, 26}};
  for (int i = 0; i < 4; i++) {
    box(x + D[i][0] - 9, y + D[i][1] - 7, 18, 14, 4, gfx_rgb(FFA_RGB[i]));
    disc(x + D[i][0] - 3, y + D[i][1] - 1, 2, WHITE), disc(x + D[i][0] + 3, y + D[i][1] - 1, 2, WHITE);
  }
  disc(x + 28, y + 18, 8, WHITE);
  gfx_text_center(&np_font_body, x + 28, y + 23, "?", INK, 256);
}
static void tiles(void) {
  for (int i = 0; i <= NG; i++) {
    int x, y, g = i ? i - 1 : MIX, on = g == sv.game;
    tile_xy(i, &x, &y);
    if (y > gfx_y1 || y + TH < gfx_y0) continue;
    if (on) gfx_rrect(x - 3, y - 3, TW + 6, TH + 6, 11, GOLD, 256);
    box(x, y, TW, TH, 9, on ? HEX(0x4A4086) : HEX(0x2C2654));
    gfx_fill_alpha(x + 1, y + 1, TW - 2, 36, 0, 60);
    if (g == MIX) mix_icon(x + 1, y + 2);
    else GAMES[g]->icon(x + 1, y + 2);
    gfx_text_center(&np_font_small, x + TW / 2, y + 48, g == MIX ? "Mix" : GAMES[g]->shortname, WHITE, 256);
  }
}
static void wins_line(int cx, int base) {
  char t[8];
  int n = teams ? 2 : 4, w = 0, at[4];
  for (int i = 0; i < n; i++) {
    num(t, sv.wins[sv.game][teams ? 4 + i : i]);
    at[i] = w, w += 16 + gfx_text_width(&np_font_body, t) + 12;
  }
  int x = cx - w / 2;
  for (int i = 0; i < n; i++) {
    num(t, sv.wins[sv.game][teams ? 4 + i : i]);
    disc(x + at[i] + 6, base - 4, 6, gfx_rgb(teams ? TEAM_RGB[i] : FFA_RGB[i]));
    gfx_text(&np_font_body, x + at[i] + 16, base, t, WHITE, 256);
  }
}

static void scene(void *ctx) {
  char s[24];
  int sc = u.screen, base = sc >= S_PAUSE ? u.under : sc;
  if (base == S_MODE) {
    backdrop();
    title(160, 30);
    for (int m = 0; m < NMODES; m++) {
      int y = 46 + m * 35, on = m == sv.mode, n = MODE_N[m];
      box(14, y, 162, 29, 14, on ? GOLD : HEX(0x3D3570));
      gfx_text(&np_font_body, 26, y + 20, MODE_NAME[m], on ? INK : WHITE, 256);
      for (int i = 0; i < n; i++) { /* its players' colours, the teams apart */
        int x = 168 - (n - i) * 11 - (m >= 3 && i < n / 2 ? 6 : 0), p = m >= 3 ? (i < n / 2 ? 2 * i : 2 * (i - n / 2) + 1) : i;
        disc(x, y + 14, 5, gfx_rgb(m >= 3 ? TEAM_RGB[p] : FFA_RGB[p]));
      }
    }
    keypad(194, 44, np, 0);
    keys_line(233, "OK  choose    Back  quit");
  } else if (base == S_GAMES) {
    backdrop();
    gfx_text(&np_font_body, 12, 24, "Pick a game", WHITE, 256);
    gfx_text_right(&np_font_small, 308, 23, "OK  play    Back  players", SOFT, 256);
    tiles();
    const char *name = sv.game == MIX ? "Mix: another game every round" : GAMES[sv.game]->name;
    say(&np_font_body, 160, 218, name, GOLD);
    wins_line(160, 235);
  } else if (base == S_RULES) {
    backdrop();
    say(&np_font_title, 160, 28, G()->name, WHITE);
    if (sv.game == MIX) gfx_text_right(&np_font_small, 312, 16, u.started ? "Mix: next game" : "Mix", GOLD, 256);
    int y = gfx_paragraph(&np_font_small, 160, 46, 300, 12, G()->rules, SOFT, 256);
    int top = y < 84 ? 84 : y - 4;
    keypad(10, top, np, u.ready);
    int rows = np, gap = rows > 4 ? 19 : 24;
    for (int i = 0; i < rows; i++) { /* the players, teams together */
      int p = teams ? (i < np / 2 ? 2 * i : 2 * (i - np / 2) + 1) : i, ry = top + 10 + i * gap + (teams && i >= np / 2 ? 4 : 0);
      bool r = u.ready >> p & 1;
      blob(140, ry, 7, p, 0);
      keytag(170, ry + 4, p, lite[p]);
      if (r) {
        box(196, ry - 9, 100, 18, 9, col[p]);
        tick_sign(210, ry, WHITE);
        gfx_text(&np_font_small, 222, ry + 4, "Ready", WHITE, 256);
      } else {
        gfx_rrect_outline(196, ry - 9, 100, 18, 9, 1, DIM, 256);
        gfx_text(&np_font_small, 206, ry + 4, "Press your key", SOFT, 256);
      }
    }
    if (!u.started) { /* the target, until the match starts */
      char *e = s;
      for (const char *q = "First to "; *q; q++) *e++ = *q;
      num(e, sv.target);
      int fy = top + 10 + rows * gap + 6;
      if (fy > 214) fy = 214;
      tri(144, fy, 2, SOFT), tri(296, fy, 0, SOFT);
      gfx_text_center(&np_font_body, 220, fy + 5, s, GOLD, 256);
    }
    keys_line(233, u.started ? "OK  start    Back  menu" : "OK  start    Back  games");
  } else if (base == S_PLAY || base == S_SCORE || base == S_OVER) {
    G()->draw();
    if (rt <= 0 && base == S_PLAY) key_tags();
    hud();
    if (base == S_PLAY) {
      if (rt <= 0) countdown(-rt);
      else if (u.freeze > 0) countdown(u.freeze * (COUNTDOWN - 1) / 1200);
      else if (rt < 600 && !u.decided) say(&np_font_title, 160, 136, "GO!", GOLD);
      if (u.decided && rt - u.end_t >= ENDING) { /* the round's winner */
        int32_t k = rt - u.end_t - ENDING;
        int y = 104 - (k < 160 ? (160 - k) / 8 : 0);
        gfx_rrect(60, y, 200, 58, 16, INK, 225);
        if (u.winner >= 0) {
          say(&np_font_title, 160, y + 28, side_name(u.winner), lite[u.winner]);
          gfx_text_center(&np_font_body, 160, y + 46, "takes the round", WHITE, 256);
        } else {
          say(&np_font_title, 160, y + 28, "Draw", WHITE);
          gfx_text_center(&np_font_body, 160, y + 46, "Play it again", SOFT, 256);
        }
      }
    }
    if (base == S_SCORE || base == S_OVER) gfx_fill_alpha(0, TOP, SCREEN_W, SCREEN_H - TOP, 0, 110);
    if (base == S_SCORE) {
      int h = 64 + u.nsides * (u.nsides > 3 ? 22 : 26);
      card(120 - h / 2, h);
      char *e = s;
      for (const char *q = u.winner >= 0 ? "Round " : "Draw, round "; *q; q++) *e++ = *q;
      num(e, (uint32_t)u.round + 1);
      say(&np_font_body, 160, 120 - h / 2 + 24, s, WHITE);
      scores(120 - h / 2 + 46, u.now - u.shown < 400);
      keys_line(120 + h / 2 - 8, "OK  next");
    } else if (base == S_OVER) {
      int w = u.winner, h = 100 + u.nsides * (u.nsides > 3 ? 22 : 26);
      card(118 - h / 2, h);
      say(&np_font_title, 160, 118 - h / 2 + 32, side_name(w), lite[w]);
      gfx_text_center(&np_font_body, 160, 118 - h / 2 + 50, "wins the match!", WHITE, 256);
      scores(118 - h / 2 + 68, false);
      static const char *const ITEM[2] = {"Rematch", "Other games"};
      for (int i = 0; i < 2; i++) button(48 + i * 116, 118 + h / 2 - 34, 108, ITEM[i], u.sel == i && sc == S_OVER);
    }
  }
  if (sc == S_PAUSE) {
    static const char *const ITEM[4] = {"Resume", "Restart", "Other games", "Quit game"};
    gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 120);
    card(36, 176);
    say(&np_font_title, 160, 66, "Paused", WHITE);
    for (int i = 0; i < 4; i++) button(84, 80 + 32 * i, 152, ITEM[i], u.sel == i);
    keys_line(232, "OK  choose    Back  resume");
  } else if (sc == S_ASK) {
    static const char *const ITEM[2] = {"No", "Yes"};
    gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 120);
    card(60, 120);
    say(&np_font_title, 160, 92, "Quit game?", WHITE);
    for (int i = 0; i < 2; i++) button(84, 104 + 32 * i, 152, ITEM[i], u.sel == i);
    keys_line(232, "OK  choose    Back  no");
  }
  (void)ctx;
}

/* ---- the keys */
static color_t strip[SCREEN_W * STRIP_H];
void np_push(int x, int y, int w, int h, const color_t *px) { plat_push(x, y, w, h, px); }
#ifndef NP_PORT /* (the other targets bring their own: platform/common/np_alloc.c) */
void *np_alloc(uint32_t size) { /* (gfx_init's: never called, the strip is ours) */
  (void)size;
  return strip;
}
#endif

void app_init(uint32_t now) {
  extern uint32_t fp_seed;
  memset(&u, 0, sizeof u);
  gfx_buf = strip;
  fp_seed = plat_random() ^ now * 2654435761u;
  if (!fp_seed) fp_seed = 1;
  load();
  set_mode(sv.mode);
  u.now = now, u.cur = sv.game < NG ? sv.game : 0;
  u.stale = plat_keys(); /* keys still down from the launcher wait to be let go */
  open(S_MODE);
}
bool app_dirty(void) { return u.dirty; }

bool app_step(uint64_t keys, uint32_t now) {
  uint32_t ms = now - u.now;
  u.now = now;
  u.stale &= keys, keys &= ~u.stale;
  uint64_t hit = keys & ~u.held;
  u.held = keys;
  if (hit) {
    extern uint32_t fp_seed;
    fp_seed ^= now * 2654435761u ^ (uint32_t)hit; /* the moment a key is pressed adds to the chance */
    if (!fp_seed) fp_seed = 1;
  }
  if (keys & (KEY(FK_HOME) | KEY(FK_ONOFF))) {
    save();
    return false;
  }
  bool back = hit & KEY(FK_BACK), ok = (hit & KEY(FK_OK)) || ((hit & KEY(FK_EXE)) && np < 4);
  int dx = !!(hit & KEY(FK_RIGHT)) - !!(hit & KEY(FK_LEFT)), dy = !!(hit & KEY(FK_DOWN)) - !!(hit & KEY(FK_UP));
  int was = u.screen, was_sel = u.sel, was_game = sv.game, was_mode = sv.mode;
  switch (u.screen) {
    case S_MODE:
      if (dy) sv.mode = (uint8_t)((sv.mode + dy + NMODES) % NMODES), set_mode(sv.mode);
      if (ok) save(), open(S_GAMES);
      else if (back) u.under = S_MODE, open(S_ASK);
      break;
    case S_GAMES: {
      int i = sv.game == MIX ? 0 : sv.game + 1;
      if (dx) i = (i + dx + NG + 1) % (NG + 1);
      if (dy) i = (i + dy * 5 + NG + 1) % (NG + 1);
      sv.game = (uint8_t)(i ? i - 1 : MIX);
      if (ok) save(), new_match();
      else if (back) open(S_MODE);
      break;
    }
    case S_RULES:
      for (int p = 0; p < np; p++)
        if (hit & KEY(PKEY[p])) u.ready |= (uint8_t)(1 << p), u.dirty = true;
      if (dx && !u.started) sv.target = (uint8_t)clampi(sv.target + dx, 1, 9), u.dirty = true;
      if (back) {
        if (u.started) u.under = S_RULES, open(S_PAUSE);
        else open(S_GAMES);
      } else if (ok || u.ready == (1 << np) - 1) {
        if (!u.started) save();
        u.started = true;
        for (int p = 0; p < np; p++) u.prev_h[p] = 1; /* (the key just pressed isn't the round's first tap) */
        begin_round();
      }
      break;
    case S_PLAY:
      if (back) {
        u.under = S_PLAY, open(S_PAUSE);
        break;
      }
      play(ms);
      break;
    case S_SCORE:
      if (back) u.under = S_SCORE, open(S_PAUSE);
      else if ((ok && now - u.shown > 500) || now - u.shown > SCORE_MS) next_round();
      break;
    case S_OVER:
      if (dx || dy) u.sel ^= 1;
      if (now - u.shown < 900) break; /* (a key still being mashed doesn't choose) */
      if (back) open(S_GAMES);
      else if (ok) {
        if (u.sel) open(S_GAMES);
        else new_match();
      }
      break;
    case S_PAUSE:
      u.sel = (u.sel + dy + 4) & 3;
      if (back || (ok && u.sel == 0)) { /* Resume */
        open(u.under);
        if (u.under == S_PLAY && rt > 0 && !u.decided) u.freeze = 1200;
        for (int p = 0; p < np; p++) u.prev_h[p] = (u.held & KEY(PKEY[p])) != 0;
      } else if (ok && u.sel == 1) {
        new_match();
        if (sv.game != MIX) u.started = true, begin_round();
      } else if (ok && u.sel == 2) {
        open(S_GAMES);
      } else if (ok) {
        open(S_ASK);
      }
      break;
    case S_ASK:
      u.sel = (u.sel + dy + 2) & 1;
      if (back || (ok && !u.sel)) open(u.under == S_MODE ? S_MODE : S_PAUSE), u.sel = u.under == S_MODE ? 0 : 3;
      else if (ok) {
        save();
        return false;
      }
      break;
  }
  if (u.screen != was || u.sel != was_sel || sv.game != was_game || sv.mode != was_mode) u.dirty = true;
  if (u.screen == S_PLAY || (u.screen == S_SCORE && now - u.shown < 500)) u.dirty = true;
  return true;
}

void app_draw(void) {
  gfx_render(scene, 0, 0, SCREEN_H);
  u.dirty = false;
}

#ifdef HOST
void app_state(app_state_t *s) {
  s->screen = u.screen, s->mode = sv.mode, s->game = sv.game, s->cur = u.cur, s->target = sv.target;
  s->round = u.round, s->nsides = u.nsides, s->winner = u.winner, s->decided = u.decided, s->rt = rt;
  s->matches = u.matches;
  memcpy(s->score, u.score, sizeof s->score);
  memcpy(s->wins, sv.wins, sizeof sv.wins);
}
uint64_t app_player_key(int p) { return KEY(PKEY[p]); }
/* the rules' tests (tests/rules.c): a round of a game straight away, then steps with the keys held (a bit each) */
void app_test_round(int mode, int game) {
  set_mode(mode);
  u.cur = game, memset(u.score, 0, sizeof u.score), memset(u.prev_h, 0, sizeof u.prev_h);
  begin_round();
  rt = 0;
}
void app_test_step(unsigned held) {
  uint8_t h[NMAX], t[NMAX], r[NMAX];
  for (int p = 0; p < NMAX; p++) {
    h[p] = held >> p & 1, t[p] = h[p] && !u.prev_h[p], r[p] = !h[p] && u.prev_h[p];
    u.prev_h[p] = h[p];
  }
  rt += TICK;
  game_step(h, t, r);
}
int app_test_winner(void) { return u.decided ? u.winner : -2; } /* a side, -1 a draw, -2 not yet */
uint64_t app_bot_keys(void) {
  uint64_t k = 0;
  static bool press;
  if (u.screen == S_RULES && u.started && (press = !press)) /* (Mix: everyone ready for the next game) */
    for (int p = 0; p < np; p++) k |= KEY(PKEY[p]);
  if (u.screen == S_PLAY && rt > 0 && !u.decided && u.freeze <= 0)
    for (int p = 0; p < np; p++)
      if (!gone[p] && G()->bot(p)) k |= KEY(PKEY[p]);
  return k;
}
#endif
