/* The Knight: HeroController and HeroAnimationController, run 50 times a second (Unity's FixedUpdate rate), each tick
 * as a Unity frame: FixedUpdate, the physics step and its collision callbacks, Update, then the animation. */
#include <math.h>
#include "game.h"

/* HeroController's values (the Knight prefab) and constants */
#define RUN_SPEED 8.3f
#define WALK_SPEED 6.0f
#define JUMP_SPEED 16.65f
#define JUMP_STEPS 9
#define JUMP_STEPS_MIN 4
#define DASH_SPEED 20.0f
#define DASH_TIME 0.25f
#define DASH_QUEUE_STEPS 10
#define DASH_COOLDOWN 0.6f
#define DEFAULT_GRAVITY 0.79f
#define ATTACK_DURATION 0.35f
#define ALT_ATTACK_RESET 0.5f
#define ATTACK_RECOVERY_TIME 0.1f
#define ATTACK_COOLDOWN_TIME 0.41f
#define BOUNCE_TIME 0.25f
#define BOUNCE_VELOCITY 12.0f
#define RECOIL_HOR_VELOCITY 3.75f
#define RECOIL_HOR_VELOCITY_LONG 16.0f
#define RECOIL_HOR_STEPS 8
#define BIG_FALL_TIME 1.1f
#define HARD_LANDING_TIME 0.8f
#define DOWN_DASH_TIME 0.0f
#define MAX_FALL_VELOCITY 20.0f
#define JUMP_QUEUE_STEPS 2
#define ATTACK_QUEUE_STEPS 5
#define LOOK_DELAY 0.85f
#define LOOK_ANIM_DELAY 0.25f
#define FLOATING_CHECK_TIME 0.18f
#define NAIL_TERRAIN_CHECK_TIME 0.12f
#define BUMP_VELOCITY 4.0f
#define BUMP_VELOCITY_DASH 5.0f
#define LANDING_BUFFER_STEPS 5
#define LEDGE_BUFFER_STEPS 2
#define HEAD_BUMP_STEPS 3
#define DT 0.02f

Hero g_hero;

/* ---------------------------------------------------------------- input */
static bool held(uint32_t k) { return (g_hero.keys & k) != 0; }
static bool pressed(uint32_t k) { return (g_hero.keys & k) && !(g_hero.prev_keys & k); }

/* ---------------------------------------------------------------- the collider's bounds */
static float bmin_x(void) { return g_hero.body.x + g_hero.body.ox - g_hero.body.hx; }
static float bmax_x(void) { return g_hero.body.x + g_hero.body.ox + g_hero.body.hx; }
static float bmin_y(void) { return g_hero.body.y + g_hero.body.oy - g_hero.body.hy; }
static float bmax_y(void) { return g_hero.body.y + g_hero.body.oy + g_hero.body.hy; }
static float bcx(void) { return g_hero.body.x + g_hero.body.ox; }
static float bcy(void) { return g_hero.body.y + g_hero.body.oy; }

static bool ray(float x, float y, float dx, float dy, float d) { return phys_ray(x, y, dx, dy, d, CF_TERRAIN, NULL); }

bool hero_touching_ground(void) {
  float d = g_hero.body.hy + 0.16f;
  return ray(bmin_x(), bcy(), 0, -1, d) || ray(bcx(), bcy(), 0, -1, d) || ray(bmax_x(), bcy(), 0, -1, d);
}

static bool check_near_roof(void) {
  float l = 1.1180340f;   /* |(0.5, 1)| */
  return phys_ray(bmin_x(), bmax_y(), -0.5f / l, 1 / l, 2, CF_TERRAIN, NULL) ||
         phys_ray(bmax_x(), bmax_y(), 0.5f / l, 1 / l, 2, CF_TERRAIN, NULL) ||
         ray(bcx() + g_hero.body.hx / 2, bmax_y(), 0, 1, 1) || ray(bcx() - g_hero.body.hx / 2, bmax_y(), 0, 1, 1);
}

static bool wall_hit(float x, float y, float dx) {
  PhysHit h;
  if (!phys_ray(x, y, dx, 0, 0.1f, CF_TERRAIN, &h)) return false;
  return !(phys_col_flags(h.col) & (CF_STEEP | CF_NONSLIDER));
}
static bool check_still_touching_wall(int left, bool check_top) {
  float x = left ? bmin_x() : bmax_x(), dx = left ? -1.0f : 1.0f;
  if (wall_hit(x, bcy(), dx) || wall_hit(x, bmin_y(), dx)) return true;
  return check_top && wall_hit(x, bmax_y(), dx);
}

static bool check_for_bump(int left) {
  float n = 0.025f, m = 0.2f, dist = 0.32f + m, dx = left ? -1.0f : 1.0f;
  float x = left ? bmin_x() + m : bmax_x() - m;
  PhysHit low;
  bool hit_low = phys_ray(x, bmin_y() - n, dx, 0, dist, CF_TERRAIN, &low);
  bool hit_high = phys_ray(x, bmin_y() + 0.2f, dx, 0, dist, CF_TERRAIN, NULL);
  if (!hit_low || hit_high) return false;
  PhysHit a, b;
  if (!phys_ray(low.x + (left ? -0.1f : 0.1f), low.y + 1, 0, -1, 1.5f, CF_TERRAIN, &a)) return false;
  if (!phys_ray(low.x + (left ? 0.1f : -0.1f), low.y + 1, 0, -1, 1.5f, CF_TERRAIN, &b)) return true;
  return a.y - b.y > 0;
}

/* ---------------------------------------------------------------- state */
static void anim_update_state(int s);
static void invulnerable_tick(void);
static void respawn_tick(void);
static void die_from_hazard(void);
static void slash_fixed(void);
static void slash_hits(void);
static void entering_tick(void);
void hero_die(void);

static bool can_exit_no_input(void) { return !g_hero.doing_hazard_respawn && !g_hero.cs.dead; }

static void set_state(int s) {
  Hero *h = &g_hero;
  if (h->state == HS_NO_INPUT && !can_exit_no_input()) return;
  if (s == HS_GROUNDED) s = fabsf(h->move_input) > 1e-6f ? HS_RUNNING : HS_IDLE;
  else if (s == HS_PREVIOUS) s = h->prev_state;
  if (s != h->state) {
    h->prev_state = h->state;
    h->state = (uint8_t)s;
    anim_update_state(s);
  }
}

static void face_right(void) { g_hero.cs.facing_right = true; }
static void face_left(void) { g_hero.cs.facing_right = false; }
static void flip_sprite(void) { g_hero.cs.facing_right = !g_hero.cs.facing_right; }

static void affected_by_gravity(bool on) {
  Hero *h = &g_hero;
  if (h->body.gravity_scale > 1e-6f && !on) {
    h->prev_gravity = h->body.gravity_scale;
    h->body.gravity_scale = 0;
  } else if (h->body.gravity_scale <= 1e-6f && on)
    h->body.gravity_scale = h->prev_gravity;
}

static void cancel_jump(void) { g_hero.cs.jumping = false, g_hero.jump_steps = 0; }
static void cancel_dash(void) {
  g_hero.cs.dashing = false, g_hero.dash_timer = 0;
  affected_by_gravity(true);
}
static void cancel_bounce(void) { g_hero.cs.bouncing = false, g_hero.bounce_timer = 0; }
static void cancel_recoil_horizontal(void) { g_hero.cs.recoiling_left = g_hero.cs.recoiling_right = false, g_hero.recoil_steps = 0; }
static void reset_attacks(void) {
  Hero *h = &g_hero;
  h->cs.attacking = h->cs.up_attacking = h->cs.down_attacking = false;
  h->attack_time = 0;
}
static void cancel_attack(void) {
  if (g_hero.cs.attacking) slash_cancel(), reset_attacks();
}
static void cancel_down_attack(void) {
  if (g_hero.cs.down_attacking) slash_cancel(), reset_attacks();
}
static void reset_motion(void) {
  Hero *h = &g_hero;
  cancel_jump(), cancel_dash(), cancel_bounce(), cancel_recoil_horizontal();
  h->body.vx = h->body.vy = 0;
}
static void reset_look(void) {
  Hero *h = &g_hero;
  h->cs.looking_up = h->cs.looking_down = h->cs.looking_up_anim = h->cs.looking_down_anim = false;
  h->look_delay_timer = 0;
}
static void reset_input(void) { g_hero.move_input = g_hero.vertical_input = 0; }

static void back_on_ground(void) {
  Hero *h = &g_hero;
  if (h->landing_buffer_steps <= 0) h->landing_buffer_steps = LANDING_BUFFER_STEPS;
  h->cs.falling = false;
  h->fall_timer = 0;
  h->dash_landing_timer = 0;
  h->cs.will_hard_land = false;
  h->hard_landing_timer = 0;
  h->hard_landed = false;
  h->jump_steps = 0;
  set_state(HS_GROUNDED);
  h->cs.on_ground = true;
  h->air_dashed = false;
}

static void jump_released(void) {
  Hero *h = &g_hero;
  if (h->body.vy > 0 && h->jumped_steps >= JUMP_STEPS_MIN) {
    h->body.vy = 0;
    cancel_jump();
  }
  h->jump_queuing = false;
}

static void finished_dashing(void) {
  cancel_dash();
  affected_by_gravity(true);
  g_hero.play_dash_to_idle = true;
}

static void do_hard_landing(void) {
  Hero *h = &g_hero;
  affected_by_gravity(true);
  reset_input();
  set_state(HS_HARD_LANDING);
  cancel_attack();
  h->hard_landed = true;
}

/* ---------------------------------------------------------------- abilities */
static bool can_jump(void) {
  Hero *h = &g_hero;
  if (h->state == HS_NO_INPUT || h->state == HS_HARD_LANDING || h->state == HS_DASH_LANDING || h->cs.dashing ||
      h->cs.jumping || h->cs.bouncing)
    return false;
  if (h->cs.on_ground) return true;
  if (h->ledge_buffer_steps > 0 && !h->cs.dead && !h->control_relinquished && h->head_bump_steps <= 0 && !check_near_roof()) {
    h->ledge_buffer_steps = 0;
    return true;
  }
  return false;
}

static bool can_dash(void) {
  Hero *h = &g_hero;
  return h->state != HS_NO_INPUT && h->state != HS_HARD_LANDING && h->state != HS_DASH_LANDING && h->dash_cooldown_timer <= 0 &&
         !h->cs.dashing && !(h->cs.attacking && h->attack_time < ATTACK_RECOVERY_TIME) && !h->cs.prevent_dash &&
         (h->cs.on_ground || !h->air_dashed) && !h->cs.hazard_death && g_pd.can_dash;
}

static bool can_attack(void) {
  Hero *h = &g_hero;
  return h->attack_cooldown <= 0 && !h->cs.attacking && !h->cs.dashing && !h->cs.dead && !h->cs.hazard_death &&
         !h->cs.hazard_respawning && !h->control_relinquished && h->state != HS_NO_INPUT && h->state != HS_HARD_LANDING &&
         h->state != HS_DASH_LANDING;
}

static void hero_jump(void) {
  Hero *h = &g_hero;
  reset_look();
  h->cs.recoiling = false;
  h->cs.jumping = true;
  h->jump_queue_steps = 0;
  h->jumped_steps = 0;
}

static void hero_dash(void) {
  Hero *h = &g_hero;
  if (!h->cs.on_ground) h->air_dashed = true;
  h->cs.attacking = h->cs.up_attacking = h->cs.down_attacking = false, h->attack_time = 0;
  cancel_bounce();
  reset_look();
  h->cs.recoiling = false;
  if (held(K_RIGHT)) face_right();
  else if (held(K_LEFT)) face_left();
  h->cs.dashing = true;
  h->dash_queue_steps = 0;
  h->dash_cooldown_timer = DASH_COOLDOWN;
  fx_dash_burst(h->body.x, h->body.y, h->cs.facing_right, h->cs.on_ground);
}

static void attack(int dir) {
  Hero *h = &g_hero;
  if (h->time_since_level - h->alt_attack_time > ALT_ATTACK_RESET) h->cs.alt_attack = false;
  h->cs.attacking = true;
  h->attack_duration = ATTACK_DURATION;
  int kind;
  if (dir == ATK_NORMAL) {
    kind = h->cs.alt_attack ? SLASH_ALT : SLASH_NORMAL;
    h->cs.alt_attack = !h->cs.alt_attack;
  } else if (dir == ATK_UP) {
    kind = SLASH_UP;
    h->cs.up_attacking = true;
  } else {
    kind = SLASH_DOWN;
    h->cs.down_attacking = true;
  }
  h->alt_attack_time = h->time_since_level;
  slash_start(kind);
}

static void do_attack(void) {
  Hero *h = &g_hero;
  reset_look();
  h->cs.recoiling = false;
  h->attack_cooldown = ATTACK_COOLDOWN_TIME;
  int dir = ATK_NORMAL;
  if (h->vertical_input > 1e-6f) dir = ATK_UP;
  else if (h->vertical_input < -1e-6f && h->state != HS_IDLE && h->state != HS_RUNNING) dir = ATK_DOWN;
  attack(dir);
  /* CheckForTerrainThunk */
  h->thunk_dir = (int8_t)dir;
  h->thunk_timer = NAIL_TERRAIN_CHECK_TIME;
  h->thunk_hit = false;
}

/* NailParry: a moment unhurt by what is no hazard */
void hero_nail_parry(void) { g_hero.parry_t = 0.25f; }

void hero_recoil_left(void) {
  Hero *h = &g_hero;
  if (!h->cs.recoiling_left && !h->cs.recoiling_right && !charm_on(14) && !h->control_relinquished) {
    cancel_dash();
    h->recoil_steps = 0;
    h->cs.recoiling_left = true, h->cs.recoiling_right = false;
    h->recoil_large = false;
    h->body.vx = -RECOIL_HOR_VELOCITY;
  }
}
void hero_recoil_right(void) {
  Hero *h = &g_hero;
  if (!h->cs.recoiling_left && !h->cs.recoiling_right && !charm_on(14) && !h->control_relinquished) {
    cancel_dash();
    h->recoil_steps = 0;
    h->cs.recoiling_right = true, h->cs.recoiling_left = false;
    h->recoil_large = false;
    h->body.vx = RECOIL_HOR_VELOCITY;
  }
}
void hero_recoil_down(void) {
  Hero *h = &g_hero;
  cancel_jump();
  if (h->body.vy > 0) h->body.vy = 0;   /* (RECOIL_DOWN_VELOCITY) */
}
void hero_bounce(void) {
  Hero *h = &g_hero;
  if (!h->cs.bouncing && !h->control_relinquished) {
    cancel_jump();
    cancel_dash();
    h->bounce_timer = 0;
    h->cs.bouncing = true;
    h->air_dashed = false;
  }
}

/* the nail meeting the ground: the Knight is pushed back (CheckForTerrainThunk) */
static void terrain_thunk(void) {
  Hero *h = &g_hero;
  if (h->thunk_timer <= 0) return;
  if (!h->thunk_hit) {
    float len = (h->thunk_dir == ATK_NORMAL ? 2.0f : 1.5f) * (charm_on(18) ? 1.2f : 1.0f);   /* (Longnail) */
    float hs = 0.225f;   /* the 0.45 box cast */
    float ox = bcx(), oy = bcy() + 0.25f, dx = 0, dy = 0;
    if (h->thunk_dir == ATK_NORMAL) dx = h->cs.facing_right ? 1 : -1;
    else if (h->thunk_dir == ATK_UP) oy = bmax_y(), dy = 1;
    else oy = bmin_y(), dy = -1;
    /* (a box cast, as three rays across the box: the nearest ground; not a NonThunker's) */
    PhysHit best = {0}, ph;
    best.dist = 1e9f;
    for (int i = -1; i <= 1; i++)
      if (phys_ray(ox + (dy ? i * hs : 0), oy + (dx ? i * hs : 0), dx, dy, len + hs, CF_SOLID, &ph) && ph.dist < best.dist)
        best = ph;
    if (best.dist < 1e9f && !(phys_col_flags(best.col) & CF_NONTHUNKER)) {
      h->thunk_hit = true;
      if (h->thunk_dir == ATK_NORMAL) {
        if (h->cs.facing_right) hero_recoil_left();
        else hero_recoil_right();
      } else if (h->thunk_dir == ATK_UP)
        hero_recoil_down();
    }
  }
  h->thunk_timer -= DT;
}

/* ---------------------------------------------------------------- FixedUpdate */
static void move(float dir) {
  Hero *h = &g_hero;
  if (h->cs.on_ground) set_state(HS_GROUNDED);
  if (h->accepting_input) h->body.vx = dir * (h->cs.in_walk_zone ? WALK_SPEED : RUN_SPEED);
}

static void jump(void) {
  Hero *h = &g_hero;
  if (h->jump_steps <= JUMP_STEPS) {
    h->body.vy = JUMP_SPEED;
    h->jump_steps++, h->jumped_steps++;
    h->ledge_buffer_steps = 0;
  } else
    cancel_jump();
}

static void dash(void) {
  Hero *h = &g_hero;
  affected_by_gravity(false);
  h->hard_landing_timer = 0;
  if (h->dash_timer > DASH_TIME) {
    finished_dashing();
    return;
  }
  float s = DASH_SPEED;
  if (h->cs.facing_right) {
    if (check_for_bump(0)) h->body.vx = s, h->body.vy = h->cs.on_ground ? BUMP_VELOCITY : BUMP_VELOCITY_DASH;
    else h->body.vx = s, h->body.vy = 0;
  } else if (check_for_bump(1))
    h->body.vx = -s, h->body.vy = h->cs.on_ground ? BUMP_VELOCITY : BUMP_VELOCITY_DASH;
  else
    h->body.vx = -s, h->body.vy = 0;
  h->dash_timer += DT;
}

static void fixed_update(void) {
  Hero *h = &g_hero;
  if (h->cs.recoiling_left || h->cs.recoiling_right) {
    if (h->recoil_steps <= RECOIL_HOR_STEPS) h->recoil_steps++;
    else cancel_recoil_horizontal();
  }
  if (h->cs.dead) h->body.vx = h->body.vy = 0;
  if (h->state == HS_HARD_LANDING || h->state == HS_DASH_LANDING)
    reset_motion();
  else if (h->state == HS_NO_INPUT) {
    if (h->cs.transitioning) {
      if (h->transition_state == TS_EXITING_SCENE) {
        affected_by_gravity(false);
        h->body.vx = h->transition_vx, h->body.vy = h->transition_vy + h->body.vy;
      } else if (h->transition_state == TS_ENTERING_SCENE)
        h->body.vx = h->transition_vx, h->body.vy = h->transition_vy;
      else if (h->transition_state == TS_DROPPING_DOWN)
        h->body.vx = h->transition_vx;
    } else if (h->cs.recoiling) {
      affected_by_gravity(false);
      h->body.vx = h->recoil_vx, h->body.vy = h->recoil_vy;
    }
  } else {
    if (h->state == HS_RUNNING) {
      if (h->move_input > 0) {
        if (check_for_bump(0)) h->body.vy = BUMP_VELOCITY;
      } else if (h->move_input < 0 && check_for_bump(1))
        h->body.vy = BUMP_VELOCITY;
    }
    if (!h->cs.dashing) {
      move(h->move_input);
      if (!h->cs.attacking || h->attack_time >= ATTACK_RECOVERY_TIME) {
        if (h->move_input > 0 && !h->cs.facing_right) flip_sprite(), cancel_attack();
        else if (h->move_input < 0 && h->cs.facing_right) flip_sprite(), cancel_attack();
      }
      if (h->cs.recoiling_left) {
        float v = h->recoil_large ? RECOIL_HOR_VELOCITY_LONG : RECOIL_HOR_VELOCITY;
        h->body.vx = h->body.vx > -v ? -v : h->body.vx - v;
      }
      if (h->cs.recoiling_right) {
        float v = h->recoil_large ? RECOIL_HOR_VELOCITY_LONG : RECOIL_HOR_VELOCITY;
        h->body.vx = h->body.vx < v ? v : h->body.vx + v;
      }
    }
    if ((h->cs.looking_up || h->cs.looking_down) && fabsf(h->move_input) > 0.6f) reset_look();
    if (h->cs.jumping) jump();
    if (h->cs.dashing) dash();
    if (h->cs.casting) {
      if (h->cs.cast_recoiling) h->body.vx = h->cs.facing_right ? -10.0f : 10.0f, h->body.vy = 0;
      else h->body.vx = h->body.vy = 0;
    }
    if (h->cs.bouncing) h->body.vy = BOUNCE_VELOCITY;
  }
  if (h->body.vy < -MAX_FALL_VELOCITY && !h->control_relinquished) h->body.vy = -MAX_FALL_VELOCITY;
  if (h->jump_queuing) h->jump_queue_steps++;
  if (h->dash_queuing) h->dash_queue_steps++;
  if (h->attack_queuing) h->attack_queue_steps++;
  if (h->landing_buffer_steps > 0) h->landing_buffer_steps--;
  if (h->ledge_buffer_steps > 0) h->ledge_buffer_steps--;
  if (h->head_bump_steps > 0) h->head_bump_steps--;
  h->cs.was_on_ground = h->cs.on_ground;
}

/* ---------------------------------------------------------------- collisions (OnCollisionEnter2D, Stay, Exit) */
static bool should_hard_land(int col) {
  Hero *h = &g_hero;
  return !(phys_col_flags(col) & CF_NOHARDLAND) && h->cs.will_hard_land && h->state != HS_HARD_LANDING;
}

static void on_enter(const BodyEvent *e) {
  Hero *h = &g_hero;
  uint8_t f = phys_col_flags(e->col);
  if ((f & CF_TERRAIN) && hero_touching_ground()) h->landed_event = true;   /* HeroCtrl-Landed */
  if (h->state != HS_NO_INPUT) {
    if (!(f & CF_TERRAIN)) return;
    int side = e->ny >= 0.5f ? SIDE_BOTTOM : e->ny <= -0.5f ? SIDE_TOP : e->nx < 0 ? SIDE_RIGHT : SIDE_LEFT;
    if (side == SIDE_TOP) {
      h->head_bump_steps = HEAD_BUMP_STEPS;
      if (h->cs.jumping) cancel_jump();
      if (h->cs.bouncing) cancel_bounce(), h->body.vy = 0;
    }
    if (side == SIDE_BOTTOM) {
      if (h->cs.attacking) cancel_down_attack();
      if (should_hard_land(e->col)) do_hard_landing();
      else if (!(f & CF_STEEP) && h->state != HS_HARD_LANDING) back_on_ground();
    }
  } else if (h->transition_state == TS_DROPPING_DOWN && (h->gate_position == GATE_BOTTOM || h->gate_position == GATE_TOP))
    hero_finished_entering_scene(true);
}

static void on_stay(const BodyEvent *e) {
  Hero *h = &g_hero;
  uint8_t f = phys_col_flags(e->col);
  if (h->state == HS_NO_INPUT || !(f & CF_TERRAIN)) return;
  if (!(f & CF_NONSLIDER)) {
    h->cs.touching_non_slider = false;
    if (check_still_touching_wall(1, false)) h->cs.touching_wall = true, h->touching_wall_l = true, h->touching_wall_r = false;
    else if (check_still_touching_wall(0, false)) h->cs.touching_wall = true, h->touching_wall_l = false, h->touching_wall_r = true;
    else h->cs.touching_wall = false, h->touching_wall_l = h->touching_wall_r = false;
    if (hero_touching_ground()) {
      if (should_hard_land(e->col)) do_hard_landing();
      else if (h->state != HS_HARD_LANDING && h->state != HS_DASH_LANDING && h->cs.falling) back_on_ground();
    } else if (h->cs.jumping || h->cs.falling) {
      h->cs.on_ground = false;
      set_state(HS_AIRBORNE);
    }
  } else
    h->cs.touching_non_slider = true;
}

static void on_exit(const BodyEvent *e) {
  Hero *h = &g_hero;
  uint8_t f = phys_col_flags(e->col);
  if (h->cs.recoiling_left || h->cs.recoiling_right) h->cs.touching_wall = h->touching_wall_l = h->touching_wall_r = false, h->cs.touching_non_slider = false;
  if (h->touching_wall_l && !check_still_touching_wall(1, false)) h->cs.touching_wall = false, h->touching_wall_l = false;
  if (h->touching_wall_r && !check_still_touching_wall(0, false)) h->cs.touching_wall = false, h->touching_wall_r = false;
  if (h->state == HS_NO_INPUT || h->cs.recoiling || !(f & CF_TERRAIN) || hero_touching_ground()) return;
  h->cs.on_ground = false;
  set_state(HS_AIRBORNE);
  if (h->cs.was_on_ground) h->ledge_buffer_steps = LEDGE_BUFFER_STEPS;
}

/* ---------------------------------------------------------------- Update */
static void fall_check(void) {
  Hero *h = &g_hero;
  if (h->body.vy <= -1e-6f) {
    if (hero_touching_ground()) return;
    h->cs.falling = true;
    h->cs.on_ground = false;
    if (h->state != HS_NO_INPUT) set_state(HS_AIRBORNE);
    h->fall_timer += DT;
    if (h->fall_timer > BIG_FALL_TIME) h->cs.will_hard_land = true;
  } else {
    h->cs.falling = false;
    h->fall_timer = 0;
    if (h->transition_state != TS_ENTERING_SCENE) h->cs.will_hard_land = false;
  }
}

static void fail_safe_checks(void) {
  Hero *h = &g_hero;
  if (h->state == HS_HARD_LANDING) {
    h->hard_land_fail_safe_timer += DT;
    if (h->hard_land_fail_safe_timer > HARD_LANDING_TIME + 0.3f) set_state(HS_GROUNDED), back_on_ground(), h->hard_land_fail_safe_timer = 0;
  } else
    h->hard_land_fail_safe_timer = 0;
  if (h->body.vy != 0 || h->cs.on_ground || h->cs.falling || h->cs.jumping || h->cs.dashing || h->state == HS_HARD_LANDING ||
      h->state == HS_NO_INPUT)
    return;
  if (hero_touching_ground()) {
    h->floating_buffer_timer += DT;
    if (h->floating_buffer_timer > FLOATING_CHECK_TIME) {
      if (h->cs.recoiling) h->cs.recoiling = false, h->recoil_timer = 0, reset_motion(), affected_by_gravity(true);
      back_on_ground();
      h->floating_buffer_timer = 0;
    }
  } else
    h->floating_buffer_timer = 0;
}

static void look_for_input(void) {
  Hero *h = &g_hero;
  if (!h->accepting_input || g_game.paused) return;
  h->move_input = held(K_RIGHT) && !held(K_LEFT) ? 1.0f : held(K_LEFT) && !held(K_RIGHT) ? -1.0f : 0.0f;
  h->vertical_input = held(K_UP) && !held(K_DOWN) ? 1.0f : held(K_DOWN) && !held(K_UP) ? -1.0f : 0.0f;
  if (!held(K_JUMP)) jump_released();
  if (!held(K_DASH)) {
    if (h->cs.prevent_dash && !h->cs.dash_cooldown) h->cs.prevent_dash = false;
    h->dash_queuing = false;
  }
  if (!held(K_ATTACK)) h->attack_queuing = false;
}

static void look_for_queue_input(void) {
  Hero *h = &g_hero;
  if (!h->accepting_input || g_game.paused) return;
  if (pressed(K_JUMP)) {
    if (can_jump()) hero_jump();
    else h->jump_queue_steps = 0, h->jump_queuing = true;
  }
  if (pressed(K_DASH)) {
    if (can_dash()) hero_dash();
    else h->dash_queue_steps = 0, h->dash_queuing = true;
  }
  if (pressed(K_ATTACK)) {
    if (can_attack()) do_attack();
    else h->attack_queue_steps = 0, h->attack_queuing = true;
  }
  if (held(K_JUMP) && h->jump_queue_steps <= JUMP_QUEUE_STEPS && can_jump() && h->jump_queuing) hero_jump();
  if (held(K_DASH) && h->dash_queue_steps <= DASH_QUEUE_STEPS && can_dash() && h->dash_queuing) hero_dash();
  if (held(K_ATTACK) && h->attack_queue_steps <= ATTACK_QUEUE_STEPS && can_attack() && h->attack_queuing) do_attack();
}

static void update(void) {
  Hero *h = &g_hero;
  fall_check();
  fail_safe_checks();
  if (h->state == HS_DASH_LANDING) {
    h->dash_landing_timer += DT;
    if (h->dash_landing_timer > DOWN_DASH_TIME) back_on_ground();
  }
  if (h->state == HS_HARD_LANDING) {
    h->hard_landing_timer += DT;
    if (h->hard_landing_timer > HARD_LANDING_TIME) set_state(HS_GROUNDED), back_on_ground();
  } else if (h->state == HS_NO_INPUT) {
    if (h->cs.recoiling) {
      if (h->recoil_timer < (charm_on(4) ? RECOIL_DURATION_STAL : RECOIL_DURATION)) h->recoil_timer += DT;
      else {
        h->cs.recoiling = false, h->recoil_timer = 0, reset_motion(), affected_by_gravity(true);
        h->damage_mode = DAMAGE_FULL;
        if ((h->prev_state == HS_IDLE || h->prev_state == HS_RUNNING) && !hero_touching_ground()) {
          h->cs.on_ground = false;
          set_state(HS_AIRBORNE);
        } else
          set_state(HS_PREVIOUS);
      }
    }
  } else {
    look_for_input();
    if (h->cs.recoiling) h->cs.recoiling = false, affected_by_gravity(true);
    if (h->cs.attacking && !h->cs.dashing) {
      h->attack_time += DT;
      if (h->attack_time >= h->attack_duration) {
        reset_attacks();
        /* (HeroAnimationController.StopAttack) */
        if (anim_is_playing(&h->anim, CLIP_KNIGHT_UPSLASH) || anim_is_playing(&h->anim, CLIP_KNIGHT_DOWNSLASH)) h->anim.playing = false;
      }
    }
    if (h->cs.bouncing) {
      if (h->bounce_timer < BOUNCE_TIME) h->bounce_timer += DT;
      else cancel_bounce(), h->body.vy = 0;
    }
    if (h->state == HS_IDLE) {
      if (!h->control_relinquished && !g_game.paused) {
        if (held(K_UP)) {
          h->cs.looking_down = h->cs.looking_down_anim = false;
          if (h->look_delay_timer >= LOOK_DELAY) h->cs.looking_up = true;
          else h->look_delay_timer += DT;
          h->cs.looking_up_anim = h->look_delay_timer >= LOOK_ANIM_DELAY;
        } else if (held(K_DOWN)) {
          h->cs.looking_up = h->cs.looking_up_anim = false;
          if (h->look_delay_timer >= LOOK_DELAY) h->cs.looking_down = true;
          else h->look_delay_timer += DT;
          h->cs.looking_down_anim = h->look_delay_timer >= LOOK_ANIM_DELAY;
        } else
          reset_look();
      }
    }
  }
  look_for_queue_input();
  if (h->attack_cooldown > 0) h->attack_cooldown -= DT;
  if (h->dash_cooldown_timer > 0) h->dash_cooldown_timer -= DT;
  terrain_thunk();
  if (h->fury && g_pd.health != 1) h->fury = false;   /* (Fury: Recheck, healed; Deactivate) */
}

/* ---------------------------------------------------------------- HeroAnimationController */
static void play_idle(void) {
  Hero *h = &g_hero;
  if (g_pd.health == 1 && g_pd.health_blue < 1) anim_play(&h->anim, charm_on(6) ? CLIP_KNIGHT_IDLE : CLIP_KNIGHT_IDLE_HURT);
  else if (anim_is_playing(&h->anim, CLIP_KNIGHT_LOOKUP)) anim_play(&h->anim, CLIP_KNIGHT_LOOKUPEND);
  else if (anim_is_playing(&h->anim, CLIP_KNIGHT_LOOKDOWN)) anim_play(&h->anim, CLIP_KNIGHT_LOOKDOWNEND);
  else anim_play(&h->anim, CLIP_KNIGHT_IDLE);
}

static void anim_update_state(int s) {
  Hero *h = &g_hero;
  if (!h->anim_control || s == h->anim_state) return;
  if (h->anim_state == HS_AIRBORNE && s == HS_IDLE && !h->play_landing) h->play_landing = true;
  if (h->anim_state == HS_RUNNING && s == HS_IDLE && !h->play_run_to_idle && !h->cs.in_walk_zone && !h->cs.attacking) h->play_run_to_idle = true;
  h->anim_state = (uint8_t)s;
}

/* the clip that finished, as AnimationCompleteDelegate (for the clips played with it) */
static void anim_completed(void) {
  Hero *h = &g_hero;
  int c = h->anim.clip;
  if (h->complete_delegate &&
      (c == CLIP_KNIGHT_LAND || c == CLIP_KNIGHT_RUN_TO_IDLE || c == CLIP_KNIGHT_DASH_TO_IDLE || c == CLIP_KNIGHT_EXIT_DOOR_TO_IDLE))
    play_idle();
}

static bool can_play_idle(void) {
  const Anim *a = &g_hero.anim;
  return !anim_is_playing(a, CLIP_KNIGHT_LAND) && !anim_is_playing(a, CLIP_KNIGHT_RUN_TO_IDLE) &&
         !anim_is_playing(a, CLIP_KNIGHT_DASH_TO_IDLE) && !anim_is_playing(a, CLIP_KNIGHT_LOOKUPEND) &&
         !anim_is_playing(a, CLIP_KNIGHT_LOOKDOWNEND) && !anim_is_playing(a, CLIP_KNIGHT_EXIT_DOOR_TO_IDLE) &&
         !anim_is_playing(a, CLIP_KNIGHT_WAKE_UP_GROUND) && !anim_is_playing(a, CLIP_KNIGHT_HAZARD_RESPAWN);
}
static bool can_play_turn(void) {
  return !anim_is_playing(&g_hero.anim, CLIP_KNIGHT_WAKE_UP_GROUND) && !anim_is_playing(&g_hero.anim, CLIP_KNIGHT_HAZARD_RESPAWN);
}

static void play(int clip) {
  anim_play(&g_hero.anim, clip);
}

static void animation(void) {
  Hero *h = &g_hero;
  Anim *a = &h->anim;
  if (h->play_landing) play(CLIP_KNIGHT_LAND), h->complete_delegate = true, h->play_landing = false;
  if (h->play_run_to_idle) play(CLIP_KNIGHT_RUN_TO_IDLE), h->complete_delegate = true, h->play_run_to_idle = false;
  if (h->play_dash_to_idle) play(CLIP_KNIGHT_DASH_TO_IDLE), h->complete_delegate = true, h->play_dash_to_idle = false;
  if (h->anim_state == HS_NO_INPUT) {
    if (h->cs.recoil_frozen) play(CLIP_KNIGHT_STUN);
    else if (h->cs.recoiling) play(CLIP_KNIGHT_RECOIL);
    else if (h->cs.transitioning) {
      if (h->cs.on_ground) {
        if (h->transition_state == TS_EXITING_SCENE) {
          if (!anim_is_playing(a, CLIP_KNIGHT_RUN)) play(CLIP_KNIGHT_RUN);
        } else if (h->transition_state == TS_ENTERING_SCENE) {
          if (!anim_is_playing(a, CLIP_KNIGHT_RUN)) anim_play_from_frame(a, CLIP_KNIGHT_RUN, 3);
        }
      } else if (h->transition_state == TS_EXITING_SCENE || h->transition_state == TS_WAITING_TO_ENTER_LEVEL) {
        if (!anim_is_playing(a, CLIP_KNIGHT_AIRBORNE)) anim_play_from_frame(a, CLIP_KNIGHT_AIRBORNE, 7);
      } else if (h->transition_state == TS_ENTERING_SCENE && !h->set_entry_anim) {
        if (h->gate_position == GATE_TOP) anim_play_from_frame(a, CLIP_KNIGHT_AIRBORNE, 7);
        else if (h->gate_position == GATE_BOTTOM) anim_play_from_frame(a, CLIP_KNIGHT_AIRBORNE, 3);
        h->set_entry_anim = true;
      }
    }
  } else if (h->set_entry_anim)
    h->set_entry_anim = false;
  else if (h->cs.dashing)
    play(CLIP_KNIGHT_DASH);
  else if (h->cs.attacking) {
    if (h->cs.up_attacking) play(CLIP_KNIGHT_UPSLASH);
    else if (h->cs.down_attacking) play(CLIP_KNIGHT_DOWNSLASH);
    else if (!h->cs.alt_attack) play(CLIP_KNIGHT_SLASH);
    else play(CLIP_KNIGHT_SLASHALT);
  } else if (h->cs.casting) {
    /* (the spell's own animation plays) */
  } else if (h->anim_state == HS_IDLE) {
    if (h->cs.looking_up_anim && !anim_is_playing(a, CLIP_KNIGHT_LOOKUP)) play(CLIP_KNIGHT_LOOKUP);
    else if (h->cs.looking_down_anim) play(CLIP_KNIGHT_LOOKDOWN);
    else if (!h->cs.looking_up_anim && !h->cs.looking_down_anim && can_play_idle()) play_idle();
  } else if (h->anim_state == HS_RUNNING) {
    if (!anim_is_playing(a, CLIP_KNIGHT_TURN)) {
      if (h->cs.in_walk_zone) {
        if (!anim_is_playing(a, CLIP_KNIGHT_WALK)) play(CLIP_KNIGHT_WALK);
      } else if (h->was_attacking)
        anim_play_from_frame(a, CLIP_KNIGHT_RUN, 3);
      else
        play(CLIP_KNIGHT_RUN);
    }
  } else if (h->anim_state == HS_AIRBORNE) {
    if (h->cs.jumping) {
      if (!anim_is_playing(a, CLIP_KNIGHT_AIRBORNE)) anim_play_from_frame(a, CLIP_KNIGHT_AIRBORNE, 0);
    } else if (h->cs.falling) {
      if (!anim_is_playing(a, CLIP_KNIGHT_AIRBORNE)) anim_play_from_frame(a, CLIP_KNIGHT_AIRBORNE, 5);
    } else if (!anim_is_playing(a, CLIP_KNIGHT_AIRBORNE))
      anim_play_from_frame(a, CLIP_KNIGHT_AIRBORNE, 3);
  } else if (h->anim_state == HS_HARD_LANDING)
    play(CLIP_KNIGHT_HARDLAND);
  if (h->cs.facing_right) {
    if (!h->was_facing_right && h->cs.on_ground && can_play_turn()) play(CLIP_KNIGHT_TURN);
    h->was_facing_right = true;
  } else {
    if (h->was_facing_right && h->cs.on_ground && can_play_turn()) play(CLIP_KNIGHT_TURN);
    h->was_facing_right = false;
  }
  h->was_attacking = h->cs.attacking;
}

/* ---------------------------------------------------------------- the tick */
static void entering_stop(void);
void hero_init(float x, float y, bool facing_right) {
  Hero *h = &g_hero;
  static BodyEvent events[MAX_EVENTS];
  memset(h, 0, sizeof *h);
  entering_stop();   /* (a way into a room not finished: over) */
  spell_reset();
  h->entry_gate = -1;
  h->body.events = events;
  h->body.x = x, h->body.y = y;
  h->body.ox = 0, h->body.oy = -0.75f, h->body.hx = 0.25f, h->body.hy = 0.640625f;   /* the Knight's BoxCollider2D */
  h->body.gravity_scale = DEFAULT_GRAVITY;
  h->prev_gravity = DEFAULT_GRAVITY;
  h->body.mask = CF_SOLID;
  h->cs.facing_right = h->was_facing_right = facing_right;
  h->accepting_input = true;
  h->anim_control = true;
  h->state = h->anim_state = HS_AIRBORNE;
  h->transition_state = TS_WAITING_TO_TRANSITION;
  if (hero_touching_ground()) {
    h->cs.on_ground = true;
    set_state(HS_GROUNDED);
    h->anim_state = h->state;
    play_idle();
  } else
    anim_play_from_frame(&h->anim, CLIP_KNIGHT_AIRBORNE, 7);
}

void hero_fixed(uint32_t keys) {
  Hero *h = &g_hero;
  h->prev_keys = h->keys, h->keys = keys;
  h->time_since_level += DT;
  fixed_update();
  slash_fixed();
  body_step(&h->body, DT);
  for (int i = 0; i < h->body.nevents; i++) {
    const BodyEvent *e = &h->body.events[i];
    if (e->kind == EV_ENTER) on_enter(e);
    else if (e->kind == EV_STAY) on_stay(e);
    else on_exit(e);
  }
  slash_hits();
}

void hero_update(void) {
  Hero *h = &g_hero;
  if (h->prevent_cast > 0) h->prevent_cast -= DT;
  if (h->parry_t > 0) h->parry_t -= DT;
  update();
  invulnerable_tick();
  respawn_tick();
  entering_tick();
  spell_update();
  map_tick();
  if (h->anim_control) animation();
  h->anim.events = 0;
  anim_update(&h->anim, DT);
  if (h->anim.events & ANIM_DONE) anim_completed();
  if (!(h->anim.clip == CLIP_KNIGHT_LAND || h->anim.clip == CLIP_KNIGHT_RUN_TO_IDLE || h->anim.clip == CLIP_KNIGHT_DASH_TO_IDLE))
    h->complete_delegate = false;
  slash_tick();
}

void hero_draw(void) {
  Hero *h = &g_hero;
  /* HeroLight: a child glow in the room's hero light color (SceneManager), blended as linear light */
  const float *hl = g_room.h->hero_light;
  Inst li;
  /* (clamped: Greenpath's green is 1.0074, and 256 in a byte is 0: a pink glow, not a green one) */
  uint8_t c[4];
  for (int k = 0; k < 4; k++) c[k] = hl[k] >= 1 ? 255 : hl[k] <= 0 ? 0 : (uint8_t)(hl[k] * 255);
  sprite_inst(SPRITE_HERO_LIGHT, h->body.x, h->body.y - 0.6f, 0.004f + 0.0312f, 1, 1, gfx_dyn_tint(1, c[0], c[1], c[2], c[3]), &li);
  li.flags = (uint8_t)((li.flags & ~F_BLEND) | BL_LINEARLIGHT);
  gfx_actor(&li, SORT_KEY(0, 0));
  if (h->hidden) return;
  Inst in;
  /* (InvulnerablePulse: the color towards its invulnerable one) */
  uint8_t tint = 0;
  if (h->pulsing) tint = gfx_dyn_tint(0, 255, 255, 255, (uint8_t)(255 - 255 * 0.5f * h->pulse_t / 0.1f));
  sprite_inst(h->anim.sprite, h->body.x, h->body.y, 0.004f, h->cs.facing_right ? -1.0f : 1.0f, 1, tint, &in);
  if (!gfx_actor(&in, SORT_KEY(0, 0))) {
#ifdef HOST
    extern uint32_t g_hero_undrawn;   /* (never: checked by tests) */
    g_hero_undrawn++;
#endif
  }
  slash_draw();
}

/* HeroController.Die: the Knight still and unseen, his Hero Death object in his place (death.c) */
void hero_die(void) {
  Hero *h = &g_hero;
  if (h->cs.dead) return;
  g_pd.disable_pause = true;
  h->body.vx = h->body.vy = 0;
  cancel_recoil_horizontal();
  affected_by_gravity(false);
  set_state(HS_NO_INPUT);
  h->cs.dead = true;
  reset_motion();
  h->hard_landing_timer = 0;
  h->hidden = true;
  spell_cancel();
  map_cancel();
  slash_cancel();
  death_start(h->body.x, h->body.y, h->cs.facing_right);
}

/* ---------------------------------------------------------------- NailSlash: the slash effect, a child of the Knight */
static const struct {
  int clip, fury;          /* (its animation; " F": while the Fury is on) */
  float x, y, z, sx, sy;   /* its place and scale on the Knight */
} slashes[4] = {
    {CLIP_KNIGHT_SLASHEFFECT, CLIP_KNIGHT_SLASHEFFECT_F, -0.01f, -0.41f, -0.001f, 1.6010780f, 1.6452440f},
    {CLIP_KNIGHT_SLASHEFFECTALT, CLIP_KNIGHT_SLASHEFFECTALT_F, 0.08f, -0.436f, -0.001f, 1.2569700f, 1.4224339f},
    {CLIP_KNIGHT_UPSLASHEFFECT, CLIP_KNIGHT_UPSLASHEFFECT_F, 0.0f, 0.69f, -0.001f, 1.15f, 1.4f},
    {CLIP_KNIGHT_DOWNSLASHEFFECT, CLIP_KNIGHT_DOWNSLASHEFFECT_F, 0.16f, -1.59f, 0.0f, 1.125f, 1.28002f},
};
#define LONGNAIL 1.15f   /* (NailSlash.StartSlash: Longnail's scale) */
static struct {
  int kind;
  float k;   /* (its scale: Longnail's) */
  bool slashing, shown, anim_completed;
  int step_counter, poly_counter;
  bool poly;
  Anim anim;
} sl;

void slash_start(int kind) {
  sl.kind = kind;
  sl.k = charm_on(18) ? LONGNAIL : 1;
  int clip = g_hero.fury ? slashes[kind].fury : slashes[kind].clip;
  anim_play(&sl.anim, clip);
  anim_play_from_frame(&sl.anim, clip, 0);
  sl.step_counter = sl.poly_counter = 0;
  sl.poly = false;
  sl.anim_completed = false;
  sl.slashing = sl.shown = true;
}

/* (NailSlash.CancelAttack: the hit shape off, then the slash is done) */
void slash_cancel(void) {
  sl.slashing = false;
  sl.poly = false;
  sl.shown = false;
}

/* NailSlash.FixedUpdate: the hit shape on from the second step to the sixth */
static void slash_fixed(void) {
  if (!sl.slashing) return;
  if (sl.step_counter == 1) {
    sl.poly = true;
    obj_swing_start();   /* (LimitSendEvents: its collider switched on) */
  }
  if (sl.step_counter >= 5 && sl.poly_counter > 0) sl.poly = false;
  if (sl.anim_completed && sl.poly_counter > 1) slash_cancel();
  if (sl.poly) sl.poly_counter++;
  sl.step_counter++;
}

/* the slash's PolygonCollider2D (local, before its scale) */
static const float slash_poly[4][12] = {
    {-1.2709147f, 0.7413744f, -1.9154043f, 0.3402300f, -2.0418284f, -0.1750279f, -1.6737577f, -0.4435550f, -0.0747072f,
     -0.6419125f, 0.0078668f, 0.9683739f},
    {-1.2172120f, 0.9806359f, -2.3899105f, 0.5705993f, -2.6464114f, -0.0792437f, -2.1236799f, -0.5379019f, -0.3672335f,
     -0.6931571f, -0.2434342f, 1.0901221f},
    {-0.4705164f, 1.4424964f, 0.4715460f, 1.4633876f, 1.0817832f, 0.6738139f, 1.1484641f, -0.8903970f, -1.2853814f,
     -0.8659905f, -1.0722543f, 0.6188976f},
    {1.2783164f, 0.6065824f, 1.1190680f, -0.8765929f, 0.5875434f, -1.6216662f, -0.4472086f, -1.6460726f, -1.0696145f,
     -0.7312644f, -1.3820535f, 0.6370943f},
};

/* the hazards a slash touches that a down slash bounces off (spikes: on the Hero Attack layer), as HB_BOUNCE */
static int hazards_nail(const float *pts, int npts) {
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++) {
    const Ent *e = &es[i];
    if (e->type != ENT_DAMAGE || !((int)e->p3 & HB_BOUNCE) || !box_meets_shape(e->x0, e->y0, e->x1, e->y1, pts, npts)) continue;
    if (!e->a) return HB_BOUNCE;
    /* (its outline: 4 points a record after it) */
    float q[16];
    int nq = (int)e->p2 < 8 ? (int)e->p2 : 8;
    for (int k = 0; k < nq; k++) {
      const float *f = &es[i + 1 + k / 4].x0;
      q[2 * k] = f[2 * (k & 3)], q[2 * k + 1] = f[2 * (k & 3) + 1];
    }
    if (shapes_meet(q, nq, pts, npts)) return HB_BOUNCE;
  }
  return 0;
}

/* the slash's trigger: what it touches is hit (the damages_enemy FSM), and the Knight recoils or bounces off (NailSlash) */
static void slash_hits(void) {
  if (!sl.poly) return;
  Hero *h = &g_hero;
  float k = h->cs.facing_right ? -1.0f : 1.0f;   /* the Knight's x scale */
  const typeof(slashes[0]) *s = &slashes[sl.kind];
  float pts[12];
  for (int i = 0; i < 6; i++) {
    pts[2 * i] = h->body.x + k * (s->x + s->sx * sl.k * slash_poly[sl.kind][2 * i]);
    pts[2 * i + 1] = h->body.y + s->y + s->sy * sl.k * slash_poly[sl.kind][2 * i + 1];
  }
  float direction = sl.kind == SLASH_UP ? 90 : sl.kind == SLASH_DOWN ? 270 : h->cs.facing_right ? 0 : 180;
  int fl = obj_nail(pts, 6, direction);
  if (direction == 270 && !(fl & HB_BOUNCE)) fl |= hazards_nail(pts, 6);
  if (direction == 0 && (fl & HB_RECOIL)) hero_recoil_left();
  else if (direction == 180 && (fl & HB_RECOIL)) hero_recoil_right();
  else if (direction == 90 && (fl & HB_RECOIL)) hero_recoil_down();
  else if (direction == 270 && (fl & HB_BOUNCE)) hero_bounce();
}

void slash_tick(void) {
  if (!sl.shown) return;
  sl.anim.events = 0;
  anim_update(&sl.anim, DT);
  if (sl.anim.events & ANIM_DONE) {
    /* (Disable) */
    sl.anim_completed = true;
    sl.shown = false;
  }
}

void slash_draw(void) {
  if (!sl.shown) return;
  Hero *h = &g_hero;
  float k = h->cs.facing_right ? -1.0f : 1.0f;   /* the Knight's x scale */
  const typeof(slashes[0]) *s = &slashes[sl.kind];
  Inst in;
  sprite_inst(sl.anim.sprite, h->body.x + k * s->x, h->body.y + s->y, 0.004f + s->z, k * s->sx * sl.k, s->sy * sl.k, 0, &in);
  gfx_actor(&in, SORT_KEY(0, 0));
}

void fx_dash_burst(float x, float y, bool facing_right, bool on_ground) { (void)x, (void)y, (void)facing_right, (void)on_ground; }

/* ---------------------------------------------------------------- scenes: LeaveScene, EnterScene */
#define SPEED_TO_ENTER_SCENE_HOR 6.0f
#define SPEED_TO_ENTER_SCENE_UP 9.4f
#define SPEED_TO_ENTER_SCENE_DOWN (-12.0f)
#define TIME_TO_ENTER_SCENE_BOT 0.1f
#define MIN_JUMP_SPEED 3.0f

void hero_leave_scene(int gate) {
  Hero *h = &g_hero;
  spell_cancel_regain();   /* (LEAVING SCENE: Spell Control's Cancel Some) */
  map_cancel();
  dialogue_cancel();
  h->accepting_input = false;   /* (IgnoreInputWithoutReset) */
  h->hard_landing_timer = 0;
  set_state(HS_NO_INPUT);
  h->damage_mode = DAMAGE_NONE;
  h->transition_state = TS_EXITING_SCENE;
  h->transition_vx = h->transition_vy = 0;
  if (gate == GATE_TOP) h->transition_vy = MIN_JUMP_SPEED, h->cs.on_ground = false;
  else if (gate == GATE_RIGHT) h->transition_vx = RUN_SPEED;
  else if (gate == GATE_BOTTOM) h->cs.on_ground = false;
  else if (gate == GATE_LEFT) h->transition_vx = -RUN_SPEED;
  h->cs.transitioning = true;
}

/* FindGroundPointY: the Knight's place standing on the ground below (x, y) */
static float ground_point_y(float x, float y, float dist) {
  Hero *h = &g_hero;
  PhysHit hit;
  if (!phys_ray(x, y, 0, -1, dist, CF_TERRAIN, &hit)) hit.y = 0;
  return hit.y + h->body.hy - h->body.oy + 0.01f;
}

/* the EnterScene coroutine: its steps, each after a wait */
static struct {
  int8_t step;
  uint8_t gate, flags;
  float t, delay, gx, gy, ox, oy;
  int gate_ent;
} en;

static void entering_wait(float t) { en.t = t; }
static void entering_stop(void) { memset(&en, 0, sizeof en); }

void hero_enter_scene(int gate_ent, int gate, float gx, float gy, float ox, float oy, uint8_t flags, float delay) {
  Hero *h = &g_hero;
  h->accepting_input = false;
  reset_motion();
  h->air_dashed = false;
  h->hard_landing_timer = 0;
  reset_attacks();
  affected_by_gravity(false);
  set_state(HS_NO_INPUT);
  h->transition_state = TS_WAITING_TO_ENTER_LEVEL;
  h->cs.transitioning = true;
  h->gate_position = (uint8_t)gate;
  memset(&en, 0, sizeof en);
  en.gate = (uint8_t)gate, en.flags = flags, en.gx = gx, en.gy = gy, en.ox = ox, en.oy = oy, en.delay = delay;
  en.gate_ent = gate_ent;
  en.step = 1;
  h->entry_gate = (int16_t)gate_ent;
  if (gate == GATE_TOP) {
    h->cs.on_ground = false;
    h->hidden = true;
    h->body.x = gx + ox, h->body.y = gy + oy;
    world_hero_in_position(), cam_init();
    entering_wait(0.165f);
  } else if (gate == GATE_BOTTOM) {
    h->cs.on_ground = false;
    if (flags & G_ENTER_RIGHT) face_right();
    if (flags & G_ENTER_LEFT) face_left();
    h->body.x = gx + ox, h->body.y = gy + oy + 3;
    world_hero_in_position(), cam_init();
    entering_wait(0.165f);
  } else if (gate == GATE_LEFT || gate == GATE_RIGHT) {
    float k = gate == GATE_LEFT ? 1.0f : -1.0f;
    h->cs.on_ground = true;
    h->body.x = gx + ox - k;
    h->body.y = ground_point_y(h->body.x + 2 * k, gy, 10);
    world_hero_in_position(), cam_init();
    if (gate == GATE_LEFT) face_right();
    else face_left();
    entering_wait(0.165f);
  } else {
    /* a door */
    if (flags & G_ENTER_RIGHT) face_right();
    if (flags & G_ENTER_LEFT) face_left();
    h->cs.on_ground = true;
    anim_play(&h->anim, CLIP_KNIGHT_IDLE);
    h->body.x = gx, h->body.y = ground_point_y(gx, gy, 10);
    world_hero_in_position(), cam_init();
    entering_wait(0);
  }
  h->body.ncontacts = 0;
}

static void entering_tick(void) {
  Hero *h = &g_hero;
  if (!en.step) return;
  if (en.t > 0) {
    en.t -= DT;
    if (en.t > 0) return;
  }
  int step = en.step++;
  float wait_delays = en.delay;   /* (the delay before entering, then the gate's entry delay) */
  switch (en.gate) {
    case GATE_TOP:
      if (step == 1) game_fade_scene_in(), entering_wait(wait_delays + 0.2f);
      else if (step == 2) {
        h->hidden = false;
        h->body.vy = SPEED_TO_ENTER_SCENE_DOWN;
        h->transition_state = TS_DROPPING_DOWN;
        affected_by_gravity(true);
        if (en.flags & G_HARD_LAND) h->cs.will_hard_land = true;
        entering_wait(0.33f);
      } else {
        en.step = 0;
        h->transition_state = TS_ENTERING_SCENE;
        hero_finished_entering_scene(true);
      }
      break;
    case GATE_BOTTOM:
      if (step == 1) entering_wait(wait_delays + 0.2f);
      else if (step == 2) {
        game_fade_scene_in();
        h->transition_vx = h->cs.facing_right ? SPEED_TO_ENTER_SCENE_HOR : -SPEED_TO_ENTER_SCENE_HOR;
        h->transition_vy = SPEED_TO_ENTER_SCENE_UP;
        h->transition_state = TS_ENTERING_SCENE;
        h->body.x = en.gx + en.ox, h->body.y = en.gy + en.oy + 3;
        entering_wait(TIME_TO_ENTER_SCENE_BOT);
      } else {
        en.step = 0;
        h->transition_vx = h->body.vx, h->transition_vy = 0;
        affected_by_gravity(true);
        h->transition_state = TS_DROPPING_DOWN;   /* (finished as it lands) */
      }
      break;
    case GATE_LEFT:
    case GATE_RIGHT:
      if (step == 1) game_fade_scene_in(), entering_wait(wait_delays + 0.2f);
      else if (step == 2) {
        h->transition_vx = en.gate == GATE_LEFT ? RUN_SPEED : -RUN_SPEED, h->transition_vy = 0;
        h->transition_state = TS_ENTERING_SCENE;
        entering_wait(0.33f + 1 / RUN_SPEED);
      } else {
        en.step = 0;
        hero_finished_entering_scene(true);
      }
      break;
    default:
      if (step == 1) entering_wait(DT + wait_delays + 0.4f);   /* (WaitForEndOfFrame, then the waits) */
      else if (step == 2) {
        game_fade_scene_in();
        if (en.flags & G_DONT_WALK_OUT) entering_wait(0.33f);
        else {
          anim_play(&h->anim, CLIP_KNIGHT_EXIT_DOOR_TO_IDLE);
          float d = clip_duration(CLIP_KNIGHT_EXIT_DOOR_TO_IDLE);
          entering_wait(d > 0 ? d : 0.33f);
        }
      } else {
        en.step = 0;
        hero_finished_entering_scene(true);
      }
      break;
  }
}

/* FinishedEnteringScene */
void hero_finished_entering_scene(bool set_hazard_marker) {
  Hero *h = &g_hero;
  en.step = 0;
  h->doing_hazard_respawn = false;
  h->cs.transitioning = false;
  h->transition_state = TS_WAITING_TO_TRANSITION;
  /* SetStartingMotionState */
  h->move_input = 0;
  h->cs.touching_wall = false;
  if (hero_touching_ground()) {
    h->cs.on_ground = true;
    set_state(HS_GROUNDED);
    h->air_dashed = false;
  } else {
    h->cs.on_ground = false;
    set_state(HS_AIRBORNE);
  }
  anim_update_state(h->state);
  affected_by_gravity(true);
  if (set_hazard_marker) {
    /* (the room's first: where the Knight is; else the entry gate's marker, unless it is a non hazard gate) */
    int n;
    const Ent *es = room_ents(&n);
    if (h->entry_gate < 0 || h->entry_gate >= n)
      g_game.hazard_x = h->body.x, g_game.hazard_y = h->body.y, g_game.hazard_facing_right = h->cs.facing_right;
    else if (!(es[h->entry_gate].flags & G_NON_HAZARD) && es[h->entry_gate].p3 > 0) {
      const Ent *m = &es[(int)es[h->entry_gate].p3 - 1];
      g_game.hazard_x = m->x0, g_game.hazard_y = m->y0, g_game.hazard_facing_right = m->flags & FACING_RIGHT;
    }
  }
  h->damage_mode = DAMAGE_FULL;
  if (h->enter_without_input) h->enter_without_input = false;   /* (something in the room gives control back) */
  else h->accepting_input = true;
}

/* ---------------------------------------------------------------- damage (TakeDamage, StartRecoil, Invulnerable) */
#define RECOIL_VELOCITY 15.0f
#define INVUL_TIME 1.3f
#define INVUL_TIME_STAL 1.75f   /* (Stalwart Shell) */
#define DAMAGE_FREEZE_DOWN 0.001f
#define PULSE_DURATION 0.1f

static bool can_take_damage(void) {
  Hero *h = &g_hero;
  return h->damage_mode != DAMAGE_NONE && h->transition_state == TS_WAITING_TO_TRANSITION && !h->cs.invulnerable &&
         !h->cs.recoiling && !h->cs.dead && !h->cs.hazard_death;
}

static void start_invulnerable(float duration) {
  Hero *h = &g_hero;
  if (!h->invuln_routine) {
    h->cs.invulnerable = true;
    h->invuln_freeze = DAMAGE_FREEZE_DOWN, h->invuln_time = duration;
    h->invuln_routine = true;
  } else if (h->invuln_freeze + h->invuln_time < duration + DAMAGE_FREEZE_DOWN) {
    if (h->invuln_freeze > 0) h->invuln_freeze = DAMAGE_FREEZE_DOWN;
    else duration += DAMAGE_FREEZE_DOWN;
    h->invuln_time = duration;
  }
}

/* (the Invulnerable coroutine, and InvulnerablePulse) */
static void invulnerable_tick(void) {
  Hero *h = &g_hero;
  if (!h->invuln_routine) return;
  if (h->invuln_freeze > 0) {
    h->invuln_freeze -= DT;
    if (h->invuln_freeze <= 0) h->pulsing = true, h->pulse_t = 0;
    return;
  }
  h->invuln_time -= DT;
  if (h->pulsing) {
    if (!h->pulse_reverse) {
      h->pulse_t += DT;
      if (h->pulse_t > PULSE_DURATION) h->pulse_t = PULSE_DURATION, h->pulse_reverse = true;
    } else {
      h->pulse_t -= DT;
      if (h->pulse_t < 0) h->pulse_t = 0, h->pulse_reverse = false;
    }
  }
  if (h->invuln_time <= 0) {
    h->pulsing = false, h->pulse_t = 0;
    h->cs.invulnerable = false;
    h->cs.recoiling = false;
    h->invuln_routine = false;
  }
}

static void die_from_hazard(void) {
  Hero *h = &g_hero;
  if (h->cs.hazard_death) return;
  set_state(HS_NO_INPUT);
  h->cs.hazard_death = true;
  reset_motion();
  h->hard_landing_timer = 0;
  affected_by_gravity(false);
  h->hidden = true;
  game_player_dead_from_hazard();
}

static void start_recoil(int side) {
  Hero *h = &g_hero;
  if (h->cs.recoiling) return;
  reset_motion();
  affected_by_gravity(false);
  if (side == SIDE_LEFT) {
    h->recoil_vx = RECOIL_VELOCITY, h->recoil_vy = RECOIL_VELOCITY * 0.5f;
    if (h->cs.facing_right) flip_sprite();
  } else if (side == SIDE_RIGHT) {
    h->recoil_vx = -RECOIL_VELOCITY, h->recoil_vy = RECOIL_VELOCITY * 0.5f;
    if (!h->cs.facing_right) flip_sprite();
  } else
    h->recoil_vx = h->recoil_vy = 0;
  set_state(HS_NO_INPUT);
  h->cs.recoil_frozen = true;
  start_invulnerable(charm_on(4) ? INVUL_TIME_STAL : INVUL_TIME);
  game_freeze_moment();   /* (then hero_recoil_unfreeze) */
}

void hero_recoil_unfreeze(void) {
  g_hero.cs.recoil_frozen = false;
  g_hero.cs.recoiling = true;
}

/* TakeDamageCharmEffects: Grubsong's soul */
#define GRUB_SOUL_MP 15
static void add_mp_charge(int amount);
static void take_damage_charm_effects(int damage) {
  if (charm_on(3) && damage > 0) add_mp_charge(GRUB_SOUL_MP);
}

/* the Fury FSM (Charm Effects) as the Knight is hurt: Fury of the Fallen on at one mask (none of them lifeblood) */
static void fury_check(void) {
  if (!g_hero.fury) g_hero.fury = charm_on(6) && g_pd.health == 1;
}

/* PlayerData.TakeHealth: lifeblood first */
static void take_health(int damage) {
  if (g_pd.health_blue > 0) {
    int rest = damage - g_pd.health_blue;
    g_pd.health_blue = (int8_t)(g_pd.health_blue > damage ? g_pd.health_blue - damage : 0);
    if (rest > 0) take_health(rest);
  } else
    g_pd.health = (int8_t)(g_pd.health > damage ? g_pd.health - damage : 0);
}

void hero_take_damage(int side, int damage, int hazard) {
  Hero *h = &g_hero;
  if (damage <= 0) return;
  if (h->parry_t > 0 && hazard == HAZ_NORMAL) return;
  if (can_take_damage()) {
    if (h->damage_mode == DAMAGE_HAZARD_ONLY && hazard == HAZ_NORMAL) return;
    spell_cancel_regain();   /* (HERO DAMAGED: Spell Control's Cancel All) */
    map_cancel();
    vm_broadcast(VMEV_HERO_DAMAGED);
    inv_damage();
    cancel_attack();
    if (h->cs.recoiling_left || h->cs.recoiling_right) cancel_recoil_horizontal();
    if (h->cs.bouncing) cancel_bounce(), h->body.vy = 0;
    take_health(pd_flag(PDF_OVERCHARMED) ? damage * 2 : damage);
    take_damage_charm_effects(damage);
    fury_check();
    if (g_pd.health == 0) {
      hero_die();
      return;
    }
    if (hazard == HAZ_SPIKES || hazard == HAZ_ACID || hazard == HAZ_PIT) die_from_hazard();
    else if (hazard != HAZ_LAVA) start_recoil(side);
  } else if (h->cs.invulnerable && !h->cs.hazard_death && (hazard == HAZ_SPIKES || hazard == HAZ_ACID)) {
    /* (spikes and acid hurt even through invulnerability) */
    take_health(damage);
    take_damage_charm_effects(damage);
    vm_broadcast(VMEV_HERO_DAMAGED);   /* (HeroCtrl-HeroDamaged) */
    fury_check();
    if (g_pd.health == 0) hero_die();
    else die_from_hazard();
  }
}

/* HeroController.HazardRespawn: back at the hazard marker, standing */
void hero_hazard_respawn(void) {
  Hero *h = &g_hero;
  h->doing_hazard_respawn = true;
  set_state(HS_NO_INPUT);
  reset_look();
  h->cs.hazard_death = false;
  h->cs.on_ground = true;
  h->cs.hazard_respawning = true;
  reset_motion();
  h->hard_landing_timer = 0;
  reset_attacks();
  reset_input();
  h->cs.recoiling = false;
  h->air_dashed = false;
  /* FindGroundPoint (extended) */
  PhysHit hit;
  float x = g_game.hazard_x, y = g_game.hazard_y;
  if (phys_ray(x, y, 0, -1, 50, CF_TERRAIN, &hit)) y = hit.y + h->body.hy - h->body.oy + 0.01f;
  h->body.x = x, h->body.y = y;
  h->body.ncontacts = 0;
  h->hidden = false;
  if (g_game.hazard_facing_right) face_right();
  else face_left();
  world_hero_in_position();
  start_invulnerable(INVUL_TIME * 2);
  game_fade(0, 0.5f, 0.1f);   /* (the camera's RESPAWN: FadeIn) */
  h->anim_control = false;
  anim_play(&h->anim, CLIP_KNIGHT_HAZARD_RESPAWN);
  h->respawn_timer = clip_duration(CLIP_KNIGHT_HAZARD_RESPAWN);
  h->respawning = true;
  cam_snap_to_hero();
}

/* HeroController.Respawn's end, the Knight not on a bench: he wakes up on the ground (Wake Up Ground), then has control */
void hero_wake_up_ground(void) {
  Hero *h = &g_hero;
  g_pd.disable_pause = true;
  reset_input();   /* (IgnoreInput) */
  h->accepting_input = false;
  anim_play_from_frame(&h->anim, CLIP_KNIGHT_WAKE_UP_GROUND, 0);
  h->anim_control = false;
  h->control_relinquished = true;
  h->wake_timer = clip_duration(CLIP_KNIGHT_WAKE_UP_GROUND);
}

void hero_face(bool right) {
  if (right) face_right();
  else face_left();
}
void hero_gravity(bool on) { affected_by_gravity(on); }
/* MaxHealth: whole again, the lifeblood as the charms give it (UpdateBlueHealth: Lifeblood Heart's two) */
void hero_max_health(void) {
  g_pd.health = g_pd.max_health;
  g_pd.health_blue = charm_on(8) ? 2 : 0;
}

/* ADD BLUE HEALTH (Blue Health Control) */
void hero_add_blue_health(void) {
  if (g_pd.health_blue < 8) g_pd.health_blue++;
}

/* FindGroundPoint: where the Knight stands on the ground below (x, y) (no ground: there) */
float hero_ground_y(float x, float y) {
  PhysHit hit;
  if (!phys_ray(x, y, 0, -1, 10, CF_TERRAIN, &hit)) return y;
  return hit.y + g_hero.body.hy - g_hero.body.oy + 0.01f;
}

static void respawn_tick(void) {
  Hero *h = &g_hero;
  if (h->wake_timer > 0 && (h->wake_timer -= DT) <= 0) {
    h->anim_control = true;
    h->anim_state = h->state;
    h->control_relinquished = false;
    hero_finished_entering_scene(true);
    g_pd.disable_pause = false;
  }
  if (!h->respawning) return;
  h->respawn_timer -= DT;
  if (h->respawn_timer > 0) return;
  h->respawning = false;
  h->cs.hazard_respawning = false;
  h->anim_control = true;
  hero_finished_entering_scene(false);
}

/* HeroBox: what touches the Knight's hurt box (hazards; enemies through world.c) */
void hero_check_damage(void) {
  Hero *h = &g_hero;
  if (h->hidden || h->cs.dead) return;
  float k = h->cs.facing_right ? -1.0f : 1.0f;
  float cx = h->body.x + 0.0055733f * k, cy = h->body.y - 0.6942673f;
  float x0 = cx - 0.2277069f, x1 = cx + 0.2277069f, y0 = cy - 0.5848932f, y1 = cy + 0.5848932f;
  int n;
  const Ent *es = room_ents(&n);
  for (int i = 0; i < n; i++) {
    const Ent *e = &es[i];
    if (e->type != ENT_DAMAGE) continue;
    if (!(x1 > e->x0 && x0 < e->x1 && y1 > e->y0 && y0 < e->y1)) continue;
    if (e->a) {
      /* (its outline: 4 points a record after it) */
      float pts[16];
      int np = (int)e->p2;
      for (int k = 0; k < np && k < 8; k++) {
        const Ent *r = &es[i + 1 + k / 4];
        const float *f = &r->x0;
        pts[2 * k] = f[2 * (k & 3)], pts[2 * k + 1] = f[2 * (k & 3) + 1];
      }
      if (!box_meets_shape(x0, y0, x1, y1, pts, np < 8 ? np : 8)) continue;
    }
    int side = (e->x0 + e->x1) / 2 > h->body.x ? SIDE_RIGHT : SIDE_LEFT;
    int hazard = (int)e->p0, damage = (int)e->p1;
    if (hazard == HAZ_NONE) {
      /* (buffered to LateUpdate) */
      h->hit_buffered = true, h->buffered_side = (int8_t)side, h->buffered_damage = (int8_t)damage, h->buffered_hazard = (int8_t)hazard;
    } else
      hero_take_damage(side, damage, hazard);
  }
  int side, damage = enemies_touch_hero(x0, y0, x1, y1, &side);
  if (damage) hero_take_damage(side, damage, HAZ_NORMAL);
}

/* PlayerData.AddMPCharge: the vessel fills, then the reserve */
static void add_mp_charge(int amount) {
  PlayerData *p = &g_pd;
  if (p->mp + amount > p->max_mp) {
    if (p->mp_reserve < p->mp_reserve_max) {
      p->mp_reserve = (int16_t)(p->mp_reserve + amount - (p->max_mp - p->mp));
      if (p->mp_reserve > p->mp_reserve_max) p->mp_reserve = p->mp_reserve_max;
    }
    p->mp = p->max_mp;
  } else
    p->mp = (int16_t)(p->mp + amount);
}

void hero_soul_gain(void) {
  if (g_pd.mp < g_pd.max_mp) add_mp_charge(charm_on(20) ? 14 : 11);   /* (Soul Catcher: more) */
  else add_mp_charge(charm_on(20) ? 8 : 6);
}
void hero_add_mp_charge(int amount) { add_mp_charge(amount); }

void hero_cancel_hero_jump(void) {
  if (!g_hero.cs.jumping) return;
  cancel_jump();
  if (g_hero.body.vy > 0) g_hero.body.vy = 0;
}

void hero_add_geo(int amount) { g_pd.geo += amount; }

void hero_add_health(int amount) {
  g_pd.health = (int8_t)(g_pd.health + amount >= g_pd.max_health ? g_pd.max_health : g_pd.health + amount);
}

/* ---------------------------------------------------------------- control (the FSMs take it and give it back) */
void hero_relinquish_control(void) {
  Hero *h = &g_hero;
  if (h->control_relinquished || h->cs.dead) return;
  reset_input();
  reset_motion();
  h->accepting_input = false;
  h->control_relinquished = true;
  reset_look();
  reset_attacks();
  h->touching_wall_l = h->touching_wall_r = false;
}

/* RelinquishControlNotVelocity: as RelinquishControl, its velocity kept */
void hero_relinquish_control_not_velocity(void) {
  Hero *h = &g_hero;
  if (h->control_relinquished || h->cs.dead) return;
  reset_input();
  h->accepting_input = false;
  h->control_relinquished = true;
  reset_look();
  reset_attacks();
  h->touching_wall_l = h->touching_wall_r = false;
}

/* CharmUpdate: the Knight as his charms make him, whole again (the other charms' effects: where they are read) */
void hero_charm_update(void) { hero_max_health(); }

void hero_regain_control(void) {
  Hero *h = &g_hero;
  /* (not while he dies or comes back from a hazard, as King's Pass's tablets would have it on any hit: those give
   * control back themselves) */
  if (h->cs.dead || h->wake_timer > 0) return;
  if (h->cs.hazard_death || h->cs.hazard_respawning) {
    h->control_relinquished = false;
    return;
  }
  h->accepting_input = true;
  h->state = HS_IDLE;
  if (!h->control_relinquished || h->cs.dead) return;
  affected_by_gravity(true);
  /* SetStartingMotionState */
  h->move_input = 0;
  h->cs.touching_wall = false;
  if (hero_touching_ground()) h->cs.on_ground = true, set_state(HS_GROUNDED), h->air_dashed = false;
  else h->cs.on_ground = false, set_state(HS_AIRBORNE);
  h->control_relinquished = false;
}

void hero_stop_anim_control(void) { g_hero.anim_control = false; }

void hero_start_anim_control(void) {
  Hero *h = &g_hero;
  if (h->cs.dead || h->cs.hazard_death || h->cs.hazard_respawning || h->wake_timer > 0) return;   /* (as above) */
  h->anim_state = h->state;
  if (!h->anim_control) {
    h->anim_control = true;
    play_idle();
  }
}

/* CanFocus, CanCast */
bool hero_can_focus(void) {
  const Hero *h = &g_hero;
  return !g_game.paused && h->state != HS_NO_INPUT && !h->cs.dashing && !(h->cs.attacking && h->attack_time < ATTACK_RECOVERY_TIME) &&
         !h->cs.recoiling && h->cs.on_ground && !h->cs.transitioning && !h->cs.recoil_frozen && !h->cs.hazard_death &&
         !h->cs.hazard_respawning && h->accepting_input;
}

bool hero_can_quick_map(void) {
  const Hero *h = &g_hero;
  return !g_game.paused && !h->control_relinquished && h->state != HS_NO_INPUT && !h->cs.dashing &&
         !(h->cs.attacking && h->attack_time < ATTACK_RECOVERY_TIME) && !h->cs.recoiling && !h->cs.transitioning &&
         !h->cs.hazard_death && !h->cs.hazard_respawning && !h->cs.recoil_frozen && h->cs.on_ground && h->accepting_input;
}

bool hero_can_cast(void) {
  const Hero *h = &g_hero;
  return !g_game.paused && h->prevent_cast <= 0 && !h->cs.dashing && h->state != HS_NO_INPUT && !(h->cs.attacking && h->attack_time < ATTACK_RECOVERY_TIME) &&
         !h->cs.recoiling && !h->cs.recoil_frozen && !h->cs.transitioning && !h->cs.hazard_death && !h->cs.hazard_respawning &&
         h->accepting_input;
}

void hero_late_update(void) {
  Hero *h = &g_hero;
  if (h->hit_buffered) {
    h->hit_buffered = false;
    hero_take_damage(h->buffered_side, h->buffered_damage, h->buffered_hazard);
  }
}
