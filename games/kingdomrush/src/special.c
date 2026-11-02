/* The special towers (TowerArcherRanger and its TowerArcherDruid, TowerArcherMusketeer, TowerMageArcane,
 * TowerMageSorcerer, TowerEngineerBfg, TowerEngineerTesla, TowerSoldierPaladin, TowerSoldierBarbarian), their
 * abilities, shots and soldiers (SoldierPaladin, SoldierBarbarian, SoldierElemental); and the hits every mage
 * bolt and bomb share (Bolt, Bomb). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "special.h"
#include "settings.h"
#include "anim.h"
#include "gfx.h"

int special_kind(int kind, int choice) {
  switch (kind) {
  case TW_ARCHER: return choice == 1 ? TW_MUSKETEER : TW_RANGER;
  case TW_MAGE: return choice == 1 ? TW_ARCANE : TW_SORCERER;
  case TW_ENGINEER: return choice == 1 ? TW_TESLA : TW_BFG;
  case TW_BARRACKS: return choice == 1 ? TW_PALADIN : TW_BARBARIAN;
  default: return kind;
  }
}

/* ------------------------------------------------------------------ the abilities: icons, places, prices */
enum {
  IC_ELEMENTAL = 19, IC_POLYMORPH, IC_THORNS, IC_POISON, IC_SNIPER, IC_SHRAPNEL, IC_HOLY, IC_HEALING, IC_SHIELD,
  IC_AXE, IC_NETS, IC_DUAL, IC_CLUSTER, IC_MISSILE, IC_BOLT, IC_OVERCHARGE, IC_DEATH, IC_TELEPORT
};

static int power_cost(int c) {   /* Industrialization: the artillery's abilities a quarter cheaper */
  return up_on(UP_ENGINEERS, 4) ? (int)floorf(c - c * 0.25f) : c;
}

int special_abilities(int s, Ability *a) {
  Spot *t = &B.spot[s];
  const uint8_t *L = t->sp_lvl;
#define AB(i, ic, pos, max, first, next, key) a[i] = (Ability){ic, pos, L[i], max, (int16_t)(L[i] ? (next) : (first)), key}
  switch (t->kind) {
  case TW_RANGER:
    AB(0, IC_POISON, 1, S_archers_ranger_poisonLevels, S_archers_ranger_poisonCost, S_archers_ranger_poisonCostLevel, "TOWER_RANGERS_POISON_NAME");
    AB(1, IC_THORNS, 2, S_archers_ranger_thornLevels, S_archers_ranger_thornCost, S_archers_ranger_thornCostLevel, "TOWER_RANGERS_THORNS_NAME");
    return 2;
  case TW_MUSKETEER:
    AB(0, IC_SNIPER, 1, S_archers_musketeer_sniperLevels, S_archers_musketeer_sniperCost, S_archers_musketeer_sniperCostLevel, "TOWER_MUSKETEERS_SNIPER_NAME");
    AB(1, IC_SHRAPNEL, 2, S_archers_musketeer_shrapnelLevels, S_archers_musketeer_shrapnelCost, S_archers_musketeer_shrapnelCostLevel, "TOWER_MUSKETEERS_SHRAPNEL_NAME");
    return 2;
  case TW_ARCANE:
    AB(0, IC_DEATH, 1, S_mages_arcane_deathRayLevels, S_mages_arcane_deathRayCost, S_mages_arcane_deathRayCostLevel, "TOWER_ARCANE_DESINTEGRATE_NAME");
    AB(1, IC_TELEPORT, 2, S_mages_arcane_teleportLevels, S_mages_arcane_teleportCost, S_mages_arcane_teleportCostLevel, "TOWER_ARCANE_TELEPORT_NAME");
    return 2;
  case TW_SORCERER:
    AB(0, IC_ELEMENTAL, 1, S_mages_sorcerer_elementalLevels, S_mages_sorcerer_elementalCost, S_mages_sorcerer_elementalCostLevel, "TOWER_SORCERER_ELEMENTAL_NAME");
    AB(1, IC_POLYMORPH, 2, S_mages_sorcerer_polymorphLevels, S_mages_sorcerer_polymorphCost, S_mages_sorcerer_polymorphCostLevel, "TOWER_SORCERER_POLIMORPH_NAME");
    return 2;
  case TW_BFG:
    AB(0, IC_CLUSTER, 1, S_engineers_bfg_clusterLevels, power_cost(S_engineers_bfg_clusterCost), power_cost(S_engineers_bfg_clusterCostLevel), "TOWER_BFG_CLUSTER_NAME");
    AB(1, IC_MISSILE, 2, S_engineers_bfg_missileLevels, power_cost(S_engineers_bfg_missileCost), power_cost(S_engineers_bfg_missileCostLevel), "TOWER_BFG_MISSILE_NAME");
    return 2;
  case TW_TESLA:
    AB(0, IC_OVERCHARGE, 1, S_engineers_tesla_overchargeLevels, power_cost(S_engineers_tesla_overchargeCost), power_cost(S_engineers_tesla_overchargeCostLevel), "TOWER_TESLA_OVERCHARGE_NAME");
    AB(1, IC_BOLT, 2, S_engineers_tesla_chargedBoltLevels, power_cost(S_engineers_tesla_chargedBoltCost), power_cost(S_engineers_tesla_chargedBoltCostLevel), "TOWER_TESLA_CHARGED_BOLT_NAME");
    return 2;
  case TW_PALADIN:
    AB(0, IC_HEALING, 10, S_soldiers_paladin_healingLevels, S_soldiers_paladin_healingCost, S_soldiers_paladin_healingCostLevel, "TOWER_PALADINS_HEALING_NAME");
    AB(1, IC_SHIELD, 7, S_soldiers_paladin_shieldLevels, S_soldiers_paladin_shieldCost, S_soldiers_paladin_shieldCostLevel, "TOWER_PALADINS_SHIELD_NAME");
    AB(2, IC_HOLY, 11, S_soldiers_paladin_holyStrikeLevels, S_soldiers_paladin_holyStrikeCost, S_soldiers_paladin_holyStrikeCostLevel, "TOWER_PALADINS_HOLY_STRIKE_NAME");
    return 3;
  case TW_BARBARIAN:
    AB(0, IC_AXE, 10, S_soldiers_barbarian_throwingLevels, S_soldiers_barbarian_throwingCost, S_soldiers_barbarian_throwingCostLevel, "TOWER_BARBARIANS_THROWING_AXES_NAME");
    AB(1, IC_NETS, 7, S_soldiers_barbarian_huntingNetsLevels, S_soldiers_barbarian_huntingNetsCost, S_soldiers_barbarian_huntingNetsCostLevel, "TOWER_BARBARIANS_HUNTER_NETS_NAME");
    AB(2, IC_DUAL, 11, S_soldiers_barbarian_dualWeaponsLevels, S_soldiers_barbarian_dualWeaponsCost, S_soldiers_barbarian_dualWeaponsCostLevel, "TOWER_BARBARIANS_DOUBLE_AXE_NAME");
    return 3;
  }
#undef AB
  return 0;
}

int spot_ability_cost(int s, int k) {
  Ability a[3];
  int n = special_abilities(s, a);
  if (k >= n || a[k].level >= a[k].max) return -1;
  return a[k].cost;
}

static Soldier *elemental(Spot *t) { return SO(t->elemental); }

void spot_buy_ability(int s, int k) {
  Spot *t = &B.spot[s];
  int c = spot_ability_cost(s, k);
  if (c < 0 || c > B.cash) return;
  battle_cash(-c);
  t->spent = (int16_t)(t->spent + c);
  t->sp_lvl[k]++;
  t->sp_t[k] = 0;
  if (t->sp_on == k + 1) t->sp_on = 0, t->sp_c = 0;
  if (t->kind == TW_SORCERER && k == 0) {   /* the elemental: summoned, or stronger */
    Soldier *el = elemental(t);
    if (!el) {
      int i = soldier_new(SK_ELEMENTAL, s, t->rally, t->rally);
      t->elemental = (int8_t)i;
    } else
      special_elemental_stats(el, s);
  }
  if (t->kind == TW_PALADIN || t->kind == TW_BARBARIAN)
    for (int j = 0; j < 3; j++) {
      Soldier *so = SO(t->soldiers[j]);
      if (so) special_new_soldier(so);
    }
}

void special_init(int s) {
  Spot *t = &B.spot[s];
  t->sp_on = 0, t->sp_c = 0;
  if (t->kind == TW_PALADIN || t->kind == TW_BARBARIAN)
    for (int j = 0; j < 3; j++) {
      Soldier *so = SO(t->soldiers[j]);
      if (so) special_new_soldier(so);
    }
}

void special_sold(int s) {
  Spot *t = &B.spot[s];
  Soldier *el = elemental(t);
  if (el) soldier_free(el);
  t->elemental = -1;
}

void special_rally(int s, Pt p) {
  Spot *t = &B.spot[s];
  t->rally = p;
  Soldier *el = elemental(t);
  if (el) soldier_set_rally(el, p);
  fx_add(AN_T_FLAG, p.x, p.y, false, false);
}

/* runSpecialTimers */
void special_timers(int s) {
  Spot *t = &B.spot[s];
  switch (t->kind) {
  case TW_RANGER:
    if (t->sp_lvl[1]) {   /* TowerArcherDruid: the thorns, on their own */
      int reload = S_archers_ranger_thornCoolDown;
      if (t->sp_t[1] < reload) {
        t->sp_t[1]++;
        break;
      }
      int dur = S_archers_ranger_thornDuration * t->sp_lvl[1], most = S_archers_ranger_thornMaxEnemies + 2 * t->sp_lvl[1];
      TowerStat st = tower_stat(t->kind, 4);
      if (t->sp_on != 2) {
        int n = 0;
        for (int i = 0; i < MAX_ENEMIES && n < S_archers_ranger_thornMinEnemies; i++) {
          Enemy *e = &B.en[i];
          if (e->alive && e->active && !e->boss && !e->no_thorn && !e->fighting && !e->flying &&
              ellipse_contains(t->x, t->y - 10, st.range, st.range * RANGE_RATIO, e->x + enemy_stats[e->type].x_adj, e->y + enemy_stats[e->type].y_adj))
            n++;
        }
        if (n >= S_archers_ranger_thornMinEnemies) t->sp_on = 2, t->sp_c = 0;
      } else if (t->sp_c < 34) {
        if (t->sp_c == 17) {
          int n = 0;
          for (int i = 0; i < MAX_ENEMIES && n < most; i++) {
            Enemy *e = &B.en[i];
            if (!e->alive || !e->active || e->boss || e->no_thorn || e->fighting || e->flying) continue;
            if (!ellipse_contains(t->x, t->y - 10, st.range, st.range * RANGE_RATIO, e->x + enemy_stats[e->type].x_adj, e->y + enemy_stats[e->type].y_adj)) continue;
            if (enemy_dodge(e)) continue;
            /* EnemyCommon.thorn: held 18 frames, then the thorns' time, 40 damage each second */
            e->thorned = 1, e->held = 1, e->thorn_t = 0, e->thorn_dmg = S_archers_ranger_thornDamage;
            e->thorn_len = (int16_t)dur, e->thorn_every = S_archers_ranger_thornDamageTime;
            if (++e->thorns >= S_archers_ranger_thornMaxTimes) e->no_thorn = 1;
            n++;
          }
          if (n) t->sp_c++;
          else t->sp_t[1] = 0, t->sp_c = 0, t->sp_on = 0;
        } else
          t->sp_c++;
      } else
        t->sp_t[1] = 0, t->sp_c = 0, t->sp_on = 0;
    }
    if (t->sp_lvl[0]) t->poison_t++;
    break;
  case TW_MUSKETEER:
    if (t->sp_lvl[0]) t->sp_t[0]++;
    if (t->sp_lvl[1]) t->sp_t[1]++;
    break;
  case TW_ARCANE:
    if (t->sp_lvl[0] && t->sp_t[0] < S_mages_arcane_deathRayCoolDown - S_mages_arcane_deathRayLessCoolDown * t->sp_lvl[0]) t->sp_t[0]++;
    if (t->sp_lvl[1]) t->sp_t[1]++;
    break;
  case TW_SORCERER:
    if (t->sp_lvl[1]) t->sp_t[1]++;
    break;
  case TW_BFG:
    if (t->sp_lvl[1]) t->sp_t[1]++;
    if (t->sp_lvl[0]) t->sp_t[0]++;
    break;
  case TW_TESLA:
    break;
  }
}

/* the most advanced enemy in an ellipse around the tower, with a test */
static int furthest(Spot *t, float w, float h, bool (*ok)(const Enemy *)) {
  int best = -1;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active || (ok && !ok(e))) continue;
    if (!ellipse_contains(t->x, t->y - 10, w, h, e->x, e->y)) continue;
    if (best < 0 || path_len(B.en[best].path, B.en[best].lane) - B.en[best].node > path_len(e->path, e->lane) - e->node) best = i;
  }
  return best;
}
static bool not_boss(const Enemy *e) { return !e->boss; }
static bool ground(const Enemy *e) { return !e->flying; }
static bool can_teleport(const Enemy *e) { return !e->boss && !e->no_teleport; }
static bool can_poly(const Enemy *e) { return !e->boss && !e->no_poly && e->type != EN_SHEEPGROUND && e->type != EN_SHEEPFLYING; }

static void face(Spot *t, const Enemy *e) {
  t->shooter_up[0] = t->shooter_up[1] = e->y < t->y;
  t->shooter_flip[0] = t->shooter_flip[1] = e->x < t->x;
  t->shooter_t[0] = t->shooter_t[1] = 0, t->anim_t = 0;
}

/* ------------------------------------------------------------------ musketeers' shots (Bullet: a homing shot) */
static void musket_shot(Spot *t, Enemy *e, int sniper) {
  Bullet *b = bullet_new(BU_SHOT);
  if (!b) return;
  t->arrows++;
  b->x = t->x + (sniper || (t->arrows & 1) == 0 ? -10 : 6), b->y = t->y - 45;
  b->target = (int8_t)(e - B.en);
  b->tx = e->x, b->ty = e->y;
  b->acc = 20, b->level = (uint8_t)sniper;
  b->min_dmg = S_archers_musketeer_minDamage, b->max_dmg = S_archers_musketeer_maxDamage;
  if (up_on(UP_ARCHERS, 5) && rnd() <= 0.1f) b->crit = 1;
  if (up_on(UP_ARCHERS, 3)) b->pierce = 10;
  t->shots++;
}

/* runSpecial: an ability instead of the usual shot (true while it runs) */
bool special_run(int s) {
  Spot *t = &B.spot[s];
  TowerStat st = tower_stat(t->kind, t->level);
  switch (t->kind) {
  case TW_MUSKETEER:
    /* Sniper Shot: 1.5 times the range, the furthest non-boss; seek 5, aim 18, shoot, 18 */
    if (t->sp_lvl[0] && t->sp_on != 2) {
      if (t->sp_on != 1) {
        if (t->sp_t[0] < S_archers_musketeer_sniperCoolDown) goto shrapnel;
        int k = furthest(t, st.range * S_archers_musketeer_sniperRange, st.range * RANGE_RATIO * S_archers_musketeer_sniperRange, not_boss);
        if (k < 0) goto shrapnel;
        t->sp_on = 1, t->sp_c = 0, t->sp_target = (int8_t)k;
        face(t, &B.en[k]);
        return true;
      }
      t->sp_c++;
      Enemy *e = EN(t->sp_target);
      if (!e || !e->active) {
        int k = furthest(t, st.range * S_archers_musketeer_sniperRange, st.range * RANGE_RATIO * S_archers_musketeer_sniperRange, not_boss);
        if (k < 0) {
          t->sp_on = 0, t->sp_c = 0, t->ready = 0;
          return true;
        }
        t->sp_target = (int8_t)k, e = &B.en[k];
      }
      if (t->sp_c == 5 + 18) musket_shot(t, e, t->sp_lvl[0]);
      if (t->sp_c >= 5 + 18 + 18) t->sp_on = 0, t->sp_c = 0, t->ready = 0, t->reload_t = 0, t->sp_t[0] = 0;
      return true;
    }
  shrapnel:
    /* Shrapnel Shot: half the range, ground enemies; 26 frames, at 16 a burst of shrapnel */
    if (t->sp_lvl[1] && t->sp_on != 1) {
      if (t->sp_on != 2) {
        if (t->sp_t[1] < S_archers_musketeer_shrapnelCoolDown) return false;
        int k = furthest(t, st.range * S_archers_musketeer_shrapnelRange, st.range * RANGE_RATIO * S_archers_musketeer_shrapnelRange, ground);
        if (k < 0) return false;
        t->sp_on = 2, t->sp_c = 0, t->ready = 1;
        t->sp_at = enemy_road(&B.en[k], 2);
        face(t, &B.en[k]);
        return true;
      }
      if (++t->sp_c == 16) {
        Pt from = {t->x - 10, t->y - 45};
        for (int a = 0; a <= 360; a += 70) {
          float w = rnd() * 40 + 25, h = (float)(int)(w * 0.7f);
          float rad = (float)(a - 90) * (float)M_PI / 180.0f;
          Bullet *b = bullet_new(BU_BOMB);
          if (!b) break;
          b->x = b->x0 = from.x, b->y = b->y0 = from.y;
          b->tx = t->sp_at.x + cosf(rad) * w / 2, b->ty = t->sp_at.y + sinf(rad) * h / 2;
          b->t1 = (int16_t)ceilf(rnd() * 4 + 4), b->g = 0;
          b->xs = (b->tx - b->x0) / b->t1, b->ys = (b->ty - b->y0) / b->t1;
          b->area = S_archers_musketeer_shrapnelArea;
          b->min_dmg = (int16_t)(S_archers_musketeer_shrapnelMinDamage * t->sp_lvl[1]);
          b->max_dmg = (int16_t)(S_archers_musketeer_shrapnelMaxDamage * t->sp_lvl[1]);
          b->bombs = 7;   /* (a shrapnel ball) */
        }
      }
      if (t->sp_c >= 26) t->sp_on = 0, t->sp_c = 0, t->ready = 0, t->reload_t = 0, t->sp_t[1] = 0;
      return true;
    }
    return false;
  case TW_ARCANE:
    /* Teleport: every 10 s, a portal under the furthest enemy */
    if (t->sp_lvl[1]) {
      if (t->sp_on == 2) {
        if (t->sp_c < 20) {
          if (t->sp_c == 4) {
            int lvl = t->sp_lvl[1];
            int most = (S_mages_arcane_teleportMaxEnemies + lvl * S_mages_arcane_teleportExtraEnemies) * lvl;
            int nodes = S_mages_arcane_teleportNodes + lvl * S_mages_arcane_teleportExtraNodes;
            portal_open(t->sp_at, most, nodes);
          }
          t->sp_c++;
          return true;
        }
        t->sp_on = 0, t->sp_c = 0, t->sp_t[1] = 0, t->reload_t = 0;
        return false;
      }
      if (t->sp_t[1] < S_mages_arcane_teleportCoolDown) return false;
      int k = furthest(t, st.range, st.range * RANGE_RATIO, can_teleport);
      if (k >= 0 && !B.en[k].flying) {
        t->sp_on = 2, t->sp_c = 0, t->sp_at = (Pt){B.en[k].x, B.en[k].y};
        face(t, &B.en[k]);
        return true;
      }
    }
    return false;
  case TW_SORCERER:
    /* Polymorph: 25 frames, at 9 the ray */
    if (t->sp_lvl[1]) {
      if (t->sp_on == 2) {
        if (t->sp_c < 25) {
          if (t->sp_c == 9) {
            Enemy *e = EN(t->sp_target);
            if (!e || !e->active) {
              int k = furthest(t, st.range, st.range * RANGE_RATIO, can_poly);
              e = EN(k);
            }
            if (!e) {
              t->sp_on = 0;
              return false;
            }
            Bullet *b = bullet_new(BU_RAY_POLY);
            if (b) b->x0 = t->x - 1.5f, b->y0 = t->y - 62.7f, b->target = (int8_t)(e - B.en), b->t1 = 30, b->level = t->sp_lvl[1];
          }
          t->sp_c++;
          return true;
        }
        t->sp_on = 0, t->sp_c = 0, t->sp_t[1] = 0, t->reload_t = 0;
        return false;
      }
      if (t->sp_t[1] < S_mages_sorcerer_polymorphCoolDown - S_mages_sorcerer_polymorphLessCoolDown * t->sp_lvl[1]) return false;
      int k = furthest(t, st.range, st.range * RANGE_RATIO, can_poly);
      if (k >= 0) {
        t->sp_on = 2, t->sp_c = 0, t->sp_target = (int8_t)k;
        face(t, &B.en[k]);
        return true;
      }
    }
    return false;
  case TW_BFG:
    /* Dragonbreath Launcher: a homing missile, further than the cannon reaches; 29 frames, at 14 */
    if (t->sp_lvl[1] && t->sp_on != 1) {
      if (t->sp_on == 2) {
        if (t->sp_c < 29) {
          if (t->sp_c == 14) {
            Enemy *e = EN(t->sp_target);
            Bullet *b = bullet_new(BU_MISSILE);
            if (b && e) {
              b->x = t->x - 20, b->y = t->y - 42, b->target = (int8_t)(e - B.en), b->level = t->sp_lvl[1];
              b->acc = 8, b->acc_max = 13, b->xs = 0, b->ys = 8, b->tx = e->x, b->ty = e->y;
            }
          }
          t->sp_c++;
          return true;
        }
        t->sp_on = 0, t->sp_c = 0, t->sp_t[1] = 0, t->ready = 0, t->reload_t = 0;
        return false;
      }
      if (t->sp_t[1] >= S_engineers_bfg_missileCoolDown) {
        float r = st.range + st.range * S_engineers_bfg_missileRangeIncrement * t->sp_lvl[1];
        int k = furthest(t, r, r * RANGE_RATIO, 0);
        if (k >= 0) {
          t->sp_on = 2, t->sp_c = 0, t->sp_target = (int8_t)k;
          return true;
        }
      }
    }
    /* Cluster Launcher: a box that bursts over the road into bomblets */
    if (t->sp_lvl[0] && t->sp_on != 2) {
      if (t->sp_on == 1) {
        if (t->sp_c < t->charge_len) {
          if (t->sp_c == t->charge_len - 26) {
            Enemy *e = EN(t->sp_target);
            Bullet *b = bullet_new(BU_BOMB);
            if (b) {
              b->x = b->x0 = t->x, b->y = b->y0 = t->y - 60;
              b->tx = t->sp_at.x, b->ty = t->sp_at.y, b->t1 = 25, b->g = 1;
              b->xs = (b->tx - b->x0) / b->t1, b->ys = (b->ty - b->y0 - b->g * b->t1 * b->t1 / 2) / b->t1;
              b->bombs = 8, b->level = t->sp_lvl[0];   /* (the box) */
              b->area = (int16_t)(e ? e->path : 0), b->pierce = (int16_t)(e ? e->node + 2 : 0);
            }
          }
          t->sp_c++, t->anim_t = (uint16_t)t->sp_c;
          return true;
        }
        t->sp_on = 0, t->sp_c = 0, t->sp_t[0] = 0, t->ready = 0, t->reload_t = 0;
        return false;
      }
      if (t->sp_t[0] >= S_engineers_bfg_clusterCoolDown) {
        int k = furthest(t, st.range, st.range * RANGE_RATIO, ground);
        if (k >= 0) {
          Enemy *e = &B.en[k];
          Pt p = enemy_road(e, enemy_nodes_speed(e));
          t->sp_on = 1, t->sp_c = 0, t->sp_target = (int8_t)k, t->sp_at = (Pt){p.x, p.y - 60};
          return true;
        }
      }
    }
    return false;
  }
  return false;
}

/* the special towers' own shots, at the shot's frame; false: the usual one */
bool special_shoot(int s) {
  Spot *t = &B.spot[s];
  Enemy *e = EN(t->target);
  switch (t->kind) {
  case TW_MUSKETEER:
    if (!e || !e->active) e = EN(tower_find_target(t, true, false));
    if (e) musket_shot(t, e, 0);
    return true;
  case TW_ARCANE: {
    if (!e || !e->active) e = EN(tower_find_target(t, true, false));
    if (!e) return true;
    Bullet *b = bullet_new(BU_RAY_ARCANE);
    if (!b) return true;
    b->x0 = t->x + 1, b->y0 = t->y - 59, b->target = (int8_t)(e - B.en), b->t1 = 10;
    TowerStat st = tower_stat(t->kind, 4);
    b->dmg = (int16_t)rnd_dmg(st.min_dmg, st.max_dmg);
    int cd = S_mages_arcane_deathRayCoolDown - S_mages_arcane_deathRayLessCoolDown * t->sp_lvl[0];
    if (t->sp_lvl[0] && t->sp_t[0] >= cd && !e->boss) {   /* the death ray */
      b->kind = BU_RAY_DEATH;
      t->sp_t[0] = 0;
    }
    t->shots++;
    return true;
  }
  case TW_SORCERER: {
    if (!e || !e->active || e->curse_t) {
      /* (an enemy not yet cursed first) */
      int best = -1, cursed = -1;
      TowerStat st = tower_stat(t->kind, 4);
      for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *o = &B.en[i];
        if (!o->alive || !o->active || !ellipse_contains(t->x, t->y - 10, st.range, st.range * RANGE_RATIO, o->x, o->y)) continue;
        int *slot = o->curse_t ? &cursed : &best;
        if (*slot < 0 || path_len(B.en[*slot].path, B.en[*slot].lane) - B.en[*slot].node > path_len(o->path, o->lane) - o->node) *slot = i;
      }
      e = EN(best >= 0 ? best : cursed);
    }
    if (!e) return true;
    Bullet *b = bullet_new(BU_BOLT_SORCERER);
    if (!b) return true;
    bool down = e->y >= t->y;
    b->x = t->x + (down ? -8 : 8), b->y = t->y - 44, b->target = (int8_t)(e - B.en), b->acc = 1, b->acc_max = 20, b->first = 1;
    b->tx = e->x, b->ty = e->y;
    TowerStat st = tower_stat(t->kind, 4);
    b->min_dmg = st.min_dmg, b->max_dmg = st.max_dmg;
    t->shots++;
    return true;
  }
  case TW_BFG: {
    Bullet *b = bullet_new(BU_BOMB);
    if (!e || !e->active) {
      e = EN(tower_find_target(t, false, true));
      if (e) t->dest = enemy_road(e, enemy_nodes_speed(e));
    } else if (e->blocked)
      t->dest = (Pt){e->x, e->y};
    if (!b) return true;
    TowerStat st = tower_stat(t->kind, 4);
    b->x = b->x0 = t->x, b->y = b->y0 = t->y - 60, b->tx = t->dest.x, b->ty = t->dest.y, b->t1 = 27, b->g = 1;
    b->xs = (b->tx - b->x0) / b->t1, b->ys = (b->ty - b->y0 - b->g * b->t1 * b->t1 / 2) / b->t1;
    b->min_dmg = st.min_dmg, b->max_dmg = st.max_dmg, b->area = st.area, b->level = 4, b->bombs = 9;
    t->shots++;
    return true;
  }
  case TW_TESLA: {
    e = EN(tower_find_target(t, true, false));
    if (!e) return true;
    if (t->sp_lvl[0]) {   /* Overcharge: a static field around the tower */
      Bullet *f = bullet_new(BU_FIELD);
      if (f) f->x = t->x, f->y = t->y, f->level = t->sp_lvl[0];
    }
    Bullet *b = bullet_new(BU_RAY_TESLA);
    if (b) {
      b->x0 = t->x + 6, b->y0 = t->y - 62, b->target = (int8_t)(e - B.en), b->t1 = 14;
      b->jump = 1, b->jump_max = (uint8_t)(3 + t->sp_lvl[1]);
      memset(b->hitlist, -1, sizeof b->hitlist);
      TowerStat st = tower_stat(t->kind, 4);
      b->dmg = (int16_t)rnd_dmg(st.min_dmg, st.max_dmg);
    }
    t->shots++;
    return true;
  }
  }
  return false;
}

/* the rangers' poison: every 12 frames an arrow carries it, to an enemy not yet poisoned */
void special_arrow(int s, Bullet *b) {
  Spot *t = &B.spot[s];
  if (t->kind != TW_RANGER || !t->sp_lvl[0]) return;
  if (t->poison_t < S_archers_ranger_poisonCoolDown) return;
  t->poison_t = 0;
  Enemy *e = EN(b->target);
  if (e && e->poison_t) {   /* another, not poisoned */
    TowerStat st = tower_stat(t->kind, 4);
    for (int i = 0; i < MAX_ENEMIES; i++) {
      Enemy *o = &B.en[i];
      if (o->alive && o->active && !o->poison_t && ellipse_contains(t->x, t->y - 10, st.range, st.range * RANGE_RATIO, o->x, o->y)) {
        b->target = (int8_t)i;
        b->tx = o->x + o->xs * b->t1, b->ty = o->y + o->ys * b->t1 - 5;
        b->xs = (b->tx - b->x0) / b->t1, b->ys = (b->ty - b->y0 - b->g * b->t1 * (b->t1 / 2.0f)) / b->t1;
        break;
      }
    }
  }
  b->bombs = t->sp_lvl[0];   /* (the poison's level) */
}

void special_bolt(int s, Bullet *b) { (void)s, (void)b; }
void special_bomb(int s, Bullet *b) { (void)s, (void)b; }

void special_poison(Enemy *e, int level) {
  if (e->type == EN_SPIDER || e->type == EN_SPIDERSMALL || e->type == EN_SPIDERTINY) return;   /* (canPoison false) */
  if (e->poison_t && e->poison_lvl > level) return;
  e->poison_lvl = (int16_t)level, e->poison_t = S_archers_ranger_poisonDuration, e->poison_tick = 0;
}

static void slow(Enemy *e) {
  if (e->slow_t || e->net_t || e->rage_t) return;
  e->slow_speed = e->speed, e->speed /= 2, e->margin = e->speed + 0.1f;
  e->slow_t = 15;
}

/* Bolt.hitOnTarget: magic damage; Slow Curse halves its speed half a second, Arcane Shatter takes 3 armour */
void special_bolt_hit(Bullet *b, Enemy *e) {
  bool slowed = up_on(UP_MAGES, 5) && !e->rage_t && !e->slow_t && !e->net_t;
  enemy_damage(e, rnd_dmg(b->min_dmg, b->max_dmg), ARM_M, 0);
  if (b->kind == BU_BOLT_SORCERER && !e->dead && !e->curse_t) {   /* DebuffCurse */
    e->curse_amt = (int16_t)(e->armor * S_mages_sorcerer_curseArmorReduce / 100);
    e->armor = (int16_t)(e->armor - e->curse_amt);
    e->curse_t = S_mages_sorcerer_curseDuration, e->curse_tick = 0;
  }
  if (slowed && !e->dead) slow(e);
  if (up_on(UP_MAGES, 2) && e->active) enemy_armor_down(e, 3);
}

void special_bomb_hit(Bullet *b) {
  if (b->bombs == 8) {   /* the cluster box: bomblets along the road, 7 nodes apart */
    int n = S_engineers_bfg_clusterMinBombs + S_engineers_bfg_clusterIncrementBombs * b->level;
    int span = (n / 2) * 7, t1 = 10, sign = 1, path = b->area, node = b->pierce;
    if (path >= B.npaths) path = 0;
    for (int k = span; k >= -span; k -= 7) {
      if (node + k < path_len(path, 0)) {
        if (node + k < 0) break;
        if (rnd() > 0.5f) sign = -sign;
        Pt p = path_node(path, 0, node + k);
        Bullet *f = bullet_new(BU_BOMB);
        if (f) {
          f->x = f->x0 = b->x, f->y = f->y0 = b->y;
          f->tx = p.x + rnd() * 6 * sign, f->ty = p.y + rnd() * 6 * sign, f->t1 = (int16_t)t1, f->g = 1;
          f->xs = (f->tx - f->x0) / f->t1, f->ys = (f->ty - f->y0 - f->g * f->t1 * f->t1 / 2) / f->t1;
          f->area = S_engineers_bfg_clusterArea, f->min_dmg = S_engineers_bfg_clusterMinDamage, f->max_dmg = S_engineers_bfg_clusterMaxDamage;
          f->bombs = 10, f->level = 4;
        }
      }
      t1++;
    }
    return;
  }
  bomb_blast(b);
}

/* ------------------------------------------------------------------ rays, missiles, fields, the musketeers' shots */
static void enemy_hit_m(Enemy *e, int dmg, bool shielded) {
  if (shielded && e->sp_state == 9) {   /* (a shield takes only a ray's first hit) */
    int keep = e->sp_state;
    e->sp_state = 0;
    enemy_damage(e, dmg, ARM_M, 0);
    if (!e->dead) e->sp_state = (int16_t)keep;
    return;
  }
  enemy_damage(e, dmg, ARM_M, 0);
}

void special_bullet(Bullet *b) {
  switch (b->kind) {
  case BU_RAY_ARCANE: case BU_RAY_DEATH: {
    /* RayArcane: its damage a tenth a frame, the rest at the end; the death ray desintegrates at the end */
    Enemy *e = EN(b->target);
    if (b->t0 >= b->t1) {
      b->alive = 0;
      return;
    }
    if (e && e->active) {
      if (b->kind == BU_RAY_DEATH) {
        if (b->t0 + 1 == b->t1) enemy_kill(e, 2);
      } else {
        int d = b->dmg / b->t1;
        if (b->t0 + 1 == b->t1) d += b->dmg - d * b->t1;
        bool slowed = !b->shielded && up_on(UP_MAGES, 5) && !e->rage_t && !e->slow_t && !e->net_t;
        enemy_hit_m(e, d, b->shielded);
        if (slowed && !e->dead) slow(e);
        if (!b->shielded && up_on(UP_MAGES, 2) && e->active) enemy_armor_down(e, 3);
        b->shielded = 1;
      }
      b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
    }
    b->t0++;
    return;
  }
  case BU_RAY_POLY: {
    Enemy *e = EN(b->target);
    if (b->t0 >= b->t1) {
      b->alive = 0;
      return;
    }
    if (e && e->active) {
      b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
      if (b->t0 == 2) polymorph(e);
    }
    b->t0++;
    return;
  }
  case BU_RAY_TESLA: {
    /* RayTesla: 14 frames, at frame 2 an arc to the next enemy (by road) within 80 */
    Enemy *e = EN(b->target);
    if (++b->t0 >= b->t1) {
      b->alive = 0;
      return;
    }
    if (!e || !e->active) return;
    float f = b->jump == 1 || up_on(UP_ENGINEERS, 5) ? 1.0f : b->jump == 2 ? 0.75f : 0.5f;
    int total = (int)(b->dmg * f), d = total / b->t1;
    if (b->t0 + 1 == b->t1) d += total - d * b->t1;
    enemy_damage(e, d, ARM_E, 0);
    if (!e->active) return;
    b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
    if (b->t0 == 2 && b->jump < b->jump_max) {
      for (int k = 0; k < 6; k++)
        if (b->hitlist[k] < 0) {
          b->hitlist[k] = b->target;
          break;
        }
      int best = -1;
      for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *o = &B.en[i];
        if (!o->alive || !o->active) continue;
        bool seen = false;
        for (int k = 0; k < 6; k++) seen |= b->hitlist[k] == i;
        if (seen) continue;
        float dx = o->x - e->x + enemy_stats[e->type].x_adj, dy = o->y - e->y + enemy_stats[e->type].y_adj;
        if (sqrtf(dx * dx + dy * dy) > S_engineers_tesla_chargedBoltRange) continue;
        if (best < 0 || abs(e->node - o->node) < abs(e->node - B.en[best].node)) best = i;
      }
      if (best >= 0) {
        Bullet *n = bullet_new(BU_RAY_TESLA);
        if (n) {
          *n = *b;
          n->t0 = 0, n->x0 = b->tx, n->y0 = b->ty, n->target = (int8_t)best, n->jump = (uint8_t)(b->jump + 1);
          n->dmg = (int16_t)rnd_dmg(S_engineers_tesla_minDamage, S_engineers_tesla_maxDamage);
          if (up_on(UP_ENGINEERS, 1))
            n->dmg = (int16_t)rnd_dmg(tower_stat(TW_TESLA, 4).min_dmg, tower_stat(TW_TESLA, 4).max_dmg);
        }
      }
    }
    return;
  }
  case BU_FIELD: {
    /* StaticField: grows 15 a frame to the tower's 230, then strikes all inside */
    b->t0 = (int16_t)(b->t0 + 15);
    if (b->t0 >= S_engineers_tesla_overchargeArea) {
      int lo = S_engineers_tesla_overchargeMinDamage + S_engineers_tesla_overchargeDamageIncrement * b->level;
      int hi = S_engineers_tesla_overchargeMaxDamage + S_engineers_tesla_overchargeDamageIncrement * b->level;
      int a = S_engineers_tesla_overchargeArea;
      for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &B.en[i];
        if (e->alive && e->active && ellipse_contains(b->x, b->y - 10, (float)a, a * RANGE_RATIO, e->x, e->y))
          enemy_damage(e, rnd_dmg(lo, hi), ARM_E, 0);
      }
      b->alive = 0;
    }
    return;
  }
  case BU_MISSILE: {
    /* Missile: homing, speeding up a tenth a frame to 13; finds another target if it loses its own */
    Enemy *e = EN(b->target);
    if (!e || !e->active) {
      int best = -1;
      for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *o = &B.en[i];
        if (!o->alive || !o->active) continue;
        float dx = o->x - b->x, dy = o->y - b->y, d = dx * dx + dy * dy;
        if (best < 0 || d < (B.en[best].x - b->x) * (B.en[best].x - b->x) + (B.en[best].y - b->y) * (B.en[best].y - b->y)) best = i;
      }
      b->target = (int8_t)best;
      e = EN(best);
    }
    if (e) b->tx = e->x, b->ty = e->y;
    if (b->acc < b->acc_max) {
      b->acc += b->acc * 0.1f;
      if (b->acc > b->acc_max) b->acc = b->acc_max;
    }
    float a = atan2f(b->ty - b->y, b->tx - b->x);
    /* (turning, not in a straight line) */
    float cur = atan2f(b->ys, b->xs), da = a - cur;
    while (da > (float)M_PI) da -= 2 * (float)M_PI;
    while (da < -(float)M_PI) da += 2 * (float)M_PI;
    if (da > 0.35f) da = 0.35f;
    if (da < -0.35f) da = -0.35f;
    cur += da;
    b->xs = cosf(cur) * b->acc, b->ys = sinf(cur) * b->acc, b->rot = cur;
    b->x += b->xs, b->y += b->ys;
    float dx = b->tx - b->x, dy = b->ty - b->y;
    if (sqrtf(dx * dx + dy * dy) < b->acc || b->anim_t > 300) {
      b->area = S_engineers_bfg_missileArea;
      b->min_dmg = (int16_t)(S_engineers_bfg_missileMinDamage + S_engineers_bfg_missileIncrementDamage * b->level);
      b->max_dmg = (int16_t)(S_engineers_bfg_missileMaxDamage + S_engineers_bfg_missileIncrementDamage * b->level);
      b->tx = b->x, b->ty = b->y;
      bomb_blast(b);
      b->active = 0, b->fade = 20, b->anim_t = 0;
    }
    return;
  }
  case BU_SHOT: {
    /* Bullet: straight at the target, 20 a frame; a sniper's shot kills outright, or hurts by its life */
    Enemy *e = EN(b->target);
    if (e && e->active) b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
    float a = atan2f(b->ty - b->y, b->tx - b->x);
    b->rot = a;
    b->x += cosf(a) * b->acc, b->y += sinf(a) * b->acc;
    float dx = b->tx - b->x, dy = b->ty - b->y;
    if (sqrtf(dx * dx + dy * dy) < b->acc) {
      if (e && e->active) {
        int dmg = rnd_dmg(b->min_dmg, b->max_dmg);
        if (b->crit) dmg *= 2;
        if (b->level) {
          float pct = (float)(S_archers_musketeer_sniperDamagePercent * b->level);
          if (rnd() < pct / 100.0f) enemy_kill(e, 1);
          else {
            int d = dmg + (int)(pct * e->init_health / 100.0f);
            if (e->health < d) enemy_kill(e, 1);
            else enemy_damage(e, d, ARM_P, 0);
          }
        } else
          enemy_damage(e, dmg, ARM_P, b->pierce);
      }
      b->alive = 0;
    }
    return;
  }
  case BU_AXE: case BU_NET: {
    /* the barbarians' axes (an arc of 20 frames) and nets (homing) */
    Enemy *e = EN(b->target);
    if (b->kind == BU_NET) {
      if (e && e->active) b->tx = e->x, b->ty = e->y - 20;
      float a = atan2f(b->ty - b->y, b->tx - b->x);
      b->x += cosf(a) * 12, b->y += sinf(a) * 12;
      float dx = b->tx - b->x, dy = b->ty - b->y;
      if (sqrtf(dx * dx + dy * dy) < 12) {
        if (e && e->active && !e->net_t && !e->slow_t && !(e->type == EN_ROCKETEER && e->sp_t)) {
          e->slow_speed = e->speed;
          e->speed -= e->speed * (S_soldiers_barbarian_huntingNetsNewSpeed + S_soldiers_barbarian_huntingNetsNewSpeedIncrement * b->level) / 100.0f;
          e->margin = e->speed + 0.1f;
          e->net_t = S_soldiers_barbarian_huntingNetsDuration;
        }
        b->alive = 0;
      }
      return;
    }
    float t = b->t0;
    b->x = b->x0 + t * b->xs, b->y = b->y0 + t * b->ys + t * t / 2;
    if (e && e->active && b->t0 + 2 >= b->t1) {
      const EnemyStat *st = &enemy_stats[e->type];
      if (b->x + 6 > e->x - st->hit_w && b->x - 6 < e->x + st->hit_w && b->y + 4 > e->y - st->hit_h && b->y - 4 < e->y + 4) {
        enemy_damage(e, rnd_dmg(b->min_dmg, b->max_dmg), ARM_P, 0);
        b->alive = 0;
        return;
      }
    }
    if (b->t0 >= b->t1) {
      b->alive = 0;
      return;
    }
    b->t0++;
    return;
  }
  default:
    b->alive = 0;
  }
}

void special_draw_bullet(const Bullet *b, int x, int y) {
  int x0 = 40 + (int)lroundf(b->x0 * 0.4f), y0 = (int)lroundf(b->y0 * 0.4f);
  int tx = 40 + (int)lroundf(b->tx * 0.4f), ty = (int)lroundf(b->ty * 0.4f);
  switch (b->kind) {
  case BU_RAY_ARCANE: case BU_RAY_DEATH:
    gfx_line(x0, y0, tx, ty, b->kind == BU_RAY_DEATH ? RGB(0x9CFF6A) : RGB(0xC27CFF));
    gfx_line(x0 + 1, y0, tx + 1, ty, RGB(0xFFFFFF));
    break;
  case BU_RAY_POLY:
    if (b->t0 < 4) gfx_line(x0, y0, tx, ty, RGB(0x7CFF9A));
    break;
  case BU_RAY_TESLA: {
    /* a jagged bolt */
    int px = x0, py = y0;
    for (int k = 1; k <= 4; k++) {
      int nx = x0 + (tx - x0) * k / 4, ny = y0 + (ty - y0) * k / 4;
      if (k < 4) nx += ((b->t0 + k) & 1) ? 2 : -2, ny += ((b->t0 + k) & 2) ? 1 : -1;
      gfx_line(px, py, nx, ny, (b->t0 & 1) ? RGB(0xBFF4FF) : RGB(0x5AC8FF));
      px = nx, py = ny;
    }
    break;
  }
  case BU_FIELD: {
    int r = (int)lroundf(b->t0 * 0.2f * S_engineers_tesla_overchargeDisplayRatio);
    gfx_ellipse(x, y - 4, r, (int)(r * RANGE_RATIO), RGB(0x8ADCFF), 20);
    break;
  }
  case BU_MISSILE:
    if (b->active) spr_anim(AN_B_MISSILE_START, b->anim_t, true, x, y, b->xs < 0);
    else spr_anim(AN_FX_EXPLODEBIG, b->anim_t, false, x, y, false);
    break;
  case BU_SHOT:
    gfx_line(x, y, x - (int)lroundf(cosf(b->rot) * 3), y - (int)lroundf(sinf(b->rot) * 3), RGB(0xFFF2A0));
    break;
  case BU_AXE:
    spr_anim(AN_B_AXE_IDLE, 0, false, x, y, b->xs < 0);
    break;
  case BU_NET:
    gfx_ellipse(x, y, 3, 2, RGB(0xD8C8A0), 32);
    break;
  }
}

/* ------------------------------------------------------------------ the arcane wizard's portal (TeleportPortal) */
void portal_open(Pt at, int most, int nodes) {
  for (int k = 0; k < (int)(sizeof B.portal / sizeof B.portal[0]); k++)
    if (!B.portal[k].on) {
      B.portal[k].on = 1, B.portal[k].t = 0, B.portal[k].at = at, B.portal[k].left = (int16_t)most, B.portal[k].nodes = (int16_t)nodes;
      return;
    }
}

void portals_step(void) {
  for (int k = 0; k < (int)(sizeof B.portal / sizeof B.portal[0]); k++) {
    if (!B.portal[k].on) continue;
    int t = ++B.portal[k].t;
    /* 5 frames opening, 10 open, 25 closing */
    if (t > 5 && t <= 15 && B.portal[k].left > 0)
      for (int i = 0; i < MAX_ENEMIES && B.portal[k].left > 0; i++) {
        Enemy *e = &B.en[i];
        if (!e->alive || !e->active || e->boss || e->no_teleport || e->teleporting) continue;
        if (!ellipse_contains(B.portal[k].at.x, B.portal[k].at.y, 50, 35, e->x, e->y)) continue;
        int n = B.portal[k].nodes + rnd_round(-3, 6);
        if (e->node - n < 0) n = e->node;
        e->active = 0, e->teleporting = 1, e->teleport_t = 0;
        e->node = (int16_t)(e->node - n);
        if (e->node <= 10) e->node = 10;
        if (e->soldier >= 0) enemy_unblock(e);
        e->held = 0, e->thorned = 0, e->sp_state = e->sp_state == 9 ? 9 : 0;
        if (++e->teleports >= S_mages_arcane_teleportMaxTimes) e->no_teleport = 1;
        B.portal[k].left--;
      }
    if (t > 40) B.portal[k].on = 0;
  }
}

/* EnemyCommon.polymorph: a sheep with half its life, its gold and lives, half again as fast */
void polymorph(Enemy *e) {
  int gold = e->gold, cost = e->cost, h = e->health * S_mages_sorcerer_polymorphDamage / 100;
  int ih = e->init_health * S_mages_sorcerer_polymorphDamage / 100;
  float speed = e->speed * S_mages_sorcerer_polymorphSpeedMultiplier;
  e->gold = 0;
  bool fly = e->flying;
  int path = e->path, lane = e->lane, node = e->node;
  float x = e->x, y = e->y;
  enemy_kill(e, 3);
  e->alive = 0;
  fx_add(fly ? AN_FX_POLYMORPHAIR : AN_FX_POLYMORPHNORMAL, x, y, false, false);
  int i = enemy_spawn(fly ? EN_SHEEPFLYING : EN_SHEEPGROUND, path, lane, node);
  if (i < 0) return;
  Enemy *s = &B.en[i];
  s->x = x, s->y = y, s->health = h < 1 ? 1 : h, s->init_health = ih < 1 ? 1 : ih;
  s->gold = (int16_t)gold, s->cost = (int16_t)cost, s->speed = s->base_speed = speed, s->margin = speed + 0.1f;
  s->active = node >= 10, s->no_poly = 1;
}

/* ------------------------------------------------------------------ soldiers */
void special_elemental_stats(Soldier *so, int spot) {
  Spot *t = &B.spot[spot];
  int l = t->sp_lvl[0] ? t->sp_lvl[0] : 1;
  so->health = so->init_health = (int16_t)(S_mages_sorcerer_elementalHealth + S_mages_sorcerer_elementalExtraHealth * l);
  so->armor = (int16_t)(S_mages_sorcerer_elementalArmor + S_mages_sorcerer_elementalArmorExtra * l);
  so->min_dmg = (int16_t)(S_mages_sorcerer_elementalMinDamage + S_mages_sorcerer_elementalDamageExtra * l);
  so->max_dmg = (int16_t)(S_mages_sorcerer_elementalMaxDamage + S_mages_sorcerer_elementalDamageExtra * l);
  so->regen = S_mages_sorcerer_elementalRegen, so->regen_reload = S_mages_sorcerer_elementalRegenReload;
  so->dead_time = S_mages_sorcerer_elementalRespawnTime, so->reload = S_mages_sorcerer_elementalReload;
  so->range_w = S_mages_sorcerer_elementalRange, so->range_h = (int16_t)(S_mages_sorcerer_elementalRange * RANGE_RATIO);
  so->level = (uint8_t)l;
  if (so->respawning && !so->dead) return;
  /* (it appears as it respawns: dead now, back next frame) */
  if (!so->active && !so->walking) so->dead = 1, so->respawning = 0, so->dead_t = (int16_t)(so->dead_time - 1), so->hidden = 1;
}

void special_elemental_hit(Soldier *so) {
  /* SoldierElemental.areaAttack: 30 ahead of it, up to 4 ground enemies */
  float cx = so->x + (so->left ? -30 : 30), cy = so->y;
  int n = 0, w = S_mages_sorcerer_elementalAreaAttackRangeWidth;
  for (int i = 0; i < MAX_ENEMIES && n < S_mages_sorcerer_elementalAreaAttackMaxEnemies; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active || e->flying || !ellipse_contains(cx, cy, (float)w, w * RANGE_RATIO, e->x, e->y)) continue;
    if (!enemy_dodge(e)) enemy_damage(e, rnd_dmg(so->min_dmg, so->max_dmg), ARM_P, 0);
    n++;
  }
}

void special_new_soldier(Soldier *so) {
  if (so->spot < 0) return;
  Spot *t = &B.spot[so->spot];
  BarrackStat b = barrack_stat(t->kind, 4);
  if (so->kind == SK_PALADIN) {
    so->armor = (int16_t)(b.armor + (t->sp_lvl[1] ? S_soldiers_paladin_shieldArmor : 0));
  } else if (so->kind == SK_BARBARIAN) {
    int d = t->sp_lvl[2] * S_soldiers_barbarian_dualWeaponsIncrementDamage;
    so->min_dmg = (int16_t)(b.min_dmg + d), so->max_dmg = (int16_t)(b.max_dmg + d);
    if (t->sp_lvl[2]) so->charge_dmg = 14;
  }
}

static int soldier_range_target(Soldier *so, float w, float h, int min_range, bool fliers_only, bool netless) {
  int best = -1;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active || (fliers_only && !e->flying)) continue;
    if (netless && (e->net_t || e->slow_t || (e->type == EN_ROCKETEER && e->sp_t))) continue;
    if (!ellipse_contains(so->x, so->y, w, h, e->x, e->y)) continue;
    float dx = e->x - so->x, dy = e->y - so->y;
    if (min_range && sqrtf(dx * dx + dy * dy) <= (float)min_range) continue;
    if (fliers_only) return i;
    if (best < 0 || path_len(B.en[best].path, B.en[best].lane) - B.en[best].node > path_len(e->path, e->lane) - e->node) best = i;
  }
  return best;
}

static void enemy_in_range_holy(Soldier *so, int *count, bool strike, int dmg) {
  Spot *t = &B.spot[so->spot];
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active || e->flying) continue;
    if (!ellipse_contains(t->rally.x, t->rally.y + 10, so->range_w, so->range_h, e->x, e->y)) continue;
    (*count)++;
    if (strike) enemy_damage(e, dmg, ARM_I, 0);
  }
}

/* SoldierPaladin.runSpecial (healing, holy strike), SoldierBarbarian.runSpecial (nets, axes) */
bool special_soldier(Soldier *so) {
  if (so->spot < 0 || (so->kind != SK_PALADIN && so->kind != SK_BARBARIAN) || !so->active) return false;
  Spot *t = &B.spot[so->spot];
  if (so->kind == SK_PALADIN) {
    int heal_l = t->sp_lvl[0], holy_l = t->sp_lvl[2];
    if (heal_l && so->sp_state != 2) {
      if (so->sp_state != 1) {
        if (so->sp_t < S_soldiers_paladin_healingCoolDown) so->sp_t++;
        else if (so->health * 100 / so->init_health <= 70) {
          so->sp_state = 1, so->sp_c = 0;
          so->anim = AN_SO_PALADIN_HEAL, so->anim_t = 0;
          return true;
        }
      } else {
        if (so->sp_c < 20) {
          so->sp_c++;
          return true;
        }
        int v = rnd_dmg(S_soldiers_paladin_healingMin * heal_l, S_soldiers_paladin_healingMax * heal_l);
        so->health = (int16_t)(so->health + v);
        if (so->health > so->init_health) so->health = so->init_health;
        so->sp_state = 0, so->sp_t = 0;
        so->anim = so->fighting ? AN_SO_PALADIN_IDLE : AN_SO_PALADIN_IDLE, so->anim_t = 0;
        return false;
      }
    }
    if (holy_l && so->sp_state == 2) {
      if (so->sp_c < 31) {
        if (++so->sp_c == 15) {
          int n = 0;
          enemy_in_range_holy(so, &n, true, rnd_dmg(S_soldiers_paladin_holyStrikeMinDamage * holy_l, S_soldiers_paladin_holyStrikeMaxDamage * holy_l));
        }
        return true;
      }
      so->sp_state = 0;
      return false;
    }
    return false;
  }
  /* the barbarians */
  int axe_l = t->sp_lvl[0], net_l = t->sp_lvl[1];
  if (net_l && so->sp_state != 4) {
    if (so->sp_state != 3) {
      if (so->sp_t2 < S_soldiers_barbarian_huntingNetsCoolDown) so->sp_t2++;
      else {
        int r = S_soldiers_barbarian_huntingNetsRange;
        int k = soldier_range_target(so, (float)r, r * RANGE_RATIO, 0, true, true);
        if (k >= 0) {
          so->left = B.en[k].x < so->x;
          so->sp_state = 3, so->sp_c = 0, so->sp_target = (int8_t)k;
          so->anim = AN_SO_BARBARIAN_HUNTING_NETS, so->anim_t = 0;
          return true;
        }
      }
    } else {
      if (so->sp_c < 14) {
        if (++so->sp_c == 7) {
          Enemy *e = EN(so->sp_target);
          if (!e || !e->active) {
            int r = S_soldiers_barbarian_huntingNetsRange;
            e = EN(soldier_range_target(so, (float)r, r * RANGE_RATIO, 0, true, true));
          }
          Bullet *b = e ? bullet_new(BU_NET) : 0;
          if (b) b->x = so->x, b->y = so->y - 12, b->target = (int8_t)(e - B.en), b->level = (uint8_t)net_l, b->tx = e->x, b->ty = e->y;
        }
        return true;
      }
      so->sp_state = 0, so->sp_t2 = 0;
      return false;
    }
  }
  if (axe_l && so->sp_state != 3) {
    int w = S_soldiers_barbarian_throwingRange + axe_l * S_soldiers_barbarian_throwingIncrementRange;
    if (so->sp_state != 4) {
      if (so->sp_t < S_soldiers_barbarian_throwingCoolDown) {
        so->sp_t++;
        return false;
      }
      int k = soldier_range_target(so, (float)w, w * RANGE_RATIO, S_soldiers_barbarian_throwingMinRange, false, false);
      if (k < 0) return false;
      so->left = B.en[k].x < so->x;
      so->sp_at = (Pt){B.en[k].x + enemy_stats[B.en[k].type].x_adj, B.en[k].y + enemy_stats[B.en[k].type].y_adj};
      so->sp_state = 4, so->sp_c = 0, so->sp_target = (int8_t)k;
      so->anim = t->sp_lvl[2] ? AN_SO_BARBARIAN_THROWING_AXE2 : AN_SO_BARBARIAN_THROWING_AXE, so->anim_t = 0;
      return true;
    }
    if (so->sp_c < 14) {
      if (++so->sp_c == 7) {
        Enemy *e = EN(so->sp_target);
        if (!e || !e->active) e = EN(soldier_range_target(so, (float)w, w * RANGE_RATIO, S_soldiers_barbarian_throwingMinRange, false, false));
        Bullet *b = bullet_new(BU_AXE);
        if (b) {
          b->x = b->x0 = so->x, b->y = b->y0 = so->y - 12, b->t1 = 20;
          if (e) {
            b->target = (int8_t)(e - B.en);
            b->tx = e->x + enemy_stats[e->type].x_adj + e->xs * 20 + rnd() * 10;
            b->ty = e->y + enemy_stats[e->type].y_adj + e->ys * 20 + rnd() * 10 - 10;
          } else
            b->tx = so->sp_at.x, b->ty = so->sp_at.y;
          b->xs = (b->tx - b->x0) / b->t1, b->ys = (b->ty - b->y0 - b->t1 * (b->t1 / 2.0f)) / b->t1;
          b->min_dmg = (int16_t)(S_soldiers_barbarian_throwingMinDamage + S_soldiers_barbarian_throwingIncrementDamage * axe_l);
          b->max_dmg = (int16_t)(S_soldiers_barbarian_throwingMaxDamage + S_soldiers_barbarian_throwingIncrementDamage * axe_l);
        }
      }
      return true;
    }
    so->sp_state = 0, so->sp_t = 0;
    return false;
  }
  return false;
}

void special_soldier_stop(Soldier *so) {
  if (so->sp_state) so->sp_state = 0;
}

int special_soldier_damage(Soldier *so, int dmg) {
  (void)so;
  return dmg;
}

bool special_soldier_attack_anim(Soldier *so) {
  if (so->kind == SK_PALADIN) {
    Spot *t = so->spot >= 0 ? &B.spot[so->spot] : 0;
    /* readyToAttack: now and then a holy strike instead (a chance in 10, an enemy in reach) */
    if (t && t->sp_lvl[2] && so->sp_state == 0 && rnd() * 100 <= S_soldiers_paladin_holyStrikeChance) {
      int n = 0;
      enemy_in_range_holy(so, &n, false, 0);
      if (n >= S_soldiers_paladin_holyStrikeMinEnemies) {
        so->sp_state = 2, so->sp_c = 0;
        so->anim = AN_SO_PALADIN_HOLYSTRIKE, so->anim_t = 0;
        return true;
      }
    }
    so->anim = rnd() > 0.5f ? AN_SO_PALADIN_ATTACK2 : AN_SO_PALADIN_ATTACK1, so->anim_t = 0;
  } else if (so->kind == SK_BARBARIAN && so->spot >= 0 && B.spot[so->spot].sp_lvl[2])
    so->anim = AN_SO_BARBARIAN_ATTACK2, so->anim_t = 0;
  return false;
}
