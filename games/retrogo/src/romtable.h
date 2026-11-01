/* Retro-Go's games: the table np_roms in the app's flash (tools/romtable.py and the customizer
 * website write it), read in place. */
#ifndef RG_ROMTABLE_H
#define RG_ROMTABLE_H
#include <stdbool.h>
#include <stdint.h>

#define RG_ROMS_MAGIC 0x4D52504Eu /* "NPRM" */
#define RG_ROMS_VERSION 1
#define RG_ROMS_ENTRY 52
#define RG_ROMS_NAME 40
#define RG_MAX_ROMS 96

enum { RG_SYS_GB = 1, RG_SYS_GBC, RG_SYS_NES, RG_SYS_SMS, RG_SYS_GG, RG_SYS_SG, RG_SYS_COUNT };

typedef struct {
  const uint8_t *data;
  uint32_t size;
  const uint8_t *name; /* RG_ROMS_NAME bytes in the table, zero-padded (maybe not ended) */
  uint8_t system;
} rg_rom_t;

/* The games in a table of which `limit` bytes can be read, at most `max`, sorted by system and
   then by name; entries that don't hold together are left out. -1 when it isn't a table at all. */
int rg_romtable_read(const uint8_t *table, uint32_t limit, rg_rom_t *out, int max);
/* a game's name as the screens can show it: ASCII, '?' for any other letter, n bytes at most
   with the ending zero */
void rg_rom_name(const rg_rom_t *rom, char *out, int n);
#endif
