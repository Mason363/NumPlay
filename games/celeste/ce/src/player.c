/* Madeline: Player.cs (NoelFB/Celeste) as src/player.c ports it for the NumWorks, in whole numbers (ce.h): the same
 * constants, states and order of operations. */
#include "ce.h"

Player g_player;
#define P (&g_player)
static Ent *E;

/* Player.cs's constants in the CE's units */
#define MaxFall SPD(160)
#define Gravity 900                     /* per frame */
#define HalfGravThreshold SPD(40)
#define FastMaxFall SPD(240)
#define FastMaxAccel 300
#define MaxRun SPD(90)
#define RunAccel 1000
#define RunReduce 400
#define DuckFriction 500
#define DuckCorrectCheck 4
#define DuckCorrectSlide SPD(50)
#define JumpGraceTime TIM(.1f)
#define JumpSpeed SPD(-105)
#define JumpHBoost SPD(40)
#define VarJumpTime TIM(.2f)
#define CeilingVarJumpGrace TIM(.05f)
#define UpwardCornerCorrection 4
#define WallSpeedRetentionTime TIM(.06f)
#define WallJumpCheckDist 3
#define WallJumpForceTime TIM(.16f)
#define WallJumpHSpeed (MaxRun + JumpHBoost)
#define WallSlideStartMax SPD(20)
#define WallSlideTime TIM(1.2f)
#define BounceVarJumpTime TIM(.2f)
#define BounceSpeed SPD(-140)
#define BounceAutoJumpTime TIM(.1f)
#define SuperBounceVarJumpTime TIM(.2f)
#define SuperBounceSpeed SPD(-185)
#define SuperJumpH SPD(260)
#define SuperWallJumpSpeed SPD(-160)
#define SuperWallJumpVarTime TIM(.25f)
#define SuperWallJumpH (MaxRun + JumpHBoost * 2)
#define DashSpeed SPD(240)
#define EndDashSpeed SPD(160)
#define DashTime TIM(.15f)
#define DashCooldown TIM(.2f)
#define DashRefillCooldown TIM(.1f)
#define DashHJumpThruNudge 6
#define DashCornerCorrection 4
#define DashVFloorSnapDist 3
#define DashAttackTime TIM(.3f)
#define STAM 3600                       /* stamina: 3600ths */
#define ClimbMaxStamina (110 * STAM)
#define ClimbUpCost 2727                /* 100 / 2.2 a second, a frame */
#define ClimbStillCost 600              /* 10 a second */
#define ClimbJumpCost (110 * STAM / 4)
#define ClimbTiredThreshold (20 * STAM)
#define ClimbCheckDist 2
#define ClimbUpCheckDist 2
#define ClimbNoMoveTime TIM(.1f)
#define ClimbUpSpeed SPD(-45)
#define ClimbDownSpeed SPD(80)
#define ClimbSlipSpeed SPD(30)
#define ClimbAccel 900
#define ClimbHopY SPD(-120)
#define ClimbHopX SPD(100)
#define ClimbHopForceTime TIM(.2f)
#define ClimbJumpBoostTime TIM(.2f)
#define LiftYCap SPD(-130)
#define LiftXCap SPD(250)
#define JumpThruAssistSpeed SPD(-40)
#define D 181                           /* 0.70710677 * 256 */

#define NormalHairColor 0xAC3232
#define UsedHairColor 0x44B7FF
#define FlashHairColor 0xFFFFFF
#define TwoDashesHairColor 0xFF6DEF

/* hitboxes: w, h, x, y */
static const int8_t HB_NORMAL[4] = {8, 11, -4, -11}, HB_DUCK[4] = {8, 6, -4, -6};
static const int8_t HB_NORMAL_HURT[4] = {8, 9, -4, -11}, HB_DUCK_HURT[4] = {8, 4, -4, -6};

static void set_box(const int8_t *b) { E->hw = b[0], E->hh = b[1], E->hx = b[2], E->hy = b[3]; }
static bool box_is(const int8_t *b) { return E->hw == b[0] && E->hh == b[1] && E->hx == b[2] && E->hy == b[3]; }
#define Ducking box_is(HB_DUCK)
static void set_ducking(bool d) { set_box(d ? HB_DUCK : HB_NORMAL); }

static bool solid_off(int dx, int dy) { return solid_at(e_left(E) + dx, e_top(E) + dy, e_right(E) + dx, e_bottom(E) + dy, E); }
static Ent *solid_hit_off(int dx, int dy) {
  extern Ent *solid_hit(int l, int t, int r, int b, const Ent *self);
  return solid_hit(e_left(E) + dx, e_top(E) + dy, e_right(E) + dx, e_bottom(E) + dy, E);
}
static bool jumpthru_off(int dx, int dy) { return jumpthru_first_outside(E, E->x + dx, E->y + dy) != NULL; }
static bool point_solid(int x, int y) { return solid_at(x, y, x + 1, y + 1, E); }

/* ---------------------------------------------------------------- moving (Actor) */
typedef void (*Collide)(int dx, int dy, Ent *hit);
static bool move_h_exact(int move, Collide cb) {
  int s = isign(move);
  while (move) {
    Ent *hit = solid_hit_off(s, 0);
    if (hit) {
      E->remx = 0;
      if (cb) cb(s, 0, hit);
      return true;
    }
    move -= s;
    E->x += s;
  }
  return false;
}
static bool move_v_exact(int move, Collide cb) {
  int s = isign(move);
  while (move) {
    Ent *hit = solid_hit_off(0, s);
    if (!hit && move > 0) hit = jumpthru_first_outside(E, E->x, E->y + s);
    if (hit) {
      E->remy = 0;
      if (cb) cb(0, s, hit);
      return true;
    }
    move -= s;
    E->y += s;
  }
  return false;
}
static bool move_h(int amount, Collide cb) {
  E->remx += amount;
  int n = round_mu(E->remx, amount);
  if (!n) return false;
  E->remx -= n * MU;
  return move_h_exact(n, cb);
}
static bool move_v(int amount, Collide cb) {
  E->remy += amount;
  int n = round_mu(E->remy, amount);
  if (!n) return false;
  E->remy -= n * MU;
  return move_v_exact(n, cb);
}
static bool on_ground_at(int x, int y) {
  int ox = E->x, oy = E->y;
  E->x = x, E->y = y;
  bool r = solid_off(0, 1) || jumpthru_off(0, 1);
  E->x = ox, E->y = oy;
  return r;
}

/* lift speed: what the platform under her gives (kept 0.16 s after she leaves it) */
static int liftx(void) { return E->liftx || E->lifty ? E->liftx : P->last_liftx; }
static int lifty(void) { return E->liftx || E->lifty ? E->lifty : P->last_lifty; }
static void lift_boost(int *x, int *y) {
  int lx = liftx(), ly = lifty();
  if (iabs(lx) > LiftXCap) lx = LiftXCap * isign(lx);
  if (ly > 0) ly = 0;
  else if (ly < LiftYCap) ly = LiftYCap;
  *x = lx, *y = ly;
}

/* ---------------------------------------------------------------- the sprite */
static void play(int a) {
  if (P->anim == a) return;
  P->anim = a, P->frame = 0, P->anim_timer = 0;
}
static void play_r(int a) { P->anim = -1, play(a); }
static int cur_sprite(void) {
  const uint8_t *an = anim(P->anim);
  int f = imin(P->frame, an[0] - 1);
  return (int)R16(an + 6 + 2 * f);
}
static void sprite_update(void) {
  const uint8_t *an = anim(P->anim);
  int delay = (int)R16(an + 2), n = an[0];
  if (!delay) return;
  P->anim_timer += DTU;
  if (P->anim_timer < delay) return;
  P->anim_timer -= delay;
  if (P->frame + 1 < n) {
    P->frame++;
    return;
  }
  if (an[4] != 255) {   /* goto */
    int g = an[4];
    if (g == A_player_idle && P->anim == A_player_idle && P->idle_timer > TIM(3) && rndn(5) == 0) {
      int r = rndn(9);   /* the idle's variants (Player.cs: idleA, B, C) */
      g = r < 5 ? A_player_idleA : r < 8 ? A_player_idleB : A_player_idleC;
    }
    P->anim = -1, play(g);
  } else if (an[1] & 1)
    P->frame = 0;
}

/* ---------------------------------------------------------------- states */
static bool in_control(void) {
  return P->state != ST_INTROJUMP && P->state != ST_INTROWALK && P->state != ST_INTRORESPAWN && P->state != ST_DUMMY;
}
bool player_dash_attacking(void) { return P->dash_attack > 0; }
static bool is_tired(void) { return (P->wall_boost_timer > 0 ? P->stamina + ClimbJumpCost : P->stamina) < ClimbTiredThreshold; }
bool player_refill_dash(void) {
  if (P->dashes < P->max_dashes) {
    P->dashes = P->max_dashes;
    return true;
  }
  return false;
}
bool player_use_refill(bool two) {
  int max = two ? 2 : P->max_dashes;
  if (P->dashes < max || P->stamina < ClimbTiredThreshold) {
    P->dashes = max;
    P->stamina = ClimbMaxStamina;
    return true;
  }
  return false;
}

static bool can_unduck(void) {
  if (!Ducking) return true;
  set_box(HB_NORMAL);
  bool ok = !solid_off(0, 0);
  set_box(HB_DUCK);
  return ok;
}
static bool can_unduck_at(int x, int y) {
  int ox = E->x, oy = E->y;
  E->x = x, E->y = y;
  bool r = can_unduck();
  E->x = ox, E->y = oy;
  return r;
}
static bool duck_free_at(int x, int y) {
  int ox = E->x, oy = E->y;
  int8_t was[4] = {(int8_t)E->hw, (int8_t)E->hh, (int8_t)E->hx, (int8_t)E->hy};
  E->x = x, E->y = y;
  set_box(HB_DUCK);
  bool r = !solid_off(0, 0);
  E->x = ox, E->y = oy;
  set_box(was);
  return r;
}

static bool climb_bounds_check(int dir) {
  Room *rm = g_level.room;
  return e_left(E) + dir * ClimbCheckDist >= rm->x && e_right(E) + dir * ClimbCheckDist < rm->x + rm->w;
}
static bool climb_check(int dir, int yadd) { return climb_bounds_check(dir) && solid_off(dir * ClimbCheckDist, yadd); }
static bool wall_jump_check(int dir) { return climb_bounds_check(dir) && solid_off(dir * WallJumpCheckDist, 0); }

static void aim(int *x, int *y) {   /* 1/256 */
  int ax = g_in.aim_x, ay = g_in.aim_y;
  if (!ax && !ay) *x = P->facing * 256, *y = 0;
  else if (ax && ay) *x = ax * D, *y = ay * D;
  else *x = ax * 256, *y = ay * 256;
}

static int normal_update(void), climb_update(void), dash_update(void);
static void normal_begin(void), normal_end(void), climb_begin(void), climb_end(void), dash_begin(void), dash_end(void);
static void respawn_begin(void), respawn_end(void);
static int co_dash(void), co_intro_jump(void), co_intro_walk(void);
typedef int (*UpdFn)(void);
typedef void (*VFn)(void);
static const UpdFn UPD[ST_COUNT] = {normal_update, climb_update, dash_update};
static const VFn BEGIN[ST_COUNT] = {normal_begin, climb_begin, dash_begin, respawn_begin};
static const VFn END[ST_COUNT] = {normal_end, climb_end, dash_end, respawn_end};
static const UpdFn CO[ST_COUNT] = {NULL, NULL, co_dash, NULL, co_intro_jump, co_intro_walk};

static void set_state(int st) {
  if (P->state == st) return;
  P->prev_state = P->state;
  P->state = st;
  if (P->prev_state >= 0 && END[P->prev_state]) END[P->prev_state]();
  if (BEGIN[st]) BEGIN[st]();
  P->co_active = CO[st] != NULL;
  P->co_step = 0, P->co_wait = 0;
}
static void co_update(void) {
  if (!P->co_active) return;
  if (P->co_wait > 0) {
    P->co_wait -= DTU;
    return;
  }
  int st = P->state;
  int r = CO[st]();
  if (P->state != st || !P->co_active) return;
  if (r < 0) P->co_active = false;
  else P->co_wait = r;
}

/* ---------------------------------------------------------------- jumps */
static void jump_common(int var_time) {
  btn_consume(&g_in.jump), g_in.jump.pressed = true;   /* (ConsumeBuffer: still Pressed this frame) */
  P->jump_grace = 0;
  P->var_jump = var_time;
  P->auto_jump = false;
  P->dash_attack = 0;
  P->wall_slide_timer = WallSlideTime;
  P->wall_boost_timer = 0;
}
static void jump(void) {
  jump_common(VarJumpTime);
  int lx, ly;
  lift_boost(&lx, &ly);
  P->sx += JumpHBoost * P->move_x + lx;
  P->sy = JumpSpeed + ly;
  P->var_jump_speed = P->sy;
  P->scale_x = 614, P->scale_y = 1434;
}
static void super_jump(void) {
  jump_common(VarJumpTime);
  int lx, ly;
  lift_boost(&lx, &ly);
  P->sx = SuperJumpH * P->facing + lx;
  P->sy = JumpSpeed + ly;
  if (Ducking) {
    set_ducking(false);
    P->sx = P->sx * 5 / 4;   /* DuckSuperJumpXMult */
    P->sy /= 2;              /* DuckSuperJumpYMult */
  }
  P->var_jump_speed = P->sy;
  P->scale_x = 614, P->scale_y = 1434;
}
static void wall_jump(int dir) {
  set_ducking(false);
  jump_common(VarJumpTime);
  if (P->move_x != 0) P->force_move_x = dir, P->force_move_x_timer = WallJumpForceTime;
  int lx, ly;
  lift_boost(&lx, &ly);
  P->sx = WallJumpHSpeed * dir + lx;
  P->sy = JumpSpeed + ly;
  P->var_jump_speed = P->sy;
  P->scale_x = 614, P->scale_y = 1434;
}
static void super_wall_jump(int dir) {
  set_ducking(false);
  jump_common(SuperWallJumpVarTime);
  int lx, ly;
  lift_boost(&lx, &ly);
  P->sx = SuperWallJumpH * dir + lx;
  P->sy = SuperWallJumpSpeed + ly;
  P->var_jump_speed = P->sy;
  P->scale_x = 614, P->scale_y = 1434;
}
static void climb_jump(void) {
  if (!P->on_ground) P->stamina -= ClimbJumpCost;
  jump();
  if (P->move_x == 0) P->wall_boost_dir = -P->facing, P->wall_boost_timer = ClimbJumpBoostTime;
}

void player_bounce(int from_y) {
  E = P->e;
  int8_t was[4] = {(int8_t)E->hw, (int8_t)E->hh, (int8_t)E->hx, (int8_t)E->hy};
  set_box(HB_NORMAL);
  move_v_exact(from_y - e_bottom(E), NULL);
  player_refill_dash();
  P->stamina = ClimbMaxStamina;
  set_state(ST_NORMAL);
  P->jump_grace = 0;
  P->var_jump = BounceVarJumpTime;
  P->auto_jump = true;
  P->auto_jump_timer = BounceAutoJumpTime;
  P->dash_attack = 0;
  P->wall_slide_timer = WallSlideTime;
  P->wall_boost_timer = 0;
  P->var_jump_speed = P->sy = BounceSpeed;
  P->scale_x = 614, P->scale_y = 1434;
  set_box(was);
}
void player_super_bounce(int from_y) {
  E = P->e;
  int8_t was[4] = {(int8_t)E->hw, (int8_t)E->hh, (int8_t)E->hx, (int8_t)E->hy};
  set_box(HB_NORMAL);
  move_v((from_y - e_bottom(E)) * MU, NULL);
  player_refill_dash();
  P->stamina = ClimbMaxStamina;
  set_state(ST_NORMAL);
  P->jump_grace = 0;
  P->var_jump = SuperBounceVarJumpTime;
  P->auto_jump = true;
  P->auto_jump_timer = 0;
  P->dash_attack = 0;
  P->wall_slide_timer = WallSlideTime;
  P->wall_boost_timer = 0;
  P->sx = 0;
  P->var_jump_speed = P->sy = SuperBounceSpeed;
  level_dir_shake(0, -1, TIM(.1f) / DTU);
  P->scale_x = 512, P->scale_y = 1536;
  set_box(was);
}
void player_side_bounce(int dir, int from_x, int from_y) {
  E = P->e;
  int8_t was[4] = {(int8_t)E->hw, (int8_t)E->hh, (int8_t)E->hx, (int8_t)E->hy};
  set_box(HB_NORMAL);
  move_v(iclamp(from_y - e_bottom(E), -4, 4) * MU, NULL);
  if (dir > 0) move_h((from_x - e_left(E)) * MU, NULL);
  else if (dir < 0) move_h((from_x - e_right(E)) * MU, NULL);
  player_refill_dash();
  P->stamina = ClimbMaxStamina;
  set_state(ST_NORMAL);
  P->jump_grace = 0;
  P->var_jump = BounceVarJumpTime;
  P->auto_jump = true;
  P->auto_jump_timer = 0;
  P->dash_attack = 0;
  P->wall_slide_timer = WallSlideTime;
  P->force_move_x = dir;
  P->force_move_x_timer = TIM(.3f);
  P->wall_boost_timer = 0;
  P->sx = SPD(240) * dir;
  P->var_jump_speed = P->sy = BounceSpeed;
  level_dir_shake(dir, 0, TIM(.1f) / DTU);
  P->scale_x = 1536, P->scale_y = 512;
  set_box(was);
}

/* ---------------------------------------------------------------- collisions */
static void on_collide_h(int dx, int dy, Ent *hit) {
  (void)dy, (void)hit;
  if (P->state == ST_DASH) {
    if (P->on_ground && duck_free_at(E->x + isign(P->sx), E->y)) {
      set_ducking(true);
      return;
    } else if (P->sy == 0 && P->sx != 0) {
      for (int i = 1; i <= DashCornerCorrection; i++)
        for (int j = 1; j >= -1; j -= 2)
          if (!solid_off(isign(P->sx), i * j)) {
            move_v_exact(i * j, NULL);
            move_h_exact(isign(P->sx), NULL);
            return;
          }
    }
  }
  if (P->wall_speed_retention <= 0) {
    P->wall_speed_retained = P->sx;
    P->wall_speed_retention = WallSpeedRetentionTime;
  }
  P->sx = 0;
  P->dash_attack = 0;
  (void)dx;
}
static void on_collide_v(int dx, int dy, Ent *hit) {
  (void)dx, (void)dy, (void)hit;
  if (P->sy > 0) {
    if (P->state == ST_DASH && !P->dash_started_on_ground) {
      if (P->sx <= 0)
        for (int i = -1; i >= -DashCornerCorrection; i--)
          if (!on_ground_at(E->x + i, E->y)) {
            move_h_exact(i, NULL);
            move_v_exact(1, NULL);
            return;
          }
      if (P->sx >= 0)
        for (int i = 1; i <= DashCornerCorrection; i++)
          if (!on_ground_at(E->x + i, E->y)) {
            move_h_exact(i, NULL);
            move_v_exact(1, NULL);
            return;
          }
    }
    if (P->dash_dx != 0 && P->dash_dy > 0 && P->sy > 0) {   /* a diagonal dash into the floor: a slide */
      P->dash_dx = isign(P->dash_dx) * 256;
      P->dash_dy = 0;
      P->sy = 0;
      P->sx = P->sx * 6 / 5;   /* DodgeSlideSpeedMult */
      set_ducking(true);
    }
    if (P->state != ST_CLIMB) {   /* a squash as she lands */
      int sq = imin(P->sy * 256 / FastMaxFall, 256);
      P->scale_x = 1024 + 614 * sq / 256;
      P->scale_y = 1024 - 614 * sq / 256;
    }
  } else if (P->sy < 0) {
    if (P->sx <= 0)
      for (int i = 1; i <= UpwardCornerCorrection; i++)
        if (!solid_off(-i, -1)) {
          E->x -= i, E->y -= 1;
          return;
        }
    if (P->sx >= 0)
      for (int i = 1; i <= UpwardCornerCorrection; i++)
        if (!solid_off(i, -1)) {
          E->x += i, E->y -= 1;
          return;
        }
    if (P->var_jump < VarJumpTime - CeilingVarJumpGrace) P->var_jump = 0;
  }
  P->dash_attack = 0;
  P->sy = 0;
}

/* ---------------------------------------------------------------- normal */
static void normal_begin(void) { P->max_fall = MaxFall; }
static void normal_end(void) {
  P->wall_boost_timer = 0;
  P->wall_speed_retention = 0;
  P->hop_wait_x = 0;
}
static bool can_dash(void) { return btn_pressed(&g_in.dash) && P->dash_cooldown <= 0 && P->dashes > 0; }
static int start_dash(void) {
  P->was_dash_b = P->dashes == 2;
  P->dashes = imax(P->dashes - 1, 0);
  btn_consume(&g_in.dash), g_in.dash.pressed = true;
  return ST_DASH;
}

static int normal_update(void) {
  int lx, ly;
  lift_boost(&lx, &ly);
  if (ly < 0 && P->was_on_ground && !P->on_ground && P->sy >= 0) P->sy = ly;
  if (g_in.grab.check && !is_tired() && !Ducking) {
    if (P->sy >= 0 && isign(P->sx) != -P->facing) {
      if (climb_check(P->facing, 0)) {
        set_ducking(false);
        return ST_CLIMB;
      }
      if (g_in.move_y < 1)
        for (int i = 1; i <= ClimbUpCheckDist; i++)
          if (!solid_off(0, -i) && climb_check(P->facing, -i)) {
            move_v_exact(-i, NULL);
            set_ducking(false);
            return ST_CLIMB;
          }
    }
  }
  if (can_dash()) {
    P->sx += lx, P->sy += ly;
    return start_dash();
  }
  if (Ducking) {
    if (P->on_ground && g_in.move_y != 1) {
      if (can_unduck()) {
        set_ducking(false);
        P->scale_x = 819, P->scale_y = 1229;
      } else if (P->sx == 0) {
        for (int i = DuckCorrectCheck; i > 0; i--) {
          if (can_unduck_at(E->x + i, E->y)) {
            move_h(DuckCorrectSlide, on_collide_h);
            break;
          } else if (can_unduck_at(E->x - i, E->y)) {
            move_h(-DuckCorrectSlide, on_collide_h);
            break;
          }
        }
      }
    }
  } else if (P->on_ground && g_in.move_y == 1 && P->sy >= 0) {
    set_ducking(true);
    P->scale_x = 1434, P->scale_y = 614;
  }
  if (Ducking && P->on_ground) P->sx = approach(P->sx, 0, DuckFriction);
  else {
    bool g = P->on_ground;
    if (iabs(P->sx) > MaxRun && isign(P->sx) == P->move_x)
      P->sx = approach(P->sx, MaxRun * P->move_x, g ? RunReduce : RunReduce * 65 / 100);
    else
      P->sx = approach(P->sx, MaxRun * P->move_x, g ? RunAccel : RunAccel * 65 / 100);
  }
  if (g_in.move_y == 1 && P->sy >= MaxFall) {
    P->max_fall = approach(P->max_fall, FastMaxFall, FastMaxAccel);
    int half = MaxFall + (FastMaxFall - MaxFall) / 2;
    if (P->sy >= half) {
      int l = imin(256, (P->sy - half) * 256 / (FastMaxFall - half));
      P->scale_x = 1024 - 512 * l / 256, P->scale_y = 1024 + 512 * l / 256;
    }
  } else
    P->max_fall = approach(P->max_fall, MaxFall, FastMaxAccel);
  if (!P->on_ground) {
    int max = P->max_fall;
    if ((P->move_x == P->facing || (P->move_x == 0 && g_in.grab.check)) && g_in.move_y != 1) {
      if (P->sy >= 0 && P->wall_slide_timer > 0 && climb_bounds_check(P->facing) && solid_off(P->facing, 0) &&
          can_unduck()) {
        set_ducking(false);
        P->wall_slide_dir = P->facing;
      }
      if (P->wall_slide_dir != 0)   /* Lerp(MaxFall, WallSlideStartMax, timer / WallSlideTime) */
        max = MaxFall + (int)((int32_t)(WallSlideStartMax - MaxFall) * P->wall_slide_timer / WallSlideTime);
    }
    bool half = iabs(P->sy) < HalfGravThreshold && (g_in.jump.check || P->auto_jump);
    P->sy = approach(P->sy, max, half ? Gravity / 2 : Gravity);
  }
  if (P->var_jump > 0) {
    if (P->auto_jump || g_in.jump.check) P->sy = imin(P->sy, P->var_jump_speed);
    else P->var_jump = 0;
  }
  if (btn_pressed(&g_in.jump)) {
    if (P->jump_grace > 0) jump();
    else if (can_unduck()) {
      if (wall_jump_check(1)) {
        if (P->facing == 1 && g_in.grab.check && P->stamina > 0) climb_jump();
        else if (player_dash_attacking() && P->dash_dx == 0 && P->dash_dy == -256) super_wall_jump(-1);
        else wall_jump(-1);
      } else if (wall_jump_check(-1)) {
        if (P->facing == -1 && g_in.grab.check && P->stamina > 0) climb_jump();
        else if (player_dash_attacking() && P->dash_dx == 0 && P->dash_dy == -256) super_wall_jump(1);
        else wall_jump(1);
      }
    }
  }
  return ST_NORMAL;
}

/* ---------------------------------------------------------------- climb */
static void climb_begin(void) {
  P->auto_jump = false;
  P->sx = 0;
  P->sy /= 5;   /* ClimbGrabYMult */
  P->wall_slide_timer = WallSlideTime;
  P->climb_no_move = ClimbNoMoveTime;
  P->wall_boost_timer = 0;
  P->last_climb_move = 0;
  for (int i = 0; i < ClimbCheckDist; i++)
    if (!solid_off(P->facing, 0)) E->x += P->facing;
    else break;
}
static void climb_end(void) { P->wall_speed_retention = 0; }
static void climb_hop(void) {
  P->hop_wait_x = 0;
  if (solid_off(P->facing, 0)) P->hop_wait_x = P->facing, P->hop_wait_x_speed = P->facing * ClimbHopX;
  else P->sx = P->facing * ClimbHopX;
  P->sy = imin(P->sy, ClimbHopY);
  P->force_move_x = 0;
  P->force_move_x_timer = ClimbHopForceTime;
  P->fast_jump = false;
}
static bool slip_check(int add_y) {
  int ax = P->facing == 1 ? e_right(E) : e_left(E) - 1, ay = e_top(E) + 4 + add_y;
  return !point_solid(ax, ay) && !point_solid(ax, ay + (-4 + add_y));
}
static bool ledge_blocked(int x, int y) {   /* spikes in the way of a hop (LedgeBlocker) */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->type <= ET_spikesRight && o->type >= ET_spikesUp && o->type != ET_spikesDown && (o->flags & EF_COLLIDABLE) &&
        ent_collide_at(E, x, y, o))
      return true;
  }
  return false;
}
static int climb_update(void) {
  P->climb_no_move -= DTU;
  if (P->on_ground) P->stamina = ClimbMaxStamina;
  if (btn_pressed(&g_in.jump) && (!Ducking || can_unduck())) {
    if (P->move_x == -P->facing) wall_jump(-P->facing);
    else climb_jump();
    return ST_NORMAL;
  }
  int lx, ly;
  if (can_dash()) {
    lift_boost(&lx, &ly);
    P->sx += lx, P->sy += ly;
    return start_dash();
  }
  if (!g_in.grab.check) {
    lift_boost(&lx, &ly);
    P->sx += lx, P->sy += ly;
    return ST_NORMAL;
  }
  if (!solid_off(P->facing, 0)) {
    if (P->sy < 0) climb_hop();
    return ST_NORMAL;
  }
  int target = 0;
  bool try_slip = false;
  if (P->climb_no_move <= 0) {
    if (g_in.move_y == -1) {
      target = ClimbUpSpeed;
      if (solid_off(0, -1) || ((ledge_blocked(E->x + P->facing * 8, E->y) || solid_off(0, -6)) && slip_check(-1))) {
        if (P->sy < 0) P->sy = 0;
        target = 0;
        try_slip = true;
      } else if (slip_check(0)) {
        climb_hop();
        return ST_NORMAL;
      }
    } else if (g_in.move_y == 1) {
      target = ClimbDownSpeed;
      if (P->on_ground) {
        if (P->sy > 0) P->sy = 0;
        target = 0;
      }
    } else
      try_slip = true;
  } else
    try_slip = true;
  P->last_climb_move = isign(target);
  if (try_slip && slip_check(0)) target = ClimbSlipSpeed;
  P->sy = approach(P->sy, target, ClimbAccel);
  if (g_in.move_y != 1 && P->sy > 0 && !solid_off(P->facing, 1)) P->sy = 0;
  if (P->climb_no_move <= 0) {
    if (P->last_climb_move == -1) P->stamina -= ClimbUpCost;
    else if (P->last_climb_move == 0) P->stamina -= ClimbStillCost;
  }
  if (P->stamina <= 0) {
    lift_boost(&lx, &ly);
    P->sx += lx, P->sy += ly;
    return ST_NORMAL;
  }
  return ST_CLIMB;
}

/* ---------------------------------------------------------------- dash */
static void dash_begin(void) {
  P->called_dash_events = false;
  P->dash_started_on_ground = P->on_ground;
  level_freeze(TIM(.05f));
  P->dash_cooldown = DashCooldown;
  P->dash_refill_cooldown = DashRefillCooldown;
  P->wall_slide_timer = WallSlideTime;
  P->dash_trail_timer = 0;
  P->dash_attack = DashAttackTime;
  P->before_dash_sx = P->sx, P->before_dash_sy = P->sy;
  P->sx = P->sy = 0;
  P->dash_dx = P->dash_dy = 0;
  if (!P->on_ground && Ducking && can_unduck()) set_ducking(false);
}
static void dash_end(void) { P->called_dash_events = true; }
static int dash_update(void) {
  if (P->dash_dy == 0) {
    for (int i = 0; i < g_nents; i++) {   /* up onto a jump-through a dash runs into */
      Ent *jt = &g_ents[i];
      if ((jt->flags & (EF_JUMPTHRU | EF_COLLIDABLE)) == (EF_JUMPTHRU | EF_COLLIDABLE) && ent_collide_at(E, E->x, E->y, jt) &&
          e_bottom(E) - e_top(jt) <= DashHJumpThruNudge)
        move_v_exact(e_top(jt) - e_bottom(E), NULL);
    }
    if (can_unduck() && btn_pressed(&g_in.jump) && P->jump_grace > 0) {
      super_jump();
      return ST_NORMAL;
    }
  }
  if (P->dash_dx == 0 && P->dash_dy == -256) {
    if (btn_pressed(&g_in.jump) && can_unduck()) {
      if (wall_jump_check(1)) { super_wall_jump(-1); return ST_NORMAL; }
      if (wall_jump_check(-1)) { super_wall_jump(1); return ST_NORMAL; }
    }
  } else if (btn_pressed(&g_in.jump) && can_unduck()) {
    if (wall_jump_check(1)) { wall_jump(-1); return ST_NORMAL; }
    if (wall_jump_check(-1)) { wall_jump(1); return ST_NORMAL; }
  }
  return ST_DASH;
}
static int co_dash(void) {
  switch (P->co_step) {
    case 0:
      P->co_step = 1;
      return 0;
    case 1: {
      int dx = P->last_aim_x, dy = P->last_aim_y;
      int nx = dx * DashSpeed / 256, ny = dy * DashSpeed / 256;
      if (dx && dy) nx = isign(dx) * 10182, ny = isign(dy) * 10182;   /* 240 * 0.70710677 */
      if (isign(P->before_dash_sx) == isign(nx) && iabs(P->before_dash_sx) > iabs(nx)) nx = P->before_dash_sx;
      P->sx = nx, P->sy = ny;
      P->dash_dx = dx, P->dash_dy = dy;
      level_dir_shake(isign(dx), isign(dy), TIM(.2f) / DTU);
      if (dx != 0) P->facing = isign(dx);
      P->called_dash_events = true;
      if (P->on_ground && P->dash_dx != 0 && P->dash_dy > 0 && P->sy > 0) {
        P->dash_dx = isign(P->dash_dx) * 256;
        P->dash_dy = 0;
        P->sy = 0;
        P->sx = P->sx * 6 / 5;
        set_ducking(true);
      }
      P->co_step = 2;
      return DashTime;
    }
    case 2:
      P->auto_jump = true;
      P->auto_jump_timer = 0;
      if (P->dash_dy <= 0) {
        int dx = P->dash_dx, dy = P->dash_dy;
        P->sx = dx * EndDashSpeed / 256, P->sy = dy * EndDashSpeed / 256;
        if (dx && dy) P->sx = isign(dx) * 6788, P->sy = isign(dy) * 6788;   /* 160 * 0.70710677 */
      }
      if (P->sy < 0) P->sy = P->sy * 3 / 4;   /* EndDashUpMult */
      set_state(ST_NORMAL);
      return -1;
  }
  return -1;
}

/* ---------------------------------------------------------------- intros */
static int intro_x, intro_y;
static int co_intro_jump(void) {
  switch (P->co_step) {
    case 0:
      intro_x = E->x, intro_y = E->y;
      E->depth = -1000000 / 64;
      P->facing = 1;
      E->y = g_level.room->y + g_level.room->h + 16;
      P->co_step = 1;
      return TIM(.5f);
    case 1:
      play(A_player_jumpSlow);
      P->co_step = 2;
      /* fall through */
    case 2:
      if (E->y > intro_y - 8) {
        E->remy -= SPD(120);
        int n = round_mu(E->remy, -1);
        E->y += n, E->remy -= n * MU;
        return 0;
      }
      E->remy = 0;
      P->sy = SPD(-100);
      P->co_step = 3;
      /* fall through */
    case 3:
      if (P->sy < 0) {
        P->sy += 800;
        return 0;
      }
      P->sy = 0;
      P->co_step = 4;
      return TIM(.1f);
    case 4:
      play(A_player_fallSlow);
      P->co_step = 5;
      /* fall through */
    case 5:
      if (!P->on_ground) {
        P->sy += 800;
        return 0;
      }
      E->x = intro_x, E->y = intro_y;
      E->depth = 0;
      level_dir_shake(0, 1, TIM(.3f) / DTU);
      set_state(ST_NORMAL);
      return -1;
  }
  return -1;
}
static int co_intro_walk(void) {
  switch (P->co_step) {
    case 0:
      intro_x = E->x, intro_y = E->y;
      if (P->facing > 0) E->x = g_level.room->x - 16;
      else E->x = g_level.room->x + g_level.room->w + 16;
      P->co_step = 1;
      return TIM(.3f);
    case 1:
      play(A_player_runSlow);
      P->co_step = 2;
      /* fall through */
    case 2:
      if (iabs(E->x - intro_x) > 2 && !solid_off(P->facing, 0)) {
        move_h(isign(intro_x - E->x) * SPD(64), NULL);
        return 0;
      }
      E->x = intro_x, E->y = intro_y;
      play(A_player_idle);
      P->co_step = 3;
      return TIM(.2f);
    default:
      set_state(ST_NORMAL);
      return -1;
  }
}
static void respawn_begin(void) {
  Room *rm = g_level.room;
  int fx = iclamp(E->x, rm->x + 40, rm->x + rm->w - 40), fy = iclamp(E->y, rm->y + 40, rm->y + rm->h - 40);
  P->respawn_fromx = fx - E->x, P->respawn_fromy = fy - E->y;
  P->intro_t = 0;
}
static void respawn_end(void) {}

/* ---------------------------------------------------------------- death */
void player_die(int dx, int dy) {
  if (P->dead) return;
  P->dead = true;
  P->sx = P->sy = 0;
  E = P->e;
  E->flags &= ~EF_COLLIDABLE;
  E->depth = -32000;
  level_shake(TIM(.3f) / DTU);
  P->body_step = 0;
  P->body_dx = dx, P->body_dy = dy;
  P->body_t = 0;
  P->death_t = -1;
  P->bodyx = E->x, P->bodyy = E->y;
  P->co_active = false;
  btn_consume(&g_in.dash), btn_consume(&g_in.jump);
}
/* PlayerDeadBody.DeathRoutine: a bounce back (0.5 s, stopped at 3/4), then the burst of hair, then the reload */
static void body_update(void) {
  if (P->body_step == 0) {
    if (P->body_dx || P->body_dy) {
      level_freeze(TIM(.05f));
      P->body_step = 1;
      return;
    }
    P->body_step = 3;
  }
  if (P->body_step == 1) {
    P->body_t += DTU;
    int t = imin(P->body_t * 256 / TIM(.5f), 256), u = 256 - t;
    int e = 256 - (int)((int32_t)u * u / 256 * u / 256);
    E->x = P->bodyx + P->body_dx * 24 * e / 256;
    E->y = P->bodyy + P->body_dy * 24 * e / 256;
    if (P->body_t >= TIM(.5f) * 3 / 4) P->body_step = 3;
    return;
  }
  if (P->body_step == 3) {
    E->y -= 5;
    level_shake(TIM(.3f) / DTU);
    P->death_t = 0;
    P->body_step = 4;
    P->body_t = 0;
    return;
  }
  P->death_t += DTU;
  P->body_t += DTU;
  if (P->body_step == 4 && P->body_t >= TIM(.834f * .65f)) {
    P->body_step = 5;
    g_level.wipe = 1;   /* the wipe, then the reload */
  }
  if (P->body_step == 5 && g_level.wipe >= 24) g_level.dead_reload = true;
}

/* ---------------------------------------------------------------- the update */
static void update_hair_color(void) {
  uint32_t c;
  if (P->dashes == 0 && P->dashes < P->max_dashes) c = UsedHairColor;
  else if (P->last_dashes != P->dashes) c = FlashHairColor, P->hair_flash = TIM(.12f);
  else if (P->hair_flash > 0) c = FlashHairColor, P->hair_flash -= DTU;
  else c = P->dashes == 2 ? TwoDashesHairColor : NormalHairColor;
  if (P->dashes == 0 && P->dashes < P->max_dashes && P->hair_rgb != UsedHairColor) {   /* Lerp to it, 6 a second */
    uint32_t a = P->hair_rgb, b = UsedHairColor;
    int r = (int)(a >> 16 & 255), g = (int)(a >> 8 & 255), bl = (int)(a & 255);
    r += ((int)(b >> 16 & 255) - r) * 26 / 256, g += ((int)(b >> 8 & 255) - g) * 26 / 256, bl += ((int)(b & 255) - bl) * 26 / 256;
    c = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
    if (iabs(r - 0x44) < 4 && iabs(g - 0xB7) < 4 && iabs(bl - 0xFF) < 4) c = UsedHairColor;
  }
  P->hair_rgb = c;
  P->last_dashes = P->dashes;
}

static void hair_update(void) {
  const uint8_t *m = player_meta(cur_sprite());
  int fac = P->facing;
  /* PlayerHair.AfterUpdate: the first node at the head, the others following, 3 px apart at most */
  P->hairx[0] = (E->x + (int8_t)m[0] * fac) << 8;
  P->hairy[0] = (E->y - 9 * P->scale_y / 1024 + (int8_t)m[1]) << 8;
  int tx = P->hairx[0] - fac * 128, ty = P->hairy[0] + 512;   /* the step: (-facing * 0.5, 2) */
  for (int i = 1; i < P->hair_count; i++) {
    int dx = tx - P->hairx[i], dy = ty - P->hairy[i];
    int l = isqrt((int32_t)dx * dx + (int32_t)dy * dy);
    int mv = (256 - i * 128 / P->hair_count) * 64 / 60;   /* (1 - i / n / 2) * 64 px a second */
    if (l <= mv) P->hairx[i] = tx, P->hairy[i] = ty;
    else if (l > 0) P->hairx[i] += (int)((int32_t)dx * mv / l), P->hairy[i] += (int)((int32_t)dy * mv / l);
    dx = P->hairx[i] - P->hairx[i - 1], dy = P->hairy[i] - P->hairy[i - 1];
    l = isqrt((int32_t)dx * dx + (int32_t)dy * dy);
    if (l > 768) P->hairx[i] = P->hairx[i - 1] + (int)((int32_t)dx * 768 / l), P->hairy[i] = P->hairy[i - 1] + (int)((int32_t)dy * 768 / l);
    tx = P->hairx[i] - fac * 128, ty = P->hairy[i] + 512;
  }
}

static void update_sprite(void) {
  P->scale_x = approach(P->scale_x, 1024, 30);
  P->scale_y = approach(P->scale_y, 1024, 30);
  if (!in_control()) return;
  if (player_dash_attacking()) {
    if (P->on_ground && P->dash_dy == 0 && !Ducking && P->sx != 0 && P->move_x == -isign(P->sx)) play(A_player_skid);
    else play(A_player_dash);
  } else if (P->state == ST_CLIMB) {
    if (P->last_climb_move < 0) play(A_player_climbup);
    else if (P->last_climb_move > 0) play(A_player_wallslide);
    else if (!solid_off(P->facing, 6)) play(A_player_dangling);
    else if (g_in.move_x == -P->facing) {
      if (P->anim != A_player_climbLookBack) play(A_player_climbLookBackStart);
    } else
      play(A_player_wallslide);
  } else if (Ducking && P->state == ST_NORMAL) {
    play(A_player_duck);
  } else if (P->on_ground) {
    P->fast_jump = false;
    if (P->move_x != 0 && solid_off(P->move_x, 0)) play(A_player_push);
    else if (iabs(P->sx) <= RunAccel / 40 * 60 / 60 && P->move_x == 0) {
      int f = P->facing;
      if (!solid_at(E->x + f, E->y + 2, E->x + f + 1, E->y + 3, E) && !solid_at(E->x + f * 4, E->y + 2, E->x + f * 4 + 1, E->y + 3, E) &&
          !jumpthru_first_outside(E, E->x + f * 4, E->y + 2))
        play(A_player_edge);
      else if (!solid_at(E->x - f, E->y + 2, E->x - f + 1, E->y + 3, E) &&
               !solid_at(E->x - f * 4, E->y + 2, E->x - f * 4 + 1, E->y + 3, E) && !jumpthru_first_outside(E, E->x - f * 4, E->y + 2))
        play(A_player_edgeBack);
      else if (g_in.move_y == -1) {
        if (P->anim != A_player_lookUp) play(A_player_lookUp);
      } else if (P->anim != A_player_idle && P->anim != A_player_idleA && P->anim != A_player_idleB && P->anim != A_player_idleC)
        play(A_player_idle);
    } else if (isign(P->sx) == -P->move_x && P->move_x != 0) {
      if (iabs(P->sx) > MaxRun) play(A_player_skid);
      else if (P->anim != A_player_skid) play(A_player_flip);
    } else if (P->anim != A_player_flip && P->anim != A_player_runSlow && P->anim != A_player_runFast &&
               P->anim != A_player_runStumble) {
      if (iabs(P->sx) < MaxRun / 2) play(A_player_runSlow);
      else play(A_player_runFast);
    }
  } else if (P->wall_slide_dir != 0) {
    play(A_player_wallslide);
  } else if (P->sy < 0) {
    if (P->fast_jump || iabs(P->sx) > MaxRun) {
      P->fast_jump = true;
      play(A_player_jumpFast);
    } else
      play(A_player_jumpSlow);
  } else {
    if (P->fast_jump || P->sy >= MaxFall) {
      P->fast_jump = true;
      play(A_player_fallFast);
    } else
      play(A_player_fallSlow);
  }
}

void player_update(void) {
  E = P->e;
  if (P->dead) {
    body_update();
    if (P->body_step < 4) sprite_update();
    return;
  }
  P->idle_timer += DTU;
  if (P->sx || P->sy) P->idle_timer = 0;
  if (P->strawb_reset > 0 && (P->strawb_reset -= DTU) <= 0) P->strawb_index = 0;
  if (P->sy >= 0) {
    Ent *first = solid_hit_off(0, 1);
    if (!first) first = jumpthru_first_outside(E, E->x, E->y + 1);
    P->on_ground = first != NULL;
    P->on_safe_ground = first && (first->flags & EF_SAFE);
  } else
    P->on_ground = P->on_safe_ground = false;
  if (g_level.frame % 6 == 0) P->flash = !P->flash;   /* (OnInterval .05) */
  if (P->wall_slide_dir != 0) {
    P->wall_slide_timer = imax(P->wall_slide_timer - DTU, 0);
    P->wall_slide_dir = 0;
  }
  if (P->wall_boost_timer > 0) {
    P->wall_boost_timer -= DTU;
    if (P->move_x == P->wall_boost_dir) {
      P->sx = WallJumpHSpeed * P->move_x;
      P->stamina += ClimbJumpCost;
      P->wall_boost_timer = 0;
    }
  }
  if (P->on_ground && P->state != ST_CLIMB) {
    P->auto_jump = false;
    P->stamina = ClimbMaxStamina;
    P->wall_slide_timer = WallSlideTime;
  }
  if (P->dash_attack > 0) P->dash_attack -= DTU;
  if (P->on_ground) P->jump_grace = JumpGraceTime;
  else if (P->jump_grace > 0) P->jump_grace -= DTU;
  if (P->dash_cooldown > 0) P->dash_cooldown -= DTU;
  if (P->dash_refill_cooldown > 0) P->dash_refill_cooldown -= DTU;
  else if (P->on_ground && (solid_off(0, 1) || jumpthru_off(0, 1))) {
    bool on_spikes = false;   /* (CollideCheck<Spikes>) */
    for (int i = 0; i < g_nents && !on_spikes; i++) {
      Ent *o = &g_ents[i];
      on_spikes = o->type >= ET_spikesUp && o->type <= ET_spikesRight && (o->flags & EF_COLLIDABLE) && ent_collide_at(E, E->x, E->y, o);
    }
    if (!on_spikes) player_refill_dash();
  }
  if (P->var_jump > 0) P->var_jump -= DTU;
  if (P->auto_jump_timer > 0) {
    if (P->auto_jump) {
      P->auto_jump_timer -= DTU;
      if (P->auto_jump_timer <= 0) P->auto_jump = false;
    } else
      P->auto_jump_timer = 0;
  }
  if (P->force_move_x_timer > 0) {
    P->force_move_x_timer -= DTU;
    P->move_x = P->force_move_x;
  } else
    P->move_x = g_in.move_x;
  if (P->move_x != 0 && in_control() && P->state != ST_CLIMB) {
    if (P->move_x != P->facing && Ducking) P->scale_x = 819, P->scale_y = 1229;
    P->facing = P->move_x;
  }
  aim(&P->last_aim_x, &P->last_aim_y);
  if (P->wall_speed_retention > 0) {
    if (isign(P->sx) == -isign(P->wall_speed_retained)) P->wall_speed_retention = 0;
    else if (!solid_off(isign(P->wall_speed_retained), 0)) {
      P->sx = P->wall_speed_retained;
      P->wall_speed_retention = 0;
    } else
      P->wall_speed_retention -= DTU;
  }
  if (P->hop_wait_x != 0) {
    if (isign(P->sx) == -P->hop_wait_x || P->sy > 0) P->hop_wait_x = 0;
    else if (!solid_off(P->hop_wait_x, 0)) {
      P->sx = P->hop_wait_x_speed;
      P->hop_wait_x = 0;
    }
  }
  P->hair_count = P->dashes > 1 ? 5 : 4;
  /* components: the hair's wave, the sprite, the state machine, then the actor's lift */
  P->hair_wave += 4;
  sprite_update();
  if (P->state == ST_INTRORESPAWN) {   /* the respawn's tween: 0.6 s */
    P->intro_t += DTU;
    if (P->intro_t >= TIM(.6f)) {
      set_state(ST_NORMAL);
      P->scale_x = 1536, P->scale_y = 512;
    }
  }
  if (P->state < 3 && UPD[P->state]) set_state(UPD[P->state]());
  co_update();
  /* the lift speed: kept 0.16 s */
  if (E->liftx || E->lifty) P->last_liftx = E->liftx, P->last_lifty = E->lifty, P->lift_timer = TIM(.16f);
  E->liftx = E->lifty = 0;
  if (P->lift_timer > 0 && (P->lift_timer -= DTU) <= 0) P->last_liftx = P->last_lifty = 0;
  if (!P->on_ground && P->sy <= 0 && (P->state != ST_CLIMB || P->last_climb_move == -1)) {   /* up through jump-throughs */
    bool in = false;
    for (int i = 0; i < g_nents && !in; i++) {
      Ent *o = &g_ents[i];
      in = (o->flags & (EF_JUMPTHRU | EF_COLLIDABLE)) == (EF_JUMPTHRU | EF_COLLIDABLE) && ent_collide_at(E, E->x, E->y, o);
    }
    if (in && !ledge_blocked(E->x, E->y - 2)) move_v(JumpThruAssistSpeed, NULL);
  }
  if (!P->on_ground && player_dash_attacking() && P->dash_dy == 0)
    if (solid_off(0, DashVFloorSnapDist) || jumpthru_off(0, DashVFloorSnapDist)) move_v_exact(DashVFloorSnapDist, NULL);
  if (P->sy > 0 && can_unduck() && !P->on_ground) set_ducking(false);
  if (P->state != ST_INTROJUMP || P->co_step >= 3) {
    move_h(P->sx, on_collide_h);
    move_v(P->sy, on_collide_v);
  }
  if (P->dead) return;
  update_sprite();
  if (in_control() && P->state != ST_INTROJUMP) {   /* the camera: 1 - 0.01 ^ dt of the way */
    int tx = player_cam_targetx() << 8, ty = player_cam_targety() << 8;
    g_level.camx += (int)(((int32_t)(tx - g_level.camx) * 1210) >> 14);
    g_level.camy += (int)(((int32_t)(ty - g_level.camy) * 1210) >> 14);
  }
  /* what she touches (PlayerCollider), with the hurt box */
  if (!P->dead) {
    int8_t was[4] = {(int8_t)E->hw, (int8_t)E->hh, (int8_t)E->hx, (int8_t)E->hy};
    set_box(Ducking ? HB_DUCK_HURT : HB_NORMAL_HURT);
    extern void ents_player_touch(Ent *p);
    ents_player_touch(E);
    if (P->dead) return;
    if (box_is(HB_DUCK_HURT) || box_is(HB_NORMAL_HURT)) set_box(was);
  }
  update_hair_color();
  P->was_on_ground = P->on_ground;
}

/* ---------------------------------------------------------------- camera and transitions */
int player_cam_targetx(void) {
  Room *rm = g_level.room;
  return iclamp(P->e->x - 160 + g_level.cam_offx, rm->x, rm->x + rm->w - 320);
}
int player_cam_targety(void) {
  Room *rm = g_level.room;
  return iclamp(P->e->y - 90 + g_level.cam_offy, rm->y, rm->y + rm->h - 180);
}
bool player_transition_to(int x, int y) {
  E = P->e;
  /* MoveTowardsX/Y: 60 px a second */
  int cx = E->x * MU + E->remx, cy = E->y * MU + E->remy;
  move_h(approach(cx, x * MU, SPD(60)) - cx, NULL);
  move_v(approach(cy, y * MU, SPD(60)) - cy, NULL);
  hair_update();
  if (E->x == x && E->y == y) {
    E->remx = E->remy = 0;
    P->sx = P->sx / 60 * 60, P->sy = P->sy / 60 * 60;   /* Speed rounded to whole pixels a second */
    return true;
  }
  return false;
}
void player_on_transition(void) {
  P->wall_slide_timer = WallSlideTime;
  P->jump_grace = 0;
  P->force_move_x_timer = 0;
  player_refill_dash();
  P->stamina = ClimbMaxStamina;
}
void player_before_up_transition(void) {
  P->sx = 0;
  P->var_jump_speed = P->sy = JumpSpeed;
  set_state(ST_NORMAL);
  P->auto_jump = true;
  P->auto_jump_timer = 0;
  P->var_jump = VarJumpTime;
  P->dash_cooldown = TIM(.2f);
}
void player_before_down_transition(void) {
  set_state(ST_NORMAL);
  P->sy = imax(0, P->sy);
  P->auto_jump = false;
  P->var_jump = 0;
}

/* ---------------------------------------------------------------- drawing */
static void hair_draw(bool outline) {
  const uint8_t *m = player_meta(cur_sprite());
  if (!(m[2] & 0x80)) return;
  int n = P->hair_count, bangs = m[2] & 0x7F;
  for (int i = outline ? 0 : n - 1; outline ? i < n : i >= 0; i += outline ? 1 : -1) {
    const uint16_t *mask = hair_mask(i == 0 ? 1 + (bangs < 3 ? bangs : 0) : 0);
    int scale = 64 + (256 - i * 256 / n) * 3 / 4;   /* 0.25 + (1 - i / n) * 0.75 */
    int cx = (i == 0 ? P->hairx[0] >> 8 : (P->hairx[i] + 128) >> 8) - g_camx;
    int cy = (i == 0 ? P->hairy[0] >> 8 : (P->hairy[i] + 128) >> 8) - g_camy;
    bool flip = i == 0 && P->facing < 0;
    int sx = (i == 0 ? 256 : scale) * P->scale_x / 1024, sy = i == 0 ? 256 : scale;
    /* the 10 x 10 picture, its centre (5, 5) at (cx, cy) */
    int x0 = cx - (5 * sx + 128) / 256, x1 = cx + (5 * sx + 128) / 256, y0 = cy - (5 * sy + 128) / 256, y1 = cy + (5 * sy + 128) / 256;
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++) {
        int u = 5 + ((x - cx) * 256 + 128) / sx, v = 5 + ((y - cy) * 256 + 128) / sy;
        if ((unsigned)u >= 10 || (unsigned)v >= 10) continue;
        if (flip) u = 9 - u;
        if (!(mask[v] >> u & 1)) continue;
        if (outline) {
          static const int8_t O[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
          for (int k = 0; k < 4; k++) {
            int px = x + O[k][0], py = y + O[k][1];
            if ((unsigned)px < VIEW_W && (unsigned)py < VIEW_H) g_fb[(VIEW_Y + py) * 320 + px] = BLACK;
          }
        } else if ((unsigned)x < VIEW_W && (unsigned)y < VIEW_H)
          g_fb[(VIEW_Y + y) * 320 + x] = mask[10 + v] >> u & 1 ? 2 : 1;
      }
  }
}

static void death_effect(int x, int y, int t) {   /* DeathEffect: 8 hair circles flying out, t 0..256 */
  int e = 256 - (int)((int32_t)(256 - t) * (256 - t) / 256 * (256 - t) / 256);
  int s = t < 128 ? 128 + t : (256 - (int)((int32_t)(t * 2 - 256) * (t * 2 - 256) / 256 * (t * 2 - 256) / 256));
  int r = 4 * s / 256;
  uint8_t col = (t * 10 / 256) % 2 == 0 ? 1 : WHITE;
  for (int i = 0; i < 8; i++) {
    int a = i * 128 + t / 4;
    int px = x + sin256(a + 256) * 24 * e / 65536, py = y + sin256(a) * 24 * e / 65536;
    draw_circle_fill(px, py, r + 1, BLACK);
  }
  for (int i = 0; i < 8; i++) {
    int a = i * 128 + t / 4;
    int px = x + sin256(a + 256) * 24 * e / 65536, py = y + sin256(a) * 24 * e / 65536;
    draw_circle_fill(px, py, r, col);
  }
}

void player_render(void) {
  E = P->e;
  if (P->dead) {
    if (P->body_step < 4) {
      const uint8_t *s = sprite(cur_sprite());
      draw_sprite_scaled(s, E->x, E->y, 256, 256, 16, 32, P->facing < 0 ? DF_FLIPX : 0);
    } else if (P->death_t >= 0 && P->death_t <= TIM(.834f))
      death_effect(E->x, E->y - 6, P->death_t * 256 / TIM(.834f));
    return;
  }
  palette_hair(P->hair_rgb);
  if (P->state == ST_INTRORESPAWN) {
    int t = imin(P->intro_t * 256 / TIM(.6f), 256);
    death_effect(E->x + P->respawn_fromx * (256 - t) / 256, E->y - 6 + P->respawn_fromy * (256 - t) / 256, 256 - t);
    return;
  }
  hair_draw(true);
  hair_draw(false);
  const uint8_t *s = sprite(cur_sprite());
  bool red = is_tired() && P->flash;
  if (red) g_draw_color = 1;
  draw_sprite_scaled(s, E->x, E->y, P->scale_x / 4, P->scale_y / 4, 16, 32, (P->facing < 0 ? DF_FLIPX : 0));
}

void player_spawn(int x, int y, int intro) {
  memset(P, 0, sizeof *P);
  Ent *e = ent_new(ET_player, x, y);
  P->e = e;
  E = e;
  e->room = 2;
  e->depth = 0;
  set_box(HB_NORMAL);
  P->facing = 1;
  P->max_dashes = 1;
  P->dashes = P->last_dashes = 1;
  P->stamina = ClimbMaxStamina;
  P->wall_slide_timer = WallSlideTime;
  P->max_fall = MaxFall;
  P->scale_x = P->scale_y = 1024;
  P->hair_rgb = NormalHairColor;
  P->hair_count = 4;
  P->anim = -1;
  play(A_player_idle);
  P->state = -1;
  if (e->x > g_level.room->x + g_level.room->w / 2 && intro != INTRO_NONE) P->facing = -1;
  switch (intro) {
    case INTRO_RESPAWN: set_state(ST_INTRORESPAWN); break;
    case INTRO_JUMP: set_state(ST_INTROJUMP); break;
    case INTRO_WALKINRIGHT: P->facing = 1, set_state(ST_INTROWALK); break;
    case INTRO_WALKINLEFT: P->facing = -1, set_state(ST_INTROWALK); break;
    default: set_state(ST_NORMAL); break;
  }
  for (int i = 0; i < HAIR_NODES; i++) P->hairx[i] = (x - P->facing * 2) << 8, P->hairy[i] = (y - 10) << 8;
  hair_update();
}

void player_after_update(void) {
  if (P->e && (!P->dead || P->body_step < 4)) {
    E = P->e;
    hair_update();
  }
}
