/* The enemies' helpers: eggs, skeletons, demons' last blast, the big ones' blows, the bosses. */
#include <math.h>
#include <string.h>
#include "anim.h"
#include "kr.h"
#include "settings.h"

int enemy_health_pct = 100;

bool enemy_area_attack(const Enemy *e) {
  return e->type == EN_YETI || e->type == EN_LAVAELEMENTAL || e->type == EN_JUGGERNAUT || e->type == EN_YETIBOSS ||
         (e->type == EN_VEZNAN && e->froze);
}

/* ------------------------------------------------------------------ spider eggs */
void egg_lay(Enemy *e) {
  for (int i = 0; i < (int)(sizeof B.egg / sizeof B.egg[0]); i++)
    if (!B.egg[i].on) {
      B.egg[i].on = 1, B.egg[i].path = e->path, B.egg[i].node = e->node, B.egg[i].t = 0, B.egg[i].n = 0;
      B.egg[i].x = e->x, B.egg[i].y = e->y;
      return;
    }
}

void eggs_step(void) {
  for (int i = 0; i < (int)(sizeof B.egg / sizeof B.egg[0]); i++) {
    if (!B.egg[i].on) continue;
    B.egg[i].t++;
    /* 85 frames, then a spiderling every 6, then the shell stays a while; the Juggernaut's: golem heads */
    bool golem = B.egg[i].on == 2;
    int wait = golem ? 20 : 85, count = golem ? S_enemies_juggernaut_golemHeads : S_enemies_spider_eggsSpiders;
    if (B.egg[i].t > wait && B.egg[i].n < count && (B.egg[i].t - wait) % 6 == 0) {
      int lane = B.egg[i].n % B.nlanes[B.egg[i].path];
      int len = path_len(B.egg[i].path, lane), node = B.egg[i].node + 2;
      if (node > len - 12) node = len - 12;
      int j = enemy_spawn(golem ? EN_GOLEMHEAD : EN_SPIDERTINY, B.egg[i].path, lane, node);
      if (j >= 0) {
        B.en[j].x = B.egg[i].x, B.en[j].y = B.egg[i].y;
        B.en[j].active = 1;
      }
      B.egg[i].n++;
    }
    if (B.egg[i].t > 85 + 120) B.egg[i].on = 0;
  }
}

/* ------------------------------------------------------------------ skeletons */
int skeletons(void) { return B.skeletons; }

void summon_skeleton(Enemy *e, int k) {
  static const int8_t lanes[8] = {1, 2, 0, 1, 2, 0, 1, 2};
  int node = e->node;
  if (k >= 2 && k <= 4) node += rnd_round(3, 7);
  if (k >= 5) node -= rnd_round(3, 7);
  int lane = lanes[k & 7] % B.nlanes[e->path];
  int len = path_len(e->path, lane);
  if (node < 11) node = 11;
  if (node > len - 12) node = len - 12;
  int j = enemy_spawn(EN_SKELETON, e->path, lane, node);
  if (j < 0) return;
  Enemy *s = &B.en[j];
  s->respawn_t = 33, s->active = 0;
  s->anim = AN_E_SKELETON_RESPAWNING, s->anim_t = 0;
  B.skeletons++;
}

/* ------------------------------------------------------------------ demons: their last blast */
void demon_explode(Enemy *e) {
  int lo, hi, r;
  if (e->type == EN_DEMONWOLF) lo = S_enemies_demonWolf_exploteMinDamage, hi = S_enemies_demonWolf_exploteMaxDamage, r = S_enemies_demonWolf_exploteRange;
  else if (e->type == EN_DEMONMAGE) lo = S_enemies_demonMage_exploteMinDamage, hi = S_enemies_demonMage_exploteMaxDamage, r = S_enemies_demonMage_exploteRange;
  else lo = S_enemies_demon_exploteMinDamage, hi = S_enemies_demon_exploteMaxDamage, r = S_enemies_demon_exploteRange;
  fx_add(AN_B_DEMONFIRE, e->x, e->y, false, false);
  for (int i = 0; i < MAX_SOLDIERS; i++) {
    Soldier *so = &B.so[i];
    if (so->alive && so->active && ellipse_contains(e->x, e->y, (float)r, r * RANGE_RATIO, so->x, so->y))
      soldier_damage(so, rnd_dmg(lo, hi), false);
  }
}

/* ------------------------------------------------------------------ the bosses */
static void boss_anim(Enemy *e, int an) { e->anim = (uint16_t)an, e->anim_t = 0; }

/* the Juggernaut: golem heads in an egg every 4 s, a missile at the soldiers every 9 s */
static bool juggernaut(Enemy *e) {
  e->sp_t++, e->sp_t2++;
  if (e->sp_state == 1) {   /* launching */
    if (e->sp_n < 49) {
      if (++e->sp_n == 24) {
        /* the egg lands a little down the road and hatches golem heads */
        Pt p = enemy_road(e, 40 + rnd_round(0, 30));
        for (int k = 0; k < (int)(sizeof B.egg / sizeof B.egg[0]); k++)
          if (!B.egg[k].on) {
            B.egg[k].on = 2, B.egg[k].path = e->path, B.egg[k].node = (int16_t)(e->node + 40), B.egg[k].t = 0, B.egg[k].n = 0;
            B.egg[k].x = p.x, B.egg[k].y = p.y;
            break;
          }
      }
      return true;
    }
    e->sp_state = 0, e->sp_t = 0, e->dir = 0;
    return false;
  }
  if (e->sp_t >= 120 && !e->blocked) {
    e->sp_state = 1, e->sp_n = 0;
    boss_anim(e, AN_E_JUGGERNAUT_MISSILE);
    return true;
  }
  if (e->sp_t2 >= S_enemies_juggernaut_missileCoolDown) {
    /* a missile at a soldier out of reach */
    for (int i = 0; i < MAX_SOLDIERS; i++) {
      Soldier *so = &B.so[i];
      if (!so->alive || !so->active) continue;
      float dx = so->x - e->x, dy = so->y - e->y;
      if (sqrtf(dx * dx + dy * dy) < S_enemies_juggernaut_missileMinRange) continue;
      Bullet *b = bullet_new(BU_MISSILE_ENEMY);
      if (b) {
        b->x = b->x0 = e->x, b->y = b->y0 = e->y - 60, b->target = (int8_t)i, b->tx = so->x, b->ty = so->y;
        b->acc = 2, b->acc_max = 12;
        b->min_dmg = S_enemies_juggernaut_missileMinDamage, b->max_dmg = S_enemies_juggernaut_missileMaxDamage;
      }
      e->sp_t2 = 0;
      break;
    }
  }
  return false;
}

/* J.T., the yeti king: freezes up to 4 towers around it every 10 s, calls winter wolves */
static bool yeti_boss(Enemy *e) {
  e->sp_t++, e->sp_t2++;
  if (e->sp_state == 1) {   /* the breath */
    if (e->sp_n < 30) {
      if (++e->sp_n == 15) {
        int n = 0;
        for (int s = 0; s < B.nspots && n < S_enemies_yetiBoss_frozeMaxTowers; s++) {
          Spot *t = &B.spot[s];
          if (t->kind == TW_NONE || t->kind == 0xFF || t->disabled || t->building) continue;
          if (!ellipse_contains(e->x, e->y, S_enemies_yetiBoss_frozeRangeWidth, S_enemies_yetiBoss_frozeRangeWidth * RANGE_RATIO, t->x, t->y)) continue;
          t->disabled = 1, t->ice = 1, t->ice_t = 0, t->ice_hits = 0;   /* IceBlock: three hits break it */
          n++;
        }
      }
      return true;
    }
    e->sp_state = 0, e->sp_t = 0, e->dir = 0;
    return false;
  }
  if (e->sp_t >= S_enemies_yetiBoss_frozeReloadTime && !e->blocked) {
    e->sp_state = 1, e->sp_n = 0;
    boss_anim(e, AN_E_YETIBOSS_BREATH);
    return true;
  }
  if (e->sp_t2 >= S_enemies_yetiBoss_wolfSpawnCooldownTime) {
    for (int k = 0; k < S_enemies_yetiBoss_maxWolf; k++) {
      int j = enemy_spawn(EN_WHITEWOLF, e->path, k % B.nlanes[e->path], 0);
      (void)j;
    }
    e->sp_t2 = 0;
  }
  return false;
}

/* ------------------------------------------------------------------ Vez'nan (EnemyVeznan, Level12) */
/* his tower's spells by wave: [hold after s, towers held], [portals after s, 1, 2, 3 open, times] */
static const uint8_t hold_timers[15][2] = {{0, 3}, {0, 3}, {0, 3}, {0, 3}, {0, 2}, {0, 2}, {0, 2}, {0, 2},
                                          {13, 2}, {13, 3}, {14, 4}, {14, 5}, {16, 6}, {16, 7}, {18, 8}};
static const uint8_t portal_timers[15][5] = {{0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0}, {0, 0, 0, 0, 0},
                                             {0, 0, 0, 0, 0}, {15, 1, 0, 0, 3}, {10, 1, 0, 0, 2}, {20, 0, 1, 0, 3},
                                             {15, 1, 0, 0, 3}, {20, 1, 1, 0, 3}, {15, 1, 1, 0, 3}, {15, 1, 1, 0, 3},
                                             {15, 0, 0, 1, 3}, {15, 1, 1, 1, 3}, {15, 1, 1, 1, 3}};
static const int16_t portal_at[3][3] = {{362, 243, 103}, {362, 365, 148}, {362, 493, 228}};

void veznan_spawn(void) {
  int i = enemy_spawn(EN_VEZNAN, 0, 1, 0);
  if (i < 0) return;
  Enemy *e = &B.en[i];
  e->x = 362, e->y = 70, e->active = 0, e->froze = 0, e->sp_n2 = -1;
  e->anim = AN_E_VEZNAN_IDLEDOWN, e->anim_t = 0;
  B.veznan = (int8_t)i;
}

/* the portals' demons: 50% 4 to 6 demon spawns, 30% 3 demon hounds, 20% 5 spawns and a demon lord */
static void portals_open(const uint8_t *pt) {
  for (int k = 0; k < 3; k++) {
    if (!pt[1 + k]) continue;
    for (int s = 0; s < (int)(sizeof B.vportal / sizeof B.vportal[0]); s++)
      if (!B.vportal[s].on) {
        float r = rnd();
        B.vportal[s].on = 1, B.vportal[s].t = 0, B.vportal[s].k = (uint8_t)k, B.vportal[s].n = 0;
        B.vportal[s].group = r <= 0.5f ? 1 : r <= 0.8f ? 2 : 3;
        B.vportal[s].max = (uint8_t)(r <= 0.5f ? rnd_round(4, 6) : r <= 0.8f ? 3 : 6);
        break;
      }
  }
}

void vportals_step(void) {
  for (int s = 0; s < (int)(sizeof B.vportal / sizeof B.vportal[0]); s++) {
    if (!B.vportal[s].on) continue;
    if (++B.vportal[s].t % 30 == 0) {
      int g = B.vportal[s].group, k = B.vportal[s].k;
      int type = g == 2 ? EN_DEMONWOLF : g == 3 && B.vportal[s].n >= 5 ? EN_DEMONMAGE : EN_DEMON;
      int lane = B.vportal[s].n % B.nlanes[0], node = portal_at[k][2];
      if (node > path_len(0, lane) - 12) node = path_len(0, lane) - 12;
      int j = enemy_spawn(type, 0, lane, node);
      if (j >= 0) {
        B.en[j].x = portal_at[k][0], B.en[j].y = portal_at[k][1], B.en[j].active = 1;
        B.en[j].teleporting = 2, B.en[j].teleport_t = 0;
      }
      if (++B.vportal[s].n >= B.vportal[s].max) B.vportal[s].on = 0;
    }
  }
}

static void hold_towers(int n) {
  int cand[MAX_SPOTS], nc = 0;
  for (int s = 0; s < B.nspots; s++) {
    Spot *t = &B.spot[s];
    if (t->kind != TW_NONE && t->kind != 0xFF && !t->building && !t->disabled) cand[nc++] = s;
  }
  for (int k = 0; k < n && nc > 0; k++) {
    int pick = (int)(rnd() * nc);
    if (pick >= nc) pick = nc - 1;
    Spot *t = &B.spot[cand[pick]];
    t->disabled = 1, t->ice = 4, t->ice_t = 0;   /* VeznanBlock */
    cand[pick] = cand[--nc];
  }
}

/* he leaves his tower when every other enemy is gone */
void veznan_descend(void) {
  Enemy *e = EN(B.veznan);
  if (!e || e->froze) return;
  e->froze = 1, e->active = 1, e->node = 0, e->dir = 0;
  Pt p = path_node(0, e->lane, 0);
  e->x = p.x, e->y = p.y;
  e->sp_t2 = 0, e->sp_n = 40, e->sp_state = 0;   /* (portals every 10 s from now) */
}

bool veznan_power(Enemy *e) {
  int w = B.current_wave - 1;
  if (w < 0) w = 0;
  if (w > 14) w = 14;
  if (e->froze == 0) {   /* in his tower: spells each wave */
    if (e->sp_n2 != B.current_wave) {   /* (updateTimers at a wave's start) */
      e->sp_n2 = B.current_wave, e->sp_t = 0, e->sp_t2 = 0, e->sp_n = portal_timers[w][4];
    }
    e->sp_t++, e->sp_t2++;
    if (hold_timers[w][0] && e->sp_t == hold_timers[w][0] * FPS) {
      e->anim = AN_E_VEZNAN_SPELLDOWN, e->anim_t = 0;
      hold_towers(hold_timers[w][1]);
    }
    if (portal_timers[w][0] && e->sp_n > 0 && e->sp_t2 >= portal_timers[w][0] * FPS) {
      e->anim = AN_E_VEZNAN_SPELL, e->anim_t = 0;
      portals_open(portal_timers[w]);
      e->sp_t2 = 0, e->sp_n--;
    }
    if (e->anim_t > 45) e->anim = AN_E_VEZNAN_IDLEDOWN, e->anim_t = 0;
    return true;
  }
  if (e->froze == 2) {   /* becoming the demon */
    if (++e->sp_c >= 34) {
      e->froze = 3, e->dir = 0;
      e->health = e->init_health = S_enemies_veznan_demonHealth * enemy_health_pct / 100;
      e->speed = e->base_speed = S_enemies_veznan_demonSpeed, e->margin = e->speed + 0.1f;
      e->charge = 53, e->reload = (int16_t)(S_enemies_veznan_demonAttackReloadTime - 53);
      e->active = 1;
      e->anim = AN_E_VEZNAN_WALKINGDEMON, e->anim_t = 0;
    }
    return true;
  }
  if (e->froze == 1) {
    /* walking down: portals every 10 s, and his soul steal (tsung) every 5 s on the soldiers around */
    if (++e->sp_t2 >= 10 * FPS && e->sp_n > 0) {
      static const uint8_t all[5] = {10, 1, 1, 1, 40};
      portals_open(all);
      e->sp_t2 = 0, e->sp_n--;
    }
    if (++e->rage_t >= S_enemies_veznan_tsungReloadTime) {
      int n = 0;
      for (int i = 0; i < MAX_SOLDIERS && n < S_enemies_veznan_tsungMaxEnemies; i++) {
        Soldier *so = &B.so[i];
        if (!so->alive || !so->active || so->kind == SK_ELEMENTAL) continue;
        if (!ellipse_contains(e->x, e->y, S_enemies_veznan_tsungRangeWidth, S_enemies_veznan_tsungRangeWidth * RANGE_RATIO, so->x, so->y)) continue;
        float dx = so->x - e->x, dy = so->y - e->y;
        if (sqrtf(dx * dx + dy * dy) <= S_enemies_veznan_tsungMinRange) continue;
        soldier_damage(so, 30000, true);
        n++;
      }
      if (n) {
        e->anim = AN_E_VEZNAN_RANGEDATTACK, e->anim_t = 0;
        e->rage_t = 0;
      }
    }
  }
  return false;
}

bool boss_power(Enemy *e) {
  switch (e->type) {
  case EN_JUGGERNAUT: return juggernaut(e);
  case EN_YETIBOSS: return yeti_boss(e);
  case EN_VEZNAN: return veznan_power(e);
  default: return false;
  }
}
