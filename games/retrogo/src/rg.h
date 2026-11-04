/* What Retro-Go's screens (main.c) and its emulators (gb/, nes/) share. */
#ifndef RG_H
#define RG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The running emulator's memory: one static block, handed out at load and taken back at the
   next. rg_alloc gives zeroed, 8-aligned memory, or NULL when the block is full. */
void *rg_alloc(uint32_t size);
void rg_alloc_reset(void);
/* the block's size: a Game Boy Color game's 32 KB of work RAM, 16 KB of video RAM and 32 KB of
   cartridge RAM, the most a game asks for */
#define RG_HEAP_SIZE (82 * 1024)

/* The picture: a core hands over each line of its frame as RGB565 pixels, top to bottom,
   when the frame is drawn (rg_drawing). */
extern bool rg_drawing;
void rg_line(int y, const uint16_t *px);

/* The parts of a running machine a save state keeps, in order. */
typedef struct {
  void *at;
  uint32_t size;
} rg_chunk_t;
#define RG_MAX_CHUNKS 24 /* (the Mega Drive: 21) */

/* The cores' failures when loading a game, shown by the screens. */
enum {
  RG_OK = 0,
  RG_ERR_ROM = -1,    /* not a ROM this emulator can read */
  RG_ERR_MAPPER = -2, /* a cartridge type it doesn't emulate */
  RG_ERR_MEMORY = -3, /* needs more RAM than the calculator has */
};

/* the pad's buttons, for every system */
enum {
  RG_PAD_A = 1, RG_PAD_B = 2, RG_PAD_SELECT = 4, RG_PAD_START = 8,
  RG_PAD_UP = 16, RG_PAD_DOWN = 32, RG_PAD_LEFT = 64, RG_PAD_RIGHT = 128
};

#endif
