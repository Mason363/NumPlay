/* The launcher's allocator, for the games that draw with its gfx.c (launcher/src/np.h) */
#include <stdint.h>
#include <stdlib.h>
void *np_alloc(uint32_t size) { return calloc(1, size); }
void np_alloc_reset(void) {}
