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

bool veznan_power(Enemy *e) { (void)e; return false; }

bool boss_power(Enemy *e) {
  switch (e->type) {
  case EN_JUGGERNAUT: return juggernaut(e);
  case EN_YETIBOSS: return yeti_boss(e);
  case EN_VEZNAN: return veznan_power(e);
  default: return false;
  }
}
