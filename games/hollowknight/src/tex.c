/* Scenery textures: 16x16 tiles (32x16 at 2 bits), decoded from their LZMA blocks into a cache when first drawn. */
#include "hk.h"
#include "tilecode.h"

#ifndef NSLOTS
#define NSLOTS 400
#endif
#define NHASH 256
#define NO 0xFFFF
#define BLOCK_TILES 16

/* the slots' texels: on the calculator, in the RAM after the game's own (_heap_start to _heap_end), as many slots as
 * fit (NSLOTS at most): the calculator's software gives apps 148928 bytes of RAM or more, some builds of it less */
#ifdef HOST
static uint8_t slot_mem[NSLOTS][128] __attribute__((aligned(4)));
static uint8_t (*slot_px)[128] = slot_mem;
static int nslots = NSLOTS;
#else
static uint8_t (*slot_px)[128];
static int nslots;
#endif
static uint16_t slot_box[NSLOTS];   /* where each tile's texels that are not clear are (tile_box) */
static uint32_t slot_key[NSLOTS];
static uint16_t slot_next[NSLOTS], slot_frame[NSLOTS];
static uint8_t slot_strip[NSLOTS];   /* the strip of the frame it was drawn in last */
static uint8_t cur_strip;
static bool full;   /* the last frame needed all the slots */
static uint16_t head[NHASH];
static uint16_t frame = 1, hand;
static uint8_t block_buf[BLOCK_TILES * 128] __attribute__((aligned(4)));
static int block_tex = -1, block_first;   /* (whose tiles block_buf holds) */
#define tcp g_scratch   /* TC_N4 */
_Static_assert(TC_N4 <= SCRATCH_N, "the tile decoder's probabilities: in g_scratch");
#define NONE_T 255

/* ---------------------------------------------------------------- the tile decoder (tilecode.h) */
/* one binary step: the range coder's state in locals (rng, code, src), the probability at pr */
#define RC_BIT(pr, bit)                                                                \
  do {                                                                                 \
    uint16_t *pr_ = (pr);                                                              \
    uint32_t pv_ = *pr_, p_ = pv_ >> 4, bound_ = (rng >> TC_PROB_BITS) * p_;           \
    uint32_t st_ = tc_state[pv_ & 7], sh_ = st_ >> 4;                                  \
    if (code < bound_) rng = bound_, bit = 0, p_ += (4096 - p_) >> sh_;                \
    else code -= bound_, rng -= bound_, bit = 1, p_ -= p_ >> sh_;                      \
    while (rng < (1u << 24)) rng <<= 8, code = code << 8 | *src++;                     \
    *pr_ = (uint16_t)(p_ << 4 | (st_ & 15));                                           \
  } while (0)

/* a 4-bit value in 4 binary steps, nodes 1..15 at p + node */
#define RC_TREE4(p, v)                                                                 \
  do {                                                                                 \
    uint16_t *pt_ = (p);                                                               \
    int b_, n_ = 1;                                                                    \
    RC_BIT(pt_ + n_, b_);                                                              \
    n_ = n_ * 2 + b_;                                                                  \
    RC_BIT(pt_ + n_, b_);                                                              \
    n_ = n_ * 2 + b_;                                                                  \
    RC_BIT(pt_ + n_, b_);                                                              \
    n_ = n_ * 2 + b_;                                                                  \
    RC_BIT(pt_ + n_, b_);                                                              \
    n_ = n_ * 2 + b_;                                                                  \
    v = n_ - 16;                                                                       \
  } while (0)

/* a texel of a packed tile */
static int texel_at(const uint8_t *t, int x, int y, bool two) {
  return two ? t[y * 8 + (x >> 2)] >> (x & 3) * 2 & 3 : t[y * 8 + (x >> 1)] >> (x & 1) * 4 & 15;
}

/* n tiles of a block, packed (4 bits a texel, or 2 in tiles twice as wide) into out, 128 bytes each; left, up: each
 * tile's neighbors in the block (NONE_T: not there) */
static void decode_block(const uint8_t *src, int n, bool two, const uint8_t *left, const uint8_t *up, uint8_t *out) {
  /* the trained probabilities, ready to use (pack.py) */
  const uint8_t *prior = section(SEC_PRIOR);
  if (two) copy_words(tcp, prior + TC_N4 * 2, TC_N2 * 2);
  else copy_words(tcp, prior, TC_N4 * 2);
  uint32_t rng = 0xFFFFFFFFu, code = 0;
  for (int i = 0; i < 4; i++) code = code << 8 | *src++;
  int w = two ? 32 : 16, s = TC_STRIDE(w);
  uint8_t img[17 * TC_STRIDE(32)];   /* the row above, then the tile's: 2 texels to the left, 1 to the right */
  for (int t = 0; t < n; t++) {
    /* the borders: the last 2 columns of the tile to the left, the last row of the one above */
    const uint8_t *lt = left[t] == NONE_T ? NULL : out + left[t] * 128;
    const uint8_t *ut = up[t] == NONE_T ? NULL : out + up[t] * 128;
    for (int y = 0; y < 16; y++) {
      uint8_t *r = img + (y + 1) * s;
      r[0] = lt ? (uint8_t)texel_at(lt, w - 2, y, two) : 0;
      r[1] = lt ? (uint8_t)texel_at(lt, w - 1, y, two) : 0;
    }
    int y = 0;
    if (ut) {
      for (int x = 0; x < w; x++) img[2 + x] = (uint8_t)texel_at(ut, x, 15, two);
      img[1] = img[s + 1], img[w + 2] = img[w + 1];
    } else {
      /* the top row on its own */
      uint8_t *r = img + s + 2;
      for (int x = 0; x < w; x++) {
        int l = r[x - 1], v, bit;
        if (two) {
          uint16_t *p = tcp + tc_c20(l, r[x - 2]);
          RC_BIT(p, bit);
          RC_BIT(p + 1 + bit, v);
          v |= bit << 1;
        } else {
          RC_BIT(tcp + tc_f0(l, r[x - 2]), bit);
          if (!bit) v = l;
          else RC_TREE4(tcp + tc_tree(l, l), v);
        }
        r[x] = (uint8_t)v;
      }
      r[w] = r[w - 1];
      y = 1;
    }
    for (; y < 16; y++) {
      uint8_t *r = img + (y + 1) * s + 2;
      const uint8_t *a = r - s;
      if (two)
        for (int x = 0; x < 32; x++) {
          uint16_t *p = tcp + tc_c2(r[x - 1], a[x], a[x - 1], a[x + 1]);
          int b0, b1;
          RC_BIT(p, b0);
          RC_BIT(p + 1 + b0, b1);
          r[x] = (uint8_t)(b0 << 1 | b1);
        }
      else
        for (int x = 0; x < 16; x++) {
          int l = r[x - 1], u = a[x], v, bit;
          RC_BIT(tcp + tc_fl(l, u, a[x - 1], a[x + 1], r[x - 2]), bit);
          if (!bit) v = l;
          else {
            if (u != l) RC_BIT(tcp + tc_fu(l, u, a[x - 1]), bit);
            if (!bit) v = u;
            else RC_TREE4(tcp + tc_tree(l, u), v);
          }
          r[x] = (uint8_t)v;
        }
      r[w] = r[w - 1];
    }
    for (y = 0; y < 16; y++) {
      const uint8_t *r = img + (y + 1) * s + 2;
      uint8_t *o = out + t * 128 + y * 8;
      if (two)
        for (int i = 0; i < 8; i++) o[i] = (uint8_t)(r[4 * i] | r[4 * i + 1] << 2 | r[4 * i + 2] << 4 | r[4 * i + 3] << 6);
      else
        for (int i = 0; i < 8; i++) o[i] = (uint8_t)(r[2 * i] | r[2 * i + 1] << 4);
    }
  }
}
static bool inited;
uint32_t g_tex_decodes, g_tex_misses, g_tex_calls;
bool g_tex_overload;

static const uint8_t *sec_tex, *sec_tdat;
const TexRec *tex_rec(uint16_t t) {
  if (!sec_tex) sec_tex = section(SEC_TEX) + 4;
  return (const TexRec *)(const void *)(sec_tex + sizeof(TexRec) * t);
}
_Static_assert(sizeof(TexRec) == 12, "TexRec: as tools/pack.py writes it");

/* (tools/pack.py) a texture's data: its tile map, where its blocks after the first start (2 bytes each, from its data),
 * its blocks. The map: per row, the rank of its first tile (a byte, 2 if TEX_WIDE), then 2 bits a tile; none for
 * TEX_FULL (every row as full_row) */
static const uint8_t full_row[64] = {[0 ... 63] = 0x55};
static int row_stride(const TexRec *r) { return ((r->flags & TEX_WIDE) ? 2 : 1) + (r->tw + 3) / 4; }
static const uint8_t *row_base(const TexRec *r, int ty) { return sec_tdat + r->off + ty * row_stride(r); }
static int row_rank(const TexRec *r, int ty) {
  if (r->flags & TEX_FULL) return ty * r->tw;
  const uint8_t *b = row_base(r, ty);
  return (r->flags & TEX_WIDE) ? rd16(b) : b[0];
}
static const uint8_t *row_codes(const TexRec *r, int ty) {
  if (r->flags & TEX_FULL) return full_row;
  return row_base(r, ty) + ((r->flags & TEX_WIDE) ? 2 : 1);
}
const uint8_t *tex_row_codes(uint16_t t, int ty) {
  if (!sec_tdat) sec_tdat = section(SEC_TDAT);
  return row_codes(tex_rec(t), ty);
}

static int code_at(const uint8_t *codes, int tx) { return codes[tx >> 2] >> ((tx & 3) * 2) & 3; }

/* how many of a code byte's 4 tiles are kept */
static uint8_t kept4[256];
static void init_kept4(void) {
  for (int i = 0; i < 256; i++) kept4[i] = (uint8_t)(((i & 3) != 0) + ((i >> 2 & 3) != 0) + ((i >> 4 & 3) != 0) + ((i >> 6 & 3) != 0));
}

/* the tile's number among the texture's kept tiles */
static int tile_rank(const TexRec *r, int tx, int ty) {
  if (r->flags & TEX_FULL) return ty * r->tw + tx;
  const uint8_t *c = row_codes(r, ty);
  int n = row_rank(r, ty), i = 0;
  for (; i + 4 <= tx; i += 4) n += kept4[c[i >> 2]];
  for (; i < tx; i++) n += code_at(c, i) != 0;
  return n;
}
static int tex_tiles(const TexRec *r) {
  if (r->flags & TEX_FULL) return r->tw * r->th;
  const uint8_t *c = row_codes(r, r->th - 1);
  int n = row_rank(r, r->th - 1);
  for (int i = 0; i < r->tw; i++) n += code_at(c, i) != 0;
  return n;
}

static void init(void) {
#ifndef HOST
  extern char _heap_start[], _heap_end[];
  uintptr_t from = ((uintptr_t)_heap_start + 3) & ~(uintptr_t)3;
  uint32_t n = (uint32_t)((uintptr_t)_heap_end - from) / 128;
  slot_px = (uint8_t (*)[128])from;
  nslots = n < NSLOTS ? (int)n : NSLOTS;
#endif
  init_kept4();
  sec_tex = section(SEC_TEX) + 4, sec_tdat = section(SEC_TDAT);
  for (int i = 0; i < NHASH; i++) head[i] = NO;
  for (int i = 0; i < nslots; i++) slot_key[i] = 0xFFFFFFFF, slot_next[i] = NO, slot_frame[i] = 0;
  inited = true;
}

static unsigned hash(uint32_t k) { return (k * 2654435761u) >> 24 & (NHASH - 1); }

static int find(uint32_t key) {
  for (int s = head[hash(key)]; s != NO; s = slot_next[s])
    if (slot_key[s] == key) return s;
  return -1;
}

static void unlink_slot(int s) {
  if (slot_key[s] == 0xFFFFFFFF) return;
  uint16_t *p = &head[hash(slot_key[s])];
  while (*p != NO && *p != s) p = &slot_next[*p];
  if (*p == s) *p = slot_next[s];
  slot_key[s] = 0xFFFFFFFF;
}

/* a slot to reuse: one unused for a while; else one not drawn yet this frame (drawn last frame, maybe out of the view
 * now), but when the view needs more tiles than there are slots, first one drawn this frame above the strip and the
 * one before it (done with, until the next frame); else any */
static int victim(void) {
  int ahead = -1, done = -1;
  for (int n = 0; n < nslots; n++) {
    int s = hand;
    hand = (uint16_t)((hand + 1) % nslots);
    uint16_t age = (uint16_t)(frame - slot_frame[s]);
    if (age > 1) return s;
    if (!age && slot_strip[s] + 1 < cur_strip) {
      if (done < 0) done = s;
    } else if (age == 1 && ahead < 0)
      ahead = s;
  }
  if (done >= 0 && (full || ahead < 0)) {
    gfx_slot_reused(done);   /* (the frame's items that kept it: they look again) */
    return done;
  }
  if (ahead >= 0) return ahead;
  g_tex_overload = true;
  int s = hand;   /* the view needs more tiles than the cache holds */
  hand = (uint16_t)((hand + 1) % nslots);
  return s;
}

/* where a tile's texels that are not clear are, 4 bits each: x0, x1 - 1, y0, y1 - 1 (x1, y1 past them; at 2 bits, x
 * in twos); all the tile if none */
static uint16_t tile_box(const uint8_t *px) {
  uint32_t c0 = 0, c1 = 0;
  int y0 = 16, y1 = 0;
  for (int y = 0; y < 16; y++) {
    uint32_t w[2];
    memcpy(w, px + y * 8, 8);
    if (w[0] | w[1]) {
      if (y0 > y) y0 = y;
      y1 = y + 1, c0 |= w[0], c1 |= w[1];
    }
  }
  if (!y1) return 0xF0F0;
  /* (texel k: bits 4k (2k at 2 bits) and up of the row) */
  int lb = c0 ? __builtin_ctz(c0) : 32 + __builtin_ctz(c1), hb = c1 ? 63 - __builtin_clz(c1) : 31 - __builtin_clz(c0);
  /* (4 bits: a texel, or two at 2 bits) */
  return (uint16_t)((lb >> 2) | (hb >> 2) << 4 | y0 << 8 | (y1 - 1) << 12);
}
uint32_t tex_slot_box(const uint8_t *px) { return slot_box[(px - slot_px[0]) >> 7]; }

static int insert(uint32_t key, const uint8_t *px, int bytes) {
  int s = victim();
  unlink_slot(s);
  slot_key[s] = key;
  unsigned h = hash(key);
  slot_next[s] = head[h];
  head[h] = (uint16_t)s;
  copy_words(slot_px[s], px, (size_t)bytes);
  slot_box[s] = tile_box(px);
  return s;
}

/* a slot not drawn this frame or the last one, for the rest of a decoded block (-1: none, keep what is used) */
static int stale_slot(void) {
  for (int n = 0; n < 64; n++) {
    int s = hand;
    hand = (uint16_t)((hand + 1) % nslots);
    if ((uint16_t)(frame - slot_frame[s]) > 1) return s;
  }
  return -1;
}

const uint8_t *tex_slot_px(int s) {
  slot_strip[s] = cur_strip;
  return slot_px[s];
}
void tex_strip(int s) { cur_strip = (uint8_t)s; }


/* where a block's tiles are (by rank: row by row), and so which of them is each one's left and upper neighbor */
static void block_geometry(const TexRec *r, int first, int n, uint8_t *left, uint8_t *up) {
  uint8_t tx[BLOCK_TILES], ty[BLOCK_TILES];
  int y = 0, i = 0;
  while (y + 1 < r->th && row_rank(r, y + 1) <= first) y++;
  for (int k = row_rank(r, y); i < n; y++)
    for (int x = 0; x < r->tw && i < n; x++)
      if (code_at(row_codes(r, y), x) && k++ >= first) tx[i] = (uint8_t)x, ty[i] = (uint8_t)y, i++;
  for (i = 0; i < n; i++) {
    left[i] = i && ty[i - 1] == ty[i] && tx[i - 1] + 1 == tx[i] ? (uint8_t)(i - 1) : NONE_T;
    up[i] = NONE_T;
    for (int j = 0; j < i; j++)
      if (tx[j] == tx[i] && ty[j] + 1 == ty[i]) up[i] = (uint8_t)j;
  }
}

const uint8_t *tex_tile(uint16_t t, int tx, int ty) {
  int s = tex_slot(t, tx, ty);
  return s < 0 ? NULL : slot_px[s];
}

int tex_slot(uint16_t t, int tx, int ty) {
  if (!inited) init();
  g_tex_calls++;
  const TexRec *r = tex_rec(t);
  if ((unsigned)tx >= r->tw || (unsigned)ty >= r->th) return -1;
  if (!code_at(row_codes(r, ty), tx)) return -1;
  int rank = tile_rank(r, tx, ty);
  uint32_t key = (uint32_t)t << 16 | (uint32_t)rank;
  int s = find(key);
  if (s >= 0) {
    slot_frame[s] = frame, slot_strip[s] = cur_strip;
    return s;
  }
  g_tex_misses++;
  /* decode the block */
  int bytes = 128;
  int blk = rank / BLOCK_TILES, first = blk * BLOCK_TILES;
  int all = tex_tiles(r), ntiles = all - first;
  if (ntiles > BLOCK_TILES) ntiles = BLOCK_TILES;
  /* (the first block after the map and where the others start) */
  const uint8_t *d = sec_tdat + r->off, *starts = d + ((r->flags & TEX_FULL) ? 0 : r->th * row_stride(r));
  const uint8_t *src = blk ? d + rd16(starts + 2 * (blk - 1)) : starts + 2 * ((all - 1) / BLOCK_TILES);
  if (block_tex != t || block_first != first) {   /* (the block decoded last: its tiles still there) */
    uint8_t left[BLOCK_TILES], up[BLOCK_TILES];
    block_geometry(r, first, ntiles, left, up);
    decode_block(src, ntiles, r->fmt == FMT_ALPHA2, left, up, block_buf);
    block_tex = t, block_first = first;
    g_tex_decodes++;
  }
  int want = insert((uint32_t)t << 16 | (uint32_t)rank, block_buf + (rank - first) * bytes, bytes);
  slot_frame[want] = frame, slot_strip[want] = cur_strip;
  /* the block's other tiles, where they push out nothing in use */
  for (int i = 0; i < ntiles; i++) {
    uint32_t k = (uint32_t)t << 16 | (uint32_t)(first + i);
    if (first + i == rank || find(k) >= 0) continue;
    int ns = stale_slot();
    if (ns < 0) break;
    unlink_slot(ns);
    slot_key[ns] = k;
    unsigned hh = hash(k);
    slot_next[ns] = head[hh];
    head[hh] = (uint16_t)ns;
    copy_words(slot_px[ns], block_buf + i * bytes, (size_t)bytes);
    slot_box[ns] = tile_box(block_buf + i * bytes);
    slot_frame[ns] = (uint16_t)(frame - 2);   /* not drawn yet: the first to go */
  }
  return want;
}

uint32_t g_tex_used;   /* tiles drawn last frame */
void tex_frame(void) {
  if (!inited) init();
  g_tex_overload = false;
  g_tex_used = 0;
  for (int i = 0; i < nslots; i++) g_tex_used += slot_frame[i] == frame;
  full = g_tex_used >= (uint32_t)nslots - 16;
  cur_strip = 0;
  frame++;
  if (!frame) frame = 1;
}

#ifdef HOST
#include <stdio.h>
void tex_dump_used(const char *path) {
  FILE *f = fopen(path, "w");
  for (int i = 0; i < NSLOTS; i++)
    if (slot_frame[i] == frame && slot_key[i] != 0xFFFFFFFF) fprintf(f, "%u\n", slot_key[i] >> 16);
  fclose(f);
}
#endif

