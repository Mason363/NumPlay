/* Save files in Epsilon's file system, with the careful record handling shared
 * with the games (games/common/epsilon_files.h): deleting a game's saves,
 * NumPlay's own settings, and a copy of every save kept in a Python script. */
#include <string.h>
#include "np.h"
#include "../../games/common/epsilon_files.h"
#include "../../games/common/np_lz.h"

/* A name ending with '*' stands for every record that starts with the rest
 * (a game that keeps its world in many records, like NumBlocks). */
static int prefix_len(const char *name) {
  int n = (int)strlen(name);
  return n > 0 && name[n - 1] == '*' ? n - 1 : -1;
}

/* the name of the first record starting with `p` (n bytes) into out, false if none */
static bool first_with_prefix(const char *p, int n, char *out, uint32_t size) {
  ef_fs_t fs;
  if (!ef_open(&fs)) return false;
  int end = ef_end(&fs);
  for (uint32_t at = 0; end >= 0 && (int)at < end; at += ef_rd16(fs.buf + at)) {
    const char *r = (const char *)fs.buf + at + 2;
    if (strncmp(r, p, (size_t)n)) continue;
    uint32_t k = 0;
    while (r[k] && k < size - 1) out[k] = r[k], k++;
    out[k] = 0;
    return true;
  }
  return false;
}

bool np_storage_delete(const char *name) {
  int n = prefix_len(name);
  if (n < 0) return ef_remove(name);
  char rec[64];
  while (first_with_prefix(name, n, rec, sizeof rec))
    if (!ef_remove(rec)) return false;
  return true;
}

uint32_t np_storage_record_size(const char *name) {
  uint32_t len = 0;
  if (prefix_len(name) < 0) return ef_read(name, &len) ? len : 0;
  /* every record with the prefix */
  ef_fs_t fs;
  if (!ef_open(&fs)) return 0;
  int end = ef_end(&fs), n = prefix_len(name);
  uint32_t total = 0;
  for (uint32_t at = 0; end >= 0 && (int)at < end; at += ef_rd16(fs.buf + at))
    if (!strncmp((const char *)fs.buf + at + 2, name, (size_t)n)) total += ef_rd16(fs.buf + at);
  return total;
}

bool np_reset_game(int game) {
  bool ok = true;
  for (const char *const *r = np_games[game].progress; r && *r; r++) ok &= np_storage_delete(*r);
  return ok;
}

#define CONFIG_NAME "numplay.set"
#define CONFIG_MAGIC 0x4E /* 'N' */

#if !NP_SIMULATOR
extern const uint8_t np_defaults[4];  /* how this NumPlay starts: launcher/app_name.c, or the website */
#endif

void np_config_load(np_config_t *c) {
  c->disguise = false;
  c->secret = NP_SECRET_XNT;
  c->hint = true;
#if !NP_SIMULATOR
  if (np_defaults[0] == 'D' && np_defaults[1] == 1) {
    c->disguise = np_defaults[2] & 1;
    c->hint = !(np_defaults[2] & 2);
    if (np_defaults[3] < NP_SECRET_COUNT) c->secret = np_defaults[3];
  }
#endif
  uint32_t len = 0;
  const uint8_t *d = ef_read(CONFIG_NAME, &len);
  if (!d || len != 4 || d[0] != CONFIG_MAGIC || d[1] != 1) return;
  c->disguise = d[2] & 1;
  c->hint = !(d[2] & 2); /* a flag for "no hint": older files keep the hint on */
  if (d[3] < NP_SECRET_COUNT) c->secret = d[3];
}

bool np_config_save(const np_config_t *c) {
  uint8_t d[4] = {CONFIG_MAGIC, 1, (uint8_t)(c->disguise | (c->hint ? 0 : 2)), c->secret};
  return ef_write(CONFIG_NAME, d, sizeof d);
}

/* Which live background of the home screen was chosen last (launcher/src/live.c). */
#define BG_NAME "npbg.set"
bool np_bg_load(int *mode) {
  uint32_t len = 0;
  const uint8_t *d = ef_read(BG_NAME, &len);
  if (!d || len != 3 || d[0] != 'B' || d[1] != 1) return false;
  *mode = d[2];
  return true;
}

bool np_bg_save(int mode) {
  uint8_t d[3] = {'B', 1, (uint8_t)mode};
  return ef_write(BG_NAME, d, sizeof d);
}

/* A game's save, for a game that uses NumPlay's file functions instead of its own (Block Blast). */
const uint8_t *np_file_read(const char *name, uint32_t *len) { return ef_read(name, len); }
bool np_file_write(const char *name, const void *data, uint32_t len) { return ef_write(name, data, len); }

/* ---------------------------------------------------------------- progress copy
 * Installing apps restarts the calculator, which empties its file system; the
 * NumWorks installer puts back the Python scripts only. So NumPlay keeps every
 * save in a script too, as comment lines, and after an update (none of those
 * files left) writes them back:
 *   "#>name:base64"          the save as it is;
 *   "#=name:size:base64"     packed (games/common/np_lz.h), when that is smaller: the file system holds
 *                            42 KB, and a copy that takes less of it leaves more to the saves. */
#define COPY_NAME "numplay_saves.py"
static const char copy_head[] = "# NumPlay's saves: keep this file\n";
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static uint32_t b64_len(uint32_t n) { return (n + 2) / 3 * 4; }
/* (written out, not as a loop over the four characters: GCC 15.2 for the calculator, inlining that
 * loop here, put '=' for the second character of every four) */
static void b64_encode(const uint8_t *in, uint32_t n, uint8_t *out) {
  for (uint32_t i = 0; i < n; i += 3, out += 4) {
    uint32_t left = n - i, v = (uint32_t)in[i] << 16 | (left > 1 ? in[i + 1] << 8 : 0) | (left > 2 ? in[i + 2] : 0);
    out[0] = (uint8_t)b64[v >> 18 & 63];
    out[1] = (uint8_t)b64[v >> 12 & 63];
    out[2] = left > 1 ? (uint8_t)b64[v >> 6 & 63] : '=';
    out[3] = left > 2 ? (uint8_t)b64[v & 63] : '=';
  }
}
static int b64_value(uint8_t c) {
  for (int i = 0; i < 64; i++)
    if ((uint8_t)b64[i] == c) return i;
  return -1;
}
/* decodes len characters (a multiple of 4) into out, at most cap bytes; the size, or -1 */
static int32_t b64_decode(const uint8_t *in, uint32_t len, uint8_t *out, uint32_t cap) {
  uint32_t o = 0;
  for (uint32_t i = 0; i + 4 <= len; i += 4) {
    int v[4];
    for (int k = 0; k < 4; k++) v[k] = in[i + k] == '=' ? 0 : b64_value(in[i + k]);
    if (v[0] < 0 || v[1] < 0 || v[2] < 0 || v[3] < 0) return -1;
    uint32_t w = (uint32_t)(v[0] << 18 | v[1] << 12 | v[2] << 6 | v[3]);
    int n = 3 - (in[i + 3] == '=') - (in[i + 2] == '=');
    for (int k = 0; k < n; k++) {
      if (o >= cap) return -1;
      out[o++] = (uint8_t)(w >> (16 - 8 * k));
    }
  }
  return (int32_t)o;
}

/* The packer's table and a buffer, from the arena: it is free while no game runs and no screen is
 * up (the home screen and the settings take it back with ui_init). */
static uint16_t *lz_table;
static uint8_t *lz_buf;
static uint32_t lz_cap;
static bool lz_scratch(uint32_t largest) {
  np_alloc_reset();
  lz_table = np_alloc(NP_LZ_HASH * sizeof(uint16_t));
  lz_cap = NP_LZ_BOUND(largest) + 4;
  lz_buf = np_alloc(lz_cap);
  return lz_table && lz_buf;
}

/* a game that keeps a copy of its saves in a script of its own (one of its records), as the big
 * games do: the installer keeps scripts, so its saves come back without NumPlay's copy */
static bool own_copy(int g) {
  for (const char *const *r = np_games[g].records; r && *r; r++) {
    size_t n = strlen(*r);
    if (n > 3 && !strcmp(*r + n - 3, ".py")) return true;
  }
  return false;
}

/* Every save file NumPlay knows, each name once (a name ending with '*': every record there is that
 * starts with the rest, like NumBlocks' regions); false when i is past the end */
static bool save_name(int i, char out[64]) {
  if (i-- == 0) return strcpy(out, CONFIG_NAME), true;
  if (i-- == 0) return strcpy(out, BG_NAME), true; /* the home screen's background */
  for (int g = 0; g < np_game_count; g++)
    for (const char *const *r = own_copy(g) ? NULL : np_games[g].records; r && *r; r++) {
      bool seen = false;
      for (int h = 0; h <= g && !seen; h++)
        for (const char *const *q = np_games[h].records; q && *q && q != r && !seen; q++) seen = !strcmp(*q, *r);
      if (seen) continue;
      int n = prefix_len(*r);
      if (n < 0) {
        if (i-- == 0) return strcpy(out, *r), true;
        continue;
      }
      ef_fs_t fs;
      if (!ef_open(&fs)) return false;
      int end = ef_end(&fs);
      for (uint32_t at = 0; end >= 0 && (int)at < end; at += ef_rd16(fs.buf + at)) {
        const char *rec = (const char *)fs.buf + at + 2;
        if (strncmp(rec, *r, (size_t)n) || strlen(rec) >= 64 || i-- != 0) continue;
        return strcpy(out, rec), true;
      }
    }
  return false;
}

static uint8_t *body(const char *name, uint32_t *len) { return (uint8_t *)(uintptr_t)ef_read(name, len); }

/* One line of the copy: its name, its data (base64) and, packed ("#="), the size of the save
 * (0 for a save as it is, "#>"); false when there are no more */
static bool copy_line(const uint8_t *text, uint32_t n, uint32_t *at, char name[32], const uint8_t **data,
                      uint32_t *len, uint32_t *size) {
  while (*at < n) {
    const uint8_t *l = text + *at, *e = l;
    while (e < text + n && *e != '\n') e++;
    *at = (uint32_t)(e - text) + 1;
    const uint8_t *colon = l;
    while (colon < e && *colon != ':') colon++;
    if (e - l < 3 || l[0] != '#' || (l[1] != '>' && l[1] != '=') || colon == e || colon - l - 2 >= 32) continue;
    for (int i = 0; i < colon - l - 2; i++) name[i] = (char)l[2 + i];
    name[colon - l - 2] = 0;
    *size = 0;
    if (l[1] == '=') {
      const uint8_t *d = colon + 1;
      uint32_t v = 0;
      while (d < e && *d >= '0' && *d <= '9' && v < 0x10000) v = v * 10 + (uint32_t)(*d++ - '0');
      if (d == e || *d != ':' || !v || v >= 0x10000) continue;
      *size = v;
      colon = d;
    }
    *data = colon + 1;
    *len = (uint32_t)(e - colon - 1);
    return true;
  }
  return false;
}

/* Set when a save of the copy could not come back (no room): the copy is then
 * left as it is, so what it holds is never lost. */
static bool restore_incomplete;

void np_progress_restore(void) {
  uint32_t n = 0;
  const uint8_t *c = ef_read(COPY_NAME, &n);
  if (!c || n < 2) return;
  c++, n--; /* the script's status byte */
  char name[32];
  const uint8_t *data;
  uint32_t at, len, size, largest = 0;
  for (at = 0; copy_line(c, n, &at, name, &data, &len, &size);)
    if (size && len / 4 * 3 > largest) largest = len / 4 * 3;
  bool scratch = !largest || lz_scratch(largest);
  /* Each save that is missing comes back; the ones that are here are newer and
   * stay. Save by save, so that one file already made (a game played on its
   * own before NumPlay, after an update) does not keep the others away. */
  for (at = 0; copy_line(c, n, &at, name, &data, &len, &size);) {
    if (ef_read(name, &(uint32_t){0})) continue;
    if (len % 4 || !len) continue;
    int32_t packed = -1;
    if (size) { /* unpacked from the scratch buffer, after the record is made */
      if (!scratch || (packed = b64_decode(data, len, lz_buf, lz_cap)) < 0) continue;
    } else {
      size = len / 4 * 3 - (data[len - 1] == '=') - (data[len - 2] == '=');
    }
    uint32_t off = (uint32_t)(data - c);
    if (!ef_write(name, NULL, size)) { /* appended: the copy stays where it is */
      restore_incomplete = true;
      continue;
    }
    c = ef_read(COPY_NAME, &n) + 1, n--;
    data = c + off;
    uint32_t got;
    uint8_t *out = body(name, &got);
    bool ok = out && got == size &&
              (packed >= 0 ? np_lz_unpack(lz_buf, (uint32_t)packed, out, size) == (int32_t)size
                           : b64_decode(data, len, out, size) == (int32_t)size);
    if (!ok) { /* (a damaged line: no half save) */
      ef_remove(name);
      c = ef_read(COPY_NAME, &n) + 1, n--;
    }
  }
}

/* the copy's line for a save of len bytes at d: its length, and written at o if o is not NULL; packed
 * when that is smaller (with the scratch buffer) */
static uint32_t backup_line(const char *name, const uint8_t *d, uint32_t len, uint8_t *o) {
  uint32_t nl = (uint32_t)strlen(name), packed = lz_buf && len <= lz_cap ? np_lz_pack(d, len, NULL, lz_table) : len;
  char digits[8];
  int nd = 0;
  bool pack = packed < len && b64_len(packed) + 7 < b64_len(len);
  if (pack)
    for (uint32_t v = len; nd == 0 || v; v /= 10) digits[nd++] = (char)('0' + v % 10);
  uint32_t total = 2 + nl + 1 + (pack ? (uint32_t)nd + 1 + b64_len(packed) : b64_len(len)) + 1;
  if (!o) return total;
  *o++ = '#', *o++ = pack ? '=' : '>';
  for (uint32_t k = 0; k < nl; k++) *o++ = (uint8_t)name[k];
  *o++ = ':';
  if (pack) {
    while (nd) *o++ = (uint8_t)digits[--nd];
    *o++ = ':';
    np_lz_pack(d, len, lz_buf, lz_table);
    b64_encode(lz_buf, packed, o);
    o += b64_len(packed);
  } else {
    b64_encode(d, len, o);
    o += b64_len(len);
  }
  *o = '\n';
  return total;
}

void np_progress_backup(void) {
  if (restore_incomplete) return;
  char name[64];
  uint32_t total = 1 + sizeof copy_head - 1 + 1, len, largest = 0; /* status byte, text, terminating zero */
  for (int i = 0; save_name(i, name); i++)
    if (ef_read(name, &len) && len > largest) largest = len;
  if (!largest || !lz_scratch(largest)) lz_buf = NULL, lz_table = NULL;
  for (int i = 0; save_name(i, name); i++) {
    const uint8_t *d = ef_read(name, &len);
    if (d) total += backup_line(name, d, len, NULL);
  }
  /* never lose the copy for want of room: keep the old one then */
  ef_fs_t fs;
  if (!ef_open(&fs)) return;
  int end = ef_end(&fs);
  if (end < 0) return;
  uint32_t old = 0;
  int was = ef_find(&fs, COPY_NAME, end);
  if (was >= 0) old = ef_rd16(fs.buf + was);
  uint32_t need = 2 + sizeof COPY_NAME + total;
  if (need != old && (uint32_t)end + 2 + need > fs.size + old) return;
  if (!ef_write(COPY_NAME, NULL, total)) return;
  uint8_t *o = body(COPY_NAME, &len);
  if (!o || len != total) return;
  *o++ = 0; /* not imported by the Python shell */
  for (const char *h = copy_head; *h; h++) *o++ = (uint8_t)*h;
  uint8_t *stop = o + total;
  for (int i = 0; save_name(i, name); i++) {
    const uint8_t *d = ef_read(name, &len);
    if (!d) continue;
    uint32_t l = backup_line(name, d, len, NULL);
    if (o + l >= stop) continue;
    o += backup_line(name, ef_read(name, &len), len, o);
  }
  *o = 0;
}
