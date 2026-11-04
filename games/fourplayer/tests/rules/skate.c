#include "../../src/g_skate.c"
#include "t.h"

void rules_skate(void) {
  app_test_round(M2, G_SKATE);
  run(0, 300);
  CHECK(x[0] == 30 && x[1] == 30, "skate: no kicks, no rolling");
  app_test_step(1), app_test_step(0);
  CHECK(v[0] > 23 && v[0] <= 24 && v[1] == 0, "skate: a kick is speed");
  run(0, 100);
  CHECK(v[0] < 24 && x[0] > 30, "skate: rolling slows down");
  int steps = 0; /* one kicks seven times a second, the other five */
  while (WINNER == UNDECIDED && steps < 6000) {
    unsigned k = (steps % 14 < 7 ? 1 : 0) | (steps % 20 < 10 ? 2 : 0);
    app_test_step(k), steps++;
  }
  CHECK(WINNER == 0, "skate: the faster kicker takes the round");
  CHECK(steps > 600 && steps < 2000, "skate: a race takes 6 to 20 seconds");
}
