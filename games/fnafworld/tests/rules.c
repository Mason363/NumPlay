/* FNAF World on a computer (make test, with the sanitizers): the numbers against the game's events (gamesrc spec
 * fnafworld_spec.md, fnafworld_spec2.md), battles played to a win by pressing OK, walking the map, random keys
 * everywhere, and the saves. Prints what failed; exits 1 if anything did. Screenshots: build/rules shots DIR. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../src/anims.h"
#include "../src/game.h"
#include "../src/gfx.h"
#include "../src/plat.h"

extern const uint8_t fnafw_data[];
const uint8_t *fnafw_bin;
extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void host_save_dir(const char *d);
static int fails, checks;
#define CHECK(c, ...) (checks++, (c) ? 0 : (fails++, printf("FAIL %d: ", __LINE__), printf(__VA_ARGS__), printf("\n")))
#define B g.b

static void step(uint32_t keys) { game_step(keys); }
static void steps(int n, uint32_t keys) {
  for (int i = 0; i < n; i++) step(keys);
}
static void tap(uint32_t k) { step(k), step(0); }

static void new_game(int mode, int diff) {
  game_init();
  tap(K_OK);             /* the title */
  tap(K_OK);             /* file 1 */
  tap(K_OK);             /* new game */
  tap(K_UP), tap(K_OK);  /* erase: yes */
  for (int i = 1; i < mode; i++) tap(K_DOWN);
  tap(K_OK);
  for (int i = 1; i < diff; i++) tap(K_DOWN);
  tap(K_OK);
}

static void rules(void) {
  CHECK(life_max(1, 0) == 105 && life_max(48, 10) == 390, "life: level * 5 + 100 + character * 5");
  CHECK(exp_needed(1, 0) == 15 && exp_needed(8, 3) == 140, "next: (level^2 + 1) * 10 + character * 5");
  CHECK(enemy_life(1, 1) == 15 && enemy_life(3, 2) == 110 && enemy_life(53, 1) == 14055, "enemy life (10 + t^2 * 5) * diff");
  CHECK(challenger_life(9, 1) == 550 && challenger_life(20, 2) == 5100 && challenger_life(40, 1) == 6100,
        "challenger life (c * 50 + 100) + (c / 20) * 2000 * diff");
  CHECK(victory_exp(3, 2, 0, 0, 1) == 30 && victory_exp(4, 14, 0, 11, 1) == 0, "exp: count * type * 5 * diff");
  CHECK(victory_exp(1, 0, 1, 2, 2) == 600 && victory_exp(1, 0, 14, 0, 1) == 5000 && victory_exp(1, 0, 6, 0, 1) == 0,
        "boss exp (area * 50 + 200) * diff; Security 5000; Gold Endo 0");
  CHECK(victory_tokens(4, 14, 0, 11, 1) == 100 && victory_tokens(1, 0, 6, 0, 2) == 1000, "Goldmine: count * 25; Gold Endo 500 * diff");
  for (int i = 0; i < 200; i++) {
    int32_t t = victory_tokens(3, 4, 0, 5, 1);
    if (t < 15 || t > 30 || (t > 20 && t < 25)) {
      CHECK(0, "tokens count * area + (Random(11)/10)*10 + Random(6): %d", (int)t);
      break;
    }
  }
  CHECK(exp_tick(1) == 1 && exp_tick(12) == 3 && exp_tick(600) == 41 && exp_tick(301) == 11, "exp ticks (1086-1093)");
  CHECK(divisor(false, false, false) == 1 && divisor(true, true, true) == 6, "the hits' divisor");
  int lo = 1000, hi = -1000;
  for (int i = 0; i < 500; i++) {
    int32_t s = enemy_strength(3, 1, 1, 0, 0);
    if (s < lo) lo = s;
    if (s > hi) hi = s;
  }
  CHECK(lo == 12 && hi == 16, "strength type 3: 12 to 16 (%d to %d)", lo, hi);
  CHECK(enemy_strength(3, 2, 2, 5, 1) <= (12 + 4) * 2 / 2 - 5 - 10, "strength: / divisor - block - armor * 10");
  for (int i = 0; i < 300; i++) {
    int c = challenger_pick(0);
    if (c < 9 || c > 14) {
      CHECK(0, "challenger in Fazbear Hills: 9 to 14 (%d)", c);
      break;
    }
  }
  int32_t life, speed, str;
  int sp, an;
  boss_info(1, 1, &life, &speed, &str, &sp, &an);
  CHECK(life == 300 && speed == 100 && str >= 40 && str < 60 && sp == 7 && an == 52, "Auto Chipper (1577)");
  boss_info(15, 2, &life, &speed, &str, &sp, &an);
  CHECK(life == 200000 && speed == 75 && an == 62, "Chipper's Revenge (1596)");
  CHECK(enemy_special(3) == 6 && enemy_special(1) == 0 && enemy_special(25) == 10 && enemy_hit_effect(19) == 11, "specials");
  CHECK(CHAR_ATTACKS[1][0] == 1 && CHAR_ATTACKS[48][2] == 59 && CHAR_ATTACKS[25][1] == 23, "attacks (156-249)");
}

static bool play_battle(int boss, int max_frames) {
  battle_start(boss);
  g.scene = SC_BATTLE;
  for (int i = 0; i < max_frames && g.scene == SC_BATTLE; i++) {
    uint32_t k = B.ready && (i & 1) ? K_OK : 0;
    step(k);
  }
  return g.scene != SC_BATTLE;
}

static void battles(void) {
  new_game(1, 1);
  CHECK(g.scene == SC_PARTY && g.f.mode == 1 && g.f.diff == 1, "a new game: Adventure, Normal, the party next");
  CHECK(g.f.have[1] && g.f.have[8] && !g.f.have[9] && g.f.slot[0] == 1 && g.f.slot[7] == 8, "the first eight");
  tap(K_DIGIT(0));
  CHECK(g.scene == SC_WORLD && g.w.x == 1827 && g.w.y == 1023, "on the map at the start (%d %d)", g.w.x, g.w.y);
  /* walking (1125-1128): 2 pixels a frame */
  int x = g.w.x;
  steps(10, K_LEFT);
  CHECK(g.w.x == x - 20 || g.scene == SC_BATTLE, "walked 20 left (%d)", g.w.x - x);
  /* battles to a win */
  int won = 0, ran = 0;
  uint32_t t0 = g.f.tokens;
  for (int n = 0; n < 12; n++) {
    g.w.area = 0;
    g.w.grace = 0;
    if (play_battle(0, 60 * 120)) won += g.b.won;
    else ran++;
    (void)ran;
    if (g.scene == SC_GAMEOVER) break;
  }
  CHECK(won >= 10, "won %d of 12 battles in Fazbear Hills", won);
  CHECK(g.f.tokens > t0, "tokens won (%u)", (unsigned)g.f.tokens);
  CHECK(g.f.lv[1] > 0, "Freddy levelled up (%d)", g.f.lv[1]);
  /* charges (91-106): never two full in a row */
  battle_start(0);
  g.scene = SC_BATTLE;
  int bad = 0;
  for (int i = 0; i < 60 * 20; i++) {
    step(0);
    int full = 0;
    for (int k = 0; k < 4; k++) full += B.charge[(B.row - 1) * 4 + k] == 10;
    bad += full > 1;
  }
  CHECK(!bad, "one full charge at a time (%d)", bad);
  /* a boss */
  g.scene = SC_WORLD;
  for (int c = 1; c <= 8; c++) g.f.lv[c] = 20;
  CHECK(play_battle(1, 60 * 300) && g.b.won, "Auto Chipper beaten at level 21");
  /* the ends: Normal's at the end of the road (1825), the others after their bosses (1881-1883) */
  g.w.note = 0, g.w.note_t = 0;
  g.scene = SC_WORLD, g.w.x = 2350, g.w.y = 3150, g.f.diff = 1;
  step(0);
  CHECK(g.scene == SC_ENDING && g.ending == 1, "Normal: the end of the road is the end (%d)", g.scene);
  for (int i = 0; i < 6000 && g.scene == SC_ENDING; i++) step(i % 8 == 0 ? K_OK : 0);
  CHECK(g.scene == SC_TITLE, "the ending's lines, then the title");
  g.scene = SC_WORLD, g.w.x = 2350, g.w.y = 3150, g.f.diff = 2;
  step(0);
  CHECK(g.scene == SC_WORLD && g.w.final && g.w.x == 1326, "Hard: on to the last");
  battle_start(15);
  B.won = 1, B.on = 0;
  g.scene = SC_WORLD;
  world_after_battle();
  CHECK(g.scene == SC_ENDING && g.ending == 3 && g.f.beat[3], "Chipper's Revenge: its ending");
  g.scene = SC_WORLD;
  /* every attack, at least once */
  for (int a = 1; a <= 60; a++) {
    battle_start(0);
    g.scene = SC_BATTLE;
    steps(60, 0);
    B.att_slot = 1, B.used = (uint8_t)a, B.att_an = 0, B.att_img = 0, B.cur_level = 5;
    steps(400, 0);
  }
  CHECK(1, "every attack played");
}

static void fuzz(void) {
  srand(7);
  static const uint32_t keys[] = {0, K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_EXE, K_BACK, K_DEL, K_DIGIT(0), K_DIGIT(1),
                                  K_DIGIT(2), K_DIGIT(3), K_LEFT | K_UP, K_RIGHT | K_DOWN};
  for (int run = 0; run < 40; run++) {
    new_game(1 + run % 2, 1 + (run / 2) % 2);
    tap(K_DIGIT(0));
    for (int i = 0; i < 60 * 60 && g.running; i++) {
      uint32_t k = keys[rand() % (int)(sizeof keys / sizeof keys[0])];
      if (g.scene == SC_PAUSE && (k & (K_OK | K_EXE)) && g.menu == 7) k = 0;   /* (not quitting) */
      int hold = 1 + rand() % 20;
      for (int h = 0; h < hold; h++) step(k);
      if (i % 300 == 0) game_draw();
    }
  }
  CHECK(1, "random keys");
}

static void saves(void) {
  new_game(1, 2);
  tap(K_DIGIT(0));
  g.f.tokens = 1234, g.f.lv[3] = 7, g.f.chest[5] = 1, g.f.chip[0] = 5;
  g.w.x = 2000, g.w.y = 1500;
  world_save_pos();
  save_file();
  File f;
  CHECK(load_file(1, &f) && f.tokens == 1234 && f.lv[3] == 7 && f.chest[5] && f.chip[0] == 5 && f.x == 2000 && f.diff == 2,
        "a file comes back");
  plat_save("fnafw2.sav", "garbage", 7);
  CHECK(!load_file(2, &f), "a bad file is not loaded");
  plat_remove("fnafw1.sav");
  save_restore_copy();
  CHECK(load_file(1, &f) && f.tokens == 1234, "the file back from the script's copy");
}

static void shots(const char *dir) {
  char p[512];
  new_game(1, 1);
  game_init();
  game_draw();
  snprintf(p, sizeof p, "%s/title.ppm", dir), host_shot(p);
  new_game(1, 1);
  game_draw();
  snprintf(p, sizeof p, "%s/party.ppm", dir), host_shot(p);
  tap(K_DIGIT(0));
  steps(30, K_RIGHT);
  game_draw();
  snprintf(p, sizeof p, "%s/map.ppm", dir), host_shot(p);
  battle_start(0);
  g.scene = SC_BATTLE;
  for (int i = 0; i < 400 && !B.ready; i++) step(0);
  game_draw();
  snprintf(p, sizeof p, "%s/battle.ppm", dir), host_shot(p);
  tap(K_OK);
  steps(16, 0);
  game_draw();
  snprintf(p, sizeof p, "%s/attack.ppm", dir), host_shot(p);
  static const int16_t spots[][2] = {{2274, 700}, {1924, 560}, {1774, 950}, {1574, 1700}, {3000, 1000}, {1000, 3000},
                                     {2500, 3500}, {3400, 2300}, {5300, 3000}};
  for (unsigned i = 0; i < sizeof spots / sizeof spots[0]; i++) {
    g.scene = SC_WORLD;
    g.w.x = spots[i][0], g.w.y = spots[i][1];
    game_draw();
    snprintf(p, sizeof p, "%s/map%u.ppm", dir, i), host_shot(p);
  }
  g.w.area = 8;
  battle_start(2);
  g.scene = SC_BATTLE;
  steps(90, 0);
  game_draw();
  snprintf(p, sizeof p, "%s/boss.ppm", dir), host_shot(p);
  /* a win: the victory, the levels */
  g.w.area = 0;
  battle_start(0);
  g.scene = SC_BATTLE;
  for (int i = 0; i < 60 * 60 && B.stage < 2; i++) step(B.ready && (i & 1) ? K_OK : 0);
  steps(70, 0);
  game_draw();
  snprintf(p, sizeof p, "%s/victory.ppm", dir), host_shot(p);
  /* attacks in the middle */
  static const uint8_t att[] = {6, 9, 12, 20, 30, 36, 44, 47, 56};
  for (unsigned i = 0; i < sizeof att; i++) {
    battle_start(0);
    g.scene = SC_BATTLE;
    for (int k = 0; k < 4; k++) B.en[k].alive = k < 3;
    steps(80, 0);
    B.ready = 0;
    B.att_slot = 1, B.used = att[i], B.att_an = AN_ATTACK1, B.att_img = 0, B.att_cnt = 0, B.cur_level = 5;
    steps(30, 0);
    game_draw();
    snprintf(p, sizeof p, "%s/att%u.ppm", dir, i), host_shot(p);
  }
  /* the menus */
  g.scene = SC_PAUSE, g.back = SC_WORLD, g.menu = 0;
  game_draw();
  snprintf(p, sizeof p, "%s/pause.ppm", dir), host_shot(p);
  g.f.tokens = 500;
  g.scene = SC_SHOP, g.shop_kind = 1, g.menu = 1;
  game_draw();
  snprintf(p, sizeof p, "%s/shop.ppm", dir), host_shot(p);
  for (int c = 1; c <= 21; c++) g.f.chest[c] = c % 3 != 0;
  g.f.chip[0] = 1, g.f.chip[1] = 5;
  g.scene = SC_CHIPS, g.menu = 4;
  game_draw();
  snprintf(p, sizeof p, "%s/chips.ppm", dir), host_shot(p);
}

int main(int argc, char **argv) {
  static uint16_t band[SCREEN_W * BAND_H];
  gfx_set_band_memory(band);
  fnafw_bin = fnafw_data;
  host_save_dir("build/host-saves");
  system("mkdir -p build/host-saves");
  if (argc > 2 && !strcmp(argv[1], "shots")) {
    shots(argv[2]);
    return 0;
  }
  rules();
  battles();
  saves();
  fuzz();
  printf("%d checks, %d failed\n", checks, fails);
  return fails ? 1 : 0;
}
