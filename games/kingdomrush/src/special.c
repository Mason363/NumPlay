/* The special towers (TowerArcherRanger, TowerArcherMusketeer, TowerMageArcane, TowerMageSorcerer,
 * TowerEngineerBfg, TowerEngineerTesla, TowerSoldierPaladin, TowerSoldierBarbarian), their abilities and
 * soldiers; and the hits every mage bolt and bomb share. */
#include <math.h>
#include <string.h>
#include "special.h"
#include "settings.h"
#include "anim.h"

int special_kind(int kind, int choice) {
  switch (kind) {
  case TW_ARCHER: return choice == 1 ? TW_MUSKETEER : TW_RANGER;
  case TW_MAGE: return choice == 1 ? TW_ARCANE : TW_SORCERER;
  case TW_ENGINEER: return choice == 1 ? TW_TESLA : TW_BFG;
  case TW_BARRACKS: return choice == 1 ? TW_PALADIN : TW_BARBARIAN;
  default: return kind;
  }
}

int special_abilities(int s, Ability *a) { (void)s, (void)a; return 0; }
void spot_buy_ability(int s, int k) { (void)s, (void)k; }
int spot_ability_cost(int s, int k) { (void)s, (void)k; return -1; }
void special_draw_bullet(const Bullet *b, int x, int y) { (void)b, (void)x, (void)y; }
void special_init(int s) { (void)s; }
void special_sold(int s) { (void)s; }
void special_timers(int s) { (void)s; }
bool special_run(int s) { (void)s; return false; }
bool special_shoot(int s) { (void)s; return false; }
void special_arrow(int s, Bullet *b) { (void)s, (void)b; }
void special_bolt(int s, Bullet *b) { (void)s, (void)b; }
void special_bomb(int s, Bullet *b) { (void)s, (void)b; }
void special_bullet(Bullet *b) { b->alive = 0; }
void special_scorched_earth(float x, float y) { (void)x, (void)y; }
void special_rally(int s, Pt p) { (void)s, (void)p; }
void special_elemental_stats(Soldier *so, int spot) { (void)so, (void)spot; }
bool special_soldier(Soldier *so) { (void)so; return false; }
void special_soldier_stop(Soldier *so) { (void)so; }
int special_soldier_damage(Soldier *so, int dmg) { (void)so; return dmg; }
void special_soldier_attack_anim(Soldier *so) { (void)so; }

void special_poison(Enemy *e, int level) {
  if (e->poison_t && e->poison_lvl > level) return;
  e->poison_lvl = (int16_t)level, e->poison_t = S_archers_ranger_poisonDuration, e->poison_tick = 0;
}

/* Bolt.hitOnTarget: magic damage; Slow Curse halves its speed half a second, Arcane Shatter takes 3 armour */
void special_bolt_hit(Bullet *b, Enemy *e) {
  bool slow = up_on(UP_MAGES, 5) && !e->rage_t && !e->slow_t && !e->net_t;
  enemy_damage(e, rnd_dmg(b->min_dmg, b->max_dmg), ARM_M, 0);
  if (slow && !e->dead) {
    e->slow_speed = e->speed, e->speed /= 2, e->margin = e->speed + 0.1f;
    e->slow_t = 15;
  }
  if (up_on(UP_MAGES, 2) && e->active) enemy_armor_down(e, 3);
}

void special_bomb_hit(Bullet *b) { bomb_blast(b); }
