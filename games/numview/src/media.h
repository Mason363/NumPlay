/* NumView's file: np_media, a table in the app's flash made by tools/mediapack.py (or NumPlay Studio), read in
 * place. All numbers little-endian, offsets from the table's start, every part 4-byte aligned.
 *
 *   header (64 bytes): u32 magic "NPMV", u16 version 1, u8 kind, u8 flags, u32 count, u32 jpeg, u32 jpeg_len,
 *     u32 index, u16 backdrop (RGB565), u16 spare, char title[36] (UTF-8, zero-padded)
 *   jpeg: the JPEG headers every JPEG tile is decoded with (DQT, DHT, SOF0, SOS: one set per file)
 *
 * Pictures (kind 1: an image, count 1, or a document's pages, flags 1): index = u32 page[count]. A page:
 *   u8 levels, u8 tile shift (6: 64 x 64 tiles), u16 spare, then per level, smallest first:
 *   u16 w, u16 h, u32 tiles: u32 offsets of its tiles, row by row, and one past the last.
 *   Level 0 fits the screen; a bigger one (4 times as big) is there to zoom into.
 *
 * A tile: u8 codec, then
 *   0 solid:   u16 colour
 *   1 JPEG:    the scan data of a w x h JPEG made with the file's headers
 *   2 palette: u8 colours - 1, u8 spare, u16 colour[n], then its pixels packed by raw DEFLATE (launcher inflate.c):
 *              1, 2, 4 or 8 bits each (2, 4, 16, 256 colours), the left one in the high bits, rows whole bytes
 *
 * Video (kind 2, count frames): index = u16 w, h (its picture, centered), u16 tile w, tile h, u16 frames a second
 *   x 100, u16 key (every key-th frame is whole: seeking lands on those), u32 frame[count + 1]. A frame: a bit per
 *   tile, row by row, the first in the low bit (set: the tile changed), then each changed tile: u16 size, the tile.
 *
 * Book (kind 3, count bytes of text): index = u32 bytes, u16 chunks, u16 spare, u32 start[chunks + 1] (where each
 *   chunk's text starts), u32 at[chunks + 1] (its raw DEFLATE data). A chunk holds 4096 bytes at most and ends after a
 *   space or a new line, so no word is cut. The text is UTF-8 with the letters of the reading fonts
 *   (read_font.h); "\n" ends a paragraph; one starting with 1 is a heading; 2 starts a new page. */
#ifndef NV_MEDIA_H
#define NV_MEDIA_H
#include <stdint.h>

#define NV_MAGIC 0x564D504Eu
#define NV_VERSION 1
#define NV_CHUNK 4096
enum { NV_PICTURES = 1, NV_VIDEO = 2, NV_BOOK = 3 };
enum { NV_SOLID = 0, NV_JPEG = 1, NV_PALETTE = 2 };
enum { NV_DOCUMENT = 1 };

typedef struct {
  uint32_t magic;
  uint16_t version;
  uint8_t kind, flags;
  uint32_t count, jpeg, jpeg_len, index;
  uint16_t backdrop, spare;
  char title[36];
} nv_header_t;

typedef struct {
  uint16_t w, h;
  uint32_t tiles;
} nv_level_t;

typedef struct {
  uint16_t w, h, tw, th, fps100, key;
  uint32_t frame[];
} nv_video_t;

typedef struct {
  uint32_t bytes;
  uint16_t chunks, spare;
  uint32_t start[]; /* then at[] */
} nv_book_t;
#endif
