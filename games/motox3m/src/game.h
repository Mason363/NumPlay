/* Moto X3M: the game's screens and rules around a run (GameplayState, ChooseZone, Garage, Menu, Inventory). */
#ifndef MX_GAME_H
#define MX_GAME_H
#include <stdbool.h>
#include <stdint.h>

#define NLEVELS 50
#define NSKINS 6

enum { S_TITLE, S_MAP, S_SHOP, S_PLAY, S_PAUSE, S_FINISH, S_HELP };

typedef struct {
  uint8_t stars[NLEVELS];   /* 0xFF: locked */
  float best[NLEVELS];      /* seconds, 0: none */
  uint8_t skin;             /* 1-6 */
  uint8_t owned;            /* bit k: skin k+1 */
  uint8_t seen_help;
} Save;

typedef struct {
  int state, prev_state;
  int level;                /* 0-based */
  int sel;                  /* the menu's choice */
  int map_sel;
  float map_x;              /* the map's scroll */
  int shop_sel;
  bool started;             /* the timer runs (MaxGameCore.drive) */
  float timer;
  float flip_t;             /* the flip message's age */
  int flip_n;
  bool help;                /* "Use arrows to move" */
  float dead_t;
  /* the last finish */
  int stars;
  bool improved;
  float result;
  float finish_t;
  bool confirm;             /* the quit question */
  uint32_t keys, prev;
  bool running;
} Game;

extern Game g;
extern Save save;

void game_init(void);
void game_start_level(int n);   /* past the menus (tests) */
void game_tick(uint32_t keys);   /* 30 a second */
void game_draw(void);
bool save_write(void);
void save_read(void);
int total_stars(void);
#endif
