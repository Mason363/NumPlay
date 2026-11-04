#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Forest (rooms.h): the Bake Sale, Susie and Lancer, the suits puzzles, Clover's holes, the dancers, the
 * maze, the Thrash Machine, Susie and Lancer's fight and the way to Card Castle. */
#include <string.h>
#include "battle.h"
#include "cut.h"
#include "game.h"
#include "gfx.h"
#include "puzzles.h"
#include "rooms.h"

void forest_setup(int room) { (void)room; }
void forest_step(void) {}
void forest_draw(void) {}
int forest_event(Ev *c) {
  (void)c;
  return 1;
}
/* the maze's X door (obj_doorX in room_forest_maze1 and 2) */
int forest_maze_door(int *entrance) {
  (void)entrance;
  return g_room;
}
