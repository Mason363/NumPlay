/* The player's changes to the world, saved as they happen.
 *
 * The world is cut in regions of 4 x 4 chunks (64 x 64 columns). Each region
 * the player changed has a record in the calculator's storage, read where it
 * lies: sorted 32-bit entries, chunk (4 bits), x and z in it (8), y (7), then
 * the block (8, the top byte). New changes go to a small journal in RAM, kept
 * in the same order; when it fills up (and when the game saves), its entries
 * are merged into the records.
 *
 * NumBlocks.nwa's blocks are 9 bits: one past 255 is kept with its ninth bit in bit 23 of the entry
 * (outside every key NumPlay's NumBlocks looks up, which so never sees those blocks), its low byte
 * on top; records stay sorted by the 24 bits. If a spot has both kinds of entry (NumPlay's
 * NumBlocks changed it after), the other kind wins; but the other woods' fences, gates, doors and
 * stairs come with an entry of oak's (blk_np), which NumPlay's NumBlocks shows, and which wins only
 * once it is something else. Each dimension has its own records: the Nether's "nb1rn-3_12.nbe", the
 * End's "nb1re...". */
#include "edits.h"
#include "nb.h"
#pragma GCC optimize("Os")   /* (not where the time goes: small) */

typedef struct {
  int16_t rx, rz;
  uint32_t e;   /* the entry, as in the records */
} Journal;

#if NB_DIMS
#define J_MAX 128   /* (NumBlocks.nwa: room for the 9th bits of blocks, nb.h) */
#else
#define J_MAX 256
#endif
static Journal jn[J_MAX];
static int nj;
static char prefix[8] = "nb1";
static bool full;   /* a record could not grow: storage is full */
static uint32_t *scratch;
static uint32_t scratch_n;
#if NB_DIMS
static char dim_tag;   /* 0 the Overworld, 'n' the Nether, 'e' the End */
void edits_dim(int d) { dim_tag = d == DIM_NETHER ? 'n' : d == DIM_END ? 'e' : 0; }
#define HI (1u << 23)
#endif

void edits_setup(const char *world, uint32_t *buf, uint32_t n) {
  int i = 0;
  for (; world[i] && i < 7; i++) prefix[i] = world[i];
  prefix[i] = 0;
  scratch = buf;
  scratch_n = n;
}
bool edits_full(void) { return full; }

static inline int floordiv(int a, int d) { return a >= 0 ? a / d : -((-a + d - 1) / d); }

/* "nb1r-3_12.nbe" */
static void region_name(char *out, int rx, int rz) {
  char *p = out;
  for (const char *s = prefix; *s;) *p++ = *s++;
  *p++ = 'r';
#if NB_DIMS
  if (dim_tag) *p++ = dim_tag;
#endif
  int v[2] = {rx, rz};
  for (int k = 0; k < 2; k++) {
    if (k) *p++ = '_';
    int n = v[k];
    if (n < 0) *p++ = '-', n = -n;
    char t[8];
    int m = 0;
    do t[m++] = (char)('0' + n % 10), n /= 10;
    while (n);
    while (m) *p++ = t[--m];
  }
  const char *ext = ".nbe";
  while (*ext) *p++ = *ext++;
  *p = 0;
}

/* a region's record (the last one looked up is remembered until records change) */
static bool cache_ok;
static int cache_rx, cache_rz, cache_n;
static const uint8_t *cache_r;
static const uint8_t *region(int rx, int rz, int *n) {
  if (!cache_ok || rx != cache_rx || rz != cache_rz) {
    char name[32];
    region_name(name, rx, rz);
    uint32_t len = 0;
    const uint8_t *d = plat_load(name, &len);
    cache_r = d;
    cache_n = d ? (int)(len / 4) : 0;
    cache_rx = rx, cache_rz = rz, cache_ok = true;
  }
  *n = cache_n;
  return cache_r;
}

void edits_clear(void) { nj = 0, full = false, cache_ok = false; }
void edits_forget(void) { cache_ok = false; }

/* records are not aligned in storage: entries are read a byte at a time */
static inline uint32_t rd32(const uint8_t *p) {
  uint32_t v;
  memcpy(&v, p, 4);
  return v;
}
#define R(i) rd32(r + 4 * (i))

/* entries are sorted by their low 24 bits */
#define KEY(e) ((e) & 0xFFFFFF)
static inline uint32_t entry_key(int x, int y, int z) {
  int cx = floordiv(x, 16), cz = floordiv(z, 16);
  int chunk = (cx & 3) | (cz & 3) << 2;
  int xz = (x & 15) | (z & 15) << 4;
  return (uint32_t)(chunk << 15 | xz << 7 | (y & 127));
}

static int jcmp(int rx, int rz, uint32_t key, const Journal *j) {
  if (rz != j->rz) return rz < j->rz ? -1 : 1;
  if (rx != j->rx) return rx < j->rx ? -1 : 1;
  uint32_t k = KEY(j->e);
  return key < k ? -1 : key > k;
}
static int jlower(int rx, int rz, uint32_t key) {
  int lo = 0, hi = nj;
  while (lo < hi) {
    int m = (lo + hi) / 2;
    if (jcmp(rx, rz, key, &jn[m]) > 0) lo = m + 1;
    else hi = m;
  }
  return lo;
}
static int rlower(const uint8_t *r, int n, uint32_t key) {
  int lo = 0, hi = n;
  while (lo < hi) {
    int m = (lo + hi) / 2;
    if (KEY(R(m)) < key) lo = m + 1;
    else hi = m;
  }
  return lo;
}

#if NB_DIMS
/* the journal has an entry of key in region (rx, rz), among its entries [i, j) */
static bool journal_has(int rx, int rz, uint32_t key, int i, int j) {
  int k = jlower(rx, rz, key);
  return k >= i && k < j && jcmp(rx, rz, key, &jn[k]) == 0;
}
#endif

static inline void wr32(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }

/* the journal's entries go into their regions' records: each record grows in place in storage and the two
 * are merged from the end, so a record can be as big as storage lets it be */
bool edits_flush(void) {
  bool ok = true;
  int i = 0;
  while (i < nj) {
    int rx = jn[i].rx, rz = jn[i].rz, j = i;
    while (j < nj && jn[j].rx == rx && jn[j].rz == rz) j++;
    int n;
    region(rx, rz, &n);
    char name[32];
    region_name(name, rx, rz);
    cache_ok = false;
    int top = n + (j - i);
    uint8_t *r = plat_save_grow(name, (uint32_t)top * 4);
    if (!r) {
      /* no room: these changes stay in the journal */
      ok = false;
      full = true;
      i = j;
      continue;
    }
    int a = n - 1, b = j - 1, o = top;
    while (a >= 0 || b >= i) {
      if (a >= 0 && (b < i || KEY(R(a)) > KEY(jn[b].e))) {
#if NB_DIMS
        if (journal_has(rx, rz, KEY(R(a)) ^ HI, i, j)) {
          a--;   /* (the journal has its spot, as the other kind) */
          continue;
        }
#endif
        wr32(r + 4 * --o, R(a)), a--;   /* (never past what is still to read: o > a) */
      } else {
        if (a >= 0 && KEY(R(a)) == KEY(jn[b].e)) a--;
        wr32(r + 4 * --o, jn[b--].e);
      }
    }
    if (o) memmove(r, r + 4 * o, (size_t)(top - o) * 4);
    plat_save_done(name, (uint32_t)(top - o) * 4);
    memmove(&jn[i], &jn[j], (size_t)(nj - j) * sizeof jn[0]);
    nj -= j - i;
  }
  return ok;
}

#if NB_DIMS
static bool entry_put(int rx, int rz, uint32_t key, int b);
#endif
bool edits_put(int x, int y, int z, int b) {
  int rx = floordiv(x, 64), rz = floordiv(z, 64);
  uint32_t key = entry_key(x, y, z);
#if NB_DIMS
  {
    int np = b > 256 && !dim_tag ? blk_np[b - FIRST_DIMS] : 255;
    if (np != 255) {
      /* (in the Overworld, a block NumPlay's NumBlocks shows as another: that one is the spot's other entry, the two
       * kept together, so a merge never parts them) */
      if (nj + 2 > J_MAX && (edits_flush(), nj + 2 > J_MAX)) return false;
      entry_put(rx, rz, key, np);
    } else {
      /* the spot's entry of the other kind goes: this one replaces it */
      int o = jlower(rx, rz, key ^ HI);
      if (o < nj && jcmp(rx, rz, key ^ HI, &jn[o]) == 0) {
        memmove(&jn[o], &jn[o + 1], (size_t)(nj - o - 1) * sizeof jn[0]);
        nj--;
      }
    }
    key |= (uint32_t)(b >> 8 & 1) << 23;
  }
  return entry_put(rx, rz, key, b);
}
static bool entry_put(int rx, int rz, uint32_t key, int b) {
#endif
  int i = jlower(rx, rz, key);
  if (i < nj && jcmp(rx, rz, key, &jn[i]) == 0) {
    jn[i].e = key | (uint32_t)b << 24;
    return true;
  }
  if (nj >= J_MAX) {
    edits_flush();
    if (nj >= J_MAX) return false;
    i = jlower(rx, rz, key);
  }
  memmove(&jn[i + 1], &jn[i], (size_t)(nj - i) * sizeof jn[0]);
  jn[i] = (Journal){(int16_t)rx, (int16_t)rz, key | (uint32_t)b << 24};
  nj++;
  return true;
}

/* onto a chunk from gen_slab: the record's entries, then the journal's */
void edits_apply(int cx, int cz, int y0, int h, uint8_t *slab) {
  int rx = floordiv(cx, 4), rz = floordiv(cz, 4), chunk = (cx & 3) | (cz & 3) << 2;
  uint32_t k0 = (uint32_t)chunk << 15, k1 = k0 + (1 << 15);
  int n;
  const uint8_t *r = region(rx, rz, &n);
  for (int i = rlower(r, n, k0); i < n && KEY(R(i)) < k1; i++) {
    uint32_t e = R(i);
    int y = (int)(e & 127) - y0, xz = (int)(e >> 7) & 255;
    if (y >= 0 && y < h) slab[(y * 16 + (xz >> 4)) * 16 + (xz & 15)] = (uint8_t)(e >> 24);
  }
  for (int i = jlower(rx, rz, k0); i < nj && jn[i].rx == rx && jn[i].rz == rz && KEY(jn[i].e) < k1; i++) {
    int y = (int)(jn[i].e & 127) - y0, xz = (int)(jn[i].e >> 7) & 255;
    if (y >= 0 && y < h) slab[(y * 16 + (xz >> 4)) * 16 + (xz & 15)] = (uint8_t)(jn[i].e >> 24);
  }
}

#if NB_DIMS
/* edits_apply, the ninth bits in hb (a bit a cell of the slab): first the record's entries of each kind
 * (the low ones last: they win, but where one stands for the other), then the journal's */
static void apply_range(const uint8_t *r, int n, uint32_t k0, uint32_t k1, int y0, int h, uint8_t *slab, uint8_t *hb,
                        bool hi) {
  for (int i = rlower(r, n, k0); i < n && KEY(R(i)) < k1; i++) {
    uint32_t e = R(i);
    int y = (int)(e & 127) - y0, xz = (int)(e >> 7) & 255;
    if (y < 0 || y >= h) continue;
    int c = (y * 16 + (xz >> 4)) * 16 + (xz & 15);
    /* (a block past 255 there, and this what NumPlay's NumBlocks shows instead of it: it stays) */
    if (!hi && (hb[c >> 3] >> (c & 7) & 1) && blk_np[slab[c] + 256 - FIRST_DIMS] == (uint8_t)(e >> 24)) continue;
    slab[c] = (uint8_t)(e >> 24);
    hb[c >> 3] = (uint8_t)((hb[c >> 3] & ~(1 << (c & 7))) | hi << (c & 7));
  }
}
void edits_apply9(int cx, int cz, int y0, int h, uint8_t *slab, uint8_t *hb) {
  int rx = floordiv(cx, 4), rz = floordiv(cz, 4), chunk = (cx & 3) | (cz & 3) << 2;
  uint32_t k0 = (uint32_t)chunk << 15, k1 = k0 + (1 << 15);
  int n;
  const uint8_t *r = region(rx, rz, &n);
  apply_range(r, n, k0 | HI, k1 | HI, y0, h, slab, hb, true);
  apply_range(r, n, k0, k1, y0, h, slab, hb, false);
  for (int pass = 0; pass < 2; pass++) {
    uint32_t a = pass ? k0 | HI : k0, b = pass ? k1 | HI : k1;
    for (int i = jlower(rx, rz, a); i < nj && jn[i].rx == rx && jn[i].rz == rz && KEY(jn[i].e) < b; i++) {
      int y = (int)(jn[i].e & 127) - y0, xz = (int)(jn[i].e >> 7) & 255;
      if (y < 0 || y >= h) continue;
      int c = (y * 16 + (xz >> 4)) * 16 + (xz & 15);
      slab[c] = (uint8_t)(jn[i].e >> 24);
      hb[c >> 3] = (uint8_t)((hb[c >> 3] & ~(1 << (c & 7))) | pass << (c & 7));
    }
  }
}
#define BLOCK_OF(e) ((int)((e) >> 24) | (int)((e) >> 23 & 1) << 8)
#else
#define BLOCK_OF(e) ((e) >> 24)
#endif

/* the column's sky top (1 + the highest opaque y) with the changes */
int edits_top(int x, int z, int top) {
  int rx = floordiv(x, 64), rz = floordiv(z, 64);
  uint32_t k0 = entry_key(x, 0, z), k1 = k0 + 128;
  int n;
  const uint8_t *r = region(rx, rz, &n);
#if NB_DIMS
  for (int pass = 0; pass < 2; pass++, k0 |= HI, k1 |= HI) {
#endif
  for (int i = rlower(r, n, k0); i < n && KEY(R(i)) < k1; i++) {
    uint32_t e = R(i);
    if ((blk_flags[BLOCK_OF(e)] & BF_OPAQUE) && (int)(e & 127) + 1 > top) top = (int)(e & 127) + 1;
  }
  for (int i = jlower(rx, rz, k0); i < nj && jn[i].rx == rx && jn[i].rz == rz && KEY(jn[i].e) < k1; i++)
    if ((blk_flags[BLOCK_OF(jn[i].e)] & BF_OPAQUE) && (int)(jn[i].e & 127) + 1 > top) top = (int)(jn[i].e & 127) + 1;
#if NB_DIMS
  }
#endif
  return top;
}

#if NB_DIMS
/* was the block there changed (else it is as the world was made)? */
bool edits_changed(int x, int y, int z) {
  int rx = floordiv(x, 64), rz = floordiv(z, 64), n;
  uint32_t key = entry_key(x, y, z);
  const uint8_t *r = region(rx, rz, &n);
  for (int pass = 0; pass < 2; pass++, key |= HI) {
    int i = jlower(rx, rz, key);
    if (i < nj && jcmp(rx, rz, key, &jn[i]) == 0) return true;
    int k = rlower(r, n, key);
    if (k < n && KEY(R(k)) == key) return true;
  }
  return false;
}

/* Teleporter.placeInExistingPortal: of the blocks b0 and b1 changed within r of (x, z) (portals: only made
 * by changes), the nearest to (x, y, z) of those with neither below; false if none. Records only: the
 * journal is flushed first */
bool edits_find(int x, int y, int z, int rad, int b0, int b1, int *out) {
  int64_t best = -1;
  for (int rz = floordiv(z - rad, 64); rz <= floordiv(z + rad, 64); rz++)
    for (int rx = floordiv(x - rad, 64); rx <= floordiv(x + rad, 64); rx++) {
      int n;
      const uint8_t *rec = region(rx, rz, &n);
      const uint8_t *r_ = rec;
#define RR(i) rd32(r_ + 4 * (i))
      for (int i = n; i-- > 0 && (RR(i) & HI);) {
        uint32_t e = RR(i);
        int b = BLOCK_OF(e);
        if (b != b0 && b != b1) continue;
        uint32_t k = KEY(e) & ~HI;
        /* (a plain entry of the spot wins over it: NumPlay's NumBlocks changed it since) */
        const uint8_t *r = rec;
        int j = rlower(r, n, k);
        if (j < n && KEY(R(j)) == k) continue;
        if ((e & 127) && i > 0 && KEY(RR(i - 1)) == KEY(e) - 1 && (BLOCK_OF(RR(i - 1)) == b0 || BLOCK_OF(RR(i - 1)) == b1))
          continue;   /* (one below: the lowest of a column counts) */
        int chunk = (int)(e >> 15) & 15, xz = (int)(e >> 7) & 255;
        int bx = (rx * 4 + (chunk & 3)) * 16 + (xz & 15), bz = (rz * 4 + (chunk >> 2)) * 16 + (xz >> 4), by = (int)(e & 127);
        if (bx < x - rad || bx > x + rad || bz < z - rad || bz > z + rad) continue;
        int64_t d = (int64_t)(bx - x) * (bx - x) + (int64_t)(by - y) * (by - y) + (int64_t)(bz - z) * (bz - z);
        if (best < 0 || d < best) best = d, out[0] = bx, out[1] = by, out[2] = bz;
      }
#undef RR
    }
  return best >= 0;
}
#endif

int edits_count(void) { return nj; }

/* the merge buffer, lent out between merges (n bytes at most) */
void *edits_scratch(uint32_t n) { return n <= scratch_n * 4 ? scratch : NULL; }
