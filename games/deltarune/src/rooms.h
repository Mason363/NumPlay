/* Deltarune: the rooms' things and events (rooms.c, lw.c, dw.c...). */
#ifndef DR_ROOMS_H
#define DR_ROOMS_H
#include <stdbool.h>
#include "event.h"

/* the events (Obj.event, event_start) */
enum {
  EV_NONE, EV_DOOR, EV_SAVE, EV_READ, EV_NPC, EV_ROOM, EV_TREASURE, EV_MENU_SAVE,
  EV_LW_FIRST = 100,
  EV_DW_FIRST = 200,
  EV_FIELD_FIRST = 300,
  EV_FOREST_FIRST = 400,
  EV_CASTLE_FIRST = 500,
};

/* the actors scripts find by name (Obj.name) */
enum { ACT_NONE, ACT_TORIEL, ACT_SUSIE, ACT_RALSEI, ACT_LANCER, ACT_NOELLE, ACT_ALPHYS, ACT_PROP, ACT_PROP2,
       ACT_PROP3, ACT_KING, ACT_CLOVER, ACT_SAVE, ACT_DOOR, ACT_DOOR2,
       ACT_NPC1, ACT_NPC2, ACT_NPC3, ACT_NPC4 };

void room_setup(int room);         /* the room's things, as it starts */
void room_menu(void);              /* C: the menu */
void room_draw_over(void);         /* what a room draws over everything (a cutscene's) */
int room_next(int room, int n);    /* n rooms after it in the original's order (before: n < 0) */
void room_step(void);              /* each frame in the overworld: the room's own (its cutscenes start) */
const char *room_title(int room);
void room_queue_event(int id);     /* the room's cutscene, started once nothing else runs */  /* the name the SAVE menu shows */

/* each part's own (lw.c the Light World, dw.c the Dark World...) */
void lw_setup(int room);
int lw_event(Ev *c);
void lw_step(void);
void lw_draw(void);
void dw_setup(int room);
int dw_event(Ev *c);
void dw_step(void);
void dw_draw(void);
#endif
