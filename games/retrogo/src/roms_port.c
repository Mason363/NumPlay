/* Retro-Go on NumPlay's other targets (platform/): the games are files in a folder, read once into the table
 * tools/romtable.py makes (main.c reads it as it reads np_roms on the calculator), each game's bytes made as it makes
 * them: Game Boy and Sega 8-bit games without their padding, a .gw game unpacked, a Mega Drive game plain and its
 * bytes swapped in pairs.
 *
 * The folder: NP_ROMS, or retrogo in the folder of NumPlay's files (NP_SAVES, ~/.numplay on a computer,
 * /documents/retrogo on a TI-Nspire, where a file's name may end with .tns). A file's last extension gives its
 * system (.gb .gbc .nes .sms .gg .sg .col .gw, .md .gen .bin .smd, in any case), the rest of its name the game's. */
#ifdef NP_PORT
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "np_port.h"
#include "romtable.h"

static const struct {
  const char *ext;
  uint8_t system;
} EXT[] = {{"gb", RG_SYS_GB}, {"gbc", RG_SYS_GBC}, {"nes", RG_SYS_NES}, {"sms", RG_SYS_SMS}, {"gg", RG_SYS_GG},
           {"sg", RG_SYS_SG},  {"col", RG_SYS_CV},  {"gw", RG_SYS_GW},   {"md", RG_SYS_MD},   {"gen", RG_SYS_MD},
           {"bin", RG_SYS_MD}, {"smd", RG_SYS_MD}};

static int lower(int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }
static bool same(const char *a, const char *b, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (lower((unsigned char)a[i]) != lower((unsigned char)b[i])) return false;
  return !b[n];
}

const char *rg_port_folder(void) {
  static char path[512];
  if (path[0]) return path;
  const char *env = getenv("NP_ROMS"), *saves = getenv("NP_SAVES"), *home = getenv("HOME");
  if (!home) home = getenv("USERPROFILE");
  if (env) snprintf(path, sizeof path, "%s", env);
  else if (!strcmp(np_target(), "nspire")) snprintf(path, sizeof path, "/documents/retrogo");
  else if (saves) snprintf(path, sizeof path, "%s/retrogo", saves);
  else snprintf(path, sizeof path, "%s/.numplay/retrogo", home ? home : ".");
  return path;
}

/* the folder as the welcome screen shows it: ~ for the home folder, the end of a long one */
const char *rg_port_folder_shown(void) {
  static char shown[40];
  const char *p = rg_port_folder(), *home = getenv("HOME");
  size_t h = home ? strlen(home) : 0;
  char tmp[520];
  if (h > 1 && !strncmp(p, home, h) && p[h] == '/') snprintf(tmp, sizeof tmp, "~%s", p + h);
  else snprintf(tmp, sizeof tmp, "%s", p);
  size_t n = strlen(tmp);
  if (n < sizeof shown) memcpy(shown, tmp, n + 1);
  else memcpy(shown, "...", 3), memcpy(shown + 3, tmp + n - (sizeof shown - 4), sizeof shown - 3);
  return shown;
}

/* a file's system (0: not a game), and the length of its name without its extension */
static uint8_t system_of(const char *file, size_t *name_len) {
  size_t n = strlen(file);
  if (n > 4 && same(file + n - 4, ".tns", 4)) n -= 4;
  size_t dot = n;
  while (dot > 0 && file[dot - 1] != '.') dot--;
  if (dot < 2) return 0;
  for (size_t i = 0; i < sizeof EXT / sizeof EXT[0]; i++)
    if (same(file + dot, EXT[i].ext, n - dot)) {
      *name_len = dot - 1;
      return EXT[i].system;
    }
  return 0;
}

static void wr32(uint8_t *p, uint32_t v) {
  for (int i = 0; i < 4; i++) p[i] = (uint8_t)(v >> (8 * i));
}
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* ---- a game's bytes, as tools/romtable.py makes them ---- */

/* an LZ4 frame unpacked (LCD-Game-Shrinker's .gw files; its checksums not checked): a new buffer, or NULL when it isn't
   one or is broken */
static bool room(uint8_t **out, uint32_t *cap, uint32_t need) {
  if (need <= *cap) return true;
  uint8_t *m = realloc(*out, need * 2);
  if (!m) return false;
  *out = m, *cap = need * 2;
  return true;
}
static uint8_t *lz4_unpack(const uint8_t *d, uint32_t n, uint32_t *out_n) {
  if (n < 7 || rd32(d) != 0x184D2204) return NULL;
  uint8_t flg = d[4], b;
  uint32_t i = 7 + (flg & 0x08 ? 8 : 0) + (flg & 0x01 ? 4 : 0), cap = 0, len = 0;
  uint8_t *out = NULL;
  while (i + 4 <= n) {
    uint32_t bn = rd32(d + i);
    i += 4;
    if (!bn) break;
    bool raw = bn >> 31;
    bn &= 0x7FFFFFFF;
    if (bn > n - i) goto broken;
    const uint8_t *blk = d + i;
    i += bn + (flg & 0x10 ? 4 : 0);
    if (raw) {
      if (!room(&out, &cap, len + bn)) goto broken;
      memcpy(out + len, blk, bn), len += bn;
      continue;
    }
    for (uint32_t j = 0; j < bn;) {
      uint8_t tok = blk[j++];
      uint32_t lit = tok >> 4, ml = (tok & 15) + 4u;
      if (lit == 15) do {
          if (j >= bn) goto broken;
          lit += b = blk[j++];
        } while (b == 255);
      if (lit > bn - j || !room(&out, &cap, len + lit)) goto broken;
      memcpy(out + len, blk + j, lit), len += lit, j += lit;
      if (j >= bn) break;
      if (bn - j < 2) goto broken;
      uint32_t off = blk[j] | blk[j + 1] << 8;
      j += 2;
      if (ml == 19) do {
          if (j >= bn) goto broken;
          ml += b = blk[j++];
        } while (b == 255);
      if (!off || off > len || !room(&out, &cap, len + ml)) goto broken;
      for (uint32_t k = 0; k < ml; k++, len++) out[len] = out[len - off];
    }
  }
  if (!out) out = malloc(1);
  *out_n = len;
  return out;
broken:
  free(out);
  return NULL;
}

/* a Mega Drive game as gwenesis reads it in place: plain, padded to an even size, its bytes swapped in pairs. An
   .smd file (its 512-byte header, then each 16 KB block's odd bytes before its even ones) is put back in order first */
static bool is_smd(const uint8_t *head, uint32_t n, bool smd_name) {
  return (smd_name || (n > 0x200 && head[1] == 3 && head[8] == 0xAA && head[9] == 0xBB)) && n % 0x4000 == 0x200;
}
static uint32_t md_size(uint32_t n, bool smd) { return ((smd ? n - 0x200 : n) + 1) & ~1u; }
/* the game read from f into out (md_size bytes), made as the calculator has it */
static bool md_read(FILE *f, uint32_t n, bool smd, uint8_t *out) {
  if (smd) {
    uint8_t *blk = malloc(0x4000);
    bool ok = blk && !fseek(f, 0x200, SEEK_SET);
    for (uint32_t k = 0; ok && k < n - 0x200; k += 0x4000) {
      ok = fread(blk, 1, 0x4000, f) == 0x4000;
      for (uint32_t j = 0; ok && j < 0x2000; j++) out[k + 2 * j] = blk[0x2000 + j], out[k + 2 * j + 1] = blk[j];
    }
    free(blk);
    if (!ok) return false;
    n -= 0x200;
  } else if (fseek(f, 0, SEEK_SET) || fread(out, 1, n, f) != n)
    return false;
  if (n & 1) out[n] = 0, n++;
  for (uint32_t k = 0; k < n; k += 2) {
    uint8_t t = out[k];
    out[k] = out[k + 1], out[k + 1] = t;
  }
  return true;
}

/* the padding a Game Boy or Sega 8-bit file has to reach a power of two (16 KB banks at its end, all 0xFF or all
   0x00) left out: the length kept, read from the file's end */
static uint32_t trim(FILE *f, uint32_t n, uint8_t system) {
  bool gb = system == RG_SYS_GB || system == RG_SYS_GBC;
  if (!(gb || system == RG_SYS_SMS || system == RG_SYS_GG || system == RG_SYS_SG) || n % 16384 || !n) return n;
  uint32_t keep = gb ? 0x8000 : n <= 0x8000 ? n : 0xC000, end = n;
  uint8_t *blk = malloc(16384);
  if (!blk) return n;
  int pad = -1;
  for (; end >= keep + 16384; end -= 16384) {
    if (fseek(f, (long)(end - 16384), SEEK_SET) || fread(blk, 1, 16384, f) != 16384) break;
    if (pad < 0) pad = blk[16383];
    if (pad != 0x00 && pad != 0xFF) break;
    uint32_t k = 0;
    while (k < 16384 && blk[k] == pad) k++;
    if (k < 16384) break;
  }
  free(blk);
  return end;
}

static uint8_t empty[12], *table;
static uint32_t table_size;

/* every game's size as it will be first (a .gw one unpacked then), then the table, each game read into its place */
static void load(void) {
  if (table) return;
  wr32(empty, RG_ROMS_MAGIC), wr32(empty + 4, RG_ROMS_VERSION);
  table = empty, table_size = sizeof empty;
  struct {
    char path[600];
    uint8_t *made; /* (a .gw game unpacked) */
    uint32_t n, size;
    uint8_t system, smd, name[RG_ROMS_NAME];
  } *games = calloc(RG_MAX_ROMS, sizeof *games);
  DIR *dir = games ? opendir(rg_port_folder()) : NULL;
  uint32_t count = 0, total = 0;
  for (struct dirent *e; dir && count < RG_MAX_ROMS && (e = readdir(dir));) {
    size_t name_len;
    uint8_t system = system_of(e->d_name, &name_len);
    if (!system) continue;
    char *path = games[count].path;
    snprintf(path, sizeof games[count].path, "%s/%s", rg_port_folder(), e->d_name);
    FILE *f = fopen(path, "rb");
    if (!f) continue;
    long n = fseek(f, 0, SEEK_END) ? -1 : ftell(f);
    uint32_t size = (uint32_t)n;
    bool ok = n > 0 && n <= 64L << 20;
    if (ok && system == RG_SYS_GW) {
      uint8_t *d = malloc(size);
      ok = d && !fseek(f, 0, SEEK_SET) && fread(d, 1, size, f) == size;
      if (ok) games[count].made = lz4_unpack(d, size, &size);
      free(d);
      if (!games[count].made) size = (uint32_t)n;
    } else if (ok && system == RG_SYS_MD) {
      uint8_t head[10] = {0};
      size_t len = strlen(e->d_name), tns = len > 4 && same(e->d_name + len - 4, ".tns", 4) ? 4 : 0;
      ok = !fseek(f, 0, SEEK_SET) && fread(head, 1, n < 10 ? (size_t)n : 10, f) > 0;
      games[count].smd = is_smd(head, size, len > 4 + tns && same(e->d_name + len - 4 - tns, ".smd", 4));
      size = md_size(size, games[count].smd);
    } else if (ok)
      size = trim(f, size, system);
    fclose(f);
    if (!ok) {
      free(games[count].made), games[count].made = NULL;
      continue;
    }
    /* (the name cut to 40 bytes, not inside a letter) */
    size_t k = name_len < RG_ROMS_NAME ? name_len : RG_ROMS_NAME;
    while (k < name_len && k > 0 && ((unsigned char)e->d_name[k] & 0xC0) == 0x80) k--;
    memcpy(games[count].name, e->d_name, k);
    games[count].n = (uint32_t)n, games[count].size = size, games[count].system = system;
    total += (size + 3) & ~3u;
    count++;
  }
  if (dir) closedir(dir);
  uint32_t first = (12 + RG_ROMS_ENTRY * count + 3) & ~3u;
  uint8_t *t = count ? calloc(1, first + total) : NULL;
  if (t) {
    wr32(t, RG_ROMS_MAGIC), wr32(t + 4, RG_ROMS_VERSION);
    uint32_t at = first, kept = 0;
    for (uint32_t i = 0; i < count; i++) {
      bool whole;
      if (games[i].made) whole = true, memcpy(t + at, games[i].made, games[i].size);
      else {
        FILE *f = fopen(games[i].path, "rb");
        if (!f) continue;
        if (games[i].system == RG_SYS_MD) whole = md_read(f, games[i].n, games[i].smd, t + at);
        else whole = fread(t + at, 1, games[i].size, f) == games[i].size;
        fclose(f);
      }
      if (!whole) {
        memset(t + at, 0, games[i].size);
        continue;
      }
      uint8_t *e = t + 12 + RG_ROMS_ENTRY * kept++;
      wr32(e, at), wr32(e + 4, games[i].size);
      e[8] = games[i].system;
      memcpy(e + 12, games[i].name, RG_ROMS_NAME);
      at += (games[i].size + 3) & ~3u;
    }
    wr32(t + 8, kept);
    table = t, table_size = at;
  }
  for (uint32_t i = 0; games && i < count; i++) free(games[i].made);
  free(games);
}

/* the table, and its size (read the first time either is asked) */
const uint8_t *rg_port_roms(void) {
  load();
  return table;
}
uint32_t rg_port_roms_size(void) {
  load();
  return table_size;
}
#endif
