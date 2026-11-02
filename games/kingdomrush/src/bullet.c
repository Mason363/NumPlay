/* What flies: arrows (Arrow.as), bolts (Bolt.as), bombs (Bomb.as), the enemies' arrows, axes and bolts, the
 * missiles, the fireballs (Fireball.as); and what stays a while (fx: blasts, blood, smoke, gold). */
#include <math.h>
#include <string.h>
#include "kr.h"
#include "settings.h"
#include "special.h"
#include "gfx.h"

Bullet *bullet_new(int kind) {
  for (int i = 0; i < MAX_BULLETS; i++)
    if (!B.bu[i].alive) {
      Bullet *b = &B.bu[i];
      memset(b, 0, sizeof *b);
      b->alive = 1, b->kind = (uint8_t)kind, b->active = 1, b->target = -1;
      return b;
    }
  return 0;
}

void fx_add(int anim, float x, float y, bool flip, bool decal) {
  if (anim < 0) return;
  int life = anim_ticks(anim);
  if (decal) life = 120;   /* blood and craters stay 4 s */
  for (int i = 0; i < MAX_FX; i++)
    if (!B.fx[i].alive) {
      B.fx[i] = (Fx){1, flip, decal, (uint16_t)anim, 0, (uint16_t)life, x, y, 0};
      return;
    }
}

void fx_value(float x, float y, int value) {
  for (int i = 0; i < MAX_FX; i++)
    if (!B.fx[i].alive) {
      B.fx[i] = (Fx){1, 0, 0, 0xFFFF, 0, 40, x, y, (int16_t)value};
      return;
    }
}

void fx_step(void) {
  for (int i = 0; i < MAX_FX; i++)
    if (B.fx[i].alive && ++B.fx[i].t >= B.fx[i].life) B.fx[i].alive = 0;
}

/* hitTestObject: the arrow's box and the enemy's (its picture and life bar) */
static bool hits(const Bullet *b, const Enemy *e) {
  const EnemyStat *s = &enemy_stats[e->type];
  return b->x + 6 > e->x - s->hit_w && b->x - 6 < e->x + s->hit_w && b->y + 4 > e->y - s->hit_h && b->y - 4 < e->y + 4;
}

static bool soldier_hit(const Bullet *b, const Soldier *so) {
  return b->x + 6 > so->x - 10 && b->x - 6 < so->x + 10 && b->y + 4 > so->y - 24 && b->y - 4 < so->y + 4;
}

/* Arrow.moveArrow: a thrown arc of t1 frames */
static void arc(Bullet *b) {
  float t = b->t0;
  b->x = b->x0 + t * b->xs;
  b->y = b->y0 + t * b->ys + b->g * t * t / 2;
  float nx = b->x0 + (t + 1) * b->xs, ny = b->y0 + (t + 1) * b->ys + b->g * (t + 1) * (t + 1) / 2;
  b->rot = atan2f(ny - b->y, nx - b->x);
}

static void fade(Bullet *b) {
  if (b->fade_t == b->fade) b->alive = 0;
  b->fade_t++;
}

/* Bomb.getDamage: falls from the most at the middle to the least at the edge (the ellipse's point that way) */
static int bomb_damage(const Bullet *b, const Enemy *e) {
  if (up_on(UP_ENGINEERS, 5)) return b->max_dmg;   /* Efficiency */
  float w = b->area * 1.5f, h = (float)b->area;
  float deg = atan2f(e->x - b->tx, e->y - b->ty) * 180.0f / (float)M_PI;
  float rad = (deg - 90.0f) * (float)M_PI / 180.0f;
  float px = b->tx + cosf(rad) * w / 2, py = b->ty + sinf(rad) * h / 2;
  float edge = sqrtf((px - b->tx) * (px - b->tx) + (py - b->ty) * (py - b->ty));
  float d = sqrtf((e->x - b->tx) * (e->x - b->tx) + (e->y - b->y) * (e->y - b->y));
  float span = (float)(b->max_dmg - b->min_dmg);
  if (edge <= 0) return b->max_dmg;
  return (int)(span - d * span / edge + b->min_dmg);
}

void bomb_blast(Bullet *b) {
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active) continue;
    if (!ellipse_contains(b->tx, b->ty, b->area * 1.5f, (float)b->area, e->x, e->y)) continue;
    /* bombHit: a kill blows it up */
    int dmg = bomb_damage(b, e);
    int left = e->health - (int)((float)dmg - (float)e->armor / 2.0f * dmg / 100.0f);
    if (left <= 0 && e->sp_state != 9) enemy_kill(e, 1);
    else enemy_damage(e, dmg, ARM_E, 0);
  }
  fx_add(AN_B_BOMBDECAL, b->x, b->y, false, true);
}

void bullet_step(Bullet *b) {
  b->anim_t++;
  if (!b->active) {
    fade(b);
    return;
  }
  switch (b->kind) {
  case BU_ARROW: {
    arc(b);
    Enemy *e = EN(b->target);
    if (e && e->active && b->t0 + 2 >= b->t1 && hits(b, e)) {
      b->x = e->x + enemy_stats[e->type].x_adj, b->y = e->y + enemy_stats[e->type].y_adj;
      const TowerStat st = tower_stat(b->level == 4 ? TW_RANGER : TW_ARCHER, b->level == 4 ? 4 : b->level);
      int dmg = rnd_dmg(b->min_dmg ? b->min_dmg : st.min_dmg, b->max_dmg ? b->max_dmg : st.max_dmg);
      if (b->crit) dmg *= 2;
      enemy_damage(e, dmg, ARM_P, b->pierce);
      if (b->bombs && !e->dead) special_poison(e, b->bombs);   /* the ranger's poison */
      b->alive = 0;
      return;
    }
    if (b->t0 == b->t1) {
      fx_add(AN_B_ARROWDECAL, b->x, b->y, b->xs < 0, true);
      b->alive = 0;
      return;
    }
    b->t0++;
    return;
  }
  case BU_ARROW_ENEMY: case BU_AXE_ENEMY: {
    arc(b);
    Soldier *so = SO(b->target);
    if (so && so->active && b->t0 + 2 >= b->t1 && soldier_hit(b, so)) {
      soldier_damage(so, rnd_dmg(b->min_dmg, b->max_dmg), false);
      b->active = 0, b->fade = 11;
      return;
    }
    if (b->t0 == b->t1) {
      fx_add(AN_B_ARROWDECAL, b->x, b->y, b->xs < 0, true);
      b->alive = 0;
      return;
    }
    b->t0++;
    return;
  }
  case BU_BOLT: case BU_BOLT_SORCERER: case BU_BOLT_NECRO: case BU_MISSILE_ENEMY: {
    /* Bolt.moveMe: homes in, speeding up */
    bool at_soldier = b->kind == BU_BOLT_NECRO || b->kind == BU_MISSILE_ENEMY;
    float tx = b->tx, ty = b->ty;
    if (!at_soldier) {
      Enemy *e = EN(b->target);
      if (e && !e->active) b->target = -1, e = 0;
      if (e) tx = b->tx = e->x + enemy_stats[e->type].x_adj, ty = b->ty = e->y + enemy_stats[e->type].y_adj;
    } else {
      Soldier *so = SO(b->target);
      if (so && !so->active) b->target = -1, so = 0;
      if (so) tx = b->tx = so->x, ty = b->ty = so->y - 7;
    }
    float a = atan2f(ty - b->y, tx - b->x);
    if (b->acc < b->acc_max) {
      b->acc += ceilf(b->acc * 0.05f);
      if (b->acc >= b->acc_max) b->acc = b->acc_max;
    }
    b->xs = cosf(a) * b->acc, b->ys = sinf(a) * b->acc;
    b->rot = a;
    if (b->first && ty <= b->y) b->y -= 11;
    b->first = 0;
    b->x += b->xs, b->y += b->ys;
    float dx = tx - b->x, dy = ty - b->y;
    if (sqrtf(dx * dx + dy * dy) < b->acc) {
      if (at_soldier) {
        Soldier *so = SO(b->target);
        if (b->kind == BU_MISSILE_ENEMY) {   /* the Juggernaut's missile: a blast on the soldiers there */
          for (int i = 0; i < MAX_SOLDIERS; i++) {
            Soldier *o = &B.so[i];
            if (o->alive && o->active && ellipse_contains(tx, ty, 90, 63, o->x, o->y)) soldier_damage(o, rnd_dmg(b->min_dmg, b->max_dmg), false);
          }
          fx_add(AN_FX_EXPLODEBIG, tx, ty, false, false);
        } else if (so)
          soldier_damage(so, rnd_dmg(b->min_dmg, b->max_dmg), false);
      } else {
        Enemy *e = EN(b->target);
        if (e) special_bolt_hit(b, e);
      }
      b->x = tx, b->y = ty, b->acc = 0;
      b->active = 0, b->fade = 10, b->anim_t = 0;
    }
    return;
  }
  case BU_BOMB:
    b->x = b->x0 + b->t0 * b->xs;
    b->y = b->y0 + b->t0 * b->ys + b->g * b->t0 * b->t0 / 2;
    b->rot += 20;
    if (b->t0 >= b->t1) {
      b->x = b->tx, b->y = b->ty;
      special_bomb_hit(b);
      b->active = 0, b->fade = 21, b->anim_t = 0;
      return;
    }
    b->t0++;
    return;
  case BU_FIREBALL: {
    /* Fireball.get: falls on its spot, speeding up; burns everything in 100 x 70 there */
    float a = atan2f(b->ty - b->y, b->tx - b->x);
    if (b->acc < b->acc_max) {
      b->acc += ceilf(b->acc * 0.05f);
      if (b->acc >= b->acc_max) b->acc = b->acc_max;
    }
    b->xs = cosf(a) * b->acc, b->ys = sinf(a) * b->acc;
    b->x += b->xs, b->y += b->ys;
    float dx = b->tx - b->x, dy = b->ty - b->y;
    if (sqrtf(dx * dx + dy * dy) < b->acc) {
      int r = b->area;
      for (int i = 0; i < MAX_ENEMIES; i++) {
        Enemy *e = &B.en[i];
        if (e->alive && e->active &&
            ellipse_contains(b->tx, b->ty, (float)r, r * RANGE_RATIO, e->x + enemy_stats[e->type].x_adj, e->y + enemy_stats[e->type].y_adj))
          enemy_damage(e, b->min_dmg, ARM_I, 0);
      }
      fx_add(AN_B_BOMBDECAL, b->tx, b->ty, false, true);
      if (b->pierce) special_scorched_earth(b->tx, b->ty);
      b->active = 0, b->fade = 30, b->anim_t = 0;
    }
    return;
  }
  default:
    special_bullet(b);
  }
}
