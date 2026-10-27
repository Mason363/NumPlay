/* Waterfall, its end: the lantern maze, the dead end where Undyne finds Frisk (and Monster Kid gets in her way),
 * the echo flowers' path, the chasm's bridge, Undyne on her crag and her fight, the chase by the river and the
 * neon sign, and Hotland's edge, where she falls in the heat. (As the Undertale Wiki describes them; what they say
 * is ours, in short.) */
#include <stddef.h>
#include "battle.h"
#include "event.h"
#include "events.h"
#include "game.h"
#include "menu.h"
#include "monsters.h"
#include "save.h"
#include "snowdin.h"
#include "waterfall.h"
#include "world.h"

static void gone(Obj *o) { o->kind = OBJ_NONE; }

#define ECHO(s) "* (The echo flower whispers.)|* " s
static void echo(int x, int y, const char *text) { obj_sign(x - 10, y - 14, 20, 40, text); }

/* ---------------------------------------------------------------- Undyne, after Frisk */
/* fled from her fight, she chases Frisk through the arena, the river and the neon sign's hall: slower than Frisk,
 * but a catch is another fight */
static bool chasing;
static int stun_t;
static void chase_update(void) {
  if (!chasing || event_running()) return;
  Obj *u = obj_find(ACTOR_UNDYNE);
  if (!u) return;
  if (stun_t) {
    stun_t--;
    return;
  }
  /* (two thirds of Frisk's speed, walking fast too) */
  int dx = pl.x + 10 - u->x, dy = pl.y + 29 - u->y, sp = 2 * (1 + g_walk_fast);
  u->x = (int16_t)(u->x + clamp_i(dx, -sp, sp)), u->y = (int16_t)(u->y + clamp_i(dy, -sp, sp));
  if (abs_i(dx) < 14 && abs_i(dy) < 12) event_start(EV_UNDYNE_CATCH, u);
}
/* she comes in after Frisk, from the way Frisk came */
static void chase_setup(void) {
  if (flag(F_UNDYNE) != 1) return;
  static const int8_t back[4][2] = {{0, -1}, {0, 1}, {1, 0}, {-1, 0}};
  chasing = true, stun_t = 30;
  wf_undyne(pl.x + 10 + back[pl.dir][0] * 70, pl.y + 29 + back[pl.dir][1] * 70);
}

/* Monster Kid on the chasm's bridge: how it went (EV_MK_CHASM's v[0]: 0 they meet, 1 Frisk helps them up, 3
 * Undyne gets there first, 4 Frisk runs off) */
static int mk_how;

void wf3_update(void) {
  if (g_room < ROOM_WF_LANTERN || g_room > ROOM_HT_ENTRANCE) return;
  chase_update();
  if (g_room == ROOM_WF_CHASM && flag(F_MK_CHASM) == 1 && !event_running()) {
    Obj *u = obj_find(ACTOR_UNDYNE);
    if (pl.x < 22330) mk_how = 4, event_start(EV_MK_CHASM, NULL);
    else if (u && (!u->walking || u->x - (pl.x + 10) < 24)) mk_how = 3, event_start(EV_MK_CHASM, NULL);
  }
}

/* ---------------------------------------------------------------- the rooms */
void wf3_setup(int room, int from) {
  (void)from;
  chasing = false;
  if (room < ROOM_WF_LANTERN || room > ROOM_HT_ENTRANCE) return;
  switch (room) {
    case ROOM_WF_LANTERN:
      obj_sign(21176, 2414, 48, 28, "* (The lanterns light the way\n  through the crystals.)|* (Their glow is dim, but\n  it's enough.)");
      {
        static const int16_t lx[5] = {21260, 21440, 21280, 21680, 21720}, ly[5] = {2440, 2640, 2740, 2800, 2420};
        for (int i = 0; i < 5; i++) obj_sign(lx[i], ly[i], 20, 20, "* (A lantern, glowing softly.)");
      }
      break;
    case ROOM_WF_DEADEND: {
      /* the end of the dark: an echo flower; the way north hidden in the grass until Undyne has come */
      Obj *e = obj_deco(SPR_ECHO_OW, 22420, 2686, true);
      if (e) obj_set_box(e, -8, -8, 16, 8);
      if (!flag(F_DEADEND)) {
        Obj *f = obj_trigger(22408, 2650, 24, 40, EV_DEADEND, ACTOR_TRIGGER);
        if (f) f->flags = OF_TALK;
        Obj *b = obj_trigger(22160, 2556, 62, 64, EV_NONE, ACTOR_TRIGGER2);
        if (b) b->flags = OF_SOLID | OF_HIDDEN;
      } else {
        echo(22420, 2666, ECHO("\"...\"\n* (It only gives back the quiet now.)"));
      }
      break;
    }
    case ROOM_WF_ECHOPATH:
      echo(22170, 2150, ECHO("\"It's so quiet back here.\n  Does anyone ever come this way?\""));
      echo(22210, 2230, ECHO("\"One day I'll see the stars.\n  The real ones, up on the surface.\""));
      echo(22170, 2330, ECHO("\"Shh... did you hear that?\n  Footsteps...\""));
      echo(22210, 2370, ECHO("\"If you could wish for anything...\n  what would it be?\""));
      break;
    case ROOM_WF_CHASM:
      /* the glyph at the end of the echo flowers' path, a SAVE, Monster Kid on the bridge */
      obj_sign(22180, 1978, 38, 44,
               "* (Ancient glyphs, about a prophecy.)|* (An angel will come down\n  from the surface...)|* (...and the underground\n  will go empty.)");
      echo(22210, 2070, ECHO("\"Yo... where'd everybody go?\""));
      obj_save(22300, 2000, "* (The roar of the chasm fills you with determination.)");
      if (flag(F_MK_CHASM) == 1) set_flag(F_MK_CHASM, 4);   /* (left in the middle of it) */
      if (!flag(F_MK_CHASM)) {
        wf_mk(22560, 2058, DIR_LEFT);
        obj_trigger(22396, 1990, 12, 110, EV_MK_CHASM, ACTOR_TRIGGER);
      }
      break;
    case ROOM_WF_ARENA:
      obj_save(23520, 2038, "* (Undyne's crag looms over you.)\n* (You're filled with determination.)");
      if (!flag(F_UNDYNE)) {
        wf_undyne(23455, 1570);
        obj_trigger(23300, 1980, 12, 100, EV_ARENA, ACTOR_TRIGGER);
      }
      chase_setup();
      break;
    case ROOM_WF_NORTH:
      chase_setup();
      break;
    case ROOM_WF_HOTGATE:
      obj_sign(23648, 672, 505, 100,
               flag(F_UNDYNE) == 2 ? "* (A neon sign. It's switched off.)" : "* (A neon sign.\n* It says WELCOME TO HOTLAND!)");
      if (!flag(F_PAP_NEON)) obj_trigger(23700, 760, 12, 70, EV_PAP_NEON, ACTOR_TRIGGER2);
      chase_setup();
      break;
    case ROOM_HT_ENTRANCE:
      obj_sign(130, 6140, 60, 92, "* (Sans's other sentry station.)\n* (He's here too, somehow.)");
      {
        Obj *k = obj_trigger(718, 6162, 30, 46, EV_HOT_COOLER, ACTOR_ITEM);
        if (k) k->flags = OF_TALK | OF_SOLID;
      }
      if (flag(F_UNDYNE) == 1) {
        obj_trigger(0, 6100, 1460, 300, EV_HOT_UNDYNE, ACTOR_TRIGGER);   /* (she's right behind) */
      } else if (flag(F_UNDYNE) == 3) {
        Obj *f = obj_deco(SPR_UNDYNE_FALLEN, 534, 6213, false);
        if (f) f->name = ACTOR_UNDYNE, f->event = EV_HOT_UNDYNE, f->flags |= OF_TALK, obj_set_box(f, 0, 6, 50, 20);
      }
      break;
  }
}

/* ---------------------------------------------------------------- the events */
static int ev_deadend(Ev *c) {
  Obj *u = obj_get(ACTOR_UNDYNE), *mk = obj_get(ACTOR_MK);
  EV_BEGIN;
  /* the echo flower at the end; the room lights up, and Undyne is there */
  NARRATE("* (The echo flower gives back the sound of your own footsteps.)");
  WAIT(20);
  pl.dir = DIR_LEFT;
  u = wf_undyne(pl.x - 170, pl.y + 29);
  WALK(u, pl.x - 50, pl.y + 29, 2);
  WAIT(20);
  NARRATE("* (Undyne raises her spear.)");
  /* ...and Monster Kid jumps out of the grass, right between them */
  mk = wf_mk(21960, pl.y + 31, DIR_RIGHT);
  WALK(mk, pl.x - 22, pl.y + 31, 6);
  mk->dir = DIR_LEFT;
  NARRATE("* Yo!! Undyne!! You're here!|* Are you gonna fight somebody?!\n* Can I watch? Please please please?!");
  WAIT(15);
  NARRATE("* (Undyne grabs Monster Kid by the arm...)");
  u = obj_get(ACTOR_UNDYNE), mk = obj_get(ACTOR_MK);
  WALK_TO(mk, 21840, mk->y, 2);
  WALK(u, 21810, u->y, 2);
  NARRATE("* (...and drags them away.)");
  gone(obj_get(ACTOR_UNDYNE)), gone(obj_get(ACTOR_MK));
  set_flag(F_DEADEND, 1);
  gone(obj_get(ACTOR_TRIGGER2));
  if (c->self) gone(c->self);
  WAIT(20);
  NARRATE("* (In the grass, a hidden path lights up.)");
  EV_END;
}

static int ev_mk_chasm(Ev *c) {
  Obj *u = obj_get(ACTOR_UNDYNE), *mk = obj_get(ACTOR_MK);
  EV_BEGIN;
  c->v[0] = c->self && c->self->name == ACTOR_MK ? 1 : mk_how;
  mk_how = 0;
  if (c->v[0] == 0) {
    /* they meet on the bridge: Monster Kid has learned what Frisk is */
    if (c->self) gone(c->self);
    WALK(mk, pl.x + 56, mk->y, 3);
    NARRATE("* Yo... I gotta ask you something.|* Undyne told me to stay away from you.\n* She says you're a human.|* ...|* So that means... I gotta be your enemy now, right?|* Okay! Here goes! You're a... you're a big...|* ...ugh. I can't think of anything mean.|* Forget it! See ya!");
    mk = obj_get(ACTOR_MK);
    mk->dir = DIR_RIGHT;
    WALK(mk, 22639, mk->y, 4);
    /* ...and trips over the edge */
    g_shake = 4;
    mk->y = 2084;
    obj_set_box(mk, -10, -30, 20, 30);
    mk->event = EV_MK_CHASM, mk->flags = OF_TALK;
    NARRATE("* (Monster Kid trips over the edge of the bridge!)|* Y-yo... a little help...?");
    u = wf_undyne(22900, 2058);
    WALK_TO(u, 22672, 2058, 1);
    set_flag(F_MK_CHASM, 1);
    NARRATE("* (Undyne is coming across the bridge.)");
    return 1;
  }
  if (c->v[0] == 1) {
    /* Frisk pulls them up: Monster Kid stands up to Undyne, and she backs off */
    u->walking = 0;
    NARRATE("* (You pull Monster Kid back up.)");
    mk->x = (int16_t)(pl.x + 40), mk->y = 2058, mk->dir = DIR_RIGHT, obj_set_box(mk, -6, -8, 12, 8);
    WAIT(10);
    NARRATE("* Undyne...|* If you wanna hurt them...\n* you'll have to get through me first!");
    WAIT(20);
    NARRATE("* (Undyne hesitates... then turns away.)");
    u = obj_get(ACTOR_UNDYNE);
    WALK(u, 22920, u->y, 3);
    gone(u);
    mk = obj_get(ACTOR_MK);
    mk->dir = DIR_LEFT;
    NARRATE("* Yo... thanks for saving me.|* We're still friends, right? Cool.|* I'd better get home before my parents worry. Later!");
    mk = obj_get(ACTOR_MK);
    LEAVE(mk, 4, 22920, mk->y);
    set_flag(F_MK_CHASM, 2);
    return 1;
  }
  if (c->v[0] == 3) {
    /* Undyne gets there first: Monster Kid falls, and she dives after them */
    NARRATE("* (Monster Kid can't hold on any more...)");
    gone(mk);
    WALK(u, 22650, u->y, 4);
    NARRATE("* (Undyne dives off the bridge after them!)");
    gone(obj_get(ACTOR_UNDYNE));
    WAIT(45);
    g_shake = 6;
    u = wf_undyne(22650, 2058);
    wf_mk(22676, 2058, DIR_LEFT);
    WAIT(15);
    NARRATE("* (Undyne climbs back up, Monster Kid with her.)\n* (She's badly hurt.)");
    NARRATE("* Undyne... you were right about them.|* They just stood there and watched me fall.");
    u = obj_get(ACTOR_UNDYNE), mk = obj_get(ACTOR_MK);
    LEAVE(mk, 2, 22940, mk->y);
    LEAVE(u, 2, 22920, u->y);
    set_flag(F_MK_CHASM, 3);
    return 1;
  }
  /* Frisk ran off: behind, Undyne helps Monster Kid up */
  gone(mk), gone(u);
  NARRATE("* (Behind you, Undyne pulls Monster Kid back up.)");
  set_flag(F_MK_CHASM, 4);
  EV_END;
}

/* after a fight: killed, she's gone; fled, she comes after Frisk */
static int undyne_after(void) {
  Obj *u = obj_find(ACTOR_UNDYNE);
  if (bt.mon[0].killed) {
    set_flag(F_UNDYNE, 2);
    chasing = false;
    if (u) gone(u);
    return 2;
  }
  set_flag(F_UNDYNE, 1);
  chasing = true, stun_t = 50;
  /* (she gets up behind Frisk, a little back the way Frisk came) */
  if (!u) u = wf_undyne(0, 0);
  u->x = (int16_t)(pl.x - 60), u->y = (int16_t)(pl.y + 29);
  return 1;
}

static int ev_arena(Ev *c) {
  EV_BEGIN;
  if (c->self) gone(c->self);
  /* Undyne, on top of her crag */
  WAIT_UNTIL(cam_pan(23290, 1440, 4));
  WAIT(15);
  NARRATE("* (Undyne stands on top of the crag.)");
  NARRATE("* You. Human.|* King Asgore has six human SOULs already.|* With one more, he can break the barrier\n  and set every monster free.|* So yours is coming with me.|* I've chased you all through Waterfall.\n* No more running.|* Get ready, human! Here I come!");
  NARRATE("* (Undyne leaps down from the crag!)");
  gone(obj_get(ACTOR_UNDYNE));
  WAIT_UNTIL(cam_pan(23250, 1840, 8));
  cam_free();
  g_shake = 8;
  wf_undyne(pl.x + 50, pl.y + 29);
  WAIT(15);
  BATTLE(FORM_UNDYNE);
  if (undyne_after() == 2) {
    NARRATE("* Heh... so that's how it ends...|* Alphys... the rest... is on you...");
    NARRATE("* (Undyne melts away.)");
  } else {
    stun_t = 50;
    NARRATE("* (Undyne isn't letting you get away that easily!)");
  }
  EV_END;
}

static int ev_undyne_catch(Ev *c) {
  EV_BEGIN;
  NARRATE("* (Undyne catches up with you!)");
  BATTLE(FORM_UNDYNE);
  if (undyne_after() == 2) NARRATE("* Heh... so that's how it ends...|* Alphys... the rest... is on you...|* (Undyne melts away.)");
  EV_END;
}

static int ev_pap_neon(Ev *c) {
  EV_BEGIN;
  if (c->self) gone(c->self);
  set_flag(F_PAP_NEON, 1);
  SAY(FACE_PAPYRUS, "* Ring...|`0* HUMAN! PAPYRUS HERE!|* I'VE BEEN THINKING ABOUT FRIENDSHIP.|`2* AND I THINK YOU AND\n  UNDYNE WOULD BE THE BEST\n  OF FRIENDS!!!|* WE SHOULD ALL GET TOGETHER SOMETIME!|`0* WELL! BYE!");
  if (chasing) NARRATE("* (Undyne waited politely for you to hang up.)");
  EV_END;
}

/* Hotland's heat: Undyne, after Frisk in her armor, falls on the bridge; the cooler's water gets her up */
static int ev_hot_undyne(Ev *c) {
  Obj *u = obj_get(ACTOR_UNDYNE);
  EV_BEGIN;
  if (flag(F_UNDYNE) == 1) {
    if (c->self) gone(c->self);
    PL_WALK(640, 6206, 3);
    pl.dir = DIR_LEFT;
    u = wf_undyne(4, 6236);
    WALK(u, 300, 6236, 2);
    WALK(u, 556, 6236, 1);
    g_shake = 6;
    gone(u);
    u = obj_deco(SPR_UNDYNE_FALLEN, 534, 6213, false);
    if (u) u->name = ACTOR_UNDYNE, u->event = EV_HOT_UNDYNE, u->flags |= OF_TALK, obj_set_box(u, 0, 6, 50, 20);
    set_flag(F_UNDYNE, 3);
    NARRATE("* (Undyne collapses on the\n  bridge.)|* (In all that armor, the\n  heat is too much for her.)");
    return 1;
  }
  if (flag(F_WATER_CUP) != 1) {
    NARRATE("* (Undyne lies there, dried out by the heat.)");
    return 1;
  }
  NARRATE("* (Pour the water on her?)@Pour@Don't");
  if (CHOICE() == 1) return 1;
  set_flag(F_WATER_CUP, 2);
  NARRATE("* (You pour the water on Undyne.)");
  WAIT(30);
  gone(u);
  u = wf_undyne(558, 6238);
  WAIT(20);
  NARRATE("* (Undyne gets up... and stares at you for a long time.)");
  u = obj_get(ACTOR_UNDYNE);
  LEAVE(u, 2, -20, u->y);
  set_flag(F_UNDYNE, 4);
  EV_END;
}

static int ev_hot_cooler(Ev *c) {
  EV_BEGIN;
  if (flag(F_UNDYNE) == 3 && !flag(F_WATER_CUP)) {
    NARRATE("* (A water cooler.)\n* (Take a cup of water?)@Yes@No");
    if (CHOICE() == 0) {
      set_flag(F_WATER_CUP, 1);
      NARRATE("* (You fill a cup with cold water.)");
    }
  } else {
    NARRATE("* (A water cooler. The water's nice and cold.)");
  }
  EV_END;
}

int wf3_event(Ev *c) {
  switch (c->id) {
    case EV_DEADEND: return ev_deadend(c);
    case EV_MK_CHASM: return ev_mk_chasm(c);
    case EV_ARENA: return ev_arena(c);
    case EV_UNDYNE_CATCH: return ev_undyne_catch(c);
    case EV_PAP_NEON: return ev_pap_neon(c);
    case EV_HOT_UNDYNE: return ev_hot_undyne(c);
    case EV_HOT_COOLER: return ev_hot_cooler(c);
  }
  return -1;
}
