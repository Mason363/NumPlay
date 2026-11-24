/* Geometry Dash 2.x physics at 240 Hz.
 *
 * Constants and collision rules follow the reverse-engineered values used by
 * the gd3ds project (itself based on the Pathfinder mod's physics) for the
 * cube, ship, ball, UFO, wave and mini sizes, and the gdsolver project's
 * measurements of GD 2.2 for the robot, spider, swing and the ring / pad
 * table. Velocities are in GD units per second, "vy" is relative to the
 * current gravity. */
#include "game.h"
#include <math.h>
#include <string.h>

enum { SPEED_SLOW, SPEED_NORMAL, SPEED_FAST, SPEED_FASTER };
static const float SPEEDS[4] = {251.16007972f, 311.58009371f, 387.42014040f, 468.00013884f};
static const float SPEED_MULT[4] = {0.7f, 0.9f, 1.1f, 1.3f};
/* the physics step: 1/240 s, scaled by a time warp (GD scales dx, dy and
   dvy together) */
static float TDT = ND_DT;
static const float CUBE_JUMP[4] = {573.481728f, 603.7217172f, 616.681728f, 606.421728f};
static const float CUBE_GRAV[4] = {-2747.52f, -2794.1082f, -2786.4f, -2799.36f};
static const float VEL_THRESH[4] = {101.541492f, 103.485494592f, 103.377492f, 103.809492f};
enum { J_YPAD, J_YORB, J_BPAD, J_BORB, J_PPAD, J_PORB };
/* [speed][jump type][cube, ship, ball, UFO][normal, mini] (gd3ds; the ship
   and the UFO cap what they receive) */
static const float JUMPS[4][6][4][2] = {
  {{{864, 691.2f}, {432, 508.248f}, {518.4f, 414.72002f}, {573.48f, 458.784f}},
   {{573.48f, 458.784f}, {573.48f, 458.784f}, {401.436f, 321.148795f}, {573.48f, 458.784f}},
   {{-345.6f, -276.48f}, {-229.392f, -183.519f}, {-160.5744f, -128.463298f}, {-229.392f, -183.519f}},
   {{-229.392f, -183.519f}, {-229.392f, -183.519f}, {-160.5744f, -128.463298f}, {-229.392f, -183.519f}},
   {{561.6f, 449.28f}, {302.4f, 241.92f}, {362.88f, 290.30401f}, {345.6f, 276.4f}},
   {{412.884f, 330.318f}, {212.166f, 169.776f}, {309.0906f, 247.287596f}, {240.84f, 192.672f}}},
  {{{864, 691.2f}, {432, 508.248f}, {518.4f, 414.72002f}, {432, 691.2f}},
   {{603.72f, 482.976f}, {603.72f, 482.976f}, {422.604f, 338.08319f}, {603.72f, 482.976f}},
   {{-345.6f, -276.48f}, {-345.6f, -276.48f}, {-207.36f, -165.88801f}, {-345.6f, -276.48f}},
   {{-241.488f, -193.185f}, {-241.488f, -193.18f}, {-169.0416f, -135.2295f}, {-241.488f, -193.185f}},
   {{561.6f, 449.28f}, {302.4f, 241.92f}, {362.88f, 290.30401f}, {345.6f, 276.4f}},
   {{434.7f, 347.76f}, {223.398f, 178.686f}, {325.4202f, 260.3286f}, {258.984f, 207.198f}}},
  {{{864, 691.2f}, {432, 508.248f}, {518.4f, 414.72002f}, {432, 691.2f}},
   {{616.68f, 481.734f}, {616.68f, 481.734f}, {431.676f, 345.34079f}, {616.68f, 481.734f}},
   {{-345.6f, -276.48f}, {-345.6f, -276.48f}, {-207.36f, -165.88801f}, {-345.6f, -276.48f}},
   {{-246.672f, -197.343f}, {-246.672f, -197.343f}, {-172.6704f, -138.1401f}, {-246.672f, -197.343f}},
   {{561.6f, 449.28f}, {302.4f, 241.92f}, {362.88f, 290.30401f}, {345.6f, 276.4f}},
   {{443.988f, 355.212f}, {228.15f, 182.52f}, {332.3754f, 265.923f}, {258.984f, 207.198f}}},
  {{{864, 691.2f}, {432, 508.248f}, {518.4f, 414.72002f}, {432, 691.2f}},
   {{606.42f, 485.136f}, {606.42f, 485.136f}, {424.494f, 339.59519f}, {606.42f, 485.136f}},
   {{-345.6f, -276.48f}, {-345.6f, -276.48f}, {-207.36f, -165.88801f}, {-345.6f, -276.48f}},
   {{-242.568f, -194.049f}, {-242.568f, -194.049f}, {-169.7976f, -135.8343f}, {-242.568f, -194.049f}},
   {{561.6f, 449.28f}, {302.4f, 241.92f}, {362.88f, 290.30401f}, {345.6f, 276.4f}},
   {{436.644f, 349.272f}, {224.37f, 179.496f}, {326.8566f, 261.5004f}, {254.718f, 203.742f}}},
};
/* ball: gravity, the push when it flips, and how fast it rolls (degrees per second) */
#define BALL_GRAV (-1676.46672f)
#define BALL_HEIGHT 240.0f
#define BALL_AIR_SPIN 0.7f
/* swing (gdsolver): gravity 0.086 GD / tick (mini 0.129), terminal 8, a tap
   flips gravity with vp := -0.8 vp */
#define SWING_G 1114.56f
#define SWING_G_MINI 1671.84f
#define SWING_TERM 432.0f
static const float BALL_JUMP[4] = {-172.044007f, -181.11601f, -185.00401f, -181.92601f};
static const float BALL_ROLL[4] = {120 / (0.2f * 1.2405638f), 120 / 0.2f, 120 / (0.2f * 0.80424345f), 120 / (0.2f * 0.6657693f)};
/* UFO: flap strength and the two gravities (weak while rising fast) */
static const float UFO_FLAP[2] = {371.034f, 358.992f};
static const float UFO_GRAV_HI[2] = {-1676.84f, -1969.92f}, UFO_GRAV_LO[2] = {-1117.56f, -1308.96f};
/* ship in mini size */
#define SHIP_MINI_MIN (-406.566f)
#define SHIP_MINI_MAX 508.248f
#define ROT_SPEED 415.3848f
#define ROT_SPEED_MINI 540.0f
#define CEILING_INVUL 0.1f
#define DRAG_TIME 0.1f
#define SHIP_MIN (-345.6f)
#define SHIP_MAX 432.0f
#define END_START (10 * 30.0f)

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static float grav(const Player *p, float v) { return p->upside ? -v : v; }
/* half the player's size: 15, or 9 when mini */
static float phalf(const Player *p) {
  if (p->mode == MODE_WAVE) return p->mini ? 3.0f : 5.0f;
  if (p->mode == MODE_SPIDER) return p->mini ? 8.1f : 13.5f;   /* gdsolver */
  return p->mini ? 9.0f : 15.0f;
}
static float grav_bottom(const Player *p) { return p->upside ? -(p->y + phalf(p)) : p->y - phalf(p); }
static float grav_top(const Player *p) { return p->upside ? -(p->y - phalf(p)) : p->y + phalf(p); }
static float grav_floor(const Player *p) { return p->upside ? -p->ceiling_y : p->ground_y; }
/* How far above the ground the player rests (the wave rides 10 above it). */
static float rest_half(const Player *p) { return p->mode == MODE_WAVE ? (p->mini ? 6.0f : 10.0f) : phalf(p); }

bool game_used(const Game *g, const RObj *o, unsigned gi) {
  return (gi < MAX_LEVEL_OBJS && (g->used[gi >> 3] >> (gi & 7) & 1)) || obj_x(o) < g->used_below;
}
static void set_used(Game *g, unsigned gi) { if (gi < MAX_LEVEL_OBJS) g->used[gi >> 3] |= (uint8_t)(1u << (gi & 7)); }
static void fx(Game *g, uint8_t kind, uint8_t arg, int obj, float x, float y) {
  if (g->fx_count < sizeof(g->fx) / sizeof(g->fx[0])) g->fx[g->fx_count++] = (FxEvent){kind, arg, (int16_t)obj, x, y};
}
#ifdef ND_PROBE
/* diagnostics only (scratch tools): report deaths instead of dying */
int nd_noclip;
void (*nd_on_kill)(const Game *g, int obj);
#endif
static void kill(Game *g, int obj) {
#ifdef ND_PROBE
  if (nd_noclip) { if (nd_on_kill) nd_on_kill(g, obj); return; }
#endif
  if (g->dead) return;
  g->dead = true;
  g->death_obj = obj < 0 ? 0xffff : (uint16_t)obj;
  fx(g, FX_DEATH, 0, obj, g->p.x, g->p.y);
}

unsigned game_coin_index(const Level *L, unsigned obj) {
  unsigned k = 0;
  while (k < L->coin_count && L->coin_obj[k] < obj) k++;
  return k;
}

static void set_velocity(Player *p, float v, bool override) { p->vel_override = override; p->vy = p->mini ? v * 0.8f : v; }
static void landing(Player *p) { if (p->mode == MODE_CUBE) p->jumped = false; }
static void boost_latch(Player *p) { if (p->mode == MODE_SHIP || p->mode == MODE_UFO || p->mode == MODE_SWING) p->boost = true; }
/* the exemption ends once the speed (gravity frame) is back inside
   (-6.4, 8) GD units, 0.85 of that when mini */
static void boost_check(Player *p) {
  float k = p->mini ? 54 / 0.85f : 54;
  if (p->vy > -6.4f * k && p->vy < 8 * k) p->boost = false;
}
static void update_rot_dir(Player *p) { p->rot_dir_neg = p->upside; }
static float closest_rotation(float rot) {
  float r = fmodf(rot, 360.0f);
  if (r < 0) r += 360;
  float s = roundf(r / 90) * 90;
  s = fmodf(s, 360);
  return s < 0 ? s + 360 : s;
}
/* Nearest of base + k * 90 degrees. */
static float closest_rotation_to(float rot, float base) {
  float r = fmodf((float)(int)rot, 360.0f);
  if (r < 0) r += 360;
  float s = fmodf(roundf((r - base) / 90) * 90 + base, 360);
  return s < 0 ? s + 360 : s;
}
static float slerp_angle(float from, float to, float t) {
  float fx = cosf(from * .5f), fy = sinf(from * .5f), tx = cosf(to * .5f), ty = sinf(to * .5f);
  float dot = fx * tx + fy * ty;
  if (dot < 0) { dot = -dot; tx = -tx; ty = -ty; }
  float w0 = 1 - t, w1 = t;
  if (dot < 0.9999f) {
    float b = acosf(dot), sb = sinf(b);
    w0 = sinf(w0 * b) / sb;
    w1 = sinf(w1 * b) / sb;
  }
  return atan2f(w0 * fy + w1 * ty, w0 * fx + w1 * tx) * 2;
}
#define DEG (3.14159265f / 180.0f)

static void set_bounds_for_portal(Game *g, float portal_y, float height) {
  Player *p = &g->p;
  float v = (portal_y - (height + 60) / 2.0f) / 30.0f, c = ceilf(v);
  if (fabsf(v - roundf(v)) < 1e-6f) c += 1;
  p->ground_y = c > 0 ? c * 30 : 0;
  p->ceiling_y = p->ground_y + height;
  p->band_h = height;
  if (g->zoom != 1) p->ceiling_y = p->ground_y + height / g->zoom;
  g->cam_intended_y = (p->ground_y + p->ceiling_y) / 2 - (VIEW_H / 2 - GROUND_OFFSET);
}

void game_start(Game *g, const Level *L, bool first_attempt) {
  memset(g, 0, sizeof(*g));
  g->L = L;
  Player *p = &g->p;
  p->x = 0;
  p->y = 15;
  p->speed = SPEED_NORMAL;
  p->ceiling_y = 999999;
  p->coyote = 1 << 30;
  p->on_ground = true;
  p->sl_o = p->co_o = -1;
  if (L->start_mode == MODE_SHIP) {
    p->mode = MODE_SHIP;
    set_bounds_for_portal(g, 150, 300);
  }
  for (int c = 0; c < CH_COUNT; c++) {
    memcpy(g->ch[c].cur, L->colors[c], 3);
    memcpy(g->ch[c].to, L->colors[c], 3);
  }
  trig_start(g);
  g->attempt_camera = first_attempt;
  g->cam_x = first_attempt ? 15 : p->x - PLAYER_SCREEN_X;
  g->ground_x = g->bg_x = g->cam_x;
  g->death_obj = 0xffff;
  g->used_below = -1e9f;
}

float game_progress(const Game *g) {
  if (g->complete) return 100;
  const LevelExt *x = g->L->ext;
  float v = x && x->end_dist ? g->dist / x->end_dist * 100 : g->p.x / g->L->end_x * 100;
  return clampf(v, 0, 100);
}

static void clamp_ground(Game *g) {
  Player *p = &g->p;
  float h = rest_half(p);
  if (p->y - h < p->ground_y) {
    if (p->ceil_inv <= 0 && p->mode == MODE_CUBE && p->upside) kill(g, -1);
    if (grav(p, p->vy) <= 0) set_velocity(p, 0, p->mode == MODE_BALL);
    p->y = p->ground_y + h;
    p->snap_frame = 0;
  }
  if (p->y + h > p->ceiling_y) {
    if (p->ceil_inv <= 0 && p->mode == MODE_CUBE && !p->upside) kill(g, -1);
    if (grav(p, p->vy) >= 0) set_velocity(p, 0, p->mode == MODE_BALL);
    p->y = p->ceiling_y - h;
  }
}

/* Does the player's box touch a round hazard? GD tests the circle's centre
   inside the box, or a box corner inside the circle (so the reach is a
   little shorter straight above a saw than at its corners), as measured by
   gdsolver. */
static bool box_circle(float px, float py, float h, float cx, float cy, float r) {
  float ex = fabsf(cx - px) - h, ey = fabsf(cy - py) - h;
  return (ex < 0 && ey < 0) || ex * ex + ey * ey < r * r;
}

static bool overlap(float ax, float ay, float ahw, float ahh, float bx, float by, float bhw, float bhh) {
  return fabsf(ax - bx) <= ahw + bhw && fabsf(ay - by) <= ahh + bhh;   /* cocos rects touch inclusively */
}

/* Separating axis test of two boxes turned by a and b radians (clockwise). */
static bool obb_overlap(float ax, float ay, float ahw, float ahh, float a, float bx, float by, float bhw, float bhh, float b) {
  float ca = cosf(a), sa = sinf(a), cb = cosf(b), sb = sinf(b), dx = bx - ax, dy = by - ay;
  /* axes of both boxes: (cos, -sin) and (sin, cos) in y-up space */
  float axes[4][2] = {{ca, -sa}, {sa, ca}, {cb, -sb}, {sb, cb}};
  for (int k = 0; k < 4; k++) {
    float ux = axes[k][0], uy = axes[k][1];
    float ra = ahw * fabsf(ux * ca - uy * sa) + ahh * fabsf(ux * sa + uy * ca);
    float rb = bhw * fabsf(ux * cb - uy * sb) + bhh * fabsf(ux * sb + uy * cb);
    if (fabsf(dx * ux + dy * uy) >= ra + rb) return false;
  }
  return true;
}

/* GD's stair snap (checkSnapJumpToObject, as measured by gdsolver on 2.2):
   on every tick the cube stands on a solid, when that solid is another one
   than the last and sits one stair step away (one block up, one down or two
   up at a speed dependent distance), x is nudged by up to a unit or two so
   the player keeps its offset to the stairs. */
static void snap_to(Game *g, float ox, float oy) {
  Player *p = &g->p;
  /* [speed] {threshold, one up, one down, two up}; the mini one-up is 90 */
  static const float stairs[4][4] = {{1, 90, 120, 60}, {1, 120, 150, 90}, {2, 150, 195, 120}, {2, 180, 225, 135}};
  const float *st = stairs[p->speed];
  if (p->has_snap && (ox != p->snap_ox || oy != p->snap_oy)) {
    float dx = ox - p->snap_ox, dy = grav(p, oy - p->snap_oy), th = st[0], up = p->mini && p->speed ? 90 : st[1];
    if ((fabsf(dx - up) <= th && fabsf(dy - 30) <= th) || (fabsf(dx - st[2]) <= th && fabsf(dy + 30) <= th) ||
        (fabsf(dx - st[3]) <= th && fabsf(dy - 60) <= th))
      p->x += clampf(ox + p->snap_diff - p->x, -th, th);
  }
  p->has_snap = true;
  p->snap_ox = ox;
  p->snap_oy = oy;
  p->snap_diff = p->x - ox;
}

/* ------------------------------------------------------------ slopes
   Ported from gd3ds (its slope.c): a slope is a right triangle in its box;
   orientation 0 rises to the right with the solid below, 1 falls to the
   right, 2 and 3 are the same hanging from above. */
typedef struct { float x, y, w, h; int o; } Slope;
enum { OR_NU, OR_ND, OR_UDD, OR_UDU };
#define SLOPE_EPS 0.01f
#define SLOPE_HEAD_TOL 2.0f
static const float SLOPE_FALL[4] = {226.044054f, 280.422108f, 348.678108f, 421.200108f};

static Slope slope_of(const Level *L, const RObj *o, float x, float y) {
  const ObjDef *d = &objdefs[o->type];
  float sx, sy;
  obj_scale_xy(L, o, &sx, &sy);
  Slope s = {x, y, d->w10 * 0.1f * fabsf(sx), d->h10 * 0.1f * fabsf(sy), 0};
  int xf = obj_xf(o);
  if (xf < 0) xf = 0;
  int o4 = xf & 3, fh = (xf >> 2) & 1, fv = (xf >> 3) & 1;
  if (o4 & 1) { float t = s.w; s.w = s.h; s.h = t; }
  o4 += fh && fv ? 2 : fh ? 1 : fv ? 3 : 0;
  s.o = o4 & 3;
  return s;
}
static void slope_get(const float *a, int o, Slope *s) { *s = (Slope){a[0], a[1], a[2], a[3], o}; }
static float sl_left(const Slope *s) { return s->x - s->w / 2; }
static float sl_right(const Slope *s) { return s->x + s->w / 2; }
static float sl_top(const Slope *s) { return s->y + s->h / 2; }
static float sl_bottom(const Slope *s) { return s->y - s->h / 2; }
static float sl_gtop(const Player *p, const Slope *s) { return p->upside ? -sl_bottom(s) : sl_top(s); }
static float sl_gbottom(const Player *p, const Slope *s) { return p->upside ? -sl_top(s) : sl_bottom(s); }
static int sl_orient(const Player *p, const Slope *s) {
  int o = s->o;
  if (p->upside) o = o == OR_UDU ? OR_NU : o == OR_UDD ? OR_ND : o == OR_NU ? OR_UDU : OR_UDD;
  return o;
}
static float sl_angle(const Player *p, const Slope *s) {
  float a = atanf(s->h / s->w);
  int o = sl_orient(p, s);
  return o == OR_ND || o == OR_UDD ? -a : a;
}
static float sl_snap_angle(const Player *p, const Slope *s) {
  float a = sl_angle(p, s);
  if (s->o == OR_NU) a = -fabsf(a);
  if (s->o == OR_ND) a = fabsf(a);
  return a;
}
static float sl_expected_y(const Player *p, const Slope *s) {
  bool flipping = sl_orient(p, s) >= OR_UDD;
  float mult = (p->upside ^ flipping) ? -1 : 1, a = sl_angle(p, s), ph = phalf(p);
  float ydist = mult * ph / cosf(a), rel = s->h / s->w * (p->x - sl_left(s));
  if ((a > 0) ^ p->upside ^ flipping) return sl_bottom(s) + fminf(rel + ydist, s->h + ph);
  return sl_top(s) - fminf(rel - ydist, s->h + ph);
}
static void sl_clear(Player *p) { p->sl_o = -1; p->sl_t = 0; p->sl_down = false; }

static bool sl_touching(const Player *p, const Player *old, const Slope *s) {
  bool has = old->sl_o >= 0;
  if (has) {
    Slope os;
    slope_get(old->sl, old->sl_o, &os);
    float mult = sl_orient(p, &os) >= OR_UDD ? -1 : 1;
    has = p->vy * mult <= 0;
  }
  float deg = fabsf(sl_angle(p, s)) / DEG, snap_h = 20 * (deg / 45), mn = has ? -3 : 0;
  if (sl_right(s) < p->x - phalf(p)) return false;
  float diff = sl_orient(p, s) < OR_UDD ? grav(p, sl_expected_y(p, s)) - grav(p, p->y) : grav(p, p->y) - grav(p, sl_expected_y(p, s));
  return diff >= mn && diff <= snap_h;
}

static void sl_snap_y(Player *p, const Slope *s) {
  float ph = phalf(p), e = sl_expected_y(p, s);
  switch (sl_orient(p, s)) {
    case OR_NU:
      p->y = p->upside ? fmaxf(sl_bottom(s) - ph, e) : fminf(sl_top(s) + ph, e);
      p->time_since_ground = 0; p->on_ground = true;
      if (p->vy < 0) p->vy = 0;
      break;
    case OR_ND:
      p->y = p->upside ? fminf(e, sl_top(s) + ph) : fmaxf(e, sl_bottom(s) - ph);
      p->time_since_ground = 0; p->on_ground = true;
      if (p->vy < 0) p->vy = 0;
      break;
    case OR_UDD:
      p->y = p->upside ? fmaxf(e, sl_bottom(s) - ph) : fminf(e, sl_top(s) + ph);
      p->time_since_ground = 0; p->on_ceiling = true;
      if (p->vy > 0) p->vy = 0;
      break;
    default:
      p->y = p->upside ? fminf(sl_top(s) + ph, e) : fmaxf(sl_bottom(s) - ph, e);
      p->time_since_ground = 0; p->on_ceiling = true;
      if (p->vy > 0) p->vy = 0;
      break;
  }
}

/* How many of the nearby slopes the player touches (only the one it rides
   and the next one of the same kind count while on a slope). */
typedef struct { Slope s[8]; int n; } SlopeSet;
static int sl_count(const Player *p, const Player *old, const SlopeSet *ss) {
  int count = 0;
  Slope cur;
  bool have = p->sl_o >= 0;
  if (have) slope_get(p->sl, p->sl_o, &cur);
  float ph = phalf(p);
  for (int k = 0; k < ss->n; k++) {
    const Slope *s = &ss->s[k];
    if (!overlap(p->x, p->y, ph + .5f, ph + .5f, s->x, s->y, s->w / 2, s->h / 2) || !sl_touching(p, old, s)) continue;
    if (have) {
      float diff = s->y - cur.y;
      bool next = (cur.o == OR_NU || cur.o == OR_UDD) ? diff > 0 : diff < 0;
      bool same = s->x == cur.x && s->y == cur.y;
      if ((cur.o == sl_orient(p, s) && sl_angle(p, &cur) == sl_angle(p, s) && next) || same) count++;
    } else {
      count++;
    }
  }
  return count;
}

#define SHIP_UFO_EXIT_VY (508.248f / 4)
static void sl_calc(Game *g, const Player *old, const Slope *s, const SlopeSet *ss, bool hold) {
  Player *p = &g->p;
  float spd = SPEEDS[p->speed];
  switch (sl_orient(p, s)) {
    case OR_NU:
      if ((p->mode == MODE_UFO || p->mode == MODE_SHIP) && p->vy < SHIP_UFO_EXIT_VY && hold) p->vy = SHIP_UFO_EXIT_VY;
      if (!sl_touching(p, old, s)) { p->pend_clear = true; return; }
      if (grav_bottom(p) != sl_gtop(p, s)) sl_snap_y(p, s);
      if (grav_bottom(p) >= sl_gtop(p, s) - SLOPE_EPS && sl_count(p, old, ss) < 2) {
        /* sliding off the top: launched along the slope */
        float v = 0.9f * fminf(1.12f / sl_angle(p, s), 1.54f) * (s->h * spd / s->w);
        float t = clampf(10 * (p->t_elapsed - p->sl_t), 0.4f, 1.0f);
        if (p->mode == MODE_BALL || p->mode == MODE_SHIP) v *= 0.75f;
        if (p->mode == MODE_UFO) v *= 0.7499f;
        v *= t;
        p->inverse_rot = true;
        memcpy(p->co, p->sl, sizeof(p->co)); p->co_o = p->sl_o; p->co_t = p->sl_t; p->co_down = p->sl_down;
        p->co_ticks = 2;
        p->new_vy = v; p->has_new_vy = true; boost_latch(p);
        p->jumped = true;
        p->pend_clear = true;
      }
      break;
    case OR_ND:
      if (p->vy > 0) { p->pend_clear = true; return; }
      if (grav_bottom(p) != sl_gtop(p, s) || p->sl_down) sl_snap_y(p, s);
      if ((sl_gtop(p, s) <= grav(p, p->y) + SLOPE_EPS || (p->x - phalf(p)) - sl_right(s) > 0) && sl_count(p, old, ss) < 2) {
        p->new_vy = -SLOPE_FALL[p->speed] * (s->h / s->w); p->has_new_vy = true; boost_latch(p);
        memcpy(p->co, p->sl, sizeof(p->co)); p->co_o = p->sl_o; p->co_t = p->sl_t; p->co_down = p->sl_down;
        p->co_ticks = 2;
        p->pend_clear = true;
      }
      break;
    case OR_UDU: {
      if ((p->mode == MODE_UFO || p->mode == MODE_SHIP) && p->vy > -SHIP_UFO_EXIT_VY && !hold) p->vy = -SHIP_UFO_EXIT_VY;
      if (!sl_touching(p, old, s)) { p->pend_clear = true; return; }
      if ((p->mode == MODE_CUBE || p->mode == MODE_ROBOT) && p->ceil_inv <= 0) {
        if (grav(p, p->y - SLOPE_HEAD_TOL) > grav(p, sl_expected_y(p, s))) kill(g, -1);
        return;
      }
      if (grav_bottom(p) != sl_gtop(p, s)) sl_snap_y(p, s);
      if (grav_top(p) <= sl_gbottom(p, s) + SLOPE_EPS && sl_count(p, old, ss) < 2) {
        float v = 0.9f * fminf(1.12f / sl_angle(p, s), 1.54f) * (s->h * spd / s->w);
        float t = clampf(10 * (p->t_elapsed - p->sl_t), 0.4f, 1.0f);
        if (p->mode == MODE_BALL || p->mode == MODE_SHIP) v *= 0.75f;
        if (p->mode == MODE_UFO) v *= 0.7499f;
        v *= t;
        p->inverse_rot = true;
        memcpy(p->co, p->sl, sizeof(p->co)); p->co_o = p->sl_o; p->co_t = p->sl_t; p->co_down = p->sl_down;
        p->co_ticks = 2;
        p->pend_clear = true;
        p->new_vy = -v; p->has_new_vy = true; boost_latch(p);
      }
      break;
    }
    default:
      if (p->vy < 0) { p->pend_clear = true; return; }
      if ((p->mode == MODE_CUBE || p->mode == MODE_ROBOT) && p->ceil_inv <= 0) {
        if (grav(p, p->y - SLOPE_HEAD_TOL) > grav(p, sl_expected_y(p, s))) kill(g, -1);
        return;
      }
      if (grav_bottom(p) != sl_gtop(p, s) || p->sl_down) sl_snap_y(p, s);
      if ((sl_gtop(p, s) <= grav(p, p->y) - SLOPE_EPS || (p->x - phalf(p)) - sl_right(s) > 0) && sl_count(p, old, ss) < 2) {
        p->new_vy = SLOPE_FALL[p->speed] * (s->h / s->w); p->has_new_vy = true; boost_latch(p);
        memcpy(p->co, p->sl, sizeof(p->co)); p->co_o = p->sl_o; p->co_t = p->sl_t; p->co_down = p->sl_down;
        p->co_ticks = 2;
        p->pend_clear = true;
      }
      break;
  }
}

/* Distance from a point to a segment <= r? */
static bool seg_circle(float cx, float cy, float r, float x1, float y1, float x2, float y2) {
  float dx = x2 - x1, dy = y2 - y1, fx_ = cx - x1, fy = cy - y1, l2 = dx * dx + dy * dy;
  float t = l2 > 0 ? (fx_ * dx + fy * dy) / l2 : 0;
  t = t < 0 ? 0 : t > 1 ? 1 : t;
  float ex = cx - (x1 + t * dx), ey = cy - (y1 + t * dy);
  return ex * ex + ey * ey <= r * r;
}
static bool sl_circle_touch(const Player *p, const Slope *s) {
  float hw = s->w / 2, hh = s->h / 2, r = (phalf(p) * 2 - 4) / 2, x = s->x, y = s->y;
  bool nu = s->o == OR_NU || s->o == OR_UDD, up = s->o == OR_NU || s->o == OR_UDU, normal = s->o < OR_UDD;
  bool hyp = nu ? seg_circle(p->x, p->y, r, x - hw, y - hh, x + hw, y + hh) : seg_circle(p->x, p->y, r, x + hw, y - hh, x - hw, y + hh);
  bool ver = up ? seg_circle(p->x, p->y, r, x + hw, y - hh, x + hw, y + hh) : seg_circle(p->x, p->y, r, x - hw, y - hh, x - hw, y + hh);
  bool hor = normal ? seg_circle(p->x, p->y, r, x + hw, y - hh, x - hw, y - hh) : seg_circle(p->x, p->y, r, x + hw, y + hh, x - hw, y + hh);
  return hyp || ver || hor;
}

static void sl_collide(Game *g, const Player *old, const Slope *s, const SlopeSet *ss, bool hold) {
  Player *p = &g->p;
  if (p->npot < 4) { p->pot[p->npot][0] = sl_top(s); p->pot[p->npot][1] = sl_bottom(s); p->pot_o[p->npot++] = (int8_t)s->o; }
  bool has = old->sl_o >= 0;
  float e = sl_expected_y(p, s), ph = phalf(p), in = p->mode == MODE_WAVE ? 1.5f : 4.5f;
  if (s->o < 2 && e + (has ? 3 : 0) <= p->y) return;
  if (s->o >= 2 && e - (has ? 3 : 0) >= p->y) return;
  float clip = p->mode == MODE_SHIP || p->mode == MODE_UFO ? 7 : 10;
  int orient = sl_orient(p, s);
  float mult = orient >= OR_UDD ? -1 : 1;
  bool gsnap = p->ceil_inv > 0, cube = p->mode == MODE_CUBE || p->mode == MODE_ROBOT;
  if (orient == OR_NU || orient == OR_UDU) {
    /* inside the slope's tall side */
    if (overlap(p->x, p->y, in, in, sl_right(s), s->y, 0.5f, s->h / 2)) { kill(g, -1); return; }
  }
  if (!has && orient < OR_UDD && grav_top(p) - sl_gbottom(p, s) <= clip + 5 * !p->mini) {
    /* under a floor slope: its flat bottom is a ceiling */
    if (p->mode != MODE_WAVE && ((!cube && p->vy >= 0) || gsnap)) {
      p->vy = 0;
      if (!gsnap) p->on_ceiling = true;
      p->time_since_ground = 0;
      p->y = grav(p, sl_gbottom(p, s)) - grav(p, ph);
    } else if (overlap(p->x, p->y, in, in, s->x, s->y, s->w / 2, s->h / 2)) kill(g, -1);
    return;
  }
  if (!has && orient >= OR_UDD && sl_gtop(p, s) - grav_bottom(p) <= clip + 5 * !p->mini) {
    /* on top of a hanging slope: its flat top is a floor */
    if (p->mode != MODE_WAVE && p->vy <= 0) {
      p->vy = 0;
      if (!gsnap) p->on_ground = true;
      p->time_since_ground = 0;
      p->y = grav(p, sl_gtop(p, s)) + grav(p, ph);
    } else if (overlap(p->x, p->y, in, in, s->x, s->y, s->w / 2, s->h / 2)) kill(g, -1);
    return;
  }
  if (!has && (orient == OR_ND || orient == OR_UDD) && p->x - sl_left(s) < 0) {
    /* coming from the left onto a falling slope's top corner */
    if (sl_gtop(p, s) - grav_bottom(p) > clip) {
      if (overlap(p->x, p->y, in, in, s->x, s->y, s->w / 2, s->h / 2)) kill(g, -1);
      return;
    }
    if (p->mode != MODE_WAVE && p->vy * mult <= 0) {
      p->y = orient == OR_ND ? grav(p, sl_gtop(p, s)) + grav(p, ph) : grav(p, sl_gbottom(p, s)) - grav(p, ph);
      p->on_ground = true;
      p->inverse_rot = false;
      return;
    }
  }
  if (!gsnap && cube && orient >= 2 && !sl_circle_touch(p, s)) return;
  bool colliding = overlap(p->x, p->y, ph, ph, s->x, s->y, s->w / 2, s->h / 2);
  bool riding = p->sl_o >= 0;
  Slope cur = {0};
  if (riding) slope_get(p->sl, p->sl_o, &cur);
  bool next = true;
  if (riding) {
    float diff = s->y - cur.y;
    next = (cur.o == OR_NU || cur.o == OR_UDD) ? diff > 0 : diff < 0;
  }
  int co = riding ? sl_orient(p, &cur) : -1;
  bool up_to_down = riding && co != orient && ((co == OR_ND && orient == OR_NU) || (co == OR_UDD && orient == OR_UDU));
  if ((!riding || co == orient || up_to_down) && sl_touching(p, old, s) && colliding && sl_gtop(p, s) - grav_bottom(p) > 2 &&
      (up_to_down || next)) {
    if (riding && sl_angle(p, s) < sl_angle(p, &cur)) return;
    float old_vel = (orient == OR_ND || orient == OR_UDD) ? grav(p, old->delta_y) : old->vy * TDT;
    float ang = atanf(old_vel / (SPEEDS[p->speed] * TDT));
    if (orient >= OR_UDD) ang = -ang;
    bool had = old->sl_o >= 0;
    if (had && riding) had = old->sl_o == p->sl_o;
    bool hit = (orient == OR_ND || orient == OR_UDD) ? ang * 5 <= sl_angle(p, s) : ang <= sl_angle(p, s);
    bool snap_down = (orient == OR_ND || orient == OR_UDD) && p->vy * mult > 0 && p->x - sl_left(s) > 0;
    if (hit || snap_down) {
      if (p->mode == MODE_WAVE) { kill(g, -1); return; }   /* the wave does not ride slopes */
      if (orient >= OR_UDD) p->on_ceiling = true;
      else { p->on_ground = true; landing(p); }
      p->inverse_rot = false;
      float t_prev = p->sl_t;
      p->sl[0] = s->x; p->sl[1] = s->y; p->sl[2] = s->w; p->sl[3] = s->h; p->sl_o = (int8_t)s->o;
      p->sl_t = t_prev;
      if (snap_down && !had) {
        if (orient == OR_ND && p->vy <= 0) { p->y = grav(p, sl_gtop(p, s)) + grav(p, ph); p->sl_down = true; p->vy = 0; }
        else if (orient == OR_UDD && p->vy >= 0) { p->y = grav(p, sl_gbottom(p, s)) - grav(p, ph); p->sl_down = true; p->vy = 0; }
      }
      if (!p->sl_t) p->sl_t = old->t_elapsed;
      sl_calc(g, old, s, ss, hold);
    }
  }
}

static void flip_other(Game *g);
static void solid(Game *g, unsigned i, float ox, float oy, float hw, float hh) {
  Player *p = &g->p;
  if (p->sl_o >= 0 && p->mode != MODE_WAVE) {
    /* riding a slope: only a falling one gives way to a block */
    Slope cur;
    slope_get(p->sl, p->sl_o, &cur);
    int o = sl_orient(p, &cur);
    if (o != OR_ND && o != OR_UDD) return;
    if (ox + hw >= p->x) { sl_clear(p); p->co_o = -1; p->co_ticks = 0; p->on_ground = true; }
  }
  /* a block level with the foot of a slope touched last tick must not catch it */
  for (int k = 0; k < p->npot; k++) {
    float bc = p->pot_o[k] < 2 ? oy + hh : oy - hh, sc = p->pot_o[k] < 2 ? p->pot[k][1] : p->pot[k][0];
    if (bc - sc < 2) return;
  }
  float clip = (p->mode == MODE_SHIP || p->mode == MODE_UFO ? 7.0f : 10.0f) + fabsf(p->vy) * TDT;
  float o_bottom = p->upside ? -(oy + hh) : oy - hh, o_top = p->upside ? -(oy - hh) : oy + hh;
  float in = p->mode == MODE_WAVE ? 1.5f : 4.5f;
  bool internal = overlap(p->x, p->y, in, in, ox, oy, hw, hh), grav_snap = false;
  if (p->ceil_inv > 0) {
    float in_bottom = p->upside ? -(p->y + in) : p->y - in, diff = o_bottom - in_bottom;
    grav_snap = internal && diff >= 0 && diff <= clip;
  }
  /* a mini player about to land on or touch a face is not crushed */
  bool safe = p->mini && (o_top - grav_bottom(p) <= clip || grav_top(p) - o_bottom <= clip);
  float bottom = grav_bottom(p);
  if (p->mode == MODE_WAVE) {
    /* a wave stands on a top face only while a slide modifier (1755) is armed */
    if (p->arm_slide && o_top - bottom <= clip + 2 * in && grav(p, p->y) >= (o_top + o_bottom) / 2) {
      p->y = grav(p, o_top) + grav(p, phalf(p));
      if (p->vy * (p->upside ? -1 : 1) < 0) p->vy = 0;
      p->on_ground = true;
      return;
    }
    if (internal) kill(g, (int)i);
    return;
  }
  /* the ground modes resolve a head hit only while armed: a gravity change
     just now, a head modifier (1859) or a flip modifier (2866) */
  bool armed = grav_snap || p->arm_head || p->arm_flip;
  if (!armed && !safe && internal) { kill(g, (int)i); return; }
  if (o_top - bottom <= clip && p->vy <= 0) {
    p->y = grav(p, o_top) + grav(p, phalf(p));
    if (p->vy <= 0) p->vy = 0;
    p->on_ground = true;
    p->inverse_rot = false;
    p->time_since_ground = 0;
    landing(p);
  } else if ((p->mode != MODE_CUBE && p->mode != MODE_ROBOT) || armed) {
    if ((grav_top(p) - o_bottom <= clip && p->vy >= 0) || grav_snap) {
      float ytop = grav(p, o_bottom) - grav(p, phalf(p));
      bool cube = p->mode == MODE_CUBE || p->mode == MODE_ROBOT || p->mode == MODE_SPIDER;
      if (cube && p->arm_flip && !grav_snap) {
        /* PlayerObject::didHitHead: turn over and stand on the face */
        p->upside = !p->upside;
        p->y = ytop;
        p->vy = 0;
        p->on_ground = true;
        p->inverse_rot = false;
        p->snap_rot = true;
        p->time_since_ground = 0;
        p->ceil_inv = CEILING_INVUL;
        if (p->arm_noauto) p->buffer = BUF_END;
        flip_other(g);
        landing(p);
        return;
      }
      if (!grav_snap && !cube) p->on_ceiling = true;
      if (grav_snap) p->vy = 0;
      p->inverse_rot = false;
      p->time_since_ground = 0;
      p->y = ytop;
      if (p->vy >= 0) p->vy = 0;
    } else if (!safe && internal) kill(g, (int)i);
  } else if (!safe && internal) kill(g, (int)i);
}

/* What a pad or a ring gives the player in its mode and size. The robot
   takes the cube's pads and 0.9 of its rings (GD 2.2's ring table, read by
   gdsolver); the wave keeps its own speed. */
static float jump_value(const Player *p, int type) {
  int m = p->mode;
  if (m <= MODE_UFO) return JUMPS[p->speed][type][m][p->mini];
  if (m == MODE_ROBOT) {
    float v = JUMPS[p->speed][type][MODE_CUBE][p->mini];
    return (type & 1) ? v * 0.9f : v;   /* the odd types are rings */
  }
  /* the spider takes the ball's values, the swing 0.6 of the cube's */
  if (m == MODE_SPIDER) return JUMPS[p->speed][type][MODE_BALL][p->mini];
  if (m == MODE_SWING) return JUMPS[p->speed][type][MODE_CUBE][p->mini] * 0.6f;
  return p->vy;
}

/* Red ring and pad (GD's ringJump / bumpPlayer ratios, read by gdsolver):
   the ring is the cube's jump times a per-mode ratio, the pad 20 GD units
   (the yellow one's 16 x 1.25), both then scaled for the ball, spider and
   swing like the other rings and pads. */
static float red_value(const Player *p, bool pad) {
  float cube = JUMPS[p->speed][J_YORB][MODE_CUBE][p->mini];
  float r;
  switch (p->mode) {
    case MODE_SHIP: r = pad ? (p->mini ? .95f : .63f) : (p->mini ? 1.40f / 1.38f : 1.00f / 1.38f) * 1.38f; break;
    case MODE_UFO: r = pad ? (p->mini ? .98f : .6f) : (p->mini ? 1.36f : 1.02f); break;
    case MODE_BALL: r = pad ? .6f * 1.25f : 1.34f * .7f; break;
    case MODE_SPIDER: r = pad ? .6f * 1.25f : 1.34f * .7f; break;
    case MODE_ROBOT: r = pad ? 1.25f : 1.28f; break;
    case MODE_SWING: r = pad ? .6f * 1.25f : 1.38f * .6f; break;
    default: r = pad ? 1.25f : 1.38f; break;
  }
  if (pad) return JUMPS[p->speed][J_YPAD][MODE_CUBE][p->mini] * r;
  return cube * r;
}

static void enter_mode(Game *g, int mode) {
  Player *p = &g->p;
  p->mode = (uint8_t)mode;
  p->hover = 0;
}

/* Dual mode: the other body (held in g->p2 while one is being stepped). */
static const uint8_t DUAL_HEIGHT[8] = {9, 10, 9, 10, 10, 9, 9, 10};
static void set_dual_bounds(Game *g) {
  int h = DUAL_HEIGHT[g->p.mode] > DUAL_HEIGHT[g->p2.mode] ? DUAL_HEIGHT[g->p.mode] : DUAL_HEIGHT[g->p2.mode];
  float in_block = fmodf(g->dual_y, 30), off = (ceilf((h + 1) / 2.f) - 1) * 30;
  float ground = fmaxf(0, floorf((g->dual_y - off) / 30)) * 30;
  if ((h & 1) && in_block < 15) ground = fmaxf(0, ground - 30);
  g->p.ground_y = g->p2.ground_y = ground;
  g->p.ceiling_y = g->p2.ceiling_y = ground + h * 30;
}
/* Modes whose gravity stays linked in dual (cube and wave, and each with itself). */
static bool linked(int a, int b) {
  bool ca = a == MODE_CUBE || a == MODE_WAVE, cb = b == MODE_CUBE || b == MODE_WAVE;
  return a == b || (ca && cb);
}
static void flip_other(Game *g) {
  Player *p = &g->p, *o = &g->p2;
  if (!g->dual || !linked(p->mode, o->mode) || p->upside != o->upside) return;
  o->upside = !p->upside;
  o->vy /= -2;
  o->ceil_inv = CEILING_INVUL;
}
/* After a teleport (GD 2.2): its gravity mode (GD's own upside-down flag,
   see the gravity portals) and a static force pushing the player off. */
static void teleported(Game *g, const LStyle *st, unsigned force) {
  Player *p = &g->p;
  unsigned m = STF_TPGRAV(st_flags(st));
  bool gd_up = p->upside ^ (g->frame >= 2);
  bool want = m == 3 ? !gd_up : m == 2;
  if (m && want != gd_up) {
    p->vy /= -2;
    p->upside = !p->upside;
    p->inverse_rot = false;
    p->snap_rot = true;
    flip_other(g);
  }
  if (force) {
    p->vy = force * 54.f;
    p->on_ground = false;
    p->jumped = true;
  }
}
static bool portal_free;   /* the portal being entered is a free-mode one */
static uint8_t force_ids;  /* force ids already counted this tick */
static void portal_bounds(Game *g, float oy, float height) {
  if (g->dual) set_dual_bounds(g);
  else if (portal_free) {
    /* free mode (GD 2.2): no band, the camera follows */
    g->p.ground_y = g->frame ? -1e6f : 0;
    g->p.ceiling_y = 1e6f;
    g->cam_intended_y = g->cam_y;
  } else set_bounds_for_portal(g, oy, height);
}

static void spider_jump(Game *g);

static void special(Game *g, unsigned i, const RObj *o, bool hold, float ox, float oy) {
  Player *p = &g->p;
  const ObjDef *d = &objdefs[o->type];
  bool used = game_used(g, o, i);
  portal_free = o->paint && (st_flags(obj_style(g->L, o)) & STF_FREE);
  switch (d->special) {
    case SP_PAD_Y: case SP_PAD_P:
      if (used) break;
      p->vy = jump_value(p, d->special == SP_PAD_Y ? J_YPAD : J_PPAD);
      p->on_ground = false; p->inverse_rot = false; p->left_ground = true; p->jumped = true; p->hover = 0;
      set_used(g, i);
      update_rot_dir(p);
      fx(g, FX_PAD, d->special == SP_PAD_Y ? 0 : 2, (int)i, ox, oy);
      break;
    case SP_PAD_B: {
      if (used) break;
      int xf = obj_xf(o), rot = (xf & 3) * 90;
      if (xf < 0) rot = (int)(obj_rot(o) * 360 / 1024);
      if (xf >= 0 && (xf & 8)) rot = (rot + 180) % 360;
      bool down = rot > 90 && rot < 270;
      if ((!down && p->upside) || (down && !p->upside)) break;
      p->left_ground = true;
      update_rot_dir(p);
      p->vy = jump_value(p, J_BPAD);
      p->upside = !p->upside;
      flip_other(g);
      p->on_ground = false; p->inverse_rot = false; p->ceil_inv = CEILING_INVUL; p->jumped = true; p->hover = 0;
      set_used(g, i);
      fx(g, FX_PAD, 1, (int)i, ox, oy);
      break;
    }
    case SP_ORB_Y: case SP_ORB_P: case SP_ORB_B: case SP_ORB_G:
      if (!used && hold && p->buffer == BUF_READY) {
        int j = d->special == SP_ORB_P ? J_PORB : d->special == SP_ORB_B ? J_BORB : J_YORB;
        if (d->special == SP_ORB_G) {
          /* the green ring flips first, then launches (not halved) */
          p->upside = !p->upside;
          p->ceil_inv = CEILING_INVUL;
          flip_other(g);
        }
        float v = jump_value(p, j);
        if (d->special == SP_ORB_G && p->mode == MODE_SHIP) v *= 0.7f;
        if (p->mode != MODE_WAVE) p->vy = v;
        if (d->special == SP_ORB_B) { p->upside = !p->upside; p->ceil_inv = CEILING_INVUL; flip_other(g); }
        p->on_ground = false; p->on_ceiling = false; p->inverse_rot = false; p->left_ground = true;
        p->buffer = BUF_END; p->jumped = true; p->hover = 0;
        p->spin = -BALL_AIR_SPIN;
        update_rot_dir(p);
        g->jumps++;
        set_used(g, i);
        fx(g, FX_ORB, (uint8_t)(d->special == SP_ORB_G ? 3 : d->special - SP_ORB_Y), (int)i, ox, oy);
      }
      break;
    case SP_GRAV_N: case SP_GRAV_F:
      if (used) break;
      p->ceil_inv = CEILING_INVUL;
      /* GD's own upside-down flag reads inverted in frames 2 and 3 (their v
         axis is opposite to GD's up; gdsolver measured frame 3): a portal
         compares against it */
      if ((p->upside ^ (g->frame >= 2)) != (d->special == SP_GRAV_F)) {
        p->vy /= -2;
        p->upside = !p->upside;
        p->inverse_rot = false;
        if (p->mode == MODE_UFO) p->rot = 0; else p->snap_rot = true;
        flip_other(g);
        p->left_ground = true;
        fx(g, FX_GRAVITY, d->special == SP_GRAV_F, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_PORTAL_CUBE: case SP_PORTAL_ROBOT: {
      if (used) break;
      int want = d->special == SP_PORTAL_CUBE ? MODE_CUBE : MODE_ROBOT;
      if (g->dual) set_dual_bounds(g);
      else { p->ground_y = 0; p->ceiling_y = 999999; }
      if (p->mode != want) {
        if (p->mode != MODE_BALL && p->mode != MODE_CUBE && p->mode != MODE_ROBOT) p->vy /= 2;
        if (p->mode == MODE_WAVE) p->vy *= 0.9f;
        p->ceil_inv = CEILING_INVUL;
        p->snap_rot = true;
        enter_mode(g, want);
        if (g->dual) set_dual_bounds(g);
        flip_other(g);
        update_rot_dir(p);
        fx(g, FX_PORTAL, want == MODE_CUBE ? 0 : 5, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    }
    case SP_PORTAL_BALL:
      if (used) break;
      portal_bounds(g, oy, BALL_HEIGHT);
      if (p->mode != MODE_BALL) {
        if (p->mode == MODE_WAVE) p->vy *= 0.45f;
        if (p->mode == MODE_SHIP || p->mode == MODE_UFO || p->mode == MODE_WAVE) {
          p->vy /= 2;
          if (hold && p->mode == MODE_SHIP) p->buffer = BUF_READY; /* holding through the portal flips at once */
        }
        enter_mode(g, MODE_BALL);
        if (g->dual) set_dual_bounds(g);
        p->spin = -BALL_AIR_SPIN;
        p->inverse_rot = false;
        p->snap_rot = true;
        flip_other(g);
        fx(g, FX_PORTAL, 2, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_PORTAL_SHIP:
      if (used) break;
      portal_bounds(g, oy, 300);
      if (p->mode != MODE_SHIP) {
        if (p->mode == MODE_WAVE) p->vy *= 0.9f;
        p->vy /= p->mode == MODE_UFO || p->mode == MODE_WAVE ? 4 : 2;
        enter_mode(g, MODE_SHIP);
        if (g->dual) set_dual_bounds(g);
        p->inverse_rot = false;
        p->snap_rot = true;
        flip_other(g);
        p->vy = p->mini ? clampf(p->vy, SHIP_MINI_MIN, SHIP_MINI_MAX) : clampf(p->vy, SHIP_MIN, SHIP_MAX);
        fx(g, FX_PORTAL, 1, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_PORTAL_UFO:
      if (used) break;
      portal_bounds(g, oy, 300);
      if (p->mode != MODE_UFO) {
        int was = p->mode;
        if (was == MODE_WAVE) p->vy *= 0.9f;
        p->vy /= was == MODE_SHIP || was == MODE_WAVE ? 4 : 2;
        enter_mode(g, MODE_UFO);
        if (g->dual) set_dual_bounds(g);
        p->inverse_rot = false;
        p->rot = 0;
        flip_other(g);
        /* coming from a flying mode, a held button flaps at once */
        if (was == MODE_SHIP || was == MODE_WAVE) p->buffer = BUF_READY;
        fx(g, FX_PORTAL, 3, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_PORTAL_WAVE:
      if (used) break;
      portal_bounds(g, oy, 300);
      if (p->mode != MODE_WAVE) {
        enter_mode(g, MODE_WAVE);
        if (g->dual) set_dual_bounds(g);
        p->inverse_rot = false;
        p->snap_rot = true;
        flip_other(g);
        fx(g, FX_PORTAL, 4, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_SIZE_MINI: case SP_SIZE_NORMAL:
      if (used) break;
      if (p->mini != (d->special == SP_SIZE_MINI)) {
        p->mini = d->special == SP_SIZE_MINI;
        fx(g, FX_SIZE, p->mini, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    case SP_SPEED_0: case SP_SPEED_1: case SP_SPEED_2: case SP_SPEED_3:
      if (used) break;
      p->speed = (uint8_t)(d->special - SP_SPEED_0);
      if (g->dual) g->p2.speed = p->speed;
      set_used(g, i);
      trig_log(g, LOG_SPEED, p->speed, 0);
      fx(g, FX_SPEED, p->speed, (int)i, ox, oy);
      break;
    case SP_DUAL_ON:
      if (used) break;
      set_used(g, i);
      if (!g->dual) {
        p->ceil_inv = CEILING_INVUL;
        g->dual = true;
        g->dual_y = oy;
        g->p2 = *p;
        g->p2.upside = !p->upside;
        g->p2.second = true;
        fx(g, FX_PORTAL, 6, (int)i, ox, oy);
      }
      set_dual_bounds(g);
      break;
    case SP_DUAL_OFF:
      if (used) break;
      set_used(g, i);
      if (g->dual) {
        g->dual = false;
        if (p->second) g->dual_swap = true;
        if (p->mode == MODE_CUBE || p->mode == MODE_ROBOT) { p->ground_y = 0; p->ceiling_y = 999999; }
        else set_bounds_for_portal(g, g->dual_y, p->mode == MODE_BALL ? BALL_HEIGHT : 300);
        fx(g, FX_PORTAL, 7, (int)i, ox, oy);
      }
      break;
    case SP_TELEPORT: {
      if (used) break;
      set_used(g, i);
      const LStyle *st = obj_style(g->L, o);
      int off = st->arg & 0x7ff;   /* the offset: 11 bits, signed */
      if (off & 0x400) off -= 0x800;
      unsigned force = ((unsigned)(uint16_t)st->arg >> 11) & 31;
#if ND_EXTRA_LEVELS
      if (st_flags(st) & STF_TPWIDE) { off = st->arg; force = 0; }
      if (g->L->ext->lflags & LF_TPAXIS) {
        /* the exit is the offset along the portal's own up (turned clockwise, flipped): a level
           portal sets the height to the exit's, a turned one moves the player along its axis */
        float a = obj_rot(o) * (6.2831853f / 1024), fo = (obj_flips(o) & 2) ? -(float)off : (float)off;
        float dxw = fo * sinf(a), dyw = fo * cosf(a), X, Y, oX, oY;
        to_world(g, p->x, p->y, &X, &Y);
        to_world(g, ox, oy, &oX, &oY);
        if (fabsf(dxw) < .5f) Y = oY + dyw;
        else { X += dxw; Y += dyw; }
        to_local(g, X, Y, &p->x, &p->y);
      } else
#endif
      p->y = oy + (float)off;
      p->has_snap = false;
      p->left_ground = true;
      teleported(g, st, force);
      fx(g, FX_PORTAL, 8, (int)i, ox, oy);
      break;
    }
    case SP_KEY: case SP_ITEM:
      if (used) break;
      set_used(g, i);
      fx(g, FX_COIN, 1, (int)i, ox, oy);
      break;
    case SP_PORTAL_SPIDER: case SP_PORTAL_SWING: {
      if (used) break;
      bool spider = d->special == SP_PORTAL_SPIDER;
      portal_bounds(g, oy, spider ? BALL_HEIGHT : 300);
      int want = spider ? MODE_SPIDER : MODE_SWING;
      if (p->mode != want) {
        int was = p->mode;
        if (was == MODE_WAVE) p->vy *= 0.9f;
        if (was == MODE_SHIP || was == MODE_UFO || was == MODE_WAVE) p->vy /= 2;
        enter_mode(g, want);
        if (g->dual) set_dual_bounds(g);
        p->inverse_rot = false;
        p->snap_rot = true;
        flip_other(g);
        if (!spider) p->vy = clampf(p->vy, -SWING_TERM, SWING_TERM);
        fx(g, FX_PORTAL, spider ? 9 : 10, (int)i, ox, oy);
      }
      set_used(g, i);
      break;
    }
    case SP_PAD_R:
      if (used) break;
      p->vy = red_value(p, true);
      boost_latch(p);
      p->on_ground = false; p->inverse_rot = false; p->left_ground = true; p->jumped = true; p->hover = 0;
      set_used(g, i);
      update_rot_dir(p);
      fx(g, FX_PAD, 3, (int)i, ox, oy);
      break;
    case SP_ORB_R: case SP_ORB_DASH: case SP_ORB_DASH_G: case SP_ORB_SPIDER: case SP_ORB_T:
      if (!used && hold && p->buffer == BUF_READY) {
        if (d->special == SP_ORB_R) {
          if (p->mode != MODE_WAVE) { p->vy = red_value(p, false); boost_latch(p); }
        } else if (d->special == SP_ORB_T) {
          trig_toggle_ring(g, (unsigned)obj_style(g->L, o)->arg);
        } else if (d->special == SP_ORB_SPIDER) {
          spider_jump(g);
          p->no_jump = true;   /* GD takes the ring in its button phase: a held jump waits a tick */
        } else {
          if (d->special == SP_ORB_DASH_G) { p->upside = !p->upside; p->ceil_inv = CEILING_INVUL; flip_other(g); }
          /* the ring's own angle, in the player's frame */
          float a = (float)((obj_rot(o) + 1024 - (g->frame & 3) * 256) & 1023) * (360.f / 1024);
          if (obj_flips(o) & 1) a = -a;
          float t = tanf(-a * DEG);
          p->dash = true;
          p->dash_slope = fmaxf(-8, fminf(8, t));
          p->vy = 0;
        }
        p->on_ground = false; p->on_ceiling = false; p->inverse_rot = false; p->left_ground = true;
        p->buffer = BUF_END; p->jumped = true; p->hover = 0;
        update_rot_dir(p);
        g->jumps++;
        set_used(g, i);
        fx(g, FX_ORB, (uint8_t)(d->special == SP_ORB_R ? 4 : d->special == SP_ORB_SPIDER ? 5 : d->special == SP_ORB_T ? 6 : 3), (int)i, ox, oy);
      }
      break;
    case SP_FORCE: {
      /* a force box: a flat push per tick while inside, along its own
         direction (0 = up), per mode (gdsolver's measurements, GD units per
         tick per unit of force); boxes sharing a force id count once */
      static const float UNIT[8] = {.225f, .1058f, .135f, .1305f, 0, .2025f, .135f, .09f};
      int arg = obj_style(g->L, o)->arg, fid = arg >> 12 & 7;
      if (fid) { if (force_ids & 1u << fid) break; force_ids |= (uint8_t)(1u << fid); }
      float a = (float)((obj_rot(o) + 1024 - (g->frame & 3) * 256) & 1023) * (6.2831853f / 1024);
      float up = cosf(a) * (obj_flips(o) & 2 ? -1 : 1);
      if (!p->dash && fabsf(up) > 0.01f) p->vy += grav(p, up * (arg & 4095) * 0.01f * UNIT[p->mode & 7] * 54);
      break;
    }
    case SP_FORCE_CIRCLE: {
      /* a force field (3645, gdsolver): circle against circle, a push along
         its direction ramping across its diameter, 0.270 at the far edge */
      float R = d->w10 * 0.1f * scale_of(g->L, o), dx = p->x - ox, dy = p->y - oy, rr = R + phalf(p);
      if (dx * dx + dy * dy > rr * rr || p->dash) break;
      float a = (float)((obj_rot(o) + 1024 - (g->frame & 3) * 256) & 1023) * (6.2831853f / 1024);
      float dir = cosf(a) < 0 ? -1.f : 1.f;
      float frac = dir < 0 ? (p->y - (oy - R)) / (2 * R) : ((oy + R) - p->y) / (2 * R);
      frac = clampf(frac, 0, 1);
      p->vy += grav(p, dir * 0.270f * frac * 54);
      break;
    }
    case SP_STOP_DASH:
      p->dash = false;
      break;
#if ND_EXTRA_LEVELS
    case SP_MIRROR_ON: case SP_MIRROR_OFF:
      if (used) break;
      set_used(g, i);
      if (g->mirror != (d->special == SP_MIRROR_ON)) fx(g, FX_PORTAL, 0, (int)i, ox, oy);
      g->mirror = d->special == SP_MIRROR_ON;
      break;
#endif
    case SP_ARM_HEAD: p->arm_head = 2; break;
    case SP_ARM_FLIP: p->arm_flip = 2; break;
    case SP_ARM_NOAUTO: p->arm_noauto = 2; break;
    case SP_ARM_SLIDE: p->arm_slide = 2; break;
    case SP_TELEPORT2: {
      if (used) break;
      set_used(g, i);
      float tx, ty;
      const LStyle *st = obj_style(g->L, o);
      if (trig_anchor(g, (unsigned)st->arg & 0x1ff, &tx, &ty)) {
        float u, v;
        to_local(g, tx, ty, &u, &v);
        p->x = u;
        p->y = v;
        p->has_snap = false;
        p->left_ground = true;
        sl_clear(p);
        teleported(g, st, ((unsigned)st->arg >> 9) & 31);
        fx(g, FX_PORTAL, 8, (int)i, ox, oy);
      }
      break;
    }
    case SP_TOUCH:
      trig_touch(g, (unsigned)obj_style(g->L, o)->arg);
      break;
    case SP_COIN:
      if (used) break;
      set_used(g, i);
      g->coins |= (uint8_t)(1u << (game_coin_index(g->L, i) & 7));
      fx(g, FX_COIN, 0, (int)i, ox, oy);
      break;
    default: break;
  }
}

#define MAX_NEAR 192
typedef struct { const RObj *o; uint32_t gi; float x, y; } Near;

/* Hitbox centre and half extents of an object at (x, y): its box turns with
   it, and a few objects carry their box off centre. */
static void hit_box(const Level *L, const RObj *o, float x, float y, float *cx, float *cy, float *hw, float *hh) {
  const ObjDef *d = &objdefs[o->type];
  obj_hitbox(o, hw, hh);
  if (obj_scale(o)) {
    float sx, sy;
    obj_scale_xy(L, o, &sx, &sy);
    if (obj_rot(o) & 256) { float t = sx; sx = sy; sy = t; }
    *hw *= fabsf(sx);
    *hh *= fabsf(sy);
  }
  *cx = x;
  *cy = y;
  if (d->hx2) {
    float off = d->hx2 * 0.5f, a = obj_rot(o) * (6.2831853f / 1024);
    if (obj_flips(o) & 1) off = -off;
    *cx += off * cosf(a);
    *cy -= off * sinf(a);
  }
}

/* An object as seen from the player's (turned) frame, also turned by its
   groups' rotation (degrees). */
static RObj local_obj(const Game *g, const RObj *o, float angle) {
  RObj r = *o;
  int turn = -(int)(g->frame & 3) * 256 + (int)lroundf(angle * (1024.f / 360));
  unsigned rot = (unsigned)((int)obj_rot(o) + turn + 4096) & 1023;
  r.ys = (r.ys & ~(1023u << 20)) | rot << 20;
  return r;
}

/* The spider's jump, after GD's spiderTestJumpInternal as gdsolver read it:
   solids in a strip hung off the centre ([x, x + 14.5]) or, failing that,
   over the player's width; hazards in an 8 unit strip. The nearest face on
   the other side of gravity wins (a hazard kills there); a solid ahead of
   the centre only if the spider fits there. Nothing: the band's face. */
typedef struct { float cx, cy, hw, hh; } Box;
static bool spider_pick(const Player *p, const Box *L, int n, float gs, float *y) {
  float h = phalf(p);
  bool f = false;
  for (int i = 0; i < n; i++) {
    const Box *o = &L[i];
    float rest = (gs < 0 ? o->cy + o->hh : o->cy - o->hh) - gs * h;
    if ((rest - p->y) * gs < -10 || (f && (rest - p->y) * gs >= (*y - p->y) * gs)) continue;
    if (o->cx - o->hw > p->x) {
      bool blocked = false;
      for (int j = 0; j < n && !blocked; j++) {
        const Box *b = &L[j];
        if (b == o) continue;
        blocked = !(p->x + h < b->cx - b->hw || b->cx + b->hw < p->x - h || rest + h < b->cy - b->hh + 2 || b->cy + b->hh - 2 < rest - h);
      }
      if (blocked) continue;
    }
    *y = rest;
    f = true;
  }
  return f;
}

static void spider_jump(Game *g) {
  Player *p = &g->p;
  float h = phalf(p), sx = p->mini ? 8.7f : 14.5f, gs = p->upside ? -1.f : 1.f;
  static Box strip[48], wide[48];
  int ns = 0, nw = 0, hz = -1, vhaz = -1;
  float haz_y = 0, vy_ = 0;
  bool vf = false;
  float X, Y;
  to_world(g, p->x, p->y, &X, &Y);
  bool vert = g->frame & 1;
  LIter it;
  level_iter_y(g->L, X - (vert ? 900 : 40), X + (vert ? 900 : 40), vert ? Y - 40 : -1e9f, vert ? Y + 40 : 1e9f, &it);
  for (const RObj *o; (o = level_next(&it));) {
    const ObjDef *d = &objdefs[o->type];
    if ((d->hit != HIT_SOLID && d->hit != HIT_HAZARD) || d->shape == SHAPE_SLOPE) continue;
    float wx, wy, ang;
    if (!obj_where_rot(g, o, &wx, &wy, &ang) || (o->paint && (st_flags(obj_style(g->L, o)) & STF_NOTOUCH))) continue;
    float lx, ly;
    to_local(g, wx, wy, &lx, &ly);
    RObj lo = local_obj(g, o, ang);
    Box b;
    hit_box(g->L, &lo, lx, ly, &b.cx, &b.cy, &b.hw, &b.hh);
    bool haz = d->hit == HIT_HAZARD;
    float hy = b.hh, hx = b.hw;
    if (haz && d->shape == SHAPE_CIRCLE) hx = hy = d->w10 * 0.1f * scale_of(g->L, o);
    if (vert) {
      /* a turned frame (gdsolver, hooked on lv22): solids from a strip off
         the leading edge, hazards within 4 of the centre (not saws); the
         nearest face ahead wins, a hazard at its centre */
      if (haz ? d->shape == SHAPE_CIRCLE || b.cx + b.hw < p->x - 4 || b.cx - b.hw > p->x + 4
              : b.cx + b.hw < p->x + h || b.cx - b.hw > p->x + 2 * h + 1) continue;
      float rest = haz ? b.cy : (gs > 0 ? b.cy - b.hh : b.cy + b.hh) - gs * h;
      if ((rest - p->y) * gs <= 0.5f) continue;
      if (!vf || (rest - p->y) * gs < (vy_ - p->y) * gs) { vy_ = rest; vf = true; vhaz = haz ? (int)it.gi : -1; }
      continue;
    }
    /* only what reaches past the spider's far face */
    if (gs > 0 ? b.cy + hy < p->y + h - 2 : b.cy - hy > p->y - h + 2) continue;
    if (haz) {
      if (b.cx + hx < p->x - 4 || b.cx - hx > p->x + 4) continue;
      if (hz < 0 || (b.cy - haz_y) * gs < 0) { hz = (int)it.gi; haz_y = b.cy; }
      continue;
    }
    if (!(b.cx + b.hw < p->x || b.cx - b.hw > p->x + sx) && ns < 48) strip[ns++] = b;
    if (!(b.cx + b.hw < p->x - h || b.cx - b.hw > p->x + h + 4) && nw < 48) wide[nw++] = b;
  }
  float y = 0;
  bool f = false, on_haz = false;
  if (vert) {
    f = vf; y = vy_; on_haz = vhaz >= 0; hz = vhaz;
  } else if (!ns && hz >= 0) {
    if (!nw) on_haz = true;
    else { f = spider_pick(p, wide, nw, gs, &y); on_haz = !f || (haz_y - y) * gs < 0; }
  } else {
    f = spider_pick(p, strip, ns, gs, &y);
    if (hz >= 0 && (!f || (haz_y - y) * gs < 0)) {
      float y2 = 0;
      bool f2 = nw ? spider_pick(p, wide, nw, gs, &y2) : false;
      if (!nw || !f2 || (haz_y - y2) * gs < 0) on_haz = true;
      else { y = y2; f = f2; }
    }
  }
  if (on_haz && !vert) { y = haz_y; f = true; }
  if (!f) {
    /* the band's face (the floor, in the unturned frame) */
    float bound = gs > 0 ? p->ceiling_y : p->ground_y;
    if (fabsf(bound) < 1e5f) { y = bound - gs * h; f = true; }
  }
  if (!f) return;   /* nothing to land on: GD does nothing */
  p->y = y;
  p->upside = !p->upside;
  p->vy = 0;
  p->has_snap = false;
  p->on_ground = true;
  sl_clear(p);
  flip_other(g);
  fx(g, FX_JUMP, 2, -1, p->x, p->y);
  if (on_haz) kill(g, hz);
}

static SlopeSet near_slopes;   /* this tick's slopes around the player */

/* GD carries a player standing on a moving solid along with it (world
   vertical only, gdsolver): a face that sank away from the feet since the
   last tick still holds the player, a few units being a tick's worth. */
static void ride(Game *g, const Player *old, const Near *near, unsigned n) {
  Player *p = &g->p;
  if (g->frame || p->on_ground || !old->on_ground || p->vy > 0 || !g->L->ext) return;
  if (p->mode != MODE_CUBE && p->mode != MODE_ROBOT && p->mode != MODE_SPIDER && p->mode != MODE_BALL) return;
  float ph = phalf(p), foot = grav(p, p->y) - ph, best = 4, face = 0;
  for (unsigned k = 0; k < n; k++) {
    const RObj *o = near[k].o;
    const ObjDef *d = &objdefs[o->type];
    if (d->hit != HIT_SOLID || d->shape == SHAPE_SLOPE || !o->paint) continue;
    const LStyle *st = obj_style(g->L, o);
    if (!gset_moved(g, st->groups)) continue;
    float hw, hh, ox, oy;
    hit_box(g->L, o, near[k].x, near[k].y, &ox, &oy, &hw, &hh);
    if (fabsf(p->x - ox) > hw + ph) continue;
    float top = p->upside ? -(oy - hh) : oy + hh, gap = foot - top;
    if (gap >= 0 && gap < best) { best = gap; face = top; }
  }
  if (best >= 4) return;
  p->y = grav(p, face + ph);
  p->vy = 0;
  p->on_ground = true;
  p->time_since_ground = 0;
  landing(p);
}

static void collide(Game *g, const Player *old, bool hold) {
  Player *p = &g->p;
  near_slopes.n = 0;
  force_ids = 0;
  /* objects around the player, in level order, where their groups put them */
  Near near[MAX_NEAR];
  static RObj turned[64];      /* objects seen from a turned frame */
  unsigned n = 0, nt = 0;
  LIter it;
  float PX_, PY_;
  to_world(g, p->x, p->y, &PX_, &PY_);
  bool vert = g->frame & 1;
  level_iter_y(g->L, PX_ - (vert ? 60 : 15), PX_ + (vert ? 60 : 15), PY_ - 200, PY_ + 200, &it);
  for (const RObj *o; (o = level_next(&it)) && n < MAX_NEAR;) {
    if (objdefs[o->type].hit == HIT_NONE) continue;
    float wx, wy, ang, x, y;
    if (!obj_where_rot(g, o, &wx, &wy, &ang)) continue;
    to_local(g, wx, wy, &x, &y);
    if (x < p->x - 60 || x >= p->x + 60 || y < p->y - 200 || y > p->y + 200) continue;
    if (o->paint && (st_flags(obj_style(g->L, o)) & STF_NOTOUCH)) continue;
    if (g->frame || ang != 0) {
      if (nt >= 64) continue;
      turned[nt] = local_obj(g, o, ang);
      o = &turned[nt++];
    }
    near[n++] = (Near){o, it.gi, x, y};
    if (objdefs[o->type].shape == SHAPE_SLOPE && near_slopes.n < 8) near_slopes.s[near_slopes.n++] = slope_of(g->L, o, x, y);
  }
  bool touching_orb = false;
  float snap_x = p->x;   /* contacts are judged before any nudge */
  /* GD resolves special objects, then solids, then slopes, then hazards. */
  for (int pass = 0; pass < 3 && !g->dead; pass++) {
    if (pass == 1 && p->left_ground) sl_clear(p);
    if (pass == 2) {
      p->npot = 0;
      for (int k = 0; k < near_slopes.n && !g->dead; k++) {
        const Slope *sl = &near_slopes.s[k];
        float ph = phalf(p);
        if (overlap(p->x, p->y, ph, ph, sl->x, sl->y, sl->w / 2, sl->h / 2)) sl_collide(g, old, sl, &near_slopes, hold);
      }
      if (g->dead) break;
    }
    if (pass == 2 && (p->mode == MODE_CUBE || p->mode == MODE_ROBOT) && p->on_ground) {
      /* stair snap against every solid under the cube, in GD's order: by
         100 unit section, right to left then top to bottom, then newest */
      int touch[8], nt = 0;
      float ph = phalf(p), foot = p->y - grav(p, ph);
      for (unsigned k = 0; k < n && nt < 8; k++) {
        const Near *e = &near[k];
        if (objdefs[e->o->type].hit != HIT_SOLID || objdefs[e->o->type].shape == SHAPE_SLOPE) continue;
        float hw, hh, ox, oy;
        hit_box(g->L, e->o, e->x, e->y, &ox, &oy, &hw, &hh);
        if (fabsf(snap_x - ox) > hw + ph + 0.01f || fabsf(foot - (p->upside ? oy - hh : oy + hh)) > 0.6f) continue;
        int j = nt++;
        for (; j > 0; j--) {
          const Near *b = &near[touch[j - 1]];
          int sa = (int)floorf(e->x / 100), sb = (int)floorf(b->x / 100);
          if (sa == sb) { sa = (int)floorf(e->y / 100); sb = (int)floorf(b->y / 100); }
          if (sa == sb ? e->gi < b->gi : sa < sb) break;
          touch[j] = touch[j - 1];
        }
        touch[j] = (int)k;
      }
      for (int j = 0; j < nt; j++) snap_to(g, near[touch[j]].x, near[touch[j]].y);
    }
    for (unsigned k = 0; k < n && !g->dead; k++) {
      const RObj *o = near[k].o;
      unsigned i = near[k].gi;
      const ObjDef *d = &objdefs[o->type];
      int want = pass == 0 ? HIT_SPECIAL : pass == 1 ? HIT_SOLID : HIT_HAZARD;
      if (d->hit != want) continue;
      if ((d->special == SP_COIN || d->special == SP_KEY) && game_used(g, o, i)) continue;
      float hw, hh, ox, oy, ph = phalf(p);
      hit_box(g->L, o, near[k].x, near[k].y, &ox, &oy, &hw, &hh);
      if (d->shape == SHAPE_CIRCLE) {   /* w is the radius */
        float r = d->w10 * 0.1f * scale_of(g->L, o);
        if (g->L->ext && (g->L->ext->lflags & LF_FIX_RADIUS)) {
          /* the level's fixRadiusCollision: centre distance (gdsolver) */
          float dx = p->x - ox, dy = p->y - oy;
          if (dx * dx + dy * dy >= (r + ph) * (r + ph)) continue;
        } else if (!box_circle(p->x, p->y, ph, ox, oy, r)) continue;
      } else if (obj_xf(o) < 0 && pass != 1) {
        /* turned by a free angle (GD's collisionCheckObjects): the bounds
           must overlap, then the object's turned box and the player's box,
           turned as its sprite is */
        float a = obj_rot(o) * (6.2831853f / 1024);
        float sx, sy;
        obj_scale_xy(g->L, o, &sx, &sy);
        hw = d->w10 * 0.05f * fabsf(sx); hh = d->h10 * 0.05f * fabsf(sy);
        float ca = fabsf(cosf(a)), sa = fabsf(sinf(a));
        if (!overlap(p->x, p->y, ph, ph, ox, oy, hw * ca + hh * sa, hw * sa + hh * ca) ||
            !obb_overlap(p->x, p->y, ph, ph, p->rot * DEG, ox, oy, hw, hh, a)) continue;
      } else if (!overlap(p->x, p->y, ph, ph, ox, oy, hw, hh)) {
        /* a ring still counts where the player was a tick ago (gdsolver) */
        bool ring = (d->special >= SP_ORB_Y && d->special <= SP_ORB_B) || d->special == SP_ORB_G;
        if (!ring || !overlap(old->x, old->y, ph, ph, ox, oy, hw, hh)) continue;
      }
      if (pass == 0) {
        if ((d->special >= SP_ORB_Y && d->special <= SP_ORB_B) || d->special == SP_ORB_G) {
          touching_orb = true;
          if (!p->orb_touching && !game_used(g, o, i)) fx(g, FX_ORB_TOUCH, 0, (int)i, ox, oy);
        }
        special(g, i, o, hold, near[k].x, near[k].y);
      } else if (pass == 1) {
        if (d->shape == SHAPE_SLOPE) continue;   /* slopes: see slope_pass */
        solid(g, i, ox, oy, hw, hh);
      } else {
        kill(g, (int)i);
      }
    }
    if (pass == 1 && !g->dead) ride(g, old, near, n);
  }
  p->orb_touching = touching_orb;
  /* a turned frame has no ground and no band (portals there set none) */
  if (g->frame) { p->ground_y = -1e6f; p->ceiling_y = 1e6f; }
}

static void cube_mode(Game *g, bool hold, bool pressed) {
  Player *p = &g->p;
  float mult = p->rot_dir_neg ? -1.0f : 1.0f, spin = p->mini ? ROT_SPEED_MINI : ROT_SPEED;
  p->gravity = CUBE_GRAV[p->speed];
  if (p->vy < -810) p->vy = -810;
  if (p->vy > 1080) p->vy = 1080;
  if (p->y > (g->L->ext ? 3594 : 2794) && !g->frame) kill(g, -1);   /* above the level (GD 2.2 lets levels go higher) */
  if (p->snap_rot) p->target_rot = p->rot;
  if (!p->on_ground) {
    if (p->inverse_rot) p->target_rot -= spin / 2 * TDT * mult;
    else p->target_rot += spin * TDT * mult;
  }
  if (p->on_ground) update_rot_dir(p);
  bool coyote = (p->upside || g->dual) && hold && p->coyote < 10;
  /* the slope under the cube, or the one it just slid off */
  Slope sl = {0};
  int so = p->sl_o >= 0 ? p->sl_o : p->co_ticks ? p->co_o : -1;
  if (so >= 0) slope_get(p->sl_o >= 0 ? p->sl : p->co, so, &sl);
  bool on_slope = so >= 0 && sl_orient(p, &sl) < OR_UDD;
  if ((on_slope || p->on_ground || coyote) && hold && !p->no_jump) {
    if (on_slope && sl_orient(p, &sl) == OR_NU) {
      /* a jump off a rising slope gets some of its speed */
      float t = clampf(10 * (p->t_elapsed - (p->sl_o >= 0 ? p->sl_t : p->co_t)), 0.4f, 1.0f);
      float v = 0.9f * fminf(1.12f / sl_angle(p, &sl), 1.54f) * (sl.h * SPEEDS[p->speed] / sl.w);
      set_velocity(p, 0.25f * t * v + CUBE_JUMP[p->speed], false);
      p->co_ticks = 0;
      p->co_o = -1;
    } else {
      set_velocity(p, CUBE_JUMP[p->speed], g->hold_prev);
    }
    p->inverse_rot = false;
    p->buffer = BUF_END;
    p->on_ground = false;
    p->jumped = true;
    g->jumps++;
    if (!pressed) p->time_since_ground = DRAG_TIME;
    fx(g, FX_JUMP, 0, -1, p->x, p->y);
  }
  if (so >= 0) p->target_rot = closest_rotation_to(p->rot, sl_snap_angle(p, &sl) / DEG);
  else if (p->on_ground) p->target_rot = closest_rotation(p->rot);
}

static void ship_mode(Game *g, bool hold) {
  Player *p = &g->p;
  boost_check(p);
  float t = grav(p, VEL_THRESH[p->speed]);
  bool m = p->mini;
  if (hold) {
    p->buffer = BUF_END;
    p->gravity = p->vy <= t ? (m ? 1643.5872f : 1397.0491f) : (m ? 1314.86976f : 1117.64328f);
  } else {
    p->gravity = p->vy >= t ? (m ? -1577.85408f : -1341.1719f) : (m ? -1051.8984f : -894.11464f);
  }
  float lo = m ? SHIP_MINI_MIN : SHIP_MIN, hi = m ? SHIP_MINI_MAX : SHIP_MAX;
  if (p->boost) return;
  if (p->gravity < 0 && p->vy < lo) p->vy = lo;
  else if (p->gravity > 0 && p->vy > hi) p->vy = hi;
}

/* UFO: a press flaps up to a fixed speed (a held button also flaps right
   after entering from the cube, ship or wave, or after a respawn). */
static void ufo_mode(Game *g, const Player *old, bool pressed, bool hold) {
  Player *p = &g->p;
  boost_check(p);
  bool m = p->mini;
  bool carry = hold && (old->mode == MODE_CUBE || old->mode == MODE_SHIP || old->mode == MODE_WAVE || p->ufo_buf);
  if (p->buffer == BUF_READY && (pressed || carry)) {
    p->vy = fmaxf(p->vy, UFO_FLAP[m]);
    p->buffer = BUF_END;
    p->vel_override = true;
    g->jumps++;
    fx(g, FX_JUMP, 1, -1, p->x, p->y);
  } else {
    p->gravity = p->vy > grav(p, VEL_THRESH[p->speed]) ? UFO_GRAV_HI[m] : UFO_GRAV_LO[m];
  }
  float lo = m ? SHIP_MINI_MIN : SHIP_MIN, hi = m ? SHIP_MINI_MAX : SHIP_MAX;
  if (!p->boost) p->vy = clampf(p->vy, lo, hi);
}

/* Robot (GD 2.2, read by gdsolver): 0.9 of the cube's gravity, half its
   jump, and a hover while the button stays held after a jump (at most 67
   ticks; letting go ends it). Only a fresh press jumps. */
static void robot_mode(Game *g, bool hold, bool pressed) {
  Player *p = &g->p;
  p->gravity = CUBE_GRAV[p->speed] * 0.9f;
  if (p->vy < -810) p->vy = -810;
  if (p->vy > 1080) p->vy = 1080;
  if (!hold) p->hover = 0;
  bool coyote = p->upside && hold && p->coyote < 10;
  if ((p->on_ground || coyote) && p->buffer == BUF_READY) {
    /* no gravity on the jump's own tick: the hover holds the full speed */
    set_velocity(p, CUBE_JUMP[p->speed] * 0.5f, true);
    p->hover = 67;
    p->buffer = BUF_END;
    p->on_ground = false;
    p->jumped = true;
    g->jumps++;
    if (!pressed) p->time_since_ground = DRAG_TIME;
    fx(g, FX_JUMP, 2, -1, p->x, p->y);
  } else if (p->hover && hold && !p->on_ground) {
    p->hover--;
    p->gravity = 0;
  }
  p->rot = 0;
}

/* Wave: straight lines at 45 degrees (twice as steep when mini). */
static void wave_mode(Game *g, bool hold) {
  Player *p = &g->p;
  if (p->buffer == BUF_READY) p->buffer = BUF_END;
  p->gravity = 0;
  p->vy = (hold ? 1.f : -1.f) * SPEEDS[p->speed] * (p->mini ? 2 : 1);
}

/* A press on a floor or ceiling flips gravity, with a small push towards the
   other side (as in GD, a press just before landing counts: the buffer). */
static void ball_mode(Game *g, const Player *old) {
  Player *p = &g->p;
  p->gravity = BALL_GRAV;
  if (p->on_ground || p->on_ceiling) p->spin = 1;
  bool coyote = (p->upside || g->dual) && p->coyote < 16;
  if ((p->on_ground || p->on_ceiling || coyote) && p->buffer == BUF_READY) {
    p->upside = !p->upside;
    set_velocity(p, BALL_JUMP[p->speed], old->buffer == BUF_READY);
    p->buffer = BUF_END;
    p->spin = -BALL_AIR_SPIN;
    p->on_ground = false;
    p->jumped = true;
    g->jumps++;
    fx(g, FX_JUMP, 0, -1, p->x, p->y);
  }
  if (p->vy < -810) p->vy = -810;
  if (p->vy > 810) p->vy = 810;
}

/* Spider: a ball's fall; a press on a surface teleports it to the one on
   the other side (spider_jump). */
static void spider_mode(Game *g) {
  Player *p = &g->p;
  p->gravity = BALL_GRAV;
  bool coyote = (p->upside || g->dual) && p->coyote < 16;
  if ((p->on_ground || p->on_ceiling || coyote) && p->buffer == BUF_READY) {
    p->buffer = BUF_END;
    p->jumped = true;
    g->jumps++;
    spider_jump(g);
    p->vel_override = true;
  }
  if (p->vy < -810) p->vy = -810;
  if (p->vy > 810) p->vy = 810;
}

/* Swing: a steady pull towards its gravity; each press flips gravity and
   keeps 0.8 of the speed (gdsolver). */
static void swing_mode(Game *g) {
  Player *p = &g->p;
  boost_check(p);
  /* a tap flips gravity one tick after the press (gdsolver), keeping 0.8 of
     the speed; the pending flip rides the robot's hover counter */
  if (p->hover) {
    p->hover = 0;
    p->upside = !p->upside;
    p->vy = -0.8f * p->vy;
    p->ceil_inv = CEILING_INVUL;
    flip_other(g);
  }
  if (p->buffer == BUF_READY) {
    p->buffer = BUF_END;
    p->hover = 1;
    g->jumps++;
    fx(g, FX_JUMP, 1, -1, p->x, p->y);
  }
  p->gravity = -(p->mini ? SWING_G_MINI : SWING_G);
  if (!p->boost) p->vy = clampf(p->vy, -SWING_TERM, SWING_TERM);
}

static void run_player(Game *g, const Player *old, bool hold, bool pressed) {
  Player *p = &g->p;
  float rest = rest_half(p);
  if (!p->left_ground) {
    if (p->y - rest <= p->ground_y) {
      if (p->upside) p->on_ceiling = true; else { p->on_ground = true; landing(p); }
      p->inverse_rot = false;
      p->time_since_ground = 0;
    }
    if (p->y + rest >= p->ceiling_y) {
      if (p->upside) { p->on_ground = true; landing(p); } else p->on_ceiling = true;
      p->inverse_rot = false;
      p->time_since_ground = 0;
    }
  }
  if (!old->on_ground && p->on_ground) fx(g, FX_LAND, 0, -1, p->x, p->y);
  if (grav_bottom(old) > grav_floor(old) && p->upside == old->upside && !p->on_ground && p->vy <= 0) {
    if (old->on_ground && !g->hold_prev) p->coyote = 0;
    p->coyote++;
  } else {
    p->coyote = 1 << 30;
  }
  if (p->dash && (!hold || p->mode == MODE_WAVE)) p->dash = false;
  if (p->dash) {
    /* a dash ring: a straight line at its angle while the button is held */
    p->vy = grav(p, p->dash_slope * SPEEDS[p->speed]);
    p->vel_override = true;
    p->buffer = BUF_END;
  } else if (p->mode == MODE_CUBE) cube_mode(g, hold, pressed);
  else if (p->mode == MODE_SPIDER) spider_mode(g);
  else if (p->mode == MODE_SWING) swing_mode(g);
  else if (p->mode == MODE_BALL) ball_mode(g, old);
  else if (p->mode == MODE_UFO) ufo_mode(g, old, pressed, hold);
  else if (p->mode == MODE_ROBOT) robot_mode(g, hold, pressed);
  else if (p->mode == MODE_WAVE) wave_mode(g, hold);
  else ship_mode(g, hold);
  p->time_since_ground += TDT;
  if (!p->vel_override) {
    float nv = p->vy + p->gravity * TDT;
    if (!(p->on_ground || p->on_ceiling) && (old->on_ground || old->on_ceiling) &&
        ((!hold && g->pressed_prev) || p->buffer == BUF_READY) && grav_bottom(old) > grav_floor(old) && p->mini == old->mini) {
      p->y += grav(old, old->gravity) * TDT * TDT;
      if (p->vy == 0) nv += old->gravity * TDT;
    }
    /* GD 2.2 keeps y velocity on a grid of 0.001 of its own unit (54 units
       per second), rounded inside the gravity step (measured by gdsolver) */
    p->vy = roundf(nv * (1000 / 54.f)) * (54 / 1000.f);
  }
  if (g->ending) return;
  p->rot = fmodf(p->rot, 360.0f);
  p->left_ground = false;
  if (p->ceil_inv > 0) p->ceil_inv -= TDT; else p->ceil_inv = 0;
  clamp_ground(g);
  if (p->co_ticks && !--p->co_ticks) p->co_o = -1;
  if (p->sl_o >= 0) {
    Slope cur;
    slope_get(p->sl, p->sl_o, &cur);
    sl_calc(g, old, &cur, &near_slopes, hold);
  }
  if (p->mode == MODE_CUBE) {
    float lerp = SPEED_MULT[p->speed] * 0.175f;
    if (p->on_ground || p->sl_o >= 0) {
      lerp *= 3;
      float t = fminf(TDT, TDT * lerp) * 60;
      p->rot = slerp_angle(p->rot * DEG, p->target_rot * DEG, t) / DEG;
    } else {
      p->rot = p->target_rot;
    }
  } else if (p->mode == MODE_BALL) {
    p->rot += p->spin * BALL_ROLL[p->speed] * (p->mini ? 1.25f : 1) * (p->upside ? -1 : 1) * TDT;
  } else if (p->mode == MODE_ROBOT || p->mode == MODE_SPIDER) {
    p->rot = 0;
  } else if (p->mode == MODE_SWING) {
    float dy = p->y - old->y;
    p->rot += (clampf(-dy * 6, -30, 30) - p->rot) * 0.15f;
  } else if (p->mode == MODE_WAVE) {
    /* the dart eases towards its travel angle (gdsolver's fit of GD) */
    float dy = p->y - old->y, target = fabsf(dy) < 1e-4f ? 0 : (p->mini ? 63.435f : 45.f) * (dy < 0 ? 1 : -1);
    float r = fmodf(p->rot + 540, 360) - 180;
    p->rot = r + (p->mini ? 0.1f : 0.0625f) * (target - r);
  } else {
    float dx = p->x - old->x, dy = p->y - old->y, ang = atan2f(-dy, dx);
    if (p->snap_rot) p->rot = ang / DEG;
    else if (TDT * 72 <= dx * dx + dy * dy) {
      float k = 0.15f;
      if (p->mode == MODE_UFO) {  /* the UFO only tilts a little */
        k = 0.07f;
        if (p->sl_o >= 0) {
          Slope cur;
          slope_get(p->sl, p->sl_o, &cur);
          ang = sl_snap_angle(p, &cur);
        } else {
          ang = p->on_ground ? 0 : p->upside ? fminf(ang * -0.4f, 0.1f) : fmaxf(ang * -0.4f, -0.1f);
        }
      }
      p->rot = slerp_angle(p->rot * DEG, ang, TDT * 60 * k) / DEG;
    }
  }
  p->snap_rot = false;
  p->ufo_buf = false;
}

static void ease_channel(Channel *c) {
  if (c->t >= c->dur) { memcpy(c->cur, c->to, 3); return; }
  c->t++;
  for (int k = 0; k < 3; k++) c->cur[k] = (uint8_t)(c->from[k] + ((int)c->to[k] - c->from[k]) * (int)c->t / (int)c->dur);
}
static void fire_event(Game *g, const LevelEvent *e) {
  if (e->kind == EV_COLOR) {
    int chans[2] = {e->arg, -1};
    if (e->arg == CH_BG && (e->flags & EVF_TINT_GROUND)) chans[1] = CH_G1;
    for (int k = 0; k < 2; k++) {
      if (chans[k] < 0 || chans[k] >= CH_COUNT) continue;
      Channel *c = &g->ch[chans[k]];
      memcpy(c->from, c->cur, 3);
      c->to[0] = e->r; c->to[1] = e->g; c->to[2] = e->b;
      c->t = 0;
      c->dur = (uint16_t)((uint32_t)e->dur * ND_HZ / 1000);
      if (!c->dur) memcpy(c->cur, c->to, 3);
    }
  } else if (e->kind == EV_FADE) {
    g->fade_effect = e->arg;
  } else if (e->kind == EV_TRAIL) {
    g->trail = e->arg != 0;
  }
}
static void triggers(Game *g) {
  const Level *L = g->L;
  Player *p = &g->p;
  while (g->next_event < L->event_count) {
    const LevelEvent *e = &L->events[g->next_event];
    if (e->x >= p->x) break;
    if (!(e->flags & EVF_TOUCH)) fire_event(g, e);
    g->next_event++;
  }
  /* touch-triggered events fire once when the player overlaps them */
  for (unsigned k = 0; k < L->event_count && k < 64; k++) {
    const LevelEvent *e = &L->events[k];
    if (!(e->flags & EVF_TOUCH) || (g->touch_done[k >> 3] >> (k & 7) & 1)) continue;
    if (e->x > p->x + 30 || e->x < p->x - 60) continue;
    if (overlap(p->x, p->y, 15, 15, e->x, e->y, 15, 15)) {
      g->touch_done[k >> 3] |= (uint8_t)(1u << (k & 7));
      fire_event(g, e);
    }
  }
  for (int c = 0; c < CH_COUNT; c++) ease_channel(&g->ch[c]);
}

static float ease_in_out(float t, float rate) {
  t *= 2;
  if (t < 1) return 0.5f * powf(t, rate);
  return 1 - 0.5f * powf(2 - t, rate);
}

static void camera(Game *g) {
  Player *p = &g->p;
  const Level *L = g->L;
  if (g->menu_camera) {
    float v = SPEEDS[SPEED_NORMAL] * TDT;
    g->ground_x += v;
    g->bg_x += v;
    g->ground_gfx = 0;
    return;
  }
  float playable = p->ceiling_y - p->ground_y, want_gfx = 0;
  bool tall = playable > VIEW_H - 40;   /* a band taller than the view: follow the player */
  if (p->mode != MODE_CUBE && !tall) want_gfx = (VIEW_H - playable) / 2;
  g->ground_gfx += (want_gfx - g->ground_gfx) * 0.02f;
  if (g->frame == 0 && g->wall_y == 0 && g->cam_x + VIEW_W >= L->wall_x - 4.5f * 30) {
    float mid = g->cam_y + VIEW_H / 2 - GROUND_OFFSET, lo = 60 + (VIEW_H / 2 - GROUND_OFFSET);
    g->wall_y = mid > lo ? mid : lo;
  }
  if (g->frame == 0 && g->wall_y > 0 && g->cam_x + VIEW_W >= L->wall_x - 60) {
    if (g->cam_wall_t == 0) { g->cam_wall_y0 = g->cam_y; g->bg_wall_x0 = g->bg_x; g->ground_wall_x0 = g->ground_x; }
    float t = clampf(g->cam_wall_t / 1.0f, 0, 1), e = ease_in_out(t, 2);
    float fx_ = L->wall_x - VIEW_W, fy = g->wall_y - (VIEW_H / 2 - GROUND_OFFSET);
    g->cam_x = fx_ - 60 + 60 * e;
    g->cam_y = g->cam_wall_y0 + (fy - g->cam_wall_y0) * e;
    g->bg_x = g->bg_wall_x0 + 60 * e;
    g->ground_x = g->ground_wall_x0 + 60 * e;
    g->cam_wall_t += TDT;
    return;
  }
  float target = g->cam_y;
  bool floor = g->frame == 0 && !g->cam_static;
  if (p->mode == MODE_CUBE || p->mode == MODE_ROBOT || p->mode == MODE_SPIDER || !floor || tall) {
    float off = p->upside ? -30.0f : 0;
    if (p->y > g->cam_y + 180 + off) target = p->y - (180 + off);
    else if (p->y < g->cam_y + 30 + off) target = p->y - (30 + off);
  } else {
    target = g->cam_intended_y;
  }
  float want = p->x - PLAYER_SCREEN_X;
  if (g->cam_static) {
    /* a static camera: centred on its target (world, eased) */
    float u, v;
    to_local(g, g->cam_tx, g->cam_ty, &u, &v);
    if (g->cam_axis != 2) want = u - VIEW_W / 2;
    if (g->cam_axis != 1) target = v - (VIEW_H / 2 - GROUND_OFFSET);
  }
  want += g->cam_off_x;
  target += g->cam_off_y;
  if (floor && target < 0) target = 0;
  g->cam_y += (target - g->cam_y) / 40.0f;
  if (floor && g->cam_y < 0) g->cam_y = 0;
  if (g->cam_static) want = g->cam_x + (want - g->cam_x) / 30.0f;
  if (g->attempt_camera && want < 15) want = 15;
  float moved = want - g->cam_x;
  g->cam_x = want;
  if (moved > 0) { g->ground_x += moved; g->bg_x += moved; }
}

/* The frame's travel and up directions in the world. */
static void frame_axes(unsigned f, float *fx_, float *fy, float *ux, float *uy) {
  static const float A[4][4] = {{1, 0, 0, 1}, {0, -1, 1, 0}, {-1, 0, 0, -1}, {0, 1, -1, 0}};
  *fx_ = A[f & 3][0]; *fy = A[f & 3][1]; *ux = A[f & 3][2]; *uy = A[f & 3][3];
}

void game_rotate(Game *g, unsigned travel, unsigned gravity, unsigned flags, float vmod) {
  static const int8_t FRAME_OF[5] = {-1, 3, 1, 2, 0};   /* GD direction (1 up .. 4 right) -> frame */
  static const float GDIR[5][2] = {{0, 0}, {0, 1}, {0, -1}, {-1, 0}, {1, 0}};
  if (travel < 1 || travel > 4) return;
  unsigned nf = (unsigned)FRAME_OF[travel];
  Player *p = &g->p;
  if (g->rebuilding) {
    /* practice rebuild: the log says where along the new travel it was */
    g->frame = (uint8_t)nf;
    for (unsigned k = 0; k < g->nlog; k++)
      if (g->log[k].kind == LOG_FRAME && g->log[k].tick == g->tick) { p->x = g->log[k].val; break; }
    return;
  }
  /* PlayerObject::rotateGameplay, as gdsolver read it: when the travel
     turns between horizontal and vertical the y speed becomes the travel
     speed (0.9 of it in these units) times the trigger's modifier, along
     GD's own up; otherwise it is kept, halved when the gravity turns over.
     Our frames 2 and 3 have their v axis opposite to GD's up, so GD's
     upside-down flag reads inverted there. */
  float fx_, fy, ux, uy;
  unsigned of = g->frame;
  float vloc = grav(p, p->vy);   /* along the old v axis */
  bool gd_up_old = p->upside ^ (of >= 2);
  float X, Y, cx, cy;
  to_world(g, p->x, p->y, &X, &Y);
  float sy = p->y - g->cam_y;
  to_world(g, g->cam_x + PLAYER_SCREEN_X, g->cam_y, &cx, &cy);
  g->frame = (uint8_t)nf;
  frame_axes(nf, &fx_, &fy, &ux, &uy);
  to_local(g, X, Y, &p->x, &p->y);
  if (gravity >= 1 && gravity <= 4) p->upside = GDIR[gravity][0] * ux + GDIR[gravity][1] * uy > 0.5f;
  bool gd_up = p->upside ^ (nf >= 2);
  if ((of ^ nf) & 1) {
    /* along GD's own up: +Y for horizontal travel, +X for vertical */
    float m = (flags & 2) ? vmod : 1;
    float v = (flags & 4) ? m * 54 : 0.9f * SPEEDS[p->speed] * m;
    vloc = nf >= 2 ? -v : v;
  } else {
    if ((of ^ nf) & 2) vloc = -vloc;   /* the v axis turned over, the world speed stays */
    if (gd_up != gd_up_old) vloc *= 0.5f;
  }
  p->vy = p->upside ? -vloc : vloc;
  p->ground_y = nf ? -1e6f : 0;
  p->ceiling_y = 1e6f;
  p->on_ground = p->on_ceiling = false;
  p->left_ground = true;
  p->has_snap = false;
  p->dash = false;
  sl_clear(p);
  p->co_o = -1;
  p->co_ticks = 0;
  g->cam_x = p->x - PLAYER_SCREEN_X;
  g->cam_y = p->y - sy;
  trig_log(g, LOG_FRAME, nf, p->x);
}

/* One tick of the body held in g->p. */
static void step_body(Game *g, bool hold, bool pressed) {
  Player *p = &g->p;
  Player old = *p;
  p->old_on_ground = p->on_ground;
  if (p->arm_head) p->arm_head--;
  if (p->arm_flip) p->arm_flip--;
  if (p->arm_noauto) p->arm_noauto--;
  if (p->arm_slide) p->arm_slide--;
  p->no_jump = false;
  if (hold) { if (p->buffer == BUF_NONE) p->buffer = BUF_READY; }
  else p->buffer = BUF_NONE;
  p->on_ground = p->on_ceiling = false;
  p->vel_override = false;
  p->t_elapsed += TDT;
  /* a zoomed camera stretches the flying band (GD: height / zoom) */
  if (g->zoom != 1 && p->band_h > 0 && p->ceiling_y < 1e5f && !g->dual && !g->frame) {
    p->ceiling_y = p->ground_y + p->band_h / g->zoom;
    g->cam_intended_y = (p->ground_y + p->ceiling_y) / 2 - (VIEW_H / 2 - GROUND_OFFSET);
  }
  p->x += SPEEDS[p->speed] * TDT;
  p->y += grav(p, p->vy) * TDT;
  clamp_ground(g);
  p->frame++;
  /* what a slope left for this tick: a launch speed and letting go of it */
  if (p->has_new_vy) { p->vy = p->new_vy; p->has_new_vy = false; }
  if (p->pend_clear) { sl_clear(p); p->pend_clear = false; }
  if (!g->dead && !g->ending) collide(g, &old, hold);
  if (g->dead) return;
  /* the first body's move fires the position triggers it passed, before
     the button (GD's tick order) */
  if (!p->second && g->L->ext && !g->ending) trig_cross(g);
  const Level *L = g->L;
  if (g->frame == 0 && p->x >= L->wall_x - END_START && !p->second) {
    if (!g->ending) {
      g->ending = true;
      g->end_x0 = p->x;
      g->end_y0 = p->y;
      if (g->wall_y == 0) g->wall_y = g->end_y0 > 60 ? g->end_y0 : 60;
    }
    float t = clampf(powf(g->end_t, 1.2f), 0, 1), u = 1 - t;
    float mx = g->end_x0 + 40, my = g->wall_y + 150, ex = L->wall_x + 50, ey = g->wall_y - 20;
    p->x = u * u * u * g->end_x0 + 3 * u * u * t * g->end_x0 + 3 * u * t * t * mx + t * t * t * ex;
    p->y = u * u * u * g->end_y0 + 3 * u * u * t * g->end_y0 + 3 * u * t * t * my + t * t * t * ey;
    float e = g->end_t / 0.5f;
    p->rot += (e > 1 ? 1 : e * e) * ROT_SPEED * TDT;
    g->end_t += TDT;
    if (p->x > L->wall_x && !g->complete) {
      g->complete = true;
      fx(g, FX_WALL, 0, -1, (float)L->wall_x, g->wall_y);
    }
  }
  run_player(g, &old, hold, pressed);
  p->delta_y = p->y - old.y;
}

/* The ship and the UFO read the button one tick later than the other modes
   (GD's tick order, measured by gdsolver). */
static bool reads_late(const Player *p) { (void)p; return false; }

void game_step(Game *g, bool hold) {
  if (g->dead || g->complete) return;
#if ND_EXTRA_LEVELS
  if (g->no_input) hold = false;
  /* the mirror flips over half a second */
  float mt = g->mirror ? 1 : 0;
  if (g->mirror_t != mt) g->mirror_t = g->mirror_t < mt ? fminf(mt, g->mirror_t + ND_DT * 2) : fmaxf(mt, g->mirror_t - ND_DT * 2);
#endif
  bool pressed = hold && !g->hold_prev, pressed_late = g->hold_prev && !g->hold_prev2;
  float x0 = g->p.x;
  g->tick++;
  TDT = ND_DT * g->time_mod;
  g->dist += SPEEDS[g->p.speed] * TDT;
  bool late = reads_late(&g->p);
  step_body(g, late ? g->hold_prev : hold, late ? pressed_late : pressed);
  if (g->dual && !g->dead && !g->complete) {
    Player t = g->p;
    g->p = g->p2;
    g->p2 = t;
    late = reads_late(&g->p);
    step_body(g, late ? g->hold_prev : hold, late ? pressed_late : pressed);
    if (g->dual_swap) {
      /* the second body left dual mode first: it goes on alone */
      g->dual_swap = false;
      g->p.second = false;
    } else {
      t = g->p;
      g->p = g->p2;
      g->p2 = t;
    }
  }
  g->hold_prev2 = g->hold_prev;
  g->hold_prev = hold;
  g->pressed_prev = pressed;
  if (!g->dead) {
    if (pressed && g->L->ext) trig_tap(g);
    camera(g);
    triggers(g);
    trig_step(g, g->p.x - x0);
    if (g->end_trig && !g->complete) {
      /* an end trigger: the level is complete where it fired */
      g->complete = true;
      fx(g, FX_WALL, 0, -1, g->p.x, g->p.y);
    }
  }
}

void game_save_checkpoint(const Game *g, Checkpoint *c) {
  c->p = g->p;
  c->cam_x = g->cam_x; c->cam_y = g->cam_y; c->ground_x = g->ground_x; c->bg_x = g->bg_x;
  c->ground_gfx = g->ground_gfx; c->cam_intended_y = g->cam_intended_y;
  memcpy(c->ch, g->ch, sizeof(c->ch));
  c->tick = g->tick; c->next_event = g->next_event; c->jumps = g->jumps;
  c->fade_effect = g->fade_effect; memcpy(c->touch_done, g->touch_done, sizeof(c->touch_done)); c->trail = g->trail;
  c->nlog = g->nlog;
  c->dist = g->dist;
  c->dual = g->dual;
  c->dual_y = g->dual_y;
  const Player *q = &g->p2;
  c->p2_mode = q->mode;
  c->p2_flags = (uint8_t)(q->upside | q->mini << 1 | q->on_ground << 2 | q->on_ceiling << 3 | q->second << 4);
  c->p2_y = q->y; c->p2_vy = q->vy; c->p2_rot = q->rot;
#if ND_EXTRA_LEVELS
  c->mirror = g->mirror;
#endif
}

/* Replays a 2.0 level's triggers for the first `ticks` ticks of the run,
   from the player's forward coordinate (its speed changes and gameplay
   rotations are in the log) and the touch triggers, taps and toggle rings
   the log recorded. Stair snaps nudge x by a unit or so; the rebuilt
   timings can be off by that much. */
static void trig_rebuild(Game *g, uint32_t ticks) {
  Player keep = g->p;
  g->rebuilding = true;
  g->p.x = 0;
  g->p.y = 15;
  unsigned sp = SPEED_NORMAL, li = 0, n = g->nlog;
  for (uint32_t t = 1; t <= ticks; t++) {
    float x0 = g->p.x;
    g->p.x += SPEEDS[sp] * ND_DT * g->time_mod;
    g->tick = t;
    for (; li < n && g->log[li].tick <= t; li++) {
      const LogEntry *e = &g->log[li];
      if (e->kind == LOG_SPEED) sp = e->arg & 3;
      else if (e->kind == LOG_TOUCH) trig_touch(g, e->arg);
      else if (e->kind == LOG_TAP) trig_tap(g);
      else if (e->kind == LOG_TOGGLE) trig_toggle_ring(g, (unsigned)e->val);
    }
    trig_cross(g);
    trig_step(g, g->p.x - x0);
  }
  g->rebuilding = false;
  uint8_t frame = g->frame;
  g->p = keep;
  g->frame = frame;
}

void game_load_checkpoint(Game *g, const Checkpoint *c) {
  const Level *L = g->L;
  uint8_t coins = g->coins;
  static LogEntry log[MAX_LOG];
  uint16_t nlog = c->nlog <= g->nlog ? c->nlog : g->nlog;
  memcpy(log, g->log, nlog * sizeof(LogEntry));
  game_start(g, L, false);
  memcpy(g->log, log, nlog * sizeof(LogEntry));
  g->nlog = nlog;
  if (L->ext) trig_rebuild(g, c->tick);
  g->p = c->p;
  if (c->dual) {
    g->dual = true;
    g->dual_y = c->dual_y;
    Player *q = &g->p2;
    *q = c->p;
    q->mode = c->p2_mode;
    q->upside = c->p2_flags & 1; q->mini = c->p2_flags >> 1 & 1; q->on_ground = c->p2_flags >> 2 & 1;
    q->on_ceiling = c->p2_flags >> 3 & 1; q->second = c->p2_flags >> 4 & 1;
    q->y = c->p2_y; q->vy = c->p2_vy; q->rot = c->p2_rot;
    q->sl_o = q->co_o = -1;
    q->buffer = BUF_NONE;
  }
  g->p.buffer = BUF_NONE;
  g->p.ufo_buf = true;
  g->cam_x = c->cam_x; g->cam_y = c->cam_y; g->ground_x = c->ground_x; g->bg_x = c->bg_x;
  g->ground_gfx = c->ground_gfx; g->cam_intended_y = c->cam_intended_y;
  memcpy(g->ch, c->ch, sizeof(c->ch));
  g->tick = c->tick; g->next_event = c->next_event; g->jumps = c->jumps;
  g->fade_effect = c->fade_effect; g->trail = c->trail;
  memcpy(g->touch_done, c->touch_done, sizeof(g->touch_done));
  g->coins = coins;
  g->dist = c->dist;
#if ND_EXTRA_LEVELS
  g->mirror = c->mirror;
  g->mirror_t = c->mirror ? 1 : 0;
#endif
  /* Objects behind the checkpoint can no longer be reached; they count as
   * used so coins and pads there do not fire again (in world x, so only
   * when the travel has not turned). */
  g->used_below = g->frame ? -1e9f : c->p.x - 60;
}
