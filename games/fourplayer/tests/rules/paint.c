#include "../../src/g_paint.c"
#include "t.h"

void rules_paint(void) {
  app_test_round(M2, G_PAINT);
  app_test_step(0);
  CHECK(count[0] > 0 && count[1] > 0, "paint: the rollers paint where they go");
  int sgn = r[0].sgn;
  app_test_step(1), app_test_step(0);
  CHECK(r[0].sgn == -sgn, "paint: a tap curves the other way");
  int total = 0;
  for (int y = 0; y < GH; y++)
    for (int x = 0; x < GW; x++) total += floor_[y][x] != 0;
  CHECK(total == count[0] + count[1], "paint: the counts are the floor");
  run(0, 3500);
  CHECK(WINNER == (count[0] > count[1] ? 0 : count[1] > count[0] ? 1 : -1), "paint: after 30 seconds, the most floor");
  CHECK(rt >= TIME && rt < TIME + 20, "paint: 30 seconds");

  app_test_round(M2, G_PAINT); /* over the others' paint */
  r[0].x = r[1].x = 100, r[0].y = 100, r[1].y = 140;
  app_test_step(0);
  int before = count[1];
  r[0].x = r[1].x, r[0].y = r[1].y;
  roll(0);
  CHECK(count[1] < before && count[0] > 0, "paint: over the others' colour");

  app_test_round(M2V2, G_PAINT); /* teams paint together */
  app_test_step(0);
  CHECK(count[2] == 0 && count[3] == 0 && count[0] > 0 && count[1] > 0, "paint: teams, one colour each");
}
