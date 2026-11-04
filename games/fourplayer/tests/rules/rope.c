#include "../../src/g_rope.c"
#include "t.h"

/* steps until the bar is about to reach p (within ms) */
static void until_near(int p, float ms) {
  for (int i = 0; i < 2000; i++) {
    float d = wrap_pi(seat(p) - beam), dist = w > 0 ? (d >= 0 ? d : d + 2 * PI) : (d <= 0 ? -d : 2 * PI - d);
    if (fabs_(w) > 0.5f && dist / fabs_(w) * 1000 < ms) return;
    for (int q = 0; q < np; q++)
      if (q != p) jump_t[q] = rt - AIR / 2; /* (the others in the air) */
    app_test_step(0);
  }
}

void rules_rope(void) {
  app_test_round(M2, G_ROPE);
  flip_at = 1 << 30;
  until_near(0, 150);
  app_test_step(1), app_test_step(0); /* a jump */
  CHECK(height(0) > 0, "rope: a tap jumps");
  app_test_step(1), app_test_step(0);
  CHECK(jump_t[0] < rt - 15, "rope: no jump in the air");
  for (int i = 0; i < 60; i++) jump_t[1] = rt - AIR / 2, app_test_step(0);
  CHECK(!gone[0], "rope: in the air, the bar passes under");
  until_near(0, 200);
  for (int i = 0; i < 40 && !gone[0]; i++) jump_t[1] = rt - AIR / 2, app_test_step(0);
  CHECK(gone[0] && WINNER == 1, "rope: on the ground, the bar throws you off");

  app_test_round(M4, G_ROPE); /* it turns back now and then, and a second bar comes */
  int was = dir;
  for (int i = 0; i < 1300 && dir == was; i++) {
    for (int p = 0; p < np; p++) jump_t[p] = rt - AIR / 2; /* (all in the air) */
    app_test_step(0);
  }
  CHECK(dir != was, "rope: the bar turns back");
  for (int i = 0; i < 2300 && !two; i++) {
    for (int p = 0; p < np; p++) jump_t[p] = rt - AIR / 2;
    app_test_step(0);
  }
  CHECK(two && rt > 22000, "rope: a second bar after 22 seconds");
  CHECK(speed() > 3, "rope: faster as the round goes on");
}
