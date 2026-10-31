/* Snow Rider 3D: the save (save.c). 32 bytes, little-endian:
 *   0 "SNR1", 4 version (1), 6 size (32), 8 best score, 12 gifts, 16 runs, 20 sleds owned (bit n: skins[n]),
 *   22 the sled ridden, 23 how to play read (1), 24 spare (0), 28 FNV-1a of bytes 0 to 27 */
#ifndef SR_SAVE_H
#define SR_SAVE_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"
#include "plat.h"

#define SAVE_VERSION 1
typedef struct {
  char magic[4];
  uint16_t version, size;
  uint32_t best, gifts, plays;
  uint16_t owned;
  uint8_t skin, guide;
  uint32_t spare;
  uint32_t sum;
} SaveFile;
_Static_assert(sizeof(SaveFile) == 32, "the save's size");

void sr_fresh(SaveFile *f);
bool sr_file_ok(const SaveFile *f);
int sr_load(SaveFile *f);   /* 1 the save, 2 its copy (written back as the save), 0 nothing good: a fresh start */
bool sr_save(SaveFile *f);
#endif
