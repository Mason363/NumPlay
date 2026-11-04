/* Retro-Go on NumPlay's other targets (platform/): the games are files in a folder, read once into the table
 * tools/romtable.py makes (main.c reads it as it reads np_roms on the calculator).
 *
 * The folder: NP_ROMS, or retrogo in the folder of NumPlay's files (NP_SAVES, ~/.numplay on a computer,
 * /documents/retrogo on a TI-Nspire, where a file's name may end with .tns). A file's last extension gives its
 * system (.gb .gbc .nes .sms .gg .sg .col, in any case), the rest of its name the game's. */
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
} EXT[] = {{"gb", RG_SYS_GB}, {"gbc", RG_SYS_GBC}, {"nes", RG_SYS_NES}, {"sms", RG_SYS_SMS},
           {"gg", RG_SYS_GG},  {"sg", RG_SYS_SG},   {"col", RG_SYS_CV}};

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

static uint8_t empty[12], *table;
static uint32_t table_size;

static void load(void) {
  if (table) return;
  wr32(empty, RG_ROMS_MAGIC), wr32(empty + 4, RG_ROMS_VERSION);
  table = empty, table_size = sizeof empty;
  struct {
    char path[600];
    uint32_t size;
    uint8_t system, name[RG_ROMS_NAME];
  } *games = calloc(RG_MAX_ROMS, sizeof *games);
  DIR *dir = games ? opendir(rg_port_folder()) : NULL;
  uint32_t count = 0, total = 0;
  for (struct dirent *e; dir && count < RG_MAX_ROMS && (e = readdir(dir));) {
    size_t name_len;
    uint8_t system = system_of(e->d_name, &name_len);
    if (!system) continue;
    snprintf(games[count].path, sizeof games[count].path, "%s/%s", rg_port_folder(), e->d_name);
    FILE *f = fopen(games[count].path, "rb");
    if (!f) continue;
    long n = fseek(f, 0, SEEK_END) ? -1 : ftell(f);
    fclose(f);
    if (n <= 0 || n > 64L << 20) continue;
    /* (the name cut to 40 bytes, not inside a letter) */
    size_t k = name_len < RG_ROMS_NAME ? name_len : RG_ROMS_NAME;
    while (k < name_len && k > 0 && ((unsigned char)e->d_name[k] & 0xC0) == 0x80) k--;
    memcpy(games[count].name, e->d_name, k);
    games[count].size = (uint32_t)n, games[count].system = system;
    total += ((uint32_t)n + 3) & ~3u;
    count++;
  }
  if (dir) closedir(dir);
  uint32_t first = (12 + RG_ROMS_ENTRY * count + 3) & ~3u;
  uint8_t *t = count ? calloc(1, first + total) : NULL;
  if (t) {
    wr32(t, RG_ROMS_MAGIC), wr32(t + 4, RG_ROMS_VERSION);
    uint32_t at = first, kept = 0;
    for (uint32_t i = 0; i < count; i++) {
      FILE *f = fopen(games[i].path, "rb");
      if (!f) continue;
      bool whole = fread(t + at, 1, games[i].size, f) == games[i].size;
      fclose(f);
      if (!whole) continue;
      uint8_t *e = t + 12 + RG_ROMS_ENTRY * kept++;
      wr32(e, at), wr32(e + 4, games[i].size);
      e[8] = games[i].system;
      memcpy(e + 12, games[i].name, RG_ROMS_NAME);
      at += (games[i].size + 3) & ~3u;
    }
    wr32(t + 8, kept);
    table = t, table_size = at;
  }
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
