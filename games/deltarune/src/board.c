#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")
#endif
/* The Great Board (rooms.h): its lit squares (obj_ob_checkertile, obj_ob_gen: touch one and the white takes
 * the party back to the start), the Ponmen in ambush (obj_ponman_appear), Lancer's team name
 * (obj_lancerscare2) and C. Round, crowned K. Round (obj_checker_animtest). */
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

enum { EV_BOARD_WHITE = EV_FIELD_FIRST + 50, EV_PONMAN, EV_LANCERSCARE2, EV_KROUND, EV_KROUND_AFTER };

/* ---------------------------------------------------------------- the lit squares */
#define MAX_TILES 24
static struct {
  bool on;
  int16_t x, y;
  int timer;
  bool active;
} T[MAX_TILES];
static int gen_timer, gen_con, white_t, white_con;

static void tile_add(int x, int y) {
  for (int k = 0; k < MAX_TILES; k++)
    if (!T[k].on) {
      T[k].on = true, T[k].x = (int16_t)(x + (g_room == room_field_checkers4 ? 2 : 0)), T[k].y = (int16_t)y;
      T[k].timer = 0, T[k].active = false;
      return;
    }
}

static int alpha_of(int k) {
  int t = T[k].timer;
  if (t >= 0 && t <= 10) return t * 255 / 10;
  if (t > 10 && t < 20) return 255;
  if (t >= 20) return clamp_i((30 - t) * 255 / 10, 0, 255);
  return 0;
}

static void tiles_step(bool interact) {
  if (g_room == room_field_checkers1) {
    if (gen_timer >= 900) {
      static const int16_t xy[9][2] = {{1202, 240}, {1242, 280}, {1282, 320}, {962, 240}, {1002, 280},
                                       {1042, 320}, {722, 240}, {762, 280}, {802, 320}};
      for (int i = 0; i < 9; i++) tile_add(xy[i][0], xy[i][1]);
    }
    if (!interact) gen_timer++;
    if (gen_timer >= 40) {
      gen_timer = 0;
      int c = gen_con * 40;
      tile_add(1442 - c, 240 + c);
      if (++gen_con > 2) gen_con = 0;
    }
  }
  for (int k = 0; k < MAX_TILES; k++) {
    if (!T[k].on) continue;
    if (!interact) T[k].timer++;
    int t = T[k].timer;
    if (t == 10) T[k].active = true;
    if (t >= 20) {
      T[k].active = false;
      if (t >= 30) {
        if (g_room == room_field_checkers1) T[k].x -= 80;
        if (g_room == room_field_checkers4) {
          T[k].x -= 40, T[k].y += 40;
          if (T[k].y >= 350) T[k].y = 240, T[k].x += 120;
        }
        if (g_room == room_field_checkers6) {
          T[k].x -= 40, T[k].y += 40;
          if (T[k].y >= 310) T[k].y = 240, T[k].x += 80;
        }
        T[k].timer = g_room == room_field_checkers6 ? -6 : g_room == room_field_checkers4 ? -4 : -10;
      }
    }
    if (T[k].x < 500) T[k].on = false;
    /* touched while lit: the white, back to the start (event_user 5) */
    if (T[k].on && T[k].active && !interact && white_con == 0 &&
        party_on(T[k].x + 2, T[k].y + 2, T[k].x + 120, T[k].y + 11, false)) {
      white_con = 1, white_t = 0;
      event_start(EV_BOARD_WHITE, NULL);
    }
  }
}

static void tiles_draw(void) {
  for (int k = 0; k < MAX_TILES; k++)
    if (T[k].on && alpha_of(k))   /* (made at scale 1: half the room's own size) */
      gfx_sprite_ex(spr_checkertile_bullet, 0, (T[k].x - cam_x) / 2, (T[k].y - cam_y) / 2, 128, 128, 0, 0, (uint8_t)alpha_of(k));
}

/* the white: 20 frames up, everyone moved back to x 340, then down */
static int ev_board_white(Ev *c) {
  EV_BEGIN;
  pl.frozen = true;
  for (white_t = 1; white_t <= 20; white_t++) WAIT(1);
  {
    int dx = pl.x - 340;
    pl.x = 340;
    for (int ch = CH_SUSIE; ch <= CH_RALSEI; ch++) {
      int fx, fy;
      if (follower_xy(ch, &fx, &fy)) follow_place(ch, fx - dx, fy, pl.dir);
    }
    follow_reset();
  }
  white_con = 2;
  for (white_t = 15; white_t > 0; white_t--) WAIT(1);
  white_con = 0;
  pl.frozen = false;
  EV_END;
}

/* ---------------------------------------------------------------- the Ponmen (obj_ponman_appear) */
#define MAX_PON 5
static struct {
  bool on;
  float x, y, vs, maxy, eye_r, eye_a, eye_factor;
  Motion m;
  int con, t, ix;
  bool draw_eye;
} P[MAX_PON];
static int pon_touched = -1;

static void ponman_setup(void) {
  Inst in;
  memset(P, 0, sizeof P);
  pon_touched = -1;
  for (int k = 0; k < MAX_PON && world_inst(obj_ponman_appear, k, &in); k++) {
    P[k].on = true, P[k].x = in.x, P[k].y = in.y;
    P[k].con = plot() >= 150 ? 99 : 0;
  }
}

static void ponman_step(bool interact) {
  for (int k = 0; k < MAX_PON; k++) {
    if (!P[k].on) continue;
    typeof(P[0]) *p = &P[k];
    if (p->t > 0 && --p->t == 0) p->con++;
    switch (p->con) {
      case 0:
        if (pl.x >= p->x - 60 && pl.x <= p->x + 60) p->con = 1, p->t = 15;
        break;
      case 1: p->ix += 1; break;   /* (image_speed 0.334: its frames, 3 to 5) */
      case 2: p->ix = 5 * 3, p->con = 3, p->t = 15; break;
      case 4:
        p->eye_factor = 0, p->draw_eye = true, p->maxy = p->y + 100, p->vs = -8, p->con = 5;
        break;
      case 5:
        if (p->eye_factor < 5) p->eye_factor++;
        p->eye_a = 270, p->eye_r = p->vs * p->eye_factor / 5 * 0.7f;
        p->vs += 1.5f, p->y += p->vs;
        if (p->y >= p->maxy) p->y = p->maxy, p->vs = 0, p->con = 6, p->t = 5;
        break;
      case 6: p->eye_r *= 0.5f; break;
      case 7: {
        if (interact) break;
        if (p->eye_r < 8) p->eye_r += 2;
        p->eye_a = point_direction(p->x + 32, p->y + 28, pl.x + 20, pl.y + 20);
        p->ix++;
        float maxsp = flag(502) == 1 ? 11 : flag(502) >= 2 ? 9 : 13;
        if (g_room == room_field_checkers5) maxsp = 9;
        float sp = mo_speed(&p->m);
        if (sp < maxsp) sp += 2;
        mo_set(&p->m, sp, p->eye_a);
        p->x += p->m.hs, p->y += p->m.vs;
        if (party_on((int)p->x, (int)p->y, (int)p->x + 58, (int)p->y + 88, false) && !event_running()) {
          pon_touched = k;
          event_start(EV_PONMAN, NULL);
        }
        break;
      }
      default: break;
    }
  }
}

static void ponman_draw_eyes(void) {
  for (int k = 0; k < MAX_PON; k++) {
    if (!P[k].on) continue;
    int x = ((int)P[k].x - cam_x) / 2, y = ((int)P[k].y - cam_y) / 2;
    uint16_t spr = k == pon_touched ? spr_ponman_touched : P[k].con >= 4 && P[k].con <= 5 ? spr_ponman_jump
                   : P[k].con >= 6 ? spr_ponman_idle : spr_ponman_appear;
    int fr = spr == spr_ponman_appear ? min_i(P[k].ix / 3, 5) : spr == spr_ponman_idle ? P[k].ix / 4 : 0;
    gfx_sprite(spr, fr, x, y, 0, 255);
    if (P[k].draw_eye && k != pon_touched)
      gfx_sprite(spr_ponman_eye, 0, x + 14 + (int)lengthdir_x(P[k].eye_r, P[k].eye_a) / 2,
                 y + 16 + (int)lengthdir_y(P[k].eye_r, P[k].eye_a) / 2, 0, 255);
  }
}

static int ev_ponman(Ev *c) {
  EV_BEGIN;
  {
    int enc = flag(502) == 1 ? 14 : flag(502) >= 2 ? (rnd_n(2) ? 14 : 13) : 13;
    if (g_room == room_field_checkers5) enc = 14;
    set_flag(502, flag(502) + 1);
    c->v[0] = enc;
  }
  pl.frozen = true;
  WAIT(20);
  pl.frozen = false;
  memset(P, 0, sizeof P);   /* (the others go too) */
  pon_touched = -1;
  BATTLE(c->v[0]);
  EV_END;
}

/* ---------------------------------------------------------------- Lancer's team (obj_lancerscare2) */
static int ev_lancerscare2(Ev *c) {
  EV_BEGIN;
  pl.frozen = true;
  {
    int sx, sy, rx, ry;
    follower_xy(CH_SUSIE, &sx, &sy), follower_xy(CH_RALSEI, &rx, &ry);
    mk(spr_susier_dark, pl.x - 100, sy, ACT_S);
    mk(spr_ralseir, pl.x - 50, ry, ACT_R);
    mk(spr_krisr_dark, pl.x, pl.y, ACT_K);
  }
  pl.hidden = true;
  follow_show(false);
  go_hv(ACT_S, 6 * FX, 0, FX / 5), go_hv(ACT_R, 6 * FX, 0, FX / 5), go_hv(ACT_K, 6 * FX, 0, FX / 5);
  WAIT(30);
  pl.dir = DIR_RIGHT;
  halt_act(ACT_S), halt_act(ACT_R), halt_act(ACT_K);
  pl.x = act(ACT_K)->x / FX, pl.hidden = false;
  gone(ACT_K);
  WAIT(30);
  SAY(FC_LANCER, 1, obj_lancerscare2_slash_Step_0_gml_60_0, obj_lancerscare2_slash_Step_0_gml_61_0,
      obj_lancerscare2_slash_Step_0_gml_62_0);
  set_spr(ACT_LANCER, spr_lancer_dt, 0);
  WAIT(25);
  set_spr(ACT_LANCER, spr_lancer_lt, 0);
  WAIT(25);
  SAY(FC_LANCER, 1, obj_lancerscare2_slash_Step_0_gml_84_0, obj_lancerscare2_slash_Step_0_gml_85_0,
      obj_lancerscare2_slash_Step_0_gml_86_0);
  /* the bucket, and the lightning that lights him up */
  set_spr(ACT_LANCER, spr_lancer_lt_laugh, FX / 2);
  {
    Obj *l = act(ACT_LANCER);
    Obj *b = mk(spr_lancer_bucket, l->x / FX + 20, l->y / FX, ACT_PROP);
    b->hs = -4 * FX, b->depth_bias = -10;
    mk(spr_crappylightning, 200, 0, ACT_PROP2)->alpha = 255;
  }
  for (c->v[0] = 0; c->v[0] < 60; c->v[0]++) {
    Obj *lt = obj_find(ACT_PROP2);
    if (lt) lt->alpha = (uint8_t)max_i(0, 255 - c->v[0] * 255 / 20);
    if (c->v[0] == 20) act(ACT_PROP)->hs = 0;
    WAIT(1);
  }
  gone(ACT_PROP2);
  set_spr(ACT_LANCER, spr_lancer_lt, 0);
  WAIT(60);
  WAIT(1);
  SAY(FC_LANCER, 3, obj_lancerscare2_slash_Step_0_gml_137_0, F_('S', 7), obj_lancerscare2_slash_Step_0_gml_139_0, F_('L', 2),
      obj_lancerscare2_slash_Step_0_gml_141_0, obj_lancerscare2_slash_Step_0_gml_142_0);
  set_spr(ACT_S, spr_susiel_dark, 0);
  WAIT(40);
  set_spr(ACT_S, spr_susier_dark, FX / 4);
  act(ACT_S)->hs = 6 * FX;
  WAIT(30);
  halt_act(ACT_S);
  {
    /* (Susie turns as she talks: the writer's message number) */
    static const int ids[] = {obj_lancerscare2_slash_Step_0_gml_181_0, F_('L', 2), obj_lancerscare2_slash_Step_0_gml_183_0, F_('S', 2),
                              obj_lancerscare2_slash_Step_0_gml_185_0, obj_lancerscare2_slash_Step_0_gml_186_0,
                              obj_lancerscare2_slash_Step_0_gml_187_0, obj_lancerscare2_slash_Step_0_gml_188_0,
                              obj_lancerscare2_slash_Step_0_gml_189_0, F_('L', 2), obj_lancerscare2_slash_Step_0_gml_191_0, F_('S', 0),
                              obj_lancerscare2_slash_Step_0_gml_193_0, F_('L', 6), obj_lancerscare2_slash_Step_0_gml_195_0,
                              obj_lancerscare2_slash_Step_0_gml_196_0, F_('S', 2), obj_lancerscare2_slash_Step_0_gml_198_0, -1};
    say_list(FC_SUSIE, 6, ids);
  }
  while (text_active()) {
    int m = text_msgno();
    Obj *s = act(ACT_S);
    if (m == 4 || m == 6) s->spr = spr_susied_dark;
    if (m == 5 || m == 8) s->spr = spr_susier_dark;
    WAIT(1);
  }
  set_spr(ACT_S, spr_susied_dark, 0);
  WAIT(30);
  set_spr(ACT_S, spr_susier_dark, 0);
  SAY(FC_SUSIE, 0, obj_lancerscare2_slash_Step_0_gml_226_0, F_('L', 2), obj_lancerscare2_slash_Step_0_gml_228_0,
      obj_lancerscare2_slash_Step_0_gml_229_0, obj_lancerscare2_slash_Step_0_gml_231_0);
  WAIT(30);
  SAY(FC_SUSIE, 6, obj_lancerscare2_slash_Step_0_gml_245_0, obj_lancerscare2_slash_Step_0_gml_246_0, F_('R', 8),
      obj_lancerscare2_slash_Step_0_gml_248_0, F_('S', 0), obj_lancerscare2_slash_Step_0_gml_250_0, F_('L', 1),
      obj_lancerscare2_slash_Step_0_gml_252_0);
  /* the dark; then everyone by the bucket, and the team's name (scr_text 175) */
  for (c->v[0] = 0; c->v[0] < 40; c->v[0]++) {
    g_fade_col = 0, g_fade = (uint8_t)min_i(255, c->v[0] * 255 / 25);
    WAIT(1);
  }
  {
    int bx = act(ACT_PROP)->x / FX;
    pl.x = bx - 50;
    act(ACT_R)->x = (bx - 100) * FX;
    act(ACT_S)->x = (bx - 150) * FX;
  }
  WAIT(20);
  for (c->v[0] = 30; c->v[0] > 0; c->v[0]--) {
    g_fade = (uint8_t)(c->v[0] * 255 / 30);
    WAIT(1);
  }
  g_fade = 0;
  talk_msc(175, -1);
  WAIT_UNTIL(!text_active());
  set_spr(ACT_LANCER, spr_lancer_lt_laugh, FX / 2);
  act(ACT_LANCER)->hs = 16 * FX;
  act(ACT_PROP)->hs = 16 * FX;
  WAIT(61);
  gone(ACT_LANCER), gone(ACT_PROP);
  /* back to the party, facing on (scr_unmarkify_caterpillar) */
  {
    Obj *s = act(ACT_S), *r = act(ACT_R);
    follow_reset();
    follow_place(CH_SUSIE, s->x / FX, s->y / FX, DIR_RIGHT);
    follow_place(CH_RALSEI, r->x / FX, r->y / FX, DIR_RIGHT);
  }
  gone(ACT_S), gone(ACT_R);
  follow_show(true);
  for (int i = 0; i < MAX_OBJS; i++)
    if (!(objs[i].flags & OF_GONE) && objs[i].name == ACT_SAVE) objs[i].flags &= (uint16_t)~OF_HIDDEN;
  pl.dir = DIR_RIGHT;
  pl.frozen = false;
  if (plot() <= 55) set_plot(55);
  EV_END;
}

/* ---------------------------------------------------------------- C. Round becomes K. Round */
static int kround_name(void) {
  switch (flag(214)) {
    case 1: return obj_checker_animtest_slash_Step_0_gml_43_0;
    case 2: return obj_checker_animtest_slash_Step_0_gml_44_0;
    case 3: return obj_checker_animtest_slash_Step_0_gml_45_0;
    default: return obj_checker_animtest_slash_Step_0_gml_42_0;
  }
}

static int ev_kround(Ev *c) {
  static char team[96];
  EV_BEGIN;
  pl.frozen = true;
  {
    Obj *kr = mk(spr_smallchecker_front, cam_x + 800, 240, ACT_KING);
    kr->ispd = FX / 5;
    int sx, sy, rx, ry;
    follower_xy(CH_SUSIE, &sx, &sy), follower_xy(CH_RALSEI, &rx, &ry);
    mk(spr_susier_dark, -100, sy, ACT_S);
    mk(spr_ralseir, -50, ry, ACT_R);
    mk(spr_krisr_dark, 0, pl.y, ACT_K);
    pl.hidden = true;
    follow_show(false);
    go_hv(ACT_S, 5 * FX, 0, FX / 5), go_hv(ACT_R, 5 * FX, 0, FX / 5), go_hv(ACT_K, 5 * FX, 0, FX / 5);
    mk(spr_lancer_rt, 500, pl.y, ACT_LANCER);
  }
  WAIT(28);
  halt_act(ACT_S), halt_act(ACT_R), halt_act(ACT_K);
  WAIT(15);
  snprintf(team, sizeof team, "%s%s/", str(obj_checker_animtest_slash_Step_0_gml_48_0), str((uint16_t)kround_name()));
  text_clear();
  text_face(FC_LANCER, 0);
  text_add(str(obj_checker_animtest_slash_Step_0_gml_47_0));
  text_add(team);
  {
    static const int ids[] = {obj_checker_animtest_slash_Step_0_gml_49_0, F_('R', 3), obj_checker_animtest_slash_Step_0_gml_51_0,
                              F_('L', 1), obj_checker_animtest_slash_Step_0_gml_53_0, -1};
    say_add(ids);
  }
  text_open();
  WAIT_UNTIL(!text_active());
  /* three warnings, Lancer backing away each time */
  for (c->v[0] = 0; c->v[0] < 3; c->v[0]++) {
    set_spr(ACT_LANCER, spr_lancer_lt, 0);
    act(ACT_LANCER)->hs = -10 * FX;
    WAIT(3);
    halt_act(ACT_LANCER);
    WAIT(15);
    if (c->v[0] == 0) SAY(FC_LANCER, 2, obj_checker_animtest_slash_Step_0_gml_82_0);
    if (c->v[0] == 1) SAY(FC_LANCER, 1, obj_checker_animtest_slash_Step_0_gml_87_0);
    if (c->v[0] == 2) SAY(FC_LANCER, 3, obj_checker_animtest_slash_Step_0_gml_92_0, F_('S', 9), obj_checker_animtest_slash_Step_0_gml_94_0);
  }
  /* the party walks on; Lancer hops up out of the way */
  go_hv(ACT_S, 5 * FX, 0, FX / 4), go_hv(ACT_R, 5 * FX, 0, FX / 4), go_hv(ACT_K, 5 * FX, 0, FX / 4);
  set_spr(ACT_LANCER, spr_lancer_dt, 0);
  act(ACT_LANCER)->vs = -2 * FX;
  for (c->v[0] = 0; c->v[0] < 96; c->v[0]++) {
    if (c->v[0] == 20) halt_act(ACT_LANCER);
    if (act(ACT_K)->x / FX >= 140) pl.x = act(ACT_K)->x / FX;
    WAIT(1);
  }
  pl.dir = DIR_RIGHT;
  halt_act(ACT_S), halt_act(ACT_R), halt_act(ACT_K);
  pl.x = act(ACT_K)->x / FX, pl.hidden = false;
  set_spr(ACT_LANCER, spr_lancer_rt, 0);
  gone(ACT_K);
  cam_pan(5 * FX, 0, 16);
  WAIT(16);
  SAY(FC_RALSEI, 8, obj_checker_animtest_slash_Step_0_gml_178_0, F_('S', 6), obj_checker_animtest_slash_Step_0_gml_180_0, F_('L', 3),
      obj_checker_animtest_slash_Step_0_gml_182_0, F_('S', 2), obj_checker_animtest_slash_Step_0_gml_184_0, F_('L', 6),
      obj_checker_animtest_slash_Step_0_gml_186_0);
  /* C. Round walks in, puts on the crown, grows */
  {
    Obj *kr = act(ACT_KING);
    kr->spr = spr_smallchecker_idle, kr->img = 0, kr->x += 40 * FX, kr->flags |= OF_FLIPX, kr->hs = 2 * FX;
  }
  WAIT_UNTIL(act(ACT_KING)->x / FX > cam_x + 520);
  {
    Obj *kr = act(ACT_KING);
    kr->img = 0, kr->flags &= (uint16_t)~OF_FLIPX, kr->spr = spr_smallchecker_front, kr->hs = 0, kr->x = (cam_x + 480) * FX;
  }
  WAIT(30);
  for (c->v[0] = 0; c->v[0] < 3; c->v[0]++) {
    Obj *t = mk(spr_checkertile_bullet, 480 - c->v[0] * 40 + 400, 240 + c->v[0] * 40, ACT_X1 + c->v[0]);
    t->alpha = 0, t->flags |= OF_BACK, t->scale = 128;   /* (scr_marker: scale 1) */
  }
  for (c->v[0] = 1; c->v[0] <= 40; c->v[0]++) {
    for (int j = 0; j < 3; j++) act(ACT_X1 + j)->alpha = (uint8_t)(c->v[0] <= 10 ? c->v[0] * 25 : max_i(0, 255 - (c->v[0] - 10) * 25));
    WAIT(1);
  }
  for (int j = 0; j < 3; j++) gone(ACT_X1 + j);
  {
    Obj *kr = act(ACT_KING);
    mk(spr_smallchecker_crown, kr->x / FX + 8, cam_y - 40, ACT_PROP)->vs = 4 * FX;
  }
  WAIT_UNTIL(act(ACT_PROP)->y >= act(ACT_KING)->y - 10 * FX);
  gone(ACT_PROP);
  {
    Obj *kr = act(ACT_KING);
    kr->spr = spr_smallchecker_transform, kr->img = 0, kr->ispd = 0, kr->y += 2 * FX;
  }
  WAIT(50);
  /* it shakes faster and faster */
  for (c->v[0] = 4; c->v[0] < 8; c->v[0]++) {
    act(ACT_KING)->ispd = c->v[0] * FX / 16;
    WAIT(15);
  }
  act(ACT_KING)->img = FX, act(ACT_KING)->ispd = 0;
  WAIT(60);
  set_spr(ACT_KING, spr_smallchecker_transform2, FX / 3);
  WAIT_UNTIL(act(ACT_KING)->img >= 4 * FX);
  act(ACT_KING)->img = 4 * FX, act(ACT_KING)->ispd = 0;
  WAIT(40);
  {
    Obj *kr = act(ACT_KING);
    kr->x -= 18 * FX, kr->y -= 90 * FX;
    kr->spr = spr_checkers_idle, kr->img = 0, kr->ispd = FX / 3;
    set_spr(ACT_S, spr_susie_shock_r, 0);
    set_spr(ACT_R, spr_ralsei_shock_overworld, 0);
    g_shake = 30;
  }
  WAIT(30);
  set_spr(ACT_R, spr_ralseir, 0), set_spr(ACT_S, spr_susier_dark, 0);
  SAY(FC_RALSEI, 6, obj_checker_animtest_slash_Step_0_gml_356_0);
  {
    /* (the party, back as the battle begins) */
    Obj *s = act(ACT_S), *r = act(ACT_R);
    follow_reset();
    follow_place(CH_SUSIE, s->x / FX, s->y / FX, DIR_RIGHT);
    follow_place(CH_RALSEI, r->x / FX, r->y / FX, DIR_RIGHT);
    gone(ACT_S), gone(ACT_R);
    follow_show(true);
    pl.dir = DIR_RIGHT;
  }
  act(ACT_KING)->flags |= OF_HIDDEN;
  BATTLE(12);
  WAIT(60);
  SAY(FC_SUSIE, 6, obj_checker_animtest_slash_Step_0_gml_589_0);
  set_spr(ACT_LANCER, spr_lancer_rt, 0);
  act(ACT_LANCER)->x = (cam_x - 60) * FX;
  act(ACT_LANCER)->hs = 18 * FX;
  WAIT(20);
  halt_act(ACT_LANCER);
  set_spr(ACT_LANCER, spr_lancer_lt, 0);
  WAIT(15);
  SAY(FC_LANCER, 3, obj_checker_animtest_slash_Step_0_gml_624_0, obj_checker_animtest_slash_Step_0_gml_625_0, F_('S', 2),
      obj_checker_animtest_slash_Step_0_gml_627_0);
  party_markers();
  set_spr(ACT_R, spr_ralseil, 0), set_spr(ACT_S, spr_susier_dark, 0), set_spr(ACT_K, spr_krisl_dark, 0);
  SAY(FC_RALSEI, 1, obj_checker_animtest_slash_Step_0_gml_657_0, obj_checker_animtest_slash_Step_0_gml_658_0,
      obj_checker_animtest_slash_Step_0_gml_659_0, obj_checker_animtest_slash_Step_0_gml_660_0,
      obj_checker_animtest_slash_Step_0_gml_661_0, obj_checker_animtest_slash_Step_0_gml_662_0, F_('S', 6),
      obj_checker_animtest_slash_Step_0_gml_664_0, obj_checker_animtest_slash_Step_0_gml_665_0,
      obj_checker_animtest_slash_Step_0_gml_666_0);
  set_spr(ACT_R, spr_ralseid, 0);
  WAIT(30);
  set_spr(ACT_R, spr_ralseil, 0);
  SAY(FC_RALSEI, 6, obj_checker_animtest_slash_Step_0_gml_685_0, F_('S', 0), obj_checker_animtest_slash_Step_0_gml_687_0, F_('L', 3),
      obj_checker_animtest_slash_Step_0_gml_689_0, F_('R', 1), obj_checker_animtest_slash_Step_0_gml_691_0);
  set_spr(ACT_S, spr_susied_dark, 0);
  WAIT(30);
  set_spr(ACT_S, spr_susier_dark, 0);
  SAY(FC_SUSIE, 2, obj_checker_animtest_slash_Step_0_gml_709_0, F_('R', 12), obj_checker_animtest_slash_Step_0_gml_711_0,
      obj_checker_animtest_slash_Step_0_gml_712_0, obj_checker_animtest_slash_Step_0_gml_713_0,
      obj_checker_animtest_slash_Step_0_gml_714_0, obj_checker_animtest_slash_Step_0_gml_715_0);
  set_spr(ACT_S, spr_susiel_dark_unhappy, 0);
  WAIT(60);
  SAY(FC_SUSIE, 0, obj_checker_animtest_slash_Step_0_gml_732_0, obj_checker_animtest_slash_Step_0_gml_733_0,
      obj_checker_animtest_slash_Step_0_gml_734_0);
  set_spr(ACT_S, spr_susied_dark_unhappy, FX / 5);
  act(ACT_S)->vs = 2 * FX;
  WAIT(10);
  halt_act(ACT_S);
  WAIT(30);
  SAY(FC_SUSIE, 0, obj_checker_animtest_slash_Step_0_gml_762_0, obj_checker_animtest_slash_Step_0_gml_763_0);
  set_spr(ACT_S, spr_susier_dark, FX / 2);
  act(ACT_S)->hs = 12 * FX;
  WAIT_UNTIL(act(ACT_S)->x / FX >= cam_x + 460);
  halt_act(ACT_S);
  set_spr(ACT_S, spr_susiel_dark, 0), set_spr(ACT_K, spr_krisr_dark, 0), set_spr(ACT_R, spr_ralseir, 0);
  SAY(FC_SUSIE, 3, obj_checker_animtest_slash_Step_0_gml_799_0);
  set_spr(ACT_R, spr_ralsei_shock_overworld, 0);
  WAIT(20);
  set_spr(ACT_R, spr_ralseir, 0);
  SAY(FC_LANCER, 2, obj_checker_animtest_slash_Step_0_gml_820_0, obj_checker_animtest_slash_Step_0_gml_821_0, F_('S', 2),
      obj_checker_animtest_slash_Step_0_gml_823_0, F_('R', 3), obj_checker_animtest_slash_Step_0_gml_825_0, F_('L', 1),
      obj_checker_animtest_slash_Step_0_gml_827_0, F_('S', 2), obj_checker_animtest_slash_Step_0_gml_829_0, F_('L', 3),
      obj_checker_animtest_slash_Step_0_gml_831_0, F_('S', 2), obj_checker_animtest_slash_Step_0_gml_833_0, F_('L', 1),
      obj_checker_animtest_slash_Step_0_gml_835_0, F_('S', 6), obj_checker_animtest_slash_Step_0_gml_837_0,
      obj_checker_animtest_slash_Step_0_gml_838_0, F_('L', 3), obj_checker_animtest_slash_Step_0_gml_840_0);
  /* off they go, Lancer and Susie */
  set_spr(ACT_LANCER, spr_lancer_lt_laugh, FX / 5);
  act(ACT_LANCER)->flags |= OF_FLIPX;
  act(ACT_LANCER)->hs = 12 * FX;
  set_spr(ACT_S, spr_susier_dark, FX / 2);
  act(ACT_S)->hs = 12 * FX;
  WAIT(71);
  gone(ACT_LANCER), gone(ACT_S);
  set_spr(ACT_R, spr_ralseir, FX / 4);
  act(ACT_R)->hs = 6 * FX;
  WAIT(20);
  pl.dir = DIR_UP;
  halt_act(ACT_R);
  SAY(FC_RALSEI, 1, obj_checker_animtest_slash_Step_0_gml_893_0, obj_checker_animtest_slash_Step_0_gml_894_0,
      obj_checker_animtest_slash_Step_0_gml_895_0, obj_checker_animtest_slash_Step_0_gml_896_0,
      obj_checker_animtest_slash_Step_0_gml_897_0, obj_checker_animtest_slash_Step_0_gml_898_0,
      obj_checker_animtest_slash_Step_0_gml_899_0);
  set_spr(ACT_K, spr_krisr_dark, FX / 4), set_spr(ACT_R, spr_ralseir, FX / 4);
  act(ACT_K)->hs = 8 * FX, act(ACT_R)->hs = 8 * FX;
  WAIT(11);
  for (c->v[0] = 0; c->v[0] < 31; c->v[0]++) {
    g_fade_col = 0, g_fade = (uint8_t)min_i(255, c->v[0] * 255 / 25);
    WAIT(1);
  }
  if (plot() < 60) set_plot(60);
  if (flag(215) == 1) set_flag(251, 1);
  sv.chr[1] = sv.chr[2] = 0;
  party_add(CH_RALSEI);
  gone(ACT_K), gone(ACT_R);
  pl.hidden = false, pl.frozen = false;
  follow_show(true);
  world_fade_in();
  world_enter_marker(room_next(g_room, 1), 1);
  EV_END;
}

/* ---------------------------------------------------------------- the rooms */
static int board_con;
void board_setup(int room) {
  Inst in;
  memset(T, 0, sizeof T);
  gen_timer = 999, gen_con = 0, white_con = 0, board_con = 0;
  for (int k = 0; world_inst(obj_ob_checkertile, k, &in); k++) tile_add(in.x, in.y);
  ponman_setup();
  if (room == room_field_checkers3 && plot() < 55 && world_inst(obj_lancerscare2, 0, &in)) {
    Obj *l = mk(spr_lancer_rt, in.x, in.y, ACT_LANCER);
    l->flags |= OF_HIDDEN;
    for (int i = 0; i < MAX_OBJS; i++)
      if (!(objs[i].flags & OF_GONE) && objs[i].name == ACT_SAVE) objs[i].flags |= OF_HIDDEN;
    /* (Lancer, darkened, shows once the party has walked in) */
    l->flags &= (uint16_t)~OF_HIDDEN;
    board_con = 1;
  }
  if (room == room_field_checkersboss && plot() < 60) board_con = 2;
}

void board_step(void) {
  bool interact = event_running() || text_active() || world_fading() || pl.frozen;
  if (g_room >= room_field_checkers4 && g_room <= room_field_checkersboss) {
    tiles_step(interact);
    ponman_step(interact);
  }
  if (board_con == 1 && !interact) board_con = 0, event_start(EV_LANCERSCARE2, NULL);
  if (board_con == 2 && !interact) board_con = 0, event_start(EV_KROUND, NULL);
}

void board_draw_under(void) { tiles_draw(); }
void board_draw(void) {
  ponman_draw_eyes();
  if (white_con) gfx_rect(0, 0, SCREEN_W, SCREEN_H, C_WHITE, (uint8_t)clamp_i(white_t * 255 / 15, 0, 255));
}

int board_event(Ev *c) {
  switch (c->id) {
    case EV_BOARD_WHITE: return ev_board_white(c);
    case EV_PONMAN: return ev_ponman(c);
    case EV_LANCERSCARE2: return ev_lancerscare2(c);
    case EV_KROUND: return ev_kround(c);
    default: return 1;
  }
}
