#include "../../src/g_sumo.c"
#include "t.h"

void rules_sumo(void) {
  app_test_round(M2, G_SUMO);
  float a0 = w[0].a;
  run(0, 100); /* a second */
  CHECK(fabs_(wrap_pi(w[0].a - a0 - SPIN)) < 0.05f, "sumo: the arrow spins 3.4 radians a second");
  CHECK(alive() == 2 && WINNER == UNDECIDED, "sumo: nobody moves, nobody falls");

  app_test_round(M2, G_SUMO);
  w[0].b = (body_t){CX - 40, CY, 0, 0}, w[0].a = 0, w[1].b = (body_t){CX, CY + 60, 0, 0};
  app_test_step(1);
  CHECK(w[0].b.vx > 150 && fabs_(w[0].b.vy) < 20, "sumo: a tap dashes where the arrow points");
  CHECK(w[0].spin < 0, "sumo: a dash turns the spin the other way");
  float v = w[0].b.vx;
  app_test_step(0), app_test_step(1); /* again at once: too soon */
  CHECK(w[0].b.vx <= v, "sumo: no second dash within 300 ms");

  app_test_round(M2, G_SUMO); /* one pushes the other off */
  w[0].b = (body_t){CX - 20, CY, 250, 0}, w[1].b = (body_t){CX + 3, CY, 0, 0};
  app_test_step(0);
  CHECK(w[1].b.vx > 100, "sumo: a hit passes the push on");
  run(0, 300);
  CHECK(gone[1] && !gone[0] && WINNER == 0, "sumo: pushed into the sea, the other takes the round");

  app_test_round(M3V3, G_SUMO); /* teams: the last team on the ice */
  w[1].b = (body_t){CX + RING + 5, CY, 0, 0}, w[3].b = (body_t){CX - RING - 5, CY, 0, 0};
  app_test_step(0);
  CHECK(gone[1] && gone[3] && WINNER == UNDECIDED, "sumo: a team with one left plays on");
  w[5].b = (body_t){CX, CY + RING + 5, 0, 0}, w[0].b = (body_t){CX, CY - RING - 5, 0, 0};
  app_test_step(0);
  CHECK(WINNER == 0, "sumo: the blue team all in the sea, the red team takes the round");

  app_test_round(M2, G_SUMO); /* all at once: a draw */
  w[0].b = (body_t){CX + RING + 5, CY, 0, 0}, w[1].b = (body_t){CX - RING - 5, CY, 0, 0};
  app_test_step(0);
  CHECK(WINNER == -1, "sumo: both in the sea at once, a draw");

  app_test_round(M4, G_SUMO); /* the ice melts after 20 seconds */
  for (int p = 0; p < 4; p++) w[p].b = (body_t){CX + (p - 1.5f) * 4, CY, 0, 0}, w[p].fall_t = -1;
  for (int i = 0; i < 2500 && WINNER == UNDECIDED; i++) {
    for (int p = 0; p < 4; p++) w[p].b.vx = w[p].b.vy = 0;
    app_test_step(0);
  }
  CHECK(ring < RING - 10, "sumo: the ice melts after 20 seconds");
}
