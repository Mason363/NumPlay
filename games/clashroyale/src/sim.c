/* The battle (sim.h): towers, elixir, the cards, and every unit's step, at the game's numbers (tables.c). */
#include <string.h>
#include "sim.h"

/* DAMAGE/HITPOINT_INCREASE_PERCENT_PER_SPELL_LEVEL = 10: each level the last * 110 / 100, rounded down */
const int32_t LEVEL_MULT[16] = {100, 110, 121, 133, 146, 160, 176, 193, 212, 233, 256, 281, 309, 339, 372, 409};

static int32_t compound(int pct, int levels) {
  int32_t m = 100;
  for (int i = 1; i < levels; i++) m = m * (100 + pct) / 100;
  return m;
}
int32_t king_mult(void) { return compound(GL(HITPOINT_INCREASE_PERCENT_PER_KING_LEVEL), GL(TOURNAMENT_MAX_EXP_LEVEL)); }
int32_t tower_mult(void) { return compound(GL(HITPOINT_INCREASE_PERCENT_PER_TOWER_LEVEL), GL(TOURNAMENT_MAX_EXP_LEVEL)); }

int card_level(int card) {
  switch (CARDS[card].rarity) {
  case R_COMMON: return GL(TOURNAMENT_MAX_COMMON_LEVEL);
  case R_RARE: return GL(TOURNAMENT_MAX_RARE_LEVEL);
  case R_EPIC: return GL(TOURNAMENT_MAX_EPIC_LEVEL);
  default: return 1;
  }
}
int card_lvl_index(int card) { return card_level(card) - 1; }
int32_t lv(int32_t v, int idx) { return v * LEVEL_MULT[idx < 0 ? 0 : idx > 15 ? 15 : idx] / 100; }

uint32_t sim_rand(uint32_t *s) {
  uint32_t x = *s ? *s : 0x9E3779B9u;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return *s = x;
}
static int32_t rnd(Battle *b, int32_t n) { return n > 0 ? (int32_t)(sim_rand(&b->rng) % (uint32_t)n) : 0; }

int32_t isqrt64(int64_t v) {
  if (v <= 0) return 0;
  uint64_t r = 0, bit = (uint64_t)1 << 62, n = (uint64_t)v;
  while (bit > n) bit >>= 2;
  while (bit) {
    if (n >= r + bit) n -= r + bit, r = (r >> 1) + bit;
    else r >>= 1;
    bit >>= 2;
  }
  return (int32_t)r;
}
int32_t dist(int32_t ax, int32_t ay, int32_t bx, int32_t by) {
  int64_t dx = ax - bx, dy = ay - by;
  return isqrt64(dx * dx + dy * dy);
}

bool ent_alive(const Battle *b, int i) { return i >= 0 && i < MAXE && b->e[i].used && b->e[i].hp > 0; }
bool ent_flying(const Ent *e) { return (UNITS[e->type].flags & U_FLY) != 0; }
static const UnitDef *def(const Ent *e) { return &UNITS[e->type]; }
static bool is_building(const Ent *e) { return (def(e)->flags & U_BUILDING) != 0; }

/* ------------------------------------------------------------------ the arena's ground (locations' Map) */
static bool on_bridge(int32_t y) { return (y >= 2500 && y < 4500) || (y >= 13500 && y < 15500); }
static bool water(int32_t x, int32_t y) { return x >= RIVER_X0 && x < RIVER_X1 && !on_bridge(y); }
static bool blocked(int32_t x, int32_t y) {
  if (x < 0 || x >= ARENA_X || y < 0 || y >= ARENA_Y) return true;
  if ((x < 1000 || x >= 31000) && (y < 6000 || y >= 12000)) return true;   /* behind the King towers */
  if (((x >= 14000 && x < 15000) || (x >= 17000 && x < 18000)) && (y < 1000 || y >= 17000)) return true;
  return false;
}
static int32_t bridge_y(int32_t y) { return y < ARENA_Y / 2 ? 3500 : 14500; }
static int side_of(int32_t x) { return x < (RIVER_X0 + RIVER_X1) / 2 ? 0 : 1; }

/* ------------------------------------------------------------------ entities */
static void fx_add(Battle *b, int kind, int team, int32_t x, int32_t y, int size, int len) {
  for (int i = 0; i < MAXFX; i++)
    if (!b->fx[i].used) {
      b->fx[i] = (Fx){1, (uint8_t)kind, (uint8_t)team, (uint8_t)size, (int16_t)(x / 100), (int16_t)(y / 100), 0, (int16_t)len};
      return;
    }
}

int battle_spawn(Battle *b, int type, int team, int lvl, int32_t x, int32_t y, int32_t deploy) {
  if (type < 0 || type >= NUNITS) return -1;
  for (int i = 0; i < MAXE; i++) {
    if (b->e[i].used) continue;
    const UnitDef *d = &UNITS[type];
    Ent *e = &b->e[i];
    memset(e, 0, sizeof *e);
    e->used = 1, e->type = (uint8_t)type, e->team = (uint8_t)team, e->lvl = (uint8_t)lvl;
    e->x = x, e->y = y;
    e->maxhp = e->hp = d->flags & U_BOMB ? 1 : lv(d->hp, lvl);
    e->dmg = d->proj != NONE ? lv(PROJS[d->proj].dmg, lvl) : lv(d->dmg, lvl);
    e->deploy = deploy;
    e->target = -1;
    e->gen = ++b->gen;
    e->face = team ? 4 : 0;
    e->anim = 2;
    e->spawn_t = d->spawn_start;
    e->hidden = (d->flags & U_HIDES) ? 1 : 0;
    return i;
  }
  return -1;
}

static bool hittable(const Battle *b, const Ent *a, int ti) {
  if (!ent_alive(b, ti)) return false;
  const Ent *t = &b->e[ti];
  if (t->team == a->team) return false;
  const UnitDef *td = def(t), *ad = def(a);
  if (td->flags & U_BOMB) return false;
  if (t->hidden) return false;
  if ((ad->flags & U_BUILDINGS) && !(td->flags & U_BUILDING)) return false;
  bool fly = (td->flags & U_FLY) != 0;
  return fly ? (ad->flags & U_AIR) != 0 : (ad->flags & U_GROUND) != 0;
}

static int32_t edge(const Ent *a, const Ent *t) { return dist(a->x, a->y, t->x, t->y) - def(t)->radius; }

/* how far a ground unit walks to (tx, ty): over a bridge when the river is between */
static int32_t path_len(const Ent *a, int32_t tx, int32_t ty) {
  const UnitDef *d = def(a);
  if ((d->flags & (U_FLY | U_JUMP)) || (a->x >= RIVER_X0 && a->x < RIVER_X1) || side_of(a->x) == side_of(tx))
    return dist(a->x, a->y, tx, ty);
  int32_t best = 0x7FFFFFFF;
  for (int k = 0; k < 2; k++) {
    int32_t by = k ? 14500 : 3500, ex = side_of(a->x) ? RIVER_X1 : RIVER_X0, ox = side_of(a->x) ? RIVER_X0 : RIVER_X1;
    int32_t l = dist(a->x, a->y, ex, by) + 2000 + dist(ox, by, tx, ty);
    if (l < best) best = l;
  }
  return best;
}

/* the next point to walk to on the way to (tx, ty) */
static void waypoint(const Ent *a, int32_t tx, int32_t ty, int32_t *wx, int32_t *wy) {
  const UnitDef *d = def(a);
  *wx = tx, *wy = ty;
  if (d->flags & (U_FLY | U_JUMP)) return;
  if (a->x >= RIVER_X0 - 300 && a->x < RIVER_X1 + 300 && on_bridge(a->y)) {
    if (side_of(tx) != side_of(a->x) || (a->x >= RIVER_X0 && a->x < RIVER_X1)) {
      /* on a bridge: straight across */
      *wx = tx >= (RIVER_X0 + RIVER_X1) / 2 ? RIVER_X1 + 600 : RIVER_X0 - 600;
      *wy = bridge_y(a->y);
      if ((tx >= (RIVER_X0 + RIVER_X1) / 2) == (a->x >= RIVER_X1)) *wx = tx, *wy = ty;
      return;
    }
  }
  if (side_of(a->x) == side_of(tx)) return;
  /* the bridge that makes the shorter walk */
  int32_t best = 0x7FFFFFFF, by = 3500;
  for (int k = 0; k < 2; k++) {
    int32_t y = k ? 14500 : 3500, ex = side_of(a->x) ? RIVER_X1 : RIVER_X0, ox = side_of(a->x) ? RIVER_X0 : RIVER_X1;
    int32_t l = dist(a->x, a->y, ex, y) + dist(ox, y, tx, ty);
    if (l < best) best = l, by = y;
  }
  *wx = side_of(a->x) ? RIVER_X1 + 200 : RIVER_X0 - 200;
  *wy = by;
  if (dist(a->x, a->y, *wx, *wy) < 500 || (a->x >= RIVER_X0 - 700 && a->x < RIVER_X1 + 700 && on_bridge(a->y)))
    *wx = side_of(a->x) ? RIVER_X0 - 600 : RIVER_X1 + 600;
}

static void wake_king(Battle *b, int team) {
  int k = b->side[team].king;
  if (ent_alive(b, k) && !(b->e[k].flags & E_AWAKE) && !b->e[k].wake) b->e[k].wake = GL(KING_ACTIVATE_TIME_MS);
}

static void kill(Battle *b, int i);

static void hurt(Battle *b, int i, int32_t dmg, bool reduced) {
  if (!ent_alive(b, i) || dmg <= 0) return;
  Ent *e = &b->e[i];
  if ((def(e)->flags & U_TOWER) && reduced) dmg = dmg * GL(REDUCED_TOWER_DAMAGE_PERCENT) / 100;
  e->hp -= dmg;
  e->flash = 120;
  if (def(e)->flags & U_KING) wake_king(b, e->team);
  if (e->hp <= 0) kill(b, i);
}

static void push(Ent *e, int32_t cx, int32_t cy, int32_t amount) {
  if ((def(e)->flags & U_NOPUSH) || amount <= 0) return;
  int32_t d = dist(e->x, e->y, cx, cy);
  int32_t dx = d ? (e->x - cx) * 1000 / d : 1000, dy = d ? (e->y - cy) * 1000 / d : 0;
  e->push_t = 5;
  e->push_x = dx * amount / 1000 / 5, e->push_y = dy * amount / 1000 / 5;
}

/* damage around (x, y): enemies of team, ground and/or air, buildings too; pushed back */
static void splash(Battle *b, int team, int32_t x, int32_t y, int32_t r, int32_t dmg, bool ground, bool air, bool reduced,
                   int32_t pushback, bool spell) {
  for (int j = 0; j < MAXE; j++) {
    if (!ent_alive(b, j)) continue;
    Ent *t = &b->e[j];
    if (t->team == team || (def(t)->flags & U_BOMB) || (t->hidden && !spell)) continue;
    bool fly = ent_flying(t);
    if ((fly && !air) || (!fly && !ground)) continue;
    if (dist(x, y, t->x, t->y) > r + def(t)->radius) continue;
    if (pushback) push(t, x, y, pushback);
    hurt(b, j, dmg, reduced);
  }
}

static void place_near(int32_t *x, int32_t *y, bool fly) {
  if (*x < 500) *x = 500;
  if (*x > ARENA_X - 500) *x = ARENA_X - 500;
  if (*y < 500) *y = 500;
  if (*y > ARENA_Y - 500) *y = ARENA_Y - 500;
  if (!fly && water(*x, *y)) *x = *x < (RIVER_X0 + RIVER_X1) / 2 ? RIVER_X0 - 300 : RIVER_X1 + 300;
}

static void spawn_around(Battle *b, int type, int team, int lvl, int32_t x, int32_t y, int n, int32_t r, int32_t deploy) {
  static const int8_t ring[8][2] = {{10, 0}, {0, -10}, {0, 10}, {-10, 0}, {7, -7}, {7, 7}, {-7, -7}, {-7, 7}};
  for (int k = 0; k < n; k++) {
    int32_t px = x, py = y;
    if (n > 1) {
      int fx = team ? -ring[k & 7][0] : ring[k & 7][0];
      px += fx * r / 10, py += ring[k & 7][1] * r / 10;
    }
    place_near(&px, &py, (UNITS[type].flags & U_FLY) != 0);
    battle_spawn(b, type, team, lvl, px, py, deploy);
  }
}

static void end_battle(Battle *b) {
  b->over = 1;
  int c0 = b->side[0].crowns, c1 = b->side[1].crowns;
  b->winner = c0 > c1 ? 0 : c1 > c0 ? 1 : 2;
}

static void kill(Battle *b, int i) {
  Ent *e = &b->e[i];
  if (!e->used) return;
  const UnitDef *d = def(e);
  int team = e->team, lvl = e->lvl;
  int32_t x = e->x, y = e->y;
  e->used = 0, e->hp = 0;
  if (d->flags & U_TOWER)
    fx_add(b, FX_EXPLOSION, team, x, y, 3, 600);
  else if (!(d->flags & U_BOMB))
    fx_add(b, FX_PUFF, team, x, y - (d->flags & U_FLY ? d->fly_height * 6 / 10 : 0), d->radius >= 700 ? 2 : 1, 350);
  if (d->death_dmg) {
    fx_add(b, FX_EXPLOSION, team, x, y, d->death_radius >= 3000 ? 3 : 2, 500);
    splash(b, team, x, y, d->death_radius, lv(d->death_dmg, lvl), (d->flags & U_GROUND) != 0, (d->flags & U_AIR) != 0,
           false, d->death_push, false);
  }
  if (d->death_unit != NONE && d->death_count > 0)
    spawn_around(b, d->death_unit, team, lvl + d->spawn_level, x, y, d->death_count, 700,
                 UNITS[d->death_unit].flags & U_BOMB ? UNITS[d->death_unit].deploy_ms : 0);
  if (d->flags & U_TOWER) {
    Side *s = &b->side[team], *o = &b->side[1 - team];
    if (d->flags & U_KING) {
      s->king = -1;
      o->crowns = 3;
      end_battle(b);
    } else {
      for (int k = 0; k < 2; k++)
        if (s->princess[k] == i) s->princess[k] = -1;
      if (o->crowns < 3) o->crowns++;
      wake_king(b, team);
      if (b->overtime) end_battle(b);
    }
  }
}

/* ------------------------------------------------------------------ the start */
void battle_init(Battle *b, const uint8_t deck0[8], const uint8_t deck1[8], int arena, uint32_t seed) {
  memset(b, 0, sizeof *b);
  b->magic = 0x31424352u;   /* "RCB1" */
  b->size = sizeof *b;
  b->rng = seed ? seed : 1;
  b->arena = (uint8_t)arena;
  b->time_ms = GL(MATCH_LENGTH) * 1000;
  for (int t = 0; t < 2; t++) {
    Side *s = &b->side[t];
    const uint8_t *deck = t ? deck1 : deck0;
    memcpy(s->deck, deck, 8);
    uint8_t order[8];
    memcpy(order, deck, 8);
    for (int i = 7; i > 0; i--) {
      int j = rnd(b, i + 1);
      uint8_t v = order[i];
      order[i] = order[j], order[j] = v;
    }
    memcpy(s->hand, order, 4);
    memcpy(s->queue, order + 4, 4);
    s->nqueue = 4;
    s->elixir = GL(START_MANA);
    s->last_card = NONE;
    int32_t kx = t ? ARENA_X - 3000 : 3000, px = t ? ARENA_X - 6500 : 6500;
    s->king = (int16_t)battle_spawn(b, UN_KINGTOWER, t, 0, kx, 9000, 0);
    s->princess[0] = (int16_t)battle_spawn(b, UN_PRINCESSTOWER, t, 0, px, 3500, 0);
    s->princess[1] = (int16_t)battle_spawn(b, UN_PRINCESSTOWER, t, 0, px, 14500, 0);
    Ent *k = &b->e[s->king];
    k->maxhp = k->hp = UNITS[UN_KINGTOWER].hp * king_mult() / 100;
    k->dmg = PROJS[UNITS[UN_KINGTOWER].proj].dmg * compound(GL(DAMAGE_INCREASE_PERCENT_PER_KING_LEVEL), GL(TOURNAMENT_MAX_EXP_LEVEL)) / 100;
    for (int p = 0; p < 2; p++) {
      Ent *e = &b->e[s->princess[p]];
      e->maxhp = e->hp = UNITS[UN_PRINCESSTOWER].hp * tower_mult() / 100;
      e->dmg = PROJS[UNITS[UN_PRINCESSTOWER].proj].dmg *
               compound(GL(DAMAGE_INCREASE_PERCENT_PER_TOWER_LEVEL), GL(TOURNAMENT_MAX_EXP_LEVEL)) / 100;
      e->flags |= E_AWAKE;
    }
  }
  for (int t = 0; t < 2; t++) {
    b->bot[t].rng = seed * 2654435761u + 7 + (uint32_t)t * 977;
    b->bot[t].level = arena;
    b->bot[t].push_lane = -1;
    b->bot[t].think = 2000 + t * 300;
  }
}

/* ------------------------------------------------------------------ elixir and cards */
bool battle_double(const Battle *b) {
  return b->overtime || b->time_ms <= GL(MANA_SPEED_UP_WHEN_REMAINING_SECONDS) * 1000;
}
int32_t battle_regen_ms(const Battle *b) {
  int32_t m = b->overtime ? GL(MANA_REGEN_MS_OVERTIME) : battle_double(b) ? GL(MANA_REGEN_MS_END) : GL(MANA_REGEN_MS);
  return m / GL(MAX_MANA);
}
static int32_t draw_delay(const Battle *b) {
  return b->overtime ? GL(NEXT_SPELL_COOLDOWN_MILLISECONDS_OVERTIME)
         : battle_double(b) ? GL(NEXT_SPELL_COOLDOWN_MILLISECONDS_BOOST)
                            : GL(NEXT_SPELL_COOLDOWN_MILLISECONDS);
}

bool battle_can_place(const Battle *b, int team, int card, int32_t x, int32_t y) {
  if (x < 0 || x >= ARENA_X || y < 0 || y >= ARENA_Y) return false;
  const CardDef *c = &CARDS[card];
  if (c->kind == K_SPELL) return true;
  bool own = team ? x >= RIVER_X1 : x < RIVER_X0;
  if (!own) {
    /* an enemy Princess tower down opens its side of the field, 4 tiles past the river */
    int lane = y < ARENA_Y / 2 ? 0 : 1;
    if (b->side[1 - team].princess[lane] >= 0) return false;
    if (team == 0 && !(x >= RIVER_X1 && x < RIVER_X1 + 4000)) return false;
    if (team == 1 && !(x < RIVER_X0 && x >= RIVER_X0 - 4000)) return false;
  }
  if (blocked(x, y) || water(x, y)) return false;
  int32_t r = c->kind == K_BUILDING && c->unit != NONE ? UNITS[c->unit].radius : 0;
  for (int j = 0; j < MAXE; j++) {
    const Ent *t = &b->e[j];
    if (!t->used || !(def(t)->flags & U_BUILDING) || (def(t)->flags & U_BOMB)) continue;
    int32_t tr = def(t)->radius;
    if (def(t)->flags & U_TOWER) tr += 400;   /* (their footprints are squares) */
    if (dist(x, y, t->x, t->y) < tr + r) return false;
  }
  return true;
}

const char *battle_check(const Battle *b, int team, int slot, int32_t x, int32_t y) {
  const Side *s = &b->side[team];
  if (b->over) return "The battle is over";
  if (slot < 0 || slot > 3 || s->hand[slot] == NONE) return "The next card is coming";
  if (s->elixir < CARDS[s->hand[slot]].cost) return "Not enough elixir";
  if (!battle_can_place(b, team, s->hand[slot], x, y)) return "Not here: on your side only";
  return NULL;
}

/* the formation of a card's units, facing the enemy (game units) */
static void formation(int n, int k, int32_t *dx, int32_t *dy) {
  *dx = *dy = 0;
  if (n == 2) *dy = k ? 600 : -600;
  else if (n == 3) {
    static const int16_t f[3][2] = {{500, 0}, {-300, -600}, {-300, 600}};
    *dx = f[k][0], *dy = f[k][1];
  } else if (n == 4) {
    *dx = k & 1 ? 500 : -500, *dy = k & 2 ? 500 : -500;
  } else if (n > 4) {
    static const int16_t ring[13][2] = {{1000, 0}, {500, 866}, {-500, 866}, {-1000, 0}, {-500, -866}, {500, -866},
                                        {1000, 0}, {809, 588}, {309, 951}, {-309, 951}, {-809, 588}, {-1000, 0}, {-809, -588}};
    if (n <= 7) {
      if (k < 6) *dx = ring[k][0] * 9 / 10, *dy = ring[k][1] * 9 / 10;
    } else if (k > 0) {
      int r = k <= 6 ? 700 : 1450;
      int i = k <= 6 ? k - 1 : k - 7;
      int m = k <= 6 ? 6 : n - 7;
      /* evenly round a circle: the angle 360 * i / m, from a small table of cosines */
      static const int16_t cs[16][2] = {{1000, 0},   {924, 383},   {707, 707},   {383, 924},  {0, 1000},  {-383, 924},
                                         {-707, 707}, {-924, 383},  {-1000, 0},   {-924, -383}, {-707, -707}, {-383, -924},
                                         {0, -1000},  {383, -924},  {707, -707},  {924, -383}};
      int a = (i * 16 + m / 2) / m & 15;
      *dx = cs[a][0] * r / 1000, *dy = cs[a][1] * r / 1000;
    }
  }
}

static void cast(Battle *b, int team, int card, int32_t x, int32_t y, int lvl) {
  const CardDef *c = &CARDS[card];
  Side *s = &b->side[team];
  if (c->unit != NONE) {
    const UnitDef *u = &UNITS[c->unit];
    for (int k = 0; k < c->count; k++) {
      int32_t dx, dy, px, py;
      formation(c->count, k, &dx, &dy);
      px = x + (team ? -dx : dx), py = y + dy;
      place_near(&px, &py, (u->flags & U_FLY) != 0);
      if (!(u->flags & U_FLY) && (blocked(px, py) || water(px, py))) px = x, py = y;
      int32_t extra = c->count > 1 && u->random_deploy ? rnd(b, u->random_deploy) : 0;
      battle_spawn(b, c->unit, team, lvl, px, py, u->deploy_ms + extra);
    }
  }
  if (c->proj != NONE) {
    /* thrown from the King tower (or where it stood) */
    int32_t kx = team ? ARENA_X - 3000 : 3000, ky = 9000;
    int n = 1 + (c->arrows > 0 ? 3 : 0);
    for (int k = 0; k < n && k < 4; k++)
      for (int i = 0; i < MAXP; i++) {
        if (b->p[i].used) continue;
        Proj *p = &b->p[i];
        memset(p, 0, sizeof *p);
        p->used = 1, p->type = (uint8_t)c->proj, p->team = (uint8_t)team, p->lvl = (uint8_t)lvl, p->spell = 1;
        p->deco = k > 0;
        p->sx = p->x = kx, p->sy = p->y = ky + (k ? (k - 2) * 900 : 0);
        p->tx = x + (k ? (k - 2) * 500 : 0), p->ty = y + (k ? (k & 1 ? 700 : -700) : 0);
        p->target = -1;
        p->z0 = 3000;
        p->dmg = lv(PROJS[c->proj].dmg, lvl);
        int32_t sp = PROJS[c->proj].speed * TICK * 1000 / 60000;
        p->total = sp ? dist(kx, ky, x, y) / sp * TICK : 0;
        break;
      }
  }
  if (c->area != NONE) {
    const AreaDef *a = &AREAS[c->area];
    for (int i = 0; i < MAXA; i++) {
      if (b->a[i].used) continue;
      Area *ar = &b->a[i];
      memset(ar, 0, sizeof *ar);
      ar->used = 1, ar->type = (uint8_t)c->area, ar->team = (uint8_t)team, ar->lvl = (uint8_t)lvl;
      ar->x = x, ar->y = y;
      ar->total = ar->life = a->life_ms + a->life_per_level * lvl;
      for (int k = 0; k < 4; k++) ar->hit_ids[k] = -1;
      if (a->dmg) {
        /* Zap: at once, and it stops what it hits for a moment */
        splash(b, team, x, y, a->radius, lv(a->dmg, lvl), (a->flags & A_GROUND) != 0, (a->flags & A_AIR) != 0,
               (a->flags & A_REDUCED) != 0, 0, true);
        for (int j = 0; j < MAXE; j++) {
          Ent *t = &b->e[j];
          if (!ent_alive(b, j) || t->team == team || dist(x, y, t->x, t->y) > a->radius + def(t)->radius) continue;
          t->stun = 500, t->t = 0, t->lock = 0, t->walked = 0, t->flags &= (uint8_t)~E_CHARGING;
        }
        fx_add(b, FX_ZAP, team, x, y, 1, 300);
      }
      break;
    }
  }
  (void)s;
}

bool battle_play(Battle *b, int team, int slot, int32_t x, int32_t y) {
  if (battle_check(b, team, slot, x, y)) return false;
  Side *s = &b->side[team];
  int card = s->hand[slot];
  s->elixir -= CARDS[card].cost;
  s->spent += CARDS[card].cost;
  s->played++;
  s->last_card = (uint8_t)card;
  cast(b, team, card, x, y, card_lvl_index(card));
  s->queue[s->nqueue++] = (uint8_t)card;
  s->hand[slot] = NONE;
  s->draw_t[slot] = (int16_t)draw_delay(b);
  return true;
}

static void side_step(Battle *b, int team) {
  Side *s = &b->side[team];
  int32_t max = GL(MAX_MANA), regen = battle_regen_ms(b);
  if (s->elixir < max) {
    s->elixir_t += TICK;
    while (s->elixir_t >= regen && s->elixir < max) s->elixir++, s->elixir_t -= regen;
    if (s->elixir >= max) s->elixir_t = 0;
  } else {
    s->leaked += TICK;
  }
  for (int k = 0; k < 4; k++)
    if (s->hand[k] == NONE) {
      s->draw_t[k] = (int16_t)(s->draw_t[k] - TICK);
      if (s->draw_t[k] <= 0 && s->nqueue) {
        s->hand[k] = s->queue[0];
        memmove(s->queue, s->queue + 1, --s->nqueue);
      }
    }
}

/* ------------------------------------------------------------------ a unit's step */
static int rate(const Ent *e, int base) {   /* Rage: 135%, as a percent */
  return e->rage > 0 ? base * 135 / 100 : base;
}

static void face_to(Ent *e, int32_t dx, int32_t dy) {
  if (!dx && !dy) return;
  /* octants: 0 right, 1 down-right, 2 down ... 7 up-right; 22.5 degrees is a slope of 0.414 */
  int32_t ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy;
  int o;
  if (ay * 1000 < ax * 414) o = dx > 0 ? 0 : 4;
  else if (ax * 1000 < ay * 414) o = dy > 0 ? 2 : 6;
  else o = dx > 0 ? (dy > 0 ? 1 : 7) : (dy > 0 ? 3 : 5);
  e->face = (uint8_t)o;
}

static int find_target(Battle *b, int i) {
  Ent *a = &b->e[i];
  const UnitDef *d = def(a);
  int best = -1;
  int32_t bd = 0x7FFFFFFF;
  if (is_building(a)) {
    for (int j = 0; j < MAXE; j++) {
      if (!hittable(b, a, j)) continue;
      int32_t e = edge(a, &b->e[j]);
      if (e > d->range || (d->min_range && dist(a->x, a->y, b->e[j].x, b->e[j].y) < d->min_range)) continue;
      if (e < bd) bd = e, best = j;
    }
    return best;
  }
  for (int j = 0; j < MAXE; j++) {
    if (!hittable(b, a, j)) continue;
    int32_t e = edge(a, &b->e[j]);
    if (e <= d->sight && e < bd) bd = e, best = j;
  }
  if (best >= 0 && !(d->flags & U_BUILDINGS)) return best;
  /* nothing in sight: the nearest building on the way */
  best = -1, bd = 0x7FFFFFFF;
  for (int j = 0; j < MAXE; j++) {
    if (!hittable(b, a, j) || !is_building(&b->e[j])) continue;
    int32_t l = path_len(a, b->e[j].x, b->e[j].y);
    if (l < bd) bd = l, best = j;
  }
  return best;
}

static void fire(Battle *b, int i, int ti) {
  Ent *a = &b->e[i], *t = &b->e[ti];
  const UnitDef *d = def(a);
  int32_t dmg = a->dmg;
  if (d->charge_range && (a->flags & E_CHARGING)) dmg = lv(d->dmg_special, a->lvl);
  if (d->dmg2) {   /* Inferno Tower: more the longer it burns one target */
    if (a->lock >= d->t1 + d->t2) dmg = lv(d->dmg3, a->lvl);
    else if (a->lock >= d->t1) dmg = lv(d->dmg2, a->lvl);
  }
  a->flags &= (uint8_t)~E_CHARGING;
  a->walked = 0;
  a->post = 0;
  if (d->proj != NONE) {
    for (int k = 0; k < MAXP; k++) {
      if (b->p[k].used) continue;
      Proj *p = &b->p[k];
      memset(p, 0, sizeof *p);
      p->used = 1, p->type = (uint8_t)d->proj, p->team = a->team, p->lvl = a->lvl;
      p->sx = p->x = a->x, p->sy = p->y = a->y;
      p->tx = t->x, p->ty = t->y;
      p->target = (int16_t)ti, p->tgen = t->gen;
      p->dmg = dmg;
      p->z0 = d->flags & U_FLY ? d->fly_height : d->attached_h ? d->attached_h : 600;
      if (d->flags & U_TOWER) p->z0 = 2400;
      int32_t sp = PROJS[d->proj].speed * TICK * 1000 / 60000;
      p->total = sp ? dist(a->x, a->y, t->x, t->y) / sp * TICK + TICK : TICK;
      return;
    }
    return;
  }
  if (d->area && (d->flags & U_SELFAOE)) {
    /* Valkyrie: all round her */
    splash(b, a->team, a->x, a->y, d->area, dmg, true, false, false, 0, false);
    return;
  }
  hurt(b, ti, dmg, false);
}

static void move_unit(Battle *b, Ent *e, int32_t tx, int32_t ty, int32_t step) {
  int32_t wx, wy;
  waypoint(e, tx, ty, &wx, &wy);
  int32_t d = dist(e->x, e->y, wx, wy);
  if (!d) return;
  if (step > d) step = d;
  int64_t ddx = (int64_t)(wx - e->x) * step, ddy = (int64_t)(wy - e->y) * step;
  int32_t nx = e->x + (int32_t)((ddx + (ddx < 0 ? -d / 2 : d / 2)) / d), ny = e->y + (int32_t)((ddy + (ddy < 0 ? -d / 2 : d / 2)) / d);
  bool ground = !(def(e)->flags & (U_FLY | U_JUMP));
  if (!(def(e)->flags & U_FLY)) {
    /* a building in the way (not the one it goes for): round it */
    for (int j = 0; j < MAXE; j++) {
      const Ent *o = &b->e[j];
      if (!o->used || j == e->target || !is_building(o) || (def(o)->flags & U_BOMB) || o == e) continue;
      int32_t r = def(o)->radius + def(e)->radius + 100;
      if (dist(nx, ny, o->x, o->y) >= r || dist(e->x, e->y, o->x, o->y) > r + step + 400) continue;
      int32_t vx = e->x - o->x, vy = e->y - o->y, vd = dist(0, 0, vx, vy);
      if (!vd) vx = 1, vd = 1;
      int32_t tx1 = -vy, ty1 = vx;
      if ((int64_t)tx1 * (wx - e->x) + (int64_t)ty1 * (wy - e->y) < 0) tx1 = -tx1, ty1 = -ty1;
      /* along the tangent, and a little outward if inside */
      int32_t out = vd < r ? (r - vd < step ? r - vd : step) : 0;
      nx = e->x + (int32_t)((int64_t)tx1 * step / vd) + (int32_t)((int64_t)vx * out / vd);
      ny = e->y + (int32_t)((int64_t)ty1 * step / vd) + (int32_t)((int64_t)vy * out / vd);
      wx = nx, wy = ny;
      break;
    }
  }
  face_to(e, wx - e->x, wy - e->y);
  if (ground && (water(nx, ny) || blocked(nx, ny))) {
    if (!water(nx, e->y) && !blocked(nx, e->y)) ny = e->y;
    else if (!water(e->x, ny) && !blocked(e->x, ny)) nx = e->x;
    else return;
  }
  if (!ground && blocked(nx, ny) && (nx < 0 || nx >= ARENA_X || ny < 0 || ny >= ARENA_Y)) return;
  e->walked += dist(e->x, e->y, nx, ny);
  e->x = nx, e->y = ny;
  (void)b;
}

static void unit_step(Battle *b, int i) {
  Ent *e = &b->e[i];
  const UnitDef *d = def(e);
  if (e->flash > 0) e->flash -= TICK;
  if (e->rage > 0) e->rage -= TICK;
  if (e->push_t > 0) {
    int32_t nx = e->x + e->push_x, ny = e->y + e->push_y;
    if (!blocked(nx, ny) && (ent_flying(e) || !water(nx, ny))) e->x = nx, e->y = ny;
    e->push_t--;
  }
  if (e->deploy > 0) {
    e->deploy -= TICK;
    e->anim = 2;
    if (e->deploy <= 0 && (d->flags & U_BOMB)) {
      kill(b, i);
      return;
    }
    if (e->deploy > 0) return;
    e->deploy = 0;
  }
  if (e->freeze > 0 || e->stun > 0) {
    if (e->freeze > 0) e->freeze -= TICK;
    if (e->stun > 0) e->stun -= TICK;
    e->anim = 2;
    return;
  }
  e->anim_ms += rate(e, TICK);
  e->post += TICK;
  /* buildings: their hitpoints drain over their lifetime */
  if (d->life_ms) {
    int64_t acc = (int64_t)e->maxhp * TICK + e->decay;
    int32_t loss = (int32_t)(acc / d->life_ms);
    e->decay = (int32_t)(acc % d->life_ms);
    e->hp -= loss;
    if (e->hp <= 0) {
      kill(b, i);
      return;
    }
  }
  if (d->mana_ms) {
    e->mana_t += TICK;
    if (e->mana_t >= d->mana_ms) {
      e->mana_t -= d->mana_ms;
      Side *s = &b->side[e->team];
      if (s->elixir < GL(MAX_MANA)) s->elixir += d->mana_amount;
      fx_add(b, FX_TEXT, e->team, e->x, e->y - 1500, 1, 900);
    }
  }
  if (d->spawn_unit != NONE && d->spawn_number) {
    int dt = rate(e, TICK);
    if (e->spawn_left > 0) {
      e->spawn_gap -= dt;
      if (e->spawn_gap <= 0) {
        int32_t fx = e->team ? -1 : 1, sx = e->x, sy = e->y;
        if (is_building(e)) sx += fx * (d->radius + 500), sy += (e->spawn_left & 1 ? 1 : -1) * 400;
        else sx += fx * 800, sy += (e->spawn_left - 2) * 700;
        place_near(&sx, &sy, false);
        battle_spawn(b, d->spawn_unit, e->team, e->lvl + d->spawn_level, sx, sy, 0);
        e->spawn_left--;
        e->spawn_gap = d->spawn_interval;
      }
    } else {
      e->spawn_t -= dt;
      if (e->spawn_t <= 0) {
        e->spawn_left = d->spawn_number;
        e->spawn_gap = 0;
        e->spawn_t += d->spawn_pause + d->spawn_interval * (d->spawn_number - 1);
      }
    }
  }
  /* the King tower sleeps until hit or a Princess tower falls */
  if (d->flags & U_KING) {
    if (!(e->flags & E_AWAKE)) {
      if (e->wake > 0) {
        e->wake -= TICK;
        if (e->wake <= 0) e->flags |= E_AWAKE;
      }
      if (!(e->flags & E_AWAKE)) return;
    }
  }
  bool attacks = d->range > 0 && (d->dmg || d->proj != NONE);
  if (!attacks && is_building(e)) {
    e->anim = 2;
    return;
  }
  /* the target: kept while it can be hit and is near; a unit walking to a building looks again */
  int tg = e->target;
  if (tg >= 0 && (!hittable(b, e, tg) || b->e[tg].gen != e->tgen)) tg = -1;
  if (tg >= 0) {
    int32_t ed = edge(e, &b->e[tg]);
    if (is_building(e)) {
      if (ed > d->range) tg = -1;
    } else if (!(e->flags & E_ATTACKING) && is_building(&b->e[tg]) && !(d->flags & U_BUILDINGS)) {
      tg = -1;
    } else if (!(e->flags & E_ATTACKING) && ed > d->sight + 2000 && !is_building(&b->e[tg])) {
      tg = -1;
    }
  }
  if (tg < 0) tg = find_target(b, i);
  if (tg != e->target || (tg >= 0 && b->e[tg].gen != e->tgen)) {
    e->lock = 0;
    if (e->flags & E_ATTACKING) e->t = 0;
    e->flags &= (uint8_t)~E_ATTACKING;
  }
  e->target = (int16_t)tg;
  e->tgen = tg >= 0 ? b->e[tg].gen : 0;
  /* Tesla: up while something is in range, under the ground otherwise */
  if (d->flags & U_HIDES) {
    if (tg >= 0) {
      if (e->hidden) {
        e->hide_t += TICK;
        if (e->hide_t >= d->up_ms) e->hidden = 0, e->hide_t = 0;
        return;
      }
    } else if (!e->hidden) {
      e->hide_t += TICK;
      if (e->hide_t >= d->hide_ms) e->hidden = 1, e->hide_t = 0;
    } else {
      e->hide_t = 0;
    }
  }
  int load = d->load_ms, hit = d->hit_ms;
  if (tg < 0) {
    e->flags &= (uint8_t)~E_ATTACKING;
    if (e->t < load) e->t = e->t + rate(e, TICK) < load ? e->t + rate(e, TICK) : load;
    e->anim = 2;
    return;
  }
  Ent *t = &b->e[tg];
  int32_t ed = edge(e, t);
  bool in_range = ed <= d->range && (!d->min_range || dist(e->x, e->y, t->x, t->y) >= d->min_range);
  if (in_range) {
    e->flags |= E_ATTACKING;
    e->anim = 1;
    face_to(e, t->x - e->x, t->y - e->y);
    e->lock += TICK;
    e->t += rate(e, TICK);
    if (e->t >= hit) {
      e->t = 0;
      fire(b, i, tg);
    }
    return;
  }
  e->flags &= (uint8_t)~E_ATTACKING;
  if (e->t < load) e->t = e->t + rate(e, TICK) < load ? e->t + rate(e, TICK) : load;
  if (is_building(e) || !d->speed) {
    e->anim = 2;
    return;
  }
  /* walk (the Prince charges after ChargeRange of walking) */
  int32_t step = rate(e, d->speed * TICK * 1000 / 60000);
  if (d->charge_range) {
    if (e->walked >= d->charge_range * 10) e->flags |= E_CHARGING;
    if (e->flags & E_CHARGING) step = step * d->charge_mul / 100;
  }
  /* stop where the target comes in range */
  int32_t room = ed - d->range;
  if (room + 20 < step) step = room + 20;
  move_unit(b, e, t->x, t->y, step);
  e->anim = 0;
}

/* units do not stand inside each other or the buildings */
static void collide(Battle *b) {
  for (int i = 0; i < MAXE; i++) {
    Ent *a = &b->e[i];
    if (!a->used || (def(a)->flags & U_BOMB)) continue;
    bool afly = ent_flying(a), ab = is_building(a);
    for (int j = i + 1; j < MAXE; j++) {
      Ent *c = &b->e[j];
      if (!c->used || (def(c)->flags & U_BOMB) || ent_flying(c) != afly) continue;
      bool cb = is_building(c);
      if (ab && cb) continue;
      int32_t r = def(a)->radius + def(c)->radius;
      int32_t dx = c->x - a->x, dy = c->y - a->y;
      if (dx >= r || -dx >= r || dy >= r || -dy >= r) continue;
      int32_t d = isqrt64((int64_t)dx * dx + (int64_t)dy * dy);
      if (d >= r) continue;
      if (!d) dx = (i & 1) ? 1 : -1, dy = (j & 1) ? 1 : -1, d = 1;
      int32_t over = r - d;
      int32_t ma = ab ? 0 : def(a)->mass ? def(a)->mass : 1, mc = cb ? 0 : def(c)->mass ? def(c)->mass : 1;
      /* the lighter gives way; a building not at all */
      int32_t sa = ab ? 0 : cb ? over : over * mc / (ma + mc), sc = cb ? 0 : ab ? over : over - sa;
      Ent *m[2] = {a, c};
      int32_t s[2] = {-sa, sc};
      for (int k = 0; k < 2; k++) {
        if (!s[k]) continue;
        int32_t nx = m[k]->x + (int32_t)((int64_t)dx * s[k] / d), ny = m[k]->y + (int32_t)((int64_t)dy * s[k] / d);
        if (!blocked(nx, ny) && (afly || !water(nx, ny) || (def(m[k])->flags & U_JUMP))) m[k]->x = nx, m[k]->y = ny;
      }
    }
  }
}

/* ------------------------------------------------------------------ projectiles and spells */
static void proj_step(Battle *b, int i) {
  Proj *p = &b->p[i];
  const ProjDef *d = &PROJS[p->type];
  p->t += TICK;
  if ((d->flags & P_HOMING) && !p->spell && p->target >= 0) {
    if (ent_alive(b, p->target) && b->e[p->target].gen == p->tgen) p->tx = b->e[p->target].x, p->ty = b->e[p->target].y;
    else p->target = -1;
  }
  int32_t step = d->speed * TICK * 1000 / 60000;
  int32_t dd = dist(p->x, p->y, p->tx, p->ty);
  if (dd > step) {
    p->x += (int32_t)((int64_t)(p->tx - p->x) * step / dd);
    p->y += (int32_t)((int64_t)(p->ty - p->y) * step / dd);
    return;
  }
  p->x = p->tx, p->y = p->ty;
  p->used = 0;
  if (p->deco) return;
  bool reduced = (d->flags & P_REDUCED) != 0;
  if (d->radius) {
    bool ground = (d->flags & P_AOE_GROUND) != 0, air = (d->flags & P_AOE_AIR) != 0;
    splash(b, p->team, p->x, p->y, d->radius, p->dmg, ground, air, reduced, d->push, p->spell);
    int sz = d->radius >= 2500 ? 3 : d->radius >= 1500 ? 2 : 1;
    fx_add(b, d->radius >= 1500 || p->spell ? FX_EXPLOSION : FX_SPLASH, p->team, p->x, p->y, sz, 450);
  } else if (p->target >= 0 && ent_alive(b, p->target)) {
    hurt(b, p->target, p->dmg, reduced);
  }
  if (d->spawn_unit != NONE && d->spawn_count)
    spawn_around(b, d->spawn_unit, p->team, p->lvl + d->spawn_level, p->x, p->y, d->spawn_count, 600, d->spawn_deploy);
}

static void area_step(Battle *b, int i) {
  Area *ar = &b->a[i];
  const AreaDef *a = &AREAS[ar->type];
  ar->life -= TICK;
  ar->hit_t += TICK;
  int32_t since = ar->total - ar->life;
  if (a->speed_mul || a->hit_mul) {
    bool freeze = a->speed_mul < 0;
    for (int j = 0; j < MAXE; j++) {
      Ent *t = &b->e[j];
      if (!ent_alive(b, j) || (def(t)->flags & U_BOMB)) continue;
      bool own = t->team == ar->team;
      if ((a->flags & A_OWN) && !own) continue;
      if ((a->flags & A_ENEMIES) && own) continue;
      bool fly = ent_flying(t);
      if ((fly && !(a->flags & A_AIR)) || (!fly && !(a->flags & A_GROUND))) continue;
      if (dist(ar->x, ar->y, t->x, t->y) > a->radius + def(t)->radius) continue;
      if (freeze) {
        if (since <= TICK) t->freeze = ar->life + TICK;   /* the ones in it when it falls, for its time */
      } else {
        t->rage = a->buff_ms;
      }
    }
  }
  if (a->proj != NONE && a->hit_ms && ar->hit_t >= a->hit_ms && ar->hits < 3) {
    /* Lightning: the biggest (most hitpoints) not hit yet */
    ar->hit_t -= a->hit_ms;
    int best = -1;
    int32_t bh = -1;
    for (int j = 0; j < MAXE; j++) {
      Ent *t = &b->e[j];
      if (!ent_alive(b, j) || t->team == ar->team || (def(t)->flags & U_BOMB)) continue;
      if (dist(ar->x, ar->y, t->x, t->y) > a->radius + def(t)->radius) continue;
      bool done = false;
      for (int k = 0; k < ar->hits; k++) done |= ar->hit_ids[k] == j;
      if (!done && t->hp > bh) bh = t->hp, best = j;
    }
    if (best >= 0) {
      Ent *t = &b->e[best];
      ar->hit_ids[ar->hits] = (int16_t)best;
      fx_add(b, FX_LIGHTNING, ar->team, t->x, t->y, 1, 400);
      t->t = 0, t->lock = 0, t->walked = 0, t->flags &= (uint8_t)~E_CHARGING;
      hurt(b, best, lv(PROJS[a->proj].dmg, ar->lvl), (PROJS[a->proj].flags & P_REDUCED) != 0);
    }
    ar->hits++;
  }
  if (ar->life <= 0) ar->used = 0;
}

void battle_step(Battle *b) {
  if (b->over) return;
  b->tick++;
  b->time_ms -= TICK;
  if (b->time_ms <= 0) {
    if (!b->overtime && b->side[0].crowns == b->side[1].crowns) {
      b->overtime = 1;
      b->time_ms = GL(OVERTIME_SECONDS) * 1000;
    } else {
      b->time_ms = 0;
      end_battle(b);
      return;
    }
  }
  side_step(b, 0);
  side_step(b, 1);
  bot_step(b);
  for (int i = 0; i < MAXE; i++)
    if (b->e[i].used) unit_step(b, i);
  collide(b);
  for (int i = 0; i < MAXP; i++)
    if (b->p[i].used) proj_step(b, i);
  for (int i = 0; i < MAXA; i++)
    if (b->a[i].used) area_step(b, i);
  for (int i = 0; i < MAXFX; i++)
    if (b->fx[i].used && (b->fx[i].t = (int16_t)(b->fx[i].t + TICK)) >= b->fx[i].len) b->fx[i].used = 0;
}
