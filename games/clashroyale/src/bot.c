/* The bot: NumPlay's own, not Supercell's. It plays team 1 by the same rules as you, with one of the training camp's
 * decks: it defends what crosses toward its towers with a card that can hit it, pulls building-chasers with its
 * buildings, spells clumps, and pushes a lane with a win condition when it has the elixir. Higher arenas think
 * faster and wait for bigger pushes. */
#include <stddef.h>
#include "sim.h"

bool bot_plays_both;   /* tests: a bot for you too */

static int32_t brand(Bot *bt, int32_t n) { return n > 0 ? (int32_t)(sim_rand(&bt->rng) % (uint32_t)n) : 0; }

typedef struct {
  int n, air, small, tank, chasers;
  int32_t x, y, hp;
} Threat;

/* the enemy troops heading for team's side, in a lane (-1: both) */
static void threats(const Battle *b, int team, int lane, Threat *t) {
  *t = (Threat){0};
  int64_t sx = 0, sy = 0;
  for (int i = 0; i < MAXE; i++) {
    const Ent *e = &b->e[i];
    if (!ent_alive(b, i) || e->team == team) continue;
    const UnitDef *d = &UNITS[e->type];
    if (d->flags & (U_BUILDING | U_BOMB)) continue;
    bool near = team ? e->x > 11500 : e->x < ARENA_X - 11500;
    if (!near) continue;
    if (lane >= 0 && (e->y < ARENA_Y / 2) != (lane == 0)) continue;
    t->n++;
    t->hp += e->hp;
    if (d->flags & U_FLY) t->air++;
    if (e->maxhp < 400) t->small++;
    if (e->maxhp >= 1500) t->tank++;
    if (d->flags & U_BUILDINGS) t->chasers++;
    /* the front one counts most */
    int w = team ? e->x : ARENA_X - e->x;
    sx += (int64_t)e->x * w, sy += (int64_t)e->y * w;
    t->x += w;
  }
  if (t->n) {
    int32_t w = t->x;
    t->x = (int32_t)(sx / w), t->y = (int32_t)(sy / w);
  }
}

static bool can_hit_air(int card) {
  const CardDef *c = &CARDS[card];
  if (c->kind == K_SPELL) return c->proj != NONE ? (PROJS[c->proj].flags & P_AOE_AIR) != 0 : c->area != NONE;
  return c->unit != NONE && (UNITS[c->unit].flags & U_AIR);
}

static bool splashes(int card) {
  const CardDef *c = &CARDS[card];
  if (c->unit == NONE) return false;
  const UnitDef *u = &UNITS[c->unit];
  return u->area || (u->proj != NONE && PROJS[u->proj].radius);
}

static int32_t dps(int card) {
  const CardDef *c = &CARDS[card];
  if (c->unit == NONE) return 0;
  const UnitDef *u = &UNITS[c->unit];
  int32_t dmg = u->proj != NONE ? PROJS[u->proj].dmg : u->dmg;
  if (u->dmg2) dmg = u->dmg2;
  return u->hit_ms ? lv(dmg, card_lvl_index(card)) * 1000 / u->hit_ms * c->count : 0;
}

/* the spell damage of a card at its level, and what it would hit around (x, y) */
static int32_t spell_value(const Battle *b, int team, int card, int32_t x, int32_t y, int32_t *hit_hp) {
  const CardDef *c = &CARDS[card];
  int32_t dmg = 0, r = c->radius;
  if (c->proj != NONE) dmg = lv(PROJS[c->proj].dmg, card_lvl_index(card));
  else if (c->area != NONE) {
    const AreaDef *a = &AREAS[c->area];
    dmg = a->dmg ? lv(a->dmg, card_lvl_index(card)) : a->proj != NONE ? lv(PROJS[a->proj].dmg, card_lvl_index(card)) : 0;
  }
  int32_t v = 0, hhp = 0;
  for (int i = 0; i < MAXE; i++) {
    const Ent *e = &b->e[i];
    if (!ent_alive(b, i) || e->team == team || (UNITS[e->type].flags & U_BOMB)) continue;
    if (dist(x, y, e->x, e->y) > r + UNITS[e->type].radius) continue;
    int32_t d = dmg;
    if (UNITS[e->type].flags & U_TOWER) d = d * GL(REDUCED_TOWER_DAMAGE_PERCENT) / 100;
    v += d < e->hp ? d : e->hp * 3 / 2;   /* a kill is worth more */
    hhp += e->hp;
  }
  if (hit_hp) *hit_hp = hhp;
  return v;
}

static bool play(Battle *b, int team, int slot, int32_t x, int32_t y) {
  /* there, or the nearest tile it may use */
  static const int8_t off[9][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {-1, 1}, {1, -1}, {-1, -1}};
  int card = b->side[team].hand[slot];
  for (int k = 0; k < 9; k++) {
    int32_t px = x + off[k][0] * 1000, py = y + off[k][1] * 1000;
    if (battle_can_place(b, team, card, px, py) && battle_play(b, team, slot, px, py)) return true;
  }
  return false;
}

static int32_t own_x(int team, int32_t from_king) { return team ? ARENA_X - from_king : from_king; }

static bool defend(Battle *b, int team) {
  const Side *s = &b->side[team];
  Threat t;
  for (int lane = 0; lane < 2; lane++) {
    threats(b, team, lane, &t);
    if (!t.n) continue;
    /* how close: the front one past the river is urgent */
    bool crossed = team ? t.x > RIVER_X1 - 1500 : t.x < RIVER_X0 + 1500;
    if (!crossed && t.hp < 600) continue;
    int best = -1;
    int32_t bs = 0, bx = 0, by = 0;
    for (int k = 0; k < 4; k++) {
      int card = s->hand[k];
      if (card == NONE || CARDS[card].cost > s->elixir) continue;
      const CardDef *c = &CARDS[card];
      int32_t score = 0, x = 0, y = t.y;
      if (c->kind == K_SPELL) {
        if (c->proj == NONE && c->area == NONE) continue;
        if (c->area != NONE && !AREAS[c->area].dmg && AREAS[c->area].proj == NONE) continue;   /* Rage, Freeze */
        if (c->proj != NONE && PROJS[c->proj].spawn_unit != NONE) continue;                    /* Goblin Barrel */
        int32_t lead = team ? 800 : -800;
        int32_t v = spell_value(b, team, card, t.x + lead, t.y, NULL);
        score = v * 10 / (c->cost * 120) - 6;
        x = t.x + lead;
      } else if (c->kind == K_BUILDING) {
        if (c->unit == NONE || !(UNITS[c->unit].range && (UNITS[c->unit].dmg || UNITS[c->unit].proj != NONE)) ||
            UNITS[c->unit].range > 9000)
          continue;
        if (t.air && !(UNITS[c->unit].flags & U_AIR)) continue;
        score = t.chasers ? 9 : 3;
        x = own_x(team, 8500), y = ARENA_Y / 2;
      } else {
        if (t.air && !can_hit_air(card)) continue;
        const UnitDef *u = &UNITS[c->unit];
        if (u->flags & U_BUILDINGS) continue;
        score = 4 + (int32_t)(dps(card) / 60);
        if (t.small >= 3 && splashes(card)) score += 5;
        if (t.tank && dps(card) > 250) score += 4;
        if (t.air && (u->flags & U_AIR)) score += 3;
        score -= c->cost;
        bool ranged = u->range > 3000;
        if (ranged) x = own_x(team, 5500), y = t.y < ARENA_Y / 2 ? 3500 : 14500;
        else x = team ? (t.x + 2500 > RIVER_X1 + 500 ? t.x + 2500 : RIVER_X1 + 500) : (t.x - 2500 < RIVER_X0 - 500 ? t.x - 2500 : RIVER_X0 - 500);
      }
      if (score > bs) bs = score, best = k, bx = x, by = y;
    }
    if (best >= 0 && bs > 0) {
      if (bx < 500) bx = 500;
      if (bx > ARENA_X - 500) bx = ARENA_X - 500;
      return play(b, team, best, bx / 1000 * 1000 + 500, by / 1000 * 1000 + 500);
    }
  }
  return false;
}

static bool is_win_condition(int card) {
  const CardDef *c = &CARDS[card];
  if (c->proj != NONE && PROJS[c->proj].spawn_unit != NONE) return true;   /* Goblin Barrel */
  if (c->unit == NONE) return false;
  const UnitDef *u = &UNITS[c->unit];
  return (u->flags & U_BUILDINGS) || u->hp * LEVEL_MULT[card_lvl_index(card)] / 100 >= 1500 || card == CA_XBOW ||
         card == CA_MORTAR || card == CA_BARBARIANS;
}

static bool attack(Battle *b, int team) {
  Bot *bt = &b->bot[team];
  const Side *s = &b->side[team], *o = &b->side[1 - team];
  int32_t need = 9 - bt->level / 3;
  if (bt->push_ms > 0) need = 3;   /* support the push */
  if (s->elixir < need) return false;
  int lane = bt->push_lane;
  if (lane < 0) {
    /* the weaker enemy tower */
    int32_t h0 = ent_alive(b, o->princess[0]) ? b->e[o->princess[0]].hp : 0;
    int32_t h1 = ent_alive(b, o->princess[1]) ? b->e[o->princess[1]].hp : 0;
    lane = h0 && h1 ? (h0 == h1 ? brand(bt, 2) : h0 < h1 ? 0 : 1) : (h0 ? 1 : 0);
    if (!h0 && !h1) lane = brand(bt, 2);
  }
  int32_t ly = lane ? 14500 : 3500;
  int best = -1, bs = -100;
  for (int k = 0; k < 4; k++) {
    int card = s->hand[k];
    if (card == NONE || CARDS[card].cost > s->elixir) continue;
    const CardDef *c = &CARDS[card];
    int32_t score = 0;
    if (bt->push_ms <= 0) {
      if (!is_win_condition(card)) continue;
      score = 10 + c->cost;
    } else {
      if (c->kind != K_TROOP || is_win_condition(card)) continue;
      score = 5 + (int32_t)(dps(card) / 80);
    }
    if (score > bs) bs = score, best = k;
  }
  if (best < 0) return false;
  int card = s->hand[best];
  const CardDef *c = &CARDS[card];
  int32_t x, y = ly;
  if (c->proj != NONE) {   /* the barrel, at the tower */
    int pt = o->princess[lane];
    if (!ent_alive(b, pt)) pt = o->king;
    if (!ent_alive(b, pt)) return false;
    x = b->e[pt].x, y = b->e[pt].y;
  } else if (bt->push_ms > 0) {
    /* behind the tank */
    x = own_x(team, 11000);
    for (int i = 0; i < MAXE; i++) {
      const Ent *e = &b->e[i];
      if (ent_alive(b, i) && e->team == team && (UNITS[e->type].flags & U_BUILDINGS) && (e->y < ARENA_Y / 2) == !lane) {
        x = team ? e->x + 2500 : e->x - 2500;
        y = e->y;
        break;
      }
    }
    if (team ? x < RIVER_X1 : x >= RIVER_X0) x = team ? RIVER_X1 + 500 : RIVER_X0 - 500;
  } else if (c->kind == K_BUILDING) {
    x = card == CA_XBOW || card == CA_MORTAR ? own_x(team, 13000) : own_x(team, 4500);
  } else {
    const UnitDef *u = &UNITS[c->unit];
    bool slow = u->speed <= 45;
    x = slow ? own_x(team, 2500) : own_x(team, 14500);
  }
  if (!play(b, team, best, x / 1000 * 1000 + 500, y / 1000 * 1000 + 500)) return false;
  if (bt->push_ms <= 0) bt->push_ms = 9000, bt->push_lane = lane;
  return true;
}

static bool finish_tower(Battle *b, int team) {
  const Side *s = &b->side[team], *o = &b->side[1 - team];
  for (int k = 0; k < 4; k++) {
    int card = s->hand[k];
    if (card == NONE || CARDS[card].cost > s->elixir || CARDS[card].kind != K_SPELL) continue;
    const CardDef *c = &CARDS[card];
    int32_t dmg = c->proj != NONE ? lv(PROJS[c->proj].dmg, card_lvl_index(card))
                  : c->area != NONE && AREAS[c->area].dmg ? lv(AREAS[c->area].dmg, card_lvl_index(card)) : 0;
    if (!dmg || (c->proj != NONE && PROJS[c->proj].spawn_unit != NONE)) continue;
    dmg = dmg * GL(REDUCED_TOWER_DAMAGE_PERCENT) / 100;
    int towers[3] = {o->princess[0], o->princess[1], o->king};
    for (int i = 0; i < 3; i++)
      if (ent_alive(b, towers[i]) && b->e[towers[i]].hp <= dmg)
        return battle_play(b, team, k, b->e[towers[i]].x, b->e[towers[i]].y);
  }
  return false;
}

static bool leak(Battle *b, int team) {
  const Side *s = &b->side[team];
  if (s->elixir < GL(MAX_MANA)) return false;
  int best = -1, bc = 99;
  for (int k = 0; k < 4; k++) {
    int card = s->hand[k];
    if (card == NONE || CARDS[card].kind == K_SPELL) continue;
    if (CARDS[card].cost < bc) bc = CARDS[card].cost, best = k;
  }
  if (best < 0) return false;
  int32_t y = brand(&b->bot[team], 2) ? 3500 : 14500;
  return play(b, team, best, own_x(team, 2500) / 1000 * 1000 + 500, y);
}

static void think(Battle *b, int team) {
  Bot *bt = &b->bot[team];
  if (bt->push_ms > 0) bt->push_ms -= 600;
  if (bt->push_ms <= 0) bt->push_lane = -1;
  /* now and then it misses its chance (less in higher arenas) */
  if (brand(bt, 100) < 18 - 2 * bt->level) return;
  if (finish_tower(b, team)) return;
  if (defend(b, team)) return;
  if (attack(b, team)) return;
  leak(b, team);
}

void bot_step(Battle *b) {
  for (int team = bot_plays_both ? 0 : 1; team < 2; team++) {
    Bot *bt = &b->bot[team];
    bt->think -= TICK;
    if (bt->think > 0) continue;
    bt->think = 1000 - 90 * bt->level + brand(bt, 400);
    think(b, team);
  }
}
