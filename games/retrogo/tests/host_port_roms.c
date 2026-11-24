/* The other targets' table of games (src/roms_port.c, made from a folder of files), written out as it is in memory,
 * for tests/test_port_roms.py to hold against tools/romtable.py's.
 *   host_port_roms FOLDER OUT.bin */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
const char *np_target(void) { return "sdl"; }
const uint8_t *rg_port_roms(void);
uint32_t rg_port_roms_size(void);
int main(int argc, char **argv) {
  if (argc < 3) return 2;
  setenv("NP_ROMS", argv[1], 1);
  const uint8_t *t = rg_port_roms();
  FILE *f = fopen(argv[2], "wb");
  if (!f || fwrite(t, 1, rg_port_roms_size(), f) != rg_port_roms_size()) return 1;
  return fclose(f) != 0;
}
