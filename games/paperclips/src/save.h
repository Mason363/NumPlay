/* The save: paperclips.sav, the game's state as bytes (pc.h's pc_game, but for the console), with a magic, a
 * version, its size and a checksum, and a copy of it in a Python script, paperclips_saves.py (installing apps from
 * the NumWorks website keeps only scripts, so the save comes back from it). */
#ifndef PC_SAVE_H
#define PC_SAVE_H
#include <stddef.h>
#include "pc.h"

#define PC_SAVE_NAME "paperclips.sav"
#define PC_COPY_NAME "paperclips_saves.py"
#define PC_MAGIC 0x31435055u        /* "UPC1" */
#define PC_VERSION 1
#define PC_KEPT offsetof(pc_game, msg)   /* everything but the console */

typedef struct {
  uint32_t magic;
  uint16_t version, size;
  uint8_t guide, tab, spare[2];   /* how to play shown at the start, the tab last open */
} pc_head;
#define PC_FILE_SIZE (sizeof(pc_head) + PC_KEPT + 4)

uint32_t pc_sum(const void *data, uint32_t n);
bool pc_game_ok(const pc_game *s);
bool pc_file_ok(const uint8_t *f, uint32_t n);
/* the save, its copy, or false: then *s is untouched */
bool save_load(pc_game *s, uint8_t *guide, uint8_t *tab);
bool save_write(const pc_game *s, uint8_t guide, uint8_t tab);
void save_pack(const pc_game *s, uint8_t guide, uint8_t tab, uint8_t *out);   /* PC_FILE_SIZE bytes */
#endif
