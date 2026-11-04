#include "../../src/g_balloon.c"
#include "t.h"

static void pump(unsigned who, int n) {
  for (int i = 0; i < n; i++) app_test_step(who), app_test_step(0);
}

void rules_balloon(void) {
  app_test_round(M2, G_BALLOON);
  burst_at[0] = burst_at[1] = 30;
  pump(1, 5);
  CHECK(pumps[0] == 5 && pumps[1] == 0, "balloons: a tap, a pump");
  pump(2, 7);
  run(0, 1100);
  CHECK(WINNER == 1, "balloons: after 10 seconds the biggest takes the round");

  app_test_round(M2, G_BALLOON);
  burst_at[0] = 10, burst_at[1] = 30;
  pump(2, 4);
  pump(1, 10);
  CHECK(burst_t[0] >= 0 && WINNER == 1, "balloons: burst, out; the only one whole takes the round");

  app_test_round(M3, G_BALLOON); /* the same size: the first there */
  burst_at[0] = burst_at[1] = burst_at[2] = 40;
  pump(1, 8), pump(2, 8), pump(4, 3);
  run(0, 1100);
  CHECK(WINNER == 0, "balloons: the same size, the first to it");

  app_test_round(M2, G_BALLOON); /* everyone bursts: a draw */
  burst_at[0] = burst_at[1] = 2;
  pump(3, 2);
  CHECK(WINNER == -1, "balloons: all burst, a draw");

  app_test_round(M2V2, G_BALLOON); /* teams: the air between you */
  for (int p = 0; p < 4; p++) burst_at[p] = 40;
  pump(1, 10), pump(4, 10), pump(2, 12), pump(8, 6);
  run(0, 1100);
  CHECK(WINNER == 0, "balloons: teams, the most air between you");

  app_test_round(M4, G_BALLOON);
  pump(1, 3);
  run(0, 1100);
  CHECK(pumps[0] == 3, "balloons: no pumping after 10 seconds");
  int lo = 99, hi = 0;
  for (int i = 0; i < 200; i++) {
    start();
    lo = burst_at[0] < lo ? burst_at[0] : lo, hi = burst_at[0] > hi ? burst_at[0] : hi;
  }
  CHECK(lo == 18 && hi == 36, "balloons: they burst at 18 to 36 pumps");
}
