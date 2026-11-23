/* Friday Night Funkin' on a computer, for tests and pictures (make host, make test).
 *
 *   play test DIR    the rules against the game's own numbers (the PBOT1 points and judgements, health, combo,
 *                    holds and their drops, misses, ghost misses, the note offset, ranks); every chart unpacked and
 *                    read; a bot playing every chart on time to a full combo (and the same scores on every
 *                    computer: a hash of them); bots hitting late and missing; a few hundred sessions of the whole
 *                    app with random keys; saves written and read back, damaged, and back from their copy; the
 *                    game over of the players through the app (deathLoop, OK, the fade, the song again, Back)
 *   play shots DIR   pictures of the screens (DIR/<name>.ppm)
 *   play film DIR CHART MS   MS milliseconds of a chart, a bot playing (DIR/<frame>_<ms>.raw, for tools/record.py)
 *   play gameover DIR CHARACTER [SONG]   a song of that player (fnf_char) with no key pressed: the game over, OK,
 *                    the song again (DIR/<frame>_<ms since the death>.raw)
 */
#define main fnf_app_main
#include "../src/main.c"
#undef main
#include <math.h>
#include <stdio.h>
#include "inflate.h"
#include <stdlib.h>

/* ---------------------------------------------------------------- the calculator, on a computer */
static uint16_t fb[240][320];
static void (*on_push)(int y, int h);
void plat_push(int x, int y, int w, int h, const uint16_t *px) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) fb[y + j][x + i] = px[j * w + i];
  if (on_push) on_push(y, h);
}
void *np_alloc(uint32_t n) { (void)n; return 0; }
static uint32_t clock_ms;
static uint32_t (*script)(uint32_t ms);
static uint32_t quit_at;
static void (*watch)(void);
uint32_t plat_keys(void) {
  clock_ms += 1;
  if (watch) watch();
  if (clock_ms >= quit_at) return FK_HOME;
  return script ? script(clock_ms) : 0;
}
uint32_t plat_millis(void) { return clock_ms; }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
void plat_begin(void) {}
int plat_end(void) { return 0; }

static struct { char name[32]; uint8_t data[4096]; uint32_t len; bool on; } files[4];
static bool storage_full;
bool plat_save(const char *name, const void *data, uint32_t len) {
  if (storage_full) return false;
  int k = -1;
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) k = i;
  for (int i = 0; i < 4 && k < 0; i++)
    if (!files[i].on) k = i;
  if (k < 0 || len > sizeof files[k].data) return false;
  strcpy(files[k].name, name), memcpy(files[k].data, data, len), files[k].len = len, files[k].on = true;
  return true;
}
const uint8_t *plat_load(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}
static void forget(const char *name) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) files[i].on = false;
}
static uint8_t *file_data(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}

static void shot(const char *dir, const char *name) {
  char path[512];
  snprintf(path, sizeof path, "%s/%s.ppm", dir, name);
  FILE *f = fopen(path, "wb");
  if (!f) exit(2);
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      uint16_t c = fb[y][x];
      fputc((c >> 11) * 255 / 31, f), fputc((c >> 5 & 63) * 255 / 63, f), fputc((c & 31) * 255 / 31, f);
    }
  fclose(f);
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- a chart's notes, read on their own */
typedef struct { int32_t t, len; uint8_t lane, kind; } note_t;
static note_t notes[FNF_CHART_NOTES_MAX];
static uint8_t raw[FNF_CHART_RAW_MAX];

static int read_chart(unsigned c) {
  const fnf_chart_t *ch = &fnf_chart[c];
  if (fnf_inflate(fnf_blob + ch->at, ch->size, raw, sizeof raw) != ch->raw) return -1;
  uint32_t pos = 0;
  int n = 0;
  int32_t t = 0;
  while (pos < ch->raw && n < FNF_CHART_NOTES_MAX) {
    uint32_t v[3] = {0, 0, 0};
    for (int k = 0; k < 3; k++) {
      if (k == 2 && !(v[1] & 8)) break;
      int s = 0;
      for (;;) {
        uint8_t b = raw[pos++];
        v[k] |= (uint32_t)(b & 127) << s;
        s += 7;
        if (!(b & 128)) break;
      }
    }
    t += (int32_t)v[0];
    notes[n++] = (note_t){t, (int32_t)v[2], (uint8_t)(v[1] & 7), (uint8_t)(v[1] >> 4)};
  }
  return pos == ch->raw ? n : -1;
}

static fnf_game_t T;
static uint8_t buf[FNF_CHART_RAW_MAX], app_chart_buf[FNF_CHART_RAW_MAX];
static color_t test_strip[SCREEN_W * STRIP_H];

/* the n-th player note (lane 0-3, scoreable) of chart c */
static note_t player_note(int n_notes, int k) {
  for (int i = 0; i < n_notes; i++)
    if (notes[i].lane < 4 && notes[i].kind != FNF_KIND_NON_SCOREABLE && !k--) return notes[i];
  return (note_t){0};
}

static int find_chart(const char *song, int kind, int diff) {
  for (int c = 0; c < FNF_CHARTS; c++) {
    const fnf_var_t *v = &fnf_var[fnf_chart[c].var];
    if (!strcmp(fnf_song[v->song].name, song) && v->kind == kind && fnf_chart[c].diff == diff) return c;
  }
  return -1;
}

/* a bot: each player note pressed on time (chords together), each hold kept to its end, other notes let go of
   30 ms on or just before the lane's next note */
typedef struct { int32_t t; uint8_t lane, down; uint16_t i; } bot_ev;
static bot_ev evs[2 * FNF_CHART_NOTES_MAX];
static int cmp_ev(const void *a, const void *b) {
  const bot_ev *x = a, *y = b;
  if (x->t != y->t) return x->t < y->t ? -1 : 1;
  if (x->down != y->down) return x->down < y->down ? -1 : 1;   /* (releases first) */
  if (x->lane != y->lane) return x->lane < y->lane ? -1 : 1;
  return x->i < y->i ? -1 : x->i > y->i;   /* (no ties left: the same order everywhere) */
}

static int bot_events(int n, int32_t late) {
  int k = 0;
  for (int i = 0; i < n; i++) {
    const note_t *a = &notes[i];
    if (a->lane >= 4) continue;
    int32_t next = INT32_MAX;
    for (int j = i + 1; j < n; j++)
      if (notes[j].lane == a->lane) {
        next = notes[j].t;
        break;
      }
    int32_t up = a->len > 0 ? a->t + a->len + 1000 : a->t + 30000;
    if (up > next - 2000) up = next - 2000;
    if (up <= a->t + late) up = a->t + late + 1;
    evs[k] = (bot_ev){a->t + late, a->lane, 1, (uint16_t)k};
    k++;
    evs[k] = (bot_ev){up, a->lane, 0, (uint16_t)k};
    k++;
  }
  qsort(evs, (size_t)k, sizeof evs[0], cmp_ev);
  return k;
}

/* ---------------------------------------------------------------- the rules */
static void judgements_and_points(void) {
  CHECK(fnf_judge(0) == J_SICK && fnf_judge(45) == J_SICK && fnf_judge(-45) == J_SICK, "Sick: up to 45 ms");
  CHECK(fnf_judge(46) == J_GOOD && fnf_judge(90) == J_GOOD && fnf_judge(-90) == J_GOOD, "Good: up to 90 ms");
  CHECK(fnf_judge(91) == J_BAD && fnf_judge(135) == J_BAD && fnf_judge(-135) == J_BAD, "Bad: up to 135 ms");
  CHECK(fnf_judge(136) == J_SHIT && fnf_judge(160) == J_SHIT && fnf_judge(-160) == J_SHIT, "Shit: up to 160 ms");
  CHECK(fnf_judge(161) == J_MISS && fnf_judge(-161) == J_MISS, "a miss past 160 ms");
  /* Scoring.scoreNotePBOT1, in double precision */
  for (int d = 0; d <= 160; d++) {
    int want = d < 5 ? 500 : (int)(500.0 * (1.0 - 1.0 / (1.0 + exp(-0.080 * (d - 54.99)))) + 9.0);
    CHECK(fnf_points(d) == want && fnf_points(-d) == want, "PBOT1 at %d ms: %d, not %d", d, fnf_points(d), want);
  }
  CHECK(fnf_points(161) == -100, "past the window: -100");
  CHECK(fnf_points(0) == 500 && fnf_points(4) == 500 && fnf_points(160) == 9, "500 to 9 points");
}

/* Tutorial, Normal: its first player notes, played by hand */
static void scoring(void) {
  int c = find_chart("Tutorial", FNF_VAR_DEFAULT, FNF_DIFF_NORMAL);
  CHECK(c >= 0, "Tutorial, Normal");
  if (c < 0) return;
  int n = read_chart((unsigned)c);
  CHECK(fnf_start(&T, (unsigned)c, buf, 0, false), "starts");
  CHECK(T.health == 1000000 && T.score == 0 && T.combo == 0, "health at half, no score");
  note_t a = player_note(n, 0), b = player_note(n, 1), d = player_note(n, 2), e = player_note(n, 3);
  CHECK(a.t > 0 && b.t > a.t, "notes");
  /* on time: Sick, 500, +1.5% of the bar */
  fnf_press(&T, a.lane, a.t);
  CHECK(T.tally[J_SICK] == 1 && fnf_score(&T) == 500 && T.health == 1030000 && T.combo == 1, "Sick: 500, +0.03");
  fnf_release(&T, a.lane, a.t + 20000);
  /* 50 ms late: Good, +0.015 */
  fnf_press(&T, b.lane, b.t + 50000);
  CHECK(T.tally[J_GOOD] == 1 && fnf_score(&T) == 500 + fnf_points(50) && T.health == 1045000 && T.combo == 2,
        "Good at 50 ms: %d points, health %d", fnf_score(&T) - 500, (int)T.health);
  fnf_release(&T, b.lane, b.t + 70000);
  /* 100 ms early: Bad, no health, the combo broken */
  int before = fnf_score(&T);
  fnf_press(&T, d.lane, d.t - 100000);
  CHECK(T.tally[J_BAD] == 1 && fnf_score(&T) == before + fnf_points(100) && T.health == 1045000 && T.combo == 0,
        "Bad 100 ms early: combo broken, health kept");
  fnf_release(&T, d.lane, d.t - 80000);
  /* 150 ms late: Shit, -0.02 */
  before = fnf_score(&T);
  fnf_press(&T, e.lane, e.t + 150000);
  CHECK(T.tally[J_SHIT] == 1 && fnf_score(&T) == before + fnf_points(150) && T.health == 1025000, "Shit: -0.02");
  fnf_release(&T, e.lane, e.t + 160000);
  /* a ghost miss: a key with no note near: -0.08 and -10, no miss counted, the combo kept */
  fnf_press(&T, 0, e.t + 170000);   /* (no note a lane's window away) */
  fnf_release(&T, 0, e.t + 180000);
  T.combo = 7;
  int32_t h = T.health;
  before = fnf_score(&T);
  int32_t far = e.t + 400000;
  bool clear = true;
  for (int i = 0; i < n; i++)
    if (notes[i].lane == 1 && notes[i].t > far - 400000 && notes[i].t < far + 400000) clear = false;
  if (clear) {
    fnf_press(&T, 1, far);
    CHECK(T.health == h - 80000 && fnf_score(&T) == before - 10 && T.combo == 7 && T.tally[J_MISS] == 0,
          "ghost miss: -0.08, -10, combo kept");
    fnf_release(&T, 1, far + 10000);
  }
  /* a note let by: a miss once 160 ms have gone, -0.08, -100, the combo broken */
  note_t m = player_note(n, 6);
  int chord = 0;
  for (int i = 0; i < n; i++) chord += notes[i].lane < 4 && notes[i].t == m.t;
  fnf_advance(&T, m.t + 160000);
  int missed = T.tally[J_MISS];
  h = T.health, before = fnf_score(&T);
  fnf_advance(&T, m.t + 160001);
  CHECK(T.tally[J_MISS] == missed + chord && T.combo == 0 && T.health == h - 80000 * chord &&
        fnf_score(&T) == before - 100 * chord, "missed past 160 ms, not at 160 ms: -0.08, -100");
}

/* the edges of the window, and the note offset */
static void window_edges(void) {
  int c = find_chart("Bopeebo", FNF_VAR_DEFAULT, FNF_DIFF_EASY);
  if (c < 0) return;
  int n = read_chart((unsigned)c);
  note_t a = player_note(n, 0), b = player_note(n, 1);
  fnf_start(&T, (unsigned)c, buf, 0, false);
  fnf_press(&T, a.lane, a.t + 160000);
  CHECK(T.tally[J_SHIT] == 1, "hit at 160 ms: Shit");
  fnf_release(&T, a.lane, a.t + 170000);
  fnf_press(&T, b.lane, b.t - 160999);
  CHECK(T.tally[J_SHIT] == 1, "160.999 ms early: out of the window");
  fnf_release(&T, b.lane, b.t - 160500);
  fnf_press(&T, b.lane, b.t - 135900);
  CHECK(T.tally[J_BAD] == 1, "135.9 ms early: Bad (whole ms, toward 0)");
  fnf_start(&T, (unsigned)c, buf, 0, false);
  fnf_press(&T, a.lane, a.t + 160001);
  CHECK(T.tally[J_SHIT] == 0 && T.tally[J_MISS] == 1, "160.001 ms late: missed, then a ghost miss");
  /* a 30 ms note offset: a press 30 ms late counts as on time */
  fnf_start(&T, (unsigned)c, buf, 30, false);
  fnf_press(&T, a.lane, a.t + 30000);
  CHECK(T.tally[J_SICK] == 1 && fnf_score(&T) == 500, "offset 30 ms: Sick");
  fnf_release(&T, a.lane, a.t + 60000);
  fnf_advance(&T, b.t + 190000);
  CHECK(T.tally[J_MISS] == 0, "offset 30 ms: not missed before 190 ms");
  fnf_advance(&T, b.t + 190001);
  CHECK(T.tally[J_MISS] == 1, "offset 30 ms: missed past 190 ms");
}

/* holds: +250 a second and +6% of the bar a second held; let go of with more than 160 ms left: -125 a second left,
   the combo broken; a missed hold: its whole length lost too */
/* the bot's presses before time t: the song up to there played on time */
static void bot_until(unsigned c, int32_t t) {
  int n = read_chart(c);
  int k = bot_events(n, 0);
  fnf_start(&T, c, buf, 0, false);
  for (int i = 0; i < k && evs[i].t < t; i++) {
    if (evs[i].down) fnf_press(&T, evs[i].lane, evs[i].t);
    else fnf_release(&T, evs[i].lane, evs[i].t);
  }
  fnf_advance(&T, t);
  read_chart(c);
}

static void holds(void) {
  for (int c = 0; c < FNF_CHARTS; c++) {
    int n = read_chart((unsigned)c);
    int k = -1;
    for (int i = 0; i < n && k < 0; i++)
      if (notes[i].lane < 4 && notes[i].len > 1000000 && notes[i].kind == 0 && notes[i].t > 3000000) k = i;
    if (k < 0) continue;
    note_t h = notes[k];
    /* nothing else in that lane near it */
    bool alone = true;
    for (int i = 0; i < n; i++)
      if (i != k && notes[i].lane == h.lane && notes[i].t > h.t - 400000 && notes[i].t < h.t + h.len + 400000) alone = false;
    if (!alone) continue;
    bot_until((unsigned)c, h.t - 300000);
    T.health = 400000, T.combo = 12;
    int64_t s0 = T.score;
    fnf_press(&T, h.lane, h.t);
    fnf_advance(&T, h.t + h.len);
    int64_t got = T.score - s0 - 500000;
    CHECK(got == h.len / 4, "a hold held: %lld thousandths for %d us, not %d", (long long)got, (int)h.len, (int)(h.len / 4));
    CHECK(T.health == 400000 + 30000 + (int32_t)((int64_t)h.len * 12 / 100) || T.health == FNF_HEALTH_MAX,
          "a hold held: +0.12 a second (%d)", (int)T.health);
    CHECK(T.combo == 13, "a hold held: the combo kept");
    fnf_release(&T, h.lane, h.t + h.len + 10000);
    /* let go of 300 ms before its end */
    bot_until((unsigned)c, h.t - 300000);
    T.combo = 12;
    s0 = T.score;
    fnf_press(&T, h.lane, h.t);
    fnf_release(&T, h.lane, h.t + h.len - 300000);
    got = T.score - s0 - 500000;
    int64_t want = (h.len - 300000) / 4 - 300000 / 8;
    CHECK(got == want, "a hold let go of 0.3 s early: %lld, not %lld", (long long)got, (long long)want);
    CHECK(T.combo == 0, "let go of early: the combo broken");
    /* let go of 100 ms before its end: no penalty */
    bot_until((unsigned)c, h.t - 300000);
    T.combo = 12;
    s0 = T.score;
    fnf_press(&T, h.lane, h.t);
    fnf_release(&T, h.lane, h.t + h.len - 100000);
    got = T.score - s0 - 500000;
    CHECK(got == (h.len - 100000) / 4 && T.combo == 13, "let go of 0.1 s early: no penalty");
    /* missed altogether */
    bot_until((unsigned)c, h.t - 300000);
    s0 = T.score;
    int32_t hp = T.health;
    fnf_advance(&T, h.t + 170000);
    got = T.score - s0;
    CHECK(got == -100000 - h.len / 8 && T.health == hp - 80000, "a missed hold: -100 and -125 a second of it");
    return;
  }
  CHECK(0, "no chart with a long hold");
}

static void health_limits(void) {
  int c = find_chart("Fresh", FNF_VAR_DEFAULT, FNF_DIFF_HARD);
  if (c < 0) return;
  fnf_start(&T, (unsigned)c, buf, 0, false);
  int32_t t = fnf_start_time(&T) + 1000;
  for (int i = 0; i < 12 && T.over == FNF_PLAYING; i++, t += 20000) {   /* ghost misses during the countdown */
    fnf_press(&T, 0, t);
    fnf_release(&T, 0, t + 5000);
  }
  CHECK(T.over == FNF_PLAYING && T.health == 1000000 - 12 * 80000, "12 ghost misses: %d", (int)T.health);
  for (int i = 0; i < 2 && T.over == FNF_PLAYING; i++, t += 20000) fnf_press(&T, 0, t), fnf_release(&T, 0, t + 5000);
  CHECK(T.over == FNF_DIED && T.health == 0, "the bar emptied: game over");
  /* never past 2.0 */
  int n = read_chart((unsigned)c);
  fnf_start(&T, (unsigned)c, buf, 0, false);
  T.health = 1990000;
  note_t a = player_note(n, 0);
  fnf_press(&T, a.lane, a.t);
  CHECK(T.health == FNF_HEALTH_MAX, "capped at 2.0");
}

static void ranks(void) {
  fnf_game_t g;
  memset(&g, 0, sizeof g);
  g.total = 100;
  g.tally[J_SICK] = 100;
  CHECK(fnf_rank(&g) == 5 && fnf_completion(&g) == 10000, "all Sick: PERFECT GOLD");
  g.tally[J_SICK] = 99, g.tally[J_GOOD] = 1;
  CHECK(fnf_rank(&g) == 4, "Sick and Good only: PERFECT");
  g.tally[J_SICK] = 80, g.tally[J_GOOD] = 10, g.tally[J_BAD] = 10;
  CHECK(fnf_rank(&g) == 3 && fnf_completion(&g) == 9000, "90%%: EXCELLENT");
  g.tally[J_SICK] = 80, g.tally[J_GOOD] = 5, g.tally[J_MISS] = 5;
  CHECK(fnf_rank(&g) == 2 && fnf_completion(&g) == 8000, "(85 - 5) %%: GREAT");
  g.tally[J_SICK] = 60, g.tally[J_GOOD] = 0, g.tally[J_MISS] = 0;
  CHECK(fnf_rank(&g) == 1, "60%%: GOOD");
  g.tally[J_SICK] = 59;
  CHECK(fnf_rank(&g) == 0, "59%%: LOSS");
  g.tally[J_SICK] = 0, g.tally[J_MISS] = 50;
  CHECK(fnf_completion(&g) == 0, "never below 0");
}

/* ---------------------------------------------------------------- every chart */
static uint32_t hash = 2166136261u;
static void mix(uint32_t v) {
  for (int i = 0; i < 4; i++) hash = (hash ^ (v >> (8 * i) & 255)) * 16777619u;
}

static void play_bot(unsigned c, int32_t late, fnf_game_t *g) {
  int n = read_chart(c);
  int k = bot_events(n, late);
  fnf_start(g, c, buf, 0, false);
  for (int i = 0; i < k && g->over == FNF_PLAYING; i++) {
    if (evs[i].down) fnf_press(g, evs[i].lane, evs[i].t);
    else fnf_release(g, evs[i].lane, evs[i].t);
  }
  while (g->over == FNF_PLAYING) fnf_advance(g, g->now + 50000);
}

static void every_chart(void) {
  int fc = 0, peak = 0;
  for (unsigned c = 0; c < FNF_CHARTS; c++) {
    const fnf_chart_t *ch = &fnf_chart[c];
    int n = read_chart(c);
    CHECK(n == ch->notes, "chart %u: %d notes read, %d listed", c, n, ch->notes);
    int total = 0;
    for (int i = 0; i < n; i++) {
      CHECK(!i || notes[i].t >= notes[i - 1].t, "chart %u: notes in order", c);
      CHECK(notes[i].kind < FNF_NOTE_KINDS && notes[i].len >= 0, "chart %u: a note's kind and length", c);
      if (notes[i].lane < 4 && notes[i].kind != FNF_KIND_NON_SCOREABLE) total++;
    }
    CHECK(total == ch->total, "chart %u: %d notes count, %d listed", c, total, ch->total);
    CHECK(n > 0 && notes[n - 1].t + notes[n - 1].len < (int32_t)fnf_var[ch->var].length * 1000,
          "chart %u: its notes within the song", c);
    fnf_game_t g;
    play_bot(c, 0, &g);
    bool ok = g.over == FNF_WON && g.tally[J_SICK] == total && g.max_combo == total && !g.tally[J_MISS] &&
              fnf_rank(&g) == 5;
    CHECK(ok, "chart %u (%s, %s): bot: over %d, sick %d/%d, max combo %d, misses %d", c,
          fnf_song[fnf_var[ch->var].song].name, DIFF_NAME[ch->diff], g.over, g.tally[J_SICK], total, g.max_combo,
          g.tally[J_MISS]);
    /* the points: 500 a note, and 250 a second of the holds, all held */
    int64_t want = (int64_t)total * 500000, held = 0;
    for (int i = 0; i < n; i++)
      if (notes[i].lane < 4 && notes[i].kind != FNF_KIND_NON_SCOREABLE) held += notes[i].len;
    want += held / 4;
    CHECK(g.score == want, "chart %u: %lld points, not %lld", c, (long long)g.score, (long long)want);
    if (g.peak > peak) peak = g.peak;
    fc += ok;
    mix((uint32_t)g.score), mix(g.max_combo), mix((uint32_t)g.health);
  }
  printf("every chart: %d of %d to a full combo, all Sick; at most %d notes on their way at once\n", fc, FNF_CHARTS, peak);
  CHECK(peak < FNF_ACTIVE, "room for the notes on their way");
}

/* bots hitting late, and missing: the song goes on, the tallies add up */
static void late_and_missing(void) {
  int c = find_chart("Dadbattle", FNF_VAR_DEFAULT, FNF_DIFF_HARD);
  if (c < 0) return;
  fnf_game_t g;
  play_bot((unsigned)c, 70000, &g);
  CHECK(g.tally[J_GOOD] == g.total && g.over == FNF_WON && g.max_combo == g.total, "70 ms late: all Good");
  play_bot((unsigned)c, 120000, &g);
  CHECK(g.tally[J_BAD] == g.total && g.combo == 0 && g.max_combo == 0, "120 ms late: all Bad, no combo");
  mix((uint32_t)g.score), mix((uint32_t)g.health), mix(g.over);
  /* nobody playing: the bar empties */
  fnf_start(&g, (unsigned)c, buf, 0, false);
  while (g.over == FNF_PLAYING) fnf_advance(&g, g.now + 100000);
  CHECK(g.over == FNF_DIED, "nobody playing: game over");
}

/* ---------------------------------------------------------------- the app, with random keys */
static uint32_t rng = 12345;
static uint32_t rnd(void) {
  rng ^= rng << 13, rng ^= rng >> 17, rng ^= rng << 5;
  return rng;
}
static uint32_t held_keys;
static uint32_t random_keys(uint32_t ms) {
  (void)ms;
  if (rnd() % 23 == 0) {
    static const uint32_t K[] = {FK_LEFT, FK_DOWN, FK_UP, FK_RIGHT, FK_7, FK_8, FK_LPAR, FK_RPAR, FK_OK, FK_OK, FK_BACK, FK_SHIFT};
    held_keys ^= K[rnd() % (sizeof K / sizeof K[0])];
  }
  return held_keys & ~FK_HOME;
}

static void app_session(uint32_t length, uint32_t (*keys)(uint32_t)) {
  clock_ms = 1000;
  quit_at = clock_ms + length;
  script = keys;
  held_keys = 0;
  fnf_app_main();
  script = 0;
}

static void fuzz(void) {
  for (int s = 0; s < 300; s++) {
    rng = 0x1234567u + (uint32_t)s * 7919u;
    memset(files, 0, sizeof files);
    app_session(4000 + rnd() % 26000, random_keys);
    uint32_t n;
    uint8_t *d = file_data(FNF_SAVE_NAME, &n);
    CHECK(d && n == sizeof(fnf_file_t) && fnf_file_ok((const fnf_file_t *)d), "session %d: the save is good", s);
    mix(n), mix((uint32_t)G.score);
  }
  /* the engine on its own: random presses through random charts */
  for (int s = 0; s < 200; s++) {
    unsigned c = rnd() % FNF_CHARTS;
    fnf_game_t g;
    fnf_start(&g, c, buf, (int32_t)(rnd() % 61) - 30, s & 1);
    int32_t t = g.now;
    while (g.over == FNF_PLAYING && t < g.length + 1000000) {
      t += (int32_t)(rnd() % 40000);
      int lane = (int)(rnd() % 4);
      if (rnd() & 1) fnf_press(&g, lane, t);
      else fnf_release(&g, lane, t);
      CHECK(g.health >= 0 && g.health <= FNF_HEALTH_MAX && g.combo <= g.total, "chart %u: health and combo in range", c);
    }
    mix((uint32_t)g.score), mix(g.tally[J_SICK]), mix(g.tally[J_MISS]);
  }
}

/* ---------------------------------------------------------------- a whole session, by the keys */
static uint32_t seq_at[64], seq_key[64];
static int seq_n;
static uint32_t scripted(uint32_t ms) {
  for (int i = 0; i < seq_n; i++)
    if (ms >= seq_at[i] && ms < seq_at[i] + 80) return seq_key[i];
  return 0;
}
static void keys_at(int n, const uint32_t *at, const uint32_t *k) {
  seq_n = n;
  for (int i = 0; i < n; i++) seq_at[i] = at[i], seq_key[i] = k[i];
}

static void sessions(void) {
  memset(files, 0, sizeof files);
  /* the title, the menu, Freeplay, a song: the guide the first time (Back skips it), then Tutorial plays; Back
     pauses, Quit game asks, Quit */
  uint32_t at[] = {1500, 2000, 2400, 2800, 3200, 3600, 9000, 9400, 9800, 10200, 10600, 11000, 11400, 11800},
           k[] = {FK_OK, FK_DOWN, FK_OK, FK_OK, FK_BACK, 0, FK_BACK, FK_DOWN, FK_DOWN, FK_DOWN, FK_DOWN, FK_DOWN, FK_OK, FK_DOWN};
  keys_at(14, at, k);
  app_session(14000, scripted);
  CHECK(A.view == V_QUIT || A.view == V_PAUSE, "paused, quitting: %d", A.view);
  uint32_t n;
  uint8_t *d = file_data(FNF_SAVE_NAME, &n);
  CHECK(d && fnf_file_ok((const fnf_file_t *)d) && ((const fnf_file_t *)d)->p.guide == 1, "the guide seen, saved");
}

/* ---------------------------------------------------------------- the game over, through the app */
/* Freeplay, a song of the player chr with no key pressed: it dies; deathLoop; OK: deathConfirm, the fade, the song
   again; it dies again; Back: freeplay. (Each key is pressed for 60 ms.) */
static struct {
  int chr, song, list;
  int deaths;
  uint32_t key, key_at;           /* the key being pressed, since */
  uint32_t over_at, ok_at, restart_at, back_at;
  uint8_t anim_at_ok, fade_steps;
  int box[4];                     /* what's drawn on black when OK is pressed (x0, y0, x1, y1) */
  bool fade_seen;                 /* the song again, coming up from black */
  const char *dir;                /* frames saved here (play gameover) */
  int frame;
} GO;

static uint32_t tap(uint32_t k) {
  if (!GO.key && clock_ms - GO.key_at >= 60) GO.key = k, GO.key_at = clock_ms;
  return 0;
}

static void drawn_box(int *b) {
  b[0] = 320, b[1] = 240, b[2] = b[3] = -1;
  for (int y = 0; y < 226; y++)   /* (above the key line) */
    for (int x = 0; x < 320; x++)
      if (fb[y][x]) {
        if (x < b[0]) b[0] = x;
        if (x > b[2]) b[2] = x;
        if (y < b[1]) b[1] = y;
        if (y > b[3]) b[3] = y;
      }
}

static uint32_t over_script(uint32_t ms) {
  (void)ms;
  if (GO.key) {   /* down for 60 ms, then up */
    if (clock_ms - GO.key_at < 60) return GO.key;
    GO.key = 0, GO.key_at = clock_ms;
    return 0;
  }
  if (clock_ms - GO.key_at < 60) return 0;
  switch (A.view) {
    case V_TITLE: return tap(FK_OK);
    case V_MAIN: return tap(A.sel == MAIN_FREEPLAY ? FK_OK : FK_DOWN);
    case V_HELP: return tap(FK_BACK);
    case V_FREEPLAY:
      if (GO.deaths >= 2) {
        if (!GO.back_at) GO.back_at = clock_ms, quit_at = clock_ms + 200;
        return 0;
      }
      if (P.list != GO.list) return tap(FK_SHIFT);
      return tap(A.list[P.song] == GO.song ? FK_OK : FK_DOWN);
    case V_PLAY:
      if (GO.ok_at && !GO.restart_at) GO.restart_at = clock_ms, GO.fade_seen = A.fade_at != 0;
      return 0;
    case V_GAMEOVER:
      if (!GO.over_at || (GO.restart_at && GO.deaths < 2)) GO.over_at = clock_ms, GO.deaths++;
      if (GO.deaths == 1 && !GO.ok_at && clock_ms - GO.over_at >= 4000) {
        GO.ok_at = clock_ms, GO.anim_at_ok = A.over_anim, GO.fade_steps = A.fade_steps;
        drawn_box(GO.box);
        return tap(FK_OK);
      }
      if (GO.deaths == 2 && clock_ms - GO.over_at >= 1000) return tap(FK_BACK);
      return 0;
    default: return 0;
  }
}

static void over_frame(int y, int h) {
  if (!GO.dir || y + h != 240 || !GO.over_at || GO.deaths > 1) return;
  if (GO.restart_at && clock_ms - GO.restart_at > 1500) return;
  char path[512];
  snprintf(path, sizeof path, "%s/%06d_%06d.raw", GO.dir, GO.frame++, (int)(clock_ms - GO.over_at));
  FILE *f = fopen(path, "wb");
  if (!f) exit(2);
  fwrite(fb, 2, 320 * 240, f);
  fclose(f);
}

/* the song (and list) where chr plays (that song, if named), or false */
static bool song_of(int chr, const char *name) {
  for (int list = 0; list < 2; list++) {
    P.list = (uint8_t)list;
    app_build_list(&A);
    for (int i = 0; i < A.list_n; i++)
      for (int d = 0; d < FNF_DIFFS; d++) {
        int c = app_chart(A.list[i], list, d);
        if (c >= 0 && fnf_var[fnf_chart[c].var].chr[ROLE_PLAYER] == chr &&
            (!name || !strcmp(fnf_song[A.list[i]].name, name))) {
          GO.song = A.list[i], GO.list = list;
          return true;
        }
      }
  }
  return false;
}

static void game_over_run(int chr, const char *dir, const char *song) {
  memset(files, 0, sizeof files);
  memset(&GO, 0, sizeof GO);
  GO.chr = chr, GO.dir = dir;
  A.p = &P, A.g = &G;
  fnf_fresh(&P);
  if (!song_of(chr, song)) {
    CHECK(0, "a song where character %d plays", chr);
    return;
  }
  P.list = 0;
  on_push = over_frame;
  app_session(400000, over_script);
  on_push = 0;
}

static void game_overs(void) {
  static const int CHR[] = {FNF_CHR_BF, FNF_CHR_BF_PIXEL, FNF_CHR_PICO_PLAYABLE, FNF_CHR_BF_CHRISTMAS,
                            FNF_CHR_BF_HOLDING_GF, FNF_CHR_PICO_BLAZIN, FNF_CHR_PICO_PIXEL};
  for (unsigned i = 0; i < sizeof CHR / sizeof CHR[0]; i++) {
    int chr = CHR[i];
    game_over_run(chr, 0, 0);
    const fnf_death_t *d = &fnf_death[fnf_char[chr].death];
    bool anims = d->anim != FNF_NONE;
    CHECK(GO.deaths == 2, "character %d: died twice (%d)", chr, GO.deaths);
    CHECK(GO.ok_at && GO.anim_at_ok == (anims ? 1 : 0), "character %d: deathLoop before OK (%d)", chr, GO.anim_at_ok);
    /* the song again after the fade: fade + 2 s (frames are 24 ms here) */
    int32_t wait = (int32_t)(GO.restart_at - GO.ok_at), want = d->fade + 2000;
    CHECK(wait >= want && wait < want + 100, "character %d: the song again %d ms after OK (%d)", chr, wait, want);
    CHECK(GO.fade_seen && GO.fade_steps == ((d->flags & FNF_DF_PIXEL) != 0), "character %d: it fades in", chr);
    CHECK(GO.back_at && A.view == V_FREEPLAY, "character %d: Back goes to freeplay (%d)", chr, A.view);
    /* the death on screen, the camera on it */
    int cx = (GO.box[0] + GO.box[2]) / 2, cy = (GO.box[1] + GO.box[3]) / 2;
    CHECK(GO.box[2] > GO.box[0] && (!anims || (cx > 100 && cx < 220 && cy > 50 && cy < 190)),
          "character %d: its death in the middle (%d, %d)", chr, cx, cy);
    mix((uint32_t)wait), mix((uint32_t)cx), mix((uint32_t)cy);
  }
  /* every death animation's frames unpack */
  for (int i = 0; i < FNF_DEATH_ANIMS; i++) {
    const fnf_death_anim_t *an = &fnf_death_anim[i];
    uint32_t n = (an->w + 1u) / 2 * an->h, at = an->at;
    static uint8_t px[FNF_BG_BYTES], tmp[FNF_BG_BYTES];
    CHECK(n <= sizeof px && fnf_inflate(fnf_blob + at, fnf_death_size[an->frame], px, n) == (int32_t)n,
          "death animation %d: frame 0", i);
    at += fnf_death_size[an->frame];
    for (int k = 1; k < an->n; k++) {
      uint16_t s = fnf_death_size[an->frame + k];
      CHECK(!s || fnf_inflate(fnf_blob + at, s, tmp, n) == (int32_t)n, "death animation %d: frame %d", i, k);
      at += s;
    }
  }
}

/* ---------------------------------------------------------------- saves */
static void saves(void) {
  memset(files, 0, sizeof files);
  fnf_progress_t p, q;
  fnf_fresh(&p);
  p.downscroll = 1, p.offset = -25, p.guide = 1, p.week = 3, p.song = 5, p.list = 1;
  p.week_score[2][1] = 123456;
  fnf_record(&p, 7, 54321, 3, 9123);
  fnf_record(&p, 7, 50000, 4, 10000);   /* a better rank, a lower score: both best kept */
  CHECK(p.chart[7].score == 54321 && p.chart[7].rank == 5 && p.chart[7].completion == 10000, "best score, best rank");
  fnf_record(&p, 7, 60000, 1, 6000);
  CHECK(p.chart[7].score == 60000 && p.chart[7].rank == 5, "a better score kept, the rank too");
  CHECK(save_write(&p), "written");
  CHECK(save_load(&q) && !memcmp(&p, &q, sizeof p), "read back the same");
  /* damaged: each byte changed in turn: never read, the copy instead */
  uint32_t n;
  uint8_t *d = file_data(FNF_SAVE_NAME, &n);
  CHECK(n == sizeof(fnf_file_t), "%u bytes", n);
  for (uint32_t i = 0; i < n; i += 7) {
    d[i] ^= 0x5A;
    fnf_file_t f;
    memcpy(&f, d, sizeof f);
    CHECK(!fnf_file_ok(&f), "byte %u changed: refused", i);
    CHECK(save_load(&q) && !memcmp(&p, &q, sizeof p), "byte %u changed: the copy read", i);
    d = file_data(FNF_SAVE_NAME, &n);   /* (put back from the copy) */
  }
  /* only the copy left (installed again) */
  forget(FNF_SAVE_NAME);
  CHECK(save_load(&q) && !memcmp(&p, &q, sizeof p), "from the copy alone");
  CHECK(file_data(FNF_SAVE_NAME, &n), "and put back");
  /* both damaged: a fresh start */
  uint8_t *c = file_data(FNF_COPY_NAME, &n);
  c[n / 2] = c[n / 2] == 'A' ? 'B' : 'A';
  d = file_data(FNF_SAVE_NAME, &n);
  d[20] ^= 1;
  CHECK(!save_load(&q) && q.guide == 0 && q.chart[7].score == 0, "both damaged: a fresh start");
  /* cut short, too long, nothing */
  memset(files, 0, sizeof files);
  save_write(&p);
  forget(FNF_COPY_NAME);
  d = file_data(FNF_SAVE_NAME, &n);
  files[0].len = n - 1;
  CHECK(!save_load(&q), "cut short: refused");
  memset(files, 0, sizeof files);
  CHECK(!save_load(&q), "nothing: a fresh start");
  /* the storage full: the game goes on */
  storage_full = true;
  CHECK(!save_write(&p), "full: not written");
  storage_full = false;
}

/* ---------------------------------------------------------------- pictures */
/* a player a little off the beat: each press up to 60 ms early or late (the same everywhere: xorshift) */
static void play_human(unsigned c, fnf_game_t *g, uint8_t *b, int32_t until) {
  int n = read_chart(c);
  int k = 0;
  uint32_t r = 2463534242u;
  for (int i = 0; i < n; i++) {   /* (each press and its release moved together) */
    const note_t *a = &notes[i];
    if (a->lane >= 4) continue;
    r ^= r << 13, r ^= r >> 17, r ^= r << 5;
    int32_t off = (int32_t)(r % 121000) - 60000, next = INT32_MAX;
    for (int j = i + 1; j < n; j++)
      if (notes[j].lane == a->lane) {
        next = notes[j].t;
        break;
      }
    int32_t down = a->t + off, up = a->len > 0 ? a->t + a->len + 1000 : down + 30000;
    if (up > next - 61000) up = next - 61000;
    if (up <= down) up = down + 1;
    evs[k] = (bot_ev){down, a->lane, 1, (uint16_t)k};
    k++;
    evs[k] = (bot_ev){up, a->lane, 0, (uint16_t)k};
    k++;
  }
  qsort(evs, (size_t)k, sizeof evs[0], cmp_ev);
  fnf_start(g, c, b, 0, false);
  for (int i = 0; i < k && g->over == FNF_PLAYING && evs[i].t <= until; i++) {
    if (evs[i].down) fnf_press(g, evs[i].lane, evs[i].t);
    else fnf_release(g, evs[i].lane, evs[i].t);
  }
  while (g->over == FNF_PLAYING && g->now < until) fnf_advance(g, g->now + 50000 < until ? g->now + 50000 : until);
}

static void shots(const char *dir) {
  memset(files, 0, sizeof files);
  A.p = &P, A.g = &G;
  fnf_fresh(&P);
  P.guide = 1;
  bold_load();
  /* a real result first: Bopeebo, Hard, played a little off the beat */
  int c = find_chart("Bopeebo", FNF_VAR_DEFAULT, FNF_DIFF_HARD);
  play_human((unsigned)c, &G, chart_buf, INT32_MAX);
  fnf_record(&P, G.chart, fnf_score(&G), fnf_rank(&G), fnf_completion(&G));
  A.result_rank = (uint8_t)fnf_rank(&G), A.result_score = fnf_score(&G), A.result_total = G.total;
  A.result_max_combo = G.max_combo, A.result_completion = fnf_completion(&G), A.result_new = 1;
  memcpy(A.result_tally, G.tally, sizeof A.result_tally);
  P.week_score[1][FNF_DIFF_HARD] = fnf_score(&G) * 3;
  A.view = V_RESULTS;
  screens_draw(&A);
  shot(dir, "results");
  A.clock = 1000;
  app_build_list(&A);
  A.view = V_TITLE, A.clock = 1000 + 60000 / 102 * 4 + 100;
  screens_draw(&A);
  shot(dir, "title");
  A.view = V_MAIN, A.sel = MAIN_FREEPLAY;
  screens_draw(&A);
  shot(dir, "menu");
  A.view = V_STORY, P.week = 1, P.week_diff = FNF_DIFF_HARD;
  screens_draw(&A);
  shot(dir, "story");
  A.view = V_FREEPLAY, P.list = 0;
  app_build_list(&A);
  P.song = 1, P.song_diff = FNF_DIFF_HARD;
  screens_draw(&A);
  shot(dir, "freeplay");
  /* songs, at moments */
  static const struct { const char *song; int kind, diff; int32_t at; const char *name; } S[] = {
      {"Bopeebo", FNF_VAR_DEFAULT, FNF_DIFF_HARD, 21300000, "shot"},
      {"Senpai", FNF_VAR_DEFAULT, FNF_DIFF_HARD, 30000000, "school"},
      {"Cocoa", FNF_VAR_DEFAULT, FNF_DIFF_HARD, 25000000, "mall"},
      {"Ugh", FNF_VAR_DEFAULT, FNF_DIFF_HARD, 33000000, "tankman"},
      {"Philly Nice", FNF_VAR_PICO, FNF_DIFF_HARD, 40000000, "pico"},
      {"Spookeez", FNF_VAR_DEFAULT, FNF_DIFF_NORMAL, -1000000, "countdown"},
      {"Monster", FNF_VAR_DEFAULT, FNF_DIFF_HARD, 40000000, "monster"},
      {"Blazin'", FNF_VAR_DEFAULT, FNF_DIFF_NORMAL, 30000000, "blazin"},
  };
  for (unsigned s = 0; s < sizeof S / sizeof S[0]; s++) {
    c = find_chart(S[s].song, S[s].kind, S[s].diff);
    if (c < 0) {
      printf("no %s\n", S[s].song);
      continue;
    }
    play_human((unsigned)c, &G, chart_buf, S[s].at);
    if (!play_load(&G)) printf("%s: not enough RAM for its pictures\n", S[s].song);
    A.view = V_PLAY, A.play_t0 = 0, A.clock = 100000;
    screens_draw(&A);
    shot(dir, S[s].name);
    if (s == 0) {
      A.view = V_PAUSE, A.sel = PAUSE_OPTIONS;
      screens_draw(&A);
      shot(dir, "pause");
      P.downscroll = 1;
      A.view = V_PLAY;
      screens_draw(&A);
      shot(dir, "downscroll");
      P.downscroll = 0;
    }
  }
  A.view = V_HELP, A.page = 0, A.first_run = 1, A.under = V_PLAY;
  screens_draw(&A);
  shot(dir, "help");
}

/* ---------------------------------------------------------------- a film */
static const char *film_dir;
static int film_k;
static int32_t film_t0 = INT32_MIN;
static void film(const char *dir, unsigned c, int32_t ms) {
  film_dir = dir;
  memset(files, 0, sizeof files);
  A.p = &P, A.g = &G;
  fnf_fresh(&P);
  bold_load();
  int n = read_chart(c);
  int k = bot_events(n, 0);
  fnf_start(&G, c, chart_buf, 0, false);
  play_load(&G);
  int32_t t = G.now, end = G.now + ms * 1000;
  int i = 0;
  A.view = V_PLAY, A.play_t0 = 0;
  for (; t <= end; t += 33333) {   /* 30 a second */
    while (i < k && evs[i].t <= t) {
      if (evs[i].down) fnf_press(&G, evs[i].lane, evs[i].t);
      else fnf_release(&G, evs[i].lane, evs[i].t);
      i++;
    }
    fnf_advance(&G, t);
    A.clock = (uint32_t)((t - fnf_start_time(&G)) / 1000) + 1;
    screens_draw(&A);
    if (film_t0 == INT32_MIN) film_t0 = t;
    char path[512];
    snprintf(path, sizeof path, "%s/%06d_%06d.raw", film_dir, film_k++, (int)((t - film_t0) / 1000));
    FILE *f = fopen(path, "wb");
    if (!f) exit(2);
    fwrite(fb, 2, 320 * 240, f);
    fclose(f);
  }
}

/* every variation, 25 s in, a bot playing (to look at) */
static void scenes(const char *dir) {
  A.p = &P, A.g = &G;
  fnf_fresh(&P);
  bold_load();
  for (int v = 0; v < FNF_VARS; v++) {
    unsigned c = fnf_var[v].chart + fnf_var[v].ncharts - 1;
    int n = read_chart(c);
    int k = bot_events(n, 0);
    fnf_start(&G, c, chart_buf, 0, false);
    play_load(&G);
    int32_t at = 25000000;
    for (int i = 0; i < k && evs[i].t <= at; i++) {
      if (evs[i].down) fnf_press(&G, evs[i].lane, evs[i].t);
      else fnf_release(&G, evs[i].lane, evs[i].t);
    }
    fnf_advance(&G, at);
    A.view = V_PLAY, A.play_t0 = 0, A.clock = 100000;
    screens_draw(&A);
    char name[64];
    snprintf(name, sizeof name, "v%02d", v);
    shot(dir, name);
  }
}

int main(int argc, char **argv) {
  chart_buf = app_chart_buf;
  if (argc >= 3 && !strcmp(argv[1], "notes")) {   /* a chart's notes (us): time lane length */
    unsigned c = (unsigned)atoi(argv[2]);
    int n = read_chart(c);
    fnf_start(&T, c, buf, 0, false);
    printf("start %d total %d\n", (int)T.now, fnf_chart[c].total);
    for (int i = 0; i < n; i++)
      if (notes[i].lane < 4) printf("%d %d %d %d\n", (int)notes[i].t, notes[i].lane, (int)notes[i].len, notes[i].kind);
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "scenes")) {
    draw_strip_buffer(test_strip);
    scenes(argv[2]);
    return 0;
  }
  draw_strip_buffer(test_strip);
  if (argc >= 3 && !strcmp(argv[1], "shots")) {
    shots(argv[2]);
    return 0;
  }
  if (argc >= 5 && !strcmp(argv[1], "film")) {
    film(argv[2], (unsigned)atoi(argv[3]), atoi(argv[4]));
    return 0;
  }
  if (argc >= 4 && !strcmp(argv[1], "gameover")) {   /* a death to the song again: DIR/<frame>_<ms>.raw */
    game_over_run(atoi(argv[3]), argv[2], argc >= 5 ? argv[4] : 0);
    return fails != 0;
  }
  if (argc < 3 || strcmp(argv[1], "test")) {
    printf("play test DIR | shots DIR | film DIR CHART MS | gameover DIR CHARACTER [SONG]\n");
    return 1;
  }
  judgements_and_points();
  scoring();
  window_edges();
  holds();
  health_limits();
  ranks();
  every_chart();
  late_and_missing();
  saves();
  sessions();
  game_overs();
  fuzz();
  printf("hash %08x (the same on every computer)\n", hash);
  printf(fails ? "%d FAILED\n" : "all good\n", fails);
  return fails != 0;
}
