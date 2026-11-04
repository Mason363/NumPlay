/* Nintendo's Game & Watch and the other LCD games, as LCD-Game-Emulator runs them (gw_system.c and
 * the Sharp SM5xx processors), from the .gw files LCD-Game-Shrinker makes, read in place. */
#ifndef GW_H
#define GW_H
#include "../rg.h"

#define GW_WIDTH 320
#define GW_HEIGHT 240

/* the game's buttons, beyond the pad's (rg.h): the console's own */
enum { GW_KEY_GAME_A = 1, GW_KEY_GAME_B = 2, GW_KEY_TIME = 4, GW_KEY_ALARM = 8 };

/* a .gw file as LCD-Game-Shrinker writes it, not packed (tools/romtable.py and NumPlay Studio
   unpack the LZ4 ones). RG_OK or an RG_ERR_ code. */
int gw_load(const uint8_t *data, uint32_t size);
/* hard: as a new game, the watch set and running; otherwise the ACL button (the processor reset) */
void gw_reset(bool hard);
/* a 64th of a second (two of the emulator's 128 Hz steps); the picture drawn only if a segment changed */
void gw_run_frame(bool draw);
/* the pad (RG_PAD_*) and the console's buttons (GW_KEY_*) held */
void gw_set_pad(int pad, int keys);
/* the console's buttons this game has (GW_KEY_*) */
int gw_keys(void);
/* the picture again, line by line through rg_line */
void gw_redraw(void);
int gw_state_chunks(rg_chunk_t *out);
void gw_state_loaded(void);
#endif
