/* The world around the player: a window of 32 x 32 columns and 32 levels that follows them (nb.h), filled from
 * the terrain (gen.c) and the edit log, with Minecraft's light: sky light (15 where the sky is seen straight up,
 * then one less a block, and less through leaves and water) and block light (torches, lava, lit furnaces),
 * spread through what lets light through. */
#include "nb.h"

uint8_t *wblk, *wlight;
uint32_t wexp[WS * WS];
uint8_t wtop[WS * WS], wbiome[WS * WS];
static Column wcol[WS * WS];
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
/* the window's edges: a step from a cell on one leaves the window */
static inline int wy_of(int slot) { return win_y + ((slot - win_y) & 31); }

/* the queues: in the 3D view's picture (light is never spread while it is drawn) */
static uint16_t *lq;      /* cells to spread light from */
static unsigned lq_h, lq_t;
#define LQ_N 5000
static uint16_t *dq;      /* cells to darken, and the light they had */
static uint8_t *dql;
static unsigned dq_n;
#define DQ_N 3000

static void lq_push(unsigned i) {
  unsigned t = lq_t + 1 == LQ_N ? 0 : lq_t + 1;
  if (t == lq_h) return;   /* full: (never, at the window's size; it would only leave some light out) */
  lq[lq_t] = (uint16_t)i;
  lq_t = t;
}

/* the six neighbours of cell i inside the window: their indices, -1 outside */
static int nbr[6];
static void neighbours(unsigned i) {
  int x = i & 31, z = (i >> 5) & 31, y = i >> 10;
  int ex = (win_x + 31) & 31, sx = win_x & 31, ez = (win_z + 31) & 31, sz = win_z & 31, ey = (win_y + 31) & 31,
      sy = win_y & 31;
  nbr[0] = y == sy ? -1 : (int)((i & 0x3FF) | ((unsigned)((y - 1) & 31) << 10));
  nbr[1] = y == ey ? -1 : (int)((i & 0x3FF) | ((unsigned)((y + 1) & 31) << 10));
  nbr[2] = z == sz ? -1 : (int)((i & ~0x3E0u) | ((unsigned)((z - 1) & 31) << 5));
  nbr[3] = z == ez ? -1 : (int)((i & ~0x3E0u) | ((unsigned)((z + 1) & 31) << 5));
  nbr[4] = x == sx ? -1 : (int)((i & ~31u) | ((unsigned)(x - 1) & 31));
  nbr[5] = x == ex ? -1 : (int)((i & ~31u) | ((unsigned)(x + 1) & 31));
}

/* spreads both lights from the queued cells */
static void light_spread(void) {
  while (lq_h != lq_t) {
    unsigned i = lq[lq_h];
    lq_h = lq_h + 1 == LQ_N ? 0 : lq_h + 1;
    int l = wlight[i], s = l & 15, b = l >> 4;
    if (s <= 1 && b <= 1) continue;
    neighbours(i);
    for (int k = 0; k < 6; k++) {
      int n = nbr[k];
      if (n < 0) continue;
      int op = blk_opacity[wblk[n]];
      if (op >= 15) continue;
      if (!op) op = 1;
      int nl = wlight[n], ns = nl & 15, nb = nl >> 4, ch = 0;
      if (s - op > ns) ns = s - op, ch = 1;
      if (b - op > nb) nb = b - op, ch = 1;
      if (ch) {
        wlight[n] = (uint8_t)(ns | nb << 4);
        lq_push((unsigned)n);
      }
    }
  }
}

/* takes away the light that came through cell i (of channel shift sh: 0 sky, 4 block), from level lv; the cells
 * at its edge that keep their own light are queued to spread it back */
static void light_darken(void) {
  unsigned h = 0;
  while (h < dq_n) {
    unsigned i = dq[h];
    int lv = dql[h], sh = lv >> 4 ? 4 : 0;
    lv &= 15;
    h++;
    neighbours(i);
    for (int k = 0; k < 6; k++) {
      int n = nbr[k];
      if (n < 0) continue;
      int nl = (wlight[n] >> sh) & 15;
      if (!nl) continue;
      if (nl < lv && !(sh && blk_light[wblk[n]] >= nl) && !(!sh && wtop[n & 0x3FF] <= wy_of(n >> 10))) {
        wlight[n] &= (uint8_t)~(15 << sh);
        if (dq_n < DQ_N) dq[dq_n] = (uint16_t)n, dql[dq_n] = (uint8_t)(nl | (sh ? 16 : 0)), dq_n++;
      } else lq_push((unsigned)n);
    }
  }
  dq_n = 0;
}
static void dark_push(unsigned i, int sh) {
  int lv = (wlight[i] >> sh) & 15;
  if (!lv) return;
  wlight[i] &= (uint8_t)~(15 << sh);
  if (dq_n < DQ_N) dq[dq_n] = (uint16_t)i, dql[dq_n] = (uint8_t)(lv | (sh ? 16 : 0)), dq_n++;
}

/* the sky's straight-down light of a column: wtop is the first level under everything that stops it */
static int column_top(int x, int z) {
  int c = CI(x, z);
  if (wcol[c].h >= win_y + WH) return win_y + WH;
  for (int y = win_y + WH - 1; y >= win_y; y--)
    if (blk_opacity[wblk[WI(x, y, z)]]) return y + 1;
  return win_y;
}

/* ---------------------------------------------------------------- faces to draw */
static inline bool hides(int b, int nb) {
  if (blk_flags[nb] & BF_OPAQUE) return true;
  if (nb == b && blk_model[b] != M_CROSS && blk_model[b] != M_TORCH) return true;
  if ((blk_flags[b] & BF_LIQUID) && (blk_flags[nb] & BF_LIQUID)) return true;
  return false;
}
static bool exposed(int x, int y, int z) {
  int b = wblk[WI(x, y, z)];
  if (b == B_AIR) return false;
  int m = blk_model[b];
  if (m == M_CROSS || m == M_TORCH) return true;
  /* (past the window: below it, as if solid; above, air) */
  if (y + 1 >= win_y + WH ? true : !hides(b, wblk[WI(x, y + 1, z)])) return true;
  if (y > win_y && !hides(b, wblk[WI(x, y - 1, z)])) return true;
  if (z > win_z && !hides(b, wblk[WI(x, y, z - 1)])) return true;
  if (z < win_z + WS - 1 && !hides(b, wblk[WI(x, y, z + 1)])) return true;
  if (x > win_x && !hides(b, wblk[WI(x - 1, y, z)])) return true;
  if (x < win_x + WS - 1 && !hides(b, wblk[WI(x + 1, y, z)])) return true;
  return false;
}
static void expose(int x, int y, int z) {
  if (!in_win(x, y, z)) return;
  uint32_t bit = (uint32_t)1 << (y & 31);
  if (exposed(x, y, z)) wexp[CI(x, z)] |= bit;
  else wexp[CI(x, z)] &= ~bit;
}

/* ---------------------------------------------------------------- filling the window */
static void put_cb(int x, int y, int z, int b) {
  if (!in_win(x, y, z)) return;
  unsigned i = WI(x, y, z);
  if ((b & 0x100) && wblk[i] != B_AIR) return;
  wblk[i] = (uint8_t)b;
}

/* makes the columns [x0, x1) x [z0, z1) at levels [y0, y1) (cols: their column data too), then their light and
 * faces */
static void fill(int x0, int z0, int x1, int z1, int y0, int y1, bool cols) {
  for (int z = z0; z < z1; z++)
    for (int x = x0; x < x1; x++) {
      int c = CI(x, z);
      Column *col = &wcol[c];
      if (cols) {
        gen_column(x, z, col);
        wbiome[c] = col->biome;
      }
      for (int y = y0; y < y1; y++) wblk[WI(x, y, z)] = (uint8_t)gen_cell(x, y, z, col);
      int py = col->h + 1;
      if (py >= y0 && py < y1 && wblk[WI(x, py, z)] == B_AIR && wblk[WI(x, py - 1, z)] == col->top) {
        int p = gen_plant(x, z, col);
        if (p) wblk[WI(x, py, z)] = (uint8_t)p;
      }
    }
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
          if (y >= win_y) dark_push(WI(x, y, z), 0);
      wtop[c] = (uint8_t)top;
      for (int y = y0; y < y1; y++) {
        unsigned i = WI(x, y, z);
        int bl = blk_light[wblk[i]];
        wlight[i] = (uint8_t)((y >= top ? 15 : 0) | bl << 4);
        if (bl) lq_push(i);
      }
    }
  if (dq_n) light_darken();
  /* spreading it: from the lit cells next to darker ones (the region's and the cells around it) */
  for (int z = z0 - 1; z <= z1; z++)
    for (int x = x0 - 1; x <= x1; x++) {
      if ((unsigned)(x - win_x) >= WS || (unsigned)(z - win_z) >= WS) continue;
      int c = CI(x, z), top = wtop[c];
      bool inside = x >= x0 && x < x1 && z >= z0 && z < z1;
      int ya = inside ? y0 - 1 : y0, yb = inside ? y1 + 1 : y1;
      if (ya < win_y) ya = win_y;
      if (yb > win_y + WH) yb = win_y + WH;
      for (int y = ya; y < yb; y++) {
        unsigned i = WI(x, y, z);
        if (!(wlight[i] & 15) && !(wlight[i] >> 4)) continue;
        /* a sky-lit cell matters only next to a column whose sky stops higher, or above leaves or water */
        if (inside && (wlight[i] & 15) == 15 && !(wlight[i] >> 4)) {
          bool edge = y == top && y > win_y && blk_opacity[wblk[WI(x, y - 1, z)]] < 15;
          if (!edge) {
            int n;
            edge = (x > win_x && (n = wtop[CI(x - 1, z)]) > y) || (x < win_x + WS - 1 && (n = wtop[CI(x + 1, z)]) > y) ||
                   (z > win_z && (n = wtop[CI(x, z - 1)]) > y) || (z < win_z + WS - 1 && (n = wtop[CI(x, z + 1)]) > y);
          }
          if (!edge) continue;
        }
        lq_push(i);
      }
      if (x == x1) light_spread();
    }
  light_spread();
  for (int z = z0 - 1; z <= z1; z++)
    for (int x = x0 - 1; x <= x1; x++) {
      if ((unsigned)(x - win_x) >= WS || (unsigned)(z - win_z) >= WS) continue;
      for (int y = y0 - 1; y <= y1; y++) expose(x, y, z);
    }
}

/* ---------------------------------------------------------------- changes */
void world_set(int x, int y, int z, int b) {
  if (!in_win(x, y, z)) return;
  unsigned i = WI(x, y, z);
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
  dark_push(i, 0);
  dark_push(i, 4);
  if (ntop > top)   /* the sky is stopped higher: the column below loses its straight-down light */
    for (int k = ntop - 1; k >= top && k >= win_y; k--) dark_push(WI(x, k, z), 0);
  light_darken();
  if (ntop < top)
    for (int k = ntop; k < top; k++) {
      unsigned j = WI(x, k, z);
      wlight[j] |= 15;
      lq_push(j);
    }
  if (blk_light[b]) {
    wlight[i] = (uint8_t)((wlight[i] & 15) | blk_light[b] << 4);
    lq_push(i);
  }
  if (y >= ntop) wlight[i] |= 15;
  /* the neighbours bring their light back in */
  neighbours(i);
  for (int k = 0; k < 6; k++)
    if (nbr[k] >= 0 && wlight[nbr[k]]) lq_push((unsigned)nbr[k]);
  light_spread();
  expose(x, y, z);
  expose(x, y - 1, z);
  expose(x, y + 1, z);
  expose(x - 1, y, z);
  expose(x + 1, y, z);
  expose(x, y, z - 1);
  expose(x, y, z + 1);
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
    for (int z = win_z; z < win_z + WS; z++)
      for (int x = win_x; x < win_x + WS; x++) wexp[CI(x, z)] &= ~((uint32_t)1 << (y & 31));
    fill(win_x, win_z, win_x + WS, win_z + WS, y, y + 1, false);
    for (int z = win_z; z < win_z + WS; z++)
      for (int x = win_x; x < win_x + WS; x++) expose(x, y - 1, z);
  } else if (win_y - want_y > 2) {
    win_y--;
    int y = win_y;
    for (int z = win_z; z < win_z + WS; z++)
      for (int x = win_x; x < win_x + WS; x++) wexp[CI(x, z)] &= ~((uint32_t)1 << (y & 31));
    fill(win_x, win_z, win_x + WS, win_z + WS, y, y + 1, false);
    for (int z = win_z; z < win_z + WS; z++)
      for (int x = win_x; x < win_x + WS; x++) expose(x, y + 1, z);
  }
}
