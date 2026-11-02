#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Dark World (rooms.h): from the fall to Castle Town, the Field, the Great Board, the Forest and the
 * Card Castle. Each cutscene is the original's event object as a protothread. */
#include <string.h>
#include "battle.h"
#include "event.h"
#include "game.h"
#include "rooms.h"
#include "text.h"
#include "world.h"

void dw_setup(int room) { (void)room; }
void dw_step(void) {}
int dw_event(Ev *c) {
  (void)c;
  return 1;
}
