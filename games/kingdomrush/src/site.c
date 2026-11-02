/* The stages' buildings:
 * - Silveroak Forest: the elves' ruined hall (TowerSoldierElfDestroyed), repaired into a hall (TowerSoldierElf) that
 *   takes Sylvan elves for gold, four at most, who fight with blades and shoot arrows (SoldierElf);
 * - Icewind Pass: the Sasquatch's cave frozen shut (TowerSoldierSasquashFrozen) until fire or soldiers break its
 *   ice on the road (SasquashDoor), then the Sasquatch for gold (TowerSoldierSasquash, SoldierSasquash);
 * - Stormcloud Temple: the Sunray Tower (TowerTemple), charged by up to four sorcerers, its ray aimed at an enemy
 *   (TowerTempleCrosshair, RayTemple). */
#include <math.h>
#include <string.h>
#include "gfx.h"
#include "kr.h"
#include "settings.h"
#include "special.h"

#define DOOR_X 201.0f   /* SasquashDoor: its ice on the road (Level8) */
#define DOOR_Y 201.0f
enum { T_INACTIVE, T_CHARGING, T_READY_IN, T_READY, T_SHOOT };   /* TowerTemple's states */

void site_init(int s, int site) {
  Spot *t = &B.spot[s];
  t->kind = (uint8_t)(site == 1 ? TW_ELFRUIN : site == 2 ? TW_CAVE : TW_TEMPLE);
  t->level = 1, t->target = -1, t->elemental = -1;
  for (int k = 0; k < 3; k++) t->soldiers[k] = -1;
}

int site_soldiers(int s) {
  int n = 0;
  for (int i = 0; i < MAX_SOLDIERS; i++) n += B.so[i].alive && B.so[i].spot == s && (B.so[i].kind == SK_ELF || B.so[i].kind == SK_SASQ);
  return n;
}
int site_mages(int s) { return B.spot[s].sp_lvl[0]; }

/* ------------------------------------------------------------------ the menus */
int site_buy_cost(int s) {
  Spot *t = &B.spot[s];
  switch (t->kind) {
  case TW_ELFRUIN: return S_specials_elves_repairCost;
  case TW_ELF: return site_soldiers(s) >= S_specials_elves_max ? -2 : S_specials_elves_elfCost;
  case TW_CAVE: return -2;   /* (frozen) */
  case TW_SASQ: return site_soldiers(s) >= S_specials_sasquash_max ? -2 : S_specials_sasquash_cost;
  case TW_TEMPLE: return site_mages(s) >= 4 ? -2 : S_specials_templeTower_mageCost;
  }
  return -1;
}

/* gotoIdle: the soldiers on an ellipse 40 x 28 around the rally point, 360 / n degrees apart */
static void place_soldiers(int s, bool all_move) {
  Spot *t = &B.spot[s];
  int n = site_soldiers(s);
  if (!n) return;
  int step = 360 / n, deg = step;
  for (int i = 0; i < MAX_SOLDIERS; i++) {
    Soldier *so = &B.so[i];
    if (!so->alive || so->spot != s || (so->kind != SK_ELF && so->kind != SK_SASQ)) continue;
    float rad = (float)(deg - 90) * (float)M_PI / 180.0f;
    Pt p = {t->rally.x + cosf(rad) * 20.0f, t->rally.y + 10.0f + sinf(rad) * 14.0f};
    deg += step;
    /* (changeRallyPoint moves them all; a new soldier only tells the others where, the fighters stay) */
    if (so->kind == SK_SASQ && so->sp_state == 7) so->rx = p.x, so->ry = p.y;
    else if (all_move || !so->fighting) soldier_set_rally(so, p);
    else so->rx = p.x, so->ry = p.y;
  }
}

void site_buy(int s) {
  Spot *t = &B.spot[s];
  int c = site_buy_cost(s);
  if (c < 0 || c > B.cash) return;
  battle_cash(-c);
  t->spent = (int16_t)(t->spent + c);
  switch (t->kind) {
  case TW_ELFRUIN:   /* repaired: the hall, in a cloud */
    t->kind = TW_ELF, t->door = 0, t->door_t = 0, t->anim_t = 0;
    fx_add(AN_T_CLOUD, t->x, t->y, false, false);
    break;
  case TW_ELF:
    soldier_new(SK_ELF, s, (Pt){t->x, t->y}, t->rally);
    place_soldiers(s, false);
    break;
  case TW_SASQ:
    soldier_new(SK_SASQ, s, (Pt){t->x - 60, t->y + 15}, t->rally);
    place_soldiers(s, false);
    break;
  case TW_TEMPLE: {   /* a sorcerer: the tower ready, and faster */
    uint8_t n = (uint8_t)(t->sp_lvl[0] + 1);
    t->sp_lvl[0] = n;
    t->reload = (int16_t)(S_specials_templeTower_reloadTime - S_specials_templeTower_reloadDecrease * n);
    t->reload_t = 0, t->sp_flag = T_READY, t->anim_t = 0, t->sp_t[1] = 0;
    break;
  }
  }
}

void site_rally(int s, Pt p) {
  B.spot[s].rally = p;
  place_soldiers(s, true);
  fx_add(AN_T_FLAG, p.x, p.y, false, false);
}

/* the rally point's reach: the barracks' (rangeRally) above the hall, the Sasquatch's 450 below its cave */
int site_rally_range(int s, Pt *centre) {
  Spot *t = &B.spot[s];
  if (t->kind == TW_SASQ) {
    *centre = (Pt){t->x, t->y + 15};
    return S_specials_sasquash_rallyRange;
  }
  *centre = (Pt){t->x, t->y - 10};
  return barrack_stat(TW_BARRACKS, 1).rally;
}

/* ------------------------------------------------------------------ the Sasquatch's cave */
static int cave(void) {
  for (int s = 0; s < B.nspots; s++)
    if (B.spot[s].kind == TW_CAVE && B.spot[s].sp_flag == 0) return s;
  return -1;
}
static void cave_break(int s) {
  Spot *t = &B.spot[s];
  t->sp_flag = 1, t->sp_c = 0, t->anim_t = 0;   /* upgradeTower("free") */
}
void site_ice_hit(float x, float y, float w, float h) {
  int s = cave();
  if (s >= 0 && ellipse_contains(x, y, w, h, DOOR_X, DOOR_Y)) cave_break(s);
}
void site_reinforcement(const Soldier *so) {   /* (SoldierReinforcement finds the ice when nothing else) */
  int s = cave();
  if (s >= 0 && ellipse_contains(so->home_x, so->home_y, so->range_w, so->range_h, DOOR_X, DOOR_Y)) cave_break(s);
}

/* ------------------------------------------------------------------ the Sunray Tower */
bool site_ray_ready(int s) { return B.spot[s].kind == TW_TEMPLE && B.spot[s].sp_flag == T_READY; }

void site_ray(int s, int enemy) {
  Spot *t = &B.spot[s];
  if (!site_ray_ready(s) || !EN(enemy)) return;
  t->sp_target = (int8_t)enemy, t->sp_flag = T_SHOOT, t->sp_c = 0;
}

static void temple_step(int s) {
  Spot *t = &B.spot[s];
  switch (t->sp_flag) {
  case T_CHARGING:
    if (t->reload_t < t->reload) {
      t->reload_t++;
      break;
    }
    t->sp_flag = T_READY_IN, t->sp_c = 0;
    /* fall through */
  case T_READY_IN:
    if (t->sp_c < 10) {
      t->sp_c++;
      break;
    }
    t->sp_flag = T_READY, t->anim_t = 0, t->sp_t[1] = 1;   /* the sorcerers end their charge */
    break;
  case T_SHOOT:
    if (t->sp_c < 16) {
      if (++t->sp_c == 1) {   /* RayTemple, from the tower's top, as strong as its sorcerers */
        Enemy *e = EN(t->sp_target);
        Bullet *b = e ? bullet_new(BU_SUNRAY) : 0;
        if (b) {
          b->x = b->x0 = t->x, b->y = b->y0 = t->y - 90;
          b->target = t->sp_target, b->level = t->sp_lvl[0], b->t0 = 0, b->t1 = 11;
          b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
          fx_add(AN_B_SUNRAYHIT, b->tx, b->ty, false, false);
        }
      }
      break;
    }
    t->reload_t = 0, t->sp_c = 0, t->sp_flag = T_CHARGING, t->anim_t = 0, t->sp_t[1] = 0;
    break;
  }
}

bool site_bullet(Bullet *b) {
  if (b->kind != BU_SUNRAY) return false;
  if (b->t0 >= b->t1) {
    b->alive = 0;
    return true;
  }
  Enemy *e = EN(b->target);
  if (e && e->active) {
    b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;   /* (follow) */
    if (b->t0 == 2) {   /* templeHit: magic; a kill desintegrates, not a boss */
      int lo = S_specials_templeTower_minDamage + S_specials_templeTower_damageIncrement * b->level;
      int hi = S_specials_templeTower_maxDamage + S_specials_templeTower_damageIncrement * b->level;
      int dmg = lo + (int)ceilf(rnd() * (float)(hi - lo));
      int left = e->health - (int)((float)dmg - (float)e->magic_armor * dmg / 100.0f);
      if (left <= 0 && !e->boss) enemy_kill(e, 2);
      else enemy_damage(e, dmg, ARM_M, 0);
    }
  }
  b->t0++;
  return true;
}

/* ------------------------------------------------------------------ a frame */
void site_step(int s) {
  Spot *t = &B.spot[s];
  switch (t->kind) {
  case TW_ELF: barracks_step(s); break;   /* (its door) */
  case TW_CAVE:
    if (t->sp_flag == 1 && ++t->sp_c >= 46) {   /* freeAnimationTime: then the Sasquatch's cave */
      t->kind = TW_SASQ, t->x = 202, t->y = 169, t->sp_flag = 0, t->sp_c = 0, t->anim_t = 0;
    }
    break;
  case TW_TEMPLE: temple_step(s); break;
  }
}

/* ------------------------------------------------------------------ the soldiers */
static int elf_target(const Soldier *so) {   /* the first in the arrows' reach, not too close */
  for (int i = 0; i < MAX_ENEMIES; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active) continue;
    float w = S_specials_elves_arrowRange;
    if (!ellipse_contains(so->x, so->y, w, w * RANGE_RATIO, e->x, e->y)) continue;
    float dx = e->x - so->x, dy = e->y - so->y;
    if (sqrtf(dx * dx + dy * dy) > S_specials_elves_arrowMinRange) return i;
  }
  return -1;
}

static void elf_arrow(Soldier *so) {
  Enemy *e = EN(so->sp_target);
  if (!e || !e->active) {
    int k = elf_target(so);
    so->sp_target = (int8_t)k;
    e = EN(k);
  }
  Bullet *b = bullet_new(BU_ARROW);
  if (!b) return;
  b->x = b->x0 = so->x + 4, b->y = b->y0 = so->y - 12.5f;
  b->level = 5, b->t1 = 12, b->g = 1;
  b->min_dmg = S_specials_elves_arrowMinDamage, b->max_dmg = S_specials_elves_arrowMaxDamage;
  b->target = e ? (int8_t)(e - B.en) : -1;
  if (!e) b->tx = so->sp_at.x, b->ty = so->sp_at.y;
  else if (e->held) b->tx = e->x + enemy_stats[e->type].x_adj, b->ty = e->y + enemy_stats[e->type].y_adj;
  else {
    b->tx = e->x + enemy_stats[e->type].x_adj + e->xs * b->t1 + rnd() * 10;
    b->ty = e->y + enemy_stats[e->type].y_adj + e->ys * b->t1 + rnd() * 10 - 10;
  }
  b->xs = (b->tx - b->x0) / b->t1;
  b->ys = (b->ty - b->y0 - b->g * b->t1 * (b->t1 / 2.0f)) / b->t1;
}

bool site_soldier(Soldier *so) {
  if (so->kind == SK_SASQ && so->sp_state == 7) {   /* out of the cave: to the road first (§_-6j§) */
    float dx = 214 - so->x, dy = 211 - so->y;
    if (sqrtf(dx * dx + dy * dy) <= so->speed) {
      so->sp_state = 0, so->dx = so->rx, so->dy = so->ry;
      return false;
    }
    float a = atan2f(dy, dx);
    so->left = dx < 0;
    so->x += cosf(a) * so->speed, so->y += sinf(a) * so->speed;
    return true;
  }
  if (so->kind != SK_ELF || !so->active) return false;
  /* SoldierElf: an arrow every arrowCoolDown, standing idle, at whoever is in reach */
  if (so->sp_state == 0) {
    if (so->sp_t < S_specials_elves_arrowCoolDown) {
      so->sp_t++;
      return false;
    }
    if (!so->idle) return false;
    int k = elf_target(so);
    if (k < 0) return false;
    Enemy *e = &B.en[k];
    so->left = e->x < so->x;
    so->sp_target = (int8_t)k, so->sp_at = (Pt){e->x + enemy_stats[e->type].x_adj, e->y + enemy_stats[e->type].y_adj};
    so->sp_state = 5, so->sp_c = 0;
    so->anim = AN_SO_ELF_ARROW, so->anim_t = 0;
    return true;
  }
  if (so->sp_c < 15) {
    if (++so->sp_c == 7) elf_arrow(so);
    return true;
  }
  so->sp_state = 0, so->sp_t = 0;
  so->anim = AN_SO_ELF_IDLE, so->anim_t = 0;
  return false;
}

/* SoldierSasquash.areaAttack: up to areaAttackMaxEnemies walkers in 70 x 49, 30 ahead of it */
bool site_soldier_hit(Soldier *so) {
  if (so->kind != SK_SASQ) return false;
  float cx = so->left ? so->x - 30 : so->x + 30;
  int n = 0;
  for (int i = 0; i < MAX_ENEMIES && n < S_specials_sasquash_areaAttackMaxEnemies; i++) {
    Enemy *e = &B.en[i];
    if (!e->alive || !e->active || e->flying) continue;
    float w = S_specials_sasquash_areaAttackRangeWidth;
    if (!ellipse_contains(cx, so->y, w, w * RANGE_RATIO, e->x, e->y)) continue;
    if (!enemy_dodge(e)) enemy_damage(e, rnd_dmg(so->min_dmg, so->max_dmg), ARM_P, 0);
    n++;
  }
  return true;
}

void site_respawn(Soldier *so) {   /* the Sasquatch comes out beside its cave and goes to the road first */
  if (so->kind != SK_SASQ || so->spot < 0) return;
  Spot *t = &B.spot[so->spot];
  so->x = t->x - 50, so->y = t->y + 10;
  so->dx = 214, so->dy = 211, so->sp_state = 7;
}

void site_draw_bullet(const Bullet *b) {
  if (b->kind != BU_SUNRAY || b->t0 >= b->t1) return;
  int x0 = 40 + (int)lroundf(b->x0 * 0.4f), y0 = (int)lroundf(b->y0 * 0.4f);
  int tx = 40 + (int)lroundf(b->tx * 0.4f), ty = (int)lroundf(b->ty * 0.4f);
  gfx_line(x0 - 1, y0, tx - 1, ty, RGB(0xFFB020));
  gfx_line(x0 + 1, y0, tx + 1, ty, RGB(0xFFB020));
  gfx_line(x0, y0, tx, ty, RGB(0xFFF6C0));
}
