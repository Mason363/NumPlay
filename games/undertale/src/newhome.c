/* New Home, the capital: the elevator up from the CORE, the grey street to Asgore's home; his home (the stairs down
 * chained shut, a key in the kitchen and one in the hallway, two presents in the children's room); the basement's
 * corridors, where the story of Asriel and the first human is told as Frisk walks; the Last Corridor and Sans's
 * judgment; the throne room, Asgore among his flowers; the coffins below; the barrier, Asgore's battle and Frisk's
 * choice; Flowey, the six SOULs and Omega Flowey; the way out, and Sans's call after. (As the Undertale Wiki describes
 * them; what they say is ours, in short.) */
#include <stddef.h>
#include "battle.h"
#include "event.h"
#include "events.h"
#include "game.h"
#include "gfx.h"
#include "hotland.h"
#include "items.h"
#include "menu.h"
#include "monsters.h"
#include "newhome.h"
#include "res.h"
#include "save.h"
#include "snowdin.h"
#include "world.h"

static void gone(Obj *o) { o->kind = OBJ_NONE; }
static Obj *thing(int x, int y, int w, int h, int event, int name, int arg) {
  Obj *o = obj_trigger(x, y, w, h, event, name);
  if (o) o->flags = OF_TALK | OF_SOLID, o->arg = (int16_t)arg;
  return o;
}
static Obj *touch(int x, int y, int w, int h, int event, int arg) {
  Obj *o = obj_trigger(x, y, w, h, event, ACTOR_TRIGGER);
  if (o) o->arg = (int16_t)arg;
  return o;
}
static Obj *npc(int name, uint16_t spr, int x, int y, int dir) {
  const uint16_t s[4] = {spr, spr, spr, spr};
  Obj *o = obj_npc(name, s, x, y, dir, 0);
  if (o) o->flags &= (uint16_t)~OF_SOLID;
  return o;
}

/* the story, told in the basement as Frisk walks (in short, in our words): corridor 1, then 2 */
#define NSTORY 11
static const int16_t story_x[NSTORY] = {1650, 1850, 2050, 2250, 2450, 2800, 3200, 3600, 4000, 4350, 4650};
static const char *const story[NSTORY] = {
  "* Long ago, a human child fell\n  into the RUINS, hurt, and cried\n  out for help.",
  "* The king's son, ASRIEL, found\n  the child and brought it back\n  to the castle.",
  "* The two became like brother and\n  sister. The king and queen\n  raised the child as their own.|* Down here, there was hope\n  again.",
  "* But one day, the child became\n  gravely ill.",
  "* Its one wish was to see the\n  flowers of its home village.\n* There was no way to grant it.|* The next day, the child was\n  gone.",
  "* Grieving, ASRIEL took the\n  child's SOUL into himself, and\n  grew powerful.|* He passed through the barrier,\n  bearing the child home.",
  "* In the village, he found a bed\n  of golden flowers, and laid the\n  child there.|* The villagers saw him beside\n  their dead child, and believed\n  he had killed it.",
  "* They attacked him with all\n  their strength. He could have\n  wiped them out.|* Instead, he held back. He only\n  smiled, and left.",
  "* Badly hurt, ASRIEL came home\n  and collapsed. His dust settled\n  over the garden.",
  "* In one night, the king and\n  queen had lost both children.|* So the king made a decree: any\n  human who fell here would die.|* With enough SOULs, the barrier\n  could be broken.",
  "* Soon, now. King ASGORE will\n  free us all.|* ...why aren't you smiling?\n* Aren't you glad?\n* Soon, you'll be free.",
};

void nh_setup(int room, int from) {
  (void)from;
  if (room < ROOM_NH_TOWN || room > ROOM_NH_WALL) return;
  set_flag(F_NH_IN, 1);
  switch (room) {
    case ROOM_NH_TOWN:
      thing(76, 4490, 48, 46, EV_NH_LIFT, ACTOR_DOOR, 0);
      thing(280, 4272, 44, 38, EV_NH_LIFT, ACTOR_DOOR, 1);
      obj_save(416, 4546, "* (The city's quiet. It fills you\n  with determination.)");
      obj_save(1670, 3175, "* (Home, sweet home... someone's.\n  You're filled with determination.)");
      obj_sign(1735, 3140, 20, 20, "* (A box. Its lid won't budge.)");
      break;
    case ROOM_NH_HOUSE:
      thing(1640, 2815, 60, 30, EV_NH_CHAIN, ACTOR_ITEM, 0);
      thing(1745, 2815, 58, 30, EV_NH_CHAIN, ACTOR_ITEM, 1);   /* (the chain itself, its note on it) */
      thing(1290, 2575, 50, 24, EV_NH_KEY, ACTOR_ITEM, 0);
      thing(2374, 2808, 20, 32, EV_NH_KEY, ACTOR_ITEM, 1);
      obj_sign(2198, 2808, 22, 32, "* (\"Room under renovations.\")");
      thing(1906, 2618, 24, 22, EV_NH_GIFT, ACTOR_ITEM, 0);
      thing(1994, 2604, 24, 22, EV_NH_GIFT, ACTOR_ITEM, 1);
      obj_sign(1290, 2735, 80, 45, "* (A fireplace. It's warm, but\n  not too hot to touch.)");
      obj_sign(2300, 2560, 60, 40, "* (A huge bed. A flowered\n  blanket hangs over its end.)");
      break;
    case ROOM_NH_BASEMENT:
      touch(1533, 2293, 8, 94, EV_NH_UP, 0);
      for (int i = flag(F_NH_STORY); i < NSTORY; i++)
        touch(story_x[i], i < 5 ? 2293 : 2143, 10, i < 5 ? 94 : 67, EV_NH_STORY, i);
      thing(4805, 2108, 50, 35, EV_NH_LIFT, ACTOR_DOOR, 2);
      break;
    case ROOM_NH_LAST:
      obj_save(4690, 2370, "* (Golden light pours in. You're\n  filled with determination.)");
      if (!flag(F_NH_SANS)) touch(5300, 2335, 10, 75, EV_NH_SANS, 0);
      break;
    case ROOM_NH_THALL:
      obj_save(6100, 1787, "* (The throne room is just ahead.\n  You're filled with determination.)");
      obj_sign(5910, 1750, 22, 40, "* (Throne Room.)");
      break;
    case ROOM_NH_THRONE:
      if (!flag(F_NH_THRONE)) {
        npc(ACTOR_ITEM, SPR_ASGORE_OW, 6018, 1357, DIR_UP);
        touch(5903, 1500, 215, 10, EV_NH_THRONE, 0);
      }
      obj_sign(5963, 1110, 60, 50, "* (A big throne. Beside it, a\n  second one under a sheet.)");
      break;
    case ROOM_NH_COFFINS:
      thing(6360, 2897, 34, 50, EV_NH_COFFIN, ACTOR_ITEM, 0);
      for (int i = 1; i < 6; i++) obj_sign(6360 + i * 46, 2897, 34, 50, "* (A coffin for a human. It's\n  empty... it's been opened.)");
      break;
    case ROOM_NH_BARRIER:
      obj_save(6340, 962, "* (The end is close. It fills\n  you with determination.)");
      break;
    case ROOM_NH_WALL:
      if (!flag(F_NH_ASGORE)) {
        npc(ACTOR_ITEM, SPR_ASGORE_OW, 6342, 760, DIR_DOWN);
        touch(6190, 795, 320, 20, EV_NH_BARRIER, 0);
      } else obj_sign(6250, 620, 160, 120, "* (The barrier. Past it, there's\n  nothing left to stop you.)");
      break;
  }
}

/* ---------------------------------------------------------------- the events */
/* the elevators: down to the CORE; the street's to the far end of the basement (once it's been reached); back */
static int ev_lift(Ev *c) {
  EV_BEGIN;
  c->v[0] = c->self ? c->self->arg : 0;
  if (c->v[0] == 1 && !flag(F_NH_LIFT)) {
    NARRATE("* (The elevator isn't working\n  yet.)");
    return 1;
  }
  NARRATE(c->v[0] == 0 ? "* (Take the elevator back down\n  to the CORE?)@Yes@No" : "* (Take the elevator?)@Yes@No");
  if (CHOICE() == 1) return 1;
  if (c->v[0] == 0) FADE_TO(ROOM_CORE_HALL, 4285, 125, DIR_DOWN);
  else if (c->v[0] == 1) FADE_TO(ROOM_NH_BASEMENT, 4740, 2150, DIR_DOWN);
  else FADE_TO(ROOM_NH_TOWN, 292, 4325, DIR_DOWN);
  EV_END;
}

static int ev_key(Ev *c) {
  EV_BEGIN;
  c->v[0] = c->self ? c->self->arg : 0;
  if (flag(c->v[0] ? F_NH_KEY2 : F_NH_KEY1)) {
    NARRATE(c->v[0] ? "* (Nothing else here.)" : "* (A note: \"Help yourself to\n  anything you want!\")");
    return 1;
  }
  if (!c->v[0]) NARRATE("* (A note: \"Help yourself to\n  anything you want!\")");
  set_flag(c->v[0] ? F_NH_KEY2 : F_NH_KEY1, 1);
  NARRATE("* (You found a key.)");
  EV_END;
}

/* the chain on the stairs down (its foot, or the chain itself with its note): both keys open it */
static int ev_chain(Ev *c) {
  EV_BEGIN;
  if (!flag(F_NH_CHAIN)) {
    if (!flag(F_NH_KEY1) || !flag(F_NH_KEY2)) {
      if (c->self && c->self->arg && !flag(F_NH_KEY1) && !flag(F_NH_KEY2))
        NARRATE("* (A note, hung on the chain.)|* (\"Howdy! I'm in the garden.\n  If you need to talk, come by.\")|* (\"The keys are in the kitchen\n  and the hallway.\")");
      NARRATE(flag(F_NH_KEY1) || flag(F_NH_KEY2) ? "* (The stairs are chained shut.\n  One lock is still closed.)"
                                                 : "* (The stairs down are chained\n  shut. Two locks.)");
      return 1;
    }
    NARRATE("* (You unlock the chain.)");
    set_flag(F_NH_CHAIN, 1);
  }
  FADE_TO(ROOM_NH_BASEMENT, 1560, 2320, DIR_RIGHT);
  EV_END;
}

static int ev_up(Ev *c) {
  EV_BEGIN;
  FADE_TO(ROOM_NH_HOUSE, 1660, 2856, DIR_DOWN);
  EV_END;
}

static int ev_gift(Ev *c) {
  EV_BEGIN;
  c->v[0] = c->self ? c->self->arg : 0;
  if (flag(c->v[0] ? F_NH_LOCKET : F_NH_DAGGER)) {
    NARRATE("* (An empty box.)");
    return 1;
  }
  NARRATE("* (A present, wrapped up.)\n* (Open it?)@Yes@No");
  if (CHOICE() == 1) return 1;
  if (!item_add(c->v[0] ? ITEM_HEART_LOCKET : ITEM_WORN_DAGGER)) {
    NARRATE("* (You're carrying too much.)");
    return 1;
  }
  set_flag(c->v[0] ? F_NH_LOCKET : F_NH_DAGGER, 1);
  NARRATE(c->v[0] ? "* (Inside, a heart-shaped locket.)\n* (You take it.)" : "* (Inside, an old dagger.)\n* (You take it.)");
  EV_END;
}

static int ev_story(Ev *c) {
  EV_BEGIN;
  c->v[0] = c->self ? c->self->arg : 0;
  if (c->self) gone(c->self);
  if (flag(F_NH_STORY) > c->v[0]) return 1;
  set_flag(F_NH_STORY, c->v[0] + 1);
  NARRATE(story[c->v[0]]);
  if (c->v[0] == NSTORY - 1) set_flag(F_NH_LIFT, 1);
  EV_END;
}

/* the Last Corridor: Sans, and what EXP and LOVE are */
static int ev_sans(Ev *c) {
  Obj *s = obj_get(ACTOR_SANS);
  EV_BEGIN;
  if (c->self) gone(c->self);
  s = obj_sans(pl.x + 110, pl.y + 29, DIR_LEFT);
  (void)s;
  SAY(FACE_SANS, "* so, you've made it this far.\n* soon you'll meet the king.|* together, you'll decide the\n  future of this world.|* so now, you'll be judged.\n* for every bit of EXP you've\n  earned.");
  SAY(FACE_SANS, "* what's EXP? \"execution\n  points.\" a measure of how\n  much you've hurt others.|* the more you kill, the easier\n  it gets to stop caring.|* that's your LOVE: your \"level\n  of violence.\"");
  if (flag(F_PAPYRUS) == 2) SAY(FACE_SANS, "* ...and you killed my brother.\n* i hope it was worth it.");
  if (sv.exp == 0)
    SAY(FACE_SANS, "* but you never gained any LOVE.\n* you stayed... you.|* heh. no LOVE... but lots of\n  love. know what i mean?|* ...or not. that's ok.");
  else SAYF(FACE_SANS, "* you've got some LOVE now.\n* LV %d.|* but there's still good in you.\n* i can tell.", sv.lv);
  SAY(FACE_SANS, "* anyway. what happens next...\n* we leave up to you.");
  set_flag(F_NH_SANS, 1);
  gone(obj_get(ACTOR_SANS));
  EV_END;
}

/* the throne room: Asgore and his flowers */
static int ev_throne(Ev *c) {
  Obj *a = obj_get(ACTOR_ITEM);
  EV_BEGIN;
  if (c->self) gone(c->self);
  SAY(FACE_ASGORE, "* Dum dum dum...|* Oh? Is someone there?\n* Just a moment! I have to\n  finish watering these.|* Howdy! How can I...");
  a = obj_get(ACTOR_ITEM);
  a->dir = DIR_DOWN;
  SAY(FACE_ASGORE, "* ...oh.|* I so wish we could have tea,\n  like any other afternoon.|* But... you know how this must\n  be.|* When you are ready, come to\n  the next room.");
  a = obj_get(ACTOR_ITEM);
  LEAVE(a, 2, 5968, a->y, 5968, 1110);
  set_flag(F_NH_THRONE, 1);
  EV_END;
}

static int ev_coffin(Ev *c) {
  EV_BEGIN;
  SAYF(FACE_NONE, "* (A coffin. The name on it is\n  \"%s\".)|* (It's been opened.)", sv.name);
  EV_END;
}

/* Sans's call, after: how things turned out (what Frisk did decides it) */
static int ev_call_after(Ev *c) {
  EV_BEGIN;
  SAY(FACE_SANS, "* ring...|* heya. it's me, sans.\n* figured you'd want to know\n  how it went down here.");
  if (flag(F_TORIEL_FIGHT) == 1) SAY(FACE_SANS, "* the lady from the ruins came\n  back. she runs things now.|* she teaches at the school,\n  too. guess she always\n  wanted to.");
  else if (flag(F_PAPYRUS) != 2 && flag(F_UNDYNE) != 2) SAY(FACE_SANS, "* undyne's in charge now, and\n  papyrus is her right hand.|* they're keeping everyone safe.");
  else SAY(FACE_SANS, "* nobody's really in charge.\n* it's... not great.");
  if (flag(F_CORE_MTT) == 1) SAY(FACE_SANS, "* mettaton's back on tv.\n* the ratings are through the\n  roof.");
  if (flag(F_CORE_MTT) == 2) SAY(FACE_SANS, "* no more of mettaton's shows,\n  though. folks miss him.");
  SAY(FACE_SANS, "* the king's gone. and the barrier\n  is still up.|* but we're getting by.|* so don't worry about us, kid.\n* live your life up there.|* see ya.");
  EV_END;
}

/* the barrier: Asgore, his battle, Frisk's choice; Flowey and the six SOULs; out */
static int ev_barrier(Ev *c) {
  Obj *a = obj_get(ACTOR_ITEM);
  EV_BEGIN;
  if (c->self) gone(c->self);
  (void)a;
  SAY(FACE_ASGORE, "* Here it is: the barrier.\n* The wall that holds us all\n  underground.|* Anything left to do?\n* Go and do it first.@Ready@Not yet");
  if (CHOICE() == 1) {
    SAY(FACE_ASGORE, "* I see. Take your time.");
    FADE_TO(ROOM_NH_BARRIER, 6415, 985, DIR_DOWN);
    return 1;
  }
  SAY(FACE_ASGORE, "* ...Then let us begin.|* Human... I am glad we met.|* Farewell.");
  NARRATE("* (Asgore swings his trident.)\n* (Your MERCY button breaks.)");
  BATTLE(FORM_ASGORE);
  if (battle_result() == BR_DIED) return 1;
  NARRATE("* (Asgore is on his knees.)\n* (What will you do?)@Kill@Spare");
  c->v[0] = CHOICE();
  if (c->v[0] == 0) {
    set_flag(F_NH_ASGORE, 2);
    sv.kills++;
    NARRATE("* (Asgore turns to dust.)");
  } else {
    set_flag(F_NH_ASGORE, 1);
    SAY(FACE_ASGORE, "* ...after everything I've done,\n  you would spare me?|* I declared war the day my\n  son died. I swore to take\n  every human SOUL...|* ...but I never wanted any\n  of it. I only wanted hope\n  back.");
    SAY(FACE_ASGORE, "* Human... I am so sorry.|* Could you... stay with me? We\n  could be like a family...");
    g_shake = 10;
    NARRATE("* (White pellets rain down on\n  Asgore.)\n* (He turns to dust.)");
  }
  gone(obj_get(ACTOR_ITEM));
  /* Flowey, where Asgore was */
  {
    static const uint16_t s[4] = {SPR_FLOWEY, SPR_FLOWEY, SPR_FLOWEY, SPR_FLOWEY};
    Obj *f = obj_npc(ACTOR_FLOWEY, s, 6336, 740, DIR_DOWN, 0);
    if (f) f->flags &= (uint16_t)~OF_SOLID;
  }
  SAY(FACE_FLOWEY, "* Heh. You fool.|* Still haven't figured it out?|* Down here, you kill, or you\n  get killed.");
  SAY(FACE_FLOWEY, "* And now... his six SOULs are\n  MINE.");
  g_shake = 16;
  NARRATE("* (The six SOULs rise from their\n  jars, and gather around\n  Flowey.)");
  BATTLE(FORM_OMEGA);
  set_flag(F_NH_FLOWEY, 3);   /* (beaten: spared or killed below) */
  SAY(FACE_FLOWEY, "* ...|* Why...?|* Why are you being so... nice\n  to me?|* I can't understand.\n* I just can't understand...");
  NARRATE("* (Flowey is beaten.)\n* (What will you do?)@Spare@Kill");
  if (CHOICE() == 0) {
    set_flag(F_NH_FLOWEY, 1);
    SAY(FACE_FLOWEY, "* ...you're letting me go? I'll\n  just come back, you know.|* ...|* ...you make no sense.");
    NARRATE("* (Flowey sinks into the ground.)");
  } else {
    set_flag(F_NH_FLOWEY, 2);
    NARRATE("* (Flowey is gone.)");
  }
  gone(obj_get(ACTOR_FLOWEY));
  NARRATE("* (With the six SOULs' power, you\n  cross the barrier.)|* (The sun is rising.)");
  set_flag(F_NH_DONE, 1);
  sv.room = (int16_t)ROOM_NH_BARRIER, sv.x = 6400, sv.y = 985;
  save_write_auto();
  /* Sans's call, then the end */
  c->v[1] = 1;
  EV_END;
}

int nh_event(Ev *c) {
  switch (c->id) {
    case EV_NH_LIFT: return ev_lift(c);
    case EV_NH_KEY: return ev_key(c);
    case EV_NH_CHAIN: return ev_chain(c);
    case EV_NH_UP: return ev_up(c);
    case EV_NH_GIFT: return ev_gift(c);
    case EV_NH_STORY: return ev_story(c);
    case EV_NH_SANS: return ev_sans(c);
    case EV_NH_THRONE: return ev_throne(c);
    case EV_NH_COFFIN: return ev_coffin(c);
    case EV_NH_BARRIER: {
      int r = ev_barrier(c);
      if (r && flag(F_NH_DONE) && c->v[1] == 1) {   /* (on to the call) */
        c->id = EV_NH_CALL, c->at = NULL, c->v[1] = 0;
        return 0;
      }
      return r;
    }
    case EV_NH_CALL: {
      int r = ev_call_after(c);
      if (r) menu_end_open("THE END|You went home. The monsters\nstayed underground.|(A neutral ending.)");
      return r;
    }
  }
  return -1;
}
