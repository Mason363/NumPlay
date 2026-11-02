/* Deltarune: the game's state and its modes (title, overworld, battle, menus).
 *
 * Inspired by Deltarune by Toby Fox, not affiliated with him.
 * Made by Mason Chen as part of NumPlay. */
#ifndef DR_GAME_H
#define DR_GAME_H
#include <stdbool.h>
#include <stdint.h>

static inline int abs_i(int v) { return v < 0 ? -v : v; }
static inline int clamp_i(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static inline int min_i(int a, int b) { return a < b ? a : b; }
static inline int max_i(int a, int b) { return a > b ? a : b; }
static inline int sgn_i(int v) { return (v > 0) - (v < 0); }

enum { MODE_TITLE, MODE_WORLD, MODE_BATTLE, MODE_MENU, MODE_GAMEOVER, MODE_SHOP, MODE_END, MODE_INTRO };

/* the party's members (global.char[]: 1 Kris, 2 Susie, 3 Ralsei) */
enum { CH_NONE, CH_KRIS, CH_SUSIE, CH_RALSEI };
#define NFLAGS 1024
#define NITEMS 12

typedef struct {
  /* what a SAVE keeps (the original's globals) */
  char name[12];               /* global.truename, the vessel's */
  int16_t plot;                /* global.plot: how far the story is */
  uint8_t chr[3];              /* global.char[]: the party, in its order */
  int16_t hp[4], maxhp[4];     /* by member (1-3) */
  int16_t at[4], df[4], mag[4];
  uint8_t weapon[4], armor1[4], armor2[4];
  uint8_t item[NITEMS], keyitem[NITEMS], weapons[NITEMS], armors[NITEMS];
  int32_t gold, xp;
  uint8_t lv;
  /* the Light World's own */
  uint8_t litem[8], lweapon, larmor;
  int16_t lhp, lmaxhp;
  int32_t lgold;
  uint32_t time;               /* frames played */
  int16_t room, x, y;          /* where it was saved */
  uint8_t facing;
  uint8_t darkzone;
  int16_t flag[NFLAGS];        /* global.flag[] */
  uint8_t chased[64];          /* the overworld enemies already fought or spared (by room and place) */
} Save;
extern Save sv;
extern int g_mode;
extern uint32_t g_frame;
extern bool g_running;

static inline int flag(int f) { return (unsigned)f < NFLAGS ? sv.flag[f] : 0; }
static inline void set_flag(int f, int v) {
  if ((unsigned)f < NFLAGS) sv.flag[f] = (int16_t)v;
}
static inline int plot(void) { return sv.plot; }
static inline void set_plot(int p) { sv.plot = (int16_t)p; }

void game_init(void);
void game_frame(uint32_t keys);
void game_draw(void);
void game_quit_save(void);
void game_new(void);
uint32_t rnd(void);
int rnd_n(int n);               /* 0 .. n - 1 */
extern uint32_t g_pressed, g_held;   /* the keys pressed this frame, held */

/* the party */
bool have_char(int c);
int party_count(void);
void party_add(int c);          /* scr_getchar */
void party_remove(int c);       /* scr_losechar */
#endif
