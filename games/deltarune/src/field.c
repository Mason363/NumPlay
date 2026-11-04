#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Field (rooms.h): Lancer's words on the way, the candy trees, the glowing tiles, the Hathy fight, Susie
 * back, the puzzle pieces and Seam's door, Lancer scared off, the box puzzle and the suits' secret room. Each
 * cutscene is the original's event object as a protothread, its texts its own. */
#include <stdio.h>
#include <string.h>
#include "battle.h"
#include "cut.h"
#include "game.h"
#include "gfx.h"
#include "gmath.h"
#include "gmtexts.h"
#include "puzzles.h"
#include "rooms.h"

enum {
  EV_F2_LANCER = EV_FIELD_FIRST, EV_HATHYFIGHT, EV_RIBBON, EV_GETSUSIE, EV_GETSUSIE_R, EV_PM1_LECTURE, EV_PM1_TALK,
  EV_PM2_TALK, EV_SCARE_LANCER, EV_BOXPUZZLE, EV_BOX_LEAVE, EV_BOX_PUSH, EV_BOX_TALK, EV_BOX_WALKOFF, EV_SECRET_SPIKES
};

static struct {
  int evx;                 /* the room's obj_event_room */
  int candyflag;
  int con, rcon, lecture, boxon, boxcon, leavecon, equipcon, pcon;
  int pm1_x, pm1_y;
  int siner;
} F;

void chest_equip_hint(void) { F.equipcon = 1; }

/* ---------------------------------------------------------------- the rooms as they start */
static void npc_at_event(void) {
  Inst in;
  F.evx = world_inst(obj_event_room, 0, &in) ? in.x : 0;
}

/* the box puzzle's party, standing (obj_npc_facing made by the event) */
static void box_party_npcs(int sx, int sy, int rx, int ry) {
  Obj *s = room_add_npc(obj_npc_facing, sx, sy);
  if (s) s->name = ACT_S, s->spr = spr_susied_dark, s->sprs[0] = spr_susied_dark, s->sprs[1] = spr_susier_dark,
         s->sprs[2] = spr_susieu_dark, s->sprs[3] = spr_susiel_dark;
  Obj *r = room_add_npc(obj_npc_facing, rx, ry);
  if (r) r->name = ACT_R, r->spr = spr_ralseid, r->sprs[0] = spr_ralseid, r->sprs[1] = spr_ralseir,
         r->sprs[2] = spr_ralseiu, r->sprs[3] = spr_ralseil;
}

void field_setup(int room) {
  Inst in;
  memset(&F, 0, sizeof F);
  npc_at_event();
  switch (room) {
    case room_field2:
      if (plot() < 34) room_queue_event(EV_F2_LANCER);
      break;
    case room_field2A: case room_field_puzzle1:
      F.candyflag = room == room_field2A ? 101 : 102;
      if (flag(F.candyflag) < 2 && world_inst(obj_npc_room, 0, &in)) {
        Obj *t = mk(spr_candytree_candies, in.x, in.y, ACT_X5);
        t->img = flag(F.candyflag) * FX;
        t->depth_bias = 5;
      }
      if (room == room_field_puzzle1) glow_setup();
      break;
    case room_field_puzzle2: glow_setup(); break;
    case room_field_maze:
      if (world_inst(obj_hathyfightevent, 0, &in) && plot() < 36) {
        mk(spr_lancer_dt, in.x, in.y, ACT_LANCER);
        F.con = 1;
      }
      break;
    case room_field_getsusie:
      if (plot() >= 38) {
        Obj *d = mk(spr_drdoor, 400, 40, ACT_PROP);
        d->img = FX, d->flags |= OF_BACK;
        for (int k = 0; k < 2; k++) {
          Obj *b = obj_new(0xFFFF, 400 + 80 * k, 120);
          b->flags |= OF_SOLID | OF_HIDDEN;
          obj_set_box(b, 0, 0, 40, 40);
        }
        Obj *door = obj_new(0xFFFF, 440, 120);
        door->flags |= OF_TOUCH | OF_HIDDEN, door->obj = obj_doorA, door->event = EV_DOOR;
        obj_set_box(door, 0, 0, 40, 40);
        room_add_npc(obj_npc_sign, 520, 260);
        if (plot() == 38) F.rcon = 1;
        if (plot() < 40) chaser_add(250, 240, 30);
      } else {
        Obj *d = mk(spr_drdoor, 400, 40, ACT_PROP);
        d->flags |= OF_BACK;
        Obj *b = obj_new(0xFFFF, 400, 120);
        b->flags |= OF_SOLID | OF_HIDDEN;
        obj_set_box(b, 0, 0, 120, 40);
      }
      if (plot() < 37 && world_inst(obj_getsusieevent, 0, &in)) {
        mk(spr_susieu_dark, in.x, in.y, ACT_SUSIE);
        F.con = 1;
      }
      break;
    case room_field_shop1:
      if (flag(215) != 1 && world_inst(obj_npc_puzzlemaster1, 0, &in)) {
        Obj *p = mk(spr_npc_puzzlepiece, in.x, in.y, ACT_NPC1);
        p->flags |= OF_TALK | OF_SOLID, p->event = EV_PM1_TALK;
        obj_set_box(p, 0, 40, 68, 20);
        Obj *w = obj_new(0xFFFF, in.x + 10, in.y - 20);
        w->name = ACT_X1, w->flags |= OF_SOLID | OF_HIDDEN;
        obj_set_box(w, 0, 0, 40, 120);
      }
      F.lecture = plot() >= 42 || flag(211) == 3 ? 100 : 0;
      break;
    case room_field_puzzletutorial:
      for (int k = 0; k < 4 && world_inst(obj_npc_puzzlemaster2, k, &in); k++) {
        Obj *p = obj_new(0xFFFF, in.x, in.y);
        int type = in.x < 150 ? 3 : in.x < 250 ? 2 : in.x < 350 ? 1 : 0;
        p->flags |= OF_TALK | OF_SOLID | OF_HIDDEN, p->event = EV_PM2_TALK, p->arg = (int16_t)type;
        p->name = (uint8_t)(ACT_X1 + type);
        obj_set_box(p, 0, 40, 68, 20);
        if (type == 3) {
          Obj *h = mk(spr_donation_hole_and_tree, 270, 126, 0);
          h->img = FX, h->flags |= OF_BACK;
        }
      }
      break;
    case room_field3:
      if (plot() < 45 && world_inst(obj_scarelancerevent, 0, &in)) {
        mk(spr_lancer_rt, in.x, in.y, ACT_LANCER);
        F.con = 1;
      }
      break;
    case room_field_boxpuzzle:
      if (world_inst(obj_boxpuzzle_event, 0, &in)) {
        Obj *fence = mk(spr_fencedoor, in.x, in.y, ACT_DOOR);
        fence->flags |= OF_BACK;
        if (plot() >= 50) {
          fence->img = (flag(212) == 2 ? 2 : 1) * FX;
          F.con = -1;
        } else {
          Obj *g = obj_new(0xFFFF, in.x, 120);
          g->name = ACT_DOOR2, g->flags |= OF_SOLID | OF_HIDDEN;
          obj_set_box(g, 0, 0, 80, 40);
          F.con = 1;
          room_queue_event(EV_BOXPUZZLE);
        }
      }
      for (int k = 0; k < 2 && world_inst(obj_pushableblock, k, &in); k++) {
        if (flag(212) == 2) break;
        Obj *b = mk(spr_npc_block, in.x, in.y, ACT_NPC1 + k);
        b->flags |= OF_TALK | OF_SOLID, b->event = EV_BOX_PUSH;
        b->arg = (int16_t)(plot() >= 50 || F.con == 1 ? 1 : 0);   /* event: 1 talks, 0 pushes */
        obj_set_box(b, 0, 10, 40, 30);
      }
      /* the switches the boxes go on (obj_blocktile: lit while something stands on them) */
      for (int k = 0; k < 2 && world_inst(obj_blocktile, k, &in); k++) {
        Obj *t = mk(spr_glowtile_step, in.x, in.y, ACT_X1 + k);
        t->flags |= OF_BACK, t->ispd = FX / 5;
      }
      break;
    case room_field_secret1: {
      static const int8_t sol[4] = {2, 4, 3, 1};
      suits_setup(1000, 100, 4, sol, false);
      for (int k = 0; k < 2; k++) {
        Obj *s = mk(spr_spiketile, 1300, 240 + 40 * k, ACT_X1 + k);
        s->flags |= OF_BACK;
      }
      Obj *w = obj_new(0xFFFF, 1300, 240);
      w->name = ACT_X3, w->flags |= OF_SOLID | OF_HIDDEN;
      obj_set_box(w, 0, 0, 40, 120);
      break;
    }
    default: break;
  }
}

/* ---------------------------------------------------------------- each frame */
static bool idle(void) { return !event_running() && !text_active() && !world_fading() && !pl.frozen; }

void field_step(void) {
  F.siner++;
  switch (g_room) {
    case room_field2A: case room_field_puzzle1: {
      Obj *t = obj_find(ACT_X5);
      if (t) {
        t->alpha = (uint8_t)clamp_i((int)((0.7f + gm_dsin(F.siner / 8.f * 57.2958f) * 0.3f) * 255), 0, 255);
        t->img = flag(F.candyflag) * FX;
        if (flag(F.candyflag) >= 2) obj_free(t);
      }
      break;
    }
    case room_field_maze:
      if (F.con == 1 && pl.x >= 1300 && pl.y > 900 && idle()) F.con = 2, event_start(EV_HATHYFIGHT, NULL);
      if (F.equipcon == 1 && (sv.armor1[CH_RALSEI] == 4 || sv.armor2[CH_RALSEI] == 4) && have_char(CH_RALSEI) && idle())
        F.equipcon = 2, event_start(EV_RIBBON, NULL);
      break;
    case room_field_getsusie:
      if (F.con == 1 && pl.x >= 430 - 80 && idle()) F.con = 2, event_start(EV_GETSUSIE, NULL);
      if (F.rcon == 1 && pl.x >= 80 && idle()) F.rcon = 2, event_start(EV_GETSUSIE_R, NULL);
      break;
    case room_field_shop1:
      if (F.lecture == 0 && pl.y <= 270 && idle()) F.lecture = 1, event_start(EV_PM1_LECTURE, NULL);
      break;
    case room_field3: {
      Inst in;
      if (F.con == 1 && world_inst(obj_scarelancerevent, 0, &in) && pl.x >= 680 && idle())
        F.con = 2, event_start(EV_SCARE_LANCER, NULL);
      break;
    }
    case room_field_boxpuzzle:
      for (int k = 0; k < 2; k++) {
        Obj *t = obj_find(ACT_X1 + k);
        if (!t) continue;
        int x = t->x / FX, y = t->y / FX;
        bool on = party_on(x + 10, y + 6, x + 30, y + 18, true);
        for (int b = 0; b < 2; b++) {
          Obj *o = obj_find(ACT_NPC1 + b);
          if (o && o->x / FX == x && o->y / FX == y) on = true;
        }
        if (on && t->spr != spr_glowtile_on) t->spr = spr_glowtile_on, t->img = 0, t->ispd = 0;
        if (!on && t->spr != spr_glowtile_step) t->spr = spr_glowtile_step, t->ispd = FX / 5;
      }
      if (F.leavecon == 1 && idle() && pl.x < 70) F.leavecon = 2, event_start(EV_BOX_LEAVE, NULL);
      if (F.con == 20 && idle()) event_start(EV_BOX_TALK, NULL);
      break;
    case room_field_secret1:
      if (F.pcon == 0 && suits_won() && idle()) F.pcon = 1, event_start(EV_SECRET_SPIKES, NULL);
      break;
    default: break;
  }
}

void field_draw(void) {
  if (g_room == room_field_puzzletutorial) {
    /* the puzzle pieces (obj_npc_puzzlemaster2's draw): a body, a face and beanies in their colours */
    static const uint32_t blends[4] = {0xFF0000, 0xFFA040, 0xFFFF00, 0x800080};
    for (int i = 0; i < MAX_OBJS; i++) {
      const Obj *o = &objs[i];
      if ((o->flags & OF_GONE) || o->event != EV_PM2_TALK) continue;
      int type = o->arg, x = (o->x / FX - cam_x) / 2, y = (o->y / FX - cam_y) / 2;
      uint16_t bl = rgb565(blends[type]);
      bool talking = text_active() && o->img;
      int sh = talking ? (int)(gm_dsin(F.siner / 4.f * 57.2958f) * 1.5f) : 0;
      gfx_sprite(spr_puzzlemaster_body, type, x, y - sh, 0, 255);
      gfx_sprite_blend(spr_puzzlemaster_face0, type, x + 12, y + 8 + sh / 2, 0, bl, 255);
      int beanie[3] = {type >= 1, type == 3, type >= 2};
      static const int bx[3] = {12, 1, 22}, by[3] = {-1, -4, -4};
      for (int k = 0; k < 3; k++)
        if (beanie[k]) gfx_sprite_blend(spr_puzzlemaster_beanie, type, x + bx[k], y + by[k] - sh, 0, bl, 255);
    }
  }
}

/* ---------------------------------------------------------------- the events */
/* obj_event_room in room_field2: Lancer's warning */
static int ev_f2_lancer(Ev *c) {
  EV_BEGIN;
  FREE(true);
  WAIT_UNTIL(pl.x >= F.evx && !text_active());
  FREE(false);
  if (flag(210) == 1) {
    SAY(FC_LANCER, 0, obj_event_room_slash_Step_0_gml_11_0, F_('R', 11), obj_event_room_slash_Step_0_gml_13_0, F_('L', 3),
        obj_event_room_slash_Step_0_gml_15_0);
  } else {
    set_flag(210, 2);
    SAY(FC_LANCER, 6, obj_event_room_slash_Step_0_gml_20_0, F_('R', 11), obj_event_room_slash_Step_0_gml_22_0, F_('L', 2),
        obj_event_room_slash_Step_0_gml_24_0);
  }
  if (plot() < 34) set_plot(34);
  EV_END;
}

/* obj_hathyfightevent: Lancer sends three Hathys; he comes into the battle to see why they lose */
static const int hf_clean[] = {obj_hathyfightevent_slash_Step_0_gml_79_0, F_('R', 11), obj_hathyfightevent_slash_Step_0_gml_81_0,
                               obj_hathyfightevent_slash_Step_0_gml_82_0, obj_hathyfightevent_slash_Step_0_gml_83_0, F_('L', 0),
                               obj_hathyfightevent_slash_Step_0_gml_85_0, obj_hathyfightevent_slash_Step_0_gml_86_0, F_('R', 8),
                               obj_hathyfightevent_slash_Step_0_gml_88_0, -1};
static const int hf_won[] = {obj_hathyfightevent_slash_Step_0_gml_136_0, F_('R', 11), obj_hathyfightevent_slash_Step_0_gml_138_0,
                             F_('L', 4), obj_hathyfightevent_slash_Step_0_gml_140_0, obj_hathyfightevent_slash_Step_0_gml_141_0,
                             F_('R', 10), obj_hathyfightevent_slash_Step_0_gml_143_0, F_('L', 1),
                             obj_hathyfightevent_slash_Step_0_gml_145_0, -1};
static int ev_hathyfight(Ev *c) {
  EV_BEGIN;
  pl.frozen = true;
  set_spr(ACT_LANCER, spr_lancer_lt, 0);
  exc_over(act(ACT_LANCER)->x / FX + 36, act(ACT_LANCER)->y / FX - 10);
  WAIT(30);
  SAY(FC_LANCER, 0, obj_hathyfightevent_slash_Step_0_gml_19_0, obj_hathyfightevent_slash_Step_0_gml_20_0,
      obj_hathyfightevent_slash_Step_0_gml_21_0);
  go_hv(ACT_LANCER, 16 * FX, 0, 0);
  WAIT(10);
  gone(ACT_LANCER);
  if (plot() < 36) set_plot(36);
  {
    int y = 1120 - cam_y;
    battle_cameo(BC_CLEAN_TURN, spr_lancer_lt, y, FC_LANCER, 0, hf_clean);
    battle_cameo(BC_VICTORY, spr_lancer_lt, y, FC_LANCER, 2, hf_won);
    battle_victory_text(obj_hathyfightevent_slash_Step_0_gml_116_0, FC_RALSEI, 8);
  }
  pl.frozen = false;
  BATTLE(9);
  room_add_npc(obj_npc_room, 1740, 1080);
  F.con = 37;
  EV_END;
}

/* the White Ribbon on Ralsei (equipcon) */
static int ev_ribbon(Ev *c) {
  EV_BEGIN;
  if (have_char(CH_SUSIE))
    SAY(FC_RALSEI, 1, obj_hathyfightevent_slash_Step_0_gml_186_0, F_('S', 0), obj_hathyfightevent_slash_Step_0_gml_188_0, F_('R', 8),
        obj_hathyfightevent_slash_Step_0_gml_190_0, F_('S', 7), obj_hathyfightevent_slash_Step_0_gml_192_0);
  else
    SAY(FC_RALSEI, 1, obj_hathyfightevent_slash_Step_0_gml_181_0, obj_hathyfightevent_slash_Step_0_gml_182_0,
        obj_hathyfightevent_slash_Step_0_gml_183_0);
  F.equipcon = -1;
  EV_END;
}

/* obj_getsusieevent: Susie, back; she joins (at a distance) */
static int ev_getsusie(Ev *c) {
  Obj *s = act(ACT_SUSIE);
  EV_BEGIN;
  pl.frozen = true;
  pl.dir = DIR_UP;
  SAY(FC_SUSIE, 9, obj_getsusieevent_slash_Step_0_gml_16_0);
  s->spr = spr_susied_dark;
  exc_over(s->x / FX + 26, s->y / FX - 30);
  WAIT(30);
  SAY(FC_SUSIE, 0, obj_getsusieevent_slash_Step_0_gml_35_0, F_('R', 6), obj_getsusieevent_slash_Step_0_gml_37_0,
      obj_getsusieevent_slash_Step_0_gml_38_0, F_('S', 1), obj_getsusieevent_slash_Step_0_gml_40_0,
      obj_getsusieevent_slash_Step_0_gml_41_0, F_('R', 8), obj_getsusieevent_slash_Step_0_gml_43_0,
      obj_getsusieevent_slash_Step_0_gml_44_0, F_('S', 1), obj_getsusieevent_slash_Step_0_gml_46_0, F_('R', 1),
      obj_getsusieevent_slash_Step_0_gml_48_0, obj_getsusieevent_slash_Step_0_gml_49_0, obj_getsusieevent_slash_Step_0_gml_50_0,
      obj_getsusieevent_slash_Step_0_gml_51_0, F_('S', 0), obj_getsusieevent_slash_Step_0_gml_53_0,
      obj_getsusieevent_slash_Step_0_gml_54_0, F_('R', 8), obj_getsusieevent_slash_Step_0_gml_56_0, F_('S', 0),
      obj_getsusieevent_slash_Step_0_gml_58_0);
  /* she walks down, then left past the party */
  s->spr = spr_susied_dark, s->vs = 5 * FX, s->ispd = FX / 5;
  WAIT_UNTIL(act(ACT_SUSIE)->y / FX >= 200);
  s->spr = spr_susiel_dark, s->vs = 0, s->hs = -5 * FX;
  {
    int rx, ry;
    follower_xy(CH_RALSEI, &rx, &ry);
    c->v[0] = rx;
  }
  WAIT_UNTIL(act(ACT_SUSIE)->x / FX <= c->v[0] - 60);
  s->spr = spr_susier_dark, s->hs = 0, s->ispd = 0, s->img = 0;
  pl.dir = DIR_LEFT;
  WAIT(30);
  SAY(FC_SUSIE, 0, obj_getsusieevent_slash_Step_0_gml_105_0, F_('R', 8), obj_getsusieevent_slash_Step_0_gml_107_0);
  /* (the fanfare: "Susie joined the party!", unskippable, 340 frames) */
  say_unskippable(obj_getsusieevent_slash_Step_0_gml_118_0);
  WAIT(340);
  WAIT_UNTIL(!text_active());
  SAY(FC_SUSIE, 8, obj_getsusieevent_slash_Step_0_gml_144_0);
  s->spr = spr_susiel_dark, s->hs = -FX, s->ispd = FX / 10;
  say_unskippable(obj_getsusieevent_slash_Step_0_gml_160_0);
  WAIT(320);
  WAIT_UNTIL(!text_active());
  gone(ACT_SUSIE);
  /* scr_losechar, then Ralsei and Susie (the original's order) */
  sv.chr[1] = sv.chr[2] = 0;
  party_add(CH_RALSEI);
  party_add(CH_SUSIE);
  if (plot() < 37) set_plot(37);
  pl.dir = DIR_DOWN;
  pl.frozen = false;
  F.con = 14;
  EV_END;
}

static int ev_getsusie_r(Ev *c) {
  EV_BEGIN;
  if (flag(205) == 0)
    SAY(FC_RALSEI, 3, obj_getsusieevent_slash_Step_0_gml_201_0, obj_getsusieevent_slash_Step_0_gml_202_0,
        obj_getsusieevent_slash_Step_0_gml_203_0);
  else
    SAY(FC_RALSEI, 3, obj_getsusieevent_slash_Step_0_gml_196_0, obj_getsusieevent_slash_Step_0_gml_197_0,
        obj_getsusieevent_slash_Step_0_gml_198_0);
  F.rcon = 3;
  if (plot() < 39) set_plot(39);
  EV_END;
}

/* obj_npc_puzzlemaster1: Ralsei explains the puzzle pieces; the one at the door wants a toll */
static int ev_pm1_lecture(Ev *c) {
  EV_BEGIN;
  SAY(FC_RALSEI, 1, obj_npc_puzzlemaster1_slash_Step_0_gml_60_0, obj_npc_puzzlemaster1_slash_Step_0_gml_61_0,
      obj_npc_puzzlemaster1_slash_Step_0_gml_62_0, F_('S', 0), obj_npc_puzzlemaster1_slash_Step_0_gml_64_0, F_('R', 6),
      obj_npc_puzzlemaster1_slash_Step_0_gml_66_0, obj_npc_puzzlemaster1_slash_Step_0_gml_67_0,
      obj_npc_puzzlemaster1_slash_Step_0_gml_68_0, F_('S', 2), obj_npc_puzzlemaster1_slash_Step_0_gml_70_0, F_('R', 8),
      obj_npc_puzzlemaster1_slash_Step_0_gml_72_0, obj_npc_puzzlemaster1_slash_Step_0_gml_73_0);
  F.lecture = 10;
  if (plot() < 42) set_plot(42);
  EV_END;
}

/* the puzzle pieces' talk (scr_text 225, 230...): their faces change with the text's \M cues */
static int ev_pm1_talk(Ev *c) {
  Obj *p = act(ACT_NPC1);
  EV_BEGIN;
  g_signal_obj = -1;
  text_set_cue(0);
  talk_msc(225, 0);
  p->ispd = FX / 5;
  while (text_active()) {
    /* (its \M cues: shocked, then more) */
    p = act(ACT_NPC1);
    p->spr = text_cue() == 1 ? spr_npc_puzzlepiece_shock1 : text_cue() == 2 ? spr_npc_puzzlepiece_shock2 : spr_npc_puzzlepiece;
    WAIT(1);
  }
  p = act(ACT_NPC1);
  p->spr = spr_npc_puzzlepiece, p->ispd = 0, p->img = 0;
  if (g_signal_obj == obj_npc_puzzlemaster1 && g_signal_con == 5) {
    gone(ACT_X1);
    p->hs = -8 * FX, p->ispd = FX / 5;
    WAIT(30);
    pl.dir = DIR_DOWN;
    set_flag(215, 1);
    gone(ACT_NPC1);
  }
  EV_END;
}

static int ev_pm2_talk(Ev *c) {
  EV_BEGIN;
  text_set_cue(0);
  c->self->img = 1;
  talk_msc(230 + 5 * c->self->arg, 0);
  WAIT_UNTIL(!text_active());
  c->self->img = 0;
  EV_END;
}

/* obj_scarelancerevent: Lancer in the way; Susie grabs him, he calls his Jigsawries */
static int ev_scare_lancer(Ev *c) {
  EV_BEGIN;
  pl.dir = DIR_RIGHT;
  party_markers();
  set_spr(ACT_K, spr_krisr_dark, 0), set_spr(ACT_S, spr_susier_dark, 0), set_spr(ACT_R, spr_ralseir, 0);
  move_over(ACT_S, pl.x - 120, pl.y + 76 - 92, 15);
  move_over(ACT_R, pl.x - 60, pl.y + 76 - 88, 15);
  {
    Obj *l = act(ACT_LANCER);
    exc_over(l->x / FX + 20, l->y / FX - 10);
    l->spr = spr_lancer_lt;
  }
  WAIT(30);
  WAIT(15);
  SAY_TOP(FC_LANCER, 3, obj_scarelancerevent_slash_Step_0_gml_49_0, obj_scarelancerevent_slash_Step_0_gml_50_0,
          obj_scarelancerevent_slash_Step_0_gml_51_0, obj_scarelancerevent_slash_Step_0_gml_52_0,
          obj_scarelancerevent_slash_Step_0_gml_53_0, obj_scarelancerevent_slash_Step_0_gml_54_0);
  SAY_TOP(FC_SUSIE, 0, obj_scarelancerevent_slash_Step_0_gml_66_0, obj_scarelancerevent_slash_Step_0_gml_67_0,
          obj_scarelancerevent_slash_Step_0_gml_68_0, F_('L', 6), obj_scarelancerevent_slash_Step_0_gml_70_0,
          obj_scarelancerevent_slash_Step_0_gml_71_0, F_('S', 1), obj_scarelancerevent_slash_Step_0_gml_73_0, F_('L', 7),
          obj_scarelancerevent_slash_Step_0_gml_75_0, F_('S', 2), obj_scarelancerevent_slash_Step_0_gml_77_0);
  /* Susie walks up past the others (the view follows) */
  cam_pan(2 * FX, 0, 60);
  go_hv(ACT_S, 2 * FX, 0, FX / 5);
  WAIT_UNTIL(act(ACT_S)->x / FX > pl.x + 60);
  halt_act(ACT_S);
  set_spr(ACT_R, spr_ralseir, 0), set_spr(ACT_K, spr_krisr_dark, 0);
  SAY_TOP(FC_SUSIE, 2, obj_scarelancerevent_slash_Step_0_gml_134_0, F_('L', 4), obj_scarelancerevent_slash_Step_0_gml_136_0,
          F_('S', 0), obj_scarelancerevent_slash_Step_0_gml_138_0, obj_scarelancerevent_slash_Step_0_gml_139_0,
          obj_scarelancerevent_slash_Step_0_gml_140_0);
  go_hv(ACT_S, FX, 0, FX / 10);
  WAIT(20);
  halt_act(ACT_S);
  SAY_TOP(FC_SUSIE, 2, obj_scarelancerevent_slash_Step_0_gml_163_0, F_('L', 5), obj_scarelancerevent_slash_Step_0_gml_165_0,
          F_('S', 1), obj_scarelancerevent_slash_Step_0_gml_167_0);
  {
    Obj *s = act(ACT_S), *l = act(ACT_LANCER);
    c->v[0] = (l->y / FX - s->y / FX + 70 - 92) * FX / 30;
    go_hv(ACT_S, 2 * FX, c->v[0], FX / 5);
    go_hv(ACT_LANCER, FX, 0, 0);
  }
  WAIT(30);
  /* she has him */
  halt_act(ACT_S), halt_act(ACT_LANCER);
  act(ACT_LANCER)->flags |= OF_HIDDEN;
  set_spr(ACT_S, spr_susie_grablancer, 0);
  WAIT(30);
  for (c->v[1] = 0; c->v[1] < 12; c->v[1]++) {   /* (image_index + 0.25 a frame to 3) */
    act(ACT_S)->img = c->v[1] * FX / 4;
    WAIT(1);
  }
  WAIT(30);
  SAY_TOP(FC_SUSIE, 3, obj_scarelancerevent_slash_Step_0_gml_215_0);
  for (c->v[1] = 12; c->v[1] < 28; c->v[1]++) {
    act(ACT_S)->img = c->v[1] * FX / 4;
    WAIT(1);
  }
  WAIT(30);
  SAY_TOP(FC_SUSIE, 3, obj_scarelancerevent_slash_Step_0_gml_234_0, obj_scarelancerevent_slash_Step_0_gml_235_0);
  set_spr(ACT_S, spr_susie_laughlancer, FX / 4);
  text_clear();
  text_face(FC_SUSIE, 3);
  text_add(str(obj_scarelancerevent_slash_Step_0_gml_249_0));
  text_open();
  WAIT_UNTIL(!text_active());
  WAIT(30);
  set_spr(ACT_S, spr_susie_finelancer, 0);
  SAY(FC_LANCER, 3, obj_scarelancerevent_slash_Step_0_gml_270_0, obj_scarelancerevent_slash_Step_0_gml_271_0, F_('S', 10),
      obj_scarelancerevent_slash_Step_0_gml_273_0, F_('L', 3), obj_scarelancerevent_slash_Step_0_gml_275_0,
      obj_scarelancerevent_slash_Step_0_gml_276_0, F_('S', 7), obj_scarelancerevent_slash_Step_0_gml_278_0, F_('L', 2),
      obj_scarelancerevent_slash_Step_0_gml_280_0);
  /* he slips away; three Jigsawries come */
  {
    Obj *s = act(ACT_S), *l = act(ACT_LANCER);
    exc_over(s->x / FX + 20, s->y / FX - 10);
    s->spr = spr_susier_dark, s->img = 0;
    l->flags &= (uint16_t)~OF_HIDDEN;
    l->y -= 14 * FX;
    l->hs = 8 * FX, l->vs = 2 * FX;
  }
  WAIT(5);
  halt_act(ACT_LANCER);
  WAIT(10);
  for (c->v[1] = 0; c->v[1] < 3; c->v[1]++) {
    mk(spr_jigsawry_idle, cam_x + 700, act(ACT_LANCER)->y / FX - 20, ACT_X1 + c->v[1]);
    move_over(ACT_X1 + c->v[1], cam_x + 640 - 140 + 60 - 40 * c->v[1], act(ACT_LANCER)->y / FX - 20 + 50 * c->v[1] - 50, 30);
  }
  WAIT(30);
  SAY(FC_LANCER, 3, obj_scarelancerevent_slash_Step_0_gml_328_0, obj_scarelancerevent_slash_Step_0_gml_329_0);
  go_hv(ACT_LANCER, 16 * FX, 0, 0);
  SAY(FC_LANCER, 3, obj_scarelancerevent_slash_Step_0_gml_340_0);
  cam_pan_to(pl.x - (320 - 19), cam_y, 20);
  for (c->v[1] = 0; c->v[1] < 3; c->v[1]++) go_hv(ACT_X1 + c->v[1], -4 * FX, 0, 0);
  move_over(ACT_K, pl.x, pl.y, 20);
  {
    int sx, sy, rx, ry;
    follower_xy(CH_SUSIE, &sx, &sy), follower_xy(CH_RALSEI, &rx, &ry);
    move_over(ACT_S, sx, sy, 20), move_over(ACT_R, rx, ry, 20);
  }
  set_spr(ACT_S, spr_susier_dark, 0);
  WAIT(30);
  for (c->v[1] = 0; c->v[1] < 3; c->v[1]++) halt_act(ACT_X1 + c->v[1]);
  party_back();
  cam_follow();
  gone(ACT_LANCER);
  SAY(FC_SUSIE, 0, obj_scarelancerevent_slash_Step_0_gml_374_0);
  for (c->v[1] = 0; c->v[1] < 3; c->v[1]++) gone(ACT_X1 + c->v[1]);
  if (plot() < 45) set_plot(45);
  BATTLE(22);
  EV_END;
}

/* ---------------------------------------------------------------- the box puzzle (obj_boxpuzzle_event) */
static int ev_boxpuzzle(Ev *c) {
  EV_BEGIN;
  /* the party walks in; Ralsei sees the fence */
  pl.dir = DIR_RIGHT;
  {
    int sx, sy, rx, ry;
    follower_xy(CH_SUSIE, &sx, &sy), follower_xy(CH_RALSEI, &rx, &ry);
    mk(spr_susier_dark, pl.x - 100, sy, ACT_S);
    mk(spr_ralseir, pl.x - 50, ry, ACT_R);
    mk(spr_krisr_dark, pl.x, pl.y, ACT_K);
  }
  pl.hidden = true, pl.frozen = true;
  follow_show(false);
  go_hv(ACT_S, 4 * FX, 0, FX / 5), go_hv(ACT_R, 4 * FX, 0, FX / 5), go_hv(ACT_K, 4 * FX, 0, FX / 5);
  WAIT(40);
  halt_act(ACT_S), halt_act(ACT_R), halt_act(ACT_K);
  pl.x = act(ACT_K)->x / FX, pl.hidden = false;
  gone(ACT_K);
  exc_over(act(ACT_R)->x / FX + 20, act(ACT_R)->y / FX - 20);
  WAIT(30);
  SAY(FC_RALSEI, 8, obj_boxpuzzle_event_slash_Step_0_gml_39_0, F_('S', 0), obj_boxpuzzle_event_slash_Step_0_gml_41_0, F_('R', 8),
      obj_boxpuzzle_event_slash_Step_0_gml_43_0);
  set_spr(ACT_S, spr_susieu_dark, 0);
  pl.dir = DIR_UP;
  set_spr(ACT_R, spr_ralseiu, FX / 4);
  act(ACT_R)->vs = -6 * FX;
  WAIT_UNTIL(act(ACT_R)->y / FX <= 100);
  halt_act(ACT_R);
  set_spr(ACT_R, spr_ralseid, 0);
  WAIT(10);
  SAY(FC_RALSEI, 3, obj_boxpuzzle_event_slash_Step_0_gml_82_0, obj_boxpuzzle_event_slash_Step_0_gml_83_0,
      obj_boxpuzzle_event_slash_Step_0_gml_84_0, obj_boxpuzzle_event_slash_Step_0_gml_85_0, F_('S', 2),
      obj_boxpuzzle_event_slash_Step_0_gml_87_0);
  pl.dir = DIR_RIGHT;
  set_spr(ACT_S, spr_susier_dark, FX / 3);
  act(ACT_S)->hs = 12 * FX;
  WAIT_UNTIL(act(ACT_S)->x / FX >= 480);
  act(ACT_S)->hs = 0, act(ACT_S)->vs = -12 * FX, act(ACT_S)->spr = spr_susieu_dark;
  WAIT_UNTIL(act(ACT_S)->y / FX <= 100);
  halt_act(ACT_S);
  WAIT(10);
  set_spr(ACT_S, spr_susied_dark, 0);
  SAY(FC_SUSIE, 1, obj_boxpuzzle_event_slash_Step_0_gml_134_0, F_('R', 3), obj_boxpuzzle_event_slash_Step_0_gml_136_0, F_('S', 2),
      obj_boxpuzzle_event_slash_Step_0_gml_138_0, F_('R', 8), obj_boxpuzzle_event_slash_Step_0_gml_140_0);
  set_flag(212, 1);
  for (int k = 0; k < 2; k++) act(ACT_NPC1 + k)->arg = 0;
  {
    Obj *s = act(ACT_S), *r = act(ACT_R);
    c->v[0] = s->x / FX, c->v[1] = s->y / FX, c->v[2] = r->x / FX, c->v[3] = r->y / FX;
  }
  gone(ACT_S), gone(ACT_R);
  box_party_npcs(c->v[0], c->v[1], c->v[2], c->v[3]);
  pl.frozen = false;
  F.boxcon = 1, F.leavecon = 1, F.con = 20;
  EV_END;
}

static int ev_box_leave(Ev *c) {
  EV_BEGIN;
  pl.x = 70, pl.dir = DIR_RIGHT;
  SAY(FC_SUSIE, 7, obj_boxpuzzle_event_slash_Step_0_gml_170_0);
  F.leavecon = 1;
  EV_END;
}

/* how the boxes sit: on their tiles (spr_npc_block_solved) */
static int boxes_solved(void) {
  int n = 0;
  for (int k = 0; k < 2; k++) {
    Obj *b = obj_find(ACT_NPC1 + k);
    if (b && b->spr == spr_npc_block_solved) n++;
  }
  return n;
}
static int tiles_stepped(void) {
  Inst in;
  int n = 0;
  for (int k = 0; world_inst(obj_blocktile, k, &in); k++)
    if (party_on(in.x + 10, in.y + 6, in.x + 30, in.y + 18, true)) n++;
  return n;
}

/* con 20: what Ralsei and Susie say as the boxes go on (and once both are on, the fence opens) */
static int ev_box_talk(Ev *c) {
  EV_BEGIN;
  {
    int boxes = boxes_solved(), tiles = tiles_stepped();
    if (boxes == 0 && F.boxon == 2) {
      F.boxon = 3;
      SAY(FC_RALSEI, 1, obj_boxpuzzle_event_slash_Step_0_gml_207_0, F_('S', 2), obj_boxpuzzle_event_slash_Step_0_gml_209_0);
      F.boxon = 4;
    } else if (boxes == 1 && F.boxon == 0) {
      F.boxon = 1;
      SAY(FC_RALSEI, 6, obj_boxpuzzle_event_slash_Step_0_gml_220_0, F_('S', 2), obj_boxpuzzle_event_slash_Step_0_gml_222_0,
          obj_boxpuzzle_event_slash_Step_0_gml_223_0);
      F.boxon = 2;
    } else if (boxes >= 2) {
      act(ACT_DOOR)->img = FX;
      gone(ACT_DOOR2);
      g_shake = 30;
      F.con = 25;
      WAIT(30);
      SAY(FC_RALSEI, 0, obj_boxpuzzle_event_slash_Step_0_gml_295_0, F_('S', 0), obj_boxpuzzle_event_slash_Step_0_gml_297_0,
          obj_boxpuzzle_event_slash_Step_0_gml_298_0);
      pl.dir = DIR_UP;
      {
        Obj *s = act(ACT_S), *r = act(ACT_R);
        c->v[0] = s->x / FX, c->v[1] = s->y / FX, c->v[2] = r->x / FX, c->v[3] = r->y / FX;
      }
      gone(ACT_S), gone(ACT_R);
      mk(spr_ralseir, c->v[2], c->v[3], ACT_R);
      mk(spr_susiel_dark, c->v[0], c->v[1], ACT_S);
      F.con = 28;
      room_queue_event(EV_BOX_WALKOFF);
    } else if (tiles == 2 && boxes == 1 && F.boxcon == 1) {
      F.boxcon = 2;
      SAY(FC_RALSEI, 6, obj_boxpuzzle_event_slash_Step_0_gml_248_0, obj_boxpuzzle_event_slash_Step_0_gml_249_0, F_('S', 1),
          obj_boxpuzzle_event_slash_Step_0_gml_251_0, obj_boxpuzzle_event_slash_Step_0_gml_252_0, F_('R', 1),
          obj_boxpuzzle_event_slash_Step_0_gml_254_0, F_('S', 2), obj_boxpuzzle_event_slash_Step_0_gml_256_0);
      F.boxcon = 3;
    }
  }
  EV_END;
}

/* con 28: through the open fence, Susie first, then Ralsei (the party gathers again) */
static int ev_box_walkoff(Ev *c) {
  EV_BEGIN;
  pl.frozen = true;
  pl.dir = DIR_UP;
  set_spr(ACT_S, spr_susiel_dark, FX / 3);
  if (act(ACT_S)->x / FX > 320 - 20) {
    act(ACT_S)->hs = -8 * FX;
    WAIT_UNTIL(act(ACT_S)->x / FX <= 320 - 20);
  }
  act(ACT_S)->hs = 0, act(ACT_S)->vs = -8 * FX, act(ACT_S)->spr = spr_susieu_dark;
  WAIT(60);
  gone(ACT_S);
  set_spr(ACT_R, spr_ralseid, 0);
  SAY_LOW(FC_RALSEI, 8, obj_boxpuzzle_event_slash_Step_0_gml_347_0);
  set_spr(ACT_R, spr_ralseir, FX / 4);
  if (act(ACT_R)->x / FX < 320 - 20) {
    act(ACT_R)->hs = 6 * FX;
    WAIT_UNTIL(act(ACT_R)->x / FX >= 320 - 20);
  }
  act(ACT_R)->hs = 0, act(ACT_R)->vs = -6 * FX, act(ACT_R)->spr = spr_ralseiu;
  WAIT(30);
  gone(ACT_R);
  for (int k = 0; k < 2; k++) act(ACT_NPC1 + k)->arg = 1;
  F.con = 35;
  if (plot() < 50) set_plot(50);
  pl.dir = DIR_DOWN;
  F.leavecon = 0;
  pl.frozen = false;
  follow_reset();
  follow_show(true);
  EV_END;
}

/* Z at a box: before the puzzle (and after), what it is; in it, a push (40 units, if the way is clear) */
static int ev_box_push(Ev *c) {
  Obj *b = c->self;
  EV_BEGIN;
  if (b->arg == 0) {
    int dir = pl.dir, ix = b->x / FX, iy = b->y / FX;
    static const int dx[4] = {0, 1, 0, -1}, dy[4] = {1, 0, -1, 0};
    ix += dx[dir] * 40, iy += dy[dir] * 40;
    bool ok = world_box_free(ix + 2, iy + 2, ix + 17, iy + 17, true);
    for (int k = 0; k < 2; k++) {
      Obj *o = obj_find(ACT_NPC1 + k);
      if (o && o != b && ix + 17 > o->x / FX && ix + 2 < o->x / FX + 40 && iy + 17 > o->y / FX && iy + 2 < o->y / FX + 40)
        ok = false;
    }
    if (!ok) return 1;
    c->v[0] = dir;
    b->spr = spr_npc_block;
    for (c->v[1] = 0; c->v[1] < 5; c->v[1]++) {
      b->x += dx[c->v[0]] * 8 * FX, b->y += dy[c->v[0]] * 8 * FX;
      WAIT(1);
    }
    {
      /* on a tile now? (obj_blocktile's collision with the block) */
      Inst in;
      for (int k = 0; world_inst(obj_blocktile, k, &in); k++)
        if (b->x / FX == in.x && b->y / FX == in.y) b->spr = spr_npc_block_solved;
    }
    return 1;
  }
  text_clear();
  if (flag(212) == 0) text_add(str(obj_pushableblock_slash_Other_10_gml_61_0));
  if (plot() >= 50) text_add(str(obj_pushableblock_slash_Other_10_gml_66_0));
  if (flag(212) == 2 && plot() < 50) {
    static const int ids[] = {obj_pushableblock_slash_Other_10_gml_74_0, obj_pushableblock_slash_Other_10_gml_75_0,
                              obj_pushableblock_slash_Other_10_gml_76_0, obj_pushableblock_slash_Other_10_gml_77_0, -1};
    text_face(FC_SUSIE, 9);
    say_add(ids);
    text_side(1);
    c->v[2] = 1;
  }
  text_open();
  WAIT_UNTIL(!text_active());
  if (!c->v[2]) return 1;
  /* con 50: the dark, a crash, and the fence is open (the boxes in pieces) */
  pl.frozen = true;
  for (c->v[1] = 0; c->v[1] < 30; c->v[1]++) {
    g_fade_col = 0, g_fade = (uint8_t)(c->v[1] * 255 / 30);
    WAIT(1);
  }
  room_add_npc(obj_npc_room, 290, 200);
  room_add_npc(obj_npc_room, 460, 370);
  {
    Obj *s = obj_find(ACT_S), *r = obj_find(ACT_R);
    c->v[3] = s ? s->y / FX : 200, c->v[4] = r ? r->x / FX : 300, c->v[5] = r ? r->y / FX : 200;
    if (s) obj_free(s);
    if (r) obj_free(r);
  }
  mk(spr_ralseir, c->v[4], c->v[5], ACT_R);
  mk(spr_susied_dark, 320 - 22, c->v[3], ACT_S);
  WAIT(30);
  act(ACT_DOOR)->img = 2 * FX;
  gone(ACT_DOOR2);
  gone(ACT_NPC1), gone(ACT_NPC1 + 1);
  pl.dir = DIR_UP;
  for (c->v[1] = 30; c->v[1] > 0; c->v[1]--) {
    g_fade = (uint8_t)(c->v[1] * 255 / 30);
    WAIT(1);
  }
  g_fade = 0;
  WAIT(20);
  SAY_LOW(FC_RALSEI, 8, obj_boxpuzzle_event_slash_Step_0_gml_437_0);
  F.con = 28;
  room_queue_event(EV_BOX_WALKOFF);
  pl.frozen = false;
  EV_END;
}

/* the secret room's suits: right, the spikes go down */
static int ev_secret_spikes(Ev *c) {
  EV_BEGIN;
  gone(ACT_X3);
  act(ACT_X1)->img = FX, act(ACT_X2)->img = FX;
  g_shake = 20;
  WAIT(20);
  F.pcon = 3;
  EV_END;
}

int field_event(Ev *c) {
  switch (c->id) {
    case EV_F2_LANCER: return ev_f2_lancer(c);
    case EV_HATHYFIGHT: return ev_hathyfight(c);
    case EV_RIBBON: return ev_ribbon(c);
    case EV_GETSUSIE: return ev_getsusie(c);
    case EV_GETSUSIE_R: return ev_getsusie_r(c);
    case EV_PM1_LECTURE: return ev_pm1_lecture(c);
    case EV_PM1_TALK: return ev_pm1_talk(c);
    case EV_PM2_TALK: return ev_pm2_talk(c);
    case EV_SCARE_LANCER: return ev_scare_lancer(c);
    case EV_BOXPUZZLE: return ev_boxpuzzle(c);
    case EV_BOX_LEAVE: return ev_box_leave(c);
    case EV_BOX_TALK: return ev_box_talk(c);
    case EV_BOX_PUSH: return ev_box_push(c);
    case EV_BOX_WALKOFF: return ev_box_walkoff(c);
    case EV_SECRET_SPIKES: return ev_secret_spikes(c);
    default: return 1;
  }
}
