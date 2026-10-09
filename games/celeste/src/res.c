/* data.bin: sections, textures, palettes, and the texture cache.
 *
 * Textures are either "direct" (row RLE in flash, drawn from there) or
 * packed: LZMA streams of raw 8-bit pixels (see tools/pack.py), decoded
 * through a small ring buffer when a room needs them and kept in the cache as
 * row RLE (tile sheets stay raw: tiles are cut from them). */
#include "celeste.h"
#include "lzma/LzmaDec.h"

const uint8_t *cel_bin;

const uint8_t *section(int s) { return cel_bin + rd32(cel_bin + 4 + 4 * s); }

const char *str(uint16_t id) {
  const uint8_t *s = section(SEC_STRINGS);
  if (id >= rd32(s)) return "";
  return (const char *)s + rd32(s + 4 + 4 * id);
}

const uint16_t *pal_colors(uint16_t pal, int *n) {
  const uint8_t *s = section(SEC_PAL);
  const uint8_t *p = s + rd32(s + 4 + 4 * pal);
  if (n) *n = rd16(p) & 0x7FFF;
  return (const uint16_t *)(const void *)(p + 2);
}
const uint8_t *pal_alpha(uint16_t pal) {
  const uint8_t *s = section(SEC_PAL);
  const uint8_t *p = s + rd32(s + 4 + 4 * pal);
  return p + 2 + 2 * (rd16(p) & 0x7FFF);
}
bool pal_opaque(uint16_t pal) {
  const uint8_t *s = section(SEC_PAL);
  return rd16(s + rd32(s + 4 + 4 * pal)) >> 15;
}

/* ---------------------------------------------------------------- LZMA streams */
#define DICT_SIZE 9600   /* (tools/pack.py DICT: the strip, all of it) */
extern uint16_t *g_strip;          /* gfx.c: the ring buffer borrows the strip */
#define NPROBS (1984 + 768)         /* NUM_BASE_PROBS + LZMA_LIT_SIZE << (lc + lp) */

static void pack_info(uint16_t p, const uint8_t **data, uint32_t *comp, uint32_t *raw) {
  const uint8_t *s = section(SEC_PACKS) + 4 + 12 * p;
  *data = section(SEC_BLOBS) + rd32(s);
  *comp = rd32(s + 4);
  *raw = rd32(s + 8);
}

/* decodes pack p from its start, giving bytes [from, to) to sink (until *stop, if given) */
static void lz_run(uint16_t p, uint32_t from, uint32_t to, Sink sink, void *ctx, const bool *stop) {
  const uint8_t *src;
  uint32_t comp, raw;
  pack_info(p, &src, &comp, &raw);
  if (to > raw) to = raw;
  /* (its probabilities on the stack, 5.5 KB of the calculator's 32: older calculator software gives apps less RAM) */
  CLzmaProb probs[NPROBS];
  CLzmaDec dec;
  memset(&dec, 0, sizeof dec);
  dec.prop.lc = 0, dec.prop.lp = 0, dec.prop.pb = 0, dec.prop.dicSize = DICT_SIZE;
  dec.probs = probs;
  dec.probs_1664 = probs + 1664;
  dec.numProbs = NPROBS;
  dec.dic = (Byte *)(void *)g_strip;
  dec.dicBufSize = DICT_SIZE;
  LzmaDec_Init(&dec);
  uint32_t pos = 0, in = 0;
  while (pos < to && !(stop && *stop)) {
    if (dec.dicPos == dec.dicBufSize) dec.dicPos = 0;
    SizeT start = dec.dicPos, inlen = comp - in;
    ELzmaStatus st;
    SizeT limit = dec.dicBufSize;
    if (limit - start > 1024) limit = start + 1024;   /* (a bit at a time: it can stop sooner) */
    if (limit - start > to - pos) limit = start + (to - pos);
    if (LzmaDec_DecodeToDic(&dec, limit, src + in, &inlen, LZMA_FINISH_ANY, &st) != SZ_OK) return;
    in += (uint32_t)inlen;
    uint32_t got = (uint32_t)(dec.dicPos - start);
    if (!got) return;
    /* the part of [pos, pos + got) inside [from, to) */
    uint32_t a = pos < from ? from : pos, b = pos + got;
    if (b > a) sink(dec.dic + start + (a - pos), b - a, ctx);
    pos += got;
  }
}

void res_stream(uint16_t pack, uint32_t off, uint32_t size, Sink sink, void *ctx) {
  lz_run(pack, off, off + size, sink, ctx, NULL);
}

/* ---------------------------------------------------------------- the cache */
#define CACHE_BYTES (46 * 1024)
#define HASH 512
typedef struct {
  uint16_t tex, pal;
  uint16_t w, h;
  int16_t ox, oy;
  uint16_t fw, fh;
  uint8_t fmt, mark;   /* mark: wanted by the rooms now (marks_gen); 0: evicted */
  uint8_t scale, pad;
  uint16_t used;       /* the frame it was last drawn */
  uint16_t bytes;      /* data after this header */
} CEnt;
static uint32_t cent_map_bytes(const CEnt *e) {   /* a TF_NIB texture's colors at the start of its data */
  return e->fmt & TF_NIB ? (1u + ((const uint8_t *)(e + 1))[0] + 1) & ~1u : 0;
}
static uint32_t cache_words[CACHE_BYTES / 4];
static uint32_t cache_limit = CACHE_BYTES;   /* below the chapter's own memory (res_chapter_ram) */
#define cache ((uint8_t *)cache_words)
static uint32_t cache_top;
static uint16_t hash_tex[HASH];
static uint16_t hash_at[HASH];     /* offset / 4 */
static uint8_t marks_gen = 1;
uint16_t g_res_frame;        /* gfx_begin counts frames: what was drawn this frame stays while drawing */
bool g_res_drawing;          /* between gfx_begin and gfx_end */
bool g_res_can_load = true;  /* false while drawing strips: the LZMA ring is the strip */
static bool load_one(uint16_t id);
static struct { uint16_t id, frame; } failed[8];   /* loads that found no room, lately */
static int nfailed;

static uint32_t tex_entry(uint16_t id) { return rd32(section(SEC_TEX) + 4 + 4 * id); }
int tex_half_extent(uint16_t id) {
  int v = section(SEC_TEXDIM)[id];
  return v == 255 ? 4096 : v * 2;
}

static int hash_find(uint16_t tex) {
  uint32_t h = (tex * 2654435761u) >> 23;
  for (int i = 0; i < HASH; i++) {
    uint32_t k = (h + i) & (HASH - 1);
    if (hash_tex[k] == 0xFFFF) return -1;
    if (hash_tex[k] == tex) return (int)k;
  }
  return -1;
}
static void hash_put(uint16_t tex, uint32_t at) {
  uint32_t h = (tex * 2654435761u) >> 23;
  for (int i = 0; i < HASH; i++) {
    uint32_t k = (h + i) & (HASH - 1);
    if (hash_tex[k] == 0xFFFF || hash_tex[k] == tex) {
      hash_tex[k] = tex;
      hash_at[k] = (uint16_t)(at / 4);
      return;
    }
  }
}
static void hash_rebuild(void) {
  memset(hash_tex, 0xFF, sizeof hash_tex);
  for (uint32_t at = 0; at < cache_top;) {
    CEnt *e = (CEnt *)(void *)(cache + at);
    hash_put(e->tex, at);
    at += (sizeof(CEnt) + e->bytes + 3) & ~3u;
  }
}

void res_init(void) {
  cache_top = 0;
  hash_rebuild();
}

bool tex_get(uint16_t id, Tex *t) {
  if (id >= rd16(section(SEC_TEX))) return false;   /* none (0xFFFF) */
  uint32_t e = tex_entry(id);
  if (e == 0xFFFFFFFF) return false;                 /* not in this data */
  if (e >> 30 == 1) {
    const uint8_t *p = section(SEC_DIRECT) + (e & 0x3FFFFFFF);
    t->w = rd16(p), t->h = rd16(p + 2);
    t->ox = rds16(p + 4), t->oy = rds16(p + 6);
    t->fw = rd16(p + 8), t->fh = rd16(p + 10);
    t->pal = rd16(p + 12);
    t->fmt = p[14];
    t->scale = p[15];
    t->px = p + 20;
    t->map = NULL;
    if (t->fmt & TF_NIB) t->map = t->px, t->px += 1 + t->map[0];
    if (t->fmt & TF_SHARED) t->px = section(SEC_DIRECT) + rd32(t->px), t->fmt &= ~TF_SHARED;   /* another's pixels */
    else if ((t->fmt & TF_NIB) && !(t->fmt & TF_ROW8) && ((1 + t->map[0]) & 1)) t->px++;
    return true;
  }
  int k = hash_find(id);
  if (k < 0) {   /* not loaded: now, if it can be (not again for a while when it could not: no room) */
    for (int i = 0; i < 8; i++)
      if (failed[i].id == id && (uint16_t)(g_res_frame - failed[i].frame) < 30) return false;
    if (!g_res_can_load) return false;
    if (!load_one(id) || (k = hash_find(id)) < 0) {
      failed[nfailed].id = id, failed[nfailed].frame = g_res_frame;
      nfailed = (nfailed + 1) & 7;
      return false;
    }
  }
  CEnt *c = (CEnt *)(void *)(cache + hash_at[k] * 4);
  c->used = g_res_frame;
  t->w = c->w, t->h = c->h, t->ox = c->ox, t->oy = c->oy, t->fw = c->fw, t->fh = c->fh;
  t->pal = c->pal, t->fmt = c->fmt;
  t->scale = c->scale ? c->scale : 1;
  t->px = (const uint8_t *)(c + 1);
  t->map = NULL;
  if (c->fmt & TF_NIB) t->map = t->px, t->px += cent_map_bytes(c);
  return true;
}

/* ---------------------------------------------------------------- what rooms need */
#define MAX_MISSING 512
static uint16_t missing[MAX_MISSING];
static int nmissing;

void res_begin_need(void) {
  marks_gen++;
  if (marks_gen < 2) marks_gen = 2;   /* 0: evicted, 1: loaded, not wanted */
  nmissing = 0;
}

void res_need(uint16_t id) {
  if (id >= rd16(section(SEC_TEX))) return;
  uint32_t e = tex_entry(id);
  if (e >> 30 == 1 || e == 0xFFFFFFFF) return;
  int k = hash_find(id);
  if (k >= 0) {
    ((CEnt *)(void *)(cache + hash_at[k] * 4))->mark = marks_gen;
    return;
  }
  for (int i = 0; i < nmissing; i++)
    if (missing[i] == id) return;
  if (nmissing < MAX_MISSING) missing[nmissing++] = id;
}

void res_need_list(const uint8_t *ids, int n) {
  for (int i = 0; i < n; i++) res_need(rd16(ids + 2 * i));
}

/* slides the entries still alive (mark != 0) down to the bottom */
uint16_t g_res_gen;   /* changes whenever textures can move in the cache (drawing's row lookups start over) */
static void compact(void) {
  uint32_t dst = 0;
  g_res_gen++;
  for (uint32_t at = 0; at < cache_top;) {
    CEnt *e = (CEnt *)(void *)(cache + at);
    uint32_t size = (sizeof(CEnt) + e->bytes + 3) & ~3u;
    if (e->mark) {
      if (dst != at) memmove(cache + dst, cache + at, size);
      dst += size;
    }
    at += size;
  }
  cache_top = dst;
  hash_rebuild();
}

/* frees `need` bytes at the top: what was drawn longest ago goes first (what the rooms want gets 8 frames more; of those
 * drawn as long ago, a tile sheet last, the biggest first), never what was drawn this frame; false if that is not enough,
 * and then nothing goes unless `partial` (a texture that could not be loaded used to empty the cache for nothing: what
 * had been drawn came back evicting the rest) */
static bool make_room_ex(uint32_t need, bool force, bool partial) {
  if (cache_top + need <= cache_limit) return true;
  uint32_t freed = 0, want = cache_top + need - cache_limit, can = 0;
  /* what this frame draws stays while it is drawn, unless a tile sheet needs the room */
#define MAY_GO(e) ((e)->mark && !(g_res_drawing && (e)->used == g_res_frame && (!force || (e)->fmt == TF_RAW4)))
  for (uint32_t at = 0; at < cache_top;) {
    CEnt *e = (CEnt *)(void *)(cache + at);
    at += (sizeof(CEnt) + e->bytes + 3) & ~3u;
    if (MAY_GO(e)) can += (sizeof(CEnt) + e->bytes + 3) & ~3u;
  }
  if (can < want && !partial) return false;
  while (freed < want) {
    CEnt *old = NULL;
    uint32_t oldest = 0;
    for (uint32_t at = 0; at < cache_top;) {
      CEnt *e = (CEnt *)(void *)(cache + at);
      at += (sizeof(CEnt) + e->bytes + 3) & ~3u;
      if (!MAY_GO(e)) continue;
      uint32_t age = ((uint32_t)(uint16_t)(g_res_frame - e->used) * 2u + (e->mark == marks_gen ? 0 : 17u)) * 2u + (e->fmt != TF_RAW4);
      if (!old || age > oldest || (age == oldest && e->bytes > old->bytes)) old = e, oldest = age;   /* (the biggest: fewer go) */
    }
    if (!old) break;
    old->mark = 0;
    freed += (sizeof(CEnt) + old->bytes + 3) & ~3u;
  }
  compact();
  return cache_top + need <= cache_limit;
#undef MAY_GO
}
static bool make_room(uint32_t need) { return make_room_ex(need, false, true); }   /* (scratch: what it can) */

/* RLE of one row of 8-bit pixels (see tools/pack.py rle()); nib: literals two a byte (local colors, TF_NIB) */
static uint32_t rle_row(const uint8_t *row, int w, uint8_t *out, bool nib) {
  int end = w;
  while (end > 0 && !row[end - 1]) end--;
  uint8_t *o = out;
  int x = 0;
  while (x < end) {
    uint8_t v = row[x];
    int n = 1;
    while (x + n < end && row[x + n] == v) n++;
    if (!v) {
      while (n > 0) {
        int k = n < 64 ? n : 64;
        *o++ = (uint8_t)(k - 1);
        n -= k, x += k;
      }
      continue;
    }
    if (n >= 3) {
      while (n >= 3) {
        int k = n < 64 ? n : 64;
        *o++ = (uint8_t)(0x40 + k - 1);
        *o++ = v;
        n -= k, x += k;
      }
      continue;
    }
    int s = x;
    while (x < end && x - s < 128) {
      uint8_t u = row[x];
      int m = 1;
      while (x + m < end && row[x + m] == u) m++;
      if (!u || m >= 3) break;
      x += m < 128 - (x - s) ? m : 128 - (x - s);
    }
    *o++ = (uint8_t)(0x80 + (x - s) - 1);
    if (nib)
      for (int i = s; i < x; i += 2) *o++ = (uint8_t)(row[i] | (i + 1 < x ? row[i + 1] << 4 : 0));
    else
      for (int i = s; i < x; i++) *o++ = row[i];
  }
  return (uint32_t)(o - out);
}

/* the texture being read out of a pack stream */
typedef struct {
  uint32_t pos;            /* stream position of the next byte */
  int cur;                 /* index into the wanted list */
  int n;
  const uint16_t *want;    /* texture ids, by increasing offset */
  uint8_t hdr[20 + 16];     /* the header, then a TF_NIB texture's colors (count, then their indices) */
  int hdr_n;
  CEnt *e;
  uint32_t px_n;           /* pixels received */
  uint8_t row[1024];
  int row_n;
  uint8_t *out;
  bool skip;               /* not enough room: drop it */
  uint8_t mark;            /* what the loaded entries are marked */
  bool soft;               /* all but `hard` only if there is room for them as it is */
  uint16_t hard;
  bool done;               /* all read: the rest of the pack is not decoded */
} Reader;

static uint32_t entry_offset(uint16_t id) { return tex_entry(id) & 0x3FFFF; }

#ifdef HOST
#include <stdio.h>
#include <stdlib.h>
#endif
static void finish_tex(Reader *r) {
#ifdef HOST
  if (getenv("RESDEBUG")) printf("tex %d %dx%d %s %u bytes (top %u)\n", r->want[r->cur], rd16(r->hdr), rd16(r->hdr + 2),
                                 r->skip ? "SKIPPED" : "", r->e ? (unsigned)r->e->bytes : 0, (unsigned)cache_top);
#endif
  if (!r->skip && r->e) {
    CEnt *e = r->e;
    if (e->fmt != TF_RAW4) {
      uint8_t *data = (uint8_t *)(e + 1);
      uint32_t ms = cent_map_bytes(e);
      uint16_t *rows = (uint16_t *)(void *)(data + ms);
      rows[e->h] = (uint16_t)(r->out - (uint8_t *)rows);
      e->bytes = (uint16_t)(r->out - data);
      /* rows all shorter than ROW8_MAX bytes: a byte each for their lengths (TF_ROW8) */
      bool small = true;
      for (int y = 0; y < e->h && small; y++) small = rows[y + 1] - rows[y] < ROW8_MAX;
      if (small) {
        uint8_t *lens = (uint8_t *)rows;
        uint32_t ops = 2u * (e->h + 1), n = rows[e->h] - ops;
        for (int y = 0; y < e->h; y++) lens[y] = (uint8_t)(rows[y + 1] - rows[y]);
        memmove(lens + e->h, (uint8_t *)rows + ops, n);
        e->bytes = (uint16_t)(ms + e->h + n);
        e->fmt |= TF_ROW8;
      }
    }
    e->mark = r->mark;
    e->used = g_res_frame;
    hash_put(e->tex, (uint32_t)((uint8_t *)e - cache));
    cache_top += (sizeof(CEnt) + e->bytes + 3) & ~3u;
  }
  g_res_gen++;
  r->e = NULL;
  r->done = ++r->cur == r->n;
  r->hdr_n = 0;
  r->px_n = 0;
  r->row_n = 0;
}

static void start_tex(Reader *r) {
  uint16_t id = r->want[r->cur];
  const uint8_t *h = r->hdr;
  uint16_t w = rd16(h), hh = rd16(h + 2);
  uint8_t fmt = h[14] == 2 ? TF_RAW4 : (h[14] & TF_NIB);
  uint32_t ms = fmt & TF_NIB ? (1u + h[20] + 1) & ~1u : 0;   /* the colors, kept even (the row table) */
  uint32_t need = rd32(h + 16) + (fmt != TF_RAW4 ? 2 * (hh + 1u) + ms : 0);
  bool soft = r->soft && r->want[r->cur] != r->hard;   /* an animation's other frames: only into free room */
  r->skip = soft ? cache_top + sizeof(CEnt) + need + 4 > cache_limit : !make_room_ex(sizeof(CEnt) + need + 4, fmt == TF_RAW4, false);
  r->e = NULL;
  if (r->skip) return;
  CEnt *e = (CEnt *)(void *)(cache + cache_top);
  e->tex = id, e->w = w, e->h = hh;
  e->ox = rds16(h + 4), e->oy = rds16(h + 6), e->fw = rd16(h + 8), e->fh = rd16(h + 10), e->pal = rd16(h + 12);
  e->fmt = fmt;
  e->scale = h[15];
  e->bytes = (uint16_t)(fmt == TF_RAW4 ? ((uint32_t)w * hh + 1) / 2 : 0);
  r->e = e;
  if (ms) memcpy(e + 1, h + 20, 1u + h[20]);
  r->out = (uint8_t *)(e + 1) + (fmt != TF_RAW4 ? ms + 2 * (hh + 1) : 0);
}

static void reader_sink(const uint8_t *p, uint32_t n, void *ctx) {
  Reader *r = ctx;
  while (n && r->cur < r->n) {
    uint32_t at = entry_offset(r->want[r->cur]);
#ifdef HOST
    if (r->pos > at && r->hdr_n == 0) {   /* (the list goes by offset: never past one) */
      fprintf(stderr, "res: texture %d at %u read from %u\n", r->want[r->cur], (unsigned)at, (unsigned)r->pos);
      abort();
    }
#endif
    if (r->pos < at) {   /* skip to the next wanted texture */
      uint32_t k = at - r->pos;
      if (k > n) k = n;
      p += k, n -= k, r->pos += k;
      continue;
    }
    int hn = 20;
    if (r->hdr_n >= 20 && (r->hdr[14] & TF_NIB)) hn = r->hdr_n > 20 ? 21 + r->hdr[20] : 21;
    if (r->hdr_n < hn) {
      r->hdr[r->hdr_n++] = *p++;
      n--, r->pos++;
      int done = r->hdr_n < 20 ? 0 : !(r->hdr[14] & TF_NIB) ? 20 : r->hdr_n > 20 ? 21 + r->hdr[20] : 0;
      if (r->hdr_n == done) {
        start_tex(r);
        /* (a last one with no room: the rest of the pack is not decoded for nothing) */
        if (rd16(r->hdr) * rd16(r->hdr + 2) == 0 || (r->skip && r->cur == r->n - 1)) finish_tex(r);
      }
      continue;
    }
    uint16_t w = rd16(r->hdr), h = rd16(r->hdr + 2);
    uint32_t total = (uint32_t)w * h;
    uint32_t k = total - r->px_n;
    if (k > n) k = n;
    if (r->e && !r->skip) {
      CEnt *e = r->e;
      if (e->fmt == TF_RAW4) {
        uint8_t *dst = (uint8_t *)(e + 1);
        for (uint32_t i = 0; i < k; i++) {
          uint32_t at = r->px_n + i;
          if (at & 1) dst[at >> 1] |= (uint8_t)(p[i] << 4);
          else dst[at >> 1] = p[i] & 15;
        }
      } else {
        uint16_t *rows = (uint16_t *)(void *)((uint8_t *)(e + 1) + cent_map_bytes(e));
        for (uint32_t i = 0; i < k; i++) {
          if (r->row_n < (int)sizeof r->row) r->row[r->row_n] = p[i];
          r->row_n++;
          if (r->row_n == w) {
            int y = (int)((r->px_n + i) / w);
            rows[y] = (uint16_t)(r->out - (uint8_t *)rows);
            r->out += rle_row(r->row, w < (int)sizeof r->row ? w : (int)sizeof r->row, r->out, (e->fmt & TF_NIB) != 0);
            r->row_n = 0;
          }
        }
      }
    }
    r->px_n += k;
    p += k, n -= k, r->pos += k;
    if (r->px_n == total) finish_tex(r);
  }
  r->pos += n;
}

static int cmp_offset(const void *a, const void *b) {   /* by pack, then offset (not by kind: tile sheets are in packs too) */
  uint32_t x = tex_entry(*(const uint16_t *)a) & 0x3FFFFFFF, y = tex_entry(*(const uint16_t *)b) & 0x3FFFFFFF;
  return x < y ? -1 : x > y;
}

/* loads the textures in `ids` (by pack, then offset: one pass over each pack), marked `mark` */
static void load_list_ex(uint16_t *ids, int n, uint8_t mark, int hard);
static void load_list(uint16_t *ids, int n, uint8_t mark) { load_list_ex(ids, n, mark, -1); }
static void load_list_ex(uint16_t *ids, int n, uint8_t mark, int hard) {
  for (int i = 1; i < n; i++)
    for (int j = i; j > 0 && cmp_offset(&ids[j - 1], &ids[j]) > 0; j--) {
      uint16_t t = ids[j];
      ids[j] = ids[j - 1];
      ids[j - 1] = t;
    }
  int i = 0;
  while (i < n) {
    uint32_t pack = (tex_entry(ids[i]) >> 18) & 0xFFF;
    int j = i;
    while (j < n && ((tex_entry(ids[j]) >> 18) & 0xFFF) == pack) j++;
    Reader rd;
    memset(&rd, 0, sizeof rd);
    rd.want = ids + i;
    rd.n = j - i;
    rd.mark = mark;
    rd.soft = hard >= 0, rd.hard = (uint16_t)hard;
    uint32_t last = entry_offset(ids[j - 1]);
    lz_run((uint16_t)pack, 0, last + 20 + 4096u * 4096u, reader_sink, &rd, &rd.done);
    i = j;
  }
}

/* what the rooms want: what was wanted before and is not now can go when room is needed */
void res_load_needed(void) {
  /* the old marks: just evictable now (1 is never a current marks_gen: see res_begin_need) */
  for (uint32_t at = 0; at < cache_top;) {
    CEnt *e = (CEnt *)(void *)(cache + at);
    if (e->mark && e->mark != marks_gen) e->mark = 1;
    at += (sizeof(CEnt) + e->bytes + 3) & ~3u;
  }
  if (nmissing) load_list(missing, nmissing, marks_gen);
  nmissing = 0;
}

/* a texture being drawn that is not loaded */
static bool load_one(uint16_t id) {
  uint32_t e = tex_entry(id);
  if (e >> 30 == 1 || e == 0xFFFFFFFF) return false;
  uint16_t one = id;
  /* drawn now: as wanted as the rooms' (marked 1, a tile sheet the room's loading had to let go of went first ever
   * after, every frame, and came back taking the room of what the frame had drawn) */
  load_list(&one, 1, marks_gen);
  return hash_find(id) >= 0;
}
/* several (an animation's frames), in one pass: `hard` surely (if it can be), the others if there is
 * room for them without making any */
void res_load_frames(const uint16_t *ids, int n, uint16_t hard) {
  uint16_t todo[32];
  int k = 0;
  for (int i = 0; i < n && k < 32; i++) {
    if (ids[i] >= rd16(section(SEC_TEX))) continue;
    uint32_t e = tex_entry(ids[i]);
    if (e >> 30 == 1 || e == 0xFFFFFFFF || hash_find(ids[i]) >= 0) continue;
    bool dup = false;
    for (int j = 0; j < k; j++) dup |= todo[j] == ids[i];
    if (!dup) todo[k++] = ids[i];
  }
  if (k && g_res_can_load) load_list_ex(todo, k, 1, hard);
}
void res_load(const uint16_t *ids, int n) {
  uint16_t todo[32];
  int k = 0;
  for (int i = 0; i < n && k < 32; i++) {
    if (ids[i] >= rd16(section(SEC_TEX))) continue;
    uint32_t e = tex_entry(ids[i]);
    if (e >> 30 == 1 || e == 0xFFFFFFFF || hash_find(ids[i]) >= 0) continue;
    bool dup = false;
    for (int j = 0; j < k; j++) dup |= todo[j] == ids[i];
    if (!dup) todo[k++] = ids[i];
  }
  if (k && g_res_can_load) load_list(todo, k, 1);
}
#ifdef HOST
/* CACHEWS=1: the bytes of what the frame drew from the cache, and what is in it */
void res_debug_working_set(void) {
  if (!getenv("CACHEWS")) return;
  uint32_t drawn = 0, all = 0;
  int n = 0;
  char list[600];
  int k = 0;
  for (uint32_t at = 0; at < cache_top;) {
    CEnt *e = (CEnt *)(void *)(cache + at);
    uint32_t size = (sizeof(CEnt) + e->bytes + 3) & ~3u;
    all += size;
    if (e->mark && e->used == g_res_frame) {
      drawn += size, n++;
      if (k < 560) k += snprintf(list + k, sizeof list - k, " %d:%u", e->tex, (unsigned)size);
    }
    at += size;
  }
  list[k] = 0;
  printf("ws frame %u drawn %u in %d, cache %u of %u:%s\n", (unsigned)g_res_frame, (unsigned)drawn, n, (unsigned)all,
         (unsigned)cache_limit, list);
}
#endif
bool res_cached(uint16_t id) {
  if (id >= rd16(section(SEC_TEX))) return true;
  uint32_t e = tex_entry(id);
  return e >> 30 == 1 || hash_find(id) >= 0;
}

/* free memory at the top of the cache for something passing (a room being read, a dialog): at least
 * `need` bytes if it can be made */
uint8_t *res_scratch(uint32_t need, uint32_t *size) {
  make_room(need);
  *size = cache_limit - cache_top;
  return cache + cache_top;
}

/* a chapter's own tables (its pools of torches, clutter...) take the top of the cache while it is played:
 * `bytes` of zeroes, everything loaded before forgotten */
uint8_t *g_chapter_ram;
uint8_t g_res_tops;   /* counts the times the top changed hands */
static void top_ram(uint32_t bytes) {
  cache_top = 0;
  g_res_gen++, g_res_tops++;
  hash_rebuild();
  cache_limit = CACHE_BYTES - bytes;
  g_chapter_ram = cache + cache_limit;
}
void res_chapter_ram(uint32_t bytes) {
  bytes = (bytes + 7) & ~7u;
  if (bytes > CACHE_BYTES / 2) bytes = CACHE_BYTES / 2;
  top_ram(bytes);
  memset(g_chapter_ram, 0, bytes);
}
/* the menus' big picture (pic.c) the same way, while no chapter is played */
uint16_t *res_picture_ram(uint32_t bytes) {
  top_ram((bytes + 7) & ~7u);
  return (uint16_t *)(void *)g_chapter_ram;
}

/* every loaded texture forgotten (the chapter's memory stays) */
void res_flush(void) {
  cache_top = 0;
  g_res_gen++;
  hash_rebuild();
}

uint8_t *arena_scratch(uint32_t *size) {
  *size = cache_limit - cache_top;
  return cache + cache_top;
}

uint16_t res_tex_by_name(const char *path) {   /* (tools/pack.py: FNV-1a folded to 24 bits, then the ids) */
  const uint8_t *t = section(SEC_TEXNAMES);
  uint32_t h = rd32(t + 4);
  for (const char *s = path; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
  h = (h ^ (h >> 24)) & 0xFFFFFF;
  int n = rd16(t), lo = 0, hi = n - 1;
  const uint8_t *ids = t + 8 + ((3u * n + 1) & ~1u);
  while (lo <= hi) {
    int m = (lo + hi) / 2;
    const uint8_t *e = t + 8 + 3 * m;
    uint32_t v = e[0] | e[1] << 8 | (uint32_t)e[2] << 16;
    if (v == h) return rd16(ids + 2 * m);
    if (v < h) lo = m + 1;
    else hi = m - 1;
  }
  return 0xFFFF;
}
