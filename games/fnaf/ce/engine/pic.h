/* The pictures (tools/cpic.py codes them): decoded into palette indices, a macroblock of 8 x 8 at a time.
 *
 * A picture is H.264's 4 x 4 integer transform of Y, Cb and Cr (the colour at half resolution), Huffman coded;
 * one coded over its base keeps the base's macroblocks it skips (what dst already holds). Each picture group
 * (the office, a camera, a jumpscare...) has a Map: from (Y, Cb, Cr) to its palette, with ordered dithering. */
#ifndef CE_PIC_H
#define CE_PIC_H
#include <stdbool.h>
#include <stdint.h>

#define YQ_LO (-96)   /* the Y values the Map's YQ covers: YQ_LO to YQ_HI - 1 (dither included) */
#define YQ_HI 352
#define CQ_LO (-192)  /* and the colour ones */
#define CQ_HI 192

typedef struct {
  const uint8_t *yq;     /* (YQ_HI - YQ_LO): Y -> its place among the 32 levels, times 8 */
  const uint8_t *cbq;    /* (CQ_HI - CQ_LO): Cb -> its place among 8 levels, times 32 */
  const uint8_t *crq;    /* and Cr */
  const uint8_t *lut;    /* 32 x 8 x 8: the levels -> a palette index */
  const uint8_t *phase;  /* NULL, or 256 random bytes: the static's phase (their low 2 bits), ORed in */
} PicMap;

/* the codec's tables (the Huffman tables, the dequantization steps), from the data: once */
void pic_init(const uint8_t *tables);
/* a picture's size in pixels, and the base picture it is coded over (-1: none) */
int pic_width(const uint8_t *pic);
int pic_height(const uint8_t *pic);
int pic_base(const uint8_t *pic);
/* decodes the picture into dst (stride bytes a row, room for all of it) */
void pic_decode(const uint8_t *pic, uint8_t *dst, int stride, const PicMap *m);
#endif
