/* Deltarune: data.bin's BLOCKS, unpacked when needed (tools/pack.py packs them with LZMA).
 *
 * The frames and tiles live in blocks; blk_get unpacks a block into a RAM cache
 * and gives its bytes. A pointer it gives holds until the next blk_get (which can
 * throw a block out to make room), so callers use it right away, and nothing keeps
 * one from a frame to the next: blk_frame, at the frame's start, packs the cache's
 * blocks together. The blocks the frame used are thrown out last. */
#ifndef DR_BLK_H
#define DR_BLK_H
#include <stdint.h>

const uint8_t *blk_get(int id);
void blk_frame(void);
/* (for the tests: blocks unpacked so far, the most in a frame) */
extern uint32_t blk_unpacked, blk_most_in_frame;
#endif
