/* The rules of every minigame, checked from inside (each file takes its game's source in).
 *
 *   rules    prints a line a failure, then how many passed */
#include <stdio.h>
#include "t.h"

int t_failed, t_passed;
void rules_sumo(void), rules_race(void), rules_tanks(void), rules_snakes(void), rules_soccer(void), rules_fish(void),
    rules_skate(void), rules_rope(void), rules_paint(void), rules_balloon(void), rules_birds(void), rules_gravity(void),
    rules_frog(void), rules_clock(void);
void host_save_dir(const char *d);

int main(int argc, char **argv) {
  host_save_dir(argc > 1 ? argv[1] : "build");
  app_init(1000);
  rules_sumo(), rules_race(), rules_tanks(), rules_snakes(), rules_soccer(), rules_fish(), rules_skate();
  rules_rope(), rules_paint(), rules_balloon(), rules_birds(), rules_gravity(), rules_frog(), rules_clock();
  /* every game, every mode: a round starts with everyone in, and its pictures draw */
  for (int g = 0; g < NG; g++)
    for (int m = 0; m < 5; m++) {
      app_test_round(m, g);
      int n = alive();
      CHECK(n == (m == 0 ? 2 : m == 1 ? 3 : m == 4 ? 6 : 4), "every game: everyone in at the start");
      for (int i = 0; i < 300; i++) app_test_step(i % 37 < 3 ? 0x3F : i % 11 < 2 ? 0x15 : 0);
      app_draw();
    }
  printf("rules: %d passed, %d failed\n", t_passed, t_failed);
  return t_failed != 0;
}
