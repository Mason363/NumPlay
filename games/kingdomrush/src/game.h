/* Kingdom Rush: the screens and the save. */
#ifndef KR_GAME_H
#define KR_GAME_H
#include <stdbool.h>
#include <stdint.h>

enum { SCR_TITLE, SCR_MAP, SCR_UPGRADES, SCR_BATTLE, SCR_HELP };

typedef struct {
  uint8_t camp[12];        /* the campaign's stars (0: not won) */
  uint8_t heroic[12], iron[12];
  uint8_t ups[6];          /* the upgrades bought (each line 0 to 5) */
  uint8_t difficulty;      /* 0 casual, 1 normal, 2 veteran */
  uint8_t guide_seen;
  uint8_t seen_note[8];    /* notifications already shown, a bit each */
} Progress;

typedef struct {
  bool running;
  uint8_t screen;
  uint32_t keys, pressed;  /* held now; pressed this step */
  uint32_t ticks;
  Progress p;
  uint8_t map_sel;         /* the map's selected stage */
} Game;
extern Game g;

void game_init(void);
void game_step(uint32_t keys);
void game_draw(void);
void game_quit(void);

/* stars: won, and free to spend on upgrades */
int stars_won(void);
int stars_free(void);
int stars_max(void);

/* save.c */
void save_progress(void);
void load_progress(void);

/* play.c: the battle screen */
void play_start(int level, int mode);
void play_step(void);
void play_draw(void);
bool play_key(uint32_t k);   /* a key pressed now (with repeats for the arrows) */

/* menus.c: the title, the map, the upgrades, the help */
void title_step(void);
void title_draw(void);
void map_step(void);
void map_draw(void);
void upgrades_step(void);
void upgrades_draw(void);
void help_step(void);
void help_draw(void);
#endif
