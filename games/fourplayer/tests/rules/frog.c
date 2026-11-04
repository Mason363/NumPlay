#include "../../src/g_frog.c"
#include "t.h"

/* p holds for ms, then lets go */
static void jump(int p, int ms) {
  for (int i = 0; i < ms / TICK; i++) app_test_step(1u << p);
  app_test_step(0);
}

void rules_frog(void) {
  app_test_round(M2, G_FROG);
  float x0 = f[0].x;
  jump(0, 300);
  CHECK(f[0].st == AIR && fabs_(f[0].to - x0 - jump_len(300)) < 2, "frog: held then let go, a jump that long");
  CHECK(jump_len(0) == 16 && jump_len(700) == 130 && jump_len(5000) == 130, "frog: 16 to 130 pixels, full in 0.7 s");
  run(0, 40);
  CHECK(f[0].st != AIR, "frog: it lands");

  app_test_round(M2, G_FROG); /* onto a pad, into the water */
  int ms = (int)((pad[0] - f[0].x - 16) / 114 * FULL);
  jump(0, ms);
  run(0, 40);
  CHECK(f[0].st == SIT && fabs_(f[0].x - pad[0]) <= 11, "frog: onto the lily pad");
  float on = f[0].x;
  ms = (int)(((pad[1] + pad[0]) / 2 - f[0].x - 16) / 114 * FULL); /* between two pads */
  if (pad[1] - pad[0] < 30) ms = 0;
  jump(0, ms);
  run(0, 40);
  CHECK(f[0].st == SWIM || pad[1] - pad[0] < 30, "frog: into the water");
  run(0, 80);
  CHECK(f[0].st == SIT && f[0].x == on, "frog: swum back to the pad");

  app_test_round(M2, G_FROG); /* across */
  f[1].x = pad[NPAD - 1];
  ms = (int)((finish + 5 - f[1].x - 16) / 114 * FULL);
  jump(1, ms);
  run(0, 40);
  CHECK(WINNER == 1, "frog: the first across takes the round");
  CHECK(finish > pad[NPAD - 1] && finish - pad[NPAD - 1] < 120, "frog: the last pad within a jump of the bank");
  bool reachable = pad[0] - 22 < 120;
  for (int k = 1; k < NPAD; k++) reachable &= pad[k] - pad[k - 1] < 125;
  CHECK(reachable, "frog: every pad within a jump");
}
