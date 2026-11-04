/* Drawing (draw.c). */
#ifndef NBCE_DRAW_H
#define NBCE_DRAW_H
#include "nb.h"

/* where drawing goes: a buffer of stride bytes a row, showing the screen's [ox, ox + w) x [oy, oy + h) */
typedef struct {
  uint8_t *p;
  int stride, ox, oy, w, h;
} Canvas;
extern Canvas cv;
void canvas_screen(void);

/* the screens' colours: Minecraft's own, kept exact in the palette (tools/pack.py GUI_FIXED) */
enum {
  C_BLACK = FIRST_STATIC, C_WHITE, C_GRAY, C_SHADOW, C_LIGHT, C_HOVER, C_YELLOW, C_DKYELLOW, C_SLOT, C_SLOTDARK,
  C_PANEL, C_DARK, C_GREEN, C_RED, C_40, C_20, C_70, C_C0
};

void fill(int x, int y, int w, int h, uint8_t c);
void blit(const uint8_t *src, int sw, int x, int y, int w, int h);   /* a buffer's w x h pixels (sw a row) to (x, y) */
uint8_t dark(uint8_t c, int level);
void text_plain(int x, int y, const char *s, uint8_t c);
void text_center(int cx, int y, const char *s, uint8_t c);
void text_big(int x, int y, const char *s, uint8_t c, int scale);
void sprite(int id, int x, int y);
void sprite_part(int id, int x, int y, int sx, int sy, int w, int h);
void icon(int id, int x, int y);
void stack_draw(const Stack *s, int x, int y);
void button(int x, int y, int w, int state, const char *label);   /* state 0 off, 1 on, 2 selected */
void dirt_background(int y0, int y1);

/* the HUD: drawn into its own layer, laid over the 3D view as it goes to the screen */
#define HUD_X 67
#define HUD_W 187
#define HUD_Y 180
#define HUD_H (SCREEN_H - HUD_Y)
extern uint8_t *hud_layer;   /* HUD_W x HUD_H (in the CE's screen memory, after the world) */
extern uint8_t hud_changed;  /* set when it is drawn into */
void compose(bool hud, bool crosshair);   /* the 3D view (rbuf) onto the screen, with the HUD and the crosshair */
#endif
