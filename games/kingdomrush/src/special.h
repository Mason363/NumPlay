/* Kingdom Rush: the special towers' abilities and their soldiers (special.c), and what tower.c shares. */
#ifndef KR_SPECIAL_H
#define KR_SPECIAL_H
#include "kr.h"

/* tower.c */
bool tower_in_range(const Spot *t, float x, float y);
int tower_find_target(Spot *t, bool fliers, bool predicted);
void barracks_step(int s);
bool barracks_door_closed(int s);
bool barracks_door_opening(int s);
int barracks_door_t(int s);
void barracks_open(int s);

/* special.c */
typedef struct {
  uint8_t icon, pos, level, max;   /* its menu icon and place (TowerMenu's), its level and top */
  int16_t cost;                    /* the next level's price */
  const char *key;                 /* its name (strings.c) */
} Ability;
int special_abilities(int s, Ability *a);   /* up to 3 */
int special_kind(int kind, int choice);      /* the tower a level 3 becomes: choice 1 or 2 */
void special_init(int s);                   /* a tower finished, upgraded or turned special */
void special_sold(int s);
void special_timers(int s);                 /* runSpecialTimers */
bool special_run(int s);                    /* runSpecial: an ability used instead of a shot */
bool special_shoot(int s);                  /* the special towers' own shots */
void special_arrow(int s, Bullet *b);
void special_bolt(int s, Bullet *b);
void special_bomb(int s, Bullet *b);
void special_bolt_hit(Bullet *b, Enemy *e);
void special_bomb_hit(Bullet *b);
void special_bullet(Bullet *b);
void special_poison(Enemy *e, int level);
void special_scorched_earth(float x, float y);
void special_rally(int s, Pt p);
/* soldiers */
void special_elemental_stats(Soldier *so, int spot);
bool special_soldier(Soldier *so);          /* runSpecial */
void special_soldier_stop(Soldier *so);
int special_soldier_damage(Soldier *so, int dmg);
bool special_soldier_attack_anim(Soldier *so);   /* true: a holy strike instead of the blow */
void portal_open(Pt at, int most, int nodes);
void portals_step(void);
void polymorph(Enemy *e);
void special_elemental_hit(Soldier *so);
void special_new_soldier(Soldier *so);       /* a paladin or barbarian gets its tower's abilities */
void bomb_blast(Bullet *b);
void special_draw_bullet(const Bullet *b, int x, int y);
#endif
