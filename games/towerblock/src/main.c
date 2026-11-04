/* Tower Blocks: drop each moving block on the tower; what hangs over the edge falls off.
 *
 * Steve Gardner's Tower Blocks (the game Y8 shows as Tower Block), ported from its source: blocks slide in
 * from either side, along one axis then the other, faster and faster; the part that misses the block below
 * is cut off and falls, landing within 0.3 of it keeps the whole block, and missing it completely ends the
 * game. Its sizes, speeds, colours, light and camera are the original's, seen from the same corner, drawn
 * in pixel lines two across for one down.
 *
 * Inside NumPlay it draws with the launcher's strips, shapes and fonts (launcher/src/gfx.c) and saves
 * through its file functions; on its own (TowerBlocks.nwa) it brings them along.
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
const char eadk_app_name[] __attribute__((section(".rodata.eadk_app_name"))) = "Tower Blocks";
const uint32_t eadk_api_level __attribute__((section(".rodata.eadk_api_level"))) = 0;
#endif
#endif
void np_push(int x, int y, int w, int h, const color_t *px) {
  eadk_display_push_rect((eadk_rect_t){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h}, px);
}
#endif

#define KEY(k) (1ull << (k))
#define INK HEX(0x333344)

/* ------------------------------------------------------------ the original's numbers (world units, 60 ticks a second) */
#define SIZE 10.0f   /* the first block, 10 x 2 x 10 */
#define LAYER 2.0f
#define TRAVEL 12.0f /* blocks slide between -12 and 12 */
#define SNAP 0.3f    /* closer than this keeps the whole block */
static float speed_of(uint32_t index) { /* -0.1 - index * 0.005 a tick, at most 4 */
  float s = 0.1f + (float)index * 0.005f;
  return s > 4 ? 4 : s;
}

/* on screen: 3.2 pixels a unit, two across for one down; the camera's point (2, cam, 2) in the middle */
#define K 3.2f
#define KY (K * 1.2247f)

#define KEEP 40 /* blocks kept (all a screen shows, with room to spare) */
typedef struct {
  float x, z, w, d;
} slab_t;

/* towerblocks.sav: the best, and the tower in progress */
static struct __attribute__((aligned(4))) { /* whole words, as the sum reads them (a TI-84 CE packs structs) */
  uint8_t magic, version, flags, pad; /* flags: 1 a tower to go on with, 2 the guide was read */
  uint32_t sum;                       /* of the words after it: a damaged file starts afresh */
  uint32_t best, score, offset;       /* offset: the colours' start, the original's colorOffset */
  float pos, dir;                     /* the moving block, on its axis */
  slab_t slab[KEEP];                  /* block i (the base is 1) at slab[i % KEEP] */
} sv;
#define SAVE_NAME "towerblocks.sav"
#define TOP (sv.score + 1)     /* the top block's index */
#define MOVING (sv.score + 2)  /* the moving one's: on x when odd, on z when even */
#define AT(i) sv.slab[(i) % KEEP]

static color_t strip[SCREEN_W * STRIP_H];
static uint32_t now, seed, over_t, reset_t, reset_n, cam_t, shown;
static float cam, cam_from, cam_to;
static int ov, sel;
enum { S_READY, S_PLAY, S_OVER, S_RESET };
static int state;
static bool frozen; /* a tower back from the menu or a save: the next OK sets it going */
enum { OV_NONE, OV_MENU, OV_HELP };

/* the pieces cut off, falling */
typedef struct {
  float x, y, z, w, d, rx, ry, rz, out;
  uint32_t t, index;
  uint8_t axis; /* 0 x, 1 z */
} chip_t;
static chip_t chips[4];

static uint32_t rnd(void) {
  seed ^= seed << 13, seed ^= seed >> 17, seed ^= seed << 5;
  return seed;
}
static float frand(void) { return (float)(rnd() & 0xFFFF) / 65536.0f; }
static float fabs_(float v) { return v < 0 ? -v : v; }

/* sine and cosine, for the colours and the turning pieces */
static float sin_(float x) {
  const float tp = 6.2831853f;
  x -= tp * (float)(int)(x / tp);
  if (x < 0) x += tp;
  if (x > 3.1415927f) return -sin_(x - 3.1415927f);
  if (x > 1.5707964f) x = 3.1415927f - x;
  float x2 = x * x;
  return x * (1 - x2 / 6 * (1 - x2 / 20 * (1 - x2 / 42 * (1 - x2 / 72))));
}
static float cos_(float x) { return sin_(x + 1.5707964f); }

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
static void save(void) {
  sv.magic = 'T', sv.version = 1, sv.sum = checksum();
  np_file_write(SAVE_NAME, &sv, sizeof sv);
}

static void camera_to(float y) { cam_from = cam, cam_to = y, cam_t = now; }

/* the next block, from the side its axis starts on, at the speed for its height */
static void next_block(void) {
  sv.pos = rnd() & 1 ? -TRAVEL : TRAVEL;
  sv.dir = -speed_of(MOVING);
  camera_to(LAYER * (float)(MOVING) + 2);
}

static void new_game(void) {
  for (int i = 0; i < KEEP; i++) sv.slab[i] = (slab_t){0, 0, SIZE, SIZE};
  sv.score = 0, sv.offset = rnd() % 101, sv.flags |= 1;
  for (int i = 0; i < 4; i++) chips[i].t = 0;
  state = S_PLAY, frozen = false, shown = 0;
  next_block();
}

static void tick(void) { /* the moving block, a sixtieth of a second */
  if (sv.pos > TRAVEL || sv.pos < -TRAVEL) sv.dir = sv.dir > 0 ? -speed_of(MOVING) : speed_of(MOVING);
  sv.pos += sv.dir;
}

/* the block placed: what overlaps the one below stays, the rest falls off */
static void place(void) {
  uint32_t i = MOVING;
  slab_t below = AT(i - 1), b = below;
  int axis = i % 2 ? 0 : 1;
  float *p = axis ? &b.z : &b.x, *dim = axis ? &b.d : &b.w, bp = axis ? below.z : below.x;
  *p = sv.pos;
  float overlap = *dim - fabs_(sv.pos - bp);
  if (*dim - overlap < SNAP) { /* close enough: the whole block, right on the one below */
    AT(i) = below;
  } else if (overlap > 0) {
    chip_t *c = &chips[0];
    for (int k = 1; k < 4; k++)
      if (chips[k].t < c->t) c = &chips[k];
    *c = (chip_t){b.x, LAYER * (float)i, b.z, b.w, b.d, 0, frand() * 0.1f, 0, 40 * fabs_(sv.dir), now, i, (uint8_t)axis};
    float spin = frand() * 10 - 5;
    if (axis) c->rx = spin, c->rz = 0.1f, c->d = *dim - overlap;
    else c->rz = spin, c->rx = 0.1f, c->w = *dim - overlap;
    if (sv.pos < bp) { /* sticking out before it: the kept part starts where the block below does */
      *p = bp;
      if (sv.dir > 0) c->out = -c->out;
    } else {
      (axis ? &c->z : &c->x)[0] += overlap;
    }
    if ((sv.pos < bp) == (sv.dir > 0)) c->out = sv.pos < bp ? -fabs_(c->out) : fabs_(c->out);
    *dim = overlap;
    AT(i) = b;
  } else { /* missed: it falls whole, and the game is over */
    state = S_OVER, over_t = now, sv.flags &= (uint8_t)~1;
    save();
    return;
  }
  sv.score++;
  if (sv.score > sv.best) sv.best = sv.score;
  next_block();
}

/* ------------------------------------------------------------ drawing */
static float ease_in(float t) { return t <= 0 ? 0 : t >= 1 ? 1 : t * t; }
static float ease_out(float t) { return t <= 0 ? 0 : t >= 1 ? 1 : 1 - (1 - t) * (1 - t); }
static float ease_io(float t) { return t <= 0 ? 0 : t >= 1 ? 1 : t < 0.5f ? 2 * t * t : 1 - 2 * (1 - t) * (1 - t); }

/* block i's colour, as the original works it out: three waves a third of a turn apart */
static uint32_t colour(uint32_t i) {
  if (i <= 1) return 0x333344;
  float o = 0.3f * (float)(i + sv.offset);
  uint32_t r = (uint32_t)(sin_(o) * 55 + 200), g = (uint32_t)(sin_(o + 2) * 55 + 200), b = (uint32_t)(sin_(o + 4) * 55 + 200);
  return r << 16 | g << 8 | b;
}
static color_t lit(uint32_t rgb, int k256) { /* the colour under the light: 0.9 on top, 0.75 on the sides */
  return gfx_rgb(gfx_lerp888(rgb, 0, 256 - k256));
}

/* a convex four-sided face, filled row by row within the strip */
static void quad(const float *px, const float *py, color_t c) {
  float y0 = py[0], y1 = py[0];
  for (int k = 1; k < 4; k++) y0 = py[k] < y0 ? py[k] : y0, y1 = py[k] > y1 ? py[k] : y1;
  int a = (int)y0, b = (int)y1 + 1;
  if (a < gfx_y0) a = gfx_y0;
  if (b > gfx_y1) b = gfx_y1;
  for (int y = a; y < b; y++) {
    float yc = (float)y + 0.5f, l = 1e9f, r = -1e9f;
    for (int k = 0; k < 4; k++) {
      int n = (k + 1) & 3;
      float ya = py[k], yb = py[n];
      if ((yc < ya) == (yc < yb)) continue;
      float x = px[k] + (px[n] - px[k]) * (yc - ya) / (yb - ya);
      l = x < l ? x : l, r = x > r ? x : r;
    }
    int xl = (int)(l + 0.5f), xr = (int)(r + 0.5f);
    if (xr > xl) gfx_fill(xl, y, xr - xl, 1, c);
  }
}

/* a block: its corner at (x, y, z), turned (Euler x, y, z about that corner, as three.js does) and scaled;
   the faces turned towards the camera, lit as the original lights them */
static void box(float x, float y, float z, float w, float d, float rx, float ry, float rz, float s, uint32_t rgb) {
  float cx = cos_(rx), sx = sin_(rx), cy = cos_(ry), sy = sin_(ry), cz = cos_(rz), sz = sin_(rz);
  /* R = Rx Ry Rz */
  float m[9] = {cy * cz, -cy * sz, sy, cx * sz + sx * sy * cz, cx * cz - sx * sy * sz, -sx * cy,
                sx * sz - cx * sy * cz, sx * cz + cx * sy * sz, cx * cy};
  float sxp[8], syp[8];
  for (int k = 0; k < 8; k++) {
    float lx = (k & 1 ? w : 0) * s, ly = (k & 2 ? LAYER : 0) * s, lz = (k & 4 ? d : 0) * s;
    float wx = x + m[0] * lx + m[1] * ly + m[2] * lz, wy = y + m[3] * lx + m[4] * ly + m[5] * lz,
          wz = z + m[6] * lx + m[7] * ly + m[8] * lz;
    sxp[k] = 160 + K * (wx - wz), syp[k] = 120 - KY * (wy - cam) + K * 0.5f * (wx + wz - 4);
  }
  float top = syp[0], bot = syp[0];
  for (int k = 1; k < 8; k++) top = syp[k] < top ? syp[k] : top, bot = syp[k] > bot ? syp[k] : bot;
  if (bot < gfx_y0 || top >= gfx_y1) return;
  /* the faces: their corners (bits x, y, z) in order around, then the axis of their normal, + or - */
  static const int8_t face[6][6] = {{2, 3, 7, 6, 1, 1}, {1, 3, 7, 5, 0, 1}, {4, 5, 7, 6, 2, 1},
                                    {0, 1, 5, 4, 1, -1}, {0, 2, 6, 4, 0, -1}, {0, 1, 3, 2, 2, -1}};
  for (int f = 0; f < 6; f++) {
    int ax = face[f][4];
    float nx = m[ax] * face[f][5], ny = m[3 + ax] * face[f][5], nz = m[6 + ax] * face[f][5];
    if (nx + ny + nz <= 0.0001f) continue; /* turned away from the camera, at (1, 1, 1) */
    float qx[4], qy[4];
    for (int k = 0; k < 4; k++) qx[k] = sxp[face[f][k]], qy[k] = syp[face[f][k]];
    quad(qx, qy, lit(rgb, ny * 0.5f + 0.5f < 0.7f ? 192 : 230));
  }
}

static void button(int y, const char *s, bool on) {
  gfx_rrect(100, y, 120, 24, 4, on ? INK : HEX(0xE6E2DF), 256);
  gfx_rrect_outline(100, y, 120, 24, 4, 2, INK, 256);
  gfx_text_center(&np_font_body, 160, y + 17, s, on ? HEX(0xD0CBC7) : INK, 256);
}
static const char *const menu_items[] = {T("Resume"), T("New game"), T("How to play"), T("Quit")};

/* the pieces falling away behind the tower (back) or in front of it */
static void chips_drawn(bool back) {
  for (int k = 0; k < 4; k++) {
    chip_t *c = &chips[k];
    float t = (float)(now - c->t) / 1000.0f;
    if (!c->t || t >= 1 || (c->out < 0) != back) continue;
    float e = ease_in(t), r = ease_out(t - 0.05f);
    box(c->x + (c->axis ? 0 : c->out * e), c->y - 30 * e, c->z + (c->axis ? c->out * e : 0), c->w, c->d, c->rx * r,
        c->ry * r, c->rz * r, 1, colour(c->index));
  }
}

static void scene(void *ctx) {
  char s[16];
  gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, HEX(0xD0CBC7));
  chips_drawn(true);
  /* the tower, from the bottom block on screen up; while it resets, each block shrinks away in turn */
  uint32_t top = state == S_PLAY && !frozen ? MOVING : TOP; /* (a missed block is gone, as in the original) */
  uint32_t lo = top > KEEP - 2 ? top - (KEEP - 2) : 1;
  for (uint32_t i = lo; i <= top; i++) {
    slab_t b = AT(i);
    float sc = 1, ry = 0;
    if (state == S_RESET && i > 1) { /* from the top, 0.02 s apart, 0.2 s each */
      float t = ((float)(now - reset_t) / 1000.0f - (float)(reset_n + 1 - i) * 0.02f) / 0.2f;
      sc = 1 - ease_in(t), ry = 0.5f * ease_in(t);
      if (sc <= 0) continue;
    }
    if (i == MOVING && state != S_RESET) { /* the moving block, at its place on its axis */
      b = AT(i - 1);
      if (i % 2) b.x = sv.pos;
      else b.z = sv.pos;
    }
    box(b.x, LAYER * (float)i, b.z, b.w, b.d, 0, ry, 0, sc, colour(i));
  }
  chips_drawn(false);
  gfx_text_right(&np_font_small, 314, 14, T("Back  menu"), INK, 160);
  if (state == S_READY) {
    gfx_rrect_outline(122, 44, 76, 34, 0, 3, INK, 256);
    gfx_text_center(&np_font_title, 160, 68, T("Start"), INK, 256);
  } else {
    num(s, state == S_RESET ? shown : sv.score);
    gfx_text_center(&np_font_title, 160, state == S_OVER ? 46 : 32, s, INK, 256);
  }
  if (state == S_PLAY && (frozen || sv.score < 3))
    gfx_text_center(&np_font_body, 160, 52, frozen ? T("Press OK to go on") : T("Press OK to place the block"), INK, 256);
  if (state == S_OVER && now - over_t > 300) {
    gfx_text_center(&np_font_title, 160, 72, T("Game Over"), INK, 256);
    gfx_text_center(&np_font_body, 160, 90, T("You did great, you're the best."), INK, 256);
    gfx_text_center(&np_font_body, 160, 106, T("Press OK to start again"), INK, 256);
    char *e = s;
    for (const char *p = T("Best "); *p;) *e++ = *p++;
    num(e, sv.best);
    gfx_text_center(&np_font_small, 160, 119, s, INK, 190);
  }
  if (ov) {
    gfx_fill(0, gfx_y0, SCREEN_W, gfx_y1 - gfx_y0, HEX(0xD0CBC7));
    if (ov == OV_MENU) {
      gfx_text_center(&np_font_title, 160, 60, T("Paused"), INK, 256);
      for (int i = 0; i < 4; i++) button(76 + 32 * i, menu_items[i], sel == i);
    } else {
      gfx_text_center(&np_font_title, 160, 60, T("How to play"), INK, 256);
      gfx_paragraph(&np_font_body, 160, 84, 220, 16,
                    T("Press OK to drop the moving block on the tower. What hangs over the edge falls off, so the blocks get smaller. Land it just right to keep it whole. Miss the tower and it's over."),
                    INK, 256);
      button(176, T("Play"), true);
    }
  }
}

/* ------------------------------------------------------------ the game */
static void load(void) {
  uint32_t len = 0;
  const uint8_t *d = np_file_read(SAVE_NAME, &len);
  uint8_t *v = (uint8_t *)&sv;
  bool ok = d && len == sizeof sv && d[0] == 'T' && d[1] == 1;
  for (uint32_t i = 0; i < sizeof sv; i++) v[i] = ok ? d[i] : 0;
  if (sv.sum != checksum())
    for (uint32_t i = 0; i < sizeof sv; i++) v[i] = 0;
}

int main(void) {
  np_app_begin();
  color_t *their_strip = gfx_buf;
  gfx_buf = strip;
  now = (uint32_t)eadk_timing_millis();
  seed = eadk_random() ^ now;
  if (!seed) seed = 1;
  load();
  if (sv.flags & 1) state = S_PLAY, frozen = true, camera_to(LAYER * (float)MOVING + 2);
  else {
    state = S_READY, sv.score = 0;
    for (int i = 0; i < KEEP; i++) sv.slab[i] = (slab_t){0, 0, SIZE, SIZE};
    camera_to(LAYER * 2 + 2);
  }
  cam = cam_to;
  if (!(sv.flags & 2)) ov = OV_HELP, sv.flags |= 2;
  uint64_t held = eadk_keyboard_scan(), stale = held; /* keys still down from the launcher wait to be let go */
  uint32_t ticks = now;
  for (;;) {
    now = (uint32_t)eadk_timing_millis();
    uint64_t raw = eadk_keyboard_scan(), k = raw & ~(stale &= raw), hit = k & ~held;
    held = k;
    if (raw & (KEY(eadk_key_home) | KEY(eadk_key_on_off))) break;
    bool ok = hit & (KEY(eadk_key_ok) | KEY(eadk_key_exe));
    int dy = !!(hit & KEY(eadk_key_down)) - !!(hit & KEY(eadk_key_up));
    if (ov) {
      int n = ov == OV_MENU ? 4 : 1;
      sel = (sel + dy + n) % n;
      if (hit & KEY(eadk_key_back)) ov = OV_NONE;
      if (ok) {
        int choice = ov == OV_MENU ? sel : 0;
        ov = OV_NONE;
        if (choice == 1) new_game();
        else if (choice == 2) ov = OV_HELP;
        else if (choice == 3) break;
      }
    } else if (hit & KEY(eadk_key_back)) {
      ov = OV_MENU, sel = 0;
      if (state == S_PLAY) frozen = true;
      save();
    } else if (ok) {
      if (state == S_READY) new_game();
      else if (state == S_OVER && now - over_t > 300) { /* the tower goes, the score counts down, a new game */
        state = S_RESET, reset_t = now, reset_n = TOP, shown = sv.score;
        camera_to(LAYER * 2 + 2);
        cam_t = now;
      } else if (state == S_PLAY && frozen) frozen = false;
      else if (state == S_PLAY) place();
    }
    /* sixty ticks a second, as the original's frames, catching up after a slow frame */
    if (state != S_PLAY || frozen || ov) ticks = now;
    if (now - ticks > 100) ticks = now - 100;
    for (; now - ticks >= 17; ticks += 17) tick();
    if (state == S_RESET) {
      float dur = 0.4f + (float)reset_n * 0.02f, t = (float)(now - reset_t) / 1000.0f / dur;
      shown = (uint32_t)((float)sv.score * (1 - (t > 1 ? 1 : t)) + 0.5f);
      cam = cam_from + (cam_to - cam_from) * ease_io(t);
      if (t >= 1) new_game();
    } else {
      cam = cam_from + (cam_to - cam_from) * ease_io((float)(now - cam_t) / 300.0f);
    }
    eadk_display_wait_for_vblank();
    gfx_render(scene, 0, 0, SCREEN_H);
    uint32_t spent = (uint32_t)eadk_timing_millis() - now;
    if (spent < 16) eadk_timing_msleep(16 - spent);
  }
  save();
  gfx_buf = their_strip;
  return np_app_end();
}
