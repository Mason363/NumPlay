/* Subway Surfers: the bot of tests/play.c (and of a timing build, make bench): it reads the tracks ahead and changes
 * lanes, jumps and rolls (it is no expert: it stumbles and crashes now and then). */
#include "../src/game.h"
#include "../src/plat.h"

/* a ramp up to this train? */
static bool ramped(const Obstacle *t) {
  for (int i = 0; i < nobs; i++)
    if (obs[i].type == O_RAMP && obs[i].lane == t->lane && obs[i].z1 > t->z0 - 0.5f && obs[i].z1 < t->z0 + 0.5f) return true;
  return false;
}

static bool blocked(int lane, float z0, float z1, float y) {
  for (int i = 0; i < nobs; i++) {
    const Obstacle *o = &obs[i];
    if (o->type == O_TRAIN && ramped(o) && o->z0 > z0 + 1) continue;
    /* (a train coming this way: where it will be by then) */
    float ahead = o->type == O_MOVING ? o->v * (z1 - z0) / pl.speed : 0;
    if (o->lane != lane || o->z1 < z0 || o->z0 - ahead > z1) continue;
    if (o->type == O_RAMP || o->type == O_HURDLE || o->type == O_HIGHBAR) continue;
    if ((o->type == O_TRAIN || o->type == O_MOVING) && y >= TRAIN_H - 0.1f) continue;
    return true;
  }
  return false;
}

static uint32_t bot(void) {
  static int wait;
  if (wait > 0) {
    wait--;
    return 0;
  }
  if (pl.dead) return 0;
  float look = pl.speed * 0.75f, z = pl.z;
  /* the nearest thing in this lane */
  const Obstacle *near = NULL;
  for (int i = 0; i < nobs; i++) {
    const Obstacle *o = &obs[i];
    float ahead = o->type == O_MOVING ? o->v * look / pl.speed : 0;
    if (o->lane != pl.lane || o->z1 < z || o->z0 - ahead > z + look) continue;
    if (o->type == O_RAMP) continue;
    if ((o->type == O_TRAIN || o->type == O_MOVING) && pl.y >= TRAIN_H - 0.1f) continue;
    if (!near || o->z0 < near->z0) near = o;
  }
  if (!near) return 0;
  /* on a roof, the next train along: jump the gap */
  if (pl.on_roof && near->type == O_TRAIN && near->z0 - z < pl.speed * 0.3f) {
    wait = 10;
    return K_UP;
  }
  float d = near->z0 - z - (near->type == O_MOVING ? near->v * (near->z0 - z) / (pl.speed + near->v) : 0);
  if (near->type == O_HURDLE && d < pl.speed * 0.25f && pl.state != ST_AIR) {
    wait = 8;
    return K_UP;
  }
  if (near->type == O_HIGHBAR && d < pl.speed * 0.22f && pl.roll <= 0) {
    wait = 8;
    return K_DOWN;
  }
  if (near->type == O_HURDLE || near->type == O_HIGHBAR) return 0;
  /* a train or a big barrier: another lane */
  int best = -1;
  for (int k = 0; k < 3; k++) {
    int l = pl.lane + (k == 0 ? -1 : k == 1 ? 1 : 0);
    if (k == 2 || l < 0 || l > 2) continue;
    if (!blocked(l, z - 1, z + look, pl.y)) {
      best = l;
      break;
    }
  }
  if (best < 0) {
    /* two lanes over? */
    int l = pl.lane == 0 ? 2 : pl.lane == 2 ? 0 : -1;
    if (l >= 0 && !blocked(1, z - 1, z + 3, pl.y) && !blocked(l, z - 1, z + look, pl.y)) best = pl.lane == 0 ? 1 : 1;
  }
  wait = 6;
  if (best < 0) {
    if (d < pl.speed * 0.3f && pl.state != ST_AIR) return K_UP;
    return 0;
  }
  return best < pl.lane ? K_LEFT : K_RIGHT;
}
