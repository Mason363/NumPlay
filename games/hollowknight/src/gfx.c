/* Drawing a room: its sprites front to back, a strip of rows at a time. Each pixel keeps the light gathered so far
 * and how much still gets through from behind, so what is hidden is never fetched nor drawn. What is behind the
 * blur plane is drawn small beforehand (as the game's background camera does) and shows through at the end.
 * Then the strip is dithered to the screen's 16 bits. */
#include <math.h>
#include <stdint.h>
#include "hk.h"
#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif

#define STRIP_H 4
#ifndef MAX_ITEMS
#define MAX_ITEMS 512
#endif
#ifndef NPAL
#define NPAL 104
#endif
#define NPX (STRIP_H * VIEW_W)
bool g_gfx_fast;
/* the strip's rows: each one of the screen's, or (fast) each two of them, where no HUD, text or menu is: the room's
 * drawn at half the height, its pixels' rows sampled between the two */
static int vsf = 1;
#define SY(y) ((y) * (float)vsf + vsf * 0.5f)   /* (a row's middle, in the screen's rows) */

float g_cam_x, g_cam_y;
uint32_t g_gfx_items, g_gfx_pixels, g_gfx_dropped;
#ifdef HOST
uint32_t g_peak_pals, g_peak_arena, g_peak_soft;
#endif

/* a strip's pixels as they take light, front to back: red and blue (16 bits each), then green (16 bits) and the light
 * left from behind (8 bits, 255: all of it) together; then, finished, the 16-bit pixels in place */
typedef struct { uint32_t rb, gt; } Acc;
static Acc acc[NPX] __attribute__((aligned(8)));
/* a pixel cleared: no light yet but its dither (the screen's 4 x 4 pattern: a strip's rows are its rows), all
 * light left */
#define CL(d) {((d) >> 1) * 0x800080u, 255u << 16 | ((d) >> 2) << 7}
static const Acc acc_clear[STRIP_H][4] = {
    {CL(0), CL(8), CL(2), CL(10)}, {CL(12), CL(4), CL(14), CL(6)}, {CL(3), CL(11), CL(1), CL(9)}, {CL(15), CL(7), CL(13), CL(5)}};
#undef CL
#define T_OF(gt) ((gt) >> 16)
static uint32_t cov[STRIP_H][VIEW_W / 32];   /* pixels with no light left from behind */
static bool strip_ready;   /* the strip buffers are clear (finish_strip leaves them so) */

/* an instance in front of the blur plane, seen this frame */
typedef struct {
  Inst in;
  uint16_t x0, x1;   /* on screen, clipped */
  uint8_t y0, y1;
  uint8_t pal;
  int8_t cty;        /* the tile row whose slots the arena holds */
  uint8_t clip;      /* (HUD: 1 + its clip circle) */
  uint8_t screen;    /* a light that screens, drawn as the others with its colors so (Pal) */
  uint16_t ar;       /* its tiles (a row of them, or all if turned), in the arena */
} Item;

/* the HUD's clip circles */
typedef struct {
  float cx, cy, r;   /* screen pixels */
  float x0, y0, x1, y1;
  bool box;          /* (else a circle) */
} HudClip;
#define MAX_HUD_CLIPS 4
static HudClip hud_clips[MAX_HUD_CLIPS];
#ifndef ARENA
#define ARENA 2048
#endif
#define UNRES_SLOT 0xFFFE
#define EMPTY_SLOT 0xFFFF
static uint16_t arena[ARENA];
static int arena_top;
static Item items[MAX_ITEMS];
static int nitems;

/* ---------------------------------------------------------------- palettes: a texture's colors for a tint, graded */
typedef struct {
  uint32_t key;
  uint16_t frame;
  uint8_t solid;    /* the tint is opaque: a tile of opaque texels hides what is behind */
  uint8_t screen;   /* for lights that screen: each color's alpha is its brightest part (screening is blending so) */
  uint32_t c[16];   /* premultiplied 0xAARRGGBB; [0] transparent */
} Pal;
static Pal pals[NPAL];
static uint16_t pal_frame = 1;
static bool pal_over;   /* more than NPAL this frame: some items' palettes were taken (draw_item makes them again) */
static int pal_hand;

static void soft_reset(void);
static uint8_t lut3[3][256];   /* the room's curves */
static float room_sat;
static int lut_room = -1;

static void make_luts(void) {
  RoomHdr *h = g_room.h;
  for (int c = 0; c < 3; c++)
    for (int i = 0; i < 256; i++) {
      float x = i * 63.0f / 255.0f;
      int a = (int)x;
      if (a > 62) a = 62;
      float t = x - a;
      lut3[c][i] = (uint8_t)(h->lut[c][a] + (h->lut[c][a + 1] - h->lut[c][a]) * t + 0.5f);
    }
  room_sat = h->saturation;
  lut_room = g_room.id;
  soft_reset();
  for (int i = 0; i < NPAL; i++) pals[i].key = 0xFFFFFFFF;
}

static bool grade_off;   /* (the HUD's tints: the room's grading not for them) */
#define HUD_TINT 240      /* (dynamic tints from slot 16 on are the HUD's: 16 the HUD's white, 17 its soul, 18-20 the
                             dialogue's, 21 the area title's, 22-26 the item message's, 27 and 28 the blankers, 29-31
                             the menus'; below, the room's: 0 the Knight's, 1 his light, 2-6 and 10, 11, 15 the
                             enemies', 7 the prompts', 8, 9, 12-14 the scripts' objects') */

static uint32_t grade(float r, float g, float b, float a) {
  /* r g b a: 0..1 straight */
  int ri = (int)(r * 255 + 0.5f), gi = (int)(g * 255 + 0.5f), bi = (int)(b * 255 + 0.5f);
  if (ri > 255) ri = 255;
  if (gi > 255) gi = 255;
  if (bi > 255) bi = 255;
  float R = ri, G = gi, B = bi;
  if (!grade_off) {
    R = lut3[0][ri], G = lut3[1][gi], B = lut3[2][bi];
    float L = 0.2126f * R + 0.7152f * G + 0.0722f * B;
    R = L + (R - L) * room_sat, G = L + (G - L) * room_sat, B = L + (B - L) * room_sat;
  }
  R = R < 0 ? 0 : R > 255 ? 255 : R;
  G = G < 0 ? 0 : G > 255 ? 255 : G;
  B = B < 0 ? 0 : B > 255 ? 255 : B;
  int A = (int)(a * 255 + 0.5f);
  if (A > 255) A = 255;
  return (uint32_t)A << 24 | (uint32_t)(R * a + 0.5f) << 16 | (uint32_t)(G * a + 0.5f) << 8 | (uint32_t)(B * a + 0.5f);
}

uint8_t g_group_alpha[MAX_GROUPS];
uint8_t g_screen_fade;   /* how black the screen is (the camera's fade) */

/* tints: the room's, then colors the game sets (gfx_dyn_tint): a color, and a flash color and amount (SpriteFlash) */
#define DYN_TINT 224
static uint8_t dyn_tints[256 - DYN_TINT][8];
static inline const uint8_t *tint_rgba(int tint) { return tint >= DYN_TINT ? dyn_tints[tint - DYN_TINT] : g_room.tints + 4 * tint; }
static void pals_forget_tint(int tint);

uint8_t gfx_dyn_flash(int slot, uint8_t r, uint8_t g, uint8_t b, uint8_t a, uint8_t fr, uint8_t fg, uint8_t fb, uint8_t amount) {
  uint8_t *t = dyn_tints[slot & 31];
  uint8_t v[8] = {r, g, b, a, fr, fg, fb, amount};
  if (memcmp(t, v, 8)) {
    /* (the colors made with its old values are stale) */
    memcpy(t, v, 8);
    pals_forget_tint(DYN_TINT + (slot & 31));
  }
  return (uint8_t)(DYN_TINT + (slot & 31));
}

uint8_t gfx_dyn_tint(int slot, uint8_t r, uint8_t g, uint8_t b, uint8_t a) { return gfx_dyn_flash(slot, r, g, b, a, 0, 0, 0, 0); }

/* the instance's alpha: its tint's, times its render group's */
static inline uint32_t inst_alpha(const Inst *in) {
  uint32_t a = tint_rgba(in->tint)[3];
  return in->group ? (a * g_group_alpha[in->group & (MAX_GROUPS - 1)] + 127) / 255 : a;
}

/* a palette as tools/pack.py codes it (pal_code): per color, Rice codes of its steps from the one before (green, then
 * red and blue less half the green: SEC_PAL's first bytes say how many low bits each has), then a bit: opaque, else 8
 * bits of alpha */
typedef struct {
  const uint8_t *p;
  uint32_t buf;
  int n;
} Bits;
static uint32_t bits_get(Bits *b, int k) {
  while (b->n < k) b->buf |= (uint32_t)*b->p++ << b->n, b->n += 8;
  uint32_t v = b->buf & ((1u << k) - 1);
  b->buf >>= k, b->n -= k;
  return v;
}
static int bits_step(Bits *b, int k) {
  uint32_t q = 0;
  while (bits_get(b, 1)) q++;
  uint32_t u = q << k | bits_get(b, k);
  return (int)(u >> 1) ^ -(int)(u & 1);
}

/* colors for a light that screens: what is behind keeps 1 - the brightest part of the color */
static void pal_screen(uint32_t *c, int n) {
  for (int i = 0; i < n; i++) {
    uint32_t r = c[i] >> 16 & 255, g = c[i] >> 8 & 255, b = c[i] & 255, m = r > g ? r : g;
    m = m > b ? m : b;
    c[i] = (c[i] & 0xFFFFFF) | m << 24;
  }
}

/* a palette's key (0: none, soft textures' colors are soft_get's) */
static uint32_t pal_key(uint16_t tex, uint8_t tint, uint8_t flags, uint32_t alpha) {
  /* (tex << 17: solid colors' TEX_NONE comes out as 0xFFFE....; black textures' keys below those) */
  uint32_t key = (uint32_t)tex << 17 | alpha << 9 | (uint32_t)tint << 1 | ((flags & F_LIT) ? 1 : 0);
  if (tex != TEX_NONE) {
    const TexRec *r = tex_rec(tex);
    if (r->fmt >= FMT_SOFT) return 0;
    if (r->fmt == FMT_ALPHA2) key = 0xFFFC0000u | alpha;   /* black: only the alpha matters */
  }
  return key;
}
static void pal_fill(Pal *p, uint16_t tex, uint8_t tint, uint8_t flags, uint32_t alpha, bool screen);

static int pal_get(uint16_t tex, uint8_t tint, uint8_t flags, uint32_t alpha, bool screen) {
  uint32_t key = pal_key(tex, tint, flags, alpha);
  if (!key) return 0;   /* (no colors) */
  for (int i = 0; i < NPAL; i++)
    if (pals[i].key == key && pals[i].screen == screen) {
      pals[i].frame = pal_frame;
      return i;
    }
  int s = -1;
  for (int n = 0; n < NPAL; n++) {
    int i = (pal_hand + n) % NPAL;
    if (pals[i].frame != pal_frame) {
      s = i;
      break;
    }
  }
  if (s < 0) s = pal_hand, pal_over = true;
  pal_hand = (s + 1) % NPAL;
  Pal *p = &pals[s];
  p->key = key, p->frame = pal_frame;
  pal_fill(p, tex, tint, flags, alpha, screen);
  return s;
}
static void pal_fill(Pal *p, uint16_t tex, uint8_t tint, uint8_t flags, uint32_t alpha, bool screen) {
  p->screen = screen;
  const uint8_t *t = tint_rgba(tint);
  grade_off = tint >= HUD_TINT;
  p->solid = alpha == 255 && !screen;
  float tr = t[0] / 255.0f, tg = t[1] / 255.0f, tb = t[2] / 255.0f, ta = alpha / 255.0f;
  if (flags & F_LIT) {
    const float *am = g_room.h->ambient;
    tr *= am[0], tg *= am[1], tb *= am[2];
  }
  p->c[0] = 0;
  if (tex == TEX_NONE) {
    p->c[1] = grade(tr, tg, tb, ta);
    if (screen) pal_screen(p->c + 1, 1);
    return;
  }
  const TexRec *r = tex_rec(tex);
  if (r->fmt >= FMT_SOFT) return;
  if (r->fmt == FMT_ALPHA2) {   /* (drawn black whatever the tint) */
    for (int i = 1; i < 4; i++) p->c[i] = (uint32_t)(ta * i / 3.0f * 255 + 0.5f) << 24;
  } else {
    /* (a flash: towards its color by its amount, the alpha kept) */
    float fk = tint >= DYN_TINT ? t[7] / 255.0f : 0, fr = fk ? t[4] / 255.0f * fk : 0, fg = fk ? t[5] / 255.0f * fk : 0,
          fb = fk ? t[6] / 255.0f * fk : 0;
    const uint8_t *ks = section(SEC_PAL);
    Bits bits = {ks + 2 * r->pal, 0, 0};
    int g = 0, ro = 0, bo = 0;
    for (int i = 0; i < 15; i++) {
      /* (red 5 bits, green 6, blue 5) */
      g += bits_step(&bits, ks[0]), ro += bits_step(&bits, ks[1]), bo += bits_step(&bits, ks[2]);
      int a = bits_get(&bits, 1) ? 255 : (int)bits_get(&bits, 8);
      p->c[i + 1] = grade((float)(ro + (g >> 1)) / 31 * tr * (1 - fk) + fr, (float)g / 63 * tg * (1 - fk) + fg,
                          (float)(bo + (g >> 1)) / 31 * tb * (1 - fk) + fb, a / 255.0f * ta);
    }
    if (screen) pal_screen(p->c + 1, 15);
  }
}


/* ---------------------------------------------------------------- smooth textures: a few texels, graded for a tint (those of
 * one color are alpha only, read in place) */
#ifndef SOFT_POOL
#define SOFT_POOL 768
#endif
#ifndef NSOFT
#define NSOFT 16
#endif
static uint32_t soft_pool[SOFT_POOL];
typedef struct {
  uint32_t key;
  uint16_t frame, off, n;
} SoftEnt;
static SoftEnt soft_ent[NSOFT];
static int nsoft, soft_top;

static void pals_forget_tint(int tint) {
  for (int i = 0; i < NPAL; i++)
    if ((pals[i].key >> 1 & 255) == (uint32_t)tint && pals[i].key < 0xFFFC0000u) pals[i].key = 0xFFFFFFFFu;
  for (int i = 0; i < nsoft; i++)
    if ((soft_ent[i].key >> 1 & 255) == (uint32_t)tint) soft_ent[i].key = 0xFFFFFFFFu;
}

static void soft_reset(void) { nsoft = 0, soft_top = 0; }

/* (FMT_SOFTA: its data is its color, then its alpha texels) */
static uint32_t softa_color(const TexRec *r) { return rd32(section(SEC_SOFT) + r->off); }

uint32_t g_soft_misses;
static const uint32_t *soft_get(uint16_t tex, uint8_t tint, uint8_t flags, uint32_t alpha) {
  const TexRec *rr = tex_rec(tex);
  if (rr->fmt == FMT_SOFTA) return (const uint32_t *)(const void *)(section(SEC_SOFT) + rr->off + 4);   /* (alpha, in place) */
  uint32_t key = (uint32_t)tex << 17 | alpha << 9 | (uint32_t)tint << 1 | ((flags & F_LIT) ? 1 : 0);
  for (int i = 0; i < nsoft; i++)
    if (soft_ent[i].key == key) {
      soft_ent[i].frame = pal_frame;
      return soft_pool + soft_ent[i].off;
    }
  const TexRec *r = rr;
  int n = r->w * r->h;   /* (in words) */
  g_soft_misses++;
  if (n > SOFT_POOL) return NULL;
  if (soft_top + n > SOFT_POOL || nsoft == NSOFT) {
    /* keep this frame's, packed */
    int k = 0, top = 0;
    for (int i = 0; i < nsoft; i++) {
      if (soft_ent[i].frame != pal_frame) continue;
      memmove(soft_pool + top, soft_pool + soft_ent[i].off, soft_ent[i].n * 4u);
      soft_ent[k] = soft_ent[i];
      soft_ent[k].off = (uint16_t)top;
      top += soft_ent[k].n, k++;
    }
    nsoft = k, soft_top = top;
    if (soft_top + n > SOFT_POOL || nsoft == NSOFT) return NULL;
  }
  uint32_t *px = soft_pool + soft_top;
  const uint8_t *src = section(SEC_SOFT) + r->off;
  lz_decode(src + 4, rd32(src), (uint8_t *)px, (uint32_t)n * 4);
  const uint8_t *t = tint_rgba(tint);
  float tr = t[0] / 255.0f, tg = t[1] / 255.0f, tb = t[2] / 255.0f, ta = alpha / 255.0f;
  if (flags & F_LIT) {
    const float *am = g_room.h->ambient;
    tr *= am[0], tg *= am[1], tb *= am[2];
  }
  grade_off = tint >= HUD_TINT;
  for (int i = 0; i < n; i++) {
    const uint8_t *c = (const uint8_t *)&px[i];   /* premultiplied r g b a bytes */
    int a = c[3];
    if (!a) {
      px[i] = 0;
      continue;
    }
    float ia = 1.0f / a;
    px[i] = grade(c[0] * ia * tr, c[1] * ia * tg, c[2] * ia * tb, a / 255.0f * ta);
  }
  soft_ent[nsoft++] = (SoftEnt){key, pal_frame, (uint16_t)soft_top, (uint16_t)n};
  soft_top += n;
  return px;
}

/* ---------------------------------------------------------------- an item's mapping from screen to texels */
typedef struct {
  float ox, oy;          /* screen position of texel (0, 0) */
  float iux, iuy, ivx, ivy;   /* u = iux*(x-ox) + iuy*(y-oy), v = ivx*(x-ox) + ivy*(y-oy) */
  float pu, pv;          /* (axis aligned) screen pixels a texel along u and v, signed */
  bool rot;
} Map;

static float inst_k(const Inst *in) { return FOCAL / (in->z * (1.0f / 128) - CAM_Z); }

/* the cosine and sine of a turn in 1/65536 (as cosf and sinf, to a float's precision): its octant, then the series on
 * [0, pi / 4] */
static void cos_sin_turn(int rot, float *c, float *s) {
  uint32_t a = (uint16_t)rot, oct = a >> 13, r = a & 0x1FFF;
  float x = (float)((oct & 1) ? 0x2000 - r : r) * (6.2831853f / 65536.0f), x2 = x * x;
  float sn = x * (1 + x2 * (-1.0f / 6 + x2 * (1.0f / 120 + x2 * (-1.0f / 5040 + x2 * (1.0f / 362880)))));
  float cs = 1 + x2 * (-0.5f + x2 * (1.0f / 24 + x2 * (-1.0f / 720 + x2 * (1.0f / 40320 + x2 * (-1.0f / 3628800)))));
  switch (oct) {
    case 0: *c = cs, *s = sn; break;
    case 1: *c = sn, *s = cs; break;
    case 2: *c = -sn, *s = cs; break;
    case 3: *c = -cs, *s = sn; break;
    case 4: *c = -cs, *s = -sn; break;
    case 5: *c = -sn, *s = -cs; break;
    case 6: *c = sn, *s = -cs; break;
    default: *c = cs, *s = -sn; break;
  }
}

/* the instance's u and v steps on screen (pixels a texel) */
static void inst_axes(const Inst *in, float k, float *ux, float *uy, float *vx, float *vy) {
  float a = f16(in->a) * k, b = f16(in->b) * k;
  if (in->flags & F_ROT) {
    float c, s;
    cos_sin_turn(in->rot, &c, &s);
    /* world u = a (c, s), v = b (-s, c); screen y is down */
    *ux = a * c, *uy = -a * s, *vx = -b * s, *vy = -b * c;
  } else
    *ux = a, *uy = 0, *vx = 0, *vy = -b;
}

static void inst_map(const Inst *in, Map *m) {
  float k = inst_k(in);
  m->ox = VIEW_W / 2 + (in->ax * (1.0f / 64) - g_cam_x) * k;
  m->oy = VIEW_H / 2 - (in->ay * (1.0f / 64) - g_cam_y) * k;
  float ax, ay, bx, by;
  inst_axes(in, k, &ax, &ay, &bx, &by);
  if (in->flags & F_ROT) {
    /* screen = o + u (ax, ay) + v (bx, by): the inverse */
    float det = ax * by - bx * ay;
    if (det == 0) det = 1e-6f;
    m->iux = by / det, m->iuy = -bx / det, m->ivx = -ay / det, m->ivy = ax / det;
    m->pu = ax, m->pv = by;
    m->rot = true;
  } else {
    m->pu = ax, m->pv = by;
    m->iux = 1 / ax, m->iuy = 0, m->ivx = 0, m->ivy = 1 / by;
    m->rot = false;
  }
}

/* an instance's box on screen (pixels, not clipped): false if it is not in front of the camera */
static bool inst_screen_box(const Inst *in, float *x0, float *y0, float *x1, float *y1) {
  float k = inst_k(in);
  if (!(k > 0) || k > 400) return false;
  float bx0, by0, bx1, by1;
  if (in->flags & F_SOLID) {
    float ox = VIEW_W / 2 + (in->ax * (1.0f / 64) - g_cam_x) * k, oy = VIEW_H / 2 - (in->ay * (1.0f / 64) - g_cam_y) * k;
    bx0 = ox, by0 = oy, bx1 = ox + f16(in->a) * k, by1 = oy - f16(in->b) * k;
  } else {
    const TexRec *r = tex_rec(in->tex);
    Map m;
    inst_map(in, &m);
    if (in->flags & F_ROT) {
      float ux, uy, vx, vy;
      inst_axes(in, k, &ux, &uy, &vx, &vy);
      ux *= r->w, uy *= r->w, vx *= r->h, vy *= r->h;
      float xs[4] = {m.ox, m.ox + ux, m.ox + vx, m.ox + ux + vx}, ys[4] = {m.oy, m.oy + uy, m.oy + vy, m.oy + uy + vy};
      bx0 = bx1 = xs[0], by0 = by1 = ys[0];
      for (int i = 1; i < 4; i++) {
        if (xs[i] < bx0) bx0 = xs[i];
        if (xs[i] > bx1) bx1 = xs[i];
        if (ys[i] < by0) by0 = ys[i];
        if (ys[i] > by1) by1 = ys[i];
      }
    } else {
      float ex = m.ox + m.pu * r->w, ey = m.oy + m.pv * r->h;
      bx0 = m.ox < ex ? m.ox : ex, bx1 = m.ox < ex ? ex : m.ox;
      by0 = m.oy < ey ? m.oy : ey, by1 = m.oy < ey ? ey : m.oy;
    }
  }
  *x0 = bx0, *y0 = by0, *x1 = bx1, *y1 = by1;
  return true;
}
__attribute__((noinline)) static bool item_box(const Inst *in, Item *it) {
  float bx0, by0, bx1, by1;
  if (!inst_screen_box(in, &bx0, &by0, &bx1, &by1)) return false;
  if (bx1 <= 0 || by1 <= 0 || bx0 >= VIEW_W || by0 >= VIEW_H) return false;
  int a = (int)(bx0 + 0.5f), b = (int)(by0 + 0.5f), c = (int)(bx1 + 0.5f), d = (int)(by1 + 0.5f);
  if (a < 0) a = 0;
  if (b < 0) b = 0;
  if (c > VIEW_W) c = VIEW_W;
  if (d > VIEW_H) d = VIEW_H;
  if (a >= c || b >= d) return false;
  it->x0 = (uint16_t)a, it->y0 = (uint8_t)b, it->x1 = (uint16_t)c, it->y1 = (uint8_t)d;
  return true;
}

/* ---------------------------------------------------------------- back to front blending (the small background) */
static inline uint32_t blend_over(uint32_t d, uint32_t s) {
  uint32_t ia = 256 - (s >> 24);
  uint32_t rb = ((d & 0xFF00FF) * ia >> 8) & 0xFF00FF, g = ((d & 0xFF00) * ia >> 8) & 0xFF00;
  return (s & 0xFFFFFF) + rb + g;
}
static inline uint32_t blend_mode(uint32_t d, uint32_t s, int mode) {
  int dr = d >> 16 & 255, dg = d >> 8 & 255, db = d & 255;
  int sr = s >> 16 & 255, sg = s >> 8 & 255, sb = s & 255, sa = s >> 24;
  int r, g, b;
  switch (mode) {
    case BL_ADD: r = dr + sr, g = dg + sg, b = db + sb; break;
    case BL_SCREEN: r = dr + sr - ((dr * sr * 257 + 257) >> 16), g = dg + sg - ((dg * sg * 257 + 257) >> 16), b = db + sb - ((db * sb * 257 + 257) >> 16); break;
    case BL_LINEARLIGHT: r = dr + 2 * sr - sa, g = dg + 2 * sg - sa, b = db + 2 * sb - sa; break;
    case BL_MULTIPLY: r = dr * (256 - sa + sr) >> 8, g = dg * (256 - sa + sg) >> 8, b = db * (256 - sa + sb) >> 8; break;
    default: return blend_over(d, s);
  }
  r = r < 0 ? 0 : r > 255 ? 255 : r;
  g = g < 0 ? 0 : g > 255 ? 255 : g;
  b = b < 0 ? 0 : b > 255 ? 255 : b;
  return (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

/* ---------------------------------------------------------------- front to back: a pixel takes a source's light */
static inline void cover(int row, int x) { cov[row][x >> 5] |= 1u << (x & 31); }
/* [a, b) */
static void cover_run(int row, int a, int b) {
  uint32_t *cv = cov[row];
  while (a < b) {
    int w = a >> 5, n = 32 - (a & 31);
    if (n > b - a) n = b - a;
    cv[w] |= (n == 32 ? 0xFFFFFFFFu : ((1u << n) - 1)) << (a & 31);
    a += n;
  }
}
static inline int code_of(const uint8_t *codes, int tx) { return codes[tx >> 2] >> ((tx & 3) * 2) & 3; }

/* s: premultiplied 0xAARRGGBB; p: the pixel's index in the strip */
#ifdef HOST
uint32_t g_kind_px[6];
#define COUNT_PX(k, n) (g_kind_px[k] += (uint32_t)(n))
#else
#define COUNT_PX(k, n) ((void)0)
#endif
/* (a strip's rows are whole words of cov: pixel p's bit is bit p of it all) */
#define COVER_P(p) (((uint32_t *)(void *)cov)[(p) >> 5] |= 1u << ((p) & 31))
static inline void take_over_a(Acc *a, uint32_t s) {
  uint32_t gt = a->gt, tt = (T_OF(gt) + 1) >> 1;
  a->rb += (s & 0xFF00FF) * tt;
  gt += ((s >> 8) & 255) * tt;   /* (green in the low half: never more than 16 bits) */
  if (s >= 0xFF000000u) {   /* (opaque: nothing left from behind) */
    a->gt = gt & 0xFFFF, COVER_P(a - acc);
    return;
  }
  uint32_t ia = ~s >> 24, t = (T_OF(gt) * (ia + (ia << 8))) >> 16;
  if (t < 2) {
    a->gt = gt & 0xFFFF, COVER_P(a - acc);
    return;
  }
  a->gt = (gt & 0xFFFF) | t << 16;
}
static inline void take_over(int row, int x, int p, uint32_t s) {
  (void)row, (void)x;
  take_over_a(acc + p, s);
}
/* a black texel (2-bit textures are drawn black): only the light left from behind changes */
static inline void take_black(Acc *a, uint32_t s) {
  uint32_t gt = a->gt;
  if (s >= 0xFF000000u) {
    a->gt = gt & 0xFFFF, COVER_P(a - acc);
    return;
  }
  uint32_t ia = ~s >> 24, t = (T_OF(gt) * (ia + (ia << 8))) >> 16;
  if (t < 2) {
    a->gt = gt & 0xFFFF, COVER_P(a - acc);
    return;
  }
  a->gt = (gt & 0xFFFF) | t << 16;
}

#define ACC_TOP (65535 - 255 * 128 - 1024)   /* lights that add stop here: what is behind, and the dither, still fit */
static void take_mode(int row, int x, int p, uint32_t s, int mode) {
  Acc *ac = acc + p;
  int32_t t = (int32_t)T_OF(ac->gt), tt = (t + 1) >> 1;
  int32_t sr = s >> 16 & 255, sg = s >> 8 & 255, sb = s & 255, sa = s >> 24;
  int32_t r = ac->rb >> 16, g = ac->gt & 0xFFFF, b = ac->rb & 0xFFFF;
  switch (mode) {
    case BL_ADD: r += sr * tt, g += sg * tt, b += sb * tt; break;
    case BL_SCREEN: {
      r += sr * tt, g += sg * tt, b += sb * tt;
      int32_t m = sr > sg ? sr : sg;
      m = m > sb ? m : sb;
      t = (t * (255 - m) * 257) >> 16;
      break;
    }
    case BL_LINEARLIGHT: {
      r += (2 * sr - sa) * tt, g += (2 * sg - sa) * tt, b += (2 * sb - sa) * tt;
      /* (it takes light away too: never below the dither the pixel started with) */
      const Acc *d = &acc_clear[p / VIEW_W][p & 3];
      int32_t dr = (int32_t)(d->rb >> 16), dg = (int32_t)(d->gt & 0xFFFF), db = (int32_t)(d->rb & 0xFFFF);
      r = r < dr ? dr : r, g = g < dg ? dg : g, b = b < db ? db : b;
      break;
    }
    case BL_MULTIPLY: t = (t * (255 - sa + (sr + sg + sb) / 3) * 257) >> 16; break;
    default:
      r += sr * tt, g += sg * tt, b += sb * tt;
      t = (t * (255 - sa) * 257) >> 16;
      break;
  }
  r = r < 0 ? 0 : r > ACC_TOP ? ACC_TOP : r;
  g = g < 0 ? 0 : g > ACC_TOP ? ACC_TOP : g;
  b = b < 0 ? 0 : b > ACC_TOP ? ACC_TOP : b;
  if (t < 2) t = 0, COVER_P(p);
  ac->rb = (uint32_t)r << 16 | (uint32_t)b;
  ac->gt = (uint32_t)g | (uint32_t)t << 16;
  (void)row, (void)x;
}

#define TAKE(row, x, p, s) (mode ? take_mode(row, x, p, s, mode) : take_over(row, x, p, s))

/* the first pixel in [x, b) with light left (b if none), and the first after it with none */
static inline int open_from(const uint32_t *cv, int x, int b) {
  while (x < b) {
    uint32_t w = ~cv[x >> 5] >> (x & 31);
    if (w) {
      x += __builtin_ctz(w);
      return x < b ? x : b;
    }
    x = (x | 31) + 1;
  }
  return b;
}
static inline int closed_from(const uint32_t *cv, int x, int b) {
  while (x < b) {
    uint32_t w = cv[x >> 5] >> (x & 31);
    if (w) {
      x += __builtin_ctz(w);
      return x < b ? x : b;
    }
    x = (x | 31) + 1;
  }
  return b;
}

/* the pixels i in [0, n) where 0 <= u0 + du i < U and 0 <= v0 + dv i < V (16.16): [*i0, *i1) */
static void clip_lin(int32_t u0, int32_t du, int32_t U, int n, int *i0, int *i1) {
  int a = *i0, b = *i1;
  if (du == 0) {
    if (u0 < 0 || u0 >= U) a = b;
  } else if (du > 0) {
    if (u0 < 0) {
      int k = (-u0 + du - 1) / du;
      if (k > a) a = k;
    }
    int k = u0 >= U ? 0 : (U - 1 - u0) / du + 1;   /* first i with u >= U */
    if (k < b) b = k;
  } else {
    if (u0 >= U) {
      int k = (u0 - U) / -du + 1;
      if (k > a) a = k;
    }
    int k = u0 < 0 ? 0 : u0 / -du + 1;   /* first i with u < 0 */
    if (k < b) b = k;
  }
  if (a > n) a = n;   /* (none in it: not past its end, where the steps from u0 would overflow) */
  *i0 = a, *i1 = b > a ? b : a;
}

static inline uint32_t rgb_of565(uint32_t c) { return (c >> 11) << 19 | (c >> 5 & 63) << 10 | (c & 31) << 3; }

static inline uint32_t lerp4(uint32_t a, uint32_t b, uint32_t w) {   /* w: 0..256 */
  uint32_t iw = 256 - w;
  uint32_t rb = ((a & 0xFF00FF) * iw + (b & 0xFF00FF) * w) >> 8 & 0xFF00FF;
  uint32_t ag = (((a >> 8) & 0xFF00FF) * iw + ((b >> 8) & 0xFF00FF) * w) & 0xFF00FF00;
  return rb | ag;
}

typedef struct {
  uint16_t tex, tw;
  const TexRec *r;
  uint16_t *slots;   /* tw * th, or NULL */
  const uint32_t *pal;
  int lk;             /* (the last tile found, without slots: its key, ty << 8 | tx; -1 none) */
  const uint8_t *lt;
  int sc, sr;         /* (the last four texels sampled: their top left texel, their colors, all one) */
  uint32_t st[4];
  bool same;
} BiTex;
#define BITEX(tex, tw, r, slots, pal) ((BiTex){tex, tw, r, slots, pal, -1, NULL, -(1 << 30), 0, {0, 0, 0, 0}, false})

static inline const uint8_t *bi_tile(BiTex *b, int tx, int ty) {
  if (b->slots && !g_tex_overload) {
    uint16_t *e = b->slots + ty * b->tw + tx;
    if (*e == UNRES_SLOT) {
      int q = tex_slot(b->tex, tx, ty);
      *e = q < 0 ? EMPTY_SLOT : (uint16_t)q;
    }
    return *e == EMPTY_SLOT ? NULL : tex_slot_px(*e);
  }
  /* (samples near each other: mostly the same tile; the last one found stays in the cache until another is asked) */
  int key = ty << 8 | tx;
  if (key != b->lk) b->lk = key, b->lt = tex_tile(b->tex, tx, ty);
  return b->lt;
}

static inline uint32_t bi_texel(BiTex *b, int u, int v) {
  if ((unsigned)u >= b->r->w || (unsigned)v >= b->r->h) return 0;
  const uint8_t *t = bi_tile(b, u >> 4, v >> 4);
  if (!t) return 0;
  int ur = u & 15, vr = v & 15;
  return b->pal[t[vr * 8 + (ur >> 1)] >> ((ur & 1) * 4) & 15];
}

/* the four texels around (u, v) (16.16), mixed (the same four as the last sample's: not read again) */
static inline uint32_t bi_sample(BiTex *b, int32_t u, int32_t v) {
  int c = u >> 16, rr = v >> 16;
  uint32_t fu = (uint32_t)(u >> 8) & 255, fv = (uint32_t)(v >> 8) & 255;
  if (c != b->sc || rr != b->sr) {
    b->sc = c, b->sr = rr;
    uint32_t *t = b->st;
    if ((c & 15) != 15 && (rr & 15) != 15 && (unsigned)c < (unsigned)(b->r->w - 1) && (unsigned)rr < (unsigned)(b->r->h - 1)) {
      const uint8_t *tl = bi_tile(b, c >> 4, rr >> 4);
      if (!tl) {
        t[0] = t[1] = t[2] = t[3] = 0, b->same = true;
        return 0;
      }
      int ur = c & 15, vr = rr & 15;
      const uint8_t *row = tl + vr * 8;
      int i0 = row[ur >> 1] >> ((ur & 1) * 4) & 15, i1 = row[(ur + 1) >> 1] >> (((ur + 1) & 1) * 4) & 15;
      row += 8;
      int i2 = row[ur >> 1] >> ((ur & 1) * 4) & 15, i3 = row[(ur + 1) >> 1] >> (((ur + 1) & 1) * 4) & 15;
      b->same = i0 == i1 && i0 == i2 && i0 == i3;   /* (the same four: as mixed; clear ones, 0) */
      t[0] = b->pal[i0], t[1] = b->pal[i1], t[2] = b->pal[i2], t[3] = b->pal[i3];
    } else {
      t[0] = bi_texel(b, c, rr), t[1] = bi_texel(b, c + 1, rr), t[2] = bi_texel(b, c, rr + 1), t[3] = bi_texel(b, c + 1, rr + 1);
      b->same = t[0] == t[1] && t[0] == t[2] && t[0] == t[3];
    }
  }
  const uint32_t *t = b->st;
  if (b->same) return t[0];
  return lerp4(lerp4(t[0], t[1], fu), lerp4(t[2], t[3], fu), fv);
}

/* ---------------------------------------------------------------- one item in a strip, on the pixels with light left */
typedef struct {
  int idx, mode, row, sy0;
  const Inst *in;
  const TexRec *r;
  const Map *m;
  const uint32_t *pal;
  Item *it;
  bool solid;   /* (its palette's) */
} Ctx;

/* a run [a, b) of row y (all pixels with light left), by kind */
static void run_solid(const Ctx *c, int y, int a, int b) {
  int mode = c->mode;
  uint32_t s = c->pal[1];
  for (int x = a; x < b; x++) TAKE(c->row, x, c->row * VIEW_W + x, s);
}

static void run_axis(const Ctx *c, int y, int a, int b, uint16_t *tp, int ty, int vr) {
  const Map *m = c->m;
  const TexRec *r = c->r;
  int mode = c->mode, row = c->row, W = r->w;
  bool a2 = r->fmt == FMT_ALPHA2, solid = c->solid;
  const uint8_t *codes = tex_row_codes(c->in->tex, ty);
  int sh = TILE_SHIFT(r->fmt), um = (1 << sh) - 1;
  int32_t du = (int32_t)(65536.0f / m->pu);
  int32_t u = (int32_t)((a + 0.5f - m->ox) * m->iux * 65536.0f);
  int x = a, p = row * VIEW_W + a;
  while (x < b) {
    int ui = u >> 16;
    if ((unsigned)ui >= (unsigned)W) {
      x++, p++, u += du;
      continue;
    }
    int tx = ui >> sh;
    int32_t edge = du > 0 ? ((tx + 1) << (16 + sh)) : ((tx << (16 + sh)) - 1);
    int n = (int)((edge - u) / du) + 1;
    if (n > b - x) n = b - x;
    if (n < 1) n = 1;
    uint16_t sl = tp[tx];
    if (sl == UNRES_SLOT) {
      int q = tex_slot(c->in->tex, tx, ty);
      sl = tp[tx] = q < 0 ? EMPTY_SLOT : (uint16_t)q;
    }
    if (sl == EMPTY_SLOT) {
      x += n, p += n, u += du * n;
      continue;
    }
    const uint8_t *t = tex_slot_px(sl);
    int e = x + n;
    if (solid && !mode && code_of(codes, tx) == 3) {
      /* a tile of opaque texels over everything behind it: nothing is left from behind */
      int x0 = x;
      if (a2) {   /* (black) */
        for (; x < e; x++, p++) acc[p].gt &= 0xFFFF;
        u += du * (e - x0);
      } else {
        const uint8_t *rw = t + vr * 8;
        for (; x < e; x++, p++, u += du) {
          int ur = (u >> 16) & 15;
          Acc *ac = acc + p;
          uint32_t s = c->pal[rw[ur >> 1] >> ((ur & 1) << 2) & 15], tt = (T_OF(ac->gt) + 1u) >> 1;
          ac->rb += (s & 0xFF00FF) * tt, ac->gt = (ac->gt + ((s >> 8) & 255) * tt) & 0xFFFF;
        }
      }
      cover_run(row, x0, e);
      continue;
    }
    uint32_t w2[2];
    memcpy(w2, t + vr * 8, 8);
    if (!(w2[0] | w2[1])) {   /* (this row of the tile is clear) */
      x = e, p += n, u += du * n;
      continue;
    }
    /* only the pixels on its texels from the first to the last that are not clear, then on to the next tile */
    int xn = e, pn = p + n;
    int32_t un = u + du * n;
    {
      int lb = w2[0] ? __builtin_ctz(w2[0]) : 32 + __builtin_ctz(w2[1]);
      int hb = w2[1] ? 63 - __builtin_clz(w2[1]) : 31 - __builtin_clz(w2[0]);
      int bs = a2 ? 1 : 2;
      int32_t L = ((tx << sh) + (lb >> bs)) << 16, H = ((tx << sh) + (hb >> bs) + 1) << 16;
      int k0, k1;
      if (du > 0) k0 = u >= L ? 0 : (L - 1 - u) / du + 1, k1 = u >= H ? 0 : (H - 1 - u) / du + 1;
      else k0 = u < H ? 0 : (u - H) / -du + 1, k1 = u < L ? 0 : (u - L) / -du + 1;
      if (k1 > n) k1 = n;
      if (k0 > k1) k0 = k1;
      e = x + k1, x += k0, p += k0, u += du * k0;
    }
    if (a2 && !mode) {
      const uint8_t *rw = t + vr * 8;
      if ((w2[0] & w2[1]) == 0xFFFFFFFFu && c->pal[3] >= 0xFF000000u) {
        /* (a row of opaque black texels: nothing left from behind) */
        cover_run(row, x, e);
        for (; x < e; x++, p++) acc[p].gt &= 0xFFFF;
        u = un, x = xn, p = pn;
        continue;
      }
      for (; x < e; x++, p++, u += du) {
        int ur = (u >> 16) & 31, ci = rw[ur >> 2] >> ((ur & 3) * 2) & 3;
        if (ci) take_black(acc + p, c->pal[ci]);
      }
    } else if (a2) {
      const uint8_t *rw = t + vr * 8;
      for (; x < e; x++, p++, u += du) {
        int ur = (u >> 16) & um, ci = rw[ur >> 2] >> ((ur & 3) * 2) & 3;
        if (ci) take_mode(row, x, p, c->pal[ci], mode);
      }
    } else if (!mode) {
      const uint8_t *rw = t + vr * 8;
      for (; x < e; x++, p++, u += du) {
        int ur = (u >> 16) & 15, ci = rw[ur >> 1] >> ((ur & 1) << 2) & 15;
        if (ci) take_over(row, x, p, c->pal[ci]);
      }
    } else {
      const uint8_t *rw = t + vr * 8;
      for (; x < e; x++, p++, u += du) {
        int ur = (u >> 16) & 15, ci = rw[ur >> 1] >> ((ur & 1) << 2) & 15;
        if (ci) take_mode(row, x, p, c->pal[ci], mode);
      }
    }
    x = xn, p = pn, u = un;
  }
}

static inline const uint8_t *turned_tile(uint16_t *rtp, uint16_t tex, int tw, int tx, int ty) {
  if (rtp && !g_tex_overload) {
    uint16_t *e = rtp + ty * tw + tx;
    if (*e == UNRES_SLOT) {
      int q = tex_slot(tex, tx, ty);
      *e = q < 0 ? EMPTY_SLOT : (uint16_t)q;
    }
    return *e == EMPTY_SLOT ? NULL : tex_slot_px(*e);
  }
  return tex_tile(tex, tx, ty);
}

/* steps from u (by du) until it leaves [lo, hi) (u in it), at least 1 */
static inline int steps_in(int32_t u, int32_t du, int32_t lo, int32_t hi) {
  return du > 0 ? (hi - 1 - u) / du + 1 : du < 0 ? (u - lo) / -du + 1 : 1 << 30;
}
/* the steps i in [k0, k1) where lo <= u + i du < hi */
__attribute__((always_inline)) static inline void clip_in(int32_t u, int32_t du, int32_t lo, int32_t hi, int *k0, int *k1) {
  if (du > 0) {
    if (u < lo) {
      int k = (lo - u - 1) / du + 1;
      if (k > *k0) *k0 = k;
    }
    int k = u >= hi ? 0 : (hi - 1 - u) / du + 1;
    if (k < *k1) *k1 = k;
  } else if (du < 0) {
    if (u >= hi) {
      int k = (u - hi) / -du + 1;
      if (k > *k0) *k0 = k;
    }
    int k = u < lo ? 0 : (u - lo) / -du + 1;
    if (k < *k1) *k1 = k;
  } else if (u < lo || u >= hi)
    *k1 = 0;
}
/* the pixels from (u, v) on in the same tile (at most left) */
__attribute__((always_inline)) static inline int turned_gap(int32_t u, int32_t v, int32_t du, int32_t dv, int sh, int left) {
  int32_t ul = (u >> (16 + sh)) << (16 + sh), vl = (v >> 20) << 20;
  int n = steps_in(u, du, ul, ul + (1 << (16 + sh))), m = steps_in(v, dv, vl, vl + (1 << 20));
  n = n < m ? n : m;
  return n < left ? n : left;
}
/* a rotated item's pixels [A, Ae) on one tile of 4-bit texels, drawn over: tu and tv step by dux and dvx */
typedef struct {
  int32_t tv, dux, dvx;
  const uint8_t *lt;
  const uint32_t *pal;
  Acc *acc;
  uint32_t *cov;
} T4;
#if defined(__arm__) && defined(__thumb2__) && !defined(GFX_NO_ASM)
/* (as the C below, which the computer's build runs: the compiler keeps too few of its values in registers) */
__attribute__((naked, noinline)) static void turned4_over(Acc *A, Acc *Ae, int32_t tu, const T4 *k) {
  __asm__ volatile(
      "push {r3-r11, lr}\n"          /* (k at [sp]) */
      "ldm r3, {r4-r8}\n"            /* tv, dux, dvx, lt, pal */
      "cmp r0, r1\n"
      "bhs 9f\n"
      "1:\n"
      "ubfx r3, r4, #16, #4\n"        /* the texel's row */
      "ubfx r11, r2, #17, #3\n"       /* its byte in it */
      "add r3, r11, r3, lsl #3\n"
      "ldrb r3, [r7, r3]\n"
      "ubfx r11, r2, #16, #1\n"       /* (which half) */
      "lsl r11, r11, #2\n"
      "lsr r3, r3, r11\n"
      "ands r3, r3, #15\n"
      "bne 3f\n"
      "2:\n"
      "add r2, r2, r5\n"
      "add r4, r4, r6\n"
      "adds r0, r0, #8\n"
      "cmp r0, r1\n"
      "blo 1b\n"
      "9:\n"
      "pop {r3-r11, pc}\n"
      "3:\n"
      "ldr r3, [r8, r3, lsl #2]\n"    /* s */
      "ldrd r9, r10, [r0]\n"          /* rb, gt */
      "add r11, r10, #65536\n"
      "lsr r11, r11, #17\n"           /* tt = (t + 1) >> 1 */
      "and r12, r3, #0xFF00FF\n"
      "mla r9, r12, r11, r9\n"
      "ubfx r12, r3, #8, #8\n"
      "mla r10, r12, r11, r10\n"      /* (green in the low half) */
      "str r9, [r0]\n"
      "cmp r3, #0xFF000000\n"
      "bhs 4f\n"
      "mvn r12, r3\n"
      "lsr r12, r12, #24\n"
      "add r12, r12, r12, lsl #8\n"   /* (255 - alpha) * 257 */
      "lsr r11, r10, #16\n"
      "mul r11, r11, r12\n"
      "lsrs r11, r11, #16\n"
      "cmp r11, #2\n"
      "blo 4f\n"
      "bfi r10, r11, #16, #16\n"
      "str r10, [r0, #4]\n"
      "b 2b\n"
      "4:\n"                          /* nothing left from behind: covered */
      "bfc r10, #16, #16\n"
      "str r10, [r0, #4]\n"
      "ldr r12, [sp]\n"
      "ldr r11, [r12, #20]\n"         /* acc */
      "ldr r12, [r12, #24]\n"         /* cov */
      "sub r11, r0, r11\n"
      "lsr r9, r11, #8\n"             /* its word */
      "ubfx r11, r11, #3, #5\n"       /* its bit */
      "movs r10, #1\n"
      "lsl r10, r10, r11\n"
      "ldr r11, [r12, r9, lsl #2]\n"
      "orr r11, r11, r10\n"
      "str r11, [r12, r9, lsl #2]\n"
      "b 2b\n");
}
#endif
#if !(defined(__arm__) && defined(__thumb2__) && !defined(GFX_NO_ASM)) || defined(GFX_ASM_CHECK)
#ifdef GFX_ASM_CHECK
#define turned4_over turned4_c
#endif
__attribute__((noinline)) static void turned4_over(Acc *A, Acc *Ae, int32_t tu, const T4 *k) {
  int32_t tv = k->tv;
  for (; A < Ae; A++, tu += k->dux, tv += k->dvx) {
    int ur = (tu >> 16) & 15, vr = (tv >> 16) & 15, ci = k->lt[vr << 3 | ur >> 1] >> ((ur & 1) * 4) & 15;
    if (ci) take_over_a(A, k->pal[ci]);
  }
}
#endif
#ifdef GFX_ASM_CHECK
#undef turned4_over
uint32_t g_asm_bad __attribute__((used)), g_asm_calls __attribute__((used));
/* (both, on copies: the C one's result kept, the differences counted) */
static void turned4_check(Acc *A, Acc *Ae, int32_t tu, const T4 *k) {
  /* (copies in g_scratch: free between tile decodes) */
  int n = (int)(Ae - A);
  if (n <= 0) return;
  Acc *a0 = (Acc *)(void *)g_scratch, *a1 = a0 + VIEW_W;
  uint32_t *c0 = (uint32_t *)(void *)(a1 + VIEW_W), *c1 = c0 + STRIP_H * VIEW_W / 32;
  memcpy(a0, A, n * sizeof *A), memcpy(c0, cov, sizeof cov);
  turned4_over(A, Ae, tu, k);
  memcpy(a1, A, n * sizeof *A), memcpy(c1, cov, sizeof cov);
  memcpy(A, a0, n * sizeof *A), memcpy(cov, c0, sizeof cov);
  turned4_c(A, Ae, tu, k);
  g_asm_calls++;
  if (memcmp(a1, A, n * sizeof *A) || memcmp(c1, cov, sizeof cov)) g_asm_bad++;
}
#define turned4_over turned4_check
#endif
typedef struct { uint16_t *rtp; uint16_t tex, tw; } TurnSrc;
/* the tile a key names (out of the loop below) */
__attribute__((noinline)) static const uint8_t *turned_key(const TurnSrc *s, int key) {
  return turned_tile(s->rtp, s->tex, s->tw, key & 255, key >> 8);
}
static void run_turned(const Ctx *c, int y, int a, int b, int *last_key, const uint8_t **last_t) {
  const Map *m = c->m;
  const TexRec *r = c->r;
  int mode = c->mode, row = c->row;
  int sh = TILE_SHIFT(r->fmt), um = (1 << sh) - 1, tw = r->tw;
  uint16_t tex = c->in->tex;
  const uint32_t *pal = c->pal;
  uint16_t *rtp = c->it->ar != 0xFFFF ? arena + c->it->ar : NULL;
  int32_t dux = (int32_t)(m->iux * 65536), dvx = (int32_t)(m->ivx * 65536);
  int32_t W16 = (int32_t)r->w << 16, H16 = (int32_t)r->h << 16;
  float fx = a + 0.5f - m->ox, fy = SY(y) - m->oy;
  int32_t u = (int32_t)((m->iux * fx + m->iuy * fy) * 65536), v = (int32_t)((m->ivx * fx + m->ivy * fy) * 65536);
  int i0 = 0, i1 = b - a;
  clip_lin(u, dux, W16, b - a, &i0, &i1);
  clip_lin(v, dvx, H16, b - a, &i0, &i1);
  u += dux * i0, v += dvx * i0;
  int lk = *last_key;
  const uint8_t *lt = *last_t;
  TurnSrc src = {rtp, tex, (uint16_t)tw};
  T4 k4 = {0, dux, dvx, NULL, pal, acc, &cov[0][0]};   /* (for turned4_over: the tile's row and texels set each time) */
  (void)sh, (void)um;
  /* (few values live in the loop: the pixel's index, its texel's place, the tile) */
#define TURNED(SH, TEXEL, PUT)                                                     \
  for (int q = row * VIEW_W + a + i0, qe = q + i1 - i0; q < qe;) {               \
    int tkey = (v >> 20) << 8 | (u >> (16 + SH));                              \
    if (tkey != lk) lk = tkey, lt = turned_key(&src, tkey);                    \
    int n = turned_gap(u, v, dux, dvx, SH, qe - q), p = q, pn = q + n;          \
    int32_t tu = u, tv = v;                                                    \
    q = pn, u += dux * n, v += dvx * n;                                        \
    if (!lt) continue;                                                         \
    uint32_t bx = tex_slot_box(lt);                                            \
    if (bx != 0xF0F0) {                                                        \
      /* (only the pixels on the tile's texels that are not clear, as a box: worked out unless the first and the \
       * last are in it) */                                                    \
      int32_t lu = tu & ((1 << (16 + SH)) - 1), lv = tv & ((1 << 20) - 1);     \
      int32_t ux0 = (int32_t)(bx & 15) << (12 + SH), ux1 = (int32_t)((bx >> 4 & 15) + 1) << (12 + SH); \
      int32_t vy0 = (int32_t)(bx >> 8 & 15) << 16, vy1 = (int32_t)((bx >> 12) + 1) << 16; \
      int32_t eu = lu + dux * (n - 1), ev = lv + dvx * (n - 1);                \
      if (lu < ux0 || lu >= ux1 || eu < ux0 || eu >= ux1 || lv < vy0 || lv >= vy1 || ev < vy0 || ev >= vy1) { \
        int k0 = 0, k1 = n;                                                    \
        clip_in(lu, dux, ux0, ux1, &k0, &k1);                                  \
        clip_in(lv, dvx, vy0, vy1, &k0, &k1);                                  \
        if (k0 > n) k0 = n;   /* (none in it: as clip_lin) */                  \
        if (k1 < k0) k1 = k0;                                                  \
        pn = p + k1, p += k0, tu += dux * k0, tv += dvx * k0;                  \
      }                                                                        \
    }                                                                          \
    if (SH == 4 && !mode) {                                                    \
      k4.tv = tv, k4.lt = lt;                                                  \
      turned4_over(acc + p, acc + pn, tu, &k4);                                \
      continue;                                                                \
    }                                                                          \
    for (Acc *A = acc + p, *Ae = acc + pn; A < Ae; A++, tu += dux, tv += dvx) { \
      int ur = (tu >> 16) & ((1 << SH) - 1), vr = (tv >> 16) & 15, ci = TEXEL; \
      if (ci) PUT;                                                             \
    }                                                                          \
  }
  if (r->fmt == FMT_ALPHA2) {
    if (!mode) TURNED(5, lt[vr << 3 | ur >> 2] >> ((ur & 3) * 2) & 3, take_black(A, pal[ci]))
    else TURNED(5, lt[vr << 3 | ur >> 2] >> ((ur & 3) * 2) & 3, take_mode(0, 0, (int)(A - acc), pal[ci], mode))
  } else {
    if (!mode) TURNED(4, lt[vr << 3 | ur >> 1] >> ((ur & 1) * 4) & 15, take_over_a(A, pal[ci]))
    else TURNED(4, lt[vr << 3 | ur >> 1] >> ((ur & 1) * 4) & 15, take_mode(0, 0, (int)(A - acc), pal[ci], mode))
  }
#undef TURNED
  *last_key = lk, *last_t = lt;
}

/* a smooth texture's texel at (u, v) (16.16, less half a texel), its edges repeated beyond them, as a color */
static uint32_t soft_edge_at(const TexRec *r, const uint32_t *tx, int32_t u, int32_t v, uint32_t crb, uint32_t cg, uint32_t ta) {
  int W = r->w, H = r->h, cx = u >> 16, rr = v >> 16;
  uint32_t fu = (uint32_t)(u >> 8) & 255, fv = (uint32_t)(v >> 8) & 255;
  int c0 = cx < 0 ? 0 : cx >= W ? W - 1 : cx, c1 = cx + 1 < 0 ? 0 : cx + 1 >= W ? W - 1 : cx + 1;
  int r0 = rr < 0 ? 0 : rr >= H ? H - 1 : rr, r1 = rr + 1 < 0 ? 0 : rr + 1 >= H ? H - 1 : rr + 1;
  if (r->fmt == FMT_SOFT)
    return lerp4(lerp4(tx[r0 * W + c0], tx[r0 * W + c1], fu), lerp4(tx[r1 * W + c0], tx[r1 * W + c1], fu), fv);
  const uint8_t *al = (const uint8_t *)tx;
  uint32_t top = al[r0 * W + c0] * (256 - fu) + al[r0 * W + c1] * fu, bot = al[r1 * W + c0] * (256 - fu) + al[r1 * W + c1] * fu;
  uint32_t aa = ((top * (256 - fv) + bot * fv) >> 16) * ta >> 8;
  return aa << 24 | ((crb * aa >> 8) & 0xFF00FF) | ((cg * aa >> 8) & 0xFF00);
}

/* a smooth texture's outer texels not all clear (SOFT_EDGED; most are: then nothing is drawn beyond the centers of
 * its outer texels), all its texels the same (SOFT_FLAT: the light that fills a room, a black square): a few textures
 * remembered */
#define SOFT_EDGED 1
#define SOFT_FLAT 2
static uint16_t soft_kind_tex[32];   /* (texture + 1) */
static uint8_t soft_kind_of[32];
static int soft_kind(uint16_t tex, const TexRec *r, const uint32_t *tx) {
  unsigned k = tex & 31;
  if (soft_kind_tex[k] == tex + 1u) return soft_kind_of[k];
  int W = r->w, H = r->h, n = W * H, kind = 0;
  const uint8_t *al = (const uint8_t *)tx;
  bool mono = r->fmt == FMT_SOFTA, flat = true;
#define CLEAR(i) (mono ? !al[i] : !(tx[i] >> 24))
  for (int x = 0; x < W && !kind; x++) kind = !CLEAR(x) || !CLEAR((H - 1) * W + x);
  for (int y = 0; y < H && !kind; y++) kind = !CLEAR(y * W) || !CLEAR(y * W + W - 1);
#undef CLEAR
  for (int i = 1; i < n && flat; i++) flat = mono ? al[i] == al[0] : tx[i] == tx[0];
  if (flat) kind |= SOFT_FLAT;
  soft_kind_tex[k] = (uint16_t)(tex + 1u), soft_kind_of[k] = (uint8_t)kind;
  return kind;
}

/* smooth textures: a few texels, interpolated; at the pixels whose centers are in the sprite (as the game's quad), its
 * edges repeated beyond the outer texels' centers (a big black square of 3 x 3 texels is a square, its corners sharp) */
static void run_soft(const Ctx *c, int y, int a, int b, const uint32_t *tx, uint32_t crb, uint32_t cg, uint32_t ta, int sk) {
  const Map *m = c->m;
  const TexRec *r = c->r;
  int mode = c->mode, row = c->row, W = r->w, H = r->h;
  bool mono = r->fmt == FMT_SOFTA;
  const uint8_t *al = (const uint8_t *)tx;
  float fx = a + 0.5f - m->ox, fy = SY(y) - m->oy;
  int32_t u0 = (int32_t)((m->iux * fx + m->iuy * fy - 0.5f) * 65536), v0 = (int32_t)((m->ivx * fx + m->ivy * fy - 0.5f) * 65536);
  int32_t dux = (int32_t)(m->iux * 65536), dvx = (int32_t)(m->ivx * 65536);
  int o0 = 0, o1 = b - a;
  bool edged = sk & SOFT_EDGED;
  if (edged) {
    clip_lin(u0 + 32768, dux, W << 16, b - a, &o0, &o1);
    clip_lin(v0 + 32768, dvx, H << 16, b - a, &o0, &o1);
    if (o0 >= o1) return;
    if (sk & SOFT_FLAT) {
      /* (one color throughout: no texel to mix) */
      uint32_t s = soft_edge_at(r, tx, 0, 0, crb, cg, ta);
      for (int x = a + o0, p = row * VIEW_W + x; x < a + o1; x++, p++) TAKE(row, x, p, s);
      return;
    }
  }
  /* where all four texels are inside: most of it, read without the edges' checks; the rest (within half a texel of
   * the edge, clear in most of these textures) with them */
  int i0 = o0, i1 = o1;
  clip_lin(u0, dux, (W - 1) << 16, b - a, &i0, &i1);
  clip_lin(v0, dvx, (H - 1) << 16, b - a, &i0, &i1);
  if (i0 >= i1) i0 = i1 = o1;
  for (int k = 0; k < 2 && edged; k++)
    for (int j = k ? i1 : o0, je = k ? o1 : i0; j < je; j++) {
      uint32_t s = soft_edge_at(r, tx, u0 + dux * j, v0 + dvx * j, crb, cg, ta);
      if (s >> 24) TAKE(row, a + j, row * VIEW_W + a + j, s);
    }
  if (i0 >= i1) return;
  u0 += dux * i0, v0 += dvx * i0;
  int x = a + i0, e = a + i1, p = row * VIEW_W + a + i0;
  if (mono && dvx == 0) {
    /* upright, one color: the row's two texel rows mixed once per texel column */
    int rr = v0 >> 16;
    uint32_t fv = (uint32_t)(v0 >> 8) & 255;
    const uint8_t *r0 = al + rr * W, *r1 = r0 + W;
    int lc = -1;
    uint32_t c0 = 0, c1 = 0;
    for (; x < e; x++, p++, u0 += dux) {
      int cx = u0 >> 16;
      if (cx != lc) {
        lc = cx;
        c0 = (r0[cx] * (256 - fv) + r1[cx] * fv) * ta >> 8, c1 = (r0[cx + 1] * (256 - fv) + r1[cx + 1] * fv) * ta >> 8;
      }
      uint32_t fu = (uint32_t)(u0 >> 8) & 255, aa = (c0 * (256 - fu) + c1 * fu) >> 16;
      if (!aa) continue;
      uint32_t s = aa << 24 | ((crb * aa >> 8) & 0xFF00FF) | ((cg * aa >> 8) & 0xFF00);
      TAKE(row, x, p, s);
    }
    return;
  }
  if (mono) {
    /* turned, one color: along the row, within one texel cell, the mix is a quadratic (bilinear on a line): stepped
     * by its differences, the cell's four texels read once */
    while (x < e) {
      int cx = u0 >> 16, rr = v0 >> 16;
      int n = steps_in(u0, dux, cx << 16, (cx + 1) << 16), nv = steps_in(v0, dvx, rr << 16, (rr + 1) << 16);
      n = n < nv ? n : nv;
      n = n < e - x ? n : e - x;
      const uint8_t *p0 = al + rr * W + cx, *p1 = p0 + W;
      float t0 = p0[0], t1 = p0[1], t2 = p1[0], t3 = p1[1], k = t0 - t1 - t2 + t3;
      float fu = (float)(u0 & 0xFFFF) * (1.0f / 65536), fv = (float)(v0 & 0xFFFF) * (1.0f / 65536);
      float du = (float)dux * (1.0f / 65536), dv = (float)dvx * (1.0f / 65536), q = (float)ta * 256;
      /* (f(j) = f0 + f1 j + f2 j^2; its alpha x ta / 256 in 16.16) */
      float f0 = t0 + (t1 - t0) * fu + (t2 - t0) * fv + k * fu * fv;
      float f1 = (t1 - t0) * du + (t2 - t0) * dv + k * (fu * dv + fv * du), f2 = k * du * dv;
      int32_t F = (int32_t)(f0 * q), D1 = (int32_t)((f1 + f2) * q), D2 = (int32_t)(2 * f2 * q);
      u0 += dux * n, v0 += dvx * n;
      for (int xe = x + n; x < xe; x++, p++, F += D1, D1 += D2) {
        int32_t aa = F >> 16;
        if (aa <= 0) continue;
        if (aa > 255) aa = 255;
        uint32_t s = (uint32_t)aa << 24 | ((crb * (uint32_t)aa >> 8) & 0xFF00FF) | ((cg * (uint32_t)aa >> 8) & 0xFF00);
        TAKE(row, x, p, s);
      }
    }
    return;
  }
  for (; x < e; x++, p++, u0 += dux, v0 += dvx) {
    int cx = u0 >> 16, rr = v0 >> 16;
    uint32_t fu = (uint32_t)(u0 >> 8) & 255, fv = (uint32_t)(v0 >> 8) & 255, s;
    if (mono) {
      const uint8_t *p0 = al + rr * W + cx, *p1 = p0 + W;
      uint32_t top = p0[0] * (256 - fu) + p0[1] * fu, bot = p1[0] * (256 - fu) + p1[1] * fu;
      uint32_t aa = ((top * (256 - fv) + bot * fv) >> 16) * ta >> 8;
      if (!aa) continue;
      s = aa << 24 | ((crb * aa >> 8) & 0xFF00FF) | ((cg * aa >> 8) & 0xFF00);
    } else {
      const uint32_t *p0 = tx + rr * W + cx, *p1 = p0 + W;
      s = lerp4(lerp4(p0[0], p0[1], fu), lerp4(p1[0], p1[1], fu), fv);
      if (!(s >> 24)) continue;
    }
    TAKE(row, x, p, s);
  }
}

/* the blurred background's own textures, if drawn in front of the blur plane: interpolated tiles */
static void run_bilinear(const Ctx *c, int y, int a, int b, BiTex *bt) {
  const Map *m = c->m;
  int mode = c->mode, row = c->row;
  float fx = a + 0.5f - m->ox, fy = SY(y) - m->oy;
  int32_t u = (int32_t)((m->iux * fx + m->iuy * fy - 0.5f) * 65536) + 65536, v = (int32_t)((m->ivx * fx + m->ivy * fy - 0.5f) * 65536) + 65536;
  int32_t dux = (int32_t)(m->iux * 65536), dvx = (int32_t)(m->ivx * 65536);
  int i0 = 0, i1 = b - a;
  clip_lin(u, dux, (int32_t)(c->r->w + 1) << 16, b - a, &i0, &i1);
  clip_lin(v, dvx, (int32_t)(c->r->h + 1) << 16, b - a, &i0, &i1);
  u += dux * i0 - 65536, v += dvx * i0 - 65536;
  for (int x = a + i0, e = a + i1, p = row * VIEW_W + a + i0; x < e; x++, p++, u += dux, v += dvx) {
    uint32_t s = bi_sample(bt, u, v);
    if (s >> 24) TAKE(row, x, p, s);
  }
}

__attribute__((noinline)) static void draw_item(int idx, Item *it, int sy0, int sy1) {
  const Inst *in = &it->in;
  int iy0 = it->y0 / vsf, iy1 = (it->y1 + vsf - 1) / vsf;   /* (its rows, in the strip's) */
  int ya = iy0 > sy0 ? iy0 : sy0, yb = iy1 < sy1 ? iy1 : sy1;
  /* (its rows from the first with light left in its box: none, nothing to make ready) */
  while (ya < yb && open_from(cov[ya - sy0], it->x0, it->x1) >= it->x1) ya++;
  if (ya >= yb) return;
  /* (its palette, unless another took its place: more than NPAL this frame, it is made again) */
  const Pal *pp = &pals[it->pal];
  Pal again;
  if (pal_over) {
    uint16_t ptex = (in->flags & F_SOLID) ? TEX_NONE : in->tex;
    uint32_t pkey = pal_key(ptex, in->tint, in->flags, inst_alpha(in));
    if (pkey && (pp->key != pkey || pp->screen != it->screen))
      pal_fill(&again, ptex, in->tint, in->flags, inst_alpha(in), it->screen), pp = &again;
  }
  Ctx c = {idx, it->screen ? BL_ALPHA : in->flags & F_BLEND, 0, sy0, in, NULL, NULL, pp->c, it, pp->solid};
  Map m;
  int kind = 0;   /* 0 solid, 1 axis, 2 turned, 3 soft, 4 bilinear */
  const uint32_t *soft = NULL;
  uint32_t crb = 0, cg = 0, ta = 256;
  int sk = 0;
  BiTex bt;
  if (!(in->flags & F_SOLID)) {
    c.r = tex_rec(in->tex);
    inst_map(in, &m);
    c.m = &m;
    if (c.r->fmt >= FMT_SOFT) {
      kind = 3;
      soft = soft_get(in->tex, in->tint, in->flags, inst_alpha(in));
      if (!soft) return;
      sk = soft_kind(in->tex, c.r, soft);
      if (c.r->fmt == FMT_SOFTA) {
        const uint8_t *t = tint_rgba(in->tint);
        float tr = t[0] / 255.0f, tg = t[1] / 255.0f, tb = t[2] / 255.0f;
        if (in->flags & F_LIT) tr *= g_room.h->ambient[0], tg *= g_room.h->ambient[1], tb *= g_room.h->ambient[2];
        uint32_t base = softa_color(c.r);
        grade_off = in->tint >= HUD_TINT;
        uint32_t col = grade((base & 255) / 255.0f * tr, (base >> 8 & 255) / 255.0f * tg, (base >> 16 & 255) / 255.0f * tb, 1);
        crb = col & 0xFF00FF, cg = col & 0xFF00, ta = inst_alpha(in) + 1u;
      }
    } else if ((c.r->flags & TEX_SMOOTH) && c.r->fmt == FMT_PAL4) {
      kind = 4;
      bt = BITEX(in->tex, c.r->tw, c.r, NULL, c.pal);
      if (it->ar != 0xFFFF && (in->flags & F_ROT)) bt.slots = arena + it->ar;
    } else
      kind = m.rot ? 2 : 1;
  }
#ifdef HOST
  if (getenv("PROBE")) {
    int px, py;
    sscanf(getenv("PROBE"), "%d,%d", &px, &py);
    if (py >= ya && py < yb && px >= it->x0 && px < it->x1)
      fprintf(stderr, "item %d kind %d tex %u fmt %d blend %d z %.2f rb %08x g %04x trans-before %u\n", idx, kind, in->tex,
              c.r ? c.r->fmt : -1, c.mode, in->z / 128.0, acc[(py - sy0) * VIEW_W + px].rb, acc[(py - sy0) * VIEW_W + px].gt & 0xFFFF, T_OF(acc[(py - sy0) * VIEW_W + px].gt));
  }
#endif
  uint16_t local[64], *tp = NULL;
  if (kind == 1) {
    tp = it->ar != 0xFFFF ? arena + it->ar : local;
    if (tp == local) it->cty = -1;
  }
  int last_key = -1;
  const uint8_t *last_t = NULL;
  for (int y = ya; y < yb; y++) {
    c.row = y - sy0;
    const uint32_t *cv = cov[c.row];
    int x = open_from(cv, it->x0, it->x1);
    if (x >= it->x1) continue;
    int ty = 0, vr = 0;
    if (kind == 1) {
      int v = (int)((SY(y) - m.oy) * m.ivy);
      if (v < 0 || v >= c.r->h) continue;
      ty = v / TILE, vr = v & (TILE - 1);
      if (ty != it->cty || g_tex_overload) {
        it->cty = (int16_t)ty;
        for (int i = 0; i < c.r->tw; i++) tp[i] = UNRES_SLOT;
      }
    }
    int xe = it->x1;
    if (it->clip) {
      /* (a circle or a box: this row's span of it) */
      const HudClip *k = &hud_clips[it->clip - 1];
      int ca, cb;
      if (k->box) {
        if (SY(y) < k->y0 || SY(y) >= k->y1) continue;
        ca = (int)(k->x0 + 0.5f), cb = (int)(k->x1 + 0.5f);
      } else {
        float dy = SY(y) - k->cy, w2 = k->r * k->r - dy * dy;
        if (w2 <= 0) continue;
        float w = sqrtf(w2);
        ca = (int)(k->cx - w + 0.5f), cb = (int)(k->cx + w + 0.5f);
      }
      if (cb < xe) xe = cb;
      if (ca > x) x = open_from(cv, ca, it->x1);
    }
    while (x < xe) {
      int e = closed_from(cv, x, xe);
      COUNT_PX(kind, e - x);
#ifdef HOST
      if (kind == 3) COUNT_PX(5, (e - x) * (c.r->fmt == FMT_SOFTA ? 1 : 0) * (m.rot ? 1 : 0));
#endif
      switch (kind) {
        case 0: run_solid(&c, y, x, e); break;
        case 1: run_axis(&c, y, x, e, tp, ty, vr); break;
        case 2: run_turned(&c, y, x, e, &last_key, &last_t); break;
        case 3: run_soft(&c, y, x, e, soft, crb, cg, ta, sk); break;
        default: run_bilinear(&c, y, x, e, &bt); break;
      }
      x = open_from(cv, e, xe);
    }
  }
}

/* ---------------------------------------------------------------- the background: what is behind the blur plane,
 * drawn small, blurred and stretched behind the rest (as LightBlurredBackground does) */
#define BG_S 4
#define BG_W (VIEW_W / BG_S)
#define BG_H (VIEW_H / BG_S)
static uint16_t bgbuf[BG_W * BG_H] __attribute__((aligned(4)));   /* RGB565 */
static int bg_step = 1;   /* (its rows drawn: all, or every other one when the room is drawn fast) */
static bool bg_on;

static inline void bg_put(uint16_t *d, uint32_t s, int mode) {
  uint32_t dd = rgb_of565(*d);
  dd = mode ? blend_mode(dd, s, mode) : blend_over(dd, s);
  *d = (uint16_t)((dd >> 8 & 0xF800) | (dd >> 5 & 0x07E0) | (dd >> 3 & 0x001F));
}

/* an item behind the blur plane, into the small background (back to front), sampled at the centers of its pixels */
static void draw_bg_item(Item *it) {
  const Inst *in = &it->in;
  int mode = in->flags & F_BLEND;
  const uint32_t *pal = pals[it->pal].c;
  int bx0 = it->x0 / BG_S, by0 = it->y0 / BG_S, bx1 = (it->x1 + BG_S - 1) / BG_S, by1 = (it->y1 + BG_S - 1) / BG_S;
  if (in->flags & F_SOLID) {
    /* the pixels whose centers are in the box */
    int a = (it->x0 + BG_S / 2) / BG_S, b = (it->x1 + BG_S / 2) / BG_S, c = (it->y0 + BG_S / 2) / BG_S, d = (it->y1 + BG_S / 2) / BG_S;
    for (int by = (c + bg_step - 1) / bg_step * bg_step; by < d; by += bg_step)
      for (int bx = a; bx < b; bx++) bg_put(bgbuf + by * BG_W + bx, pal[1], mode);
    return;
  }
  const TexRec *r = tex_rec(in->tex);
  Map m;
  inst_map(in, &m);
  const uint32_t *soft = NULL;
  uint32_t crb = 0, cg = 0, ta = 256;
  if (r->fmt >= FMT_SOFT) {
    soft = soft_get(in->tex, in->tint, in->flags, inst_alpha(in));
    if (!soft) return;
    if (r->fmt == FMT_SOFTA) {
      const uint8_t *t = tint_rgba(in->tint);
      float tr = t[0] / 255.0f, tg = t[1] / 255.0f, tb = t[2] / 255.0f;
      if (in->flags & F_LIT) tr *= g_room.h->ambient[0], tg *= g_room.h->ambient[1], tb *= g_room.h->ambient[2];
      uint32_t base = softa_color(r);
      grade_off = in->tint >= HUD_TINT;
      uint32_t c = grade((base & 255) / 255.0f * tr, (base >> 8 & 255) / 255.0f * tg, (base >> 16 & 255) / 255.0f * tb, 1);
      crb = c & 0xFF00FF, cg = c & 0xFF00, ta = inst_alpha(in) + 1u;
    }
  }
  BiTex bt = BITEX(in->tex, r->tw, r, NULL, pal);
  if (it->ar != 0xFFFF && (in->flags & F_ROT)) bt.slots = arena + it->ar;
  int W = r->w, H = r->h;
  /* texel coordinates (16.16) less half a texel: where bilinear samples are centered */
  int32_t du = (int32_t)(m.iux * BG_S * 65536), dv = (int32_t)(m.ivx * BG_S * 65536);
  for (int by = (by0 + bg_step - 1) / bg_step * bg_step; by < by1; by += bg_step) {
    uint16_t *d16 = bgbuf + by * BG_W;
    float fx = bx0 * BG_S + BG_S * 0.5f - m.ox, fy = by * BG_S + BG_S * 0.5f - m.oy;
    int32_t u = (int32_t)((m.iux * fx + m.iuy * fy - 0.5f) * 65536), v = (int32_t)((m.ivx * fx + m.ivy * fy - 0.5f) * 65536);
    int i0 = 0, i1 = bx1 - bx0;
    if (r->fmt == FMT_ALPHA2) {
      /* nearest */
      u += 32768, v += 32768;
      clip_lin(u, du, W << 16, i1, &i0, &i1);
      clip_lin(v, dv, H << 16, i1, &i0, &i1);
      u += du * i0, v += dv * i0;
      for (int bx = bx0 + i0; bx < bx0 + i1; bx++, u += du, v += dv) {
        int ui = u >> 16, vi = v >> 16;
        const uint8_t *t = bi_tile(&bt, ui >> 5, vi >> 4);
        if (!t) continue;
        int ur = ui & 31, vr = vi & 15, ci = t[vr * 8 + (ur >> 2)] >> ((ur & 3) * 2) & 3;
        if (ci) bg_put(d16 + bx, pal[ci], mode);
      }
    } else if (r->fmt == FMT_PAL4) {
      /* bilinear: where any of the four texels is inside */
      clip_lin(u + 65536, du, (W + 1) << 16, i1, &i0, &i1);
      clip_lin(v + 65536, dv, (H + 1) << 16, i1, &i0, &i1);
      u += du * i0, v += dv * i0;
      if (dv == 0) {
        /* upright: the row's two texel rows mixed once per texel column, then the columns (most of these textures are
         * much bigger than the background's pixels: a column serves several) */
        int rr = v >> 16, lc = -2;
        uint32_t fv = (uint32_t)(v >> 8) & 255, L0 = 0, L1 = 0;
        for (int bx = bx0 + i0; bx < bx0 + i1; bx++, u += du) {
          int c = u >> 16;
          if (c != lc) {
            L0 = c == lc + 1 ? L1 : lerp4(bi_texel(&bt, c, rr), bi_texel(&bt, c, rr + 1), fv);
            L1 = lerp4(bi_texel(&bt, c + 1, rr), bi_texel(&bt, c + 1, rr + 1), fv);
            lc = c;
          }
          uint32_t s = lerp4(L0, L1, (uint32_t)(u >> 8) & 255);
          if (s >> 24) bg_put(d16 + bx, s, mode);
        }
        continue;
      }
      for (int bx = bx0 + i0; bx < bx0 + i1; bx++, u += du, v += dv) {
        uint32_t s = bi_sample(&bt, u, v);
        if (s >> 24) bg_put(d16 + bx, s, mode);
      }
    } else {
      /* smooth: bilinear, the edges repeated, at the pixels whose centers are in the sprite */
      clip_lin(u + 32768, du, W << 16, i1, &i0, &i1);
      clip_lin(v + 32768, dv, H << 16, i1, &i0, &i1);
      u += du * i0, v += dv * i0;
      for (int bx = bx0 + i0; bx < bx0 + i1; bx++, u += du, v += dv) {
        int c = u >> 16, rr = v >> 16;
        uint32_t wu = (uint32_t)(u >> 8) & 255, wv = (uint32_t)(v >> 8) & 255;
        int c0 = c < 0 ? 0 : c >= W ? W - 1 : c, c1 = c + 1 < 0 ? 0 : c + 1 >= W ? W - 1 : c + 1;
        int r0 = rr < 0 ? 0 : rr >= H ? H - 1 : rr, r1 = rr + 1 < 0 ? 0 : rr + 1 >= H ? H - 1 : rr + 1;
        uint32_t s;
        if (r->fmt == FMT_SOFT)
          s = lerp4(lerp4(soft[r0 * W + c0], soft[r0 * W + c1], wu), lerp4(soft[r1 * W + c0], soft[r1 * W + c1], wu), wv);
        else {
          const uint8_t *al = (const uint8_t *)soft;
          uint32_t top = al[r0 * W + c0] * (256 - wu) + al[r0 * W + c1] * wu, bot = al[r1 * W + c0] * (256 - wu) + al[r1 * W + c1] * wu;
          uint32_t aa = ((top * (256 - wv) + bot * wv) >> 16) * ta >> 8;
          s = aa << 24 | ((crb * aa >> 8) & 0xFF00FF) | ((cg * aa >> 8) & 0xFF00);
        }
        if (s >> 24) bg_put(d16 + bx, s, mode);
      }
    }
  }
}

/* the background shows where light is left; then the strip goes to the screen, dithered. The strip's buffers are
 * left cleared for the next one. */
static uint32_t bgline[BG_W + 2];   /* a row of the background (0xRRGGBB), its edges repeated */


/* light (x 128) to 5 or 6 bits (dithering added before): one instruction on the calculator */
#if defined(__arm__) && defined(__ARM_FEATURE_SIMD32) /* (ARMv6 and later: not the Nspire's ARM926) */
static inline uint32_t to5(uint32_t v) {
  uint32_t r;
  __asm__("usat %0, #5, %1, asr #10" : "=r"(r) : "r"(v));
  return r;
}
static inline uint32_t to6(uint32_t v) {
  uint32_t r;
  __asm__("usat %0, #6, %1, asr #9" : "=r"(r) : "r"(v));
  return r;
}
/* red and blue (16 bits each) and green to a 16-bit pixel: red and blue doubled at once, each at most 65535, their
 * top 5 bits are the colors */
static inline uint32_t to565(uint32_t rb, uint32_t g) {
  uint32_t w;
  __asm__("uqadd16 %0, %1, %1" : "=r"(w) : "r"(rb));
  return (w >> 16 & 0xF800) | (w >> 11 & 31) | to6(g) << 5;
}
#else
static inline uint32_t to5(uint32_t v) { return v >= (32u << 10) ? 31u : v >> 10; }
static inline uint32_t to6(uint32_t v) { return v >= (64u << 9) ? 63u : v >> 9; }
static inline uint32_t to565(uint32_t rb, uint32_t g) { return to5(rb >> 16) << 11 | to6(g) << 5 | to5(rb & 0xFFFF); }
#endif

/* 4 pixels with no light left from behind to 16 bits (green: gt's low half, gm 0xFFFF; or all of it, gm ~0, if
 * nothing is left anyway), two to a word */
__attribute__((always_inline)) static inline void out4(const Acc *A, uint16_t *O, uint32_t gm) {
  uint32_t p0 = to565(A[0].rb, A[0].gt & gm), p1 = to565(A[1].rb, A[1].gt & gm);
  uint32_t p2 = to565(A[2].rb, A[2].gt & gm), p3 = to565(A[3].rb, A[3].gt & gm);
  uint32_t *o = (uint32_t *)(void *)O;
  o[0] = p0 | p1 << 16, o[1] = p2 | p3 << 16;
}

/* a pixel over the background (bg: 0xRRGGBB) to 16 bits: red and blue in their lanes together */
__attribute__((always_inline)) static inline uint32_t bg_px(const Acc *a, uint32_t bg) {
  uint32_t gt = a->gt, t = (T_OF(gt) + 1u) >> 1;
  return to565(a->rb + (bg & 0xFF00FF) * t, (gt & 0xFFFF) + (bg >> 8 & 255) * t);
}

/* a row of the strip to 16 bits (its dither in it already), over the background (bl: NULL if none). O is the
 * row's place in the strip's first row (at or before A) */
static void row_out(const Acc *A, uint16_t *O, const uint32_t *bl) {
  if (!bl) {
    for (int x = 0; x < VIEW_W; x += 4, A += 4, O += 4) out4(A, O, 0xFFFF);
    return;
  }
  for (int x = 0; x < VIEW_W; x += 4, A += 4, O += 4, bl++) {
    if (!((A[0].gt | A[1].gt | A[2].gt | A[3].gt) >> 16)) {
      out4(A, O, ~0u);
      continue;
    }
    /* screen pixel 4i + j samples the background at i - 0.375 + j / 4: between texels i - 1 and i, then i and
     * i + 1 (bgline is one texel ahead) */
    uint32_t b0 = bl[0], b1 = bl[1], b2 = bl[2];
    uint32_t p0 = bg_px(A, lerp4(b0, b1, 160)), p1 = bg_px(A + 1, lerp4(b0, b1, 224));
    uint32_t p2 = bg_px(A + 2, lerp4(b1, b2, 32)), p3 = bg_px(A + 3, lerp4(b1, b2, 96));
    uint32_t *o = (uint32_t *)(void *)O;
    o[0] = p0 | p1 << 16, o[1] = p2 | p3 << 16;
  }
}

/* the strip cleared for the next one */
static void strip_clear(void) {
  for (int y = 0; y < STRIP_H; y++) {
    const Acc *c = acc_clear[y];
    uint32_t a0 = c[0].rb, a1 = c[0].gt, a2 = c[1].rb, a3 = c[1].gt, a4 = c[2].rb, a5 = c[2].gt, a6 = c[3].rb, a7 = c[3].gt;
    uint32_t *d = (uint32_t *)(void *)(acc + y * VIEW_W);
    for (int i = 0; i < VIEW_W / 4; i++, d += 8) d[0] = a0, d[1] = a1, d[2] = a2, d[3] = a3, d[4] = a4, d[5] = a5, d[6] = a6, d[7] = a7;
  }
}

__attribute__((noinline)) static void finish_strip(int sy0, int rows) {
#ifdef HOST
  if (getenv("PROBE")) {
    int px, py;
    sscanf(getenv("PROBE"), "%d,%d", &px, &py);
    if (py >= sy0 && py < sy0 + rows) {
      int p = (py - sy0) * VIEW_W + px;
      fprintf(stderr, "probe %d,%d: trans %u acc r %u g %u b %u bg_on %d bg565 %04x\n", px, py, T_OF(acc[p].gt), acc[p].rb >> 16,
              acc[p].gt & 0xFFFF, acc[p].rb & 0xFFFF, bg_on,
              bgbuf[(py / BG_S) * BG_W + px / BG_S]);
    }
  }
#endif
  uint16_t *out = (uint16_t *)(void *)acc;   /* in place: each 16-bit pixel goes where its 8 bytes were read */
  for (int y = 0; y < rows; y++) {
    float sy = sy0 + y * vsf + (vsf - 1) * 0.5f;   /* (the screen's row, or between two) */
    bool bg = bg_on;
    if (bg) {   /* (not if no light is left anywhere on the row) */
      uint32_t all = ~0u;
      for (int i = 0; i < VIEW_W / 32; i++) all &= cov[y][i];
      bg = all != ~0u;
    }
    if (bg) {
      /* the background's row, between two of its own rows */
      float fy = ((sy + 0.5f) / BG_S - 0.5f) / bg_step;
      int r0 = (int)(fy + 4096.0f) - 4096, last = BG_H - bg_step;
      uint32_t wv = (uint32_t)((fy - r0) * 256);
      r0 *= bg_step;
      int r1 = r0 + bg_step;
      const uint16_t *a16 = bgbuf + (r0 < 0 ? 0 : r0 > last ? last : r0) * BG_W;
      const uint16_t *b16 = bgbuf + (r1 < 0 ? 0 : r1 > last ? last : r1) * BG_W;
      for (int i = 0; i < BG_W; i++) bgline[i + 1] = lerp4(rgb_of565(a16[i]), rgb_of565(b16[i]), wv);
      bgline[0] = bgline[1], bgline[BG_W + 1] = bgline[BG_W];
    }
    /* (two rows each: the second where the first's 8 bytes were read) */
    row_out(acc + y * VIEW_W, out + y * vsf * VIEW_W, bg ? bgline : NULL);
    if (vsf == 2) copy_words(out + (2 * y + 1) * VIEW_W, out + 2 * y * VIEW_W, VIEW_W * 2);
  }
  plat_push(0, VIEW_Y + sy0, VIEW_W, rows * vsf, out);
  strip_clear();
}

/* the items that kept a slot for this frame look for their tile again */
void gfx_slot_reused(int s) {
  for (int i = 0; i < arena_top; i++)
    if (arena[i] == s) arena[i] = UNRES_SLOT;
}

/* a rotated item's slots (all its tiles) or an upright one's (a row), in the arena: false if it is full */
static bool arena_take(Item *it, bool keep) {
  const Inst *in = &it->in;
  it->ar = 0xFFFF, it->cty = -1;
  if (in->flags & F_SOLID) return true;
  const TexRec *tr = tex_rec(in->tex);
  int n = (in->flags & F_ROT) ? tr->tw * tr->th : tr->tw;
  if (!n || n > 256 || arena_top + n > ARENA) return true;
  it->ar = (uint16_t)arena_top;
  if (keep) arena_top += n;
  if (in->flags & F_ROT)
    for (int q = 0; q < n; q++) arena[it->ar + q] = UNRES_SLOT;
  return true;
}

/* ---------------------------------------------------------------- the frame */
typedef struct {
  Inst in;
  uint8_t minor;   /* (geo, debris, specks: what gives way to the others when there is no room left) */
  uint32_t group;
} Actor;
#define MAX_ACTORS 64
static Actor actors[MAX_ACTORS];
static int nactors;

/* the HUD: in front of all, its own camera's (not graded with the room, not faded) */
#define MAX_HUD 48
static Inst hud[MAX_HUD];
static uint8_t hud_clip_of[MAX_HUD];
static int nhud;
#define HUD_Z (-1.342f)   /* (where the room's camera sees as much as the HUD's, 8.7107 units half height) */

bool gfx_hud(const Inst *in, int clip) {
  if (nhud >= MAX_HUD) return false;
  hud_clip_of[nhud] = (uint8_t)clip;
  hud[nhud++] = *in;
  return true;
}

void gfx_hud_clip(int clip, float x, float y, float r) {
  /* (HUD units, from the screen's center, to pixels) */
  float k = FOCAL / (HUD_Z - CAM_Z);
  hud_clips[clip - 1] = (HudClip){VIEW_W / 2 + x * k, VIEW_H / 2 - y * k, r * k, 0, 0, 0, 0, false};
}

void gfx_hud_fill(float x0, float y0, float x1, float y1, uint8_t tint) {
  /* (a solid color: its top left, its size) */
  Inst in;
  memset(&in, 0, sizeof in);
  in.ax = (int16_t)lrintf(x0 * 64), in.ay = (int16_t)lrintf(y1 * 64);
  in.tex = TEX_NONE, in.flags = F_SOLID, in.tint = tint;
  in.a = to_f16(x1 - x0), in.b = to_f16(y0 - y1);
  gfx_hud(&in, 0);
}

void gfx_hud_rect(int clip, float x0, float y0, float x1, float y1) {
  float k = FOCAL / (HUD_Z - CAM_Z);
  hud_clips[clip - 1] = (HudClip){0, 0, 0, VIEW_W / 2 + x0 * k, VIEW_H / 2 - y1 * k, VIEW_W / 2 + x1 * k, VIEW_H / 2 - y0 * k, true};
}

/* text: lines of glyphs (4-bit alpha), drawn in front of what is behind them in their layer */
typedef struct {
  const uint8_t *s;
  const uint8_t *style;
  float x;            /* the pen's start (view pixels) */
  int16_t y, y0, y1;  /* the baseline; the rows it can cover */
  uint8_t n, layer;
  uint32_t rgb;       /* (premultiplied by its alpha: 0xAARRGGBB) */
} TextRun;
#define MAX_RUNS 32   /* (a shop's list, its description, its keys) */
static TextRun runs[MAX_RUNS];
static int nruns;

#ifdef HOST
/* (tests: the text of each frame drawn, checked: in the screen, no line over another, none left out) */
bool g_text_audit;
char g_text_audit_log[2048];
static uint32_t runs_dropped;
static void text_box_px(const TextRun *r, int *x0, int *y0, int *x1, int *y1) {
  float pen = r->x;
  *x0 = *y0 = 1 << 30, *x1 = *y1 = -(1 << 30);
  for (int i = 0; i < r->n; i++) {
    const Glyph *g = font_glyph(r->style, r->s[i], 0);
    if (!g) continue;
    int gx = (int)floorf(pen) + g->left, gy = r->y - g->top;
    pen += g->adv * (1.0f / 64);
    if (!g->w) continue;
    if (gx < *x0) *x0 = gx;
    if (gx + g->w > *x1) *x1 = gx + g->w;
    if (gy < *y0) *y0 = gy;
    if (gy + g->h > *y1) *y1 = gy + g->h;
  }
}
static void text_audit(void) {
  char *o = g_text_audit_log;
  size_t left = sizeof g_text_audit_log;
  o[0] = 0;
#define LOG(...) do { int k_ = snprintf(o, left, __VA_ARGS__); if (k_ > 0 && (size_t)k_ < left) o += k_, left -= (size_t)k_; } while (0)
  if (runs_dropped) LOG("%u lines left out; ", runs_dropped);
  runs_dropped = 0;
  for (int i = 0; i < nruns; i++) {
    const TextRun *a = &runs[i];
    if ((a->rgb >> 24) < 128) continue;
    int ax0, ay0, ax1, ay1;
    text_box_px(a, &ax0, &ay0, &ax1, &ay1);
    if (ax1 < ax0 || ax1 <= 0 || ay1 <= 0 || ax0 >= VIEW_W || ay0 >= VIEW_H) continue;   /* (not shown at all) */
    if (ax0 < 0 || ay0 < 0 || ax1 > VIEW_W || ay1 > VIEW_H)
      LOG("\"%.*s\" off the screen (%d,%d-%d,%d); ", a->n, (const char *)a->s, ax0, ay0, ax1, ay1);
    for (int j = i + 1; j < nruns; j++) {
      const TextRun *b = &runs[j];
      if ((b->rgb >> 24) < 128) continue;
      int bx0, by0, bx1, by1;
      text_box_px(b, &bx0, &by0, &bx1, &by1);
      int ox = (ax1 < bx1 ? ax1 : bx1) - (ax0 > bx0 ? ax0 : bx0), oy = (ay1 < by1 ? ay1 : by1) - (ay0 > by0 ? ay0 : by0);
      if (ox > 1 && oy > 1)
        LOG("\"%.*s\" over \"%.*s\"; ", a->n, (const char *)a->s, b->n, (const char *)b->s);
    }
  }
#undef LOG
}
#endif

bool gfx_text(int style, float x, float y, const uint8_t *s, int n, uint32_t argb, int layer) {
  uint32_t a = argb >> 24;
#ifdef HOST
  if (nruns >= MAX_RUNS && n > 0 && a) runs_dropped++;
#endif
  if (nruns >= MAX_RUNS || n <= 0 || !a) return false;
  const uint8_t *st = font_style(style);
  TextRun *r = &runs[nruns++];
  r->s = s, r->style = st, r->x = x, r->n = (uint8_t)(n > 255 ? 255 : n), r->layer = (uint8_t)layer;
  r->y = (int16_t)lrintf(y);
  r->y0 = (int16_t)(r->y - (int)font_asc(st) - 3), r->y1 = (int16_t)(r->y + (int)font_desc(st) + 3);
  uint32_t cr = (argb >> 16 & 255) * a / 255, cg = (argb >> 8 & 255) * a / 255, cb = (argb & 255) * a / 255;
  r->rgb = a << 24 | cr << 16 | cg << 8 | cb;
  return true;
}

static void draw_runs(int layer, int sy0, int sy1) {
  for (int k = 0; k < nruns; k++) {
    const TextRun *r = &runs[k];
    if (r->layer != layer || r->y0 >= sy1 || r->y1 <= sy0) continue;
    uint32_t a0 = r->rgb >> 24, rb0 = r->rgb & 0xFF00FF, g0 = r->rgb >> 8 & 255;
    float pen = r->x;
    for (int i = 0; i < r->n; i++) {
      float fl = floorf(pen);
      int ip = (int)fl, ph = (int)((pen - fl) * FONT_PHASES + 0.5f);
      if (ph >= FONT_PHASES) ph = 0, ip++;
      const Glyph *g = font_glyph(r->style, r->s[i], ph);
      if (!g) continue;
      int gx = ip + g->left, gy = r->y - g->top;
      pen += g->adv * (1.0f / 64);
      if (!g->w || gy >= sy1 || gy + g->h <= sy0 || gx >= VIEW_W || gx + g->w <= 0) continue;
      int stride = (g->w + 1) >> 1;
      const uint8_t *bits = (const uint8_t *)(g + 1);
      int ya = gy < sy0 ? sy0 : gy, yb = gy + g->h > sy1 ? sy1 : gy + g->h;
      int xa = gx < 0 ? 0 : gx, xb = gx + g->w > VIEW_W ? VIEW_W : gx + g->w;
      for (int y = ya; y < yb; y++) {
        const uint8_t *row = bits + (y - gy) * stride;
        int p = (y - sy0) * VIEW_W;
        for (int x = xa; x < xb; x++) {
          int u = x - gx;
          uint32_t v = row[u >> 1] >> ((u & 1) * 4) & 15;
          if (!v) continue;
          uint32_t w = v * 17 * 257 >> 8;   /* (0 .. 256) */
          uint32_t s = (a0 * w >> 8) << 24 | (((rb0 * w) >> 8) & 0xFF00FF) | ((g0 * w >> 8) << 8);
          take_over(y - sy0, x, p + x, s);
        }
      }
    }
  }
}

#ifdef HOST
uint32_t g_actors_dropped, g_actors_peak, g_items_dropped_total, g_items_peak, g_hero_undrawn;
#endif
/* (kept in draw order: sorting layer and order, then far first; those well off the screen not kept, they take no
 * room; when there is none left, an actor that matters takes a minor one's) */
static bool actor_add(const Inst *in, uint32_t group, bool minor) {
  float x0, y0, x1, y1;
  if (!inst_screen_box(in, &x0, &y0, &x1, &y1) || x1 < -32 || y1 < -32 || x0 > VIEW_W + 32 || y0 > VIEW_H + 32) return true;
#ifdef HOST
  if ((uint32_t)nactors + 1 > g_actors_peak) g_actors_peak = (uint32_t)nactors + 1;
#endif
  if (nactors >= MAX_ACTORS) {
    int m = nactors - 1;
    if (!minor)
      while (m >= 0 && !actors[m].minor) m--;
    if (minor || m < 0) {
#ifdef HOST
      g_actors_dropped++;
#endif
      return false;
    }
    for (nactors--; m < nactors; m++) actors[m] = actors[m + 1];
  }
  int i = nactors++;
  while (i > 0 && (actors[i - 1].group > group || (actors[i - 1].group == group && actors[i - 1].in.z < in->z)))
    actors[i] = actors[i - 1], i--;
  actors[i].in = *in, actors[i].group = group, actors[i].minor = minor;
  return true;
}
bool gfx_actor(const Inst *in, uint32_t group) { return actor_add(in, group, false); }
bool gfx_actor_minor(const Inst *in, uint32_t group) { return actor_add(in, group, true); }

static bool overlay_on;   /* (the overlay's instances: the room kept back to leave them room, g_gfx_reserve) */
static int items_keep;    /* (the actors and the HUD still to come: the room's own instances leave them room) */
static bool add_item(const Inst *in, float blur_z) {
  Item cur;
  cur.in = *in;
  cur.clip = 0;
  uint32_t alpha = inst_alpha(in);
  if (!alpha) return false;   /* (a hidden group, or a clear tint) */
  if (!item_box(&cur.in, &cur)) return false;
  bool front = !(cur.in.z * (1.0f / 128) > blur_z);
  /* (screening as blending: texels of a palette, not mixed with their neighbors, in front of the blur plane) */
  cur.screen = front && (cur.in.flags & F_BLEND) == BL_SCREEN &&
               ((cur.in.flags & F_SOLID) || (tex_rec(cur.in.tex)->fmt == FMT_PAL4 && !(tex_rec(cur.in.tex)->flags & TEX_SMOOTH)));
  cur.pal = (uint8_t)pal_get(cur.in.flags & F_SOLID ? TEX_NONE : cur.in.tex, cur.in.tint, cur.in.flags, alpha, cur.screen);
  if (!front) {
    arena_take(&cur, false);
    if (!bg_on) zero_words(bgbuf, sizeof bgbuf), bg_on = true;
    draw_bg_item(&cur);
    return false;
  }
  if (nitems >= MAX_ITEMS - (overlay_on ? 0 : g_gfx_reserve) - items_keep) {
    g_gfx_dropped++;   /* (never in the game's rooms: checked by tests) */
#ifdef HOST
    g_items_dropped_total++;
#endif
    return false;
  }
  arena_take(&cur, true);
  items[nitems++] = cur;
  return true;
}

bool g_gfx_no_room;
void (*g_gfx_overlay)(void);
int g_gfx_reserve;

/* the overlay's (as the HUD's, straight into the frame's instances: as many as there is room for) */
bool gfx_overlay(const Inst *src) {
  Inst in = *src;
  in.ax = (int16_t)(in.ax + (int)lrintf(g_cam_x * 64)), in.ay = (int16_t)(in.ay + (int)lrintf(g_cam_y * 64));
  in.z = (int16_t)lrintf(HUD_Z * 128);
  return add_item(&in, 1e9f);
}

void gfx_overlay_fill(float x0, float y0, float x1, float y1, uint8_t tint) {
  Inst in;
  memset(&in, 0, sizeof in);
  in.ax = (int16_t)lrintf(x0 * 64), in.ay = (int16_t)lrintf(y1 * 64);
  in.tex = TEX_NONE, in.flags = F_SOLID, in.tint = tint;
  in.a = to_f16(x1 - x0), in.b = to_f16(y0 - y1);
  gfx_overlay(&in);
}

void gfx_frame(void) {
  bool roomless = g_room.h == NULL || g_gfx_no_room;   /* (the title screen: the HUD's alone) */
  if (!roomless && lut_room != g_room.id) make_luts();
  tex_frame();
  pal_frame++;
  if (!pal_frame) pal_frame = 1;
  pal_over = false;
  nitems = 0;
  arena_top = 0;
  bg_on = false;
  g_gfx_dropped = 0;
  float blur_z = roomless ? 1e9f : g_room.h->blur_z;
  bg_step = g_gfx_fast && !roomless ? 2 : 1;
  /* the instances near the camera, back to front, and the actors among them: what is behind the blur plane is drawn
   * now (small), the rest kept */
  Item cur;
  uint32_t group;
  int na = 0;
  if (!roomless) room_near(g_cam_x, g_cam_y), room_first();
  /* (an actor behind the blur plane but in front of what is kept, by its sorting layer: kept too) */
  while (!roomless && room_next(&cur.in, &group)) {
    while (na < nactors && (actors[na].group < group || (actors[na].group == group && actors[na].in.z > cur.in.z)))
      add_item(&actors[na++].in, nitems ? 1e9f : blur_z);
    items_keep = nactors - na + nhud;
    add_item(&cur.in, blur_z);
    items_keep = 0;
  }
  while (na < nactors) add_item(&actors[na++].in, nitems ? 1e9f : blur_z);
  nactors = 0;
  /* the HUD's, in front (HUD units from the screen's center: where the camera is) */
  int nscene = nitems;
  for (int i = 0; i < nhud; i++) {
    Inst in = hud[i];
    in.ax = (int16_t)(in.ax + (int)lrintf(g_cam_x * 64)), in.ay = (int16_t)(in.ay + (int)lrintf(g_cam_y * 64));
    in.z = (int16_t)lrintf(HUD_Z * 128);
    if (add_item(&in, 1e9f)) items[nitems - 1].clip = hud_clip_of[i];
  }
  nhud = 0;
  if (g_gfx_overlay) overlay_on = true, g_gfx_overlay(), overlay_on = false;
  g_gfx_items = (uint32_t)nitems;
#ifdef HOST
  if ((uint32_t)nitems > g_items_peak) g_items_peak = (uint32_t)nitems;
#endif
  if (!strip_ready) {
    strip_clear();
    strip_ready = true;
  }
#ifdef HOST
  {
    extern uint32_t g_peak_pals, g_peak_arena, g_peak_soft;
    uint32_t n = 0;
    for (int i = 0; i < NPAL; i++) {
      bool used = false;
      for (int j = 0; j < nitems && !used; j++) used = items[j].pal == i;
      n += used;
    }
    if (n > g_peak_pals) g_peak_pals = n;
    if ((uint32_t)arena_top > g_peak_arena) g_peak_arena = (uint32_t)arena_top;
  }
#endif
  /* (the rows where the HUD, text or a menu are: drawn whole) */
  uint8_t busy[VIEW_H / STRIP_H];
  memset(busy, 0, sizeof busy);
  if (g_gfx_fast && !roomless) {
    for (int i = nscene; i < nitems; i++)
      for (int b = items[i].y0 / STRIP_H; b < (items[i].y1 + STRIP_H - 1) / STRIP_H; b++) busy[b] = 1;
    for (int k = 0; k < nruns; k++)
      for (int b = runs[k].y0 / STRIP_H; b < (runs[k].y1 + STRIP_H - 1) / STRIP_H; b++)
        if (b >= 0 && b < VIEW_H / STRIP_H) busy[b] = 1;
  }
  int nstrip = 0;
  for (int sy0 = 0, sy1; sy0 < VIEW_H; sy0 = sy1) {
    int b = sy0 / STRIP_H;
    vsf = g_gfx_fast && !roomless && sy0 + 2 * STRIP_H <= VIEW_H && !busy[b] && !busy[b + 1] ? 2 : 1;
    sy1 = sy0 + STRIP_H * vsf;
    int r0 = sy0 / vsf, r1 = r0 + STRIP_H;   /* (the strip's rows) */
    tex_strip(++nstrip);
    zero_words(cov, sizeof cov);
    if (nruns) draw_runs(0, sy0, sy1);
    for (int i = nitems - 1; i >= nscene; i--)
      if (items[i].y0 < sy1 && items[i].y1 > sy0) draw_item(i, &items[i], r0, r1);
    if (g_screen_fade) {
      /* (the camera fading to black: a black layer between the HUD and the room) */
      uint32_t k = 255u - g_screen_fade;
      for (int p = 0; p < NPX; p++) {
        uint32_t t0 = T_OF(acc[p].gt), t = t0 * k / 255;
        if (t < 2 && t0 >= 2) t = 0, cover(p / VIEW_W, p % VIEW_W);
        acc[p].gt = (acc[p].gt & 0xFFFF) | t << 16;
      }
    }
    if (g_screen_fade < 255 && nruns) draw_runs(1, sy0, sy1);
    if (g_screen_fade < 255)
      for (int i = nscene - 1; i >= 0; i--)
        if (items[i].y0 < sy1 && items[i].y1 > sy0) draw_item(i, &items[i], r0, r1);
    finish_strip(sy0, STRIP_H);
  }
  vsf = 1;
#ifdef HOST
  if (g_text_audit) text_audit();
#endif
  nruns = 0;
#ifdef HOST
  {
    extern uint32_t g_peak_soft;
    uint32_t sn = 0;
    for (int i = 0; i < nsoft; i++)
      if (soft_ent[i].frame == pal_frame) sn += soft_ent[i].n;
    if (sn > g_peak_soft) g_peak_soft = sn;
    if (getenv("SOFTDUMP"))
      for (int i = 0; i < nsoft; i++)
        if (soft_ent[i].frame == pal_frame)
          fprintf(stderr, "soft tex %u key %08x words %u (%dx%d fmt %d)\n", soft_ent[i].key >> 16, soft_ent[i].key, soft_ent[i].n,
                  tex_rec((uint16_t)(soft_ent[i].key >> 16))->w, tex_rec((uint16_t)(soft_ent[i].key >> 16))->h, tex_rec((uint16_t)(soft_ent[i].key >> 16))->fmt);
  }
#endif
}
