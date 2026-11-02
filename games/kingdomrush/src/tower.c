/* The towers (Tower.as, TowerHolder.as, TowerArcher.as, TowerMage.as, TowerEngineer.as, TowerSoldier.as; the
 * specials in special.c). A spot holds the build spot or its tower. */
#include <math.h>
#include <string.h>
#include "kr.h"
#include "settings.h"
#include "special.h"

#define BUILD_TIME 30

TowerStat tower_stat(int kind, int level) {
  TowerStat t = {0};
#define T(c, r, lo, hi, rl, ar) t = (TowerStat){c, r, lo, hi, rl, ar}
  switch (kind) {
  case TW_ARCHER:
    if (level == 1) T(S_archers_level1_cost, S_archers_level1_range, S_archers_level1_minDamage, S_archers_level1_maxDamage, S_archers_level1_reload, 0);
    if (level == 2) T(S_archers_level2_cost, S_archers_level2_range, S_archers_level2_minDamage, S_archers_level2_maxDamage, S_archers_level2_reload, 0);
    if (level == 3) T(S_archers_level3_cost, S_archers_level3_range, S_archers_level3_minDamage, S_archers_level3_maxDamage, S_archers_level3_reload, 0);
    break;
  case TW_RANGER: T(S_archers_ranger_cost, S_archers_ranger_range, S_archers_ranger_minDamage, S_archers_ranger_maxDamage, S_archers_ranger_reload, 0); break;
  case TW_MUSKETEER: T(S_archers_musketeer_cost, S_archers_musketeer_range, S_archers_musketeer_minDamage, S_archers_musketeer_maxDamage, S_archers_musketeer_reload, 0); break;
  case TW_MAGE:
    if (level == 1) T(S_mages_level1_cost, S_mages_level1_range, S_mages_level1_minDamage, S_mages_level1_maxDamage, S_mages_level1_reload, 0);
    if (level == 2) T(S_mages_level2_cost, S_mages_level2_range, S_mages_level2_minDamage, S_mages_level2_maxDamage, S_mages_level2_reload, 0);
    if (level == 3) T(S_mages_level3_cost, S_mages_level3_range, S_mages_level3_minDamage, S_mages_level3_maxDamage, S_mages_level3_reload, 0);
    break;
  case TW_ARCANE: T(S_mages_arcane_cost, S_mages_arcane_range, S_mages_arcane_minDamage, S_mages_arcane_maxDamage, S_mages_arcane_reload, 0); break;
  case TW_SORCERER: T(S_mages_sorcerer_cost, S_mages_sorcerer_range, S_mages_sorcerer_minDamage, S_mages_sorcerer_maxDamage, S_mages_sorcerer_reload, 0); break;
  case TW_ENGINEER:
    if (level == 1) T(S_engineers_level1_cost, S_engineers_level1_range, S_engineers_level1_minDamage, S_engineers_level1_maxDamage, S_engineers_level1_reload, S_engineers_level1_area);
    if (level == 2) T(S_engineers_level2_cost, S_engineers_level2_range, S_engineers_level2_minDamage, S_engineers_level2_maxDamage, S_engineers_level2_reload, S_engineers_level2_area);
    if (level == 3) T(S_engineers_level3_cost, S_engineers_level3_range, S_engineers_level3_minDamage, S_engineers_level3_maxDamage, S_engineers_level3_reload, S_engineers_level3_area);
    break;
  case TW_BFG: T(S_engineers_bfg_cost, S_engineers_bfg_range, S_engineers_bfg_minDamage, S_engineers_bfg_maxDamage, S_engineers_bfg_reload, S_engineers_bfg_area); break;
  case TW_TESLA: T(S_engineers_tesla_cost, S_engineers_tesla_range, S_engineers_tesla_minDamage, S_engineers_tesla_maxDamage, S_engineers_tesla_reload, 0); break;
  }
#undef T
  /* GameSettings.§_-Gm§: the upgrades bought with stars */
  bool archer = kind == TW_ARCHER || kind == TW_RANGER || kind == TW_MUSKETEER;
  bool mage = kind == TW_MAGE || kind == TW_ARCANE || kind == TW_SORCERER;
  bool eng = kind == TW_ENGINEER || kind == TW_BFG || kind == TW_TESLA;
  if (archer && up_on(UP_ARCHERS, 2)) t.range = (int16_t)(t.range + (int)ceilf(t.range * 0.1f));
  if (archer && up_on(UP_ARCHERS, 4)) t.range = (int16_t)(t.range + (int)ceilf(t.range * 0.1f));
  if (mage && upgrades[UP_MAGES] >= 1) t.range = (int16_t)(t.range + (int)ceilf(t.range * 0.1f));
  if (mage && up_on(UP_MAGES, 4))
    t.min_dmg = (int16_t)(t.min_dmg + (int)ceilf(t.min_dmg * 0.15f)), t.max_dmg = (int16_t)(t.max_dmg + (int)ceilf(t.max_dmg * 0.15f));
  if (eng && upgrades[UP_ENGINEERS] >= 1)
    t.min_dmg = (int16_t)(t.min_dmg + (int)ceilf(t.min_dmg * 0.1f)), t.max_dmg = (int16_t)(t.max_dmg + (int)ceilf(t.max_dmg * 0.1f));
  if (eng && up_on(UP_ENGINEERS, 2)) t.range = (int16_t)(t.range + (int)ceilf(t.range * 0.1f));
  /* the mages' and engineers' price cut (Hermetic Study, Field Logistics) */
  if ((mage && up_on(UP_MAGES, 3)) || (eng && up_on(UP_ENGINEERS, 3))) t.cost = (int16_t)ceilf(t.cost - t.cost * 0.1f);
  return t;
}

BarrackStat barrack_stat(int kind, int level) {
  BarrackStat b = {0};
#define BS(g) b = (BarrackStat){S_soldiers_##g##_cost, S_soldiers_##g##_range, S_soldiers_##g##_rangeRally, S_soldiers_##g##_health, \
                                S_soldiers_##g##_armor, S_soldiers_##g##_minDamage, S_soldiers_##g##_maxDamage, S_soldiers_##g##_reload, \
                                S_soldiers_##g##_respawn, S_soldiers_##g##_regen, S_soldiers_##g##_regenReload}
  if (kind == TW_PALADIN) BS(paladin);
  else if (kind == TW_BARBARIAN) BS(barbarian);
  else if (level == 1) BS(level1);
  else if (level == 2) BS(level2);
  else BS(level3);
#undef BS
  if (upgrades[UP_BARRACKS] >= 1) {
    float f = up_on(UP_BARRACKS, 4) ? 0.2f : 0.1f;
    b.health = (int16_t)(b.health + (int)ceilf(b.health * f));
  }
  if (up_on(UP_BARRACKS, 2)) b.armor = (int16_t)(b.armor + 10);
  if (up_on(UP_BARRACKS, 3)) {
    b.respawn = (int16_t)(b.respawn - (int)ceilf(b.respawn * 0.2f));
    b.rally = (int16_t)(b.rally + (int)ceilf(b.rally * 0.2f));
  }
  return b;
}

int tower_range(int kind, int level) {
  if (kind == TW_BARRACKS || kind == TW_PALADIN || kind == TW_BARBARIAN) return barrack_stat(kind, level).rally;
  return tower_stat(kind, level).range;
}

static bool is_barracks(int k) { return k == TW_BARRACKS || k == TW_PALADIN || k == TW_BARBARIAN; }
static int base_kind(int k) {
  switch (k) {
  case TW_RANGER: case TW_MUSKETEER: return TW_ARCHER;
  case TW_ARCANE: case TW_SORCERER: return TW_MAGE;
  case TW_BFG: case TW_TESLA: return TW_ENGINEER;
  case TW_PALADIN: case TW_BARBARIAN: return TW_BARRACKS;
  default: return k;
  }
}
static int lock_kind(int k) {   /* isTowerLocked's numbering: archers, barracks, engineers, mages (as packed) */
  switch (base_kind(k)) {
  case TW_ARCHER: return 0;
  case TW_BARRACKS: return 1;
  case TW_ENGINEER: return 2;
  default: return 3;
  }
}

/* ------------------------------------------------------------------ the shooting cycle */
static void set_level(Spot *t) {
  /* reloadTime: the reload less the charge; the charge plays, then the shot */
  t->target = -1, t->ready = 0, t->reload_t = 0, t->charging = 0, t->charge_t = 0;
  switch (t->kind) {
  case TW_ARCHER: case TW_RANGER: t->charge_len = 8, t->shoot_at = 7; break;
  case TW_MUSKETEER: t->charge_len = 24, t->shoot_at = 7; break;
  case TW_MAGE: t->charge_len = 15, t->shoot_at = 10; break;
  case TW_ARCANE: t->charge_len = 38, t->shoot_at = 18; break;
  case TW_SORCERER: t->charge_len = 25, t->shoot_at = 14; break;
  case TW_ENGINEER: t->charge_len = 34, t->shoot_at = 10; break;
  case TW_BFG: t->charge_len = 48, t->shoot_at = 22; break;
  case TW_TESLA: t->charge_len = 63, t->shoot_at = 48; break;
  default: t->charge_len = 0;
  }
  if (!is_barracks(t->kind)) t->reload = (int16_t)(tower_stat(t->kind, t->level).reload - t->charge_len);
  /* the shooters stand at the tower's top (§_-5E§ right, §_-0C§ left) */
}

static void barracks_soldiers(int s, int kind) {
  Spot *t = &B.spot[s];
  for (int k = 0; k < 3; k++) {
    Soldier *old = SO(t->soldiers[k]);
    float ox = old ? old->x : t->x, oy = old ? old->y : t->y - 0.01f;
    bool was_dead = old ? old->dead || old->respawning : true;
    if (old) soldier_free(old);
    /* their places: around the rally point, on an ellipse 40 x 28, from 180 degrees in thirds */
    float ang = (180.0f + 120.0f * (float)k - 90.0f) * (float)M_PI / 180.0f;
    Pt r = {t->rally.x + cosf(ang) * 20.0f, t->rally.y + 10.0f + sinf(ang) * 14.0f};
    int i = soldier_new(kind, s, (Pt){ox, oy}, r);
    t->soldiers[k] = (int8_t)i;
    if (i >= 0 && old && !was_dead) {
      Soldier *so = &B.so[i];
      so->respawning = 0, so->active = 1, so->walking = 1, so->hidden = 0;
      so->dx = r.x, so->dy = r.y;
    }
  }
}

static int soldier_kind(int kind, int level) {
  if (kind == TW_PALADIN) return SK_PALADIN;
  if (kind == TW_BARBARIAN) return SK_BARBARIAN;
  return level == 1 ? SK_L1 : level == 2 ? SK_L2 : SK_L3;
}

static void finish(int s) {
  Spot *t = &B.spot[s];
  t->building = 0;
  set_level(t);
  if (is_barracks(t->kind)) barracks_soldiers(s, soldier_kind(t->kind, t->level));
  special_init(s);
}

void spot_build(int s, int kind) {
  Spot *t = &B.spot[s];
  int cost = kind == TW_BARRACKS ? barrack_stat(TW_BARRACKS, 1).cost : tower_stat(kind, 1).cost;
  memset(&t->charging, 0, (size_t)((uint8_t *)&t->premade - &t->charging));
  t->kind = (uint8_t)kind, t->level = 1;
  t->spent = (int16_t)cost;
  battle_cash(-cost);
  t->building = BUILD_TIME;
  t->target = -1, t->elemental = -1;
  for (int k = 0; k < 3; k++) t->soldiers[k] = -1;
}

void spot_make(int s, int kind, int level) {
  Spot *t = &B.spot[s];
  t->kind = (uint8_t)kind, t->level = (uint8_t)level, t->premade = 1, t->spent = 0;
  t->target = -1, t->elemental = -1;
  for (int k = 0; k < 3; k++) t->soldiers[k] = -1;
  finish(s);
}

/* what the next step costs: choice 0 the next level, 1 and 2 the specials; -1 if there is none, -2 locked */
int spot_cost(int s, int choice) {
  Spot *t = &B.spot[s];
  if (t->kind == TW_NONE) {   /* the build menu: choice 1 archers, 2 barracks, 3 mages, 4 engineers */
    static const uint8_t kinds[5] = {0, TW_ARCHER, TW_BARRACKS, TW_MAGE, TW_ENGINEER};
    int k = kinds[choice < 0 || choice > 4 ? 0 : choice];
    if (!k) return -1;
    if (tower_locked(lock_kind(k), 1)) return -2;
    return k == TW_BARRACKS ? barrack_stat(k, 1).cost : tower_stat(k, 1).cost;
  }
  if (t->level >= 4 || t->building) return -1;
  if (choice == 0) {
    if (t->level == 3) return -1;
    if (tower_locked(lock_kind(t->kind), t->level + 1)) return -2;
    return is_barracks(t->kind) ? barrack_stat(t->kind, t->level + 1).cost : tower_stat(t->kind, t->level + 1).cost;
  }
  if (t->level != 3) return -1;
  int k = special_kind(t->kind, choice);
  /* the first special (ranger, arcane, BFG, paladin) needs unlock 4, the second 5 */
  bool second = k == TW_MUSKETEER || k == TW_SORCERER || k == TW_TESLA || k == TW_BARBARIAN;
  if (tower_locked(lock_kind(t->kind), second ? 5 : 4)) return -2;
  return is_barracks(k) ? barrack_stat(k, 4).cost : tower_stat(k, 4).cost;
}

void spot_upgrade(int s, int choice) {
  Spot *t = &B.spot[s];
  int cost = spot_cost(s, choice);
  if (cost < 0) return;
  battle_cash(-cost);
  t->spent = (int16_t)(t->spent + cost);
  fx_add(AN_T_CLOUD, t->x, t->y, false, false);
  if (choice == 0) {
    t->level++;
    set_level(t);
    if (is_barracks(t->kind)) barracks_soldiers(s, soldier_kind(t->kind, t->level));
    return;
  }
  t->kind = (uint8_t)special_kind(t->kind, choice);
  t->level = 4;
  set_level(t);
  if (is_barracks(t->kind)) barracks_soldiers(s, soldier_kind(t->kind, t->level));
  special_init(s);
}

int spot_sell_value(int s) {
  Spot *t = &B.spot[s];
  if (base_kind(t->kind) == TW_ARCHER && upgrades[UP_ARCHERS] >= 1) return (int)((float)t->spent * 0.9f);
  return (int)((float)t->spent * 0.6f);
}

void spot_sell(int s) {
  Spot *t = &B.spot[s];
  int v = spot_sell_value(s);
  battle_cash(v);
  for (int k = 0; k < 3; k++) {
    Soldier *so = SO(t->soldiers[k]);
    if (so) soldier_free(so);
    t->soldiers[k] = -1;
  }
  special_sold(s);
  fx_add(AN_T_SELL, t->x, t->y, false, false);
  fx_value(t->x, t->y - 30, v);
  Pt rally = t->rally;
  int16_t x = t->x, y = t->y;
  memset(t, 0, sizeof *t);
  t->x = x, t->y = y, t->rally = rally, t->target = -1, t->elemental = -1;
  for (int k = 0; k < 3; k++) t->soldiers[k] = -1;
}

/* ------------------------------------------------------------------ targets */
bool tower_in_range(const Spot *t, float x, float y) {
  int r = tower_stat(t->kind, t->level).range;
  return ellipse_contains(t->x, t->y - 10, (float)r, r * RANGE_RATIO, x, y);
}

/* findNewTarget: the active enemy in range furthest along its road */
int tower_find_target(Spot *t, bool fliers, bool predicted) {
  int best = -1;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active || (!fliers && e->flying)) continue;
    float x = e->x, y = e->y;
    if (predicted) {   /* the engineers aim where it will be */
      Pt p = enemy_road(e, enemy_nodes_speed(e));
      x = p.x, y = p.y;
    }
    if (!tower_in_range(t, x, y)) continue;
    if (best < 0) {
      best = i;
      continue;
    }
    Enemy *b = &B.en[best];
    if (path_len(b->path, b->lane) - b->node > path_len(e->path, e->lane) - e->node) best = i;
  }
  t->target = (int8_t)best;
  return best;
}

/* ------------------------------------------------------------------ shots */
static void shoot_arrow(int s) {
  Spot *t = &B.spot[s];
  Enemy *e = EN(t->target);
  bool have = e && e->active;
  if (!have) {
    int k = tower_find_target(t, true, false);
    e = EN(k);
    have = e != 0;
  }
  Bullet *b = bullet_new(BU_ARROW);
  if (!b) return;
  t->arrows++;
  bool right = t->arrows & 1;
  float lift = t->level >= 3 ? (t->kind == TW_ARCHER ? 50 : 53) : 45;
  if (t->kind == TW_RANGER || t->kind == TW_MUSKETEER) lift = 53;
  b->x = b->x0 = t->x + (right ? (t->level >= 3 && t->kind == TW_ARCHER ? 8 : 5) : (t->level >= 3 && t->kind == TW_ARCHER ? -7 : -9));
  b->y = b->y0 = t->y - lift;
  b->level = t->kind == TW_RANGER ? 4 : t->level;
  b->t1 = 17;
  b->target = have ? (int8_t)(e - B.en) : -1;
  if (!have) b->tx = t->dest.x, b->ty = t->dest.y;
  else if (e->held) b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
  else {
    b->tx = e->x + enemy_stats[e->type].x_adj + e->xs * b->t1 + rnd() * 10;
    b->ty = e->y + enemy_stats[e->type].y_adj + e->ys * b->t1 + rnd() * 10 - 10;
  }
  b->g = 1;
  b->xs = (b->tx - b->x0) / b->t1;
  b->ys = (b->ty - b->y0 - b->g * b->t1 * (b->t1 / 2.0f)) / b->t1;
  if (up_on(UP_ARCHERS, 5) && rnd() <= 0.1f) b->crit = 1;   /* Precision: double damage */
  if (up_on(UP_ARCHERS, 3)) b->pierce = 10;                 /* Piercing Shots */
  special_arrow(s, b);
  t->shots++;
}

static void shoot_bolt(int s) {
  Spot *t = &B.spot[s];
  Enemy *e = EN(t->target);
  if (!e || !e->active) {
    e = EN(tower_find_target(t, true, false));
    if (!e) return;
  }
  bool down = e->y >= t->y;
  Bullet *b = bullet_new(BU_BOLT);
  if (!b) return;
  float up_y = t->level >= 3 ? 51 : 48, down_y = t->level >= 3 ? 53 : 48;
  b->x = t->x + (down ? (t->level >= 3 ? -7 : -6) : 6), b->y = t->y - (down ? down_y : up_y);
  b->target = (int8_t)(e - B.en), b->level = t->level;
  b->acc = 1, b->acc_max = 10;
  b->first = 1;
  b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
  TowerStat st = tower_stat(t->kind, t->level);
  b->min_dmg = st.min_dmg, b->max_dmg = st.max_dmg;
  special_bolt(s, b);
  t->shots++;
}

static void shoot_bomb(int s) {
  Spot *t = &B.spot[s];
  Enemy *e = EN(t->target);
  if (!e || !e->active) {
    e = EN(tower_find_target(t, false, true));
    if (e) {
      Pt p = enemy_road(e, enemy_nodes_speed(e));
      t->dest = p;
    }
  } else if (e->blocked)
    t->dest = (Pt){e->x, e->y};
  Bullet *b = bullet_new(BU_BOMB);
  if (!b) return;
  b->x = b->x0 = t->x, b->y = b->y0 = t->y - 45;
  b->tx = t->dest.x, b->ty = t->dest.y;
  b->t1 = 27, b->g = 1;
  b->xs = (b->tx - b->x0) / b->t1;
  b->ys = (b->ty - b->y0 - b->g * b->t1 * b->t1 / 2) / b->t1;
  TowerStat st = tower_stat(t->kind, t->level);
  b->min_dmg = st.min_dmg, b->max_dmg = st.max_dmg, b->area = st.area, b->level = t->level;
  b->rot = rnd() * 100;
  special_bomb(s, b);
  t->shots++;
}

/* §_-06§: the shot starts (the shooter turns, the tower plays its shot) */
static void start_shot(int s) {
  Spot *t = &B.spot[s];
  Enemy *e = EN(t->target);
  t->ready = 0, t->charging = 1, t->charge_t = 0, t->idle_t = 0;
  if (!e) return;
  int k = (t->arrows + 1) & 1;
  t->shooter_flip[k] = e->x < t->x;
  t->shooter_up[k] = e->y < t->y;
  t->shooter_t[k] = 0;
  t->anim_t = 0;
  t->dest = (Pt){e->x, e->y};
  if (base_kind(t->kind) == TW_ENGINEER) t->dest = enemy_road(e, enemy_nodes_speed(e));
}

/* ------------------------------------------------------------------ a frame */
static void ice_step(Spot *t) {
  /* IceBlock: 23 frames forming, then held until it takes 3 hits, then 44 melting (free at 25) */
  if (t->ice == 1 && ++t->ice_t >= 23) t->ice = 2, t->ice_t = 0;
  else if (t->ice == 3) {
    if (++t->ice_t == 25) t->disabled = 0;
    if (t->ice_t >= 44) t->ice = 0;
  }
}

void tower_step(int s) {
  Spot *t = &B.spot[s];
  if (t->kind == TW_NONE || t->kind == 0xFF) return;
  t->anim_t++, t->shooter_t[0]++, t->shooter_t[1]++;
  if (t->ice) ice_step(t);
  if (t->disabled) return;
  if (t->building) {
    if (--t->building == 0) finish(s);
    return;
  }
  if (is_barracks(t->kind)) {
    barracks_step(s);
    return;
  }
  special_timers(s);
  if (t->charging) {
    t->charge_t++;
    bool at = t->charge_t == t->shoot_at;
    if (t->charge_t == t->charge_len) t->charge_t = 0, t->charging = 0;
    if (at) {
      switch (base_kind(t->kind)) {
      case TW_ARCHER: if (!special_shoot(s)) shoot_arrow(s); break;
      case TW_MAGE: if (!special_shoot(s)) shoot_bolt(s); break;
      case TW_ENGINEER: if (!special_shoot(s)) shoot_bomb(s); break;
      }
    }
    return;
  }
  bool mage_like = base_kind(t->kind) != TW_ARCHER;
  if (mage_like && special_run(s)) return;
  if (!t->ready && ++t->reload_t >= t->reload) t->ready = 1, t->reload_t = 0;
  if (t->ready) {
    if (!mage_like && special_run(s)) return;
    bool fliers = base_kind(t->kind) != TW_ENGINEER || t->kind == TW_TESLA;
    if (tower_find_target(t, fliers, base_kind(t->kind) == TW_ENGINEER && t->kind != TW_TESLA) >= 0) start_shot(s);
    else if (++t->idle_t == 90) t->shooter_up[0] = t->shooter_up[1] = 0;
  }
}

/* ------------------------------------------------------------------ barracks */
void barracks_step(int s) {
  Spot *t = &B.spot[s];
  /* the door: opens over 12 frames (soldiers come out), stays, closes from 22 to 25 */
  if (t->door) {
    if (t->door_t < 25) {
      if (t->door_t == 12) t->door = 2;
      else if (t->door_t > 22) t->door = 3;
      t->door_t++;
    } else
      t->door = 0;
  }
  special_timers(s);
}

bool barracks_door_closed(int s) { return B.spot[s].door == 0; }
bool barracks_door_opening(int s) { return B.spot[s].door == 1; }
int barracks_door_t(int s) { return B.spot[s].door_t; }
void barracks_open(int s) {
  Spot *t = &B.spot[s];
  if (t->door == 0) t->door = 1, t->door_t = 0, t->anim_t = 0;
}

bool spot_rally_ok(int s, Pt p) {
  Spot *t = &B.spot[s];
  int r = t->kind == TW_SORCERER ? S_mages_sorcerer_elementalRallyRange : barrack_stat(TW_BARRACKS, 1).rally;
  if (!ellipse_contains(t->x, t->y - 10, (float)r, r * RANGE_RATIO, p.x, p.y)) return false;
  for (int pa = 0; pa < B.npaths; pa++) {
    int n = path_len(pa, 0);
    for (int k = 0; k < n; k++) {
      Pt q = path_node(pa, 0, k);
      float dx = q.x - p.x, dy = q.y - p.y;
      if (dx * dx + dy * dy < 900) return true;
    }
  }
  return false;
}

void spot_rally(int s, Pt p) {
  Spot *t = &B.spot[s];
  if (t->kind == TW_SORCERER) {
    special_rally(s, p);
    return;
  }
  t->rally = p;
  for (int k = 0; k < 3; k++) {
    Soldier *so = SO(t->soldiers[k]);
    if (!so) continue;
    float ang = (180.0f + 120.0f * (float)k - 90.0f) * (float)M_PI / 180.0f;
    soldier_set_rally(so, (Pt){p.x + cosf(ang) * 20.0f, p.y + 10.0f + sinf(ang) * 14.0f});
  }
  fx_add(AN_T_FLAG, p.x, p.y, false, false);
}
