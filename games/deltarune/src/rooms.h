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
  EV_CHASE_BATTLE = 299,
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
Obj *room_add_npc(int obj, int x, int y);   /* (an npc a cutscene makes) */
bool room_open(int room);          /* the chapter goes there so far */
bool talk_msc(int msc, int side);  /* an npc's scr_text(msc) in the box, its choices going on (side 0 top, -1 by Kris) */
void room_queue_event(int id);     /* the room's cutscene, started once nothing else runs */  /* the name the SAVE menu shows */

/* each part's own (lw.c the Light World, dw.c the Dark World...) */
void lw_setup(int room);
int lw_event(Ev *c);
void lw_step(void);
void lw_draw(void);
void room_draw_under(void);        /* what a room draws on its floor, under everyone (glowing tiles...) */
void dw_setup(int room);
int dw_event(Ev *c);
void dw_step(void);
void dw_draw(void);
/* the overworld's enemies (chase.c, obj_chaseenemy) */
void chasers_setup(void);
void chasers_step(void);
void chasers_draw(void);
void chaser_add(int x, int y, int radius);
bool chasers_any(void);
void chaser_kick(int vs);
void ow_damage(int dmg);           /* (forest.c) an overworld bullet's hit on the party */
int chase_battle_event(Ev *c);
/* the areas past Castle Town: the Field (field.c), the Forest (forest.c), Card Castle (castle.c) */
void field_setup(int room);
void field_step(void);
void field_draw(void);
int field_event(Ev *c);
void board_setup(int room);           /* the Great Board (board.c) */
void board_step(void);
void board_draw(void);
void board_draw_under(void);
int board_event(Ev *c);
void forest_setup(int room);
void forest_step(void);
void forest_draw(void);
int forest_event(Ev *c);
int forest_maze_door(int *entrance);
void castle_setup(int room);
void castle_step(void);
void castle_draw(void);
int castle_event(Ev *c);
#endif
