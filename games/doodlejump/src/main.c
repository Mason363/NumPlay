/* Doodle Jump on the NumWorks calculator: Lima Sky's game with its own pictures, rules and screens, driven by the
 * keypad. The arrows steer, OK shoots, Back pauses, Home saves and quits.
 *
 * The screens are the original's: the title with its buttons, the page (game.c, draw.c), the pause, game over,
 * and the scores and stats. Made by Mason Chen as part of NumPlay. */
#include <stddef.h>
#include "../../common/np_lz.h"
#include "dj.h"
#include "gfx.h"
#include "plat.h"

/* ---------------------------------------------------------------- the save
 * doodlejump.sav: the scores, the stats, and the climb in progress (kept when you leave). Version 1 (Doodle Jump
 * inside NumPlay) kept the best score only: it comes back as the first of the scores. A copy goes into
 * doodlejump_saves.py, which installing the app again keeps; with neither file, NumPlay's own copy of the old save
 * (numplay_saves.py) is read. */
#define SAVE_NAME "doodlejump.sav"
#define COPY_NAME "doodlejump_saves.py"
#define NUMPLAY_COPY "numplay_saves.py"
#define VERSION 2
#define NBEST 10
enum { END_FELL, END_MONSTER, END_UFO, END_HOLE };

typedef struct {
  uint8_t magic, version, flags, pad; /* 'D', 2; flags: 1 a climb kept, 2 the guide read */
  uint32_t sum;                       /* FNV-1a of what follows: a damaged file starts afresh */
  uint32_t best[NBEST];               /* the high scores, best first */
  uint32_t games, last, total, jumps, mon_shot, mon_stomped, ufo_shot, ufo_stomped, jets, hats;
  uint32_t ends[4];                   /* how the climbs ended (END_*) */
  world_t w;
} save_t;
static save_t sv;
#define OLD_SIZE 488 /* version 1 */

static uint32_t fnv(const uint8_t *d, uint32_t n) {
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
  return h;
}
#define SUM_FROM offsetof(save_t, best)
static uint32_t save_sum(const save_t *s) { return fnv((const uint8_t *)s + SUM_FROM, sizeof *s - SUM_FROM); }

/* a save's bytes into sv: version 2 as it is, version 1's best score and guide; false if neither */
static bool take(const uint8_t *d, uint32_t n) {
  static save_t t;
  if (n == sizeof t && d[0] == 'D' && d[1] == VERSION) {
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&t)[i] = d[i];
    if (save_sum(&t) != t.sum || t.w.score > 100000000u) return false;
    sv = t;
    return true;
  }
  if (n == OLD_SIZE && d[0] == 'D' && d[1] == 1) { /* its sum: over the words after it */
    uint32_t h = 0x811C9DC5u, w;
    for (uint32_t i = 8; i < n; i += 4) w = (uint32_t)(d[i] | d[i + 1] << 8 | d[i + 2] << 16 | (uint32_t)d[i + 3] << 24), h = (h ^ w) * 0x01000193u;
    if (h != (uint32_t)(d[4] | d[5] << 8 | d[6] << 16 | (uint32_t)d[7] << 24)) return false;
    for (uint32_t i = 0; i < sizeof sv; i++) ((uint8_t *)&sv)[i] = 0;
    sv.best[0] = (uint32_t)(d[8] | d[9] << 8 | d[10] << 16 | (uint32_t)d[11] << 24);
    sv.flags = d[2] & 2;
    return true;
  }
  return false;
}

static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static int b64v(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return c == '=' ? 0 : -1;
}
/* decodes n characters of base64 into out (cap bytes); the bytes, or -1 */
static int32_t unb64(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap) {
  uint32_t o = 0;
  if (n % 4) return -1;
  for (uint32_t i = 0; i < n; i += 4) {
    int v[4];
    for (int k = 0; k < 4; k++)
      if ((v[k] = b64v(in[i + k])) < 0) return -1;
    uint32_t w = (uint32_t)(v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3]);
    int m = 3 - (in[i + 3] == '=') - (in[i + 2] == '=');
    for (int k = 0; k < m; k++) {
      if (o >= cap) return -1;
      out[o++] = (uint8_t)(w >> (16 - 8 * k));
    }
  }
  return (int32_t)o;
}

/* the line of a copy for SAVE_NAME: "#>doodlejump.sav:base64" (as it is) or "#=doodlejump.sav:size:base64" (packed) */
static bool take_copy(const char *file) {
  uint32_t n;
  const uint8_t *c = plat_load(file, &n);
  static uint8_t buf[sizeof(save_t) + 8], packed[NP_LZ_BOUND(sizeof(save_t)) + 8];
  static const char name[] = SAVE_NAME ":";
  if (!c) return false;
  for (uint32_t i = 0; i + 2 + sizeof name < n; i++) {
    if (c[i] != '#' || (c[i + 1] != '>' && c[i + 1] != '=') || (i && c[i - 1] != '\n' && c[i - 1] != 0)) continue;
    uint32_t k = 0;
    while (name[k] && c[i + 2 + k] == (uint8_t)name[k]) k++;
    if (name[k]) continue;
    const uint8_t *l = c + i + 2 + k;
    uint32_t left = n - (uint32_t)(l - c), size = 0;
    if (c[i + 1] == '=') { /* its size, then ':' */
      while (left && *l >= '0' && *l <= '9' && size < 0x10000) size = size * 10 + (uint32_t)(*l++ - '0'), left--;
      if (!left || *l != ':' || size > sizeof buf) return false;
      l++, left--;
    }
    uint32_t m = 0;
    while (m < left && l[m] != '\n' && l[m] != 0) m++;
    int32_t got;
    if (size) {
      got = unb64(l, m, packed, sizeof packed);
      if (got < 0 || np_lz_unpack(packed, (uint32_t)got, buf, sizeof buf) != (int32_t)size) return false;
      got = (int32_t)size;
    } else if ((got = unb64(l, m, buf, sizeof buf)) < 0) return false;
    return take(buf, (uint32_t)got);
  }
  return false;
}

static void load(void) {
  uint32_t n;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  for (uint32_t i = 0; i < sizeof sv; i++) ((uint8_t *)&sv)[i] = 0;
  if (d && take(d, n)) return;
  if (take_copy(COPY_NAME) || take_copy(NUMPLAY_COPY)) return;
  for (uint32_t i = 0; i < sizeof sv; i++) ((uint8_t *)&sv)[i] = 0;
}

static void save(void) {
  static const char head[] = "# Doodle Jump's saves: keep this file\n#>" SAVE_NAME ":";
  static uint8_t copy[1 + sizeof head + (sizeof(save_t) + 2) / 3 * 4 + 2];
  sv.magic = 'D', sv.version = VERSION, sv.w = W, sv.sum = save_sum(&sv);
  plat_save(SAVE_NAME, &sv, sizeof sv);
  uint8_t *p = copy;
  const uint8_t *f = (const uint8_t *)&sv;
  *p++ = 0; /* (a script's first byte: not imported) */
  for (uint32_t i = 0; i < sizeof head - 1; i++) *p++ = (uint8_t)head[i];
  for (uint32_t i = 0; i < sizeof sv; i += 3, p += 4) {
    uint32_t left = sizeof sv - i;
    uint32_t v = (uint32_t)f[i] << 16 | (left > 1 ? (uint32_t)f[i + 1] << 8 : 0) | (left > 2 ? f[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63], p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=', p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  *p++ = 0; /* (a script's text ends with a zero) */
  plat_save(COPY_NAME, copy, (uint32_t)(p - copy));
}

/* a climb has ended: its score among the best, and the stats */
static void record(int end) {
  uint32_t s = W.score;
  sv.games++, sv.last = s, sv.total += s, sv.jumps += W.jumps, sv.mon_shot += W.mon_shot;
  sv.mon_stomped += W.mon_stomped, sv.ufo_shot += W.ufo_shot, sv.ufo_stomped += W.ufo_stomped;
  sv.jets += W.jets, sv.hats += W.hats;
  if (end >= 0) sv.ends[end]++;
  for (int i = 0; i < NBEST; i++)
    if (s > sv.best[i]) {
      for (int j = NBEST - 1; j > i; j--) sv.best[j] = sv.best[j - 1];
      sv.best[i] = s;
      break;
    }
  sv.flags &= (uint8_t)~1;
}

/* ---------------------------------------------------------------- the screens */
enum { SC_TITLE, SC_HELP, SC_SCORES, SC_PLAY, SC_PAUSE, SC_ASK, SC_OVER };
static int screen, sel, tab;
static uint32_t now, anim; /* the title's frames */
static bool margins;       /* the sides of the page to draw again */

static char *num(char *o, uint32_t v) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static const char *label(const char *s, uint32_t v) {
  static char b[48];
  char *o = b;
  while (*s) *o++ = *s++;
  num(o, v);
  return b;
}

/* the original's button: cream, or pink when it's the one OK presses */
static void button(int cx, int cy, const char *s, bool on) {
  gfx_spr(on ? S_BUTTON_ON : S_BUTTON, cx, cy, 32);
  gfx_text(F_BIG, cx, cy + 6, s, on ? RED_INK : INK, 1);
}

static void paper(void) { gfx_paper(0, 0, SCREEN_W, SCREEN_H, 141, 0, 0, 0); }

static const char *const title_items[] = {"play", "scores", "how to play"};
static void draw_title(void) {
  paper();
  gfx_spr(S_LOGO, 14, 14, 32);
  /* the doodler jumping on his platform, the UFO, the black hole and a monster, as on the original's */
  int t = (int)(anim % 56), h = t * (56 - t) / 10; /* (a jump of 78 pixels, 56 frames) */
  gfx_spr(S_T_HOLE, 158, 132, 32);
  gfx_spr(S_T_UFO, 282, 34 + (int)(anim / 10 % 2), 32);
  gfx_spr(S_T_EYE, 136, 205 + (int)(anim / 12 % 2), 32);
  gfx_spr(S_T_GREEN, 58, 214, 32);
  gfx_spr(t < 6 || t > 52 ? S_T_DOODJ : S_T_DOOD, 58, 215 - h, 32);
  for (int i = 0; i < 3; i++) button(246, 104 + 48 * i, title_items[i], sel == i);
}

static void draw_help(void) {
  paper();
  gfx_text(F_BIG, 160, 30, "how to play", RED_INK, 1);
  /* the original's tutorial, its words for the keypad */
  draw_doodler_at(48, 76, true, false);
  gfx_text(F_SMALL, 104, 64, "left and right to move", INK, 0);
  gfx_spr(S_T_GREEN, 36, 100, 32), gfx_spr(S_T_SPRING, 48, 94, 32), gfx_spr(S_T_BLUE, 70, 112, 32);
  gfx_text(F_SMALL, 104, 110, "jump on these", INK, 0);
  gfx_spr(S_M_HOLE, 24, 150, 32), gfx_spr(S_T_BROWN, 70, 152, 32);
  gfx_text(F_SMALL, 104, 154, "avoid", INK, 0);
  gfx_spr(S_M_BLOB0, 52, 192, 32);
  gfx_text(F_SMALL, 104, 186, "jump on monsters", INK, 0);
  gfx_text(F_SMALL, 104, 204, "or press OK to shoot", INK, 0);
  gfx_text(F_SMALL, 160, 234, "Back pauses. Home saves and quits.", INK, 1);
}

static void draw_scores(void) {
  char b[16];
  paper();
  gfx_text(F_BIG, 90, 30, "scores", tab == 0 ? RED_INK : INK, 1);
  gfx_text(F_BIG, 230, 30, "stats", tab == 1 ? RED_INK : INK, 1);
  gfx_fill(tab == 0 ? 50 : 196, 36, tab == 0 ? 80 : 68, 2, RED_INK);
  if (tab == 0) {
    for (int i = 0; i < NBEST; i++) {
      int x = i < 5 ? 40 : 190, y = 68 + (i % 5) * 30;
      char *o = num(b, (uint32_t)i + 1);
      *o++ = '.', *o = 0;
      gfx_text(F_BIG, x + 18, y, b, INK, 2);
      if (sv.best[i]) gfx_text(F_BIG, x + 26, y, num(b, sv.best[i]) ? b : b, INK, 0);
    }
  } else { /* the original's stats, those a calculator keeps */
    static const char *const left[] = {"total games played", "high score", "last score", "average score",
                                       "total jumps", "monsters shot", "monsters jumped on", "UFOs shot"};
    static const char *const right[] = {"UFOs jumped on", "jetpack flights", "propellerhat flights", "HOW YOU ENDED",
                                        "missed a platform", "jumped into a monster", "abducted by UFO", "black hole"};
    uint32_t lv[] = {sv.games, sv.best[0], sv.last, sv.games ? sv.total / sv.games : 0, sv.jumps, sv.mon_shot,
                     sv.mon_stomped, sv.ufo_shot};
    uint32_t rv[] = {sv.ufo_stomped, sv.jets, sv.hats, 0, sv.ends[END_FELL], sv.ends[END_MONSTER], sv.ends[END_UFO],
                     sv.ends[END_HOLE]};
    for (int i = 0; i < 8; i++) {
      int y = 62 + i * 20;
      gfx_text(F_SMALL, 8, y, left[i], INK, 0), gfx_text(F_SMALL, 156, y, num(b, lv[i]) ? b : b, RED_INK, 2);
      gfx_text(F_SMALL, 166, y, right[i], i == 3 ? RED_INK : INK, 0);
      if (i != 3) gfx_text(F_SMALL, 314, y, num(b, rv[i]) ? b : b, RED_INK, 2);
    }
  }
  gfx_text(F_SMALL, 160, 230, "Left Right: scores or stats", INK, 1);
}

static void draw_over(void) {
  paper();
  gfx_spr(S_GAMEOVER, 160, 10, 32);
  gfx_text(F_BIG, 160, 92, label("your score: ", W.score), INK, 1);
  gfx_text(F_BIG, 160, 120, label("your high score: ", sv.best[0]), INK, 1);
  button(160, 160, "play again", sel == 0);
  button(160, 208, "menu", sel == 1);
  gfx_spr(S_T_DOODJ, 50, 214, 32);
}

/* the sides of the page: the high score on the left, the keys on the right */
static void draw_margins(void) {
  char b[16];
  for (int side = 0; side < 2; side++) {
    gfx_begin(side ? 240 : 0, 0, 80, SCREEN_H);
    gfx_paper(side ? 240 : 0, 0, 80, SCREEN_H, 141, 0, 0, 12);
    gfx_fill(side ? 240 : 78, 0, 2, SCREEN_H, RGB(120, 110, 96));
    if (!side) {
      gfx_text(F_SMALL, 40, 30, "high score", INK, 1);
      gfx_text(F_BIG, 40, 56, num(b, sv.best[0]) ? b : b, RED_INK, 1);
    } else {
      static const char *const keys[][2] = {{"Left Right", "move"}, {"OK", "shoot"}, {"Back", "pause"}};
      for (int i = 0; i < 3; i++) {
        gfx_text(F_SMALL, 280, 150 + 30 * i, keys[i][0], RED_INK, 1);
        gfx_text(F_SMALL, 280, 164 + 30 * i, keys[i][1], INK, 1);
      }
    }
    gfx_end();
  }
}

static const char *const pause_items[] = {"resume", "menu", "quit game"};
static void draw_play(void) {
  gfx_begin(80, 0, 160, SCREEN_H);
  draw_page();
  if (screen == SC_PAUSE || screen == SC_ASK) {
    gfx_shade(80, 0, 160, SCREEN_H, RGB(247, 240, 232), 22);
    if (screen == SC_PAUSE) {
      gfx_text(F_BIG, 160, 56, "paused", RED_INK, 1);
      for (int i = 0; i < 3; i++) button(160, 96 + 48 * i, pause_items[i], sel == i);
    } else {
      gfx_text(F_BIG, 160, 86, "quit game?", RED_INK, 1);
      button(160, 130, "yes", sel == 0);
      button(160, 178, "no", sel == 1);
    }
  }
  gfx_end();
}

static void start_climb(void) {
  game_new(plat_random() ^ now);
  sv.flags |= 1;
  screen = SC_PLAY, margins = true;
}

/* read by tools/emu.py (make perf): the page's frames, the slowest, and how many took more than 33 ms */
uint32_t perf_frames __attribute__((used)), perf_max __attribute__((used)), perf_slow __attribute__((used));

static int end_of(int dead) {
  return dead == D_HOLE ? END_HOLE : dead == D_UFO ? END_UFO : dead == D_MONSTER ? END_MONSTER : END_FELL;
}

int main(void) {
  plat_begin();
  now = plat_millis();
  load();
  W = sv.w;
  if (sv.flags & 1) screen = SC_PAUSE, sel = 0, margins = true; /* the climb you left, paused */
  else if (!(sv.flags & 2)) screen = SC_HELP, sv.flags |= 2;   /* the first time: how to play */
  else screen = SC_TITLE;
  uint32_t held = plat_keys(), stale = held, last = now, acc = 0; /* keys still down from the launcher wait */
  for (;;) {
    now = plat_millis();
    uint32_t raw = plat_keys(), k = raw & ~(stale &= raw), hit = k & ~held;
    held = k;
    if (raw & K_HOME) break;
    int dy = !!(hit & K_DOWN) - !!(hit & K_UP), dx = !!(hit & K_RIGHT) - !!(hit & K_LEFT);
    bool ok = hit & K_OK, back = hit & K_BACK;
    switch (screen) {
    case SC_TITLE:
      sel = (sel + dy + 3) % 3;
      if (back) goto quit;
      if (ok) {
        if (sel == 0) start_climb();
        else screen = sel == 1 ? SC_SCORES : SC_HELP, tab = 0;
      }
      break;
    case SC_HELP:
      if (ok || back) screen = SC_TITLE, sel = 0;
      break;
    case SC_SCORES:
      if (dx) tab = !tab;
      if (ok || back) screen = SC_TITLE, sel = 1;
      break;
    case SC_PLAY:
      if (back) {
        screen = SC_PAUSE, sel = 0;
        save();
        break;
      }
      if (ok || (hit & K_UP)) game_shoot();
      break;
    case SC_PAUSE:
      sel = (sel + dy + 3) % 3;
      if (back) screen = SC_PLAY, margins = true;
      else if (ok) {
        if (sel == 0) screen = SC_PLAY, margins = true;
        else if (sel == 1) record(-1), screen = SC_TITLE, sel = 0, save();
        else screen = SC_ASK, sel = 0;
      }
      break;
    case SC_ASK:
      sel = (sel + dy + 2) % 2;
      if (back || (ok && sel == 1)) screen = SC_PAUSE, sel = 2;
      else if (ok) goto quit;
      break;
    case SC_OVER:
      sel = (sel + dy + 2) % 2;
      if (back) screen = SC_TITLE, sel = 0;
      else if (ok) {
        if (sel == 0) start_climb();
        else screen = SC_TITLE, sel = 0;
      }
      break;
    }
    /* the page: sixty steps a second, as the original, catching up after a slow frame */
    acc += (now - last) * 3, last = now;
    if (acc > 300) acc = 300; /* (at most 6 steps) */
    if (screen == SC_PLAY) {
      int steer = !!(k & K_RIGHT) - !!(k & K_LEFT);
      for (; acc >= 50; acc -= 50) {
        game_step(steer);
        if (game_over()) {
          record(end_of(W.dead));
          save();
          screen = SC_OVER, sel = 0;
          break;
        }
      }
    } else acc = 0;
    anim = now / 17; /* (the title's jump, at sixty frames a second) */
    plat_vblank();
    if (screen == SC_PLAY || screen == SC_PAUSE || screen == SC_ASK) {
      if (margins) draw_margins(), margins = false;
      draw_play();
    } else {
      gfx_screen(screen == SC_TITLE ? draw_title : screen == SC_HELP ? draw_help : screen == SC_SCORES ? draw_scores : draw_over);
      margins = true;
    }
    uint32_t spent = plat_millis() - now, frame = screen == SC_PLAY ? 16 : 33;
    if (screen == SC_PLAY) perf_frames++, perf_max = spent > perf_max ? spent : perf_max, perf_slow += spent > 33;
    if (spent < frame) plat_sleep(frame - spent);
  }
quit:
  save();
  return plat_end();
}
