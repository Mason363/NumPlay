#include "../../src/g_gravity.c"
#include "t.h"

void rules_gravity(void) {
  app_test_round(M2, G_GRAVITY);
  CHECK(r[0].on && r[1].on, "gravity: the runners start on the ground");
  app_test_step(1), app_test_step(0);
  CHECK(r[0].g == -1 && !r[0].on, "gravity: a tap flips it");
  app_test_step(1), app_test_step(0);
  CHECK(r[0].g == -1, "gravity: no flip in the air");
  run(0, 100);
  CHECK(r[0].on && r[0].y == CEIL + R, "gravity: on the ceiling");

  app_test_round(M2, G_GRAVITY); /* a hole */
  ngap = 1, gap[0].x0 = world + rx(1) + 20, gap[0].x1 = gap[0].x0 + 50, gap[0].up = 0, next_x = 1e9f;
  run(0, 300);
  CHECK(gone[1] && WINNER == 0, "gravity: through a hole, out");

  app_test_round(M3V3, G_GRAVITY); /* the holes never face each other */
  bool fair = true;
  for (int k = 0; k < 6000; k++) {
    for (int p = 0; p < np; p++) r[p].y = r[p].g > 0 ? FLOOR - R : CEIL + R, r[p].on = true, r[p].vy = 0; /* (all kept) */
    app_test_step(0);
    for (int i = 0; i < ngap; i++)
      for (int j = 0; j < ngap; j++)
        if (gap[i].up != gap[j].up && gap[i].x0 < gap[j].x1 + 20 && gap[j].x0 < gap[i].x1 + 20) fair = false;
  }
  CHECK(fair, "gravity: a hole in the floor is never over one in the ceiling");
  CHECK(speed > 150, "gravity: faster and faster");
}
