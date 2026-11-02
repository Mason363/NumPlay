#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Light World (rooms.h): Kris's house, the drive to school, Alphys's class, Susie in the hall, the
 * supply closet. Each cutscene is the original's event object (obj_krisroom, obj_carcutscene,
 * obj_schoollobbycutscene, obj_classscene, obj_darkdoor, obj_insideclosetcutscene) as a protothread, its
 * texts its own, its timings its alarms'. */
#include <string.h>
#include "event.h"
#include "game.h"
#include "plat.h"
#include "rooms.h"
#include "text.h"
#include "world.h"

enum { EV_KRISROOM = EV_LW_FIRST, EV_CAR, EV_LOBBY_TORIEL, EV_CLASS, EV_LOBBY_SUSIE, EV_DARKDOOR, EV_CLOSET };

/* a thing showing a sprite (scr_marker), image_speed in 1/16 frames */
static Obj *marker(uint16_t spr, int x, int y, int ispd, int name) {
  Obj *o = obj_new(spr, x, y);
  o->ispd = ispd, o->name = (uint8_t)name;
  return o;
}
static void halt(Obj *o) { o->hs = o->vs = 0, o->ispd = 0, o->img = 0; }
static void go(Obj *o, uint16_t spr, int hs16, int vs16) {
  o->spr = spr, o->hs = hs16, o->vs = vs16, o->ispd = FX / 5;
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
        room_queue_event(EV_KRISROOM);
      }
      break;
    }
    case room_town_krisyard:
      if (plot() == 1) {
        marker(spr_toriel_lt, 195, 240, 0, ACT_TORIEL);
        marker(spr_torcar_d, 131, 240, 0, ACT_PROP);
        room_queue_event(EV_CAR);
      }
      break;
    case room_schoollobby:
      if (plot() < 2) {
        marker(spr_toriel_handhold_u, 460, 360, 0, ACT_TORIEL);
        pl.hidden = true;
        room_queue_event(EV_LOBBY_TORIEL);
      } else if (plot() == 3) {
        marker(spr_susie_eatchalk, 240, 192, FX / 4, ACT_SUSIE);
        room_queue_event(EV_LOBBY_SUSIE);
      }
      break;
    case room_alphysclass:
      if (plot() < 3) {
        marker(spr_classdoor, 236, 26, 0, ACT_DOOR);
        marker(spr_alphysd, 50, 50, 0, ACT_ALPHYS);
        pl.x = 240, pl.y = 30, pl.dir = DIR_DOWN;
        room_queue_event(EV_CLASS);
      }
      break;
    case room_schooldoor:
      if (plot() == 4) {
        marker(spr_susieu, 310, 190, 0, ACT_SUSIE);
        pl.dir = DIR_UP;
        room_queue_event(EV_DARKDOOR);
      }
      break;
    case room_insidecloset:
      if (plot() < 6) {
        marker(spr_susiel_plain, 120, 92, 0, ACT_SUSIE);
        pl.dir = DIR_RIGHT;
        room_queue_event(EV_CLOSET);
      }
      break;
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

/* obj_carcutscene: Toriel is waiting by the car; off to school */
static int ev_car(Ev *c) {
  Obj *t = obj_get(ACT_TORIEL), *car = obj_get(ACT_PROP);
  EV_BEGIN;
  pl.frozen = true;
  WAIT(30);
  t->spr = spr_toriel_u;
  text_side(0);
  TALKF(FC_TORIEL, 2, obj_carcutscene_slash_Step_0_gml_17_0);
  t->spr = spr_toriel_l;
  KRIS_WALK(car->x / FX + 10, car->y / FX - 30, 2);
  pl.hidden = true;
  t->flags |= OF_HIDDEN;
  WAIT(30);
  car->spr = spr_torcar_r, car->ispd = FX / 4, car->hs = 2 * FX;
  WAIT(60);
  pl.frozen = false;
  pl.hidden = false;
  FADE_TO(room_schoollobby, 1);
  EV_END;
}

/* obj_schoollobbycutscene, the morning: Toriel walks Kris to class */
static int ev_lobby_toriel(Ev *c) {
  Obj *t = obj_get(ACT_TORIEL);
  EV_BEGIN;
  pl.frozen = true;
  t->ispd = FX / 5, t->vs = -FX;
  WAIT(60);
  halt(t);
  t->spr = spr_toriel_hug, t->ispd = FX / 6;
  WAIT(60);
  t->spr = spr_toriel_d, t->img = 0, t->ispd = 0;
  pl.x = t->x / FX + 4, pl.y = t->y / FX + 21, pl.hidden = false, pl.dir = DIR_UP;
  WAIT(30);
  go(t, spr_toriel_d, 0, 2 * FX);
  WAIT(60);
  obj_free(t);
  KRIS_WALK(pl.x, 40, 3);
  pl.frozen = false;
  FADE_TO(room_alphysclass, 1);
  EV_END;
}

/* obj_classscene: late for Alphys's class, partnered with Susie, sent for chalk */
static int ev_class(Ev *c) {
  Obj *a = obj_get(ACT_ALPHYS);
  EV_BEGIN;
  pl.frozen = true;
  TALKF(FC_ALPHYS, 0, obj_classscene_slash_Step_0_gml_14_0);
  a->spr = spr_alphysr;
  KRIS_WALK(240, 60, 2);
  TALKF(FC_ALPHYS, 0, obj_classscene_slash_Step_0_gml_60_0, obj_classscene_slash_Step_0_gml_61_0,
        obj_classscene_slash_Step_0_gml_62_0);
  set_plot(2);
  WAIT(30);
  TALKF(FC_BERDLY, 3, obj_classscene_slash_Step_0_gml_107_0, obj_classscene_slash_Step_0_gml_108_0,
        obj_classscene_slash_Step_0_gml_109_0, obj_classscene_slash_Step_0_gml_110_0,
        obj_classscene_slash_Step_0_gml_111_0, obj_classscene_slash_Step_0_gml_112_0);
  /* the door slams: Susie */
  obj_get(ACT_DOOR)->spr = spr_classdoorslam;
  marker(spr_susied_plain, 237, 24, 0, ACT_SUSIE);
  a->spr = spr_alphysr_shock;
  WAIT(30);
  TALKF(FC_ALPHYS, 2, obj_classscene_slash_Step_0_gml_190_0);
  {
    static const char *m[9];
    m[0] = str(obj_classscene_slash_Step_0_gml_203_0), m[1] = str(obj_classscene_slash_Step_0_gml_204_0);
    m[2] = str(obj_classscene_slash_Step_0_gml_205_0), m[3] = str(obj_classscene_slash_Step_0_gml_206_0);
    m[4] = str(obj_classscene_slash_Step_0_gml_207_0), m[5] = "\\F0\\E0\\FS%";
    m[6] = str(obj_classscene_slash_Step_0_gml_209_0), m[7] = str(obj_classscene_slash_Step_0_gml_210_0);
    text_clear();
    text_face(FC_SUSIE, 0);
    for (int i = 0; i < 8; i++) text_add(m[i]);
    text_open();
  }
  WAIT_UNTIL(!text_active());
  WALK(obj_get(ACT_SUSIE), 237, 80, 2);
  TALKF(FC_ALPHYS, 6, obj_classscene_slash_Step_0_gml_230_0, obj_classscene_slash_Step_0_gml_231_0);
  a->spr = spr_alphysu;
  WAIT(40);
  TALKF(FC_ALPHYS, 8, obj_classscene_slash_Step_0_gml_294_0, obj_classscene_slash_Step_0_gml_295_0,
        obj_classscene_slash_Step_0_gml_296_0, obj_classscene_slash_Step_0_gml_298_0,
        obj_classscene_slash_Step_0_gml_299_0, obj_classscene_slash_Step_0_gml_300_0,
        obj_classscene_slash_Step_0_gml_301_0, obj_classscene_slash_Step_0_gml_302_0,
        obj_classscene_slash_Step_0_gml_303_0);
  WALK(obj_get(ACT_SUSIE), 237, 24, 3);
  obj_free(obj_get(ACT_SUSIE));
  TALKF(FC_ALPHYS, 4, obj_classscene_slash_Step_0_gml_394_0, obj_classscene_slash_Step_0_gml_395_0,
        obj_classscene_slash_Step_0_gml_396_0);
  set_plot(3);
  KRIS_WALK(240, 40, 3);
  pl.frozen = false;
  FADE_TO(room_schoollobby, 2);
  EV_END;
}

/* obj_schoollobbycutscene, after class: Susie and the chalk */
static int ev_lobby_susie(Ev *c) {
  Obj *s = obj_get(ACT_SUSIE);
  EV_BEGIN;
  WAIT_UNTIL(pl.y < 260);
  pl.frozen = true;
  s->spr = spr_susier, s->ispd = 0;
  TALKF(FC_SUSIE, 0, obj_schoollobbycutscene_slash_Step_0_gml_193_0, obj_schoollobbycutscene_slash_Step_0_gml_194_0);
  s->spr = spr_susieu;
  WAIT(30);
  s->spr = spr_susier;
  TALKF(FC_SUSIE, 0, obj_schoollobbycutscene_slash_Step_0_gml_209_0, obj_schoollobbycutscene_slash_Step_0_gml_210_0);
  s->spr = spr_susier_lookup;
  TALKF(FC_SUSIE, 0, obj_schoollobbycutscene_slash_Step_0_gml_225_0, obj_schoollobbycutscene_slash_Step_0_gml_226_0);
  s->spr = spr_susied_plain;
  WAIT(30);
  TALKF(FC_SUSIE, 1, obj_schoollobbycutscene_slash_Step_0_gml_254_0);
  WALK(s, pl.x - 24, pl.y - 6, 2);
  TALKF(FC_SUSIE, 1, obj_schoollobbycutscene_slash_Step_0_gml_303_0);
  s->spr = spr_susie_krisshoulder, s->img = 0, s->ispd = FX / 4;
  pl.hidden = true;
  TALKF(FC_SUSIE, 1, obj_schoollobbycutscene_slash_Step_0_gml_330_0);
  s->spr = spr_susie_krisgrab, s->img = 0, s->ispd = FX / 4;
  WAIT(20);
  TALKF(FC_SUSIE, 2, obj_schoollobbycutscene_slash_Step_0_gml_374_0);
  TALKF(FC_SUSIE, 2, obj_schoollobbycutscene_slash_Step_0_gml_389_0, obj_schoollobbycutscene_slash_Step_0_gml_390_0,
        obj_schoollobbycutscene_slash_Step_0_gml_391_0, obj_schoollobbycutscene_slash_Step_0_gml_392_0,
        obj_schoollobbycutscene_slash_Step_0_gml_393_0, obj_schoollobbycutscene_slash_Step_0_gml_394_0,
        obj_schoollobbycutscene_slash_Step_0_gml_395_0, obj_schoollobbycutscene_slash_Step_0_gml_396_0,
        obj_schoollobbycutscene_slash_Step_0_gml_397_0, obj_schoollobbycutscene_slash_Step_0_gml_398_0,
        obj_schoollobbycutscene_slash_Step_0_gml_399_0, obj_schoollobbycutscene_slash_Step_0_gml_400_0,
        obj_schoollobbycutscene_slash_Step_0_gml_401_0, obj_schoollobbycutscene_slash_Step_0_gml_402_0);
  s->spr = spr_susie_grabsmile, s->img = 0, s->ispd = FX / 4;
  TALKF(FC_SUSIE, 3, obj_schoollobbycutscene_slash_Step_0_gml_432_0);
  WAIT(40);
  TALKF(FC_SUSIE, 0, obj_schoollobbycutscene_slash_Step_0_gml_499_0);
  s->spr = spr_susie_krisdrop, s->img = 0, s->ispd = FX / 4;
  WAIT(16);
  pl.hidden = false, pl.dir = DIR_LEFT;
  s->spr = spr_susier, s->ispd = 0;
  TALKF(FC_SUSIE, 0, obj_schoollobbycutscene_slash_Step_0_gml_532_0, obj_schoollobbycutscene_slash_Step_0_gml_533_0);
  TALKF(FC_SUSIE, 0, obj_schoollobbycutscene_slash_Step_0_gml_582_0, obj_schoollobbycutscene_slash_Step_0_gml_583_0,
        obj_schoollobbycutscene_slash_Step_0_gml_584_0);
  TALKF(FC_SUSIE, 0, obj_schoollobbycutscene_slash_Step_0_gml_616_0);
  s->sprs[0] = spr_susied, s->sprs[1] = spr_susier, s->sprs[2] = spr_susieu, s->sprs[3] = spr_susiel;
  WALK(s, s->x / FX, 0, 3);
  obj_free(s);
  pl.frozen = false;
  /* (Kris follows her up the hall: the closet) */
  FREE(true);
  WAIT_UNTIL(pl.y < 120);
  FREE(false);
  pl.frozen = true;
  TALKF(FC_SUSIE, 1, obj_schoollobbycutscene_slash_Step_0_gml_727_0, obj_schoollobbycutscene_slash_Step_0_gml_728_0,
        obj_schoollobbycutscene_slash_Step_0_gml_729_0, obj_schoollobbycutscene_slash_Step_0_gml_730_0);
  set_plot(4);
  pl.frozen = false;
  FADE_TO(room_schooldoor, 4);
  EV_END;
}

/* obj_darkdoor: the closet is dark inside */
static int ev_darkdoor(Ev *c) {
  Obj *s = obj_get(ACT_SUSIE);
  EV_BEGIN;
  pl.frozen = true;
  WAIT(30);
  TALKF(FC_SUSIE, 0, obj_darkdoor_slash_Step_0_gml_21_0, obj_darkdoor_slash_Step_0_gml_22_0);
  WAIT(30);
  TALKF(FC_SUSIE, 5, obj_darkdoor_slash_Step_0_gml_72_0, obj_darkdoor_slash_Step_0_gml_73_0,
        obj_darkdoor_slash_Step_0_gml_74_0);
  WAIT(40);
  TALKF(FC_SUSIE, 5, obj_darkdoor_slash_Step_0_gml_90_0);
  WAIT(40);
  TALKF(FC_SUSIE, 5, obj_darkdoor_slash_Step_0_gml_117_0);
  s->spr = spr_susie_krisgrab, s->ispd = FX / 4;
  pl.hidden = true;
  TALKF(FC_SUSIE, 5, obj_darkdoor_slash_Step_0_gml_132_0);
  s->vs = -2 * FX;
  WAIT(30);
  set_plot(5);
  pl.frozen = false, pl.hidden = false;
  FADE_TO(room_insidecloset, 1);
  EV_END;
}

/* obj_insideclosetcutscene: no walls, then the floor gives */
static int ev_closet(Ev *c) {
  Obj *s = obj_get(ACT_SUSIE);
  EV_BEGIN;
  pl.frozen = true;
  WAIT(30);
  TALKF(FC_SUSIE, 0, obj_insideclosetcutscene_slash_Step_0_gml_62_0, obj_insideclosetcutscene_slash_Step_0_gml_63_0,
        obj_insideclosetcutscene_slash_Step_0_gml_64_0);
  go(s, spr_susier, FX, 0);
  KRIS_WALK(pl.x + 60, pl.y, 1);
  halt(s);
  TALKF(FC_SUSIE, 0, obj_insideclosetcutscene_slash_Step_0_gml_97_0, obj_insideclosetcutscene_slash_Step_0_gml_98_0);
  go(s, spr_susier, FX, 0);
  KRIS_WALK(pl.x + 100, pl.y, 1);
  halt(s);
  TALKF(FC_SUSIE, 0, obj_insideclosetcutscene_slash_Step_0_gml_134_0, obj_insideclosetcutscene_slash_Step_0_gml_135_0,
        obj_insideclosetcutscene_slash_Step_0_gml_136_0);
  go(s, spr_susier, FX, 0);
  KRIS_WALK(pl.x + 100, pl.y, 1);
  halt(s);
  TALKF(FC_SUSIE, 0, obj_insideclosetcutscene_slash_Step_0_gml_180_0, obj_insideclosetcutscene_slash_Step_0_gml_181_0,
        obj_insideclosetcutscene_slash_Step_0_gml_182_0, obj_insideclosetcutscene_slash_Step_0_gml_183_0,
        obj_insideclosetcutscene_slash_Step_0_gml_184_0, obj_insideclosetcutscene_slash_Step_0_gml_185_0,
        obj_insideclosetcutscene_slash_Step_0_gml_186_0, obj_insideclosetcutscene_slash_Step_0_gml_187_0);
  s->spr = spr_susiel;
  WAIT(30);
  TALKF(FC_SUSIE, 9, obj_insideclosetcutscene_slash_Step_0_gml_237_0);
  g_shake = 30;
  WAIT(30);
  TALKF(FC_SUSIE, 9, obj_insideclosetcutscene_slash_Step_0_gml_266_0, obj_insideclosetcutscene_slash_Step_0_gml_267_0);
  WAIT(20);
  TALKF(FC_SUSIE, 0, obj_insideclosetcutscene_slash_Step_0_gml_284_0);
  WAIT(20);
  TALKF(FC_SUSIE, 0, obj_insideclosetcutscene_slash_Step_0_gml_313_0);
  /* they fall */
  s->spr = spr_susie_fall, s->vs = 2 * FX;
  pl.spr = spr_kris_fall;
  for (c->v[0] = 0; c->v[0] < 40; c->v[0]++) {
    pl.y += 2 + c->v[0] / 4;
    s->vs += FX / 4;
    g_fade = (uint8_t)min_i(255, c->v[0] * 7);
    WAIT(1);
  }
  set_plot(10);
  pl.frozen = false, pl.spr = 0;
  g_fade = 0;
  world_enter(room_dark1, 280, 200, DIR_DOWN);
  EV_END;
}

int lw_event(Ev *c) {
  switch (c->id) {
    case EV_KRISROOM: return ev_krisroom(c);
    case EV_CAR: return ev_car(c);
    case EV_LOBBY_TORIEL: return ev_lobby_toriel(c);
    case EV_CLASS: return ev_class(c);
    case EV_LOBBY_SUSIE: return ev_lobby_susie(c);
    case EV_DARKDOOR: return ev_darkdoor(c);
    case EV_CLOSET: return ev_closet(c);
    default: return 1;
  }
}
