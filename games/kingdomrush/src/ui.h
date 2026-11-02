/* Kingdom Rush: the battle's pictures and menus (ui.c). */
#ifndef KR_UI_H
#define KR_UI_H
#include "kr.h"

enum {
  RA_NONE, RA_ARCHER, RA_BARRACKS, RA_MAGE, RA_ENGINEER, RA_UPGRADE, RA_SPECIAL1, RA_SPECIAL2, RA_SELL, RA_RALLY,
  RA_ABILITY0, RA_ABILITY1, RA_ABILITY2, RA_SITE_BUY, RA_SITE_RAY
};
typedef struct {
  uint8_t action, icon, locked, level, max;
  int16_t cost, x, y, range;   /* x, y: from the menu's middle, on screen */
  char name[40];
} RingItem;
int ring_items(int s, RingItem *out);
void draw_ring(const RingItem *it, int n, int sel, int cx, int cy, int cash);

void draw_tower(const Spot *t, int x, int y);
void draw_base(const Spot *t, int x, int y, int terrain);
void draw_bullet(const Bullet *b, int x, int y);
void draw_wave_flag(int x, int y, bool air, uint32_t frame, bool selected);
void draw_sidebar(int aiming);
void draw_enemy_info(const Enemy *e);
void draw_spot_info(const Spot *t);
void draw_key_line(int x0, const char *text, const RingItem *item, int cash);
void draw_pause(int sel);
void draw_quit(int sel);
void draw_note(int note);
void draw_result(bool won, int stars, int mode, int sel, int t);
const char *tower_name(int kind, int level);
#endif
