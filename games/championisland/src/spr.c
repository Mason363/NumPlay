#include <stdio.h>
#include <stdlib.h>
/* Sprites: LZMA decoding into a cache of run-length rows.
 *
 * data.bin keeps the doodle's images as 8-bit palette indices, compressed
 * with LZMA: small ones in banks of up to 16 KB, large ones (backgrounds) as
 * streams of their own. One decoder with an 8 KB ring dictionary serves both.
 * A sprite about to be drawn is decoded into the cache as rows of runs of
 * visible pixels (transparent ones cost nothing), and stays there until the
 * least recently used sprites have to make room. */
#include "spr.h"
#include "lzma/LzmaDec.h"

/* ---------------------------------------------------------------- LZMA */
static CLzmaProb probs[1984 + 0x300];
#ifdef HOST
static uint8_t ring_mem[LZMA_DICT] __attribute__((aligned(4)));
static uint8_t *ring = ring_mem;
#else
static uint8_t *ring;   /* on main's stack (spr_set_ring), like the nodes */
#endif
void spr_set_ring(uint8_t *mem) { ring = mem; }
static CLzmaDec dec;
static const uint8_t *z_src;
static uint32_t z_in, z_len, z_out, z_total;
static const uint8_t *z_next;   /* a stream's next strip (its record), read on when this one ends */
uint32_t z_epoch;               /* counts z_open: whoever reads on knows if someone else used the decoder */

#ifdef HOST
uint64_t z_bytes, z_opens;   /* tests: how much the decoder did */
#endif
void z_open(uint32_t off, uint32_t clen, uint32_t rawlen) {
  z_epoch++;
  memset(&dec, 0, sizeof dec);
  dec.prop.lc = 0;
  dec.prop.lp = 0;
  dec.prop.pb = 0;
  dec.prop.dicSize = LZMA_DICT;
  dec.probs = probs;
  dec.probs_1664 = probs + 1664;
  dec.numProbs = sizeof probs / sizeof probs[0];
  dec.dic = ring;
  dec.dicBufSize = LZMA_DICT;
  LzmaDec_Init(&dec);
  z_src = ci_data + off;
#ifdef HOST
  z_opens++;
#endif
  z_in = 0;
  z_len = clen;
  z_out = 0;
  z_total = rawlen;
  z_next = NULL;
}

/* A stream is one record of the stream table, or several in a row for a tall
 * image cut into strips (pack.py): read as one, strip after strip. */
void z_open_stream(const uint8_t *st) {
  z_open(rd32(st + 2), rd32(st + 6), rd32(st + 10));
  const uint8_t *end = ci_data + HDR(H_STREAMS) + 14u * HDR(H_NSTREAMS);
  if (st + 14 < end && rd16(st + 14) == rd16(st)) z_next = st + 14;
}

/* Decodes up to n more bytes; they sit in ring[] from *at (never wrapping:
 * a read ends at the end of the ring). Returns how many (0 at the end). */
uint32_t z_read(uint32_t n, const uint8_t **at) {
  if (z_out >= z_total) {
    if (!z_next) return 0;
    z_open_stream(z_next);
  }
  if (dec.dicPos == LZMA_DICT) dec.dicPos = 0;
  uint32_t start = (uint32_t)dec.dicPos, room = LZMA_DICT - start;
  if (n > room) n = room;
  if (n > z_total - z_out) n = z_total - z_out;
  SizeT inlen = z_len - z_in;
  ELzmaStatus st;
  if (LzmaDec_DecodeToDic(&dec, start + n, z_src + z_in, &inlen, LZMA_FINISH_ANY, &st) != SZ_OK) {
    z_out = z_total;
    return 0;
  }
  z_in += (uint32_t)inlen;
  uint32_t got = (uint32_t)dec.dicPos - start;
  z_out += got;
#ifdef HOST
  z_bytes += got;
#endif
  *at = ring + start;
  return got;
}

/* Copies the next n decoded bytes into dst (NULL: skips them). */
bool z_get(uint8_t *dst, uint32_t n) {
  while (n) {
    const uint8_t *p;
    uint32_t got = z_read(n, &p);
    if (!got) return false;
    if (dst) {
      memcpy(dst, p, got);
      dst += got;
    }
    n -= got;
  }
  return true;
}

/* the decoder's ring when no stream is being read: scratch memory for others */
uint8_t *z_scratch(uint32_t *size) { *size = LZMA_DICT; return ring; }

/* ---------------------------------------------------------------- the cache */
#define ENTRIES 160
/* off in 4-byte units, used: the tick it was last drawn (16 bits, compared as ages) */
typedef struct { uint16_t spr, off4, size, used; } Entry;
static uint8_t *mem;
static uint32_t cache_bytes;
static Entry ent[ENTRIES];
static uint16_t nent;
static uint8_t hint[64];            /* sprite & 63 -> an entry to try first */
static uint32_t top, tick;

void spr_setup(uint8_t *m, uint32_t size) {
  mem = m;
  cache_bytes = size;
  nent = 0;
  top = 0;
}

void spr_reset(void) {
  nent = 0;
  top = 0;
}

/* entry + 1 of a cached sprite, 0 if not cached */
static unsigned find(uint16_t sp) {
  unsigned h = hint[sp & 63];
  if (h < nent && ent[h].spr == sp) return h + 1;
  for (unsigned i = 0; i < nent; i++)
    if (ent[i].spr == sp) { hint[sp & 63] = (uint8_t)i; return i + 1; }
  return 0;
}

void spr_tick(void) { tick++; }

static void compact(void) {
  /* entries sorted by offset: slide each down */
  for (int i = 1; i < nent; i++) {
    Entry e = ent[i];
    int j = i - 1;
    while (j >= 0 && ent[j].off4 > e.off4) { ent[j + 1] = ent[j]; j--; }
    ent[j + 1] = e;
  }
  uint32_t at = 0;
  for (int i = 0; i < nent; i++) {
    if (4u * ent[i].off4 != at) memmove(mem + at, mem + 4u * ent[i].off4, ent[i].size);
    ent[i].off4 = (uint16_t)(at / 4);
    at += (ent[i].size + 3) & ~3u;
  }
  top = at;
}

static inline uint16_t age(const Entry *e) { return (uint16_t)((uint16_t)tick - e->used); }
/* what getting an entry back costs for each byte of room it takes: its
 * sprite's pixels (a mask's too) decoded again, with what comes before them
 * in their bank (16ths of a byte); a tiny sprite at the end of a bank costs
 * the most */
static uint32_t regain(const Entry *e) {
  Sprite s;
  sprite_info((uint16_t)(e->spr & 0x7FFF), &s);
  return (((s.kind ? 0 : s.off) + (uint32_t)s.w * s.h) << 4) / (e->size + 4u);
}

/* the cache the entries used this tick (keep = 0) or the last too (keep = 1) take */
static uint32_t fresh_bytes(unsigned keep, unsigned *count) {
  uint32_t n = 0;
  unsigned c = 0;
  for (unsigned i = 0; i < nent; i++)
    if (age(&ent[i]) <= keep) { n += (ent[i].size + 3) & ~3u; c++; }
  if (count) *count = c;
  return n;
}

/* Room for n bytes; evicts only entries not used this tick or the last
 * (keep = 1), or this tick (keep = 0), unless forced. */
static uint8_t *alloc_keep(uint32_t n, bool force, unsigned keep) {
  n = (n + 3) & ~3u;
  if (n > cache_bytes) return NULL;
  if (top + n > cache_bytes || nent >= ENTRIES) {
    uint32_t live = 0;
    for (int i = 0; i < nent; i++) live += (ent[i].size + 3) & ~3u;
    while (nent && (live + n > cache_bytes || nent >= ENTRIES)) {
      /* the oldest goes; of the same age (what this very frame draws, in a
       * frame bigger than the cache), the one cheapest to get back */
      int old = 0;
      uint32_t r = regain(&ent[0]);
      for (int i = 1; i < nent; i++) {
        if (age(&ent[i]) < age(&ent[old])) continue;
        uint32_t ri = regain(&ent[i]);
        if (age(&ent[i]) > age(&ent[old]) || ri < r) { old = i; r = ri; }
      }
      if (!force && age(&ent[old]) <= keep) return NULL;
      live -= (ent[old].size + 3) & ~3u;
      ent[old] = ent[--nent];          /* that one (its size is what live lost) */
    }
    /* only when the space is short: a full entry table needs no moving */
    if (top + n > cache_bytes) compact();
  }
  if (top + n > cache_bytes) return NULL;
  uint8_t *p = mem + top;
  top += n;
  return p;
}
static uint8_t *alloc(uint32_t n, bool force) { return alloc_keep(n, force, 1); }

/* Encodes rows of palette indices as runs of visible pixels:
 * u16 w, u16 h, u16 row offsets[h] (from the start), then per row: u16 nruns,
 * and per run: skip (transparent pixels before it), then either a fill
 * (0x80 | (length - 1), one colour: 1 to 128 pixels of one colour) or
 * literal pixels (length 0..127, then the colours). pack.py's rle_size()
 * computes the same sizes. */
typedef struct { uint8_t *base, *p, *end; uint16_t row; bool ok; } Rle;
static void rle_row(Rle *r, const uint8_t *px, int w, const uint8_t *alpha) {
  if (!r->ok) return;
  uint32_t off = (uint32_t)(r->p - r->base);
  r->base[4 + 2 * r->row] = (uint8_t)off;
  r->base[5 + 2 * r->row] = (uint8_t)(off >> 8);
  r->row++;
  if (r->p + 2 > r->end) { r->ok = false; return; }
  uint8_t *count = r->p;
  r->p += 2;
  int n = 0, x = 0;
  while (x < w) {
    int skip = 0;
    while (x < w && !alpha[px[x]]) { x++; skip++; }
    if (x >= w) break;
    while (skip > 255) {         /* a long gap: runs of nothing */
      if (r->p + 2 > r->end) { r->ok = false; return; }
      *r->p++ = 255; *r->p++ = 0; skip -= 255; n++;
    }
    int run = 1;
    while (x + run < w && run < 128 && px[x + run] == px[x]) run++;
    if (run >= 3) {
      if (r->p + 3 > r->end) { r->ok = false; return; }
      *r->p++ = (uint8_t)skip;
      *r->p++ = (uint8_t)(0x80 | (run - 1));
      *r->p++ = px[x];
      x += run;
    } else {
      int len = 0;
      while (x + len < w && len < 127 && alpha[px[x + len]] &&
             !(x + len + 2 < w && px[x + len] == px[x + len + 1] && px[x + len] == px[x + len + 2]))
        len++;
      if (!len) len = 1;
      if (r->p + 2 + len > r->end) { r->ok = false; return; }
      *r->p++ = (uint8_t)skip;
      *r->p++ = (uint8_t)len;
      memcpy(r->p, px + x, (size_t)len);
      r->p += len;
      x += len;
    }
    n++;
  }
  count[0] = (uint8_t)n;
  count[1] = (uint8_t)(n >> 8);
}
/* Worst-case size of a sprite's runs. */
static uint32_t rle_bound(int w, int h) { return 4 + 2u * h + (uint32_t)h * (2 + 3 * ((uint32_t)w / 2 + 2) + (uint32_t)w); }
uint8_t spr_rowbuf[SPRITE_W_MAX];   /* one decoded row (shared with gfx.c) */
#define rowbuf spr_rowbuf

/* Adds sprite sp from the decoder, whose next bytes are its pixels. */
static bool add_from_decoder(uint16_t sp, const Sprite *s, bool force) {
  const uint8_t *alpha = palalpha(s->sheet);
  uint32_t bound = s->rle ? s->rle : rle_bound(s->w, s->h);   /* the packer measured it */
  if (bound > cache_bytes) { z_get(NULL, (uint32_t)s->w * s->h); return false; }
  uint8_t *p = alloc(bound, force);
  if (!p) { z_get(NULL, (uint32_t)s->w * s->h); return false; }
  Rle r = {p, p + 4 + 2 * s->h, p + bound, 0, true};
  p[0] = (uint8_t)s->w; p[1] = (uint8_t)(s->w >> 8); p[2] = (uint8_t)s->h; p[3] = (uint8_t)(s->h >> 8);
  for (int y = 0; y < s->h; y++) {
    if (!z_get(rowbuf, s->w)) r.ok = false;
    rle_row(&r, rowbuf, s->w, alpha);
  }
  uint32_t used = (uint32_t)(r.p - p);
  top -= (bound + 3) & ~3u;          /* give back what the runs did not need */
  if (!r.ok) return false;
  top += (used + 3) & ~3u;
  /* sprites decoded on the way (bank neighbours) count as older than any in use */
  if (used > 0xFFFF) { top -= (used + 3) & ~3u; return false; }
  ent[nent++] = (Entry){sp, (uint16_t)((p - mem) / 4), (uint16_t)used, (uint16_t)(force ? tick : tick - 1)};
  return true;
}

/* sprites kept outside the cache by a scene (the ending's glow: small runs,
 * but a whole screen to decode again each time the cache lets it go) */
#define PINS 2
static struct { uint16_t sp; const uint8_t *rle; } pins[PINS];
void spr_pin(uint16_t sp, const uint8_t *rle) {
  for (int i = 0; i < PINS; i++)
    if (pins[i].rle && pins[i].sp == sp) pins[i].rle = NULL;
  for (int i = 0; rle && i < PINS; i++)
    if (!pins[i].rle) { pins[i].sp = sp; pins[i].rle = rle; return; }
}
static const uint8_t *pinned(uint16_t sp) {
  for (int i = 0; i < PINS; i++)
    if (pins[i].rle && pins[i].sp == sp) return pins[i].rle;
  return NULL;
}
bool spr_pinned(uint16_t sp) { return pinned(sp) != NULL; }

/* A big sprite's opaque inner rectangle (a menu's panel: what it covers is not
 * drawn), measured once from its pixels: the rows around the middle one whose
 * middle pixel is opaque, and the opaque run through it that they all share.
 * box: x0, y0, x1, y1 in the sprite (excluded ends); false if none. */
#define BOXES 8
static struct { uint16_t sp; int16_t b[4]; } boxes[BOXES];
static int nboxes, box_next;
bool spr_opaque_box(uint16_t sp, int16_t box[4]) {
  for (int i = 0; i < nboxes; i++)
    if (boxes[i].sp == sp) { memcpy(box, boxes[i].b, sizeof boxes[i].b); return box[2] > box[0] && box[3] > box[1]; }
  Sprite s;
  sprite_info(sp, &s);
  if (s.w > SPRITE_W_MAX || !spr_rows_open(sp, &s)) return false;
  const uint8_t *al = palalpha(s.sheet);
  int cx = s.w / 2, cy = s.h / 2, x0 = 0, x1 = s.w, y0 = -1, y1 = -1;
  for (int y = 0; y < s.h; y++) {
    if (!z_get(rowbuf, s.w)) { y0 = -1; break; }
    if (al[rowbuf[cx]] != 255) {
      if (y > cy) break;             /* the block around the middle row ended */
      y0 = -1;                       /* a block above the middle: not it */
      continue;
    }
    int a = cx, b = cx + 1;
    while (a > 0 && al[rowbuf[a - 1]] == 255) a--;
    while (b < s.w && al[rowbuf[b]] == 255) b++;
    if (y0 < 0) { y0 = y; x0 = a; x1 = b; }
    else { x0 = a > x0 ? a : x0; x1 = b < x1 ? b : x1; }
    y1 = y + 1;
  }
  int16_t b[4] = {(int16_t)x0, (int16_t)(y0 < 0 ? 0 : y0), (int16_t)x1, (int16_t)(y0 < 0 ? 0 : y1)};
  int i = nboxes < BOXES ? nboxes++ : box_next++ % BOXES;
  boxes[i].sp = sp;
  memcpy(boxes[i].b, b, sizeof b);
  memcpy(box, b, sizeof b);
  return b[2] > b[0] && b[3] > b[1];
}

const uint8_t *spr_get(uint16_t sp) {
  if (sp >= SPRITE_COUNT || !mem) return NULL;
  const uint8_t *pn = pinned(sp);
  if (pn) return pn;
  unsigned f = find(sp);
  if (f) {
    Entry *e = &ent[f - 1];
    e->used = (uint16_t)tick;
    return mem + 4u * e->off4;
  }
  Sprite s;
  sprite_info(sp, &s);
#ifdef HOST
  if (getenv("CI_SPR_MISS")) fprintf(stderr, "miss %u kind %d %dx%d rle %u bank %u\n", sp, s.kind, s.w, s.h, (unsigned)s.rle, (unsigned)s.bank);
#endif
  if (s.kind == 1) {
    const uint8_t *st = spr_stream(sp);
    if (!st) return NULL;
    z_open_stream(st);
    add_from_decoder(sp, &s, true);
  } else {
    const uint8_t *b = ci_data + HDR(H_BANKS) + 12u * s.bank;
    z_open(rd32(b), rd32(b + 4), rd32(b + 8));
    const uint8_t *m = ci_data + rd32(ci_data + HDR(H_BANKSPR) + 4u * s.bank);
    unsigned n = rd16(m), pos = 0;
    for (unsigned i = 0; i < n; i++) {
      uint16_t o = rd16(m + 2 + 2 * i);
      Sprite t;
      sprite_info(o, &t);
      if (t.off > pos) z_get(NULL, t.off - pos);
      pos = t.off + (uint32_t)t.w * t.h;
      if (find(o) || (o != sp && (nent >= ENTRIES || top + (t.rle ? t.rle : rle_bound(t.w, t.h)) > cache_bytes))) {
        z_get(NULL, (uint32_t)t.w * t.h);
        continue;
      }
      add_from_decoder(o, &t, o == sp);
      if (o == sp && !find(sp)) break;
    }
  }
  unsigned f2 = find(sp);
  return f2 ? mem + 4u * ent[f2 - 1].off4 : NULL;
}

/* spr_get, only if what this frame drew so far can stay */
const uint8_t *spr_get_soft(uint16_t sp) {
  const uint8_t *r = spr_peek(sp);
  if (r || sp >= SPRITE_COUNT || !mem) return r;
  Sprite s;
  sprite_info(sp, &s);
  if (fresh_bytes(0, NULL) + (((s.rle ? s.rle : rle_bound(s.w, s.h)) + 3) & ~3u) > cache_bytes) return NULL;
  return spr_get(sp);
}

/* the cached runs if the sprite is in the cache now (never decodes) */
const uint8_t *spr_peek(uint16_t sp) {
  const uint8_t *pn = pinned(sp);
  if (pn) return pn;
  unsigned f = sp < SPRITE_COUNT && mem ? find(sp) : 0;
  if (!f) return NULL;
  Entry *e = &ent[f - 1];
  e->used = (uint16_t)tick;
  return mem + 4u * e->off4;
}

bool spr_rows_open(uint16_t sp, const Sprite *s) {
  if (s->kind == 1) {
    const uint8_t *st = spr_stream(sp);
    if (!st) return false;
    z_open_stream(st);
    return true;
  }
  if (s->kind != 0) return false;
  const uint8_t *b = ci_data + HDR(H_BANKS) + 12u * s->bank;
  z_open(rd32(b), rd32(b + 4), rd32(b + 8));
  return z_get(NULL, s->off);
}

/* spr_rows_open at row `row`: a stream in strips starts from the strip holding it */
bool spr_rows_at(uint16_t sp, const Sprite *s, int row) {
  const uint8_t *st = s->kind == 1 ? spr_stream(sp) : NULL;
  if (!st) return spr_rows_open(sp, s) && z_get(NULL, (uint32_t)row * s->w);
  const uint8_t *end = ci_data + HDR(H_STREAMS) + 14u * HDR(H_NSTREAMS);
  uint32_t skip = (uint32_t)row * s->w;
  while (st + 14 < end && rd16(st + 14) == sp && skip >= rd32(st + 10)) {
    skip -= rd32(st + 10);
    st += 14;
  }
  z_open_stream(st);
  return z_get(NULL, skip);
}

bool spr_room(uint32_t n) {
  n = (n + 3) & ~3u;
  if (!mem || n > cache_bytes) return false;
  unsigned nfresh;
  return fresh_bytes(1, &nfresh) + n <= cache_bytes && nfresh < ENTRIES;
}

const uint8_t *spr_stream(uint16_t sp) {
  uint32_t n = HDR(H_NSTREAMS);
  const uint8_t *t = ci_data + HDR(H_STREAMS);
  for (uint32_t i = 0; i < n; i++, t += 14)
    if (rd16(t) == sp) return t;
  return NULL;
}

/* ---------------------------------------------------------------- masks */
/* A sprite's fully opaque pixels as spans, kept in the cache as entry
 * sprite | 0x8000: u16 w, u16 h, u16 row offsets[h], then per row: u16 n,
 * and n spans of (skip, len) bytes (a skip over 255 goes as (255, 0)).
 * Scenery in front of the actors is redrawn from the layer through these:
 * far smaller than the sprite's colours. */
#define MASK 0x8000u
typedef struct { uint8_t *out; uint32_t at, n; int last; } MaskOut;
static void mask_span(MaskOut *o, int a, int b) {
  int gap = a - o->last, l = b - a;
  while (gap > 255) {
    if (o->out) { o->out[o->at] = 255; o->out[o->at + 1] = 0; }
    o->at += 2; o->n++; gap -= 255;
  }
  while (l > 0) {
    int part = l > 255 ? 255 : l;
    if (o->out) { o->out[o->at] = (uint8_t)gap; o->out[o->at + 1] = (uint8_t)part; }
    o->at += 2; o->n++; gap = 0; l -= part;
  }
  o->last = b;
}
/* the mask of a sprite's runs r into out (NULL: only its size), returns the size */
static uint32_t mask_build(const uint8_t *r, const uint8_t *al, uint8_t *out) {
  int w = rd16(r), h = rd16(r + 2);
  MaskOut o = {out, 4 + 2u * (uint32_t)h, 0, 0};
  for (int y = 0; y < h; y++) {
    if (out) { out[4 + 2 * y] = (uint8_t)o.at; out[5 + 2 * y] = (uint8_t)(o.at >> 8); }
    uint32_t cnt_at = o.at;
    o.at += 2;
    o.n = 0;
    o.last = 0;
    const uint8_t *p = r + rd16(r + 4 + 2 * y);
    int runs = rd16(p), x = 0, s0 = -1;
    p += 2;
    for (int k = 0; k < runs; k++) {
      int skip = *p++, code = *p++;
      bool fill = code & 0x80;
      int len = fill ? (code & 0x7F) + 1 : code;
      const uint8_t *px = p;
      p += fill ? 1 : len;
      if (skip && s0 >= 0) { mask_span(&o, s0, x); s0 = -1; }
      x += skip;
      for (int i = 0; i < len; i++, x++) {
        bool op = al[fill ? px[0] : px[i]] == 255;
        if (op && s0 < 0) s0 = x;
        else if (!op && s0 >= 0) { mask_span(&o, s0, x); s0 = -1; }
      }
    }
    if (s0 >= 0) mask_span(&o, s0, x);
    if (out) { out[cnt_at] = (uint8_t)o.n; out[cnt_at + 1] = (uint8_t)(o.n >> 8); }
  }
  if (out) { out[0] = (uint8_t)w; out[1] = (uint8_t)(w >> 8); out[2] = (uint8_t)h; out[3] = (uint8_t)(h >> 8); }
  return o.at;
}

/* the same from the decoder's rows (the sprite is not in the cache) */
static uint32_t mask_rows(uint16_t sp, const Sprite *s, const uint8_t *al, uint8_t *out) {
#ifdef HOST
  if (getenv("CI_SPR_MISS")) fprintf(stderr, "maskrows %u %dx%d rle %u out %d\n", sp, s->w, s->h, (unsigned)s->rle, out != NULL);
#endif
  if (!spr_rows_open(sp, s)) return 0;
  MaskOut o = {out, 4 + 2u * s->h, 0, 0};
  for (int y = 0; y < s->h; y++) {
    if (!z_get(rowbuf, s->w)) return 0;
    if (out) { out[4 + 2 * y] = (uint8_t)o.at; out[5 + 2 * y] = (uint8_t)(o.at >> 8); }
    uint32_t cnt_at = o.at;
    o.at += 2;
    o.n = 0;
    o.last = 0;
    int s0 = -1;
    for (int x = 0; x < s->w; x++) {
      bool op = al[rowbuf[x]] == 255;
      if (op && s0 < 0) s0 = x;
      else if (!op && s0 >= 0) { mask_span(&o, s0, x); s0 = -1; }
    }
    if (s0 >= 0) mask_span(&o, s0, s->w);
    if (out) { out[cnt_at] = (uint8_t)o.n; out[cnt_at + 1] = (uint8_t)(o.n >> 8); }
  }
  if (out) { out[0] = (uint8_t)s->w; out[1] = (uint8_t)(s->w >> 8); out[2] = (uint8_t)s->h; out[3] = (uint8_t)(s->h >> 8); }
  return o.at;
}

/* the sizes of masks made before: one the cache let go is made again from
 * the decoder in one pass */
#define KNOWN 32
typedef struct { uint16_t sp, size; } Known;
static Known known[KNOWN];
static unsigned known_next;
static uint32_t mask_known(uint16_t sp) {
  for (int i = 0; i < KNOWN; i++)
    if (known[i].sp == sp && known[i].size) return known[i].size;
  return 0;
}
static void mask_learn(uint16_t sp, uint32_t n) {
  if (n && n <= 0xFFFF && !mask_known(sp)) known[known_next++ % KNOWN] = (Known){sp, (uint16_t)n};
}

const uint8_t *spr_mask(uint16_t sp) {
  if (sp >= SPRITE_COUNT || !mem) return NULL;
  unsigned f = find((uint16_t)(sp | MASK));
  if (f) {
    ent[f - 1].used = (uint16_t)tick;
    return mem + 4u * ent[f - 1].off4;
  }
  Sprite s;
  sprite_info(sp, &s);
  const uint8_t *al = palalpha(s.sheet);
  bool had = find(sp);
  if (!had && !spr_room(s.rle ? s.rle : rle_bound(s.w, s.h))) {
    /* no room for the sprite: its mask straight from the decoder, measured
     * (once: the size is kept, and frames count it) then written, when what
     * the frame draws leaves room for it */
    uint32_t n = mask_known(sp);
    if (!n) mask_learn(sp, n = mask_rows(sp, &s, al, NULL));
    if (!n || n > 0xFFFF || fresh_bytes(0, NULL) + n > cache_bytes) return NULL;
    uint8_t *m = alloc_keep(n, false, 0);   /* (a full table lets an older entry go) */
    if (!m || mask_rows(sp, &s, al, m) != n || nent >= ENTRIES) return NULL;
    ent[nent++] = (Entry){(uint16_t)(sp | MASK), (uint16_t)((uint32_t)(m - mem) / 4), (uint16_t)n, (uint16_t)tick};
    return m;
  }
  if (!spr_get(sp)) return NULL;
  uint32_t n = mask_build(mem + 4u * ent[find(sp) - 1].off4, al, NULL);
  if (n > 0xFFFF) return NULL;
  mask_learn(sp, n);
  uint8_t *m = alloc_keep(n, false, 0);   /* may move the sprite (compaction): look it up again */
  unsigned fs = find(sp);
  if (fs && !had) ent[fs - 1].used = (uint16_t)(tick - 2);   /* decoded for its mask only: the first to go */
  if (!m || !fs) return NULL;
  mask_build(mem + 4u * ent[fs - 1].off4, al, m);
  if (nent >= ENTRIES) return NULL;
  ent[nent++] = (Entry){(uint16_t)(sp | MASK), (uint16_t)((uint32_t)(m - mem) / 4), (uint16_t)n, (uint16_t)tick};
  return m;
}

/* the room a sprite's mask takes: its size once made, else a guess (a
 * quarter of the sprite's runs, about what scenery's masks take) */
uint32_t spr_mask_bytes(uint16_t sp) {
  uint32_t n = mask_known(sp);
  if (n) return (n + 3) & ~3u;
  Sprite s;
  sprite_info(sp, &s);
  return ((s.rle ? s.rle : rle_bound(s.w, s.h)) / 4 + 3) & ~3u;
}

const uint8_t *spr_peek_mask(uint16_t sp) {
  unsigned f = sp < SPRITE_COUNT && mem ? find((uint16_t)(sp | MASK)) : 0;
  return f ? mem + 4u * ent[f - 1].off4 : NULL;
}

/* a sprite this frame needs no more (its last band is drawn): the first to go */
void spr_release(uint16_t sp) {
  unsigned f = sp < SPRITE_COUNT && mem ? find(sp) : 0;
  if (f) ent[f - 1].used = (uint16_t)(tick - 2);
  f = sp < SPRITE_COUNT && mem ? find((uint16_t)(sp | MASK)) : 0;
  if (f) ent[f - 1].used = (uint16_t)(tick - 2);
}

uint32_t spr_cache_used(void) { return top; }
uint32_t spr_capacity(void) { return cache_bytes; }
