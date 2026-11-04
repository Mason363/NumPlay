/* Suika Game: drop fruit in the box; two of a kind that touch join into the next one, up to the watermelon.
 *
 * Aladdin X's Suika Game as it plays on the Nintendo Switch. The cloud carries a fruit along the top of the
 * box and OK drops it; the next one comes the moment it touches something. Fruit fall, roll and stack as
 * the original's physics makes them (Unity's Box2D at 50 steps a second: no bounce, friction 0.4, mass by
 * area, still fruit asleep), two of a kind that touch become the next fruit where they met, two
 * watermelons vanish, and a fruit that stays above the box for about a second ends the game. The sizes,
 * colours, speeds and timings were measured on the Switch version (gamesrc/suika/spec): the physics runs
 * in its 720p pixels, drawn at a third of that.
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and saves
 * through its file functions; on its own (SuikaGame.nwa) it brings them along.
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
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Suika Game";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

#define KEY(k) (1ull << (k))
#define INK HEX(0x7A4A1E)

/* ------------------------------------------------------------ the original's numbers (its 720p pixels, seconds) */
#define W 448.0f      /* the box inside, wall to wall */
#define FLOOR 564.0f  /* its floor, below the line at its top (y = 0) */
#define HANG -16.5f   /* where the cloud holds a fruit's centre */
#define GRAV 1450.0f  /* gravity, px/s^2 */
#define DT 0.02f      /* Unity's physics step */
#define MU 0.4f       /* friction (Unity's default material, bounciness 0) */
#define CLOUD 5.8f    /* the cloud's move in a step: 290 px/s */
#define OVER_STEPS 47 /* above the line this long (0.94 s) and the game is over */
#define SLOP 0.75f    /* Box2D's linear slop, the overlap resting fruit keep */
#define BETA 0.4f     /* position correction per iteration */
#define STILL 3.0f    /* slower than this (px/s) for half a second: asleep */
/* the fruit: cherry, strawberry, grape, dekopon, persimmon, apple, pear, peach, pineapple, melon, watermelon;
   and 0, the cloud's colours */
static const uint16_t diam[12] = {0, 33, 44, 61, 70, 89, 113, 130, 158, 178, 219, 259};
#define RAD(k) (diam[k] * 0.5f)
static const color_t fill_c[12] = {HEX(0xFFF0A0), HEX(0xEE0A0A), HEX(0xFB5E4A), HEX(0xA86AFC), HEX(0xFFB000), HEX(0xFC8414),
                                   HEX(0xF01818), HEX(0xFFF07A), HEX(0xFFC0BA), HEX(0xF6E21C), HEX(0x98D418), HEX(0x22A412)};
static const color_t line_c[12] = {HEX(0xF0C45A), HEX(0xA80406), HEX(0xE01E1A), HEX(0x6A1CE6), HEX(0xE07C06), HEX(0xEE5E0C),
                                   HEX(0xC00808), HEX(0xEAB824), HEX(0xF0786A), HEX(0xE2A60E), HEX(0x4E9C12), HEX(0x0A4A04)};
static const color_t face_c[12] = {HEX(0x5A3A2A), HEX(0x6A0000), HEX(0x8A1410), HEX(0x3C0A90), HEX(0xFFF3C4), HEX(0xA0300C),
                                   HEX(0x7A0606), HEX(0x8E6418), HEX(0xC21C1C), HEX(0x8E5210), HEX(0x2E6A0E), HEX(0x042A00)};

/* on screen: a third of the original, the box's inside from x = 85.3 to 234.7, its top line at y = 36.7 */
#define SX(x) (85.333f + (x) * (1 / 3.0f))
#define SY(y) (36.667f + (y) * (1 / 3.0f))

#define MAXF 64
#define MAXC 256
#define WALL MAXF /* f[WALL], f[WALL + 1], f[WALL + 2]: the walls and the floor, as huge circles that never move */
#define BIG 65536.0f
typedef struct {
  float x, y, vx, vy, c, s, w; /* where, how fast, turned how far (cos, sin) and how fast */
  uint8_t k, land, over, still, zz; /* kind, has touched something, steps above the line, still, asleep */
} fruit_t;
typedef struct {
  uint8_t a, b;
  float nx, ny, pn, pt; /* from a to b; the impulses so far (kept for the next step) */
} contact_t;
static fruit_t f[WALL + 3];
static float rad[WALL + 3], inv[WALL + 3]; /* radius and 1 / mass (by area), for this step */
static contact_t con[2][MAXC];
static int ncon[2], cb;

/* suika.sav: the best, and the game in progress */
enum { GAME = 1, SEEN = 2, HELD = 4 };
typedef struct {
  int16_t v[7]; /* x, y, vx, vy, c, s, w, times scale[] */
  uint8_t k, land;
} packed_t;
static struct {
  uint8_t magic, version, flags, cur; /* cur: the fruit in (or coming to) the cloud */
  uint8_t next, drop, pad[2];         /* drop: the falling fruit + 1 */
  uint32_t sum;                       /* of the words after it: a damaged file starts afresh */
  uint32_t best, score, rng;
  float aim;
  packed_t p[MAXF];
} sv;
#define SAVE_NAME "suika.sav"
static const float scale[7] = {8, 8, 4, 4, 16384, 16384, 64};

static color_t strip[SCREEN_W * STRIP_H];
static uint32_t now, over_t, best0;
static bool over;
static int ov, sel;
enum { OV_NONE, OV_MENU, OV_SURE, OV_HELP, OV_OVER };

static float sqrt_(float v) { return __builtin_sqrtf(v); }
#define fminf_ __builtin_fminf
#define fmaxf_ __builtin_fmaxf
static float clampf(float v, float a, float b) { return fminf_(fmaxf_(v, a), b); }
static float frac(float v) { return v - (float)(int)v; }
static uint32_t rnd(void) {
  uint32_t x = sv.rng;
  x ^= x << 13, x ^= x >> 17, x ^= x << 5;
  return sv.rng = x;
}
static uint8_t drop_kind(void) { return (uint8_t)(rnd() % 5 + 1); }
static char *num(char *o, uint32_t v) {
  char t[10];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}

/* ------------------------------------------------------------ the rules */
static uint32_t checksum(void) {
  uint32_t h = 0x811C9DC5u;
  for (const uint32_t *w = &sv.sum + 1; w < (const uint32_t *)(&sv + 1); w++) h = (h ^ *w) * 0x01000193u;
  return h;
}
static void unpack(void) {
  for (int i = 0; i < MAXF; i++) {
    float *a = &f[i].x;
    for (int j = 0; j < 7; j++) a[j] = sv.p[i].v[j] / scale[j];
    f[i].k = sv.p[i].k > 11 ? 0 : sv.p[i].k, f[i].land = sv.p[i].land;
  }
}
static void save(void) {
  for (int i = 0; i < MAXF; i++) {
    float *a = &f[i].x;
    for (int j = 0; j < 7; j++) sv.p[i].v[j] = (int16_t)clampf(a[j] * scale[j], -32767, 32767);
    sv.p[i].k = f[i].k, sv.p[i].land = f[i].land;
  }
  unpack(); /* (what goes on is what was saved) */
  sv.magic = 'S', sv.version = 1, sv.sum = checksum();
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}

static void aim_at(float x) { sv.aim = clampf(x, RAD(sv.cur), W - RAD(sv.cur)); }

static void new_game(void) {
  for (int i = 0; i < MAXF; i++) f[i].k = 0;
  if (!sv.rng) sv.rng = 0x2545F491u;
  sv.score = 0, sv.drop = 0, sv.flags |= GAME | HELD;
  sv.cur = drop_kind(), sv.next = drop_kind();
  aim_at(W / 2);
  over = false, best0 = sv.best;
}

/* the dropped fruit touched something: the next one comes to the cloud */
static void landed(void) {
  f[sv.drop - 1].land = 1;
  sv.drop = 0, sv.flags |= HELD;
  sv.cur = sv.next, sv.next = drop_kind();
  aim_at(sv.aim);
}

static void drop_fruit(void) {
  if (!(sv.flags & HELD) || over) return;
  for (int i = 0; i < MAXF; i++)
    if (!f[i].k) {
      f[i] = (fruit_t){sv.aim, HANG, 0, 0, 1, 0, 0, sv.cur, 0, 0, 0, 0};
      sv.drop = (uint8_t)(i + 1), sv.flags &= (uint8_t)~HELD;
      return;
    }
}

/* b, for a contact with a: a wall or the floor is moved to face a */
static fruit_t *body(int a, int b) {
  fruit_t *p = &f[b];
  int w = b - WALL;
  if (w >= 0) p->k = 12, p->zz = 1, p->x = w == 2 ? f[a].x : w ? W + BIG : -BIG, p->y = w == 2 ? FLOOR + BIG : f[a].y;
  return p;
}

static void radius(int i) { rad[i] = i < WALL ? RAD(f[i].k) : BIG, inv[i] = i < WALL && f[i].k ? 1 / (rad[i] * rad[i]) : 0; }

/* two of a kind touched: the next fruit where they met, at rest; two watermelons both go */
static void merge(int i, int j) {
  int k = f[i].k;
  sv.score += (uint32_t)(k * (k + 1) / 2); /* 1, 3, 6 ... 55, and 66 for two watermelons */
  if (sv.score > sv.best) sv.best = sv.score;
  if (sv.drop - 1 == i || sv.drop - 1 == j) landed();
  f[j].k = 0;
  f[i] = (fruit_t){(f[i].x + f[j].x) / 2, (f[i].y + f[j].y) / 2, 0, 0, 1, 0, 0, (uint8_t)(k < 11 ? k + 1 : 0), 1, 0, 0, 0};
  radius(i), radius(j);
}

/* the contacts: every fruit against those after it, then the walls and the floor (so they come out in
   order, to find last step's impulses); two of a kind merge instead */
static void collide(void) {
  contact_t *c = con[cb ^ 1], *o = con[cb];
  int n = 0, on = ncon[cb], oi = 0, d = sv.drop - 1;
  bool touched = false;
  uint64_t merged = 0;
  for (int i = 0; i < MAXF; i++)
    for (int j = i + 1; j < WALL + 3 && f[i].k; j++) {
      fruit_t *a = &f[i], *b = body(i, j);
      if (!b->k) continue;
      float rs = rad[i] + rad[j], dx = b->x - a->x, dy = b->y - a->y;
      if (dx > rs || dx < -rs || dy > rs || dy < -rs) continue;
      float d2 = dx * dx + dy * dy, l = sqrt_(d2);
      if (d2 >= rs * rs) continue;
      if (a->k == b->k && !((merged >> i | merged >> j) & 1)) {
        merged |= 1ull << i | 1ull << j;
        merge(i, j);
        continue;
      }
      touched |= i == d || j == d;
      int key = i << 7 | j;
      while (oi < on && (o[oi].a << 7 | o[oi].b) < key) oi++;
      bool kept = oi < on && (o[oi].a << 7 | o[oi].b) == key;
      if (n < MAXC) c[n++] = (contact_t){(uint8_t)i, (uint8_t)j, l > 1e-3f ? dx / l : 0, l > 1e-3f ? dy / l : 1, kept ? o[oi].pn : 0, kept ? o[oi].pt : 0};
    }
  if (touched && sv.drop) landed();
  cb ^= 1, ncon[cb] = n;
}

/* an impulse along the normal (pn) and the tangent (pt), on b and back on a; the tangent one turns them */
static void impulse(contact_t *c, float pn, float pt) {
  fruit_t *a = &f[c->a], *b = &f[c->b];
  float px = pn * c->nx - pt * c->ny, py = pn * c->ny + pt * c->nx, ia = inv[c->a], ib = inv[c->b];
  a->vx -= px * ia, a->vy -= py * ia, a->w -= pt * 2 * ia / rad[c->a];
  b->vx += px * ib, b->vy += py * ib, b->w -= pt * 2 * ib / rad[c->b];
}

static bool live(const contact_t *c) { return f[c->a].k && f[c->b].k && !(f[c->a].zz && f[c->b].zz); }

static uint8_t root(uint8_t *up, int i) {
  while (up[i] != i) i = up[i] = up[up[i]];
  return (uint8_t)i;
}

/* a fiftieth of a second, as Box2D steps: forces, contacts, 8 velocity and 3 position iterations, then
   islands of still fruit go to sleep */
static void step(void) {
  if (over) return;
  for (int i = 0; i < WALL + 3; i++) radius(i);
  collide();
  contact_t *cs = con[cb];
  int n = ncon[cb];
  uint8_t up[MAXF], least[MAXF], awake[MAXF];
  /* islands: fruit touching each other; one awake wakes them all */
  for (int i = 0; i < MAXF; i++) up[i] = (uint8_t)i, least[i] = 255, awake[i] = 0;
  for (int i = 0; i < n; i++)
    if (cs[i].b < WALL && f[cs[i].a].k && f[cs[i].b].k) up[root(up, cs[i].a)] = root(up, cs[i].b);
  for (int i = 0; i < MAXF; i++) awake[root(up, i)] |= f[i].k && !f[i].zz;
  for (int i = 0; i < MAXF; i++) {
    fruit_t *a = &f[i];
    if (!a->k || !awake[root(up, i)]) continue;
    if (a->zz) a->zz = a->still = 0;
    a->vy += GRAV * DT, a->w *= 0.999f; /* Unity's angular drag, 0.05 */
  }
  for (int it = -1; it < 8; it++)
    for (int i = 0; i < n; i++) {
      contact_t *c = &cs[i];
      if (!live(c)) continue;
      fruit_t *a = &f[c->a], *b = &f[c->b];
      float m = inv[c->a] + inv[c->b], vn;
      if (it < 0) { /* last step's impulses again */
        impulse(c, c->pn, c->pt);
        continue;
      }
      /* friction first, up to MU times the push */
      float vt = (b->vy - a->vy) * c->nx - (b->vx - a->vx) * c->ny - a->w * rad[c->a] - b->w * rad[c->b];
      float lim = MU * c->pn, pt = clampf(c->pt - vt / (3 * m), -lim, lim);
      impulse(c, 0, pt - c->pt), c->pt = pt;
      vn = (b->vx - a->vx) * c->nx + (b->vy - a->vy) * c->ny;
      float pn = c->pn - vn / m;
      if (pn < 0) pn = 0;
      impulse(c, pn - c->pn, 0), c->pn = pn;
    }
  for (int i = 0; i < MAXF; i++) {
    fruit_t *a = &f[i];
    if (!a->k || a->zz) continue;
    a->x += a->vx * DT, a->y += a->vy * DT;
    float t = a->w * DT, c = a->c - a->s * t, s = a->s + a->c * t, m = 1 / sqrt_(c * c + s * s);
    a->c = c * m, a->s = s * m;
  }
  for (int it = 0; it < 3; it++)
    for (int i = 0; i < n; i++) {
      contact_t *c = &cs[i];
      if (!live(c)) continue;
      fruit_t *a = &f[c->a], *b = body(c->a, c->b);
      float dx = b->x - a->x, dy = b->y - a->y, d = sqrt_(dx * dx + dy * dy), nx = c->nx, ny = c->ny;
      float ia = inv[c->a], ib = inv[c->b], corr = BETA * (d - rad[c->a] - rad[c->b] + SLOP);
      if (d > 1e-3f) nx = dx / d, ny = dy / d;
      if (corr >= 0) continue;
      float p = -clampf(corr, -20, 0) / (ia + ib);
      a->x -= nx * p * ia, a->y -= ny * p * ia, b->x += nx * p * ib, b->y += ny * p * ib;
    }
  /* still for half a second, with all it touches: asleep */
  for (int i = 0; i < MAXF; i++) {
    fruit_t *a = &f[i];
    float r = rad[i];
    a->x = clampf(a->x, r - SLOP, W - r + SLOP), a->y = clampf(a->y, -2000, FLOOR - r + SLOP);
    bool slow = a->vx * a->vx + a->vy * a->vy < STILL * STILL && a->w * a->w * r * r < STILL * STILL;
    a->still = a->zz ? 255 : slow ? (uint8_t)(a->still < 254 ? a->still + 1 : 254) : 0;
    if (a->k && a->still < least[root(up, i)]) least[root(up, i)] = a->still;
  }
  for (int i = 0; i < MAXF; i++) {
    fruit_t *a = &f[i];
    if (!a->k) continue;
    if (!a->zz && least[root(up, i)] >= 25) a->zz = 1, a->vx = a->vy = a->w = 0;
    if (a->land && a->y < rad[i]) { /* landed and above the line too long: the end */
      if (++a->over >= OVER_STEPS) over = true, over_t = now, sv.flags &= (uint8_t)~GAME;
    } else
      a->over = 0;
  }
  if (over) save();
}

static void sim(int dir) { /* a step of the game: the cloud, then the fruit */
  aim_at(sv.aim + (float)dir * CLOUD);
  step();
}

/* ------------------------------------------------------------ drawing */
static void put(color_t *p, color_t c, int t) { *p = t >= 32 ? c : gfx_mix(*p, c, t); }
/* a line with round ends, ro thick on either side (a disc when it has no length), hollow within ri; t of 32 */
static void seg(float x0, float y0, float x1, float y1, float ro, float ri, color_t c, int t) {
  int ya = (int)fmaxf_(fminf_(y0, y1) - ro, gfx_y0), yb = (int)fminf_(fmaxf_(y0, y1) + ro + 1, gfx_y1);
  int xa = (int)fmaxf_(fminf_(x0, x1) - ro, 0), xb = (int)fminf_(fmaxf_(x0, x1) + ro + 1, SCREEN_W);
  float dx = x1 - x0, dy = y1 - y0, l2 = dx * dx + dy * dy;
  for (int y = ya; y < yb; y++)
    for (int x = xa; x < xb; x++) {
      float qx = x + 0.5f - x0, qy = y + 0.5f - y0, u = l2 > 0 ? clampf((qx * dx + qy * dy) / l2, 0, 1) : 0;
      qx -= u * dx, qy -= u * dy;
      float d = sqrt_(qx * qx + qy * qy), cov = clampf(ro + 0.5f - d, 0, 1), hole = clampf(d - ri + 0.5f, 0, 1);
      if (ri > 0 && hole < cov) cov = hole;
      if (cov > 0) put(gfx_buf + (y - gfx_y0) * SCREEN_W + x, c, (int)(cov * t));
    }
}
#define DISC(x, y, r, c, t) seg(x, y, x, y, r, 0, c, t)

/* the faces and the bits that stick out, drawn on the fruit in its own frame (a 32nd of its radius, y down):
   each a dot, or with CONT a line on from the one before; thickness in 64ths of the radius; colours:
   0 shine, 1 the face, 2 leaf, 3 dark leaf, 4 stem, 5 seed, 6 the fruit's outline, 7 blush. */
typedef struct {
  int8_t x, y;
  uint8_t tc;
} stroke_t;
#define S(x, y, t, c) {x, y, (uint8_t)((t) | (c) << 4)}
#define C(x, y, t, c) {x, y, (uint8_t)((t) | (c) << 4 | 0x80)}
static const stroke_t strokes[] = {
  /* the cloud */
  S(-8, 2, 7, 1), S(10, 2, 7, 1), S(-2, 6, 4, 1), C(0, 8, 4, 1), C(3, 6, 4, 1), S(-15, 6, 14, 7), S(17, 6, 14, 7), S(19, 0, 15, 0),
  /* cherry */
  S(2, -26, 7, 4), C(8, -52, 7, 4), S(14, -50, 14, 2), C(22, -46, 12, 2), S(-12, -12, 15, 0), S(-10, 4, 10, 1), S(10, 4, 10, 1),
  /* strawberry */
  S(-16, -30, 12, 2), C(0, -24, 12, 2), C(16, -30, 12, 2), S(0, -34, 10, 2), C(0, -24, 10, 2), S(-17, -6, 7, 5), S(15, -9, 7, 5),
  S(-5, 15, 7, 5), S(17, 13, 7, 5), S(-19, 17, 7, 5), S(3, -12, 6, 5), S(-10, 1, 9, 1), S(10, 1, 9, 1), S(-4, 8, 5, 1), C(0, 11, 5, 1),
  C(4, 8, 5, 1),
  /* grape (its bunch drawn with the berries below) */
  S(-24, -28, 15, 2), C(-13, -22, 12, 2), S(-8, -2, 8, 1), S(8, -2, 8, 1), S(0, 7, 8, 1),
  /* dekopon */
  S(-10, -2, 9, 1), S(10, -2, 9, 1), S(0, 8, 9, 1), S(-15, -16, 8, 0),
  /* persimmon */
  S(-14, -34, 10, 2), C(0, -28, 12, 2), C(14, -34, 10, 2), S(-8, -22, 9, 2), C(0, -28, 9, 2), C(8, -22, 9, 2), S(16, -14, 10, 0),
  S(-11, 0, 7, 1), S(11, 0, 7, 1), S(-3, 6, 4, 1), C(0, 9, 4, 1), C(3, 6, 4, 1),
  /* apple */
  S(1, -29, 6, 4), C(5, -40, 6, 4), S(-16, -16, 11, 0), S(-21, -5, 5, 0), S(-10, -2, 7, 1), S(10, -2, 7, 1), S(0, 6, 8, 1), S(0, 5, 3, 0),
  /* pear */
  S(0, -31, 5, 4), C(4, -40, 5, 4), S(-14, 12, 3, 6), S(18, -12, 3, 6), S(11, 19, 3, 6), S(-21, -9, 3, 6), S(20, 16, 9, 0),
  S(-14, -2, 7, 1), S(14, -2, 7, 1), S(-6, 4, 3, 1), C(-3, 7, 3, 1), C(0, 4, 3, 1), C(3, 7, 3, 1), C(6, 4, 3, 1),
  /* peach */
  S(-3, -31, 3, 6), C(-9, -19, 3, 6), S(-13, -1, 7, 1), S(13, -1, 7, 1), S(-5, 3, 5, 1), C(0, 8, 5, 1), C(5, 3, 5, 1), S(0, 5, 3, 0),
  /* pineapple */
  S(-14, -40, 9, 2), C(0, -29, 9, 2), C(-5, -44, 9, 2), C(0, -29, 9, 2), C(5, -44, 9, 2), C(0, -29, 9, 2), C(14, -40, 9, 2),
  S(-16, -18, 8, 0), S(-14, 0, 4, 1), C(-10, -4, 4, 1), C(-6, 0, 4, 1), S(6, 0, 4, 1), C(10, -4, 4, 1), C(14, 0, 4, 1), S(-4, 6, 5, 1),
  C(0, 10, 5, 1), C(4, 6, 5, 1), S(0, 7, 3, 0),
  /* melon */
  S(12, -30, 5, 3), C(18, -38, 5, 3), C(24, -36, 5, 3), S(20, 22, 6, 0), S(-10, -2, 6, 1), S(10, -2, 6, 1), S(-4, 6, 3, 1),
  C(-2, 8, 3, 1), C(0, 6, 3, 1), C(2, 8, 3, 1), C(4, 6, 3, 1),
  /* watermelon */
  S(-14, -6, 4, 1), C(-8, -2, 4, 1), C(-14, 2, 4, 1), S(10, -2, 8, 1), S(-4, 4, 4, 1), C(0, 9, 4, 1), C(4, 4, 4, 1), S(16, -22, 9, 0),
  S(22, -14, 4, 0),
};
#undef S
#undef C
static const uint8_t first[13] = {0, 8, 15, 31, 36, 40, 52, 60, 74, 82, 100, 111, 120};

/* the strokes of kind k (0: the cloud) at (cx, cy), r its radius, turned (c, s) */
static void decor(int k, float cx, float cy, float r, float c, float s) {
  static const color_t pal[8] = {0xFFFF, 0, HEX(0x6CC530), HEX(0x2C7A1A), HEX(0x8E5B2A), HEX(0xFFE9A6), 0, HEX(0xF7A8A0)};
  float q = r / 32, x0 = 0, y0 = 0;
  for (int i = first[k]; i < first[k + 1]; i++) {
    const stroke_t *t = &strokes[i];
    int n = t->tc >> 4 & 7;
    float x = cx + (t->x * c - t->y * s) * q, y = cy + (t->x * s + t->y * c) * q, rd = (float)(t->tc & 15) * r / 128;
    color_t col = n == 1 ? face_c[k] : n == 6 ? line_c[k] : pal[n];
    if (rd < 0.5f) rd = 0.5f;
    if (t->tc & 0x80)
      seg(x0, y0, x, y, rd, 0, col, 32);
    else
      DISC(x, y, rd, col, n ? n == 7 ? 18 : 32 : 24);
    x0 = x, y0 = y;
  }
}

/* the grape's bunch: its berries, back to front */
static const int8_t berry[7][3] = {{-14, -14, 13}, {6, -18, 13}, {20, -4, 12}, {-20, 4, 12}, {16, 14, 12}, {-8, 18, 12}, {0, 0, 14}};

/* a fruit at (cx, cy), radius r on screen, turned (c, s): its outline, its body lit from the top left (the
   pineapple's lattice, the melon's net and the watermelon's stripes turning with it), then its face */
static void fruit(int k, float cx, float cy, float r, float c, float s) {
  if (cy + 1.6f * r + 2 < gfx_y0 || cy - 1.6f * r - 2 >= gfx_y1) return;
  color_t fc = fill_c[k], lc = line_c[k], shade = gfx_mix(fc, lc, 10);
  float q = r / 32, ow = r * 0.08f + 0.5f;
  if (k == 3) { /* the bunch */
    for (int i = 0; i < 7; i++) {
      float x = cx + (berry[i][0] * c - berry[i][1] * s) * q, y = cy + (berry[i][0] * s + berry[i][1] * c) * q, br = berry[i][2] * q;
      DISC(x, y, br + ow * 0.6f, lc, 32), DISC(x, y, br - ow * 0.4f, fc, 32);
    }
  } else {
    float bx = cx + 27 * s * q, by = cy - 27 * c * q; /* the dekopon's bump */
    if (k == 4) DISC(bx, by, 10 * q + ow * 0.5f, lc, 32);
    int ya = (int)fmaxf_(cy - r, gfx_y0), yb = (int)fminf_(cy + r + 1, gfx_y1), xa = (int)fmaxf_(cx - r, 0), xb = (int)fminf_(cx + r + 1, SCREEN_W);
    for (int y = ya; y < yb; y++)
      for (int x = xa; x < xb; x++) {
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrt_(dx * dx + dy * dy), lx = dx + 0.12f * r, ly = dy + 0.14f * r;
        if (d >= r + 0.5f) continue;
        color_t col = d > r - ow ? lc : lx * lx + ly * ly < 0.88f * r * r ? fc : shade;
        if (k >= 9 && d <= r - ow) { /* lines across it (in tenths): a u + b v + c u^2, and maybe a second set; how wide (100ths), from how far out, how strong */
          static const int8_t pat[3][8] = {{23, 23, 0, 23, -23, 9, 0, 14}, {33, 8, 0, -8, 33, 16, 6, 24}, {0, 18, 4, 0, 0, 34, 0, 24}};
          static const color_t pc[3] = {HEX(0xD9A80C), HEX(0xDCF290), HEX(0x0C6A08)};
          const int8_t *t = pat[k - 9];
          float u = (dx * c + dy * s) / r, v = (dy * c - dx * s) / r, w = t[5] * 0.01f;
          bool on = frac((t[0] * u + t[1] * v + t[2] * u * u) * 0.1f + 9.25f) < w || (t[3] && frac((t[3] * u + t[4] * v) * 0.1f + 9) < w);
          if (on && d > t[6] * 0.1f * r) col = gfx_mix(col, pc[k - 9], t[7]);
        }
        put(gfx_buf + (y - gfx_y0) * SCREEN_W + x, col, (int)(clampf(r + 0.5f - d, 0, 1) * 32));
      }
    if (k == 4) DISC(bx, by, 10 * q - ow * 0.5f, fc, 32);
  }
  decor(k, cx, cy, r, c, s);
}
static void fruit_at(int k, float x, float y) { fruit(k, x, y, RAD(k) / 3, 1, 0); }

/* the cloud, holding the fruit at its lower left */
static void cloud(float x, float y) {
  static const int8_t lobe[7][3] = {{-13, 2, 8}, {-6, -5, 9}, {4, -6, 10}, {13, -1, 8}, {11, 6, 7}, {0, 5, 10}, {-11, 8, 6}};
  for (int pass = 0; pass < 2; pass++) {
    float e = pass ? 0 : 1.4f;
    color_t col = pass ? fill_c[0] : line_c[0];
    for (int i = 0; i < 7; i++) DISC(x + lobe[i][0], y + lobe[i][1], lobe[i][2] + e, col, 32);
  }
  decor(0, x, y, 20, 1, 0);
}

/* a soap bubble */
static void bubble(float x, float y, float r) {
  seg(x, y, x, y, r, r - 1.3f, 0xFFFF, 13);
  seg(x - r * 0.62f, y - r * 0.2f, x - r * 0.3f, y - r * 0.62f, 1.6f, 0, 0xFFFF, 22);
}
static const np_font_t *const fonts[3] = {&np_font_title, &np_font_body, &np_font_small};
/* cream with a shadow (style 0), brown (1) or orange (2) */
static void text(int fs, int x, int y, const char *s) {
  if (!(fs >> 4)) gfx_text_center(fonts[fs & 3], x + 1, y + 1, s, HEX(0x8A5420), 256);
  gfx_text_center(fonts[fs & 3], x, y, s, (fs >> 4) == 1 ? INK : (fs >> 4) ? HEX(0xD9480F) : HEX(0xFFF4D6), 256);
}
static const struct {
  uint8_t fs, y; /* font, style, and 0x40 for the right side */
  const char *s;
} labels[] = {
  {0x00, 24, T("Score")},      {0x02, 75, T("Best score")}, {0x40, 30, T("Next")}, {0x42, 117, T("Circle of")},
  {0x42, 128, T("Evolution")}, {0x12, 140, T("Move")},      {0x21, 163, "OK"},     {0x12, 176, T("Drop")},
  {0x21, 199, T("Back")},      {0x12, 212, T("Menu")},
};
static const char *const items[] = {T("Resume"), T("Restart"), T("How to play"), T("Quit"), T("No"), T("Yes"), T("Play"), T("Restart"), T("Quit")};
static const uint8_t ov_first[5] = {0, 0, 4, 6, 7}, ov_count[5] = {0, 4, 2, 1, 2}, ov_h[5] = {0, 186, 120, 208, 176};
static const char *const ov_title[4] = {0, T("Paused"), T("Start again?"), T("How to play")};

static void scene(void *ctx) {
  char s[24];
  /* the room: the wall, its shadow, the floor */
  for (int y = gfx_y0; y < gfx_y1; y++)
    gfx_fill(0, y, SCREEN_W, 1, y < 87 ? HEX(0xC68E52) : y < 108 ? HEX(0xB0751F) : y < 137 ? HEX(0xCDA458) : HEX(0xEEDFA4));
  /* the box, behind the fruit: its glass, its top seen from above */
  gfx_fill_alpha(85, 41, 150, 184, HEX(0xFFF6E0), 80);
  gfx_fill(95, 37, 130, 4, HEX(0xF4D680));
  seg(95, 39, 83, 55, 1.4f, 0, HEX(0xF4D680), 32), seg(225, 39, 237, 55, 1.4f, 0, HEX(0xF4D680), 32);
  bool held = sv.flags & HELD && !over, danger = false;
  if (held) gfx_fill_alpha((int)SX(sv.aim), 31, 1, 194, 0xFFFF, 190); /* the guide line */
  for (int i = 0; i < MAXF; i++) {
    fruit_t *a = &f[i];
    if (a->k) fruit(a->k, SX(a->x), SY(a->y), RAD(a->k) / 3, a->c, a->s), danger |= a->over > 0;
  }
  /* its front, over them */
  gfx_fill_alpha(81, 53, 158, 5, HEX(0xF4D27E), 210);
  gfx_vgrad(81, 58, 4, 167, 0xF4D27E, 0xE8B04A), gfx_vgrad(235, 58, 4, 167, 0xF4D27E, 0xE8B04A);
  gfx_fill(81, 225, 158, 4, HEX(0xE8B04A));
  if (danger)
    for (int x = 86; x < 234; x += 7) gfx_fill(x, 36, 4, 2, HEX(0xE8341C));
  /* the score, the next fruit, the circle of evolution, and the keys where the original lists the day's best */
  bubble(40, 58, 33), bubble(280, 62, 28);
  gfx_rrect(6, 104, 68, 116, 10, HEX(0xFFF7E2), 190);
  gfx_icon(&np_icon_left, 27, 112, INK, 256), gfx_icon(&np_icon_right, 45, 112, INK, 256);
  for (unsigned i = 0; i < sizeof labels / sizeof labels[0]; i++) text(labels[i].fs & 0x33, labels[i].fs & 0x40 ? 280 : 40, labels[i].y, labels[i].s);
  num(s, sv.score), text(0, 40, 62, s);
  num(s, sv.best), text(1, 40, 88, s);
  fruit_at(sv.next, 280, 64);
  seg(280, 182, 280, 182, 40, 23, HEX(0xC8EC6A), 17);
  static const int8_t evo[11][2] = {{16, -28}, {28, -16}, {32, 0}, {28, 16}, {16, 28}, {0, 32}, {-16, 28}, {-28, 16}, {-32, 0}, {-28, -16}, {-16, -28}};
  for (int k = 1; k <= 11; k++) fruit(k, 280 + evo[k - 1][0], 182 + evo[k - 1][1], 5.2f, 1, 0);
  /* the cloud and its fruit */
  cloud(SX(sv.aim) + 18, SY(HANG) - 13);
  if (held) fruit_at(sv.cur, SX(sv.aim), SY(HANG));
  if (ov) {
    int h = ov_h[ov], top = 120 - h / 2, n = ov_count[ov];
    gfx_fill_alpha(0, 0, SCREEN_W, SCREEN_H, 0, 110);
    gfx_rrect(40, top, 240, h, 14, HEX(0xFFF7DE), 256);
    gfx_rrect_outline(40, top, 240, h, 14, 3, HEX(0xE8B04A), 256);
    gfx_text_center(&np_font_title, 160, top + 30, ov < OV_OVER ? ov_title[ov] : sv.score > best0 ? T("New best!") : T("Game over"), INK,
                    256);
    if (ov == OV_HELP) {
      gfx_paragraph(&np_font_body, 160, top + 52, 216, 15,
                    T("Move the cloud with the arrows and drop the fruit with OK. Two of the same fruit that touch join into the next one, up to the watermelon. If fruit stays above the box, it's over."),
                    INK, 256);
      float x = 70;
      for (int k = 1; k <= 11; k++) { /* the fruit, smallest to biggest */
        float r = 2.6f + k * 0.75f;
        fruit(k, x + r, top + 154, r, 1, 0), x += 2 * r + 1.6f;
      }
    }
    if (ov == OV_OVER) {
      num(s, sv.score);
      gfx_text_center(&np_font_title, 160, top + 66, s, HEX(0xF08A00), 256);
      char *e = s;
      for (const char *p = T("Best "); *p;) *e++ = *p++;
      num(e, sv.best);
      gfx_text_center(&np_font_body, 160, top + 86, s, INK, 256);
    }
    for (int i = 0; i < n; i++) {
      int y = top + h - 38 - 34 * (n - 1 - i);
      gfx_rrect(95, y, 130, 24, 12, sel == i ? HEX(0xFFB624) : HEX(0xE6DFD0), 256);
      gfx_text_center(&np_font_body, 160, y + 17, items[ov_first[ov] + i], sel == i ? 0xFFFF : INK, 256);
    }
  }
}

/* ------------------------------------------------------------ the game */
static void load(void) {
  uint32_t len = 0;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  uint8_t *v = (uint8_t *)&sv;
  bool ok = d && len == sizeof sv && d[0] == 'S' && d[1] == 1;
  for (int i = 0; i < MAXF; i++) f[i] = (fruit_t){0};
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = ok ? d[i] : 0;
  if (sv.sum != checksum() || sv.cur - 1u > 4 || sv.next - 1u > 4 || sv.drop > MAXF)
    for (uint32_t i = 0; i < sizeof sv; i++) v[i] = 0;
  unpack();
  aim_at(sv.aim);
  over = false;
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  now = (uint32_t)eadk_timing_millis();
  load();
  sv.rng ^= eadk_random() ^ now;
  if (!(sv.flags & GAME)) new_game();
  best0 = sv.best;
  if (!(sv.flags & SEEN)) ov = OV_HELP, sv.flags |= SEEN;
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the launcher wait to be let go */
  uint32_t tick = now, saved = now;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint64_t raw = eadk_keyboard_scan(), k = raw & ~(stale &= raw), hit = k & ~held;
    held = k;
    if (raw & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe));
    int dy = !!(hit & KEY(eadk_key_down)) - !!(hit & KEY(eadk_key_up));
    int dir = !!(k & (KEY(eadk_key_right) | KEY(eadk_key_six))) - !!(k & (KEY(eadk_key_left) | KEY(eadk_key_four)));
    if (over && !ov && now - over_t > 530) ov = OV_OVER, sel = 0;
    if (ov) {
      sel = (sel + dy + ov_count[ov]) % ov_count[ov];
      if (hit & KEY(eadk_key_back) && ov != OV_OVER) {
        ov = ov == OV_SURE ? OV_MENU : OV_NONE, sel = 1;
      } else if (ok) {
        int was = ov, choice = sel;
        ov = OV_NONE, sel = 0;
        if (was == OV_MENU) { /* Resume, Restart (asks first), How to play, Quit */
          if (choice == 1) ov = OV_SURE;
          else if (choice == 2) ov = OV_HELP;
          else if (choice == 3) break;
        } else if (was == OV_SURE) { /* No, Yes */
          if (choice) new_game();
          else ov = OV_MENU, sel = 1;
        } else if (was == OV_OVER) { /* Restart, Quit */
          if (choice) break;
          new_game();
        }
      }
    } else if (hit & KEY(eadk_key_back)) {
      ov = OV_MENU, sel = 0;
      save();
    } else if (ok) {
      drop_fruit();
    }
    /* fifty steps a second, as the original's physics, catching up after a slow frame */
    if (ov || over) tick = now;
    if (now - tick > 100) tick = now - 100;
    for (; now - tick >= 20; tick += 20) sim(dir);
    if (now - saved > 10000) saved = now, save();
    eadk_display_wait_for_vblank();
    gfx_render(scene, 0, 0, SCREEN_H);
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 20) eadk_timing_msleep(20 - spent);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
