/* Installed games: each one is a block of flash between its head and tail
 * markers (see tools/gen_games.py). Uninstalling wipes the block, so a game
 * counts as installed only while both markers are intact. */
#include <string.h>
#include "np.h"
#include "sys.h"

#define HEAD_MAGIC 0x3147504Eu /* "NPG1" */
#define TAIL_MAGIC 0x444E4550u /* "PEND" */

#if NP_SIMULATOR
bool np_sim_removed[NP_MAX_GAMES];
#endif

bool np_game_installed(int i) {
  const np_game_t *g = &np_games[i];
#if NP_SIMULATOR
  (void)g;
  return !np_sim_removed[i];
#else
  /* volatile: this flash changes under the compiler's feet when uninstalling */
  const volatile uint32_t *head = g->begin, *tail = g->end - 1;
  return head[0] == HEAD_MAGIC && tail[0] == TAIL_MAGIC;
#endif
}

uint32_t np_game_size(int i) {
  const np_game_t *g = &np_games[i];
#if NP_SIMULATOR
  uint32_t shots = 0;
  for (int k = 0; k < g->nshots; k++) shots += g->shots[k].zsize + 2 * g->shots[k].ncolors;
  return g->est_size + shots;
#else
  return (uint32_t)((const uint8_t *)g->end - (const uint8_t *)g->begin);
#endif
}

uint32_t np_game_ram_missing(int i) {
#if NP_SIMULATOR
  (void)i;
  return 0;
#else
  const np_game_t *g = &np_games[i];
  uint32_t data = (uint32_t)(g->data + g->data_size - np_arena), bss = (uint32_t)(g->bss + g->bss_size - np_arena);
  uint32_t need = data > bss ? data : bss, room = np_arena_room();
  return need > room ? need - room : 0;
#endif
}

void np_game_run(int i) {
  const np_game_t *g = &np_games[i];
  if (np_game_ram_missing(i)) return; /* (the home screen says so and doesn't start it) */
  /* fresh RAM, as if the game had just been launched from the home screen */
  if (g->data_size) memcpy(g->data, g->data_init, g->data_size);
  if (g->bss_size) memset(g->bss, 0, g->bss_size);
  g->main();
}
