/* The ride, as the game runs it: its tracks (levels, or the endless run's pieces one after another) and the bike as
 * Box2D bodies and joints in the game's own engine (phys.h), built and driven the way its Lua does it:
 *   code.GameCharacter  the bike: a main body with the bike's outline, two wheels on pistons, two wheel triggers and
 *                       four sensors welded on; the button's speed on the ground, the turning force in the air, the
 *                       turn dying down when let go, the wheels' damping, speed pads;
 *   code.Board          the push down in the air, the collision tables (which part touches which platform), landing,
 *                       flips, and the crashes: a hard knock where a sensor touches, the side of a ground, red saws;
 *   code.GameObject     the platforms: static, or dynamic held by a stiff touch joint that its animations move
 *                       frame by frame (turns set directly), or by a pivot that a touch takes away.
 * The frame: the touch, the physics step (with its contact events), then the timers and enterFrame listeners.
 *
 * The calculator holds the bike and the track near it: static pieces come in and go out as the bike moves (they
 * touch nothing else), moving ones are followed all the time and their bodies made near the bike. */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rider.h"
#include "data.h"

#define DT_MS (1000.0f / 60)
#define PI 3.14159265f
#define DEG (PI / 180)
#define CHAR_SCALE 0.7f                 /* gameCharacterScale */
#define WHEEL_X1 (-34 * CHAR_SCALE)     /* startingPlatformWheel1Position, scaled in setStartingState */
#define WHEEL_X2 (38 * CHAR_SCALE)
#define WHEEL_Y ((37 - 36 * 0.5f) * CHAR_SCALE * CHAR_SCALE) /* (group height - wheel half height) x0.7, x0.7 again */
#define WHEEL_R (12 * CHAR_SCALE)
#define TRIG_R 11.2f                    /* wheelTriggerRadius */
#define TRIG_DY 4                       /* wheelTriggerYOffset */
#define ACCEL 14                        /* accelerateDeltaValue */
#define ACCEL_BACK 30                   /* accelerateDeltaValueWhenMovingBackwards */
#define VMAX 550                        /* maxAccelerationVelocityValue */
#define DOWN_AIR (3.2f * 9.8f)          /* additionalInAirDownwardsVelocity x gravityYForceValue, px/s a frame */
#define DAMP_GROUND 3                   /* onGroudCharacterDamping */
#define AIR_TORQUE (-100.0f)            /* inAirAccelerationTorqueValue */
#define AIR_WMAX (-500.0f)              /* inAirAccelerationMaxTorqueValue (deg/s) */
#define AIR_DELAY 70                    /* delayBetweenAccelerationAndRotation */
#define STOP_ROT_MS 1000                /* stopBikeInAirRotationTime */
#define STOP_ROT_END 20                 /* stopBikeInAirRotationEndTimerDelay */
#define BOOST_FRAMES 30                 /* additionalBoosterDuration */
#define BOOST_V0 550                    /* initialBoosterAccelerationVelocityValue */
#define BOOST_ADD 2000                  /* additionalBoosterForwardVelocity */
#define BOOST_VMAX 1050                 /* maxAdditionalBoosterForwardVelocityValue */
#define FIRST_FLIP 270                  /* firstFlipDegreesAmount */
#define PERFECT_MS 30                   /* gameCharacterPerfectLandingErrorMargin */
#define BODY_LAND_MS 1000               /* flipTimeToLandOnWheelsAfterBodyTouch */
#define MAX_LANDINGS 3                  /* maxPlatformLandingAmount */
#define MAX_FORCE 10000                 /* maxRealImpactForce */
#define FRAME_DUR 16.6f                 /* defaultFrameDurationTime */
#define BASE_TOP (1600 + 107)           /* the ground base's top: groundBaseTopY + 107 on a 16:9 iPhone */
#define FALL_OUT (1600 + 800)           /* fallen past it, between its blocks: gone */

/* physicsObjectFilter */
enum { CAT_BODY = 1, CAT_MAIN = 2, CAT_WHEEL = 4, CAT_TRIG = 8, CAT_SENSOR = 16, CAT_PLATFORM = 32, CAT_PICKUP = 64,
       CAT_ANIMATED = 128 };
#define MASK_BODY 224
#define MASK_MAIN 64
#define MASK_WHEEL 160
#define MASK_TRIG 224
#define MASK_SENSOR 224
#define MASK_PLATFORM 61
#define MASK_PICKUP 29

/* fixture tags: the bike's parts, the ground base, then objects (16 + their slot) */
#define TAG_BASE 14
#define TAG_OBJ 16
#define TAG_POLYS 40 /* an object's polygons each have their tag (static ones come and go one by one) */

static rd_run *R; /* the run being stepped (the contact callbacks' world) */

uint32_t rd_rand(uint32_t *s) {
  uint32_t x = *s ? *s : 0x9E3779B9u;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return *s = x;
}

/* ---------------------------------------------------------------- the game's easings (code.config.easings: t, b, c, d) */
static float ease(int e, float t, float b, float c, float d) {
  switch (e) {
  case 1: /* inOutQuad */
    t = t / d * 2;
    if (t < 1) return c / 2 * t * t + b;
    return -c / 2 * ((t - 1) * (t - 3) - 1) + b;
  case 2: /* outBounce */
    t /= d;
    if (t < 0.36363636f) return c * (7.5625f * t * t) + b;
    if (t < 0.72727273f) return t -= 0.54545455f, c * (7.5625f * t * t + 0.75f) + b;
    if (t < 0.90909091f) return t -= 0.81818182f, c * (7.5625f * t * t + 0.9375f) + b;
    return t -= 0.95454545f, c * (7.5625f * t * t + 0.984375f) + b;
  case 3: /* outBack */
    t = t / d - 1;
    return c * (t * t * (2.70158f * t + 1.70158f) + 1) + b;
  case 4: t /= d; return c * t * t + b;            /* inQuad */
  case 5: t /= d; return -c * t * (t - 2) + b;      /* outQuad */
  case 6: return -c / 2 * (cosf(PI * t / d) - 1) + b; /* inOutSine */
  default: return c * t / d + b;                   /* linear */
  }
}

/* ---------------------------------------------------------------- objects */
static inline const rd_obj *O(const rd_run *r, int i) { return &rd_objs[r->live[i].obj]; }
static inline float seg_x(const rd_run *r, int i) { return (float)r->seg[r->live[i].seg].x; }

void obj_pose(const rd_run *r, int i, float *x, float *y, float *rot) {
  const rd_live *l = &r->live[i];
  if (l->dyn != NO_DYN) {
    const rd_dyn *d = &r->dyn[l->dyn];
    if (d->body) {
      float deg;
      ph_get(d->body, x, y, &deg);
      *rot = deg * DEG;
      return;
    }
    *x = d->x, *y = d->y, *rot = d->rot * DEG;
    return;
  }
  const rd_obj *o = O(r, i);
  *x = o->cx + seg_x(r, i), *y = o->cy, *rot = o->rot * DEG;
}
void obj_box(const rd_run *r, int i, float *x0, float *y0, float *x1, float *y1) {
  const rd_obj *o = O(r, i);
  float ox = seg_x(r, i);
  *x0 = o->x0 + ox, *y0 = o->y0, *x1 = o->x1 + ox, *y1 = o->y1;
  const rd_live *l = &r->live[i];
  if (l->dyn == NO_DYN) return;
  /* moved and turned: the box round its turned box */
  float x, y, a;
  obj_pose(r, i, &x, &y, &a);
  float hx = (o->x1 - o->x0) * 0.5f, hy = (o->y1 - o->y0) * 0.5f, rr = sqrtf(hx * hx + hy * hy);
  float mx = (o->x0 + o->x1) * 0.5f + ox - (o->cx + ox), my = (o->y0 + o->y1) * 0.5f - o->cy;
  float da = a - o->rot * DEG, c = cosf(da), s = sinf(da);
  float bx = x + c * mx - s * my, by = y + s * mx + c * my;
  *x0 = bx - rr, *x1 = bx + rr, *y0 = by - rr, *y1 = by + rr;
}

/* a template polygon of an object in its own frame (pixels round its centre): returns its points */
static int tpl_local(const rd_obj *o, int k, float *xy) {
  if (o->tpl == 0xFF) {
    if (k) return 0;
    float hw = fabsf(o->sx) / 2, hh = fabsf(o->sy) / 2, q[8] = {-hw, -hh, hw, -hh, hw, hh, -hw, hh};
    memcpy(xy, q, sizeof q);
    return 4;
  }
  int p = tpl_poly[o->tpl] + k;
  if (p >= tpl_poly[o->tpl + 1]) return 0;
  int v0 = poly_vert[p], n = poly_vert[p + 1] - v0;
  bool flip = (o->sx < 0) != (o->sy < 0);
  for (int j = 0; j < n; j++) {
    int v = v0 + (flip ? n - 1 - j : j);
    xy[2 * j] = tpl_xy[2 * v] * 0.25f * o->sx, xy[2 * j + 1] = tpl_xy[2 * v + 1] * 0.25f * o->sy;
  }
  return n;
}
/* the same in the world, where it is now (for drawing) */
int obj_poly(const rd_run *r, int i, int k, float *xy) {
  int n = tpl_local(O(r, i), k, xy);
  if (!n) return 0;
  float x, y, a;
  obj_pose(r, i, &x, &y, &a);
  float c = cosf(a), s = sinf(a);
  for (int j = 0; j < n; j++) {
    float px = xy[2 * j], py = xy[2 * j + 1];
    xy[2 * j] = x + c * px - s * py, xy[2 * j + 1] = y + s * px + c * py;
  }
  return n;
}

static bool animated(const rd_obj *o) { return o->anim != 0xFFFF; }
static bool dynamic(const rd_obj *o) { return animated(o) || (o->flags & F_ACTIVE); }

/* ---------------------------------------------------------------- timers (Corona's, fired in the frame's enterFrame) */
enum { TM_SET_GROUND, TM_DEATH_BEGAN, TM_DEATH_ENDED, TM_TOP_CLEAR, TM_PERFECT, TM_BODY_LAND, TM_STOP_ROT_END, TM_PIVOT };
static void timer_add(rd_run *r, float delay, int kind, int a, int b) {
  if (r->ntimer == MAXTIMER) return;
  rd_timer *t = &r->timer[r->ntimer++];
  t->at = r->ms + delay, t->kind = (uint8_t)kind, t->a = (uint16_t)a, t->b = (uint8_t)b;
}
static void timer_cancel(rd_run *r, int kind) {
  for (int i = 0; i < r->ntimer; i++)
    if (r->timer[i].kind == kind) r->timer[i] = r->timer[--r->ntimer], i--;
}

/* ---------------------------------------------------------------- the collision tables */
static void touch_add(rd_run *r, int table, int who, int what, int d) {
  for (int i = 0; i < r->ntouch; i++) {
    rd_touch *t = &r->touch[i];
    if (t->table == table && t->who == who && t->what == what) {
      t->n = (int16_t)(t->n + d);
      if (t->n == 0) r->touch[i] = r->touch[--r->ntouch]; /* (a count of 0 is taken out, as the game does) */
      return;
    }
  }
  if (d == 0 || r->ntouch == MAXTOUCH) return;
  rd_touch *t = &r->touch[r->ntouch++];
  t->table = (uint8_t)table, t->who = (uint8_t)who, t->what = (uint16_t)what, t->n = (int16_t)d;
}
static int touch_sum(const rd_run *r, int table, int who) { /* who < 0: anyone */
  int s = 0;
  for (int i = 0; i < r->ntouch; i++)
    if (r->touch[i].table == table && (who < 0 || r->touch[i].who == who)) s += r->touch[i].n;
  return s;
}

/* ---------------------------------------------------------------- the bike's state, as GameCharacter's methods */
static ph_body *MB(rd_run *r) { return r->b.part[BP_MAIN]; }
static bool in_air(const rd_run *r) { return !r->b.on_ground[BP_TRIG1] && !r->b.on_ground[BP_TRIG2]; }
static float main_rot(rd_run *r) { /* the body picture's rotation: the main body's, in degrees */
  float x, y, a;
  ph_get(MB(r), &x, &y, &a);
  return a;
}
static float lua_mod(float a, float m) { return a - floorf(a / m) * m; }
/* getForwardSpeed: |vx / cos(rotation)| */
static float forward_speed(rd_run *r) {
  float vx, vy;
  ph_vel(MB(r), &vx, &vy);
  return fabsf(vx / cosf(lua_mod(main_rot(r), 360) * PI / 180));
}
/* setGround(onGround, object) */
static void set_ground(rd_run *r, bool on, int part) {
  rd_bikestate *b = &r->b;
  if (!b->on_ground[part] && on && (part == BP_TRIG1 || part == BP_TRIG2)) b->landing_ms[part - BP_TRIG1] = r->ms;
  b->on_ground[part] = on;
}
static void cancel_stop_rotation(rd_run *r) {
  rd_bikestate *b = &r->b;
  b->stop_rot = false;
  ph_set_fixed_rot(MB(r), false);
}
static void stop_rotation(rd_run *r) {
  rd_bikestate *b = &r->b;
  cancel_stop_rotation(r);
  b->stop_rot_ms = r->ms + DT_MS, b->stop_rot_w0 = ph_avel(MB(r)); /* (let go before this frame's step) */
  b->stop_rot = true;
  ph_set_fixed_rot(MB(r), true);
}
/* the accelerate timer's function (every frame while held; first: the call made by accelerate itself) */
static void accel_tick(rd_run *r, bool first) {
  rd_bikestate *b = &r->b;
  float air_t = b->in_air_ms >= 0 ? r->ms - b->in_air_ms : 0;
  if (!in_air(r)) {
    if (b->boost) { b->speed = forward_speed(r); return; }
    if (forward_speed(r) > VMAX) return;
    if (b->accel_in_air > 0) { b->accel_in_air--, b->speed = forward_speed(r); return; }
    b->speed += (float)b->delta;
    if (b->speed > VMAX) b->speed = VMAX;
    float a = lua_mod(main_rot(r), 360) * PI / 180;
    ph_set_vel(MB(r), cosf(a) * b->speed, sinf(a) * b->speed);
  } else if (AIR_DELAY < air_t) {
    if (first) b->accel_in_air = 2;
    ph_set_fixed_rot(MB(r), false);
    if (AIR_WMAX < ph_avel(MB(r))) ph_torque(MB(r), AIR_TORQUE);
  }
}
static void accelerate(rd_run *r) {
  rd_bikestate *b = &r->b;
  b->accelerating = true, b->accel_fresh = true;
  b->speed = forward_speed(r);
  accel_tick(r, true);
}
static void stop_accelerate(rd_run *r) {
  rd_bikestate *b = &r->b;
  b->accelerating = false, b->accel_in_air = 0;
  if (!(b->on_ground[BP_TRIG1] || b->on_ground[BP_TRIG2])) stop_rotation(r);
  else cancel_stop_rotation(r);
}
static void set_boost(rd_run *r, float rot) {
  rd_bikestate *b = &r->b;
  if (b->boost) return;
  if (rot < 0) rot += 360;
  b->boost = true, b->boost_frames = BOOST_FRAMES, b->boost_first = 4;
  b->boost_rot = lua_mod(rot, 360) * PI / 180, b->boost_angle_set = false;
}
static bool two_wheels_landed(const rd_run *r) {
  return (touch_sum(r, T_TOP, BP_TRIG1) > 0) + (touch_sum(r, T_TOP, BP_TRIG2) > 0) == 2;
}

/* ---------------------------------------------------------------- texts and the score */
static void say(rd_run *r, const char *t, const char *t2) { r->text = t, r->text2 = t2, r->text_ms = 1500; }

/* GameController.flipMade: a point waiting for the landing (levels: half a second off the time) */
static void flip_made(rd_run *r, int n) {
  rd_bikestate *b = &r->b;
  b->air_flips += n, r->flips += n;
  if (r->mode == RUN_ENDLESS) r->pending += n;
}
/* Board.characterLanded's end: the texts, the flips counted (a perfect landing or not) */
static void landed_texts(rd_run *r, bool perfect) {
  rd_bikestate *b = &r->b;
  int n = b->air_flips;
  if (perfect) r->perfect++;
  if (n >= 2) {
    int k = n > 6 ? 6 : n, kind = perfect ? 0 : 1, m = flip_ntexts[(k - 2) * 2 + kind];
    say(r, flip_texts[k - 2][kind][rd_rand(&r->seed) % m], 0);
    r->doubles++;
  } else if (n == 1 && perfect) {
    say(r, "Perfect!", 0);
  } else if (n == 1 && b->front_flip) {
    say(r, front_texts[rd_rand(&r->seed) % 2], 0);
  }
  float air = b->in_air_ms >= 0 ? r->ms - b->in_air_ms : 0;
  if (air >= 2000 && n < 2) {
    int k = air >= 5000 ? 3 : air >= 4000 ? 2 : air >= 3000 ? 1 : 0;
    say(r, air_texts[k], r->text);
  }
  if (n > r->best_flips_jump) r->best_flips_jump = n;
  if (n && r->mode == RUN_ENDLESS) r->landed_flips += n, r->pending_ms = 500;
  b->air_flips = 0, b->front_flip = false;
  b->in_air_ms = -1;
}
static void landing_check(rd_run *r) { /* waitForLandingOrDelay: checkPerfectLanding, mergeHudBonusScore */
  rd_bikestate *b = &r->b;
  bool perfect = two_wheels_landed(r) && b->in_air_ms >= 0 && fabsf(b->landing_ms[0] - b->landing_ms[1]) < PERFECT_MS &&
                 b->air_flips > 0;
  landed_texts(r, perfect);
}
static void lose_bonus(rd_run *r) { /* hudResetBonusScore */
  rd_bikestate *b = &r->b;
  if (r->mode == RUN_ENDLESS) r->pending -= b->air_flips;
  b->air_flips = 0, b->front_flip = false;
}
/* Board.characterLanded(platform, the trigger or nothing) */
static void character_landed(rd_run *r, int obj, int trig) {
  r->body_touch = false;
  timer_cancel(r, TM_BODY_LAND);
  if (trig) {
    timer_cancel(r, TM_PERFECT);
    if (obj >= 0) { /* GameObject.setLanded */
      rd_live *l = &r->live[obj];
      if (l->landed != MAX_LANDINGS && two_wheels_landed(r)) l->landed++;
    }
    if (two_wheels_landed(r)) landing_check(r);
    else timer_add(r, PERFECT_MS, TM_PERFECT, obj < 0 ? 0xFFFF : obj, 0);
  } else {
    lose_bonus(r);
  }
}

/* ---------------------------------------------------------------- crashing */
static const char *crash_why;
static void crash(rd_run *r) {
  if (r->ended) return;
  rd_bikestate *b = &r->b;
  r->ended = END_CRASH, r->end_ms = 0, r->pending -= r->landed_flips + b->air_flips, r->landed_flips = 0;
  if (r->pending < 0) r->pending = 0;
  b->accelerating = false;
  /* the bike in pieces, as the game breaks it in six, and sparks */
  r->nbits = 0;
  float c = cosf(b->a), s = sinf(b->a);
  for (int i = 0; i < NBITS; i++) {
    rd_bit *p = &r->bits[r->nbits++];
    uint32_t q = rd_rand(&r->seed);
    float ang = (q & 1023) / 1023.0f * 2 * PI, sp = 60 + (q >> 10 & 255) * 1.2f;
    float lx = i < 6 ? (i % 3 - 1) * 26.0f * CHAR_SCALE : 0, ly = i < 6 ? (i / 3) * 12.0f * CHAR_SCALE - 4 : 0;
    p->x = b->x + c * lx - s * ly, p->y = b->y + s * lx + c * ly;
    p->vx = b->vx * 0.4f + cosf(ang) * sp, p->vy = b->vy * 0.4f + sinf(ang) * sp - 120;
    p->a = b->a, p->w = ((q >> 18 & 63) - 32) * 0.4f;
    p->c = i < 6 ? C_BODY : i < 10 ? C_WHITE : C_SPARK, p->size = i < 6 ? 12 : 2;
  }
  r->shake = 0.3f;
}

/* ---------------------------------------------------------------- the contact events (Board's listeners) */
static int part_of(uint16_t tag) { return tag < BP_N + 1 ? tag - 1 : -1; } /* tags 1..9: the bike's parts */
static int obj_of(uint16_t tag) { return tag >= TAG_OBJ ? (tag - TAG_OBJ) / TAG_POLYS : -1; }
static inline uint16_t tag_for(int i, int k) { return (uint16_t)(TAG_OBJ + i * TAG_POLYS + k); }
/* "the name" of an object, as gameObjectTypesPhysicsName gives it */
enum { NM_NONE, NM_GROUND, NM_TOP, NM_BOOST, NM_ENEMY, NM_FINISH, NM_PICKUP, NM_BASE };
static int name_of(const rd_run *r, uint16_t tag) {
  if (tag == TAG_BASE) return NM_BASE;
  int o = obj_of(tag);
  if (o < 0) return NM_NONE;
  switch (O(r, o)->kind) {
  case K_GROUND: return NM_GROUND;
  case K_PLATFORM: return NM_TOP;
  case K_BOOST: return NM_BOOST;
  case K_ENEMY: return NM_ENEMY;
  case K_FINISH: return NM_FINISH;
  default: return NM_PICKUP;
  }
}
static void obj_on_collision(rd_run *r, int o);
static void obj_on_precollision(rd_run *r, int o);
static void instant_death(rd_run *r) { crash_why = "deadly", crash(r); }

/* Board's "collision" listener, both phases */
static void collision(bool began, uint16_t ta, uint16_t tb) {
  rd_run *r = R;
  rd_bikestate *b = &r->b;
  int pa = part_of(ta), pb = part_of(tb);
#define PICK(cond) ((pa >= 0 && (cond(pa))) ? pa : (pb >= 0 && (cond(pb))) ? pb : -1)
#define IS_WHEEL(p) ((p) == BP_WHEEL1 || (p) == BP_WHEEL2)
#define IS_TRIG(p) ((p) == BP_TRIG1 || (p) == BP_TRIG2)
#define IS_MAIN(p) ((p) == BP_MAIN)
#define IS_SENS(p) ((p) >= BP_FRONT)
  int wheel = PICK(IS_WHEEL), trig = PICK(IS_TRIG), mainb = PICK(IS_MAIN);
  int sens = -1; /* sensorFront, sensorBottom, sensorTop, sensorBack: the first found in that order */
  for (int s = BP_FRONT; s <= BP_BACK && sens < 0; s++)
    if (pa == s || pb == s) sens = s;
  int na = name_of(r, ta), nb = name_of(r, tb);
#define FIND(nm) (na == (nm) ? obj_of(ta) : nb == (nm) ? obj_of(tb) : -2)
  int ground = FIND(NM_GROUND), top = FIND(NM_TOP), boost = FIND(NM_BOOST), enemy = FIND(NM_ENEMY);
  int finish = FIND(NM_FINISH), pickup = FIND(NM_PICKUP);
  bool base = na == NM_BASE || nb == NM_BASE;
  bool g = ground != -2, t = top != -2, bo = boost != -2, en = enemy != -2;
  if (!r->finish_passed && (wheel >= 0 || trig >= 0 || mainb >= 0) && finish != -2) {
    r->finish_passed = true;
    if (!r->ended) r->ended = END_FINISH, r->end_ms = 0;
    return;
  }
  int oa = obj_of(ta), ob = obj_of(tb);
  if (oa >= 0) obj_on_collision(r, oa);
  if (ob >= 0) obj_on_collision(r, ob);
  int d = began ? 1 : -1;
  if (trig >= 0 && (t || bo)) touch_add(r, T_TOP, trig, t ? top : boost, d);
  if (trig >= 0 && bo) touch_add(r, T_BOOST, trig, boost, d);
  if (trig >= 0 && (g || base || en)) touch_add(r, T_DEADLY, trig, g ? ground : base ? 0xFFFF : enemy, d);
  if (mainb >= 0 && (t || bo)) touch_add(r, T_BODY, 0, t ? top : boost, d);
  if (sens >= 0 && t) touch_add(r, T_SENS, sens, top, d);
  if (began) {
    bool just_landed = false;
    int n_trig = trig >= 0 ? touch_sum(r, T_TOP, trig) : -1, n_body = mainb >= 0 ? touch_sum(r, T_BODY, 0) : -1;
    if (trig >= 0 && n_trig == 1) just_landed = in_air(r), set_ground(r, true, trig);
    else if (wheel >= 0) just_landed = in_air(r), set_ground(r, true, wheel);
    else if (mainb >= 0 && n_body == 1) just_landed = in_air(r), timer_cancel(r, TM_SET_GROUND), timer_add(r, 80, TM_SET_GROUND, 0, 0);
    if (((trig >= 0 && n_trig == 0) || (mainb >= 0 && n_body == 0)) && (g || base || en)) {
      if (r->death_check) return;
      r->death_check = true;
      timer_add(r, 16, TM_DEATH_BEGAN, (uint16_t)((trig >= 0 ? trig : 0xFF) | (mainb >= 0 ? 0x100 : 0) | (wheel >= 0 ? 0x200 : 0)),
                (uint8_t)((en ? 1 : 0) | (g ? 2 : 0) | (base ? 4 : 0)));
      return;
    }
    if ((trig >= 0 || mainb >= 0) && t) {
      if (just_landed && trig >= 0) {
        if (top >= 0 && r->live[top].landed == MAX_LANDINGS) character_landed(r, top, 0);
        else character_landed(r, top, trig + 1);
      } else if (trig < 0) { /* the body touched first: the flips are lost unless the wheels follow within a second */
        if (b->air_flips > 0) {
          timer_cancel(r, TM_BODY_LAND);
          timer_add(r, BODY_LAND_MS, TM_BODY_LAND, top, 0);
          r->body_touch = true;
        }
      } else if (r->body_touch) {
        if (top >= 0 && r->live[top].landed == MAX_LANDINGS) character_landed(r, top, 0);
        else character_landed(r, top, trig + 1);
      }
    } else if (trig >= 0 && bo) {
      float x, y, a;
      obj_pose(r, boost, &x, &y, &a);
      set_boost(r, a / DEG); /* checkOnBoostLanding: setBoost(the pad's rotation) */
    } else if (!r->ended && (wheel >= 0 || mainb >= 0) && pickup != -2) {
      rd_live *l = &r->live[pickup];
      if (!(l->st & ST_GONE)) l->st |= ST_GONE, r->gems++;
    }
  } else if (!r->ended && (wheel >= 0 || mainb >= 0 || trig >= 0) && (t || bo)) {
    if (trig >= 0 && touch_sum(r, T_TOP, trig) == 0) {
      set_ground(r, false, trig);
      if (touch_sum(r, T_DEADLY, trig) > 0) timer_add(r, 16, TM_DEATH_ENDED, trig, 0);
      if (touch_sum(r, T_TOP, -1) == 0) { /* off the ground: where the flips count from */
        float rot = main_rot(r), v = rot + (360 - FIRST_FLIP);
        b->flip_zero = v - lua_mod(v, 360), b->flip_zero_set = true;
        b->flip_dir = 0, b->flip_very_start = v, b->flip_abs_start = rot;
      }
    } else if (mainb >= 0 && touch_sum(r, T_BODY, 0) == 0) {
      timer_cancel(r, TM_SET_GROUND);
      set_ground(r, false, BP_MAIN);
    } else if (wheel >= 0) {
      set_ground(r, false, wheel);
    }
  }
}
static void cb_begin(uint16_t ta, uint16_t ca, uint16_t tb, uint16_t cb) { (void)ca, (void)cb; collision(true, ta, tb); }
static void cb_end(uint16_t ta, uint16_t ca, uint16_t tb, uint16_t cb) { (void)ca, (void)cb; collision(false, ta, tb); }
/* Board's "preCollision": the objects hear it when the bike or a platform top is in it */
static void cb_pre(uint16_t ta, uint16_t tb) {
  rd_run *r = R;
  int pa = part_of(ta), pb = part_of(tb);
  bool bike = (pa >= 0 && pa <= BP_TRIG2) || (pb >= 0 && pb <= BP_TRIG2);
  if (!bike && name_of(r, ta) != NM_TOP && name_of(r, tb) != NM_TOP) return;
  int oa = obj_of(ta), ob = obj_of(tb);
  if (oa >= 0) obj_on_precollision(r, oa);
  if (ob >= 0) obj_on_precollision(r, ob);
}
/* the main body's "postCollision" (postCollisionOnBoard): a knock harder than its sensor's threshold breaks it */
static void cb_post(uint16_t ta, uint16_t ca, uint16_t tb, uint16_t cb, float force) {
  rd_run *r = R;
  (void)ca, (void)cb;
  uint16_t other;
  if (part_of(ta) == BP_MAIN) other = tb;
  else if (part_of(tb) == BP_MAIN) other = ta;
  else return;
  if (r->ended || force > MAX_FORCE) return;
  int thr = -1, which = -1;
  if (touch_sum(r, T_SENS, BP_TOP) > 0) thr = 0, which = BP_TOP; /* deathImpactThresholdTop */
  else {
    if (touch_sum(r, T_SENS, BP_FRONT) > 0) thr = 50, which = BP_FRONT;
    if (touch_sum(r, T_SENS, BP_BOTTOM) > 0 && (thr < 0 || thr < 150)) thr = 150, which = BP_BOTTOM;
    if (touch_sum(r, T_SENS, BP_BACK) > 0 && (thr < 0 || thr < 50)) thr = 50, which = BP_BACK;
  }
  if (thr < 0) return;
  int o = obj_of(other);
  if (o >= 0 && (O(r, o)->flags & F_SAFE)) return; /* dontDieOnCollision */
  if (force <= (float)thr) return;
  if ((r->top_frame >= 0 && (uint32_t)(r->top_frame + 1) < r->frame) || which != BP_TOP) {
    timer_cancel(r, TM_TOP_CLEAR);
    r->top_frame = -1;
    crash_why = which == BP_TOP ? "top" : which == BP_BOTTOM ? "bottom" : "front/back";
    crash(r); /* overTheBackGameOver */
  } else if (r->top_frame < 0) {
    timer_add(r, 50, TM_TOP_CLEAR, 0, 0);
    r->top_frame = (int)r->frame;
  }
}
static const ph_listener listener = {cb_begin, cb_end, cb_pre, cb_post};

/* ---------------------------------------------------------------- objects in the physics world */
static ph_fixdef plat_fix(bool anim, int tag) {
  ph_fixdef f = {2, 0, 0, anim ? CAT_ANIMATED : CAT_PLATFORM, MASK_PLATFORM, false, (uint16_t)tag}; /* physics.json */
  return f;
}
/* polygon k of a static object in the world (gems and the finish: their box): its points, 0 past the last */
static int static_poly(const rd_run *r, int i, int k, float *xy) {
  const rd_obj *o = O(r, i);
  float ox = seg_x(r, i);
  float c = cosf(o->rot * DEG), s = sinf(o->rot * DEG);
  int n;
  if (o->kind == K_GEM || o->kind == K_FINISH) {
    if (k) return 0;
    /* GamePickup.addPhysics: a sensor box, 0.6 x 1.1 of its picture; finishObject: its rectangle, not turned */
    float f = o->kind == K_GEM ? 0.66f : 1, hw = fabsf(o->sx) * f / 2, hh = fabsf(o->sy) * f / 2;
    float q[8] = {-hw, -hh, hw, -hh, hw, hh, -hw, hh};
    if (o->kind == K_FINISH) c = 1, s = 0;
    memcpy(xy, q, sizeof q), n = 4;
  } else if (!(n = tpl_local(o, k, xy))) return 0;
  for (int j = 0; j < n; j++) {
    float px = xy[2 * j], py = xy[2 * j + 1];
    xy[2 * j] = o->cx + ox + c * px - s * py, xy[2 * j + 1] = o->cy + s * px + c * py;
  }
  return n;
}
static void static_in(rd_run *r, int i, int k, const float *xy, int n) {
  const rd_obj *o = O(r, i);
  ph_fixdef f;
  if (o->kind == K_GEM || o->kind == K_FINISH) {
    ph_fixdef q = {0, 0, 0, CAT_PICKUP, MASK_PICKUP, true, tag_for(i, k)};
    f = q;
  } else f = plat_fix(false, tag_for(i, k));
  ph_poly(ph_ground(), xy, n, &f);
}
static void static_out(rd_run *r, int i) {
  uint64_t *m = &r->pmask[i];
  for (int k = 0; *m; k++)
    if (*m >> k & 1) ph_free_tag(ph_ground(), tag_for(i, k)), *m &= ~((uint64_t)1 << k);
}
/* a moving object's body (GameObject.addPhysics) */
static void dyn_in(rd_run *r, int i) {
  rd_live *l = &r->live[i];
  rd_dyn *d = &r->dyn[l->dyn];
  const rd_obj *o = O(r, i);
  bool anim = animated(o);
  float bx = d->x, by = d->y;
  if (anim) bx = d->tx, by = d->ty; /* (where its touch joint holds it) */
  d->body = ph_body_new(PH_DYNAMIC, bx, by, d->rot);
  ph_fixdef f = plat_fix(anim, tag_for(i, 0));
  float xy[64];
  for (int k = 0;; k++) {
    int n = tpl_local(o, k, xy);
    if (!n) break;
    ph_poly(d->body, xy, n, &f);
  }
  if (d->flags & DY_PIVOT) {
    float c = cosf(d->rot * DEG), s = sinf(d->rot * DEG);
    (void)c, (void)s;
    d->pivot = ph_pivot(ph_ground(), d->body, o->cx + seg_x(r, i) + o->pvx, o->cy + o->pvy);
    if (o->flags & F_REMOVE) ph_set_fixed_rot(d->body, true);
  }
  if (anim) {
    ph_set_gravity(d->body, 0);
    ph_set_fixed_rot(d->body, true);
    d->touch = ph_touch(d->body, bx, by);
    ph_touch_tune(d->touch, 11111, 1);
    if (d->tx != bx || d->ty != by) ph_touch_set(d->touch, d->tx, d->ty);
  }
}
static void dyn_out(rd_run *r, rd_dyn *d) {
  if (!d->body) return;
  float x, y, rot;
  ph_get(d->body, &x, &y, &rot);
  d->x = x, d->y = y, d->rot = rot;
  ph_body_free(d->body); /* (its joints go with it) */
  d->body = 0, d->touch = 0, d->pivot = 0;
}

/* ---------------------------------------------------------------- animations (GameObject.animateObjectProperty) */
static void anim_listen(rd_run *r, int i, bool on) {
  rd_live *l = &r->live[i];
  if (l->dyn == NO_DYN) return;
  rd_dyn *d = &r->dyn[l->dyn];
  for (int k = 0; k < 3; k++) d->an[k].on = on && (k == 0 ? rd_anims[O(r, i)->anim].nrot : k == 1 ? rd_anims[O(r, i)->anim].nx : rd_anims[O(r, i)->anim].ny) > 0;
}
/* GameObject.onCollision: a touch starts (or stops) its animations */
static void obj_on_collision(rd_run *r, int o) {
  rd_live *l = &r->live[o];
  if (l->dyn == NO_DYN || !animated(O(r, o))) return;
  rd_dyn *d = &r->dyn[l->dyn];
  const rd_anim *a = &rd_anims[O(r, o)->anim];
  int ns[3] = {a->nrot, a->nx, a->ny};
  for (int k = 0; k < 3; k++) {
    rd_anst *s = &d->an[k];
    if (!ns[k]) continue;
    if (s->on_touch) s->on_touch = 0, s->on = 1;
    else if (a->stop_touch && s->on) s->on = 0;
  }
}
/* GameObject.onPreCollision: the pivot of a platform that falls once touched goes (after physicsActiveDelay) */
static void obj_on_precollision(rd_run *r, int o) {
  rd_live *l = &r->live[o];
  if (l->dyn == NO_DYN) return;
  rd_dyn *d = &r->dyn[l->dyn];
  const rd_obj *ob = O(r, o);
  if (!(d->flags & DY_PIVOT) || !(ob->flags & F_REMOVE) || d->pivot_ms >= 0) return;
  d->pivot_ms = r->ms + (ob->delay >= 0 ? ob->delay : 0); /* (no delay: right after the step) */
}
static void anim_frame(rd_run *r, int i, int k) {
  rd_dyn *d = &r->dyn[r->live[i].dyn];
  const rd_obj *o = O(r, i);
  const rd_anim *a = &rd_anims[o->anim];
  rd_anst *s = &d->an[k];
  int ns = k == 0 ? a->nrot : k == 1 ? a->nx : a->ny;
  const rd_step *st0 = &rd_steps[a->first + (k > 0 ? a->nrot : 0) + (k > 1 ? a->nx : 0)];
  float scale = k ? 0.5f : 1; /* the position deltas are halved */
  const rd_step *st = &st0[s->cur];
  int delay_f = (int)roundf(st->delay / FRAME_DUR), time_f = (int)roundf(st->ms / FRAME_DUR);
  int state = (0 < delay_f && !s->delay_done && s->t < delay_f) ? 2 : 3; /* checkAnimate */
  if (state == 3) s->delay_done = 1;
  if (isnan(s->start)) s->start = k == 0 ? d->rot : k == 1 ? d->tx : d->ty;
  if (state == 3) {
    float tt = (float)(s->t - delay_f);
    float v = ease(st->ease, tt, s->start, st->delta * scale, (float)time_f);
    if (0 < tt) {
      if (k == 1) d->tx = v;
      else if (k == 2) d->ty = v;
      else {
        float ox = o->cx + seg_x(r, i), oy = o->cy, ax = ox + a->ax, ay = oy + a->ay;
        if (ax != ox || ay != oy) {
          float ang = (v - o->rot) * DEG, c = cosf(ang), sn = sinf(ang), dx = ox - ax, dy = oy - ay;
          d->tx = dx * c - dy * sn + ax, d->ty = dx * sn + dy * c + ay;
        }
        d->rot = v;
        if (d->body) ph_set_rot(d->body, v);
      }
    }
  }
  if (time_f + delay_f <= s->t) {
    s->t = 0;
    s->cur++;
    if (s->cur >= ns) {
      s->cur = 0;
      if (s->count) {
        s->count--;
        if (s->count == 0) s->on = 0;
      }
    }
    if (k == 1) d->tx = s->end;
    else if (k == 2) d->ty = s->end;
    else {
      d->rot = s->end;
      if (d->body) ph_set_rot(d->body, s->end);
    }
    if (s->cur == 0) s->orig = k == 0 ? o->rot : k == 1 ? o->cx + seg_x(r, i) : o->cy;
    else s->orig = k == 1 ? d->tx : k == 2 ? d->ty : s->end;
    s->end = s->orig + st0[s->cur].delta * scale;
    s->delay_done = 0, s->start = NAN;
  }
  s->t++;
}

/* ---------------------------------------------------------------- the world's objects: pieces of track */
static int dyn_new(rd_run *r) {
  for (int k = 0; k < MAXDYN; k++)
    if (!r->dyn[k].used) return k;
  return -1;
}
static void obj_free(rd_run *r, int i) {
  rd_live *l = &r->live[i];
  if (l->dyn != NO_DYN) dyn_out(r, &r->dyn[l->dyn]);
  else static_out(r, i);
  if (l->dyn != NO_DYN) r->dyn[l->dyn].used = 0;
  /* (the collision tables forget it) */
  for (int k = 0; k < r->ntouch; k++)
    if (r->touch[k].what == i) r->touch[k] = r->touch[--r->ntouch], k--;
  memset(l, 0, sizeof *l);
}
static void add_track(rd_run *r, int track) {
  if (r->nseg == MAXSEG) { /* forget the oldest piece */
    for (int i = 0; i < r->nlive; i++)
      if ((r->live[i].st & ST_USED) && r->live[i].seg == 0) obj_free(r, i);
    for (int i = 0; i < r->nlive; i++)
      if (r->live[i].st & ST_USED) r->live[i].seg--;
    memmove(r->seg, r->seg + 1, sizeof r->seg[0] * (MAXSEG - 1));
    r->nseg--;
  }
  const rd_track *t = &rd_tracks[track];
  int s = r->nseg++;
  r->seg[s].track = (int16_t)track, r->seg[s].x = r->end_x;
  /* canPickupBeSpawned: in the endless run, gems in every other piece */
  r->nchunks++;
  r->seg[s].gems = r->mode == RUN_LEVEL || r->nchunks % 2 == 0;
  int slot = 0;
  for (int k = 0; k < t->n; k++) {
    const rd_obj *o = &rd_objs[t->first + k];
    if (o->kind == K_GEM && !r->seg[s].gems) continue;
    while (slot < MAXLIVE && (r->live[slot].st & ST_USED)) slot++;
    if (slot == MAXLIVE) break;
    rd_live *l = &r->live[slot];
    memset(l, 0, sizeof *l);
    l->obj = (uint16_t)(t->first + k), l->seg = (uint8_t)s, l->st = ST_USED, l->dyn = NO_DYN;
    if (slot >= r->nlive) r->nlive = slot + 1;
    if (dynamic(o)) {
      int dk = dyn_new(r);
      if (dk >= 0) {
        rd_dyn *d = &r->dyn[dk];
        memset(d, 0, sizeof *d);
        d->used = 1, d->live = (uint16_t)slot, l->dyn = (uint8_t)dk;
        d->x = d->tx = o->cx + r->end_x, d->y = d->ty = o->cy, d->rot = o->rot;
        d->pivot_ms = -1, d->start_ms = -1;
        if (animated(o)) d->flags |= DY_ANIM;
        if (o->flags & F_PIVOT) d->flags |= DY_PIVOT;
        if ((o->flags & F_ACTIVE) && !(o->flags & F_PIVOT) && !animated(o)) d->flags |= DY_FREE;
        if (animated(o)) {
          const rd_anim *a = &rd_anims[o->anim];
          int ns[3] = {a->nrot, a->nx, a->ny};
          float v0[3] = {o->rot, o->cx + r->end_x, o->cy};
          int first[3] = {a->first, a->first + a->nrot, a->first + a->nrot + a->nx};
          for (int q = 0; q < 3; q++) {
            rd_anst *an = &d->an[q];
            an->start = NAN, an->t = 1, an->orig = v0[q];
            if (!ns[q]) continue;
            an->end = v0[q] + rd_steps[first[q]].delta * (q ? 0.5f : 1);
            an->count = a->loops == -1 ? 0 : a->loops < 0 ? (int16_t)-1000 : a->loops; /* (-1 and nil: for ever) */
            if (a->on_touch) an->on_touch = 1;
          }
        }
      }
    }
    slot++;
  }
  r->end_x += t->width;
}
static void endless_more(rd_run *r) {
  int g = 0;
  for (int i = 1; i < 4; i++)
    if (r->score >= endless_from[i]) g = i;
  int at = 0;
  for (int i = 0; i < g; i++) at += endless_n[i];
  uint32_t k = rd_rand(&r->seed) % endless_n[g];
  add_track(r, NLEVELS + endless_pick[at + k]);
}

/* ---------------------------------------------------------------- the near track: in and out of the physics world */
#define RESERVE 16000      /* arena bytes kept free: for the step, and the picture between steps */
#define EMERGENCY 6000     /* short of memory: what is right round the bike still comes in above this */
#define PROXY_BUDGET 140   /* fixtures in the physics world, at most (the tree holds B2_TREE_NODES / 2) */
/* the bike goes 18 pixels a frame at most: what is this near comes in some frames before it can touch it; short of
 * memory (many falling platforms and their contacts), only what is right round the bike stays */
static bool tight;
typedef struct {
  float x0, y0, x1, y1;
} box_t;
static bool near_box(float x0, float y0, float x1, float y1, const box_t *w, float m) {
  return x1 > w->x0 - m && x0 < w->x1 + m && y1 > w->y0 - m && y0 < w->y1 + m;
}
static void stream(rd_run *r, bool all) {
  rd_bikestate *b = &r->b;
  tight = ph_free() < RESERVE;
  /* the windows: round the bike, and round anything falling free (awake) */
  box_t win[MAXDYN + 1];
  int nw = 0;
  if (tight) win[nw++] = (box_t){b->x - 50, b->y - 55, b->x + 80, b->y + 55};
  else win[nw++] = (box_t){b->x - 80, b->y - 90, b->x + 150, b->y + 90};
  for (int k = 0; k < MAXDYN && !tight; k++) {
    rd_dyn *d = &r->dyn[k];
    box_t *w = &win[nw];
    if (d->used && d->body && (d->flags & DY_FREE) && ph_awake(d->body) && ph_bounds(d->body, &w->x0, &w->y0, &w->x1, &w->y1))
      w->x0 -= 50, w->y0 -= 50, w->x1 += 50, w->y1 += 50, nw++;
  }
  for (int i = 0; i < r->nlive; i++) {
    rd_live *l = &r->live[i];
    if (!(l->st & ST_USED)) continue;
    if (l->st & ST_GONE) {
      if (l->dyn == NO_DYN) static_out(r, i);
      continue;
    }
    float x0, y0, x1, y1;
    obj_box(r, i, &x0, &y0, &x1, &y1);
    float m = l->dyn != NO_DYN ? 60 : 0;
    bool in = false, keep = false;
    for (int w = 0; w < nw; w++) {
      in |= near_box(x0, y0, x1, y1, &win[w], tight ? 0 : m);
      keep |= near_box(x0, y0, x1, y1, &win[w], tight ? 0 : m + 60);
    }
    if (l->dyn != NO_DYN) { /* a moving object: all of it */
      if (all) in = keep;
      if (!(l->st & ST_IN) && in) {
        if (ph_proxies() >= PROXY_BUDGET || ph_free() < (tight ? EMERGENCY : RESERVE)) continue;
        dyn_in(r, i);
        l->st |= ST_IN;
      } else if ((l->st & ST_IN) && !keep) {
        rd_dyn *d = &r->dyn[l->dyn];
        if ((d->flags & DY_FREE) && ph_awake(d->body) && !tight) continue; /* (falling: kept till it rests) */
        dyn_out(r, d);
        l->st &= ~ST_IN;
      }
      continue;
    }
    if (!keep) { static_out(r, i); continue; }
    /* a static one: its polygons near a window in, the others out */
    float xy[64];
    for (int k = 0; k < TAG_POLYS; k++) {
      int n = static_poly(r, i, k, xy);
      if (!n) break;
      float a0 = xy[0], b0 = xy[1], a1 = xy[0], b1 = xy[1];
      for (int j = 1; j < n; j++) a0 = fminf(a0, xy[2 * j]), a1 = fmaxf(a1, xy[2 * j]), b0 = fminf(b0, xy[2 * j + 1]), b1 = fmaxf(b1, xy[2 * j + 1]);
      bool pin = false, pkeep = false;
      for (int w = 0; w < nw; w++) {
        pin |= near_box(a0, b0, a1, b1, &win[w], 0);
        pkeep |= near_box(a0, b0, a1, b1, &win[w], tight ? 0 : 60);
      }
      if (all) pin = pkeep;
      uint64_t bit = (uint64_t)1 << k;
      if (!(r->pmask[i] & bit) && pin) {
        if (ph_proxies() >= PROXY_BUDGET || ph_free() < (tight ? EMERGENCY : RESERVE)) continue;
        static_in(r, i, k, xy, n);
        r->pmask[i] |= bit;
      } else if ((r->pmask[i] & bit) && !pkeep) {
        ph_free_tag(ph_ground(), tag_for(i, k));
        r->pmask[i] &= ~bit;
      }
    }
  }
}
/* the ground base (backgroundModule.addGroundBaseObj): blocks every 1280 pixels from the track's start, their
 * physics 640 x 230 (pf_ground_base, not scaled to the 1280 x 460 picture); the ones round the bike */
static void base_stream(rd_run *r) {
  int32_t k = (int32_t)floorf(r->b.x / 1280);
  if (k == r->base_k) return;
  r->base_k = k;
  ph_free_tag(ph_ground(), TAG_BASE);
  for (int32_t j = k - 1; j <= k + 2; j++) {
    if (j < 0) continue;
    float x = (float)j * 1280;
    float q[8] = {x - 320, BASE_TOP, x + 320, BASE_TOP, x + 319.5f, BASE_TOP + 228, x - 320, BASE_TOP + 230};
    ph_fixdef f = {2, 0, 0, CAT_PLATFORM, MASK_PLATFORM, false, TAG_BASE};
    ph_poly(ph_ground(), q, 4, &f);
  }
}

/* ---------------------------------------------------------------- the bike's bodies (GameCharacter.setMotorPhysics) */
static ph_body *bike_body(float x, float y) { return ph_body_new(PH_DYNAMIC, x, y, 0); }
static void bike_poly(ph_body *b, const float *q, int n, float density, bool sensor, int cat, int mask, int part) {
  float xy[16];
  for (int j = 0; j < n; j++) xy[2 * j] = q[2 * j] * CHAR_SCALE, xy[2 * j + 1] = q[2 * j + 1] * CHAR_SCALE;
  ph_fixdef f = {density, 0, 0, (uint16_t)cat, (uint16_t)mask, sensor, (uint16_t)(part + 1)};
  ph_poly(b, xy, n, &f);
}
/* motor_main (physics_motors): the main body's weight and the four sensors, in the bike picture's pixels */
static const float S_TOP[] = {30.5f, -18.5f, 30.5f, 0.5f, -30.5f, 0.5f, -30.5f, -18.5f};
static const float S_BOTTOM[] = {38.5f, 2.5f, 54.5f, 18.5f, -54.5f, 18.5f, -38.5f, 2.5f};
static const float S_FRONT[] = {58.5f, 14.5f, 50.5f, 14.5f, 38.5f, 2.5f, 38.5f, -5.5f, 58.5f, -5.5f};
static const float S_BACK[] = {-38.5f, -5.5f, -38.5f, 2.5f, -50.5f, 14.5f, -58.5f, 14.5f, -58.5f, -5.5f};
static const float M_BODY[] = {38.5f, 0.5f, 38.5f, 12.5f, -38.5f, 12.5f, -38.5f, 0.5f};
static void bike_build(rd_run *r, float x, float y) {
  rd_bikestate *b = &r->b;
  /* built where the game builds it, at the origin (welds anchored there), then moved */
  b->part[BP_TOP] = bike_body(0, 0), bike_poly(b->part[BP_TOP], S_TOP, 4, 2, true, CAT_SENSOR, MASK_SENSOR, BP_TOP);
  b->part[BP_BOTTOM] = bike_body(0, 0), bike_poly(b->part[BP_BOTTOM], S_BOTTOM, 4, 0, true, CAT_SENSOR, MASK_SENSOR, BP_BOTTOM);
  b->part[BP_FRONT] = bike_body(0, 0), bike_poly(b->part[BP_FRONT], S_FRONT, 5, 0, true, CAT_SENSOR, MASK_SENSOR, BP_FRONT);
  b->part[BP_BACK] = bike_body(0, 0), bike_poly(b->part[BP_BACK], S_BACK, 5, 0, true, CAT_SENSOR, MASK_SENSOR, BP_BACK);
  ph_body *m = b->part[BP_MAIN] = bike_body(0, 0);
  const rd_bike *bk = &rd_bikes[r->bike];
  for (int h = 0; h < bk->hn; h++) { /* motor_body_<id>: density, bounce and friction 0 */
    int v0 = hull_poly[bk->h0 + h], n = hull_poly[bk->h0 + h + 1] - v0;
    float q[16];
    if (n > 8) n = 8;
    for (int j = 0; j < n; j++) q[2 * j] = hull_xy[2 * (v0 + j)] / 64.0f, q[2 * j + 1] = hull_xy[2 * (v0 + j) + 1] / 64.0f;
    bike_poly(m, q, n, 0, false, CAT_BODY, MASK_BODY, BP_MAIN);
  }
  bike_poly(m, M_BODY, 4, 3, false, CAT_MAIN, MASK_MAIN, BP_MAIN);
  ph_fixdef wf = {6, -1, 0, CAT_WHEEL, MASK_WHEEL, false, 0}; /* wheelDensity 6, bounce 0, friction unset (0.3) */
  b->part[BP_WHEEL1] = bike_body(WHEEL_X1, WHEEL_Y), wf.tag = BP_WHEEL1 + 1, ph_circle(b->part[BP_WHEEL1], 0, 0, WHEEL_R, &wf);
  b->part[BP_WHEEL2] = bike_body(WHEEL_X2, WHEEL_Y), wf.tag = BP_WHEEL2 + 1, ph_circle(b->part[BP_WHEEL2], 0, 0, WHEEL_R, &wf);
  ph_set_damping(b->part[BP_WHEEL1], DAMP_GROUND), ph_set_damping(b->part[BP_WHEEL2], DAMP_GROUND);
  ph_set_bullet(b->part[BP_WHEEL1], true), ph_set_bullet(b->part[BP_WHEEL2], true);
  ph_fixdef tf = {0, -1, 0, CAT_TRIG, MASK_TRIG, true, 0};
  b->part[BP_TRIG1] = bike_body(WHEEL_X1, WHEEL_Y + TRIG_DY), tf.tag = BP_TRIG1 + 1, ph_circle(b->part[BP_TRIG1], 0, 0, TRIG_R, &tf);
  b->part[BP_TRIG2] = bike_body(WHEEL_X2, WHEEL_Y + TRIG_DY), tf.tag = BP_TRIG2 + 1, ph_circle(b->part[BP_TRIG2], 0, 0, TRIG_R, &tf);
  ph_set_bullet(b->part[BP_TRIG1], true), ph_set_bullet(b->part[BP_TRIG2], true);
  ph_weld(b->part[BP_WHEEL1], b->part[BP_TRIG1], 0, 0);
  ph_weld(b->part[BP_WHEEL2], b->part[BP_TRIG2], 0, 0);
  ph_joint *p1 = ph_piston(m, b->part[BP_WHEEL1], WHEEL_X1, WHEEL_Y, 0, 1);
  ph_joint *p2 = ph_piston(m, b->part[BP_WHEEL2], WHEEL_X2, WHEEL_Y, 0, 1);
  ph_piston_limits(p1, -1, 2); /* wheelJointsRearLimits */
  ph_piston_limits(p2, -2, 0); /* wheelJointsFrontLimits */
  ph_weld(b->part[BP_FRONT], m, 0, 0);
  ph_weld(b->part[BP_BOTTOM], m, 0, 0);
  ph_weld(b->part[BP_TOP], m, 0, 0);
  ph_weld(b->part[BP_BACK], m, 0, 0);
  /* setPosition: everything moved by (x, y), the triggers under their wheels */
  for (int p = 0; p < BP_N; p++) {
    float px, py, a;
    ph_get(b->part[p], &px, &py, &a);
    ph_set_pos(b->part[p], px + x, py + y);
  }
}
/* setRotation: the main body and the wheels turned about the main body's centre (the sensors and triggers are not) */
static void bike_turn(rd_run *r, float rot) {
  rd_bikestate *b = &r->b;
  float cx, cy, a;
  ph_get(MB(r), &cx, &cy, &a);
  ph_set_rot(MB(r), rot);
  float c = cosf(rot * DEG), s = sinf(rot * DEG);
  for (int p = BP_WHEEL1; p <= BP_WHEEL2; p++) {
    float x, y;
    ph_get(b->part[p], &x, &y, &a);
    float dx = x - cx, dy = y - cy;
    ph_set_pos(b->part[p], dx * c - dy * s + cx, dx * s + dy * c + cy);
    ph_set_rot(b->part[p], rot);
  }
}
static void bike_read(rd_run *r) {
  rd_bikestate *b = &r->b;
  float deg;
  ph_get(MB(r), &b->x, &b->y, &deg);
  b->a = deg * DEG, b->w = ph_avel(MB(r)) * DEG;
  ph_vel(MB(r), &b->vx, &b->vy);
  for (int k = 0; k < 2; k++) ph_get(b->part[BP_WHEEL1 + k], &b->wx[k], &b->wy[k], &deg);
  b->ground = !in_air(r);
  if (b->x > b->max_x) b->max_x = b->x;
  b->odo += (b->vx * cosf(b->a) + b->vy * sinf(b->a)) / 60;
}

/* ---------------------------------------------------------------- a run */
void run_start(rd_run *r, int mode, int level, int bike, int theme, uint32_t seed) {
  memset(r, 0, sizeof *r);
  R = r;
  r->mode = mode, r->level = level, r->bike = bike, r->theme = theme, r->seed = seed ? seed : 1;
  r->top_frame = -1;
  ph_world_new();
  int first = mode == RUN_LEVEL ? level : NLEVELS; /* (the endless run starts with its start piece) */
  add_track(r, first);
  if (mode == RUN_ENDLESS)
    while (r->end_x < 2400) endless_more(r);
  const rd_track *t = &rd_tracks[first];
  rd_bikestate *b = &r->b;
  b->in_air_ms = -1;
  r->finish_x = mode == RUN_LEVEL ? (float)t->finish : 1e30f;
  r->bottom = FALL_OUT;
  bike_build(r, t->sx, t->sy);
  bike_turn(r, t->srot);
  bike_read(r);
  r->base_k = -99;
  base_stream(r);
  stream(r, false);
  r->camx = b->x - 200, r->camy = b->y - 260;
}

/* the game starts with the first touch (Board.GameStart): the physics runs, the animations start */
static void game_start(rd_run *r) {
  r->b.started = true;
  for (int k = 0; k < MAXDYN; k++) {
    rd_dyn *d = &r->dyn[k];
    if (!d->used || !(d->flags & DY_ANIM)) continue;
    const rd_anim *a = &rd_anims[O(r, d->live)->anim];
    if (a->on_touch || a->loops == 0) continue;
    d->start_ms = r->ms + a->start_delay;
  }
}

static void timers(rd_run *r) {
  rd_bikestate *b = &r->b;
  for (int i = 0; i < r->ntimer; i++) {
    rd_timer t = r->timer[i];
    if (t.at > r->ms + 0.01f) continue;
    r->timer[i] = r->timer[--r->ntimer], i--;
    switch (t.kind) {
    case TM_SET_GROUND: set_ground(r, true, BP_MAIN); break;
    case TM_DEATH_BEGAN: { /* the check 16 ms after touching the side of a ground, the ground base or a saw */
      r->death_check = false;
      int trig = t.a & 0xFF, mainb = t.a & 0x100, wheel = t.a & 0x200;
      bool tr = trig != 0xFF;
      int n_trig = tr ? touch_sum(r, T_TOP, trig) : -1, n_body = mainb ? touch_sum(r, T_BODY, 0) : -1;
      if (t.b & 1) { if (mainb || tr || wheel) instant_death(r); }
      else if ((tr && n_trig == 0) || (mainb && n_body == 0)) { if (t.b & 6) instant_death(r); }
      break;
    }
    case TM_DEATH_ENDED: { /* left the platform while on the side of a ground: dies, or looks again next frame */
      int n_top = touch_sum(r, T_TOP, t.a), n_dead = touch_sum(r, T_DEADLY, t.a);
      if (n_top == 0 && n_dead > 0) instant_death(r);
      else if (n_dead > 0) timer_add(r, 16, TM_DEATH_ENDED, t.a, 0);
      break;
    }
    case TM_TOP_CLEAR: r->top_frame = -1; break;
    case TM_PERFECT: landing_check(r); break;
    case TM_BODY_LAND: r->body_touch = false, character_landed(r, t.a == 0xFFFF ? -1 : t.a, 0); break;
    case TM_STOP_ROT_END: ph_set_fixed_rot(MB(r), false); break;
    }
  }
  if (b->accelerating && !b->accel_fresh) accel_tick(r, false); /* (its timer first fires the frame after) */
  b->accel_fresh = false;
}

void run_step(rd_run *r, bool held) {
  R = r;
  rd_bikestate *b = &r->b;
  ph_listen(&listener);
  if (r->text_ms > 0) r->text_ms -= DT_MS;
  /* the touch */
  bool was_held = b->held;
  b->held = held;
  if (!r->ended) {
    if (held && !was_held) {
      if (!b->started) game_start(r);
      accelerate(r);
    } else if (!held && was_held && b->started) stop_accelerate(r);
  }
  if (!b->started) { ph_listen(0); return; }
  if (!r->ended) r->ms += DT_MS;
  /* the physics step, with its contact events */
  ph_step();
  /* falling platforms let go (preCollision, after physicsActiveDelay) */
  for (int k = 0; k < MAXDYN; k++) {
    rd_dyn *d = &r->dyn[k];
    if (!d->used || d->pivot_ms < 0 || d->pivot_ms > r->ms + 0.01f) continue;
    d->flags &= (uint8_t)~DY_PIVOT, d->flags |= DY_FREE, d->pivot_ms = -1;
    if (d->body) {
      if (d->pivot) ph_joint_free(d->pivot), d->pivot = 0;
      ph_set_fixed_rot(d->body, false);
    }
  }
  /* enterFrame: the timers (the accelerate timer's tick among them) */
  timers(r);
  r->frame++;
  if (r->ended) {
    r->end_ms += DT_MS;
    for (int i = 0; i < r->nbits; i++) {
      rd_bit *p = &r->bits[i];
      p->vy += 294 * 1.5f * (1.0f / 60), p->x += p->vx / 60, p->y += p->vy / 60, p->a += p->w / 60;
    }
    if (r->shake > 0) r->shake -= 1.0f / 60;
  }
  if (!r->ended) {
    /* GameCharacter's enterFrame: the button's step by the way it goes, the wheels' damping */
    float vx, vy;
    ph_vel(MB(r), &vx, &vy);
    float ang = lua_mod(roundf(main_rot(r)), 360);
    b->delta = vx < 0 && (ang < 90 || ang > 270) ? ACCEL_BACK : ACCEL;
    if (b->stop_rot && !b->accelerating && ((b->on_ground[BP_TRIG1] || b->on_ground[BP_TRIG2]) || b->on_ground[BP_MAIN]))
      cancel_stop_rotation(r);
    for (int k = BP_WHEEL1; k <= BP_WHEEL2; k++)
      ph_set_damping(b->part[k], b->accelerating ? 0 : b->on_ground[k] ? DAMP_GROUND : 0);
    /* Board.setDownforce: the push down in the air */
    if (in_air(r)) {
      ph_vel(MB(r), &vx, &vy);
      ph_set_vel(MB(r), vx, vy + DOWN_AIR);
    }
    /* Board.trackMotor: when it left the ground, the flips */
    if (in_air(r)) {
      if (!b->in_air_flag || b->in_air_ms < 0) b->in_air_flag = true, b->in_air_ms = r->ms;
      if (b->flip_zero_set) {
        float rot = main_rot(r);
        if (fabsf(rot - b->flip_abs_start) >= FIRST_FLIP) {
          float r8 = rot + lua_mod(-b->flip_very_start, 360), r9 = r8 - b->flip_zero;
          int n = abs((int)floorf(r9 / 360));
          if (0 < n) {
            int dir = 0 < r9 ? 1 : -1;
            b->flip_zero = r8 - lua_mod(r8, 360);
            if (!b->flip_dir || b->flip_dir == dir) {
              flip_made(r, n);
              if (dir > 0) b->front_flip = true;
            }
            b->flip_dir = (int8_t)dir;
          }
        }
      }
    } else b->in_air_flag = false;
  }
  /* the animations (each object's listeners: rotation, x, y) */
  for (int k = 0; k < MAXDYN; k++) {
    rd_dyn *d = &r->dyn[k];
    if (!d->used || !(d->flags & DY_ANIM)) continue;
    if (d->start_ms >= 0 && d->start_ms <= r->ms + 0.01f) d->start_ms = -1, anim_listen(r, d->live, true);
    for (int q = 0; q < 3; q++)
      if (d->an[q].on) anim_frame(r, d->live, q);
    if (d->touch) ph_touch_set(d->touch, d->tx, d->ty);
  }
  if (!r->ended) {
    /* stopBikeRotation's listener: the turn eases out (outCirc) in a second, contacts can't turn it */
    if (b->stop_rot) {
      if (b->accelerating) cancel_stop_rotation(r);
      else {
        float t = r->ms - b->stop_rot_ms, q = t / STOP_ROT_MS - 1;
        float w = -b->stop_rot_w0 * sqrtf(fmaxf(0, 1 - q * q)) + b->stop_rot_w0;
        ph_set_avel(MB(r), w);
        if (STOP_ROT_MS < t) {
          b->stop_rot = false;
          ph_set_fixed_rot(MB(r), true);
          timer_add(r, STOP_ROT_END, TM_STOP_ROT_END, 0, 0);
        }
      }
    }
    /* setBoost's listener: a speed pad's push, its first 4 frames from the bike's turn */
    if (b->boost) {
      b->boost_frames--;
      float vx, vy, rot = lua_mod(main_rot(r), 360) * PI / 180;
      if (b->boost_first > 0) {
        b->boost_first--;
        vx = cosf(rot) * BOOST_V0, vy = sinf(rot) * BOOST_V0;
      } else {
        float cvx, cvy;
        ph_vel(MB(r), &cvx, &cvy);
        float ang = (b->boost_rot + rot) * 0.5f;
        if (PI < fabsf(b->boost_rot - rot)) ang += PI;
        if (!two_wheels_landed(r) && b->boost_angle_set) ang = b->boost_angle;
        else b->boost_angle = rot, b->boost_angle_set = true;
        float v = cvx / cosf(ang) + BOOST_ADD;
        if (BOOST_VMAX < v) v = BOOST_VMAX;
        vx = cosf(ang) * v, vy = sinf(ang) * v;
      }
      ph_set_vel(MB(r), vx, vy);
      if (b->boost_frames == 0) b->boost = false;
    }
  }
  ph_listen(0);
  bike_read(r);
  /* landed flips count 500 ms later (the game's bonusScoreWaitTimerDelay), a crash before loses them */
  if (r->landed_flips && (r->pending_ms -= DT_MS) <= 0)
    r->score += r->landed_flips, r->pending -= r->landed_flips, r->landed_flips = 0;
  if (!b->ground) b->air_ms += DT_MS;
  else b->air_ms = 0;
  if (!r->ended && b->y > r->bottom) crash_why = "fell", crash(r);
#ifdef HOST
  if (r->ended == END_CRASH && r->end_ms == 0 && crash_why && getenv("CRASH")) printf("crash: %s a %.1f\n", crash_why, b->a / DEG);
#endif
  /* falling platforms that fell out of the world go */
  for (int k = 0; k < MAXDYN; k++) {
    rd_dyn *d = &r->dyn[k];
    if (!d->used || !d->body || !(d->flags & DY_FREE)) continue;
    float x, y, a;
    ph_get(d->body, &x, &y, &a);
    if (y > FALL_OUT) dyn_out(r, d), r->live[d->live].st = (uint8_t)((r->live[d->live].st | ST_GONE) & ~ST_IN);
  }
  /* the trail */
  if (b->trail_n < TRAIL) b->trail_n++;
  memmove(b->trail_x + 1, b->trail_x, sizeof(float) * (TRAIL - 1));
  memmove(b->trail_y + 1, b->trail_y, sizeof(float) * (TRAIL - 1));
  b->trail_x[0] = b->wx[0], b->trail_y[0] = b->wy[0];
  /* the endless run goes on */
  if (r->mode == RUN_ENDLESS)
    while (r->end_x < b->x + 2400) endless_more(r);
  base_stream(r);
  stream(r, false);
  /* the camera: the bike a third in, a little below the middle */
  float tx = b->x + b->vx * 0.15f - 210, ty = b->y - 250;
  r->camx += (tx - r->camx) * 0.15f;
  r->camy += (ty - r->camy) * 0.12f;
}

float run_level_time(const rd_run *r) {
  float t = r->ms - r->flips * 500.0f;
  return t < 0 ? 0 : t;
}
