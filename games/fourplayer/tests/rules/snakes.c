#include "../../src/g_snakes.c"
#include "t.h"

static int head_x(int p) { return s[p].cell[s[p].hd] % GW; }
static int head_y(int p) { return s[p].cell[s[p].hd] / GW; }
/* steps until the snakes have moved once more */
static void one_move(unsigned held) {
  int32_t t0 = next_t;
  while (next_t == t0 && WINNER == UNDECIDED) app_test_step(held);
}

void rules_snakes(void) {
  app_test_round(M2, G_SNAKES);
  for (int k = 0; k < NSTAR; k++) star[k] = -1;
  int x = head_x(0), y = head_y(0), d = s[0].dir;
  one_move(0);
  CHECK(head_x(0) == x + DX[d] && head_y(0) == y + DY[d] && s[0].len == 4, "snakes: a step a cell, straight on");
  app_test_step(1);
  one_move(0);
  CHECK(s[0].dir == ((d + 1) & 3), "snakes: a tap turns right");
  for (int i = 0; i < 4; i++) app_test_step(1), app_test_step(0); /* four taps before the next step */
  CHECK(s[0].turns == 2, "snakes: two turns wait their turn, no more");

  app_test_round(M2, G_SNAKES); /* a star */
  for (int k = 0; k < NSTAR; k++) star[k] = -1;
  int h = s[0].cell[s[0].hd];
  star[0] = (int16_t)at(h % GW + DX[s[0].dir], h / GW + DY[s[0].dir]);
  one_move(0);
  for (int i = 0; i < 4; i++) one_move(0);
  CHECK(s[0].len == 7, "snakes: a star, three cells longer");
  CHECK(star[0] >= 0 && grid[star[0] / GW][star[0] % GW] == 0, "snakes: a new star, on a free cell");

  app_test_round(M2, G_SNAKES); /* the wall */
  for (int k = 0; k < NSTAR; k++) star[k] = -1;
  int steps = 0;
  while (WINNER == UNDECIDED && steps < 100) one_move(0), steps++;
  CHECK(gone[0] || gone[1], "snakes: into the wall, out");
  CHECK(WINNER >= -1, "snakes: the round decided");

  app_test_round(M2, G_SNAKES); /* into another snake */
  for (int k = 0; k < NSTAR; k++) star[k] = -1;
  clear_snake(0), clear_snake(1);
  int a[4] = {at(10, 10), at(11, 10), at(12, 10), at(13, 10)}, b[4] = {at(14, 7), at(14, 8), at(14, 9), at(14, 10)};
  for (int i = 0; i < 4; i++) { /* 0 goes right into 1's head's cell... 1 goes down past it */
    s[0].cell[i] = (uint16_t)a[i], grid[10][10 + i] = 1;
    s[1].cell[i] = (uint16_t)b[i], grid[7 + i][14] = 2;
  }
  s[0].hd = s[1].hd = 3, s[0].len = s[1].len = 4, s[0].dir = 0, s[1].dir = 1;
  one_move(0);
  CHECK(gone[0] && !gone[1] && WINNER == 1, "snakes: into a snake's body, out");

  app_test_round(M2, G_SNAKES); /* head to head */
  for (int k = 0; k < NSTAR; k++) star[k] = -1;
  clear_snake(0), clear_snake(1);
  for (int i = 0; i < 4; i++) {
    s[0].cell[i] = (uint16_t)at(5 + i, 10), grid[10][5 + i] = 1;
    s[1].cell[i] = (uint16_t)at(14 - i, 10), grid[10][14 - i] = 2;
  }
  s[0].hd = s[1].hd = 3, s[0].len = s[1].len = 4, s[0].dir = 0, s[1].dir = 2;
  one_move(0);
  CHECK(!gone[0] && !gone[1], "snakes: one cell apart, still in");
  one_move(0);
  CHECK(gone[0] && gone[1] && WINNER == -1, "snakes: head to head, both out: a draw");

  app_test_round(M3V3, G_SNAKES); /* six snakes on the board */
  int n = 0;
  for (int yy = 0; yy < GH; yy++)
    for (int xx = 0; xx < GW; xx++) n += grid[yy][xx] != 0;
  CHECK(n == 24, "snakes: six snakes of four, none on another");
  run(0, 150);
  CHECK(alive() == 6, "snakes: six snakes start with room to turn");
}
