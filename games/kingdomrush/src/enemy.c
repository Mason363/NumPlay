/* The enemies (Enemy.as, EnemyCommon.as and each Enemy*.as). */
#include <math.h>
#include <string.h>
#include "anim.h"
#include "kr.h"
#include "settings.h"

#define ST(e) (&enemy_stats[(e)->type])

float ellipse_contains(float cx, float cy, float w, float h, float px, float py) {
  /* casalib's Ellipse.containsPoint, the ellipse given by its centre and size */
  float rx = w / 2, ry = h / 2;
  if (rx <= 0 || ry <= 0) return 0;
  float dx = (px - cx) / rx, dy = (py - cy) / ry;
  return dx * dx + dy * dy <= 1.0f;
}

Pt enemy_road(const Enemy *e, int ahead) {
  int n = e->node + ahead, len = path_len(e->path, e->lane);
  if (n >= len) n = len - 1;
  if (n < 0) n = 0;
  return path_node(e->path, e->lane, n);
}

int enemy_nodes_speed(const Enemy *e) {
  if (e->blocked || e->held) return 0;
  int k = (int)ceilf(5.5f * e->speed), len = path_len(e->path, e->lane);
  if (e->node + k >= len) return len - e->node - 1;
  return k;
}

/* ------------------------------------------------------------------ animations */
static void play(Enemy *e, int an) {
  if (an < 0) return;
  e->anim = (uint16_t)an, e->anim_t = 0;
}

/* an enemy's animation (Vez'nan's demon has its own) */
static int anim_of(const Enemy *e, int a) {
  if (e->type == EN_VEZNAN && e->froze == 3) {
    static const int16_t demon[6] = {AN_E_VEZNAN_WALKINGDEMON, AN_E_VEZNAN_WALKINGDEMON, AN_E_VEZNAN_WALKINGDOWNDEMON,
                                     AN_E_VEZNAN_IDLEDEMON, AN_E_VEZNAN_FIGHTINGDEMON, AN_E_VEZNAN_DEAD};
    return demon[a];
  }
  return ANIM(e->type, a);
}

/* getFacing: the walk for the road's direction */
static void facing(Enemy *e) {
  if (e->fighting) {
    e->charge_t = 0;
    play(e, anim_of(e, A_IDLE));
    return;
  }
  Pt a = enemy_road(e, 0), b = enemy_road(e, 1);
  int ang = (int)lroundf(atan2f(-(b.y - a.y), b.x - a.x) * 180.0f / (float)M_PI);
  if (ang < 0) ang += 360;
  int d;
  if (ang > 55 && ang < 135) d = 3;
  else if (ang >= 135 && ang <= 240) d = 2;
  else if (ang > 240 && ang < 315) d = 4;
  else d = 1;
  if (d == e->dir) return;
  e->dir = (uint8_t)d;
  if (d == 1 || d == 2) e->left = d == 2;
  int an = anim_of(e, d == 3 ? A_UP : d == 4 ? A_DOWN : A_WALK);
  if (an < 0) an = anim_of(e, A_WALK);
  play(e, an);
}

/* ------------------------------------------------------------------ spawn */
int enemy_spawn(int type, int path, int lane, int node) {
  int i = 0;
  while (i < MAX_ENEMIES && B.en[i].alive) i++;
  if (i == MAX_ENEMIES) return -1;
  Enemy *e = &B.en[i];
  memset(e, 0, sizeof *e);
  const EnemyStat *s = &enemy_stats[type];
  e->alive = 1, e->type = (uint8_t)type, e->path = (uint8_t)path, e->lane = (uint8_t)lane;
  e->node = (int16_t)node;
  Pt p = path_node(path, lane, node);
  e->x = p.x, e->y = p.y;
  e->health = e->init_health = (s->health * enemy_health_pct + 50) / 100;
  if (s->health == 0) e->health = e->init_health = 1;
  e->armor = s->armor, e->magic_armor = s->magic_armor, e->dodge = s->dodge;
  e->gold = s->gold, e->cost = s->cost, e->min_dmg = s->min_dmg, e->max_dmg = s->max_dmg;
  e->speed = e->base_speed = s->speed;
  e->margin = s->speed + 0.1f;
  e->flying = (uint8_t)s->flying, e->boss = (uint8_t)s->boss;
  e->charge = s->charge;
  e->reload = (int16_t)(s->reload - s->charge);
  e->dead_time = s->dead_time;
  e->soldier = -1;
  /* the specials' first timers */
  if (type == EN_SPIDER) e->sp_t2 = (int16_t)rnd_round(S_enemies_spider_eggsCooldownTimeMin, S_enemies_spider_eggsCooldownTimeMax);
  if (type == EN_SHADOWARCHER || type == EN_TROLLAXETHROWER)
    e->sp_n = (int16_t)((type == EN_SHADOWARCHER ? S_enemies_shadowArcher_arrowRange : S_enemies_trollAxeThrower_axeRange) +
                        rnd_round(-70, 30));
  facing(e);
  return i;
}

/* ------------------------------------------------------------------ damage */
/* getArmorDamage (the game subtracts this Number from an int: the result is cut to an int) */
static float armor_loss(const Enemy *e, int type, int dmg, int pierce) {
  int a = e->armor;
  if (pierce) {
    a -= pierce;
    if (a < 0) a = 0;
  }
  float d = (float)dmg;
  switch (type) {
  case ARM_P: return d - (float)a * d / 100.0f;
  case ARM_M: return d - (float)e->magic_armor * d / 100.0f;
  case ARM_E: return d - (float)a / 2.0f * d / 100.0f;
  default: return d;
  }
}

static void dead_status(Enemy *e) {
  e->active = 0, e->dead = 1;
  battle_cash(e->gold);
  B.kills++;
  if (e->soldier >= 0) enemy_unblock(e);
}

void enemy_kill(Enemy *e, int how) {
  if (e->dead) return;
  if (how != 3) grave_rise(e);   /* (toGraveyard) */
  dead_status(e);
  if (e->type == EN_VEZNAN) {   /* EnemyVeznan.setDeadBasicStatus: the graveyard gone, and every other enemy */
    B.grave = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
      Enemy *o = &B.en[i];
      if (o != e && o->alive && !o->dead) {
        dead_status(o);
        fx_add(ST(o)->blood == 1 ? AN_B_BLOODGREEN : ST(o)->blood == 2 ? -1 : AN_B_BLOOD, o->x, o->y + 1, false, true);
      }
    }
  }
  if (how == 1) {   /* blown up (EnemyExplode*) */
    fx_add(e->flying ? AN_FX_EXPLODEAIR : ST(e)->size == SZ_BIG ? AN_FX_EXPLODEBIG : AN_FX_EXPLODENORMAL, e->x, e->y, false, false);
    e->alive = 0;
  } else if (how == 2) {   /* desintegrated by the arcane death ray */
    fx_add(e->flying ? AN_FX_DESINTEGRATEAIR : ST(e)->size == SZ_BIG ? AN_FX_DESINTEGRATEBIG : AN_FX_DESINTEGRATENORMAL,
           e->x, e->y, false, false);
    e->alive = 0;
  } else if (how == 0) {
    fx_add(ST(e)->blood == 1 ? AN_B_BLOODGREEN : ST(e)->blood == 2 ? -1 : AN_B_BLOOD, e->x, e->y + 1, false, true);
  }
}

void enemy_damage(Enemy *e, int dmg, int type, int pierce) {
  if (e->dead) return;
  if (e->sp_state == 9 && type != 99) {   /* a demon lord's shield takes the hit */
    if (--e->sp_n <= 0) e->sp_state = 0;
    return;
  }
  float loss = type == ARM_I ? (float)dmg : armor_loss(e, type, dmg, pierce);
  e->health = (int32_t)((float)e->health - loss);
  if (e->type == EN_ROCKETEER && !e->sp_state && e->health > 0) {   /* DebuffRocketeer: a rush at its first hit */
    e->sp_state = 1;
    if (!e->net_t && !e->slow_t) {
      e->sp_t = S_enemies_rocketeer_fastSpeedDuration;
      e->speed = S_enemies_rocketeer_fastSpeed, e->margin = e->speed + 0.1f;
      e->dir = 0;
      facing(e);
    }
  }
  if (e->health <= 0) {
    if (e->type == EN_VEZNAN && e->froze == 1) {   /* gotoDemon: the wizard falls, the demon rises */
      e->health = 1, e->froze = 2, e->sp_c = 0, e->active = 0;
      if (e->soldier >= 0) enemy_unblock(e);
      play(e, AN_E_VEZNAN_TODEMON);
      return;
    }
    e->health = 0;
    enemy_kill(e, 0);
  }
}

void enemy_armor_down(Enemy *e, int v) {
  if (e->armor == 0) return;
  e->armor = (int16_t)(e->armor - v);
  if (e->armor < 0) e->armor = 0;
}

bool enemy_dodge(Enemy *e) { return e->dodge && rnd() * 100.0f <= (float)e->dodge; }

/* ------------------------------------------------------------------ soldiers */
void enemy_block(Enemy *e, int soldier) {
  e->soldier = (int8_t)soldier, e->blocked = 1;
  play(e, anim_of(e, A_IDLE));
}

void enemy_unblock(Enemy *e) {   /* §_-Ef§ */
  e->blocked = 0, e->fighting = 0, e->charging = 0, e->soldier = -1;
  e->dir = 0;
  if (!e->dead) facing(e);
}

void enemy_start_fighting(Enemy *e) {
  e->fighting = 1, e->dir = 0;
  e->y += 0.1f;
  play(e, anim_of(e, A_FIGHT));
  e->charging = 1;
}

int enemy_fighter_position(Enemy *e) {
  if (e->fpos[0] == 0) return e->fpos[0]++, 0;
  if (e->fpos[1] == 0) return e->fpos[1]++, 1;
  if (rnd() >= 0.5f) return e->fpos[0]++, 0;
  return e->fpos[1]++, 1;
}
void enemy_free_position(Enemy *e, int pos) {
  if (pos >= 0 && pos < 2 && e->fpos[pos]) e->fpos[pos]--;
}

static void attack(Enemy *e) {
  Soldier *so = SO(e->soldier);
  if (!so) return;
  int dmg = rnd_dmg(e->min_dmg, e->max_dmg);
  if (enemy_area_attack(e)) {   /* the big ones hit everyone around (yetis, lava) */
    int n = 0;
    for (int i = 0; i < MAX_SOLDIERS && n < 10; i++) {
      Soldier *o = &B.so[i];
      if (o->alive && o->active && ellipse_contains(e->x, e->y, 70, 49, o->x, o->y)) soldier_damage(o, dmg, false), n++;
    }
    return;
  }
  soldier_damage(so, dmg, false);
}

/* ------------------------------------------------------------------ the road */
static bool reached_node(Enemy *e) {   /* §_-M9§: true when it left the battlefield */
  int len = path_len(e->path, e->lane);
  if (e->node == 10) {
    e->active = 1;
    return false;
  }
  if (e->node + 11 == len) {
    e->active = 0;
    return false;
  }
  if (e->node + 7 < len) return false;
  e->active = 0;
  battle_lose_lives(e->cost);
  battle_cash(e->gold);
  e->alive = 0;
  if (e->soldier >= 0) {
    Soldier *so = SO(e->soldier);
    if (so) so->enemy = -1;
  }
  return true;
}

static void move(Enemy *e) {
  if (e->blocked) {
    e->xs = e->ys = 0;
    return;
  }
  Pt n = enemy_road(e, 1);
  float a = atan2f(n.y - e->y, n.x - e->x);
  e->ys = sinf(a) * e->speed, e->xs = cosf(a) * e->speed;
  e->x += e->xs, e->y += e->ys;
  n = enemy_road(e, 1);
  float dx = n.x - e->x, dy = n.y - e->y;
  if (sqrtf(dx * dx + dy * dy) < e->margin) {
    e->node++;
    if (reached_node(e)) return;
    facing(e);
  }
}

/* ------------------------------------------------------------------ debuffs (DebuffPoison, Curse, Slow, Net) */
static void debuffs(Enemy *e) {
  /* DebuffPoison: its damage each second (true damage) while it lasts */
  if (e->poison_t > 0) {
    e->poison_t--;
    if (e->poison_tick < FPS) e->poison_tick++;
    else {
      enemy_damage(e, S_archers_ranger_poisonDamage * e->poison_lvl, ARM_I, 0);
      e->poison_tick = 0;
      if (e->dead) return;
    }
  }
  /* DebuffCurse: half its armour, 10 a second */
  if (e->curse_t > 0) {
    if (e->curse_tick < FPS) e->curse_tick++;
    else {
      enemy_damage(e, S_mages_sorcerer_curseDamage, ARM_I, 0);
      e->curse_tick = 0;
      if (e->dead) return;
    }
    if (--e->curse_t == 0) e->armor = (int16_t)(e->armor + e->curse_amt), e->curse_amt = 0;
  }
  if (e->slow_t > 0 && --e->slow_t == 0) e->speed = e->slow_speed, e->margin = e->speed + 0.1f;
  if (e->net_t > 0 && --e->net_t == 0) e->speed = e->slow_speed, e->margin = e->speed + 0.1f;
  if (e->type == EN_ROCKETEER && e->sp_t > 0 && --e->sp_t == 0) {
    e->speed = e->base_speed, e->margin = e->speed + 0.1f;
    e->dir = 0;
    facing(e);
  }
}

/* ------------------------------------------------------------------ the specials */

/* a soldier for a ranged attack: in range, not too close (§_-F9§, §_-9s§) */
static int ranged_target(Enemy *e, int range, int min_range) {
  for (int i = 0; i < MAX_SOLDIERS; i++) {
    Soldier *so = &B.so[i];
    if (!so->alive || !so->active) continue;
    if (!ellipse_contains(e->x, e->y, (float)range, range * RANGE_RATIO, so->x, so->y)) continue;
    float dx = so->x - e->x, dy = so->y - e->y;
    if (sqrtf(dx * dx + dy * dy) > (float)min_range) return i;
  }
  return -1;
}

/* the shadow archer's arrows, the axe thrower's axes, the necromancer's bolts: stop, aim, throw */
static bool ranged(Enemy *e, int cooldown, int min_range, int charge_len, int fire_at, int kind, int an) {
  if (e->sp_state != 2) {
    if (e->blocked) {
      e->held = 0;
      return false;
    }
    if (e->sp_t < cooldown) {
      e->sp_t++;
      return e->held;
    }
    int t = ranged_target(e, e->sp_n, min_range);
    if (t < 0) {
      if (e->held) {
        e->held = 0, e->dir = 0;
        facing(e);
      }
      return false;
    }
    e->left = B.so[t].x < e->x;
    e->sp_state = 2, e->held = 1, e->sp_t2 = 0;
    e->sp_n2 = (int16_t)t;
    play(e, an);
    return true;
  }
  if (e->sp_t2 < charge_len) {
    e->sp_t2++;
    if (e->sp_t2 == fire_at) {
      Soldier *so = SO(e->sp_n2);
      if (!so || !so->active) {
        int t = ranged_target(e, e->sp_n, min_range);
        so = t >= 0 ? &B.so[t] : 0;
      }
      Bullet *b = bullet_new(kind);
      if (b) {
        b->x = b->x0 = e->x + (e->left ? -4 : 4), b->y = b->y0 = e->y - 12.5f;
        b->target = so ? (int8_t)(so - B.so) : -1;
        b->tx = so ? so->x : e->x, b->ty = so ? so->y - 7 : e->y;
        b->t1 = 17;
        b->xs = (b->tx - b->x0) / b->t1;
        b->ys = (b->ty - b->y0 - 1.0f * b->t1 * (b->t1 / 2.0f)) / b->t1;
        b->g = 1;
        if (kind == BU_ARROW_ENEMY) b->min_dmg = S_enemies_shadowArcher_arrowMinDamage, b->max_dmg = S_enemies_shadowArcher_arrowMaxDamage;
        else if (kind == BU_AXE_ENEMY) b->min_dmg = S_enemies_trollAxeThrower_axeMinDamage, b->max_dmg = S_enemies_trollAxeThrower_axeMaxDamage;
        else b->min_dmg = S_enemies_necromancer_spellMinDamage, b->max_dmg = S_enemies_necromancer_spellMaxDamage;
      }
    }
    return true;
  }
  e->sp_state = 0, e->sp_t = 0;
  return true;
}

static void heal(Enemy *e, int v) {
  e->health += v;
  if (e->health > e->init_health) e->health = e->init_health;
}

static bool special_power(Enemy *e) {
  switch (e->type) {
  case EN_SHAMAN:   /* heals 3 wounded around it every 8 s */
    if (e->sp_state != 1) {
      if (e->sp_t < S_enemies_shaman_healCoolDown) {
        e->sp_t++;
        return false;
      }
      bool any = false;
      for (int i = 0; i < MAX_ENEMIES && !any; i++) {
        Enemy *o = &B.en[i];
        any = o->alive && o->active && o->health < o->init_health &&
              ellipse_contains(e->x, e->y, S_enemies_shaman_healRange, S_enemies_shaman_healRange * RANGE_RATIO, o->x, o->y);
      }
      if (!any) return false;
      e->sp_state = 1, e->sp_t2 = 0;
      play(e, AN_E_SHAMAN_HEALING);
      return true;
    }
    if (e->sp_t2 < 28) {
      if (++e->sp_t2 == 14) {
        int n = 0;
        for (int i = 0; i < MAX_ENEMIES; i++) {
          Enemy *o = &B.en[i];
          if (o->alive && o->active && o->health < o->init_health &&
              ellipse_contains(e->x, e->y, S_enemies_shaman_healRange, S_enemies_shaman_healRange * RANGE_RATIO, o->x, o->y)) {
            heal(o, S_enemies_shaman_healPoints);
            if (++n == S_enemies_shaman_healMaxEnemies) break;
          }
        }
      }
      return true;
    }
    e->sp_state = 0, e->sp_t = 0, e->dir = 0;
    if (!e->blocked) facing(e);
    return false;
  case EN_SPIDER:   /* lays up to 3 eggs, each hatching 3 spiderlings */
    if (e->sp_n >= S_enemies_spider_eggsMax) return false;
    if (e->sp_t < e->sp_t2) {
      e->sp_t++;
      return false;
    }
    if (e->blocked) return false;
    egg_lay(e);
    e->sp_t = 0, e->sp_n++;
    return false;
  case EN_SHADOWARCHER:
    return ranged(e, S_enemies_shadowArcher_arrowCoolDown, S_enemies_shadowArcher_arrowMinRange, 12, 7, BU_ARROW_ENEMY,
                  AN_E_SHADOWARCHER_ARROW);
  case EN_TROLLAXETHROWER:
    return ranged(e, S_enemies_trollAxeThrower_axeCoolDown, S_enemies_trollAxeThrower_axeMinRange, 15, 8, BU_AXE_ENEMY,
                  AN_E_TROLLAXETHROWER_AXE);
  case EN_TROLLCHIEFTAIN:   /* rage: 3 trolls around it healed, stronger, faster, armoured for 6 s */
    if (e->sp_state != 1) {
      if (e->sp_t < S_enemies_trollChieftain_rageCoolDown) {
        e->sp_t++;
        return false;
      }
      e->sp_state = 1, e->sp_t2 = 0;
      play(e, AN_E_TROLLCHIEFTAIN_RAGE);
      return true;
    }
    if (e->sp_t2 < 49) {
      if (++e->sp_t2 == 25) {
        int n = 0;
        for (int i = 0; i < MAX_ENEMIES && n < S_enemies_trollChieftain_rageMaxEnemies; i++) {
          Enemy *o = &B.en[i];
          if (!o->alive || !o->active || o == e || o->rage_t) continue;
          if (o->type != EN_TROLL && o->type != EN_TROLLAXETHROWER && o->type != EN_TROLLCHIEFTAIN) continue;
          if (!ellipse_contains(e->x, e->y, S_enemies_trollChieftain_rageRange, S_enemies_trollChieftain_rageRange * RANGE_RATIO, o->x, o->y)) continue;
          heal(o, S_enemies_trollChieftain_rageHealPoints);
          o->rage_t = S_enemies_trollChieftain_rageDurationTime;
          o->min_dmg = (int16_t)(o->min_dmg + S_enemies_trollChieftain_rageAddedMinDamage);
          o->max_dmg = (int16_t)(o->max_dmg + S_enemies_trollChieftain_rageAddedMaxDamage);
          o->speed += S_enemies_trollChieftain_rageAddedSpeed, o->margin = o->speed + 0.1f;
          o->base_speed += S_enemies_trollChieftain_rageAddedSpeed;
          o->armor = (int16_t)(o->armor + S_enemies_trollChieftain_rageAddedArmor);
          n++;
        }
      }
      return true;
    }
    e->sp_state = 0, e->sp_t = 0, e->dir = 0;
    if (!e->blocked) facing(e);
    return false;
  case EN_DEMONMAGE:   /* shields 4 demons around it: they shrug off 4 hits */
    if (e->sp_state != 1) {
      if (e->sp_t < S_enemies_demonMage_shieldCoolDown) {
        e->sp_t++;
        return false;
      }
      bool any = false;
      for (int i = 0; i < MAX_ENEMIES && !any; i++) {
        Enemy *o = &B.en[i];
        any = o->alive && o->active && (o->type == EN_DEMON || o->type == EN_DEMONWOLF) && o->sp_state != 9 &&
              ellipse_contains(e->x, e->y, S_enemies_demonMage_shieldRange, S_enemies_demonMage_shieldRange * RANGE_RATIO, o->x, o->y);
      }
      if (!any) return false;
      e->sp_state = 1, e->sp_t2 = 0;
      play(e, AN_E_DEMONMAGE_SHIELD);
      return true;
    }
    if (e->sp_t2 < 24) {
      if (++e->sp_t2 == 15) {
        int n = 0;
        for (int i = 0; i < MAX_ENEMIES && n < S_enemies_demonMage_shieldMaxEnemies; i++) {
          Enemy *o = &B.en[i];
          if (o->alive && o->active && (o->type == EN_DEMON || o->type == EN_DEMONWOLF) && o->sp_state != 9 &&
              ellipse_contains(e->x, e->y, S_enemies_demonMage_shieldRange, S_enemies_demonMage_shieldRange * RANGE_RATIO, o->x, o->y))
            o->sp_state = 9, o->sp_n = S_enemies_demonMage_shieldIgnoreHits, n++;
        }
      }
      return true;
    }
    e->sp_state = 0, e->sp_t = 0, e->charge_t = 0, e->dir = 0;
    if (!e->blocked) facing(e);
    return false;
  case EN_NECROMANCER: {   /* raises skeletons, and throws bolts at soldiers */
    e->sp_t++, e->rage_t++;
    if (e->sp_state != 2) {
      if (e->blocked) e->sp_t = 0;
      else if (e->sp_state == 3 || (e->sp_t >= S_enemies_necromancer_summonCoolDown && skeletons() < S_enemies_necromancer_summonMaxSkeletons)) {
        if (e->sp_state != 3) {
          e->sp_state = 3, e->sp_t2 = 0, e->sp_n2 = 0, e->thorn_tick = 0;
          play(e, AN_E_NECROMANCER_SUMMON);
          return true;
        }
        if (e->sp_t2 < 47) {
          e->sp_t2++;
          if (e->sp_t2 > 12 && e->sp_n2 <= S_enemies_necromancer_summonMaxEnemies) {
            if (e->thorn_tick < 4) e->thorn_tick++;
            else if (skeletons() < S_enemies_necromancer_summonMaxSkeletons) {
              summon_skeleton(e, e->sp_n2);
              e->sp_n2++, e->thorn_tick = 0;
            }
          }
          return true;
        }
        e->sp_state = 0, e->sp_t = 0, e->dir = 0;
        if (!e->blocked) facing(e);
        return false;
      }
    }
    /* the bolt: as the archers, on its own cooldown */
    int16_t keep = e->sp_t;
    e->sp_t = e->rage_t;
    bool r = ranged(e, S_enemies_necromancer_spellCoolDown, S_enemies_necromancer_spellMinRange, 23, 13, BU_BOLT_NECRO,
                    AN_E_NECROMANCER_SPELL);
    e->rage_t = e->sp_t, e->sp_t = keep;
    return r;
  }
  default:
    return boss_power(e);
  }
}

/* EnemyCommon.specialMove: respawning, arcane teleports, the rangers' thorns, regeneration */
static bool special_move(Enemy *e) {
  if (e->respawn_t > 0) {   /* rising (skeletons) */
    if (--e->respawn_t == 0) {
      e->active = 1, e->dir = 0;
      facing(e);
    }
    return true;
  }
  if (e->teleporting) {
    if (++e->teleport_t >= 9) {
      if (e->teleporting == 1) {
        Pt p = enemy_road(e, 0);
        e->x = p.x, e->y = p.y;
        e->teleporting = 2, e->teleport_t = 0;
        fx_add(AN_FX_TELEPORT, e->x, e->y, false, false);
        return true;
      }
      e->teleporting = 0, e->active = 1, e->fighting = 0, e->blocked = 0, e->thorned = 0, e->held = 0, e->dir = 0;
      facing(e);
    }
    return true;
  }
  if (e->thorned) {
    if (e->thorned == 1) {
      if (e->thorn_t < 18) {
        e->thorn_t++;
        return true;
      }
      e->thorned = 2, e->thorn_t = 0, e->thorn_tick = 0;
    }
    if (e->thorn_t < e->thorn_len) {
      e->thorn_t++;
      if (++e->thorn_tick == e->thorn_every) {
        enemy_damage(e, e->thorn_dmg, ARM_P, 0);
        e->thorn_tick = 0;
      }
      return true;
    }
    e->thorned = 0, e->held = 0;
    if (!e->dead) {
      e->dir = 0;
      facing(e);
    }
    return false;
  }
  int heal_pts = 0, heal_time = 0;
  switch (e->type) {
  case EN_TROLL: heal_pts = S_enemies_troll_regenerateHealPoints, heal_time = S_enemies_troll_regenerateTime; break;
  case EN_TROLLCHIEFTAIN: heal_pts = S_enemies_trollChieftain_regenerateHealPoints, heal_time = S_enemies_trollChieftain_regenerateTime; break;
  case EN_TROLLAXETHROWER: heal_pts = S_enemies_trollAxeThrower_regenerateHealPoints, heal_time = S_enemies_trollAxeThrower_regenerateTime; break;
  }
  if (heal_pts) {
    if (e->regen_t < heal_time) e->regen_t++;
    else heal(e, heal_pts), e->regen_t = 0;
  }
  return false;
}

/* ------------------------------------------------------------------ a frame */
void enemy_step(Enemy *e) {
  e->anim_t++;
  if (e->dead) {
    if (e->dead_t == 0) play(e, anim_of(e, A_DEAD));
    e->dead_t++;
    if ((e->type == EN_DEMON || e->type == EN_DEMONWOLF || e->type == EN_DEMONMAGE) && e->dead_t == 5) demon_explode(e);
    if (e->dead_t >= e->dead_time) e->alive = 0;
    return;
  }
  if (e->type == EN_VEZNAN && (e->froze == 0 || e->froze == 2)) {   /* in his tower, or changing */
    veznan_power(e);
    return;
  }
  if (special_move(e)) return;
  if (e->dead) return;
  debuffs(e);
  if (e->dead || !e->alive) return;
  if (e->rage_t > 0 && (e->type == EN_TROLL || e->type == EN_TROLLAXETHROWER || e->type == EN_TROLLCHIEFTAIN) && --e->rage_t == 0) {   /* the chieftain's rage ends */
    e->min_dmg = (int16_t)(e->min_dmg - S_enemies_trollChieftain_rageAddedMinDamage);
    e->max_dmg = (int16_t)(e->max_dmg - S_enemies_trollChieftain_rageAddedMaxDamage);
    e->speed -= S_enemies_trollChieftain_rageAddedSpeed, e->base_speed -= S_enemies_trollChieftain_rageAddedSpeed;
    e->margin = e->speed + 0.1f;
    e->armor = (int16_t)(e->armor - S_enemies_trollChieftain_rageAddedArmor);
    if (e->armor < 0) e->armor = 0;
  }
  if (e->active && !e->charging && special_power(e)) return;
  if (!e->alive || e->dead) return;
  if (e->blocked) {
    Soldier *so = SO(e->soldier);
    if (!so || !so->active || !so->fighting) enemy_unblock(e);
  }
  if (e->fighting) {
    Soldier *so = SO(e->soldier);
    if (!so || !so->active) enemy_unblock(e);
    else if (e->charging) {
      if (++e->charge_t != e->charge) return;
      e->charge_t = 0;
      e->charging = 0;
      attack(e);
      play(e, anim_of(e, A_IDLE));
      so = SO(e->soldier);
      if (!so || so->dead) enemy_unblock(e);
    } else if (++e->reload_t == e->reload) {
      play(e, anim_of(e, A_FIGHT));
      e->charging = 1;
      e->reload_t = 0;
    }
  } else if (!e->held)
    move(e);
}
