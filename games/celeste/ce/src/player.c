/* Madeline: Player.cs (NoelFB/Celeste) as src/player.c ports it for the NumWorks: the same constants, states and
 * order of operations, and the same single-precision arithmetic (sf.c), so she moves pixel for pixel as there. */
#include "ce.h"
#include "fconst.h"

Player g_player;
#define P (&g_player)
static Ent *E;
#define DT F_DT

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

extern Ent *solid_hit(int l, int t, int r, int b, const Ent *self);
static bool solid_off(int dx, int dy) { return solid_at(e_left(E) + dx, e_top(E) + dy, e_right(E) + dx, e_bottom(E) + dy, E); }
static Ent *solid_hit_off(int dx, int dy) { return solid_hit(e_left(E) + dx, e_top(E) + dy, e_right(E) + dx, e_bottom(E) + dy, E); }
static bool jumpthru_off(int dx, int dy) { return jumpthru_first_outside(E, E->x + dx, E->y + dy) != NULL; }
static bool point_solid(int x, int y) { return solid_at(x, y, x + 1, y + 1, E); }
static f32 ftimes(f32 a, int k) { return f_mul(a, f_int(k)); }   /* a * k (k a small whole number) */
static f32 dec(f32 t) { return f_sub(t, DT); }

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
static bool move_h(f32 amount, Collide cb) {
  E->remx = f_add(E->remx, amount);
  int n = f_round(E->remx);
  if (!n) return false;
  E->remx = f_sub(E->remx, f_int(n));
  return move_h_exact(n, cb);
}
static bool move_v(f32 amount, Collide cb) {
  E->remy = f_add(E->remy, amount);
  int n = f_round(E->remy);
  if (!n) return false;
  E->remy = f_sub(E->remy, f_int(n));
  return move_v_exact(n, cb);
}
static bool on_ground_at(int x, int y) {
  int ox = E->x, oy = E->y;
  E->x = x, E->y = y;
  bool r = solid_off(0, 1) || jumpthru_off(0, 1);
  E->x = ox, E->y = oy;
  return r;
}

/* the lift speed: what the platform under her gives (kept 0.16 s after she leaves it) */
static f32 lift_x(void) { return (E->liftx || E->lifty) ? E->liftx : P->last_liftx; }
static f32 lift_y(void) { return (E->liftx || E->lifty) ? E->lifty : P->last_lifty; }
static void lift_boost(f32 *x, f32 *y) {
  f32 lx = lift_x(), ly = lift_y();
  if (f_lt(F_LiftXCap, f_abs(lx))) lx = (lx & 0x80000000ul) | F_LiftXCap;
  if (f_pos(ly)) ly = 0;
  else if (f_lt(ly, F_LiftYCap)) ly = F_LiftYCap;
  *x = lx, *y = ly;
}

/* ---------------------------------------------------------------- what the platforms do to her */
bool player_climbing(void) { return P->state == ST_CLIMB; }
bool player_riding(const Ent *s) {   /* IsRiding(Solid) */
  Ent *e = P->e;
  if (P->state == ST_CLIMB) return ent_collide_at(e, e->x + P->facing, e->y, s);
  return ent_collide_at(e, e->x, e->y + 1, s);
}
bool player_riding_jumpthru(const Ent *j) {
  Ent *e = P->e;
  return P->state != ST_CLIMB && !f_negative(P->sy) && ent_collide_at(e, e->x, e->y + 1, j) && !ent_collide_at(e, e->x, e->y, j);
}
void player_set_lift(f32 lx, f32 ly) {
  P->e->liftx = lx, P->e->lifty = ly;
  if (lx || ly) P->last_liftx = lx, P->last_lifty = ly, P->lift_timer = F_LiftTime;
}
static bool can_unduck(void);
/* squished between solids: ducking first, then a wiggle of up to 3 pixels, else dead */
static void on_squish(int dx, int dy, Ent *hit) {
  (void)dx, (void)dy, (void)hit;
  bool ducked = false;
  if (!Ducking) {
    ducked = true;
    set_ducking(true);
    if (!solid_off(0, 0)) return;
  }
  for (int i = 0; i <= 3; i++)
    for (int j = 0; j <= 3; j++) {
      if (!i && !j) continue;
      for (int sx = 1; sx >= -1; sx -= 2)
        for (int sy = 1; sy >= -1; sy -= 2)
          if (!solid_off(i * sx, j * sy)) {
            E->x += i * sx, E->y += j * sy;
            if (ducked && can_unduck()) set_ducking(false);
            return;
          }
    }
  player_die(0, 0);
}
void player_push_h(int move, Ent *pusher) { E = P->e; (void)pusher; move_h_exact(move, on_squish); }
void player_push_v(int move, Ent *pusher) { E = P->e; (void)pusher; move_v_exact(move, on_squish); }
void player_carry_h(int move) { E = P->e; move_h_exact(move, NULL); }
void player_carry_v(int move) { E = P->e; move_v_exact(move, NULL); }

/* ---------------------------------------------------------------- the sprite */
static void play(int a) {
  if (P->anim == a) return;
  P->anim = a, P->frame = 0, P->anim_timer = 0;
}
static int cur_sprite(void) {
  const uint8_t *an = anim(P->anim);
  int f = imin(P->frame, an[0] - 1);
  return (int)R16(an + 6 + 2 * f);
}
static void sprite_update(void) {   /* (frame timing in 600ths of a second: only the look depends on it) */
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
    if (g == A_player_idle && P->anim == A_player_idle && P->idle_timer > 3 * 60 && rndn(5) == 0) {
      int r = rndn(9);   /* the idle's variants */
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
bool player_in_control(void) { return in_control(); }
bool player_dash_attacking(void) { return f_pos(P->dash_attack); }
static bool is_tired(void) {
  f32 s = f_pos(P->wall_boost_timer) ? f_add(P->stamina, F_ClimbJumpCost) : P->stamina;
  return f_lt(s, F_ClimbTiredThreshold);
}
bool player_refill_dash(void) {
  if (P->dashes < P->max_dashes) {
    P->dashes = P->max_dashes;
    return true;
  }
  return false;
}
bool player_use_refill(bool two) {
  int max = two ? 2 : P->max_dashes;
  if (P->dashes < max || f_lt(P->stamina, F_ClimbTiredThreshold)) {
    P->dashes = max;
    P->stamina = F_ClimbMaxStamina;
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
  return e_left(E) + dir * 2 >= rm->x && e_right(E) + dir * 2 < rm->x + rm->w;
}
static bool climb_check(int dir, int yadd) { return climb_bounds_check(dir) && solid_off(dir * 2, yadd); }
static bool wall_jump_check(int dir) { return climb_bounds_check(dir) && solid_off(dir * 3, 0); }

static void aim(void) {
  int ax = g_in.aim_x, ay = g_in.aim_y;
  P->aim_x = ax, P->aim_y = ay;
  if (!ax && !ay) P->aim_x = P->facing;
}

static int normal_update(void), climb_update(void), dash_update(void);
static void normal_begin(void), normal_end(void), climb_begin(void), climb_end(void), dash_begin(void), dash_end(void);
static void respawn_begin(void);
static int32_t co_dash(void), co_intro_jump(void), co_intro_walk(void);
typedef int (*UpdFn)(void);
typedef void (*VFn)(void);
static const UpdFn UPD[ST_COUNT] = {normal_update, climb_update, dash_update};
static const VFn BEGIN[ST_COUNT] = {normal_begin, climb_begin, dash_begin, respawn_begin};
static const VFn END[ST_COUNT] = {normal_end, climb_end, dash_end};
typedef int32_t (*CoFn)(void);
static const CoFn CO[ST_COUNT] = {NULL, NULL, co_dash, NULL, co_intro_jump, co_intro_walk};

static void set_state(int st) {
  if (P->state == st) return;
  P->prev_state = P->state;
  P->state = st;
  if (P->prev_state >= 0 && END[P->prev_state]) END[P->prev_state]();
  if (BEGIN[st]) BEGIN[st]();
  P->co_active = CO[st] != NULL;
  P->co_step = 0, P->co_wait = 0;
}
/* the state's coroutine: a step returns how long to wait (a float's bits), or -1 when it ends */
static void co_update(void) {
  if (!P->co_active) return;
  if (f_pos(P->co_wait)) {
    P->co_wait = dec(P->co_wait);
    return;
  }
  int st = P->state;
  int32_t r = CO[st]();
  if (P->state != st || !P->co_active) return;
  if (r < 0) P->co_active = false;
  else P->co_wait = (f32)r;
}

/* ---------------------------------------------------------------- jumps */
static void jump_reset(f32 var_time) {
  btn_consume_buffer(&g_in.jump);
  P->jump_grace = 0;
  P->var_jump = var_time;
  P->auto_jump = false;
  P->dash_attack = 0;
  P->wall_slide_timer = F_WallSlideTime;
  P->wall_boost_timer = 0;
}
static void jump(void) {
  jump_reset(F_VarJumpTime);
  f32 lx, ly;
  P->sx = f_add(P->sx, ftimes(F_JumpHBoost, P->move_x));
  P->sy = F_JumpSpeed;
  lift_boost(&lx, &ly);
  P->sx = f_add(P->sx, lx), P->sy = f_add(P->sy, ly);
  P->var_jump_speed = P->sy;
  P->scale_x = 614, P->scale_y = 1434;
}
static void super_jump(void) {
  jump_reset(F_VarJumpTime);
  f32 lx, ly;
  P->sx = ftimes(F_SuperJumpH, P->facing);
  P->sy = F_JumpSpeed;
  lift_boost(&lx, &ly);
  P->sx = f_add(P->sx, lx), P->sy = f_add(P->sy, ly);
  if (Ducking) {
    set_ducking(false);
    P->sx = f_mul(P->sx, F_DuckSuperJumpXMult);
    P->sy = f_mul(P->sy, F_DuckSuperJumpYMult);
  }
  P->var_jump_speed = P->sy;
  P->scale_x = 614, P->scale_y = 1434;
}
static void wall_jump(int dir) {
  set_ducking(false);
  jump_reset(F_VarJumpTime);
  if (P->move_x != 0) P->force_move_x = dir, P->force_move_x_timer = F_WallJumpForceTime;
  f32 lx, ly;
  if (!lift_x() && !lift_y()) {   /* the wall's own lift speed */
    Ent *w = solid_first(e_left(E) + 3, e_top(E), e_right(E) + 3, e_bottom(E), E);
    if (w) player_set_lift(w->liftx, w->lifty);
  }
  P->sx = ftimes(F_WallJumpHSpeed, dir);
  P->sy = F_JumpSpeed;
  lift_boost(&lx, &ly);
  P->sx = f_add(P->sx, lx), P->sy = f_add(P->sy, ly);
  P->var_jump_speed = P->sy;
  P->scale_x = 614, P->scale_y = 1434;
}
static void super_wall_jump(int dir) {
  set_ducking(false);
  jump_reset(F_SuperWallJumpVarTime);
  f32 lx, ly;
  P->sx = ftimes(F_SuperWallJumpH, dir);
  P->sy = F_SuperWallJumpSpeed;
  lift_boost(&lx, &ly);
  P->sx = f_add(P->sx, lx), P->sy = f_add(P->sy, ly);
  P->var_jump_speed = P->sy;
  P->scale_x = 614, P->scale_y = 1434;
}
static void climb_jump(void) {
  if (!P->on_ground) P->stamina = f_sub(P->stamina, F_ClimbJumpCost);
  jump();
  if (P->move_x == 0) P->wall_boost_dir = -P->facing, P->wall_boost_timer = F_ClimbJumpBoostTime;
}

static void save_box(int8_t *w) { w[0] = (int8_t)E->hw, w[1] = (int8_t)E->hh, w[2] = (int8_t)E->hx, w[3] = (int8_t)E->hy; }
static void bounce_common(f32 var_time) {
  player_refill_dash();
  P->stamina = F_ClimbMaxStamina;
  set_state(ST_NORMAL);
  P->jump_grace = 0;
  P->var_jump = var_time;
  P->auto_jump = true;
  P->dash_attack = 0;
  P->wall_slide_timer = F_WallSlideTime;
  P->wall_boost_timer = 0;
}
void player_bounce(int from_y) {
  E = P->e;
  int8_t was[4];
  save_box(was);
  set_box(HB_NORMAL);
  move_v_exact(from_y - e_bottom(E), NULL);
  bounce_common(F_BounceVarJumpTime);
  P->auto_jump_timer = F_BounceAutoJumpTime;
  P->var_jump_speed = P->sy = F_BounceSpeed;
  P->scale_x = 614, P->scale_y = 1434;
  set_box(was);
}
void player_super_bounce(int from_y) {
  E = P->e;
  int8_t was[4];
  save_box(was);
  set_box(HB_NORMAL);
  move_v(f_int(from_y - e_bottom(E)), NULL);
  bounce_common(F_SuperBounceVarJumpTime);
  P->auto_jump_timer = 0;
  P->sx = 0;
  P->var_jump_speed = P->sy = F_SuperBounceSpeed;
  level_dir_shake(0, -1, 6);
  P->scale_x = 512, P->scale_y = 1536;
  set_box(was);
}
void player_side_bounce(int dir, int from_x, int from_y) {
  E = P->e;
  int8_t was[4];
  save_box(was);
  set_box(HB_NORMAL);
  move_v(f_int(iclamp(from_y - e_bottom(E), -4, 4)), NULL);
  if (dir > 0) move_h(f_int(from_x - e_left(E)), NULL);
  else if (dir < 0) move_h(f_int(from_x - e_right(E)), NULL);
  bounce_common(F_BounceVarJumpTime);
  P->auto_jump_timer = 0;
  P->force_move_x = dir;
  P->force_move_x_timer = F_SideBounceForce;
  P->sx = ftimes(F_SideBounceSpeed, dir);
  P->var_jump_speed = P->sy = F_BounceSpeed;
  level_dir_shake(dir, 0, 6);
  P->scale_x = 1536, P->scale_y = 512;
  set_box(was);
}
static void rebound(int dir) {   /* Player.Rebound: off a dash block */
  P->sx = ftimes(F_ReboundSpeedX, dir);
  P->sy = F_ReboundSpeedY;
  P->var_jump_speed = P->sy;
  P->var_jump = F_ReboundVarJumpTime;
  P->auto_jump = true;
  P->auto_jump_timer = 0;
  P->dash_attack = 0;
  P->wall_slide_timer = F_WallSlideTime;
  P->wall_boost_timer = 0;
  P->force_move_x_timer = 0;
  set_state(ST_NORMAL);
}

/* ---------------------------------------------------------------- collisions */
extern void ents_dash_into(Ent *hit, int dx, int dy, bool *rebound);
static void on_collide_h(int dx, int dy, Ent *hit) {
  (void)dy;
  if (player_dash_attacking() && dx == f_sign(P->dash_dx)) {
    bool rb;
    ents_dash_into(hit, dx, 0, &rb);
    if (rb) { rebound(-f_sign(P->sx)); return; }
  }
  if (P->state == ST_DASH) {
    if (P->on_ground && duck_free_at(E->x + f_sign(P->sx), E->y)) {
      set_ducking(true);
      return;
    } else if (f_zero(P->sy) && !f_zero(P->sx)) {
      for (int i = 1; i <= 4; i++)   /* DashCornerCorrection */
        for (int j = 1; j >= -1; j -= 2)
          if (!solid_off(f_sign(P->sx), i * j)) {
            move_v_exact(i * j, NULL);
            move_h_exact(f_sign(P->sx), NULL);
            return;
          }
    }
  }
  if (!f_pos(P->wall_speed_retention)) {
    P->wall_speed_retained = P->sx;
    P->wall_speed_retention = F_WallSpeedRetentionTime;
  }
  P->sx = 0;
  P->dash_attack = 0;
}
static void on_collide_v(int dx, int dy, Ent *hit) {
  (void)dx;
  if (player_dash_attacking() && dy == f_sign(P->dash_dy)) {
    bool rb;
    ents_dash_into(hit, 0, dy, &rb);
    if (rb) { rebound(0); return; }
  }
  if (f_pos(P->sy)) {
    if (P->state == ST_DASH && !P->dash_started_on_ground) {
      if (!f_pos(P->sx))
        for (int i = -1; i >= -4; i--)
          if (!on_ground_at(E->x + i, E->y)) {
            move_h_exact(i, NULL);
            move_v_exact(1, NULL);
            return;
          }
      if (!f_negative(P->sx))
        for (int i = 1; i <= 4; i++)
          if (!on_ground_at(E->x + i, E->y)) {
            move_h_exact(i, NULL);
            move_v_exact(1, NULL);
            return;
          }
    }
    if (!f_zero(P->dash_dx) && f_pos(P->dash_dy) && f_pos(P->sy)) {   /* a diagonal dash into the floor: a slide */
      P->dash_dx = f_sign(P->dash_dx) > 0 ? F_1 : f_neg(F_1);
      P->dash_dy = 0;
      P->sy = 0;
      P->sx = f_mul(P->sx, F_DodgeSlideSpeedMult);
      set_ducking(true);
    }
    if (P->state != ST_CLIMB) {   /* the landing's squash (Lerp(1, 1.6), Lerp(1, .4)) */
      int sq = imin(f_trunc(f_mul(P->sy, f_int(256))) / 240, 256);
      P->scale_x = 1024 + 614 * sq / 256;
      P->scale_y = 1024 - 614 * sq / 256;
    }
  } else {
    if (f_negative(P->sy)) {
      if (!f_pos(P->sx))
        for (int i = 1; i <= 4; i++)   /* UpwardCornerCorrection */
          if (!solid_off(-i, -1)) {
            E->x -= i, E->y -= 1;
            return;
          }
      if (!f_negative(P->sx))
        for (int i = 1; i <= 4; i++)
          if (!solid_off(i, -1)) {
            E->x += i, E->y -= 1;
            return;
          }
      if (f_lt(P->var_jump, f_sub(F_VarJumpTime, F_CeilingVarJumpGrace))) P->var_jump = 0;
    }
  }
  P->dash_attack = 0;
  P->sy = 0;
}

/* ---------------------------------------------------------------- normal */
static void normal_begin(void) { P->max_fall = F_MaxFall; }
static void normal_end(void) {
  P->wall_boost_timer = 0;
  P->wall_speed_retention = 0;
  P->hop_wait_x = 0;
}
static bool can_dash(void) { return btn_pressed(&g_in.dash) && !f_pos(P->dash_cooldown) && P->dashes > 0; }
static int start_dash(void) {
  P->was_dash_b = P->dashes == 2;
  P->dashes = imax(P->dashes - 1, 0);
  btn_consume_buffer(&g_in.dash);
  return ST_DASH;
}

static int normal_update(void) {
  f32 lx, ly;
  lift_boost(&lx, &ly);
  if (f_negative(ly) && P->was_on_ground && !P->on_ground && !f_negative(P->sy)) P->sy = ly;
  if (g_in.grab.check && !is_tired() && !Ducking) {
    if (!f_negative(P->sy) && f_sign(P->sx) != -P->facing) {
      if (climb_check(P->facing, 0)) {
        set_ducking(false);
        return ST_CLIMB;
      }
      if (g_in.move_y < 1)
        for (int i = 1; i <= 2; i++)   /* ClimbUpCheckDist */
          if (!solid_off(0, -i) && climb_check(P->facing, -i)) {
            move_v_exact(-i, NULL);
            set_ducking(false);
            return ST_CLIMB;
          }
    }
  }
  if (can_dash()) {
    lift_boost(&lx, &ly);
    P->sx = f_add(P->sx, lx), P->sy = f_add(P->sy, ly);
    return start_dash();
  }
  if (Ducking) {
    if (P->on_ground && g_in.move_y != 1) {
      if (can_unduck()) {
        set_ducking(false);
        P->scale_x = 819, P->scale_y = 1229;
      } else if (f_zero(P->sx)) {
        for (int i = 4; i > 0; i--) {   /* DuckCorrectCheck */
          if (can_unduck_at(E->x + i, E->y)) {
            move_h(f_mul(F_DuckCorrectSlide, DT), NULL);
            break;
          } else if (can_unduck_at(E->x - i, E->y)) {
            move_h(f_mul(f_neg(F_DuckCorrectSlide), DT), NULL);
            break;
          }
        }
      }
    }
  } else if (P->on_ground && g_in.move_y == 1 && !f_negative(P->sy)) {
    set_ducking(true);
    P->scale_x = 1434, P->scale_y = 614;
  }
  if (Ducking && P->on_ground) P->sx = f_approach(P->sx, 0, f_mul(F_DuckFriction, DT));
  else {
    bool g = P->on_ground;
    f32 target = ftimes(F_MaxRun, P->move_x);
    if (f_lt(F_MaxRun, f_abs(P->sx)) && f_sign(P->sx) == P->move_x)
      P->sx = f_approach(P->sx, target, f_mul(g ? F_RunReduce : F_RunReduceAir, DT));
    else
      P->sx = f_approach(P->sx, target, f_mul(g ? F_RunAccel : F_RunAccelAir, DT));
  }
  if (g_in.move_y == 1 && !f_lt(P->sy, F_MaxFall)) {
    P->max_fall = f_approach(P->max_fall, F_FastMaxFall, f_mul(F_FastMaxAccel, DT));
    if (!f_lt(P->sy, F_FastHalf)) {   /* the stretch: Lerp(1, .5), Lerp(1, 1.5) */
      int l = imin(256, f_trunc(f_mul(f_sub(P->sy, F_FastHalf), f_int(256))) / 40);
      P->scale_x = 1024 - 512 * l / 256, P->scale_y = 1024 + 512 * l / 256;
    }
  } else
    P->max_fall = f_approach(P->max_fall, F_MaxFall, f_mul(F_FastMaxAccel, DT));
  if (!P->on_ground) {
    f32 max = P->max_fall;
    if ((P->move_x == P->facing || (P->move_x == 0 && g_in.grab.check)) && g_in.move_y != 1) {
      if (!f_negative(P->sy) && f_pos(P->wall_slide_timer) && climb_bounds_check(P->facing) && solid_off(P->facing, 0) &&
          can_unduck()) {
        set_ducking(false);
        P->wall_slide_dir = P->facing;
      }
      if (P->wall_slide_dir != 0)
        max = f_lerp(F_MaxFall, F_WallSlideStartMax, f_div(P->wall_slide_timer, F_WallSlideTime));
    }
    bool half = f_lt(f_abs(P->sy), F_HalfGravThreshold) && (g_in.jump.check || P->auto_jump);
    P->sy = f_approach(P->sy, max, f_mul(half ? f_mul(F_Gravity, F_HALF) : F_Gravity, DT));
  }
  if (f_pos(P->var_jump)) {
    if (P->auto_jump || g_in.jump.check) P->sy = f_min(P->sy, P->var_jump_speed);
    else P->var_jump = 0;
  }
  if (btn_pressed(&g_in.jump)) {
    if (f_pos(P->jump_grace)) jump();
    else if (can_unduck()) {
      bool up_dash = player_dash_attacking() && f_zero(P->dash_dx) && P->dash_dy == f_neg(F_1);
      if (wall_jump_check(1)) {
        if (P->facing == 1 && g_in.grab.check && f_pos(P->stamina)) climb_jump();
        else if (up_dash) super_wall_jump(-1);
        else wall_jump(-1);
      } else if (wall_jump_check(-1)) {
        if (P->facing == -1 && g_in.grab.check && f_pos(P->stamina)) climb_jump();
        else if (up_dash) super_wall_jump(1);
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
  P->sy = f_mul(P->sy, F_ClimbGrabYMult);
  P->wall_slide_timer = F_WallSlideTime;
  P->climb_no_move = F_ClimbNoMoveTime;
  P->wall_boost_timer = 0;
  P->last_climb_move = 0;
  for (int i = 0; i < 2; i++)
    if (!solid_off(P->facing, 0)) E->x += P->facing;
    else break;
}
static void climb_end(void) { P->wall_speed_retention = 0; }
static void climb_hop(void) {
  Ent *s = solid_first(e_left(E) + P->facing, e_top(E), e_right(E) + P->facing, e_bottom(E), E);
  P->climb_hop_solid = s;
  if (s || tiles_solid(e_left(E) + P->facing, e_top(E), e_right(E) + P->facing, e_bottom(E))) {
    if (s) P->climb_hop_x = s->x, P->climb_hop_y = s->y;
    P->hop_wait_x = P->facing;
    P->hop_wait_x_speed = ftimes(F_ClimbHopX, P->facing);
  } else {
    P->hop_wait_x = 0;
    P->sx = ftimes(F_ClimbHopX, P->facing);
  }
  P->sy = f_min(P->sy, F_ClimbHopY);
  P->force_move_x = 0;
  P->force_move_x_timer = F_ClimbHopForceTime;
  P->fast_jump = false;
}
static bool slip_check(int add_y) {
  int ax = P->facing == 1 ? e_right(E) : e_left(E) - 1, ay = e_top(E) + 4 + add_y;
  return !point_solid(ax, ay) && !point_solid(ax, ay + (-4 + add_y));
}
static bool ledge_blocked(int x, int y) {   /* spikes in the way of a hop (LedgeBlocker) */
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if ((o->type == ET_spikesUp || o->type == ET_spikesLeft || o->type == ET_spikesRight) && (o->flags & EF_COLLIDABLE) &&
        ent_collide_at(E, x, y, o))
      return true;
  }
  return false;
}
static int climb_update(void) {
  P->climb_no_move = dec(P->climb_no_move);
  if (P->on_ground) P->stamina = F_ClimbMaxStamina;
  if (btn_pressed(&g_in.jump) && (!Ducking || can_unduck())) {
    if (P->move_x == -P->facing) wall_jump(-P->facing);
    else climb_jump();
    return ST_NORMAL;
  }
  f32 lx, ly;
  if (can_dash()) {
    lift_boost(&lx, &ly);
    P->sx = f_add(P->sx, lx), P->sy = f_add(P->sy, ly);
    return start_dash();
  }
  if (!g_in.grab.check) {
    lift_boost(&lx, &ly);
    P->sx = f_add(P->sx, lx), P->sy = f_add(P->sy, ly);
    return ST_NORMAL;
  }
  if (!solid_off(P->facing, 0)) {
    if (f_negative(P->sy)) climb_hop();
    return ST_NORMAL;
  }
  f32 target = 0;
  bool try_slip = false;
  if (!f_pos(P->climb_no_move)) {
    if (g_in.move_y == -1) {
      target = F_ClimbUpSpeed;
      if (solid_off(0, -1) || ((ledge_blocked(E->x + P->facing * 8, E->y) || solid_off(0, -6)) && slip_check(-1))) {
        if (f_negative(P->sy)) P->sy = 0;
        target = 0;
        try_slip = true;
      } else if (slip_check(0)) {
        climb_hop();
        return ST_NORMAL;
      }
    } else if (g_in.move_y == 1) {
      target = F_ClimbDownSpeed;
      if (P->on_ground) {
        if (f_pos(P->sy)) P->sy = 0;
        target = 0;
      }
    } else
      try_slip = true;
  } else
    try_slip = true;
  P->last_climb_move = f_sign(target);
  if (try_slip && slip_check(0)) target = F_ClimbSlipSpeed;
  P->sy = f_approach(P->sy, target, f_mul(F_ClimbAccel, DT));
  if (g_in.move_y != 1 && f_pos(P->sy) && !solid_off(P->facing, 1)) P->sy = 0;
  if (!f_pos(P->climb_no_move)) {
    if (P->last_climb_move == -1) P->stamina = f_sub(P->stamina, f_mul(F_ClimbUpCost, DT));
    else if (P->last_climb_move == 0) P->stamina = f_sub(P->stamina, f_mul(F_ClimbStillCost, DT));
  }
  if (!f_pos(P->stamina)) {
    lift_boost(&lx, &ly);
    P->sx = f_add(P->sx, lx), P->sy = f_add(P->sy, ly);
    return ST_NORMAL;
  }
  return ST_CLIMB;
}

/* ---------------------------------------------------------------- dash */
static void dash_begin(void) {
  P->called_dash_events = false;
  P->dash_started_on_ground = P->on_ground;
  level_freeze(TIM(.05f));
  P->dash_cooldown = F_DashCooldown;
  P->dash_refill_cooldown = F_DashRefillCooldown;
  P->wall_slide_timer = F_WallSlideTime;
  P->dash_attack = F_DashAttackTime;
  P->before_dash_sx = P->sx, P->before_dash_sy = P->sy;
  P->sx = P->sy = 0;
  P->dash_dx = P->dash_dy = 0;
  if (!P->on_ground && Ducking && can_unduck()) set_ducking(false);
}
static void dash_end(void) { P->called_dash_events = true; }
static int dash_update(void) {
  if (f_zero(P->dash_dy)) {
    for (int i = 0; i < g_nents; i++) {   /* up onto a jump-through a dash runs into */
      Ent *jt = &g_ents[i];
      if ((jt->flags & (EF_JUMPTHRU | EF_COLLIDABLE)) == (EF_JUMPTHRU | EF_COLLIDABLE) && ent_collide_at(E, E->x, E->y, jt) &&
          e_bottom(E) - e_top(jt) <= 6)
        move_v_exact(e_top(jt) - e_bottom(E), NULL);
    }
    if (can_unduck() && btn_pressed(&g_in.jump) && f_pos(P->jump_grace)) {
      super_jump();
      return ST_NORMAL;
    }
  }
  if (f_zero(P->dash_dx) && P->dash_dy == f_neg(F_1)) {
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
static f32 unit(int s) { return s > 0 ? F_1 : s < 0 ? f_neg(F_1) : 0; }
static int32_t co_dash(void) {
  switch (P->co_step) {
    case 0:
      P->co_step = 1;
      return 0;
    case 1: {
      int ax = P->aim_x, ay = P->aim_y;
      f32 nx, ny, dx, dy;
      if (ax && ay) {
        dx = ax > 0 ? F_Diag : f_neg(F_Diag), dy = ay > 0 ? F_Diag : f_neg(F_Diag);
        nx = ax > 0 ? F_DashDiag : f_neg(F_DashDiag), ny = ay > 0 ? F_DashDiag : f_neg(F_DashDiag);
      } else {
        dx = unit(ax), dy = unit(ay);
        nx = ftimes(F_DashSpeed, ax), ny = ftimes(F_DashSpeed, ay);
      }
      if (f_sign(P->before_dash_sx) == f_sign(nx) && f_lt(f_abs(nx), f_abs(P->before_dash_sx))) nx = P->before_dash_sx;
      P->sx = nx, P->sy = ny;
      P->dash_dx = dx, P->dash_dy = dy;
      level_dir_shake(ax, ay, 12);
      if (ax) P->facing = ax;
      P->called_dash_events = true;
      { extern void ents_on_dash(void); ents_on_dash(); }
      if (P->on_ground && !f_zero(P->dash_dx) && f_pos(P->dash_dy) && f_pos(P->sy)) {
        P->dash_dx = unit(f_sign(P->dash_dx));
        P->dash_dy = 0;
        P->sy = 0;
        P->sx = f_mul(P->sx, F_DodgeSlideSpeedMult);
        set_ducking(true);
      }
      P->co_step = 2;
      return (int32_t)F_DashTime;
    }
    case 2:
      P->auto_jump = true;
      P->auto_jump_timer = 0;
      if (!f_pos(P->dash_dy)) P->sx = f_mul(P->dash_dx, F_EndDashSpeed), P->sy = f_mul(P->dash_dy, F_EndDashSpeed);
      if (f_negative(P->sy)) P->sy = f_mul(P->sy, F_EndDashUpMult);
      set_state(ST_NORMAL);
      return -1;
  }
  return -1;
}

/* ---------------------------------------------------------------- intros */
static int intro_x, intro_y;
static int32_t co_intro_jump(void) {
  switch (P->co_step) {
    case 0:
      intro_x = E->x, intro_y = E->y;
      E->depth = -32000;
      P->facing = 1;
      E->y = g_level.room->y + g_level.room->h + 16;
      P->co_step = 1;
      return (int32_t)F_Hop;
    case 1:
      play(A_player_jumpSlow);
      P->co_step = 2;
      /* fall through */
    case 2:
      if (E->y > intro_y - 8) {   /* Y += -120 * DT (a float position: its rest is kept) */
        P->intro_fy = f_add(P->intro_fy, F_IntroRise);
        int n = f_trunc(P->intro_fy);
        E->y += n, P->intro_fy = f_sub(P->intro_fy, f_int(n));
        return 0;
      }
      P->sy = F_IntroJumpSpeed;
      P->co_step = 3;
      /* fall through */
    case 3:
      if (f_negative(P->sy)) {
        P->sy = f_add(P->sy, F_IntroGravity);
        return 0;
      }
      P->sy = 0;
      P->co_step = 4;
      return (int32_t)F_JumpGraceTime;
    case 4:
      play(A_player_fallSlow);
      P->co_step = 5;
      /* fall through */
    case 5:
      if (!P->on_ground) {
        P->sy = f_add(P->sy, F_IntroGravity);
        return 0;
      }
      E->x = intro_x, E->y = intro_y;
      E->depth = 0;
      level_dir_shake(0, 1, 18);
      set_state(ST_NORMAL);
      return -1;
  }
  return -1;
}
static int32_t co_intro_walk(void) {
  switch (P->co_step) {
    case 0:
      intro_x = E->x, intro_y = E->y;
      E->x = P->facing > 0 ? g_level.room->x - 16 : g_level.room->x + g_level.room->w + 16;
      P->co_step = 1;
      return (int32_t)F_SideBounceForce;
    case 1:
      play(A_player_runSlow);
      P->co_step = 2;
      /* fall through */
    case 2:
      if (iabs(E->x - intro_x) > 2 && !solid_off(P->facing, 0)) {   /* MoveTowardsX(start, 64 * DT) */
        f32 at = f_add(f_int(E->x), E->remx), to = f_int(intro_x);
        move_h(f_sub(f_approach(at, to, F_WalkIn), at), NULL);
        return 0;
      }
      E->x = intro_x, E->y = intro_y;
      play(A_player_idle);
      P->co_step = 3;
      return (int32_t)F_ClimbHopForceTime;
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

/* ---------------------------------------------------------------- death */
void player_die(int dx, int dy) {
  if (P->dead) return;
  extern void save_add_death(void), ents_lose_followers(void);
  save_add_death();
  ents_lose_followers();
  P->dead = true;
  P->sx = P->sy = 0;
  E = P->e;
  E->flags &= ~EF_COLLIDABLE;
  E->depth = -32000;
  level_shake(18);
  P->body_step = 0;
  P->body_dx = dx, P->body_dy = dy;
  P->body_t = 0;
  P->death_t = -1;
  P->bodyx = E->x, P->bodyy = E->y;
  P->co_active = false;
  btn_consume_buffer(&g_in.dash), btn_consume_buffer(&g_in.jump);
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
    level_shake(18);
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

/* ---------------------------------------------------------------- the hair */
static void update_hair_color(void) {
  uint32_t c;
  if (P->dashes == 0 && P->dashes < P->max_dashes) {   /* Lerp to the used colour, 6 a second */
    uint32_t a = P->hair_rgb, b = UsedHairColor;
    int r = (int)(a >> 16 & 255), g = (int)(a >> 8 & 255), bl = (int)(a & 255);
    r += ((int)(b >> 16 & 255) - r) * 26 / 256, g += ((int)(b >> 8 & 255) - g) * 26 / 256, bl += ((int)(b & 255) - bl) * 26 / 256;
    c = (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)bl;
    if (iabs(r - 0x44) < 6 && iabs(g - 0xB7) < 6 && iabs(bl - 0xFF) < 6) c = UsedHairColor;
  } else if (P->last_dashes != P->dashes) c = FlashHairColor, P->hair_flash = TIM(.12f);
  else if (P->hair_flash > 0) c = FlashHairColor, P->hair_flash -= DTU;
  else c = P->dashes == 2 ? TwoDashesHairColor : NormalHairColor;
  P->hair_rgb = c;
  P->last_dashes = P->dashes;
}
static void hair_update(void) {   /* PlayerHair.AfterUpdate (1/256 px): the nodes follow, 3 px apart at most */
  const uint8_t *m = player_meta(cur_sprite());
  int fac = P->facing;
  P->hairx[0] = (E->x + (int8_t)m[0] * fac) * 256;
  P->hairy[0] = (E->y - 9 * P->scale_y / 1024 + (int8_t)m[1]) * 256;
  int tx = P->hairx[0] - fac * 128, ty = P->hairy[0] + 512;
  for (int i = 1; i < P->hair_count; i++) {
    int dx = tx - P->hairx[i], dy = ty - P->hairy[i];
    if (iabs(dx) > 32 * 256 || iabs(dy) > 32 * 256) P->hairx[i] = tx, P->hairy[i] = ty, dx = dy = 0;
    int l = isqrt((int32_t)dx * dx + (int32_t)dy * dy);
    int mv = (256 - i * 128 / P->hair_count) * 64 / 60;
    if (l <= mv) P->hairx[i] = tx, P->hairy[i] = ty;
    else P->hairx[i] += (int)((int32_t)dx * mv / l), P->hairy[i] += (int)((int32_t)dy * mv / l);
    dx = P->hairx[i] - P->hairx[i - 1], dy = P->hairy[i] - P->hairy[i - 1];
    l = isqrt((int32_t)dx * dx + (int32_t)dy * dy);
    if (l > 768) P->hairx[i] = P->hairx[i - 1] + (int)((int32_t)dx * 768 / l), P->hairy[i] = P->hairy[i - 1] + (int)((int32_t)dy * 768 / l);
    tx = P->hairx[i] - fac * 128, ty = P->hairy[i] + 512;
  }
}

/* ---------------------------------------------------------------- the sprite's animation */
static void update_sprite(void) {
  P->scale_x = approach(P->scale_x, 1024, 30);
  P->scale_y = approach(P->scale_y, 1024, 30);
  if (!in_control()) return;
  if (player_dash_attacking()) {
    if (P->on_ground && f_zero(P->dash_dy) && !Ducking && !f_zero(P->sx) && P->move_x == -f_sign(P->sx)) play(A_player_skid);
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
    else if (!f_lt(F_RunAccel40, f_abs(P->sx)) && P->move_x == 0) {
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
    } else if (f_sign(P->sx) == -P->move_x && P->move_x != 0) {
      if (f_lt(F_MaxRun, f_abs(P->sx))) play(A_player_skid);
      else if (P->anim != A_player_skid) play(A_player_flip);
    } else if (P->anim != A_player_flip && P->anim != A_player_runSlow && P->anim != A_player_runFast &&
               P->anim != A_player_runStumble) {
      if (f_lt(f_abs(P->sx), f_mul(F_MaxRun, F_HALF))) play(A_player_runSlow);
      else play(A_player_runFast);
    }
  } else if (P->wall_slide_dir != 0) {
    play(A_player_wallslide);
  } else if (f_negative(P->sy)) {
    if (P->fast_jump || f_lt(F_MaxRun, f_abs(P->sx))) {
      P->fast_jump = true;
      play(A_player_jumpFast);
    } else
      play(A_player_jumpSlow);
  } else {
    if (P->fast_jump || !f_lt(P->sy, F_MaxFall)) {
      P->fast_jump = true;
      play(A_player_fallFast);
    } else
      play(A_player_fallSlow);
  }
}

/* ---------------------------------------------------------------- the update */
void player_update(void) {
  E = P->e;
  if (P->dead) {
    body_update();
    if (P->body_step < 4) sprite_update();
    return;
  }
  P->idle_timer++;
  if (P->sx || P->sy) P->idle_timer = 0;
  if (f_pos(P->strawb_reset) && !f_pos(P->strawb_reset = dec(P->strawb_reset))) P->strawb_index = 0;
  if (!f_negative(P->sy)) {
    Ent *first = solid_hit_off(0, 1);
    if (!first) first = jumpthru_first_outside(E, E->x, E->y + 1);
    P->on_ground = first != NULL;
    P->on_safe_ground = first && (first->flags & EF_SAFE);
  } else
    P->on_ground = P->on_safe_ground = false;
  if (g_level.frame % 6 == 0) P->flash = !P->flash;   /* (OnInterval .05) */
  if (P->wall_slide_dir != 0) {
    P->wall_slide_timer = f_max(dec(P->wall_slide_timer), 0);
    P->wall_slide_dir = 0;
  }
  if (f_pos(P->wall_boost_timer)) {
    P->wall_boost_timer = dec(P->wall_boost_timer);
    if (P->move_x == P->wall_boost_dir) {
      P->sx = ftimes(F_WallJumpHSpeed, P->move_x);
      P->stamina = f_add(P->stamina, F_ClimbJumpCost);
      P->wall_boost_timer = 0;
    }
  }
  if (P->on_ground && P->state != ST_CLIMB) {
    P->auto_jump = false;
    P->stamina = F_ClimbMaxStamina;
    P->wall_slide_timer = F_WallSlideTime;
  }
  if (f_pos(P->dash_attack)) P->dash_attack = dec(P->dash_attack);
  if (P->on_ground) P->jump_grace = F_JumpGraceTime;
  else if (f_pos(P->jump_grace)) P->jump_grace = dec(P->jump_grace);
  if (f_pos(P->dash_cooldown)) P->dash_cooldown = dec(P->dash_cooldown);
  if (f_pos(P->dash_refill_cooldown)) P->dash_refill_cooldown = dec(P->dash_refill_cooldown);
  else if (P->on_ground && (solid_off(0, 1) || jumpthru_off(0, 1))) {
    bool on_spikes = false;   /* (CollideCheck<Spikes>) */
    for (int i = 0; i < g_nents && !on_spikes; i++) {
      Ent *o = &g_ents[i];
      on_spikes = o->type >= ET_spikesUp && o->type <= ET_spikesRight && (o->flags & EF_COLLIDABLE) && ent_collide_at(E, E->x, E->y, o);
    }
    if (!on_spikes) player_refill_dash();
  }
  if (f_pos(P->var_jump)) P->var_jump = dec(P->var_jump);
  if (f_pos(P->auto_jump_timer)) {
    if (P->auto_jump) {
      P->auto_jump_timer = dec(P->auto_jump_timer);
      if (!f_pos(P->auto_jump_timer)) P->auto_jump = false;
    } else
      P->auto_jump_timer = 0;
  }
  if (f_pos(P->force_move_x_timer)) {
    P->force_move_x_timer = dec(P->force_move_x_timer);
    P->move_x = P->force_move_x;
  } else {
    P->move_x = g_in.move_x;
    P->climb_hop_solid = NULL;
  }
  if (P->climb_hop_solid && !(P->climb_hop_solid->flags & EF_COLLIDABLE)) P->climb_hop_solid = NULL;
  else if (P->climb_hop_solid && (P->climb_hop_solid->x != P->climb_hop_x || P->climb_hop_solid->y != P->climb_hop_y)) {
    int mx = P->climb_hop_solid->x - P->climb_hop_x, my = P->climb_hop_solid->y - P->climb_hop_y;
    P->climb_hop_x = P->climb_hop_solid->x, P->climb_hop_y = P->climb_hop_solid->y;
    move_h_exact(mx, NULL);
    move_v_exact(my, NULL);
  }
  if (P->move_x != 0 && in_control() && P->state != ST_CLIMB) {
    if (P->move_x != P->facing && Ducking) P->scale_x = 819, P->scale_y = 1229;
    P->facing = P->move_x;
  }
  aim();
  if (f_pos(P->wall_speed_retention)) {
    if (f_sign(P->sx) == -f_sign(P->wall_speed_retained)) P->wall_speed_retention = 0;
    else if (!solid_off(f_sign(P->wall_speed_retained), 0)) {
      P->sx = P->wall_speed_retained;
      P->wall_speed_retention = 0;
    } else
      P->wall_speed_retention = dec(P->wall_speed_retention);
  }
  if (P->hop_wait_x != 0) {
    if (f_sign(P->sx) == -P->hop_wait_x || f_pos(P->sy)) P->hop_wait_x = 0;
    else if (!solid_off(P->hop_wait_x, 0)) {
      P->sx = P->hop_wait_x_speed;
      P->hop_wait_x = 0;
    }
  }
  P->hair_count = P->dashes > 1 ? 5 : 4;
  /* components: the sprite, the state machine, then the actor's lift */
  sprite_update();
  if (P->state == ST_INTRORESPAWN) {   /* the respawn's tween: 0.6 s */
    P->intro_t += DTU;
    if (P->intro_t >= TIM(.6f)) {
      set_state(ST_NORMAL);
      P->scale_x = 1536, P->scale_y = 512;
    }
  }
  if (P->state < 3) set_state(UPD[P->state]());
  co_update();
  E->liftx = E->lifty = 0;   /* (LiftSpeed: the moves of this frame set it again) */
  if (f_pos(P->lift_timer) && !f_pos(P->lift_timer = dec(P->lift_timer))) P->last_liftx = P->last_lifty = 0;
  if (!P->on_ground && !f_pos(P->sy) && (P->state != ST_CLIMB || P->last_climb_move == -1)) {   /* up through jump-throughs */
    bool in = false;
    for (int i = 0; i < g_nents && !in; i++) {
      Ent *o = &g_ents[i];
      in = (o->flags & (EF_JUMPTHRU | EF_COLLIDABLE)) == (EF_JUMPTHRU | EF_COLLIDABLE) && ent_collide_at(E, E->x, E->y, o);
    }
    if (in && !ledge_blocked(E->x, E->y - 2)) move_v(f_mul(F_JumpThruAssistSpeed, DT), NULL);
  }
  if (!P->on_ground && player_dash_attacking() && f_zero(P->dash_dy))
    if (solid_off(0, 3) || jumpthru_off(0, 3)) move_v_exact(3, NULL);   /* DashVFloorSnapDist */
  if (f_pos(P->sy) && can_unduck() && !P->on_ground) set_ducking(false);
  if (P->state != ST_INTROJUMP || P->co_step >= 3) {
    move_h(f_mul(P->sx, DT), on_collide_h);
    move_v(f_mul(P->sy, DT), on_collide_v);
  }
  if (P->dead) return;
  update_sprite();
  if (in_control()) {   /* the camera: 1 - 0.01 ^ dt of the way */
    int tx = player_cam_targetx() * 256, ty = player_cam_targety() * 256;
    g_level.camx += (int)(((int32_t)(tx - g_level.camx) * 1210) >> 14);
    g_level.camy += (int)(((int32_t)(ty - g_level.camy) * 1210) >> 14);
  }
  /* what she touches (PlayerCollider), with the hurt box */
  int8_t was[4];
  save_box(was);
  set_box(Ducking ? HB_DUCK_HURT : HB_NORMAL_HURT);
  extern void ents_player_touch(Ent *p);
  ents_player_touch(E);
  if (P->dead) return;
  if (box_is(HB_DUCK_HURT) || box_is(HB_NORMAL_HURT)) set_box(was);
  update_hair_color();
  P->was_on_ground = P->on_ground;
}

/* ---------------------------------------------------------------- camera and transitions */
extern int g_anchor_x, g_anchor_y, g_anchor_lerp;
extern uint8_t g_anchor_ignore;
static int cam_target(int px, int off, int anchor, bool ignore, int lo, int hi) {
  int t = px + off;
  if (g_anchor_lerp > 0 && !ignore) t += (int)((int32_t)(anchor - t) * g_anchor_lerp / 256);
  return iclamp(t, lo, hi);
}
int player_cam_targetx(void) {
  Room *rm = g_level.room;
  return cam_target(P->e->x - 160, g_level.cam_offx, g_anchor_x, g_anchor_ignore & 2, rm->x, rm->x + rm->w - 320);
}
int player_cam_targety(void) {
  Room *rm = g_level.room;
  return cam_target(P->e->y - 90, g_level.cam_offy, g_anchor_y, g_anchor_ignore & 1, rm->y, rm->y + rm->h - 180);
}
bool player_transition_to(int x, int y) {
  E = P->e;
  f32 cx = f_add(f_int(E->x), E->remx), cy = f_add(f_int(E->y), E->remy);   /* MoveTowardsX/Y: 60 px a second */
  move_h(f_sub(f_approach(cx, f_int(x), F_TransitionSpeed), cx), NULL);
  cy = f_add(f_int(E->y), E->remy);
  move_v(f_sub(f_approach(cy, f_int(y), F_TransitionSpeed), cy), NULL);
  hair_update();
  if (E->x == x && E->y == y) {
    E->remx = E->remy = 0;
    P->sx = f_int(f_round(P->sx)), P->sy = f_int(f_round(P->sy));
    return true;
  }
  return false;
}
void player_on_transition(void) {
  P->wall_slide_timer = F_WallSlideTime;
  P->jump_grace = 0;
  P->force_move_x_timer = 0;
  player_refill_dash();
  P->stamina = F_ClimbMaxStamina;
}
void player_before_up_transition(void) {
  P->sx = 0;
  P->var_jump_speed = P->sy = F_JumpSpeed;
  set_state(ST_NORMAL);
  P->auto_jump = true;
  P->auto_jump_timer = 0;
  P->var_jump = F_VarJumpTime;
  P->dash_cooldown = F_DashCooldown;
}
void player_before_down_transition(void) {
  set_state(ST_NORMAL);
  P->sy = f_max(0, P->sy);
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
    if (sx < 16) sx = 16;
    int x0 = cx - (5 * sx + 128) / 256, x1 = cx + (5 * sx + 128) / 256, y0 = cy - (5 * sy + 128) / 256, y1 = cy + (5 * sy + 128) / 256;
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++) {
        int u = 5 + fdiv((x - cx) * 256 + 128, sx), v = 5 + fdiv((y - cy) * 256 + 128, sy);
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
          g_fb[(VIEW_Y + y) * 320 + x] = mask[10 + v] >> u & 1 ? HAIR_DARK : HAIR;
      }
  }
}

static void death_effect(int x, int y, int t) {   /* DeathEffect: 8 hair circles flying out, t 0..256 */
  int e = 256 - (int)((int32_t)(256 - t) * (256 - t) / 256 * (256 - t) / 256);
  int s = t < 128 ? 128 + t : (256 - (int)((int32_t)(t * 2 - 256) * (t * 2 - 256) / 256 * (t * 2 - 256) / 256));
  int r = 4 * s / 256;
  uint8_t col = (t * 10 / 256) % 2 == 0 ? HAIR : WHITE;
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < 8; i++) {
      int a = i * 128 + t / 4;
      int px = x + (int)((int32_t)sin15(a + 256) * 24 * e / 8388608), py = y + (int)((int32_t)sin15(a) * 24 * e / 8388608);
      draw_circle_fill(px, py, pass ? r : r + 1, pass ? col : BLACK);
    }
}

void player_render(void) {
  E = P->e;
  if (P->dead) {
    if (P->body_step < 4) draw_sprite_scaled(sprite(cur_sprite()), E->x, E->y, 256, 256, 16, 32, P->facing < 0 ? DF_FLIPX : 0);
    else if (P->death_t >= 0 && P->death_t <= TIM(.834f)) death_effect(E->x, E->y - 6, P->death_t * 256 / TIM(.834f));
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
  extern const uint8_t *g_remap;
  extern const uint8_t *g_remap_red;
  if (is_tired() && P->flash) g_remap = g_remap_red;   /* (Color.Red: tired) */
  draw_sprite_scaled(sprite(cur_sprite()), E->x, E->y, P->scale_x / 4, P->scale_y / 4, 16, 32, P->facing < 0 ? DF_FLIPX : 0);
  g_remap = NULL;
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
  P->stamina = F_ClimbMaxStamina;
  P->wall_slide_timer = F_WallSlideTime;
  P->max_fall = F_MaxFall;
  P->scale_x = P->scale_y = 1024;
  P->hair_rgb = NormalHairColor;
  P->hair_count = 4;
  P->aim_x = 1;
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
  for (int i = 0; i < HAIR_NODES; i++) P->hairx[i] = (x - P->facing * 2) * 256, P->hairy[i] = (y - 10) * 256;
  hair_update();
}

void player_after_update(void) {
  if (P->e && (!P->dead || P->body_step < 4)) {
    E = P->e;
    hair_update();
  }
}
