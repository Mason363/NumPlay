#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Dark World (rooms.h): the cliffs where Kris lands, Susie in the dust, the eye puzzle, Lancer's chase,
 * the castle's front (Lancer's first fight, Ralsei), Castle Town and the tutorial, the Great Door and the
 * Field's enemies. Each cutscene is the original's event object as a protothread, its texts its own. */
#include <stdio.h>
#include <string.h>
#include "battle.h"
#include "cut.h"
#include "event.h"
#include "puzzles.h"
#include "game.h"
#include "gfx.h"
#include "rooms.h"
#include "text.h"
#include "world.h"

enum {
  EV_SUSIE_DUST = EV_DW_FIRST, EV_EYE_SWITCH, EV_EYE_OPEN, EV_CHASE1, EV_SLIDE, EV_CASTLE_FRONT, EV_TUTORIAL,
  EV_BIGDOOR, EV_DARKSLIDE
};

static Obj *marker(uint16_t spr, int x, int y, int ispd, int name) {
  Obj *o = obj_new(spr, x, y);
  o->ispd = ispd, o->name = (uint8_t)name;
  return o;
}
static void halt(Obj *o) { o->hs = o->vs = 0, o->ispd = 0, o->img = 0; }
static void walker(Obj *o, int c) {
  if (c == CH_SUSIE) o->sprs[0] = spr_susied_dark, o->sprs[1] = spr_susier_dark, o->sprs[2] = spr_susieu_dark, o->sprs[3] = spr_susiel_dark;
  if (c == CH_RALSEI) o->sprs[0] = spr_ralseid, o->sprs[1] = spr_ralseir, o->sprs[2] = spr_ralseiu, o->sprs[3] = spr_ralseil;
  if (c == ACT_LANCER) o->sprs[0] = spr_lancer_dt, o->sprs[1] = spr_lancer_rt, o->sprs[2] = spr_lancer_ut, o->sprs[3] = spr_lancer_lt;
}

/* the eye puzzle (obj_darkeyepuzzle): three eyes, three switches; two lit open the way */
static uint8_t eye[3];

void dw_setup(int room) {
  puzzles_reset();
  cut_reset();
  Inst in;
  switch (room) {
    case room_dark7:
      /* (obj_dustpile_susie) Susie hides in a dust pile */
      if (plot() < 14 && world_inst(obj_dustpile_susie, 0, &in)) {
        Obj *d = marker(spr_dustpile, in.x, in.y, 0, ACT_PROP);
        d->flags |= OF_TALK | OF_SOLID;
        obj_set_box(d, 0, 20, 60, 30);
        d->event = EV_SUSIE_DUST;
      }
      break;
    case room_dark_eyepuzzle:
      if (!world_inst(obj_darkeyepuzzle, 0, &in)) break;
      if (flag(201)) {
        eye[0] = eye[1] = eye[2] = 1;
        for (int i = 0; i < 3; i++) {
          marker(spr_magicalglass, 960 + i * 40, 320, 0, 0)->flags |= OF_BACK;
          marker(spr_magicalglass, 960 + i * 40, 360, 0, 0)->flags |= OF_BACK;
        }
      } else {
        eye[0] = eye[1] = eye[2] = 0;
        Obj *b = obj_new(0xFFFF, 960, 320);
        b->flags |= OF_SOLID | OF_HIDDEN, b->name = ACT_DOOR;
        obj_set_box(b, 0, 0, 40, 80);
        for (int k = 0; k < 3; k++)
          if (world_inst(obj_darkeyepuzzle_switch, k, &in)) {
            Obj *s = obj_new(0xFFFF, in.x, in.y);
            s->flags |= OF_TALK | OF_HIDDEN, s->arg = (int16_t)in.x;
            obj_set_box(s, 0, 0, 40, 40);
            s->event = EV_EYE_SWITCH;
          }
      }
      break;
    case room_castle_front:
      if (plot() < 30) {
        marker(spr_ralsei_hood, 495, 310, 0, ACT_RALSEI)->flags |= OF_HIDDEN;
        room_queue_event(EV_CASTLE_FRONT);
      }
      break;
    case room_castle_tutorial:
      /* obj_tutorialbattleevent: the training dummy Ralsei made, and his lesson */
      room_add_npc(obj_npc_room, 900, 160);
      if (plot() < 31) room_queue_event(EV_TUTORIAL);
      break;
    case room_castle_darkdoor:
      if (plot() < 33) room_queue_event(EV_BIGDOOR);
      break;
    default: break;
  }
  /* the slopes (obj_darkslide): down them Kris slides, and can't climb back */
  for (int k = 0; k < 8; k++) {
    if (!world_inst(obj_darkslide, k, &in)) break;
    Obj *s = obj_new(0xFFFF, in.x, in.y);
    s->flags |= OF_TOUCH | OF_HIDDEN;
    obj_set_box(s, 0, 0, 20 * in.sx / 100, 20 * in.sy / 100);
    s->event = EV_DARKSLIDE;
  }
  chasers_setup();
  chests_setup();
  field_setup(room);
  forest_setup(room);
  castle_setup(room);
}

/* the eyes, drawn over the room */
static void eyes_draw(void) {
  Inst in;
  if (g_room != room_dark_eyepuzzle || !world_inst(obj_darkeyepuzzle, 0, &in)) return;
  for (int i = 0; i < 3; i++) {
    gfx_sprite(spr_eyepuzzle_marking, i, (in.x + 16 + i * 110 - cam_x) / 2, (in.y + 60 - cam_y) / 2, 0, 255);
    gfx_sprite(spr_darkeye_h, eye[i], (in.x + i * 110 - cam_x) / 2, (in.y - cam_y) / 2, 0, 255);
  }
}

void dw_step(void) {
  static int last_room = -1;
  if (g_room == room_dark_eyepuzzle && !flag(201) && eye[1] && eye[2] && !event_running()) room_queue_event(EV_EYE_OPEN);
  if (g_room != last_room) {
    last_room = g_room;
    if (g_room == room_dark_chase1 && plot() < 16) room_queue_event(EV_CHASE1);
    if (g_room == room_dark_chase2 && plot() < 17) room_queue_event(EV_SLIDE);
  }
  if (g_room == room_dark_chase1 && plot() == 16) {
    Obj *l = obj_find(ACT_LANCER);
    if (l) obj_walk_to(l, pl.x + 40, pl.y - 120, 3);
  }
  chasers_step();
  cut_step();
  puzzles_step();
  field_step();
  forest_step();
  castle_step();
}

void dw_draw(void) {
  eyes_draw();
  chasers_draw();
  cut_draw();
  field_draw();
  forest_draw();
  castle_draw();
}

/* ---------------------------------------------------------------- events */
static int ev_susie_dust(Ev *c) {
  Obj *d = c->self;
  EV_BEGIN;
  TALKF(FC_SUSIE, 7, obj_dustpile_susie_slash_Step_0_gml_57_0);
  d->spr = spr_dustpile, d->ispd = FX / 4;
  {
    Obj *s = marker(spr_susied_dark, d->x / FX + 10, d->y / FX - 40, 0, ACT_SUSIE);
    walker(s, CH_SUSIE);
  }
  WAIT(20);
  TALKF(FC_SUSIE, 6, obj_dustpile_susie_slash_Step_0_gml_78_0, obj_dustpile_susie_slash_Step_0_gml_79_0,
        obj_dustpile_susie_slash_Step_0_gml_80_0, obj_dustpile_susie_slash_Step_0_gml_81_0,
        obj_dustpile_susie_slash_Step_0_gml_82_0);
  TALKF(FC_SUSIE, 6, obj_dustpile_susie_slash_Step_0_gml_104_0);
  TALKF(FC_SUSIE, 7, obj_dustpile_susie_slash_Step_0_gml_119_0);
  obj_free(obj_get(ACT_SUSIE));
  obj_free(d);
  party_add(CH_SUSIE);
  TALKF(FC_SUSIE, 1, obj_dustpile_susie_slash_Step_0_gml_156_0);
  /* she won't be led for long */
  FREE(true);
  WAIT(150);
  FREE(false);
  TALKF(FC_SUSIE, 0, obj_dustpile_susie_slash_Step_0_gml_201_0);
  party_remove(CH_SUSIE);
  set_plot(14);
  EV_END;
}

static int ev_eye_switch(Ev *c) {
  int x = c->self->arg;
  if (x < 600) eye[0] ^= 1, eye[2] ^= 1;
  else if (x < 700) eye[0] ^= 1, eye[1] ^= 1;
  else eye[2] ^= 1;
  return 1;
}

static int ev_eye_open(Ev *c) {
  EV_BEGIN;
  pl.frozen = true;
  WAIT(30);
  for (int i = 0; i < 3; i++) {
    marker(spr_magicalglass, 960 + i * 40, 320, 0, 0)->flags |= OF_BACK;
    marker(spr_magicalglass, 960 + i * 40, 360, 0, 0)->flags |= OF_BACK;
  }
  obj_free(obj_get(ACT_DOOR));
  for (int i = 0; i < MAX_OBJS; i++)
    if (objs[i].event == EV_EYE_SWITCH) obj_free(&objs[i]);
  g_shake = 10;
  set_flag(201, 1);
  WAIT(30);
  pl.frozen = false;
  EV_END;
}

/* obj_lancerchaseevent: someone waving up there... and he gives chase */
static int ev_chase1(Ev *c) {
  EV_BEGIN;
  party_add(CH_SUSIE);
  set_plot(15);
  WAIT(20);
  TALKF(FC_SUSIE, 0, obj_lancerchaseevent_slash_Step_0_gml_16_0, obj_lancerchaseevent_slash_Step_0_gml_17_0);
  {
    Obj *l = marker(spr_lancer_dt, pl.x + 120, pl.y - 160, FX / 5, ACT_LANCER);
    walker(l, ACT_LANCER);
    l->dir = DIR_DOWN;
  }
  WAIT(30);
  TALKF(FC_SUSIE, 9, obj_lancerchaseevent_slash_Step_0_gml_137_0);
  set_plot(16);
  /* (he follows, at Kris's own pace: dw_step) */
  EV_END;
}

/* obj_lancerslideevent: down the slope; Susie calls from below */
static int ev_slide(Ev *c) {
  EV_BEGIN;
  set_plot(17);
  WAIT(10);
  TALKF(FC_SUSIE, 9, obj_lancerslideevent_slash_Step_0_gml_34_0);
  pl.frozen = true;
  pl.spr = spr_krisd_slide;
  for (c->v[0] = 0; c->v[0] < 60; c->v[0]++) {
    pl.y += 12;
    WAIT(1);
  }
  pl.spr = 0, pl.frozen = false;
  FADE_TO(room_next(g_room, 1), 1);
  EV_END;
}

/* obj_darkcastle_event: the castle; Lancer; the first fight; Ralsei */
static int ev_castle_front(Ev *c) {
  Obj *r = obj_get(ACT_RALSEI);
  EV_BEGIN;
  pl.frozen = true;
  follow_show(false);
  {
    Obj *s = marker(spr_susieu_dark, pl.x + 60, pl.y + 40, 0, ACT_SUSIE);
    walker(s, CH_SUSIE);
  }
  WALK(obj_get(ACT_SUSIE), 540, 470, 4);
  KRIS_WALK(480, 440, 4);
  pl.dir = DIR_UP;
  TALKF(FC_SUSIE, 0, obj_darkcastle_event_slash_Step_0_gml_20_0, obj_darkcastle_event_slash_Step_0_gml_21_0);
  WAIT(30);
  TALKF(FC_SUSIE, 9, obj_darkcastle_event_slash_Step_0_gml_46_0);
  r->flags &= (uint16_t)~OF_HIDDEN;
  WAIT(20);
  TALKF(FC_RALSEI, 0, obj_darkcastle_event_slash_Step_0_gml_58_0, obj_darkcastle_event_slash_Step_0_gml_59_0);
  set_plot(21);
  TALKF(FC_RALSEI, 0, obj_darkcastle_event_slash_Step_0_gml_142_0, obj_darkcastle_event_slash_Step_0_gml_143_0,
        obj_darkcastle_event_slash_Step_0_gml_144_0, obj_darkcastle_event_slash_Step_0_gml_145_0,
        obj_darkcastle_event_slash_Step_0_gml_146_0);
  {
    static const char *m[16];
    m[0] = str(obj_darkcastle_event_slash_Step_0_gml_181_0), m[1] = "\\F0\\E1\\FR%";
    m[2] = str(obj_darkcastle_event_slash_Step_0_gml_183_0), m[3] = "\\F0\\E0\\FS%";
    m[4] = str(obj_darkcastle_event_slash_Step_0_gml_185_0), m[5] = str(obj_darkcastle_event_slash_Step_0_gml_186_0);
    m[6] = "\\F0\\E1\\FR%", m[7] = str(obj_darkcastle_event_slash_Step_0_gml_188_0), m[8] = "\\F0\\E0\\FS%";
    m[9] = str(obj_darkcastle_event_slash_Step_0_gml_190_0), m[10] = str(obj_darkcastle_event_slash_Step_0_gml_191_0);
    m[11] = str(obj_darkcastle_event_slash_Step_0_gml_192_0), m[12] = "\\F0\\E0\\FS%";
    m[13] = str(obj_darkcastle_event_slash_Step_0_gml_197_0), m[14] = str(obj_darkcastle_event_slash_Step_0_gml_199_0);
    m[15] = str(obj_darkcastle_event_slash_Step_0_gml_201_0);
    text_clear();
    text_face(FC_SUSIE, 0);
    for (int i = 0; i < 16; i++) text_add(m[i]);
    text_open();
  }
  WAIT_UNTIL(!text_active());
  /* Lancer rides in */
  {
    Obj *l = marker(spr_lancerbike, cam_x + 720, r->y / FX + 30, FX / 4, ACT_LANCER);
    l->hs = -12 * FX;
  }
  WAIT(30);
  halt(obj_get(ACT_LANCER));
  r->flags |= OF_HIDDEN;
  TALKF(FC_LANCER, 1, obj_darkcastle_event_slash_Step_0_gml_287_0, obj_darkcastle_event_slash_Step_0_gml_288_0);
  TALKF(FC_LANCER, 1, obj_darkcastle_event_slash_Step_0_gml_327_0, obj_darkcastle_event_slash_Step_0_gml_328_0,
        obj_darkcastle_event_slash_Step_0_gml_329_0, obj_darkcastle_event_slash_Step_0_gml_330_0);
  TALKF(FC_LANCER, 6, obj_darkcastle_event_slash_Step_0_gml_412_0, obj_darkcastle_event_slash_Step_0_gml_413_0,
        obj_darkcastle_event_slash_Step_0_gml_414_0, obj_darkcastle_event_slash_Step_0_gml_415_0,
        obj_darkcastle_event_slash_Step_0_gml_416_0);
  TALKF(FC_SUSIE, 1, obj_darkcastle_event_slash_Step_0_gml_460_0, obj_darkcastle_event_slash_Step_0_gml_464_0,
        obj_darkcastle_event_slash_Step_0_gml_465_0);
  party_add(CH_SUSIE);
  BATTLE(2);
  obj_free(obj_get(ACT_LANCER));
  r->flags &= (uint16_t)~OF_HIDDEN;
  TALKF(FC_RALSEI, 1, obj_darkcastle_event_slash_Step_0_gml_552_0);
  TALKF(FC_RALSEI, 1, obj_darkcastle_event_slash_Step_0_gml_569_0);
  r->spr = spr_ralseid;
  TALKF(FC_RALSEI, 0, obj_darkcastle_event_slash_Step_0_gml_611_0, obj_darkcastle_event_slash_Step_0_gml_612_0,
        obj_darkcastle_event_slash_Step_0_gml_613_0);
  TALKF(FC_SUSIE, 2, obj_darkcastle_event_slash_Step_0_gml_637_0);
  WALK(obj_get(ACT_SUSIE), pl.x + 300, pl.y + 200, 6);
  obj_free(obj_get(ACT_SUSIE));
  party_remove(CH_SUSIE);
  TALKF(FC_RALSEI, 1, obj_darkcastle_event_slash_Step_0_gml_663_0, obj_darkcastle_event_slash_Step_0_gml_664_0);
  TALKF(FC_RALSEI, 5, obj_darkcastle_event_slash_Step_0_gml_702_0, obj_darkcastle_event_slash_Step_0_gml_703_0,
        obj_darkcastle_event_slash_Step_0_gml_704_0, obj_darkcastle_event_slash_Step_0_gml_705_0,
        obj_darkcastle_event_slash_Step_0_gml_706_0, obj_darkcastle_event_slash_Step_0_gml_707_0);
  TALKF(FC_RALSEI, 0, obj_darkcastle_event_slash_Step_0_gml_723_0);
  TALKF(FC_RALSEI, 8, obj_darkcastle_event_slash_Step_0_gml_749_0);
  obj_free(r);
  party_add(CH_RALSEI);
  set_plot(30);
  follow_show(true);
  follow_reset();
  pl.frozen = false;
  EV_END;
}

/* obj_tutorialbattleevent: Ralsei's lesson with the Dummy, and his Manual */
static int ev_tutorial(Ev *c) {
  static char manual[2][128];
  EV_BEGIN;
  /* when the view reaches the dummy (xx - 13 .. xx + 20, the event at 400) */
  FREE(true);
  WAIT_UNTIL(cam_x >= 387 && !text_active());
  FREE(false);
  pl.frozen = true;
  cam_locked = true, cam_x = 390;
  {
    /* scr_text 135: would Kris like a lesson? */
    static const char *ch[2];
    text_clear();
    text_face(FC_RALSEI, 3);
    text_add(str(scr_text_slash_scr_text_gml_292_0));
    text_add(str(scr_text_slash_scr_text_gml_293_0));
    text_add(str(scr_text_slash_scr_text_gml_294_0));
    text_add(str(scr_text_slash_scr_text_gml_295_0));
    ch[0] = str(scr_text_slash_scr_text_gml_288_0), ch[1] = str(scr_text_slash_scr_text_gml_289_0);
    for (int i = 0; i < 2; i++)
      if (ch[i][0] == '#') ch[i]++;   /* ("#" starts a line in GameMaker) */
    text_set_choices(ch[0], ch[1], NULL, NULL);
    text_open();
  }
  WAIT_UNTIL(!text_active());
  if (text_choice() == 0) {
    TALKF(FC_RALSEI, 3, scr_text_slash_scr_text_gml_302_0);
    {
      Obj *d = obj_find_inst(250);
      if (d) d->flags |= OF_HIDDEN;
    }
    BATTLE(3);
    {
      Obj *d = obj_find_inst(250);
      if (d) d->flags &= (uint16_t)~OF_HIDDEN;
    }
    WAIT(30);
    snprintf(manual[0], sizeof manual[0], "%s%s%s", str(obj_tutorialbattleevent_slash_Step_0_gml_90_0), "toolbox",
             str(obj_tutorialbattleevent_slash_Step_0_gml_90_1));
    text_clear();
    text_face(FC_RALSEI, 0);
    if (flag(205) >= 4) {
      text_add(str(obj_tutorialbattleevent_slash_Step_0_gml_95_0));
      text_add(str(obj_tutorialbattleevent_slash_Step_0_gml_96_0));
    } else {
      text_add(str(obj_tutorialbattleevent_slash_Step_0_gml_87_0));
      text_add(str(obj_tutorialbattleevent_slash_Step_0_gml_88_0));
    }
    text_add(str(obj_tutorialbattleevent_slash_Step_0_gml_89_0));
    text_add(manual[0]);
    text_add("\\F0%");
    text_add(str(obj_tutorialbattleevent_slash_Step_0_gml_92_0));
    text_open();
  } else {
    /* (no lesson: Kris fought Lancer already) */
    snprintf(manual[1], sizeof manual[1], "%s%s%s", str(scr_text_slash_scr_text_gml_311_0), "toolbox",
             str(scr_text_slash_scr_text_gml_311_1));
    text_clear();
    text_face(FC_RALSEI, 3);
    text_add(str(scr_text_slash_scr_text_gml_307_0));
    text_add(str(scr_text_slash_scr_text_gml_308_0));
    text_add(str(scr_text_slash_scr_text_gml_309_0));
    text_add(str(scr_text_slash_scr_text_gml_310_0));
    text_add(manual[1]);
    text_add("\\F0%");
    text_add(str(scr_text_slash_scr_text_gml_313_0));
    text_open();
  }
  WAIT_UNTIL(!text_active());
  {
    extern void gm_itemget(int id);
    gm_itemget(4);   /* the Manual */
  }
  if (plot() < 31) set_plot(31);
  pl.frozen = false, cam_locked = false;
  EV_END;
}

/* obj_darkdoorevent: the Great Door to the Field */
static int ev_bigdoor(Ev *c) {
  EV_BEGIN;
  WAIT_UNTIL(pl.x > 200);
  pl.frozen = true;
  set_plot(32);
  g_shake = 30;
  WAIT(40);
  set_plot(33);
  pl.frozen = false;
  FADE_TO(room_field_start, 1);
  EV_END;
}

static bool on_slide(const Obj *o) {
  int x0 = pl.x, y0 = pl.y + 50, x1 = pl.x + 38, y1 = pl.y + 78;
  int ox = o->x / FX + o->bx, oy = o->y / FX + o->by;
  return x1 > ox && x0 < ox + o->bw && y1 > oy && y0 < oy + o->bh;
}
static int ev_darkslide(Ev *c) {
  Obj *o = c->self;
  EV_BEGIN;
  if (pl.y + 50 > o->y / FX + 20) return 1;   /* (from below: nothing) */
  pl.frozen = true;
  pl.spr = spr_krisd_slide;
  while (on_slide(o)) {
    pl.y += 12;
    WAIT(1);
  }
  pl.spr = 0, pl.dir = DIR_DOWN;
  {
    /* a wall where it ends */
    Obj *w = obj_new(0xFFFF, o->x / FX, o->y / FX + o->bh - 40);
    w->flags |= OF_SOLID | OF_HIDDEN;
    obj_set_box(w, 0, 0, 40, 40);
  }
  obj_free(o);
  pl.frozen = false;
  EV_END;
}

int dw_event(Ev *c) {
  if (c->id >= EV_CASTLE_FIRST) return castle_event(c);
  if (c->id >= EV_FOREST_FIRST) return forest_event(c);
  if (c->id >= EV_FIELD_FIRST) return field_event(c);
  switch (c->id) {
    case EV_SUSIE_DUST: return ev_susie_dust(c);
    case EV_EYE_SWITCH: return ev_eye_switch(c);
    case EV_EYE_OPEN: return ev_eye_open(c);
    case EV_CHASE1: return ev_chase1(c);
    case EV_SLIDE: return ev_slide(c);
    case EV_CASTLE_FRONT: return ev_castle_front(c);
    case EV_TUTORIAL: return ev_tutorial(c);
    case EV_BIGDOOR: return ev_bigdoor(c);
    case EV_CHASE_BATTLE: return chase_battle_event(c);
    case EV_CHEST: return chest_event(c);
    case EV_GLOW_SWITCH: return glow_switch_event(c);
    case EV_GLOW_HINT: return glow_hint_event(c);
    case EV_GLOW_SHAKE: return glow_shake_event(c);
    case EV_SUITS_BUTTON: return suits_button_event(c);
    case EV_DARKSLIDE: return ev_darkslide(c);
    default: return 1;
  }
}
