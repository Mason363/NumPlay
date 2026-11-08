/* Doodle Jump: the rules. The doodler jumps by himself whenever he lands, the arrows steer him, and going off one
 * side of the page brings him back on the other. Green platforms stay, blue ones slide, brown ones break under him,
 * white ones are gone after one jump and yellow ones turn orange, then red, then blow up. Springs, trampolines,
 * propeller hats and jetpacks throw him higher; monsters, black holes and UFOs end the climb unless he shoots them or
 * lands on top (black holes can't be shot). The score is the highest the climb has reached, in points.
 *
 * The numbers are gamesrc/doodlejump/spec/'s: the page in points, time in the original's frames (60 a second). */
#include "dj.h"

world_t W;

uint32_t game_rand(void) {
  uint32_t x = W.rng;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return W.rng = x;
}
static float frand(float a, float b) { return a + (b - a) * (float)(game_rand() & 0xFFFF) / 65535.0f; }
static bool chance(int per1000) { return (int)(game_rand() % 1000) < per1000; }
static float fabs_(float v) { return v < 0 ? -v : v; }
static float minf(float a, float b) { return a < b ? a : b; }
static float sin_(float x) { /* (the fliers' sway: no libm) */
  const float tp = 6.2831853f;
  x -= tp * (float)(int)(x / tp);
  if (x < 0) x += tp;
  if (x > 3.1415927f) return -sin_(x - 3.1415927f);
  if (x > 1.5707964f) x = 3.1415927f - x;
  float x2 = x * x;
  return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42)));
}

/* a monster's box, half its width and height in points: its picture, a fifth in from its edges */
void mon_box(int kind, float *hw, float *hh) {
  static const uint8_t pic[M_KINDS] = {0, S_M_FLY0, S_M_BAT0, S_M_EYE0, S_M_BLOB0, S_M_CROC0, S_M_HAIRY, S_M_BUG0,
                                        S_M_RED, S_M_PURPLE, S_M_HOLE, S_M_UFO0};
  const dj_sprite_t *s = &dj_spr[pic[kind]];
  *hw = (float)s->w * 0.8f, *hh = (float)s->h * 0.8f; /* (a pixel is 2 points: half of it is w) */
}
float mon_x(const mon_t *m) {
  return m->x + ((m->kind == M_FLY || m->kind == M_BAT) && !m->state ? 30 * sin_(m->t * 0.05f) : 0);
}
float blue_speed(void) { return 1.0f + minf((float)W.score / 8000.0f, 1.2f); }

/* the next platform up, at a height the doodler can always reach from the last one he can stand on, and the page
   getting harder with height: further apart, more of them moving, white, about to blow up. Brown ones come in
   between, only looking like help; a monster, a black hole or a UFO now and then beside the way up, the next
   platform on the other side of the page from it */
static void spawn(plat_t *p) {
  float h = W.top, gmax = minf(60 + h / 60, 150), gmin = minf(20 + h / 250, 90); /* (a jump is 173) */
  if (h > 300 && chance(150)) { /* a brown one, somewhere below the next real one */
    *p = (plat_t){frand(PW, PAGE_W - PW), W.top + frand(15, gmax * 0.8f), 0, 1, P_BROWN, I_NONE, 0};
    return;
  }
  int r = (int)(game_rand() % 1000), blue = h < 1000 ? 0 : (int)minf((h - 1000) / 22, 400);
  int white = h < 3000 ? 0 : (int)minf(100 + (h - 3000) / 60, 250), bomb = h < 8000 ? 0 : 80;
  int type = r < blue ? P_BLUE : r < blue + white ? P_WHITE : r < blue + white + bomb ? P_BOMB : P_GREEN;
  float y = W.top + frand(gmin, gmax), x = frand(PW, PAGE_W - PW);
  if (type != P_BOMB) { /* (a bomb may be gone by the time he gets there: never the only way up) */
    W.top = y;
    if (W.side) /* (away from the monster) */
      x = W.side > 0 ? frand(PAGE_W / 2 + 10, PAGE_W - PW) : frand(PW, PAGE_W / 2 - 10), W.side = 0;
  }
  *p = (plat_t){x, y, 0, (int8_t)(game_rand() & 1 ? 1 : -1), (uint8_t)type, I_NONE, 0};
  if (p->type == P_GREEN || p->type == P_BLUE) {
    r = (int)(game_rand() % 1000);
    if (r < 80) p->item = I_SPRING;
    else if (h > 2000 && r < 100) p->item = I_TRAMP;
    else if (h > 1000 && r < 112) p->item = I_PROP;
    else if (h > 3000 && r < 118) p->item = I_JET;
  }
  if (h > 2500 && chance(h > 8000 ? 60 : 40))
    for (int i = 0; i < NM; i++)
      if (W.mon[i].kind == M_NONE || W.mon[i].y < W.cam - 100) {
        static const uint8_t still[] = {M_EYE, M_BLOB, M_CROC, M_HAIRY, M_BUG, M_RED, M_PURPLE};
        int k = h > 8000 && chance(250) ? (chance(500) ? M_HOLE : M_UFO)
                : chance(500)            ? (chance(500) ? M_FLY : M_BAT)
                                         : still[game_rand() % sizeof still];
        bool left = p->x > PAGE_W / 2; /* on the other half from this platform, the next one on the other half from it */
        W.mon[i] = (mon_t){left ? frand(45, PAGE_W / 2 - 45) : frand(PAGE_W / 2 + 45, PAGE_W - 45), W.top + 80, 0,
                           (uint8_t)k, 0, {0}};
        W.side = left ? 1 : -1;
        break;
      }
}

void game_new(uint32_t seed) {
  for (uint32_t i = 0; i < sizeof W; i++) ((uint8_t *)&W)[i] = 0;
  W.rng = seed ? seed : 1;
  W.x = PAGE_W / 2, W.y = 40, W.vy = JUMP, W.face = 1;
  W.plat[0] = (plat_t){PAGE_W / 2, 40, 0, 1, P_GREEN, I_NONE, 0}; /* the one he starts on */
  W.top = 40;
  for (int i = 1; i < NP; i++) spawn(&W.plat[i]);
}

bool game_shoot(void) {
  if (W.dead || W.fly > 0) return false;
  for (int i = 0; i < NS; i++)
    if (!W.shot_on[i]) {
      W.shot_on[i] = 1, W.shot_x[i] = W.x, W.shot_y[i] = W.y + BODY, W.shot = W.frame;
      return true;
    }
  return false;
}

bool game_over(void) { return W.falling ? W.dead_t >= 40 : (W.dead == D_HOLE || W.dead == D_UFO) && W.dead_t >= 45; }

/* the feet over a platform's top, either side of the page's edge */
static bool over_x(float px, float half) {
  for (int k = -1; k <= 1; k++)
    if (fabs_(W.x + (float)k * PAGE_W - px) < half + FEET) return true;
  return false;
}

static void end_flight(void) {
  if (W.fly > 0 || W.item == I_NONE) return;
  W.drop = W.item, W.drop_x = W.x, W.drop_y = W.y + 30, W.drop_vy = 2, W.item = I_NONE; /* it falls away */
}

void game_step(int steer) {
  W.frame++;
  if (W.drop) /* the hat or jetpack that fell away */
    if ((W.drop_y += (W.drop_vy -= GRAV)) < W.cam - 100) W.drop = 0;
  for (int i = 0; i < NP; i++) { /* the platforms move and break as he climbs or not */
    plat_t *p = &W.plat[i];
    if (p->type == P_BLUE) { /* sliding from side to side */
      p->x += p->dir * blue_speed();
      if (p->x < PW || p->x > PAGE_W - PW) p->dir = (int8_t)-p->dir;
    }
    if (p->state) p->t += 1; /* (broken, gone, or its spring or trampoline used) */
    else if (p->type == P_BOMB && p->y < W.cam + VIEW && (p->t += 1) > 180) p->state = 1, p->t = 0; /* lit on the page */
  }
  for (int i = 0; i < NM; i++) {
    mon_t *m = &W.mon[i];
    if (m->kind == M_NONE) continue;
    if (m->state) m->y -= 6; /* shot or jumped on: falling away */
    else m->t += 1;
  }
  for (int s = 0; s < NS; s++)
    if (W.shot_on[s] && (W.shot_y[s] += SHOT_V) > W.cam + VIEW + 40) W.shot_on[s] = 0;
  /* bullets hit the monsters and UFOs (not a black hole) */
  for (int i = 0; i < NM; i++) {
    mon_t *m = &W.mon[i];
    if (m->kind == M_NONE || m->state || m->kind == M_HOLE) continue;
    float hw, hh, mx = mon_x(m);
    mon_box(m->kind, &hw, &hh);
    for (int s = 0; s < NS; s++)
      if (W.shot_on[s] && fabs_(W.shot_x[s] - mx) < hw && fabs_(W.shot_y[s] - m->y) < hh) {
        W.shot_on[s] = 0, m->state = 1;
        if (m->kind == M_UFO) W.ufo_shot++;
        else W.mon_shot++;
        break;
      }
  }
  if (W.dead) { /* the end of the climb */
    W.dead_t++;
    if (W.dead == D_HOLE || W.dead == D_UFO) { /* drawn in, the hole or UFO staying where it is */
      W.x += (W.hx - W.x) * 0.15f, W.y += (W.hy - BODY / 2 - W.y) * 0.15f;
      return;
    }
    if (W.falling) { /* falling past the page: it rushes up */
      W.cam -= 24, W.y = W.cam + VIEW - 80 - (float)W.dead_t * 6;
      return;
    }
    W.vy -= GRAV, W.y += W.vy, W.x += W.vx; /* knocked out by a monster */
    if (W.y < W.cam - 80) W.falling = 1, W.dead_t = 0;
    return;
  }
  W.dead_t = 0;
  if (steer) W.vx += (float)steer * ACCEL, W.face = (uint8_t)(steer > 0);
  else W.vx *= 0.85f;
  if (W.vx > VMAX) W.vx = VMAX;
  if (W.vx < -VMAX) W.vx = -VMAX;
  W.x += W.vx;
  if (W.x < 0) W.x += PAGE_W;
  if (W.x >= PAGE_W) W.x -= PAGE_W;
  float oy = W.y;
  if (W.fly > 0) {
    W.vy = W.item == I_JET ? JET : PROP;
    if (--W.fly == 0) end_flight();
  } else W.vy -= GRAV;
  W.y += W.vy;
  if (W.flip) W.flip--;
  for (int i = 0; i < NP; i++) { /* landing */
    plat_t *p = &W.plat[i];
    if (W.vy >= 0 || p->state == 1 || oy < p->y || W.y > p->y || !over_x(p->x, PW)) continue;
    if (p->type == P_BROWN) { /* it breaks, he falls on */
      p->state = 1, p->t = 0;
      continue;
    }
    W.y = p->y, W.vy = JUMP, W.land = W.frame, W.jumps++;
    if (p->item == I_SPRING && over_x(p->x + 8, 8)) W.vy = SPRING, p->state = 2, p->t = 0;
    if (p->item == I_TRAMP && over_x(p->x, 14)) W.vy = TRAMP, p->state = 2, p->t = 0, W.flip = 72;
    if (p->type == P_WHITE) p->state = 1, p->t = 0;
  }
  /* the hat and the jetpack, picked up on the way */
  for (int i = 0; i < NP; i++) {
    plat_t *p = &W.plat[i];
    if ((p->item == I_PROP || p->item == I_JET) && p->state == 0 && W.fly <= 0 && over_x(p->x, 10) &&
        W.y < p->y + 24 && W.y + BODY > p->y) {
      W.drop = 0, W.item = p->item, W.fly = FLY, p->item = I_NONE, W.flip = 0;
      if (W.item == I_JET) W.jets++;
      else W.hats++;
    }
  }
  /* monsters: land on them, shoot them, or keep away; a black hole or a UFO takes him */
  for (int i = 0; i < NM; i++) {
    mon_t *m = &W.mon[i];
    if (m->kind == M_NONE || m->state || W.fly > 0) continue;
    float hw, hh, mx = mon_x(m);
    mon_box(m->kind, &hw, &hh);
    if (!over_x(mx, hw - FEET + 4) || W.y > m->y + hh || W.y + BODY < m->y - hh) continue;
    if (m->kind == M_HOLE) {
      W.dead = D_HOLE, W.hx = mx, W.hy = m->y, W.vy = 0, W.dead_t = 0;
      return;
    }
    if (W.vy < 0 && oy > m->y + hh * 0.3f) { /* landed on top */
      m->state = 1, W.vy = JUMP, W.land = W.frame;
      if (m->kind == M_UFO) W.ufo_stomped++;
      else W.mon_stomped++;
    } else if (m->kind == M_UFO) {
      W.dead = D_UFO, W.hx = mx, W.hy = m->y + hh, W.vy = 0, W.dead_t = 0;
      return;
    } else {
      W.dead = D_MONSTER, W.vy = 0, W.vx = 0, W.flip = 0;
      return;
    }
  }
  /* the page follows him up, and new platforms come in at the top */
  if (W.y > W.cam + VIEW / 2) W.cam = W.y - VIEW / 2;
  if (W.y > (float)W.score) W.score = (uint32_t)W.y;
  for (int i = 0; i < NP; i++)
    if (W.plat[i].y < W.cam - 40) spawn(&W.plat[i]);
  if (W.y < W.cam - 80) W.dead = D_FELL, W.falling = 1, W.dead_t = 0, W.fly = 0;
}
