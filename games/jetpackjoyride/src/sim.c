/* The run: Barry, his vehicles, and what the level pacing sends at him, 60 times a second, with the
 * original's numbers (data.h). Where the files don't say how a number is used, the choice is the spec's
 * (marked [P] there). */
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "jj.h"
#include "data.h"

#define FRAMES_SAFE ((JJ_IMMUNITY_MS * 60 + 999) / 1000)
#define WARN_FRAMES 75
#define LOCK_FRAMES 30
#define CHARGE 90
#define SLIDE 20
#define GAP 72

/* ---------------------------------------------------------------- small things */
uint32_t jj_rand(uint32_t *s) {
  uint32_t x = *s;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return *s = x ? x : 0x2545F491u;
}
static int range(jj_run *r, int a, int b) { return b <= a ? a : a + (int)(jj_rand(&r->rng) % (uint32_t)(b - a + 1)); }
/* the scientists' and the bullets' dice: their own, so what comes next never hangs on who got shot */
static int crowd(jj_run *r, int a, int b) { return b <= a ? a : a + (int)(jj_rand(&r->crowd) % (uint32_t)(b - a + 1)); }
static float crowdf(jj_run *r, float a, float b) { return a + (b - a) * (float)(jj_rand(&r->crowd) & 0xffff) / 65535.0f; }

static const int16_t quarter[65] = {
  0, 25, 50, 75, 100, 125, 150, 175, 200, 224, 249, 273, 297, 321, 345, 369, 392, 415, 438, 460, 483, 505,
  526, 548, 569, 590, 610, 630, 650, 669, 688, 706, 724, 742, 759, 775, 792, 807, 822, 837, 851, 865, 878,
  891, 903, 915, 926, 936, 946, 955, 964, 972, 980, 987, 993, 999, 1004, 1009, 1013, 1016, 1019, 1021, 1023,
  1024, 1024};
int jj_sin(uint16_t ang) {
  unsigned i = ang >> 8;   /* 256 steps a turn */
  unsigned q = i >> 6, k = i & 63;
  int v = q & 1 ? quarter[64 - k] : quarter[k];
  return q & 2 ? -v : v;
}
int jj_cos(uint16_t ang) { return jj_sin((uint16_t)(ang + 16384)); }

uint32_t jj_metres(const jj_run *r) { return r->dist / (16 * JJ_DIST_DIV); }

static int weight(unsigned sched, uint32_t metres) {
  int w = 0;
  for (unsigned i = sched; jj_sched[2 * i] != 0xffff; i++)
    if (jj_sched[2 * i] <= metres) w = jj_sched[2 * i + 1];
  return w;
}

float jj_speed(const jj_run *r) {
  if (r->ride != JJ_FOOT) return r->ride_speed;
  float m = (float)r->dist / (16.0f * JJ_DIST_DIV);
  float k = m >= JJ_SPEED_DIST ? 1.0f : m / JJ_SPEED_DIST;
  return JJ_START_SPEED + (JJ_END_SPEED - JJ_START_SPEED) * k;
}

int jj_box_h(int ride) {
  static const uint8_t h[JJ_RIDES] = {30, 56, 40, 40, 40, 48};
  return h[ride];
}
int jj_box_w(int ride) {
  static const uint8_t w[JJ_RIDES] = {16, 44, 50, 50, 24, 34};
  return w[ride];
}

int jj_coin_at(const jj_coinset *c, int col, int row) {
  int w = jj_pat_w[c->pat];
  if (col < 0 || row < 0 || col >= w || row >= jj_pat_h[c->pat]) return 0;
  return (c->bits[row * ((w + 7) / 8) + col / 8] >> (col & 7)) & 1;
}

/* ---------------------------------------------------------------- lasers
 * A laser charges 90 frames, fires for its on interval (60 for the single shots), and fires again its off
 * interval after a shot ends, if that shot ends before the pattern does. */
void jj_beam_state(int pattern, int beam, int t, int *y, int *phase, int *k) {
  const jj_lpat_t *p = &jj_lpats[pattern];
  const jj_beam_t *b = &jj_beams[p->first + beam];
  int on = b->on > 1 ? b->on : 60;
  /* the height: from y0 to y1 over `move` frames */
  int yy = b->y0;
  if (b->move) {
    int tt = t < 0 ? 0 : t > b->move ? b->move : t;
    yy = b->y0 + (b->y1 - b->y0) * tt / b->move;
  }
  *y = JJ_MID - yy;
  *phase = 0, *k = 0;
  int fire = b->wait + CHARGE;
  while (fire + on <= p->time) {
    if (t < fire - CHARGE) return;
    if (t < fire) {
      *phase = 1, *k = t - (fire - CHARGE);
      return;
    }
    if (t < fire + on) {
      *phase = 2, *k = t - fire;
      return;
    }
    if (b->off == 0 || b->off >= 10000) return;
    int next = fire + on + b->off;
    fire = next - fire - on < CHARGE ? fire + on + CHARGE : next;
  }
}

/* ---------------------------------------------------------------- spawning */
static float place(jj_run *r, int lo, int hi, const jj_file_t *f) {
  if (hi < lo) hi = lo;
  int mid = (lo + hi) / 2, p = range(r, 0, 99);
  if (p < f->top) return (float)lo;
  p -= f->top;
  if (p < f->bottom) return (float)hi;
  p -= f->bottom;
  if (p < f->tophalf) return (float)range(r, lo, mid);
  p -= f->tophalf;
  if (p < f->bottomhalf) return (float)range(r, mid, hi);
  return (float)range(r, lo, hi);
}

static int pick(jj_run *r, const uint16_t *sched_of, int first, int count, uint32_t m) {
  int total = 0;
  for (int i = 0; i < count; i++) total += weight(sched_of[first + i], m);
  if (total <= 0) return -1;
  int x = range(r, 0, total - 1);
  for (int i = 0; i < count; i++) {
    x -= weight(sched_of[first + i], m);
    if (x < 0) return first + i;
  }
  return first + count - 1;
}

/* the heights Barry's middle can pass a zapper at (up to two ranges, lo to hi): the room but the zapper, its orbs
   (9 points) and half of Barry (15); a rotating one takes its whole turn */
static int zap_free(const jj_zapper *z, float *lo, float *hi) {
  int ext = z->spin ? z->len : (abs(jj_sin(z->ang)) * z->len) >> 10, n = 0;
  float top = z->y - ext / 2.0f - 24, bottom = z->y + ext / 2.0f + 24;
  if (top > JJ_CEIL + 15) lo[n] = JJ_CEIL + 15, hi[n++] = top;
  if (bottom < JJ_FLOOR - 15) lo[n] = bottom, hi[n++] = JJ_FLOOR - 15;
  return n;
}
/* how far Barry gets in t frames from still, on foot: down letting go, up holding (his own fall and boost; worked
   out once, up to 10 seconds) */
#define REACH_T 600
static float reach(int t, bool up) {
  static float table[2][REACH_T + 1];
  static bool made;
  if (!made) {
    for (int u = 0; u < 2; u++) {
      float y = 0, v = 0;
      for (int i = 1; i <= REACH_T; i++) {
        v += JJ_GRAVITY;
        if (u) v -= JJ_BOOST;
        v *= JJ_FRICTION;
        y += v;
        table[u][i] = u ? -y : y;
      }
    }
    made = true;
  }
  return table[up][t < REACH_T ? t : REACH_T];
}
/* where Barry's middle can be as he passes zapper z, t frames after he could be anywhere in [lo, hi]: false if
   nowhere (the ranges kept in lo, hi) */
static bool zap_pass(const jj_zapper *z, int t, float *lo, float *hi) {
  float flo[2], fhi[2], a = 1e9f, b = -1e9f;
  int n = zap_free(z, flo, fhi);
  t -= 14;   /* (a moment to react) */
  float up = t > 0 ? reach(t, true) : 0, down = t > 0 ? reach(t, false) : 0;
  for (int i = 0; i < n; i++) {
    float l = flo[i] > *lo - up ? flo[i] : *lo - up, h = fhi[i] < *hi + down ? fhi[i] : *hi + down;
    if (l <= h) a = a < l ? a : l, b = b > h ? b : h;
  }
  if (a > b) return false;
  *lo = a, *hi = b;
  return true;
}
/* zapper s, coming in: can Barry get past it, from where he is, past every zapper before it (in order), at the
   speed he goes (dxf points a frame)? (one range kept for each: a little more than he can reach) */
static bool zap_ok(const jj_run *r, const jj_zapper *s, float dxf) {
  float lo = r->y - 15, hi = r->y - 15, x = JJ_BX;
  const jj_zapper *done[JJ_ZAPPERS];
  int nd = 0;
  for (;;) {
    const jj_zapper *next = NULL;
    for (int k = 0; k < JJ_ZAPPERS; k++) {
      const jj_zapper *z = &r->zap[k];
      int e = z->spin ? z->len : (abs(jj_cos(z->ang)) * z->len) >> 10, seen = 0;
      for (int d = 0; d < nd; d++) seen |= done[d] == z;
      if (!z->on || z == s || seen || z->x + e / 2.0f < JJ_BX || z->x > s->x) continue;
      if (!next || z->x < next->x) next = z;
    }
    const jj_zapper *z = next ? next : s;
    int e = z->spin ? z->len : (abs(jj_cos(z->ang)) * z->len) >> 10;
    float at = z->x - e / 2.0f;
    if (!zap_pass(z, at > x ? (int)((at - x) / dxf) : 0, &lo, &hi)) {
      if (!next) return false;
      lo = JJ_CEIL + 15, hi = JJ_FLOOR - 15;   /* (no way past one already there: nothing to keep to) */
    }
    if (!next) return true;
    x = z->x + e / 2.0f, done[nd++] = z;
  }
}

static float spawn_zapper(jj_run *r, int file, uint32_t m) {
  const jj_file_t *f = &jj_zap_files[file];
  int total = 0, i;
  for (i = 0; i < f->count; i++) total += weight(jj_zaps[f->first + i].sched, m);
  if (total <= 0) return 0;
  int x = range(r, 0, total - 1);
  for (i = 0; i < f->count - 1; i++) {
    x -= weight(jj_zaps[f->first + i].sched, m);
    if (x < 0) break;
  }
  const jj_zap_t *z = &jj_zaps[f->first + i];
  for (int k = 0; k < JJ_ZAPPERS; k++) {
    jj_zapper *s = &r->zap[k];
    if (s->on) continue;
    memset(s, 0, sizeof *s);
    s->on = 1;
    s->len = (uint16_t)range(r, z->min, z->max);
    s->ang = (uint16_t)((z->angle % 360) * 65536 / 360);
    if (z->rotate) {
      s->spin = (int16_t)(65536 / (z->rotate < 0 ? -z->rotate : z->rotate));
      if (z->rotate < 0) s->spin = (int16_t)-s->spin;
      s->ang = (uint16_t)jj_rand(&r->rng);
    }
    int ext_y = z->rotate ? s->len : (abs(jj_sin(s->ang)) * s->len) >> 10;
    int ext_x = z->rotate ? s->len : (abs(jj_cos(s->ang)) * s->len) >> 10;
    /* where it fits; one that's too long for the room left is pushed into the floor or the ceiling, so a gap of
       GAP points stays open (the files don't say, the spec's choice) */
    int half = ext_y / 2 + 14;
    int lo = JJ_CEIL + half, hi = JJ_FLOOR - half;
    if (hi - lo < 0 || (JJ_FLOOR - JJ_CEIL) - 2 * half < GAP) {
      int top = jj_rand(&r->rng) & 1;
      lo = hi = top ? JJ_FLOOR - GAP - half : JJ_CEIL + GAP + half;
    }
    s->y = place(r, lo, hi, f);
    s->x = JJ_SPAWN_X + ext_x / 2.0f;
    /* (the files don't keep two zappers from closing every way between them: a long one pushed to the floor, then
       one leaving only the bottom free, too soon for Barry to get there; the next comes in later then) */
    float extra = 0, dxf = jj_speed(r) / 60.0f;
    if (r->ride == JJ_FOOT && dxf > 0)
      while (!zap_ok(r, s, dxf) && extra < 600 * dxf) extra += 4 * dxf, s->x += 4 * dxf;
    return (float)ext_x + 28 + extra;
  }
  return 0;
}

static float spawn_coins(jj_run *r, int file, uint32_t m) {
  const jj_file_t *f = &jj_coin_files[file];
  int p = pick(r, jj_coin_pick_sched, f->first, f->count, m);
  if (p < 0) return 0;
  int pat = jj_coin_pick[p];
  for (int k = 0; k < JJ_COINSETS; k++) {
    jj_coinset *c = &r->coin[k];
    if (c->on) continue;
    memset(c, 0, sizeof *c);
    c->on = 1, c->pat = (uint8_t)pat;
    int w = jj_pat_w[pat], h = jj_pat_h[pat], nb = (w + 7) / 8;
    memcpy(c->bits, jj_pat_bits + jj_pat_off[pat], (size_t)(nb * h));
    c->y = (int16_t)place(r, JJ_CEIL + 6, JJ_FLOOR - 10 - (h - 1) * JJ_COIN_GAP, f);
    c->x = JJ_SPAWN_X;
    return (float)(w * JJ_COIN_GAP);
  }
  return 0;
}

static float spawn_missile(jj_run *r, int file) {
  for (int k = 0; k < JJ_MISSILES; k++) {
    jj_missile *s = &r->mis[k];
    if (s->on) continue;
    memset(s, 0, sizeof *s);
    s->on = 1, s->state = JJ_WARN;
    s->chase = jj_mis_files[file].chase;
    s->y = r->y - jj_box_h(r->ride) / 2.0f;
    s->x = JJ_W - 18;
    for (int i = 0; i < JJ_SCIENTISTS; i++)   /* CowerWithMissilesProbability 50 */
      if (r->sci[i].on && r->sci[i].state <= JJ_SCI_RUN && crowd(r, 0, 99) < 50) r->sci[i].state = JJ_SCI_FETAL, r->sci[i].t = 0;
    return 0;
  }
  return 0;
}

static float spawn_token(jj_run *r, uint32_t m) {
  for (int k = 0; k < JJ_TOKENS; k++) {
    jj_token *s = &r->token[k];
    if (s->on) continue;
    s->on = 1;
    s->x = JJ_SPAWN_X;
    s->y = (float)range(r, JJ_CEIL + 70, JJ_FLOOR - 70);
    s->phase = (uint16_t)jj_rand(&r->rng);
    if (m / JJ_TOKEN_KM != r->token_km) r->token_km = m / JJ_TOKEN_KM, r->token_count = 0;
    r->token_count++;
    r->token_last = m + JJ_TOKEN_MAX_GAP;   /* the next one is due by then */
    return 40;
  }
  return 0;
}

/* workerinfo.xml: a scientist walks at 0.5 to 2 points a frame, one in five runs (2 to 6), and he notices Barry
   200 to 500 points away */
static void spawn_scientist(jj_run *r) {
  for (int k = 0; k < JJ_SCIENTISTS; k++) {
    jj_scientist *s = &r->sci[k];
    if (s->on) continue;
    memset(s, 0, sizeof *s);
    s->on = 1, s->state = JJ_SCI_WALK;
    s->x = JJ_SPAWN_X, s->y = JJ_FLOOR;
    s->dir = (int8_t)(jj_rand(&r->crowd) & 1 ? 1 : -1);
    s->kind = (uint8_t)(jj_rand(&r->crowd) & 1);
    s->vx = crowdf(r, 0.5f, 2.0f);
    s->near = (uint16_t)crowd(r, 200, 500);
    if (crowd(r, 0, 99) < 20) s->state = JJ_SCI_RUN, s->vx = crowdf(r, 2.0f, 6.0f);
    return;
  }
}

static void spawn(jj_run *r) {
  uint32_t m = jj_metres(r);
  const jj_set_t *set = &jj_sets[r->set];
  float width = 0;
  int imin, imax;
  if (r->set == JJ_SET_DEFAULT) {
    int i = 0;
    while (i + 1 < JJ_PACE_STEPS && jj_pace_from[i + 1] <= m) i++;
    imin = jj_pace_min[i], imax = jj_pace_max[i];
  } else {
    imin = set->imin, imax = set->imax;
  }
  if (range(r, 0, 99) < 40) spawn_scientist(r);   /* workerinfo.xml spawnProbability 40 */
  /* a spin token that's due */
  bool token_ok = m >= JJ_TOKEN_FIRST_MIN && (m / JJ_TOKEN_KM != r->token_km || r->token_count < JJ_TOKEN_PER_KM);
  if (token_ok && m >= r->token_last) {
    width = spawn_token(r, m);
    r->next = (float)range(r, imin, imax) + width;
    return;
  }
  if ((r->set == JJ_SET_MISSILE || r->set == JJ_SET_COIN) && r->streak) {
    if (r->set == JJ_SET_MISSILE) spawn_missile(r, set->mis);
    else width = spawn_coins(r, set->coin, m);
    r->next = (float)range(r, imin, imax) + width;
    if (!--r->streak) r->set = JJ_SET_DEFAULT;
    return;
  }
  /* weights: zapper, missile, coins, lasers, token, missile swarm, coin streak */
  int w[7] = {0};
  if (set->zap != 255) w[0] = weight(jj_zap_files[set->zap].sched, m);
  if (set->mis != 255) w[1] = weight(jj_mis_files[set->mis].sched, m);
  if (set->coin != 255) w[2] = weight(jj_coin_files[set->coin].sched, m);
  if (r->set == JJ_SET_DEFAULT) {
    w[3] = weight(JJ_LASER_SCHED, m);
    w[5] = jj_sets[JJ_SET_MISSILE].prob;
    w[6] = jj_sets[JJ_SET_COIN].prob;
  }
  if (token_ok) w[4] = JJ_TOKEN_WEIGHT;
  int total = 0;
  for (int i = 0; i < 7; i++) total += w[i];
  int x = total > 0 ? range(r, 0, total - 1) : 0, kind = 0;
  while (kind < 6 && x >= w[kind]) x -= w[kind++];
  switch (kind) {
  case 0: width = spawn_zapper(r, set->zap, m); break;
  case 1: width = spawn_missile(r, set->mis); break;
  case 2: width = spawn_coins(r, set->coin, m); break;
  case 3: {
    int total3 = 0, p;
    for (p = 0; p < JJ_LASER_PATTERNS; p++) total3 += weight(jj_lpats[p].sched, m);
    if (total3 <= 0) break;
    int y = range(r, 0, total3 - 1);
    for (p = 0; p < JJ_LASER_PATTERNS - 1; p++) {
      y -= weight(jj_lpats[p].sched, m);
      if (y < 0) break;
    }
    r->laser = (uint8_t)(p + 1), r->laser_t = 0;
    break;
  }
  case 4: width = spawn_token(r, m); break;
  case 5: r->set = JJ_SET_MISSILE, r->streak = (uint8_t)range(r, jj_sets[JJ_SET_MISSILE].minc, jj_sets[JJ_SET_MISSILE].maxc); break;
  case 6: r->set = JJ_SET_COIN, r->streak = (uint8_t)range(r, jj_sets[JJ_SET_COIN].minc, jj_sets[JJ_SET_COIN].maxc); break;
  }
  r->next = kind >= 5 ? 0 : (float)range(r, imin, imax) + width;
}

/* ---------------------------------------------------------------- the run */
void jj_start(jj_run *r, uint32_t seed, bool double_coins, bool head_start) {
  memset(r, 0, sizeof *r);
  r->rng = seed ? seed : 0x4A4A4A4Au;
  r->crowd = (r->rng * 0x9E3779B9u) ^ 0x5BD1E995u;
  r->level = (r->rng * 0x85EBCA6Bu) ^ 0xC2B2AE35u;
  if (!r->crowd) r->crowd = 1;
  r->y = JJ_FLOOR;
  r->ground = 1;
  r->double_coins = double_coins;
  r->head_start = head_start;
  uint32_t m0 = head_start ? 500 : 0;
  r->dist = m0 * 16 * JJ_DIST_DIV;
  if (head_start) r->safe = 120;
  r->next = 300;
  r->token_last = m0 + (uint32_t)range(r, JJ_TOKEN_FIRST_MIN, JJ_TOKEN_FIRST_MAX);
  r->token_km = m0 / JJ_TOKEN_KM;
  r->vehicle_at = m0 + (uint32_t)range(r, 50, JJ_VEHICLE_RANGE);
}

bool jj_over(const jj_run *r) { return r->state == JJ_STOPPED && r->stop_t >= 45; }

static void boom(jj_run *r, float x, float y, int kind) {
  int k = 0;   /* a free one, else the oldest */
  for (int i = 0; i < JJ_FX; i++) {
    if (!r->fx[i].t) {
      k = i;
      break;
    }
    if (r->fx[i].t > r->fx[k].t) k = i;
  }
  r->fx[k] = (jj_fx){x, y, (uint8_t)kind, 1};
}

/* everything on screen switched off: a vehicle taken, a Second Chance */
static void clear_screen(jj_run *r) {
  for (int i = 0; i < JJ_ZAPPERS; i++)
    if (r->zap[i].on && r->zap[i].x < JJ_W + 60) r->zap[i].dud = 1;
  for (int i = 0; i < JJ_MISSILES; i++)
    if (r->mis[i].on) {
      if (r->mis[i].state == JJ_FLY) boom(r, r->mis[i].x, r->mis[i].y, 0);
      r->mis[i].on = 0;
    }
  r->laser = 0;
}

void jj_revive(jj_run *r) {
  clear_screen(r);
  r->state = JJ_ALIVE, r->by = 0, r->stop_t = 0;
  r->ride = JJ_FOOT, r->set = JJ_SET_DEFAULT, r->streak = 0;
  r->vy = -6, r->ground = 0, r->safe = 120, r->dvx = 0;
  r->revived++;
}

/* his body spins as it flies and bounces: 8 to 18 degrees a frame (settings.xml MIN/MAX_ROTATION_SPEED), one of
   the four death bounce poses each time it leaves the floor */
static void new_spin(jj_run *r) {
  r->spin = (int16_t)(crowd(r, 8, 18) * 65536 / 360);
  r->pose = (uint8_t)crowd(r, 0, 3);
}
static void tumble(jj_run *r, float vy) {
  r->state = JJ_TUMBLE, r->vy = vy, r->stop_t = 0, r->rot = 0;
  new_spin(r);
}

static void knock_out(jj_run *r, int by) {
  r->by = (uint8_t)by;
  r->dvx = jj_speed(r) / 60.0f;
  r->ride = JJ_FOOT;
  r->held = 0;
  if (by == JJ_BY_ZAPPER) {
    r->state = JJ_ZAPPED, r->stop_t = 0;
  } else {
    boom(r, JJ_BX, r->y - 20, 0);
    tumble(r, -5);
  }
}

/* hit: a vehicle breaks, else Barry's out */
static void hit(jj_run *r, int by) {
  if (r->safe || r->state != JJ_ALIVE) return;
  if (r->ride != JJ_FOOT) {
    boom(r, JJ_BX, r->y - jj_box_h(r->ride) / 2.0f, 1);
    r->ride = JJ_FOOT, r->set = JJ_SET_DEFAULT, r->streak = 0, r->flip = 0;
    r->safe = FRAMES_SAFE + 30;
    r->vy = -4, r->ground = 0;
    r->vehicle_at = jj_metres(r) + (uint32_t)range(r, JJ_VEHICLE_EXCLUSION, JJ_VEHICLE_RANGE);
    return;
  }
  knock_out(r, by);
}

static void take_vehicle(jj_run *r) {
  int total = 0;
  for (int i = 0; i < JJ_RIDES - 1; i++) total += jj_vehicles[i].weight;
  int x = range(r, 0, total - 1), v = 0;
  while (v < JJ_RIDES - 2 && x >= jj_vehicles[v].weight) x -= jj_vehicles[v++].weight;
  const jj_vehicle_t *d = &jj_vehicles[v];
  float now = jj_speed(r);
  r->ride = (uint8_t)(v + 1);
  r->ride_speed = now > d->start ? now : d->start;
  r->set = d->set, r->streak = 0;
  r->jumps = d->jumps, r->boost = 0, r->flip = 0, r->cft = 0;
  r->vy = 0;
  r->next = 260;
  clear_screen(r);
}

static void barry(jj_run *r, bool held) {
  bool press = held && !r->held;
  int h = jj_box_h(r->ride);
  if (r->ride == JJ_FOOT) {
    if (press) r->vy *= JJ_VSCALAR;   /* a boost starts from half the speed he had (vehicles: from none) */
    r->vy += JJ_GRAVITY;
    if (held) r->vy -= JJ_BOOST;
    r->vy *= JJ_FRICTION;
  } else if (r->ride == JJ_CFT) {
    r->cft = (uint16_t)(r->cft + 65536 * 8 / 360);
    if (r->boost) r->boost--;
    if (press) {
      int ang = jj_sin(r->cft) * 16384 / 1024;   /* +-90 degrees */
      float ty = r->y - h / 2.0f - 96.0f * jj_sin((uint16_t)ang) / 1024.0f;
      if (ty < JJ_CEIL + h / 2.0f) ty = JJ_CEIL + h / 2.0f;
      if (ty > JJ_FLOOR - h / 2.0f) ty = JJ_FLOOR - h / 2.0f;
      r->y = ty + h / 2.0f;
      r->boost = 12;   /* (the Teleporter's teleport frames) */
    }
    r->vy = 0;
  } else if (r->ride == JJ_GRAV) {
    const jj_vehicle_t *d = &jj_vehicles[JJ_GRAV - 1];
    if (press) r->flip ^= 1, r->ground = 0;
    r->vy += r->flip ? -d->gravity : d->gravity;
    r->vy *= d->friction;
  } else {
    const jj_vehicle_t *d = &jj_vehicles[r->ride - 1];
    if (press && r->jumps > 0 && (r->ride == JJ_BIRD || r->ground)) {
      r->vy = -d->initial;
      r->boost = d->frames;
      r->jumps--;
      r->ground = 0;
    }
    if (held && r->ride != JJ_BIRD) {
      if (r->boost) r->vy -= d->boost, r->boost--;
      else if (!r->ground) r->vy -= d->final_boost;
    }
    if (!held) r->boost = 0;
    r->vy += d->gravity;
    r->vy *= d->friction;
  }
  r->held = held;
  r->y += r->vy;
  if (r->y >= JJ_FLOOR) {
    r->y = JJ_FLOOR;
    if (r->vy > 0) r->vy = 0;
    if (!r->ground && r->ride != JJ_FOOT && r->ride != JJ_CFT) r->jumps = jj_vehicles[r->ride - 1].jumps;
    r->ground = !(r->ride == JJ_GRAV && r->flip);
  } else if (r->y - h <= JJ_CEIL) {
    r->y = JJ_CEIL + h;
    if (r->vy < 0) r->vy = 0;
    r->ground = r->ride == JJ_GRAV && r->flip;
  } else {
    r->ground = 0;
  }
}

static bool boxes(float ax, float ay, float aw, float ah, float bx, float by, float bw, float bh) {
  return ax < bx + bw && bx < ax + aw && ay < by + bh && by < ay + ah;
}

/* a zapper hits what its electricity touches: the beam, 5 points either side of the line between the orbs (its
   glow), and the orbs, 9 points round (their picture). Points along it, 2 apart, are tried against the box. */
static float box_d2(float px, float py, float bx, float by, float bw, float bh) {
  float ex = px < bx ? bx - px : px > bx + bw ? px - (bx + bw) : 0;
  float ey = py < by ? by - py : py > by + bh ? py - (by + bh) : 0;
  return ex * ex + ey * ey;
}
static bool zapper_hits(const jj_zapper *z, float bx, float by, float bw, float bh) {
  float dx = jj_cos(z->ang) * (z->len / 2.0f) / 1024.0f, dy = jj_sin(z->ang) * (z->len / 2.0f) / 1024.0f;
  if (box_d2(z->x - dx, z->y - dy, bx, by, bw, bh) < 9 * 9 || box_d2(z->x + dx, z->y + dy, bx, by, bw, bh) < 9 * 9) return true;
  int n = z->len / 2;
  for (int i = 0; i <= n; i++) {
    float t = -1.0f + 2.0f * (float)i / (float)n;
    if (box_d2(z->x + dx * t, z->y + dy * t, bx, by, bw, bh) < 5 * 5) return true;
  }
  return false;
}

/* ---------------------------------------------------------------- the jetpack's fire and the scientists
 * weapons.xml JETPACK_DEFAULT: a bullet every 2 frames, 15 points a frame, at 185 degrees (0 up, clockwise) give or
 * take 17.5, from 10 points behind and 28 under Barry's middle; a bullet that meets a scientist knocks him out
 * (killOnCollide), one that meets the floor makes a ricochet. The shells fall out behind. */
static void fire(jj_run *r) {
  if (r->fire) {
    r->fire--;
    return;
  }
  r->fire = 1;
  for (int i = 0; i < JJ_BULLETS; i++) {
    jj_bullet *b = &r->bul[i];
    if (b->vy != 0) continue;
    int deg = 185 * 2 + crowd(r, -35, 35);   /* half degrees */
    uint16_t ang = (uint16_t)(deg * 65536 / 720);
    b->x = JJ_BX - 6, b->y = r->y + 12;
    b->vx = 15.0f * (float)jj_sin(ang) / 1024.0f, b->vy = -15.0f * (float)jj_cos(ang) / 1024.0f;
    break;
  }
  if (r->t & 2) {
    for (int i = 0; i < JJ_SHELLS; i++) {
      jj_shell *c = &r->shell[i];
      if (c->on) continue;
      c->on = 1, c->rot = (uint8_t)crowd(r, 0, 3);
      c->x = JJ_BX - 10, c->y = r->y - 10;
      c->vx = crowdf(r, -3.0f, -1.5f), c->vy = crowdf(r, -3.0f, -1.0f);
      break;
    }
  }
}

static void spark(jj_run *r, float x) {
  int k = 0;
  for (int i = 0; i < JJ_SPARKS; i++) {
    if (!r->spark[i].t) {
      k = i;
      break;
    }
    if (r->spark[i].t > r->spark[k].t) k = i;
  }
  r->spark[k].x = x, r->spark[k].t = 1;
}

static bool sci_standing(const jj_scientist *s) { return s->state <= JJ_SCI_FETAL; }

/* knocked out: up into the air and bouncing (DeathBounce: 1.5 to 4 points a frame along, 1.5 to 7 up), or thrown
   by a vehicle (KnockDeath: 4 to 8 points a frame, up give or take 45 degrees, falling faster) */
static void sci_down(jj_run *r, jj_scientist *s, int dir, bool knock) {
  s->state = JJ_SCI_AIR, s->t = 0, s->timer = knock;
  s->pose = (uint8_t)crowd(r, 0, 3);
  if (knock) {
    float v = crowdf(r, 4.0f, 8.0f);
    uint16_t a = (uint16_t)(crowd(r, -45, 45) * 65536 / 360);
    s->vx = v * (float)jj_sin(a) / 1024.0f, s->vy = -v * (float)jj_cos(a) / 1024.0f;
  } else {
    s->vx = (float)dir * crowdf(r, 1.5f, 4.0f), s->vy = -crowdf(r, 1.5f, 7.0f);
  }
}

static void scientist(jj_run *r, jj_scientist *s, float dx, uint32_t m) {
  s->t++;
  switch (s->state) {
  case JJ_SCI_WALK:
  case JJ_SCI_RUN:
    s->x += (float)s->dir * s->vx - dx;
    /* Barry comes near: one in ten curls up, most run off (60 in 100, 75 from 1000 m) */
    if (!s->alert && r->state == JJ_ALIVE && s->x > JJ_BX && s->x - JJ_BX < s->near) {
      s->alert = 1;
      int p = crowd(r, 0, 99), run = m >= 1000 ? 75 : 60;
      if (p < 10) {
        s->state = JJ_SCI_FETAL, s->t = 0;
      } else if (p < 10 + run) {
        s->state = JJ_SCI_RUN, s->t = 0, s->dir = 1, s->vx = crowdf(r, 2.0f, 6.0f);
        /* a runner may trip (30 in 100) or curl up (10 in 100) after a while */
        int q = crowd(r, 0, 99);
        if (q < 30) s->alert = 3, s->timer = (uint16_t)(m >= 1000 ? crowd(r, 15, 60) : crowd(r, 30, 180));
        else if (q < 40) s->alert = 5, s->timer = (uint16_t)crowd(r, 120, 240);
      }
    }
    if (s->timer && !--s->timer) {
      if (s->alert & 2) s->state = JJ_SCI_SLIDE, s->pose = 0, s->t = 0, s->vx *= (float)s->dir;   /* tripped, flat on his face */
      else s->state = JJ_SCI_FETAL, s->t = 0;
    }
    break;
  case JJ_SCI_FETAL:
    s->vx *= 0.95f;   /* FetalFriction */
    s->x += (float)s->dir * s->vx - dx;
    break;
  case JJ_SCI_ZAPPED:
    s->x -= dx;
    if (s->t >= 24) sci_down(r, s, -1, false);
    break;
  case JJ_SCI_AIR:
    s->vy += s->timer ? 0.5f : 0.24f;
    s->vy *= 0.98f;
    s->y += s->vy;
    s->x += s->vx - dx;
    if (s->y >= JJ_FLOOR) {
      s->y = JJ_FLOOR;
      if (s->vy > 2.0f) s->vy = -s->vy * 0.7f, s->pose = (uint8_t)crowd(r, 0, 3);   /* DeathBounce_Y_Dampening */
      else s->vy = 0, s->state = JJ_SCI_SLIDE, s->t = 0;
    }
    break;
  case JJ_SCI_SLIDE:
    s->vx *= 0.94f;   /* Death_X_Ground_Friction */
    s->x += s->vx - dx;
    break;
  }
  if (s->x < -40 || s->x > JJ_SPAWN_X + 60) s->on = 0;
}

void jj_step(jj_run *r, bool held) {
  r->t++;
  for (int i = 0; i < JJ_FX; i++)
    if (r->fx[i].t && ++r->fx[i].t > 32) r->fx[i].t = 0;
  float dx;
  if (r->state == JJ_ALIVE) {
    barry(r, held);
    if (r->safe) r->safe--;
    if (r->ride != JJ_FOOT) {
      const jj_vehicle_t *d = &jj_vehicles[r->ride - 1];
      r->ride_speed += d->increase;
      if (r->ride_speed > d->max) r->ride_speed = d->max;
    }
    dx = jj_speed(r) / 60.0f;
  } else {
    /* knocked out: zapped a moment, then he tumbles and slides to a stop */
    if (r->state == JJ_ZAPPED) {
      if (++r->stop_t >= 24) tumble(r, -3);
    } else {
      bool air = r->y < JJ_FLOOR || r->vy != 0;
      r->vy += JJ_DEATH_GRAVITY;
      r->y += r->vy;
      if (air) r->rot = (uint16_t)(r->rot + r->spin);
      if (r->y >= JJ_FLOOR) {
        r->y = JJ_FLOOR;
        r->vy = r->vy > JJ_MIN_BOUNCE ? -r->vy * 0.5f : 0;
        if (r->vy == 0) r->dvx *= JJ_DEATH_GROUND_FRICTION;
        else new_spin(r);
      }
      if (r->state == JJ_TUMBLE && r->vy == 0 && r->y >= JJ_FLOOR && r->dvx < 0.3f) r->state = JJ_STOPPED, r->dvx = 0;
      if (r->state == JJ_STOPPED && r->stop_t < 60000) r->stop_t++;
    }
    dx = r->dvx;
  }
  r->dist += (uint32_t)(dx * 16.0f + 0.5f);
  uint32_t m = jj_metres(r);

  /* things move */
  for (int i = 0; i < JJ_ZAPPERS; i++) {
    jj_zapper *z = &r->zap[i];
    if (!z->on) continue;
    z->x -= dx;
    z->ang = (uint16_t)(z->ang + z->spin);
    if (z->x < -z->len / 2 - 40) {
      z->on = 0;
      if (r->state == JJ_ALIVE) r->zappers_dodged++;
    }
  }
  for (int i = 0; i < JJ_COINSETS; i++)
    if (r->coin[i].on && (r->coin[i].x -= dx) < -(float)jj_pat_w[r->coin[i].pat] * JJ_COIN_GAP - 20) r->coin[i].on = 0;
  for (int i = 0; i < JJ_TOKENS; i++) {
    jj_token *t = &r->token[i];
    if (!t->on) continue;
    t->x -= dx;
    t->phase = (uint16_t)(t->phase + 300);
    if (t->x < -30) t->on = 0;
  }
  if (r->box.on) {
    r->box.x -= dx;
    r->box.phase = (uint16_t)(r->box.phase + 600);
    if (r->box.x < -40) r->box.on = 0, r->vehicle_at = m + (uint32_t)range(r, JJ_VEHICLE_EXCLUSION, JJ_VEHICLE_RANGE);
  }
  for (int i = 0; i < JJ_FX; i++)
    if (r->fx[i].t) r->fx[i].x -= dx;
  if (r->state == JJ_ALIVE && r->ride == JJ_FOOT && held) fire(r);
  else r->fire = 0;
  for (int i = 0; i < JJ_BULLETS; i++) {
    jj_bullet *b = &r->bul[i];
    if (b->vy == 0) continue;
    b->x += b->vx - dx, b->y += b->vy;
    for (int k = 0; k < JJ_SCIENTISTS; k++) {
      jj_scientist *c = &r->sci[k];
      if (!c->on || !sci_standing(c)) continue;
      /* the bullet's last frame of flight, in 3 steps, against his 16 x 28 points */
      for (int q = 0; q < 3; q++) {
        float px = b->x - (b->vx - dx) * (float)q / 3.0f, py = b->y - b->vy * (float)q / 3.0f;
        if (px > c->x - 8 && px < c->x + 8 && py > c->y - 28 && py < c->y) {
          sci_down(r, c, b->vx < 0 ? -1 : 1, false);
          b->vy = 0;
          break;
        }
      }
      if (b->vy == 0) break;
    }
    if (b->vy == 0) continue;
    if (b->y >= JJ_FLOOR) spark(r, b->x - (b->y - JJ_FLOOR) * b->vx / b->vy), b->vy = 0;
    else if (b->x < -20) b->vy = 0;
  }
  for (int i = 0; i < JJ_SHELLS; i++) {
    jj_shell *c = &r->shell[i];
    if (!c->on) continue;
    c->vy += 0.25f;
    c->x += c->vx - dx, c->y += c->vy;
    if ((r->t & 3) == 0) c->rot = (uint8_t)((c->rot + 1) & 3);
    if (c->y > JJ_FLOOR + 6 || c->x < -10) c->on = 0;
  }
  for (int i = 0; i < JJ_SPARKS; i++)
    if (r->spark[i].t) {
      r->spark[i].x -= dx;
      if (++r->spark[i].t > 4) r->spark[i].t = 0;
    }
  for (int i = 0; i < JJ_SCIENTISTS; i++)
    if (r->sci[i].on) scientist(r, &r->sci[i], dx, m);
  for (int i = 0; i < JJ_MISSILES; i++) {
    jj_missile *s = &r->mis[i];
    if (!s->on) continue;
    s->t++;
    float target = r->y - jj_box_h(r->ride) / 2.0f;
    if (s->state == JJ_WARN) {
      if (r->state == JJ_ALIVE) s->y += s->y < target - s->chase ? s->chase : s->y > target + s->chase ? -s->chase : target - s->y;
      if (s->t >= WARN_FRAMES) s->state = JJ_LOCK, s->t = 0;
    } else if (s->state == JJ_LOCK) {
      if (s->t >= LOCK_FRAMES) s->state = JJ_FLY, s->t = 0, s->x = JJ_W + 30;
    } else {
      s->x -= jj_mis_files[0].speed + dx;
      if (s->x < -40) {
        s->on = 0;
        if (r->state == JJ_ALIVE) r->missiles_dodged++;
      }
    }
  }
  if (r->laser) {
    r->laser_t++;
    if (r->laser_t > jj_lpats[r->laser - 1].time + SLIDE) r->laser = 0, r->next = 120;
  }

  /* the lab's own defences get the scientists too: zappers and lasers zap them, missiles blow them up, and a
     vehicle throws them */
  for (int i = 0; i < JJ_SCIENTISTS; i++) {
    jj_scientist *c = &r->sci[i];
    if (!c->on || !sci_standing(c)) continue;
    float cx = c->x - 8, cy = c->y - 28;
    bool zap = false;
    for (int k = 0; k < JJ_ZAPPERS && !zap; k++) {
      const jj_zapper *z = &r->zap[k];
      zap = z->on && !z->dud && z->x - z->len / 2 - 20 < cx + 16 && z->x + z->len / 2 + 20 > cx && zapper_hits(z, cx, cy, 16, 28);
    }
    if (r->laser)
      for (int b = 0; b < jj_lpats[r->laser - 1].count && !zap; b++) {
        int y, phase, k;
        jj_beam_state(r->laser - 1, b, r->laser_t, &y, &phase, &k);
        zap = phase == 2 && k >= 4 && y + 7 > cy && y - 7 < cy + 28;
      }
    if (zap) {
      c->state = JJ_SCI_ZAPPED, c->t = 0;
      continue;
    }
    for (int k = 0; k < JJ_MISSILES; k++) {
      jj_missile *s2 = &r->mis[k];
      if (s2->on && s2->state == JJ_FLY && boxes(s2->x - 14, s2->y - 6, 28, 12, cx, cy, 16, 28)) {
        sci_down(r, c, -1, true);   /* (the missile flies on: it's after Barry) */
        break;
      }
    }
    if (sci_standing(c) && r->state == JJ_ALIVE && r->ride != JJ_FOOT) {
      int w = jj_box_w(r->ride), h = jj_box_h(r->ride);
      if (boxes(JJ_BX - w / 2.0f, r->y - h, (float)w, (float)h, cx, cy, 16, 28)) sci_down(r, c, 1, true);
    }
  }

  if (r->state != JJ_ALIVE) {
    /* coins still count while he slides */
    goto coins;
  }

  /* what comes next */
  if (!r->laser) {
    r->next -= dx;
    if (r->next <= 0) spawn(r);
    if (r->ride == JJ_FOOT && !r->box.on && m >= r->vehicle_at && !r->laser) {
      r->box.on = 1, r->box.x = JJ_SPAWN_X;
      r->box.y = (float)range(r, JJ_CEIL + 50, JJ_MID - 10);
      r->box.phase = 0;
      r->vehicle_at = 0xffffffffu;
    }
  }

  /* collisions */
  {
    int w = jj_box_w(r->ride), h = jj_box_h(r->ride);
    float bx = JJ_BX - w / 2.0f, by = r->y - h;
    for (int i = 0; i < JJ_ZAPPERS; i++) {
      jj_zapper *z = &r->zap[i];
      if (z->on && !z->dud && z->x - z->len / 2 - 20 < bx + w && z->x + z->len / 2 + 20 > bx && zapper_hits(z, bx, by, w, h)) {
        if (!r->safe && r->ride != JJ_FOOT) z->dud = 1;
        hit(r, JJ_BY_ZAPPER);
        if (r->state != JJ_ALIVE) break;
      }
    }
    for (int i = 0; i < JJ_MISSILES && r->state == JJ_ALIVE; i++) {
      jj_missile *s = &r->mis[i];
      if (s->on && s->state == JJ_FLY && boxes(s->x - 14, s->y - 6, 28, 12, bx, by, w, h)) {
        if (!r->safe) s->on = 0, boom(r, s->x, s->y, 0);
        hit(r, JJ_BY_MISSILE);
      }
    }
    if (r->laser && r->state == JJ_ALIVE) {
      const jj_lpat_t *p = &jj_lpats[r->laser - 1];
      for (int b = 0; b < p->count; b++) {
        int y, phase, k;
        jj_beam_state(r->laser - 1, b, r->laser_t, &y, &phase, &k);
        if (phase == 2 && k >= 4 && y + 7 > by && y - 7 < by + h) {
          hit(r, JJ_BY_LASER);
          break;
        }
      }
    }
    for (int i = 0; i < JJ_TOKENS; i++) {
      jj_token *t = &r->token[i];
      if (!t->on) continue;
      float ty = t->y + 60.0f * jj_sin(t->phase) / 1024.0f;
      if (boxes(t->x - 12, ty - 12, 24, 24, bx, by, w, h)) {
        t->on = 0;
        if (r->tokens < 99) r->tokens++;
        r->tokens_taken++;
      }
    }
    if (r->box.on && r->ride == JJ_FOOT) {
      float yy = r->box.y + 6.0f * jj_sin(r->box.phase) / 1024.0f;
      if (boxes(r->box.x - 20, yy - 20, 40, 40, bx, by, w, h)) {
        r->box.on = 0;
        take_vehicle(r);
      }
    }
  }
coins: {
  int w = jj_box_w(r->ride), h = jj_box_h(r->ride);
  if (r->state != JJ_ALIVE) w = 24, h = 24;
  float bx = JJ_BX - w / 2.0f, by = r->y - h;
  for (int i = 0; i < JJ_COINSETS; i++) {
    jj_coinset *c = &r->coin[i];
    if (!c->on) continue;
    int c0 = (int)floorf((bx - 8 - c->x) / JJ_COIN_GAP + 0.5f), c1 = (int)floorf((bx + w + 8 - c->x) / JJ_COIN_GAP + 0.5f);
    int r0 = (int)floorf((by - 8 - c->y) / JJ_COIN_GAP + 0.5f), r1 = (int)floorf((by + h + 8 - c->y) / JJ_COIN_GAP + 0.5f);
    for (int row = r0; row <= r1; row++)
      for (int col = c0; col <= c1; col++)
        if (jj_coin_at(c, col, row)) {
          float cx = c->x + col * JJ_COIN_GAP, cy = c->y + row * JJ_COIN_GAP;
          if (cx + 8 > bx && cx - 8 < bx + w && cy + 8 > by && cy - 8 < by + h) {
            int pw = (jj_pat_w[c->pat] + 7) / 8;
            c->bits[row * pw + col / 8] &= (uint8_t)~(1u << (col & 7));
            uint32_t add = r->double_coins ? 2 : 1;
            if (r->coins < 99999999u) r->coins += add;
          }
        }
  }
}
}

/* ---------------------------------------------------------------- saves read back */
static bool finite_in(float v, float lo, float hi) { return v == v && v >= lo && v <= hi; }

bool jj_valid(const jj_run *r) {
  if (!finite_in(r->y, JJ_CEIL, JJ_FLOOR + 1) || !finite_in(r->vy, -60, 60) || !finite_in(r->dvx, 0, 120) ||
      !finite_in(r->ride_speed, 0, 6001) || !finite_in(r->next, -200, 2000))
    return false;
  if (r->held > 1 || r->ground > 1 || r->state > JJ_STOPPED || r->by > JJ_BY_LASER || r->ride >= JJ_RIDES || r->flip > 1 ||
      r->set > JJ_SET_CFT || r->laser > JJ_LASER_PATTERNS || r->tokens > 99 || r->double_coins > 1 || r->head_start > 1 ||
      r->jumps > 10 || r->boost > 40 || r->safe > 400)
    return false;
  if (r->laser && r->laser_t > jj_lpats[r->laser - 1].time + SLIDE + 1) return false;
  for (int i = 0; i < JJ_ZAPPERS; i++) {
    const jj_zapper *z = &r->zap[i];
    if (z->on > 1 || z->dud > 1 || (z->on && (!finite_in(z->x, -400, 900) || !finite_in(z->y, 0, JJ_H) || z->len > 300)))
      return false;
  }
  for (int i = 0; i < JJ_COINSETS; i++) {
    const jj_coinset *c = &r->coin[i];
    if (c->on > 1 || (c->on && (c->pat >= JJ_PATTERNS || !finite_in(c->x, -1000, 900) || c->y < 0 || c->y > JJ_H))) return false;
  }
  for (int i = 0; i < JJ_MISSILES; i++) {
    const jj_missile *s = &r->mis[i];
    if (s->on > 1 || (s->on && (s->state > JJ_FLY || !finite_in(s->x, -200, 900) || !finite_in(s->y, -100, 500) ||
                                !finite_in(s->chase, 0, 10))))
      return false;
  }
  for (int i = 0; i < JJ_TOKENS; i++)
    if (r->token[i].on > 1 || (r->token[i].on && (!finite_in(r->token[i].x, -100, 900) || !finite_in(r->token[i].y, 0, JJ_H))))
      return false;
  if (r->box.on > 1 || (r->box.on && (!finite_in(r->box.x, -100, 900) || !finite_in(r->box.y, 0, JJ_H)))) return false;
  for (int i = 0; i < JJ_FX; i++)
    if (r->fx[i].t > 33 || r->fx[i].kind > 1 || (r->fx[i].t && (!finite_in(r->fx[i].x, -2000, 2000) || !finite_in(r->fx[i].y, -500, 900))))
      return false;
  for (int i = 0; i < JJ_SCIENTISTS; i++) {
    const jj_scientist *c = &r->sci[i];
    if (c->on > 1 || (c->on && (c->state > JJ_SCI_SLIDE || c->kind > 1 || c->pose > 3 || (c->dir != 1 && c->dir != -1) ||
                                !finite_in(c->x, -100, 1000) || !finite_in(c->y, -400, JJ_FLOOR) || !finite_in(c->vx, -20, 20) ||
                                !finite_in(c->vy, -20, 20))))
      return false;
  }
  for (int i = 0; i < JJ_BULLETS; i++) {
    const jj_bullet *b = &r->bul[i];
    if (b->vy != 0 && (!finite_in(b->x, -100, 900) || !finite_in(b->y, -100, JJ_FLOOR + 20) || !finite_in(b->vx, -16, 16) ||
                       !finite_in(b->vy, 1, 16)))
      return false;
  }
  for (int i = 0; i < JJ_SHELLS; i++) {
    const jj_shell *c = &r->shell[i];
    if (c->on > 1 || c->rot > 3 || (c->on && (!finite_in(c->x, -100, 900) || !finite_in(c->y, -100, JJ_FLOOR + 20) ||
                                              !finite_in(c->vx, -20, 20) || !finite_in(c->vy, -20, 40))))
      return false;
  }
  for (int i = 0; i < JJ_SPARKS; i++)
    if (r->spark[i].t > 4 || (r->spark[i].t && !finite_in(r->spark[i].x, -2000, 2000))) return false;
  if (r->pose > 3 || !r->crowd) return false;
  return true;
}

/* ---------------------------------------------------------------- the Final Spin */
void jj_spin_start(jj_spin *s) {
  for (int i = 0; i < JJ_PRIZES; i++) s->weight[i] = jj_prize_weight[i];
  s->won = 0;
}

int jj_spin_roll(jj_spin *s, uint32_t *rng) {
  s->won = 0;
  if (jj_rand(rng) % 4) return -1;   /* one token in four wins (the spec, [P]) */
  int total = 0;
  for (int i = 0; i < JJ_PRIZES; i++) total += s->weight[i];
  if (total <= 0) return -1;
  int x = (int)(jj_rand(rng) % (uint32_t)total), p = 0;
  while (p < JJ_PRIZES - 1 && x >= s->weight[p]) x -= s->weight[p++];
  s->weight[p] = (uint16_t)(s->weight[p] * jj_prize_after[p] / 1000);
  s->won = (uint8_t)(p + 1);
  return p;
}
