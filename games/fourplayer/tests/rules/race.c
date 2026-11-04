#include "../../src/g_race.c"
#include "t.h"

void rules_race(void) {
  app_test_round(M2, G_RACE);
  CHECK(c[0].x < MX && c[1].x < MX && c[0].prog < 0, "racing: the cars start behind the line");
  float h = c[0].h;
  app_test_step(1);
  CHECK(fabs_(wrap_pi(h - TURN * DT - c[0].h)) < 0.001f && c[1].h == 0, "racing: holding turns left, letting go goes straight");
  run(0, 150);
  CHECK(c[1].s > 100 && c[1].x > 160, "racing: the cars drive on their own");
  CHECK(c[1].prog > 0 && lap(1) == 1, "racing: over the line, lap 1");

  app_test_round(M2, G_RACE); /* the right way round: three laps take the round */
  c[1].prog = LAPS * 2 * PI - 0.001f, c[1].last_a = fatan2(c[1].y - MY, c[1].x - MX);
  run(0, 100);
  CHECK(WINNER == 1, "racing: three laps, the round");

  app_test_round(M2, G_RACE); /* the wrong way doesn't count */
  c[0].h = c[0].va = PI, c[0].s = FAST, c[0].x = 200, c[0].y = 212;
  c[0].last_a = fatan2(c[0].y - MY, c[0].x - MX), c[0].prog = 0;
  run(0, 60);
  CHECK(c[0].prog < 0, "racing: driving the wrong way, laps go back");

  app_test_round(M2, G_RACE); /* the island is a wall */
  c[0].x = IX + IW / 2, c[0].y = IY + IH - 2, c[0].h = c[0].va = -PI / 2, c[0].s = FAST;
  app_test_step(0);
  CHECK(island(c[0].x, c[0].y) >= CR - 0.5f, "racing: the island's barrier pushes cars out");
  CHECK(fabs_(wrap_pi(c[0].va)) < 0.1f || fabs_(wrap_pi(c[0].va - PI)) < 0.1f, "racing: along the barrier");

  app_test_round(M2, G_RACE); /* grass */
  c[0].x = 10, c[0].y = 30, c[0].h = c[0].va = PI / 4, c[0].s = FAST;
  app_test_step(0);
  CHECK(c[0].s < FAST, "racing: grass slows");
  c[0].x = 10, c[0].y = 30, c[0].s = FAST;
  for (int i = 0; i < 40; i++) c[0].x = 10, c[0].y = 30, app_test_step(0);
  CHECK(c[0].s < SLOW + 15, "racing: on grass, little more than a crawl");

  app_test_round(M2, G_RACE); /* oil */
  c[0].x = oil[0][0], c[0].y = oil[0][1], c[0].h = c[0].va = 0, c[0].s = 40;
  app_test_step(0);
  h = c[0].h;
  app_test_step(1), app_test_step(1);
  CHECK(c[0].h == h, "racing: on oil, no steering");
  for (int i = 0; i < 60; i++) app_test_step(1);
  CHECK(c[0].h != h, "racing: steering again after the oil");
}
