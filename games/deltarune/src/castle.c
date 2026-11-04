#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* Card Castle (rooms.h): the prison, Lancer, the elevator, its floors and Rouxls Kaard's puzzles, the King,
 * the fountain. */
#include <string.h>
#include "battle.h"
#include "cut.h"
#include "game.h"
#include "gfx.h"
#include "puzzles.h"
#include "rooms.h"

void castle_setup(int room) { (void)room; }
void castle_step(void) {}
void castle_draw(void) {}
int castle_event(Ev *c) {
  (void)c;
  return 1;
}
