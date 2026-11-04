/* Imposter: the players, the settings, the rounds and the save (game.c). */
#ifndef IM_GAME_H
#define IM_GAME_H
#include <stdbool.h>
#include <stdint.h>
#include "words.h"

#define MIN_PLAYERS 3
#define MAX_PLAYERS 12
#define NAME_MAX 10 /* letters */
#define N_TIMERS 5
#define NO_WORD 0xFFFF

/* where a round is */
enum { PH_NONE, PH_PASS, PH_TALK, PH_REVEAL };

/* imposter.sav: everything, written whenever something changes */
typedef struct {
  char magic[4];  /* "IMPS" */
  uint8_t version, nplayers, imposters, hint; /* imposters: the setting (1-3); hint: the imposter sees the category */
  uint8_t timer, phase, turn, starter;        /* timer: 0 off, then timer_minutes[]; turn: who sees their role next */
  uint8_t flags, pad[3];                      /* flags: 1 How to play was shown */
  uint16_t word, last;                        /* the round's word, the one before: category * 64 + its number */
  uint16_t imps, rounds;                      /* the round's imposters (a bit a player) */
  uint32_t cats;                              /* the categories chosen (a bit each); 0: all */
  uint32_t time_left;                         /* ms left on the discussion timer */
  char names[MAX_PLAYERS][NAME_MAX + 1];
  uint8_t used[N_CATS * 8];                   /* the words already played, 64 bits a category */
} save_t;

extern save_t sv;
extern const uint8_t timer_minutes[N_TIMERS];

void game_load(void);
void game_save(void);
void game_defaults(void);

int max_imposters(int players);
int imposters_now(void); /* the setting, as the players allow */
bool cat_on(int c);
int cats_chosen(void);
bool is_imposter(int p);
int word_text(uint16_t w, char *out); /* the word's text; its category in the result */

/* a round: its word (never one played until its category runs out), its imposters, who starts */
void round_new(void);
void round_end(void);
uint32_t rnd(void);
void rnd_seed(uint32_t s);
void rnd_stir(uint32_t v);
#endif
