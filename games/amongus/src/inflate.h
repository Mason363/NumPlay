/* Among Us: raw DEFLATE (inflate.c). */
#ifndef AU_INFLATE_H
#define AU_INFLATE_H
#include <stdint.h>
/* unpacks n bytes into out (cap bytes at most): the size unpacked, or -1 if the stream is damaged */
int32_t inflate_raw(const uint8_t *in, uint32_t n, uint8_t *out, uint32_t cap);
#endif
