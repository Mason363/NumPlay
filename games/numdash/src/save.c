/* Progress, settings and custom levels in Epsilon's record storage.
 *
 * Epsilon caches a pointer to the last record it looked up, so moving a
 * record behind its back could make it read or write the wrong bytes after
 * NumDash exits. Records are therefore only ever appended, rewritten in
 * place with the same size, or resized when they are the very last one. */
#include "save.h"
#include <string.h>

uint32_t nd_crc32(const uint8_t *p, size_t n) {
  uint32_t c = ~0u;
  while (n--) {
    c ^= *p++;
    for (int j = 0; j < 8; j++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1)));
  }
  return ~c;
}
static unsigned rd16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static uint32_t rd32(const uint8_t *p) { return rd16(p) | (uint32_t)rd16(p + 2) << 16; }
static void wr16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { wr16(p, v & 0xffff); wr16(p + 2, v >> 16); }

/* ------------------------------------------------------------ progress */

void progress_defaults(Progress *p) {
  memset(p, 0, sizeof(*p));
  p->color1 = 0;
  p->color2 = 3;
  p->options = OPT_PERCENT | OPT_BAR;
}

/* Layout version 1 used 12 bytes per level; version 2 drops the spare byte
   so that twelve levels and the custom slots fit the same 192 bytes. */
#if ND_EXTRA_LEVELS
/* NumDash.nwa's other levels keep their stats in numdash.ndx: numdash.nds
   stays as NumPlay's NumDash writes it (its twelve levels, then the custom
   slots), so either reads the other's saves. The last level played, as
   numdash.nds holds it (0 for one of the other levels). */
static uint8_t main_last(const Progress *p) {
  unsigned l = p->last_level;
  return (uint8_t)(l < LEVEL_BASE ? l : l >= LEVEL_COUNT ? l - LEVEL_COUNT + LEVEL_BASE : 0);
}
#endif
void progress_encode(const Progress *p, uint8_t out[PROGRESS_BYTES]) {
  _Static_assert(12 + (LEVEL_BASE + CUSTOM_SLOTS) * 11 + 12 <= PROGRESS_BYTES, "progress record too small");
  memset(out, 0, PROGRESS_BYTES);
  memcpy(out, "NDS1", 4);
  out[8] = 2;
  out[9] = LEVEL_BASE;
  out[10] = CUSTOM_SLOTS;
  uint8_t *q = out + 12;
  for (int i = 0; i < LEVEL_BASE + CUSTOM_SLOTS; i++, q += 11) {
#if ND_EXTRA_LEVELS
    const LevelStat *s = &p->lv[i < LEVEL_BASE ? i : i - LEVEL_BASE + LEVEL_COUNT];
#else
    const LevelStat *s = &p->lv[i];
#endif
    q[0] = s->normal; q[1] = s->practice; q[2] = s->coins;
    wr32(q + 3, s->attempts);
    wr32(q + 7, s->jumps);
  }
  wr32(q, p->attempts); wr32(q + 4, p->jumps);
#if ND_EXTRA_LEVELS
  q[8] = p->color1; q[9] = p->color2; q[10] = p->options; q[11] = main_last(p);
#else
  q[8] = p->color1; q[9] = p->color2; q[10] = p->options; q[11] = p->last_level;
#endif
  wr32(out + 4, nd_crc32(out + 8, PROGRESS_BYTES - 8));
}

bool progress_decode(Progress *p, const uint8_t *in, size_t len) {
  if (len < PROGRESS_BYTES || memcmp(in, "NDS1", 4) || rd32(in + 4) != nd_crc32(in + 8, PROGRESS_BYTES - 8)) return false;
  /* A save from a release with fewer levels keeps its stats: the custom
     slots follow the built-in levels, so they move up. */
  int levels = in[9], stride = in[8] == 1 ? 12 : 11, a = stride - 8;
  if (in[8] < 1 || in[8] > 2 || levels == 0 || levels > LEVEL_COUNT || in[10] != CUSTOM_SLOTS ||
      12 + (levels + CUSTOM_SLOTS) * stride + 12 > PROGRESS_BYTES) return false;
  const uint8_t *q = in + 12;
  for (int i = 0; i < levels + CUSTOM_SLOTS; i++, q += stride)
    if (q[0] > 100 || q[1] > 100 || q[2] > 7) return false;
  progress_defaults(p);
  q = in + 12;
  for (int i = 0; i < levels + CUSTOM_SLOTS; i++, q += stride) {
    LevelStat *s = &p->lv[i < levels ? i : i - levels + LEVEL_COUNT];
    s->normal = q[0]; s->practice = q[1]; s->coins = q[2];
    s->attempts = rd32(q + a);
    s->jumps = rd32(q + a + 4);
  }
  p->attempts = rd32(q); p->jumps = rd32(q + 4);
  p->color1 = q[8] < 42 ? q[8] : 0;
  p->color2 = q[9] < 42 ? q[9] : 3;
  p->options = q[10];
  int last = q[11] < levels ? q[11] : q[11] - levels + LEVEL_COUNT;
  p->last_level = q[11] < levels + CUSTOM_SLOTS ? (uint8_t)last : 0;
  return true;
}

#if ND_EXTRA_LEVELS
/* numdash.ndx: "NDX1", CRC, version, level count, the last level byte of the
   numdash.nds written with it and the last level played if it is one of
   these, then 11 bytes per level as in numdash.nds. */
void extra_encode(const Progress *p, uint8_t out[EXTRA_BYTES]) {
  _Static_assert(12 + (LEVEL_COUNT - LEVEL_BASE) * 11 <= EXTRA_BYTES, "extra record too small");
  memset(out, 0, EXTRA_BYTES);
  memcpy(out, "NDX1", 4);
  out[8] = 1;
  out[9] = LEVEL_COUNT - LEVEL_BASE;
  out[10] = main_last(p);
  out[11] = p->last_level >= LEVEL_BASE && p->last_level < LEVEL_COUNT ? p->last_level : 0xff;
  uint8_t *q = out + 12;
  for (int i = LEVEL_BASE; i < LEVEL_COUNT; i++, q += 11) {
    const LevelStat *s = &p->lv[i];
    q[0] = s->normal; q[1] = s->practice; q[2] = s->coins;
    wr32(q + 3, s->attempts);
    wr32(q + 7, s->jumps);
  }
  wr32(out + 4, nd_crc32(out + 8, EXTRA_BYTES - 8));
}

bool extra_decode(Progress *p, const uint8_t *in, size_t len) {
  if (len < EXTRA_BYTES || memcmp(in, "NDX1", 4) || rd32(in + 4) != nd_crc32(in + 8, EXTRA_BYTES - 8) || in[8] != 1 ||
      12 + in[9] * 11 > EXTRA_BYTES) return false;
  int n = in[9] < LEVEL_COUNT - LEVEL_BASE ? in[9] : LEVEL_COUNT - LEVEL_BASE;
  const uint8_t *q = in + 12;
  for (int i = 0; i < n; i++, q += 11)
    if (q[0] > 100 || q[1] > 100 || q[2] > 7) return false;
  q = in + 12;
  for (int i = 0; i < n; i++, q += 11) {
    LevelStat *s = &p->lv[LEVEL_BASE + i];
    s->normal = q[0]; s->practice = q[1]; s->coins = q[2];
    s->attempts = rd32(q + 3);
    s->jumps = rd32(q + 7);
  }
  /* the last level played, unless NumPlay's NumDash has played another since */
  if (in[11] >= LEVEL_BASE && in[11] < LEVEL_BASE + n && in[10] == main_last(p)) p->last_level = in[11];
  return true;
}
#endif

/* ------------------------------------------------------------ custom levels */

static size_t put_varint(uint8_t *out, size_t pos, size_t cap, uint32_t v) {
  do {
    if (pos >= cap) return 0;
    out[pos++] = (uint8_t)((v & 127) | (v > 127 ? 128 : 0));
    v >>= 7;
  } while (v);
  return pos;
}
static bool get_varint(const uint8_t *in, size_t len, size_t *pos, uint32_t *v) {
  uint32_t r = 0;
  for (int shift = 0; shift < 21; shift += 7) {
    if (*pos >= len) return false;
    uint8_t b = in[(*pos)++];
    r |= (uint32_t)(b & 127) << shift;
    if (!(b & 128)) { *v = r; return true; }
  }
  return false;
}

size_t custom_encode(const LObj *objs, unsigned count, const CustomMeta *m, uint8_t *out, size_t cap) {
  if (cap < 16 || count > CUSTOM_MAX) return 0;
  memcpy(out, "NDL1", 4);
  wr16(out + 10, count);
  wr16(out + 12, m->end_x);
  out[14] = m->theme;
  out[15] = m->flags;
  size_t n = 16;
  int px = 0, py = 0;
  for (unsigned i = 0; i < count; i++) {
    const LObj *o = &objs[i];
    if (o->x < px) return 0;
    int dy = o->y - py;
    n = put_varint(out, n, cap, (uint32_t)(o->x - px));
    if (n) n = put_varint(out, n, cap, dy >= 0 ? (uint32_t)dy * 2 : (uint32_t)(-dy) * 2 - 1);
    if (!n || n + 2 > cap) return 0;
    out[n++] = o->type;
    out[n++] = o->xf;
    px = o->x;
    py = o->y;
  }
  wr16(out + 8, (unsigned)(n - 16));
  wr32(out + 4, nd_crc32(out + 8, n - 8));
  return n;
}

bool custom_decode(const uint8_t *in, size_t len, LObj *objs, unsigned cap, CustomMeta *m) {
  if (len < 16 || memcmp(in, "NDL1", 4)) return false;
  size_t n = 16 + rd16(in + 8);
  unsigned count = rd16(in + 10);
  if (n > len || rd32(in + 4) != nd_crc32(in + 8, n - 8) || count > CUSTOM_MAX || count > cap) return false;
  size_t pos = 16;
  int x = 0, y = 0;
  for (unsigned i = 0; i < count; i++) {
    uint32_t dx, zy;
    if (!get_varint(in, n, &pos, &dx) || !get_varint(in, n, &pos, &zy) || pos + 2 > n) return false;
    x += (int)dx;
    y += (zy & 1) ? -(int)((zy + 1) / 2) : (int)(zy / 2);
    uint8_t type = in[pos++], xf = in[pos++];
    if (x > 0xffff || y < -600 || y > 3000 || !type || type >= OT_LOBJ || (xf & 0xf0)) return false;
    objs[i] = (LObj){(uint16_t)x, (int16_t)y, type, xf};
  }
  if (pos != n) return false;
  m->count = (uint16_t)count;
  m->end_x = (uint16_t)rd16(in + 12);
  m->theme = in[14];
  m->flags = in[15];
  m->exists = true;
  return true;
}

/* ------------------------------------------------------------ legacy saves */

static uint8_t type_for_gd(unsigned id) {
  for (unsigned t = 1; t < OT_LOBJ; t++) if (objdefs[t].gd_id == id) return (uint8_t)t;
  return 0;
}

bool legacy_decode(const uint8_t *in, size_t size, Progress *p, LObj *objs, unsigned cap, unsigned counts[CUSTOM_SLOTS],
                   CustomMeta metas[CUSTOM_SLOTS]) {
  if (size < 108) return false;
  bool v2 = !memcmp(in, "NDASH002", 8);
  if (!v2 && memcmp(in, "NDASH003", 8)) return false;
  if (rd32(in + 8) != size || nd_crc32(in + 16, size - 16) != rd32(in + 12)) return false;
  unsigned stride = v2 ? 12 : 8;
  for (size_t n = 16; n < 86; n += 7) if (in[n] > 100 || in[n + 1] > 100 || in[n + 2] > 3) return false;
  progress_defaults(p);
  /* The first four built-in levels are unchanged; the old 5-7 were others. */
  for (int i = 0; i < 4; i++) {
    const uint8_t *q = in + 16 + i * 7;
    p->lv[i].normal = q[0];
    p->lv[i].practice = q[1];
    p->lv[i].coins = (uint8_t)((1u << q[2]) - 1);
    p->lv[i].attempts = rd32(q + 3);
  }
  for (int i = 0; i < 10; i++) p->attempts += rd32(in + 16 + i * 7 + 3);
  if (!(v2 ? 1 : in[87])) p->options &= (uint8_t)~OPT_PERCENT;
  if (in[88]) p->options |= OPT_FPS;
  size_t n = 90;
  unsigned used = 0;
  for (int s = 0; s < CUSTOM_SLOTS; s++) {
    if (n + 6 > size) return false;
    unsigned count = rd16(in + n), length = rd16(in + n + 2);
    metas[s] = (CustomMeta){0, (uint16_t)length, in[n + 4], 0, count > 0};
    counts[s] = 0;
    n += 6;
    for (unsigned j = 0; j < count; j++, n += stride) {
      if (n + stride > size) return false;
      int x = (int16_t)rd16(in + n), y = (int16_t)rd16(in + n + 2);
      uint8_t t = type_for_gd(rd16(in + n + 4));
      if (!t || x < 0 || used >= cap) continue;
      objs[used++] = (LObj){(uint16_t)x, (int16_t)y, t, (uint8_t)(in[n + 6] & 3)};
      counts[s]++;
    }
    metas[s].count = (uint16_t)counts[s];
  }
  return true;
}

/* ------------------------------------------------------------ records */

/* Locates `name`; `end` is the offset of the terminating zero size. Rejects
 * malformed storage (overlong records, missing names, duplicates). */
static bool scan(const uint8_t *a, size_t size, const char *name, size_t *found, size_t *end) {
  if (!a || !name || size < 2) return false;
  *found = SIZE_MAX;
  size_t p = 0;
  while (p + 2 <= size) {
    size_t len = rd16(a + p);
    if (!len) { *end = p; return true; }
    if (len < 4 || len > size - p - 2) return false;
    const uint8_t *zero = memchr(a + p + 2, 0, len - 2);
    if (!zero) return false;
    if (!strcmp((const char *)a + p + 2, name)) {
      if (*found != SIZE_MAX) return false;
      *found = p;
    }
    p += len;
  }
  return false;
}

const uint8_t *storage_read(const uint8_t *a, size_t size, const char *name, size_t *len) {
  size_t found, end;
  if (!scan(a, size, name, &found, &end) || found == SIZE_MAX) return NULL;
  size_t head = 2 + strlen(name) + 1, total = rd16(a + found);
  if (total < head) return NULL;
  *len = total - head;
  return a + found + head;
}

bool storage_put(uint8_t *a, size_t size, const char *name, const uint8_t *data, size_t len, size_t granule) {
  size_t found, end, nlen = name ? strlen(name) : 0;
  if (!data || !nlen || nlen > 32 || !scan(a, size, name, &found, &end)) return false;
  size_t head = 2 + nlen + 1, need = head + len;
  if (granule < 1) granule = 1;
  size_t rounded = (need + granule - 1) / granule * granule;
  if (need > 0xffff) return false;
  if (rounded > 0xffff) rounded = need;
  size_t at, total;
  if (found != SIZE_MAX) {
    size_t old = rd16(a + found);
    bool last = found + old == end;
    if (need <= old && !(last && rounded < old)) {
      total = old;                                  /* same size, padded */
    } else if (last) {
      total = rounded;                              /* only the end marker moves */
      if (found + total + 2 > size) {
        total = need;
        if (found + total + 2 > size) return false;
      }
    } else {
      return false;
    }
    at = found;
    if (total != old) wr16(a + found + total, 0);
  } else {
    total = rounded;
    if (end + total + 2 > size) total = need;
    if (end + total + 2 > size) return false;
    at = end;
    wr16(a + at + total, 0);
  }
  wr16(a + at, (unsigned)total);
  memcpy(a + at + 2, name, nlen + 1);
  memcpy(a + at + head, data, len);
  memset(a + at + head + len, 0, total - head - len);
  return true;
}

bool storage_remove_last(uint8_t *a, size_t size, const char *name) {
  size_t found, end;
  if (!scan(a, size, name, &found, &end) || found == SIZE_MAX) return false;
  if (found + rd16(a + found) != end) return false;
  wr16(a + found, 0);
  return true;
}

/* ------------------------------------------------------------ platform glue */

#include "platform.h"

static const char *slot_name(int slot) {
  static const char *names[CUSTOM_SLOTS] = {"numdash1.ndl", "numdash2.ndl", "numdash3.ndl"};
  return names[slot];
}

bool save_write_progress(const Progress *p) {
  size_t size;
  uint8_t *a = platform_storage(&size);
  uint8_t buf[PROGRESS_BYTES];
  progress_encode(p, buf);
  if (!a || !storage_put(a, size, "numdash.nds", buf, sizeof(buf), 1)) return false;
#if ND_EXTRA_LEVELS
  uint8_t ext[EXTRA_BYTES];
  extra_encode(p, ext);
  bool ok = storage_put(a, size, "numdash.ndx", ext, sizeof(ext), 1);
  platform_storage_commit();
  return ok;
#else
  platform_storage_commit();
  return true;
#endif
}


bool save_write_custom(int slot, const LObj *objs, unsigned count, const CustomMeta *m) {
  size_t size;
  uint8_t *a = platform_storage(&size);
  size_t n = custom_encode(objs, count, m, level_scratch, sizeof(level_scratch));
  if (!a || !n || !storage_put(a, size, slot_name(slot), level_scratch, n, 512)) return false;
  platform_storage_commit();
  return true;
}

bool save_read_custom(int slot, LObj *objs, unsigned cap, CustomMeta *m) {
  size_t size, len;
  const uint8_t *a = platform_storage(&size), *d;
  if (!a || !(d = storage_read(a, size, slot_name(slot), &len))) return false;
  return custom_decode(d, len, objs, cap, m);
}

bool save_load_all(Progress *p, CustomMeta metas[CUSTOM_SLOTS]) {
  progress_defaults(p);
  memset(metas, 0, sizeof(CustomMeta) * CUSTOM_SLOTS);
  size_t size, len;
  uint8_t *a = platform_storage(&size);
  if (!a) return false;
  const uint8_t *d = storage_read(a, size, "numdash.nds", &len);
  if (d) {
    bool ok = progress_decode(p, d, len);
#if ND_EXTRA_LEVELS
    const uint8_t *x = ok ? storage_read(a, size, "numdash.ndx", &len) : NULL;
    if (x) extra_decode(p, x, len);
#endif
    for (int s = 0; s < CUSTOM_SLOTS; s++) {
      CustomMeta m;
      if (save_read_custom(s, level_objs, CUSTOM_MAX, &m)) metas[s] = m;
    }
    return ok;
  }
  d = storage_read(a, size, "numdash.ndd", &len);
  if (!d) return true;   /* first run */
  unsigned counts[CUSTOM_SLOTS], cap;
  LObj *objs = level_big_scratch(&cap);
  if (!legacy_decode(d, len, p, objs, cap, counts, metas)) { progress_defaults(p); return false; }
  /* Everything is decoded: drop the old record if nothing follows it. */
  storage_remove_last(a, size, "numdash.ndd");
  unsigned first = 0;
  for (int s = 0; s < CUSTOM_SLOTS; s++) {
    if (counts[s]) {
      CustomMeta m = metas[s];
      unsigned last_x = objs[first + counts[s] - 1].x;
      if (m.end_x < last_x + 300) m.end_x = (uint16_t)(last_x + 300 > 0xfff0 ? 0xfff0 : last_x + 300);
      m.theme %= 6;
      metas[s] = m;
      if (counts[s] > CUSTOM_MAX || !save_write_custom(s, objs + first, counts[s], &m)) metas[s].exists = false;
    } else {
      metas[s].exists = false;
    }
    first += counts[s];
  }
  return save_write_progress(p);
}
