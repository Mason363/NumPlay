/* Mr Bullet: what is kept, in mrbullet.sav, and a copy of it in a Python script, mrbullet_saves.py (installing
 * apps from the NumWorks website keeps only scripts, so the save comes back from it).
 *
 * One format: a magic, a version, its size, then the progress, then a checksum of all before it. A file that is
 * damaged, cut short or from elsewhere is not read: the copy is tried, then a fresh start. */
#ifndef MB_SAVE_H
#define MB_SAVE_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

#define MB_SAVE_NAME "mrbullet.sav"
#define MB_COPY_NAME "mrbullet_saves.py"
#define MB_MAGIC 0x3142524Du     /* "MRB1" */
#define MB_VERSION 1

enum { LV_CLEARED = 1 };

typedef struct {
  uint8_t stars[LEVEL_COUNT];     /* best stars, 0 to 3 */
  uint8_t flags[LEVEL_COUNT];     /* LV_* */
  uint16_t last[MODE_COUNT];      /* the last level played in each mode */
  uint8_t guide;                  /* the first-run guide was shown */
  uint8_t mode;                   /* the last mode */
} Progress;

typedef struct {
  uint32_t magic;
  uint16_t version, size;
  Progress p;
  uint32_t sum;                   /* FNV-1a of everything before it */
} SaveFile;

/* the memory saving and loading work in: at least SAVE_SCRATCH bytes */
#define SAVE_SCRATCH 4096
void save_scratch(void *mem);
void save_fresh(Progress *p);
uint32_t save_sum(const void *data, uint32_t n);
bool save_ok(const SaveFile *f);
bool save_load(Progress *p);      /* false: a fresh start */
bool save_write(const Progress *p);
#endif
