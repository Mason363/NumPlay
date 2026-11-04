/* Doodle Jump: bounce from platform to platform, as high as you can.
 *
 * Lima Sky's game as it plays on phones: the doodler jumps by himself whenever he lands, the arrows steer
 * him, and going off one side of the page brings him back on the other. Green platforms stay, blue ones
 * slide, brown ones break under him, white ones are gone after one jump and yellow ones blow up. Springs,
 * trampolines, propeller hats and jetpacks throw him higher; monsters and black holes end the climb unless
 * he shoots them or lands on top. The page is the original's, 320 x 480 points, shown at half size.
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and saves
 * through its file functions; on its own (DoodleJump.nwa) it brings them along.
 *
 * Made by Bobydeluxe and Mason Chen as part of NumPlay. */
#include <eadk.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"

#if NP_MODULE /* (np_file_read and np_file_write: NumPlay's, launcher/src/np.h) */
#define np_app_begin()
#define np_app_end() 0
#else
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#define np_file_read ef_read
#define np_file_write ef_write
#ifndef HOST /* (a computer build of the tests has no app header) */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Doodle Jump";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

#define KEY(k) (1ull << (k))

/* ------------------------------------------------------------ the numbers, in the original's points and frames */
#define W 320.0f      /* the page, 320 x 480 */
#define VIEW 480.0f
#define G 0.35f       /* gravity, points a frame a frame */
#define JUMP 11.0f    /* a jump: 173 points up */
#define SPRING 20.0f
#define TRAMP 26.0f
#define PROP 8.0f     /* the propeller hat lifts at 8 a frame, the jetpack at 16, for 150 frames */
#define JET 16.0f
#define FLY 150
#define ACCEL 0.6f
#define VMAX 7.0f
#define FEET 20.0f    /* half the doodler's feet */
#define PW 28.0f      /* half a platform (56 x 15) */

/* on screen: half size, the page in the middle */
#define X0 80
#define SX(x) (X0 + (int)((x) * 0.5f))
#define SY(y) (240 - (int)(((y) - sv.cam) * 0.5f))

enum { P_GREEN, P_BLUE, P_BROWN, P_WHITE, P_BOMB };
enum { I_NONE, I_SPRING, I_TRAMP, I_PROP, I_JET };
enum { M_NONE, M_FLYER, M_BLOB, M_HOLE };
#define NP 24
#define NM 3
typedef struct {
  float x, y, t;             /* its middle, its top; how long since it broke, went or started ticking */
  int8_t dir;                /* a blue one's way */
  uint8_t type, item, state; /* state: 0 there, 1 broken / gone / blown up, 2 its spring used */
} plat_t;
typedef struct {
  float x, y, t;
  uint8_t kind, state; /* state: 0 alive, 1 falling away */
} mon_t;

/* doodlejump.sav: the best, and the climb in progress (kept when you leave) */
static struct __attribute__((aligned(4))) { /* whole words, as the sum reads them (a TI-84 CE packs structs) */
  uint8_t magic, version, flags, face; /* flags: 1 a climb to go on with, 2 the guide was read, 4 dead */
  uint32_t sum;                        /* of the words after it: a damaged file starts afresh */
  uint32_t best, score, rng;
  float x, y, vx, vy, cam, top;        /* top: where the next platform goes */
  int32_t fly, item;                   /* frames left flying, and with what */
  int32_t side;                        /* after a monster, the half of the page the next platform goes in */
  plat_t plat[NP];
  mon_t mon[NM];
} sv;
#define SAVE_NAME "doodlejump.sav"

static color_t strip[SCREEN_W * STRIP_H];
static uint32_t now, best0, frame, shoot_t, land_t, over_t;
static float shot_x[3], shot_y[3];
static bool shot_on[3], over, ready, frozen;
static int ov, sel;
enum { OV_NONE, OV_MENU, OV_HELP, OV_OVER };

static uint32_t rnd(void) {
  uint32_t x = sv.rng;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return sv.rng = x;
}
static float frand(float a, float b) { return a + (b - a) * (float)(rnd() & 0xFFFF) / 65535.0f; }
static bool chance(int per1000) { return (int)(rnd() % 1000) < per1000; }
static float fabs_(float v) { return v < 0 ? -v : v; }
static float minf(float a, float b) { return a < b ? a : b; }
static float sin_(float x) { /* (the flying monster's sway: no libm) */
  const float tp = 6.2831853f;
  x -= tp * (float)(int)(x / tp);
  if (x < 0) x += tp;
  if (x > 3.1415927f) return -sin_(x - 3.1415927f);
  if (x > 1.5707964f) x = 3.1415927f - x;
  float x2 = x * x;
  return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42)));
}

static char *num(char *o, uint32_t v) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *label(char *o, const char *s, uint32_t v) {
  char *e = o;
  while (*s) *e++ = *s++;
  num(e, v);
  return o;
}

/* ------------------------------------------------------------ the page */
static uint32_t checksum(void) {
  uint32_t h = 0x811C9DC5u;
  for (const uint32_t *w = &sv.sum + 1; w < (const uint32_t *)(&sv + 1); w++) h = (h ^ *w) * 0x01000193u;
  return h;
}
static void save(void) {
  sv.magic = 'D', sv.version = 1, sv.sum = checksum();
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}

/* the next platform up, at a height the doodler can always reach from the last one he can stand on, and the
   page getting harder with height: further apart, more of them moving, white, about to blow up. Brown ones
   come in between, only looking like help; a monster or a black hole now and then beside the way up, the
   next platform on the other side of the page from it */
static void spawn(plat_t *p) {
  float h = sv.top, gmax = minf(60 + h / 60, 150), gmin = minf(20 + h / 250, 90); /* (a jump is 173) */
  if (h > 300 && chance(150)) { /* a brown one, somewhere below the next real one */
    *p = (plat_t){frand(PW, W - PW), sv.top + frand(15, gmax * 0.8f), 0, 1, P_BROWN, I_NONE, 0};
    return;
  }
  sv.top += frand(gmin, gmax);
  float x = frand(PW, W - PW);
  if (sv.side) x = sv.side > 0 ? frand(W / 2 + 10, W - PW) : frand(PW, W / 2 - 10), sv.side = 0; /* (away from the monster) */
  *p = (plat_t){x, sv.top, 0, (int8_t)(rnd() & 1 ? 1 : -1), P_GREEN, I_NONE, 0};
  int r = (int)(rnd() % 1000), blue = h < 1000 ? 0 : (int)minf((h - 1000) / 22, 400);
  int white = h < 3000 ? 0 : (int)minf(100 + (h - 3000) / 60, 250), bomb = h < 8000 ? 0 : 80;
  if (r < blue) p->type = P_BLUE;
  else if (r < blue + white) p->type = P_WHITE;
  else if (r < blue + white + bomb) p->type = P_BOMB;
  if (p->type == P_GREEN || p->type == P_BLUE) {
    r = (int)(rnd() % 1000);
    if (r < 80) p->item = I_SPRING;
    else if (h > 2000 && r < 100) p->item = I_TRAMP;
    else if (h > 1000 && r < 112) p->item = I_PROP;
    else if (h > 3000 && r < 118) p->item = I_JET;
  }
  if (h > 2500 && chance(h > 8000 ? 60 : 40))
    for (int i = 0; i < NM; i++)
      if (sv.mon[i].kind == M_NONE || sv.mon[i].y < sv.cam - 100) {
        int k = h > 8000 && chance(250) ? M_HOLE : chance(500) ? M_FLYER : M_BLOB;
        bool left = p->x > W / 2; /* on the other half from this platform, the next one on the other half from it */
        sv.mon[i] = (mon_t){left ? frand(45, W / 2 - 45) : frand(W / 2 + 45, W - 45), sv.top + 80, 0, (uint8_t)k, 0};
        sv.side = left ? 1 : -1;
        break;
      }
}

static void new_climb(void) {
  sv.rng = eadk_random() ^ now;
  if (!sv.rng) sv.rng = 1;
  sv.x = W / 2, sv.y = 40, sv.vx = 0, sv.vy = JUMP, sv.cam = 0, sv.score = 0, sv.fly = 0, sv.face = 1, sv.side = 0;
  sv.flags = (uint8_t)((sv.flags & 2) | 1);
  for (int i = 0; i < NM; i++) sv.mon[i].kind = M_NONE;
  sv.plat[0] = (plat_t){W / 2, 40, 0, 1, P_GREEN, I_NONE, 0}; /* the one he starts on */
  sv.top = 40;
  for (int i = 1; i < NP; i++) spawn(&sv.plat[i]);
  for (int i = 0; i < 3; i++) shot_on[i] = false;
  over = false, best0 = sv.best;
}

static void die(void) { sv.flags |= 4, sv.fly = 0; }

static void end_climb(void) {
  over = true, over_t = now, sv.flags &= (uint8_t)~1;
  save();
}

static void shoot(void) {
  for (int i = 0; i < 3; i++)
    if (!shot_on[i]) {
      shot_on[i] = true, shot_x[i] = sv.x, shot_y[i] = sv.y + 50, shoot_t = now;
      return;
    }
}

/* the feet over a platform's top, either side of the page's edge */
static bool over_x(float px, float half) {
  for (int k = -1; k <= 1; k++)
    if (fabs_(sv.x + (float)k * W - px) < half + FEET) return true;
  return false;
}

static void step(int steer) {
  frame++;
  bool dead = sv.flags & 4;
  if (!dead) {
    if (steer) sv.vx += (float)steer * ACCEL, sv.face = (uint8_t)(steer > 0);
    else sv.vx *= 0.85f;
    if (sv.vx > VMAX) sv.vx = VMAX;
    if (sv.vx < -VMAX) sv.vx = -VMAX;
  }
  sv.x += sv.vx;
  if (sv.x < 0) sv.x += W;
  if (sv.x >= W) sv.x -= W;
  float oy = sv.y;
  if (sv.fly > 0) sv.vy = sv.item == I_JET ? JET : PROP, sv.fly--;
  else sv.vy -= G;
  sv.y += sv.vy;
  for (int i = 0; i < NP; i++) {
    plat_t *p = &sv.plat[i];
    if (p->type == P_BLUE) { /* sliding from side to side */
      p->x += p->dir * (1.0f + minf(sv.score / 8000.0f, 1.2f));
      if (p->x < PW || p->x > W - PW) p->dir = (int8_t)-p->dir;
    }
    if (p->state == 1 || p->type == P_BOMB) p->t += 1;
    if (p->type == P_BOMB && p->state == 0 && p->t > 180) p->state = 1, p->t = 0; /* yellow, orange, red, gone */
    if (dead || sv.vy >= 0 || p->state == 1 || oy < p->y || sv.y > p->y || !over_x(p->x, PW)) continue;
    if (p->type == P_BROWN) { /* it breaks, he falls on */
      p->state = 1, p->t = 0;
      continue;
    }
    sv.y = p->y, sv.vy = JUMP, land_t = now;
    if (p->item == I_SPRING && over_x(p->x + 8, 8)) sv.vy = SPRING, p->state = 2;
    if (p->item == I_TRAMP && over_x(p->x, 14)) sv.vy = TRAMP;
    if (p->type == P_WHITE) p->state = 1, p->t = 0;
  }
  /* the hat and the jetpack, picked up on the way */
  for (int i = 0; i < NP; i++) {
    plat_t *p = &sv.plat[i];
    if ((p->item == I_PROP || p->item == I_JET) && p->state == 0 && !dead && sv.fly <= 0 && over_x(p->x, 10) &&
        sv.y < p->y + 24 && sv.y + 50 > p->y)
      sv.item = p->item, sv.fly = FLY, p->item = I_NONE;
  }
  /* monsters: land on them, shoot them, or keep away; a black hole takes him */
  for (int i = 0; i < NM; i++) {
    mon_t *m = &sv.mon[i];
    if (m->kind == M_NONE) continue;
    if (m->state) {
      m->y -= 6;
      continue;
    }
    m->t += 1;
    float mx = m->x + (m->kind == M_FLYER ? 30 * sin_(m->t * 0.05f) : 0), r = m->kind == M_HOLE ? 30 : 32;
    for (int s = 0; s < 3; s++)
      if (shot_on[s] && m->kind != M_HOLE && fabs_(shot_x[s] - mx) < r && fabs_(shot_y[s] - m->y) < r)
        shot_on[s] = false, m->state = 1;
    if (dead || m->state || sv.fly > 0 || !over_x(mx, r - FEET + 4) || sv.y > m->y + r || sv.y + 50 < m->y - r) continue;
    if (m->kind == M_HOLE) {
      die(), sv.x = mx, sv.y = m->y, sv.vy = 0, end_climb();
      return;
    }
    if (sv.vy < 0 && oy > m->y + r * 0.3f) m->state = 1, sv.vy = JUMP; /* landed on top */
    else die(), sv.vy = 0;
  }
  for (int s = 0; s < 3; s++)
    if (shot_on[s] && (shot_y[s] += 15) > sv.cam + VIEW + 40) shot_on[s] = false;
  /* the page follows him up, and new platforms come in at the top */
  if (sv.y > sv.cam + VIEW / 2) sv.cam = sv.y - VIEW / 2;
  if (sv.y > (float)sv.score) sv.score = (uint32_t)sv.y;
  if (sv.score > sv.best) sv.best = sv.score;
  for (int i = 0; i < NP; i++)
    if (sv.plat[i].y < sv.cam - 40) spawn(&sv.plat[i]);
  if (sv.y < sv.cam - 80) end_climb();
}

/* ------------------------------------------------------------ drawing */
#define INK HEX(0x2B2B16)

static void rr(int x, int y, int w, int h, int r, color_t fill, color_t edge) { /* a shape with its outline */
  gfx_rrect(x - 1, y - 1, w + 2, h + 2, r + 1, edge, 256);
  gfx_rrect(x, y, w, h, r, fill, 256);
}

static void paper(void) {
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, HEX(0xE6DFCB));
  gfx_fill(X0, gfx_y0, 160, gfx_y1 - gfx_y0, HEX(0xF7F7F4));
  int off = (int)(sv.cam * 0.5f) % 9;
  for (int y = gfx_y0; y < gfx_y1; y++)
    if ((y + off) % 9 == 0) gfx_fill(X0, y, 160, 1, HEX(0xD8E2EA));
  for (int x = X0 + 4; x < X0 + 160; x += 9) gfx_fill(x, gfx_y0, 1, gfx_y1 - gfx_y0, HEX(0xD8E2EA));
  gfx_fill(X0 - 2, gfx_y0, 2, gfx_y1 - gfx_y0, HEX(0xB8AF98));
  gfx_fill(X0 + 160, gfx_y0, 2, gfx_y1 - gfx_y0, HEX(0xB8AF98));
}

static void platform(const plat_t *p) {
  int x = SX(p->x) - 14, y = SY(p->y);
  if (y < gfx_y0 - 30 || y > gfx_y1 + 8) return;
  static const uint32_t fills[5] = {0x6CBF2E, 0x3EA6E6, 0x8B5A2B, 0xFFFFFF, 0xF2D33A};
  uint32_t f = fills[p->type];
  if (p->type == P_BOMB) f = p->t < 60 ? 0xF2D33A : p->t < 120 ? 0xF29A2E : 0xE0442E;
  if (p->state == 1) { /* broken in two and falling, gone, or blown to bits */
    if (p->t > 30 || p->type != P_BROWN) return;
    int d = (int)(p->t * 0.8f);
    rr(x - d / 3, y + d, 13, 7, 3, gfx_rgb(f), INK), rr(x + 15 + d / 3, y + d, 13, 7, 3, gfx_rgb(f), INK);
    return;
  }
  rr(x, y, 28, 7, 3, gfx_rgb(f), p->type == P_WHITE ? HEX(0x8E8E8E) : INK);
  gfx_fill(x + 3, y + 1, 22, 1, gfx_rgb(gfx_lerp888(f, 0xFFFFFF, 110)));
  if (p->type == P_BROWN) gfx_fill(x + 13, y, 2, 7, INK);
  /* what's on it */
  int cx = SX(p->x);
  if (p->item == I_SPRING) {
    int h = p->state == 2 ? 7 : 4, sx = cx + 2;
    for (int k = 0; k < h; k += 2) gfx_fill(sx, y - 2 - k, 6, 1, HEX(0x5A5A5A));
    gfx_fill(sx - 1, y - h - 2, 8, 2, HEX(0x8A8A8A));
  } else if (p->item == I_TRAMP) {
    gfx_fill(cx - 9, y - 3, 1, 3, INK), gfx_fill(cx + 8, y - 3, 1, 3, INK);
    rr(cx - 10, y - 5, 20, 3, 1, HEX(0xE33A2E), INK);
  } else if (p->item == I_PROP) {
    rr(cx - 5, y - 6, 10, 5, 3, HEX(0xF28A1E), INK);
    gfx_fill(cx - 6 + (int)(frame / 3 % 3) * 2, y - 8, 8, 1, INK);
  } else if (p->item == I_JET) {
    rr(cx - 5, y - 11, 4, 10, 2, HEX(0xB0BEC5), INK), rr(cx + 1, y - 11, 4, 10, 2, HEX(0xB0BEC5), INK);
  }
}

static void monster(const mon_t *m) {
  int x = SX(m->x + (m->kind == M_FLYER && !m->state ? 30 * sin_(m->t * 0.05f) : 0)), y = SY(m->y);
  if (y < gfx_y0 - 24 || y > gfx_y1 + 24) return;
  /* across the page's edge, drawn at both sides */
  for (int k = -1; k <= 1; k++) {
    int cx = x + k * 160;
    if (cx < X0 - 20 || cx > X0 + 180) continue;
    if (m->kind == M_HOLE) {
      gfx_circle(cx, y, 15, HEX(0xBDBDBD), 256), gfx_circle(cx, y, 13, HEX(0x111111), 256);
    } else if (m->kind == M_FLYER) { /* the blue one with striped wings */
      int flap = (int)(m->t / 6) % 2 * 3;
      for (int s = -1; s <= 1; s += 2) {
        rr(cx + s * 14 - 7, y - 6 + flap, 14, 9 - flap, 4, HEX(0x2E5FA8), INK);
        gfx_fill(cx + s * 14 - 3, y - 5 + flap, 1, 7 - flap, HEX(0x7FA8E0));
        gfx_fill(cx + s * 14 + 2, y - 5 + flap, 1, 7 - flap, HEX(0x7FA8E0));
      }
      gfx_circle(cx, y, 10, INK, 256), gfx_circle(cx, y, 9, HEX(0x4D8FD8), 256);
      gfx_circle(cx - 3, y - 2, 3, 0xFFFF, 256), gfx_circle(cx + 3, y - 2, 3, 0xFFFF, 256);
      gfx_fill(cx - 3, y - 2, 2, 2, INK), gfx_fill(cx + 3, y - 2, 2, 2, INK);
      gfx_fill(cx - 4, y + 5, 2, 3, 0xFFFF), gfx_fill(cx + 3, y + 5, 2, 3, 0xFFFF);
    } else { /* the green one with three eyes */
      gfx_rrect(cx - 17, y - 11, 34, 22, 11, INK, 256);
      gfx_rrect(cx - 16, y - 10, 32, 20, 10, HEX(0x8BC34A), 256);
      gfx_fill(cx - 16, y + 4, 32, 7, HEX(0xE6DFCB) == 0 ? 0 : HEX(0x8BC34A));
      for (int e = -1; e <= 1; e++) gfx_circle(cx + e * 8, y - 3, 3, HEX(0xFFE04A), 256), gfx_fill(cx + e * 8, y - 3, 2, 2, INK);
      for (int t = -2; t <= 2; t++) gfx_fill(cx + t * 5, y + 5, 3, 3, 0xFFFF);
      for (int l = -3; l <= 3; l += 2) gfx_fill(cx + l * 4, y + 11, 2, 3, HEX(0xE2533A));
    }
  }
}

/* the doodler, facing his way: a yellow-green body, striped green pants, four legs, his nose; looking up
   with his nose up when he shoots, legs bent just after a landing */
static void doodler(int fx, int fy) {
  bool up = now - shoot_t < 300, dead = sv.flags & 4, bent = now - land_t < 120 && !up;
  int s = sv.face ? 1 : -1, by = fy - (bent ? 2 : 0);
  color_t body = HEX(0xD3DB46);
  if (sv.fly > 0 && sv.item == I_JET) { /* the jetpack on his back, firing */
    rr(fx - s * 13 - 2, by - 24, 5, 12, 2, HEX(0xB0BEC5), INK);
    gfx_circle(fx - s * 11, by - 9 + (int)(frame % 3), 3, HEX(0xFF8A1E), 256);
  }
  for (int l = 0; l < 4; l++) { /* legs and feet */
    int lx = fx - 8 + l * 5;
    gfx_fill(lx, by - 6, 1, bent ? 3 : 5, INK);
    gfx_fill(s > 0 ? lx : lx - 2, by - (bent ? 3 : 1), 3, 1, INK);
  }
  gfx_circle(fx, by - 18, 12, INK, 256), gfx_fill(fx - 12, by - 18, 24, 12, INK);
  gfx_circle(fx, by - 18, 11, body, 256), gfx_fill(fx - 11, by - 18, 22, 11, body);
  for (int k = 0; k < 3; k++) gfx_fill(fx - 11, by - 12 + k * 2, 22, 1, k & 1 ? HEX(0x56A332) : HEX(0x2F6B1F));
  gfx_fill(fx - 11, by - 7, 22, 1, INK);
  if (up) { /* nose up */
    rr(fx - 2, by - 36, 5, 7, 2, body, INK);
    gfx_fill(fx - 4, by - 24, 2, 2, INK), gfx_fill(fx + 3, by - 24, 2, 2, INK);
  } else {
    rr(s > 0 ? fx + 8 : fx - 16, by - 22, 8, 5, 2, body, INK);
    gfx_fill(fx + s * 2, by - 22, 2, 2, INK), gfx_fill(fx + s * 6, by - 22, 2, 2, INK);
  }
  if (sv.fly > 0 && sv.item == I_PROP) { /* the hat, spinning */
    rr(fx - 6, by - 33, 12, 5, 3, HEX(0xF28A1E), INK);
    int w = 4 + (int)(frame % 4) * 3;
    gfx_fill(fx - w / 2, by - 35, w, 1, INK);
  }
  if (dead) /* seeing stars */
    for (int k = 0; k < 3; k++) gfx_circle(fx - 8 + k * 8, by - 32 + (int)((frame / 5 + (uint32_t)k) % 2) * 2, 2, HEX(0xF2D33A), 256);
}

static void button(int y, const char *s, bool on) {
  rr(X0 + 20, y, 120, 22, 6, on ? HEX(0x6CBF2E) : HEX(0xF7F7F4), INK);
  gfx_text_center(&np_font_body, 160, y + 16, s, INK, 256);
}
static const char *const menu_items[] = {T("resume"), T("new game"), T("how to play"), T("quit")};
static const char *const over_items[] = {T("play again"), T("quit")};

static void scene(void *ctx) {
  char s[24];
  paper();
  for (int i = 0; i < NP; i++) platform(&sv.plat[i]);
  for (int i = 0; i < NM; i++)
    if (sv.mon[i].kind) monster(&sv.mon[i]);
  for (int i = 0; i < 3; i++)
    if (shot_on[i]) gfx_circle(SX(shot_x[i]), SY(shot_y[i]), 2, INK, 256);
  /* the doodler, and his other half across the page's edge */
  int fx = SX(sv.x), fy = SY(sv.y);
  doodler(fx, fy);
  if (fx < X0 + 20) doodler(fx + 160, fy);
  if (fx > X0 + 140) doodler(fx - 160, fy);
  gfx_fill(0, gfx_y0, X0 - 2, gfx_y1 - gfx_y0, HEX(0xE6DFCB)); /* (what went past the page's edge) */
  gfx_fill(X0 + 162, gfx_y0, SCREEN_W - X0 - 162, gfx_y1 - gfx_y0, HEX(0xE6DFCB));
  /* the score on the page, the best and the keys beside it */
  gfx_fill_alpha(X0, 0, 160, 20, HEX(0xF7F7F4), 200);
  num(s, sv.score);
  gfx_text(&np_font_title, X0 + 5, 18, s, INK, 256);
  gfx_text_center(&np_font_small, 40, 20, T("best"), HEX(0x6B6449), 256);
  gfx_text_center(&np_font_body, 40, 36, num(s, sv.best) ? s : s, INK, 256);
  static const char *const keys[][2] = {{T("Left Right"), T("move")}, {T("OK"), T("shoot")}, {T("Back"), T("menu")}};
  for (int i = 0; i < 3; i++) {
    gfx_text_center(&np_font_body, 280, 176 + 20 * i, keys[i][0], INK, 256);
    gfx_text_center(&np_font_small, 280, 186 + 20 * i, keys[i][1], HEX(0x6B6449), 256);
  }
  if ((ready || frozen) && !ov) {
    gfx_text_center(&np_font_title, 160, 96, ready ? "doodle jump" : T("paused"), INK, 256);
    gfx_text_center(&np_font_body, 160, 118, ready ? T("Press OK to play") : T("Press OK to go on"), INK, 256);
  }
  if (ov) {
    gfx_fill_alpha(X0, 0, 160, SCREEN_H, HEX(0xF7F7F4), 230);
    if (ov == OV_MENU) {
      gfx_text_center(&np_font_title, 160, 60, T("paused"), INK, 256);
      for (int i = 0; i < 4; i++) button(76 + 30 * i, menu_items[i], sel == i);
    } else if (ov == OV_OVER) {
      gfx_text_center(&np_font_title, 160, 60, T("game over!"), INK, 256);
      gfx_text_center(&np_font_body, 160, 88, label(s, T("your score: "), sv.score), INK, 256);
      gfx_text_center(&np_font_body, 160, 106, label(s, T("your high score: "), sv.best), INK, 256);
      if (sv.score > best0) gfx_text_center(&np_font_small, 160, 122, T("a new high score!"), HEX(0x2F6B1F), 256);
      for (int i = 0; i < 2; i++) button(136 + 30 * i, over_items[i], sel == i);
    } else {
      gfx_text_center(&np_font_title, 160, 46, T("how to play"), INK, 256);
      gfx_paragraph(&np_font_small, 160, 66, 148, 13,
                    T("He jumps by himself when he lands. Steer him from platform to platform, as high as you can. Off one side of the page, he comes back on the other. Shoot monsters or jump on their heads, and keep away from black holes. Falling off the bottom ends the climb."),
                    INK, 256);
      button(196, T("play"), true);
    }
  }
}

/* ------------------------------------------------------------ the game */
static void load(void) {
  uint32_t len = 0;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  uint8_t *v = (uint8_t *)&sv;
  bool ok = d && len == sizeof sv && d[0] == 'D' && d[1] == 1;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = ok ? d[i] : 0;
  if (sv.sum != checksum())
    for (uint32_t i = 0; i < sizeof sv; i++) v[i] = 0;
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  now = (uint32_t)eadk_timing_millis();
  load();
  if (sv.flags & 1) frozen = true, best0 = sv.best;
  else new_climb(), ready = true;
  if (!(sv.flags & 2)) ov = OV_HELP, sv.flags |= 2;
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the launcher wait to be let go */
  uint32_t tick = now;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint64_t raw = eadk_keyboard_scan(), k = raw & ~(stale &= raw), hit = k & ~held;
    held = k;
    if (raw & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe));
    int dy = !!(hit & KEY(eadk_key_down)) - !!(hit & KEY(eadk_key_up));
    int steer = !!(k & (KEY(eadk_key_right) | KEY(eadk_key_six))) - !!(k & (KEY(eadk_key_left) | KEY(eadk_key_four)));
    if (over && !ov && now - over_t > 700) ov = OV_OVER, sel = 0;
    if (ov) {
      int n = ov == OV_MENU ? 4 : ov == OV_OVER ? 2 : 1;
      sel = (sel + dy + n) % n;
      if (hit & KEY(eadk_key_back) && ov != OV_OVER) ov = OV_NONE;
      if (ok) {
        int choice = ov == OV_OVER ? (sel ? 3 : 1) : ov == OV_MENU ? sel : 0;
        ov = OV_NONE;
        if (choice == 1) new_climb(), ready = true, frozen = false;
        else if (choice == 2) ov = OV_HELP;
        else if (choice == 3) break;
      }
    } else if (hit & KEY(eadk_key_back)) {
      ov = OV_MENU, sel = 0;
      if (!over && !ready) frozen = true;
      save();
    } else if (ready || frozen) {
      if (ok) ready = frozen = false;
    } else if (!over && (ok || (hit & KEY(eadk_key_up))) && !(sv.flags & 4) && sv.fly <= 0) {
      shoot();
    }
    /* sixty frames a second, as the original, catching up after a slow one; on the first page he bounces
       in place until OK */
    if (ov || frozen || over) tick = now;
    if (now - tick > 100) tick = now - 100;
    for (; now - tick >= 17; tick += 17) step(ready ? 0 : steer);
    eadk_display_wait_for_vblank();
    gfx_render(scene, 0, 0, SCREEN_H);
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
