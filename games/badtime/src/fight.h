/* Bad Time Simulator: the fight around the rules (bt.h): the turns, the menu, the dodge, the words, the end. */
#ifndef BT_FIGHT_H
#define BT_FIGHT_H
#include "bt.h"

enum { MODE_NORMAL, MODE_PRACTICE, MODE_ENDLESS, MODE_SINGLE };
enum {
  FS_ATTACK,     /* an attack */
  FS_RETURN,     /* the zone going back to the menu's */
  FS_MENU,       /* FIGHT ACT ITEM MERCY */
  FS_SUB,        /* a list in the box */
  FS_TARGET,     /* FIGHT's bar */
  FS_DODGE,      /* he dodges */
  FS_INFO,       /* words in the box, then the attack */
  FS_DEATH,      /* the heart breaks */
  FS_LOST,       /* the end of a lost fight */
  FS_WIN,        /* the last words */
  FS_WON,        /* the end of a won fight */
};
enum { SUB_ENEMY, SUB_ACT, SUB_ITEM, SUB_MERCY };
enum { TXT_NONE, TXT_BOX, TXT_BUBBLE };

typedef struct {
  const char *name, *full;
  uint8_t heal;
} item_t;

#define TEXT_MAX 200
typedef struct {
  world_t w;
  uint8_t mode, state, phase, sel;        /* phase: Endless' 0 or 1 */
  uint8_t sub, sub_sel, act_open, single;  /* the list shown, its cursor, ACT's second list, Single attack's */
  uint8_t next_attack, hit_attempts, practice, failed;
  uint8_t items[8], nitems;
  uint8_t progress;                        /* the furthest attack reached (1..24) in Normal */
  uint8_t win_line, strikes, checked, last_attack;
  int16_t timer;
  /* FIGHT */
  float bar_x;
  int8_t bar_dir;
  uint8_t bar_hit;
  int16_t bar_fade;
  int16_t miss_t, slash_t;
  /* the words */
  char text[TEXT_MAX];
  uint16_t text_len, text_shown;
  uint8_t text_kind, text_after;
  const char *info;                        /* the box's line between attacks */
  /* SUCCESS / FAILURE (Practice) */
  int16_t banner, banner_t;
  /* the heart's pieces */
  shard_t shards[MAX_SHARDS];
  int16_t death_t;
  uint32_t seed, attacks_done;
  uint8_t heart_dirty;
} fight_t;

void fight_start(fight_t *f, int mode, int arg, uint32_t seed);
/* a tick: keys held and keys just pressed (BK_*) */
void fight_tick(fight_t *f, uint32_t keys, uint32_t pressed);
/* the key line's words for now */
const char *fight_keys(const fight_t *f);
bool fight_over(const fight_t *f);        /* lost or won: the end screen */
bool fight_can_pause(const fight_t *f);   /* Back pauses (not in a list, where it goes back) */
int fight_progress_of(int hit_attempts, int next_attack);
void fight_item_pos(int i, float *x, float *y);   /* a list's line i: where the heart goes (the original's pixels) */

/* text.c */
extern const char *const TALK_SCRIPT[2], *const TALK_FIRST[14], *const TALK_BREAK, *const TALK_SECOND[9],
    *const TALK_FINAL, *const TALK_WIN[2];
extern const char *const INFO_DEFAULT, *const INFO_FIRST, *const INFO_KARMA, *const INFO_BREAK, *const INFO_15,
    *const INFO_19, *const INFO_20, *const INFO_21, *const INFO_22, *const CHECK_TEXT, *const CHECK_TEXT2;
extern const item_t ITEMS[4];
#define HELP_PAGES 4
extern const char *const HELP[HELP_PAGES];
#endif
