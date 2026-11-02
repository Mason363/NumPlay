/* Cat Ninja on a computer, for tests and pictures (make host, make test).
 *   play test              the cat's physics against the original's numbers (spec/), the first levels played
 *                          through with keys, every level loaded and run, random keys, saves
 *   play shot DIR KEYS     pictures (DIR/<frame>.ppm) of a run: KEYS is "frame:keys,..." (l r u d j R b h o)
 *   play film DIR KEYS N   every 2nd frame of a run of N frames (DIR/<n>.ppm), for the GIF */
#define main catninja_main
#include "../src/main.c"
#undef main
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

/* ---------------------------------------------------------------- the calculator, on a computer */
static uint16_t screen_px[240][320];
static void (*on_frame)(void);
void np_push(int x, int y, int w, int h, const color_t *px) {
  for (int j = 0; j < h; j++)
    for (int i = 0; i < w; i++) screen_px[y + j][x + i] = px[j * w + i];
  if (on_frame && y + h == 240) on_frame();
}
void *np_alloc(uint32_t n) {
  (void)n;
  return 0;
}
static uint64_t clock_ms;
static uint32_t (*script)(void);
static uint32_t frame_no, quit_at = 0xFFFFFFFFu;
uint32_t plat_keys(void) {
  frame_no++;
  if (frame_no >= quit_at) return PK_HOME;
  return script ? script() : 0;
}
uint32_t plat_millis(void) { return (uint32_t)(clock_ms += 16); }
void plat_sleep(uint32_t ms) { clock_ms += ms; }
static uint32_t lcg_s = 1;
static uint32_t lcg(void) { return lcg_s = lcg_s * 1664525u + 1013904223u; }
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
static void files_clear(void) { memset(files, 0, sizeof files); }
static void file_drop(const char *name) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) files[i].on = false;
}
static uint8_t *file_of(const char *name, uint32_t *len) {
  for (int i = 0; i < 4; i++)
    if (files[i].on && !strcmp(files[i].name, name)) return *len = files[i].len, files[i].data;
  return 0;
}

static void ppm(const char *path) {
  FILE *f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6 320 240 255\n");
  for (int y = 0; y < 240; y++)
    for (int x = 0; x < 320; x++) {
      uint16_t c = screen_px[y][x];
      uint8_t rgb[3] = {(uint8_t)((c >> 11) << 3 | (c >> 13)), (uint8_t)(((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                        (uint8_t)((c & 31) << 3 | ((c >> 2) & 7))};
      fwrite(rgb, 1, 3, f);
    }
  fclose(f);
}

static int fails;
#define CHECK(c, ...)                                                                                                  \
  do {                                                                                                                 \
    if (!(c)) {                                                                                                        \
      fails++;                                                                                                         \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                      \
      printf(__VA_ARGS__);                                                                                             \
      printf("\n");                                                                                                    \
    }                                                                                                                  \
  } while (0)

/* ---------------------------------------------------------------- key scripts: "frame:keys,frame:keys" */
static struct {
  uint32_t at, keys;
} steps_[512];
static int n_steps_;
static uint32_t keys_of(const char *s) {
  uint32_t k = 0;
  for (; *s && *s != ','; s++) switch (*s) {
    case 'l': k |= PK_LEFT; break;
    case 'r': k |= PK_RIGHT; break;
    case 'u': k |= PK_UP; break;
    case 'd': k |= PK_DOWN; break;
    case 'j': k |= PK_SHIFT; break;
    case 'R': k |= PK_DEL; break;
    case 'b': k |= PK_BACK; break;
    case 'o': k |= PK_OK; break;
    case 'h': k |= PK_HOME; break;
    }
  return k;
}
static void parse_keys(const char *s) {
  n_steps_ = 0;
  while (*s && n_steps_ < 512) {
    steps_[n_steps_].at = (uint32_t)strtoul(s, (char **)&s, 10);
    if (*s == ':') s++;
    steps_[n_steps_].keys = keys_of(s);
    n_steps_++;
    while (*s && *s != ',') s++;
    if (*s == ',') s++;
  }
}
static int goto_level_at = -2;
static uint32_t scripted(void) {
  uint32_t k = 0;
  if (frame_no == 1 && goto_level_at >= -1) {
    extern void game_debug_goto(int lv);
    game_debug_goto(goto_level_at);
  }
  for (int i = 0; i < n_steps_; i++)
    if (steps_[i].at <= frame_no) k = steps_[i].keys;
  return k;
}
static const char *shot_dir;
static int film_every;
static void shot_frame(void) {
  char path[256];
  if (getenv("CN_TRACE")) printf("%u L%d x %.2f y %.2f rot %d wr %.1f rotating %d dead %d\n", frame_no, level, P.x, P.y, currentrotation, world_rot, rotating, P.dead);
  if (film_every) {
    if (frame_no % film_every) return;
    snprintf(path, sizeof path, "%s/%05u.ppm", shot_dir, frame_no / film_every);
  } else {
    for (int i = 0; i < n_steps_; i++)
      if (steps_[i].at == frame_no) goto take;
    return;
  take:
    snprintf(path, sizeof path, "%s/%05u.ppm", shot_dir, frame_no);
  }
  ppm(path);
}

/* A player for the first levels: it runs right, climbs shafts by jumping from wall to wall, and jumps the gaps
   the levels ask it to. Each frame it looks at the cat and the tiles about it, as a player looks at the screen. */
static uint32_t bot_keys, bot_held;
static int bot_dir = 1, bot_wait;
static bool bot_climb;
static bool solid_at(int x, int y) { return !walk_at(x, y); }
static void bot_frame(void) {
  uint32_t k = 0;
  int cx = P.coordx, cy = P.coordy;
  if (bot_wait > 0) bot_wait--;
  k |= bot_dir > 0 ? 1u << GK_RIGHT : 1u << GK_LEFT;
  if (!P.dead) {
    bool wall = bot_dir > 0 ? solid_at(cx + 1, cy) : solid_at(cx, cy) || solid_at(cx - 1, cy);
    if (P.onground && !wall) bot_climb = false;
    if (P.onground && wall && !bot_wait) k |= 1u << GK_UP, bot_wait = 4, bot_climb = true; /* a wall ahead: up it */
    else if (bot_climb && !P.onground && P.speedx == 0 && wall && !bot_wait) { /* against it: jump off, the other way */
      k |= 1u << GK_UP, bot_wait = 4;
      bot_dir = -bot_dir;
    } else if (!P.onground && !wall && bot_dir < 0 && P.gravity > 3 && level == 1) {
      bot_dir = 1; /* (over the top of the shaft: right again) */
    }
    /* a pit with spikes ahead: jump it */
    if (P.onground && bot_dir > 0 && walk_at(cx + 2, cy + 1)) {
      int y = cy + 1;
      while (y < gh && walk_at(cx + 2, y) && (grid[y * gw + cx + 2] & 0x7F) != 2) y++;
      if (y < gh && (grid[y * gw + cx + 2] & 0x7F) == 2) k |= 1u << GK_UP;
    }
    if (!P.onground && P.gravity > -6 && P.gravity < 0 && P.doublejump && bot_dir > 0 && level == 1 && cx > 100)
      k |= 1u << GK_UP; /* (the floating floors: double jump) */
  }
  uint32_t on = k & ~bot_held, off = bot_held & ~k;
  for (int i = 0; i < 6; i++) {
    if (off >> i & 1) key_release(i);
    if (on >> i & 1) key_press(i);
  }
  if (k >> GK_UP & 1) k &= ~(1u << GK_UP); /* (taps: let go at once) */
  bot_held = k & ~(1u << GK_UP);
  if (on >> GK_UP & 1) key_release(GK_UP);
  bot_keys = on;
  game_step();
}

/* the game's own steps, without the shell: a fresh game at a level's spawn */
static void at_level(int lv) {
  game_new();
  extern void test_goto(int lv);
  test_goto(lv);
}


/* ---------------------------------------------------------------- the tests */
static double trunc20(double v) { return (double)(int)(v * 20) / 20; } /* a display object's x: twips */
extern void game_debug_goto(int lv);
extern void mine_create(int x, int y, int g);
static void press(int k) { key_press(k); }
static void release(int k) { key_release(k); }
static void steps(int n) {
  for (int i = 0; i < n; i++) game_step();
}
static void settle(void) { /* standing still on the ground */
  key_left = key_right = key_up = key_down = false;
  for (int i = 0; i < 120 && (i < 10 || !(P.onground && P.speedx == 0)); i++) game_step();
}

/* spec/physics.md: the run, the jumps, the roll, the wall jump, the spring */
static void test_physics(void) {
  /* running: each frame the speed is held to the last frame's top (6), gains 1, loses a tenth; the cat moves by it,
     its place kept in twentieths of a pixel (twips, truncated) */
  game_new();
  game_debug_goto(1); /* level 2's first floor */
  settle();
  double x = P.x, s = P.speedx;
  int top = P.maxspeedx;
  press(GK_RIGHT);
  for (int f = 0; f < 40; f++) {
    game_step();
    if (s >= top) s = top;
    top = 6;
    s = (s + 1) * 0.9;
    x = trunc20(x + s);
    CHECK(P.x == x && P.speedx == s, "run, frame %d: x %.2f (spec %.2f), speed %.4f (spec %.4f)", f, P.x, x, P.speedx, s);
  }
  CHECK(fabs(P.speedx - 6.3) < 1e-9, "running speed %.4f, spec 6.3", P.speedx);
  release(GK_RIGHT);
  settle();
  CHECK(P.speedx == 0 && P.onground, "stops: speed %.3f", P.speedx);

  /* jumping: up sets -8; each frame 0.5 more; a jump rises 60 pixels (three tiles) and lands where it began */
  game_new();
  game_debug_goto(-1); /* the title map: open above */
  settle();
  double y0 = P.y, ymin = P.y;
  press(GK_UP);
  release(GK_UP);
  CHECK(P.gravity == -8, "jump: gravity %.2f, spec -8", P.gravity);
  int f = 0;
  for (; f < 100; f++) {
    game_step();
      if (P.y < ymin) ymin = P.y;
    if (P.onground) break;
  }
  CHECK(y0 - ymin == 60, "jump height %.2f, spec 60", y0 - ymin);
  CHECK(P.y == y0, "lands at %.2f, began at %.2f", P.y, y0);

  /* the double jump: again in the air, once gravity is -6 or more: -8 again (120 pixels in all from the top) */
  settle();
  y0 = ymin = P.y;
  press(GK_UP), release(GK_UP);
  for (f = 0; f < 16; f++) game_step(); /* (the top of the first jump) */
  press(GK_UP), release(GK_UP);
  CHECK(P.gravity == -8 && P.doublefalling, "double jump: gravity %.2f", P.gravity);
  press(GK_UP), release(GK_UP);
  CHECK(P.gravity == -8 && !P.doublejump, "only one double jump");
  for (f = 0; f < 100; f++) {
    game_step();
    if (P.y < ymin) ymin = P.y;
    if (P.onground) break;
  }
  CHECK(y0 - ymin == 120, "double jump height %.2f, spec 120", y0 - ymin);

  /* rolling: Down held while running: top speed 10, friction 0.94: (10 + 1) x 0.94 = 10.34 */
  game_new();
  game_debug_goto(1);
  settle();
  press(GK_DOWN), press(GK_RIGHT);
  steps(60);
  CHECK(fabs(P.speedx - 10.34) < 1e-9 && P.rolling, "roll speed %.4f, spec 10.34", P.speedx);
  release(GK_DOWN), release(GK_RIGHT);

  /* the wall jump: up against a wall of ground in the air: 12 away from it, -12 up */
  game_new();
  game_debug_goto(1);
  P.x = 36 * 20 + OFFX - 1, P.y = 25 * 20 + OFFY, P.speedx = 0, P.gravity = 2, P.onground = false, P.doublejump = false;
  press(GK_UP), release(GK_UP);
  CHECK(P.speedx == 12 && P.gravity == -12, "wall jump: speed %.2f, gravity %.2f, spec 12 and -12", P.speedx, P.gravity);
  /* sliding down it: holding towards it while falling: gravity 2 */
  P.x = 36 * 20 + OFFX - 1, P.y = 25 * 20 + OFFY, P.speedx = -1, P.gravity = 3, P.onground = false;
  press(GK_LEFT);
  game_step();
  CHECK(P.gravity == 2 && P.hitwall, "wall slide: gravity %.2f, spec 2", P.gravity);
  release(GK_LEFT);

  /* the spring: -20 */
  game_new();
  game_debug_goto(-1);
  settle();
  change_tile((int)(P.x / 20), (int)(P.y / 20), 62, true, false, true, true);
  game_step();
  CHECK(P.gravity == -20, "spring: gravity %.2f, spec -20", P.gravity);

  /* a mine: set off, it blinks at 10, 8, 6, 4, 2 frames, then goes off (30 frames) */
  game_new();
  game_debug_goto(-1);
  settle();
  int m = n_mines;
  mine_create((int)P.x - 10, (int)P.y - 10, 0);
  for (f = 1; f <= 40 && !mines[m].off; f++) game_step();
  CHECK(f - 1 == 30, "mine: off after %d frames, spec 30", f - 1);

  /* the laser of level 9's second script: on from the start, switched every second (60 frames): off at 2 s */
  game_new();
  game_debug_goto(8);
  int lx = 85, ly = 27, t0 = grid[ly * gw + lx] & 0x7F;
  steps(61);
  int t1 = grid[ly * gw + lx] & 0x7F;
  steps(60);
  int t2 = grid[ly * gw + lx] & 0x7F;
  steps(60);
  int t3 = grid[ly * gw + lx] & 0x7F;
  CHECK(t0 == 15 && t1 == 15 && t2 == 0 && t3 == 15, "laser: %d %d %d %d, spec 15 15 0 15", t0, t1, t2, t3);

  /* dying: the message, then the cat back at its checkpoint 1.5 s (90 frames) later */
  game_new();
  game_debug_goto(1);
  settle();
  int sx = P.spawnx, sy = P.spawny, d0 = pdeaths;
  press(GK_R), release(GK_R);
  CHECK(P.dead && pdeaths == d0 + 1, "R kills the cat");
  for (f = 1; f <= 120 && P.dead; f++) game_step();
  CHECK(f - 1 == 90 && P.x == sx + OFFX && P.y == sy + OFFY, "back after %d frames at %.0f, %.0f (spec 90)", f - 1, P.x,
        P.y);
}

/* the first levels, played: from the title map through level 1 and level 2, to level 3 */
static void test_run(void) {
  game_new();
  bot_dir = 1, bot_wait = 0, bot_climb = false, bot_held = 0;
  int reached = -2, at = 0;
  for (int f = 0; f < 1400 && reached < 2; f++) {
    bot_frame();
    if (level > reached) reached = level, at = f;
  }
  CHECK(reached == 2, "the run reached level %d", reached + 1);
  CHECK(pdeaths == 0, "the run died %d times", pdeaths);
  printf("  title map to level 3 in %d frames (%.1f s), %d crystals\n", at, at / 60.0, collected);
}

/* every level: loaded, its scripts run, ten seconds of random keys each (twice), nothing out of the map */
static void test_levels(void) {
  for (int round = 0; round < 2; round++)
    for (int lv = -1; lv < 30; lv++) {
      game_new();
      game_debug_goto(lv);
      uint32_t held = 0;
      for (int f = 0; f < 600; f++) {
        uint32_t g = lcg() >> 7 & 63, on = g & ~held, off = held & ~g;
        if (lcg() % 4) g = held, on = off = 0;
        for (int i = 0; i < 6; i++) {
          if (off >> i & 1) key_release(i);
          if (on >> i & 1) key_press(i);
        }
        held = g;
        game_step();
        if (f % 3 == 0) {
          draw_frame();
          gfx_render(scene, NULL, 0, 240);
        }
        CHECK(level >= -1 && level < 30 && P.x == P.x && P.y == P.y, "level %d: lost (level %d)", lv + 1, level);
      }
    }
}

/* the whole app on random keys: pauses, menus, the How to play page, quitting and coming back */
static uint32_t fuzz_keys(void) {
  static uint32_t k;
  if (lcg() % 6 == 0) {
    static const uint32_t pick[] = {PK_LEFT, PK_RIGHT, PK_UP, PK_DOWN, PK_OK, PK_EXE, PK_BACK, PK_DEL, PK_SHIFT, 0};
    k = pick[lcg() % 10] | (lcg() % 3 ? 0 : pick[lcg() % 10]);
    if (k & PK_BACK && lcg() % 4) k &= ~PK_BACK;
  }
  return k;
}
static void test_fuzz(void) {
  for (int run = 0; run < 12; run++) {
    if (run % 4 == 0) files_clear();
    lcg_s = 1000 + run;
    frame_no = 0, quit_at = 2500 + lcg() % 1500;
    script = fuzz_keys;
    catninja_main();
    uint32_t n;
    const uint8_t *d = file_of(SAVE_NAME, &n);
    save_t t;
    CHECK(d && n == sizeof t, "fuzz %d: no save", run);
    if (d && n == sizeof t) {
      memcpy(&t, d, sizeof t);
      CHECK(save_ok(&t), "fuzz %d: a bad save", run);
    }
  }
  script = 0, quit_at = 0xFFFFFFFFu;
}

/* the saves: kept, damaged, lost, from the copy; and the game back where it was */
static void test_saves(void) {
  files_clear();
  /* play into level 2 with the bot, then quit */
  game_new();
  bot_dir = 1, bot_wait = 0, bot_climb = false, bot_held = 0;
  for (int f = 0; f < 600; f++) bot_frame();
  CHECK(level == 1, "the bot is in level 2 (%d)", level + 1);
  int lv = level;
  save();
  uint32_t n;
  CHECK(plat_load(SAVE_NAME, &n) && n == sizeof(save_t), "the save is written");
  save_t kept = sv;
  /* back: the title map, the way right leads to level 2, where the cat was */
  memset(&sv, 0, sizeof sv);
  load();
  CHECK(!memcmp(&sv, &kept, sizeof sv), "the save reads back");
  start_game();
  CHECK(level == -1 && playerpoints[5] == lv, "back on the title map, its way right to level %d", playerpoints[5] + 1);
  /* lost: from the copy */
  file_drop(SAVE_NAME);
  memset(&sv, 0, sizeof sv);
  load();
  CHECK(!memcmp(&sv, &kept, sizeof sv), "the save comes back from the copy");
  /* damaged: from the copy */
  save();
  uint8_t *d = file_of(SAVE_NAME, &n);
  d[10] ^= 0x55;
  memset(&sv, 0, sizeof sv);
  load();
  CHECK(sv.level == kept.level && sv.x == kept.x && save_ok(&sv), "a damaged save comes back from the copy");
  /* both gone: a new game */
  files_clear();
  memset(&sv, 0x77, sizeof sv);
  load();
  CHECK(sv.level == -1 && sv.deaths == 0, "no save: a new game");
  /* a damaged copy alone: a new game, no crash */
  files_clear();
  plat_save(COPY_NAME, "\0# Cat Ninja's saves: keep this file\n#>catninja.sav:AAAA!!\n", 60);
  load();
  CHECK(sv.level == -1, "a damaged copy: a new game");
}

int main(int argc, char **argv) {
  if (argc >= 2 && !strcmp(argv[1], "test")) {
    gfx_buf = strip;
    draw_init();
    printf("physics\n");
    test_physics();
    printf("the first levels\n");
    test_run();
    printf("every level, random keys\n");
    test_levels();
    printf("saves\n");
    test_saves();
    printf("the app, random keys\n");
    test_fuzz();
    printf("%s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
  }
  if (argc >= 4 && (!strcmp(argv[1], "shot") || !strcmp(argv[1], "film"))) {
    shot_dir = argv[2];
    parse_keys(argv[3]);
    film_every = !strcmp(argv[1], "film") ? 2 : 0;
    quit_at = argc >= 5 ? (uint32_t)atoi(argv[4]) : steps_[n_steps_ - 1].at + 2;
    script = scripted;
    on_frame = shot_frame;
    files_clear();
    if (argc >= 6 && !strchr(argv[5], ',')) { /* a level to start in */
      goto_level_at = atoi(argv[5]);
      save_t s;
      memset(&s, 0, sizeof s);
      s.magic = MAGIC, s.version = VERSION, s.flags = 1, s.level = -1, s.spawnlevel = -1;
      s.sum = fnv(&s, offsetof(save_t, sum));
      plat_save(SAVE_NAME, &s, sizeof s);
    } else if (argc >= 6) { /* a save to start from: level x y */
      int lv = 0, x = 0, y = 0;
      sscanf(argv[5], "%d,%d,%d", &lv, &x, &y);
      save_t s;
      memset(&s, 0, sizeof s);
      s.magic = MAGIC, s.version = VERSION, s.flags = 1, s.level = (int8_t)lv, s.x = (int16_t)x, s.y = (int16_t)y;
      s.spawnlevel = -1;
      s.sum = fnv(&s, offsetof(save_t, sum));
      plat_save(SAVE_NAME, &s, sizeof s);
    } else {
      save_t s;
      memset(&s, 0, sizeof s);
      s.magic = MAGIC, s.version = VERSION, s.flags = 1, s.level = -1, s.spawnlevel = -1;
      s.sum = fnv(&s, offsetof(save_t, sum));
      plat_save(SAVE_NAME, &s, sizeof s);
    }
    catninja_main();
    printf("frames %u, slowest %u ms\n", perf_frames, perf_max);
    return 0;
  }
  if (argc >= 3 && !strcmp(argv[1], "levels")) { /* every level's start, a second in */
    gfx_buf = strip;
    draw_init();
    for (int lv = -1; lv < 30; lv++) {
      game_new();
      extern void game_debug_goto(int lv);
      game_debug_goto(lv);
      for (int f = 0; f < 60; f++) game_step();
      draw_frame();
      gfx_render(scene, NULL, 0, 240);
      char path[256];
      snprintf(path, sizeof path, "%s/level%02d.ppm", argv[2], lv + 1);
      ppm(path);
    }
    return 0;
  }
  if (argc >= 4 && !strcmp(argv[1], "trace")) { /* trace LEVEL KEYS [frames]: the cat, frame by frame */
    gfx_buf = strip;
    draw_init();
    parse_keys(argv[3]);
    int frames = argc >= 5 ? atoi(argv[4]) : (int)steps_[n_steps_ - 1].at + 1;
    game_new();
    extern void game_debug_goto(int lv);
    game_debug_goto(atoi(argv[2]));
    uint32_t held = 0;
    for (frame_no = 1; (int)frame_no <= frames; frame_no++) {
      uint32_t g = game_keys_of(scripted()), on = g & ~held, off = held & ~g;
      for (int i = 0; i < 6; i++) {
        if (off >> i & 1) key_release(i);
        if (on >> i & 1) key_press(i);
      }
      held = g;
      game_step();
      printf("%4u L%d x %.2f y %.2f c %d,%d sx %.3f g %.3f %s%s%s rot %d\n", frame_no, level, P.x, P.y, P.coordx, P.coordy,
             P.speedx, P.gravity, P.onground ? "ground " : "", P.dead ? "dead " : "", P.hitwall ? "wall " : "",
             currentrotation);
    }
    return 0;
  }
  if (argc >= 2 && !strcmp(argv[1], "bot")) { /* the climbing player, traced */
    gfx_buf = strip;
    draw_init();
    game_new();
    int frames = argc >= 3 ? atoi(argv[2]) : 2000;
    for (int f = 0; f < frames; f++) {
      bot_frame();
      printf("%4d L%d x %.2f y %.2f c %d,%d sx %.3f g %.3f %s%s keys %x\n", f, level, P.x, P.y, P.coordx, P.coordy, P.speedx,
             P.gravity, P.onground ? "ground " : "", P.dead ? "dead " : "", bot_keys);
    }
    return 0;
  }
  (void)at_level;
  (void)file_of;
  (void)file_drop;
  printf("%s\n", fails ? "FAILED" : "ok");
  return fails ? 1 : 0;
}
