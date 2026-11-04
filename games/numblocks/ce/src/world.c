/* The world around the player: a window of 32 x 32 columns and 32 levels that follows them (nb.h), filled from
 * the terrain (gen.c) and the edit log, with Minecraft's light: sky light (15 where the sky is seen straight up,
 * then one less a block, and less through leaves and water) and block light (torches, lava, lit furnaces),
 * spread through what lets light through. */
#include "nb.h"

uint8_t *wblk, *wlight;
uint32_t wexp[WS * WS];
uint8_t wtop[WS * WS];
CCol wcol[WS * WS];
/* a column's top and filler blocks and depth, packed (tf) */
static const uint8_t TOPS[6] = {B_GRASS, B_SAND, B_GRAVEL, B_DIRT, B_GRASS_SNOWED, B_STONE};
static const uint8_t FILLS[3] = {B_DIRT, B_SAND, B_GRAVEL};
static void col_pack(CCol *k, const Column *c) {
  int t = 0, f = 0;
  while (t < 5 && TOPS[t] != c->top) t++;
  while (f < 2 && FILLS[f] != c->filler) f++;
  k->h = c->h, k->biome = c->biome, k->tf = (uint8_t)(t | f << 3 | (c->depth - 3) << 5);
}
static void col_unpack(const CCol *k, Column *c) {
  c->h = k->h, c->biome = k->biome, c->top = TOPS[k->tf & 7], c->filler = FILLS[k->tf >> 3 & 3];
  c->depth = (uint8_t)(3 + (k->tf >> 5));
}
int win_x, win_z, win_y;
uint32_t world_seed;
Edit *edits;
unsigned n_edits, max_edits;
/* where the window is going: world_step() moves it there a row (or a level) at a time */
static int want_x, want_z, want_y;

/* ---------------------------------------------------------------- blocks */
int world_get(int x, int y, int z) {
  if (y < 0) return B_BEDROCK;
  if (y >= WORLD_H) return B_AIR;
  if ((unsigned)(x - win_x) >= WS || (unsigned)(z - win_z) >= WS) return B_AIR;
  if (y < win_y) return B_STONE;
  if (y >= win_y + WH) return B_AIR;
  return wblk[WI(x, y, z)];
}

int world_light(int x, int y, int z) {
  if (!in_win(x, y, z)) return y >= win_y + WH ? 15 : 0;
  return wlight[WI(x, y, z)];
}

int world_top(int x, int z) {
  for (int y = win_y + WH - 1; y >= win_y; y--) {
    int b = world_get(x, y, z);
    if (b != B_AIR && !(blk_flags[b] & BF_LIQUID) && blk_model[b] != M_CROSS) return y + 1;
  }
  return win_y;
}

/* ---------------------------------------------------------------- the edit log, sorted by x, z, y */
static int edit_cmp(int x, int z, int y, const Edit *e) {
  if (x != e->x) return x < e->x ? -1 : 1;
  if (z != e->z) return z < e->z ? -1 : 1;
  if (y != e->y) return y < e->y ? -1 : 1;
  return 0;
}
static unsigned edit_find(int x, int y, int z, bool *found) {
  unsigned lo = 0, hi = n_edits;
  while (lo < hi) {
    unsigned mid = (lo + hi) / 2;
    int c = edit_cmp(x, z, y, &edits[mid]);
    if (!c) {
      *found = true;
      return mid;
    }
    if (c < 0) hi = mid;
    else lo = mid + 1;
  }
  *found = false;
  return lo;
}
bool edits_room(unsigned n) {
  if (n_edits + n <= max_edits) return true;
  unsigned want = (n_edits + n + 255) & ~255u;
  Edit *e = plat_grow(edits, want * sizeof(Edit));
  if (!e) return false;
  edits = e, max_edits = want;
  return true;
}
static void edit_log(int x, int y, int z, int b) {
  bool found;
  unsigned i = edit_find(x, y, z, &found);
  /* back to what the terrain had there: no need to keep it (the terrain, not trees: those are kept) */
  if (found) {
    edits[i].b = (uint8_t)b;
    return;
  }
  if (!edits_room(1)) return;
  memmove(&edits[i + 1], &edits[i], (n_edits - i) * sizeof(Edit));
  edits[i] = (Edit){(int16_t)x, (int16_t)z, (uint8_t)y, (uint8_t)b};
  n_edits++;
}

/* ---------------------------------------------------------------- light */
/* Cell indices taken apart a byte at a time (on the eZ80 a mask or a shift of an int is a call): lo = (z & 7) << 5
 * | x, hi = y << 2 | z >> 3; and a column's cells a level at a time, 1024 apart, round the 32 slots. */
typedef union { uint16_t w; uint8_t b[2]; } U16;
static const uint16_t YOFF[32] = {0,     1024,  2048,  3072,  4096,  5120,  6144,  7168,  8192,  9216,  10240,
                                  11264, 12288, 13312, 14336, 15360, 16384, 17408, 18432, 19456, 20480, 21504,
                                  22528, 23552, 24576, 25600, 26624, 27648, 28672, 29696, 30720, 31744};
uint8_t w_esx, w_eex, w_esz, w_eez, w_esy, w_eey;   /* the window's first and last slots (rs.s too) */
/* y & 31 for y 0..63, and the slot after and before one (masks of bytes are widened to ints, and calls) */
static const uint8_t MOD32[64] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21,
                                  22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
                                  12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
#define NEXT32(s) ((s) == 31 ? 0 : (uint8_t)((s) + 1))
#define PREV32(s) ((s) ? (uint8_t)((s) - 1) : 31)
static void edges(void) {
  w_esx = MOD32[win_x & 31], w_eex = PREV32(w_esx);
  w_esz = MOD32[win_z & 31], w_eez = PREV32(w_esz);
  w_esy = MOD32[win_y & 31], w_eey = PREV32(w_esy);
}
/* the y slot's level */
static inline int wy_of(uint8_t slot) { return win_y + (uint8_t)((uint8_t)(slot - (uint8_t)win_y) & 31); }

/* the queues: in the 3D view's picture (light is never spread while it is drawn) */
uint16_t *w_lq;           /* cells to spread light from (rs.s too) */
unsigned w_lq_h, w_lq_t;
#define lq w_lq
#define lq_h w_lq_h
#define lq_t w_lq_t
#define LQ_N 5000
static uint16_t *dq;      /* cells to darken, and the light they had */
static uint8_t *dql;
static unsigned dq_n;
#define DQ_N 3000

static void lq_push(uint16_t i) {
  unsigned t = lq_t + 1 == LQ_N ? 0 : lq_t + 1;
  if (t == lq_h) return;   /* full: (never, at the window's size; it would only leave some light out) */
  lq[lq_t] = i;
  lq_t = t;
}

/* the neighbours of cell i inside the window (edges() first): nbr[0..nbr_n), -Y +Y -Z +Z -X +X (rs.s on the CE) */
uint16_t w_nbr[6];
uint8_t w_nbr_n;
#define nbr w_nbr
#define nbr_n w_nbr_n
#ifdef __ez80__
void w_neighbours(uint16_t i);
#define neighbours w_neighbours
#else
static void neighbours(uint16_t i) {
  uint8_t lo = (uint8_t)i, hi = (uint8_t)(i >> 8), n = 0;
  uint8_t x = lo & 31, z = (uint8_t)((hi & 3) << 3 | lo >> 5), y = hi >> 2;
  if (y != w_esy) nbr[n++] = (uint16_t)((((hi - 4) & 0x7C) | (hi & 3)) << 8 | lo);
  if (y != w_eey) nbr[n++] = (uint16_t)((((hi + 4) & 0x7C) | (hi & 3)) << 8 | lo);
  if (z != w_esz) {
    uint8_t z2 = (z - 1) & 31;
    nbr[n++] = (uint16_t)(((hi & 0x7C) | z2 >> 3) << 8 | (z2 & 7) << 5 | x);
  }
  if (z != w_eez) {
    uint8_t z2 = (z + 1) & 31;
    nbr[n++] = (uint16_t)(((hi & 0x7C) | z2 >> 3) << 8 | (z2 & 7) << 5 | x);
  }
  if (x != w_esx) nbr[n++] = (uint16_t)(hi << 8 | (lo & 0xE0) | ((x - 1) & 31));
  if (x != w_eex) nbr[n++] = (uint16_t)(hi << 8 | (lo & 0xE0) | ((x + 1) & 31));
  nbr_n = n;
}
#endif

/* spreads both lights from the queued cells */
static const uint8_t OP16[16] = {0, 16, 32, 48, 64, 80, 96, 112, 128, 144, 160, 176, 192, 208, 224, 240};
#if defined(__ez80__) && !defined(NB_WORLD_C)
void w_light_spread(void);
static void light_spread(void) {
  edges();
  w_light_spread();
}
#else
static void light_spread(void) {
  edges();
  while (lq_h != lq_t) {
    uint16_t i = lq[lq_h];
    lq_h = lq_h + 1 == LQ_N ? 0 : lq_h + 1;
    /* (block light kept in the high nibble: no shifting, a call on the eZ80) */
    uint8_t l = wlight[i], s = l & 15, b = l & 0xF0;
    if (s <= 1 && b <= 0x10) continue;
    neighbours(i);
    for (uint8_t k = 0; k < nbr_n; k++) {
      uint16_t n = nbr[k];
      uint8_t op = blk_opacity[wblk[n]];
      if (op >= 15) continue;
      if (!op) op = 1;
      uint8_t op16 = OP16[op], nl = wlight[n], ns = nl & 15, nb = nl & 0xF0, ch = 0;
      if (s > op && (uint8_t)(s - op) > ns) ns = (uint8_t)(s - op), ch = 1;
      if (b > op16 && (uint8_t)(b - op16) > nb) nb = (uint8_t)(b - op16), ch = 1;
      if (ch) {
        wlight[n] = (uint8_t)(ns | nb);
        lq_push(n);
      }
    }
  }
}
#endif

/* takes away the light that came through the queued cells (dql: the level they had, 16: block light); the cells
 * at its edge that keep their own light are queued to spread it back */
static void light_darken(void) {
  edges();
  unsigned h = 0;
  while (h < dq_n) {
    uint16_t i = dq[h];
    uint8_t lv = dql[h], blk = lv & 16;
    lv &= 15;
    h++;
    neighbours(i);
    for (uint8_t k = 0; k < nbr_n; k++) {
      uint16_t n = nbr[k];
      uint8_t w = wlight[n], nl = blk ? w >> 4 : w & 15;
      if (!nl) continue;
      bool keeps;
      if (blk) keeps = blk_light[wblk[n]] >= nl;
      else {
        /* (sky light straight down: kept) */
        static U16 u;
        u.w = n;
        uint8_t slot = u.b[1] >> 2;
        u.b[1] &= 3;
        keeps = wtop[u.w] <= wy_of(slot);
      }
      if (nl < lv && !keeps) {
        wlight[n] = blk ? w & 15 : w & 0xF0;
        if (dq_n < DQ_N) dq[dq_n] = n, dql[dq_n] = (uint8_t)(nl | blk), dq_n++;
      } else lq_push(n);
    }
  }
  dq_n = 0;
}
static void dark_push(uint16_t i, bool blk) {
  uint8_t w = wlight[i], lv = blk ? w >> 4 : w & 15;
  if (!lv) return;
  wlight[i] = blk ? w & 15 : w & 0xF0;
  if (dq_n < DQ_N) dq[dq_n] = i, dql[dq_n] = (uint8_t)(lv | (blk ? 16 : 0)), dq_n++;
}

/* the sky's straight-down light of a column: wtop is the first level under everything that stops it */
static int column_top(int x, int z) {
  int c = CI(x, z);
  if (wcol[c].h >= win_y + WH) return win_y + WH;
  uint8_t ys = MOD32[win_y + WH - 1];
  for (int y = win_y + WH - 1; y >= win_y; y--) {
    if (blk_opacity[wblk[YOFF[ys] + c]]) return y + 1;
    ys = PREV32(ys);
  }
  return win_y;
}

/* ---------------------------------------------------------------- faces to draw */
static const uint8_t BIT32[32] = {1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128,
                                   1, 2, 4, 8, 16, 32, 64, 128, 1, 2, 4, 8, 16, 32, 64, 128};
static const uint8_t BYTE8[32] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1,
                                  2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3};
/* a column's cells with a face to draw (past the window: below it, as if solid; above, air): a face shows unless
 * what is next to it is opaque, the same block (not a plant), or both are liquids (rs.s on the CE) */
#ifdef __ez80__
extern const uint8_t *xp_p;
extern uint8_t *xp_out;
extern int xp_ox0, xp_ox1, xp_oz0, xp_oz1;
extern uint8_t xp_ys, xp_sides, xp_k0, xp_k1;
void w_expose(void);
#else
static const uint8_t *xp_p;
static uint8_t *xp_out;
static int xp_ox0, xp_ox1, xp_oz0, xp_oz1;
static uint8_t xp_ys, xp_sides, xp_k0, xp_k1 = 32;
static void w_expose(void) {
  static uint8_t m[4];
  memcpy(m, xp_out, 4);
  const uint8_t *p = xp_p;
  uint8_t ys = xp_ys;
  for (uint8_t k = xp_k0; k < xp_k1; k++) {
    m[BYTE8[ys]] &= (uint8_t)~BIT32[ys];
    uint8_t b = *p;
    const uint8_t *up = ys == 31 ? p - 31 * 1024 : p + 1024;
    if (b != B_AIR) {
      uint8_t md = blk_model[b];
      bool e = md == M_CROSS || md == M_TORCH || k == 31;
      if (!e) {
        uint8_t liq = blk_flags[b] & BF_LIQUID, n, fn;
#define SHOWS(q) (n = (q), fn = blk_flags[n], !(fn & BF_OPAQUE) && n != b && !(liq && (fn & BF_LIQUID)))
        e = SHOWS(*up) || (k && SHOWS(ys ? p[-1024] : p[31 * 1024])) || ((xp_sides & 4) && SHOWS(p[xp_oz0])) ||
            ((xp_sides & 8) && SHOWS(p[xp_oz1])) || ((xp_sides & 1) && SHOWS(p[xp_ox0])) ||
            ((xp_sides & 2) && SHOWS(p[xp_ox1]));
#undef SHOWS
      }
      if (e) m[BYTE8[ys]] |= BIT32[ys];
    }
    p = up;
    ys = NEXT32(ys);
  }
  memcpy(xp_out, m, 4);
}
#endif
/* (levels [k0, k1) up from the window's bottom, the others kept) */
static void expose_col(int x, int z, uint8_t k0, uint8_t k1) {
  if ((unsigned)(x - win_x) >= WS || (unsigned)(z - win_z) >= WS) return;
  xp_k0 = k0, xp_k1 = k1;
  int c = CI(x, z);
  uint8_t sx = MOD32[x - win_x + (win_x & 31)], sz = MOD32[z - win_z + (win_z & 31)];
  xp_sides = (uint8_t)((x > win_x ? 1 : 0) | (x < win_x + WS - 1 ? 2 : 0) | (z > win_z ? 4 : 0) |
                       (z < win_z + WS - 1 ? 8 : 0));
  xp_ox0 = sx ? -1 : 31, xp_ox1 = sx != 31 ? 1 : -31, xp_oz0 = sz ? -32 : 31 * 32, xp_oz1 = sz != 31 ? 32 : -31 * 32;
  xp_ys = MOD32[win_y + k0];
  xp_p = wblk + YOFF[xp_ys] + c;
  xp_out = (uint8_t *)&wexp[c];
  w_expose();
}
/* the columns around a cell that changed */
static void expose_around(int x, int y, int z) {
  /* (its level and those above and below) */
  int k = y - win_y;
  uint8_t k0 = (uint8_t)(k > 0 ? k - 1 : 0), k1 = (uint8_t)(k < WH - 1 ? k + 2 : WH);
  expose_col(x, z, k0, k1);
  k0 = (uint8_t)k, k1 = (uint8_t)(k + 1);
  expose_col(x - 1, z, k0, k1);
  expose_col(x + 1, z, k0, k1);
  expose_col(x, z - 1, k0, k1);
  expose_col(x, z + 1, k0, k1);
}

/* ---------------------------------------------------------------- filling the window */
static void put_cb(int x, int y, int z, int b) {
  if (!in_win(x, y, z)) return;
  unsigned i = WI(x, y, z);
  if ((b & 0x100) && wblk[i] != B_AIR) return;
  wblk[i] = (uint8_t)b;
}

/* (fill's loops down a column, each on its own: few values at once, kept in registers; the CE's in assembly) */
#if defined(__ez80__) && !defined(NB_WORLD_C)
void w_col_copy(uint8_t *p, uint8_t ys, const uint8_t *cells, uint8_t n);
void w_col_light(uint16_t c, uint8_t ys, uint8_t y, uint8_t y1, uint8_t top);
void w_col_seed_border(uint16_t c, uint16_t cn, uint8_t ys, uint8_t y, uint8_t y1);
void w_col_seed(uint16_t c, uint8_t ys, uint8_t y, uint8_t y1, uint8_t top, uint8_t nt, uint8_t inside);
#define col_copy w_col_copy
#define col_light w_col_light
#define col_seed_border w_col_seed_border
#define col_seed(c, ys, y, y1, top, nt, inside) w_col_seed(c, ys, y, y1, top, nt, (uint8_t)(inside))
#else
__attribute__((noinline)) static void col_copy(uint8_t *p, uint8_t ys, const uint8_t *cells, uint8_t n) {
  for (; n; n--, cells++) {
    p[YOFF[ys]] = *cells;
    ys = NEXT32(ys);
  }
}
__attribute__((noinline)) static void col_light(uint16_t c, uint8_t ys, uint8_t y, uint8_t y1, uint8_t top) {
  for (; y < y1; y++, ys = NEXT32(ys)) {
    uint16_t i = (uint16_t)(YOFF[ys] + c);
    uint8_t bl = blk_light[wblk[i]], v = y >= top ? 15 : 0;
    if (bl) v |= (uint8_t)(bl << 4), lq_push(i);
    wlight[i] = v;
  }
}
__attribute__((noinline)) static void col_seed_border(uint16_t c, uint16_t cn, uint8_t ys, uint8_t y, uint8_t y1) {
  for (; y < y1; y++, ys = NEXT32(ys)) {
    uint16_t o = YOFF[ys];
    uint8_t l = wlight[o + c];
    if (!l) continue;
    uint8_t nl = wlight[o + cn];
    if (((l & 15) > (uint8_t)((nl & 15) + 1) || ((nl & 0xF0) < 0xE0 && (l & 0xF0) > (uint8_t)((nl & 0xF0) + 0x10))) &&
        blk_opacity[wblk[o + cn]] < 15)
      lq_push((uint16_t)(o + c));
  }
}
__attribute__((noinline)) static void col_seed(uint16_t c, uint8_t ys, uint8_t y, uint8_t y1, uint8_t top, uint8_t nt, bool inside) {
  uint8_t wy = (uint8_t)win_y;
  for (; y < y1; y++, ys = NEXT32(ys)) {
    uint16_t i = (uint16_t)(YOFF[ys] + c);
    uint8_t l = wlight[i];
    if (!l) continue;
    /* a sky-lit cell matters only next to a column whose sky stops higher, or above leaves or water */
    if (inside && l == 15 && nt <= y && (y != top || y <= wy || blk_opacity[wblk[YOFF[PREV32(ys)] + c]] >= 15))
      continue;
    lq_push(i);
  }
}

#endif

/* makes the columns [x0, x1) x [z0, z1) at levels [y0, y1) (cols: their column data too), then their light and
 * faces */
static void fill(int x0, int z0, int x1, int z1, int y0, int y1, bool cols) {
  render_stale = 1;   /* (the light's queues are in rbuf) */
  uint8_t ys0 = MOD32[y0];
  for (int z = z0; z < z1; z++)
    for (int x = x0; x < x1; x++) {
      int c = CI(x, z);
      Column cc, *col = &cc;
      if (cols) {
        gen_column(x, z, col);
        col_pack(&wcol[c], col);
      } else col_unpack(&wcol[c], col);
      uint8_t *p = wblk + c, cells[WH];
      gen_cells(x, z, col, y0, y1, cells);
      col_copy(p, ys0, cells, (uint8_t)(y1 - y0));
      int py = col->h + 1;
      if (py >= y0 && py < y1 && p[YOFF[MOD32[py]]] == B_AIR && p[YOFF[MOD32[py - 1]]] == col->top) {
        int pl = gen_plant(x, z, col);
        if (pl) p[YOFF[MOD32[py]]] = (uint8_t)pl;
      }
    }
  plat_poll();
  gen_decorate(x0, z0, x1, z1, y0, y1, put_cb);
  for (unsigned k = 0; k < n_edits; k++) {
    const Edit *e = &edits[k];
    if (e->x >= x0 && e->x < x1 && e->z >= z0 && e->z < z1 && e->y >= y0 && e->y < y1)
      wblk[WI(e->x, e->y, e->z)] = e->b;
  }
  /* the sky's light straight down, the block light of what shines; (a new top level) where the sky now stops
   * higher, the column below loses its straight-down light */
  dq_n = 0;
  for (int z = z0; z < z1; z++)
    for (int x = x0; x < x1; x++) {
      int c = CI(x, z), top = column_top(x, z);
      if (!cols && top > wtop[c])
        for (int y = wtop[c]; y < top && y < y0; y++)
          if (y >= win_y) dark_push((uint16_t)(YOFF[MOD32[y]] + c), false);
      wtop[c] = (uint8_t)top;
      col_light((uint16_t)c, ys0, (uint8_t)y0, (uint8_t)y1, (uint8_t)top);
    }
  if (dq_n) light_darken();
  plat_poll();
  /* spreading it: from the lit cells next to darker ones (the region's and the cells around it) */
  for (int z = z0 - 1; z <= z1; z++)
    for (int x = x0 - 1; x <= x1; x++) {
      if ((unsigned)(x - win_x) >= WS || (unsigned)(z - win_z) >= WS) continue;
      int c = CI(x, z), top = wtop[c];
      bool inside = x >= x0 && x < x1 && z >= z0 && z < z1;
      if (!inside) {
        /* a column around the region: the cells brighter than the region's next to them by more than a step
         * (none at the corners: not next to it) */
        bool ox = x < x0 || x >= x1, oz = z < z0 || z >= z1;
        if (ox && oz) continue;
        int cn = CI(x < x0 ? x0 : x >= x1 ? x1 - 1 : x, z < z0 ? z0 : z >= z1 ? z1 - 1 : z);
        col_seed_border((uint16_t)c, (uint16_t)cn, ys0, (uint8_t)y0, (uint8_t)y1);
        if (x == x1) light_spread();
        continue;
      }
      /* (the highest top of the columns around: a sky-lit cell under it lights them sideways) */
      uint8_t sx = MOD32[x - win_x + (win_x & 31)], sz = MOD32[z - win_z + (win_z & 31)], nt = 0;
      const uint8_t *tp = wtop + c;
      if (x > win_x && tp[sx ? -1 : 31] > nt) nt = tp[sx ? -1 : 31];
      if (x < win_x + WS - 1 && tp[sx != 31 ? 1 : -31] > nt) nt = tp[sx != 31 ? 1 : -31];
      if (z > win_z && tp[sz ? -32 : 31 * 32] > nt) nt = tp[sz ? -32 : 31 * 32];
      if (z < win_z + WS - 1 && tp[sz != 31 ? 32 : -31 * 32] > nt) nt = tp[sz != 31 ? 32 : -31 * 32];
      int ya = inside ? y0 - 1 : y0, yb = inside ? y1 + 1 : y1;
      if (ya < win_y) ya = win_y;
      if (yb > win_y + WH) yb = win_y + WH;
      col_seed((uint16_t)c, MOD32[ya], (uint8_t)ya, (uint8_t)yb, (uint8_t)top, nt, inside);
      if (x == x1) light_spread();
    }
  light_spread();
  int ka = y0 - 1 - win_y, kb = y1 + 1 - win_y;
  uint8_t k0 = (uint8_t)(ka < 0 ? 0 : ka), k1 = (uint8_t)(kb > WH ? WH : kb);
  for (int z = z0 - 1; z <= z1; z++)
    for (int x = x0 - 1; x <= x1; x++) expose_col(x, z, k0, k1);
}

/* ---------------------------------------------------------------- changes */
void world_set(int x, int y, int z, int b) {
  if (!in_win(x, y, z)) return;
  render_stale = 1;
  uint16_t i = (uint16_t)WI(x, y, z);
  int old = wblk[i];
  if (old == b) return;
  edit_log(x, y, z, b);
  wblk[i] = (uint8_t)b;
  /* light: take away what came through or from here, then spread it again */
  int c = CI(x, z), top = wtop[c], ntop = top;
  if (blk_opacity[b] && y >= top) ntop = y + 1;
  else if (!blk_opacity[b] && y == top - 1) ntop = column_top(x, z);
  wtop[c] = (uint8_t)ntop;
  dq_n = 0;
  lq_h = lq_t = 0;
  dark_push(i, false);
  dark_push(i, true);
  if (ntop > top)   /* the sky is stopped higher: the column below loses its straight-down light */
    for (int k = ntop - 1; k >= top && k >= win_y; k--) dark_push((uint16_t)WI(x, k, z), false);
  light_darken();
  if (ntop < top)
    for (int k = ntop; k < top; k++) {
      uint16_t j = (uint16_t)WI(x, k, z);
      wlight[j] |= 15;
      lq_push(j);
    }
  if (blk_light[b]) {
    wlight[i] = (uint8_t)((wlight[i] & 15) | blk_light[b] << 4);
    lq_push(i);
  }
  if (y >= ntop) wlight[i] |= 15;
  /* the neighbours bring their light back in */
  edges();
  neighbours(i);
  for (uint8_t k = 0; k < nbr_n; k++)
    if (wlight[nbr[k]]) lq_push(nbr[k]);
  light_spread();
  expose_around(x, y, z);
}

/* ---------------------------------------------------------------- the window */
static void queues(void) {
  lq = (uint16_t *)rbuf;
  dq = (uint16_t *)(rbuf + LQ_N * 2);
  dql = rbuf + LQ_N * 2 + DQ_N * 2;
}

void world_new(uint32_t seed) {
  world_seed = seed;
  gen_seed(seed);
  queues();
}

static void fill_all(void) {
  memset(wexp, 0, sizeof wexp);
  lq_h = lq_t = 0;
  fill(win_x, win_z, win_x + WS, win_z + WS, win_y, win_y + WH, true);
}

static int clampy(int y) {
  y -= WH / 2;
  return y < 0 ? 0 : y > WORLD_H - WH ? WORLD_H - WH : y;
}

void world_center(int x, int y, int z, bool all) {
  want_x = x - WS / 2, want_z = z - WS / 2, want_y = clampy(y);
  if (all) {
    win_x = want_x, win_z = want_z, win_y = want_y;
    fill_all();
  }
}

bool world_busy(void) {
  /* (a row of slack each way: the window moves when the player is 2 blocks off its middle) */
  return want_x - win_x > 1 || win_x - want_x > 1 || want_z - win_z > 1 || win_z - want_z > 1 ||
         want_y - win_y > 2 || win_y - want_y > 2;
}

bool world_behind(void) {
  /* (more rows to make than the view's edge can wait for: the window holds 15 blocks ahead, the view sees
   * view_dist + 1, 9 at 8, 11 at 10) */
  extern int view_dist;
  int k = 13 - view_dist;
  return want_x - win_x > k || win_x - want_x > k || want_z - win_z > k || win_z - want_z > k;
}

/* one row (or level) of the window made: the side the player went towards */
void world_step(void) {
  lq_h = lq_t = 0;
  if (want_x - win_x > 1) {
    win_x++;
    fill(win_x + WS - 1, win_z, win_x + WS, win_z + WS, win_y, win_y + WH, true);
  } else if (win_x - want_x > 1) {
    win_x--;
    fill(win_x, win_z, win_x + 1, win_z + WS, win_y, win_y + WH, true);
  } else if (want_z - win_z > 1) {
    win_z++;
    fill(win_x, win_z + WS - 1, win_x + WS, win_z + WS, win_y, win_y + WH, true);
  } else if (win_z - want_z > 1) {
    win_z--;
    fill(win_x, win_z, win_x + WS, win_z + 1, win_y, win_y + WH, true);
  } else if (want_y - win_y > 2) {
    /* up a level: the new top level, then the sky stops where it may now stop higher */
    win_y++;
    int y = win_y + WH - 1;
    fill(win_x, win_z, win_x + WS, win_z + WS, y, y + 1, false);
  } else if (win_y - want_y > 2) {
    win_y--;
    int y = win_y;
    fill(win_x, win_z, win_x + WS, win_z + WS, y, y + 1, false);
  }
}
