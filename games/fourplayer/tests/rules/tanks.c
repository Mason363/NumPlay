#include "../../src/g_tanks.c"
#include "t.h"

static int shells(int p) {
  int n = 0;
  for (int i = 0; i < NSH; i++) n += sh[i].own == p;
  return n;
}
static void clear_map(void) { nmap = 0; }

void rules_tanks(void) {
  app_test_round(M2, G_TANKS);
  clear_map();
  float a = t[0].a, x = t[0].b.x;
  app_test_step(0);
  CHECK(fabs_(wrap_pi(t[0].a - a - TURN * DT)) < 0.001f && t[0].b.x == x, "tanks: alone, a tank turns on the spot");
  a = t[0].a;
  run(1, 50);
  CHECK(t[0].a == a && fabs_(t[0].b.x - x) > 10, "tanks: held, it drives straight");
  CHECK(shells(0) == 0, "tanks: no shell while driving");
  app_test_step(0);
  CHECK(shells(0) == 1, "tanks: let go, it fires");
  app_test_step(1), app_test_step(0);
  CHECK(shells(0) == 1, "tanks: 400 ms between shells");

  app_test_round(M2, G_TANKS); /* a hit */
  clear_map();
  t[0].b = (body_t){100, 131, 0, 0}, t[0].a = 0, t[1].b = (body_t){200, 131, 0, 0};
  app_test_step(1), app_test_step(0);
  run(0, 200);
  CHECK(gone[1] && WINNER == 0, "tanks: one hit, out, and the last tank takes the round");

  app_test_round(M2V2, G_TANKS); /* teams: through your own */
  clear_map();
  t[0].b = (body_t){60, 131, 0, 0}, t[0].a = 0, t[2].b = (body_t){120, 131, 0, 0}, t[1].b = (body_t){220, 131, 0, 0};
  t[3].b = (body_t){160, 60, 0, 0};
  app_test_step(1), app_test_step(0);
  for (int i = 0; i < 200 && !gone[1]; i++) {
    t[2].b = (body_t){120, 131, 0, 0}; /* (the teammate stays in the way) */
    app_test_step(0);
  }
  CHECK(!gone[2] && gone[1], "tanks: shells pass through your team");

  app_test_round(M2, G_TANKS); /* bounces, then gone: in a corridor 60 pixels wide */
  static const rect_t CORR[2] = {{100, 50, 20, 150}, {180, 50, 20, 150}};
  map = CORR, nmap = 2;
  t[0].b = (body_t){150, 131, 0, 0}, t[0].a = 0, t[1].b = (body_t){40, 40, 0, 0};
  app_test_step(1), app_test_step(0);
  int i = 0, bounced = 0;
  for (; i < 300 && shells(0); i++) {
    t[0].b = (body_t){150, 60, 0, 0}; /* (out of the way) */
    for (int k = 0; k < NSH; k++)
      if (sh[k].own == 0) bounced = sh[k].hits;
    app_test_step(0);
  }
  CHECK(!shells(0) && bounced == 3 && i > 100, "tanks: a shell bounces three times, then it's gone");
  app_test_round(M2, G_TANKS);
  clear_map();
  t[0].b = (body_t){290, 131, 0, 0}, t[0].a = 0, t[1].b = (body_t){40, 40, 0, 0};
  app_test_step(1), app_test_step(0);
  run(0, 100);
  CHECK(gone[0] && WINNER == 1, "tanks: your own shell can hit you");

  app_test_round(M2, G_TANKS); /* the walls */
  t[0].b = (body_t){map[0].x + map[0].w / 2.0f, map[0].y + map[0].h / 2.0f, 0, 0};
  app_test_step(0);
  CHECK(!solid(t[0].b.x, t[0].b.y), "tanks: a tank never stays in a wall");
}
