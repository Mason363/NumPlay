/* Live backgrounds for the home screen, ported from NumVisuals: drawn straight
 * into the strip being rendered, then dimmed a little so the text stays
 * readable. Their small tables live in the arena; when there is no room for
 * them the home screen keeps its plain gradient. */
#ifndef NP_LIVE_H
#define NP_LIVE_H
#include "gfx.h"

/* Aurora, Sunset Drive, Plasma, Pastel, Lava Lamp, Warp, Tunnel, Fire, Ocean, Bokeh, Code Rain */
#define LIVE_COUNT 11

void live_init(void);              /* after ui_init has taken its buffers, the tables of the background chosen */
bool live_ready(void);             /* the tables are there: false after live_set to one that needs more (ui_init) */
bool live_active(void);            /* ready, and a background is chosen */
int live_mode(void);               /* 0..LIVE_COUNT-1, or LIVE_COUNT for "none" */
void live_set(int mode);
void live_next(void);              /* the next background, then none, then the first again */
void live_frame(uint32_t now_ms);  /* once per frame, before rendering */
void live_strip(void);             /* fills the current strip (gfx_y0..gfx_y1) */
#endif
