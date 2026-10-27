/* Waterfall, the west: from Snowdin to the bridge Undyne cuts. Monster Kid, the echo flowers, the stream's rocks,
 * Papyrus's report to Undyne, the bridge seeds, the wishing room, Undyne's spears, Sans's telescope, Onionsan, the
 * piano and its artifact, the statue in the rain, Monster Kid under the umbrella, and the bridge. (As the Undertale
 * Wiki describes them; what they say is ours, in short.) */
#include <stdio.h>
#include <string.h>
#include "battle.h"
#include "event.h"
#include "events.h"
#include "game.h"
#include "items.h"
#include "menu.h"
#include "monsters.h"
#include "save.h"
#include "snowdin.h"
#include "waterfall.h"
#include "world.h"

static void gone(Obj *o) { o->kind = OBJ_NONE; }

/* who walks about */
static Obj *obj_mk(int x, int y, int dir) {
  static const uint16_t s[4] = {SPR_MK_OW, SPR_MK_OW, SPR_MK_OW_L, SPR_MK_OW};
  return obj_npc(ACTOR_MK, s, x, y, dir, 0);
}
static Obj *obj_undyne(int x, int y) {
  static const uint16_t s[4] = {SPR_UNDYNE_OW, SPR_UNDYNE_OW, SPR_UNDYNE_OW, SPR_UNDYNE_OW};
  Obj *o = obj_npc(ACTOR_UNDYNE, s, x, y, DIR_DOWN, 0);
  if (o) o->flags &= (uint16_t)~OF_SOLID;
  return o;
}
Obj *wf_mk(int x, int y, int dir) { return obj_mk(x, y, dir); }
Obj *wf_undyne(int x, int y) { return obj_undyne(x, y); }
/* a thing to pick up: its item and the flag that says it's taken (and what's drawn of it, at (x, y)) */
static void pickup(int x, int y, int w, int h, int item, int fl, uint16_t spr) {
  if (flag(fl)) return;
  Obj *o = obj_trigger(x, y, w, h, EV_WF_ITEM, ACTOR_ITEM);
  if (o) o->flags = OF_TALK | OF_BACK, o->arg = (int16_t)item, o->arg2 = (int16_t)fl, o->spr[0] = spr;
}

/* ---------------------------------------------------------------- the bridge seeds */
/* each puzzle: where its water is (the seeds bloom on it) and where its seeds start; solved, the blooms that
 * stay */
typedef struct {
  int room;
  int16_t wx, wy, ww, wh;
  int16_t seeds[4][2];
  int16_t blooms[4][2];
  int fl;
} Seeds;
static const Seeds seed_puzzles[] = {
  {ROOM_WF_SEEDS1, 2380, 1480, 80, 120, {{2300, 1440}, {2280, 1460}, {2300, 1460}, {2300, 1480}},
   {{2380, 1500}, {2400, 1500}, {2420, 1500}, {2440, 1500}}, F_SEEDS1},
  {ROOM_WF_SEEDS2, 2820, 1236, 84, 264, {{2960, 1340}, {2960, 1380}, {3000, 1340}, {3000, 1380}},
   {{2860, 1260}, {2840, 1260}, {2820, 1260}, {2860, 1280}}, F_SEEDS2},
};
static const Seeds *seeds_here(void) {
  for (unsigned i = 0; i < sizeof seed_puzzles / sizeof seed_puzzles[0]; i++)
    if (seed_puzzles[i].room == g_room) return &seed_puzzles[i];
  return NULL;
}
/* the quiche's secret: the water under the second puzzle's floor */
#define SECRET_X 3036
#define SECRET_Y 1536
#define SECRET_W 84
#define SECRET_H 24

static Obj *bloom(int x, int y) {
  Obj *o = obj_deco(SPR_BLOOM, x, y, false);
  if (o) o->name = ACTOR_BLOOM, o->flags |= OF_FLOOR | OF_BACK, obj_set_box(o, 0, 0, 20, 20);
  return o;
}
static void seeds_setup(const Seeds *s) {
  for (int i = 0; i < 4; i++) {
    if (flag(s->fl)) {
      bloom(s->blooms[i][0], s->blooms[i][1]);
      continue;
    }
    Obj *o = obj_rock(s->seeds[i][0], s->seeds[i][1], ACTOR_SEED, 0);
    if (o) {
      o->spr[0] = SPR_SEED;
      obj_set_box(o, 2, 4, 16, 14);
    }
  }
  if (g_room == ROOM_WF_SEEDS2 && flag(F_SEEDS3))
    for (int i = 0; i < 4; i++) bloom(3040 + 20 * i, 1540);
}
/* water: in the puzzle's (or the secret's) rectangle, where there's no floor */
static bool in_water(int x, int y, int ignore) {
  const Seeds *s = seeds_here();
  if (!s) return false;
  bool in = (x >= s->wx && x < s->wx + s->ww && y >= s->wy && y < s->wy + s->wh) ||
            (g_room == ROOM_WF_SEEDS2 && x >= SECRET_X && x < SECRET_X + SECRET_W && y >= SECRET_Y && y < SECRET_Y + SECRET_H);
  return in && !world_walkable(x, y, 1, 1, ignore);
}
bool wf_seed_enters(Obj *o, int x, int y) {
  if (o->name != ACTOR_SEED) return false;
  /* the seed's box over water or floor (nothing solid in the way, but the seed itself) */
  int me = (int)(o - objs), bx = x + o->bx, by = y + o->by;
  for (int yy = by;; yy += 4) {
    if (yy > by + o->bh - 1) yy = by + o->bh - 1;
    for (int xx = bx;; xx += 4) {
      if (xx > bx + o->bw - 1) xx = bx + o->bw - 1;
      if (!in_water(xx, yy, me) && !world_walkable(xx, yy, 1, 1, me)) return false;
      if (xx == bx + o->bw - 1) break;
    }
    if (yy == by + o->bh - 1) break;
  }
  return true;
}
/* pushed along one way, a seed eases onto the 20-pixel grid the other way (a pixel a push, where it's free), so
 * it stays in line for the next push */
static void seed_align(Obj *o) {
  bool across = pl.dir == DIR_LEFT || pl.dir == DIR_RIGHT;
  int v = across ? o->y : o->x, to = (v + 10) / 20 * 20;
  if (v == to) return;
  int nx = o->x + (across ? 0 : (to > v ? 1 : -1)), ny = o->y + (across ? (to > v ? 1 : -1) : 0);
  if (world_walkable(nx + o->bx, ny + o->by, o->bw, o->bh, (int)(o - objs)) || wf_seed_enters(o, nx, ny))
    o->x = (int16_t)nx, o->y = (int16_t)ny;
}

void wf_seed_moved(Obj *o) {
  if (o->name != ACTOR_SEED) return;
  seed_align(o);
  /* mostly over the water (its edges are ragged): it blooms where its middle is, on the 20-pixel grid, and can be
   * walked on (and pushed over) */
  int me = (int)(o - objs), wet = 0;
  for (int j = 0; j < 4; j++)
    for (int i = 0; i < 4; i++) wet += in_water(o->x + o->bx + 2 + 4 * i, o->y + o->by + 1 + 4 * j, me);
  if (wet < 9) return;
  int cx = o->x + o->bx + o->bw / 2, cy = o->y + o->by + o->bh / 2;
  gone(o);
  bloom(cx / 20 * 20, cy / 20 * 20);
}
/* solved: the first, its four across the path; the second, Frisk on the far side; the secret, at the quiche */
static void seeds_watch(void) {
  if (g_room == ROOM_WF_SEEDS1 && !flag(F_SEEDS1)) {
    int across = 0;
    for (int i = 0; i < MAX_OBJS; i++)
      if (objs[i].kind != OBJ_NONE && objs[i].name == ACTOR_BLOOM && objs[i].y >= 1480 && objs[i].y <= 1520) across++;
    if (across >= 4) set_flag(F_SEEDS1, 1);
  }
  if (g_room == ROOM_WF_SEEDS2 && !flag(F_SEEDS2) && pl.x < 2800 && pl.y < 1300) set_flag(F_SEEDS2, 1);
}

/* ---------------------------------------------------------------- the stream's rocks */
#define STREAM_X0 1184
#define STREAM_X1 1336
#define STREAM_Y0 1446
#define STREAM_FALL 1622   /* Frisk's feet past it: over the falls */
static int stream_t;
static void stream_update(void) {
  if (g_room != ROOM_WF_FALLS) return;
  if (++stream_t % 34 == 0) {
    Obj *d = obj_deco(SPR_DEBRIS, STREAM_X0 + 8 + rnd_n(STREAM_X1 - STREAM_X0 - 16), STREAM_Y0, false);
    if (d) d->name = ACTOR_DEBRIS, obj_set_box(d, -8, -6, 16, 12);
  }
  int fx = pl.x + 3, fy = pl.y + 20;
  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *d = &objs[i];
    if (d->kind == OBJ_NONE || d->name != ACTOR_DEBRIS) continue;
    d->y = (int16_t)(d->y + 2);
    if (d->y > 1650) {
      gone(d);
      continue;
    }
    /* a rock carries Frisk down with it */
    if (!event_running() && fx < d->x + 8 && fx + 14 > d->x - 8 && fy < d->y + 6 && fy + 9 > d->y - 6) pl.y = (int16_t)(pl.y + 2);
  }
  if (!event_running() && pl.y + 29 > STREAM_FALL && pl.x + 10 > STREAM_X0 && pl.x + 10 < STREAM_X1)
    event_start(EV_WF_STREAM_FALL, NULL);
}

/* ---------------------------------------------------------------- Undyne's spears */
/* a spear comes up where Frisk is going: a glint, then the spear; it hurts (never below 1 HP) */
typedef struct {
  int room, from_x, to_x;   /* where the spears come */
  int fl;
} Chase;
static const Chase chases[] = {{ROOM_WF_CHASE1, 4150, 6460, F_CHASE1}, {ROOM_WF_BRIDGE, 15600, 17380, F_WF_BRIDGE}};
static int spear_t, hurt_t;
static const Chase *chase_here(void) {
  for (unsigned i = 0; i < sizeof chases / sizeof chases[0]; i++)
    if (chases[i].room == g_room && !flag(chases[i].fl)) return &chases[i];
  return NULL;
}
static void spears_update(void) {
  const Chase *ch = chase_here();
  if (!ch) return;
  Obj *u = obj_find(ACTOR_UNDYNE);
  if (pl.x < ch->from_x || pl.x > ch->to_x || event_running()) {
    if (u && g_room == ROOM_WF_CHASE1 && pl.x >= ch->from_x) u->x = (int16_t)(pl.x - 40);
    return;
  }
  /* she keeps up along the ledge above */
  if (u && g_room == ROOM_WF_CHASE1) {
    int want = pl.x - 40;
    u->x = (int16_t)(u->x + (want > u->x ? min_i(3, want - u->x) : -min_i(3, u->x - want)));
  }
  if (hurt_t) hurt_t--;
  int every = g_room == ROOM_WF_BRIDGE ? 26 : 40;
  if (++spear_t % every == 0) {
    int lead = pl.moving ? (pl.dir == DIR_RIGHT ? 30 : pl.dir == DIR_LEFT ? -30 : 0) : 0;
    Obj *s = obj_deco(SPR_SPEAR, pl.x + 10 + lead, pl.y + 34, false);
    if (s) s->name = ACTOR_SPEAR, s->arg = 0, s->alpha = 90, obj_set_box(s, -3, -36, 6, 34);
  }
  for (int i = 0; i < MAX_OBJS; i++) {
    Obj *s = &objs[i];
    if (s->kind == OBJ_NONE || s->name != ACTOR_SPEAR) continue;
    s->arg++;
    if (s->arg == 18) s->alpha = 255;   /* (the glint, then it's out) */
    if (s->arg > 40) {
      gone(s);
      continue;
    }
    if (s->arg >= 18 && !hurt_t && pl.x + 3 < s->x + 3 && pl.x + 17 > s->x - 3 && pl.y + 29 > s->y - 36 && pl.y + 20 < s->y) {
      sv.hp = (int16_t)max_i(1, sv.hp - 3);
      hurt_t = 40, g_shake = 6;
    }
  }
  if (g_room == ROOM_WF_BRIDGE && pl.x >= ch->to_x - 4) event_start(EV_WF_BRIDGE_END, NULL);
}

/* ---------------------------------------------------------------- Monster Kid, along under the umbrella */
#define TRAIL 32
static int16_t trail[TRAIL][2];
static int trail_at;
static bool mk_follows;
static void mk_update(void) {
  if (!mk_follows) return;
  Obj *mk = obj_find(ACTOR_MK);
  if (!mk) return;
  mk->flags &= (uint16_t)~OF_SOLID;   /* (walking along behind, never in the way) */
  if (pl.moving || !trail_at) {
    trail[trail_at % TRAIL][0] = pl.x, trail[trail_at % TRAIL][1] = pl.y;
    trail_at++;
  }
  if (trail_at < TRAIL) return;
  const int16_t *p = trail[(trail_at - TRAIL + 4) % TRAIL];
  int nx = p[0] + 10, ny = p[1] + 29;
  mk->dir = (uint8_t)(nx < mk->x ? DIR_LEFT : nx > mk->x ? DIR_RIGHT : mk->dir);
  mk->x = (int16_t)nx, mk->y = (int16_t)ny;
}

/* ---------------------------------------------------------------- Onionsan, along in the water */
static void onion_update(void) {
  if (g_room != ROOM_WF_ONION || flag(F_WF_ONION) != 1) return;
  Obj *o = obj_find(ACTOR_ONION);
  if (o && !event_running()) {
    int want = pl.x + 60;
    if (o->x < want) o->x = (int16_t)(o->x + 1);
  }
  if (!event_running() && pl.x > 9500) event_start(EV_WF_ONION, NULL);
}

void wf1_update(void) {
  if (world_room_info()->area != AREA_WATERFALL) return;
  seeds_watch();
  stream_update();
  spears_update();
  mk_update();
  onion_update();
}

/* ---------------------------------------------------------------- the rooms */
static void echo(int x, int y, const char *says) {
  static char buf[8][120];
  static int k;
  snprintf(buf[k], sizeof buf[k], "* (The echo flower whispers.)|* %s", says);
  obj_sign(x - 10, y - 14, 20, 40, buf[k]);
  k = (k + 1) % 8;
}

void wf1_setup(int room, int from) {
  (void)from;
  if (room < ROOM_WF1 || room > ROOM_WF_BRIDGE) return;
  mk_follows = false, trail_at = 0, spear_t = 0, hurt_t = 0;
  switch (room) {
    case ROOM_WF1:
      set_flag(F_SNOW_EXIT, 1);   /* (out of Snowdin) */
      if (!flag(F_WF_MK)) {
        obj_mk(731, 1531, DIR_LEFT);
        obj_trigger(640, 1450, 20, 100, EV_WF_MK, ACTOR_TRIGGER);
      }
      obj_sign(718, 1376, 36, 48, "* Echo flowers repeat whatever they heard last.\n* Over and over and over...");
      echo(769, 1423, "\"...is anybody listening?\"");
      obj_save(814, 1430, "* (The sound of rushing water fills you with determination.)");
      {
        /* (the booth is the map's: talking reaches it from the path below) */
        Obj *s = obj_trigger(856, 1440, 68, 30, EV_GRILLBYS, ACTOR_SANS);
        if (s) s->flags = OF_TALK;
      }
      obj_sign(1084, 1452, 20, 20, "* (Waterfall ahead.\n* Mind the rocks in the stream.)");
      {
        Obj *b = obj_trigger(1112, 1452, 22, 22, EV_BOX, ACTOR_ITEM);
        if (b) b->flags = OF_TALK | OF_SOLID;
      }
      break;
    case ROOM_WF_FALLS:
      stream_t = 0;
      pickup(1240, 1240, 22, 24, ITEM_OLD_TUTU, F_WF_TUTU, SPR_TUTU);
      break;
    case ROOM_WF_FALLS_LOW:
      echo(1450, 1700, "\"Is someone down there?\"");
      obj_sign(1250, 1652, 20, 18, "* (Something small and shiny is tucked in the planks.\n* A camera?)");
      break;
    case ROOM_WF2:
      obj_save(2035, 1485, "* (The rumbling of the falls fills you with determination.)");
      if (!flag(F_WF_UNDYNE1)) obj_trigger(1770, 1496, 40, 44, EV_WF_UNDYNE1, ACTOR_TRIGGER);
      break;
    case ROOM_WF_SEEDS1:
      obj_sign(2172, 1452, 20, 20, "* (Bridge seeds bloom when\n  they touch the water.)|* (Push them in, one after\n  another.)");
      seeds_setup(&seed_puzzles[0]);
      break;
    case ROOM_WF_SEEDS2:
      obj_sign(2840, 1540, 20, 22, "* (Ring the bell to call the seeds back.)");
      {
        Obj *b = obj_trigger(3018, 1188, 24, 26, EV_BELL, ACTOR_ITEM);
        if (b) b->flags = OF_TALK | OF_SOLID;
      }
      seeds_setup(&seed_puzzles[1]);
      break;
    case ROOM_WF_QUICHE:
      /* (reached over the secret bridge: it stays, and leads back) */
      set_flag(F_SEEDS3, 1);
      for (int i = 0; i < 4; i++) bloom(3040 + 20 * i, 1540);
      pickup(3262, 1474, 30, 34, ITEM_QUICHE, F_WF_QUICHE, 0);
      echo(3330, 1463, "\"I'll wait here for a while.\"");
      break;
    case ROOM_WF_WISH:
      obj_sign(2770, 990, 26, 16, "* (Old writing on the wall: people used to come here to wish on the \"stars\" above.)");
      echo(2690, 1050, "\"I wish I could see the real sky...\"");
      echo(2838, 1068, "\"Someday we'll all be free.\"");
      echo(2928, 1050, "\"I hope my wish comes true.\"");
      echo(2968, 1050, "\"Please, let me find a friend.\"");
      echo(3288, 1050, "\"...thank you for listening.\"");
      obj_sign(3122, 1060, 20, 26, "* (A telescope.\n* The \"stars\" are crystals on the cave's ceiling.)");
      break;
    case ROOM_WF_HIST:
      obj_sign(3090, 486, 30, 16, "* (Long ago, humans and monsters lived side by side.)");
      obj_sign(3170, 486, 30, 16, "* (Then a war broke out between them.\n* The humans won.)");
      obj_sign(3250, 486, 30, 16, "* (The monsters were sealed under the mountain by a magic barrier.)");
      obj_sign(3330, 486, 30, 16, "* (Only power as great as seven human SOULs could break it.)");
      obj_sign(3410, 486, 30, 16, "* (Still, monsters hope.\n* One day, they will see the sky again.)");
      {
        static const uint16_t w[4] = {SPR_WOSHUA_OW, SPR_WOSHUA_OW, SPR_WOSHUA_OW, SPR_WOSHUA_OW};
        Obj *o = obj_npc(ACTOR_ANY, w, 3454, 580, DIR_DOWN, EV_SIGN);
        if (o) o->text = "* (Woshua is scrubbing the boardwalk.\n* It doesn't look up.)", o->flags |= OF_ANIM, o->anim_speed = 8;
      }
      break;
    case ROOM_WF_CHASE1:
      if (!flag(F_CHASE1)) obj_undyne(4180, 470);
      break;
    case ROOM_WF_GRASS1:
      if (!flag(F_CHASE1)) obj_trigger(6640, 540, 30, 72, EV_WF_GRASS, ACTOR_TRIGGER);
      echo(7219, 503, "\"...is it over? Is it gone?\"");
      obj_save(7250, 540, "* (The crystals glitter. You're filled with determination.)");
      obj_sign(7172, 500, 26, 24, "* (A mouse hole.\n* Something squeaks inside.)");
      obj_sign(7490, 512, 24, 48, "* (The door is locked.)");
      break;
    case ROOM_WF_SANS2: {
      /* (the telescope, the Nice Cream cart, the box are the map's: talked to from the floor beside them) */
      Obj *t = obj_trigger(7843, 520, 62, 36, EV_WF_TELESCOPE, ACTOR_SANS);
      if (t) t->flags = OF_TALK | OF_SOLID;
      Obj *n = obj_trigger(7876, 96, 70, 50, EV_NICECREAM, ACTOR_ITEM);
      if (n) n->flags = OF_TALK | OF_SOLID;
      Obj *b = obj_trigger(7928, 540, 26, 22, EV_BOX, ACTOR_ITEM);
      if (b) b->flags = OF_TALK | OF_SOLID;
      pickup(8048, 118, 24, 42, ITEM_PUNCH_CARD, F_WF_PUNCH, 0);
      Obj *m = obj_sign(8074, 554, 18, 12, "* (It's gazing up at the glittering ceiling.)\n* (It doesn't notice you.)");
      if (m) m->flags |= OF_SOLID;
      break;
    }
    case ROOM_WF_MARSH_TOP:
      wf_bird_setup(room);
      break;
    case ROOM_WF_MARSH:
      pickup(7578, 1230, 30, 30, ITEM_BALLET_SHOES, F_WF_BALLET, 0);
      {
        Obj *h = obj_sign(8826, 812, 50, 26, "* (The humans were afraid of how strong a monster's SOUL might become.)");
        if (h) h->flags |= OF_SOLID;
      }
      echo(8449, 1098, "\"Splish... splash...\"");
      echo(8449, 1228, "\"Is the water getting higher?\"");
      echo(8719, 1177, "\"Hello? Hello?\"");
      break;
    case ROOM_WF_ONION:
      if (flag(F_WF_ONION) < 2) {
        static const uint16_t o[4] = {SPR_ONIONSAN, SPR_ONIONSAN, SPR_ONIONSAN, SPR_ONIONSAN};
        Obj *on = obj_npc(ACTOR_ONION, o, 8900, 650, DIR_DOWN, 0);
        if (on) on->flags = OF_BACK, on->alpha = flag(F_WF_ONION) ? 255 : 0;
        if (!flag(F_WF_ONION)) obj_trigger(8840, 656, 20, 48, EV_WF_ONION, ACTOR_TRIGGER);
      }
      break;
    case ROOM_WF_PIANO: {
      Obj *p = obj_trigger(10052, 404, 44, 26, EV_PIANO, ACTOR_ITEM);
      if (p) p->flags = OF_TALK | OF_SOLID;
      obj_sign(10194, 374, 30, 16, "* (A sign: \"Play the song of the rain.\")");
      if (!flag(F_PIANO)) {
        Obj *d = obj_trigger(10118, 330, 64, 50, 0, ACTOR_BLOCK);
        if (d) d->flags = OF_SOLID;
      }
      pickup(10156, 150, 12, 30, ITEM_ARTIFACT, F_WF_ARTIFACT, SPR_ARTIFACT);
      obj_sign(10194, 572, 30, 16, "* (Monsters fear humans... yet some wish to meet one.)");
      obj_sign(10374, 610, 30, 22, "* (The barrier can't be crossed by anything with a SOUL... except a human.)");
      obj_sign(10484, 610, 30, 22, "* (A human SOUL and a monster SOUL together: that would be enough.)");
      if (!flag(F_SHYREN)) {
        static const uint16_t s[4] = {SPR_SHYREN_OW, SPR_SHYREN_OW, SPR_SHYREN_OW, SPR_SHYREN_OW};
        obj_npc(ACTOR_SHYREN, s, 10147, 660, DIR_DOWN, EV_SHYREN);
      }
      break;
    }
    case ROOM_WF_STATUE: {
      Obj *st = obj_trigger(10744, 618, 52, 42, EV_STATUE, ACTOR_ITEM);
      if (st) st->flags = OF_TALK | OF_SOLID;
      obj_sign(11050, 636, 22, 26, "* (Umbrellas: take one, leave one.)");
      Obj *u = obj_trigger(11146, 630, 28, 30, EV_UMBRELLA, ACTOR_ITEM);
      if (u) u->flags = OF_TALK | OF_SOLID;
      break;
    }
    case ROOM_WF_RAIN1:
      if (!flag(F_MK_RAIN)) {
        obj_mk(11911, 648, DIR_DOWN);
        obj_trigger(11880, 660, 60, 80, EV_MK_RAIN, ACTOR_TRIGGER);
      }
      break;
    case ROOM_WF_RAIN2: case ROOM_WF_CASTLE:
      if (flag(F_MK_RAIN) == 1) {
        obj_mk(pl.x + 10, pl.y + 29, DIR_DOWN);
        mk_follows = true;
        if (room == ROOM_WF_RAIN2) obj_trigger(12800, 1300, 60, 20, EV_MK_TALK, ACTOR_TRIGGER2);
        else obj_trigger(13600, 1700, 20, 80, EV_MK_TALK, ACTOR_TRIGGER3);
      }
      break;
    case ROOM_WF_AARON:
      if (flag(F_MK_RAIN) == 1) {
        obj_mk(pl.x + 10, pl.y + 29, DIR_DOWN);
        mk_follows = true;
        obj_trigger(14700, 1660, 20, 70, EV_MK_TALK, ACTOR_TRIGGER);
      }
      {
        static const uint16_t a[4] = {SPR_AARON_OW, SPR_AARON_OW, SPR_AARON_OW, SPR_AARON_OW};
        Obj *o = obj_npc(ACTOR_ANY, a, 14504, 1682, DIR_DOWN, EV_SIGN);
        if (o) o->text = "* Hey there, small fry ;)\n* Want to see me flex? ;)";
      }
      obj_save(15140, 1640, "* (The bridge ahead looks long.\n* You're filled with determination.)");
      obj_sign(15070, 1590, 30, 16, "* (A human and a monster may cross the barrier... but none ever has.)");
      obj_sign(15210, 1590, 30, 16, "* (The king vowed to break the barrier, and set us all free.)");
      break;
    case ROOM_WF_BRIDGE:
      if (!flag(F_WF_BRIDGE)) obj_undyne(15500, 1820);
      break;
  }
}

/* ---------------------------------------------------------------- the events */
static int ev_wf_mk(Ev *c) {
  Obj *mk = obj_get(ACTOR_MK);
  EV_BEGIN;
  pl.dir = DIR_RIGHT;
  NARRATE("* Yo! Are you going to see Undyne too?|* She's the head of the Royal Guard!\n* She's SO cool!|* Come on, let's go!");
  set_flag(F_WF_MK, 1);
  LEAVE(mk, 4, 1180, 1496);
  if (c->self) gone(c->self);
  EV_END;
}

static int ev_wf_item(Ev *c) {
  int item = c->self ? c->self->arg : 0, fl = c->self ? c->self->arg2 : 0;
  static char buf[120];
  EV_BEGIN;
  if (!item_add(item)) {
    NARRATE("* (You're carrying too much.)");
    return 1;
  }
  set_flag(fl, 1);
  snprintf(buf, sizeof buf, "* (You got the %s.)", item_info(item)->name);
  NARRATE(buf);
  if (c->self) gone(c->self);
  EV_END;
}

static int ev_stream_fall(Ev *c) {
  EV_BEGIN;
  NARRATE("* (The current sweeps you over the edge!)");
  FADE_TO(ROOM_WF_FALLS_LOW, 1252, 1640, DIR_DOWN);
  EV_END;
}

static int ev_wf_undyne1(Ev *c) {
  Obj *p = obj_get(ACTOR_PAPYRUS), *u = obj_get(ACTOR_UNDYNE), *mk = obj_get(ACTOR_MK);
  EV_BEGIN;
  /* hidden in the tall grass, Frisk hears Papyrus report to Undyne on the ledge above */
  PL_WALK(1820, 1500, 2);
  p = obj_papyrus(1770, 1396, DIR_RIGHT);
  obj_undyne(1822, 1396);
  WAIT_UNTIL(cam_pan(1640, 1300, 2));
  WAIT(50);
  SAY(FACE_PAPYRUS, "`0* H-HI, UNDYNE!\n* I'M HERE WITH MY REPORT!|* THAT HUMAN I TOLD YOU ABOUT...\n* I FOUGHT THEM...|`4* ...AND I, UM, DIDN'T WIN.|`3* WHAT? YOU'LL CAPTURE THEM YOURSELF?|`4* BUT, UNDYNE...|* ...|`0* ...OKAY. I'LL HELP YOU.");
  WALK(p, 1680, 1396, 2);
  gone(p);
  /* she looks down into the grass, right at Frisk */
  WAIT(40);
  g_shake = 4;
  NARRATE("* (Undyne stares into the grass...)");
  WAIT(40);
  u = obj_get(ACTOR_UNDYNE);
  WALK(u, 1900, 1396, 1);
  gone(u);
  cam_free();
  if (flag(F_WF_MK)) {
    obj_mk(1840, 1530, DIR_LEFT);
    NARRATE("* Yo! Did you see that?!\n* She was looking right at you!|* That's so awesome!!");
    mk = obj_get(ACTOR_MK);
    LEAVE(mk, 4, 2120, 1520);
  }
  set_flag(F_WF_UNDYNE1, 1);
  if (c->self) gone(c->self);
  EV_END;
}

static int ev_bell(Ev *c) {
  EV_BEGIN;
  NARRATE("* (Ding!)\n* (The seeds float back where they came from.)");
  for (int i = 0; i < MAX_OBJS; i++)
    if (objs[i].kind != OBJ_NONE && (objs[i].name == ACTOR_SEED || objs[i].name == ACTOR_BLOOM)) gone(&objs[i]);
  set_flag(F_SEEDS2, 0);
  seeds_setup(&seed_puzzles[1]);
  EV_END;
}

static int ev_wf_grass(Ev *c) {
  Obj *u = obj_get(ACTOR_UNDYNE), *mk = obj_get(ACTOR_MK);
  EV_BEGIN;
  /* the end of the first chase: Frisk ducks into the grass; Undyne reaches in... and pulls out Monster Kid */
  PL_WALK(6700, 560, 3);
  pl.hidden = true;
  u = obj_undyne(6580, 520);
  WALK(u, 6690, 540, 2);
  WAIT(30);
  g_shake = 6;
  NARRATE("* (Undyne reaches into the grass...)");
  if (flag(F_WF_MK)) {
    obj_mk(6700, 540, DIR_DOWN);
    WAIT(30);
    NARRATE("* (...and pulls out Monster Kid.)");
    WAIT(20);
    NARRATE("* (She drops them, and walks away.)");
  }
  u = obj_get(ACTOR_UNDYNE);
  WALK(u, 6560, 520, 2);
  gone(u);
  pl.hidden = false;
  if (flag(F_WF_MK)) {
    NARRATE("* Yo! She almost got you!\n* ...wait, why WAS she after you?|* Never mind! Let's keep going!");
    mk = obj_get(ACTOR_MK);
    LEAVE(mk, 4, 6900, 580);
  }
  set_flag(F_CHASE1, 1);
  if (c->self) gone(c->self);
  EV_END;
}

static int ev_wf_telescope(Ev *c) {
  EV_BEGIN;
  if (!flag(F_WF_TELESCOPE)) {
    SAY(FACE_SANS, "* hey. want to look through\n  my telescope? it's free.@Sure@No thanks");
    if (CHOICE() == 1) {
      SAY(FACE_SANS, "* suit yourself.");
      return 1;
    }
    NARRATE("* (You look through the telescope.)\n* (It's very dark...)");
    set_flag(F_WF_TELESCOPE, 1);
    SAY(FACE_SANS, "`1* heh. you've got a red ring around your eye now.");
  } else {
    SAY(FACE_SANS, "* how was the view?");
  }
  EV_END;
}

/* the small bird carries Frisk over the gap: east from the boardwalk (arg 2), west from the Quiet Area's side (1) */
static int ev_wf_bird(Ev *c) {
  EV_BEGIN;
  c->v[1] = c->self ? c->self->arg : 1;
  NARRATE("* (The little bird offers to carry you over the gap.)@Yes@No");
  if (CHOICE() == 0) {
    set_flag(F_WF_BIRD, c->v[1]);
    if (c->v[1] == 1) FADE_TO(ROOM_WF_MARSH_TOP, 8440, 562, DIR_LEFT);
    else FADE_TO(ROOM_WF_BIRD2, 18548, 2318, DIR_RIGHT);
  }
  EV_END;
}

static int ev_wf_onion(Ev *c) {
  Obj *o = obj_get(ACTOR_ONION);
  EV_BEGIN;
  if (!flag(F_WF_ONION)) {
    /* up from the water, a huge friendly face */
    o->x = (int16_t)(pl.x + 60);
    for (c->v[0] = 0; c->v[0] < 16; c->v[0]++) {
      o->alpha = (uint8_t)(c->v[0] * 16);
      WAIT(2);
    }
    o->alpha = 255;
    NARRATE("* Hey there... you're new around here, huh?|* I'm Onionsan!\n* I live in this big, big water.|* Let's walk together a while!");
    set_flag(F_WF_ONION, 1);
    if (c->self) gone(c->self);
    return 1;
  }
  NARRATE("* Someday they'll drain all this water...|* Then I'll live somewhere else.\n* Somewhere nice!|* See you!");
  for (c->v[0] = 16; c->v[0] > 0; c->v[0]--) {
    o->alpha = (uint8_t)(c->v[0] * 15);
    WAIT(2);
  }
  gone(o);
  set_flag(F_WF_ONION, 2);
  EV_END;
}

/* the piano: the rain's song (on the statue's sign: middle, up, right, middle, down, down, right) */
static const char *const notes[] = {"Up", "Down", "Left", "Right", "Middle", "Stop"};
static const uint8_t song[7] = {4, 0, 3, 4, 1, 1, 3};
static uint8_t played[7];
static int ev_piano(Ev *c) {
  EV_BEGIN;
  if (flag(F_PIANO)) {
    NARRATE("* (The piano rings softly.)");
    return 1;
  }
  NARRATE("* (Play the piano?)@Yes@No");
  if (CHOICE() == 1) return 1;
  memset(played, 0xFF, sizeof played);
  for (;;) {
    CHOOSE(notes, 6);
    if (CHOSEN() < 0 || CHOSEN() == 5) break;
    memmove(played, played + 1, 6);
    played[6] = (uint8_t)CHOSEN();
    if (!memcmp(played, song, 7)) {
      set_flag(F_PIANO, 1);
      for (int i = 0; i < MAX_OBJS; i++)
        if (objs[i].kind != OBJ_NONE && objs[i].name == ACTOR_BLOCK) gone(&objs[i]);
      g_shake = 6;
      NARRATE("* (Somewhere, a door rumbles open.)");
      break;
    }
  }
  EV_END;
}

static int ev_shyren(Ev *c) {
  EV_BEGIN;
  NARRATE("* (She's humming to herself... she hasn't noticed you.)");
  BATTLE(FORM_SHYREN);
  set_flag(F_SHYREN, bt.mon[0].killed ? 2 : 1);
  gone(obj_get(ACTOR_SHYREN));
  EV_END;
}

/* the umbrella (F_UMBRELLA): 1 carried, 2 left with the statue, 3 left there and another carried */
static bool carrying_umbrella(void) { return flag(F_UMBRELLA) == 1 || flag(F_UMBRELLA) == 3; }

static int ev_statue(Ev *c) {
  EV_BEGIN;
  if (flag(F_UMBRELLA) >= 2) {
    NARRATE("* (The music box plays a gentle song under the umbrella.)");
  } else if (flag(F_UMBRELLA) == 1) {
    NARRATE("* (A statue, out in the rain.)\n* (Give it your umbrella?)@Yes@No");
    if (CHOICE() == 0) {
      set_flag(F_UMBRELLA, 2);
      NARRATE("* (You hold the umbrella over the statue.)|* (A music box inside starts to play.)");
    }
  } else {
    NARRATE("* (A statue, out in the rain.)\n* (It looks lonely.)");
  }
  EV_END;
}

static int ev_umbrella(Ev *c) {
  EV_BEGIN;
  if (carrying_umbrella()) {
    NARRATE("* (Umbrellas.)");
  } else {
    set_flag(F_UMBRELLA, flag(F_UMBRELLA) ? 3 : 1);
    NARRATE("* (You took an umbrella.)");
  }
  EV_END;
}

static int ev_mk_rain(Ev *c) {
  EV_BEGIN;
  if (!carrying_umbrella()) {
    NARRATE("* Yo... it's really pouring.|* If only I had an umbrella...");
    PL_WALK(pl.x - 20, pl.y, 2);
    return 1;
  }
  NARRATE("* Yo! An umbrella!|* Can I walk under it with you? I don't have arms to hold one.|* Thanks, dude!");
  set_flag(F_MK_RAIN, 1);
  mk_follows = true, trail_at = 0;
  if (c->self) gone(c->self);
  EV_END;
}

/* what Monster Kid says on the way: down the slope, the castle, the parting */
static int ev_mk_talk(Ev *c) {
  int k = c->self ? c->self->name : 0;
  EV_BEGIN;
  if (k == ACTOR_TRIGGER2) {
    NARRATE("* Undyne's so strong... she beats up the bad guys and never loses!|* Someday I'll be just like her.");
  } else if (k == ACTOR_TRIGGER3) {
    NARRATE("* Whoa, look!\n* That's the king's castle, way out there!");
  } else {
    NARRATE("* Yo, this is where I live.\n* I gotta go home.|* See you later, dude!");
    set_flag(F_MK_RAIN, 2);
    mk_follows = false;
    {
      Obj *mk = obj_get(ACTOR_MK);
      WALK_TO(mk, 14300, 1700, 4);
    }
    WAIT(40);
    gone(obj_get(ACTOR_MK));
  }
  if (c->self) gone(c->self);
  EV_END;
}

static int ev_bridge_end(Ev *c) {
  EV_BEGIN;
  obj_undyne(pl.x - 60, pl.y + 30);
  NARRATE("* (Undyne raises her spear...)");
  g_shake = 12;
  WAIT(30);
  NARRATE("* (...and cuts the bridge from under you!)");
  set_flag(F_WF_BRIDGE, 1);
  /* down, into the dark: the garbage dump */
  world_fade_hold(ROOM_WF_DUMP, 17680, 3214, DIR_DOWN, 60);
  WAIT_UNTIL(!world_fading());
  EV_END;
}

int wf1_event(Ev *c) {
  switch (c->id) {
    case EV_WF_MK: return ev_wf_mk(c);
    case EV_WF_ITEM: return ev_wf_item(c);
    case EV_WF_STREAM_FALL: return ev_stream_fall(c);
    case EV_WF_UNDYNE1: return ev_wf_undyne1(c);
    case EV_BELL: return ev_bell(c);
    case EV_WF_GRASS: return ev_wf_grass(c);
    case EV_WF_TELESCOPE: return ev_wf_telescope(c);
    case EV_WF_BIRD: return ev_wf_bird(c);
    case EV_WF_ONION: return ev_wf_onion(c);
    case EV_PIANO: return ev_piano(c);
    case EV_SHYREN: return ev_shyren(c);
    case EV_STATUE: return ev_statue(c);
    case EV_UMBRELLA: return ev_umbrella(c);
    case EV_MK_RAIN: return ev_mk_rain(c);
    case EV_MK_TALK: return ev_mk_talk(c);
    case EV_WF_BRIDGE_END: return ev_bridge_end(c);
  }
  return -1;
}

void wf_battle_done(int form) {
  (void)form;
  for (int i = 0; i < bt.nmon; i++)
    if (bt.mon[i].killed) set_flag(F_KILLS_WATERFALL, flag(F_KILLS_WATERFALL) + 1);
}
