/* Flappy Dunk's rules and physics, as the original's code runs them (Dunky, HoopSpawner, Hoop, MovingHoop,
 * Parameters, Challenge, FinishLine): 100 physics steps a second, Box2D-like contacts with the rims (no bounce,
 * no slipping), the floor and the ceiling deadly. Numbers: gamesrc/flappydunk/spec/flappydunk.md. */
#include <math.h>
#include <string.h>
#include "fd.h"

fd_game_t G;
bool fd_quiet;

uint32_t game_rand(void) {
  uint32_t x = G.rng ? G.rng : 0x9E3779B9u;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return G.rng = x;
}
float game_frand(void) { return (float)(game_rand() >> 8) * (1.0f / 16777216.0f); }

/* ---------------------------------------------------------------- the endless mode's parameters */
float curve_eval(int p, float t) {
  const fd_param_t *q = &fd_params[p];
  const fd_key_t *k = &fd_param_keys[q->key];
  int n = q->nkeys;
  if (t <= k[0].t) return k[0].v;
  if (t >= k[n - 1].t) return k[n - 1].v;
  for (int i = 0; i + 1 < n; i++)
    if (t <= k[i + 1].t) { /* Unity's Hermite curve between two keys */
      float d = k[i + 1].t - k[i].t, s = (t - k[i].t) / d, s2 = s * s, s3 = s2 * s;
      return (2 * s3 - 3 * s2 + 1) * k[i].v + (s3 - 2 * s2 + s) * d * k[i].out + (-2 * s3 + 3 * s2) * k[i + 1].v +
             (s3 - s2) * d * k[i + 1].in;
    }
  return k[n - 1].v;
}
float param_sample(int p, int n) {
  const fd_param_t *q = &fd_params[p];
  if (q->type == 2) return q->min; /* Unique */
  float t = q->type == 1 ? curve_eval(p, (float)(n - q->start) / (float)(q->end - q->start)) : game_frand();
  return q->min + t * (q->max - q->min);
}

/* Mathf.Round: halves to even */
static float round_even(float v) {
  float f = floorf(v), d = v - f;
  if (d > 0.5f || (d == 0.5f && fmodf(f, 2.0f) != 0)) f += 1;
  return f;
}
/* Hoop.GetHoopAngle: forward tilts in 15s up to 60, backward in 10s up to 20, more as the hoops go by */
static float hoop_angle(int n) {
  float a = param_sample(P_ANGLE, n), r = game_frand();
  if (a > 1) a = 1;
  if (r >= 0.5f) return round_even(a * (r - 0.5f) * 44.9f / -10.0f) * 10.0f;
  return round_even(a * (r + r) * 67.4f / 15.0f) * 15.0f;
}

#ifdef HOST
float game_test_angle(int n) { return hoop_angle(n); }
#endif

/* ---------------------------------------------------------------- hoops */
static void hoop_place(hoop_t *h) {
  float ox = 0, oy = 0;
  if (h->type == H_LATERAL) ox = h->pos * h->c, oy = h->pos * h->s;
  else if (h->type == H_VERTICAL) ox = -h->pos * h->s, oy = h->pos * h->c;
  else if (h->type == H_WTF) { /* WTFHoop.anim: the Hoop's position and turn, 15 keys a second, looping */
    float f = fmodf(h->t, (float)(FD_WTF_N - 1) / 15.0f) * 15.0f;
    int i = (int)f;
    if (i >= FD_WTF_N - 1) i = FD_WTF_N - 2;
    float u = f - (float)i;
    ox = ((float)fd_wtf_x[i] + ((float)fd_wtf_x[i + 1] - (float)fd_wtf_x[i]) * u) / 256.0f;
    oy = ((float)fd_wtf_y[i] + ((float)fd_wtf_y[i + 1] - (float)fd_wtf_y[i]) * u) / 256.0f;
    h->angle = ((float)fd_wtf_angle[i] + ((float)fd_wtf_angle[i + 1] - (float)fd_wtf_angle[i]) * u) / 64.0f;
    h->c = cosf(h->angle * 0.017453293f), h->s = sinf(h->angle * 0.017453293f);
  }
  h->hx = h->x + ox, h->hy = h->y + oy;
}
static void hoop_setup(hoop_t *h, int type, float x, float y, float angle, float w, float speed, float bound) {
  memset(h, 0, sizeof *h);
  h->type = (uint8_t)type, h->x = x, h->y = y, h->angle = angle, h->w = w, h->bound = bound;
  h->c = cosf(angle * 0.017453293f), h->s = sinf(angle * 0.017453293f);
  h->fail_x = 4;
  if (type == H_VERTICAL || type == H_LATERAL) { /* MovingHoop.SetRandom / SetFromAbstract */
    h->speed = game_frand() > 0.5f ? speed : -speed;
    h->pos = bound * (2 * game_frand() - 1);
    float f = (bound + 0.5f) * fabsf(h->s);
    h->fail_x = (f < 2 ? 2 : f) + 3;
  }
  hoop_place(h);
}

/* the next endless hoop (HoopSpawner.SampleHoop, Hoop.SetRandom, NextPosition) */
static void spawn_endless(void) {
  if (G.nh >= FD_MAXH) return;
  int n = G.spawned;
  int type = H_STATIC;
  if (game_frand() < param_sample(P_MOVING, n)) type = game_frand() < param_sample(P_LATERAL, n) ? H_LATERAL : H_VERTICAL;
  float angle = hoop_angle(n), w = param_sample(P_WIDTH, n), speed = param_sample(P_MOVE_SPEED, n);
  float maxh = type == H_VERTICAL ? 4.5f : 6.5f;
  float y = param_sample(P_HEIGHT, n) * maxh * game_frand();
  if (game_frand() < 0.5f) y = -y;
  G.spawn_x += FD_SPAWN_GAP;
  hoop_setup(&G.h[G.nh++], type, G.spawn_x, y, angle, w, speed, 2);
  G.spawned++;
}

#ifdef HOST
void game_test_spawn(int n) {
  G.nh = 0, G.spawned = n, G.spawn_x = 0;
  spawn_endless();
}
#endif

static void spawn_challenge(int k) {
  const fd_challenge_t *c = &fd_challenges[k];
  float x = FD_SPAWN_X0;
  for (int i = 0; i < c->count && G.nh < FD_MAXH; i++) {
    const fd_choop_t *a = &fd_choops[c->first + i];
    x += a->distance;
    hoop_setup(&G.h[G.nh++], a->type, x, a->height, a->angle, a->width, a->speed, a->bound);
  }
  G.finish_x = x + c->finish;
}

void game_new(int challenge, bool mirror, uint32_t seed) {
  memset(&G, 0, sizeof G);
  G.rng = seed ? seed : 1;
  G.challenge = (int8_t)challenge, G.mirror_mode = mirror;
  G.x = FD_BALL_X0, G.cam = G.x, G.min_y = 0;
  G.mult = 1, G.last_touched = -1;
  G.top = FD_TOP;
  G.spawn_x = FD_SPAWN_X0;
  if (challenge >= 0) {
    const fd_challenge_t *c = &fd_challenges[challenge];
    G.gravity = c->gravity, G.flap = c->flap, G.scroll = c->scroll;
    if (c->narrow) G.top = FD_TOP_NARROW;
    G.pixel = c->pixel;
    G.hflip = (uint8_t)((c->mirror & 1) ^ (mirror ? 1 : 0)), G.vflip = (uint8_t)(c->mirror >> 1 & 1);
    spawn_challenge(challenge);
  } else {
    G.gravity = param_sample(P_GRAVITY, 0), G.flap = param_sample(P_FLAP, 0);
    while (G.nh < 4) spawn_endless();
  }
  G.state = ST_READY;
}

/* Dunky.Flap: the velocity set at once, forward and up; the wings beat */
void game_flap(void) {
  if (G.state == ST_DEAD || G.state == ST_WON) return;
  float up = G.challenge >= 0 ? G.flap : param_sample(P_FLAP, G.spawned);
  if (G.flap_centered && G.y < 0) up = -up;
  G.vx = G.challenge >= 0 ? G.scroll : param_sample(P_SCROLL, G.spawned);
  G.vy = up, G.wing = 150;
  if (G.state == ST_READY) G.state = ST_PLAY;
}

/* ---------------------------------------------------------------- effects */
static void part_add(int kind, float x, float y, float vx, float vy, float life, int colour) {
  part_t *best = &G.p[0];
  for (int i = 0; i < FD_NPART; i++) {
    if (G.p[i].life <= 0) { best = &G.p[i]; break; }
    if (G.p[i].life < best->life) best = &G.p[i];
  }
  *best = (part_t){x, y, vx, vy, (game_frand() - 0.5f) * 8, life, life, (uint8_t)kind, (uint8_t)colour};
}

/* ---------------------------------------------------------------- the run */
static void end_run(int why) {
  if (G.state == ST_DEAD || G.state == ST_WON) return;
  G.state = why == END_WIN ? ST_WON : ST_DEAD;
  G.end = (uint8_t)why;
  G.cam_v = G.vx > 0 ? G.vx : 0;
  G.fever = 0;
  on_end();
}

/* Challenge.CannotWinCondition */
static bool cannot_win(int swishes, int notswish) {
  if (G.challenge < 0) return false;
  const fd_challenge_t *c = &fd_challenges[G.challenge];
  if (c->swishes < 0) return false;
  if (c->condition == 0 && swishes > c->swishes) return true;
  return (int)c->count - c->swishes < notswish;
}

/* Hoop.Pass and Stats.OnPassHoop: a basket is 1, a swish one more than the last hoop's points */
static void pass(int i) {
  hoop_t *h = &G.h[i];
  bool swish = G.last_touched != i;
  int n = swish ? (int)G.mult + 1 : 1;
  h->passed = 1, h->t = 0, h->burn = n > 3;
  G.score += (uint32_t)n, G.mult = (uint32_t)n, G.hoops++;
  if (swish) {
    G.swishes++, G.streak++, G.notswish_streak = 0;
    if (G.streak > G.best_streak) G.best_streak = G.streak;
  } else {
    G.streak = 0, G.notswish_streak++;
    if (G.notswish_streak > G.best_notswish) G.best_notswish = G.notswish_streak;
  }
  if (n == 2) G.fever = 1; /* DunkyAnimation.SetOnFire: smoke, then fire */
  else if (n >= 3) G.fever = 2;
  if (n >= 2) { /* the hoop's stars, and a blast */
    for (int k = 0; k < 8; k++) {
      float a = (float)k * 0.785398f + game_frand() * 0.5f, v = 3 + game_frand() * 3;
      part_add(PT_STAR, h->hx, h->hy, cosf(a) * v, sinf(a) * v + 2, 0.6f + game_frand() * 0.3f, 0);
    }
    part_add(n == 2 ? PT_RING : PT_BIGRING, h->hx, h->hy, 0, 0, 0.35f, 0);
  }
  if (n > 3) /* the hoop burns away */
    for (int k = 0; k < 6; k++)
      part_add(PT_FIRE, h->hx + (game_frand() - 0.5f) * 3 * h->w, h->hy + game_frand() * 0.4f, 0, 1 + game_frand(), 0.45f, 1);
  G.pop_t = 1.2f, G.pop_n = n, G.pop_x = h->hx, G.pop_y = h->hy;
  G.last_touched = -1, G.bounces = 0;
  on_pass(n);
  if (G.challenge >= 0 && cannot_win((int)G.swishes, (int)G.notswish)) end_run(END_RULE);
  G.since_hoop = 0, G.rot_since = 0, G.min_y = G.y;
  if (G.next == i) G.next++;
}

/* a rim: a fixed circle; the ball is pushed out, keeps none of its speed into it and rolls without slipping
   (the physics material's friction 100000): its contact point stops, a third of the sliding goes into spin */
static void rim(int i, float rx, float ry, float rr) {
  float dx = G.x - rx, dy = G.y - ry, d2 = dx * dx + dy * dy, r = FD_R + rr;
  if (d2 >= r * r) return;
  float d = sqrtf(d2), nx = 0, ny = 1;
  if (d > 1e-5f) nx = dx / d, ny = dy / d;
  G.x += nx * (r - d), G.y += ny * (r - d);
  float vn = G.vx * nx + G.vy * ny;
  if (vn < 0) {
    G.vx -= vn * nx, G.vy -= vn * ny;
    /* the contact point's speed along the rim: the ball's, plus its turning (contact at -R n) */
    float tx = -ny, ty = nx;
    float vt = G.vx * tx + G.vy * ty + G.spin * FD_R; /* (w x r).t with r = -R n is +w R */
    float j = -vt / 3; /* a disc: 1/m + R^2/I = 3/m */
    G.vx += j * tx, G.vy += j * ty;
    G.spin += 2 * j / FD_R;
  }
  if (G.last_touched != i) { /* the first knock on this hoop: no swish */
    G.last_touched = i, G.bounces = 1, G.fever = 0;
    if (G.challenge >= 0 && cannot_win((int)G.swishes, (int)G.notswish + 1)) {
      G.notswish++;
      end_run(END_RULE);
      return;
    }
    if (G.challenge >= 0) G.notswish++;
    on_touch();
  } else if (vn < -0.5f) {
    G.bounces++;
    if (G.bounces > G.best_bounces) G.best_bounces = G.bounces;
    on_touch();
  }
}

static bool circle_box(float lx, float ly, float hw, float hh) {
  float ex = fabsf(lx) - hw, ey = fabsf(ly) - hh;
  if (ex < 0) ex = 0;
  if (ey < 0) ey = 0;
  return ex * ex + ey * ey < FD_R * FD_R;
}

static void hoops_step(void) {
  for (int i = 0; i < G.nh; i++) {
    hoop_t *h = &G.h[i];
    if (h->type == H_VERTICAL || h->type == H_LATERAL) { /* MovingHoop.Update: back and forth on its track */
      h->pos += h->speed * FD_DT;
      if ((h->speed > 0 && h->pos > h->bound) || (h->speed <= 0 && h->pos < -h->bound)) h->speed = -h->speed;
    }
    h->t += FD_DT;
    hoop_place(h);
  }
}

static void collide(void) {
  for (int i = 0; i < G.nh && G.state <= ST_PLAY; i++) {
    hoop_t *h = &G.h[i];
    if (h->passed) continue;
    /* the fail trigger past the hoop (on its root: a moving hoop may be far from it) */
    if (fabsf(G.x - (h->x + h->fail_x)) < 1.2f && circle_box(G.x - (h->x + h->fail_x), G.y - h->y, 0.5f, 20)) {
      end_run(END_MISS);
      return;
    }
    if (h->hx > G.x + 6 || h->hx < G.x - 8) continue;
    float ax = 1.6f * h->w * h->c, ay = 1.6f * h->w * h->s, rr = 0.34f * h->w;
    rim(i, h->hx - ax, h->hy - ay, rr);
    if (G.state > ST_PLAY) return;
    rim(i, h->hx + ax, h->hy + ay, rr);
    if (G.state > ST_PLAY) return;
    /* the score box ("Point"): in from above, out below scores; in from below ends the run */
    float dx = G.x - h->hx, dy = G.y - h->hy;
    float lx = dx * h->c + dy * h->s, ly = dy * h->c - dx * h->s;
    bool in = circle_box(lx, ly, h->w, 0.1f);
    if (in && !h->inside) {
      h->inside = 1;
      if (ly < 0) { end_run(END_BELOW); return; }
    } else if (!in && h->inside) {
      h->inside = 0;
      if (ly < 0) { pass(i); continue; }
    }
  }
  if (G.challenge >= 0 && G.state == ST_PLAY && circle_box(G.x - G.finish_x, G.y, 0.6f, 10)) end_run(END_WIN);
}

void game_step(void) {
  float dt = FD_DT;
  hoops_step();
  /* the wings ease back (DunkyAnimation.Update: 5 x dt a frame), the effects */
  G.wing += (0 - G.wing) * (5 * dt > 1 ? 1 : 5 * dt);
  for (int i = 0; i < FD_NPART; i++) {
    part_t *p = &G.p[i];
    if (p->life <= 0) continue;
    p->life -= dt;
    p->x += p->vx * dt, p->y += p->vy * dt;
    if (p->kind == PT_STAR) p->vy -= 12 * dt;
    if (p->kind == PT_SMOKE || p->kind == PT_FIRE) p->vy += 1.5f * dt;
  }
  if (G.pop_t > 0) G.pop_t -= dt;
  if (G.state == ST_READY) { /* waiting: the ball flaps by itself below -1 (Dunky.Update) */
    G.vy += G.gravity * dt;
    G.y += G.vy * dt;
    if (G.y < -1 && G.vy < 0) G.vy = G.challenge >= 0 ? G.flap : param_sample(P_FLAP, 0), G.wing = 150;
    G.cam = G.x;
    return;
  }
  float g = G.gravity;
  if (G.gravity_centered && G.y < 0) g = -g;
  G.vy += g * dt;
  G.spin *= 1.0f / (1.0f + dt); /* angular drag 1 */
  G.x += G.vx * dt, G.y += G.vy * dt;
  float da = G.spin * dt;
  G.ang += da;
  if (G.state == ST_PLAY) {
    G.time += dt;
    G.since_hoop += dt, G.rot_since += fabsf(da) * 57.29578f;
    if (G.y < G.min_y) G.min_y = G.y;
    collide();
    if (G.state == ST_PLAY && (G.y - FD_R <= -G.top || G.y + FD_R >= G.top)) end_run(END_GROUND);
    G.cam = G.x;
    /* the fire's flames and the smoke trail behind the ball */
    if (G.fever && ((int)(G.time * 100 + 0.5f) % (G.fever == 2 ? 2 : 4)) == 0)
      part_add(G.fever == 2 ? PT_FIRE : PT_SMOKE, G.x + (game_frand() - 0.5f) * 0.3f, G.y + (game_frand() - 0.5f) * 0.3f,
               (game_frand() - 0.5f) * 0.6f, 0.4f + game_frand() * 0.6f, G.fever == 2 ? 0.3f : 0.45f, 0);
    if (G.challenge < 0) { /* the next hoops, and the passed ones dropped */
      while (G.nh < FD_MAXH && G.spawn_x < G.cam + 30) spawn_endless();
      int drop = 0;
      while (drop < G.nh && G.h[drop].passed && G.h[drop].hx < G.cam - 16) drop++;
      if (drop) {
        memmove(G.h, G.h + drop, (size_t)(G.nh - drop) * sizeof(hoop_t));
        G.nh -= drop, G.next -= drop;
        if (G.last_touched >= 0) G.last_touched = G.last_touched >= drop ? G.last_touched - drop : -1;
        if (G.next < 0) G.next = 0;
      }
    }
  } else { /* after the crash: the ball drops off, the camera slows (GameCamera: 2 a second per second) */
    if (G.y < -G.top - 4) G.y = -G.top - 4, G.vy = 0, G.vx = 0;
    G.cam_v -= 2 * dt;
    if (G.cam_v < 0) G.cam_v = 0;
    G.cam += G.cam_v * dt;
  }
}
