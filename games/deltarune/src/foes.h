/* Deltarune: the enemies past Castle Town (foes.c the Field and the Great Board; more to come), and what
 * they share. */
#ifndef DR_FOES_H
#define DR_FOES_H
#include "battle.h"

void flavor_std(Enemy *e, const uint16_t *ids, int n, uint16_t tired, uint16_t low, uint16_t spare);
Bullet *gm_bullet(uint16_t spr, float x, float y, float sp, float dir, int dmg);
void spinheart_attack(Enemy *e, int t);   /* (enemies.c: obj_spinheart, Hathy's alone) */
extern const EnemyDef foes_defs[];
extern const int foes_count;
#endif
