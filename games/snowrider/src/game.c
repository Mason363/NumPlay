/* Snow Rider 3D: the sled, the camera and the game's moments, as the game's scripts have them.
 *
 * PlayerControl steers (left and right turn the sled up to 120 degrees, held jump jumps whenever it lands),
 * drives it ahead at a speed growing with the score, and ends the run when one of its three collision rays meets
 * an obstacle or it falls. SledgePoint keeps the sled's point on the snow, with its gravity and its jump.
 * CameraControlC follows the sled's camera point from behind, leaning with the turns and the slopes. A crash
 * breaks the sled into its pieces, slows time down (SlowMotion) and turns the camera around them. */
#include "sr.h"
#include "save.h"

int mode = M_INTRO;
int score, best, gifts, gifts_game, plays, skin, sel, screen, shop_skin;
uint16_t owned = 1;
bool best_now;
float time_scale = 1, game_time, score_pop, end_wait;
bool game_running = true;
Sled sled;
Piece pieces[MAXP_PIECES];
float cam_pos[3], cam_rot[9];

/* ---------------------------------------------------------------- saves (save.c) */
static SaveFile sv;
bool guide_read;

static void save(void) {
  sv.best = (uint32_t)best, sv.gifts = (uint32_t)gifts, sv.plays = (uint32_t)plays;
  sv.owned = owned, sv.skin = (uint8_t)skin, sv.guide = guide_read;
  sr_save(&sv);
}

static void load(void) {
  sr_load(&sv);
  best = (int)sv.best, gifts = (int)sv.gifts, plays = (int)sv.plays;
  owned = sv.owned, skin = sv.skin, guide_read = sv.guide;
}

/* ---------------------------------------------------------------- shaking (PerlinShake) */
static float shake_t = 1, shake_dur = 0.5f, shake_speed, shake_seed;
static float cam_offset[2];

static float noise1(float x) {
  int i = (int)floorf(x);
  float f = x - (float)i;
  uint32_t a = (uint32_t)i * 2654435761u, b = (uint32_t)(i + 1) * 2654435761u;
  a ^= a >> 15, b ^= b >> 15;
  float u = f * f * (3 - 2 * f);
  return ((float)(a & 1023) + ((float)(b & 1023) - (float)(a & 1023)) * u) / 1023;
}

static void shake(float speed) { shake_t = 0, shake_speed = speed, shake_seed = rnd_range(-1000, 1000); }

static void shake_step(float unscaled) {
  if (shake_t >= shake_dur) {
    cam_offset[0] = cam_offset[1] = 0;
    return;
  }
  shake_t += unscaled;
  float p = shake_t / shake_dur, fade = p * 2 - 1;
  fade = fade < 0 ? 0 : fade > 1 ? 1 : fade;
  float n = shake_seed + shake_speed * p;
  cam_offset[0] = (noise1(n) * 2 - 1) * 0.1f * (1 - fade);
  cam_offset[1] = (noise1(n + 517.3f) * 2 - 1) * 0.1f * (1 - fade);
}

/* ---------------------------------------------------------------- the screen from black (TransitionControl) */
float fade = 1;   /* black over the picture (draw.c): from 1 at the start and on the way to the menu, to 0 in 1 s */

/* ---------------------------------------------------------------- slow motion */
static float slow_from, slow_to = 1, slow_time, slow_p = 1;
static void slow(float time, float amount) { slow_from = time_scale, slow_to = amount, slow_time = time, slow_p = time > 0 ? 0 : 1; }
static void slow_step(void) {
  if (slow_p >= 1) {
    time_scale = slow_to;
    return;
  }
  time_scale = slow_from + (slow_to - slow_from) * slow_p;
  slow_p += DT_REF / slow_time;
}

/* ---------------------------------------------------------------- the sled */
static const float point_start[3] = POINT_POS;
static const float rays[NRAYS][3] = RAYS;

void sled_model_matrix(float m[12]) {
  float t[3] = {sled.x, sled.y, sled.z}, one[3] = {1, 1, 1}, up[3] = {0, sled.model_y, 0}, a[12], b[12];
  m_trs(a, t, sled.yaw, one);
  m_trs(b, up, sled.pitch, one);
  m_mul(m, a, b);
}

/* the collision point's place on the ground plane (a child of the Sledge, turned with it) */
static void point_xz(float *x, float *z) {
  float v[3] = {point_start[0], 0, point_start[2]}, w[3];
  q_rot(sled.yaw, v, w);
  *x = sled.x + w[0], *z = sled.z + w[2];
}

static void sled_spawn(void) {
  /* PlayerControl.Spawn and SledgePoint.Spawn */
  sled.speed = 0, sled.vrot = 0;
  sled.hrot = 2 * atan2f(sled.yaw.y, sled.yaw.w) / DEG;
  sled.vspeed = 0, sled.py = sled.y + point_start[1];
  sled.alive = true, sled.pgrounded = true, sled.last_vt = DT_REF;
}

static void sled_to_camp(void) {
  const float p[3] = SLEDS_POINT, q[4] = SLEDS_POINT_ROT;
  sled.x = p[0], sled.y = p[1], sled.z = p[2];
  sled.yaw = (Quat){q[0], q[1], q[2], q[3]};
  /* (only its turn about y: the Sledge never leans) */
  float yaw = 2 * atan2f(sled.yaw.y, sled.yaw.w);
  sled.yaw = (Quat){0, sinf(yaw / 2), 0, cosf(yaw / 2)};
  sled.pitch = (Quat){0, 0, 0, 1};
  sled_spawn();
}

/* SledgePoint.U */
static void point_update(float dt) {
  float x, z;
  point_xz(&x, &z);
  sled.last_vt = dt;
  sled.pgrounded = false;
  float o[3] = {x, sled.py + 0.3f, z}, down[3] = {0, -1, 0}, dist;
  Hit h;
  if (dt <= 0) return;
  if (ray_cast(o, down, 0.3f, L_SURFACE, &h)) {
    sled.pgrounded = true;
    float v = (sled.py - h.p[1]) / dt;
    sled.vspeed = v > sled.vspeed ? v : sled.vspeed;
    sled.py = h.p[1];
    dist = sled.vspeed * dt;
  } else {
    float v0 = sled.vspeed;
    sled.vspeed -= GRAVITY_ACC * dt;
    dist = v0 * dt - GRAVITY_ACC * dt * dt * 0.5f - GRAVITY_ACC * DT_REF * dt * 0.5f;
  }
  float o2[3] = {x, sled.py, z};
  float l = -dist > 0 ? -dist : 0;
  if (l > 0 && ray_cast(o2, down, l, L_SURFACE, &h)) sled.pgrounded = true, sled.vspeed = 0, dist = 0;
  if (ray_cast(o2, down, 0.1f, L_SURFACE, &h)) sled.pgrounded = true;
  sled.py += dist;
}

static void point_jump(void) {
  if (sled.pgrounded && sled.vspeed < 0) sled.vspeed = 0;
  sled.vspeed += SLED_JUMPSPEED;
  sled.py += sled.vspeed * DT_REF * 2;
  sled.last_vt = DT_REF;
}

static float rot_limit(float cur, float dir) {
  if ((cur < 0 && dir == -1) || (cur > 0 && dir == 1)) return 1 - powf(fabsf(sled.hrot) / 120, 3);
  return 1;
}

static void control(uint32_t keys, float dt) {
  bool left = keys & SK_LEFT, right = keys & SK_RIGHT;
  if (left && !right) sled.hrot -= SLED_ROTATIONSPEED * dt * rot_limit(sled.hrot, -1);
  else if (right && !left) sled.hrot += SLED_ROTATIONSPEED * dt * rot_limit(sled.hrot, 1);
  bool jump = keys & (SK_UP | SK_OK);
  if (!jump || !sled.grounded) sled.already_jumped = false;
  if (jump && !sled.already_jumped && sled.grounded) {
    sled.already_jumped = true;
    point_jump();
    sled.jump_start = game_time;
  }
}

static void end_run(void);

/* the three rays ahead of the sled (the game switches its fourth, low one off), swept as far as it goes in a step */
static bool collision(float dt) {
  float m[12], sweep = sled.speed * dt > RAY_LEN ? sled.speed * dt : RAY_LEN, f0[3] = {0, 0, 1}, f[3];
  sled_model_matrix(m);
  m_dir(m, f0, f);
  for (int i = 0; i < NRAYS; i++) {
    float p[3], o[3];
    m_point(m, rays[i], p);
    for (int k = 0; k < 3; k++) o[k] = p[k] - f[k] * sweep;
    Hit h;
    if (ray_cast(o, f, sweep, L_OBSTACLE, &h)) return true;
  }
  return false;
}

static void touched(Inst *in, const Coll *c) {
  const Prefab *pf = &prefabs[in->prefab];
  float at[3] = {in->m[3], in->m[7], in->m[11]}, push[3] = {0, 0, 0}, im[12];
  /* (its turn, without its size: Transform.rotation, which a mirrored chunk's -1 does not turn, so that undone) */
  float mirror = m_det(in->m) < 0 ? -1 : 1;
  for (int i = 0; i < 12; i++) im[i] = i < 3 ? in->m[i] * mirror : in->m[i];
  float sx = sqrtf(im[0] * im[0] + im[4] * im[4] + im[8] * im[8]);
  float sy = sqrtf(im[1] * im[1] + im[5] * im[5] + im[9] * im[9]);
  float sz = sqrtf(im[2] * im[2] + im[6] * im[6] + im[10] * im[10]);
  float r00 = im[0] / sx, r11 = im[5] / sy, r22 = im[10] / sz;
  float w = sqrtf(fmaxf(0, 1 + r00 + r11 + r22)) / 2, k = w > 1e-4f ? 1 / (4 * w) : 0;
  Quat q = {(im[9] / sy - im[6] / sz) * k, (im[2] / sz - im[8] / sx) * k, (im[4] / sx - im[1] / sy) * k, w};
  if (w <= 1e-4f) q = (Quat){0, 0, 0, 1};
  if (c->layer & L_BREAK) shake(6);
  if (c->layer & L_GIFT) gifts++, gifts_game++;
  if (pf->pieces >= 0) pieces_spawn(pf->pieces, at, q, push);
  in->alive = 0;
}

static void touch(void) {
  const Skin *sk = &skins[skin];
  float m[12], b[12];
  sled_model_matrix(m);
  float t[12];
  m_trs(t, sk->box_c, (Quat){0, 0, 0, 1}, sk->box_h);
  m_mul(b, m, t);
  box_touch(b, L_BREAK | L_GIFT, touched);
}

/* PlayerControl.Jump: landing after more than a second in the air shakes the camera */
static void land(void) {
  bool was = sled.grounded;
  if (!sled.pgrounded) {
    if (was) sled.jump_start = game_time;
    sled.grounded = false;
  } else {
    if (!was && game_time - sled.jump_start > 1) shake(3);
    sled.grounded = true;
  }
}

static void sled_play(uint32_t keys, float dt) {
  if (sled.z >= 0.5f) control(keys, dt);
  /* Rotation */
  sled.yaw = q_lerp(sled.yaw, (Quat){0, sinf(sled.hrot * DEG / 2), 0, cosf(sled.hrot * DEG / 2)}, blend(1 - SLED_ROTATIONSMOOTHNESS));
  /* Position */
  sled.move_speed = SLED_BASEMOVESPEED + powf((float)score / SLED_SPEEDACCELERATION * SLED_SPEEDACCELERATIONAMPLITUDE, 0.55f);
  sled.speed += (sled.move_speed - sled.speed) * blend(0.01f);
  float k = sled.speed * cosf(sled.vrot * DEG) * dt;
  sled.x += sinf(sled.hrot * DEG) * k, sled.z += cosf(sled.hrot * DEG) * k;
  point_update(dt);
#ifdef HOST
  extern bool host_god;   /* (tests/play.c: the sled goes through everything) */
  if (host_god) {
    touch();
    land();
    if (sled.py < -10) sled.py = 2, sled.vspeed = 0;
    return;
  }
#endif
  if (collision(dt)) {
    end_run();
    return;
  }
  touch();
  land();
  if (sled.py < -10 || sled.py > 10) end_run();
}

/* ModelTransform */
static void sled_model(void) {
  sled.model_y = sled.py - sled.y;
  if (sled.move_speed != 0 && sled.last_vt > 0)
    sled.vrot = -atanf((sled.py - sled.prev_h) / (sled.move_speed * sled.last_vt)) / DEG;
  sled.pitch = q_lerp(sled.pitch, q_euler(sled.vrot, 0, 0), blend((1 - SLED_MODELROTATIONSMOOTHNESS) * time_scale));
  sled.prev_h = sled.py;
}

/* ---------------------------------------------------------------- pieces */
void pieces_spawn(int ch, const float at[3], Quat rot, const float push[3]) {
  const Chunk *c = &chunks[ch];
  for (int i = 0; i < c->n; i++) {
    int k = 0;
    while (k < MAXP_PIECES && pieces[k].alive) k++;
    if (k == MAXP_PIECES) k = (int)(rnd() % MAXP_PIECES);
    Piece *p = &pieces[k];
    const Obj *o = &c->obj[i];
    float d[3];
    q_rot(rot, o->pos, d);
    for (int j = 0; j < 3; j++) p->p[j] = at[j] + d[j], p->s[j] = o->scale[j];
    p->q = q_mul(rot, (Quat){o->rot[0], o->rot[1], o->rot[2], o->rot[3]});
    /* AddForce(forward * 100) for a step of 0.02 s: 2 m/s */
    float f0[3] = {0, 0, 1}, f[3];
    q_rot(p->q, f0, f);
    for (int j = 0; j < 3; j++) p->v[j] = push[j] + f[j] * 2 + rnd_range(-0.5f, 0.5f);
    p->v[1] += rnd_range(0.5f, 2);
    for (int j = 0; j < 3; j++) p->w[j] = rnd_range(-6, 6);
    p->prefab = o->prefab;
    p->alive = prefabs[o->prefab].nparts > 0;
  }
}

static void pieces_step(float dt) {
  for (int i = 0; i < MAXP_PIECES; i++) {
    Piece *p = &pieces[i];
    if (!p->alive) continue;
    p->v[1] -= 9.81f * dt;
    /* (a piece meets what the sled met: it bounces back off it) */
    float sp = sqrtf(p->v[0] * p->v[0] + p->v[1] * p->v[1] + p->v[2] * p->v[2]), step = sp * dt;
    if (step > 1e-5f) {
      float dir[3] = {p->v[0] / sp, p->v[1] / sp, p->v[2] / sp};
      Hit hw;
      if (ray_cast(p->p, dir, step + 0.05f, L_OBSTACLE, &hw)) {
        for (int j = 0; j < 3; j++) p->v[j] = -p->v[j] * 0.3f;
        continue;
      }
    }
    for (int j = 0; j < 3; j++) p->p[j] += p->v[j] * dt;
    Quat dq = q_euler(p->w[0] * dt / DEG, p->w[1] * dt / DEG, p->w[2] * dt / DEG);
    p->q = q_mul(dq, p->q);
    float o[3] = {p->p[0], p->p[1] + 1, p->p[2]}, d[3] = {0, -1, 0};
    Hit h;
    if (ray_cast(o, d, 1.05f, L_SURFACE, &h) && p->p[1] < h.p[1] + 0.03f) {
      p->p[1] = h.p[1] + 0.03f;
      if (p->v[1] < 0) p->v[1] = -p->v[1] * 0.3f;
      p->v[0] *= 0.9f, p->v[2] *= 0.9f;
      for (int j = 0; j < 3; j++) p->w[j] *= 0.8f;
    }
    if (p->p[1] < -30) p->alive = false;
  }
}

/* ---------------------------------------------------------------- the camera (CameraControlC) */
static float pivot[3], start_pos[3], cam_local[3];
static Quat pivot_rot, start_rot, cam_local_rot = {0, 0, 0, 1};
static float start_time, end_rx, end_ry, end_dir, end_pivot[3];
static bool locked;

static void cam_intro(void) {
  pivot[0] = 2.5f, pivot[1] = 10, pivot[2] = 10;
  pivot_rot = q_euler(50, 75, 0);
  cam_local[0] = -10, cam_local[1] = 0, cam_local[2] = 0;
  cam_local_rot = (Quat){0, 0, 0, 1};
}

/* OnMain: the pivot at the camp's CameraPoint, turned as it is, the camera 2 m ahead of it */
static void cam_main(void) {
  const float p[3] = CAMP_CAM_POINT, q[4] = CAMP_CAM_ROT;
  for (int k = 0; k < 3; k++) pivot[k] = p[k];
  pivot_rot = (Quat){q[0], q[1], q[2], q[3]};
  cam_local[0] = 0, cam_local[1] = 0, cam_local[2] = 2;
  cam_local_rot = (Quat){0, 0, 0, 1};
}

/* MainControl: it turns smoothly to the menu's angle, the sleds' (OnSleds: (1 - 1) * 72 - 6); (back from the
 * sleds' own camera, the pivot and camera go back too) */
static void cam_main_step(void) {
  const float p[3] = CAMP_CAM_POINT;
  for (int k = 0; k < 3; k++) pivot[k] = p[k];
  pivot_rot = q_lerp(pivot_rot, q_euler(0, -6, 0), blend(0.1f));
  cam_local[0] = 0, cam_local[1] = 0, cam_local[2] = 2;
  cam_local_rot = (Quat){0, 0, 0, 1};
}

static void cam_point(float out[3], Quat *rot) {
  float m[12];
  sled_model_matrix(m);
  m_point(m, skins[skin].cam, out);
  *rot = q_mul(sled.yaw, sled.pitch);
}

static void cam_play_start(void) {
  locked = false;
  for (int k = 0; k < 3; k++) start_pos[k] = pivot[k];
  start_rot = pivot_rot;
  start_time = game_time;
  cam_local_rot = (Quat){0, 0, 0, 1};
}

static void cam_play(void) {
  const float amp[3] = CAM_ROT_AMPLIFY, off[3] = CAM_OFFSET;
  Quat target = q_euler(-12 + sled.vrot * amp[0], -sled.hrot * amp[1], -sled.hrot * amp[2]);
  float tp[3];
  Quat tr;
  cam_point(tp, &tr);
  for (int k = 0; k < 3; k++) cam_local[k] = off[k];
  Quat want = q_mul(tr, q_euler(15, 0, 0));
  if (locked) {
    for (int k = 0; k < 3; k++) pivot[k] = tp[k];
    pivot_rot = want;
    cam_local_rot = q_lerp(cam_local_rot, target, blend(0.1f));
  } else {
    float p = (game_time - start_time) * 2;
    cam_local_rot = q_lerp(cam_local_rot, target, p);
    pivot_rot = q_lerp(start_rot, want, p);
    float c = p > 1 ? 1 : p;
    for (int k = 0; k < 3; k++) pivot[k] = start_pos[k] + (tp[k] - start_pos[k]) * c;
    if (p > 1) locked = true;
  }
}

static void choose_angle(void) {
  end_ry = (float)rnd_int(-60, 60), end_rx = (float)rnd_int(50, 75);
  end_dir = end_ry < 0 ? 1 : -1;
}

/* EndRoutine: the camera turns around the broken sled's PhysicsBody (its pieces' empty PlayerCameraPivot, which
 * stays where it was left: it has no body). The camera's place is its own way ahead at the crash, backwards twice,
 * and its turn the PhysicsBody's, both taken in the turning pivot: it looks at the pivot from where it came. */
static void cam_end_start(void) {
  const Chunk *c = &chunks[skins[skin].pieces];
  const Obj *o = &c->obj[0];
  for (int i = 0; i < c->n; i++)
    if (!prefabs[c->obj[i].prefab].nparts && !prefabs[c->obj[i].prefab].ncolls) o = &c->obj[i];
  float m[12];
  sled_model_matrix(m);
  for (int k = 0; k < 3; k++) end_pivot[k] = m[k * 4 + 3] + o->pos[k], cam_local[k] = cam_rot[6 + k] * -2;
  cam_local_rot = q_mul(q_mul(sled.yaw, sled.pitch), (Quat){o->rot[0], o->rot[1], o->rot[2], o->rot[3]});
  choose_angle();
}

/* EndControl */
static void cam_end(float dt) {
  if (end_ry > -70 && end_ry < 70) end_ry += 30 * end_dir * dt;
  else choose_angle();
  pivot_rot = q_euler(end_rx, end_ry, 0);
  for (int k = 0; k < 3; k++) pivot[k] = end_pivot[k];
}

/* the sleds: turning slowly around the sled shown (as the shop's stand turns its sleds) */
static float show_angle = 200;
static void cam_show(void) {
  show_angle += 25 * DT_REF;
  pivot[0] = sled.x, pivot[1] = sled.y + 0.25f, pivot[2] = sled.z;
  pivot_rot = q_euler(22, show_angle, 0);
  cam_local[0] = 0, cam_local[1] = 0, cam_local[2] = -2.1f;
  cam_local_rot = (Quat){0, 0, 0, 1};
}

static void cam_apply(void) {
  float off[3] = {cam_local[0] + cam_offset[0], cam_local[1] + cam_offset[1], cam_local[2]}, w[3];
  q_rot(pivot_rot, off, w);
  for (int k = 0; k < 3; k++) cam_pos[k] = pivot[k] + w[k];
  Quat q = q_mul(pivot_rot, cam_local_rot);
  const float ax[3][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  for (int r = 0; r < 3; r++) q_rot(q, ax[r], cam_rot + r * 3);
}

/* ---------------------------------------------------------------- the moments (GameControl) */
static void go_intro(void) {
  mode = M_INTRO;
  world_camp(false);
  world_start();
  cam_intro();
}

static void go_main(void) {
  mode = M_MAIN, screen = S_NONE, sel = 0;
  world_clear();
  for (int i = 0; i < MAXP_PIECES; i++) pieces[i].alive = false;
  world_camp(true);
  sled_to_camp();
  sled.prev_h = sled.py;
  cam_main();
  slow(0, 1);
  fade = 1;
}

static void go_play(void) {
  mode = M_PLAY, screen = S_NONE;
  plays++, score = 0, gifts_game = 0, best_now = false, score_pop = 0;
  sled_spawn();
  world_start();
  cam_play_start();
  slow(0, 1);
  save();
}

static void end_run(void) {
  if (!sled.alive) return;
  sled.alive = false;
  mode = M_END;
  if (best < score) best = score, best_now = true;
  save();
  /* PlayerControl.Die: the sled breaks into its pieces */
  float m[12];
  sled_model_matrix(m);
  float at[3] = {m[3], m[7], m[11]}, push[3] = {0, 0, 0};
  pieces_spawn(skins[skin].pieces, at, q_mul(sled.yaw, sled.pitch), push);
  cam_end_start();
  shake(3);
  slow(0.3f, 0.05f);
  end_wait = 0;
  screen = S_NONE;
}

void game_init(void) {
  load();
  rnd_seed(plat_random());
  sled_to_camp();
  go_intro();
}

/* one step of 1/60 s; keys held and keys pressed this step */
void game_step(uint32_t keys, uint32_t hit) {
  if (hit & SK_HOME) {
    game_running = false;
    save();
    return;
  }
  bool ok = hit & SK_OK, back = hit & SK_BACK, up = hit & SK_UP, down = hit & SK_DOWN;
  /* menus */
  if (screen == S_PAUSE || screen == S_ASK) {
    int n = screen == S_PAUSE ? 3 : 2;
    if (up) sel = (sel + n - 1) % n;
    if (down) sel = (sel + 1) % n;
    if (screen == S_PAUSE && (ok || back)) {
      if (back || sel == 0) screen = S_NONE, time_scale = 0.02f, slow(1, 1);   /* (GUIControl: time comes back in 1 s) */
      else if (sel == 1) go_main(), go_play();
      else screen = S_ASK, sel = 0;
    } else if (screen == S_ASK && (ok || back)) {
      if (ok && sel == 1) go_main();
      else screen = S_PAUSE, sel = 2;
    }
    return;   /* (the game waits) */
  }
  if (screen == S_GUIDE) {
    if (ok) guide_read = true, go_play();
    else if (back) screen = S_NONE;
    return;
  }
  if (screen == S_SLEDS) {
    if (hit & SK_LEFT) shop_skin = (shop_skin + NSKINS - 1) % NSKINS;
    if (hit & SK_RIGHT) shop_skin = (shop_skin + 1) % NSKINS;
    if (ok) {
      if (owned >> shop_skin & 1) skin = shop_skin, screen = S_NONE, save();
      else if (gifts >= skins[shop_skin].price) gifts -= skins[shop_skin].price, owned |= (uint16_t)(1 << shop_skin), skin = shop_skin, save();
    }
    if (back) screen = S_NONE;
  } else if (mode == M_INTRO) {
    if (ok || back) go_main();
  } else if (mode == M_MAIN) {
    if (up) sel = (sel + 2) % 3;
    if (down) sel = (sel + 1) % 3;
    if (ok) {
      if (sel == 0 && !guide_read) screen = S_GUIDE;
      else if (sel == 0) go_play();
      else if (sel == 1) screen = S_SLEDS, shop_skin = skin;
      else game_running = false, save();
    }
    if (back) go_intro();
  } else if (mode == M_PLAY) {
    if (back) {
      screen = S_PAUSE, sel = 0;
      return;
    }
  } else if (mode == M_END && end_wait > 1.2f) {
    if (up || down) sel ^= 1;
    if (ok) {
      if (sel == 0) go_main(), go_play();
      else go_main();
    }
    if (back) go_main();
  }

  slow_step();
  float dt = DT_REF * time_scale;
  game_time += dt;
  shake_step(DT_REF);
  fade = fade > DT_REF ? fade - DT_REF : 0;
  if (mode == M_END) {
    if (end_wait < 1.2f && end_wait + DT_REF >= 1.2f) sel = 0;
    end_wait += DT_REF;
  }
  if (score_pop > 0) score_pop -= DT_REF;

  if (mode == M_PLAY) sled_play(keys, dt);
  if (mode == M_PLAY || mode == M_MAIN) sled_model();
  if (mode == M_PLAY && camp_on && sled.z > 10) world_camp(false);
  /* GGen.Update: a structure each 10 m, a point each one past the warm-up */
  if (mode == M_PLAY) {
    while (world_update(sled.z))
      if (sled.z > WARMUP_LEN * CHUNK_Z) score++, score_pop = 0.3f;
  } else if (mode == M_INTRO) {
    pivot[2] += CAM_INTRO_SPEED * dt;
    while (world_update(pivot[2])) {}
  }
  if (mode == M_PLAY || mode == M_END || mode == M_INTRO) world_balls(dt);
  pieces_step(dt);
  if (mode == M_PLAY) cam_play();
  else if (mode == M_END) cam_end(dt);
  else if (mode == M_MAIN && screen == S_SLEDS) cam_show();
  else if (mode == M_MAIN) cam_main_step();
  cam_apply();
}
