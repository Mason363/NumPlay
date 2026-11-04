/* The Sega Mega Drive (Genesis), as gwenesis runs it (its 68000, Musashi, its VDP and its
 * ports), with NumPlay's Z80, for Retro-Go's screens. No sound: the calculator has no speaker. */
#ifndef MD_H
#define MD_H
#include "../rg.h"

#define MD_WIDTH 320
#define MD_HEIGHT 240

/* the RAMs (64 KB for the 68000, 8 KB for the Z80) in pages of MD_PAGE bytes, from rg_alloc_page
   (main.c: the heap, then the RAM after the app's own, then a buffer on main's stack) */
#define MD_PAGE 2048
#define MD_PAGES (32 + 4)
void *rg_alloc_page(void);

/* a game read in place, plain and its bytes swapped in pairs (tools/romtable.py). RG_OK or an
   RG_ERR_ code. */
int md_load(const uint8_t *data, uint32_t size);
/* hard: switched off and on; soft: the console's reset button */
void md_reset(bool hard);
void md_run_frame(bool draw);
void md_set_pad(int rg_pad);
/* whether the Z80 runs (Retro-Go's "Z80 emulation": it only plays the sound, most games run
   without it) */
void md_set_z80(bool on);
/* the picture: 256 or 320 wide, 224 or 240 high */
void md_view(int *x, int *y, int *w, int *h);
/* a frame's length in microseconds: 60 a second, 50 for a game in its PAL mode */
uint32_t md_frame_us(void);
void md_redraw(void);
int md_state_chunks(rg_chunk_t *out);
void md_state_loaded(void);
#endif
