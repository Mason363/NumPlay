#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC optimize("Os")   /* not drawn every frame: smaller over faster */
#endif
/* Madeline. A line-by-line port of Player.cs (NoelFB/Celeste, MIT): the same
 * constants, states and order of operations, with Monocle's coroutines
 * written as steps. */
#include "player.h"
#include "level.h"
#include "fx.h"
#include "entities.h"
#include "wipe.h"
#include "talk.h"
#include "cutscene.h"

Player g_player;

#define MaxFall 160.f
#define Gravity 900.f
#define HalfGravThreshold 40.f
#define FastMaxFall 240.f
#define FastMaxAccel 300.f
#define MaxRun 90.f
#define RunAccel 1000.f
#define RunReduce 400.f
#define AirMult .65f
#define HoldingMaxRun 70.f
#define HoldMinTime .35f
#define BounceAutoJumpTime .1f
#define DuckFriction 500.f
#define DuckCorrectCheck 4
#define DuckCorrectSlide 50.f
#define DodgeSlideSpeedMult 1.2f
#define DuckSuperJumpXMult 1.25f
#define DuckSuperJumpYMult .5f
#define JumpGraceTime 0.1f
#define JumpSpeed -105.f
#define JumpHBoost 40.f
#define VarJumpTime .2f
#define CeilingVarJumpGrace .05f
#define UpwardCornerCorrection 4
#define WallSpeedRetentionTime .06f
#define WallJumpCheckDist 3
#define WallJumpForceTime .16f
#define WallJumpHSpeed (MaxRun + JumpHBoost)
#define WallSlideStartMax 20.f
#define WallSlideTime 1.2f
#define BounceVarJumpTime .2f
#define BounceSpeed -140.f
#define SuperBounceVarJumpTime .2f
#define SuperBounceSpeed -185.f
#define SuperJumpSpeed JumpSpeed
#define SuperJumpH 260.f
#define SuperWallJumpSpeed -160.f
#define SuperWallJumpVarTime .25f
#define SuperWallJumpForceTime .2f
#define SuperWallJumpH (MaxRun + JumpHBoost * 2)
#define DashSpeed 240.f
#define EndDashSpeed 160.f
#define EndDashUpMult .75f
#define DashTime .15f
#define DashCooldown .2f
#define DashRefillCooldown .1f
#define DashHJumpThruNudge 6
#define DashCornerCorrection 4
#define DashVFloorSnapDist 3
#define DashAttackTime .3f
#define BoostMoveSpeed 80.f
#define BoostTime .25f
#define DuckWindMult 0.f
#define WindWallDistance 3
#define ReboundSpeedX 120.f
#define ReboundSpeedY -120.f
#define ReboundVarJumpTime .15f
#define ReflectBoundSpeed 220.f
#define DreamDashSpeed DashSpeed
#define DreamDashEndWiggle 5
#define DreamDashMinTime .1f
#define ClimbMaxStamina 110.f
#define ClimbUpCost (100 / 2.2f)
#define ClimbStillCost (100 / 10.f)
#define ClimbJumpCost (110 / 4.f)
#define ClimbCheckDist 2
#define ClimbUpCheckDist 2
#define ClimbNoMoveTime .1f
#define ClimbTiredThreshold 20.f
#define ClimbUpSpeed -45.f
#define ClimbDownSpeed 80.f
#define ClimbSlipSpeed 30.f
#define ClimbAccel 900.f
#define ClimbGrabYMult .2f
#define ClimbHopY -120.f
#define ClimbHopX 100.f
#define ClimbHopForceTime .2f
#define ClimbJumpBoostTime .2f
#define ClimbHopNoWindTime .3f
#define LaunchSpeed 280.f
#define LaunchCancelThreshold 220.f
#define LiftYCap -130.f
#define LiftXCap 250.f
#define JumpThruAssistSpeed -40.f
#define ThrowRecoil 80.f
#define WalkSpeed 64.f
#define SpacePhysicsMult .6f
#define SwimYSpeedMult .5f
#define SwimMaxRise -60.f
#define SwimVDeccel 600.f
#define SwimMax 80.f
#define SwimUnderwaterMax 60.f
#define SwimAccel 600.f
#define SwimReduce 400.f
#define SwimDashSpeedMult .75f
#define HitSquashNoMoveTime .1f
#define HitSquashFriction 800.f
#define StarFlyTransformDeccel 1000.f
#define StarFlyTime 2.f
#define StarFlyStartSpeed 250.f
#define StarFlyTargetSpeed 140.f
#define StarFlyMaxSpeed 190.f
#define StarFlyMaxLerpTime 1.f
#define StarFlySlowSpeed (StarFlyTargetSpeed * .65f)
#define StarFlyAccel 1000.f
#define StarFlyRotateSpeed (320 * PI_F / 180)
#define StarFlyEndFlashDuration 0.5f
#define StarFlyEndNoBounceTime .2f
#define StarFlyWallBounce -.5f
#define StarFlyMaxExitY 0.f
#define StarFlyMaxExitX 140.f
#define StarFlyExitUp -100.f
#define WallBoosterSpeed -160.f
#define WallBoosterLiftSpeed -80.f
#define WallBoosterAccel 600.f

#define NormalHairColor 0xAC3232
#define FlyPowerHairColor 0xF2EB6D
#define UsedHairColor 0x44B7FF
#define FlashHairColor 0xFFFFFF
#define TwoDashesHairColor 0xFF6DEF
#define StarFlyColor 0xFFD65C

/* hitboxes: w, h, x, y */
static const float HB_NORMAL[4] = {8, 11, -4, -11}, HB_DUCK[4] = {8, 6, -4, -6};
static const float HB_NORMAL_HURT[4] = {8, 9, -4, -11}, HB_DUCK_HURT[4] = {8, 4, -4, -6};
static const float HB_STARFLY[4] = {8, 8, -4, -10}, HB_STARFLY_HURT[4] = {6, 6, -3, -9};
static const float *hurtbox = HB_NORMAL_HURT;

static Ent *E;          /* the player's entity, during an update */
#define P (&g_player)

static void set_box(const float *b) { ent_box(E, b[0], b[1], b[2], b[3]); }
static bool box_is(const float *b) { return E->cw == b[0] && E->ch == b[1] && E->cx == b[2] && E->cy == b[3]; }
static void on_collide_h(Ent *e, Collision *c);
static void on_collide_v(Ent *e, Collision *c);

/* ---------------------------------------------------------------- small helpers */
static bool solid_at(float dx, float dy) { return collide_solid(E, E->x + dx, E->y + dy); }
static bool jumpthru_outside_at(float dx, float dy) { return collide_first_jumpthru_outside(E, E->x + dx, E->y + dy) != NULL; }
static V2 center(void) { return e_center(E); }
static V2 bottom_center(void) { return v2(e_cxm(E), e_bottom(E)); }
V2 player_center(const Player *p) { return e_center(p->ent); }
static float angle_of(V2 v) { return atan2f(v.y, v.x); }
static uint32_t lerp_color(uint32_t a, uint32_t b, float t) {
  t = clampf(t, 0, 1);
  int r = (int)(((a >> 16) & 255) + (((int)((b >> 16) & 255) - (int)((a >> 16) & 255)) * t));
  int g = (int)(((a >> 8) & 255) + (((int)((b >> 8) & 255) - (int)((a >> 8) & 255)) * t));
  int bl = (int)((a & 255) + (((int)(b & 255) - (int)(a & 255)) * t));
  return (uint32_t)(r << 16 | g << 8 | bl);
}
static uint32_t hair_rgb;

static void play(int anim) { spr_play(&P->spr, anim, false); }
static void play_r(int anim) { spr_play(&P->spr, anim, true); }
static int last_anim(void) { return P->spr.last_goto; }

int player_max_dashes(const Player *p) { return p->inventory_dashes; }
bool player_dash_attacking(const Player *p) { return p->dash_attack_timer > 0 || p->state == ST_REDDASH; }
bool player_ducking(const Player *p) { (void)p; return box_is(HB_DUCK); }
void player_set_ducking(Player *p, bool d) {
  (void)p;
  if (d) set_box(HB_DUCK), hurtbox = HB_DUCK_HURT;
  else set_box(HB_NORMAL), hurtbox = HB_NORMAL_HURT;
}
#define Ducking player_ducking(P)
static void set_ducking(bool d) { player_set_ducking(P, d); }

static bool can_unduck(void) {
  if (!Ducking) return true;
  float w[4] = {E->cw, E->ch, E->cx, E->cy};
  set_box(HB_NORMAL);
  bool ok = !solid_at(0, 0);
  ent_box(E, w[0], w[1], w[2], w[3]);
  return ok;
}
static bool can_unduck_at(float x, float y) {
  float ox = E->x, oy = E->y;
  E->x = x, E->y = y;
  bool r = can_unduck();
  E->x = ox, E->y = oy;
  return r;
}
static bool duck_free_at(float x, float y) {
  float ox = E->x, oy = E->y, w[4] = {E->cw, E->ch, E->cx, E->cy};
  E->x = x, E->y = y;
  set_box(HB_DUCK);
  bool r = !solid_at(0, 0);
  E->x = ox, E->y = oy;
  ent_box(E, w[0], w[1], w[2], w[3]);
  return r;
}

static V2 lift_boost(void) {
  V2 v = actor_lift(E);
  if (fabsf(v.x) > LiftXCap) v.x = LiftXCap * signf(v.x);
  if (v.y > 0) v.y = 0;
  else if (v.y < LiftYCap) v.y = LiftYCap;
  return v;
}

bool player_in_control(const Player *p) {
  switch (p->state) {
    case ST_INTROJUMP: case ST_INTROWALK: case ST_INTROWAKEUP: case ST_INTRORESPAWN: case ST_DUMMY: case ST_FROZEN:
    case ST_BIRDDASHTUTORIAL: return false;
    default: return true;
  }
}

static bool is_tired(void) { return (P->wall_boost_timer > 0 ? P->stamina + ClimbJumpCost : P->stamina) < ClimbTiredThreshold; }
bool player_refill_dash(Player *p) {
  if (p->dashes < player_max_dashes(p)) {
    p->dashes = player_max_dashes(p);
    return true;
  }
  return false;
}
bool player_use_refill(Player *p, bool two) {
  int max = two ? 2 : player_max_dashes(p);
  if (p->dashes < max || p->stamina < ClimbTiredThreshold) {
    p->dashes = max;
    player_refill_stamina(p);
    return true;
  }
  return false;
}
void player_refill_stamina(Player *p) { p->stamina = ClimbMaxStamina; }

static bool climb_bounds_check(int dir) {
  Room *rm = g_level.room;
  return e_left(E) + dir * ClimbCheckDist >= rm->x && e_right(E) + dir * ClimbCheckDist < rm->x + rm->w;
}
static bool climb_blocker(float x, float y) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if ((o->kind & KIND_CLIMBBLOCKER) && o->cls && o->collidable && o->dead != 1 && collide_ent_at(E, x, y, o)) return true;
  }
  return false;
}
static bool climb_check(int dir, int yadd) {
  return climb_bounds_check(dir) && !climb_blocker(E->x + ClimbCheckDist * P->facing, E->y + yadd) &&
         solid_at(dir * ClimbCheckDist, yadd);
}
static bool wall_jump_check(int dir) { return climb_bounds_check(dir) && solid_at(dir * WallJumpCheckDist, 0); }
static bool water_at(float dx, float dy) { return collide_first_kind(E, E->x + dx, E->y + dy, KIND_WATER) != NULL; }
static bool swim_check(void) { return water_at(0, -8) && water_at(0, 0); }
static bool swim_underwater_check(void) { return water_at(0, -9); }
static bool swim_jump_check(void) { return !water_at(0, -14); }
static bool swim_rise_check(void) { return !water_at(0, -18); }

static V2 aim_vector(void) {
  int x = g_in.aim_x, y = g_in.aim_y;
  if (!x && !y) return v2((float)P->facing, 0);
  if (x && y) return v2(x * 0.70710677f, y * 0.70710677f);
  return v2((float)x, (float)y);
}

/* ---------------------------------------------------------------- states */
static int normal_update(void), climb_update(void), dash_update(void), swim_update(void), boost_update(void),
    red_dash_update(void), hit_squash_update(void), launch_update(void), dream_dash_update(void),
    summit_launch_update(void), dummy_update(void), star_fly_update(void), attract_update(void), frozen_update(void),
    temple_fall_update(void), reflection_fall_update(void), bird_update(void), cassette_update(void);
static void bird_begin(void);
static void normal_begin(void), normal_end(void), climb_begin(void), climb_end(void), dash_begin(void), dash_end(void),
    swim_begin(void), boost_begin(void), boost_end(void), red_dash_begin(void), red_dash_end(void),
    hit_squash_begin(void), launch_begin(void), dream_dash_begin(void), dream_dash_end(void),
    summit_launch_begin(void), dummy_begin(void), intro_respawn_begin(void), intro_respawn_end(void),
    star_fly_begin(void), star_fly_end(void), attract_begin(void), cassette_fly_begin(void), reflection_fall_begin(void),
    reflection_fall_end(void);
static float co_dash(void), co_boost(void), co_red_dash(void), co_pickup(void), co_intro_walk(void),
    co_intro_jump(void), co_intro_wakeup(void), co_star_fly(void), co_temple_fall(void), co_bird(void),
    co_cassette_fly(void), co_reflection_fall(void);

typedef int (*UpdFn)(void);
typedef void (*VFn)(void);
typedef float (*CoFn)(void);
static const UpdFn UPD[ST_COUNT] = {
    normal_update, climb_update, dash_update, swim_update, boost_update, red_dash_update, hit_squash_update,
    launch_update, NULL, dream_dash_update, summit_launch_update, dummy_update, NULL, NULL, NULL, NULL, bird_update,
    frozen_update, reflection_fall_update, star_fly_update, temple_fall_update, cassette_update, attract_update};
static const VFn BEGIN[ST_COUNT] = {
    normal_begin, climb_begin, dash_begin, swim_begin, boost_begin, red_dash_begin, hit_squash_begin, launch_begin,
    NULL, dream_dash_begin, summit_launch_begin, dummy_begin, NULL, NULL, intro_respawn_begin, NULL, bird_begin,
    NULL, reflection_fall_begin, star_fly_begin, NULL, cassette_fly_begin, attract_begin};
static const VFn END[ST_COUNT] = {
    normal_end, climb_end, dash_end, NULL, boost_end, red_dash_end, NULL, NULL, NULL, dream_dash_end, NULL, NULL,
    NULL, NULL, intro_respawn_end, NULL, NULL, NULL, reflection_fall_end, star_fly_end, NULL, NULL, NULL};
static const CoFn CO[ST_COUNT] = {
    NULL, NULL, co_dash, NULL, co_boost, co_red_dash, NULL, NULL, co_pickup, NULL, NULL, NULL, co_intro_walk,
    co_intro_jump, NULL, co_intro_wakeup, co_bird, NULL, co_reflection_fall, co_star_fly, co_temple_fall, co_cassette_fly,
    NULL};

void player_set_state(Player *p, int st) {
  if (p->state_locked || p->state == st) return;
  p->prev_state = p->state;
  p->state = st;
  if (p->prev_state >= 0 && END[p->prev_state]) END[p->prev_state]();
  if (BEGIN[st]) BEGIN[st]();
  if (CO[st]) {
    p->co_active = true;
    p->co_step = 0;
    p->co_wait = 0;
    p->co_t = 0;
  } else
    p->co_active = false;
}
#define SET(st) player_set_state(P, st)

static void co_update(void) {
  if (!P->co_active) return;
  if (P->co_wait > 0) {
    P->co_wait -= DT;
    return;
  }
  int st = P->state;
  float r = CO[st]();
  if (P->state != st || !P->co_active) return;   /* replaced or cancelled */
  if (r < 0) P->co_active = false;
  else P->co_wait = r;
}

/* ---------------------------------------------------------------- jumps */
void player_jump(Player *p, bool particles, bool sfx) {
  (void)sfx;
  btn_consume_buffer(&g_in.jump);
  p->jump_grace_timer = 0;
  p->var_jump_timer = VarJumpTime;
  p->auto_jump = false;
  p->dash_attack_timer = 0;
  p->wall_slide_timer = WallSlideTime;
  p->wall_boost_timer = 0;
  p->speed.x += JumpHBoost * p->move_x;
  p->speed.y = JumpSpeed;
  p->speed = v2add(p->speed, lift_boost());
  p->var_jump_speed = p->speed.y;
  V2 lb = lift_boost();
  p->launched = (lb.x * lb.x + lb.y * lb.y >= 100 * 100 && p->speed.x * p->speed.x + p->speed.y * p->speed.y >= 220 * 220);
  p->spr.sx = .6f, p->spr.sy = 1.4f;
  if (particles) dust_burst(bottom_center(), -PI_F / 2, 4);
}
static void jump(void) { player_jump(P, true, true); }

static void super_jump(void) {
  btn_consume_buffer(&g_in.jump);
  P->jump_grace_timer = 0;
  P->var_jump_timer = VarJumpTime;
  P->auto_jump = false;
  P->dash_attack_timer = 0;
  P->wall_slide_timer = WallSlideTime;
  P->wall_boost_timer = 0;
  P->speed.x = SuperJumpH * P->facing;
  P->speed.y = JumpSpeed;
  P->speed = v2add(P->speed, lift_boost());
  if (Ducking) {
    set_ducking(false);
    P->speed.x *= DuckSuperJumpXMult;
    P->speed.y *= DuckSuperJumpYMult;
  }
  P->var_jump_speed = P->speed.y;
  P->launched = true;
  P->spr.sx = .6f, P->spr.sy = 1.4f;
  dust_burst(bottom_center(), -PI_F / 2, 4);
}

static void wall_jump(int dir) {
  set_ducking(false);
  btn_consume_buffer(&g_in.jump);
  P->jump_grace_timer = 0;
  P->var_jump_timer = VarJumpTime;
  P->auto_jump = false;
  P->dash_attack_timer = 0;
  P->wall_slide_timer = WallSlideTime;
  P->wall_boost_timer = 0;
  if (P->move_x != 0) {
    P->force_move_x = dir;
    P->force_move_x_timer = WallJumpForceTime;
  }
  if (actor_lift(E).x == 0 && actor_lift(E).y == 0) {
    Ent *wall = collide_first_solid(E, E->x + WallJumpCheckDist, E->y);
    if (wall) actor_set_lift(E, wall->lift);
  }
  P->speed.x = WallJumpHSpeed * dir;
  P->speed.y = JumpSpeed;
  P->speed = v2add(P->speed, lift_boost());
  P->var_jump_speed = P->speed.y;
  V2 lb = lift_boost();
  P->launched = (lb.x * lb.x + lb.y * lb.y >= 100 * 100 && P->speed.x * P->speed.x + P->speed.y * P->speed.y >= 220 * 220);
  P->spr.sx = .6f, P->spr.sy = 1.4f;
  if (dir == -1) dust_burst(v2add(center(), v2(2, 0)), -3 * PI_F / 4, 4);
  else dust_burst(v2add(center(), v2(-2, 0)), -PI_F / 4, 4);
}

static void super_wall_jump(int dir) {
  set_ducking(false);
  btn_consume_buffer(&g_in.jump);
  P->jump_grace_timer = 0;
  P->var_jump_timer = SuperWallJumpVarTime;
  P->auto_jump = false;
  P->dash_attack_timer = 0;
  P->wall_slide_timer = WallSlideTime;
  P->wall_boost_timer = 0;
  P->speed.x = SuperWallJumpH * dir;
  P->speed.y = SuperWallJumpSpeed;
  P->speed = v2add(P->speed, lift_boost());
  P->var_jump_speed = P->speed.y;
  P->launched = true;
  P->spr.sx = .6f, P->spr.sy = 1.4f;
  if (dir == -1) dust_burst(v2add(center(), v2(2, 0)), -3 * PI_F / 4, 4);
  else dust_burst(v2add(center(), v2(-2, 0)), -PI_F / 4, 4);
}

static void climb_jump(void) {
  if (!P->on_ground) {
    P->stamina -= ClimbJumpCost;
    spr_play(&P->sweat, A_player_sweat_jump, true);
  }
  P->dream_jump = false;
  player_jump(P, false, false);
  if (P->move_x == 0) {
    P->wall_boost_dir = -P->facing;
    P->wall_boost_timer = ClimbJumpBoostTime;
  }
  if (P->facing == 1) dust_burst(v2add(center(), v2(2, 0)), -3 * PI_F / 4, 4);
  else dust_burst(v2add(center(), v2(-2, 0)), -PI_F / 4, 4);
}

void player_bounce(Player *p, float from_y) {
  float w[4] = {E->cw, E->ch, E->cx, E->cy};
  set_box(HB_NORMAL);
  actor_move_v_exact(E, (int)(from_y - e_bottom(E)), NULL, NULL);
  if (!p->inventory_norefills) player_refill_dash(p);
  player_refill_stamina(p);
  player_set_state(p, ST_NORMAL);
  p->jump_grace_timer = 0;
  p->var_jump_timer = BounceVarJumpTime;
  p->auto_jump = true;
  p->auto_jump_timer = BounceAutoJumpTime;
  p->dash_attack_timer = 0;
  p->wall_slide_timer = WallSlideTime;
  p->wall_boost_timer = 0;
  p->var_jump_speed = p->speed.y = BounceSpeed;
  p->launched = false;
  p->spr.sx = .6f, p->spr.sy = 1.4f;
  ent_box(E, w[0], w[1], w[2], w[3]);
}

void player_super_bounce(Player *p, float from_y) {
  float w[4] = {E->cw, E->ch, E->cx, E->cy};
  set_box(HB_NORMAL);
  actor_move_v(E, from_y - e_bottom(E), NULL, NULL);
  if (!p->inventory_norefills) player_refill_dash(p);
  player_refill_stamina(p);
  player_set_state(p, ST_NORMAL);
  p->jump_grace_timer = 0;
  p->var_jump_timer = SuperBounceVarJumpTime;
  p->auto_jump = true;
  p->auto_jump_timer = 0;
  p->dash_attack_timer = 0;
  p->wall_slide_timer = WallSlideTime;
  p->wall_boost_timer = 0;
  p->speed.x = 0;
  p->var_jump_speed = p->speed.y = SuperBounceSpeed;
  p->launched = false;
  level_dir_shake(v2(0, -1), .1f);
  p->spr.sx = .5f, p->spr.sy = 1.5f;
  ent_box(E, w[0], w[1], w[2], w[3]);
}

void player_side_bounce(Player *p, int dir, float from_x, float from_y) {
  float w[4] = {E->cw, E->ch, E->cx, E->cy};
  set_box(HB_NORMAL);
  actor_move_v(E, clampf(from_y - e_bottom(E), -4, 4), NULL, NULL);
  if (dir > 0) actor_move_h(E, from_x - e_left(E), NULL, NULL);
  else if (dir < 0) actor_move_h(E, from_x - e_right(E), NULL, NULL);
  if (!p->inventory_norefills) player_refill_dash(p);
  player_refill_stamina(p);
  player_set_state(p, ST_NORMAL);
  p->jump_grace_timer = 0;
  p->var_jump_timer = BounceVarJumpTime;
  p->auto_jump = true;
  p->auto_jump_timer = 0;
  p->dash_attack_timer = 0;
  p->wall_slide_timer = WallSlideTime;
  p->force_move_x = dir;
  p->force_move_x_timer = .3f;
  p->wall_boost_timer = 0;
  p->launched = false;
  p->speed.x = 240 * dir;
  p->var_jump_speed = p->speed.y = BounceSpeed;
  level_dir_shake(v2((float)dir, 0), .1f);
  p->spr.sx = 1.5f, p->spr.sy = .5f;
  ent_box(E, w[0], w[1], w[2], w[3]);
}

void player_rebound(Player *p, int dir) {
  p->speed.x = dir * ReboundSpeedX;
  p->speed.y = ReboundSpeedY;
  p->var_jump_speed = p->speed.y;
  p->var_jump_timer = ReboundVarJumpTime;
  p->auto_jump = true;
  p->auto_jump_timer = 0;
  p->dash_attack_timer = 0;
  p->wall_slide_timer = WallSlideTime;
  p->wall_boost_timer = 0;
  p->launched = false;
  p->force_move_x_timer = 0;
  player_set_state(p, ST_NORMAL);
}

static void reflect_bounce(V2 dir) {
  if (dir.x != 0) P->speed.x = dir.x * ReflectBoundSpeed;
  if (dir.y != 0) P->speed.y = dir.y * ReflectBoundSpeed;
  P->auto_jump_timer = 0;
  P->dash_attack_timer = 0;
  P->wall_slide_timer = WallSlideTime;
  P->wall_boost_timer = 0;
  P->launched = false;
  P->force_move_x_timer = 0;
  SET(ST_NORMAL);
}

void player_point_bounce(Player *p, V2 from) {
  if (p->state == ST_BOOST && p->current_booster) p->current_booster = NULL;
  player_refill_dash(p);
  player_refill_stamina(p);
  V2 d = v2sub(center(), from);
  float l = v2len(d);
  p->speed = l > 0 ? v2mul(d, 200 / l) : v2(0, 0);
  p->speed.x *= 1.2f;
  if (fabsf(p->speed.x) < 120) {
    if (p->speed.x == 0) p->speed.x = -p->facing * 120.f;
    else p->speed.x = signf(p->speed.x) * 120;
  }
}

bool player_bounce_check(const Player *p, float y) { return e_bottom(p->ent) <= y + 3; }

/* ---------------------------------------------------------------- death */
void player_die_ex(Player *p, V2 dir, bool even, bool stats) {
  (void)even;
  if (p->dead || p->state == ST_REFLECTIONFALL) return;
  if (stats) {
    g_session.deaths++;
    g_session.deaths_in_current_level++;
    save_add_death();
  }
  int golden = berry_golden_room();   /* holding the golden: back to where it was found */
  p->dead = true;
  leader_lose_all();
  p->speed = v2(0, 0);
  p->state_locked = true;
  E = p->ent;
  if (p->holding) holdable_release(p->holding, v2(0, 0)), p->holding = NULL;
  E->collidable = 0;
  E->depth = D_TOP;
  ents_mark_unsorted();
  level_shake(.3f);
  /* PlayerDeadBody */
  p->body = true;
  p->body_bounce = dir;
  p->body_t = 0;
  p->body_step = 0;
  p->body_scale = 1;
  p->death_effect = -1;
  p->death_color = (uint16_t)rgb(hair_rgb);
  p->death_finished = false;
  p->golden_room = golden;
  p->co_active = false;
  btn_consume_buffer(&g_in.dash);
  btn_consume_buffer(&g_in.jump);
}

static void death_wiped(void) { g_level.dead_reload = true; }

/* PlayerDeadBody.DeathRoutine, a frame at a time */
static void body_update(void) {
  Player *p = P;
  if (p->body_step == 0) {
    if (p->body_bounce.x != 0 || p->body_bounce.y != 0) {
      p->body_scale = 1.5f;
      level_freeze(0.05f);
      p->body_step = 1;
      p->respawn_from = v2(E->x, E->y);
      return;
    }
    p->body_step = 3;
  }
  if (p->body_step == 1) {   /* the bounce tween, 0.5 s, stopped at 75% */
    p->body_t += DT;
    float t = fminf(p->body_t / 0.5f, 1);
    float eased = ease_cube_out(t);
    E->x = p->respawn_from.x + p->body_bounce.x * 24 * eased;
    E->y = p->respawn_from.y + p->body_bounce.y * 24 * eased;
    p->body_scale = 1.5f - eased * 0.5f;
    p->spr.rot = floorf(eased * 4) * 6.2831855f;
    if (p->body_t >= 0.5f * 0.75f) p->body_step = 3;
    return;
  }
  if (p->body_step == 3) {
    E->y -= 5;
    level_shake(.3f);
    p->death_effect = 0;
    p->body_step = 4;
    p->body_t = 0;
    return;
  }
  if (p->body_step == 4) {
    p->death_effect += DT / 0.834f;
    p->body_t += DT;
    if (p->body_t >= 0.834f * 0.65f) {   /* End(): the area's wipe, then the reload (or the golden's restart) */
      p->body_step = 5;
      wipe_area(false, death_wiped);
    }
    return;
  }
  if (p->body_step == 5) p->death_effect += DT / 0.834f;
}

/* DeathEffect.Draw: a gfx_custom layer (its 40 scaled discs, as affine commands, took 40 of the frame's 48: with anything
 * else turning or scaled, the colors drawn over their black outlines were left out) */
static struct { V2 pos; float ease; uint16_t color; } death_fx;
static void death_effect_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  V2 pos = death_fx.pos;
  float ease = death_fx.ease;
  uint16_t c2 = ((int)floorf(ease * 10) % 2 == 0) ? death_fx.color : 0xFFFF;
  float s = ease < 0.5f ? 0.5f + ease : ease_cube_out(1 - (ease - 0.5f) * 2);
  for (int pass = 0; pass < 2; pass++)
    for (int i = 0; i < 8; i++) {
      V2 v = angle_vec((i / 8.f + ease * 0.25f) * 2 * PI_F, ease_cube_out(ease) * 24);
      V2 at = v2add(pos, v);
      if (!pass) {
        blit_tex_ex(strip, sy0, sy1, T_characters_player_hair00, at.x - 1, at.y, 5, 5, s, s, 0, 0, 255, GF_SILHOUETTE);
        blit_tex_ex(strip, sy0, sy1, T_characters_player_hair00, at.x + 1, at.y, 5, 5, s, s, 0, 0, 255, GF_SILHOUETTE);
        blit_tex_ex(strip, sy0, sy1, T_characters_player_hair00, at.x, at.y - 1, 5, 5, s, s, 0, 0, 255, GF_SILHOUETTE);
        blit_tex_ex(strip, sy0, sy1, T_characters_player_hair00, at.x, at.y + 1, 5, 5, s, s, 0, 0, 255, GF_SILHOUETTE);
      } else
        blit_tex_ex(strip, sy0, sy1, T_characters_player_hair00, at.x, at.y, 5, 5, s, s, 0, c2, 255, GF_SILHOUETTE);
    }
}
static void death_effect_draw(V2 pos, uint16_t color, float ease) {
  Tex t;
  tex_get(T_characters_player_hair00, &t);   /* (loaded now) */
  death_fx.pos = pos, death_fx.ease = ease, death_fx.color = color;
  gfx_custom(death_effect_strip, NULL, (int)floorf(pos.y) - 32 - g_camy, (int)ceilf(pos.y) + 33 - g_camy);
}

/* ---------------------------------------------------------------- collisions */
static bool dream_dash_check(V2 dir);

static void on_collide_h(Ent *e, Collision *c) {
  (void)e;
  if (P->state == ST_STARFLY) {
    if (P->star_fly_timer < StarFlyEndNoBounceTime) P->speed.x = 0;
    else P->speed.x *= StarFlyWallBounce;
    return;
  }
  if (P->state == ST_DREAMDASH) return;
  if (player_dash_attacking(P) && c->hit && MORE(c->hit->cls)->on_dash_collide && c->dir.x == signf(P->dash_dir.x)) {
    int r = MORE(c->hit->cls)->on_dash_collide(c->hit, P, c->dir);
    if (P->state == ST_REDDASH) r = DASH_IGNORE;
    if (r == DASH_REBOUND) { player_rebound(P, -(int)signf(P->speed.x)); return; }
    if (r == DASH_BOUNCE) { reflect_bounce(v2(-signf(P->speed.x), 0)); return; }
    if (r == DASH_IGNORE) return;
  }
  if (P->state == ST_DASH || P->state == ST_REDDASH) {
    if (P->on_ground && duck_free_at(E->x + signf(P->speed.x), E->y)) {
      set_ducking(true);
      return;
    } else if (P->speed.y == 0 && P->speed.x != 0) {
      for (int i = 1; i <= DashCornerCorrection; i++)
        for (int j = 1; j >= -1; j -= 2)
          if (!solid_at(signf(P->speed.x), (float)(i * j))) {
            actor_move_v_exact(E, i * j, NULL, NULL);
            actor_move_h_exact(E, (int)signf(P->speed.x), NULL, NULL);
            return;
          }
    }
  }
  if (dream_dash_check(v2(signf(P->speed.x), 0))) {
    SET(ST_DREAMDASH);
    P->dash_attack_timer = 0;
    return;
  }
  if (P->wall_speed_retention_timer <= 0) {
    P->wall_speed_retained = P->speed.x;
    P->wall_speed_retention_timer = WallSpeedRetentionTime;
  }
  if (c->hit && MORE(c->hit->cls)->on_collide) MORE(c->hit->cls)->on_collide(c->hit, c->dir);
  P->speed.x = 0;
  P->dash_attack_timer = 0;
  if (P->state == ST_REDDASH) SET(ST_HITSQUASH);
}

static void on_collide_v(Ent *e, Collision *c) {
  (void)e;
  if (P->state == ST_STARFLY) {
    if (P->star_fly_timer < StarFlyEndNoBounceTime) P->speed.y = 0;
    else P->speed.y *= StarFlyWallBounce;
    return;
  }
  if (P->state == ST_SWIM) {
    P->speed.y = 0;
    return;
  }
  if (P->state == ST_DREAMDASH) return;
  if (c->hit && MORE(c->hit->cls)->on_dash_collide) {
    if (player_dash_attacking(P) && c->dir.y == signf(P->dash_dir.y)) {
      int r = MORE(c->hit->cls)->on_dash_collide(c->hit, P, c->dir);
      if (P->state == ST_REDDASH) r = DASH_IGNORE;
      if (r == DASH_REBOUND) { player_rebound(P, 0); return; }
      if (r == DASH_BOUNCE) { reflect_bounce(v2(0, -signf(P->speed.y))); return; }
      if (r == DASH_IGNORE) return;
    } else if (P->state == ST_SUMMITLAUNCH) {
      MORE(c->hit->cls)->on_dash_collide(c->hit, P, c->dir);
      return;
    }
  }
  if (P->speed.y > 0) {
    if ((P->state == ST_DASH || P->state == ST_REDDASH) && !P->dash_started_on_ground) {
      if (P->speed.x <= 0)
        for (int i = -1; i >= -DashCornerCorrection; i--)
          if (!actor_on_ground_at(E, E->x + i, E->y, 1)) {
            actor_move_h_exact(E, i, NULL, NULL);
            actor_move_v_exact(E, 1, NULL, NULL);
            return;
          }
      if (P->speed.x >= 0)
        for (int i = 1; i <= DashCornerCorrection; i++)
          if (!actor_on_ground_at(E, E->x + i, E->y, 1)) {
            actor_move_h_exact(E, i, NULL, NULL);
            actor_move_v_exact(E, 1, NULL, NULL);
            return;
          }
    }
    if (dream_dash_check(v2(0, signf(P->speed.y)))) {
      SET(ST_DREAMDASH);
      P->dash_attack_timer = 0;
      return;
    }
    if (P->dash_dir.x != 0 && P->dash_dir.y > 0 && P->speed.y > 0) {
      P->dash_dir.x = signf(P->dash_dir.x);
      P->dash_dir.y = 0;
      P->speed.y = 0;
      P->speed.x *= DodgeSlideSpeedMult;
      set_ducking(true);
    }
    if (P->state != ST_CLIMB) {
      float squish = fminf(P->speed.y / FastMaxFall, 1);
      P->spr.sx = lerpf(1, 1.6f, squish);
      P->spr.sy = lerpf(1, .4f, squish);
      if (P->speed.y >= MaxFall / 2) dust_burst(v2(E->x, E->y), -PI_F / 2, 8);
      if (P->highest_air_y < E->y - 50 && P->speed.y >= MaxFall && fabsf(P->speed.x) >= MaxRun) play(A_player_runStumble);
      P->play_footstep_on_land = 0;
    }
  } else {
    if (P->speed.y < 0) {
      if (P->speed.x <= 0)
        for (int i = 1; i <= UpwardCornerCorrection; i++)
          if (!solid_at((float)-i, -1)) {
            E->x -= i, E->y -= 1;
            return;
          }
      if (P->speed.x >= 0)
        for (int i = 1; i <= UpwardCornerCorrection; i++)
          if (!solid_at((float)i, -1)) {
            E->x += i, E->y -= 1;
            return;
          }
      if (P->var_jump_timer < VarJumpTime - CeilingVarJumpGrace) P->var_jump_timer = 0;
    }
    if (dream_dash_check(v2(0, signf(P->speed.y)))) {
      SET(ST_DREAMDASH);
      P->dash_attack_timer = 0;
      return;
    }
  }
  if (c->hit && MORE(c->hit->cls)->on_collide) MORE(c->hit->cls)->on_collide(c->hit, c->dir);
  P->dash_attack_timer = 0;
  P->speed.y = 0;
  if (P->state == ST_REDDASH) SET(ST_HITSQUASH);
}

static bool solid_or_dream_at(float x, float y) {
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if ((o->kind & KIND_SOLID) && !(o->kind & KIND_DREAMBLOCK) && o->cls && o != E && o->collidable && o->dead != 1 &&
        collide_ent_at(E, x, y, o))
      return true;
  }
  return false;
}

static bool dream_dash_check(V2 dir) {
  if (!(P->inventory_dreamdash && player_dash_attacking(P) && (dir.x == signf(P->dash_dir.x) || dir.y == signf(P->dash_dir.y))))
    return false;
  Ent *block = collide_first_kind(E, E->x + dir.x, E->y + dir.y, KIND_DREAMBLOCK);
  if (!block) return false;
  if (solid_or_dream_at(E->x + dir.x, E->y + dir.y)) {
    V2 side = v2(fabsf(dir.y), fabsf(dir.x));
    bool neg, pos;
    if (dir.x != 0) neg = P->speed.y <= 0, pos = P->speed.y >= 0;
    else neg = P->speed.x <= 0, pos = P->speed.x >= 0;
    if (neg)
      for (int i = -1; i >= -DashCornerCorrection; i--)
        if (!solid_or_dream_at(E->x + dir.x + side.x * i, E->y + dir.y + side.y * i)) {
          E->x += side.x * i, E->y += side.y * i;
          P->dream_block = block;
          return true;
        }
    if (pos)
      for (int i = 1; i <= DashCornerCorrection; i++)
        if (!solid_or_dream_at(E->x + dir.x + side.x * i, E->y + dir.y + side.y * i)) {
          E->x += side.x * i, E->y += side.y * i;
          P->dream_block = block;
          return true;
        }
    return false;
  }
  P->dream_block = block;
  return true;
}

void player_on_bounds_h(Player *p) {
  p->speed.x = 0;
  if (p->state == ST_REDDASH) player_set_state(p, ST_NORMAL);
}
void player_on_bounds_v(Player *p) {
  p->speed.y = 0;
  if (p->state == ST_REDDASH) player_set_state(p, ST_NORMAL);
}

static void on_squish(Ent *e, Collision *c) {
  E = e;
  bool ducked = false;
  if (!Ducking) {
    ducked = true;
    set_ducking(true);
    if (c->pusher) c->pusher->collidable = 1;
    if (!solid_at(0, 0)) {
      if (c->pusher) c->pusher->collidable = 0;
      return;
    }
    float wx = E->x, wy = E->y;
    E->x = c->target.x, E->y = c->target.y;
    if (!solid_at(0, 0)) {
      if (c->pusher) c->pusher->collidable = 0;
      return;
    }
    E->x = wx, E->y = wy;
    if (c->pusher) c->pusher->collidable = 0;
  }
  if (!actor_try_squish_wiggle(E, c)) player_die(P, v2(0, 0), false);
  else if (ducked && can_unduck()) set_ducking(false);
}

static bool is_riding_solid(Ent *e, Ent *s) {
  if (P->state == ST_DREAMDASH) return collide_ent_at(e, e->x, e->y, s);
  if (P->state == ST_CLIMB || P->state == ST_HITSQUASH) return collide_ent_at(e, e->x + P->facing, e->y, s);
  return collide_ent_at(e, e->x, e->y + 1, s);
}
static bool is_riding_jumpthru(Ent *e, Ent *j) {
  if (P->state == ST_DREAMDASH) return false;
  return P->state != ST_CLIMB && P->speed.y >= 0 && !e->ignore_jumpthrus && collide_ent_at(e, e->x, e->y + 1, j) &&
         !collide_ent_at(e, e->x, e->y, j);
}

/* ---------------------------------------------------------------- normal */
static void normal_begin(void) { P->max_fall = MaxFall; }
static void normal_end(void) {
  P->wall_boost_timer = 0;
  P->wall_speed_retention_timer = 0;
  P->hop_wait_x = 0;
}

/* CanDash: not when Madeline stands where she can talk and the talk key is pressed */
static bool can_dash(void) {
  return btn_pressed(&g_in.dash) && P->dash_cooldown_timer <= 0 && P->dashes > 0 && (!g_talk_over || !btn_pressed(&g_in.talk));
}
static int start_dash(void) {
  P->was_dash_b = P->dashes == 2;
  P->dashes = P->dashes - 1 < 0 ? 0 : P->dashes - 1;
  btn_consume_buffer(&g_in.dash);
  return ST_DASH;
}

static void wall_slide_particles(int dir) {
  if (level_on_interval(.01f)) {
    V2 at = center();
    at = v2add(at, v2(dir == 1 ? 5.f : -5.f, 4));
    dust_burst(at, -PI_F / 2, 1);
  }
}

static Ent *holdable_check(void);

static int normal_update(void) {
  if (lift_boost().y < 0 && P->was_on_ground && !P->on_ground && P->speed.y >= 0) P->speed.y = lift_boost().y;
  if (!P->holding) {
    if (g_in.grab.check && !is_tired() && !Ducking) {
      Ent *h = holdable_check();
      if (h) {
        set_ducking(false);
        P->holding = h;
        P->min_hold_timer = HoldMinTime;
        return ST_PICKUP;
      }
      if (P->speed.y >= 0 && signf(P->speed.x) != -P->facing) {
        if (climb_check(P->facing, 0)) {
          set_ducking(false);
          return ST_CLIMB;
        }
        if (g_in.move_y < 1 && g_level.wind.y <= 0)
          for (int i = 1; i <= ClimbUpCheckDist; i++)
            if (!solid_at(0, (float)-i) && climb_check(P->facing, -i)) {
              actor_move_v_exact(E, -i, NULL, NULL);
              set_ducking(false);
              return ST_CLIMB;
            }
      }
    }
    if (can_dash()) {
      P->speed = v2add(P->speed, lift_boost());
      return start_dash();
    }
    if (Ducking) {
      if (P->on_ground && g_in.move_y != 1) {
        if (can_unduck()) {
          set_ducking(false);
          P->spr.sx = .8f, P->spr.sy = 1.2f;
        } else if (P->speed.x == 0) {
          for (int i = DuckCorrectCheck; i > 0; i--) {
            if (can_unduck_at(E->x + i, E->y)) {
              actor_move_h(E, DuckCorrectSlide * DT, on_collide_h, NULL);
              break;
            } else if (can_unduck_at(E->x - i, E->y)) {
              actor_move_h(E, -DuckCorrectSlide * DT, on_collide_h, NULL);
              break;
            }
          }
        }
      }
    } else if (P->on_ground && g_in.move_y == 1 && P->speed.y >= 0) {
      set_ducking(true);
      P->spr.sx = 1.4f, P->spr.sy = .6f;
    }
  } else {
    if (!g_in.grab.check && P->min_hold_timer <= 0) {   /* Throw */
      if (g_in.move_y == 1) holdable_release(P->holding, v2(0, 0));
      else {
        holdable_release(P->holding, v2((float)P->facing, 0));
        P->speed.x += ThrowRecoil * -P->facing;
      }
      play(A_player_throw);
      P->holding = NULL;
    }
    if (!Ducking && P->on_ground && g_in.move_y == 1 && P->speed.y >= 0) {   /* Drop */
      holdable_release(P->holding, v2(0, 0));
      P->holding = NULL;
      set_ducking(true);
      P->spr.sx = 1.4f, P->spr.sy = .6f;
    }
  }
  if (Ducking && P->on_ground) P->speed.x = approach(P->speed.x, 0, DuckFriction * DT);
  else {
    float mult = P->on_ground ? 1 : AirMult;
    if (P->on_ground && g_level.core_mode == 2) mult *= .3f;
    float max = P->holding ? HoldingMaxRun : MaxRun;
    if (g_level.in_space) max *= SpacePhysicsMult;
    if (fabsf(P->speed.x) > max && signf(P->speed.x) == P->move_x)
      P->speed.x = approach(P->speed.x, max * P->move_x, RunReduce * mult * DT);
    else
      P->speed.x = approach(P->speed.x, max * P->move_x, RunAccel * mult * DT);
  }
  {
    float mf = MaxFall, fmf = FastMaxFall;
    if (g_level.in_space) mf *= SpacePhysicsMult, fmf *= SpacePhysicsMult;
    if (g_in.move_y == 1 && P->speed.y >= mf) {
      P->max_fall = approach(P->max_fall, fmf, FastMaxAccel * DT);
      float half = mf + (fmf - mf) * .5f;
      if (P->speed.y >= half) {
        float l = fminf(1, (P->speed.y - half) / (fmf - half));
        P->spr.sx = lerpf(1, .5f, l);
        P->spr.sy = lerpf(1, 1.5f, l);
      }
    } else
      P->max_fall = approach(P->max_fall, mf, FastMaxAccel * DT);
  }
  if (!P->on_ground) {
    float max = P->max_fall;
    if ((P->move_x == P->facing || (P->move_x == 0 && g_in.grab.check)) && g_in.move_y != 1) {
      if (P->speed.y >= 0 && P->wall_slide_timer > 0 && !P->holding && climb_bounds_check(P->facing) &&
          solid_at((float)P->facing, 0) && can_unduck()) {
        set_ducking(false);
        P->wall_slide_dir = P->facing;
      }
      if (P->wall_slide_dir != 0) {
        if (P->wall_slide_timer > WallSlideTime * .5f && climb_blocker(E->x + P->wall_slide_dir, E->y))
          P->wall_slide_timer = WallSlideTime * .5f;
        max = lerpf(MaxFall, WallSlideStartMax, P->wall_slide_timer / WallSlideTime);
        if (P->wall_slide_timer / WallSlideTime > .65f) wall_slide_particles(P->wall_slide_dir);
      }
    }
    float mult = (fabsf(P->speed.y) < HalfGravThreshold && (g_in.jump.check || P->auto_jump)) ? .5f : 1.f;
    if (g_level.in_space) mult *= SpacePhysicsMult;
    P->speed.y = approach(P->speed.y, max, Gravity * mult * DT);
  }
  if (P->var_jump_timer > 0) {
    if (P->auto_jump || g_in.jump.check) P->speed.y = fminf(P->speed.y, P->var_jump_speed);
    else P->var_jump_timer = 0;
  }
  if (btn_pressed(&g_in.jump)) {
    if (P->jump_grace_timer > 0) jump();
    else if (can_unduck()) {
      if (wall_jump_check(1)) {
        if (P->facing == 1 && g_in.grab.check && P->stamina > 0 && !P->holding && !climb_blocker(E->x + WallJumpCheckDist, E->y))
          climb_jump();
        else if (player_dash_attacking(P) && P->dash_dir.x == 0 && P->dash_dir.y == -1)
          super_wall_jump(-1);
        else
          wall_jump(-1);
      } else if (wall_jump_check(-1)) {
        if (P->facing == -1 && g_in.grab.check && P->stamina > 0 && !P->holding && !climb_blocker(E->x - WallJumpCheckDist, E->y))
          climb_jump();
        else if (player_dash_attacking(P) && P->dash_dir.x == 0 && P->dash_dir.y == -1)
          super_wall_jump(1);
        else
          wall_jump(1);
      } else if (water_at(0, 2))
        jump();
    }
  }
  return ST_NORMAL;
}

/* ---------------------------------------------------------------- climb */
static void climb_begin(void) {
  P->auto_jump = false;
  P->speed.x = 0;
  P->speed.y *= ClimbGrabYMult;
  P->wall_slide_timer = WallSlideTime;
  P->climb_no_move_timer = ClimbNoMoveTime;
  P->wall_boost_timer = 0;
  P->last_climb_move = 0;
  for (int i = 0; i < ClimbCheckDist; i++)
    if (!solid_at((float)P->facing, 0)) E->x += P->facing;
    else break;
}
static void climb_end(void) {
  P->wall_speed_retention_timer = 0;
  if (!spr_is(&P->sweat, A_player_sweat_jump)) spr_play(&P->sweat, A_player_sweat_idle, false);
}

static void climb_hop(void) {
  P->climb_hop_solid = collide_first_solid(E, E->x + P->facing, E->y);
  P->play_footstep_on_land = 0.5f;
  if (P->climb_hop_solid) {
    P->climb_hop_solid_pos = v2(P->climb_hop_solid->x, P->climb_hop_solid->y);
    P->hop_wait_x = P->facing;
    P->hop_wait_x_speed = P->facing * ClimbHopX;
  } else {
    P->hop_wait_x = 0;
    P->speed.x = P->facing * ClimbHopX;
  }
  P->speed.y = fminf(P->speed.y, ClimbHopY);
  P->force_move_x = 0;
  P->force_move_x_timer = ClimbHopForceTime;
  P->fast_jump = false;
  P->no_wind_timer = ClimbHopNoWindTime;
}

static bool slip_check(float add_y) {
  float ax, ay;
  if (P->facing == 1) ax = e_right(E), ay = e_top(E) + 4 + add_y;
  else ax = e_left(E) - 1, ay = e_top(E) + 4 + add_y;
  return !point_solid(ax, ay) && !point_solid(ax, ay + (-4 + add_y));
}

/* a LedgeBlocker Madeline would collide with at (x, y) (Tracker.GetComponents<LedgeBlocker>) */
static bool ledge_blocked(float x, float y) {
  if (spinners_hit_rect(x + E->cx, y + E->cy, x + E->cx + E->cw, y + E->cy + E->ch)) return true;
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (o->cls && o->collidable && o->dead != 1 && (spikes_ledge(o) || dust_ledge(o) || tspikes_ledge(o, E, P->facing)) &&
        collide_ent_at(E, x, y, o))
      return true;
  }
  return false;
}
/* ClimbHopBlockedCheck: no hop onto spikes, spinners or tendrils (LedgeBlocker.HopBlockCheck), nor with seeds following */
static bool climb_hop_blocked_check(void) {
  return leader_has_seed() || ledge_blocked(E->x + P->facing * 8, E->y) || solid_at(0, -6);
}

static void sweat_danger(int anim) {
  if (P->stamina <= ClimbTiredThreshold) spr_play(&P->sweat, A_player_sweat_danger, false);
  else spr_play(&P->sweat, anim, false);
}

/* WallBoosterCheck: a wall booster on the side Madeline faces, unless a climb blocker is there */
static Ent *wall_booster_check(void) {
  if (climb_blocker(E->x + P->facing, E->y)) return NULL;
  for (int i = 0; i < g_nents; i++) {
    Ent *o = &g_ents[i];
    if (!o->cls || o->dead == 1 || !o->collidable || !o->cls->name || strcmp(o->cls->name, "wallBooster")) continue;
    int facing = o->cx == 0 ? -1 : 1;   /* (core's walls: Hitbox(2, h) on the left, (2, h, 6, 0) on the right) */
    if (facing == P->facing && collide_ent_at(E, E->x, E->y, o)) return o;
  }
  return NULL;
}

static int climb_update(void) {
  P->climb_no_move_timer -= DT;
  if (P->on_ground) P->stamina = ClimbMaxStamina;
  if (btn_pressed(&g_in.jump) && (!Ducking || can_unduck())) {
    if (P->move_x == -P->facing) wall_jump(-P->facing);
    else climb_jump();
    return ST_NORMAL;
  }
  if (can_dash()) {
    P->speed = v2add(P->speed, lift_boost());
    return start_dash();
  }
  if (!g_in.grab.check) {
    P->speed = v2add(P->speed, lift_boost());
    return ST_NORMAL;
  }
  if (!solid_at((float)P->facing, 0)) {
    if (P->speed.y < 0) climb_hop();
    return ST_NORMAL;
  }
  Ent *booster = wall_booster_check();
  if (P->climb_no_move_timer <= 0 && booster) {
    P->speed.y = approach(P->speed.y, WallBoosterSpeed, WallBoosterAccel * DT);
    actor_set_lift(E, v2(0, fmaxf(P->speed.y, WallBoosterLiftSpeed)));
  } else {
    float target = 0;
    bool try_slip = false;
    if (P->climb_no_move_timer <= 0) {
      if (climb_blocker(E->x + P->facing, E->y)) try_slip = true;
      else if (g_in.move_y == -1) {
        target = ClimbUpSpeed;
        if (solid_at(0, -1) || (climb_hop_blocked_check() && slip_check(-1))) {
          if (P->speed.y < 0) P->speed.y = 0;
          target = 0;
          try_slip = true;
        } else if (slip_check(0)) {
          climb_hop();
          return ST_NORMAL;
        }
      } else if (g_in.move_y == 1) {
        target = ClimbDownSpeed;
        if (P->on_ground) {
          if (P->speed.y > 0) P->speed.y = 0;
          target = 0;
        } else
          wall_slide_particles(P->facing);
      } else
        try_slip = true;
    } else
      try_slip = true;
    P->last_climb_move = (int)signf(target);
    if (try_slip && slip_check(0)) target = ClimbSlipSpeed;
    P->speed.y = approach(P->speed.y, target, ClimbAccel * DT);
  }
  if (g_in.move_y != 1 && P->speed.y > 0 && !solid_at((float)P->facing, 1)) P->speed.y = 0;
  if (P->climb_no_move_timer <= 0) {
    if (P->last_climb_move == -1) {
      P->stamina -= ClimbUpCost * DT;
      if (P->stamina <= ClimbTiredThreshold) spr_play(&P->sweat, A_player_sweat_danger, false);
      else if (!spr_is(&P->sweat, A_player_sweat_climbLoop)) spr_play(&P->sweat, A_player_sweat_climb, false);
    } else {
      if (P->last_climb_move == 0) P->stamina -= ClimbStillCost * DT;
      if (!P->on_ground) sweat_danger(A_player_sweat_still);
      else sweat_danger(A_player_sweat_idle);
    }
  } else
    sweat_danger(A_player_sweat_idle);
  if (P->stamina <= 0) {
    P->speed = v2add(P->speed, lift_boost());
    return ST_NORMAL;
  }
  return ST_CLIMB;
}

/* ---------------------------------------------------------------- dash */
/* Player.CreateTrail: TrailManager.Add(this, (|Scale.X| * Facing, Scale.Y), wasDashB ? NormalHairColor :
 * UsedHairColor), 1 s, at Depth + 1; what it shows is taken at the end of the frame (trail_snapshots) */
static void create_trail(void) {
  if (P->trail_n < 2) P->trail_n++;
  P->trail_color = rgb(P->was_dash_b ? NormalHairColor : UsedHairColor);
  P->trail_depth = (int16_t)(E->depth + 1);
}
/* TrailManager.BeforeRender: the sprite's frame and the hair's nodes as they are drawn this frame */
static void trail_snapshots(void) {
  for (int n = P->trail_n; n; n--) {
    P->trail_n = 0;
    float x = floorf(E->x), y = floorf(E->y);
    Trail *t = trail_add(x, y, spr_tex(&P->spr), P->spr.ox, P->spr.oy, fabsf(P->spr.sx) * P->facing, P->spr.sy, 0, 1, P->trail_depth);
    if (!t) return;
    t->color = P->trail_color;
    t->sdy = (int8_t)P->spr_dy;
    int hx, hy, fr, carry;
    bool has;
    hair_meta(t->tex, &hx, &hy, &fr, &has, &carry);
    if (!has) continue;
    t->nhair = (uint8_t)(P->hair_count < TRAIL_HAIR ? P->hair_count : TRAIL_HAIR);
    t->bangs = (uint8_t)(fr < 3 ? fr : 0);
    for (int i = 0; i < t->nhair; i++) {
      V2 h = P->hair[i];
      if (!i) h = v2(floorf(h.x), floorf(h.y));
      t->hair[i][0] = (int8_t)(floorf(h.x + 0.5f) - x), t->hair[i][1] = (int8_t)(floorf(h.y + 0.5f) - y);
    }
  }
}

static void dash_begin(void) {
  P->called_dash_events = false;
  P->dash_started_on_ground = P->on_ground;
  P->launched = false;
  level_freeze(.05f);
  P->dash_cooldown_timer = DashCooldown;
  P->dash_refill_cooldown_timer = DashRefillCooldown;
  P->started_dashing = true;
  P->wall_slide_timer = WallSlideTime;
  P->dash_trail_timer = 0;
  P->dash_attack_timer = DashAttackTime;
  P->before_dash_speed = P->speed;
  P->speed = v2(0, 0);
  P->dash_dir = v2(0, 0);
  if (!P->on_ground && Ducking && can_unduck()) set_ducking(false);
}
static void call_dash_events(void) {
  if (!P->called_dash_events) {
    P->called_dash_events = true;
    if (!P->current_booster) {
      g_save.total_dashes++;
      g_session.dashes++;
      level_dash_listeners(P->dash_dir);
    } else {
      level_booster_boosted(P->current_booster, P->dash_dir);
      P->current_booster = NULL;
    }
  }
}
static void dash_end(void) { call_dash_events(); }

static int dash_update(void) {
  P->started_dashing = false;
  if (P->dash_trail_timer > 0) {
    P->dash_trail_timer -= DT;
    if (P->dash_trail_timer <= 0) create_trail();
  }
  if (!P->holding && g_in.grab.check && !is_tired() && can_unduck()) {
    Ent *h = holdable_check();
    if (h) {
      set_ducking(false);
      P->holding = h;
      P->min_hold_timer = HoldMinTime;
      return ST_PICKUP;
    }
  }
  if (P->dash_dir.y == 0) {
    for (int i = 0; i < g_nents; i++) {
      Ent *jt = &g_ents[i];
      if ((jt->kind & KIND_JUMPTHRU) && jt->cls && jt->collidable && jt->dead != 1 && collide_ent_at(E, E->x, E->y, jt) &&
          e_bottom(E) - e_top(jt) <= DashHJumpThruNudge)
        actor_move_v_exact(E, (int)(e_top(jt) - e_bottom(E)), NULL, NULL);
    }
    if (can_unduck() && btn_pressed(&g_in.jump) && P->jump_grace_timer > 0) {
      super_jump();
      return ST_NORMAL;
    }
  }
  if (P->dash_dir.x == 0 && P->dash_dir.y == -1) {
    if (btn_pressed(&g_in.jump) && can_unduck()) {
      if (wall_jump_check(1)) { super_wall_jump(-1); return ST_NORMAL; }
      if (wall_jump_check(-1)) { super_wall_jump(1); return ST_NORMAL; }
    }
  } else if (btn_pressed(&g_in.jump) && can_unduck()) {
    if (wall_jump_check(1)) { wall_jump(-1); return ST_NORMAL; }
    if (wall_jump_check(-1)) { wall_jump(1); return ST_NORMAL; }
  }
  if ((P->speed.x != 0 || P->speed.y != 0) && level_on_interval(0.02f))
    particles_emit(PL_FG, P->was_dash_b ? &P_Player_P_DashB : &P_Player_P_DashA, 1, center(), v2(2, 2), angle_of(P->dash_dir));
  return ST_DASH;
}

static float co_dash(void) {
  switch (P->co_step) {
    case 0:
      P->co_step = 1;
      return 0;   /* yield null */
    case 1: {
      V2 dir = P->last_aim;
      V2 ns = v2mul(dir, DashSpeed);
      if (signf(P->before_dash_speed.x) == signf(ns.x) && fabsf(P->before_dash_speed.x) > fabsf(ns.x)) ns.x = P->before_dash_speed.x;
      P->speed = ns;
      if (water_at(0, 0)) P->speed = v2mul(P->speed, SwimDashSpeedMult);
      P->dash_dir = dir;
      level_dir_shake(dir, .2f);
      if (dir.x != 0) P->facing = (int)signf(dir.x);
      call_dash_events();
      if (P->on_ground && P->dash_dir.x != 0 && P->dash_dir.y > 0 && P->speed.y > 0 &&
          (!P->inventory_dreamdash || !collide_first_kind(E, E->x, E->y + 1, KIND_DREAMBLOCK))) {
        P->dash_dir.x = signf(P->dash_dir.x);
        P->dash_dir.y = 0;
        P->speed.y = 0;
        P->speed.x *= DodgeSlideSpeedMult;
        set_ducking(true);
      }
      create_trail();
      P->dash_trail_timer = .08f;
      P->swap_cancel = v2(1, 1);
      P->co_step = 2;
      return DashTime;
    }
    case 2:
      create_trail();
      P->auto_jump = true;
      P->auto_jump_timer = 0;
      if (P->dash_dir.y <= 0) {
        P->speed = v2mul(P->dash_dir, EndDashSpeed);
        P->speed.x *= P->swap_cancel.x;
        P->speed.y *= P->swap_cancel.y;
      }
      if (P->speed.y < 0) P->speed.y *= EndDashUpMult;
      SET(ST_NORMAL);
      return -1;
  }
  return -1;
}

/* ---------------------------------------------------------------- swim */
static void swim_begin(void) {
  if (P->speed.y > 0) P->speed.y *= SwimYSpeedMult;
  P->stamina = ClimbMaxStamina;
}
static int swim_update(void) {
  if (!swim_check()) return ST_NORMAL;
  if (can_unduck()) set_ducking(false);
  if (can_dash()) {
    btn_consume_buffer(&g_in.dash);
    return ST_DASH;
  }
  bool under = swim_underwater_check();
  if (!under && P->speed.y >= 0 && g_in.grab.check && !is_tired() && can_unduck()) {
    if (signf(P->speed.x) != -P->facing && climb_check(P->facing, 0))
      if (!actor_move_v_exact(E, -1, NULL, NULL)) {
        set_ducking(false);
        return ST_CLIMB;
      }
  }
  V2 move = v2((float)g_in.aim_x, (float)g_in.aim_y);
  move = v2norm(move);
  float maxx = under ? SwimUnderwaterMax : SwimMax, maxy = SwimMax;
  if (fabsf(P->speed.x) > SwimMax && signf(P->speed.x) == signf(move.x))
    P->speed.x = approach(P->speed.x, maxx * move.x, SwimReduce * DT);
  else
    P->speed.x = approach(P->speed.x, maxx * move.x, SwimAccel * DT);
  if (move.y == 0 && swim_rise_check())
    P->speed.y = approach(P->speed.y, SwimMaxRise, SwimAccel * DT);
  else if (move.y >= 0 || swim_underwater_check()) {
    if (fabsf(P->speed.y) > SwimMax && signf(P->speed.y) == signf(move.y))
      P->speed.y = approach(P->speed.y, maxy * move.y, SwimReduce * DT);
    else
      P->speed.y = approach(P->speed.y, maxy * move.y, SwimAccel * DT);
  }
  if (!under && P->move_x != 0 && solid_at((float)P->move_x, 0) && !solid_at((float)P->move_x, -3)) climb_hop();
  if (btn_pressed(&g_in.jump) && swim_jump_check()) {
    jump();
    return ST_NORMAL;
  }
  return ST_SWIM;
}

/* ---------------------------------------------------------------- boost (bubbles) */
void player_boost(Player *p, Ent *booster, bool red) {
  player_set_state(p, ST_BOOST);
  p->speed = v2(0, 0);
  p->boost_target = e_center(booster);
  p->boost_red = red;
  p->last_booster = p->current_booster = booster;
}
static void boost_begin(void) {
  player_refill_dash(P);
  player_refill_stamina(P);
}
static void boost_end(void) {
  V2 to = v2(floorf(P->boost_target.x - (E->cx + E->cw / 2)), floorf(P->boost_target.y - (E->cy + E->ch / 2)));
  actor_move_to_x(E, to.x, NULL);
  actor_move_to_y(E, to.y, NULL);
}
static int boost_update(void) {
  V2 add = v2(g_in.aim_x * 3.f, g_in.aim_y * 3.f);
  V2 target = v2add(v2sub(P->boost_target, v2(E->cx + E->cw / 2, E->cy + E->ch / 2)), add);
  V2 ex = v2(E->x + E->remx, E->y + E->remy);
  V2 d = v2sub(target, ex);
  float l = v2len(d), m = BoostMoveSpeed * DT;
  V2 to = l <= m ? target : v2add(ex, v2mul(d, m / l));
  actor_move_to_x(E, to.x, NULL);
  actor_move_to_y(E, to.y, NULL);
  if (btn_pressed(&g_in.dash)) {
    btn_consume(&g_in.dash);
    return P->boost_red ? ST_REDDASH : ST_DASH;
  }
  return ST_BOOST;
}
static float co_boost(void) {
  if (P->co_step == 0) {
    P->co_step = 1;
    return BoostTime;
  }
  SET(P->boost_red ? ST_REDDASH : ST_DASH);
  return -1;
}

/* ---------------------------------------------------------------- red dash */
static void red_dash_begin(void) {
  P->called_dash_events = false;
  P->dash_started_on_ground = false;
  level_freeze(.05f);
  dust_burst(v2(E->x, E->y), angle_of(v2(-P->dash_dir.x, -P->dash_dir.y)), 8);
  P->dash_cooldown_timer = DashCooldown;
  P->dash_refill_cooldown_timer = DashRefillCooldown;
  P->started_dashing = true;
  P->dash_attack_timer = DashAttackTime;
  P->speed = v2(0, 0);
  if (!P->on_ground && Ducking && can_unduck()) set_ducking(false);
}
static void red_dash_end(void) { call_dash_events(); }
static int red_dash_update(void) {
  P->started_dashing = false;
  if (can_dash()) return start_dash();
  if (P->dash_dir.y == 0) {
    for (int i = 0; i < g_nents; i++) {
      Ent *jt = &g_ents[i];
      if ((jt->kind & KIND_JUMPTHRU) && jt->cls && jt->collidable && jt->dead != 1 && collide_ent_at(E, E->x, E->y, jt) &&
          e_bottom(E) - e_top(jt) <= DashHJumpThruNudge)
        actor_move_v_exact(E, (int)(e_top(jt) - e_bottom(E)), NULL, NULL);
    }
    if (can_unduck() && btn_pressed(&g_in.jump) && P->jump_grace_timer > 0) {
      super_jump();
      return ST_NORMAL;
    }
  } else if (P->dash_dir.x == 0 && P->dash_dir.y == -1) {
    if (btn_pressed(&g_in.jump) && can_unduck()) {
      if (wall_jump_check(1)) { super_wall_jump(-1); return ST_NORMAL; }
      if (wall_jump_check(-1)) { super_wall_jump(1); return ST_NORMAL; }
    }
  } else if (btn_pressed(&g_in.jump) && can_unduck()) {
    if (wall_jump_check(1)) { wall_jump(-1); return ST_NORMAL; }
    if (wall_jump_check(-1)) { wall_jump(1); return ST_NORMAL; }
  }
  return ST_REDDASH;
}
static float co_red_dash(void) {
  if (P->co_step == 0) {
    P->co_step = 1;
    return 0;
  }
  P->speed = v2mul(P->last_aim, DashSpeed);
  P->dash_dir = P->last_aim;
  level_dir_shake(P->dash_dir, .2f);
  call_dash_events();
  return -1;
}

/* ---------------------------------------------------------------- hit squash, launch */
static void hit_squash_begin(void) { P->hit_squash_no_move_timer = HitSquashNoMoveTime; }
static int hit_squash_update(void) {
  P->speed.x = approach(P->speed.x, 0, HitSquashFriction * DT);
  P->speed.y = approach(P->speed.y, 0, HitSquashFriction * DT);
  if (btn_pressed(&g_in.jump)) {
    if (P->on_ground) jump();
    else if (wall_jump_check(1)) wall_jump(-1);
    else if (wall_jump_check(-1)) wall_jump(1);
    else btn_consume_buffer(&g_in.jump);
    return ST_NORMAL;
  }
  if (can_dash()) return start_dash();
  if (g_in.grab.check && climb_check(P->facing, 0)) return ST_CLIMB;
  if (P->hit_squash_no_move_timer > 0) P->hit_squash_no_move_timer -= DT;
  else return ST_NORMAL;
  return ST_HITSQUASH;
}

V2 player_explode_launch(Player *p, V2 from, bool snap_up) {
  level_freeze(.1f);
  p->has_launch_approach = false;
  V2 d = v2sub(player_center(p), from);
  float l = v2len(d);
  V2 n = l > 0 ? v2mul(d, 1 / l) : v2(0, -1);
  float dot = n.y;
  if (snap_up && dot <= -.7f) n = v2(0, -1);
  else if (dot <= .55f && dot >= -.55f) n = v2(signf(n.x), 0);
  p->speed = v2mul(n, LaunchSpeed);
  if (p->speed.y <= 50) {
    p->speed.y = fminf(-150, p->speed.y);
    p->auto_jump = true;
  }
  if (g_in.move_x == signf(p->speed.x)) p->speed.x *= 1.2f;
  if (!p->inventory_norefills) player_refill_dash(p);
  player_refill_stamina(p);
  p->dash_cooldown_timer = DashCooldown;
  player_set_state(p, ST_LAUNCH);
  return n;
}
static void launch_begin(void) { P->launched = true; }
static int launch_update(void) {
  if (P->has_launch_approach) actor_move_towards_x(E, P->launch_approach_x, 60 * DT, NULL);
  if (can_dash()) return start_dash();
  if (P->speed.y < 0) P->speed.y = approach(P->speed.y, MaxFall, Gravity * .5f * DT);
  else P->speed.y = approach(P->speed.y, MaxFall, Gravity * .25f * DT);
  P->speed.x = approach(P->speed.x, 0, RunAccel * .2f * DT);
  if (v2len(P->speed) < LaunchCancelThreshold) return ST_NORMAL;
  return ST_LAUNCH;
}

/* ---------------------------------------------------------------- summit launch */
static void summit_launch_begin(void) {
  P->wall_boost_timer = 0;
  play(A_player_launch);
  P->speed = v2(0, -DashSpeed);
  P->summit_particle_timer = .4f;
}
static int summit_launch_update(void) {
  P->summit_particle_timer -= DT;
  P->facing = 1;
  actor_move_towards_x(E, P->summit_target_x, 20 * DT, NULL);
  P->speed = v2(0, -DashSpeed);
  return ST_SUMMITLAUNCH;
}

/* ---------------------------------------------------------------- pickup (Theo's crystal) */
static Ent *holdable_check(void) { return level_holdable_check(); }
static float co_pickup(void) {
  switch (P->co_step) {
    case 0:
      P->before_dash_speed = P->speed;   /* oldSpeed */
      P->co_t = P->var_jump_timer;
      P->speed = v2(0, 0);
      P->co_step = 1;
      return 0.16f;
    default:
      P->carry_offset = v2(0, -12);
      P->speed = P->before_dash_speed;
      P->speed.y = fminf(P->speed.y, 0);
      P->var_jump_timer = P->co_t;
      SET(ST_NORMAL);
      return -1;
  }
}

/* ---------------------------------------------------------------- dream dash */
static void dream_dash_begin(void) {
  P->speed = v2mul(P->dash_dir, DreamDashSpeed);
  E->treat_naive = 1;
  E->depth = D_PLAYERDREAMDASHING;
  ents_mark_unsorted();
  P->dream_dash_can_end_timer = DreamDashMinTime;
  P->stamina = ClimbMaxStamina;
  P->dream_jump = false;
}
static void dream_dash_end(void) {
  E->depth = D_PLAYER;
  ents_mark_unsorted();
  if (!P->dream_jump) {
    P->auto_jump = true;
    P->auto_jump_timer = 0;
  }
  if (!P->inventory_norefills) player_refill_dash(P);
  player_refill_stamina(P);
  E->treat_naive = 0;
  if (P->dream_block) {
    if (P->dash_dir.x != 0) {
      P->jump_grace_timer = JumpGraceTime;
      P->dream_jump = true;
    } else
      P->jump_grace_timer = 0;
    P->dream_block = NULL;
  }
}
static bool dream_dashed_into_solid(void) {
  if (solid_at(0, 0)) {
    for (int x = 1; x <= DreamDashEndWiggle; x++)
      for (int xm = -1; xm <= 1; xm += 2)
        for (int y = 1; y <= DreamDashEndWiggle; y++)
          for (int ym = -1; ym <= 1; ym += 2)
            if (!solid_at((float)(x * xm), (float)(y * ym))) {
              E->x += x * xm, E->y += y * ym;
              return false;
            }
    return true;
  }
  return false;
}
static int dream_dash_update(void) {
  actor_naive_move(E, v2mul(P->speed, DT));
  if (P->dream_dash_can_end_timer > 0) P->dream_dash_can_end_timer -= DT;
  Ent *block = collide_first_kind(E, E->x, E->y, KIND_DREAMBLOCK);
  if (!block) {
    if (dream_dashed_into_solid()) player_die(P, v2(0, 0), false);
    else if (P->dream_dash_can_end_timer <= 0) {
      level_freeze(.05f);
      if (btn_pressed(&g_in.jump) && P->dash_dir.x != 0) {
        P->dream_jump = true;
        jump();
      } else {
        bool l = climb_check(-1, 0), r = climb_check(1, 0);
        if (g_in.grab.check && (P->dash_dir.y >= 0 || P->dash_dir.x != 0) && ((P->move_x == 1 && r) || (P->move_x == -1 && l))) {
          P->facing = P->move_x;
          return ST_CLIMB;
        }
      }
      return ST_NORMAL;
    }
  } else {
    P->dream_block = block;
    if (level_on_interval(0.1f)) create_trail();
  }
  return ST_DREAMDASH;
}

/* ---------------------------------------------------------------- star fly (feathers) */
bool player_start_star_fly(Player *p) {
  player_refill_stamina(p);
  if (p->state == ST_REFLECTIONFALL) return false;
  if (p->state == ST_STARFLY) {
    p->star_fly_timer = StarFlyTime;
    p->spr.tint = rgb(StarFlyColor);
  } else
    player_set_state(p, ST_STARFLY);
  return true;
}
static void star_fly_begin(void) {
  play(A_player_startStarFly);
  P->star_fly_transforming = true;
  P->star_fly_timer = StarFlyTime;
  P->star_fly_speed_lerp = 0;
  P->jump_grace_timer = 0;
  set_box(HB_STARFLY);
  hurtbox = HB_STARFLY_HURT;
}
static void star_fly_return_to_normal_hitbox(void) {
  set_box(HB_NORMAL);
  hurtbox = HB_NORMAL_HURT;
  if (solid_at(0, 0)) {
    float sy = E->y;
    E->y -= (HB_NORMAL[1] + HB_NORMAL[3]) - (HB_STARFLY[1] + HB_STARFLY[3]);
    if (solid_at(0, 0)) E->y = sy;
    else return;
    set_ducking(true);
    E->y -= (HB_DUCK[1] + HB_DUCK[3]) - (HB_STARFLY[1] + HB_STARFLY[3]);
    if (solid_at(0, 0)) E->y = sy;
  }
}
static void star_fly_end(void) {
  P->hair_outline = false;
  P->spr.tint = 0xFFFF;
  P->hair_count = P->start_hair_count;
  star_fly_return_to_normal_hitbox();
}
static float co_star_fly(void) {
  switch (P->co_step) {
    case 0:
      if (P->spr.anim == A_player_startStarFly) return 0;
      P->co_step = 1;
      /* fall through */
    case 1:
      if (P->speed.x != 0 || P->speed.y != 0) return 0;
      P->co_step = 2;
      return .1f;
    case 2: {
      P->spr.tint = rgb(StarFlyColor);
      P->hair_count = 7;
      P->hair_outline = true;
      P->star_fly_transforming = false;
      P->star_fly_timer = StarFlyTime;
      player_refill_dash(P);
      player_refill_stamina(P);
      V2 dir = v2((float)g_in.aim_x, (float)g_in.aim_y);
      if (dir.x == 0 && dir.y == 0) dir = v2((float)P->facing, 0);
      dir = v2norm(dir);
      P->speed = v2mul(dir, StarFlyStartSpeed);
      P->star_fly_last_dir = dir;
      level_dir_shake(dir, .3f);
      P->co_step = 3;
      return 0;
    }
  }
  return -1;
}
static V2 rotate_towards(V2 v, float target, float max) {
  float a = angle_of(v), d = target - a;
  while (d > PI_F) d -= 2 * PI_F;
  while (d < -PI_F) d += 2 * PI_F;
  if (fabsf(d) > max) d = max * signf(d);
  float l = v2len(v);
  return angle_vec(a + d, l);
}
static int star_fly_update(void) {
  if (P->star_fly_transforming) {
    float l = v2len(P->speed), nl = approach(l, 0, StarFlyTransformDeccel * DT);
    P->speed = l > 0 ? v2mul(P->speed, nl / l) : P->speed;
  } else {
    V2 aim = v2((float)g_in.aim_x, (float)g_in.aim_y);
    bool slow = false;
    if (aim.x == 0 && aim.y == 0) {
      slow = true;
      aim = P->star_fly_last_dir;
    }
    aim = v2norm(aim);
    V2 cur = v2norm(P->speed);
    if (cur.x == 0 && cur.y == 0) cur = aim;
    else cur = rotate_towards(cur, angle_of(aim), StarFlyRotateSpeed * DT);
    P->star_fly_last_dir = cur;
    float max;
    if (slow) {
      P->star_fly_speed_lerp = 0;
      max = StarFlySlowSpeed;
    } else if ((cur.x != 0 || cur.y != 0) && cur.x * aim.x + cur.y * aim.y >= .45f) {
      P->star_fly_speed_lerp = approach(P->star_fly_speed_lerp, 1, DT / StarFlyMaxLerpTime);
      max = lerpf(StarFlyTargetSpeed, StarFlyMaxSpeed, P->star_fly_speed_lerp);
    } else {
      P->star_fly_speed_lerp = 0;
      max = StarFlyTargetSpeed;
    }
    float s = approach(v2len(P->speed), max, StarFlyAccel * DT);
    P->speed = v2mul(cur, s);
    if (btn_pressed(&g_in.jump)) {
      if (actor_on_ground(E, 3)) { jump(); return ST_NORMAL; }
      if (wall_jump_check(-1)) { wall_jump(1); return ST_NORMAL; }
      if (wall_jump_check(1)) { wall_jump(-1); return ST_NORMAL; }
    }
    if (g_in.grab.check) {
      if (g_in.move_x != -1 && climb_check(1, 0)) { P->facing = 1; return ST_CLIMB; }
      if (g_in.move_x != 1 && climb_check(-1, 0)) { P->facing = -1; return ST_CLIMB; }
    }
    if (can_dash()) return start_dash();
    P->star_fly_timer -= DT;
    if (P->star_fly_timer <= 0) {
      if (g_in.move_y == -1) P->speed.y = StarFlyExitUp;
      if (g_in.move_y < 1) {
        P->var_jump_speed = P->speed.y;
        P->auto_jump = true;
        P->auto_jump_timer = 0;
        P->var_jump_timer = VarJumpTime;
      }
      if (P->speed.y > StarFlyMaxExitY) P->speed.y = StarFlyMaxExitY;
      if (fabsf(P->speed.x) > StarFlyMaxExitX) P->speed.x = StarFlyMaxExitX * signf(P->speed.x);
      return ST_NORMAL;
    }
    if (P->star_fly_timer < StarFlyEndFlashDuration && level_on_interval(0.05f))
      P->spr.tint = P->spr.tint == rgb(StarFlyColor) ? rgb(NormalHairColor) : rgb(StarFlyColor);
  }
  return ST_STARFLY;
}

/* ---------------------------------------------------------------- attract, dummy, frozen, intros */
void player_start_attract(Player *p, V2 to) {
  p->attract_to = v2(roundf(to.x), roundf(to.y));
  player_set_state(p, ST_ATTRACT);
}
static void attract_begin(void) { P->speed = v2(0, 0); }
static int attract_update(void) {
  V2 ex = v2(E->x + E->remx, E->y + E->remy);
  V2 d = v2sub(P->attract_to, ex);
  if (v2len(d) <= 1.5f) {
    E->x = P->attract_to.x, E->y = P->attract_to.y;
    E->remx = E->remy = 0;
  } else {
    float l = v2len(d), m = 200 * DT;
    V2 at = l <= m ? P->attract_to : v2add(ex, v2mul(d, m / l));
    actor_move_to_x(E, at.x, NULL);
    actor_move_to_y(E, at.y, NULL);
  }
  return ST_ATTRACT;
}
static void dummy_begin(void) {
  P->dummy_moving = false;
  P->dummy_gravity = true;
  P->dummy_auto_animate = true;
}
static int dummy_update(void) {
  if (can_unduck()) set_ducking(false);
  if (!P->on_ground && P->dummy_gravity) {
    float mult = (fabsf(P->speed.y) < HalfGravThreshold && (g_in.jump.check || P->auto_jump)) ? .5f : 1.f;
    if (g_level.in_space) mult *= SpacePhysicsMult;
    P->speed.y = approach(P->speed.y, MaxFall, Gravity * mult * DT);
  }
  if (P->var_jump_timer > 0) {
    if (P->auto_jump || g_in.jump.check) P->speed.y = fminf(P->speed.y, P->var_jump_speed);
    else P->var_jump_timer = 0;
  }
  if (!P->dummy_moving) {
    if (fabsf(P->speed.x) > MaxRun && P->dummy_maxspeed) P->speed.x = approach(P->speed.x, MaxRun * signf(P->speed.x), RunAccel * 2.5f * DT);
    if (P->dummy_friction) P->speed.x = approach(P->speed.x, 0, RunAccel * DT);
  }
  if (P->dummy_auto_animate) {
    if (P->on_ground) play(P->speed.x == 0 ? A_player_idle : A_player_walk);
    else play(P->speed.y < 0 ? A_player_jumpSlow : A_player_fallSlow);
  }
  return ST_DUMMY;
}
static int frozen_update(void) { return ST_FROZEN; }
static int cassette_update(void) { return ST_CASSETTEFLY; }
/* StartCassetteFly: back to the start in a bubble, along a curve */
void player_start_cassette_fly(Player *p, V2 target, V2 control) {
  CassetteFly *f = cassette_fly();
  player_set_state(p, ST_CASSETTEFLY);
  f->a = v2(p->ent->x, p->ent->y), f->b = target, f->c = control;
  f->lerp = 0;
  p->speed = v2(0, 0);
  if (p->holding) holdable_release(p->holding, v2(0, 0)), p->holding = NULL;   /* Drop */
}
static void cassette_fly_begin(void) {
  play(A_player_bubble);
  P->spr_dy += 5;
}
static float co_cassette_fly(void) {
  CassetteFly *f = cassette_fly();
  switch (P->co_step) {
    case 0:
      g_level.no_retry = true;
      g_level.formation = true;
      g_level.formation_alpha = 0.5f;
      P->spr.sx = P->spr.sy = 1.25f;
      E->depth = -2000000;
      P->co_step = 1;
      return 0.4f;
    case 1:
      if (f->lerp < 1) {
        if (level_on_interval(0.03f)) particles_emit(PL_MID, &P_Player_P_CassetteFly, 2, center(), v2(4, 4), 0);
        f->lerp = approach(f->lerp, 1, 1.6f * DT);
        float t = ease_sine_inout(f->lerp), u = 1 - t;
        E->x = u * u * f->a.x + 2 * u * t * f->c.x + t * t * f->b.x;
        E->y = u * u * f->a.y + 2 * u * t * f->c.y + t * t * f->b.y;
        g_level.cam = player_camera_target(P);
        return 0;
      }
      E->x = f->b.x, E->y = f->b.y;
      P->spr.sx = P->spr.sy = 1.25f;
      P->spr_dy -= 5;
      play(A_player_fallFast);
      P->co_step = 2;
      return 0.2f;
    case 2:
      g_level.no_retry = false;
      g_level.formation = false;
      g_level.formation_alpha = 0.5f;
      SET(ST_NORMAL);
      E->depth = 0;
      return -1;
  }
  return -1;
}
static int bird_update(void) { return ST_BIRDDASHTUTORIAL; }
/* IntroTypes.Fall (Reflection, after the dream): FallEffects is not drawn */
static void reflection_fall_begin(void) { E->ignore_jumpthrus = 1; }
static void reflection_fall_end(void) { E->ignore_jumpthrus = 0; }
static int reflection_fall_update(void) {
  P->facing = 1;
  if (level_on_interval(0.05f)) {
    P->was_dash_b = true;
    create_trail();
  }
  if (water_at(0, 0)) P->speed.y = approach(P->speed.y, -20, 400 * DT);
  else P->speed.y = approach(P->speed.y, MaxFall * 2, Gravity * 0.25f * DT);
  for (int i = 0; i < g_nents; i++) {   /* the feathers go */
    Ent *o = &g_ents[i];
    if (o->cls && o->cls->name && (!strcmp(o->cls->name, "infiniteStar") || !strcmp(o->cls->name, "flyFeather"))) ent_remove(o);
  }
  if (spinners_hit_rect(E->x - 6, E->y - 6, E->x + 6, E->y + 6)) {   /* a crystal in the way breaks */
    spinners_destroy_near(E->x, E->y, 12);
    level_shake(0.3f);
    level_freeze(0.01f);
  }
  return ST_REFLECTIONFALL;
}
/* Level.StartCutscene(OnReflectionFallSkip): skipped, the level goes on in room 00 */
typedef struct { Cutscene cs; } FallScene;
static const EntClass FALLSCENE = {.name = "reflectionFall", .size = sizeof(FallScene)};
static Ent *fall_scene;
static void fall_scene_end(Ent *e, bool skipped) {
  (void)e;
  fall_scene = NULL;
  int r = skipped ? chapter_find_room(g_level.chapter, "00") : -1;
  if (r >= 0) level_load_room_near(r, INTRO_NONE, 0, 1);
}
static float co_reflection_fall(void) {
  switch (P->co_step) {
    case 0:
      play(A_player_bigFall);
      fall_scene = ent_new(&FALLSCENE, 0, 0);
      if (fall_scene) fall_scene->visible = 0, fall_scene->collidable = 0, cutscene_start(fall_scene, fall_scene_end, true, false);
      P->co_t = 0;
      P->co_step = 1;
      /* fall through */
    case 1:   /* held up for 2 s */
      P->speed.y = 0;
      if ((P->co_t += DT) < 2) return 0;
      P->speed.y = 320;
      P->co_step = 2;
      return 0;
    case 2:
      if (!water_at(0, 0)) return 0;
      play(A_player_bigFallRecover);
      if (fall_scene && fall_scene->cls == &FALLSCENE) cutscene_end(fall_scene);
      P->co_step = 3;
      return 1.2f;
    default:
      SET(ST_NORMAL);
      return -1;
  }
}
static int temple_fall_update(void) {
  P->facing = 1;
  if (!P->on_ground) {
    float c = g_level.room->x + 160.f;
    int mx = fabsf(c - E->x) > 4 ? (int)signf(c - E->x) : 0;
    P->speed.x = approach(P->speed.x, MaxRun * .6f * mx, RunAccel * .5f * AirMult * DT);
  }
  if (!P->on_ground && P->dummy_gravity) P->speed.y = approach(P->speed.y, MaxFall * 2, Gravity * 0.25f * DT);
  return ST_TEMPLEFALL;
}
static float co_temple_fall(void) {
  switch (P->co_step) {
    case 0:
      play(A_player_fallFast);
      P->co_step = 1;
      /* fall through */
    case 1:
      if (!P->on_ground) return 0;
      play(P->dashes <= 1 ? A_player_fallPose : A_player_idle);
      P->spr.sy = 0.7f;
      level_dir_shake(v2(0, 1), 0.5f);
      P->speed.x = 0;
      P->co_t = 0;
      P->co_step = 2;
      return 0;
    case 2:
      P->co_t += DT;
      if (P->co_t < 1) return 0;
      SET(ST_NORMAL);
      return -1;
  }
  return -1;
}
/* BirdDashTutorialCoroutine: the prologue's first dash, up and right, onto the cliff */
static float co_bird(void) {
  Player *p = P;
  switch (p->co_step) {
    case 0:
      p->co_step = 1;
      return 0;
    case 1: {
      create_trail();
      p->bird_trails = 2;   /* the alarms at 0.08 and 0.15 */
      p->bird_trail_t = 0;
      p->co_t = 0;
      V2 d = v2norm(v2(1, -1));
      p->facing = 1;
      p->speed = v2mul(d, 240);
      p->dash_dir = d;
      level_dir_shake(d, 0.2f);
      p->co_step = 2;
    }
      /* fall through */
    case 2:
      if (p->co_t < 0.15f) {
        if ((p->speed.x != 0 || p->speed.y != 0) && level_on_interval(0.02f))
          particles_emit1(PL_FG, &P_Player_P_DashA, v2add(center(), v2(rnd_rangef(-2, 2), rnd_rangef(-2, 2))), angle_of(p->dash_dir));
        p->co_t += DT;
        return 0;
      }
      p->auto_jump = true;
      p->auto_jump_timer = 0;
      if (p->dash_dir.y <= 0) p->speed = v2mul(p->dash_dir, 160);
      if (p->speed.y < 0) p->speed.y *= 0.75f;
      play(A_player_fallFast);
      p->bird_climbing = false;
      p->co_step = 3;
      /* fall through */
    case 3:
      if (!actor_on_ground(E, 1) && !p->bird_climbing) {
        p->speed.y = approach(p->speed.y, 160, 900 * DT);
        if (solid_at(1, 0)) p->bird_climbing = true;
        if (e_top(E) > g_level.room->y + g_level.room->h) {
          g_level.in_cutscene = false;   /* CancelCutscene */
          g_level.skipping_cutscene = false;
          player_die(p, v2(0, 0), false);
        }
        return 0;
      }
      if (p->bird_climbing) {
        play(A_player_wallslide);
        dust_burst(v2add(v2(E->x, E->y), v2(4, -6)), PI_F, 1);
        p->speed.y = 0;
        p->co_step = 4;
        return 0.2f;
      }
      play(A_player_tired);
      p->speed.y = 0;
      p->co_step = 7;
      return 0;
    case 4:
      play(A_player_climbup);
      p->co_step = 5;
      /* fall through */
    case 5:
      if (solid_at(1, 0)) {
        E->y += -45 * DT;
        return 0;
      }
      E->y = roundf(E->y);
      play(A_player_jumpFast);
      p->speed.y = -105;
      p->co_step = 6;
      /* fall through */
    case 6:
      if (!actor_on_ground(E, 1)) {
        p->speed.y = approach(p->speed.y, 160, 900 * DT);
        p->speed.x = 20;
        return 0;
      }
      p->speed = v2(0, 0);
      play(A_player_walk);
      p->co_t = 0;
      p->co_step = 8;
      /* fall through */
    case 8:
      if (p->co_t < 0.5f) {
        E->x += 32 * DT;
        p->co_t += DT;
        return 0;
      }
      play(A_player_tired);
      return -1;
    case 7:
      if (p->speed.x != 0) {
        p->speed.x = approach(p->speed.x, 0, 240 * DT);
        if (level_on_interval(0.04f)) dust_burst(v2add(bottom_center(), v2(0, -2)), PI_F * -3 / 4, 1);
        return 0;
      }
      return -1;
  }
  return -1;
}
static void bird_begin(void) {
  dash_begin();
  play(A_player_dash);
}

static V2 intro_start;
static float co_intro_walk(void) {
  switch (P->co_step) {
    case 0:
      intro_start = v2(E->x, E->y);
      if (P->intro == INTRO_WALKINRIGHT) E->x = g_level.room->x - 16.f, P->facing = 1;
      else E->x = g_level.room->x + g_level.room->w + 16.f, P->facing = -1;
      P->co_step = 1;
      return .3f;
    case 1:
      play(A_player_runSlow);
      P->co_step = 2;
      /* fall through */
    case 2:
      if (fabsf(E->x - intro_start.x) > 2 && !solid_at((float)P->facing, 0)) {
        actor_move_towards_x(E, intro_start.x, 64 * DT, NULL);
        return 0;
      }
      E->x = intro_start.x, E->y = intro_start.y;
      play(A_player_idle);
      P->co_step = 3;
      return .2f;
    default:
      SET(ST_NORMAL);
      return -1;
  }
}
static bool intro_summit;   /* the jump into a room after a summit launch (7A's ascents: PreviousState StSummitLaunch) */
static float co_intro_jump(void) {
  switch (P->co_step) {
    case 0:
      intro_start = v2(E->x, E->y);
      intro_summit = P->prev_state == ST_SUMMITLAUNCH;
      E->depth = D_TOP;
      ents_mark_unsorted();
      P->facing = 1;
      if (intro_summit) {   /* up from the room's bottom to 24 px above it, on the 8 px grid */
        intro_start.y = g_level.room->y + g_level.room->h - 24.f;
        actor_move_to_x(E, roundf(E->x / 8) * 8, NULL);
        P->co_step = 2;
        goto rise;
      }
      E->y = g_level.room->y + g_level.room->h + 16.f;
      P->co_step = 1;
      return .5f;
    case 1:
      play(A_player_jumpSlow);
      P->co_step = 2;
      /* fall through */
    case 2:
    rise:
      if (E->y > intro_start.y - 8) {
        E->y += -120 * DT;
        return 0;
      }
      E->y = roundf(E->y);
      P->speed.y = -100;
      P->co_step = 3;
      /* fall through */
    case 3:
      if (P->speed.y < 0) {
        P->speed.y += DT * 800;
        return 0;
      }
      P->speed.y = 0;
      if (intro_summit) {
        P->co_step = 30;
        return .2f;
      }
      P->co_step = 4;
      return .1f;
    case 30:   /* (the area start sound is not played) */
      play(A_player_launchRecover);
      P->co_step = 4;
      return .1f;
    case 4:
      if (!intro_summit) play(A_player_fallSlow);
      P->co_step = 5;
      /* fall through */
    case 5:
      if (!P->on_ground) {
        P->speed.y += DT * 800;
        return 0;
      }
      if (!intro_summit) E->x = intro_start.x, E->y = intro_start.y;
      E->depth = D_PLAYER;
      ents_mark_unsorted();
      level_dir_shake(v2(0, 1), .3f);
      if (intro_summit) {
        V2 at = v2(E->x, E->y);
        particles_emit(PL_MID, &P_Player_P_SummitLandA, 12, at, v2(3, 0), -PI_F / 2);
        particles_emit(PL_MID, &P_Player_P_SummitLandB, 8, v2(at.x - 2, at.y), v2(2, 0), 3.403392f);
        particles_emit(PL_MID, &P_Player_P_SummitLandB, 8, v2(at.x + 2, at.y), v2(2, 0), -PI_F / 12);
        particles_emit(PL_BG, &P_Player_P_SummitLandC, 30, at, v2(5, 0), P_Player_P_SummitLandC.direction);
        P->co_step = 6;
        return .35f;
      }
      SET(ST_NORMAL);
      return -1;
    case 6:   /* (the hair settles by itself) */
      SET(ST_NORMAL);
      return -1;
  }
  return -1;
}
static float co_intro_wakeup(void) {
  switch (P->co_step) {
    case 0:
      play(A_player_asleep);
      P->co_step = 1;
      return .5f;
    case 1:
      play_r(A_player_wakeUp);
      P->co_step = 2;
      /* fall through */
    case 2:
      if (P->spr.anim == A_player_wakeUp) return 0;
      P->co_step = 3;
      return .2f;
    default:
      SET(ST_NORMAL);
      return -1;
  }
}

static void intro_respawn_begin(void) {
  E->depth = D_TOP;
  ents_mark_unsorted();
  P->intro_ease = 1;
  Room *rm = g_level.room;
  V2 from = v2(E->x, E->y);
  from.x = clampf(from.x, rm->x + 40.f, rm->x + rm->w - 40.f);
  from.y = clampf(from.y, rm->y + 40.f, rm->y + rm->h - 40.f);
  P->dead_offset = from;
  P->respawn_from = v2sub(from, v2(E->x, E->y));
  P->respawn_t = 0;
  P->respawn_tween_on = 1;
}
static void intro_respawn_end(void) {
  E->depth = D_PLAYER;
  ents_mark_unsorted();
  P->dead_offset = v2(0, 0);
  P->respawn_tween_on = 0;
}
static void respawn_tween(void) {
  if (!P->respawn_tween_on) return;
  P->respawn_t += DT;
  float t = fminf(P->respawn_t / .6f, 1);
  P->dead_offset = v2mul(P->respawn_from, 1 - t);
  P->intro_ease = 1 - t;
  if (t >= 1) {
    P->respawn_tween_on = 0;
    if (P->state == ST_INTRORESPAWN) {
      SET(ST_NORMAL);
      P->spr.sx = 1.5f, P->spr.sy = .5f;
    }
  }
}

/* ---------------------------------------------------------------- the sprite */
static bool sprite_running(void) {
  int a = last_anim();
  return a == A_player_flip || a == A_player_runSlow || a == A_player_runFast || a == A_player_runStumble ||
         a == A_player_runWind || a == A_player_runSlow_carry;
}
static bool sprite_dream_dashing(void) {
  int a = last_anim();
  return a == A_player_dreamDashIn || a == A_player_dreamDashLoop || a == A_player_dreamDashOut;
}
static bool anim_idle_family(void) {
  int a = P->spr.anim;
  return a == A_player_idle || a == A_player_idleA || a == A_player_idleB || a == A_player_idleC || a == A_player_idle_carry;
}

static void update_sprite(void) {
  P->spr.sx = approach(P->spr.sx, 1, 1.75f * DT);
  P->spr.sy = approach(P->spr.sy, 1, 1.75f * DT);
  if (player_in_control(P) && P->spr.anim != A_player_throw && P->state != ST_TEMPLEFALL && P->state != ST_REFLECTIONFALL &&
      P->state != ST_STARFLY && P->state != ST_CASSETTEFLY) {
    if (P->state == ST_ATTRACT) play(A_player_fallFast);
    else if (P->state == ST_SUMMITLAUNCH) play(A_player_launch);
    else if (P->state == ST_PICKUP) play(A_player_pickUp);
    else if (P->state == ST_SWIM) {
      if (g_in.move_y > 0) play(A_player_swimDown);
      else if (g_in.move_y < 0) play(A_player_swimUp);
      else play(A_player_swimIdle);
    } else if (P->state == ST_DREAMDASH) {
      if (P->spr.anim != A_player_dreamDashIn && P->spr.anim != A_player_dreamDashLoop) play(A_player_dreamDashIn);
    } else if (sprite_dream_dashing() && last_anim() != A_player_dreamDashOut) {
      play(A_player_dreamDashOut);
    } else if (P->spr.anim != A_player_dreamDashOut) {
      if (player_dash_attacking(P)) {
        if (P->on_ground && P->dash_dir.y == 0 && !Ducking && P->speed.x != 0 && P->move_x == -signf(P->speed.x)) {
          if (level_on_interval(.02f)) dust_burst(v2(E->x, E->y), -PI_F / 2, 1);
          play(A_player_skid);
        } else
          play(A_player_dash);
      } else if (P->state == ST_CLIMB) {
        if (P->last_climb_move < 0) play(A_player_climbup);
        else if (P->last_climb_move > 0) play(A_player_wallslide);
        else if (!solid_at((float)P->facing, 6)) play(A_player_dangling);
        else if (g_in.move_x == -P->facing) {
          if (P->spr.anim != A_player_climbLookBack) play(A_player_climbLookBackStart);
        } else
          play(A_player_wallslide);
      } else if (Ducking && P->state == ST_NORMAL) {
        play(A_player_duck);
      } else if (P->on_ground) {
        P->fast_jump = false;
        if (!P->holding && P->move_x != 0 && solid_at((float)P->move_x, 0)) play(A_player_push);
        else if (fabsf(P->speed.x) <= RunAccel / 40 && P->move_x == 0) {
          if (P->holding) play(A_player_idle_carry);
          else if (!rect_solid(E->x + P->facing * 1, E->y + 2, E->x + P->facing * 1 + 1, E->y + 3) &&
                   !rect_solid(E->x + P->facing * 4, E->y + 2, E->x + P->facing * 4 + 1, E->y + 3) &&
                   !collide_jumpthru(E, E->x + P->facing * 4, E->y + 2))
            play(A_player_edge);
          else if (!rect_solid(E->x - P->facing * 1, E->y + 2, E->x - P->facing * 1 + 1, E->y + 3) &&
                   !rect_solid(E->x - P->facing * 4, E->y + 2, E->x - P->facing * 4 + 1, E->y + 3) &&
                   !collide_jumpthru(E, E->x - P->facing * 4, E->y + 2))
            play(A_player_edgeBack);
          else if (g_in.move_y == -1) {
            if (last_anim() != A_player_lookUp) play(A_player_lookUp);
          } else if (P->spr.anim != 255 && !anim_idle_family())
            play(A_player_idle);
        } else if (P->holding)
          play(A_player_runSlow_carry);
        else if (signf(P->speed.x) == -P->move_x && P->move_x != 0) {
          if (fabsf(P->speed.x) > MaxRun) play(A_player_skid);
          else if (P->spr.anim != A_player_skid) play(A_player_flip);
        } else if (P->wind_dir.x != 0 && P->wind_timeout > 0 && P->facing == -signf(P->wind_dir.x))
          play(A_player_runWind);
        else if (!sprite_running()) {
          if (fabsf(P->speed.x) < MaxRun * .5f) play(A_player_runSlow);
          else play(A_player_runFast);
        }
      } else if (P->wall_slide_dir != 0 && !P->holding) {
        play(A_player_wallslide);
      } else if (P->speed.y < 0) {
        if (P->holding) play(A_player_jumpSlow_carry);
        else if (P->fast_jump || fabsf(P->speed.x) > MaxRun) {
          P->fast_jump = true;
          play(A_player_jumpFast);
        } else
          play(A_player_jumpSlow);
      } else {
        if (P->holding) play(A_player_fallSlow_carry);
        else if (P->fast_jump || P->speed.y >= MaxFall || g_level.in_space) {
          P->fast_jump = true;
          if (last_anim() != A_player_fallFast) play(A_player_fallFast);
        } else
          play(A_player_fallSlow);
      }
    }
  }
  if (P->state != ST_DUMMY) P->spr.rate = g_level.in_space ? .5f : 1;
}

/* the idle animations' random variants (Sprite.OnLastFrame) */
static void sprite_advance(void) {
  int before = P->spr.anim, frame = P->spr.frame;
  spr_update(&P->spr);
  /* OnLastFrame of "idle": the anim looped back */
  if (before == A_player_idle && P->spr.anim == A_player_idle && P->spr.frame == 0 && frame != 0 && !g_level.in_cutscene &&
      P->idle_timer > 3) {
    if (rndf() < 0.2f) {
      float r = rndf();
      int next;
      if (!P->no_backpack) {
        if (g_level.core_mode == 1) next = r * 8 < 5 ? A_player_idleA : A_player_idleB;
        else next = r * 9 < 5 ? A_player_idleA : r * 9 < 8 ? A_player_idleB : A_player_idleC;
      } else
        next = r * 7 < 1 ? A_player_idleA : r * 7 < 4 ? A_player_idleB : A_player_idleC;
      play(next);
    }
  }
}

/* PlayerSprite metadata: hair offset and frame */
void hair_meta(uint16_t tex, int *hx, int *hy, int *frame, bool *has, int *carry) {
  const uint8_t *s = section(SEC_PMETA);
  int n = rd16(s), lo = 0, hi = n - 1;
  *hx = 0, *hy = 0, *frame = 0, *has = true, *carry = 0;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    const uint8_t *r = s + 4 + 6 * mid;
    uint16_t t = rd16(r);
    if (t == tex) {
      *hx = (int8_t)r[2], *hy = (int8_t)r[3], *frame = r[4] & 0x7F, *has = r[4] & 0x80, *carry = (int8_t)r[5];
      return;
    }
    if (t < tex) lo = mid + 1;
    else hi = mid - 1;
  }
}

/* PlayerHair.AfterUpdate */
static void hair_after_update(void) {
  int hx, hy, fr, carry;
  bool has;
  hair_meta(spr_tex(&P->spr), &hx, &hy, &fr, &has, &carry);
  V2 off = v2((float)hx * P->facing, (float)hy);
  P->hair[0] = v2add(v2(E->x, E->y - 9 * P->spr.sy), off);
  V2 target = v2add(P->hair[0], v2(-P->facing * P->hair_step_facing * 2, sinf(P->hair_wave) * P->hair_step_ysine));
  target = v2add(target, P->hair_step);
  V2 prev = P->hair[0];
  for (int i = 1; i < P->hair_count && i < HAIR_NODES; i++) {
    if (P->hair_simulate) {
      float m = (1 - (float)i / P->hair_count * 0.5f) * P->hair_step_approach;
      V2 d = v2sub(target, P->hair[i]);
      float l = v2len(d), mv = m * DT;
      P->hair[i] = l <= mv ? target : v2add(P->hair[i], v2mul(d, mv / l));
    }
    V2 d = v2sub(P->hair[i], prev);
    if (v2len(d) > 3) P->hair[i] = v2add(prev, v2mul(v2norm(d), 3));
    target = v2add(P->hair[i], v2(-P->facing * P->hair_step_facing, sinf(P->hair_wave + i * 0.8f) * P->hair_step_ysine));
    target = v2add(target, P->hair_step);
    prev = P->hair[i];
  }
}

static void update_hair(bool gravity) {
  if (P->state == ST_STARFLY) {
    hair_rgb = P->spr.tint == rgb(StarFlyColor) ? StarFlyColor : NormalHairColor;
    gravity = false;
  } else if (P->dashes == 0 && P->dashes < player_max_dashes(P)) {
    hair_rgb = lerp_color(hair_rgb, UsedHairColor, 6 * DT);
  } else {
    uint32_t c;
    if (P->last_dashes != P->dashes) {
      c = FlashHairColor;
      P->hair_flash_timer = .12f;
    } else if (P->hair_flash_timer > 0) {
      c = FlashHairColor;
      P->hair_flash_timer -= DT;
    } else if (P->dashes == 2)
      c = TwoDashesHairColor;
    else
      c = NormalHairColor;
    hair_rgb = c;
  }
  P->hair_simulate = gravity;
  P->last_dashes = P->dashes;
}

/* ---------------------------------------------------------------- the update */
static void trigger_check(void);

static void player_update(Ent *e) {
  E = e;
  Player *p = P;
  if (p->body) {
    body_update();
    if (p->body_step >= 4) return;
    spr_update(&p->spr);
    return;
  }
  p->prev_pos = v2(E->x, E->y);
  p->strawb_reset_timer -= DT;
  if (p->strawb_reset_timer <= 0) p->strawb_index = 0;
  p->idle_timer += DT;
  if (g_level.in_cutscene) p->idle_timer = -5;
  else if (p->speed.x != 0 || p->speed.y != 0) p->idle_timer = 0;
  if (p->just_respawned && (p->speed.x != 0 || p->speed.y != 0)) p->just_respawned = false;
  if (p->state == ST_DREAMDASH) p->on_ground = p->on_safe_ground = false;
  else if (p->speed.y >= 0) {
    Ent *first = collide_first_solid(E, E->x, E->y + 1);
    if (!first) first = collide_first_jumpthru_outside(E, E->x, E->y + 1);
    if (first) {
      p->on_ground = true;
      p->on_safe_ground = first->safe;
    } else
      p->on_ground = p->on_safe_ground = false;
  } else
    p->on_ground = p->on_safe_ground = false;
  if (p->state == ST_SWIM) p->on_safe_ground = true;
  p->play_footstep_on_land -= DT;
  if (p->on_ground) p->highest_air_y = E->y;
  else p->highest_air_y = fminf(E->y, p->highest_air_y);
  if (level_on_interval(.05f)) p->flash = !p->flash;
  if (p->wall_slide_dir != 0) {
    p->wall_slide_timer = fmaxf(p->wall_slide_timer - DT, 0);
    p->wall_slide_dir = 0;
  }
  if (p->wall_boost_timer > 0) {
    p->wall_boost_timer -= DT;
    if (p->move_x == p->wall_boost_dir) {
      p->speed.x = WallJumpHSpeed * p->move_x;
      p->stamina += ClimbJumpCost;
      p->wall_boost_timer = 0;
      spr_play(&p->sweat, A_player_sweat_idle, false);
    }
  }
  if (p->on_ground && p->state != ST_CLIMB) {
    p->auto_jump = false;
    p->stamina = ClimbMaxStamina;
    p->wall_slide_timer = WallSlideTime;
  }
  if (p->dash_attack_timer > 0) p->dash_attack_timer -= DT;
  if (p->on_ground) {
    p->dream_jump = false;
    p->jump_grace_timer = JumpGraceTime;
  } else if (p->jump_grace_timer > 0)
    p->jump_grace_timer -= DT;
  if (p->dash_cooldown_timer > 0) p->dash_cooldown_timer -= DT;
  if (p->dash_refill_cooldown_timer > 0) p->dash_refill_cooldown_timer -= DT;
  else if (!p->inventory_norefills) {
    if (p->state == ST_SWIM) player_refill_dash(p);
    else if (p->on_ground)
      if (solid_at(0, 1) || jumpthru_outside_at(0, 1))
        if (!collide_first_kind(E, E->x, E->y, KIND_SPIKES)) player_refill_dash(p);
  }
  if (p->var_jump_timer > 0) p->var_jump_timer -= DT;
  if (p->auto_jump_timer > 0) {
    if (p->auto_jump) {
      p->auto_jump_timer -= DT;
      if (p->auto_jump_timer <= 0) p->auto_jump = false;
    } else
      p->auto_jump_timer = 0;
  }
  if (p->force_move_x_timer > 0) {
    p->force_move_x_timer -= DT;
    p->move_x = p->force_move_x;
  } else {
    p->move_x = g_in.move_x;
    p->climb_hop_solid = NULL;
  }
  if (p->climb_hop_solid && !p->climb_hop_solid->collidable) p->climb_hop_solid = NULL;
  else if (p->climb_hop_solid && (p->climb_hop_solid->x != p->climb_hop_solid_pos.x || p->climb_hop_solid->y != p->climb_hop_solid_pos.y)) {
    V2 mv = v2sub(v2(p->climb_hop_solid->x, p->climb_hop_solid->y), p->climb_hop_solid_pos);
    p->climb_hop_solid_pos = v2(p->climb_hop_solid->x, p->climb_hop_solid->y);
    actor_move_h_exact(E, (int)mv.x, NULL, NULL);
    actor_move_v_exact(E, (int)mv.y, NULL, NULL);
  }
  if (p->no_wind_timer > 0) p->no_wind_timer -= DT;
  if (p->move_x != 0 && player_in_control(p) && p->state != ST_CLIMB && p->state != ST_PICKUP && p->state != ST_REDDASH &&
      p->state != ST_HITSQUASH) {
    if (p->move_x != p->facing && Ducking) p->spr.sx = .8f, p->spr.sy = 1.2f;
    p->facing = p->move_x;
  }
  p->last_aim = aim_vector();
  if (p->wall_speed_retention_timer > 0) {
    if (signf(p->speed.x) == -signf(p->wall_speed_retained)) p->wall_speed_retention_timer = 0;
    else if (!solid_at(signf(p->wall_speed_retained), 0)) {
      p->speed.x = p->wall_speed_retained;
      p->wall_speed_retention_timer = 0;
    } else
      p->wall_speed_retention_timer -= DT;
  }
  if (p->hop_wait_x != 0) {
    if (signf(p->speed.x) == -p->hop_wait_x || p->speed.y > 0) p->hop_wait_x = 0;
    else if (!solid_at((float)p->hop_wait_x, 0)) {
      p->speed.x = p->hop_wait_x_speed;
      p->hop_wait_x = 0;
    }
  }
  if (p->wind_timeout > 0) p->wind_timeout -= DT;
  {
    V2 wd = p->wind_dir;
    if (p->wind_timeout > 0 && wd.x != 0) {
      p->wind_hair_timer += DT * 8;
      p->hair_step = v2(wd.x * 5, sinf(p->wind_hair_timer));
      p->hair_step_facing = 0;
      p->hair_step_approach = 128;
      p->hair_step_ysine = 0;
    } else if (p->dashes > 1) {
      p->hair_step = v2(sinf(g_level.time_active * 2) * 0.7f - p->facing * 3, sinf(g_level.time_active * 1));
      p->hair_step_facing = 0;
      p->hair_step_approach = 90;
      p->hair_step_ysine = 1;
      p->hair_step.y += wd.y * 2;
    } else {
      p->hair_step = v2(0, 2);
      p->hair_step_facing = 0.5f;
      p->hair_step_approach = 64;
      p->hair_step_ysine = 0;
      p->hair_step.y += wd.y * 0.5f;
    }
  }
  if (p->state == ST_REDDASH) p->hair_count = 1;
  else if (p->state != ST_STARFLY) p->hair_count = p->dashes > 1 ? 5 : p->start_hair_count;
  if (p->min_hold_timer > 0) p->min_hold_timer -= DT;
  if (p->launched) {
    float sq = p->speed.x * p->speed.x + p->speed.y * p->speed.y;
    if (sq < 140 * 140) p->launched = false;
    else {
      p->launched_timer += DT;
      if (p->launched_timer >= .5f) p->launched = false, p->launched_timer = 0;
    }
  } else
    p->launched_timer = 0;
  p->was_tired = is_tired();

  /* base.Update(): components (hair wave, sprite, sweat, state machine), then the actor's lift */
  p->hair_wave += DT * 4;
  sprite_advance();
  spr_update(&p->sweat);
  respawn_tween();
  if (UPD[p->state]) {
    int next = UPD[p->state]();
    player_set_state(p, next);
  }
  co_update();
  if (p->bird_trails) {   /* BirdDashTutorial's two alarms */
    p->bird_trail_t += DT;
    if (p->bird_trails == 2 && p->bird_trail_t >= 0.08f) create_trail(), p->bird_trails = 1;
    if (p->bird_trails == 1 && p->bird_trail_t >= 0.15f) create_trail(), p->bird_trails = 0;
  }
  leader_update(v2(E->x, E->y - 8));   /* the Leader component, at (0, -8) */
  actor_update_lift(E);

  if (!p->on_ground && p->speed.y <= 0 && (p->state != ST_CLIMB || p->last_climb_move == -1) && collide_jumpthru(E, E->x, E->y) &&
      !ledge_blocked(E->x, E->y - 2))   /* (JumpThruBoostBlockedCheck) */
    actor_move_v(E, JumpThruAssistSpeed * DT, NULL, NULL);
  if (!p->on_ground && player_dash_attacking(p) && p->dash_dir.y == 0)
    if (solid_at(0, DashVFloorSnapDist) || jumpthru_outside_at(0, DashVFloorSnapDist)) actor_move_v_exact(E, DashVFloorSnapDist, NULL, NULL);
  if (p->speed.y > 0 && can_unduck() && !box_is(HB_STARFLY) && !p->on_ground) set_ducking(false);
  if (p->state != ST_DREAMDASH && p->state != ST_ATTRACT) actor_move_h(E, p->speed.x * DT, on_collide_h, NULL);
  if (p->state != ST_DREAMDASH && p->state != ST_ATTRACT) actor_move_v(E, p->speed.y * DT, on_collide_v, NULL);
  if (p->dead) return;
  if (p->state == ST_SWIM) {
    if (p->speed.y < 0 && p->speed.y >= SwimMaxRise)
      while (!swim_check()) {
        p->speed.y = 0;
        if (actor_move_v_exact(E, 1, NULL, NULL)) break;
      }
  } else if (p->state == ST_NORMAL && swim_check())
    SET(ST_SWIM);
  else if (p->state == ST_CLIMB && swim_check()) {
    Ent *w = collide_first_kind(E, E->x, E->y, KIND_WATER);
    if (w && e_cym(E) < e_cym(w)) {
      while (swim_check())
        if (actor_move_v_exact(E, -1, NULL, NULL)) break;
      if (swim_check()) SET(ST_SWIM);
    } else
      SET(ST_SWIM);
  }
  update_sprite();
  if (p->holding) {   /* UpdateCarry */
    int hx, hy, fr, carry;
    bool has;
    hair_meta(spr_tex(&p->spr), &hx, &hy, &fr, &has, &carry);
    holdable_carry(p->holding, v2(E->x + p->carry_offset.x, E->y + p->carry_offset.y + carry));
  }
  if (p->state != ST_REFLECTIONFALL) trigger_check();
  p->strawberries_blocked = collide_first_kind(E, E->x, E->y, KIND_BLOCKFIELD) != NULL;
  if (player_in_control(p) || p->force_camera_update) {
    V2 from = g_level.cam, target = player_camera_target(p);
    float mult = p->state == ST_TEMPLEFALL ? 8 : 1.f;
    float k = 1 - powf(0.01f / mult, DT);
    g_level.cam = v2add(from, v2mul(v2sub(target, from), k));
  }
  if (!p->dead && p->state != ST_CASSETTEFLY) {
    float was[4] = {E->cw, E->ch, E->cx, E->cy};
    set_box(hurtbox);
    for (int i = 0; i < g_nents && !p->dead; i++) {
      Ent *o = &g_ents[i];
      if (!(o->kind & KIND_PCOLLIDE) || !o->cls || o->dead == 1 || !o->collidable || !o->cls->on_player) continue;
      if (collide_ent_at(E, E->x, E->y, o)) o->cls->on_player(o, p);
    }
    if (p->dead) return;
    if (box_is(hurtbox)) ent_box(E, was[0], was[1], was[2], was[3]);
  }
  if (player_in_control(p) && !p->dead && p->state != ST_DREAMDASH) level_enforce_bounds(p);
  update_hair(true);
  if (p->was_ducking != Ducking) p->was_ducking = Ducking;
  p->was_on_ground = p->on_ground;
}

static void trigger_check(void) {
  for (int i = 0; i < g_nents; i++) {
    Ent *t = &g_ents[i];
    if (!(t->kind & KIND_TRIGGER) || !t->cls || t->dead == 1 || !t->collidable) continue;
    if (collide_ent_at(E, E->x, E->y, t)) {
      if (!t->triggered) {
        t->triggered = 1;
        if (MORE(t->cls)->on_enter) MORE(t->cls)->on_enter(t, P);
      }
      if (MORE(t->cls)->on_stay) MORE(t->cls)->on_stay(t, P);
    } else if (t->triggered) {
      t->triggered = 0;
      if (MORE(t->cls)->on_leave) MORE(t->cls)->on_leave(t, P);
    }
  }
}

void player_after_update(Player *p) {
  if (!p->ent || !p->ent->cls) return;
  E = p->ent;
  if (!p->body || p->body_step < 4) hair_after_update();
  trail_snapshots();
}

/* ---------------------------------------------------------------- camera */
V2 player_camera_target(Player *p) {
  Ent *e = p->ent;
  Room *rm = g_level.room;
  V2 t = v2(e->x - 160, e->y - 90);
  if (p->state != ST_REFLECTIONFALL) t = v2add(t, g_level.cam_offset);
  if (p->state == ST_STARFLY) t.x += .2f * p->speed.x, t.y += .2f * p->speed.y;
  else if (p->state == ST_REDDASH) t.x += 48 * signf(p->speed.x), t.y += 48 * signf(p->speed.y);
  else if (p->state == ST_SUMMITLAUNCH) t.y -= 64;
  else if (p->state == ST_REFLECTIONFALL) t.y += 32;
  if (v2len(p->cam_anchor_lerp) > 0) {
    if (p->cam_anchor_ignore_x && !p->cam_anchor_ignore_y) t.y = lerpf(t.y, p->cam_anchor.y, p->cam_anchor_lerp.y);
    else if (!p->cam_anchor_ignore_x && p->cam_anchor_ignore_y) t.x = lerpf(t.x, p->cam_anchor.x, p->cam_anchor_lerp.x);
    else if (p->cam_anchor_lerp.x == p->cam_anchor_lerp.y) {
      t.x = lerpf(t.x, p->cam_anchor.x, p->cam_anchor_lerp.x);
      t.y = lerpf(t.y, p->cam_anchor.y, p->cam_anchor_lerp.x);
    } else {
      t.x = lerpf(t.x, p->cam_anchor.x, p->cam_anchor_lerp.x);
      t.y = lerpf(t.y, p->cam_anchor.y, p->cam_anchor_lerp.y);
    }
  }
  V2 at;
  at.x = clampf(t.x, (float)rm->x, (float)(rm->x + rm->w - 320));
  at.y = clampf(t.y, (float)rm->y, (float)(rm->y + rm->h - 180));
  int lock = g_level.cam_lock;
  Ent *lk = g_level.cam_locker;
  if (lock && lk && (lk->dead == 1 || !lk->cls)) lk = NULL;
  if (lock) {
    if (lock != 1) {
      at.x = fmaxf(at.x, g_level.cam.x);
      if (lk) at.x = fminf(at.x, fmaxf((float)rm->x, lk->x - g_level.cam_lock_dx));
    }
    if (lock == 2) {
      at.y = fmaxf(at.y, g_level.cam.y);
      if (lk) at.y = fminf(at.y, fmaxf((float)rm->y, lk->y - g_level.cam_lock_dy));
    } else if (lock == 1) {
      g_level.cam_upward_max_y = fminf(g_level.cam.y + 180, g_level.cam_upward_max_y);
      at.y = fminf(at.y, g_level.cam_upward_max_y);
      if (lk) at.y = fmaxf(at.y, fminf((float)(rm->y + rm->h - 180), lk->y - g_level.cam_lock_dy));
    }
  }
  for (int i = 0; i < g_nents; i++) {
    Ent *k = &g_ents[i];
    if ((k->kind & KIND_KILLBOX) && k->cls && k->collidable && e_top(e) < e_bottom(k) && e_right(e) > e_left(k) && e_left(e) < e_right(k))
      at.y = fminf(at.y, e_top(k) - 180);
  }
  return at;
}

/* ---------------------------------------------------------------- transitions */
bool player_transition_to(Player *p, V2 target) {
  E = p->ent;
  actor_move_towards_x(E, target.x, 60 * DT, NULL);
  actor_move_towards_y(E, target.y, 60 * DT, NULL);
  update_hair(false);
  hair_after_update();
  if (E->x == target.x && E->y == target.y) {
    E->remx = E->remy = 0;
    p->speed.x = (float)roundi(p->speed.x);
    p->speed.y = (float)roundi(p->speed.y);
    return true;
  }
  return false;
}
void player_on_transition(Player *p) {
  p->wall_slide_timer = WallSlideTime;
  p->jump_grace_timer = 0;
  p->force_move_x_timer = 0;
  player_refill_dash(p);
  player_refill_stamina(p);
  leader_transfer();
}
void player_before_up_transition(Player *p) {
  p->speed.x = 0;
  if (p->state != ST_REDDASH && p->state != ST_REFLECTIONFALL && p->state != ST_STARFLY) {
    p->var_jump_speed = p->speed.y = JumpSpeed;
    player_set_state(p, p->state == ST_SUMMITLAUNCH ? ST_INTROJUMP : ST_NORMAL);
    p->auto_jump = true;
    p->auto_jump_timer = 0;
    p->var_jump_timer = VarJumpTime;
  }
  p->dash_cooldown_timer = 0.2f;
}
void player_before_down_transition(Player *p) {
  if (p->state != ST_REDDASH && p->state != ST_REFLECTIONFALL && p->state != ST_STARFLY) {
    player_set_state(p, ST_NORMAL);
    p->speed.y = fmaxf(0, p->speed.y);
    p->auto_jump = false;
    p->var_jump_timer = 0;
  }
}

/* ---------------------------------------------------------------- rendering */
/* PlayerHair.Render: a gfx_custom layer (as affine commands, its scaled nodes and their outlines took up to 25 of the
 * frame's 48: in a busy frame the colors were left out, over black outlines) */
static void hair_strip(uint16_t *strip, int sy0, int sy1, void *ctx) {
  (void)ctx;
  int hx, hy, fr, carry;
  bool has;
  hair_meta(spr_tex(&P->spr), &hx, &hy, &fr, &has, &carry);
  uint16_t col = rgb(hair_rgb);
  uint16_t bang_tex[3] = {T_characters_player_bangs00, T_characters_player_bangs01, T_characters_player_bangs02};
  int n = P->hair_count;
  for (int i = 0; i < n; i++) {
    uint16_t t = i == 0 ? bang_tex[fr < 3 ? fr : 0] : T_characters_player_hair00;
    float num = 0.25f + (1 - (float)i / n) * 0.75f;
    float sx = (i == 0 ? (float)P->facing : num) * fabsf(P->spr.sx), sy = num;
    V2 at = P->hair[i];
    blit_tex_ex(strip, sy0, sy1, t, at.x - 1, at.y, 5, 5, sx, sy, 0, 0, 255, GF_SILHOUETTE);
    blit_tex_ex(strip, sy0, sy1, t, at.x + 1, at.y, 5, 5, sx, sy, 0, 0, 255, GF_SILHOUETTE);
    blit_tex_ex(strip, sy0, sy1, t, at.x, at.y - 1, 5, 5, sx, sy, 0, 0, 255, GF_SILHOUETTE);
    blit_tex_ex(strip, sy0, sy1, t, at.x, at.y + 1, 5, 5, sx, sy, 0, 0, 255, GF_SILHOUETTE);
  }
  for (int i = n - 1; i >= 0; i--) {
    uint16_t t = i == 0 ? bang_tex[fr < 3 ? fr : 0] : T_characters_player_hair00;
    float num = 0.25f + (1 - (float)i / n) * 0.75f;
    float sx = (i == 0 ? (float)P->facing : num) * fabsf(P->spr.sx), sy = num;
    blit_tex_ex(strip, sy0, sy1, t, P->hair[i].x, P->hair[i].y, 5, 5, sx, sy, 0, col, 255, GF_SILHOUETTE);
  }
}
static void hair_render(void) {
  int hx, hy, fr, carry;
  bool has;
  hair_meta(spr_tex(&P->spr), &hx, &hy, &fr, &has, &carry);
  if (!has) return;
  P->hair[0] = v2(floorf(P->hair[0].x), floorf(P->hair[0].y));
  Tex t;
  tex_get(fr == 1 ? T_characters_player_bangs01 : fr == 2 ? T_characters_player_bangs02 : T_characters_player_bangs00, &t);
  tex_get(T_characters_player_hair00, &t);   /* (loaded now) */
  float y0 = P->hair[0].y, y1 = y0;
  for (int i = 1; i < P->hair_count; i++) y0 = fminf(y0, P->hair[i].y), y1 = fmaxf(y1, P->hair[i].y);
  gfx_custom(hair_strip, NULL, (int)floorf(y0) - 8 - g_camy, (int)ceilf(y1) + 9 - g_camy);
}

static void player_render(Ent *e) {
  E = e;
  Player *p = P;
  float rx = floorf(e->x), ry = floorf(e->y);
  if (p->body) {
    if (p->body_step < 4) {
      Sprite s = p->spr;
      s.sx = p->body_scale * p->facing, s.sy = p->body_scale;
      spr_draw(&s, rx, ry);
    } else if (p->death_effect >= 0 && p->death_effect <= 1)
      death_effect_draw(v2add(v2(rx, ry), v2(0, -6)), p->death_color, p->death_effect);
    return;
  }
  if (p->state == ST_INTRORESPAWN) {
    death_effect_draw(v2add(center(), p->dead_offset), rgb(hair_rgb), p->intro_ease);
    return;
  }
  if (p->state != ST_STARFLY) p->spr.tint = (is_tired() && p->flash) ? 0xF800 : 0xFFFF;
  hair_render();
  Sprite s = p->spr;
  s.sx *= p->facing;
  spr_draw(&s, rx, ry + p->spr_dy);
  if (P->sweat.visible && (P->state == ST_CLIMB || spr_is(&P->sweat, A_player_sweat_jump))) {
    Sprite w = p->sweat;
    w.sx = p->facing;
    spr_draw(&w, rx, ry - 8);
  }
}


/* ---------------------------------------------------------------- spawning */
static const EntClass PLAYER_CLASS = {.name = "player", .size = sizeof(ActorExt), .update = player_update,
                                      .render = player_render, .kind = KIND_ACTOR | KIND_PLAYER,
                                      .more = &(const EntMore){.on_squish = on_squish,
                                                               .is_riding_solid = is_riding_solid,
                                                               .is_riding_jumpthru = is_riding_jumpthru}};

void player_spawn(V2 at, int intro) {
  Player *p = P;
  memset(p, 0, sizeof *p);
  p->golden_room = -1;
  leader_reset();
  Ent *e = ent_new(&PLAYER_CLASS, (float)(int)at.x, (float)(int)at.y);
  E = e;
  p->ent = e;
  e->depth = D_PLAYER;
  e->tags = TAG_PERSISTENT;   /* (between rooms, only TransitionTo moves her) */
  e->dead = 0;
  p->no_backpack = !g_session.backpack;
  spr_init(&p->spr, p->no_backpack ? SB_player_no_backpack : SB_player);
  spr_init(&p->sweat, SB_player_sweat);
  p->hair_count = p->start_hair_count = 4;
  hair_rgb = NormalHairColor;
  p->hair_step = v2(0, 2);
  p->hair_step_facing = 0.5f;
  p->hair_step_approach = 64;
  p->hair_simulate = true;
  p->inventory_dashes = g_session.inv_dashes;
  p->inventory_dreamdash = g_session.dreamdash;
  set_box(HB_NORMAL);
  hurtbox = HB_NORMAL_HURT;
  p->last_aim = v2(1, 0);
  p->facing = 1;
  p->stamina = ClimbMaxStamina;
  p->wall_slide_timer = WallSlideTime;
  p->max_fall = MaxFall;
  p->dummy_gravity = p->dummy_friction = p->dummy_maxspeed = p->dummy_auto_animate = true;
  p->state = -1;
  p->prev_state = -1;
  p->dashes = p->last_dashes = player_max_dashes(p);
  p->intro = intro;
  if (e->x > g_level.room->x + g_level.room->w / 2 && intro != INTRO_NONE) p->facing = -1;
  switch (intro) {
    case INTRO_RESPAWN: player_set_state(p, ST_INTRORESPAWN), p->just_respawned = true; break;
    case INTRO_WALKINRIGHT: case INTRO_WALKINLEFT: player_set_state(p, ST_INTROWALK); break;
    case INTRO_JUMP: player_set_state(p, ST_INTROJUMP); break;
    case INTRO_WAKEUP: play(A_player_asleep), p->facing = 1, player_set_state(p, ST_INTROWAKEUP); break;
    case INTRO_FALL: player_set_state(p, ST_REFLECTIONFALL); break;
    case INTRO_TEMPLEMIRRORVOID:   /* StartTempleMirrorVoidSleep */
      play(A_player_asleep), p->facing = 1, player_set_state(p, ST_DUMMY), p->state_locked = true;
      p->dummy_auto_animate = p->dummy_gravity = false;
      break;
    default: player_set_state(p, ST_NORMAL); break;
  }
  if (p->state < 0) player_set_state(p, ST_NORMAL);
  p->intro = intro;
  /* PlayerHair.Start */
  for (int i = 0; i < HAIR_NODES; i++) p->hair[i] = v2(e->x - p->facing * 200, e->y + 200);
  update_hair(true);
  hair_after_update();
  p->prev_pos = v2(e->x, e->y);
}

V2 player_spawn_near(V2 at) { return level_closest_spawn(at); }

/* ---------------------------------------------------------------- cutscene walks */
/* DummyWalkTo, DummyWalkToExact and DummyRunTo, an update at a time */
bool player_walk(Player *p, Walk *w) {
  Ent *e = p->ent;
  E = e;
  if (w->phase == 0) {
    player_set_state(p, ST_DUMMY);
    w->phase = 1;
    if (w->mode == WALK_EXACT) {
      if (e->x == w->x) return false;
      p->dummy_moving = true;
      if (w->flags & WALK_BACKWARDS) {
        p->spr.rate = -1;
        p->facing = (int)signf(e->x - w->x);
      } else
        p->facing = (int)signf(w->x - e->x);
      w->last = (int8_t)signf(e->x - w->x);
    } else if (w->mode == RUN_TO) {
      if (fabsf(e->x - w->x) <= 4) return false;
      p->dummy_moving = true;
      if (w->flags & RUN_FAST) spr_play(&p->spr, A_player_runFast, false);
      else if (p->spr.last_goto != A_player_runSlow && p->spr.last_goto != A_player_runFast &&
               p->spr.last_goto != A_player_runStumble && p->spr.last_goto != A_player_runWind)
        spr_play(&p->spr, A_player_runSlow, false);
      p->facing = signf(w->x - e->x) > 0 ? 1 : -1;
    } else {
      if (!(fabsf(e->x - w->x) > 4 && !p->dead)) return false;
      p->dummy_moving = true;
      if (w->flags & WALK_BACKWARDS) {
        p->spr.rate = -1;
        p->facing = signf(e->x - w->x) > 0 ? 1 : -1;
      } else
        p->facing = signf(w->x - e->x) > 0 ? 1 : -1;
    }
  }
  if (w->mode == WALK_EXACT) {
    if (!p->dead && e->x != w->x && !collide_solid(e, e->x + p->facing, e->y) &&
        (!(w->flags & WALK_CANCEL_ON_FALL) || actor_on_ground(e, 1))) {
      p->speed.x = approach(p->speed.x, signf(w->x - e->x) * 64 * w->mult, 1000 * DT);
      int now = (int)signf(e->x - w->x);
      if (now != w->last) e->x = w->x;
      else {
        w->last = (int8_t)now;
        return true;
      }
    }
    p->speed.x = 0;
    p->spr.rate = 1;
    spr_play(&p->spr, A_player_idle, false);
    p->dummy_moving = false;
    return false;
  }
  if (w->mode == RUN_TO) {
    if (fabsf(e->x - w->x) > 4) {
      p->speed.x = approach(p->speed.x, signf(w->x - e->x) * 90, 1000 * DT);
      return true;
    }
    spr_play(&p->spr, A_player_idle, false);
    p->dummy_moving = false;
    return false;
  }
  if (fabsf(w->x - e->x) > 4 && ((w->flags & WALK_INTO_WALLS) || !collide_solid(e, e->x + signf(w->x - e->x), e->y))) {
    p->speed.x = approach(p->speed.x, signf(w->x - e->x) * 64 * w->mult, 1000 * DT);
    return true;
  }
  p->spr.rate = 1;
  spr_play(&p->spr, A_player_idle, false);
  p->dummy_moving = false;
  return false;
}
