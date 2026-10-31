/* Bad Time Simulator: the screens (draw.c). */
#ifndef BT_DRAW_H
#define BT_DRAW_H
#include "fight.h"

/* a menu screen: a title, a list with the heart beside the chosen line, some words, a key line */
typedef struct {
  const char *title;
  int title_y;
  const char *const *items;
  int n, sel, first, rows, list_x, list_y, step;
  bool more_up, more_down;
  const char *body;
  int body_x, body_y;
  const char *foot, *keys;
  bool judge;                /* the Judge in the middle, his feet at judge_y */
  int judge_y;
  uint8_t judge_head;
  uint32_t tick;
} screen_t;

void draw_fight(const fight_t *f, const char *title);
void draw_screen(const screen_t *s);
void draw_pause(const fight_t *f, const screen_t *s);   /* (title, items, sel, keys) over the fight */
#endif
