/* Five Nights at Freddy's 3 on a computer (make test, with the sanitizers): the night's rules against the game's
 * events (gamesrc spec fnaf3_spec.md), whole nights played by a bot that watches, lures, seals and reboots, random
 * keys on every night, and the saves. Prints what failed; exits 1 if anything did. Screenshots: build/rules shots
 * DIR. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/plat.h"

extern const uint8_t fnaf3_data[];
const uint8_t *fnaf3_bin;
extern uint32_t host_keys;
void host_shot(const char *path);
void host_save_dir(const char *d);
static int fails, checks;
#define CHECK(c, ...) (checks++, (c) ? 0 : (fails++, printf("FAIL %d: ", __LINE__), printf(__VA_ARGS__), printf("\n")))
#define N g.n

static void step(uint32_t keys) { game_step(keys); }
static void steps(int n, uint32_t keys) {
  for (int i = 0; i < n; i++) step(keys);
}
static void tap(uint32_t k) { step(k), step(0); }

static void night(int n) {
  start_night(n);
  while (g.scene == SC_DAY) step(0);
}

static void rules(void) {
  /* night 1: no Springtrap, no errors however long you look away */
  night(1);
  steps(60 * 100, 0);
  CHECK(N.loc == L_OFF && spring_cam() == 0, "night 1: Springtrap off the map (%d)", N.loc);
  CHECK(N.vent_h > -10 && N.audio > -10 && N.camera > -10, "night 1: no error");
  CHECK(N.ai == 0 && N.tlimit == 100, "night 1: AI 0, time limit 100");
  /* night 1 lasts 3 minutes: 6 AM at 180 s */
  night(1);
  steps(60 * 179, 0);
  CHECK(g.scene == SC_NIGHT && N.hour == 5, "night 1: 5 AM at 179 s (hour %d)", N.hour);
  steps(70, 0);
  CHECK(g.scene == SC_SIX, "night 1: 6 AM after 3 minutes (scene %d)", g.scene);
  /* nights 2 on: he starts in CAM 06 to 10 */
  for (int s = 0; s < 40; s++) {
    night(2);
    CHECK(N.loc >= 6 && N.loc <= 10, "night 2: starts at 06..10 (%d)", N.loc);
  }
  /* AI by night (events 492-497) */
  static const uint8_t ai[7] = {0, 0, 2, 3, 4, 5, 7};
  for (int n = 1; n <= 6; n++) {
    night(n);
    step(0);
    CHECK(N.ai == ai[n], "night %d: AI %d (%d)", n, ai[n], N.ai);
  }
  /* the lure: CAM 06 pulls him from 05 or 07, the counter refills in 9 s */
  night(3);
  steps(60 * 10, 0);
  N.loc = 5, N.lure_to = 0, N.action = 0;
  tap(K_OK);   /* (the view pans right, then the monitor comes up) */
  while (N.viewing < 2) step(0);
  tap(K_DIGIT(6));
  CHECK(N.cam == 6, "the monitor: 6 picks CAM 06 (%d)", N.cam);
  int pulled = 0, tries = 0;
  for (int k = 0; k < 140; k++) {
    N.loc = 5, N.lure_to = 0, N.play = 7, N.action = 0, N.audio = 0;
    tap(K_OK);
    tries++;
    pulled += N.lure_to == 6;
  }
  CHECK(pulled > tries * 70 / 100 && pulled < tries * 95 / 100, "the lure works 6 times in 7 (%d of %d)", pulled, tries);
  N.loc = 9, N.lure_to = 0, N.play = 7;
  tap(K_OK);
  CHECK(N.lure_to == 0, "a lure on CAM 06 doesn't reach CAM 09");
  CHECK(N.play == 1, "the lure's counter starts again");
  steps(9 * 60 + 2, 0);
  CHECK(N.play == 7, "the lure is ready again after 9 s (%d)", N.play);
  /* audio: AI per play, an error at -10 */
  night(2);
  step(0);
  N.x = 800;
  tap(K_OK);
  while (N.viewing < 2) step(0);
  int plays = 0;
  while (N.audio > -10 && plays < 20) N.play = 7, tap(K_OK), plays++;
  CHECK(plays == 5, "night 2: 5 lures before the audio error (%d)", plays);
  /* sealing: a vent camera on the vent map; he goes back out of a sealed vent */
  tap(K_DEL);
  CHECK(N.vents_map, "backspace shows the vent map");
  tap(K_DIGIT(3));
  CHECK(N.vent == 13, "3 on the vent map is CAM 13 (%d)", N.vent);
  tap(K_OK);
  CHECK(N.sealing == 13, "OK seals CAM 13");
  steps(110, 0);
  CHECK(N.closed == 13 && !N.sealing, "CAM 13 sealed in under 1.7 s (%d)", N.closed);
  N.loc = 13, N.action = 2;
  step(0);
  CHECK(N.loc == 5, "out of the sealed CAM 13: back to CAM 05 (%d)", N.loc);
  N.closed = 0, N.loc = 13, N.action = 2;
  step(0);
  CHECK(N.loc == L_ST1, "out of an open CAM 13: the window (%d)", N.loc);
  /* the panel: reboot all takes 10 to 20 s and clears the three */
  night(4);
  step(0);
  N.audio = N.camera = N.vent_h = -10;
  tap(K_DOWN);
  steps(40, 0);
  CHECK(N.panel == 3, "Down brings the panel up (%d)", N.panel);
  tap(K_DOWN), tap(K_DOWN), tap(K_DOWN);
  CHECK(N.cursor == 4, "the cursor on reboot all (%d)", N.cursor);
  tap(K_OK);
  int t = 0;
  while (N.rebooting && t < 60 * 30) step(0), t++;
  CHECK(t >= 60 * 9 && t <= 60 * 21, "reboot all: %d frames", t);
  CHECK(N.audio == 0 && N.camera == 0 && N.vent_h == 0, "reboot all clears the three");
  /* ventilation error: the blackout moves him on at its peak */
  night(5);
  step(0);
  N.loc = L_ST1, N.vent_h = -10;
  int moved = 0;
  for (int k = 0; k < 60 * 30 && !moved; k++) step(0), moved = N.loc != L_ST1;
  CHECK(moved, "a ventilation error's blackout brings him from the window");
  /* at the door with a screen up: he gets you */
  night(5);
  step(0);
  N.loc = L_ST4, N.action = 2;
  N.x = 400;
  tap(K_DOWN);
  steps(60, 0);
  CHECK(g.scene == SC_STATIC || N.got_you, "at the door, the panel up: the lunge");
  steps(60, 0);
  CHECK(g.scene == SC_STATIC || g.scene == SC_OVER, "then the static (%d)", g.scene);
}

/* a bot: watches him, lures him away, keeps the systems up */
static uint32_t bot_keys(void) {
  static int t;
  t++;
  if (g.scene != SC_NIGHT || N.got_you) return t & 1 ? K_OK : 0;
  if (N.panel >= 2) {
    if (N.rebooting) return 0;
    if (N.audio > -10 && N.camera > -10 && N.vent_h > -10) return t & 1 ? K_EXE : 0;
    if (N.cursor != 4) return t & 1 ? K_DOWN : 0;
    return t & 1 ? K_OK : 0;
  }
  if (N.viewing >= 2) {
    if (N.vent_h <= -10 || N.camera <= -10 || (N.audio <= -10 && N.loc <= 5)) return t & 1 ? K_EXE : 0;
    if (N.bb_shown || N.mangle == 2 || N.puppet == 2 || N.chica == 2) return (t & 1) ? K_DIGIT(5) : 0;
    int s = spring_cam(), c = N.cam;
    static const uint8_t away[16] = {0, 0, 5, 2, 3, 6, 7, 8, 9, 10, 9, 0, 0, 0, 0, 0};
    if (s && s <= 10 && away[s] && c != away[s]) return t & 1 ? K_DIGIT(away[s] % 10) : 0;
    if (s && s <= 10 && away[s] && N.play == 7) return t & 1 ? K_OK : 0;
    return 0;
  }
  if (N.vent_h <= -10 || N.camera <= -10 || N.audio <= -10) return t & 1 ? K_DOWN : 0;
  return t & 1 ? K_OK : 0;
}

static void bot_nights(void) {
  int won = 0, lost = 0;
  for (int n = 1; n <= 6; n++)
    for (int s = 0; s < 3; s++) {
      night(n);
      int t = 0;
      while (g.scene == SC_NIGHT && t < 60 * 60 * 7) step(bot_keys()), t++;
      if (g.scene == SC_SIX) won++;
      else lost++;
    }
  printf("bot: %d nights won, %d lost\n", won, lost);
  CHECK(won >= 3, "the bot wins some nights (%d)", won);
}

static void fuzz(int sessions) {
  uint32_t r = 7;
  for (int s = 0; s < sessions; s++) {
    night(1 + s % 6);
    for (int f = 0; f < 60 * 120 && g.scene == SC_NIGHT; f++) {
      r = r * 1103515245u + 12345u;
      static const uint32_t keys[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_EXE, K_DEL, K_DIGIT(1), K_DIGIT(5),
                                      K_DIGIT(0), K_DIGIT(3), 0};
      uint32_t k = keys[(r >> 16) % 12];
      if (k == 0 || (r >> 8) % 3) step(0);
      else step(k);
    }
  }
}

static void saves(void) {
  night(3);
  steps(60 * 30, 0);
  Night keep = g.n;
  save_resume();
  memset(&g.n, 0, sizeof g.n);
  CHECK(save_resume_load() && g.n.night == 3 && g.n.frame == keep.frame && g.n.loc == keep.loc, "the night kept by Home comes back");
  CHECK(!save_resume_load(), "and only once");
  g.level = 4, g.beatgame = 1, g.beat6 = 0, g.goodend = 0;
  save_progress();
  g.level = 1, g.beatgame = 0;
  save_load();
  CHECK(g.level == 4 && g.beatgame == 1, "the progress comes back");
}

static void shot(const char *dir, const char *name) {
  char p[256];
  game_draw();
  snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
  host_shot(p);
}

static void draw_all(const char *dir) {
  night(5);
  steps(60, 0);
  N.loc = L_ST1, N.window = 1, N.x = 760, N.hour = 3;
  shot(dir, "shot");
  night(5);
  steps(60, 0);
  N.x = 640, N.scare = PH_BB, N.scare_t = 36, N.hour = 2;
  shot(dir, "bb");
  night(5);
  steps(60, 0);
  N.x = 800, N.cam = 7, N.hour = 4;
  tap(K_OK);
  steps(30, 0);
  N.loc = 7;
  step(0);
  shot(dir, "monitor");
  tap(K_DEL);
  steps(2, 0);
  N.closed = 12;
  step(0);
  shot(dir, "vents");
  night(5);
  steps(60, 0);
  N.vent_h = -10, N.camera = -10;
  tap(K_DOWN);
  steps(60, 0);
  shot(dir, "panel");
}

int main(int argc, char **argv) {
  static uint16_t band[SCREEN_W * BAND_H];
  gfx_set_band_memory(band);
  fnaf3_bin = fnaf3_data;
  host_save_dir("build");
  game_init();
  if (argc > 2 && !strcmp(argv[1], "shots")) {
    draw_all(argv[2]);
    return 0;
  }
  rules();
  bot_nights();
  fuzz(60);
  saves();
  printf("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
