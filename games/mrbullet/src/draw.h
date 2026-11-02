/* Mr Bullet: drawing (draw.c), a strip of 16 rows at a time with the launcher's gfx (its fonts and panels) and the
 * game's pictures. The play field (440 x 568 of the game's units) fills the screen's height in its middle. */
#ifndef MB_DRAW_H
#define MB_DRAW_H
#include <stdbool.h>
#include <stdint.h>
#include "gfx.h"

#define FIELD_X0 67
#define FIELD_W 186
#define FIELD_S (240.0f / 568.0f)   /* pixels a unit */
#define SX(x) (160.0f + (x) * FIELD_S)
#define SY(y) (120.0f + (y) * FIELD_S)

#define COL_PANEL HEX(0x1B2633)
#define COL_PANEL_LIGHT HEX(0x2C3A4A)
#define COL_INK HEX(0x0E141B)
#define COL_TEXT 0xFFFF
#define COL_MUTED HEX(0x9AA8B6)
#define COL_FOCUS HEX(0xFFD23F)
#define COL_RED HEX(0xE8392F)

void draw_pic(int pic, int x, int y);
/* a picture with its pivot at screen (sx, sy), scaled and turned (sine and cosine), mirrored if flip */
void draw_pic_at(int pic, float sx, float sy, float scale, float sn, float cs, bool flip);
void draw_line(float x0, float y0, float x1, float y1, color_t c);
void draw_disc(float cx, float cy, float r, color_t c);
/* the level as it is now, in the field (and the aim when `aim`) */
void draw_world(bool aim);
/* the side panels: the bullets and the level */
void draw_hud(const char *title, const char *keys1, const char *keys2);
#endif
