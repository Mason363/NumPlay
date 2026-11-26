/* The calculator's file list, changed by the games' own code (games/common/epsilon_files.h) and the saves' packer
 * (games/common/np_lz.h), built for this computer: tests/test_saves.py runs the same changes through
 * customize/saves.js and wants the same bytes.
 *
 * Reads commands from stdin, one a line, and answers each with a line:
 *   size N        a list of N bytes, empty                 -> "ok"
 *   w NAME HEX    ef_write(NAME, the bytes; "-" for none)  -> "1" or "0", then the list's checksum
 *   r NAME        ef_remove(NAME)                          -> "1" or "0", then the list's checksum
 *   dump          the whole list                           -> its bytes in hex
 *   pack HEX      np_lz_pack of the bytes                  -> the packed bytes in hex ("-" for none) */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* (in place of games/common/epsilon_app.h: the files are here, and nothing remembers the last one looked up,
 * as on the calculators that aren't the NumWorks, so a file in the middle can always go) */
#define EPSILON_APP_H
#define NP_PORT 1
static uint8_t *store;
static uint32_t store_size;
static uint8_t *epsilon_storage(uint32_t *size) {
  *size = store_size;
  return store;
}
static void np_storage_changed(void) {}
#include "../../games/common/epsilon_files.h"
#include "../../games/common/np_lz.h"

static uint32_t sum(void) { /* FNV-1a */
  uint32_t h = 0x811C9DC5u;
  for (uint32_t i = 0; i < store_size; i++) h = (h ^ store[i]) * 0x01000193u;
  return h;
}
static uint32_t unhex(const char *s, uint8_t *out) {
  uint32_t n = 0;
  if (s[0] == '-') return 0;
  for (; s[0] && s[1] && s[0] != '\n'; s += 2) {
    unsigned v;
    sscanf(s, "%2x", &v);
    out[n++] = (uint8_t)v;
  }
  return n;
}

int main(void) {
  static char line[400000], name[128];
  static uint8_t data[70000], packed[NP_LZ_BOUND(70000)];
  static uint16_t table[NP_LZ_HASH];
  while (fgets(line, sizeof line, stdin)) {
    char *arg = strchr(line, ' ');
    if (arg) *arg++ = 0;
    if (!strncmp(line, "size", 4)) {
      free(store);
      store_size = (uint32_t)atoi(arg);
      store = calloc(store_size, 1);
      puts("ok");
    } else if (!strcmp(line, "w")) {
      char *hex = strchr(arg, ' ');
      *hex++ = 0;
      snprintf(name, sizeof name, "%s", arg);
      uint32_t n = unhex(hex, data);
      printf("%d %08x\n", ef_write(name, n ? data : NULL, n), (unsigned)sum());
    } else if (!strcmp(line, "r")) {
      arg[strcspn(arg, "\n")] = 0;
      printf("%d %08x\n", ef_remove(arg), (unsigned)sum());
    } else if (!strncmp(line, "dump", 4)) {
      for (uint32_t i = 0; i < store_size; i++) printf("%02x", store[i]);
      putchar('\n');
    } else if (!strcmp(line, "pack")) {
      uint32_t n = unhex(arg, data), p = np_lz_pack(data, n, packed, table);
      if (!p) putchar('-');
      for (uint32_t i = 0; i < p; i++) printf("%02x", packed[i]);
      putchar('\n');
    }
    fflush(stdout);
  }
  return 0;
}
