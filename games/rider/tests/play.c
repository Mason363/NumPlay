/* Rider on a computer, for tests and pictures (make host, make test).
 *   play test            the bike against the game's numbers, a level ridden to its end with the keys, a crash,
 *                        the endless run, saves (kept, damaged, from the copy) and random keys
 *   play docs DIR        the README's pictures (DIR/<name>.ppm)
 *   play film DIR        the frames of the README's GIF (DIR/<n>.ppm, 30 a second) */

#define main rider_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- the calculator, on a computer */
static uint16_t screen_px[240][320];
static void (*on_frame)(void);
void np_push(int x, int y, int w, int h, const color_t *px) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) screen_px[y + j][x + i] = px[j * w + i];
  if (on_frame && y + h == 240) on_frame();
}
void *np_alloc(uint32_t n) { (void)n; return 0; }
static uint64_t clock_ms;
static uint32_t (*script)(void);
static uint32_t frame_no, quit_at = 0xFFFFFFFFu;
uint32_t plat_keys(void) {
  frame_no++;
  if (frame_no >= quit_at) return PK_HOME;
  return script ? script() : 0;
}
uint32_t plat_millis(void) { return (uint32_t)clock_ms; }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
static uint32_t rnd = 1;
static uint32_t lcg(void) { return rnd = rnd * 1664525u + 1013904223u; }
uint32_t plat_random(void) { return lcg(); }
void plat_begin(void) {}
int plat_end(void) { return 0; }
static struct {
  char name[32];
  uint8_t data[4096];
  uint32_t len;
  bool on;
} files[4];
bool plat_save(const char *name, const void *data, uint32_t len) {
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
static uint8_t *file_of(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}
static void forget(const char *name) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) files[i].on = false;
}
/* the copy as the game wrote it before, under a longer head */
static void old_copy(const char *head) {
  static uint8_t buf[sizeof files[0].data];
  uint32_t n, at = 1, hl = (uint32_t)strlen(head);
  const uint8_t *c = file_of(COPY_NAME, &n);
  while (at + 1 < n && !(c[at] == '#' && c[at + 1] == '>')) at++;
  buf[0] = 0, memcpy(buf + 1, head, hl), memcpy(buf + 1 + hl, c + at, n - at);
  plat_save(COPY_NAME, buf, 1 + hl + n - at);
}

static void ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      uint16_t c = screen_px[y][x];
      uint8_t p[3] = {(uint8_t)((c >> 11) * 255 / 31), (uint8_t)((c >> 5 & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
      fwrite(p, 1, 3, f);
    }
  fclose(f);
}

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- the bike against the spec */
static int flat_level(void) { /* the first Easy level starts on flat ground */
  return 0;
}
static void test_physics(void) {
  rd_run *r = &run;
  run_start(r, RUN_LEVEL, flat_level(), 0, 0, 5);
  CHECK(r->b.ground, "the bike stands on the track at the start");
  float y0 = r->b.y;
  for (int i = 0; i < 30; i++) run_step(r, false);
  CHECK(fabsf(r->b.y - y0) < 2 && fabsf(r->b.vx) < 5, "still: it stays put (moved %.2f, vx %.2f)", r->b.y - y0, r->b.vx);
  /* held on the ground: the button's speed grows 14 a frame up to 550 */
  run_step(r, true);
  float s1 = r->b.speed;
  for (int i = 0; i < 9; i++) run_step(r, true);
  CHECK(fabsf(r->b.speed - s1 - 9 * 14) < 1, "14 px/s a frame (%.1f -> %.1f)", s1, r->b.speed);
  for (int i = 0; i < 60 && r->b.ground; i++) run_step(r, true);
  CHECK(r->b.speed <= 550.01f, "at most 550 px/s (%.1f)", r->b.speed);
  /* in the air: the turn grows by the torque and stops at 500 degrees a second */
  rd_bikestate *b = &r->b;
  b->y -= 2000, b->vx = 300, b->vy = 0, b->w = 0, b->ground = false, b->air_ms = 100;
  float vy0 = b->vy;
  run_step(r, false);
  CHECK(fabsf(b->vy - vy0 - (294.0f / 60 + 4.41f)) < 0.5f, "gravity and the game's push down in the air (%.2f)", b->vy - vy0);
  b->held = false;
  for (int i = 0; i < 90; i++) run_step(r, true);
  CHECK(fabsf(b->w / 0.017453f + 500) < 10, "spin at -500 deg/s (%.1f)", b->w / 0.017453f);
  /* let go: it eases to a stop in a second */
  run_step(r, false);
  for (int i = 0; i < 59; i++) run_step(r, false);
  CHECK(fabsf(b->w) < 0.05f, "stopped turning a second after letting go (%.3f)", b->w);
  CHECK(r->flips >= 1, "a flip counted (%d)", r->flips);
}

/* a bot: holds on the ground, and in the air holds only while the bike leans forward of its landing */
static bool bot(const rd_run *r, int flips) {
  const rd_bikestate *b = &r->b;
  if (b->ground) return true;
  if (!flips) { /* lands: noses up to the way it flies */
    float target = atan2f(b->vy, b->vx > 1 ? b->vx : 1) * 0.5f;
    return b->a > target + 0.12f;
  }
  float deg = fmodf(b->a / 0.0174533f, 360);
  if (deg > 180) deg -= 360;
  if (deg < -180) deg += 360;
  return r->b.air_ms < 1400 && (b->air_flips < flips && !(deg < -30 && deg > -120 && b->air_flips == flips - 1));
}
/* a level ridden by search: pieces of 15 frames, a few ways to hold the button in each, the furthest
 * still riding kept; returns the frames' button in path (1 held), or 0 if none got to the end */
#define SEG 15
#ifndef BEAM
#define BEAM 12
#endif
#define PAT 7
static bool pat(int p, int f) {
  switch (p) {
  case 0: return true;
  case 1: return false;
  case 2: return f < SEG / 2;
  case 3: return f >= SEG / 2;
  case 4: return f < 4;
  case 5: return (f / 3) & 1;
  default: return f >= SEG - 4;
  }
}
static rd_run beam[BEAM], next_beam[BEAM * PAT], tmp;
static uint8_t paths[BEAM][1600], next_paths[BEAM * PAT][1600];
static float worth(const rd_run *r) { return r->mode == RUN_ENDLESS ? (r->score + r->pending) * 2000.0f + r->perfect * 300 + r->b.x : r->b.x; }
static int solve_run(int mode, int level, int bike, uint32_t seed, uint8_t *out, int max_segs) {
  int nb = 1;
  run_start(&beam[0], mode, level, bike, mode == RUN_LEVEL ? level / 8 : 6, seed);
  for (int sg = 0; sg < max_segs; sg++) {
    int nn = 0;
    for (int i = 0; i < nb; i++)
      for (int p = 0; p < PAT; p++) {
        tmp = beam[i];
        for (int f = 0; f < SEG && !tmp.ended; f++) run_step(&tmp, pat(p, f));
        if (tmp.ended == END_CRASH) continue;
        if (tmp.ended == END_FINISH) {
          memcpy(out, paths[i], (size_t)sg), out[sg] = (uint8_t)p;
          return sg + 1;
        }
        next_beam[nn] = tmp;
        memcpy(next_paths[nn], paths[i], (size_t)sg), next_paths[nn][sg] = (uint8_t)p;
        nn++;
      }
    if (!nn) {
      printf(" [died at x %.0f, %.1f s]", beam[0].b.x, beam[0].ms / 1000);
      if (getenv("DIED")) {
        char p[256];
        run = beam[0], draw_theme(run.theme), screen = S_RIDE, used_ok = true, overlay = 0, draw_ride();
        snprintf(p, sizeof p, "%s/died%02d.ppm", getenv("DIED"), level), ppm(p);
      }
      return 0;
    }
    /* keep the furthest, a few apart */
    nb = 0;
    for (int k = 0; k < BEAM && nn; k++) {
      int best = 0;
      for (int j = 1; j < nn; j++)
        if (worth(&next_beam[j]) > worth(&next_beam[best])) best = j;
      beam[nb] = next_beam[best], memcpy(paths[nb], next_paths[best], (size_t)sg + 1), nb++;
      float bx = next_beam[best].b.x, by = next_beam[best].b.y;
      for (int j = 0; j < nn; j++)
        if (fabsf(next_beam[j].b.x - bx) < 3 && fabsf(next_beam[j].b.y - by) < 3) next_beam[j] = next_beam[--nn], memcpy(next_paths[j], next_paths[nn], (size_t)sg + 1), j--;
    }
  }
  if (mode == RUN_ENDLESS) { memcpy(out, paths[0], (size_t)max_segs); return max_segs; }
  printf(" [out of time at x %.0f]", beam[0].b.x);
  return 0;
}
static int solve_level(int level, uint8_t *out, int max_segs) { return solve_run(RUN_LEVEL, level, 0, 9, out, max_segs); }

static int ride(int mode, int level, int flips, int frames, int bike) {
  rd_run *r = &run;
  run_start(r, mode, level, bike, 0, 9);
  for (int i = 0; i < frames && !r->ended; i++) run_step(r, bot(r, flips));
  return r->ended;
}
static void test_levels(void) {
  /* levels ridden to their end by a search over the button (the game's own tracks and times) */
  static uint8_t path[1600];
  static const int some[] = {0, 4, 7, 17};
  int ok = 0;
  for (unsigned i = 0; i < sizeof some / sizeof *some; i++) {
    int n = solve_level(some[i], path, 300);
    if (n) {
      run_start(&run, RUN_LEVEL, some[i], 0, 0, 9);
      for (int sg = 0; sg < n; sg++)
        for (int f = 0; f < SEG && !run.ended; f++) run_step(&run, pat(path[sg], f));
      CHECK(run.ended == END_FINISH, "level %d: the found ride replays to the end", some[i] + 1);
      ok++;
    }
  }
  printf("levels: %d of %d sample levels ridden to their end by the search\n", ok, (int)(sizeof some / sizeof *some));
  CHECK(ok == 4, "the sample levels can be ridden (%d)", ok);
  /* flipping all the time ends on the bike's head */
  int e = ride(RUN_ENDLESS, 0, 9, 60 * 60, 0);
  CHECK(e == END_CRASH, "holding the button for ever crashes (%d)", e);
  /* the endless run: pieces keep coming, flips are counted */
  int best = 0, flips = 0;
  for (int s = 0; s < 20; s++) {
    rd_run *r = &run;
    run_start(r, RUN_ENDLESS, 0, s % NBIKES, 6 + s % 10, 100 + (uint32_t)s);
    for (int i = 0; i < 60 * 90 && !r->ended; i++) run_step(r, bot(r, 1));
    best = r->score > best ? r->score : best, flips += r->flips;
  }
  printf("endless: best %d, %d flips in 20 runs\n", best, flips);
  CHECK(flips > 0, "flips in the endless run");
}

/* ---------------------------------------------------------------- the app, with keys */
static uint32_t keys_now;
static const uint8_t *ride_path;
static int ride_n, ride_phase;
static uint32_t level_keys(void) {
  clock_ms += 17;
  switch (ride_phase) {
  case 0: if (frame_no > 3) ride_phase = 1; return 0;
  case 1: ride_phase = 2; return PK_DOWN;   /* to Levels */
  case 2: ride_phase = 3; return 0;
  case 3: ride_phase = 4; return PK_OK;     /* the levels */
  case 4: ride_phase = 5; return 0;
  case 5: ride_phase = 6; return PK_OK;     /* level 1 */
  case 6: {
    if (screen != S_RIDE) return 0;
    if (run.ended) { ride_phase = 7; return 0; }
    int step = (int)(run.ms * 60 / 1000 + 0.5f);
    if (step / SEG >= ride_n) return 0;
    return pat(ride_path[step / SEG], step % SEG) ? PK_OK : 0;
  }
  default: return overlay == OV_DONE ? PK_HOME : 0;
  }
}
static uint32_t endless_keys(void) {
  clock_ms += 17;
  switch (ride_phase) {
  case 0: if (frame_no > 3) ride_phase = 1; return 0;
  case 1: ride_phase = 2; return PK_OK; /* Play */
  case 2: if (overlay == OV_OVER) ride_phase = 3; return PK_OK;
  case 3: ride_phase = 4; return 0;
  case 4: ride_phase = 5; return PK_OK; /* Ride again */
  case 5: if (overlay == OV_OVER) ride_phase = 6; return screen == S_RIDE && !overlay ? PK_OK : 0;
  case 6: ride_phase = 7; return 0;
  case 7: ride_phase = 8; return PK_BACK; /* Menu */
  case 8: ride_phase = 9; return 0;
  default: return PK_HOME;
  }
}
static uint32_t rnd_keys(void) {
  static const uint32_t ks[] = {PK_OK, PK_UP, PK_DOWN, PK_LEFT, PK_RIGHT, PK_BACK, PK_EXE, 0, 0, PK_OK, PK_OK};
  clock_ms += 17;
  if (lcg() % 4) return keys_now;
  keys_now = ks[lcg() % (sizeof ks / sizeof *ks)];
  return keys_now;
}
static void test_app(void) {
  /* a fresh start: how to play, then the menu; random keys for a while; everything saved on Home */
  for (int s = 0; s < 150; s++) {
    if (s % 3 == 0) forget(SAVE_NAME), forget(COPY_NAME);
    rnd = 77 + (uint32_t)s * 1013, frame_no = 0, quit_at = 400 + lcg() % 2000, script = rnd_keys;
    rider_main();
    uint32_t n;
    const uint8_t *d = file_of(SAVE_NAME, &n);
    CHECK(d && n == sizeof(save_t), "saved on Home (%d)", s);
    if (d) {
      save_t t;
      memcpy(&t, d, sizeof t);
      CHECK(save_ok(&t), "the save reads back (%d)", s);
    }
  }
  /* damaged: the copy in rider_saves.py brings it back */
  uint32_t n;
  uint8_t *d = file_of(SAVE_NAME, &n);
  save_t before;
  memcpy(&before, d, sizeof before);
  before.gems = 4321;
  sv = before, save();
  d = file_of(SAVE_NAME, &n), d[10] ^= 0x55;
  load();
  CHECK(sv.gems == 4321, "a damaged save comes back from the copy (%u)", (unsigned)sv.gems);
  forget(SAVE_NAME);
  load();
  CHECK(sv.gems == 4321, "a lost save comes back from the copy (%u)", (unsigned)sv.gems);
  old_copy("# Rider keeps a copy of your game here,\n# so that installing it again doesn't erase it.\n");
  sv.gems = 0;
  load();
  CHECK(sv.gems == 4321, "a lost save comes back from an older copy (%u)", (unsigned)sv.gems);
  forget(COPY_NAME), forget(SAVE_NAME);
  load();
  CHECK(sv.gems == 0 && owned(0), "nothing left: a fresh start");
  /* gems win bikes: the first draws are common, rare, common, epic */
  fresh(), sv.gems = 100000;
  uint32_t sd = 5;
  int cls[4];
  for (int i = 0; i < 4; i++) cls[i] = rd_bikes[win_bike(&sd)].cls;
  CHECK(cls[0] == 0 && cls[1] == 1 && cls[2] == 0 && cls[3] == 2, "the first four draws (%d %d %d %d)", cls[0], cls[1], cls[2], cls[3]);
  CHECK(price() == 800, "price 600 + 50 a bike won (%d)", price());
  /* special bikes */
  fresh(), sv.runs = 50, check_specials();
  int spoily = -1;
  for (int b = 0; b < NBIKES; b++)
    if (!strcmp(rd_bikes[b].name, "spoily")) spoily = b;
  CHECK(spoily >= 0 && owned(spoily), "spoily after 50 runs");
  /* challenges: the first one is three runs */
  fresh(), next_challenge();
  CHECK(!strcmp(challenge_text(), "Play 3 runs"), "first challenge: %s", challenge_text());
  for (int i = 0; i < 3; i++) challenge_run(0, 0, 0, 0, 0, &sd);
  CHECK(sv.challenges == 1 && sv.gems == 200, "done: 200 gems (%d, %u)", sv.challenges, (unsigned)sv.gems);
  CHECK(!strcmp(challenge_text(), "Score 25 points in total"), "second: %s", challenge_text());
  /* a level ridden with the keys: Levels, OK on level 1, then the button as the search found it */
  static uint8_t path[1600];
  int n_path = solve_level(0, path, 400);
  CHECK(n_path > 0, "level 1 can be ridden to its end");
  forget(SAVE_NAME), forget(COPY_NAME);
  fresh(), sv.flags = 1, next_challenge(), save();
  ride_path = path, ride_n = n_path, ride_phase = 0;
  script = level_keys, frame_no = 0, quit_at = 60 * 120;
  rider_main();
  CHECK(sv.stars[0] >= 1 && sv.best_ms[0] > 0, "level 1 finished with the keys: %d stars, %u ms", sv.stars[0], (unsigned)sv.best_ms[0]);
  CHECK(levels_done() == 1, "one level done (%d)", levels_done());
  /* the endless run with the button held: a crash, the run counted, then Ride again and Menu */
  ride_phase = 0, script = endless_keys, frame_no = 0, quit_at = 60 * 60;
  uint32_t runs = sv.runs;
  rider_main();
  CHECK(sv.runs >= runs + 2, "runs counted after crashes (%u -> %u)", (unsigned)runs, (unsigned)sv.runs);
  CHECK(screen == S_MENU, "back to the menu (%d)", screen);
}

void print_segs(const rd_run *r);
static int film_n;
static const char *film_dir;
static void film_frame(void) {
  static uint32_t last;
  if (clock_ms - last < 33) return;
  last = (uint32_t)clock_ms;
  char p[512];
  snprintf(p, sizeof p, "%s/%04d.ppm", film_dir, film_n++);
  ppm(p);
}

int main(int argc, char **argv) {
  gfx_buf = strip;
  if (argc > 1 && !strcmp(argv[1], "test")) {
    test_physics();
    test_levels();
    test_app();
    printf(fails ? "%d FAILED\n" : "all good\n", fails);
    return fails != 0;
  }
  if (argc > 2 && !strcmp(argv[1], "shot")) {
    /* one picture of the endless run after N frames of the bot */
    rd_run *r = &run;
    run_start(r, RUN_ENDLESS, 0, argc > 3 ? atoi(argv[3]) : 0, 6, 3);
    int n = argc > 4 ? atoi(argv[4]) : 200;
    for (int i = 0; i < n && !r->ended; i++) run_step(r, bot(r, 1));
    draw_theme(r->theme);
    screen = S_RIDE, used_ok = true;
    draw_ride();
    ppm(argv[2]);
    return 0;
  }
  if (argc > 4 && !strcmp(argv[1], "dbg")) { /* dbg OUT.ppm LEVEL FRAMES [FLIPS]: -1 the endless run */
    rd_run *r = &run;
    int l = atoi(argv[3]);
    run_start(r, l < 0 ? RUN_ENDLESS : RUN_LEVEL, l < 0 ? 0 : l, 0, l < 0 ? 6 : l / 8, 3);
    int n = atoi(argv[4]), fl = argc > 5 ? atoi(argv[5]) : 0;
    for (int i = 0; i < n && !r->ended; i++) {
      run_step(r, bot(r, fl));
      if (argc > 6 && i >= atoi(argv[6]) - 12 && i <= atoi(argv[6])) run_debug(r);
      if (i % 20 == 0 || (argc > 6 && i >= atoi(argv[6]) - 12 && i <= atoi(argv[6]))) printf("%4d x %7.1f y %7.1f vx %6.1f vy %6.1f a %6.1f w %6.1f g %d spd %5.1f flips %d score %d\n", i, r->b.x, r->b.y, r->b.vx, r->b.vy, r->b.a / 0.0174533f, r->b.w / 0.0174533f, r->b.ground, r->b.speed, r->flips, r->score);
    }
    printf("ended %d at x %.0f (finish %.0f) ms %.0f\n", r->ended, r->b.x, r->finish_x, r->ms);
    if (getenv("SEGS")) print_segs(r);
    draw_theme(r->theme);
    screen = S_RIDE, used_ok = true;
    draw_ride();
    ppm(argv[2]);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "solve")) { /* solve FIRST [LAST]: can the levels be ridden to their end */
    static uint8_t path[1600];
    int a = atoi(argv[2]), z = argc > 3 ? atoi(argv[3]) : a, ok = 0;
    for (int l = a; l <= z; l++) {
      int n = solve_level(l, path, 400);
      printf("level %d (%s %d): %s", l, group_name[l / 8], l % 8 + 1, n ? "ridden" : "not found");
      if (n) {
        run_start(&run, RUN_LEVEL, l, 0, l / 8, 9);
        for (int sg = 0; sg < n; sg++) for (int f = 0; f < SEG && !run.ended; f++) run_step(&run, pat(path[sg], f));
        printf(" in %.2f s, %d flips", run_level_time(&run) / 1000, run.flips);
        ok++;
      }
      printf("\n");
    }
    printf("%d of %d\n", ok, z - a + 1);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "docs")) { /* the README's pictures */
    char p[512];
    const char *dir = argv[2];
    fresh(), sv.flags = 1, sv.gems = 1350, sv.best = 37, sv.runs = 64, next_challenge();
    for (int b = 0; b < NBIKES; b += 3) own(b);
    for (int l = 1; l < 14; l++) sv.stars[l] = (uint8_t)(1 + l % 3), sv.best_ms[l] = 9000 + 731 * (uint32_t)l;
    sv.challenges = 7, next_challenge();
    /* the menu */
    go_menu(), sel = 0, draw_menu();
    snprintf(p, sizeof p, "%s/menu.ppm", dir), ppm(p);
    /* an endless run (the search's), a perfect double flip */
    static uint8_t path[1600];
    rd_run *r = &run;
    solve_run(RUN_ENDLESS, 0, 0, 11, path, 60);
    start_run(RUN_ENDLESS, 0), used_ok = true;
    run_start(r, RUN_ENDLESS, 0, 0, 7, 11), draw_theme(7);
    for (int k = 0; k < 60 * SEG; k++) {
      run_step(r, pat(path[k / SEG], k % SEG));
      if (k > 150 && r->score > 2 && r->text && r->text_ms > 1000 && r->text_ms < 1300 && r->b.ground && r->b.trail_n > 10) break;
    }
    sv.gems = 1350, draw_ride();
    snprintf(p, sizeof p, "%s/shot.ppm", dir), ppm(p);
    /* a level, part way */
    int n = solve_level(0, path, 300);
    start_run(RUN_LEVEL, 0), used_ok = true;
    for (int sg = 0; sg < n && sg < 30; sg++) for (int f = 0; f < SEG; f++) run_step(r, pat(path[sg], f));
    draw_ride();
    snprintf(p, sizeof p, "%s/level.ppm", dir), ppm(p);
    for (int sg = 30; sg < n; sg++) for (int f = 0; f < SEG && !r->ended; f++) run_step(r, pat(path[sg], f));
    for (int i = 0; i < 40; i++) run_step(r, false);
    level_done(), overlay = OV_DONE, sel = 0, draw_ride();
    snprintf(p, sizeof p, "%s/done.ppm", dir), ppm(p);
    /* the levels, the bikes, the colours */
    screen = S_LEVELS, page = 0, level_sel = 4, overlay = 0, draw_theme(0), draw_levels();
    snprintf(p, sizeof p, "%s/levels.ppm", dir), ppm(p);
    screen = S_BIKES, bike_sel = 9, bike_scroll = 0, overlay = 0, draw_theme(6), draw_bikes();
    snprintf(p, sizeof p, "%s/bikes.ppm", dir), ppm(p);
    screen = S_COLORS, color_sel = 1, draw_theme(6), draw_colors();
    snprintf(p, sizeof p, "%s/colours.ppm", dir), ppm(p);
    help_back = S_MENU, screen = S_HELP, draw_help();
    snprintf(p, sizeof p, "%s/help.ppm", dir), ppm(p);
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "film")) { /* film DIR [SEGMENTS]: an endless run found by the search, 30 frames a second */
    static uint8_t path[1600];
    int segs = argc > 3 ? atoi(argv[3]) : 40, level = argc > 4 ? atoi(argv[4]) : -1;
    fresh(), sv.flags = 1, sv.best = 21, sv.runs = 9, next_challenge();
    if (level >= 0) {
      segs = solve_level(level, path, 400);
      start_run(RUN_LEVEL, level), used_ok = true;
    } else {
      uint32_t sd = getenv("SEED") ? (uint32_t)atoi(getenv("SEED")) : 33;
      solve_run(RUN_ENDLESS, 0, 0, sd, path, segs);
      start_run(RUN_ENDLESS, 0), used_ok = true;
      run_start(&run, RUN_ENDLESS, 0, 0, 7, sd), draw_theme(7);
    }
    film_dir = argv[2];
    for (int sg = 0; sg < segs; sg++)
      for (int f = 0; f < SEG && !run.ended; f++) {
        run_step(&run, pat(path[sg], f));
        if (getenv("PRINT") && f == 0) printf("%5.0f x %6.0f y %6.0f vx %6.0f vy %6.0f g %d air %5.0f flips %d\n", run.ms, run.b.x, run.b.y, run.b.vx, run.b.vy, run.b.ground, run.b.air_ms, run.flips);
        if (run.ended == END_FINISH) level_done(), overlay = OV_DONE;
        if (f & 1) { draw_ride(); char p[512]; snprintf(p, sizeof p, "%s/%04d.ppm", film_dir, film_n++); ppm(p); }
      }
    for (int i = 0; i < 40; i++) { run_step(&run, false); if (i & 1) { draw_ride(); char p[512]; snprintf(p, sizeof p, "%s/%04d.ppm", film_dir, film_n++); ppm(p); } }
    printf("score %d, %d flips, %d perfect, ended %d, %d frames\n", run.score, run.flips, run.perfect, run.ended, film_n);
    return 0;
  }
  (void)film_frame;
  return 0;
}
/* (debugging: which pieces an endless run is made of) */
void print_segs(const rd_run *r);
void print_segs(const rd_run *r) {
  for (int i = 0; i < r->nseg; i++) printf("seg %d track %d (chunk %d) at x %d\n", i, r->seg[i].track, r->seg[i].track - NLEVELS, (int)r->seg[i].x);
}
