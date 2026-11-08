/* Flappy Dunk on the NumWorks calculator: Voodoo's game (2017), an app of its own.
 *
 * The game (game.c) runs the original's rules and numbers; draw.c paints it with the game's own sprites. This
 * file has the screens laid out after the original's (the title over the first hoops, the results, the skins,
 * the challenges, the pause), the goals that unlock the balls, wings, hoops and flames, and the saves.
 *
 * Keys: OK, EXE or Up flaps; the arrows move in the menus, OK picks; Back pauses or goes back; Home saves and
 * quits from anywhere. Made by Mason Chen as part of NumPlay. */
#include <stddef.h>
#include <string.h>
#include "draw.h"
#include "fd.h"
#include "gfx.h"
#include "plat.h"

#define SAVE_NAME "flappydunk.sav"
#define COPY_NAME "flappydunk_saves.py"
#define SAVE_MAGIC 0x324B4446u /* "FDK2" */
#define SAVE_VERSION 1

/* ---------------------------------------------------------------- the skins and their goals */
enum { K_BALL, K_WING, K_HOOP, K_FEVER };
enum {
  R_DEFAULT, R_SCORE, R_HOOPS_RUN, R_COMBO, R_GAMES, R_CHALLENGES, R_MIRROR, R_SWISHES_RUN, R_SWISHES, R_HOOPS,
  R_SKINS, R_PLAYED, R_ROTATE, R_PIZZA, R_BUOY, R_BRAIN, R_NOSWISH, R_BOUNCES, R_MELON_SCORE, R_MELON_SWISHES,
  R_CH19, R_CH_PLAYED, R_EASY_ROW, R_MYSTERY, R_EXACT, R_RAINBOW
};
typedef struct {
  uint8_t kind, index, rule;
  uint16_t n;
  const char *goal;
} skin_t;
/* the order of the game's collections (Balls, Wings, Hoops, Fevers), their goals as they say them */
static const skin_t skins[] = {
  {K_BALL, 0, R_DEFAULT, 0, "Play Flappy Dunk"},
  {K_BALL, 1, R_HOOPS_RUN, 3, "Pass 3 hoops or more in a single game in endless mode"},
  {K_BALL, 2, R_COMBO, 4, "Achieve a swish x4"},
  {K_BALL, 3, R_SCORE, 20, "Score 20 or more"},
  {K_BALL, 4, R_GAMES, 30, "Play 30 games in endless mode"},
  {K_BALL, 5, R_CHALLENGES, 15, "Complete 15 challenges in normal mode"},
  {K_BALL, 6, R_COMBO, 10, "Achieve a swish x10"},
  {K_BALL, 7, R_GAMES, 100, "Play 100 games in endless mode"},
  {K_BALL, 8, R_COMBO, 20, "Achieve a swish x20"},
  {K_BALL, 9, R_CHALLENGES, 30, "Complete 30 challenges in normal mode"},
  {K_BALL, 10, R_SCORE, 50, "Score 50 or more"},
  {K_BALL, 11, R_ROTATE, 1800, "Rotate of 1,800\xB0 between two hoops"},
  {K_BALL, 12, R_GAMES, 200, "Play 200 games in endless mode"},
  {K_BALL, 13, R_SCORE, 125, "Score 125 or more"},
  {K_BALL, 14, R_SCORE, 275, "Score 275 or more"},
  {K_BALL, 15, R_HOOPS, 1000, "Pass 1,000 hoops in total in endless mode"},
  {K_BALL, 16, R_SKINS, 15, "Unlock 15 skins"},
  {K_BALL, 17, R_HOOPS, 2000, "Pass 2,000 hoops in total in endless mode"},
  {K_BALL, 18, R_PLAYED, 12, "Play with 12 different skins in endless mode"},
  {K_BALL, 19, R_SWISHES_RUN, 15, "Achieve 15 swishes in a single endless game"},
  {K_BALL, 20, R_HOOPS_RUN, 30, "Pass 30 hoops or more in a single game in endless mode"},
  {K_BALL, 21, R_CHALLENGES, 51, "Complete 51 challenges in normal mode"},
  {K_BALL, 22, R_MIRROR, 51, "Complete 51 challenges in mirror mode"},
  {K_BALL, 23, R_PIZZA, 8, "Last 8 seconds between 2 hoops during an endless game"},
  {K_BALL, 24, R_BUOY, 0, "You almost touched the floor between two hoops in endless mode"},
  {K_BALL, 25, R_BRAIN, 0, "7542 + 8731 = 16273 Complete challenges 1, 6, 2, 7 and 3 in that order"},
  {K_WING, 0, R_DEFAULT, 0, "Play Flappy Dunk"},
  {K_WING, 1, R_NOSWISH, 15, "Score 15 without making a swish"},
  {K_WING, 2, R_BOUNCES, 5, "Bounce 5 times on a single hoop in endless mode"},
  {K_WING, 3, R_MELON_SCORE, 150, "Score 150 or more using the watermelon ball"},
  {K_WING, 4, R_SWISHES_RUN, 25, "Achieve 25 swishes in a single endless game"},
  {K_WING, 5, R_CH19, 0, "Finish Challenge 19 with swishes only"},
  {K_WING, 6, R_SWISHES, 200, "Achieve 200 swishes in total"},
  {K_WING, 7, R_CH_PLAYED, 30, "Play 30 challenges"},
  {K_WING, 8, R_PLAYED, 25, "Play with 25 different skins in endless mode"},
  {K_WING, 9, R_EASY_ROW, 10, "Complete 10 different easy challenges in a row in normal mode without losing"},
  {K_HOOP, 0, R_DEFAULT, 0, "Play Flappy Dunk"},
  {K_HOOP, 1, R_MYSTERY, 2, "Unlock two mystery skins"},
  {K_HOOP, 2, R_HOOPS_RUN, 75, "Pass 75 hoops or more in a single game in endless mode"},
  {K_HOOP, 3, R_SKINS, 25, "Unlock 25 skins"},
  {K_HOOP, 4, R_MELON_SWISHES, 25, "Achieve 25 swishes using the watermelon ball in a single endless game"},
  {K_HOOP, 5, R_SWISHES, 100, "Achieve 100 swishes in total"},
  {K_HOOP, 6, R_EXACT, 42, "End a game with a score of exactly 42"},
  {K_HOOP, 7, R_RAINBOW, 51, "Complete 51 challenges in normal mode with the Rainbow ball"},
  {K_HOOP, 8, R_SKINS, 30, "Unlock 30 skins"},
  {K_FEVER, 0, R_DEFAULT, 0, "Play Flappy Dunk"},
  {K_FEVER, 1, R_PLAYED, 35, "Play with 35 different skins in endless mode"},
  {K_FEVER, 2, R_GAMES, 300, "Play 300 games in endless mode"},
  {K_FEVER, 3, R_SKINS, 40, "Unlock 40 skins"},
  {K_FEVER, 4, R_SWISHES, 350, "Achieve 350 swishes in total"},
  {K_FEVER, 5, R_CH_PLAYED, 100, "Play 100 challenges"},
};
#define NSKINS ((int)(sizeof skins / sizeof skins[0]))
static const uint8_t kind_first[4] = {0, FD_NBALLS, FD_NBALLS + FD_NWINGS, FD_NBALLS + FD_NWINGS + FD_NHOOPS};
static const uint8_t kind_count[4] = {FD_NBALLS, FD_NWINGS, FD_NHOOPS, FD_NFEVERS};
#define BALL_WATERMELON 17
#define BALL_RAINBOW 9
static bool mystery(int k) { return skins[k].rule == R_PIZZA || skins[k].rule == R_BUOY || skins[k].rule == R_BRAIN; }

/* ---------------------------------------------------------------- the save */
typedef struct {
  uint32_t magic;
  uint16_t version, size;
  uint8_t flags; /* 1 How to play seen */
  uint8_t equip[4], mirror, easy_row, brain;
  uint32_t best, games, swishes, hoops, ch_played, melon_best;
  uint32_t unlocked[2], seen[2], ch_done[2], ch_mirror[2], ch_rainbow[2], played[2], easy_done[2];
  uint32_t sum;
} save_t;
static save_t sv;

static bool bit(const uint32_t *b, int i) { return b[i >> 5] >> (i & 31) & 1; }
static void bit_set(uint32_t *b, int i) { b[i >> 5] |= 1u << (i & 31); }
static int bits(const uint32_t *b) {
  int n = 0;
  for (int i = 0; i < 64; i++) n += bit(b, i);
  return n;
}
static uint32_t fnv(const void *d, uint32_t n) {
  const uint8_t *p = d;
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
  return h;
}
static bool save_ok(const save_t *s) {
  if (s->magic != SAVE_MAGIC || s->version != SAVE_VERSION || s->size != sizeof *s) return false;
  if (s->sum != fnv(s, offsetof(save_t, sum))) return false;
  for (int k = 0; k < 4; k++)
    if (s->equip[k] >= kind_count[k]) return false;
  return true;
}
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static const char copy_head[] = "# Flappy Dunk's saves: keep this file\n#>" SAVE_NAME ":";
static void save(void) {
  sv.magic = SAVE_MAGIC, sv.version = SAVE_VERSION, sv.size = sizeof sv;
  sv.sum = fnv(&sv, offsetof(save_t, sum));
  plat_save(SAVE_NAME, &sv, sizeof sv);
  /* a copy in a Python script, the only files the installer keeps: a zero first (a script's status), the
     save in base64 on a comment line, a zero at the end */
  static uint8_t o[1 + sizeof copy_head + (sizeof(save_t) + 2) / 3 * 4 + 2];
  uint8_t *p = o;
  const uint8_t *d = (const uint8_t *)&sv;
  *p++ = 0;
  memcpy(p, copy_head, sizeof copy_head - 1), p += sizeof copy_head - 1;
  for (uint32_t i = 0; i < sizeof sv; i += 3, p += 4) {
    uint32_t left = sizeof sv - i, v = (uint32_t)d[i] << 16 | (left > 1 ? (uint32_t)d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  *p++ = 0;
  plat_save(COPY_NAME, o, (uint32_t)(p - o));
}
static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return c == '=' ? 0 : -1;
}
static bool copy_read(save_t *out) {
  uint32_t n;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  const uint32_t hl = sizeof "#>" SAVE_NAME ":" - 1;
  if (!c) return false;
  for (uint32_t i = 0; i + hl <= n; i++) {
    if (memcmp(c + i, "#>" SAVE_NAME ":", hl)) continue;
    const uint8_t *l = c + i + hl;
    uint32_t m = 0;
    while (i + hl + m < n && l[m] != '\n' && l[m] && b64v(l[m]) >= 0) m++;
    if (m != (sizeof(save_t) + 2) / 3 * 4) return false;
    static uint8_t buf[(sizeof(save_t) + 2) / 3 * 3];
    for (uint32_t k = 0; k < m; k += 4) {
      uint32_t w = (uint32_t)(b64v(l[k]) << 18 | b64v(l[k + 1]) << 12 | b64v(l[k + 2]) << 6 | b64v(l[k + 3]));
      buf[k / 4 * 3] = (uint8_t)(w >> 16), buf[k / 4 * 3 + 1] = (uint8_t)(w >> 8), buf[k / 4 * 3 + 2] = (uint8_t)w;
    }
    memcpy(out, buf, sizeof *out);
    return save_ok(out);
  }
  return false;
}
/* the first Flappy Dunk of NumPlay's save (200 bytes: 'F', version 1, flags, ball, a sum of the words after it,
   then best, swishes, games, best swish streak and a run): its records carry over, and its balls' goals unlock
   the original's balls that have them */
static bool migrate(const uint8_t *d, uint32_t n) {
  if (n != 200 || d[0] != 'F' || d[1] != 1) return false;
  uint32_t w[50];
  memcpy(w, d, sizeof w);
  uint32_t h = 0x811C9DC5u;
  for (int i = 2; i < 50; i++) h = (h ^ w[i]) * 0x01000193u;
  if (h != w[1]) return false;
  sv.best = w[2], sv.swishes = w[3], sv.games = w[4];
  if (w[5] + 1 >= 4) bit_set(sv.unlocked, 2);   /* a swish x4: the purple ball */
  if (w[5] + 1 >= 10) bit_set(sv.unlocked, 6);  /* x10: the pink one */
  if (w[5] + 1 >= 20) bit_set(sv.unlocked, 8);  /* x20: the white one */
  sv.flags |= d[2] & 2 ? 1 : 0;
  /* the ball it had on: Watermelon (best 20), Ocean (50), Candy (10 in a row), Gold (125) */
  static const uint8_t to[6] = {0, 3, 10, 6, 13, 0};
  if (d[3] < 6) sv.equip[K_BALL] = to[d[3]];
  return true;
}
static void load(void) {
  uint32_t n;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  static save_t t;
  memset(&sv, 0, sizeof sv);
  if (d && n == sizeof t) {
    memcpy(&t, d, sizeof t);
    if (save_ok(&t)) {
      sv = t;
      return;
    }
  }
  if (d && migrate(d, n)) return;
  if (copy_read(&t)) sv = t;
}

/* ---------------------------------------------------------------- goals */
static uint8_t news[NSKINS], nnews; /* unlocked in this run, shown after it */
static int brain_seq[5] = {0, 5, 1, 6, 2};

static uint32_t progress(int k, uint32_t *goal) {
  const skin_t *s = &skins[k];
  *goal = s->n;
  switch (s->rule) {
    case R_GAMES: return sv.games;
    case R_CHALLENGES: return (uint32_t)bits(sv.ch_done);
    case R_MIRROR: return (uint32_t)bits(sv.ch_mirror);
    case R_SWISHES: return sv.swishes;
    case R_HOOPS: return sv.hoops;
    case R_SKINS: return (uint32_t)bits(sv.unlocked);
    case R_PLAYED: return (uint32_t)bits(sv.played);
    case R_CH_PLAYED: return sv.ch_played;
    case R_EASY_ROW: return sv.easy_row;
    case R_RAINBOW: return (uint32_t)bits(sv.ch_rainbow);
    case R_MYSTERY: {
      uint32_t m = 0;
      for (int i = 0; i < NSKINS; i++) m += mystery(i) && bit(sv.unlocked, i);
      return m;
    }
    case R_SCORE: return sv.best;
    case R_MELON_SCORE: return sv.melon_best;
  }
  *goal = 0;
  return 0;
}

static void unlock(int k) {
  if (bit(sv.unlocked, k)) return;
  bit_set(sv.unlocked, k);
  if (nnews < NSKINS) news[nnews++] = (uint8_t)k;
}
/* every goal met by the records and by the run so far (in endless mode: G's) */
static void check_goals(bool during_endless, int points) {
  for (int pass = 0; pass < 2; pass++) /* (unlocking may meet "Unlock N skins") */
    for (int k = 0; k < NSKINS; k++) {
      const skin_t *s = &skins[k];
      bool ok = false;
      uint32_t goal, have = progress(k, &goal);
      switch (s->rule) {
        case R_DEFAULT: ok = true; break;
        case R_GAMES: case R_CHALLENGES: case R_MIRROR: case R_SWISHES: case R_HOOPS: case R_SKINS: case R_PLAYED:
        case R_CH_PLAYED: case R_EASY_ROW: case R_RAINBOW: case R_MYSTERY: case R_SCORE: case R_MELON_SCORE:
          ok = have >= goal;
          break;
        default:
          if (!during_endless) break;
          switch (s->rule) {
            case R_HOOPS_RUN: ok = G.hoops >= s->n; break;
            case R_COMBO: ok = points >= s->n; break;
            case R_SWISHES_RUN: ok = G.swishes >= s->n; break;
            case R_NOSWISH: ok = G.best_notswish >= s->n; break;
            case R_BOUNCES: ok = G.best_bounces >= s->n; break;
            case R_MELON_SWISHES: ok = sv.equip[K_BALL] == BALL_WATERMELON && G.swishes >= s->n; break;
            case R_ROTATE: ok = points && G.rot_since >= (float)s->n; break;
            case R_PIZZA: ok = points && G.since_hoop >= (float)s->n; break;
            case R_BUOY: ok = points && G.min_y <= -8.5f; break;
          }
      }
      if (ok) unlock(k);
    }
}

/* ---------------------------------------------------------------- the run's events (game.c) */
static uint32_t best0;
void on_touch(void) {}
void on_pass(int points) {
  if (fd_quiet) return;
  sv.swishes += points >= 2;
  if (G.challenge >= 0) return;
  sv.hoops++;
  if (G.score > sv.best) sv.best = G.score;
  if (sv.equip[K_BALL] == BALL_WATERMELON && G.score > sv.melon_best) sv.melon_best = G.score;
  check_goals(true, points);
}
static void challenge_won(void);
void on_end(void) {
  if (fd_quiet) return;
  if (G.challenge < 0) {
    if (G.end != END_WIN && G.score == 42) unlock(kind_first[K_HOOP] + 6); /* exactly 42 */
    check_goals(true, 0);
  } else if (G.end == END_WIN) {
    challenge_won();
  } else {
    sv.easy_row = 0, sv.easy_done[0] = sv.easy_done[1] = 0, sv.brain = 0;
  }
  save();
}
static void challenge_won(void) {
  int c = G.challenge;
  if (G.mirror_mode) bit_set(sv.ch_mirror, c);
  else {
    bit_set(sv.ch_done, c);
    if (sv.equip[K_BALL] == BALL_RAINBOW) bit_set(sv.ch_rainbow, c);
    if (c == 18 && G.notswish == 0) unlock(kind_first[K_WING] + 5);
    if (fd_challenges[c].difficulty == 0 && !bit(sv.easy_done, c)) bit_set(sv.easy_done, c), sv.easy_row++;
    /* 1, 6, 2, 7 and 3 in that order */
    sv.brain = c == brain_seq[sv.brain] ? (uint8_t)(sv.brain + 1) : c == brain_seq[0] ? 1 : 0;
    if (sv.brain >= 5) unlock(25), sv.brain = 0;
  }
  check_goals(false, 0);
}

/* ---------------------------------------------------------------- screens */
enum { SC_TITLE, SC_PLAY, SC_PAUSE, SC_QUIT, SC_RESULT, SC_NEWSKIN, SC_SKINS, SC_CHALLENGES, SC_HELP };
static int sc = SC_TITLE, sel, sel2, help_back, quit_back, tab, after_news;
static uint32_t now, dead_t, frame_ms;
static bool ch_mirror_view;
static int ch_sel;

static void start_endless(void) {
  game_new(-1, false, plat_random() ^ now);
  best0 = sv.best;
}
static void apply_skins(void) {
  dr_ball = sv.equip[K_BALL], dr_wing = sv.equip[K_WING], dr_hoop = sv.equip[K_HOOP], dr_fever = sv.equip[K_FEVER];
}
static void begin_run(void) { /* the first flap: the game counts as played */
  if (G.challenge < 0) {
    sv.games++;
    for (int k = 0; k < 4; k++) bit_set(sv.played, kind_first[k] + sv.equip[k]);
  } else {
    sv.ch_played++;
  }
  nnews = 0;
}
static void start_challenge(int c, bool mirror) {
  game_new(c, mirror, plat_random() ^ now);
  sc = SC_PLAY;
}

/* ---- drawing helpers in the game's style: orange buttons, white Lilita lettering outlined in black */
static char *num(char *o, uint32_t v) {
  char t[12];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static const char *cat(char *o, const char *a, uint32_t v, const char *b) {
  char *p = o;
  while (*a) *p++ = *a++;
  p = num(p, v);
  while (b && *b) *p++ = *b++;
  *p = 0;
  return o;
}
#define ORANGE HEX(0xFF9317)
#define DARK HEX(0x1A1A1A)
static void button(int cx, int cy, int w, int h, const char *s, bool on, color_t col) {
  int x = cx - w / 2, y = cy - h / 2;
  gfx_rrect(x, y + 3, w, h, h / 2, HEX(0x7A3A00), 256);
  gfx_rrect(x, y, w, h, h / 2, on ? col : gfx_mix(col, DARK, 14), 256);
  if (on) gfx_rrect_outline(x - 2, y - 2, w + 4, h + 4, h / 2 + 2, 2, 0xFFFF, 256);
  dr_text(&fd_font_mid, &fd_font_mid_o, cx, cy + 6, s, 0xFFFF, 256, 1);
}
static void round_icon(int cx, int cy, const fd_img_t *im, float k, bool on, const char *label) {
  gfx_circle(cx, cy + 2, 17, HEX(0x7A3A00), 256);
  gfx_circle(cx, cy, 17, on ? ORANGE : gfx_mix(ORANGE, DARK, 14), 256);
  if (on) gfx_rrect_outline(cx - 19, cy - 19, 38, 38, 19, 2, 0xFFFF, 256);
  if (im) dr_image_c(im, cx, cy, k, 256);
  else if (k < 0) gfx_icon(&np_icon_back, cx - np_icon_back.w / 2, cy - np_icon_back.h / 2, 0xFFFF, 256);
  else dr_text(&fd_font_mid, &fd_font_mid_o, cx, cy + 7, "?", 0xFFFF, 256, 1);
  if (on && label) dr_text(&fd_font_small, &fd_font_small_o, cx, cy + 30, label, 0xFFFF, 256, 1);
}
static void keyline(const char *s) {
  gfx_fill_alpha(0, 226, SCREEN_W, 14, 0, 140);
  dr_text(&fd_font_small, 0, 160, 236, s, HEX(0xF2E6D0), 256, 1);
}
static void panel(int x, int y, int w, int h) {
  gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 120);
  gfx_rrect(x, y + 4, w, h, 14, HEX(0x5A3410), 256);
  gfx_rrect(x, y, w, h, 14, HEX(0xFFF4E2), 256);
  gfx_rrect_outline(x, y, w, h, 14, 3, DARK, 256);
}

/* a skin's picture, centred */
static void skin_pic(int k, int cx, int cy, float big, bool have) {
  const skin_t *s = &skins[k];
  const fd_img_t *im = s->kind == K_BALL ? fd_balls[s->index] : s->kind == K_WING ? fd_wings[s->index] : 0;
  if (s->kind == K_HOOP) {
    float kk = 0.3f * big;
    if (have) {
      dr_image_c(fd_hoop_back[s->index], cx, cy, kk, 256), dr_image_c(fd_hoop_front[s->index], cx, cy, kk, 256);
    } else {
      dr_image_dark(fd_hoop_back[s->index], cx, cy, kk, HEX(0x4A4038));
      dr_image_dark(fd_hoop_front[s->index], cx, cy, kk, HEX(0x4A4038));
    }
    return;
  }
  if (s->kind == K_FEVER) {
    color_t c = have ? HEX(fd_fever_rgb[s->index]) : HEX(0x4A4038);
    dr_image_dark(&fd_flames_big, cx, cy, 0.6f * big, c);
    if (have) dr_image_dark(&fd_icon_flames, cx + 2, cy + 1, 0.7f * big, gfx_mix(c, 0xFFFF, 16));
    return;
  }
  float kk = (s->kind == K_BALL ? 0.8f : 0.9f) * big;
  if (have) dr_image_c(im, cx, cy, kk, 256);
  else dr_image_dark(im, cx, cy, kk, HEX(0x4A4038));
}

static uint32_t pulse(void) { /* 0..256..0 over a second */
  uint32_t t = now % 1000;
  return t < 500 ? t * 256 / 500 : (1000 - t) * 256 / 500;
}

static const char *const diff_name[3] = {"Easy", "Medium", "Hard"};
static const uint32_t diff_rgb[3] = {0x4CD964, 0xFF9500, 0xFF3B30};
static const char *end_text(void) {
  switch (G.end) {
    case END_GROUND: return "You touched the floor or the ceiling";
    case END_MISS: return "You missed a hoop";
    case END_BELOW: return "Hoops go from the top only";
    case END_RULE: return G.challenge >= 0 ? fd_challenge_text[G.challenge] : "";
  }
  return "";
}

static void draw_hud(void) {
  char s[24];
  if (sc == SC_TITLE) {
    dr_text(&fd_font_big, &fd_font_big_o, 160, 46, "FLAPPY DUNK", ORANGE, 256, 1);
    dr_text(&fd_font_small, &fd_font_small_o, 160, 64, cat(s, "Best ", sv.best, 0), 0xFFFF, 256, 1);
    if (sel == 0) dr_text(&fd_font_mid, &fd_font_mid_o, 160, 160, "OK to play", 0xFFFF, (int)(140 + pulse() / 2), 1);
    static const char *const lab[3] = {"Skins", "Challenges", "How to play"};
    round_icon(110, 192, &fd_icon_balls, 1.0f, sel == 1, lab[0]);
    round_icon(160, 192, &fd_icon_challenge, 0.95f, sel == 2, lab[1]);
    round_icon(210, 192, 0, 1, sel == 3, lab[2]);
    keyline(sel ? "Arrows choose   OK open   Back quit" : "OK play   Down menu   Back quit");
    return;
  }
  /* the score at the top (in a challenge: the hoops passed out of all) */
  if (G.challenge >= 0) {
    const fd_challenge_t *c = &fd_challenges[G.challenge];
    char *p = num(s, G.hoops);
    *p++ = '/';
    num(p, c->count);
    dr_text(&fd_font_big, &fd_font_big_o, 160, 40, s, 0xFFFF, 256, 1);
  } else if (G.state != ST_READY) {
    num(s, G.score);
    dr_text(&fd_font_big, &fd_font_big_o, 160, 40, s, 0xFFFF, 256, 1);
  }
  /* the swish's words (GamePage: "Swish!", then x2, x3...) */
  if (G.pop_t > 0 && G.pop_n >= 2) {
    int a = G.pop_t > 0.4f ? 256 : (int)(G.pop_t / 0.4f * 256);
    int rise = (int)((1.2f - G.pop_t) * 10);
    dr_text(&fd_font_mid, &fd_font_mid_o, 160, 70 - rise, "Swish!", HEX(0xFFD342), a, 1);
    cat(s, "x", (uint32_t)G.pop_n, 0);
    dr_text(&fd_font_mid, &fd_font_mid_o, 166, 88 - rise, s, G.pop_n == 2 ? 0xFFFF : G.pop_n == 3 ? HEX(0xFFE070) : HEX(0xFF7020), a, 1);
  }
  if (sc == SC_PLAY && G.state == ST_READY) {
    if (G.challenge >= 0) {
      const fd_challenge_t *c = &fd_challenges[G.challenge];
      gfx_rrect(40, 66, 240, 82, 12, HEX(0xFFF4E2), 236);
      gfx_rrect_outline(40, 66, 240, 82, 12, 2, DARK, 256);
      dr_text(&fd_font_mid, &fd_font_mid_o, 160, 88, cat(s, G.mirror_mode ? "Mirror " : "Challenge ", (uint32_t)G.challenge + 1, 0),
              HEX(diff_rgb[c->difficulty]), 256, 1);
      gfx_paragraph(&np_font_body, 160, 108, 220, 13, fd_challenge_text[G.challenge], DARK, 256);
      dr_text(&fd_font_small, 0, 160, 140, diff_name[c->difficulty], HEX(0x6A5A48), 256, 1);
    }
    dr_text(&fd_font_mid, &fd_font_mid_o, 160, 178, "OK to flap", 0xFFFF, (int)(140 + pulse() / 2), 1);
    keyline("OK flap   Back pause");
  }
}

static void draw_pause(void) {
  static const char *const items[4] = {"Resume", "Main menu", "How to play", "Quit game"};
  panel(80, 30, 160, 186);
  dr_text(&fd_font_mid, &fd_font_mid_o, 160, 58, "Paused", ORANGE, 256, 1);
  for (int i = 0; i < 4; i++) button(160, 86 + 34 * i, 128, 24, items[i], sel == i, ORANGE);
  keyline("Arrows choose   OK pick   Back resume");
}
static void draw_quit(void) {
  panel(60, 70, 200, 100);
  dr_text(&fd_font_mid, &fd_font_mid_o, 160, 102, "Quit the game?", 0xFFFF, 256, 1);
  button(115, 140, 76, 24, "Yes", sel == 0, HEX(0xE84A3A));
  button(205, 140, 76, 24, "No", sel == 1, ORANGE);
}

static void draw_result(void) {
  char s[40];
  if (G.challenge < 0) {
    bool rec = G.score > best0;
    panel(50, 18, 220, 196);
    dr_text(&fd_font_small, 0, 160, 44, "Score", HEX(0x6A5A48), 256, 1);
    num(s, G.score);
    dr_text(&fd_font_big, &fd_font_big_o, 160, 76, s, ORANGE, 256, 1);
    if (rec) {
      dr_image_c(&fd_star_small, 104, 92, 1, 256), dr_image_c(&fd_star_small, 216, 92, 1, 256);
      dr_text(&fd_font_mid, &fd_font_mid_o, 160, 97, "New best!", HEX(0xFFD342), 256, 1);
    } else dr_text(&fd_font_small, 0, 160, 96, cat(s, "Best: ", sv.best, 0), DARK, 256, 1);
    button(160, 126, 150, 28, "Play again", sel == 0, ORANGE);
    static const char *const lab[3] = {"Skins", "Challenges", "Menu"};
    round_icon(110, 172, &fd_icon_balls, 1.0f, sel == 1, 0);
    round_icon(160, 172, &fd_icon_challenge, 0.95f, sel == 2, 0);
    round_icon(210, 172, 0, -1, sel == 3, 0);
    dr_text(&fd_font_small, 0, 160, 204, sel ? lab[sel - 1] : "", DARK, 256, 1);
  } else {
    bool won = G.end == END_WIN;
    panel(40, 24, 240, 190);
    dr_text(&fd_font_mid, &fd_font_mid_o, 160, 54, cat(s, G.mirror_mode ? "Mirror " : "Challenge ", (uint32_t)G.challenge + 1, 0),
            HEX(diff_rgb[fd_challenges[G.challenge].difficulty]), 256, 1);
    dr_text(&fd_font_big, &fd_font_big_o, 160, 88, won ? "Complete!" : "Failed", won ? HEX(0xFFD342) : HEX(0xE84A3A), 256, 1);
    if (won) dr_image_c(&fd_star, 160, 108, 1, 256);
    else gfx_paragraph(&np_font_body, 160, 106, 210, 13, end_text(), DARK, 256);
    static const char *const won_items[3] = {"Next", "Retry", "Challenges"}, *const lost_items[3] = {"Retry", "Challenges", "Menu"};
    for (int i = 0; i < 3; i++) button(160, 134 + 26 * i, 140, 22, won ? won_items[i] : lost_items[i], sel == i, ORANGE);
  }
  keyline("Arrows choose   OK pick");
}

static void draw_newskin(void) {
  int k = news[sel2];
  panel(50, 24, 220, 192);
  dr_text(&fd_font_mid, &fd_font_mid_o, 160, 52, "New skin!", HEX(0xFFD342), 256, 1);
  gfx_circle(160, 104, 34, HEX(0xFFE2B8), 256);
  skin_pic(k, 160, 104, 2.0f, true);
  gfx_paragraph(&np_font_body, 160, 158, 196, 13, skins[k].goal, DARK, 256);
  button(160, 196, 120, 24, "Use it", true, ORANGE);
  keyline("OK use it   Back later");
}

static void tile_bg(void) {
  int off = (int)(now / 40) % 64;
  for (int y = gfx_y0; y < gfx_y1; y++) {
    color_t *row = gfx_buf + (y - gfx_y0) * SCREEN_W;
    int ty = (y + off) & 63;
    for (int x = 0; x < SCREEN_W; x++) {
      int tx = (x + off) & 63, i = ty * 64 + tx;
      row[x] = fd_tile.a[i] > 128 ? fd_tile.c[i] : HEX(0xF4F4F4);
    }
  }
}

static void draw_skins(void) {
  char s[40];
  tile_bg();
  /* the tabs: balls, wings, hoops, flames */
  static const char *const tabs[4] = {"Balls", "Wings", "Hoops", "Flames"};
  for (int t = 0; t < 4; t++) {
    int x = 8 + t * 78;
    bool on = tab == t, focus = on && sel < 0;
    gfx_rrect(x, 6, 72, 24, 10, on ? ORANGE : HEX(0x8A7A68), 256);
    if (focus) gfx_rrect_outline(x - 2, 4, 76, 28, 12, 2, DARK, 256);
    dr_text(&fd_font_small, &fd_font_small_o, x + 36, 23, tabs[t], 0xFFFF, 256, 1);
  }
  /* the Dunky as it is, on the left */
  gfx_rrect(4, 38, 72, 150, 10, HEX(0xFFF4E2), 230);
  gfx_rrect_outline(4, 38, 72, 150, 10, 2, DARK, 256);
  {
    dr_blit(fd_hoop_back[dr_hoop], 40, 152, 0.62f, 0, 0, 1.0f, 256, 0, 0);
    int bob = (int)((now / 30) % 20), by = 96 + (bob < 10 ? bob : 20 - bob) / 3;
    /* the fire, if any colour */
    dr_image_dark(&fd_flames_big, 26, by + 2, 0.55f, HEX(fd_fever_rgb[dr_fever]));
    float sxs = 1.6f;
    dr_blit(fd_wings[dr_wing], 40 + 0.3f * 26.7f * sxs, by - 0.3f * 26.7f * sxs, sxs, 0, 0, sxs, 256, 0, 0);
    dr_blit(fd_balls[dr_ball], 40, by, sxs, 0, 0, sxs, 256, 0, 0);
    dr_blit(fd_wings[dr_wing], 40 - 0.456f * 26.7f * sxs, by - 0.3f * 26.7f * sxs, sxs, 0, 0, sxs, 256, 0, 0);
    dr_blit(fd_hoop_front[dr_hoop], 40, 152, 0.62f, 0, 0, 1.0f, 256, 0, 0);
  }
  /* the grid */
  int first = kind_first[tab], n = kind_count[tab], cols = 7;
  for (int i = 0; i < n; i++) {
    int k = first + i, x = 82 + (i % cols) * 33, y = 38 + (i / cols) * 34;
    bool have = bit(sv.unlocked, k), eq = sv.equip[tab] == i, on = sel == i;
    gfx_rrect(x, y, 30, 30, 8, eq ? HEX(0xFFD342) : HEX(0xFFF4E2), 240);
    if (on) gfx_rrect_outline(x - 2, y - 2, 34, 34, 10, 2, DARK, 256);
    if (!have && mystery(k)) dr_text(&fd_font_mid, &fd_font_mid_o, x + 15, y + 22, "?", 0xFFFF, 256, 1);
    else skin_pic(k, x + 15, y + 15, 0.95f, have);
    if (have && !bit(sv.seen, k)) gfx_circle(x + 26, y + 4, 4, HEX(0xE84A3A), 256);
  }
  /* what the chosen one needs, or that it is on */
  gfx_rrect(80, 176, 236, 46, 10, HEX(0xFFF4E2), 240);
  gfx_rrect_outline(80, 176, 236, 46, 10, 2, DARK, 256);
  if (sel >= 0) {
    int k = first + sel;
    bool have = bit(sv.unlocked, k);
    const char *g = skins[k].goal;
    if (!have && mystery(k)) g = skins[k].rule == R_PIZZA ? "You could get this marvellous skin in no more than 8 seconds..."
                               : skins[k].rule == R_BUOY ? "That was close..." : "7542 + 8731 = ?";
    if (have) dr_text(&fd_font_small, 0, 198, 196, sv.equip[tab] == sel ? "In play" : "OK to play with it", HEX(0x2E8B3A), 256, 1);
    else {
      gfx_paragraph(&np_font_small, 198, 190, 224, 10, g, DARK, 256);
      uint32_t goal, have_n = progress(k, &goal);
      if (goal > 1) {
        char *p = num(s, have_n > goal ? goal : have_n);
        *p++ = '/';
        num(p, goal);
        dr_text(&fd_font_small, 0, 198, 216, s, HEX(0xD9480F), 256, 1);
      }
    }
  } else dr_text(&fd_font_small, 0, 198, 202, "Left and Right: balls, wings, hoops, flames", DARK, 256, 1);
  keyline(sel < 0 ? "Left Right tab   Down skins   Back menu" : "Arrows choose   OK use   Back menu");
}

static void draw_challenges(void) {
  char s[8];
  gfx_vgrad(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, 0x39A5FF, 0x395AFF);
  if (gfx_y0 < 40) {
    dr_text(&fd_font_mid, &fd_font_mid_o, 12, 26, "Challenges", 0xFFFF, 256, 0);
    for (int m = 0; m < 2; m++) {
      int x = 170 + m * 72;
      bool on = ch_mirror_view == m;
      gfx_rrect(x, 8, 66, 24, 10, on ? ORANGE : HEX(0x2A3A8A), 256);
      if (on && ch_sel < 0) gfx_rrect_outline(x - 2, 6, 70, 28, 12, 2, 0xFFFF, 256);
      dr_text(&fd_font_small, &fd_font_small_o, x + 33, 25, m ? "Mirror" : "Normal", 0xFFFF, 256, 1);
    }
  }
  for (int i = 0; i < 51; i++) {
    int x = 8 + (i % 9) * 34, y = 40 + (i / 9) * 26;
    if (y > gfx_y1 || y + 24 < gfx_y0) continue;
    bool done = bit(ch_mirror_view ? sv.ch_mirror : sv.ch_done, i), on = ch_sel == i;
    color_t c = HEX(diff_rgb[fd_challenges[i].difficulty]);
    gfx_rrect(x, y + 2, 32, 22, 7, gfx_mix(c, DARK, 12), 256);
    gfx_rrect(x, y, 32, 22, 7, c, 256);
    if (on) gfx_rrect_outline(x - 2, y - 2, 36, 26, 9, 2, 0xFFFF, 256);
    num(s, (uint32_t)i + 1);
    dr_text(&fd_font_small, &fd_font_small_o, x + 16, y + 16, s, 0xFFFF, 256, 1);
    if (done) dr_image_c(&fd_star_small, x + 29, y + 3, 0.7f, 256);
  }
  if (gfx_y1 > 196) {
    gfx_rrect(8, 198, 304, 26, 8, 0xFFFF, 220);
    if (ch_sel >= 0) {
      char t[16];
      dr_text(&fd_font_small, 0, 14, 210, cat(t, "", (uint32_t)ch_sel + 1, ""), HEX(diff_rgb[fd_challenges[ch_sel].difficulty]), 256, 0);
      dr_text(&fd_font_small, 0, 36, 210, fd_challenge_text[ch_sel], DARK, 256, 0);
      dr_text(&fd_font_small, 0, 36, 221, diff_name[fd_challenges[ch_sel].difficulty], HEX(0x6A6A6A), 256, 0);
    } else dr_text(&fd_font_small, 0, 160, 215, "Mirror: the same challenges, flipped", DARK, 256, 1);
  }
  keyline("Arrows choose   OK play   Back menu");
}

static void draw_help(void) {
  panel(24, 12, 272, 214);
  dr_text(&fd_font_mid, &fd_font_mid_o, 160, 38, "How to play", ORANGE, 256, 1);
  gfx_paragraph(&np_font_body, 160, 58, 246, 13,
                "Press OK to flap. Drop the ball down through every hoop. Miss one, go up through it or touch the floor "
                "or the ceiling and it's over.",
                DARK, 256);
  gfx_paragraph(&np_font_body, 160, 112, 246, 13,
                "No rim touched is a swish. Swishes in a row score 2, 3, 4... and set the ball on fire. Reach goals to "
                "unlock balls, wings, hoops and flames.",
                DARK, 256);
  static const char *const k[4][2] = {{"OK", "flap"}, {"Back", "pause"}, {"Arrows", "menus"}, {"Home", "save and quit"}};
  for (int i = 0; i < 4; i++) {
    int y = 170 + (i / 2) * 16, x = 92 + (i % 2) * 120;
    dr_text(&fd_font_small, 0, x - 4, y, k[i][0], HEX(0xD9480F), 256, 2);
    dr_text(&fd_font_small, 0, x + 2, y, k[i][1], DARK, 256, 0);
  }
  button(160, 208, 110, 22, "OK", true, ORANGE);
}

static void scene(void *ctx) {
  (void)ctx;
  if (sc == SC_SKINS) {
    draw_skins();
    return;
  }
  if (sc == SC_CHALLENGES) {
    draw_challenges();
    return;
  }
  dr_world(true);
  draw_hud();
  if (sc == SC_PAUSE) draw_pause();
  else if (sc == SC_QUIT) draw_quit();
  else if (sc == SC_RESULT) draw_result();
  else if (sc == SC_NEWSKIN) draw_newskin();
  else if (sc == SC_HELP) draw_help();
}

/* ---------------------------------------------------------------- keys */
static void go_menu(void) {
  start_endless();
  sc = SC_TITLE, sel = 0;
}
static void open_skins(void) { sc = SC_SKINS, tab = 0, sel = sv.equip[0]; }
static void open_challenges(void) {
  sc = SC_CHALLENGES, ch_mirror_view = sv.mirror;
  ch_sel = 0;
  for (int i = 0; i < 51; i++)
    if (!bit(ch_mirror_view ? sv.ch_mirror : sv.ch_done, i)) { ch_sel = i; break; }
}
static void after_run(void) {
  if (nnews) sc = SC_NEWSKIN, sel2 = 0, after_news = 1;
  else sc = SC_RESULT, sel = 0;
}

static void keys(uint32_t hit) {
  bool ok = hit & (PK_OK | PK_EXE), back = hit & PK_BACK;
  int dx = !!(hit & PK_RIGHT) - !!(hit & PK_LEFT), dy = !!(hit & PK_DOWN) - !!(hit & PK_UP);
  switch (sc) {
    case SC_TITLE:
      if (back) quit_back = SC_TITLE, sc = SC_QUIT, sel = 1;
      else if (sel == 0) {
        if (ok || (hit & PK_UP)) apply_skins(), begin_run(), game_flap(), sc = SC_PLAY;
        else if (dy > 0) sel = 1;
      } else {
        if (dy < 0) sel = 0;
        sel += dx;
        if (sel < 1) sel = 1;
        if (sel > 3) sel = 3;
        if (ok) {
          if (sel == 1) open_skins();
          else if (sel == 2) open_challenges();
          else help_back = SC_TITLE, sc = SC_HELP;
        }
      }
      break;
    case SC_PLAY:
      if (back && G.state <= ST_PLAY) sc = SC_PAUSE, sel = 0;
      else if ((ok || (hit & PK_UP)) && G.state <= ST_PLAY) {
        if (G.state == ST_READY) begin_run();
        game_flap();
      }
      break;
    case SC_PAUSE:
      sel = (sel + dy + 4) % 4;
      if (back) sc = SC_PLAY;
      else if (ok) {
        if (sel == 0) sc = SC_PLAY;
        else if (sel == 1) save(), go_menu();
        else if (sel == 2) help_back = SC_PAUSE, sc = SC_HELP;
        else quit_back = SC_PAUSE, sc = SC_QUIT, sel = 1;
      }
      break;
    case SC_QUIT:
      sel = (sel + dx + 2) % 2;
      if (back || (ok && sel == 1)) sc = quit_back, sel = quit_back == SC_PAUSE ? 3 : 0;
      break;
    case SC_HELP:
      if (ok || back) sc = help_back, sel = help_back == SC_PAUSE ? 2 : help_back == SC_TITLE ? 0 : sel;
      break;
    case SC_NEWSKIN:
      if (ok || back) {
        int k = news[sel2];
        if (ok) sv.equip[skins[k].kind] = skins[k].index, bit_set(sv.seen, k), apply_skins();
        if (++sel2 >= nnews) nnews = 0, sc = SC_RESULT, sel = 0, save();
      }
      break;
    case SC_RESULT:
      if (G.challenge < 0) {
        if (sel == 0) {
          if (dy > 0) sel = 1;
          if (ok) start_endless(), apply_skins(), sc = SC_PLAY, G.state = ST_READY;
          if (back) go_menu();
        } else {
          if (dy < 0) sel = 0;
          sel += dx;
          if (sel < 1) sel = 1;
          if (sel > 3) sel = 3;
          if (back) go_menu();
          if (ok) {
            if (sel == 1) open_skins();
            else if (sel == 2) open_challenges();
            else go_menu();
          }
        }
      } else {
        bool won = G.end == END_WIN;
        sel = (sel + dy + 3) % 3;
        if (back) open_challenges();
        else if (ok) {
          int c = G.challenge;
          bool m = G.mirror_mode;
          int act = won ? sel : sel + 1; /* 0 next, 1 retry, 2 challenges, 3 menu */
          if (act == 0) start_challenge(c + 1 < 51 ? c + 1 : 0, m);
          else if (act == 1) start_challenge(c, m);
          else if (act == 2) open_challenges();
          else go_menu();
        }
      }
      break;
    case SC_SKINS:
      if (back) {
        save(), go_menu();
        break;
      }
      if (sel < 0) {
        tab = (tab + dx + 4) % 4;
        if (dy > 0 || ok) sel = sv.equip[tab];
      } else {
        int n = kind_count[tab];
        if (dx) sel = (sel + dx + n) % n;
        if (dy > 0 && sel + 7 < n) sel += 7;
        if (dy < 0) sel = sel >= 7 ? sel - 7 : -1;
        if (sel >= 0) {
          int k = kind_first[tab] + sel;
          if (bit(sv.unlocked, k)) bit_set(sv.seen, k);
          if (ok && bit(sv.unlocked, k)) sv.equip[tab] = (uint8_t)sel, apply_skins();
        }
      }
      break;
    case SC_CHALLENGES:
      if (back) {
        go_menu();
        break;
      }
      if (ch_sel < 0) {
        if (dx) ch_mirror_view = !ch_mirror_view, sv.mirror = ch_mirror_view;
        if (dy > 0 || ok) ch_sel = 0;
      } else {
        if (dx) ch_sel = (ch_sel + dx + 51) % 51;
        if (dy > 0) ch_sel = ch_sel + 9 < 51 ? ch_sel + 9 : ch_sel;
        if (dy < 0) ch_sel = ch_sel >= 9 ? ch_sel - 9 : -1;
        if (ok && ch_sel >= 0) apply_skins(), start_challenge(ch_sel, ch_mirror_view);
      }
      break;
  }
}

/* ---------------------------------------------------------------- the loop */
#define STRIP_ROWS 40
static color_t strip[SCREEN_W * STRIP_ROWS];
static void render(void) {
  for (int y = 0; y < SCREEN_H; y += STRIP_ROWS) {
    gfx_y0 = y, gfx_y1 = y + STRIP_ROWS;
    scene(0);
    np_push(0, y, SCREEN_W, STRIP_ROWS, strip);
  }
}

/* frames drawn, the slowest one's work and how many took over 33 ms (tools/emu.py reads them) */
__attribute__((used)) uint32_t perf_frames, perf_max, perf_slow;
int main(void) {
  plat_begin();
  color_t *their = gfx_buf;
  gfx_buf = strip;
  dr_init();
  now = plat_millis();
  load();
  check_goals(false, 0);
  for (int k = 0; k < 4; k++) bit_set(sv.seen, kind_first[k] + sv.equip[k]), bit_set(sv.unlocked, kind_first[k]);
  apply_skins();
  start_endless();
  nnews = 0;
  if (!(sv.flags & 1)) sv.flags |= 1, help_back = SC_TITLE, sc = SC_HELP;
  uint32_t held = plat_keys(), stale = held, tick = now, last_save = now;
  for (;;) {
    now = plat_millis();
    uint32_t raw = plat_keys(), k = raw & ~(stale &= raw), hit = k & ~held;
    held = k;
    if (raw & PK_HOME) break;
#ifdef FD_AUTOPLAY /* (the gallery's recording: src/bot.c flies) */
    {
      extern bool bot_wants_flap(int effort);
      if (sc == SC_PLAY && ((G.state == ST_READY && (now / 500) % 2) || (G.state == ST_PLAY && bot_wants_flap(8)))) hit |= PK_OK;
      static uint32_t res_t;
      if (sc != SC_RESULT) res_t = 0;
      else if (!res_t) res_t = now;
      else if (now - res_t > 1500) hit |= PK_OK, res_t = 0;
    }
#endif
    keys(hit);
    if (sc == SC_QUIT && (hit & (PK_OK | PK_EXE)) && sel == 0) break;
    /* the physics at the original's 100 steps a second, catching up after a slow frame */
    bool running = sc == SC_PLAY || sc == SC_TITLE || sc == SC_RESULT || sc == SC_NEWSKIN || (sc == SC_HELP && help_back == SC_TITLE);
    if (!running || now - tick > 100) tick = now - (running ? 100 : 0);
    while (now - tick >= 10) {
      tick += 10;
      game_step();
    }
    if (sc == SC_PLAY && (G.state == ST_DEAD || G.state == ST_WON)) {
      if (!dead_t) dead_t = now;
      if (now - dead_t > (G.state == ST_WON ? 700u : 1000u)) dead_t = 0, after_run();
    }
    if (now - last_save > 20000) last_save = now, save();
    render();
    frame_ms = plat_millis() - now;
    perf_frames++;
    if (sc == SC_PLAY && G.state == ST_PLAY) {
      if (frame_ms > perf_max) perf_max = frame_ms;
      perf_slow += frame_ms > 33;
    }
    if (frame_ms < 20) plat_sleep(20 - frame_ms);
  }
  save();
  gfx_buf = their;
  return plat_end();
}
