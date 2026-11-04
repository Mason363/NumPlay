#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The rooms' common things (rooms.h): doors and markers, SAVE points, what can be read or talked to,
 * treasure, and the events by id. Each part's own rooms are in lw.c (the Light World) and dw.c (the Dark). */
#include <string.h>
#include "battle.h"
#include "event.h"
#include "game.h"
#include "gmtexts.h"
#include "menu.h"
#include "puzzles.h"
#include "rooms.h"
#include "text.h"
#include "world.h"

/* every room of the chapter (they go in data.bin), in the original's order */
const uint16_t chapter_rooms[] = {
  room_krisroom, room_krishallway, room_torroom, room_torhouse, room_torbathroom, room_town_krisyard,
  room_town_northwest, room_town_north, room_beach, room_town_mid, room_town_apartments, room_town_south,
  room_town_school, room_town_church, room_graveyard, room_town_shelter, room_hospital_lobby, room_hospital_hallway,
  room_hospital_rudy, room_hospital_room2, room_diner, room_townhall, room_flowershop_1f, room_flowershop_2f,
  room_library, room_alphysalley, room_torielclass, room_schoollobby, room_alphysclass, room_schooldoor,
  room_insidecloset, room_dark1, room_dark1a, room_dark2, room_dark3, room_dark3a, room_dark_wobbles,
  room_dark_eyepuzzle, room_dark7, room_dark_chase1, room_dark_chase2, room_castle_outskirts, room_castle_town,
  room_castle_front, room_castle_tutorial, room_castle_darkdoor, room_field_start, room_field_forest, room_field1,
  room_field2, room_field2A, room_field_topchef, room_field_puzzle1, room_field_maze, room_field_puzzle2,
  room_field_getsusie, room_field_shop1, room_field_puzzletutorial, room_field3, room_field_boxpuzzle, room_field4,
  room_field_secret1, room_field_checkers4, room_field_checkers2, room_field_checkers6, room_field_checkers3,
  room_field_checkers1, room_field_checkers5, room_field_checkers7, room_field_checkersboss,
};

int room_next(int room, int n) {
  const uint8_t *o = section(SEC_ORDER);
  int want = rd16(o + 2 * room) + n;
  for (int i = 0; i < NUM_ROOMS; i++)
    if (rd16(o + 2 * i) == want) return i;
  return room;
}

/* scr_roomname: the name the SAVE menu shows (by the room's place in the original's order) */
const char *room_title(int room) {
  static const struct {
    uint8_t orig;
    uint16_t text;
  } names[] = {
    {2, scr_roomname_slash_scr_roomname_gml_3_0}, {35, scr_roomname_slash_scr_roomname_gml_4_0},
    {40, scr_roomname_slash_scr_roomname_gml_5_0}, {45, scr_roomname_slash_scr_roomname_gml_6_0},
    {49, scr_roomname_slash_scr_roomname_gml_7_0}, {59, scr_roomname_slash_scr_roomname_gml_8_0},
    {68, scr_roomname_slash_scr_roomname_gml_9_0}, {71, scr_roomname_slash_scr_roomname_gml_10_0},
    {73, scr_roomname_slash_scr_roomname_gml_11_0}, {82, scr_roomname_slash_scr_roomname_gml_12_0},
    {90, scr_roomname_slash_scr_roomname_gml_13_0}, {96, scr_roomname_slash_scr_roomname_gml_14_0},
    {97, scr_roomname_slash_scr_roomname_gml_15_0}, {107, scr_roomname_slash_scr_roomname_gml_16_0},
    {114, scr_roomname_slash_scr_roomname_gml_17_0}, {123, scr_roomname_slash_scr_roomname_gml_18_0},
    {126, scr_roomname_slash_scr_roomname_gml_19_0}, {111, scr_roomname_slash_scr_roomname_gml_20_0},
  };
  int orig = rd16(section(SEC_ORDER) + 2 * room);
  for (unsigned i = 0; i < sizeof names / sizeof names[0]; i++)
    if (names[i].orig == orig) return str(names[i].text);
  return " ";
}

/* the doors: where each goes (obj_doorA..F: the next or previous rooms; X and W: by the room) */
static int door_dest(int obj, int *entrance) {
  switch (obj) {
    case obj_doorA: case obj_doorA_musfade: *entrance = 1; return room_next(g_room, 1);
    case obj_doorB: case obj_doorB_musfade: *entrance = 2; return room_next(g_room, -1);
    case obj_doorC: case obj_doorC_musfade: *entrance = 4; return room_next(g_room, 2);
    case obj_doorD: case obj_doorD_musfade: *entrance = 5; return room_next(g_room, -2);
    case obj_doorE: *entrance = 6; return room_next(g_room, 3);
    case obj_doorF: *entrance = 7; return room_next(g_room, -3);
    case obj_doorX: case obj_doorX_musfade:
      /* (the original's tables, obj_doorX's and obj_doorX_musfade's alarm 2) */
      *entrance = 24;
      switch (g_room) {
        case room_castle_town: return room_castle_tutorial;
        case room_castle_tutorial: return room_castle_town;
        case room_town_south: return room_library;
        case room_library: return room_town_south;
        case room_town_church: return room_townhall;
        case room_townhall: return room_town_church;
        case room_schoollobby: return room_town_school;
        case room_town_school: return room_schoollobby;
        case room_town_mid: return room_diner;
        case room_diner: return room_town_mid;
        case room_town_north: return room_flowershop_1f;
        case room_flowershop_1f: return room_town_north;
        case room_torhouse: return room_town_krisyard;
        case room_town_krisyard: return room_torhouse;
        case room_field_shop1: return room_shop1;
        case room_man: return room_forest_savepoint2;
        case room_forest_area5: return rnd_n(50) == 49 && flag(910) == 0 ? room_man : room_forest_starwalker;
        case room_cc_1f: return pl.x >= 1300 ? room_cc_prisonelevator : g_room;
        case room_cc_5f: return room_shop2;
        case room_cc_prisonelevator:
          return flag(239) == 1 ? room_cc_1f : flag(239) == 2 ? room_cc_5f : flag(239) == 3 ? room_cc_prison_prejoker
                                                                                             : room_cc_prison_to_elevator;
        case room_cc_prison_to_elevator: set_flag(239, 0); return room_cc_prisonelevator;
        case room_cc_prison_prejoker: set_flag(239, 3); return room_cc_prisonelevator;
        case room_cc_entrance: return room_forest_castlefront;
        case room_cc_prisonlancer: return room_cc_prison_cells;
        case room_cc_prison_cells: return room_cc_prisonlancer;
        case room_forest_secret1: return room_forest_dancers1;
        case room_forest_dancers1: return room_forest_secret1;
        case room_forest_savepoint3: return room_forest_savepoint_relax;
        case room_forest_savepoint_relax: return plot() < 120 ? room_forest_maze1 : room_forest_savepoint3;
        case room_forest_maze_susie:
          if (pl.y >= world_room_info()->h / 2) {
            set_flag(292, 0), set_flag(291, 0);
            return room_forest_savepoint_relax;
          }
          return room_forest_maze2;
        case room_forest_castlefront: return plot() < 150 ? room_next(g_room, -1) : room_forest_fightsusie;
        case room_forest_fightsusie: return plot() < 150 ? room_next(g_room, 1) : room_forest_castlefront;
        case room_forest_maze1: case room_forest_maze2: return forest_maze_door(entrance);
        default: return g_room;
      }
    case obj_doorW: case obj_doorw_musfade:
      *entrance = 23;
      switch (g_room) {
        case room_town_south: return room_hospital_lobby;
        case room_hospital_lobby: return room_town_south;
        case room_town_mid: return room_alphysalley;
        case room_alphysalley: return room_town_mid;
        case room_town_north: return room_beach;
        case room_beach: return room_town_north;
        case room_cc_1f: set_flag(239, 1); return room_cc_elevator;
        case room_cc_5f: set_flag(239, 2); return room_cc_elevator;
        case room_cc_elevator: return flag(239) == 2 ? room_cc_5f : room_cc_1f;
        case room_forest_castlefront: return room_cc_entrance;
        case room_cc_entrance: return room_forest_castlefront;
        default: return g_room;
      }
    default: *entrance = 1; return g_room;
  }
}

static bool is_door(int obj) {
  switch (obj) {
    case obj_doorA: case obj_doorA_musfade: case obj_doorB: case obj_doorB_musfade: case obj_doorC: case obj_doorC_musfade:
    case obj_doorD: case obj_doorD_musfade: case obj_doorE: case obj_doorF: case obj_doorX: case obj_doorX_musfade:
    case obj_doorW: case obj_doorw_musfade: return true;
    default: return false;
  }
}

static void npc_setup(const Inst *in);
/* an npc a cutscene makes (instance_create of obj_npc_room...): as the room's own */
Obj *room_add_npc(int obj, int x, int y) {
  Inst in = {(uint16_t)obj, (int16_t)x, (int16_t)y, 100, 100, 250};
  npc_setup(&in);
  for (int i = MAX_OBJS - 1; i >= 0; i--)
    if (!(objs[i].flags & OF_GONE) && objs[i].inst == 250) return &objs[i];
  return NULL;
}
static uint8_t reads[256];
static int pending_ev;

/* the instances every room places the same way */
void room_setup(int room) {
  const RoomInfo *r = world_room_info();
  memset(reads, 0, sizeof reads);
  pending_ev = 0;
  for (int i = 0; i < r->ninst; i++) {
    Inst in;
    room_inst(r, i, &in);
    int w = 20 * in.sx / 100, h = 20 * in.sy / 100;
    if (is_door(in.obj)) {
      Obj *o = obj_new(0xFFFF, in.x, in.y);
      o->flags |= OF_TOUCH | OF_HIDDEN;
      o->obj = in.obj, o->inst = (int16_t)in.n;
      obj_set_box(o, 0, 0, w, h);
      o->event = EV_DOOR;
    } else if (in.obj == obj_readable_room1 || in.obj == obj_readable) {
      Obj *o = obj_new(0xFFFF, in.x, in.y);
      o->flags |= OF_TALK | OF_HIDDEN;
      o->obj = in.obj, o->inst = (int16_t)in.n;
      obj_set_box(o, 0, 0, w, h);
      o->event = EV_READ;
    } else if (in.obj == obj_savepoint) {
      Obj *o = obj_new(spr_savepoint, in.x, in.y);
      o->flags |= OF_TALK | OF_SOLID;
      o->obj = in.obj, o->inst = (int16_t)in.n;
      o->ispd = FX / 4;
      int s = world_scale();
      obj_set_box(o, 0, 10 * s, 20 * s, 10 * s);
      o->event = EV_SAVE;
      o->name = ACT_SAVE;
    } else if (in.obj == obj_npc_room || in.obj == obj_npc_sign || in.obj == obj_npc_facing ||
               in.obj == obj_npc_room_animated) {
      npc_setup(&in);
    }
  }
  if (world_dark()) dw_setup(room);
  else lw_setup(room);
}


void room_queue_event(int id) { pending_ev = id; }

void room_step(void) {
  if (pending_ev && !event_running() && !world_fading()) {
    int id = pending_ev;
    pending_ev = 0;
    event_start(id, NULL);
  }
  if (world_dark()) dw_step();
  else lw_step();
}

void room_draw_under(void) {
  if (world_dark()) puzzles_draw_under();
}

void room_draw_over(void) {
  if (world_dark()) dw_draw();
  else lw_draw();
}

void room_menu(void) { menu_open(); }

/* ---------------------------------------------------------------- the common events */
static int ev_door(Ev *c) {
  EV_BEGIN;
  {
    int e;
    int dest = door_dest(c->self->obj, &e);
    if (dest == g_room) return 1;
    c->v[0] = dest, c->v[1] = e;
  }
  pl.frozen = true;
  FADE_TO(c->v[0], c->v[1]);
  pl.frozen = false;
  EV_END;
}

/* ---------------------------------------------------------------- what things say (gmtexts.c) */
static TxtCtx tc;

static char face_msgs[MAX_MSG][16];

void txt_msg(TxtCtx *c, int i, const char *s) {
  if (i < 0 || i >= MAX_MSG) return;
  c->msg[i] = s;
  if (i + 1 > c->nmsg) c->nmsg = i + 1;
}
void txt_msg_subst(TxtCtx *c, int i, const char *s, const char *a) {
  static char bufs[4][160];
  static int k;
  char *o = bufs[k++ & 3];
  int n = 0;
  for (int j = 0; s[j] && n < 150; j++) {
    if (s[j] == '~' && s[j + 1] == '1') {
      for (const char *q = a; *q && n < 150; q++) o[n++] = *q;
      j++;
    } else {
      o[n++] = s[j];
    }
  }
  o[n] = 0;
  txt_msg(c, i, o);
}

/* scr_susface and the others: a message that only changes the face (who 0: none) */
void txt_face(TxtCtx *c, int i, char who, int fe) {
  if (i < 0 || i >= MAX_MSG) return;
  char *m = face_msgs[i];
  int n = 0;
  m[n++] = '\\', m[n++] = 'F', m[n++] = '0';
  if (who) {
    m[n++] = '\\', m[n++] = 'E', m[n++] = (char)(fe < 10 ? '0' + fe : 'A' + fe - 10);
    m[n++] = '\\', m[n++] = 'F', m[n++] = who;
  }
  m[n++] = '%', m[n] = 0;
  txt_msg(c, i, m);
}
int g_noroom;
int g_signal_obj = -1, g_signal_con;
void gm_signal(int obj, int con) { g_signal_obj = obj, g_signal_con = con; }
void gm_itemget(int id) {
  g_noroom = 1;
  for (int i = 0; i < NITEMS; i++)
    if (!sv.item[i]) {
      sv.item[i] = (uint8_t)id;
      g_noroom = 0;
      return;
    }
}
void gm_keyitemget(int id) {
  g_noroom = 1;
  for (int i = 0; i < NITEMS; i++)
    if (!sv.keyitem[i]) {
      sv.keyitem[i] = (uint8_t)id;
      g_noroom = 0;
      return;
    }
}
void gm_weaponget(int id) {
  g_noroom = 1;
  for (int i = 0; i < NITEMS; i++)
    if (!sv.weapons[i]) {
      sv.weapons[i] = (uint8_t)id;
      g_noroom = 0;
      return;
    }
}
void gm_armorget(int id) {
  g_noroom = 1;
  for (int i = 0; i < NITEMS; i++)
    if (!sv.armors[i]) {
      sv.armors[i] = (uint8_t)id;
      g_noroom = 0;
      return;
    }
}
void gm_litemget(int id) {
  g_noroom = 1;
  for (int i = 0; i < 8; i++)
    if (!sv.litem[i]) {
      sv.litem[i] = (uint8_t)id;
      g_noroom = 0;
      return;
    }
}

static int has(const uint8_t *list, int n, int id) {
  for (int i = 0; i < n; i++)
    if (list[i] == id) return 1;
  return 0;
}
static void drop(uint8_t *list, int n, int id) {
  for (int i = 0; i < n; i++)
    if (list[i] == id) {
      memmove(list + i, list + i + 1, (size_t)(n - i - 1));
      list[n - 1] = 0;
      return;
    }
}
int gm_itemcheck(int id) { return has(sv.item, NITEMS, id); }
int gm_keyitemcheck(int id) { return has(sv.keyitem, NITEMS, id); }
int gm_litemcheck(int id) { return has(sv.litem, 8, id); }
int gm_weaponcheck(int id) {
  return has(sv.weapons, NITEMS, id) || sv.weapon[1] == id || sv.weapon[2] == id || sv.weapon[3] == id;
}
void gm_itemremove(int id) { drop(sv.item, NITEMS, id); }
void gm_keyitemremove(int id) { drop(sv.keyitem, NITEMS, id); }
void gm_litemremove(int id) { drop(sv.litem, 8, id); }

static void tc_begin(const Obj *o) {
  memset(&tc, 0, sizeof tc);
  const RoomInfo *r = world_room_info();
  for (int i = 0; i < r->ninst; i++) {
    Inst in;
    room_inst(r, i, &in);
    if (in.n == o->inst) {
      tc.x = in.x, tc.y = in.y;
      break;
    }
  }
  tc.spr = o->spr;
  tc.type = o->arg;
  tc.read = reads[o->inst & 255];
  tc.talked = tc.read;
  tc.facing = pl.dir;
}

/* the box opened with what tc says */
static bool tc_say(void) {
  if (!tc.nmsg) return false;
  text_clear();
  text_face(tc.fc, tc.fe);
  for (int i = 0; i < tc.nmsg; i++) text_add(tc.msg[i] ? tc.msg[i] : " %");
  if (tc.choicemsg[0]) text_set_choices(tc.choicemsg[0], tc.choicemsg[1], tc.choicemsg[2], tc.choicemsg[3]);
  text_open();
  return true;
}

/* a choice made (obj_choicer_neo): scr_text(global.msc + 1) goes on */
static void tc_choice(int choice) {
  tc.choice = choice;
  tc.msc++;
  tc.arg = tc.msc;
  tc.nmsg = 0;
  memset(tc.msg, 0, sizeof tc.msg);
  memset(tc.choicemsg, 0, sizeof tc.choicemsg);
  int msc = tc.msc;
  gm_scr_text(&tc);
  for (int i = 0; i < tc.nmsg; i++) text_add(tc.msg[i] ? tc.msg[i] : " %");
  if (tc.choicemsg[0]) text_set_choices(tc.choicemsg[0], tc.choicemsg[1], tc.choicemsg[2], tc.choicemsg[3]);
  /* (a choice in the new messages: it goes on again) */
  if (tc.msc != msc || tc.choicemsg[0]) text_on_choice(tc_choice);
}

/* an npc's own talk: scr_text(msc), its choices going on as they do (side: 0 the box on top, -1 by Kris) */
bool talk_msc(int msc, int side) {
  memset(&tc, 0, sizeof tc);
  tc.msc = tc.arg = msc;
  tc.facing = pl.dir;
  gm_scr_text(&tc);
  text_side(side);
  if (!tc_say()) return false;
  if (tc.msc) text_on_choice(tc_choice);
  return true;
}

/* obj_readable_room1, obj_npc_room...: what it says, by the room, where it is, how often it was read */
static int ev_read(Ev *c) {
  EV_BEGIN;
  tc_begin(c->self);
  switch (c->self->obj) {
    case obj_npc_room: gm_npc_room_talk(&tc); break;
    case obj_npc_sign: gm_npc_sign_talk(&tc); break;
    case obj_npc_facing: gm_npc_facing_talk(&tc); break;
    case obj_npc_room_animated: gm_npc_room_animated_talk(&tc); break;
    default: gm_readable_room1(&tc); break;
  }
  reads[c->self->inst & 255]++;
  if (!tc_say()) return 1;
  if (tc.msc) text_on_choice(tc_choice);
  WAIT_UNTIL(!text_active());
  EV_END;
}

/* an npc's sprite (its create) */
static void npc_setup(const Inst *in) {
  memset(&tc, 0, sizeof tc);
  tc.x = in->x, tc.y = in->y, tc.spr = 0xFFFF, tc.ispd = -1;
  switch (in->obj) {
    case obj_npc_room: gm_npc_room_create(&tc); break;
    case obj_npc_sign: gm_npc_sign_create(&tc); break;
    case obj_npc_facing: gm_npc_facing_create(&tc); break;
    case obj_npc_room_animated: gm_npc_room_animated_create(&tc); break;
    default: break;
  }
  if (tc.gone) return;
  Obj *o = obj_new(tc.spr, tc.x, tc.y);
  o->flags |= OF_TALK | OF_SOLID;
  if (tc.hidden) o->flags |= OF_HIDDEN;
  o->obj = in->obj, o->inst = (int16_t)in->n;
  o->event = EV_READ;
  o->ispd = tc.ispd >= 0 ? tc.ispd : in->obj == obj_npc_room_animated ? FX / 5 : 0;
  o->arg = (int16_t)tc.type;
  /* its box: its sprite's (the original's interactablesolid) */
  int s = world_scale();
  int w = tc.spr != 0xFFFF ? spr_w(tc.spr) * s : 20, h = tc.spr != 0xFFFF ? spr_h(tc.spr) * s : 20;
  obj_set_box(o, 0, h / 2, w, h - h / 2);
}

/* obj_savepoint's texts, by the room */
static int c_talked;   /* (the eye puzzle's SAVE point talks once) */
static int save_text(int *ids) {
  int n = 0;
  switch (g_room) {
    case room_dark1a:
      ids[n++] = obj_savepoint_slash_Other_10_gml_17_0, ids[n++] = obj_savepoint_slash_Other_10_gml_18_0;
      break;
    case room_dark_eyepuzzle:
      if (c_talked) break;
      if (sv.hp[CH_KRIS] < 30)
        ids[n++] = obj_savepoint_slash_Other_10_gml_33_0, ids[n++] = obj_savepoint_slash_Other_10_gml_34_0;
      else if (sv.hp[CH_KRIS] < sv.maxhp[CH_KRIS])
        ids[n++] = obj_savepoint_slash_Other_10_gml_26_0, ids[n++] = obj_savepoint_slash_Other_10_gml_27_0,
        ids[n++] = obj_savepoint_slash_Other_10_gml_28_0;
      else
        ids[n++] = obj_savepoint_slash_Other_10_gml_39_0, ids[n++] = obj_savepoint_slash_Other_10_gml_40_0,
        ids[n++] = obj_savepoint_slash_Other_10_gml_41_0;
      c_talked++;
      break;
    case room_castle_town:
      if (have_char(CH_RALSEI))
        ids[n++] = obj_savepoint_slash_Other_10_gml_57_0, ids[n++] = obj_savepoint_slash_Other_10_gml_58_0;
      else
        ids[n++] = obj_savepoint_slash_Other_10_gml_51_0, ids[n++] = obj_savepoint_slash_Other_10_gml_52_0,
        ids[n++] = obj_savepoint_slash_Other_10_gml_53_0;
      break;
    case room_field_start:
      if (plot() >= 37)
        ids[n++] = obj_savepoint_slash_Other_10_gml_71_0, ids[n++] = obj_savepoint_slash_Other_10_gml_72_0;
      else
        ids[n++] = obj_savepoint_slash_Other_10_gml_66_0, ids[n++] = obj_savepoint_slash_Other_10_gml_67_0;
      break;
    case room_field_shop1:
      if (!have_char(CH_SUSIE))
        ids[n++] = obj_savepoint_slash_Other_10_gml_84_0, ids[n++] = obj_savepoint_slash_Other_10_gml_85_0;
      else
        ids[n++] = obj_savepoint_slash_Other_10_gml_80_0, ids[n++] = obj_savepoint_slash_Other_10_gml_81_0;
      break;
    default: break;
  }
  return n;
}

static int ev_save(Ev *c) {
  EV_BEGIN;
  {
    /* what it says in this room (obj_savepoint), then every SAVE point heals the party */
    int ids[4];
    int n = save_text(ids);
    for (int i = 1; i < 4; i++)
      if (sv.hp[i] < sv.maxhp[i]) sv.hp[i] = sv.maxhp[i];
    if (n) {
      text_clear();
      for (int i = 0; i < n; i++) text_add(str((uint16_t)ids[i]));
      text_open();
    }
  }
  WAIT_UNTIL(!text_active());
  menu_save_open();
  WAIT_UNTIL(menu_save_done());
  EV_END;
}

int event_run(Ev *c) {
  if (c->id >= EV_DW_FIRST) return dw_event(c);
  if (c->id >= EV_LW_FIRST) return lw_event(c);
  switch (c->id) {
    case EV_DOOR: return ev_door(c);
    case EV_READ: return ev_read(c);
    case EV_SAVE: return ev_save(c);
    default: return 1;
  }
}
