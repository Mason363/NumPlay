/* Deltarune: data.bin (tools/pack.py), read where it lies in flash.
 *
 * Its sections (SEC_* in data.h): the original's sprites and their frames,
 * the rooms (tile grids, solids, instances), fonts and texts. Frames and tiles
 * are packed in LZMA blocks (blk.h): their pointers hold until the next
 * frame_get or tile_get. */
#ifndef DR_RES_H
#define DR_RES_H
#include <stdbool.h>
#include <stdint.h>
#include "data.h"

extern const uint8_t *dr_bin;
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline int16_t rds16(const uint8_t *p) { return (int16_t)rd16(p); }
static inline uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static inline const uint8_t *section(int s) { return dr_bin + rd32(dr_bin + 8 + 4 * s); }

/* a sprite's frame: its size and origin, palette and rows */
typedef struct {
  uint16_t w, h;
  int16_t ox, oy;
  uint8_t bpp, ncol;
  const uint8_t *pal;       /* ncol RGB565 colours (unaligned: read with rd16) */
  const uint8_t *rows;      /* u16 offsets of each row, from `data` */
  const uint8_t *data;
} Frame;
bool frame_get(uint16_t spr, int i, Frame *f);
int spr_frames(uint16_t spr);
int spr_w(uint16_t spr);
int spr_h(uint16_t spr);

/* a 20 x 20 tile: bpp 0 (one colour), 1, 2, 4 or 8; clear: colour 0 is see-through */
typedef struct {
  uint8_t bpp, ncol;
  bool clear;
  const uint8_t *pal;       /* ncol RGB565 colours */
  const uint8_t *bits;      /* 400 pixels, bpp bits each, lowest bits first */
} Tile;
void tile_get(uint16_t id, Tile *t);

/* a room: its size in the original's units (the Dark World's are twice the screen's), its tiles */
typedef struct {
  uint16_t w, h;
  uint16_t gcols, grows;
  const uint8_t *grid;      /* gcols * grows tile ids (u16), 0xFFFF none */
  const uint8_t *fgrid;     /* the same, drawn over the characters; NULL none */
  const uint8_t *solids;    /* nsolid * 10 bytes */
  const uint8_t *insts;     /* ninst * 12 bytes */
  uint16_t nsolid, ninst;
  uint8_t flags;
  uint16_t bgcol;
} RoomInfo;
#define ROOM_DARK 1
void room_info(int room, RoomInfo *r);

/* an instance the room places (of an object the game knows: OBJ ids in data.h) */
typedef struct {
  uint16_t obj;
  int16_t x, y;
  int16_t sx, sy;           /* its scale, 1/100 */
  uint16_t n;               /* its place among the room's instances */
} Inst;
void room_inst(const RoomInfo *r, int i, Inst *out);

const char *str(uint16_t id);
#endif
