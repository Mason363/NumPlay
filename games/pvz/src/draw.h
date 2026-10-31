/* Plants vs. Zombies: drawing the lawn (draw.c) and what is over it (hud.c). */
#ifndef PVZ_DRAW_H
#define PVZ_DRAW_H
#include <stdbool.h>
#include <stdint.h>
#include "game.h"

extern float cam_x;            /* the view's left in the game's pixels, 0 as the lawn is played */
extern uint32_t vis_ticks;     /* hundredths of a second, always running (menus' animations) */
int screen_x(float x);
int screen_y(float y);
int plant_anim(int kind);
void draw_lawn(bool hud);
void draw_plant_at(int kind, int x, int y, int alpha);
void draw_sun(float x, float y, int value, int alpha);
void draw_street(const uint8_t *kinds, int n);
int left_fog_column(void);
bool bowling_line(void);       /* (screens.c) */

/* hud.c: the seed bank, the progress meter, the cursor and the screen's messages */
typedef struct {
  int col, row;                /* the cursor's cell */
  int selected;                /* a packet (or a dropped seed), -1 none */
  bool shovel;
  int issue, issue_t;          /* why it couldn't plant, and since when */
  int ready_t;                 /* "Ready... Set... PLANT!", ticks in */
} Hud;
extern Hud hud;
void hud_draw(bool cursor);
void hud_cursor_draw(void);
void hud_bank_draw(int x, int y, bool costs);
int bank_width(void);
/* cooldown_full < 0: a packet on the conveyor belt (free: no cost on it) */
void packet_draw(int kind, int x, int y, int cooldown, int cooldown_full, bool dim, bool selected, int key);
void draw_ready(int t);
void draw_message_band(const char *s);
void draw_dialog(int x, int y, int w, int h);
void draw_button(int x, int y, int w, const char *label, bool focus);
void draw_money(int x, int y, uint32_t money);
#endif
