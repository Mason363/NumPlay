/* The app: its screens (main.c moves between them, screens.c draws them). */
#ifndef FNF_APP_H
#define FNF_APP_H
#include "game.h"
#include "save.h"

enum { V_TITLE, V_MAIN, V_STORY, V_FREEPLAY, V_OPTIONS, V_HELP, V_CREDITS, V_PLAY, V_PAUSE, V_QUIT, V_RESULTS,
       V_GAMEOVER, V_ERROR };
enum { MAIN_STORY, MAIN_FREEPLAY, MAIN_OPTIONS, MAIN_HELP, MAIN_CREDITS, MAIN_COUNT };
enum { PAUSE_RESUME, PAUSE_RESTART, PAUSE_EXIT, PAUSE_OPTIONS, PAUSE_HELP, PAUSE_QUIT, PAUSE_COUNT };
enum { OPT_DOWNSCROLL, OPT_OFFSET, OPT_COUNT };
#define HELP_PAGES 3
enum { LIST_BF, LIST_PICO };

typedef struct {
  uint8_t view, under;            /* the screen, and what a pause, options or how to play sits on */
  uint8_t sel;                    /* the menu's choice */
  uint8_t page, first_run;        /* how to play; first_run: before the first song (OK starts it) */
  uint8_t story, story_song;      /* a week being played, and which of its songs */
  uint8_t list_n;                 /* freeplay: the songs in the list */
  uint8_t list[FNF_SONGS];
  int32_t campaign;               /* the week's score so far */
  uint16_t week_tally[5], week_total, week_max_combo;
  uint8_t result_rank, result_new;   /* the results screen: its rank, a new best */
  int32_t result_score, result_completion;
  uint16_t result_tally[5], result_total, result_max_combo;
  uint32_t clock;                 /* ms, for the menus' beat */
  uint32_t menu_at;               /* when the screen came up */
  uint32_t play_t0;               /* when the song's screen came up (its arrows fade in) */
  uint32_t over_at, over_anim_at; /* the game over: when it came up, and when its animation started */
  uint8_t over_anim;              /* 0 firstDeath, 1 deathLoop, 2 deathConfirm (OK pressed: then it fades out) */
  uint8_t fade_steps;             /* (the fades go in ten steps) */
  uint32_t fade_at;               /* a song started again from the game over comes up from black (0: not) */
  fnf_progress_t *p;
  fnf_game_t *g;
} app_t;

/* the songs and charts on offer */
int app_owner(unsigned chr);                                  /* 0 nobody's, 1 Boyfriend's, 2 Pico's */
int app_chart(unsigned song, int list, int diff);             /* freeplay: a chart, or -1 */
int app_story_chart(unsigned song, int diff);                 /* story mode: a chart, or -1 */
unsigned app_story_diffs(unsigned level);                     /* the difficulties a week has (bits) */
void app_build_list(app_t *a);

void screens_draw(app_t *a);
const char *app_keys(const app_t *a);
extern const char *const DIFF_NAME[FNF_DIFFS];
#endif
