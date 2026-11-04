#include "../../src/g_clock.c"
#include "t.h"

/* p taps at the round's time ms */
static void tap_at(unsigned who, int32_t ms) {
  while (rt + TICK < ms && WINNER == UNDECIDED) app_test_step(0);
  app_test_step(who), app_test_step(0);
}

void rules_clock(void) {
  app_test_round(M2, G_CLOCK);
  target = 7000;
  tap_at(1, 6800), tap_at(2, 7100);
  CHECK(WINNER == 1, "clock: the closest takes the round");
  CHECK(stopped[0] == 6800 && stopped[1] == 7100, "clock: each one's time, to the hundredth");

  app_test_round(M2, G_CLOCK);
  target = 5000;
  tap_at(1, 4000), tap_at(1, 5000), tap_at(2, 5500);
  CHECK(stopped[0] == 4000 && WINNER == 1, "clock: only the first tap counts");

  app_test_round(M2, G_CLOCK);
  target = 6000;
  tap_at(1, 5800), tap_at(2, 6200);
  CHECK(WINNER == -1, "clock: as close as each other, a draw");

  app_test_round(M3, G_CLOCK);
  target = 8000;
  tap_at(4, 7000);
  run(0, 2000);
  CHECK(WINNER == 2 && rt <= 12000 + 20, "clock: those who don't stop it, no time");
  app_test_round(M2, G_CLOCK);
  run(0, 2000);
  CHECK(WINNER == -1, "clock: nobody stops it, a draw");
  int lo = 99999, hi = 0;
  for (int i = 0; i < 200; i++) start(), lo = target < lo ? target : lo, hi = target > hi ? target : hi;
  CHECK(lo == 5000 && hi == 10000, "clock: 5 to 10 seconds");
}
