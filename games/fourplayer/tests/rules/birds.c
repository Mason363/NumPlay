#include "../../src/g_birds.c"
#include "t.h"

void rules_birds(void) {
  app_test_round(M2, G_BIRDS);
  app_test_step(1);
  CHECK(vy[0] < -200, "birds: a tap, a flap");
  y[1] = y[0], vy[1] = vy[0] = 0;
  run(0, 200);
  CHECK(gone[0] && gone[1] && WINNER == -1, "birds: no flaps, both on the ground: a draw");

  app_test_round(M2, G_BIRDS); /* through a gap, and into a column */
  c[0].x = bx(1) + 30, c[0].mid = 131, c[0].gap = 78;
  for (int i = 0; i < 120 && WINNER == UNDECIDED; i++) {
    y[0] = 131, y[1] = 60, vy[0] = vy[1] = 0;
    app_test_step(0);
  }
  CHECK(!gone[0] && gone[1] && WINNER == 0, "birds: through the gap; into a column, out");
}
