#include "../../src/g_soccer.c"
#include "t.h"

/* the ball sent at a goal's middle, from just inside */
static void shoot_at(int k, float speed) {
  float mx = (seg[k].x0 + seg[k].x1) / 2, my = (seg[k].y0 + seg[k].y1) / 2, dx = mx - CX, dy = my - CY, l = fsqrt(dx * dx + dy * dy);
  ball = (body_t){mx - dx / l * 20, my - dy / l * 20, dx / l * speed, dy / l * speed};
}
static int goal_seg(int side_) {
  for (int i = 0; i < nseg; i++)
    if (seg[i].goal == side_) return i;
  return -1;
}
static void players_away(void) { /* (out of the ball's way) */
  for (int p = 0; p < np; p++) pl[p].b = (body_t){CX + (p - np / 2.0f) * 22, CY + 60, 0, 0};
}

void rules_soccer(void) {
  app_test_round(M2, G_SOCCER);
  CHECK(!corners && nseg == 8 && goal_seg(0) >= 0 && goal_seg(1) >= 0, "soccer: two players, a pitch and two goals");
  CHECK(seg[goal_seg(0)].x0 < CX && pl[0].b.x < CX, "soccer: player 1's goal on the left, theirs the left half");
  players_away();
  shoot_at(goal_seg(0), 300);
  run(0, 50);
  CHECK(gone[0] && WINNER == 1, "soccer: a goal in yours, you're out: the other takes the round");

  app_test_round(M2, G_SOCCER); /* a hit sends the ball */
  ball = (body_t){CX, CY, 0, 0};
  pl[0].b = (body_t){CX - 30, CY, 0, 0}, pl[0].a = 0;
  app_test_step(1);
  run(0, 30);
  CHECK(ball.vx > 100, "soccer: a dash into the ball sends it flying");

  app_test_round(M2V2, G_SOCCER); /* teams: the first goal */
  players_away();
  shoot_at(goal_seg(0), 300);
  run(0, 50);
  CHECK(WINNER == 1 && alive() == 4, "soccer: teams, a goal takes the round");

  app_test_round(M4, G_SOCCER); /* four: a goal each, in the corners */
  CHECK(corners && goal_seg(0) >= 0 && goal_seg(3) >= 0, "soccer: four players, four corner goals");
  CHECK(seg[goal_seg(0)].x0 < CX && seg[goal_seg(0)].y1 < CY, "soccer: player 1's goal, top left");
  players_away();
  shoot_at(goal_seg(2), 300);
  run(0, 50);
  CHECK(gone[2] && WINNER == UNDECIDED, "soccer: a goal in yours, you're out, the others play on");
  run(0, 150); /* the kick-off */
  CHECK(fabs_(ball.x - CX) < 1 && fabs_(ball.y - CY) < 1, "soccer: the ball back in the middle");
  players_away();
  shoot_at(goal_seg(2), 300);
  run(0, 60);
  CHECK(past(&seg[goal_seg(2)], ball.x, ball.y) < 0 && alive() == 3, "soccer: the goal of one who's out is a wall");

  app_test_round(M3, G_SOCCER); /* three: the bottom right corner is a wall */
  players_away();
  int corner = 3; /* (the fourth diagonal) */
  for (int i = 0, k = 0; i < nseg; i++)
    if (seg[i].x0 != seg[i].x1 && seg[i].y0 != seg[i].y1 && k++ == 1) corner = i;
  shoot_at(corner, 300);
  run(0, 60);
  CHECK(alive() == 3 && WINNER == UNDECIDED, "soccer: three players, one corner is a wall");
}
