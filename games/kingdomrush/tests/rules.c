/* Kingdom Rush on a computer: the rules against the game's numbers, a won stage, random keys.
 *
 *   build/rules            all the tests
 *   build/rules shots DIR  the screens as PPM pictures in DIR */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "gfx.h"
#include "kr.h"
#include "plat.h"
#include "settings.h"
#include "special.h"

extern const uint8_t kr_data[];
const uint8_t *kr_bin;
extern uint32_t host_keys, host_time;
void host_shot(const char *path);
void host_save_dir(const char *d);
static uint16_t band[SCREEN_W * BAND_H];

static int fails;
#define CHECK(c, ...)                                     \
  do {                                                    \
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
  if (argc > 2 && !strcmp(argv[1], "fight")) {
    /* level 1 by the rules alone: archers on every spot, waves called, a picture now and then */
    char p[512];
    memset(g.p.seen_note, 0xFF, sizeof g.p.seen_note);
    play_start(0, MODE_CAMPAIGN);
    for (int i = 0; i < 40; i++) step(0);
    for (int s = 0; s < B.nspots && s < 3; s++) spot_build(s, s == 1 ? TW_BARRACKS : TW_ARCHER);
    battle_call_wave();
    for (int t = 0; t < 30 * 300 && B.state != LS_WIN && B.state != LS_OVER; t++) {
      step(0);
      if (B.flags_up) battle_call_wave();
      for (int s = 0; s < B.nspots; s++)
        if (B.spot[s].kind == TW_NONE && B.cash >= 100) spot_build(s, s % 3 == 1 ? TW_MAGE : TW_ARCHER);
        else if (B.spot[s].kind != TW_NONE && B.spot[s].level < 3 && spot_cost(s, 0) > 0 && spot_cost(s, 0) <= B.cash - 50) spot_upgrade(s, 0);
      if (t % 600 == 300) {
        game_draw();
        snprintf(p, sizeof p, "%s/fight%d.ppm", argv[2], t / 600);
        host_shot(p);
      }
    }
    printf("state %d lives %d cash %d wave %d/%d kills %d\n", B.state, B.lives, B.cash, B.current_wave, B.max_waves, B.kills);
    for (int s = 0; s < B.nspots; s++) printf("spot %d kind %d level %d shots %d\n", s, B.spot[s].kind, B.spot[s].level, B.spot[s].shots);
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
    press(K_OK);
    for (int i = 0; i < 40; i++) step(0);
    game_draw();
    snprintf(p, sizeof p, "%s/battle.ppm", argv[2]);
    host_shot(p);
    return 0;
  }
  printf("%s\n", fails ? "FAILED" : "ok");
  return fails != 0;
}
