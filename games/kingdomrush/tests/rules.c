/* Kingdom Rush on a computer: the rules against the game's numbers (GameSettings, Level*.as), stage 1 won by
 * script, every stage and mode played by a simple general, random keys through every screen, the save.
 *
 *   build/rules            all the tests
 *   build/rules shots DIR  the screens as PPM pictures in DIR
 *   build/rules sizes      the battle's memory */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "anim.h"
#include "game.h"
#include "gfx.h"
#include "kr.h"
#include "plat.h"
#include "settings.h"
#include "special.h"

extern const uint8_t kr_data[];
const uint8_t *kr_bin;
extern uint32_t host_keys, host_time;
extern bool host_storage_full;
void host_shot(const char *path);
void host_save_dir(const char *d);
static uint16_t band[SCREEN_W * BAND_H];

static int fails, checks;
#define CHECK(c, ...)                                     \
  do {                                                    \
    checks++;                                             \
    if (!(c)) {                                           \
      fails++;                                            \
      fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
      fprintf(stderr, __VA_ARGS__);                       \
      fprintf(stderr, "\n");                              \
    }                                                     \
  } while (0)

static void step(uint32_t keys) {
  game_step(keys);
  host_time += 33;
}
static void press(uint32_t k) {
  step(k);
  step(0);
}

/* ------------------------------------------------------------------ the numbers */
static void test_numbers(void) {
  memset(upgrades, 0, sizeof upgrades);
  battle_start(0, MODE_CAMPAIGN);
  /* GameSettings: towers */
  TowerStat a = tower_stat(TW_ARCHER, 1);
  CHECK(a.cost == 70 && a.range == 220 && a.min_dmg == 4 && a.max_dmg == 6 && a.reload == 24, "archers level 1");
  a = tower_stat(TW_MAGE, 3);
  CHECK(a.cost == 240 && a.range == 280 && a.min_dmg == 40 && a.max_dmg == 74 && a.reload == 45, "mages level 3");
  a = tower_stat(TW_ENGINEER, 2);
  CHECK(a.cost == 220 && a.area == 70 && a.min_dmg == 20 && a.max_dmg == 39 && a.reload == 90, "artillery level 2");
  a = tower_stat(TW_TESLA, 4);
  CHECK(a.cost == 375 && a.range == 260 && a.min_dmg == 55 && a.max_dmg == 105, "tesla");
  BarrackStat b = barrack_stat(TW_BARRACKS, 3);
  CHECK(b.cost == 160 && b.health == 150 && b.armor == 30 && b.min_dmg == 6 && b.max_dmg == 10 && b.respawn == 300, "knights");
  b = barrack_stat(TW_PALADIN, 4);
  CHECK(b.health == 200 && b.armor == 50 && b.respawn == 420, "paladins");
  /* enemies */
  CHECK(enemy_stats[EN_GOBLIN].health == 20 && enemy_stats[EN_GOBLIN].gold == 3 && fabsf(enemy_stats[EN_GOBLIN].speed - 1.2f) < 1e-6f, "goblin");
  CHECK(enemy_stats[EN_FATORC].armor == 30 && enemy_stats[EN_FATORC].health == 80, "orc");
  CHECK(enemy_stats[EN_DARKKNIGHT].armor == 80 && enemy_stats[EN_SHAMAN].magic_armor == 85, "armours");
  CHECK(enemy_stats[EN_GARGOYLE].flying && !enemy_stats[EN_GARGOYLE].blockable, "fliers can't be blocked");
  CHECK(enemy_stats[EN_JUGGERNAUT].boss && enemy_stats[EN_JUGGERNAUT].health == 10000, "the Juggernaut");
  /* Level1.as: 20 lives, 265 gold, 8 spots, 7 waves, one tower level */
  CHECK(B.lives == 20 && B.cash == 265 && B.nspots == 8 && B.max_waves == 7, "stage 1: %d lives %d gold %d spots %d waves", B.lives, B.cash, B.nspots, B.max_waves);
  CHECK(tower_locked(0, 2), "stage 1 has level 1 towers only");
  CHECK(B.npaths == 1 && B.nlanes[0] == 3 && path_len(0, 0) == 203, "stage 1's road");
  /* the armour: physical damage less armour %, explosions half of it, magic the magic armour */
  int i = enemy_spawn(EN_FATORC, 0, 0, 20);
  Enemy *e = &B.en[i];
  enemy_damage(e, 10, ARM_P, 0);
  CHECK(e->health == 73, "10 physical on 30 armour: 7 (%d left)", e->health);
  enemy_damage(e, 10, ARM_E, 0);
  CHECK(e->health == 64, "10 explosive on 30 armour: 8.5, cut (%d left)", e->health);
  enemy_damage(e, 10, ARM_M, 0);
  CHECK(e->health == 54, "10 magic: 10 (%d left)", e->health);
  /* selling: 60% back */
  battle_start(0, MODE_CAMPAIGN);
  spot_build(0, TW_ARCHER);
  CHECK(B.cash == 195 && spot_sell_value(0) == 42, "an archer tower: 70, sold for 42");
  spot_sell(0);
  CHECK(B.cash == 237 && B.spot[0].kind == TW_NONE, "sold");
  /* the stars: 3 at 18 lives or more, 2 above 5, else 1 */
  B.lives = 18;
  CHECK(battle_stars() == 3, "3 stars");
  B.lives = 6;
  CHECK(battle_stars() == 2, "2 stars");
  B.lives = 5;
  CHECK(battle_stars() == 1, "1 star");
  /* the upgrades: Eagle Eye +10% range (ceil), Hermetic Study mages 10% cheaper */
  memset(upgrades, 0, sizeof upgrades);
  upgrades[UP_ARCHERS] = 2, upgrades[UP_MAGES] = 3;
  battle_start(1, MODE_CAMPAIGN);
  CHECK(tower_stat(TW_ARCHER, 1).range == 242, "eagle eye: %d", tower_stat(TW_ARCHER, 1).range);
  CHECK(tower_stat(TW_MAGE, 1).cost == 90 && tower_stat(TW_MAGE, 1).range == 242, "hermetic study, spell reach");
  memset(upgrades, 0, sizeof upgrades);
  /* waves: the first waits for its call, an early call pays a gold a second left */
  battle_start(0, MODE_CAMPAIGN);
  for (int t = 0; t < 40; t++) battle_step();
  CHECK(B.waiting_first && B.flags_up, "the first wave waits");
  int cash = B.cash;
  CHECK(battle_call_wave(), "called");
  CHECK(B.cash == cash, "no gold for the first call");
  for (int t = 0; t < 30 * 8; t++) battle_step();
  CHECK(B.current_wave == 1, "wave 1 under way");
  int guard = 0;
  while (!B.flags_up && guard++ < 30 * 60) battle_step();
  CHECK(B.flags_up, "the next wave's flags");
  int left = battle_wave_seconds_left();
  cash = B.cash;
  battle_call_wave();
  CHECK(B.cash - cash == left || B.cash - cash == left - 1, "early call: +%d for %d s", B.cash - cash, left);
  /* heroic and iron: 1 life */
  battle_start(4, MODE_HEROIC);
  CHECK(B.lives == 1 && B.max_waves == 6, "heroic: 1 life, 6 waves (%d)", B.max_waves);
  battle_start(4, MODE_IRON);
  CHECK(B.lives == 1 && B.max_waves == 1, "iron: one wave (%d)", B.max_waves);
}

/* ------------------------------------------------------------------ a general */
static int bot_style;
static int alive_enemies(void) {
  int n = 0;
  for (int i = 0; i < MAX_ENEMIES; i++) n += B.en[i].alive && !B.en[i].dead && i != B.veznan;
  return n;
}
static void general(void) {
  /* spells: on the enemy furthest along */
  int best = -1;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active || e->dead) continue;
    if (best < 0 || path_len(e->path, e->lane) - e->node < path_len(B.en[best].path, B.en[best].lane) - B.en[best].node) best = i;
  }
  if (best >= 0) {
    Pt at = {B.en[best].x, B.en[best].y};
    Enemy *e = &B.en[best];
    if (power_fire_cooldown() == 0) power_fire(at);
    /* (soldiers where the towers reach: not at the road's start) */
    if (power_reinf_cooldown() == 0 && e->node > path_len(e->path, e->lane) / 3) power_reinforce(at);
  }
  static const uint8_t mix[4] = {TW_ARCHER, TW_BARRACKS, TW_MAGE, TW_ENGINEER};
  for (int s = 0; s < B.nspots; s++) {
    Spot *t = &B.spot[s];
    if (t->kind == 0xFF || t->building || t->disabled) continue;
    if (t->kind == TW_NONE) {
      int k = mix[(s + bot_style) % 4];
      int c = spot_cost(s, (int[]){0, 1, 2, 3, 4}[k == TW_ARCHER ? 1 : k == TW_BARRACKS ? 2 : k == TW_MAGE ? 3 : 4]);
      if (c > 0 && c <= B.cash) spot_build(s, k);
      continue;
    }
    if (t->level < 3) {
      int c = spot_cost(s, 0);
      if (c > 0 && c <= B.cash - 40) spot_upgrade(s, 0);
    } else if (t->level == 3) {
      int ch = 1 + ((s + bot_style) & 1), c = spot_cost(s, ch);
      if (c > 0 && c <= B.cash - 40) spot_upgrade(s, ch);
    } else
      for (int k = 0; k < 3; k++) {
        int c = spot_ability_cost(s, k);
        if (c > 0 && c <= B.cash - 60) spot_buy_ability(s, k);
      }
    if (t->ice == 2) t->ice_hits++, t->ice = t->ice_hits >= 3 ? 3 : 2;   /* break J.T.'s ice */
  }
}

static int play_battle(int level, int mode, bool full_upgrades, int *lives) {
  memset(upgrades, full_upgrades ? 5 : 0, sizeof upgrades);
  battle_seed = (uint32_t)(level * 31 + mode * 7 + bot_style);
  battle_start(level, mode);
  for (int t = 0; t < 40; t++) battle_step();
  battle_call_wave();
  for (int t = 0; t < 30 * 60 * 60 && B.state != LS_WIN && B.state != LS_OVER; t++) {
    battle_step();
    if (t % 15 == 0) general();
    if (B.pending_note) B.pending_note = 0;
    if (B.flags_up && (mode == MODE_IRON || alive_enemies() < 4)) battle_call_wave();   /* (the general is in a hurry) */
  }
  *lives = B.lives;
  return B.state;
}

static void test_level1(void) {
  int lives;
  bot_style = 0;
  int st = play_battle(0, MODE_CAMPAIGN, false, &lives);
  CHECK(st == LS_WIN, "stage 1 won by script (state %d)", st);
  CHECK(lives >= 18, "with 3 stars (%d lives)", lives);
}

static void test_all(void) {
  int won = 0, total = 0;
  for (int l = 0; l < NLEVELS; l++)
    for (int m = 0; m < 3; m++) {
      int lives;
      bot_style = l + m;
      int st = play_battle(l, m, true, &lives);
      CHECK(st == LS_WIN || st == LS_OVER, "stage %d mode %d ended (state %d)", l + 1, m, st);
      printf("  stage %2d %-8s %s, %d lives, %d kills\n", l + 1, m == 0 ? "campaign" : m == 1 ? "heroic" : "iron",
             st == LS_WIN ? "won" : "lost", lives, B.kills);
      won += st == LS_WIN, total++;
    }
  printf("  the general won %d of %d\n", won, total);
}

/* ------------------------------------------------------------------ random keys */
static uint32_t fz = 12345;
static uint32_t fuzz_rand(void) {
  fz ^= fz << 13, fz ^= fz >> 17, fz ^= fz << 5;
  return fz;
}

static void test_fuzz(int sessions) {
  static const uint32_t keys[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_OK, K_OK, K_BACK, K_DEL, K_DIGIT(0),
                                  K_DIGIT(1), K_DIGIT(2), K_DIGIT(3), K_EXE, 0, 0, 0};
  for (int s = 0; s < sessions; s++) {
    game_init();
    /* every stage open, stars to spend */
    for (int i = 0; i < NLEVELS; i++) g.p.camp[i] = 3;
    for (int t = 0; t < 4000 && g.running; t++) {
      uint32_t k = keys[fuzz_rand() % (sizeof keys / sizeof keys[0])];
      step(k);
      if (t % 97 == 0) game_draw();
    }
  }
  CHECK(1, "fuzz");
}

/* ------------------------------------------------------------------ the save */
static void test_save(void) {
  game_init();
  g.p.camp[0] = 3, g.p.camp[1] = 2, g.p.heroic[0] = 1, g.p.ups[UP_RAIN] = 2;
  save_progress();
  memset(&g.p, 0, sizeof g.p);
  load_progress();
  CHECK(g.p.camp[0] == 3 && g.p.camp[1] == 2 && g.p.heroic[0] == 1 && g.p.ups[UP_RAIN] == 2, "the save comes back");
  FILE *f = fopen("build/host-saves/kingdomrush.sav", "r+b");
  if (f) {
    fseek(f, 6, SEEK_SET);
    fputc(0x77, f);
    fclose(f);
  }
  load_progress();
  CHECK(g.p.camp[0] == 0 && g.p.difficulty == 1, "a broken save: a fresh start");
  CHECK(stars_won() == 0, "no stars");
  remove("build/host-saves/kingdomrush.sav");
}

int main(int argc, char **argv) {
  kr_bin = kr_data;
  gfx_set_band_memory(band);
  system("mkdir -p build/host-saves");
  host_save_dir("build/host-saves");
  remove("build/host-saves/kingdomrush.sav");
  game_init();
  if (argc > 1 && !strcmp(argv[1], "sizes")) {
    printf("Battle %zu: Enemy %zu x %d, Soldier %zu x %d, Bullet %zu x %d, Fx %zu x %d, Spot %zu\n", sizeof B, sizeof(Enemy),
           MAX_ENEMIES, sizeof(Soldier), MAX_SOLDIERS, sizeof(Bullet), MAX_BULLETS, sizeof(Fx), MAX_FX, sizeof(Spot));
    return 0;
  }
  if (argc > 3 && !strcmp(argv[1], "debug")) {   /* a stage that does not end: who is left */
    int lives;
    bot_style = atoi(argv[2]) + atoi(argv[3]);
    int st = play_battle(atoi(argv[2]), atoi(argv[3]), true, &lives);
    printf("state %d lives %d wave %d/%d index %d/%d active waves %d flags %d\n", st, lives, B.current_wave, B.max_waves,
           B.index_waves, B.nwaves, B.nactive, B.flags_up);
    for (int i = 0; i < MAX_ENEMIES; i++) {
      Enemy *e = &B.en[i];
      if (e->alive)
        printf("  #%d %s node %d/%d active %d dead %d held %d blocked %d fighting %d sp %d tele %d thorn %d resp %d froze %d hp %d speed %.2f slow %d net %d (%.0f,%.0f)\n",
               i, enemy_name(e->type), e->node, path_len(e->path, e->lane), e->active, e->dead, e->held, e->blocked, e->fighting,
               e->sp_state, e->teleporting, e->thorned, e->respawn_t, e->froze, (int)e->health, e->speed, e->slow_t, e->net_t, e->x, e->y);
    }
    return 0;
  }
  if (argc > 2 && !strcmp(argv[1], "shots")) {
    char p[512];
    game_draw();
    snprintf(p, sizeof p, "%s/title.ppm", argv[2]);
    host_shot(p);
    press(K_OK);
    game_draw();
    snprintf(p, sizeof p, "%s/map.ppm", argv[2]);
    host_shot(p);
    press(K_OK);
    game_draw();
    snprintf(p, sizeof p, "%s/stage.ppm", argv[2]);
    host_shot(p);
    press(K_OK);
    for (int i = 0; i < 40; i++) step(0);
    press(K_OK);
    game_draw();
    snprintf(p, sizeof p, "%s/ring.ppm", argv[2]);
    host_shot(p);
    /* a stage well under way, every upgrade bought, towers at their best */
    memset(g.p.ups, 5, sizeof g.p.ups);
    memset(g.p.seen_note, 0xFF, sizeof g.p.seen_note);
    int lvl = argc > 3 ? atoi(argv[3]) : 5;
    play_start(lvl, MODE_CAMPAIGN);
    for (int i = 0; i < 40; i++) step(0);
    static const int notes[] = {1, 3, 17, 24};   /* a spell, an enemy, a tower, a flying enemy */
    for (int k = 0; k < 4; k++) {
      int n = notes[k];
      g.p.seen_note[n >> 3] = (uint8_t)(g.p.seen_note[n >> 3] & ~(1 << (n & 7)));
      B.pending_note = (uint8_t)n;
      step(0);
      game_draw();
      snprintf(p, sizeof p, "%s/note%d.ppm", argv[2], n);
      host_shot(p);
      press(K_OK);
    }
    battle_call_wave();
    bot_style = 1;
    for (int t = 0; t < 30 * 150; t++) {
      step(0);
      if (t % 15 == 0) general();
      if (B.flags_up && t > 30 * 60) battle_call_wave();
      if (t % 450 == 449) {
        game_draw();
        snprintf(p, sizeof p, "%s/fight%d.ppm", argv[2], t / 450);
        host_shot(p);
      }
    }
    return 0;
  }
  test_numbers();
  test_level1();
  test_save();
  test_fuzz(argc > 1 && !strcmp(argv[1], "quick") ? 20 : 150);
  if (!(argc > 1 && !strcmp(argv[1], "quick"))) test_all();
  printf("%d checks, %s\n", checks, fails ? "FAILED" : "ok");
  return fails != 0;
}
