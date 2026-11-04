/* Snake: Google's Snake, the game in Search, for the NumWorks calculator.
 * Its lawn, its blue snake with a shadow, its fruit, its menu card and its
 * settings: 6 fruits, 12 modes, 1, 3 or 5 fruits at a time, 3 speeds, 3
 * board sizes (17 x 15, 10 x 9, 24 x 21), 8 snake colors and 4 themes.
 * Tatone26's Snake from All the Apps was the starting point.
 *
 * The snake is a chain of capsules through the centres of its cells, so it
 * glides from cell to cell and turns with round corners. Each frame only the
 * cells that change are drawn again (around the heads and tails, fruit that
 * moves or pops, the light in Light mode): a cell at a time, composed in a
 * small buffer, then pushed. Menus are drawn in bands of 20 rows. */
#include <eadk.h>
#include <stdbool.h>
#include <stdint.h>
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"
#include "../../common/np_text.h"

#ifdef __ELF__ /* app name and API level, for the calculator's installer */
#ifndef NP_PORT /* (another target: platform/port.mk names the app) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Snake";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif

typedef uint16_t color;
#define RGB(c) (color)((((c) >> 8) & 0xF800) | (((c) >> 5) & 0x07E0) | (((c) >> 3) & 0x1F))
#define WHITE 0xFFFF
/* the keys used, in 32 bits: 4, 6, 2 and EXE come down from the high word */
#define KEY(k) ((uint32_t)1 << ((k) - ((k) >= 32) * 24))
static uint32_t keys(void) {
  uint64_t s = eadk_keyboard_scan();
  return ((uint32_t)s & 0x800001FFu) | ((uint32_t)(s >> 32) & 0x00100850u) << 8;
}

/* Google Snake's colors */
#define C_APPLE RGB(0xE7471D)
#define C_GOLD RGB(0xFFC53D)
#define C_CARD RGB(0x4DC1F9)  /* the menu card */
#define C_CARD2 RGB(0x35A7E3) /* the row in focus */
#define C_BTN RGB(0x1A5CD7)   /* a button */
#define C_BTN2 RGB(0x2F4F9A)  /* a button not in focus */
#define HDR 24                /* the band on top */

/* the themes: the band on top, around the field (and walls), the lawn */
static const uint32_t THEME[4][4] = {
  {0x4A752C, 0x578A34, 0xAAD751, 0xA2D149}, /* day */
  {0x17222E, 0x22354A, 0x31475B, 0x2B4053}, /* night */
  {0x55778F, 0x7FA2BB, 0xEEF5FB, 0xDEEAF4}, /* snow */
  {0x3A1810, 0x5C2414, 0x5C4136, 0x52382E}, /* volcano */
};
static const uint32_t SNAKES[8] = {0x4E7CF6, 0x8E5CE6, 0xE8588F, 0xF48C2E, 0xF2C21E, 0x17A5A0, 0xF4F4F4, 0x2E2E2E};
static color c_bar, c_edge, c_light, c_dark, c_slight, c_sdark; /* the theme's, and shadows on the lawn */

/* ------------------------------------------------------------------ drawing */
#define BUFPX (320 * 20)
static color buf[BUFPX];
static int rx, ry, rw, rh;     /* the part of the screen in buf */
static int kx0, ky0, kx1, ky1; /* where drawing lands, inside it */
static bool shade;             /* blob() lays shadows on the lawn instead of painting */

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int iabs(int v) { return v < 0 ? -v : v; }

static void clip(int x, int y, int w, int h) {
  kx0 = imax(x, rx), ky0 = imax(y, ry), kx1 = imin(x + w, rx + rw), ky1 = imin(y + h, ry + rh);
}
static void unclip(void) { clip(0, 0, 320, 240); }

/* f over b; a from 0 (all b) to 32 (all f) */
static color mix(color f, color b, int a) {
  uint32_t x = (f | (uint32_t)f << 16) & 0x07E0F81F, y = (b | (uint32_t)b << 16) & 0x07E0F81F;
  y = (y + ((x - y) * (uint32_t)a >> 5)) & 0x07E0F81F;
  return (color)(y | y >> 16);
}

static void fill(int x, int y, int w, int h, color c) {
  for (int j = imax(y, ky0), j1 = imin(y + h, ky1); j < j1; j++)
    for (int i = imax(x, kx0), i1 = imin(x + w, kx1); i < i1; i++) buf[(j - ry) * rw + i - rx] = c;
}
/* the pixels of a rectangle, a of the way to c */
static void wash(int x, int y, int w, int h, color c, int a) {
  for (int j = imax(y, ky0), j1 = imin(y + h, ky1); j < j1; j++)
    for (int i = imax(x, kx0), i1 = imin(x + w, kx1); i < i1; i++) {
      color *p = buf + (j - ry) * rw + i - rx;
      *p = mix(c, *p, a);
    }
}

/* A capsule from a to b, its radius going from ra to rb, with soft edges.
   Everything round is one: discs, the snake, fruit, icons. */
static void blob(float ax, float ay, float bx, float by, float ra, float rb, color c) {
  float dx = bx - ax, dy = by - ay, l = dx * dx + dy * dy, inv = l > 0 ? 1 / l : 0, r = (ra > rb ? ra : rb) + 2;
  int x0 = imax(kx0, (int)((ax < bx ? ax : bx) - r)), x1 = imin(kx1, (int)((ax > bx ? ax : bx) + r));
  int y0 = imax(ky0, (int)((ay < by ? ay : by) - r)), y1 = imin(ky1, (int)((ay > by ? ay : by) + r));
  for (int y = y0; y < y1; y++) {
    float qy = y + 0.5f - ay;
    color *p = buf + (y - ry) * rw - rx;
    for (int x = x0; x < x1; x++) {
      float qx = x + 0.5f - ax, t = (qx * dx + qy * dy) * inv;
      t = t < 0 ? 0 : t > 1 ? 1 : t;
      float ex = qx - t * dx, ey = qy - t * dy, e = ra + (rb - ra) * t + 0.5f, d = ex * ex + ey * ey;
      if (d >= e * e) continue;
      if (shade) { /* the same shadow however many shapes cast it */
        p[x] = p[x] == c_light ? c_slight : p[x] == c_dark ? c_sdark : p[x];
        continue;
      }
      float a = e - __builtin_sqrtf(d);
      p[x] = a >= 1 ? c : mix(c, p[x], (int)(a * 32));
    }
  }
}
static void disc(float x, float y, float r, color c) { blob(x, y, x, y, r, r, c); }
static void box(int x, int y, int w, int h, int r, color c) {
  fill(x + r, y, w - 2 * r, h, c);
  fill(x, y + r, w, h - 2 * r, c);
  for (int i = 0; i < 4; i++) disc(x + r + (i & 1) * (w - 2 * r), y + r + (i >> 1) * (h - 2 * r), r, c);
}
/* a triangle pointing along (dx, dy) (one of them 0), its tip at (x, y), a
   long, its base 2 s + 1 wide */
static void tri(int x, int y, int dx, int dy, int a, int s, color c) {
  for (int t = 0; t < a; t++) {
    int w = s * t / a;
    if (dx) fill(x - dx * t, y - w, 1, 2 * w + 1, c);
    else fill(x - w, y - dy * t, 2 * w + 1, 1, c);
  }
}
/* darkens everything under the band on top, behind a card */
static void dim(void) {
  clip(0, HDR, 320, 240);
  for (int j = ky0; j < ky1; j++)
    for (color *p = buf + (j - ry) * rw, *e = p + rw; p < e; p++) *p = mix(*p, 0, 14);
  unclip();
}

/* ------------------------------------------------------------------ the game */
#define MAXW 24
#define MAXH 21
#define MAXN (MAXW * MAXH)
/* sizes in Google's order: normal, small, large; speeds: normal, fast, slow */
static const uint8_t BW[3] = {17, 10, 24}, BH[3] = {15, 9, 21}, CS[3] = {14, 23, 10};
static const uint8_t STEP_MS[3] = {135, 95, 190};
static const int8_t DX[4] = {1, 0, -1, 0}, DY[4] = {0, 1, 0, -1}; /* right, down, left, up */

enum { O_FRUIT, O_MODE, O_COUNT, O_SPEED, O_SIZE, O_COLOR, O_THEME, NOPT };
static const uint8_t OPT_N[NOPT] = {6, 12, 3, 3, 3, 8, 4};
enum { M_CLASSIC, M_WALL, M_PORTAL, M_CHEESE, M_BORDERLESS, M_TWIN, M_WINGED, M_YINYANG, M_STATUE, M_LIGHT, M_MAGNET, M_PEACEFUL };
#define NBEST (12 * 3 * 3 * 3)

/* everything kept between visits (snake.sav) */
static struct {
  uint8_t magic, version;
  uint8_t opt[NOPT], pad;
  uint16_t best[NBEST]; /* per mode, count, speed and size */
} V;
#define SAVE_NAME "snake.sav"
#define MODE V.opt[O_MODE]

static int bw, bh, cs, fx, fy; /* board in cells, cell size, where the field starts */
static uint8_t occ[MAXN];      /* per cell: snakes' solid parts, a wall, a statue */
#define O_BODY 0x3F
#define O_STONE 0x40
#define O_WALL 0x80

typedef struct {
  uint16_t body[MAXN]; /* its cells (x | y << 8), a ring from tl to hd */
  uint8_t ndir[MAXN];  /* the way each was entered, | 4 for a hole (Cheese) */
  int hd, tl, len, grow, otail, hop, nq, made;
  uint8_t q[2];      /* turns asked for, not made yet */
  bool tmove, flip;  /* the tail moved in the last step; Twin: turn round first */
} snake_t;
static snake_t S[2];
static int ns; /* 2 in Yin Yang: the second one mirrors the first */

typedef struct {
  uint16_t p, from; /* where it is, and where it was a step ago (it flies, or is pulled) */
  uint8_t kind;     /* 0 fruit, else the pair of portals it belongs to */
  uint8_t eat;      /* being eaten: steps left before it goes */
  int8_t vx, vy;    /* Winged: its flight */
  bool live;
  uint32_t born;
} apple_t;
static apple_t ap[8];
static int score, wpop, gate[3][2], nstep, acc; /* gate: each pair's portals last used; acc: ms into the step */
static uint32_t now, wborn, chew_t, lit_t, seed;

static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  seed ^= seed << 5;
  return seed;
}
static int at(int p) { return (p >> 8) * bw + (p & 255); }
static int step_ms(void) { return STEP_MS[V.opt[O_SPEED]]; }
static uint16_t *best(void) {
  return &V.best[((MODE * 3 + V.opt[O_COUNT]) * 3 + V.opt[O_SPEED]) * 3 + V.opt[O_SIZE]];
}
static bool solid(snake_t *s, int k) { return !(s->ndir[k % MAXN] & 4); }
static bool wraps(void) { return MODE == M_BORDERLESS || MODE == M_PEACEFUL; }

static bool far(int p, int h) { return iabs((p & 255) - (h & 255)) + iabs((p >> 8) - (h >> 8)) > 2; }
/* a cell for fruit or a wall: free, and (first try) not right by a head,
   or the portal it is about to come out of */
static bool vacant(int p, int pass) {
  if (occ[at(p)]) return false;
  for (int i = 0; i < 8; i++)
    if (ap[i].live && ap[i].p == p) return false;
  for (int k = 0; k < ns && !pass; k++)
    if (!far(p, S[k].body[S[k].hd]) || (S[k].hop >= 0 && !far(p, S[k].hop))) return false;
  return true;
}
/* the n-th vacant cell, or (1 << 20) less the number of them */
static int nth(int pass, int n) {
  for (int y = 0; y < bh; y++)
    for (int x = 0; x < bw; x++)
      if (vacant(x | y << 8, pass) && !n--) return x | y << 8;
  return n;
}
static int pick(void) {
  for (int pass = 0; pass < 2; pass++) {
    int n = (1 << 20) - nth(pass, 1 << 20);
    if (n) return nth(pass, (int)(rnd() % (uint32_t)n));
  }
  return -1;
}
static void put(int i, int p, int kind) {
  ap[i] = (apple_t){(uint16_t)p, (uint16_t)p, (uint8_t)kind, 0, (int8_t)(rnd() & 1 ? 1 : -1), (int8_t)(rnd() & 1 ? 1 : -1), true, now};
}
static bool spawn(int kind) {
  int p = pick();
  for (int i = 0; i < 8 && p >= 0; i++)
    if (!ap[i].live) {
      put(i, p, kind);
      return true;
    }
  return false;
}

static void set_theme(void) {
  const uint32_t *t = THEME[V.opt[O_THEME]];
  c_bar = RGB(t[0]), c_edge = RGB(t[1]), c_light = RGB(t[2]), c_dark = RGB(t[3]);
  c_slight = mix(0, c_light, 4), c_sdark = mix(0, c_dark, 4);
}

static void new_game(void) {
  int sz = V.opt[O_SIZE];
  bw = BW[sz], bh = BH[sz], cs = CS[sz];
  fx = (320 - bw * cs) / 2, fy = HDR + (240 - HDR - bh * cs) / 2;
  set_theme();
  for (int i = 0; i < MAXN; i++) occ[i] = 0;
  for (int i = 0; i < 8; i++) ap[i].live = false;
  ns = MODE == M_YINYANG ? 2 : 1;
  int row = bh / 2 - (ns == 2) * (bh / 5); /* two snakes: rows apart, mirrored through the centre */
  for (int k = 0; k < ns; k++) {
    snake_t *s = &S[k];
    for (int i = 0; i < 4; i++) {
      int x = k ? bw - 2 - i : 1 + i, y = k ? bh - 1 - row : row;
      s->body[i] = (uint16_t)(x | y << 8), s->ndir[i] = (uint8_t)(k * 2), occ[y * bw + x]++;
    }
    s->tl = 0, s->hd = 3, s->len = 4, s->grow = s->nq = s->made = 0, s->hop = -1, s->tmove = s->flip = false;
  }
  for (int i = 0; i < 6; i++) gate[i / 2][i & 1] = -1;
  score = nstep = acc = 0, wpop = -1, chew_t = lit_t = now - 5000;
  /* the first fruit straight ahead, like Google's */
  int n = V.opt[O_COUNT] * 2 + 1, k = MODE == M_PORTAL;
  put(0, (bw - 5) | row << 8, k);
  if (k) {
    spawn(1);
    for (k = 2; k <= (n + 1) / 2; k++) spawn(k), spawn(k);
  } else {
    while (--n) spawn(0);
  }
  for (int i = 0; i < 8; i++) ap[i].born = 0; /* no popping in the menus */
}

/* Twin: head and tail trade places, and it goes back the way it came */
static void turn_round(snake_t *s) {
  static uint16_t tb[MAXN];
  static uint8_t td[MAXN];
  int n = s->len;
  for (int i = 0; i < n; i++) tb[i] = s->body[(s->tl + i) % MAXN], td[i] = s->ndir[(s->tl + i) % MAXN];
  for (int j = 0; j < n; j++) {
    int o = n - 1 - j, d = (j ? td[o + 1] : td[n - 1]) & 3;
    s->body[j] = tb[o], s->ndir[j] = (uint8_t)((d ^ 2) | (td[o] & 4));
  }
  s->tl = 0, s->hd = n - 1, s->nq = 0, s->tmove = false, s->hop = -1;
}

/* Statue: all but the head turns to stone, and it starts again from 4 */
static void statue(snake_t *s) {
  for (int i = 0; i < s->len - 1; i++) {
    int k = (s->tl + i) % MAXN, c = at(s->body[k]);
    if (solid(s, k)) occ[c]--;
    occ[c] |= O_STONE;
  }
  s->tl = s->hd, s->len = 1, s->grow += 3, s->tmove = false;
}

static void eat(int k, apple_t *a) {
  snake_t *s = &S[k];
  a->eat = 1, score++, s->grow++, lit_t = now;
  if (a->kind) {
    for (int j = 0; j < 8; j++)
      if (&ap[j] != a && ap[j].live && !ap[j].eat && ap[j].kind == a->kind) s->hop = gate[a->kind - 1][1] = ap[j].p, ap[j].eat = 2;
    gate[a->kind - 1][0] = a->p;
    if (spawn(a->kind) && !spawn(a->kind)) /* no room for two: a plain fruit */
      for (int j = 0; j < 8; j++)
        if (ap[j].live && !ap[j].eat && ap[j].kind == a->kind) ap[j].kind = 0;
  } else {
    spawn(0);
  }
  if (MODE == M_WALL) { /* Google's walls: one more for each fruit */
    int w = pick();
    if (w >= 0) occ[at(w)] |= O_WALL, wpop = w, wborn = now;
  }
  if (MODE == M_TWIN) s->flip = true;
  if (MODE == M_STATUE) statue(s);
}

/* a cell fruit can move to */
static bool open_for_fruit(int x, int y) {
  if (x < 0 || y < 0 || x >= bw || y >= bh || occ[y * bw + x]) return false;
  for (int i = 0; i < 8; i++)
    if (ap[i].live && ap[i].p == (x | y << 8)) return false;
  return true;
}
/* Winged: fruit flies on the diagonal, bouncing off the edges and the snake;
   Magnet: fruit near a head is pulled into it */
static void move_fruit(void) {
  for (int i = 0; i < 8; i++) {
    apple_t *a = &ap[i];
    a->from = a->p;
    if (!a->live || a->eat || a->kind) continue;
    int x = a->p & 255, y = a->p >> 8;
    if (MODE == M_WINGED && (nstep & 1)) {
      for (int t = 0; t < 4; t++) {
        int vx = t & 1 ? -a->vx : a->vx, vy = t & 2 ? -a->vy : a->vy;
        if (open_for_fruit(x + vx, y + vy)) {
          a->vx = (int8_t)vx, a->vy = (int8_t)vy, a->p = (uint16_t)((x + vx) | (y + vy) << 8);
          break;
        }
      }
    } else if (MODE == M_MAGNET) {
      for (int k = 0; k < ns; k++) {
        int h = S[k].body[S[k].hd], dx = (h & 255) - x, dy = (h >> 8) - y;
        if (iabs(dx) > 2 || iabs(dy) > 2) continue;
        int mx = iabs(dx) >= iabs(dy) ? (dx > 0) - (dx < 0) : 0, my = mx ? 0 : (dy > 0) - (dy < 0);
        if ((x + mx) == (h & 255) && (y + my) == (h >> 8)) {
          a->p = (uint16_t)h, eat(k, a); /* into the mouth */
        } else if (open_for_fruit(x + mx, y + my)) {
          a->p = (uint16_t)((x + mx) | (y + my) << 8);
        }
        break;
      }
    }
  }
}

/* One cell forward for every snake; false if one crashes. */
static bool step(void) {
  int np[2], d[2], c[2];
  bool moving[2];
  for (int k = 0; k < ns; k++)
    if (S[k].flip) turn_round(&S[k]), S[k].flip = false;
  for (int k = 0; k < ns; k++) {
    snake_t *s = &S[k];
    int h = s->body[s->hd];
    d[k] = s->ndir[s->hd] & 3;
    if (s->hop >= 0) { /* out of the other portal, the same way */
      np[k] = s->hop;
    } else {
      if (s->nq) d[k] = s->q[0];
      int x = (h & 255) + DX[d[k]], y = (h >> 8) + DY[d[k]];
      if (x < 0 || y < 0 || x >= bw || y >= bh) {
        if (!wraps()) return false;
        x = (x + bw) % bw, y = (y + bh) % bh;
      }
      np[k] = x | y << 8;
    }
    moving[k] = !s->grow;
    c[k] = at(np[k]);
  }
  if (MODE != M_PEACEFUL) {
    for (int k = 0; k < ns; k++) {
      int n = occ[c[k]] & O_BODY;
      for (int j = 0; j < ns; j++) /* a tail that goes away as the head comes */
        if (moving[j] && S[j].body[S[j].tl] == np[k] && solid(&S[j], S[j].tl)) n--;
      if ((occ[c[k]] & (O_WALL | O_STONE)) || n > 0) return false;
    }
    if (ns == 2 && np[0] == np[1]) return false;
  }
  for (int i = 0; i < 8; i++)
    if (ap[i].eat && !--ap[i].eat) ap[i].live = false, chew_t = now;
  nstep++;
  for (int k = 0; k < ns; k++) {
    snake_t *s = &S[k];
    if (s->hop >= 0) s->hop = -1;
    else if (s->nq) s->q[0] = s->q[1], s->nq--;
    if (moving[k]) {
      s->otail = s->body[s->tl];
      if (solid(s, s->tl)) occ[at(s->otail)]--;
      s->tl = (s->tl + 1) % MAXN, s->len--;
    } else {
      s->grow--;
    }
    s->tmove = moving[k];
  }
  for (int k = 0; k < ns; k++) {
    snake_t *s = &S[k];
    bool hole = MODE == M_CHEESE && ++s->made % 3 == 0;
    s->hd = (s->hd + 1) % MAXN, s->len++;
    s->body[s->hd] = (uint16_t)np[k], s->ndir[s->hd] = (uint8_t)(d[k] | hole * 4);
    if (!hole) occ[c[k]]++;
  }
  for (int k = 0; k < ns; k++)
    for (int i = 0; i < 8; i++)
      if (ap[i].live && !ap[i].eat && ap[i].p == np[k]) {
        eat(k, &ap[i]);
        break;
      }
  move_fruit();
  return true;
}

/* ------------------------------------------------------------------ the board */
enum { S_TITLE, S_SET, S_READY, S_PLAY, S_PAUSE, S_QUIT, S_DIE };
static int state, sel, row;
static bool dead, newbest, won, played;
static float prog, mouth, oy; /* how far into the step; how open the mouth is; the shadow's drop */
static bool flash;            /* the snake flashes when it dies */
static int qx0, qy0, qx1, qy1; /* the cells the drawn part of the screen touches */

static int fl(int v) { return (v + cs * 16) / cs - 16; } /* floor(v / cs) */
static bool near(int p) {
  int x = p & 255, y = p >> 8;
  return x >= qx0 && x <= qx1 && y >= qy0 - shade && y <= qy1;
}
static float ux(int p) { return fx + (p & 255) * cs + cs * 0.5f; }
static float uy(int p) { return fy + (p >> 8) * cs + cs * 0.5f + oy; }
static void cellclip(int p) { clip(fx + (p & 255) * cs, fy + (p >> 8) * cs + (int)oy, cs, cs); }
static void fieldclip(void) { clip(fx, fy, bw * cs, bh * cs); }
static color checker(int p) { return ((p & 255) + (p >> 8)) & 1 ? c_dark : c_light; }

static void ground(void) {
  for (int j = 0; j < rh; j++) {
    int y = ry + j, cy = fl(y - fy), cx = fl(rx - fx), e = fx + cx * cs + cs;
    bool in = y >= HDR && cy >= 0 && cy < bh;
    color *p = buf + j * rw;
    for (int i = 0; i < rw; i++) {
      if (rx + i >= e) cx++, e += cs;
      p[i] = y < HDR ? c_bar : in && cx >= 0 && cx < bw ? ((cx + cy) & 1 ? c_dark : c_light) : c_edge;
    }
  }
}

static const uint32_t RING[3] = {0x9A5CF6, 0xFF8A1E, 0x1EB8E8}; /* the portal pairs */

/* ------------------------------------------------------------------ fruit and icons */
static void leaf(float x, float y, float s) { blob(x, y, x + s * 0.22f, y - s * 0.1f, s * 0.08f, s * 0.03f, RGB(0x4E9F2D)); }
static void stem(float x, float y, float x1, float y1, float s) { blob(x, y, x1, y1, s * 0.045f, s * 0.035f, RGB(0x6D4C2F)); }
/* the fruit f, about s pixels, centred on (x, y) */
static void fruit(int f, float x, float y, float s) {
  switch (f) {
    case 0: /* apple */
      stem(x + s * 0.02f, y - s * 0.2f, x + s * 0.08f, y - s * 0.42f, s);
      disc(x, y + s * 0.05f, s * 0.37f, C_APPLE);
      disc(x - s * 0.14f, y - s * 0.05f, s * 0.09f, RGB(0xF58A67));
      leaf(x + s * 0.1f, y - s * 0.34f, s);
      break;
    case 1: /* banana: a yellow crescent, brown at the ends */
      for (int i = 0; i < 6; i++) {
        float a0 = 0.35f + i * 0.4f, a1 = a0 + 0.4f; /* along a curve, in fifths of a half turn */
        float c0 = 1 - a0 * a0 / 2 + a0 * a0 * a0 * a0 / 24, c1 = 1 - a1 * a1 / 2 + a1 * a1 * a1 * a1 / 24;
        float s0 = a0 - a0 * a0 * a0 / 6, s1 = a1 - a1 * a1 * a1 / 6, w0 = i == 0 ? 0.05f : 0.13f, w1 = i == 5 ? 0.05f : 0.13f;
        blob(x + c0 * s * 0.36f, y - s * 0.22f + s0 * s * 0.36f, x + c1 * s * 0.36f, y - s * 0.22f + s1 * s * 0.36f, s * w0, s * w1, RGB(0xF6D33C));
      }
      disc(x + s * 0.33f, y - s * 0.1f, s * 0.05f, RGB(0x6D4C2F));
      break;
    case 2: /* pineapple */
      for (int i = -1; i <= 1; i++) blob(x, y - s * 0.2f, x + i * s * 0.16f, y - s * 0.46f, s * 0.07f, s * 0.03f, RGB(0x3E8E2A));
      blob(x, y - s * 0.02f, x, y + s * 0.2f, s * 0.26f, s * 0.24f, RGB(0xF0A932));
      for (int i = 0; i < 6; i++) disc(x + ((i % 3) - 1) * s * 0.14f + (i / 3) * s * 0.07f - s * 0.035f, y + (i / 3) * s * 0.16f, s * 0.035f, RGB(0xB8741F));
      break;
    case 3: /* grapes */
      stem(x, y - s * 0.3f, x + s * 0.05f, y - s * 0.46f, s);
      leaf(x + s * 0.04f, y - s * 0.38f, s);
      for (int i = 0; i < 6; i++) {
        int r = i < 3 ? 0 : i < 5 ? 1 : 2, c = i < 3 ? i : i < 5 ? i - 3 : 0;
        disc(x + (c - (2 - r) * 0.5f) * s * 0.24f, y - s * 0.2f + r * s * 0.2f, s * 0.13f, RGB(0x8E47C4));
      }
      disc(x - s * 0.28f, y - s * 0.26f, s * 0.04f, RGB(0xC49AE8));
      break;
    case 4: /* strawberry */
      blob(x, y - s * 0.06f, x, y + s * 0.3f, s * 0.3f, s * 0.07f, RGB(0xE53935));
      for (int i = 0; i < 5; i++) disc(x + (i - 2) * s * 0.11f, y + ((i & 1) * 0.12f - 0.02f) * s, s * 0.028f, RGB(0xFFE08A));
      for (int i = -1; i <= 1; i++) blob(x, y - s * 0.28f, x + i * s * 0.2f, y - s * 0.3f + !i * -s * 0.1f, s * 0.07f, s * 0.04f, RGB(0x3E8E2A));
      break;
    default: /* cherries */
      stem(x - s * 0.18f, y + s * 0.12f, x + s * 0.06f, y - s * 0.38f, s);
      stem(x + s * 0.2f, y + s * 0.06f, x + s * 0.06f, y - s * 0.38f, s);
      leaf(x + s * 0.06f, y - s * 0.38f, s);
      disc(x - s * 0.18f, y + s * 0.18f, s * 0.2f, RGB(0xD7263D));
      disc(x + s * 0.2f, y + s * 0.12f, s * 0.2f, RGB(0xD7263D));
      disc(x - s * 0.24f, y + s * 0.12f, s * 0.05f, RGB(0xF08A96));
      break;
  }
}

static void trophy(float x, float y, float k, color bg) {
  for (int s = -1; s <= 1; s += 2) {
    disc(x + 6 * k * s, y - 4 * k, 3 * k, C_GOLD);
    disc(x + 6 * k * s, y - 4 * k, 1.4f * k, bg);
  }
  int ky = ky0;
  clip(0, (int)(y - 7 * k), 320, 240);
  ky0 = imax(ky0, ky);
  blob(x, y - 7 * k, x, y, 7 * k, 2 * k, C_GOLD);
  unclip();
  fill((int)(x - k), (int)(y), (int)(2 * k + 1), (int)(4 * k), C_GOLD);
  box((int)(x - 5 * k), (int)(y + 3 * k), (int)(10 * k), (int)(3 * k), (int)k, C_GOLD);
}

/* The head at (x, y), looking along (ax, ay), r its radius: an open mouth,
   and eyes that look where it goes. */
static void face(float x, float y, float ax, float ay, float r) {
  if (mouth > 0.05f) {
    float mx = x + ax * r * 0.5f, my = y + ay * r * 0.5f, w = r * 0.45f * mouth, m = r * 0.42f * mouth;
    blob(mx - ay * w, my + ax * w, mx + ay * w, my - ax * w, m, m, RGB(0x7A1F1A));
  }
  for (int s = -1; s <= 1; s += 2) {
    float ex = x - ax * r * 0.1f - ay * r * 0.5f * s, ey = y - ay * r * 0.1f + ax * r * 0.5f * s, k = r * 0.22f;
    disc(ex, ey, r * 0.47f, WHITE);
    if (dead) {
      blob(ex - k, ey - k, ex + k, ey + k, r * 0.07f, r * 0.07f, 0);
      blob(ex - k, ey + k, ex + k, ey - k, r * 0.07f, r * 0.07f, 0);
    } else {
      disc(ex + ax * r * 0.2f, ey + ay * r * 0.2f, r * 0.25f, RGB(0x1B1B1B));
    }
  }
}

static color snake_color(int k) {
  color c = RGB(SNAKES[V.opt[O_COLOR]]);
  if (k) c = V.opt[O_COLOR] == 7 ? RGB(0xF4F4F4) : RGB(0x2E2E2E); /* Yin Yang: black and white */
  return flash ? mix(WHITE, c, 20) : c;
}

/* the icon of option o's value v, at (x, y) */
static void icon(int o, int v, float x, float y, color bg) {
  static const uint8_t CHECK[3] = {4, 3, 5};
  switch (o) {
    case O_FRUIT: fruit(v, x, y, 17); break;
    case O_COUNT: { /* 1, 3 in a triangle, 5 like a die */
      static const int8_t P[9][2] = {{0, 0}, {0, -4}, {-5, 4}, {5, 4}, {-4, -4}, {4, -4}, {0, 0}, {-4, 4}, {4, 4}};
      for (int i = 0; i < v * 2 + 1; i++) fruit(V.opt[O_FRUIT], x + P[v * v + i][0], y + P[v * v + i][1], v ? 8.5f : 16);
      break;
    }
    case O_SPEED:
      if (v == 0) { /* a snake */
        blob(x - 7, y + 4, x + 1, y + 4, 2.6f, 3.2f, RGB(0x4E7CF6));
        blob(x + 1, y + 4, x + 5, y - 3, 3.2f, 3.8f, RGB(0x4E7CF6));
        disc(x + 5, y - 4, 1.7f, WHITE);
      } else if (v == 1) { /* a rabbit */
        blob(x - 3, y - 1, x - 4, y - 8, 1.6f, 1.4f, WHITE);
        blob(x + 1, y - 1, x + 2, y - 8, 1.6f, 1.4f, WHITE);
        disc(x - 1, y + 3, 5, WHITE);
        disc(x - 3, y + 2, 0.9f, RGB(0x333333));
        disc(x + 1, y + 2, 0.9f, RGB(0x333333));
      } else { /* a turtle */
        disc(x + 6, y + 1, 2.4f, RGB(0x7CB342));
        for (int i = -1; i <= 1; i += 2) disc(x + i * 4, y + 5, 1.8f, RGB(0x7CB342));
        clip((int)x - 9, (int)y - 8, 18, 12);
        disc(x - 1, y + 3, 6.5f, RGB(0x2E7D32));
        disc(x - 1, y + 3, 3, RGB(0x4CAF50));
        unclip();
      }
      break;
    case O_SIZE: {
      int n = CHECK[v], w = 16;
      for (int j = 0; j < w; j++)
        for (int i = 0; i < w; i++)
          fill((int)x - 8 + i, (int)y - 8 + j, 1, 1, (i * n / w + j * n / w) & 1 ? RGB(0xA2D149) : RGB(0xAAD751));
      break;
    }
    case O_COLOR:
      disc(x, y, 7.5f, RGB(SNAKES[v]));
      disc(x, y, 3.8f, bg);
      break;
    case O_THEME:
      if (v == 0) {
        for (int i = 0; i < 8; i++) {
          static const int8_t R[8][2] = {{8, 0}, {6, 6}, {0, 8}, {-6, 6}, {-8, 0}, {-6, -6}, {0, -8}, {6, -6}};
          blob(x, y, x + R[i][0], y + R[i][1], 1, 1, RGB(0xFFB300));
        }
        disc(x, y, 4.8f, RGB(0xFFCA28));
      } else if (v == 1) {
        disc(x, y, 7, RGB(0xF3E5AB));
        disc(x + 4, y - 3, 6, bg);
      } else if (v == 2) {
        for (int i = 0; i < 3; i++) {
          static const int8_t R[3][2] = {{8, 0}, {4, 7}, {-4, 7}};
          blob(x - R[i][0], y - R[i][1], x + R[i][0], y + R[i][1], 1.1f, 1.1f, WHITE);
        }
      } else {
        for (int t = 0; t < 12; t++) fill((int)x - 2 - t * 3 / 4, (int)y - 4 + t, 5 + t * 3 / 2, 1, RGB(0x6D4C41));
        blob(x - 2, y - 5, x + 2, y - 5, 1.8f, 1.8f, RGB(0xFF5722));
        disc(x, y - 8, 1.6f, RGB(0xFFA000));
      }
      break;
    default: /* O_MODE */
      switch (v) {
        case M_CLASSIC: trophy(x, y - 1, 0.95f, bg); break;
        case M_WALL:
          box((int)x - 8, (int)y - 7, 16, 15, 2, RGB(0x3D6624));
          box((int)x - 8, (int)y - 7, 16, 12, 2, RGB(0x578A34));
          fill((int)x - 7, (int)y - 2, 14, 1, RGB(0x3D6624));
          fill((int)x - 1, (int)y - 6, 1, 4, RGB(0x3D6624));
          fill((int)x - 5, (int)y - 1, 1, 5, RGB(0x3D6624));
          fill((int)x + 3, (int)y - 1, 1, 5, RGB(0x3D6624));
          break;
        case M_PORTAL:
          disc(x, y, 8, RGB(RING[0]));
          disc(x, y, 5.5f, bg);
          fruit(0, x, y, 9);
          break;
        case M_CHEESE:
          for (int t = 0; t < 12; t++) fill((int)x - 8, (int)y - 5 + t, 2 + t * 4 / 3, 1, RGB(0xFFCA28));
          fill((int)x - 8, (int)y + 7, 16, 1, RGB(0xF9A825));
          disc(x - 4, y + 3, 1.8f, RGB(0xF9A825));
          disc(x + 1, y + 5, 1.3f, RGB(0xF9A825));
          disc(x - 5, y - 1, 1.2f, RGB(0xF9A825));
          break;
        case M_BORDERLESS:
          for (int i = 0; i < 4; i++) {
            fill((int)x - 8 + i * 5, (int)y - 8, 3, 2, WHITE), fill((int)x - 8 + i * 5, (int)y + 7, 3, 2, WHITE);
            fill((int)x - 8, (int)y - 8 + i * 5, 2, 3, WHITE), fill((int)x + 7, (int)y - 8 + i * 5, 2, 3, WHITE);
          }
          blob(x - 3, y, x + 3, y, 2.5f, 2.5f, RGB(0x4E7CF6));
          break;
        case M_TWIN:
          blob(x - 6, y, x + 6, y, 3.4f, 3.4f, RGB(0x4E7CF6));
          for (int i = -1; i <= 1; i += 2) disc(x + i * 6, y - 1.5f, 1.4f, WHITE), disc(x + i * 6, y + 1.5f, 1.4f, WHITE);
          break;
        case M_WINGED:
          for (int i = -1; i <= 1; i += 2) blob(x + i * 4, y - 1, x + i * 8, y - 4, 2.2f, 1.2f, WHITE);
          fruit(0, x, y + 1, 12);
          break;
        case M_YINYANG:
          disc(x, y, 7.5f, RGB(0x222222));
          disc(x, y, 6.5f, WHITE);
          clip((int)x, (int)y - 8, 9, 17);
          disc(x, y, 6.5f, RGB(0x222222));
          unclip();
          disc(x, y - 3.25f, 3.25f, WHITE), disc(x, y + 3.25f, 3.25f, RGB(0x222222));
          disc(x, y - 3.25f, 1, RGB(0x222222)), disc(x, y + 3.25f, 1, WHITE);
          break;
        case M_STATUE:
          blob(x - 7, y + 4, x + 2, y + 4, 2.8f, 3.4f, RGB(0x8D949B));
          blob(x + 2, y + 4, x + 5, y - 3, 3.4f, 4, RGB(0x9EA5AC));
          disc(x + 5, y - 4, 1.6f, RGB(0x6B7278));
          break;
        case M_LIGHT:
          box((int)x - 8, (int)y - 8, 16, 16, 3, RGB(0x1E2A22));
          disc(x, y, 5.5f, RGB(0xFFF3B0));
          disc(x, y, 3, WHITE);
          break;
        case M_MAGNET:
          blob(x - 5, y - 7, x - 5, y + 1, 2.2f, 2.2f, RGB(0xE53935));
          blob(x + 5, y - 7, x + 5, y + 1, 2.2f, 2.2f, RGB(0xE53935));
          for (int i = 0; i < 4; i++) {
            static const float C[5][2] = {{-5, 1}, {-3.5f, 4.5f}, {0, 6}, {3.5f, 4.5f}, {5, 1}};
            blob(x + C[i][0], y + C[i][1], x + C[i + 1][0], y + C[i + 1][1], 2.2f, 2.2f, RGB(0xE53935));
          }
          fill((int)x - 7, (int)y - 8, 5, 3, RGB(0xCFD8DC)), fill((int)x + 3, (int)y - 8, 5, 3, RGB(0xCFD8DC));
          break;
        default: /* peaceful: a heart */
          disc(x - 3.2f, y - 2, 4, RGB(0xEC407A)), disc(x + 3.2f, y - 2, 4, RGB(0xEC407A));
          for (int t = 0; t < 7; t++) fill((int)x - 7 + t, (int)y + t, 15 - 2 * t, 1, RGB(0xEC407A));
          break;
      }
  }
}

static float ease(uint32_t born, int ms) { /* 0 to 1 with a little overshoot */
  int a = (int)(now - born);
  if (a >= ms) return 1;
  float t = (float)a / ms - 1;
  return 1 + t * t * (2.70158f * t + 1.70158f);
}

/* where fruit is drawn: between its last cell and this one */
static float fx_of(apple_t *a) { return ux(a->from) + (ux(a->p) - ux(a->from)) * prog; }
static float fy_of(apple_t *a) { return uy(a->from) + (uy(a->p) - uy(a->from)) * prog; }
static bool fruit_near(apple_t *a) { return near(a->p) || near(a->from); }

/* an item of fruit: a portal has a ring in its pair's color */
static void apple(apple_t *a) {
  float s = cs * ease(a->born, 260), x = fx_of(a), y = fy_of(a);
  if (a->kind) { /* a ring, and a smaller fruit in it */
    disc(x, y, s * 0.5f, RGB(RING[a->kind - 1]));
    disc(x, y, s * 0.35f, checker(a->p));
    s *= 0.72f;
  }
  if (MODE == M_WINGED && !a->kind) {
    float f = (float)((now >> 5) & 7) / 7, w = s * (0.12f + 0.1f * (f > 0.5f ? 1 - f : f));
    for (int i = -1; i <= 1; i += 2) blob(x + i * s * 0.22f, y - s * 0.1f, x + i * s * 0.48f, y - s * 0.3f, w, w * 0.6f, WHITE);
  }
  fruit(V.opt[O_FRUIT], x, y, s);
}

static void items(void) {
  for (int y = imax(qy0, 0); y <= imin(qy1, bh - 1) && !shade; y++)
    for (int x = imax(qx0, 0); x <= imin(qx1, bw - 1); x++) {
      int o = occ[y * bw + x];
      if (!(o & (O_WALL | O_STONE))) continue;
      int p = x | y << 8, m = (int)(cs * (1 - (p == wpop ? ease(wborn, 300) : 1)) / 2) + 1, w = cs - 2 * m;
      int bx = fx + x * cs + m, by = fy + y * cs + m;
      if (o & O_WALL) { /* a block of hedge */
        box(bx, by, w, w, cs / 5, mix(0, c_edge, 22));
        box(bx, by, w, w - cs / 7 - 1, cs / 5, c_edge);
      } else { /* stone */
        box(bx, by, w, w, cs / 4, RGB(0x7B8288));
        box(bx, by, w, w - cs / 7 - 1, cs / 4, RGB(0x9EA5AC));
      }
    }
  for (int i = 0; i < 6 && !shade; i++) { /* portals a snake is going through: a dark hole under it */
    int g = gate[i / 2][i & 1];
    if (g >= 0 && near(g) && (occ[at(g)] & O_BODY)) disc(ux(g), uy(g), cs * 0.36f, mix(RGB(RING[i / 2]), 0, 16));
  }
  for (int i = 0; i < 8; i++) {
    apple_t *a = &ap[i];
    if (!a->live || !fruit_near(a)) continue;
    if (shade) disc(fx_of(a), fy_of(a), cs * 0.36f * ease(a->born, 260), 0);
    else apple(a);
  }
}

/* the rings of portals the snake is going through, over it: it goes into
   one and comes out of the other */
static void rings(void) {
  for (int i = 0; i < 6; i++) {
    int g = gate[i / 2][i & 1];
    if (g < 0 || !near(g) || !(occ[at(g)] & O_BODY)) continue;
    float c = 1, sn = 0, r = cs * 0.43f;
    for (int k = 0; k < 16; k++) { /* 16 dots around, turning by 22.5 degrees */
      disc(ux(g) + c * r, uy(g) + sn * r, cs * 0.085f, RGB(RING[i / 2]));
      float c2 = c * 0.92388f - sn * 0.38268f;
      sn = sn * 0.92388f + c * 0.38268f, c = c2;
    }
  }
}

static float rad(float s) { return cs * (s >= 1.5f ? 0.45f : 0.27f + 0.12f * s); } /* thinner near the tail */

/* The snake from cell a to cell b (entered going d), from u0 to u1 of the
   way. Across an edge (wrapping) or a portal, a and b are not side by side:
   each gets its own half, cut at its cell's border. */
static void piece(int a, int b, int d, float u0, float u1, float r0, float r1, color c) {
  float sx = DX[d] * cs, sy = DY[d] * cs, ax = ux(a), ay = uy(a);
  bool side = (a & 255) + DX[d] == (b & 255) && (a >> 8) + DY[d] == (b >> 8);
  if (!side) cellclip(a);
  blob(ax + sx * u0, ay + sy * u0, ax + sx * u1, ay + sy * u1, r0, r1, c);
  if (side) return;
  ax = ux(b) - sx, ay = uy(b) - sy;
  cellclip(b);
  blob(ax + sx * u0, ay + sy * u0, ax + sx * u1, ay + sy * u1, r0, r1, c);
  fieldclip();
}

static void snake(int n) {
  snake_t *s = &S[n];
  color c = snake_color(n), tail = mix(0, c, 5);
  float p = prog, sh = s->tmove ? 1 - p : 0;
  int k = s->tl, L = s->len;
  fieldclip();
  if (s->tmove && solid(s, s->tl + MAXN - 1) && solid(s, s->tl) && (near(s->otail) || near(s->body[s->tl])))
    piece(s->otail, s->body[s->tl], s->ndir[s->tl] & 3, p, 1, rad(0), rad(sh), tail);
  for (int i = 0; i < L - 1; i++) {
    int k1 = (k + 1) % MAXN;
    float u = i == L - 2 ? p : 1;
    bool a = solid(s, k), b = solid(s, k1) || k1 == s->hd;
    if ((near(s->body[k]) || near(s->body[k1])) && a) {
      color ci = mix(c, tail, 32 * (i + 1) / L);
      if (b) piece(s->body[k], s->body[k1], s->ndir[k1] & 3, 0, u, rad(sh + i), rad(sh + i + u), ci);
      else if (MODE == M_CHEESE) disc(ux(s->body[k]), uy(s->body[k]), rad(sh + i), ci); /* round, by a hole */
    }
    k = k1;
  }
  int h = s->body[s->hd], b = s->body[(s->hd + MAXN - 1) % MAXN], d = s->ndir[s->hd] & 3, d0 = s->ndir[(s->hd + MAXN - 1) % MAXN] & 3;
  if (L == 1) b = s->tmove ? s->otail : h;
  if (near(h) || near(b)) {
    /* the head turns over the first third of a step */
    float w = p * 3 > 1 ? 1 : p * 3, ax = DX[d0] + (DX[d] - DX[d0]) * w, ay = DY[d0] + (DY[d] - DY[d0]) * w;
    float nn = 1 / __builtin_sqrtf(ax * ax + ay * ay + 1e-6f), r = cs * 0.45f;
    bool side = (b & 255) + DX[d] == (h & 255) && (b >> 8) + DY[d] == (h >> 8);
    if (L == 1 || !solid(s, s->hd + MAXN - 1)) { /* no neck: the head alone */
      if (!side) cellclip(h);
      disc(ux(h) - DX[d] * cs * (1 - p), uy(h) - DY[d] * cs * (1 - p), r, c);
      fieldclip();
    }
    if (!shade) {
      if (!side) cellclip(h);
      face(ux(h) - DX[d] * cs * (1 - p), uy(h) - DY[d] * cs * (1 - p), ax * nn, ay * nn, r);
      if (!side) {
        cellclip(b);
        face(ux(b) + DX[d] * cs * p, uy(b) + DY[d] * cs * p, ax * nn, ay * nn, r);
      }
    }
  }
  unclip();
}

/* Light: dark all over but around the heads, wider for a while after fruit */
static float light_r(void) {
  int a = (int)(now - lit_t);
  return cs * (2.4f + (a < 3000 ? 2.6f * (3000 - a) / 3000 : 0));
}
static void darkness(void) {
  float r = light_r(), r2 = r * r, e = cs * 0.8f, o2 = (r + e) * (r + e), hx[2], hy[2];
  for (int k = 0; k < ns; k++) { /* where the heads are drawn */
    snake_t *s = &S[k];
    int d = s->ndir[s->hd] & 3;
    hx[k] = ux(s->body[s->hd]) - DX[d] * cs * (1 - prog), hy[k] = uy(s->body[s->hd]) - DY[d] * cs * (1 - prog);
  }
  clip(fx, fy, bw * cs, bh * cs);
  for (int y = ky0; y < ky1; y++) {
    color *p = buf + (y - ry) * rw - rx;
    for (int x = kx0; x < kx1; x++) {
      float d2 = 1e9f;
      for (int k = 0; k < ns; k++) {
        float dx = x + 0.5f - hx[k], dy = y + 0.5f - hy[k], q = dx * dx + dy * dy;
        if (q < d2) d2 = q;
      }
      if (d2 <= r2) continue;
      int a = d2 >= o2 ? 27 : (int)(27 * (d2 - r2) / (o2 - r2));
      p[x] = mix(RGB(0x07100A), p[x], a);
    }
  }
  unclip();
}

/* ------------------------------------------------------------------ text and panels */
static int pass, ty0, ty1; /* 0: shapes into buf; 1: the text whose last row is in ty0..ty1 */
static int slen(const char *s) { return np_text_cells(s); } /* (Chinese letters take two cells) */
static void lab(const char *s, int x, int y, int large, color fg, color bg) {
  int b = y + (large ? 17 : 13);
  if (pass && b >= ty0 && b < ty1) eadk_display_draw_string(s, (eadk_point_t){(uint16_t)x, (uint16_t)y}, large, fg, bg);
}
static void clab(const char *s, int cx, int y, int large, color fg, color bg) {
  lab(s, cx - slen(s) * (large ? 10 : 7) / 2, y, large, fg, bg);
}
static char *itoa(char *o, int v) {
  char t[8];
  int n = 0;
  do t[n++] = (char)('0' + v % 10); while (v /= 10);
  while (n) *o++ = t[--n];
  *o = 0;
  return o;
}
static char *cat(char *o, const char *s) {
  while (*s) *o++ = *s++;
  *o = 0;
  return o;
}

static void card(int x, int y, int w, int h) {
  if (pass) return;
  box(x, y + 3, w, h, 10, RGB(0x1C6E9C));
  box(x, y, w, h, 10, C_CARD);
}
/* a Google blue button, with an icon: 0 none, 1 play, 2 gear, 3 dice, 4 back */
static void button(const char *s, int x, int y, int w, int h, bool on, int ic) {
  color c = on ? C_BTN : C_BTN2;
  int tx = x + w / 2 + (ic ? 8 : 0);
  if (!pass) {
    if (on) box(x - 2, y - 2, w + 4, h + 4, 8, WHITE);
    box(x, y, w, h, 6, c);
    int cx = tx - slen(s) * 5 - 16, cy = y + h / 2;
    if (ic == 1) tri(cx + 4, cy, 1, 0, 9, 5, WHITE);
    if (ic == 2) {
      for (int i = 0; i < 8; i++) {
        static const int8_t R[8][2] = {{6, 0}, {4, 4}, {0, 6}, {-4, 4}, {-6, 0}, {-4, -4}, {0, -6}, {4, -4}};
        disc(cx + R[i][0], cy + R[i][1], 1.6f, WHITE);
      }
      disc(cx, cy, 4.6f, WHITE), disc(cx, cy, 2, c);
    }
    if (ic == 3) {
      box(cx - 6, cy - 6, 12, 12, 2, WHITE);
      disc(cx - 3, cy - 3, 1.2f, c), disc(cx + 3, cy + 3, 1.2f, c), disc(cx, cy, 1.2f, c);
    }
    if (ic == 4) {
      blob(cx - 5, cy, cx + 5, cy, 1.1f, 1.1f, WHITE);
      blob(cx - 5, cy, cx - 1, cy - 4, 1.1f, 1.1f, WHITE), blob(cx - 5, cy, cx - 1, cy + 4, 1.1f, 1.1f, WHITE);
    }
  }
  clab(s, tx, y + (h - 18) / 2, 1, WHITE, c);
}

static const char *const ROWS[NOPT] = {T("Fruit"), T("Mode"), T("Count"), T("Speed"), T("Size"), T("Color"), T("Theme")};
static const char *const NAMES[] = {
  T("Apple"), T("Banana"), T("Pineapple"), T("Grapes"), T("Strawberry"), T("Cherries"),
  T("Classic"), T("Wall"), T("Portal"), T("Cheese"), T("Borderless"), T("Twin"), T("Winged"), T("Yin Yang"), T("Statue"),
  T("Light"), T("Magnet"), T("Peaceful"),
  "1", "3", "5",
  T("Normal"), T("Fast"), T("Slow"),
  T("Normal"), T("Small"), T("Large"),
  T("Blue"), T("Purple"), T("Pink"), T("Orange"), T("Yellow"), T("Teal"), T("White"), T("Black"),
  T("Day"), T("Night"), T("Snow"), T("Volcano"),
};
static const char *name_of(int o, int v) {
  int b = 0;
  for (int i = 0; i < o; i++) b += OPT_N[i];
  return NAMES[b + v];
}

/* the card of the title and of the end of a game: the scores, and the snake
   lying on the lawn */
static void title_card(void) {
  char t[8];
  const int X = 62, Y = 28, W = 195, H = 124, F = Y + H - 30; /* the lawn from F */
  card(X, Y, W, H);
  if (!pass) {
    fruit(V.opt[O_FRUIT], 124, Y + 22, 30);
    trophy(196, Y + 24, 1.35f, C_CARD);
    clip(X, F, W, Y + H - F);
    box(X, F - 10, W, Y + H - F + 10, 10, c_light);
    for (int j = 0; j < 2; j++)
      for (int i = j & 1; i < 13; i += 2) fill(X + i * 15, F + j * 15, 15, 15, c_dark); /* light in the round corners */
    clip(X, Y, W, H);
    color c = snake_color(0);
    blob(X - 10, F - 5, X + 118, F - 5, 10, 10, mix(0, c, 5));
    blob(X + 118, F - 5, X + 140, F - 17, 10, 12.5f, c);
    mouth = 0, dead = false;
    face(X + 142, F - 19, 0.9f, -0.45f, 12.5f);
    unclip();
  }
  itoa(t, played ? score : 0);
  clab(t, 124, Y + 40, 1, WHITE, C_CARD);
  itoa(t, *best());
  clab(t, 196, Y + 40, 1, WHITE, C_CARD);
  if (played && (won || newbest)) clab(won ? T("You win!") : T("New best!"), 150, Y + 62, 0, RGB(0xFFF59D), C_CARD);
}

/* the band on top, and the panel of the moment */
static void ui(void) {
  char t[NP_TEXT_EXTRA ? 48 : 24]; /* (a setting and its value: longer in other languages) */
  if (!pass) fruit(V.opt[O_FRUIT], 14, 12, 20), trophy(84, 13, 0.95f, c_bar);
  itoa(t, score);
  lab(t, 28, 3, 1, WHITE, c_bar);
  itoa(t, *best());
  lab(t, 98, 3, 1, WHITE, c_bar);
  clab(name_of(O_MODE, MODE), 262, 5, 0, mix(WHITE, c_bar, 20), c_bar);
  switch (state) {
    case S_READY: /* the arrow keys, until the first move */
      if (pass) break;
      box(116, 104, 88, 58, 10, mix(0, c_bar, 16));
      for (int d = 0; d < 4; d++) {
        int x = 160 + DX[d] * 26, y = d == 3 ? 119 : 146;
        box(x - 11, y - 11, 22, 22, 5, WHITE);
        tri(x + DX[d] * 4 + !DX[d] * 0, y + DY[d] * 4, DX[d] ? DX[d] : 0, DY[d], 8, 5, mix(0, c_bar, 16));
      }
      break;
    case S_TITLE:
      if (!pass) dim();
      title_card();
      button(T("Play"), 62, 160, 195, 26, sel == 0, 1);
      button(T("Settings"), 62, 192, 195, 26, sel == 1, 2);
      if (!pass) box(62, 223, 195, 16, 8, mix(0, c_bar, 18));
      clab(T("Based on Tatone26's version"), 160, 224, 0, RGB(0xC8D6BE), mix(0, c_bar, 18));
      break;
    case S_SET: {
      const int X = 24, Y = 28, W = 272;
      if (!pass) dim();
      card(X, Y, W, 172);
      cat(cat(cat(t, ROWS[row < NOPT ? row : 0]), T(": ")), name_of(row < NOPT ? row : 0, V.opt[row < NOPT ? row : 0]));
      if (row < NOPT) clab(t, 160, Y + 5, 1, WHITE, C_CARD);
      else clab(T("Settings"), 160, Y + 5, 1, WHITE, C_CARD);
      if (!pass) {
        blob(X + 14, Y + 14, X + 24, Y + 14, 1.3f, 1.3f, WHITE);
        blob(X + 14, Y + 14, X + 19, Y + 9, 1.3f, 1.3f, WHITE), blob(X + 14, Y + 14, X + 19, Y + 19, 1.3f, 1.3f, WHITE);
        for (int o = 0; o < NOPT; o++) {
          int y = Y + 36 + o * 21, n = OPT_N[o], x0 = 160 - n * 21 / 2 + 10;
          color bg = o == row ? C_CARD2 : C_CARD;
          if (o == row) box(X + 6, y - 10, W - 12, 21, 6, C_CARD2);
          for (int v = 0; v < n; v++) {
            int x = x0 + v * 21;
            bool on = V.opt[o] == v;
            if (on) box(x - 10, y - 10, 21, 21, 5, o == row ? WHITE : RGB(0xB3E5FC));
            icon(o, v, x + 0.5f, y + 0.5f, on ? (o == row ? WHITE : RGB(0xB3E5FC)) : bg);
            if (!on) wash(x - 10, y - 10, 21, 21, bg, 15);
          }
        }
      }
      static const char *const B[3] = {T("Play"), T("Shuffle"), T("Reset")};
      for (int i = 0; i < 3; i++) button(B[i], 24 + i * 94, 208, 84, 26, row == NOPT && sel == i, i ? i + 2 : 1);
      break;
    }
    case S_PAUSE:
      if (!pass) dim();
      card(70, 44, 180, 164);
      clab(T("Paused"), 160, 56, 1, WHITE, C_CARD);
      button(T("Resume"), 86, 88, 148, 28, sel == 0, 1);
      button(T("Restart"), 86, 124, 148, 28, sel == 1, 0);
      button(T("Quit game"), 86, 160, 148, 28, sel == 2, 0);
      break;
    case S_QUIT:
      if (!pass) dim();
      card(76, 72, 168, 104);
      clab(T("Quit game?"), 160, 88, 1, WHITE, C_CARD);
      button(T("No"), 92, 128, 60, 28, sel == 0, 0);
      button(T("Yes"), 168, 128, 60, 28, sel == 1, 0);
      break;
  }
}

/* Draws a part of the screen, band by band; text comes right after the band
   that holds its last row. */
static void area(int x, int y, int w, int h) {
  int band = BUFPX / w;
  for (int y0 = y; y0 < y + h; y0 += band) {
    rx = x, ry = y0, rw = w, rh = imin(band, y + h - y0);
    qx0 = fl(rx - fx), qx1 = fl(rx + rw - 1 - fx), qy0 = fl(ry - fy), qy1 = fl(ry + rh - 1 - fy);
    unclip();
    ground();
    shade = true, oy = cs * 0.14f; /* the shadows first */
    items();
    for (int k = 0; k < ns; k++) snake(k);
    shade = false, oy = 0;
    items();
    for (int k = 0; k < ns; k++) snake(k);
    rings();
    if (MODE == M_LIGHT && state != S_TITLE && state != S_SET) darkness();
    pass = 0;
    ui();
    eadk_display_push_rect((eadk_rect_t){(uint16_t)rx, (uint16_t)ry, (uint16_t)rw, (uint16_t)rh}, buf);
    if (w == 320) pass = 1, ty0 = ry, ty1 = ry + rh, ui();
  }
}

/* In play: only the cells that change, and those that changed last time
   (so they are left in their final state); each with the one below, where
   shadows fall. */
static uint16_t dcur[MAXN], dprev[MAXN];
static uint8_t seen[MAXN], frame; /* seen: the frame a cell was last marked in */
static int ncur, nprev;
static void mark(int p) {
  if ((p & 255) >= bw || (p >> 8) >= bh || seen[at(p)] == frame) return;
  seen[at(p)] = frame, dcur[ncur++] = (uint16_t)p;
}
static void mark2(int p) { mark(p), mark(p + 256); }
static void cells(void) {
  if (!++frame)
    for (int i = 0; i < MAXN; i++) seen[i] = 0;
  ncur = 0;
  for (int k = 0; k < ns; k++) {
    snake_t *s = &S[k];
    for (int i = 0; i < 3 && i < s->len; i++) mark2(s->body[(s->hd + MAXN - i) % MAXN]), mark2(s->body[(s->tl + i) % MAXN]);
    if (s->tmove) mark2(s->otail);
  }
  for (int i = 0; i < 8; i++)
    if (ap[i].live && (ap[i].eat || ap[i].from != ap[i].p || (int)(now - ap[i].born) < 300)) mark2(ap[i].p), mark2(ap[i].from);
  if (wpop >= 0 && (int)(now - wborn) < 350) mark(wpop);
  if (MODE == M_LIGHT) { /* the edge of the light: the cells it crosses (inside and out, nothing changes) */
    float lr = light_r() / cs, a = lr - 1.3f, b = lr + 2.1f;
    int r = (int)b + 1;
    for (int k = 0; k < ns; k++) {
      snake_t *s = &S[k];
      int h = s->body[s->hd], x0 = (h & 255), y0 = h >> 8, d = s->ndir[s->hd] & 3;
      float cx = x0 - DX[d] * (1 - prog), cy = y0 - DY[d] * (1 - prog);
      for (int y = imax(0, y0 - r); y <= imin(bh - 1, y0 + r); y++)
        for (int x = imax(0, x0 - r); x <= imin(bw - 1, x0 + r); x++) {
          float q = (x - cx) * (x - cx) + (y - cy) * (y - cy);
          if (q >= a * a * (a > 0) && q <= b * b) mark(x | y << 8);
        }
    }
  }
  int n = ncur;
  for (int i = 0; i < nprev; i++) mark(dprev[i]);
  for (int i = 0; i < ncur; i++) {
    int p = dcur[i];
    area(fx + (p & 255) * cs, fy + (p >> 8) * cs, cs, cs);
  }
  for (nprev = 0; nprev < n; nprev++) dprev[nprev] = dcur[nprev]; /* only this frame's own */
}

static void save(void) {
  V.magic = 'S', V.version = 2;
  ef_write(SAVE_NAME, &V, sizeof V);
}

/* the arrow keys, or 6 2 4 8, as directions */
static const uint32_t DIR_KEYS[4] = {KEY(eadk_key_right) | KEY(eadk_key_six), KEY(eadk_key_down) | KEY(eadk_key_two),
                                     KEY(eadk_key_left) | KEY(eadk_key_four), KEY(eadk_key_up) | KEY(eadk_key_eight)};
static const uint8_t OPT_DEF[NOPT] = {0, 0, 0, 0, 0, 0, 0};

/* a turn for snake k (Yin Yang: the second turns the other way) */
static void ask(int i) {
  for (int k = 0; k < ns; k++) {
    snake_t *s = &S[k];
    int d = k ? i ^ 2 : i, l = s->nq ? s->q[s->nq - 1] : s->ndir[s->hd] & 3;
    if (s->nq < 2 && d != l && (d != (l ^ 2) || s->len == 1)) s->q[s->nq++] = (uint8_t)d;
  }
}

int main(void) {
  np_app_begin();
  now = eadk_timing_millis();
  seed = eadk_random() ^ now ^ 0x9E3779B9u;
  if (!seed) seed = 1;
  uint32_t n = 0;
  const uint8_t *d = ef_read(SAVE_NAME, &n);
  if (d && n == sizeof V && d[0] == 'S' && d[1] == 2) {
    for (uint32_t i = 0; i < n; i++) ((uint8_t *)&V)[i] = d[i];
    for (int i = 0; i < NBEST; i++)
      if (V.best[i] > MAXN) V.best[i] = 0;
  }
  for (int i = 0; i < NOPT; i++)
    if (V.opt[i] >= OPT_N[i]) V.opt[i] = OPT_DEF[i];
  new_game();
  state = S_TITLE;
  bool full = true, hdr = false, dirty = false;
  int resume = S_PLAY, flashed = -1;
  uint32_t last = now, die_t = 0, rep_t = 0;
  uint32_t held = keys();
  for (;;) {
    now = eadk_timing_millis();
    int dt = (int)(now - last);
    dt = dt < 0 ? 0 : dt > 100 ? 100 : dt;
    last = now;
    uint32_t k = keys(), edge = k & ~held, hit = edge, arrows = DIR_KEYS[0] | DIR_KEYS[1] | DIR_KEYS[2] | DIR_KEYS[3];
    held = k;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    int dk = -1; /* a direction, repeated while held (menus) */
    if (hit & arrows) rep_t = now + 350;
    else if ((k & arrows) && (int)(now - rep_t) >= 0) hit |= k & arrows, rep_t = now + 90;
    for (int i = 3; i >= 0; i--)
      if (hit & DIR_KEYS[i]) dk = i;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe)), back = hit & KEY(eadk_key_back), redraw = true;
    switch (state) {
      case S_TITLE:
        if (dk == 1 || dk == 3) sel ^= 1;
        else if (ok && sel) state = S_SET, row = 0, sel = 0;
        else if (ok) state = S_READY, played = false, new_game();
        else if (back) resume = S_TITLE, state = S_QUIT, sel = 0; /* "Quit game?" from the title */
        else redraw = false;
        break;
      case S_SET:
        if (dk == 1 || dk == 3) {
          row = (row + (dk == 1 ? 1 : NOPT)) % (NOPT + 1);
        } else if ((dk == 0 || dk == 2) && row == NOPT) {
          sel = (sel + (dk ? 2 : 1)) % 3;
        } else if (dk == 0 || dk == 2) {
          V.opt[row] = (uint8_t)((V.opt[row] + (dk ? OPT_N[row] - 1 : 1)) % OPT_N[row]);
          new_game(), dirty = true;
        } else if (ok && row == NOPT && sel) { /* Shuffle, Reset */
          for (int i = 0; i < NOPT; i++) V.opt[i] = sel == 1 ? (uint8_t)(rnd() % OPT_N[i]) : OPT_DEF[i];
          new_game(), dirty = true;
        } else if (ok) {
          state = S_READY, played = false, new_game();
          if (dirty) save(), dirty = false;
        } else if (back) {
          state = S_TITLE, sel = 1, played = false;
          if (dirty) save(), dirty = false;
        } else {
          redraw = false;
        }
        break;
      case S_READY:
      case S_PLAY:
        redraw = false;
        if (back) {
          resume = state, state = S_PAUSE, sel = 0, redraw = true;
          break;
        }
        for (int i = 0; i < 4; i++)
          if (edge & DIR_KEYS[i]) ask(i);
        if (state == S_READY) {
          if (!S[0].nq && !(edge & (arrows | KEY(eadk_key_ok) | KEY(eadk_key_exe)))) break;
          state = S_PLAY, full = true;
        }
        {
          int ate = score;
          bool crash = false, left = false;
          for (acc += dt; acc >= step_ms() && !crash; acc -= step_ms()) crash = !step();
          hdr |= score != ate;
          for (int i = 0; i < 8; i++) left |= ap[i].live && !ap[i].eat;
          if (crash || !left) { /* crashed, or no room left for fruit: the board is full */
            state = S_DIE, die_t = now, acc = step_ms(), dead = crash, won = !crash, newbest = score > *best(), played = true;
            if (newbest) *best() = (uint16_t)score, save();
          }
          prog = (float)acc / step_ms();
        }
        break;
      case S_PAUSE:
        if (dk == 1 || dk == 3) sel = (sel + (dk == 1 ? 1 : 2)) % 3;
        else if (back || (ok && !sel)) state = S_READY; /* it waits for a key: no surprise start */
        else if (ok && sel == 1) new_game(), state = S_READY;
        else if (ok) state = S_QUIT, sel = 0;
        else redraw = false;
        break;
      case S_QUIT:
        if (dk == 0 || dk == 2) sel ^= 1;
        else if (ok && sel) goto quit;
        else if (ok || back) state = resume == S_TITLE ? S_TITLE : S_PAUSE, sel = resume == S_TITLE ? 0 : 2;
        else redraw = false;
        break;
      case S_DIE:
        redraw = (int)(now - die_t) > 1100;
        if (redraw) state = S_TITLE, sel = 0, flash = false;
        break;
    }
    if (state == S_TITLE && !played) dead = won = false;
    full |= redraw;
    if (prog > 1) prog = 1;
    if (state != S_PLAY && state != S_DIE && state != S_READY) prog = 0;
    /* the mouth opens on fruit just ahead, and shuts after a bite */
    snake_t *s = &S[0];
    int h = s->body[s->hd], ahead = ((h & 255) + DX[s->ndir[s->hd] & 3]) | ((h >> 8) + DY[s->ndir[s->hd] & 3]) << 8;
    mouth = 1 - (int)(now - chew_t) / 160.0f;
    for (int i = 0; i < 8; i++)
      if (ap[i].live && (ap[i].p == h || (ap[i].p == ahead && !ap[i].eat && prog > mouth))) mouth = ap[i].p == h ? 1 : prog;
    if (dead) mouth = 0;
    eadk_display_wait_for_vblank();
    if (full) {
      area(0, 0, 320, 240);
      full = hdr = false, nprev = 0, flashed = -1;
    } else if (state == S_DIE) {
      int f = (int)(now - die_t) / 150;
      if (f != flashed && f < 7) { /* the snake flashes */
        flashed = f, flash = f & 1;
        area(0, HDR, 320, 240 - HDR);
      }
    } else if (state == S_PLAY || state == S_READY) {
      if (hdr) area(0, 0, 320, HDR), hdr = false;
      cells();
    }
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
quit:
  if (dirty) save();
  return np_app_end();
}
