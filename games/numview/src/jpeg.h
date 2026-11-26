/* A small baseline JPEG decoder for NumView: Huffman coded, 8-bit, one or three components (any sampling up to
 * 2x2), restart intervals. It writes RGB565 straight into a caller's buffer, at full size or 1/2, 1/4 or 1/8 of it
 * (an eighth is each block's DC alone, with no IDCT), so a zoomed-out picture costs little.
 *
 * Its tables persist: jpeg_setup() reads a stream of DQT, DHT, DRI, SOF0 and SOS headers once (NumView keeps one
 * such stream per file, shared by every tile), then jpeg_scan() decodes the entropy-coded data of each tile with
 * them. jpeg_decode() reads a whole JPEG of its own instead.
 *
 * The IDCT is stb_image's (public domain), itself after the IJG's jidctint. */
#ifndef NV_JPEG_H
#define NV_JPEG_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  uint8_t fast[512]; /* the first 9 bits of a code: its index in values, or 255 */
  uint8_t values[256];
  uint8_t size[257];
  uint16_t code[256];
  uint32_t maxcode[18];
  int32_t delta[17];
} jpeg_huff_t;

typedef struct {
  jpeg_huff_t dc[2], ac[2];
  uint16_t q[4][64]; /* in natural order */
  struct {
    uint8_t id, h, v, tq, td, ta;
  } comp[3];
  uint8_t ncomp, hmax, vmax, ok;
  uint16_t restart, w, h; /* (w, h: the last frame header's) */
} jpeg_t;

/* Reads table and frame headers (no scan data); false if they aren't baseline JPEG NumView can decode. */
bool jpeg_setup(jpeg_t *j, const uint8_t *p, uint32_t n);
/* Decodes the scan data of a w x h image with the tables set, into out (stride pixels a row) at 1/d of its size,
 * d = 1, 2, 4 or 8: ceil(w / d) x ceil(h / d) pixels. False if the data is broken (what was decoded stays). */
bool jpeg_scan(const jpeg_t *j, const uint8_t *data, uint32_t n, int w, int h, int d, uint16_t *out, int stride);
/* A whole JPEG: its headers, then its scan; *w and *h get its size. */
bool jpeg_decode(jpeg_t *j, const uint8_t *p, uint32_t n, int d, uint16_t *out, int stride, int *w, int *h);
#endif
