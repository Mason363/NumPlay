/* Five Nights at Freddy's 4 on a computer (make test, with the sanitizers): the night's rules against the game's
 * events (gamesrc spec level_events.txt), whole nights played by a bot that listens, holds the doors and lights the
 * bed, Fun with Plushtrap, random keys on every night, and the saves. Prints what failed; exits 1 if anything did.
 * Screenshots: build/rules shots DIR. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/plat.h"

extern const uint8_t fnaf4_data[];
const uint8_t *fnaf4_bin;
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

static void night(int n, int shadow) {
  start_night(n, shadow);
  while (g.scene == SC_CARD) step(0);
}

/* to a door (-1 left, 1 right) or the closet (0), or the bed (2): back in the bedroom first */
static void go_to(int where) {
  for (int i = 0; i < 400 && N.s0 != 0; i++) step(0);
  if (where == 2) {
    tap(K_DOWN);
    for (int i = 0; i < 200 && N.s0 != 43; i++) step(0);
    return;
  }
  if (where) {
    uint32_t k = where < 0 ? K_LEFT : K_RIGHT;
    for (int i = 0; i < 200 && N.s0 != (where < 0 ? 2 : 5); i++) step(k);
  }
  tap(K_OK);
  for (int i = 0; i < 400 && N.s0 != (where < 0 ? 10 : where > 0 ? 17 : 29); i++) step(0);
}
static void back(void) {
  tap(K_DOWN);
  for (int i = 0; i < 400 && N.s0 != 0; i++) step(0);
}

static void rules(void) {
  /* the hours: a minute each, 6 AM after six */
  night(1, 0);
  CHECK(N.hour == 12, "12 AM to start");
  steps(60 * 60, 0);
  CHECK(N.hour == 1, "1 AM after a minute (%d)", N.hour);
  CHECK(!N.ai_bonnie && !N.ai_chica && !N.ai_freddy && !N.ai_foxy, "night 1: nobody before 2 AM");
  steps(60 * 60, 0);
  CHECK(N.ai_bonnie == 1 && N.ai_chica == 1 && N.ai_freddy == 1, "night 1 at 2 AM: 1, 1, 1");
  steps(60 * 60, 0);
  CHECK(N.ai_bonnie == 3 && N.ai_chica == 2 && N.ai_freddy == 2, "night 1 at 3 AM: 3, 2, 2 (%d %d %d)", N.ai_bonnie,
        N.ai_chica, N.ai_freddy);
  /* the AIs by night (373 to 392) */
  static const uint8_t ai[9][5] = {{0}, {0}, {5, 5, 1, 2, 0}, {7, 7, 10, 3, 0}, {10, 10, 5, 4, 0}, {0, 0, 0, 0, 12},
                                   {12, 12, 10, 5, 0}, {15, 15, 15, 6, 0}, {20, 20, 20, 6, 0}};
  for (int n = 2; n <= 8; n++) {
    night(n <= 6 ? n : 7, n <= 6 ? 0 : n - 6);
    step(0);
    uint8_t got[5] = {N.ai_bonnie, N.ai_chica, N.ai_foxy, N.ai_freddy, N.ai_fredbear};
    CHECK(!memcmp(got, ai[n], 5) && N.night == n, "night %d: AI %d %d %d %d %d", n, got[0], got[1], got[2], got[3], got[4]);
  }
  night(6, 0);
  N.hour = 4;
  step(0);
  CHECK(!N.ai_bonnie && !N.ai_chica && !N.ai_foxy && !N.ai_freddy && N.ai_fredbear == 15, "night 6 at 4 AM: Fredbear alone");
  /* looking: 10 a step, the view turns to a door at the edge */
  night(1, 0);
  steps(11, K_LEFT);   /* (the first step looks ahead again: 457) */
  CHECK(N.x == 400, "left: x 400 in 10 steps (%d)", N.x);
  steps(10, K_LEFT);
  CHECK(N.s0 == 2, "the view turns to the left door (%d)", N.s0);
  /* to the door: the dark, the run, the door coming up (15 + 40 + 35 frames) */
  tap(K_OK);
  int t = 0;
  while (N.s0 != 10 && t < 300) step(0), t++;
  CHECK(N.s0 == 10 && t >= 80 && t <= 100, "to the left door in %d steps", t);
  step(0);
  CHECK(N.listening == 1, "at the door with no light: listening");
  /* breathing when Bonnie stands at the door */
  N.ai_bonnie = 1;
  step(0);
  N.bonnie.loc = P_LHN;
  step(0);
  CHECK(g.hold && !strcmp(g.hold, "Breathing"), "breathing");
  /* the light on her there: she gets you */
  steps(4, K_OK);
  CHECK(N.gameover && N.ob[O_JUMP].alive, "the light on Bonnie at the door: the scare");
  /* the light on her far down the hall sends her away */
  night(3, 0);
  go_to(-1);
  N.bonnie.loc = P_LHF;
  steps(4, K_OK);
  CHECK(N.bonnie.loc == P_LRC && !N.gameover, "the light sends Bonnie back (%d)", N.bonnie.loc);
  steps(60, 0);
  CHECK(N.s0 == 10, "and you are at the door again (%d)", N.s0);
  /* the door held on her at it: she leaves in 3 seconds, nobody comes until it opens */
  N.bonnie.loc = P_LHN, N.bonnie.s5 = 2, N.door_lock = 0;
  steps(60 * 4, K_UP);
  CHECK(N.bonnie.loc == P_LRL && N.door_lock == 2, "holding the door: Bonnie goes (%d)", N.bonnie.loc);
  steps(60, 0);
  CHECK(N.door_lock == 0, "the door open: they move again");
  /* Foxy: in the left hall when you are at the right door, he runs into the closet */
  night(3, 0);
  go_to(1);
  N.foxy = P_LHF;
  step(0);
  CHECK(N.foxy == P_CLOSET, "Foxy runs into the closet (%d)", N.foxy);
  /* the closet held shut takes him back a step a second */
  back();
  go_to(0);
  N.ai_foxy = 1, N.foxy = P_CLOSET;
  step(0);
  N.foxy_s2 = 8;
  steps(30, K_UP);
  int s2 = N.foxy_s2;
  steps(60 * 3, K_UP);
  CHECK(s2 - N.foxy_s2 >= 2 && s2 - N.foxy_s2 <= 3, "the closet held: %d to %d in 3 s", s2, N.foxy_s2);
  /* the Freddles: the AI every 4 s while you don't look, the light takes 20 a second */
  night(4, 0);
  N.ai_bonnie = N.ai_chica = N.ai_foxy = 0;
  N.freddy = 0;
  steps(60 * 8 + 1, 0);
  CHECK(N.freddy == 8, "Freddles: 4 in 4 s, twice (%d)", N.freddy);
  go_to(2);
  N.freddy = 40;
  steps(60, K_OK);
  CHECK(N.freddy >= 19 && N.freddy <= 22, "the light on the bed: 40 to 20 in 1 s (%d)", N.freddy);
  N.freddy = 70;
  steps(4, K_OK);
  CHECK(N.gameover, "70 Freddles while you look: the scare");
  /* Fredbear on the 5th night */
  night(5, 0);
  step(0);
  CHECK(N.fred == P_LRC, "night 5: Fredbear in the living room (%d)", N.fred);
  /* 6 AM */
  night(2, 0);
  N.hour = 5;
  N.every[32] = 1;
  steps(2, 0);
  CHECK(g.scene == SC_SIX, "6 AM ends the night (%d)", g.scene);
  CHECK(g.level == 3, "and the next is saved (%d)", g.level);
}

/* a bot: listens at each door, holds it on breathing or lights the hall, keeps Foxy back and the bed clear */
typedef struct { int step, t, plan; } Bot;
static uint32_t bot_keys(Bot *b) {
  static const int8_t round[6] = {-1, 2, 1, 2, 0, 2};
  b->t++;
  if (g.scene != SC_NIGHT) return b->t & 1 ? K_OK : 0;
  if (b->step == 9) {   /* leaving: Down, the light off */
    if (N.s0 == 10 || N.s0 == 17 || N.s0 == 29 || N.s0 == 43) return b->t & 1 ? K_DOWN : 0;
    b->step = 0, b->plan++, b->t = 0;
  }
  switch (N.s0) {
  case 0: {
    int w = round[b->plan % 6];
    if (b->t < 4) return 0;
    if (w == 2) return b->t & 1 ? K_DOWN : 0;
    if (w == 0) return b->t & 1 ? K_OK : 0;
    return w < 0 ? K_LEFT : K_RIGHT;
  }
  case 2: return round[b->plan % 6] < 0 ? (b->t & 1 ? K_OK : 0) : K_RIGHT;
  case 5: return round[b->plan % 6] > 0 ? (b->t & 1 ? K_OK : 0) : K_LEFT;
  case 10: case 17: case 20: case 21: case 22: case 23: case 24: case 25: {
    bool breath = g.hold && !strcmp(g.hold, "Breathing");
    if (b->step == 0) b->step = 1, b->t = 0;
    if (b->step == 1) {   /* listen */
      if (breath) b->step = 2, b->t = 0;
      else if (b->t > 40) b->step = 3, b->t = 0;
      return 0;
    }
    if (b->step == 2) {   /* hold it until the steps go */
      if (b->t < 60 * 4) return K_UP;
      b->step = 1, b->t = 0;
      return 0;
    }
    if (b->step == 3) {   /* the light down the hall */
      if (b->t < 20) return K_OK;
      b->step = (N.fred == P_LHF && N.s0 == 10) || (N.fred == P_RHF && N.s0 == 17) ? 5 : 9, b->t = 0;
      return 0;
    }
    if (b->step == 5) {   /* Fredbear in the hall: the door held until he goes */
      if ((N.fred == P_LHF || N.fred == P_RHF) && b->t < 60 * 7) return K_UP;
      b->step = 9, b->t = 0;
      return 0;
    }
    return 0;
  }
  case 29: case 32: case 33: case 34:
    if (b->step == 0) b->step = 1, b->t = 0;
    if (b->step == 1) {
      if (b->t < 12) return K_OK;
      b->step = (N.foxy == P_CLOSET && N.foxy_s2 >= 2) || N.fred == P_CLOSET ? 2 : 3, b->t = 0;
      return 0;
    }
    if (b->step == 2) {
      if ((N.foxy == P_CLOSET && N.foxy_s2 > 1 && b->t < 60 * 8) || (N.fred == P_CLOSET && b->t < 60 * 7)) return K_UP;
      b->step = 9, b->t = 0;
      return 0;
    }
    b->step = 9, b->t = 0;
    return 0;
  case 43:
    if (b->step == 0) b->step = 1, b->t = 0;
    if (b->t < 6 || (N.freddy > 5 && b->t < 60 * 3) || (N.fred == P_BED && b->t < 60 * 3)) return K_OK;
    b->step = 9, b->t = 0;
    return 0;
  }
  b->step = 0;
  return 0;
}

static void bot_nights(void) {
  int won[9] = {0}, total = 0;
  for (int n = 1; n <= 6; n++)
    for (int s = 0; s < 3; s++) {
      night(n, 0);
      Bot b = {0};
      int t = 0;
      while (g.scene == SC_NIGHT && t < 60 * 60 * 7) {
        step(bot_keys(&b)), t++;
        if (getenv("BOT") && n == atoi(getenv("BOT")) && t % 60 == 0)
          printf("%3d s0 %2d plan %d step %d freddy %d bonnie %d/%d chica %d/%d foxy %d/%d hold %s\n", t / 60, N.s0, b.plan,
                 b.step, N.freddy, N.bonnie.loc, N.bonnie.s6, N.chica.loc, N.chica.s6, N.foxy, N.foxy_s2, g.hold ? g.hold : "");
      }
      if (g.scene == SC_SIX) won[n]++, total++;
      else if (getenv("BOT")) printf("night %d lost at %d:%02d, scene %d, s0 %d, top %d\n", n, t / 3600, t / 60 % 60, g.scene, N.s0, night_top());
    }
  printf("bot: nights won %d %d %d %d %d %d of 3 each\n", won[1], won[2], won[3], won[4], won[5], won[6]);
  CHECK(won[1] >= 2 && total >= 5, "the bot wins nights (%d)", total);
}

static void plushtrap(void) {
  /* the light on him on the X: GREAT!, the next night from 2 AM */
  g.n.night = 2;
  plush_begin(false);
  g.scene = SC_PLUSH;
  g.p.loc = 7, g.p.active = 300;
  step(K_OK);
  CHECK(g.p.s0 == 3, "the light on him on the X: caught (%d)", g.p.s0);
  int t = 0;
  while (g.scene == SC_PLUSH && t < 60 * 6) step(K_OK), t++;
  CHECK(g.reward == 1 && g.scene == SC_CARD, "GREAT!, then the next night (%d)", g.scene);
  while (g.scene == SC_CARD) step(0);
  step(0);
  CHECK(N.hour == 2, "it starts at 2 AM (%d)", N.hour);
  /* the time running out: TOO BAD. */
  g.n.night = 4;
  plush_begin(false);
  g.scene = SC_PLUSH;
  CHECK(g.p.counter == 30, "night 4: 30 seconds (%d)", g.p.counter);
  t = 0;
  while (g.scene == SC_PLUSH && t < 60 * 40) step(0), t++;
  CHECK(g.scene == SC_TOOBAD, "too late: TOO BAD. (%d)", g.scene);
  /* at the hall's start the light sends him to his chair */
  plush_begin(false);
  g.scene = SC_PLUSH;
  g.p.loc = 1;
  steps(3, K_OK);
  CHECK(g.p.loc == 0, "back to the chair (%d)", g.p.loc);
}

static void fuzz(int sessions) {
  uint32_t r = 7;
  for (int s = 0; s < sessions; s++) {
    night(1 + s % 6, s % 7 == 6 ? 1 + s % 2 : 0);
    for (int f = 0; f < 60 * 120 && g.scene == SC_NIGHT; f++) {
      r = r * 1103515245u + 12345u;
      static const uint32_t keys[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_EXE, K_OK, K_UP, K_DIGIT(5), 0, 0, 0};
      uint32_t k = keys[(r >> 16) % 12];
      if (k == 0 || (r >> 8) % 3) step(0);
      else steps((int)((r >> 4) % 30), k);
    }
  }
  /* and every scene, for its keys */
  for (int s = 0; s < 40; s++) {
    g.scene = SC_TITLE;
    for (int f = 0; f < 60 * 30; f++) {
      r = r * 1103515245u + 12345u;
      static const uint32_t keys[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_BACK, 0, 0};
      uint32_t k = keys[(r >> 16) % 8];
      step((r >> 8) % 4 ? 0 : k);
      if (!g.running) g.running = true, g.scene = SC_TITLE;
    }
  }
}

static void saves(void) {
  night(3, 0);
  steps(60 * 30, 0);
  Night keep = g.n;
  save_resume();
  memset(&g.n, 0, sizeof g.n);
  CHECK(save_resume_load() && g.n.night == 3 && g.n.frame == keep.frame && !memcmp(&g.n, &keep, sizeof keep),
        "the night kept by Home comes back");
  CHECK(!save_resume_load(), "and only once");
  g.level = 4, g.beat5 = 1, g.beat6 = 0, g.beat7 = 1;
  save_progress();
  g.level = 1, g.beat5 = 0;
  save_load();
  CHECK(g.level == 4 && g.beat5 == 1 && g.beat7 == 1, "the progress comes back");
}

static void shot(const char *dir, const char *name) {
  char p[256];
  game_draw();
  snprintf(p, sizeof p, "%s/%s.ppm", dir, name);
  host_shot(p);
}

static void draw_all(const char *dir) {
  g.scene = SC_TITLE, g.beat5 = 1, g.beat6 = 1;
  shot(dir, "title");
  night(3, 0);
  steps(60, 0);
  N.hour = 2;
  shot(dir, "shot");
  night(5, 0);
  steps(60, 0);
  N.hour = 2;
  go_to(-1);
  N.fred = P_LHF;
  steps(3, K_OK);
  shot(dir, "door");
  night(3, 0);
  steps(60, 0);
  N.hour = 3, N.ai_foxy = 1, N.foxy = P_CLOSET;
  step(0);
  N.foxy_s2 = 6;
  go_to(0);
  steps(3, K_OK);
  shot(dir, "closet");
  night(4, 0);
  steps(60, 0);
  N.hour = 4, N.ai_freddy = 0;
  go_to(2);
  N.freddy = 35;
  steps(2, K_OK);
  shot(dir, "bed");
  g.n.night = 2;
  plush_begin(false);
  g.scene = SC_PLUSH;
  g.p.loc = 0;
  steps(8, K_OK);
  shot(dir, "plushtrap");
}

int main(int argc, char **argv) {
  static uint16_t band[SCREEN_W * BAND_H];
  gfx_set_band_memory(band);
  fnaf4_bin = fnaf4_data;
  host_save_dir("build");
  game_init();
  if (argc > 2 && !strcmp(argv[1], "shots")) {
    draw_all(argv[2]);
    return 0;
  }
  rules();
  plushtrap();
  bot_nights();
  fuzz(60);
  saves();
  printf("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
