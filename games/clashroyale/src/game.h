/* Clash Royale: the screens around the battle, and what is kept. */
#ifndef CR_GAME_H
#define CR_GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "sim.h"

typedef struct {
  uint8_t deck[8];
  int32_t trophies, best, wins, losses, draws, three;
  uint8_t guide;   /* how to play seen */
  uint8_t pad[3];
} Profile;

enum { SC_TITLE, SC_DECK, SC_HELP, SC_BATTLE, SC_PAUSE, SC_RESULT };

typedef struct {
  bool running;
  int screen, back_to;              /* back_to: where How to play returns */
  Profile prof;
  uint32_t keys, prev, held_ms;     /* keys now, last step's; how long the arrows have been held */
  uint32_t ms;                      /* time on this screen */
  int sel;                          /* the menu's choice */
  /* deck */
  int drow, dcol, dscroll, hold;    /* hold: a card picked (-1 none), held from the deck (slot + 100) or collection */
  /* battle */
  bool field;                       /* the cursor on the field (else the hand) */
  int slot, cx, cy;                 /* the card chosen, the cursor's tile */
  int msg_t;
  const char *msg;
  int steps;                        /* game steps (60 a second): a battle step each 3 */
  bool confirm;                     /* pause: quit asked */
  int dtroph;                       /* the result's trophies */
  int old_arena;
} Game;

extern Game g;
extern Battle bt;

void game_init(void);
void game_step(uint32_t keys);      /* 60 a second */
void game_draw(void);
int arena_of(int32_t trophies);
const char *arena_name(int a);

/* view.c: the battle on screen */
void view_load_arena(int a);
void view_prepare(void);           /* once a frame, before drawing */
void view_battle(void);
void view_hud(void);

/* save.c */
void save_load(void);
void save_write(void);
bool resume_load(void);             /* a battle kept by Home: true when it is back */
void resume_write(void);
void resume_clear(void);
#endif
