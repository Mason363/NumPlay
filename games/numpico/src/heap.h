/* Lua's memory (heap.c). */
#ifndef NP8_HEAP_H
#define NP8_HEAP_H
#include <stddef.h>
#include <stdint.h>
void heap_init(uint8_t *start, uint8_t *end);
/* lua_Alloc */
void *heap_alloc(void *ud, void *ptr, size_t osize, size_t nsize);
uint32_t heap_used(void);
uint32_t heap_peak(void);
uint32_t heap_size(void);
#endif
