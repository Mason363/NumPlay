/* The spells (PowerFireball, PowerReinforcement, FireballControler, the reinforcements' pointers). */
#include <math.h>
#include "kr.h"
#include "settings.h"

/* the rain of fire's numbers, with the upgrades (GameSettings.§_-Gm§) */
static int fire_reload(void) {
  int cd = S_powers_fireball_coolDown;
  if (up_on(UP_RAIN, 3)) cd -= 10 * FPS;
  if (up_on(UP_RAIN, 4)) cd -= 10 * FPS;
  return cd;
}
static void fire_damage(int *lo, int *hi, int *range) {
  *lo = S_powers_fireball_minDamage, *hi = S_powers_fireball_maxDamage, *range = S_powers_fireball_range;
  if (upgrades[UP_RAIN] >= 1) *lo += 20, *hi += 20;
  if (up_on(UP_RAIN, 3)) *range += *range / 4, *lo += 40, *hi += 40;
  if (up_on(UP_RAIN, 5)) *lo += 60, *hi += 60;
}

static int reinf_level(void) {
  int l = upgrades[UP_REINF];
  if (B.max_upgrade != 0 && B.max_upgrade < l) l = B.max_upgrade;
  return l;
}

void power_init(void) {
  B.fire_reload = (int16_t)fire_reload();
  B.reinf_reload = 10 * FPS;
}

/* unlocked: after 15 + 17 frames it is ready (§_-Ff§: opening, then refresh) */
void power_unlock_fire(void) {
  if (!B.fire_on) B.fire_on = 1, B.fire_open_t = 32, B.fire_cd = 0;
}
void power_unlock_reinf(void) {
  if (!B.reinf_on) B.reinf_on = 1, B.reinf_open_t = 32, B.reinf_cd = 0;
}

int power_fire_cooldown(void) { return !B.fire_on ? -1 : B.fire_open_t ? B.fire_open_t : B.fire_cd; }
int power_reinf_cooldown(void) { return !B.reinf_on ? -1 : B.reinf_open_t ? B.reinf_open_t : B.reinf_cd; }

void power_shorten(int frames) {   /* §_-Hv§: calling a wave early takes the time off the cooldowns */
  if (B.fire_on && !B.fire_open_t && B.fire_cd > 0) {
    B.fire_cd = (int16_t)(B.fire_cd - frames);
    if (B.fire_cd <= 0) B.fire_cd = 1;
  }
  if (B.reinf_on && !B.reinf_open_t && B.reinf_cd > 0) {
    B.reinf_cd = (int16_t)(B.reinf_cd - frames);
    if (B.reinf_cd <= 0) B.reinf_cd = 1;
  }
}

/* ------------------------------------------------------------------ rain of fire */
bool power_fire(Pt at) {
  if (power_fire_cooldown() != 0) return false;
  for (int k = 0; k < 2; k++)
    if (!B.rain[k].on) {
      B.rain[k].on = 1, B.rain[k].t = 0, B.rain[k].at = (Pt){at.x, at.y + 20};
      /* three fireballs (five with Blazing Skies), and Cataclysm's five more over the roads */
      B.rain[k].dur = (int16_t)(31 + (upgrades[UP_RAIN] >= 1 ? 20 : 0));
      break;
    }
  B.fire_cd = (int16_t)fire_reload();
  return true;
}

static void fireball(Pt from, Pt to) {
  Bullet *b = bullet_new(BU_FIREBALL);
  if (!b) return;
  int lo, hi, range;
  fire_damage(&lo, &hi, &range);
  b->x = from.x, b->y = from.y, b->tx = to.x, b->ty = to.y;
  b->acc = 10, b->acc_max = 15;
  b->min_dmg = (int16_t)rnd_dmg(lo, hi), b->area = (int16_t)range;
  b->pierce = up_on(UP_RAIN, 2);   /* (Scorched Earth) */
}

static void rain_step(int k) {
  int dev = 15 + (up_on(UP_RAIN, 3) ? 10 : 0);
  bool cataclysm = up_on(UP_RAIN, 5);
  int t = ++B.rain[k].t;
  if (t < B.rain[k].dur && t % 10 == 0) {
    float sx = rnd() >= 0.5f ? 1 : -1, sy = rnd() >= 0.5f ? 1 : -1;
    Pt at = B.rain[k].at;
    float x0 = at.x + sx * (float)rnd_round(0, dev);
    Pt to = {at.x + sx * (float)rnd_round(0, dev), at.y + sy * (float)rnd_round(0, dev)};
    fireball((Pt){x0, 0}, to);
  }
  if (cataclysm && t < 51 && t % 10 == 0) {
    int p = (int)floorf((float)rnd_round(0, B.npaths - 1));
    int n = (int)floorf((float)rnd_round(0, path_len(p, 0) - 1));
    Pt to = path_node(p, 0, n);
    fireball((Pt){to.x, 0}, to);
  }
  if ((!cataclysm && t >= B.rain[k].dur) || (cataclysm && t >= 51 && t >= B.rain[k].dur)) B.rain[k].on = 0;
}

/* ------------------------------------------------------------------ reinforcements */
bool power_reinforce_ok(Pt at) {
  for (int p = 0; p < B.npaths; p++) {
    int n = path_len(p, 0);
    for (int k = 0; k < n; k++) {
      Pt q = path_node(p, 0, k);
      float dx = q.x - at.x, dy = q.y - at.y;
      if (sqrtf(dx * dx + dy * dy) < 30) return true;
    }
  }
  return false;
}

bool power_reinforce(Pt at) {
  if (power_reinf_cooldown() != 0 || !power_reinforce_ok(at)) return false;
  int l = reinf_level(), kind = l <= 1 ? SK_FARMER : l == 2 ? SK_REINF2 : l == 3 ? SK_REINF3 : SK_REINF4;
  for (int k = 0; k < 2; k++) {
    float d = k ? -7.0f : 7.0f;
    int i = soldier_new(kind, -1, (Pt){at.x + d, at.y + d}, (Pt){at.x + d, at.y + d});
    if (i >= 0) B.so[i].home_x = at.x, B.so[i].home_y = at.y, B.so[i].look = (uint8_t)(rnd() * 3);
  }
  B.reinf_cd = B.reinf_reload;
  return true;
}

/* ------------------------------------------------------------------ a frame */
void power_step(void) {
  if (B.fire_open_t) B.fire_open_t--;
  else if (B.fire_on && B.fire_cd > 0) B.fire_cd--;
  if (B.reinf_open_t) B.reinf_open_t--;
  else if (B.reinf_on && B.reinf_cd > 0) B.reinf_cd--;
  for (int k = 0; k < 2; k++)
    if (B.rain[k].on) rain_step(k);
}
