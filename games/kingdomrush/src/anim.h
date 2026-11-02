/* Kingdom Rush: which animation (AN_*, data.h) each enemy plays. */
#ifndef KR_ANIM_H
#define KR_ANIM_H
#include <stdint.h>
#include "data.h"

enum { A_WALK, A_UP, A_DOWN, A_IDLE, A_FIGHT, A_DEAD };
extern const int16_t enemy_anim[EN_COUNT][6];   /* stats.c; -1: none */
static inline int ANIM(int type, int a) {
  int an = enemy_anim[type][a];
  if (an < 0 && a != A_WALK) an = enemy_anim[type][a == A_DEAD || a == A_FIGHT || a == A_IDLE ? A_WALK : A_WALK];
  return an;
}

/* enemy2.c */
#include <stdbool.h>
struct Enemy;
extern int enemy_health_pct;   /* the difficulty: 80, 100 or 120 */
bool enemy_area_attack(const struct Enemy *e);
void egg_lay(struct Enemy *e);
void eggs_step(void);
int skeletons(void);
void summon_skeleton(struct Enemy *e, int k);
void demon_explode(struct Enemy *e);
bool boss_power(struct Enemy *e);
bool veznan_power(struct Enemy *e);
void enemy_armor_down(struct Enemy *e, int v);
#endif
