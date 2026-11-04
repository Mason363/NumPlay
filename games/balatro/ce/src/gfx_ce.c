/* The renderer on the TI-84 Plus CE: the NumWorks version's (src/gfx.c) list of what a frame shows, drawn at 8 bits a
 * pixel. The screen is cut in 15 strips of 16 rows; a strip is drawn again (in a buffer, then copied to the screen)
 * only when something in it changed: each strip keeps a hash of what covers it. The background's motion is its
 * colours turning (bg_ce.c), which needs no drawing at all.
 *
 * Colours: the interface's are fixed palette entries; a colour that has none (a boss's own, mixed shades of it) takes
 * one of 16 free entries while the screen shows it. Shading (panels, shadows, Options over the game) goes through
 * tables of every entry darkened. */
#include <string.h>
#ifdef NP_HOST
#include <stdio.h>
#include <stdlib.h>
#endif
#include "art.h"
#include "assets.h"
#include "bg.h"
#include "ce.h"
#include "flame.h"

#define SH 16
#define NSTRIP (SCR_H / SH)
#define MAXI 360
#define MAXCLIP 16
#define NDYN 16
/* the fonts, read in the archive (BALLUT) */
#define font_small ((const glyph_t *)(ce_lut + LUT_FONT_SMALL))
#define font_med ((const glyph_t *)(ce_lut + LUT_FONT_MED))
#define font_large ((const glyph_t *)(ce_lut + LUT_FONT_LARGE))
#define font_large_digits ((const glyph_t *)(ce_lut + LUT_FONT_LARGE_DIGITS))
#define font_small_bits (ce_lut + LUT_FONT_SMALL_BITS)
#define font_med_bits (ce_lut + LUT_FONT_MED_BITS)
#define font_large_bits (ce_lut + LUT_FONT_LARGE_BITS)
#define font_large_digits_bits (ce_lut + LUT_FONT_LARGE_DIGITS_BITS)

enum { I_RECT, I_RRECT, I_SHADE, I_SPRITE, I_TEXT, I_FLAME, I_RSQ };

typedef struct {
  int16_t x, y, w, h;
  uint8_t type, clip, r, fx;
  uint16_t a, b;
  uint8_t anim;   /* the frame of what moves on its own (an edition's sheen, the fire) */
  uint8_t s0, s1; /* the strips it covers */
  const char *s;
} item_t;
#ifndef NP_HOST
_Static_assert(offsetof(item_t, clip) == 9 && offsetof(item_t, anim) == 16 && offsetof(item_t, s0) == 17 &&
                   offsetof(item_t, s) == 19 && sizeof(item_t) == 22,
               "blit.S's ce_hash_items reads items");
#endif
void ce_hash_items(unsigned *hs, const item_t *it, unsigned n, const unsigned *clip_hash);

static item_t items[MAXI];
item_t *ce_item_next; /* (blit.S's ce_add reads these) */
int ce_nitems;
const int16_t *ce_clip; /* the clip's: x0, y0, x1, y1 */
uint8_t ce_clip_i;
static char strbuf[2560]; /* (a frame takes under 1 KB: the How to play page) */
static int strpos;
static int16_t clips[MAXCLIP][4];
static int nclips;
static uint8_t strip[SCR_W * SH];
static const uint16_t ROW_OFF[SH] = {0, 320, 640, 960, 1280, 1600, 1920, 2240, 2560, 2880, 3200, 3520, 3840, 4160,
                                     4480, 4800}; /* (a strip's rows: the eZ80 multiplies by calls) */
static int bg_swirl, bg_cover_x;
static uint8_t bg_solid;
static unsigned strip_hash[NSTRIP];
static uint8_t drawn_once;
static unsigned ed_frame; /* the editions' sheen's frame */
uint32_t g_time;

void *g_scratch(int *size) {
  *size = (int)sizeof strip;
  return strip;
}
void g_bg_cover(int x) { bg_cover_x = x; }

#ifdef NP_HOST
#define CE_SLOW (getenv("NP_SLOW") != 0) /* (the C paths, to compare) */
/* (blit.S on the calculator) */
void ce_shade(uint8_t *d, const uint8_t *tab, unsigned n) {
  while (n--) *d = tab[*d], d++;
}
unsigned ce_hash(unsigned h, const void *p, unsigned n) {
  for (const uint8_t *q = p; n; n--) h = (h * 33 ^ *q++) & 0xFFFFFF;
  return h;
}
unsigned ce_hash_words(unsigned h, const void *p, unsigned n) {
  for (const uint8_t *q = p; n; n--, q += 3) h = (h * 33 + (unsigned)(q[0] | q[1] << 8 | q[2] << 16)) & 0xFFFFFF;
  return h;
}
unsigned ce_hash_str(unsigned h, const char *s) {
  while (*s) h = (h * 33 ^ (uint8_t)*s++) & 0xFFFFFF;
  return h;
}
void ce_glyph(uint8_t *d, const uint8_t *rows, unsigned n, unsigned col) {
  for (; n; n--, d += SCR_W, rows++)
    for (int x = 0; x < 8; x++)
      if (*rows & (0x80 >> x)) d[x] = (uint8_t)col;
}
void ce_glyph_shade(uint8_t *d, const uint8_t *rows, unsigned n, const uint8_t *tab) {
  for (; n; n--, d += SCR_W, rows++)
    for (int x = 0; x < 8; x++)
      if (*rows & (0x80 >> x)) d[x] = tab[d[x]];
}
void ce_scale_row(uint8_t *d, const uint8_t *row, unsigned n, unsigned x8, unsigned step, const uint8_t *tab) {
  for (; n; n--, d++, x8 += step) {
    uint8_t v = row[(x8 >> 8) & 255];
    if (v != 255) *d = tab ? tab[*d] : v;
  }
}
void ce_rle(ce_rle_t *b) {
  for (; b->n; b->n--, b->d += SCR_W, b->rt += 2) {
    const uint8_t *row = b->spr + (b->rt[0] | b->rt[1] << 8);
    uint8_t *d = b->d;
    for (int left = b->sw; left > 0;) {
      d += row[0];
      int n = row[1];
      left -= row[0] + n;
      for (int i = 0; i < n; i++, d++) *d = b->tab ? b->tab[*d] : row[2 + i];
      row += 2 + n;
    }
  }
}
#else
#define CE_SLOW 0
_Static_assert(offsetof(ce_rle_t, tab) == 11, "blit.S reads ce_rle_t");
#endif

C darken(C a, int t) { return mix565(a, 0, t); }
C lighten(C a, int t) { return mix565(a, 0xFFFF, t); }

/* ------------------------------------------------------------------ colours */
static C dyn_col[NDYN];
static uint16_t strip_dyn[NSTRIP], frame_dyn; /* the taken entries each strip shows; those this frame uses */
static struct {
  C c;
  uint8_t v, ok;
} pcache[256];
static const uint16_t BIT16[16] = {1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768};
static uint8_t pal_r[CE_NSTATIC], pal_g[CE_NSTATIC], pal_b[CE_NSTATIC]; /* the fixed colours, 5 bits a channel */
static uint16_t sq2[63], sq4[63], sq3[63];                             /* the distance's terms, by difference */

static uint8_t nearest_fixed(C c, int *d) {
  int r = c >> 11, g = (c >> 6) & 31, b = c & 31;
  unsigned best = 0, bd = 0xFFFF;
  for (unsigned i = 0; i < CE_NSTATIC; i++) {
    unsigned e = (unsigned)sq2[r - pal_r[i] + 31] + sq4[g - pal_g[i] + 31] + sq3[b - pal_b[i] + 31];
    if (e < bd) {
      bd = e, best = i;
      if (!e) break;
    }
  }
  if (d) *d = (int)bd;
  return (uint8_t)best;
}

static void shade_tabs_reset(void);

uint8_t ce_pal(C c) {
  uint8_t hi = ((const uint8_t *)&c)[1], h = (uint8_t)((uint8_t)c + (uint8_t)(hi << 3 | hi >> 5)); /* (bytes: the eZ80 shifts by calls) */
  if (pcache[h].ok && pcache[h].c == c) {
    uint8_t v = pcache[h].v;
    if (v < CE_DYN0 || v >= CE_SWIRL0) return v;
    if (dyn_col[v - CE_DYN0] == c) {
      frame_dyn |= BIT16[v - CE_DYN0];
      return v;
    }
  }
  int d;
#ifdef NP_HOST
  extern long ce_pal_misses;
  ce_pal_misses++;
  if (getenv("NP_PAL")) fprintf(stderr, "%u: miss %04X (slot %d had %04X)\n", (unsigned)g_time, c, h, pcache[h].c);
#endif
  uint8_t v = nearest_fixed(c, &d);
  if (d > 24) {
    /* a free entry, or one already showing it */
    uint16_t used = frame_dyn;
    for (int s = 0; s < NSTRIP; s++) used |= strip_dyn[s];
    int k = -1;
    for (int i = 0; i < NDYN; i++)
      if (dyn_col[i] == c && (used & BIT16[i])) k = i;
    if (k < 0)
      for (int i = 0; i < NDYN && k < 0; i++)
        if (!(used & BIT16[i])) k = i;
    if (k >= 0) {
      if (dyn_col[k] != c) {
        dyn_col[k] = c;
        uint16_t p = ce_1555(c);
        ce_set_palette(CE_DYN0 + k, 1, &p);
        shade_tabs_reset();
      }
      frame_dyn |= BIT16[k];
      v = (uint8_t)(CE_DYN0 + k);
    }
  }
  pcache[h].c = c, pcache[h].v = v, pcache[h].ok = 1;
  return v;
}

/* darkened tables: a few kept, made when asked for (the taken entries' only while they are shown) */
#define NSTAB 8
static const int8_t bank_dark[CE_BANKS] = CE_BANK_DARK;
static struct {
  uint8_t alpha, ok;
} stabs[NSTAB];
static uint8_t stab_t[NSTAB][256] __attribute__((aligned(256))); /* (ce_shade looks up by the low byte) */
static uint8_t stab_next;
static void shade_tabs_reset(void) {
  for (int i = 0; i < NSTAB; i++) stabs[i].ok = 0;
}
const uint8_t *ce_shade_tab(int alpha) {
  if (alpha < 0) alpha = 0;
  if (alpha > 32) alpha = 32;
  for (int i = 0; i < NSTAB; i++)
    if (stabs[i].ok && stabs[i].alpha == alpha) return stab_t[i];
  int i = stab_next;
  stab_next = (uint8_t)((stab_next + 1) % NSTAB);
  uint8_t *t = stab_t[i];
  memcpy(t, ce_lut + LUT_SHADE + alpha * CE_NSTATIC, CE_NSTATIC);
  uint16_t used = frame_dyn;
  for (int s = 0; s < NSTRIP; s++) used |= strip_dyn[s];
  for (int k = 0; k < NDYN; k++) t[CE_DYN0 + k] = used & BIT16[k] ? nearest_fixed(mix565(dyn_col[k], 0, alpha), 0) : 0;
  for (int b = 0; b < CE_BANKS; b++) {
    /* the background's banks: the darkness after shading, to the nearest bank */
    int dark = 32 - (32 - bank_dark[b]) * (32 - alpha) / 32, nb = 0;
    for (int q = 1; q < CE_BANKS; q++)
      if ((dark - bank_dark[q]) * (dark - bank_dark[q]) < (dark - bank_dark[nb]) * (dark - bank_dark[nb])) nb = q;
    for (int l = 0; l < CE_LEVELS; l++) t[CE_SWIRL0 + b * CE_LEVELS + l] = (uint8_t)(CE_SWIRL0 + nb * CE_LEVELS + l);
  }
  stabs[i].alpha = (uint8_t)alpha, stabs[i].ok = 1;
  return t;
}

/* ------------------------------------------------------------------ the list */
#define MAXR 8 /* (the interface's corners are 7 at most) */
static uint8_t cut[MAXR + 1][MAXR]; /* a corner's row: the pixels left out at the edge (under half covered) */

void g_init(void) {
  for (int r = 1; r <= MAXR; r++)
    for (int y = 0; y < r; y++)
      for (int x = 0; x < r; x++) {
        /* 4x4 samples of the quarter circle centred at (r, r) */
        int n = 0;
        for (int sy = 0; sy < 4; sy++)
          for (int sx = 0; sx < 4; sx++) {
            int dx = 8 * r - (8 * x + 2 * sx + 1), dy = 8 * r - (8 * y + 2 * sy + 1);
            if (dx * dx + dy * dy <= 64 * r * r) n++;
          }
        if (n * 2 < 16) cut[r][y] = (uint8_t)(x + 1); /* (coverage grows towards the middle) */
      }
  for (int i = 0; i < CE_NSTATIC; i++)
    pal_r[i] = ce_palette[i] >> 10 & 31, pal_g[i] = ce_palette[i] >> 5 & 31, pal_b[i] = ce_palette[i] & 31;
  for (int d = -31; d <= 31; d++) sq2[d + 31] = (uint16_t)(2 * d * d), sq4[d + 31] = (uint16_t)(4 * d * d),
                                  sq3[d + 31] = (uint16_t)(3 * d * d);
  drawn_once = 0;
}

#ifdef CE_PROF
/* where a frame's time goes (ce_main.c prints it at the end), in thousands of cycles: 0 between frames, 1 the list,
   2 the hashes, 3 the strips (4 how many, 5 frames), then in the strips 6 + an item's type, 13 the background, 14 the
   copy to the screen */
#include <sys/timers.h>
unsigned long ce_prof[16];
static unsigned long prof_t, prof_c[16];
static unsigned long prof_now(void) {
  static int on;
  if (!on) on = 1, timer_Enable(2, TIMER_CPU, TIMER_NOINT, TIMER_UP);
  return timer_GetSafe(2, TIMER_UP);
}
#define PROF(k)                        \
  do {                                 \
    unsigned long t_ = prof_now();     \
    prof_c[k] += t_ - prof_t;          \
    ce_prof[k] = prof_c[k] / 1000;     \
    prof_t = t_;                       \
  } while (0)
#else
#define PROF(k) ((void)0)
#endif

void g_begin(void) {
  PROF(0); /* (the time between frames: the rules, the keys, the waiting) */
  ce_nitems = 0;
  ce_item_next = items;
  bg_cover_x = 0;
  strpos = 0;
  nclips = 1;
  ce_clip_i = 0, ce_clip = clips[0];
  frame_dyn = 0;
  ed_frame = (unsigned)(g_time / ED_DT_MS);
  clips[0][0] = 0, clips[0][1] = 0, clips[0][2] = SCR_W, clips[0][3] = SCR_H;
}

void g_clip(int x, int y, int w, int h) {
  if (nclips >= MAXCLIP) return;
  clips[nclips][0] = (int16_t)(x < 0 ? 0 : x);
  clips[nclips][1] = (int16_t)(y < 0 ? 0 : y);
  clips[nclips][2] = (int16_t)(x + w > SCR_W ? SCR_W : x + w);
  clips[nclips][3] = (int16_t)(y + h > SCR_H ? SCR_H : y + h);
  ce_clip_i = (uint8_t)nclips, ce_clip = clips[nclips++];
}
void g_noclip(void) { ce_clip_i = 0, ce_clip = clips[0]; }

/* a new item in the list, or NULL (none left, empty, or wholly outside the clip): its strips worked out, the rest 0
   (blit.S; its C on a computer) */
item_t *ce_add(int type, int x, int y, int w, int h);
#ifdef NP_HOST
item_t *ce_add(int type, int x, int y, int w, int h) {
  if (ce_nitems >= MAXI || w <= 0 || h <= 0) return 0;
  const int16_t *c = ce_clip;
  if (x >= c[2] || y >= c[3] || x + w <= c[0] || y + h <= c[1]) return 0;
  item_t *it = ce_item_next++;
  ce_nitems++;
  it->type = (uint8_t)type;
  it->x = (int16_t)x, it->y = (int16_t)y, it->w = (int16_t)w, it->h = (int16_t)h;
  it->clip = ce_clip_i;
  it->fx = 0, it->r = 0, it->a = 0, it->b = 0, it->s = 0, it->anim = 0;
  int y0 = y > c[1] ? y : c[1], y1 = y + h < c[3] ? y + h : c[3];
  it->s0 = (uint8_t)(y0 / SH), it->s1 = (uint8_t)((y1 - 1) / SH);
  return it;
}
#else
_Static_assert(SH == 16 && MAXI == 360, "blit.S's ce_add");
#endif
#define add ce_add

void g_rect(int x, int y, int w, int h, C c) {
  item_t *it = add(I_RECT, x, y, w, h);
  if (it) it->a = ce_pal(c);
}
void g_line_h(int x, int y, int w, C c) { g_rect(x, y, w, 1, c); }
void g_rrect(int x, int y, int w, int h, int r, C c) {
  if (r > MAXR) r = MAXR;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  item_t *it = add(r > 0 ? I_RRECT : I_RECT, x, y, w, h);
  if (it) it->a = ce_pal(c), it->r = (uint8_t)r;
}
void g_shade(int x, int y, int w, int h, int r, int alpha) {
  if (r > MAXR) r = MAXR;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  item_t *it = add(I_SHADE, x, y, w, h);
  if (it) it->a = (uint16_t)alpha, it->r = (uint8_t)r;
}
void g_sprite(int id, int x, int y, int fx, int scale) {
  if (id < 0 || id >= ART_NSPRITES) return;
  const art_sprite_t *s = &art_sprites[id];
  int w = s->w, h = s->h;
  if (scale != 256) {
    int nw = (w * scale + 128) >> 8, nh = (h * scale + 128) >> 8;
    x += (w - nw) / 2, y += (h - nh) / 2;
    w = nw, h = nh;
  }
  item_t *it = add(I_SPRITE, x, y, w, h);
  if (it) it->a = (uint16_t)id, it->fx = (uint8_t)fx, it->b = (uint16_t)scale, it->anim = fx & FX_EDMASK ? ed_frame : 0;
}
void g_sprite_wh(int id, int x, int y, int w, int h, int fx) {
  item_t *it = add(I_SPRITE, x, y, w, h);
  if (it) it->a = (uint16_t)id, it->fx = (uint8_t)fx, it->b = 0, it->anim = fx & FX_EDMASK ? ed_frame : 0;
}
void g_rsquare(int cx, int cy, int hs, int angle, C c) {
  int R = hs * 3 / 2 + 1;
  item_t *it = add(I_RSQ, cx - R, cy - R, 2 * R + 1, 2 * R + 1);
  if (it) it->a = ce_pal(c), it->r = (uint8_t)hs, it->fx = (uint8_t)angle;
}
void g_flame(int k, int x, int y, int w, int h, int top) {
  if (!flame_on()) return;
  item_t *it = add(I_FLAME, x, y, w, h);
  if (it) it->a = (uint16_t)k, it->b = (uint16_t)top, it->anim = (uint8_t)ce_flame_frame();
}
void g_bg(int swirl, C solid) {
  bg_swirl = swirl;
  bg_solid = ce_pal(solid);
}

/* ------------------------------------------------------------------ text */
static const C code_col[17] = {
    0, 0xFFFF, HEXC(0x009DFF), HEXC(0xFE5F55), HEXC(0xFF9A00), HEXC(0xF3B958), HEXC(0x4BC292), HEXC(0x88888A),
    HEXC(0xA782D1), HEXC(0x13AFCE), 0, HEXC(0x4584FA), HEXC(0x4F6367), 0, 0xFFFF, HEXC(0x4CA893), 0,
};

/* the flags' low byte (the sizes, T_X2, T_TIGHT...), and T_COND in the next */
#define FLO(f) ((uint8_t)(f))
#define FCOND(f) (((const uint8_t *)&(f))[1] & (T_COND >> 8))
static const glyph_t *font(int f) { return FLO(f) & T_LARGE ? font_large : FLO(f) & T_MED ? font_med : font_small; }
static const uint8_t *font_bits(int f) {
  return FLO(f) & T_LARGE ? font_large_bits : FLO(f) & T_MED ? font_med_bits : font_small_bits;
}
static int glyph_rows(int f) { return FLO(f) & T_LARGE ? FONT_LARGE_H : FLO(f) & T_MED ? FONT_MED_H : FONT_SMALL_H; }
int g_cap(int f) {
  int c = FLO(f) & T_LARGE ? 11 : FLO(f) & T_MED ? 9 : 7;
  return FLO(f) & T_X2 ? c + c : c;
}
int g_pitch(int f) {
  int p = FLO(f) & T_LARGE ? 15 : FLO(f) & T_MED ? 13 : LINE_H;
  return FLO(f) & T_X2 ? p + p : p;
}

static const glyph_t *glyph(int f, unsigned ch, const uint8_t **bits) {
  if ((f & (T_COND | T_LARGE)) == (T_COND | T_LARGE) && ch >= '0' && ch <= '9') {
    *bits = font_large_digits_bits;
    return &font_large_digits[ch - '0'];
  }
  *bits = font_bits(f);
  return &font(f)[ch - 32];
}
/* the condensed medium size: one column fewer (see src/gfx.c) */
static int drop_col(int f, unsigned ch, const glyph_t *g) {
  if ((f & (T_COND | T_MED)) != (T_COND | T_MED) || f & T_LARGE) return -1;
  if (ch == 'T' || ch == 'I' || ch == '1') return 0;
  if (ch == 'f' || ch == 't') return 4;
  switch (g->w) {
    case 5: return 3;
    case 6: return 2;
    case 7: return 3;
    case 8: return 4;
  }
  return ch == ' ' ? 99 : -1;
}
static int advance(int f, unsigned ch, const glyph_t *g) {
  int a = g->adv;
  if (drop_col(f, ch, g) >= 0) a--;
  if (f & T_TIGHT) a -= ch == ' ' ? 2 : 1;
  if ((f & T_COND) && (ch == ',' || ch == '.')) a--;
  return a;
}

/* a size's advances (characters 32 to 130), made when first asked for: measuring texts is a frame's busiest work */
#define NADV 6
static struct {
  uint8_t key;
  int8_t a[99];
} advs[NADV];
static uint8_t adv_next;
#ifdef NP_HOST
long ce_adv_made;
#endif
static const int8_t *adv_tab(int f) {
  uint8_t key = (uint8_t)((FLO(f) & (T_LARGE | T_MED | T_TIGHT)) | (FCOND(f) ? T_X2 : 0)) + 1; /* (T_X2: not a size) */
  for (int i = 0; i < NADV; i++)
    if (advs[i].key == key) return advs[i].a - 32;
  int i = adv_next;
  adv_next = (uint8_t)(adv_next == NADV - 1 ? 0 : adv_next + 1);
  const uint8_t *bits;
  for (unsigned ch = 32; ch <= 130; ch++) {
    unsigned c = ch > (FLO(f) & (T_LARGE | T_MED) ? 126u : 130u) ? '?' : ch;
    advs[i].a[ch - 32] = (int8_t)advance(f, c, glyph(f, c, &bits));
  }
  advs[i].key = key;
#ifdef NP_HOST
  ce_adv_made++;
#endif
  return advs[i].a - 32;
}

static int textw(const char *s, int f, int one_line) {
  int w = 0, best = 0, badge = 0;
  const int8_t *adv = adv_tab(f);
  for (; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch >= 32) {
      w += adv[ch > 130 ? '?' : ch];
      continue;
    }
    if (ch == '\n') {
      if (badge) w += 2, badge = 0;
      if (w > best) best = w;
      if (one_line) break;
      w = 0;
      continue;
    }
    if (ch == 14 && !badge) w += 2, badge = 1;
    else if (badge && ch != 14) w += 2, badge = 0;
  }
  if (badge) w += 2;
  if (w > best) best = w;
  if (best > 0) best--;
  return FLO(f) & T_X2 ? best + best : best;
}
int g_textw(const char *s, int f) { return textw(s, f, 0); }

int g_lines(const char *s) {
  int n = 1;
  for (; *s; s++) n += *s == '\n';
  return n;
}

static item_t *text_item(const char *s, int x, int y, int w, int h) {
  int n = (int)strlen(s);
  if (strpos + n + 1 > (int)sizeof strbuf) return 0;
  item_t *it = add(I_TEXT, x, y, w, h);
  if (!it) return 0;
  char *d = strbuf + strpos;
  memcpy(d, s, (size_t)n + 1);
  strpos += n + 1;
  it->s = d;
  return it;
}

void g_text(const char *s, int x, int y, int f, C c) {
  if (!s || !*s) return;
  item_t *it;
  if (f & T_ROT) {
    int len = g_textw(s, f & ~T_ROT);
    if (f & T_CENTER) y += len / 2;
    it = text_item(s, x - 2, y - len - 2, glyph_rows(f) + 4, len + 4);
  } else {
    int w = g_textw(s, f) + 2;
    if (f & T_CENTER) x -= (w - 2) / 2;
    if (f & T_RIGHT) x -= w - 2;
    int h = g_pitch(f) * (g_lines(s) - 1) + glyph_rows(f) * ((f & T_X2) ? 2 : 1) + 3;
    it = text_item(s, x - 2, y - 2, w + 4, h + 2);
  }
  if (it) it->a = ce_pal(c), it->b = (uint16_t)f;
}

void g_text_box(const char *s, int x, int y, int w, int h, int f, C c) {
  int th = g_cap(f) + g_pitch(f) * (g_lines(s) - 1);
  g_text(s, x + w / 2, y + (h - th + 1) / 2, (f & ~(T_RIGHT)) | T_CENTER, c);
}

/* one glyph's pixels (its shadow first), into the strip rows ra..rb, columns cx0..cx1 */
static void put_glyph(const glyph_t *g, const uint8_t *b, int f, int dc, int x, int y, uint8_t col, int y0, int ra,
                      int rb, int cx0, int cx1, const uint8_t *shadow) {
  static const uint8_t BIT8[8] = {1, 2, 4, 8, 16, 32, 64, 128};
  int sc = (f & T_X2) ? 2 : 1, gh = glyph_rows(f);
  for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
    /* the bits column by column, a byte and a mask walked along, the strip by a pointer moved a row at a time (the
       eZ80 shifts and multiplies by calls) */
    const uint8_t *p = b + (g->off >> 3);
    uint8_t m = BIT8[g->off & 7];
    int top = y + (pass ? 0 : sc);
    uint8_t *base = strip + (top - y0) * SCR_W;
    for (int gx = 0; gx < g->w; gx++) {
      int px = x + (gx - (dc >= 0 && gx > dc)) * sc, skip = gx == dc || px >= cx1 || px + sc <= cx0;
      if (skip) {
        for (int gy = 0; gy < gh; gy++) {
          m = (uint8_t)(m + m);
          if (!m) m = 1, p++;
        }
        continue;
      }
      uint8_t *d = base + px;
      int py = top;
      for (int gy = 0; gy < gh; gy++, py += sc, d += sc * SCR_W) {
        int on = *p & m;
        m = (uint8_t)(m + m);
        if (!m) m = 1, p++;
        if (!on) continue;
        if (sc == 1) {
          if (py >= ra && py < rb) *d = pass ? col : shadow[*d];
          continue;
        }
        for (int k = 0; k < 4; k++) {
          int X = px + (k & 1), Y = py + (k >> 1);
          if (Y >= ra && Y < rb && X >= cx0 && X < cx1) {
            uint8_t *q = d + (k & 1) + (k >> 1) * SCR_W;
            *q = pass ? col : shadow[*q];
          }
        }
      }
    }
  }
}

/* the same row by row in eZ80 code (tools/mkart.py's glyph_rows), the glyph wholly between the clip's columns and
   at its size: e its entry (where its rows start, its width), bits the size's rows */
static void fast_glyph(const uint8_t *e, const uint8_t *bits, int gh, int x, int y, uint8_t col, int y0, int ra, int rb,
                       const uint8_t *shadow) {
  const uint8_t *rows = bits + *(const uint16_t *)e;
  for (int pass = shadow ? 0 : 1; pass < 2; pass++) {
    int top = y + 1 - pass, r0 = ra > top ? ra - top : 0, r1 = rb - top < gh ? rb - top : gh;
    if (r0 >= r1) continue;
    uint8_t *d = strip + ROW_OFF[top + r0 - y0] + x;
    const uint8_t *r = rows + r0;
    unsigned n = (unsigned)(r1 - r0);
    for (int part = e[2] > 8 ? 2 : 1; part; part--, d += 8, r += gh) {
      if (pass) ce_glyph(d, r, n, col);
      else ce_glyph_shade(d, r, n, shadow);
    }
  }
}

static void draw_text_rot(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int f = it->b, gh = glyph_rows(f);
  int x = it->x + 2, pen = it->y + it->h - 2;
  const uint8_t *sh = ce_shade_tab(12);
  for (const char *s = it->s; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch < 32 || ch > 126) continue;
    const uint8_t *b;
    const glyph_t *g = glyph(f, ch, &b);
    int dc = drop_col(f, ch, g);
    for (int pass = (f & T_SHADOW) ? 0 : 1; pass < 2; pass++) {
      unsigned bit = g->off;
      for (int gx = 0; gx < g->w; gx++)
        for (int gy = 0; gy < gh; gy++, bit++) {
          if (gx == dc || !((b[bit >> 3] >> (bit & 7)) & 1)) continue;
          int X = x + gy + (pass ? 0 : 1), Y = pen - 1 - (gx - (dc >= 0 && gx > dc));
          if (Y < ra || Y >= rb || X < cx0 || X >= cx1) continue;
          uint8_t *d = strip + (Y - y0) * SCR_W + X;
          *d = pass ? (uint8_t)it->a : sh[*d];
        }
    }
    pen -= advance(f, ch, g);
  }
}

static void draw_text(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int f = it->b;
  if (f & T_ROT) {
    draw_text_rot(it, y0, ra, rb, cx0, cx1);
    return;
  }
  int sc = (f & T_X2) ? 2 : 1, cap = g_cap(f) / sc, pitch = g_pitch(f);
  int x0 = it->x + 2, x = x0, y = it->y + 2;
  int multi = strchr(it->s, '\n') != 0;
  uint8_t col = (uint8_t)it->a, base = col;
  const uint8_t *sh = (f & (T_SHADOW | T_OUTLINE)) ? ce_shade_tab(12) : 0;
  int badge = 0;
  const int8_t *advt = adv_tab(f);
  /* the size's rows (glyphs drawn by fast_glyph) */
  const uint8_t *rtab, *rbits;
  int gh = glyph_rows(f), digits = (f & (T_COND | T_LARGE)) == (T_COND | T_LARGE);
  if (f & T_LARGE) rtab = ce_lut + LUT_ROWS_LARGE, rbits = ce_lut + LUT_ROWBITS_LARGE;
  else if (f & T_MED) {
    if (f & T_COND) rtab = ce_lut + LUT_ROWS_MED_COND, rbits = ce_lut + LUT_ROWBITS_MED_COND;
    else rtab = ce_lut + LUT_ROWS_MED, rbits = ce_lut + LUT_ROWBITS_MED;
  } else
    rtab = ce_lut + LUT_ROWS_SMALL, rbits = ce_lut + LUT_ROWBITS_SMALL;
  int align = (f & (T_CENTER | T_RIGHT)) && multi, total = it->w - 4;
  if (align) {
    int lw = textw(it->s, f, 1);
    x = x0 + ((f & T_RIGHT) ? total - lw : (total - lw) / 2);
  }
  for (const char *s = it->s; *s; s++) {
    unsigned char ch = (unsigned char)*s;
    if (ch == '\n') {
      badge = 0;
      y += pitch;
      x = x0;
      if (align) {
        int lw = textw(s + 1, f, 1);
        x = x0 + ((f & T_RIGHT) ? total - lw : (total - lw) / 2);
      }
      continue;
    }
    if (ch < 32) {
      if (badge && ch != 14) x += 2 * sc, badge = 0;
      if (ch == 16) col = base;
      else if (ch == 14) {
        /* a red badge behind the following text, until the next code */
        const char *e = s + 1;
        int bw = 0;
        for (; (unsigned char)*e >= 32; e++) bw += advt[(unsigned char)*e > 130 ? '?' : (unsigned char)*e];
        bw = bw * sc + 3;
        int by0 = y - 2, by1 = y + cap * sc + 2;
        uint8_t red = ce_pal(HEXC(0xFE5F55));
        for (int yy = by0 > ra ? by0 : ra; yy < by1 && yy < rb; yy++) {
          uint8_t *d = strip + (yy - y0) * SCR_W;
          int a = x, b = x + bw;
          if (yy == by0 || yy == by1 - 1) a++, b--;
          for (int xx = a < cx0 ? cx0 : a; xx < b && xx < cx1; xx++) d[xx] = red;
        }
        x += 2 * sc;
        col = ce_pal(0xFFFF);
        badge = 1;
      } else if (code_col[ch])
        col = ce_pal(code_col[ch]);
      continue;
    }
    if (ch > (f & (T_LARGE | T_MED) ? 126 : 130)) ch = '?';
    int adv = advt[ch] * sc;
    if (x < cx1 && x + adv + sc > cx0 && y < rb && y + gh * sc + sc > ra) {
      int dg = digits && ch >= '0' && ch <= '9';
      const uint8_t *e = dg ? ce_lut + LUT_ROWS_LARGE_DIGITS + (ch - '0') * 4 : rtab + (ch - 32) * 4;
      if (!e[2])
        ; /* (a space) */
      else if (sc == 1 && x >= cx0 && x + e[2] <= cx1 && !CE_SLOW)
        fast_glyph(e, dg ? ce_lut + LUT_ROWBITS_LARGE_DIGITS : rbits, gh, x, y, col, y0, ra, rb, sh);
      else {
        const uint8_t *b;
        const glyph_t *g = glyph(f, ch, &b);
        put_glyph(g, b, f, drop_col(f, ch, g), x, y, col, y0, ra, rb, cx0, cx1, sh);
      }
    }
    x += adv;
  }
}

/* ------------------------------------------------------------------ sprites */
/* editions: a level a 2 x 2 cell of the card, a frame every ED_DT_MS, each card a little behind the next */
static const uint8_t *ed_pattern(int ed, int x) {
  int f = (int)((ed_frame + (unsigned)(x / 12)) % ED_FRAMES);
  return ce_edpat + ((ed - 1) * ED_FRAMES + f) * (ED_CH * ED_CW / 2);
}

static uint8_t rowbuf[256] __attribute__((aligned(256))); /* (ce_scale_row looks up by the low byte) */

static void draw_sprite(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int id = it->a;
  const uint8_t *spr = ce_sprite(id);
  if (!spr) return;
  const art_sprite_t *s = &art_sprites[id];
  int w = it->w, h = it->h, sw = s->w, sh = s->h, fx = it->fx;
  int ya = it->y > ra ? it->y : ra, yb = it->y + h < rb ? it->y + h : rb;
  int xa = it->x > cx0 ? it->x : cx0, xb = it->x + w < cx1 ? it->x + w : cx1;
  if (xa >= xb || ya >= yb) return;
  int same = w == sw && h == sh, ed = fx & FX_EDMASK;
  const uint8_t *tab = 0, *dim = (fx & FX_DIM) ? ce_lut + LUT_SHADE + 14 * CE_NSTATIC : 0;
  const uint8_t *grey = (fx & FX_GREY) ? ce_lut + LUT_GREY : 0, *pat = ed ? ed_pattern(ed, it->x) : 0;
  const uint8_t *edl = ed ? ce_lut + LUT_EDLUT + (ed - 1) * ED_LEVELS * CE_NSTATIC : 0;
  if (fx & FX_SHADOW) tab = ce_shade_tab(11);
  int plain = !(fx & ~FX_SHADOW);
  /* the source row: a step of sh / h in 1/256 */
  unsigned ystep = same ? 256 : ((unsigned)sh << 8) / (unsigned)h, sy8 = (unsigned)(ya - it->y) * ystep;
  uint8_t *d = strip + (ya - y0) * SCR_W;
  for (int y = ya; y < yb; y++, sy8 += ystep, d += SCR_W) {
    int sy = (int)(sy8 >> 8);
    const uint8_t *rt = spr + sy + sy;
    const uint8_t *row = spr + (rt[0] | (unsigned)rt[1] << 8);
    if (same && plain) {
      if (it->x >= xa && it->x + sw <= xb && sw < 256 && !CE_SLOW) {
        /* (in eZ80 code: the whole sprite) */
        static ce_rle_t b;
        b.d = d + it->x, b.rt = rt, b.spr = spr, b.n = (uint8_t)(yb - y), b.sw = (uint8_t)sw, b.tab = tab;
        ce_rle(&b);
        return;
      }
      /* the runs straight in (or their shadow) */
      int x = it->x, end = it->x + sw;
      while (x < end) {
        x += row[0];
        int n = row[1];
        const uint8_t *p = row + 2;
        row = p + n;
        int a = x < xa ? xa : x, b = x + n > xb ? xb : x + n;
        if (a < b) {
          if (tab)
            ce_shade(d + a, tab, (unsigned)(b - a));
          else
            memcpy(d + a, p + (a - x), (size_t)(b - a));
        }
        x += n;
      }
      continue;
    }
    /* the row unpacked, then each pixel with its effects */
    memset(rowbuf, 255, (size_t)sw);
    for (int x = 0; x < sw;) {
      x += row[0];
      int n = row[1];
      memcpy(rowbuf + x, row + 2, (size_t)n);
      row += 2 + n;
      x += n;
    }
    const uint8_t *prow = pat ? pat + (sy >> 1 < ED_CH ? sy >> 1 : ED_CH - 1) * (ED_CW / 2) : 0;
    unsigned step = ((unsigned)sw << 8) / (unsigned)w, sx8 = (unsigned)(xa - it->x) * step;
    if (!(fx & ~FX_SHADOW) && xb - xa < 256 && !CE_SLOW) {
      ce_scale_row(d + xa, rowbuf, (unsigned)(xb - xa), sx8, step, tab);
      continue;
    }
    for (int x = xa; x < xb; x++, sx8 += step) {
      int sx = same ? x - it->x : (int)(sx8 >> 8);
      int v = rowbuf[sx];
      if (v == 255) continue;
      if (fx & FX_SHADOW) {
        d[x] = tab[d[x]];
        continue;
      }
      if (fx & FX_WHITE) {
        d[x] = CE_WHITE;
        continue;
      }
      if (edl) {
        int cx = sx >> 1 < ED_CW ? sx >> 1 : ED_CW - 1;
        uint8_t pv = prow[cx >> 1];
        int lv = cx & 1 ? pv >> 4 : pv & 15;
        v = edl[lv * CE_NSTATIC + v];
      }
      if (grey) v = grey[v];
      if (dim) v = dim[v];
      d[x] = (uint8_t)v;
    }
  }
}

/* ------------------------------------------------------------------ shapes */
static void draw_rsq(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  static const int16_t q[17] = {256, 255, 251, 245, 237, 226, 213, 198, 181, 162, 142, 121, 98, 74, 50, 25, 0};
  int i = (it->fx & 63) >> 2, co = q[i], si = q[16 - i];
  int hs = it->r * 256 + 128, R = it->w / 2, cx = it->x + R, cy = it->y + R;
  int ya = it->y > ra ? it->y : ra, yb = it->y + it->h < rb ? it->y + it->h : rb;
  int xa = it->x > cx0 ? it->x : cx0, xb = it->x + it->w < cx1 ? it->x + it->w : cx1;
  uint8_t *d = strip + (ya - y0) * SCR_W;
  for (int y = ya; y < yb; y++, d += SCR_W) {
    int dy = y - cy, dx = xa - cx;
    /* u and v along the row: a step of co and -si a pixel (no multiplying per pixel) */
    int u = dx * co + dy * si, v = dy * co - dx * si;
    for (int x = xa; x < xb; x++, u += co, v -= si)
      if ((u < 0 ? -u : u) <= hs && (v < 0 ? -v : v) <= hs) d[x] = (uint8_t)it->a;
  }
}

static void draw_rrect(item_t *it, int y0, int ra, int rb, int cx0, int cx1, int shade) {
  int r = it->r, x = it->x, w = it->w, h = it->h;
  int ya = it->y > ra ? it->y : ra, yb = it->y + h < rb ? it->y + h : rb;
  int xa = x > cx0 ? x : cx0, xb = x + w < cx1 ? x + w : cx1;
  uint8_t c = (uint8_t)it->a;
  const uint8_t *tab = shade ? ce_shade_tab(it->a) : 0;
  uint8_t *d = strip + ROW_OFF[ya - y0];
  const uint8_t *cr = cut[r];
  for (int j = ya - it->y, je = yb - it->y; j < je; j++, d += SCR_W) {
    /* the row less its corners' cut */
    int k = j < r ? cr[j] : j >= h - r ? cr[h - 1 - j] : 0, ma = x + k, mb = x + w - k;
    if (ma < xa) ma = xa;
    if (mb > xb) mb = xb;
    if (ma < mb) {
      if (shade)
        ce_shade(d + ma, tab, (unsigned)(mb - ma));
      else
        memset(d + ma, c, (size_t)(mb - ma));
    }
  }
}

static void draw_rect(item_t *it, int y0, int ra, int rb, int cx0, int cx1) {
  int ya = it->y > ra ? it->y : ra, yb = it->y + it->h < rb ? it->y + it->h : rb;
  int xa = it->x > cx0 ? it->x : cx0, xb = it->x + it->w < cx1 ? it->x + it->w : cx1;
  if (xa >= xb) return;
  uint8_t *d = strip + (ya - y0) * SCR_W + xa;
  for (int y = ya; y < yb; y++, d += SCR_W) memset(d, it->a, (size_t)(xb - xa));
}

/* ------------------------------------------------------------------ the frame */
static unsigned clip_hash[MAXCLIP];

#ifdef NP_HOST
/* (blit.S on the calculator) */
void ce_hash_items(unsigned *hs, const item_t *it, unsigned n, const unsigned *ch) {
  for (; n; n--, it++) {
    uint8_t b[18];
    memcpy(b, it, 16), b[16] = it->anim, b[17] = it->s0; /* (the calculator's layout) */
    unsigned h = ce_hash_words(ch[it->clip], b, 6);
    if (it->s) h = ce_hash_str(h, it->s);
    for (int s = it->s0; s <= it->s1; s++) hs[s] = (hs[s] * 33 + h) & 0xFFFFFF;
  }
}
#endif

#ifdef NP_HOST
/* how full the frame's lists get (tools/host.c prints it) */
int ce_max_items, ce_max_str;
long ce_strips_drawn, ce_pal_misses;
#endif

void g_end(void) {
#ifdef NP_HOST
  if (ce_nitems > ce_max_items) ce_max_items = ce_nitems;
  if (strpos > ce_max_str) ce_max_str = strpos;
#endif
  PROF(1); /* (the frame's list) */
#ifdef CE_PROF
  ce_prof[5]++;
#endif
  unsigned hs[NSTRIP], base[3] = {(unsigned)bg_swirl, (unsigned)bg_cover_x, bg_solid + (unsigned)ce_bg_ver * 256};
#ifdef NP_HOST
  for (int i = 0; i < 3; i++) base[i] &= 0xFFFFFF;
#endif
  unsigned b = ce_hash_words(0, base, 3);
  for (int s = 0; s < NSTRIP; s++) hs[s] = b + (unsigned)s;
  for (int i = 0; i < nclips; i++) clip_hash[i] = ce_hash(0x5A17u, clips[i], sizeof clips[i]);
  ce_hash_items(hs, items, (unsigned)ce_nitems, clip_hash);
  PROF(2); /* (the hashes) */
  uint8_t *d = strip;
  for (int s = 0, y0 = 0; s < NSTRIP; s++, y0 += SH) {
    if (drawn_once && hs[s] == strip_hash[s]) continue;
#ifdef CE_PROF
    ce_prof[4]++;
#endif
    strip_hash[s] = hs[s];
#ifdef NP_HOST
    ce_strips_drawn++;
    if (getenv("NP_STRIPS")) fprintf(stderr, "%u: strip %d\n", (unsigned)g_time, s);
#endif
    int y1 = y0 + SH;
    uint16_t dyn = 0;
    if (bg_swirl) {
      d = strip;
      for (int y = y0; y < y1; y++, d += SCR_W) {
        if (bg_cover_x > 0) memset(d, bg_solid, (size_t)(bg_cover_x < SCR_W ? bg_cover_x : SCR_W));
        if (bg_cover_x < SCR_W) ce_bg_row(d, y, bg_cover_x);
      }
    } else
      memset(strip, bg_solid, sizeof strip);
    PROF(13);
    for (int i = 0; i < ce_nitems; i++) {
      item_t *it = &items[i];
      if (s < it->s0 || s > it->s1) continue;
      const int16_t *c = clips[it->clip];
      int cy0 = y0 > c[1] ? y0 : c[1], cy1 = y1 < c[3] ? y1 : c[3];
      if (cy0 >= cy1) continue;
      if (it->type != I_SPRITE && it->type != I_SHADE && it->type != I_FLAME && it->a >= CE_DYN0 && it->a < CE_SWIRL0)
        dyn |= BIT16[it->a - CE_DYN0];
      PROF(15);
      switch (it->type) {
        case I_RECT: draw_rect(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_RRECT: draw_rrect(it, y0, cy0, cy1, c[0], c[2], 0); break;
        case I_SHADE: draw_rrect(it, y0, cy0, cy1, c[0], c[2], 1); break;
        case I_SPRITE: draw_sprite(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_TEXT: draw_text(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_RSQ: draw_rsq(it, y0, cy0, cy1, c[0], c[2]); break;
        case I_FLAME: ce_flame_rows(it->a & 1, it->x, it->y, it->w, it->h, it->b, strip, y0, cy0, cy1, c[0], c[2]); break;
      }
      PROF(6 + it->type);
    }
    strip_dyn[s] = dyn;
    PROF(15);
    memcpy(ce_vram + y0 * SCR_W, strip, sizeof strip);
    PROF(14);
  }
  drawn_once = 1;
  PROF(3); /* (the strips drawn: what the items left) */
}
