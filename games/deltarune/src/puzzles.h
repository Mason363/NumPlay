/* Deltarune: the Dark World's chests and puzzles (puzzles.c). */
#ifndef DR_PUZZLES_H
#define DR_PUZZLES_H
#include <stdbool.h>
#include <stdint.h>
#include "event.h"

/* their events (in dw.c's range) */
enum { EV_CHEST = 280, EV_GLOW_SWITCH, EV_GLOW_HINT, EV_GLOW_SHAKE, EV_SUITS_BUTTON, EV_PUSHBLOCK };

bool party_on(int x0, int y0, int x1, int y1, bool followers);   /* Kris's box (or a follower's) over it */
const char *keyitem_name(int id);
const char *keyitem_desc(int id);

void chests_setup(void);
int chest_event(Ev *c);
void chest_equip_hint(void);                  /* (field.c: the White Ribbon Ralsei talks about) */

void glow_setup(void);
int glow_switch_event(Ev *c);
int glow_hint_event(Ev *c);
int glow_shake_event(Ev *c);

void suits_setup(int x, int y, int max_suit, const int8_t *sol, bool clue);
bool suits_won(void);
int suits_button_event(Ev *c);

void puzzles_reset(void);
void puzzles_step(void);
void puzzles_draw_under(void);
#endif
