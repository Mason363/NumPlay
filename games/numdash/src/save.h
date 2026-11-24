#ifndef NUMDASH_SAVE_H
#define NUMDASH_SAVE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "level.h"

#define CUSTOM_SLOTS 3
#define LEVEL_SLOTS (LEVEL_COUNT + CUSTOM_SLOTS)
enum { OPT_PERCENT = 1, OPT_BAR = 2, OPT_FPS = 4, OPT_AUTOCHECK = 8, OPT_LOWDETAIL = 16, OPT_NOSHAKE = 32,
       OPT_KEYS_SEEN = 64 /* the editor's key guide was shown */ };

typedef struct {
  uint8_t normal, practice, coins;   /* best percentages, coin bits */
  uint32_t attempts, jumps;
} LevelStat;

typedef struct {
  LevelStat lv[LEVEL_SLOTS];
  uint32_t attempts, jumps;          /* totals */
  uint8_t color1, color2, options, last_level;
} Progress;

typedef struct {
  uint16_t count, end_x;
  uint8_t theme, flags;
  bool exists;
} CustomMeta;

void progress_defaults(Progress *p);
/* Serialised progress record: fixed size so it never moves other records. */
#define PROGRESS_BYTES 192
void progress_encode(const Progress *p, uint8_t out[PROGRESS_BYTES]);
bool progress_decode(Progress *p, const uint8_t *in, size_t len);
#if ND_EXTRA_LEVELS
/* The other levels' stats (numdash.ndx, fixed size), read after numdash.nds. */
#define EXTRA_BYTES 128
void extra_encode(const Progress *p, uint8_t out[EXTRA_BYTES]);
bool extra_decode(Progress *p, const uint8_t *in, size_t len);
#endif
/* Custom level record; returns bytes written or 0. */
size_t custom_encode(const LObj *objs, unsigned count, const CustomMeta *m, uint8_t *out, size_t cap);
bool custom_decode(const uint8_t *in, size_t len, LObj *objs, unsigned cap, CustomMeta *m);
/* Reads a save of the previous NumDash release (NDASH002/003) into the new
 * structures; custom objects are converted when their type still exists. */
bool legacy_decode(const uint8_t *in, size_t len, Progress *p, LObj *objs, unsigned cap, unsigned counts[CUSTOM_SLOTS],
                   CustomMeta metas[CUSTOM_SLOTS]);

/* Epsilon record storage (the arena excludes both magic words). */
const uint8_t *storage_read(const uint8_t *a, size_t size, const char *name, size_t *len);
/* Writes without moving any other record: a record is only resized when it
 * is the last one; otherwise it keeps its size (padded) or the write fails.
 * New records are appended with their size rounded up to `granule`. */
bool storage_put(uint8_t *a, size_t size, const char *name, const uint8_t *data, size_t len, size_t granule);
/* Removes a record only if it is the last one (nothing moves). */
bool storage_remove_last(uint8_t *a, size_t size, const char *name);
uint32_t nd_crc32(const uint8_t *data, size_t size);

/* High level: platform storage. */
bool save_load_all(Progress *p, CustomMeta metas[CUSTOM_SLOTS]);
bool save_write_progress(const Progress *p);
bool save_write_custom(int slot, const LObj *objs, unsigned count, const CustomMeta *m);
bool save_read_custom(int slot, LObj *objs, unsigned cap, CustomMeta *m);
#endif
