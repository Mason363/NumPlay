#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Forest (rooms.h): the Rabbicks in the bushes, Susie with Lancer's gang, the dark Ponmen, the suits
 * puzzles, Clover's holes, the Bake Sale and the smith, the scissor dancers and the maze. Each cutscene is the
 * original's event object as a protothread, its texts its own. */
#include <stdio.h>
#include <string.h>
#include "battle.h"
#include "cut.h"
#include "game.h"
#include "gfx.h"
#include "gmath.h"
#include "gmtexts.h"
#include "menu.h"
#include "puzzles.h"
#include "rooms.h"

enum {
  EV_BADGUY = EV_FOREST_FIRST, EV_SUITS1, EV_SUITS1_LEAVE, EV_SUITS1_WON, EV_BEFORECLOVER_SPIKES, EV_CLOVER_N, EV_CLOVER,
  EV_BAKESALE_OFF, EV_BAKESALE_TALK, EV_SMITH, EV_HAZARD
};

static struct {
  int con, pcon, talked;
  int bushx[2], bushy[2], bushcon[2];
  float siner;
} FO;

/* ---------------------------------------------------------------- hazards (obj_overworldbulletparent) */
/* what hurts on the overworld: the dark Ponmen's diamonds, the scissor dancers. Touched, every member loses
 * the damage (down to 1 HP); all at 1, it's over (scr_damage_all_overworld) */
#define MAX_HZ 40
typedef struct {
  bool on;
  float x, y;
  uint16_t spr;
  int16_t angle;              /* 1/256 turn */
  uint8_t alpha;
  int8_t bl, bt, br, bb;      /* its box from (x, y), units */
  int dmg;
} Hazard;
static Hazard hz[MAX_HZ];
static int ow_inv;

static Hazard *hz_new(uint16_t spr, int dmg, int bl, int bt, int br, int bb) {
  for (int i = 0; i < MAX_HZ; i++)
    if (!hz[i].on) {
      Hazard *h = &hz[i];
      memset(h, 0, sizeof *h);
      h->on = true, h->spr = spr, h->dmg = dmg, h->alpha = 255;
      h->bl = (int8_t)bl, h->bt = (int8_t)bt, h->br = (int8_t)br, h->bb = (int8_t)bb;
      return h;
    }
  static Hazard spare;
  return &spare;
}

void ow_damage(int dmg) {
  if (ow_inv > 0) return;
  g_shake = 6;
  bool any = false;
  for (int k = 0; k < 3; k++) {
    int c = sv.chr[k];
    if (!c) continue;
    int d = dmg;
    if (d >= sv.hp[c]) d = sv.hp[c] - 1;
    if (d > 1) sv.hp[c] = (int16_t)(sv.hp[c] - d);
    if (sv.hp[c] < 1) sv.hp[c] = 1;
    if (sv.hp[c] > 1) any = true;
  }
  ow_inv = 40;
  if (!any) menu_gameover_start();
}

static void hz_step(void) {
  if (ow_inv > 0) ow_inv--;
  if (event_running() || pl.frozen) return;
  for (int i = 0; i < MAX_HZ; i++) {
    Hazard *h = &hz[i];
    if (!h->on || h->alpha < 128) continue;
    if (party_on((int)h->x + h->bl, (int)h->y + h->bt, (int)h->x + h->br, (int)h->y + h->bb, false)) ow_damage(h->dmg);
  }
}

static void hz_draw(void) {
  for (int i = 0; i < MAX_HZ; i++) {
    Hazard *h = &hz[i];
    if (!h->on || !h->alpha || h->spr == 0xFFFF) continue;
    int x = ((int)h->x - cam_x) / 2, y = ((int)h->y - cam_y) / 2;
    if (h->angle) gfx_sprite_ex(h->spr, 0, x, y, 256, 256, h->angle, 0, h->alpha);
    else gfx_sprite(h->spr, 0, x, y, 0, h->alpha);
  }
}

/* the dark Ponmen (obj_darkponman_ow): an eye in a dark Ponman, and a ring of diamonds swinging round it */
#define MAX_DP 4
static struct {
  bool on;
  int x, y, bmax;
  float rot, rotspeed, eyer, siner;
  int con;
  int8_t hz0;
} DP[MAX_DP];

static void darkponman_add(int x, int y) {
  for (int k = 0; k < MAX_DP; k++)
    if (!DP[k].on) {
      DP[k].on = true, DP[k].x = x, DP[k].y = y, DP[k].rot = 0, DP[k].rotspeed = 4, DP[k].bmax = 8;
      DP[k].eyer = 0, DP[k].con = 0, DP[k].siner = 0;
      const RoomInfo *r = world_room_info();
      if (g_room == room_forest_area2 && x >= r->w / 2) DP[k].rotspeed = -4, DP[k].rot = 180;
      if (g_room == room_forest_area5) DP[k].rotspeed = 2.5f, DP[k].bmax = 11;
      if (g_room == room_cc_2f) {
        DP[k].rotspeed = 3;
        if ((x >= 800 && y <= 800) || (x <= 800 && y >= 800)) DP[k].rotspeed = -3, DP[k].rot = 180;
      }
      DP[k].hz0 = -1;
      for (int i = 0; i < DP[k].bmax; i++) {
        Hazard *h = hz_new(spr_diamondbullet, 16, -6, -6, 6, 6);
        h->alpha = 0;
        if (DP[k].hz0 < 0) DP[k].hz0 = (int8_t)(h - hz);
      }
      Obj *o = obj_new(spr_ponman_dark, x, y);
      o->ispd = FX / 10;
      return;
    }
}

static void darkponman_step(void) {
  for (int k = 0; k < MAX_DP; k++) {
    if (!DP[k].on) continue;
    typeof(DP[0]) *d = &DP[k];
    d->rot += d->rotspeed;
    if (d->con == 0) {
      if (d->eyer <= 10) d->eyer += 1;
      else d->con = 1;
    }
    d->siner += d->rotspeed > 0 ? 1 : -1;
    for (int i = 0; i < d->bmax; i++) {
      Hazard *h = &hz[d->hz0 + i];
      float a = d->rot - gm_dsin(d->siner / 10 * 57.2958f) * (i * 2);
      h->x = d->x + lengthdir_x(d->eyer * (2 + i * 2), a) + 30, h->y = d->y + lengthdir_y(d->eyer * (2 + i * 2), a) + 30;
      float al = d->con < 1 ? d->eyer / 10 : 0.8f + gm_dsin((i * 4 + d->siner) / 5 * 57.2958f) * 0.2f;
      h->alpha = (uint8_t)clamp_i((int)(al * 255), 0, 255);
      h->angle = (int16_t)(-(d->rot - gm_dsin(d->siner / 10 * 57.2958f) * (i * 4)) * 256 / 360);
    }
  }
}

static void darkponman_draw(void) {
  for (int k = 0; k < MAX_DP; k++)
    if (DP[k].on)
      gfx_sprite(spr_ponman_eye_white, 0, (DP[k].x + 30 + (int)lengthdir_x(DP[k].eyer, DP[k].rot) - cam_x) / 2,
                 (DP[k].y + 30 + (int)lengthdir_y(DP[k].eyer, DP[k].rot) - cam_y) / 2, 0, 255);
}

/* ---------------------------------------------------------------- the rooms as they start */
void forest_party_npcs(int lx, int ly, int sx, int sy);
static Obj *clover_holes[3];
void forest_setup(int room) {
  Inst in;
  memset(&FO, 0, sizeof FO);
  memset(hz, 0, sizeof hz);
  memset(DP, 0, sizeof DP);
  for (int k = 0; k < 2 && world_inst(obj_rabbick_bush, k, &in); k++) FO.bushx[k] = in.x, FO.bushy[k] = in.y, FO.bushcon[k] = 1;
  for (int k = 0; k < MAX_DP && world_inst(obj_darkponman_ow, k, &in); k++) darkponman_add(in.x, in.y);
  switch (room) {
    case room_forest_area1:
      if (plot() < 70 && world_inst(obj_susiebadguy_event, 0, &in)) {
        mk(spr_lancer_rt, in.x, in.y, ACT_LANCER);
        mk(spr_susier_dark, in.x + 80, in.y - 16, ACT_SUSIE);
        FO.con = 1;
      }
      mk(spr_donation_hole_and_tree, 1120, 752, ACT_PROP)->flags |= OF_BACK;
      break;
    case room_forest_puzzle1: {
      static const int8_t sol[3] = {3, 4, 3};
      suits_setup(180, 60, 3, sol, true);
      for (int i = 0; i < 3; i++) mk(spr_spiketile, 520, 240 + i * 40, ACT_X1 + i)->flags |= OF_BACK;
      Obj *w = obj_new(0xFFFF, 520, 240);
      w->name = ACT_X4, w->flags |= OF_SOLID | OF_HIDDEN;
      obj_set_box(w, 0, 0, 40, 120);
      if (plot() < 71) set_plot(71), FO.con = 1;
      else if (plot() == 71) FO.con = 15;
      if (plot() >= 72) {
        suits_force(3, (const int8_t[]){3, 4, 3});
        gone(ACT_X4);
        for (int i = 0; i < 3; i++) act(ACT_X1 + i)->img = FX;
        FO.con = 999;
        break;
      }
      mk(spr_thrashdesign, 400, 160, ACT_PROP);
      if (world_inst(obj_suitspuzzle_event, 0, &in)) {
        mk(spr_lancer_lt, in.x, in.y, ACT_LANCER);
        mk(spr_susieu_dark, 372, 170, ACT_SUSIE);
        if (FO.con == 15) {
          /* (they stand by as npcs; talking to them is theirs) */
          act(ACT_LANCER)->flags |= OF_HIDDEN, act(ACT_SUSIE)->flags |= OF_HIDDEN;
          forest_party_npcs(in.x, in.y + 100, 372, 270);
        }
      }
      break;
    }
    case room_forest_beforeclover: {
      static const int8_t sol[3] = {1, 2, 3};
      suits_setup(180, 140, 3, sol, false);
      for (int k = 0; k < 3; k++) {
        Obj *h = mk(spr_suitsicon_fade, k == 0 ? 320 : k == 1 ? 840 : 1400, 680, 0);
        h->img = k * FX, h->flags |= OF_BACK;
      }
      mk(spr_spiketile, 400, 240, ACT_X1)->flags |= OF_BACK;
      mk(spr_spiketile, 440, 240, ACT_X2)->flags |= OF_BACK;
      Obj *w = obj_new(0xFFFF, 400, 240);
      w->name = ACT_X3, w->flags |= OF_SOLID | OF_HIDDEN;
      obj_set_box(w, 0, 0, 80, 40);
      if (flag(290) == 1) {
        act(ACT_X1)->img = FX, act(ACT_X2)->img = FX;
        gone(ACT_X3);
        suits_force(3, (const int8_t[]){1, 2, 3});
        FO.pcon = 3;
      }
      break;
    }
    case room_forest_area3: {
      static const int8_t sol[4] = {9, 9, 9, 9};
      suits_setup(500, 440, 3, sol, false);
      mk(spr_spiketile, 520, 280, ACT_X1)->flags |= OF_BACK;
      mk(spr_spiketile, 560, 280, ACT_X2)->flags |= OF_BACK;
      if (flag(234) == 0) {
        Obj *w = obj_new(0xFFFF, 520, 280);
        w->name = ACT_X3, w->flags |= OF_SOLID | OF_HIDDEN;
        obj_set_box(w, 0, 0, 80, 40);
      } else {
        act(ACT_X1)->img = FX, act(ACT_X2)->img = FX;
      }
      for (int k = 0; k < 3; k++) {
        clover_holes[k] = NULL;
        if (!world_inst(obj_cloverhole, k, &in)) continue;
        Obj *h = mk(spr_cloverhole, in.x, in.y, ACT_X4 + k);
        h->arg = (int16_t)(in.x < 600 ? 0 : in.x >= 1000 ? 2 : 1);
        h->depth_bias = 0;
        h->tx = plot() >= 75 ? 999 : 0;   /* (holecon) */
      }
      FO.con = plot() < 75 ? 0 : 99;
      break;
    }
    case room_forest_savepoint2:
      if (world_inst(obj_lancerbakesale_event, 0, &in)) {
        if (plot() >= 80 || flag(238) == 1) {
          FO.con = 99;
        } else {
          mk(spr_susied_dark, in.x + 20, in.y + 60, ACT_SUSIE);
          mk(spr_lancer_dt, in.x + 60, in.y + 80, ACT_LANCER);
          FO.con = 1;
        }
      }
      break;
    case room_forest_smith:
      if (world_inst(obj_npc_hammerguy, 0, &in)) {
        Obj *h = mk(spr_hammerguy, in.x, in.y, ACT_NPC1);
        h->ispd = FX / 20, h->flags |= OF_TALK | OF_SOLID, h->event = EV_SMITH;
        obj_set_box(h, 0, 0, 82, 104);
      }
      break;
    default: break;
  }
}

/* Lancer and Susie standing by in the suits room (obj_npc_facing, their faces turning to Kris) */
void forest_party_npcs(int lx, int ly, int sx, int sy) {
  Obj *l = room_add_npc(obj_npc_facing, lx, ly);
  if (l) {
    l->spr = spr_lancer_dt, l->sprs[0] = spr_lancer_dt, l->sprs[1] = spr_lancer_rt, l->sprs[2] = spr_lancer_ut;
    l->sprs[3] = spr_lancer_lt, l->name = ACT_NPC1;
  }
  Obj *s = room_add_npc(obj_npc_facing, sx, sy);
  if (s) {
    s->spr = spr_susiedt_dark, s->sprs[0] = spr_susiedt_dark, s->sprs[1] = spr_susiert_dark, s->sprs[2] = spr_susieut_dark;
    s->sprs[3] = spr_susielt_dark, s->name = ACT_NPC2;
  }
}

/* ---------------------------------------------------------------- each frame */
static bool idle(void) { return !event_running() && !text_active() && !world_fading() && !pl.frozen; }

void forest_step(void) {
  FO.siner++;
  hz_step();
  darkponman_step();
  /* the Rabbick bushes: near one, a Rabbick jumps out (obj_rabbick_bush) */
  for (int k = 0; k < 2; k++)
    if (FO.bushcon[k] == 1 && abs_i(pl.x + 19 - (FO.bushx[k] + 20)) <= 90 && pl.y >= FO.bushy[k]) {
      FO.bushcon[k] = 2;
      chaser_add(FO.bushx[k], FO.bushy[k], 200);
      chaser_kick(16);
    }
  switch (g_room) {
    case room_forest_area1: {
      Inst in;
      if (FO.con == 1 && world_inst(obj_susiebadguy_event, 0, &in) && pl.x >= in.x - 200 && idle())
        FO.con = 2, event_start(EV_BADGUY, NULL);
      break;
    }
    case room_forest_puzzle1:
      if (FO.con == 1 && pl.x >= 140 && idle()) FO.con = 2, event_start(EV_SUITS1, NULL);
      if (FO.con == 9 && pl.x < 40 && idle()) event_start(EV_SUITS1_LEAVE, NULL);
      if (FO.con == 15 && suits_won() && idle()) FO.con = 16, event_start(EV_SUITS1_WON, NULL);
      break;
    case room_forest_beforeclover:
      if (FO.pcon == 0 && suits_won() && idle()) FO.pcon = 1, event_start(EV_BEFORECLOVER_SPIKES, NULL);
      break;
    case room_forest_area3: {
      /* the north spikes: the vandalized hint's order (club, spade, diamond) */
      if (FO.pcon == 0 && flag(234) == 0 && suits_row(0) == 4 && suits_row(1) == 1 && suits_row(2) == 2 && idle())
        FO.pcon = 1, event_start(EV_CLOVER_N, NULL);
      if (FO.con == 0 && pl.y <= 80 && idle()) FO.con = 1, event_start(EV_CLOVER, NULL);
      /* the holes look up as Kris comes near (obj_cloverhole) */
      for (int k = 0; k < 3; k++) {
        Obj *h = obj_find(ACT_X4 + k);
        if (!h || h->tx == 999) continue;
        int hx = h->x / FX + 32, hy = h->y / FX + 70;
        int dx = abs_i(pl.x + 19 - hx) - 40, dy = abs_i(pl.y + 60 - hy) - 30;
        int dist = max_i(0, max_i(dx, dy));
        static const uint16_t faces[3] = {spr_cloverhole_happy, spr_cloverhole_shy, spr_cloverhole_mad};
        if (h->tx == 0 && dist <= 80) h->tx = 1, h->img = 0, h->spr = spr_cloverhole;
        if (h->tx == 1) {
          if (dist <= 100) {
            h->img += FX / 2;
            if (h->img >= 10 * FX) h->img = 0, h->spr = faces[h->arg], h->tx = 2, h->ispd = FX / 3;
          } else {
            h->img -= FX / 2;
            if (h->img <= 0) h->img = 0, h->tx = 0;
          }
        }
        if (h->tx == 2 && dist > 90) h->spr = spr_cloverhole, h->img = 10 * FX, h->ispd = 0, h->tx = 1;
      }
      break;
    }
    case room_forest_savepoint2:
      if (g_signal_obj == obj_lancerbakesale_event && g_signal_con == 10 && !text_active()) {
        g_signal_obj = -1;
        if (FO.con == 1) FO.con = 10, event_start(EV_BAKESALE_OFF, NULL);
      }
      break;
    default: break;
  }
}

void forest_draw(void) {
  darkponman_draw();
  hz_draw();
  if (g_room == room_forest_area3) {
    /* the vandalized hint (obj_cloverpuzzle_event's draw) */
    static const uint8_t f[3] = {3, 0, 1};
    for (int i = 0; i < 3; i++) gfx_sprite(spr_suitsicon_vandal, f[i], (360 + 40 * i - cam_x) / 2, (280 - cam_y) / 2, 0, 255);
  }
}

/* ---------------------------------------------------------------- the events */
/* obj_susiebadguy_event: Susie, a member of Lancer's gang now */
static int ev_badguy(Ev *c) {
  EV_BEGIN;
  pl.frozen = true;
  pl.dir = DIR_RIGHT;
  set_spr(ACT_LANCER, spr_lancer_lt, 0);
  set_spr(ACT_SUSIE, spr_susiel_dark, 0);
  exc_over(act(ACT_SUSIE)->x / FX + 30, act(ACT_SUSIE)->y / FX - 20);
  cam_pan(4 * FX, 0, 30);
  WAIT(30);
  SAY(FC_RALSEI, 10, obj_susiebadguy_event_slash_Step_0_gml_30_0, F_('S', 2), obj_susiebadguy_event_slash_Step_0_gml_32_0, F_('L', 1),
      obj_susiebadguy_event_slash_Step_0_gml_34_0, F_('S', 2), obj_susiebadguy_event_slash_Step_0_gml_36_0, F_('L', 2),
      obj_susiebadguy_event_slash_Step_0_gml_38_0, F_('S', 4), obj_susiebadguy_event_slash_Step_0_gml_40_0);
  set_spr(ACT_LANCER, spr_lancer_dt, 0);
  g_shake = 5;
  WAIT(5);
  SAY(FC_LANCER, 2, obj_susiebadguy_event_slash_Step_0_gml_60_0);
  set_spr(ACT_SUSIE, spr_susied_dark, 0);
  g_shake = 5;
  WAIT(5);
  SAY(FC_SUSIE, 2, obj_susiebadguy_event_slash_Step_0_gml_79_0);
  set_spr(ACT_LANCER, spr_lancer_rt, 0);
  set_spr(ACT_SUSIE, spr_susiel_dark, 0);
  WAIT(5);
  SAY(FC_SUSIE, 3, obj_susiebadguy_event_slash_Step_0_gml_97_0);
  /* the white flash, and their pose */
  for (c->v[0] = 1; c->v[0] <= 60; c->v[0]++) {
    int a = c->v[0] <= 25 ? c->v[0] * 10 : c->v[0] <= 28 ? 255 : max_i(0, 255 - (c->v[0] - 28) * 10);
    g_fade_col = C_WHITE, g_fade = (uint8_t)clamp_i(a, 0, 255);
    if (c->v[0] == 26) {
      set_spr(ACT_LANCER, spr_lancer_pose, 0);
      Obj *s = act(ACT_SUSIE);
      s->spr = spr_susieb_spellready, s->ispd = FX / 5, s->flags |= OF_FLIPX, s->x += 60 * FX;
    }
    WAIT(1);
  }
  g_fade = 0, g_fade_col = 0;
  text_clear();
  text_face(FC_LANCER, 3);
  text_add(str(obj_susiebadguy_event_slash_Step_0_gml_158_0));
  if (flag(214) == 1) text_add(str(obj_susiebadguy_event_slash_Step_0_gml_161_0));
  if (flag(214) == 2) text_add(str(obj_susiebadguy_event_slash_Step_0_gml_165_0));
  text_open();
  WAIT_UNTIL(!text_active());
  WAIT(30);
  SAY(FC_RALSEI, 8, obj_susiebadguy_event_slash_Step_0_gml_185_0);
  {
    Obj *s = act(ACT_SUSIE);
    s->img = 0, s->spr = spr_susiel_dark_unhappy, s->ispd = 0, s->flags &= (uint16_t)~OF_FLIPX, s->x -= 60 * FX;
    set_spr(ACT_LANCER, spr_lancer_lt_unhappy, 0);
  }
  SAY(FC_SUSIE, 6, obj_susiebadguy_event_slash_Step_0_gml_205_0, obj_susiebadguy_event_slash_Step_0_gml_206_0,
      obj_susiebadguy_event_slash_Step_0_gml_207_0, F_('L', 6), obj_susiebadguy_event_slash_Step_0_gml_209_0, F_('R', 1),
      obj_susiebadguy_event_slash_Step_0_gml_211_0, obj_susiebadguy_event_slash_Step_0_gml_212_0);
  set_spr(ACT_LANCER, spr_lancer_lt, 0), set_spr(ACT_SUSIE, spr_susiel_dark, 0);
  SAY(FC_SUSIE, 1, obj_susiebadguy_event_slash_Step_0_gml_224_0, F_('L', 2), obj_susiebadguy_event_slash_Step_0_gml_226_0, F_('S', 2),
      obj_susiebadguy_event_slash_Step_0_gml_228_0, F_('L', 2), obj_susiebadguy_event_slash_Step_0_gml_230_0);
  set_spr(ACT_SUSIE, spr_susier_dark, 0);
  WAIT(30);
  SAY(FC_SUSIE, 1, obj_susiebadguy_event_slash_Step_0_gml_247_0);
  set_spr(ACT_LANCER, spr_lancer_rt, 0);
  WAIT(30);
  SAY(FC_LANCER, 3, obj_susiebadguy_event_slash_Step_0_gml_265_0);
  set_spr(ACT_LANCER, spr_lancer_dt, 0), act(ACT_LANCER)->vs = 16 * FX;
  set_spr(ACT_SUSIE, spr_susied_dark, FX / 2), act(ACT_SUSIE)->vs = 16 * FX;
  WAIT(30);
  gone(ACT_LANCER), gone(ACT_SUSIE);
  cam_pan(-4 * FX, 0, 30);
  WAIT(30);
  cam_follow();
  pl.dir = DIR_DOWN;
  pl.frozen = false;
  set_plot(70);
  FO.con = 30;
  EV_END;
}

/* obj_suitspuzzle_event: Susie and Lancer at the suits; they wait for it to be solved */
static int ev_suits1(Ev *c) {
  EV_BEGIN;
  pl.dir = DIR_RIGHT;
  SAY(FC_SUSIE, 0, obj_suitspuzzle_event_slash_Step_0_gml_16_0, F_('L', 3), obj_suitspuzzle_event_slash_Step_0_gml_18_0, F_('S', 2),
      obj_suitspuzzle_event_slash_Step_0_gml_20_0);
  exc_over(act(ACT_SUSIE)->x / FX + 30, act(ACT_SUSIE)->y / FX - 20);
  set_spr(ACT_SUSIE, spr_susiel_dark, 0), set_spr(ACT_LANCER, spr_lancer_lt, 0);
  WAIT(30);
  SAY(FC_RALSEI, 8, obj_suitspuzzle_event_slash_Step_0_gml_41_0, F_('S', 7), obj_suitspuzzle_event_slash_Step_0_gml_43_0, F_('L', 6),
      obj_suitspuzzle_event_slash_Step_0_gml_45_0, F_('S', 0), obj_suitspuzzle_event_slash_Step_0_gml_47_0, F_('R', 6),
      obj_suitspuzzle_event_slash_Step_0_gml_49_0);
  set_spr(ACT_SUSIE, spr_susieu_dark, 0), set_spr(ACT_LANCER, spr_lancer_ut, 0);
  WAIT(30);
  set_spr(ACT_SUSIE, spr_susiel_dark, 0), set_spr(ACT_LANCER, spr_lancer_lt, 0);
  SAY(FC_RALSEI, 0, obj_suitspuzzle_event_slash_Step_0_gml_71_0, F_('S', 0), obj_suitspuzzle_event_slash_Step_0_gml_73_0, F_('R', 6),
      obj_suitspuzzle_event_slash_Step_0_gml_75_0, F_('S', 0), obj_suitspuzzle_event_slash_Step_0_gml_77_0,
      obj_suitspuzzle_event_slash_Step_0_gml_78_0, F_('L', 3), obj_suitspuzzle_event_slash_Step_0_gml_80_0, F_('S', 2),
      obj_suitspuzzle_event_slash_Step_0_gml_82_0, obj_suitspuzzle_event_slash_Step_0_gml_83_0);
  pl.dir = DIR_DOWN;
  {
    Obj *l = act(ACT_LANCER), *s = act(ACT_SUSIE);
    l->flags |= OF_HIDDEN, s->flags |= OF_HIDDEN;
    forest_party_npcs(l->x / FX, l->y / FX + 100, s->x / FX, s->y / FX + 100);
  }
  FO.con = 9;
  EV_END;
}

/* leaving that way: Susie stops Kris */
static int ev_suits1_leave(Ev *c) {
  EV_BEGIN;
  pl.dir = DIR_RIGHT, pl.x = 32;
  if (FO.talked == 0) SAY(FC_SUSIE, 0, obj_suitspuzzle_event_slash_Step_0_gml_133_0);
  else SAY(FC_SUSIE, 0, obj_suitspuzzle_event_slash_Step_0_gml_137_0);
  FO.talked++;
  pl.x = 32;
  EV_END;
}

static void spikes_down(int first, int n) {
  for (int i = 0; i < n; i++) {
    Obj *o = obj_find(first + i);
    if (o) o->img = FX;
  }
}

/* solved: the spikes go down, they run off with the design */
static int ev_suits1_won(Ev *c) {
  EV_BEGIN;
  pl.dir = DIR_RIGHT;
  g_shake = 10;
  gone(ACT_X4);
  spikes_down(ACT_X1, 3);
  gone(ACT_NPC1), gone(ACT_NPC2);
  {
    Obj *l = act(ACT_LANCER), *s = act(ACT_SUSIE);
    l->flags &= (uint16_t)~OF_HIDDEN, s->flags &= (uint16_t)~OF_HIDDEN;
    l->spr = spr_lancer_rt, s->spr = spr_susier_dark;
  }
  WAIT(60);
  set_spr(ACT_SUSIE, spr_susiel_dark, 0), set_spr(ACT_LANCER, spr_lancer_lt, 0);
  SAY(FC_SUSIE, 0, obj_suitspuzzle_event_slash_Step_0_gml_189_0);
  set_spr(ACT_SUSIE, spr_susier_dark, FX / 2), set_spr(ACT_LANCER, spr_lancer_rt, 0);
  act(ACT_SUSIE)->hs = 14 * FX, act(ACT_LANCER)->hs = 14 * FX;
  SAY(FC_SUSIE, 2, obj_suitspuzzle_event_slash_Step_0_gml_200_0, F_('L', 2), obj_suitspuzzle_event_slash_Step_0_gml_202_0);
  WAIT(30);
  /* back for the design */
  {
    Obj *s = act(ACT_SUSIE), *l = act(ACT_LANCER);
    s->x = (cam_x + 640) * FX, s->spr = spr_susiel_dark, s->hs = -12 * FX;
    l->x = (cam_x + 690) * FX, l->spr = spr_lancer_lt, l->hs = -12 * FX;
  }
  WAIT_UNTIL(act(ACT_SUSIE)->x / FX <= 380);
  halt_act(ACT_SUSIE), halt_act(ACT_LANCER);
  WAIT(15);
  set_spr(ACT_SUSIE, spr_susieu_dark, 0), set_spr(ACT_LANCER, spr_lancer_ut, 0);
  SAY(FC_SUSIE, 0, obj_suitspuzzle_event_slash_Step_0_gml_247_0);
  set_spr(ACT_SUSIE, spr_susier_dark, FX / 2), set_spr(ACT_LANCER, spr_lancer_rt, 0);
  act(ACT_SUSIE)->hs = 12 * FX, act(ACT_LANCER)->hs = 12 * FX, act(ACT_PROP)->hs = 12 * FX;
  WAIT(30);
  gone(ACT_SUSIE), gone(ACT_LANCER), gone(ACT_PROP);
  pl.dir = DIR_DOWN;
  set_plot(72);
  FO.con = 23;
  EV_END;
}

static int ev_beforeclover_spikes(Ev *c) {
  EV_BEGIN;
  gone(ACT_X3);
  spikes_down(ACT_X1, 2);
  g_shake = 20;
  WAIT(20);
  set_flag(290, 1);
  FO.pcon = 3;
  EV_END;
}

static int ev_clover_n(Ev *c) {
  EV_BEGIN;
  gone(ACT_X3);
  set_flag(234, 1);
  spikes_down(ACT_X1, 2);
  g_shake = 20;
  WAIT(20);
  FO.pcon = 0;
  EV_END;
}

/* obj_cloverpuzzle_event: Clover climbs out of the holes */
static int ev_clover(Ev *c) {
  EV_BEGIN;
  pl.dir = DIR_DOWN;
  pl.frozen = true;
  {
    Inst in;
    world_inst(obj_cloverpuzzle_event, 0, &in);
    Obj *cl = mk(spr_clubs_idle, in.x, in.y, ACT_X6);
    cl->ispd = FX / 5, cl->vs = -8 * FX;
  }
  WAIT(22);
  WAIT_UNTIL(act(ACT_X6)->y / FX <= 280);
  halt_act(ACT_X6);
  act(ACT_X6)->ispd = FX / 5;
  text_clear();
  {
    static const int a[] = {obj_cloverpuzzle_event_slash_Step_0_gml_78_0, obj_cloverpuzzle_event_slash_Step_0_gml_79_0,
                            obj_cloverpuzzle_event_slash_Step_0_gml_80_0, -1};
    static const int b[] = {obj_cloverpuzzle_event_slash_Step_0_gml_85_0, obj_cloverpuzzle_event_slash_Step_0_gml_86_0, -1};
    say_add(flag(236) == 1 ? b : a);
  }
  text_open();
  WAIT_UNTIL(!text_active());
  if (plot() < 75) set_plot(75);
  for (int k = 0; k < 3; k++) {
    Obj *h = obj_find(ACT_X4 + k);
    if (h) h->spr = spr_cloverhole, h->tx = 999, h->img = 0, h->ispd = 0;
  }
  gone(ACT_X6);
  pl.frozen = false;
  BATTLE(8);
  FO.con = 4;
  EV_END;
}

/* the Bake Sale: once Kris has bought, Susie and Lancer are off (obj_lancerbakesale_event, con 10) */
static int ev_bakesale_off(Ev *c) {
  EV_BEGIN;
  set_spr(ACT_SUSIE, spr_susier_dark, FX / 3), set_spr(ACT_LANCER, spr_lancer_rt, 0);
  act(ACT_SUSIE)->hs = 12 * FX, act(ACT_LANCER)->hs = 12 * FX;
  WAIT(20);
  {
    Obj *s = act(ACT_SUSIE), *l = act(ACT_LANCER);
    s->x = (cam_x - 140) * FX, s->y += 90 * FX, s->hs = 12 * FX, s->vs = 0;
    l->x = (cam_x - 100) * FX, l->y += 90 * FX, l->hs = 12 * FX;
  }
  WAIT(29);
  halt_act(ACT_SUSIE), halt_act(ACT_LANCER);
  set_spr(ACT_SUSIE, spr_susieu_dark, 0), set_spr(ACT_LANCER, spr_lancer_ut, 0);
  pl.dir = DIR_LEFT;
  WAIT(5);
  set_spr(ACT_SUSIE, spr_susier_dark, 0), set_spr(ACT_LANCER, spr_lancer_lt, 0);
  SAY(FC_SUSIE, 1, obj_lancerbakesale_event_slash_Step_0_gml_80_0, F_('L', 2), obj_lancerbakesale_event_slash_Step_0_gml_82_0);
  set_spr(ACT_LANCER, spr_lancer_ut, 0), set_spr(ACT_SUSIE, spr_susieu_dark, 0);
  WAIT(30);
  set_spr(ACT_LANCER, spr_lancer_lt, 0), set_spr(ACT_SUSIE, spr_susier_dark, 0);
  SAY(FC_LANCER, 6, obj_lancerbakesale_event_slash_Step_0_gml_103_0, F_('S', 0), obj_lancerbakesale_event_slash_Step_0_gml_105_0,
      obj_lancerbakesale_event_slash_Step_0_gml_106_0, F_('L', 3), obj_lancerbakesale_event_slash_Step_0_gml_108_0,
      obj_lancerbakesale_event_slash_Step_0_gml_109_0);
  set_spr(ACT_SUSIE, spr_susier_dark, FX / 4), set_spr(ACT_LANCER, spr_lancer_rt, 0);
  act(ACT_SUSIE)->hs = 13 * FX, act(ACT_LANCER)->hs = 13 * FX;
  WAIT(6);
  halt_act(ACT_SUSIE), halt_act(ACT_LANCER);
  set_spr(ACT_SUSIE, spr_susieu_dark, 0), set_spr(ACT_LANCER, spr_lancer_ut, 0);
  act(ACT_SUSIE)->vs = -10 * FX, act(ACT_LANCER)->vs = -10 * FX;
  WAIT(6);
  halt_act(ACT_SUSIE), halt_act(ACT_LANCER);
  WAIT(10);
  set_spr(ACT_SUSIE, spr_susied_dark, FX / 4), set_spr(ACT_LANCER, spr_lancer_dt, 0);
  act(ACT_SUSIE)->vs = 12 * FX, act(ACT_LANCER)->vs = 12 * FX;
  WAIT(7);
  halt_act(ACT_SUSIE), halt_act(ACT_LANCER);
  {
    Obj *s = act(ACT_SUSIE), *l = act(ACT_LANCER);
    Obj *ln = room_add_npc(obj_npc_facing, l->x / FX, l->y / FX + 108);
    if (ln) ln->spr = spr_lancer_dt;
    Obj *sn = room_add_npc(obj_npc_facing, s->x / FX, s->y / FX + 108);
    if (sn) sn->spr = spr_susier_dark;
  }
  gone(ACT_SUSIE), gone(ACT_LANCER);
  if (plot() < 80) set_plot(80);
  set_flag(238, 1);
  pl.dir = DIR_DOWN;
  FO.con = 36;
  EV_END;
}

/* the smith (obj_npc_hammerguy): scr_text 315; he heals the party, fixes the cake, makes the Door Key */
static int ev_smith(Ev *c) {
  Obj *h = act(ACT_NPC1);
  EV_BEGIN;
  g_signal_obj = -1;
  h->ispd = FX / 5;
  talk_msc(315, -1);
  WAIT_UNTIL(!text_active());
  h->spr = spr_hammerguy, h->ispd = FX / 10;
  if (g_signal_obj != obj_npc_hammerguy) return 1;
  c->v[0] = g_signal_con;   /* 10 heal, 30 the cake, 50 the key */
  g_signal_obj = -1;
  /* powering up, then a hit on each member in turn (on the cake, on the key) */
  h = act(ACT_NPC1);
  h->spr = spr_hammerguy_powerup, h->img = 0, h->ispd = FX / 2;
  WAIT(30);
  for (c->v[1] = c->v[0] == 10 ? party_count() - 1 : 0; c->v[1] >= 0; c->v[1]--) {
    h = act(ACT_NPC1);
    h->spr = spr_hammerguy_hit, h->img = 0, h->ispd = FX / 2;
    if (c->v[0] == 10) {
      int ch = sv.chr[c->v[1]];
      Obj *m = mk(ch == CH_SUSIE ? spr_susie_shock : ch == CH_RALSEI ? spr_ralsei_shock_overworld : spr_krisd_dark,
                  ch == CH_SUSIE ? 380 : 390, ch == CH_SUSIE ? 110 : ch == CH_RALSEI ? 140 : 125, ACT_X1);
      (void)m;
      if (c->v[1] == 0) pl.hidden = true;
      follow_show(false);
    } else {
      mk(c->v[0] == 30 ? spr_brokencake : spr_brokenkey, 385, c->v[0] == 30 ? 160 : 170, ACT_X1);
    }
    WAIT(36);
    gone(ACT_X1);
  }
  pl.hidden = false;
  follow_show(true);
  h = act(ACT_NPC1);
  h->spr = spr_hammerguy, h->img = 0, h->ispd = FX / 20;
  if (c->v[0] == 10) {
    for (int ch = 1; ch <= 3; ch++) sv.hp[ch] = sv.maxhp[ch];
    TALK(obj_npc_hammerguy_slash_Step_0_gml_108_0, obj_npc_hammerguy_slash_Step_0_gml_109_0);
  } else if (c->v[0] == 30) {
    WAIT(14);
    gm_keyitemremove(3);
    TALK(obj_npc_hammerguy_slash_Step_0_gml_153_0, obj_npc_hammerguy_slash_Step_0_gml_154_0, obj_npc_hammerguy_slash_Step_0_gml_155_0);
  } else {
    WAIT(14);
    gm_keyitemremove(4), gm_keyitemremove(6), gm_keyitemremove(7);
    gm_keyitemget(5);
    TALK(obj_npc_hammerguy_slash_Step_0_gml_184_0, obj_npc_hammerguy_slash_Step_0_gml_185_0, obj_npc_hammerguy_slash_Step_0_gml_186_0,
         obj_npc_hammerguy_slash_Step_0_gml_187_0, obj_npc_hammerguy_slash_Step_0_gml_188_0);
  }
  EV_END;
}

int forest_event(Ev *c) {
  switch (c->id) {
    case EV_BADGUY: return ev_badguy(c);
    case EV_SUITS1: return ev_suits1(c);
    case EV_SUITS1_LEAVE: return ev_suits1_leave(c);
    case EV_SUITS1_WON: return ev_suits1_won(c);
    case EV_BEFORECLOVER_SPIKES: return ev_beforeclover_spikes(c);
    case EV_CLOVER_N: return ev_clover_n(c);
    case EV_CLOVER: return ev_clover(c);
    case EV_BAKESALE_OFF: return ev_bakesale_off(c);
    case EV_SMITH: return ev_smith(c);
    default: return 1;
  }
}

/* the maze's X door (obj_doorX in room_forest_maze1 and 2) */
int forest_maze_door(int *entrance) {
  (void)entrance;
  return g_room;
}
