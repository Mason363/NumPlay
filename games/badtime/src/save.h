/* What's kept (the furthest attack reached, the wins, how to play read, Endless' best runs), in badtime.sav, and a
 * copy of it in a Python script, badtime_saves.py (installing apps from the NumWorks website keeps only scripts,
 * so the save comes back from it). */
#ifndef BT_SAVE_H
#define BT_SAVE_H
#include <stdbool.h>
#include <stdint.h>

#define BT_SAVE_NAME "badtime.sav"
#define BT_COPY_NAME "badtime_saves.py"
#define BT_MAGIC 0x31535442u       /* "BTS1" */
#define BT_VERSION 1

typedef struct {
  uint8_t best;                    /* the furthest attack reached in a whole fight, 0 (none) to 24 */
  uint8_t guide;                   /* how to play shown before the first fight */
  uint16_t wins;                   /* fights won */
  uint16_t endless[2];             /* the most attacks dodged in a row in Endless, phase 1 and 2 */
  uint8_t spare[4];
} bt_progress;

typedef struct {
  uint32_t magic;
  uint16_t version, size;
  bt_progress p;
  uint32_t sum;                    /* FNV-1a of everything before it */
} bt_file;
_Static_assert(sizeof(bt_file) == 24, "badtime.sav: 24 bytes");

void bt_fresh(bt_progress *p);
uint32_t bt_sum(const void *data, uint32_t n);
bool bt_file_ok(const bt_file *f);
bool save_load(bt_progress *p);    /* the save, its copy, or a fresh start; false: a fresh start */
bool save_write(const bt_progress *p);
#endif
