/* NumPico's carts: the table np_carts in the app's flash, read in place. tools/p8cart.py and NumPlay
 * Studio make it from .p8 and .p8.png files, with the code already unpacked and in P8SCII:
 *   u32 magic "NPP8" (0x3850504E), u32 version 1, u32 count;
 *   count entries of 64 bytes:
 *     u32 rom      from np_carts to the cart's 0x4300 bytes (sprites, map, flags, music, sound);
 *     u32 code     from np_carts to its Lua code, u32 code_len its bytes;
 *     u32 label    from np_carts to its label, or 0: 16 colours (0..15, or 128..143 for the
 *                  secondary ones), then 128 x 128 pixels, 4 bits each, the left one low;
 *     char name[48] the file's name without .p8 or .p8.png (UTF-8, zero-padded);
 *   then the carts' parts, each 4-byte aligned.
 * All numbers little-endian. */
#ifndef NP8_CARTS_H
#define NP8_CARTS_H
#include <stdbool.h>
#include <stdint.h>

#define P8_CARTS_MAGIC 0x3850504Eu
#define P8_CARTS_VERSION 1
#define P8_CARTS_ENTRY 64
#define P8_CARTS_NAME 48
#define P8_MAX_CARTS 64
#define P8_ROM_SIZE 0x4300
#define P8_LABEL_SIZE (16 + 128 * 64)

typedef struct {
  const uint8_t *rom, *label, *name; /* (name: P8_CARTS_NAME bytes, zero-padded, maybe not ended) */
  const char *code;
  uint32_t code_len;
} p8_cart_t;

/* The carts of a table of which `limit` bytes can be read, at most `max`, in the table's order;
   entries that don't hold together are left out. -1 when it isn't a table at all. */
int carts_read(const uint8_t *table, uint32_t limit, p8_cart_t *out, int max);
/* the cart called `name` (any case, with or without .p8 or .p8.png, a path or a "#" before it), or -1 */
int carts_find(const p8_cart_t *carts, int n, const char *name);
/* its file's name, ASCII ('?' for other letters), at most n - 1 bytes */
void carts_name(const p8_cart_t *c, char *out, int n);
/* its title and author: the code's first two comment lines (as PICO-8 writes them: lowercase shows
   as its capitals), or "" */
void carts_title(const p8_cart_t *c, char *title, int tn, char *author, int an);
#endif
