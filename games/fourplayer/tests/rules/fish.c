#include "../../src/g_fish.c"
#include "t.h"

/* the next thing, out of the water now */
static void jumps(int what) {
  item = what, stage = 0, item_t = rt + 10;
  app_test_step(0), app_test_step(0);
  for (int i = 0; i < 9; i++) app_test_step(0); /* (80 ms before a paw can reach it) */
}

void rules_fish(void) {
  app_test_round(M2, G_FISH);
  jumps(FISH);
  CHECK(stage == 1, "fish: a goldfish out of the water");
  app_test_step(2), app_test_step(0);
  CHECK(caught[1] == 1 && caught[0] == 0 && stage == 0, "fish: the first paw on a goldfish keeps it");
  app_test_step(1), app_test_step(0); /* nothing out */
  CHECK(dizzy(0), "fish: a swipe at nothing, dizzy");
  jumps(FISH);
  app_test_step(1), app_test_step(0);
  CHECK(caught[0] == 0, "fish: dizzy, no catching");

  app_test_round(M2, G_FISH);
  jumps(BOOT);
  app_test_step(1), app_test_step(0);
  CHECK(caught[0] == 0 && dizzy(0) && dizzy_ms[0] == 1800, "fish: a swipe at a boot, dizzier");
  for (int k = 0; k < 3; k++) jumps(FISH), app_test_step(2), app_test_step(0);
  CHECK(caught[1] == 3 && WINNER == 1, "fish: three goldfish take the round");

  app_test_round(M2V2, G_FISH); /* teams: three between you */
  jumps(FISH), app_test_step(1), app_test_step(0);
  jumps(FISH), app_test_step(4), app_test_step(0);
  CHECK(tally[0] == 2 && WINNER == UNDECIDED, "fish: teams count together");
  jumps(FISH), app_test_step(4), app_test_step(0);
  CHECK(WINNER == 0, "fish: three for the red team");

  app_test_round(M4, G_FISH); /* things come, then go back in */
  int fish_ = 0, junk = 0;
  for (int i = 0; i < 300; i++) {
    stage = 0, item_t = rt + 1;
    next_item(0);
    fish_ += item == FISH, junk += item != FISH;
  }
  CHECK(fish_ > 130 && junk > 100, "fish: about half goldfish, half junk");
  jumps(CAN);
  run(0, 150);
  CHECK(stage == 0 && grabber == -1, "fish: nobody swipes, it falls back in");
}
