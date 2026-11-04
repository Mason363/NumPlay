/* The rules' tests: each file takes one minigame's source in, to look inside it (tests/rules/main.c runs them). */
#ifndef FP_T_H
#define FP_T_H
#include <stdio.h>
#include "../../src/app.h"
#include "../../src/fp.h"

extern int t_failed, t_passed;
#define CHECK(c, what)                                                \
  do {                                                                \
    if (c) t_passed++;                                                \
    else t_failed++, printf("FAIL %s:%d %s\n", __FILE__, __LINE__, what); \
  } while (0)
enum { M2, M3, M4, M2V2, M3V3 };
/* steps with the keys held (a bit a player) until the round is decided or n steps; the steps taken */
static inline int run(unsigned held, int n) {
  int i = 0;
  while (i < n && app_test_winner() == -2) app_test_step(held), i++;
  return i;
}
#define WINNER app_test_winner()
#define UNDECIDED (-2)
#endif
