#pragma GCC optimize ("O2")  /* (drawn every frame: fast, while the rest of the game is built small) */
#include "scene.h"
#include "fx.h"
#include <math.h>
#include <string.h>
#include "../../common/np_text.h"

#define PX (2.0f / 3.0f)       /* screen pixels per GD unit */
#define FADE_W 75.0f

/* ------------------------------------------------------------ background */

void scene_background(color_t bg, float bg_x, float cam_y) {
  const float tex = 0.9f, tile = 512 * tex;
  float off = fmodf(bg_x * 0.1f, tile);
  if (off < 0) off += tile;
  float top = cam_y * 0.111f - 200;
  int y0 = gfx_y0, y1 = gfx_y1;
  for (int y = y0; y < y1; y++) {
    float ty = (y + .5f - top) / tex;
    if (ty < 0) ty = 0;
    if (ty > 511) ty = 511;
    float g = ty / 384.f;
    if (g > 1) g = 1;
    unsigned gi = (unsigned)(g * 256);
    color_t gap = c_scale(bg, gi), sq = c_scale(bg, gi * 216 / 256), sh = c_scale(bg, gi * 182 / 256);
    gfx_fill(0, y, GFX_W, 1, gap);
    int ity = (int)ty;
    for (float base = -off; base < GFX_W; base += tile) {
      for (int k = 0; k < BG_RECTS; k++) {
        const uint16_t *r = bg_rects[k];
        if (ity < r[1] || ity >= r[1] + r[3]) continue;
        int a = (int)floorf(base + r[0] * tex + .5f), b = (int)floorf(base + (r[0] + r[2]) * tex + .5f);
        if (b <= 0 || a >= GFX_W) continue;
        gfx_fill(a, y, b - a - 1, 1, sq);
        gfx_fill(b - 1, y, 1, 1, sh);
      }
    }
  }
}

static const uint8_t line_profile[23] = {3, 44, 87, 128, 170, 195, 204, 214, 223, 232, 241, 250, 243, 233, 224, 215, 206, 197, 176, 135, 92, 51, 9};

/* Ground band whose top edge (or bottom edge for a ceiling) is at top_y. */
void scene_ground(color_t g1, color_t line, float ground_x, int top_y, bool ceiling, bool shadows) {
  const float tile = 128 * PX;
  float off = fmodf(ground_x * PX, tile);
  if (off < 0) off += tile;
  int ya = ceiling ? 0 : top_y, yb = ceiling ? top_y : GFX_H;
  if (ya < gfx_y0) ya = gfx_y0;
  if (yb > gfx_y1) yb = gfx_y1;
  for (int y = ya; y < yb; y++) {
    float d = ceiling ? (top_y - 1 - y) : (y - top_y);   /* depth in px */
    float t = d * 1.5f;                                   /* depth in units */
    float in = t < 2 ? 1.0f : 0.85f - (t - 2) / 126.f * 0.65f;
    if (in < 0.2f) in = 0.2f;
    color_t base = c_scale(g1, (unsigned)(in * 256)), seam = c_scale(g1, (unsigned)((in + 0.13f > 1 ? 1 : in + 0.13f) * 256));
    gfx_fill(0, y, GFX_W, 1, base);
    for (float x = -off; x < GFX_W + tile; x += tile) {
      int s = (int)floorf(x + .5f);
      gfx_fill(s - 4, y, 8, 1, seam);
    }
  }
  if (shadows) {
    int h = ceiling ? top_y : GFX_H - top_y;
    int y = ceiling ? 0 : top_y;
    gfx_hgrad_alpha(0, y, 86, h, 0, 100, 0, BLEND_NORMAL);
    gfx_hgrad_alpha(GFX_W - 86, y, 86, h, 0, 0, 100, BLEND_NORMAL);
  }
  /* floor line: additive, brightest in the middle of the screen */
  int ly = ceiling ? top_y - 1 : top_y;
  if (ly >= gfx_y0 - 1 && ly < gfx_y1 + 1) {
    const int len = 296, x0 = (GFX_W - len) / 2;
    for (int i = 0; i < len; i++) {
      float f = i * 22.f / (len - 1);
      int k = (int)f;
      float fr = f - k;
      unsigned a = (unsigned)(line_profile[k] * (1 - fr) + line_profile[k < 22 ? k + 1 : 22] * fr);
      gfx_add(x0 + i, ly, 1, 1, line, a);
      gfx_add(x0 + i, ceiling ? ly - 1 : ly + 1, 1, 1, line, a / 3);
    }
  }
}

/* ------------------------------------------------------------ objects */

typedef struct {
  int16_t x, y;          /* anchor position, 1/4 px */
  uint16_t spr;          /* sprite, or tile program (K_PROG) */
  uint16_t scale;        /* 256 = 1 */
  int16_t angle;         /* extra clockwise rotation, 1/16 degree */
  uint8_t xf;
  uint8_t ct, detail;    /* palette entries: the colour (a program's base colour) and a program's detail colour */
  uint8_t alpha, kind;
  union {
    uint8_t layer;       /* the drawing order, until sorted */
    uint8_t strips;      /* then the strips it can touch: first | last << 4 */
  };
} Item;
/* sprite at a whole pixel; rotated / scaled sprite; four mirrored quarters
   of a round object; a block drawn from rectangles; the player. K_ADD:
   blend additively whatever the colour's own blending. */
enum { K_SPRITE, K_SCALED, K_QUAD, K_PROG, K_PLAYER, K_ADD = 0x80 };

/* Colours of the frame: one entry per colour channel of the level (copies,
   HSV, player colours and pulses resolved), then a few made per object
   (lighter shades, group pulses) and the fixed portal glows. */
typedef struct { color_t c; uint8_t a, add; } Pal;
enum { PAL_DYN = 224, PAL_DYN_END = 247, PAL_RAIN = 247, PAL_GLOW_Y = 253, PAL_GLOW_B = 254, PAL_GLOW_P = 255 };
enum { DYN_LIGHT, DYN_DARK, DYN_PULSE };
static Pal pal[256];
static uint16_t dyn_key[PAL_DYN_END - PAL_DYN];
static int ndyn;
#define MAX_ITEMS 900   /* Dash peaks near 800 */
static Item items[MAX_ITEMS];
static uint16_t order[MAX_ITEMS];
static int nitems;
static const Game *G;
static SceneOpts O;
static float player_sx, player_sy;
static color_t bg_col, g1_col, line_col;
static int ground_top, ceil_bottom, floor_bottom;
static bool draw_gfx_grounds;

color_t scene_channel(const Game *g, int ch) { return rgb(g->ch[ch].cur[0], g->ch[ch].cur[1], g->ch[ch].cur[2]); }

static int fade_at(const Level *L, float x) {
  int eff = 0;
  for (unsigned k = 0; k < L->event_count; k++) {
    const LevelEvent *e = &L->events[k];
    if (e->x >= x) break;
    if (e->kind == EV_FADE) eff = e->arg;
  }
  return eff;
}

static void push(float sx, float sy, int spr, int xf, int ct, int detail, int alpha, int layer, int kind, int scale, int angle16) {
  if (nitems >= MAX_ITEMS || spr < 0 || sx < -2000 || sx > 2000 || sy < -2000 || sy > 2000) return;
  items[nitems++] = (Item){.x = (int16_t)floorf(sx * 4 + .5f), .y = (int16_t)floorf(sy * 4 + .5f), .spr = (uint16_t)spr, .scale = (uint16_t)scale,
                           .angle = (int16_t)angle16, .xf = (uint8_t)xf, .ct = (uint8_t)ct, .detail = (uint8_t)detail,
                           .alpha = (uint8_t)(alpha > 255 ? 255 : alpha), .kind = (uint8_t)kind, .layer = (uint8_t)layer};
}

/* ------------------------------------------------------------ palette */

static color_t c3(const uint8_t v[3]) { return rgb(v[0], v[1], v[2]); }
/* Palette slots are handed out per frame: the fixed channels keep theirs,
   the level's other channels get one the first time an object shows them. */
#define MAX_LEVEL_CHANS 1024
static uint8_t pal_of[MAX_LEVEL_CHANS];   /* dense channel -> slot, 0xff: none yet */
static uint8_t slot_rgb[PAL_DYN][3];
static int next_slot;
static const Game *pal_game;

/* A channel's state: the live one, or the level's (it never changes). */
static const Channel *chan_get(const Game *g, unsigned c, Channel *tmp) {
  const LevelExt *x = g->L->ext;
  if (!x || c < x->ndyn) return &g->ch[c < MAX_CHANNELS ? c : 0];
  const LChan *l = &x->chans[c < x->nchans ? c : 0];
  memcpy(tmp->cur, l->rgb, 3);
  tmp->op = l->opacity;
  tmp->flags = l->flags;
  tmp->copy = l->copy;
  tmp->h = l->h; tmp->s = l->s; tmp->v = l->v;
  return tmp;
}

static int pal_slot(const Game *g, unsigned c);
static void pulse_rgb(const PulseAction *pu, uint8_t out[3]);
static const uint8_t *chan_rgb(const Game *g, unsigned c) { return slot_rgb[pal_slot(g, c)]; }

/* Evaluates channel c into slot s: its own colour, a player colour, or a
   copy of another with HSV, then any pulses on it. */
static void chan_fill(const Game *g, unsigned c, int s) {
  Channel tmp;
  const Channel *ch = chan_get(g, c, &tmp);
  uint8_t *out = slot_rgb[s];
  memcpy(out, ch->cur, 3);
  if ((ch->flags & CHF_COPY) && ch->copy != c && ch->copy < MAX_LEVEL_CHANS)
    hsv_shift(chan_rgb(g, ch->copy), ch->h, ch->s, ch->v, ch->flags, out);
  else if (ch->flags & (CHF_P1 | CHF_P2)) {
    color_t pc = (ch->flags & CHF_P1) ? O.p1 : O.p2;
    out[0] = (uint8_t)c_r(pc); out[1] = (uint8_t)c_g(pc); out[2] = (uint8_t)c_b(pc);
  }
  for (unsigned k = 0; k < g->npu; k++) {
    const PulseAction *pu = &g->pu[k];
    if ((pu->flags & 1) || pu->target != c) continue;
    uint8_t prgb[3];
    pulse_rgb(pu, prgb);
    float lv = pulse_level(pu);
    for (int j = 0; j < 3; j++) out[j] = (uint8_t)(out[j] + (prgb[j] - out[j]) * lv);
  }
  pal[s] = (Pal){c3(out), g->L->ext ? ch->op : 255, g->L->ext ? (ch->flags & CHF_BLEND) != 0 : 0};
}

static int pal_slot(const Game *g, unsigned c) {
  if (c < CH_SPECIAL) return (int)c;
  const LevelExt *x = g->L->ext;
  if (!x || c >= x->nchans || c >= MAX_LEVEL_CHANS) return CH_WHITE;
  if (pal_of[c] != 0xff) return pal_of[c];
  if (next_slot >= PAL_DYN) return CH_WHITE;   /* more colours on screen than slots */
  int s = next_slot++;
  pal_of[c] = (uint8_t)s;                      /* before filling: copies can loop */
  chan_fill(g, c, s);
  return s;
}

static void pulse_rgb(const PulseAction *pu, uint8_t out[3]) {
  if ((pu->flags & CHF_COPY) && pu->copy < MAX_LEVEL_CHANS) hsv_shift(chan_rgb(pal_game, pu->copy), pu->h, pu->s, pu->v, pu->flags, out);
  else memcpy(out, pu->rgb, 3);
}

static void build_palette(const Game *g) {
  const LevelExt *x = g->L->ext;
  pal_game = g;
  memset(pal_of, 0xff, x && x->nchans < MAX_LEVEL_CHANS ? x->nchans : MAX_LEVEL_CHANS);
  next_slot = CH_SPECIAL;
  for (unsigned c = 0; c < CH_SPECIAL; c++) chan_fill(g, c, (int)c);
  /* the fixed ones */
  Channel tmp;
  pal[CH_P1] = (Pal){x && (chan_get(g, CH_P1, &tmp)->flags & CHF_COPY) ? pal[CH_P1].c : O.p1, 255, 1};
  pal[CH_P2] = (Pal){x && (chan_get(g, CH_P2, &tmp)->flags & CHF_COPY) ? pal[CH_P2].c : O.p2, 255, 1};
  pal[CH_LBG] = (Pal){c_hsv_lighten(pal[CH_BG].c, -20, 20), 255, 0};
  if (!x) { pal[CH_3DL] = pal[CH_OBJ]; pal[CH_G2] = pal[CH_G1]; }
  pal[CH_BLACK] = (Pal){0, 255, 0};
  pal[CH_WHITE] = (Pal){0xffff, 255, 0};
  pal[CH_LIGHTER] = (Pal){0xffff, 255, 0};
  static const uint8_t fixed_rgb[][3] = {{0, 0, 0}, {255, 255, 255}, {255, 255, 255}};
  memcpy(slot_rgb[CH_BLACK], fixed_rgb[0], 3);
  memcpy(slot_rgb[CH_WHITE], fixed_rgb[1], 3);
  memcpy(slot_rgb[CH_LIGHTER], fixed_rgb[2], 3);
  pal[PAL_GLOW_Y] = (Pal){rgb(255, 255, 0), 255, 1};
  pal[PAL_GLOW_B] = (Pal){rgb(0, 255, 255), 255, 1};
  pal[PAL_GLOW_P] = (Pal){rgb(255, 0, 255), 255, 1};
  static const uint8_t rain[6][3] = {{255, 30, 30}, {255, 150, 0}, {255, 240, 0}, {40, 230, 40}, {30, 120, 255}, {170, 40, 255}};
  for (int k = 0; k < 6; k++) pal[PAL_RAIN + k] = (Pal){c3(rain[k]), 255, 0};
  ndyn = 0;
}

/* A per-frame colour made from entry `base`: a lighter or darker shade, or
   group pulse k over it (DYN_PULSE + k). Falls back to base when the slots
   run out. */
static int pal_dyn(const Game *g, int base, int kind) {
  uint16_t key = (uint16_t)(base | kind << 8);
  for (int k = 0; k < ndyn; k++) if (dyn_key[k] == key) return PAL_DYN + k;
  if (ndyn >= PAL_DYN_END - PAL_DYN) return base;
  Pal e = pal[base];
  if (kind == DYN_LIGHT) {
    e.c = c_mix(e.c, 0xffff, 110);
  } else if (kind == DYN_DARK) {
    e.c = c_scale(e.c, 150);
  } else {
    const PulseAction *pu = &g->pu[kind - DYN_PULSE];
    uint8_t prgb[3];
    pulse_rgb(pu, prgb);
    e.c = c_mix(e.c, c3(prgb), (unsigned)(pulse_level(pu) * 256));
  }
  dyn_key[ndyn] = key;
  pal[PAL_DYN + ndyn] = e;
  return PAL_DYN + ndyn++;
}

/* The palette entry of a part's colour type for an object whose base and
   detail entries are given (legacy colour types keep their fixed colours
   unless the object is painted). */
static int part_pal(int ct, int base, int detail, bool painted) {
  if (ct >= CT_RAIN0 && ct <= CT_RAIN5) return PAL_RAIN + ct - CT_RAIN0;
  switch (ct) {
    case CT_BASE_D: return pal_dyn(G, base, DYN_DARK);
    case CT_BASE_L: return pal_dyn(G, base, DYN_LIGHT);
    case CT_DETAIL_D: return pal_dyn(G, detail, DYN_DARK);
    case CT_BLACK: return CH_BLACK;
    case CT_WHITE: return CH_WHITE;
    case CT_GLOW_Y: return PAL_GLOW_Y;
    case CT_GLOW_B: return PAL_GLOW_B;
    case CT_GLOW_P: return PAL_GLOW_P;
    case CT_DETAIL: return detail;
    case CT_BASE: return base;
    case CT_OBJ: case CT_GLOW: return painted ? base : CH_OBJ;
    case CT_P1ADD: return painted ? base : CH_P1;
    case CT_P2ADD: return painted ? base : CH_P2;
    case CT_LBG: return painted ? base : CH_LBG;
    default: return CH_WHITE;
  }
}

static float pulse_amount(void) {
  unsigned bpm = G && G->L->bpm ? G->L->bpm : 120;
  float beat = O.time * bpm / 60.f;
  float ph = beat - floorf(beat);
  return ph < 0.25f ? 1 - ph * 4 : 0;
}

/* Rotate a part offset (units) by the object's transform. */
static void part_offset(int xf, float dx, float dy, float *ox, float *oy) {
  if (xf & XF_FLIPX) dx = -dx;
  if (xf & XF_FLIPY) dy = -dy;
  switch (xf & 3) {
    case 1: *ox = dy; *oy = -dx; break;       /* clockwise 90: (x,y) -> (y,-x) in y-up space */
    case 2: *ox = -dx; *oy = -dy; break;
    case 3: *ox = -dy; *oy = dx; break;
    default: *ox = dx; *oy = dy; break;
  }
}

/* ------------------------------------------------------------ programs */

/* Tile programs: rectangles, alpha ramps and convex polygons in half units
   around the item's anchor (2 units with TOP_BIG), y up, turned, flipped and
   scaled like the object. */
typedef struct { float cx, cy, m00, m01, m10, m11; bool aligned; } ProgXf;
typedef struct { int op, n, ct; unsigned a0, a1; float q[16]; } ProgOp;

/* A program is left out of strips beyond 60 units from its anchor. */
static bool prog_culled(const Item *it, int y0, int y1) {
  float cy = it->y * 0.25f, s = PX * 0.5f * it->scale / 256;
  return cy + 60 * s * 2 < y0 || cy - 60 * s * 2 > y1;
}

/* screen = centre + M * local (clockwise turn, y up locally, y down on screen) */
static void prog_xform(const Item *it, ProgXf *m) {
  float s = PX * 0.5f * it->scale / 256;
  m->cx = it->x * 0.25f;
  m->cy = it->y * 0.25f;
  float a = ((it->xf & 3) * 90 + it->angle / 16.f) * (3.14159265f / 180), ca = cosf(a), sa = sinf(a);
  if (fabsf(ca) < 1e-4f) ca = 0;
  if (fabsf(sa) < 1e-4f) sa = 0;
  float fx = (it->xf & XF_FLIPX) ? -s : s, fy = (it->xf & XF_FLIPY) ? -s : s;
  m->m00 = ca * fx;
  m->m01 = sa * fy;
  m->m10 = sa * fx;
  m->m11 = -ca * fy;
  m->aligned = ca == 0 || sa == 0;
}

/* Decodes the op at p (not the end mark), returns the next one. */
static const uint8_t *prog_op(const uint8_t *p, ProgOp *o) {
  o->op = p[0] & 0x7f;
  float k4 = (p[0] & TOP_BIG) ? 4 : 1;
  o->n = 4;
  if (o->op == TOP_POLY) {
    o->n = p[1];
    for (int k = 0; k < o->n * 2; k++) o->q[k] = (int8_t)p[2 + k] * k4;
    p += 2 + o->n * 2;
    o->ct = p[0];
    o->a0 = o->a1 = p[1];
    return p + 2;
  }
  for (int k = 0; k < 4; k++) o->q[k] = (int8_t)p[1 + k] * k4;
  o->ct = p[5];
  o->a0 = p[6];
  o->a1 = o->op == TOP_VGRAD ? p[7] : o->a0;
  return p + (o->op == TOP_VGRAD ? 8 : 7);
}

/* Screen rows around an op's shapes: its corners, with three rows to spare
   (the drawing adds at most one, and the ramp's bands round). */
static void prog_op_rows(const ProgXf *m, const ProgOp *o, float *lo, float *hi) {
  float mn = 1e9f, mx = -1e9f;
  int n = o->op == TOP_POLY ? o->n : 4;
  for (int k = 0; k < n; k++) {
    float lx = o->op == TOP_POLY ? o->q[2 * k] : o->q[(k & 1) ? 2 : 0];
    float ly = o->op == TOP_POLY ? o->q[2 * k + 1] : o->q[(k & 2) ? 3 : 1];
    float y = m->cy + m->m10 * lx + m->m11 * ly;
    if (y < mn) mn = y;
    if (y > mx) mx = y;
  }
  *lo = mn - 3;
  *hi = mx + 3;
}

/* The palette entry of an op's colour; takes a shade slot the first time. */
static int prog_pal(const Item *it, int ct) {
  int e = part_pal(ct, it->ct, it->detail, true);
  if (ct == CT_OBJ || ct == CT_P1ADD || ct == CT_P2ADD || ct == CT_LBG) e = it->ct;
  return e;
}

/* ------------------------------------------------------------ per frame */

/* First | last << 4 for the strips holding rows [r0, r1). */
static uint8_t strip_range(int r0, int r1) {
  if (r0 < 0) r0 = 0;
  if (r1 > GFX_H) r1 = GFX_H;
  if (r0 >= r1) return 0x0f;   /* none */
  return (uint8_t)(r0 / STRIP_H | (r1 - 1) / STRIP_H << 4);
}
_Static_assert(STRIPS <= 15, "strip numbers are kept in 4 bits");

/* Once per frame, after sorting: the strips each item can touch (drawing
   clips to the strip anyway, so this only skips calls that would draw
   nothing), and the shades the tile programs take. Drawing takes a shade
   (pal_dyn) the first time a strip reaches its program and the slots can
   run out, so they are taken here in that order: strip by strip, in
   drawing order. */
static void prepare_items(void) {
  uint8_t first[MAX_ITEMS];   /* the first strip that reaches a program */
  uint16_t count[STRIPS + 1] = {0};
  for (int k = 0; k < nitems; k++) {
    Item *it = &items[order[k]];
    int kind = it->kind & 0x7f, r0 = 0, r1 = GFX_H;
    first[k] = STRIPS;
    if (kind == K_PROG) {
      for (int s = 0; s < STRIPS; s++)
        if (!prog_culled(it, s * STRIP_H, s * STRIP_H + STRIP_H)) { first[k] = (uint8_t)s; break; }
      ProgXf m;
      prog_xform(it, &m);
      float lo = 1e9f, hi = -1e9f;
      for (const uint8_t *p = tile_prog_data + tile_prog_off[it->spr]; *p;) {
        ProgOp o;
        float a, b;
        p = prog_op(p, &o);
        prog_op_rows(&m, &o, &a, &b);
        if (a < lo) lo = a;
        if (b > hi) hi = b;
      }
      if (!(lo >= -1000)) lo = -1000;   /* (NaN too) */
      if (!(hi <= 1000)) hi = 1000;
      r0 = (int)floorf(lo);
      r1 = (int)ceilf(hi) + 1;
    } else if (kind == K_SPRITE) {
      gfx_sprite_rows(it->spr, (it->y + 2) >> 2, it->xf, &r0, &r1);
    } else if (kind != K_PLAYER) {
      gfx_sprite_ex_rows(it->spr, it->x * 4, it->y * 4, (it->xf & 3) * 90 * 16 + it->angle, it->scale, &r0, &r1);
    }
    count[first[k]]++;
    it->strips = strip_range(r0, r1);
  }
  for (int s = 0; s < STRIPS; s++)
    for (int k = 0, left = count[s]; k < nitems && left; k++) {
      if (first[k] != s) continue;
      left--;
      const Item *it = &items[order[k]];
      for (const uint8_t *p = tile_prog_data + tile_prog_off[it->spr]; *p;) {
        ProgOp o;
        p = prog_op(p, &o);
        prog_pal(it, o.ct);
      }
    }
}

void scene_prepare(const Game *g, const SceneOpts *o) {
  G = g;
  O = *o;
  nitems = 0;
  const Level *L = g->L;
  bg_col = scene_channel(g, CH_BG);
  g1_col = scene_channel(g, CH_G1);
  line_col = scene_channel(g, CH_LINE);
  float cam_x = g->cam_x;
  float pulse = pulse_amount();
  unsigned coin_frame = (unsigned)(o->time * 8) & 3;
  build_palette(g);
  const LevelExt *X = L->ext;
  /* z layer buckets of 2.0 objects (0 = the part's own layer) */
  static const uint8_t ZMAP[9] = {0, LAYER_B4, LAYER_DECO_BACK, LAYER_RODS, LAYER_DETAIL, LAYER_BLOCK, LAYER_T2, LAYER_T3, LAYER_T3};
  LIter it;
  unsigned frame = o->editor ? 0 : g->frame & 3;
  if (frame) {
    /* a turned frame: the world box the view covers */
    float xs[2] = {cam_x - 90, cam_x + VIEW_W + 90}, ys[2] = {g->cam_y - GROUND_OFFSET - 90, g->cam_y + VIEW_H + 90};
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int a = 0; a < 2; a++)
      for (int b = 0; b < 2; b++) {
        float X, Y;
        to_world(g, xs[a], ys[b], &X, &Y);
        x0 = fminf(x0, X); x1 = fmaxf(x1, X); y0 = fminf(y0, Y); y1 = fmaxf(y1, Y);
      }
    level_iter_y(L, x0, x1, y0, y1, &it);
  } else {
    level_iter(L, cam_x - 90, cam_x + VIEW_W + 90, &it);
  }
  for (const RObj *ob; (ob = level_next(&it));) {
    unsigned i = it.gi;
    const ObjDef *d = &objdefs[ob->type];
    const LStyle *st = obj_style(L, ob);
    float obx = obj_x(ob), oby = obj_y(ob), gang = 0;
    unsigned galpha = 255;
    int gpulse = -1;
    RObj turned;
    if (st->groups) {
      float dx, dy;
      bool on;
      gset_state(g, st->groups, &dx, &dy, &galpha, &on);
      if (!on || !galpha) continue;
      if (g->nrg) obj_where_rot(g, ob, &obx, &oby, &gang);
      else { obx += dx; oby += dy; }
      for (unsigned k = 0; k < g->npu && gpulse < 0; k++) {
        const PulseAction *pu = &g->pu[k];
        if (!(pu->flags & 1)) continue;
        const uint16_t *set = X->gsets + st->groups;
        for (unsigned j = 1; j <= set[0]; j++) if (set[j] == pu->target) { gpulse = (int)k; break; }
      }
    }
    if ((st_flags(st) & STF_HIDE) && !o->editor) continue;
    if (frame || gang != 0) {
      /* seen from a turned frame, or turned by a rotate trigger */
      if (frame) to_local(g, obx, oby, &obx, &oby);
      turned = *ob;
      int turn = -(int)frame * 256 + (int)lroundf(gang * (1024.f / 360));
      turned.ys = (turned.ys & ~(1023u << 20)) | ((unsigned)((int)obj_rot(ob) + turn + 4096) & 1023) << 20;
      ob = &turned;
    }
    if (obx < cam_x - 90 || obx >= cam_x + VIEW_W + 90) continue;
    if ((d->special == SP_COIN || d->special == SP_KEY) && game_used(g, ob, i)) continue;
    /* low detail: no decoration, but for the pixel art Dash is built of (Geometry Dash's own low detail mode only
     * leaves out objects marked High Detail, and its main levels have none) */
    if (o->low_detail && d->hit == HIT_NONE && !o->editor && !(d->nparts && (OBJ_PART(d, 0)->flags & PF_TERRAIN))) continue;
    int xf = obj_xf(ob);
    float angle = 0;   /* degrees on top of the quarter turns in xf */
    if (xf < 0) { angle = obj_rot(ob) * (360.f / 1024); xf = (int)(obj_flips(ob) << 2); }
    float rel = obx - cam_x;
    float fade = o->editor || (st_flags(st) & STF_DONT_FADE) ? 1 : rel < 0 || rel > VIEW_W ? 0 : rel < FADE_W ? rel / FADE_W : rel > VIEW_W - FADE_W ? (VIEW_W - rel) / FADE_W : 1;
    if (fade <= 0) continue;
    float offx = 0, offy = 0, sc = 1;
    if (fade < 1 && !(st_flags(st) & STF_DONT_ENTER)) {
      int eff = X ? g->fade_effect : fade_at(L, rel < VIEW_W / 2 ? obx + 75 : obx - (VIEW_W - PLAYER_SCREEN_X) + 75);
      float off = (1 - fade) * 127.5f;
      bool high = oby - g->cam_y > VIEW_H / 2 - GROUND_OFFSET;
      switch (eff) {
        case 1: offy = -off; break;          /* rises into place from below */
        case 2: offy = off; break;
        case 3: offx = -off; break;
        case 4: offx = off; break;
        case 5: sc = fade; break;
        case 6: sc = 1 + (1 - fade) / 2; break;
        case 8: offx = high ? -off : off; break;   /* halve left / right */
        case 9: offx = high ? off : -off; break;
        case 10: offy = high ? off : -off; break;  /* halve: the top half from above */
        case 11: offy = high ? -off : off; break;  /* inverse halve */
        default: break;
      }
    }
    float oscx = 1, oscy = 1;
    obj_scale_xy(L, ob, &oscx, &oscy);
    float osc = fabsf(oscx) > fabsf(oscy) ? fabsf(oscx) : fabsf(oscy);
    float wx = obx + offx, wy = oby + offy;
    int alpha = (int)(fade * galpha + .5f);
    if (d->special == SP_COIN && (o->coins_saved >> game_coin_index(L, i) & 1)) alpha = alpha * 2 / 5;
    if (d->anim == ANIM_SAW || d->anim == ANIM_SPIN)
      angle += (d->anim == ANIM_SAW ? 360.f : 180.f) * o->time * ((i * 2654435761u >> 16 & 1) ? -1 : 1);
    else if (d->anim == ANIM_INVIS && !o->editor) {
      /* invisible objects show faintly near the player */
      float dist = fabsf(obx - g->p.x);
      alpha = (int)(alpha * fmaxf(0, fminf(1, (210 - dist) / 90)) * 0.7f);
      if (alpha <= 0) continue;
    }
    /* colours: the object's own channels, or its paint */
    bool painted = st_main(st) != ST_NONE;
    int base = pal_slot(g, painted ? st_main(st) : d->dbase), detail = pal_slot(g, st_detail(st) != ST_NONE ? st_detail(st) : d->ddetail);
    if (base == CH_LIGHTER) base = pal_dyn(g, CH_OBJ, DYN_LIGHT);
    if (detail == CH_LIGHTER) detail = pal_dyn(g, base, DYN_LIGHT);
    if (gpulse >= 0) {
      const PulseAction *pu = &g->pu[gpulse];
      if (!(pu->flags & 4)) base = pal_dyn(g, base, DYN_PULSE + gpulse);
      if (!(pu->flags & 2)) detail = pal_dyn(g, detail, DYN_PULSE + gpulse);
    }
    for (int k = 0; k < d->nparts; k++) {
      const ObjPart *pt = OBJ_PART(d, k);
      if (o->low_detail && pt->ctype >= CT_GLOW && pt->ctype <= CT_GLOW_P) continue;
      int spr = pt->sprite;
      if (pt->flags & PF_RANDOM3) spr += (int)(i % 3);
      if (pt->flags & PF_COIN) spr += (int)coin_frame;
      float ox, oy;
      part_offset(xf, pt->dx4 / 4.f * sc * osc, pt->dy4 / 4.f * sc * osc, &ox, &oy);
      if (angle != 0 && (ox != 0 || oy != 0)) {
        float r = -angle * 3.14159265f / 180.f, c = cosf(r), s = sinf(r), t = ox * c - oy * s;
        oy = ox * s + oy * c;
        ox = t;
      }
      float sx = (wx + ox - cam_x) * PX, sy = 240 - (GROUND_OFFSET + wy + oy - g->cam_y) * PX;
      int psc = (int)(sc * osc * 256);
      if (pt->flags & PF_PULSE) psc = (int)(psc * (1 + 0.22f * pulse));
      if (pt->flags & PF_HALF) psc *= 2;
      int a16 = (int)(angle * 16);
      int kind = pt->prog ? K_PROG : (pt->flags & PF_QUAD) ? K_QUAD : (psc != 256 || a16) ? K_SCALED : K_SPRITE;
      int ct = pt->prog ? base : part_pal(pt->ctype, base, detail, painted);
      if (pt->ctype == CT_GLOW) kind |= K_ADD;
      int layer = pt->layer;
      if (st_zlayer(st)) {
        bool glow = layer == LAYER_BLOCK_GLOW || layer == LAYER_SPECIAL_GLOW;
        layer = ZMAP[st_zlayer(st)];
        if (glow && layer > 0) layer--;
      }
      int a = alpha * pal[ct].a / 255;
      if (a <= 0 && !pt->prog) continue;
      push(sx, sy, pt->prog ? pt->prog : spr, xf, ct, detail, pt->prog ? alpha : a, layer, kind, psc, a16);
    }
  }
  /* the player */
  player_sx = wx_to_sx(g, g->p.x);
  player_sy = wy_to_sy(g, g->p.y);
  if (!g->dead && !o->hide_player) push(0, 0, 0, 0, 0, 0, 255, LAYER_PLAYER, K_PLAYER, 256, 0);
  /* sort by layer, stable */
  uint16_t count[LAYER_COUNT + 1] = {0};
  for (int k = 0; k < nitems; k++) count[items[k].layer + 1]++;
  for (int k = 1; k <= LAYER_COUNT; k++) count[k] += count[k - 1];
  for (int k = 0; k < nitems; k++) order[count[items[k].layer]++] = (uint16_t)k;
  /* ground positions */
  ground_top = (int)floorf(wy_to_sy(g, 0) + .5f);
  draw_gfx_grounds = g->ground_gfx > 2;
  if (frame) { ground_top = GFX_H + 1; draw_gfx_grounds = false; }   /* no ground in a turned frame */
  floor_bottom = (int)floorf(240 - g->ground_gfx * PX + .5f);
  ceil_bottom = (int)floorf(g->ground_gfx * PX + .5f);
  prepare_items();
}

/* Colour of a palette entry. */
static void pal_for(int e, bool add, color_t *c, int *mode, unsigned *alpha) {
  *c = pal[e].c;
  *mode = add || pal[e].add ? BLEND_ADD : BLEND_NORMAL;
  *alpha = pal[e].a;
}

static float icon_phase;
static bool icon_air;

void scene_draw_player_icon(int mode, float cx, float cy, float rot, float scale, color_t p1, color_t p2, bool upside, unsigned alpha) {
  int x16 = (int)(cx * 16), y16 = (int)(cy * 16), a16 = (int)(rot * 16);
  if (mode == MODE_SHIP || mode == MODE_UFO) {
    bool ship = mode == MODE_SHIP;
    float r = rot * 3.14159265f / 180.f, s = upside ? -1.f : 1.f;
    /* the pilot sits in the cockpit (or under the dome), at half size */
    float lx = 0, ly = (ship ? -6.5f : -4.0f) * PX * s * scale;
    float px_ = cx + lx * cosf(r) - ly * sinf(r), py_ = cy + lx * sinf(r) + ly * cosf(r);
    int fl = upside ? XF_FLIPY : 0, sc = (int)(256 * scale);
    if (!ship) gfx_sprite_ex(SPR_UFO1_DOME, x16, y16, a16, sc, fl, 0xffff, alpha, BLEND_NORMAL);
    gfx_sprite_ex(SPR_CUBE1_S, (int)(px_ * 16), (int)(py_ * 16), a16, sc / 2, fl, p2, alpha, BLEND_NORMAL);
    gfx_sprite_ex(SPR_CUBE1_P, (int)(px_ * 16), (int)(py_ * 16), a16, sc / 2, fl, p1, alpha, BLEND_NORMAL);
    if (ship) {
      gfx_sprite_ex(SPR_SHIP1_S, x16, y16 + (int)(2 * 16 * s * scale), a16, sc, fl, p2, alpha, BLEND_NORMAL);
      gfx_sprite_ex(SPR_SHIP1_P, x16, y16 + (int)(2 * 16 * s * scale), a16, sc, fl, p1, alpha, BLEND_NORMAL);
    } else {
      gfx_sprite_ex(SPR_UFO1_S, x16, y16, a16, sc, fl, p2, alpha, BLEND_NORMAL);
      gfx_sprite_ex(SPR_UFO1_P, x16, y16, a16, sc, fl, p1, alpha, BLEND_NORMAL);
    }
    return;
  }
  if (mode == MODE_ROBOT || mode == MODE_SPIDER) {
    /* legs first (behind the body), then the body */
    float r = rot * 3.14159265f / 180.f, cr = cosf(r), sr = sinf(r), k = PX * scale, s = upside ? -1.f : 1.f;
    float ph = icon_phase, run = icon_air ? 0 : 1;
    int nl = mode == MODE_ROBOT ? 2 : 4;
    for (int pass = 0; pass < 2; pass++)
      for (int l = 0; l < nl; l++) {
        float side = (l & 1) ? 1 : -1, sw = sinf(ph + (l & 1) * 3.14159f) * run;
        float hx, hy, kx, ky, fx_, fy;
        if (mode == MODE_ROBOT) {
          hx = side * 4 - 1; hy = -3;
          fx_ = hx + (icon_air ? side * 3 - 3 : sw * 7); fy = icon_air ? -12 : -15 + fmaxf(0, cosf(ph + (l & 1) * 3.14159f)) * 3 * run;
          kx = (hx + fx_) / 2 + 3; ky = (hy + fy) / 2;
        } else {
          float spread = l < 2 ? 7 : 12;
          hx = side * (l < 2 ? 5 : 9); hy = -5;
          fx_ = side * spread + sw * 3; fy = icon_air ? -13 : -15;
          kx = side * (spread + 4); ky = -8;
        }
        float pts[3][2] = {{hx, hy}, {kx, ky}, {fx_, fy}};
        float sc[3][2];
        for (int q = 0; q < 3; q++) {
          float lx = pts[q][0] * k, ly = -pts[q][1] * k * s;
          sc[q][0] = cx + lx * cr - ly * sr;
          sc[q][1] = cy + lx * sr + ly * cr;
        }
        float w = (pass ? 2.2f : 4.4f) * k;
        for (int q = 0; q < 2; q++) {
          float dx = sc[q + 1][0] - sc[q][0], dy = sc[q + 1][1] - sc[q][1], len = sqrtf(dx * dx + dy * dy) + 1e-3f;
          float nx = -dy / len * w * .5f, ny = dx / len * w * .5f, ex = dx / len * w * .3f, ey = dy / len * w * .3f;
          float quad[8] = {sc[q][0] + nx - ex, sc[q][1] + ny - ey, sc[q + 1][0] + nx + ex, sc[q + 1][1] + ny + ey,
                           sc[q + 1][0] - nx + ex, sc[q + 1][1] - ny + ey, sc[q][0] - nx - ex, sc[q][1] - ny - ey};
          gfx_poly(quad, 4, pass ? p1 : 0, alpha, BLEND_NORMAL);
        }
      }
    int fl = upside ? XF_FLIPY : 0;
    bool robot = mode == MODE_ROBOT;
    gfx_sprite_ex(robot ? SPR_ROBOT1_S : SPR_SPIDER1_S, x16, y16, a16, (int)(256 * scale), fl, p2, alpha, BLEND_NORMAL);
    gfx_sprite_ex(robot ? SPR_ROBOT1_P : SPR_SPIDER1_P, x16, y16, a16, (int)(256 * scale), fl, p1, alpha, BLEND_NORMAL);
    return;
  }
  if (mode == MODE_WAVE || mode == MODE_SWING) {
    bool wave = mode == MODE_WAVE;
    int fl = upside ? XF_FLIPY : 0;
    gfx_sprite_ex(wave ? SPR_WAVE1_S : SPR_SWING1_S, x16, y16, a16, (int)(256 * scale), fl, p2, alpha, BLEND_NORMAL);
    gfx_sprite_ex(wave ? SPR_WAVE1_P : SPR_SWING1_P, x16, y16, a16, (int)(256 * scale), fl, p1, alpha, BLEND_NORMAL);
    return;
  }
  bool ball = mode == MODE_BALL;
  gfx_sprite_ex(ball ? SPR_BALL1_S : SPR_CUBE1_S, x16, y16, a16, (int)(256 * scale), 0, p2, alpha, BLEND_NORMAL);
  gfx_sprite_ex(ball ? SPR_BALL1_P : SPR_CUBE1_P, x16, y16, a16, (int)(256 * scale), 0, p1, alpha, BLEND_NORMAL);
}

/* Draws the ops of a tile program that reach the current strip. Colour
   types BASE and DETAIL are the object's. */
static void draw_prog(const Item *it) {
  if (prog_culled(it, gfx_y0, gfx_y1)) return;
  ProgXf m;
  prog_xform(it, &m);
  const float cx = m.cx, cy = m.cy, m00 = m.m00, m01 = m.m01, m10 = m.m10, m11 = m.m11;
  bool add = (it->kind & K_ADD) != 0;
  for (const uint8_t *p = tile_prog_data + tile_prog_off[it->spr]; *p;) {
    ProgOp o;
    p = prog_op(p, &o);
    float lo, hi;
    prog_op_rows(&m, &o, &lo, &hi);
    if (hi < gfx_y0 || lo > gfx_y1) continue;   /* its shade was taken in prepare_items */
    const float *q = o.q;
    int ct = o.ct, n = o.n;
    unsigned a0 = o.a0, a1 = o.a1;
    int e = prog_pal(it, ct);
    color_t c;
    int mode;
    unsigned pa;
    pal_for(e, add || ct == CT_GLOW, &c, &mode, &pa);
    unsigned al = (it->alpha + 1) * pa / 255;
    if (!al) continue;
    if (o.op == TOP_POLY) {
      float xy[16];
      for (int k = 0; k < n; k++) {
        xy[2 * k] = cx + m00 * q[2 * k] + m01 * q[2 * k + 1];
        xy[2 * k + 1] = cy + m10 * q[2 * k] + m11 * q[2 * k + 1];
      }
      gfx_poly(xy, n, c, a0 * al >> 8, mode);
      continue;
    }
    int bands = a0 == a1 ? 1 : 6;
    for (int b = 0; b < bands; b++) {
      /* band b of the rectangle along its local y (top first) */
      float yt = q[3] + (q[1] - q[3]) * b / bands, yb = q[3] + (q[1] - q[3]) * (b + 1) / bands;
      unsigned ab = bands == 1 ? a0 : (unsigned)(a0 + ((int)a1 - (int)a0) * (b + .5f) / bands);
      float lx[4] = {q[0], q[2], q[2], q[0]}, ly[4] = {yt, yt, yb, yb}, xy[8];
      for (int k = 0; k < 4; k++) {
        xy[2 * k] = cx + m00 * lx[k] + m01 * ly[k];
        xy[2 * k + 1] = cy + m10 * lx[k] + m11 * ly[k];
      }
      if (m.aligned) {
        float x0 = fminf(fminf(xy[0], xy[2]), xy[4]), x1 = fmaxf(fmaxf(xy[0], xy[2]), xy[4]);
        float y0 = fminf(fminf(xy[1], xy[3]), xy[5]), y1 = fmaxf(fmaxf(xy[1], xy[3]), xy[5]);
        gfx_rectf(x0, y0, x1, y1, c, ab * al >> 8, mode);
      } else {
        gfx_poly(xy, 4, c, ab * al >> 8, mode);
      }
    }
  }
}

static void draw_body(const Player *p, float sx, float sy, bool swap) {
  icon_phase = p->x * (3.14159265f / 30);
  icon_air = !(p->on_ground || p->on_ceiling);
  scene_draw_player_icon(p->mode, sx, sy, p->rot, p->mini ? 0.6f : 1, swap ? O.p2 : O.p1, swap ? O.p1 : O.p2, p->upside, 256);
}

static void draw_player(void) {
  fx_draw_player_trail();
  /* the dual's second body wears the colours the other way round */
  if (G->dual) draw_body(&G->p2, wx_to_sx(G, G->p2.x), wy_to_sy(G, G->p2.y), G->p2.second);
  draw_body(&G->p, player_sx, player_sy, G->p.second);
}

static void draw_end_wall(void) {
  if (G->wall_y <= 0) return;
  int x = (int)floorf(wx_to_sx(G, G->L->wall_x) + .5f);
  if (x > GFX_W + 20) return;
  float oy = fmodf(G->cam_y, 30);
  for (float wy = -30; wy < VIEW_H + 60; wy += 30) {
    int y = (int)floorf(240 - (wy - oy + 15) * PX + .5f);
    gfx_sprite(SPR_GRID_T, x, y, 3, pal[CH_OBJ].c, 255, BLEND_NORMAL);
  }
  gfx_hgrad_alpha(x - 26, 0, 20, GFX_H, O.p1, 0, 170, BLEND_ADD);
}

static void draw_attempt(void) {
  if (O.editor || !O.attempt) return;
  float sx = wx_to_sx(G, O.attempt_x);
  if (sx < -200 || sx > 520) return;
  char buf[32];
  int n = (int)strlen(T("ATTEMPT "));
  memcpy(buf, T("ATTEMPT "), (size_t)n);
  gfx_format_uint(buf + n, O.attempt);
  int base = (int)floorf(wy_to_sy(G, O.attempt_y) + fonts[FONT_HUGE].cap / 2 + .5f);
  gfx_text_center(FONT_HUGE, (int)sx, base, buf, 0xffff, 0xffff, 256);
}

static void draw_hud(void) {
  if (O.editor) return;
  /* progress bar, top centre */
  float prog = game_progress(G);
  const int bw = 160, bh = 8, bx = (GFX_W - bw) / 2, by = 5;
  if (O.show_bar) {
    gfx_round_rect(bx - 2, by - 2, bw + 4, bh + 4, 5, 0xffff, 170);
    gfx_round_rect(bx, by, bw, bh, 4, 0, 200);
    int fw = (int)(prog / 100 * (bw - 2));
    if (fw > 0) {
      color_t c = O.p1 ? O.p1 : 0xffff;   /* the player's colour, as in 2.2 */
      int w = fw < 6 ? 6 : fw;
      gfx_round_rect(bx + 1, by + 1, w, bh - 2, 3, c, 256);
      gfx_add(bx + 3, by + 2, w - 4, 1, 0xffff, 90);
    }
  }
  if (O.show_percent) {
    char buf[8];
    int n = gfx_format_uint(buf, (unsigned long)prog);
    buf[n++] = '%';
    buf[n] = 0;
    if (O.show_bar) gfx_text(FONT_SMALL, bx + bw + 6, by + 8, buf, 0xffff, 0xffff, 256);
    else gfx_text_center(FONT_SMALL, GFX_W / 2, by + 8, buf, 0xffff, 0xffff, 256);
  }
  /* practice checkpoints: green diamonds */
  for (unsigned k = 0; k < O.checkpoint_count; k++) {
    const Checkpoint *c = &O.checkpoints[k];
    float sx = wx_to_sx(G, c->p.x), sy = wy_to_sy(G, c->p.y);
    if (sx < -10 || sx > GFX_W + 10 || sy < gfx_y0 - 12 || sy > gfx_y1 + 12) continue;
    float d0[8] = {sx, sy - 10, sx + 7, sy, sx, sy + 10, sx - 7, sy};
    float d1[8] = {sx, sy - 7.5f, sx + 5, sy, sx, sy + 7.5f, sx - 5, sy};
    float d2[6] = {sx, sy - 7.5f, sx + 5, sy, sx - 5, sy};
    gfx_poly(d0, 4, 0, 256, BLEND_NORMAL);
    gfx_poly(d1, 4, rgb(0, 190, 40), 256, BLEND_NORMAL);
    gfx_poly(d2, 3, rgb(120, 255, 120), 256, BLEND_NORMAL);
  }
}

/* Editor grid: 30 unit cells above the ground. */
static void draw_grid(void) {
  float x0 = fmodf(-G->cam_x, 30);
  if (x0 > 0) x0 -= 30;
  for (float wx = x0; wx < VIEW_W + 30; wx += 30) {
    int sx = (int)floorf(wx * PX + .5f);
    gfx_blend(sx, 0, 1, ground_top, 0, 70);
  }
  for (int row = 0; row < 90; row++) {
    int sy = (int)floorf(wy_to_sy(G, row * 30.0f) + .5f);
    if (sy < gfx_y0 - 1) break;
    if (sy <= gfx_y1) gfx_blend(0, sy, GFX_W, 1, 0, 70);
  }
}

void scene_draw(void) {
  if (!G) return;
  scene_background(bg_col, G->bg_x, G->cam_y);
  if (O.editor) draw_grid();
  /* the strip's number, when it is one of the frame's strips */
  int strip = gfx_y1 - gfx_y0 == STRIP_H && gfx_y0 % STRIP_H == 0 ? gfx_y0 / STRIP_H : -1;
  for (int k = 0; k < nitems; k++) {
    const Item *it = &items[order[k]];
    if (strip >= 0 && (strip < (it->strips & 15) || strip > it->strips >> 4)) continue;
    int kind = it->kind & 0x7f;
    if (kind == K_PLAYER) { draw_player(); continue; }
    if (kind == K_PROG) { draw_prog(it); continue; }
    const Sprite *s = sprite_def(it->spr);
    int ext = (s->w > s->h ? s->w : s->h) * (it->scale > 256 ? it->scale : 256) / 256 + 2;
    if (kind == K_QUAD) ext *= 2;
    int y = it->y >> 2;
    if (y + ext < gfx_y0 || y - ext > gfx_y1) continue;
    color_t c;
    int mode;
    unsigned pa;
    pal_for(it->ct, (it->kind & K_ADD) != 0, &c, &mode, &pa);
    if (kind == K_SPRITE) {
      gfx_sprite(it->spr, (it->x + 2) >> 2, (it->y + 2) >> 2, it->xf, c, it->alpha + 1, mode);
      continue;
    }
    int a16 = (it->xf & 3) * 90 * 16 + it->angle, fl = it->xf & (XF_FLIPX | XF_FLIPY);
    if (kind == K_SCALED) {
      /* 90 degree transforms are expressed as rotation + flips for the bilinear path */
      gfx_sprite_ex(it->spr, it->x * 4, it->y * 4, a16, it->scale, fl, c, it->alpha + 1, mode);
    } else {
      /* the sprite is the top right quarter; the others are its mirror images */
      for (int q = 0; q < 4; q++) {
        int qf = (q == 1 || q == 2 ? XF_FLIPX : 0) | (q >= 2 ? XF_FLIPY : 0);
        gfx_sprite_ex(it->spr, it->x * 4, it->y * 4, a16, it->scale, qf | XF_ANCHOR, c, it->alpha + 1, mode);
      }
    }
  }
  draw_end_wall();
  draw_attempt();
  if (ground_top < GFX_H) scene_ground(g1_col, line_col, G->ground_x, ground_top, false, true);
  if (draw_gfx_grounds) {
    if (floor_bottom < ground_top) scene_ground(g1_col, line_col, G->ground_x, floor_bottom, false, true);
    scene_ground(g1_col, line_col, G->ground_x, ceil_bottom, true, true);
  }
  fx_draw();
  draw_hud();
  if (O.fade > 0) gfx_scale_rect(0, 0, GFX_W, GFX_H, (unsigned)((1 - O.fade) * 256));
}
