#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Light World (rooms.h): Kris's house, the town, the school, the supply closet. Each cutscene is the
 * original's event object (obj_krisroom...) as a protothread, its timings its alarms'. */
#include <string.h>
#include "event.h"
#include "plat.h"
#include "game.h"
#include "rooms.h"
#include "text.h"
#include "world.h"

enum { EV_KRISROOM = EV_LW_FIRST };

/* a thing showing a sprite (scr_marker), image_speed in 1/16 frames */
static Obj *marker(uint16_t spr, int x, int y, int ispd, int name) {
  Obj *o = obj_new(spr, x, y);
  o->ispd = ispd, o->name = (uint8_t)name;
  return o;
}

void lw_setup(int room) {
  switch (room) {
    case room_krisroom: {
      Obj *w = marker(spr_housewindow, 140, 40, 0, ACT_PROP);
      w->img = plot() > 0 ? FX : 0;
      Obj *b = marker(spr_bedsheet, 232, 100, 0, ACT_PROP2);
      b->img = plot() > 0 ? FX : 0;
      if (plot() == 0) {
        marker(spr_toriel_rt, 151, 97, FX / 4, ACT_TORIEL);
        pl.dir = DIR_LEFT;
        event_start(EV_KRISROOM, NULL);
      }
      break;
    }
    default: break;
  }
}

void lw_step(void) {}

/* obj_krisroom: Toriel wakes Kris up */
static int ev_krisroom(Ev *c) {
  Obj *t = obj_get(ACT_TORIEL);
  EV_BEGIN;
  TALKF(FC_TORIEL, 1, obj_krisroom_slash_Create_0_gml_21_0, obj_krisroom_slash_Create_0_gml_22_0);
  t->spr = spr_toriel_u, t->img = 0, t->ispd = FX / 4, t->vs = -2 * FX;
  WAIT(20);
  t->img = 0, t->ispd = 0, t->vs = 0;
  WAIT(10);
  t->spr = spr_toriel_windowopen, t->ispd = FX / 4;
  obj_get(ACT_PROP)->img = FX;
  WAIT(10);
  t->ispd = 0;
  WAIT(20);
  t->spr = spr_toriel_d, t->img = 0, t->ispd = FX / 4, t->vs = 2 * FX;
  WAIT(20);
  t->img = 0, t->ispd = 0, t->vs = 0;
  WAIT(20);
  t->spr = spr_toriel_rt;
  WAIT(20);
  t->ispd = FX / 4;
  TALKF(FC_TORIEL, 0, obj_krisroom_slash_Step_0_gml_92_0);
  t->img = 0, t->spr = spr_toriel_d, t->ispd = FX / 4, t->vs = 3 * FX;
  WAIT(90);
  obj_free(t);
  obj_get(ACT_PROP2)->img = FX;
  WAIT(40);
  /* Kris gets up and walks out of bed, to the left (faster with X held) */
  c->v[0] = (g_held & K_X) ? 4 : 1;
  c->v[1] = (g_held & K_X) ? 10 : 40;
  pl.spr = spr_krisl;
  for (c->v[2] = 0; c->v[2] < c->v[1]; c->v[2]++) {
    pl.x -= c->v[0];
    pl.img = (c->v[2] / 4) % 4;
    WAIT(1);
  }
  pl.spr = 0, pl.img = 0, pl.dir = DIR_LEFT;
  WAIT((g_held & K_X) ? 5 : 20);
  pl.dir = DIR_DOWN;
  if (plot() < 1) set_plot(1);
  EV_END;
}

int lw_event(Ev *c) {
  switch (c->id) {
    case EV_KRISROOM: return ev_krisroom(c);
    default: return 1;
  }
}
