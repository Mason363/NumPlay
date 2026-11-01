/* Famidash on a computer, for tests and pictures (make host, make test).
 *   play test            the physics against the game's numbers (spec/), Stereo Madness played to its end by
 *                        a recorded run, practice mode, random keys, saves (kept, damaged, lost, from the copy)
 *   play docs DIR        the README's pictures (DIR/<name>.ppm)
 *   play film DIR        the frames of the README's GIF (DIR/<n>.ppm, 30 a second) */

#define main famidash_main
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
  uint8_t data[8192];
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
static void draw_now(void) { gfx_render(scene, NULL, 0, 240); }

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ---------------------------------------------------------------- a level run without pictures: inputs[] is OK held per frame */
static int sim_frames;
static int sim_run(int lv, const uint8_t *hold, int n, bool practice_on) {
  rng_seed = 0xFDFDFDFD;
  level = (uint8_t)lv;
  game_begin();
  game_start();
  joypad1 = (pad_t){0, 0, 0};
  if (practice_on) game_store_practice();
  for (int f = 0; f < n; f++) {
    uint8_t h = hold[f] ? PAD_A : 0;
    joypad1.press = (uint8_t)(h & ~joypad1.hold), joypad1.release = (uint8_t)(~h & joypad1.hold), joypad1.hold = h;
    game_step();
    if (gameState == STATE_LVLDONE) { sim_frames = f + 1; return 1; }
    if (game_dead()) { sim_frames = f + 1; return 0; }
  }
  sim_frames = n;
  return 0;
}

/* finds OK presses that get through a level: at each death, the press (when and how long) that lives
 * longest, looking further back when nothing helps */
#define MAXF 12000
static uint8_t bot_hold[MAXF], trial[MAXF], best_trial[MAXF];
static int bot_search(int lv) {
  memset(bot_hold, 0, sizeof bot_hold);
  int window = 75, stuck = 0, last_d = 0;
  static const int lens[] = {1, 3, 8, 14, 20, 28, 40, 60, 90};
  for (int round = 0; round < 3000; round++) {
    if (sim_run(lv, bot_hold, MAXF, false)) return sim_frames;
    int d = sim_frames, best = d;
    if (d <= last_d) stuck++;
    else stuck = 0, window = 75;
    last_d = d > last_d ? d : last_d;
    if (stuck > 2) window = window < 400 ? window + 60 : window;
    for (int back = 1; back <= window; back++) {
      int t = d - back;
      if (t < 0) break;
      for (unsigned li = 0; li < sizeof lens / sizeof lens[0]; li++) {
        memcpy(trial, bot_hold, (size_t)t);
        memset(trial + t, 0, sizeof trial - (size_t)t);
        for (int k = t; k < t + lens[li] && k < MAXF; k++) trial[k] = 1;
        int r = sim_run(lv, trial, d + 900, false);
        int f = r ? 100000 : sim_frames;
        if (f > best) best = f, memcpy(best_trial, trial, sizeof trial);
        if (r) break;
      }
      if (best >= 100000) break;
    }
    if (best > d) memcpy(bot_hold, best_trial, sizeof bot_hold);
    else { /* nothing helps: drop the last press before the death and look further back next time */
      int t = d - 1;
      while (t > 0 && !bot_hold[t]) t--;
      while (t > 0 && bot_hold[t]) bot_hold[t--] = 0;
      window += 60;
    }
  }
  sim_run(lv, bot_hold, MAXF, false);
  return -sim_frames;
}

/* Stereo Madness, played to its end: OK held from frame a for b frames (found by tests/bot.c) */
static const uint16_t stereo_madness[] = {93,156,272,255,543,323,886,17,932,48,1034,190,1243,115,1811,1,1829,35,1958,73,2096,3,
  2118,89,2311,171,2505,92,2638,250,2915,90,3020,46,3089,183,3292,78,3384,206,3634,168,3873,233,4124,178,4327,150,4561,101,
  4738,83,4873,40,4957,37,5011,94};
#define STEREO_FRAMES 5105
static bool sm_held(int f) {
  for (unsigned i = 0; i < sizeof stereo_madness / sizeof stereo_madness[0]; i += 2)
    if (f >= stereo_madness[i] && f < stereo_madness[i] + stereo_madness[i + 1]) return true;
  return false;
}

/* ---------------------------------------------------------------- driving the game directly */
static void steps(int n, uint16_t k) {
  for (int i = 0; i < n; i++) {
    hit = (uint16_t)(k & ~keys);
    keys = k;
    tick();
  }
}
static void boot(void) {
  memset(files, 0, sizeof files);
  gfx_buf = strip;
  ppu_init();
  load();
  screen = S_TITLE, next_screen = -1, fade_dir = 0;
  enter(S_TITLE);
  steps(20, 0);
}
static void start_level(int l) {
  level = (uint8_t)l;
  game_begin();
  enter(S_PLAY);
  steps(12, 0); /* the fade in */
}


/* ---------------------------------------------------------------- the physics against the spec */
void fd_test_set(uint8_t gm, uint8_t mini, uint8_t grav, int16_t vy);
int16_t fd_test_vy(void);
uint16_t fd_test_y(void);
uint16_t fd_test_x(void);
uint8_t fd_test_speed(void);
static void step_keys(uint8_t pad) {
  joypad1.press = (uint8_t)(pad & ~joypad1.hold), joypad1.release = (uint8_t)(~pad & joypad1.hold), joypad1.hold = pad;
  game_step();
}
static void fresh_level(int lv) {
  rng_seed = 0xFDFDFDFD;
  level = (uint8_t)lv;
  game_begin();
  game_start();
  joypad1 = (pad_t){0, 0, 0};
}
static void test_physics(void) {
  /* the numbers in the tables are the spec's (spec/famidash_spec.md, 60 Hz) */
  static const uint16_t speeds[6] = {0x02C4, 0x023B, 0x0371, 0x0429, 0x051E, 0x016E};
  for (int i = 0; i < 6; i++) CHECK(cube_speed[i] == speeds[i], "speed %d: %04x", i, cube_speed[i]);
  CHECK(JUMP_VEL[0] == 0xFA70 && JUMP_VEL[2] == 0xFB30 && JUMP_VEL[1] == 0x0590, "cube jump -0x590, mini -0x4D0, mirrored");
  CHECK(CUBE_GRAVITY[0] == 0x6B && CUBE_GRAVITY[2] == 0x6F && CUBE_MAX_FALLSPEED[0] == 0x600, "cube gravity and fall cap");
  CHECK(SHIP_GRAVITY_BASE[0] == 0x2A && SHIP_GRAVITY[0] == 0x22 && SHIP_GRAVITY_AFTER_HOLD[0] == 0x32 &&
        SHIP_GRAVITY_HOLD_FALL[0] == 0x34, "ship gravities");
  CHECK(BALL_GRAVITY[0] == 0x47 && UFO_GRAVITY[0] == 0x32 && SPIDER_GRAVITY[0] == 0x4B && SWING_GRAVITY[0] == 0x32, "gravities");
  CHECK(UFO_JUMP_VEL[0] == 0xFCD0 && ROBOT_JUMP_VEL[0] == 0xFD50 && ROBOT_JUMP_TIME == 0x13, "ufo and robot jumps");
  CHECK(PAD_HEIGHT_BLUE[0] == 0x03A0 && ORB_BALL_HEIGHT_BLUE[0] == 0x01A0, "blue orbs and pads");
  static const int16_t yellow_orb[8] = {-0x590, -0x450, -0x410, -0x3B0, -0x590, -0x440, 0, -0x3A0};
  static const int16_t yellow_pad[8] = {-0x7C0, -0x3C0, -0x4F0, -0x330, -0x8B0, -0x500, 0, -0x450};
  static const int16_t pink_orb[8] = {-0x3D0, -0x200, -0x330, -0x220, -0x450, -0x350, 0, -0x2D0};
  static const int16_t red_pad[8] = {-0x9F0, -0x620, -0x630, -0x400, -0xA50, -0x690, 0, -0x660};
  for (int g = 0; g < 8; g++) {
    CHECK((int16_t)adjust_heights[0][g] == yellow_orb[g], "yellow orb, mode %d: %d", g, (int16_t)adjust_heights[0][g]);
    CHECK((int16_t)adjust_heights[0][8 + g] == yellow_pad[g], "yellow pad, mode %d", g);
    CHECK((int16_t)adjust_heights[0][16 + g] == pink_orb[g], "pink orb, mode %d", g);
    CHECK((int16_t)adjust_heights[0][64 + g] == red_pad[g], "red pad, mode %d", g);
    CHECK((int16_t)adjust_heights[1][g] == -yellow_orb[g], "upside down, mode %d", g);
  }

  /* Stereo Madness starts on flat ground: the cube runs at 0x2C4 a frame */
  fresh_level(0);
  for (int i = 0; i < 40; i++) step_keys(0);
  uint32_t x0 = (uint32_t)(scroll_x << 8) + fd_test_x();
  for (int i = 0; i < 30; i++) step_keys(0);
  uint32_t x1 = (uint32_t)(scroll_x << 8) + fd_test_x();
  CHECK(x1 - x0 == 30 * 0x2C4, "30 frames at speed 1: %u (want %u)", x1 - x0, 30 * 0x2C4);
  CHECK(fd_test_vy() == 0 && fd_test_x() >> 8 == 0x50, "on the ground, the camera holds the cube at 80 (%04x)", fd_test_x());

  /* a jump: -0x590, then 0x6B a frame, back on the ground as the spec's sum says */
  uint16_t ground = fd_test_y();
  step_keys(PAD_A);
  CHECK(fd_test_vy() == -0x590, "jump velocity %d", fd_test_vy());
  int v = -0x590, y = 0, top = 0, n = 0;
  do v += 0x6B, y += v, top = y < top ? y : top, n++;
  while (y < 0);
  int ytop = 0, frames = 0;
  int16_t last = fd_test_vy();
  for (int i = 0; i < 60; i++) {
    step_keys(0);
    frames++;
    int dy = (int)fd_test_y() - (int)ground;
    ytop = dy < ytop ? dy : ytop;
    if (i == 0) CHECK(fd_test_vy() == last + 0x6B, "gravity 0x6B a frame (%d -> %d)", last, fd_test_vy());
    if (fd_test_vy() == 0 && i > 2) break;
  }
  CHECK(ytop / 256 == top / 256, "the jump's top: %d px (spec %d)", ytop / 256, top / 256);
  CHECK(frames >= n - 1 && frames <= n + 1, "in the air %d frames (spec %d)", frames, n);

  /* the ship: holding pulls up by 0x2A a frame, letting go (still rising) pulls down by 0x32 */
  fresh_level(0);
  for (int i = 0; i < 40; i++) step_keys(0);
  fd_test_set(GAMEMODE_SHIP, 0, 0, 0);
  step_keys(PAD_A);
  CHECK(fd_test_vy() == -0x2A, "ship, held: %d", fd_test_vy());
  for (int i = 0; i < 6; i++) step_keys(PAD_A);
  int16_t vs = fd_test_vy();
  step_keys(0);
  CHECK(fd_test_vy() == vs + 0x32, "ship, let go while rising: %d -> %d", vs, fd_test_vy());
  for (int i = 0; i < 60; i++) step_keys(PAD_A);
  CHECK(fd_test_vy() >= -0x443, "ship's top speed -0x443: %d", fd_test_vy());

  /* the UFO jumps -0x330 on a press, the ball flips with 0x200 */
  fresh_level(0);
  for (int i = 0; i < 40; i++) step_keys(0);
  fd_test_set(GAMEMODE_UFO, 0, 0, 0);
  step_keys(PAD_A);
  CHECK(fd_test_vy() == -0x330, "ufo jump %d", fd_test_vy());
  fresh_level(0);
  for (int i = 0; i < 40; i++) step_keys(0);
  fd_test_set(GAMEMODE_BALL, 0, 0, 0);
  step_keys(0);
  step_keys(PAD_A);
  CHECK(fd_test_vy() == -0x200 && player_gravity[0] == GRAVITY_UP, "ball flip %d, gravity %d", fd_test_vy(), player_gravity[0]);

  /* the cube falls no faster than 0x600, 0x700 in levels like this one with maxFallSpeed_is_7 (gravity turns back
   * over it) */
  fresh_level(0);
  for (int i = 0; i < 40; i++) step_keys(0);
  fd_test_set(GAMEMODE_CUBE, 0, GRAVITY_UP, 0); /* upside down, it falls up into the open sky */
  int16_t most = 0;
  for (int i = 0; i < 40; i++) {
    step_keys(0);
    most = fd_test_vy() < most ? fd_test_vy() : most;
  }
  CHECK(level_list[0].hdr[5] >> 7, "Stereo Madness falls at up to 7");
  CHECK(most >= -(0x700 + 0x6B) && most <= -0x700 + 0x6B, "upside down, top fall speed %d", most);
}

/* ---------------------------------------------------------------- Stereo Madness, played */
static void test_run(void) {
  memset(bot_hold, 0, sizeof bot_hold);
  for (unsigned i = 0; i < sizeof stereo_madness / sizeof stereo_madness[0]; i += 2)
    for (int k = 0; k < stereo_madness[i + 1]; k++) bot_hold[stereo_madness[i] + k] = 1;
  CHECK(sim_run(0, bot_hold, MAXF, false) && sim_frames == STEREO_FRAMES, "the recorded presses finish Stereo Madness (%d)", sim_frames);
  CHECK(coins == 0, "no coins on this route");
  /* the same with the keys, from the title screen */
  boot();
  sv.flags |= 3;
  menusel = BTN_MAIN, title_sel_drawn = 0xFF;
  steps(2, 0), steps(1, PK_OK), steps(30, 0);
  CHECK(screen == S_LEVELS && level == 0, "Play opens the level select at Stereo Madness (%d, %d)", screen, level);
  steps(1, PK_EXE), steps(1, 0);
  int f = 0, guard = 0;
  while (screen != S_PLAY && guard++ < 200) steps(1, 0);
  while (screen == S_PLAY && guard++ < 7000) {
    bool run = phase == P_RUN;
    steps(1, sm_held(f) ? PK_OK : 0);
    if (run) f++;
  }
  CHECK(screen == S_DONE && attemptCounter[0] == 1, "level complete on the first attempt (screen %d, %d run frames)", screen, f);
  CHECK(prog.complete[0] && prog.normal[0] == 100, "Stereo Madness kept as done (%d, %d%%)", prog.complete[0], prog.normal[0]);
  steps(240, 0);
  CHECK(done_state == 7, "the level complete screen settles");
  steps(1, PK_OK), steps(30, 0);
  CHECK(screen == S_LEVELS, "OK on the menu button: back to the level select");
  uint32_t n;
  const uint8_t *d = plat_load(SAVE_NAME, &n);
  CHECK(d && n == sizeof(save_t) && ((const save_t *)d)->prog.complete[0], "saved");
}

/* ---------------------------------------------------------------- practice mode */
static void test_practice(void) {
  boot();
  sv.flags |= 3;
  level = 0, game_begin(), enter(S_PLAY);
  while (phase != P_RUN) steps(1, 0);
  steps(60, 0);
  steps(1, PK_BACK), steps(1, 0);
  CHECK(phase == P_PAUSE, "Back pauses");
  steps(1, PK_DOWN), steps(1, 0), steps(1, PK_OK), steps(1, 0);
  CHECK(phase == P_RUN && practice_point_count == 1, "Practice puts the first checkpoint down (%d)", practice_point_count);
  uint32_t cp = scroll_x;
  int guard = 0;
  while (phase == P_RUN && guard++ < 600) steps(1, 0);
  CHECK(phase == P_DYING, "no jumps: the first spike");
  while (phase != P_RUN && guard++ < 900) steps(1, 0);
  CHECK(attemptCounter[0] == 2 && scroll_x + 300 >= cp && scroll_x <= cp + 64, "back at the checkpoint (%u, it was %u)", (unsigned)scroll_x, (unsigned)cp);
  steps(1, PK_DOWN), steps(1, 0);
  CHECK(practice_point_count == 2, "Down places a checkpoint (%d)", practice_point_count);
  uint8_t cur = curr_practice_point;
  steps(1, PK_DEL), steps(1, 0);
  CHECK(curr_practice_point == (uint8_t)(cur - 1) || practice_point_count == 1, "delete goes back to the one before");
  /* the whole level in practice: only the practice bar fills */
  memset(&prog, 0, sizeof prog);
  memset(bot_hold, 0, sizeof bot_hold);
  for (unsigned i = 0; i < sizeof stereo_madness / sizeof stereo_madness[0]; i += 2)
    for (int k = 0; k < stereo_madness[i + 1]; k++) bot_hold[stereo_madness[i] + k] = 1;
  int r = sim_run(0, bot_hold, MAXF, true);
  CHECK(r, "practice: the run still finishes");
  set_completion_data();
  CHECK(prog.practice[0] == 100 && !prog.complete[0] && prog.normal[0] == 0, "practice: only its bar (%d %d %d)", prog.practice[0], prog.complete[0], prog.normal[0]);
}

/* ---------------------------------------------------------------- random keys */
static long fuzz_screens[16];
static void test_fuzz(int sessions) {
  static const uint16_t pool[] = {PK_OK, PK_EXE, PK_UP, PK_DOWN, PK_LEFT, PK_RIGHT, PK_BACK, PK_DEL, 0, 0, 0, PK_OK, PK_OK};
  for (int s = 0; s < sessions; s++) {
    rnd = (uint32_t)s * 7919u + 13u;
    boot();
    if (s & 1) sv.flags |= 3;
    if (s % 5 == 0) { /* straight into a level */
      level = (uint8_t)(lcg() % NLEVELS);
      game_begin();
      enter(S_PLAY);
    }
    for (int i = 0; i < 1500; i++) {
      uint16_t k = (lcg() % 3) ? keys : pool[lcg() % (sizeof pool / sizeof pool[0])];
      steps(1, k);
      fuzz_screens[screen]++;
      if (i % 500 == 499) draw_now();
    }
    CHECK(screen >= S_TITLE && screen <= S_CREDITS, "session %d: still on a screen", s);
  }
  printf("random keys: %d sessions; frames on the title %ld, level select %ld, playing %ld, level complete %ld, "
         "customize %ld, settings %ld, how to play %ld\n", sessions, fuzz_screens[S_TITLE], fuzz_screens[S_LEVELS],
         fuzz_screens[S_PLAY], fuzz_screens[S_DONE], fuzz_screens[S_CUSTOM], fuzz_screens[S_SETTINGS] + fuzz_screens[S_FUN],
         fuzz_screens[S_HELP]);
  CHECK(fuzz_screens[S_PLAY] > 50000, "the random keys play levels too");
  /* every level loads and runs with the button held and let go */
  for (int lv = 0; lv < NLEVELS; lv++) {
    fresh_level(lv);
    for (int i = 0; i < 900; i++) {
      step_keys((i / 23) & 1 ? PAD_A : 0);
      if (game_dead()) game_reset();
      if (gameState == STATE_LVLDONE) break;
      if (i % 300 == 0) game_banks(), game_draw(), bg_mode = BG_LEVEL, draw_now();
    }
  }
}

/* ---------------------------------------------------------------- saves */
static uint32_t help_script(void) { return (frame_no == 30 || frame_no == 60) ? PK_OK : 0; }
static void test_saves(void) {
  boot();
  icon = 7, icon_colors[0] = 0x16, prog.coin2[3] = 1, prog.normal[5] = 42, options |= platformer;
  save();
  uint32_t n;
  uint8_t keep[sizeof(save_t)];
  memcpy(keep, file_of(SAVE_NAME, &n), sizeof keep);
  icon = 0, icon_colors[0] = 0x2A, memset(&prog, 0, sizeof prog), options = 0;
  load();
  CHECK(icon == 7 && icon_colors[0] == 0x16 && prog.coin2[3] && prog.normal[5] == 42 && (options & platformer), "kept");
  /* damaged: the copy in famidash_saves.py takes over */
  uint8_t *d = file_of(SAVE_NAME, &n);
  d[40] ^= 0x5A;
  icon = 0, memset(&prog, 0, sizeof prog);
  load();
  CHECK(icon == 7 && prog.normal[5] == 42, "damaged: from the copy");
  /* lost: the copy again */
  forget(SAVE_NAME);
  icon = 0, memset(&prog, 0, sizeof prog);
  load();
  CHECK(icon == 7 && prog.normal[5] == 42, "lost: from the copy");
  old_copy("# Famidash keeps a copy of your game here,\n# so that installing it again doesn't erase it.\n");
  icon = 0, memset(&prog, 0, sizeof prog);
  load();
  CHECK(icon == 7 && prog.normal[5] == 42, "lost: from an older copy");
  /* both damaged: a fresh game, no crash */
  memcpy(file_of(COPY_NAME, &n) + 120, "!!!!", 4);
  plat_save(SAVE_NAME, keep, 10);
  load();
  CHECK(icon == 0 && prog.normal[5] == 0 && icon_colors[0] == 0x2A && viseffects == 1 && auto_practicepoints == 1,
        "both gone: a fresh game with the NES's defaults");
  /* a save with values out of range is refused */
  save_t bad;
  memcpy(&bad, keep, sizeof bad);
  bad.icon = 200, bad.sum = fnv(&bad, offsetof(save_t, sum));
  CHECK(!save_ok(&bad), "an icon out of range is refused");
  bad = *(const save_t *)keep, bad.prog.normal[0] = 101, bad.sum = fnv(&bad, offsetof(save_t, sum));
  CHECK(!save_ok(&bad), "a percentage over 100 is refused");
  /* the first time: how to play; OK twice reads it, Home saves that */
  memset(files, 0, sizeof files);
  frame_no = 0, quit_at = 120, script = help_script;
  famidash_main();
  quit_at = 0xFFFFFFFFu, script = NULL;
  load();
  CHECK(sv.flags & 1, "how to play read, and Home saved it");
}

int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "test";
  if (!strcmp(mode, "docs") && argc > 2) { /* the README's pictures */
    char p[512];
#define SHOT(name) (draw_now(), snprintf(p, sizeof p, "%s/%s.ppm", argv[2], name), ppm(p))
    boot();
    sv.flags |= 3;
    menusel = BTN_MAIN, title_sel_drawn = 0xFF;
    rng_seed = 7;
    enter(S_TITLE), steps(70, 0), SHOT("menu");
    prog.normal[0] = 100, prog.complete[0] = 1, prog.coin1[0] = 1, prog.practice[0] = 100;
    prog.normal[2] = 64, prog.practice[2] = 88, prog.coin2[2] = 1;
    level = 2, enter(S_LEVELS), steps(40, 0), SHOT("levels");
    icon = 4, icon_colors[0] = 0x28, icon_colors[1] = 0x12, enter(S_CUSTOM), steps(30, 0), SHOT("customize");
    icon = 0, icon_colors[0] = 0x2A, icon_colors[1] = 0x2C;
    memset(&prog, 0, sizeof prog);
    level = 0, game_begin(), enter(S_PLAY);
    int f = 0;
    while (f < 5200 && screen == S_PLAY) {
      bool run = phase == P_RUN;
      steps(1, sm_held(f) ? PK_OK : 0);
      if (run) f++;
      if (f == 2350 && run) SHOT("ship");
      if (f == 2566 && run) SHOT("shot");
    }
    steps(200, 0), SHOT("done");
    /* practice: a checkpoint on the way */
    level = 0, game_begin(), enter(S_PLAY);
    f = 0;
    while (f < 500) {
      bool run = phase == P_RUN;
      if (f == 380 && run) steps(1, PK_BACK), steps(1, 0), steps(1, PK_DOWN), steps(1, 0), steps(1, PK_OK), steps(1, 0);
      steps(1, sm_held(f) ? PK_OK : 0);
      if (run) f++;
    }
    SHOT("practice");
    steps(1, PK_BACK), steps(2, 0), SHOT("pause");
    return 0;
  }
  if (!strcmp(mode, "film") && argc > 4) { /* DIR FROM TO: Stereo Madness's run frames FROM..TO, every other */
    char p[512];
    boot();
    sv.flags |= 3;
    int a = atoi(argv[3]), b = atoi(argv[4]), n = 0;
    level = 0, game_begin(), enter(S_PLAY);
    int f = 0;
    while (f < b && screen == S_PLAY) {
      bool run = phase == P_RUN;
      steps(1, sm_held(f) ? PK_OK : 0);
      if (run) {
        if (f >= a && !(f & 1)) draw_now(), snprintf(p, sizeof p, "%s/%05d.ppm", argv[2], n++), ppm(p);
        f++;
      }
    }
    return 0;
  }
  if (!strcmp(mode, "trace")) { /* level, then press list: start,len,... ; prints the player near the end */
    gfx_buf = strip;
    ppu_init();
    int lv = atoi(argv[2]);
    memset(bot_hold, 0, sizeof bot_hold);
    for (int i = 3; i + 1 < argc; i += 2)
      for (int k = 0; k < atoi(argv[i + 1]); k++) bot_hold[atoi(argv[i]) + k] = 1;
    int r = sim_run(lv, bot_hold, MAXF, false);
    int n = sim_frames;
    printf("%s at %d\n", r ? "done" : "dead", n);
    sim_run(lv, bot_hold, n > 90 ? n - 90 : 0, false);
    for (int f = n > 90 ? n - 90 : 0; f < n; f++) {
      uint8_t h = bot_hold[f] ? PAD_A : 0;
      joypad1.press = (uint8_t)(h & ~joypad1.hold), joypad1.release = (uint8_t)(~h & joypad1.hold), joypad1.hold = h;
      game_step();
      printf("%5d %c x %u.%02x y %3d.%02x vy %6d gm %d\n", f, h ? '*' : ' ', (unsigned)(scroll_x + (player_relx[0] >> 8)),
             player_relx[0] & 0xFF, (scroll_y + (player_rely[0] >> 8)), player_rely[0] & 0xFF, player_vel_y[0], gamemode);
    }
    return 0;
  }
  if (!strcmp(mode, "bot")) { /* prints the presses that finish a level */
    gfx_buf = strip;
    ppu_init();
    int lv = argc > 2 ? atoi(argv[2]) : 0;
    int r = bot_search(lv);
    printf("level %d: %s at frame %d\n", lv, r > 0 ? "done" : "stuck", r > 0 ? r : -r);
    int n = r > 0 ? r : -r;
    printf("{");
    for (int f = 0; f < n; f++)
      if (bot_hold[f] && (f == 0 || !bot_hold[f - 1])) {
        int e = f;
        while (e < n && bot_hold[e]) e++;
        printf("%d,%d,", f, e - f);
      }
    printf("}\n");
    if (argc > 3) { /* pictures of the last second */
      char p[512];
      rng_seed = 0xFDFDFDFD;
      level = (uint8_t)lv;
      game_begin();
      game_start();
      screen = S_PLAY, phase = P_RUN, bg_mode = BG_LEVEL, pal_bright_level = 4;
      joypad1 = (pad_t){0, 0, 0};
      for (int f = 0; f < n; f++) {
        uint8_t h = bot_hold[f] ? PAD_A : 0;
        joypad1.press = (uint8_t)(h & ~joypad1.hold), joypad1.release = (uint8_t)(~h & joypad1.hold), joypad1.hold = h;
        game_step();
        game_banks();
        game_draw();
        if (f >= n - 70 && f % 8 == 0) {
          draw_now(), snprintf(p, sizeof p, "%s/b%05d.ppm", argv[3], f), ppm(p);
          printf("%5d scroll %u,%u x %04x y %04x vy %d gm %d spd %d\n", f, (unsigned)scroll_x, scroll_y, player_relx[0], player_rely[0], player_vel_y[0], gamemode, speed);
        }
      }
    }
    return 0;
  }
  if (!strcmp(mode, "seq") && argc > 2) { /* level, frames, every: pictures along a run (OK held now and then) */
    char p[512];
    boot();
    int l = argc > 3 ? atoi(argv[3]) : 0, n = argc > 4 ? atoi(argv[4]) : 600, every = argc > 5 ? atoi(argv[5]) : 60;
    const char *pat = argc > 6 ? argv[6] : "x";
    start_level(l);
    for (int i = 0; i < n; i++) {
      uint16_t k = 0;
      if (pat[0] == 'h') k = PK_OK; /* held */
      else if (pat[0] == 't') k = (i % 30) < 2 ? PK_OK : 0;
      steps(1, k);
      if (i % every == 0) {
        draw_now(), snprintf(p, sizeof p, "%s/f%05d.ppm", argv[2], i), ppm(p);
        printf("%5d phase %d scroll %u,%u x %04x y %04x vy %d gm %d att %d\n", i, phase, (unsigned)scroll_x, scroll_y, player_relx[0],
               player_rely[0], player_vel_y[0], gamemode, attemptCounter[0]);
      }
    }
    return 0;
  }
  if (!strcmp(mode, "test")) {
    test_physics();
    test_run();
    test_practice();
    test_saves();
    test_fuzz(argc > 2 ? atoi(argv[2]) : 200);
  }
  printf("famidash: %d failures\n", fails);
  return fails != 0;
}
