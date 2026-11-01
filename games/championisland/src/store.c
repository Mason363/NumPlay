/* Saved values and quest conditions (see store.h). */
#include "store.h"

#define SAVE_NAME "champion.sav"
/* a value's name is a string id of data.bin (pack.py adds every name the game
 * saves); a name not there takes one of a few spare slots */
#define MAX_KEYS 128
#define SPARE 8
#define SPARE_LEN 24
typedef struct { uint16_t key; uint8_t type; bool b; uint16_t s, pad; float num; } Slot;
static Slot slots[MAX_KEYS];
static char spare[SPARE][SPARE_LEN];
static int nslots;
static bool dirty;

static const char *key_name(uint16_t k) { return k >= 0xFF00 ? spare[k - 0xFF00] : str(k); }

static int key_of(const char *key, bool make) {
  int id = str_find(key);
  if (id >= 0) return id;
  for (int i = 0; i < SPARE; i++) if (spare[i][0] && !strcmp(spare[i], key)) return 0xFF00 + i;
  if (!make || strlen(key) >= SPARE_LEN) return -1;
  for (int i = 0; i < SPARE; i++)
    if (!spare[i][0]) { strcpy(spare[i], key); return 0xFF00 + i; }
  return -1;
}

static Slot *find(const char *key, bool make) {
  int k = key_of(key, make);
  if (k < 0) return NULL;
  for (int i = 0; i < nslots; i++)
    if (slots[i].key == k) return &slots[i];
  if (!make || nslots >= MAX_KEYS) return NULL;
  Slot *s = &slots[nslots++];
  memset(s, 0, sizeof *s);
  s->key = (uint16_t)k;
  return s;
}

static Value value_of_slot(const Slot *s) { return (Value){s->type, s->b, s->num, s->s}; }

Value store_get(const char *key) {
  Slot *s = find(key, false);
  Value none = {SV_NONE, false, 0, NONE16};
  return s ? value_of_slot(s) : none;
}

bool store_bool(const char *key, bool def) {
  Value v = store_get(key);
  switch (v.type) {
    case SV_NONE: return def;
    case SV_NULL: return false;
    case SV_BOOL: return v.b;
    case SV_NUM: return v.num != 0;
    case SV_STR: return str(v.s)[0] != 0;
  }
  return def;
}

float store_num(const char *key, float def) {
  Value v = store_get(key);
  return v.type == SV_NUM ? v.num : v.type == SV_BOOL ? (float)v.b : def;
}

const char *store_str(const char *key) {
  Value v = store_get(key);
  return v.type == SV_STR ? str(v.s) : NULL;
}

bool store_is(const char *key, const char *s) {
  const char *v = store_str(key);
  return v && !strcmp(v, s);
}

static bool same(Value a, Value b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case SV_BOOL: return a.b == b.b;
    case SV_NUM: return a.num == b.num;
    case SV_STR: return a.s == b.s;
  }
  return true;
}

void store_set(const char *key, Value v) {
  Slot *s = find(key, true);
  if (!s) return;
  if (same(value_of_slot(s), v)) return;
  s->type = v.type;
  s->b = v.b;
  s->num = v.num;
  s->s = v.s;
  dirty = true;
}

void store_set_bool(const char *key, bool b) { store_set(key, (Value){SV_BOOL, b, 0, NONE16}); }
void store_set_num(const char *key, float n) { store_set(key, (Value){SV_NUM, false, n, NONE16}); }
void store_set_sid(const char *key, uint16_t s) { store_set(key, (Value){SV_STR, false, 0, s}); }
void store_set_str(const char *key, const char *s) {
  int id = str_find(s);
  if (id >= 0) store_set_sid(key, (uint16_t)id);
}

void store_clear(void) {
  nslots = 0;
  memset(spare, 0, sizeof spare);
  dirty = true;
}

bool store_dirty(void) { return dirty; }

/* The copy: installing apps from the NumWorks website keeps only Python
 * scripts, so the save is also kept in one, as a comment line
 * "#>champion.sav:base64" (the way NumPlay keeps its games' saves), and
 * comes back from it when the save itself is gone. */
#define COPY_NAME "champion_saves.py"
static const char copy_head[] = "# Champion Island's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void save_copy(const uint8_t *d, uint32_t n, uint8_t *o, uint32_t cap) {
  uint32_t total = 1 + (sizeof copy_head - 1) + 2 + (sizeof SAVE_NAME - 1) + 1 + (n + 2) / 3 * 4 + 2;
  if (total > cap) return;
  uint8_t *p = o;
  *p++ = 0;                                   /* the script's status byte: not imported */
  memcpy(p, copy_head, sizeof copy_head - 1);
  p += sizeof copy_head - 1;
  *p++ = '#', *p++ = '>';
  memcpy(p, SAVE_NAME, sizeof SAVE_NAME - 1);
  p += sizeof SAVE_NAME - 1;
  *p++ = ':';
  for (uint32_t i = 0; i < n; i += 3, p += 4) {
    /* (written out: GCC 15.2 for the calculator got a loop over the four wrong, see NumPlay's
     * launcher/src/storage.c) */
    uint32_t left = n - i, v = (uint32_t)d[i] << 16 | (left > 1 ? d[i + 1] << 8 : 0) | (left > 2 ? d[i + 2] : 0);
    p[0] = (uint8_t)b64[v >> 18 & 63];
    p[1] = (uint8_t)b64[v >> 12 & 63];
    p[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    p[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
  *p++ = '\n';
  *p++ = 0;
  plat_save(COPY_NAME, o, (uint32_t)(p - o));
}

static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}

/* the save from the copy, when it is gone (after the app was installed again) */
static void restore_copy(void) {
  uint32_t n, cap;
  const uint8_t *c = plat_load(COPY_NAME, &n);
  static const char tag[] = "#>" SAVE_NAME ":";
  if (!c) return;
  const uint8_t *l = NULL;
  for (uint32_t i = 0; i + sizeof tag - 1 <= n; i++)
    if (!memcmp(c + i, tag, sizeof tag - 1)) { l = c + i + sizeof tag - 1; break; }
  if (!l) return;
  uint32_t len = 0;
  while (l + len < c + n && l[len] != '\n' && l[len]) len++;
  if (!len || len % 4) return;
  extern uint8_t *z_scratch(uint32_t *size);
  uint8_t *out = z_scratch(&cap);
  uint32_t size = len / 4 * 3 - (l[len - 1] == '=') - (l[len - 2] == '='), o = 0;
  if (size > cap) return;
  for (uint32_t i = 0; i < len; i += 4) {
    int v[4];
    for (int k = 0; k < 4; k++) v[k] = l[i + k] == '=' ? 0 : b64_value(l[i + k]);
    if (v[0] < 0 || v[1] < 0 || v[2] < 0 || v[3] < 0) return;
    uint32_t w = (uint32_t)(v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3]);
    for (int k = 0; k < 3 && o < size; k++) out[o++] = (uint8_t)(w >> (16 - 8 * k));
  }
  if (o == size && size >= 3 && !memcmp(out, "CI1", 3)) plat_save(SAVE_NAME, out, size);
}

/* The file: "CI1", then per value: key length, key, type, then a byte (bool),
 * a float (number) or a length and text (string). */
bool store_save(void) {
  extern uint8_t *z_scratch(uint32_t *size);
  uint32_t cap;
  uint8_t *buf = z_scratch(&cap);   /* the decoder is idle between frames */
  uint32_t n = 0;
  memcpy(buf, "CI1", 3);
  n = 3;
  for (int i = 0; i < nslots; i++) {
    const Slot *s = &slots[i];
    if (s->type == SV_NONE) continue;
    const char *kn = key_name(s->key);
    size_t kl = strlen(kn);
    if (kl > 255) continue;
    const char *t = s->type == SV_STR ? str(s->s) : NULL;
    size_t tl = t ? strlen(t) : 0;
    if (tl > 255) tl = 255;
    if (n + 2 + kl + 6 + tl > cap) break;
    buf[n++] = (uint8_t)kl;
    memcpy(buf + n, kn, kl);
    n += (uint32_t)kl;
    buf[n++] = s->type;
    if (s->type == SV_BOOL) buf[n++] = s->b;
    else if (s->type == SV_NUM) { memcpy(buf + n, &s->num, 4); n += 4; }
    else if (s->type == SV_STR) { buf[n++] = (uint8_t)tl; memcpy(buf + n, t, tl); n += (uint32_t)tl; }
  }
  if (!plat_save(SAVE_NAME, buf, n)) return false;
  save_copy(buf, n, buf + n, cap - n);
  dirty = false;
  return true;
}

void store_load(void) {
  uint32_t len;
  const uint8_t *d = plat_load(SAVE_NAME, &len);
  if (!d) {
    restore_copy();
    d = plat_load(SAVE_NAME, &len);
  }
  nslots = 0;
  memset(spare, 0, sizeof spare);
  if (!d || len < 3 || memcmp(d, "CI1", 3)) return;
  uint32_t i = 3;
  while (i < len) {
    char key[64];
    uint8_t kl = d[i++];
    if (kl >= sizeof key || i + kl + 1 > len) break;
    memcpy(key, d + i, kl);
    key[kl] = 0;
    i += kl;
    uint8_t t = d[i++];
    Value v = {t, false, 0, NONE16};
    if (t == SV_BOOL) { if (i >= len) break; v.b = d[i++]; }
    else if (t == SV_NUM) { if (i + 4 > len) break; memcpy(&v.num, d + i, 4); i += 4; }
    else if (t == SV_STR) {
      if (i >= len) break;
      uint8_t tl = d[i++];
      if (i + tl > len) break;
      char tmp[256];
      memcpy(tmp, d + i, tl);
      tmp[tl] = 0;
      i += tl;
      int id = str_find(tmp);
      if (id < 0) continue;
      v.s = (uint16_t)id;
    } else if (t != SV_NULL) break;
    Slot *s = find(key, true);
    if (s) { s->type = v.type; s->b = v.b; s->num = v.num; s->s = v.s; }
  }
  dirty = false;
}

/* ---------------------------------------------------------------- conditions */
typedef struct { uint8_t t; float n; uint16_t s; } EV;   /* t: 0 false/true as n, 1 number, 2 string */

static EV value_of(const char *key) {
  /* expressions read missing values as false */
  Value v = store_get(key);
  switch (v.type) {
    case SV_BOOL: return (EV){3, v.b, 0};
    case SV_NUM: return (EV){1, v.num, 0};
    case SV_STR: return (EV){2, 0, v.s};
  }
  return (EV){3, 0, 0};
}

static bool truthy(EV e) { return e.t == 2 ? str(e.s)[0] != 0 : e.n != 0; }
static bool eq(EV a, EV b) {
  if (a.t != b.t) return false;
  return a.t == 2 ? a.s == b.s : a.n == b.n;
}
static float num(EV e) { return e.t == 2 ? 0 : e.n; }

static bool run(const uint8_t *p) {
  EV st[16];
  int sp = 0;
  for (;;) {
    uint8_t op = *p++;
    switch (op) {
      case E_VAR: {
        extern const char *const var_names[];
        uint16_t v = rd16(p);
        p += 2;
        if (sp < 16) st[sp++] = value_of(var_names[v]);
        break;
      }
      case E_STR: if (sp < 16) st[sp++] = (EV){2, 0, rd16(p)}; p += 2; break;
      case E_NUM: if (sp < 16) st[sp++] = (EV){1, rdf(p), 0}; p += 4; break;
      case E_TRUE: if (sp < 16) st[sp++] = (EV){3, 1, 0}; break;
      case E_FALSE: if (sp < 16) st[sp++] = (EV){3, 0, 0}; break;
      case E_NOT: if (sp >= 1) st[sp - 1] = (EV){3, !truthy(st[sp - 1]), 0}; break;
      case E_END: return sp ? truthy(st[sp - 1]) : false;
      default: {
        if (sp < 2) return false;
        EV b = st[--sp], a = st[--sp], r = {3, 0, 0};
        switch (op) {
          case E_EQ: r.n = eq(a, b); break;
          case E_NE: r.n = !eq(a, b); break;
          case E_LT: r.n = num(a) < num(b); break;
          case E_GT: r.n = num(a) > num(b); break;
          case E_LE: r.n = num(a) <= num(b); break;
          case E_GE: r.n = num(a) >= num(b); break;
          case E_AND: r.n = truthy(a) && truthy(b); break;
          case E_OR: r.n = truthy(a) || truthy(b); break;
          case E_ADD: r = (EV){1, num(a) + num(b), 0}; break;
        }
        st[sp++] = r;
      }
    }
  }
}

bool cond_eval(uint16_t sid) {
  uint32_t n = HDR(H_NEXPRS);
  const uint8_t *t = ci_data + HDR(H_EXPRS);
  for (uint32_t i = 0; i < n; i++, t += 6)
    if (rd16(t) == sid) return run(ci_data + rd32(t + 2));
  return false;
}

bool cond_eval_str(const char *c) {
  int id = str_find(c);
  return id >= 0 && cond_eval((uint16_t)id);
}
