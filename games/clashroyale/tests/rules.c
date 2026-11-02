/* Clash Royale on a computer: the units against the spec's numbers (gamesrc/clashroyale/spec), whole battles bot
 * against bot, random keys, saves, and screenshots (build/shots, as PPM). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "game.h"
#include "gfx.h"
#include "plat.h"
#include "res.h"

extern const uint8_t cr_data[];
const uint8_t *cr_bin;
extern uint16_t host_fb[SCREEN_W * SCREEN_H];
extern uint32_t host_time, host_keys;
extern bool host_storage_full;
extern bool bot_plays_both;
void host_shot(const char *path);
void host_save_dir(const char *d);

static int fails, checks;
#define CHECK(c, ...)                                     \
  do {                                                    \
    checks++;                                             \
    if (!(c)) {                                           \
      fails++;                                            \
      printf("FAIL %s:%d: ", __FILE__, __LINE__);         \
      printf(__VA_ARGS__);                                \
      printf("\n");                                       \
    }                                                     \
  } while (0)

static const uint8_t DECK[8] = {CA_KNIGHT, CA_ARCHER, CA_SKELETONARMY, CA_GOBLINS, CA_FIREBALL, CA_GIANT, CA_VALKYRIE, CA_MINIPEKKA};

/* an empty field: the towers only, no bot */
static void bare(Battle *b) {
  battle_init(b, DECK, DECK, 0, 1);
  b->bot[1].think = 1 << 30;
  b->bot[0].think = 1 << 30;
}

static void test_numbers(void) {
  Battle *b = malloc(sizeof *b);
  bare(b);
  /* towers at King level 8 */
  CHECK(b->e[b->side[0].king].maxhp == 3768, "King tower %ld HP", (long)b->e[b->side[0].king].maxhp);
  CHECK(b->e[b->side[0].king].dmg == 78, "King tower %ld damage", (long)b->e[b->side[0].king].dmg);
  CHECK(b->e[b->side[0].princess[0]].maxhp == 2352, "Princess tower %ld HP", (long)b->e[b->side[0].princess[0]].maxhp);
  CHECK(b->e[b->side[0].princess[0]].dmg == 84, "Princess tower %ld damage", (long)b->e[b->side[0].princess[0]].dmg);
  /* cards at tournament standard (spec/cards.md) */
  struct { int card; int32_t hp, dmg; } want[] = {
      {CA_KNIGHT, 1158, 144}, {CA_ARCHER, 241, 77},  {CA_GIANT, 3200, 192},   {CA_PEKKA, 3146, 544},
      {CA_MUSKETEER, 544, 144}, {CA_MINIPEKKA, 960, 520}, {CA_HOGRIDER, 1280, 256}, {CA_BABYDRAGON, 968, 121},
      {CA_CANNON, 868, 115}, {CA_GOLEM, 3811, 225}};
  for (unsigned i = 0; i < sizeof want / sizeof want[0]; i++) {
    const CardDef *c = &CARDS[want[i].card];
    int idx = card_lvl_index(want[i].card);
    const UnitDef *u = &UNITS[c->unit];
    int32_t d = u->proj != NONE ? PROJS[u->proj].dmg : u->dmg;
    CHECK(lv(u->hp, idx) == want[i].hp, "%s: %ld HP, want %ld", c->name, (long)lv(u->hp, idx), (long)want[i].hp);
    CHECK(lv(d, idx) == want[i].dmg, "%s: %ld damage, want %ld", c->name, (long)lv(d, idx), (long)want[i].dmg);
  }
  CHECK(CARDS[CA_KNIGHT].cost == 3 && CARDS[CA_GOLEM].cost == 8 && CARDS[CA_SKELETONS].cost == 1, "costs");
  CHECK(CARDS[CA_SKELETONARMY].count == 20 && CARDS[CA_MINIONHORDE].count == 6, "counts");
  CHECK(lv(PROJS[CARDS[CA_FIREBALL].proj].dmg, card_lvl_index(CA_FIREBALL)) == 520, "Fireball 520");
  CHECK(lv(AREAS[CARDS[CA_ZAP].area].dmg, card_lvl_index(CA_ZAP)) == 164, "Zap 164");
  CHECK(lv(PROJS[CARDS[CA_ARROWS].proj].dmg, card_lvl_index(CA_ARROWS)) == 221 && CARDS[CA_ARROWS].radius == 4000,
        "Arrows 221 in 4 tiles");
  CHECK(GL(MATCH_LENGTH) == 180 && GL(OVERTIME_SECONDS) == 60, "3 minutes and a minute of overtime");
  free(b);
}

static void test_elixir(void) {
  Battle *b = malloc(sizeof *b);
  bare(b);
  CHECK(b->side[0].elixir == 5, "start with 5 elixir");
  for (int i = 0; i < 56; i++) battle_step(b);   /* 2.8 s */
  CHECK(b->side[0].elixir == 6, "an elixir every 2.8 s: %ld", (long)b->side[0].elixir);
  b->time_ms = 60000;
  b->side[0].elixir = 0, b->side[0].elixir_t = 0;
  for (int i = 0; i < 28; i++) battle_step(b);   /* 1.4 s */
  CHECK(b->side[0].elixir == 1, "double elixir: one every 1.4 s: %ld", (long)b->side[0].elixir);
  /* the cycle: a played card goes to the back, the next comes after 2 s */
  bare(b);
  b->side[0].elixir = 10;
  uint8_t first = b->side[0].hand[0], next = b->side[0].queue[0];
  CHECK(battle_play(b, 0, 0, 5500, 9500), "play a card");
  CHECK(b->side[0].hand[0] == NONE && b->side[0].queue[b->side[0].nqueue - 1] == first, "to the back of the queue");
  for (int i = 0; i < 40; i++) battle_step(b);
  CHECK(b->side[0].hand[0] == next, "the next card after 2 s");
  /* where cards go */
  CHECK(!battle_can_place(b, 0, CA_KNIGHT, 20500, 3500), "no troops on the enemy side");
  CHECK(battle_can_place(b, 0, CA_FIREBALL, 25500, 3500), "spells anywhere");
  CHECK(!battle_can_place(b, 0, CA_KNIGHT, 15500, 9500), "not in the river");
  CHECK(!battle_can_place(b, 0, CA_KNIGHT, 6500, 3500), "not on a tower");
  free(b);
}

/* a Knight against a Knight, in the middle of the field away from the towers */
static void test_duel(void) {
  Battle *b = malloc(sizeof *b);
  bare(b);
  int lv8 = card_lvl_index(CA_KNIGHT);
  int a = battle_spawn(b, UN_KNIGHT, 0, lv8, 12500, 9000, 0);
  int c = battle_spawn(b, UN_KNIGHT, 1, lv8, 19500, 9000, 0);
  int steps = 0;
  while (ent_alive(b, a) && ent_alive(b, c) && steps < 2000) battle_step(b), steps++;
  bool one = ent_alive(b, a) != ent_alive(b, c);
  CHECK(one, "one Knight left");
  int w = ent_alive(b, a) ? a : c;
  /* nine hits of 144 kill 1158; the one that hits first takes eight */
  CHECK(b->e[w].hp == 1158 - 8 * 144, "the winner has %ld HP, want %d", (long)b->e[w].hp, 1158 - 8 * 144);
  /* they met near the middle */
  CHECK(b->e[w].x > 13000 && b->e[w].x < 19000, "met at x %ld", (long)b->e[w].x);
  free(b);
}

static void test_spells(void) {
  Battle *b = malloc(sizeof *b);
  bare(b);
  int k = battle_spawn(b, UN_KNIGHT, 1, card_lvl_index(CA_KNIGHT), 22500, 9500, 1 << 20);
  /* a Fireball from the hand */
  b->side[0].hand[0] = CA_FIREBALL;
  b->side[0].elixir = 10;
  CHECK(battle_play(b, 0, 0, 22500, 9500), "Fireball cast");
  for (int i = 0; i < 80 && b->e[k].hp == b->e[k].maxhp; i++) battle_step(b);
  CHECK(b->e[k].hp == 1158 - 520, "Fireball does 520: %ld left", (long)b->e[k].hp);
  /* half to a crown tower */
  int pt = b->side[1].princess[0];
  b->side[0].hand[1] = CA_FIREBALL;
  b->side[0].elixir = 10;
  CHECK(battle_play(b, 0, 1, b->e[pt].x, b->e[pt].y), "Fireball on a tower");
  for (int i = 0; i < 80 && b->e[pt].hp == b->e[pt].maxhp; i++) battle_step(b);
  CHECK(b->e[pt].maxhp - b->e[pt].hp == 260, "Fireball does 260 to a tower: %ld", (long)(b->e[pt].maxhp - b->e[pt].hp));
  /* Arrows */
  int k2 = battle_spawn(b, UN_KNIGHT, 1, card_lvl_index(CA_KNIGHT), 24500, 14000, 1 << 20);
  b->side[0].hand[3] = CA_ARROWS;
  b->side[0].elixir = 10;
  CHECK(battle_play(b, 0, 3, 24500, 14000), "Arrows cast");
  for (int i = 0; i < 80 && b->e[k2].hp == b->e[k2].maxhp; i++) battle_step(b);
  CHECK(b->e[k2].hp == 1158 - 221, "Arrows do 221: %ld left", (long)b->e[k2].hp);
  /* Zap at once */
  int g1 = battle_spawn(b, UN_GOBLIN, 1, card_lvl_index(CA_GOBLINS), 20500, 14500, 1 << 20);
  b->side[0].hand[2] = CA_ZAP;
  b->side[0].elixir = 10;
  battle_play(b, 0, 2, 20500, 14500);
  CHECK(!ent_alive(b, g1) || b->e[g1].hp == b->e[g1].maxhp - 164, "Zap 164");
  free(b);
}

static void test_rules(void) {
  Battle *b = malloc(sizeof *b);
  /* a Giant walks to the tower over the bridge and hits only buildings */
  bare(b);
  int gi = battle_spawn(b, UN_GIANT, 0, card_lvl_index(CA_GIANT), 9500, 3500, 0);
  int sk = battle_spawn(b, UN_SKELETON, 1, card_lvl_index(CA_SKELETONS), 18500, 3500, 0);
  b->e[sk].x = 18500;
  bool crossed_bridge = false, hit_tower = false;
  int pt = b->side[1].princess[0];
  for (int i = 0; i < 1200 && ent_alive(b, gi); i++) {
    battle_step(b);
    if (ent_alive(b, gi) && b->e[gi].x >= 15000 && b->e[gi].x < 17000) crossed_bridge |= b->e[gi].y >= 2500 && b->e[gi].y < 4500;
    if (b->e[pt].hp < b->e[pt].maxhp && ent_alive(b, gi) && b->e[gi].target == pt) hit_tower = true;
  }
  CHECK(crossed_bridge, "the Giant crossed on the bridge");
  CHECK(hit_tower, "the Giant hit the tower");
  /* the King wakes when a Princess tower falls */
  bare(b);
  int kt = b->side[1].king;
  CHECK(!(b->e[kt].flags & E_AWAKE), "the King sleeps");
  b->e[b->side[1].princess[1]].hp = 1;
  int ar = battle_spawn(b, UN_ARCHER, 0, 7, 12500, 14500, 0);
  for (int i = 0; i < 600 && ent_alive(b, b->side[1].princess[1] >= 0 ? b->side[1].princess[1] : -1); i++) battle_step(b);
  CHECK(b->side[1].princess[1] < 0 && b->side[0].crowns == 1, "a crown for the tower");
  for (int i = 0; i < 80; i++) battle_step(b);
  CHECK(b->e[kt].flags & E_AWAKE, "the King woke");
  (void)ar;
  /* overtime and the first crown */
  bare(b);
  b->time_ms = 100;
  for (int i = 0; i < 4; i++) battle_step(b);
  CHECK(b->overtime && !b->over && b->time_ms > 59000, "overtime");
  b->e[b->side[1].princess[0]].hp = 1;
  battle_spawn(b, UN_ARCHER, 0, 7, 12500, 3500, 0);
  for (int i = 0; i < 600 && !b->over; i++) battle_step(b);
  CHECK(b->over && b->winner == 0, "sudden death: the first crown wins");
  free(b);
}

/* bot against bot, whole battles */
static void test_battles(void) {
  Battle *b = malloc(sizeof *b);
  bot_plays_both = true;
  int wins[3] = {0}, longest = 0, maxe = 0, crowns = 0;
  for (int s = 0; s < 24; s++) {
    battle_init(b, BOT_DECKS[s % NBOTDECKS], BOT_DECKS[(s * 7 + 3) % NBOTDECKS], s % NARENAS, 1000 + (uint32_t)s);
    int steps = 0;
    while (!b->over && steps < 20 * 60 * 6) {
      battle_step(b);
      steps++;
      int n = 0;
      for (int i = 0; i < MAXE; i++) n += b->e[i].used;
      if (n > maxe) maxe = n;
    }
    CHECK(b->over, "battle %d ended", s);
    if (steps > longest) longest = steps;
    wins[b->winner]++;
    crowns += b->side[0].crowns + b->side[1].crowns;
    if (s < 2) printf("battle %d: crowns %d-%d, %ld and %ld cards played, %ld s\n", s, b->side[0].crowns, b->side[1].crowns,
                      (long)b->side[0].played, (long)b->side[1].played, (long)(steps / 20));
    CHECK(b->side[0].played > 10 && b->side[1].played > 10, "both play cards: %ld %ld", (long)b->side[0].played, (long)b->side[1].played);
  }
  printf("bot battles: %d, %d, %d draws; longest %d s; at most %d units\n", wins[0], wins[1], wins[2], longest / 20, maxe);
  CHECK(crowns >= 6, "towers fall: %d crowns in 24 battles", crowns);
  /* and against no one, the bot takes all three crowns */
  bot_plays_both = false;
  for (int k = 0; k < NBOTDECKS; k++) {
    battle_init(b, DECK, BOT_DECKS[k], k % NARENAS, 50 + (uint32_t)k);
    for (int s = 0; s < 20 * 240 && !b->over; s++) battle_step(b);
    CHECK(b->over && b->winner == 1 && b->side[1].crowns == 3, "deck %d against no one: %d crowns", k, b->side[1].crowns);
  }
  bot_plays_both = true;
  /* a deterministic replay */
  battle_init(b, BOT_DECKS[0], BOT_DECKS[1], 3, 77);
  for (int i = 0; i < 2000 && !b->over; i++) battle_step(b);
  uint32_t h = 0;
  for (unsigned i = 0; i < sizeof *b; i++) h = h * 31 + ((uint8_t *)b)[i];
  Battle *c = malloc(sizeof *c);
  battle_init(c, BOT_DECKS[0], BOT_DECKS[1], 3, 77);
  for (int i = 0; i < 2000 && !c->over; i++) battle_step(c);
  uint32_t h2 = 0;
  for (unsigned i = 0; i < sizeof *c; i++) h2 = h2 * 31 + ((uint8_t *)c)[i];
  CHECK(h == h2, "the same start plays the same");
  bot_plays_both = false;
  free(b);
  free(c);
}

/* the game through its keys */
static void run(uint32_t keys, int steps) {
  for (int i = 0; i < steps && g.running; i++) {
    host_keys = keys;
    game_step(keys);
    host_time += 17;
  }
}
static void tap(uint32_t k) {
  run(k, 2);
  run(0, 3);
}
static void shot(const char *name) {
  char p[128];
  game_draw();
  snprintf(p, sizeof p, "build/shots/%s.ppm", name);
  host_shot(p);
}

static void test_play(void) {
  mkdir("build/shots", 0755);
  mkdir("build/host-saves", 0755);
  host_save_dir("build/host-saves");
  remove("build/host-saves/clashroyale.sav");
  remove("build/host-saves/crresume.sav");
  remove("build/host-saves/clashroyale_saves.py");
  game_init();
  shot("title");
  CHECK(g.screen == SC_TITLE, "the title");
  tap(K_OK);   /* Battle: how to play first */
  CHECK(g.screen == SC_HELP, "how to play the first time");
  shot("help");
  tap(K_OK);
  CHECK(g.screen == SC_BATTLE, "the battle");
  run(0, 60 * 4);
  /* a card to the bridge */
  tap(K_DIGIT(1));
  CHECK(g.field, "the cursor on the field");
  for (int i = 0; i < 2; i++) tap(K_RIGHT);
  shot("place");
  tap(K_OK);
  run(0, 60 * 12);
  tap(K_DIGIT(2));
  tap(K_OK);
  run(0, 60 * 8);
  shot("battle");
  /* Home keeps the battle */
  run(K_HOME, 1);
  CHECK(!g.running, "Home quits");
  game_init();
  CHECK(g.screen == SC_PAUSE, "the battle back, paused");
  tap(K_BACK);
  CHECK(g.screen == SC_BATTLE, "and on");
  bot_plays_both = true;
  for (int i = 0; i < 60 * 60 * 5 && g.screen == SC_BATTLE; i++) run(0, 1);
  bot_plays_both = false;
  CHECK(g.screen == SC_RESULT, "the battle ended");
  shot("result");
  run(0, 60);
  tap(K_OK);
  CHECK(g.screen == SC_TITLE, "back to the title");
  CHECK(g.prof.wins + g.prof.losses + g.prof.draws == 1, "one battle counted");
  int32_t troph = g.prof.trophies;
  /* the deck: swap a card in */
  tap(K_DOWN);
  tap(K_OK);
  CHECK(g.screen == SC_DECK, "the deck");
  tap(K_DOWN), tap(K_DOWN), tap(K_DOWN);
  shot("deck");
  int card = (g.drow - 1) * 8 + g.dcol;
  bool in = false;
  for (int k = 0; k < 8; k++) in |= g.prof.deck[k] == card;
  tap(K_OK);
  if (!in) {
    tap(K_OK);
    CHECK(g.prof.deck[g.dcol] == card, "the card went into the deck");
  }
  tap(K_BACK);
  tap(K_BACK);
  CHECK(g.screen == SC_TITLE, "back from the deck");
  run(K_HOME, 1);
  /* the profile comes back, and from the copy alone */
  game_init();
  CHECK(g.prof.trophies == troph && g.prof.guide, "the profile kept");
  remove("build/host-saves/clashroyale.sav");
  game_init();
  CHECK(g.prof.trophies == troph && g.prof.wins + g.prof.losses + g.prof.draws == 1, "the profile from the copy");
}

/* a battle bot against bot as you would see it: build/shots/demo_*.ppm every 5 s */
static void test_demo(void) {
  bot_plays_both = true;
  memset(&g, 0, sizeof g);
  g.running = true;
  battle_init(&bt, BOT_DECKS[6], BOT_DECKS[2], 2, 4242);
  view_load_arena(2);
  g.screen = SC_BATTLE;
  for (int s = 0; s < 20 * 150 && !bt.over; s++) {
    battle_step(&bt);
    if (s % 100 == 99) {
      char n[32];
      snprintf(n, sizeof n, "demo_%03d", s / 20 + 1);
      shot(n);
    }
  }
  bot_plays_both = false;
}

static void test_fuzz(void) {
  static const uint32_t keys[] = {K_LEFT, K_RIGHT, K_UP, K_DOWN, K_OK, K_BACK, K_EXE, K_DIGIT(1), K_DIGIT(2), K_DIGIT(3),
                                  K_DIGIT(4), 0, 0, 0};
  srand(5);
  for (int s = 0; s < 300; s++) {
    game_init();
    for (int i = 0; i < 1500 && g.running; i++) {
      uint32_t k = keys[rand() % (sizeof keys / sizeof keys[0])];
      if (rand() % 400 == 0) k = K_HOME;
      run(k, 1 + rand() % 4);
      if (i % 200 == 0) game_draw();
    }
    if (g.running) run(K_HOME, 1);
  }
  CHECK(1, "fuzz");
  /* a damaged resume file is dropped */
  FILE *f = fopen("build/host-saves/crresume.sav", "wb");
  for (int i = 0; i < 300; i++) fputc(i * 37, f);
  fclose(f);
  game_init();
  CHECK(g.screen == SC_TITLE, "a damaged battle is not loaded");
}

int main(void) {
  cr_bin = cr_data;
  static uint16_t band[SCREEN_W * BAND_H];
  gfx_set_band_memory(band);
  test_numbers();
  test_elixir();
  test_duel();
  test_spells();
  test_rules();
  test_battles();
  test_play();
  test_demo();
  test_fuzz();
  printf("%d checks, %d failed\n", checks, fails);
  return fails != 0;
}
