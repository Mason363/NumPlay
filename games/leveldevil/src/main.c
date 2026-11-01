/* Level Devil: run to the door. The level has other plans.
 *
 * Unept's troll platformer (2023): sixteen worlds of five short rooms in flat colours, with a tiny
 * black runner. Floors drop away under you, spikes pop up, walls push, the door runs off, the
 * controls turn round, gravity flips... The rooms are the game's own: their tiles, their actors and
 * what their scripts do (tools/rooms.py, from tools/scenes.json).
 *
 * A room: its ground (tiles of 16 pixels, 33 x 15), its objects (blocks, spikes, saws, springs,
 * doors, triggers, coins, warps, buttons...) and its script: steps, each set off by an event (the
 * room starting, the runner touching a trigger, a key) after so many 60ths of a second, that move,
 * grow, show or hide objects, or change the runner (its speed, its jump, its size, gravity). The
 * game steps 60 times a second, in the scene's pixels; it is drawn at 3/4 (the screen shows x 50
 * to 477 of the 528 pixels, all 240 high), in the launcher's strips (launcher/src/gfx.c).
 *
 * All it shows is the game's own: its pictures and fonts (src/pictures.h, tools/pictures.py), its
 * title and its 1 Player card, its map of the worlds with their portals, the jaws with the Devil's
 * eyes that shut between two screens, its buttons. */
#include <eadk.h>
#include "../../../launcher/src/gfx.h"
#include "../../common/np_text.h"
#include "../../common/epsilon_app.h"
#include "../../common/epsilon_files.h"

#ifndef HOST /* (a computer build of the tests has no app header) */
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Level Devil";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}

#define KEY(k) (1ull << (k))
#define ROOMS 5
#define LEVELS 16

/* the packed rooms (tools/rooms.py), column by column: 80 rooms' modes and runners, their ground
   (33 columns of 15 bits), how many spans each has, the spans' fields, how many steps, the steps' */
#include "rooms.inc" /* data, ROOM_BYTES, SPANS, STEPS */
#define D_GROUND (5 * 80)
#define D_NSPAN (D_GROUND + 66 * 80)
#define D_KIND (D_NSPAN + 80)
#define D_FLAG (D_KIND + SPANS)
#define D_EV (D_FLAG + SPANS)
#define D_X (D_EV + SPANS)
#define D_Y (D_X + 2 * SPANS)
#define D_W (D_Y + 2 * SPANS)
#define D_H (D_W + SPANS)
#define D_N (D_H + SPANS)
#define D_NSTEP (D_N + SPANS)
#define D_SEV (D_NSTEP + 80)
#define D_ST (D_SEV + STEPS)
#define D_CODE (D_ST + 2 * STEPS)
#define D_OBJ (D_CODE + STEPS)
#define D_SN (D_OBJ + STEPS)
#define D_COND (D_SN + STEPS)
#define D_A (D_COND + STEPS)
#define D_B (D_A + 2 * STEPS)
#define D_C (D_B + 2 * STEPS)
#define D_K (D_C + 2 * STEPS)

enum { K_BLOCK, K_SPIKE, K_SAW, K_SPRING, K_DOOR, K_TRIG, K_COIN, K_BAD, K_WARP, K_BUTTON, K_THRU, K_GSPIKE, K_DEATH, K_SIGN };
#define F_HIDE 1
#define F_GHOST 2  /* nothing to stand on */
#define F_OFF 4    /* not there (yet, or any more) */
#define F_BIG 8    /* a saw's second picture, twice as big */
#define F_USED 16  /* a trigger gone off, a coin taken, a button pressed */
#define F_ICE 32
#define F_HOP 64   /* thrown up, falling back */
enum { R_WRAP = 1, R_WRAPY = 2, R_SWAP = 4, R_FLAPPY = 16, R_GRAVITY = 32 };
enum { O_VEL, O_MOVE, O_SET, O_GROW, O_FLAG, O_SPAWN, O_WARP, O_HERO, O_GRAV, O_HGROW, O_SHAKE, O_FIRE, O_REARM,
       O_TOWARD, O_RULE, O_ACCEL, O_HOP, O_KEY };
enum { H_RUN, H_JUMP, H_MAXUP, H_MAXDOWN, H_BACK, H_DOWNJUMP, H_DIR, H_SLIDE, H_PUSHX };
enum { U_NONE, U_PATROL, U_FOLLOW, U_STOPX, U_WRAPX };
#define EV_KEY 9
#define EV_MOVE 10
#define EVENTS 11

/* the colours of the sixteen worlds: background, ground */
static const uint32_t colors[LEVELS][2] = {
  {0xFEB854, 0x996B07}, {0xF5B13A, 0xBE7708}, {0xF8C750, 0xCD8507}, {0xF8C750, 0xE37B07},
  {0xE4C448, 0xE36821}, {0xFD8E57, 0xD65C2C}, {0xFECB30, 0xD34F2B}, {0xED9970, 0xB44D2A},
  {0xCDB67D, 0xA24829}, {0xF48B7C, 0x844934}, {0xFFBC73, 0x754E39}, {0xF49E7C, 0x624C40},
  {0xEA7851, 0x4A3B35}, {0xCD7647, 0x40312A}, {0xAB7050, 0x401D0D}, {0xCD7647, 0x2F1509},
};
#define BLACK 0

/* leveldevil.sav */
static struct {
  uint8_t magic, version, flags, open; /* flags: none now; open: the levels open, 1 to 16 */
  uint8_t level, room;                 /* the level last played, and the room reached in it */
  uint16_t done;                       /* the levels cleared, a bit each */
  uint32_t deaths;                     /* in all */
  uint16_t run, sel;                   /* deaths since that level began; the door picked last */
  uint16_t best[LEVELS];               /* the fewest deaths each level was cleared with */
} sv;
#define SAVE_NAME "leveldevil.sav"
#define COPY_NAME "leveldevil_saves.py"

typedef struct {
  int32_t x, y, tx, ty;   /* its centre, and where a move takes it (256ths of a pixel) */
  int16_t w, h, gw, gh;   /* half its size, and what it grows to (16ths of a pixel) */
  int16_t vx, vy, ax, ay; /* 256ths of a pixel a step; and a step each step */
  int16_t rest;           /* thrown up: the y it comes down to (pixels) */
  uint16_t ml, gl;        /* steps left of its move, of its growing */
  uint8_t kind, f, ev, t; /* kind (and its turn, 90 degrees a step, in bits 4 and 5); flags; t: a spring's push */
} obj_t;
typedef struct {
  uint16_t t;
  uint8_t ev, cond, code, o, done;
  int16_t a, b, c;
} step_t;
static obj_t ob[120];
static step_t st[240];
static struct {
  uint8_t o, type;
  int16_t p, q;
} rule[12];
static int no, ns, nr;
static uint16_t gcol[33];

static color_t strip[SCREEN_W * STRIP_H];
static int level, rnum, ri, scr, ov, sel, modes, frame, shake, shake_n;
static uint8_t cnt[EVENTS];
static uint16_t at_[EVENTS];
/* the runner: its box's corner and its speed (256ths of a pixel, a step), its size (pixels) */
static int32_t hx, hy, vx, vy;
static int hw, hh, hsx, hsy, tsx, tsy, hgl;
static int run, jumpv, maxup, maxdown, back, downjump, dir, slide, pushx, gx, gy, keyev;
static int face, ground, coyote, jumping, anim, dead, won, frozen, born, started, flapped, held_in, waiting;
static uint32_t seed, now;
static color_t bg, fg;
static struct {
  int32_t x, y;
  int16_t vx, vy;
} bits[8], spark[3]; /* the runner in pieces (or coming together: where from, and its place about the middle);
                         the sparks of a coin taken */
static int spark_t; /* (steps the sparks have left) */
enum { SC_TITLE, SC_MAP, SC_PLAY, SC_END };
enum { OV_NONE, OV_PAUSE, OV_QUIT };
enum { I_LEFT = 1, I_RIGHT = 2, I_UP = 4, I_DOWN = 8 };

static uint32_t rnd(void) {
  seed ^= seed << 13;
  seed ^= seed >> 17;
  return seed ^= seed << 5;
}
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }
static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }
static int iabs(int v) { return v < 0 ? -v : v; }
static int rd16(int at) { return (int16_t)(data[at] | data[at + 1] << 8); }
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
/* The save, and a copy of it in a Python script: installing apps from the NumWorks website keeps only
   Python scripts, so the save comes back from the copy when the file itself is gone. The copy is a
   comment line "#>leveldevil.sav:base64" (the way NumPlay and the big games keep theirs). */
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#define TAG "#>" SAVE_NAME ":" /* the copy's line (found anywhere: an older copy has a longer head) */
static const char copy_head[] = "# Level Devil's saves: keep this file\n" TAG;
static void save(void) {
  static char copy[sizeof copy_head + 72];
  const uint8_t *d = (const uint8_t *)&sv;
  char *p = copy;
  sv.magic = 'L', sv.version = 1;
  ef_write(SAVE_NAME, &sv, sizeof sv);
  *p++ = 0; /* (the script's status byte) */
  for (const char *h = copy_head; *h;) *p++ = *h++;
  for (unsigned i = 0; i < sizeof sv; i += 3) { /* (48 bytes: no padding) */
    uint32_t v = (uint32_t)d[i] << 16 | d[i + 1] << 8 | d[i + 2];
    for (int k = 18; k >= 0; k -= 6) *p++ = b64[v >> k & 63];
  }
  *p++ = '\n', *p++ = 0;
  ef_write(COPY_NAME, copy, (uint32_t)(p - copy));
}
/* the save back from the copy, if only the copy is left: the line after its tag */
static void restore(void) {
  const char *tag = copy_head + sizeof copy_head - sizeof TAG;
  uint32_t len, j = 0, m = 0;
  const uint8_t *c;
  uint8_t out[sizeof sv];
  if (ef_read(SAVE_NAME, &len) || !(c = ef_read(COPY_NAME, &len))) return;
  while (m < sizeof TAG - 1 && j + sizeof TAG - 1 <= len) m = c[j + m] == (uint8_t)tag[m] ? m + 1 : (j++, 0u);
  if ((j += sizeof TAG - 1) + 64 > len) return;
  for (unsigned i = 0; i < sizeof sv; i += 3, j += 4) {
    uint32_t v = 0;
    for (int k = 0; k < 4; k++) {
      int x = 0;
      while (x < 64 && (uint8_t)b64[x] != c[j + k]) x++;
      if (x == 64) return;
      v = v << 6 | (uint32_t)x;
    }
    out[i] = (uint8_t)(v >> 16), out[i + 1] = (uint8_t)(v >> 8), out[i + 2] = (uint8_t)v;
  }
  ef_write(SAVE_NAME, out, sizeof out);
}
#define PS(v) ((v) * 64 / 15) /* pixels a second: 256ths of a pixel a step */
#define PS2(v) ((v) * 64 / 900) /* pixels a second, each second: 256ths a step, each step */

/* ------------------------------------------------------------ the room */
static int cell(int tx, int ty) { return tx >= 0 && tx < 33 && ty >= 0 && ty < 15 && gcol[tx] >> ty & 1; }
/* any ground in the pixels [x0, x1) x [y0, y1)? */
static int ground_in(int x0, int y0, int x1, int y1) {
  for (int tx = x0 >> 4; tx <= (x1 - 1) >> 4; tx++)
    for (int ty = y0 >> 4; ty <= (y1 - 1) >> 4; ty++)
      if (cell(tx, ty)) return 1;
  return 0;
}
static void box(const obj_t *o, int *b) { /* its box in pixels: x0, y0, x1, y1 */
  int w = o->w >> 4, h = o->h >> 4;
  b[0] = (o->x >> 8) - w, b[1] = (o->y >> 8) - h, b[2] = (o->x >> 8) + w, b[3] = (o->y >> 8) + h;
}
static int hit(const int *b, int x0, int y0, int x1, int y1) { return x0 < b[2] && b[0] < x1 && y0 < b[3] && b[1] < y1; }
static int there(const obj_t *o) { return !(o->f & F_OFF); }
static int solid(const obj_t *o) { return (o->kind & 15) == K_BLOCK && !(o->f & (F_GHOST | F_OFF)) && o->w >= 8 && o->h >= 8; }

/* what the runner's box would hit at (x, y): the ground, a block, the top of a one-way tile it is
   above (feet: where its feet were, or -1); 0 for nothing, 1 for the ground, else 2 + the object */
static int blocked(int x, int y, int feet) {
  int b[4];
  if (ground_in(x, y, x + hw, y + hh)) return 1;
  for (int k = 0; k < no; k++) {
    obj_t *o = &ob[k];
    box(o, b);
    if (solid(o) || ((o->kind & 15) == K_THRU && there(o) && feet >= 0 && feet <= b[1]))
      if (hit(b, x, y, x + hw, y + hh)) return 2 + k;
  }
  return 0;
}
/* moves the runner d along an axis, a pixel at a time; what stopped it, or 0 */
static int step_axis(int32_t *p, int32_t d, int axis) {
  int a = *p >> 8, e = (*p + d) >> 8, s = e > a ? 1 : -1, w;
  for (; a != e; a += s)
    if ((w = axis ? blocked(hx >> 8, a + s, s > 0 ? a + hh : -1) : blocked(a + s, hy >> 8, -1))) {
      *p = a * 256 + (s > 0 ? 255 : 0);
      return w;
    }
  *p += d;
  return 0;
}
/* out of what it is in, a pixel at a time, the nearest way; false when it cannot get out */
static int unstick(void) { /* the nearest free place, up, down, left or right first */
  for (int d = 0; d < 64; d++)
    for (int j = 0; j <= d; j++)
      for (int i = 0; i < 8; i++) {
        int a = i & 4 ? -j : j, b = i & 1 ? d : -d;
        int x = (hx >> 8) + (i & 2 ? b : a), y = (hy >> 8) + (i & 2 ? a : b);
        if (!blocked(x, y, -1)) {
          hx = x * 256 + (hx & 255), hy = y * 256 + (hy & 255);
          return 1;
        }
      }
  return 0;
}

static void fire(int e) {
  cnt[e]++, at_[e] = (uint16_t)frame;
  for (int i = 0; i < ns; i++)
    if (st[i].ev == e) st[i].done = 0;
}

static void size_hero(void) { /* its box from its scale, about its centre */
  int w = imax(1, 5 * hsx / 100), h = imax(1, 13 * hsy / 100);
  hx -= (w - hw) * 128, hy -= (h - hh) * 128, hw = w, hh = h;
}

static void reset(void) { /* the room as it starts */
  int s0 = 0, r0 = 0;
  for (int i = 0; i < ri; i++) s0 += data[D_NSPAN + i], r0 += data[D_NSTEP + i];
  const uint8_t *h = data + ri * 5;
  modes = h[0];
  hsx = hsy = tsx = tsy = 100, hgl = 0, hw = 5, hh = 13;
  hx = (rd16(ri * 5 + 1) + 5) * 256, hy = (rd16(ri * 5 + 3) + 3) * 256, vx = vy = 0;
  for (int i = 0; i < 33; i++) gcol[i] = (uint16_t)rd16(D_GROUND + ri * 66 + 2 * i);
  no = nr = 0;
  for (int s = s0; s < s0 + data[D_NSPAN + ri]; s++) {
    int k = data[D_KIND + s];
    for (int i = 0; i < data[D_N + s]; i++) {
      obj_t *o = &ob[no++], z = {0};
      *o = z;
      o->kind = (uint8_t)(k & 63), o->f = data[D_FLAG + s], o->ev = data[D_EV + s];
      o->x = o->tx = (rd16(D_X + 2 * s) + (k & 64 ? 0 : 16 * i)) * 256;
      o->y = o->ty = (rd16(D_Y + 2 * s) + (k & 64 ? 16 * i : 0)) * 256;
      o->w = o->gw = (int16_t)(data[D_W + s] * 16), o->h = o->gh = (int16_t)(data[D_H + s] * 16);
    }
  }
  ns = 0;
  for (int r = r0; r < r0 + data[D_NSTEP + ri]; r++) {
    int o0 = data[D_OBJ + r], k = rd16(D_K + 2 * r);
    for (int i = 0; i < data[D_SN + r]; i++) {
      step_t *p = &st[ns++];
      p->ev = data[D_SEV + r], p->cond = data[D_COND + r], p->code = data[D_CODE + r];
      p->o = (uint8_t)(o0 == 255 ? 255 : o0 + i), p->done = 1;
      p->t = (uint16_t)(rd16(D_ST + 2 * r) + (k ? ((ob[o0 + i].x - ob[o0].x) >> 8) * k / 256 : 0));
      p->a = (int16_t)rd16(D_A + 2 * r), p->b = (int16_t)rd16(D_B + 2 * r), p->c = (int16_t)rd16(D_C + 2 * r);
    }
  }
  for (int e = 0; e < EVENTS; e++) cnt[e] = 0;
  int fl = modes & R_FLAPPY;
  run = PS(fl ? 70 : 80), jumpv = PS(fl ? 251 : 188), maxup = PS(fl ? 9999 : 300), maxdown = PS(fl ? 300 : 200);
  gx = 0, gy = PS2(fl ? 1200 : 800), back = downjump = slide = pushx = 0, dir = 1, keyev = -1;
  face = 1, ground = coyote = jumping = dead = won = frozen = started = flapped = waiting = 0, born = 18;
  frame = 0, shake = 0;
  for (int i = 0; i < 8; i++) { /* the game's eight pieces, from up to 40 pixels away */
    bits[i].x = hx + hw * 128 + ((int)(rnd() % 81) - 40) * 256, bits[i].y = hy + hh * 128 + ((int)(rnd() % 81) - 40) * 256;
    bits[i].vx = (int16_t)(rnd() % 7) - 3, bits[i].vy = (int16_t)(rnd() % 11) - 5;
  }
  fire(0);
}
static void enter(void) { /* room rnum of the level */
  ri = level * ROOMS + rnum;
  bg = gfx_rgb(colors[level][0]), fg = gfx_rgb(colors[level][1]);
  reset();
}

/* the runner in eight pieces, thrown up at 300 to 400 pixels a second, anywhere from left to right */
static const int16_t sine[33] = {0,   25,  50,  74,  98,  121, 142, 162, 181, 198, 213, 226, 237, 245, 251, 255, 256,
                                 255, 251, 245, 237, 226, 213, 198, 181, 162, 142, 121, 98,  74,  50,  25,  0};
static int sine_of(int d) { /* a 64th of a turn: 256ths */
  d &= 63;
  return d <= 32 ? sine[d] : -sine[d - 32];
}
static void die(void) {
  if (dead || won) return;
  dead = 1, shake = 5, shake_n = 30;
  sv.deaths++, sv.run++;
  for (int i = 0; i < 8; i++) {
    int a = (int)(rnd() % 33), v = PS(300 + (int)(rnd() % 101));
    bits[i].x = hx + hw * 128, bits[i].y = hy + hh * 128;
    bits[i].vx = (int16_t)(v * sine_of(a + 16) / 256), bits[i].vy = (int16_t)(-v * sine[a] / 256);
  }
  save();
}

/* a step of the script */
static void exec(const step_t *p) {
  obj_t *o = &ob[p->o == 255 ? 0 : p->o];
  int a = p->a, b = p->b, c = p->c, big = o->f & F_BIG ? 2 : 1;
  switch (p->code) {
    case O_VEL: o->vx = (int16_t)PS(a), o->vy = (int16_t)PS(b), o->ml = 0; break;
    case O_MOVE:
      o->tx = o->x + a * 256, o->ty = o->y + b * 256, o->ml = (uint16_t)c;
      if (!c) o->x = o->tx, o->y = o->ty;
      break;
    case O_SET:
      if (c & 1) o->x = o->tx = a * 256;
      if (c & 2) o->y = o->ty = b * 256;
      break;
    case O_GROW:
      o->gw = (int16_t)(a * 16 * big), o->gh = (int16_t)(b * 16 * big), o->gl = (uint16_t)c;
      if (!c) o->w = o->gw, o->h = o->gh;
      break;
    case O_FLAG:
      if (a & ~o->f & F_BIG) o->w *= 2, o->h *= 2, o->gw *= 2, o->gh *= 2;
      o->f = (uint8_t)((o->f | a) & ~b);
      break;
    case O_SPAWN:
      o->f &= ~F_OFF;
      if (a != 255) o->x = o->tx = ob[a].x + b * 256, o->y = o->ty = ob[a].y + c * 256;
      break;
    case O_WARP: /* (the runner in pieces, spreading at 250 pixels a second, then coming together over there) */
      for (int i = 0; i < 8; i++) {
        int d = (int)(rnd() % 64);
        bits[i].x = hx + hw * 128 + ((int)(rnd() % 5) - 2) * 256, bits[i].y = hy + hh * 128 + ((int)(rnd() % 11) - 5) * 256;
        bits[i].vx = (int16_t)(PS(250) * sine_of(d + 16) / 256), bits[i].vy = (int16_t)(-PS(250) * sine_of(d) / 256);
      }
      hx = (a + 5) * 256, hy = (b + 3) * 256, vx = vy = 0, frozen = 45;
      break;
    case O_HERO:
      if (a == H_RUN) run = PS(b);
      if (a == H_JUMP) jumpv = PS(b);
      if (a == H_MAXUP) maxup = PS(b);
      if (a == H_MAXDOWN) maxdown = PS(b);
      if (a == H_BACK) back = b;
      if (a == H_DOWNJUMP) downjump = b;
      if (a == H_DIR) dir = b;
      if (a == H_SLIDE) slide = b;
      if (a == H_PUSHX) pushx = PS(b), run = PS(300);
      break;
    case O_GRAV: gx = PS2(a), gy = PS2(b); break;
    case O_HGROW: tsx = a, tsy = b, hgl = c ? c : 1; break;
    case O_SHAKE: shake = a, shake_n = b; break;
    case O_FIRE: fire(a); break;
    case O_REARM: o->f &= ~F_USED; break;
    case O_TOWARD: o->vx = (int16_t)PS(hx < o->x ? -a : a); break;
    case O_RULE:
      if (a == U_NONE) {
        for (int i = 0; i < nr; i++)
          if (rule[i].o == p->o && (!b || rule[i].type == b)) rule[i] = rule[--nr], i--;
      } else if (nr < 12) {
        rule[nr].o = p->o, rule[nr].type = (uint8_t)a, rule[nr].p = (int16_t)b, rule[nr++].q = (int16_t)c;
      }
      break;
    case O_ACCEL: o->ax = (int16_t)PS2(a), o->ay = (int16_t)PS2(b); break;
    case O_HOP:
      if (!(o->f & F_HOP)) o->f |= F_HOP, o->rest = (int16_t)(o->y >> 8), o->vy = (int16_t)-PS(a);
      break;
    default: keyev = a; /* O_KEY */
  }
}

static void script(void) {
  for (int i = 0; i < ns; i++) {
    step_t *p = &st[i];
    int c = p->cond;
    if (p->done || !cnt[p->ev] || (uint16_t)(frame - at_[p->ev]) < p->t) continue;
    p->done = 1;
    if (!c || (c < 16 ? cnt[p->ev] == c : c < 32 ? cnt[c & 15] : !cnt[c & 15])) exec(p);
  }
}

/* the runner pushed out of the way of a block that moved by (dx, dy) pixels into it */
static void shove(obj_t *o, int dx, int dy) {
  int b[4];
  box(o, b);
  if (!hit(b, hx >> 8, hy >> 8, (hx >> 8) + hw, (hy >> 8) + hh)) return;
  int32_t px = hx, py = hy;
  o->f |= F_GHOST; /* (the block left out while the runner moves out of it) */
  if (dy) step_axis(&hy, (dy < 0 ? b[1] - hh - (hy >> 8) : b[3] - (hy >> 8)) * 256, 1);
  else if (dx) step_axis(&hx, (dx < 0 ? b[0] - hw - (hx >> 8) : b[2] - (hx >> 8)) * 256, 0);
  o->f &= ~F_GHOST;
  if (hit(b, hx >> 8, hy >> 8, (hx >> 8) + hw, (hy >> 8) + hh) || (px == hx && py == hy && !unstick())) die();
}

static void objects(void) {
  int b[4];
  for (int k = 0; k < no; k++) {
    obj_t *o = &ob[k];
    int kind = o->kind & 15;
    if (!there(o)) continue;
    int32_t ox = o->x, oy = o->y;
    if (o->gl) o->w += (o->gw - o->w) / o->gl, o->h += (o->gh - o->h) / o->gl, o->gl--;
    if (o->ml) o->x += (o->tx - o->x) / o->ml, o->y += (o->ty - o->y) / o->ml, o->ml--;
    if (o->f & F_HOP || kind == K_GSPIKE) o->vy = (int16_t)clampi(o->vy + gy, -PS(300), PS(300));
    o->vx += o->ax, o->vy += o->ay;
    o->x += o->vx;
    box(o, b);
    if ((o->ax || kind == K_GSPIKE) && ground_in(b[0], b[1], b[2], b[3])) o->x -= o->vx, o->vx = 0;
    o->y += o->vy;
    box(o, b);
    if (kind == K_GSPIKE) { /* falling with gravity, onto the ground and the blocks */
      int c[4], stop = ground_in(b[0], b[1], b[2], b[3]);
      for (int j = 0; j < no; j++)
        if (solid(&ob[j])) box(&ob[j], c), stop |= hit(c, b[0], b[1], b[2], b[3]);
      if (stop) o->y -= o->vy, o->vy = 0;
    }
    if (o->f & F_HOP && o->y >> 8 >= o->rest) o->y = o->rest * 256, o->vy = 0, o->f &= ~F_HOP;
    if (solid(o) && !dead) shove(o, (o->x >> 8) - (ox >> 8), (o->y >> 8) - (oy >> 8));
    if (kind == K_SPRING && o->t) o->t--;
  }
  for (int i = 0; i < nr; i++) {
    obj_t *o = &ob[rule[i].o];
    int x = o->x >> 8, p = rule[i].p, q = rule[i].q;
    switch (rule[i].type) {
      case U_PATROL:
        if (x <= p) o->vx = (int16_t)PS(70);
        if (x >= q) o->vx = (int16_t)-PS(70);
        break;
      case U_FOLLOW: o->x = ob[p].x + q * 256; break;
      case U_STOPX:
        if ((o->vx > 0 && x >= p) || (o->vx < 0 && x <= p)) o->vx = 0, o->x = q * 256;
        break;
      case U_WRAPX:
        if (o->vx < 0 && x < p) o->x = q * 256;
        if (o->vx > 0 && x > q) o->x = p * 256;
    }
  }
}

/* what the runner touches */
static void contacts(void) {
  int x = hx >> 8, y = hy >> 8, b[4];
  for (int k = 0; k < no && !dead && !won; k++) {
    obj_t *o = &ob[k];
    int kind = o->kind & 15, t = o->kind >> 4 & 3, w = o->w >> 4, h = o->h >> 4;
    if (!there(o)) continue;
    box(o, b);
    if (kind == K_SPIKE) { /* the teeth: the 5 pixels of 16 they stand in, turned with it */
      if (t == 0) b[1] = b[3] - h * 5 / 8;
      if (t == 2) b[3] = b[1] + h * 5 / 8;
      if (t == 1) b[2] = b[0] + w * 5 / 8;
      if (t == 3) b[0] = b[2] - w * 5 / 8;
    }
    if (kind == K_COIN || kind == K_BAD || kind == K_DEATH) b[0] += w / 3, b[1] += h / 3, b[2] -= w / 3, b[3] -= h / 3;
    if (kind == K_SPRING) b[1] = b[3] - h * 3 / 8;
    if (kind == K_DOOR) b[0] += w / 4, b[2] -= w / 4, b[1] += h / 4;
    if (!hit(b, x, y, x + hw, y + hh)) continue;
    if (kind == K_SAW) { /* round: the nearest point of the box to its middle */
      int cx = o->x >> 8, cy = o->y >> 8, dx = cx - clampi(cx, x, x + hw - 1), dy = cy - clampi(cy, y, y + hh - 1);
      if (dx * dx + dy * dy < (w - 1) * (w - 1)) die();
    } else if (kind == K_SPIKE || kind == K_BAD || kind == K_GSPIKE || kind == K_DEATH) {
      die();
    } else if (kind == K_SPRING && !o->t) {
      vy = -maxup, jumping = 1, o->t = 18;
    } else if (kind == K_TRIG && !(o->f & F_USED)) {
      o->f |= F_USED, fire(o->ev);
    } else if (kind == K_COIN) { /* (three sparks fly up from it, at 100 pixels a second, for 300 ms) */
      o->f |= F_OFF, spark_t = 18;
      for (int i = 0; i < 3; i++) {
        int d = 34 + (int)(rnd() % 30); /* (190 to 355 degrees) */
        spark[i].x = o->x, spark[i].y = o->y;
        spark[i].vx = (int16_t)(PS(100) * sine_of(d + 16) / 256), spark[i].vy = (int16_t)(PS(100) * sine_of(d) / 256);
      }
    } else if (kind == K_BUTTON) {
      o->f |= F_USED;
    } else if (kind == K_DOOR) { /* (and the screen shakes a little, a quarter of a second) */
      won = 1, shake = 0, shake_n = 15;
    }
  }
}

static void hero(int in) {
  int down = gy < 0 ? -1 : 1, b[4];
  if (hgl) { /* growing or shrinking, out of what it grows into */
    hsx += (tsx - hsx) / hgl, hsy += (tsy - hsy) / hgl, hgl--;
    size_hero();
    if (blocked(hx >> 8, hy >> 8, -1) && !unstick()) die();
  }
  int m = !!(in & I_RIGHT) - !!(in & I_LEFT), jump = (downjump ? I_DOWN : I_UP) & in & ~held_in;
  if (back) m = -m;
  if (m) face = m;
  if (modes & R_FLAPPY) {
    if (!started && in & ~held_in && frame > 30) started = 1;
    if (started) vx = clampi(vx + dir * PS(52), -run, run);
    if (started && in & I_UP & ~held_in) vy = -jumpv, flapped = 8;
    if (vy > maxdown) vy = maxdown;
  } else {
    int under = blocked(hx >> 8, (hy >> 8) + down, down > 0 ? (hy >> 8) + hh - 1 : -1);
    if (pushx) vx = pushx;
    else if (m) vx = m * run;
    else if (ground) {
      obj_t *o = under > 1 ? &ob[under - 2] : 0;
      if (o && (o->vx || o->vy)) vx = o->vx, vy = o->vy; /* carried by what it stands on */
      else if (!(o && o->f & F_ICE)) vx = iabs(vx) <= iabs(gy) ? 0 : vx - (vx > 0 ? 1 : -1) * iabs(gy);
    } else if (!slide) {
      vx = iabs(vx) > PS(10) ? vx * 4 / 5 : 0;
    }
    if (modes & R_GRAVITY) {
      if (in & I_UP & ~held_in && started && (ground || blocked(hx >> 8, (hy >> 8) - down, -1))) gy = -gy, ground = 0;
    } else if (jump && (ground || (coyote < 5 && !jumping))) {
      vy = -down * jumpv, jumping = 1;
    }
  }
  vx += gx, vy += gy;
  if (vy >= maxdown) vy = maxdown;
  if (vy <= -maxup) vy = -maxup;
  if (!pushx) vx = clampi(vx, -run, run);
  if (step_axis(&hx, vx, 0)) vx = 0;
  if (step_axis(&hy, vy, 1)) vy = 0;
  /* where its picture is on the screen (the game's camera: x 50 to 477) */
  int ax = (hx >> 8) - 55, ay = (hy >> 8) - 3;
  if (modes & R_WRAP) { /* off one side of the screen, back in at the other */
    if (ax < -6) hx += 424 * 256;
    if (ax > 419) hx -= 424 * 256;
    if (ay > 234) hy -= 242 * 256;
    if (modes & R_WRAPY && ay < -9) hy += 239 * 256;
    ax = (hx >> 8) - 55, ay = (hy >> 8) - 3;
  }
  if (ay < -16 || ay > 240 || ax < -8 || ax > 421) die();
  ground = blocked(hx >> 8, (hy >> 8) + down, down > 0 ? (hy >> 8) + hh - 1 : -1) != 0;
  if (ground) coyote = 0, jumping &= vy * down < 0;
  else coyote++;
  if (frame == 30) started = 1;
  (void)b;
  contacts();
}

static int sdx, sdy; /* the screen shaking (pixels of the screen) */
static void step(int in) {
  frame++, anim++;
  if (shake_n) shake_n--;
  sdx = shake_n ? ((int)(rnd() % 3) - 1) * (1 + shake / 4) : 0, sdy = shake_n ? ((int)(rnd() % 3) - 1) * (1 + shake / 4) : 0;
  if (dead) {
    for (int i = 0; i < 8; i++) bits[i].x += bits[i].vx, bits[i].y += bits[i].vy, bits[i].vy += PS2(800);
    if (++dead == 30) waiting = 1;
    return;
  }
  if (won) {
    won++;
    return;
  }
  script();
  objects();
  if (born) {
    born--;
  } else if (frozen) { /* (warped: its pieces spread 250 ms, then come together in 500) */
    if (--frozen > 30)
      for (int i = 0; i < 8; i++) bits[i].x += bits[i].vx, bits[i].y += bits[i].vy;
    else if (frozen == 30) /* (where about the runner each piece goes) */
      for (int i = 0; i < 8; i++) bits[i].vx = (int16_t)(rnd() % 7) - 3, bits[i].vy = (int16_t)(rnd() % 11) - 5;
  } else if (!dead) {
    hero(in);
  }
  if (spark_t) {
    spark_t--;
    for (int i = 0; i < 3; i++) spark[i].x += spark[i].vx, spark[i].y += spark[i].vy;
    if (spark_t == 9) /* (150 ms on, a quarter of the speed) */
      for (int i = 0; i < 3; i++) spark[i].vx /= 4, spark[i].vy /= 4;
  }
  if ((in & (I_LEFT | I_RIGHT)) && !cnt[EV_MOVE]) fire(EV_MOVE);
  if (keyev >= 0 && in & I_UP & ~held_in) fire(keyev);
  held_in = in;
}

/* ------------------------------------------------------------ drawing */
/* The game's own pictures and fonts (tools/pictures.py): the rooms are drawn at 3/4, as the scene is,
   the rest (the jaws, the title, the map, what is written) as the game has them, a pixel a pixel. */
#include "pictures.h"
#define DARK HEX(0x6D1102) /* the game's dark red: the jaws, its buttons, its writing */
#define YELLOW HEX(0xFCF965)
#define ORANGE HEX(0xFFAB33) /* the title's */
#define BTN HEX(0xC57D17)    /* its buttons, and their lower edge */
#define BTN_EDGE HEX(0xA2650F)

static int sxp(int32_t x) { return (int)((x * 3 >> 10) - 38) + sdx; } /* 256ths of a scene pixel: the screen's */
static int syp(int32_t y) { return (int)((y * 3 >> 10) + 30) + sdy; }
static void put(int x, int y, color_t c) {
  if (x >= 0 && x < SCREEN_W && y >= gfx_y0 && y < gfx_y1) gfx_buf[(y - gfx_y0) * SCREEN_W + x] = c;
}

/* a picture into the box (x0, y0)-(x1, y1) of the screen, each pixel the nearest of the picture's,
   turned t quarters (clockwise), mirrored if fx, upside down if fy; its grey in tint (the room's colour) */
static void spr(int id, int x0, int y0, int x1, int y1, int t, int fx, int fy, color_t tint) {
  const pic_t *p = &pics[id];
  int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0 || y1 <= gfx_y0 || y0 >= gfx_y1 || x1 <= 0 || x0 >= SCREEN_W) return;
  const uint8_t *d = pic_data + p->at;
  int stride = (p->w + 1) >> 1, tw = t & 1 ? p->h : p->w, th = t & 1 ? p->w : p->h;
  for (int y = imax(y0, gfx_y0); y < imin(y1, gfx_y1); y++) {
    int v = ((y - y0) * 2 + 1) * th / (2 * h);
    color_t *row = gfx_buf + (y - gfx_y0) * SCREEN_W;
    if (fy) v = th - 1 - v;
    for (int x = imax(x0, 0); x < imin(x1, SCREEN_W); x++) {
      int u = ((x - x0) * 2 + 1) * tw / (2 * w), sx, sy;
      if (fx) u = tw - 1 - u;
      if (t == 0) sx = u, sy = v;
      else if (t == 1) sx = v, sy = p->h - 1 - u;
      else if (t == 2) sx = p->w - 1 - u, sy = p->h - 1 - v;
      else sx = p->w - 1 - v, sy = u;
      int n = d[sy * stride + (sx >> 1)] >> (sx & 1) * 4 & 15;
      if (n) row[x] = n == p->tint ? tint : pic_pal[p->pal + n - 1];
    }
  }
}

/* a picture as it is, its rows r0 to r1 (its row r at y + r), its corner at x */
static void blit_rows(int id, int x, int y, int r0, int r1, color_t tint) {
  const pic_t *p = &pics[id];
  const uint8_t *d = pic_data + p->at;
  r0 = imax(r0, gfx_y0 - y), r1 = imin(imin(r1, p->h), gfx_y1 - y);
  for (int r = r0; r < r1; r++) {
    color_t *row = gfx_buf + (y + r - gfx_y0) * SCREEN_W;
    if (p->runs) { /* a byte a run (its colour, its length less one; 15: 16 and the next byte) */
      const uint8_t *q = d + (d[2 * r] | d[2 * r + 1] << 8);
      for (int u = 0; u < p->w;) {
        int b = *q++, n = (b & 15) + 1, c = b >> 4;
        if (n == 16) n += *q++;
        if (c) {
          color_t k = c == p->tint ? tint : pic_pal[p->pal + c - 1];
          for (int i = imax(0, x + u); i < imin(SCREEN_W, x + u + n); i++) row[i] = k;
        }
        u += n;
      }
    } else {
      const uint8_t *q = d + r * ((p->w + 1) >> 1);
      for (int u = imax(0, -x); u < p->w && x + u < SCREEN_W; u++) {
        int n = q[u >> 1] >> (u & 1) * 4 & 15;
        if (n) row[x + u] = n == p->tint ? tint : pic_pal[p->pal + n - 1];
      }
    }
  }
}
static void blit(int id, int x, int y) { blit_rows(id, x, y, 0, SCREEN_H, 0); }

/* the game's writing: y is the top of the line */
static const glyph_t *glyph(const pfont_t *f, char c) { return &f->g[(c < 32 || c > 126 ? '?' : c) - 32]; }
static int text_w(const pfont_t *f, const char *s) {
  int w = 0;
  for (; *s; s++) w += glyph(f, *s)->adv;
  return w;
}
static int text(const pfont_t *f, int x, int y, const char *s, color_t c) {
  for (; *s; s++) {
    const glyph_t *g = glyph(f, *s);
    const uint8_t *b = f->bits + g->at;
    int bw = (g->w + 7) >> 3;
    if (y + g->y + g->h > gfx_y0 && y + g->y < gfx_y1)
      for (int j = 0; j < g->h; j++)
        for (int i = 0; i < g->w; i++)
          if (b[j * bw + (i >> 3)] >> (i & 7) & 1) put(x + g->x + i, y + g->y + j, c);
    x += g->adv;
  }
  return x;
}
static void text_c(const pfont_t *f, int cx, int y, const char *s, color_t c) { text(f, cx - text_w(f, s) / 2, y, s, c); }

/* ---- the jaws: the game's scene change, the Devil's eyes on its upper jaw. They shut (500 ms, slow
   then fast), stay shut a while, then something changes behind them (then) and they open again. */
static const uint16_t expo[31] = {0,   1,   2,   2,   3,   3,   4,   5,   6,   8,   10,  13,  16,  20,  25, 32,
                                  40,  51,  64,  81,  102, 128, 161, 203, 256, 323, 406, 512, 645, 813, 1024};
enum { J_OPEN, J_CLOSING, J_SHUT, J_OPENING };
enum { THEN_NONE, THEN_MAP, THEN_ROOM, THEN_NEXT };
static int jaw = J_SHUT, jaw_t, jaw_hold = 40, jaw_then, jaw_still; /* still: the room waits until they open */
static void jaws_close(int hold, int then) { jaw = J_CLOSING, jaw_t = 0, jaw_hold = hold, jaw_then = then; }
static void jaws_shut(void) { jaw = J_SHUT, jaw_t = 0, jaw_hold = -1; } /* at once, until told */
static void jaws_open(void) { jaw = J_OPENING, jaw_t = 0; }
static void draw_jaws(void) {
  int e = expo[imin(jaw_t, 30)], top, bot;
  if (jaw == J_OPEN) return;
  if (jaw == J_CLOSING) top = -136 + 136 * e / 1024, bot = 336 - 233 * e / 1024;
  else if (jaw == J_SHUT) top = 0, bot = 103;
  else top = -136 * e / 1024, bot = 103 + 466 * e / 1024;
  if (jaw == J_SHUT) gfx_fill(0, 0, SCREEN_W, SCREEN_H, DARK); /* (a pixel shows between the teeth) */
  if (bot < SCREEN_H) blit(P_JAW_BOTTOM, -56, bot), gfx_fill(0, bot + 135, SCREEN_W, SCREEN_H, DARK);
  blit(P_JAW_TOP, -56, top);
}

/* ---- the room */
static void draw_object(obj_t *o) {
  int kind = o->kind & 15, t = o->kind >> 4 & 3, b[4];
  if (o->f & (F_HIDE | F_OFF) || kind == K_TRIG || kind == K_SIGN || o->w <= 0 || o->h <= 0) return;
  box(o, b);
  int x0 = sxp(b[0] * 256), y0 = syp(b[1] * 256), x1 = sxp(b[2] * 256), y1 = syp(b[3] * 256);
  if (kind == K_BLOCK) { /* (on through the margins, as the ground) */
    if (b[1] <= 0) y0 = 0;
    if (b[3] >= 240) y1 = SCREEN_H;
  }
  if (y1 < gfx_y0 - 4 || y0 > gfx_y1 + 4) return;
  if (kind == K_BLOCK) {
    gfx_fill(x0, y0, x1 - x0, y1 - y0, fg);
    return;
  }
  int id = kind == K_SPIKE    ? P_SPIKE
           : kind == K_GSPIKE ? P_GSPIKE
           : kind == K_SAW    ? (o->f & F_BIG ? P_SAW_BIG : P_SAW)
           : kind == K_SPRING ? (o->t ? P_SPRING_UP : P_SPRING)
           : kind == K_COIN   ? P_COIN
           : kind == K_BAD    ? P_BAD_COIN
           : kind == K_WARP   ? P_WARP0 + (anim % 33 >= 24) /* (400 ms, then 150) */
           : kind == K_BUTTON ? (o->f & F_USED ? P_BUTTON_DOWN : P_BUTTON)
           : kind == K_THRU   ? P_THRU
                              : -1;
  if (kind == K_DOOR) { /* the door; once reached, it shuts (300, 100, 75, 50, 25 ms) and is gone at 600 */
    if (modes & R_SWAP) id = P_DOOR_HERO; /* (dressed as the runner) */
    else if (!won) id = P_DOOR;
    else if (won < 36) id = P_DOOR_SHUT0 + (won >= 18) + (won >= 24) + (won >= 29) + (won >= 32);
  }
  if (id >= 0) spr(id, x0, y0, x1, y1, t, 0, 0, fg);
}

static void ground_draw(void) {
  for (int ty = 0; ty < 15; ty++) {
    int y0 = syp(ty * 4096), y1 = syp(ty * 4096 + 4096);
    if (ty == 0) y0 = 0;
    if (ty == 14) y1 = SCREEN_H;
    if (y1 <= gfx_y0 || y0 >= gfx_y1) continue;
    for (int tx = 3; tx < 30; tx++) {
      if (!cell(tx, ty)) continue;
      int e = tx;
      while (e < 29 && cell(e + 1, ty)) e++;
      gfx_fill(imax(0, sxp(tx * 4096)), y0, sxp(e * 4096 + 4096) - imax(0, sxp(tx * 4096)), y1 - y0, fg);
      tx = e;
    }
  }
}

/* the runner: its 16 x 16 picture about its 5 x 13 box (the box at 5, 3), standing, running (60 ms a
   picture), jumping, or flapping; in the swap room it looks like the door */
static void draw_hero(void) {
  int flip = gy < 0, id;
  int32_t px = hx - hw * 256, py = hy - hh * 3 * 256 / 13, pw = hw * 16 * 256 / 5, ph = hh * 16 * 256 / 13;
  if (flip) py = hy + hh * 256 - ph + hh * 3 * 256 / 13; /* (upside down: the box at the picture's top) */
  if (modes & R_SWAP) id = P_DOOR_DUMMY;
  else if (modes & R_FLAPPY) id = vx > 0 ? P_FLAP_R : vx < 0 ? P_FLAP_L : P_FLAP_STAND;
  else if (!ground) id = face < 0 ? P_HERO_JUMP_L : P_HERO_JUMP_R;
  else if (vx) id = (face < 0 ? P_HERO_RUN_L0 : P_HERO_RUN_R0) + anim * 5 / 18 % 4;
  else id = P_HERO_IDLE;
  spr(id, sxp(px), syp(py), sxp(px + pw), syp(py + ph), 0, 0, flip, fg);
}
static void piece(int id, int32_t x, int32_t y) { /* 4 x 3, about (2, 1) */
  spr(id, sxp(x - 512), syp(y - 256), sxp(x + 512), syp(y + 512), 0, 0, 0, BLACK);
}

static void play_hud(void) {
  for (int i = 0; i < ROOMS; i++) { /* the five rooms: those reached fill in, growing and back, at 750 ms */
    int t = frame - 45, d = 6 * (i + 1), s = 0, x = 136 + 10 * i;
    if (i <= rnum && t >= 0 && t < 2 * d) s = t < d ? t * 4 / d : (2 * d - t) * 4 / d; /* (9 to 13 pixels) */
    spr(i <= rnum && t >= 0 ? P_ROOM_DONE : P_ROOM_SQUARE, x - s / 2, 4 - s / 2, x + 9 + s / 2 + s % 2, 12 + s / 2 + s % 2, 0, 0,
        0, 0);
  }
  if (frame >= 30 && !ov && (jaw == J_OPEN || jaw == J_OPENING)) { /* the game's back button, the calculator's key */
    blit(P_BACK, 8, 6);
    text(&font, 4, 23, T("back"), BTN_EDGE), text(&font, 4, 22, T("back"), YELLOW); /* (shadowed, as its buttons) */
  }
  if (waiting && sv.open <= 3) text_c(&font, 160, 40, T("PRESS ANY KEY"), YELLOW); /* (the first worlds) */
}

/* ---- the menus: the game's buttons */
static void button(int x, int y, int w, const char *s, int on) {
  int tx = x + (w - text_w(&font, s)) / 2;
  gfx_fill(x, y, w, 14, on ? YELLOW : BTN);
  gfx_fill(x, y + 14, w, 2, on ? BTN : BTN_EDGE);
  if (!on) text(&font, tx, y + 3, s, BTN_EDGE);
  text(&font, tx, y + 2, s, on ? DARK : YELLOW);
}
static const char *const pause_items[] = {T("Resume"), T("Restart"), T("Levels"), T("Quit game")};
static void overlay(void) { /* over the shut jaws */
  if (ov == OV_PAUSE)
    for (int i = 0; i < 4; i++) button(112, 134 + 22 * i, 96, pause_items[i], sel == i);
  if (ov == OV_QUIT) {
    text_c(&font, 160, 142, T("Quit game?"), YELLOW);
    button(100, 168, 52, T("No"), sel == 0), button(168, 168, 52, T("Yes"), sel == 1);
  }
}

static void play_scene(void *ctx) {
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, bg);
  for (int k = 0; k < no; k++) draw_object(&ob[k]);
  ground_draw();
  for (int k = 0; k < no; k++) /* (the sign with the keys, over the ground, as it is: small, its arrows would go) */
    if ((ob[k].kind & 15) == K_SIGN) blit(P_KEYS, sxp(ob[k].x) - pics[P_KEYS].w / 2, syp(ob[k].y) - pics[P_KEYS].h / 2);
  if (dead) {
    for (int i = 0; i < 8; i++)
      if (dead < 42 + i) piece(P_BODY0 + (i & 3), bits[i].x, bits[i].y);
  } else if (!won && !born && !(frozen && frozen < 45)) {
    draw_hero();
  }
  if (frozen && frozen < 45 && !dead) /* warped: the pieces spreading, then coming together where it went */
    for (int i = 0; i < 8; i++) {
      int e = frozen > 30 ? 0 : expo[30 - frozen];
      int32_t tx = hx + hw * 128 + bits[i].vx * 256, ty = hy + hh * 128 + bits[i].vy * 256;
      piece(P_PRE0 + (i & 3), bits[i].x + (tx - bits[i].x) / 1024 * e, bits[i].y + (ty - bits[i].y) / 1024 * e);
    }
  for (int i = 0; i < 3 && spark_t; i++) /* (the game's 2 x 2 sparks) */
    gfx_fill(sxp(spark[i].x) - 1, syp(spark[i].y) - 1, 2, 2, HEX(0xFFE752));
  if (born) /* the pieces coming together, faster and faster (300 ms) */
    for (int i = 0; i < 8; i++) {
      int e = expo[(18 - born) * 30 / 18];
      int32_t tx = hx + hw * 128 + bits[i].vx * 256, ty = hy + hh * 128 + bits[i].vy * 256;
      piece(P_PRE0 + (i & 3), bits[i].x + (tx - bits[i].x) / 1024 * e, bits[i].y + (ty - bits[i].y) / 1024 * e);
    }
  draw_jaws();
  play_hud();
  if (ov) overlay();
}

/* ---- the title (the game's 1 Player card) and the map of the worlds (its title, its portals on their paths) */
static const char *const world_names[LEVELS] = {"Pits",     "Spikes", "Push",  "Coins", "Controls", "Platforms",
                                                 "Springs",  "Warps",  "Scale", "Doors", "Saws",     "Flappy",
                                                 "Gravity",  "Movement", "Wraparound", "Boss"};
static const int16_t portal_at[LEVELS][2] = { /* the game's (its screen's pixels: the scene's less 48) */
  {42, 109}, {85, 97},   {135, 100}, {171, 121}, {147, 158}, {160, 198}, {207, 193}, {235, 160},
  {227, 118}, {262, 100}, {295, 129}, {307, 172}, {331, 203}, {373, 207}, {390, 169}, {365, 113}};
static int pan, menu_anim, credits_y; /* the map scrolled (its 432 pixels on the screen's 320) */
static int pan_for(int i) { return clampi(portal_at[i][0] + (i == 15 ? 16 : 8) - 160, 0, 112); }

static void title_scene(void *ctx) {
  blit_rows(P_TITLE, -56, 0, 0, 90, 0); /* (its top: LEVEL DEVIL, and the flames) */
  gfx_fill(0, 90, SCREEN_W, SCREEN_H - 90, ORANGE);
  if (!ov) blit(P_CARD_ON, 122, 120), text_c(&font, 160, 204, T("press OK"), DARK);
  draw_jaws();
  if (ov) overlay();
}

static void map_scene(void *ctx) {
  char s[24];
  int i = sv.sel, f = menu_anim / 9 & 3, boss = i == LEVELS - 1;
  blit_rows(P_TITLE, -56, 0, 0, 90, 0); /* its top stays */
  blit_rows(P_TITLE, -pan, 0, 90, SCREEN_H, 0);
  for (int k = 0; k < LEVELS; k++) {
    int x = portal_at[k][0] - pan, y = portal_at[k][1];
    if (k == LEVELS - 1) blit(k < sv.open ? P_BOSS_OPEN : P_BOSS_LOCKED, x, y);
    else blit(P_DIRT, x - 2, y + 15), blit(k < sv.open ? P_PORTAL0 + k : P_PORTAL_LOCKED, x, y);
  }
  if (!ov) { /* the one picked: the game's highlight, its name, and the key */
    int x = portal_at[i][0] - pan, y = portal_at[i][1], cx = x + (boss ? 16 : 8), ty = y + (boss ? 33 : 18);
    if (boss) blit(P_HILITE_BOSS0 + f, x + 9, y - 6);
    else blit(P_HILITE0 + f, x + 2, y - 15);
    text_c(&font_small, cx, ty, world_names[i], DARK);
    text_c(&font_small, cx, ty + 7, T("press"), YELLOW);
    text_c(&font_small, cx, ty + 13, T("OK"), YELLOW);
    text(&font_small, 4, 231, label(s, T("deaths "), sv.deaths), DARK);
    if (sv.done >> i & 1) label(s, T("best "), sv.best[i]), text(&font_small, 316 - text_w(&font_small, s), 231, s, DARK);
  }
  draw_jaws();
  if (ov) overlay();
}

/* the end: the Devil laughs, and the game's credits come up */
static void end_scene(void *ctx) {
  char s[24];
  gfx_fill(0, 0, SCREEN_W, SCREEN_H, ORANGE);
  blit(menu_anim / 12 & 1 ? P_EYES_LAUGH : P_EYES, 40, 20);
  blit(P_CREDITS, 20, credits_y);
  if (credits_y <= 100) {
    text_c(&font, 160, 184, label(s, T("Deaths: "), sv.deaths), DARK);
    text_c(&font_small, 160, 206, T("press OK"), DARK);
  }
  draw_jaws();
}

/* ------------------------------------------------------------ the levels */
static void start_level(int l) {
  if (l != sv.level || sv.room >= ROOMS) sv.room = 0, sv.run = 0;
  level = sv.level = (uint8_t)l, rnum = sv.room;
  scr = SC_PLAY, ov = OV_NONE;
  enter();
  save();
}
static void level_done(void) {
  int first = !(sv.done >> level & 1);
  if (first || sv.run < sv.best[level]) sv.best[level] = sv.run;
  sv.done |= (uint16_t)(1u << level);
  sv.open = (uint8_t)imax(sv.open, imin(level + 2, LEVELS));
  sv.room = 0, sv.run = 0, sv.sel = (uint16_t)imin(level + 1, LEVELS - 1);
  scr = level == LEVELS - 1 && first ? SC_END : SC_MAP, pan = pan_for(sv.sel), credits_y = SCREEN_H;
  save();
}
/* what changes behind the shut jaws */
static void then_do(int what) {
  if (what == THEN_MAP) scr = SC_MAP, ov = OV_NONE, pan = pan_for(sv.sel);
  if (what == THEN_ROOM) start_level(sv.sel);
  if (what == THEN_NEXT) { /* the next room, or the map */
    if (++rnum == ROOMS) level_done();
    else sv.room = (uint8_t)rnum, save(), enter();
  }
}

/* a 60th of a second: the jaws, the room (unless paused, or waiting for the jaws), the map scrolling */
static void frame_step(int in) {
  menu_anim++;
  if (jaw == J_CLOSING && ++jaw_t >= 30) jaw = J_SHUT, jaw_t = 0;
  else if (jaw == J_SHUT && jaw_hold >= 0 && ++jaw_t >= jaw_hold) {
    int what = jaw_then;
    jaw_then = THEN_NONE;
    jaws_open();
    then_do(what);
  } else if (jaw == J_OPENING && ++jaw_t >= 30) jaw = J_OPEN, jaw_still = 0;
  if (scr == SC_PLAY && !ov && !jaw_still) {
    if (won == 45) jaws_close(45, THEN_NEXT); /* (the door shut: the jaws at 750 ms, the next room at 2 s) */
    step(in);
  }
  if (scr == SC_MAP) {
    int to = pan_for(sv.sel);
    pan += (to - pan) / 4 + (to > pan) - (to < pan);
  }
  if (scr == SC_END && credits_y > 100 && !(menu_anim % 3)) credits_y--;
}

static void load(void) {
  uint32_t len = 0;
  restore();
  const uint8_t *d = ef_read(SAVE_NAME, &len);
  uint8_t *v = (uint8_t *)&sv;
  bool ok = d && len == sizeof sv && d[0] == 'L' && d[1] == 1;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = ok ? d[i] : 0;
  sv.open = (uint8_t)clampi(sv.open, 1, LEVELS), sv.sel = (uint16_t)clampi(sv.sel, 0, sv.open - 1);
  if (sv.level >= LEVELS) sv.level = 0;
}

static void ask_quit(void) { ov = OV_QUIT, sel = 0, jaws_shut(); }
/* the map: Left and Right the world before and after, Up and Down the nearest one up or down */
static void map_move(int dx, int dy) {
  int best = -1, bd = 1 << 30, x = portal_at[sv.sel][0], y = portal_at[sv.sel][1];
  if (dx) best = clampi(sv.sel + dx, 0, sv.open - 1);
  else
    for (int k = 0; k < sv.open; k++) {
      int ex = portal_at[k][0] - x, ey = (portal_at[k][1] - y) * dy, d = ex * ex * 2 + ey * ey;
      if (ey > 8 && iabs(ex) <= 3 * ey && d < bd) bd = d, best = k; /* (up or down, not far to the side) */
    }
  if (best >= 0) sv.sel = (uint16_t)best;
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  now = (uint32_t)eadk_timing_millis();
  seed = eadk_random() ^ now;
  if (!seed) seed = 1;
  load();
  scr = SC_TITLE; /* (the jaws shut, then open on the title: the game's start) */
  const uint64_t left = KEY(eadk_key_left) | KEY(eadk_key_four), right = KEY(eadk_key_right) | KEY(eadk_key_six);
  const uint64_t up = KEY(eadk_key_up) | KEY(eadk_key_eight), down = KEY(eadk_key_down) | KEY(eadk_key_two);
  const uint64_t ok = KEY(eadk_key_ok) | KEY(eadk_key_exe) | KEY(eadk_key_five), back_k = KEY(eadk_key_back);
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the launcher wait to be let go */
  uint32_t last = now, acc = 0;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint64_t k = eadk_keyboard_scan(), hitk = k & ~held;
    stale &= k, held = k, k &= ~stale, hitk &= ~stale;
    if (k & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    int dx = !!(hitk & right) - !!(hitk & left), dy = !!(hitk & down) - !!(hitk & up), quit = 0;
    int moving = jaw == J_CLOSING || (jaw == J_SHUT && !ov); /* (between two screens: keys wait) */
    if (ov == OV_PAUSE) {
      sel = (sel + dy + 4) % 4;
      if (hitk & back_k || (hitk & ok && sel == 0)) ov = OV_NONE, jaws_open(), jaw_still = 1; /* Resume */
      else if (hitk & ok && sel == 1) ov = OV_NONE, reset(), jaws_open();                     /* Restart */
      else if (hitk & ok && sel == 2) /* Levels: the map, the room left as it was */
        ov = OV_NONE, sv.room = (uint8_t)rnum, save(), jaw_hold = 20, jaw_then = THEN_MAP, jaw_still = 1;
      else if (hitk & ok) ov = OV_QUIT, sel = 0;
    } else if (ov == OV_QUIT) {
      if (dx || dy) sel ^= 1;
      if (hitk & ok && sel) quit = 1;
      else if (hitk & (ok | back_k)) {
        if (scr == SC_PLAY) ov = OV_PAUSE, sel = 3;
        else ov = OV_NONE, jaws_open();
      }
    } else if (moving) {
    } else if (scr == SC_TITLE) {
      if (hitk & ok) jaws_close(10, THEN_MAP);
      else if (hitk & back_k) ask_quit();
    } else if (scr == SC_MAP) {
      if (dx || dy) map_move(dx, dy);
      if (hitk & ok) jaws_close(30, THEN_ROOM);
      else if (hitk & back_k) ask_quit();
    } else if (scr == SC_END) {
      if (hitk & (ok | back_k) && credits_y <= 100) jaws_close(10, THEN_MAP);
    } else if (hitk & back_k) { /* in a room: the jaws shut, and the game's buttons on them */
      ov = OV_PAUSE, sel = 0, jaws_shut();
      save();
    } else if (waiting && hitk) { /* the game's retry: any key */
      reset();
      hitk = 0;
    }
    if (quit) break;
    acc += (now - last) * 3, last = now;
    int in = (k & left ? I_LEFT : 0) | (k & right ? I_RIGHT : 0) | (k & (up | ok) ? I_UP : 0) | (k & down ? I_DOWN : 0);
    int n = 0;
    for (; acc >= 50 && n < 4; acc -= 50) frame_step(in), n++;
    if (acc >= 50) acc = 0; /* (too far behind: let it go) */
    if (n || hitk) {
      eadk_display_wait_for_vblank();
      gfx_render(scr == SC_PLAY ? play_scene : scr == SC_MAP ? map_scene : scr == SC_END ? end_scene : title_scene, 0, 0,
                 SCREEN_H);
    } else {
      eadk_timing_msleep(2);
    }
  }
  if (scr == SC_PLAY) sv.room = (uint8_t)rnum;
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
