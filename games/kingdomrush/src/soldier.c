/* The soldiers (Soldier.as and its kinds: SoldierStandar*, the reinforcements, the paladins', barbarians' and the
 * sorcerer's elemental, whose specials are in special.c). */
#include <math.h>
#include <string.h>
#include "kr.h"
#include "settings.h"
#include "special.h"

typedef struct {
  int16_t charge, charge_dmg, speed10, respawn_wait, x_adj;
  int16_t an_idle, an_run, an_fight, an_dead, an_spawn;
} SoldierKind;

static SoldierKind kind_of(int k) {
  switch (k) {
  case SK_L1: return (SoldierKind){11, 5, 20, 1, 0, AN_SO_STANDAR_IDLE, AN_SO_STANDAR_RUNNING, AN_SO_STANDAR_FIGHTING, AN_SO_STANDAR_DEAD, -1};
  case SK_L2: return (SoldierKind){20, 5, 20, 1, 0, AN_SO_STANDARLEVEL2_IDLE, AN_SO_STANDARLEVEL2_RUNNING, AN_SO_STANDARLEVEL2_FIGHTING, AN_SO_STANDARLEVEL2_DEAD, -1};
  case SK_L3: return (SoldierKind){20, 5, 20, 1, 0, AN_SO_STANDARLEVEL3_IDLE, AN_SO_STANDARLEVEL3_RUNNING, AN_SO_STANDARLEVEL3_FIGHTING, AN_SO_STANDARLEVEL3_DEAD, -1};
  case SK_PALADIN: return (SoldierKind){20, 5, 20, 1, 0, AN_SO_PALADIN_IDLE, AN_SO_PALADIN_RUNNING, AN_SO_PALADIN_ATTACK1, AN_SO_PALADIN_DEAD, -1};
  case SK_BARBARIAN: return (SoldierKind){20, 5, 20, 1, 0, AN_SO_BARBARIAN_IDLE, AN_SO_BARBARIAN_RUNNING, AN_SO_BARBARIAN_ATTACK, AN_SO_BARBARIAN_DEAD, -1};
  case SK_FARMER: return (SoldierKind){11, 5, 20, 6, 5, AN_SO_FARMERJOHN_IDLE, AN_SO_FARMERJOHN_RUNNING, AN_SO_FARMERJOHN_FIGHTING, AN_SO_FARMERJOHN_DEAD, AN_SO_FARMERJOHN_RESPAWNING};
  case SK_REINF2: return (SoldierKind){11, 5, 20, 6, 5, AN_SO_REINFORCEMENTLEVEL2A_IDLE, AN_SO_REINFORCEMENTLEVEL2A_RUNNING, AN_SO_REINFORCEMENTLEVEL2A_FIGHTING, AN_SO_REINFORCEMENTLEVEL2A_DEAD, AN_SO_REINFORCEMENTLEVEL2A_RESPAWNING};
  case SK_REINF3: return (SoldierKind){11, 5, 20, 6, 5, AN_SO_REINFORCEMENTLEVEL3A_IDLE, AN_SO_REINFORCEMENTLEVEL3A_RUNNING, AN_SO_REINFORCEMENTLEVEL3A_FIGHTING, AN_SO_REINFORCEMENTLEVEL3A_DEAD, AN_SO_REINFORCEMENTLEVEL3A_RESPAWNING};
  case SK_REINF4: return (SoldierKind){11, 5, 20, 6, 5, AN_SO_REINFORCEMENTLEVEL4A_IDLE, AN_SO_REINFORCEMENTLEVEL4A_RUNNING, AN_SO_REINFORCEMENTLEVEL4A_FIGHTING, AN_SO_REINFORCEMENTLEVEL4A_DEAD, AN_SO_REINFORCEMENTLEVEL4A_RESPAWNING};
  default: return (SoldierKind){30, 13, 13, 27, 0, AN_SO_ELEMENTAL_IDLE, AN_SO_ELEMENTAL_RUNNING, AN_SO_ELEMENTAL_FIGHTING, AN_SO_ELEMENTAL_DEAD, AN_SO_ELEMENTAL_RESPAWNING};
  }
}

static void play(Soldier *so, int an) {
  if (an < 0) return;
  so->anim = (uint16_t)an, so->anim_t = 0;
}

static bool reinforcement(const Soldier *so) { return so->kind >= SK_FARMER && so->kind <= SK_REINF4; }

int soldier_new(int kind, int spot, Pt at, Pt rally) {
  int i = 0;
  while (i < MAX_SOLDIERS && B.so[i].alive) i++;
  if (i == MAX_SOLDIERS) return -1;
  Soldier *so = &B.so[i];
  memset(so, 0, sizeof *so);
  SoldierKind k = kind_of(kind);
  so->alive = 1, so->kind = (uint8_t)kind, so->spot = (int8_t)spot, so->enemy = -1;
  so->x = at.x, so->y = at.y, so->rx = rally.x, so->ry = rally.y, so->dx = rally.x, so->dy = rally.y;
  so->charge = k.charge, so->charge_dmg = k.charge_dmg, so->speed = k.speed10 / 10.0f;
  so->door_wait = k.respawn_wait, so->idle_time = 30;
  so->hidden = 1, so->respawning = 1;
  if (spot >= 0 && kind <= SK_BARBARIAN) {
    int tk = B.spot[spot].kind;
    BarrackStat b = barrack_stat(tk, B.spot[spot].level);
    so->health = so->init_health = b.health, so->armor = b.armor;
    so->min_dmg = b.min_dmg, so->max_dmg = b.max_dmg;
    so->reload = (int16_t)(kind == SK_L1 ? b.reload - so->charge : b.reload);
    so->dead_time = b.respawn, so->regen = b.regen, so->regen_reload = b.regen_reload;
    so->range_w = b.range, so->range_h = (int16_t)(b.range * RANGE_RATIO);
  } else if (reinforcement(so)) {
    int lvl = kind == SK_FARMER ? (upgrades[UP_REINF] >= 1 ? 1 : 0) : kind - SK_REINF2 + 2;
    static const int16_t hp[5] = {S_powers_farmers_health, S_powers_reinforcementLevel1_health, S_powers_reinforcementLevel2_health,
                                  S_powers_reinforcementLevel3_health, S_powers_reinforcementLevel4_health};
    static const int16_t ar[5] = {S_powers_farmers_armor, S_powers_reinforcementLevel1_armor, S_powers_reinforcementLevel2_armor,
                                  S_powers_reinforcementLevel3_armor, S_powers_reinforcementLevel4_armor};
    static const int16_t lo[5] = {S_powers_farmers_minDamage, S_powers_reinforcementLevel1_minDamage, S_powers_reinforcementLevel2_minDamage,
                                  S_powers_reinforcementLevel3_minDamage, S_powers_reinforcementLevel4_minDamage};
    static const int16_t hi[5] = {S_powers_farmers_maxDamage, S_powers_reinforcementLevel1_maxDamage, S_powers_reinforcementLevel2_maxDamage,
                                  S_powers_reinforcementLevel3_maxDamage, S_powers_reinforcementLevel4_maxDamage};
    static const int16_t rg[5] = {S_powers_farmers_regen, S_powers_reinforcementLevel1_regen, S_powers_reinforcementLevel2_regen,
                                  S_powers_reinforcementLevel3_regen, S_powers_reinforcementLevel4_regen};
    so->level = (uint8_t)lvl;
    so->health = so->init_health = hp[lvl], so->armor = ar[lvl], so->min_dmg = lo[lvl], so->max_dmg = hi[lvl];
    so->regen = rg[lvl], so->regen_reload = FPS;
    so->reload = (int16_t)(FPS - so->charge);
    so->range_w = 100, so->range_h = 70;
    so->life_time = 20 * FPS;
    /* they start dead, appear (respawning) at once, live 20 s and are gone when they fall */
    so->respawning = 0, so->dead = 1, so->lifes = 1, so->dead_time = 10, so->dead_t = 9;
    so->home_x = at.x, so->home_y = at.y;
  } else {   /* the sorcerer's elemental */
    special_elemental_stats(so, spot);
  }
  play(so, k.an_idle);
  return i;
}

void soldier_free(Soldier *so) {
  Enemy *e = EN(so->enemy);
  if (e) {
    if (so->blocking) {
      if (e->soldier == so - B.so) enemy_unblock(e);
    } else if (so->fighting)
      enemy_free_position(e, so->fpos);
  }
  so->alive = 0;
}

/* §_-02§: stops fighting */
static void stop_fight(Soldier *so) {
  Enemy *e = EN(so->enemy);
  so->blocking = 0, so->fighting = 0, so->charging = 0, so->charge_t = 0;
  if (e) enemy_free_position(e, so->fpos);
  so->enemy = -1;
}

/* §_-LT§: back to the rally point */
static void go_home(Soldier *so) {
  if (!so->walking) {
    so->idle = 0, so->walking = 1;
    play(so, kind_of(so->kind).an_run);
  }
  so->enemy = -1, so->fighting = 0, so->blocking = 0, so->charging = 0;
  so->dx = so->rx, so->dy = so->ry;
}

void soldier_set_rally(Soldier *so, Pt p) {   /* changeRallyPoint */
  so->rx = p.x, so->ry = p.y;
  if (so->dead || so->respawning) return;
  so->active = 0;
  stop_fight(so);
  go_home(so);
  special_soldier_stop(so);
}

void soldier_damage(Soldier *so, int dmg, bool true_dmg) {
  if (!so->active && !so->walking) return;
  so->targeted_t = 0;
  int loss = true_dmg ? dmg : (int)((float)dmg - (float)so->armor * dmg / 100.0f);
  /* (the shield of the paladins and the elemental's armour are in its armour) */
  so->health = (int16_t)(so->health - loss);
  if (so->health <= 0) {
    so->health = 0;
    so->active = 0, so->dead = 1, so->charging = 0, so->charge_t = 0;
    play(so, kind_of(so->kind).an_dead);
    if (so->blocking) stop_fight(so);
    else if (so->fighting) stop_fight(so);
    so->dead_t = 0;
    return;
  }
  /* Barbed Armor: the attacker takes a tenth back */
  if (up_on(UP_BARRACKS, 5) && so->spot >= 0 && so->kind <= SK_BARBARIAN) {
    Enemy *e = EN(so->enemy);
    if (e && e->active) {
      enemy_damage(e, (int)ceilf(dmg * 0.1f), ARM_I, 0);
      if (!e->active) stop_fight(so);
    }
  }
}

/* ------------------------------------------------------------------ fights */
static bool in_range(const Soldier *so, const Enemy *e) {
  float cx, cy;
  if (reinforcement(so)) cx = so->home_x, cy = so->home_y;
  else if (so->spot >= 0 && so->kind == SK_ELEMENTAL) cx = so->rx, cy = so->ry;
  else if (so->spot >= 0) cx = B.spot[so->spot].rally.x, cy = B.spot[so->spot].rally.y + 10;
  else cx = so->rx, cy = so->ry;
  return ellipse_contains(cx, cy, so->range_w, so->range_h, e->x, e->y);
}

static void fight_position(Soldier *so) {   /* setFightingPosition */
  Enemy *e = EN(so->enemy);
  if (!e) return;
  float dy = 0, dx = 0;
  if (so->fpos == 0) dy = 6, dx = 3;
  else if (so->fpos == 1) dy = -6, dx = 3;
  int xadj = kind_of(so->kind).x_adj, xs = enemy_stats[e->type].x_soldier;
  so->dy = roundf(e->y) + dy;
  if (!e->left) so->dx = roundf(e->x) + xadj + xs - dx;
  else so->dx = roundf(e->x) - xadj - xs + dx;
  play(so, kind_of(so->kind).an_run);
}

static void engage(Soldier *so, int i, bool block) {
  Enemy *e = &B.en[i];
  if (block) {   /* §_-Df§: it stops the enemy */
    if (so->fighting) {
      Enemy *o = EN(so->enemy);
      if (o) enemy_free_position(o, so->fpos);
    }
    so->fighting = 1, so->blocking = 1, so->walking = 1, so->idle = 0;
    so->enemy = (int8_t)i;
    enemy_block(e, (int)(so - B.so));
    so->fpos = 2;
  } else {   /* §_-Fc§: it joins a fight *
              */
    so->walking = 1, so->blocking = 0, so->fighting = 1, so->idle = 0;
    so->enemy = (int8_t)i;
    so->fpos = (uint8_t)enemy_fighter_position(e);
  }
  so->reload_t = (int16_t)(so->reload - 1);
  fight_position(so);
}

/* findEnemy: an unblocked enemy first (the nearest the start), else one already held */
static bool find_enemy(Soldier *so) {
  if (so->blocking) return false;
  int free_e = -1, held_e = -1;
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active || !enemy_stats[e->type].blockable || e->flying || !in_range(so, e)) continue;
    if (!e->blocked) {
      if (free_e < 0 || B.en[free_e].node > e->node) free_e = i;
    } else if (!so->fighting && (held_e < 0 || B.en[held_e].node > e->node))
      held_e = i;
  }
  if (free_e >= 0) {
    engage(so, free_e, true);
    return true;
  }
  if (held_e >= 0) {
    engage(so, held_e, false);
    return true;
  }
  return false;
}

static void face_enemy(Soldier *so) {   /* §_-7g§ */
  Enemy *e = EN(so->enemy);
  if (!e) return;
  if (!e->left) so->left = 1;
  else so->left = 0, e->left = 1;
}

static void hit(Soldier *so) {
  Enemy *e = EN(so->enemy);
  if (!e || !e->active) {
    stop_fight(so);
    go_home(so);
    return;
  }
  if (!enemy_dodge(e)) {
    enemy_damage(e, special_soldier_damage(so, rnd_dmg(so->min_dmg, so->max_dmg)), ARM_P, 0);
    if (!e->active) stop_fight(so);
  }
}

/* §_-Ha§: walking to where it goes; false while on the way */
static bool walk(Soldier *so) {
  if (so->active) {
    if (so->fighting) {
      Enemy *e = EN(so->enemy);
      if (!e || !e->active || !e->blocked) {
        stop_fight(so);
        if (!find_enemy(so)) go_home(so);
      } else if (!so->blocking)
        find_enemy(so);
    } else
      find_enemy(so);
  }
  float dx = so->dx - so->x, dy = so->dy - so->y;
  if (sqrtf(dx * dx + dy * dy) <= so->speed) {   /* §_-Eo§: arrived */
    so->walking = 0, so->active = 1;
    so->x = roundf(so->dx), so->y = roundf(so->dy);
    if (so->fighting) {
      face_enemy(so);
      Enemy *e = EN(so->enemy);
      if (so->blocking && e && e->active) enemy_start_fighting(e);
    } else
      so->idle = 1;
    play(so, kind_of(so->kind).an_idle);
    return true;
  }
  float a = atan2f(dy, dx);
  so->left = so->dx < so->x;
  so->x += cosf(a) * so->speed, so->y += sinf(a) * so->speed;
  return false;
}

static void respawn(Soldier *so) {
  so->active = 1, so->dead = 0, so->respawning = 0, so->walking = 1;
  so->charging = 0, so->blocking = 0, so->fighting = 0, so->idle = 0, so->hidden = 0;
  if (so->spot >= 0 && !reinforcement(so) && so->kind != SK_ELEMENTAL) {
    so->x = B.spot[so->spot].x, so->y = B.spot[so->spot].y - 1;
  }
  so->dx = so->rx, so->dy = so->ry;
  so->health = so->init_health;
  so->dead_t = 0, so->door_t = 0;
  play(so, kind_of(so->kind).an_run);
}

static void idle(Soldier *so) {   /* playIdle: turns now and then; heals after 2 s untouched */
  so->targeted_t++;
  if (++so->idle_t >= so->idle_time) {
    if (roundf(rnd() * 10) < 4) so->left = !so->left;
    so->idle_t = 0;
  }
  if (so->targeted_t < 60) return;
  so->targeted_t = 60;
  if (++so->regen_t >= so->regen_reload) {
    if (so->health < so->init_health) {
      so->health = (int16_t)(so->health + so->regen);
      if (so->health > so->init_health) so->health = so->init_health;
    }
    so->regen_t = 0;
  }
}

/* ------------------------------------------------------------------ a frame */
void soldier_step(Soldier *so) {
  so->anim_t++;
  if (so->dead) {   /* readyToRespawn */
    if (++so->dead_t < so->dead_time) return;
    if (reinforcement(so)) {
      if (so->lifes != 1) {
        so->alive = 0;
        return;
      }
      so->lifes = 2, so->dead = 0, so->respawning = 1, so->door_t = 0, so->hidden = 0;
      play(so, kind_of(so->kind).an_spawn);
      return;
    }
    if (so->kind == SK_ELEMENTAL) {
      so->dead = 0, so->respawning = 1, so->door_t = 0, so->hidden = 0;
      so->x = so->rx, so->y = so->ry;
      play(so, kind_of(so->kind).an_spawn);
      return;
    }
    so->dead = 0, so->door_queued = 0, so->respawning = 1, so->hidden = 1;
    return;
  }
  if (so->respawning) {   /* readyToLive */
    if (reinforcement(so) || so->kind == SK_ELEMENTAL) {
      if (++so->door_t >= so->door_wait) respawn(so);
      return;
    }
    if (so->spot < 0 || B.spot[so->spot].disabled) return;
    so->hidden = 1;
    if (!so->door_queued) {
      int s = so->spot;
      if (barracks_door_closed(s)) {
        barracks_open(s);
        so->door_wait = 12, so->door_queued = 1;
      } else if (barracks_door_opening(s)) {
        so->door_wait = (int16_t)(12 - barracks_door_t(s)), so->door_queued = 1;
      } else
        return;
    }
    if (++so->door_t >= so->door_wait) respawn(so);
    return;
  }
  if (reinforcement(so) && ++so->life_t >= so->life_time) {   /* readyToHide */
    so->active = 0, so->dead = 1, so->dead_t = 0, so->lifes = 2;
    play(so, kind_of(so->kind).an_dead);
    if (so->blocking || so->fighting) stop_fight(so);
    return;
  }
  if (special_soldier(so)) return;
  if (so->active && !so->fighting && !so->blocking && so->charge_t != 0) {   /* a blow finishing */
    if (so->charge_t < so->charge) {
      so->charge_t++;
      return;
    }
    so->charge_t = 0;
    go_home(so);
  }
  if (so->walking && !walk(so)) return;
  if (!so->active) return;
  if (so->idle) {
    if (!find_enemy(so)) idle(so);
  } else if (so->fighting) {
    Enemy *e = EN(so->enemy);
    if (!e || !e->active) stop_fight(so);
    e = EN(so->enemy);
    if (so->fighting && !so->blocking && (!e || !e->blocked)) stop_fight(so);
    if (!so->blocking && find_enemy(so)) return;
    if (!so->fighting) {
      go_home(so);
      return;
    }
    if (so->charging) {   /* §_-Jy§ */
      if (++so->charge_t < so->charge) {
        if (so->charge_t == so->charge_dmg) hit(so);
        return;
      }
      so->charge_t = 0, so->charging = 0;
    } else if (++so->reload_t == so->reload) {
      play(so, kind_of(so->kind).an_fight);
      special_soldier_attack_anim(so);
      so->charging = 1, so->reload_t = 0;
    }
  } else
    go_home(so);
}
