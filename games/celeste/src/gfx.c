/* The 320x180 view, drawn in strips.
 *
 * Draw calls during a frame only record commands (in call order, which is
 * depth order: the level renders entities sorted by depth). gfx_end() then
 * fills one 20-row strip at a time, runs every command that touches it, and
 * pushes the strip to the screen. */
#include <stdlib.h>
#include "celeste.h"
#include "text.h"

#define STRIP_H 15
#define NSTRIPS (VIEW_H / STRIP_H)
#define MAX_CMDS 400
#define MAX_AFFINE 56

/* the strip and the frame's draw commands: on main's stack (gfx_set_memory), the calculator gives apps 32 KB
 * of it apart from their RAM, and some calculator software gives apps less RAM than 23.2's 148928 bytes */
#ifdef HOST
static uint16_t strip_mem[VIEW_W * STRIP_H];
uint16_t *g_strip = strip_mem;
#else
uint16_t *g_strip;
#endif
int g_camx, g_camy;
static float camx, camy;
static bool hud, ui;   /* hud: screen coordinates; ui: the interface (drawn after the zoom) */
static uint16_t clear_color;
static bool letterbox_dirty = true;
static char top_label[24];   /* a line in the top bar (the room's code), redrawn when it changes */
static bool top_dirty;

enum { OP_TEX, OP_PART, OP_AFFINE, OP_RECT, OP_LINE, OP_PIXEL, OP_CUSTOM, OP_SCALED, OP_MASK, OP_TILES, OP_BANNER, OP_GRID };

typedef struct {
  uint8_t op, flags, alpha, extra;
  uint16_t tex, tint;
  int16_t x, y;
  uint8_t y0, y1;     /* view rows covered: [y0, y1) */
  uint16_t a, b;
} Cmd;

typedef struct {
  float px, py;       /* screen position of the origin */
  float ox, oy;       /* origin in frame pixels */
  float sx, sy, rot;
  int16_t cx0, cy0, cx1, cy1;   /* the part of the frame drawn (a subtexture) */
} Affine;

typedef struct { StripFn fn; void *ctx; } Custom;

#ifdef HOST
static Cmd cmds_mem[MAX_CMDS];
static Cmd *cmds = cmds_mem;
#else
static Cmd *cmds;
#endif
_Static_assert(VIEW_W * STRIP_H * sizeof(uint16_t) == GFX_STRIP_BYTES && MAX_CMDS * sizeof(Cmd) == GFX_CMD_BYTES,
               "celeste.h's GFX_STRIP_BYTES, GFX_CMD_BYTES");
void gfx_set_memory(void *strip, void *cmd) { g_strip = strip, cmds = cmd; }
static int ncmds;
static Affine affs[MAX_AFFINE];
static int naffs;
#define MAX_CUSTOMS 40
static Custom customs[MAX_CUSTOMS];
static int ncustoms;

void gfx_camera(float x, float y) {
  camx = x, camy = y;
  g_camx = (int)floorf(x);
  g_camy = (int)floorf(y);
}
void gfx_hud(bool on) { hud = ui = on; }
/* screen coordinates for the gameplay's own layers (stylegrounds): zoomed with the world */
void gfx_screen(bool on) { hud = on, ui = false; }
void gfx_clear(uint16_t c) { clear_color = c; }
void gfx_present_all(void) { letterbox_dirty = true; }
void gfx_top_label(const char *s) {
  if (!strncmp(s, top_label, sizeof top_label - 1)) return;
  strncpy(top_label, s, sizeof top_label - 1);
  top_dirty = true;
}

#ifdef HOST
#include <stdio.h>
static int dropped_cmds, dropped_affs;
#define AFF_DROP() (dropped_affs++)
#else
#define AFF_DROP() ((void)0)
#endif
void gfx_begin(void) {
#ifdef HOST
  extern void res_debug_working_set(void);
  res_debug_working_set();
  if (getenv("CMDDBG")) printf("cmds %d dropped %d affs %d dropped %d\n", ncmds, dropped_cmds, naffs, dropped_affs);
  dropped_cmds = 0;
  dropped_affs = 0;
#endif
  g_res_frame++;
  g_res_drawing = true;
  ncmds = 0;
  naffs = 0;
  ncustoms = 0;
  hud = false;
}

/* the interface keeps the last HUD_CMDS commands and HUD_AFFS affine ones: a busy room's world can't take them */
#define HUD_CMDS 48
#define HUD_AFFS 8
/* and the layers that must be drawn (the level's tiles, the stylegrounds in front: 1A's sunset, the snow) the last
 * KEEP_CMDS of the world's commands; they and the interface, the last KEEP_CUSTOMS custom layers */
#define KEEP_CMDS 12
#define KEEP_CUSTOMS 8
static bool keep;
void gfx_keep(bool on) { keep = on; }
static Cmd *add(uint8_t op, int y0, int y1) {
  if (y1 <= 0 || y0 >= VIEW_H) return NULL;
  if (ncmds >= (ui ? MAX_CMDS : MAX_CMDS - HUD_CMDS - (keep ? 0 : KEEP_CMDS))) {
#ifdef HOST
    dropped_cmds++;
#endif
    return NULL;
  }
  Cmd *c = &cmds[ncmds++];
  c->op = op;
  c->extra = ui ? 1 : 0;   /* the interface's: not zoomed */
  c->y0 = (uint8_t)(y0 < 0 ? 0 : y0);
  c->y1 = (uint8_t)(y1 > VIEW_H ? VIEW_H : y1);
  c->flags = 0;
  c->alpha = 255;
  c->tint = 0xFFFF;
  return c;
}

static inline int sx_(float x) { return (int)floorf(x + 0.5f) - (hud ? 0 : g_camx); }
static inline int sy_(float y) { return (int)floorf(y + 0.5f) - (hud ? 0 : g_camy); }

/* ---------------------------------------------------------------- color math */
uint16_t blend565(uint16_t d, uint16_t s, int a) {
  /* s premultiplied: s + d * (1 - a), a in 0..256 */
  uint32_t ia = 256 - a;
  uint32_t r = (s >> 11) + (((d >> 11) * ia) >> 8);
  uint32_t g = ((s >> 5) & 63) + ((((d >> 5) & 63) * ia) >> 8);
  uint32_t b = (s & 31) + (((d & 31) * ia) >> 8);
  if (r > 31) r = 31;
  if (g > 63) g = 63;
  if (b > 31) b = 31;
  return (uint16_t)(r << 11 | g << 5 | b);
}
/* saturating add of two RGB565 colors: the fields spread out with room to overflow */
static inline uint16_t add565(uint16_t d, uint16_t s) {
  uint32_t x = (d | (uint32_t)d << 16) & 0x07E0F81Fu, y = (s | (uint32_t)s << 16) & 0x07E0F81Fu;
  uint32_t t = x + y, ov = t & 0x08010020u;
  uint32_t lo = ov & 0x00010020u, hi = ov & 0x08000000u;
  t |= (lo - (lo >> 5)) | (hi - (hi >> 6));
  t &= 0x07E0F81Fu;
  return (uint16_t)(t | t >> 16);
}
uint16_t mul565(uint16_t c, uint16_t t) {
  uint32_t r = ((c >> 11) * ((t >> 11) + 1)) >> 5;
  uint32_t g = (((c >> 5) & 63) * (((t >> 5) & 63) + 1)) >> 6;
  uint32_t b = ((c & 31) * ((t & 31) + 1)) >> 5;
  return (uint16_t)(r << 11 | g << 5 | b);
}

/* the palette a command draws with: tint, alpha and blending folded in. Drawn as it is (no tint,
 * opaque, no blending: most draws), the palette is used from flash as it is, nothing computed. */
static uint16_t epal[256];
static uint16_t ealpha[256];   /* 0..256 */
static uint16_t ep_pal = 0xFFFF, ep_tint;
static uint8_t ep_alpha, ep_flags;
static bool ep_opaque;          /* every entry fully opaque, nothing to blend */
static const uint16_t *EC;      /* the colors (by index: 1 the palette's first) */
static const uint8_t *EA;       /* plain: the palette's own alphas (0..255), else NULL (ealpha) */

static void prep(uint16_t pal, uint16_t tint, uint8_t alpha, uint8_t flags) {
  flags &= GF_ADD | GF_SILHOUETTE | GF_ADDALPHA;
  if (pal == ep_pal && tint == ep_tint && alpha == ep_alpha && flags == ep_flags) return;
  ep_pal = pal, ep_tint = tint, ep_alpha = alpha, ep_flags = flags;
  int n;
  const uint16_t *c = pal_colors(pal, &n);
  const uint8_t *a = pal_alpha(pal);
  if (tint == 0xFFFF && alpha == 255 && !flags) {   /* as it is */
    EC = c - 1, EA = a - 1;
    ep_opaque = pal_opaque(pal);
    return;
  }
  EC = epal, EA = NULL;
#ifdef HOST
  { extern long g_dbg_prep, g_dbg_prep_n; g_dbg_prep++, g_dbg_prep_n += n; }
#endif
  ep_opaque = alpha == 255 && !(flags & GF_ADD);
  int ga = alpha + (alpha >> 7);
  if (flags & GF_SILHOUETTE) {
    for (int i = 0; i < n; i++) {
      int aa = a[i] + (a[i] >> 7);
      uint16_t col = aa >= 256 ? tint : scale565(tint, aa);
      if (ga != 256) col = scale565(col, ga), aa = (aa * ga) >> 8;
      epal[i + 1] = col;
      ealpha[i + 1] = (uint16_t)aa;
      if (aa != 256) ep_opaque = false;
    }
    return;
  }
  /* each channel through a table: the tint (mul565) and the fade (scale565) at once */
  uint8_t lr[32], lg[64], lb[32];
  int tr = tint >> 11, tg = tint >> 5 & 63, tb = tint & 31;
  for (int v = 0; v < 32; v++) {
    lr[v] = (uint8_t)((((v * (tr + 1)) >> 5) * ga) >> 8);
    lb[v] = (uint8_t)((((v * (tb + 1)) >> 5) * ga) >> 8);
  }
  for (int v = 0; v < 64; v++) lg[v] = (uint8_t)((((v * (tg + 1)) >> 6) * ga) >> 8);
  for (int i = 0; i < n; i++) {
    uint16_t col = c[i];
    int aa = a[i] + (a[i] >> 7);
    if (ga != 256) aa = (aa * ga) >> 8;
    epal[i + 1] = (uint16_t)(lr[col >> 11] << 11 | lg[col >> 5 & 63] << 5 | lb[col & 31]);
    if (flags & GF_ADDALPHA) {   /* the tinted color times ga * aa, rounded once (truncated twice, half of it was lost) */
      uint32_t k = (uint32_t)ga * (uint32_t)aa;
      epal[i + 1] = (uint16_t)(((((col >> 11) * (tr + 1)) >> 5) * k + 32768) >> 16 << 11 |
                               ((((col >> 5 & 63) * (tg + 1)) >> 6) * k + 32768) >> 16 << 5 | ((((col & 31) * (tb + 1)) >> 5) * k + 32768) >> 16);
    }
    ealpha[i + 1] = (uint16_t)aa;
    if (aa != 256) ep_opaque = false;
  }
}
static inline int alpha_of(uint8_t idx) {   /* 0..256 */
  if (EA) return EA[idx] + (EA[idx] >> 7);
  return ealpha[idx];
}

static inline void plot(uint16_t *d, uint8_t idx) {
  if (ep_opaque) {
    *d = EC[idx];
    return;
  }
  if (ep_flags & GF_ADD) {
    *d = add565(*d, EC[idx]);
    return;
  }
  int a = alpha_of(idx);
  *d = a >= 256 ? EC[idx] : blend565(*d, EC[idx], a);
}

/* n pixels from d on, one step (+1 or -1) apart: one index (run) or n indices (literals) */
static inline void span_run(uint16_t *d, int n, int step, uint8_t v) {
  uint16_t c = EC[v];
  if (ep_opaque) {
    if (step > 0)
      while (n--) *d++ = c;
    else
      while (n--) *d-- = c;
  } else if (ep_flags & GF_ADD) {
    for (; n--; d += step) *d = add565(*d, c);
  } else {
    int a = alpha_of(v);
    if (a >= 256)
      for (; n--; d += step) *d = c;
    else
      for (; n--; d += step) *d = blend565(*d, c, a);
  }
}
static inline void span_lit(uint16_t *d, int n, int step, const uint8_t *s) {
  if (ep_opaque) {
    if (step > 0)
      while (n--) *d++ = EC[*s++];
    else
      while (n--) *d-- = EC[*s++];
  } else if (ep_flags & GF_ADD) {
    for (; n--; d += step) *d = add565(*d, EC[*s++]);
  } else
    for (; n--; d += step, s++) {
      int a = alpha_of(*s);
      *d = a >= 256 ? EC[*s] : blend565(*d, EC[*s], a);
    }
}

/* ---------------------------------------------------------------- blits */
/* an RLE texture's row r: its ops [*begin, *end). TF_ROW8 rows are found by adding the lengths
 * up, from the last row asked for when it is the same texture; a length from ROW8_MAX up is a row met before
 * (in flash: tools/pack.py direct_rows), three bytes: its length and where it is in DIRECT */
static const uint8_t *row_ops(const Tex *t, int r, const uint8_t **end) {
  if (!(t->fmt & TF_ROW8)) {
    const uint16_t *offs = (const uint16_t *)(const void *)t->px;
    *end = t->px + offs[r + 1];
    return t->px + offs[r];
  }
  static const uint8_t *last_px, *last_p;
  static int last_r;
  static uint16_t last_gen;
  extern uint16_t g_res_gen;
  const uint8_t *lens = t->px, *p;
  int i;
  if (last_px == t->px && last_r <= r && last_gen == g_res_gen) p = last_p, i = last_r;
  else p = t->px + t->h, i = 0;
  for (; i < r; i++) p += lens[i] >= ROW8_MAX ? 3 : lens[i];
  last_px = t->px, last_r = r, last_p = p, last_gen = g_res_gen;
  if (lens[r] >= ROW8_MAX) {
    const uint8_t *o = section(SEC_DIRECT) + ((uint32_t)(lens[r] - ROW8_MAX) << 16 | p[1] | p[2] << 8);
    *end = o + p[0];
    return o;
  }
  *end = p + lens[r];
  return p;
}
/* TF_NIB: n literals (two a byte) as palette indices */
static uint8_t litbuf[128];
static const uint8_t *nib_lits(const Tex *t, const uint8_t *op, int n) {
  for (int i = 0; i < n; i++) litbuf[i] = t->map[(op[i >> 1] >> ((i & 1) * 4)) & 15];
  return litbuf;
}

/* a texture's stored pixels with their top-left at strip-relative (x, y) */
void blit_tex(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, uint8_t flags, uint16_t tint, uint8_t alpha) {
  prep(t->pal, tint, alpha, flags);
  int h = t->h, w = t->w;
  int r0 = sy0 - y, r1 = sy1 - y;    /* rows of the texture inside the strip */
  if (r0 < 0) r0 = 0;
  if (r1 > h) r1 = h;
  if (x >= VIEW_W || x + w <= 0) return;
  bool fx = flags & GF_FLIPX, fy = flags & GF_FLIPY, nib = t->fmt & TF_NIB;
  for (int r = r0; r < r1; r++) {
    int src = fy ? h - 1 - r : r;
    uint16_t *row = strip + (y + r - sy0) * VIEW_W;
    if (t->fmt == TF_RAW4) {
      for (int i = 0; i < w; i++) {
        uint32_t at = (uint32_t)src * w + i;
        uint8_t v = (t->px[at >> 1] >> ((at & 1) * 4)) & 15;
        int dx = fx ? x + w - 1 - i : x + i;
        if ((unsigned)dx >= VIEW_W || !v) continue;
        plot(row + dx, v);
      }
      continue;
    }
    const uint8_t *end, *op = row_ops(t, src, &end);
    /* i: texture column; pixel i lands at x0 + i * step */
    int x0 = fx ? x + w - 1 : x, step = fx ? -1 : 1;
    int i = 0;
    while (op < end) {
      uint8_t o = *op++;
      int n;
      if (o < 0x40) {
        i += o + 1;
        continue;
      }
      n = o < 0x80 ? o - 0x3F : o - 0x7F;
      /* the part of [i, i + n) whose pixels fall inside the view */
      int a = i, b = i + n;
      int da = x0 + a * step, db = x0 + (b - 1) * step;
      if (step > 0) {
        if (da < 0) a += -da;
        if (db >= VIEW_W) b -= db - (VIEW_W - 1);
      } else {
        if (da >= VIEW_W) a += da - (VIEW_W - 1);
        if (db < 0) b -= -db;
      }
      if (o < 0x80) {
        if (a < b) span_run(row + x0 + a * step, b - a, step, nib ? t->map[*op] : *op);
        op++;
      } else if (nib) {
        if (a < b) span_lit(row + x0 + a * step, b - a, step, nib_lits(t, op, n) + (a - i));
        op += (n + 1) >> 1;
      } else {
        if (a < b) span_lit(row + x0 + a * step, b - a, step, op + (a - i));
        op += n;
      }
      i += n;
    }
  }
}

/* one row of a texture as indices (for scaled and partial draws) */
void tex_row(const Tex *t, int r, uint8_t *out) {
  memset(out, 0, t->w);
  if (t->fmt == TF_RAW4) {
    for (int i = 0; i < t->w; i++) {
      uint32_t at = (uint32_t)r * t->w + i;
      out[i] = (t->px[at >> 1] >> ((at & 1) * 4)) & 15;
    }
    return;
  }
  bool nib = t->fmt & TF_NIB;
  const uint8_t *end, *op = row_ops(t, r, &end);
  int i = 0;
  while (op < end && i < t->w) {
    uint8_t o = *op++;
    if (o < 0x40) i += o + 1;
    else if (o < 0x80) {
      uint8_t v = nib ? t->map[*op] : *op;
      op++;
      for (int k = o - 0x3F; k > 0 && i < t->w; k--) out[i++] = v;
    } else if (nib) {
      int n = o - 0x7F;
      for (int k = 0; k < n; k++) {
        uint8_t v = t->map[(op[k >> 1] >> ((k & 1) * 4)) & 15];
        if (i < t->w) out[i++] = v;
      }
      op += (n + 1) >> 1;
    } else
      for (int k = o - 0x7F; k > 0; k--) {
        uint8_t v = *op++;
        if (i < t->w) out[i++] = v;
      }
  }
}

static uint8_t rowbuf[1024];
static const uint8_t *turned_src;   /* the texture blit_turned left in rowbuf (32 a row) */
static uint16_t turned_gen;

/* sub-rect (sx, sy, w, h) of the frame, at strip-relative (x, y) */
void blit_part_ex(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, int sx, int sy, int w, int h,
                      uint8_t flags, uint16_t tint, uint8_t alpha) {
  prep(t->pal, tint, alpha, flags);
  bool fx = flags & GF_FLIPX, fy = flags & GF_FLIPY;
  int k = t->scale > 1 ? t->scale : 1;   /* (a texture stored k times smaller: its frame's pixels k times bigger) */
  for (int r = 0; r < h; r++) {
    int vy = y + r;
    if (vy < sy0 || vy >= sy1) continue;
    int fr = (sy + (fy ? h - 1 - r : r)) / k - t->oy;    /* stored row */
    if (fr < 0 || fr >= t->h) continue;
    uint16_t *row = strip + (vy - sy0) * VIEW_W;
    turned_src = NULL, tex_row(t, fr, rowbuf);
    for (int i = 0; i < w; i++) {
      int c = (sx + (fx ? w - 1 - i : i)) / k - t->ox;
      int dx = x + i;
      if ((unsigned)dx >= VIEW_W || c < 0 || c >= t->w || !rowbuf[c]) continue;
      plot(row + dx, rowbuf[c]);
    }
  }
}

/* Decal.Banner: the frame cut in slices of 1 or 2 rows (GetSubtexture), slice i of n moved sideways by
 * sin(sineTimer * WaveSpeed + i * 0.05) * w * amplitude + w * offset, its weight w = (easeDown ? i / n : 1 - i / n) * wind;
 * ph: sineTimer * WaveSpeed (a turn in 65536), prm: the kind (0 grass, 1 rags, 2 flowers) | offset < 0 << 2 | wind * 64 << 8 */
static void blit_banner(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, uint8_t flags, uint16_t tint,
                        uint8_t alpha, uint16_t ph, uint16_t prm) {
  static const struct { uint8_t slice, ease_down; float amp, off; } K[3] = {{1, 0, 2, -2}, {2, 1, 3.5f, 0}, {1, 0, 2, 2}};
  int k = prm & 3, slice = K[k].slice, n = (t->fh + slice - 1) / slice, h = t->h, w = t->w;
  float wind = (prm >> 8) / 64.f, off = (prm & 4) ? -K[k].off : K[k].off, phase = ph * (2 * PI_F / 65536);
  bool fx = flags & GF_FLIPX, fy = flags & GF_FLIPY;
  prep(t->pal, tint, alpha, flags);
  for (int r = sy0 - y > 0 ? sy0 - y : 0; r < h && y + r < sy1; r++) {
    int src = fy ? h - 1 - r : r, i = (src + t->oy) / slice;
    float wt = (K[k].ease_down ? (float)i / n : 1 - (float)i / n) * wind;
    int dx = x + (int)floorf(sinf(phase + i * 0.05f) * wt * K[k].amp + wt * off + 0.5f);
    uint16_t *row = strip + (y + r - sy0) * VIEW_W;
    turned_src = NULL, tex_row(t, src, rowbuf);
    for (int c = 0; c < w; c++) {
      int X = dx + (fx ? w - 1 - c : c);
      if ((unsigned)X < VIEW_W && rowbuf[c]) plot(row + X, rowbuf[c]);
    }
  }
}

/* t's frame (square, 32 at most) turned q quarter turns clockwise around its center, the frame's top-left at
 * strip-relative (x, y) */
void blit_turned(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, int q, uint8_t flags, uint16_t tint) {
  uint8_t *px = rowbuf;   /* the texture, 32 a row */
  int fw = t->fw, w = t->w, h = t->h;
  if (fw > 32 || w > 32 || h > 32 || y >= sy1 || y + fw <= sy0 || x >= VIEW_W || x + fw <= 0) return;
  prep(t->pal, tint, 255, flags);
  extern uint16_t g_res_gen;
  if (turned_src != t->px || turned_gen != g_res_gen) {   /* (the same one again: still there) */
    for (int r = 0; r < h; r++) tex_row(t, r, px + r * 32);
    turned_src = t->px, turned_gen = g_res_gen;
  }
  for (int dy = 0; dy < fw; dy++) {
    int vy = y + dy;
    if (vy < sy0 || vy >= sy1) continue;
    uint16_t *row = strip + (vy - sy0) * VIEW_W;
    for (int dx = 0; dx < fw; dx++) {
      int vx = x + dx, a, b;
      if ((unsigned)vx >= VIEW_W) continue;
      switch (q & 3) {
        case 0: a = dx, b = dy; break;
        case 1: a = dy, b = fw - 1 - dx; break;
        case 2: a = fw - 1 - dx, b = fw - 1 - dy; break;
        default: a = fw - 1 - dy, b = dx; break;
      }
      a -= t->ox, b -= t->oy;
      if (a < 0 || b < 0 || a >= w || b >= h || !px[b * 32 + a]) continue;
      plot(row + vx, px[b * 32 + a]);
    }
  }
}

/* a grid of 8x8 tiles of t (per nibble: tx | ty << 2, 12 for none) with its top-left at screen (x, y),
 * scaled by (scx, scy) around screen (gx, gy) (CassetteBlock's images, CrumblePlatform's) */
void blit_cells(uint16_t *strip, int sy0, int sy1, const Tex *t, float x, float y, int ncx, int ncy, const uint8_t *cells,
                float gx, float gy, float scx, float scy, uint16_t tint, uint8_t alpha) {
  if (!alpha) return;
  prep(t->pal, tint, alpha, 0);
  float l = gx + (x - gx) * scx, r = gx + (x + ncx * 8 - gx) * scx;
  float top = gy + (y - gy) * scy, bot = gy + (y + ncy * 8 - gy) * scy;
  int r0 = (int)floorf(top), r1 = (int)ceilf(bot), c0 = (int)floorf(l), c1 = (int)ceilf(r);
  if (r0 < sy0) r0 = sy0;
  if (r1 > sy1) r1 = sy1;
  if (c0 < 0) c0 = 0;
  if (c1 > VIEW_W) c1 = VIEW_W;
  for (int vy = r0; vy < r1; vy++) {
    float ly = (vy + 0.5f - gy) / scy + gy - y;
    if (ly < 0 || ly >= ncy * 8) continue;
    int cy = (int)ly >> 3, py = (int)ly & 7, cached = -1;
    uint16_t *row = strip + (vy - sy0) * VIEW_W;
    for (int vx = c0; vx < c1; vx++) {
      float lx = (vx + 0.5f - gx) / scx + gx - x;
      if (lx < 0 || lx >= ncx * 8) continue;
      int cx = (int)lx >> 3, i = cy * ncx + cx, tile = cells[i >> 1] >> ((i & 1) * 4) & 15;
      if (tile == 12) continue;
      int sr = (tile >> 2) * 8 + py - t->oy, sc = (tile & 3) * 8 + ((int)lx & 7) - t->ox;
      if (sr < 0 || sr >= t->h || sc < 0 || sc >= t->w) continue;
      if (sr != cached) turned_src = NULL, tex_row(t, sr, rowbuf), cached = sr;
      if (rowbuf[sc]) plot(row + vx, rowbuf[sc]);
    }
  }
}

/* a block's tiles: a grid of w x h 8x8 tiles of t (quads, ty * 8 + tx, 255 for none) at strip-relative (x, y) */
static void blit_tiles(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, const uint8_t *q, int w, int h,
                       uint16_t tint, uint8_t alpha) {
  prep(t->pal, tint, alpha, 0);
  int r0 = y > sy0 ? y : sy0, r1 = y + h * 8 < sy1 ? y + h * 8 : sy1;
  for (int vy = r0; vy < r1; vy++) {
    const uint8_t *qr = q + ((vy - y) >> 3) * w;
    uint16_t *row = strip + (vy - sy0) * VIEW_W;
    int cached = -1;
    for (int i = 0; i < w; i++) {
      int k = qr[i], dx = x + i * 8;
      if (k == 255 || dx >= VIEW_W || dx + 8 <= 0) continue;
      int sr = (k >> 3) * 8 + ((vy - y) & 7) - t->oy;
      if (sr < 0 || sr >= t->h) continue;
      if (sr != cached) turned_src = NULL, tex_row(t, sr, rowbuf), cached = sr;
      for (int px = 0; px < 8; px++) {
        int c = (k & 7) * 8 + px - t->ox;
        if ((unsigned)(dx + px) < VIEW_W && c >= 0 && c < t->w && rowbuf[c]) plot(row + dx + px, rowbuf[c]);
      }
    }
  }
}

/* gfx_nine's cells (tw 0; th 1: hollow) or gfx_tiled's repeated part (tw x th), w x h at strip-relative (x, y): pixel i of
 * a span comes from column (row) i % tw of the frame, or from the first, middle or last cell's */
static inline int grid_src(int i, int n, int tw) {
  if (tw) return i % tw;
  int cell = i >> 3;
  return (cell == 0 ? 0 : cell == n - 1 ? 16 : 8) + (i & 7);
}
static void blit_grid(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, int w, int h, int tw, int th, uint16_t tint,
                      uint8_t alpha) {
  prep(t->pal, tint, alpha, 0);
  int nw = w >> 3, nh = h >> 3, r0 = y > sy0 ? y : sy0, r1 = y + h < sy1 ? y + h : sy1, c0 = x > 0 ? x : 0, c1 = x + w < VIEW_W ? x + w : VIEW_W;
  for (int vy = r0; vy < r1; vy++) {
    int sr = grid_src(vy - y, nh, tw ? th : 0), mid_row = !tw && th && sr >= 8 && sr < 16;   /* (hollow: no middle cells) */
    if ((sr -= t->oy) < 0 || sr >= t->h) continue;
    turned_src = NULL, tex_row(t, sr, rowbuf);
    uint16_t *row = strip + (vy - sy0) * VIEW_W;
    for (int vx = c0; vx < c1; vx++) {
      int c = grid_src(vx - x, nw, tw);
      if (mid_row && c >= 8 && c < 16) continue;
      if ((c -= t->ox) >= 0 && c < t->w && rowbuf[c]) plot(row + vx, rowbuf[c]);
    }
  }
}

/* a palette's first 16 colors as drawn (tint folded in) and their alphas (0..256) */
void pal_lut(uint16_t pal, uint16_t tint, uint16_t *col, uint16_t *alpha) {
  int n;
  const uint16_t *c = pal_colors(pal, &n);
  const uint8_t *a = pal_alpha(pal);
  if (n > 15) n = 15;
  for (int i = 0; i < n; i++) {
    col[i + 1] = tint != 0xFFFF ? mul565(c[i], tint) : c[i];
    alpha[i + 1] = (uint16_t)(a[i] + (a[i] >> 7));
  }
}

/* 4-bit textures (TF_RAW4) rotated and scaled around screen points, the first opaque one on top, in
 * one go (DustGraphic): within the box [x0, x1) x [y0, y1); where nothing is but a neighbour is, the
 * outline's color (outline: NULL for none), or what is covered marked in mask (blit_edges draws it) */
void blit_layers(uint16_t *strip, int sy0, int sy1, const Layer *l, int n, int x0, int y0, int x1, int y1,
                 uint16_t (*outline)(int x, int y, void *ctx), void *ctx, uint32_t *mask) {
  if (x0 < 0) x0 = 0;
  if (x1 > VIEW_W) x1 = VIEW_W;
  if (x1 - x0 > 32) x1 = x0 + 32;
  bool edges = outline || mask;
  int r0 = y0 > sy0 - edges ? y0 : sy0 - edges, r1 = y1 < sy1 + edges ? y1 : sy1 + edges;
  if (r1 - r0 > 48) r1 = r0 + 48;
  if (r0 >= r1 || x0 >= x1 || !n || n > 9) return;
#ifdef HOST
  extern long g_dbg_layers, g_dbg_pixels;
  g_dbg_layers++, g_dbg_pixels += (long)(r1 - r0) * (x1 - x0);
#endif
  uint32_t cover[48], all = x1 - x0 == 32 ? 0xFFFFFFFFu : (1u << (x1 - x0)) - 1;
  int32_t du[9], dv[9];
  float rad2[9];
  for (int k = 0; k < n; k++) {   /* 16.16 texture coordinates along the row; the circle around each */
    du[k] = (int32_t)(l[k].cs * l[k].inv * 65536), dv[k] = (int32_t)(-l[k].sn * l[k].inv * 65536);
    float hx = fmaxf(l[k].ox - l[k].t->ox, l[k].t->ox + l[k].t->w - l[k].ox), hy = fmaxf(l[k].oy - l[k].t->oy, l[k].t->oy + l[k].t->h - l[k].oy);
    rad2[k] = (hx * hx + hy * hy) / (l[k].inv * l[k].inv);
  }
  for (int y = r0; y < r1; y++) {
    float py = y + 0.5f;
    bool draw = y >= sy0 && y < sy1;
    uint16_t *row = strip + (y - sy0) * VIEW_W;
    uint32_t filled = 0, bits = 0;
    if (mask) {   /* covered by ones drawn before: they stay on top */
      const uint32_t *m = mask + (y - sy0 + 1) * 10;
      int s0 = x0 & 31;
      uint64_t two = (uint64_t)m[x0 >> 5] | (x0 >> 5 < 9 ? (uint64_t)m[(x0 >> 5) + 1] << 32 : 0);
      filled = (uint32_t)(two >> s0) & all;
    }
    for (int k = 0; k < n && filled != all; k++) {   /* the top one first: each only where nothing above is */
      float dy = py - l[k].cy, r2 = rad2[k] - dy * dy;
      if (r2 <= 0) continue;
      float half = sqrtf(r2);
      int xa = (int)floorf(l[k].cx - half), xb = (int)ceilf(l[k].cx + half);
      if (xa < x0) xa = x0;
      if (xb > x1) xb = x1;
      if (xa >= xb) continue;
      uint32_t span = (xb - x0 == 32 ? 0xFFFFFFFFu : (1u << (xb - x0)) - 1) & ~((1u << (xa - x0)) - 1);
      uint32_t todo = span & ~filled;
      if (!todo) continue;
      float dx = x0 + 0.5f - l[k].cx;
      int32_t u0 = (int32_t)(((dx * l[k].cs + dy * l[k].sn) * l[k].inv + l[k].ox - l[k].t->ox) * 65536);
      int32_t v0 = (int32_t)(((-dx * l[k].sn + dy * l[k].cs) * l[k].inv + l[k].oy - l[k].t->oy) * 65536);
      const uint8_t *px = l[k].t->px;
      uint32_t w = l[k].t->w, h = l[k].t->h;
      while (todo) {
        int i = __builtin_ctz(todo);
        todo &= todo - 1;
        uint32_t iu = (uint32_t)((u0 + du[k] * i) >> 16), iv = (uint32_t)((v0 + dv[k] * i) >> 16);
        if (iu >= w || iv >= h) continue;
        uint32_t at = iv * w + iu;
        int c = (px[at >> 1] >> ((at & 1) * 4)) & 15;
        if (!c) continue;
        filled |= 1u << i, bits |= 1u << i;
        if (draw) {
          int a = l[k].la[c];
          uint16_t *d = row + x0 + i;
          *d = a >= 256 ? l[k].lc[c] : blend565(*d, l[k].lc[c], a);
        }
      }
    }
    cover[y - r0] = bits;
    if (mask && bits) {   /* rows sy0 - 1 .. sy1 of the screen, 10 words each */
      uint32_t *m = mask + (y - sy0 + 1) * 10;
      int s0 = x0 & 31;
      m[x0 >> 5] |= bits << s0;
      if (s0 && x0 >> 5 < 9) m[(x0 >> 5) + 1] |= bits >> (32 - s0);
    }
  }
  if (!outline || mask) return;
  for (int y = r0 > sy0 ? r0 : sy0; y < r1 && y < sy1; y++) {
    uint16_t *row = strip + (y - sy0) * VIEW_W;
    uint32_t c = cover[y - r0], up = y > r0 ? cover[y - r0 - 1] : 0, dn = y + 1 < r1 ? cover[y + 1 - r0] : 0;
    uint32_t e = (up | dn | c << 1 | c >> 1) & ~c & all;
    while (e) {
      int i = __builtin_ctz(e);
      e &= e - 1;
      row[x0 + i] = outline(x0 + i, y, ctx);
    }
  }
}
/* a 4-bit texture (TF_RAW4) as it is, its top-left at screen (x, y), in pal_lut's colors */
void blit_raw4(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, const uint16_t *lc, const uint16_t *la) {
  x += t->ox, y += t->oy;
  for (int r = 0; r < t->h; r++) {
    int vy = y + r;
    if (vy < sy0 || vy >= sy1) continue;
    uint16_t *row = strip + (vy - sy0) * VIEW_W;
    for (int i = 0; i < t->w; i++) {
      int vx = x + i;
      uint32_t at = (uint32_t)r * t->w + (uint32_t)i;
      int c = (t->px[at >> 1] >> ((at & 1) * 4)) & 15;
      if (!c || (unsigned)vx >= VIEW_W) continue;
      row[vx] = la[c] >= 256 ? lc[c] : blend565(row[vx], lc[c], la[c]);
    }
  }
}
/* the outline of what blit_layers marked (mask: rows sy0 - 1 .. sy1, 10 words each) */
void blit_edges(uint16_t *strip, int sy0, int sy1, const uint32_t *mask, uint16_t (*outline)(int x, int y, void *ctx), void *ctx) {
  for (int y = sy0; y < sy1; y++) {
    const uint32_t *up = mask + (y - sy0) * 10, *c = up + 10, *dn = c + 10;
    uint16_t *row = strip + (y - sy0) * VIEW_W;
    for (int w = 0; w < 10; w++) {
      uint32_t left = w ? c[w - 1] >> 31 : 0, right = w < 9 ? c[w + 1] << 31 : 0;
      uint32_t e = (up[w] | dn[w] | c[w] << 1 | left | c[w] >> 1 | right) & ~c[w];
      while (e) {
        int b = __builtin_ctz(e);
        e &= e - 1;
        row[w * 32 + b] = outline(w * 32 + b, y, ctx);
      }
    }
  }
}

/* a texture stored k times smaller, drawn k times bigger at (x, y) */
static void blit_scaled(uint16_t *strip, int sy0, int sy1, const Tex *t, int x, int y, uint8_t flags, uint16_t tint, uint8_t alpha) {
  prep(t->pal, tint, alpha, flags);
  int k = t->scale, w = t->w * k, h = t->h * k;
  int r0 = sy0 - y, r1 = sy1 - y;
  if (r0 < 0) r0 = 0;
  if (r1 > h) r1 = h;
  int cached = -1;
  for (int r = r0; r < r1; r++) {
    int src = r / k;
    if (src != cached) turned_src = NULL, tex_row(t, src, rowbuf), cached = src;
    uint16_t *row = strip + (y + r - sy0) * VIEW_W;
    int i0 = x < 0 ? -x : 0, i1 = x + w > VIEW_W ? VIEW_W - x : w;
    for (int i = i0; i < i1;) {
      int s = i / k;
      uint8_t v = rowbuf[s];
      int s2 = s + 1;
      while (s2 < t->w && rowbuf[s2] == v) s2++;   /* equal neighbours make one span */
      int e = s2 * k;
      if (e > i1) e = i1;
      if (v) span_run(row + x + i, e - i, 1, v);
      i = e;
    }
  }
}

static void blit_affine(uint16_t *strip, int sy0, int sy1, const Tex *t, const Affine *a, uint16_t tint, uint8_t alpha, uint8_t flags) {
  float cs = cosf(a->rot), sn = sinf(a->rot);
  if (a->sx == 0 || a->sy == 0) return;
  float isx = 1.f / a->sx, isy = 1.f / a->sy;
  /* bounds of the transformed stored rect */
  /* the stored rect within the drawn part */
  float l = fmaxf((float)t->ox, a->cx0), r = fminf((float)(t->ox + t->w), a->cx1);
  float tp = fmaxf((float)t->oy, a->cy0), bt = fminf((float)(t->oy + t->h), a->cy1);
  if (l >= r || tp >= bt) return;
  float cx[4] = {l, r, l, r};
  float cy[4] = {tp, tp, bt, bt};
  float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
  for (int i = 0; i < 4; i++) {
    float lx = (cx[i] - a->ox) * a->sx, ly = (cy[i] - a->oy) * a->sy;
    float X = a->px + lx * cs - ly * sn, Y = a->py + lx * sn + ly * cs;
    if (X < minx) minx = X;
    if (X > maxx) maxx = X;
    if (Y < miny) miny = Y;
    if (Y > maxy) maxy = Y;
  }
  int x0 = (int)floorf(minx), x1 = (int)ceilf(maxx), y0 = (int)floorf(miny), y1 = (int)ceilf(maxy);
  if (x0 < 0) x0 = 0;
  if (x1 > VIEW_W) x1 = VIEW_W;
  if (y0 < sy0) y0 = sy0;
  if (y1 > sy1) y1 = sy1;
  if (x0 >= x1 || y0 >= y1) return;
  prep(t->pal, tint, alpha, flags);   /* (only for what is drawn: the colors cost more than the bounds) */
  int cached = -1;
  for (int Y = y0; Y < y1; Y++) {
    uint16_t *row = strip + (Y - sy0) * VIEW_W;
    for (int X = x0; X < x1; X++) {
      float dx = X + 0.5f - a->px, dy = Y + 0.5f - a->py;
      float lx = (dx * cs + dy * sn) * isx + a->ox, ly = (-dx * sn + dy * cs) * isy + a->oy;
      if (lx < a->cx0 || lx >= a->cx1 || ly < a->cy0 || ly >= a->cy1) continue;
      int u = (int)floorf(lx) - t->ox, v = (int)floorf(ly) - t->oy;
      if ((unsigned)u >= t->w || (unsigned)v >= t->h) continue;
      if (v != cached) {
        turned_src = NULL, tex_row(t, v, rowbuf);
        cached = v;
      }
      if (rowbuf[u]) plot(row + X, rowbuf[u]);
    }
  }
}

static void fill(uint16_t *strip, int sy0, int sy1, int x, int y, int w, int h, uint16_t c, uint8_t alpha) {
  int x0 = x < 0 ? 0 : x, x1 = x + w > VIEW_W ? VIEW_W : x + w;
  int y0 = y < sy0 ? sy0 : y, y1 = y + h > sy1 ? sy1 : y + h;
  if (x0 >= x1 || y0 >= y1) return;
  int a = alpha + (alpha >> 7);
  uint16_t pc = a >= 256 ? c : scale565(c, a);
  for (int r = y0; r < y1; r++) {
    uint16_t *d = strip + (r - sy0) * VIEW_W + x0;
    if (a >= 256)
      for (int i = x0; i < x1; i++) *d++ = c;
    else
      for (int i = x0; i < x1; i++, d++) *d = blend565(*d, pc, a);
  }
}

static void line(uint16_t *strip, int sy0, int sy1, int x0, int y0, int x1, int y1, uint16_t c, uint8_t alpha) {
  int dx = abs(x1 - x0), dy = -abs(y1 - y0), sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1, err = dx + dy;
  int a = alpha + (alpha >> 7);
  uint16_t pc = a >= 256 ? c : scale565(c, a);
  for (int guard = 0; guard < 4096; guard++) {
    if (y0 >= sy0 && y0 < sy1 && (unsigned)x0 < VIEW_W) {
      uint16_t *d = strip + (y0 - sy0) * VIEW_W + x0;
      *d = a >= 256 ? c : blend565(*d, pc, a);
    }
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) err += dy, x0 += sx;
    if (e2 <= dx) err += dx, y0 += sy;
  }
}

/* a line between world points (as gfx_line places them) drawn into the strip: for layers of many lines, one command */
void blit_line(uint16_t *strip, int sy0, int sy1, float x0, float y0, float x1, float y1, uint16_t col, uint8_t alpha) {
  int X0 = (int)floorf(x0 + 0.5f) - g_camx, Y0 = (int)floorf(y0 + 0.5f) - g_camy;
  int X1 = (int)floorf(x1 + 0.5f) - g_camx, Y1 = (int)floorf(y1 + 0.5f) - g_camy;
  if ((Y0 < Y1 ? Y1 : Y0) < sy0 || (Y0 < Y1 ? Y0 : Y1) >= sy1) return;
  line(strip, sy0, sy1, X0, Y0, X1, Y1, col, alpha);
}

/* ---------------------------------------------------------------- recording */
void gfx_tex(uint16_t tex, float x, float y, uint8_t flags, uint16_t tint, uint8_t alpha) {
  Tex t;
  if (!tex_get(tex, &t) || !alpha) return;
  if (t.scale > 1) {
    int k = t.scale, X = sx_(x) + t.ox * k, Y = sy_(y) + t.oy * k;
    Cmd *c = add(OP_SCALED, Y, Y + t.h * k);
    if (!c) return;
    c->tex = tex, c->x = (int16_t)X, c->y = (int16_t)Y, c->flags = flags, c->tint = tint, c->alpha = alpha;
    return;
  }
  int X = sx_(x) + (flags & GF_FLIPX ? t.fw - t.ox - t.w : t.ox);
  int Y = sy_(y) + (flags & GF_FLIPY ? t.fh - t.oy - t.h : t.oy);
  Cmd *c = add(OP_TEX, Y, Y + t.h);
  if (!c) return;
  if (X >= VIEW_W || X + t.w <= 0) {
    ncmds--;
    return;
  }
  c->tex = tex, c->x = (int16_t)X, c->y = (int16_t)Y, c->flags = flags, c->tint = tint, c->alpha = alpha;
}

void gfx_tex_ex(uint16_t tex, float x, float y, float ox, float oy, float sx, float sy, float rot, uint16_t tint, uint8_t alpha, uint8_t flags) {
  if (!alpha) return;
  Tex t;
  if (!tex_get(tex, &t)) return;
  int k = t.scale > 1 ? t.scale : 1;   /* stored k times smaller: the origin is in drawn pixels */
  if (rot == 0 && (sx == 1 || sx == -1) && (sy == 1 || sy == -1)) {
    uint8_t f = flags;
    float px = x - (sx > 0 ? ox : t.fw * k - ox), py = y - (sy > 0 ? oy : t.fh * k - oy);
    if (sx < 0) f ^= GF_FLIPX;
    if (sy < 0) f ^= GF_FLIPY;
    gfx_tex(tex, px, py, f, tint, alpha);
    return;
  }
  float px = floorf(x + 0.5f) - (hud ? 0 : g_camx), py = floorf(y + 0.5f) - (hud ? 0 : g_camy);
  float r = (fabsf(sx) + fabsf(sy)) * k * (t.fw + t.fh + 2), rc = r + (fabsf(sx) + fabsf(sy)) * (fabsf(ox) + fabsf(oy));
  if (px - rc >= VIEW_W || px + rc <= 0 || py + rc <= 0 || py - rc >= VIEW_H) return;   /* off the view: no slot used */
  if (naffs >= (ui ? MAX_AFFINE : MAX_AFFINE - HUD_AFFS)) {
    AFF_DROP();
    return;
  }
  Affine *a = &affs[naffs];
  a->px = px, a->py = py;
  a->ox = ox / k, a->oy = oy / k, a->sx = sx * k, a->sy = sy * k, a->rot = rot;
  a->cx0 = a->cy0 = -32768, a->cx1 = a->cy1 = 32767;
  if (flags & GF_FLIPX) a->sx = -a->sx;
  if (flags & GF_FLIPY) a->sy = -a->sy;
  Cmd *c = add(OP_AFFINE, (int)(a->py - r), (int)(a->py + r) + 1);
  if (!c) return;
  naffs++;
  c->tex = tex, c->a = (uint16_t)(naffs - 1), c->tint = tint, c->alpha = alpha, c->flags = flags;
}

/* a subtexture (frame rect sx, sy, w, h) drawn like MTexture.Draw with an origin (in the part), scale and rotation */
/* a swaying decal (blit_banner): its frame's top-left at (x, y) */
void gfx_banner(uint16_t tex, float x, float y, uint8_t flags, uint16_t ph, uint16_t prm) {
  Tex t;
  if (!tex_get(tex, &t)) return;
  if (t.scale > 1) {
    gfx_tex(tex, x, y, flags, 0xFFFF, 255);
    return;
  }
  int X = sx_(x) + (flags & GF_FLIPX ? t.fw - t.ox - t.w : t.ox);
  int Y = sy_(y) + (flags & GF_FLIPY ? t.fh - t.oy - t.h : t.oy);
  if (X - 16 >= VIEW_W || X + t.w + 16 <= 0) return;   /* (it sways 12 pixels at most) */
  Cmd *c = add(OP_BANNER, Y, Y + t.h);
  if (!c) return;
  c->tex = tex, c->x = (int16_t)X, c->y = (int16_t)Y, c->flags = flags, c->a = ph, c->b = prm;
}

void gfx_tex_part_ex(uint16_t tex, float x, float y, int sx, int sy, int w, int h, float ox, float oy, float scx, float scy,
                     float rot, uint16_t tint, uint8_t alpha, uint8_t flags) {
  Tex t;
  if (!alpha || !tex_get(tex, &t)) return;
  if (rot == 0 && scx == 1 && scy == 1 && t.scale <= 1 && ox == (int)ox && oy == (int)oy && (sx | sy | w | h) < 256) {
    /* drawn as it is: a plain part (its corner and size fit its bytes), the same pixels (the affine slots are few: 40
     * for the world) */
    gfx_tex_part(tex, x - ox, y - oy, sx, sy, w, h, flags, tint, alpha);
    return;
  }
  float px = floorf(x + 0.5f) - (hud ? 0 : g_camx), py = floorf(y + 0.5f) - (hud ? 0 : g_camy);
  float r = (fabsf(scx) + fabsf(scy)) * (w + h + 2), rc = r + (fabsf(scx) + fabsf(scy)) * (fabsf(ox) + fabsf(oy));
  if (px - rc >= VIEW_W || px + rc <= 0 || py + rc <= 0 || py - rc >= VIEW_H) return;   /* off the view: no slot used */
  if (naffs >= (ui ? MAX_AFFINE : MAX_AFFINE - HUD_AFFS)) {
    AFF_DROP();
    return;
  }
  Affine *a = &affs[naffs];
  a->px = px, a->py = py;
  a->ox = sx + ox, a->oy = sy + oy, a->sx = scx, a->sy = scy, a->rot = rot;
  if (flags & GF_FLIPX) a->sx = -a->sx, a->ox = sx + w - ox;
  if (flags & GF_FLIPY) a->sy = -a->sy, a->oy = sy + h - oy;
  a->cx0 = (int16_t)sx, a->cy0 = (int16_t)sy, a->cx1 = (int16_t)(sx + w), a->cy1 = (int16_t)(sy + h);
  Cmd *c = add(OP_AFFINE, (int)(a->py - r), (int)(a->py + r) + 1);
  if (!c) return;
  naffs++;
  c->tex = tex, c->a = (uint16_t)(naffs - 1), c->tint = tint, c->alpha = alpha, c->flags = flags & ~(GF_FLIPX | GF_FLIPY);
}

void gfx_tex_part(uint16_t tex, float x, float y, int sx, int sy, int w, int h, uint8_t flags, uint16_t tint, uint8_t alpha) {
  Tex t;
  if (!tex_get(tex, &t) || !alpha) return;
  int X = sx_(x), Y = sy_(y);
  if (X >= VIEW_W || X + w <= 0) return;
  Cmd *c = add(OP_PART, Y, Y + h);
  if (!c) return;
  c->tex = tex, c->x = (int16_t)X, c->y = (int16_t)Y, c->flags = flags, c->tint = tint, c->alpha = alpha;
  c->a = (uint16_t)(sx | sy << 8), c->b = (uint16_t)(w | h << 8);
}

/* a block's w x h tiles of tex (q: TileQ in g_tile_pool), its top-left at (x, y): one command */
void gfx_tiles(uint16_t tex, const uint8_t *q, int w, int h, float x, float y, uint16_t tint, uint8_t alpha) {
  Tex t;
  if (!alpha || w > 255 || h > 127 || q < g_tile_pool || q + w * h > g_tile_pool + TILE_POOL || !tex_get(tex, &t)) return;
  int X = sx_(x), Y = sy_(y);
  if (X >= VIEW_W || X + w * 8 <= 0) return;
  Cmd *c = add(OP_TILES, Y, Y + h * 8);
  if (!c) return;
  c->tex = tex, c->x = (int16_t)X, c->y = (int16_t)Y, c->tint = tint, c->alpha = alpha;
  c->flags = (uint8_t)w, c->extra |= (uint8_t)(h << 1), c->a = (uint16_t)(q - g_tile_pool);
}

/* gfx_nine and gfx_tiled: one OP_GRID command */
static void grid(uint16_t tex, float x, float y, int w, int h, int tw, int th, uint16_t tint, uint8_t alpha) {
  Tex t;
  if (!tex_get(tex, &t) || !alpha || w <= 0 || h <= 0) return;   /* (got first, in view or not, as gfx_tex_part does) */
  int X = sx_(x), Y = sy_(y);
  if (X >= VIEW_W || X + w <= 0) return;
  Cmd *c = add(OP_GRID, Y, Y + h);
  if (!c) return;
  c->tex = tex, c->x = (int16_t)X, c->y = (int16_t)Y, c->tint = tint, c->alpha = alpha;
  c->flags = (uint8_t)tw, c->extra |= (uint8_t)(th << 1), c->a = (uint16_t)w, c->b = (uint16_t)h;
}
/* Celeste's nine-slice blocks in one command (what would be a part for each 8x8 cell): a 24x24 texture's corners, edges
 * and middle over w x h (multiples of 8) at (x, y), a single column (row) taking the first one's cells; hollow: no middle */
void gfx_nine(uint16_t tex, float x, float y, int w, int h, bool hollow, uint16_t tint, uint8_t alpha) {
  if (!((w | h) & 7)) grid(tex, x, y, w, h, 0, hollow, tint, alpha);
}
/* the part (0, 0, tw, th) of a texture's frame repeated over w x h at (x, y), the last ones cut: one command */
void gfx_tiled(uint16_t tex, float x, float y, int w, int h, int tw, int th, uint16_t tint, uint8_t alpha) {
  if (tw > 0 && th > 0 && tw < 256 && th < 128) grid(tex, x, y, w, h, tw, th, tint, alpha);
}

/* gfx_tex_ex (world coordinates) drawn straight into a strip, for StripFn layers: the same pixels as its command, without
 * using up any, or an affine slot (the texture got when the layer is recorded: nothing loads while strips are drawn) */
void blit_tex_ex(uint16_t *strip, int sy0, int sy1, uint16_t tex, float x, float y, float ox, float oy, float sx, float sy,
                 float rot, uint16_t tint, uint8_t alpha, uint8_t flags) {
  Tex t;
  if (!alpha || !tex_get(tex, &t)) return;
  int k = t.scale > 1 ? t.scale : 1;   /* (textures stored smaller: affine here, as close) */
  if (k == 1 && rot == 0 && (sx == 1 || sx == -1) && (sy == 1 || sy == -1)) {   /* gfx_tex */
    uint8_t f = flags;
    float px = x - (sx > 0 ? ox : t.fw - ox), py = y - (sy > 0 ? oy : t.fh - oy);
    if (sx < 0) f ^= GF_FLIPX;
    if (sy < 0) f ^= GF_FLIPY;
    int X = (int)floorf(px + 0.5f) - g_camx + (f & GF_FLIPX ? t.fw - t.ox - t.w : t.ox);
    int Y = (int)floorf(py + 0.5f) - g_camy + (f & GF_FLIPY ? t.fh - t.oy - t.h : t.oy);
    blit_tex(strip, sy0, sy1, &t, X, Y, f, tint, alpha);
    return;
  }
  Affine a;
  a.px = floorf(x + 0.5f) - g_camx, a.py = floorf(y + 0.5f) - g_camy;
  a.ox = ox / k, a.oy = oy / k, a.sx = sx * k, a.sy = sy * k, a.rot = rot;
  a.cx0 = a.cy0 = -32768, a.cx1 = a.cy1 = 32767;
  if (flags & GF_FLIPX) a.sx = -a.sx;
  if (flags & GF_FLIPY) a.sy = -a.sy;
  blit_affine(strip, sy0, sy1, &t, &a, tint, alpha, flags);
}

/* gfx_rect (world coordinates) drawn straight into a strip, for StripFn layers */
void blit_rect(uint16_t *strip, int sy0, int sy1, float x, float y, float w, float h, uint16_t col, uint8_t alpha) {
  fill(strip, sy0, sy1, (int)floorf(x + 0.5f) - g_camx, (int)floorf(y + 0.5f) - g_camy, (int)floorf(w + 0.5f),
       (int)floorf(h + 0.5f), col, alpha);
}
void gfx_rect(float x, float y, float w, float h, uint16_t col, uint8_t alpha) {
  if (!alpha) return;
  int X = sx_(x), Y = sy_(y), W_ = (int)floorf(w + 0.5f), H_ = (int)floorf(h + 0.5f);
  if (W_ <= 0 || H_ <= 0 || X >= VIEW_W || X + W_ <= 0) return;
  Cmd *c = add(OP_RECT, Y, Y + H_);
  if (!c) return;
  c->x = (int16_t)X, c->y = (int16_t)Y, c->a = (uint16_t)W_, c->b = (uint16_t)H_, c->tint = col, c->alpha = alpha;
}

void gfx_hollow_rect(float x, float y, float w, float h, uint16_t c, uint8_t alpha) {
  gfx_rect(x, y, w, 1, c, alpha);
  gfx_rect(x, y + h - 1, w, 1, c, alpha);
  gfx_rect(x, y + 1, 1, h - 2, c, alpha);
  gfx_rect(x + w - 1, y + 1, 1, h - 2, c, alpha);
}

void gfx_line(float x0, float y0, float x1, float y1, uint16_t col, uint8_t alpha) {
  int X0 = sx_(x0), Y0 = sy_(y0), X1 = sx_(x1), Y1 = sy_(y1);
  Cmd *c = add(OP_LINE, Y0 < Y1 ? Y0 : Y1, (Y0 > Y1 ? Y0 : Y1) + 1);
  if (!c) return;
  c->x = (int16_t)X0, c->y = (int16_t)Y0, c->a = (uint16_t)(int16_t)X1, c->b = (uint16_t)(int16_t)Y1, c->tint = col, c->alpha = alpha;
}

void gfx_pixel(float x, float y, uint16_t col, uint8_t alpha) { gfx_rect(x, y, 1, 1, col, alpha); }

void gfx_circle(float x, float y, float r, uint16_t col, uint8_t alpha, int res) {
  float px = x + r, py = y;
  for (int i = 1; i <= res; i++) {
    float a = i * 2 * PI_F / res;
    float nx = x + cosf(a) * r, ny = y + sinf(a) * r;
    gfx_line(px, py, nx, ny, col, alpha);
    px = nx, py = ny;
  }
}

/* a 4-bit alpha mask (fonts) in data.bin: w x h, rows padded to bytes, drawn in one color */
void gfx_mask4(const uint8_t *bits, int w, int h, float x, float y, uint16_t col, uint8_t alpha) {
  int X = sx_(x), Y = sy_(y);
  if (X >= VIEW_W || X + w <= 0 || !alpha) return;
  Cmd *c = add(OP_MASK, Y, Y + h);
  if (!c) return;
  uint32_t off = (uint32_t)(bits - cel_bin);
  c->x = (int16_t)X, c->y = (int16_t)Y, c->tint = col, c->alpha = alpha;
  c->a = (uint16_t)(off & 0xFFFF), c->b = (uint16_t)(off >> 16);
  c->tex = (uint16_t)(w | h << 8);
}
static void blit_mask(uint16_t *strip, int sy0, int sy1, const uint8_t *bits, int w, int h, int x, int y, uint16_t col, uint8_t alpha) {
  int stride = (w + 1) / 2, ga = alpha + (alpha >> 7);
  for (int r = 0; r < h; r++) {
    int Y = y + r;
    if (Y < sy0 || Y >= sy1) continue;
    const uint8_t *p = bits + r * stride;
    uint16_t *row = strip + (Y - sy0) * VIEW_W;
    for (int i = 0; i < w; i++) {
      int X = x + i;
      if ((unsigned)X >= VIEW_W) continue;
      int v = (p[i >> 1] >> ((i & 1) * 4)) & 15;
      if (!v) continue;
      int a = (v * 17 * ga) >> 8;
      row[X] = a >= 255 ? col : blend565(row[X], scale565(col, a + 1), a + 1);
    }
  }
}

void gfx_custom(StripFn fn, void *ctx, int y0, int y1) {
  if (ncustoms >= (ui || keep ? MAX_CUSTOMS : MAX_CUSTOMS - KEEP_CUSTOMS)) {
#ifdef HOST
    dropped_cmds++;
#endif
    return;
  }
  Cmd *c = add(OP_CUSTOM, y0, y1);
  if (!c) return;
  customs[ncustoms] = (Custom){fn, ctx};
  c->a = (uint16_t)ncustoms++;
}

/* ---------------------------------------------------------------- presenting */
/* the commands crossing rows [y0, y1) drawn into the strip (which holds those rows); pass: 0 the world's,
 * 1 the interface's, 2 all */
static void replay(int y0, int y1, int pass) {
  for (int i = 0; i < ncmds; i++) {
    Cmd *c = &cmds[i];
    if (c->y1 <= y0 || c->y0 >= y1) continue;
    if (pass != 2 && (c->extra & 1) != pass) continue;
    Tex t;
    switch (c->op) {
      case OP_TEX:
        if (tex_get(c->tex, &t)) blit_tex(g_strip, y0, y1, &t, c->x, c->y, c->flags, c->tint, c->alpha);
        break;
      case OP_PART:
        if (tex_get(c->tex, &t))
          blit_part_ex(g_strip, y0, y1, &t, c->x, c->y, c->a & 0xFF, c->a >> 8, c->b & 0xFF, c->b >> 8, c->flags, c->tint, c->alpha);
        break;
      case OP_MASK:
        blit_mask(g_strip, y0, y1, cel_bin + (c->a | (uint32_t)c->b << 16), c->tex & 0xFF, c->tex >> 8, c->x, c->y, c->tint, c->alpha);
        break;
      case OP_SCALED:
        if (tex_get(c->tex, &t)) blit_scaled(g_strip, y0, y1, &t, c->x, c->y, c->flags, c->tint, c->alpha);
        break;
      case OP_AFFINE:
        if (tex_get(c->tex, &t)) blit_affine(g_strip, y0, y1, &t, &affs[c->a], c->tint, c->alpha, c->flags);
        break;
      case OP_RECT: fill(g_strip, y0, y1, c->x, c->y, c->a, c->b, c->tint, c->alpha); break;
      case OP_LINE: line(g_strip, y0, y1, c->x, c->y, (int16_t)c->a, (int16_t)c->b, c->tint, c->alpha); break;
      case OP_CUSTOM: customs[c->a].fn(g_strip, y0, y1, customs[c->a].ctx); break;
      case OP_GRID:
        if (tex_get(c->tex, &t)) blit_grid(g_strip, y0, y1, &t, c->x, c->y, c->a, c->b, c->flags, c->extra >> 1, c->tint, c->alpha);
        break;
      case OP_TILES:
        if (tex_get(c->tex, &t))
          blit_tiles(g_strip, y0, y1, &t, c->x, c->y, g_tile_pool + c->a, c->flags, c->extra >> 1, c->tint, c->alpha);
        break;
      case OP_BANNER:
        if (tex_get(c->tex, &t)) blit_banner(g_strip, y0, y1, &t, c->x, c->y, c->flags, c->tint, c->alpha, c->a, c->b);
        break;
    }
  }
}

/* Level.Zoom: the gameplay drawn larger around a point of the screen */
static float zoom = 1, zoom_fx, zoom_fy;
void gfx_zoom(float z, float fx, float fy) { zoom = z, zoom_fx = fx, zoom_fy = fy; }

/* the world's rows [sa, sb) are in the strip: make them rows [y0, y1) seen through the zoom, in place
 * (each pixel comes from nearer the focus, so working away from it never reads one already written) */
static void zoom_strip(int y0, int y1, int sa) {
  float inv = 1 / zoom;
  int n = y1 - y0;
  int16_t src_x[VIEW_W];
  for (int x = 0; x < VIEW_W; x++) {
    int sx = (int)floorf(zoom_fx + (x + 0.5f - zoom_fx) * inv);
    src_x[x] = (int16_t)(sx < 0 ? 0 : sx >= VIEW_W ? VIEW_W - 1 : sx);
  }
  int fx = (int)zoom_fx;
  if (fx < 0) fx = 0;
  if (fx > VIEW_W) fx = VIEW_W;
  for (int r = n - 1; r >= 0; r--) {
    int sy = (int)floorf(zoom_fy + (y0 + r + 0.5f - zoom_fy) * inv) - sa;
    if (sy < 0) sy = 0;
    if (sy > STRIP_H - 1) sy = STRIP_H - 1;
    const uint16_t *src = g_strip + sy * VIEW_W;
    uint16_t *dst = g_strip + r * VIEW_W;
    for (int x = 0; x < fx; x++) dst[x] = src[src_x[x]];
    for (int x = VIEW_W - 1; x >= fx; x--) dst[x] = src[src_x[x]];
  }
}

void gfx_end(void) {
  if (letterbox_dirty) {
    plat_fill(0, 0, SCREEN_W, VIEW_Y, 0);
    plat_fill(0, VIEW_Y + VIEW_H, SCREEN_W, SCREEN_H - VIEW_Y - VIEW_H, 0);
    letterbox_dirty = false;
    top_dirty = top_label[0] != 0;
  }
  ep_pal = 0xFFFF;
  g_res_can_load = false;   /* the strip is the LZMA ring: nothing loads while strips are drawn */
  if (zoom > 1.001f) {
    /* 14 rows at a time: the world rows they need (at most 15) first, then enlarged, then the interface */
    for (int y0 = 0; y0 < VIEW_H; y0 += 14) {
      int y1 = y0 + 14 > VIEW_H ? VIEW_H : y0 + 14;
      int sa = (int)floorf(zoom_fy + (y0 + 0.5f - zoom_fy) / zoom);
      int sb = (int)floorf(zoom_fy + (y1 - 0.5f - zoom_fy) / zoom) + 1;
      if (sb > sa + STRIP_H) sb = sa + STRIP_H;
      for (int i = 0; i < VIEW_W * STRIP_H; i++) g_strip[i] = clear_color;
      replay(sa, sb, 0);
      zoom_strip(y0, y1, sa);
      replay(y0, y1, 1);
      plat_push(0, VIEW_Y + y0, VIEW_W, y1 - y0, g_strip);
    }
  } else
    for (int s = 0; s < NSTRIPS; s++) {
      int y0 = s * STRIP_H, y1 = y0 + STRIP_H;
      for (int i = 0; i < VIEW_W * STRIP_H; i++) g_strip[i] = clear_color;
      replay(y0, y1, 2);
      plat_push(0, VIEW_Y + y0, VIEW_W, STRIP_H, g_strip);
    }
  if (top_dirty) {   /* (after the strips: the strip is free again) */
    for (int i = 0; i < VIEW_W * STRIP_H; i++) g_strip[i] = 0;
    text_into(g_strip, 0, STRIP_H, top_label, 4 * 6, STRIP_H * 3, 0, 0.5f, 0.7f, rgb(0xA0A0A0), 255);
    plat_push(0, (VIEW_Y - STRIP_H) / 2, VIEW_W, STRIP_H, g_strip);
    top_dirty = false;
  }
  g_res_can_load = true;
  g_res_drawing = false;
}
