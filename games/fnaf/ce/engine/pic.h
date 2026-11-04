/* The pictures (tools/cvq.py codes them): grids of 2 x 2 blocks, each one of its group's codewords (four palette
 * indices), the grid's bytes in zx0. A block of 255 keeps what dst holds (a picture coded over its base).
 *
 * A group's map (MAP_*, unpacked once): the codewords (four pages: top left, top right, bottom left, bottom right)
 * at 0, the palette at MAP_PALETTE, two remap tables at MAP_REMAP. */
#ifndef CE_PIC_H
#define CE_PIC_H
#include <stdbool.h>
#include <stdint.h>

#define MAP_PALETTE 1024
#define MAP_REMAP 1536
#define PIC_MAP_SIZE 2048    /* (unpacked: 1024-aligned, the codewords in one 64 KB bank) */

int pic_width(const uint8_t *pic);
int pic_height(const uint8_t *pic);
int pic_base(const uint8_t *pic);   /* the picture it is coded over, or -1 */
bool pic_keeps(const uint8_t *pic); /* some blocks keep what dst holds */
/* decodes the picture into dst (stride bytes a row) with its group's map. Its grid is unpacked into temp (its
 * blocks' count of bytes); temp NULL: into the end of dst itself (a picture that keeps nothing, as wide as dst) */
void pic_decode(const uint8_t *pic, uint8_t *dst, int stride, const uint8_t *map, uint8_t *temp);
/* (the grid's blocks into pixels: pic_ez80.s on the calculator) */
void pic_expand(const uint8_t *grid, uint8_t *dst, int stride, int bw, int bh, const uint8_t *map);
#endif
