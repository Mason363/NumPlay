/* Friday Night Funkin' on the NumWorks calculator: the songs of the game's weeks against their singers, as the
 * game plays them (game.c), silent (the calculator has no speaker): the notes, the characters and the camera keep
 * the beat. The screens: screens.c and draw.c.
 *
 * Keys: Left, Down, Up and Right (or 7, 8, ( and )) hit the notes; Back pauses; Home saves and quits. */
#include <string.h>
#include "app.h"
#include "draw.h"
#include "plat.h"

/* (seen by tools/emu.py: the frames drawn in songs, the slowest (ms), those over 33 ms, the longest time between
 * two readings of the keys in a song (ms); and the song, its clock and the app's screen, for tests that play it) */
#ifndef HOST
#define SEEN __attribute__((used, externally_visible))
#else
#define SEEN
#endif
SEEN uint32_t perf_frames, perf_max, perf_slow, perf_gap;
SEEN fnf_game_t G;
SEEN app_t A;
SEEN uint32_t origin;   /* the song's clock: plat_millis() at its start */
SEEN int32_t start_us;
static fnf_progress_t P;
static uint8_t *chart_buf;   /* the chart, unpacked (on main's stack) */
static uint32_t pause_at;
static bool saved;

/* ------------------------------------------------------------------------------------------------ the keys */
typedef struct {
  uint32_t t;
  uint8_t lane, down;
} key_event_t;
static key_event_t queue[64];
static unsigned q_head, q_len;
static uint8_t lanes, stale_lanes;
static uint32_t keys_now, last_poll;

static uint8_t lanes_of(uint32_t k) {
  /* both keys of a lane count as one: holding the other keeps a hold going */
  return (uint8_t)((k & (FK_LEFT | FK_7) ? 1 : 0) | (k & (FK_DOWN | FK_8) ? 2 : 0) | (k & (FK_UP | FK_LPAR) ? 4 : 0) |
                   (k & (FK_RIGHT | FK_RPAR) ? 8 : 0));
}

/* read the keys and time-stamp the lanes' changes (also between the strips of a frame being drawn) */
static void poll(void) {
  uint32_t t = plat_millis();
  if (last_poll && t - last_poll > perf_gap && A.view == V_PLAY) perf_gap = t - last_poll;
  last_poll = t;
  keys_now = plat_keys();
  uint8_t l = lanes_of(keys_now);
  stale_lanes &= l;   /* a key held from before counts once let go of */
  l &= (uint8_t)~stale_lanes;
  if (A.view == V_PLAY) {
    uint8_t changed = l ^ lanes;
    for (int i = 0; i < 4; i++)
      if (changed >> i & 1 && q_len < 64) {
        queue[(q_head + q_len) % 64] = (key_event_t){t, (uint8_t)i, (uint8_t)(l >> i & 1)};
        q_len++;
      }
  }
  lanes = l;
}
extern void (*draw_between)(void);

static int32_t song_time(uint32_t ms) { return (int32_t)(ms - origin) * 1000 + start_us; }

/* ------------------------------------------------------------------------------------------------ saving */
static void save(void) {
  save_write(&P);
  saved = true;
}

/* ------------------------------------------------------------------------------------------------ songs */
static void go(int view) {
  A.view = (uint8_t)view;
  A.menu_at = A.clock;
}

static void menu_back(void) {
  go(A.story ? V_STORY : V_FREEPLAY);
  A.story = 0;
}

static unsigned current_chart(void) {
  if (A.story) return (unsigned)app_story_chart(fnf_level[P.week].song[A.story_song], P.week_diff);
  return (unsigned)app_chart(A.list[P.song < A.list_n ? P.song : 0], P.list, P.song_diff);
}

static void start_song(bool fresh) {
  unsigned chart = current_chart();
  if (chart >= FNF_CHARTS || !fnf_start(&G, chart, chart_buf, P.offset, A.story) || !play_load(&G)) {
    go(V_ERROR);
    return;
  }
  origin = last_poll = plat_millis();   /* (the time unpacking took isn't a gap in the keys' reading) */
  if (fresh) A.play_t0 = origin;
  else A.play_t0 = origin - 5000;   /* (a restart: the arrows are there already) */
  A.fade_at = 0;
  start_us = G.now;
  q_len = 0;
  stale_lanes = lanes_of(keys_now);
  lanes = 0;
  go(V_PLAY);
}

/* the guide the first time, then the song */
static void begin(void) {
  if (!P.guide) {
    P.guide = 1;
    A.first_run = 1, A.page = 0, A.under = V_PLAY;
    go(V_HELP);
    return;
  }
  start_song(true);
}

static void results(const fnf_game_t *g, bool week) {
  if (week) {
    fnf_game_t sum;
    memset(&sum, 0, sizeof sum);
    memcpy(sum.tally, A.week_tally, sizeof sum.tally);
    sum.total = A.week_total;
    A.result_rank = (uint8_t)fnf_rank(&sum);
    A.result_completion = fnf_completion(&sum);
    memcpy(A.result_tally, A.week_tally, sizeof A.result_tally);
    A.result_total = A.week_total, A.result_max_combo = A.week_max_combo;
    A.result_score = A.campaign;
  } else {
    A.result_rank = (uint8_t)fnf_rank(g);
    A.result_completion = fnf_completion(g);
    memcpy(A.result_tally, g->tally, sizeof A.result_tally);
    A.result_total = g->total, A.result_max_combo = g->max_combo;
    A.result_score = fnf_score(g);
  }
  go(V_RESULTS);
}

static void song_won(void) {
  int32_t score = fnf_score(&G);
  int32_t *best = A.story ? &P.week_score[P.week][P.week_diff] : &P.chart[G.chart].score;
  int32_t before = *best;
  bool had = A.story ? true : P.chart[G.chart].rank != 0;
  fnf_record(&P, G.chart, score, fnf_rank(&G), fnf_completion(&G));
  if (A.story) {
    A.campaign += score;
    for (int j = 0; j < 5; j++) A.week_tally[j] = (uint16_t)(A.week_tally[j] + G.tally[j]);
    A.week_total = (uint16_t)(A.week_total + G.total);
    if (G.max_combo > A.week_max_combo) A.week_max_combo = G.max_combo;
    if (A.story_song + 1 < fnf_level[P.week].n) {
      A.story_song++;
      save();
      start_song(true);
      return;
    }
    A.result_new = A.campaign > before || !before;
    if (A.campaign > *best || !*best) *best = A.campaign;
    save();
    results(&G, true);
    return;
  }
  A.result_new = !had || score > before;
  save();
  results(&G, false);
}

/* ------------------------------------------------------------------------------------------------ the game over */
/* GameOverSubState: firstDeath, then deathLoop until OK (deathConfirm, then the screen fades out and the song starts
   again, fading in) or Back (the menu) */
static void game_over(void) {
  const fnf_death_t *d = over_death(&G);
  A.sel = 0, A.over_anim = 0;
  A.over_at = A.over_anim_at = A.clock;
  A.fade_steps = d && (d->flags & FNF_DF_PIXEL);
  over_begin(&G);
  go(V_GAMEOVER);
}

/* when firstDeath gives way to deathLoop (ms; 0: never, no death animations) */
static uint32_t first_death_ms(const fnf_death_t *d) {
  if (!d || d->anim == FNF_NONE) return 0;
  const fnf_death_anim_t *an = &fnf_death_anim[d->anim];
  return (d->loop_at ? d->loop_at : an->len) * 1000u / an->fps;
}

static void over_keys(uint32_t hit, uint32_t now) {
  const fnf_death_t *d = over_death(&G);
  uint32_t first = first_death_ms(d);
  if (A.over_anim == 0 && first && now - A.over_anim_at >= first) A.over_anim = 1, A.over_anim_at += first;
  if (A.over_anim < 2) {
    if (hit & FK_OK) A.over_anim = 2, A.over_anim_at = now;
    else if (hit & FK_BACK) menu_back();
  } else if (now - A.over_anim_at >= (d ? d->fade : 0) + 2000u) {
    start_song(false);
    if (A.view == V_PLAY) A.fade_at = plat_millis();
  }
}

static void resume(void) {
  uint32_t now = last_poll = plat_millis();
  origin += now - pause_at;
  /* keys let go of during the pause: let go of now; keys pressed during it: not until let go of */
  uint8_t l = lanes_of(keys_now);
  int32_t t = song_time(now);
  for (int i = 0; i < 4; i++)
    if ((G.held >> i & 1) && !(l >> i & 1)) fnf_release(&G, i, t);
  stale_lanes = (uint8_t)(l & ~G.held);
  lanes = (uint8_t)(l & G.held);
  q_len = 0;
  G.offset = P.offset * 1000;
  go(V_PLAY);
}

/* ------------------------------------------------------------------------------------------------ menus */
static int wrap(int v, int n) { return ((v % n) + n) % n; }

static void story_diff_fix(void) {
  unsigned diffs = app_story_diffs(P.week);
  for (int i = 0; i < FNF_DIFFS && !(diffs >> P.week_diff & 1); i++) P.week_diff = (uint8_t)((P.week_diff + 1) % FNF_DIFFS);
}

static void song_diff_fix(void) {
  if (!A.list_n) return;
  if (P.song >= A.list_n) P.song = 0;
  for (int i = 0; i < FNF_DIFFS && app_chart(A.list[P.song], P.list, P.song_diff) < 0; i++)
    P.song_diff = (uint8_t)((P.song_diff + 1) % FNF_DIFFS);
}

static void play_keys(uint32_t hit) {
  if (hit & FK_BACK) {
    pause_at = plat_millis();
    A.sel = PAUSE_RESUME;
    go(V_PAUSE);
  }
}

int main(void) {
  /* (the big buffers on the stack: the app's RAM stays under what custom builds of Epsilon give it) */
  uint8_t chart[FNF_CHART_RAW_MAX];
  color_t strip[SCREEN_W * STRIP_H];
  if (!chart_buf) chart_buf = chart;
  draw_strip_buffer(strip);
  plat_begin();
  memset(&A, 0, sizeof A);
  A.p = &P, A.g = &G;
  save_load(&P);
  bold_load();
  draw_between = poll;
  keys_now = plat_keys();
  uint32_t stale = keys_now, prev = stale, rep_at = 0;
  uint8_t shown = 0xFF;
  go(V_TITLE);
  app_build_list(&A);
  bool quit = false;
  while (!quit) {
    poll();
    uint32_t now = plat_millis();
    A.clock = now;
    uint32_t keys = keys_now;
    stale &= keys;   /* keys still down from the screen before don't count */
    uint32_t live = keys & ~stale, hit = live & ~prev;
    /* arrows held: again after 350 ms, then every 110 ms (not in a song) */
    uint32_t arrows = live & (FK_LEFT | FK_RIGHT | FK_UP | FK_DOWN);
    if (A.view != V_PLAY) {
      if (hit & arrows) rep_at = now + 350;
      else if (arrows && (int32_t)(now - rep_at) >= 0) hit |= arrows, rep_at = now + 110;
    }
    prev = live;
    if (keys & FK_HOME) break;   /* Home: saved below, then out */
    int up = hit & FK_UP ? -1 : 0, down = hit & FK_DOWN ? 1 : 0, ud = up + down;
    int lr = (hit & FK_RIGHT ? 1 : 0) - (hit & FK_LEFT ? 1 : 0);
    switch (A.view) {
      case V_TITLE:
        if (hit & FK_OK) A.sel = MAIN_STORY, go(V_MAIN);
        else if (hit & FK_BACK) quit = true;
        break;
      case V_MAIN:
        A.sel = (uint8_t)wrap(A.sel + ud, MAIN_COUNT);
        if (hit & FK_BACK) go(V_TITLE);
        else if (hit & FK_OK) switch (A.sel) {
          case MAIN_STORY: story_diff_fix(), go(V_STORY); break;
          case MAIN_FREEPLAY: app_build_list(&A), song_diff_fix(), go(V_FREEPLAY); break;
          case MAIN_OPTIONS: A.under = V_MAIN, A.sel = 0, go(V_OPTIONS); break;
          case MAIN_HELP: A.under = V_MAIN, A.page = 0, A.first_run = 0, go(V_HELP); break;
          default: go(V_CREDITS); break;
        }
        break;
      case V_STORY:
        if (ud) P.week = (uint8_t)wrap(P.week + ud, FNF_LEVELS), story_diff_fix();
        if (lr) {
          unsigned diffs = app_story_diffs(P.week);
          for (int i = 0; i < FNF_DIFFS; i++) {
            P.week_diff = (uint8_t)wrap(P.week_diff + lr, FNF_DIFFS);
            if (diffs >> P.week_diff & 1) break;
          }
        }
        if (hit & FK_BACK) A.sel = MAIN_STORY, go(V_MAIN);
        else if (hit & FK_OK) {
          A.story = 1, A.story_song = 0, A.campaign = 0;
          memset(A.week_tally, 0, sizeof A.week_tally);
          A.week_total = A.week_max_combo = 0;
          begin();
        }
        break;
      case V_FREEPLAY:
        if (ud && A.list_n) P.song = (uint8_t)wrap(P.song + ud, A.list_n), song_diff_fix();
        if (lr && A.list_n)
          for (int i = 0; i < FNF_DIFFS; i++) {
            P.song_diff = (uint8_t)wrap(P.song_diff + lr, FNF_DIFFS);
            if (app_chart(A.list[P.song], P.list, P.song_diff) >= 0) break;
          }
        if (hit & FK_SHIFT) {   /* Boyfriend's songs, or Pico's */
          unsigned song = A.list_n ? A.list[P.song] : 0;
          P.list ^= 1;
          app_build_list(&A);
          P.song = 0;
          for (int i = 0; i < A.list_n; i++)
            if (A.list[i] == song) P.song = (uint8_t)i;
          song_diff_fix();
        }
        if (hit & FK_BACK) A.sel = MAIN_FREEPLAY, go(V_MAIN);
        else if ((hit & FK_OK) && A.list_n) A.story = 0, begin();
        break;
      case V_OPTIONS:
        A.sel = (uint8_t)wrap(A.sel + ud, OPT_COUNT);
        if (A.sel == OPT_DOWNSCROLL && (lr || (hit & FK_OK))) P.downscroll ^= 1;
        if (A.sel == OPT_OFFSET && lr) {
          int o = P.offset + lr;
          P.offset = (int16_t)(o < -FNF_OFFSET_MAX ? -FNF_OFFSET_MAX : o > FNF_OFFSET_MAX ? FNF_OFFSET_MAX : o);
        }
        if (hit & FK_BACK) {
          save();
          A.sel = A.under == V_PAUSE ? PAUSE_OPTIONS : MAIN_OPTIONS;
          go(A.under);
        }
        break;
      case V_HELP:
        if (A.first_run && (hit & FK_BACK)) {
          A.first_run = 0;
          save();
          start_song(true);
        } else if ((lr > 0 || (A.first_run && (hit & FK_OK))) && A.page + 1 < HELP_PAGES) {
          A.page++;
        } else if (lr < 0 && A.page) {
          A.page--;
        } else if (A.first_run && (hit & FK_OK)) {
          A.first_run = 0;
          save();
          start_song(true);
        } else if (hit & (FK_BACK | FK_OK)) {
          A.sel = A.under == V_PAUSE ? PAUSE_HELP : MAIN_HELP;
          go(A.under);
        }
        break;
      case V_CREDITS:
        if (hit & (FK_BACK | FK_OK)) A.sel = MAIN_CREDITS, go(V_MAIN);
        break;
      case V_PLAY: {
        while (q_len) {
          key_event_t e = queue[q_head];
          q_head = (q_head + 1) % 64, q_len--;
          int32_t t = song_time(e.t);
          if (t < G.now) t = G.now;
          if (e.down) fnf_press(&G, e.lane, t);
          else fnf_release(&G, e.lane, t);
        }
        fnf_advance(&G, song_time(plat_millis()));
        if (G.over == FNF_WON) song_won();
        else if (G.over == FNF_DIED) game_over();
        else play_keys(hit);
        if (A.fade_at && (int32_t)(now - A.fade_at) > 1100) A.fade_at = 0;
        break;
      }
      case V_PAUSE:
        A.sel = (uint8_t)wrap(A.sel + ud, PAUSE_COUNT);
        if (hit & FK_BACK) resume();
        else if (hit & FK_OK) switch (A.sel) {
          case PAUSE_RESUME: resume(); break;
          case PAUSE_RESTART: start_song(false); break;
          case PAUSE_EXIT: menu_back(); break;
          case PAUSE_OPTIONS: A.under = V_PAUSE, A.sel = 0, go(V_OPTIONS); break;
          case PAUSE_HELP: A.under = V_PAUSE, A.page = 0, A.first_run = 0, go(V_HELP); break;
          default: A.sel = 0, go(V_QUIT); break;
        }
        break;
      case V_QUIT:
        if (hit & (FK_LEFT | FK_RIGHT | FK_UP | FK_DOWN)) A.sel ^= 1;
        if (hit & FK_BACK) A.sel = PAUSE_QUIT, go(V_PAUSE);
        else if (hit & FK_OK) {
          if (A.sel) quit = true;
          else A.sel = PAUSE_QUIT, go(V_PAUSE);
        }
        break;
      case V_RESULTS:
        if (hit & (FK_OK | FK_BACK)) menu_back();
        break;
      case V_GAMEOVER:
        over_keys(hit, now);
        break;
      default:
        if (hit & (FK_OK | FK_BACK)) A.story = 0, go(V_MAIN);
        break;
    }
    if (A.view != shown) {
      stale = keys, prev = 0;   /* a new screen starts with all keys up */
      shown = A.view;
    }
    uint32_t t0 = plat_millis();
    screens_draw(&A);
    uint32_t spent = plat_millis() - t0;
    if (A.view == V_PLAY) {
      perf_frames++;
      if (spent > perf_max) perf_max = spent;
      if (spent > 33) perf_slow++;
    } else {
      plat_sleep(8);
    }
  }
  save();
  (void)saved;
  return plat_end();
}
