/* Moto X3M: the level and the bike on the screen. */
#ifndef MX_VIEW_H
#define MX_VIEW_H
#include <stdint.h>

extern uint32_t view_ticks;   /* game ticks, for animations */
/* the drawings in view (24 bytes each) and screen points, given room on main's stack */
#define VIEW_MAXVIS 300
#define VIEW_VIS_BYTES (24 * VIEW_MAXVIS)
#define VIEW_SCR_FLOATS (2 * 304)
void view_init(void *visbuf, float *scrbuf);
void view_reset(void);
/* once a frame, before drawing: which drawings are in view */
void view_prepare(void);
/* once a band */
void view_draw_world(void);
#endif
